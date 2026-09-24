
### 웹디장인 관련 주요 기능 정리

1. 주요 페이지 :  4페이지
    1. AOS 및 GFC 동작 상황표 
        - AOS(ID : 1~20), GFC(ID:1~20) - 두개의 장비가 한쌍임. 
        - 콘솔이 2~3개를 사용에정, 콘솔도 UDP server에 Data를 upload하고 있으면 보여야함.
    2. AOS/GFC 자세한 결과 보기 페이지(한개의 장비 AOS1개, GFC 1개 쌍만 표시함)
        - AOS가 자동측정 모드인지 Manual Mode인지에 따라 표시 형태가 조금 다름
        - GFC 는 가스 농도 및 제어상태를 표시항 (자동 또는 수동이 두가지 가 있음.) -> 가스농도는 작은 그래프로 표시
        - Manual Mode : AOS Current 그래프 보기(1초에 한번씩 올라오는 데이타를 화면에 표시함. y축)
            - HV, Frq, Duty, CV, LF-OnOff, LF_Frq, LF_Volt 6가지(+1) 를제어할 수 있어야함.초기 상태를 표시 할것.
        - 자동 측정모드 : 지속적으로 AOS 장비로 부터 heatmap data 가 올라옴. 이 데이타를 current 그래프로 순차적으로 보임. 이때 히트맵이 한화면에 3~4개 보이도록 함. y축시간 1분내외 예상됨.
            - 제어, 시작, 종료, Full, 1hour, Sample mode 3가지 종류가 있음.
    3. 가스농도 heatmap Viewer
        - Full, 1hour data는  16개의 heatmap표시
        - Sample mode 8개의 heatmap 표시
        - 가스종류,AOS ID, Air(Ref) Data등을 dB list 에서 선택하면 heatmap이 표시됨.
    4. dB 저장되어 있는 가스 heatmap Data  List 보기
        - air(ref) data List
        - Gas data list
2. 예제화일 Heatmap 16 Viewer.jpg 참조

---------------------
### 추가사항 및 변경사항
AOS Current 단위(pA) : V 로 변경할것.
가스농도 ppm -> V 로 변경
1. AOS/GFC 동작 상황표
    1. PAIR 01 -> AOS 01 변경(Pair 가 맞지만 당연히 AOS가 주가 되기 때문에)
    2. IDF 표시 필요없음.
    3. 수동모드 일때는 평균전류 표시(단위 V). 전류이지만 전압으로 표시함
2. AOS Current IDF 실시간 추이 -> AOS Current 로 표시
    1. 파지 버튼 -> 삭제(필요없음)
    2. 가스명 사용자가 변경가능하도록 할것.
3. 자동측정모드
    1. Current Graph Y : 전압, X축 : time(120sec)
    2. 가스명 변경가능 하도록 할것.
4. 가스농도  Heatmap Viewer : 수정없음
    


