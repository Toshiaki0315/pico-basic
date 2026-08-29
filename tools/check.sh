#!/usr/bin/env bash
#
# コミット前のフル確認。ホストテストと実機ビルドの両方を通す。
#
#   tools/check.sh           自分のソースだけ作り直して両方を通す
#   tools/check.sh --clean   依存ごと作り直す（googletest と SDK。数分かかる）
#
# 両方を通す必要があるのは、警告の出方がコンパイラで違うため。ホストは
# clang、実機は arm-none-eabi-gcc で、gcc にしか無い警告（strncpy の切り詰め
# など）は実機ビルドまで通さないと見えない。
#
# 自分たちのコードに警告が 1 つでも出たら失敗にする。-Wall -Wextra を入れて
# ある以上、新しい警告は「いま入った問題」だから。
# FetchContent 由来（googletest / FatFS ドライバ）の警告は数えない。

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        -h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "不明な引数: $arg" ;;
    esac
done

require_pico_toolchain

FAILURES=""

# ビルドしてログを残し、要点だけ画面に出す。
# パイプの `|| true` でまとめて握り潰すとビルドの失敗まで消えて、古いバイナリの
# ままテストへ進んでしまうので、cmake 自身の終了コードを取り出して返す
build_into() {
    local build_dir="$1" log="$2" rc
    set +e
    cmake --build "$build_dir" -j"$JOBS" 2>&1 | tee "$log" | grep -E 'error:|Built target'
    rc=${PIPESTATUS[0]}
    set -e
    return "$rc"
}

# ---- 1. ホスト: 自分のソースを作り直してビルド、テスト ----
say "[1/2] ホストテスト"
if [ "$CLEAN" = 1 ]; then
    rm -rf "$HOST_BUILD_DIR"
else
    # ビルドディレクトリごと消すと _deps も消えて、実行のたびに googletest の
    # 取得とビルドが走る（ネットワークも要る）。依存はそのままに、自分の
    # ソースだけ作り直させる。警告は再コンパイルしないと出てこないため
    touch_project_sources
    touch "$REPO_ROOT"/tests/*.cpp "$REPO_ROOT"/tests/*.h 2> /dev/null || true
fi
cmake -S "$REPO_ROOT" -B "$HOST_BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON > /dev/null
HOST_LOG="$HOST_BUILD_DIR/build.log"
if build_into "$HOST_BUILD_DIR" "$HOST_LOG"; then
    "$HOST_BUILD_DIR/basic_tests" || FAILURES="$FAILURES host-tests"
else
    # 通っていないバイナリでテストしても意味が無いので実行しない
    FAILURES="$FAILURES host-build"
fi

HOST_WARN="$(project_warnings "$HOST_LOG")"
[ -z "$HOST_WARN" ] || FAILURES="$FAILURES host-warnings"

# ---- 2. 実機: 自分のソースを作り直してビルド ----
say "[2/2] 実機ファームウェア"
if [ "$CLEAN" = 1 ]; then
    rm -rf "$PICO_BUILD_DIR"
else
    # 依存はキャッシュのまま、自分のソースだけ作り直させる。
    # 再コンパイルしないと警告が出てこないため
    touch_project_sources
fi
cmake -S "$REPO_ROOT" -B "$PICO_BUILD_DIR" > /dev/null
PICO_LOG="$PICO_BUILD_DIR/build.log"
build_into "$PICO_BUILD_DIR" "$PICO_LOG" || FAILURES="$FAILURES pico-build"

PICO_WARN="$(project_warnings "$PICO_LOG")"
[ -z "$PICO_WARN" ] || FAILURES="$FAILURES pico-warnings"
[ -f "$PICO_BUILD_DIR/pico_basic.uf2" ] || FAILURES="$FAILURES pico-uf2"

# ---- まとめ ----
say "結果"
if [ -n "$HOST_WARN" ]; then
    warn "ホストビルドの警告:"
    printf '%s\n' "$HOST_WARN" >&2
fi
if [ -n "$PICO_WARN" ]; then
    warn "実機ビルドの警告:"
    printf '%s\n' "$PICO_WARN" >&2
fi

if [ -n "$FAILURES" ]; then
    die "失敗:$FAILURES"
fi

echo "ホストテスト: 通過"
echo "実機ビルド:   $PICO_BUILD_DIR/pico_basic.uf2"
echo "警告:         自分たちのコードには無し"
