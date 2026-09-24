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
        제어종류가 6가지 이고 각각마다 step 이 있음. 스텝이 보여야됨.
        6종류의 현재값이 표시됨. jog 변경시 선택된 제어 종류가 step에 따라서 증가 또는 감소함.


### UDP Protocol
