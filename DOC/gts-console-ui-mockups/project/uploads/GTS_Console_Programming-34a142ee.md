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
            - Duty: 10~50%, jog step : 0.01%, 0.1%
            - CV : -5 ~ 5V,  step: 0.001V, 0.01V, 0.1V
            - LF_Mode : On/Off
                - LF_Frq : 50 ~ 200Hz, step : 1Hz
                - LF_Volt : 0~5V, step : 0.01V, 0.1V
                - LF_Shape : Square, Sine, Triangle, Tropizodal
            - step 변경은 Jog S/W를 사용한다.
            - 값의 변경은 Jog를 변경함 따라 변동되면, 값변경시 udp로 서버에 전달한다.


### 화면 구성
1. LCD 화면 : 800x480(H mode)
2. 화면구성 : 4화면(장치 선택에 따라서 어떤 화면을 사용할지 결정됨)
    1. Device 선택 : Device Type Device ID선택 : 선택을 하면 장비와 테스트 컨넥션을 하여 장비상태를 읽어옴.
    2. GFC Mode
    3. AOS Device 가스 Data 측정 Mode
    4. AOS Device Manual Mode
        - 제어종류가 6가지 이고 각각마다 step 이 있음. 스텝이 보여야됨.
        - 6종류의 현재값이 표시됨. jog 변경시 선택된 제어 종류가 step에 따라서 증가 또는 감소함.
    5. (추가사항) Wifi  접속 List
        - 접속할 장소가 2~3곳 이기에 Wifi 접속되는 지점의 List를 보여주며. 선택시에 Wifi 접속한다.
        - Wifi 연결이 않되면 1번화면에서 5번으로 자동 변경되면. 이화면 선택후 Wifi가 연결되면 1번 화면으로 전환한다.
3. 기타 수정사항
    - DEVICE change 버튼 조금 크게 20%정도
    - 위좌측 "GTS CONSOLE" Lavel을 크게할것. 글씨가 넘어감. 대략 30% 정도
        



### UDP Protocol
1. FW_RS232_통신규격.md 화일은 현재 AOS 장치와 UART로 연결된 ESP32 사이에 사용하게될 F/W 프로토콜이다. 이 프로토콜을 참고하여 콘솔에서 AOS 를 제어하기 위한 UDP 프롵콜을 설계할것.
2. 기본형식은 //0x02,device_type, device_id, cmd, size_L, size_H, data......, CRC16 으로 설계할것.


### 참고사항.
1. LCD Design은 다음을 사용할 것. : DOC/GTS_Consol UI mockups/ 
2. udp upload 기능은 디자이너에 있는 기능을 기본으로 하여 정리할것.
3. 프로젝트명은 GTS_CONSOLE_ESP32P4 로 만들것.
4. JC4880P443C_Demo/ 는 ESP32P4 데모코드로 F/W Source encoder(jog) 및 UDP server 동작등을 테스트 완료한 코드임(LCD화면제어, JOG, UDP Upload). 따라서 이 코드를 복사하여 작업을 할것. 
5. AOS 장치 선택시 기본화면은 Manual Mode로 한다.

