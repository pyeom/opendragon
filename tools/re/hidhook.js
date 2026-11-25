// Frida agent: log the HID traffic of a vendor app on Windows.
//   W/R <collection> <bytes>       WriteFile / ReadFile on a HID handle (CX receivers use these)
//   HidD_SetFeature <coll> <bytes> feature reports (Compx receivers use these)
// Hooks kernelbase.dll, not kernel32.dll: modern binaries resolve file APIs there directly.
// If spoof.js is loaded first, input reports are passed through its patchReply() before logging.

const hex = (p, n) => {
  try {
    return Array.from(new Uint8Array(p.readByteArray(n)), b => b.toString(16).padStart(2, '0')).join(' ');
  } catch (e) {
    return '?';
  }
};

const handles = {};  // HANDLE -> "cN" (top-level collection number from the device path)
const pending = {};  // OVERLAPPED* -> {h, buf, len}

function findExport(mod, name) {
  try { return Process.getModuleByName(mod).findExportByName(name); } catch (e) { return null; }
}

// Attach now, or as soon as the module gets loaded.
function hook(mod, name, callbacks) {
  const addr = findExport(mod, name);
  if (addr) return Interceptor.attach(addr, callbacks);
  const timer = setInterval(() => {
    const a = findExport(mod, name);
    if (a) { clearInterval(timer); Interceptor.attach(a, callbacks); }
  }, 20);
}

function inputReport(h, buf, len) {
  if (globalThis.patchReply) globalThis.patchReply(buf, len);
  send('R ' + h + ' ' + hex(buf, len));
}

const kb = 'kernelbase.dll';

hook(kb, 'CreateFileW', {
  onEnter(a) { this.path = a[0].readUtf16String(); },
  onLeave(r) {
    if (!this.path || !/hid#/i.test(this.path)) return;
    const m = this.path.match(/col0(\d)/i);
    handles[r.toString()] = 'c' + (m ? m[1] : '?');
  },
});

hook(kb, 'CloseHandle', { onEnter(a) { delete handles[a[0].toString()]; } });

hook(kb, 'WriteFile', {
  onEnter(a) {
    const h = handles[a[0].toString()];
    if (h) send('W ' + h + ' ' + hex(a[1], a[2].toInt32()));
  },
});

hook(kb, 'ReadFile', {
  onEnter(a) {
    const h = handles[a[0].toString()];
    if (!h) return;
    const req = { h, buf: a[1], len: a[2].toInt32() };
    if (a[4].isNull()) this.sync = req;
    else pending[a[4].toString()] = req;
  },
  onLeave() { if (this.sync) inputReport(this.sync.h, this.sync.buf, this.sync.len); },
});

for (const fn of ['GetOverlappedResult', 'GetOverlappedResultEx']) {
  hook(kb, fn, {
    onEnter(a) { this.ov = a[1].toString(); },
    onLeave(r) {
      const req = pending[this.ov];
      if (!req || !r.toInt32()) return;
      delete pending[this.ov];
      inputReport(req.h, req.buf, req.len);
    },
  });
}

for (const fn of ['HidD_SetFeature', 'HidD_GetFeature', 'HidD_SetOutputReport', 'HidD_GetInputReport']) {
  hook('hid.dll', fn, {
    onEnter(a) {
      this.buf = a[1];
      this.len = a[2].toInt32();
      this.h = handles[a[0].toString()] || '?';
      if (fn.includes('Set')) send(fn + ' ' + this.h + ' ' + hex(this.buf, this.len));
    },
    onLeave(r) {
      if (fn.includes('Get')) send(fn + ' ' + this.h + ' r=' + r + ' ' + hex(this.buf, this.len));
    },
  });
}

send('ready');
