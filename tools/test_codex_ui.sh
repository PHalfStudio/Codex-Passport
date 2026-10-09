#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:-$root/build/codex-ui}"
mkdir -p "$out"
out="$(cd "$out" && pwd)"
lvgl="$root/managed_components/lvgl__lvgl"
test -f "$lvgl/lvgl.h" || { echo "Build firmware first to obtain pinned LVGL." >&2; exit 1; }
cmake -S "$lvgl" -B "$out/lvgl" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DLV_BUILD_CONF_PATH="$root/tests/codex_lv_conf.h" \
    -DCONFIG_LV_BUILD_EXAMPLES=OFF -DCONFIG_LV_BUILD_DEMOS=OFF -DCONFIG_LV_USE_THORVG_INTERNAL=OFF
cmake --build "$out/lvgl" --parallel "${BUILD_JOBS:-4}"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
    -DLV_KCONFIG_IGNORE -DLV_CONF_PATH="\"$root/tests/codex_lv_conf.h\"" \
    -I"$lvgl" -I"$root/components/bsp/src" -I"$root/main" \
    "$root/tests/test_codex_ui.c" "$root/main/codex_state.c" "$root/main/codex_input.c" \
    "$root/main/codex_virtual_keys.c" "$root/main/codex_local.c" "$root/components/bsp/src/bsp_display_rounding.c" \
    "$out/lvgl/lib/liblvgl.a" -lm -o "$out/ui-tests"
"$out/ui-tests" "$out"
