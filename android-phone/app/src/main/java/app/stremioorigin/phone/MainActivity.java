// The formatting is rough and I'm not going to bother. Pull requests that fix it are welcome; complaints without one help neither me nor the end user.
package app.stremioorigin.phone;

import android.app.ActivityManager;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Locale;
import android.content.res.AssetManager;

public class MainActivity extends SDLActivity {
    static MainActivity self;
    private static WifiManager.MulticastLock mcLock;

    @Override
    protected String[] getLibraries() {
        return new String[] {"SDL2", "curl", "main"};
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        self = this; appCtx = getApplicationContext(); if (getIntent() != null && getIntent().getBooleanExtra("openDownloads", false)) openDl = true; /* (started from the downloads' notification) */
        /* development: test settings as the native side reads them from its environment
           (adb shell am start -n app.stremioorigin.phone/.MainActivity --es env "SB_UPD_GH=http://127.0.0.1:8765/r.json") */
        String env = getIntent() != null && (getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0 ? getIntent().getStringExtra("env") : null; /* (debug builds only: another app could start a release build with these) */
        if (env != null) for (String kv : env.split(";")) { int q = kv.indexOf('='); if (q > 0) try { android.system.Os.setenv(kv.substring(0, q).trim(), kv.substring(q + 1), true); } catch (Throwable e) { } }
        crashHook(this); exitInfo(this);
        copyAssets();
        writeVersion();
        super.onCreate(savedInstanceState);
        installStatus(getIntent()); uiWatchdog();
        if (env != null && env.contains("SB_TEST_JCRASH")) new Thread(() -> { try { Thread.sleep(6000); } catch (InterruptedException e) { } throw new IllegalStateException("test: a Java crash, on purpose"); }, "test-crash").start(); /* (development: proves the Java crash log) */
        if (Build.VERSION.SDK_INT >= 31) getSplashScreen().setOnExitAnimationListener(v -> v.remove()); /* the system's start card goes at once: the app's own start screen takes over */
        /* until the app has drawn its own first frames: a cover that is that first frame (the page's colour, the mark in
           the middle at its size), so no black shows between Android's start card and the app's start screen */
        try { android.widget.FrameLayout cover = new android.widget.FrameLayout(this); cover.setBackgroundColor(0xFF0C0B11);
            android.widget.ImageView mark = new android.widget.ImageView(this); mark.setImageResource(R.drawable.splash_mark);
            float dp = getResources().getDisplayMetrics().density; int side = Math.round(287 * dp); /* (the drawn circle: 236 layout units wide, 1.75 of them a dp; the picture's disc is 47 % of it) */
            android.widget.FrameLayout.LayoutParams lp2 = new android.widget.FrameLayout.LayoutParams(side, side, android.view.Gravity.CENTER); cover.addView(mark, lp2);
            if (mLayout != null) { mLayout.addView(cover, new android.view.ViewGroup.LayoutParams(android.view.ViewGroup.LayoutParams.MATCH_PARENT, android.view.ViewGroup.LayoutParams.MATCH_PARENT)); startCover = cover; cover.postDelayed(MainActivity::firstFrame, 5000); } } catch (Throwable e) { } /* (gone after 5 s whatever happens) */
        if (mSurface != null) { mSurface.setZOrderMediaOverlay(true); mSurface.getHolder().setFormat(android.graphics.PixelFormat.TRANSLUCENT); } /* the app's drawing over the video plane: clear where the picture shows */

        Window w = getWindow();
        topRefreshRate(w);
        if (Build.VERSION.SDK_INT >= 28) { WindowManager.LayoutParams lp = w.getAttributes(); /* drawn into the camera cutout's side too (films sideways use the whole screen) */
            lp.layoutInDisplayCutoutMode = Build.VERSION.SDK_INT >= 30 ? WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS : WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES; w.setAttributes(lp); }
        if (Build.VERSION.SDK_INT >= 30) {
            w.setDecorFitsSystemWindows(false);
        }
        w.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
        w.setStatusBarColor(0);
        w.setNavigationBarColor(0);
        if (Build.VERSION.SDK_INT < 30) {
            w.getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }

    /* The screen at its fastest refresh rate (90/120 Hz where the panel has it), at its current resolution:
       the drawing follows the panel's rate, cheap phones included. */
    private void topRefreshRate(Window w) {
        if (Build.VERSION.SDK_INT < 23) return;
        android.view.Display d = getWindowManager().getDefaultDisplay();
        android.view.Display.Mode cur = d.getMode(), best = cur;
        for (android.view.Display.Mode m : d.getSupportedModes())
            if (m.getPhysicalWidth() == cur.getPhysicalWidth() && m.getPhysicalHeight() == cur.getPhysicalHeight() && m.getRefreshRate() > best.getRefreshRate()) best = m;
        WindowManager.LayoutParams lp = w.getAttributes(); lp.preferredDisplayModeId = best.getModeId(); w.setAttributes(lp);
        android.util.Log.i("StremioOrigin", "refresh " + best.getRefreshRate() + " Hz (of " + d.getSupportedModes().length + " modes)");
    }

    /* The app reads its fonts, dictionaries and icons as files: copied out of the APK once per version. */
    private void copyAssets() {
        File dst = new File(getFilesDir(), "assets");
        File stamp = new File(dst, ".version");
        String ver = "";
        try { ver = String.valueOf(getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime); } catch (Exception e) { }
        try {
            if (stamp.exists() && ver.equals(new String(readAll(new FileInputStream(stamp)), "UTF-8"))) return;
            copyDir(getAssets(), "", dst);
            FileOutputStream o = new FileOutputStream(stamp); o.write(ver.getBytes("UTF-8")); o.close();
        } catch (IOException e) { android.util.Log.e("StremioOrigin", "assets: " + e); }
    }
    private static void copyDir(AssetManager am, String path, File dst) throws IOException {
        String[] names = am.list(path);
        if (names == null || names.length == 0) {
            if (path.isEmpty()) return;
            dst.getParentFile().mkdirs();
            InputStream in = am.open(path); FileOutputStream out = new FileOutputStream(dst);
            byte[] b = new byte[65536]; int n; while ((n = in.read(b)) > 0) out.write(b, 0, n);
            in.close(); out.close(); return;
        }
        dst.mkdirs();
        for (String n : names) {
            if (path.isEmpty() && (n.equals("images") || n.equals("webkit") || n.equals("geoid_map"))) continue; /* the system's own */
            copyDir(am, path.isEmpty() ? n : path + "/" + n, new File(dst, n));
        }
    }
    private static byte[] readAll(InputStream in) throws IOException {
        java.io.ByteArrayOutputStream o = new java.io.ByteArrayOutputStream(); byte[] b = new byte[4096]; int n;
        while ((n = in.read(b)) > 0) o.write(b, 0, n); in.close(); return o.toByteArray();
    }

    static volatile Context appCtx;
    /* the native app ended for good (the task swiped away): the services that kept it running in the background end too,
       their notifications with them, then the process, so that opening the app again starts a fresh one (a second run
       of the native side in this process would find it as the first left it) */
    public static void appEnded() { final Context c = appCtx; if (c == null) return;
        try { CastService.running = false; c.stopService(new android.content.Intent(c, CastService.class)); } catch (Throwable e) { }
        try { DownloadService.running = false; c.stopService(new android.content.Intent(c, DownloadService.class)); } catch (Throwable e) { }
        try { android.app.NotificationManager nm = (android.app.NotificationManager) c.getSystemService(Context.NOTIFICATION_SERVICE); if (nm != null) { nm.cancel(CastService.ID); nm.cancel(DownloadService.ID); } } catch (Throwable e) { }
        new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(() -> android.os.Process.killProcess(android.os.Process.myPid()), 400); }

    @Override
    protected void onDestroy() {
        if (self == this) {
            self = null;
        }
        super.onDestroy();
    }

    static android.view.ViewGroup layout() { return mLayout; }

    /* a short tick under the finger (a hold has started) */
    /* haptics shaped to what moves: 1 a soft tick (scale = how strong, a hold's ticks rising as it fills), 2 the pop of a hold
       that fires (a quick rise into a crisp click), 3 a click (a line crossed), 4 a soft thud (something gone), 5 a light tick
       (a pick). The phone's primitives where it has them; its predefined effects else; the view's feedback at worst */
    static android.os.Vibrator vib() { MainActivity a = self; if (a == null) return null; try { if (Build.VERSION.SDK_INT >= 31) return ((android.os.VibratorManager) a.getSystemService(Context.VIBRATOR_MANAGER_SERVICE)).getDefaultVibrator(); return (android.os.Vibrator) a.getSystemService(Context.VIBRATOR_SERVICE); } catch (Throwable e) { return null; } }
    public static void haptic2(int kind, float s) { android.os.Vibrator v = vib(); if (s <= 0) s = 0.5f; if (s > 1) s = 1;
        try { if (v != null && v.hasVibrator() && Build.VERSION.SDK_INT >= 30) { android.os.VibrationEffect.Composition c = android.os.VibrationEffect.startComposition(); int[] need;
                if (kind == 1) { need = new int[]{ android.os.VibrationEffect.Composition.PRIMITIVE_LOW_TICK }; c.addPrimitive(need[0], s); }
                else if (kind == 2) { need = new int[]{ android.os.VibrationEffect.Composition.PRIMITIVE_QUICK_RISE, android.os.VibrationEffect.Composition.PRIMITIVE_CLICK }; c.addPrimitive(need[0], 0.45f); c.addPrimitive(need[1], 1f, 25); }
                else if (kind == 3) { need = new int[]{ android.os.VibrationEffect.Composition.PRIMITIVE_CLICK }; c.addPrimitive(need[0], 0.7f * s + 0.3f); }
                else if (kind == 4) { need = new int[]{ android.os.VibrationEffect.Composition.PRIMITIVE_THUD }; c.addPrimitive(need[0], 0.8f); }
                else { need = new int[]{ android.os.VibrationEffect.Composition.PRIMITIVE_TICK }; c.addPrimitive(need[0], s); }
                if (v.areAllPrimitivesSupported(need)) { v.vibrate(c.compose()); return; } }
            if (v != null && v.hasVibrator() && Build.VERSION.SDK_INT >= 29) { int e = kind == 2 || kind == 4 ? android.os.VibrationEffect.EFFECT_HEAVY_CLICK : kind == 3 ? android.os.VibrationEffect.EFFECT_CLICK : android.os.VibrationEffect.EFFECT_TICK;
                if (kind == 1 && s < 0.3f) return; v.vibrate(android.os.VibrationEffect.createPredefined(e)); return; } } catch (Throwable ex) { }
        final MainActivity a = self; if (a == null) return; final int fk = kind; a.runOnUiThread(() -> { if (mLayout != null) mLayout.performHapticFeedback(fk == 2 || fk == 4 ? android.view.HapticFeedbackConstants.LONG_PRESS : fk == 3 ? android.view.HapticFeedbackConstants.VIRTUAL_KEY : android.view.HapticFeedbackConstants.CLOCK_TICK); }); }
    static android.view.View startCover;
    public static void firstFrame() { final MainActivity a = self; if (a == null) return; a.runOnUiThread(() -> { if (startCover != null) { android.view.ViewParent pr = startCover.getParent(); if (pr instanceof android.view.ViewGroup) ((android.view.ViewGroup) pr).removeView(startCover); startCover = null; } }); } /* the app's own start screen is up: the cover goes */
    /* downloads' notification (DownloadService), and asking for notifications once (Android 13+) */
    public static void castNotify(String t, String s, String art, boolean playing, long pos, long dur) { CastService.update(t, s, art, playing, pos, dur); }
    public static long[] castTake() { return CastService.take(); }
    public static void dlNotify(String t, String x, int p, String sub, String lines, String art) { DownloadService.update(t, x, p, sub, lines, art); }
    public static void dlFinished(String t, String x, String art) { DownloadService.finished(t, x, art); }
    public static void askNotifications() { final MainActivity a = self; if (a == null || Build.VERSION.SDK_INT < 33) return; a.runOnUiThread(() -> { try { if (a.checkSelfPermission("android.permission.POST_NOTIFICATIONS") != android.content.pm.PackageManager.PERMISSION_GRANTED) a.requestPermissions(new String[] { "android.permission.POST_NOTIFICATIONS" }, 7702); } catch (Throwable e) { } }); }
    static volatile boolean openDl; public static boolean takeOpenDownloads() { boolean o = openDl; openDl = false; return o; }
    /* ---- what the logs need from the Java side (core/applog.c folds the files in at the next start; nativeLog is live) ----
       an uncaught Java exception: its whole stack, kept in <files>/java_crash.txt before the app goes;
       why Android ended the app's earlier runs (Android 11 on): not responding (with Android's report: every thread's
       stack, the main one first), out of memory, a crash, killed in the back; kept in <files>/exit_info.txt;
       the Android side stuck: the main thread not answering for 5 s, its stack (and again if it stays stuck) */
    public static native void nativeLog(String line);
    static void logLine(String s) { try { nativeLog(s); } catch (Throwable e) { android.util.Log.i("StremioOrigin", s); } }
    private static void crashHook(final Context c) {
        final Thread.UncaughtExceptionHandler prev = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((t, ex) -> {
            try (FileOutputStream o = new FileOutputStream(new File(c.getFilesDir(), "java_crash.txt"), true)) {
                java.io.StringWriter w = new java.io.StringWriter(); ex.printStackTrace(new java.io.PrintWriter(w));
                StringBuilder b = new StringBuilder("[crash] java " + new java.util.Date() + " thread \"" + t.getName() + "\" version " + version(c) + "\n");
                int n = 0; for (String l : w.toString().split("\n")) { if (n++ > 120) break; b.append("[crash] java ").append(l.replace("\t", "  ")).append('\n'); }
                b.append("[crash] java end\n"); o.write(b.toString().getBytes("UTF-8"));
            } catch (Throwable e) { }
            if (prev != null) prev.uncaughtException(t, ex);
        });
    }
    static String version(Context c) { try { return c.getPackageManager().getPackageInfo(c.getPackageName(), 0).versionName; } catch (Throwable e) { return "?"; } }
    private static void exitInfo(Context c) {
        if (Build.VERSION.SDK_INT < 30) return;
        try {
            ActivityManager am = (ActivityManager) c.getSystemService(Context.ACTIVITY_SERVICE); if (am == null) return;
            android.content.SharedPreferences sp = c.getSharedPreferences("logs", 0); long seen = sp.getLong("exitSeen", 0), newest = seen;
            java.util.List<android.app.ApplicationExitInfo> l = am.getHistoricalProcessExitReasons(c.getPackageName(), 0, 8);
            StringBuilder b = new StringBuilder(); String[] why = { "unknown", "exited by itself", "a signal", "low memory", "a Java crash", "a native crash", "not responding (ANR)", "init failure", "permission change", "too much resource use", "user request", "user stopped", "dependency died", "other", "frozen", "package state", "package updated" };
            for (int i = l.size() - 1; i >= 0; i--) { android.app.ApplicationExitInfo x = l.get(i); if (x.getTimestamp() <= seen) continue; newest = Math.max(newest, x.getTimestamp());
                int r = x.getReason(); String rs = r >= 0 && r < why.length ? why[r] : "reason " + r;
                b.append("[exit-info] ").append(new java.util.Date(x.getTimestamp())).append(": ended by ").append(rs).append(" (status ").append(x.getStatus()).append(", importance ").append(x.getImportance())
                 .append(", pss ").append(x.getPss() / 1024).append(" MB, rss ").append(x.getRss() / 1024).append(" MB)").append(x.getDescription() != null ? " " + x.getDescription() : "").append('\n');
                if (r == android.app.ApplicationExitInfo.REASON_ANR || r == android.app.ApplicationExitInfo.REASON_CRASH) {   /* Android's report: the main thread's stack first */
                    try (InputStream in = x.getTraceInputStream()) { if (in != null) { java.io.BufferedReader rd = new java.io.BufferedReader(new java.io.InputStreamReader(in)); String ln; int n = 0, inMain = 0;
                        while ((ln = rd.readLine()) != null && n < 160) { if (ln.startsWith("\"main\"")) inMain = 1; else if (inMain == 1 && ln.trim().isEmpty()) inMain = 2;
                            if (inMain == 1 || (inMain == 2 && ln.startsWith("\"") ) || (inMain == 2 && ln.contains("stremioorigin"))) { b.append("[exit-info] ").append(ln.replace("\t", "  ")).append('\n'); n++; } } } } catch (Throwable e) { }
                    b.append("[exit-info] end\n"); } }
            if (b.length() > 0) try (FileOutputStream o = new FileOutputStream(new File(c.getFilesDir(), "exit_info.txt"), true)) { o.write(b.toString().getBytes("UTF-8")); }
            sp.edit().putLong("exitSeen", newest).apply();
        } catch (Throwable e) { }
    }
    private static volatile long uiBeat; private static volatile boolean uiWatch;
    private static void uiWatchdog() {
        if (uiWatch) return; uiWatch = true; final android.os.Handler h = new android.os.Handler(android.os.Looper.getMainLooper());
        new Thread(() -> { long told = 0, stuckFrom = 0;
            while (true) { final long ask = System.nanoTime(); h.post(() -> uiBeat = ask); try { Thread.sleep(1000); } catch (InterruptedException e) { return; }
                if (uiBeat >= ask) { if (told > 0) logLine("[freeze] java: the Android side answers again after " + (System.nanoTime() - stuckFrom) / 1000000 + " ms"); told = 0; stuckFrom = 0; continue; }
                if (stuckFrom == 0) stuckFrom = ask; long st = (System.nanoTime() - stuckFrom) / 1000000;
                if ((told == 0 && st >= 5000) || (told == 1 && st >= 15000)) { told++;
                    StringBuilder b = new StringBuilder("[freeze] java: the Android side (main thread) has not answered for " + st / 1000 + " s, its stack:");
                    for (StackTraceElement e : android.os.Looper.getMainLooper().getThread().getStackTrace()) b.append("\n[freeze] java   at ").append(e);
                    logLine(b.toString()); } }
        }, "ui-watchdog").start();
    }
    /* the version for the app's About page, its update check and its logs (core/update.c reads <files>/appinfo.json) */
    private void writeVersion() {
        try { String v = getPackageManager().getPackageInfo(getPackageName(), 0).versionName;
            FileOutputStream o = new FileOutputStream(new File(getFilesDir(), "appinfo.json")); o.write(("{\"version\": \"" + v + "\"}").getBytes("UTF-8")); o.close();
        } catch (Throwable e) { }
    }
    /* the update's answers, taken once a frame by the platform (plat_luna_pump) */
    static final java.util.concurrent.ConcurrentLinkedQueue<String> updateQ = new java.util.concurrent.ConcurrentLinkedQueue<>();
    public static String updTake() { String s = updateQ.poll(); return s == null ? "" : s; }
    /* ---- the in-app update (core/update.c asks through the platform): the package downloaded, its SHA-256 checked, then
       the system's package installer (it asks the user to confirm); answers as {"statusText","progress"},
       {"returnValue":false,"errorText"}, {"finished":true} ---- */
    private static volatile boolean updCancel;
    private static String q(String s) { return org.json.JSONObject.quote(s); }
    static void updSay(String status, int pct) { updateQ.add("{\"statusText\":" + q(status) + (pct >= 0 ? ",\"progress\":" + pct : "") + "}"); }
    /* (its words are the core's, in the app's language: codes here, upd:..., said by core/update.c) */
    static void updFail(String why) { updateQ.add("{\"returnValue\":false,\"errorText\":" + q(why) + "}"); android.util.Log.w("StremioOrigin", "[update] " + why); }
    public static void updateCancel(int unused) { updCancel = true; }
    public static void updateStart(final String url, final String sha) {
        final MainActivity a = self; if (a == null) { updFail("upd:closing"); return; }
        updCancel = false;
        new Thread(() -> {
            File apk = new File(a.getCacheDir(), "update.apk");
            try {
                if (Build.VERSION.SDK_INT >= 26 && !a.getPackageManager().canRequestPackageInstalls()) {
                    updFail("upd:allow");
                    a.runOnUiThread(() -> { try { a.startActivity(new android.content.Intent(android.provider.Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, android.net.Uri.parse("package:" + a.getPackageName()))); } catch (Throwable e) { } });
                    return;
                }
                updSay("upd:downloading", 0);
                java.net.HttpURLConnection c = (java.net.HttpURLConnection) new java.net.URL(url).openConnection();
                c.setInstanceFollowRedirects(true); c.setConnectTimeout(20000); c.setReadTimeout(30000);
                c.setRequestProperty("User-Agent", "StremioOrigin (Android phone)");
                if (c.getResponseCode() != 200) { updFail("upd:download " + c.getResponseCode()); return; }
                long total = c.getContentLength(), got = 0; int lastPct = -1;
                java.security.MessageDigest md = java.security.MessageDigest.getInstance("SHA-256");
                try (InputStream in = c.getInputStream(); FileOutputStream out = new FileOutputStream(apk)) {
                    byte[] b = new byte[65536]; int n;
                    while ((n = in.read(b)) > 0) { if (updCancel) { updFail("upd:cancelled"); return; } out.write(b, 0, n); md.update(b, 0, n); got += n;
                        int pct = total > 0 ? (int) (got * 100 / total) : -1; if (pct != lastPct && pct % 5 == 0) { lastPct = pct; updSay("upd:downloading", pct); } }
                }
                StringBuilder hex = new StringBuilder(); for (byte x : md.digest()) hex.append(String.format(Locale.ROOT, "%02x", x));
                if (!hex.toString().equalsIgnoreCase(sha)) { apk.delete(); updFail("upd:checksum"); return; }
                updSay("upd:installing", -1);
                install(a, apk);
            } catch (Throwable e) { updFail("upd:failed " + e); }
        }, "update").start();
    }
    /* Android 13 and up ask every receiver whether other apps may reach it */
    static void reg(Context c, android.content.BroadcastReceiver r, android.content.IntentFilter f, boolean fromSystem) {
        if (Build.VERSION.SDK_INT >= 33) c.registerReceiver(r, f, fromSystem ? Context.RECEIVER_EXPORTED : Context.RECEIVER_NOT_EXPORTED); else c.registerReceiver(r, f);
    }
    /* The install, made so the app is open again afterwards (an update ends every process of the app):
       - Android 9 and before, or where the user allowed "Display over other apps": a package session; UpdatedReceiver
         opens the app from the "my package was replaced" broadcast (kept to updates this app started: update.reopen);
       - Android 10 and up otherwise (an app in the back may not open a screen; the installer's answer is refused too,
         measured on Android 12): the system's install screen, which ends on Open. */
    static final String INSTALL_STATUS = "app.stremioorigin.phone.INSTALL_STATUS";
    private static void install(final MainActivity a, File apk) throws IOException {
        if (Build.VERSION.SDK_INT >= 29 && !android.provider.Settings.canDrawOverlays(a)) {
            android.content.Intent i = new android.content.Intent(android.content.Intent.ACTION_INSTALL_PACKAGE)
                .setData(Uri.parse("content://" + ApkProvider.AUTH + "/update.apk"))
                .addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION)
                .putExtra(android.content.Intent.EXTRA_INSTALLER_PACKAGE_NAME, a.getPackageName());
            a.runOnUiThread(() -> { try { a.startActivity(i); updSay("upd:confirm", -1); } catch (Throwable e) { updFail("upd:installer"); } });
            return;
        }
        android.content.pm.PackageInstaller pi = a.getPackageManager().getPackageInstaller();
        android.content.pm.PackageInstaller.SessionParams sp = new android.content.pm.PackageInstaller.SessionParams(android.content.pm.PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        sp.setAppPackageName(a.getPackageName());
        int id = pi.createSession(sp);
        try (android.content.pm.PackageInstaller.Session s = pi.openSession(id)) {
            try (InputStream in = new FileInputStream(apk); java.io.OutputStream out = s.openWrite("app.apk", 0, apk.length())) {
                byte[] b = new byte[65536]; int n; while ((n = in.read(b)) > 0) out.write(b, 0, n); s.fsync(out);
            }
            try (FileOutputStream m = new FileOutputStream(new File(a.getFilesDir(), "update.reopen"))) { m.write(String.valueOf(System.currentTimeMillis()).getBytes("UTF-8")); }
            android.content.Intent back = new android.content.Intent(a, MainActivity.class).setAction(INSTALL_STATUS).addFlags(android.content.Intent.FLAG_ACTIVITY_SINGLE_TOP);
            int fl = (Build.VERSION.SDK_INT >= 31 ? android.app.PendingIntent.FLAG_MUTABLE : 0) | android.app.PendingIntent.FLAG_UPDATE_CURRENT;
            Bundle opts = null;
            if (Build.VERSION.SDK_INT >= 34) opts = android.app.ActivityOptions.makeBasic().setPendingIntentCreatorBackgroundActivityStartMode(android.app.ActivityOptions.MODE_BACKGROUND_ACTIVITY_START_ALLOWED).toBundle();
            android.app.PendingIntent done = android.app.PendingIntent.getActivity(a, id, back, fl, opts);
            s.commit(done.getIntentSender());
        }
    }
    /* the installer's answer (see install): the confirmation to show, the end, or why it failed */
    private boolean installStatus(android.content.Intent i) {
        if (i == null || !INSTALL_STATUS.equals(i.getAction())) return false;
        int st = i.getIntExtra(android.content.pm.PackageInstaller.EXTRA_STATUS, -999);
        if (st == android.content.pm.PackageInstaller.STATUS_PENDING_USER_ACTION) {   /* the system's confirmation */
            android.content.Intent confirm = (android.content.Intent) i.getParcelableExtra(android.content.Intent.EXTRA_INTENT);
            if (confirm != null) try { startActivity(confirm); } catch (Throwable e) { updFail("upd:installer"); }
            updSay("upd:confirm", -1);
        } else if (st == android.content.pm.PackageInstaller.STATUS_SUCCESS) {
            new File(getFilesDir(), "update.reopen").delete(); updateQ.add("{\"finished\":true}");
            android.util.Log.i("StremioOrigin", "[update] installed: opened again by the installer's answer");
        } else {
            new File(getFilesDir(), "update.reopen").delete();
            String m = i.getStringExtra(android.content.pm.PackageInstaller.EXTRA_STATUS_MESSAGE); updFail(st == android.content.pm.PackageInstaller.STATUS_FAILURE_ABORTED ? "upd:cancelled" : st == android.content.pm.PackageInstaller.STATUS_FAILURE_STORAGE ? "upd:storage" : "upd:failed " + m);
        }
        return true;
    }

    @Override protected void onNewIntent(Intent i) { super.onNewIntent(i); if (installStatus(i)) return; if (i != null && i.getBooleanExtra("openDownloads", false)) openDl = true; }
    public static void haptic() { final MainActivity a = self; if (a == null) return; a.runOnUiThread(() -> { if (mLayout != null) mLayout.performHapticFeedback(android.view.HapticFeedbackConstants.LONG_PRESS, android.view.HapticFeedbackConstants.FLAG_IGNORE_VIEW_SETTING); }); }

    /* the screen's brightness for this window (0..1; below 0: read only; -1 back: the phone's own) */
    private static volatile float bright = -1;
    public static float brightness(final float set) {
        final MainActivity a = self; if (a == null) return 0.5f;
        if (set >= 0) { bright = set; a.runOnUiThread(() -> { WindowManager.LayoutParams lp = a.getWindow().getAttributes(); lp.screenBrightness = Math.max(0.01f, set); a.getWindow().setAttributes(lp); }); return set; }
        if (bright >= 0) return bright;
        try { return android.provider.Settings.System.getInt(a.getContentResolver(), android.provider.Settings.System.SCREEN_BRIGHTNESS) / 255f; } catch (Exception e) { return 0.5f; }
    }
    /* the player's brightness is the player's: the phone's own comes back when it closes, the player's when it opens again */
    static void brightnessPlayer(final boolean on) { final MainActivity a = self; if (a == null) return; final float b = bright;
        a.runOnUiThread(() -> { WindowManager.LayoutParams lp = a.getWindow().getAttributes(); lp.screenBrightness = on && b >= 0 ? Math.max(0.01f, b) : WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE; a.getWindow().setAttributes(lp); }); }
    /* what this phone can show and decode, as bits: 1 HDR10 screen, 2 HLG, 4 HDR10+, 8 Dolby Vision screen, 16 a Dolby Vision decoder,
       32 10-bit HEVC (Main10) decoder, 64 AV1 decoder, 128 any HEVC decoder */
    public static int mediaCaps() { int c = 0; MainActivity a = self;
        try { if (a != null) { android.view.Display d = a.getWindowManager().getDefaultDisplay(); android.view.Display.HdrCapabilities h = d.getHdrCapabilities();
            if (h != null) for (int t : h.getSupportedHdrTypes()) c |= t == android.view.Display.HdrCapabilities.HDR_TYPE_HDR10 ? 1 : t == android.view.Display.HdrCapabilities.HDR_TYPE_HLG ? 2 : t == android.view.Display.HdrCapabilities.HDR_TYPE_HDR10_PLUS ? 4 : t == android.view.Display.HdrCapabilities.HDR_TYPE_DOLBY_VISION ? 8 : 0; } } catch (Throwable e) { }
        try { for (android.media.MediaCodecInfo ci : new android.media.MediaCodecList(android.media.MediaCodecList.REGULAR_CODECS).getCodecInfos()) { if (ci.isEncoder()) continue;
            for (String ty : ci.getSupportedTypes()) { String t = ty.toLowerCase(java.util.Locale.ROOT);
                if (t.equals("video/dolby-vision")) c |= 16; else if (t.equals("video/av01")) c |= 64;
                else if (t.equals("video/hevc")) { c |= 128; try { for (android.media.MediaCodecInfo.CodecProfileLevel pl : ci.getCapabilitiesForType(ty).profileLevels) if (pl.profile == android.media.MediaCodecInfo.CodecProfileLevel.HEVCProfileMain10 || pl.profile == android.media.MediaCodecInfo.CodecProfileLevel.HEVCProfileMain10HDR10) c |= 32; } catch (Throwable e) { } } } } } catch (Throwable e) { }
        return c; }
    /* downloads: on Wi-Fi (or a cable) now, and the places they can go (the app's own storage, then each card's) */
    public static boolean onWifi() { MainActivity a = self; if (a == null) return true; try { android.net.ConnectivityManager cm = (android.net.ConnectivityManager) a.getSystemService(Context.CONNECTIVITY_SERVICE);
        android.net.NetworkCapabilities nc = cm.getNetworkCapabilities(cm.getActiveNetwork()); return nc != null && (nc.hasTransport(android.net.NetworkCapabilities.TRANSPORT_WIFI) || nc.hasTransport(android.net.NetworkCapabilities.TRANSPORT_ETHERNET)); } catch (Throwable e) { return true; } }
    public static String dlDirs() { MainActivity a = self; if (a == null) return ""; StringBuilder b = new StringBuilder(); try { java.io.File[] ds = a.getExternalFilesDirs(android.os.Environment.DIRECTORY_MOVIES);
        for (int i = 0; ds != null && i < ds.length; i++) if (ds[i] != null) { ds[i].mkdirs(); long free = ds[i].getUsableSpace(); b.append(i == 0 ? "phone" : "card").append('\t').append(ds[i].getAbsolutePath()).append('\t').append(free).append('\n'); } } catch (Throwable e) { } return b.toString(); }
    /* the media volume (0..1; below 0: read only) */
    public static float volume(float set) {
        MainActivity a = self; if (a == null) return 0.5f; android.media.AudioManager am = (android.media.AudioManager) a.getSystemService(Context.AUDIO_SERVICE); if (am == null) return 0.5f;
        int max = am.getStreamMaxVolume(android.media.AudioManager.STREAM_MUSIC);
        if (set >= 0) am.setStreamVolume(android.media.AudioManager.STREAM_MUSIC, Math.round(set * max), 0);
        return max > 0 ? am.getStreamVolume(android.media.AudioManager.STREAM_MUSIC) / (float) max : 0.5f;
    }

    /* the video plane, for the native player (found through this class: the native thread sees the app's classes from here) */
    public static void vLoad(String url, String headers, long startMs) { VideoPlane.load(url, headers, startMs); }
    public static void vRate(float r) { VideoPlane.rate(r); }
    public static void vAspect(String a, float scale) { VideoPlane.aspect(a, scale); }
    public static void vPinch(float k) { VideoPlane.pinch(k); }
    public static void vDrag(float tx, float ty, float k) { VideoPlane.drag(tx, ty, k); }
    public static void vSnap(int w, int h) { VideoPlane.snap(w, h); }
    public static int[] vSnapTake() { return VideoPlane.snapTake(); }
    public static String vPinchEnd(float k) { return VideoPlane.pinchEnd(k); }
    public static void vAddSub(String url, boolean select) { VideoPlane.addSubtitle(url, select); }
    public static void vSpuDelay(long us) { VideoPlane.spuDelay(us); }
    public static void vUnload() { VideoPlane.unload(); }
    public static void vSetLive(boolean on) { VideoPlane.liveSrc = on; }
    public static void vLiveRejoin() { VideoPlane.liveRejoin(); }
    public static void vFull(boolean on) { VideoPlane.fullscreen(on); }
    public static int vHeld() { return VideoPlane.held(); }
    public static int atvOpen(String ip, String link) { return AtvRemote.openApp(ip, link); } /* an Android TV's own remote: open our app on it */
    public static int atvPairStart(String ip, String name) { return AtvRemote.pairStart(ip, name); }
    public static int atvPairFinish(String code) { return AtvRemote.pairFinish(code); }
    public static void atvPairClose() { AtvRemote.pairClose(); }
    public static void vPlay() { VideoPlane.play(); }
    public static void vPause() { VideoPlane.pause(); }
    public static void vSeek(long ms) { VideoPlane.seek(ms); }
    public static void vVolume(float v) { VideoPlane.volume(v); }
    public static void vWindow(int x, int y, int w, int h) { VideoPlane.window(x, y, w, h); }
    public static long[] vPoll() { return VideoPlane.poll(); }
    public static String vError() { return VideoPlane.error(); }
    public static String[] vTracks(int type) { return VideoPlane.tracks(type); }
    public static void vSelect(int type, int idx) { VideoPlane.select(type, idx); }

    public static float density() {
        MainActivity a = self;
        return a == null ? 1f : a.getResources().getDisplayMetrics().density;
    }

    /** Returns {top, bottom, left, right, keyboard} insets in pixels (the keyboard: 0 when it is down). */
    public static int[] insets() {
        int[] r = new int[5];
        MainActivity a = self;
        if (a == null) {
            return r;
        }
        if (Build.VERSION.SDK_INT >= 23) {
            WindowInsets wi = a.getWindow().getDecorView().getRootWindowInsets();
            if (wi != null) {
                r[0] = wi.getSystemWindowInsetTop();
                r[1] = wi.getSystemWindowInsetBottom();
                r[2] = wi.getSystemWindowInsetLeft();
                r[3] = wi.getSystemWindowInsetRight();
                if (Build.VERSION.SDK_INT >= 30) {
                    r[1] = wi.getInsets(WindowInsets.Type.navigationBars()).bottom;
                    r[4] = wi.getInsets(WindowInsets.Type.ime()).bottom;
                } else if (r[3] >= 0) {
                    int nav = wi.getStableInsetBottom(); if (r[1] > nav + 100) { r[4] = r[1]; r[1] = nav; }
                }
            }
        } else {
            int id = a.getResources().getIdentifier("status_bar_height", "dimen", "android");
            if (id > 0) {
                r[0] = a.getResources().getDimensionPixelSize(id);
            }
            int nb = a.getResources().getIdentifier("navigation_bar_height", "dimen", "android");
            if (nb > 0) r[1] = a.getResources().getDimensionPixelSize(nb); /* drawn behind the navigation bar there too */
        }
        return r;
    }

    public static int memoryClassMb() {
        ActivityManager am = activityManager();
        return am == null ? 0 : am.getMemoryClass();
    }

    public static long totalMemMb() {
        ActivityManager am = activityManager();
        if (am == null) {
            return 0;
        }
        ActivityManager.MemoryInfo mi = new ActivityManager.MemoryInfo();
        am.getMemoryInfo(mi);
        return mi.totalMem / (1024L * 1024L);
    }

    private static ActivityManager activityManager() {
        MainActivity a = self;
        return a == null ? null : (ActivityManager) a.getSystemService(Context.ACTIVITY_SERVICE);
    }

    public static synchronized void multicast(boolean on) {
        MainActivity a = self;
        if (a == null) {
            return;
        }
        if (mcLock == null) {
            WifiManager wm = (WifiManager) a.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
            if (wm == null) {
                return;
            }
            mcLock = wm.createMulticastLock("sbcast");
            mcLock.setReferenceCounted(false);
        }
        if (on) {
            if (!mcLock.isHeld()) {
                mcLock.acquire();
            }
        } else if (mcLock.isHeld()) {
            mcLock.release();
        }
    }

    /** headers: "Name: value" lines separated by \n, may be empty. */
    public static boolean openExternal(String url, String title) {
        MainActivity a = self;
        if (a == null) {
            return false;
        }
        Intent intent = new Intent(Intent.ACTION_VIEW);
        intent.setDataAndType(Uri.parse(url), "video/*");
        intent.putExtra("title", title);
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        if (intent.resolveActivity(a.getPackageManager()) == null) {
            return false;
        }
        try {
            a.startActivity(Intent.createChooser(intent, "Play with"));
            return true;
        } catch (ActivityNotFoundException e) {
            return false;
        }
    }

    /* the video players on this phone: "label\tpackage" lines (not this app) */
    public static String videoPlayers() {
        MainActivity a = self; if (a == null) return "";
        StringBuilder b = new StringBuilder(); java.util.HashSet<String> seen = new java.util.HashSet<>();
        try { android.content.pm.PackageManager pm = a.getPackageManager();
            for (String sc : new String[] { "https", "http" }) { Intent i = new Intent(Intent.ACTION_VIEW); i.setDataAndType(Uri.parse(sc + "://example.com/video.mkv"), "video/*");
                for (android.content.pm.ResolveInfo r : pm.queryIntentActivities(i, 0)) { String p = r.activityInfo.packageName; if (p.equals(a.getPackageName()) || !seen.add(p)) continue;
                    b.append(r.loadLabel(pm).toString().replace('\t', ' ').replace('\n', ' ')).append('\t').append(p).append('\n'); } }
        } catch (Exception e) { }
        return b.toString();
    }
    /* a stream in another app: that one (pkg), or the phone's chooser ("" or when it is gone) */
    public static boolean openExternalWith(String url, String title, String pkg) {
        MainActivity a = self; if (a == null) return false;
        Intent intent = new Intent(Intent.ACTION_VIEW); intent.setDataAndType(Uri.parse(url), "video/*"); intent.putExtra("title", intent.getStringExtra("title") == null ? title : title); intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        if (pkg != null && !pkg.isEmpty()) { Intent d = new Intent(intent); d.setPackage(pkg); try { a.startActivity(d); return true; } catch (Exception e) { } }
        try { a.startActivity(Intent.createChooser(intent, "Play with").addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)); return true; } catch (Exception e) { return false; }
    }

    public static boolean openUrl(String url) {
        MainActivity a = self;
        if (a == null) return false;
        try { a.startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)); return true; }
        catch (ActivityNotFoundException e) { return false; }
    }

    public static String filesDir() {
        MainActivity a = self;
        return a == null ? "" : a.getFilesDir().getAbsolutePath();
    }

    public static String cacheDir() {
        MainActivity a = self;
        return a == null ? "" : a.getCacheDir().getAbsolutePath();
    }

    public static String locale() {
        return Locale.getDefault().toLanguageTag();
    }

    /** state: 0 playing, 1 paused, 2 closed, 3 ended, 4 error. */
    public static native void nativeProgress(String key, long posMs, long durMs, int state);

    /* picture in picture: the film goes on in a small window when the user leaves the app (home, another app) while it
       plays; the app's own drawing is hidden meanwhile (the film alone shows), and comes back with the full screen.
       Left without it (an older Android, nothing playing, the small window closed): the film pauses */
    private android.app.PictureInPictureParams pipParams() {
        long[] v = VideoPlane.poll(); int w = (int) v[4], h = (int) v[5]; if (w <= 0 || h <= 0) { w = 16; h = 9; }
        float r = (float) w / h; if (r > 2.39f) { w = 239; h = 100; } else if (r < 0.42f) { w = 42; h = 100; } /* (Android's limits) */
        android.app.PictureInPictureParams.Builder b = new android.app.PictureInPictureParams.Builder().setAspectRatio(new android.util.Rational(w, h));
        if (Build.VERSION.SDK_INT >= 31) b.setAutoEnterEnabled(VideoPlane.playing()).setSeamlessResizeEnabled(true);
        return b.build(); }
    @Override protected void onUserLeaveHint() { super.onUserLeaveHint();
        if (Build.VERSION.SDK_INT >= 26 && VideoPlane.playing()) { try { enterPictureInPictureMode(pipParams()); } catch (Throwable t) { } } }
    @Override public void onPictureInPictureModeChanged(boolean in, android.content.res.Configuration c) { super.onPictureInPictureModeChanged(in, c);
        inPip = in; if (in) wasPip = true; vPipSub(""); /* (its words come from the app as they change) */
        if (mSurface != null) mSurface.setVisibility(in ? View.INVISIBLE : View.VISIBLE); VideoPlane.pipMode(in);
        if (!in) { /* left picture in picture: brought back (the app comes up), or the small window closed (it does not): then the film stops */
            if (mLayout != null) mLayout.postDelayed(() -> { if (!resumedNow && !inPip) { VideoPlane.pause(); android.util.Log.i("StremioOrigin", "[pip] closed: the film stopped"); } }, 1500); } } /* (brought back takes a moment on a slow phone: 350 ms took an expand for a close and paused it; closed, the app is also stopped, see onStop) */
    static volatile boolean resumedNow;
    /* picture in picture: the app draws nothing there, so the subtitles showing are put in a text over the small window
       (white, an outline's shadow, at its foot, sized to it) */
    static volatile boolean inPip; private static android.widget.TextView pipSubV; private static String pipSubWas = "";
    public static boolean vPip() { return inPip; }
    static boolean pipNow() { MainActivity a = self; try { return a != null && Build.VERSION.SDK_INT >= 24 && a.isInPictureInPictureMode(); } catch (Throwable e) { return false; } }
    public static void vPipSubB(byte[] b) { vPipSub(b == null ? "" : new String(b, java.nio.charset.StandardCharsets.UTF_8)); }
    /* true: its size changed (laid out again before anything is drawn) */
    private static boolean pipSubSize() { if (pipSubV == null || mLayout == null || !inPip) return false; int h = mLayout.getHeight(), w = mLayout.getWidth(); if (w <= 0 || h <= 0) return false; float px = Math.max(10f, Math.min(h, w * 9 / 16f) / 11f);
        if (Math.abs(pipSubV.getTextSize() - px) < 0.5f) return false; pipSubV.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX, px); pipSubV.setPadding((int) (px * 0.6f), 0, (int) (px * 0.6f), (int) (px * 0.5f)); return true; }
    public static void vPipSub(final String t) { final MainActivity a = self; if (a == null) return; final String s = t == null ? "" : t;
        a.runOnUiThread(() -> { try {
            if (pipSubV == null) { if (mLayout == null) return; pipSubV = new android.widget.TextView(a); pipSubV.setTextColor(0xFFFFFFFF); pipSubV.setShadowLayer(6f, 0f, 1.5f, 0xE6000000);
                pipSubV.setGravity(android.view.Gravity.CENTER_HORIZONTAL | android.view.Gravity.BOTTOM); pipSubV.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
                android.widget.RelativeLayout.LayoutParams lp = new android.widget.RelativeLayout.LayoutParams(android.view.ViewGroup.LayoutParams.MATCH_PARENT, android.view.ViewGroup.LayoutParams.WRAP_CONTENT);
                lp.addRule(android.widget.RelativeLayout.ALIGN_PARENT_BOTTOM); mLayout.addView(pipSubV, lp);
                pipSubV.getViewTreeObserver().addOnPreDrawListener(() -> !pipSubSize()); } /* (the small window resized: its words sized before the frame is drawn, never a frame at the old size) */
            if (!inPip || s.isEmpty()) { pipSubV.setVisibility(View.GONE); pipSubV.setText(""); pipSubWas = ""; return; }
            pipSubSize();
            if (!s.equals(pipSubWas)) { pipSubV.setText(s); pipSubWas = s; } pipSubV.setVisibility(View.VISIBLE); pipSubV.bringToFront();
        } catch (Throwable e) { } }); }
    /* downloads: a folder the user picks (the system's folder picker), turned into a path the engine writes to; where
       Android wants it, the "all files" permission is asked once; the result waits for the app to take it */
    static volatile String pickedDir; static volatile int pickState; /* 0 nothing, 1 picking, 2 done (pickedDir), 3 refused */ static String pendingDir;
    public static void pickFolder() { final MainActivity a = self; if (a == null) return; pickState = 1;
        a.runOnUiThread(() -> { try { a.startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), 7701); } catch (Throwable e) { pickState = 3; } }); }
    public static String folderTake() { int st = pickState; if (st == 2) { pickState = 0; return pickedDir; } if (st == 3) { pickState = 0; return "!"; } return ""; }
    static boolean writable(String dir) { try { java.io.File d = new java.io.File(dir); d.mkdirs(); java.io.File t = new java.io.File(d, ".sbtest"); java.io.FileOutputStream o = new java.io.FileOutputStream(t); o.write(1); o.close(); t.delete(); return true; } catch (Throwable e) { return false; } }
    @Override protected void onActivityResult(int req, int res, Intent data) { super.onActivityResult(req, res, data);
        if (req != 7701) return; if (res != RESULT_OK || data == null || data.getData() == null) { pickState = 0; return; }
        String path = null; try { String id = android.provider.DocumentsContract.getTreeDocumentId(data.getData()); int c = id.indexOf(':'); String vol = c >= 0 ? id.substring(0, c) : id, rel = c >= 0 ? id.substring(c + 1) : "";
            String root = "primary".equalsIgnoreCase(vol) ? android.os.Environment.getExternalStorageDirectory().getAbsolutePath() : "/storage/" + vol; path = rel.isEmpty() ? root : root + "/" + rel; } catch (Throwable e) { }
        if (path == null) { pickState = 3; return; }
        if (writable(path)) { pickedDir = path; pickState = 2; return; }
        if (Build.VERSION.SDK_INT >= 30 && !android.os.Environment.isExternalStorageManager()) { pendingDir = path; /* allowed once, then the folder is taken */
            try { startActivity(new Intent(android.provider.Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName()))); return; } catch (Throwable e) { } }
        pickState = 3; }
    /* how the phone is tilted (gravity, -1..1 on the screen's x and y): the glass's light follows it, as on an iPhone */
    static volatile float tiltX, tiltY; static android.hardware.SensorEventListener tiltL;
    public static float tilt(int axis) { return axis == 0 ? tiltX : tiltY; }
    void tiltOn(boolean on) { try { android.hardware.SensorManager sm = (android.hardware.SensorManager) getSystemService(Context.SENSOR_SERVICE); if (sm == null) return;
        if (!on) { if (tiltL != null) sm.unregisterListener(tiltL); return; }
        android.hardware.Sensor g = sm.getDefaultSensor(android.hardware.Sensor.TYPE_GRAVITY); if (g == null) g = sm.getDefaultSensor(android.hardware.Sensor.TYPE_ACCELEROMETER); if (g == null) return;
        if (tiltL == null) tiltL = new android.hardware.SensorEventListener() { public void onSensorChanged(android.hardware.SensorEvent e) { int rot = getWindowManager().getDefaultDisplay().getRotation(); float ax = e.values[0] / 9.81f, ay = e.values[1] / 9.81f, x, y;
              if (rot == android.view.Surface.ROTATION_90) { x = -ay; y = ax; } else if (rot == android.view.Surface.ROTATION_270) { x = ay; y = -ax; } else if (rot == android.view.Surface.ROTATION_180) { x = ax; y = -ay; } else { x = -ax; y = ay; }
              tiltX += (x - tiltX) * 0.15f; tiltY += (y - tiltY) * 0.15f; } public void onAccuracyChanged(android.hardware.Sensor s, int a) { } };
        sm.registerListener(tiltL, g, android.hardware.SensorManager.SENSOR_DELAY_GAME); } catch (Throwable e) { } }
    @Override protected void onPause() { super.onPause(); tiltOn(false); resumedNow = false; }
    @Override protected void onResume() { super.onResume(); resumedNow = true; tiltOn(true); if (pendingDir != null) { String d = pendingDir; pendingDir = null; if (writable(d)) { pickedDir = d; pickState = 2; } else pickState = 3; } }
    @Override protected void onStop() { super.onStop(); if (Build.VERSION.SDK_INT < 24 || !isInPictureInPictureMode()) { VideoPlane.pause(); if (wasPip) android.util.Log.i("StremioOrigin", "[pip] closed: the film stopped"); } wasPip = false; } /* (the small window closed: the app is stopped) */
    private boolean wasPip;
    /* the player tells when a film runs, so Android 12+ goes to picture in picture by itself on the way home */
    public static void vPipUpdate() { final MainActivity a = self; if (a == null || Build.VERSION.SDK_INT < 26) return; a.runOnUiThread(() -> { try { a.setPictureInPictureParams(a.pipParams()); } catch (Throwable t) { } }); }
}
