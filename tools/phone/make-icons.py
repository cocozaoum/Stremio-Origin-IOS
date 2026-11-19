#!/usr/bin/env python3
# Usage: tools/phone/make-icons.py <out dir>  (writes fg_432.png, legacy_192.png, splash_288.png; see android-phone/app/src/main/res)
# Draws the app's mark: a purple disc (lit top-left) with a white rounded play triangle, on transparency.
import zlib, struct, sys, math
def png(path, W, px):
    raw = b''.join(b'\x00' + bytes(px[y*W*4:(y+1)*W*4]) for y in range(W))
    def ch(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', W, W, 8, 6, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw, 9)) + ch(b'IEND', b''))
def tri_d(x, y):  # exact signed distance to a play triangle (inside negative)
    A = (0.43, 0.31); B = (0.43, 0.69); C = (0.74, 0.50)
    def seg(p, q):
        ex, ey = q[0]-p[0], q[1]-p[1]; t = max(0, min(1, ((x-p[0])*ex + (y-p[1])*ey) / (ex*ex+ey*ey))); return math.hypot(x-p[0]-t*ex, y-p[1]-t*ey)
    def side(p, q):
        ex, ey = q[0]-p[0], q[1]-p[1]; return (x-p[0])*ey - (y-p[1])*ex
    d = min(seg(A, B), seg(B, C), seg(C, A)); inside = side(A, B) <= 0 and side(B, C) <= 0 and side(C, A) <= 0 or side(A, B) >= 0 and side(B, C) >= 0 and side(C, A) >= 0
    return -d if inside else d
def draw(W, discR, path, cy=.5, triR=None):
    triR = triR or discR  # the play sign's own scale (a launcher icon: the disc fills it all, the sign stays its size)
    S = 3; px = bytearray(W*W*4)
    for j in range(W):
        for i in range(W):
            r = g = b = a = 0.0
            for sj in range(S):
                for si in range(S):
                    x = (i + (si+.5)/S) / W; y = (j + (sj+.5)/S) / W
                    y = y - (cy - .5); d = math.hypot(x-.5, y-.5)
                    if d > discR: continue
                    t = (y-.5) / (2*min(discR, .36)) + .5; t = max(0, min(1, t))  # 0 top .. 1 bottom (the start screen's own gradient)
                    cr, cg, cb = 0x6a + (0x2f-0x6a)*t, 0x45 + (0x1a-0x45)*t, 0xe0 + (0x9e-0xe0)*t; li = 26 * max(0, 1 - t) / 255.0; cr, cg, cb = cr + (255-cr)*li, cg + (255-cg)*li, cb + (255-cb)*li
                    u, v = (x-.5)/triR*.5+.5, (y-.5)/triR*.5+.5
                    if tri_d(u, v) < 0.045: cr, cg, cb = 255, 255, 255  # rounded (the triangle grown by its corner radius), a lilac white
                    r += cr; g += cg; b += cb; a += 1
            n = S*S
            if a: px[(j*W+i)*4:(j*W+i)*4+4] = bytes((int(r/a), int(g/a), int(b/a), int(255*a/n)))
    png(path, W, px)
out = sys.argv[1]
# launcher foreground (adaptive: 108dp, the mark inside the 66dp safe circle), legacy round icon, splash icon
# the launcher's: the disc fills the whole adaptive canvas (whatever the launcher's mask, only the disc shows: a circle,
# nothing behind it), the play sign sized as before inside the safe zone
draw(432, 0.5, f'{out}/fg_432.png', .5, 0.30)
for name, W, R in [('legacy_192', 192, 0.47), ('splash_288', 288, 0.235)]:
    draw(W, R, f'{out}/{name}.png', .5)
