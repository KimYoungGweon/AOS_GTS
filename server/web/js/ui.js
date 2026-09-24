// 공통 UI 도우미
export const $ = (sel, root = document) => root.querySelector(sel);
export const $$ = (sel, root = document) => [...root.querySelectorAll(sel)];

const ESC = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };
export const esc = (v) => (v === null || v === undefined) ? '' : String(v).replace(/[&<>"']/g, c => ESC[c]);

export function fmt(v, d = 2, dash = '—') {
  if (v === null || v === undefined || Number.isNaN(v)) return dash;
  return Number(v).toFixed(d);
}
export const pad2 = (n) => String(n).padStart(2, '0');

export function fmtTime(t, withSec = true) {           // t: epoch 초 또는 ISO
  if (!t) return '—';
  const d = typeof t === 'number' ? new Date(t * 1000) : new Date(t);
  return `${pad2(d.getHours())}:${pad2(d.getMinutes())}${withSec ? ':' + pad2(d.getSeconds()) : ''}`;
}
export function fmtDateTime(t) {
  if (!t) return '—';
  const d = typeof t === 'number' ? new Date(t * 1000) : new Date(t);
  return `${d.getFullYear()}-${pad2(d.getMonth() + 1)}-${pad2(d.getDate())} ${fmtTime(d.getTime() / 1000)}`;
}
export function fmtShort(t) {
  if (!t) return '—';
  const d = new Date(t);
  return `${pad2(d.getMonth() + 1)}-${pad2(d.getDate())} ${fmtTime(d.getTime() / 1000, false)}`;
}
export function fmtBytes(b) {
  if (b === null || b === undefined) return '—';
  if (b < 1024) return `${b} B`;
  if (b < 1024 ** 2) return `${(b / 1024).toFixed(1)} KB`;
  if (b < 1024 ** 3) return `${(b / 1024 ** 2).toFixed(1)} MB`;
  return `${(b / 1024 ** 3).toFixed(2)} GB`;
}
export function fmtAge(s) {
  if (s === null || s === undefined) return '—';
  if (s < 60) return `${s.toFixed(1)}s`;
  if (s < 3600) return `${Math.floor(s / 60)}m`;
  return `${Math.floor(s / 3600)}h`;
}
export const id2 = (n) => pad2(n);

export function toast(msg, kind = 'info', ms = 3800) {
  const el = document.createElement('div');
  el.className = `toast ${kind}`;
  el.textContent = msg;
  document.getElementById('toasts').appendChild(el);
  setTimeout(() => el.remove(), ms);
}

// 오류를 사람이 읽을 문장으로
export function errMsg(e) {
  if (!e) return '오류';
  if (e.status === 403) return `권한이 없습니다 — ${e.detail}`;
  if (e.code === 'denied_lock') return `제어권 없음 — ${e.detail}`;
  if (e.code === 'denied_run') return e.detail;
  if (e.code === 'offline') return e.detail;
  return e.detail || e.message || String(e);
}

export function modal(html) {
  const bg = document.createElement('div');
  bg.className = 'modal-bg';
  bg.innerHTML = `<div class="modal" role="dialog" aria-modal="true">${html}</div>`;
  document.body.appendChild(bg);
  return { el: bg, close: () => bg.remove() };
}

// 장치 상태 → 표시
export const STATE = {
  run:     { t: '측정중',   dot: 'ok',   fg: 'var(--ok-fg)',   card: 'run' },
  idle:    { t: '대기',     dot: 'idle', fg: 'var(--tx-dim)',  card: '' },
  error:   { t: '장치오류', dot: 'warn', fg: 'var(--warn-fg)', card: 'warn' },
  offline: { t: '통신오류', dot: 'err',  fg: 'var(--err-fg)',  card: 'err' },
  unknown: { t: '미접속',   dot: 'idle', fg: 'var(--tx-mute)', card: 'unknown' },
};
export function devState(d) {
  if (!d || !d.known) return STATE.unknown;
  if (!d.online) return STATE.offline;
  return STATE[d.state] || STATE.idle;
}

export const MODE_NAME = { full8: 'Full', hour1: '1 hour', fast: 'Sample' };
export const MODE_CHIP = { full8: 'warn', hour1: '', fast: 'ok' };
export const STATUS_NAME = { running: '측정중', paused: '일시정지', done: '완료', aborted: '중단', failed: '실패' };

export function debounce(fn, ms = 250) {
  let t; return (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), ms); };
}
