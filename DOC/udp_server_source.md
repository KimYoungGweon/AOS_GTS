# UDP Server 설치 및 소스 정리

파일명: `udp_server_source.md`

## 1. 서버 구성 개요

현재 GTS(Gas Training System)용 UDP 서버는 다음 환경으로 구성되어 있습니다.

- 서버 PC: BOSGAME Mini PC
- CPU: Ryzen 5 계열
- RAM: 16 GB
- SSD: 500 GB
- OS: Ubuntu Server 24.04 LTS
- Hostname: `gts-server`
- 사용자 계정: `gts`
- 유선 LAN 인터페이스: `enp4s0`
- 서버 고정 IP: `192.168.0.6`
- Gateway: `192.168.0.1`
- DNS:
  - `168.126.63.1`
  - `168.126.63.2`
- Time zone: `Asia/Seoul`
- SSH 접속 가능
- UDP Server는 `systemd` 서비스로 자동 실행

UDP 포트 구성:

| Port | Device | 용도 |
|---:|---|---|
| 5500 | AOS Sensor | AOS 센서 데이터 및 명령 |
| 5501 | GFC | Gas Flow Controller 데이터 및 명령 |
| 5502 | Console | Console 명령 및 상태 데이터 |

---

## 2. 현재 서버 접속 방법

Mac 터미널에서 다음 명령으로 접속합니다.

```bash
ssh gts@192.168.0.6
```

처음 접속하는 경우 fingerprint 확인 메시지가 나오면:

```text
yes
```

를 입력한 후 `gts` 계정 비밀번호를 입력합니다.

정상 접속 시:

```text
gts@gts-server:~$
```

형태의 프롬프트가 나타납니다.

---

## 3. 네트워크 설정

현재 서버 IP는 고정 IP로 설정되어 있습니다.

Netplan 설정 파일:

```text
/etc/netplan/01-gts-network.yaml
```

현재 설정 내용:

```yaml
network:
  version: 2
  ethernets:
    enp4s0:
      dhcp4: false
      addresses:
        - 192.168.0.6/24
      routes:
        - to: default
          via: 192.168.0.1
      nameservers:
        addresses:
          - 168.126.63.1
          - 168.126.63.2
```

설정 파일 수정:

```bash
sudo nano /etc/netplan/01-gts-network.yaml
```

문법 검사:

```bash
sudo netplan generate
```

SSH 연결 상태에서 안전하게 적용:

```bash
sudo netplan try
```

직접 적용:

```bash
sudo netplan apply
```

현재 IP 확인:

```bash
ip -br addr
```

Routing 확인:

```bash
ip route
```

DNS 확인:

```bash
resolvectl status
```

---

## 4. SSD / LVM 구성

Ubuntu 설치 직후 SSD 전체는 약 476.9 GB로 인식되었지만,
Ubuntu root logical volume은 약 100 GB만 사용하고 있었습니다.

확장 전 예:

```text
/dev/mapper/ubuntu--vg-ubuntu--lv   98G
```

다음 명령으로 LVM의 남은 공간 전체를 `/` 파일시스템에 확장하였습니다.

```bash
sudo lvextend -l +100%FREE -r /dev/mapper/ubuntu--vg-ubuntu--lv
```

확인:

```bash
df -h /
```

현재 root filesystem은 약 466 GB 수준으로 확장되어 있습니다.

디스크 확인 명령:

```bash
df -h
```

```bash
lsblk
```

---

## 5. 시간 / NTP 설정

현재 Time Zone:

```text
Asia/Seoul
```

설정 명령:

```bash
sudo timedatectl set-timezone Asia/Seoul
```

확인:

```bash
timedatectl
```

정상 상태 예:

```text
Time zone: Asia/Seoul (KST, +0900)
System clock synchronized: yes
NTP service: active
RTC in local TZ: no
```

`RTC in local TZ: no`는 정상입니다.
하드웨어 RTC는 UTC를 사용하고 Ubuntu에서 KST로 변환합니다.

---

# 6. UDP Server 프로그램 구조

UDP 서버 프로젝트 디렉터리는 다음 위치입니다.

```text
/home/gts/gts_udp_server
```

즉 `gts` 계정 기준으로:

```bash
~/gts_udp_server
```

프로젝트로 이동:

```bash
cd ~/gts_udp_server
```

현재 디렉터리 확인:

```bash
pwd
```

예:

```text
/home/gts/gts_udp_server
```

파일 목록 확인:

```bash
ls -l
```

주요 파일:

```text
udp_server.py
venv/
```

---

# 7. Python 환경

Ubuntu에서 다음 패키지를 설치했습니다.

```bash
sudo apt install -y python3 python3-venv
```

프로젝트 디렉터리 생성:

```bash
mkdir -p ~/gts_udp_server
cd ~/gts_udp_server
```

Python virtual environment 생성:

```bash
python3 -m venv venv
```

수동 실행 시 virtual environment 활성화:

```bash
source venv/bin/activate
```

활성화되면 프롬프트 앞에 `(venv)`가 나타납니다.

```text
(venv) gts@gts-server:~/gts_udp_server$
```

가상환경 종료:

```bash
deactivate
```

참고로 현재 `systemd` 서비스에서는 virtual environment를 수동으로 activate하지 않습니다.
대신 아래 Python 실행 파일을 직접 지정합니다.

```text
/home/gts/gts_udp_server/venv/bin/python
```

---

# 8. 현재 동작 중인 UDP Server Source

현재 사용 중인 소스 파일:

```text
/home/gts/gts_udp_server/udp_server.py
```

현재 소스는 아래와 같습니다.

```python
import asyncio
from datetime import datetime

PORTS = {
    5500: "AOS",
    5501: "GFC",
    5502: "CONSOLE",
}

known_devices = set()


class UDPServerProtocol(asyncio.DatagramProtocol):

    def __init__(self, port, device_type):
        self.port = port
        self.device_type = device_type
        self.transport = None

    def connection_made(self, transport):
        self.transport = transport

        print(
            f"[SERVER] {self.device_type} UDP server started "
            f"on port {self.port}"
        )

    def datagram_received(self, data, addr):

        now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        ip, source_port = addr

        device_key = (
            self.device_type,
            ip,
            source_port,
        )

        if device_key not in known_devices:
            known_devices.add(device_key)

            print(
                f"[{now}] "
                f"[{self.device_type}] "
                f"NEW DEVICE {ip}:{source_port}"
            )

        try:
            message = data.decode("utf-8").strip()

        except UnicodeDecodeError:
            message = "HEX:" + data.hex()

        print(
            f"[{now}] "
            f"[{self.device_type}] "
            f"{ip}:{source_port} -> {message}"
        )


async def main():

    print("")
    print("========================================")
    print("       GTS UDP SERVER")
    print("========================================")
    print("5500 : AOS Sensor")
    print("5501 : GFC")
    print("5502 : Console")
    print("========================================")
    print("")

    loop = asyncio.get_running_loop()

    transports = []

    for port, device_type in PORTS.items():

        transport, protocol = await loop.create_datagram_endpoint(
            lambda p=port, d=device_type: UDPServerProtocol(p, d),
            local_addr=("0.0.0.0", port),
        )

        transports.append(transport)

    try:
        await asyncio.Future()

    finally:
        for transport in transports:
            transport.close()


if __name__ == "__main__":
    try:
        asyncio.run(main())

    except KeyboardInterrupt:
        print("\n[SERVER] stopped")
```

---

# 9. UDP Server Source 동작 설명

## 9.1 PORTS

```python
PORTS = {
    5500: "AOS",
    5501: "GFC",
    5502: "CONSOLE",
}
```

각 UDP 포트와 장치 종류를 정의합니다.

예를 들어 5500번 포트로 데이터가 들어오면 서버 로그에는:

```text
[AOS]
```

로 표시됩니다.

5501은:

```text
[GFC]
```

5502는:

```text
[CONSOLE]
```

로 표시됩니다.

---

## 9.2 known_devices

```python
known_devices = set()
```

현재 서버가 이미 본 적 있는 장치의:

- Device Type
- IP Address
- UDP Source Port

조합을 기억합니다.

장치가 처음 패킷을 보내면:

```text
NEW DEVICE
```

를 출력합니다.

예:

```text
[2026-09-17 17:45:21] [AOS] NEW DEVICE 192.168.0.101:54321
```

중요:

UDP는 TCP와 달리 실제 connection 개념이 없습니다.

따라서 현재 프로그램의 `NEW DEVICE`는

```text
서버가 해당 IP:Port에서 처음 패킷을 받았다
```

라는 의미입니다.

실제 장비 식별은 향후 `DEVICE_ID`를 UDP 패킷 안에 넣는 방식이 더 적합합니다.

---

## 9.3 connection_made()

```python
def connection_made(self, transport):
```

UDP socket이 생성되면 한 번 호출됩니다.

예:

```text
[SERVER] AOS UDP server started on port 5500
```

---

## 9.4 datagram_received()

UDP packet을 받을 때마다 실행됩니다.

```python
def datagram_received(self, data, addr):
```

`data`

```text
수신된 실제 UDP payload
```

`addr`

```text
송신 장치의 (IP, Source Port)
```

예:

```python
("192.168.0.101", 54321)
```

---

## 9.5 문자열 데이터 처리

UTF-8 문자열이면:

```python
message = data.decode("utf-8").strip()
```

으로 변환합니다.

예:

```text
CURRENT,123.45
```

UTF-8로 변환할 수 없는 binary packet이면:

```python
message = "HEX:" + data.hex()
```

로 HEX 문자열로 표시합니다.

따라서 향후 MCU에서 binary UDP packet을 보내도 최소한 raw data를 확인할 수 있습니다.

---

## 9.6 asyncio 사용 이유

현재 UDP Server는 Python `asyncio`를 사용합니다.

한 프로그램 안에서:

```text
UDP 5500
UDP 5501
UDP 5502
```

세 포트를 동시에 처리합니다.

현재 GTS 구성처럼:

- AOS 10~20대
- GFC 여러 대
- Console 1~2대

수준에서는 충분히 처리 가능한 구조입니다.

---

# 10. UDP Server 수동 실행 방법

현재는 `systemd` 서비스로 자동 실행되고 있으므로,
일반적으로 수동 실행할 필요는 없습니다.

소스를 직접 테스트하려면 먼저 서비스와의 포트 충돌을 방지해야 합니다.

서비스 정지:

```bash
sudo systemctl stop gts-udp
```

프로젝트 디렉터리 이동:

```bash
cd ~/gts_udp_server
```

가상환경 활성화:

```bash
source venv/bin/activate
```

실행:

```bash
python udp_server.py
```

정상 실행 시:

```text
========================================
       GTS UDP SERVER
========================================
5500 : AOS Sensor
5501 : GFC
5502 : Console
========================================

[SERVER] AOS UDP server started on port 5500
[SERVER] GFC UDP server started on port 5501
[SERVER] CONSOLE UDP server started on port 5502
```

종료:

```text
Ctrl + C
```

테스트가 끝난 뒤 다시 systemd 서비스 시작:

```bash
sudo systemctl start gts-udp
```

---

# 11. UDP Server Source 수정 방법

소스 파일 열기:

```bash
nano ~/gts_udp_server/udp_server.py
```

또는:

```bash
cd ~/gts_udp_server
nano udp_server.py
```

Nano 주요 명령:

| 기능 | 키 |
|---|---|
| 저장 | `Ctrl + O` |
| 저장 확인 | `Enter` |
| 종료 | `Ctrl + X` |
| 검색 | `Ctrl + W` |
| 한 줄 잘라내기 | `Ctrl + K` |
| 붙여넣기 | `Ctrl + U` |

소스를 수정한 뒤에는 `systemd` 서비스를 반드시 재시작해야 합니다.

```bash
sudo systemctl restart gts-udp
```

그리고 상태 확인:

```bash
systemctl status gts-udp
```

실시간 로그 확인:

```bash
journalctl -u gts-udp -f
```

---

# 12. systemd 서비스 설정

서비스 파일:

```text
/etc/systemd/system/gts-udp.service
```

수정:

```bash
sudo nano /etc/systemd/system/gts-udp.service
```

현재 내용:

```ini
[Unit]
Description=GTS UDP Server
After=network.target

[Service]
Type=simple
User=gts
WorkingDirectory=/home/gts/gts_udp_server
ExecStart=/home/gts/gts_udp_server/venv/bin/python -u /home/gts/gts_udp_server/udp_server.py
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
```

중요한 부분:

```ini
ExecStart=/home/gts/gts_udp_server/venv/bin/python -u /home/gts/gts_udp_server/udp_server.py
```

여기서:

```text
-u
```

옵션이 매우 중요합니다.

Python 기본 `print()` 출력은 systemd 환경에서 buffering될 수 있습니다.

`-u`를 사용하면:

```text
unbuffered mode
```

로 실행되어 `print()` 결과가 즉시 `journalctl`에 나타납니다.

`-u`가 없을 경우 UDP packet은 정상 수신되어도 실시간 로그가 늦게 보일 수 있습니다.

---

# 13. systemd 서비스 관리 명령

서비스 시작:

```bash
sudo systemctl start gts-udp
```

서비스 정지:

```bash
sudo systemctl stop gts-udp
```

재시작:

```bash
sudo systemctl restart gts-udp
```

상태 확인:

```bash
systemctl status gts-udp
```

부팅 자동 실행 등록:

```bash
sudo systemctl enable gts-udp
```

자동 실행 해제:

```bash
sudo systemctl disable gts-udp
```

서비스 파일 수정 후에는 반드시:

```bash
sudo systemctl daemon-reload
```

를 실행합니다.

그 후:

```bash
sudo systemctl restart gts-udp
```

를 실행합니다.

---

# 14. 로그 확인 방법

## 실시간 로그

가장 자주 사용할 명령:

```bash
journalctl -u gts-udp -f
```

종료:

```text
Ctrl + C
```

---

## 최근 로그 확인

```bash
journalctl -u gts-udp
```

최근 50줄:

```bash
journalctl -u gts-udp -n 50
```

최근 100줄:

```bash
journalctl -u gts-udp -n 100
```

오늘 로그:

```bash
journalctl -u gts-udp --since today
```

최근 10분 로그:

```bash
journalctl -u gts-udp --since "10 minutes ago"
```

---

# 15. UDP 포트 Listen 상태 확인

현재 UDP 포트를 확인하려면:

```bash
ss -lunp
```

GTS 포트만 확인:

```bash
ss -lunp | grep -E '5500|5501|5502'
```

정상적인 경우 대략:

```text
0.0.0.0:5500
0.0.0.0:5501
0.0.0.0:5502
```

형태로 표시됩니다.

`0.0.0.0`은 모든 네트워크 인터페이스에서 해당 포트를 수신한다는 의미입니다.

---

# 16. Mac에서 UDP Test 방법

Mac에서 다음과 같이 테스트할 수 있습니다.

## AOS 5500

```bash
echo "AOS_TEST,123" | nc -u -w1 192.168.0.6 5500
```

## GFC 5501

```bash
echo "GFC_TEST,456" | nc -u -w1 192.168.0.6 5501
```

## Console 5502

```bash
echo "CONSOLE_TEST,789" | nc -u -w1 192.168.0.6 5502
```

서버에서는:

```bash
journalctl -u gts-udp -f
```

를 실행하여 패킷 수신 여부를 확인합니다.

예:

```text
[2026-09-17 17:45:21] [AOS] NEW DEVICE 192.168.0.52:54321
[2026-09-17 17:45:21] [AOS] 192.168.0.52:54321 -> AOS_TEST,123
```

---

# 17. UDP Server 수정 작업 권장 절차

향후 UDP 서버 코드를 수정할 때는 아래 순서를 권장합니다.

```text
1. source backup
2. service stop
3. source 수정
4. 수동 실행 테스트
5. UDP packet test
6. 수동 실행 종료
7. systemd service start
8. journalctl 확인
```

실제 명령 예:

```bash
cd ~/gts_udp_server
cp udp_server.py udp_server.py.bak
sudo systemctl stop gts-udp
nano udp_server.py
source venv/bin/activate
python udp_server.py
```

다른 Mac 터미널에서:

```bash
echo "AOS_TEST,123" | nc -u -w1 192.168.0.6 5500
```

테스트 완료 후 서버 프로그램 종료:

```text
Ctrl + C
```

서비스 재시작:

```bash
sudo systemctl start gts-udp
```

로그 확인:

```bash
journalctl -u gts-udp -f
```

---

# 18. Source Backup 방법

수정 전에 항상 백업하는 것을 권장합니다.

예:

```bash
cd ~/gts_udp_server
cp udp_server.py udp_server.py.bak
```

날짜를 포함하여 백업:

```bash
cp udp_server.py udp_server_20260921.py
```

백업 파일 확인:

```bash
ls -lh
```

기존 버전으로 복구:

```bash
cp udp_server.py.bak udp_server.py
sudo systemctl restart gts-udp
```

---

# 19. 서버 재부팅 후 확인

서버 재부팅:

```bash
sudo reboot
```

잠시 후 Mac에서 다시 접속:

```bash
ssh gts@192.168.0.6
```

서비스 확인:

```bash
systemctl status gts-udp
```

실시간 로그:

```bash
journalctl -u gts-udp -f
```

UDP port 확인:

```bash
ss -lunp | grep -E '5500|5501|5502'
```

`systemd` 서비스가 enable 되어 있으므로 서버가 재부팅되어도 자동으로 UDP Server가 실행됩니다.

---

# 20. 현재 서버 로그 예

AOS:

```text
[2026-09-17 17:45:21] [AOS] NEW DEVICE 192.168.0.101:54321
[2026-09-17 17:45:21] [AOS] 192.168.0.101:54321 -> CURRENT,123.45
```

GFC:

```text
[2026-09-17 17:45:25] [GFC] NEW DEVICE 192.168.0.102:50122
[2026-09-17 17:45:25] [GFC] 192.168.0.102:50122 -> TVOC,48.2
```

Console:

```text
[2026-09-17 17:45:28] [CONSOLE] NEW DEVICE 192.168.0.110:62310
[2026-09-17 17:45:28] [CONSOLE] 192.168.0.110:62310 -> SET,VOLTAGE,300
```

---

# 21. 현재 구조에서 주의할 점

현재 UDP Server는 초기 테스트용 기본 구조입니다.

현재 서버가 식별하는 정보는:

```text
Device Type
Source IP
Source UDP Port
```

입니다.

그러나 UDP Source Port는 장치 재부팅이나 socket 재생성 시 변경될 수 있습니다.

따라서 최종 GTS에서는 UDP payload 내부에 고유 Device ID를 포함하는 것을 권장합니다.

예:

```text
AOS001,CURRENT,123.45
```

```text
GFC001,TVOC,48.2
```

```text
CONSOLE01,SET,VOLTAGE,300
```

또는 JSON을 사용할 수도 있습니다.

예:

```json
{
  "device_id": "AOS001",
  "cmd": "CURRENT",
  "value": 123.45
}
```

다만 ESP32 / MCU 구현이 단순해야 한다면 초기에는 CSV 형식이 더 편리할 수 있습니다.

---

# 22. 향후 개발 예정 구조

현재 단계:

```text
AOS Sensor ---- UDP 5500 ----\
                              \
GFC ---------- UDP 5501 ------> Python asyncio UDP Server
                              /
Console ------ UDP 5502 -----/
```

향후에는 다음 기능을 추가할 예정입니다.

```text
UDP Server
   |
   +-- Device ID 관리
   |
   +-- Last Seen 관리
   |
   +-- AOS Packet Parser
   |
   +-- GFC Packet Parser
   |
   +-- Console Command Parser
   |
   +-- Log File
   |
   +-- PostgreSQL
   |
   +-- FastAPI
   |
   +-- Web Dashboard
```

특히 다음 단계에서는 아래 항목을 먼저 정의하는 것이 좋습니다.

1. Device ID 규칙
2. UDP Message Format
3. Command / Response 규칙
4. Packet Sequence Number
5. Timestamp 처리
6. ACK 필요 여부
7. Device 상태 / Last Seen
8. Database 저장 포맷

---

# 23. 자주 사용하는 명령 요약

서버 접속:

```bash
ssh gts@192.168.0.6
```

UDP 서버 상태:

```bash
systemctl status gts-udp
```

UDP 서버 재시작:

```bash
sudo systemctl restart gts-udp
```

실시간 로그:

```bash
journalctl -u gts-udp -f
```

Source 수정:

```bash
nano ~/gts_udp_server/udp_server.py
```

Service 수정:

```bash
sudo nano /etc/systemd/system/gts-udp.service
```

Service 설정 반영:

```bash
sudo systemctl daemon-reload
sudo systemctl restart gts-udp
```

Port 확인:

```bash
ss -lunp | grep -E '5500|5501|5502'
```

IP 확인:

```bash
ip -br addr
```

Routing 확인:

```bash
ip route
```

디스크 확인:

```bash
df -h
```

시간 확인:

```bash
timedatectl
```

서버 재부팅:

```bash
sudo reboot
```

---

# 24. 현재 최종 상태

현재 GTS UDP Server는 다음 상태로 정상 동작하고 있습니다.

```text
Server       : BOSGAME Mini PC
OS           : Ubuntu Server 24.04 LTS
Hostname     : gts-server
IP           : 192.168.0.6
Interface    : enp4s0
Disk         : 약 466 GB root filesystem
SSH          : 정상
NTP          : 정상
Time Zone    : Asia/Seoul

UDP 5500     : AOS Sensor
UDP 5501     : GFC
UDP 5502     : Console

Python       : asyncio UDP server
Auto Start   : systemd
Service Name : gts-udp
Live Log     : journalctl -u gts-udp -f
```

현재 3개의 UDP 포트 모두 Mac에서 송신 테스트를 완료했으며 정상적으로 수신되고 있습니다.
