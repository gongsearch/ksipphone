#!/bin/sh
# make-manifests.sh <version> <path to KSipPhone-<version>-setup.exe> [date]
# Writes the winget manifests to manifests/g/gongsearch/KSipPhone/<version>/ (layout of microsoft/winget-pkgs).
set -e
V="$1"; EXE="$2"; D="${3:-$(date +%F)}"
SHA=$(sha256sum "$EXE" | cut -d' ' -f1 | tr a-f A-F)
OUT="$(dirname "$0")/manifests/g/gongsearch/KSipPhone/$V"; mkdir -p "$OUT"
for f in "$(dirname "$0")"/*.yaml.in; do
  sed -e "s/@VERSION@/$V/g" -e "s/@SHA256@/$SHA/g" -e "s/@DATE@/$D/g" "$f" > "$OUT/$(basename "$f" .in)"
done
echo "manifests in $OUT (sha256 $SHA)"
