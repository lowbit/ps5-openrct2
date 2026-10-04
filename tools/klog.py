"""Writes the console's kernel log (klogsrv on port 3232) to a file until stopped.

    uv run --no-project python tools/klog.py build/klog.txt
"""
import socket
import sys
import time

with open(sys.argv[1], "ab", buffering=0) as out:
    while True:
        try:
            with socket.create_connection(("192.168.0.90", 3232), timeout=10) as sock:
                sock.settimeout(None)
                while chunk := sock.recv(65536):
                    out.write(chunk)
        except OSError as e:
            out.write(f"\n[klog.py: {e}]\n".encode())
            time.sleep(5)
