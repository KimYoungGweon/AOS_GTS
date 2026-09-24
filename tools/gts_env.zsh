# ──────────────────────────────────────────────────────────────────────
#  gts_env.zsh — AOS_GTS 프로젝트 zsh 작업 환경
#
#  설치 (한 번만):
#    echo 'source "$HOME/Library/Mobile Documents/com~apple~CloudDocs/Work/AOS_GTS/tools/gts_env.zsh"' >> ~/.zshrc
#    exec zsh
#
#  이 파일은 "빠르게" 로드된다 — ESP-IDF export.sh 를 여기서 부르지 않는다.
#  (export.sh 는 몇 초 걸려서 모든 새 터미널이 느려진다.)
#  IDF 가 필요할 때만 `idf` 를 한 번 치면 된다.
#
#  주요 명령은 `gts-help` 로 확인.
# ──────────────────────────────────────────────────────────────────────

# ── 경로 ─────────────────────────────────────────────────────────────
# 프로젝트 루트는 **이 파일이 있는 위치**에서 스스로 찾는다.
# 맥마다 iCloud 경로가 다를 수 있고, 아예 다른 곳에 두었을 수도 있다.
# ${(%):-%x} = 지금 실행 중인 파일의 경로 (source 된 파일에서도 정확하다)
_gts_self="${${(%):-%x}:A}"
if [[ -n "$_gts_self" && -d "${_gts_self:h:h}/DOC" ]]; then
    export GTS_ROOT="${_gts_self:h:h}"
else
    export GTS_ROOT="$HOME/Library/Mobile Documents/com~apple~CloudDocs/Work/AOS_GTS"
fi
unset _gts_self

# export.sh 가 있는 곳. IDF_PATH 자체는 export.sh 가 스스로 정하게 둔다
# (미리 박아 두면 설치 위치가 바뀌었을 때 조용히 어긋난다).
export GTS_IDF="$HOME/.espressif/v6.1/esp-idf"

# 서버 주소는 **있는 장소에 따라 다르다**.
#   회사(같은 LAN) : 192.168.0.6
#   집·외부        : 218.147.152.41  (공유기 포트포워딩을 타고 들어간다)
# 기계마다 다른 값은 이 파일이 아니라 ~/.gts_env.local 에 둔다 (맨 아래 참조).
# 전환은 `gts-site office` / `gts-site home`.
export GTS_SRV_USER="gts"
export GTS_SRV_HOST="192.168.0.6"
export GTS_SRV_PORT="22"           # 외부에서 들어올 땐 다른 포트일 수 있다
export GTS_SRV_DIR="/home/gts/gts_udp_server"
export GTS_SRV="$GTS_SRV_USER@$GTS_SRV_HOST"
export GTS_HTTP_PORT="8081"        # 웹/API (gts_server.py)

# ── SSH 연결 재사용 (ControlMaster) ─────────────────────────────────
# 첫 접속에서 비밀번호를 한 번 받아 마스터 연결을 만들고, 이후 ssh/scp 는
# 그 연결에 얹는다. gts-push 처럼 ssh 를 여러 번 쓰는 명령이 비밀번호를
# 한 번만 묻게 된다. 마지막 사용 후 10분간 살아 있다.
_gts_ssh_opts=(
    -o ControlMaster=auto
    -o "ControlPath=$HOME/.ssh/cm-%r@%h:%p"
    -o ControlPersist=10m
)
# ssh 는 -p, scp 는 -P 로 포트를 받는다 (대소문자가 다르다).
_gts_ssh()  { mkdir -p "$HOME/.ssh"; ssh -p "${GTS_SRV_PORT:-22}" "${_gts_ssh_opts[@]}" "$@" }
_gts_scp()  { mkdir -p "$HOME/.ssh"; scp -P "${GTS_SRV_PORT:-22}" "${_gts_ssh_opts[@]}" "$@" }

# 마스터 연결을 미리 열어 둔다. 비밀번호는 여기서 한 번만.
gts-connect() {
    _gts_ssh -O check "$GTS_SRV" 2>/dev/null && { _gts_ok "이미 연결돼 있다"; return 0 }
    _gts_step "마스터 연결 — 비밀번호는 여기서 한 번만"
    _gts_ssh -N -f "$GTS_SRV" && _gts_ok "연결됨 (10분 유휴 후 자동 종료)"
}

# 마스터 연결 끊기
gts-disconnect() { _gts_ssh -O exit "$GTS_SRV" 2>/dev/null; _gts_ok "연결 종료" }

# 비밀번호를 아예 안 묻게 — 공개키 등록 (권장, 한 번만)
gts-ssh-key() {
    [[ -f "$HOME/.ssh/id_ed25519" ]] || ssh-keygen -t ed25519 -N "" -f "$HOME/.ssh/id_ed25519"
    ssh-copy-id -i "$HOME/.ssh/id_ed25519.pub" "$GTS_SRV" \
        && _gts_ok "공개키 등록 완료 — 이제 비밀번호를 안 묻는다"
}

# 프로젝트별 기본 시리얼 포트 (.vscode/settings.json 과 같은 값).
# 꽂는 USB 포트에 따라 번호가 바뀌므로, 없으면 자동 탐색한다.
export GTS_PORT_CONSOLE="/dev/cu.usbmodem1452401"   # ESP32-P4 콘솔 (USB-JTAG)
export GTS_PORT_ESP32="/dev/cu.usbserial-0001"      # ESP32 GFC / AOS (CP2102)

# ── 색 ───────────────────────────────────────────────────────────────
_gts_ok()   { print -P "%F{green}✔%f $*" }
_gts_warn() { print -P "%F{yellow}▲%f $*" }
_gts_err()  { print -P "%F{red}✘%f $*" }
_gts_step() { print -P "%F{cyan}▸%f $*" }

# ══════════════════════════════════════════════════════════════════════
#  ESP-IDF
# ══════════════════════════════════════════════════════════════════════

# IDF 환경을 현재 셸에 올린다. 터미널을 새로 열 때마다 한 번씩 필요하다.
#
# conda 가 base 를 자동 활성화해 두면 idf.py 가 conda 의 python 을 집어
# "No module named 'rich_click'" 로 죽는다. 그래서 먼저 떼어 낸다.
# ESP-IDF 의 python 가상환경을 찾는다.
#
#   install.sh        → ~/.espressif/python_env/idf<ver>_py<X.Y>_env
#   VS Code 확장      → ~/.espressif/tools/python/v<ver>/venv
#
# export.sh 는 앞의 것만 본다. 확장으로만 설치했다면 그 경로가 비어 있어
# "Python virtual environment not found" 로 죽는다. 뒤의 것을 찾아
# IDF_PYTHON_ENV_PATH 로 알려 주면 그대로 쓸 수 있다.
_gts_find_idf_venv() {
    local c
    for c in $HOME/.espressif/python_env/idf6*_env(N); do
        [[ -x "$c/bin/python" ]] && { print "$c"; return 0 }
    done
    for c in $HOME/.espressif/tools/python/v6*/venv(N); do
        [[ -x "$c/bin/python" ]] && { print "$c"; return 0 }
    done
    return 1
}

idf() {
    if [[ -n "$CONDA_DEFAULT_ENV" ]]; then
        _gts_step "conda ($CONDA_DEFAULT_ENV) 해제"
        local n=0
        while [[ -n "$CONDA_DEFAULT_ENV" && $n -lt 5 ]]; do
            conda deactivate 2>/dev/null || break
            (( n++ ))
        done
        [[ -n "$CONDA_DEFAULT_ENV" ]] && _gts_warn "conda 가 안 떨어진다 ($CONDA_DEFAULT_ENV)"
    fi

    # conda 를 떼고 나면 python3 이 사라지는 경우가 있다. export.sh 가
    # idf_tools.py 를 돌리려면 python3 이 필요하다.
    if ! command -v python3 >/dev/null 2>&1; then
        _gts_err "PATH 에 python3 이 없다 — export.sh 가 돌 수 없다"
        return 1
    fi

    local sh="$GTS_IDF/export.sh"
    if [[ ! -f "$sh" ]]; then
        _gts_err "export.sh 를 못 찾음: $sh"
        _gts_warn "설치 위치가 다르면 gts_env.zsh 의 GTS_IDF 를 고칠 것"
        return 1
    fi

    # 표준 위치에 venv 가 없으면 VS Code 확장 것을 찾아 쓴다.
    if [[ -z "$IDF_PYTHON_ENV_PATH" ]]; then
        local venv=$(_gts_find_idf_venv)
        if [[ -n "$venv" ]]; then
            if "$venv/bin/python" -c "import rich_click" 2>/dev/null; then
                export IDF_PYTHON_ENV_PATH="$venv"
                [[ "$venv" == *"/tools/python/"* ]] && \
                    _gts_step "VS Code 확장의 venv 재사용: $venv"
            else
                _gts_warn "찾은 venv 에 IDF 패키지가 없다: $venv"
                _gts_warn "install.sh 로 제대로 만드는 게 낫다"
            fi
        fi
    fi

    _gts_step "ESP-IDF 로드 중 … ($GTS_IDF)"

    # ★ 출력을 감추지 않는다.
    #   감추면 "도구 미설치" 같은 진짜 실패 이유가 같이 사라진다.
    source "$sh"

    if ! command -v idf.py >/dev/null 2>&1; then
        print ""
        _gts_err "idf.py 가 PATH 에 없다 — 바로 위 메시지가 진짜 이유다"
        print ""
        print "  ▶ 대부분 이것으로 해결된다 (1~3분, 툴체인은 이미 있으면 건너뛴다)"
        print ""
        print "      $GTS_IDF/install.sh esp32,esp32p4"
        print ""
        print "  참고"
        print "   · IDF_PYTHON_ENV_PATH = ${IDF_PYTHON_ENV_PATH:-(미설정)}"
        print "   · 있는 python_env     : $(ls -d ~/.espressif/python_env/*/ 2>/dev/null | tr '\n' ' ')"
        print "   · 확장 venv           : $(ls -d ~/.espressif/tools/python/*/venv 2>/dev/null | tr '\n' ' ')"
        print "   · conda 가 가로채면   : conda config --set auto_activate_base false"
        return 1
    fi

    _gts_ok "IDF $(idf.py --version 2>/dev/null | tail -1)"
    print "   IDF_PATH = $IDF_PATH"
    print "   python   = $(command -v python)"
}

alias get_idf='idf'

# ESP-IDF python 환경 + 툴체인 설치 (한 번만).
# "Python virtual environment ... not found" 가 뜨면 이것을 돌린다.
gts-idf-install() {
    [[ -f "$GTS_IDF/install.sh" ]] || { _gts_err "install.sh 없음: $GTS_IDF"; return 1 }
    _gts_step "ESP-IDF 도구 설치 — esp32, esp32p4  (몇 분 걸릴 수 있다)"
    "$GTS_IDF/install.sh" esp32,esp32p4 || { _gts_err "설치 실패"; return 1 }
    _gts_ok "설치 완료. 이제 'idf' 를 실행할 것"
}        # Espressif 문서에서 쓰는 이름

# IDF 가 올라와 있는지 확인하고, 아니면 자동으로 올린다.
_gts_need_idf() {
    command -v idf.py >/dev/null 2>&1 && return 0
    _gts_warn "IDF 가 아직 안 올라옴 — 자동으로 로드한다"
    idf
}

# ── 시리얼 포트 ──────────────────────────────────────────────────────

# 꽂혀 있는 USB 시리얼 포트를 보여 준다.
gts-ports() {
    local found=0
    # (N) = NULL_GLOB. 안 붙이면 zsh 가 "no matches found" 로 에러를 낸다.
    for p in /dev/cu.usbserial-*(N) /dev/cu.usbmodem*(N) /dev/cu.SLAB_USBtoUART*(N); do
        [[ -e "$p" ]] || continue
        print "  $p"
        found=1
    done
    (( found )) || _gts_warn "USB 시리얼 포트가 안 보인다 — 케이블/전원 확인"
}

# $1 = 선호 포트. 없으면 꽂혀 있는 것 중 첫 번째.
_gts_pick_port() {
    local want="$1"
    [[ -n "$want" && -e "$want" ]] && { print "$want"; return 0; }
    for p in /dev/cu.usbserial-*(N) /dev/cu.SLAB_USBtoUART*(N) /dev/cu.usbmodem*(N); do
        [[ -e "$p" ]] && { print "$p"; return 0; }
    done
    return 1
}

# 현재 디렉터리가 어느 프로젝트인지 보고 기본 포트를 고른다.
_gts_default_port() {
    case "${PWD:t}" in
        GTS_CONSOLE_ESP32P4) _gts_pick_port "$GTS_PORT_CONSOLE" ;;
        *)                   _gts_pick_port "$GTS_PORT_ESP32" ;;
    esac
}

# ── 프로젝트 이동 + 바로 실행 ────────────────────────────────────────
#
#   gts-console              그 폴더로 이동만 (기존 동작)
#   gts-console build        이동 + 빌드
#   gts-console flash        이동 + 플래시
#   gts-console fm           이동 + 빌드 + 플래시 + 모니터   ← 가장 많이 씀
#   gts-console mon          이동 + 모니터만
#   gts-console clean        이동 + build/ 삭제
#
# 포트는 이동한 폴더를 보고 고른다 (콘솔은 P4, 나머지는 ESP32).
_gts_proj() {
    local dir="$1"; shift
    cd "$dir" || { _gts_err "폴더 없음: $dir"; return 1 }
    (( $# == 0 )) && return 0                   # 이동만

    local act="$1"; shift
    case "$act" in
        build)        gts-build "$@" ;;
        flash|dl)     gts-flash "$@" ;;
        mon|monitor)  gts-mon   "$@" ;;
        fm|all)       gts-fm    "$@" ;;
        clean)        gts-clean ;;
        *)  _gts_err "모르는 동작: $act"
            print "   쓸 수 있는 것: build | flash | mon | fm | clean"
            return 1 ;;
    esac
}

gts()         { cd "$GTS_ROOT" }
gts-console() { _gts_proj "$GTS_ROOT/GTS_CONSOLE_ESP32P4" "$@" }
gts-gfc()     { _gts_proj "$GTS_ROOT/gts_gfc_udp"        "$@" }
gts-aos()     { _gts_proj "$GTS_ROOT/gts_aos_bridge"     "$@" }
gts-srv()     { cd "$GTS_ROOT/server" }
gts-doc()     { cd "$GTS_ROOT/DOC" }

# ── 빌드 / 플래시 / 모니터 ───────────────────────────────────────────
gts-build() { _gts_need_idf || return 1; idf.py build "$@" }

gts-flash() {
    _gts_need_idf || return 1
    local port="${1:-$(_gts_default_port)}"
    [[ -z "$port" ]] && { _gts_err "포트를 못 찾음 — gts-ports 로 확인"; return 1 }
    _gts_step "flash → $port"
    idf.py -p "$port" flash
}

gts-mon() {
    _gts_need_idf || return 1
    local port="${1:-$(_gts_default_port)}"
    [[ -z "$port" ]] && { _gts_err "포트를 못 찾음 — gts-ports 로 확인"; return 1 }
    _gts_step "monitor → $port   (종료: Ctrl+])"
    idf.py -p "$port" monitor
}

# 빌드 + 플래시 + 모니터 한 방에
gts-fm() {
    _gts_need_idf || return 1
    local port="${1:-$(_gts_default_port)}"
    [[ -z "$port" ]] && { _gts_err "포트를 못 찾음 — gts-ports 로 확인"; return 1 }
    _gts_step "build + flash + monitor → $port   (종료: Ctrl+])"
    idf.py -p "$port" flash monitor
}

# build/ 를 지우고 다시 — 프로젝트를 옮겨 왔거나 캐시가 꼬였을 때.
# (옛 경로가 CMake 캐시에 남으면 picolibc.specs 를 두 번 읽어 죽는다)
gts-clean() {
    [[ -f CMakeLists.txt ]] || { _gts_err "여기는 IDF 프로젝트가 아니다: $PWD"; return 1 }
    _gts_step "rm -rf build  ($PWD)"
    rm -rf build
    _gts_ok "지웠다. 이제 gts-build"
}

# ══════════════════════════════════════════════════════════════════════
#  UDP 서버 (gts@192.168.0.6)
# ══════════════════════════════════════════════════════════════════════

gts-ssh()  { _gts_ssh "$GTS_SRV" "$@" }

# 실시간 로그
gts-log()  { _gts_ssh -t "$GTS_SRV" "journalctl -u gts-udp -f" }

# 최근 N줄 (기본 50)
gts-log-n() { _gts_ssh "$GTS_SRV" "journalctl -u gts-udp -n ${1:-50} --no-pager" }

gts-status()  { _gts_ssh "$GTS_SRV" "systemctl status gts-udp --no-pager" }
# 웹 API (gts_server.py, :8081) — 2026-09-23
gts-api()     { curl -s "http://$GTS_SRV_HOST:$GTS_HTTP_PORT/api/health"; echo }
gts-docs()    { open "http://$GTS_SRV_HOST:$GTS_HTTP_PORT/docs" }
gts-token()   { _gts_ssh "$GTS_SRV" "cd $GTS_SRV_DIR && venv/bin/python manage.py ${*:-list}" }
# PCSW Twin .dat → Single 변환 → 서버 DB 적재.  gts-import [폴더]  (기본 DOC/example_data)
# 임포트 Air(Ref) 정리 — 임포트 Air 를 모두 지우고 air1~air4 만 다시 올림 (2026-09-23)
#   gts-air-reset --dry-run   먼저 확인 /   gts-air-reset   실행
gts-air-reset() {
    gts-connect || return 1
    _gts_ssh "$GTS_SRV" "cd $GTS_SRV_DIR && venv/bin/python air_reset.py ~/import $*"
}

gts-import() {
    local dir="${1:-$GTS_ROOT/DOC/example_data}"
    [[ -d "$dir" ]] || { _gts_err "$dir 없음"; return 1 }
    gts-connect || return 1
    _gts_step "① 전송  $dir → ~/import/"
    _gts_ssh "$GTS_SRV" "mkdir -p ~/import" && _gts_scp "$dir"/*.dat "$GTS_SRV:~/import/" \
        || { _gts_err "scp 실패"; return 1 }
    _gts_step "② 변환 + 적재 (이미 들어간 파일은 건너뜀)"
    _gts_ssh "$GTS_SRV" "cd $GTS_SRV_DIR && venv/bin/python import_dat.py ~/import/"
}
# sudo 가 비밀번호를 물어볼 수 있으므로 -t 로 TTY 를 준다.
gts-restart() { _gts_ssh -t "$GTS_SRV" "sudo systemctl restart gts-udp" && _gts_ssh "$GTS_SRV" "sleep 1; systemctl is-active gts-udp" }
gts-stop()    { _gts_ssh -t "$GTS_SRV" "sudo systemctl stop gts-udp" }
gts-start()   { _gts_ssh -t "$GTS_SRV" "sudo systemctl start gts-udp" && _gts_ssh "$GTS_SRV" "sleep 1; systemctl is-active gts-udp" }

# 서비스 제어만 비밀번호 없이 — 서버에서 한 번 실행하면 된다.
# (전체 sudo 를 여는 게 아니라 gts-udp 서비스 3개 명령만 연다)
gts-nopasswd() {
cat <<'NOPW'
서버에 접속해서 한 번만 실행:

  gts-ssh
  SC=$(command -v systemctl)          # 보통 /usr/bin/systemctl
  echo "gts ALL=(ALL) NOPASSWD: $SC restart gts-udp, $SC start gts-udp, $SC stop gts-udp" \
      | sudo tee /etc/sudoers.d/gts-udp
  sudo chmod 440 /etc/sudoers.d/gts-udp
  sudo visudo -c                      # 문법 검사 — "parsed OK" 확인
  exit

그 뒤로는 gts-push / gts-restart 가 비밀번호를 안 묻는다.
되돌리려면:  sudo rm /etc/sudoers.d/gts-udp
NOPW
}

# 서버 소스 배포 — 백업 → 전송 → 테스트 → (통과 시에만) 재시작
#
# scp 가 기존 udp_server.py 를 덮어쓰므로 백업이 먼저다.
gts-push() {
    local src="$GTS_ROOT/server"
    [[ -d "$src" ]] || { _gts_err "$src 없음"; return 1 }

    gts-connect || return 1        # 비밀번호는 여기서 한 번만

    local stamp=$(date +%Y%m%d_%H%M)
    _gts_step "① 서버에 백업  (udp_server_$stamp.py)"
    _gts_ssh "$GTS_SRV" "cd $GTS_SRV_DIR && cp -n udp_server.py udp_server_$stamp.py 2>/dev/null; ls -1 udp_server*.py | tail -3" \
        || { _gts_err "백업 실패 — 중단"; return 1 }

    _gts_step "② 전송"
    local -a files=("$src"/*.py(N))
    (( ${#files} )) || { _gts_err "$src 에 .py 파일이 없다"; return 1 }
    _gts_scp "${files[@]}" "$GTS_SRV:$GTS_SRV_DIR/" \
        || { _gts_err "scp 실패"; return 1 }
    # DB / API (gts_server.py) 에 필요한 폴더와 파일 — 2026-09-23 추가
    local -a extra=()
    for d in gts db deploy web requirements.txt; do
        [[ -e "$src/$d" ]] && extra+=("$src/$d")
    done
    if (( ${#extra} )); then
        _gts_scp -r "${extra[@]}" "$GTS_SRV:$GTS_SRV_DIR/" \
            || { _gts_err "scp (gts/ db/ deploy/) 실패"; return 1 }
    fi

    _gts_step "③ 패킷 테스트"
    if ! _gts_ssh "$GTS_SRV" "cd $GTS_SRV_DIR && venv/bin/python test_packets.py 2>&1 | tail -3"; then
        _gts_err "테스트 실패 — 서비스를 재시작하지 않았다"
        _gts_warn "되돌리려면: gts-rollback $stamp"
        return 1
    fi

    # sudo 는 비밀번호를 물어볼 수 있다. ssh 에 -t 로 TTY 를 줘야
    # "sudo: no tty present and no askpass program specified" 를 피한다.
    _gts_step "④ 서비스 재시작"
    if ! _gts_ssh -t "$GTS_SRV" "sudo systemctl restart gts-udp"; then
        _gts_err "restart 명령 자체가 실패 (sudo 비밀번호 / 권한)"
        _gts_warn "비밀번호 없이 쓰려면: gts-nopasswd 참고"
        _gts_warn "되돌리려면: gts-rollback $stamp"
        return 1
    fi

    # 상태 확인은 TTY 가 필요 없다. 기동에 잠깐 걸릴 수 있어 세 번 본다.
    local st=""
    for i in 1 2 3; do
        st=$(_gts_ssh "$GTS_SRV" "systemctl is-active gts-udp" 2>/dev/null)
        [[ "$st" == "active" ]] && break
        sleep 1
    done

    if [[ "$st" != "active" ]]; then
        _gts_err "서비스가 안 올라옴 (상태: ${st:-unknown})"
        print ""
        _gts_ssh "$GTS_SRV" "journalctl -u gts-udp -n 25 --no-pager"
        print ""
        _gts_warn "되돌리려면: gts-rollback $stamp"
        return 1
    fi

    _gts_ok "배포 완료.  로그: gts-log"
}

# 백업으로 되돌리기.  gts-rollback              → 목록만 보여 준다
#                     gts-rollback 20260921_1430 → 그 버전으로 복구
gts-rollback() {
    if [[ -z "$1" ]]; then
        _gts_step "서버의 백업 목록"
        _gts_ssh "$GTS_SRV" "cd $GTS_SRV_DIR && ls -1t udp_server_*.py udp_server.py.bak 2>/dev/null | head -10"
        _gts_warn "사용법: gts-rollback 20260921_1430"
        return 0
    fi
    _gts_ssh -t "$GTS_SRV" "cd $GTS_SRV_DIR && cp udp_server_$1.py udp_server.py && sudo systemctl restart gts-udp" \
        && _gts_ssh "$GTS_SRV" "sleep 1; systemctl is-active gts-udp" \
        && _gts_ok "udp_server_$1.py 로 복구"
}

# 서버에서 파일 가져오기 (서버에서 직접 고친 걸 회수)
gts-pull() {
    local f="${1:-udp_server.py}"
    _gts_scp "$GTS_SRV:$GTS_SRV_DIR/$f" "$GTS_ROOT/server/" \
        && _gts_ok "server/$f 로 받았다"
}

# 서비스가 안 뜰 때 필요한 것을 한 번에 긁어 온다.
gts-diag() {
    print -P "%F{cyan}── systemctl status ──%f"
    _gts_ssh "$GTS_SRV" "systemctl status gts-udp --no-pager -l | head -20"
    print -P "\n%F{cyan}── 최근 로그 40줄 ──%f"
    _gts_ssh "$GTS_SRV" "journalctl -u gts-udp -n 40 --no-pager"
    print -P "\n%F{cyan}── 포트를 누가 잡고 있나 ──%f"
    _gts_ssh "$GTS_SRV" "ss -lunp 2>/dev/null | grep -E '5500|5501|5502' || echo '(아무도 안 잡고 있음)'"
    print -P "\n%F{cyan}── 파이썬 / 파일 ──%f"
    _gts_ssh "$GTS_SRV" "cd $GTS_SRV_DIR && venv/bin/python -V && ls -lt *.py | head -6"
    print -P "\n%F{cyan}── 직접 실행해 보기 (5초) ──%f"
    _gts_ssh "$GTS_SRV" "cd $GTS_SRV_DIR && timeout 5 venv/bin/python -u udp_server.py 2>&1 | head -30"
}

# 지금 서버에 어느 버전이 돌고 있는지 (5500 포트 취급으로 구분)
gts-which() {
    _gts_step "서버 기동 배너"
    _gts_ssh "$GTS_SRV" "journalctl -u gts-udp --no-pager | grep -E '5500 :' | tail -1"
    print ""
    print "  'AOS Sensor   (raw log)'      → 구 버전, AOS 미지원"
    print "  'AOS          (binary ...)'   → 신 버전"
}

# ── 시뮬레이터 (서버에서 실행) ───────────────────────────────────────
# 서버가 5500/5501 을 이미 잡고 있으므로 로컬 포트를 비켜 준다.
gts-sim-aos() { _gts_ssh -t "$GTS_SRV" "cd $GTS_SRV_DIR && venv/bin/python -u aos_sim.py 127.0.0.1 ${1:-1} 6500" }
gts-sim-gfc() { _gts_ssh -t "$GTS_SRV" "cd $GTS_SRV_DIR && venv/bin/python -u gfc_sim.py 127.0.0.1 ${1:-1} 6501" }
gts-sim-con() { _gts_ssh -t "$GTS_SRV" "cd $GTS_SRV_DIR && venv/bin/python -u console_sim.py 127.0.0.1 ${1:-1}" }

# ── 도움말 ───────────────────────────────────────────────────────────
gts-help() {
cat <<'HELP'
AOS_GTS 작업 명령

 펌웨어  —  프로젝트 이름 뒤에 동작을 붙인다
   gts-console [동작]   ESP32-P4 콘솔   (포트 자동: /dev/cu.usbmodem*)
   gts-gfc     [동작]   GFC 브리지      (포트 자동: /dev/cu.usbserial-*)
   gts-aos     [동작]   AOS 브리지      (포트 자동: /dev/cu.usbserial-*)

     동작 없음   그 폴더로 이동만
     build       빌드
     flash       플래시 (dl 도 됨)
     fm          빌드 + 플래시 + 모니터   ← 가장 많이 쓴다
     mon         모니터만            (종료 Ctrl+])
     clean       build/ 삭제

     예)  gts-console fm
          gts-aos fm /dev/cu.usbserial-0001      ← 포트를 직접 줄 때

   idf                IDF 환경 로드 (터미널마다 한 번, conda 자동 해제)
   gts-idf-install    IDF python 환경 + 툴체인 설치 (처음 한 번)
   gts-ports          꽂혀 있는 USB 시리얼 포트
   gts-build / gts-flash / gts-mon / gts-fm / gts-clean
                      현재 폴더에 대해 직접 (위 동작과 같은 것)

 서버  gts@192.168.0.6
   gts-push       백업 → scp → 테스트 → 재시작  (권장 경로)
   gts-log        실시간 로그        gts-log-n [N]   최근 N줄
   gts-which      지금 도는 서버가 신/구 버전인지
   gts-diag       서비스가 안 뜰 때 — status·로그·포트·직접실행 한 번에
   gts-rollback [스탬프]   백업 목록 / 복구
   gts-pull [파일]         서버에서 회수
   gts-status / gts-restart / gts-stop / gts-start
   gts-ssh        그냥 접속
   gts-api        웹 API 상태 (:8081/api/health)     gts-docs  API 문서 열기
   gts-air-reset  임포트 Air(Ref) 정리 → air1~air4 (--dry-run 먼저)
   gts-token [명령]  토큰 관리 — list / create-admin 이름 / token 이름 read|control|admin / revoke ID
   gts-import [폴더] Twin .dat → Single → DB 적재 (기본 DOC/example_data)
   gts-connect    마스터 연결 열기 (비밀번호 1회)   gts-disconnect  끊기
   gts-ssh-key    공개키 등록 — 비밀번호를 아예 안 묻게 (권장, 한 번만)
   gts-nopasswd   sudo 비밀번호 면제 설정 방법 출력

 시뮬레이터 (서버에서 실행, 장비 없이 검증)
   gts-sim-aos [ID]   gts-sim-gfc [ID]   gts-sim-con [ID]

 Git — 기기 간 동기화 (회사 iMac ↔ 집 MacBook)
   gts-sync             작업 시작: GitHub 에서 받기
   gts-save ["메시지"]  작업 끝: add + commit + push (메시지 없으면 기기·시각)
   gts-st               안 올린 변경 / 안 받은 커밋 확인
   ※ gts-push·gts-pull 은 서버용, gts-save·gts-sync 는 GitHub 용

 이동만
   gts   루트        gts-srv   server/        gts-doc   DOC/

 새 기계 / 장소가 바뀌었을 때
   gts-doctor     환경 점검 — 프로젝트·IDF·포트·서버까지 한 번에
   gts-site       지금 서버 주소 + 닿는지 확인
   gts-site home     서버를 218.147.152.41 로  (집·외부)
   gts-site office   서버를 192.168.0.6 으로   (회사 LAN)
   gts-icloud     iCloud 파일 실체 내려받기
   gts-idf-install   ESP-IDF python 환경 설치

   기계마다 다른 값(IDF 경로·시리얼 포트)은 ~/.gts_env.local 에 적는다.
   그 파일은 iCloud 밖이라 맥마다 따로 있고, 기본값을 덮어쓴다.

 하루 시작할 때
   gts-sync                          ← 먼저 GitHub 에서 받기
   gts-doctor                        ← 자리를 옮겼으면
   idf                               ← 터미널마다 한 번
   gts-aos fm                        ← 펌웨어
   gts-push ; gts-log                ← 서버

 하루 끝낼 때
   gts-save "오늘 한 일"              ← 자리 뜨기 전에 꼭
HELP
}

# ══════════════════════════════════════════════════════════════════════
#  기계·장소별 설정
#
#  이 파일(gts_env.zsh)은 iCloud 에 있어서 모든 맥이 같은 내용을 본다.
#  그런데 기계마다 달라야 하는 것들이 있다 — ESP-IDF 설치 경로, 시리얼 포트,
#  그리고 "지금 어디에 있는가"(회사 LAN vs 집).
#
#  그런 값은 ~/.gts_env.local 에 둔다. 이 파일은 iCloud 밖이라 기계마다
#  따로 존재하고, 아래에서 마지막에 읽으므로 위의 기본값을 덮어쓴다.
# ══════════════════════════════════════════════════════════════════════

export GTS_LOCAL="$HOME/.gts_env.local"
[[ -f "$GTS_LOCAL" ]] && source "$GTS_LOCAL"
export GTS_SRV="$GTS_SRV_USER@$GTS_SRV_HOST"     # 덮어쓴 값으로 다시 계산

# ── 장소 전환 ────────────────────────────────────────────────────────
#   gts-site            지금 설정 + 닿는지 확인
#   gts-site office     192.168.0.6      (같은 LAN)
#   gts-site home       218.147.152.41   (외부, 포트포워딩)
gts-site() {
    local host port
    case "${1:-}" in
        office|회사) host="192.168.0.6";       port="22" ;;
        home|집)     host="218.147.152.41";    port="${GTS_SRV_EXT_PORT:-22}" ;;
        "")
            print -P "%F{cyan}현재%f  서버 $GTS_SRV_HOST:$GTS_SRV_PORT   웹 $GTS_HTTP_PORT"
            _gts_step "닿는지 확인 중 …"
            if nc -z -G 3 "$GTS_SRV_HOST" "$GTS_SRV_PORT" 2>/dev/null; then
                _gts_ok "SSH $GTS_SRV_HOST:$GTS_SRV_PORT 열려 있다"
            else
                _gts_err "SSH $GTS_SRV_HOST:$GTS_SRV_PORT 안 닿는다"
            fi
            if curl -s -m 4 -o /dev/null "http://$GTS_SRV_HOST:$GTS_HTTP_PORT/api/health"; then
                _gts_ok "웹/API http://$GTS_SRV_HOST:$GTS_HTTP_PORT 응답"
            else
                _gts_err "웹/API http://$GTS_SRV_HOST:$GTS_HTTP_PORT 무응답"
            fi
            print ""
            print "  바꾸려면:  gts-site office   |   gts-site home"
            return 0 ;;
        *) _gts_err "office 또는 home"; return 1 ;;
    esac

    export GTS_SRV_HOST="$host" GTS_SRV_PORT="$port"
    export GTS_SRV="$GTS_SRV_USER@$GTS_SRV_HOST"

    # ~/.gts_env.local 에 기록해 다음 터미널에도 유지되게 한다
    touch "$GTS_LOCAL"
    local tmp=$(mktemp)
    grep -v '^export GTS_SRV_HOST=\|^export GTS_SRV_PORT=' "$GTS_LOCAL" > "$tmp"
    print "export GTS_SRV_HOST=\"$host\"" >> "$tmp"
    print "export GTS_SRV_PORT=\"$port\"" >> "$tmp"
    mv "$tmp" "$GTS_LOCAL"

    _gts_ok "서버 = $GTS_SRV_HOST:$GTS_SRV_PORT  ($GTS_LOCAL 에 저장)"
    gts-disconnect 2>/dev/null    # 이전 주소로 열린 마스터 연결은 버린다
}

# ── iCloud 파일 내려받기 ─────────────────────────────────────────────
# 새 맥에서는 iCloud 파일이 "이름만" 있고 실제 내용이 없을 수 있다.
# 그 상태로 빌드하면 파일을 못 찾는다는 엉뚱한 에러가 난다.
gts-icloud() {
    _gts_step "iCloud 실체 내려받기 — $GTS_ROOT"
    if command -v brctl >/dev/null 2>&1; then
        brctl download "$GTS_ROOT" 2>/dev/null
    fi
    # brctl 이 폴더를 재귀로 안 훑는 경우가 있어 한 번 더 훑어 준다
    find "$GTS_ROOT" -name '.*.icloud' -print 2>/dev/null | head -5
    local n=$(find "$GTS_ROOT" -name '*.icloud' 2>/dev/null | wc -l | tr -d ' ')
    if [[ "$n" == "0" ]]; then
        _gts_ok "전부 내려받아져 있다"
    else
        _gts_warn "$n 개가 아직 클라우드에만 있다 — Finder 에서 폴더를 한 번 열어 볼 것"
    fi
}

# ── 환경 점검 ────────────────────────────────────────────────────────
# 새 기계에서 처음 쓸 때, 또는 뭔가 안 될 때 이것부터.
gts-doctor() {
    local bad=0
    print -P "%F{cyan}── 프로젝트 ──%f"
    if [[ -d "$GTS_ROOT" ]]; then
        _gts_ok "GTS_ROOT  $GTS_ROOT"
        for d in GTS_CONSOLE_ESP32P4 gts_gfc_udp gts_aos_bridge server DOC tools; do
            [[ -d "$GTS_ROOT/$d" ]] || { _gts_err "  없음: $d"; bad=1 }
        done
        local ic=$(find "$GTS_ROOT" -name '*.icloud' 2>/dev/null | wc -l | tr -d ' ')
        [[ "$ic" != "0" ]] && { _gts_warn "iCloud 미다운로드 $ic 개 — gts-icloud"; bad=1 }
    else
        _gts_err "GTS_ROOT 없음: $GTS_ROOT"
        _gts_warn "iCloud 동기화가 아직 안 끝났거나 경로가 다르다"
        bad=1
    fi

    print -P "\n%F{cyan}── ESP-IDF ──%f"
    if command -v idf.py >/dev/null 2>&1; then
        _gts_ok "이 셸에 이미 로드됨 ($IDF_PATH)"
    elif [[ -f "$GTS_IDF/export.sh" ]]; then
        _gts_ok "설치돼 있음 — $GTS_IDF   (쓰려면 'idf')"
        local venv=$(_gts_find_idf_venv)
        if [[ -n "$venv" ]]; then
            _gts_ok "  python 환경 $venv"
        else
            _gts_err "  python 환경 없음 → gts-idf-install"
            bad=1
        fi
    else
        _gts_err "ESP-IDF 없음 — $GTS_IDF"
        _gts_warn "  다른 곳에 설치했으면 ~/.gts_env.local 에 GTS_IDF 를 적을 것"
        _gts_warn "  아직 안 깔았으면 espressif 문서대로 설치 후 gts-idf-install"
        bad=1
    fi
    [[ -n "$CONDA_DEFAULT_ENV" ]] && \
        _gts_warn "conda($CONDA_DEFAULT_ENV) 활성 — idf 가 자동 해제하지만, 끄려면 conda config --set auto_activate_base false"

    print -P "\n%F{cyan}── 시리얼 포트 ──%f"
    gts-ports

    print -P "\n%F{cyan}── 서버 $GTS_SRV_HOST ──%f"
    local ssh_ok=0 web_ok=0
    if nc -z -G 3 "$GTS_SRV_HOST" "$GTS_SRV_PORT" 2>/dev/null; then
        _gts_ok "SSH $GTS_SRV_PORT 열림"
        ssh_ok=1
    else
        _gts_err "SSH $GTS_SRV_HOST:$GTS_SRV_PORT 안 닿음"
    fi
    if curl -s -m 4 -o /dev/null "http://$GTS_SRV_HOST:$GTS_HTTP_PORT/api/health"; then
        _gts_ok "웹/API http://$GTS_SRV_HOST:$GTS_HTTP_PORT"
        web_ok=1
    else
        _gts_err "웹/API 무응답"
    fi

    print ""
    if (( ssh_ok && web_ok )); then
        (( bad )) && _gts_warn "위의 ✘ 부터 해결할 것" || _gts_ok "전부 정상"
    elif (( web_ok )); then
        # 웹은 되는데 SSH 만 안 된다 = 공유기에 22번이 포워딩 안 돼 있다.
        # 주소가 틀린 게 아니므로 gts-site 로는 해결되지 않는다.
        _gts_warn "웹은 되는데 SSH 만 안 된다 — 공유기에 SSH 포트가 열려 있지 않다"
        print ""
        print "  지금 할 수 있는 것"
        print "    · 펌웨어 빌드·플래시   idf ; gts-aos fm"
        print "    · 웹으로 확인          http://$GTS_SRV_HOST:$GTS_HTTP_PORT/"
        print "  할 수 없는 것 (SSH 필요)"
        print "    · gts-push / gts-log / gts-token / gts-import / 시뮬레이터"
        print ""
        print "  열려면 회사 공유기에서 외부포트 → 192.168.0.6:22 포워딩."
        print "  22 말고 다른 번호로 열었다면 ~/.gts_env.local 에"
        print "    export GTS_SRV_EXT_PORT=\"2222\"      ← 그 번호"
        print "  를 적고 gts-site home 을 다시 하면 된다."
        bad=1
    else
        _gts_warn "서버에 전혀 안 닿는다"
        print "    집이면 gts-site home · 회사면 gts-site office · 그래도 안 되면 네트워크 확인"
        bad=1
    fi
    return $bad
}

# ══════════════════════════════════════════════════════════════════════
#  Git — 기기 간 동기화 (회사 iMac ↔ 집 MacBook)
#
#   gts-sync            작업 시작: GitHub 에서 받기 (pull)
#   gts-save ["메시지"]  작업 끝:   add + commit + push
#   gts-st              지금 상태 (안 올린 변경 / 안 받은 커밋)
#
#  서버용 gts-push / gts-pull 과 이름이 다르니 헷갈리지 말 것.
#  (gts-push = 서버에 배포, gts-save = GitHub 에 저장)
# ══════════════════════════════════════════════════════════════════════

_gts_git() { git -C "$GTS_ROOT" "$@" }

_gts_need_git() {
    if ! _gts_git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        _gts_err "git 저장소가 아니다: $GTS_ROOT"
        return 1
    fi
}

gts-st() {
    _gts_need_git || return 1
    _gts_git fetch -q origin 2>/dev/null || _gts_warn "GitHub 에 못 닿음 — 로컬 상태만 표시"
    _gts_git status -sb
}

gts-sync() {
    _gts_need_git || return 1
    local before=$(_gts_git rev-parse HEAD)

    if [[ -n "$(_gts_git status --porcelain)" ]]; then
        _gts_warn "커밋 안 된 변경이 있다 (받기는 계속 진행)"
        _gts_git status --short | head -20
    fi

    _gts_step "GitHub 에서 받는 중…"
    if ! _gts_git pull --ff-only; then
        _gts_err "자동으로 합칠 수 없다"
        print "  다른 기기에서 올린 것과 여기서 커밋한 것이 갈라진 상태일 가능성이 크다."
        print "    gts-st                               ← 상태 확인"
        print "    git -C \"\$GTS_ROOT\" pull --rebase    ← 내 커밋을 위로 올려 합치기"
        print "  같은 줄을 양쪽에서 고쳤으면 충돌 표시가 나온다 — 그때는 물어볼 것."
        return 1
    fi

    local after=$(_gts_git rev-parse HEAD)
    if [[ "$before" == "$after" ]]; then
        _gts_ok "이미 최신"
    else
        _gts_ok "받은 커밋:"
        _gts_git log --oneline "$before..$after"
    fi
}

gts-save() {
    _gts_need_git || return 1
    local msg="$*"
    [[ -z "$msg" ]] && msg="작업 저장 — ${HOST%%.*} $(date '+%Y-%m-%d %H:%M')"

    _gts_git add -A

    # 비밀 파일이 실수로 들어가는 것을 막는다 (.gitignore 가 1차 방어)
    local bad=$(_gts_git diff --cached --name-only | grep -E '(^|/)(wifi_secrets\.h|\.env)$')
    if [[ -n "$bad" ]]; then
        _gts_err "비밀 파일이 커밋에 들어가려 한다 — 중단"
        print "$bad" | sed 's/^/    /'
        _gts_git reset -q
        return 1
    fi

    if [[ -n "$(_gts_git diff --cached --name-only)" ]]; then
        _gts_step "커밋할 변경:"
        _gts_git diff --cached --stat | tail -15
        _gts_git commit -q -m "$msg" || { _gts_err "커밋 실패"; return 1 }
        _gts_ok "커밋: $msg"
    else
        _gts_ok "새로 커밋할 변경 없음"
    fi

    _gts_step "GitHub 에 올리는 중…"
    if ! _gts_git push -q; then
        _gts_err "push 실패"
        print "  다른 기기에서 먼저 올린 것이 있으면 이렇게 된다."
        print "    gts-sync  →  gts-save   순서로 다시"
        return 1
    fi
    _gts_ok "GitHub 에 저장됨 — 다른 기기에서 gts-sync 로 받으면 된다"
}

# 로드 확인용 한 줄 (조용히 하고 싶으면 이 줄을 지울 것)
_gts_ok "AOS_GTS 환경 로드됨 — 명령 목록은 %Bgts-help%b"
