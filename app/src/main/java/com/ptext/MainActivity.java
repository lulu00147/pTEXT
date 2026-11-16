package com.ptext;

import android.app.NativeActivity;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStreamWriter;
import java.nio.charset.StandardCharsets;

public class MainActivity extends NativeActivity {
    private static final int REQ = 1001;
    private static volatile MainActivity sInst;
    private static volatile int sRescan;
    private static int sFileCounter;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        sInst = this;

        getWindow().setDecorFitsSystemWindows(false);
        WindowInsetsController c = getWindow().getInsetsController();
        if (c != null) {
            c.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
            c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
        }
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

            Uri uri = data.getData();
            String displayName = null;
            long totalSize = -1;
            try (Cursor c = getContentResolver().query(uri,
                    new String[]{OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE},
                    null, null, null)) {
                if (c != null && c.moveToFirst()) {
                    displayName = c.getString(0);
                    if (!c.isNull(1)) totalSize = c.getLong(1);
                }
            } catch (Exception ignored) {}

            String base = displayName == null ? "" : displayName.trim();
            base = base.replaceAll("[\\\\/:*?\"<>|]", "_");
            while (base.startsWith(".")) base = "_" + base.substring(1);
            if (base.length() > 80) base = base.substring(0, 80);
            if (!base.isEmpty() && !base.toLowerCase().endsWith(".txt")) base += ".txt";
            if (base.isEmpty()) base = System.currentTimeMillis() + "_" + (++sFileCounter) + ".txt";

            File dest = new File(dir, base);
            if (dest.exists()) {
                String stem = base.substring(0, base.length() - 4);
                int suffix = 1;
                do {
                    dest = new File(dir, stem + " (" + suffix++ + ").txt");
                } while (dest.exists());
            }
            tmp = new File(dir, dest.getName() + ".part");

            try (InputStream is = getContentResolver().openInputStream(uri);
                 FileOutputStream os = new FileOutputStream(tmp)) {
                if (is == null) throw new IOException("openInputStream returned null");

                byte[] buf = new byte[8192];
                int len;
                long copied = 0;
                int lastPct = -1;
                while ((len = is.read(buf)) != -1) {
                    os.write(buf, 0, len);
                    copied += len;
                    if (totalSize > 0) {
                        int pct = (int)(copied * 100 / totalSize);
                        if (pct >= 100) pct = 99;
                        if (pct / 10 != lastPct / 10) {
                            lastPct = pct;
                            final int shown = pct;
                            runOnUiThread(() -> Toast.makeText(this,
                                    "导入中: " + shown + "%", Toast.LENGTH_SHORT).show());
                        }
                    }
                }
                os.flush();
                os.getFD().sync();
            }

            if (!tmp.renameTo(dest)) throw new IOException("rename failed");
            tmp = null;

            if (displayName != null && !displayName.isEmpty()) {
                File nameFile = new File(dir, dest.getName() + ".name");
                try (OutputStreamWriter w = new OutputStreamWriter(
                        new FileOutputStream(nameFile), StandardCharsets.UTF_8)) {
                    w.write(displayName);
                } catch (Exception ignored) {}
            }

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
