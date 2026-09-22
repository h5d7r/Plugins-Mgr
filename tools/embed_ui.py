#!/usr/bin/env python3
"""Embed source/ui.html into source/html.h as a C string.

Usage:
    python3 tools/embed_ui.py source/ui.html source/html.h
"""

import sys


def embed(src, dst):
    with open(src, "r", encoding="utf-8") as f:
        text = f.read()
    if not text.endswith("\n"):
        text += "\n"
    lines = text.split("\n")[:-1]
    out = []
    out.append("/* Auto-generated from source/ui.html - do not edit by hand. */")
    out.append("/* Regenerate: python3 tools/embed_ui.py source/ui.html source/html.h */")
    out.append("static const char HTML_PAGE[] =")
    for line in lines:
        escaped = line.replace("\\", "\\\\").replace('"', '\\"')
        out.append('  "%s\\n"' % escaped)
    out.append("  ;")
    with open(dst, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    embed(sys.argv[1], sys.argv[2])