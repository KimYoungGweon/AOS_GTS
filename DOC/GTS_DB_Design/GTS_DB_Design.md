
### PCSW Data 정보 주요 작업항목 정리
1. PCSW Data 분석 : /DOC/GTS_DB_DESIGN/AOS_TWIN_File_Viewer
2. Sample Data : /DOC/GTS_DB_DESIGN/air_lavender_4x4_20260726_112700.dat
3. FastMode Sample Data : /DOC/GTS_DB_DESIGN/Alcohol_air2_Sample_20260910_112026.dat
4. GTS Data dB 정리 : fast mode (약 2:30sec), 1hours,  8hours(3종) 
    -> 현재는 fast, Full data(8hour) 만 있음
5. Original Data Type 수정 (Heat)
    1. Air(ref.) 와 Target Gas Data 2종중 Target Gas Data Positive 만 저장
    2. Trace Data 삭제
6. Full Data (8hours 10x10 = 100) - 10x10*4*4  =  1600ea heatmaps
    === Grid Definition ===
    SectionXNo	10 : HV
    SectionYNo	10 : Frq

    HeatmapXNo	4 : Duty
    HeatmapYNo	4 : LF Frq

    LFV_No	16 : LF Volt
    CV_No	11 : CV

    Frq_List	10 sections : 200	266.7	333.3	400	466.7	533.3	600	666.7	733.3	800
    HV_List     10 sections	: 45, 60, 75, 90, 105, 120, 135, 150, 165, 180
    Duty_List   4 : 	50	55	60	65
    LFF_List	4 : 50	100	150	200
    LFV_List	16 : 0	0.2	0.4	0.6	0.8	1	1.2	1.4	1.6	1.8	2	2.2	2.4	2.6	2.8	3
    CV_List	    11 : -1	-0.8	-0.6	-0.4	-0.2	1.490116E-08	0.2	0.4	0.6	0.8	1

    Total Data 갯수 10x10x4x4x16x11  = 281,600 ea (5types* 4byte(실수))
    Air_P, Air_N, Target_P, Target_N, IDF(Target_P - Air_P)

7. about 1Hours (4x4 = 16)
    SectionXNo	4 : HV  (45, 90, 135, 180) 
    SectionYNo	4 : Frq (200, 400, 600, 800)

8. Heatmap 1 image : 16x11 -> 176*4 (byte) : float type 
9. 174*0.1sec = 17.4 sec -> 1600*17.4 = 27840 sec ~ 7.7 hours : 통신대기 및 기타 시간을 포함해서 약 8hour 정도 필요함.
10. Fast Mode : 8ea heatmaps
    - HV, Frq, duty, LF_Frq 4가지를 선택적으로 8개만 따로 정의함.



