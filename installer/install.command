#!/bin/zsh
# HarmEQ installer: double-click to install the VST3 and the standalone app on this Mac.
# Copies the bundles that sit next to this script, then strips the quarantine flag so Gatekeeper
# doesn't block the (ad-hoc signed, not notarised) binaries.
#
# First time: macOS may refuse to open this script because it was downloaded. Right-click it,
# choose Open, and confirm.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
if [ ! -d "$HERE/HarmEQ.vst3" ]; then
  echo "ERROR: HarmEQ.vst3 not found next to this script ($HERE)" >&2
  exit 1
fi

VST3_DEST="$HOME/Library/Audio/Plug-Ins/VST3"
APP_DEST="/Applications"
[ -w "$APP_DEST" ] || APP_DEST="$HOME/Applications"
mkdir -p "$VST3_DEST" "$APP_DEST"

echo "Installing HarmEQ from: $HERE"

rm -rf "$VST3_DEST/HarmEQ.vst3"
cp -R "$HERE/HarmEQ.vst3" "$VST3_DEST/"
xattr -dr com.apple.quarantine "$VST3_DEST/HarmEQ.vst3" 2>/dev/null || true
echo "  VST3       -> $VST3_DEST/HarmEQ.vst3"

if [ -d "$HERE/HarmEQ.app" ]; then
  rm -rf "$APP_DEST/HarmEQ.app"
  cp -R "$HERE/HarmEQ.app" "$APP_DEST/"
  xattr -dr com.apple.quarantine "$APP_DEST/HarmEQ.app" 2>/dev/null || true
  echo "  Standalone -> $APP_DEST/HarmEQ.app"
fi

# Early test builds also installed an Audio Unit; HarmEQ is VST3-only now, so remove it to avoid
# a duplicate entry in Live.
OLD_AU="$HOME/Library/Audio/Plug-Ins/Components/HarmEQ.component"
if [ -d "$OLD_AU" ]; then
  rm -rf "$OLD_AU"
  echo "  Removed old Audio Unit: $OLD_AU"
fi

echo "Done. Rescan plug-ins in Live (Settings > Plug-Ins) if it was running."
