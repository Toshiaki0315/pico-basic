10 REM ============================
20 REM  TOUCH PAINT
30 REM  draw on screen with a finger
40 REM  color changes as you drag
50 REM  press Ctrl-C to stop
60 REM ============================
70 CLS
80 C = 15
90 *LOOP
100 IF TOUCH(2) = 0 THEN GOTO *LOOP
110 X = TOUCH(0) : Y = TOUCH(1)
120 PSET (X, Y), C
130 REM cycle color 8..15 as you draw
140 C = C + 1 : IF C > 15 THEN C = 8
150 GOTO *LOOP
