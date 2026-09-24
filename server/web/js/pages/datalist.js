// 4. DB 저장 데이터 목록 — Air(Ref) List · Gas Data List · 미리보기
import { get, patch, qs } from '../api.js';
import { canControl } from '../state.js';
import { esc, fmt, fmtBytes, fmtShort, id2, toast, errMsg, MODE_NAME, MODE_CHIP, STATUS_NAME, debounce } from '../ui.js';
import { drawHeatmap } from '../charts.js';

const PAGE = 12;

export async function mount(view, r) {
  const f = {
    date_from: r.query.date_from || '', date_to: r.query.date_to || '', gas: r.query.gas || '',
    device_id: r.query.device_id || '', mode: r.query.mode || '', q: r.query.q || '',
    origin: r.query.origin ?? '',
  };
  const pg = { air: 0, gas: 0 };
  let sel = null;

  view.innerHTML = `
  <div class="subbar">
    <div class="row" style="gap:18px">
      <div><div class="tiny mute">DB 총 용량</div><div class="num" style="font-size:16px" id="dbSize">—</div></div>
      <div><div class="tiny mute">측정 run</div><div class="num" style="font-size:16px" id="dbRuns">—</div></div>
      <div><div class="tiny mute">heatmap</div><div class="num" style="font-size:16px" id="dbHm">—</div></div>
    </div>
    <div class="filters ml-auto">
      <label class="label" for="fFrom">기간</label>
      <input type="date" id="fFrom"><span class="mute">–</span><input type="date" id="fTo">
      <label class="label" for="fGas">가스</label><select id="fGas"><option value="">전체</option></select>
      <label class="label" for="fAos">AOS ID</label>
      <select id="fAos"><option value="">전체</option>${Array.from({ length: 20 }, (_, i) => `<option value="${i + 1}">AOS-${id2(i + 1)}</option>`).join('')}</select>
      <label class="label" for="fMode">모드</label>
      <select id="fMode"><option value="">전체</option><option value="full8">Full</option><option value="hour1">1 hour</option><option value="fast">Sample</option></select>
      <label class="label" for="fOrigin">출처</label>
      <select id="fOrigin"><option value="">전체</option><option value="measured">실측</option><option value="legacy_import">임포트(개발 전 샘플)</option></select>
      <input type="text" id="fQ" placeholder="파일명·라벨·메모 검색" style="width:200px">
      <button class="btn" id="fReset" type="button">초기화</button>
    </div>
  </div>
  <div class="page">
    <div class="dl">
      <div class="col" style="gap:18px">
        <section class="panel">
          <div class="row" style="margin-bottom:10px"><h2>Air (Ref) Data List</h2><span class="small mute">기준 데이터 <b id="airTotal">0</b> 건</span></div>
          <div class="tbl-wrap"><table class="t" id="airTbl"><thead><tr>
            <th>#</th><th>파일명 / 라벨</th><th>AOS</th><th>측정 일시</th><th>모드</th><th>HM</th><th>크기</th><th>상태</th></tr></thead><tbody></tbody></table></div>
          <div class="pager" id="airPg"></div>
        </section>
        <section class="panel">
          <div class="row" style="margin-bottom:10px"><h2>Gas Data List</h2><span class="small mute">측정 데이터 <b id="gasTotal">0</b> 건</span></div>
          <div class="tbl-wrap"><table class="t" id="gasTbl"><thead><tr>
            <th>#</th><th>파일명 / 라벨</th><th>가스</th><th>농도</th><th>AOS</th><th>측정 일시</th><th>모드</th><th>HM</th><th>크기</th><th>Ref 연결</th></tr></thead><tbody></tbody></table></div>
          <div class="pager" id="gasPg"></div>
        </section>
      </div>
      <aside class="panel preview" id="preview"><div class="empty-state">목록에서 데이터를 고르면<br>여기에 미리보기가 나옵니다</div></aside>
    </div>
  </div>`;

  const $ = (s) => view.querySelector(s);
  $('#fFrom').value = f.date_from; $('#fTo').value = f.date_to; $('#fAos').value = f.device_id;
  $('#fMode').value = f.mode; $('#fQ').value = f.q; $('#fOrigin').value = f.origin;

  const reload = () => { pg.air = 0; pg.gas = 0; loadAir(); loadGas(); };
  const bind = (id, key) => $(id).addEventListener('change', (e) => { f[key] = e.target.value; reload(); });
  bind('#fFrom', 'date_from'); bind('#fTo', 'date_to'); bind('#fGas', 'gas'); bind('#fAos', 'device_id');
  bind('#fMode', 'mode'); bind('#fOrigin', 'origin');
  $('#fQ').addEventListener('input', debounce((e) => { f.q = e.target.value.trim(); reload(); }, 300));
  $('#fReset').addEventListener('click', () => {
    Object.keys(f).forEach(k => { f[k] = ''; });
    ['#fFrom', '#fTo', '#fGas', '#fAos', '#fMode', '#fQ', '#fOrigin'].forEach(q => { $(q).value = ''; });
    reload();
  });

  try {
    const [st, gases] = await Promise.all([get('/api/db/stats'), get('/api/gases')]);
    $('#dbSize').textContent = fmtBytes(st.db_bytes);
    $('#dbRuns').textContent = st.runs.reduce((a, x) => a + x.count, 0).toLocaleString();
    $('#dbHm').textContent = st.heatmaps.toLocaleString();
    $('#fGas').innerHTML = `<option value="">전체</option>` + gases.map(g => `<option value="${esc(g.target_gas)}">${esc(g.target_gas)}</option>`).join('');
    $('#fGas').value = f.gas;
  } catch (e) { toast(errMsg(e), 'err'); }

  const base = () => ({ ...f, limit: PAGE });
  const nameOf = (x) => x.source_file || x.label || `run_${x.run_id}`;
  const originChip = (x) => x.data_origin === 'legacy_import' ? ' <span class="chip gray" title="개발 전 샘플 — 실측 데이터와 비교하지 않음">임포트</span>' : '';

  async function loadAir() {
    try {
      const d = await get(`/api/runs${qs({ ...base(), kind: 'air_ref', gas: '', offset: pg.air * PAGE })}`);
      $('#airTotal').textContent = d.total.toLocaleString();
      $('#airTbl tbody').innerHTML = d.rows.map(x => `
        <tr data-id="${x.run_id}" class="${sel?.run_id === x.run_id ? 'sel' : ''}">
          <td class="n mute">${x.run_id}</td>
          <td class="wrap">${esc(nameOf(x))}${originChip(x)}</td>
          <td class="n">AOS-${id2(x.device_id)}</td><td class="n">${fmtShort(x.started_at)}</td>
          <td><span class="chip ${MODE_CHIP[x.mode]}">${MODE_NAME[x.mode]}</span></td>
          <td class="n">${x.received_heatmaps}</td><td class="n">${fmtBytes(x.approx_bytes)}</td>
          <td class="small">${STATUS_NAME[x.status]}</td></tr>`).join('') || `<tr><td colspan="8" class="mute">없음</td></tr>`;
      pager('#airPg', d.total, 'air', loadAir);
      rowsClick('#airTbl', d.rows);
    } catch (e) { toast(errMsg(e), 'err'); }
  }
  async function loadGas() {
    try {
      const d = await get(`/api/runs${qs({ ...base(), kind: 'gas', offset: pg.gas * PAGE })}`);
      $('#gasTotal').textContent = d.total.toLocaleString();
      $('#gasTbl tbody').innerHTML = d.rows.map(x => `
        <tr data-id="${x.run_id}" class="${sel?.run_id === x.run_id ? 'sel' : ''}">
          <td class="n mute">${x.run_id}</td>
          <td class="wrap">${esc(nameOf(x))}${originChip(x)}</td>
          <td>${esc(x.target_gas || '—')}</td><td class="n">${x.concentration !== null ? fmt(x.concentration, 2) : '—'}</td>
          <td class="n">AOS-${id2(x.device_id)}</td><td class="n">${fmtShort(x.started_at)}</td>
          <td><span class="chip ${MODE_CHIP[x.mode]}">${MODE_NAME[x.mode]}</span></td>
          <td class="n">${x.received_heatmaps}</td><td class="n">${fmtBytes(x.approx_bytes)}</td>
          <td class="n small">${x.air_ref_run_id ? `#${x.air_ref_run_id}` : '—'}</td></tr>`).join('') || `<tr><td colspan="10" class="mute">없음</td></tr>`;
      pager('#gasPg', d.total, 'gas', loadGas);
      rowsClick('#gasTbl', d.rows);
    } catch (e) { toast(errMsg(e), 'err'); }
  }
  function pager(q, total, key, fn) {
    const pages = Math.max(1, Math.ceil(total / PAGE));
    const a = total ? pg[key] * PAGE + 1 : 0, b = Math.min(total, (pg[key] + 1) * PAGE);
    $(q).innerHTML = `<span class="mono">${a} – ${b} / ${total.toLocaleString()}</span>
      <button class="btn sm" type="button" data-d="-1" ${pg[key] <= 0 ? 'disabled' : ''}>이전</button>
      <button class="btn sm" type="button" data-d="1" ${pg[key] >= pages - 1 ? 'disabled' : ''}>다음</button>`;
    $(q).querySelectorAll('button').forEach(bn => bn.addEventListener('click', () => { pg[key] += +bn.dataset.d; fn(); }));
  }
  function rowsClick(tbl, rows) {
    $(tbl).querySelectorAll('tbody tr[data-id]').forEach(tr => tr.addEventListener('click', () => {
      view.querySelectorAll('table.t tr.sel').forEach(x => x.classList.remove('sel'));
      tr.classList.add('sel');
      showPreview(rows.find(x => x.run_id === +tr.dataset.id));
    }));
  }

  async function showPreview(x) {
    sel = x;
    const viewerHref = `#/viewer?run=${x.run_id}${x.kind === 'gas' && x.air_ref_run_id ? `&air=${x.air_ref_run_id}&value=idf` : ''}`;
    $('#preview').innerHTML = `
      <div class="row" style="margin-bottom:10px"><h2>선택 데이터 미리보기</h2></div>
      <div class="mono" style="font-size:13px;word-break:break-all;margin-bottom:10px">${esc(nameOf(x))}</div>
      <div class="infogrid" style="margin-bottom:12px">
        <div><span class="k">종류</span><span class="v">${x.kind === 'air_ref' ? '기준 Air (Ref)' : '가스 (Target)'}${x.data_origin === 'legacy_import' ? ' · 임포트' : ''}</span></div>
        <div><span class="k">가스</span><span class="v">${esc(x.target_gas || '—')}</span></div>
        <div><span class="k">농도</span><span class="v">${x.concentration !== null ? fmt(x.concentration, 2) + ' V' : '—'}</span></div>
        <div><span class="k">AOS</span><span class="v">AOS-${id2(x.device_id)}</span></div>
        <div><span class="k">모드</span><span class="v">${MODE_NAME[x.mode]} · ${x.received_heatmaps}/${x.expected_heatmaps} HM</span></div>
        <div><span class="k">측정 일시</span><span class="v">${fmtShort(x.started_at)}</span></div>
        <div><span class="k">Ref 연결</span><span class="v">${x.air_ref_run_id ? `#${x.air_ref_run_id} ${esc(x.air_ref_label || '')}` : '—'}</span></div>
        <div><span class="k">상태</span><span class="v">${STATUS_NAME[x.status]}</span></div>
      </div>
      <div class="row" style="margin-bottom:12px;gap:8px">
        <a class="btn primary grow" href="${viewerHref}">Viewer 열기</a>
        <button class="btn" type="button" id="editBtn" ${canControl() ? '' : 'disabled'}>정보 수정</button>
      </div>
      <form id="editForm" class="col hidden" style="gap:8px;margin-bottom:12px">
        <label class="small mute">라벨 <input type="text" id="eLabel" style="width:100%" value="${esc(x.label || '')}"></label>
        ${x.kind === 'gas' ? `<label class="small mute">가스명 <input type="text" id="eGas" style="width:100%" value="${esc(x.target_gas || '')}"></label>
        <label class="small mute">농도 (V) <input type="number" step="0.01" id="eConc" style="width:100%" value="${x.concentration ?? ''}"></label>` : ''}
        <label class="small mute">메모 <textarea id="eCmt" rows="2" style="width:100%">${esc(x.comment || '')}</textarea></label>
        <button class="btn primary" type="submit">저장</button>
      </form>
      <div class="small mute" style="margin-bottom:6px" id="thumbCap">불러오는 중…</div>
      <div class="thumbs" id="thumbs"></div>
      ${x.comment ? `<div class="small mute" style="margin-top:10px;line-height:1.5">메모: ${esc(x.comment)}</div>` : ''}`;

    $('#editBtn').addEventListener('click', () => $('#editForm').classList.toggle('hidden'));
    $('#editForm').addEventListener('submit', async (e) => {
      e.preventDefault();
      const body = { label: $('#eLabel').value.trim(), comment: $('#eCmt').value.trim() };
      if (x.kind === 'gas') {
        body.target_gas = $('#eGas').value.trim();
        if ($('#eConc').value !== '') body.concentration = +$('#eConc').value;
      }
      try { const nx = await patch(`/api/runs/${x.run_id}`, body); toast('저장했습니다', 'ok'); Object.assign(x, nx); loadAir(); loadGas(); showPreview(x); }
      catch (err) { toast(errMsg(err), 'err'); }
    });

    // 썸네일 8장 — Full/1hour 는 대표값이 가장 큰 섹션의 앞 8장, Sample 은 전체
    try {
      let sy = null, sx = null;
      if (x.mode !== 'fast' && x.received_heatmaps) {
        const ov = await get(`/api/runs/${x.run_id}/overview`);
        const best = (ov.sections || []).filter(s => s.count).sort((a, b) => (b.best ?? -1e9) - (a.best ?? -1e9))[0];
        if (best) { sy = best.sy; sx = best.sx; }
      }
      if (!x.received_heatmaps) { $('#thumbCap').textContent = '저장된 heatmap 없음'; return; }
      const hm = await get(`/api/runs/${x.run_id}/heatmaps${qs({ sy, sx })}`);
      const list = hm.heatmaps.slice(0, 8);
      $('#thumbCap').textContent = x.mode === 'fast' ? `Sample ${list.length}장 · 측정값 · Jet · 누르면 Viewer`
        : `섹션 [${sy},${sx}] (HV ${fmt(list[0]?.hv, 0)} · ${fmt(list[0]?.frq, 0)} kHz) 16장 중 8장 · 측정값 · 누르면 Viewer`;
      $('#thumbs').innerHTML = list.map((h, i) => `<canvas data-i="${i}" title="Duty ${fmt(h.duty, 0)} · LFF ${fmt(h.lff, 0)}"></canvas>`).join('');
      view.querySelectorAll('#thumbs canvas').forEach(cv => {
        drawHeatmap(cv, list[cv.dataset.i].values, hm.min, hm.max);
        cv.addEventListener('click', () => { location.hash = `${viewerHref}${sy !== null ? `&sy=${sy}&sx=${sx}` : ''}`; });
      });
    } catch (e) { $('#thumbCap').textContent = errMsg(e); }
  }

  loadAir(); loadGas();
  return {};
}
