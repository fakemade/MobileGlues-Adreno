// MobileGlues-Adreno - core/gpu_detect.cpp
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
// SPDX-License-Identifier: GPL-3.0-or-later
// End of Source File Header

#include "gpu_detect.h"

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <dlfcn.h>

#include <cctype>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#ifdef __ANDROID__
#  include <android/log.h>
#  define GPU_LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "MobileGlues", __VA_ARGS__)
#  define GPU_LOGW(...) __android_log_print(ANDROID_LOG_WARN,  "MobileGlues", __VA_ARGS__)
#else
#  include <cstdio>
#  define GPU_LOGI(...) do { printf(__VA_ARGS__); printf("\n"); } while (0)
#  define GPU_LOGW(...) do { printf(__VA_ARGS__); printf("\n"); } while (0)
#endif

namespace gpu_detect {

// ---------------------------------------------------------------------------
// Module-level state
// ---------------------------------------------------------------------------

static GpuInfo       g_info;
static bool          g_initialised = false;
static std::string   g_ext_string;   // raw space-separated extension list

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Parse the Adreno model number from a GL_RENDERER string.
// e.g. "Adreno (TM) 750" → 750,  "Adreno 730" → 730
static int parse_adreno_model(const std::string& renderer) {
    auto pos = renderer.find("Adreno");
    if (pos == std::string::npos) return 0;

    // Skip to the first digit after "Adreno"
    pos = renderer.find_first_of("0123456789", pos);
    if (pos == std::string::npos) return 0;

    size_t end = renderer.find_first_not_of("0123456789", pos);
    if (end == std::string::npos) end = renderer.size();

    try {
        return std::stoi(renderer.substr(pos, end - pos));
    } catch (...) {
        return 0;
    }
}

// Check extension in the cached extension string.
static bool ext_present(const char* name) noexcept {
    if (!name || g_ext_string.empty()) return false;
    // Use strstr with word-boundary check to avoid false substring matches.
    const char* p = g_ext_string.c_str();
    size_t      n = strlen(name);
    while ((p = strstr(p, name)) != nullptr) {
        bool left_ok  = (p == g_ext_string.c_str()) || (p[-1] == ' ');
        bool right_ok = (p[n] == '\0')               || (p[n] == ' ');
        if (left_ok && right_ok) return true;
        p += n;
    }
    return false;
}

// ---------------------------------------------------------------------------
// EGL/GLES function pointer typedefs (only what we need)
// ---------------------------------------------------------------------------

using PFN_eglGetDisplay      = EGLDisplay (*)(EGLNativeDisplayType);
using PFN_eglInitialize      = EGLBoolean (*)(EGLDisplay, EGLint*, EGLint*);
using PFN_eglChooseConfig    = EGLBoolean (*)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
using PFN_eglCreateContext   = EGLContext (*)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
using PFN_eglMakeCurrent     = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
using PFN_eglDestroyContext   = EGLBoolean (*)(EGLDisplay, EGLContext);
using PFN_eglTerminate        = EGLBoolean (*)(EGLDisplay);

using PFN_glGetString   = const GLubyte* (*)(GLenum);
using PFN_glGetStringi  = const GLubyte* (*)(GLenum, GLuint);
using PFN_glGetIntegerv = void           (*)(GLenum, GLint*);

// ---------------------------------------------------------------------------
// init()
// ---------------------------------------------------------------------------

void init() {
    if (g_initialised) return;
    g_initialised = true;

    // --- Open EGL and GLES libraries via dlopen so we are independent of the
    //     main loader initialisation order. ---

    void* egl_lib  = dlopen("libEGL.so",    RTLD_LOCAL | RTLD_NOW);
    void* gles_lib = dlopen("libGLESv3.so", RTLD_LOCAL | RTLD_NOW);
    if (!gles_lib)
        gles_lib = dlopen("libGLESv2.so",   RTLD_LOCAL | RTLD_NOW);

    auto cleanup = [&]() {
        if (egl_lib)  dlclose(egl_lib);
        if (gles_lib) dlclose(gles_lib);
    };

    if (!egl_lib || !gles_lib) {
        GPU_LOGW("gpu_detect: failed to open EGL/GLES libraries");
        cleanup();
        return;
    }

    // Load EGL entry points.
    auto fn_eglGetDisplay    = (PFN_eglGetDisplay)   dlsym(egl_lib, "eglGetDisplay");
    auto fn_eglInitialize    = (PFN_eglInitialize)   dlsym(egl_lib, "eglInitialize");
    auto fn_eglChooseConfig  = (PFN_eglChooseConfig) dlsym(egl_lib, "eglChooseConfig");
    auto fn_eglCreateContext = (PFN_eglCreateContext) dlsym(egl_lib, "eglCreateContext");
    auto fn_eglMakeCurrent   = (PFN_eglMakeCurrent)  dlsym(egl_lib, "eglMakeCurrent");
    auto fn_eglDestroyContext = (PFN_eglDestroyContext)dlsym(egl_lib, "eglDestroyContext");
    auto fn_eglTerminate      = (PFN_eglTerminate)    dlsym(egl_lib, "eglTerminate");

    if (!fn_eglGetDisplay || !fn_eglInitialize || !fn_eglChooseConfig ||
        !fn_eglCreateContext || !fn_eglMakeCurrent ||
        !fn_eglDestroyContext || !fn_eglTerminate) {
        GPU_LOGW("gpu_detect: failed to resolve EGL functions");
        cleanup();
        return;
    }

    // --- Create temporary EGL context ---

    EGLDisplay display = fn_eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        GPU_LOGW("gpu_detect: eglGetDisplay failed");
        cleanup();
        return;
    }
    if (fn_eglInitialize(display, nullptr, nullptr) != EGL_TRUE) {
        GPU_LOGW("gpu_detect: eglInitialize failed");
        fn_eglTerminate(display);
        cleanup();
        return;
    }

    const EGLint cfg_attribs[] = {
        EGL_RED_SIZE,       8,
        EGL_GREEN_SIZE,     8,
        EGL_BLUE_SIZE,      8,
        EGL_ALPHA_SIZE,     8,
        EGL_DEPTH_SIZE,     24,
        EGL_SURFACE_TYPE,   EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_NONE
    };
    EGLint    num_configs = 0;
    EGLConfig config      = nullptr;
    if (fn_eglChooseConfig(display, cfg_attribs, &config, 1, &num_configs) != EGL_TRUE
        || num_configs == 0) {
        // Fallback to GLES2
        const EGLint cfg2_attribs[] = {
            EGL_RED_SIZE,        8,
            EGL_GREEN_SIZE,      8,
            EGL_BLUE_SIZE,       8,
            EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_NONE
        };
        fn_eglChooseConfig(display, cfg2_attribs, &config, 1, &num_configs);
    }
    if (num_configs == 0) {
        GPU_LOGW("gpu_detect: eglChooseConfig found no configs");
        fn_eglTerminate(display);
        cleanup();
        return;
    }

    const EGLint ctx_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext ctx = fn_eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attribs);
    if (ctx == EGL_NO_CONTEXT) {
        GPU_LOGW("gpu_detect: eglCreateContext failed");
        fn_eglTerminate(display);
        cleanup();
        return;
    }
    if (fn_eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx) != EGL_TRUE) {
        GPU_LOGW("gpu_detect: eglMakeCurrent failed");
        fn_eglDestroyContext(display, ctx);
        fn_eglTerminate(display);
        cleanup();
        return;
    }

    // --- Query GLES strings ---

    auto fn_glGetString   = (PFN_glGetString)   dlsym(gles_lib, "glGetString");
    auto fn_glGetStringi  = (PFN_glGetStringi)  dlsym(gles_lib, "glGetStringi");
    auto fn_glGetIntegerv = (PFN_glGetIntegerv) dlsym(gles_lib, "glGetIntegerv");

    if (fn_glGetString) {
        const auto* r = fn_glGetString(GL_RENDERER);
        if (r) g_info.renderer = reinterpret_cast<const char*>(r);
        const auto* v = fn_glGetString(GL_VENDOR);
        if (v) g_info.vendor = reinterpret_cast<const char*>(v);

        // Build extension string (GLES3: use glGetStringi; GLES2 fallback: glGetString)
        if (fn_glGetStringi && fn_glGetIntegerv) {
            GLint num_exts = 0;
            fn_glGetIntegerv(GL_NUM_EXTENSIONS, &num_exts);
            for (GLint i = 0; i < num_exts; ++i) {
                const auto* e = fn_glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i));
                if (e) {
                    if (!g_ext_string.empty()) g_ext_string += ' ';
                    g_ext_string += reinterpret_cast<const char*>(e);
                }
            }
        } else {
            const auto* exts = fn_glGetString(GL_EXTENSIONS);
            if (exts) g_ext_string = reinterpret_cast<const char*>(exts);
        }
    }

    // --- Tear down temporary context ---

    fn_eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    fn_eglDestroyContext(display, ctx);
    fn_eglTerminate(display);
    cleanup();

    // --- Parse GPU info ---

    g_info.is_adreno = (g_info.renderer.find("Adreno") != std::string::npos);
    if (g_info.is_adreno) {
        g_info.adreno_model = parse_adreno_model(g_info.renderer);
        if (g_info.adreno_model >= 100) {
            g_info.adreno_gen = g_info.adreno_model / 100;
        }
    }

    // --- Check Adreno fast-path extensions ---

    g_info.ext_geometry_shader =
        ext_present("GL_EXT_geometry_shader") ||
        ext_present("GL_OES_geometry_shader");

    g_info.ext_tessellation_shader =
        ext_present("GL_EXT_tessellation_shader") ||
        ext_present("GL_OES_tessellation_shader");

    g_info.ext_buffer_storage =
        ext_present("GL_EXT_buffer_storage");

    g_info.oes_shader_image_atomic =
        ext_present("GL_OES_shader_image_atomic");

    g_info.qcom_tiled_rendering =
        ext_present("GL_QCOM_tiled_rendering");

    g_info.ext_multisampled_render_to_texture =
        ext_present("GL_EXT_multisampled_render_to_texture");
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

const GpuInfo& get() noexcept {
    return g_info;
}

bool has_extension(const char* name) noexcept {
    return ext_present(name);
}

void log_capabilities() {
    GPU_LOGI("=== GPU Detection ===");
    GPU_LOGI("  Renderer : %s", g_info.renderer.empty() ? "(unknown)" : g_info.renderer.c_str());
    GPU_LOGI("  Vendor   : %s", g_info.vendor.empty()   ? "(unknown)" : g_info.vendor.c_str());
    GPU_LOGI("  Adreno   : %s (gen=%d model=%d)",
             g_info.is_adreno ? "YES" : "NO",
             g_info.adreno_gen, g_info.adreno_model);
    GPU_LOGI("  Fast path: %s", g_info.fast_path_capable() ? "ENABLED (Adreno 7xx)" : "disabled");
    GPU_LOGI("  Extensions:");
    GPU_LOGI("    EXT_geometry_shader              : %s", g_info.ext_geometry_shader              ? "YES" : "no");
    GPU_LOGI("    EXT_tessellation_shader          : %s", g_info.ext_tessellation_shader          ? "YES" : "no");
    GPU_LOGI("    EXT_buffer_storage               : %s", g_info.ext_buffer_storage               ? "YES" : "no");
    GPU_LOGI("    OES_shader_image_atomic          : %s", g_info.oes_shader_image_atomic          ? "YES" : "no");
    GPU_LOGI("    QCOM_tiled_rendering             : %s", g_info.qcom_tiled_rendering             ? "YES" : "no");
    GPU_LOGI("    EXT_multisampled_render_to_texture: %s", g_info.ext_multisampled_render_to_texture ? "YES" : "no");
    GPU_LOGI("=====================");
}

} // namespace gpu_detect
