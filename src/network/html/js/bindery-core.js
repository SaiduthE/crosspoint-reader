/*
 * eMinimal Bindery — core.
 *
 * Everything here is pure: no DOM, no canvas. It runs in the page and under
 * Node (scripts/bindery/run-core-test.js), so the file formats can be checked without a
 * browser. The page's own code (app.js) does the decoding and drawing and
 * hands this file grey arrays and blobs.
 *
 *   zip     writeZip (stored entries, CRC computed from the blobs in chunks)
 *           openZip  (reads the central directory, inflates entries lazily
 *                     with DecompressionStream, so a 500 MB CBZ never sits
 *                     in memory whole)
 *   tone    toneLut, applyTone, quantize (Floyd–Steinberg, Atkinson,
 *           ordered 8x8 Bayer, threshold)
 *   xtc     encodeXtg (1-bit), encodeXth (2-bit), buildXtc — byte-for-byte
 *           what lib/Xtc/Xtc/XtcParser.cpp in the reader expects
 *   epub    buildMangaEpub (fixed layout), buildTextEpub (reflowable)
 *   text    decodeText, stripGutenberg, parseTxt, parseMarkdown
 */
(function (G) {
  'use strict';

  const TE = new TextEncoder();

  // ---------------------------------------------------------------- bytes

  function toBlob(data, type) {
    if (data instanceof Blob) return data;
    if (typeof data === 'string') return new Blob([TE.encode(data)], { type: type || '' });
    return new Blob([data], { type: type || '' });
  }

  // UTF-8, cut to at most `max` bytes without splitting a character.
  function utf8Cut(str, max) {
    let b = TE.encode(str || '');
    if (b.length <= max) return b;
    let n = max;
    while (n > 0 && (b[n] & 0xc0) === 0x80) n--;
    return b.subarray(0, n);
  }

  // ------------------------------------------------------------------ crc

  const CRC_TABLE = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      t[n] = c >>> 0;
    }
    return t;
  })();

  function crc32(bytes, crc) {
    let c = (crc === undefined ? 0 : crc) ^ 0xffffffff;
    for (let i = 0; i < bytes.length; i++) c = CRC_TABLE[(c ^ bytes[i]) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
  }

  const CHUNK = 8 * 1024 * 1024;

  async function crc32Blob(blob) {
    let crc = 0;
    for (let off = 0; off < blob.size; off += CHUNK) {
      const part = new Uint8Array(await blob.slice(off, Math.min(blob.size, off + CHUNK)).arrayBuffer());
      crc = crc32(part, crc);
    }
    return crc;
  }

  // ------------------------------------------------------------ zip write

  function dosDateTime(d) {
    const time = (d.getHours() << 11) | (d.getMinutes() << 5) | (d.getSeconds() >> 1);
    const date = ((d.getFullYear() - 1980) << 9) | ((d.getMonth() + 1) << 5) | d.getDate();
    return { time, date };
  }

  // entries: [{ name, data: Blob | Uint8Array | string }]. Every entry is
  // stored, not deflated: images are already compressed, and an EPUB's
  // mimetype must be stored anyway. Returns a Blob; the entry data is never
  // copied, only referenced.
  async function writeZip(entries, onProgress) {
    const { time, date } = dosDateTime(new Date());
    const parts = [];
    const central = [];
    let offset = 0;
    for (let i = 0; i < entries.length; i++) {
      const e = entries[i];
      const blob = toBlob(e.data);
      const size = blob.size;
      const crc = await crc32Blob(blob);
      const name = TE.encode(e.name);
      if (offset + 30 + name.length + size > 0xfffffffe) throw new Error('The output is over 4 GB, which a plain zip cannot hold. Convert fewer pages at a time.');

      const lh = new DataView(new ArrayBuffer(30));
      lh.setUint32(0, 0x04034b50, true);
      lh.setUint16(4, 20, true);
      lh.setUint16(6, 0x0800, true); // names are UTF-8
      lh.setUint16(8, 0, true); // stored
      lh.setUint16(10, time, true);
      lh.setUint16(12, date, true);
      lh.setUint32(14, crc, true);
      lh.setUint32(18, size, true);
      lh.setUint32(22, size, true);
      lh.setUint16(26, name.length, true);
      lh.setUint16(28, 0, true);
      parts.push(lh.buffer, name, blob);

      const ch = new DataView(new ArrayBuffer(46));
      ch.setUint32(0, 0x02014b50, true);
      ch.setUint16(4, 20, true);
      ch.setUint16(6, 20, true);
      ch.setUint16(8, 0x0800, true);
      ch.setUint16(10, 0, true);
      ch.setUint16(12, time, true);
      ch.setUint16(14, date, true);
      ch.setUint32(16, crc, true);
      ch.setUint32(20, size, true);
      ch.setUint32(24, size, true);
      ch.setUint16(28, name.length, true);
      ch.setUint32(42, offset, true);
      central.push(ch.buffer, name);

      offset += 30 + name.length + size;
      if (onProgress) onProgress(i + 1, entries.length);
    }
    let cdSize = 0;
    for (const c of central) cdSize += c.byteLength;
    const end = new DataView(new ArrayBuffer(22));
    end.setUint32(0, 0x06054b50, true);
    end.setUint16(8, entries.length, true);
    end.setUint16(10, entries.length, true);
    end.setUint32(12, cdSize, true);
    end.setUint32(16, offset, true);
    return new Blob([...parts, ...central, end.buffer], { type: 'application/zip' });
  }

  // ------------------------------------------------------------- zip read

  function decodeName(bytes, utf8Flag) {
    if (utf8Flag) return new TextDecoder('utf-8').decode(bytes);
    try {
      return new TextDecoder('utf-8', { fatal: true }).decode(bytes);
    } catch (_) {
      return new TextDecoder('latin1').decode(bytes);
    }
  }

  async function openZip(blob) {
    const size = blob.size;
    const tailLen = Math.min(size, 65535 + 22 + 20);
    const tailStart = size - tailLen;
    const tail = new Uint8Array(await blob.slice(tailStart).arrayBuffer());
    const tv = new DataView(tail.buffer);
    let eocd = -1;
    for (let i = tail.length - 22; i >= 0; i--) {
      if (tv.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
    }
    if (eocd < 0) throw new Error('This is not a zip file (no central directory). If it is a .cbr or .7z, unpack it and zip the pages.');

    let count = tv.getUint16(eocd + 10, true);
    let cdSize = tv.getUint32(eocd + 12, true);
    let cdOff = tv.getUint32(eocd + 16, true);
    if (count === 0xffff || cdSize === 0xffffffff || cdOff === 0xffffffff) {
      const loc = eocd - 20;
      if (loc >= 0 && tv.getUint32(loc, true) === 0x07064b50) {
        const e64Off = Number(tv.getBigUint64(loc + 8, true));
        const e64 = new DataView(await blob.slice(e64Off, e64Off + 56).arrayBuffer());
        if (e64.getUint32(0, true) === 0x06064b50) {
          count = Number(e64.getBigUint64(32, true));
          cdSize = Number(e64.getBigUint64(40, true));
          cdOff = Number(e64.getBigUint64(48, true));
        }
      }
    }

    const cd = new Uint8Array(await blob.slice(cdOff, cdOff + cdSize).arrayBuffer());
    const dv = new DataView(cd.buffer);
    const entries = [];
    let p = 0;
    for (let n = 0; n < count && p + 46 <= cd.length; n++) {
      if (dv.getUint32(p, true) !== 0x02014b50) break;
      const flags = dv.getUint16(p + 8, true);
      const method = dv.getUint16(p + 10, true);
      let csize = dv.getUint32(p + 20, true);
      let usize = dv.getUint32(p + 24, true);
      const nameLen = dv.getUint16(p + 28, true);
      const extraLen = dv.getUint16(p + 30, true);
      const commentLen = dv.getUint16(p + 32, true);
      let localOff = dv.getUint32(p + 42, true);
      const name = decodeName(cd.subarray(p + 46, p + 46 + nameLen), flags & 0x0800);
      // Zip64 sizes and offsets live in extra field 0x0001, in this order,
      // only for the fields whose 32-bit slot is saturated.
      let x = p + 46 + nameLen;
      const xEnd = x + extraLen;
      while (x + 4 <= xEnd) {
        const id = dv.getUint16(x, true);
        const len = dv.getUint16(x + 2, true);
        if (id === 0x0001) {
          let q = x + 4;
          if (usize === 0xffffffff) { usize = Number(dv.getBigUint64(q, true)); q += 8; }
          if (csize === 0xffffffff) { csize = Number(dv.getBigUint64(q, true)); q += 8; }
          if (localOff === 0xffffffff) { localOff = Number(dv.getBigUint64(q, true)); q += 8; }
        }
        x += 4 + len;
      }
      p = xEnd + commentLen;
      entries.push(makeEntry(blob, { name, flags, method, csize, usize, localOff }));
    }
    const byName = new Map(entries.map((e) => [e.name, e]));
    return { entries, get: (name) => byName.get(name) };
  }

  function makeEntry(blob, e) {
    e.dir = e.name.endsWith('/');
    e.raw = async () => {
      const lh = new DataView(await blob.slice(e.localOff, e.localOff + 30).arrayBuffer());
      if (lh.getUint32(0, true) !== 0x04034b50) throw new Error('Damaged zip entry: ' + e.name);
      const start = e.localOff + 30 + lh.getUint16(26, true) + lh.getUint16(28, true);
      return blob.slice(start, start + e.csize);
    };
    e.blob = async (type) => {
      if (e.flags & 1) throw new Error('"' + e.name + '" is password-protected. Remove the password and try again.');
      const raw = await e.raw();
      if (e.method === 0) return type ? new Blob([raw], { type }) : raw;
      if (e.method === 8) {
        const stream = raw.stream().pipeThrough(new DecompressionStream('deflate-raw'));
        const out = await new Response(stream).blob();
        return type ? new Blob([out], { type }) : out;
      }
      throw new Error('"' + e.name + '" uses a compression this tool cannot read (method ' + e.method + '). Re-zip the pages with ordinary compression.');
    };
    e.text = async () => new TextDecoder('utf-8').decode(await (await e.blob()).arrayBuffer());
    return e;
  }

  // ---------------------------------------------------------------- paths

  const naturalCompare = (a, b) => a.localeCompare(b, undefined, { numeric: true, sensitivity: 'base' });

  function dirname(path) {
    const i = path.lastIndexOf('/');
    return i < 0 ? '' : path.slice(0, i + 1);
  }

  function resolvePath(base, rel) {
    rel = rel.split('#')[0].split('?')[0];
    try { rel = decodeURIComponent(rel); } catch (_) { /* keep as written */ }
    const out = [];
    for (const seg of (rel.startsWith('/') ? rel.slice(1) : base + rel).split('/')) {
      if (seg === '..') out.pop();
      else if (seg !== '.' && seg !== '') out.push(seg);
    }
    return out.join('/');
  }

  const IMAGE_EXT = /\.(jpe?g|png|webp|gif|bmp|avif)$/i;
  const MIME = { jpg: 'image/jpeg', jpeg: 'image/jpeg', png: 'image/png', webp: 'image/webp', gif: 'image/gif', bmp: 'image/bmp', avif: 'image/avif' };
  const mimeOf = (name) => MIME[(name.split('.').pop() || '').toLowerCase()] || '';

  function isJunkPath(name) {
    return name.split('/').some((s) => s.startsWith('.') || s === '__MACOSX' || s === 'Thumbs.db');
  }

  // A progressive JPEG decodes at an eighth of its size on the reader
  // (JPEGDEC forces JPEG_SCALE_EIGHTH), so it is always worth re-encoding.
  function jpegIsProgressive(bytes) {
    let i = 2;
    while (i + 4 < bytes.length) {
      if (bytes[i] !== 0xff) { i++; continue; }
      const m = bytes[i + 1];
      if (m === 0xc2 || m === 0xc6 || m === 0xca || m === 0xce) return true;
      if (m >= 0xc0 && m <= 0xcf && m !== 0xc4 && m !== 0xc8 && m !== 0xcc) return false;
      if (m === 0xd8 || (m >= 0xd0 && m <= 0xd7) || m === 0x01 || m === 0xff) { i += 2; continue; }
      i += 2 + ((bytes[i + 2] << 8) | bytes[i + 3]);
    }
    return false;
  }

  // ----------------------------------------------------------------- tone

  // A 256-entry table: optional black/white points, contrast and midtones.
  // contrast -50..50, gamma 0.5..2 (above 1 lightens the midtones).
  function toneLut(opt, black, white) {
    const lut = new Float32Array(256);
    const C = (opt.contrast || 0) * 2.55;
    const f = (259 * (C + 255)) / (255 * (259 - C));
    const g = opt.gamma || 1;
    const lo = black || 0;
    const hi = white === undefined ? 255 : white;
    for (let v = 0; v < 256; v++) {
      let x = hi > lo ? ((v - lo) * 255) / (hi - lo) : v;
      x = f * (x - 128) + 128;
      x = Math.min(255, Math.max(0, x));
      x = 255 * Math.pow(x / 255, 1 / g);
      lut[v] = x;
    }
    return lut;
  }

  // luma: Uint8 grey values (0 black). Returns Float32 grey after tone.
  function applyTone(luma, opt) {
    let black = 0;
    let white = 255;
    if (opt.autoLevels) {
      const hist = new Uint32Array(256);
      for (let i = 0; i < luma.length; i++) hist[luma[i]]++;
      const cut = luma.length * 0.005;
      let acc = 0;
      for (let v = 0; v < 256; v++) { acc += hist[v]; if (acc > cut) { black = v; break; } }
      acc = 0;
      for (let v = 255; v >= 0; v--) { acc += hist[v]; if (acc > cut) { white = v; break; } }
      if (white - black < 48) { black = 0; white = 255; }
    }
    const lut = toneLut(opt, black, white);
    const out = new Float32Array(luma.length);
    for (let i = 0; i < luma.length; i++) out[i] = lut[luma[i]];
    return out;
  }

  const BAYER8 = (() => {
    const m = [[0, 2], [3, 1]];
    let cur = m;
    while (cur.length < 8) {
      const n = cur.length;
      const next = Array.from({ length: n * 2 }, () => new Array(n * 2));
      for (let y = 0; y < n; y++)
        for (let x = 0; x < n; x++) {
          const v = cur[y][x] * 4;
          next[y][x] = v;
          next[y][x + n] = v + 2;
          next[y + n][x] = v + 3;
          next[y + n][x + n] = v + 1;
        }
      cur = next;
    }
    return Float32Array.from(cur.flat(), (v) => (v + 0.5) / 64 - 0.5);
  })();

  // grey: Float32 0..255. Returns Uint8 level indices 0..levels-1, 0 = black.
  function quantize(grey, w, h, levels, method) {
    const step = 255 / (levels - 1);
    const top = levels - 1;
    const out = new Uint8Array(w * h);
    const q = (v) => { const k = Math.round(v / step); return k < 0 ? 0 : k > top ? top : k; };

    if (method === 'ordered') {
      for (let y = 0; y < h; y++)
        for (let x = 0; x < w; x++) {
          const i = y * w + x;
          out[i] = q(grey[i] + BAYER8[(y & 7) * 8 + (x & 7)] * step);
        }
      return out;
    }
    if (method !== 'floyd' && method !== 'atkinson') {
      for (let i = 0; i < out.length; i++) out[i] = q(grey[i]);
      return out;
    }

    const buf = Float32Array.from(grey);
    if (method === 'floyd') {
      for (let y = 0; y < h; y++) {
        const rtl = y & 1;
        for (let k = 0; k < w; k++) {
          const x = rtl ? w - 1 - k : k;
          const d = rtl ? -1 : 1;
          const i = y * w + x;
          const v = buf[i];
          const lv = q(v);
          out[i] = lv;
          const err = v - lv * step;
          const xr = x + d;
          const xl = x - d;
          if (xr >= 0 && xr < w) buf[i + d] += (err * 7) / 16;
          if (y + 1 < h) {
            const b = i + w;
            if (xl >= 0 && xl < w) buf[b - d] += (err * 3) / 16;
            buf[b] += (err * 5) / 16;
            if (xr >= 0 && xr < w) buf[b + d] += err / 16;
          }
        }
      }
      return out;
    }
    // Atkinson: spreads only 6/8 of the error, which keeps flat blacks and
    // whites clean — kind to line art, a little harsh on photos.
    for (let y = 0; y < h; y++) {
      for (let x = 0; x < w; x++) {
        const i = y * w + x;
        const v = buf[i];
        const lv = q(v);
        out[i] = lv;
        const e = (v - lv * step) / 8;
        if (x + 1 < w) buf[i + 1] += e;
        if (x + 2 < w) buf[i + 2] += e;
        if (y + 1 < h) {
          if (x > 0) buf[i + w - 1] += e;
          buf[i + w] += e;
          if (x + 1 < w) buf[i + w + 1] += e;
        }
        if (y + 2 < h) buf[i + 2 * w] += e;
      }
    }
    return out;
  }

  // ------------------------------------------------------------------ xtc
  //
  // Mirrors lib/Xtc/Xtc/XtcTypes.h and XtcParser.cpp in the reader:
  //   0x00 header (56 bytes)            magic "XTC\0" (1-bit) / "XTCH" (2-bit)
  //   0x38 title  (128 bytes, UTF-8, NUL-terminated)
  //   0xB8 author (64 bytes)
  //   0xF8 chapters, 96 bytes each: name[80] · u16 start · u16 end (1-based)
  //   ...  page table, 16 bytes a page: u64 offset · u32 size · u16 w · u16 h
  //   ...  pages: 22-byte XTG/XTH header + bitmap

  const XTC_MAGIC = 0x00435458;
  const XTCH_MAGIC = 0x48435458;
  const XTG_MAGIC = 0x00475458;
  const XTH_MAGIC = 0x00485458;
  const META_OFF = 0x38;
  const CHAPTER_OFF = 0xf8;

  function pageHeader(bytes, magic, w, h, dataSize) {
    const dv = new DataView(bytes.buffer, bytes.byteOffset, 22);
    dv.setUint32(0, magic, true);
    dv.setUint16(4, w, true);
    dv.setUint16(6, h, true);
    dv.setUint8(8, 0);
    dv.setUint8(9, 0);
    dv.setUint32(10, dataSize, true);
    // bytes 14..21: optional MD5 prefix, left zero
  }

  // 1-bit page. levels: 0 black, 1 white. Row-major, MSB first, 1 = white.
  function encodeXtg(lv, w, h) {
    const rowBytes = (w + 7) >> 3;
    const size = rowBytes * h;
    const out = new Uint8Array(22 + size);
    pageHeader(out, XTG_MAGIC, w, h, size);
    for (let y = 0; y < h; y++) {
      const row = 22 + y * rowBytes;
      for (let x = 0; x < w; x++) if (lv[y * w + x]) out[row + (x >> 3)] |= 0x80 >> (x & 7);
    }
    return out;
  }

  // 2-bit page. levels: 0 black … 3 white. The reader's pixel values are
  // 0 white, 1 dark grey, 2 light grey, 3 black (XtcReaderActivity: >=1 is
  // inked in the base pass, 1 lands in both grey planes, 2 in MSB only).
  // Two bit planes, column-major, columns stored right to left, 8 vertical
  // pixels a byte, MSB = top.
  const LEVEL_TO_XTH = [3, 1, 2, 0];

  function encodeXth(lv, w, h) {
    if (h % 8) throw new Error('XTCH page height must be a multiple of 8');
    const colBytes = h >> 3;
    const plane = colBytes * w;
    const out = new Uint8Array(22 + plane * 2);
    pageHeader(out, XTH_MAGIC, w, h, plane * 2);
    const p1 = 22;
    const p2 = 22 + plane;
    for (let x = 0; x < w; x++) {
      const base = (w - 1 - x) * colBytes;
      for (let y = 0; y < h; y++) {
        const pv = LEVEL_TO_XTH[lv[y * w + x]];
        if (!pv) continue;
        const o = base + (y >> 3);
        const bit = 0x80 >> (y & 7);
        if (pv & 2) out[p1 + o] |= bit;
        if (pv & 1) out[p2 + o] |= bit;
      }
    }
    return out;
  }

  // Bytes one encoded page takes (22-byte page header + bitmap).
  function xtcPageSize(width, height, bitDepth) {
    return 22 + (bitDepth === 2 ? ((width * height + 7) >> 3) * 2 : ((width + 7) >> 3) * height);
  }

  // Everything before the first page: header, metadata, chapters, page
  // table. Every page is the same size, so this can be written before any
  // page is rendered — the reader upload streams pages behind it.
  // chapters: [{ title, start, end }] 0-based inclusive page indices.
  function buildXtcHeader({ count, width, height, bitDepth, title, author, chapters, rtl, pageSizes }) {
    const n = count;
    if (!n) throw new Error('Nothing to write: no pages.');
    if (n > 65535) throw new Error('An XTC book holds at most 65,535 pages; this one has ' + n + '. Split it.');
    const chs = (chapters || []).filter((c) => c.start <= c.end && c.start < n);
    const tableOff = CHAPTER_OFF + chs.length * 96;
    const dataOff = tableOff + n * 16;
    const head = new Uint8Array(dataOff);
    const dv = new DataView(head.buffer);
    dv.setUint32(0x00, bitDepth === 2 ? XTCH_MAGIC : XTC_MAGIC, true);
    dv.setUint8(0x04, 1);
    dv.setUint8(0x05, 0);
    dv.setUint16(0x06, n, true);
    dv.setUint8(0x08, rtl ? 1 : 0);
    dv.setUint8(0x09, 1);
    dv.setUint8(0x0a, 0);
    dv.setUint8(0x0b, chs.length ? 1 : 0);
    dv.setUint32(0x0c, 0, true);
    dv.setBigUint64(0x10, BigInt(META_OFF), true);
    dv.setBigUint64(0x18, BigInt(tableOff), true);
    dv.setBigUint64(0x20, BigInt(dataOff), true);
    dv.setBigUint64(0x28, 0n, true);
    dv.setUint32(0x30, chs.length ? CHAPTER_OFF : 0, true);
    dv.setUint32(0x34, 0, true);
    head.set(utf8Cut(title, 127), META_OFF);
    head.set(utf8Cut(author, 63), META_OFF + 128);

    chs.forEach((c, i) => {
      const o = CHAPTER_OFF + i * 96;
      head.set(utf8Cut(c.title || 'Chapter ' + (i + 1), 79), o);
      dv.setUint16(o + 0x50, Math.min(n, c.start + 1), true);
      dv.setUint16(o + 0x52, Math.min(n, c.end + 1), true);
    });

    let cursor = dataOff;
    const fixed = xtcPageSize(width, height, bitDepth);
    for (let i = 0; i < n; i++) {
      const size = pageSizes ? pageSizes[i] : fixed;
      const o = tableOff + i * 16;
      dv.setBigUint64(o, BigInt(cursor), true);
      dv.setUint32(o + 8, size, true);
      dv.setUint16(o + 12, width, true);
      dv.setUint16(o + 14, height, true);
      cursor += size;
    }
    return head;
  }

  // pages: [Blob | Uint8Array] of encoded pages (header included), all w×h.
  function buildXtc(opts) {
    const blobs = opts.pages.map(toBlob);
    const head = buildXtcHeader({ ...opts, count: blobs.length, pageSizes: blobs.map((b) => b.size) });
    return new Blob([head, ...blobs], { type: 'application/octet-stream' });
  }

  // ----------------------------------------------------------- image size

  // Width and height from the first bytes of a JPEG, PNG, GIF, WebP or BMP,
  // without decoding. null when the header is not recognised.
  function imageSize(b) {
    const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
    const len = b.length;
    if (len > 24 && b[0] === 0x89 && b[1] === 0x50) return { w: dv.getUint32(16), h: dv.getUint32(20) };
    if (len > 10 && b[0] === 0x47 && b[1] === 0x49) return { w: dv.getUint16(6, true), h: dv.getUint16(8, true) };
    if (len > 26 && b[0] === 0x42 && b[1] === 0x4d) return { w: Math.abs(dv.getInt32(18, true)), h: Math.abs(dv.getInt32(22, true)) };
    if (len > 30 && b[0] === 0x52 && b[8] === 0x57 && b[9] === 0x45) {
      const kind = String.fromCharCode(b[12], b[13], b[14], b[15]);
      if (kind === 'VP8 ') return { w: dv.getUint16(26, true) & 0x3fff, h: dv.getUint16(28, true) & 0x3fff };
      if (kind === 'VP8L') {
        const v = dv.getUint32(21, true);
        return { w: (v & 0x3fff) + 1, h: ((v >> 14) & 0x3fff) + 1 };
      }
      if (kind === 'VP8X') return { w: 1 + (b[24] | (b[25] << 8) | (b[26] << 16)), h: 1 + (b[27] | (b[28] << 8) | (b[29] << 16)) };
    }
    if (len > 4 && b[0] === 0xff && b[1] === 0xd8) {
      let i = 2;
      while (i + 9 < len) {
        if (b[i] !== 0xff) { i++; continue; }
        const m = b[i + 1];
        if (m >= 0xc0 && m <= 0xcf && m !== 0xc4 && m !== 0xc8 && m !== 0xcc) return { w: dv.getUint16(i + 7), h: dv.getUint16(i + 5) };
        if (m === 0xd8 || (m >= 0xd0 && m <= 0xd7) || m === 0x01 || m === 0xff) { i += 2; continue; }
        i += 2 + dv.getUint16(i + 2);
      }
    }
    return null;
  }

  // ----------------------------------------------------------------- epub

  const esc = (s) => String(s == null ? '' : s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

  function newUuid() {
    if (G.crypto && G.crypto.randomUUID) return G.crypto.randomUUID();
    const b = new Uint8Array(16);
    G.crypto.getRandomValues(b);
    b[6] = (b[6] & 0x0f) | 0x40;
    b[8] = (b[8] & 0x3f) | 0x80;
    const h = [...b].map((x) => x.toString(16).padStart(2, '0')).join('');
    return `${h.slice(0, 8)}-${h.slice(8, 12)}-${h.slice(12, 16)}-${h.slice(16, 20)}-${h.slice(20)}`;
  }

  const CONTAINER_XML =
    '<?xml version="1.0" encoding="UTF-8"?>\n' +
    '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">\n' +
    '  <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>\n' +
    '</container>\n';

  function xhtmlDoc(title, lang, head, body, extraNs) {
    return (
      '<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE html>\n' +
      `<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"${extraNs || ''} lang="${esc(lang)}" xml:lang="${esc(lang)}">\n` +
      `<head>\n<meta charset="utf-8"/>\n<title>${esc(title)}</title>\n${head}</head>\n<body>\n${body}\n</body>\n</html>\n`
    );
  }

  function navDoc(book, toc) {
    const items = toc.map((t) => `      <li><a href="${esc(t.href)}">${esc(t.title)}</a></li>`).join('\n');
    return xhtmlDoc('Contents', book.lang, '', `<nav epub:type="toc" id="toc">\n  <h1>Contents</h1>\n  <ol>\n${items}\n  </ol>\n</nav>`);
  }

  function ncxDoc(book, toc) {
    const pts = toc
      .map((t, i) => `    <navPoint id="np${i + 1}" playOrder="${i + 1}"><navLabel><text>${esc(t.title)}</text></navLabel><content src="${esc(t.href)}"/></navPoint>`)
      .join('\n');
    return (
      '<?xml version="1.0" encoding="UTF-8"?>\n' +
      '<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">\n' +
      `  <head><meta name="dtb:uid" content="urn:uuid:${book.id}"/><meta name="dtb:depth" content="1"/><meta name="dtb:totalPageCount" content="0"/><meta name="dtb:maxPageNumber" content="0"/></head>\n` +
      `  <docTitle><text>${esc(book.title)}</text></docTitle>\n  <navMap>\n${pts}\n  </navMap>\n</ncx>\n`
    );
  }

  function opfDoc(book, manifest, spine, extraMeta, spineAttr) {
    const modified = new Date().toISOString().replace(/\.\d+Z$/, 'Z');
    const items = manifest
      .map((m) => `    <item id="${m.id}" href="${esc(m.href)}" media-type="${m.type}"${m.props ? ` properties="${m.props}"` : ''}/>`)
      .join('\n');
    const refs = spine.map((s) => `    <itemref idref="${s.id}"${s.props ? ` properties="${s.props}"` : ''}${s.linear === false ? ' linear="no"' : ''}/>`).join('\n');
    return (
      '<?xml version="1.0" encoding="UTF-8"?>\n' +
      '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="bookid" prefix="rendition: http://www.idpf.org/vocab/rendition/#">\n' +
      '  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:opf="http://www.idpf.org/2007/opf">\n' +
      `    <dc:identifier id="bookid">urn:uuid:${book.id}</dc:identifier>\n` +
      `    <dc:title>${esc(book.title)}</dc:title>\n` +
      (book.author ? `    <dc:creator id="creator">${esc(book.author)}</dc:creator>\n` : '') +
      `    <dc:language>${esc(book.lang)}</dc:language>\n` +
      `    <meta property="dcterms:modified">${modified}</meta>\n` +
      '    <meta name="cover" content="cover-image"/>\n' +
      (extraMeta || '') +
      '  </metadata>\n  <manifest>\n' + items + '\n  </manifest>\n' +
      `  <spine toc="ncx"${spineAttr || ''}>\n` + refs + '\n  </spine>\n</package>\n'
    );
  }

  // Image-per-page, fixed-layout EPUB 3 (with the Kindle/Kobo hints), for
  // readers that do not take XTC. pages: [{ blob, ext }] in reading order.
  function buildMangaEpub({ title, author, lang, rtl, width, height, pages, chapters }) {
    const book = { id: newUuid(), title: title || 'Untitled', author, lang: lang || 'en' };
    const files = [];
    const manifest = [
      { id: 'nav', href: 'nav.xhtml', type: 'application/xhtml+xml', props: 'nav' },
      { id: 'ncx', href: 'toc.ncx', type: 'application/x-dtbncx+xml' },
      { id: 'css', href: 'style.css', type: 'text/css' },
    ];
    const spine = [];
    const css = `@page { margin: 0; }\nhtml, body { margin: 0; padding: 0; width: ${width}px; height: ${height}px; background: #fff; }\n` +
      'div.page { width: 100%; height: 100%; text-align: center; }\nimg { width: 100%; height: 100%; object-fit: contain; display: block; }\n';
    files.push({ name: 'OEBPS/style.css', data: css });

    pages.forEach((p, i) => {
      const n = String(i + 1).padStart(4, '0');
      const img = `images/p${n}.${p.ext}`;
      const doc = `p${n}.xhtml`;
      const imgId = i === 0 ? 'cover-image' : `img${n}`;
      manifest.push({ id: imgId, href: img, type: MIME[p.ext], props: i === 0 ? 'cover-image' : '' });
      manifest.push({ id: `p${n}`, href: doc, type: 'application/xhtml+xml' });
      spine.push({ id: `p${n}` });
      files.push({ name: 'OEBPS/' + img, data: p.blob });
      files.push({
        name: 'OEBPS/' + doc,
        data: xhtmlDoc(
          `${book.title} — ${i + 1}`,
          book.lang,
          `<meta name="viewport" content="width=${width}, height=${height}"/>\n<link rel="stylesheet" type="text/css" href="style.css"/>\n`,
          `<div class="page"><img src="${img}" alt="Page ${i + 1}"/></div>`
        ),
      });
    });

    const toc = (chapters && chapters.length ? chapters : [{ title: book.title, start: 0 }]).map((c) => ({
      title: c.title,
      href: `p${String(c.start + 1).padStart(4, '0')}.xhtml`,
    }));
    files.push({ name: 'OEBPS/nav.xhtml', data: navDoc(book, toc) });
    files.push({ name: 'OEBPS/toc.ncx', data: ncxDoc(book, toc) });
    const meta =
      '    <meta property="rendition:layout">pre-paginated</meta>\n' +
      '    <meta property="rendition:spread">none</meta>\n' +
      '    <meta property="rendition:orientation">portrait</meta>\n' +
      '    <meta name="fixed-layout" content="true"/>\n' +
      '    <meta name="book-type" content="comic"/>\n' +
      `    <meta name="original-resolution" content="${width}x${height}"/>\n` +
      (rtl ? '    <meta name="primary-writing-mode" content="horizontal-rl"/>\n' : '');
    files.push({ name: 'OEBPS/content.opf', data: opfDoc(book, manifest, spine, meta, rtl ? ' page-progression-direction="rtl"' : '') });
    return epubEntries(files);
  }

  const TEXT_CSS =
    'body { margin: 0 4%; }\n' +
    'p { margin: 0; text-indent: 1.4em; text-align: justify; line-height: 1.45; }\n' +
    'h1, h2, h3 + p, h2 + p, h1 + p, hr + p, blockquote + p, .first { text-indent: 0; }\n' +
    'h1, h2 { text-align: center; margin: 2em 0 1.2em; line-height: 1.25; page-break-after: avoid; }\n' +
    'h3, h4 { margin: 1.4em 0 0.6em; page-break-after: avoid; }\n' +
    'hr.scene { border: 0; text-align: center; margin: 1em 0; }\n' +
    'hr.scene:after { content: "* * *"; }\n' +
    'blockquote { margin: 0.8em 1.5em; font-style: italic; }\n' +
    'pre { white-space: pre-wrap; font-size: 0.85em; }\n' +
    'ul, ol { margin: 0.6em 0 0.6em 1.5em; padding: 0; }\n' +
    'div.cover { text-align: center; margin: 0; padding: 0; }\n' +
    'div.cover img { max-width: 100%; max-height: 100%; }\n';

  // chapters: [{ title, blocks: [xhtml string] }]. cover: { blob, ext } | null.
  // Chapters over ~180 KB are split into parts so the reader's section
  // cache does not have to lay out one enormous file.
  function buildTextEpub({ title, author, lang, chapters, cover, listParts }) {
    const book = { id: newUuid(), title: title || 'Untitled', author, lang: lang || 'en' };
    const files = [{ name: 'OEBPS/style.css', data: TEXT_CSS }];
    const manifest = [
      { id: 'nav', href: 'nav.xhtml', type: 'application/xhtml+xml', props: 'nav' },
      { id: 'ncx', href: 'toc.ncx', type: 'application/x-dtbncx+xml' },
      { id: 'css', href: 'style.css', type: 'text/css' },
    ];
    const spine = [];
    const toc = [];
    const head = '<link rel="stylesheet" type="text/css" href="style.css"/>\n';

    if (cover) {
      const href = `images/cover.${cover.ext}`;
      manifest.push({ id: 'cover-image', href, type: MIME[cover.ext], props: 'cover-image' });
      manifest.push({ id: 'cover', href: 'cover.xhtml', type: 'application/xhtml+xml' });
      spine.push({ id: 'cover' });
      files.push({ name: 'OEBPS/' + href, data: cover.blob });
      files.push({ name: 'OEBPS/cover.xhtml', data: xhtmlDoc('Cover', book.lang, head, `<div class="cover"><img src="${href}" alt="${esc(book.title)}"/></div>`) });
    }

    const LIMIT = 180 * 1024;
    let fileNo = 0;
    chapters.forEach((ch, ci) => {
      const parts = [[]];
      let size = 0;
      for (const b of ch.blocks) {
        if (size > LIMIT && parts[parts.length - 1].length) { parts.push([]); size = 0; }
        parts[parts.length - 1].push(b);
        size += b.length;
      }
      parts.forEach((blocks, pi) => {
        fileNo++;
        const id = 'c' + String(fileNo).padStart(4, '0');
        const href = id + '.xhtml';
        const heading = pi === 0 && ch.title && !ch.untitled ? `<h2>${esc(ch.title)}</h2>\n` : '';
        manifest.push({ id, href, type: 'application/xhtml+xml' });
        spine.push({ id });
        if (pi === 0 || listParts) toc.push({ title: listParts && parts.length > 1 ? `${ch.title} (${pi + 1})` : ch.title || `Part ${ci + 1}`, href });
        files.push({ name: 'OEBPS/' + href, data: xhtmlDoc(ch.title || book.title, book.lang, head, `<section epub:type="chapter">\n${heading}${blocks.join('\n')}\n</section>`) });
      });
    });

    files.push({ name: 'OEBPS/nav.xhtml', data: navDoc(book, toc) });
    files.push({ name: 'OEBPS/toc.ncx', data: ncxDoc(book, toc) });
    files.push({ name: 'OEBPS/content.opf', data: opfDoc(book, manifest, spine, '', '') });
    return epubEntries(files);
  }

  function epubEntries(files) {
    return [
      { name: 'mimetype', data: 'application/epub+zip' },
      { name: 'META-INF/container.xml', data: CONTAINER_XML },
      ...files,
    ];
  }

  // ----------------------------------------------------------------- text

  function decodeText(bytes) {
    let b = bytes;
    if (b[0] === 0xff && b[1] === 0xfe) return new TextDecoder('utf-16le').decode(b.subarray(2));
    if (b[0] === 0xfe && b[1] === 0xff) return new TextDecoder('utf-16be').decode(b.subarray(2));
    if (b[0] === 0xef && b[1] === 0xbb && b[2] === 0xbf) b = b.subarray(3);
    try {
      return new TextDecoder('utf-8', { fatal: true }).decode(b);
    } catch (_) {
      return new TextDecoder('windows-1252').decode(b);
    }
  }

  // Project Gutenberg texts wrap the book in a licence header and footer.
  function stripGutenberg(text) {
    const start = text.search(/^\*{3}\s*START OF (THE|THIS) PROJECT GUTENBERG.*$/im);
    const end = text.search(/^\*{3}\s*END OF (THE|THIS) PROJECT GUTENBERG.*$/im);
    if (start < 0) return { text, stripped: false };
    const from = text.indexOf('\n', start) + 1;
    return { text: text.slice(from, end > from ? end : undefined), stripped: true };
  }

  const HEADING_RES = [
    /^(chapter|chap\.|part|book|volume|prologue|epilogue|interlude|introduction|preface|foreword|afterword|appendix|letter)\b.{0,70}$/i,
    /^第[\s0-9０-９一二三四五六七八九十百千零〇两]+[章回节節話话部卷]/,
    /^[IVXLCDM]{1,8}\.?$/,
    /^\d{1,3}\.?$/,
  ];

  const isHeading = (line) => line.length <= 80 && HEADING_RES.some((re) => re.test(line));
  const SCENE_RE = /^([*·•~#=-]\s*){3,}$/;

  function inlineTxt(s) {
    return esc(s).replace(/_([^_\n]{1,200})_/g, '<em>$1</em>');
  }

  // Returns [{ title, blocks, untitled? }].
  function parseTxt(text, { detect }) {
    const lines = text.replace(/\r\n?/g, '\n').split('\n');
    const nonBlank = lines.filter((l) => l.trim()).length;
    const blank = lines.length - nonBlank;
    // Hard-wrapped text separates paragraphs with blank lines; some files
    // put one paragraph on each line instead.
    const blankSeparated = blank >= nonBlank * 0.05;
    const paras = [];
    if (blankSeparated) {
      let cur = [];
      for (const l of lines) {
        if (l.trim()) cur.push(l.trim());
        else if (cur.length) { paras.push(cur); cur = []; }
      }
      if (cur.length) paras.push(cur);
    } else {
      for (const l of lines) if (l.trim()) paras.push([l.trim()]);
    }

    const chapters = [];
    let cur = { title: '', blocks: [], untitled: true };
    const flush = () => {
      if (cur.blocks.length) chapters.push(cur);
      else if (!cur.untitled) {
        // A heading with nothing under it (a contents list): keep its text.
        const prev = chapters[chapters.length - 1];
        if (prev) prev.blocks.push(`<p class="first">${esc(cur.title)}</p>`);
        else chapters.push({ title: '', blocks: [`<p class="first">${esc(cur.title)}</p>`], untitled: true });
      }
    };
    let afterBreak = true;
    for (let i = 0; i < paras.length; i++) {
      const p = paras[i];
      const one = p.length === 1 ? p[0] : null;
      if (detect !== 'none' && one && isHeading(one)) {
        flush();
        let title = one;
        // "CHAPTER I." followed by its name on its own line.
        const next = paras[i + 1];
        if (one.length <= 20 && next && next.length === 1 && next[0].length <= 60 && !/[.,;:]["”’']?$/.test(next[0]) && !isHeading(next[0])) {
          title = one.replace(/[.:]?$/, '. ') + next[0];
          i++;
        }
        cur = { title, blocks: [] };
        afterBreak = true;
        continue;
      }
      // Gutenberg style: "CHAPTER I." with the chapter's name on the next line.
      if (detect !== 'none' && p.length === 2 && isHeading(p[0]) && p[0].length <= 24 && p[1].length <= 60 && !isHeading(p[1]) && !/[.,;:]["”’']?$/.test(p[1])) {
        flush();
        cur = { title: p[0].replace(/[.:]?$/, '. ') + p[1], blocks: [] };
        afterBreak = true;
        continue;
      }
      // A contents list: every line a heading. Keep its lines apart.
      if (p.length >= 3 && p.every((l) => isHeading(l.replace(/\s{2,}.*$/, '')))) {
        cur.blocks.push(`<p class="first">${p.map(inlineTxt).join('<br/>')}</p>`);
        afterBreak = true;
        continue;
      }
      const joined = p.join(' ');
      if (SCENE_RE.test(joined)) { cur.blocks.push('<hr class="scene"/>'); afterBreak = true; continue; }
      cur.blocks.push(`<p${afterBreak ? ' class="first"' : ''}>${inlineTxt(joined)}</p>`);
      afterBreak = false;
    }
    flush();
    return chapters;
  }

  function inlineMd(s) {
    const codes = [];
    let t = esc(s).replace(/`([^`]+)`/g, (_, c) => { codes.push(c); return `\u0000${codes.length - 1}\u0000`; });
    t = t
      .replace(/!\[([^\]]*)\]\([^)]*\)/g, '$1')
      .replace(/\[([^\]]+)\]\((https?:[^)\s]+)[^)]*\)/g, '<a href="$2">$1</a>')
      .replace(/\[([^\]]+)\]\([^)]*\)/g, '$1')
      .replace(/(\*\*|__)(?=\S)([\s\S]*?\S)\1/g, '<strong>$2</strong>')
      .replace(/(\*|_)(?=\S)([\s\S]*?\S)\1/g, '<em>$2</em>');
    return t.replace(/\u0000(\d+)\u0000/g, (_, i) => `<code>${codes[+i]}</code>`);
  }

  // splitLevel: headings at this level or above start a chapter.
  function parseMarkdown(text, { splitLevel }) {
    const lines = text.replace(/\r\n?/g, '\n').split('\n');
    const chapters = [];
    let cur = { title: '', blocks: [], untitled: true };
    let para = [];
    let list = null;
    let quote = [];
    const endPara = () => { if (para.length) { cur.blocks.push(`<p>${inlineMd(para.join(' '))}</p>`); para = []; } };
    const endList = () => { if (list) { cur.blocks.push(`<${list.tag}>${list.items.map((i) => `<li>${inlineMd(i)}</li>`).join('')}</${list.tag}>`); list = null; } };
    const endQuote = () => { if (quote.length) { cur.blocks.push(`<blockquote><p>${inlineMd(quote.join(' '))}</p></blockquote>`); quote = []; } };
    const endAll = () => { endPara(); endList(); endQuote(); };

    for (let i = 0; i < lines.length; i++) {
      const line = lines[i];
      const t = line.trim();
      if (/^(```|~~~)/.test(t)) {
        endAll();
        const fence = t.slice(0, 3);
        const code = [];
        for (i++; i < lines.length && !lines[i].trim().startsWith(fence); i++) code.push(lines[i]);
        cur.blocks.push(`<pre>${esc(code.join('\n'))}</pre>`);
        continue;
      }
      const h = /^(#{1,6})\s+(.*?)\s*#*$/.exec(t);
      if (h) {
        endAll();
        const level = h[1].length;
        if (level <= splitLevel) {
          if (cur.blocks.length || !cur.untitled) chapters.push(cur);
          cur = { title: h[2].replace(/[*_`]/g, ''), blocks: [] };
        } else {
          cur.blocks.push(`<h${Math.min(6, level + 1)}>${inlineMd(h[2])}</h${Math.min(6, level + 1)}>`);
        }
        continue;
      }
      if (!t) { endAll(); continue; }
      if (/^([-*_]\s*){3,}$/.test(t)) { endAll(); cur.blocks.push('<hr class="scene"/>'); continue; }
      if (t.startsWith('>')) { endPara(); endList(); quote.push(t.replace(/^>\s?/, '')); continue; }
      const li = /^([-*+]|\d+[.)])\s+(.*)$/.exec(t);
      if (li) {
        endPara(); endQuote();
        const tag = /\d/.test(li[1]) ? 'ol' : 'ul';
        if (!list || list.tag !== tag) { endList(); list = { tag, items: [] }; }
        list.items.push(li[2]);
        continue;
      }
      if (list && /^\s{2,}/.test(line)) { list.items[list.items.length - 1] += ' ' + t; continue; }
      endList(); endQuote();
      para.push(t);
    }
    endAll();
    if (cur.blocks.length || !cur.untitled) chapters.push(cur);
    return chapters;
  }

  // ---------------------------------------------------------------- export

  G.Bindery = {
    crc32, crc32Blob, writeZip, openZip, naturalCompare, dirname, resolvePath,
    IMAGE_EXT, MIME, mimeOf, isJunkPath, jpegIsProgressive,
    toneLut, applyTone, quantize,
    encodeXtg, encodeXth, buildXtc, buildXtcHeader, xtcPageSize, imageSize,
    buildMangaEpub, buildTextEpub, esc,
    decodeText, stripGutenberg, parseTxt, parseMarkdown, isHeading,
  };
})(typeof globalThis !== 'undefined' ? globalThis : window);
