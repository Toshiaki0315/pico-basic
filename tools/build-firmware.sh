#!/usr/bin/env bash
#
# 実機ファームウェア（.uf2）のビルド。
#
#   tools/build-firmware.sh          変更ぶんだけビルド
#   tools/build-firmware.sh --clean  ビルドディレクトリを作り直してから
#                                    （SDK からやり直すので数分かかる）
#
# 初回の構成では FatFS ドライバの取得にネットワークが要る。
# 書き込み手順は SETUP.md のセクション 5 を参照。

source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"

CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --clean) CLEAN=1 ;;
        -h|--help) sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) die "不明な引数: $arg" ;;
    esac
done

require_pico_toolchain

if [ "$CLEAN" = 1 ]; then
    say "ビルドディレクトリを消す: $PICO_BUILD_DIR"
    rm -rf "$PICO_BUILD_DIR"
fi

say "構成 (Pico RP2350)"
cmake -S "$REPO_ROOT" -B "$PICO_BUILD_DIR"

say "ビルド (-j$JOBS)"
LOG="$PICO_BUILD_DIR/build.log"
cmake --build "$PICO_BUILD_DIR" -j"$JOBS" 2>&1 | tee "$LOG"

FOUND="$(project_warnings "$LOG")"
if [ -n "$FOUND" ]; then
    warn ""
    warn "自分たちのコードの警告:"
    printf '%s\n' "$FOUND" >&2
fi

UF2="$PICO_BUILD_DIR/pico_basic.uf2"
[ -f "$UF2" ] || die "$UF2 が生成されませんでした"

say "完成: $UF2"
ls -lh "$UF2"
echo
echo "書き込み: BOOTSEL を押しながら USB 接続し、RP2350 ドライブへこのファイルをコピー"
