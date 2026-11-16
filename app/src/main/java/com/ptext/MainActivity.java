package com.ptext;
import android.app.NativeActivity;
import android.content.Intent;
import android.os.Bundle;
import android.widget.Toast;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

public class MainActivity extends NativeActivity {
    private static final int REQ = 1001;
    private static MainActivity sInst;
    private static volatile int sRescan = 0;
    private static int sFileCounter = 0;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        sInst = this;
    }

    @Override protected void onDestroy() {
        super.onDestroy();
        sInst = null;
    }

    public static void pickFile() {
        if (sInst == null) return;
        sInst.runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("text/plain");
            sInst.startActivityForResult(i, REQ);
        });
    }

    public static int takeRescanFlag() {
        int v = sRescan;
        sRescan = 0;
        return v;
    }

    @SuppressWarnings("deprecation")
    @Override protected void onActivityResult(int r, int rc, Intent d) {
        if (r == REQ && rc == RESULT_OK && d != null && d.getData() != null) {
            try {
                File dir = new File(getFilesDir(), "files");
                if (!dir.exists()) dir.mkdirs();
                File dest = new File(dir, System.currentTimeMillis() + "_" +
                                          (++sFileCounter) + ".txt");
                try (InputStream is = getContentResolver().openInputStream(d.getData());
                     OutputStream os = new FileOutputStream(dest)) {
                    byte[] buf = new byte[8192];
                    int len;
                    while ((len = is.read(buf)) != -1) {
                        os.write(buf, 0, len);
                    }
                }
                sRescan = 1;
            } catch (Exception e) {
                runOnUiThread(() ->
                    Toast.makeText(this, "导入失败: " + e.getMessage(), Toast.LENGTH_SHORT).show());
            }
        }
    }
}