# Project: AdrenoForge — High-Performance OpenGL→GLES Renderer for Android Minecraft

## Origin & Context

This project is a fork and heavy optimization of **MobileGlues** (https://github.com/MobileGL-Dev/MobileGlues), an OpenGL→OpenGL ES translation layer that allows Minecraft Java Edition to run on Android via launchers like PojavLauncher and ZalithLauncher.

The original MobileGlues is designed for broad compatibility across Mali, Adreno, PowerVR, and Xclipse GPUs, which forces it to emulate many features in software. This fork **specialises for high-end Adreno 7xx GPUs** and uses native hardware features wherever possible.

## Target Environment

- **Device:** Lenovo Legion Y700 (2nd gen, 2023)
- **SoC:** Qualcomm Snapdragon 8 Gen 3
- **GPU:** Adreno 750
- **RAM:** 16 GB
- **OS:** Android 13+
- **Launcher:** ZalithLauncher (PojavLauncher-based, supports renderer plugins)
- **Minecraft version (priority):** 1.21.11
- **Modloader:** Fabric
- **Performance mods:** Sodium 0.6+, Iris 1.8+
- **Target shaderpacks:** BSL, Complementary Reimagined, SEUS PTGI, Bliss, Sildur's Vibrant
- **Target GLES version:** OpenGL ES 3.2
- **Target ABI:** arm64-v8a (primary), armeabi-v7a optional

## Project Goals

1. Achieve measurably higher FPS than upstream MobileGlues on the target device, both vanilla and with Sodium+Iris + shaders.
2. Integrate as a **ZalithLauncher renderer plugin** (see https://github.com/ZalithLauncher for plugin spec).
3. Build via GitHub Actions — the developer does not have a powerful laptop and will not compile locally.
4. Keep the project publicly licensed (GPL-3.0 to match upstream and ZalithLauncher ecosystem).

## Architecture Overview

Language: **C++20** (primary), C (for GL ABI boundary), GLSL.

Core modules, in rough dependency order:

1. **`core/gpu_detect`** — runtime detection of Adreno generation and supported extensions. Enables "Adreno fast path" when running on Adreno 7xx.
2. **`core/state_tracker`** — caches GL state client-side. Elides redundant driver calls (Minecraft repeatedly sets the same state).
3. **`core/buffer_manager`** — wraps GLES buffer objects. Uses `EXT_buffer_storage` persistent-mapped buffers for Sodium chunk meshes; falls back to `glMapBufferRange` where unavailable.
4. **`core/shader_cache`** — on-disk cache of transpiled GLSL and compiled program binaries (via `GL_OES_get_program_binary`). Keyed by hash of source + driver version. Target path: `<launcher_data>/adrenoforge/shader_cache/`.
5. **`gl/hot_path`** — aggressively inlined, NEON-accelerated implementations of `glDrawElements`, `glDrawArrays`, `glUniform*`, `glBindBuffer`, `glVertexAttribPointer`. These are the most-called functions in a Minecraft frame.
6. **`gl/legacy_emu`** — immediate mode (`glBegin`/`glEnd`), display lists, matrix stack, fixed-function pipeline emulation. Required for Minecraft's legacy GL code paths before Sodium takes over.
7. **`glsl/transpiler`** — desktop GLSL (330/420/460) → GLES 320 translator. **Critical for Iris shaderpacks.** Multithreaded (uses all cores of 8 Gen 3: 1× Cortex-X4 + 5× A720 + 2× A520). Must handle: UBO/SSBO, geometry shaders, compute shaders, image load/store, atomic counters, explicit uniform locations.
8. **`glsl/intrinsics`** — maps desktop GLSL built-ins to GLES equivalents.
9. **`platform/jni_bridge`** — JNI entry points expected by ZalithLauncher's renderer plugin interface.
10. **`platform/egl_setup`** — EGL context creation, pbuffer/window surface management.

## Optimisation Priorities (in order)

1. **Adreno fast path** — detect GPU, bypass emulation for `EXT_geometry_shader`, `EXT_tessellation_shader`, `EXT_buffer_storage`, `OES_shader_image_atomic`, `QCOM_tiled_rendering`, `EXT_multisampled_render_to_texture`. Expected: +30–50% FPS on Sodium.
2. **Persistent mapped buffers** for Sodium chunk meshes — eliminates per-frame data copies.
3. **On-disk shader cache** — Iris currently recompiles every launch; caching saves seconds per startup.
4. **Multithreaded GLSL transpilation** — parallelise per-shader-stage work.
5. **State tracker** — drop no-op state changes.
6. **Hot-path inlining + batching** of `glUniform*` and draw calls.
7. **NEON SIMD** for matrix multiplication, vertex transform fallbacks, and memcpy on large buffers.
8. **JNI call batching** — reduce Java↔native boundary crossings.

## Build & CI

- **No local builds.** All compilation happens on GitHub Actions.
- Workflow targets: `arm64-v8a` release build, produces `libadrenoforge.so` as an artefact.
- Use `actions/cache` for NDK and CMake build directories.
- Matrix builds for Debug / Release / Release-with-LTO.
- On tag `v*`, auto-publish a GitHub Release with the `.so` attached.
- NDK version: r26d or newer (required for C++20 features on Android).
- CMake 3.22+.
- Android API level: compileSdk 34, minSdk 26 (Android 8.0, matches ZalithLauncher minimum).

## Integration with ZalithLauncher

ZalithLauncher uses a **renderer plugin** system. The build must package `libadrenoforge.so` in the directory structure expected by the launcher. Plugin spec reference: https://github.com/ZalithLauncher (see ZalithLauncher or ZalithLauncher2 source for the exact ABI).

Before finalising the JNI bridge, inspect the ZalithLauncher2 source tree for the current plugin entry-point signatures and required metadata file.

## Development Workflow

1. Claude Code edits source files in the repo.
2. Commits and pushes trigger GitHub Actions.
3. User downloads the `.so` artefact from the Actions run.
4. User copies it into ZalithLauncher's renderer plugin directory on the Legion Y700.
5. User launches Minecraft 1.21.11 with Sodium+Iris and a shaderpack; captures logs and FPS.
6. Logs/results are pasted back into Claude Code for the next iteration.

## Milestones

- **M1 — Scaffolding:** CMake project, GitHub Actions workflow producing a `.so`, stub JNI entry points, GPU detection module working. Target: vanilla Minecraft (no Sodium) launches and renders a menu.
- **M2 — Vanilla parity:** Full GL → GLES translation sufficient for vanilla Minecraft 1.21.11 gameplay without Sodium. Measurable FPS.
- **M3 — Sodium:** Sodium 0.6+ works. Persistent mapped buffers path active. Benchmark vs upstream MobileGlues.
- **M4 — Iris + shaders:** GLSL transpiler sufficient for BSL and Complementary. Shader cache active.
- **M5 — Heavy shaders:** SEUS PTGI, Bliss working.
- **M6 — Optimisation pass:** NEON, state tracker tuning, hot-path profiling with `simpleperf`.

## Coding Conventions

- C++20, no exceptions in hot paths (use `std::expected`-style error returns).
- No RTTI in the GL ABI layer.
- `snake_case` for functions and variables, `PascalCase` for types, `kCamelCase` for constants.
- Aggressive `[[likely]]` / `[[unlikely]]` hints on hot paths.
- `-O3 -flto -fno-exceptions -fno-rtti` for release builds.
- Every GL entry point in `gl/hot_path` must be measured — no speculative optimisation without a profile.

## Upstream Sync

Keep a `upstream/main` branch tracking MobileGlues. Periodically rebase or cherry-pick bug fixes. Our optimisations live on `main`.

## Non-Goals

- Support for non-Adreno GPUs (Mali, PowerVR, Xclipse) — upstream MobileGlues covers those.
- Support for OpenGL ES versions below 3.2.
- Windows or desktop Linux builds.
- Supporting Minecraft versions below 1.21.

## Useful References

- Upstream: https://github.com/MobileGL-Dev/MobileGlues
- ZalithLauncher org: https://github.com/ZalithLauncher
- ZalithLauncher2 (target launcher): https://github.com/ZalithLauncher/ZalithLauncher2
- LWJGL fork used by Zalith: https://github.com/ZalithLauncher/lwjgl3
- gl4es (ancestor of MobileGlues): https://github.com/ptitSeb/gl4es
- Adreno extensions: https://registry.khronos.org/OpenGL/index_es.php
- Sodium: https://github.com/CaffeineMC/sodium
- Iris: https://github.com/IrisShaders/Iris

## First Tasks When Opening Claude Code

1. Fork https://github.com/MobileGL-Dev/MobileGlues into the user's GitHub account.
2. Clone the fork locally (Claude Code handles this in the sandbox).
3. Add this `CLAUDE.md` at the repo root.
4. Inspect `ZalithLauncher2` source to confirm the renderer plugin ABI.
5. Set up the GitHub Actions workflow (`.github/workflows/build.yml`) and verify it produces a `.so`.
6. Begin milestone M1: `core/gpu_detect` and skeleton JNI bridge.

## Open Questions To Resolve Early

- Exact ABI of ZalithLauncher2 renderer plugins (inspect its source before writing `platform/jni_bridge`).
- Whether the upstream MobileGlues CMake layout should be preserved or restructured. Default: **preserve**, add our modules alongside.
- Licensing header to put on new files. Default: GPL-3.0-or-later, matching upstream.