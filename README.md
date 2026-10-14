# minicut

**minicut** is a fast, lightweight, and modern multi-track non-linear video editor built specifically for Linux desktop environments. It delivers a streamlined, responsive workflow for video, audio, image, and text editing without unnecessary bloat or complex subscription layers.

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

- **Multi-Track Timeline**: Unlimited video and audio tracks with interactive track header reordering, renaming, and Z-depth management.
- **Live Preview & Direct Transform**: Real-time canvas preview supporting spatial translation, rotation, scaling, and opacity changes.
- **FreeType Text Generator**: Dedicated typography generator supporting live composition preview, outline strokes, letter/line spacing, and multi-lingual scripts (Latin, Cyrillic, Japanese, Korean, and right-to-left Arabic).
- **Keyframe Animation**: Animate opacity, position coordinates, scale factors, and audio volume over time with smooth linear interpolation.
- **Extensible Scripting Engine**: Run automation tools and interactive UI panels written in either **Lua** or **JavaScript**:
  - Autorun scripts in `~/.config/minicut/autorun/` automatically create icon buttons on the **Dock**.
  - On-demand scripts in `~/.config/minicut/scripts/` appear directly in the **Scripts** menu.
- **Broad Format Compatibility**: MP4, MKV, MOV, WebM, AVI, OGG, OGV, OGM, MP3, WAV, AAC, FLAC, PNG, JPEG, WebP, and BMP.
- **Native Multi-Language Support**: English, 日本語 (Japanese), 한국어 (Korean), Українська (Ukrainian), and العربية (Arabic).

---

## Basic Guidance: How to Use minicut

### 1. Installation & Launch
To install minicut for your user account:
```bash
./install.sh --user
```
Run `minicut` from your terminal or launch it from your desktop application launcher:
```bash
minicut
# Or open an existing project:
minicut my_project.minicut
```

### 2. Basic Editing Workflow

1. **Import Assets**:
   - Use **Project > Import Video / Audio / Image** or drag-and-drop media files directly into the window.
   - Assets appear in the **Clips & Assets** browser.
2. **Add Text Overlays**:
   - Click the **Add Text** button in the asset browser or **Project > Add Text**.
   - Customize font, size, colors, outlines, and alignment.
   - Click **Add to Timeline** to place your text graphic at the playhead.
3. **Assemble the Timeline**:
   - Drag or click to place clips onto video and audio tracks.
   - Scrub the playhead or press **Space** to play and pause.
   - Press **S** to split clips at the playhead position.
   - Hold **Ctrl** or **Shift** to multi-select clips across tracks and move them together.
4. **Transform & Keyframing**:
   - Select any clip on the timeline to inspect its properties in the **Context Inspector**.
   - Add keyframes to animate opacity fades, pan/zoom motion, or audio volume.
5. **Use Custom Scripts**:
   - Click any icon on the **Dock** below the menu bar to open an autorun tool panel.
   - Browse additional tools in the **Scripts** menu or add your own in `~/.config/minicut/scripts/`.
6. **Export Your Video**:
   - Choose **File > Export Video (.mp4)...** to render your finished project.

---

## Documentation & Help

Comprehensive localized API reference manuals for both **Lua** and **JavaScript** are bundled in the `docs/` folder and accessible directly within the application via **Help > Lua Script API** or **Help > JavaScript API**.
