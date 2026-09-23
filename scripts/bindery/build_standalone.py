"""Builds the standalone e-Minimal Convert page: one HTML file with the shared
stylesheet, bindery-core.js, bindery-app.js and pdf.js inside it. Opens from
disk in any browser with no network; share the file itself.

    python scripts/bindery/build_standalone.py [out.html]

Default output: build/bindery/eminimal-convert.html (build/ is git-ignored).
The page source is src/network/html/ConvertPage.html, the same file the
reader serves at /convert, so the two never drift apart.
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
HTML = os.path.join(ROOT, "src", "network", "html")


def read(*parts):
    with open(os.path.join(HTML, *parts), encoding="utf-8") as f:
        return f.read()


def script_safe(js):
    # Inside <script>, "</script" and "<!--" end or confuse the element.
    return re.sub(r"</(script)", r"<\\/\1", js, flags=re.I).replace("<!--", "<\\!--")


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "bindery", "eminimal-convert.html")
    page = read("ConvertPage.html")

    def swap(old, new):
        nonlocal page
        if old not in page:
            raise SystemExit(f"build_standalone: expected to find {old!r} in ConvertPage.html")
        page = page.replace(old, new)

    swap('<link rel="stylesheet" href="/css/app.css" />', "<style>\n" + read("css", "app.css") + "\n</style>")
    swap('<script src="/js/app.js" defer></script>', "")
    swap('<script src="/js/bindery-core.js" defer></script>', "")
    swap('<script src="/js/bindery-app.js" defer></script>', "")
    swap('data-bindery-host="device"', 'data-bindery-host="standalone"')
    swap("<title>Convert – e-Minimal</title>", '<title>e-Minimal Convert</title>\n    <link rel="icon" href="data:," />')

    tail = (
        '<script type="text/plain" id="pdfjs-main">' + script_safe(read("js", "pdf.min.js")) + "</script>\n"
        '<script type="text/plain" id="pdfjs-worker">' + script_safe(read("js", "pdf.worker.min.js")) + "</script>\n"
        "<script>\n" + script_safe(read("js", "bindery-core.js")) + "\n</script>\n"
        "<script>\n" + script_safe(read("js", "bindery-app.js")) + "\n</script>\n"
    )
    swap("</body>", tail + "</body>")

    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(page)
    print(f"wrote {out} ({os.path.getsize(out) / 1024:.0f} KB)")


if __name__ == "__main__":
    main()
