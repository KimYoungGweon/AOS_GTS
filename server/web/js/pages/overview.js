// 1. AOS / GFC 동작 상황표 — WebSocket 1초 갱신
import { live, get } from '../api.js';
import { esc, fmt, fmtTime, fmtAge, id2, devState, MODE_NAME, modal } from '../ui.js';

const LEVEL_DOT = { ok: 'ok', warn: 'warn', error: 'err', ctrl: 'blue', info: 'idle' };

export async function mount(view) {
  view.innerHTML = `
  <div class="page">
    <div class="ov">
      <div style="min-width:0">
        <div class="tiles" id="tiles"></div>
        <div class="pickbar" id="pickbar">
          <span class="small mute">카드 왼쪽 위 네모를 눌러 여러 장비를 고른 뒤 함께 보고 제어할 수 있습니다</span>
          <span class="ml-auto small" id="pickN"></span>
          <button class="btn sm" type="button" id="pickClear">선택 해제</button>
          <button class="btn sm primary" type="button" id="pickGo">선택 장비 상세 보기</button>
        </div>
        <div class="cards" id="cards"></div>
      </div>
      <aside class="side">
        <section class="panel">
          <div class="row" style="margin-bottom:14px"><h2>UDP 수집 서버</h2>
            <span class="ml-auto mono small mute" id="srvAddr"></span></div>
          <div class="stat3" id="srvStat"></div>
          <div class="small mute" id="srvDb" style="margin-top:10px"></div>
        </section>
        <section class="panel">
          <div class="row" style="margin-bottom:12px"><h2>콘솔 접속 현황</h2>
            <span class="ml-auto mono small mute" id="conCount"></span></div>
          <div class="col" id="consoles" style="gap:9px"></div>
        </section>
        <section class="panel" style="display:flex;flex-direction:column;min-height:0">
          <div class="row" style="margin-bottom:12px"><h2>최근 이벤트</h2>
            <button class="btn sm ghost ml-auto" id="allLog" type="button" style="color:var(--accent)">전체 로그</button></div>
          <div class="events" id="events"></div>
        </section>
      </aside>
    </div>
  </div>`;

  const $ = (s) => view.querySelector(s);
  // 여러 장비 선택 (2026-09-23.02) — 카드가 1초마다 다시 그려지므로 선택은 여기에 둔다
  const picked = new Set();
  try { (JSON.parse(sessionStorage.getItem('gts_picked') || '[]')).forEach(i => picked.add(i)); } catch { /* */ }
  const savePick = () => { try { sessionStorage.setItem('gts_picked', JSON.stringify([...picked])); } catch { /* */ } };
  function paintPick() {
    const n = picked.size;
    $('#pickN').innerHTML = n ? `<b style="color:var(--accent)">${n}대 선택</b> · ${[...picked].sort((a, b) => a - b).map(id2).join(', ')}` : '';
    $('#pickGo').disabled = !n; $('#pickClear').disabled = !n;
    view.querySelectorAll('#cards .card').forEach(el => {
      const on = picked.has(+el.dataset.id);
      el.classList.toggle('picked', on);
      const b = el.querySelector('.pick'); if (b) { b.setAttribute('aria-checked', on); }
    });
  }
  $('#cards').addEventListener('click', (e) => {
    const b = e.target.closest('.pick'); if (!b) return;
    e.preventDefault(); e.stopPropagation();
    const i = +b.dataset.id;
    picked.has(i) ? picked.delete(i) : picked.add(i);
    savePick(); paintPick();
  });
  $('#pickClear').addEventListener('click', () => { picked.clear(); savePick(); paintPick(); });
  $('#pickGo').addEventListener('click', () => {
    if (!picked.size) return;
    location.hash = `#/detail/${[...picked].sort((a, b) => a - b).join(',')}`;
  });
  $('#srvAddr').textContent = `${location.hostname}:${location.port || 80}`;
  $('#allLog').addEventListener('click', showAllLog);

  const off = live.on((m) => render(m.overview));
  if (!live.last) {                      // WS 첫 tick 전에 한 번 그려 둔다
    try { render(await get('/api/overview')); } catch { /* app 이 처리 */ }
  }

  function render(o) {
    if (!o) return;
    const c = o.counts;
    const tiles = [
      ['측정중', c.measuring, '대', 'var(--ok)'],
      ['대기', c.idle, '대', 'var(--tx-dim)'],
      ['장치오류', c.error, '대', 'var(--warn)'],
      ['통신오류', c.offline, '대', 'var(--err)'],
      ['자동 측정 진행', c.auto_running, 'run', 'var(--accent)'],
    ];
    $('#tiles').innerHTML = tiles.map(([l, v, u, col]) => `
      <div class="tile"><div class="t"><span class="dot" style="background:${col}"></span>${l}</div>
        <div class="v"><b style="color:${col}">${v}</b><span>${u}</span></div></div>`).join('');

    $('#cards').innerHTML = o.pairs.map(card).join('');
    paintPick();

    const s = o.server;
    const up = s.uptime_s >= 3600 ? [(s.uptime_s / 3600).toFixed(1), 'h'] : [Math.round(s.uptime_s / 60), 'min'];
    $('#srvStat').innerHTML = `
      <div><div class="k">수신 속도</div><div class="v">${s.pkt_s}<small> pkt/s</small></div></div>
      <div><div class="k">손실률</div><div class="v">${s.loss_pct}<small> %</small></div></div>
      <div><div class="k">가동</div><div class="v">${up[0]}<small> ${up[1]}</small></div></div>`;
    $('#srvDb').innerHTML = s.db.connected
      ? `DB 정상 · 대기열 ${s.db.queue} · UDP ${s.ports.aos}/${s.ports.gfc}/${s.ports.console}`
      : `<span style="color:var(--err-fg)">DB 연결 끊김</span> — 실시간 화면만 동작`;

    const cons = o.consoles;
    $('#conCount').textContent = `${cons.filter(x => x.alive).length} / ${cons.length} online`;
    $('#consoles').innerHTML = cons.length ? cons.map(cn => {
      const st = cn.alive ? (cn.connected ? ['연결됨', 'ok', 'var(--ok-fg)', 'var(--ok-line)'] : ['대기', 'warn', 'var(--warn-fg)', 'var(--warn-line)'])
                          : ['끊김', 'err', 'var(--err-fg)', 'var(--err-line)'];
      return `<div class="cons" style="border-color:${st[3]}">
        <div class="row" style="gap:8px"><span class="dot ${st[1]}"></span>
          <span class="mono" style="font-size:12.5px;font-weight:600">${esc(cn.name)}</span>
          <span class="small" style="color:${st[2]};font-weight:600">${st[0]}</span>
          <span class="ml-auto mono small mute">${esc(cn.ip)}</span></div>
        <div class="row small mute">담당 <span class="mono" style="color:var(--tx-3)">${cn.target_type} ${id2(cn.target_id)}</span>
          <span class="ml-auto">수신 <span class="mono" style="color:var(--tx-3)">${cn.pkt_s} pkt/s</span></span></div>
      </div>`;
    }).join('') : `<div class="small mute">접속한 콘솔 없음</div>`;

    $('#events').innerHTML = o.events.length ? o.events.map(e => `
      <div class="ev"><span class="dot ${LEVEL_DOT[e.level] || 'idle'}"></span>
        <div style="min-width:0"><div class="tx">${esc(e.text)}</div><div class="tm">${fmtTime(e.at)}</div></div></div>`).join('')
      : `<div class="small mute">이벤트 없음</div>`;
  }

  return { cleanup: off };
}

function card(p) {
  const a = p.aos, g = p.gfc;
  const st = devState(a);
  const auto = a.mode === 'AUTO';
  let metricLabel = '', metric = '—';
  if (auto && a.run) {
    metricLabel = a.run.status === 'paused' ? '일시정지' : MODE_NAME[a.run.mode] || '스캔';
    metric = `${a.run.received}/${a.run.expected}`;
  } else if (a.online) {
    metricLabel = '평균전류'; metric = a.current_avg_v !== null ? `${fmt(a.current_avg_v, 2)} V` : (a.online && a.cur && !a.cur.valid ? '미수신' : '—');
  }
  const pr = a.params;
  const par = pr ? `HV ${fmt(pr.hv, 0)}V · ${fmt(pr.frq, 0)}kHz · D${fmt(pr.duty, 0)}% · CV ${fmt(pr.cv, 2)}`
                 : (a.online ? '파라미터 미수신' : '');
  const conc = g.online ? g.conc_v : null;
  const pct = conc !== null && conc !== undefined ? Math.max(0, Math.min(100, conc / 5 * 100)) : 0;
  const gst = devState(g);
  const gfcMode = !g.known ? '—' : (g.mode === 'AUTO' ? '자동' : '수동');
  const pumpOn = g.pump && g.pump[1];
  const rx = Math.min(a.last_rx_s ?? 1e9, g.last_rx_s ?? 1e9);
  return `
  <a class="card ${st.card}" data-id="${p.id}" href="#/detail/${p.id}${auto ? '/auto' : ''}" aria-label="AOS ${id2(p.id)} 상세 보기">
    <div class="hd"><span class="pick" role="checkbox" aria-checked="false" data-id="${p.id}" title="여러 장비 선택" aria-label="AOS ${id2(p.id)} 선택"></span>
      <span class="dot ${st.dot}"></span><span class="id">AOS ${id2(p.id)}</span>
      ${a.known ? `<span class="chip ${auto ? '' : 'warn'}" style="margin-left:auto">${auto ? 'AUTO' : 'MANUAL'}</span>` : ''}</div>
    <div class="ln"><span class="k">AOS</span><span class="st" style="color:${st.fg}">${st.t}</span>
      <span class="ml">${metricLabel}</span><span class="big">${esc(metric)}</span></div>
    <div class="par">${esc(par)}</div>
    <div class="sep"></div>
    <div class="ln"><span class="k">GFC</span><span class="gas">${esc(p.gas_name || '—')}</span>
      <span class="big" style="margin-left:auto;font-size:14px">${conc !== null && conc !== undefined ? fmt(conc, 2) + ' V' : '—'}</span></div>
    <div class="bar"><i style="width:${pct}%"></i></div>
    <div class="ln small mute" style="font-size:10.5px"><span style="color:${gst.fg}">${g.known ? gst.t : 'GFC 미접속'}</span>
      <span>· 제어 ${gfcMode}${pumpOn ? ' · 펌프 ON' : ''}</span>
      <span class="mono" style="margin-left:auto">${rx < 1e8 ? fmtAge(rx) : '—'}</span></div>
  </a>`;
}

async function showAllLog() {
  const m = modal(`<div class="row"><h2>전체 이벤트 로그</h2><button class="btn sm ml-auto" type="button" id="x">닫기</button></div>
    <div class="events" id="all" style="max-height:60vh;margin-top:14px"><div class="small mute">불러오는 중…</div></div>`);
  m.el.querySelector('.modal').style.width = 'min(720px,100%)';
  m.el.querySelector('#x').onclick = m.close;
  m.el.addEventListener('click', (e) => { if (e.target === m.el) m.close(); });
  try {
    const ev = await get('/api/events?limit=300');
    m.el.querySelector('#all').innerHTML = ev.map(e => `
      <div class="ev"><span class="dot ${LEVEL_DOT[e.level] || 'idle'}"></span>
      <div><div class="tx">${esc(e.text)}</div><div class="tm">${new Date(e.at * 1000).toLocaleString('ko-KR')}</div></div></div>`).join('')
      || '<div class="small mute">없음</div>';
  } catch (e) { m.el.querySelector('#all').textContent = e.message; }
}
