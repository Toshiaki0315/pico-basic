#pragma once
#include "lexer.h"

/// @file parser.h
/// インタプリタの公開インタフェース。REPL と main から使う。

/**
 * @brief 1 行ぶんのトークン列を解釈して実行する。
 * @param tokens 字句解析済みのトークン列。先頭が数値なら行番号つきとみなす
 * @return 行番号つきでプログラムに格納しただけなら true（実行はしていない）。
 *         ダイレクトモードで実行したなら false。
 *         REPL はこれを見て Ready の表示を抑える
 */
bool parse_and_execute(const TokenList& tokens);

/**
 * @brief BASIC の出力を LCD とシリアル端末の両方へ出す。
 * @param s 出力する文字列。半角カタカナはシリアルへ出す際に UTF-8 へ直す
 */
void basic_print(const char* s);

/**
 * @brief Ctrl-C で実行中のプログラムを止める（CONT で再開できる状態にする）。
 *
 * 実行ループの定期チェックと、入力待ちの最中に読んだ Ctrl-C の両方から呼ぶ。
 * ダイレクトモードでは止めるプログラムが無いので "Break" とだけ出す。
 */
void basic_break_program();

/**
 * @brief 電源を切る（POWEROFF 文と電源ボタンの長押しの共通処理）。
 *
 * 電源が落ちれば戻らない。戻ったときは切れなかったということで（USB 給電中か
 * ボタンを押したまま）、開いていたファイルは閉じたうえで実行中のプログラムを
 * 停止させてある。
 */
void basic_power_off();

/**
 * @brief 乱数の種を時刻から作る（RUN のたびに撒き直すのに使う）。
 * @return 種にする 32bit 値。RTC があればその値、無ければ起動からの経過時間
 */
unsigned int basic_random_seed_source();

/**
 * @brief プログラムの 1 行を格納する（同じ行番号があれば置き換える）。
 * @param line_number 行番号（1-65535）
 * @param tokens 行番号を除いたトークン列。空なら既存行の削除になる
 * @throws std::runtime_error 中間コードが 1 行の上限を超える場合（Line too long）
 */
void store_line(int line_number, const TokenList& tokens);

/**
 * @brief プログラムを LIST する（範囲を絞れる）。
 * @param from_line 表示を始める行番号
 * @param to_line 表示を終える行番号（この行を含む）
 */
void list_program(int from_line = 0, int to_line = 65535);

/// @brief プログラム・変数・配列・ファイルをすべて消す（NEW 相当）。
void clear_program();

/**
 * @brief プログラムを先頭から実行する。
 * @param max_steps 実行する行数の上限。-1 なら制限なし。
 *                  無限ループを含むプログラムをテストから走らせるために使う
 */
void run_program(int max_steps = -1);

/**
 * @brief 直前の parse_and_execute が AUTO を実行したかを調べる。
 * @param[out] start 自動採番の開始行番号
 * @param[out] step  行番号の刻み
 * @return AUTO が実行されていれば true（保留フラグはここでクリアされる）。
 *         対話入力を持つ repl_start がこれを見て自動行番号モードに入る
 */
bool auto_mode_requested(int* start, int* step);
