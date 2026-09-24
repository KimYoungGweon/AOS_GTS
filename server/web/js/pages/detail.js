// 2. 장비 상세 — 2-A Manual Mode / 2-B 자동 측정 모드
import { live, serverNow, get, post, put, del, qs } from '../api.js';
import { canControl, session } from '../state.js';
import { esc, fmt, fmtTime, fmtAge, id2, devState, toast, errMsg, MODE_NAME, STATUS_NAME, fmtShort } from '../ui.js';
import { lineChart, drawHeatmap, drawColorbar, fitColorbar, onResize, DEV_COLORS } from '../charts.js';

const LAST_KEY = 'gts_last_pair';

// AOS 수동 제어 6종 + LF On/Off (API 범위 = GTS_UDP_Protocol 4-5)
const CTRLS = [
  { id: 'hv', label: 'HV', desc: '고전압 (Dispersion)', unit: 'V', min: 0, max: 200, step: 0.1, d: 1 },
  { id: 'frq', label: 'Frq', desc: '구동 주파수', unit: 'kHz', min: 200, max: 800, step: 0.1, d: 1 },
  { id: 'duty', label: 'Duty', desc: '구동 듀티비', unit: '%', min: 20, max: 80, step: 0.01, d: 2 },
  { id: 'cv', label: 'CV', desc: '보상 전압', unit: 'V', min: -5, max: 5, step: 0.001, d: 3 },
  { id: 'lf_frq', label: 'LF_Frq', desc: '저주파 주파수', unit: 'Hz', min: 50, max: 200, step: 1, d: 0, lf: true },
  { id: 'lf_volt', label: 'LF_Volt', desc: '저주파 전압', unit: 'V', min: 0, max: 5, step: 0.01, d: 2, lf: true },
];
// Sample 모드 기본 8조합 (PCSW FAIMs_Twin_Sample.dat 와 동일)
const SAMPLE_SETS = [
  [100, 200, 55, 50], [120, 250, 54, 100], [140, 300, 60, 150], [140, 350, 50, 100],
  [160, 400, 65, 100], [160, 450, 62, 50], [180, 500, 60, 200], [180, 550, 57, 150],
];

// 주소: #/detail/3 (1대) · #/detail/1,3,5 (여러 대 — 2026-09-23.02) · #/detail/3/auto
export function parseIds(arg) {
  const ids = [];
  String(arg || '').split(',').forEach(x => { const n = parseInt(x, 10); if (n >= 1 && n <= 20 && !ids.includes(n)) ids.push(n); });
  return ids;
}

export async function mount(view, r) {
  let ids = parseIds(r.args[0]);
  if (!ids.length) {
    let last = ''; try { last = localStorage.getItem(LAST_KEY) || ''; } catch { /* */ }
    ids = parseIds(last);
    if (!ids.length) ids = [firstOnline() || 1];
  }
  let auto = r.args[1] === 'auto';
  if (auto && ids.length > 1) { toast('자동 측정 모드는 한 대씩 — 첫 번째 장비로 엽니다', 'warn'); ids = [ids[0]]; }
  const id = ids[0], multi = ids.length > 1, key = ids.join(',');
  try { localStorage.setItem(LAST_KEY, key); } catch { /* */ }
  if (location.hash.split('?')[0] !== `#/detail/${key}${auto ? '/auto' : ''}`) {
    history.replaceState(null, '', `#/detail/${key}${auto ? '/auto' : ''}`);
  }

  view.innerHTML = `
  <div class="subbar">
    <label class="label" for="pairSel">장비 선택</label>
    <select id="pairSel">${multi ? `<option value="" selected>${ids.length}대 선택됨</option>` : ''}${Array.from({ length: 20 }, (_, i) => `<option value="${i + 1}" ${!multi && i + 1 === id ? 'selected' : ''}>AOS ${id2(i + 1)} · GFC ${id2(i + 1)}</option>`).join('')}</select>
    <details class="multisel" id="multiSel"><summary class="btn sm" title="여러 장비를 함께 보고 제어">여러 대 ▾</summary>
      <div class="multisel-pop">
        <div class="multisel-grid">${Array.from({ length: 20 }, (_, i) => `<label><input type="checkbox" value="${i + 1}" ${ids.includes(i + 1) ? 'checked' : ''}> ${id2(i + 1)}</label>`).join('')}</div>
        <div class="row" style="margin-top:8px"><button class="btn sm" type="button" id="msOnline">온라인 전부</button>
          <button class="btn sm primary ml-auto" type="button" id="msGo">보기</button></div>
      </div></details>
    <div class="row" id="devBadge" style="padding:6px 12px;border-radius:8px;background:var(--panel);border:1px solid var(--line);gap:8px;flex-wrap:wrap"></div>
    <div class="seg" style="margin-left:12px">
      <a class="${auto ? '' : 'on'}" href="#/detail/${key}">Manual Mode</a>
      <a class="${auto ? 'on' : ''}" href="#/detail/${id}/auto" ${multi ? 'title="자동 측정은 한 대씩 — 첫 번째 장비"' : ''}>자동 측정 모드</a>
    </div>
    <div class="row ml-auto" style="gap:10px">
      <span class="small mute">마지막 수신</span><span class="mono small" id="lastRx" style="color:var(--tx-2)">—</span>
      ${auto ? '' : `<button class="btn" id="markBtn" type="button" ${canControl() ? '' : 'disabled'} title="지금의 파라미터와 전류를 DB 에 기록">현재 그래프 저장</button>`}
    </div>
  </div>
  <div class="page" id="body"></div>`;

  const $ = (s) => view.querySelector(s);
  $('#pairSel').addEventListener('change', (e) => { if (e.target.value) location.hash = `#/detail/${e.target.value}${auto ? '/auto' : ''}`; });
  $('#msOnline').addEventListener('click', () => {
    const on = new Set((live.last?.overview?.pairs || []).filter(p => p.aos.online || p.gfc.online).map(p => p.id));
    view.querySelectorAll('#multiSel input').forEach(i => { i.checked = on.has(+i.value); });
  });
  $('#msGo').addEventListener('click', () => {
    const sel = [...view.querySelectorAll('#multiSel input:checked')].map(i => +i.value);
    if (!sel.length) { toast('장비를 하나 이상 고르세요', 'warn'); return; }
    location.hash = `#/detail/${sel.join(',')}`;
  });

  live.setDetail(id);
  const ctx = { id, ids, multi, view, $, cleanups: [] };
  const page = auto ? await mountAuto(ctx) : await mountManual(ctx);
  const pairsOf = (m) => ids.map(i => (m.detail && m.detail.id === i ? m.detail : m.overview?.pairs?.[i - 1]));

  const offLive = live.on((m) => {
    const ds = pairsOf(m);
    const d = ds[0];
    if (!d) return;
    renderBadge(ctx, ds);
    page.tick(m, d, ds);
  });
  ctx.cleanups.push(offLive);
  if (live.last?.overview) renderBadge(ctx, pairsOf(live.last));

  return { cleanup: () => { live.setDetail(null); ctx.cleanups.forEach(f => { try { f(); } catch { /* */ } }); } };
}

function firstOnline() {
  const ps = live.last?.overview?.pairs || [];
  const p = ps.find(x => x.aos.online || x.gfc.online);
  return p?.id;
}

function renderBadge(ctx, ds) {
  ds = (Array.isArray(ds) ? ds : [ds]).filter(Boolean);
  if (!ds.length) return;
  const multi = ds.length > 1;
  ctx.$('#devBadge').innerHTML = ds.map((d, k) => {
    const a = devState(d.aos), g = devState(d.gfc);
    const gtxt = d.gfc.known && d.gfc.online ? (d.gfc.src_enable ? '공급중' : (d.gfc.pump?.[1] ? '펌프 ON' : '대기')) : g.t;
    const sw = multi ? `<span style="width:10px;height:3px;background:${DEV_COLORS[k % DEV_COLORS.length]};border-radius:2px"></span>` : '';
    return `${k ? '<span style="width:1px;height:18px;background:var(--line-2,#3A424D);margin:0 4px"></span>' : ''}${sw}
    <span class="dot ${a.dot}"></span><span class="mono" style="font-size:12.5px;font-weight:600">AOS-${id2(d.id)}</span>
    <span class="small" style="color:${a.fg}">${d.aos.run?.status === 'running' ? '스캔중' : a.t}</span>
    ${multi ? '' : '<span style="width:1px;height:14px;background:var(--line)"></span>'}
    <span class="dot ${g.dot}"></span><span class="mono" style="font-size:12.5px;font-weight:600">GFC${multi ? '' : `-${id2(d.id)}`}</span>
    <span class="small" style="color:${g.fg}">${gtxt}</span>`;
  }).join('');
  const rx = Math.min(...ds.map(d => Math.min(d.aos.last_rx_s ?? 1e9, d.gfc.last_rx_s ?? 1e9)));
  ctx.$('#lastRx').textContent = rx < 1e8 ? `${fmtTime(Date.now() / 1000 - rx)} (${fmtAge(rx)} 전)` : '수신 없음';
}

// ── 공통: AOS Current 링 / GFC 링 ──────────────────────────────────
function makeSeries() {
  const s = { aos: [], gfc: [], lastA: 0, lastG: 0 };
  s.pushAos = (p) => { if (p && p[0] > s.lastA) { s.aos.push(p); s.lastA = p[0]; if (s.aos.length > 900) s.aos.shift(); } };
  s.pushGfc = (g) => {
    if (g && g.t > s.lastG) { s.gfc.push(g); s.lastG = g.t; if (s.gfc.length > 900) s.gfc.shift(); }
  };
  return s;
}

async function loadSeries(id, s) {
  try {
    const [a, g] = await Promise.all([get(`/api/aos/${id}/current?sec=600`), get(`/api/gfc/${id}/telemetry?sec=600`)]);
    a.points.forEach(p => s.pushAos(p));
    g.points.forEach(p => s.pushGfc(p));
  } catch (e) { toast(errMsg(e), 'err'); }
}

function stats(pts, span) {
  const t0 = serverNow() - span;
  const v = pts.filter(p => p[0] >= t0).map(p => p[1]);
  if (!v.length) return null;
  const avg = v.reduce((a, b) => a + b, 0) / v.length;
  const sd = Math.sqrt(v.reduce((a, b) => a + (b - avg) ** 2, 0) / v.length);
  return { now: v[v.length - 1], min: Math.min(...v), max: Math.max(...v), avg, sd };
}

// 전류값 수신 상태 — "0 이 들어온 것" 과 "안 들어온 것" 을 구별해 보여 준다
function paintCurState(ctx, d, series) {
  const el = ctx.$('#curState');
  const a = d?.aos, c = a?.cur;
  let chip = ['gray', '확인 중', ''], empty = 'AOS Current 수신 없음';
  if (!a?.online) { chip = ['gray', 'AOS 오프라인', '']; empty = 'AOS 오프라인 — 전류값 없음'; }
  else if (a && !('cur' in a)) {
    chip = ['err', '서버 구버전', ''];
    empty = '서버가 전류 상태(cur)를 보내지 않음 — gts-push 로 서버 갱신 후 새로고침';
  } else if (c?.valid) {
    chip = ['ok', '전류 수신 중', `채널 ${c.channel}`];
    const last = series?.aos?.[series.aos.length - 1];
    empty = last ? `전류 수신 중이나 그래프 구간 밖 — 마지막 점 ${Math.round(serverNow() - last[0])}초 전`
                 : '전류 수신 중 — 첫 값을 기다리는 중';
  } else if (c) {
    chip = ['warn', '전류값 미수신', c.reason || ''];
    empty = `전류값 미수신 — ${c.reason || ''}${c.last_valid_s != null ? ` · 마지막 정상 ${Math.round(c.last_valid_s)}초 전` : ''}`;
  }
  if (el) { el.className = `chip ${chip[0]}`; el.textContent = chip[1]; el.title = chip[2]; }
  const adc = ctx.$('#curAdc');
  if (adc) adc.textContent = c?.adc
    ? `ADC 원시값 air_p ${c.adc.air_p} · air_n ${c.adc.air_n} · gas_p ${c.adc.gas_p} · gas_n ${c.adc.gas_n}  (표시 채널 ${c.channel}) · 상태 ${c.code || 'ok'}`
    : (a?.online ? `브리지 STATUS: ${c ? '값 없음' : a && 'cur' in a ? '미수신' : '—'}` : '');
  return empty;
}

// 여러 대 — 장비별 전류 수신 상태 칩
function paintCurStates(ctx, ds, color) {
  const el = ctx.$('#curState');
  const list = ds.filter(Boolean);
  const okN = list.filter(d => d.aos?.online && d.aos?.cur?.valid).length;
  if (el) { el.className = `chip ${okN === list.length ? 'ok' : okN ? 'warn' : 'gray'}`; el.textContent = `전류 수신 ${okN}/${list.length}대`; }
  const adc = ctx.$('#curAdc');
  if (adc) adc.innerHTML = list.map(d => {
    const c = d.aos?.cur;
    const t = !d.aos?.online ? '오프라인' : c?.valid ? `수신 중 (gas_p ${c.adc?.gas_p})` : c ? `미수신 — ${c.reason || ''}` : '—';
    return `<span style="color:${color(d.id)}">AOS ${id2(d.id)}</span> ${esc(t)}`;
  }).join(' &nbsp;·&nbsp; ');
  return okN ? 'AOS Current 수신 없음' : '선택한 AOS 모두 전류값 없음';
}

// 제어권 임대: 내가 쥐고 있으면 10초마다 연장, 나갈 때 반납
function lockKeeper(ctx) {
  let mine = { aos: false, gfc: false };
  const hb = setInterval(async () => {
    for (const k of ['aos', 'gfc']) {
      if (!mine[k]) continue;
      try { await post(`/api/devices/${k}/${ctx.id}/lock`); } catch { mine[k] = false; }
    }
  }, 10000);
  ctx.cleanups.push(() => {
    clearInterval(hb);
    for (const k of ['aos', 'gfc']) if (mine[k]) del(`/api/devices/${k}/${ctx.id}/lock`).catch(() => {});
  });
  return {
    update(d) { mine = { aos: !!d.lock_aos?.mine, gfc: !!d.lock_gfc?.mine }; },
    // 콘솔·웹 동시 제어 (2026-09-23.01) — 막지 않고 "최근 조작자" 만 알려 준다
    html(ls, what) {
      if (!ls) return '';
      if (!canControl()) return `<div class="lockbar free">현재 토큰은 조회 전용입니다</div>`;
      if (ls.locked && ls.lock.owner_kind === 'console') {
        const ago = Math.max(0, 30 - ls.lock.remain_s);
        return `<div class="lockbar other">${esc(ls.lock.owner_label)} 에서 ${what} 을 ${Math.round(ago)}초 전 조작 — 콘솔 값이 이 화면에 반영됩니다 (동시 제어 가능)</div>`;
      }
      if (ls.locked && !ls.mine) {
        return `<div class="lockbar other">다른 웹 사용자(${esc(ls.lock.owner_label)})가 ${what} 을 조작 중 — 동시 제어 가능</div>`;
      }
      if (ls.mine) return `<div class="lockbar mine">● 내가 최근 ${what} 을 조작함 · 콘솔과 동시 제어 가능</div>`;
      return `<div class="lockbar free">콘솔과 웹에서 동시에 제어할 수 있습니다 · 나중 명령이 적용됩니다</div>`;
    },
  };
}

// ════════════════════════════════════════════════════════════════════
// 2-A. Manual Mode
// ════════════════════════════════════════════════════════════════════
async function mountManual(ctx) {
  const { id, ids, multi, $ } = ctx;
  const color = (i) => DEV_COLORS[ids.indexOf(i) % DEV_COLORS.length];
  const idsTxt = ids.map(id2).join(' · ');
  $('#body').innerHTML = `
  <div class="det">
    <div class="det-main">
      <section class="panel" style="display:flex;flex-direction:column">
        <div class="row" style="margin-bottom:6px;flex-wrap:wrap">
          <h2>AOS Current</h2><span class="small mute">1초 주기 수신 · 단위 V</span>
          <span class="chip gray" id="curState" title="">확인 중</span>
          <div class="row ml-auto">
            <label class="small mute" for="span">표시 구간</label>
            <select id="span"><option value="60">최근 60초</option><option value="120" selected>최근 120초</option>
              <option value="300">최근 300초</option><option value="600">최근 600초</option></select>
            <label class="row small dim" style="gap:6px"><input type="checkbox" id="autoScale" checked> 자동 스케일</label>
          </div>
        </div>
        <div class="row" style="align-items:flex-end;gap:26px;margin-bottom:6px;flex-wrap:wrap">
          <div class="${multi ? 'hidden' : ''}"><div class="small mute" style="margin-bottom:2px">현재 값</div><div class="bigval" id="curNow">—</div></div>
          <div class="kv" id="curStats"></div>
        </div>
        <canvas class="chart" id="curChart" style="height:320px"></canvas>
        <div class="tiny mute mono" id="curAdc" style="margin-top:4px"></div>
      </section>

      <section class="panel">
        <div class="row" style="margin-bottom:12px;flex-wrap:wrap">
          <h2>GFC ${idsTxt} 가스 농도 및 제어 상태</h2>
          <label class="label ${multi ? 'hidden' : ''}" for="gasName">가스명</label>
          <input type="text" id="gasName" class="${multi ? 'hidden' : ''}" style="width:150px" maxlength="64">
          <button class="btn sm ${multi ? 'hidden' : ''}" id="gasSave" type="button">이름 저장</button>
          ${multi ? `<span class="chip warn" title="펌프·모드·주입 버튼이 선택한 GFC 모두에 전송됩니다">${ids.length}대 동시 제어</span>` : ''}
          <div class="seg sm blue" id="gfcMode"><button type="button" data-m="auto">자동 제어</button><button type="button" data-m="manual">수동 제어</button></div>
          <div class="row ml-auto">
            <button class="btn sm" id="pumpBtn" type="button">펌프 ON</button>
            <button class="btn sm danger" id="stopBtn" type="button" title="주입 시퀀스 정지 + 전 펌프 OFF">공급 중지</button>
          </div>
        </div>
        <div id="gfcLock" style="margin-bottom:10px"></div>
        <div class="row" style="align-items:stretch;gap:20px;flex-wrap:wrap">
          <div class="inset grow" style="padding:12px 14px;min-width:320px">
            <div class="row" style="margin-bottom:4px"><span class="small mute">가스 농도 (TVOC 1) · 최근 10분</span>
              <label class="row small dim" style="gap:5px;margin-left:10px" title="체크하면 0 ~ 4.0 V 로 고정"><input type="checkbox" id="gasFix"> 스케일 고정 (0~4 V)</label>
              <span class="ml-auto mono" style="font-size:20px;font-weight:600;color:var(--blue)" id="gNow">—</span><span class="small mute">V</span></div>
            <canvas class="chart" id="gfcChart" style="height:170px"></canvas>
          </div>
          <div style="width:476px;max-width:100%" class="col">
            <div class="gfcgrid" id="gfcStats"></div>
            <div class="inset row" style="padding:10px 12px;gap:8px;flex-wrap:wrap">
              <span class="small mute">자동 주입</span>
              <label class="small mute" for="tStart">초기</label><input id="tStart" type="number" step="0.1" min="0.1" max="60" style="width:70px">
              <label class="small mute" for="tCycle">주기 분사</label><input id="tCycle" type="number" step="0.1" min="0.1" max="60" style="width:70px">
              <span class="small mute">초</span>
              <button class="btn sm" id="timesBtn" type="button">시간 저장</button>
              <button class="btn sm ok ml-auto" id="runBtn" type="button">주입 시작</button>
            </div>
          </div>
        </div>
      </section>
    </div>

    <aside class="panel" style="display:flex;flex-direction:column;padding:18px">
      <div class="row" style="margin-bottom:4px"><h2>AOS-${idsTxt} 수동 제어</h2><span class="chip warn">${multi ? `${ids.length}대 동시` : 'MANUAL'}</span>
        <span class="ml-auto small mute">편집 중 <b id="chgCount" style="color:var(--warn-fg)">0개</b></span></div>
      <p class="small mute" style="margin:0 0 10px;line-height:1.5">값을 바꾼 뒤 <b style="color:var(--tx-2)">적용</b>을 눌러야 장비로 전송됩니다.
        편집하지 않은 항목은 <b style="color:var(--tx-2)">장비 값을 그대로 따라갑니다</b> — 콘솔에서 바꾸면 여기도 바뀝니다.</p>
      <div id="aosLock" style="margin-bottom:10px"></div>
      <div class="col" id="ctrls" style="gap:9px;flex:1 1 auto"></div>
      <div class="row" style="margin-top:14px;padding-top:14px;border-top:1px solid var(--line)">
        <button class="btn primary grow" id="applyAll" type="button" style="height:44px;font-size:14px">변경값 일괄 적용</button>
        <button class="btn" id="resetAll" type="button" style="height:44px" title="편집한 값을 버리고 장비 값으로">편집 취소</button>
        <button class="btn" id="reread" type="button" style="height:44px" title="장비에 현재 설정값을 다시 요청">다시 읽기</button>
      </div>
    </aside>
  </div>`;

  const S = new Map(ids.map(i => [i, makeSeries()]));   // 장비별 링
  const s = S.get(id);
  const lk = lockKeeper(ctx);
  let ds = [];                                         // 선택 장비들의 최신 pair_view
  let span = 120;
  let dev = null;             // 장비가 보고하는 최신 값 (콘솔·웹 누가 바꾸든)
  const val = {};             // 화면 값 = 편집 중이면 사용자 값, 아니면 장비 값
  const dirty = new Set();    // 사용자가 편집한 항목 — 장비가 그 값이 되면 해제
  const sentAt = {};          // 적용 누른 시각 — 반영 안 되면 경고
  let gfc = null, gasNameLoaded = false;

  // ── AOS 제어 패널 ──
  $('#ctrls').innerHTML = CTRLS.filter(c => !c.lf).map(ctrlHtml).join('') + `
    <div class="ctl row" id="c_lf_on" style="gap:10px">
      <div><div style="font-size:12.5px;font-weight:600">LF On / Off</div>
        <div class="tiny mute">저주파 인가 스위치 · 장비 <span id="lfDev">—</span></div></div>
      <span class="ml-auto mono small" id="lfTxt" style="font-weight:600">—</span>
      <button class="switch" type="button" role="switch" aria-checked="false" aria-label="LF On Off 전환" id="lfSw"></button>
      <button class="btn sm" type="button" data-apply="lf_on">적용</button>
    </div>` + CTRLS.filter(c => c.lf).map(ctrlHtml).join('');

  function ctrlHtml(c) {
    return `<div class="ctl" id="c_${c.id}">
      <div class="hd"><label for="r_${c.id}">${c.label}</label><span class="tiny mute">${c.desc}</span>
        <span class="v" id="v_${c.id}">—</span><span class="small mute">${c.unit}</span></div>
      <input type="range" id="r_${c.id}" min="${c.min}" max="${c.max}" step="${c.step}">
      <div class="ft"><span class="tiny mute mono">${c.min}~${c.max}</span>
        <span class="ml-auto tiny mute mono">장비 <span id="d_${c.id}">—</span></span>
        <input type="number" id="n_${c.id}" min="${c.min}" max="${c.max}" step="${c.step}" aria-label="${c.label} 설정값">
        <button class="btn sm" type="button" data-apply="${c.id}">적용</button></div>
    </div>`;
  }

  const ctrlEnabled = () => canControl() && dev !== null;
  const same = (k, a, b) => {
    if (k === 'lf_on') return !!a === !!b;
    const c = CTRLS.find(x => x.id === k);
    return Math.abs(a - b) <= c.step / 2;
  };
  function paintCtrl(c) {
    const v = val[c.id];
    const changed = dirty.has(c.id) && dev && !same(c.id, v, dev[c.id]);
    $(`#c_${c.id}`).classList.toggle('changed', !!changed);
    $(`#v_${c.id}`).textContent = v === undefined ? '—' : fmt(v, c.d);
    if (document.activeElement !== $(`#n_${c.id}`)) $(`#n_${c.id}`).value = v === undefined ? '' : (+v).toFixed(c.d);
    if (document.activeElement !== $(`#r_${c.id}`)) $(`#r_${c.id}`).value = v ?? c.min;
  }
  function paintLf() {
    const on = !!val.lf_on;
    $('#lfSw').setAttribute('aria-checked', on);
    $('#lfTxt').textContent = on ? 'ON' : 'OFF';
    $('#lfTxt').style.color = on ? 'var(--ok-fg)' : 'var(--tx-mute)';
    $('#c_lf_on').classList.toggle('changed', dirty.has('lf_on') && !!dev && !same('lf_on', on, dev.lf_on));
  }
  function changedKeys() {
    if (!dev) return [];
    return [...dirty].filter(k => !same(k, val[k], dev[k]));
  }
  function paintCount() {
    const n = changedKeys().length;
    $('#chgCount').textContent = `${n}개`;
    $('#applyAll').disabled = !n || !ctrlEnabled();
  }
  function flash(id) {                       // 장비 값이 바뀌어 화면이 따라간 항목 (콘솔 조작 등)
    const el = $(id); if (!el) return;
    el.style.transition = 'none'; el.style.boxShadow = '0 0 0 2px rgba(111,168,220,.8)';
    setTimeout(() => { el.style.transition = 'box-shadow .8s'; el.style.boxShadow = ''; }, 600);
  }
  // 장비 값 반영 — 편집하지 않은 항목은 따라가고, 편집한 항목은 장비가 그 값이 되면 편집 해제
  function onDevice(p) {
    const prev = dev;
    dev = p;
    const keys = [...CTRLS.map(c => c.id), 'lf_on'];
    for (const k of keys) {
      if (dirty.has(k)) {
        if (same(k, val[k], p[k])) { dirty.delete(k); delete sentAt[k]; }
        else if (sentAt[k] && Date.now() - sentAt[k] > 5000) {
          delete sentAt[k];
          toast(`${k.toUpperCase()} 적용이 장비에 반영되지 않았습니다 (장비 값 ${k === 'lf_on' ? (p[k] ? 'ON' : 'OFF') : p[k]}) — 다시 적용해 보세요`, 'warn', 7000);
        }
      } else {
        val[k] = p[k];
        if (prev && !same(k, prev[k], p[k])) flash(k === 'lf_on' ? '#c_lf_on' : `#c_${k}`);
      }
    }
    CTRLS.forEach(paintCtrl); paintLf(); paintCount();
  }

  CTRLS.forEach(c => {
    $(`#r_${c.id}`).addEventListener('input', (e) => { val[c.id] = +e.target.value; dirty.add(c.id); paintCtrl(c); paintCount(); });
    $(`#n_${c.id}`).addEventListener('change', (e) => {
      let v = +e.target.value; if (Number.isNaN(v)) return;
      v = Math.min(c.max, Math.max(c.min, v)); val[c.id] = v; dirty.add(c.id); paintCtrl(c); paintCount();
    });
  });
  $('#lfSw').addEventListener('click', () => { val.lf_on = !val.lf_on; dirty.add('lf_on'); paintLf(); paintCount(); });

  async function send(keys) {
    const body = {};
    keys.forEach(k => { body[k] = val[k]; dirty.add(k); });
    // 여러 대면 선택한 AOS 모두에 같은 값 (2026-09-23.02). 오프라인은 건너뜀
    const targets = ids.filter(i => !multi || ds.find(d => d?.id === i)?.aos?.online);
    const skipped = ids.filter(i => !targets.includes(i));
    const res = await Promise.allSettled(targets.map(i => post(`/api/aos/${i}/params`, body)));
    const ok = targets.filter((_, k) => res[k].status === 'fulfilled');
    const bad = targets.map((i, k) => [i, res[k]]).filter(([, r]) => r.status === 'rejected');
    if (ok.includes(id)) { const now = Date.now(); keys.forEach(k => { sentAt[k] = now; }); }
    if (ok.length) toast(`AOS ${ok.map(id2).join(', ')} 적용: ${keys.join(', ')} — 장비 반영을 확인합니다`, 'ok');
    bad.forEach(([i, r]) => toast(`AOS ${id2(i)}: ${errMsg(r.reason)}`, 'err', 6000));
    if (skipped.length) toast(`오프라인이라 건너뜀: AOS ${skipped.map(id2).join(', ')}`, 'warn', 5000);
  }
  $('#ctrls').addEventListener('click', (e) => {
    const k = e.target.closest('[data-apply]')?.dataset.apply;
    if (k) send([k]);
  });
  $('#applyAll').addEventListener('click', () => { const ks = changedKeys(); if (ks.length) send(ks); });
  $('#resetAll').addEventListener('click', () => { dirty.clear(); if (dev) onDevice(dev); });
  $('#reread').addEventListener('click', async () => {
    try { await post(`/api/aos/${id}/params/query`); toast('장비에 설정값을 다시 요청했습니다'); }
    catch (e) { toast(errMsg(e), 'err'); }
  });

  // ── GFC ──
  $('#gasSave').addEventListener('click', async () => {
    const name = $('#gasName').value.trim(); if (!name) return;
    try { await put(`/api/pairs/${id}/gas-name`, { gas_name: name }); toast(`가스명 저장: ${name}`, 'ok'); }
    catch (e) { toast(errMsg(e), 'err'); }
  });
  const gfcCmd = async (path, body, okMsg) => {       // 여러 대면 선택한 GFC 모두에
    const targets = multi ? ids.filter(i => ds.find(d => d?.id === i)?.gfc?.online) : [id];
    if (!targets.length) { toast('온라인 GFC 가 없습니다', 'warn'); return; }
    const res = await Promise.allSettled(targets.map(i => post(`/api/gfc/${i}/${path}`, body)));
    const okN = res.filter(r => r.status === 'fulfilled').length;
    if (okN) toast(multi ? `${okMsg.replace(`GFC ${id2(id)} `, '')} → GFC ${targets.filter((_, k) => res[k].status === 'fulfilled').map(id2).join(', ')}` : okMsg, 'ok');
    res.forEach((r, k) => { if (r.status === 'rejected') toast(`GFC ${id2(targets[k])}: ${errMsg(r.reason)}`, 'err', 6000); });
  };
  $('#gfcMode').addEventListener('click', (e) => {
    const m = e.target.dataset.m; if (m) gfcCmd('mode', { mode: m }, `GFC ${id2(id)} ${m === 'auto' ? '자동' : '수동'} 제어`);
  });
  $('#pumpBtn').addEventListener('click', () => {
    const on = !(gfc?.pump?.[1]); gfcCmd('pump', { on }, `GFC ${id2(id)} 펌프 ${on ? 'ON' : 'OFF'}`);
  });
  $('#stopBtn').addEventListener('click', () => gfcCmd('auto-run', { run: false }, `GFC ${id2(id)} 공급 중지`));
  $('#runBtn').addEventListener('click', () => {
    const run = !gfc?.src_enable; gfcCmd('auto-run', { run }, `GFC ${id2(id)} 자동 주입 ${run ? '시작' : '정지'}`);
  });
  $('#timesBtn').addEventListener('click', () => gfcCmd('times', { start_s: +$('#tStart').value, cycle_s: +$('#tCycle').value }, '주입 시간 저장'));

  // ── 현재 그래프 저장 (manual_mark) ──
  $('#markBtn').addEventListener('click', async () => {
    const note = prompt('메모 (선택) — 지금의 파라미터와 최근 10초 평균 전류가 기록됩니다', '');
    if (note === null) return;
    for (const i of ids) {
      try {
        const r = await post(`/api/aos/${i}/marks`, { note, avg_sec: 10 });
        toast(`AOS ${id2(i)} 기록됨 #${r.mark_id} · 평균 ${fmt(r.current?.avg, 3)} V`, 'ok');
      } catch (e) { toast(`AOS ${id2(i)}: ${errMsg(e)}`, 'err'); }
    }
  });

  let curEmpty = 'AOS Current 수신 없음 (AOS 오프라인)';
  $('#span').addEventListener('change', (e) => { span = +e.target.value; draw(); });
  $('#autoScale').addEventListener('change', draw);
  $('#gasFix').addEventListener('change', draw);

  function draw() {
    const auto = $('#autoScale').checked;
    const now = serverNow();
    // 자동 스케일 Off = 0 ~ 5 V 고정 (2026-09-23.02)
    const curOpt = { span, auto, yMin: 0, yMax: 5, height: 320, emptyText: curEmpty, now };
    if (multi) curOpt.series = ids.map(i => ({ points: S.get(i).aos, color: color(i), label: `AOS ${id2(i)}` }));
    else curOpt.points = s.aos;
    lineChart($('#curChart'), curOpt);
    if (multi) {
      $('#curNow').textContent = '';
      $('#curStats').innerHTML = `<table class="mtbl"><tr><th></th><th>현재</th><th>최소</th><th>최대</th><th>평균</th><th>표준편차</th></tr>${ids.map(i => {
        const st = stats(S.get(i).aos, span);
        return `<tr><td><span class="sw" style="background:${color(i)}"></span>AOS ${id2(i)}</td>${[st?.now, st?.min, st?.max, st?.avg].map(v => `<td>${fmt(v, 3)}</td>`).join('')}<td>${fmt(st?.sd, 4)}</td></tr>`;
      }).join('')}</table>`;
    } else {
      const st = stats(s.aos, span);
      $('#curNow').textContent = st ? fmt(st.now, 3) : '—';
      $('#curStats').innerHTML = [['최소', st?.min], ['최대', st?.max], ['평균', st?.avg], ['표준편차', st?.sd]]
        .map(([k, v]) => `<div><div class="k">${k}</div><div class="v">${fmt(v, k === '표준편차' ? 4 : 3)}</div></div>`).join('');
    }
    // 가스 농도: 스케일 고정이면 0 ~ 4.0 V (2026-09-23.02)
    const fix = $('#gasFix').checked;
    const gOpt = { span: 600, now, color: '#6FA8DC', height: 170, emptyText: 'GFC 수신 없음', auto: !fix, yMin: 0, yMax: 4 };
    if (multi) gOpt.series = ids.map(i => ({ points: S.get(i).gfc.map(g => [g.t, g.volt1]), color: color(i), label: `GFC ${id2(i)}` }));
    else gOpt.points = s.gfc.map(g => [g.t, g.volt1]);
    lineChart($('#gfcChart'), gOpt);
  }

  function paintGfc(d) {
    gfc = d.gfc;
    if (!gasNameLoaded && document.activeElement !== $('#gasName')) { $('#gasName').value = d.gas_name || ''; gasNameLoaded = true; }
    const g = d.gfc;
    $('#gNow').textContent = multi ? '' : (g.online ? fmt(g.conc_v, 3) : '—');
    $$('#gfcMode button').forEach(b => b.classList.toggle('on', g.known && ((b.dataset.m === 'auto') === (g.mode === 'AUTO'))));
    const pumps = g.pump || [0, 0, 0];
    $('#gfcStats').innerHTML = [
      ['현재 농도 (TVOC 1)', fmt(g.conc_v, 3), 'V', 'var(--blue)'],
      ['TVOC 2', fmt(g.volt2, 3), 'V'],
      ['펌프 1 / 2 / 3', pumps.map(p => p ? 'ON' : '—').join(' / '), '', pumps[1] ? 'var(--ok-fg)' : ''],
      ['주입 상태', g.src_enable ? (g.src_init ? '초기 주입' : (g.src_on ? '분사 중' : '대기')) : '정지', '', g.src_enable ? 'var(--ok-fg)' : ''],
      ['잔여 / 사이클', `${fmt(g.remain_s, 1)} / ${g.cycle_count ?? '—'}`, 's · 회'],
      ['RSSI', g.rssi ?? '—', 'dBm'],
    ].map(([k, v, u, c]) => `<div><div class="k">${k}</div><div class="v" style="${c ? `color:${c}` : ''}">${esc(v)} <small>${u}</small></div></div>`).join('');
    if (multi) {       // 여러 대: 장비별 한 줄
      $('#gfcStats').innerHTML = `<table class="mtbl" style="grid-column:1/-1"><tr><th></th><th>가스</th><th>농도 V</th><th>펌프</th><th>주입</th><th>모드</th></tr>${ds.filter(Boolean).map(x => {
        const q = x.gfc, pp = q.pump || [0, 0, 0];
        return `<tr><td><span class="sw" style="background:${color(x.id)}"></span>GFC ${id2(x.id)}</td><td>${esc(x.gas_name || '—')}</td>
          <td>${q.online ? fmt(q.conc_v, 3) : '—'}</td><td>${pp[1] ? 'ON' : '—'}</td>
          <td>${q.online ? (q.src_enable ? (q.src_on ? '분사' : '대기') : '정지') : '오프라인'}</td><td>${q.known ? (q.mode || '—') : '—'}</td></tr>`;
      }).join('')}</table>`;
    }
    const can = canControl() && (multi ? ds.some(x => x?.gfc?.online) : g.online);
    ['#gfcMode button', '#pumpBtn', '#stopBtn', '#runBtn', '#timesBtn'].forEach(q => $$(q).forEach(b => { b.disabled = !can; }));
    $('#gasSave').disabled = !canControl();
    $('#pumpBtn').textContent = pumps[1] ? '펌프 OFF' : '펌프 ON';
    $('#runBtn').textContent = g.src_enable ? '주입 정지' : '주입 시작';
    $('#runBtn').className = `btn sm ml-auto ${g.src_enable ? 'danger' : 'ok'}`;
    if (document.activeElement !== $('#tStart') && g.start_s !== undefined) $('#tStart').value = g.start_s;
    if (document.activeElement !== $('#tCycle') && g.cycle_s !== undefined) $('#tCycle').value = g.cycle_s;
    $('#gfcLock').innerHTML = g.known ? lk.html(d.lock_gfc, 'GFC') : '';
  }

  function $$(q) { return [...ctx.view.querySelectorAll(q)]; }

  function paintAos(d) {
    const p = d.aos.params;
    if (p) onDevice(p);
    if (multi) {       // 장비별 현재 값 (색으로 구별)
      const vv = (fn) => ds.filter(Boolean).map(x => `<span style="color:${color(x.id)}">${id2(x.id)} ${x.aos.params ? fn(x.aos.params) : '—'}</span>`).join(' · ');
      CTRLS.forEach(c => { $(`#d_${c.id}`).innerHTML = vv(q => fmt(q[c.id], c.d)); });
      $('#lfDev').innerHTML = vv(q => (q.lf_on ? 'ON' : 'OFF'));
    } else {
      CTRLS.forEach(c => { $(`#d_${c.id}`).textContent = p ? fmt(p[c.id], c.d) : '—'; });
      $('#lfDev').textContent = p ? (p.lf_on ? 'ON' : 'OFF') : '—';
    }
    const running = ds.filter(Boolean).find(x => x.aos.run?.status === 'running');
    const can = ctrlEnabled() && d.aos.online && !running;
    ctx.view.querySelectorAll('#ctrls input, #ctrls button').forEach(el => { el.disabled = !can; });
    $('#reread').disabled = !d.aos.online;
    paintCount();
    let lockHtml = lk.html(d.lock_aos, 'AOS');
    if (running) lockHtml = `<div class="lockbar other">AOS ${id2(running.id)} 측정 run #${running.aos.run.run_id} 진행 중 — 파라미터 변경 불가</div>`;
    else if (multi) lockHtml = `<div class="lockbar free">적용하면 선택한 AOS ${ids.length}대 (${idsTxt}) 에 같은 값을 보냅니다 · 화면 값은 AOS ${id2(id)} 기준, 장비별 값은 색으로 표시</div>`;
    if (!d.aos.online) lockHtml = `<div class="lockbar lost">AOS ${id2(id)} 오프라인</div>`;
    else if (!p) lockHtml = `<div class="lockbar free">장비 설정값을 아직 받지 못했습니다 — "다시 읽기"를 눌러 보세요</div>`;
    $('#aosLock').innerHTML = lockHtml;
    $('#markBtn').disabled = !canControl() || !ds.some(x => x?.aos?.online);
  }

  await Promise.all(ids.map(i => loadSeries(i, S.get(i))));
  draw();
  ctx.cleanups.push(onResize($('#curChart'), draw));
  // 처음 들어왔을 때 장비 값이 없으면 한 번 물어본다
  const first = live.last?.detail?.id === id ? live.last.detail : null;
  if (!first?.aos?.params) post(`/api/aos/${id}/params/query`).catch(() => {});

  return {
    tick(m, d, dsNow) {
      ds = dsNow || [d];
      for (const i of ids) { if (m.aos?.[i]) S.get(i).pushAos(m.aos[i]); if (m.gfc?.[i]) S.get(i).pushGfc(m.gfc[i]); }
      lk.update(d);
      curEmpty = multi ? paintCurStates(ctx, ds, color) : paintCurState(ctx, d, s);
      paintAos(d); paintGfc(d); draw();
    },
  };
}

// ════════════════════════════════════════════════════════════════════
// 2-B. 자동 측정 모드
// ════════════════════════════════════════════════════════════════════
async function mountAuto(ctx) {
  const { id, $ } = ctx;
  $('#body').innerHTML = `
  <div class="lockbar other" style="margin-bottom:14px">⚠ 장비로 측정 시작 명령(0x80)과 heatmap 업로드(0x86)는 아직 구현되지 않았습니다.
    지금은 <b>&nbsp;측정 run 기록(DB)&nbsp;</b>만 만들어지며, heatmap 은 API 업로드로만 들어옵니다.</div>
  <div class="auto">
    <div class="col" style="gap:18px">
      <section class="panel">
        <div class="row" style="flex-wrap:wrap;gap:14px">
          <span class="label">측정 종류</span>
          <div class="modes" id="kinds">
            <label class="on"><input type="radio" name="kind" value="gas" checked> 가스 (Target)</label>
            <label><input type="radio" name="kind" value="air_ref"> 기준 Air (Ref)</label>
          </div>
          <span class="label" style="margin-left:10px">측정 모드</span>
          <div class="modes" id="modes">
            <label class="on"><input type="radio" name="mode" value="full8" checked> Full <span class="tiny mute">1600 HM · ≈8h</span></label>
            <label><input type="radio" name="mode" value="hour1"> 1 hour <span class="tiny mute">256 HM</span></label>
            <label><input type="radio" name="mode" value="fast"> Sample <span class="tiny mute">8 HM</span></label>
          </div>
        </div>
        <div class="row" style="flex-wrap:wrap;gap:12px;margin-top:14px">
          <label class="label" for="gasName">측정 가스</label>
          <input type="text" id="gasName" style="width:150px" maxlength="64">
          <button class="btn sm" id="gasSave" type="button">이름 저장</button>
          <label class="label" for="conc">농도</label><input type="number" id="conc" step="0.01" style="width:80px"><span class="small mute">V</span>
          <label class="label" for="airSel">기준 Air</label>
          <select id="airSel" style="max-width:280px"><option value="">(선택 안 함)</option></select>
          <label class="label" for="label">라벨</label><input type="text" id="label" style="width:180px" maxlength="80" placeholder="예: tol_0923_A">
        </div>
        <details id="sampleBox" class="hidden" style="margin-top:12px">
          <summary class="small dim" style="cursor:pointer">Sample 8조합 (HV · Frq · Duty · LFF)</summary>
          <div id="sampleSets" style="display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:6px;margin-top:8px"></div>
        </details>
        <div class="row" style="margin-top:16px;gap:10px;flex-wrap:wrap">
          <button class="btn primary" id="startBtn" type="button" style="height:40px;padding:0 20px">측정 시작</button>
          <button class="btn" id="pauseBtn" type="button" style="height:40px">일시 정지</button>
          <button class="btn ok" id="finishBtn" type="button" style="height:40px">측정 종료 (저장)</button>
          <button class="btn danger" id="abortBtn" type="button" style="height:40px">중단 (폐기 표시)</button>
          <div class="row ml-auto" style="gap:18px">
            <div><div class="small mute">진행 heatmap</div><div class="num" style="font-size:20px" id="prog">—</div></div>
            <div style="width:220px"><div class="row small mute"><span id="progLbl">진행률</span><span class="ml-auto num" id="pct">—</span></div>
              <div class="progress" style="margin-top:6px"><i id="bar" style="width:0%"></i></div></div>
            <div><div class="small mute">남은 시간(추정)</div><div class="num" style="font-size:20px" id="eta">—</div></div>
          </div>
        </div>
      </section>

      <section class="panel">
        <div class="row" style="margin-bottom:6px"><h2>AOS Current</h2>
          <span class="small mute">x축 최근 120초 · y축 V</span><span class="chip gray" id="curState">확인 중</span></div>
        <canvas class="chart" id="curChart" style="height:210px"></canvas>
        <div class="tiny mute mono" id="curAdc" style="margin-top:4px"></div>
      </section>

      <section class="panel">
        <div class="row" style="margin-bottom:12px"><h2>누적 Heatmap — 최근 4개</h2>
          <span class="small mute">x축 LF_Volt (0→3 V) · y축 CV (−1→1 V) · 값 V</span>
          <a class="ml-auto small" id="toViewer" href="#/viewer">Viewer 에서 전체 보기 →</a></div>
        <div class="row" style="align-items:stretch;gap:14px">
          <div class="hm4 grow" id="recent"></div>
          <div class="cbar" style="width:44px"><span id="cbMax">—</span><canvas id="cb" style="height:120px"></canvas><span id="cbMin">—</span></div>
        </div>
      </section>
    </div>

    <aside class="col" style="gap:18px">
      <section class="panel">
        <h2 style="margin-bottom:12px">측정 정보</h2>
        <div class="infogrid" id="info"><div class="small mute">진행 중인 측정이 없습니다</div></div>
      </section>
      <section class="panel">
        <div class="row" style="margin-bottom:8px"><h2>GFC 가스 농도</h2><span class="chip" id="gMode">—</span>
          <span class="ml-auto mono" style="font-size:18px;font-weight:600;color:var(--blue)" id="gNow">—</span></div>
        <canvas class="chart" id="gfcChart" style="height:130px"></canvas>
      </section>
      <section class="panel">
        <div class="row" style="margin-bottom:10px"><h2>이 장비의 최근 측정</h2>
          <a class="ml-auto small" href="#/data?device_id=${id}">목록 →</a></div>
        <div class="col" id="history" style="gap:7px"></div>
      </section>
    </aside>
  </div>`;

  const s = makeSeries();
  let runId = null, run = null, lastRecv = -1, rate = [];
  let gasNameLoaded = false;

  // 입력 UI
  const radioGroup = (sel, fn) => ctx.view.querySelectorAll(`${sel} input`).forEach(inp => inp.addEventListener('change', () => {
    ctx.view.querySelectorAll(`${sel} label`).forEach(l => l.classList.toggle('on', l.querySelector('input').checked));
    fn?.();
  }));
  radioGroup('#kinds', () => { const g = kind() === 'gas'; $('#gasName').disabled = !g; $('#airSel').disabled = !g; });
  radioGroup('#modes', () => { $('#sampleBox').classList.toggle('hidden', mode() !== 'fast'); loadAirs(); });
  const kind = () => ctx.view.querySelector('input[name=kind]:checked').value;
  const mode = () => ctx.view.querySelector('input[name=mode]:checked').value;
  $('#sampleSets').innerHTML = SAMPLE_SETS.map((p, i) => `<div class="inset" style="padding:6px 8px">
      <div class="tiny mute">No.${i + 1}</div>
      <div class="row" style="gap:4px">${p.map((v, k) => `<input type="number" data-s="${i}" data-k="${k}" value="${v}" style="width:100%;padding:3px 5px;font-size:11.5px" aria-label="세트 ${i + 1} ${['HV', 'Frq', 'Duty', 'LFF'][k]}">`).join('')}</div></div>`).join('');

  async function loadAirs() {
    try {
      const airs = await get(`/api/air-list${qs({ device_id: id, mode: mode() })}`);
      $('#airSel').innerHTML = `<option value="">(선택 안 함)</option>` + airs.map(a =>
        `<option value="${a.run_id}">#${a.run_id} ${esc(a.label || '')} · ${fmtShort(a.started_at)} · ${a.stored_heatmaps}HM</option>`).join('');
    } catch { /* DB 없음 */ }
  }
  loadAirs();

  $('#gasSave').addEventListener('click', async () => {
    const name = $('#gasName').value.trim(); if (!name) return;
    try { await put(`/api/pairs/${id}/gas-name`, { gas_name: name }); toast(`가스명 저장: ${name}`, 'ok'); }
    catch (e) { toast(errMsg(e), 'err'); }
  });

  $('#startBtn').addEventListener('click', async () => {
    const body = { device_id: id, kind: kind(), mode: mode(), label: $('#label').value.trim() || null };
    if (body.kind === 'gas') {
      body.target_gas = $('#gasName').value.trim() || null;
      if ($('#conc').value !== '') body.concentration = +$('#conc').value;
      if ($('#airSel').value) body.air_ref_run_id = +$('#airSel').value;
    }
    if (body.mode === 'fast') {
      body.param_sets = SAMPLE_SETS.map((_, i) => {
        const v = [0, 1, 2, 3].map(k => +ctx.view.querySelector(`[data-s="${i}"][data-k="${k}"]`).value);
        return { hv: v[0], frq: v[1], duty: v[2], lff: v[3] };
      });
    }
    try {
      const r = await post('/api/runs', body);
      toast(`측정 run #${r.run_id} 시작 (${MODE_NAME[r.mode]}) — 장비 명령은 미구현`, 'warn', 6000);
      runId = r.run_id; await loadRun(); loadHistory();
    } catch (e) { toast(errMsg(e), 'err', 6000); }
  });
  const runAct = async (act, confirmMsg) => {
    if (!runId) return;
    if (confirmMsg && !confirm(confirmMsg)) return;
    try { const r = await post(`/api/runs/${runId}/${act}`, {}); run = r; paintRun(); loadHistory(); toast(`run #${runId} ${STATUS_NAME[r.status]}`, 'ok'); }
    catch (e) { toast(errMsg(e), 'err', 6000); }
  };
  $('#pauseBtn').addEventListener('click', () => runAct(run?.status === 'paused' ? 'resume' : 'pause'));
  $('#finishBtn').addEventListener('click', () => runAct('finish', '측정을 종료하고 저장할까요? (받은 heatmap 까지만 저장됩니다)'));
  $('#abortBtn').addEventListener('click', () => runAct('abort', '측정을 중단할까요? 재개할 수 없습니다.'));

  async function loadRun() {
    if (!runId) { run = null; paintRun(); return; }
    try { run = await get(`/api/runs/${runId}`); } catch { run = null; }
    paintRun(); loadRecent(true);
  }

  function paintRun() {
    const active = run && (run.status === 'running' || run.status === 'paused');
    const can = canControl();
    $('#startBtn').disabled = !can || !!active;
    $('#pauseBtn').disabled = !can || !active;
    $('#finishBtn').disabled = !can || !active;
    $('#abortBtn').disabled = !can || !active;
    $('#pauseBtn').textContent = run?.status === 'paused' ? '재개' : '일시 정지';
    ctx.view.querySelectorAll('#kinds input, #modes input').forEach(i => { i.disabled = !!active; });
    if (!run) {
      $('#prog').textContent = '—'; $('#pct').textContent = '—'; $('#bar').style.width = '0%'; $('#eta').textContent = '—';
      $('#info').innerHTML = `<div class="small mute">진행 중인 측정이 없습니다</div>`;
      $('#toViewer').href = '#/viewer';
      return;
    }
    const pct = run.expected_heatmaps ? run.received_heatmaps / run.expected_heatmaps * 100 : 0;
    $('#prog').textContent = `${run.received_heatmaps} / ${run.expected_heatmaps}`;
    $('#progLbl').textContent = `${MODE_NAME[run.mode]} 진행률 · ${STATUS_NAME[run.status]}`;
    $('#pct').textContent = `${pct.toFixed(1)}%`;
    $('#bar').style.width = `${pct}%`;
    // 수신 속도로 남은 시간 추정
    const remain = run.expected_heatmaps - run.received_heatmaps;
    let eta = '—';
    if (rate.length >= 2 && run.status === 'running') {
      const [a, b] = [rate[0], rate[rate.length - 1]];
      const per = (b[0] - a[0]) / Math.max(1, b[1] - a[1]);
      if (b[1] > a[1]) { const sec = remain * per; eta = `${Math.floor(sec / 3600)}:${String(Math.floor(sec % 3600 / 60)).padStart(2, '0')}`; }
    } else if (run.status === 'running') eta = `≈${Math.round(remain * 17.4 / 60)}분`;
    $('#eta').textContent = eta;
    $('#toViewer').href = `#/viewer?run=${run.run_id}`;
    $('#info').innerHTML = [
      ['run', `#${run.run_id} ${run.kind === 'air_ref' ? '기준 Air' : '가스'}`],
      ['측정 모드', `${MODE_NAME[run.mode]} (${run.expected_heatmaps} HM)`],
      ['가스', run.target_gas || '—'],
      ['농도', run.concentration !== null ? `${fmt(run.concentration, 2)} V` : '—'],
      ['기준 Air', run.air_ref_run_id ? `#${run.air_ref_run_id} ${run.air_ref_label || ''}` : '—'],
      ['라벨', run.label || '—'],
      ['시작', fmtShort(run.started_at)],
      ['재개 횟수', run.resume_count],
      ['시작한 곳', `${run.control_source} · ${run.requested_by || ''}`],
      ['상태', STATUS_NAME[run.status]],
    ].map(([k, v]) => `<div><span class="k">${k}</span><span class="v">${esc(v)}</span></div>`).join('');
  }

  async function loadRecent(force) {
    if (!run) { $('#recent').innerHTML = `<div class="small mute" style="grid-column:1/-1;padding:30px 0;text-align:center">측정 run 이 없습니다</div>`; return; }
    if (!force && run.received_heatmaps === lastRecv) return;
    lastRecv = run.received_heatmaps;
    if (!run.received_heatmaps) { $('#recent').innerHTML = `<div class="small mute" style="grid-column:1/-1;padding:30px 0;text-align:center">아직 받은 heatmap 이 없습니다</div>`; return; }
    try {
      const rc = await get(`/api/runs/${run.run_id}/recent?n=4`);
      const hm = await get(`/api/runs/${run.run_id}/heatmaps?cond=${rc.cond_idx.join(',')}`);
      const byIdx = Object.fromEntries(hm.heatmaps.map(h => [h.cond_idx, h]));
      $('#recent').innerHTML = rc.cond_idx.map((ci, i) => {
        const h = byIdx[ci];
        const pos = h.sy !== null ? `S[${h.sy},${h.sx}] H[${h.hy},${h.hx}]` : `No.${h.param_no}`;
        return `<div class="hmtile"><div class="hd"><b>LFF ${fmt(h.lff, 0)}Hz · D ${fmt(h.duty, 0)}%</b>
          <span class="chip ${i === rc.cond_idx.length - 1 ? 'warn' : ''}">${i === rc.cond_idx.length - 1 ? '최신' : '#' + ci}</span></div>
          <canvas data-ci="${ci}"></canvas>
          <div class="ax"><span>HV ${fmt(h.hv, 0)} · ${fmt(h.frq, 0)}k</span><span>${pos}</span></div></div>`;
      }).join('');
      ctx.view.querySelectorAll('#recent canvas').forEach(cv => drawHeatmap(cv, byIdx[cv.dataset.ci].values, hm.min, hm.max));
      fitColorbar($('#cb'), $('#recent'));
      $('#cbMax').textContent = fmt(hm.max, 2); $('#cbMin').textContent = fmt(hm.min, 2);
    } catch (e) { /* 조용히 */ }
  }
  drawColorbar($('#cb'));

  async function loadHistory() {
    try {
      const r = await get(`/api/runs${qs({ device_id: id, limit: 6 })}`);
      $('#history').innerHTML = r.rows.length ? r.rows.map(x => `
        <a class="inset row" href="#/viewer?run=${x.run_id}${x.air_ref_run_id ? `&air=${x.air_ref_run_id}` : ''}" style="padding:8px 10px;color:var(--tx);gap:8px">
          <span class="chip ${x.kind === 'air_ref' ? 'gray' : ''}">${x.kind === 'air_ref' ? 'AIR' : 'GAS'}</span>
          <span class="small" style="white-space:nowrap;overflow:hidden;text-overflow:ellipsis">${esc(x.label || x.target_gas || '')}</span>
          <span class="ml-auto tiny mute mono">${x.received_heatmaps}/${x.expected_heatmaps} · ${STATUS_NAME[x.status]}</span></a>`).join('')
        : `<div class="small mute">측정 기록 없음</div>`;
    } catch { $('#history').innerHTML = `<div class="small mute">DB 연결 없음</div>`; }
  }

  let curEmpty = 'AOS Current 수신 없음';
  function draw() {
    lineChart($('#curChart'), { points: s.aos, span: 120, height: 210, emptyText: curEmpty, now: serverNow() });
    lineChart($('#gfcChart'), { points: s.gfc.map(g => [g.t, g.volt1]), span: 600, now: serverNow(), color: '#6FA8DC', height: 130, emptyText: 'GFC 수신 없음' });
  }

  await loadSeries(id, s);
  draw(); loadHistory(); paintRun(); loadRecent(true);
  ctx.cleanups.push(onResize($('#curChart'), draw));

  return {
    async tick(m, d) {
      curEmpty = paintCurState(ctx, d, s);
      if (m.aos?.[id]) s.pushAos(m.aos[id]);
      if (m.gfc?.[id]) s.pushGfc(m.gfc[id]);
      draw();
      if (!gasNameLoaded && document.activeElement !== $('#gasName')) { $('#gasName').value = d.gas_name || ''; gasNameLoaded = true; }
      $('#gasSave').disabled = !canControl();
      const g = d.gfc;
      $('#gNow').textContent = g.online ? `${fmt(g.conc_v, 2)} V` : '—';
      $('#gMode').textContent = !g.known ? '미접속' : g.mode === 'AUTO' ? '자동 제어' : '수동 제어';
      const pr = d.aos.run;           // 서버 캐시 (2초 주기)
      const newId = pr ? pr.run_id : null;
      if (newId !== null && newId !== runId) { runId = newId; rate = []; await loadRun(); loadHistory(); return; }
      if (newId === null && run && (run.status === 'running' || run.status === 'paused')) {
        await loadRun(); loadHistory(); return;          // 다른 곳(콘솔·API)에서 종료됨
      }
      if (pr && run) {
        if (pr.received !== run.received_heatmaps || pr.status !== run.status) {
          run.received_heatmaps = pr.received; run.status = pr.status;
          rate.push([Date.now() / 1000, pr.received]); if (rate.length > 20) rate.shift();
          paintRun(); loadRecent(false);
        }
      }
    },
  };
}
