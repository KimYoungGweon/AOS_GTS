// API · 토큰 · WebSocket — 서버와 같은 origin(:8081)에서 서빙된다
const KEY = 'gts_token';

export function getToken() {
  try { return localStorage.getItem(KEY) || ''; } catch { return ''; }
}
export function setToken(t) {
  try { t ? localStorage.setItem(KEY, t) : localStorage.removeItem(KEY); } catch { /* 사생활 보호 모드 */ }
}

export class ApiError extends Error {
  constructor(status, code, detail) {
    super(detail || code || `HTTP ${status}`);
    this.status = status; this.code = code; this.detail = detail;
  }
}

function errText(body) {
  if (!body) return '';
  if (typeof body.detail === 'string') return body.detail;
  if (Array.isArray(body.detail)) {         // FastAPI 검증 오류
    return body.detail.map(d => `${(d.loc || []).slice(-1)[0]}: ${d.msg}`).join(', ');
  }
  return JSON.stringify(body);
}

export async function api(path, { method = 'GET', body, token } = {}) {
  const headers = { Authorization: `Bearer ${token ?? getToken()}` };
  if (body !== undefined) headers['Content-Type'] = 'application/json';
  let r;
  try {
    r = await fetch(path, { method, headers, body: body !== undefined ? JSON.stringify(body) : undefined });
  } catch (e) {
    throw new ApiError(0, 'network', '서버에 연결할 수 없습니다');
  }
  const ct = r.headers.get('content-type') || '';
  const data = ct.includes('json') ? await r.json().catch(() => null) : await r.text();
  if (!r.ok) {
    const e = new ApiError(r.status, data?.error, errText(data) || `HTTP ${r.status}`);
    if (r.status === 401) window.dispatchEvent(new CustomEvent('gts:unauthorized'));
    throw e;
  }
  return data;
}

export const get = (p) => api(p);
export const post = (p, body = {}) => api(p, { method: 'POST', body });
export const put = (p, body = {}) => api(p, { method: 'PUT', body });
export const patch = (p, body = {}) => api(p, { method: 'PATCH', body });
export const del = (p) => api(p, { method: 'DELETE' });

export function qs(obj) {
  const u = new URLSearchParams();
  for (const [k, v] of Object.entries(obj)) if (v !== undefined && v !== null && v !== '') u.set(k, v);
  const s = u.toString();
  return s ? `?${s}` : '';
}

// ── 실시간: WebSocket 1초 tick.  WebSocket 이 안 되면(프록시·서버 라이브러리 없음 등)
//    1초 폴링(REST)으로 자동 전환해 같은 모양의 메시지를 만든다 ─────────────────────
export class Live {
  constructor() {
    this.ws = null; this.listeners = new Set(); this.statusListeners = new Set();
    this.detail = null; this.retry = 0; this.closed = false; this.last = null; this.connected = false;
    this.mode = 'ws';           // 'ws' | 'poll'
    this.wsFails = 0; this.pollTimer = null; this.lastA = {}; this.skew = 0;
  }
  start() {
    this.closed = false;
    if (this.mode === 'poll') { this._poll(); return; }
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    const url = `${proto}://${location.host}/ws/live?token=${encodeURIComponent(getToken())}`;
    let opened = false;
    let ws;
    try { ws = this.ws = new WebSocket(url); } catch { this._fallback(); return; }
    ws.onopen = () => {
      opened = true; this.retry = 0; this.wsFails = 0; this._status(true);
      if (this.detail) ws.send(JSON.stringify({ detail: this.detail }));
    };
    ws.onmessage = (ev) => {
      let m; try { m = JSON.parse(ev.data); } catch { return; }
      this._emit(m);
    };
    ws.onclose = (ev) => {
      if (ev.code === 4401) { this._status(false); window.dispatchEvent(new CustomEvent('gts:unauthorized')); return; }
      if (this.closed) return;
      if (!opened && ++this.wsFails >= 2) { this._fallback(); return; }   // 연결 자체가 안 됨 → 폴링
      this._status(false);
      const wait = Math.min(10000, 1000 * 2 ** this.retry++);
      setTimeout(() => !this.closed && this.start(), wait);
    };
  }
  _fallback() {
    console.warn('WebSocket 사용 불가 — 1초 폴링으로 전환');
    this.mode = 'poll';
    this._poll();
    // 서버가 고쳐졌을 수 있으니 1분마다 WebSocket 을 다시 시도
    setTimeout(() => { if (!this.closed && this.mode === 'poll') { this.mode = 'ws'; this.wsFails = 0; clearTimeout(this.pollTimer); this.start(); } }, 60000);
  }
  async _poll() {
    if (this.closed || this.mode !== 'poll') return;
    try {
      const ov = await api('/api/overview');
      const m = { type: 'tick', overview: ov, aos: {}, gfc: {}, polled: true };
      const id = this.detail;
      if (id) {
        const [d, cur] = await Promise.all([api(`/api/pairs/${id}`), api(`/api/aos/${id}/current?sec=5`)]);
        m.detail = d;
        const p = cur.points[cur.points.length - 1];
        if (p) m.aos[id] = p;
        const g = d.gfc_recent?.[d.gfc_recent.length - 1];
        if (g) m.gfc[id] = g;
      }
      this._status(true);
      this._emit(m);
    } catch (e) {
      this._status(false);
      if (e.status === 401) return;
    }
    this.pollTimer = setTimeout(() => this._poll(), 1000);
  }
  _emit(m) {
    this.last = m;
    // 서버 시계 기준으로 그래프를 그린다 — PC 시계가 서버와 어긋나면 점이 표시 구간 밖으로 밀려 "수신 없음" 이 된다
    if (m.overview?.t) this.skew = m.overview.t - Date.now() / 1000;
    for (const fn of this.listeners) { try { fn(m); } catch (e) { console.error(e); } }
  }
  stop() { this.closed = true; clearTimeout(this.pollTimer); try { this.ws?.close(); } catch { /* */ } }
  on(fn) { this.listeners.add(fn); if (this.last) fn(this.last); return () => this.listeners.delete(fn); }
  onStatus(fn) { this.statusListeners.add(fn); fn(this.connected); return () => this.statusListeners.delete(fn); }
  setDetail(id) {
    this.detail = id;
    if (this.ws?.readyState === 1) this.ws.send(JSON.stringify({ detail: id }));
  }
  _status(ok) { this.connected = ok; for (const fn of this.statusListeners) fn(ok); }
}

export const live = new Live();
export const serverNow = () => Date.now() / 1000 + (live.skew || 0);
