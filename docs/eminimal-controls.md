# e-Minimal five-button controls

e-Minimal has five physical buttons: four front keys in a row under the
screen — Back, Select (Confirm), Up, Down, left to right as the hint bar
draws them — plus Power. No side buttons, no Left/Right, no touch.
`five_button::active()`
(`src/util/FiveButtonInput.h`) is the runtime gate every screen below checks:
true whenever the board has no touch, is `DigitalButtons`, and has Left or
Right unassigned. Upstream boards with a real Left/Right keep their own grammar.
Everything below names the logical roles; which physical key plays each role
is the user's key map (next section).

## Key map and orientation
- **Remap Front Buttons** (Settings > Controls) assigns the four front keys
  to Back, Select, Up, Down. The wizard asks "Press the button for …" for each
  role in turn; a key already taken is refused. Keys assign on release. After
  the fourth, a summary shows the new layout and waits: pressing the new
  Select key (hinted "Save") saves; nothing is written before that. Hold Back
  (the current Back) 1 s at any point to cancel; hold Select (the current
  Select) 1 s to reset to the default layout and exit. Power is not a role;
  its hold still opens the power menu. Stored as `buttonMapBack`,
  `buttonMapConfirm`, `buttonMapUp`, `buttonMapDown` (HalGPIO::BTN_* indices)
  in settings.json; anything but a permutation of the four keys resets to
  the default (`CrossPointSettings::validateButtonMap()`).
- **Page Turn Buttons** picks the reader direction: "Up = back, Down =
  forward" (default) or "Down = back, Up = forward". Same `sideButtonLayout`
  field and indices as upstream's Side Button Layout; its other values
  (Disabled, Next/Next, Prev/Prev) aren't offered here.
- **Orient Front Buttons** (off by default) turns Up/Down with the device when
  the reader is rotated: swapped in Inverted and Landscape CCW (the Down key
  comes first on screen there), unchanged in Portrait and Landscape CW. The
  hint bar labels swap with them. Table: `MappedInputManager::upDownKey()`.
- Stays physical whatever the map: the boot-time Up+Power recovery hold and
  the Power+Down screenshot chord. The boot-time Back hold (skip resuming the
  book) follows the map.

## Home
Up/Down walk one flat list: current book, then the grid, then the bottom
tabs, wrapping at both ends. Hold Down jumps to the tabs, hold Up to the
first book. Select opens the highlighted item; Back resumes the most
recently read book directly (when one exists).

## Library / Settings ring
Up/Down move the tab band; Select drops into that tab's rows, which loop;
Back climbs back out to the tab band. Hold Select opens that tab's own
options (e.g. sort order) only on the tab band; on a row it opens
recent-book options, delete, or collapse instead, where offered
(`LibraryListActivity.cpp:727-738`).

## Reader
Up/Down turn pages (direction: Page Turn Buttons); hold either to skip a
chapter, when enabled in Settings (Long-press behaviour). With Orient Front
Buttons on, a rotated page keeps "up" on screen as Up (see Key map above).
Select opens the reader menu (forced to List style
here — Toolbar needs touch). Hold Select drops/clears a bookmark, when
enabled in Settings (Long-press menu function) — both are OFF by default
(`longPressButtonBehavior = OFF`, `longPressMenuFunction = LP_MENU_DISABLED`
in `CrossPointSettings.h`). Back short
goes Home; held past `GO_BACK_OR_HOME_MS` it opens the file browser instead
(or the reverse, if `backShortToFileBrowser` is set).

## Keyboard (on-device)
A linear walk over the key grid, not Left/Right + Up/Down cursor motion:
Up/Down step through keys row-major, wrapping row-to-row and end-to-end.
Select types the highlighted key; hold Select submits. Back deletes a
character (cancels on an empty field); hold Back always cancels.

## Text entry chooser (reader or phone)
Two rows: type on this reader, or on a phone via a QR code. Select picks
one; hold Select remembers it as the default (Settings > Controls > Text
Entry) and skips the chooser next time.

## Go to % / Time to sleep
Up/Down press for ±1 (±1 minute); hold either to repeat at the dialog's
large step: Go to % steps by 10, Time to Sleep by 5 (`SettingsActivity.cpp:492`).
Hints read "Press Up/Down: 1" / "Hold Up/Down: <large step>"; the Up slot
shows "+", Down shows "-".

## Dictionary word select
A tap of Up/Down steps one word at a time. Holding either (≥500 ms) jumps a
line instead, repeating every 350 ms; the release ending a line-jump hold
does not also step a word. Select looks up the word; Back returns to the reader.

## Wi-Fi list
The network list gains a trailing "Scan again" row (Select rescans), since
there is no Right button to rescan with directly. Hold Select on a saved
network forgets it. The save/forget prompts hint Up/Down in place of
Left/Right to move between Yes/No.

## OPDS browser
Up/Down move the row selection; Select opens or downloads the entry; hold
Select opens search when the feed offers one (no synthetic row, which would
shift every entry's index). Back climbs the feed hierarchy.

## Power
Hold Power ~0.7 s while awake for the power menu (sleep, shutdown, restart).
Hold Power ~1 s while asleep to wake. A short tap does nothing either way —
no double-click frontlight shortcut here.

## Hidden on this board, and why
Settings hides Menu Style in the reader section (forced to List; Toolbar
needs Left/Right or touch). Upstream's Side Button Layout shows as Page Turn
Buttons with its two directions only — Disabled, Next/Next and Prev/Prev
leave no key turning one way. Display hides UI Theme: e-Minimal ships its
own theme only (`FREEINK_DEVICE_EMINIMAL`; `fromJson()` pins `uiTheme` to
`EMINIMAL`). Home Button settings stay gated on `hasHomeKey()`, which this
board lacks.

## Divergence from the vault
The Obsidian vault's 04 §2 button table predates this pass and differs (Back
short = "advance ring", Select long = "full refresh"). What is actually
built: Back climbs rather than advancing a ring, and Refresh lives in the
power menu, not on a held Select. Treat this file as current until the vault
is amended.
