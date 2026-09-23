"""A stand-in for the reader's web server, for testing /convert on a PC.

Serves src/network/html the way CrossPointWebServer routes it (/convert,
/css/app.css, /js/*.js, /mkdir, /api/status) on PORT, and the WebSocket
upload on PORT+1 with the same protocol as onWebSocketEvent():
START:<name>:<size>:<path> -> READY | ERROR:..., binary chunks, then DONE.
Uploaded files land in CARD_DIR, mirroring the SD card.

    python scripts/bindery/mock_reader.py 8766 CARD_DIR
"""
import asyncio, os, sys, threading
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
import websockets

PORT = int(sys.argv[1]); CARD = os.path.abspath(sys.argv[2])
HTML = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "src", "network", "html")
os.makedirs(CARD, exist_ok=True)
ROUTES = {"/convert": "/ConvertPage.html", "/": "/HomePage.html", "/files": "/FilesPage.html"}

def card_path(web):
    return os.path.join(CARD, *[p for p in web.split("/") if p and p not in (".", "..")])

class H(SimpleHTTPRequestHandler):
    def __init__(self, *a, **k): super().__init__(*a, directory=HTML, **k)
    def log_message(self, *a): pass
    def do_GET(self):
        if self.path == "/api/status":
            body = b'{"version":"mock","ip":"127.0.0.1","mode":"STA","freeHeap":200000}'
            self.send_response(200); self.send_header("Content-Type", "application/json"); self.end_headers(); self.wfile.write(body); return
        self.path = ROUTES.get(self.path.split("?")[0], self.path)
        return super().do_GET()
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0)); raw = self.rfile.read(n)
        if self.path == "/mkdir":
            from email.parser import BytesParser
            msg = BytesParser().parsebytes(b"Content-Type: " + self.headers["Content-Type"].encode() + b"\r\n\r\n" + raw)
            form = {part.get_param("name", header="content-disposition"): part.get_payload(decode=True).decode() for part in msg.get_payload()}
            p = os.path.join(card_path(form.get("path", "/")), form["name"])
            code = 400 if os.path.exists(p) else 200
            os.makedirs(p, exist_ok=True)
            self.send_response(code); self.end_headers(); return
        self.send_response(404); self.end_headers()

async def ws_handler(ws):
    f = None; size = got = 0
    async for msg in ws:
        if isinstance(msg, str):
            if msg.startswith("START:"):
                name, sz, path = msg[6:].split(":", 2)
                target = os.path.join(card_path(path), name)
                if os.path.exists(target): await ws.send("ERROR:File already exists: " + name); continue
                if not os.path.isdir(card_path(path)): await ws.send("ERROR:Failed to create file"); continue
                f = open(target, "wb"); size = int(sz); got = 0
                print("START", target, size, flush=True)
                await ws.send("READY")
        else:
            if f is None: await ws.send("ERROR:No upload in progress"); continue
            if got + len(msg) > size: await ws.send("ERROR:Upload overflow"); f.close(); f = None; continue
            f.write(msg); got += len(msg)
            if got == size:
                f.close(); f = None; print("DONE", got, flush=True); await ws.send("DONE")
    if f: f.close(); print("ABORTED at", got, "of", size, flush=True)

def http(): ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
threading.Thread(target=http, daemon=True).start()
async def main():
    async with websockets.serve(ws_handler, "127.0.0.1", PORT + 1, max_size=None):
        print(f"mock reader on http://127.0.0.1:{PORT}/convert, ws {PORT + 1}, card {CARD}", flush=True)
        await asyncio.Future()
asyncio.run(main())
