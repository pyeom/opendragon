// Frida agent: let a renamed copy of OemDrv.exe (M901P-KS app) start while the installed one is
// running. It renames the MUTEX_COMPX_WIRELESS mutex and hides existing windows from FindWindow
// during the first seconds. Only use it when the installed app can't be closed: it makes the
// copy's own dialogs misbehave later on.

const T0 = Date.now();

for (const [mod, fn, wide, nameArg] of [
  ['kernelbase.dll', 'CreateMutexW', true, 2],
  ['kernel32.dll', 'CreateMutexA', false, 2],
  ['kernelbase.dll', 'CreateMutexExW', true, 1],
  ['kernelbase.dll', 'OpenMutexW', true, 2],
]) {
  let addr = null;
  try { addr = Process.getModuleByName(mod).findExportByName(fn); } catch (e) {}
  if (!addr) continue;
  Interceptor.attach(addr, {
    onEnter(args) {
      const p = args[nameArg];
      if (p.isNull()) return;
      const name = wide ? p.readUtf16String() : p.readAnsiString();
      if (name !== 'MUTEX_COMPX_WIRELESS') return;
      this.renamed = wide ? Memory.allocUtf16String(name + '_re') : Memory.allocAnsiString(name + '_re');
      args[nameArg] = this.renamed;
      send(fn + ' ' + name + ' -> ' + name + '_re');
    },
  });
}

for (const fn of ['FindWindowA', 'FindWindowW', 'FindWindowExA', 'FindWindowExW']) {
  const timer = setInterval(() => {
    let addr = null;
    try { addr = Process.getModuleByName('user32.dll').findExportByName(fn); } catch (e) {}
    if (!addr) return;
    clearInterval(timer);
    Interceptor.attach(addr, {
      onLeave(r) {
        if (!r.isNull() && Date.now() - T0 < 4000) r.replace(ptr(0));
      },
    });
  }, 10);
}
