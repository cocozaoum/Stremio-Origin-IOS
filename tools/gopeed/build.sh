#!/bin/sh
# libsbdl: Gopeed's HTTP downloader (github.com/GopeedLab/gopeed, GPL-3.0) as a shared library behind src/phone/sbdl.h.
#   tools/gopeed/build.sh mac            -> build/phone-mac/libsbdl.dylib
#   tools/gopeed/build.sh android-arm64  -> build/phone-arm64-v8a/libsbdl.so
#   tools/gopeed/build.sh android-armv7  -> build/phone-armeabi-v7a/libsbdl.so
#   tools/gopeed/build.sh all
# Gopeed's source lives in ~/stremiobrew-build/gopeed/src, pinned to GOPEED_COMMIT. sbdl.go uses Gopeed's internal
# packages, so it is built as a package of that tree (through a build overlay, see below).
set -eu
GOPEED_COMMIT=e0951de270dee2703bb5bea0e81832c7e701aeef
GOPEED_REPO=https://github.com/GopeedLab/gopeed.git

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
WORK="${SB_GOPEED_DIR:-$HOME/stremiobrew-build/gopeed}"
SRC="$WORK/src"
GO="${GO:-/opt/homebrew/bin/go}"
NDK="${NDK:-$HOME/stremiobrew-build/android/sdk/ndk/27.2.12479018}"
API=24

if [ ! -d "$SRC/.git" ]; then
  mkdir -p "$WORK"
  git clone -q "$GOPEED_REPO" "$SRC"
fi
if [ "$(git -C "$SRC" rev-parse HEAD)" != "$GOPEED_COMMIT" ]; then
  git -C "$SRC" fetch -q origin "$GOPEED_COMMIT" 2>/dev/null || git -C "$SRC" fetch -q origin
  git -C "$SRC" checkout -q --detach "$GOPEED_COMMIT"
fi

# sbdl.go, sbdl_table.c and sbdl.h are copied in as the package $SRC/sbdl (untracked; it uses Gopeed's internal
# packages). Gopeed's own files are changed only through a build overlay, the checkout stays as it is:
#  - pkg/download loses its extensions (JavaScript, webview, git) and archive extraction, stubbed by
#    overlay/download_stub.go, and its default protocol list loses bt, ed2k and hls, so none of their
#    dependencies (torrent, goja, wazero/ffmpeg, go-git, pion...) are linked in
mkdir -p "$SRC/sbdl"
cp "$HERE/sbdl.go" "$HERE/sbdl_table.c" "$ROOT/src/phone/sbdl.h" "$SRC/sbdl/"
OV="$WORK/overlay"
rm -rf "$OV"; mkdir -p "$OV"
DL="$SRC/pkg/download"
sed -E -e '/engine\/webview"/d' -e '/WebViewProvider/d' \
    -e '/internal\/protocol\/(bt|ed2k|hls)"/d' -e '/new\((hls|bt|ed2k)\.FetcherManager\)/d' \
    "$DL/model.go" > "$OV/model.go"
{
  echo '{"Replace":{'
  for f in "$DL"/extension*.go "$DL"/extract.go "$DL"/extract_7z.go "$DL"/extract_rar.go "$DL"/extract_zip.go; do
    case "$f" in *_test.go) ;; *) echo "\"$f\":\"\"," ;; esac
  done
  echo "\"$DL/model.go\":\"$OV/model.go\","
  echo "\"$DL/zz_sbdl_stub.go\":\"$HERE/overlay/download_stub.go\""
  echo '}}'
} > "$OV/overlay.json"

export GOTOOLCHAIN=local CGO_ENABLED=1
LDX="-s -w -X main.gopeedCommit=$(echo $GOPEED_COMMIT | cut -c1-12)"

host_tag() { case "$(uname -s)" in Darwin) echo darwin-x86_64 ;; *) echo linux-x86_64 ;; esac; }

build() {
  case "$1" in
    mac)
      OUT="$ROOT/build/phone-mac/libsbdl.dylib"; mkdir -p "$(dirname "$OUT")"
      (cd "$SRC" && "$GO" build -overlay "$OV/overlay.json" -buildmode=c-shared -trimpath \
        -ldflags "$LDX -extldflags=-Wl,-install_name,@rpath/libsbdl.dylib" -o "$OUT" ./sbdl)
      ;;
    android-arm64|android-armv7)
      TC="$NDK/toolchains/llvm/prebuilt/$(host_tag)/bin"
      if [ "$1" = android-arm64 ]; then
        ABI=arm64-v8a; CC="$TC/aarch64-linux-android$API-clang"; set -- arm64 ""
      else
        ABI=armeabi-v7a; CC="$TC/armv7a-linux-androideabi$API-clang"; set -- arm 7
      fi
      OUT="$ROOT/build/phone-$ABI/libsbdl.so"; mkdir -p "$(dirname "$OUT")"
      (cd "$SRC" && GOOS=android GOARCH="$1" GOARM="$2" CC="$CC" "$GO" build -overlay "$OV/overlay.json" -buildmode=c-shared -trimpath \
        -ldflags "$LDX -extldflags=-Wl,-soname,libsbdl.so,-z,max-page-size=16384" -o "$OUT" ./sbdl)
      ;;
    *) echo "usage: $0 mac|android-arm64|android-armv7|all" >&2; exit 2 ;;
  esac
  rm -f "${OUT%.*}.h"   # go writes a header next to the library; the app uses src/phone/sbdl.h
  ls -l "$OUT"
}

if [ "${1:-}" = all ]; then
  for t in mac android-arm64 android-armv7; do build $t; done
else
  build "${1:-}"
fi
