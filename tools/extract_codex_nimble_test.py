#!/usr/bin/env python3
"""Extract pinned SDK permission functions for a host regression, not firmware.

No SDK implementation is vendored: the unmodified functions and their license
notice are read from the activated ESP-IDF installation into the test build.
Fail rather than silently substituting a hand-written security model.
"""
import pathlib
import re
import sys

idf, output = map(pathlib.Path, sys.argv[1:])
host = idf / "components/bt/host/nimble/nimble/nimble/host"
chunks = []
for filename, names in (
    ("ble_gatts.c", ("ble_gatts_chr_clt_cfg_flags_from_chr_flags", "ble_gatts_att_flags_from_chr_flags")),
    ("ble_att_svr.c", ("ble_att_svr_check_perms",)),
):
    source = (host / "src" / filename).read_text()
    chunks.append(source[:source.index("*/") + 2])
    for name in names:
        pattern = rf"^static (?:int|uint8_t)\n{re.escape(name)}\([^{{]+^\{{.*?^\}}"
        match = re.search(pattern, source, re.MULTILINE | re.DOTALL)
        if not match:
            raise SystemExit(f"Missing pinned NimBLE function: {name}")
        chunks.append(match.group())

# Confirm fixture permission constants against actual SDK headers.
for filename, prefix in (("ble_att.h", "BLE_ATT_F_"), ("ble_gatt.h", "BLE_GATT_CHR_F_")):
    source = (host / "include/host" / filename).read_text()
    for name, value in re.findall(rf"^#define ({prefix}\w+)\s+(0x[0-9a-fA-F]+)\b", source, re.MULTILINE):
        chunks.append(f'#ifdef {name}\n_Static_assert({name} == {value}, "SDK flags: {name}");\n#endif')

output.write_text("\n\n".join(chunks) + "\n")
print("Extracted actual NimBLE ATT/GATT permission functions for regression")
