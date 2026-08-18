// Live-dot overlay: renders the current voice, streamed from a locally running
// InFormant (ws://127.0.0.1:8765, see its LiveFeed class), as a moving dot with
// a fading trail in the genderspace plot. Local addition; not part of upstream.
//
// Axis semantics differ slightly from uploaded clips: upstream's resonance is a
// phoneme-normalized formant z-score (it knows which phoneme you said, via
// forced alignment); the live value is InFormant's anchor-normalized F1/F2/F3
// blend, which is phoneme-blind. Comparable scale and direction, but expect the
// live dot to wander a little with vowel choice where uploads would not.

(function () {
	const FEED_URL = 'ws://127.0.0.1:8765';
	const TRAIL_SECONDS = 6;
	const DOT_SIZE_EM = 1.1;

	const graphEl = document.querySelector('voice-graph-2d');
	if (!graphEl) return;
	const overlay = graphEl.querySelector('.overlay');
	const bgCanvas = graphEl.querySelector('canvas');
	if (!overlay || !bgCanvas) return;

	// --- Trail canvas, painted above the gradient, below clip markers.
	const trail = document.createElement('canvas');
	trail.style.position = 'absolute';
	trail.style.left = '0';
	trail.style.top = '0';
	trail.style.pointerEvents = 'none';
	trail.style.zIndex = 'calc(var(--voice-graph-z-index) + 1)';
	bgCanvas.insertAdjacentElement('afterend', trail);

	function resizeTrail() {
		trail.width = overlay.clientWidth || bgCanvas.clientWidth;
		trail.height = overlay.clientHeight || bgCanvas.clientHeight;
		trail.style.width = trail.width + 'px';
		trail.style.height = trail.height + 'px';
	}
	resizeTrail();
	window.addEventListener('resize', resizeTrail);

	// --- The live dot, sitting with the clip markers inside the overlay.
	const dot = document.createElement('div');
	dot.style.position = 'absolute';
	dot.style.height = DOT_SIZE_EM + 'em';
	dot.style.width = DOT_SIZE_EM + 'em';
	dot.style.borderRadius = '50%';
	dot.style.border = '0.15em solid white';
	dot.style.boxShadow = '0 0 6px rgba(0,0,0,0.6)';
	dot.style.boxSizing = 'border-box';
	dot.style.zIndex = 'calc(var(--voice-graph-z-index) + 3)';
	dot.style.pointerEvents = 'none';
	dot.style.opacity = '0';
	dot.style.transition = 'opacity 0.4s';
	dot.title = 'Your voice, live from InFormant';
	overlay.appendChild(dot);

	// --- Status chip.
	const chip = document.createElement('div');
	chip.style.position = 'absolute';
	chip.style.left = '0.5em';
	chip.style.bottom = '0.5em';
	chip.style.font = '12px sans-serif';
	chip.style.padding = '0.15em 0.5em';
	chip.style.borderRadius = '1em';
	chip.style.background = 'rgba(0,0,0,0.45)';
	chip.style.color = '#ccc';
	chip.style.zIndex = 'calc(var(--voice-graph-z-index) + 3)';
	chip.style.pointerEvents = 'none';
	overlay.appendChild(chip);

	function setChip(state) {
		if (state === 'live') {
			chip.textContent = '● live';
			chip.style.color = '#7dda9c';
		} else {
			chip.textContent = '○ InFormant not running';
			chip.style.color = '#999';
		}
	}
	setChip('off');

	// Same blue-grey-pink axis InFormant uses for its meter and tracks.
	function scoreColor(p) {
		const stops = [[96, 165, 250], [158, 155, 166], [244, 114, 182]];
		if (p < 0 || p > 1 || Number.isNaN(p)) return 'rgb(158,155,166)';
		const [a, b, t] = p < 0.5
			? [stops[0], stops[1], p * 2]
			: [stops[1], stops[2], (p - 0.5) * 2];
		return 'rgb(' + a.map((v, i) => Math.round(v + t * (b[i] - v))).join(',') + ')';
	}

	// pitchPercent from util.js, restated here so the overlay has no load-order
	// dependency: y position of a pitch on the 50-300 Hz axis.
	function pp(hz) {
		return Math.max(0, Math.min(1, (hz - 50) / 250));
	}

	const points = []; // {x, y (fractions), score, t (ms), gap}

	function drawTrail() {
		const ctx = trail.getContext('2d');
		ctx.clearRect(0, 0, trail.width, trail.height);
		const now = performance.now();
		for (let i = 1; i < points.length; i++) {
			const a = points[i - 1], b = points[i];
			if (b.gap) continue;
			const age = (now - b.t) / 1000;
			if (age > TRAIL_SECONDS) continue;
			ctx.strokeStyle = scoreColor(b.score);
			ctx.globalAlpha = 0.85 * (1 - age / TRAIL_SECONDS);
			ctx.lineWidth = 3;
			ctx.lineCap = 'round';
			ctx.beginPath();
			ctx.moveTo(a.x * trail.width, (1 - a.y) * trail.height);
			ctx.lineTo(b.x * trail.width, (1 - b.y) * trail.height);
			ctx.stroke();
		}
		ctx.globalAlpha = 1;
	}

	let redrawTimer = null;
	function scheduleRedraws() {
		// Keep fading the trail for a while after messages stop.
		if (redrawTimer) return;
		redrawTimer = setInterval(() => {
			drawTrail();
			const now = performance.now();
			while (points.length && now - points[0].t > TRAIL_SECONDS * 1000) {
				points.shift();
			}
			if (!points.length) {
				clearInterval(redrawTimer);
				redrawTimer = null;
			}
		}, 50);
	}

	let lastVoiced = false;

	function onMessage(evt) {
		let m;
		try { m = JSON.parse(evt.data); } catch (e) { return; }

		// Prefer the calibrated site-scale resonance (matches how the reference
		// clips were scored); resScore is the steeper masc-fem meter logistic,
		// kept as a fallback for an older InFormant build.
		var res = (typeof m.resonance === 'number' && m.resonance >= 0)
				? m.resonance : m.resScore;

		if (!m.voiced || m.pitch <= 0 || res < 0) {
			// Keep the dot where it was, dimmed; break the trail.
			dot.style.opacity = '0.25';
			if (lastVoiced && points.length) {
				points.push(Object.assign({}, points[points.length - 1],
						{ gap: true, t: performance.now() }));
			}
			lastVoiced = false;
			return;
		}

		const x = res;                 // resonance axis, 0..1, site scale
		const y = pp(m.pitch);         // pitch axis, 50-300 Hz
		const w = overlay.clientWidth, h = overlay.clientHeight;
		const px = Math.round(w * x), py = Math.round(h * (1 - y));

		dot.style.transform = 'translate(' + (px - dot.offsetWidth / 2) + 'px, '
				+ (py - dot.offsetHeight / 2) + 'px)';
		dot.style.background = scoreColor(m.score);
		dot.style.opacity = '1';

		points.push({ x: x, y: y, score: m.score, t: performance.now(), gap: false });
		lastVoiced = true;
		scheduleRedraws();
	}

	let sock = null;
	function connectWebSocket() {
		sock = new WebSocket(FEED_URL);
		sock.onopen = () => setChip('live');
		sock.onmessage = onMessage;
		sock.onclose = () => {
			setChip('off');
			dot.style.opacity = '0';
			setTimeout(connectWebSocket, 2500);
		};
		sock.onerror = () => { try { sock.close(); } catch (e) {} };
	}

	// The chip doubles as the start control when the engine runs in-page:
	// microphone access needs a user gesture, so something has to be clicked.
	function setLive(on) {
		setChip(on ? 'live' : 'off');
	}

	function setNeedsStart() {
		chip.textContent = '▶ tap to start'; chip.style.color = '#8a5cd0';
		chip.style.cursor = 'pointer';
		chip.title = 'Click to start listening';
		chip.addEventListener('click', async () => {
			if (!localFeed || localFeed.started) return;
			chip.textContent = '… starting';
			try {
				await localFeed.start();
			} catch (err) {
				chip.textContent = '○ microphone blocked'; chip.style.color = '#999';
			}
		});
	}

	// Module scripts are deferred, so this classic script runs first: give the
	// local engine a moment to register before deciding it is absent.
	function waitForEngine(ms) {
		if (window.VoiceLabLocalEngine) return Promise.resolve(window.VoiceLabLocalEngine);
		return new Promise((resolve) => {
			const done = () => { clearTimeout(timer); resolve(window.VoiceLabLocalEngine || null); };
			const timer = setTimeout(() => {
				window.removeEventListener('voicelab-engine-ready', done);
				resolve(window.VoiceLabLocalEngine || null);
			}, ms);
			window.addEventListener('voicelab-engine-ready', done, { once: true });
		});
	}

	// Prefer running the engine in this page (works on a phone, needs no
	// desktop app); fall back to the desktop's WebSocket feed if it is not
	// built, so the existing desk setup is unaffected.
	let localFeed = null;

	async function connect() {
		const local = await waitForEngine(2000);
		if (local && await local.available) {
			localFeed = local.create();
			localFeed.onopen = () => setLive(true);
			localFeed.onmessage = onMessage;
			localFeed.onclose = () => setLive(false);
			setNeedsStart();
			return;
		}
		connectWebSocket();
	}

	connect();
})();
