plugins {
    alias(libs.plugins.android.application)
}

android {
    namespace = "com.mobileglues.adreno.plugin"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.mobileglues.adreno.plugin"
        minSdk = 26
        targetSdk = 34
        versionCode = project.findProperty("versionCode")?.toString()?.toInt() ?: 1
        versionName = project.findProperty("versionName")?.toString() ?: "1.0"

        // ---------------------------------------------------------------------------
        // ZalithLauncher / FCL renderer plugin metadata
        //
        // renderer format: "DisplayName:renderer_lib.so:egl_lib.so"
        //   ZalithLauncher will dlopen renderer_lib.so for OpenGL calls
        //   and egl_lib.so for EGL surface management.
        //
        // pojavEnv / boatEnv: colon-separated KEY=VALUE pairs injected into
        //   the environment before Minecraft starts.
        //   MG_PLUGIN_STATUS=1 tells MobileGlues it is running as a plugin.
        // ---------------------------------------------------------------------------
        manifestPlaceholders["des"]       = "High-performance OpenGL→GLES renderer optimised for Adreno 7xx (Snapdragon 8 Gen 3)"
        manifestPlaceholders["renderer"]  = "MobileGlues-Adreno:libmobileglues.so:libEGL.so"
        manifestPlaceholders["boatEnv"]   = "MG_PLUGIN_STATUS=1"
        manifestPlaceholders["pojavEnv"]  = "MG_PLUGIN_STATUS=1"
        manifestPlaceholders["minMCVer"]  = "1.21"
        manifestPlaceholders["maxMCVer"]  = "1.21.11"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            // Use the default debug signing for now so ZalithLauncher can sideload it.
            // Replace with a real keystore before publishing to any store.
            signingConfig = signingConfigs.getByName("debug")
        }
    }

    // Pack the native library uncompressed so the OS can dlopen() it directly
    // from the APK without extracting it first (API 23+ feature).
    // We keep extractNativeLibs=true in the manifest for API < 23 compatibility,
    // but also mark it here for documentation.
    packaging {
        jniLibs {
            useLegacyPackaging = true  // ensures extractNativeLibs=true behaviour
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }
}

dependencies {
    // Intentionally empty — this APK is just a native library container.
}
