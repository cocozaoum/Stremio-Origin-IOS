#!/usr/bin/env bash
# Builds an UNSIGNED iOS app (arm64) and packs it as build/ios/StremioOrigin.ipa; SideStore signs it when it installs it.
# Needs macOS with Xcode, cmake and python3 (GitHub's macos-14 runner has all three). Run tools/ios/apply.sh first.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; TP="$ROOT/third_party"
W="$ROOT/build/ios"; D="$W/deps"; S="$W/src"; mkdir -p "$W/tp/ass" "$W/obj" "$D" "$S"
MIN=14.0; SDK="$(xcrun --sdk iphoneos --show-sdk-path)"; NCPU="$(sysctl -n hw.ncpu)"
CC="xcrun --sdk iphoneos clang -arch arm64 -miphoneos-version-min=$MIN -isysroot $SDK"
CXX="xcrun --sdk iphoneos clang++ -arch arm64 -miphoneos-version-min=$MIN -isysroot $SDK"
AR="xcrun --sdk iphoneos ar"
CM=(-DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=$MIN -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$D")
fetch() { [ -f "$S/$2" ] || curl -fL --retry 3 -o "$S/$2" "$1"; }
unpack() { [ -d "$S/$2" ] || { mkdir -p "$S/$2"; tar -xf "$S/$1" -C "$S/$2" --strip-components 1; }; }

# ---- dependencies, once: mbedTLS + curl (as on Android), SDL2 ----
if [ ! -f "$D/lib/libmbedtls.a" ]; then echo "== mbedTLS"
  fetch https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.2/mbedtls-3.6.2.tar.bz2 mbedtls.tar.bz2; unpack mbedtls.tar.bz2 mbedtls
  cmake -S "$S/mbedtls" -B "$W/mbedtls-build" "${CM[@]}" -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DMBEDTLS_FATAL_WARNINGS=OFF -DUSE_SHARED_MBEDTLS_LIBRARY=OFF -DUSE_STATIC_MBEDTLS_LIBRARY=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON >/dev/null
  cmake --build "$W/mbedtls-build" -j"$NCPU" >/dev/null && cmake --install "$W/mbedtls-build" >/dev/null; fi
if [ ! -f "$D/lib/libcurl.a" ]; then echo "== curl"
  fetch https://curl.se/download/curl-8.11.1.tar.xz curl.tar.xz; unpack curl.tar.xz curl
  ( cd "$S/curl" && ./configure --host=arm-apple-darwin --prefix="$D" --disable-shared --enable-static --disable-dependency-tracking \
      --with-mbedtls="$D" --without-ssl --without-libpsl --without-nghttp2 --without-brotli --without-zstd --without-libidn2 --without-libssh2 \
      --disable-ldap --disable-ldaps --disable-manual --disable-docs --enable-websockets --disable-threaded-resolver --without-ca-bundle --without-ca-path \
      CC="xcrun --sdk iphoneos clang" CFLAGS="-arch arm64 -isysroot $SDK -miphoneos-version-min=$MIN -O2" LDFLAGS="-arch arm64 -isysroot $SDK" >/dev/null
    make -j"$NCPU" >/dev/null && make install >/dev/null ); fi
if [ ! -f "$D/lib/libSDL2.a" ]; then echo "== SDL2"
  fetch https://github.com/libsdl-org/SDL/releases/download/release-2.30.12/SDL2-2.30.12.tar.gz sdl.tar.gz; unpack sdl.tar.gz SDL
  cmake -S "$S/SDL" -B "$W/sdl-build" "${CM[@]}" -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF >/dev/null
  cmake --build "$W/sdl-build" -j"$NCPU" >/dev/null && cmake --install "$W/sdl-build" >/dev/null; fi
SDLMAIN="$D/lib/libSDL2main.a"
if [ ! -f "$SDLMAIN" ]; then $CC -O2 -I"$D/include/SDL2" -c "$S/SDL/src/main/uikit/SDL_uikit_main.c" -o "$W/SDL_uikit_main.o"; $AR rcs "$SDLMAIN" "$W/SDL_uikit_main.o"; fi

# ---- keys (optional: TMDB_API_KEY, TRAKT_CLIENT_ID in the environment) ----
python3 "$ROOT/tools/ios/genkeys.py" "$W/sbkeys.h"

# ---- third-party, once (as tools/phone/build.sh) ----
OPT="-O2 -fno-strict-aliasing -ffunction-sections -fdata-sections"
if [ ! -f "$W/tp/libtp.a" ]; then echo "== third-party"; TPQ="-w $OPT"
  FT="-DFT2_BUILD_LIBRARY -DFT_CONFIG_MODULES_H=<ftmodule-sb.h> -I$TP/config -I$TP/freetype/include"
  for f in base/ftbase base/ftinit base/ftsystem base/ftdebug base/ftbitmap base/ftglyph base/ftmm base/ftbbox base/ftsynth base/ftstroke gzip/ftgzip truetype/truetype sfnt/sfnt smooth/smooth autofit/autofit psnames/psnames pshinter/pshinter cff/cff psaux/psaux; do
    $CC $TPQ $FT -c "$TP/freetype/src/$f.c" -o "$W/tp/ft_$(basename $f).o"; done
  $CXX -w $OPT -std=c++11 -fno-exceptions -fno-rtti -DHAVE_FREETYPE=1 -DHAVE_PTHREAD=1 -I$TP/freetype/include -I$TP/harfbuzz/src -c "$TP/harfbuzz/src/harfbuzz.cc" -o "$W/tp/harfbuzz.o"
  for f in "$TP"/fribidi/lib/fribidi*.c; do $CC $TPQ -DHAVE_CONFIG_H -I$TP/config/fribidi -I$TP/fribidi/lib -c "$f" -o "$W/tp/fb_$(basename "${f%.c}").o"; done
  $CC $TPQ -c "$TP/cjson/cJSON.c" -o "$W/tp/cJSON.o"; $CC $TPQ -c "$TP/qrcodegen/qrcodegen.c" -o "$W/tp/qrcodegen.o"
  for f in "$TP"/webp/src/dec/*.c "$TP"/webp/src/dsp/*.c "$TP"/webp/src/utils/*.c; do case "$(basename "$f")" in *enc*|*writer*|*quant_levels_utils*|*_sse*|*_msa*|*_mips*) continue;; esac
    $CC $TPQ -DWEBP_REDUCE_CSP -I$TP/webp -c "$f" -o "$W/tp/webp_$(basename "${f%.c}").o"; done
  $AR rcs "$W/tp/libtp.a" "$W"/tp/*.o; fi
if [ ! -f "$W/tp/libass.a" ]; then echo "== libass"
  for f in "$TP"/libass/libass/*.c "$TP"/libass/libass/c/*.c; do $CC -w $OPT -std=gnu11 -DHAVE_CONFIG_H -I$TP/config/libass -I$TP/libass/libass -I$TP/freetype/include -I$TP/harfbuzz/src -I$TP/fribidi/lib -I$TP/config/fribidi -c "$f" -o "$W/tp/ass/$(basename "${f%.c}").o"; done
  $CC -w $OPT -DFT2_BUILD_LIBRARY -DFT_CONFIG_MODULES_H="<ftmodule-sb.h>" -I$TP/config -I$TP/freetype/include -c "$TP/freetype/src/base/fttype1.c" -o "$W/tp/ass/ft_fttype1.o"
  $AR rcs "$W/tp/libass.a" "$W"/tp/ass/*.o; fi

# ---- the app ----
echo "== app"; GITREV="$(git -C "$ROOT" rev-parse --short=10 HEAD 2>/dev/null || echo ios)"
CFLAGS="$OPT -std=c11 -D_GNU_SOURCE -I$W -DSB_GITREV=\"$GITREV\" -DSB_PHONE=1 -DSB_IOS=1 -DSB_APP_ID=\"app.stremioorigin.phone\" -Wno-everything \
  -I$D/include -I$D/include/SDL2 -DSB_REPO=\"android-phone\" -DSB_PKG_PREFIX=\"none-\" -DSB_PKG_SUFFIX=\".none\" \
  -I$ROOT/src -I$TP/freetype/include -I$TP/harfbuzz/src -I$TP/fribidi/lib -I$TP/config/fribidi -I$TP/cjson -I$TP/stb -I$TP/qrcodegen -I$TP/webp -I$TP/crypto -I$TP/libass"
OBJS=(); mkdir -p "$W/obj"
for f in "$TP"/crypto/*.c; do o="$W/tp/crypto_$(basename "${f%.c}").o"; [ -f "$o" ] || $CC -w $OPT -I$TP/crypto -c "$f" -o "$o"; OBJS+=("$o"); done
SCREENS=$(ls "$ROOT"/src/screens/*.c | grep -v -E '/(board|detail|cwmenu|lib|search|settings|login|livehub)\.c$')
CORE=$(ls "$ROOT"/src/core/*.c | grep -v -E '/(castlink)\.c$')
PLATSRC="$ROOT/src/platform/ios/plat_ios.m $ROOT/src/platform/ios/video_ios.m $ROOT/src/phone/ios/pplat_ios.m"
for f in $CORE "$ROOT"/src/ui/*.c $SCREENS "$ROOT/src/main.c" "$ROOT"/src/phone/*.c $PLATSRC; do
  o="$W/obj/$(echo "${f#$ROOT/src/}" | tr / _).o"; FL="$CFLAGS"; case "$f" in *.m) FL="${CFLAGS/-std=c11/} -fobjc-arc";; esac
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ -n "$(find "$ROOT/src" -name '*.h' -newer "$o" | head -1)" ]; then $CC $FL -c "$f" -o "$o"; fi; OBJS+=("$o"); done

APP="$W/StremioOrigin.app"; rm -rf "$APP" "$W/Payload"; mkdir -p "$APP"
$CXX -Wl,-dead_strip -ObjC -o "$APP/StremioOrigin" "${OBJS[@]}" "$W/tp/libass.a" "$W/tp/libtp.a" "$D/lib/libcurl.a" "$D/lib/libmbedtls.a" "$D/lib/libmbedx509.a" "$D/lib/libmbedcrypto.a" "$D/lib/libSDL2.a" "$SDLMAIN" \
  -framework UIKit -framework Foundation -framework CoreFoundation -framework OpenGLES -framework QuartzCore -framework AVFoundation -framework AudioToolbox -framework CoreAudio \
  -framework CoreMedia -framework CoreVideo -framework CoreGraphics -framework CoreMotion -framework CoreHaptics -framework CoreBluetooth -framework GameController -framework Metal \
  -framework MediaPlayer -framework MobileCoreServices -framework UserNotifications -framework SystemConfiguration -framework ImageIO -lz -liconv -lc++

# ---- package ----
cp -R "$ROOT/assets" "$APP/assets"; find "$APP/assets" -name .DS_Store -delete
sips -z 120 120 "$ROOT/assets/icon.png" --out "$APP/AppIcon60x60@2x.png" >/dev/null; sips -z 180 180 "$ROOT/assets/icon.png" --out "$APP/AppIcon60x60@3x.png" >/dev/null
cat > "$APP/Info.plist" <<'PL'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
 <key>CFBundleExecutable</key><string>StremioOrigin</string>
 <key>CFBundleIdentifier</key><string>app.stremioorigin.phone</string>
 <key>CFBundleName</key><string>Stremio Origin</string>
 <key>CFBundleDisplayName</key><string>Stremio Origin</string>
 <key>CFBundlePackageType</key><string>APPL</string>
 <key>CFBundleShortVersionString</key><string>1.1.0</string>
 <key>CFBundleVersion</key><string>1</string>
 <key>MinimumOSVersion</key><string>14.0</string>
 <key>UIDeviceFamily</key><array><integer>1</integer><integer>2</integer></array>
 <key>UIRequiredDeviceCapabilities</key><array><string>arm64</string><string>opengles-2</string></array>
 <key>UILaunchScreen</key><dict/>
 <key>UIRequiresFullScreen</key><true/>
 <key>UIViewControllerBasedStatusBarAppearance</key><true/>
 <key>UIStatusBarHidden</key><true/>
 <key>UIBackgroundModes</key><array><string>audio</string></array>
 <key>UISupportedInterfaceOrientations</key><array><string>UIInterfaceOrientationPortrait</string><string>UIInterfaceOrientationLandscapeLeft</string><string>UIInterfaceOrientationLandscapeRight</string></array>
 <key>UISupportedInterfaceOrientations~ipad</key><array><string>UIInterfaceOrientationPortrait</string><string>UIInterfaceOrientationPortraitUpsideDown</string><string>UIInterfaceOrientationLandscapeLeft</string><string>UIInterfaceOrientationLandscapeRight</string></array>
 <key>CFBundleIcons</key><dict><key>CFBundlePrimaryIcon</key><dict><key>CFBundleIconFiles</key><array><string>AppIcon60x60</string></array></dict></dict>
 <key>NSAppTransportSecurity</key><dict><key>NSAllowsArbitraryLoads</key><true/></dict>
 <key>NSLocalNetworkUsageDescription</key><string>To find TVs and devices on your network.</string>
 <key>NSBonjourServices</key><array><string>_googlecast._tcp</string></array>
 <key>LSApplicationQueriesSchemes</key><array><string>vlc</string><string>infuse</string><string>outplayer</string></array>
 <key>UIFileSharingEnabled</key><true/>
 <key>LSSupportsOpeningDocumentsInPlace</key><true/>
</dict></plist>
PL
( cd "$W" && mkdir Payload && cp -R StremioOrigin.app Payload/ && rm -f StremioOrigin.ipa && zip -qry StremioOrigin.ipa Payload )
ls -l "$W/StremioOrigin.ipa" | awk '{printf "%s  %.1f MB\n", $NF, $5/1048576}'
