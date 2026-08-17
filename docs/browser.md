# The browser build

Everything runs client-side: microphone → AudioWorklet tap → analysis Worker →
canvas. No audio and no measurements leave the device, and there is no server
component beyond static file hosting.

## Running it

Locally it is served by the `voicelab-browser.service` user unit at
<http://127.0.0.1:8181/>. To serve it by hand:

```sh
cd browser && python3 -m http.server 8181 --bind 127.0.0.1
```

**On a phone you need HTTPS.** Browsers only grant microphone access in a secure
context, and `localhost` is the sole exemption, so a plain `http://192.168.x.x`
page will silently fail to get the mic. Options, easiest first: put it behind
Tailscale (which gives real certificates via `tailscale serve`), run a reverse
proxy with a self-signed cert and trust it on the phone, or host it somewhere
with TLS. The page detects an insecure context and says so rather than looking
broken.

`docs/deploy.md` covers both of the routes that give you a real certificate —
GitHub Pages and `tailscale serve` — along with what each one implies for a
private repository and for the AGPL.

## One engine, not two

The browser runs **the desktop engine's own analysis code**, compiled to
WebAssembly: the same RAPT pitch solver, the same Burg linear prediction and
FilteredLP formant solver, the same spectral-tilt measure, the same gender model
with the same compiled-in calibration table, linked against the same FFTW and
libsamplerate rather than substitutes. `engine/src/wasm/voicelab_wasm.cpp` is a
thin C API over that core; the browser owns buffering and scheduling, which its
Worker already did.

Verified on the synthetic weight pair: WebAssembly reads **-10.50 and -19.64
dB/oct** where the native desktop build reads **-10.49 and -19.64**, and the
built, deployable site shows the same through the full worklet and worker path.
There is no second implementation to drift and no second set of calibration
constants.

This reverses an earlier decision recorded here. The first browser build was an
independent JavaScript implementation, argued for on the grounds that the
calibration was reproducible on demand so a rewrite could be *measured* rather
than trusted. That was true, and the measuring did catch two real bugs. But
maintaining two implementations meant the same defect had to be found and fixed
twice, which is exactly what happened with the vocal-weight measure. Sharing one
core removes the failure mode rather than testing for it.

### Building it

```sh
scripts/build-fftw-wasm.sh     # once: cross-compile FFTW for wasm32
scripts/build-wasm.sh          # the core -> browser/wasm/
```

Four things had to give way, all small and all in the repository now: `QMutex`
around FFTW plan creation became `std::mutex`; the vocal-weight measure moved out
of the Qt-bound pitch processor into `src/analysis/weight/`; FFTW needs a host
triple its `config.sub` recognises, because that file predates Emscripten;
libsamplerate is compiled directly, because its CMake refuses to cross-compile.
DeepFormants (libtorch) and `gci/sigma` (armadillo) are excluded, neither having
a browser equivalent nor being on this path.

`browser/wasm/` is committed rather than built in CI, because GitHub Pages cannot
cross-compile Emscripten, and because it guarantees the deployed site is the
artifact that was tested.

### The JavaScript implementation

It still lives in `browser/src/dsp/`. It is no longer the engine; it is an
independent second opinion for `tools/validate-dsp.mjs`, which is worth keeping
precisely because it was written from the same specification by different means.

## Session recording

The **Save** button exports a session summary as JSON: pitch distribution and
time in target, resonance, gender read, weight and size, a histogram of
fullness cells, and a 30-second-bucket timeline for drift. Feed it to the
coach:

```sh
python3 ml/coach/coach.py voice-session-*.json
```

## Verification harness

`tools/smoke-dist.mjs` drives the real page in headless Chromium with a known WAV
injected as a fake microphone (`--use-file-for-fake-audio-capture`), so the whole
client-side chain is testable against ground truth without anyone speaking into a
microphone. `tools/make-test-vowel.py` synthesises the WAV: a jittered glottal
source through four formants, at a stated pitch and F1/F2.

```sh
npm i playwright                                   # once; browsers can be skipped
tools/make-test-vowel.py /tmp/vowel.wav
tools/build-static.sh
node tools/smoke-dist.mjs dist /tmp/vowel.wav
```

It serves the built site under a *path prefix* rather than a domain root,
because that is where deployment-only bugs live: at a root, a document-relative
and a root-relative asset path resolve to the same URL, so a site can be wrong in
a way nothing local reveals. On the default synthetic vowel (140 Hz, F1 620,
F2 1180) the page should report 140 Hz, a resonance around 54% and a masculine
read; a thinner test signal is not enough, and a two-formant pulse train reads
the pitch correctly and the resonance not at all.
