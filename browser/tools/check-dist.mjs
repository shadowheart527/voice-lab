// Check a built site before it goes anywhere: every asset the page asks for
// exists, and nothing is addressed in a way that only works at a domain root.
//
// This catches the class of bug that is invisible locally and total in
// production. Served from browser/ on a plain port, a root-relative "/vendor/x"
// and a relative "vendor/x" resolve to the same URL; under a path prefix, which
// is how a GitHub project page is served, only one of them still does.
//
//   node browser/tools/check-dist.mjs browser/dist
import { readFileSync, existsSync, readdirSync, statSync } from 'node:fs';
import { join, relative, dirname } from 'node:path';

const root = process.argv[2] ?? 'browser/dist';
const errors = [];
const notes = [];

// Fetched lazily by the neural probe, which is optional by design, so a missing
// one is a note rather than a failure.
const OPTIONAL = /^(models|vendor)\//;

function walk(dir) {
    return readdirSync(dir, { withFileTypes: true }).flatMap((e) => {
        const p = join(dir, e.name);
        return e.isDirectory() ? walk(p) : [p];
    });
}

/**
 * Resolve a relative reference and report on it.
 *
 * `base` is the file the browser resolves against, which is not always the file
 * the reference is written in: imports and `new URL(..., import.meta.url)` are
 * module-relative, while a bare string handed to `new Worker` or `addModule`
 * resolves against the document instead.
 */
function check(ref, fromFile, kind, base = fromFile) {
    if (/^([a-z]+:|\/\/|#)/i.test(ref)) return;              // absolute URL, protocol-relative, anchor
    const where = relative(root, fromFile);
    if (ref.startsWith('/')) {
        errors.push(`${where}: ${kind} "${ref}" is root-relative, so it breaks under a path prefix`);
        return;
    }
    const target = relative(root, join(dirname(base), ref.split(/[?#]/)[0]));
    if (target.startsWith('..')) {
        errors.push(`${where}: ${kind} "${ref}" escapes the site root`);
        return;
    }
    if (existsSync(join(root, target))) return;
    if (OPTIONAL.test(target)) notes.push(`${where}: optional ${kind} "${target}" not deployed`);
    else errors.push(`${where}: ${kind} "${ref}" -> ${target} is missing`);
}

if (!existsSync(join(root, 'index.html'))) {
    console.error(`${root}: no index.html; run browser/tools/build-static.sh first`);
    process.exit(1);
}

// The page itself.
const indexPath = join(root, 'index.html');
const html = readFileSync(indexPath, 'utf8');
for (const m of html.matchAll(/\b(?:src|href)="([^"]+)"/g)) check(m[1], indexPath, 'reference');

if (html.includes('<!--SOURCE-OFFER-->')) {
    errors.push('index.html: the AGPL source offer was never injected into the footer');
}
if (!/source\/[^"]+\.tar\.gz/.test(html)) {
    errors.push('index.html: the footer links to no source archive');
}

// Module graph, workers, worklets and anything addressed through import.meta.url.
for (const file of walk(root).filter((f) => f.endsWith('.js') || f.endsWith('.mjs'))) {
    if (relative(root, file).startsWith('vendor/')) continue;   // third-party bundle, self-contained
    const js = readFileSync(file, 'utf8');
    for (const m of js.matchAll(/^\s*(?:import|export)\b[^'"\n]*?from\s*['"]([^'"]+)['"]/gm)) check(m[1], file, 'import');
    for (const m of js.matchAll(/\bimport\(\s*['"]([^'"]+)['"]/g)) check(m[1], file, 'dynamic import');
    for (const m of js.matchAll(/new URL\(\s*['"]([^'"]+)['"]\s*,\s*import\.meta\.url/g)) check(m[1], file, 'asset URL');
    for (const m of js.matchAll(/new Worker\(\s*['"]([^'"]+)['"]/g)) check(m[1], file, 'worker', indexPath);
    for (const m of js.matchAll(/addModule\(\s*['"]([^'"]+)['"]/g)) check(m[1], file, 'worklet', indexPath);
}

// A half-copied onnxruntime is worse than none: the probe would start loading
// and then fail on a 404 instead of staying quietly hidden.
const vendor = join(root, 'vendor');
if (existsSync(vendor)) {
    for (const f of ['ort.wasm.min.mjs', 'ort-wasm-simd-threaded.mjs', 'ort-wasm-simd-threaded.wasm']) {
        if (!existsSync(join(vendor, f))) errors.push(`vendor/: incomplete onnxruntime, ${f} is missing`);
    }
} else {
    notes.push('vendor/: onnxruntime-web not deployed, so the neural probe stays hidden');
}
if (!existsSync(join(root, 'LICENSE.txt'))) errors.push('LICENSE.txt: missing from the built site');

const total = walk(root).length;
const bytes = walk(root).reduce((n, f) => n + statSync(f).size, 0);
console.log(`${root}: ${total} files, ${(bytes / 1e6).toFixed(1)} MB`);
for (const n of notes) console.log(`  note: ${n}`);
for (const e of errors) console.error(`  error: ${e}`);
process.exit(errors.length ? 1 : 0);
