# Putting the browser build on the web

The browser build is static: microphone → AudioWorklet → Worker → canvas, with no
server component beyond file hosting. Deploying it is therefore only ever a
question of copying a directory somewhere with TLS — and TLS is the whole point,
because browsers grant microphone access only in a secure context and `localhost`
is the sole exemption. A page served over plain HTTP from a LAN address silently
fails to get a microphone, which is why the phone case needs a real certificate.

Two routes, differing in who else can reach it:

| Route | Who can load the page | Setup |
|---|---|---|
| GitHub Pages | anyone with the URL | this repository's workflow, plus one settings change |
| `tailscale serve` | only your own devices | one command, no deploy |

Neither changes what the app does with audio: analysis is entirely client-side
and no recording or measurement is uploaded anywhere in either case.

## The build

```sh
cd browser && npm ci          # onnxruntime-web, for the optional neural probe
tools/build-static.sh         # assembles browser/dist
node tools/check-dist.mjs browser/dist
python3 -m http.server 8181 --directory dist --bind 127.0.0.1
```

`tools/build-static.sh` gathers what the working tree keeps out of git —
`vendor/` from npm, the gender-probe model if a copy is around — and adds the
AGPL licence and source archive described below. `tools/check-dist.mjs` then
asserts that every asset the page references is actually present and that nothing
is addressed root-relatively, since a root-relative path works when the site is
served from a domain root and breaks under a path prefix.

The model is genuinely optional. Without it the listener-model reading stays
hidden and every acoustic measure works as normal.

## GitHub Pages

`.github/workflows/deploy-browser.yml` builds and publishes on every push to
`main` that touches `browser/`, and on demand from the Actions tab. It needs one
thing done by hand first:

**Settings → Pages → Build and deployment → Source: GitHub Actions.**

Until that is set the deploy step fails with "Pages site not found". After it is
set, the site appears at `https://<user>.github.io/voice-lab/` — served under a
path prefix, which is what the asset-path check above exists to protect.

Two things to know before publishing, both consequences of this repository being
private:

- **Pages from a private repository needs a paid plan** (Pro, Team or
  Enterprise). On a free plan the option is unavailable; the private routes below
  work regardless.
- **The published site is public even though the repository is not.** Access
  control for Pages exists only on Enterprise. The URL is unlisted rather than
  secret: anyone who has it can load the page. Nothing about your voice is
  exposed by that — there is no server to store anything — but the page itself,
  including anything you have written into the interface, is world-readable.

### The AGPL source offer

`browser/` is AGPL-3.0, because it carries acousticgender.space's resonance scale
and per-phoneme statistics and reproduces its genderspace visualisation. The
AGPL's network clause treats serving the page to other people as distribution, so
a public deployment has to offer those people its source. Running it locally or
across your own devices triggers nothing.

The build satisfies this without making the repository public: it ships
`LICENSE` and `source/voice-lab-browser-src.tar.gz` — the app, the calibration
harness, the table generator and the upstream statistics it reads, stamped with
the revision — and links both from the page footer. `check-dist.mjs` fails the
build if that footer link is missing, so the offer cannot quietly disappear.

If you later make the repository public, a link to it is a simpler answer and the
archive becomes redundant.

### Publishing the neural probe model

The 16 MB ONNX is deliberately not in git. To include it in deployments, attach
it to a release once:

```sh
gh release create model ml/models/ecapa_gender_int8.onnx \
    --title 'gender-probe model' \
    --notes 'ECAPA voice-gender classifier, int8. MIT, from JaesungHuh/voice-gender-classifier.'
```

The workflow downloads that asset if it exists and deploys without it if it does
not. Pass a different tag through the workflow's `model_tag` input to use another
release. The weights are MIT, so redistributing them is fine as long as the
notice travels with them; `ml/gender_probe/README.md` has the provenance and the
caveats that matter more than the licence — the model is binary by construction,
trained on cis-normative labels, and has effectively never heard a voice
mid-transition.

Note that on a public site this is 16 MB fetched over whatever connection the
phone has, once, on first use of that panel.

## Keeping it to your own devices

If the point is only to reach the tools from your own phone, publishing nothing
is simpler and avoids both caveats above. Tailscale issues a real certificate for
a machine on your tailnet, so the secure-context requirement is satisfied without
a public URL:

```sh
cd browser && tools/build-static.sh
tailscale serve --https=443 --set-path / http://127.0.0.1:8181
python3 -m http.server 8181 --directory dist --bind 127.0.0.1
```

The page is then at `https://<machine>.<tailnet>.ts.net/` for any device signed
into the tailnet, and unreachable from anywhere else. `cloudflared tunnel` is the
equivalent if you would rather not run Tailscale, though its URLs are public by
default.

`docs/browser.md` covers the app itself: what it measures, how the calibration is
re-derived and how it is verified against ground truth.
