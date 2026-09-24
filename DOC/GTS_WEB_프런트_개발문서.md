# GTS 웹 프런트 개발문서 — 2026-09-23

> 대상: `server/web/` — `gts_server.py` 가 `http://<서버>:8081/` 로 그대로 서빙한다
> 근거: 웹 디자인 캔버스 5화면, `DOC/GTS_Web_Design.md`(변경사항 포함), `DOC/GTS_DB_API_개발문서.md`

---

## 0. 요약

| 항목 | 결정 |
|---|---|
| 방식 | **빌드 없는 HTML/JS** (ES 모듈). Node·npm 불필요, `gts-push` 로 함께 배포 |
| 로그인 | 첫 접속 시 **토큰 입력 → 브라우저에 저장**. 권한(read/control/admin)에 따라 제어 버튼 자동 잠금 |
| 화면 | 1920×1080 기준(디자인 그대로) + 노트북(1366~1600) 대응, 휴대폰은 조회 수준 |
| 실시간 | WebSocket `/ws/live` 1초. 끊기면 자동 재접속, WebSocket 이 안 되면 1초 REST 폴링으로 자동 전환 |
| 그래프 | 캔버스 (선 그래프 · heatmap · Jet/Turbo/Viridis/Twin 컬러맵). Stacked 3D 만 로컬 `vendor/plotly-gl3d.min.js` (필요할 때만 로드) |
| 캐시 | 정적 파일 `Cache-Control: no-cache` — 배포 후 새로고침만 하면 새 버전 |

## 1. 파일

| 파일 | 역할 |
|---|---|
| `web/index.html` | 셸 (상단바 · 탭 · 연결 상태 · 시계 · 로그아웃) |
| `web/css/app.css` | 디자인 토큰(다크, IBM Plex, 앰버 + 상태 4색) · 반응형 |
| `web/js/app.js` | 해시 라우팅 · 토큰 로그인 · 상단바 |
| `web/js/api.js` | fetch 래퍼 · WebSocket(재접속) |
| `web/js/charts.js` | 선 그래프 · heatmap · 컬러바 · 컬러맵 |
| `web/js/ui.js` `state.js` | 공통 도우미 · 로그인 사용자 |
| `web/js/pages/overview.js` | 1. 동작 상황표 |
| `web/js/pages/detail.js` | 2-A Manual / 2-B 자동 측정 |
| `web/js/pages/viewer.js` | 3. Heatmap Viewer |
| `web/js/pages/datalist.js` | 4. 데이터 목록 |
| `web/vendor/plotly-gl3d.min.js` | Stacked 3D 용 Plotly 2.35.2 (gl3d 부분, 로컬) |

주소: `#/overview` · `#/detail/3` · `#/detail/3/auto` · `#/viewer?run=24&air=23&value=idf&sy=9&sx=5` · `#/data`
(Viewer 주소를 그대로 공유하면 같은 화면이 열린다)

## 2. 화면별 — 디자인 대비 구현

### 1. 동작 상황표
- 20쌍 카드: AOS 상태·모드(AUTO/MANUAL)·평균전류(V, 최근 10초)·파라미터, GFC 가스명·농도(V)·제어모드·마지막 수신
- 자동측정 중이면 카드에 `받은/전체 heatmap`
- 타일 5종, UDP 서버(수신속도·손실률·가동), 콘솔 접속 현황, 최근 이벤트 + 전체 로그
- 변경사항 반영: "PAIR" → "AOS 01", IDF 표시 없음, 수동모드 평균전류(V)

### 2-A. Manual Mode
- AOS Current (V) — 60/120/300/600초, 자동 스케일, 현재·최소·최대·평균·표준편차
- **"현재 그래프 저장"** → `manual_mark` 에 파라미터 + 최근 10초 평균 기록
- 수동 제어 6종 + LF On/Off: 슬라이더·숫자·개별 **적용**, **변경값 일괄 적용**, 편집 취소, 다시 읽기
  - (2026-09-23.01) **콘솔과 동시 제어** — 편집하지 않은 칸은 장비 값(0x44, 1초)을 그대로 따라간다. 콘솔에서 바꾸면 파란색 깜빡임
  - 편집 중인 칸만 노란 테두리로 유지, 장비 값이 같아지면 편집 해제. 적용 후 5초 안에 장비에 반영되지 않으면 경고
- 최근 조작자 표시(정보용): 내가 최근 조작 / 콘솔이 최근 조작. 제어를 막는 것은 **권한 없음 · 오프라인 · run 진행 중** 뿐
- GFC: 가스명 변경·저장, 자동/수동 제어, 펌프 ON/OFF, 공급 중지, 자동 주입 시작/정지·시간 설정, 농도 10분 그래프
- ⚠ 디자인의 목표 농도·MFC1/2·챔버 온습도는 **GFC 가 보내지 않는 값**이라 실제 수신값(TVOC 1·2, 펌프 3채널, 주입 상태, 잔여/사이클, RSSI)으로 대체

### 2-B. 자동 측정 모드
- 측정 종류(가스/기준 Air) · 모드(Full/1 hour/Sample, Sample 8조합 편집) · 가스명·농도·기준 Air·라벨
- 측정 시작 / 일시 정지·재개 / 종료(저장) / 중단 · 진행률 · 남은 시간 추정
- AOS Current 120초, **누적 heatmap 최근 4장**, 측정 정보, GFC 농도, 이 장비의 최근 측정
- ⚠ **장비 명령(0x80 시작 / 0x86 업로드)은 미구현** — 화면 상단에 표시. 지금은 DB run 기록만

### 3. Heatmap Viewer
- 가스 · AOS · 측정 데이터 · **Air(Ref) 선택(필수, 자동 선택 없음 — D13b)** → 불러오기
- 표시 값: Idf(Target − Air) / 측정값 / Air 값 · 스케일: 16장 공통 / 타일별 / Manual · 컬러맵 Jet(장비 호환)/Turbo/Viridis
- **Selection Grid 10×10**(1 hour 는 4×4): 섹션 대표값 색 · 클릭 이동 · HV/Frq 드롭다운
- 16장(행 LFF × 열 Duty) / Sample 8장, 타일 클릭 → 확대 + 마우스 위치 값(CV·LFV·값)
- CSV · PNG 내보내기
- 임포트(legacy) 데이터는 임포트 Air 와만, 실측은 실측끼리만 비교 (D16)
- (2026-09-23.01) **보기 방식**: 개별(16장) / **Stacked 3D** — PCSW `ShowTwinMultiStackedHeatMap` 과 같은 2×4
  (위 DutyStack: 열=LFF, 층=Duty · 아래 LFFStack: 열=Duty, 층=LFF · x=LF_Volt, y=CV, Twin 컬러맵, 3D Surface 옵션)
- (2026-09-23.01) **Interpolation (보간)** 체크 — 개별·확대·Stacked 공통. On=이중선형, Off=측정 격자 그대로
- 주소 파라미터 `view=stack`, `interp=1`
- 디자인의 "Twin Trace" 는 Single 전환으로 trace 가 없어져 제외

### 4. 데이터 목록
- 기간 · 가스 · AOS · 모드 · 출처(실측/임포트) · 검색
- Air(Ref) List / Gas Data List (페이지 12행) · DB 용량 · run · heatmap 수
- 미리보기: 정보 · heatmap 8장 · **Viewer 열기** · 정보 수정(라벨·가스명·농도·메모, control 권한)

## 3. 배포

```bash
gts-push                          # server/web 포함 전송 + 재시작
# 브라우저: http://192.168.0.6:8081/   (외부 http://218.147.152.41:8081/)
```

처음 접속하면 토큰을 묻는다 → `gts-token token 이름 read|control` 로 발급한 토큰을 붙여 넣기.
API 문서는 그대로 `/docs`.
