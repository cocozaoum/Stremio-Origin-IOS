#!/bin/bash
# Opens the phone app's Mac preview as a real Mac app (in front, in the Dock), sized as a device.
# Usage: tools/phone/mac-preview.sh [phone|tablet|tablet-wide|phone-wide]
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
case "${1:-tablet}" in
  phone)       W=405x900;  D=2 ;;
  phone-wide)  W=900x405;  D=2 ;;
  tablet)      W=560x896;  D=1.4 ;;   # a 10-inch tablet, upright, in a window that fits a laptop's screen
  tablet-wide) W=896x560;  D=1.4 ;;
  *) echo "usage: $0 [phone|tablet|tablet-wide|phone-wide]"; exit 2 ;;
esac
APP="$ROOT/build/phone-mac/Stremio Origin Preview.app"
mkdir -p "$APP/Contents/MacOS"
cat > "$APP/Contents/Info.plist" <<P
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>run</string>
<key>CFBundleIdentifier</key><string>app.stremioorigin.preview</string>
<key>CFBundleName</key><string>Stremio Origin Preview</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
P
printf '#!/bin/bash\ncd "%s"\nexport SB_SHOW=1 SB_ASSETS=assets SB_WINDOW=%s SB_DENSITY=%s\nexec "%s/build/phone-mac/stremioorigin-phone"\n' "$ROOT" "$W" "$D" "$ROOT" > "$APP/Contents/MacOS/run"
chmod +x "$APP/Contents/MacOS/run"
open -n "$APP"
