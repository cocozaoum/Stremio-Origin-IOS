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
import android.os.Build;
import android.os.IBinder;

/* While something downloads: a notification that keeps the user up to date (what, how far, how fast, how long left)
   and keeps the app running in the background so the downloads go on; gone when nothing downloads any more. A finished
   download says so in a notification of its own. A tap opens the app at Downloads. */
public class DownloadService extends Service {
    static final String CH = "downloads", CH_DONE = "downloads_done"; static final int ID = 7101;
    static volatile String title = "", text = "", sub = "", lines = "", art = ""; static volatile int pct = -1; static volatile boolean running;
    /* the show's logo on a dark rounded tile (the poster when there is none), fetched once per picture, off the main thread */
    static final java.util.Map<String, android.graphics.Bitmap> tiles = new java.util.concurrent.ConcurrentHashMap<>(); static final java.util.Set<String> asked = java.util.Collections.newSetFromMap(new java.util.concurrent.ConcurrentHashMap<>());
    static android.graphics.Bitmap tile(String url) { if (url == null || url.isEmpty()) return null; android.graphics.Bitmap b = tiles.get(url); if (b != null) return b;
        if (asked.add(url)) new Thread(() -> { try { java.net.HttpURLConnection c = (java.net.HttpURLConnection) new java.net.URL(url).openConnection(); c.setConnectTimeout(8000); c.setReadTimeout(8000); c.setRequestProperty("User-Agent", "Mozilla/5.0");
            android.graphics.Bitmap src = android.graphics.BitmapFactory.decodeStream(c.getInputStream()); c.disconnect(); if (src == null) return;
            int S = 256; android.graphics.Bitmap t = android.graphics.Bitmap.createBitmap(S, S, android.graphics.Bitmap.Config.ARGB_8888); android.graphics.Canvas cv = new android.graphics.Canvas(t);
            android.graphics.Paint p = new android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG | android.graphics.Paint.FILTER_BITMAP_FLAG); p.setColor(0xFF14121C); cv.drawRoundRect(0, 0, S, S, 48, 48, p);
            boolean logo = src.hasAlpha() && src.getWidth() > src.getHeight(); /* a logo: fitted on the tile; a poster: filling it */
            float k = logo ? Math.min(S * 0.84f / src.getWidth(), S * 0.6f / src.getHeight()) : Math.max((float) S / src.getWidth(), (float) S / src.getHeight());
            float w = src.getWidth() * k, h = src.getHeight() * k; android.graphics.RectF r = new android.graphics.RectF((S - w) / 2, logo ? (S - h) / 2 : 0, (S + w) / 2, (logo ? (S - h) / 2 : 0) + h);
            if (!logo) { android.graphics.Path clip = new android.graphics.Path(); clip.addRoundRect(0, 0, S, S, 48, 48, android.graphics.Path.Direction.CW); cv.clipPath(clip); }
            cv.drawBitmap(src, null, r, p); src.recycle(); tiles.put(url, t); refresh(); } catch (Throwable e) { } }).start();
        return null; }
    static void refresh() { MainActivity a = MainActivity.self; if (a == null || !running) return; Context c = a.getApplicationContext(); NotificationManager nm = (NotificationManager) c.getSystemService(Context.NOTIFICATION_SERVICE); if (nm != null) try { nm.notify(ID, build(c)); } catch (Throwable e) { } }
    static void channels(Context c) { if (Build.VERSION.SDK_INT < 26) return; NotificationManager nm = c.getSystemService(NotificationManager.class); if (nm == null) return;
        NotificationChannel a = new NotificationChannel(CH, "Downloads", NotificationManager.IMPORTANCE_LOW); a.setShowBadge(false); nm.createNotificationChannel(a);
        NotificationChannel b = new NotificationChannel(CH_DONE, "Finished downloads", NotificationManager.IMPORTANCE_DEFAULT); nm.createNotificationChannel(b); }
    static PendingIntent open(Context c) { Intent i = new Intent(c, MainActivity.class).setFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT).putExtra("openDownloads", true);
        return PendingIntent.getActivity(c, 1, i, PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE); }
    static Notification build(Context c) { Notification.Builder b = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(c, CH) : new Notification.Builder(c);
        b.setSmallIcon(R.drawable.ic_stat_origin).setColor(0xFF8C5CFF).setContentTitle(title).setContentText(text).setOngoing(true).setOnlyAlertOnce(true).setShowWhen(false).setContentIntent(open(c)).setCategory(Notification.CATEGORY_PROGRESS);
        if (sub != null && !sub.isEmpty()) b.setSubText(sub);
        android.graphics.Bitmap t = tile(art); if (t != null) b.setLargeIcon(t);
        if (lines != null && !lines.isEmpty()) b.setStyle(new Notification.BigTextStyle().bigText(text + "\n" + lines.trim())); /* opened: the whole, then each one (the progress bar kept) */
        if (pct >= 0) b.setProgress(100, pct, false); else if (pct == -1) b.setProgress(0, 0, true); /* -2: no bar (paused, waiting) */ return b.build(); }
    /* from the app, about once a second: what to say, or null to end */
    public static void update(String t, String x, int p, String subText, String lineList, String artUrl) { sub = subText; lines = lineList; art = artUrl; MainActivity a = MainActivity.self; if (a == null) return; Context c = a.getApplicationContext();
        if (t == null) { if (running) { running = false; c.stopService(new Intent(c, DownloadService.class)); } return; }
        title = t; text = x; pct = p; channels(c);
        if (!running) { running = true; Intent i = new Intent(c, DownloadService.class); try { if (Build.VERSION.SDK_INT >= 26) c.startForegroundService(i); else c.startService(i); } catch (Throwable e) { running = false; } return; }
        NotificationManager nm = (NotificationManager) c.getSystemService(Context.NOTIFICATION_SERVICE); if (nm != null) try { nm.notify(ID, build(c)); } catch (Throwable e) { } }
    public static void finished(String t, String x, String artUrl) { MainActivity a = MainActivity.self; if (a == null) return; Context c = a.getApplicationContext(); channels(c);
        Notification.Builder b = Build.VERSION.SDK_INT >= 26 ? new Notification.Builder(c, CH_DONE) : new Notification.Builder(c);
        b.setSmallIcon(R.drawable.ic_stat_origin).setColor(0xFF8C5CFF).setContentTitle(t).setContentText(x).setAutoCancel(true).setContentIntent(open(c)); android.graphics.Bitmap tl = tile(artUrl); if (tl != null) b.setLargeIcon(tl);
        NotificationManager nm = (NotificationManager) c.getSystemService(Context.NOTIFICATION_SERVICE); if (nm != null) try { nm.notify((t + x).hashCode(), b.build()); } catch (Throwable e) { } }
    @Override public int onStartCommand(Intent intent, int flags, int startId) { channels(this);
        try { if (Build.VERSION.SDK_INT >= 29) startForeground(ID, build(this), ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC); else startForeground(ID, build(this)); } catch (Throwable e) { stopSelf(); }
        if (!running) stopSelf(); return START_NOT_STICKY; }
    @Override public void onTimeout(int startId) { running = false; stopSelf(); } /* (Android 15's limit for a data-sync service) */
    @Override public IBinder onBind(Intent i) { return null; }
}
