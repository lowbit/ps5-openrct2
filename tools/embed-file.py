#!/usr/bin/env python3
"""Writes a C header that holds a file's bytes as a static array: embed-file.py <file> <name> <header>.
The header is left alone when it would not change, so nothing that includes it is rebuilt."""
import sys
from pathlib import Path

source, name, header = sys.argv[1:]
data = Path(source).read_bytes()
lines = [f"/* Generated from {Path(source).name} by tools/embed-file.py. */",
         f"static const unsigned char {name}[] = {{"]
for i in range(0, len(data), 20):
    lines.append("    " + ", ".join(str(b) for b in data[i:i + 20]) + ",")
lines.append("};")
text = "\n".join(lines) + "\n"
target = Path(header)
if not target.exists() or target.read_text() != text:
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, newline="\n")
