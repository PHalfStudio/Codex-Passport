**English** · [简体中文](codex-passport-guide.zh_CN.md)

# Codex Passport

An experimental Codex Micro-compatible BLE control panel for **FoloToy AI
Passport**, ESP32-C3, 8 MB Flash, no PSRAM. It boots into a new 240×320 portrait
interface, not the upstream hardware-test menu. Use only UP, DOWN and OK.
No touch, Wi-Fi, API credentials, cloud forwarding, Arduino or custom Mac bridge.

This is **not official Codex Micro hardware**. The name and compatibility IDs
are used for personal interoperability experiments. The wire protocol is
unpublished and desktop updates can break detection. Compiling and Bluetooth
pairing do not prove ChatGPT integration. This targets ChatGPT Desktop's local
feature, not native hardware pairing in Codex CLI.

## Controls

| Physical input | Normal pages |
| --- | --- |
| UP / DOWN short press | Previous / next focus, wrapping within the page |
| UP / DOWN hold ≥500 ms | Previous / next page, once per hold |
| OK press / release | Virtual press / release; same identity until release |
| OK held | Keep the key down; no automatic repeat or forced release |
| Two quick OK clicks | Two original press/release pairs; host interprets them |

Pages cycle **AGENTS → COMMANDS → NAV / DIAL → AGENTS**, retaining separate
focus indices. Green borders mean local focus, white borders mean held, and
amber means local confirmation. Host Agent colors occupy separate status marks;
without host data they are neutral gray. Commands are factory-default labels:
ChatGPT can remap the positions, and the firmware sends positions, not actions.

| Page / label | Wire ID / message | Press | Release |
| --- | --- | --- | --- |
| Agent 1 | AG00, ag=0 | act=1 | act=0 |
| Agent 2 | AG01, ag=1 | act=1 | act=0 |
| Agent 3 | AG02, ag=2 | act=1 | act=0 |
| Agent 4 | AG03, ag=3 | act=1 | act=0 |
| Agent 5 | AG04, ag=4 | act=1 | act=0 |
| Agent 6 | AG05, ag=5 | act=1 | act=0 |
| Fast | ACT06 | act=1 | act=0 |
| Approve | ACT07 | After local confirmation | act=0 |
| Decline | ACT08 | After local confirmation | act=0 |
| Fork | ACT09 | act=1 | act=0 |
| Mic / PTT | ACT10 only | act=1 | act=0 |
| Send | ACT12 | act=1 | act=0 |
| Stick Up | v.oai.rad, a=0.75 | d=1 | a=0,d=0 |
| Stick Right | v.oai.rad, a=0.00 | d=1 | a=0,d=0 |
| Stick Down | v.oai.rad, a=0.25 | d=1 | a=0,d=0 |
| Stick Left | v.oai.rad, a=0.50 | d=1 | a=0,d=0 |
| Dial CW / CCW | ENC_CW / ENC_CC | One act=2 detent | No report |
| Dial Click | ENC_CLK | act=1, including long holds | act=0 |
| Settings | Local only, last NAV cell | Opens sidebar after release | No HID |

Approve and Decline require two distinct OK presses within three seconds. The
first never reaches the host. Navigation, timeout, page change or disconnection
cancels confirmation. The optional `CONFIG_CODEX_PROTECT_FORK_SEND` also protects
Fork/Send. Mic controls the **computer's microphone**, not Passport audio. A
double click is not synthesized: keep both clicks within the host's roughly
350 ms window. The firmware does not claim to know whether recording is active.
Dial Click is never used for local navigation. `CONFIG_CODEX_DIAL_LEGACY_ALIAS`
selects `ENC` only when a real host test establishes that the alias is necessary.

## Local sidebar

On NAV / DIAL, focus **Settings** and click OK. It works without a BLE host.
UP/DOWN selects a row; OK opens it. Mic and dial long presses remain unchanged.

- **Screen Off:** blank the panel and backlight; keep BLE and button scanning
  running. The next press wakes the screen, consuming that whole gesture without
  sending HID or navigating. This is not computer locking or MCU deep sleep.
- **Brightness:** UP +5%, DOWN −5%, bounded to 10–100%; OK returns.
- **Re-pair:** press OK again within three seconds. Disconnect, stop advertising,
  unpair the one saved host, verify controller removal, clear standard peer/CCCD
  records, reload the store and inspect persistent entry names. Keep local IRK
  and application preferences. Resume advertising only on success; show failure
  instead of claiming that pairing was cleared. Also forget the device on macOS.
- **Diagnostics:** encryption, subscription, MTU, actual SOC/unknown charging,
  receive/error/drop/send counters and sanitized method name. OK returns.
- **Back:** close the sidebar; the closing release cannot activate a virtual key.

Page, focus and brightness are saved after ten seconds of input inactivity,
outside LVGL locks and while no key is held. Confirmation, press state and host
lighting are never persisted. NVS initialization errors do not trigger erasure.

## Architecture and HID

`main/codex_app.c` owns initialization, input queue, persistence and battery task.
`codex_input.c` implements the timestamped focus/confirmation/hold state machine;
`codex_local.c` handles the sidebar and wake-event consumption.
`codex_virtual_keys.c` is the stable identity-to-message mapping.
`codex_ble_hid.c` owns NimBLE HOGP and the serial worker; `codex_ble_link.c` checks
connection generations/readiness. `codex_protocol.c` implements framing and RPC.
`codex_state.c` owns snapshot types and light calculations; `codex_ui.c` creates
ordinary LVGL objects and updates changed properties under the BSP LVGL lock.

Button callbacks only enqueue edges. The 32-entry BLE queue serializes output
reports and notifications; GATT callbacks never parse JSON, read the battery or
render. Disconnect/reset/subscription loss invalidates generations and assembly
buffers. Old presses older than 250 ms are dropped, and disconnected actions are
not queued. Notification failure or loss of a release path ends the connection.
The shared BSP ADC now debounces one identity and rejects transfers to another
key until a stable release; no additional ADC unit or pin changes were made.
The existing BSP already registers RELEASE to BUTTON_PRESS_UP.

| HID identity | Value |
| --- | --- |
| Name / manufacturer | Codex Micro / Work Louder |
| PnP source, VID, PID, release | 02, 303A, 8360, 0101 |
| PnP value, little endian | `02 3A 30 60 83 01 01` |
| Usage page / application / report ID | FF00 / 01 / 06 |
| HID / Battery / Device Information | 1812 / 180F / 180A |
| Input / Output report references | `06 01` / `06 02` |
| Input / Output characteristic body | 63 bytes, excluding report ID |
| Security | LE Secure Connections, 16-byte encryption keys, bonding, one host |

Just Works has **no passkey/MITM verification**: first pair in a trusted
environment. Bonds are persisted by NimBLE. Report accesses and notification
subscription writes require encryption. Report mode is supported; boot keyboard
mode is not. There is no USB keyboard or ordinary keyboard report collection.
Version 0.1.1 fixes an authentication mismatch: NimBLE's `sm_sc_only` also
requires authenticated level-4 ATT access, which Just Works cannot provide.
It is now disabled; legacy pairing remains disabled in the build, Secure
Connections remains enabled, and reports/CCCDs still require encryption.
The HID information is `11 01 00 02`; the complete Report Map is:

```text
06 00 FF 09 01 A1 01 85 06 15 00 26 FF 00 75 08 95 3F
09 02 81 02 95 3F 09 03 91 02 C0
```

A report body is `[02][length 0..61][UTF-8 fragment][zero padding]`. TX appends
LF and sends ordered 61-byte fragments, four milliseconds apart in the worker.
RX accepts 63-byte bodies or 64-byte values prefixed with `06`, LF/CRLF or a
complete JSON object without a delimiter. Assembly is bounded to 4096 bytes,
16 nesting levels and 256 structural tokens, with a 2.5-second timeout, UTF-8
validation, parse-error reset and recognition of a new top-level RPC after loss.
Multiple adjacent messages are accepted. The protocol has no sequence number,
so undetectable fragment loss cannot be turned into guaranteed delivery.

| Host RPC | Behavior |
| --- | --- |
| sys.version | Actual firmware version |
| device.status | version, profile_index=0, layer_index=1, battery, is_charging |
| v.oai.thstatus | Six independent slots; c, b, e, s, m, sk, sa; validate ranges |
| v.oai.rgbcfg | Store both zones; ambient color drives the screen accent |
| host.focused_app / lights.preview | Acknowledge without storing private content |
| Unknown method with id | −32601, Method not found; invalid params use −32602 |

Standard `method`/`params` and compact `m`/`p` are accepted. Responses preserve
string/numeric/null IDs; notifications without IDs do not get responses. Numeric
effects 0/4 and named off/breath work; unfamiliar effects use static color.
Only valid RPC marks **Host Active**, which is not proof of ChatGPT identity.
Input readiness is separate: encryption, notification subscription, MTU ≥66
and a usable connection are required, but a previous RPC is not. This permits
the desktop's input-only mode without falsely claiming successful RPC. A link
being terminated cannot be reopened by a host resume command.

SOC is sampled from CW2017 in a separate task, every 15 seconds. Failure returns
`battery:null` and `battery_available:false`; the Battery characteristic does not
invent a value. This BSP has no charging-status API/pin: `is_charging:null` and
`charging_available:false` explicitly indicate unknown state. Desktop handling
of these unknown values needs real host verification.

The LVGL pool is 32 KiB; the existing single 40-line RGB565 DMA buffer is 19.2 KB.
No full-screen target framebuffer, PSRAM allocation or audio stream is used.
The fixed RX/response buffers consume about 8.7 KB; task stacks and JSON objects
use internal RAM. Health logs report free/minimum heap, largest block and app
stack watermark; real-device memory/timing remains an acceptance item.

## Build, validation and flashing

Activate the existing **ESP-IDF 5.5.3** installation as described in the upstream
engineering guide. The dependency lock and minimal 8 MB partition table remain
unchanged. Then, from the project root:

The build identifier and canonical image prefix retain `FoloToy-AI-Passport`
for the shared validation/archive tools; the application product and UI are
Codex Passport. Release assets may rename the verified merged image to `full.bin`.

```bash
idf.py build
./tools/validate.sh
CODEX_SANITIZE=1 ./tools/test_codex.sh build/codex-sanitizers
```

The complete gate runs repository/host tests, an isolated build, actual LVGL
host rendering, merged-image layout checks and ELF/image archive checks. Pure
and stub-radio tests do not require hardware or an installed SDK; their pinned
cJSON copy is documented in `tests/vendor/cjson/README.md`. Rendering does need
the build's pinned LVGL component and native CMake/compiler.

Use the gate's `build/FoloToy-AI-Passport-full.bin` at **0x0**. Do not flash an app-only image at 0x0.
The gate keeps the matching ELF/MAP/bootloader/partition images and manifest in
`build/firmware/<full-image-sha256>/`. Verify that exact bundle with:

```bash
python3 tools/archive_firmware.py verify build/firmware/<full-image-sha256>
```

**No device is flashed by the validation commands.** Only after explicit approval,
identify the actual USB Serial/JTAG port and run:

```bash
python -m esptool --chip esp32c3 --port PORT --baud 460800 \
  write_flash --flash_mode dio --flash_size 8MB --flash_freq 80m \
  0x0 build/FoloToy-AI-Passport-full.bin
idf.py -p PORT monitor
```

Merged-image gaps can reset NVS preferences and bonds; preserve needed user
data first. A compatible segmented flash may be necessary for preservation.
There is no full-chip erase command or original-firmware readback prerequisite.
No firmware/ELF/logs are published or pushed by these scripts.

## Authentication failure fixed in 0.1.1

The old device was correctly enumerated by macOS as 303A:8360, usage FF00/01,
with 64-byte HID reports including ID 6. System logs nevertheless reported
`Authentication is insufficient` for both the input CCCD and output report.
That explains **ENC yes / NTF no / RX 0** and the desktop's limited status.
The pinned SDK permission check reproduced this failure before the correction
and passes after it; SDK-free radio mocks had missed it. The complete gate now
also executes the actual SDK ATT/GATT permission functions against fixtures.
It checks encrypted Just Works access and rejection of unencrypted/short-key
report access. No private desktop implementation is included in the firmware.

After an authorized update, verify **NTF yes**, a growing RX count and real
button actions. If the old device remains cached, forget it in macOS and use
the device's confirmed Re-pair action before pairing again. This repair has
not yet been flashed or validated on the physical device.

## macOS acceptance — all device/desktop steps pending

1. Compilation is complete; flash this exact verified bundle only after approval.
2. Confirm the new Codex Passport screen at boot, without the test menu.
3. Confirm Advertising, and find **Codex Micro** in macOS Bluetooth settings.
4. Pair in a trusted environment; inspect actual VID 303A, PID 8360, usage FF00
   and Report ID 6, rather than trusting source constants.
5. Open ChatGPT Desktop and grant Input Monitoring when prompted.
6. Open Settings → Codex Micro and confirm the desktop app discovers the device.
7. Confirm a device.status round trip, Host Active, encryption, notifications
   and MTU ≥66 in Diagnostics/logs. Bluetooth connection alone is insufficient.
8. Exercise Agent 1–6, including single/double clicks and focus restoration.
9. Test Fast, confirmed Approve/Decline, Fork and Send; cancel/expire confirmation.
10. Test computer-mic hold/release, double-click lock and another click to stop.
11. Test all four stick directions, neutral release and the same direction again.
12. Test one CW/CCW tick per click and Dial Click holds longer than 500 ms.
13. Confirm independent Agent colors, brightness/off and light breathing.
14. Verify actual battery/unknown charging, disconnection while held, advertising,
    restored subscription and power-cycle bond reuse.
15. Test sidebar brightness/save, screen-off/wake without accidental HID, local
    re-pair confirmation, forgetting the device on Mac and pairing again.

If Bluetooth pairs but ChatGPT does not detect the device, check in this order:

| Check | Expected evidence |
| --- | --- |
| PnP endian / enumeration | VID/PID observed on macOS, exact seven PnP bytes |
| HID descriptor / references | FF00, application 1, ID 6, 63-byte input/output |
| Notification subscription / MTU | NTF yes, encrypted, MTU at least 66 |
| GATT permissions / security | Successful SC encryption, not just connection |
| device.status | Request and matching-ID reply, RX increasing, no TX failures |
| Cached HID descriptor | Forget old device on Mac, local Re-pair, pair again |
| Input Monitoring | Enable ChatGPT access in Privacy & Security; reopen the app |
| Both logs | Compare firmware method/error/lag logs with desktop diagnostics |

Use `ioreg -r -c IOHIDDevice -l` locally for enumeration; avoid sharing unrelated
device identifiers. USB-port enumeration is read-only and never authorizes
flashing. The old firmware's enumeration/failure was inspected read-only;
repaired-firmware pairing/actions, electrical button timing, charging and
physical display behavior remain unverified on FoloToy.

## References and execution record

- Core2 transport/UI reference, read at commit
  [2ee23a4](https://github.com/imliubo/codex-micro-4-core2/tree/2ee23a4ab696f94bb78d250f28cc4a9b879ba079):
  `src/CodexMicroBle.cpp`, `docs/TECHNICAL.md`, `src/main.cpp`. No Arduino or
  M5Stack UI implementation was copied.
- Reverse-engineered interface/default facts at
  [64258eb](https://github.com/eliBenven/freemicro/tree/64258eb6cc3312a43f9f9f86d87e55e0b609ccc5):
  `docs/PROTOCOL.md`, `docs/FACTORY-DEFAULTS.md`. Vendor proprietary app code was
  not copied. Conflicting lighting-preview claims are not treated as verified.
- [Official behavior and macOS setup](https://learn.chatgpt.com/docs/features/codex-micro).
- Upstream `origin/demo/claude-buddy-port` and `docs/reference/`: task ownership,
  encrypted BLE lifecycle, ADC identity filtering and bounded memory patterns.

Additional layers, physical lighting, OTA, multi-host channels, physical
continuous analog stick and Passport microphone streaming are out of scope.
See `codex-passport-validation.md` for automated test results, regression coverage and remaining hardware checks.
