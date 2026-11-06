#!/usr/bin/env python3
"""gen_assets.py — embarque les fichiers de app/web dans l'executable (C).

Usage : python3 tools/gen_assets.py app/web app/server/src/assets.c
Le fichier genere n'est jamais edite a la main.
"""
import os
import sys

MIME = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".json": "application/json; charset=utf-8",
    ".webmanifest": "application/manifest+json",
    ".svg": "image/svg+xml",
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".ico": "image/x-icon",
}

ORDER = ["index.html", "styles.css", "i18n.js", "app.js", "manifest.webmanifest", "logo.png", "favicon.ico"]


def escape_c(data: bytes) -> str:
    out = []
    for i, b in enumerate(data):
        if i % 20 == 0:
            out.append("\n    ")
        out.append("0x%02x," % b)
    return "".join(out)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    webdir, target = sys.argv[1], sys.argv[2]

    files = [f for f in ORDER if os.path.isfile(os.path.join(webdir, f))]
    for name in sorted(os.listdir(webdir)):
        if name not in files and os.path.isfile(os.path.join(webdir, name)):
            files.append(name)

    lines = [
        "/* Fichier genere par tools/gen_assets.py — ne pas editer. */",
        '#include "app.h"',
        "",
    ]
    entries = []
    for name in files:
        path = os.path.join(webdir, name)
        with open(path, "rb") as fh:
            data = fh.read()
        ext = os.path.splitext(name)[1].lower()
        mime = MIME.get(ext, "application/octet-stream")
        var = "asset_" + "".join(c if c.isalnum() else "_" for c in os.path.splitext(name)[0])
        lines.append("static const unsigned char %s[] = {%s\n};" % (var, escape_c(data)))
        lines.append("")
        entries.append('    { "%s", "%s", %s, %d, "" }' % (name, mime, var, len(data)))

    lines.append("asset_t g_assets[] = {")
    lines.extend(e + "," for e in entries)
    lines.append("};")
    lines.append("const int g_assets_count = %d;" % len(entries))
    lines.append("")

    os.makedirs(os.path.dirname(os.path.abspath(target)), exist_ok=True)
    with open(target, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))
    print("assets: %d fichiers -> %s" % (len(entries), target))
    return 0


if __name__ == "__main__":
    sys.exit(main())
