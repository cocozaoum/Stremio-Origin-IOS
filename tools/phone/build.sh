#!/bin/bash
# The formatting is rough and I'm not going to bother. Pull requests that fix it are welcome; complaints without one help neither me nor the end user.
# Builds the phone app's native part:
#   tools/phone/build.sh mac            -> build/phone-mac/stremioorigin-phone   (the Mac preview, phone-sized)
#   tools/phone/build.sh arm64-v8a      -> build/phone-arm64-v8a/libmain.so     (Android; tools/phone/build-deps.sh first)
#   tools/phone/build.sh armeabi-v7a    -> build/phone-armeabi-v7a/libmain.so
# The TV app itself (src/core, src/ui, src/screens, main.c) with the phone's part in src/phone.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"; TP="$ROOT/third_party"
T="${1:-mac}"; B="$ROOT/build/phone-$T"; mkdir -p "$B/tp/ass" "$B/obj"
PRIV="$HOME/stremiobrew-build"; NDK="${NDK:-$PRIV/android/sdk/ndk/27.2.12479018}"; API="${API:-24}"
OPT="-O2 -gline-tables-only -fno-strict-aliasing -ffunction-sections -fdata-sections -ffile-prefix-map=$ROOT=/app"   # (line tables: a crash or a freeze named to the file and line, from the kept unstripped copy; the shipped library is stripped)
case "$T" in
  mac) MACSDL="${MACSDL:-$PRIV/mac/sdl2}"; CC=cc; CXX=c++; AR=ar
    TFLAGS="-DSB_MAC=1 -I$MACSDL/include -I$MACSDL/include/SDL2"; LIBS="$($MACSDL/bin/sdl2-config --static-libs) -Wl,-framework,OpenGL -Wl,-framework,AVFoundation -Wl,-framework,CoreMedia -Wl,-framework,CoreVideo -Wl,-framework,Foundation -Wl,-framework,QuartzCore -lcurl -liconv -lz"   # (AVFoundation: the preview plays the streams)
    PLATSRC="$ROOT/src/platform/mac/plat_mac.c $ROOT/src/platform/mac/mem_mac.c $ROOT/src/platform/mac/video_mac.c $ROOT/src/platform/mac/video_mac_av.m $ROOT/src/phone/mac/pplat_mac.c" ;;
  arm64-v8a|armeabi-v7a) TRIPLE=$([ "$T" = arm64-v8a ] && echo aarch64-linux-android$API || echo armv7a-linux-androideabi$API)
    BIN="$NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin"; CC="$BIN/clang --target=$TRIPLE -fPIC"; CXX="$BIN/clang++ --target=$TRIPLE -fPIC"; AR="$BIN/llvm-ar"
    D="$B/deps"; [ -f "$D/lib/libcurl.so" ] || { echo "run tools/phone/build-deps.sh $T first"; exit 1; }
    TFLAGS="-DSB_ANDROID=1 -I$D/include -DSB_REPO=\"android-phone\" -DSB_PKG_PREFIX=\"stremio-origin-phone-\" -DSB_PKG_SUFFIX=\"-$T.apk\""; LIBS="-L$D/lib -lSDL2 -lcurl -lGLESv2 -lEGL -llog -landroid -lz -lm -ldl -static-libstdc++"
    PLATSRC="$ROOT/src/phone/android/plat_phone_android.c" ;;   # (updates: stremio-origin-phone-<x.y.z>-<abi>.apk from the stremio-origin/android-phone releases, the name tools/phone/apk.sh gives)
  *) echo "unknown target $T"; exit 1 ;;
esac
# a phone's motion runs on the real time between frames (60, 90 or 120 a second): a merge from main that brings back
# the TV's 60 Hz clock alone (dt snapped to 16.7 ms) makes every animation run up to twice as fast on a 120 Hz phone
grep -q "SDL_GetPerformanceCounter(); if (pc0) dt" "$ROOT/src/main.c" || { echo "src/main.c: the phone's real-time frame clock (#ifdef SB_PHONE before the 60 Hz snap) is missing"; exit 1; }
GITREV=$(git -C "$ROOT" rev-parse --short=10 HEAD 2>/dev/null || echo unknown)$(git -C "$ROOT" diff --quiet HEAD -- src 2>/dev/null || echo "+$(git -C "$ROOT" diff HEAD -- src | shasum | cut -c1-6)")   # (uncommitted changes: their own fingerprint, so each test build keeps its own debug copy)   # (the build named in crash reports: its unstripped libraries are kept under it by apk.sh)
CFLAGS="$OPT -std=c11 -D_GNU_SOURCE -I$B -DSB_GITREV=\"$GITREV\" -DSB_PHONE=1 -DSB_APP_ID=\"app.stremioorigin.phone\" -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers $TFLAGS -I$ROOT/src -I$TP/freetype/include -I$TP/harfbuzz/src -I$TP/fribidi/lib -I$TP/config/fribidi -I$TP/cjson -I$TP/stb -I$TP/qrcodegen -I$TP/webp -I$TP/crypto -I$TP/libass"

# ---- built-in keys ------------------------------------------------------------------------
# The app's own keys (Trakt, TMDB) come from a private file outside the repo, KEYS=~/stremiobrew-build/keys.env
# by default (TRAKT_CLIENT_ID=..., TMDB_API_KEY=...; Trakt's app signs in with PKCE: no secret). They go into build/phone-<target>/sbkeys.h, scrambled so they are
# not plain text in the binary. Without the file the build still works: each then needs a user's own key.
KEYS="${KEYS:-$HOME/stremiobrew-build/keys.env}"
python3 - "$KEYS" "$B/sbkeys.h" <<'PY'
import os, sys, hashlib
src, out = sys.argv[1], sys.argv[2]; kv = {}
if os.path.isfile(src):
    for ln in open(src):
        ln = ln.strip()
        if ln and not ln.startswith('#') and '=' in ln: k, v = ln.split('=', 1); kv[k.strip()] = v.strip().strip('"\'')
def arr(name):
    v = kv.get(name, '').encode(); pad = hashlib.sha256(b'sb:' + name.encode()).digest()
    return 'static const unsigned char SBK_%s[] = { %s0 }, SBK_%s_PAD[32] = { %s };\n#define SBK_%s_LEN %d\n' % (name, ''.join('%d,' % (c ^ pad[i % 32]) for i, c in enumerate(v)), name, ','.join(str(x) for x in pad), name, len(v))
body = '/* made by tools/build.sh from the private keys file: never commit */\n' + arr('TRAKT_CLIENT_ID') + arr('TMDB_API_KEY')
if not os.path.isfile(out) or open(out).read() != body: open(out, 'w').write(body)
print('== keys built in: %s' % (', '.join(n for n in ('TRAKT_CLIENT_ID', 'TMDB_API_KEY') if kv.get(n)) or 'none (%s)' % src))
PY

# ---- third-party, once per target (as tools/build.sh builds it) ----
if [ ! -f "$B/tp/libtp.a" ]; then echo "== third-party ($T)"; TPQ="-w $OPT"
  FT="-DFT2_BUILD_LIBRARY -DFT_CONFIG_MODULES_H=<ftmodule-sb.h> -I$TP/config -I$TP/freetype/include"
  for f in base/ftbase base/ftinit base/ftsystem base/ftdebug base/ftbitmap base/ftglyph base/ftmm base/ftbbox base/ftsynth base/ftstroke gzip/ftgzip truetype/truetype sfnt/sfnt smooth/smooth autofit/autofit psnames/psnames pshinter/pshinter cff/cff psaux/psaux; do
    $CC $TPQ $FT -c "$TP/freetype/src/$f.c" -o "$B/tp/ft_$(basename $f).o"; done
  $CXX -w $OPT -std=c++11 -fno-exceptions -fno-rtti -DHAVE_FREETYPE=1 -DHAVE_PTHREAD=1 -I$TP/freetype/include -I$TP/harfbuzz/src -c "$TP/harfbuzz/src/harfbuzz.cc" -o "$B/tp/harfbuzz.o"
  for f in "$TP"/fribidi/lib/fribidi*.c; do $CC $TPQ -DHAVE_CONFIG_H -I$TP/config/fribidi -I$TP/fribidi/lib -c "$f" -o "$B/tp/fb_$(basename "${f%.c}").o"; done
  $CC $TPQ -c "$TP/cjson/cJSON.c" -o "$B/tp/cJSON.o"; $CC $TPQ -c "$TP/qrcodegen/qrcodegen.c" -o "$B/tp/qrcodegen.o"
  for f in "$TP"/webp/src/dec/*.c "$TP"/webp/src/dsp/*.c "$TP"/webp/src/utils/*.c; do case "$(basename "$f")" in *enc*|*writer*|*quant_levels_utils*|*_sse*|*_msa*|*_mips*) continue;; esac
    $CC $TPQ -DWEBP_REDUCE_CSP -I$TP/webp -c "$f" -o "$B/tp/webp_$(basename "${f%.c}").o"; done
  $AR rcs "$B/tp/libtp.a" "$B"/tp/*.o; fi
if [ ! -f "$B/tp/libass.a" ]; then echo "== libass ($T)"
  for f in "$TP"/libass/libass/*.c "$TP"/libass/libass/c/*.c; do $CC -w $OPT -std=gnu11 -DHAVE_CONFIG_H -I$TP/config/libass -I$TP/libass/libass -I$TP/freetype/include -I$TP/harfbuzz/src -I$TP/fribidi/lib -I$TP/config/fribidi -c "$f" -o "$B/tp/ass/$(basename "${f%.c}").o"; done
  $CC -w $OPT -DFT2_BUILD_LIBRARY -DFT_CONFIG_MODULES_H="<ftmodule-sb.h>" -I$TP/config -I$TP/freetype/include -c "$TP/freetype/src/base/fttype1.c" -o "$B/tp/ass/ft_fttype1.o"
  $AR rcs "$B/tp/libass.a" "$B"/tp/ass/*.o; fi

# ---- the app ----
echo "== phone ($T)"; OBJS=()
for f in "$TP"/crypto/*.c; do o="$B/tp/crypto_$(basename "${f%.c}").o"; [ -f "$o" ] && [ "$o" -nt "$f" ] || $CC -w $OPT -I$TP/crypto -c "$f" -o "$o"; OBJS+=("$o"); done
SCREENS=$(ls "$ROOT"/src/screens/*.c | grep -v -E '/(board|detail|cwmenu|lib|search|settings|login|livehub)\.c$')   # the phone has its own in their places
CORE=$(ls "$ROOT"/src/core/*.c | grep -v -E '/(castlink)\.c$')   # (the TV's cast receiver: a phone is never one)
for f in $CORE "$ROOT"/src/ui/*.c $SCREENS "$ROOT"/src/main.c "$ROOT"/src/phone/*.c $PLATSRC; do   # (applog.c every time: it names the build, SB_GITREV, that crash addresses belong to)
  o="$B/obj/$(echo "${f#$ROOT/src/}" | tr / _).o"; FL="$CFLAGS"; case "$f" in *.m) FL="${CFLAGS/-std=c11/}";; esac
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ "$f" = "$ROOT/src/core/applog.c" ] || [ -n "$(find "$ROOT/src" -name '*.h' -newer "$o" | head -1)" ] || [ "$B/sbkeys.h" -nt "$o" ]; then $CC $FL -c "$f" -o "$o"; fi; OBJS+=("$o"); done
if [ "$T" = mac ]; then $CXX $OPT -o "$B/stremioorigin-phone" "${OBJS[@]}" "$B/tp/libass.a" "$B/tp/libtp.a" $LIBS; OUT="$B/stremioorigin-phone"
else $CXX $OPT -shared -Wl,--gc-sections -Wl,-soname,libmain.so -Wl,--no-undefined -o "$B/libmain.dbg.so" "${OBJS[@]}" "$B/tp/libass.a" "$B/tp/libtp.a" $LIBS
  "$BIN/llvm-strip" -o "$B/libmain.so" "$B/libmain.dbg.so"; OUT="$B/libmain.so"; fi
ls -l "$OUT" | awk '{printf "%s  %.1f MB\n", $NF, $5/1048576}'
