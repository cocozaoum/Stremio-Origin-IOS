package app.stremioorigin.phone;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;

import java.io.File;

/* The app was replaced by a newer version. When the update was this app's own (MainActivity.install left update.reopen,
   less than 10 minutes ago), the app opens again: always on Android 9 and before; from Android 10 on only where the user
   allowed "Display over other apps" (the one documented way an app in the back may open a screen). Elsewhere the
   installer's own answer or its Open button does it (MainActivity.install). */
public class UpdatedReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context c, Intent i) {
        if (!Intent.ACTION_MY_PACKAGE_REPLACED.equals(i.getAction())) return;
        File m = new File(c.getFilesDir(), "update.reopen");
        if (!m.exists() || System.currentTimeMillis() - m.lastModified() > 10 * 60 * 1000) return;
        m.delete();
        boolean may = Build.VERSION.SDK_INT < 29 || android.provider.Settings.canDrawOverlays(c);
        android.util.Log.i("StremioOrigin", "[update] replaced; " + (may ? "opening the app again" : "the installer's answer opens it"));
        if (!may) return;
        try { c.startActivity(new Intent(c, MainActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)); } catch (Throwable e) { }
    }
}
