# Writes build/ios/sbkeys.h from TMDB_API_KEY / TRAKT_CLIENT_ID (environment), scrambled as tools/phone/build.sh does.
import os, sys, hashlib
out = sys.argv[1]; kv = {n: os.environ.get(n, '') for n in ('TRAKT_CLIENT_ID', 'TMDB_API_KEY')}
def arr(name):
    v = kv.get(name, '').encode(); pad = hashlib.sha256(b'sb:' + name.encode()).digest()
    return 'static const unsigned char SBK_%s[] = { %s0 }, SBK_%s_PAD[32] = { %s };\n#define SBK_%s_LEN %d\n' % (name, ''.join('%d,' % (c ^ pad[i % 32]) for i, c in enumerate(v)), name, ','.join(str(x) for x in pad), name, len(v))
body = '/* made by tools/ios/build.sh: never commit */\n' + arr('TRAKT_CLIENT_ID') + arr('TMDB_API_KEY')
if not os.path.isfile(out) or open(out).read() != body: open(out, 'w').write(body)
print('== keys built in: %s' % (', '.join(n for n in kv if kv[n]) or 'none'))
