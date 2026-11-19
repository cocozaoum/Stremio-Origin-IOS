package app.stremioorigin.phone;

import android.content.pm.ActivityInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.graphics.Bitmap;
import android.view.PixelCopy;
import android.view.SurfaceView;
import android.view.SurfaceHolder;
import android.view.View;
import android.view.WindowManager;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.widget.RelativeLayout;

import org.videolan.libvlc.LibVLC;
import org.videolan.libvlc.Media;
import org.videolan.libvlc.MediaPlayer;
import org.videolan.libvlc.interfaces.IVLCVout;

import java.util.ArrayList;

/* The video plane under the app, played by libVLC (the engine of VLC for Android): a SurfaceView behind SDL's
   (transparent where the picture shows), driven by the native player through these static calls. libVLC decodes
   every format and sound (DTS and TrueHD included) and draws the file's own subtitles into the picture. The native
   side reads a snapshot (poll) once a frame. The phone turns sideways while a film is open. */
public final class VideoPlane {
    private static LibVLC vlc;
    private static MediaPlayer player;
    private static SurfaceView view;
    private static final Handler ui = new Handler(Looper.getMainLooper());
    /* for the native side: 0 idle, 1 loading, 2 playing, 3 paused, 4 buffering, 5 ended, 6 error */
    private static volatile long state, pos, dur, vw, vh, live; private static volatile float buffering = 100;
    private static volatile long posAt; /* when pos was read: the native side moves it on between reads */
    private static volatile String error = "";
    private static int wx, wy, ww, wh, spuWanted = -2, retries; private static String lastUrl;

    /* A stream that stops answering holds libVLC's stop() until its network read gives up (minutes on a stalled live
       server): never on the main thread, or the whole app hangs (Android: "not responding"). Each new stream gets a fresh
       player; the one before is taken off the screen at once and stopped and released on a thread of its own, so a stuck
       one only holds that thread. */
    private static MediaPlayer.EventListener events;
    private static void retire() {
        final MediaPlayer old = player; if (old == null) return; player = null;
        try { old.setEventListener(null); old.setVolume(0); } catch (Throwable e) { } /* (silent at once: its sound never heard over the next one) */
        try { IVLCVout o = old.getVLCVout(); if (o.areViewsAttached()) o.detachViews(); } catch (Throwable e) { }
        Thread t = new Thread(() -> { long t0 = System.nanoTime(); try { old.stop(); } catch (Throwable e) { } try { old.release(); } catch (Throwable e) { }
            long ms = (System.nanoTime() - t0) / 1000000; if (ms > 1500) android.util.Log.i("StremioOrigin", "[video] a stream let go after " + ms + " ms (off the main thread)");
            /* it held the phone's video decoder all that time: the stream playing now may have had none (its picture still
               or never there); opened again at once on the freed decoder, not after the frozen-picture watch's wait */
            if (ms > 1500) ui.post(() -> { if (player != null && lastUrl != null && !userPaused && state != 3 && state != 5 && state != 6 && (vouts == 0 || System.nanoTime() - dispAt > 1200000000L)) recover("the video decoder free again"); }); }, "vlc-release");
        t.setDaemon(true); t.start();
    }
    private static void fresh() { /* (the main thread) a new player on the same screen, the listener and output as the first one */
        if (vlc == null || view == null) return; retire(); newView(); player = new MediaPlayer(vlc); player.setEventListener(events); dispN = -1; dispAt = 0; vouts = 0;
        try { IVLCVout out = player.getVLCVout(); out.setVideoView(view); out.attachViews(layoutL); if (view.getWidth() > 0 && view.getHeight() > 0 && !swDecode) out.setWindowSize(view.getWidth(), view.getHeight()); } catch (Throwable e) { }
    }
    private static void run(Runnable r) { if (Looper.myLooper() == Looper.getMainLooper()) r.run(); else ui.post(r); }

    private static void ensure() {
        MainActivity a = MainActivity.self;
        if (a == null) return; if (vlc != null) { if (player == null) fresh(); return; } /* (made once; after a stream was let go, only a new player) */
        ArrayList<String> o = new ArrayList<>();
        o.add("--audio-time-stretch"); o.add("--avcodec-skiploopfilter=0"); o.add("--avcodec-skip-frame=0"); o.add("--avcodec-skip-idct=0"); o.add("--no-avcodec-corrupted"); /* (the edges of the blocks smoothed; a picture missing what it is built on is dropped, never shown as coloured blocks: a start, a seek, a torrent catching up) */
        o.add("--network-caching=1500"); o.add("--deinterlace=0"); o.add("--no-sub-autodetect-file");
        o.add("--freetype-rel-fontsize=16"); o.add("--freetype-color=16777215"); o.add("--freetype-background-opacity=0");
        vlc = new LibVLC(a, o);
        events = ev -> {
            switch (ev.type) {
                case MediaPlayer.Event.Opening: state = 1; break;
                case MediaPlayer.Event.Playing: state = 2; MainActivity.vPipUpdate(); if (spuWanted == -2 && player.getSpuTrack() != -1) player.setSpuTrack(-1); break;
                case MediaPlayer.Event.Paused: state = 3; MainActivity.vPipUpdate(); break;
                case MediaPlayer.Event.Buffering: buffering = ev.getBuffering(); break; /* it says this all the time while playing: a real wait is the time not moving (the native side sees it) */
                case MediaPlayer.Event.EndReached: android.util.Log.i("StremioOrigin", "[video] end reached at " + pos + " of " + dur); state = 5; break;
                case MediaPlayer.Event.EncounteredError: /* a connection dropped mid-film: twice again from where it was, then the player hears it */
                    if (live == 1 && pos > 0 && retries < 2 && lastUrl != null) { retries++; final long at = pos; ui.postDelayed(() -> reopen(at), 600); android.util.Log.i("StremioOrigin", "[video] dropped at " + at + " ms: again (" + retries + ")"); }
                    else { state = 6; error = "VLC_ERROR"; android.util.Log.i("StremioOrigin", "[video] error at " + pos); } break;
                case MediaPlayer.Event.TimeChanged: pos = ev.getTimeChanged(); posAt = System.nanoTime(); if (live == 0 && pos > 0) live = 1; if (vw <= 0 && !"fit".equals(sizeMode)) applySize(); break;
                case MediaPlayer.Event.LengthChanged: dur = ev.getLengthChanged(); break;
                case MediaPlayer.Event.Vout: vouts = ev.getVoutCount(); if (vouts > 0) { live = 1; vw = vh = 0; applySize(); } break;
                case MediaPlayer.Event.ESAdded: case MediaPlayer.Event.ESSelected: if (spuWanted == -2 && player.getSpuTrack() != -1) player.setSpuTrack(-1); if (!swDecode && legacyVideo()) { swDecode = true; final long at = pos; android.util.Log.i("StremioOrigin", "[video] an old video codec: decoded in software"); ui.post(() -> reopen(at)); } break; /* the file's subtitles stay off until the player asks (it shows its own) */
                default: break;
            }
        };
        player = new MediaPlayer(vlc); player.setEventListener(events);
        newView();
        ViewGroup l = MainActivity.layout();
        l.addOnLayoutChangeListener((v, left, top, right, bottom, ol, ot, or, ob) -> { if (player != null && right > left && bottom > top) { if (inPip()) { pipFit(); return; } if (!swDecode) player.getVLCVout().setWindowSize(right - left, bottom - top); placeView(); centrePivot(); applySize(); } } /* (the small window resized: nothing told to libVLC, its surface keeps its size and Android scales it) */);
        ui.postDelayed(watch, 2000); ui.removeCallbacks(pictureWatch); ui.postDelayed(pictureWatch, 500); ui.removeCallbacks(clockWatch); ui.post(clockWatch);
        layoutL = (v, width, height, visibleWidth, visibleHeight, sarNum, sarDen) -> { vw = visibleWidth > 0 ? visibleWidth : width; vh = visibleHeight > 0 ? visibleHeight : height; sarN = sarNum; sarD = sarDen; if (sarNum > 0 && sarDen > 0 && sarNum != sarDen) vw = vw * sarNum / sarDen; placeView(); applySize(); };
        IVLCVout out = player.getVLCVout();
        out.setVideoView(view);
        out.attachViews(layoutL);
    }
    /* each stream on a surface of its own: a stream let go keeps its own (taken off the screen at once) while it ends,
       so it never draws over the next one or holds the surface it needs (the screen takes one source at a time) */
    private static void newView() {
        MainActivity a = MainActivity.self; ViewGroup l = MainActivity.layout(); if (a == null || l == null) return;
        SurfaceView old = view, v = new SurfaceView(a);
        RelativeLayout.LayoutParams lp = new RelativeLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT); /* the whole screen: libVLC fits the film in it */
        if (old != null) { RelativeLayout.LayoutParams op = (RelativeLayout.LayoutParams) old.getLayoutParams(); lp.width = op.width; lp.height = op.height; lp.addRule(RelativeLayout.CENTER_IN_PARENT);
            if (boxed) { v.setPivotX(old.getPivotX()); v.setPivotY(old.getPivotY()); } /* (a box's: kept; else the new one scales about its middle) */ v.setScaleX(old.getScaleX()); v.setScaleY(old.getScaleY()); v.setTranslationX(old.getTranslationX()); v.setTranslationY(old.getTranslationY()); }
        l.addView(v, 0, lp); view = v;
        /* the surface replaced under a film (a turn, back from the background, picture in picture): pictures must flow
           again on the new one within a moment, else the film's output is attached to it afresh and opened there again */
        v.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override public void surfaceCreated(SurfaceHolder h) { surfAt = System.nanoTime(); if (lastUrl != null && live == 1) { final int d0 = shown(); ui.postDelayed(() -> { if (player != null && lastUrl != null && live == 1 && state == 2 && !userPaused && hasVideo() && (vouts == 0 || shown() == d0)) recover("the surface was replaced"); }, 1200); } }
            @Override public void surfaceChanged(SurfaceHolder h, int f, int w, int hh) { }
            @Override public void surfaceDestroyed(SurfaceHolder h) { } });
        if (old != null) l.removeView(old); /* (its last picture gone with it: black under the page until the next one's first) */
    }
    private static IVLCVout.OnNewVideoLayoutListener layoutL; private static volatile int vouts; private static long surfAt; private static int recoveries;
    /* the pictures shown so far (libVLC's own count), -1 unknown */
    private static int shown() { try { org.videolan.libvlc.interfaces.IMedia m = player.getMedia(); if (m == null) return -1; org.videolan.libvlc.interfaces.IMedia.Stats st = m.getStats(); m.release(); return st != null ? st.displayedPictures : -1; } catch (Throwable e) { return -1; } }
    /* the film has a picture at all (a radio stream, an audio file: never watched for one) */
    private static boolean hasVideo() { if (vouts > 0) return true; try { org.videolan.libvlc.interfaces.IMedia m = player.getMedia(); if (m == null) return false; try { for (int i = 0; i < m.getTrackCount(); i++) { org.videolan.libvlc.interfaces.IMedia.Track t = m.getTrack(i); if (t != null && t.type == org.videolan.libvlc.interfaces.IMedia.Track.Type.Video) return true; } } finally { m.release(); } } catch (Throwable e) { } return false; }
    /* the film's output attached to the surface afresh and the film opened again where it is (at most 4 times a film) */
    private static void recover(String why) { if (player == null || lastUrl == null || recoveries >= 4) return; recoveries++; long at = Math.max(0, player.getTime());
        android.util.Log.i("StremioOrigin", "[video] no picture (" + why + "): its output attached afresh, opened again at " + at + " ms (" + recoveries + ")");
        try { IVLCVout out = player.getVLCVout(); out.detachViews(); out.setVideoView(view); out.attachViews(layoutL); if (view.getWidth() > 0 && view.getHeight() > 0 && !swDecode) out.setWindowSize(view.getWidth(), view.getHeight()); } catch (Throwable e) { }
        reopen(at); }
    /* a stream that stops sending without an error (the picture frozen, the time still): after 10 s playing in place,
       open it again from where it was, three times per film at most */
    private static int stalls;
    private static final Runnable watch = new Runnable() { public void run() {
        if (player == null) return;
        long since = posAt == 0 ? 0 : (System.nanoTime() - posAt) / 1000000;
        if (state == 2 && live == 1 && since > 10000 && stalls < 3 && lastUrl != null && !userPaused && player.isPlaying()) { /* (paused by hand: never "stuck", whatever state libVLC says) */ stalls++; posAt = System.nanoTime(); android.util.Log.i("StremioOrigin", "[video] stuck at " + pos + " ms: again (" + stalls + ")"); reopen(pos); }
        ui.postDelayed(this, 2000);
    } };
    /* a picture that stops while the clock and the sound go on (a far seek in a downloaded file can leave the decoder
       without a picture): libVLC's own count of pictures shown is watched; the clock 1.5 s on with none new, the decoder
       is set going again at the same point (a precise seek), and if that does not do it, the film is opened again there */
    /* the playing clock itself, read often on the main thread: no waiting for libVLC's time events (they come late when
       the decoder is busy); and the picture's size from its track when the layout has not said it (a stream with sound alone stays at 0) */
    private static final Runnable clockWatch = new Runnable() { public void run() {
        MediaPlayer mp = player;
        if (mp != null && (state == 2 || state == 3)) { long t = mp.getTime(); if (t > 0 && t != pos) { pos = t; posAt = System.nanoTime(); } }
        if (mp != null && vw <= 0 && state == 2 && live == 1 && System.nanoTime() - trackAt > 500000000L) { trackAt = System.nanoTime(); try { org.videolan.libvlc.interfaces.IMedia.VideoTrack t = mp.getCurrentVideoTrack(); if (t != null && t.width > 0 && t.height > 0) { vw = t.width; vh = t.height; if (t.sarNum > 0 && t.sarDen > 0 && t.sarNum != t.sarDen) vw = vw * t.sarNum / t.sarDen; } } catch (Throwable e) { } }
        ui.postDelayed(this, 40); } };
    private static int dispN = -1, freezeFixes; private static long dispPos, fixAt, clockT = -1, clockAt; private static volatile long dispAt; /* dispAt: when a new picture was last shown */
    private static final Runnable pictureWatch = new Runnable() { public void run() {
        MediaPlayer mp = player; if (mp == null) return;
        try { boolean fileNow = lastUrl != null && lastUrl.startsWith("file:"); if (state == 2 && live == 1 && (vw > 0 || hasVideo())) { /* (a film with a picture: its size known or not, a missing output is the case to catch) */ /* (playing only: 3 is paused, never "stuck") */ org.videolan.libvlc.interfaces.IMedia m = mp.getMedia(); /* (paused is not watched: its time stands still on purpose) */ if (m != null) { org.videolan.libvlc.interfaces.IMedia.Stats st = m.getStats(); m.release();
              if (st != null) { long t = mp.getTime();
                boolean local = lastUrl != null && lastUrl.startsWith("file:"); long nowN = System.nanoTime();
                if (t != clockT) { clockT = t; clockAt = nowN; } else if (local && seekWant < 0 && settleTo < 0 && nowN - clockAt > 2500000000L && nowN - fixAt > 3000000000L) { fixAt = nowN; clockAt = nowN; android.util.Log.i("StremioOrigin", "[video] stuck at " + t + " ms (a file on the phone): opened again there"); reopen(t); } /* the clock itself stuck: opened again at that place */
                if (st.displayedPictures != dispN) { dispN = st.displayedPictures; dispPos = t; dispAt = System.nanoTime(); if (System.nanoTime() - fixAt > 8000000000L) freezeFixes = 0; }
                else if (t - dispPos > 1500) { long now = System.nanoTime(); dispPos = t;
                  if (vouts == 0) { freezeFixes = 0; fixAt = now; recover("no video output"); } /* (nothing to show it on: a nudge would not help) */
                  else if (freezeFixes == 0) { freezeFixes = 1; fixAt = now; android.util.Log.i("StremioOrigin", "[video] picture frozen at " + t + " ms, the clock going on: set going again"); mp.setTime(t, false); }
                  else { freezeFixes = 0; fixAt = now; recover("picture still frozen"); } } } } }
          else dispN = -1; } catch (Throwable e) { }
        ui.postDelayed(this, 500);
    } };
    /* a small copy of the picture, for the glass of the player's panels (it shows the film blurred): asked for, then
       taken once ready, as {width, height, ARGB pixels...} */
    private static volatile int[] snapOut; private static volatile boolean snapBusy;
    public static void snap(final int w, final int h) { run(() -> {
        if (view == null || snapBusy || live == 0 || Build.VERSION.SDK_INT < 24 || w < 4 || h < 4) return;
        try { final Bitmap b = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888); snapBusy = true;
            PixelCopy.request(view, b, r -> { snapBusy = false; if (r == PixelCopy.SUCCESS) { int[] o = new int[2 + w * h]; o[0] = w; o[1] = h; b.getPixels(o, 2, w, 0, 0, w, h); snapOut = o; } b.recycle(); }, ui);
        } catch (Throwable t) { snapBusy = false; } }); }
    public static int[] snapTake() { int[] o = snapOut; snapOut = null; return o; }
    /* {state, pos ms, duration ms, buffered %, width, height, live, ms since pos was read, a picture (an output or a size)} */
    private static long trackAt;
    public static long[] poll() {
        /* (the native side's thread never calls libVLC: a player being let go holds its lock until it ends, and the app would
           wait with it; the clock and the picture's size are read on the main thread, clockWatch, and kept here) */
        long since = posAt == 0 ? 0 : (System.nanoTime() - posAt) / 1000000; return new long[] { state, pos, dur, (long) buffering, vw, vh, live, since, vouts > 0 || vw > 0 ? 1 : 0 }; }
    public static String error() { return error; }
    public static boolean playing() { return player != null && live == 1 && (state == 2 || state == 4); } /* a film running: picture in picture on leaving */

    /* old video codecs (MPEG-4 part 2 as in XviD/DivX, H.263, WMV/VC-1, MPEG-2): phones' hardware decoders show them
       broken into blocks, so they are decoded in software */
    private static boolean swDecode;
    private static boolean legacyVideo() { try { org.videolan.libvlc.interfaces.IMedia m = player.getMedia(); if (m == null) return false; try { for (int i = 0; i < m.getTrackCount(); i++) { org.videolan.libvlc.interfaces.IMedia.Track t = m.getTrack(i);
        if (t != null && t.type == org.videolan.libvlc.interfaces.IMedia.Track.Type.Video && t.codec != null) { String c = (t.codec + " " + (t.originalCodec != null ? t.originalCodec : "")).toLowerCase(java.util.Locale.ROOT); /* libVLC names them ("Xvid MPEG-4 Video", "MPEG-1/2 Video") */
          if (c.contains("mpeg-4 video") || c.contains("xvid") || c.contains("divx") || c.contains("h263") || c.contains("h.263") || c.contains("wmv") || c.contains("windows media video") || c.contains("vc-1") || c.contains("mpeg-1/2 video") || c.contains("mpeg-2 video")) return true; } } } finally { m.release(); } } catch (Throwable e) { } return false; }
    public static void liveRejoin() { run(() -> { if (player != null && lastUrl != null) { userPaused = false; open(lastUrl, 0); } }); } /* a live broadcast after a pause: opened again, at the live edge */
    static volatile boolean liveSrc; /* a live broadcast: opened again, it joins it where it is now */
    private static void reopen(long atMs) { if (player == null || lastUrl == null) return; int spu = spuWanted; open(lastUrl, liveSrc ? 0 : atMs); if (spu != -2) { spuWanted = spu; } }
    private static void open(String url, long startMs) {
        Media m = new Media(vlc, Uri.parse(url)); m.setHWDecoderEnabled(!swDecode, false); if (url.startsWith("file:")) m.addOption(":file-caching=150");
        m.addOption(":http-user-agent=StremioOrigin/0.1 (Android; phone)"); m.addOption(":sub-language=none"); m.addOption(":sub-track=-1");
        m.addOption(":http-reconnect"); if (startMs > 0) m.addOption(":start-time=" + (startMs / 1000.0));
        fresh(); if (player == null) { m.release(); return; } player.setMedia(m); m.release(); player.play();
    }
    /* play once the screen is sideways with its surface ready (at most 1.5 s, then at once whatever it is): the output
       attached afresh to that surface first */
    private static void playWhenTurned(final long since) {
        if (player == null) return; boolean wide = view != null && (!wantFull || view.getWidth() > view.getHeight()) && view.getHolder().getSurface() != null && view.getHolder().getSurface().isValid();
        boolean settled = wide && System.nanoTime() - surfAt > 150000000L; /* (the surface a moment old: the turn over) */
        if (!settled && System.nanoTime() - since < 1500000000L) { ui.postDelayed(() -> playWhenTurned(since), 40); return; }
        try { IVLCVout out = player.getVLCVout(); if (out.areViewsAttached()) out.detachViews(); out.setVideoView(view); out.attachViews(layoutL); if (view.getWidth() > 0 && view.getHeight() > 0) out.setWindowSize(view.getWidth(), view.getHeight()); } catch (Throwable e) { }
        if (!userPaused) player.play(); }
    public static void load(final String url, final String headers, final long startMs) {
        userPaused = false; run(() -> { lastUrl = url; retries = 0; stalls = 0;
            ensure(); if (player == null) return; fresh(); if (player == null) return; /* (the stream before let go off the main thread) */
            Media m = new Media(vlc, Uri.parse(url));
            swDecode = false; placeView(); m.setHWDecoderEnabled(true, false);
            m.addOption(":http-user-agent=StremioOrigin/0.1 (Android; phone)");
            m.addOption(":sub-language=none"); m.addOption(":sub-track=-1"); /* no subtitles of its own choosing: the player shows the ones you picked */
            if (url.startsWith("file:")) { m.addOption(":file-caching=150"); } /* a file on the phone: a small read-ahead, seeks resolve at once */
            if (startMs > 0) m.addOption(":start-time=" + (startMs / 1000.0));
            player.setMedia(m); m.release();
            error = ""; live = 0; vw = vh = 0; sarN = sarD = 1; pinchK = -1; pos = startMs; dur = 0; posAt = 0; buffering = 0; state = 1; spuWanted = -2; recoveries = 0; vouts = 0;
            if (wantFull) full(true); /* (only the player turns the phone: a preview in its box plays as the phone is held) */
            playWhenTurned(System.nanoTime()); /* (the film starts once the phone is sideways and its surface is there: started in the middle of the turn, its output failed on the surface being replaced and the picture stayed black) */
        });
    }
    public static void unload() { run(() -> { lastUrl = null; /* (let go: nothing may open it again, not a recovery posted by the release thread, a retry or the surface coming back; it played on, unseen and heard) */ retire(); if (vlc != null && view != null) fresh(); /* (stopped off the main thread; a fresh one waiting) */ live = 0; state = 0; MainActivity.vPipUpdate(); if (view != null) { view.animate().cancel(); view.setTranslationX(0); view.setTranslationY(0); view.setScaleX(1); view.setScaleY(1); } boxed = false; state = 0; live = 0; error = ""; }); } /* (the player turns the screen back when it closes) */
    private static volatile boolean userPaused; /* paused by hand: the stall watch leaves it alone */
    public static void play() { userPaused = false; posAt = System.nanoTime(); run(() -> { if (player != null) player.play(); }); }
    public static void pause() { userPaused = true; { StackTraceElement[] st = new Throwable().getStackTrace(); android.util.Log.i("StremioOrigin", "[video] pause, from " + (st.length > 1 ? st[1].getMethodName() : "?") + (st.length > 2 ? " < " + st[2].getMethodName() : "")); } run(() -> { if (player != null) player.pause(); }); }
    /* a file on the phone: a precise seek (its picture with it); several in a quick run (taps, jumps) go as one, to the
       last place asked, so the decoder never works through a backlog of them and hangs */
    private static volatile long seekWant = -1; private static long preciseAt, settleTo = -1;
    private static final Runnable seekSettle = () -> { long t = settleTo; settleTo = -1; if (player != null && t >= 0) { player.setTime(t, false); preciseAt = System.nanoTime(); dispN = -1; } }; /* (after a quick one: the exact place, once things are calm) */
    private static final Runnable seekGo = () -> { long t = seekWant; seekWant = -1; if (player == null || t < 0) return; long now = System.nanoTime(); dispN = -1;
        if (now - preciseAt < 400000000L) { player.setTime(t, true); settleTo = t; ui.removeCallbacks(seekSettle); ui.postDelayed(seekSettle, 450); } /* precise seeks spaced out: in a storm of them, quick ones, then the exact one */
        else { player.setTime(t, false); preciseAt = now; } };
    public static void seek(final long ms) { run(() -> { if (player == null) return; boolean local = lastUrl != null && lastUrl.startsWith("file:"); pos = ms; posAt = System.nanoTime(); dispN = -1;
        if (local) { ui.removeCallbacks(seekGo); ui.removeCallbacks(seekSettle); seekWant = -1; settleTo = -1; player.setTime(ms, true); preciseAt = System.nanoTime(); } else player.setTime(ms); }); } /* (a file on the phone: at once, to the nearest key picture: the picture there straight away) */
    public static void volume(final float v) { run(() -> { if (player != null) player.setVolume(Math.round(v * 100)); }); }
    public static void rate(final float r) { run(() -> { if (player != null) player.setRate(r); }); }
    /* the picture's size: libVLC always fits the whole picture in the screen; "fill" and "zoom" grow the view itself
       (a GPU scale, so a pinch follows the fingers and a change glides): fill covers the screen, its edges cut, zoom a
       step further */
    private static String sizeMode = "fit"; private static int sarN = 1, sarD = 1; private static float pinchK = -1;
    public static void aspect(final String mode, final float unused) { run(() -> { sizeMode = mode == null || mode.isEmpty() ? "fit" : mode; applySize(); }); }
    /* decoded in software, libVLC draws the picture at its own size in the top corner of the surface: the view is then
       made the picture's fitted size, in the middle (as VLC for Android does); decoded in hardware it fits it itself */
    private static void placeView() {
        if (view == null) return; ViewGroup l = MainActivity.layout(); int dw = l != null ? l.getWidth() : 0, dh = l != null ? l.getHeight() : 0;
        RelativeLayout.LayoutParams p = (RelativeLayout.LayoutParams) view.getLayoutParams(); int w = ViewGroup.LayoutParams.MATCH_PARENT, h = ViewGroup.LayoutParams.MATCH_PARENT;
        if (swDecode && vw > 0 && vh > 0 && dw > 0 && dh > 0) { double k = Math.min((double) dw / vw, (double) dh / vh); w = (int) Math.round(vw * k); h = (int) Math.round(vh * k); }
        if (player != null && !inPip()) player.getVLCVout().setWindowSize(w > 0 ? w : dw, h > 0 ? h : dh); /* libVLC told the size it really draws into (not in the small window: its surface is kept as it was) */
        if (p.width == w && p.height == h) return; p.width = w; p.height = h; p.addRule(RelativeLayout.CENTER_IN_PARENT); view.setLayoutParams(p);
    }
    private static float fillK() {
        ViewGroup l = MainActivity.layout(); int dw = l != null ? l.getWidth() : 0, dh = l != null ? l.getHeight() : 0;
        if ((vw <= 0 || vh <= 0) && player != null) { org.videolan.libvlc.interfaces.IMedia.VideoTrack t = player.getCurrentVideoTrack(); if (t != null && t.width > 0 && t.height > 0) { vw = t.width; vh = t.height; if (t.sarNum > 0 && t.sarDen > 0 && t.sarNum != t.sarDen) vw = vw * t.sarNum / t.sarDen; } }
        if (vw <= 0 || vh <= 0 || dw <= 0 || dh <= 0) return 1;
        double va = (double) vw / vh, sa = (double) dw / dh; return (float) Math.max(va / sa, sa / va);
    }
    private static float modeK(String m) { float f = fillK(); return "zoom".equals(m) ? f * 1.25f : "fill".equals(m) ? f : 1; }
    /* not in a box: the picture scales about its own middle (a box pins it to the screen's middle as it was then; kept
       after a turn or the small window, a zoom or a pinch went off toward a corner) */
    private static void centrePivot() { if (view == null || boxed) return; if (Build.VERSION.SDK_INT >= 28) view.resetPivot(); else { view.setPivotX(view.getWidth() / 2f); view.setPivotY(view.getHeight() / 2f); } }
    private static void applySize() {
        if (player == null || view == null) return; player.setAspectRatio(null); player.setScale(0);
        if (pinchK > 0 || boxed || pip) return; /* the fingers have it; or it is in a box (or the small window) */
        centrePivot(); float k = modeK(sizeMode); view.animate().cancel();
        if (Math.abs(view.getScaleX() - k) < 0.002f) return;
        view.animate().scaleX(k).scaleY(k).setDuration(260).setInterpolator(new android.view.animation.DecelerateInterpolator(1.6f)).start();
    }
    /* a swipe down from the middle: the picture follows the finger (moved down by ty, a fraction of the screen, and
       made k as big), then either goes with the player or settles back */
    public static void drag(final float tx, final float ty, final float k) { run(() -> { if (view == null || pip) return; view.animate().cancel(); float m = modeK(sizeMode); ViewGroup l = MainActivity.layout(); float w = l != null ? l.getWidth() : 0, h = l != null ? l.getHeight() : 0;
        view.setTranslationX(tx * w); view.setTranslationY(ty * h); view.setScaleX(m * k); view.setScaleY(m * k); }); } /* (set each frame by the native side: one motion with the corners it draws) */
    /* a pinch: k is the fingers' spread since it began (1 = as it was); live, then the nearest size on release */
    private static float pinchFrom = 1;
    public static void pinch(final float k) { run(() -> { if (view == null) return; if (pinchK < 0) { view.animate().cancel(); pinchFrom = view.getScaleX(); centrePivot(); }
        float z = modeK("zoom"), v = pinchFrom * k; if (v < 1) v = 1 - (1 - v) * 0.3f; if (v > z) v = z + (v - z) * 0.3f; /* past the ends: it gives a little */
        pinchK = v; view.setScaleX(v); view.setScaleY(v); }); }
    /* the size the pinch ended nearest, among fit, fill and zoom (scales compared by ratio) */
    public static String pinchEnd(float k) { if (view == null) return sizeMode; float v = pinchFrom * k; String best = "fit"; double bd = 1e9;
        for (String m : new String[] { "fit", "fill", "zoom" }) { double d = Math.abs(Math.log(v / modeK(m))); if (d < bd) { bd = d; best = m; } }
        run(() -> pinchK = -1); return best; }
    /* where the picture goes: libVLC fits it in the whole screen itself (the view covers it, as in VLC for Android); the
       native side's rectangle only tells the screen's size */
    public static void window(final int x, final int y, final int w, final int h) {
        run(() -> { if (view == null || player == null || inPip()) return; ViewGroup l = MainActivity.layout(); if (l == null || l.getWidth() <= 0) return; int dw = l.getWidth(), dh = l.getHeight(); /* (going into the small window or in it: no box, nothing told to libVLC) */
            if (!boxed) player.getVLCVout().setWindowSize(dw, dh);
            boolean full = w <= 0 || h <= 0 || ((Math.abs(w - dw) <= 4 || Math.abs(h - dh) <= 4) && Math.abs(x + w / 2f - dw / 2f) <= 4 && Math.abs(y + h / 2f - dh / 2f) <= 4); /* the whole screen, or the picture fitted in it (the player's own rectangle): its own size */
            if (full == boxed) android.util.Log.i("StremioOrigin", "[video] view " + (full ? "full" : "box") + " " + x + "," + y + " " + w + "x" + h + " of " + dw + "x" + dh);
            if (full) { if (boxed) { boxed = false; centrePivot(); view.setTranslationX(0); view.setTranslationY(0); float k = pinchK > 0 ? pinchK : modeK(sizeMode); view.setScaleX(k); view.setScaleY(k); } return; } /* the whole screen: the picture's own size again */
            /* a box (a channel's preview, or one growing into the player): the view, which fits the picture in the whole
               screen, moved and scaled as one so the picture fills that box (a GPU transform: smooth each frame) */
            fillK(); double va = vw > 0 && vh > 0 ? (double) vw / vh : 16.0 / 9, sa = (double) dw / dh; double pw = va > sa ? dw : dh * va, ph = va > sa ? dw / va : dh;
            float k = (float) Math.min(w / pw, h / ph); /* (fitted in the box as on the whole screen: a box growing to the screen ends exactly at the picture's own size, no snap) */
            boxed = true; view.animate().cancel(); view.setPivotX(dw / 2f); view.setPivotY(dh / 2f); view.setScaleX(k); view.setScaleY(k); view.setTranslationX(x + w / 2f - dw / 2f); view.setTranslationY(y + h / 2f - dh / 2f); });
    }
    private static boolean boxed;
    /* picture in picture: the picture fills the small window as it is (a channel's preview box, a swipe or a pinch would
       move it out of it: black); back in the app, the native side's next rectangle places it again */
    static volatile boolean pip;
    /* in the small window, or going into it (the window shrinks before Android says so) */
    private static long pipOutAt; /* (just out of it: Android may say so a moment longer, the screen's size must reach libVLC) */
    private static boolean inPip() { return pip || (MainActivity.pipNow() && System.nanoTime() - pipOutAt > 2000000000L); }
    /* the small window: once it has its size (Android first turns the app to the screen's way, full size, then
       shrinks it), the video surface is given one fixed size of that shape (the film's width, 1280 at most) and libVLC
       is told it once; growing or shrinking the window (a double tap) then only scales that surface: libVLC's output is
       never remade on the way (remade, the first frames came out at the old size in a corner, then black until the
       next key frame) */
    private static boolean pipFixed;
    private static void pipFit() { if (view == null || player == null || pipFixed || !pip || swDecode) return; ViewGroup l = MainActivity.layout(); if (l == null) return; int w = l.getWidth(), h = l.getHeight(); if (w <= 0 || h <= 0) return;
        android.util.DisplayMetrics dm = new android.util.DisplayMetrics(); try { ((android.view.WindowManager) l.getContext().getSystemService(android.content.Context.WINDOW_SERVICE)).getDefaultDisplay().getRealMetrics(dm); } catch (Throwable e) { return; } if ((long) w * h > (long) dm.widthPixels * dm.heightPixels * 6 / 10) return; /* (the real screen: in the small window the app's own metrics are the window's) */ /* (still full size: the small one comes next) */
        int fw = vw > 0 ? (int) Math.min(vw, 1280) : 1280, fh = Math.max(2, Math.round(fw * (float) h / w)); try { view.getHolder().setFixedSize(fw, fh); } catch (Throwable e) { } player.getVLCVout().setWindowSize(fw, fh); pipFixed = true;
        android.util.Log.i("StremioOrigin", "[pip] the picture held at " + fw + "x" + fh + " for a " + w + "x" + h + " window"); }
    public static void pipMode(final boolean in) { run(() -> { pip = in; if (view == null) return; boxed = false; centrePivot(); { ViewGroup l0 = MainActivity.layout(); android.graphics.Rect fr = view.getHolder().getSurfaceFrame(); android.util.Log.i("StremioOrigin", "[pip] " + (in ? "in" : "out") + ": layout " + (l0 != null ? l0.getWidth() + "x" + l0.getHeight() : "-") + ", view " + view.getWidth() + "x" + view.getHeight() + " at " + view.getLeft() + "," + view.getTop() + " t " + view.getTranslationX() + "," + view.getTranslationY() + " s " + view.getScaleX() + ", surface " + fr.width() + "x" + fr.height() + ", video " + vw + "x" + vh); } view.animate().cancel(); boxed = false; view.setTranslationX(0); view.setTranslationY(0);
        float k = in ? 1 : (pinchK > 0 ? pinchK : modeK(sizeMode)); view.setScaleX(k); view.setScaleY(k);
        /* the small window (and a double tap growing or shrinking it): the video surface keeps the size it had on the
           screen and Android scales it; libVLC is told nothing, so its output is never remade (remade, the first frames
           came out at the old size in a corner, then black until the next key frame) */
        ViewGroup l = MainActivity.layout();
        if (in) pipFit();
        else { pipFixed = false; pipOutAt = System.nanoTime(); try { view.getHolder().setSizeFromLayout(); } catch (Throwable e) { } if (player != null && l != null && l.getWidth() > 0 && !swDecode) player.getVLCVout().setWindowSize(l.getWidth(), l.getHeight()); } }); }
    /* the tracks of a type (1 audio, 2 text) as "id|name|selected" */
    public static String[] tracks(final int type) {
        final ArrayList<String> out = new ArrayList<>(); final Object lock = new Object(); final boolean[] done = { false };
        run(() -> {
            if (player != null) { MediaPlayer.TrackDescription[] t = type == 1 ? player.getAudioTracks() : player.getSpuTracks(); int cur = type == 1 ? player.getAudioTrack() : player.getSpuTrack();
                if (t != null) for (MediaPlayer.TrackDescription d : t) if (d.id >= 0) out.add(d.id + "|" + (d.name != null ? d.name : "") + "|" + (d.id == cur ? 1 : 0)); }
            synchronized (lock) { done[0] = true; lock.notifyAll(); }
        });
        synchronized (lock) { long end = System.currentTimeMillis() + 300; while (!done[0]) { long w = end - System.currentTimeMillis(); if (w <= 0) break; try { lock.wait(w); } catch (InterruptedException e) { break; } } }
        return out.toArray(new String[0]);
    }
    /* a track by its id (-1: subtitles off) */
    public static void select(final int type, final int id) { run(() -> { if (player == null) return; if (type == 1) player.setAudioTrack(id); else { spuWanted = id; player.setSpuTrack(id); } }); }
    public static void addSubtitle(final String url, final boolean select) { run(() -> { if (player != null) player.addSlave(Media.Slave.Type.Subtitle, Uri.parse(url), select); }); }
    public static void spuDelay(final long us) { run(() -> { if (player != null) player.setSpuDelay(us); }); }

    private static volatile boolean wantFull; /* the player asked for the phone sideways (a preview never does) */
    public static void fullscreen(final boolean on) { wantFull = on; run(() -> full(on)); }
    /* how the phone is held (its sensor, whatever the screen is locked to): 1 upright, 0 sideways, -1 flat or not known;
       upright counts only when the phone turns by itself (auto-rotate on) */
    private static volatile int held = -1; private static android.view.OrientationEventListener heldL;
    public static int held() {
        MainActivity a = MainActivity.self; if (a == null) return -1;
        if (heldL == null) { run(() -> { if (heldL != null) return; heldL = new android.view.OrientationEventListener(a) { @Override public void onOrientationChanged(int d) {
            if (d == ORIENTATION_UNKNOWN) { held = -1; return; } held = (d <= 30 || d >= 330) ? 1 : ((d >= 60 && d <= 120) || (d >= 240 && d <= 300)) ? 0 : held; } }; if (heldL.canDetectOrientation()) heldL.enable(); }); return -1; }
        try { if (android.provider.Settings.System.getInt(a.getContentResolver(), android.provider.Settings.System.ACCELEROMETER_ROTATION, 0) == 0) return held == 1 ? -1 : held; } catch (Throwable e) { }
        return held; }
    /* a film open: the phone sideways and the system bars away; closed: upright with its bars */
    private static void full(boolean on) {
        MainActivity a = MainActivity.self; if (a == null) return; MainActivity.brightnessPlayer(on);
        { WindowManager.LayoutParams lp = a.getWindow().getAttributes(); lp.rotationAnimation = WindowManager.LayoutParams.ROTATION_ANIMATION_ROTATE; a.getWindow().setAttributes(lp); } /* every turn the system's own rotation (in, out, and any turn after: a cut left set made every later turn abrupt) */
        a.setRequestedOrientation(on ? ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE : ActivityInfo.SCREEN_ORIENTATION_FULL_USER);
        if (Build.VERSION.SDK_INT >= 30) { WindowInsetsController c = a.getWindow().getInsetsController();
            if (c != null) { if (on) { c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE); c.hide(WindowInsets.Type.systemBars()); } else c.show(WindowInsets.Type.systemBars()); } }
        else a.getWindow().getDecorView().setSystemUiVisibility(on ? (View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY)
            : (View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION));
        a.getWindow().getDecorView().setKeepScreenOn(on);
    }
}
