#!/bin/sh
# empreinte.sh — calcule l'empreinte SHA-256 de l'executable.
# A joindre a toute demande de correction de faux positif antivirus
# (Microsoft, AVG, Avast, ESET, Kaspersky, Bitdefender).
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
EXE="$ROOT/release/Les-Theoremes.exe"
[ -f "$EXE" ] || { echo "compiler d'abord : tools/build.sh installer"; exit 1; }
echo "fichier : $EXE"
echo "taille  : $(wc -c < "$EXE") octets"
if command -v sha256sum >/dev/null 2>&1; then
  sha256sum "$EXE"
else
  shasum -a 256 "$EXE"
fi
echo
echo "Sous Windows : certutil -hashfile Les-Theoremes.exe SHA256"
