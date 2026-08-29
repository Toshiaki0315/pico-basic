#pragma once

/// @file repl.h
/// 対話ループ（Read-Eval-Print Loop）と行エディタ。

/**
 * @brief BASIC の対話ループを始める。戻らない。
 *
 * 端末から 1 行読んでは parse_and_execute に渡す、を繰り返す。
 * AUTO（行番号自動生成）と Ctrl-P（画面の BMP 保存）もここで扱う。
 */
void repl_start();
