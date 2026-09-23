"""Reads Bindery outputs the way the reader does.

XTC/XTCH: re-implements XtcParser.cpp (header, metadata, chapters, page
table, page headers) and XtcReaderActivity's getPixelValue, and renders
each page to PNG with the greys the reader would draw. EPUB: zip layout,
stored mimetype first, every XML file well-formed, OPF manifest complete."""
import struct, sys, os, zipfile
import xml.dom.minidom
from PIL import Image

out = sys.argv[1]
fail = 0
def check(c, m):
    global fail
    print(("ok   " if c else "FAIL ") + m)
    fail += (not c)

def read_xtc(path):
    b = open(path, "rb").read()
    magic, vmaj, vmin, n, rdir, has_meta, has_thumb, has_ch, cur, meta_off, table_off, data_off, thumb_off, ch_off, pad = \
        struct.unpack_from("<IBBHBBBBIQQQQII", b, 0)
    check(magic in (0x00435458, 0x48435458), f"{os.path.basename(path)}: magic {magic:#x}")
    depth = 2 if magic == 0x48435458 else 1
    check((vmaj, vmin) == (1, 0), "version 1.0")
    title = b[0x38:0x38 + 127].split(b"\0")[0].decode()
    author = b[0xB8:0xB8 + 63].split(b"\0")[0].decode()
    print(f"     {n} pages, {depth}-bit, rtl={rdir}, title={title!r}, author={author!r}")
    # chapters exactly as readChapters(): read u64 at 0x30, 96-byte records up to the page table
    ch_off64 = struct.unpack_from("<Q", b, 0x30)[0]
    chapters = []
    if has_ch == 1 and table_off >= 56 and ch_off64:
        count = (table_off - ch_off64) // 96
        for i in range(count):
            r = b[ch_off64 + i * 96: ch_off64 + (i + 1) * 96]
            name = r[:80].split(b"\0")[0].decode()
            s, e = struct.unpack_from("<HH", r, 0x50)
            if not name and s == 0 and e == 0: break
            chapters.append((name, s - 1, e - 1))
    print("     chapters:", chapters)
    check(table_off + n * 16 <= len(b), "page table inside the file")
    pages = []
    for i in range(n):
        off, size, w, h = struct.unpack_from("<QIHH", b, table_off + i * 16)
        pm, pw, ph, cm, comp, dsize = struct.unpack_from("<IHHBBI", b, off)
        want = 0x00485458 if depth == 2 else 0x00475458
        bitmap = (pw * ph + 7) // 8 * 2 if depth == 2 else (pw + 7) // 8 * ph
        ok = pm == want and (pw, ph) == (w, h) and dsize == bitmap and size == 22 + bitmap and off + size <= len(b)
        check(ok, f"page {i}: {w}x{h} at {off}, {size} bytes")
        data = b[off + 22: off + 22 + bitmap]
        img = Image.new("L", (pw, ph))
        px = img.load()
        if depth == 2:
            plane = (pw * ph + 7) // 8
            colb = (ph + 7) // 8
            grey = {0: 255, 1: 85, 2: 170, 3: 0}   # 0 white, 1 dark, 2 light, 3 black
            hist = [0, 0, 0, 0]
            for x in range(pw):
                base = (pw - 1 - x) * colb
                for y in range(ph):
                    o = base + y // 8; bit = 7 - y % 8
                    v = ((data[o] >> bit) & 1) << 1 | ((data[plane + o] >> bit) & 1)
                    hist[v] += 1
                    px[x, y] = grey[v]
            print("     pixel values 0..3:", hist)
        else:
            rb = (pw + 7) // 8
            for y in range(ph):
                for x in range(pw):
                    px[x, y] = 255 if (data[y * rb + x // 8] >> (7 - x % 8)) & 1 else 0
        name = f"{os.path.splitext(os.path.basename(path))[0]}_{os.path.splitext(path)[1][1:]}_p{i}.png"
        img.save(os.path.join(out, name))
        pages.append(name)
    return pages

def check_epub(path):
    z = zipfile.ZipFile(path)
    infos = z.infolist()
    check(infos[0].filename == "mimetype" and infos[0].compress_type == 0 and z.read("mimetype") == b"application/epub+zip",
          f"{os.path.basename(path)}: stored mimetype first")
    check(z.testzip() is None, "CRCs all match")
    for i in infos:
        if i.filename.endswith((".xml", ".opf", ".ncx", ".xhtml")):
            try: xml.dom.minidom.parseString(z.read(i.filename))
            except Exception as e: check(False, f"{i.filename} well-formed: {e}")
    opf = xml.dom.minidom.parseString(z.read("OEBPS/content.opf"))
    hrefs = ["OEBPS/" + it.getAttribute("href") for it in opf.getElementsByTagName("item")]
    missing = [h for h in hrefs if h not in z.namelist()]
    check(not missing, f"manifest complete ({len(hrefs)} items){' missing ' + str(missing) if missing else ''}")
    print("     spine:", len(opf.getElementsByTagName("itemref")), "items;",
          [m.firstChild.data for m in opf.getElementsByTagName("meta") if m.getAttribute("property") == "rendition:layout"])

for f in ("test.xtch", "test.xtc"): read_xtc(os.path.join(out, f))
for f in ("manga.epub", "text.epub"): check_epub(os.path.join(out, f))
sys.exit(1 if fail else 0)
