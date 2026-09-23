"""Drives the standalone page in headless Chrome: loads test books, takes
screenshots, converts, saves the outputs and checks the XTCH with the parser
mirror in check_outputs.py.

    python -m http.server 8765 -d build/bindery   (with fx/ inputs inside)
    python scripts/bindery/browser_test.py http://127.0.0.1:8765/eminimal-convert.html OUT_DIR
"""
import os, sys, time
from playwright.sync_api import sync_playwright

url, out = sys.argv[1], sys.argv[2]
fx = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build", "bindery", "fx")
os.makedirs(out, exist_ok=True)
errors = []

def save_all(page, n_expected):
    page.wait_for_selector("#results .row", timeout=600_000)
    page.wait_for_function(f"document.querySelectorAll('#results .row').length >= {n_expected} && !document.querySelector('#cancelBtn:not([hidden])')", timeout=600_000)
    saved = []
    for btn in page.query_selector_all("#results .row button"):
        with page.expect_download() as d:
            btn.click()
        dl = d.value
        path = os.path.join(out, dl.suggested_filename)
        dl.save_as(path)
        saved.append(path)
    return saved

with sync_playwright() as p:
    b = p.chromium.launch(channel="chrome", headless=True)
    for width, name in ((390, "phone"), (1280, "laptop")):
        ctx = b.new_context(viewport={"width": width, "height": 900}, accept_downloads=True, device_scale_factor=1)
        page = ctx.new_page()
        page.on("console", lambda m: m.type == "error" and errors.append(m.text))
        page.on("pageerror", lambda e: errors.append(str(e)))
        page.goto(url)
        page.screenshot(path=os.path.join(out, f"{name}-empty.png"), full_page=True)
        if name == "laptop":
            continue
        # Manga: a CBZ and a PDF, separate books, XTCH
        page.set_input_files("#fileInput", [os.path.join(fx, "PepperCarrot_E01_Potion-of-Flight.cbz"), os.path.join(fx, "bj-20pages.pdf")])
        page.wait_for_function("document.querySelectorAll('#fileList .row').length === 2 && !document.body.innerText.includes('Reading…')", timeout=120_000)
        page.wait_for_function("document.querySelector('#previewInfo').textContent.includes('page 1')", timeout=120_000)
        page.click("#nextPage"); page.click("#nextPage")
        page.wait_for_function("document.querySelector('#previewInfo').textContent.includes('page 3')", timeout=60_000)
        page.screenshot(path=os.path.join(out, f"{name}-manga.png"), full_page=True)
        t = time.time()
        page.click("#saveBtn")
        files = save_all(page, 2)
        print(f"manga converted in {time.time() - t:.1f} s:", [os.path.basename(f) for f in files])
        print("status:", page.inner_text("#runStatus"))
        page.screenshot(path=os.path.join(out, f"{name}-manga-done.png"), full_page=True)
        # Books: a Gutenberg text → EPUB
        page.click("#modeBooks")
        page.set_input_files("#fileInput", [os.path.join(fx, "alice.txt")])
        page.wait_for_function("document.querySelector('#chapterList .row')", timeout=60_000)
        page.screenshot(path=os.path.join(out, f"{name}-books.png"), full_page=True)
        print("books summary:", page.inner_text("#bookSummary"))
        print("chapters:", [r.inner_text().split("\n")[0] for r in page.query_selector_all("#chapterList .row")][:14])
        page.click("#saveBtn")
        print("books:", [os.path.basename(f) for f in save_all(page, 1)])
        ctx.close()
    b.close()
print("console errors:", errors or "none")
