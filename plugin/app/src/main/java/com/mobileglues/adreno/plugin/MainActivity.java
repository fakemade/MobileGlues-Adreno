// MobileGlues-Adreno - plugin/app/src/main/java/.../MainActivity.java
// SPDX-License-Identifier: GPL-3.0-or-later
package com.mobileglues.adreno.plugin;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;
import android.view.Gravity;

/**
 * Minimal activity — exists only so the APK is installable.
 * ZalithLauncher / FCL loads this plugin by scanning the installed APK's
 * manifest for <meta-data android:name="fclPlugin" android:value="true" />.
 */
public class MainActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        TextView tv = new TextView(this);
        tv.setText("MobileGlues-Adreno\nHigh-performance OpenGL→GLES renderer\nfor Adreno 7xx (Snapdragon 8 Gen 3)\n\nInstall this APK and select\n\"MobileGlues-Adreno\" in ZalithLauncher.");
        tv.setGravity(Gravity.CENTER);
        tv.setPadding(48, 48, 48, 48);
        setContentView(tv);
    }
}
