package com.ptext;

import android.app.NativeActivity;
import android.content.Intent;
import android.os.Bundle;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public class MainActivity extends NativeActivity {
    private static final int REQ = 1001;
    private static volatile MainActivity sInst;
    private static volatile int sRescan;
    private static int sFileCounter;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        sInst = this;
    }

    @Override
    protected void onDestroy() {
        sInst = null;
        super.onDestroy();
    }

    public static void pickFile() {
        MainActivity activity = sInst;
        if (activity == null) return;

        activity.runOnUiThread(() -> {
            MainActivity current = sInst;
            if (current == null) return;

            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("text/plain");
            current.startActivityForResult(intent, REQ);
        });
    }

    public static int takeRescanFlag() {
        int value = sRescan;
        sRescan = 0;
        return value;
    }

    @SuppressWarnings("deprecation")
    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != REQ || resultCode != RESULT_OK ||
            data == null || data.getData() == null) {
            return;
        }

        File tmp = null;
        try {
            File dir = new File(getFilesDir(), "files");
            if (!dir.isDirectory() && !dir.mkdirs()) {
                throw new IOException("mkdirs failed");
            }

            File dest = new File(dir,
                    System.currentTimeMillis() + "_" + (++sFileCounter) + ".txt");
            tmp = new File(dir, dest.getName() + ".part");

            try (InputStream is = getContentResolver().openInputStream(data.getData());
                 OutputStream os = new FileOutputStream(tmp)) {
                if (is == null) throw new IOException("openInputStream returned null");

                byte[] buf = new byte[8192];
                int len;
                while ((len = is.read(buf)) != -1) {
                    os.write(buf, 0, len);
                }
            }

            if (!tmp.renameTo(dest)) throw new IOException("rename failed");
            tmp = null;
            sRescan = 1;
        } catch (Exception e) {
            if (tmp != null) tmp.delete();
            final String message = e.getMessage();
            runOnUiThread(() -> Toast.makeText(this,
                    "导入失败: " + (message == null ? "unknown" : message),
                    Toast.LENGTH_SHORT).show());
        }
    }
}
