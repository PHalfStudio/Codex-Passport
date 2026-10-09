#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:-$(mktemp -d /tmp/codex-passport-tests.XXXXXX)}"
mkdir -p "$out"
cd "$root"
flags=(-std=c11 -Wall -Wextra -Werror -g -Imain -Icomponents/bsp/src -Itests/vendor/cjson)
if [[ "${CODEX_SANITIZE:-0}" == 1 ]]; then flags+=(-fsanitize=address,undefined -fno-omit-frame-pointer); fi
"${CC:-cc}" "${flags[@]}" tests/test_codex_input.c main/codex_input.c \
    main/codex_virtual_keys.c main/codex_ble_link.c components/bsp/src/bsp_button_ladder.c -o "$out/input"
"$out/input"
"${CC:-cc}" "${flags[@]}" tests/test_codex_local.c main/codex_local.c \
    main/codex_input.c main/codex_virtual_keys.c -o "$out/local"
"$out/local"
# Only the unmodified third-party source has a platform deprecation exception.
"${CC:-cc}" "${flags[@]}" -Wno-error=deprecated-declarations \
    -c tests/vendor/cjson/cJSON.c -o "$out/cjson.o"
"${CC:-cc}" "${flags[@]}" tests/test_codex_protocol.c main/codex_protocol.c \
    main/codex_state.c "$out/cjson.o" -lm -o "$out/protocol"
"$out/protocol"
"${CC:-cc}" "${flags[@]}" -Itests/codex_ble_stubs tests/test_codex_ble.c \
    main/codex_protocol.c main/codex_state.c main/codex_virtual_keys.c main/codex_ble_link.c \
    "$out/cjson.o" -lm -o "$out/ble"
"$out/ble"
if [[ -n "${IDF_PATH:-}" ]]; then
    python3 tools/extract_codex_nimble_test.py "$IDF_PATH" "$out/codex_nimble_permissions.inc"
    "${CC:-cc}" "${flags[@]}" -DCODEX_TEST_REAL_ATT -I"$out" -Itests/codex_ble_stubs \
        tests/test_codex_ble.c main/codex_protocol.c main/codex_state.c \
        main/codex_virtual_keys.c main/codex_ble_link.c "$out/cjson.o" -lm -o "$out/ble-sdk"
    "$out/ble-sdk"
else
    echo "Actual SDK ATT security regression: NOT RUN (activate ESP-IDF for complete gate)"
fi
echo "Evidence retained: $out"
