# minicut

[English](README.md) | [العربية](docs/README_ar.md) | [日本語](docs/README_ja.md) | [한국어](docs/README_ko.md) | [Українська](docs/README_uk.md)

**minicut** is a fast, lightweight, and modern multi-track non-linear video editor built specifically for Linux desktop environments. It delivers a streamlined, responsive workflow for video, audio, image, and text editing without unnecessary bloat or complex subscription layers.

![minicut Interface](docs/screenshot.png)

---

## The Story: How It Was Made (Vibe Coded)

`minicut` was conceived and developed through an interactive "vibe coding" journey with **Google DeepMind Antigravity**, an advanced AI pair programming assistant.

Starting from an initial architectural idea, the application was engineered from scratch in modern **C++20**:
- **Rendering & UI**: Hardware-accelerated OpenGL 3.3 backend coupled with **Dear ImGui**, customized with a KDE Plasma Breeze Dark theme.
- **Media Engine**: Native **FFmpeg** pipeline for high-performance frame decoding, audio resampling, and multi-threaded video encoding (H.264/AAC, OGG/Theora).
- **Typography & Localization**: Vector glyph rendering via **FreeType 2** with native cursive BiDi shaping for Arabic and complete CJK coverage for Japanese and Korean.
- **Dual Embedded Scripting**: Fully sandboxed **LuaJIT** and **Duktape JavaScript** runtimes empowering users to script timeline actions and build custom interactive dockable tool panels.

---

## Key Features

- **Multi-Window Dual-Monitor Workflow**:
  - **Pop-out Preview Window**: Detach the video preview canvas into its own native OS window (`SDL_Window*`) with preserved aspect ratio buffer zones, interactive transform gizmo, numerical transform controls, and transport bar.
  - **Standalone Export Window**: Drag the Export dialog and render progress tracker out of the way onto another monitor and continue timeline editing without modal blocking or dimming.
- **Universal Transform Tool & Interactive Gizmo**:
  - Center crosshair, corner scaling handles, top rotation pin, and direct canvas translation with full keyframe interpolation support.
- **Multi-Track Timeline**:
  - Unlimited video, audio, and subtitle tracks with interactive track header reordering, renaming, and Z-depth management.
  - Drag assets freely across tracks of matching types.
- **Stereo Audio Engine & Dual-Channel Keyframing**:
  - Timeline audio tracks support stereo panning and independent Left and Right channel volume curve keyframing.
  - Real-time preview audio volume attenuation from -60 dB to +6 dB with mute.
- **Text-to-Speech (TTS) Studio**:
  - Built-in multi-lingual speech synthesis supporting English, Japanese (with Kanji transliteration via `kakasi`/`mecab`/kana dictionary fallback), Korean, Ukrainian, and Arabic.
- **Aspect Ratio Engine & Auto-Detection**:
  - Preview and export adapt dynamically to 16:9 Wide, 9:16 Shorts/Reels, 4:3 Standard, 1:1 Square, 21:9 Cinema, 4:5 Portrait, or Auto (timeline's largest asset).
- **FreeType Text Generator & Subtitle Sidecars**:
  - Dedicated typography generator supporting live composition preview, outline strokes, letter/line spacing, and multi-lingual scripts (Latin, Cyrillic, Japanese, Korean, and right-to-left Arabic).
  - Export embedded subtitles or separate `.srt` sidecar files.
- **Smart Auto-Length Export**:
  - Video exports automatically stop where assets end, or extend to custom durations when unchecking auto-length.
  - Dual progress tracking: Audio mixing (stereo & keyframes) and Video rendering & encoding.
- **Universal IME Composition Overlay**:
  - Auto-completion and composition overlays rendered natively across all text boxes, search entries, and dialogs for Japanese, Korean, Arabic, and Cyrillic.
  - Layout-independent shortcuts synchronize reliably across all international keyboard layouts.
- **Extensible Scripting Engine**:
  - Run automation tools and interactive UI panels written in either **Lua** or **JavaScript**:
    - Autorun scripts in `~/.config/minicut/autorun/` automatically create icon buttons on the **Dock**.
    - On-demand scripts in `~/.config/minicut/scripts/` appear directly in the **Scripts** menu.
- **Broad Format Compatibility**: MP4, MKV, MOV, WebM, AVI, OGG, OGV, OGM, MP3, WAV, AAC, FLAC, PNG, JPEG, WebP, and BMP.
- **Live Theme & Colorblind Accessibility**:
  - Instant theme switching (KDE Plasma, Breeze Dark, Breeze Light) and Protanopia, Deuteranopia, and Tritanopia colorblind modes without restarting.
  - Clean tabular Context Inspector aligning properties and keyframe curves.

---

## Basic Guidance: How to Use minicut

### 1. Installation & Launch
To install minicut for your user account (automatically checking and installing all required system dependencies):
```bash
./install.sh -y
```
*(Use `--deps-only` to check/install dependencies without compiling, or `--no-deps` to skip dependency resolution)*

Run `minicut` from your terminal or launch it from your desktop application launcher:
```bash
minicut
# Or from the project build directory:
./build/minicut
# Or open an existing project:
minicut my_project.minicut
```

### 2. Basic Editing Workflow

1. **Import Assets**:
   - Use **Project > Import Video / Audio / Image** or drag-and-drop media files directly into the window.
   - Assets appear in the **Clips & Assets** browser.
2. **Add Text & Subtitles**:
   - Click the **Add Text** button in the asset browser or **Project > Add Text**.
   - Customize font, size, colors, outlines, and alignment, then place directly onto the timeline.
3. **Assemble the Timeline**:
   - Drag or click to place clips onto video and audio tracks. Drag clips across compatible tracks.
   - Scrub the playhead or press **Space** to play and pause.
   - Press **S** to split clips at the playhead position.
   - Hold **Ctrl** or **Shift** to multi-select clips across tracks and move them together.
4. **Transform & Keyframing**:
   - Use the **Transform Tool (T)** on the preview canvas to visually drag, scale, and rotate visual clips.
   - Open the **Context Inspector** to fine-tune numerical transforms and keyframe Left/Right audio channels.
5. **Pop-Out Views & Dual-Monitor Setup**:
   - Click **Pop Out** on the preview toolbar to pop the video canvas into its own OS window. Press **F11** for fullscreen.
   - Open **File > Export Timeline Media (Ctrl+E)** to spawn the floating export pipeline window and continue editing simultaneously.
6. **Use Custom Scripts**:
   - Click any icon on the **Dock** below the menu bar to open an autorun tool panel.
   - Browse additional tools in the **Scripts** menu or add your own in `~/.config/minicut/scripts/`.
7. **Export Your Finished Media**:
   - Render to MP4 video, animated GIF, or animated WebP with optional `.srt` sidecars.

---

## Documentation & Help

Comprehensive localized API reference manuals for both **Lua** and **JavaScript** are bundled in the `docs/` folder and accessible directly within the application via **Help > Lua Script API** or **Help > JavaScript API**.
