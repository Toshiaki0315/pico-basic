10 REM ============================
20 REM  SONG + VISUALIZER
30 REM  PLAY is asynchronous, so the
40 REM  bars are drawn while the tune
50 REM  is still playing
60 REM ============================
70 CLS
80 REM --- three-voice chord melody ---
90 PLAY "T120 O4 CEG CEG O5 C", "O4 R EGB EGB", "O3 CCGG CCGG"
100 REM --- draw bars while it plays ---
110 FOR I = 0 TO 39
120 H = INT(RND(180)) + 20
130 X = I * 8
140 C = (I AND 7) + 8 : REM cycle colors 8..15 with a bit mask
150 LINE (X, 239)-(X + 6, 239 - H), C, BF
160 WAIT 40
170 NEXT I
180 END
