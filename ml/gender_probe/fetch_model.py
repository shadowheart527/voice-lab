#!/usr/bin/env python3
"""Download the ECAPA voice-gender classifier and export it to ONNX (opset 17).

Writes into ml/models/ (gitignored):

    ecapa_gender.safetensors   upstream weights, cached copy
    ecapa_gender_fp32.onnx     full graph, audio in -> 2 logits out

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

    batch = torch.export.Dim("batch", min=1, max=64)
    samples = torch.export.Dim("samples", min=4000, max=16000 * 60)
    try:
        torch.onnx.export(
            model, (example,), str(ONNX_PATH),
            input_names=["waveform"], output_names=["logits"],
            opset_version=OPSET, dynamo=True,
            dynamic_shapes={"x": {0: batch, 1: samples}},
            optimize=True,
        )
    except Exception as exc:  # noqa: BLE001 - fall back to the legacy exporter
        print(f"dynamo export failed ({exc}); retrying with the legacy exporter")
        torch.onnx.export(
            model, (example,), str(ONNX_PATH),
            input_names=["waveform"], output_names=["logits"],
            opset_version=OPSET, dynamo=False,
            dynamic_axes={"waveform": {0: "batch", 1: "samples"},
                          "logits": {0: "batch"}},
            do_constant_folding=True,
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


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--skip-verify", action="store_true",
                    help="skip the torchaudio front-end cross-check")
    args = ap.parse_args()

    weights = download_weights()
    model = build_model(weights)
    if not args.skip_verify:
        verify_frontend(model)
    path = export_onnx(model)
    verify_onnx(model, path)
    print("ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
