// 3. 가스농도 Heatmap Viewer — Full/1hour 16장(섹션) · Sample 8장
//    D13b: Idf 는 사용자가 고른 Air(Ref) 로만 계산한다 (자동 선택 없음)
import { get, qs } from '../api.js';
import { esc, fmt, fmtDateTime, id2, toast, errMsg, MODE_NAME, STATUS_NAME, fmtShort } from '../ui.js';
import { drawHeatmap, drawColorbar, colorOf, fitColorbar, plotlyScale, upsample, linspace } from '../charts.js';

const VALUE_NAME = { idf: 'Idf (Target − Air)', raw: '측정값 (이 run)', air: 'Air(Ref) 값' };

// 다른 화면에 갔다 와도 마지막 보던 데이터를 다시 연다 (2026-09-23.02)
const LAST_Q = 'gts_viewer_q';
function loadLastQ() { try { return localStorage.getItem(LAST_Q) || ''; } catch { return ''; } }
function saveLastQ(q) { try { if (q) localStorage.setItem(LAST_Q, q); } catch { /* */ } }

export async function mount(view, r) {
  if (!r.query.run) {
    const q = loadLastQ();
    if (q && q.startsWith('?')) {
      history.replaceState(null, '', `#/viewer${q}`);
      r = { ...r, query: Object.fromEntries(new URLSearchParams(q.slice(1))) };
    }
  }
  const st = {
    run: +r.query.run || null, air: +r.query.air || null,
    sy: r.query.sy !== undefined ? +r.query.sy : null, sx: r.query.sx !== undefined ? +r.query.sx : null,
    value: r.query.value || (r.query.air ? 'idf' : 'raw'),
    scale: 'section', min: null, max: null, cmap: r.query.cmap || 'jet',
    gas: r.query.gas || '', aos: r.query.aos || '', origin: r.query.origin || '',
    view: r.query.view === 'stack' ? 'stack' : 'tiles',       // 개별(16장) | Stacked 3D
    interp: r.query.interp !== '0', surf3d: false,          // Interpolation 기본 On (2026-09-23.02)
  };
  let runInfo = null, ov = null, hm = null, zoom = null, tilesFoot = '';

  view.innerHTML = `
  <div class="subbar">
    <label class="label" for="gasSel">가스 종류</label><select id="gasSel"><option value="">전체</option></select>
    <label class="label" for="aosSel">AOS ID</label>
    <select id="aosSel"><option value="">전체</option>${Array.from({ length: 20 }, (_, i) => `<option value="${i + 1}">AOS-${id2(i + 1)}</option>`).join('')}</select>
    <label class="label" for="dataSel">측정 데이터</label><select id="dataSel" style="min-width:300px;max-width:420px"></select>
    <label class="label" for="refSel">Air(Ref) Data</label><select id="refSel" style="min-width:240px;max-width:380px"></select>
    <button class="btn primary" id="loadBtn" type="button">불러오기</button>
    <span class="small mute ml-auto">Full / 1hour — 16 heatmap · Sample — 8 heatmap</span>
  </div>
  <div class="page">
    <div class="vw">
      <aside class="panel vw-side">
        <div class="sec"><div class="label">보기 방식 (View Mode)</div>
          <div class="radios" id="viewR">
            <label data-v="tiles"><input type="radio" name="vm" value="tiles"> 개별 (16장)</label>
            <label data-v="stack"><input type="radio" name="vm" value="stack"> Stacked 3D</label>
          </div>
          <label class="row small" style="gap:6px;cursor:pointer"><input type="checkbox" id="interpChk"> Interpolation (보간)</label>
          <label class="row small" id="surfRow" style="gap:6px;cursor:pointer"><input type="checkbox" id="surfChk"> 3D Surface (값을 높이로)</label>
          <div class="tiny mute" id="viewHint"></div>
        </div>
        <div class="sec"><div class="label">표시 값</div>
          <div class="radios" id="valR">
            <label data-v="idf"><input type="radio" name="val" value="idf"> Idf</label>
            <label data-v="raw"><input type="radio" name="val" value="raw"> 측정값</label>
            <label data-v="air"><input type="radio" name="val" value="air"> Air(Ref)</label>
          </div>
          <div class="tiny mute" id="valHint"></div>
        </div>
        <div class="sec"><div class="label">스케일</div>
          <div class="radios" id="scR">
            <label data-v="section"><input type="radio" name="sc" value="section"> 16 HM 공통</label>
            <label data-v="tile"><input type="radio" name="sc" value="tile"> 타일별</label>
            <label data-v="manual"><input type="radio" name="sc" value="manual"> Manual</label>
          </div>
          <div class="row" style="gap:6px"><label class="small mute" for="minV">Min</label><input id="minV" type="number" step="0.001" style="width:84px">
            <label class="small mute" for="maxV">Max</label><input id="maxV" type="number" step="0.001" style="width:84px"></div>
          <div class="tiny mute mono" id="rangeInfo"></div>
        </div>
        <div class="sec"><div class="row"><span class="label">Colormap</span><span class="tiny mute ml-auto">장비 2D=Jet · 3D=Twin</span></div>
          <select id="cmapSel"><option value="jet">Jet (장비 호환)</option><option value="turbo">Turbo</option><option value="viridis">Viridis (색각 보정)</option><option value="twin">Twin 3D (장비 Stacked)</option></select>
        </div>
        <div class="sec" id="gridSec">
          <div class="row"><span class="label">Selection Grid</span><span class="tiny mute mono ml-auto" id="secLbl"></span></div>
          <div class="row" style="gap:6px">
            <select id="hvSel" aria-label="Section HV" style="flex:1"></select>
            <select id="frqSel" aria-label="Section 주파수" style="flex:1"></select>
          </div>
          <div class="row tiny mute" style="justify-content:space-between"><span>← HV →</span><span id="gridKind"></span></div>
          <div class="selgrid" id="selGrid"></div>
          <div class="tiny mute">행: 주파수(Frq, 위→아래 증가) · 열: HV · 색: 섹션 대표값(|값| 최대)</div>
        </div>
      </aside>

      <section class="panel" style="display:flex;flex-direction:column;gap:12px;min-width:0">
        <div class="row" style="align-items:flex-start;flex-wrap:wrap">
          <div style="min-width:0"><h2 id="title">Heatmap Viewer</h2><div class="small mute" id="subtitle" style="margin-top:4px"></div></div>
          <div class="row ml-auto" style="gap:18px;align-items:flex-start">
            <div><div class="tiny mute">측정 일시</div><div class="mono small" id="mAt">—</div></div>
            <div><div class="tiny mute">Ref 연결</div><div class="mono small" id="mRef">—</div></div>
            <button class="btn sm" id="csvBtn" type="button">CSV 내보내기</button>
            <button class="btn sm" id="pngBtn" type="button">PNG 내보내기</button>
          </div>
        </div>
        <div id="warn"></div>
        <div id="stage" style="min-height:420px"></div>
        <div class="row tiny mute" id="foot" style="flex-wrap:wrap"></div>
      </section>
    </div>
  </div>`;

  const $ = (s) => view.querySelector(s);
  const radio = (box, v) => view.querySelectorAll(`${box} label`).forEach(l => {
    const on = l.dataset.v === v; l.classList.toggle('on', on); l.querySelector('input').checked = on;
  });

  // ── 선택 목록 ──
  let runsCache = [];
  async function loadGases() {
    try {
      const g = await get('/api/gases');
      $('#gasSel').innerHTML = `<option value="">전체</option>` + g.map(x => `<option value="${esc(x.target_gas)}">${esc(x.target_gas)} (${x.n})</option>`).join('');
      $('#gasSel').value = st.gas;
    } catch { /* */ }
  }
  async function loadRuns() {
    try {
      const r = await get(`/api/runs${qs({ kind: 'gas', gas: $('#gasSel').value, device_id: $('#aosSel').value, limit: 300 })}`);
      runsCache = r.rows;
      // Air run 도 따로 볼 수 있게 목록 끝에 붙인다
      const ra = await get(`/api/runs${qs({ kind: 'air_ref', device_id: $('#aosSel').value, limit: 300 })}`);
      runsCache = runsCache.concat(ra.rows);
      const opt = (x) => `<option value="${x.run_id}">#${x.run_id} ${esc(x.source_file || x.label || x.target_gas || '')} · ${MODE_NAME[x.mode]} · AOS-${id2(x.device_id)}${x.data_origin === 'legacy_import' ? ' · 임포트' : ''}</option>`;
      $('#dataSel').innerHTML = `<option value="">— 측정 데이터 선택 —</option>`
        + `<optgroup label="가스 (Target)">${r.rows.map(opt).join('')}</optgroup>`
        + `<optgroup label="기준 Air (Ref)">${ra.rows.map(opt).join('')}</optgroup>`;
      if (st.run) {
        if (!runsCache.some(x => x.run_id === st.run)) {
          try { const one = await get(`/api/runs/${st.run}`); runsCache.push(one); $('#dataSel').insertAdjacentHTML('beforeend', opt(one)); } catch { /* */ }
        }
        $('#dataSel').value = st.run;
      }
    } catch (e) { toast(errMsg(e), 'err'); }
  }
  async function loadAirs() {
    const rsel = runsCache.find(x => x.run_id === +$('#dataSel').value);
    if (!rsel) { $('#refSel').innerHTML = `<option value="">— 먼저 측정 데이터 선택 —</option>`; return; }
    const legacy = rsel.data_origin === 'legacy_import';
    try {
      // 임포트(legacy) 는 HV 범위가 달라도 같은 모드면 위치로 비교한다 → 격자 대신 모드로 거른다
      let airs = await get(`/api/air-list${qs(legacy ? { mode: rsel.mode, include_legacy: 'true' } : { grid_id: rsel.grid_id })}`);
      airs = airs.filter(a => a.run_id !== rsel.run_id && (legacy ? true : a.data_origin !== 'legacy_import'));
      // 같은 장치 → 최근 순
      airs.sort((a, b) => (b.device_id === rsel.device_id) - (a.device_id === rsel.device_id) || (b.started_at > a.started_at ? 1 : -1));
      $('#refSel').innerHTML = `<option value="">— Air(Ref) 선택 (Idf 계산용) —</option>` + airs.map(a =>
        `<option value="${a.run_id}">#${a.run_id} ${esc(a.label || '')} · AOS-${id2(a.device_id)} · ${fmtShort(a.started_at)}${a.run_id === rsel.air_ref_run_id ? ' · 기록된 짝' : ''}</option>`).join('');
      if (st.air && airs.some(a => a.run_id === st.air)) $('#refSel').value = st.air;
    } catch (e) { $('#refSel').innerHTML = `<option value="">(목록 없음)</option>`; }
  }

  $('#gasSel').addEventListener('change', () => { loadRuns().then(loadAirs); });
  $('#aosSel').addEventListener('change', () => { loadRuns().then(loadAirs); });
  $('#dataSel').addEventListener('change', loadAirs);
  $('#loadBtn').addEventListener('click', () => {
    const run = +$('#dataSel').value; if (!run) { toast('측정 데이터를 선택하세요', 'warn'); return; }
    const air = +$('#refSel').value || null;
    const rr = runsCache.find(x => x.run_id === run);
    const value = rr?.kind === 'air_ref' ? 'raw' : (air ? 'idf' : 'raw');
    setHash({ run, air, value, sy: null, sx: null });
  });

  function hashQ() {
    return qs({ run: st.run, air: st.air, sy: st.sy, sx: st.sx, value: st.value, cmap: st.cmap !== 'jet' ? st.cmap : '',
      view: st.view === 'stack' ? 'stack' : '', interp: st.interp ? '' : '0' });
  }
  function rep() { const q = hashQ(); history.replaceState(null, '', `#/viewer${q}`); if (st.run) saveLastQ(q); }
  function setHash(p) {
    Object.assign(st, p);
    const h = `#/viewer${hashQ()}`;
    if (location.hash !== h) location.hash = h; else show();
  }

  // ── 옵션 ──
  view.querySelectorAll('#valR input').forEach(i => i.addEventListener('change', () => {
    if ((i.value === 'idf' || i.value === 'air') && !st.air) {
      toast('Idf / Air 값을 보려면 위에서 Air(Ref) Data 를 먼저 고르고 불러오세요', 'warn'); radio('#valR', st.value); return;
    }
    setHash({ value: i.value });
  }));
  view.querySelectorAll('#scR input').forEach(i => i.addEventListener('change', () => { st.scale = i.value; radio('#scR', st.scale); paint(); }));
  ['#minV', '#maxV'].forEach(q => $(q).addEventListener('change', () => { st.scale = 'manual'; radio('#scR', 'manual'); paint(); }));
  $('#cmapSel').addEventListener('change', (e) => { st.cmap = e.target.value; autoTwin = false; paintGrid(); paint(); rep(); });
  let autoTwin = false;         // Stacked 로 바꿀 때 Jet 이면 장비와 같은 Twin 컬러맵으로 자동 전환
  view.querySelectorAll('#viewR input').forEach(i => i.addEventListener('change', () => {
    if (i.value === 'stack' && runInfo?.grid?.mode === 'fast') {
      toast('Sample 모드는 Duty × LFF 격자가 아니라서 Stacked 보기를 쓸 수 없습니다', 'warn'); radio('#viewR', st.view); return;
    }
    st.view = i.value; zoom = null;
    if (st.view === 'stack' && st.cmap === 'jet') { st.cmap = 'twin'; autoTwin = true; }
    else if (st.view === 'tiles' && autoTwin && st.cmap === 'twin') { st.cmap = 'jet'; autoTwin = false; }
    $('#cmapSel').value = st.cmap;
    syncViewUi(); paintGrid(); paint();
    rep();
  }));
  $('#interpChk').addEventListener('change', (e) => { st.interp = e.target.checked; paint(); rep(); });
  $('#surfChk').addEventListener('change', (e) => { st.surf3d = e.target.checked; paint(); });
  function syncViewUi() {
    radio('#viewR', st.view);
    $('#interpChk').checked = st.interp; $('#surfChk').checked = st.surf3d;
    $('#surfRow').classList.toggle('hidden', st.view !== 'stack');
    $('#viewHint').textContent = st.view === 'stack'
      ? '위: DutyStack (열=LFF, 층=Duty) · 아래: LFFStack (열=Duty, 층=LFF) — 드래그 회전 · 휠 확대'
      : '행 LFF × 열 Duty 16장 — 타일을 누르면 확대';
  }
  $('#hvSel').addEventListener('change', () => setHash({ sx: +$('#hvSel').value }));
  $('#frqSel').addEventListener('change', () => setHash({ sy: +$('#frqSel').value }));
  $('#csvBtn').addEventListener('click', () => {
    if (!st.run) return;
    const q = qs({ sy: st.sy, sx: st.sx, value: st.value, air_run_id: st.air, token: localStorage.getItem('gts_token') });
    location.href = `/api/runs/${st.run}/export.csv${q}`;
  });
  $('#pngBtn').addEventListener('click', exportPng);

  // ── 표시 ──
  async function show() {
    radio('#valR', st.value); radio('#scR', st.scale); $('#cmapSel').value = st.cmap; syncViewUi();
    $('#valHint').textContent = VALUE_NAME[st.value] || '';
    zoom = null;
    if (!st.run) {
      $('#title').textContent = 'Heatmap Viewer';
      $('#subtitle').textContent = '';
      $('#stage').innerHTML = `<div class="empty-state">위에서 <b>측정 데이터</b>를 고르고 <b>불러오기</b>를 누르세요.<br>
        Idf(Target − Air)를 보려면 <b>Air(Ref) Data</b> 도 함께 골라야 합니다 — 시스템이 자동으로 고르지 않습니다.</div>`;
      $('#gridSec').classList.add('hidden');
      return;
    }
    saveLastQ(hashQ());
    $('#stage').innerHTML = `<div class="empty-state">불러오는 중…</div>`;
    $('#warn').innerHTML = '';
    try {
      runInfo = await get(`/api/runs/${st.run}`);
    } catch (e) { $('#stage').innerHTML = `<div class="empty-state">${esc(errMsg(e))}</div>`; return; }
    if (runInfo.kind === 'air_ref' && st.value !== 'raw') st.value = 'raw';
    const g = runInfo.grid;
    const fast = g.mode === 'fast';
    $('#gridSec').classList.toggle('hidden', fast);
    if (fast && st.view === 'stack') { st.view = 'tiles'; syncViewUi(); }
    view.querySelector('#viewR label[data-v="stack"]').classList.toggle('disabled', fast);

    // 섹션 개요 (Selection Grid)
    try {
      ov = await get(`/api/runs/${st.run}/overview${qs({ air_run_id: st.value !== 'raw' ? st.air : null })}`);
    } catch (e) {
      $('#warn').innerHTML = `<div class="lockbar lost">${esc(errMsg(e))}</div>`;
      if (st.value !== 'raw') { st.value = 'raw'; radio('#valR', 'raw'); }
      ov = await get(`/api/runs/${st.run}/overview`).catch(() => null);
    }
    if (!fast && (st.sy === null || st.sx === null)) {       // 기본 섹션: 데이터가 있는 첫 섹션 중 대표값 최대
      const secs = (ov?.sections || []).filter(s => s.count);
      const best = secs.reduce((a, s) => (a === null || Math.abs(s.best ?? 0) > Math.abs(a.best ?? 0)) ? s : a, null);
      st.sy = best ? best.sy : 0; st.sx = best ? best.sx : 0;
      rep();
    }
    if (!fast) {
      $('#hvSel').innerHTML = g.hv_list.map((v, i) => `<option value="${i}">[${i}] ${fmt(v, 1)} V</option>`).join('');
      $('#frqSel').innerHTML = g.frq_list.map((v, i) => `<option value="${i}">[${i}] ${fmt(v, 1)} kHz</option>`).join('');
      $('#hvSel').value = st.sx; $('#frqSel').value = st.sy;
      $('#secLbl').textContent = `Section [${st.sy}, ${st.sx}]`;
      $('#gridKind').textContent = ov ? (ov.kind === 'idf' ? 'best Idf' : '최대값') : '';
    }
    paintGrid();

    try {
      hm = await get(`/api/runs/${st.run}/heatmaps${qs({ sy: fast ? null : st.sy, sx: fast ? null : st.sx, value: st.value, air_run_id: st.value !== 'raw' ? st.air : null })}`);
    } catch (e) { $('#stage').innerHTML = `<div class="empty-state">${esc(errMsg(e))}</div>`; return; }

    const air = st.air ? runsCache.find(x => x.run_id === st.air) : null;
    const secTxt = fast ? 'Sample 8조합' : `Section [${st.sy},${st.sx}] · Frq ${fmt(g.frq_list[st.sy], 1)} kHz · HV ${fmt(g.hv_list[st.sx], 1)} V · Duty × LFF ${g.no_hx} × ${g.no_hy}`;
    $('#title').textContent = `Multi View — ${secTxt}`;
    $('#subtitle').innerHTML = `${esc(runInfo.source_file || runInfo.label || '')} · ${esc(runInfo.target_gas || '')} · AOS-${id2(runInfo.device_id)} · ${runInfo.received_heatmaps}/${runInfo.expected_heatmaps} heatmap · ${STATUS_NAME[runInfo.status]}`
      + (runInfo.data_origin === 'legacy_import' ? ` · <span class="chip gray">임포트(개발 전 샘플)</span>` : '');
    $('#mAt').textContent = fmtDateTime(runInfo.started_at);
    $('#mRef').textContent = st.air ? `#${st.air} ${air?.label || ''} ✓` : '선택 안 함';
    tilesFoot = fast ? `<span>각 타일: x축 LF_Volt 0→3 V · y축 CV −1→1 V · 값 ${st.value === 'idf' ? 'Idf' : ''} (V)</span><span class="ml-auto">타일을 누르면 확대</span>`
      : `<span>행: LF 주파수 (LFF) · 열: Duty · 각 타일 x축 LF_Volt 0→3 V, y축 CV −1→1 V · 값 V</span><span class="ml-auto">타일을 누르면 확대 · 마지막 LFV 열(3.0 V)이 회색이면 임포트 보정값</span>`;
    paint();
  }

  function range() {
    if (st.scale === 'manual') {
      const mn = parseFloat($('#minV').value), mx = parseFloat($('#maxV').value);
      if (!Number.isNaN(mn) && !Number.isNaN(mx) && mx > mn) return [mn, mx];
    }
    let mn = hm.min, mx = hm.max;
    if (st.value === 'idf' && mn !== null) { const a = Math.max(Math.abs(mn), Math.abs(mx)); mn = -a; mx = a; }   // Idf 는 0 대칭
    return [mn ?? 0, mx ?? 1];
  }

  function paint() {
    if (!hm) return;
    if (st.view === 'stack' && runInfo.grid.mode !== 'fast') return paintStack();
    purgePlot();
    $('#foot').innerHTML = tilesFoot;
    if (zoom !== null) return paintZoom();
    const g = runInfo.grid, fast = g.mode === 'fast';
    const [gmn, gmx] = range();
    if (st.scale !== 'manual') { $('#minV').value = gmn.toFixed(3); $('#maxV').value = gmx.toFixed(3); }
    $('#rangeInfo').textContent = `16 HM : ${fmt(hm.min, 3)} ~ ${fmt(hm.max, 3)}` + (ov?.val_min !== undefined && ov?.val_min !== null ? `  ·  run(best) : ${fmt(ov.val_min, 3)} ~ ${fmt(ov.val_max, 3)}` : '');
    const cols = fast ? 4 : g.no_hx;
    const byPos = {};
    hm.heatmaps.forEach(h => { byPos[fast ? h.param_no - 1 : h.hy * g.no_hx + h.hx] = h; });
    const n = fast ? g.heatmap_count : g.no_hx * g.no_hy;
    const heads = fast ? '' : `<div style="display:grid;grid-template-columns:70px repeat(${cols},minmax(0,1fr)) 50px;gap:12px" class="tiny mute">
        <span></span>${g.duty_list.map(d => `<span style="text-align:center">Duty ${fmt(d, 0)}%</span>`).join('')}<span></span></div>`;
    let tiles = '';
    for (let i = 0; i < n; i++) {
      const h = byPos[i];
      if (!fast && i % cols === 0) tiles += `<div class="tiny mute" style="align-self:center;text-align:right">LFF<br><b class="mono" style="color:var(--tx-2);font-size:12px">${fmt(g.lff_list[Math.floor(i / cols)], 0)} Hz</b></div>`;
      if (!h || !h.values) {
        tiles += `<div class="hmtile empty"><div class="hd"><b>${h ? '짝 없음' : '미측정'}</b></div><canvas></canvas><div class="ax"><span>0</span><span>3.0 V</span></div></div>`;
      } else {
        const lab = fast ? `No.${h.param_no} · HV ${fmt(h.hv, 0)} · ${fmt(h.frq, 0)}k` : `D ${fmt(h.duty, 0)}% · LFF ${fmt(h.lff, 0)}`;
        const sub = fast ? `D${fmt(h.duty, 0)} · LFF ${fmt(h.lff, 0)}` : '';
        tiles += `<div class="hmtile" data-i="${i}" role="button" tabindex="0" aria-label="${lab} 확대">
          <div class="hd"><b>${lab}</b><span>max ${fmt(h.max, 2)}</span></div>
          <canvas data-i="${i}"></canvas>
          <div class="ax"><span>LFV 0${sub ? ' · ' + sub : ''}</span><span>3.0 V</span></div></div>`;
      }
      if (!fast && i % cols === cols - 1) tiles += `<span></span>`;
    }
    $('#stage').innerHTML = `${heads}
      <div class="row" style="align-items:stretch;gap:12px">
        <div class="tiles16 grow" style="grid-template-columns:${fast ? '' : '70px '}repeat(${cols},minmax(0,1fr))${fast ? '' : ' 0'}">${tiles}</div>
        <div class="cbar" style="width:50px"><span id="cbMax">${fmt(gmx, 2)}</span><canvas id="cb"></canvas><span id="cbMin">${fmt(gmn, 2)}</span></div>
      </div>`;
    drawColorbar($('#cb'), st.cmap);
    fitColorbar($('#cb'), $('#stage .tiles16'));
    view.querySelectorAll('#stage canvas[data-i]').forEach(cv => {
      const h = byPos[cv.dataset.i];
      const [mn, mx] = st.scale === 'tile' ? tileRange(h) : [gmn, gmx];
      drawHeatmap(cv, h.values, mn, mx, st.cmap, st.interp);
    });
    view.querySelectorAll('#stage .hmtile[data-i]').forEach(el => {
      const open = () => { zoom = +el.dataset.i; paintZoom(); };
      el.addEventListener('click', open);
      el.addEventListener('keydown', (e) => { if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); open(); } });
    });
  }

  // ── Stacked 3D (PCSW frm3DView ShowTwinMultiStackedHeatMap 과 같은 배치) ──
  //    위 줄 DutyStack: 열마다 LFF 고정, 층 = Duty 값 (z = Duty)
  //    아래 줄 LFFStack: 열마다 Duty 고정, 층 = LFF 값 (z = LFF)
  //    x = LF_Volt, y = CV, 색 = 값.  3D Surface 켜면 z = 층 + 값 × zscale
  let plotEl = null;
  function purgePlot() {
    if (plotEl && window.Plotly) { try { window.Plotly.purge(plotEl); } catch { /* */ } }
    plotEl = null;
  }
  let plotlyP = null;
  function loadPlotly() {
    if (window.Plotly) return Promise.resolve(window.Plotly);
    if (!plotlyP) {
      plotlyP = new Promise((ok, bad) => {
        const sc = document.createElement('script');
        sc.src = 'vendor/plotly-gl3d.min.js';
        sc.onload = () => ok(window.Plotly);
        sc.onerror = () => { plotlyP = null; bad(new Error('3D 라이브러리(vendor/plotly-gl3d.min.js)를 불러오지 못했습니다')); };
        document.head.appendChild(sc);
      });
    }
    return plotlyP;
  }
  // Interpolation Off: 칸마다 한 색(계단) — 점 사이 중간에서 색이 바뀌도록 꼭짓점을 두 번씩 둔다
  function blocky(values, xs, ys) {
    const mids = (a) => {
      const out = [a[0]];
      for (let i = 0; i < a.length - 1; i++) { const m = (a[i] + a[i + 1]) / 2; out.push(m, m); }
      out.push(a[a.length - 1]); return out;
    };
    const dup = (row) => { const o = []; row.forEach(v => o.push(v, v)); return o; };
    const x = mids(xs), y = mids(ys);
    const z = []; values.forEach(r => { const d = dup(r); z.push(d, d); });
    return { x, y, v: z };
  }

  async function paintStack() {
    const g = runInfo.grid;
    const [gmn, gmx] = range();
    if (st.scale !== 'manual') { $('#minV').value = gmn.toFixed(3); $('#maxV').value = gmx.toFixed(3); }
    $('#rangeInfo').textContent = `16 HM : ${fmt(hm.min, 3)} ~ ${fmt(hm.max, 3)}` + (st.scale === 'tile' ? ' · Stacked 는 공통 스케일' : '');
    purgePlot();
    $('#foot').innerHTML = `<span>x축 LF_Volt · y축 CV · 층(z) 위 줄 Duty(%) / 아래 줄 LFF(Hz) · 색 ${st.value === 'idf' ? 'Idf' : '값'} (V) · 공통 스케일</span>`
      + `<span class="ml-auto">드래그: 회전 · 휠: 확대 · 더블클릭: 처음 시점 · Interpolation ${st.interp ? 'On (보간)' : 'Off (측정 격자 그대로)'}</span>`;
    $('#stage').innerHTML = `<div class="empty-state">3D 준비 중…</div>`;
    let Plotly;
    try { Plotly = await loadPlotly(); } catch (e) { $('#stage').innerHTML = `<div class="empty-state">${esc(e.message)}</div>`; return; }
    if (st.view !== 'stack') return;              // 기다리는 사이 바뀜

    const byHyHx = {};
    hm.heatmaps.forEach(h => { if (h.values) byHyHx[`${h.hy},${h.hx}`] = h; });
    const xs0 = hm.lfv_list, ys0 = hm.cv_list;
    const shape = (vals) => {
      if (st.interp) {
        const f = 4, u = upsample(vals, f);
        return { x: linspace(xs0[0], xs0[xs0.length - 1], u[0].length), y: linspace(ys0[0], ys0[ys0.length - 1], u.length), v: u };
      }
      return blocky(vals, xs0, ys0);
    };
    const absMax = Math.max(Math.abs(gmn), Math.abs(gmx)) || 1;
    const minGap = (a) => { let m = Infinity; for (let i = 1; i < a.length; i++) m = Math.min(m, Math.abs(a[i] - a[i - 1])); return Number.isFinite(m) ? m : 1; };
    const scale = plotlyScale(st.cmap, 24);
    const valName = st.value === 'idf' ? 'Idf' : st.value === 'air' ? 'Air' : '값';
    const cols = Math.max(g.no_hx, g.no_hy);
    const traces = [], layout = {
      paper_bgcolor: '#0F1215', plot_bgcolor: '#0F1215', font: { color: '#A8B1BC', family: 'IBM Plex Mono, monospace', size: 10 },
      margin: { l: 0, r: 0, t: 26, b: 0 }, showlegend: false, annotations: [],
    };
    const axis = (title, extra = {}) => ({ title: { text: title, font: { size: 10 } }, color: '#A8B1BC', gridcolor: '#2A313A',
      zerolinecolor: '#3A424D', backgroundcolor: '#0F1215', showbackground: true, tickfont: { size: 9 }, ...extra });
    let first = true, sceneNo = 0;
    const addScene = (row, col, title, layers, layerName, zName) => {
      sceneNo++;
      const key = sceneNo === 1 ? 'scene' : `scene${sceneNo}`;
      const x0 = col / cols, x1 = (col + 1) / cols;
      const y0 = row === 0 ? 0.5 : 0, y1 = row === 0 ? 0.97 : 0.47;
      const zv = layers.map(l => l.z);
      const gap = minGap(zv), zscale = gap * 0.4 / absMax;
      layers.forEach(l => {
        const sh = shape(l.h.values);
        const z = st.surf3d ? sh.v.map(r => r.map(v => (v === null ? null : l.z + v * zscale))) : sh.v.map(r => r.map(v => (v === null ? null : l.z)));
        traces.push({
          type: 'surface', scene: key, x: sh.x, y: sh.y, z, surfacecolor: sh.v,
          colorscale: scale, cmin: gmn, cmax: gmx, opacity: 0.85, showscale: first,
          colorbar: first ? { title: { text: valName, side: 'right' }, thickness: 12, len: 0.9, x: 1.0, tickfont: { size: 10 }, outlinewidth: 0 } : undefined,
          text: sh.v.map(r => r.map(v => (v === null ? '없음' : v.toFixed(5)))),
          hovertemplate: `LFV %{x:.2f} V<br>CV %{y:.2f} V<br>${valName} %{text}<extra>${layerName} ${fmt(l.z, 0)}</extra>`,
          contours: { x: { highlight: false }, y: { highlight: false }, z: { highlight: false } },
        });
        first = false;
      });
      layout[key] = {
        domain: { x: [x0, x1], y: [y0, y1] }, aspectmode: 'manual', aspectratio: { x: 1, y: 1, z: 1.1 },
        camera: { eye: { x: -1.6, y: -1.6, z: 0.8 } },
        xaxis: axis('LF_Volt'), yaxis: axis('CV'), zaxis: axis(zName, { tickvals: zv, ticktext: zv.map(v => fmt(v, 0)) }),
      };
      layout.annotations.push({ text: title, x: (x0 + x1) / 2, y: y1 + 0.015, xref: 'paper', yref: 'paper', showarrow: false,
        font: { size: 12, color: '#E8EAED' }, xanchor: 'center', yanchor: 'bottom' });
    };
    // 위: DutyStack (LFF 고정)
    g.lff_list.forEach((lff, hy) => {
      const layers = g.duty_list.map((d, hx) => ({ z: d, h: byHyHx[`${hy},${hx}`] })).filter(l => l.h);
      addScene(0, hy, `DutyStack · LFF ${fmt(lff, 0)} Hz`, layers, 'Duty', 'Duty %');
    });
    // 아래: LFFStack (Duty 고정)
    g.duty_list.forEach((d, hx) => {
      const layers = g.lff_list.map((lff, hy) => ({ z: lff, h: byHyHx[`${hy},${hx}`] })).filter(l => l.h);
      addScene(1, hx, `LFFStack · Duty ${fmt(d, 0)} %`, layers, 'LFF', 'LFF Hz');
    });

    $('#stage').innerHTML = `<div id="stack3d" class="stack3d"></div>`;
    plotEl = $('#stack3d');
    await Plotly.newPlot(plotEl, traces, layout, { displaylogo: false, responsive: true,
      modeBarButtonsToRemove: ['toImage', 'resetCameraLastSave3d'], scrollZoom: true });
  }

  function tileRange(h) {
    if (st.value === 'idf') { const a = Math.max(Math.abs(h.min), Math.abs(h.max)); return [-a, a]; }
    return [h.min, h.max];
  }

  function paintZoom() {
    const g = runInfo.grid, fast = g.mode === 'fast';
    const h = hm.heatmaps.find(x => (fast ? x.param_no - 1 : x.hy * g.no_hx + x.hx) === zoom);
    if (!h) { zoom = null; return paint(); }
    const [mn, mx] = st.scale === 'tile' ? tileRange(h) : range();
    $('#stage').innerHTML = `
      <div class="row" style="margin-bottom:10px;flex-wrap:wrap">
        <button class="btn sm" id="back" type="button">← 16개 보기</button>
        <span class="small"><b>HV ${fmt(h.hv, 1)} V · Frq ${fmt(h.frq, 1)} kHz · Duty ${fmt(h.duty, 1)} % · LFF ${fmt(h.lff, 0)} Hz</b></span>
        <span class="small mute">cond ${h.cond_idx}${h.best ? ` · best ${fmt(h.best.value, 4)} @ CV ${fmt(hm.cv_list[h.best.idx_cv], 2)} / LFV ${fmt(hm.lfv_list[h.best.idx_lfv], 2)}` : ''}</span>
      </div>
      <div class="zoom">
        <div><canvas class="big" id="big"></canvas>
          <div class="row tiny mute mono" style="justify-content:space-between;margin-top:4px"><span>LF_Volt ${fmt(hm.lfv_list[0], 1)} V</span><span>→</span><span>${fmt(hm.lfv_list[hm.lfv_list.length - 1], 1)} V</span></div>
          <div class="hover" id="hov">마우스를 올리면 값이 보입니다 (y축 아래 CV ${fmt(hm.cv_list[0], 1)} → 위 ${fmt(hm.cv_list[hm.cv_list.length - 1], 1)} V)</div></div>
        <div class="cbar"><span>${fmt(mx, 3)}</span><canvas id="cb"></canvas><span>${fmt(mn, 3)}</span></div>
      </div>`;
    drawColorbar($('#cb'), st.cmap);
    const big = $('#big');
    fitColorbar($('#cb'), big, 0);
    drawHeatmap(big, h.values, mn, mx, st.cmap, st.interp);
    big.addEventListener('mousemove', (e) => {
      const rc = big.getBoundingClientRect();
      const ix = Math.min(h.values[0].length - 1, Math.floor((e.clientX - rc.left) / rc.width * h.values[0].length));
      const iyFromTop = Math.min(h.values.length - 1, Math.floor((e.clientY - rc.top) / rc.height * h.values.length));
      const iy = h.values.length - 1 - iyFromTop;
      const v = h.values[iy][ix];
      $('#hov').textContent = `CV ${fmt(hm.cv_list[iy], 2)} V · LFV ${fmt(hm.lfv_list[ix], 2)} V → ${v === null ? '없음(보정)' : fmt(v, 5) + ' V'}`;
    });
    $('#back').addEventListener('click', () => { zoom = null; paint(); });
  }

  function paintGrid() {
    const g = runInfo?.grid;
    if (!g || g.mode === 'fast' || !ov?.sections) { $('#selGrid').innerHTML = ''; return; }
    const vals = ov.sections.filter(s => s.count && s.best !== null).map(s => s.best);
    const mn = Math.min(...vals), mx = Math.max(...vals);    // 섹션 간 대비가 보이도록 실제 범위
    $('#selGrid').style.gridTemplateColumns = `repeat(${g.no_sx}, minmax(0,1fr))`;
    $('#selGrid').innerHTML = ov.sections.map(s => {
      const sel = s.sy === st.sy && s.sx === st.sx;
      const bg = s.count ? colorOf(s.best, mn, mx, st.cmap) : '#262C34';
      return `<button type="button" class="${sel ? 'sel' : ''}" data-sy="${s.sy}" data-sx="${s.sx}" ${s.count ? '' : 'disabled'}
        style="background:${bg}" title="[${s.sy},${s.sx}] Frq ${fmt(s.frq, 1)} · HV ${fmt(s.hv, 1)} · ${s.count}장 · ${fmt(s.best, 3)}"
        aria-label="섹션 ${s.sy},${s.sx}"></button>`;
    }).join('');
    view.querySelectorAll('#selGrid button').forEach(b => b.addEventListener('click', () => setHash({ sy: +b.dataset.sy, sx: +b.dataset.sx })));
  }

  function exportPng() {
    if (plotEl && window.Plotly) {
      window.Plotly.toImage(plotEl, { format: 'png', width: 1920, height: 1000 }).then(url => {
        const a = document.createElement('a');
        a.download = `run${st.run}_${st.value}_s${st.sy}-${st.sx}_stacked.png`; a.href = url; a.click();
      });
      return;
    }
    const cvs = [...view.querySelectorAll('#stage canvas[data-i], #big')];
    if (!cvs.length) return;
    const W = 1600, pad = 20;
    const fast = runInfo.grid.mode === 'fast';
    const cols = zoom !== null ? 1 : (fast ? 4 : runInfo.grid.no_hx);
    const rows = Math.ceil(cvs.length / cols);
    const tw = (W - pad * (cols + 1)) / cols, th = tw * 11 / 16;
    const out = document.createElement('canvas');
    out.width = W; out.height = rows * (th + pad + 18) + pad + 40;
    const c = out.getContext('2d');
    c.fillStyle = '#0F1215'; c.fillRect(0, 0, out.width, out.height);
    c.fillStyle = '#E8EAED'; c.font = '16px sans-serif';
    c.fillText(`${$('#title').textContent}  —  ${$('#subtitle').textContent}`, pad, 28);
    c.imageSmoothingEnabled = st.interp;
    cvs.forEach((cv, i) => {
      const x = pad + (i % cols) * (tw + pad), y = 44 + Math.floor(i / cols) * (th + pad + 18);
      c.drawImage(cv, x, y + 18, tw, th);
      c.fillStyle = '#A8B1BC'; c.font = '12px monospace';
      c.fillText(cv.closest('.hmtile')?.querySelector('.hd b')?.textContent || '', x, y + 12);
    });
    const a = document.createElement('a');
    a.download = `run${st.run}_${st.value}${st.sy !== null ? `_s${st.sy}-${st.sx}` : ''}.png`;
    a.href = out.toDataURL('image/png'); a.click();
  }

  await loadGases();
  await loadRuns();
  await loadAirs();
  await show();

  return {
    cleanup() { purgePlot(); },
    update(r2) {           // 해시만 바뀐 경우 — 다시 그리기만
      const q = r2.query;
      const next = { run: +q.run || null, air: +q.air || null, sy: q.sy !== undefined ? +q.sy : null, sx: q.sx !== undefined ? +q.sx : null,
        value: q.value || (q.air ? 'idf' : 'raw'), cmap: q.cmap || st.cmap,
        view: q.view === 'stack' ? 'stack' : 'tiles', interp: q.interp !== '0' };
      const runChanged = next.run !== st.run || next.air !== st.air;
      Object.assign(st, next);
      if (runChanged) { $('#dataSel').value = st.run || ''; loadAirs().then(show); } else show();
      return true;
    },
  };
}
