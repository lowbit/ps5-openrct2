"""Sends the RollerCoaster Tycoon 2 game folder (or a ZIP file of it, or the GOG installer) to the
import screen, the way its web page does, and prints what the console said.

    uv run --no-project python tools/send.py http://192.168.0.90:9666/ path/to/rct2
    uv run --no-project python tools/send.py ADDRESS FOLDER --only Data/g1.dat Data/css1.dat
    uv run --no-project python tools/send.py ADDRESS game.zip
    uv run --no-project python tools/send.py ADDRESS setup_rollercoaster_tycoon_2_....exe

The screen shows the address. Only files below Data, ObjData, Scenarios, Tracks and Landscapes
are sent; --only limits that to the paths given (relative to the folder). An installer's .exe is
sent with the .bin files next to it that share its name. Used with test/console-import.cfg for
hands-free tests.
"""
import argparse
import http.client
import json
import time
import urllib.parse
from pathlib import Path

GAME_FOLDERS = {"data", "objdata", "scenarios", "tracks", "landscapes"}


def request(address, method, path, body=None):
    url = urllib.parse.urlsplit(address)
    connection = http.client.HTTPConnection(url.hostname, url.port, timeout=60)
    connection.request(method, path, body=body)
    response = connection.getresponse()
    reply = response.read().decode(errors="replace")
    connection.close()
    return response.status, reply


def game_path(relative):
    parts = relative.split("/")
    for start, part in enumerate(parts[:-1]):
        if part.lower() in GAME_FOLDERS:
            return "/".join(parts[start:])
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("address", help="the address the screen shows")
    ap.add_argument("source", type=Path, help="the game folder or a ZIP file of it")
    ap.add_argument("--only", nargs="+", help="send only these paths inside the folder")
    ap.add_argument("--wait", type=int, default=60, help="seconds to wait for the screen to answer")
    args = ap.parse_args()

    deadline = time.time() + args.wait
    while True:
        try:
            request(args.address, "GET", "/status")
            break
        except OSError:
            if time.time() > deadline:
                raise SystemExit("the screen does not answer")
            time.sleep(2)

    if args.source.suffix.lower() == ".exe":
        parts = sorted(args.source.parent.glob(args.source.stem + "-*.bin"))
        files = [(file.name, file) for file in [args.source, *parts]]
    elif args.source.is_file():
        files = [(args.source.name, args.source)]
    else:
        files = []
        for file in sorted(args.source.rglob("*")):
            relative = file.relative_to(args.source).as_posix()
            path = game_path(relative)
            if file.is_file() and path and (not args.only or relative in args.only):
                files.append((path, file))
    total = sum(file.stat().st_size for _, file in files)
    started = time.time()
    sent = 0
    for index, (path, file) in enumerate(files):
        try:
            status, reply = request(args.address, "PUT", "/upload/" + urllib.parse.quote(path), file.read_bytes())
        except OSError as error:
            status, reply = 0, str(error)
        sent += file.stat().st_size
        if status != 200:
            print(f"{path}: {status} {reply}")
        elif index % 100 == 0 or index == len(files) - 1:
            seconds = time.time() - started
            print(f"{index + 1}/{len(files)} files, {sent >> 20} of {total >> 20} MB, "
                  f"{sent / max(seconds, 0.001) / 1048576:.1f} MB/s", flush=True)
    request(args.address, "POST", "/done")
    time.sleep(3)
    try:
        _, reply = request(args.address, "GET", "/status")
        for item in json.loads(reply)["notes"]:
            print(("ok   " if item["ok"] else "fail ") + item["text"])
    except OSError:
        print("the screen has closed (the game is starting)")


if __name__ == "__main__":
    main()
