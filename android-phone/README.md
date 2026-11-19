# Stremio Origin for Android phones

The phone app: the same C core and drawing as the TV app (`src/core`, `src/ui`), with its own screens and touch in
`src/phone`, packaged here for Android (SDL2 over GLES2, one APK per processor).

## Building
- `tools/phone/build-deps.sh arm64-v8a` and `armeabi-v7a`: SDL2 and curl (on mbedTLS) with the Android NDK.
- `tools/phone/build.sh arm64-v8a` and `armeabi-v7a`: the app's native part.
- `tools/phone/apk.sh debug` (or `release`): the APKs in `dist/`.

## Video playback: libVLC
Films play through **libVLC**, the playback engine of [VLC for Android](https://github.com/videolan/vlc-android)
by VideoLAN (`org.videolan.android:libvlc-all`), used unmodified under the GNU LGPL 2.1 or later. Its player
structure (a video surface under the app's own controls, the engine fitting and scaling the picture, the file's
own subtitles drawn by the engine) is the model for this app's player, and will be the same on Android TV.
Thanks to the VideoLAN team and contributors.
