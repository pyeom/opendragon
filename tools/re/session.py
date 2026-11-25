"""Start a vendor app under Frida and log its HID traffic.

    python session.py APP.exe LOG [--ctl FILE] AGENT.js [AGENT.js ...]

Agents are concatenated in order (put spoof.js before hidhook.js). With --ctl, the file is polled:
write a number into it to spoof the battery level, 'none' to stop spoofing, 'quit' to detach.
"""
import argparse
import os
import time

import frida

ap = argparse.ArgumentParser()
ap.add_argument("exe")
ap.add_argument("log")
ap.add_argument("--ctl")
ap.add_argument("agents", nargs="+")
args = ap.parse_args()

source = "\n".join(open(p, encoding="utf-8").read() for p in args.agents)
log = open(args.log, "w", encoding="utf-8")
t0 = time.time()

pid = frida.spawn([args.exe], cwd=os.path.dirname(os.path.abspath(args.exe)))
session = frida.attach(pid)
gone = []
session.on("detached", lambda *a: gone.append(a))
script = session.create_script(source)


def on_message(msg, _data):
    text = msg.get("payload") if msg["type"] == "send" else str(msg)
    log.write(f"{time.time() - t0:8.3f} {text}\n")
    log.flush()


script.on("message", on_message)
script.load()
frida.resume(pid)
print(f"pid {pid}, logging to {args.log}; close the app or write 'quit' to the ctl file to stop")

last = None
while not gone:
    if args.ctl:
        try:
            value = open(args.ctl).read().strip()
        except OSError:
            value = "none"
        if value == "quit":
            break
        if value != last:
            script.post({"type": "ctl", "value": None if value == "none" else int(value)})
            last = value
    time.sleep(0.3)

if not gone:
    session.detach()
