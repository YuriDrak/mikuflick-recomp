#!/bin/sh
# Safely extract a user-supplied Miku Flick/02 IPA into extracted/ipa and
# dump basic binary metadata into work/. Run from the project root:
#   sh tools/extract_ipa.sh "com.sega.MikuFlick02-iOS5.0-(Clutch-2.0.4).ipa"
# Requires: unzip, gcc. The original IPA is never modified.
set -eu
IPA="${1:?usage: $0 <file.ipa>}"
ROOT="$(pwd)"
mkdir -p "$ROOT/work" "$ROOT/build" "$ROOT/extracted/ipa"

# Refuse archives containing absolute or parent-relative paths.
unzip -Z1 "$IPA" > "$ROOT/work/ipa_names.txt"
if grep -qE '^/|(^|/)\.\.(/|$)' "$ROOT/work/ipa_names.txt"; then
    echo "unsafe path in archive, aborting" >&2; exit 1
fi
unzip -l "$IPA" > "$ROOT/work/ipa_listing.txt"
unzip -q -o "$IPA" -d "$ROOT/extracted/ipa"

APP="$(ls -d "$ROOT"/extracted/ipa/Payload/*.app | head -1)"
BIN="$APP/$(basename "$APP" .app)"

gcc -O2 -Wall -o "$ROOT/build/machoinfo" "$ROOT/tools/machoinfo/machoinfo.c" -lm
"$ROOT/build/machoinfo" "$BIN" > "$ROOT/work/macho_loadcmds.txt"

# Symbol string table (offsets taken from LC_SYMTAB in machoinfo output).
STROFF=$(sed -n 's/.*stroff 0x\([0-9a-f]*\) strsize \([0-9]*\).*/\1/p' "$ROOT/work/macho_loadcmds.txt")
STRSZ=$(sed -n 's/.*stroff 0x[0-9a-f]* strsize \([0-9]*\).*/\1/p' "$ROOT/work/macho_loadcmds.txt")
tail -c +$((0x$STROFF + 1)) "$BIN" | head -c "$STRSZ" | tr '\0' '\n' | grep -v '^$' > "$ROOT/work/symbols.txt"

echo "app:     $APP"
echo "binary:  $BIN"
grep -E 'cputype|cryptid' "$ROOT/work/macho_loadcmds.txt"
echo "symbols: $(wc -l < "$ROOT/work/symbols.txt") names -> work/symbols.txt"
