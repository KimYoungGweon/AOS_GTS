// 캔버스 그래프 · heatmap · 컬러맵 (외부 라이브러리 없음)
const C = {
  grid: '#242A33', axis: '#39424E', label: '#8B95A1', now: '#C7CED8',
  font: "12px 'IBM Plex Mono', ui-monospace, monospace",
  small: "11px 'IBM Plex Mono', ui-monospace, monospace",
};

function fit(canvas, cssH) {
  const dpr = window.devicePixelRatio || 1;
  const w = canvas.clientWidth || canvas.parentElement.clientWidth || 600;
  const h = cssH || canvas.clientHeight || 200;
  if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(h * dpr)) {
    canvas.width = Math.round(w * dpr); canvas.height = Math.round(h * dpr);
  }
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  return { ctx, w, h };
}

function niceStep(range, n) {
  const raw = range / Math.max(1, n);
  const mag = 10 ** Math.floor(Math.log10(raw));
  const f = raw / mag;
  return (f < 1.5 ? 1 : f < 3.5 ? 2 : f < 7.5 ? 5 : 10) * mag;
}

/**
 * 시간축 선 그래프.
 *  opts.points  [[t_epoch, v], …]
 *  opts.span    표시 구간(초)  — 오른쪽 끝 = now
 *  opts.yMin/yMax  고정 범위 (auto=true 면 무시)
 *  opts.target  목표선 값 (선택)
 *  opts.markers [t_epoch, …] 세로 점선 (선택)
 */
export function lineChart(canvas, opts) {
  const { ctx, w, h } = fit(canvas, opts.height);
  const color = opts.color || '#E39A3B';
  const L = 52, R = 14, T = 12, B = 26;
  const pw = w - L - R, ph = h - T - B;
  ctx.clearRect(0, 0, w, h);

  const now = opts.now || Date.now() / 1000;
  const t0 = now - opts.span;
  const keep = (arr) => (arr || []).filter(p => p[0] >= t0 - 1 && p[1] !== null && p[1] !== undefined);
  // 여러 장비: opts.series = [{points, color, label}] (2026-09-23.02) — 없으면 한 줄
  const series = (opts.series || [{ points: opts.points, color }]).map(x => ({ ...x, pts: keep(x.points) }));
  const pts = series.flatMap(x => x.pts);

  let lo = opts.yMin ?? 0, hi = opts.yMax ?? 1;
  if (opts.auto !== false && pts.length) {
    lo = Math.min(...pts.map(p => p[1])); hi = Math.max(...pts.map(p => p[1]));
    if (opts.target !== undefined && opts.target !== null) { lo = Math.min(lo, opts.target); hi = Math.max(hi, opts.target); }
    const pad = (hi - lo) * 0.12 || Math.abs(hi) * 0.1 || 0.1;
    lo -= pad; hi += pad;
  }
  if (hi <= lo) hi = lo + 1;
  const step = niceStep(hi - lo, 4);
  lo = Math.floor(lo / step) * step; hi = Math.ceil(hi / step) * step;
  const X = t => L + ((t - t0) / opts.span) * pw;
  const Y = v => T + ph - ((v - lo) / (hi - lo)) * ph;

  // 격자 · y 라벨
  ctx.font = C.font; ctx.textAlign = 'right'; ctx.textBaseline = 'middle';
  const dec = Math.max(0, -Math.floor(Math.log10(step)));
  for (let v = lo; v <= hi + step / 2; v += step) {
    const y = Math.round(Y(v)) + 0.5;
    ctx.strokeStyle = Math.abs(v - lo) < step / 2 ? C.axis : C.grid;
    ctx.beginPath(); ctx.moveTo(L, y); ctx.lineTo(L + pw, y); ctx.stroke();
    ctx.fillStyle = C.label; ctx.fillText(v.toFixed(Math.min(dec, 3)), L - 8, y);
  }
  // x 라벨 (-span … now)
  ctx.textBaseline = 'alphabetic';
  const xs = opts.span <= 120 ? 20 : opts.span <= 300 ? 60 : opts.span <= 900 ? 120 : 600;
  for (let s = opts.span; s >= 0; s -= xs) {
    const x = X(now - s);
    ctx.textAlign = s === opts.span ? 'left' : s === 0 ? 'right' : 'center';
    ctx.fillStyle = s === 0 ? C.now : C.label;
    const lab = s === 0 ? 'now' : s >= 60 && s % 60 === 0 && opts.span > 300 ? `-${s / 60}m` : `-${s}s`;
    ctx.fillText(lab, x, h - 7);
  }
  // 목표선
  if (opts.target !== undefined && opts.target !== null) {
    const y = Y(opts.target);
    ctx.setLineDash([5, 5]); ctx.strokeStyle = 'rgba(76,175,134,.8)';
    ctx.beginPath(); ctx.moveTo(L, y); ctx.lineTo(L + pw, y); ctx.stroke(); ctx.setLineDash([]);
  }
  for (const m of opts.markers || []) {
    if (m < t0) continue;
    ctx.setLineDash([3, 4]); ctx.strokeStyle = 'rgba(111,168,220,.6)';
    ctx.beginPath(); ctx.moveTo(X(m), T); ctx.lineTo(X(m), T + ph); ctx.stroke(); ctx.setLineDash([]);
  }
  if (!pts.length) {
    ctx.fillStyle = C.label; ctx.textAlign = 'center'; ctx.font = "13px 'IBM Plex Sans KR', sans-serif";
    ctx.fillText(opts.emptyText || '수신 데이터 없음', L + pw / 2, T + ph / 2);
    return;
  }
  // 면 + 선 (여러 장비면 면 채움 없이 선만)
  const multi = series.length > 1;
  for (const sr of series) {
    const sp = sr.pts, col = sr.color || color;
    if (!sp.length) continue;
    ctx.save(); ctx.beginPath(); ctx.rect(L, T, pw, ph); ctx.clip();
    ctx.beginPath();
    sp.forEach((p, i) => i ? ctx.lineTo(X(p[0]), Y(p[1])) : ctx.moveTo(X(p[0]), Y(p[1])));
    if (opts.fill !== false && !multi) {
      ctx.lineTo(X(sp[sp.length - 1][0]), T + ph); ctx.lineTo(X(sp[0][0]), T + ph); ctx.closePath();
      ctx.fillStyle = hexA(col, 0.10); ctx.fill();
      ctx.beginPath();
      sp.forEach((p, i) => i ? ctx.lineTo(X(p[0]), Y(p[1])) : ctx.moveTo(X(p[0]), Y(p[1])));
    }
    ctx.strokeStyle = col; ctx.lineWidth = 2; ctx.lineJoin = 'round'; ctx.stroke(); ctx.lineWidth = 1;
    ctx.restore();
    const last = sp[sp.length - 1];
    ctx.beginPath(); ctx.arc(X(last[0]), Y(last[1]), 4.5, 0, Math.PI * 2);
    ctx.fillStyle = col; ctx.fill(); ctx.strokeStyle = '#181C21'; ctx.lineWidth = 2; ctx.stroke(); ctx.lineWidth = 1;
  }
  // 범례 (여러 장비)
  if (multi) {
    ctx.font = C.font; ctx.textBaseline = 'middle'; ctx.textAlign = 'left';
    let x = L + 8;
    for (const sr of series) {
      const lab = sr.label || '';
      ctx.fillStyle = sr.color || color; ctx.fillRect(x, T + 6, 14, 3);
      ctx.fillStyle = C.label; ctx.fillText(lab, x + 18, T + 8);
      x += 26 + ctx.measureText(lab).width;
    }
  }
}

// 장비 구별 색 (여러 장비 동시 표시 — 2026-09-23.02)
export const DEV_COLORS = ['#E39A3B', '#6FA8DC', '#4CAF86', '#D96C8A', '#B08CE0', '#E0C95A', '#5CC8C8', '#E07A5F'];

function hexA(hex, a) {
  const n = parseInt(hex.slice(1), 16);
  return `rgba(${n >> 16},${(n >> 8) & 255},${n & 255},${a})`;
}

// ── 컬러맵 ─────────────────────────────────────────────────────────
const clamp01 = x => x < 0 ? 0 : x > 1 ? 1 : x;
function jet(t) {                          // 장비 뷰어(PCSW)와 같은 Jet
  t = clamp01(t);
  const r = clamp01(1.5 - Math.abs(4 * t - 3));
  const g = clamp01(1.5 - Math.abs(4 * t - 2));
  const b = clamp01(1.5 - Math.abs(4 * t - 1));
  return [r * 255, g * 255, b * 255];
}
function turbo(t) {                        // Google Turbo 다항 근사
  t = clamp01(t);
  const r = 0.13572138 + t * (4.61539260 + t * (-42.66032258 + t * (132.13108234 + t * (-152.94239396 + t * 59.28637943))));
  const g = 0.09140261 + t * (2.19418839 + t * (4.84296658 + t * (-14.18503333 + t * (4.27729857 + t * 2.82956604))));
  const b = 0.10667330 + t * (12.64194608 + t * (-60.58204836 + t * (110.36276771 + t * (-89.90310912 + t * 27.34824973))));
  return [clamp01(r) * 255, clamp01(g) * 255, clamp01(b) * 255];
}
const VIR = [[68, 1, 84], [72, 40, 120], [62, 74, 137], [49, 104, 142], [38, 130, 142],
  [31, 158, 137], [53, 183, 121], [109, 205, 89], [180, 222, 44], [253, 231, 37]];
function viridis(t) {
  t = clamp01(t) * (VIR.length - 1);
  const i = Math.min(VIR.length - 2, Math.floor(t)), f = t - i;
  return VIR[i].map((v, k) => v + (VIR[i + 1][k] - v) * f);
}
// PCSW Twin Viewer 3D 컬러스케일 (frm3DView.cs TWIN_COLORSCALE) — 0 이 파랑, 음수 보라, 양수 녹→노→빨
const TWIN = [[0, [180, 100, 240]], [0.125, [240, 80, 200]], [0.25, [110, 0, 210]], [0.375, [40, 0, 170]],
  [0.5, [0, 0, 255]], [0.625, [0, 255, 255]], [0.75, [0, 255, 0]], [0.875, [255, 255, 0]], [1, [255, 0, 0]]];
function twin(t) {
  t = clamp01(t);
  for (let i = 1; i < TWIN.length; i++) {
    if (t <= TWIN[i][0]) {
      const [t0, a] = TWIN[i - 1], [t1, b] = TWIN[i];
      const f = (t - t0) / (t1 - t0);
      return a.map((v, k) => v + (b[k] - v) * f);
    }
  }
  return TWIN[TWIN.length - 1][1];
}
export const CMAPS = { jet, turbo, viridis, twin };

// Plotly 용 colorscale 배열
export function plotlyScale(cmap = 'jet', n = 16) {
  const f = CMAPS[cmap] || jet;
  return Array.from({ length: n + 1 }, (_, i) => {
    const [r, g, b] = f(i / n);
    return [i / n, `rgb(${r | 0},${g | 0},${b | 0})`];
  });
}

/**
 * 보간 (Interpolation) — 값 격자를 f 배로 이중선형 보간.
 * null(보정된 마지막 LFV 열 등)은 같은 행의 가장 가까운 값으로 채운 뒤 보간한다 (표시 전용).
 */
export function upsample(values, f = 6) {
  if (!values || !values.length) return values;
  const R = values.length, C = values[0].length;
  const g = values.map(row => {
    const r = row.slice();
    for (let c = 0; c < C; c++) {
      if (r[c] === null || r[c] === undefined) {
        let k = 1, v = null;
        while (v === null && (c - k >= 0 || c + k < C)) {
          if (c - k >= 0 && row[c - k] !== null && row[c - k] !== undefined) v = row[c - k];
          else if (c + k < C && row[c + k] !== null && row[c + k] !== undefined) v = row[c + k];
          k++;
        }
        r[c] = v ?? 0;
      }
    }
    return r;
  });
  const R2 = (R - 1) * f + 1, C2 = (C - 1) * f + 1;
  const out = new Array(R2);
  for (let i = 0; i < R2; i++) {
    const y = i / f, y0 = Math.min(R - 2, Math.floor(y)), fy = y - y0;
    const row = out[i] = new Array(C2);
    for (let j = 0; j < C2; j++) {
      const x = j / f, x0 = Math.min(C - 2, Math.floor(x)), fx = x - x0;
      const a = g[y0][x0], b = g[y0][x0 + 1], c = g[y0 + 1][x0], d = g[y0 + 1][x0 + 1];
      row[j] = (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
    }
  }
  return out;
}
export function linspace(a, b, n) {
  return Array.from({ length: n }, (_, i) => a + (b - a) * i / (n - 1));
}

/**
 * heatmap 1장 — values: rows=CV(no_cv), cols=LFV(no_lfv).  CV 가 위로 커지도록 y 뒤집음.
 * null(NaN) 칸은 어두운 회색.
 */
export function drawHeatmap(canvas, values, vmin, vmax, cmap = 'jet', interp = false) {
  if (!values) { const c = canvas.getContext('2d'); c.clearRect(0, 0, canvas.width, canvas.height); return; }
  // Interpolation On: 격자를 보간해 부드럽게, Off: 측정 격자 그대로(한 칸 = 한 점, 픽셀 확대)
  canvas.style.imageRendering = interp ? 'auto' : 'pixelated';
  if (interp && values.length > 1 && values[0].length > 1) values = upsample(values, 8);
  const rows = values.length, cols = values[0].length;
  if (canvas.width !== cols || canvas.height !== rows) { canvas.width = cols; canvas.height = rows; }
  const ctx = canvas.getContext('2d');
  const img = ctx.createImageData(cols, rows);
  const f = CMAPS[cmap] || jet;
  const span = (vmax - vmin) || 1;
  for (let r = 0; r < rows; r++) {
    const src = values[rows - 1 - r];
    for (let c = 0; c < cols; c++) {
      const v = src[c];
      const k = (r * cols + c) * 4;
      if (v === null || v === undefined || Number.isNaN(v)) {
        img.data[k] = 30; img.data[k + 1] = 34; img.data[k + 2] = 40; img.data[k + 3] = 255;
      } else {
        const [R, G, B] = f((v - vmin) / span);
        img.data[k] = R; img.data[k + 1] = G; img.data[k + 2] = B; img.data[k + 3] = 255;
      }
    }
  }
  ctx.putImageData(img, 0, 0);
}

export function drawColorbar(canvas, cmap = 'jet') {
  canvas.width = 1; canvas.height = 256;
  const ctx = canvas.getContext('2d');
  const img = ctx.createImageData(1, 256);
  const f = CMAPS[cmap] || jet;
  for (let i = 0; i < 256; i++) {
    const [r, g, b] = f(1 - i / 255);
    img.data.set([r, g, b, 255], i * 4);
  }
  ctx.putImageData(img, 0, 0);
}

export function fitColorbar(canvas, ref, minus = 44) {
  const h = Math.max(120, (ref?.clientHeight || 240) - minus);
  canvas.style.height = `${h}px`;
}

export function colorOf(v, vmin, vmax, cmap = 'jet') {
  if (v === null || v === undefined) return '#39424E';
  const [r, g, b] = (CMAPS[cmap] || jet)((v - vmin) / ((vmax - vmin) || 1));
  return `rgb(${r | 0},${g | 0},${b | 0})`;
}

// 캔버스가 크기 바뀔 때 다시 그리기
export function onResize(el, fn) {
  const ro = new ResizeObserver(() => fn());
  ro.observe(el);
  return () => ro.disconnect();
}
