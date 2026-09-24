
### GTS WEB 개발문서 기능정리

1. WEB Design 페이지 : https://claude.ai/artifact/73mtYsQwxdiFLZCdfXVUDn
2. 디자인 페이지 설계에서 대부분 기능정리가 되었음. 이 디자인을 가지고 작업을 할것.
3. 서버 ip:192.168.0.6 http port를 어떤것을 사용할지 정하고, 공유기 포트를 열어서, 집에서도 접속가능하도록 할 예정임.



### 기타사항
1. 완료된 결과를 표시할 것.[완료]


### 진행 결과 (2026-09-23)
1. DB 생성 [완료] — PostgreSQL, 스키마 v1.4 (`server/db/schema.sql`). AOS/GFC 쌍 고정, device PK 충돌 수정
2. API 서버 생성 [완료] — `server/gts_server.py` (UDP + DB + FastAPI 한 프로세스), REST + WebSocket, 토큰 3등급
3. HTTP 포트 [결정] — **8081** (TCP). 공유기: 외부 8081 → 192.168.0.6:8081. 외부에서는 read 토큰 권장
4. 예제 데이터 [완료] — `DOC/example_data` 16개 → Single 변환(32 run) 개발 DB 적재·검증 완료. 서버 DB 에는 설치 후 `gts-import` 한 번. 사본 `DOC/example_data_single/`
5. 서버 설치 [완료] — `DOC/GTS_DB_API_개발문서.md` 5절 순서대로
6. 웹 프런트 [완료] — 5화면 구현 (`server/web/`), `http://192.168.0.6:8081/` · 자세한 내용 `DOC/GTS_WEB_프런트_개발문서.md`
자세한 내용: `DOC/GTS_DB_API_개발문서.md`



### 수정사항.(2026.09.23.01)
1. Heatmap Viewer 
    1. 현재 개별그림보기 Mode 에 Stacked heatmap 보기 추가 : View Mode 추가
        - example image : /DOC/stacked heatmap example.jpg
    2. 이미지 Interpolation 기능 추가 : Interpolation OnOff(CheckButton)
    3. /DOC/GTS_DB_Design/AOS_TWIN_File_Viewer 에 코드참조할것. (stacked heatmap)
        - 이미지 인터폴레이션 기능이 있음.
2. console 동작과 Web 에서 제어가 동시에 가능하도록 할것.
    - Web 에서의 변경이 콘솔에서 update, 콘솔변경시 web update 되도록 할것.


#### 처리 결과 (2026-09-23) — [완료]
1. Heatmap Viewer — [완료]
    - 왼쪽 "보기 방식 (View Mode)" : **개별 (16장)** / **Stacked 3D** 선택 (주소에 `view=stack` 저장 → 공유 가능)
    - Stacked 3D = PCSW frm3DView `ShowTwinMultiStackedHeatMap` 과 같은 2×4 배치
      - 위 줄 DutyStack (열마다 LFF 고정, 층 = Duty) · 아래 줄 LFFStack (열마다 Duty 고정, 층 = LFF)
      - x = LF_Volt, y = CV, 색 = 값(Idf/측정값/Air), 공통 스케일, 투명도 0.85, 같은 카메라 시점
      - 컬러맵 **Twin 3D (장비 Stacked)** 추가 — Stacked 로 바꾸면 Jet → Twin 자동 전환
      - "3D Surface (값을 높이로)" 옵션: z = 층 + 값 × (층간격×0.4/최대|값|) — PCSW 와 같은 식
      - 드래그 회전 · 휠 확대 · PNG 내보내기 지원. 3D 라이브러리는 `web/vendor/plotly-gl3d.min.js`(로컬, 인터넷 불필요)를 Stacked 를 처음 열 때만 읽음
      - Sample(8조합) run 은 Duty×LFF 격자가 아니라서 Stacked 비활성
    - **Interpolation (보간)** 체크 : 개별 16장·확대·Stacked 모두 적용 (주소 `interp=1`)
      - On = 이중선형 보간(부드럽게), Off = 측정 격자 그대로(칸마다 한 색)
2. 콘솔·웹 동시 제어 — [완료]
    - 웹 제어 잠금(denied_lock) 제거 → 콘솔과 웹이 동시에 조작, 나중 명령이 적용. "락" 은 최근 조작자 표시로만 사용
    - 콘솔 → 웹 : 장비가 1초마다 올리는 0x44 로 웹 입력칸이 장비 값을 따라감 (바뀐 칸은 파란색 깜빡임). 내가 편집 중인 칸만 유지
    - 웹 → 콘솔 : 장비 값이 바뀌면 서버가 그 AOS 를 보고 있는 콘솔에 0xC3 을 자동 전송
    - 측정 run 진행 중에는 AOS 파라미터 변경 거부는 그대로
    - 한계: GFC 자동/수동 모드·주입 시간은 콘솔 메시지(0xA3)에 필드가 없어 웹 변경이 콘솔 화면에 표시되지 않음 (펌프·주입 상태는 동기화됨)
3. (추가) 장비 상세에서 주파수 등 변경이 가끔 적용 안 되던 문제 — [완료]
    - 원인(브리지 aos_ctrl.c): 그림자(shadow) 값이 부팅 기본값에서 시작해 STM32 실제 값과 맞춰지지 않음 →
      ① 요청값이 그림자와 같으면 UART 전송 생략 ② 0x2A 프레임에 다른 항목(HV/DUTY/CV)의 옛 값이 실려 덮어씀 ③ UART 프레임 유실 시 확인·재전송 없음
    - 브리지 수정: 항상 전송 · 전송 300ms 후 0x02 읽어 확인 · 불일치 항목만 최대 3회 재전송 · 그래도 실패하면 이벤트 0x22(APPLY_FAIL) · 대기 중엔 STM32 실제 값으로 그림자 동기화
    - 서버 수정: 제어 명령(0x40/0x41/0x42, GFC 펌프/공급)을 ACK 받을 때까지 0.4초 간격 최대 3회 재전송, 실패 시 오류 이벤트
    - 웹: 적용 후 5초 안에 장비 값이 안 바뀌면 경고 알림


### 수정사항.(2026.09.23.02)
1. 자동스케일 Off : 2V -> 5V Scale 변경
2. Heatmap Viewer ->장비상세 페지에서->Heatmap Viewer 변경시 Heatmap 화면이 초기화됨. 앞에 선택된 것이 있으면 그 data를 보일것.
3. Heatmap Viewer : Interpolation checked default로 할것.
4. 가스 그래프에서 스케일 고정 추가 : 고정시 최대값 4.0으로 할것.
5. 동작상황표에서 2 장비 이상 선택 가능하도록 수정할것. (현재 장비가 1대여서 테스트는 분가함.)
    - 2장비 이상 선택시, 전류 그래프, 가스농도 그래프 등은 장비갯수 모듀 표시할것.
    - 우측 수동제어에서 선택된 장비 모두 HV, Frq, Duty 등등의 파라메터를 모두 동작 시킬것.
    - 그래프 색으로 장비 구별


#### 처리 결과 (2026-09-23.02) — [완료]
1. AOS Current 자동 스케일 Off → 0 ~ 5 V 고정 — [완료]
2. Heatmap Viewer 상태 유지 — [완료] 다른 화면에 갔다 "Heatmap Viewer" 탭을 누르면 마지막 보던 측정/Air/섹션/표시값/보기방식이 그대로 열림 (브라우저에 저장)
3. Interpolation 기본 On — [완료] (끄면 주소에 `interp=0`)
4. 가스 농도 그래프 "스케일 고정 (0~4 V)" 체크 추가 — [완료]
5. 여러 장비 동시 보기·제어 — [완료] (실장비 1대라 시뮬레이터 2대로 검증)
    - 동작상황표: 카드 왼쪽 위 네모로 선택 → "선택 장비 상세 보기" (주소 `#/detail/1,3,5`). 장비 상세 상단 "여러 대 ▾" 로도 선택
    - AOS Current · 가스 농도 그래프: 선택 장비 모두 표시, 장비별 색 + 범례, 장비별 현재/최소/최대/평균/표준편차 표
    - 수동 제어: 적용 시 선택한 AOS 모두에 같은 값 전송 (오프라인 장비는 건너뛰고 알림). 화면 값은 첫 장비 기준, 장비별 현재 값은 색으로 표시
    - GFC 버튼(자동/수동, 펌프, 공급 중지, 주입 시작, 시간 저장)도 선택한 GFC 모두에 전송. 가스명 변경은 한 대씩
    - 자동 측정 모드는 한 대씩 (여러 대 선택 시 첫 장비)
