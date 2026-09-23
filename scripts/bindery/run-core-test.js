// Runs core.js under Node on the fixtures from make_fixtures.py and writes
// an XTCH, an XTC, a manga EPUB and a text EPUB for check_outputs.py.
const fs = require('fs');
const path = require('path');
require('../../src/network/html/js/bindery-core.js');
const B = globalThis.Bindery;

const [fixtures, out] = process.argv.slice(2);
fs.mkdirSync(out, { recursive: true });
const W = 1404, H = 1872;
const names = ['bj0', 'bj12', 'pepper'];
const assert = (c, m) => { if (!c) { console.error('FAIL', m); process.exitCode = 1; } else console.log('ok  ', m); };

(async () => {
  // zip read: deflated entries, junk filter, natural order, UTF-8 names
  const zb = new Blob([fs.readFileSync(path.join(fixtures, 'deflated.zip'))]);
  const z = await B.openZip(zb);
  const kept = z.entries.filter((e) => !e.dir && !B.isJunkPath(e.name)).map((e) => e.name).sort(B.naturalCompare);
  assert(JSON.stringify(kept) === JSON.stringify(['Vol 1/002.txt', 'Vol 1/010.txt', 'Vol 2/Ünïcode.txt']), 'openZip lists, filters and sorts: ' + kept.join(' | '));
  assert((await z.get('Vol 1/002.txt').text()) === 'second '.repeat(1000), 'openZip inflates a deflated entry');

  const pages2 = [], pages1 = [];
  for (const n of names) {
    const luma = new Uint8Array(fs.readFileSync(path.join(fixtures, n + '.gray')));
    const grey = B.applyTone(luma, { autoLevels: true, contrast: 0, gamma: 1 });
    let t = Date.now();
    const lv4 = B.quantize(grey, W, H, 4, 'floyd');
    const tq = Date.now() - t; t = Date.now();
    pages2.push(B.encodeXth(lv4, W, H));
    const te = Date.now() - t;
    pages1.push(B.encodeXtg(B.quantize(grey, W, H, 2, 'atkinson'), W, H));
    console.log(`     ${n}: quantize ${tq} ms, encodeXth ${te} ms`);
  }
  const chapters = [{ title: 'Give My Regards to Black Jack — 第1話', start: 0, end: 1 }, { title: 'Pepper&Carrot', start: 2, end: 2 }];
  const xtch = B.buildXtc({ pages: pages2, width: W, height: H, bitDepth: 2, title: 'Bindery test (XTCH) — ブラックジャック', author: 'Shuho Sato / David Revoy', chapters, rtl: true });
  fs.writeFileSync(path.join(out, 'test.xtch'), Buffer.from(await xtch.arrayBuffer()));
  const xtc = B.buildXtc({ pages: pages1, width: W, height: H, bitDepth: 1, title: 'Bindery test (XTC)', author: 'x', chapters: [], rtl: false });
  fs.writeFileSync(path.join(out, 'test.xtc'), Buffer.from(await xtc.arrayBuffer()));
  assert(xtch.size === 0xf8 + 2 * 96 + 3 * 16 + 3 * (22 + 2 * (W * H / 8)), 'XTCH size is header+meta+chapters+table+pages: ' + xtch.size);

  // manga epub from the source PNGs (stand-ins for the JPEGs the page makes)
  const mp = names.map((n) => ({ blob: new Blob([fs.readFileSync(path.join(fixtures, n + '_src.png'))]), ext: 'png' }));
  const me = await B.writeZip(B.buildMangaEpub({ title: 'Bindery manga test', author: 'Shuho Sato', lang: 'en', rtl: true, width: W, height: H, pages: mp, chapters }));
  fs.writeFileSync(path.join(out, 'manga.epub'), Buffer.from(await me.arrayBuffer()));

  // text epub from a TXT with Gutenberg wrapper, contents list, headings
  const txt = [
    'The Project Gutenberg eBook of Test', 'licence blah', '',
    '*** START OF THE PROJECT GUTENBERG EBOOK TEST ***', '',
    'CONTENTS', '', 'CHAPTER I.', '', 'Down the Rabbit-Hole', '',
    'Alice was beginning to get very tired of sitting by her sister on the',
    'bank, and of having _nothing_ to do.', '',
    'So she was considering in her own mind.', '', '* * *', '', 'After the break.', '',
    'CHAPTER II.', '', 'The Pool of Tears', '', '“Curiouser and curiouser!” cried Alice.', '',
    '*** END OF THE PROJECT GUTENBERG EBOOK TEST ***', 'footer licence',
  ].join('\r\n');
  const g = B.stripGutenberg(B.decodeText(new TextEncoder().encode(txt)));
  const chs = B.parseTxt(g.text, { detect: 'auto' });
  console.log('     chapters:', JSON.stringify(chs.map((c) => [c.title, c.blocks.length])));
  assert(g.stripped && !g.text.includes('licence'), 'Gutenberg header and footer stripped');
  assert(chs.length === 3 && chs[1].title === 'CHAPTER I. Down the Rabbit-Hole' && chs[2].title === 'CHAPTER II. The Pool of Tears', 'TXT chapters detected with their names');
  assert(chs[1].blocks.some((b) => b.includes('<em>nothing</em>')) && chs[1].blocks.includes('<hr class="scene"/>'), 'italics and scene break');
  const gb = B.parseTxt([
    'Title', '',
    ' CHAPTER I.     Down the Rabbit-Hole', ' CHAPTER II.    The Pool of Tears', ' CHAPTER III.   A Caucus-Race', '',
    'CHAPTER I.', 'Down the Rabbit-Hole', '', 'Alice was tired.', '',
    'CHAPTER II.', 'The Pool of Tears', '', 'Curiouser.', '',
  ].join('\n'), { detect: 'auto' });
  assert(gb.length === 3 && gb[1].title === 'CHAPTER I. Down the Rabbit-Hole' && gb[0].blocks[1].includes('<br/>'), 'Gutenberg two-line headings and contents list: ' + JSON.stringify(gb.map((c) => c.title)));
  const md = B.parseMarkdown('# One\n\nHello **bold** and *it* `c<d`.\n\n- a\n- b\n\n## Sub\n\ntext\n\n# Two\n\n> quote\n', { splitLevel: 1 });
  assert(md.length === 2 && md[0].blocks.join('').includes('<strong>bold</strong>') && md[0].blocks.join('').includes('<code>c&lt;d</code>'), 'Markdown chapters and inline');
  const te = await B.writeZip(B.buildTextEpub({ title: 'Bindery text test', author: 'Lewis Carroll', lang: 'en', chapters: chs, cover: { blob: mp[0].blob, ext: 'png' } }));
  fs.writeFileSync(path.join(out, 'text.epub'), Buffer.from(await te.arrayBuffer()));
  // round trip our own zip through our own reader
  const rz = await B.openZip(te);
  assert(rz.entries[0].name === 'mimetype' && (await rz.entries[0].text()) === 'application/epub+zip', 'writeZip → openZip round trip, mimetype first');
  // streaming header == the header buildXtc writes; image size probe
  const head = B.buildXtcHeader({ count: 3, width: W, height: H, bitDepth: 2, title: 'Bindery test (XTCH) — ブラックジャック', author: 'Shuho Sato / David Revoy', chapters, rtl: true });
  const full = new Uint8Array(await xtch.arrayBuffer());
  assert(head.length < full.length && head.every((v, i) => v === full[i]), 'buildXtcHeader matches the streamed file head (' + head.length + ' bytes)');
  assert(B.xtcPageSize(W, H, 2) === 657094 && B.xtcPageSize(W, H, 1) === 329494, 'page sizes');
  const jpg = fs.readFileSync(process.env.PC_JPG);
  const js = B.imageSize(new Uint8Array(jpg.buffer, jpg.byteOffset, 65536));
  const png = fs.readFileSync(path.join(fixtures, 'bj0_src.png'));
  const ps = B.imageSize(new Uint8Array(png.buffer, png.byteOffset, 64));
  assert(js && js.w === 2481 && js.h === 3503 && ps.w === W && ps.h === H, `imageSize: jpeg ${JSON.stringify(js)}, png ${JSON.stringify(ps)}`);
  assert(B.jpegIsProgressive(new Uint8Array([0xff, 0xd8, 0xff, 0xc2, 0, 4, 0, 0])) && !B.jpegIsProgressive(new Uint8Array([0xff, 0xd8, 0xff, 0xc0, 0, 4, 0, 0])), 'progressive JPEG detection');
})();
