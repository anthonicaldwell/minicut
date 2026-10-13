# minicut — Project Summary & Reference Guide

## 1. Project Overview
**minicut** is a lightweight, modern multi-track desktop video editor written in C++17 for Linux (optimized for KDE Plasma). It features real-time preview, multi-track audio and video editing, keyframe animation (position, rotation, scale, opacity, volume), project save/load, and asynchronous background MP4 export powered by FFmpeg and Dear ImGui.

---

## 2. Technology Stack & Dependencies
- **Language**: C++17
- **UI Framework**: Dear ImGui (v1.91.8, Docking branch), styled with KDE Plasma / Breeze Dark theme
- **Window & Context**: SDL2 (Window creation, input events, audio device playback)
- **Rendering**: Modern OpenGL 3.3+ via GLEW (Texture rendering, timeline preview, asset thumbnails)
- **Media Engine**: FFmpeg (`libavcodec`, `libavformat`, `libswscale`, `libswresample`, `libavutil`)
- **Serialization**: nlohmann/json (v3.11.3)
- **Build System**: CMake (>= 3.16)

---

## 3. Directory & File Structure

```
/home/kaiju/.local/bin/
├── CMakeLists.txt              # CMake build definitions (minicut & test_minicut)
├── install.sh                  # Installer script for KDE Plasma desktop integration
├── uninstall.sh                # Standalone uninstaller (removes core files, preserves dependencies)
├── minicut.desktop             # Desktop launcher specification (StartupWMClass, MIME associations)
├── minicut.xml                 # FreeDesktop MIME type package (application/x-minicut)
├── minicut.png                 # Application icon (256x256)
├── imgui.ini                   # ImGui saved panel layout and docking configuration
├── demo_colorbars.mp4          # Demo video media for testing
├── demo_tone.mp3               # Demo audio tone for testing
├── test_sample.png             # Sample image for image-track testing
├── minicut                     # Compiled production binary
├── build/                      # Build output directory
│   ├── minicut                 # Main executable
│   └── test_minicut            # Automated test suite executable
├── locales/                    # JSON localization dictionaries (en, ja/jp, ko, uk, ar)
├── src/                        # Core C++ source files
│   ├── main.cpp                # Application entry point with CLI argument forwarding
│   ├── Common.h                # Core data models, transform blitting, and keyframe helpers
│   ├── App.h / App.cpp         # Main application controller, SDL/GL loop, docking & splitters
│   ├── Timeline.h / .cpp       # Multi-track timeline UI, scrubbing, snapping, split/trim, paste
│   ├── ClipManager.h / .cpp    # Media pool/library panel, asset import & thumbnails
│   ├── KeyframeEditor.h / .cpp # Context Inspector (Audio, Video, Image tools & curve graph)
│   ├── ExportManager.h / .cpp  # FFmpeg asynchronous multi-track export pipeline to MP4
│   ├── MediaDecoder.h / .cpp   # FFmpeg demuxing, video frame & audio decoding
│   ├── AudioEngine.h / .cpp    # SDL2 audio callback & synchronized audio mixer
│   ├── NativeFileDialog.h / .cpp # Non-blocking system file dialogs (kdialog / zenity)
│   ├── ProjectSerializer.h / .cpp # JSON save/load system (.minicut) with transform/depth persistence
│   ├── FileBrowser.h / .cpp    # In-app file dialog with navigation and system thumbnail cache
│   ├── TempManager.h / .cpp    # Centralized ~/.config/minicut/temp manager & disk cleanup
│   ├── LocaleManager.h / .cpp  # Multi-language translation manager with embedded fallbacks
│   └── MD5.h                   # Lightweight RFC 1321 MD5 hasher for FreeDesktop thumbnail URIs
├── tests/
│   └── test_minicut.cpp        # Full test suite (16 unit, integration, and pipeline tests)
└── third_party/                # Embedded / fallback dependencies (FFmpeg headers, ImGui, JSON)
```

---

## 4. Architecture & Key Subsystems

### A. Data Model & Transform Pipeline (`src/Common.h`)
- **`Clip`**: Holds timeline position (`start_time`, `duration`, `offset`) and default transform properties:
  - `pos_x` (X offset in pixels relative to center)
  - `pos_y` (Y offset in pixels relative to center)
  - `rotation` (rotation in degrees)
  - `scale` (scale multiplier, 1.0 = 100%)
  - `opacity` (alpha multiplier [0.0, 1.0])
- **`Project`**: Evaluates parameter keyframes (`get_clip_param`), checks keyframes at playhead (`has_keyframe_at`), removes keyframes (`remove_keyframe_at`), and jumps between keyframes (`find_prev_keyframe_time`, `find_next_keyframe_time`).
- **`blit_transformed_layer()`**: High-performance bilinear-interpolated affine image/video transformation function. Computes inverse mapping and destination bounding boxes, scaling cleanly across preview (1280x720) and export resolutions.

### B. Lower-Right Context Panel (`src/KeyframeEditor.cpp`)
- Replaces static keyframe view with a dynamic context panel that switches based on selection:
  - **No Selection**: Displays project overview (name, duration, track counts, asset totals) and editing tips.
  - **Image Selected**: Image inspector (resolution, duration), animation presets (Reset, Fade In/Out, Ken Burns Zoom In/Out), transform controls (Pos X/Y, Rotation, Scale, Opacity) with keyframe diamond toggles, and the Blender-style keyframe curve graph.
  - **Video Selected**: Video inspector (resolution, FPS, duration), video presets, transform controls, audio volume (if audio present), and curve graph.
  - **Audio Selected**: Audio inspector (sample rate, channels), volume presets (Fade In/Out, Audio Ducking), Gain slider, and volume curve graph.

### C. Media Decoder & Export Manager (`src/MediaDecoder.cpp`, `src/ExportManager.cpp`)
- Real-time preview compositing and background MP4 export evaluate keyframed transforms (`pos_x`, `pos_y`, `rotation`, `scale`, `opacity`) and apply `blit_transformed_layer` for full-resolution rendering.
- **Export Dialog & File Browser**: Export configuration uses a floating utility window (`ImGui::Begin`), completely eliminating ImGui modal stack lockups when opening the file browser dialog. Browse path selection updates destination cleanly.

### D. Multi-Track Timeline & Audio Separation (`src/Timeline.cpp`, `src/App.cpp`, `src/AudioEngine.cpp`)
- **Separated Audio Tracks**: Video clips with audio streams automatically separate their audio onto an audio track (`clip_a_...`) at matching start time, duration, and offset. This enables independent volume keyframing, trimming, track muting, and soloing.
- **Timeline Context Menus**: Right-clicking a clip opens a context menu with actions: "Separate Audio Track", "Split at Playhead", "Cut", "Copy", "Delete [Del]", and "Reset Transforms". Right-clicking track lanes provides track management.

### E. Clip Pool, Asset Management & Prefabs (`src/ClipManager.cpp`, `src/Common.h`)
- **Project Prefab Clips**:
  - Entire `.minicut` projects can now be imported directly into the Clip Pool as **Prefab Clips** via the menu (`File -> Import Project as Prefab...`) or toolbar (`+ Prefab...`).
  - Prefabs display in the Clip Pool with a distinct purple `[PF]` badge, duration, track counts, and total clip statistics.
  - Adding or dragging a prefab to the timeline instantiates all its video and audio tracks, clips, and animated keyframes starting at the playhead or drop timestamp.
  - Assets from the prefab are automatically registered and deduplicated in the host project library.
  - Instantiated prefab clips are tagged with `prefab_id` (`[PF: ProjectName]`) with distinct timeline tinting, and offer context menu actions: "Delete Prefab Block" and "Dissolve Prefab (Keep Clips)".
  - Full `.minicut` JSON serialization and deserialization support for prefabs and clip provenance.
- **Right-Click Context Menus**:
  - On asset rows: displays asset specs, usage counter ("Used in X timeline clip(s)"), "+ Add to Timeline", "Copy File Path", "- Delete from Clip Pool [Del]", "- Delete from Pool Only", and "Delete All Unused Assets".
  - On empty space: provides quick import options ("+ Import Video...", "+ Import Image...", "+ Import Audio...", "+ Import Project as Prefab..."), "Delete All Unused Assets", and "Clear Selection".
- **Multiple Deletion Mechanisms**:
  - `Del` or `Backspace` shortcut when an asset or prefab is selected.
  - Dedicated "Delete Asset [Del]" button in the Clip Pool action bar.
  - Context menu items for deleting individual assets or purging all unused assets.
  - Asset deletion cleans up all timeline clip references and associated keyframes to maintain project integrity.
- **Fast Timeline Placement**: Double-clicking an asset or prefab row or pressing `Enter` instantly places it at the playhead position.

### F. File Browser Enhancements (`src/FileBrowser.cpp`, `src/FileBrowser.h`)
- **Video & Image Thumbnails**: File lists now feature a dedicated "Thumb" column displaying real-time decoded OpenGL thumbnails for videos and images using FFmpeg.
- **Selected Media Preview Card**: Selecting any media file displays a visual preview thumbnail alongside dimensions, format, and file size.
- **Mounted Media & Removable Devices**:
  - Automatically discovers and lists mounted storage devices (SD cards like `/run/media/$USER/SD_256`, USB drives, `/mnt` volumes, external disks) under a dedicated **MOUNTED MEDIA** section in the sidebar.
  - Displays device volume label, filesystem type, total capacity, and remaining free space tooltips with one-click direct folder navigation.

### G. Desktop & System Integration
- **`minicut.desktop`**: Configured with `StartupWMClass=minicut` and associated with `application/x-minicut;video/mp4;video/x-matroska;video/quicktime;`.
- **`minicut.xml`**: Shared MIME type definition for `.minicut` project files.
- **`main.cpp` & `App::init()`**: Accepts initial file argument to directly open `.minicut` projects or import dropped media files.
- **`install.sh`**: Installs binary, desktop launcher, icons, MIME types, and refreshes desktop/MIME/icon/KDE sycoca caches.
- **`uninstall.sh`**: Cleanly removes minicut core files without touching any system libraries or dependencies.

### H. Scrollable Timeline & Multi-Axis Navigation (`src/Timeline.h`, `src/Timeline.cpp`)
- **Horizontal Scrolling & Panning**:
  - Horizontal mouse wheel panning (`io.MouseWheelH`), `Shift` + vertical mouse wheel panning, and middle-click drag (`ImGuiMouseButton_Middle` / button 2) or `Alt` + left-click drag.
  - Dedicated bottom scrollbar track featuring a proportional draggable thumb, direct track clicking to jump, and toolbar jump buttons (`|<`, `>|`) alongside a fast-navigation slider.
  - Sub-pixel boundary clamping keeping the timeline bounds strictly within `[0.0, max_time]`.
  - Automatic playhead tracking during playback to keep active playback visible.
- **Vertical Track Scrolling & Clipping**:
  - Automatically scrolls vertically when total tracks exceed visible canvas height (`m_scroll_track_y`).
  - Track rendering strictly wrapped with `ImDrawList::PushClipRect` / `PopClipRect` to prevent drawing bleed over the ruler header and bottom scrollbar.
  - Smooth vertical scroll position indicator drawn along the right edge.

### I. Native System File Dialog Integration (`src/NativeFileDialog.h`, `src/NativeFileDialog.cpp`)
- **Non-Blocking Asynchronous Dialogs**:
  - Auto-detects desktop environment dialog tools: `/usr/bin/kdialog` (KDE Plasma native) and `/usr/bin/zenity` (GTK fallback).
  - Dialog commands execute inside detached background worker threads (`std::thread`), completely preventing SDL event loop freezes or OS unresponsive-application warnings.
  - Results and chosen paths are posted to a thread-safe callback queue drained synchronously on the main thread via `NativeFileDialog::poll_callbacks()`.
- **Integrated Actions**:
  - File -> Open Project (`Ctrl+O`)
  - File -> Save Project As (`Ctrl+Shift+S`)
  - File -> Import Video (`Ctrl+I`), Import Image, Import Audio, and Import Project as Prefab
  - Export Dialog destination directory picker
- **Toggleable Fallback**:
  - Option `File -> Use System File Dialog` toggles between native system dialogs and minicut's in-app `FileBrowser`.

### J. Clip Import Progress & Asynchronous Ingestion (`src/ClipManager.h`, `src/ClipManager.cpp`, `src/MediaDecoder.h`)
- **Multi-Stage Import Progress State**:
  - `ImportProgressState` tracks background progress with `is_importing`, fractional `progress` (0.0 to 1.0), and stage descriptions:
    - `"Probing media streams..."`
    - `"Decoding thumbnail..."`
    - `"Preloading audio track..."`
    - `"Import complete!"`
- **Thread-Safe OpenGL Thumbnail Ingestion**:
  - Offloaded CPU-side RGBA thumbnail decoding via `MediaDecoder::decode_thumbnail_rgba(...)` on background worker threads (`start_async_import_video`, `start_async_import_image`, etc.).
  - Main thread uploads CPU buffers to OpenGL textures via `MediaDecoder::upload_rgba_texture(...)` during `poll_async_imports()`, ensuring zero OpenGL context race conditions.
- **Visual Progress Feedback**:
  - Inline progress bar and status message rendered at the bottom of the Clip Pool panel.
  - Centered modal progress window rendered during active imports for clear user visibility.

### K. Audio Preview Engine & Dynamic Preload Caching (`src/AudioEngine.h`, `src/AudioEngine.cpp`, `src/App.cpp`)
- **Robust FFmpeg 8.0 Resampling (`SwrContext`)**:
  - Dynamically initializes FFmpeg's `SwrContext` from decoded audio frame parameters (`frame->ch_layout`, `frame->sample_rate`, `frame->format`) rather than uninitialized codec context parameters.
  - Flushes decoder delay samples via `swr_get_delay` into the cached sample buffer.
- **Multi-Track Preview Mixer**:
  - Multi-track audio engine decodes and caches uncompressed float samples for all video tracks and separated audio tracks (`ensure_project_assets_loaded`).
  - Plays audio from video clips directly on video tracks unless explicitly separated onto an audio track, avoiding silent video previews.
  - Synchronized SDL2 playback callback with sub-millisecond precision, volume keyframing, and master volume control.

### L. Centralized Temporary File Management (`src/TempManager.h`, `src/TempManager.cpp`)
- **Safe Dedicated Storage**:
  - All intermediate audio stems, PCM/WAV dumps, and export buffers are strictly stored in `~/.config/minicut/temp/` (XDG compliant).
  - Eliminates uncontrolled disk pollution in `/tmp` or hidden project locations.
- **Interactive Storage Cleanup**:
  - `Project -> Cleanup Temporary Files` action scans the temporary directory and purges stale render files.
  - Reports files removed and exact megabytes/kilobytes of disk space reclaimed.

### M. Menu Reorganization & Multi-Language Localization (`src/LocaleManager.h`, `src/LocaleManager.cpp`, `locales/`)
- **Structured Top-Level Menu Bar**:
  - **File**: New Project, Open Project, Save Project, Save Project As, Export Video to MP4, Exit.
  - **Edit**: Undo, Redo, Cut, Copy, Paste, Delete, Split, Select All.
  - **Project**: Import Video, Import Audio, Import Image, Import Prefab, Generate Demo Media, Cleanup Temporary Files.
  - **Settings**: Use System File Dialog, Language submenu (`English`, `日本語`, `한국어`, `Українська`, `العربية`).
  - **Help**: About minicut, Keyboard Shortcuts.
- **Multi-Language Localization Subsystem**:
  - Supported languages: English (`en`), Japanese (`ja`/`jp`), Korean (`ko`), Ukrainian (`uk`), and Arabic (`ar`).
  - Runtime loading from `locales/locale_<code_>.json` with embedded C++ dictionaries as fallback for 100% offline reliability.
  - Comprehensive multi-script ImGui font atlas merging: Latin & Cyrillic (`NotoSans-Regular.ttf`), CJK Japanese & Korean (`NotoSansCJK-Regular.ttc`), and Arabic (`NotoSansArabicUI-Regular.ttf`) providing over 16,300 glyphs across all 5 languages without missing-glyph dropouts.
  - Automatically installed by `install.sh` to `~/.config/minicut/locales/` and `~/.local/share/minicut/locales/`, with `LocaleManager` prioritizing user config directory overrides.
  - User settings (language, system dialog preferences, layout splitters) persisted to `~/.config/minicut/settings.json`.

### N. Interactive 4-Way View Resizing (`src/App.h`, `src/App.cpp`)
- **Draggable Fluid Layout Splitters**:
  - The 4 primary workspace sections (Clip Library, Video Preview, Timeline, and Context Inspector) are interactively resizable via draggable splitters.
  - Horizontal splitter (`m_split_h`) adjusts the height ratio between top panels and bottom panels.
  - Upper vertical splitter (`m_split_top_v`) adjusts the width ratio between the Clip Library and Video Preview.
  - Lower vertical splitter (`m_split_bottom_v`) adjusts the width ratio between the Multi-Track Timeline and Context Inspector.
  - Custom splitters provide responsive, stable panel resizing across all screen resolutions without docking instability.

### O. System Thumbnail Cache Integration (`src/MD5.h`, `src/FileBrowser.h`, `src/FileBrowser.cpp`)
- **Linux FreeDesktop Cache Lookup**:
  - Implements canonical FreeDesktop thumbnail cache lookup (`~/.cache/thumbnails/{large,normal,x-large,xx-large}/`).
  - Includes a lightweight RFC 1321 MD5 hash generator to hash `file:///...` URIs without third-party library bloat.
  - Direct loading of cached PNG thumbnails provides instantaneous file browser loading, seamlessly falling back to FFmpeg decoding only for uncached media.

### P. Track Render Depth & Layer Compositing Order (`src/Common.h`, `src/MediaDecoder.cpp`, `src/ExportManager.cpp`, `src/Timeline.cpp`)
- **Configurable Z-Depth (0 to 100)**:
  - Each video/image track has an integer `depth` property (default: 0).
  - During real-time preview and MP4 export, video tracks are composited in ascending order of depth (lower renders background, higher renders foreground).
  - Track headers provide interactive depth drag controls (`Z:%d`) and right-click context menu options ("Set Render Depth", "Bring Forward", "Send Backward").
  - Depth ordering is fully preserved across project saves and loads in `.minicut` JSON files.

### Q. Timeline Usability & Editing Bug Fixes
- **Canvas Clipping Bounds (BUG 1 Fixed)**:
  - Timeline clips scrolling off the right edge are strictly clipped to the timeline canvas bounds. Clicking within the Context Inspector or Tools panel no longer triggers accidental clip selection or timeline interaction.
- **Targeted Clip Splitting (BUG 2 Fixed)**:
  - Slicing and splitting actions prioritize the explicitly selected clip or the active/selected track under the playhead, eliminating unintended splits on track 0.
- **Context-Aware Pasting (BUG 3 Fixed)**:
  - Pasting (`Ctrl+V` or right-click "Paste") pastes clips into the hovered or context-clicked track lane rather than defaulting to track 0.

---

## 5. Build, Test, and Execution Commands

### Building
```bash
cmake -B build -S .
cmake --build build -j$(nproc)
```

### Running the Application
```bash
./minicut [optional_file.minicut]
```

### Running the Test Suite
```bash
./build/test_minicut
```
*(All 16 unit, integration, and pipeline tests passing)*

### Installation & Uninstallation
```bash
# Install to ~/.local (binary, desktop launcher, icons, MIME types, locales):
./install.sh --user

# Cleanly uninstall minicut core files (leaves system dependencies intact):
./uninstall.sh
```
