# AOS_GTS

AOS / GFC 가스 측정·제어 시스템.

```
 ESP32-P4 콘솔          Ubuntu 서버 192.168.0.6              ESP32 브리지         STM32
┌───────────┐  5502   ┌──────────────────────┐   5501    ┌──────────┐ UART2  ┌────────┐
│ P1 장치선택 │───────▶│  gts_server.py        │──────────▶│gts_gfc_udp│──────▶│ Pump   │
│ P2 GFC     │◀───────│   UDP 3포트 + FastAPI  │◀──────────│           │115200 │ TVOC   │
│ P4 AOS     │        │   + PostgreSQL         │   5500    ├──────────┤ UART2  ├────────┤
└───────────┘        │   + 웹 (8081)          │──────────▶│gts_aos_br│──────▶│ HV/CV  │
                      └──────────────────────┘◀──────────│          │38400  │ Frq/LF │
                                                          └──────────┘       └────────┘
```

## 구성

| 폴더 | 내용 |
|---|---|
| `GTS_CONSOLE_ESP32P4/` | 콘솔 펌웨어 (ESP32-P4, LVGL 5화면) |
| `gts_gfc_udp/` | GFC 브리지 (ESP32) — 가스 유량 제어 |
| `gts_aos_bridge/` | AOS 브리지 (ESP32) — FAIMs Twin 파라미터 |
| `server/` | UDP 서버 + FastAPI + 웹 (서버로 배포) |
| `tools/` | zsh 작업 환경 (`gts-help`) |
| `DOC/` | 규격·설계·인수인계 문서 |

## 시작하기

```bash
# ① 작업 환경
echo "source \"$PWD/tools/gts_env.zsh\"" >> ~/.zshrc
exec zsh
gts-doctor          # 무엇이 빠졌는지 알려 준다

# ② Wi-Fi 접속 정보 (git 에 없다 — 기계마다 채운다)
for p in GTS_CONSOLE_ESP32P4 gts_gfc_udp gts_aos_bridge; do
  cp $p/main/wifi_secrets.h.example $p/main/wifi_secrets.h
done
# 각 파일을 열어 SSID/비밀번호를 채울 것

# ③ 빌드
idf                 # ESP-IDF 환경 (터미널마다 한 번)
gts-aos fm          # 이동 + 빌드 + 플래시 + 모니터
```

`managed_components/` 와 `build/` 는 추적하지 않는다. 첫 빌드에서
`dependencies.lock` 을 보고 자동으로 내려받는다.

## 읽는 순서

1. `DOC/GTS_인수인계_20260922.md` — 전체 맥락·프로토콜·설계 결정
2. `DOC/GTS_UDP_Protocol.md` — 콘솔 ↔ 서버
3. `DOC/GTS_GFC_UDP.md` — 서버 ↔ GFC
4. `DOC/GTS_AOS_Bridge.md` — 서버 ↔ AOS, STM32 Twin 프로토콜
5. `DOC/GTS_DB_API_개발문서.md` — DB·API·웹

## 이 저장소에 없는 것

용량이 크고 거의 바뀌지 않는 자료는 **iCloud** 에 둔다
(`iCloud/Work/AOS_GTS/`).

| | 크기 | 무엇 |
|---|---|---|
| `DOC/OLD_PCSW/` | 1.2G | 옛 PC 프로그램 바이너리 |
| `DOC/GTS_DB_Design/AOS_TWIN_File_Viewer/` | 229M | 뷰어 바이너리 |
| `DOC/example_data/`, `example_data_single/` | 191M | 측정 `.dat` 48개 |
| `DOC/AOS_H753_V1_Single/Debug/` | 106M | STM32 빌드 산출물 |
| `JC4880P443C_Demo/` | 20M | LCD 보드 벤더 데모 — [upstream](https://github.com/lalith-ais/JC4880P443C_Demo) |

`JC4880P443C_Demo` 에서 참고한 파일은 `GTS_CONSOLE_ESP32P4/docs/demo_ref/`
에 복사돼 있다.
