#pragma once
#include <stddef.h>
#include <stdbool.h>

/// @file hal_sdcard.h
/// MicroSD 上のファイル操作。戻り値の形は C の stdio に寄せてある
/// （void* は FatFS の FIL / DIR への不透明ハンドル。静的プールから貸し出す）。

#ifdef __cplusplus
extern "C" {
#endif

/// @brief SD カードとファイルシステム（FatFS）を初期化する。
/// カードが無くても失敗しない。以降の hal_file_open が null を返すだけ
void hal_sdcard_init();

/**
 * @brief ファイルを開く。
 * @param path パス。"0:" や "CAS:" の接頭辞は取り除かれる
 * @param mode "r" / "w" / "a"
 * @return ハンドル。開けなければ nullptr（カード無し・空きハンドル無しを含む）
 */
void* hal_file_open(const char* path, const char* mode);

/// @brief ファイルを閉じてハンドルを返却する。nullptr を渡してもよい
/**
 * @brief ファイルを閉じてハンドルを返却する。
 * @param file hal_file_open が返したハンドル。nullptr を渡してもよい
 */
void hal_file_close(void* file);

/**
 * @brief ファイルから読む。
 * @return 読めた要素数（count 未満なら終端かエラー）
 */
/**
 * @brief ファイルから読む。
 * @param[out] buffer 読み込み先
 * @param size 1 要素の大きさ
 * @param count 読む要素数
 * @param file ハンドル
 * @return 読めた要素数（count 未満なら終端かエラー）
 */
size_t hal_file_read(void* buffer, size_t size, size_t count, void* file);

/**
 * @brief ファイルへ書く。
 * @return 書けた要素数
 */
/**
 * @brief ファイルへ書く。
 * @param buffer 書く内容
 * @param size 1 要素の大きさ
 * @param count 書く要素数
 * @param file ハンドル
 * @return 書けた要素数
 */
size_t hal_file_write(const void* buffer, size_t size, size_t count, void* file);

/**
 * @brief 1 行読む。改行は取り除かれずに残る。
 * @param str 読み込み先
 * @param n str の大きさ（終端を含む）
 * @param file ハンドル
 * @return str。終端に達していたら nullptr
 */
char* hal_file_gets(char* str, int n, void* file);

/**
 * @brief 書式つきでファイルへ書く。
 * @return 書いたバイト数
 */
/**
 * @brief 書式つきでファイルへ書く。
 * @param file ハンドル
 * @param format printf 互換の書式
 * @return 書いたバイト数
 */
int hal_file_printf(void* file, const char* format, ...);

/**
 * @brief ディレクトリを開く。
 * @return ハンドル。開けなければ nullptr
 */
/**
 * @brief ディレクトリを開く。
 * @param path ディレクトリのパス
 * @return ハンドル。開けなければ nullptr
 */
void* hal_dir_open(const char* path);

/// @brief ディレクトリを閉じてハンドルを返却する
/**
 * @brief ディレクトリを閉じてハンドルを返却する。
 * @param dir hal_dir_open が返したハンドル
 */
void hal_dir_close(void* dir);

/**
 * @brief 次のエントリ名を返す。
 * @return ファイル名。終端まで読んだら nullptr。
 *         返る文字列は次の呼び出しまでしか有効でない
 */
/**
 * @brief 次のエントリ名を返す。
 * @param dir ハンドル
 * @return ファイル名。終端まで読んだら nullptr。返る文字列は次の呼び出しまでしか有効でない
 */
const char* hal_dir_read(void* dir);

/**
 * @brief ファイルを消す。
 * @return 成功なら 0
 */
/**
 * @brief ファイルを消す。
 * @param path 消すファイル
 * @return 成功なら 0
 */
int hal_file_remove(const char* path);

/**
 * @brief ファイル名を変える。
 * @return 成功なら 0
 */
/**
 * @brief ファイル名を変える。
 * @param old_path 元の名前
 * @param new_path 新しい名前
 * @return 成功なら 0
 */
int hal_file_rename(const char* old_path, const char* new_path);

/**
 * @brief "0:TEST.BAS" や "CAS:TEST.BAS" の接頭辞を落とす。
 * @param input_path 元のパス
 * @param[out] resolved_path 接頭辞を落としたパス。必ず終端される
 * @param max_len resolved_path の大きさ
 */
void hal_sdcard_resolve_path(const char* input_path, char* resolved_path, size_t max_len);

#ifdef __cplusplus
}
#endif
