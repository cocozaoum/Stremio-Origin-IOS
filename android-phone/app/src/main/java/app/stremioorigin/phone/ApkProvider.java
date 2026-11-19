package app.stremioorigin.phone;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;

import java.io.File;
import java.io.FileNotFoundException;

/* Hands the downloaded update (<cache>/update.apk) to the system's installer, read-only, to the one it was granted to. */
public class ApkProvider extends ContentProvider {
    static final String AUTH = "app.stremioorigin.phone.apk";
    @Override public boolean onCreate() { return true; }
    @Override public String getType(Uri u) { return "application/vnd.android.package-archive"; }
    @Override public ParcelFileDescriptor openFile(Uri u, String mode) throws FileNotFoundException {
        return ParcelFileDescriptor.open(new File(getContext().getCacheDir(), "update.apk"), ParcelFileDescriptor.MODE_READ_ONLY);
    }
    @Override public Cursor query(Uri u, String[] p, String s, String[] a, String o) {
        android.database.MatrixCursor c = new android.database.MatrixCursor(new String[] { android.provider.OpenableColumns.DISPLAY_NAME, android.provider.OpenableColumns.SIZE });
        File f = new File(getContext().getCacheDir(), "update.apk"); c.addRow(new Object[] { "update.apk", f.length() }); return c;
    }
    @Override public Uri insert(Uri u, ContentValues v) { return null; }
    @Override public int delete(Uri u, String s, String[] a) { return 0; }
    @Override public int update(Uri u, ContentValues v, String s, String[] a) { return 0; }
}
