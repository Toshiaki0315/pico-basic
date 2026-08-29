#!/usr/bin/env bash
# tools/*.sh が共有する部品。単体では実行しない。
#
# macOS 標準の bash は 3.2 なので、連想配列や mapfile などの bash 4 以降の
# 機能は使わない。

set -euo pipefail

# スクリプトの位置からリポジトリルートを求める。どこから呼んでも動くように
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# ビルドディレクトリ。既存の構成に合わせた既定値で、環境変数で上書きできる
HOST_BUILD_DIR="${HOST_BUILD_DIR:-$REPO_ROOT/build-test}"
PICO_BUILD_DIR="${PICO_BUILD_DIR:-$REPO_ROOT/build}"

if command -v nproc > /dev/null 2>&1; then
    JOBS="${JOBS:-$(nproc)}"
elif command -v sysctl > /dev/null 2>&1; then
    JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"
else
    JOBS="${JOBS:-4}"
fi

say() { printf '\n\033[1m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[33m%s\033[0m\n' "$*" >&2; }
die() { printf '\033[31merror: %s\033[0m\n' "$*" >&2; exit 1; }

# ビルド出力から「自分たちのコードの警告」だけを拾う。
#
# FetchContent で取ってくる googletest / FatFS ドライバも警告を出すが、それは
# 直す対象ではないうえ、混ざると自分の警告が埋もれる。パスで振り分ける。
project_warnings() {
    grep -E 'warning:|error:' "$1" 2> /dev/null | grep -vF '/_deps/' || true
}

# 実機ビルドに必要なものが揃っているか。無ければ理由を言って止める
require_pico_toolchain() {
    command -v arm-none-eabi-gcc > /dev/null 2>&1 \
        || die "arm-none-eabi-gcc が見つかりません。SETUP.md のセクション 2〜3 を参照してください"
    [ -f "$REPO_ROOT/pico_sdk_import.cmake" ] \
        || die "pico_sdk_import.cmake がありません。SETUP.md のセクション 3.3 を参照してください"
}

# 実機ビルドで自分たちのソースだけを確実に再コンパイルさせる。
#
# SDK と FatFS ドライバまで作り直すと数分かかるので、依存はキャッシュを使い、
# 自分のソースの mtime だけ進めて作り直させる。警告は再コンパイルしないと
# 出てこないため、警告を見たいときはこれを通す。
touch_project_sources() {
    touch "$REPO_ROOT"/src/*.cpp "$REPO_ROOT"/src/*.c "$REPO_ROOT"/src/*.h
}
