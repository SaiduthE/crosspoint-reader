@AGENTS.md

## e-Minimal fork

This checkout is the **e-Minimal** fork of CrossPoint: a DIY ESP32-S3 reader
with a 7.8" Waveshare IT8951E panel, not the Xteink X4 AGENTS.md describes.
`AGENTS.md` stays upstream-pristine (it rebases against
crosspoint-reader/crosspoint-reader); fork-only facts live here.

**Board.** Freenove ESP32-S3 devkit, N16R8 module (16 MB flash, 8 MB octal
PSRAM), `qio_opi` memory type (not `dio_opi`). Panel: IT8951 1872x1404,
`uiScale` 1.5 (the UI fonts are the 2x tier — Ubuntu 20/24, NotoSans 16
small; only the 12 px hint font is 1.5x; see `docs/web-ui.md` and
`e-Minimal-Notes/tools/ui_tune.md`).

**Five buttons, not seven.** No side buttons: four front keys in a row under
the screen — Back (GPIO 7), Confirm/Select (GPIO 6), Up (GPIO 4), Down
(GPIO 5) — plus Power (GPIO 15). `Button::Left`/`Button::Right` map to
`PIN_UNASSIGNED` here and never fire. The four front keys are user-remappable
(Settings › Controls › Remap Front Buttons, stored as `buttonMap*`, resolved
in `MappedInputManager::mapButton()`); page-turn direction is "Page Turn
Buttons" (the `sideButtonLayout` field); "Orient Front Buttons" turns Up/Down
with the reader's rotation. Detect this board at runtime with
`five_button::active()` (`src/util/FiveButtonInput.h`); **never** branch on
`Button::Left`/`Button::Right` reaching the user, and read keys through
`MappedInputManager`, not `HalGPIO::BTN_*`, unless a site must stay physical.
Pin table: `e-Minimal-Notes/MAP.md` §2; controls: `docs/eminimal-controls.md`.

**Build env.** Always pass `-e eminimal`: `platformio.ini:2`
`default_envs = default` is the Xteink C3 target; on a clone without
`platformio.local.ini` (which pins `default_envs = eminimal` on this PC), a
bare `pio run` builds the wrong chip.

**Console/flash ports.** COM5 = native USB (USB-Serial-JTAG): console,
flashing, `CMD:` commands. COM4 = CH343 UART0, ROM banner only.
`platformio.local.ini` (gitignored, new) pins `upload_port`/`monitor_port`
to COM5 so `pio run -e eminimal` needs no `--upload-port`.

**Sandboxed `pio` hangs.** `pio run` hangs at SCons startup under a
sandboxed Bash tool; run build/flash with the sandbox off or hand them to
the user. `e-Minimal-Notes/tools/dev.ps1` wraps build/flash/monitor/
capture/size for this env and port — see `MAP.md` §3.

**clang-format.** Needs clang-format >= 21, not installed on this PC;
`.\bin\clang-format-fix.ps1 -g` fails here. Hand-match 120 cols / 2-space
indent; CI's format check is the backstop. Don't set `core.hooksPath
.githooks` until clang-format 21 is on PATH.

**Firmware size.** `python scripts/firmware_size_history.py --env eminimal
--commits <refs>` — the script defaults to env `default`, the wrong chip.

Read `e-Minimal-Notes/MAP.md` (navigation) and `e-Minimal-Notes/STATUS.md`
(current state) before non-trivial work.

**Submodule order.** `freeink-sdk` is a real submodule: commit and push it
first, bump and commit the fork's gitlink, then push the fork. Never run
`git submodule update` while `freeink-sdk` has commits the fork's gitlink
does not record yet: it detaches the submodule HEAD at the recorded SHA and
those commits vanish from the checkout (recoverable only from the SDK
branch or reflog).

**Settings enums are append-only.** `CrossPointSettings.h` enums and their
`SettingsList.h` label arrays are persisted by index in `settings.json`;
only append new values at the end, never insert or reorder.

**Corrections to AGENTS.md:**
- Web-UI HTML source lives in `src/network/html/*.html`, not `data/html/`
  (no `data/html/` in this fork).
- Cache-version constants are `BOOK_CACHE_VERSION`
  (`lib/Epub/Epub/BookMetadataCache.cpp`) and `SECTION_FILE_VERSION`
  (`lib/Epub/Epub/Section.cpp`); bump alongside `docs/file-formats.md`.
- AGENTS.md's hardware section describes the Xteink X4; the Board note
  above is what applies here.
