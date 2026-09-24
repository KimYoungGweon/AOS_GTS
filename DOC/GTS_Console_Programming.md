> **진행 표시 규칙** — ✅ 구현 완료 / ⏳ 진행 중 / ⬜ 미착수
> 실기 확인까지 끝난 것만 ✅ 로 올린다. 갱신 2026-09-21

### 주요 기능 정리
1. LCD 화면 : 800x480(H mode)
2. JC4880p443c EVM Kit(ESP32P4)
3. udp. server : 218.147.152.41, port: 5502 로 연결
4. 참고 프로그램 JC4880P443C_Demo
    Encoder 동작, SW포함, UDP동작 -> 테스트 완료
5. encoder를 사용하여 값을 변경함. 
6. Device 연결 및 제어
    - AOS(1~20번)
    - GFC(1~20번)
7. LCD Touch를 사용하여 제어
    -  연결 Device 종류 및 ID 변경
    - GFC Device 제어
        - Manual Mode 
            - Pump1 On/Off
            - Pump2 On/Off
        - Auto Mode
            - Start시 Pump1, Pump2 On Time(sec) : 0.1 단위 ~ 60sec 까지
            - 10분 주기로 Pump1, Pump2 On Time(sec) : 0.1 단위 ~60sec 까지
            - Auto Start / Stop
            - Stop : Pump1, Pump2 모두 Off
    - AOS Device 제어
        - 가스 Data 측정 Mode : 
            - 측정 Data Type(4종) : preTest, 1hour, 2hour, 8hour(full data)
            - 4종류 선택
            - Start / Stop
            - 측정종료가 되면 자동 Stop 됨
        - Manual Mode
            - HV : 0~200V, step : 0.01V, 0.1V, 1V
            - Frq : 200~800kH, step : 0.1kHz, 1kHz, 10kHz
            - Duty: 20~80%(오류수정), jog step : 0.01%, 0.1%
            - CV : -5 ~ 5V,  step: 0.001V, 0.01V, 0.1V
            - LF_Mode : On/Off
                - LF_Frq : 50 ~ 200Hz, step : 1Hz
                - LF_Volt : 0~5V, step : 0.01V, 0.1V
                - LF_Shape : Square(6) 고정 변경필요없음, Sine, Triangle, Tropizodal
            - step 변경은 Jog S/W를 사용한다.
            - 값의 변경은 Jog를 변경함 따라 변동되면, 값변경시 udp로 서버에 전달한다.


### 화면 구성
1. ✅ LCD 화면 : 800x480(H mode) — 패널은 480x800 세로, LVGL 270도 회전 (실기 확인)
2. 화면구성 : **5화면**(장치 선택에 따라서 어떤 화면을 사용할지 결정됨)
    1. ✅ Device 선택 : Device Type Device ID선택 : 선택을 하면 장비와 테스트 컨넥션을 하여 장비상태를 읽어옴.
         (종류·번호를 바꾸는 즉시 CONNECT 송신. Jog 는 50ms 디바운스 후 1회)
    2. ⏳ GFC Mode — 구현 완료, 실기 확인 대기
    3. ⏳ AOS Device 가스 Data 측정 Mode — 구현 완료, 실기 확인 대기
    4. ⏳ AOS Device Manual Mode — 구현 완료, 실기 확인 대기
        - 제어종류가 6가지 이고 각각마다 step 이 있음. 스텝이 보여야됨.
        - 6종류의 현재값이 표시됨. jog 변경시 선택된 제어 종류가 step에 따라서 증가 또는 감소함.
    5. ⏳ (추가사항) Wifi 접속 List — 구현 완료, 실기 확인 대기
        - 접속할 장소가 2~3곳 이기에 Wifi 접속되는 지점의 List를 보여주며. 선택시에 Wifi 접속한다.
        - Wifi 연결이 않되면 1번화면에서 5번으로 자동 변경되면. 이화면 선택후 Wifi가 연결되면 1번 화면으로 전환한다.
        - **결정** : 자동 전환은 P1 에서만. P2~P4 작업 중에는 화면을 뺏지 않고
          상단바에 NO WIFI 만 표시한다 (측정·펌프 운전 중 화면이 바뀌는 것을 막기 위해).
          상단바의 Wi-Fi 표시를 누르면 어느 화면에서든 P5 로 갈 수 있다.
3. 기타 수정사항
    - ✅ DEVICE change 버튼 조금 크게 20%정도 — 126x44 → 140x53
    - ✅ 위좌측 "GTS CONSOLE" Lavel을 크게할것 — 20px → 26px (약 30%).
          Montserrat 은 Barlow Condensed 보다 넓어 MyID 칩 x 를 138 → 200 으로 밀었다.
        



### UDP Protocol
1. FW_RS232_통신규격.md 화일은 현재 AOS 장치와 UART로 연결된 ESP32 사이에 사용하게될 F/W 프로토콜이다. 이 프로토콜을 참고하여 콘솔에서 AOS 를 제어하기 위한 UDP 프롵콜을 설계할것.
2. 기본형식은 //0x02,device_type, device_id, cmd, size_L, size_H, data......, CRC16 으로 설계할것.


### 참고사항.
1. LCD Design은 다음을 사용할 것. : `DOC/gts-console-ui-mockups/project/`
   (폴더명 변경됨. 사양서 = `GTS_Console_LCD_Spec.md` Rev 0.2)
2. ✅ udp upload 기능은 디자이너에 있는 기능을 기본으로 하여 정리할것.
   → `DOC/GTS_UDP_Protocol.md` Rev 0.1. **디자인 사양서 §9 의 초안은 이것으로 대체됨**
      (cmd 코드·값 표현이 다르므로 §9 를 따르지 말 것. 이유는 UDP 문서 0절 참고)
3. ✅ 프로젝트명은 GTS_CONSOLE_ESP32P4 로 만들것.
4. ✅ JC4880P443C_Demo/ 는 ESP32P4 데모코드로 F/W Source encoder(jog) 및 UDP server 동작등을 테스트 완료한 코드임(LCD화면제어, JOG, UDP Upload). 따라서 이 코드를 복사하여 작업을 할것. 

(추가변경사항)
5. ✅ AOS 장치 선택시 기본화면은 Manual Mode로 한다. 현재는 가스 Data 측정 Mode 임.
   → ENTER CONTROL : GFC → P2, AOS → **P4 Manual**. P3 는 조그바 "GO TO / 자동측정" 으로.
6. ✅ Wifi List 3개 를 우선 하드 코딩으로 정리할것. 선택해서 wifi 접속할것.
   → `main/gts_config.h` 의 `GTS_WIFI_0/1/2_*`. 순서를 바꾸면 NVS 의 최근 접속
      인덱스가 어긋나므로 추가는 뒤에만 할 것.
   ✅ (최근접속 list를 flash 저장할것.)
   → **결정** : NVS 에는 마지막 성공 지점 인덱스 + 접속 시각만 저장한다.
      SSID/비밀번호는 소스에 둔다. 부팅 시 그 지점부터 접속을 시도하고,
      P5 목록에 "최근 접속 HH:MM" 으로 표시한다.
      시각은 SNTP 로 맞춘다 (`GTS_WIFI_SNTP`). 시각이 없으면 "최근 접속" 만 표시.
7. ✅ 완료된사항은 완료 표시할것. 다음부터 추가사항은 번호를 붙여서 계속 작업함.
   → 이 문서 맨 위의 진행 표시 규칙 참고.
8. ✅ 의문사항이 있을시 반듯이 질문을 하고 시작할것.
   → 이번 작업 전 3건 확인함 (UDP 규격 / P5 전환 범위 / NVS 저장 범위).

### 아직 남은 것
- ⬜ 서버 UDP **송신** 구현 (현재 수신만). CONNECT_ACK(0x81) 이 와야 ENTER CONTROL 이 열린다.
  그때까지는 `gts_config.h` 의 `GTS_OFFLINE_MODE 1` 로 화면을 검증한다. **배포 전 0 으로.**
- ⬜ 한글 폰트 — LVGL 내장 Montserrat 에 한글 글리프가 없다.
  서브셋 폰트 변환 후 `GTS_USE_KR_FONT` 1 로.
- ⬜ 실기 시인성 확인 후 폰트 크기 조정
