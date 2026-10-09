**English** · [简体中文](codex-passport-validation.zh_CN.md)

# Validation and compatibility

Validation date: 2026-10-09. Firmware: 1.0.0. Toolchain: ESP-IDF 5.5.3.

- Build: PASS
- Host tests: PASS
- Device tests: PASS, confirmed by the device owner.
- Hardware acceptance: the owner reports that all functions have been tested and are working. This records owner-performed acceptance separately from the automated checks below.

## Automated checks

| Check | Result |
| --- | --- |
| `idf.py build` | PASS, ESP32-C3 / 8 MB |
| `./tools/validate.sh` | PASS, repository checks, host tests, isolated build, rendering, merged image and archive verification |
| `CODEX_SANITIZE=1 ./tools/test_codex.sh build/codex-sanitizers` | PASS, AddressSanitizer and UndefinedBehaviorSanitizer |
| Input, mapping, link and ADC filtering | 55 scenarios passed |
| Local settings, brightness, screen off/wake | 14 scenarios passed |
| Framing, RPC and state | 45 scenarios passed, including 20,000 malformed reports |
| Production BLE callbacks with stub radio/store | 35 scenarios passed |
| Actual SDK ATT/GATT permission functions | 4 additional security scenarios passed |
| Actual LVGL host rendering | 10 frames, 500 page/focus/sidebar changes passed |

The 153 distinct scenarios are in addition to the upstream repository tests.
SDK-backed BLE execution repeats the 35 callback cases; those repeats are not counted twice. Radio/store stubs do not prove real SMP, persistent bonds or desktop recognition. Host rendering is not a photograph of the device.

The test-only cJSON 1.7.19 source has a narrow deprecated-sprintf warning exception on the current macOS SDK; project code retains strict warnings. See its [source and license record](../tests/vendor/cjson/README.md).

## Authentication regression

Read-only inspection of the earlier firmware found correct macOS enumeration: VID 0x303A, PID 0x8360, usage FF00/01 and report ID 6. System logs reported `Authentication is insufficient` (ATT error 5) for both input notification subscription and output report access. This matched encrypted connectivity with no notifications and no received RPCs; enumeration alone did not establish working control.

In the pinned NimBLE implementation, `sm_sc_only=1` imposes authenticated level-4 ATT access. Just Works pairing is encrypted but not authenticated in that sense. Version 0.1.1 uses `sm_sc=1`, `sm_sc_only=0` and security level 2. Legacy pairing stays disabled in the build; encrypted reports/CCCDs and 16-byte keys remain required.

The regression extracts three unmodified permission functions from the activated SDK, retaining their license notice. It reproduces the original failure and checks encrypted Just Works access, unencrypted rejection and short-key rejection. Earlier SDK-free mocks did not catch the problem. This test does not replace over-the-air validation.

Input readiness now requires a usable encrypted, subscribed connection with sufficient MTU, not an earlier host RPC. Host Active still requires valid RPC. A closing flag prevents a resume request from reopening a terminating link.

## Reproducing and retaining artifacts

Run the commands above from the repository root with the pinned framework activated. The complete gate produces `build/FoloToy-AI-Passport-full.bin` and its matching archive under `build/firmware/<full-image-sha256>/`.

```bash
python3 tools/archive_firmware.py verify build/firmware/<full-image-sha256>
```

Keep the matching ELF, map and manifest alongside release work; an ELF from a different build timestamp is not interchangeable. Build history, local logs and dependency caches are not committed. The merged image is flashed at offset `0x0`; it can reset NVS settings and bonds. No full-chip erase is needed.

## Resource checks and hardware acceptance

The host UI test uses a 32 KiB LVGL pool and reported 5,232 bytes free after the exercise, with a peak use of 23,568 bytes. These are host allocator values, not board heap measurements. The linked firmware reported 154,058 bytes of DRAM used and 167,238 remaining before runtime allocations. The target uses the existing 19.2 KB partial LCD buffer, not a full-screen framebuffer.

The older firmware's enumeration and authentication failure were observed read-only. The device owner has confirmed full functional acceptance of version 1.0.0. Use the [complete manual checklist and troubleshooting table](codex-passport-guide.md): boot, display, all keys, notification and RPC exchange, computer microphone behavior, reconnect, bond reuse, actual battery, unknown charging state, brightness and screen wake. Flashing requires explicit device-owner authorization.
