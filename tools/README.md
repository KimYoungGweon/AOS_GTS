# tools/ — 작업 환경 설정

## 새 맥에서 설정하기

집 맥·회사 맥처럼 기계가 둘 이상일 때.

```bash
# ① 환경 파일 불러오기 — 파일 자체는 iCloud 로 따라온다
echo 'source "$HOME/Library/Mobile Documents/com~apple~CloudDocs/Work/AOS_GTS/tools/gts_env.zsh"' >> ~/.zshrc
exec zsh

# ② 점검 — 뭐가 없는지 한 번에 알려 준다
gts-doctor

# ③ 장소에 맞게 서버 주소 (집이면)
gts-site home
```

`gts-doctor` 가 ✘ 로 짚어 주는 것만 해결하면 된다. 흔한 것 셋:

| ✘ | 처방 |
|---|---|
| iCloud 미다운로드 N개 | `gts-icloud` |
| ESP-IDF 없음 / python 환경 없음 | `gts-idf-install` (IDF 자체가 없으면 먼저 설치) |
| SSH 안 닿음 | `gts-site home` (집) / `gts-site office` (회사) |

### `.zshrc` 줄이 깨졌을 때 복구

```bash
cp ~/.zshrc ~/.zshrc.bak.$(date +%H%M)
grep -v gts_env ~/.zshrc > ~/.zshrc.new && mv ~/.zshrc.new ~/.zshrc
echo 'source "$HOME/Library/Mobile Documents/com~apple~CloudDocs/Work/AOS_GTS/tools/gts_env.zsh"' >> ~/.zshrc
zsh -n ~/.zshrc && exec zsh
```

gts_env 가 들어간 줄을 전부 지우고 다시 넣는다. **작은따옴표**를 써야
`$HOME` 이 그대로 들어가고 경로의 공백도 안전하다. `zsh -n` 으로 문법을
먼저 보고 통과했을 때만 셸을 바꾼다.

> `gts_env.zsh` 는 자기 위치에서 프로젝트 루트를 스스로 찾으므로,
> AOS_GTS 를 다른 데 두었어도 그 경로로 source 하기만 하면 된다.

### 기계마다 다른 값 — `~/.gts_env.local`

`gts_env.zsh` 는 iCloud 에 있어 모든 맥이 같은 내용을 본다. 기계마다 달라야
하는 값은 **`~/.gts_env.local`** 에 적는다. iCloud 밖이라 맥마다 따로 있고,
`gts_env.zsh` 가 **맨 마지막에 읽어서** 기본값을 덮어쓴다.

```zsh
# ~/.gts_env.local  — 집 맥 예시
export GTS_IDF="$HOME/esp/esp-idf"                  # IDF 를 다른 데 깔았다면
export GTS_PORT_ESP32="/dev/cu.SLAB_USBtoUART"      # 어댑터가 다르면
export GTS_SRV_EXT_PORT="22"                        # 외부 SSH 포트가 22 가 아니면
```

`gts-site` 가 `GTS_SRV_HOST` / `GTS_SRV_PORT` 를 여기에 자동으로 써 준다.

### 집에서 서버에 닿는 문제

서버(`192.168.0.6`)는 회사 LAN 안에 있다. 집에서는 공유기 포트포워딩을 타고
외부 IP `218.147.152.41` 로 들어가야 한다.

- **웹/API (8081)** — 포워딩돼 있다. `http://218.147.152.41:8081/`
- **SSH (22)** — 포워딩 여부는 확인 필요. 안 열려 있으면 집에서는
  `gts-push` / `gts-log` / `gts-token` 이 안 된다

SSH 가 막혀 있으면 집에서 할 수 있는 것은 **펌웨어 빌드·플래시**와
**웹 UI 로 확인**까지다. 서버 배포는 회사에서 하거나, 공유기에 22 번
포워딩을 열어야 한다.

## 설치 (한 번만)

Mac 터미널에서:

```bash
echo 'source "$HOME/Library/Mobile Documents/com~apple~CloudDocs/Work/AOS_GTS/tools/gts_env.zsh"' >> ~/.zshrc
exec zsh
```

`✔ AOS_GTS 환경 로드됨 — 명령 목록은 gts-help` 가 뜨면 된 것이다.

### conda 자동 활성화 끄기 (권장)

프롬프트의 `(base)` 가 `idf.py` 를 conda 의 python 으로 실행시켜
`No module named 'rich_click'` 을 만든다. `idf` 명령이 매번 떼어 주지만,
아예 꺼 두는 게 편하다.

```bash
conda config --set auto_activate_base false
```

---

## 하루 시작

```bash
idf                 # ESP-IDF 환경 로드 (터미널마다 한 번)
gts-aos             # 프로젝트로 이동
gts-fm              # build + flash + monitor
```

서버 쪽은:

```bash
gts-push            # 백업 → scp → 테스트 → 재시작
gts-log             # 실시간 로그
```

---

## 명령 요약

`gts-help` 가 같은 내용을 보여 준다.

### 펌웨어 — 프로젝트 이름 + 동작

```bash
gts-console fm        # 이동 + 빌드 + 플래시 + 모니터
gts-aos build
gts-gfc flash /dev/cu.usbserial-0001
```

| 프로젝트 | |
|---|---|
| `gts-console` | ESP32-P4 콘솔 (`/dev/cu.usbmodem*`) |
| `gts-gfc` | GFC 브리지 (`/dev/cu.usbserial-*`) |
| `gts-aos` | AOS 브리지 (`/dev/cu.usbserial-*`) |

| 동작 | |
|---|---|
| (없음) | 그 폴더로 이동만 |
| `build` | 빌드 |
| `flash` (`dl`) | 플래시 |
| `fm` | 빌드 + 플래시 + 모니터 |
| `mon` | 모니터만 (종료 `Ctrl+]`) |
| `clean` | `build/` 삭제 |

### 이동만

| | |
|---|---|
| `gts` | 프로젝트 루트 |
| `gts-srv` | `server/` |
| `gts-doc` | `DOC/` |

### ESP-IDF

| | |
|---|---|
| `idf` | IDF 환경 로드. conda 를 먼저 떼어 낸다 |
| `gts-idf-install` | IDF python 환경 + 툴체인 설치 (처음 한 번) |
| `gts-build` | `idf.py build` |
| `gts-flash [포트]` | 플래시 |
| `gts-mon [포트]` | 시리얼 모니터 (종료 `Ctrl+]`) |
| `gts-fm [포트]` | build + flash + monitor |
| `gts-ports` | 꽂혀 있는 USB 시리얼 포트 |
| `gts-clean` | `build/` 삭제 |

포트를 안 적으면 현재 디렉터리를 보고 고른다.

- `GTS_CONSOLE_ESP32P4` → `/dev/cu.usbmodem*` (ESP32-P4, USB-JTAG)
- 그 외 → `/dev/cu.usbserial-0001` (ESP32, CP2102)

기본 포트가 없으면 꽂혀 있는 것 중 첫 번째를 쓴다. 두 보드를 동시에
꽂았다면 `gts-ports` 로 확인하고 직접 넘기는 게 안전하다.

```bash
gts-fm /dev/cu.usbserial-0001
```

`gts-build`/`gts-flash`/`gts-mon` 은 IDF 가 안 올라와 있으면 자동으로 올린다.

### 서버 `gts@192.168.0.6`

| | |
|---|---|
| `gts-push` | **권장 배포 경로** — 날짜 백업 → scp → `test_packets.py` → 통과 시에만 재시작 |
| `gts-rollback` | 백업 목록 |
| `gts-rollback 20260921_1430` | 그 버전으로 복구 |
| `gts-pull [파일]` | 서버에서 회수 (기본 `udp_server.py`) |
| `gts-which` | 지금 도는 서버가 신/구 버전인지 |
| `gts-diag` | 서비스가 안 뜰 때 — status·로그·포트 점유·직접 실행을 한 번에 |
| `gts-log` | 실시간 로그 |
| `gts-log-n [N]` | 최근 N줄 (기본 50) |
| `gts-status` / `gts-restart` / `gts-stop` / `gts-start` | 서비스 |
| `gts-ssh` | 그냥 접속. 인자를 주면 원격 실행 |
| `gts-connect` / `gts-disconnect` | 마스터 연결 열기 / 끊기 |
| `gts-ssh-key` | 공개키 등록 — 비밀번호를 아예 안 묻게 (권장) |
| `gts-nopasswd` | 서비스 제어만 비밀번호 없이 쓰는 설정 방법을 출력 |

`gts-push` 는 **테스트가 실패하면 재시작하지 않는다.** 깨진 서버가 올라가는
일을 막기 위해서다. 실패하면 복구 명령을 알려 준다.

### 시뮬레이터 (서버에서 실행)

장비 없이 경로를 검증할 때. 서버가 5500/5501 을 이미 잡고 있어서 로컬
포트를 비켜 준다.

| | |
|---|---|
| `gts-sim-aos [ID]` | AOS 흉내 (로컬 6500) |
| `gts-sim-gfc [ID]` | GFC 흉내 (로컬 6501) |
| `gts-sim-con [ID]` | 콘솔 흉내 |

터미널을 세 개 띄우고 서버 → 시뮬 → 콘솔 순으로 올리면 된다.

---

## 비밀번호를 여러 번 묻는 문제

`gts-push` 는 ssh 를 여러 번 쓴다. **ControlMaster** 로 연결을 재사용하게 해
뒀으므로 첫 접속에서 한 번만 묻고, 이후 10분간은 그 연결에 얹힌다.

아예 안 묻게 하려면 공개키를 등록하면 된다 (한 번만).

```bash
gts-ssh-key
```

`~/.ssh/id_ed25519` 가 없으면 만들고 `ssh-copy-id` 로 서버에 올린다.
`sudo` 비밀번호는 별개이므로, 그쪽도 없애려면 `gts-nopasswd` 를 참고.

## 자주 겪는 것

| 증상 | 원인 / 처방 |
|---|---|
| `command not found: gts-help` | `~/.zshrc` 에 줄이 없다. `.zshrc` 는 iCloud 가 아니라 **기계마다 따로**다 — 맥마다 한 번씩 넣어야 한다 |
| `~/.zshrc:export:N: not valid in this context` | 그 줄이 깨졌다. 아래 "복구" 참조 |
| `No module named 'rich_click'` | IDF 환경이 없거나 conda 가 가로챘다 → `idf` |
| `ESP-IDF Python virtual environment ... not found` | 터미널용 python 환경이 없다. VS Code 확장은 자기 venv(`~/.espressif/tools/python/v6.1/venv`)를 쓰고 `export.sh` 는 표준 위치(`~/.espressif/python_env/idf6.1_py3.11_env`)를 본다 → **`gts-idf-install`** (`idf` 가 확장 venv 를 자동으로 재사용해 보지만, 제대로 설치하는 게 낫다) |
| `attempt to rename spec 'link' to already defined spec 'picolibc_link'` | 옛 경로가 남은 `build/` → `gts-clean` 후 `gts-build` |
| 포트를 못 찾음 | `gts-ports`. 안 보이면 케이블/전원/드라이버 |
| 콘솔의 `ENTER CONTROL` 이 비활성 | 그 장치가 서버에 등록돼 있지 않다 (`CONNECT_ACK result=3`). `gts-which` 로 서버 버전 확인 → `gts-sim-aos` 나 `gts-sim-gfc` 로 장치를 하나 띄워 볼 것 |
| 서버에 패킷이 `HEX:...` 로만 찍힘 | 구 버전 서버가 바이너리를 모른다 → `gts-push` |
| `gts-push` 의 ④ 재시작 실패 | ⓐ `sudo` 비밀번호 — `ssh -t` 로 고쳤으니 프롬프트가 뜬다. 매번 묻는 게 싫으면 `gts-nopasswd`<br>ⓑ 서비스가 실제로 안 뜬 것 — 이제 `journalctl` 마지막 25줄을 자동으로 보여 준다. 그러면 `gts-rollback <스탬프>` |

## 경로 / 주소가 바뀌면

`gts_env.zsh` 맨 위의 변수만 고치면 된다.

```zsh
export GTS_ROOT="..."          # 프로젝트 루트
export IDF_PATH="..."          # ESP-IDF 설치 위치
export GTS_SRV_HOST="..."      # 서버 IP
export GTS_PORT_CONSOLE="..."  # 콘솔 기본 포트
export GTS_PORT_ESP32="..."    # GFC/AOS 기본 포트
```
