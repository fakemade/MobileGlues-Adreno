// MobileGlues-Adreno - core/gpu_detect.h
// Copyright (c) 2025-2026 MobileGL-Dev
// Licensed under the GNU General Public License v3.0:
//   https://www.gnu.org/licenses/gpl-3.0.txt
// SPDX-License-Identifier: GPL-3.0-or-later
// End of Source File Header

#pragma once

#include <string>

namespace gpu_detect {

struct GpuInfo {
    std::string renderer;   // GL_RENDERER string, e.g. "Adreno (TM) 750"
    std::string vendor;     // GL_VENDOR string

    bool is_adreno = false;
    int  adreno_gen   = 0;  // 7 for Adreno 7xx, 8 for 8xx, …
    int  adreno_model = 0;  // e.g. 750

    // Adreno fast-path extensions
    bool ext_geometry_shader                = false;
    bool ext_tessellation_shader            = false;
    bool ext_buffer_storage                 = false;
    bool oes_shader_image_atomic            = false;
    bool qcom_tiled_rendering               = false;
    bool ext_multisampled_render_to_texture = false;

    [[nodiscard]] bool is_adreno_7xx() const noexcept {
        return is_adreno && adreno_gen == 7;
    }

    // True when we can use native hardware extensions instead of software emulation.
    [[nodiscard]] bool fast_path_capable() const noexcept {
        return is_adreno_7xx();
    }
};

// Call once during proc_init(), before destroy_temp_egl_ctx().
// Creates its own temporary EGL context to query the driver; safe to call
// before any other GLES initialisation.
void init();

// Returns the detected GpuInfo.  Behaviour is undefined if called before init().
const GpuInfo& get() noexcept;

// Returns true if the named GLES extension is supported on this GPU.
bool has_extension(const char* name) noexcept;

// Writes a human-readable capability summary to the Android log.
void log_capabilities();

} // namespace gpu_detect
