#!/usr/bin/env bash
# The formatting is rough and I'm not going to bother. Pull requests that fix it are welcome; complaints without one help neither me nor the end user.
# Build the Stremio Origin phone APK.
# Usage: tools/phone/apk.sh [debug|release]
set -euo pipefail

MODE="${1:-debug}"
case "$MODE" in
  debug|release) ;;
  *) echo "usage: $0 [debug|release]" >&2; exit 2 ;;
esac
TASK="assemble$(tr '[:lower:]' '[:upper:]' <<< "${MODE:0:1}")${MODE:1}"

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PHONE="$ROOT/android-phone"
TOOLS="$HOME/stremiobrew-build/android"
KEYS="$TOOLS/keys"
PROPS="$KEYS/phone.properties"

export ANDROID_HOME="$TOOLS/sdk"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
GRADLE="$TOOLS/gradle-8.11.1/bin/gradle"

[ -d "$ANDROID_HOME" ] || { echo "error: Android SDK not found at $ANDROID_HOME" >&2; exit 1; }
[ -x "$GRADLE" ] || { echo "error: Gradle not found at $GRADLE" >&2; exit 1; }

# Gradle 8.11 / AGP 8.7 do not run on JDK 24+; pick a JDK 17-23 by real version.
jdk_major() {
  "$1/bin/java" -version 2>&1 | sed -n 's/.*version "\([0-9]*\).*/\1/p' | head -1
}
JDK=""
for c in \
  "$(brew --prefix openjdk 2>/dev/null || true)/libexec/openjdk.jdk/Contents/Home" \
  "$(brew --prefix openjdk@17 2>/dev/null || true)/libexec/openjdk.jdk/Contents/Home" \
  "$(/usr/libexec/java_home -v 23 2>/dev/null || true)" \
  "$(/usr/libexec/java_home -v 17 2>/dev/null || true)"; do
  [ -n "$c" ] && [ -x "$c/bin/java" ] || continue
  major="$(jdk_major "$c")"
  if [ "$major" -ge 17 ] && [ "$major" -le 23 ]; then JDK="$c"; break; fi
done
[ -n "$JDK" ] || { echo "error: no JDK 17-23 found (brew install openjdk@17)" >&2; exit 1; }
export JAVA_HOME="$JDK"
echo "JDK: $JAVA_HOME"

# Local SDK location for Gradle (never committed).
printf 'sdk.dir=%s\n' "$ANDROID_HOME" > "$PHONE/local.properties"

# Native libraries: one set per ABI, extracted by Gradle (useLegacyPackaging).
JNI="$PHONE/app/src/main/jniLibs"
for ABI in arm64-v8a armeabi-v7a; do
  mkdir -p "$JNI/$ABI"
  rm -f "$JNI/$ABI"/*.so
  B="$ROOT/build/phone-$ABI"
  for f in "$B/deps/lib/libSDL2.so" "$B/deps/lib/libcurl.so" "$B/libmain.so"; do
    if [ -f "$f" ]; then
      cp "$f" "$JNI/$ABI/"
    else
      echo "warning: missing $f" >&2
    fi
  done
  # the torrent engine (tools/build-engine.sh android-arm64 / android-armv7): optional, torrents need it
  if [ -f "$B/libsbengine.so" ]; then cp "$B/libsbengine.so" "$JNI/$ABI/"; else echo "note: no $B/libsbengine.so (torrents will not play on the phone)" >&2; fi
  # the download engine (tools/gopeed/build.sh android-arm64 / android-armv7): optional, downloads need it
  if [ -f "$B/libsbdl.so" ]; then cp "$B/libsbdl.so" "$JNI/$ABI/"; else echo "note: no $B/libsbdl.so (downloads will not work on the phone)" >&2; fi
  # the seek-preview decoder (tools/phone/build-prev.sh): only when asked (SB_PREVIEW_LIB=1), shipping it is the owner's call
  if [ "${SB_PREVIEW_LIB:-0}" = 1 ]; then PL="$ROOT/build/phone-prev/$ABI/libsbprev.so"; if [ -f "$PL" ]; then cp "$PL" "$JNI/$ABI/"; else echo "note: no $PL (run tools/phone/build-prev.sh)" >&2; fi; fi
done

# Release keystore, created once.
if [ "$MODE" = release ] && [ ! -f "$PROPS" ]; then
  mkdir -p "$KEYS"
  STORE="$KEYS/phone.jks"
  PASS="$(openssl rand -hex 12)"   # 24 characters
  "$JDK/bin/keytool" -genkeypair -keystore "$STORE" -storepass "$PASS" -keypass "$PASS" \
    -alias phone -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Stremio Origin"
  ( umask 077
    printf 'storeFile=%s\nstorePassword=%s\nkeyAlias=phone\nkeyPassword=%s\n' \
      "$STORE" "$PASS" "$PASS" > "$PROPS" )
  chmod 600 "$PROPS"
  echo "created keystore: $STORE"
fi

"$GRADLE" -p "$PHONE" "$TASK" --console=plain

VERSION="$(sed -n "s/.*versionName '\([^']*\)'.*/\1/p" "$PHONE/app/build.gradle" | head -1)"
OUTDIR="$PHONE/app/build/outputs/apk/$MODE"
# one APK per processor (libVLC is large): 64-bit phones take arm64-v8a, older ones armeabi-v7a
GITREV=$(git -C "$ROOT" rev-parse --short=10 HEAD 2>/dev/null || echo unknown)$(git -C "$ROOT" diff --quiet HEAD -- src 2>/dev/null || echo "+$(git -C "$ROOT" diff HEAD -- src | shasum | cut -c1-6)")   # (uncommitted changes: their own fingerprint, so each test build keeps its own debug copy)
for ABI in arm64-v8a armeabi-v7a; do D="$ROOT/dist/dbg/$GITREV/$ABI"; mkdir -p "$D"; for f in libmain.dbg.so libsbengine.dbg.so; do [ -f "$ROOT/build/phone-$ABI/$f" ] && cp "$ROOT/build/phone-$ABI/$f" "$D/"; done; done   # (a crash report's addresses, named with these: llvm-addr2line -fCe)
[ "$MODE" = release ] && { R="$ROOT/dist/dbg/release-$VERSION-$GITREV"; mkdir -p "$R"; cp -R "$ROOT/dist/dbg/$GITREV/." "$R/"; }   # (a release's debug files kept apart, never pruned: a user's crash report names its code from them)
ls -dt "$ROOT"/dist/dbg/*/ 2>/dev/null | grep -v '/release-' | tail -n +6 | while read -r d; do rm -rf "${d:?}"; done   # (the newest 5 test builds' debug files kept: each is ~185 MB, and they filled the disk twice)
SUF="-$MODE"; [ "$MODE" = release ] && SUF=""   # (a release: the very name the app's updater looks for in the releases, stremio-origin-phone-<x.y.z>-<abi>.apk)
mkdir -p "$ROOT/dist"; if [ -z "$SUF" ]; then rm -f "$ROOT/dist/stremio-origin-phone-"*"-arm64-v8a.apk" "$ROOT/dist/stremio-origin-phone-"*"-armeabi-v7a.apk"; else rm -f "$ROOT/dist/stremio-origin-phone-"*"$SUF.apk"; fi; n=0
for ABI in arm64-v8a armeabi-v7a; do
  for f in "$OUTDIR/app-$ABI-$MODE.apk" "$OUTDIR/app-$ABI-$MODE-unsigned.apk"; do
    [ -f "$f" ] || continue; DEST="$ROOT/dist/stremio-origin-phone-$VERSION-$ABI$SUF.apk"; cp "$f" "$DEST"; n=$((n + 1))
    echo "APK: $DEST ($(ls -lh "$DEST" | awk '{print $5}'))"; break
  done
done
[ $n -gt 0 ] || { echo "error: no APK found in $OUTDIR" >&2; exit 1; }
