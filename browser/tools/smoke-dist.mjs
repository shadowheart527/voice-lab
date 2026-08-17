// Drive a built site the way a phone would, before it goes anywhere: served
// under a path prefix, with a known WAV injected as the microphone, and check
// that real numbers come out of the whole chain.
//
// The prefix is the part worth testing. Served from a domain root, a
// document-relative and a root-relative asset path resolve identically, so a
// site can be quietly wrong in a way that only appears once it is deployed under
// a prefix like a GitHub project page. That failure mode is invisible to
// inspection and total in production: it took exactly this harness to catch
// onnxruntime fetching its .wasm from the wrong origin path.
//
//   browser/tools/make-test-vowel.py /tmp/vowel.wav
//   node browser/tools/smoke-dist.mjs browser/dist /tmp/vowel.wav
//
// Needs playwright (npm i playwright) and a chromium it can find: either
// playwright's own, or one named in CHROMIUM_PATH.
import { createServer } from 'node:http';
import { existsSync, readFileSync, statSync } from 'node:fs';
import { extname, join, normalize, resolve } from 'node:path';
import { glob } from 'node:fs/promises';

const dist = resolve(process.argv[2] ?? 'browser/dist');
const wav = process.argv[3] ? resolve(process.argv[3]) : null;
const PREFIX = '/voice-lab/';

if (!existsSync(join(dist, 'index.html'))) {
    console.error(`${dist}: no index.html; run browser/tools/build-static.sh first`);
    process.exit(1);
}
if (!wav || !existsSync(wav)) {
    console.error('usage: node browser/tools/smoke-dist.mjs <dist> <vowel.wav>');
    console.error('       make one with browser/tools/make-test-vowel.py');
    process.exit(1);
}

let chromium;
try {
    ({ chromium } = await import('playwright'));
} catch {
    console.error('playwright is not installed: npm i playwright');
    process.exit(1);
}

const TYPES = {
    '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript',
    '.wasm': 'application/wasm', '.json': 'application/json',
    '.onnx': 'application/octet-stream', '.gz': 'application/gzip',
};

const server = createServer((req, res) => {
    const url = new URL(req.url, 'http://127.0.0.1');
    if (!url.pathname.startsWith(PREFIX)) {
        res.writeHead(404).end('outside the mount point');
        return;
    }
    let rel = normalize(url.pathname.slice(PREFIX.length));
    if (rel.startsWith('..')) {
        res.writeHead(403).end();
        return;
    }
    let file = join(dist, rel);
    if (!existsSync(file) || statSync(file).isDirectory()) file = join(file, 'index.html');
    if (!existsSync(file)) {
        res.writeHead(404).end('not found');
        return;
    }
    res.writeHead(200, { 'content-type': TYPES[extname(file)] ?? 'text/plain' });
    res.end(req.method === 'HEAD' ? undefined : readFileSync(file));
});
// Port 0: the kernel picks a free one, so a stray server from an earlier run
// cannot make this look like a failure of the site.
await new Promise((ok) => server.listen(0, '127.0.0.1', ok));
const base = `http://127.0.0.1:${server.address().port}${PREFIX}`;

async function findChromium() {
    if (process.env.CHROMIUM_PATH) return process.env.CHROMIUM_PATH;
    for await (const p of glob('/opt/pw-browsers/chromium-*/chrome-linux/chrome')) return p;
    return undefined;   // let playwright resolve its own
}

const browser = await chromium.launch({
    executablePath: await findChromium(),
    args: [
        '--use-fake-ui-for-media-stream',
        '--use-fake-device-for-media-stream',
        `--use-file-for-fake-audio-capture=${wav}%noloop`,
        '--autoplay-policy=no-user-gesture-required',
    ],
});
const context = await browser.newContext({ permissions: ['microphone'] });
const page = await context.newPage();

const problems = [];
const seen = new Map();
page.on('console', (m) => m.type() === 'error' && problems.push(`console: ${m.text()}`));
page.on('pageerror', (e) => problems.push(`page error: ${e.message}`));
page.on('requestfailed', (r) => problems.push(`request failed: ${r.url()} ${r.failure()?.errorText}`));
page.on('response', (r) => {
    seen.set(r.url().replace(base, ''), r.status());
    if (r.status() >= 400) problems.push(`http ${r.status()}: ${r.url()}`);
});

await page.goto(base, { waitUntil: 'load' });
const read = async (sel) => (await page.locator(sel).textContent()).trim();

await page.click('#mic');
await page.waitForFunction(
    () => !['—', ''].includes(document.querySelector('#vPitch').textContent.trim()),
    { timeout: 20000 },
).catch(() => problems.push('no pitch reading appeared within 20 s'));
await page.waitForTimeout(4000);   // let the 2 s gender-read window fill

console.log(`${base}  (${await page.title()})`);
console.log(`  status      ${await read('#status')}`);
console.log(`  pitch       ${await read('#vPitch')} ${await read('#vNote')}`);
console.log(`  resonance   ${await read('#vRes')}`);
console.log(`  reads       ${await read('#vReads')}`);

await page.click('#tabFullness');
await page.waitForTimeout(1500);
console.log(`  fullness    ${await read('#fCode')} · ${await read('#fDesc')}`);
console.log(`  weight/size ${await read('#fWeight')} / ${await read('#fSize')}, tilt ${await read('#fTilt')}`);

// A blank canvas with correct numbers beside it is still a broken page.
const colours = await page.evaluate(() => {
    const c = document.querySelector('#chart');
    const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
    const set = new Set();
    for (let i = 0; i < d.length; i += 4000) set.add(`${d[i]},${d[i + 1]},${d[i + 2]}`);
    return set.size;
});
if (colours < 4) problems.push(`the chart canvas looks blank (${colours} distinct colours)`);

for (const required of ['src/app.js', 'src/worker/analyzer.worker.js', 'src/dsp/core.js']) {
    if (seen.get(required) !== 200) problems.push(`${required} was never served with a 200`);
}
console.log(`  requests    ${seen.size} served, chart painted in ${colours} colours`);

await browser.close();
server.close();

if (problems.length) {
    console.error(`\n${problems.length} problem(s):`);
    for (const p of problems) console.error(`  ${p}`);
    process.exit(1);
}
console.log('\nno console errors, no failed requests, readings present');
