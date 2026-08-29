#!/usr/bin/env bash
#
# ホスト単体テスト（Pico 実機も SDK も不要）。
#
#   tools/test.sh                    ビルドして全テストを実行
#   tools/test.sh --clean            ビルドディレクトリを作り直してから
#   tools/test.sh PowerKeyTest.*     Google Test のフィルタを渡す
#
# 初回だけ Google Test の取得にネットワークが要る。

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

CLEAN=0
FILTER=""
for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        -h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) FILTER="$arg" ;;
    esac
done

if [ "$CLEAN" = 1 ]; then
    say "ビルドディレクトリを消す: $HOST_BUILD_DIR"
    rm -rf "$HOST_BUILD_DIR"
fi

say "構成 (BUILD_TESTS=ON)"
cmake -S "$REPO_ROOT" -B "$HOST_BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON

say "ビルド (-j$JOBS)"
LOG="$HOST_BUILD_DIR/build.log"
cmake --build "$HOST_BUILD_DIR" -j"$JOBS" 2>&1 | tee "$LOG"

FOUND="$(project_warnings "$LOG")"
if [ -n "$FOUND" ]; then
    warn ""
    warn "自分たちのコードの警告:"
    printf '%s\n' "$FOUND" >&2
fi

say "テスト実行"
if [ -n "$FILTER" ]; then
    "$HOST_BUILD_DIR/basic_tests" --gtest_filter="$FILTER"
else
    "$HOST_BUILD_DIR/basic_tests"
fi
