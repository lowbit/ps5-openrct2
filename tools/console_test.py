"""Runs the deployed title on the PS5 with a test plan and collects what happened.

    uv run --no-project --with pillow python tools/console_test.py test/console-boot.cfg [--timeout S]

Writes the plan to the title folder as test.cfg (plus a capture port), launches PPSA99728 with the
rct2launch payload (tools/launcher, loaded through the Payload Manager; through the PS5Upload
engine's launch route when the Payload Manager does not answer), pulls the frames the plan
captures from the running title over TCP and saves them as PNG under build/test/run/, receives the
title's log over the same connection when it exits (build/test/run/openrct2.log), prints it and
removes test.cfg again. If the title dies before that, the log is still in the title folder:
tools/deploy.py cat /data/homebrew/PPSA99728/openrct2.log. The PS5Upload helper must be running.
"""
import argparse
import base64
import socket
import struct
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent / "tools"))
import ps5  # noqa: E402
import push_ffpkg as push  # noqa: E402

TITLE = "PPSA99728"
PLDMGR = f"http://{ps5.PS5_IP}:8084"
LAUNCHER = ROOT / "build" / "launcher" / "rct2launch.elf"
LAUNCHER_DIR = "/data/pldmgr/payloads/rct2launch"
TEST_PLAN = f"/data/homebrew/{TITLE}/test.cfg"
CAPTURE_PORT = 9119
OUT = ROOT / "build" / "test" / "run"
LOG = "openrct2.log"


def get(url, timeout=10):
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return r.read().decode(errors="replace")


def write_remote(path, data):
    r = push.http("POST", "/api/ps5/fs/write-bytes",
                  {"addr": push.XFER, "path": path, "bytes_b64": base64.b64encode(data).decode()})
    if "http_error" in r:
        sys.exit(f"writing {path} failed: {r}")


def install_launcher():
    data = LAUNCHER.read_bytes()
    try:
        if any(e["name"] == LAUNCHER.name and e["size"] == len(data)
               for e in ps5.list_dir(LAUNCHER_DIR)["entries"]):
            return
    except Exception:
        pass
    push.http("POST", "/api/ps5/fs/mkdir", {"addr": push.XFER, "path": LAUNCHER_DIR})
    write_remote(f"{LAUNCHER_DIR}/{LAUNCHER.name}", data)


def receive_exactly(sock, size):
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(min(1 << 20, size - len(data)))
        if not chunk:
            raise ConnectionError("capture stream ended early")
        data += chunk
    return bytes(data)


def pull_captures(seen):
    from PIL import Image

    try:
        sock = socket.create_connection((ps5.PS5_IP, CAPTURE_PORT), timeout=2)
    except OSError:
        return
    sock.settimeout(30)
    try:
        while True:
            (name_length,) = struct.unpack("<I", receive_exactly(sock, 4))
            if not name_length:
                break
            name = receive_exactly(sock, name_length).decode()
            (size,) = struct.unpack("<Q", receive_exactly(sock, 8))
            data = receive_exactly(sock, size)
            seen.add(name)
            if name == LOG:
                (OUT / name).write_bytes(data)
                continue
            frame, dimensions = name.split("-")
            width, height = (int(v) for v in dimensions.split("x"))
            Image.frombuffer("RGBA", (width, height), data, "raw", "RGBA", 0, 1).convert("RGB").save(OUT / f"{frame}.png")
            print(f"captured {frame}", flush=True)
    except (OSError, ConnectionError) as e:
        print(f"capture pull interrupted: {e}", flush=True)
    finally:
        sock.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("plan")
    ap.add_argument("--timeout", type=int, default=180)
    args = ap.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob("*"):
        old.unlink()
    push.LOG = ROOT / "build" / "deploy.log"
    push.start_engine()
    install_launcher()
    plan = Path(args.plan).read_text().rstrip("\n") + f"\nserve {CAPTURE_PORT}\n"
    write_remote(TEST_PLAN, plan.encode())

    try:
        print("launch:", get(f"{PLDMGR}/loadpayload:{LAUNCHER.name}", timeout=30).strip()[:200], flush=True)
    except OSError as e:
        # The Payload Manager's port can stay dead after rest mode; the engine launches the title
        # too, but does not close a running one first.
        print(f"payload manager unreachable ({e}); launching through the engine", flush=True)
        print("launch:", push.http("POST", "/api/ps5/app/launch", {"addr": push.XFER, "title_id": TITLE}), flush=True)
    seen = set()
    deadline = time.time() + args.timeout
    try:
        while time.time() < deadline and LOG not in seen:
            pull_captures(seen)
            time.sleep(1)
    finally:
        push.http("POST", "/api/ps5/fs/delete", {"addr": push.XFER, "path": TEST_PLAN})

    log = OUT / LOG
    if not log.exists():
        try:
            log.write_bytes(ps5.read(f"/data/homebrew/{TITLE}/{LOG}"))
            print("(log read from the title folder: the title did not exit by itself)")
        except Exception as e:
            print(f"no log: {e}")
    if log.exists():
        print("\n".join(log.read_text(errors="replace").splitlines()[-80:]))
    print(f"\nexited={LOG in seen} captures={sorted(seen - {LOG})}")


if __name__ == "__main__":
    main()
