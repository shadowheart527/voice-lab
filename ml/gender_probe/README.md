# gender_probe

A continuous 0..1 read of how a listener-model genders a voice. 0 reads masculine,
1 reads feminine, and the middle is the part that matters for training feedback.

The method is the one from [Voice Passing (arXiv:2404.15176)](https://arxiv.org/abs/2404.15176):
rather than ask a binary gender classifier for a class, run it over short overlapping
windows and average the posterior. A voice every window calls female sits near 1;
one that flips window to window sits near 0.5; the graded values in between are what
you can actually train against. It complements the acoustic measures elsewhere in
this repo, which tell you *what* your voice is doing. This tells you what a machine
listener trained on a lot of voices does with the result.

## Model

| | |
|---|---|
| Base | [`JaesungHuh/voice-gender-classifier`](https://huggingface.co/JaesungHuh/voice-gender-classifier) |
| Architecture | ECAPA-TDNN, C=1024, two-class head |
| Parameters | 15.44 M |
| Training data | VoxCeleb2 dev, fine-tuned from TaoRuijie's speaker-verification ECAPA |
| Upstream accuracy | 98.7 % on the VoxCeleb1 identification test split |
| License | MIT (both the [weights](https://huggingface.co/JaesungHuh/voice-gender-classifier) and the [code](https://github.com/JaesungHuh/voice-gender-classifier)) |
| Downloads | ~345 k/month on the Hub, so it is well exercised |

Chosen over the alternatives for size and licence. The obvious competitor,
`alefiury/wav2vec2-large-xlsr-53-gender-recognition-librispeech`, is Apache-2.0 and
popular but is a wav2vec2-large at roughly 1.2 GB, which is not going anywhere near a
browser. ECAPA at 62 MB fp32 and 16 MB int8 is small enough to ship and, being a
speaker-verification backbone, encodes voice quality rather than just pitch.

### Exported artifacts

`fetch_model.py` writes these into `ml/models/` (gitignored, so re-run it on a fresh
checkout):

| file | size | use |
|---|---|---|
| `ecapa_gender.safetensors` | 61.9 MB | cached upstream weights |
| `ecapa_gender_fp32.onnx` | 63.1 MB | reference graph, opset 17 |
| `ecapa_gender_int8.onnx` | 16.1 MB | weight-only int8, for onnxruntime-web |

The ONNX graph takes raw mono float32 at 16 kHz, shape `(batch, samples)`, and returns
pre-softmax logits `(batch, 2)` over `{0: male, 1: female}`. Preprocessing is inside the
graph, so a caller needs no mel front end of its own.

### What was changed from upstream

Only the front end. Upstream's `forward` calls
`torchaudio.transforms.MelSpectrogram`, constructing a module on every call, which does
not trace into a portable graph. `ecapa_gender.py` precomputes the Hamming window, the
DFT basis and the HTK mel filterbank as buffers and runs the STFT as a strided `conv1d`,
so the whole thing is Conv/Pad/MatMul/Log and nothing exotic. That avoids depending on
ONNX `STFT` op coverage in onnxruntime-web, and it costs nothing: the DFT conv is about
0.3 % of the model's arithmetic.

Both substitutions are asserted at export time, not assumed:

```
front end vs torchaudio: max abs diff 5.484e-05 over (1, 80, 201)
onnx vs torch:           max abs logit diff 5.364e-06 over 3 lengths
```

The int8 export excludes exactly one node, the attentive-statistics-pooling MatMul.
That node multiplies two runtime activations (attention weights by frame features)
rather than a weight by an activation, so dynamic quantisation degrades both operands
and it takes far more damage than any of the 38 convolutions, whose weights are static
and per-channel calibrated. This was documented by
[Alice-Sabrina-Ivy](https://huggingface.co/Alice-Sabrina-Ivy/voice-gender-classifier-onnx-q8-v2)
on their export of the same model for Syrinx; `fetch_model.py` finds the node structurally
(a MatMul with no initializer operand) rather than by name, and confirms it finds exactly one.

## Validation

Measured 17 Aug 2026 against the 21 reference clips in the genderspace calibration set,
1.0 s window, 0.25 s hop, silence gate at -45 dBFS. Ground truth is the source project's
labelling of how each clip reads. Three clips (`all`, `charlotte`, `fern`) carry no label
and are scored but excluded from the statistics; `all` is a concatenation of the others,
which is why it lands mid-scale.

| clip | truth | fp32 | int8 | median (fp32) | sd | windows |
|---|---|---|---|---|---|---|
| bob | masc | **0.003** | 0.005 | 0.002 | 0.002 | 66 |
| david | masc | **0.005** | 0.006 | 0.003 | 0.005 | 113 |
| chuck | masc | **0.009** | 0.011 | 0.003 | 0.015 | 45 |
| chris | masc | **0.015** | 0.020 | 0.007 | 0.025 | 157 |
| leonard | masc | **0.092** | 0.066 | 0.022 | 0.141 | 33 |
| aiden | masc | **0.395** | 0.345 | 0.359 | 0.334 | 94 |
| steve | masc | **0.589** | 0.497 | 0.716 | 0.337 | 73 |
| all | *(mixed)* | 0.641 | 0.606 | 0.899 | 0.414 | 104 |
| quinn | fem | **0.828** | 0.787 | 0.914 | 0.171 | 32 |
| ashley | fem | **0.861** | 0.818 | 0.942 | 0.203 | 99 |
| fern | *(unlabelled)* | 0.925 | 0.879 | 0.962 | 0.081 | 28 |
| charlotte | *(unlabelled)* | 0.934 | 0.922 | 0.988 | 0.152 | 80 |
| lesley | fem | **0.948** | 0.907 | 0.972 | 0.065 | 96 |
| morgan | fem | **0.948** | 0.936 | 0.965 | 0.052 | 111 |
| cheryl | fem | **0.951** | 0.946 | 0.966 | 0.039 | 67 |
| lucy | fem | **0.959** | 0.943 | 0.978 | 0.044 | 80 |
| wina | fem | **0.965** | 0.962 | 0.967 | 0.021 | 58 |
| jenn | fem | **0.968** | 0.952 | 0.983 | 0.038 | 67 |
| kristen | fem | **0.972** | 0.969 | 0.981 | 0.027 | 53 |
| luna | fem | **0.972** | 0.964 | 0.986 | 0.066 | 75 |
| zoe | fem | **0.987** | 0.983 | 0.994 | 0.028 | 70 |

| statistic (18 labelled clips, 7 masc / 11 fem) | fp32 | int8 |
|---|---|---|
| ROC AUC | **1.0000** | **1.0000** |
| best-threshold accuracy | 1.0000 at 0.709 | 1.0000 at 0.642 |
| accuracy at a naive 0.5 cut | 0.9444 (17/18) | 1.0000 (18/18) |
| highest masculine clip | 0.589 (steve) | 0.497 (steve) |
| lowest feminine clip | 0.828 (quinn) | 0.787 (quinn) |
| separation margin | +0.239 | +0.290 |

The two groups separate completely, with a quarter of the scale of clear air between
them. Reproduce with:

```sh
ml/.venv/bin/python -m gender_probe.validate --model ml/models/ecapa_gender_int8.onnx path/to/refclips
```

Three things in that table are worth reading properly rather than skimming.

**The decision boundary is not 0.5.** On this corpus the clean cut sits around 0.64 to
0.71, so the model leans feminine relative to a naive threshold. That is a property of
VoxCeleb2's balance and of these particular clips, not a law. Do not hardcode 0.5 as
"the line"; if you must show a line, calibrate it, and prefer showing the number and
its spread.

**The interesting clips are the ambiguous ones.** `steve` and `aiden` are labelled
masculine and land at 0.50 to 0.59 and 0.31 to 0.40, with per-window standard deviations
of 0.33, meaning the model genuinely flips window to window on them. That is the model
working as intended: those clips are androgynous demonstrations, and a graded output
that says "this one is a coin flip" carries more information than a hard label would.
The `sd` column is not noise to be smoothed away, it is signal about consistency.

**int8 tracks fp32 closely but not exactly.** Mean absolute difference across all 21
clips is 0.022, worst case 0.092 on `steve`, and the worst case falls on precisely the
clip where the model is least certain. Both give AUC 1.0. The int8 graph is what should
ship to the browser; the fp32 graph is the reference when a number looks wrong.

**One clean sweep on 18 clips is a floor, not a ceiling.** These are curated tutorial
and demonstration voices, recorded well, mostly at the ends of the range. A perfect AUC
here says the probe is not broken. It does not say it is accurate on a quiet phone
recording of a voice mid-transition, which is the case that actually matters, and for
which no ground truth exists here.

## Performance

Single 1.0 s window, onnxruntime CPU, i9-13900K:

| graph | 1 thread | 4 threads |
|---|---|---|
| fp32 | 575 ms | 162 ms |
| int8 | **34.5 ms** | **16.1 ms** |

int8 is 17x faster than fp32, not merely smaller, because the graph is convolution-bound
and int8 kernels are far better optimised. At 34.5 ms per window single-threaded there is
ample headroom for a 4 Hz live meter in a browser with one worker.

Batch inference is not worth it here. It gives no speedup, and for the int8 graph it
changes the answer: dynamic quantisation derives activation scales per tensor over
whatever is in the batch, so a window's score depends on its neighbours. Batching 16
shifts clip means by up to 0.01 against single-window inference. `GenderProbe` therefore
defaults to `batch_size=1`, which is reproducible, matches what the browser will do, and
is also the fastest option.

## Usage

Fetch and export once:

```sh
ml/.venv/bin/python -m gender_probe.fetch_model
```

Score a file:

```sh
ml/.venv/bin/python -m gender_probe.probe --model ml/models/ecapa_gender_int8.onnx clip.wav
# clip.wav              0.922  median  0.988  sd 0.189  n= 80  reads feminine
```

From Python, batch or streaming:

```python
from gender_probe.probe import GenderProbe, StreamingGenderProbe

probe = GenderProbe("ml/models/ecapa_gender_int8.onnx")

result = probe.score_file("clip.wav")
result.score          # 0.922, mean posterior over kept windows
result.median         # 0.988, robust to a few odd windows
result.std            # 0.189, how much it flips window to window
result.window_scores  # per-window array, for plotting against time

# live: push arbitrary chunks, read a smoothed value
live = StreamingGenderProbe(probe, ema_alpha=0.2)
for chunk in mic_chunks:                 # any length, any sample rate
    value = live.push(chunk, sr=48000)   # None until a full window lands
```

Input at any sample rate is resampled to 16 kHz internally with a Kaiser-windowed sinc,
so no scipy or torchaudio is needed at inference time. Windows quieter than -45 dBFS RMS
are dropped: silence carries no gender information but does get confidently classified,
and it drags the mean otherwise. `StreamingGenderProbe` holds its last value through
silence rather than decaying it.

### In the browser

`ecapa_gender_int8.onnx` runs directly under onnxruntime-web. Preprocessing is inside
the graph, so the only work on the JS side is getting mono float32 at 16 kHz into a
tensor:

```js
const session = await ort.InferenceSession.create('models/ecapa_gender_int8.onnx',
                                                  { executionProviders: ['wasm'] });
const tensor = new ort.Tensor('float32', window16k, [1, window16k.length]);
const { logits } = await session.run({ waveform: tensor });
const [m, f] = logits.data;
const femininity = 1 / (1 + Math.exp(m - f));   // two-class softmax
```

Use the WASM execution provider, not WebGPU. Alice-Sabrina-Ivy measured the WebGPU EP on
this quantised graph creating a session successfully and then failing at run time with
BindGroupLayout validation errors around Concat, while running about 4x slower than WASM.
Because session creation succeeds, a try-WebGPU-catch-fallback pattern selects the broken
path rather than avoiding it.

## Caveats, stated plainly

The training data is VoxCeleb2: celebrity interview audio, labelled with a binary
cis-normative gender variable, skewed toward the speakers who get interviewed in English
and a handful of other languages. Every number this probe emits is calibrated to that
distribution. Upstream says as much in its own model card, and it is right to.

What follows from that, concretely:

The output is **a model's read, not a listener's, and not a verdict**. It estimates how
one particular network trained on one particular corpus sorts an audio window. Real
listeners bring context, expectation, visual information, accent familiarity and their
own politics. Nothing here measures whether a voice "passes", whether it is good, or
whether it is yours enough.

The scale is **not calibrated to any external quantity**. 0.7 does not mean 70 % of
listeners, or 70 % of anything. It is a posterior from a softmax over two classes that
were defined by whoever labelled VoxCeleb. Treat it as an ordinal signal you can watch
move while you change something about your voice, which is the use it is actually good
for, and not as a score to be maximised.

The model has **never seen a voice mid-transition**, at least not knowingly and not in
any balanced way. Voices in the androgynous middle are exactly where its training
distribution is thinnest and where the per-window variance goes highest. The `std` field
exists so that uncertainty is visible in the output rather than hidden by the mean.

It is **binary by construction**. A two-class head cannot represent a non-binary voice,
and mapping its output onto a masculine-to-feminine line does not make that line real.
It is a convenient axis for feedback, not a description of gender.

Short windows are noisy. Below about 3 s of speech there are too few windows for the
mean to settle; the CLI reports `n` so you can see when that is the case. Background
noise, music and multiple speakers all degrade it silently, with no flag raised.

## Provenance and licence

Weights and the original architecture are MIT, copyright 2024 jaesunghuh, from
[JaesungHuh/voice-gender-classifier](https://github.com/JaesungHuh/voice-gender-classifier),
itself derived from TaoRuijie's ECAPA-TDNN implementation. `ecapa_gender.py` is a
derivative of that MIT code and carries the same licence. The int8 exclusion recipe
follows a finding published by Alice-Sabrina-Ivy under MIT.

Nothing here is AGPL-encumbered, so this directory can be used from either side of the
repo's licence split. See `docs/LICENSING.md`.
