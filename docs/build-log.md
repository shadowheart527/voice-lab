# voice-lab: consolidation, a browser build, and the AI layer

17 Aug 2026. Everything from the InFormant and acousticgender.space work is now one
repository, there is a version that runs entirely in a browser, and the AI capabilities
assessed yesterday are implemented or evaluated with measured results.

Repository: <https://github.com/shadowheart527/voice-lab> (private).
Working tree: `~/git/voice-lab/`.

## Consolidation

Both forks live in one tree with their full upstream histories preserved (249 commits):
`engine/` is the patched InFormant, `genderspace/` is the acousticgender.space fork with the
live overlays, and `browser/`, `ml/`, `docs/`, `scripts/` are new. History came across by
subtree merge rather than by copying files, so the provenance of every upstream commit
survives and an upstream pull request remains possible later.

Three things needed fixing on the way. Git submodules cannot work under a subtree layout at
all, because git only reads `.gitmodules` at the repository root, so `tomlplusplus` (header
only, and the build genuinely needs it) is vendored directly and the unused Android/Windows/
macOS deploy submodules were dropped. The launcher scripts and calibration tools now locate
themselves relative to their own path instead of hardcoding a home directory, so the folder
can move again freely. And the `.gitignore` gained rules covering the actual build
directories and libtorch: the old one covered `/build` while the real directories are
`build-fedora*`, so a `git add -A` would have committed 1.4 GB of build output.

The desktop menu entries and the local web service were repointed, both binaries rebuild
cleanly in the new location, and `scripts/bootstrap.sh` sets up a fresh machine (container,
libtorch, python environment, generated tables).

## The browser build

`browser/` runs the whole analysis client-side: microphone into an AudioWorklet tap, which
hands blocks to a Worker that does pitch, formants, vocal weight and the calibrated gender
scoring, drawn as either the genderspace plot or the TVL fullness space in one mobile-first
page. No audio and no measurements leave the device. Served locally at
<http://127.0.0.1:8181/>.

**The reimplementation decision, and why yesterday's recommendation was wrong.** The earlier
assessment argued for compiling the C++ engine to WebAssembly, reasoning that a rewrite would
reopen weeks of calibration verification. The concern was right; the conclusion was not,
because the calibration is *reproducible on demand*. There is a harness that re-derives it
from ground truth, so a fresh implementation can be measured rather than trusted. Given that,
plain JavaScript avoids an Emscripten toolchain, an FFTW substitution and the extraction of
an analysis core from a Qt-entangled codebase.

**Calibration is re-derived, not inherited.** Calibration constants encode a particular
tracker's measurement biases, so they do not transfer between implementations. The validator
runs the browser tracker over acousticgender.space's 21 reference clips at the phoneme
midpoints where the original pipeline recorded praat's formants, fits the tracker-to-praat
unit maps by the same robust procedure, and checks the result leave-one-clip-out against the
official medians. Measured: pitch 6.5 Hz mean absolute error against praat (desktop engine:
6.4), and the resonance scale at 0.066 clip-median error, slightly *better* than the desktop
engine's 0.073.

**The validator earned its keep twice.**

First, an aliasing bug in the Burg recursion: the backward prediction error array was updated
in place while still being read for later indices, so the recursion fed on its own
partially-updated state. LPC coefficients came out around −300 where they should be order 1.
Formant extraction returned essentially nothing, which the clip-level numbers made obvious
(155 usable frames where the desktop engine found 2192, and a fitted map with a negative
slope, meaning no correlation at all).

Second, and more consequential: **the vocal weight measure was inverted, in both
implementations.** Collecting every harmonic that clears the noise floor and fitting a slope
sounds reasonable and is subtly wrong. A light voice's upper harmonics fall below the floor,
so its slope gets fitted only across the surviving low harmonics, where the F1 resonance is
*rising*, while a heavy voice is fitted across the whole falling spectrum. The two are
measured over different frequency ranges and are not comparable. On a synthetic pair with
known source tilts, the light half was reported as flatter, meaning heavier, than the heavy
half. This is the same defect behind the original field report that a light breathy voice
read as heavy: the SNR gate added earlier fixed the noise sensitivity but not this.

The replacement compares fixed low (190-900 Hz) and high (1500-3200 Hz) bands, so every voice
is measured on the same ruler, and a voice with nothing in the high band floors out as very
light rather than returning no reading. Verified three ways: clean and noisy synthetic pairs
now give *identical* answers, so it is noise-immune; the TransVoiceLessons light and heavy
demonstrations separate correctly; and the C++ and JavaScript implementations agree to within
0.2 dB/oct on the same signal. The fix was ported back into the desktop engine, so the
existing pages are corrected too.

**Verification method worth keeping.** The browser app is tested by driving the real page in
headless Chrome with a known WAV injected as a fake microphone
(`--use-file-for-fake-audio-capture`), so the entire client-side chain is testable against
ground truth without anyone speaking. That is how the inversion was caught in the live app
and not just in the unit tests.

**Phone use needs HTTPS.** Browsers grant microphone access only in a secure context and
`localhost` is the sole exemption, so a plain `http://192.168.x.x` page will silently fail.
Tailscale (`tailscale serve`) is the least painful fix; the page detects an insecure context
and says so rather than appearing broken.

## Session recording and coaching

The browser app records a session (pitch distribution, time in target, resonance, gender
read, weight and size, a histogram of fullness cells, and a 30-second-bucket timeline for
drift) and exports it as JSON. `ml/coach/coach.py` turns that into a debrief.

The design point worth noting: the observations are computed deterministically in Python, and
a local LLM is used only to choose the wording, under explicit instructions not to invent
numbers or trends. With no LLM reachable (LM Studio is installed here but its server is not
running) it prints the deterministic summary, so it always works and a hallucinating model
cannot fabricate progress that did not happen. The advice picks its lever from where time was
actually spent rather than from the balance of session medians, because a voice can average
"balanced" while having spent two thirds of the session underfull.

## The AI layer

### The listener-model read (shipped, desktop and browser)

The biggest genuine capability gap is now filled. An ECAPA-TDNN gender classifier (MIT, 15.4
million parameters) runs over overlapping one-second windows and its posterior is averaged
into a continuous read, which is the Voice Passing method. This answers the question the
acoustic model structurally cannot: the acoustic measures say what your voice is *doing*,
this says what a machine listener trained on many thousands of voices *does with the result*,
including all the prosody and phonation the two-cue model cannot see.

Validated against the 21 reference clips: **ROC AUC 1.0000**, with a quarter of the scale of
clear air between the masculine-read and feminine-read groups. Quantised to int8 it is 16 MB
and takes 34.5 ms per window single-threaded, seventeen times faster than fp32 rather than
merely smaller, which is what makes it viable in a browser. Verified running live in the
browser under onnxruntime-web, where Luna's reference clip reads 96 to 98 per cent against
the offline measurement of 0.964.

Three findings from that work are worth carrying forward. The decision boundary on this
corpus sits near **0.65, not 0.5**, so a naive midpoint would be miscalibrated. The
per-window standard deviation is signal rather than noise: on the genuinely androgynous
demonstration clips the model flips window to window, and that inconsistency is information a
hard label would destroy. And WebGPU must be avoided for this graph, because it creates a
session successfully and only then fails at run time, meaning the obvious
try-WebGPU-and-fall-back pattern selects the broken path.

The caveats are documented in the repository and are load-bearing rather than decorative: the
model is binary by construction, trained on cis-labelled celebrity interview audio, has
effectively never seen a voice mid-transition (exactly where its variance goes highest), and
its scale is not calibrated to any external quantity. A perfect sweep on 18 curated, well
recorded, mostly end-of-range clips establishes that it is not broken; it says nothing about
accuracy on a quiet phone recording of a voice in transition, which is the case that actually
matters and for which no ground truth exists here.

### DeepFilterNet: a negative result that overturns yesterday's recommendation

Yesterday's assessment called a DeepFilterNet virtual microphone "the most boring and possibly
most effective item on the list". Measurement says otherwise, and this is the most useful
finding of the day.

DeepFilterNet3 is a much better model than the RNNoise the engine already rejected, and for
exactly that reason it is **better at recognising a held vowel as noise**. On a perfectly
steady 200 Hz sustained vowel it drove the signal down by 56 dB, raised its own artifact
floor by 39 dB, and pushed the pitch estimate down an octave. On the realistic case, a held
note with vibrato and shimmer, the tone survives about one second before the model decides it
is background, then sits 25 to 50 dB down for the remaining 29 seconds and never recovers.

Sustained-phonation drills are a core part of voice training and are precisely what this would
silently destroy. It is safe for cleaning up recordings you listen back to, and not safe
anywhere upstream of the analysis at default settings. There is a usable operating point at a
limited attenuation setting, and it is not the default. The existing gate-only RNNoise
arrangement stands.

### PESTO, and a third bug it exposed

The neural pitch tracker is not worth adopting: 6 to 10 times less accurate than the engine's
existing trackers on clean speech, and it invents 8 to 11 cents of frame-to-frame jitter,
the same order as real vibrato, so it would blur exactly the hold-steady drills it would be
used for. It is genuinely excellent on badly degraded audio, which makes it worth remembering
for archival material rather than live feedback.

Benchmarking it turned up a real defect in the engine, though. The MPM tracker seeded its
autocorrelation normaliser with an absolute 0.02 floor on a quantity that scales with signal
power, so on quiet input the division stopped normalising and it reported unvoiced. Measured
on a 200 Hz vowel it started dropping frames at -15 dBFS and was completely dead by -18 dBFS,
and a well-recorded voice sits around -20 dBFS. It was failing at ordinary recording levels.
One line; it is now scale-invariant with an identical estimate down to -73 dBFS.

### Session coaching (shipped)

Covered above: deterministic observations, optional local LLM for wording only.

### Not built today

Streaming phoneme recognition for true per-phoneme resonance, and the Whisper-based local
replacement for the upload backend, are both still just designs. They are the two remaining
items from yesterday's list, and neither is blocked by anything discovered today.

## Standing caveats

Everything measured here is a feedback signal, not a verdict. The gender model is a
two-cue acoustic estimate; the perceived-gender probe is a neural model trained on cis binary
data and calibrated to somebody else's listener panel; the weight and resonance anchors come
from one teacher's demonstrations and one project's reference speakers. They are useful for
watching your own voice change over time, which is what they are for, and they are not
authorities on anyone's voice.
