// Frida agent (load before hidhook.js): rewrite the battery level in Compx replies
// (input report 09 04 ...) so you can see how the vendor app displays a given raw value.
// The level is set from session.py through a control file.

let spoof = null;

function onControl(msg) {
  spoof = msg.value === null ? null : msg.value;
  send('spoof = ' + spoof);
  recv('ctl', onControl);
}
recv('ctl', onControl);

globalThis.patchReply = function (buf, len) {
  if (spoof === null || len < 17) return;
  if (buf.readU8() !== 0x09 || buf.add(1).readU8() !== 0x04) return;
  buf.add(6).writeU8(spoof);
  let sum = 0;
  for (let i = 0; i < 16; i++) sum = (sum + buf.add(i).readU8()) & 0xff;
  buf.add(16).writeU8((0x55 - sum) & 0xff);
  send('patched battery -> ' + spoof);
};
