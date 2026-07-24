10 REM ============================
20 REM  PSG SOUND EFFECTS
30 REM  direct AY-3-8910 registers
40 REM  via SOUND reg, value
50 REM ============================
60 CLS
70 PRINT "1: LASER  2: EXPLOSION"
80 REM ---- LASER: falling tone ----
90 PRINT "LASER..."
100 SOUND 8, 15 : REM channel A full volume
110 SOUND 7, 62 : REM mixer: tone A only
120 FOR P = 40 TO 400 STEP 8
130 SOUND 0, P : SOUND 1, P / 256
140 WAIT 6
150 NEXT P
160 SOUND 8, 0 : REM silence
170 WAIT 300
180 REM ---- EXPLOSION: noise + envelope ----
190 PRINT "EXPLOSION..."
200 SOUND 6, 20 : REM noise period
210 SOUND 7, 55 : REM mixer: noise on ch A
220 SOUND 11, 0 : SOUND 12, 60 : REM envelope period
230 SOUND 13, 9 : REM shape 9: decay to zero
240 SOUND 8, 16 : REM ch A uses envelope (bit4)
250 WAIT 800
260 SOUND 8, 0 : REM silence
270 END
