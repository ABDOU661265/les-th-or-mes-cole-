#!/usr/bin/env bash
# build.sh — construit l'application Les Théorèmes (serveur + interface)
#
#   ./tools/build.sh            -> binaire pour la machine courante
#   ./tools/build.sh windows    -> Les-Theoremes.exe (64 bits, sans installation)
#   ./tools/build.sh installer  -> archive Windows (.zip) prête à distribuer
#
# Prérequis : gcc (Linux/macOS) ou zig (compilation croisée Windows),
#             python3 pour la génération des ressources embarquées.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/server/src"
VENDOR="$ROOT/server/vendor"
DIST="$ROOT/release"
TARGET="${1:-native}"
VERSION="1.0.0"

PY=python3
command -v python3 >/dev/null 2>&1 || PY=python

echo "== Les Théorèmes $VERSION — cible : $TARGET =="
mkdir -p "$DIST"
"$PY" "$ROOT/tools/gen_assets.py" "$ROOT/web" "$SRC/assets.c"

SOURCES=(
  "$SRC/main.c" "$SRC/util.c" "$SRC/db.c" "$SRC/auth.c"
  "$SRC/http.c" "$SRC/api.c" "$SRC/assets.c"
  "$VENDOR/cJSON.c"
)

if [ "$TARGET" = "windows" ] || [ "$TARGET" = "installer" ]; then
  ZIG="${ZIG:-zig}"
  command -v "$ZIG" >/dev/null 2>&1 || { echo "zig est requis pour la compilation Windows"; exit 1; }
  echo "-- compilation Windows (64 bits)"
  mkdir -p "$ROOT/.work"
  if [ ! -f "$ROOT/.work/sqlite3.obj" ] || [ "$VENDOR/sqlite3.c" -nt "$ROOT/.work/sqlite3.obj" ]; then
    echo "-- sqlite (peut prendre une minute)"
    "$ZIG" cc -target x86_64-windows-gnu -O1 \
      -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=1 \
      -c "$VENDOR/sqlite3.c" -o "$ROOT/.work/sqlite3.obj"
  fi
  ( cd "$ROOT/packaging" && "$ZIG" rc /fo "$ROOT/.work/app.res" app.rc )
  OUT="$DIST/Les-Theoremes.exe"
  "$ZIG" cc -target x86_64-windows-gnu -O2 -Wl,--subsystem,windows \
    -I "$VENDOR" -I "$SRC" "${SOURCES[@]}" \
    "$ROOT/.work/sqlite3.obj" "$ROOT/.work/app.res" \
    -o "$OUT" -lws2_32 -lshell32 -lole32 -luuid -ladvapi32 -lgdi32 -luser32 -lcomdlg32
  rm -f "$DIST"/*.pdb "$DIST"/*.lib
  echo "-- $OUT"
  if [ "$TARGET" = "installer" ]; then
    PKG="$DIST/Les-Theoremes-Windows"
    rm -rf "$PKG" && mkdir -p "$PKG"
    cp "$OUT" "$PKG/"
    cp "$ROOT/../README.md" "$PKG/" 2>/dev/null || true
    cp "$ROOT/../GUIDE-UTILISATION.txt" "$PKG/" 2>/dev/null || true
    if command -v zip >/dev/null 2>&1; then
      rm -f "$DIST/Les-Theoremes-Windows-$VERSION.zip"
      (cd "$DIST" && zip -q -r "Les-Theoremes-Windows-$VERSION.zip" "Les-Theoremes-Windows")
      echo "-- $DIST/Les-Theoremes-Windows-$VERSION.zip"
    fi
    rm -rf "$PKG"
  fi
  exit 0
fi

# --- compilation locale (développement) ---
CC="${CC:-gcc}"
$CC -std=c99 -D_GNU_SOURCE -O2 -Wall -I "$VENDOR" -I "$SRC" \
  "${SOURCES[@]}" "$VENDOR/sqlite3.c" \
  -o "$DIST/les-theoremes" -lpthread -lm
echo "-- $DIST/les-theoremes"
echo "   lancer : ./dist/les-theoremes --datadir . --console"
