# Convert (Bindery) — tools and tests

The Convert page (`src/network/html/ConvertPage.html`, served at `/convert`)
turns manga, comics and books into files the reader opens fast. The logic is in
`src/network/html/js/bindery-core.js` (pure: zip, dithering, XTC/EPUB writers,
text parsing) and `bindery-app.js` (decoding, drawing, preview, upload).

| file | what |
|---|---|
| `build_standalone.py` | one self-contained HTML file (pdf.js inside) → `build/bindery/eminimal-convert.html`. Opens from disk, no network. |
| `make_fixtures.py` | grey 1404×1872 test pages from real books, plus a deflated zip |
| `run-core-test.js` | runs `bindery-core.js` under Node: zip read/write, tone, XTC/XTCH, EPUBs, TXT/Markdown chapters |
| `check_outputs.py` | reads XTC/XTCH exactly as `XtcParser.cpp` + `XtcReaderActivity` do and renders each page to PNG; checks EPUB layout |
| `browser_test.py` | Playwright + installed Chrome against the standalone build: convert, save, screenshots |
| `mock_reader.py` | stand-in for the reader's web server: routes, `/mkdir`, and the WebSocket upload protocol, writing to a folder |

```sh
python scripts/bindery/make_fixtures.py <books dir> $TEMP/fx
PC_JPG=<a jpeg> node scripts/bindery/run-core-test.js $TEMP/fx $TEMP/out
python scripts/bindery/check_outputs.py $TEMP/out
python scripts/bindery/build_standalone.py
python scripts/bindery/mock_reader.py 8766 $TEMP/card   # then open http://127.0.0.1:8766/convert
```

Test books used (both free to redistribute): *Give My Regards to Black Jack*
by Shuho Sato (densho810.com/free, free secondary use), and *Pepper&Carrot* by
David Revoy (CC BY 4.0).

pdf.js 3.11.174 (Apache 2.0) is vendored as `src/network/html/js/pdf.min.js`
and `pdf.worker.min.js`; documents open with `isEvalSupported: false`.
