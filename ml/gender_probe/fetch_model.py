#!/usr/bin/env python3
"""Download the ECAPA voice-gender classifier and export it to ONNX (opset 17).

Writes into ml/models/ (gitignored):

    ecapa_gender.safetensors   upstream weights, cached copy
    ecapa_gender_fp32.onnx     full graph, audio in -> 2 logits out
    ecapa_gender_int8.onnx     weight-only int8, for onnxruntime-web

Along the way it checks two things that would otherwise fail silently:

1. the reimplemented log-mel front end against torchaudio's MelSpectrogram
2. the exported ONNX graph against the PyTorch module, on random audio

Both are hard assertions. Run with --skip-verify only if torchaudio is missing.

    python -m gender_probe.fetch_model
"""

from __future__ import annotations

import argparse
import pathlib
import sys

import numpy as np
import torch

if __package__ in (None, ""):
    sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
    from gender_probe.ecapa_gender import ECAPA_gender
else:
    from .ecapa_gender import ECAPA_gender

REPO_ID = "JaesungHuh/voice-gender-classifier"
MODELS_DIR = pathlib.Path(__file__).resolve().parent.parent / "models"
ONNX_PATH = MODELS_DIR / "ecapa_gender_fp32.onnx"
INT8_PATH = MODELS_DIR / "ecapa_gender_int8.onnx"
WEIGHTS_PATH = MODELS_DIR / "ecapa_gender.safetensors"
OPSET = 17


def download_weights() -> pathlib.Path:
    from huggingface_hub import hf_hub_download

    MODELS_DIR.mkdir(parents=True, exist_ok=True)
    cached = hf_hub_download(repo_id=REPO_ID, filename="model.safetensors")
    data = pathlib.Path(cached).read_bytes()
    WEIGHTS_PATH.write_bytes(data)
    print(f"weights: {WEIGHTS_PATH} ({len(data) / 1e6:.1f} MB)")
    return WEIGHTS_PATH


def build_model(weights: pathlib.Path) -> ECAPA_gender:
    from safetensors.torch import load_file

    model = ECAPA_gender(C=1024)
    state = load_file(str(weights))
    missing, unexpected = model.load_state_dict(state, strict=False)
    # Front-end buffers are non-persistent, so they are legitimately "missing"
    # from the checkpoint; anything else missing means a real mismatch.
    hard_missing = [k for k in missing
                    if not k.startswith(("preemph", "dft_kernel", "mel_fb"))]
    if hard_missing or unexpected:
        raise RuntimeError(
            f"state_dict mismatch; missing={hard_missing} unexpected={unexpected}"
        )
    model.eval()
    n_params = sum(p.numel() for p in model.parameters())
    print(f"model: ECAPA_gender C=1024, {n_params / 1e6:.2f}M parameters")
    return model


def verify_frontend(model: ECAPA_gender) -> None:
    """Our conv-based STFT must reproduce torchaudio's MelSpectrogram."""
    import torchaudio

    torch.manual_seed(0)
    wav = torch.randn(1, 16000 * 2) * 0.1

    ours = model.logtorchfbank(wav)

    x = wav.unsqueeze(1)
    x = torch.nn.functional.pad(x, (1, 0), "reflect")
    x = torch.nn.functional.conv1d(
        x, torch.FloatTensor([-0.97, 1.0]).unsqueeze(0).unsqueeze(0)
    ).squeeze(1)
    ref = torchaudio.transforms.MelSpectrogram(
        sample_rate=16000, n_fft=512, win_length=400, hop_length=160,
        f_min=20, f_max=7600, window_fn=torch.hamming_window, n_mels=80,
    )(x) + 1e-6
    ref = ref.log()
    ref = ref - torch.mean(ref, dim=-1, keepdim=True)

    err = (ours - ref).abs().max().item()
    print(f"front end vs torchaudio: max abs diff {err:.3e} over {tuple(ours.shape)}")
    if err > 2e-3:
        raise RuntimeError(f"log-mel front end does not match torchaudio (max {err})")


def export_onnx(model: ECAPA_gender) -> pathlib.Path:
    MODELS_DIR.mkdir(parents=True, exist_ok=True)
    example = torch.randn(1, 16000)

    # The TorchScript exporter is tried first on purpose: it is what produced the
    # graph the validation numbers in README.md were measured on, it needs no
    # onnxscript, and it emits a flat Conv/MatMul graph that onnxruntime-web and
    # the dynamic quantiser both handle without special cases. torch.export is
    # the fallback for when the legacy path is eventually removed.
    try:
        torch.onnx.export(
            model, (example,), str(ONNX_PATH),
            input_names=["waveform"], output_names=["logits"],
            opset_version=OPSET, dynamo=False,
            dynamic_axes={"waveform": {0: "batch", 1: "samples"},
                          "logits": {0: "batch"}},
            do_constant_folding=True,
        )
    except Exception as exc:  # noqa: BLE001
        print(f"legacy export failed ({exc}); retrying with torch.export")
        batch = torch.export.Dim("batch", min=1, max=64)
        samples = torch.export.Dim("samples", min=4000, max=16000 * 60)
        torch.onnx.export(
            model, (example,), str(ONNX_PATH),
            input_names=["waveform"], output_names=["logits"],
            opset_version=OPSET, dynamo=True,
            dynamic_shapes={"x": {0: batch, 1: samples}},
            optimize=True,
        )

    size_mb = ONNX_PATH.stat().st_size / 1e6
    print(f"onnx:   {ONNX_PATH} ({size_mb:.1f} MB, opset {OPSET})")
    return ONNX_PATH


def verify_onnx(model: ECAPA_gender, path: pathlib.Path) -> None:
    import onnxruntime as ort

    sess = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"])
    inp = sess.get_inputs()[0].name

    worst = 0.0
    for n_samples in (12000, 16000, 24000):
        torch.manual_seed(n_samples)
        wav = (torch.randn(1, n_samples) * 0.1).clamp(-1, 1)
        with torch.no_grad():
            ref = model(wav).numpy()
        got = sess.run(None, {inp: wav.numpy()})[0]
        worst = max(worst, float(np.abs(ref - got).max()))

    print(f"onnx vs torch: max abs logit diff {worst:.3e} over 3 lengths")
    if worst > 1e-3:
        raise RuntimeError(f"ONNX graph disagrees with PyTorch (max {worst})")


def quantize_int8(src: pathlib.Path) -> pathlib.Path:
    """Weight-only dynamic int8, for shipping to onnxruntime-web.

    The attentive-pooling MatMul is excluded. It multiplies two runtime
    activations (attention weights x frame features) rather than a weight by an
    activation, so dynamic quantisation hits both operands and costs far more
    accuracy there than in any of the convolutions. Alice-Sabrina-Ivy documented
    this on their export of the same model; the exclusion is cheap and it also
    makes the graph slightly faster by skipping a quantise/dequantise round trip.
    """
    from onnxruntime.quantization import QuantType, quantize_dynamic
    from onnxruntime.quantization.shape_inference import quant_pre_process

    import onnx

    # Find MatMuls with no initializer operand: those are the activation x
    # activation products, the ones dynamic quantisation damages.
    graph = onnx.load(str(src)).graph
    inits = {i.name for i in graph.initializer}
    exclude = [n.name for n in graph.node
               if n.op_type == "MatMul" and not (set(n.input) & inits)]

    prepped = src.with_name(src.stem + "_prep.onnx")
    quant_pre_process(str(src), str(prepped), skip_symbolic_shape=False)
    quantize_dynamic(str(prepped), str(INT8_PATH), weight_type=QuantType.QUInt8,
                     per_channel=True, nodes_to_exclude=exclude,
                     extra_options={"MatMulConstBOnly": True})
    prepped.unlink(missing_ok=True)

    print(f"int8:   {INT8_PATH} ({INT8_PATH.stat().st_size / 1e6:.1f} MB, "
          f"excluded {len(exclude)} activation-x-activation MatMul node(s))")
    return INT8_PATH


def compare_int8(fp32: pathlib.Path, int8: pathlib.Path) -> None:
    import onnxruntime as ort

    a = ort.InferenceSession(str(fp32), providers=["CPUExecutionProvider"])
    b = ort.InferenceSession(str(int8), providers=["CPUExecutionProvider"])

    deltas = []
    rng = np.random.default_rng(0)
    for _ in range(8):
        wav = (rng.standard_normal((1, 16000)) * 0.1).astype(np.float32)
        pa = _softmax(a.run(None, {a.get_inputs()[0].name: wav})[0])[0, 1]
        pb = _softmax(b.run(None, {b.get_inputs()[0].name: wav})[0])[0, 1]
        deltas.append(abs(pa - pb))
    print(f"int8 vs fp32: mean |dp| {np.mean(deltas):.4f}, max {np.max(deltas):.4f} "
          f"(random noise, 8 windows)")


def _softmax(x: np.ndarray) -> np.ndarray:
    e = np.exp(x - x.max(axis=1, keepdims=True))
    return e / e.sum(axis=1, keepdims=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--skip-verify", action="store_true",
                    help="skip the torchaudio front-end cross-check")
    ap.add_argument("--no-int8", action="store_true",
                    help="skip the quantised browser export")
    args = ap.parse_args()

    weights = download_weights()
    model = build_model(weights)
    if not args.skip_verify:
        verify_frontend(model)
    path = export_onnx(model)
    verify_onnx(model, path)
    if not args.no_int8:
        int8 = quantize_int8(path)
        compare_int8(path, int8)
    print("ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
