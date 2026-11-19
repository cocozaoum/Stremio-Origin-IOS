// The formatting is rough and I'm not going to bother. Pull requests that fix it are welcome; complaints without one help neither me nor the end user.
package app.stremioorigin.phone;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.graphics.Bitmap;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.os.Build;
import android.os.IBinder;

/* While something plays on a TV from this phone: a media notification (its poster, its name, 10 s back, play/pause,
   10 s on, stop) that is also on the lock screen and answers headset and Bluetooth buttons, through a media session;
   it keeps the app running in the background so the TV can still be driven. What is pressed goes to the app (taken
   about once a second, or at once when it is in front); gone when nothing plays on the TV any more. */
public class CastService extends Service {
    static final String CH = "cast"; static final int ID = 7201;
    static final int A_TOGGLE = 1, A_BACK = 2, A_FWD = 3, A_STOP = 4, A_PLAY = 5, A_PAUSE = 6, A_SEEK = 7;
    static volatile String title = "", sub = "", art = ""; static volatile boolean playing, running; static volatile long posMs, durMs;
    static MediaSession session;
    /* connected to a TV: the Wi-Fi kept at full power and the CPU awake enough for the link, so Android does not put the
       connection to sleep with the screen off (both let go once disconnected) */
    static android.net.wifi.WifiManager.WifiLock wifiLock; static android.os.PowerManager.WakeLock wakeLock;
    static void holdOn(Context c) { try { if (wifiLock == null) { android.net.wifi.WifiManager wm = (android.net.wifi.WifiManager) c.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if (wm != null) { wifiLock = wm.createWifiLock(Build.VERSION.SDK_INT >= 29 ? android.net.wifi.WifiManager.WIFI_MODE_FULL_LOW_LATENCY : android.net.wifi.WifiManager.WIFI_MODE_FULL_HIGH_PERF, "StremioOrigin:cast"); wifiLock.setReferenceCounted(false); wifiLock.acquire(); } } } catch (Throwable e) { }
        try { if (wakeLock == null) { android.os.PowerManager pm = (android.os.PowerManager) c.getSystemService(Context.POWER_SERVICE); if (pm != null) { wakeLock = pm.newWakeLock(android.os.PowerManager.PARTIAL_WAKE_LOCK, "StremioOrigin:cast"); wakeLock.setReferenceCounted(false); wakeLock.acquire(); } } } catch (Throwable e) { } }
    static void holdOff() { try { if (wifiLock != null) { wifiLock.release(); wifiLock = null; } } catch (Throwable e) { } try { if (wakeLock != null) { wakeLock.release(); wakeLock = null; } } catch (Throwable e) { } }
    static final java.util.concurrent.ConcurrentLinkedQueue<long[]> actions = new java.util.concurrent.ConcurrentLinkedQueue<>();
    /* the app asks: the next press, {kind, value ms}, or null */
    public static long[] take() { return actions.poll(); }
    static final java.util.Map<String, Bitmap> pics = new java.util.concurrent.ConcurrentHashMap<>(); static final java.util.Set<String> asked = java.util.Collections.newSetFromMap(new java.util.concurrent.ConcurrentHashMap<>());
    static Bitmap pic(String url) { if (url == null || url.isEmpty()) return null; Bitmap b = pics.get(url); if (b != null) return b;
        if (asked.add(url)) new Thread(() -> { try { java.net.HttpURLConnection c = (java.net.HttpURLConnection) new java.net.URL(url).openConnection(); c.setConnectTimeout(8000); c.setReadTimeout(8000); c.setRequestProperty("User-Agent", "Mozilla/5.0");
            Bitmap src = android.graphics.BitmapFactory.decodeStream(c.getInputStream()); c.disconnect(); if (src == null) return;
            int W = Math.min(src.getWidth(), 720); Bitmap s = W < src.getWidth() ? Bitmap.createScaledBitmap(src, W, src.getHeight() * W / src.getWidth(), true) : src; pics.put(url, s); refresh(); } catch (Throwable e) { } }).start();
        return null; }
    static void channel(Context c) { if (Build.VERSION.SDK_INT < 26) return; NotificationManager nm = c.getSystemService(NotificationManager.class); if (nm == null) return;
        NotificationChannel a = new NotificationChannel(CH, "Playing on a TV", NotificationManager.IMPORTANCE_LOW); a.setShowBadge(false); a.setLockscreenVisibility(Notification.VISIBILITY_PUBLIC); nm.createNotificationChannel(a); }
    static PendingIntent act(Context c, int a) { Intent i = new Intent(c, CastService.class).setAction("cast." + a); return PendingIntent.getService(c, 100 + a, i, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE); }
    static PendingIntent open(Context c) { Intent i = new Intent(c, MainActivity.class).setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT); return PendingIntent.getActivity(c, 2, i, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE); }
    /* (made from the app's own thread, which has no message loop: its presses are delivered on the main one, else the
       callback is never set and the notification's buttons, Android 13 on, do nothing) */
    static synchronized void ensureSession(Context c) { if (session != null) return; MediaSession made = new MediaSession(c, "StremioOriginCast");
        made.setCallback(new MediaSession.Callback() {
            @Override public void onPlay() { actions.add(new long[] { A_PLAY, 0 }); }
            @Override public void onPause() { actions.add(new long[] { A_PAUSE, 0 }); }
            @Override public void onStop() { actions.add(new long[] { A_STOP, 0 }); }
            @Override public void onFastForward() { actions.add(new long[] { A_FWD, 0 }); }
            @Override public void onRewind() { actions.add(new long[] { A_BACK, 0 }); }
            @Override public void onSkipToNext() { actions.add(new long[] { A_FWD, 0 }); }
            @Override public void onSkipToPrevious() { actions.add(new long[] { A_BACK, 0 }); }
            @Override public void onSeekTo(long ms) { actions.add(new long[] { A_SEEK, ms }); } }, new android.os.Handler(android.os.Looper.getMainLooper()));
        made.setSessionActivity(open(c)); made.setActive(true); session = made; }
    static void sessionState() { if (session == null) return;
        PlaybackState.Builder p = new PlaybackState.Builder().setActions(PlaybackState.ACTION_PLAY | PlaybackState.ACTION_PAUSE | PlaybackState.ACTION_PLAY_PAUSE | PlaybackState.ACTION_STOP | PlaybackState.ACTION_SEEK_TO | PlaybackState.ACTION_FAST_FORWARD | PlaybackState.ACTION_REWIND | PlaybackState.ACTION_SKIP_TO_NEXT | PlaybackState.ACTION_SKIP_TO_PREVIOUS)
            .setState(playing ? PlaybackState.STATE_PLAYING : PlaybackState.STATE_PAUSED, posMs, playing ? 1f : 0f);
        session.setPlaybackState(p.build());
        MediaMetadata.Builder m = new MediaMetadata.Builder().putString(MediaMetadata.METADATA_KEY_TITLE, title).putString(MediaMetadata.METADATA_KEY_ARTIST, sub).putLong(MediaMetadata.METADATA_KEY_DURATION, durMs);
        Bitmap b = pic(art); if (b != null) m.putBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART, b);
        session.setMetadata(m.build()); }
    static Notification build(Context c) { Notification.Builder b = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(c, CH) : new Notification.Builder(c);
        b.setSmallIcon(R.drawable.ic_stat_origin).setColor(0xFF8C5CFF).setContentTitle(title).setContentText(sub).setOngoing(true).setOnlyAlertOnce(true).setShowWhen(false).setContentIntent(open(c)).setVisibility(Notification.VISIBILITY_PUBLIC).setCategory(Notification.CATEGORY_TRANSPORT);
        Bitmap p = pic(art); if (p != null) b.setLargeIcon(p);
        b.addAction(new Notification.Action.Builder(android.graphics.drawable.Icon.createWithResource(c, android.R.drawable.ic_media_rew), "-10", act(c, A_BACK)).build());
        b.addAction(new Notification.Action.Builder(android.graphics.drawable.Icon.createWithResource(c, playing ? android.R.drawable.ic_media_pause : android.R.drawable.ic_media_play), playing ? "Pause" : "Play", act(c, A_TOGGLE)).build());
        b.addAction(new Notification.Action.Builder(android.graphics.drawable.Icon.createWithResource(c, android.R.drawable.ic_media_ff), "+10", act(c, A_FWD)).build());
        b.addAction(new Notification.Action.Builder(android.graphics.drawable.Icon.createWithResource(c, android.R.drawable.ic_menu_close_clear_cancel), "Stop", act(c, A_STOP)).build());
        Notification.MediaStyle st = new Notification.MediaStyle().setShowActionsInCompactView(0, 1, 2); if (session != null) st.setMediaSession(session.getSessionToken()); b.setStyle(st);
        return b.build(); }
    static void refresh() { MainActivity a = MainActivity.self; if (a == null || !running) return; Context c = a.getApplicationContext(); sessionState(); NotificationManager nm = (NotificationManager) c.getSystemService(Context.NOTIFICATION_SERVICE); if (nm != null) try { nm.notify(ID, build(c)); } catch (Throwable e) { } }
    /* from the app: what plays on the TV (t null: nothing any more) */
    public static void update(String t, String s, String artUrl, boolean isPlaying, long pos, long dur) { MainActivity a = MainActivity.self; if (a == null) return; Context c = a.getApplicationContext();
        if (t == null) { holdOff(); if (running) { running = false; if (session != null) { session.setActive(false); session.release(); session = null; } c.stopService(new Intent(c, CastService.class)); } return; }
        holdOn(c); title = t; sub = s == null ? "" : s; art = artUrl == null ? "" : artUrl; playing = isPlaying; posMs = pos; durMs = dur; channel(c); ensureSession(c);
        if (!running) { running = true; Intent i = new Intent(c, CastService.class); try { if (Build.VERSION.SDK_INT >= 26) c.startForegroundService(i); else c.startService(i); } catch (Throwable e) { running = false; } return; }
        refresh(); }
    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        String ac = intent != null ? intent.getAction() : null; if (ac != null && ac.startsWith("cast.")) { try { actions.add(new long[] { Integer.parseInt(ac.substring(5)), 0 }); } catch (Throwable e) { } }
        channel(this); ensureSession(this); sessionState();
        try { if (Build.VERSION.SDK_INT >= 29) startForeground(ID, build(this), ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK); else startForeground(ID, build(this)); } catch (Throwable e) { stopSelf(); }
        if (!running) stopSelf(); return START_NOT_STICKY; }
    @Override public IBinder onBind(Intent i) { return null; }
}
