10 REM ============================
20 REM  GEOMETRIC ART
30 REM  polygons, circles and paint
40 REM ============================
50 CLS
60 REM --- concentric polygons ---
70 FOR N = 3 TO 8
80 R = N * 14
90 C = N
100 POLY (160, 120), R, C, N
110 NEXT N
120 REM --- row of filled circles ---
130 FOR I = 0 TO 7
140 X = 20 + I * 40
150 CIRCLE (X, 220), 14, I + 8
160 PAINT (X, 220), I + 8, I + 8
170 NEXT I
180 REM --- a star on top ---
190 POLY (160, 120), 30, 15, 5, 2
200 END
