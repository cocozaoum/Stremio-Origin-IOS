#!/usr/bin/env bash
# Makes the phone sources build for iOS (SB_IOS): the Mac preview's paths (static curl, video drawn into the window) are
# taken for iOS too. Safe to run twice. Run from anywhere: it finds the repo root.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
python3 - "$ROOT" <<'PY'
import re, sys, os
root = sys.argv[1]
def edit(rel, fn):
    p = os.path.join(root, rel); s = open(p, encoding='utf-8').read(); n = fn(s)
    if n == s: print('  (unchanged) ' + rel)
    else: open(p, 'w', encoding='utf-8').write(n); print('  patched    ' + rel)
MAC_OR_IOS = '#if defined(SB_MAC) || defined(SB_IOS)'
def hooks(s):   # only the #ifdef SB_MAC blocks that call the Mac preview's video drawing
    return re.sub(r'#ifdef SB_MAC(\n\s*\{ extern [^\n]*?video_mac_)', MAC_OR_IOS.replace('\\', '\\\\') + r'\1', s)
def gl(s):
    if 'SB_IOS' in s: return s
    return s.replace('#if defined(SB_MAC)', '#if defined(SB_IOS)\n#  include <OpenGLES/ES2/gl.h>\n#  include <OpenGLES/ES2/glext.h>\n#  define SB_GLSL_PRECISION "precision mediump float;\\n"\n#elif defined(SB_MAC)', 1)
def net(s):
    if 'SB_IOS' in s: return s
    return s.replace('#ifdef SB_MAC', MAC_OR_IOS).replace('#ifndef SB_MAC', '#if !defined(SB_MAC) && !defined(SB_IOS)')
def mainc(s):
    s = hooks(s)
    return s.replace('#if defined(SB_PHONE) && !defined(__ANDROID__)\n', '#if defined(SB_PHONE) && !defined(__ANDROID__) && !defined(SB_IOS)\n')
edit('src/ui/gl.h', gl)
edit('src/core/net.c', net)
edit('src/ui/gfx.c', hooks)
edit('src/main.c', mainc)
edit('src/screens/player.c', hooks)
edit('src/screens/livepeek.c', hooks)
PY
echo "iOS patches applied."
