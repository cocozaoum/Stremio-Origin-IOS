package app.stremioorigin.phone;

import android.os.Build;
import android.util.Log;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.EOFException;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.math.BigInteger;
import java.net.ConnectException;
import java.net.InetSocketAddress;
import java.net.SocketTimeoutException;
import java.security.KeyFactory;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.KeyStore;
import java.security.MessageDigest;
import java.security.PrivateKey;
import java.security.SecureRandom;
import java.security.Signature;
import java.security.cert.Certificate;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.security.interfaces.RSAPublicKey;
import java.security.spec.PKCS8EncodedKeySpec;
import java.util.ArrayList;
import java.util.List;

import javax.net.ssl.KeyManagerFactory;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLException;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.TrustManager;
import javax.net.ssl.X509TrustManager;

/* Google's TV remote (Android TV, Google TV): what the phone needs to open our app on a box whose app is closed, or
   which sleeps. The phone has its own certificate (made once, kept in the app's own files: never anywhere else). The
   box trusts it after one pairing: it shows a 6-character code, typed on the phone (port 6467). Then, on port 6466,
   the phone wakes the box if its screen is off and asks it to open stremioorigin://open. Messages are protobuf, each
   preceded by its length, written by hand here (a few small ones). Every call blocks: the cast thread calls them. */
final class AtvRemote {
    private static final String TAG = "StremioOrigin";
    private static SSLContext ctx;
    private static X509Certificate mine;
    private static SSLSocket pairSock;

    /* ---- this phone's certificate: RSA 2048, self-signed, made once ---- */
    private static synchronized SSLContext context() throws Exception {
        if (ctx != null) return ctx;
        File dir = MainActivity.self.getFilesDir(), kf = new File(dir, "atv_remote.key"), cf = new File(dir, "atv_remote.crt");
        PrivateKey pk;
        CertificateFactory certs = CertificateFactory.getInstance("X.509");
        if (kf.exists() && cf.exists()) {
            pk = KeyFactory.getInstance("RSA").generatePrivate(new PKCS8EncodedKeySpec(read(kf)));
            mine = (X509Certificate) certs.generateCertificate(new ByteArrayInputStream(read(cf)));
        } else {
            KeyPairGenerator g = KeyPairGenerator.getInstance("RSA"); g.initialize(2048); KeyPair kp = g.generateKeyPair();
            byte[] der = selfSigned(kp); pk = kp.getPrivate();
            mine = (X509Certificate) certs.generateCertificate(new ByteArrayInputStream(der));
            write(kf, pk.getEncoded()); write(cf, der);
        }
        KeyStore ks = KeyStore.getInstance(KeyStore.getDefaultType()); ks.load(null, null); char[] pw = "atv".toCharArray();
        ks.setKeyEntry("me", pk, pw, new Certificate[] { mine });
        KeyManagerFactory kmf = KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm()); kmf.init(ks, pw);
        TrustManager[] any = { new X509TrustManager() { /* the box's own certificate is self-signed: on this home network it is taken as it is */
            public void checkClientTrusted(X509Certificate[] c, String a) {}
            public void checkServerTrusted(X509Certificate[] c, String a) {}
            public X509Certificate[] getAcceptedIssuers() { return new X509Certificate[0]; } } };
        SSLContext c = SSLContext.getInstance("TLS"); c.init(kmf.getKeyManagers(), any, new SecureRandom());
        return ctx = c;
    }
    private static byte[] read(File f) throws Exception { try (InputStream in = new FileInputStream(f)) { ByteArrayOutputStream o = new ByteArrayOutputStream(); byte[] b = new byte[4096]; int n; while ((n = in.read(b)) > 0) o.write(b, 0, n); return o.toByteArray(); } }
    private static void write(File f, byte[] d) throws Exception { try (OutputStream o = new FileOutputStream(f)) { o.write(d); } }

    /* a minimal X.509 v3 certificate, DER by hand (no library on old Androids makes one) */
    private static byte[] tlv(int tag, byte[]... parts) { ByteArrayOutputStream v = new ByteArrayOutputStream(); for (byte[] p : parts) v.write(p, 0, p.length); byte[] b = v.toByteArray();
        ByteArrayOutputStream o = new ByteArrayOutputStream(); o.write(tag); int n = b.length;
        if (n < 128) o.write(n); else if (n < 256) { o.write(0x81); o.write(n); } else { o.write(0x82); o.write(n >> 8); o.write(n & 255); }
        o.write(b, 0, n); return o.toByteArray(); }
    private static byte[] selfSigned(KeyPair kp) throws Exception {
        byte[] sha256rsa = { 0x06, 0x09, 0x2a, (byte) 0x86, 0x48, (byte) 0x86, (byte) 0xf7, 0x0d, 0x01, 0x01, 0x0b }, nul = { 0x05, 0x00 }, cn = { 0x06, 0x03, 0x55, 0x04, 0x03 };
        byte[] alg = tlv(0x30, sha256rsa, nul), name = tlv(0x30, tlv(0x31, tlv(0x30, cn, tlv(0x0c, "atvremote".getBytes("UTF-8")))));
        byte[] sn = new byte[8]; new SecureRandom().nextBytes(sn); sn[0] = (byte) ((sn[0] & 0x7f) | 0x01);
        byte[] validity = tlv(0x30, tlv(0x17, "250101000000Z".getBytes("US-ASCII")), tlv(0x17, "491231235959Z".getBytes("US-ASCII")));
        byte[] tbs = tlv(0x30, tlv(0xa0, tlv(0x02, new byte[] { 2 })), tlv(0x02, sn), alg, name, validity, name, kp.getPublic().getEncoded());
        Signature s = Signature.getInstance("SHA256withRSA"); s.initSign(kp.getPrivate()); s.update(tbs); byte[] sig = s.sign();
        byte[] bits = new byte[sig.length + 1]; System.arraycopy(sig, 0, bits, 1, sig.length);
        return tlv(0x30, tbs, alg, tlv(0x03, bits));
    }

    /* ---- protobuf, the little needed ---- */
    private static final class Pb {
        final ByteArrayOutputStream o = new ByteArrayOutputStream();
        static void varint(ByteArrayOutputStream o, long v) { while ((v & ~0x7fL) != 0) { o.write((int) ((v & 0x7f) | 0x80)); v >>>= 7; } o.write((int) v); }
        Pb num(int f, long v) { varint(o, (long) f << 3); varint(o, v); return this; }
        Pb bytes(int f, byte[] b) { varint(o, ((long) f << 3) | 2); varint(o, b.length); o.write(b, 0, b.length); return this; }
        Pb str(int f, String s) { try { return bytes(f, s.getBytes("UTF-8")); } catch (Exception e) { return this; } }
        Pb msg(int f, Pb m) { return bytes(f, m.o.toByteArray()); }
        byte[] done() { return o.toByteArray(); }
    }
    /* a message's fields: number, and its value (a number, or bytes) */
    private static final class Field { int num; long v; byte[] b; }
    private static List<Field> parse(byte[] m) {
        List<Field> out = new ArrayList<>(); int i = 0;
        while (i < m.length) { long key = 0; int sh = 0; while (i < m.length) { int c = m[i++] & 0xff; key |= (long) (c & 0x7f) << sh; sh += 7; if ((c & 0x80) == 0) break; }
            Field f = new Field(); f.num = (int) (key >>> 3); int wt = (int) (key & 7);
            if (wt == 0) { long v = 0; sh = 0; while (i < m.length) { int c = m[i++] & 0xff; v |= (long) (c & 0x7f) << sh; sh += 7; if ((c & 0x80) == 0) break; } f.v = v; }
            else if (wt == 2) { long l = 0; sh = 0; while (i < m.length) { int c = m[i++] & 0xff; l |= (long) (c & 0x7f) << sh; sh += 7; if ((c & 0x80) == 0) break; } int n = (int) Math.min(l, m.length - i); f.b = new byte[n]; System.arraycopy(m, i, f.b, 0, n); i += n; }
            else if (wt == 5) i += 4; else if (wt == 1) i += 8; else break;
            out.add(f); }
        return out; }
    private static Field get(List<Field> fs, int num) { for (Field f : fs) if (f.num == num) return f; return null; }
    private static void send(OutputStream o, byte[] m) throws Exception { ByteArrayOutputStream b = new ByteArrayOutputStream(); Pb.varint(b, m.length); b.write(m, 0, m.length); o.write(b.toByteArray()); o.flush(); }
    private static byte[] recv(InputStream in) throws Exception { long l = 0; int sh = 0; while (true) { int c = in.read(); if (c < 0) throw new EOFException(); l |= (long) (c & 0x7f) << sh; sh += 7; if ((c & 0x80) == 0) break; }
        if (l > 1 << 20) throw new EOFException(); byte[] m = new byte[(int) l]; int got = 0; while (got < l) { int n = in.read(m, got, (int) l - got); if (n < 0) throw new EOFException(); got += n; } return m; }

    private static SSLSocket open(String ip, int port, int ms) throws Exception {
        SSLSocket s = (SSLSocket) context().getSocketFactory().createSocket(); s.connect(new InetSocketAddress(ip, port), ms); s.setSoTimeout(10000); s.startHandshake(); return s; }

    /* ---- pairing (6467): the box shows a code; then the code's secret ---- */
    private static byte[] polo(Pb inner, int field) { return new Pb().num(1, 2).num(2, 200).msg(field, inner).done(); }
    private static int poloStep(InputStream in, int want) throws Exception { List<Field> r = parse(recv(in)); Field st = get(r, 2); if (st != null && st.v != 200) return (int) st.v; return get(r, want) != null ? 0 : -1; }
    /* 0: the code is on the TV; -1: no pairing here */
    static synchronized int pairStart(String ip, String name) {
        pairClose();
        try { SSLSocket s = open(ip, 6467, 4000); pairSock = s; InputStream in = s.getInputStream(); OutputStream o = s.getOutputStream();
            send(o, polo(new Pb().str(1, "atvremote").str(2, name), 10)); if (poloStep(in, 11) != 0) { pairClose(); return -1; }
            Pb enc = new Pb().num(1, 3).num(2, 6); /* hexadecimal, 6 */
            send(o, polo(new Pb().msg(1, enc).num(3, 1), 20)); if (poloStep(in, 20) != 0) { pairClose(); return -1; }
            send(o, polo(new Pb().msg(1, enc).num(2, 1), 30)); if (poloStep(in, 31) != 0) { pairClose(); return -1; }
            s.setSoTimeout(15000); Log.i(TAG, "[atv] pairing: the code is on the TV"); return 0;
        } catch (Exception e) { Log.i(TAG, "[atv] pairing did not start: " + e); pairClose(); return -1; } }
    private static byte[] unsigned(BigInteger b) { byte[] a = b.toByteArray(); int z = 0; while (z < a.length - 1 && a[z] == 0) z++; byte[] r = new byte[a.length - z]; System.arraycopy(a, z, r, 0, r.length); return r; }
    /* 0: paired; 1: not the code shown (try again); -1: failed */
    static synchronized int pairFinish(String code) {
        SSLSocket s = pairSock; if (s == null || code == null || code.length() != 6) return -1;
        try { byte[] c = new byte[3]; for (int i = 0; i < 3; i++) c[i] = (byte) Integer.parseInt(code.substring(i * 2, i * 2 + 2), 16);
            RSAPublicKey me = (RSAPublicKey) mine.getPublicKey(), tv = (RSAPublicKey) s.getSession().getPeerCertificates()[0].getPublicKey();
            MessageDigest h = MessageDigest.getInstance("SHA-256");
            h.update(unsigned(me.getModulus())); h.update(unsigned(me.getPublicExponent())); h.update(unsigned(tv.getModulus())); h.update(unsigned(tv.getPublicExponent())); h.update(c, 1, 2);
            byte[] sec = h.digest(); if (sec[0] != c[0]) { Log.i(TAG, "[atv] pairing: not the code shown"); return 1; } /* (the code's first byte is a check on the rest) */
            send(s.getOutputStream(), polo(new Pb().bytes(1, sec), 40)); int r = poloStep(s.getInputStream(), 41);
            pairClose(); Log.i(TAG, "[atv] pairing " + (r == 0 ? "done" : "refused (" + r + ")")); return r == 0 ? 0 : r == 402 ? 1 : -1;
        } catch (NumberFormatException e) { return 1; } catch (Exception e) { Log.i(TAG, "[atv] pairing failed: " + e); pairClose(); return -1; } }
    static synchronized void pairClose() { try { if (pairSock != null) pairSock.close(); } catch (Exception e) { } pairSock = null; }

    /* ---- the remote (6466): wake it, open the link. 0: asked; 1: pairing needed; -1: not reachable ---- */
    static int openApp(String ip, String link) {
        SSLSocket s;
        try { s = open(ip, 6466, 3000); }
        catch (ConnectException | SocketTimeoutException e) { Log.i(TAG, "[atv] remote not reachable: " + e); return -1; }
        catch (SSLException e) { Log.i(TAG, "[atv] remote refused this phone (pairing needed): " + e); return 1; }
        catch (Exception e) { Log.i(TAG, "[atv] remote: " + e); return e instanceof java.io.IOException ? 1 : -1; }
        try { InputStream in = s.getInputStream(); OutputStream o = s.getOutputStream(); s.setSoTimeout(6000);
            int feats = 1 | 2 | 32 | 64 | 512; /* ping, keys, power, volume, app links */ boolean configured = false, sent = false; long end = System.currentTimeMillis() + 9000;
            while (System.currentTimeMillis() < end) {
                byte[] m; try { m = recv(in); } catch (SocketTimeoutException t) { if (configured && !sent) { send(o, new Pb().msg(90, new Pb().str(1, link)).done()); sent = true; end = System.currentTimeMillis() + 1500; continue; } break; }
                List<Field> f = parse(m);
                Field cfg = get(f, 1), act = get(f, 2), ping = get(f, 8), start = get(f, 40);
                if (cfg != null && cfg.b != null) { Field c1 = get(parse(cfg.b), 1); int both = c1 != null ? (int) (c1.v & feats) : feats; configured = true;
                    Pb dev = new Pb().str(1, Build.MODEL).str(2, Build.MANUFACTURER).num(3, 1).str(4, "1").str(5, "atvremote").str(6, "1.0.0");
                    send(o, new Pb().msg(1, new Pb().num(1, both).msg(2, dev)).done()); }
                else if (act != null) send(o, new Pb().msg(2, new Pb().num(1, feats)).done());
                else if (ping != null && ping.b != null) { Field v = get(parse(ping.b), 1); send(o, new Pb().msg(9, new Pb().num(1, v != null ? v.v : 0)).done()); }
                else if (start != null && !sent) { Field on = start.b != null ? get(parse(start.b), 1) : null; boolean isOn = on != null && on.v != 0;
                    if (!isOn) { Log.i(TAG, "[atv] the screen is off: waking it"); send(o, new Pb().msg(10, new Pb().num(1, 26).num(2, 3)).done()); Thread.sleep(1500); } /* (power, a short press) */
                    send(o, new Pb().msg(90, new Pb().str(1, link)).done()); sent = true; end = System.currentTimeMillis() + 1500; Log.i(TAG, "[atv] asked to open " + link); }
                else if (get(f, 3) != null) { Log.i(TAG, "[atv] the remote said an error"); }
            }
            s.close(); if (!configured) { Log.i(TAG, "[atv] remote closed before talking (pairing needed)"); return 1; }
            return sent ? 0 : -1;
        } catch (EOFException e) { try { s.close(); } catch (Exception x) { } Log.i(TAG, "[atv] remote closed (pairing needed)"); return 1; }
        catch (Exception e) { try { s.close(); } catch (Exception x) { } Log.i(TAG, "[atv] remote failed: " + e); return -1; }
    }
}
