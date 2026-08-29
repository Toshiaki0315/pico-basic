#include "hal_sdcard.h"
#include "strutil.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

void hal_sdcard_resolve_path(const char* input_path, char* resolved_path, size_t max_len) {
    const char* p = input_path;
    // Strip "0:" prefix
    if (strncmp(p, "0:", 2) == 0) {
        p += 2;
    }
    // Strip "CAS:" prefix
    else if (strncmp(p, "CAS:", 4) == 0) {
        p += 4;
    }
    
    copy_string(resolved_path, (size_t)max_len, p);
}

#if __has_include("pico/stdlib.h")
// ---------------------------------------------------------
// Pico Target Implementations (FatFS + SDIO 接続 MicroSD)
//
// ピン定義は src/hw_config.c、ドライバは
// no-OS-FatFS-SD-SDIO-SPI-RPi-Pico ライブラリ。
// ---------------------------------------------------------
#include "ff.h"
#include "f_util.h"   // FRESULT_str()
#include "hw_config.h" // sd_get_by_num()

// FIL / DIR は数百バイトあるため、動的確保せず固定数のプールから貸し出す
#define MAX_OPEN_FILES 4
#define MAX_OPEN_DIRS  2

static FATFS   fs;
static bool    fs_mounted = false;
static FRESULT last_mount_result = FR_OK;

static FIL  file_pool[MAX_OPEN_FILES];
static bool file_in_use[MAX_OPEN_FILES];

static DIR      dir_pool[MAX_OPEN_DIRS];
static bool     dir_in_use[MAX_OPEN_DIRS];
static FILINFO  dir_info[MAX_OPEN_DIRS];

// ---------------------------------------------------------
// SD ドライバのメッセージ出力
//
// ライブラリは USE_PRINTF 未定義時、これらの weak 関数にメッセージを渡す。
// カード抜き差しの検出では「応答が無いこと」を意図的に確かめるため、
// その間だけ出力を抑制する（利用者に無用なエラーを見せないため）。
// ---------------------------------------------------------
static bool sd_log_quiet = false;

extern "C" void put_out_error_message(const char* s) {
    if (sd_log_quiet) return;
    printf("[SD] %s", s); // メッセージ側に改行が含まれている
}
extern "C" void put_out_info_message(const char* s)  { (void)s; }
extern "C" void put_out_debug_message(const char* s) { (void)s; }

// カードと通信できているかを CMD13 で確認する。
// このボードにはカード検出用の GPIO が無いため、抜き差しはこれで判断するしかない。
// 通信できない場合、ドライバ側は次回アクセス時に再初期化される。
static bool card_responds() {
    sd_card_t* sd = sd_get_by_num(0);
    if (!sd || !sd->sd_test_com) return false;

    // 応答が無いのは「カードが抜かれた」という想定内の結果なので、
    // ドライバのタイムアウト出力は見せない
    sd_log_quiet = true;
    bool ok = sd->sd_test_com(sd);
    sd_log_quiet = false;
    return ok;
}

// マウントを試みる。起動後にカードを挿した場合も拾えるよう毎回呼ぶ
static bool ensure_mounted() {
    if (fs_mounted) {
        // 抜き差しされていないか毎回確かめる。
        // カードが入れ替わったのに FatFS が前のカードのキャッシュ（FAT・
        // フリークラスタ等）のまま書き込むと、新しいカードを壊しかねない。
        // また、未初期化のカードにいきなり書き込みコマンドを送ると
        // ドライバがタイムアウトを繰り返すため、先にここで作り直す。
        if (card_responds()) return true;
        f_unmount("");
        fs_mounted = false;
    }
    last_mount_result = f_mount(&fs, "", 1);
    fs_mounted = (last_mount_result == FR_OK);
    return fs_mounted;
}

// マウント済みフラグを落とす。カードを抜かれた後に古い状態で操作しないため
static void invalidate_mount() {
    fs_mounted = false;
}

// 読み書きの失敗がカード側の問題なら、次回操作でマウントし直す
static void invalidate_mount_on_disk_error(FRESULT fr) {
    if (fr == FR_DISK_ERR || fr == FR_NOT_READY || fr == FR_INVALID_OBJECT) {
        invalidate_mount();
    }
}

// "r" / "w" / "a"（+ "b" や "+" の組み合わせ）を FatFS のフラグに変換する
static BYTE to_fatfs_mode(const char* mode) {
    bool update = (strchr(mode, '+') != NULL);

    switch (mode[0]) {
        case 'w': return FA_CREATE_ALWAYS | FA_WRITE | (update ? FA_READ : 0);
        case 'a': return FA_OPEN_APPEND  | FA_WRITE | (update ? FA_READ : 0);
        case 'r':
        default:  return FA_READ | (update ? FA_WRITE : 0);
    }
}

void hal_sdcard_init() {
    if (!ensure_mounted()) {
        // 原因の切り分けに使えるよう FatFS の戻り値をそのまま出す。
        //   FR_NOT_READY   … カード未挿入 or SPI 配線・初期化の問題
        //   FR_NO_FILESYSTEM … 認識はできたがパーティション/フォーマットが読めない
        //   FR_DISK_ERR    … 通信は成立したが読み書きに失敗（速度・結線を疑う）
        printf("[SD] Card not mounted: %s (%d)\n",
               FRESULT_str(last_mount_result), last_mount_result);
    }
}

void* hal_file_open(const char* path, const char* mode) {
    if (!ensure_mounted()) return NULL;

    int slot = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!file_in_use[i]) { slot = i; break; }
    }
    if (slot < 0) return NULL; // 同時オープン数の上限

    char resolved[256];
    hal_sdcard_resolve_path(path, resolved, sizeof(resolved));

    FRESULT fr = f_open(&file_pool[slot], resolved, to_fatfs_mode(mode));
    if (fr != FR_OK) {
        invalidate_mount_on_disk_error(fr);
        return NULL;
    }

    file_in_use[slot] = true;
    return &file_pool[slot];
}

void hal_file_close(void* file) {
    if (!file) return;
    FIL* fp = (FIL*)file;
    // 書き込み内容は f_close で初めてカードに確定するため、ここの失敗は見逃せない
    invalidate_mount_on_disk_error(f_close(fp));

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (&file_pool[i] == fp) { file_in_use[i] = false; break; }
    }
}

size_t hal_file_read(void* buffer, size_t size, size_t count, void* file) {
    if (!file || size == 0) return 0;
    UINT read_bytes = 0;
    FRESULT fr = f_read((FIL*)file, buffer, (UINT)(size * count), &read_bytes);
    if (fr != FR_OK) {
        invalidate_mount_on_disk_error(fr);
        return 0;
    }
    return read_bytes / size; // fread と同じく「読めた要素数」を返す
}

size_t hal_file_write(const void* buffer, size_t size, size_t count, void* file) {
    if (!file || size == 0) return 0;
    UINT written = 0;
    FRESULT fr = f_write((FIL*)file, buffer, (UINT)(size * count), &written);
    if (fr != FR_OK) {
        invalidate_mount_on_disk_error(fr);
        return 0;
    }
    return written / size;
}

char* hal_file_gets(char* str, int n, void* file) {
    if (!file) return NULL;
    return f_gets(str, n, (FIL*)file);
}

int hal_file_printf(void* file, const char* format, ...) {
    if (!file) return -1;

    // FatFS には f_vprintf が無いため、一度整形してから f_puts で書き出す
    char buf[512];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    if (len < 0) return -1;
    return f_puts(buf, (FIL*)file);
}

void* hal_dir_open(const char* path) {
    if (!ensure_mounted()) return NULL;

    int slot = -1;
    for (int i = 0; i < MAX_OPEN_DIRS; i++) {
        if (!dir_in_use[i]) { slot = i; break; }
    }
    if (slot < 0) return NULL;

    char resolved[256];
    hal_sdcard_resolve_path(path, resolved, sizeof(resolved));

    FRESULT fr = f_opendir(&dir_pool[slot], resolved);
    if (fr != FR_OK) {
        invalidate_mount_on_disk_error(fr);
        return NULL;
    }

    dir_in_use[slot] = true;
    return &dir_pool[slot];
}

void hal_dir_close(void* dir) {
    if (!dir) return;
    DIR* dp = (DIR*)dir;
    f_closedir(dp);

    for (int i = 0; i < MAX_OPEN_DIRS; i++) {
        if (&dir_pool[i] == dp) { dir_in_use[i] = false; break; }
    }
}

const char* hal_dir_read(void* dir) {
    if (!dir) return NULL;
    DIR* dp = (DIR*)dir;

    // 返す名前は次回呼び出しまで有効（readdir と同じ約束）
    int slot = 0;
    for (int i = 0; i < MAX_OPEN_DIRS; i++) {
        if (&dir_pool[i] == dp) { slot = i; break; }
    }

    // FILES は「ファイル」だけを見せる。
    // Windows が作る "System Volume Information" のようなディレクトリや、
    // 隠し・システム属性のエントリは読み飛ばす
    while (true) {
        if (f_readdir(dp, &dir_info[slot]) != FR_OK) return NULL;
        if (dir_info[slot].fname[0] == '\0') return NULL; // 終端

        BYTE attr = dir_info[slot].fattrib;
        if (attr & (AM_DIR | AM_HID | AM_SYS)) continue;
        if (dir_info[slot].fname[0] == '.') continue; // ホスト側と揃えて隠しファイル扱い
        break;
    }

    return dir_info[slot].fname;
}

int hal_file_remove(const char* path) {
    if (!ensure_mounted()) return -1;

    char resolved[256];
    hal_sdcard_resolve_path(path, resolved, sizeof(resolved));
    return (f_unlink(resolved) == FR_OK) ? 0 : -1;
}

int hal_file_rename(const char* old_path, const char* new_path) {
    if (!ensure_mounted()) return -1;

    char resolved_old[256];
    char resolved_new[256];
    hal_sdcard_resolve_path(old_path, resolved_old, sizeof(resolved_old));
    hal_sdcard_resolve_path(new_path, resolved_new, sizeof(resolved_new));
    return (f_rename(resolved_old, resolved_new) == FR_OK) ? 0 : -1;
}

#else
// ---------------------------------------------------------
// Host Target Implementations (using standard C library)
// ---------------------------------------------------------
#include <dirent.h>
#include <sys/stat.h>

void hal_sdcard_init() {
    // No initialization needed for host
}

void* hal_file_open(const char* path, const char* mode) {
    char resolved[256];
    hal_sdcard_resolve_path(path, resolved, sizeof(resolved));
    return fopen(resolved, mode);
}

void hal_file_close(void* file) {
    if (file) fclose((FILE*)file);
}

size_t hal_file_read(void* buffer, size_t size, size_t count, void* file) {
    if (!file) return 0;
    return fread(buffer, size, count, (FILE*)file);
}

size_t hal_file_write(const void* buffer, size_t size, size_t count, void* file) {
    if (!file) return 0;
    return fwrite(buffer, size, count, (FILE*)file);
}

char* hal_file_gets(char* str, int n, void* file) {
    if (!file) return NULL;
    return fgets(str, n, (FILE*)file);
}

int hal_file_printf(void* file, const char* format, ...) {
    if (!file) return -1;
    va_list args;
    va_start(args, format);
    int ret = vfprintf((FILE*)file, format, args);
    va_end(args);
    return ret;
}

void* hal_dir_open(const char* path) {
    char resolved[256];
    hal_sdcard_resolve_path(path, resolved, sizeof(resolved));
    return opendir(resolved);
}

void hal_dir_close(void* dir) {
    if (dir) closedir((DIR*)dir);
}

const char* hal_dir_read(void* dir) {
    if (!dir) return NULL;

    // 実機（FatFS）側と規則を揃える: ディレクトリと隠しファイルは返さない
    while (true) {
        struct dirent* entry = readdir((DIR*)dir);
        if (!entry) return NULL;

        if (entry->d_name[0] == '.') continue; // "." ".." と隠しファイル
        if (entry->d_type == DT_DIR) continue;
        if (entry->d_type == DT_UNKNOWN) {
            // d_type が使えないファイルシステム向けの保険
            struct stat st;
            if (stat(entry->d_name, &st) == 0 && S_ISDIR(st.st_mode)) continue;
        }
        return entry->d_name;
    }
}

int hal_file_remove(const char* path) {
    char resolved[256];
    hal_sdcard_resolve_path(path, resolved, sizeof(resolved));
    return remove(resolved);
}

int hal_file_rename(const char* old_path, const char* new_path) {
    char resolved_old[256];
    char resolved_new[256];
    hal_sdcard_resolve_path(old_path, resolved_old, sizeof(resolved_old));
    hal_sdcard_resolve_path(new_path, resolved_new, sizeof(resolved_new));
    return rename(resolved_old, resolved_new);
}

#endif
