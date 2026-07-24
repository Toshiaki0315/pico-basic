10 REM ============================
20 REM  BOUNCING BALL (SPRITE)
30 REM  GET@ captures, PUT@ with XOR
40 REM  moves it without erasing the
50 REM  background
60 REM ============================
70 CLS
80 DIM S(300)
90 REM --- draw a ball and capture it ---
100 CIRCLE (8, 8), 6, 14
110 PAINT (8, 8), 14, 14
120 GET@ (0, 0)-(16, 16), S
130 REM --- start position and velocity ---
140 X = 40 : Y = 40 : DX = 4 : DY = 3
150 REM --- animate for a while ---
160 FOR T = 1 TO 400
170 PUT@ (X, Y), S, 3 : REM XOR draw (show)
180 WAIT 15
190 PUT@ (X, Y), S, 3 : REM XOR draw (erase)
200 X = X + DX : Y = Y + DY
210 IF X <= 0 THEN DX = -DX
220 IF X >= 300 THEN DX = -DX
230 IF Y <= 0 THEN DY = -DY
240 IF Y >= 220 THEN DY = -DY
250 NEXT T
260 END
