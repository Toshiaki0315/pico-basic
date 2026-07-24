# サンプルプログラム

pico-basic の機能を一通り使ったサンプル集です。実機（Waveshare RP2350-Touch-LCD-2.8）で動かすことを想定しています。

## 動かし方

SD カードにコピーして `LOAD` するか、シリアル端末に直接貼り付けて `RUN` します。

```basic
LOAD "GUESS.BAS"
RUN
```

> ファイル名は 8.3 形式（大文字）で保存してください（例: `GUESS.BAS`）。
> `AUTO` を使うと行番号を自分で打たずに入力できます。

## 一覧

| ファイル | 内容 | 使っている機能 |
| :--- | :--- | :--- |
| [guess.bas](guess.bas) | 数当てゲーム（1〜100） | `INPUT` / `IF`〜`ELSE` / 行ラベル / `RND` / `GOTO` |
| [art.bas](art.bas) | 幾何アート（多角形・円・塗り） | `POLY` / `CIRCLE` / `PAINT` / `FOR`〜`NEXT` |
| [ball.bas](ball.bas) | 跳ねるボール（スプライト） | `GET@` / `PUT@`（XOR） / `DIM` / 単一行の複数文 |
| [draw.bas](draw.bas) | タッチでお絵かき | `TOUCH()` / `PSET` / 行ラベル / `IF`〜`GOTO` |
| [song.bas](song.bas) | 演奏＋ビジュアライザ | `PLAY`（非同期・3声） / `LINE ... BF` / `RND` |
| [sfx.bas](sfx.bas) | PSG 効果音（レーザー・爆発） | `SOUND`（PSG レジスタ・ノイズ・エンベロープ） |
| [qix.bas](qix.bas) | 陣取りゲーム（クイックス風） | 2次元配列 / `REPEAT`〜`UNTIL`（フラッドフィル） / `AND`/`OR` / 行ラベル / `GET`・`TOUCH()` / `PLAY` |

## メモ

- 画面は 320×240、色番号は 0〜15 です。フォントは 8×8 の ASCII なので、`PRINT` の文字列は英数字を使っています。
- `guess.bas` と `draw.bas` は入力待ち／無限ループです。`draw.bas` は **Ctrl-C** で止めます。
- `song.bas` は `PLAY` が非同期なので、演奏しながらバーが描かれます。
- `qix.bas` は W/A/S/D キー（シリアル端末）またはタッチで操作します。線を引いて戻ると、敵がいない側が自陣になります。75% で勝ちです。
- `sfx.bas` の `SOUND` はレジスタ直接指定です。周期・ミキサー・エンベロープの意味は [MANUAL.md](../MANUAL.md) の §5 を参照してください。
