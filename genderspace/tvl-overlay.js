// Fullness Space: live weight x size chart after TransVoiceLessons' fullness
// graph and UFO-FAM space. Fed by InFormant's LiveFeed (ws://127.0.0.1:8765):
// weight = formant-corrected harmonic tilt mapped 0..1, size = 1 - calibrated
// resonance (small at top, matching Zhea's chart orientation).

(function () {
	const FEED_URL = 'ws://127.0.0.1:8765';
	const TRAIL_SECONDS = 6;
	// Size-axis anchors in r-space (0 = at the masc formant anchors, 1 = at
	// the fem anchors). Tune these two if small/large feel off on your mic.
	const SIZE_LO = 0.0, SIZE_HI = 1.0;

	// Personal weight anchors: every mic chain shifts the tilt scale a few
	// dB/oct, so ?wlight=-13&wheavy=-5 pins light/heavy to YOUR chain. Do
	// your lightest and heaviest sound, read the dB/oct numbers from the
	// readout, put them in the URL. Without params, the scale is anchored to
	// the TVL video's own demonstrations.
	const q = new URLSearchParams(location.search);
	const W_LIGHT = parseFloat(q.get('wlight'));
	const W_HEAVY = parseFloat(q.get('wheavy'));
	const customW = Number.isFinite(W_LIGHT) && Number.isFinite(W_HEAVY)
			&& W_HEAVY > W_LIGHT;

	const cv = document.getElementById('chart');
	const ctx = cv.getContext('2d');
	const W = cv.width, H = cv.height;
	const M = 130; // chart margin inside canvas

	const cellCode = document.getElementById('cellCode');
	const cellDesc = document.getElementById('cellDesc');
	const nums = document.getElementById('nums');
	const conn = document.getElementById('conn');
	const ufofamBox = document.getElementById('ufofam');

	// weight w in 0..1 -> x px ; smallness s in 0..1 (1 = small) -> y px
	function X(w) { return M + w * (W - 2 * M); }
	function Y(s) { return M + (1 - s) * (H - 2 * M); }

	const FEM = [201, 79, 140], MASC = [68, 104, 201], GREY = [150, 148, 158];
	function androColor(a) { // 0 = fem, 1 = masc
		const [c1, c2, t] = a < 0.5 ? [FEM, GREY, a * 2] : [GREY, MASC, (a - 0.5) * 2];
		return 'rgb(' + c1.map((v, i) => Math.round(v + t * (c2[i] - v))).join(',') + ')';
	}

	function drawBase(ufofam) {
		ctx.clearRect(0, 0, W, H);

		if (ufofam) {
			// Soft fem->masc gradient along the balance diagonal (fem = top-left).
			const g = ctx.createLinearGradient(X(0), Y(1), X(1), Y(0));
			g.addColorStop(0, 'rgba(220,120,170,0.35)');
			g.addColorStop(0.5, 'rgba(190,160,205,0.28)');
			g.addColorStop(1, 'rgba(110,140,220,0.35)');
			ctx.fillStyle = g;
			ctx.fillRect(X(0), Y(1), W - 2 * M, H - 2 * M);

			// Balance band boundaries: fullness = weight - size01, band edges at +-0.28.
			ctx.save();
			ctx.beginPath();
			ctx.rect(X(0), Y(1), W - 2 * M, H - 2 * M);
			ctx.clip();
			ctx.strokeStyle = 'rgba(60,60,70,0.5)';
			ctx.setLineDash([14, 12]);
			ctx.lineWidth = 3;
			for (const off of [-0.28, 0.28]) {
				ctx.beginPath();
				// weight - (1 - s) = off  ->  s = 1 - w + off
				ctx.moveTo(X(0), Y(1 - 0 + off));
				ctx.lineTo(X(1), Y(1 - 1 + off));
				ctx.stroke();
			}
			// Androgenization band boundaries, perpendicular: a = (size01+w)/2.
			for (const a of [0.4, 0.6]) {
				ctx.beginPath();
				// (1 - s + w)/2 = a -> s = 1 + w - 2a
				ctx.moveTo(X(0), Y(1 + 0 - 2 * a));
				ctx.lineTo(X(1), Y(1 + 1 - 2 * a));
				ctx.stroke();
			}
			ctx.restore();
			ctx.setLineDash([]);

			// The nine UFO-FAM cells at their homes (u = fullness low side).
			ctx.font = '600 44px Georgia, serif';
			ctx.fillStyle = 'rgba(40,40,50,0.75)';
			ctx.textAlign = 'center';
			const cells = [
				['UM', 0.28, 0.08], ['UA', 0.15, 0.35], ['FM', 0.85, 0.22],
				['UF', 0.06, 0.68], ['FA', 0.5, 0.5], ['OM', 0.94, 0.32],
				['FF', 0.15, 0.78], ['OA', 0.85, 0.65], ['OF', 0.72, 0.92],
			];
			for (const [code, w, s] of cells) ctx.fillText(code, X(w), Y(s) + 15);
		}

		// Axes (cross through the middle, arrowheads, serif labels like the slide)
		ctx.strokeStyle = '#3c3c44';
		ctx.lineWidth = 5;
		const cx = X(0.5), cy = Y(0.5);
		ctx.beginPath();
		ctx.moveTo(X(0) + 10, cy); ctx.lineTo(X(1) - 10, cy);
		ctx.moveTo(cx, Y(1) + 10); ctx.lineTo(cx, Y(0) - 10);
		ctx.stroke();
		function arrow(x, y, dx, dy) {
			ctx.beginPath();
			ctx.moveTo(x, y);
			ctx.lineTo(x - 18 * dx + 10 * dy, y - 18 * dy + 10 * dx);
			ctx.moveTo(x, y);
			ctx.lineTo(x - 18 * dx - 10 * dy, y - 18 * dy - 10 * dx);
			ctx.stroke();
		}
		arrow(X(1) - 10, cy, 1, 0); arrow(X(0) + 10, cy, -1, 0);
		arrow(cx, Y(1) + 10, 0, -1); arrow(cx, Y(0) - 10, 0, 1);

		ctx.fillStyle = '#26262e';
		ctx.textAlign = 'center';
		ctx.font = '52px Georgia, serif';
		ctx.fillText('small', cx, Y(1) - 40);
		ctx.fillText('large', cx, Y(0) + 75);
		ctx.textAlign = 'right'; ctx.fillText('heavy', X(1) + 105, cy + 16);
		ctx.textAlign = 'left'; ctx.fillText('light', X(0) - 105, cy + 16);

		if (!ufofam) {
			// Quadrant titles + descriptor lists, per the video's slide.
			ctx.textAlign = 'left';
			ctx.font = '600 48px Georgia, serif';
			ctx.fillStyle = 'rgba(30,30,38,0.9)';
			ctx.fillText('fem / full', X(0.03), Y(0.93));
			ctx.fillText('overfull', X(0.76), Y(0.93));
			ctx.fillText('underfull', X(0.03), Y(0.05));
			ctx.fillText('masc / full', X(0.72), Y(0.05));
			ctx.font = '30px system-ui, sans-serif';
			ctx.fillStyle = 'rgba(90,90,100,0.85)';
			const lists = [
				[0.24, 0.86, ['feminine', 'balance', 'human', 'average', '“normal”']],
				[0.62, 0.86, ['duck-like', 'twangy', 'nerdy', 'saturated', '(nasal?)']],
				[0.24, 0.30, ['hollow', 'dopey', 'giant', 'dark', 'yawn-like']],
			];
			for (const [w, s, words] of lists) {
				words.forEach((t, i) => ctx.fillText(t, X(w), Y(s) + i * 36));
			}
		}
	}

	// ---- live state ----
	const points = []; // {w, s, a, t}
	let lastMsg = null;
	let redrawTimer = null;

	function fullnessCell(w, size01) {
		const a = (size01 + w) / 2;           // 0 fem .. 1 masc
		const fb = w - size01;                // + overfull, - underfull
		const f = fb > 0.28 ? 'O' : (fb < -0.28 ? 'U' : 'F');
		const g = a < 0.4 ? 'F' : (a > 0.6 ? 'M' : 'A');
		const fWord = { U: 'underfull', F: 'full', O: 'overfull' }[f];
		const gWord = { F: 'feminine', A: 'androgynous', M: 'masculine' }[g];
		return { code: f + g, desc: fWord + ' · ' + gWord, a: a };
	}

	function draw() {
		drawBase(ufofamBox.checked);
		const now = performance.now();

		// trail
		for (let i = 1; i < points.length; i++) {
			const p = points[i], q = points[i - 1];
			const age = (now - p.t) / 1000;
			if (age > TRAIL_SECONDS || p.gap) continue;
			ctx.strokeStyle = androColor(p.a);
			ctx.globalAlpha = 0.8 * (1 - age / TRAIL_SECONDS);
			ctx.lineWidth = 7;
			ctx.lineCap = 'round';
			ctx.beginPath();
			ctx.moveTo(X(q.w), Y(q.s));
			ctx.lineTo(X(p.w), Y(p.s));
			ctx.stroke();
		}
		ctx.globalAlpha = 1;

		// dot
		if (points.length) {
			const p = points[points.length - 1];
			const stale = (now - p.t) / 1000 > 1.2;
			ctx.globalAlpha = stale ? 0.3 : 1;
			ctx.beginPath();
			ctx.arc(X(p.w), Y(p.s), 20, 0, 7);
			ctx.fillStyle = androColor(p.a);
			ctx.fill();
			ctx.lineWidth = 6;
			ctx.strokeStyle = '#fff';
			ctx.stroke();
			ctx.globalAlpha = 1;
		}
	}

	function scheduleRedraws() {
		if (redrawTimer) return;
		redrawTimer = setInterval(() => {
			draw();
			const now = performance.now();
			while (points.length > 1 && now - points[0].t > TRAIL_SECONDS * 1000) {
				points.shift();
			}
			if (points.length <= 1 && (now - (points[0] ? points[0].t : 0)) > 8000) {
				clearInterval(redrawTimer); redrawTimer = null;
			}
		}, 50);
	}

	let lastLabelUpdate = 0;
	function onMessage(evt) {
		let m;
		try { m = JSON.parse(evt.data); } catch (e) { return; }
		lastMsg = m;

		const haveW = typeof m.weight === 'number' && m.weight >= 0;
		const haveS = typeof m.sizeR === 'number' && m.sizeR > -90;
		const haveR = typeof m.resonance === 'number' && m.resonance >= 0;
		if (!m.voiced || !haveW || !(haveS || haveR)) {
			if (points.length) points.push(Object.assign({}, points[points.length - 1],
					{ gap: true, t: performance.now() }));
			return;
		}

		// Size axis from the linear r-space value (responds fully to
		// whole-tract shifts on sustained vowels, unlike the site's
		// phoneme-normalized scale, which self-centers those). Verified
		// against the TVL video's own small/large demonstrations.
		const w = customW
			? Math.max(0, Math.min(1, (m.tilt - W_LIGHT) / (W_HEAVY - W_LIGHT)))
			: m.weight;
		const s = haveS
			? Math.max(0, Math.min(1, (m.sizeR - SIZE_LO) / (SIZE_HI - SIZE_LO)))
			: m.resonance;            // old-build fallback
		const size01 = 1 - s;
		const cell = fullnessCell(w, size01);

		points.push({ w: w, s: s, a: cell.a, t: performance.now(), gap: false });
		scheduleRedraws();

		const now = performance.now();
		if (now - lastLabelUpdate > 250) {
			lastLabelUpdate = now;
			cellCode.textContent = cell.code;
			cellCode.style.color = androColor(cell.a);
			cellDesc.textContent = cell.desc;
			nums.textContent = 'weight ' + m.tilt.toFixed(1) + ' dB/oct · '
					+ 'resonance ' + Math.round(s * 100) + '% · '
					+ 'pitch ' + Math.round(m.pitch) + ' Hz (not plotted)';
		}
	}

	let sock = null;
	function connectWebSocket() {
		sock = new WebSocket(FEED_URL);
		sock.onopen = () => { conn.textContent = '● live'; conn.className = 'chip live'; };
		sock.onmessage = onMessage;
		sock.onclose = () => {
			conn.textContent = '○ InFormant not running';
			conn.className = 'chip';
			setTimeout(connectWebSocket, 2500);
		};
		sock.onerror = () => { try { sock.close(); } catch (e) {} };
	}

	ufofamBox.addEventListener('change', draw);
	if (location.hash === '#ufofam') ufofamBox.checked = true;
	drawBase(ufofamBox.checked);

	// The chip doubles as the start control when the engine runs in-page:
	// microphone access needs a user gesture, so something has to be clicked.
	function setLive(on) {
		conn.textContent = on ? '● live' : '○ stopped';
		conn.className = on ? 'chip live' : 'chip';
	}

	function setNeedsStart() {
		conn.textContent = '▶ tap to start'; conn.className = 'chip';
		conn.style.cursor = 'pointer';
		conn.title = 'Click to start listening';
		conn.addEventListener('click', async () => {
			if (!localFeed || localFeed.started) return;
			conn.textContent = '… starting';
			try {
				await localFeed.start();
			} catch (err) {
				conn.textContent = '○ microphone blocked';
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
