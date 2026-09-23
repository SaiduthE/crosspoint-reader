"""Fixtures for run-core-test.js: raw 8-bit grey pages at 1404x1872 (fit,
centred on white) from the test books, plus a deflated zip for openZip."""
import sys, os, zipfile, io
from PIL import Image
import pymupdf

W, H = 1404, 1872
src_dir, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)

def fit(img):
    img = img.convert("L")
    s = min(W / img.width, H / img.height)
    r = img.resize((round(img.width * s), round(img.height * s)), Image.LANCZOS)
    page = Image.new("L", (W, H), 255)
    page.paste(r, ((W - r.width) // 2, (H - r.height) // 2))
    return page

pages = []
pc = os.path.join(src_dir, "pc01", "en_Pepper-and-Carrot_by-David-Revoy_E01P01.jpg")
pages.append(("pepper", fit(Image.open(pc))))
doc = pymupdf.open(os.path.join(src_dir, "enbj001.pdf"))
for i in (0, 12):
    pix = doc[i].get_pixmap()
    pages.append((f"bj{i}", fit(Image.frombytes("RGB", (pix.width, pix.height), pix.samples))))
for name, p in pages:
    open(os.path.join(out, name + ".gray"), "wb").write(p.tobytes())
    p.save(os.path.join(out, name + "_src.png"))

with zipfile.ZipFile(os.path.join(out, "deflated.zip"), "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("Vol 1/002.txt", "second " * 1000)
    z.writestr("Vol 1/010.txt", "tenth")
    z.writestr("__MACOSX/._x", "junk")
    z.writestr("Vol 2/Ünïcode.txt", "unicode name")
print("fixtures:", [n for n, _ in pages])
