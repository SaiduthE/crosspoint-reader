/* e-Minimal web UI shell — see docs/web-ui.md */
(function () {
  'use strict';
  var d = document, body = d.body;
  var page = body.getAttribute('data-page') || '';
  var title = body.getAttribute('data-title') || 'e-Minimal';
  var $ = function (id) { return d.getElementById(id); };
  var esc = function (s) {
    return String(s == null ? '' : s).replace(/[&<>"]/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c];
    });
  };
  var FOCUSABLE = 'a[href],button:not([disabled]),input:not([disabled]):not([type=hidden]),select:not([disabled]),textarea:not([disabled]),[tabindex]:not([tabindex="-1"])';

  /* Shell */
  body.classList.add('has-shell');
  body.insertAdjacentHTML('afterbegin',
    '<header class="topbar">' +
      '<button class="menu-btn" id="menuBtn" aria-controls="drawer" aria-expanded="false" aria-label="Menu">' +
        '<svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" aria-hidden="true"><path d="M4 7h16M4 12h16M4 17h16"/></svg>' +
      '</button>' +
      '<div class="topbar-title" id="topbarTitle"></div>' +
      '<div class="topbar-status" id="topbarStatus"></div>' +
    '</header>' +
    '<div class="backdrop" id="backdrop"></div>' +
    '<nav class="drawer" id="drawer" aria-label="Pages">' +
      '<a class="brand" href="/">e-Minimal</a>' +
      '<a href="/" data-page="home">Home</a>' +
      '<a href="/files" data-page="files">Files</a>' +
      '<a href="/settings" data-page="settings">Settings</a>' +
      '<a href="/fonts" data-page="fonts">Fonts</a>' +
      '<div class="drawer-foot" id="drawerFoot"></div>' +
    '</nav>');

  var menuBtn = $('menuBtn'), drawer = $('drawer'), backdrop = $('backdrop');
  $('topbarTitle').textContent = title;
  var active = drawer.querySelector('a[data-page="' + page + '"]');
  if (active) { active.classList.add('active'); active.setAttribute('aria-current', 'page'); }

  function isOpen() { return body.classList.contains('drawer-open'); }
  function setDrawer(open) {
    body.classList.toggle('drawer-open', open);
    menuBtn.setAttribute('aria-expanded', open ? 'true' : 'false');
    if (open) (active || drawer.querySelector('a')).focus();
    else menuBtn.focus();
  }
  menuBtn.addEventListener('click', function () { setDrawer(!isOpen()); });
  backdrop.addEventListener('click', function () { setDrawer(false); });
  drawer.addEventListener('click', function (e) { if (e.target.closest('a')) body.classList.remove('drawer-open'); });
  var wide = window.matchMedia('(min-width: 900px)');
  var onWide = function () { if (wide.matches && isOpen()) body.classList.remove('drawer-open'); };
  if (wide.addEventListener) wide.addEventListener('change', onWide); else wide.addListener(onWide);

  /* Status: topbar chip and drawer foot */
  fetch('/api/status', { cache: 'no-store' })
    .then(function (r) { if (!r.ok) throw new Error(r.status); return r.json(); })
    .then(function (s) {
      var mode = s.mode === 'AP' ? 'Hotspot' : 'Joined';
      $('topbarStatus').innerHTML = '<span class="chip">' + esc(mode + ' · ' + (s.ip || '')) + '</span>';
      var lines = [];
      if (s.device) lines.push(esc(s.device));
      if (s.ip) lines.push(esc(s.ip));
      if (s.freeHeap) lines.push(Math.round(s.freeHeap / 1024) + ' KB free');
      if (s.version) lines.push('Version ' + esc(s.version));
      $('drawerFoot').innerHTML = lines.map(function (l) { return '<div>' + l + '</div>'; }).join('');
    })
    .catch(function () {});

  /* Modals: open/close with focus trap, Esc, backdrop tap, focus return */
  var stack = [];
  function openModal(el, opts) {
    opts = opts || {};
    var rec = { el: el, opener: d.activeElement, dismiss: opts.dismiss !== false, onClose: opts.onClose };
    stack.push(rec);
    el.classList.add('open');
    var box = el.querySelector('.modal') || el;
    box.setAttribute('role', 'dialog');
    box.setAttribute('aria-modal', 'true');
    // Scope every alternative of the selector list, not just the first one.
    var inBody = FOCUSABLE.split(',').map(function (sel) { return '.modal-body ' + sel; }).join(',');
    var first = el.querySelector('[autofocus]') || el.querySelector(inBody) || el.querySelector(FOCUSABLE);
    if (first) setTimeout(function () { first.focus(); }, 0);
  }
  function closeModal(el, result) {
    var i = stack.length - 1;
    while (i >= 0 && el && stack[i].el !== el) i--;
    if (i < 0) return;
    var rec = stack.splice(i, 1)[0];
    rec.el.classList.remove('open');
    if (rec.opener && rec.opener.focus) rec.opener.focus();
    if (rec.onClose) rec.onClose(result);
  }
  d.addEventListener('click', function (e) {
    var top = stack[stack.length - 1];
    if (top && top.dismiss && e.target === top.el) closeModal(top.el, false);
  });
  d.addEventListener('keydown', function (e) {
    var top = stack[stack.length - 1];
    if (e.key === 'Escape') {
      if (top) { if (top.dismiss) { e.preventDefault(); closeModal(top.el, false); } }
      else if (isOpen()) { e.preventDefault(); setDrawer(false); }
    } else if (e.key === 'Tab' && top) {
      var f = top.el.querySelectorAll(FOCUSABLE);
      if (!f.length) { e.preventDefault(); return; }
      var a = f[0], z = f[f.length - 1];
      if (e.shiftKey && (d.activeElement === a || !top.el.contains(d.activeElement))) { e.preventDefault(); z.focus(); }
      else if (!e.shiftKey && (d.activeElement === z || !top.el.contains(d.activeElement))) { e.preventDefault(); a.focus(); }
    }
  });

  /* Toast */
  var toastEl, toastTimer;
  function toast(text, kind) {
    if (!toastEl) {
      toastEl = d.createElement('div');
      toastEl.setAttribute('role', 'status');
      toastEl.setAttribute('aria-live', 'polite');
      body.appendChild(toastEl);
    }
    clearTimeout(toastTimer);
    toastEl.className = 'toast' + (kind ? ' toast-' + kind : '');
    toastEl.textContent = text;
    var bar = d.querySelector('.savebar.show');
    toastEl.style.bottom = bar ? (bar.offsetHeight + 16) + 'px' : '';
    void toastEl.offsetWidth;
    toastEl.classList.add('show');
    toastTimer = setTimeout(function () { toastEl.classList.remove('show'); }, 3000);
  }

  /* Confirm */
  var confirmEl;
  function confirm(text, opts) {
    opts = opts || {};
    if (!confirmEl) {
      confirmEl = d.createElement('div');
      confirmEl.className = 'modal-backdrop';
      confirmEl.innerHTML =
        '<div class="modal" aria-labelledby="uiConfirmText">' +
          '<div class="modal-body"><p id="uiConfirmText"></p></div>' +
          '<div class="modal-foot"><button type="button" class="btn" data-x="cancel">Cancel</button>' +
          '<button type="button" class="btn" data-x="ok"></button></div></div>';
      body.appendChild(confirmEl);
    }
    var ok = confirmEl.querySelector('[data-x=ok]'), cancel = confirmEl.querySelector('[data-x=cancel]');
    confirmEl.querySelector('p').textContent = text;
    ok.textContent = opts.ok || 'OK';
    ok.className = 'btn ' + (opts.danger ? 'btn-danger' : 'btn-primary');
    return new Promise(function (resolve) {
      ok.onclick = function () { closeModal(confirmEl, true); };
      cancel.onclick = function () { closeModal(confirmEl, false); };
      openModal(confirmEl, { onClose: function (r) { resolve(!!r); } });
      setTimeout(function () { (opts.danger ? cancel : ok).focus(); }, 0);
    });
  }

  window.ui = { toast: toast, confirm: confirm, openModal: openModal, closeModal: closeModal };
})();
