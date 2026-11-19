#!/bin/zsh
# The phone's seek-preview decoder: FFmpeg with only the H.264 and HEVC decoders (no demuxers, muxers, parsers, filters
# or scaler: the app reads the keyframe itself and hands its bytes over), and the shared preview call
# (tools/dts/sbdts.c built with SBD_PREVIEW), as one small library per ABI: libsbprev.so, exporting sbd_prev_get only.
# -O2, not --enable-small: about 1 MB more for 15-20 % less CPU a picture (battery and heat on every film).
# Optional: shipping software video decoding is the owner's release decision; apk.sh packs it only with SB_PREVIEW_LIB=1.
#   tools/phone/build-prev.sh [arm64-v8a|armeabi-v7a]...   (both by default)
# LGPL-2.1+ as the rest of FFmpeg here (no --enable-gpl, no nonfree).
set -e
ROOT=${0:A:h:h:h}; W=$HOME/stremiobrew-build/ffmpeg; VERSION=7.1.1; SRC="$W/ffmpeg-$VERSION"
NDK=$HOME/stremiobrew-build/android/sdk/ndk/27.2.12479018/toolchains/llvm/prebuilt/darwin-x86_64/bin
[ -f "$SRC/configure" ] || { echo "no FFmpeg source at $SRC"; exit 1; }
ABIS=("$@"); [ ${#ABIS} -eq 0 ] && ABIS=(arm64-v8a armeabi-v7a)
for ABI in $ABIS; do
  case $ABI in
    arm64-v8a) TC=$W/tc-atv-arm64; ARCH=aarch64 ;;
    armeabi-v7a) TC=$W/tc-atv-armv7; ARCH=arm ;;
    *) echo "unknown ABI $ABI"; exit 1 ;;
  esac
  B=$W/build-phone-prev2-$ABI; P=$W/prefix-phone-prev2-$ABI; mkdir -p "$B"
  XC="-O2 -fPIC -ffunction-sections -fdata-sections -ffile-prefix-map=$HOME=/src -ffile-prefix-map=$W=/build"
  if [ ! -f "$P/lib/libavcodec.a" ]; then
    (cd "$B" && "$SRC/configure" --prefix="$P" --disable-autodetect --disable-shared --enable-static --enable-pic \
      --disable-programs --disable-doc --disable-debug --disable-network --disable-avdevice --disable-avfilter --disable-swscale --disable-postproc \
      --enable-avcodec --enable-avformat --enable-avutil --enable-swresample --disable-iconv --disable-zlib \
      --disable-everything --enable-decoder=h264,hevc \
      --enable-cross-compile --arch=$ARCH --target-os=android --cc=$TC/cc --ar=$NDK/llvm-ar --ranlib=$NDK/llvm-ranlib --nm=$NDK/llvm-nm --strip=$NDK/llvm-strip \
      --extra-cflags="$XC" >/dev/null && make -j8 >/dev/null && make install >/dev/null)
  fi
  OUT=$ROOT/build/phone-prev/$ABI; mkdir -p "$OUT"
  echo '{ global: sbd_prev_get; local: *; };' > "$OUT/exports.map"
  $TC/cc -std=c11 -O2 -fPIC -shared -fvisibility=hidden -ffunction-sections -fdata-sections -D_GNU_SOURCE -DSBD_PREVIEW -I"$P/include" -I"$ROOT/src" \
    -ffile-prefix-map=$HOME=/src -ffile-prefix-map=$ROOT=/app "$ROOT/tools/dts/sbdts.c" \
    -L"$P/lib" -lavformat -lavcodec -lswresample -lavutil -lm \
    -Wl,--gc-sections -Wl,--version-script="$OUT/exports.map" -Wl,-z,max-page-size=16384 -Wl,--build-id=none -o "$OUT/libsbprev.so"
  $NDK/llvm-strip --strip-all "$OUT/libsbprev.so"
  echo "$ABI: $(stat -f %z "$OUT/libsbprev.so") bytes -> build/phone-prev/$ABI/libsbprev.so"
done
