// 앱 셸 — 로그인(토큰) · 해시 라우팅 · 상단바 상태 · WebSocket
import { getToken, setToken, live } from './api.js';
import { $, $$, esc, modal, fmtDateTime } from './ui.js';
import * as overview from './pages/overview.js';
import * as detail from './pages/detail.js';
import * as viewer from './pages/viewer.js';
import * as datalist from './pages/datalist.js';

import { session } from './state.js';

const ROUTES = { overview, detail, viewer, data: datalist };
let current = null;          // { name, cleanup }

export function go(hash) { if (location.hash !== hash) location.hash = hash; else route(); }

function parseHash() {
  const h = location.hash.replace(/^#\/?/, '');
  const [path, query = ''] = h.split('?');
  const parts = path.split('/').filter(Boolean);
  return { name: parts[0] || 'overview', args: parts.slice(1), query: Object.fromEntries(new URLSearchParams(query)) };
}

async function route() {
  const r = parseHash();
  const page = ROUTES[r.name] || overview;
  const name = ROUTES[r.name] ? r.name : 'overview';
  $$('#nav a').forEach(a => a.classList.toggle('on', a.dataset.r === name));
  // 같은 페이지 안의 파라미터 변경은 페이지가 직접 처리 (예: viewer 섹션 이동)
  if (current && current.name === name && current.update && current.update(r) !== false) return;
  try { current?.cleanup?.(); } catch (e) { console.error(e); }
  const view = $('#view');
  view.innerHTML = '';
  const res = await page.mount(view, r) || {};
  current = { name, cleanup: res.cleanup, update: res.update };
}

// ── 로그인 ─────────────────────────────────────────────────────────
async function verify(token) {
  const r = await fetch('/api/me', { headers: { Authorization: `Bearer ${token}` } });
  if (r.status === 401) { const e = new Error('401'); e.status = 401; throw e; }
  if (!r.ok) throw new Error(`HTTP ${r.status}`);
  return r.json();
}

function loginDialog(reason = '') {
  return new Promise(resolve => {
    const m = modal(`
      <h2>GTS 접속</h2>
      <p>관리자에게 받은 <b style="color:var(--tx-2)">접속 토큰</b>(gts_로 시작)을 붙여 넣으세요.
      이 브라우저에 저장되어 다음부터는 묻지 않습니다.<br>
      <span class="mute">read = 조회만 · control = 장비 제어 · admin = 전체</span></p>
      <form id="loginForm" class="col">
        <label class="label" for="tok">토큰</label>
        <input id="tok" type="password" autocomplete="off" spellcheck="false" placeholder="gts_…" style="width:100%">
        <div class="err" id="loginErr">${esc(reason)}</div>
        <button class="btn primary" type="submit" style="height:42px">접속</button>
      </form>`);
    const inp = $('#tok', m.el); inp.focus();
    $('#loginForm', m.el).addEventListener('submit', async (e) => {
      e.preventDefault();
      const t = inp.value.trim().replace(/^Bearer\s+/i, '');
      if (!t) return;
      try {
        const me = await fetch('/api/me', { headers: { Authorization: `Bearer ${t}` } });
        if (!me.ok) throw new Error();
        setToken(t); m.close(); resolve(await me.json());
      } catch {
        $('#loginErr', m.el).textContent = '토큰이 맞지 않거나 만료되었습니다.';
      }
    });
  });
}

async function ensureLogin() {
  const t = getToken();
  if (t) {
    try { return await verify(t); } catch (e) { if (e.status !== 401) throw e; }
  }
  return await loginDialog(t ? '저장된 토큰이 더 이상 유효하지 않습니다.' : '');
}

function setWho(me) {
  Object.assign(session, me);
  const sc = { read: '조회', control: '제어', admin: '관리자' }[me.scope] || me.scope;
  $('#whoText').innerHTML = `<span class="mono">${esc(me.login)}</span> <span class="chip ${me.scope === 'read' ? 'gray' : 'warn'}">${sc}</span>`;
}

// ── 상단바: 연결 상태 · 시계 ─────────────────────────────────────────
function wireTopbar() {
  const tick = () => { $('#clock').textContent = fmtDateTime(Date.now() / 1000); };
  tick(); setInterval(tick, 1000);
  const upd = () => {
    const pill = $('#connPill'), dot = $('#connDot');
    const m = live.last;
    if (!live.connected) {
      pill.className = 'pill bad'; dot.className = 'dot err';
      $('#connText').textContent = '서버 연결 끊김'; $('#connSub').textContent = '재접속 중';
      return;
    }
    const s = m?.overview?.server;
    if (s && !s.db.connected) {
      pill.className = 'pill warn'; dot.className = 'dot warn';
      $('#connText').textContent = 'DB 연결 끊김'; $('#connSub').textContent = `${s.pkt_s} pkt/s`;
      return;
    }
    pill.className = 'pill'; dot.className = 'dot ok';
    $('#connText').textContent = 'UDP 서버 정상';
    $('#connSub').textContent = s ? `${s.pkt_s} pkt/s` : '';
  };
  live.onStatus(upd); live.on(upd);
  $('#logoutBtn').addEventListener('click', () => {
    setToken(''); live.stop(); location.hash = '#/overview'; location.reload();
  });
}

window.addEventListener('gts:unauthorized', async () => {
  if (document.querySelector('.modal-bg')) return;
  live.stop();
  const me = await loginDialog('토큰이 거부되었습니다. 다시 입력하세요.');
  setWho(me); live.start(); route();
});

(async function boot() {
  wireTopbar();
  let me;
  try { me = await ensureLogin(); }
  catch (e) {
    $('#view').innerHTML = `<div class="empty-state">서버에 연결할 수 없습니다.<br><span class="mono">${esc(e.message)}</span></div>`;
    return;
  }
  setWho(me);
  live.start();
  window.addEventListener('hashchange', route);
  route();
})();
