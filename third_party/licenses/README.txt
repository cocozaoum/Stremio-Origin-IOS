Third-party code in this app and its licences:
- libsbengine.so (the TV app, and the phone app on Android): the torrent engine github.com/NuvioMedia/nuvio-engine (GPL-3.0, torrent-engine-GPL-3.0.txt), patched (tools/engine/engine.patch in the source),
  on libtorrent 2.0.12 (BSD, libtorrent-BSD.txt) and Boost 1.86 (Boost Software License 1.0, boost-BSL-1.0.txt).
- libsbdl.so (the phone app's downloads): Gopeed's HTTP downloader github.com/GopeedLab/gopeed (GPL-3.0) at commit e0951de2,
  built by tools/gopeed/build.sh with its extensions and archive extraction left out (tools/gopeed/overlay); with imroc/req
  (MIT), quic-go and qpack (MIT), refraction-networking/utls (BSD-3-Clause), cloudflare/circl (BSD-3-Clause), bbolt (MIT),
  zerolog (MIT), klauspost/compress (BSD-3-Clause, parts Apache-2.0/MIT), andybalholm/brotli (MIT), go-nanoid (MIT),
  go-ieproxy (MIT), icholy/digest (MIT), armon/go-socks5 (MIT), hashicorp/go-multierror (MPL-2.0), golang.org/x/net, crypto,
  text, sys, exp and the Go runtime (BSD-3-Clause).
- libsbdts.so: the sound converter and the seek previews' H.264 / HEVC picture decoding (tools/dts/sbdts.c) on FFmpeg 7.1.1 (LGPL 2.1 or later, ffmpeg-LGPL-2.1.txt), unmodified, built
  as described in ffmpeg-SOURCE.txt (tools/build-ffmpeg.sh); a shared library of its own, so it can be rebuilt and replaced.
- SHA-256 and AES (third_party/crypto): Brad Conte's crypto-algorithms, public domain (crypto-algorithms-public-domain.txt).
- P-256 key exchange (third_party/crypto/uecc): micro-ecc by Kenneth MacKay, BSD-2-Clause (micro-ecc-BSD-2-Clause.txt).
- FreeType, HarfBuzz, FriBidi, libass (ISC, 0.17.5), cJSON, stb, qrcodegen, libwebp, SDL2: their licences ship with their sources in third_party/.
The app itself is GPL-3.0.

liquid-glass-react (https://github.com/rdev/liquid-glass-react), MIT, Copyright 2025 Max Rovensky
  Its "standard" glass ported to a GLSL shader (src/ui/gfx.c, FS_LGLASS): the displacement, the per-colour scales, the
  blur and saturation, the light-background darkening, the border gradients, the pressed highlights and the elasticity.
  (Permission is hereby granted, free of charge, to any person obtaining a copy of this software ... the above
  copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.)
- The Library icon (assets/icons.phone.json, ph_lib): Nuvio's sidebar_library drawable from github.com/NuvioMedia/NuvioMobile
  (GPL-3.0), its slot drawn the other way round for this app's non-zero fill.
