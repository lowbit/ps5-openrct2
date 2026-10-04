"""Uploads the built title folder to /data/homebrew on the PS5, or prints a file from the console.

    uv run --no-project python tools/deploy.py               upload dist/PPSA99728
    uv run --no-project python tools/deploy.py --rct2 DIR    also upload an RCT2 folder as rct2/
    uv run --no-project python tools/deploy.py cat PATH      print a console file (e.g. the log)

Uses the standalone PS5Upload engine from dev/ps5/tools/push_ffpkg.py, so the PS5Upload desktop
app can stay closed. The PS5Upload helper payload must be running on the console. Uploading adds
and replaces files but never deletes. --rct2 is for testing until the title can import the game
data itself; only Data, ObjData, Scenarios, Tracks and Landscapes are sent.
"""
import argparse
import hashlib
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent / "tools"))
import ps5  # noqa: E402
import push_ffpkg as push  # noqa: E402

TITLE = "PPSA99728"
APP = ROOT / "dist" / TITLE
DEST = f"/data/homebrew/{TITLE}"
RCT2_FOLDERS = ("Data", "ObjData", "Scenarios", "Tracks", "Landscapes")


def transfer(source, dest):
    digest = hashlib.md5(dest.encode())
    for f in sorted(source.rglob("*")):
        if f.is_file():
            digest.update(f"{f.relative_to(source)}|{f.stat().st_size}|{f.stat().st_mtime_ns}".encode())
    r = push.http("POST", "/api/transfer/dir",
                  {"addr": push.XFER, "tx_id": digest.hexdigest(), "dest_root": dest, "src_dir": str(source)})
    job = r.get("job_id")
    if not job:
        sys.exit(f"transfer did not start: {r}")
    while True:
        j = push.http("GET", f"/api/jobs/{job}")
        status = j.get("status") or j.get("job", {}).get("status")
        if status in ("done", "failed"):
            break
        time.sleep(2)
    if status != "done":
        sys.exit(f"upload failed: {j}")


def upload(rct2):
    if not (APP / "eboot.bin").exists():
        sys.exit(f"{APP} is missing: run make package first")
    push.LOG = ROOT / "build" / "deploy.log"
    push.LOG.parent.mkdir(exist_ok=True)
    push.start_engine()
    push.ensure_helper()

    transfer(APP, DEST)
    if rct2:
        missing = [name for name in RCT2_FOLDERS if not (rct2 / name).is_dir()]
        if missing:
            sys.exit(f"{rct2} lacks {', '.join(missing)}")
        for name in RCT2_FOLDERS:
            transfer(rct2 / name, f"{DEST}/rct2/{name}")
    names = sorted(e["name"] for e in ps5.list_dir(DEST)["entries"])
    print(f"uploaded to {DEST}: {', '.join(names)}")


def cat(path):
    sys.stdout.buffer.write(ps5.read(path))


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "cat":
        cat(sys.argv[2])
        sys.exit()
    ap = argparse.ArgumentParser(usage=__doc__)
    ap.add_argument("--rct2", type=Path, help="RCT2 folder to upload as rct2/")
    upload(ap.parse_args().rct2)
