# Licensing

This tree vendors two upstream projects under different licences, so which terms apply
depends on which directory the code lives in.

| Path | Licence | Origin |
|---|---|---|
| `engine/` | Apache-2.0 | [in-formant/in-formant](https://github.com/in-formant/in-formant) |
| `genderspace/` | AGPL-3.0 | [lmcnulty/gender-voice-visualization](https://github.com/lmcnulty/gender-voice-visualization) |
| `browser/` | AGPL-3.0 (see below) | new work |
| `ml/`, `scripts/`, `docs/` | Apache-2.0 | new work |

## Why `browser/` is AGPL

The browser build carries the acousticgender.space resonance scale and its per-phoneme
population statistics (`browser/src/dsp/phone-stats.js`, generated from that project's
`stats.json`), and reproduces its genderspace visualisation. That is derivative of the AGPL
work, so the browser build is AGPL-3.0 too.

The practical consequence: **if you ever serve the browser build to other people over a
network, you must offer them its source.** The AGPL's network clause treats serving a page
as distribution. Running it locally, on your own devices, or over a private network for your
own use triggers nothing at all.

`browser/tools/build-static.sh` discharges that obligation for you: a built site carries the
licence and a source archive of the revision it was built from, linked from the page footer,
and `browser/tools/check-dist.mjs` fails the build if that link is missing. So a deployment
is compliant without the repository itself having to be public. See `docs/deploy.md`.

`browser/src/dsp/core.js` is an independent implementation of standard signal-processing
algorithms and carries no upstream code, but it ships as part of an AGPL work here.

## Vendored dependencies

`engine/external/` contains third-party sources under their own licences (tomlplusplus MIT,
rnnoise BSD-3-Clause, libsamplerate BSD-2-Clause, armadillo Apache-2.0, rpmalloc public
domain/MIT). `engine/external/libtorch` is fetched by `scripts/bootstrap.sh` and is BSD-3-Clause.

## Model weights

Anything under `ml/models/` is downloaded, not distributed here, and keeps its own licence.
Check the relevant `ml/*/README.md` before redistributing a model or anything derived from
its outputs.

## Upstream contributions

Several fixes in `engine/` are genuine upstream bugs rather than local adaptations, notably
the spectrogram texture-mapping defect, the uninitialised read in font initialisation, the
per-frame font atlas rebuild, and the config that never persisted because the destructor
never ran. Those are Apache-2.0 and would be welcome upstream; the fork keeps its history so
they can be cherry-picked into a pull request.
