10 REM ============================
20 REM  HALFWIDTH KATAKANA CHART
30 REM  JIS X 0201 codes A1-DF
40 REM  shown on the LCD in kana
50 REM ============================
60 CLS
70 PRINT "KATAKANA (CHR$ A1-DF):"
80 PRINT
90 FOR C = &HA1 TO &HDF
100 PRINT CHR$(C);
110 IF (C - &HA0) MOD 16 = 0 THEN PRINT
120 NEXT C
130 PRINT
140 PRINT
150 REM --- build words from codes ---
160 REM  ス=BD KA=BA ... spell "SUKO-A" (score) etc.
170 G$ = CHR$(&HBD) + CHR$(&HBA) + CHR$(&HB1)
180 PRINT "WORD: "; G$
190 END
