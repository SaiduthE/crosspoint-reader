# The web UI — design contract

The pages the reader serves to a phone or laptop: Home, Files, Settings, Fonts,
and the Wi-Fi setup page. This is the contract every page is built against so
they read as one product. Anything not covered here follows the reference
implementation in `src/network/html/HomePage.html`.

## Subject and job

An e-ink reader, **e-Minimal**, with four buttons and no keyboard. These pages
exist because a phone has the keyboard and the file picker the device lacks.
The primary job is *get a book onto the card from a phone in under a minute*;
the secondary jobs are changing reading settings and adding fonts. Most visits
happen on a phone held in one hand, some on a laptop, occasionally inside a
captive-portal sheet (iOS CNA / Android sign-in webview) with no internet.

## Direction: the panel, extended

The phone page looks like the reader's own screen continued: ink on a pale,
neutral grey-white, no colour, no depth. E-ink has sixteen greys and no
shadows, so the UI has greys and rules, never drop shadows or gradients. The
one bold element is the **navigation drawer / sidebar, rendered inverted —
white on black — like the device's selected row.** Everything else is quiet.

### Tokens (`:root`, dark under `prefers-color-scheme: dark` — the panel inverted)

| token | light | dark | use |
|---|---|---|---|
| `--paper` | `#f2f2ef` | `#000000` | page background |
| `--surface` | `#ffffff` | `#141414` | cards, inputs |
| `--ink` | `#000000` | `#f2f2ef` | text, primary buttons, rules of emphasis |
| `--ink-2` | `#555555` | `#b0b0b0` | secondary text, labels |
| `--ink-3` | `#8a8a8a` | `#7a7a7a` | placeholders, disabled, hints |
| `--rule` | `#d6d6d2` | `#2a2a2a` | hairline borders, dividers |
| `--rule-2` | `#eaeae6` | `#1c1c1c` | row separators inside lists/tables |
| `--danger` | `#b3261e` | `#ff6b62` | destructive actions and error text only |
| `--ok` | `#1b6e3a` | `#5cc48a` | success text only |
| `--focus` | `#000000` | `#f2f2ef` | 2 px focus ring, offset 2 px |

No other colours. State is carried by weight, inversion and the two functional
colours. Never a tinted near-black: black is `#000`.

### Type

- **Serif for titles**: `"Iowan Old Style", Charter, "Noto Serif", Georgia, serif`
  — the device sets books in a serif. Page title (`h1`) and card titles (`h2`).
- **Sans for everything else**: `system-ui, -apple-system, "Segoe UI", Roboto, "Noto Sans", sans-serif`.
- Scale: base `16px`; `h1` 1.5rem/1.2 weight 600; `h2` 1.125rem/1.3 weight 600;
  body 1rem/1.5; small 0.875rem; tiny 0.8125rem (only for meta).
- Sentence case throughout. No all-caps labels, no tracked-out eyebrows, no
  emoji in headings or buttons, no `→` on links.
- Line length ≤ 72ch for running text.

### Shape and space

- Radius: `4px` on controls and cards, `999px` only on chips/toggles. One radius, not several.
- Borders are 1 px `--rule`. Emphasis is a 2 px `--ink` rule, never a shadow.
- Spacing scale: 4 / 8 / 12 / 16 / 24 / 32 px. Page gutter 16 px on phones, 32 px from 900 px.
- Motion only in answer to a tap (drawer open/close 180 ms, modal 120 ms), and
  none under `prefers-reduced-motion`.
- Tap targets ≥ 44 px tall on phones.

## The shell

`/js/app.js` injects the shell into `<body>` at load; the page ships only its
`<main>`. `/css/app.css` styles both. Both are served by the device with an
ETag and cached.

```
phone (< 900 px)                       laptop (≥ 900 px)
┌────────────────────────────┐         ┌──────────┬───────────────────────────┐
│ ☰  Files              ▮▮▮  │ topbar  │ e-Minimal│ Files               ▮▮▮   │
│────────────────────────────│         │          │───────────────────────────│
│ <main> 16 px gutters       │         │ Home     │ <main> max-width 880 px   │
│                            │         │ Files    │                           │
│                            │         │ Settings │                           │
│                            │         │ Fonts    │                           │
│                            │         │          │                           │
│                            │         │ eMinimal │                           │
│                            │         │ 192.168… │                           │
└────────────────────────────┘         └──────────┴───────────────────────────┘
 drawer: off-canvas from the left,       sidebar: 232 px, persistent, black.
 black, backdrop, closes on tap/Esc.
```

Markup app.js produces (ids and classes are the contract):

```html
<header class="topbar">
  <button class="menu-btn" id="menuBtn" aria-controls="drawer" aria-expanded="false" aria-label="Menu">…</button>
  <div class="topbar-title" id="topbarTitle">Files</div>
  <div class="topbar-status" id="topbarStatus"></div>   <!-- link chip: mode + IP -->
</header>
<div class="backdrop" id="backdrop"></div>
<nav class="drawer" id="drawer" aria-label="Pages">
  <a class="brand" href="/">e-Minimal</a>
  <a href="/" data-page="home">Home</a>
  <a href="/files" data-page="files">Files</a>
  <a href="/settings" data-page="settings">Settings</a>
  <a href="/fonts" data-page="fonts">Fonts</a>
  <div class="drawer-foot" id="drawerFoot"></div>   <!-- hostname, IP, free memory, version -->
</nav>
```

- The page declares itself with `<body data-page="files" data-title="Files">`;
  app.js sets the active link and the topbar title from those.
- app.js fetches `/api/status` once and fills `#topbarStatus` ("Joined · 192.168.50.240"
  or "Hotspot · 192.168.4.1") and `#drawerFoot`. Failure leaves them empty; no error UI.
- Drawer state: `body.drawer-open`. Opened by `#menuBtn`, closed by backdrop,
  Esc, or any drawer link. Focus returns to the button on close.
- The brand link in the topbar is omitted on phones (the title is enough); the
  drawer carries it.
- app.js also exposes `window.ui = { toast(text, kind), confirm(text, {ok, danger}) → Promise<bool> }`
  so pages stop hand-rolling message divs. `toast` shows for 3 s at the bottom;
  kinds: `""`, `"ok"`, `"danger"`.

## Components (app.css)

Class names are the contract; pages use these and keep their own CSS to what is
genuinely page-specific, scoped under `body[data-page="…"]`.

| class | what |
|---|---|
| `.card` | surface, 1 px rule, radius 4, padding 16 (24 from 900 px); `> h2` is the card title |
| `.card-head` | flex row: title left, actions right, wraps on phones |
| `.kv` / `.kv > div` | key–value rows (Home status): label `--ink-2`, value right-aligned, hairline between rows |
| `.btn` | 40 px tall (44 on touch), 1 px `--ink` border, transparent; `.btn-primary` inverted (ink fill, paper text); `.btn-danger` `--danger` border and text, inverted on hover; `.btn-ghost` no border; `.btn-sm` 32 px |
| `.field` | label above control, 8 px gap; `.field-hint` small `--ink-2` below |
| `input, select, textarea` | 40 px tall, surface fill, 1 px rule, focus = 2 px `--ink` ring; `select` gets an inline-SVG chevron |
| `.toggle` | switch: 44 × 24 track, `--rule` off, `--ink` on, knob `--surface` |
| `.list` / `.list > .row` | vertical list of rows separated by `--rule-2`; row = flex, 44 px min, `.row-main` grows, `.row-meta` small `--ink-2` |
| `.table` | responsive table: from 700 px a real table with `th` in `--ink-2` small; below 700 px each row becomes a stacked block and cells show `data-label` as their key |
| `.toolbar` | flex row of buttons, 8 px gap, wraps; sticky under the topbar when `.toolbar-sticky` |
| `.modal-backdrop` / `.modal` | centred sheet on laptop, bottom sheet on phones (full width, radius top 8); `.modal-head`, `.modal-body`, `.modal-foot` (buttons right-aligned, primary last) |
| `.toast` | bottom-centred pill, inverted |
| `.chip` | small inline status (`.chip-ok`, `.chip-danger`) |
| `.empty` | empty state: one sentence of what to do next, centred, `--ink-2` |
| `.savebar` | sticky bottom bar (surface, top rule) with a summary left and `.btn-primary` right; hidden until there is something to save |
| `.section-nav` | Settings: on ≥ 900 px a sticky list of section links left of the content (`.section-nav a.active` = 2 px ink rule left); below 900 px a `select` labelled "Section" that scrolls to the section |
| `.progress` | 4 px track `--rule`, fill `--ink` |
| `.visually-hidden` | the usual |

Dark mode: tokens only; no component overrides.

## Rules for every page

1. **Ship only `<main>`** plus `<link rel="stylesheet" href="/css/app.css">` and
   `<script src="/js/app.js" defer>` (plus the page's own script). `<body data-page data-title>`.
2. **Keep every element id, every JS function and every API call exactly as it
   is.** The C++ server is the other half of these pages; endpoints, methods,
   query strings and JSON shapes do not change. The Files page's EPUB optimiser,
   WebSocket upload and retry logic stay byte-for-byte in behaviour.
3. **Copy**: product name "e-Minimal"; sentence case; buttons say what happens
   ("Upload", "Save changes", "Delete 3 files"); empty states say what to do
   ("No files yet. Upload a book to start."); errors say what went wrong and what
   to do. No "CrossPoint" anywhere the user can see; no emoji.
4. **Responsive**: 360 px wide with no horizontal scroll, 768 px, 1280 px. Phone
   first. Tables stack below 700 px. Modals become bottom sheets.
5. **Accessible floor**: visible focus, labels on every control, `aria-expanded`
   on disclosures, dialogs trap focus and close on Esc, `prefers-reduced-motion`
   honoured, contrast ≥ 4.5:1 (the palette guarantees it if used as specified).
6. **Budget**: the four pages together stay under the size they are today
   (FilesPage 231 KB raw is dominated by its script; do not grow it). Vanilla
   JS, no frameworks, no external fonts or CDNs — the captive portal has no
   internet. One deliberate exception: Convert serves pdf.js from flash
   (`pdf.min.js` 89 KB + `pdf.worker.min.js` 290 KB gzipped), fetched only when
   a PDF is added — there is no CDN to fall back on.
7. **Verify in a browser** before finishing: `python -m http.server 8000` from
   `src/network/html/` serves `/css/app.css` and `/js/app.js` at the right
   paths; open `http://localhost:8000/<Page>.html` at 360, 768 and 1280 px
   widths. API calls will fail there — the page must still lay out and show its
   empty/error states cleanly.

## Per-page notes

- **Home** (`HomePage.html`): the reference implementation. One `.card` of
  device status as `.kv` rows (Serial, Version, Network, IP address, Free
  memory); a second card of "What you can do here" with three links (Upload a
  book → /files, Change settings → /settings, Add a font → /fonts) as `.list`
  rows. Footer line: "e-Minimal · open source" in `--ink-3`, small — the only
  place a middle dot is fine.
- **Files** (`FilesPage.html`): the toolbar (Upload, New folder, Delete selected)
  becomes `.toolbar.toolbar-sticky`; breadcrumbs stay above the list; the file
  table becomes `.table` with columns Name, Size, Modified? (whatever exists),
  actions collapsed into a row-end menu on phones; the failed-uploads banner is a
  `.card` with `--danger` rule; every modal becomes `.modal`. The upload modal's
  optimiser options keep their ids and behaviour; restyle their controls with
  `.field`/`.toggle`.
- **Settings** (`SettingsPage.html`): categories are rendered by JS from
  `/api/settings` (`s.category`); render each category as a `.card` with an id
  `section-<slug>` and build `.section-nav` from the category list (sticky list
  on laptops, `select` on phones). Rows become `.list > .row` with the control
  right-aligned; `toggle` type uses `.toggle`, `enum` a `select`, `value` a
  number input, `string` a text/password input. Wi-Fi networks and OPDS servers
  are two more sections in the same nav. The save button becomes the `.savebar`
  ("3 changes" left, "Save changes" right), shown only when something changed.
- **Convert** (`ConvertPage.html` + `js/bindery-core.js`, `js/bindery-app.js`):
  manga, comics and books made ready on the phone. Two modes (`.btn-group`):
  *Manga and comics* takes CBZ/ZIP, PDF, image EPUBs and loose pictures and
  writes XTCH (default), XTC, fixed-layout EPUB or CBZ at the reader's size;
  *Books* turns TXT/Markdown/HTML into a reflowable EPUB with detected
  chapters and a made cover, and tidies EPUB pictures (shrink, grey,
  baseline — progressive JPEGs decode at 1/8 on the reader). "Send to
  reader" streams XTC pages over the Files page's WebSocket upload as they
  are drawn (header and page table first: every page is the same size), so
  memory on the phone stays flat; "Save to this phone" keeps them instead.
  The same file builds a standalone single-page copy
  (`scripts/bindery/build_standalone.py`, `data-bindery-host="standalone"`,
  no shell). Tests and the mock reader live in `scripts/bindery/`.
- **Fonts** (`FontsPage.html`): installed fonts as a `.list` (name, sizes,
  Delete as `.btn-danger.btn-sm`); upload as a `.card` with a `.field` file
  input and the progress bar as `.progress`. Empty state: "No fonts on the card
  yet. Upload a .cpfont family to use it in books."
- **Wi-Fi setup** (`WifiSetupPage.html`, served by a different server that
  also serves `/css/app.css` but not `/js/app.js`): no shell. A single centred
  `.card` under a serif `h1` "Connect your reader to Wi-Fi"; the form and the
  status logic stay as they are; restyle with `.field`, `.btn-primary`,
  `.chip`. Keep its inline CSS to the few rules the card needs beyond app.css.
