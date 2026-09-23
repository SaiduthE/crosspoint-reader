/*
 * e-Minimal Convert — the page (ConvertPage.html).
 *
 * Decodes what the user drops in (CBZ/ZIP, PDF, EPUB, pictures, TXT, MD,
 * HTML), draws each page to the reader's size on a canvas, and hands grey
 * arrays to bindery-core.js for dithering and the file formats.
 *
 * Two hosts, one file:
 *   device      served by the reader at /convert. "Send to reader" streams
 *               an XTC/XTCH to the card over the Files page's WebSocket
 *               upload (port + 1) page by page, so a 200-page volume never
 *               has to fit in the phone's memory.
 *   standalone  scripts/bindery/build_standalone.py inlines everything into
 *               one HTML file; output is saved to the device instead.
 */
(function () {
  'use strict';

  const B = window.Bindery;
  const body = document.body;
  const HOST = body.getAttribute('data-bindery-host') || 'device';
  const $ = (id) => document.getElementById(id);
  const esc = B.esc;

  const PRESETS = { eminimal: { w: 1404, h: 1872 }, x4: { w: 480, h: 800 } };
  const MANGA_EXT = /\.(cbz|zip|pdf|epub|jpe?g|png|webp|gif|bmp|avif)$/i;
  const BOOK_EXT = /\.(txt|text|md|markdown|html?|xhtml|epub)$/i;
  const ARCHIVE_UNSUPPORTED = /\.(cbr|rar|cb7|7z|cbt|tar)$/i;
  const SERIF = '"Iowan Old Style", Charter, "Noto Serif", Georgia, serif';

  const state = {
    mode: 'manga',
    manga: [],
    books: [],
    preview: 0,
    busy: false,
    cancel: false,
    cover: null,
  };

  // ------------------------------------------------------------ utilities

  function toast(text, kind) {
    if (window.ui && window.ui.toast) return window.ui.toast(text, kind || '');
    // No shell (standalone): errors go to the status line, which already
    // says how a run ended.
    if (kind === 'danger' && !state.busy) { $('runProgress').hidden = false; $('runStatus').textContent = text; }
  }

  const tick = () => new Promise((r) => setTimeout(r, 0));

  function formatBytes(n) {
    if (n < 1024) return n + ' B';
    if (n < 1024 * 1024) return (n / 1024).toFixed(0) + ' KB';
    if (n < 1024 * 1024 * 1024) return (n / 1024 / 1024).toFixed(1) + ' MB';
    return (n / 1024 / 1024 / 1024).toFixed(2) + ' GB';
  }

  function formatTime(s) {
    if (s < 60) return Math.max(1, Math.round(s)) + ' s';
    const m = Math.round(s / 60);
    return m < 60 ? m + ' min' : Math.floor(m / 60) + ' h ' + (m % 60) + ' min';
  }

  const baseName = (name) => name.replace(/^.*[\\/]/, '').replace(/\.[^.]+$/, '');
  const prettyName = (name) => baseName(name).replace(/[_]+/g, ' ').replace(/\s+/g, ' ').trim();

  // Safe on FAT32 and in the upload's "START:name:size:path" message.
  function safeFileName(name, ext) {
    let n = String(name || 'Untitled').replace(/[\\/:*?"<>|\u0000-\u001f]+/g, ' ').replace(/\s+/g, ' ').trim();
    n = n.replace(/^\.+/, '').slice(0, 96).trim() || 'Untitled';
    return n + '.' + ext;
  }

  function canvas(w, h) {
    const c = document.createElement('canvas');
    c.width = Math.max(1, Math.round(w));
    c.height = Math.max(1, Math.round(h));
    return c;
  }

  const ctx2d = (c) => c.getContext('2d', { willReadFrequently: true });

  function loadScript(src) {
    return new Promise((resolve, reject) => {
      const s = document.createElement('script');
      s.src = src;
      s.onload = resolve;
      s.onerror = () => reject(new Error('Could not load ' + src));
      document.head.appendChild(s);
    });
  }

  function lsGet(key) {
    try { return JSON.parse(localStorage.getItem(key) || 'null'); } catch (_) { return null; }
  }
  function lsSet(key, value) {
    try { localStorage.setItem(key, JSON.stringify(value)); } catch (_) { /* private mode */ }
  }

  // ------------------------------------------------------------ decoding

  async function decodeImage(blob) {
    if (window.createImageBitmap) {
      try {
        const bmp = await createImageBitmap(blob);
        return { src: bmp, w: bmp.width, h: bmp.height, close: () => bmp.close && bmp.close() };
      } catch (_) { /* fall back to <img> */ }
    }
    const url = URL.createObjectURL(blob);
    try {
      const img = new Image();
      img.decoding = 'async';
      img.src = url;
      await img.decode();
      return { src: img, w: img.naturalWidth, h: img.naturalHeight, close: () => {} };
    } catch (_) {
      throw new Error('This picture could not be decoded by the browser.');
    } finally {
      setTimeout(() => URL.revokeObjectURL(url), 0);
    }
  }

  async function probeBlob(blob) {
    const head = new Uint8Array(await blob.slice(0, 256 * 1024).arrayBuffer());
    const s = B.imageSize(head);
    if (s && s.w && s.h) return s;
    const d = await decodeImage(blob);
    d.close();
    return { w: d.w, h: d.h };
  }

  let pdfjs = null;
  async function loadPdfJs() {
    if (pdfjs) return pdfjs;
    const inlineMain = $('pdfjs-main');
    const inlineWorker = $('pdfjs-worker');
    const blobUrl = (el) => URL.createObjectURL(new Blob([el.textContent], { type: 'text/javascript' }));
    if (!window.pdfjsLib) await loadScript(inlineMain ? blobUrl(inlineMain) : '/js/pdf.min.js');
    pdfjs = window.pdfjsLib || window['pdfjs-dist/build/pdf'];
    if (!pdfjs) throw new Error('The PDF engine did not load.');
    pdfjs.GlobalWorkerOptions.workerSrc = inlineWorker ? blobUrl(inlineWorker) : '/js/pdf.worker.min.js';
    return pdfjs;
  }

  // A PDF is read in 1 MB ranges straight from the File, so a 400 MB scan
  // does not have to be loaded whole on a phone.
  async function openPdf(file) {
    const lib = await loadPdfJs();
    const first = new Uint8Array(await file.slice(0, Math.min(file.size, 1 << 20)).arrayBuffer());
    const common = { isEvalSupported: false, disableFontFace: false };
    if (first.length >= file.size) return lib.getDocument({ ...common, data: first }).promise;
    const t = new lib.PDFDataRangeTransport(file.size, first);
    t.requestDataRange = (begin, end) => {
      file.slice(begin, end).arrayBuffer().then((b) => t.onDataRange(begin, new Uint8Array(b)));
    };
    return lib.getDocument({ ...common, range: t, length: file.size, rangeChunkSize: 1 << 20, disableAutoFetch: true, disableStream: true }).promise;
  }

  // ------------------------------------------------------------ manga items
  //
  // item: { id, name, kind, pages: [ref], chapters: [{ title, start }],
  //         meta: { title, author, rtl }, error }
  // ref:  { probe() → {w,h}, load(scaleHint) → drawable }

  let nextId = 1;

  async function loadMangaItem(file) {
    const item = { id: nextId++, name: file.name, file, kind: '', pages: [], chapters: [], meta: { title: prettyName(file.name) } };
    try {
      if (ARCHIVE_UNSUPPORTED.test(file.name)) throw new Error('RAR and 7z archives cannot be read here. Unpack it and zip the pages, or save it as .cbz.');
      if (/\.(cbz|zip)$/i.test(file.name)) await fromZip(item, file);
      else if (/\.pdf$/i.test(file.name)) await fromPdf(item, file);
      else if (/\.epub$/i.test(file.name)) await fromEpub(item, file);
      else throw new Error('Not a file type this page reads.');
      if (!item.pages.length) throw new Error('No pictures found inside.');
    } catch (e) {
      item.error = e.message || String(e);
    }
    return item;
  }

  function zipRef(entry) {
    return {
      probe: async () => probeBlob(await entry.blob()),
      load: async () => decodeImage(await entry.blob(B.mimeOf(entry.name))),
    };
  }

  async function fromZip(item, file) {
    item.kind = 'CBZ';
    const z = await B.openZip(file);
    const imgs = z.entries.filter((e) => !e.dir && B.IMAGE_EXT.test(e.name) && !B.isJunkPath(e.name)).sort((a, b) => B.naturalCompare(a.name, b.name));
    let lastDir = null;
    const dirs = new Set(imgs.map((e) => B.dirname(e.name)));
    imgs.forEach((e, i) => {
      const dir = B.dirname(e.name);
      if (dirs.size > 1 && dir !== lastDir) {
        item.chapters.push({ title: dir.replace(/\/$/, '').split('/').pop() || item.meta.title, start: i });
        lastDir = dir;
      }
      item.pages.push(zipRef(e));
    });
    const info = z.entries.find((e) => /(^|\/)comicinfo\.xml$/i.test(e.name));
    if (info) {
      try {
        const doc = new DOMParser().parseFromString(await info.text(), 'application/xml');
        const get = (t) => { const n = doc.getElementsByTagName(t)[0]; return n ? n.textContent.trim() : ''; };
        const series = get('Series');
        const num = get('Number') || get('Volume');
        const title = get('Title');
        item.meta.title = series ? series + (num ? ' ' + num : '') + (title && title !== series ? ' — ' + title : '') : title || item.meta.title;
        item.meta.author = get('Writer') || get('Penciller') || '';
        const manga = get('Manga');
        if (manga) item.meta.rtl = /righttoleft/i.test(manga);
      } catch (_) { /* ComicInfo is optional */ }
    }
  }

  async function fromPdf(item, file) {
    item.kind = 'PDF';
    const doc = await openPdf(file);
    item.doc = doc;
    try {
      const md = await doc.getMetadata();
      if (md && md.info) {
        if (md.info.Title && md.info.Title.trim().length > 1) item.meta.title = md.info.Title.trim();
        if (md.info.Author) item.meta.author = md.info.Author.trim();
      }
    } catch (_) { /* no metadata */ }
    for (let i = 1; i <= doc.numPages; i++) {
      item.pages.push({
        probe: async () => {
          const p = await doc.getPage(i);
          const v = p.getViewport({ scale: 1 });
          return { w: v.width, h: v.height };
        },
        load: async (hint) => {
          const p = await doc.getPage(i);
          const v1 = p.getViewport({ scale: 1 });
          let s = hint ? Math.max(hint.w / v1.width, hint.h / v1.height) : 2;
          s = Math.min(6, Math.max(0.25, s));
          while (v1.width * v1.height * s * s > 24e6) s *= 0.9;
          const v = p.getViewport({ scale: s });
          const c = canvas(v.width, v.height);
          const cx = c.getContext('2d');
          cx.fillStyle = '#fff';
          cx.fillRect(0, 0, c.width, c.height);
          await p.render({ canvasContext: cx, viewport: v }).promise;
          return { src: c, w: c.width, h: c.height, close: () => { p.cleanup(); c.width = c.height = 1; } };
        },
      });
    }
    try {
      const outline = await doc.getOutline();
      if (outline && outline.length > 1) {
        for (const o of outline) {
          let dest = o.dest;
          if (typeof dest === 'string') dest = await doc.getDestination(dest);
          if (!Array.isArray(dest)) continue;
          const idx = await doc.getPageIndex(dest[0]);
          item.chapters.push({ title: o.title, start: idx });
        }
        item.chapters.sort((a, b) => a.start - b.start);
      }
    } catch (_) { /* no usable outline */ }
  }

  async function readOpf(z) {
    const container = z.get('META-INF/container.xml');
    if (!container) throw new Error('This EPUB has no META-INF/container.xml.');
    const cdoc = new DOMParser().parseFromString(await container.text(), 'application/xml');
    const rootfile = cdoc.getElementsByTagName('rootfile')[0];
    const opfPath = rootfile && rootfile.getAttribute('full-path');
    const opfEntry = opfPath && z.get(opfPath);
    if (!opfEntry) throw new Error('This EPUB’s package file is missing.');
    const opf = new DOMParser().parseFromString(await opfEntry.text(), 'application/xml');
    const base = B.dirname(opfPath);
    const manifest = new Map();
    for (const it of opf.getElementsByTagName('item')) manifest.set(it.getAttribute('id'), { href: B.resolvePath(base, it.getAttribute('href') || ''), type: it.getAttribute('media-type') || '' });
    const spineEl = opf.getElementsByTagName('spine')[0];
    const spine = [...opf.getElementsByTagName('itemref')].map((r) => manifest.get(r.getAttribute('idref'))).filter(Boolean);
    const dc = (tag) => { const n = opf.getElementsByTagNameNS('http://purl.org/dc/elements/1.1/', tag)[0]; return n ? n.textContent.trim() : ''; };
    return { spine, title: dc('title'), author: dc('creator'), rtl: spineEl && spineEl.getAttribute('page-progression-direction') === 'rtl' };
  }

  async function fromEpub(item, file) {
    item.kind = 'EPUB';
    const z = await B.openZip(file);
    const opf = await readOpf(z);
    if (opf.title) item.meta.title = opf.title;
    if (opf.author) item.meta.author = opf.author;
    item.meta.rtl = opf.rtl;
    const seen = new Set();
    const re = /<(?:img|image)\b[^>]*?\s(?:src|xlink:href|href)\s*=\s*["']([^"']+)["']/gi;
    for (const doc of opf.spine) {
      if (/^image\//.test(doc.type)) { if (!seen.has(doc.href)) { seen.add(doc.href); } continue; }
      const entry = z.get(doc.href);
      if (!entry) continue;
      const text = await entry.text();
      let m;
      while ((m = re.exec(text))) {
        const p = B.resolvePath(B.dirname(doc.href), m[1]);
        if (!seen.has(p) && z.get(p)) seen.add(p);
      }
    }
    let paths = [...seen];
    if (!paths.length) paths = z.entries.filter((e) => B.IMAGE_EXT.test(e.name) && !B.isJunkPath(e.name)).map((e) => e.name).sort(B.naturalCompare);
    for (const p of paths) item.pages.push(zipRef(z.get(p)));
  }

  // Loose pictures dropped together become one item, in name order.
  function loadImagesItem(files) {
    files.sort((a, b) => B.naturalCompare(a.name, b.name));
    const common = files[0].name.replace(/[\s_-]*\d+\.[^.]+$/, '');
    return {
      id: nextId++,
      name: files.length === 1 ? files[0].name : `${files.length} pictures`,
      kind: 'Pictures',
      pages: files.map((f) => ({ probe: () => probeBlob(f), load: () => decodeImage(f) })),
      chapters: [],
      meta: { title: prettyName(common || files[0].name) || 'Pictures' },
    };
  }

  // ------------------------------------------------------------ settings

  function mangaOptions() {
    const dev = $('device').value;
    let T = PRESETS[dev];
    if (!T) T = { w: Math.round(+$('customW').value || 1404), h: Math.round(+$('customH').value || 1872) };
    T = { w: Math.min(4000, Math.max(200, T.w)), h: Math.min(4000, Math.max(200, T.h)) };
    const format = $('format').value;
    // XTCH pages are stored in columns of 8 pixels; keep the height a multiple of 8.
    if (format === 'xtch' || format === 'xtc') T.h -= T.h % 8;
    return {
      T,
      format,
      rtl: $('direction').value === 'rtl',
      spread: $('spread').value,
      trim: $('trim').checked,
      fill: $('fill').checked,
      join: $('join').checked,
      autoLevels: $('autoLevels').checked,
      contrast: +$('contrast').value,
      gamma: +$('gamma').value / 100,
      dither: $('dither').value,
      title: $('title').value.trim(),
      author: $('author').value.trim(),
    };
  }

  const SETTING_IDS = ['device', 'customW', 'customH', 'format', 'direction', 'spread', 'trim', 'fill', 'autoLevels', 'contrast', 'gamma', 'dither', 'chapters', 'lang', 'bDevice', 'gutenberg', 'shrink', 'folder'];

  function saveSettings() {
    const s = {};
    for (const id of SETTING_IDS) { const el = $(id); if (el) s[id] = el.type === 'checkbox' ? el.checked : el.value; }
    s.mode = state.mode;
    lsSet('eminimal.convert', s);
  }

  function restoreSettings() {
    const s = lsGet('eminimal.convert');
    if (!s) return;
    for (const id of SETTING_IDS) {
      const el = $(id);
      if (!el || s[id] === undefined) continue;
      if (el.type === 'checkbox') el.checked = !!s[id];
      else el.value = s[id];
    }
    if (s.mode === 'books') state.mode = 'books';
  }

  // ------------------------------------------------------------ page plan
  //
  // Regions are fractions of the source page, so they hold for a PDF page
  // rendered at any scale. The plan comes from the probed size alone, which
  // is how the page count (and so the XTC header) is known before rendering.

  function planRegions(size, o) {
    // A hairline strip (a scan's edge, a comic's end bar) is not a page.
    if (size.w > size.h * 8 || size.h > size.w * 8) return [];
    // A spread is ~1.4:1; a banner or a strip far wider than that is left whole.
    const wide = size.w > size.h * 1.15 && size.w < size.h * 2.4 && o.T.h > o.T.w;
    if (!wide || o.spread === 'keep') return [{ x: 0, y: 0, w: 1, h: 1, rot: 0 }];
    if (o.spread === 'rotate') return [{ x: 0, y: 0, w: 1, h: 1, rot: o.rtl ? -90 : 90 }];
    const left = { x: 0, y: 0, w: 0.5, h: 1, rot: 0 };
    const right = { x: 0.5, y: 0, w: 0.5, h: 1, rot: 0 };
    return o.rtl ? [right, left] : [left, right];
  }

  // Bounding box of the ink in a region, found on a small copy. Returns the
  // region unchanged when the page has no clear margin.
  function findContent(d, sx, sy, sw, sh) {
    const s = Math.min(1, 360 / Math.max(sw, sh));
    const w = Math.max(1, Math.round(sw * s));
    const h = Math.max(1, Math.round(sh * s));
    const c = canvas(w, h);
    const cx = ctx2d(c);
    cx.fillStyle = '#fff';
    cx.fillRect(0, 0, w, h);
    cx.drawImage(d.src, sx, sy, sw, sh, 0, 0, w, h);
    const px = cx.getImageData(0, 0, w, h).data;
    const luma = new Uint8Array(w * h);
    const hist = new Uint32Array(256);
    for (let i = 0; i < luma.length; i++) {
      const v = (px[i * 4] * 299 + px[i * 4 + 1] * 587 + px[i * 4 + 2] * 114) / 1000 | 0;
      luma[i] = v;
      hist[v]++;
    }
    let acc = 0, paper = 255;
    for (let v = 255; v >= 0; v--) { acc += hist[v]; if (acc > luma.length * 0.05) { paper = v; break; } }
    const thr = Math.min(215, paper - 30);
    const rows = new Uint32Array(h), cols = new Uint32Array(w);
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) if (luma[y * w + x] < thr) { rows[y]++; cols[x]++; }
    const rMin = Math.max(1, w * 0.01), cMin = Math.max(1, h * 0.01);
    let top = 0, bottom = h - 1, left = 0, right = w - 1;
    while (top < h && rows[top] < rMin) top++;
    while (bottom > top && rows[bottom] < rMin) bottom--;
    while (left < w && cols[left] < cMin) left++;
    while (right > left && cols[right] < cMin) right--;
    const bw = right - left + 1, bh = bottom - top + 1;
    if (top >= h || left >= w || bw * bh < w * h * 0.2) return { sx, sy, sw, sh };
    const px2 = Math.round(w * 0.012), py2 = Math.round(h * 0.012);
    left = Math.max(0, left - px2); right = Math.min(w - 1, right + px2);
    top = Math.max(0, top - py2); bottom = Math.min(h - 1, bottom + py2);
    return { sx: sx + left / s, sy: sy + top / s, sw: (right - left + 1) / s, sh: (bottom - top + 1) / s };
  }

  // Downscale in halving steps: one big drawImage step aliases screentone.
  function resample(src, sx, sy, sw, sh, dw, dh) {
    let cur = src, cx = sx, cy = sy, cw = sw, ch = sh;
    while (cw / 2 >= dw && ch / 2 >= dh) {
      const nw = Math.max(dw, Math.round(cw / 2)), nh = Math.max(dh, Math.round(ch / 2));
      const c = canvas(nw, nh);
      const g = c.getContext('2d');
      g.imageSmoothingQuality = 'high';
      g.drawImage(cur, cx, cy, cw, ch, 0, 0, nw, nh);
      cur = c; cx = 0; cy = 0; cw = nw; ch = nh;
    }
    const out = canvas(dw, dh);
    const g = out.getContext('2d');
    g.imageSmoothingQuality = 'high';
    g.fillStyle = '#fff';
    g.fillRect(0, 0, dw, dh);
    g.drawImage(cur, cx, cy, cw, ch, 0, 0, dw, dh);
    return out;
  }

  const target = canvas(1, 1);

  // Draws one region of a decoded page onto a T-sized white page and
  // returns its luma (Uint8Array, 0 = black).
  function renderRegion(d, reg, o) {
    const T = o.T;
    let r = { sx: reg.x * d.w, sy: reg.y * d.h, sw: reg.w * d.w, sh: reg.h * d.h };
    if (o.trim) r = findContent(d, r.sx, r.sy, r.sw, r.sh);
    const rot = reg.rot;
    const rw = rot ? r.sh : r.sw;
    const rh = rot ? r.sw : r.sh;
    const scale = o.fill ? Math.max(T.w / rw, T.h / rh) : Math.min(T.w / rw, T.h / rh);
    const dw = Math.max(1, Math.round(r.sw * scale));
    const dh = Math.max(1, Math.round(r.sh * scale));
    const scaled = resample(d.src, r.sx, r.sy, r.sw, r.sh, dw, dh);
    target.width = T.w;
    target.height = T.h;
    const g = ctx2d(target);
    g.setTransform(1, 0, 0, 1, 0, 0);
    g.fillStyle = '#fff';
    g.fillRect(0, 0, T.w, T.h);
    g.translate(T.w / 2, T.h / 2);
    if (rot) g.rotate((rot * Math.PI) / 180);
    g.drawImage(scaled, -dw / 2, -dh / 2);
    g.setTransform(1, 0, 0, 1, 0, 0);
    const px = g.getImageData(0, 0, T.w, T.h).data;
    const luma = new Uint8Array(T.w * T.h);
    for (let i = 0; i < luma.length; i++) luma[i] = (px[i * 4] * 299 + px[i * 4 + 1] * 587 + px[i * 4 + 2] * 114) / 1000;
    scaled.width = scaled.height = 1;
    return luma;
  }

  function levelsFor(format) { return format === 'xtch' ? 4 : format === 'xtc' ? 2 : 0; }

  // One rendered region → what the chosen format stores, plus a grey
  // picture of exactly that for the preview.
  function finishPage(luma, o) {
    const grey = B.applyTone(luma, o);
    const levels = levelsFor(o.format);
    if (!levels) return { grey };
    const lv = B.quantize(grey, o.T.w, o.T.h, levels, o.dither);
    return { lv, levels };
  }

  function drawToCanvas(c, page, o) {
    c.width = o.T.w;
    c.height = o.T.h;
    const g = ctx2d(c);
    const img = g.createImageData(o.T.w, o.T.h);
    const px = img.data;
    const n = o.T.w * o.T.h;
    if (page.lv) {
      const step = 255 / (page.levels - 1);
      for (let i = 0; i < n; i++) { const v = page.lv[i] * step; px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = v; px[i * 4 + 3] = 255; }
    } else {
      for (let i = 0; i < n; i++) { const v = page.grey[i]; px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = v; px[i * 4 + 3] = 255; }
    }
    g.putImageData(img, 0, 0);
  }

  function jpegOf(page, o) {
    const c = canvas(o.T.w, o.T.h);
    drawToCanvas(c, page, o);
    return new Promise((resolve, reject) => c.toBlob((b) => { c.width = c.height = 1; b ? resolve(b) : reject(new Error('The browser could not encode a JPEG.')); }, 'image/jpeg', 0.86));
  }

  // ------------------------------------------------------------ preview

  let previewToken = 0;
  let previewTimer = 0;

  function allMangaPages() {
    const out = [];
    for (const it of state.manga) if (!it.error) it.pages.forEach((ref, i) => out.push({ item: it, ref, index: i }));
    return out;
  }

  function schedulePreview() {
    clearTimeout(previewTimer);
    previewTimer = setTimeout(renderPreview, 150);
  }

  async function renderPreview() {
    if (state.mode !== 'manga') return renderBookPreview();
    const pages = allMangaPages();
    const card = $('mangaPreviewCard');
    card.hidden = !pages.length;
    if (!pages.length) return;
    state.preview = Math.min(Math.max(0, state.preview), pages.length - 1);
    $('pageCount').textContent = `Page ${state.preview + 1} of ${pages.length}`;
    $('prevPage').disabled = state.preview === 0;
    $('nextPage').disabled = state.preview >= pages.length - 1;
    const token = ++previewToken;
    const o = mangaOptions();
    const { ref, item } = pages[state.preview];
    const stage = $('previewStage');
    $('previewInfo').innerHTML = '<span class="spinner"></span> Drawing…';
    let d;
    try {
      const size = await ref.probe();
      if (token !== previewToken) return;
      d = await ref.load(o.T);
      if (token !== previewToken) return d.close();
      const canvases = [];
      if ($('showOriginal').checked) {
        const c = canvas(d.w, d.h);
        c.getContext('2d').drawImage(d.src, 0, 0);
        canvases.push(c);
      } else {
        const regs = planRegions(size, o);
        for (const reg of regs.length ? regs : [{ x: 0, y: 0, w: 1, h: 1, rot: 0 }]) {
          const page = finishPage(renderRegion(d, reg, o), o);
          const c = canvas(1, 1);
          drawToCanvas(c, page, o);
          canvases.push(c);
        }
      }
      if (token !== previewToken) return d.close();
      stage.replaceChildren(...canvases);
      stage.classList.toggle('split', canvases.length > 1);
      const src = `${Math.round(size.w)} × ${Math.round(size.h)}`;
      const outText = $('showOriginal').checked ? 'as it is in the file' : `→ ${canvases.length === 2 ? 'two pages' : 'one page'} of ${o.T.w} × ${o.T.h}`;
      $('previewInfo').textContent = `${item.name} · page ${pages[state.preview].index + 1} · ${src} ${outText}`;
    } catch (e) {
      if (token === previewToken) $('previewInfo').textContent = 'Could not draw this page: ' + (e.message || e);
    } finally {
      if (d) d.close();
    }
  }

  // ------------------------------------------------------------ books

  async function loadBookItem(file) {
    const item = { id: nextId++, name: file.name, file, kind: '', meta: { title: prettyName(file.name) } };
    try {
      if (/\.epub$/i.test(file.name)) {
        item.kind = 'EPUB';
        const z = await B.openZip(file);
        const opf = await readOpf(z);
        item.meta.title = opf.title || item.meta.title;
        item.meta.author = opf.author || '';
        let images = 0, bytes = 0, progressive = 0, big = 0;
        for (const e of z.entries) {
          if (e.dir || !/\.(jpe?g|png)$/i.test(e.name)) continue;
          images++;
          bytes += e.usize;
          const head = new Uint8Array(await (await e.blob()).slice(0, 256 * 1024).arrayBuffer());
          if (/\.jpe?g$/i.test(e.name) && B.jpegIsProgressive(head)) progressive++;
          const s = B.imageSize(head);
          if (s && (s.w > 1404 || s.h > 1872)) big++;
        }
        item.epub = { images, bytes, progressive, big };
      } else {
        const text = B.decodeText(new Uint8Array(await file.arrayBuffer()));
        item.kind = /\.(md|markdown)$/i.test(file.name) ? 'Markdown' : /\.x?html?$/i.test(file.name) ? 'HTML' : 'Text';
        item.text = text;
        const g = B.stripGutenberg(text);
        if (g.stripped) {
          const t = /^Title:\s*(.+)$/im.exec(text);
          const a = /^Author:\s*(.+)$/im.exec(text);
          if (t) item.meta.title = t[1].trim();
          if (a) item.meta.author = a[1].trim();
        }
        if (item.kind === 'HTML') {
          const doc = new DOMParser().parseFromString(text, 'text/html');
          const t = doc.querySelector('title');
          if (t && t.textContent.trim()) item.meta.title = t.textContent.trim();
        }
      }
    } catch (e) {
      item.error = e.message || String(e);
    }
    return item;
  }

  function htmlChapters(text, detect) {
    const doc = new DOMParser().parseFromString(text, 'text/html');
    doc.querySelectorAll('script,style,link,meta,iframe,object,embed,form,input,button,select,textarea,noscript,img,svg,video,audio,picture,source').forEach((n) => n.remove());
    for (const el of doc.body.querySelectorAll('*')) {
      for (const a of [...el.attributes]) if (a.name !== 'href' && a.name !== 'id' && a.name !== 'lang') el.removeAttribute(a.name);
      if (el.hasAttribute('href') && /^\s*javascript:/i.test(el.getAttribute('href'))) el.removeAttribute('href');
    }
    // Descend through a single wrapper (<div id="content">…) to the blocks.
    let root = doc.body;
    const looseText = (el) => [...el.childNodes].some((n) => n.nodeType === 3 && n.textContent.trim());
    while (root.children.length === 1 && /^(DIV|ARTICLE|MAIN|SECTION)$/.test(root.children[0].tagName) && !looseText(root)) root = root.children[0];
    const split = detect === 'none' ? null : root.querySelector(':scope > h1') ? 'H1' : root.querySelector(':scope > h2') ? 'H2' : null;
    const ser = new XMLSerializer();
    const chapters = [];
    let cur = { title: '', blocks: [], untitled: true };
    for (const node of root.childNodes) {
      if (node.nodeType === 3) {
        const t = node.textContent.trim();
        if (t) cur.blocks.push(`<p>${esc(t)}</p>`);
        continue;
      }
      if (node.nodeType !== 1) continue;
      if (split && node.tagName === split) {
        if (cur.blocks.length || !cur.untitled) chapters.push(cur);
        cur = { title: node.textContent.trim().replace(/\s+/g, ' '), blocks: [] };
        continue;
      }
      cur.blocks.push(ser.serializeToString(node));
    }
    if (cur.blocks.length || !cur.untitled) chapters.push(cur);
    return chapters;
  }

  function bookChaptersFor(item) {
    const detect = $('chapters').value;
    let text = item.text;
    if ($('gutenberg').checked) text = B.stripGutenberg(text).text;
    let chs;
    if (item.kind === 'Markdown') chs = B.parseMarkdown(text, { splitLevel: detect === 'none' ? 0 : detect === 'h2' ? 2 : 1 });
    else if (item.kind === 'HTML') chs = htmlChapters(text, detect);
    else chs = B.parseTxt(text, { detect: detect === 'none' ? 'none' : 'auto' });
    return chs;
  }

  function bookPlan() {
    const texts = state.books.filter((b) => !b.error && b.kind !== 'EPUB');
    const chapters = [];
    for (const it of texts) {
      const chs = bookChaptersFor(it);
      const fileTitle = texts.length > 1 ? it.meta.title : '';
      chs.forEach((c) => {
        if (c.untitled) { c.title = fileTitle || $('bTitle').value.trim() || it.meta.title; c.untitled = texts.length === 1 && chs.length === 1 ? false : !fileTitle && chs.length > 1; }
        chapters.push(c);
      });
    }
    return chapters;
  }

  const words = (blocks) => blocks.join(' ').replace(/<[^>]+>/g, ' ').split(/\s+/).filter(Boolean).length;

  function drawCover(c, title, author) {
    const W = c.width, H = c.height;
    const g = c.getContext('2d');
    g.fillStyle = '#fff';
    g.fillRect(0, 0, W, H);
    g.fillStyle = '#000';
    const m = W * 0.1;
    g.fillRect(m, H * 0.14, W - 2 * m, Math.max(2, W * 0.006));
    g.fillRect(m, H * 0.86, W - 2 * m, Math.max(1, W * 0.003));
    const wrap = (text, font, maxW) => {
      g.font = font;
      const out = [];
      let line = '';
      for (const word of String(text).split(/\s+/)) {
        const t = line ? line + ' ' + word : word;
        if (g.measureText(t).width > maxW && line) { out.push(line); line = word; } else line = t;
      }
      if (line) out.push(line);
      return out;
    };
    let size = W * 0.11, lines;
    do { lines = wrap(title || 'Untitled', `600 ${size}px ${SERIF}`, W - 2 * m); size *= 0.92; } while ((lines.length > 5 || lines.some((l) => g.measureText(l).width > W - 2 * m)) && size > W * 0.035);
    size /= 0.92;
    g.textAlign = 'center';
    g.textBaseline = 'alphabetic';
    let y = H * 0.3;
    for (const l of lines) { g.font = `600 ${size}px ${SERIF}`; g.fillText(l, W / 2, y); y += size * 1.18; }
    if (author) {
      const as = W * 0.05;
      g.font = `italic ${as}px ${SERIF}`;
      g.fillText(author, W / 2, Math.min(H * 0.8, y + as * 1.6), W - 2 * m);
    }
  }

  async function coverBlob(title, author) {
    const mode = $('coverMode').value;
    if (mode === 'none') return null;
    const T = PRESETS[$('bDevice').value];
    if (mode === 'image' && state.cover) {
      const d = await decodeImage(state.cover);
      const s = Math.min(1, T.w / d.w, T.h / d.h);
      const c = resample(d.src, 0, 0, d.w, d.h, Math.round(d.w * s), Math.round(d.h * s));
      d.close();
      greyCanvas(c);
      return { blob: await new Promise((r) => c.toBlob(r, 'image/jpeg', 0.88)), ext: 'jpg' };
    }
    const c = canvas(T.w, T.h);
    drawCover(c, title, author);
    return { blob: await new Promise((r) => c.toBlob(r, 'image/jpeg', 0.9)), ext: 'jpg' };
  }

  function greyCanvas(c) {
    const g = ctx2d(c);
    const img = g.getImageData(0, 0, c.width, c.height);
    const px = img.data;
    for (let i = 0; i < px.length; i += 4) {
      const a = px[i + 3] / 255;
      const v = ((px[i] * 299 + px[i + 1] * 587 + px[i + 2] * 114) / 1000) * a + 255 * (1 - a);
      px[i] = px[i + 1] = px[i + 2] = v;
      px[i + 3] = 255;
    }
    g.putImageData(img, 0, 0);
  }

  async function renderBookPreview() {
    const card = $('bookPreviewCard');
    const ok = state.books.filter((b) => !b.error);
    card.hidden = !ok.length;
    if (!ok.length) return;
    const epubs = ok.filter((b) => b.kind === 'EPUB');
    const texts = ok.filter((b) => b.kind !== 'EPUB');
    const cover = $('coverCanvas');
    const list = $('chapterList');
    if (texts.length) {
      const chs = bookPlan();
      const title = $('bTitle').value.trim() || texts[0].meta.title;
      const author = $('bAuthor').value.trim();
      cover.hidden = $('coverMode').value === 'none';
      if ($('coverMode').value === 'image' && state.cover) {
        const d = await decodeImage(state.cover);
        const g = cover.getContext('2d');
        g.fillStyle = '#fff';
        g.fillRect(0, 0, cover.width, cover.height);
        const s = Math.min(cover.width / d.w, cover.height / d.h);
        g.drawImage(d.src, (cover.width - d.w * s) / 2, (cover.height - d.h * s) / 2, d.w * s, d.h * s);
        d.close();
      } else drawCover(cover, title, author);
      const total = chs.reduce((n, c) => n + words(c.blocks), 0);
      $('bookSummary').textContent = `${chs.length} chapter${chs.length === 1 ? '' : 's'} · about ${total.toLocaleString()} words` + (epubs.length ? ` · ${epubs.length} EPUB${epubs.length > 1 ? 's' : ''} to tidy as well` : '');
      list.innerHTML = chs.slice(0, 200).map((c) => `<div class="row"><span class="row-main">${esc(c.title || '(untitled)')}</span><span class="row-meta">${words(c.blocks).toLocaleString()} words</span></div>`).join('');
    } else {
      cover.hidden = true;
      $('bookSummary').textContent = 'These EPUBs are copied as they are, with their pictures made small, grey and baseline so the reader opens them quickly.';
      list.innerHTML = epubs.map((b) => `<div class="row"><span class="row-main">${esc(b.meta.title)}</span><span class="row-meta">${b.epub.images} pictures · ${formatBytes(b.epub.bytes)}${b.epub.progressive ? ` · ${b.epub.progressive} progressive` : ''}${b.epub.big ? ` · ${b.epub.big} oversized` : ''}</span></div>`).join('');
    }
  }

  // Shrinks, greys and re-encodes pictures inside an EPUB in place (same
  // names, so no references change).
  async function tidyEpub(item, onProgress) {
    const T = PRESETS[$('bDevice').value];
    const z = await B.openZip(item.file);
    const out = [{ name: 'mimetype', data: 'application/epub+zip' }];
    const stats = { before: 0, after: 0, changed: 0 };
    const entries = z.entries.filter((e) => !e.dir && e.name !== 'mimetype');
    for (let i = 0; i < entries.length; i++) {
      if (state.cancel) throw new Error('Cancelled.');
      const e = entries[i];
      let data = await e.blob();
      if ($('shrink').checked && /\.(jpe?g|png)$/i.test(e.name)) {
        const isJpg = /\.jpe?g$/i.test(e.name);
        const head = new Uint8Array(await data.slice(0, 256 * 1024).arrayBuffer());
        const progressive = isJpg && B.jpegIsProgressive(head);
        const size = B.imageSize(head);
        const s = size ? Math.min(1, T.w / size.w, T.h / size.h) : 1;
        if (progressive || s < 1 || isJpg) {
          try {
            const d = await decodeImage(data);
            const c = resample(d.src, 0, 0, d.w, d.h, Math.max(1, Math.round(d.w * s)), Math.max(1, Math.round(d.h * s)));
            d.close();
            greyCanvas(c);
            const nb = await new Promise((r) => c.toBlob(r, isJpg ? 'image/jpeg' : 'image/png', 0.85));
            c.width = c.height = 1;
            if (nb && (progressive || s < 1 || nb.size < data.size)) {
              stats.before += data.size;
              stats.after += nb.size;
              stats.changed++;
              data = nb;
            }
          } catch (_) { /* keep the original picture */ }
        }
      }
      out.push({ name: e.name, data });
      onProgress(i + 1, entries.length);
      await tick();
    }
    return { blob: await B.writeZip(out), stats };
  }

  // ------------------------------------------------------------ sinks
  //
  // A sink takes the output file in order. MemorySink keeps the parts and
  // hands back a Blob; ReaderSink streams them to the card.

  class MemorySink {
    async begin(name) { this.name = name; this.parts = []; }
    async write(bytes) { this.parts.push(new Blob([bytes])); }
    async end() { return new Blob(this.parts); }
    abort() { this.parts = []; }
  }

  function wsUrl() {
    const port = +location.port || 80;
    return `ws://${location.hostname}:${port + 1}/`;
  }

  async function ensureFolder(path) {
    const segs = path.split('/').filter(Boolean);
    let parent = '/';
    for (const s of segs) {
      const fd = new FormData();
      fd.append('name', s);
      fd.append('path', parent);
      try { await fetch('/mkdir', { method: 'POST', body: fd }); } catch (_) { /* exists, or the upload will say */ }
      parent = parent.replace(/\/?$/, '/') + s;
    }
    return '/' + segs.join('/');
  }

  class ReaderSink {
    constructor(folder) { this.folder = folder; }

    async begin(name, total) {
      this.total = total;
      const stem = name.replace(/\.[^.]+$/, '');
      const ext = name.slice(stem.length);
      for (let n = 1; n <= 20; n++) {
        this.name = n === 1 ? name : `${stem} (${n})${ext}`;
        const res = await this.open();
        if (res === 'READY') return;
        if (!/already exists/i.test(res)) throw new Error('The reader refused the file: ' + res);
      }
      throw new Error('A file with this name is already on the card.');
    }

    open() {
      return new Promise((resolve, reject) => {
        const ws = new WebSocket(wsUrl());
        ws.binaryType = 'arraybuffer';
        this.ws = ws;
        this.done = null;
        this.closed = false;
        this.error = null;
        this.finished = false;
        let settled = false;
        ws.onopen = () => ws.send(`START:${this.name}:${this.total}:${this.folder}`);
        ws.onmessage = (ev) => {
          const msg = String(ev.data);
          if (!settled) {
            settled = true;
            if (msg === 'READY') resolve('READY');
            else { ws.close(); resolve(msg.replace(/^ERROR:/, '')); }
            return;
          }
          // DONE can arrive before end() is called: the last chunk is often
          // written by the time write() returns.
          if (msg === 'DONE') { this.finished = true; if (this.done) this.done.resolve(); }
          else if (msg.startsWith('ERROR:')) { this.error = msg.slice(6); if (this.done) this.done.reject(new Error(this.error)); }
        };
        ws.onerror = () => { if (!settled) { settled = true; reject(new Error('Could not reach the reader. Is this phone still on its Wi-Fi?')); } };
        ws.onclose = () => { this.closed = true; if (this.done && !this.finished) this.done.reject(new Error(this.error || 'The connection to the reader closed.')); };
      });
    }

    async write(bytes) {
      const ws = this.ws;
      const CH = 4096;
      for (let off = 0; off < bytes.length; off += CH) {
        while (ws.bufferedAmount > CH * 4 && ws.readyState === WebSocket.OPEN) await new Promise((r) => setTimeout(r, 4));
        if (ws.readyState !== WebSocket.OPEN || this.error) throw Object.assign(new Error(this.error || 'The connection to the reader closed.'), { sinkError: true });
        ws.send(bytes.subarray(off, Math.min(bytes.length, off + CH)));
      }
    }

    async writeBlob(blob, onProgress) {
      const CH = 256 * 1024;
      for (let off = 0; off < blob.size; off += CH) {
        await this.write(new Uint8Array(await blob.slice(off, Math.min(blob.size, off + CH)).arrayBuffer()));
        if (onProgress) onProgress(Math.min(blob.size, off + CH), blob.size);
      }
    }

    end() {
      return new Promise((resolve, reject) => {
        const finish = () => { this.ws.close(); resolve(); };
        if (this.finished) return finish();
        if (this.closed) return reject(new Error(this.error || 'The connection to the reader closed.'));
        this.done = { resolve: finish, reject };
      });
    }

    abort() { try { this.ws.close(); } catch (_) { /* already closed */ } }
  }

  // ------------------------------------------------------------ jobs

  function mangaJobs(o) {
    const items = state.manga.filter((it) => !it.error);
    const one = (it) => ({
      title: (items.length === 1 && o.title) || it.meta.title,
      author: (items.length === 1 && o.author) || it.meta.author || '',
      rtl: o.rtl,
      parts: [{ item: it, chapterTitle: null }],
    });
    if (!o.join || items.length < 2) return items.map(one);
    return [{
      title: o.title || items[0].meta.title,
      author: o.author || items[0].meta.author || '',
      rtl: o.rtl,
      parts: items.map((it) => ({ item: it, chapterTitle: it.meta.title })),
    }];
  }

  // Probe every page once: the plan fixes the page count and chapter starts.
  async function planJob(job, o, progress) {
    const plans = [];
    const chapters = [];
    let count = 0;
    let total = 0;
    for (const p of job.parts) total += p.item.pages.length;
    let seen = 0;
    for (const part of job.parts) {
      const it = part.item;
      const inner = it.chapters.slice();
      if (part.chapterTitle && (!inner.length || inner[0].start > 0)) inner.unshift({ title: part.chapterTitle, start: 0 });
      for (let i = 0; i < it.pages.length; i++) {
        if (state.cancel) throw new Error('Cancelled.');
        const ch = inner.filter((c) => c.start === i);
        for (const c of ch) chapters.push({ title: part.chapterTitle && c.title !== part.chapterTitle ? `${part.chapterTitle} — ${c.title}` : c.title, start: count });
        let size;
        try { size = await it.pages[i].probe(); } catch (_) { size = { w: 1, h: 1.4 }; }
        const regions = planRegions(size, o);
        plans.push({ ref: it.pages[i], regions });
        count += regions.length;
        seen++;
        if (seen % 8 === 0) { progress(`Measuring pages · ${seen} of ${total}`, seen / total * 0.05); await tick(); }
      }
    }
    chapters.forEach((c, i) => { c.end = (i + 1 < chapters.length ? chapters[i + 1].start : count) - 1; });
    return { plans, count, chapters: chapters.filter((c) => c.end >= c.start) };
  }

  async function renderPlans(plan, o, onPage, progress) {
    const t0 = performance.now();
    let done = 0;
    for (const p of plan.plans) {
      if (state.cancel) throw new Error('Cancelled.');
      let d = null;
      let emitted = 0;
      try {
        d = await p.ref.load(o.T);
        for (const reg of p.regions) {
          const page = finishPage(renderRegion(d, reg, o), o);
          emitted++;
          done++;
          await onPage(page);
        }
      } catch (e) {
        if (state.cancel) throw e;
        if (e && e.sinkError) throw e;
        // A page that will not decode becomes a blank page, so the count
        // the header promised still holds.
        for (; emitted < p.regions.length; emitted++, done++) await onPage(finishPage(new Uint8Array(o.T.w * o.T.h).fill(255), o));
      } finally {
        if (d) d.close();
      }
      const el = (performance.now() - t0) / 1000;
      const left = (el / done) * (plan.count - done);
      progress(`Page ${done} of ${plan.count} · about ${formatTime(left)} left`, 0.05 + 0.95 * (done / plan.count));
      await tick();
    }
  }

  async function runMangaJob(job, o, sinkFactory, progress) {
    const plan = await planJob(job, o, progress);
    if (!plan.count) throw new Error('No pages to convert.');
    const ext = o.format === 'xtch' ? 'xtch' : o.format === 'xtc' ? 'xtc' : o.format;
    const name = safeFileName(job.title, ext);

    if (o.format === 'xtch' || o.format === 'xtc') {
      const bitDepth = o.format === 'xtch' ? 2 : 1;
      const head = B.buildXtcHeader({ count: plan.count, width: o.T.w, height: o.T.h, bitDepth, title: job.title, author: job.author, chapters: plan.chapters, rtl: job.rtl });
      const size = head.length + plan.count * B.xtcPageSize(o.T.w, o.T.h, bitDepth);
      const sink = sinkFactory();
      await sink.begin(name, size);
      try {
        await sink.write(head);
        await renderPlans(plan, o, (page) => sink.write(bitDepth === 2 ? B.encodeXth(page.lv, o.T.w, o.T.h) : B.encodeXtg(page.lv, o.T.w, o.T.h)), progress);
        progress('Finishing on the card…', 1);
        const blob = await sink.end();
        return { name: sink.name, size, blob, pages: plan.count };
      } catch (e) {
        sink.abort();
        throw e;
      }
    }

    // EPUB and CBZ hold JPEGs of unknown size: build the file, then deliver.
    const pages = [];
    await renderPlans(plan, o, async (page) => { pages.push({ blob: await jpegOf(page, o), ext: 'jpg' }); }, progress);
    let blob;
    if (o.format === 'epub') {
      blob = await B.writeZip(B.buildMangaEpub({ title: job.title, author: job.author, lang: 'en', rtl: job.rtl, width: o.T.w, height: o.T.h, pages, chapters: plan.chapters }));
    } else {
      const info = `<?xml version="1.0" encoding="utf-8"?>\n<ComicInfo xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns:xsd="http://www.w3.org/2001/XMLSchema">\n  <Title>${esc(job.title)}</Title>\n  <Writer>${esc(job.author)}</Writer>\n  <PageCount>${pages.length}</PageCount>\n  <Manga>${job.rtl ? 'YesAndRightToLeft' : 'No'}</Manga>\n</ComicInfo>\n`;
      blob = await B.writeZip([...pages.map((p, i) => ({ name: `${String(i + 1).padStart(4, '0')}.jpg`, data: p.blob })), { name: 'ComicInfo.xml', data: info }]);
    }
    return deliverBlob(blob, name, sinkFactory, progress, plan.count);
  }

  async function deliverBlob(blob, name, sinkFactory, progress, pages) {
    const sink = sinkFactory();
    await sink.begin(name, blob.size);
    try {
      if (sink.writeBlob) await sink.writeBlob(blob, (a, b) => progress(`Sending · ${formatBytes(a)} of ${formatBytes(b)}`, a / b));
      else await sink.write(new Uint8Array(await blob.arrayBuffer()));
      await sink.end();
    } catch (e) {
      sink.abort();
      throw e;
    }
    return { name: sink.name, size: blob.size, blob: sink instanceof MemorySink ? blob : null, pages };
  }

  async function runBooks(sinkFactory, progress) {
    const out = [];
    const ok = state.books.filter((b) => !b.error);
    const texts = ok.filter((b) => b.kind !== 'EPUB');
    if (texts.length) {
      progress('Laying out chapters…', 0.1);
      const chapters = bookPlan();
      const title = $('bTitle').value.trim() || texts[0].meta.title;
      const author = $('bAuthor').value.trim() || texts[0].meta.author || '';
      const cover = await coverBlob(title, author);
      const blob = await B.writeZip(B.buildTextEpub({ title, author, lang: $('lang').value, chapters, cover, listParts: chapters.length === 1 }));
      out.push(await deliverBlob(blob, safeFileName(title, 'epub'), sinkFactory, progress));
    }
    for (const it of ok.filter((b) => b.kind === 'EPUB')) {
      const r = await tidyEpub(it, (a, b) => progress(`${it.meta.title} · picture ${a} of ${b}`, a / b));
      const res = await deliverBlob(r.blob, safeFileName(it.meta.title, 'epub'), sinkFactory, progress);
      res.note = r.stats.changed ? `${r.stats.changed} pictures, ${formatBytes(r.stats.before)} → ${formatBytes(r.stats.after)}` : 'no pictures needed changing';
      out.push(res);
    }
    return out;
  }

  // ------------------------------------------------------------ saving

  const DL_OK = new Set('gif png jpg jpeg webp mp4 webm txt json md docx pptx epub csv ttf html svg pdf xlsx zip'.split(' '));
  const claudeDownloads = window.claude && typeof window.claude.use === 'function' ? window.claude.use('downloads').catch(() => null) : null;

  async function saveToDevice(r) {
    let { name, blob } = r;
    if (claudeDownloads) {
      const dl = await claudeDownloads;
      if (dl) {
        const ext = name.split('.').pop().toLowerCase();
        if (ext === 'cbz') name = name.replace(/\.cbz$/i, '.zip');
        else if (!DL_OK.has(ext)) { blob = await B.writeZip([{ name, data: blob }]); name += '.zip'; }
        try {
          await dl.save({ filename: name, data: blob });
          toast('Saved ' + name, 'ok');
        } catch (e) {
          if (e && e.code !== 'declined') toast('Could not save: ' + (e.message || e.code), 'danger');
        }
        return;
      }
    }
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = name;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 60000);
  }

  // ------------------------------------------------------------ run

  let wakeLock = null;

  async function run(toReader) {
    if (state.busy) return;
    state.busy = true;
    state.cancel = false;
    saveSettings();
    setBusy(true);
    const results = $('results');
    const bar = $('runBar');
    const status = $('runStatus');
    $('runProgress').hidden = false;
    const progress = (text, f) => { status.textContent = text; bar.style.width = Math.round(Math.min(1, Math.max(0, f)) * 100) + '%'; };
    try { wakeLock = navigator.wakeLock ? await navigator.wakeLock.request('screen') : null; } catch (_) { wakeLock = null; }

    let folder = '/';
    const sinkFactory = () => (toReader ? new ReaderSink(folder) : new MemorySink());
    const done = [];
    try {
      if (toReader) {
        progress('Getting the folder ready…', 0);
        folder = await ensureFolder($('folder').value.trim() || '/');
      }
      if (state.mode === 'manga') {
        const o = mangaOptions();
        const jobs = mangaJobs(o);
        for (let j = 0; j < jobs.length; j++) {
          const prefix = jobs.length > 1 ? `Book ${j + 1} of ${jobs.length} · ` : '';
          const r = await runMangaJob(jobs[j], o, sinkFactory, (t, f) => progress(prefix + t, (j + f) / jobs.length));
          done.push(r);
          addResult(r, toReader, folder);
        }
      } else {
        for (const r of await runBooks(sinkFactory, progress)) { done.push(r); addResult(r, toReader, folder); }
      }
      progress(toReader ? `Done. ${done.length === 1 ? 'The book is' : done.length + ' books are'} on the card.` : 'Done. Save the files below.', 1);
      toast(toReader ? 'Sent to the reader' : 'Converted', 'ok');
    } catch (e) {
      const msg = e && e.message ? e.message : String(e);
      progress(state.cancel ? 'Cancelled. Nothing half-finished was left on the card.' : 'Stopped: ' + msg, 0);
      if (!state.cancel) toast(msg, 'danger');
    } finally {
      state.busy = false;
      setBusy(false);
      if (wakeLock) { wakeLock.release().catch(() => {}); wakeLock = null; }
    }
    if (!results.children.length) results.hidden = true;
  }

  function addResult(r, toReader, folder) {
    const results = $('results');
    results.hidden = false;
    const row = document.createElement('div');
    row.className = 'row';
    const where = toReader ? `On the card in ${folder}` : 'Ready to save';
    const meta = [where, formatBytes(r.size), r.pages ? `${r.pages} pages` : '', r.note || ''].filter(Boolean).join(' · ');
    row.innerHTML = `<span class="row-main">${esc(r.name)}<span class="row-meta">${esc(meta)}</span></span>`;
    if (r.blob) {
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'btn btn-sm';
      btn.textContent = 'Save';
      btn.addEventListener('click', () => saveToDevice(r));
      row.appendChild(btn);
    } else {
      const chip = document.createElement('span');
      chip.className = 'chip chip-ok';
      chip.textContent = 'Sent';
      row.appendChild(chip);
    }
    results.prepend(row);
  }

  function setBusy(busy) {
    $('cancelBtn').hidden = !busy;
    for (const id of ['sendBtn', 'saveBtn', 'fileInput', 'modeManga', 'modeBooks', 'clearFiles']) { const el = $(id); if (el) el.disabled = busy; }
    if (!busy) refreshButtons();
  }

  // ------------------------------------------------------------ file list

  function currentItems() { return state.mode === 'manga' ? state.manga : state.books; }

  function renderFiles() {
    const items = currentItems();
    const list = $('fileList');
    list.innerHTML = '';
    items.forEach((it, i) => {
      const row = document.createElement('div');
      row.className = 'row file-row' + (it.error ? ' is-error' : '');
      let meta;
      if (it.loading) meta = 'Reading…';
      else if (it.error) meta = it.error;
      else if (state.mode === 'manga') meta = `${it.kind} · ${it.pages.length} page${it.pages.length === 1 ? '' : 's'}${it.chapters.length > 1 ? ` · ${it.chapters.length} chapters` : ''}`;
      else meta = `${it.kind}${it.file ? ' · ' + formatBytes(it.file.size) : ''}`;
      row.innerHTML = `<span class="row-main">${esc(it.name)}<span class="row-meta">${esc(meta)}</span></span><span class="row-actions"></span>`;
      const actions = row.querySelector('.row-actions');
      const mk = (label, aria, fn, disabled) => {
        const b = document.createElement('button');
        b.type = 'button';
        b.className = 'btn btn-sm btn-ghost';
        b.textContent = label;
        b.setAttribute('aria-label', aria);
        b.disabled = !!disabled || state.busy;
        b.addEventListener('click', fn);
        actions.appendChild(b);
      };
      if (items.length > 1) {
        mk('Up', `Move ${it.name} up`, () => { items.splice(i - 1, 0, items.splice(i, 1)[0]); changed(); }, i === 0);
        mk('Down', `Move ${it.name} down`, () => { items.splice(i + 1, 0, items.splice(i, 1)[0]); changed(); }, i === items.length - 1);
      }
      mk('Remove', `Remove ${it.name}`, () => { items.splice(i, 1); changed(); });
      list.appendChild(row);
    });
    $('filesEmpty').hidden = items.length > 0;
    $('clearFiles').hidden = items.length === 0;
    $('joinRow').hidden = !(state.mode === 'manga' && state.manga.filter((m) => !m.error).length > 1);
  }

  function fillMeta() {
    if (state.mode === 'manga') {
      const ok = state.manga.filter((m) => !m.error && !m.loading);
      const single = ok.length === 1 || $('join').checked;
      $('metaFields').hidden = !single;
      if (ok.length && !$('title').dataset.touched) $('title').value = ok[0].meta.title || '';
      if (ok.length && !$('author').dataset.touched) $('author').value = ok[0].meta.author || '';
      if (ok.length === 1 && ok[0].meta.rtl !== undefined && !directionTouched) $('direction').value = ok[0].meta.rtl ? 'rtl' : 'ltr';
    } else {
      const ok = state.books.filter((m) => !m.error && !m.loading && m.kind !== 'EPUB');
      if (ok.length && !$('bTitle').dataset.touched) $('bTitle').value = ok[0].meta.title || '';
      if (ok.length && !$('bAuthor').dataset.touched) $('bAuthor').value = ok[0].meta.author || '';
    }
  }

  function estimate() {
    const el = $('estimate');
    if (state.mode !== 'manga') {
      const n = state.books.filter((b) => !b.error).length;
      el.textContent = n ? '' : '';
      return;
    }
    const o = mangaOptions();
    const pages = state.manga.filter((m) => !m.error).reduce((n, m) => n + m.pages.length, 0);
    if (!pages) { el.textContent = ''; return; }
    const levels = levelsFor(o.format);
    const per = levels ? B.xtcPageSize(o.T.w, o.T.h, levels === 4 ? 2 : 1) : o.T.w * o.T.h * 0.12;
    el.textContent = `${pages} source page${pages === 1 ? '' : 's'} · about ${formatBytes(per * pages)}${levels ? '' : ' (depends on the pictures)'}${o.spread === 'split' ? ', more if spreads are split' : ''}.`;
  }

  function refreshButtons() {
    const any = currentItems().some((it) => !it.error && !it.loading);
    $('sendBtn').disabled = !any || state.busy;
    $('saveBtn').disabled = !any || state.busy;
  }

  function changed() {
    renderFiles();
    fillMeta();
    estimate();
    refreshButtons();
    schedulePreview();
  }

  async function addFiles(fileList) {
    const files = [...fileList];
    if (!files.length) return;
    if (state.mode === 'manga') {
      const images = files.filter((f) => /\.(jpe?g|png|webp|gif|bmp|avif)$/i.test(f.name));
      const others = files.filter((f) => !images.includes(f));
      const placeholders = [];
      for (const f of others) {
        const ph = { id: nextId++, name: f.name, loading: true, pages: [], chapters: [], meta: {} };
        if (!MANGA_EXT.test(f.name) && !ARCHIVE_UNSUPPORTED.test(f.name)) { ph.loading = false; ph.error = 'Not a file type this page reads. Use CBZ, ZIP, PDF, EPUB or pictures.'; }
        state.manga.push(ph);
        placeholders.push([ph, f]);
      }
      if (images.length) state.manga.push(loadImagesItem(images));
      changed();
      for (const [ph, f] of placeholders) {
        if (ph.error) continue;
        const it = await loadMangaItem(f);
        const i = state.manga.indexOf(ph);
        if (i >= 0) state.manga[i] = it;
        changed();
      }
    } else {
      for (const f of files) {
        const ph = { id: nextId++, name: f.name, loading: true, meta: {} };
        if (!BOOK_EXT.test(f.name)) { ph.loading = false; ph.error = 'Not a file type this page reads. Use TXT, Markdown, HTML or EPUB.'; }
        state.books.push(ph);
        changed();
        if (ph.error) continue;
        const it = await loadBookItem(f);
        const i = state.books.indexOf(ph);
        if (i >= 0) state.books[i] = it;
        changed();
      }
    }
  }

  // ------------------------------------------------------------ mode

  function setMode(mode) {
    state.mode = mode;
    const manga = mode === 'manga';
    $('modeManga').setAttribute('aria-pressed', String(manga));
    $('modeBooks').setAttribute('aria-pressed', String(!manga));
    $('mangaSettings').hidden = !manga;
    $('bookSettings').hidden = manga;
    $('mangaPreviewCard').hidden = true;
    $('bookPreviewCard').hidden = true;
    $('dropHint').textContent = manga ? 'CBZ, ZIP, PDF, EPUB or pictures (JPG, PNG, WebP)' : 'TXT, Markdown or HTML to make an EPUB · EPUBs to tidy their pictures';
    $('fileInput').accept = manga ? '.cbz,.zip,.pdf,.epub,image/*' : '.txt,.text,.md,.markdown,.html,.htm,.xhtml,.epub';
    $('filesEmpty').textContent = manga ? 'Nothing added yet. Pick a volume to see how its pages will look.' : 'Nothing added yet. Pick a text file or an EPUB.';
    const folder = $('folder');
    if (folder.value === '/Manga' || folder.value === '/Books' || !folder.value) folder.value = manga ? '/Manga' : '/Books';
    $('results').innerHTML = '';
    $('runProgress').hidden = true;
    changed();
  }

  const FORMAT_HINTS = {
    xtch: 'Best for the e-Minimal: pages are drawn here and stored ready to show, so a turn takes about 2 s.',
    xtc: 'Half the size of XTCH and a quicker turn. Fine for line art; grey areas turn into dot patterns.',
    epub: 'For Kobo, Kindle and phone apps. The e-Minimal opens it, but decodes every picture as you turn (about 6 s a page).',
    cbz: 'For comic apps on phones and tablets. The e-Minimal does not list CBZ files — use XTCH for it.',
  };

  // ------------------------------------------------------------ wiring

  let directionTouched = false;

  function init() {
    restoreSettings();
    $('customSize').hidden = $('device').value !== 'custom';
    $('formatHint').textContent = FORMAT_HINTS[$('format').value];
    $('contrastOut').textContent = $('contrast').value;
    $('gammaOut').textContent = (+$('gamma').value / 100).toFixed(2);

    $('modeManga').addEventListener('click', () => setMode('manga'));
    $('modeBooks').addEventListener('click', () => setMode('books'));
    $('fileInput').addEventListener('change', (e) => { addFiles(e.target.files); e.target.value = ''; });
    $('clearFiles').addEventListener('click', () => { currentItems().length = 0; changed(); });

    const dz = $('dropzone');
    ['dragenter', 'dragover'].forEach((t) => dz.addEventListener(t, (e) => { e.preventDefault(); dz.classList.add('dragover'); }));
    ['dragleave', 'drop'].forEach((t) => dz.addEventListener(t, (e) => { e.preventDefault(); dz.classList.remove('dragover'); }));
    dz.addEventListener('drop', (e) => { if (!state.busy && e.dataTransfer) addFiles(e.dataTransfer.files); });

    $('prevPage').addEventListener('click', () => { state.preview--; renderPreview(); });
    $('nextPage').addEventListener('click', () => { state.preview++; renderPreview(); });
    $('showOriginal').addEventListener('change', renderPreview);
    $('previewStage').addEventListener('click', () => $('previewStage').classList.toggle('actual'));

    $('device').addEventListener('change', () => { $('customSize').hidden = $('device').value !== 'custom'; });
    $('format').addEventListener('change', () => { $('formatHint').textContent = FORMAT_HINTS[$('format').value]; });
    $('direction').addEventListener('change', () => { directionTouched = true; });
    $('contrast').addEventListener('input', () => { $('contrastOut').textContent = $('contrast').value; });
    $('gamma').addEventListener('input', () => { $('gammaOut').textContent = (+$('gamma').value / 100).toFixed(2); });
    for (const id of ['title', 'author', 'bTitle', 'bAuthor']) $(id).addEventListener('input', () => { $(id).dataset.touched = '1'; });
    $('join').addEventListener('change', fillMeta);
    $('coverMode').addEventListener('change', () => { $('coverFile').hidden = $('coverMode').value !== 'image'; schedulePreview(); });
    $('coverFile').addEventListener('change', (e) => { state.cover = e.target.files[0] || null; schedulePreview(); });

    for (const id of ['device', 'customW', 'customH', 'format', 'direction', 'spread', 'trim', 'fill', 'autoLevels', 'contrast', 'gamma', 'dither', 'chapters', 'gutenberg', 'bTitle', 'bAuthor', 'bDevice', 'lang', 'join']) {
      $(id).addEventListener('change', () => { saveSettings(); estimate(); schedulePreview(); });
    }
    for (const id of ['bTitle', 'bAuthor']) $(id).addEventListener('input', schedulePreview);

    $('sendBtn').addEventListener('click', () => run(true));
    $('saveBtn').addEventListener('click', () => run(false));
    $('cancelBtn').addEventListener('click', () => { state.cancel = true; $('runStatus').textContent = 'Cancelling…'; });
    if (HOST !== 'device') { $('saveBtn').classList.add('btn-primary'); $('saveBtn').textContent = 'Convert'; }
    else $('saveBtn').textContent = 'Save to this phone';

    setMode(state.mode);
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
  else init();
})();
