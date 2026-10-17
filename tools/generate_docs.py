#!/usr/bin/env python3
"""
minicut Documentation Generator
Generates comprehensive API reference manuals for all supported languages:
  - English (en)
  - Japanese (ja)
  - Korean (ko)
  - Ukrainian (uk)
  - Arabic (ar)
For both LuaJIT and JavaScript engines as HTML, Markdown, and PDF.

Outputs:
  - minicut_lua_api.pdf (en)
  - minicut_lua_api_ja.pdf (ja)
  - minicut_lua_api_ko.pdf (ko)
  - minicut_lua_api_uk.pdf (uk)
  - minicut_lua_api_ar.pdf (ar)
  - minicut_js_api.pdf (en)
  - minicut_js_api_ja.pdf (ja)
  - minicut_js_api_ko.pdf (ko)
  - minicut_js_api_uk.pdf (uk)
  - minicut_js_api_ar.pdf (ar)
"""

import os
import shutil
import subprocess
import sys

CSS_TEMPLATE = """
@page {
    size: letter portrait;
    margin: 0.8in;
    @bottom-right {
        content: counter(page);
    }
}
body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Noto Sans CJK JP", "Noto Sans CJK KR", "NanumGothic", "Noto Sans Arabic", "Noto Naskh Arabic", "Droid Sans Fallback", "DejaVu Sans", Arial, sans-serif;
    color: #232629;
    background: #ffffff;
    line-height: 1.55;
    font-size: 10pt;
    margin: 0;
    padding: 0;
}
.cover {
    text-align: center;
    padding: 40px 0 25px 0;
    border-bottom: 3px solid #1a73e8;
    margin-bottom: 25px;
}
.cover h1 {
    font-size: 24pt;
    color: #1a73e8;
    margin: 0 0 10px 0;
    font-weight: 700;
}
.cover .subtitle {
    font-size: 13pt;
    color: #5f6368;
    margin: 0 0 18px 0;
}
.cover .badge {
    display: inline-block;
    background: #e8f0fe;
    color: #1a73e8;
    font-weight: bold;
    padding: 5px 12px;
    border-radius: 14px;
    font-size: 9pt;
    margin: 4px;
}
h2 {
    color: #0d47a1;
    font-size: 13.5pt;
    border-bottom: 2px solid #e0e0e0;
    padding-bottom: 5px;
    margin-top: 28px;
    page-break-after: avoid;
}
h3 {
    color: #1565c0;
    font-size: 11pt;
    margin-top: 18px;
    margin-bottom: 6px;
    page-break-after: avoid;
}
p {
    margin: 8px 0;
}
ul, ol {
    margin: 6px 0 12px 24px;
    padding: 0;
}
li {
    margin-bottom: 4px;
}
code {
    background-color: #f1f3f4;
    color: #c5221f;
    padding: 2px 5px;
    border-radius: 3px;
    font-family: "DejaVu Sans Mono", Consolas, "Liberation Mono", Courier, monospace;
    font-size: 9pt;
}
pre {
    background-color: #f8f9fa;
    border: 1px solid #dadce0;
    border-left: 4px solid #1a73e8;
    border-radius: 4px;
    padding: 10px 14px;
    font-family: "DejaVu Sans Mono", Consolas, "Liberation Mono", Courier, monospace;
    font-size: 8.5pt;
    line-height: 1.45;
    overflow-x: auto;
    page-break-inside: avoid;
    margin: 10px 0;
}
table {
    width: 100%;
    border-collapse: collapse;
    margin: 12px 0;
    font-size: 8.8pt;
    page-break-inside: avoid;
}
th {
    background-color: #1a73e8;
    color: #ffffff;
    font-weight: 600;
    text-align: left;
    padding: 7px 9px;
    border: 1px solid #1a73e8;
}
td {
    padding: 6px 9px;
    border: 1px solid #dadce0;
    vertical-align: top;
}
tr:nth-child(even) {
    background-color: #f8f9fa;
}
.rtl {
    direction: rtl;
    text-align: right;
}
.rtl th {
    text-align: right;
}
.rtl pre, .rtl code {
    direction: ltr;
    text-align: left;
    display: inline-block;
}
.rtl pre {
    display: block;
}
.footer {
    text-align: center;
    color: #70757a;
    font-size: 8.5pt;
    margin-top: 35px;
    border-top: 1px solid #e0e0e0;
    padding-top: 12px;
}
"""

def generate_lua_en_html():
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>minicut Lua Scripting & Plugin API Reference Manual</title>
<style>{CSS_TEMPLATE}</style>
</head>
<body>
<div class="cover">
    <h1>minicut Video Editor</h1>
    <div class="subtitle">Embedded LuaJIT & Plugin API Reference Manual</div>
    <div>
        <span class="badge">LuaJIT 5.1 Runtime</span>
        <span class="badge">Dockable Tool Panels</span>
        <span class="badge">Tool Shelf Dock</span>
        <span class="badge">Network Library (libcurl)</span>
        <span class="badge">FreeType Text Generation</span>
    </div>
</div>

<h2>1. Introduction & Architecture</h2>
<p>
    <strong>minicut</strong> features a fully embedded, high-performance <strong>LuaJIT</strong> scripting engine.
    minicut enables complete automation, timeline editing, clip keyframing, and custom interactive dockable tool panels.
</p>

<h3>Script Directory Locations</h3>
<p>Scripts and icons are discovered in the user configuration directory:</p>
<table>
<thead>
    <tr><th style="width: 32%;">Location</th><th style="width: 28%;">Trigger / Role</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr>
        <td><code>~/.config/minicut/autorun/</code></td>
        <td>Startup / Auto-register</td>
        <td>Scripts execute on startup and generate an icon button on the horizontal Tool Shelf Dock below the main menu bar.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/scripts/</code></td>
        <td>Menu Click / On Demand</td>
        <td>Scripts appear directly in the top-level <strong>Modifiers</strong> menu on the main menu bar. Clicking runs the script and opens its tool panel.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/shaders/gl/</code></td>
        <td>Custom GL Shaders</td>
        <td>User OpenGL vertex (<code>.vert</code>) and fragment (<code>.frag</code>) shaders loaded by name via <code>shader.create("name")</code>.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/shaders/vulkan/</code></td>
        <td>Custom Vulkan Shaders</td>
        <td>User Vulkan GLSL vertex and fragment shaders compiled for the Vulkan rendering backend.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/icons/</code></td>
        <td>Dock Icon Asset</td>
        <td>PNG icon images matching script stems (e.g., <code>keyframe_animator.png</code> for <code>keyframe_animator.lua</code>).</td>
    </tr>
</tbody>
</table>

<h2>2. Plugin Script Architecture</h2>
<p>
    Every minicut script defines timeline and clip manipulation callbacks, followed by a <code>plugin:init_panel()</code> method that registers Dear ImGui UI controls.
</p>

<pre>-- ============================================================================
-- Example Base Script: my_tool.lua
-- ============================================================================

local target_track = 0
local clip_scale = 1.25

function execute_placement()
    minicut.push_undo()
    local path = "/path/to/media.mp4"
    local clip_id = timeline.place_clip(path, target_track, timeline.get_playhead(), 5.0)
    if clip_id and clip_id ~= "" then
        clip.set_scale(clip_id, clip_scale)
        timeline.add_keyframe(clip_id, "opacity", 0.0, 0.0)
        timeline.add_keyframe(clip_id, "opacity", 1.0, 1.0)
        minicut.set_status("Placed media clip on track " .. target_track)
    end
end

function plugin:init_panel()
    gui.text("Media Placement Tool")
    gui.separator()
    target_track = gui.slider_int("Track Lane", target_track, 0, 4)
    clip_scale = gui.slider_float("Initial Scale", clip_scale, 0.1, 5.0)
    
    gui.button("Place Clip At Playhead", execute_placement)
end</pre>

<h2>3. Core Lua Modules</h2>

<h3>3.1. Project & File Automation (<code>project</code> / Global)</h3>
<table>
<thead>
    <tr><th style="width: 35%;">Function</th><th style="width: 25%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr><td><code>project_save([path])</code><br><code>project.save([path])</code></td><td><code>boolean, string</code></td><td>Saves current project state to a <code>.minicut</code> JSON file.</td></tr>
    <tr><td><code>project_load(path)</code><br><code>project.load(path)</code></td><td><code>boolean, string</code></td><td>Loads a project from <code>path</code>, refreshing timeline and media.</td></tr>
    <tr><td><code>project.get_name()</code></td><td><code>string</code></td><td>Returns the current project name.</td></tr>
    <tr><td><code>project.set_name(name)</code></td><td><code>void</code></td><td>Sets the current project name.</td></tr>
</tbody>
</table>

<h3>3.2. Network & Internet Library (<code>net</code>)</h3>
<table>
<thead>
    <tr><th style="width: 35%;">Function</th><th style="width: 25%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr><td><code>net.get(url)</code></td><td><code>table</code></td><td>HTTP GET request. Returns <code>{{status, body, ok, headers}}</code>.</td></tr>
    <tr><td><code>net.post(url, body)</code></td><td><code>table</code></td><td>HTTP POST request with string or JSON payload.</td></tr>
    <tr><td><code>net.download(url, dest)</code></td><td><code>boolean, string</code></td><td>Downloads remote asset directly to local disk (supports HTTP, HTTPS, FTP, SFTP).</td></tr>
    <tr><td><code>net.upload(url, path)</code></td><td><code>table, string</code></td><td>Uploads local file binary stream (supports HTTP multipart, FTP, and secure SFTP).</td></tr>
</tbody>
</table>

<h3>3.3. Timeline & Track Management (<code>timeline</code>)</h3>
<table>
<thead>
    <tr><th style="width: 38%;">Function</th><th style="width: 22%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr><td><code>timeline.get_playhead()</code></td><td><code>number</code></td><td>Current playhead position in seconds.</td></tr>
    <tr><td><code>timeline.set_playhead(t)</code></td><td><code>void</code></td><td>Moves timeline playhead to <code>t</code> seconds.</td></tr>
    <tr><td><code>timeline.get_duration()</code></td><td><code>number</code></td><td>Total duration of the project timeline.</td></tr>
    <tr><td><code>timeline.get_track_count()</code></td><td><code>integer</code></td><td>Total number of video and audio tracks.</td></tr>
    <tr><td><code>timeline.place_clip(path, track, t, dur)</code></td><td><code>string</code></td><td>Places video, audio, or image clip on track.</td></tr>
    <tr><td><code>timeline.place_image(path, track, t, dur)</code></td><td><code>string</code></td><td>Places image clip on track and returns clip ID.</td></tr>
    <tr><td><code>timeline.add_keyframe(id, prop, t, val)</code></td><td><code>void</code></td><td>Adds animation keyframe (opacity, volume, pos_x, etc.).</td></tr>
    <tr><td><code>timeline.clear_keyframes(id, prop)</code></td><td><code>void</code></td><td>Clears all keyframes for a property on a clip.</td></tr>
    <tr><td><code>timeline.select_clips(ids)</code></td><td><code>void</code></td><td>Selects multiple clips simultaneously.</td></tr>
    <tr><td><code>timeline.get_selected_clips()</code></td><td><code>table</code></td><td>Returns array of selected clip IDs.</td></tr>
    <tr><td><code>timeline.move_track_up(idx)</code></td><td><code>void</code></td><td>Moves track up in stack order.</td></tr>
    <tr><td><code>timeline.move_track_down(idx)</code></td><td><code>void</code></td><td>Moves track down in stack order.</td></tr>
    <tr><td><code>timeline.set_track_name(idx, name)</code></td><td><code>void</code></td><td>Renames track at index.</td></tr>
    <tr><td><code>timeline.get_track_name(idx)</code></td><td><code>string</code></td><td>Gets track name at index.</td></tr>
</tbody>
</table>

<h3>3.4. Clip Transforms & Attributes (<code>clip</code>)</h3>
<table>
<thead>
    <tr><th style="width: 38%;">Function</th><th style="width: 22%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr><td><code>clip.get_position(clip_id)</code></td><td><code>number, number</code></td><td>Gets (x, y) coordinates relative to canvas center.</td></tr>
    <tr><td><code>clip.set_position(clip_id, x, y)</code></td><td><code>void</code></td><td>Sets center position coordinates.</td></tr>
    <tr><td><code>clip.get_scale(clip_id)</code></td><td><code>number</code></td><td>Gets uniform scale factor (1.0 = 100%).</td></tr>
    <tr><td><code>clip.set_scale(clip_id, s)</code></td><td><code>void</code></td><td>Sets scale factor.</td></tr>
    <tr><td><code>clip.get_opacity(clip_id)</code></td><td><code>number</code></td><td>Gets opacity [0.0, 1.0].</td></tr>
    <tr><td><code>clip.set_opacity(clip_id, o)</code></td><td><code>void</code></td><td>Sets opacity.</td></tr>
    <tr><td><code>clip.get_volume(clip_id)</code></td><td><code>number</code></td><td>Gets audio volume gain multiplier [0.0, 2.0].</td></tr>
    <tr><td><code>clip.set_volume(clip_id, v)</code></td><td><code>void</code></td><td>Sets audio volume gain multiplier.</td></tr>
    <tr><td><code>clip.get_time(clip_id)</code></td><td><code>number, number, number</code></td><td>Gets (start_time, duration, offset).</td></tr>
    <tr><td><code>clip.get_transform(clip_id)</code></td><td><code>table</code></td><td>Gets transform table <code>{{x, y, scale, rotation}}</code>.</td></tr>
    <tr><td><code>clip.set_transform(clip_id, x, y, s, r)</code></td><td><code>void</code></td><td>Sets translation X, Y, scale, and rotation simultaneously.</td></tr>
    <tr><td><code>clip.reset_transform(clip_id)</code></td><td><code>void</code></td><td>Resets transform to origin (x=0, y=0, scale=1.0, rotation=0).</td></tr>
    <tr><td><code>clip.get_frame(clip_id, [t, w, h])</code></td><td><code>table</code></td><td>Extracts raw RGBA frame object. Pass <code>"active"</code> for selected clip. Supports fluent chaining (<code>:apply_shader():reinsert()</code>).</td></tr>
    <tr><td><code>clip.set_frame(clip_id, frame, [t])</code></td><td><code>boolean</code></td><td>Reinserts modified RGBA pixel frame into clip media. Triggers full-clip rendering if shader attached.</td></tr>
</tbody>
</table>

<h3>3.5. GUI Controls (<code>gui</code>)</h3>
<table>
<thead>
    <tr><th style="width: 38%;">Function</th><th style="width: 22%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr><td><code>gui.text(text)</code></td><td><code>void</code></td><td>Renders text label.</td></tr>
    <tr><td><code>gui.text_colored(r, g, b, a, text)</code></td><td><code>void</code></td><td>Renders colored RGBA text.</td></tr>
    <tr><td><code>gui.button(label, [callback])</code></td><td><code>boolean</code></td><td>Renders push button and executes callback on click.</td></tr>
    <tr><td><code>gui.slider_float(label, val, min, max)</code></td><td><code>number</code></td><td>Renders float slider widget.</td></tr>
    <tr><td><code>gui.slider_int(label, val, min, max)</code></td><td><code>integer</code></td><td>Renders integer slider widget.</td></tr>
    <tr><td><code>gui.drag_float(label, val, [speed, min, max])</code></td><td><code>number</code></td><td>Renders drag float box.</td></tr>
    <tr><td><code>gui.input_text(label, text)</code></td><td><code>string</code></td><td>Renders single-line text input field.</td></tr>
    <tr><td><code>gui.checkbox(label, state)</code></td><td><code>boolean</code></td><td>Renders toggle checkbox.</td></tr>
    <tr><td><code>gui.same_line()</code></td><td><code>void</code></td><td>Aligns next widget on the same horizontal line.</td></tr>
    <tr><td><code>gui.separator()</code></td><td><code>void</code></td><td>Draws horizontal separator line.</td></tr>
    <tr><td><code>gui.file_dialog(title, cb, [filter])</code></td><td><code>void</code></td><td>Opens file dialog matching user settings (Native/Built-in).</td></tr>
    <tr><td><code>gui.native_file_dialog(title, cb, [filter])</code></td><td><code>void</code></td><td>Forces native Zenity/KDialog system file dialog.</td></tr>
    <tr><td><code>gui.popout_preview(popped)</code></td><td><code>void</code></td><td>Detaches (true) or docks (false) the video preview panel into a floating window.</td></tr>
    <tr><td><code>gui.is_preview_popped_out()</code></td><td><code>boolean</code></td><td>Returns whether preview is currently detached/floating.</td></tr>
    <tr><td><code>gui.set_preview_aspect_mode(mode)</code></td><td><code>void</code></td><td>Sets aspect mode: 0 (Auto), 1 (16:9), 2 (9:16), 3 (4:3), 4 (1:1), 5 (21:9), 6 (4:5).</td></tr>
    <tr><td><code>gui.get_preview_aspect_mode()</code></td><td><code>integer</code></td><td>Returns current preview aspect mode index (0-6).</td></tr>
    <tr><td><code>gui.set_transform_tool(tool)</code></td><td><code>void</code></td><td>Sets active preview gizmo tool ("select", "translate", "scale", "rotate").</td></tr>
    <tr><td><code>gui.get_transform_tool()</code></td><td><code>string</code></td><td>Returns active preview gizmo tool name.</td></tr>
</tbody>
</table>

<h3>3.6. Engine Utilities (<code>minicut</code>)</h3>
<table>
<thead>
    <tr><th style="width: 35%;">Function</th><th style="width: 25%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr><td><code>minicut.push_undo()</code></td><td><code>void</code></td><td>Records snapshot for Undo history.</td></tr>
    <tr><td><code>minicut.set_status(msg, [dur])</code></td><td><code>void</code></td><td>Displays temporary toast status message.</td></tr>
    <tr><td><code>minicut.log(msg)</code></td><td><code>void</code></td><td>Outputs debug log message to console.</td></tr>
</tbody>
</table>

<h3>3.7. Export Automation & Progress (<code>export</code>)</h3>
<table>
<thead>
    <tr><th style="width: 38%;">Function</th><th style="width: 22%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr><td><code>export.get_audio_progress()</code></td><td><code>number</code></td><td>Audio mixing progress (stereo mixing & keyframes, 0.0 to 1.0).</td></tr>
    <tr><td><code>export.get_progress()</code></td><td><code>number</code></td><td>Overall export pipeline progress (0.0 to 1.0).</td></tr>
    <tr><td><code>export.is_exporting()</code></td><td><code>boolean</code></td><td>True if background render/encode is actively running.</td></tr>
    <tr><td><code>export.get_status()</code></td><td><code>string</code></td><td>Human-readable pipeline status description.</td></tr>
</tbody>
</table>

<h3>3.8. Custom GLSL Shader Pipeline (<code>shader</code>)</h3>
<p>
    Loads custom shaders from <code>~/.config/minicut/shaders/gl/</code> and <code>~/.config/minicut/shaders/vulkan/</code> by name (e.g. <code>"ripple"</code>, <code>"grayscale"</code>, <code>"invert"</code>, <code>"vignette"</code>), or compiles custom GLSL fragment source strings.
    When applied to a clip, the shader processes the <strong>entire clip frame-by-frame</strong> in the background using FFmpeg without blocking UI responsiveness.
</p>
<table>
<thead>
    <tr><th style="width: 38%;">Function</th><th style="width: 22%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr><td><code>shader.create(name_or_src)</code></td><td><code>integer</code></td><td>Loads shader by name from shaders directory or compiles in-memory GLSL source.</td></tr>
    <tr><td><code>shader.set_uniform(id, name, val)</code></td><td><code>void</code></td><td>Sets float, int, or table vector uniform (matches with or without <code>u_</code> prefix).</td></tr>
    <tr><td><code>shader.get_uniform(id, name)</code></td><td><code>number / table</code></td><td>Returns current uniform parameter value.</td></tr>
    <tr><td><code>shader.apply(id, target, [uniforms])</code></td><td><code>boolean / table</code></td><td>Renders shader across all video frames of clip (or transforms single frame buffer).</td></tr>
</tbody>
</table>

<h4>Frame Buffer Fluent Piping</h4>
<pre>-- Fluent method chaining across entire clip
clip.get_frame("active"):apply_shader("ripple", {{ amplitude = 12.0, frequency = 0.08 }}):reinsert()

-- Or synthesize a brand-new clip on track 0
clip.get_frame("active"):apply_shader("ripple"):create_clip(0, 0.0, 5.0, "Ripple Wave")</pre>

<div class="footer">
    minicut v1.5.0 • Lightweight Linux Multi-track Video Editor • Generated Documentation
</div>
</body>
</html>"""

def generate_lua_ja_html():
    return f"""<!DOCTYPE html>
<html lang="ja">
<head>
<meta charset="UTF-8">
<title>minicut Lua スクリプト & プラグイン API リファレンスマニュアル</title>
<style>{CSS_TEMPLATE}</style>
</head>
<body>
<div class="cover">
    <h1>minicut 動画編集エディタ</h1>
    <div class="subtitle">組み込み LuaJIT & プラグイン API リファレンスマニュアル</div>
    <div>
        <span class="badge">LuaJIT 5.1 ランタイム</span>
        <span class="badge">ドッキング可能ツールパネル</span>
        <span class="badge">ツールシェルフドック</span>
        <span class="badge">ネットワークライブラリ (libcurl)</span>
        <span class="badge">FreeType テキスト生成</span>
    </div>
</div>

<h2>1. はじめに & アーキテクチャ</h2>
<p>
    <strong>minicut</strong> は高速な <strong>LuaJIT</strong> スクリプトエンジンを内蔵しています。
    タイムライン編集、クリップ配置、キーフレームアニメーション、自由なUIツールパネルの構築を自動化できます。
</p>

<h3>スクリプト配置ディレクトリ</h3>
<table>
<thead>
    <tr><th style="width: 32%;">配置場所</th><th style="width: 28%;">起動契機 / 役割</th><th style="width: 40%;">説明</th></tr>
</thead>
<tbody>
    <tr>
        <td><code>~/.config/minicut/autorun/</code></td>
        <td>起動時自動実行</td>
        <td>起動時に自動読み込みされ、メニューバー下の「ドック」にアイコンが登録されます。</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/scripts/</code></td>
        <td>メニュークリック時</td>
        <td>メニューバーの「モディファイア (Modifiers)」メニューに表示され、クリック時に実行されます。</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/shaders/gl/</code></td>
        <td>カスタムGLシェーダー</td>
        <td>名前で読み込まれるOpenGL頂点 (<code>.vert</code>) およびフラグメント (<code>.frag</code>) シェーダー。</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/shaders/vulkan/</code></td>
        <td>カスタムVulkanシェーダー</td>
        <td>Vulkanバックエンド用GLSLシェーダー。</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/icons/</code></td>
        <td>ドックアイコン画像</td>
        <td>スクリプト名と一致するPNGアイコン画像（例: <code>keyframe_animator.png</code>）。</td>
    </tr>
</tbody>
</table>

<h2>2. プラグインスクリプト構成</h2>
<pre>-- ============================================================================
-- スクリプト例: my_tool.lua
-- ============================================================================

local target_track = 0
local clip_scale = 1.0

function execute_action()
    minicut.push_undo()
    local clip_id = timeline.place_clip("sample.png", target_track, timeline.get_playhead(), 4.0)
    if clip_id and clip_id ~= "" then
        clip.set_scale(clip_id, clip_scale)
        timeline.add_keyframe(clip_id, "opacity", 0.0, 0.0)
        timeline.add_keyframe(clip_id, "opacity", 1.0, 1.0)
        minicut.set_status("クリップを配置しました: " .. clip_id)
    end
end

function choose_file()
    gui.file_dialog("ファイルを選択", function(path)
        if path and path ~= "" then minicut.set_status("選択: " .. path) end
    end)
end

function plugin:init_panel()
    gui.text("メディア配置ツール")
    gui.separator()
    target_track = gui.slider_int("トラック番号", target_track, 0, 4)
    clip_scale = gui.slider_float("拡大率", clip_scale, 0.1, 4.0)
    gui.button("参照...", choose_file)
    gui.button("再生ヘッド位置に配置", execute_action)
end</pre>

<h2>3. コアモジュール API</h2>

<h3>3.1. プロジェクト操作 (<code>project</code> / グローバル)</h3>
<table>
<thead><tr><th style="width: 35%;">関数</th><th style="width: 25%;">戻り値</th><th style="width: 40%;">説明</th></tr></thead>
<tbody>
    <tr><td><code>project_save([path])</code></td><td><code>boolean, string</code></td><td>プロジェクトを <code>.minicut</code> ファイルに保存します。</td></tr>
    <tr><td><code>project_load(path)</code></td><td><code>boolean, string</code></td><td>指定パスからプロジェクトを読み込みます。</td></tr>
    <tr><td><code>project.get_name()</code></td><td><code>string</code></td><td>プロジェクト名を取得します。</td></tr>
    <tr><td><code>project.set_name(name)</code></td><td><code>void</code></td><td>プロジェクト名を設定します。</td></tr>
</tbody>
</table>

<h3>3.2. ネットワーク通信 (<code>net</code>)</h3>
<table>
<thead><tr><th style="width: 35%;">関数</th><th style="width: 25%;">戻り値</th><th style="width: 40%;">説明</th></tr></thead>
<tbody>
    <tr><td><code>net.get(url)</code></td><td><code>table</code></td><td>HTTP GET リクエストを実行します。</td></tr>
    <tr><td><code>net.post(url, body)</code></td><td><code>table</code></td><td>HTTP POST リクエストを送信します。</td></tr>
    <tr><td><code>net.download(url, dest)</code></td><td><code>boolean</code></td><td>リモートファイルをローカルへ直接ダウンロードします。</td></tr>
</tbody>
</table>

<h3>3.3. タイムライン & トラック (<code>timeline</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">関数</th><th style="width: 22%;">戻り値</th><th style="width: 40%;">説明</th></tr></thead>
<tbody>
    <tr><td><code>timeline.get_playhead()</code></td><td><code>number</code></td><td>再生ヘッドの現在時刻（秒）を取得します。</td></tr>
    <tr><td><code>timeline.set_playhead(t)</code></td><td><code>void</code></td><td>再生ヘッドを時刻 <code>t</code> 秒へ移動します。</td></tr>
    <tr><td><code>timeline.place_clip(path, trk, t, dur)</code></td><td><code>string</code></td><td>動画/音声/画像をトラックに配置します。</td></tr>
    <tr><td><code>timeline.add_keyframe(id, prop, t, val)</code></td><td><code>void</code></td><td>アニメーションキーフレームを追加します。</td></tr>
    <tr><td><code>timeline.clear_keyframes(id, prop)</code></td><td><code>void</code></td><td>特定プロパティの全キーフレームを消去します。</td></tr>
    <tr><td><code>timeline.move_track_up(idx)</code></td><td><code>void</code></td><td>トラックを上へ移動します。</td></tr>
    <tr><td><code>timeline.move_track_down(idx)</code></td><td><code>void</code></td><td>トラックを下へ移動します。</td></tr>
    <tr><td><code>timeline.set_track_name(idx, name)</code></td><td><code>void</code></td><td>トラック名を変更します。</td></tr>
</tbody>
</table>

<h3>3.4. クリップ変形 (<code>clip</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">関数</th><th style="width: 22%;">戻り値</th><th style="width: 40%;">説明</th></tr></thead>
<tbody>
    <tr><td><code>clip.get_position(id)</code></td><td><code>number, number</code></td><td>中心基準の (x, y) 座標を取得します。</td></tr>
    <tr><td><code>clip.set_position(id, x, y)</code></td><td><code>void</code></td><td>中心座標を設定します。</td></tr>
    <tr><td><code>clip.get_scale(id)</code></td><td><code>number</code></td><td>スケール倍率（1.0 = 100%）を取得します。</td></tr>
    <tr><td><code>clip.set_scale(id, s)</code></td><td><code>void</code></td><td>スケール倍率を設定します。</td></tr>
    <tr><td><code>clip.get_opacity(id)</code></td><td><code>number</code></td><td>不透明度 [0.0, 1.0] を取得します。</td></tr>
    <tr><td><code>clip.set_opacity(id, o)</code></td><td><code>void</code></td><td>不透明度を設定します。</td></tr>
    <tr><td><code>clip.get_volume(id)</code></td><td><code>number</code></td><td>音声ボリュームゲイン [0.0, 2.0] を取得します。</td></tr>
    <tr><td><code>clip.set_volume(id, v)</code></td><td><code>void</code></td><td>音声ボリュームゲインを設定します。</td></tr>
    <tr><td><code>clip.get_transform(clip_id)</code></td><td><code>table</code></td><td>変形情報テーブル <code>{{x, y, scale, rotation}}</code> を取得します。</td></tr>
    <tr><td><code>clip.set_transform(clip_id, x, y, s, r)</code></td><td><code>void</code></td><td>位置(X, Y)、拡大率、回転角を一括設定します。</td></tr>
    <tr><td><code>clip.reset_transform(clip_id)</code></td><td><code>void</code></td><td>変形を初期状態（x=0, y=0, scale=1.0, rotation=0）にリセットします。</td></tr>
</tbody>
</table>

<h3>3.5. GUI ウィジェット (<code>gui</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">関数</th><th style="width: 22%;">戻り値</th><th style="width: 40%;">説明</th></tr></thead>
<tbody>
    <tr><td><code>gui.text(text)</code></td><td><code>void</code></td><td>テキストを表示します。</td></tr>
    <tr><td><code>gui.text_colored(r, g, b, a, text)</code></td><td><code>void</code></td><td>色付きテキストを表示します。</td></tr>
    <tr><td><code>gui.button(label, [callback])</code></td><td><code>boolean</code></td><td>ボタンを表示しクリック時に実行します。</td></tr>
    <tr><td><code>gui.slider_float(label, val, min, max)</code></td><td><code>number</code></td><td>浮動小数点スライダーを表示します。</td></tr>
    <tr><td><code>gui.slider_int(label, val, min, max)</code></td><td><code>integer</code></td><td>整数スライダーを表示します。</td></tr>
    <tr><td><code>gui.input_text(label, text)</code></td><td><code>string</code></td><td>テキスト入力フィールドを表示します。</td></tr>
    <tr><td><code>gui.file_dialog(title, cb, [filter])</code></td><td><code>void</code></td><td>ファイルダイアログを開き結果を返します。</td></tr>
    <tr><td><code>gui.popout_preview(popped)</code></td><td><code>void</code></td><td>プレビュー画面を独立ウィンドウに分離（true）またはドッキング（false）します。</td></tr>
    <tr><td><code>gui.is_preview_popped_out()</code></td><td><code>boolean</code></td><td>プレビューが現在分離されているかを取得します。</td></tr>
    <tr><td><code>gui.set_preview_aspect_mode(mode)</code></td><td><code>void</code></td><td>アスペクト比モード（0:自動, 1:16:9, 2:9:16, 3:4:3, 4:1:1, 5:21:9, 6:4:5）を設定します。</td></tr>
    <tr><td><code>gui.get_preview_aspect_mode()</code></td><td><code>integer</code></td><td>現在のアスペクト比モード（0〜6）を取得します。</td></tr>
    <tr><td><code>gui.set_transform_tool(tool)</code></td><td><code>void</code></td><td>ギズモ変形ツール（"select", "translate", "scale", "rotate"）を設定します。</td></tr>
    <tr><td><code>gui.get_transform_tool()</code></td><td><code>string</code></td><td>現在の変形ツール名を取得します。</td></tr>
</tbody>
</table>

<h3>3.6. エクスポート操作 & 進捗状況 (<code>export</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">関数</th><th style="width: 22%;">戻り値</th><th style="width: 40%;">説明</th></tr></thead>
<tbody>
    <tr><td><code>export.get_audio_progress()</code></td><td><code>number</code></td><td>音声ミキシングの進捗率（ステレオ・キーフレーム処理、0.0〜1.0）を取得します。</td></tr>
    <tr><td><code>export.get_progress()</code></td><td><code>number</code></td><td>全体のエクスポート進捗率（0.0〜1.0）を取得します。</td></tr>
    <tr><td><code>export.is_exporting()</code></td><td><code>boolean</code></td><td>エクスポート処理が実行中かを取得します。</td></tr>
    <tr><td><code>export.get_status()</code></td><td><code>string</code></td><td>パイプラインの現在の状態文字列を取得します。</td></tr>
</tbody>
</table>

<div class="footer">
    minicut v1.5.0 • 軽量マルチトラック動画編集エディタ • ドキュメント自動生成
</div>
</body>
</html>"""

def generate_lua_ko_html():
    return f"""<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="UTF-8">
<title>minicut Lua 스크립팅 & 플러그인 API 참조 매뉴얼</title>
<style>{CSS_TEMPLATE}</style>
</head>
<body>
<div class="cover">
    <h1>minicut 비디오 편집기</h1>
    <div class="subtitle">임베디드 LuaJIT & 플러그인 API 참조 매뉴얼</div>
    <div>
        <span class="badge">LuaJIT 5.1 런타임</span>
        <span class="badge">도킹 가능 도구 패널</span>
        <span class="badge">도구 선반 도크</span>
        <span class="badge">네트워크 라이브러리 (libcurl)</span>
        <span class="badge">FreeType 텍스트 생성</span>
    </div>
</div>

<h2>1. 소개 및 아키텍처</h2>
<p>
    <strong>minicut</strong>은 고성능 <strong>LuaJIT</strong> 스크립팅 엔진을 내장하고 있습니다.
    타임라인 자동화, 클립 배치, 키프레임 애니메이션, 맞춤형 인터랙티브 도구 패널을 자유롭게 제작할 수 있습니다.
</p>

<h3>스크립트 디렉터리 경로</h3>
<table>
<thead>
    <tr><th style="width: 32%;">경로</th><th style="width: 28%;">실행 시점 / 역할</th><th style="width: 40%;">설명</th></tr>
</thead>
<tbody>
    <tr>
        <td><code>~/.config/minicut/autorun/</code></td>
        <td>시작 시 자동 실행</td>
        <td>앱 실행 시 자동으로 로드되며 메뉴 표시줄 아래 '도크'에 아이콘 버튼이 생성됩니다.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/scripts/</code></td>
        <td>메뉴 클릭 시</td>
        <td>메뉴 표시줄의 <strong>수정자 (Modifiers)</strong> 메뉴에 표시되며 클릭 시 실행됩니다.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/shaders/gl/</code></td>
        <td>커스텀 GL 셰이더</td>
        <td>이름으로 로드되는 OpenGL 버텍스(<code>.vert</code>) 및 프래그먼트(<code>.frag</code>) 셰이더.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/shaders/vulkan/</code></td>
        <td>커스텀 Vulkan 셰이더</td>
        <td>Vulkan 렌더링 백엔드용 GLSL 셰이더.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/icons/</code></td>
        <td>도크 아이콘 파일</td>
        <td>스크립트 파일명과 일치하는 PNG 아이콘 이미지입니다.</td>
    </tr>
</tbody>
</table>

<h2>2. 플러그인 스크립트 구조</h2>
<pre>-- ============================================================================
-- 스크립트 예제: my_tool.lua
-- ============================================================================

local target_track = 0
local clip_scale = 1.0

function execute_action()
    minicut.push_undo()
    local clip_id = timeline.place_clip("tests/assets/test_sample.png", target_track, timeline.get_playhead(), 4.0)
    if clip_id and clip_id ~= "" then
        clip.set_scale(clip_id, clip_scale)
        timeline.add_keyframe(clip_id, "opacity", 0.0, 0.0)
        timeline.add_keyframe(clip_id, "opacity", 1.0, 1.0)
        minicut.set_status("클립 배치 완료: " .. clip_id)
    end
end

function choose_file()
    gui.file_dialog("파일 선택", function(selected)
        if selected and selected ~= "" then
            minicut.set_status("선택된 파일: " .. selected)
        end
    end)
end

function plugin:init_panel()
    gui.text("미디어 배치 도구")
    gui.separator()
    target_track = gui.slider_int("트랙 번호", target_track, 0, 4)
    clip_scale = gui.slider_float("크기 비율", clip_scale, 0.1, 4.0)
    gui.button("파일 찾아보기...", choose_file)
    gui.button("재생헤드 위치에 배치", execute_action)
end</pre>

<h2>3. 코어 모듈 API</h2>

<h3>3.1. 프로젝트 제어 (<code>project</code> / 전역)</h3>
<table>
<thead><tr><th style="width: 35%;">함수</th><th style="width: 25%;">반환값</th><th style="width: 40%;">설명</th></tr></thead>
<tbody>
    <tr><td><code>project_save([path])</code></td><td><code>boolean, string</code></td><td>프로젝트를 <code>.minicut</code> 파일로 저장합니다.</td></tr>
    <tr><td><code>project_load(path)</code></td><td><code>boolean, string</code></td><td>지정된 경로의 프로젝트를 로드합니다.</td></tr>
    <tr><td><code>project.get_name()</code></td><td><code>string</code></td><td>현재 프로젝트 이름을 가져옵니다.</td></tr>
    <tr><td><code>project.set_name(name)</code></td><td><code>void</code></td><td>프로젝트 이름을 설정합니다.</td></tr>
</tbody>
</table>

<h3>3.2. 네트워크 라이브러리 (<code>net</code>)</h3>
<table>
<thead><tr><th style="width: 35%;">함수</th><th style="width: 25%;">반환값</th><th style="width: 40%;">설명</th></tr></thead>
<tbody>
    <tr><td><code>net.get(url)</code></td><td><code>table</code></td><td>HTTP GET 요청을 수행합니다.</td></tr>
    <tr><td><code>net.post(url, body)</code></td><td><code>table</code></td><td>HTTP POST 요청을 전송합니다.</td></tr>
    <tr><td><code>net.download(url, dest)</code></td><td><code>boolean</code></td><td>원격 파일을 로컬 디스크에 직접 다운로드합니다.</td></tr>
</tbody>
</table>

<h3>3.3. 타임라인 및 트랙 (<code>timeline</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">함수</th><th style="width: 22%;">반환값</th><th style="width: 40%;">설명</th></tr></thead>
<tbody>
    <tr><td><code>timeline.get_playhead()</code></td><td><code>number</code></td><td>현재 재생헤드 위치(초)를 반환합니다.</td></tr>
    <tr><td><code>timeline.set_playhead(t)</code></td><td><code>void</code></td><td>재생헤드를 <code>t</code>초 위치로 이동합니다.</td></tr>
    <tr><td><code>timeline.place_clip(path, trk, t, dur)</code></td><td><code>string</code></td><td>비디오/오디오/이미지를 트랙에 배치합니다.</td></tr>
    <tr><td><code>timeline.add_keyframe(id, prop, t, val)</code></td><td><code>void</code></td><td>애니메이션 키프레임을 추가합니다.</td></tr>
    <tr><td><code>timeline.clear_keyframes(id, prop)</code></td><td><code>void</code></td><td>속성의 모든 키프레임을 제거합니다.</td></tr>
    <tr><td><code>timeline.move_track_up(idx)</code></td><td><code>void</code></td><td>트랙을 위로 이동합니다.</td></tr>
    <tr><td><code>timeline.move_track_down(idx)</code></td><td><code>void</code></td><td>트랙을 아래로 이동합니다.</td></tr>
    <tr><td><code>timeline.set_track_name(idx, name)</code></td><td><code>void</code></td><td>트랙 이름을 변경합니다.</td></tr>
</tbody>
</table>

<h3>3.4. 클립 변형 (<code>clip</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">함수</th><th style="width: 22%;">반환값</th><th style="width: 40%;">설명</th></tr></thead>
<tbody>
    <tr><td><code>clip.get_position(id)</code></td><td><code>number, number</code></td><td>캔버스 중심 기준 (x, y) 좌표를 반환합니다.</td></tr>
    <tr><td><code>clip.set_position(id, x, y)</code></td><td><code>void</code></td><td>중심 좌표를 설정합니다.</td></tr>
    <tr><td><code>clip.get_scale(id)</code></td><td><code>number</code></td><td>크기 비율(1.0 = 100%)을 반환합니다.</td></tr>
    <tr><td><code>clip.set_scale(id, s)</code></td><td><code>void</code></td><td>크기 비율을 설정합니다.</td></tr>
    <tr><td><code>clip.get_opacity(id)</code></td><td><code>number</code></td><td>불투명도 [0.0, 1.0]를 반환합니다.</td></tr>
    <tr><td><code>clip.set_opacity(id, o)</code></td><td><code>void</code></td><td>불투명도를 설정합니다.</td></tr>
    <tr><td><code>clip.get_volume(id)</code></td><td><code>number</code></td><td>오디오 볼륨 게인 [0.0, 2.0]을 반환합니다.</td></tr>
    <tr><td><code>clip.set_volume(id, v)</code></td><td><code>void</code></td><td>오디오 볼륨 게인을 설정합니다.</td></tr>
    <tr><td><code>clip.get_transform(clip_id)</code></td><td><code>table</code></td><td>변형 속성 테이블 <code>{{x, y, scale, rotation}}</code>을 반환합니다.</td></tr>
    <tr><td><code>clip.set_transform(clip_id, x, y, s, r)</code></td><td><code>void</code></td><td>위치(X, Y), 크기 비율, 회전 각도를 일괄 설정합니다.</td></tr>
    <tr><td><code>clip.reset_transform(clip_id)</code></td><td><code>void</code></td><td>클립 변형을 기본값(x=0, y=0, scale=1.0, rotation=0)으로 초기화합니다.</td></tr>
</tbody>
</table>

<h3>3.5. GUI 위젯 (<code>gui</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">함수</th><th style="width: 22%;">반환값</th><th style="width: 40%;">설명</th></tr></thead>
<tbody>
    <tr><td><code>gui.text(text)</code></td><td><code>void</code></td><td>텍스트 라벨을 렌더링합니다.</td></tr>
    <tr><td><code>gui.text_colored(r, g, b, a, text)</code></td><td><code>void</code></td><td>색상 텍스트를 렌더링합니다.</td></tr>
    <tr><td><code>gui.button(label, [callback])</code></td><td><code>boolean</code></td><td>버튼을 렌더링하고 클릭 시 콜백을 실행합니다.</td></tr>
    <tr><td><code>gui.slider_float(label, val, min, max)</code></td><td><code>number</code></td><td>실수형 슬라이더를 렌더링합니다.</td></tr>
    <tr><td><code>gui.slider_int(label, val, min, max)</code></td><td><code>integer</code></td><td>정수형 슬라이더를 렌더링합니다.</td></tr>
    <tr><td><code>gui.input_text(label, text)</code></td><td><code>string</code></td><td>텍스트 입력 상자를 렌더링합니다.</td></tr>
    <tr><td><code>gui.file_dialog(title, cb, [filter])</code></td><td><code>void</code></td><td>파일 대화상자를 열고 결과를 콜백으로 전달합니다.</td></tr>
    <tr><td><code>gui.popout_preview(popped)</code></td><td><code>void</code></td><td>미리보기 창을 독립 플로팅 창으로 분리(true)하거나 도킹(false)합니다.</td></tr>
    <tr><td><code>gui.is_preview_popped_out()</code></td><td><code>boolean</code></td><td>미리보기가 분리된 플로팅 상태인지 여부를 반환합니다.</td></tr>
    <tr><td><code>gui.set_preview_aspect_mode(mode)</code></td><td><code>void</code></td><td>종횡비 모드(0:자동, 1:16:9, 2:9:16, 3:4:3, 4:1:1, 5:21:9, 6:4:5)를 설정합니다.</td></tr>
    <tr><td><code>gui.get_preview_aspect_mode()</code></td><td><code>integer</code></td><td>현재 종횡비 모드 번호(0~6)를 반환합니다.</td></tr>
    <tr><td><code>gui.set_transform_tool(tool)</code></td><td><code>void</code></td><td>기즈모 변형 도구("select", "translate", "scale", "rotate")를 설정합니다.</td></tr>
    <tr><td><code>gui.get_transform_tool()</code></td><td><code>string</code></td><td>현재 활성 변형 도구 이름을 반환합니다.</td></tr>
</tbody>
</table>

<h3>3.6. 내보내기 자동화 및 진행률 (<code>export</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">함수</th><th style="width: 22%;">반환값</th><th style="width: 40%;">설명</th></tr></thead>
<tbody>
    <tr><td><code>export.get_audio_progress()</code></td><td><code>number</code></td><td>오디오 믹싱 진행률(스테레오 및 키프레임, 0.0~1.0)을 반환합니다.</td></tr>
    <tr><td><code>export.get_progress()</code></td><td><code>number</code></td><td>전체 내보내기 파이프라인 진행률(0.0~1.0)을 반환합니다.</td></tr>
    <tr><td><code>export.is_exporting()</code></td><td><code>boolean</code></td><td>내보내기 작업이 진행 중인지 여부를 반환합니다.</td></tr>
    <tr><td><code>export.get_status()</code></td><td><code>string</code></td><td>내보내기 상태 설명을 반환합니다.</td></tr>
</tbody>
</table>

<div class="footer">
    minicut v1.5.0 • 가벼운 리눅스 멀티트랙 비디오 편집기 • 자동 생성 문서
</div>
</body>
</html>"""

def generate_lua_uk_html():
    return f"""<!DOCTYPE html>
<html lang="uk">
<head>
<meta charset="UTF-8">
<title>minicut Довідник з LuaJIT та Plugin API</title>
<style>{CSS_TEMPLATE}</style>
</head>
<body>
<div class="cover">
    <h1>Відеоредактор minicut</h1>
    <div class="subtitle">Вбудований рушій LuaJIT & Довідник з Plugin API</div>
    <div>
        <span class="badge">Середовище LuaJIT 5.1</span>
        <span class="badge">Панелі інструментів</span>
        <span class="badge">Док інструментів</span>
        <span class="badge">Мережева бібліотека (libcurl)</span>
        <span class="badge">Генерація тексту FreeType</span>
    </div>
</div>

<h2>1. Вступ та архітектура</h2>
<p>
    <strong>minicut</strong> має повністю вбудований, високопродуктивний рушій <strong>LuaJIT</strong>.
    Він забезпечує автоматизацію редагування таймлайну, розміщення медіа, анімацію ключових кадрів та створення власних панелей інструментів.
</p>

<h3>Каталоги розташування скриптів</h3>
<table>
<thead>
    <tr><th style="width: 32%;">Розташування</th><th style="width: 28%;">Запуск / Роль</th><th style="width: 40%;">Опис</th></tr>
</thead>
<tbody>
    <tr>
        <td><code>~/.config/minicut/autorun/</code></td>
        <td>Автозапуск при старті</td>
        <td>Скрипти завантажуються під час запуску та додають кнопку з іконкою на панель Доку під головним меню.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/scripts/</code></td>
        <td>Вибір у меню</td>
        <td>Скрипти з'являються безпосередньо в меню <strong>Модифікатори (Modifiers)</strong> головного меню і запускаються за запитом користувача.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/shaders/gl/</code></td>
        <td>Користувацькі GL шейдери</td>
        <td>Вершинні (<code>.vert</code>) та фрагментні (<code>.frag</code>) шейдери OpenGL, що завантажуються за іменем.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/shaders/vulkan/</code></td>
        <td>Користувацькі Vulkan шейдери</td>
        <td>GLSL шейдери для бекенду Vulkan.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/icons/</code></td>
        <td>Файли іконок Доку</td>
        <td>PNG іконки, що відповідають іменам скриптів (наприклад: <code>keyframe_animator.png</code>).</td>
    </tr>
</tbody>
</table>

<h2>2. Архітектура плагіна</h2>
<pre>-- ============================================================================
-- Приклад скрипта: my_tool.lua
-- ============================================================================

local target_track = 0
local clip_scale = 1.0

function execute_action()
    minicut.push_undo()
    local clip_id = timeline.place_clip("tests/assets/test_sample.png", target_track, timeline.get_playhead(), 4.0)
    if clip_id and clip_id ~= "" then
        clip.set_scale(clip_id, clip_scale)
        timeline.add_keyframe(clip_id, "opacity", 0.0, 0.0)
        timeline.add_keyframe(clip_id, "opacity", 1.0, 1.0)
        minicut.set_status("Кліп успішно розміщено: " .. clip_id)
    end
end

function choose_file()
    gui.file_dialog("Оберіть файл", function(selected)
        if selected and selected ~= "" then
            minicut.set_status("Обрано: " .. selected)
        end
    end)
end

function plugin:init_panel()
    gui.text("Інструмент розміщення медіа")
    gui.separator()
    target_track = gui.slider_int("Номер доріжки", target_track, 0, 4)
    clip_scale = gui.slider_float("Масштаб", clip_scale, 0.1, 4.0)
    gui.button("Огляд файлів...", choose_file)
    gui.button("Розмістити на курсорі", execute_action)
end</pre>

<h2>3. Опис основних модулів</h2>

<h3>3.1. Керування проєктом (<code>project</code> / Глобальні)</h3>
<table>
<thead><tr><th style="width: 35%;">Функція</th><th style="width: 25%;">Повертає</th><th style="width: 40%;">Опис</th></tr></thead>
<tbody>
    <tr><td><code>project_save([path])</code></td><td><code>boolean, string</code></td><td>Зберігає стан проєкту у файл <code>.minicut</code>.</td></tr>
    <tr><td><code>project_load(path)</code></td><td><code>boolean, string</code></td><td>Завантажує проєкт за вказаним шляхом.</td></tr>
    <tr><td><code>project.get_name()</code></td><td><code>string</code></td><td>Повертає поточну назву проєкту.</td></tr>
    <tr><td><code>project.set_name(name)</code></td><td><code>void</code></td><td>Встановлює назву проєкту.</td></tr>
</tbody>
</table>

<h3>3.2. Мережева бібліотека (<code>net</code>)</h3>
<table>
<thead><tr><th style="width: 35%;">Функція</th><th style="width: 25%;">Повертає</th><th style="width: 40%;">Опис</th></tr></thead>
<tbody>
    <tr><td><code>net.get(url)</code></td><td><code>table</code></td><td>Виконує запит HTTP GET.</td></tr>
    <tr><td><code>net.post(url, body)</code></td><td><code>table</code></td><td>Надсилає запит HTTP POST.</td></tr>
    <tr><td><code>net.download(url, dest)</code></td><td><code>boolean</code></td><td>Завантажує віддалений файл безпосередньо на диск.</td></tr>
</tbody>
</table>

<h3>3.3. Часова шкала та доріжки (<code>timeline</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">Функція</th><th style="width: 22%;">Повертає</th><th style="width: 40%;">Опис</th></tr></thead>
<tbody>
    <tr><td><code>timeline.get_playhead()</code></td><td><code>number</code></td><td>Повертає поточну позицію курсору в секундах.</td></tr>
    <tr><td><code>timeline.set_playhead(t)</code></td><td><code>void</code></td><td>Переміщує курсор у позицію <code>t</code> секунд.</td></tr>
    <tr><td><code>timeline.place_clip(path, trk, t, dur)</code></td><td><code>string</code></td><td>Розміщує відео, аудіо або зображення на доріжці.</td></tr>
    <tr><td><code>timeline.add_keyframe(id, prop, t, val)</code></td><td><code>void</code></td><td>Додає ключовий кадр анімації (opacity, volume тощо).</td></tr>
    <tr><td><code>timeline.clear_keyframes(id, prop)</code></td><td><code>void</code></td><td>Видаляє всі ключові кадри для властивості.</td></tr>
    <tr><td><code>timeline.move_track_up(idx)</code></td><td><code>void</code></td><td>Переміщує доріжку вгору в стеку.</td></tr>
    <tr><td><code>timeline.move_track_down(idx)</code></td><td><code>void</code></td><td>Переміщує доріжку вниз в стеку.</td></tr>
    <tr><td><code>timeline.set_track_name(idx, name)</code></td><td><code>void</code></td><td>Перейменовує доріжку за індексом.</td></tr>
</tbody>
</table>

<h3>3.4. Трансформація кліпів (<code>clip</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">Функція</th><th style="width: 22%;">Повертає</th><th style="width: 40%;">Опис</th></tr></thead>
<tbody>
    <tr><td><code>clip.get_position(id)</code></td><td><code>number, number</code></td><td>Отримує координати (x, y) відносно центру.</td></tr>
    <tr><td><code>clip.set_position(id, x, y)</code></td><td><code>void</code></td><td>Встановлює координати центру кліпу.</td></tr>
    <tr><td><code>clip.get_scale(id)</code></td><td><code>number</code></td><td>Отримує коефіцієнт масштабування (1.0 = 100%).</td></tr>
    <tr><td><code>clip.set_scale(id, s)</code></td><td><code>void</code></td><td>Встановлює коефіцієнт масштабування.</td></tr>
    <tr><td><code>clip.get_opacity(id)</code></td><td><code>number</code></td><td>Отримує рівень непрозорості [0.0, 1.0].</td></tr>
    <tr><td><code>clip.set_opacity(id, o)</code></td><td><code>void</code></td><td>Встановлює непрозорість кліпу.</td></tr>
    <tr><td><code>clip.get_volume(id)</code></td><td><code>number</code></td><td>Отримує підсилення гучності [0.0, 2.0].</td></tr>
    <tr><td><code>clip.set_volume(id, v)</code></td><td><code>void</code></td><td>Встановлює підсилення гучності.</td></tr>
    <tr><td><code>clip.get_transform(clip_id)</code></td><td><code>table</code></td><td>Повертає таблицю трансформацій <code>{{x, y, scale, rotation}}</code>.</td></tr>
    <tr><td><code>clip.set_transform(clip_id, x, y, s, r)</code></td><td><code>void</code></td><td>Одночасно встановлює зміщення X, Y, масштаб та обертання.</td></tr>
    <tr><td><code>clip.reset_transform(clip_id)</code></td><td><code>void</code></td><td>Скидає трансформацію до початкового стану (x=0, y=0, scale=1.0, rotation=0).</td></tr>
</tbody>
</table>

<h3>3.5. Графічні елементи керування (<code>gui</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">Функція</th><th style="width: 22%;">Повертає</th><th style="width: 40%;">Опис</th></tr></thead>
<tbody>
    <tr><td><code>gui.text(text)</code></td><td><code>void</code></td><td>Виводить текстовий напис.</td></tr>
    <tr><td><code>gui.text_colored(r, g, b, a, text)</code></td><td><code>void</code></td><td>Виводить кольоровий текст RGBA.</td></tr>
    <tr><td><code>gui.button(label, [callback])</code></td><td><code>boolean</code></td><td>Створює кнопку з можливістю виклику функції.</td></tr>
    <tr><td><code>gui.slider_float(label, val, min, max)</code></td><td><code>number</code></td><td>Створює повзунок для дійсних чисел.</td></tr>
    <tr><td><code>gui.slider_int(label, val, min, max)</code></td><td><code>integer</code></td><td>Створює повзунок для цілих чисел.</td></tr>
    <tr><td><code>gui.input_text(label, text)</code></td><td><code>string</code></td><td>Створює поле введення тексту.</td></tr>
    <tr><td><code>gui.file_dialog(title, cb, [filter])</code></td><td><code>void</code></td><td>Відкриває діалогове вікно вибору файлів.</td></tr>
    <tr><td><code>gui.popout_preview(popped)</code></td><td><code>void</code></td><td>Відокремлює (true) або прикріплює (false) вікно попереднього перегляду.</td></tr>
    <tr><td><code>gui.is_preview_popped_out()</code></td><td><code>boolean</code></td><td>Повертає, чи є вікно попереднього перегляду плаваючим.</td></tr>
    <tr><td><code>gui.set_preview_aspect_mode(mode)</code></td><td><code>void</code></td><td>Встановлює режим співвідношення сторін (0: Авто, 1: 16:9, 2: 9:16, 3: 4:3, 4: 1:1, 5: 21:9, 6: 4:5).</td></tr>
    <tr><td><code>gui.get_preview_aspect_mode()</code></td><td><code>integer</code></td><td>Повертає поточний індекс режиму співвідношення сторін (0–6).</td></tr>
    <tr><td><code>gui.set_transform_tool(tool)</code></td><td><code>void</code></td><td>Встановлює інструмент трансформації ("select", "translate", "scale", "rotate").</td></tr>
    <tr><td><code>gui.get_transform_tool()</code></td><td><code>string</code></td><td>Повертає назву активного інструмента трансформації.</td></tr>
</tbody>
</table>

<h3>3.6. Автоматизація експорту та прогрес (<code>export</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">Функція</th><th style="width: 22%;">Повертає</th><th style="width: 40%;">Опис</th></tr></thead>
<tbody>
    <tr><td><code>export.get_audio_progress()</code></td><td><code>number</code></td><td>Прогрес мікшування аудіо (стереозведення та ключові кадри, 0.0 до 1.0).</td></tr>
    <tr><td><code>export.get_progress()</code></td><td><code>number</code></td><td>Загальний прогрес конвеєра експорту (0.0 до 1.0).</td></tr>
    <tr><td><code>export.is_exporting()</code></td><td><code>boolean</code></td><td>Повертає true, якщо фоновий рендеринг/кодування активні.</td></tr>
    <tr><td><code>export.get_status()</code></td><td><code>string</code></td><td>Текстовий опис поточного стану експорту.</td></tr>
</tbody>
</table>

<div class="footer">
    minicut v1.5.0 • Легкий мультитрековий відеоредактор для Linux • Автоматична документація
</div>
</body>
</html>"""

def generate_lua_ar_html():
    return f"""<!DOCTYPE html>
<html lang="ar" dir="rtl">
<head>
<meta charset="UTF-8">
<title>minicut دليل مرجعي للبرمجة بلغة LuaJIT وواجهة Plugin API</title>
<style>{CSS_TEMPLATE}</style>
</head>
<body class="rtl">
<div class="cover">
    <h1>محرر الفيديو minicut</h1>
    <div class="subtitle">دليل مرجعي لبيئة البرمجة LuaJIT والإضافات البرمجية</div>
    <div>
        <span class="badge">محرك LuaJIT 5.1</span>
        <span class="badge">لوحات أدوات قابلة للتثبيت</span>
        <span class="badge">منصة الأدوات (Dock)</span>
        <span class="badge">مكتبة الشبكة (libcurl)</span>
        <span class="badge">توليد النصوص عبر FreeType</span>
    </div>
</div>

<h2>1. المقدمة والبنية البرمجية</h2>
<p>
    يحتوي برنامج <strong>minicut</strong> على محرك برمجي مدمج عالي الأداء بلغة <strong>LuaJIT</strong>.
    يتيح المحرك أتمتة عمليات المخطط الزمني بالكامل، إدراج الوسائط، تحريك الإطارات المفتاحية (Keyframing)، وبناء لوحات تحكم تفاعلية مخصصة.
</p>

<h3>مسارات مجلدات السكربتات</h3>
<table>
<thead>
    <tr><th style="width: 32%;">المسار</th><th style="width: 28%;">وقت التشغيل / الدور</th><th style="width: 40%;">الوصف</th></tr>
</thead>
<tbody>
    <tr>
        <td><code>~/.config/minicut/autorun/</code></td>
        <td>التشغيل التلقائي عند البدء</td>
        <td>يتم تحميل السكربتات تلقائياً عند بدء التشغيل وتنشئ زراً في منصة الأدوات (Dock) أسفل شريط القوائم.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/scripts/</code></td>
        <td>النقر في شريط القوائم</td>
        <td>تظهر السكربتات مباشرة في قائمة <strong>المعدِّلات (Modifiers)</strong> ويتم تشغيلها عند اختيارها من قبل المستخدم.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/shaders/gl/</code></td>
        <td>مظللات GL المخصصة</td>
        <td>مظللات الرؤوس (<code>.vert</code>) والبكسلات (<code>.frag</code>) لـ OpenGL المحملة بالاسم.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/shaders/vulkan/</code></td>
        <td>مظللات Vulkan المخصصة</td>
        <td>مظللات GLSL لمحرك التصيير Vulkan.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/icons/</code></td>
        <td>أيقونات منصة الأدوات</td>
        <td>ملفات أيقونات PNG مطابقة لاسم السكربت (مثال: <code>keyframe_animator.png</code>).</td>
    </tr>
</tbody>
</table>

<h2>2. بنية ملف السكربت البرمجي</h2>
<pre>-- ============================================================================
-- سكربت توضيحي: my_tool.lua
-- ============================================================================

local target_track = 0
local clip_scale = 1.0

function execute_action()
    minicut.push_undo()
    local clip_id = timeline.place_clip("sample.png", target_track, timeline.get_playhead(), 4.0)
    if clip_id and clip_id ~= "" then
        clip.set_scale(clip_id, clip_scale)
        timeline.add_keyframe(clip_id, "opacity", 0.0, 0.0)
        timeline.add_keyframe(clip_id, "opacity", 1.0, 1.0)
        minicut.set_status("تم إدراج المقطع: " .. clip_id)
    end
end

function choose_file()
    gui.file_dialog("اختر ملفاً", function(selected)
        if selected and selected ~= "" then
            minicut.set_status("تم اختيار: " .. selected)
        end
    end)
end

function plugin:init_panel()
    gui.text("أداة إدراج الوسائط")
    gui.separator()
    target_track = gui.slider_int("رقم المسار", target_track, 0, 4)
    clip_scale = gui.slider_float("المقياس", clip_scale, 0.1, 4.0)
    gui.button("تصفح الملفات...", choose_file)
    gui.button("إدراج عند المؤشر", execute_action)
end</pre>

<h2>3. مكتبات ووحدات النظام البرمجية</h2>

<h3>3.1. إدارة المشروع (<code>project</code> / عام)</h3>
<table>
<thead><tr><th style="width: 35%;">الدالة</th><th style="width: 25%;">القيمة المرجعة</th><th style="width: 40%;">الوصف</th></tr></thead>
<tbody>
    <tr><td><code>project_save([path])</code></td><td><code>boolean, string</code></td><td>حفظ حالة المشروع في ملف <code>.minicut</code>.</td></tr>
    <tr><td><code>project_load(path)</code></td><td><code>boolean, string</code></td><td>تحميل مشروع من المسار المحدد.</td></tr>
    <tr><td><code>project.get_name()</code></td><td><code>string</code></td><td>الحصول على اسم المشروع الحالي.</td></tr>
    <tr><td><code>project.set_name(name)</code></td><td><code>void</code></td><td>تعيين اسم المشروع.</td></tr>
</tbody>
</table>

<h3>3.2. مكتبة الشبكة والإنترنت (<code>net</code>)</h3>
<table>
<thead><tr><th style="width: 35%;">الدالة</th><th style="width: 25%;">القيمة المرجعة</th><th style="width: 40%;">الوصف</th></tr></thead>
<tbody>
    <tr><td><code>net.get(url)</code></td><td><code>table</code></td><td>تنفيذ طلب HTTP GET عبر الشبكة.</td></tr>
    <tr><td><code>net.post(url, body)</code></td><td><code>table</code></td><td>إرسال طلب HTTP POST.</td></tr>
    <tr><td><code>net.download(url, dest)</code></td><td><code>boolean</code></td><td>تنزيل ملف عبر الإنترنت وتخزينه محلياً.</td></tr>
</tbody>
</table>

<h3>3.3. المخطط الزمني والمسارات (<code>timeline</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">الدالة</th><th style="width: 22%;">القيمة المرجعة</th><th style="width: 40%;">الوصف</th></tr></thead>
<tbody>
    <tr><td><code>timeline.get_playhead()</code></td><td><code>number</code></td><td>موقع مؤشر التشغيل بالثواني.</td></tr>
    <tr><td><code>timeline.set_playhead(t)</code></td><td><code>void</code></td><td>تحريك مؤشر التشغيل إلى الثانية <code>t</code>.</td></tr>
    <tr><td><code>timeline.place_clip(path, trk, t, dur)</code></td><td><code>string</code></td><td>إدراج مقطع فيديو أو صوت أو صورة في المسار.</td></tr>
    <tr><td><code>timeline.add_keyframe(id, prop, t, val)</code></td><td><code>void</code></td><td>إضافة إطار مفتاحي للحركة (الشفافية، الصوت، إلخ).</td></tr>
    <tr><td><code>timeline.clear_keyframes(id, prop)</code></td><td><code>void</code></td><td>مسح كافة الإطارات المفتاحية للخاصية.</td></tr>
    <tr><td><code>timeline.move_track_up(idx)</code></td><td><code>void</code></td><td>تحريك المسار لأعلى.</td></tr>
    <tr><td><code>timeline.move_track_down(idx)</code></td><td><code>void</code></td><td>تحريك المسار لأسفل.</td></tr>
    <tr><td><code>timeline.set_track_name(idx, name)</code></td><td><code>void</code></td><td>إعادة تسمية المسار.</td></tr>
</tbody>
</table>

<h3>3.4. خصائص وتحويلات المقاطع (<code>clip</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">الدالة</th><th style="width: 22%;">القيمة المرجعة</th><th style="width: 40%;">الوصف</th></tr></thead>
<tbody>
    <tr><td><code>clip.get_position(id)</code></td><td><code>number, number</code></td><td>الحصول على إحداثيات (x, y) بالنسبة لمركز الشاشة.</td></tr>
    <tr><td><code>clip.set_position(id, x, y)</code></td><td><code>void</code></td><td>تعيين موقع مركز المقطع.</td></tr>
    <tr><td><code>clip.get_scale(id)</code></td><td><code>number</code></td><td>الحصول على معامل المقياس (1.0 = 100%).</td></tr>
    <tr><td><code>clip.set_scale(id, s)</code></td><td><code>void</code></td><td>تعيين معامل المقياس.</td></tr>
    <tr><td><code>clip.get_opacity(id)</code></td><td><code>number</code></td><td>الحصول على درجة الشفافية [0.0, 1.0].</td></tr>
    <tr><td><code>clip.set_opacity(id, o)</code></td><td><code>void</code></td><td>تعيين درجة الشفافية.</td></tr>
    <tr><td><code>clip.get_volume(id)</code></td><td><code>number</code></td><td>الحصول على مضخم الصوت [0.0, 2.0].</td></tr>
    <tr><td><code>clip.set_volume(id, v)</code></td><td><code>void</code></td><td>تعيين مستوى الصوت.</td></tr>
    <tr><td><code>clip.get_transform(clip_id)</code></td><td><code>table</code></td><td>الحصول على جدول خصائص التحويل <code>{{x, y, scale, rotation}}</code>.</td></tr>
    <tr><td><code>clip.set_transform(clip_id, x, y, s, r)</code></td><td><code>void</code></td><td>تعيين الإزاحة الأفقية والعمودية والمقياس وزاوية الدوران معاً.</td></tr>
    <tr><td><code>clip.reset_transform(clip_id)</code></td><td><code>void</code></td><td>إعادة تعيين التحويل للوضع الافتراضي (x=0, y=0, scale=1.0, rotation=0).</td></tr>
</tbody>
</table>

<h3>3.5. عناصر الواجهة الرسومية (<code>gui</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">الدالة</th><th style="width: 22%;">القيمة المرجعة</th><th style="width: 40%;">الوصف</th></tr></thead>
<tbody>
    <tr><td><code>gui.text(text)</code></td><td><code>void</code></td><td>عرض نص في اللوحة.</td></tr>
    <tr><td><code>gui.text_colored(r, g, b, a, text)</code></td><td><code>void</code></td><td>عرض نص ملون بتنسيق RGBA.</td></tr>
    <tr><td><code>gui.button(label, [callback])</code></td><td><code>boolean</code></td><td>إنشاء زر وتنفيذ دالة عند الضغط عليه.</td></tr>
    <tr><td><code>gui.slider_float(label, val, min, max)</code></td><td><code>number</code></td><td>شريط تمرير للأرقام العشرية.</td></tr>
    <tr><td><code>gui.slider_int(label, val, min, max)</code></td><td><code>integer</code></td><td>شريط تمرير للأرقام الصحيحة.</td></tr>
    <tr><td><code>gui.input_text(label, text)</code></td><td><code>string</code></td><td>حقل إدخال نصي.</td></tr>
    <tr><td><code>gui.file_dialog(title, cb, [filter])</code></td><td><code>void</code></td><td>فتح نافذة حوار اختيار الملفات.</td></tr>
    <tr><td><code>gui.popout_preview(popped)</code></td><td><code>void</code></td><td>فصل نافذة المعاينة كنافذة عائمة مستقلة (true) أو إرساؤها (false).</td></tr>
    <tr><td><code>gui.is_preview_popped_out()</code></td><td><code>boolean</code></td><td>معرفة ما إذا كانت نافذة المعاينة عائمة حالياً.</td></tr>
    <tr><td><code>gui.set_preview_aspect_mode(mode)</code></td><td><code>void</code></td><td>تعيين نسبة أبعاد المعاينة (0: تلقائي، 1: 16:9، 2: 9:16، 3: 4:3، 4: 1:1، 5: 21:9، 6: 4:5).</td></tr>
    <tr><td><code>gui.get_preview_aspect_mode()</code></td><td><code>integer</code></td><td>الحصول على مؤشر نسبة الأبعاد الحالي (0-6).</td></tr>
    <tr><td><code>gui.set_transform_tool(tool)</code></td><td><code>void</code></td><td>تعيين أداة التحويل النشطة ("select", "translate", "scale", "rotate").</td></tr>
    <tr><td><code>gui.get_transform_tool()</code></td><td><code>string</code></td><td>الحصول على اسم أداة التحويل النشطة للمعاينة.</td></tr>
</tbody>
</table>

<h3>3.6. أتمتة التصدير ومتابعة التقدم (<code>export</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">الدالة</th><th style="width: 22%;">القيمة المرجعة</th><th style="width: 40%;">الوصف</th></tr></thead>
<tbody>
    <tr><td><code>export.get_audio_progress()</code></td><td><code>number</code></td><td>نسبة تقدم معالجة ومزج الصوت المجسم والإطارات المفتاحية (0.0 إلى 1.0).</td></tr>
    <tr><td><code>export.get_progress()</code></td><td><code>number</code></td><td>نسبة التقدم الإجمالية لعملية التصدير (0.0 إلى 1.0).</td></tr>
    <tr><td><code>export.is_exporting()</code></td><td><code>boolean</code></td><td>معرفة ما إذا كانت عملية التصدير والترميز قيد التشغيل حالياً.</td></tr>
    <tr><td><code>export.get_status()</code></td><td><code>string</code></td><td>وصف نصي لحالة خط أنابيب التصدير الحالية.</td></tr>
</tbody>
</table>

<div class="footer">
    minicut v1.5.0 • محرر فيديو خفيف ومتعدد المسارات لنظام لينكس • تم إنشاء الدليل تلقائياً
</div>
</body>
</html>"""

def generate_js_en_html():
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>minicut JavaScript Scripting & Plugin API Reference Manual</title>
<style>{CSS_TEMPLATE}</style>
</head>
<body>
<div class="cover">
    <h1>minicut Video Editor</h1>
    <div class="subtitle">Embedded JavaScript & Plugin API Reference Manual</div>
    <div>
        <span class="badge">Duktape 2.7.0 Engine</span>
        <span class="badge">Full UTF-8 / Multi-lingual</span>
        <span class="badge">Dockable Tool Panels</span>
        <span class="badge">Tool Shelf Dock</span>
        <span class="badge">Network Library (libcurl)</span>
    </div>
</div>

<h2>1. Introduction & JavaScript Architecture</h2>
<p>
    <strong>minicut</strong> embeds a complete <strong>JavaScript</strong> engine (Duktape 2.7.0) with native UTF-8 and Unicode string support.
    JavaScript plugins provide complete parity with the minicut scripting API, making it easy to create internationalized tools across all supported languages.
</p>

<h3>Script Directory Locations</h3>
<table>
<thead>
    <tr><th style="width: 32%;">Location</th><th style="width: 28%;">Trigger / Role</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr>
        <td><code>~/.config/minicut/autorun/</code></td>
        <td>Startup / Auto-register</td>
        <td><code>.js</code> files execute on startup and generate an icon button on the horizontal Tool Shelf Dock below the main menu bar.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/scripts/</code></td>
        <td>Menu Click / On Demand</td>
        <td><code>.js</code> files appear directly in the top-level <strong>Modifiers</strong> menu on the main menu bar.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/shaders/gl/</code></td>
        <td>Custom GL Shaders</td>
        <td>User OpenGL vertex (<code>.vert</code>) and fragment (<code>.frag</code>) shaders loaded by name via <code>shader.create("name")</code>.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/shaders/vulkan/</code></td>
        <td>Custom Vulkan Shaders</td>
        <td>User Vulkan GLSL vertex and fragment shaders compiled for the Vulkan rendering backend.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/icons/</code></td>
        <td>Dock Icon Asset</td>
        <td>PNG icon images matching script stems (e.g., <code>quick_fade_js.png</code> for <code>quick_fade_js.js</code>).</td>
    </tr>
</tbody>
</table>

<h2>2. JavaScript Plugin Architecture</h2>
<pre>// ==============================================================================
// Example JavaScript Plugin: sample_tool.js
// ==============================================================================

var targetTrack = 0;
var clipDuration = 4.0;
var mediaPath = "tests/assets/test_sample.png";

function placeMedia() {{
    minicut.push_undo();
    var playhead = timeline.get_playhead();
    var cid = timeline.place_clip(mediaPath, targetTrack, playhead, clipDuration);
    if (cid && cid.length > 0) {{
        clip.set_scale(cid, 1.25);
        timeline.add_keyframe(cid, "opacity", 0.0, 0.0);
        timeline.add_keyframe(cid, "opacity", 1.0, 1.0);
        minicut.set_status("Placed media clip: " + cid);
    }}
}}

function browseFile() {{
    gui.file_dialog("Select Media", function(selected) {{
        if (selected && selected.length > 0) {{
            mediaPath = selected;
            minicut.set_status("Selected: " + selected);
        }}
    }});
}}

plugin.init_panel = function() {{
    gui.text_colored(0.24, 0.68, 0.91, 1.0, "Media Placer (JS)");
    gui.separator();
    targetTrack = gui.slider_int("Track Lane", targetTrack, 0, 4);
    clipDuration = gui.slider_float("Duration", clipDuration, 0.5, 30.0);
    gui.button("Browse Media...", browseFile);
    gui.button("Place on Timeline", placeMedia);
}};</pre>

<h2>3. JavaScript Modules API</h2>

<h3>3.1. Project & File Automation (<code>project</code> / Global)</h3>
<table>
<thead><tr><th style="width: 35%;">Function</th><th style="width: 25%;">Returns</th><th style="width: 40%;">Description</th></tr></thead>
<tbody>
    <tr><td><code>project_save([path])</code><br><code>project.save([path])</code></td><td><code>boolean</code></td><td>Saves project to <code>.minicut</code> JSON file.</td></tr>
    <tr><td><code>project_load(path)</code><br><code>project.load(path)</code></td><td><code>boolean</code></td><td>Loads project from path.</td></tr>
    <tr><td><code>project.get_name()</code></td><td><code>string</code></td><td>Returns current project name.</td></tr>
    <tr><td><code>project.set_name(name)</code></td><td><code>void</code></td><td>Sets current project name.</td></tr>
</tbody>
</table>

<h3>3.2. Network & Internet Library (<code>net</code>)</h3>
<table>
<thead><tr><th style="width: 35%;">Function</th><th style="width: 25%;">Returns</th><th style="width: 40%;">Description</th></tr></thead>
<tbody>
    <tr><td><code>net.get(url)</code></td><td><code>object</code></td><td>HTTP GET request returning <code>{{status, body, ok, headers}}</code>.</td></tr>
    <tr><td><code>net.post(url, body)</code></td><td><code>object</code></td><td>HTTP POST request.</td></tr>
    <tr><td><code>net.download(url, dest)</code></td><td><code>boolean</code></td><td>Downloads remote asset directly to file (supports HTTP, HTTPS, FTP, SFTP).</td></tr>
    <tr><td><code>net.upload(url, path)</code></td><td><code>object</code></td><td>Uploads local file binary stream (supports HTTP multipart, FTP, and secure SFTP).</td></tr>
</tbody>
</table>

<h3>3.3. Timeline & Track Management (<code>timeline</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">Function</th><th style="width: 22%;">Returns</th><th style="width: 40%;">Description</th></tr></thead>
<tbody>
    <tr><td><code>timeline.get_playhead()</code></td><td><code>number</code></td><td>Gets playhead time in seconds.</td></tr>
    <tr><td><code>timeline.set_playhead(t)</code></td><td><code>void</code></td><td>Sets playhead time in seconds.</td></tr>
    <tr><td><code>timeline.place_clip(path, track, t, dur)</code></td><td><code>string</code></td><td>Places media clip on track and returns clip ID.</td></tr>
    <tr><td><code>timeline.add_keyframe(id, prop, t, val)</code></td><td><code>void</code></td><td>Adds keyframe for property at timestamp.</td></tr>
    <tr><td><code>timeline.clear_keyframes(id, prop)</code></td><td><code>void</code></td><td>Clears all keyframes for property.</td></tr>
    <tr><td><code>timeline.select_clips(ids_array)</code></td><td><code>void</code></td><td>Selects multiple clips simultaneously.</td></tr>
    <tr><td><code>timeline.move_track_up(idx)</code></td><td><code>void</code></td><td>Moves track up in stack.</td></tr>
    <tr><td><code>timeline.move_track_down(idx)</code></td><td><code>void</code></td><td>Moves track down in stack.</td></tr>
    <tr><td><code>timeline.set_track_name(idx, name)</code></td><td><code>void</code></td><td>Renames track at index.</td></tr>
</tbody>
</table>

<h3>3.4. Clip Attributes & Transforms (<code>clip</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">Function</th><th style="width: 22%;">Returns</th><th style="width: 40%;">Description</th></tr></thead>
<tbody>
    <tr><td><code>clip.get_position(id)</code></td><td><code>array [x, y]</code></td><td>Gets position coordinates relative to canvas center.</td></tr>
    <tr><td><code>clip.set_position(id, x, y)</code></td><td><code>void</code></td><td>Sets center position coordinates.</td></tr>
    <tr><td><code>clip.get_scale(id)</code></td><td><code>number</code></td><td>Gets uniform scale factor (1.0 = 100%).</td></tr>
    <tr><td><code>clip.set_scale(id, s)</code></td><td><code>void</code></td><td>Sets uniform scale factor.</td></tr>
    <tr><td><code>clip.get_opacity(id)</code></td><td><code>number</code></td><td>Gets opacity [0.0, 1.0].</td></tr>
    <tr><td><code>clip.set_opacity(id, o)</code></td><td><code>void</code></td><td>Sets opacity.</td></tr>
    <tr><td><code>clip.get_volume(id)</code></td><td><code>number</code></td><td>Gets audio volume gain multiplier [0.0, 2.0].</td></tr>
    <tr><td><code>clip.set_volume(id, v)</code></td><td><code>void</code></td><td>Sets audio volume gain multiplier.</td></tr>
    <tr><td><code>clip.get_transform(id)</code></td><td><code>object</code></td><td>Gets transform object <code>{{x, y, scale, rotation}}</code>.</td></tr>
    <tr><td><code>clip.set_transform(id, x, y, s, r)</code></td><td><code>void</code></td><td>Sets translation X, Y, scale, and rotation simultaneously.</td></tr>
    <tr><td><code>clip.reset_transform(id)</code></td><td><code>void</code></td><td>Resets clip transform to origin (x=0, y=0, scale=1.0, rotation=0).</td></tr>
    <tr><td><code>clip.get_frame(id, [t, w, h])</code></td><td><code>object</code></td><td>Extracts raw RGBA frame object. Pass <code>"active"</code> for selected clip. Supports fluent chaining (<code>.apply_shader().reinsert()</code>).</td></tr>
    <tr><td><code>clip.set_frame(id, frame, [t])</code></td><td><code>boolean</code></td><td>Reinserts modified RGBA pixel frame into clip media. Triggers full-clip rendering if shader attached.</td></tr>
</tbody>
</table>

<h3>3.5. GUI Controls (<code>gui</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">Function</th><th style="width: 22%;">Returns</th><th style="width: 40%;">Description</th></tr></thead>
<tbody>
    <tr><td><code>gui.text(text)</code></td><td><code>void</code></td><td>Renders text label.</td></tr>
    <tr><td><code>gui.text_colored(r, g, b, a, text)</code></td><td><code>void</code></td><td>Renders colored text with RGBA.</td></tr>
    <tr><td><code>gui.button(label, [callback])</code></td><td><code>boolean</code></td><td>Renders button and triggers callback on click.</td></tr>
    <tr><td><code>gui.slider_float(label, val, min, max)</code></td><td><code>number</code></td><td>Renders float slider.</td></tr>
    <tr><td><code>gui.slider_int(label, val, min, max)</code></td><td><code>integer</code></td><td>Renders integer slider.</td></tr>
    <tr><td><code>gui.input_text(label, text)</code></td><td><code>string</code></td><td>Renders text entry box.</td></tr>
    <tr><td><code>gui.file_dialog(title, cb, [filter])</code></td><td><code>void</code></td><td>Opens file dialog matching user settings.</td></tr>
    <tr><td><code>gui.popout_preview(popped)</code></td><td><code>void</code></td><td>Detaches (true) or docks (false) the video preview panel into a floating window.</td></tr>
    <tr><td><code>gui.is_preview_popped_out()</code></td><td><code>boolean</code></td><td>Returns whether preview is currently detached/floating.</td></tr>
    <tr><td><code>gui.set_preview_aspect_mode(mode)</code></td><td><code>void</code></td><td>Sets aspect mode: 0 (Auto), 1 (16:9), 2 (9:16), 3 (4:3), 4 (1:1), 5 (21:9), 6 (4:5).</td></tr>
    <tr><td><code>gui.get_preview_aspect_mode()</code></td><td><code>number</code></td><td>Returns current preview aspect mode index (0-6).</td></tr>
    <tr><td><code>gui.set_transform_tool(tool)</code></td><td><code>void</code></td><td>Sets active preview gizmo tool ("select", "translate", "scale", "rotate").</td></tr>
    <tr><td><code>gui.get_transform_tool()</code></td><td><code>string</code></td><td>Returns active preview gizmo tool name.</td></tr>
</tbody>
</table>

<h3>3.6. Export Automation & Progress (<code>export</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">Function</th><th style="width: 22%;">Returns</th><th style="width: 40%;">Description</th></tr></thead>
<tbody>
    <tr><td><code>export.get_audio_progress()</code></td><td><code>number</code></td><td>Audio mixing progress (stereo mixing & keyframes, 0.0 to 1.0).</td></tr>
    <tr><td><code>export.get_progress()</code></td><td><code>number</code></td><td>Overall export pipeline progress (0.0 to 1.0).</td></tr>
    <tr><td><code>export.is_exporting()</code></td><td><code>boolean</code></td><td>True if background render/encode is actively running.</td></tr>
    <tr><td><code>export.get_status()</code></td><td><code>string</code></td><td>Human-readable pipeline status description.</td></tr>
</tbody>
</table>

<h3>3.7. Custom GLSL Shader Pipeline (<code>shader</code>)</h3>
<p>
    Loads custom shaders from <code>~/.config/minicut/shaders/gl/</code> and <code>~/.config/minicut/shaders/vulkan/</code> by name (e.g. <code>"ripple"</code>, <code>"grayscale"</code>, <code>"invert"</code>, <code>"vignette"</code>), or compiles custom GLSL fragment source strings.
    When applied to a clip, the shader processes the <strong>entire clip frame-by-frame</strong> in the background using FFmpeg without blocking UI responsiveness.
</p>
<table>
<thead>
    <tr><th style="width: 38%;">Function</th><th style="width: 22%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr><td><code>shader.create(name_or_src)</code></td><td><code>number</code></td><td>Loads shader by name from shaders directory or compiles in-memory GLSL source.</td></tr>
    <tr><td><code>shader.set_uniform(id, name, val)</code></td><td><code>void</code></td><td>Sets float, int, or object vector uniform (matches with or without <code>u_</code> prefix).</td></tr>
    <tr><td><code>shader.get_uniform(id, name)</code></td><td><code>number / object</code></td><td>Returns current uniform parameter value.</td></tr>
    <tr><td><code>shader.apply(id, target, [uniforms])</code></td><td><code>boolean / object</code></td><td>Renders shader across all video frames of clip (or transforms single frame buffer).</td></tr>
</tbody>
</table>

<h4>Frame Buffer Fluent Piping</h4>
<pre>// Fluent method chaining across entire clip
clip.get_frame("active").apply_shader("ripple", {{ amplitude: 12.0, frequency: 0.08 }}).reinsert();

// Or synthesize a brand-new clip on track 0
clip.get_frame("active").apply_shader("ripple").create_clip(0, 0.0, 5.0, "Ripple Wave");</pre>

<div class="footer">
    minicut v1.5.0 • Lightweight Linux Multi-track Video Editor • Generated Documentation
</div>
</body>
</html>"""

def generate_js_ja_html():
    return f"""<!DOCTYPE html>
<html lang="ja">
<head>
<meta charset="UTF-8">
<title>minicut JavaScript スクリプト & プラグイン API リファレンスマニュアル</title>
<style>{CSS_TEMPLATE}</style>
</head>
<body>
<div class="cover">
    <h1>minicut 動画編集エディタ</h1>
    <div class="subtitle">組み込み JavaScript & プラグイン API リファレンスマニュアル</div>
    <div>
        <span class="badge">Duktape 2.7.0 エンジン</span>
        <span class="badge">完全な UTF-8 / 多言語対応</span>
        <span class="badge">ドッキング可能ツールパネル</span>
        <span class="badge">ツールシェルフドック</span>
        <span class="badge">ネットワークライブラリ (libcurl)</span>
    </div>
</div>

<h2>1. はじめに & JavaScript アーキテクチャ</h2>
<p>
    <strong>minicut</strong> は完全な <strong>JavaScript</strong> エンジン（Duktape 2.7.0）を内蔵しており、標準でネイティブ UTF-8 および多言語 Unicode 文字列を扱えます。
    Lua と完全に同等の API が提供されており、日本語などのスクリプトツールを容易に構築できます。
</p>

<h3>スクリプト配置場所</h3>
<table>
<thead><tr><th style="width: 32%;">配置場所</th><th style="width: 28%;">役割</th><th style="width: 40%;">説明</th></tr></thead>
<tbody>
    <tr><td><code>~/.config/minicut/autorun/</code></td><td>起動時自動実行</td><td><code>.js</code> ファイルは起動時に自動読み込みされ、「ドック」にアイコンが追加されます。</td></tr>
    <tr><td><code>~/.config/minicut/scripts/</code></td><td>メニュー実行</td><td><code>.js</code> ファイルは「モディファイア (Modifiers)」メニューに表示されます。</td></tr>
    <tr><td><code>~/.config/minicut/shaders/gl/</code></td><td>カスタムGLシェーダー</td><td>OpenGL頂点/フラグメントシェーダー。</td></tr>
    <tr><td><code>~/.config/minicut/shaders/vulkan/</code></td><td>カスタムVulkanシェーダー</td><td>Vulkan用GLSLシェーダー。</td></tr>
    <tr><td><code>~/.config/minicut/icons/</code></td><td>ドックアイコン</td><td>スクリプト名と一致するPNGアイコン画像です。</td></tr>
</tbody>
</table>

<h2>2. JavaScript プラグイン構成</h2>
<pre>var targetTrack = 0;
var clipDuration = 4.0;
var mediaPath = "tests/assets/test_sample.png";

function placeMedia() {{
    minicut.push_undo();
    var playhead = timeline.get_playhead();
    var cid = timeline.place_clip(mediaPath, targetTrack, playhead, clipDuration);
    if (cid && cid.length > 0) {{
        clip.set_scale(cid, 1.25);
        timeline.add_keyframe(cid, "opacity", 0.0, 0.0);
        timeline.add_keyframe(cid, "opacity", 1.0, 1.0);
        minicut.set_status("クリップを配置しました: " + cid);
    }}
}}

function browseFile() {{
    gui.file_dialog("ファイルを選択", function(selected) {{
        if (selected && selected.length > 0) {{
            mediaPath = selected;
            minicut.set_status("選択: " + selected);
        }}
    }});
}}

plugin.init_panel = function() {{
    gui.text_colored(0.24, 0.68, 0.91, 1.0, "メディア配置ツール (JS)");
    gui.separator();
    targetTrack = gui.slider_int("トラック番号", targetTrack, 0, 4);
    clipDuration = gui.slider_float("長さ(秒)", clipDuration, 0.5, 30.0);
    gui.button("ファイル参照...", browseFile);
    gui.button("タイムラインに配置", placeMedia);
}};</pre>

<h2>3. コアモジュール API</h2>
<p>JavaScript から利用可能な主要モジュールは <code>project</code>, <code>timeline</code>, <code>clip</code>, <code>gui</code>, <code>net</code>, <code>minicut</code>, <code>export</code> です。</p>

<h3>3.1. クリップ変形 & 属性 (<code>clip</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">関数</th><th style="width: 22%;">戻り値</th><th style="width: 40%;">説明</th></tr></thead>
<tbody>
    <tr><td><code>clip.get_position(id)</code></td><td><code>array [x, y]</code></td><td>中心基準の (x, y) 座標を取得します。</td></tr>
    <tr><td><code>clip.set_position(id, x, y)</code></td><td><code>void</code></td><td>中心座標を設定します。</td></tr>
    <tr><td><code>clip.get_scale(id)</code></td><td><code>number</code></td><td>スケール倍率（1.0 = 100%）を取得します。</td></tr>
    <tr><td><code>clip.set_scale(id, s)</code></td><td><code>void</code></td><td>スケール倍率を設定します。</td></tr>
    <tr><td><code>clip.get_opacity(id)</code></td><td><code>number</code></td><td>不透明度 [0.0, 1.0] を取得します。</td></tr>
    <tr><td><code>clip.set_opacity(id, o)</code></td><td><code>void</code></td><td>不透明度を設定します。</td></tr>
    <tr><td><code>clip.get_volume(id)</code></td><td><code>number</code></td><td>音声ボリュームゲイン [0.0, 2.0] を取得します。</td></tr>
    <tr><td><code>clip.set_volume(id, v)</code></td><td><code>void</code></td><td>音声ボリュームゲインを設定します。</td></tr>
    <tr><td><code>clip.get_transform(id)</code></td><td><code>object</code></td><td>変形オブジェクト <code>{{x, y, scale, rotation}}</code> を取得します。</td></tr>
    <tr><td><code>clip.set_transform(id, x, y, s, r)</code></td><td><code>void</code></td><td>位置(X, Y)、拡大率、回転角を一括設定します。</td></tr>
    <tr><td><code>clip.reset_transform(id)</code></td><td><code>void</code></td><td>変形を初期状態（x=0, y=0, scale=1.0, rotation=0）にリセットします。</td></tr>
</tbody>
</table>

<h3>3.2. GUI コントロール (<code>gui</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">関数</th><th style="width: 22%;">戻り値</th><th style="width: 40%;">説明</th></tr></thead>
<tbody>
    <tr><td><code>gui.text(text)</code></td><td><code>void</code></td><td>テキストを表示します。</td></tr>
    <tr><td><code>gui.text_colored(r, g, b, a, text)</code></td><td><code>void</code></td><td>色付きテキストを表示します。</td></tr>
    <tr><td><code>gui.button(label, [callback])</code></td><td><code>boolean</code></td><td>ボタンを表示しクリック時に実行します。</td></tr>
    <tr><td><code>gui.slider_float(label, val, min, max)</code></td><td><code>number</code></td><td>浮動小数点スライダーを表示します。</td></tr>
    <tr><td><code>gui.slider_int(label, val, min, max)</code></td><td><code>integer</code></td><td>整数スライダーを表示します。</td></tr>
    <tr><td><code>gui.input_text(label, text)</code></td><td><code>string</code></td><td>テキスト入力フィールドを表示します。</td></tr>
    <tr><td><code>gui.file_dialog(title, cb, [filter])</code></td><td><code>void</code></td><td>ファイルダイアログを開き結果を返します。</td></tr>
    <tr><td><code>gui.popout_preview(popped)</code></td><td><code>void</code></td><td>プレビュー画面を独立ウィンドウに分離（true）またはドッキング（false）します。</td></tr>
    <tr><td><code>gui.is_preview_popped_out()</code></td><td><code>boolean</code></td><td>プレビューが現在分離されているかを取得します。</td></tr>
    <tr><td><code>gui.set_preview_aspect_mode(mode)</code></td><td><code>void</code></td><td>アスペクト比モード（0:自動, 1:16:9, 2:9:16, 3:4:3, 4:1:1, 5:21:9, 6:4:5）を設定します。</td></tr>
    <tr><td><code>gui.get_preview_aspect_mode()</code></td><td><code>number</code></td><td>現在のアスペクト比モード（0〜6）を取得します。</td></tr>
    <tr><td><code>gui.set_transform_tool(tool)</code></td><td><code>void</code></td><td>ギズモ変形ツール（"select", "translate", "scale", "rotate"）を設定します。</td></tr>
    <tr><td><code>gui.get_transform_tool()</code></td><td><code>string</code></td><td>現在の変形ツール名を取得します。</td></tr>
</tbody>
</table>

<h3>3.3. エクスポート操作 & 進捗状況 (<code>export</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">関数</th><th style="width: 22%;">戻り値</th><th style="width: 40%;">説明</th></tr></thead>
<tbody>
    <tr><td><code>export.get_audio_progress()</code></td><td><code>number</code></td><td>音声ミキシングの進捗率（ステレオ・キーフレーム処理、0.0〜1.0）を取得します。</td></tr>
    <tr><td><code>export.get_progress()</code></td><td><code>number</code></td><td>全体のエクスポート進捗率（0.0〜1.0）を取得します。</td></tr>
    <tr><td><code>export.is_exporting()</code></td><td><code>boolean</code></td><td>エクスポート処理が実行中かを取得します。</td></tr>
    <tr><td><code>export.get_status()</code></td><td><code>string</code></td><td>パイプラインの現在の状態文字列を取得します。</td></tr>
</tbody>
</table>

<div class="footer">
    minicut v1.5.0 • 軽量マルチトラック動画編集エディタ • ドキュメント自動生成
</div>
</body>
</html>"""

def generate_js_ko_html():
    return f"""<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="UTF-8">
<title>minicut JavaScript 스크립팅 & 플러그인 API 참조 매뉴얼</title>
<style>{CSS_TEMPLATE}</style>
</head>
<body>
<div class="cover">
    <h1>minicut 비디오 편집기</h1>
    <div class="subtitle">임베디드 JavaScript & 플러그인 API 참조 매뉴얼</div>
    <div>
        <span class="badge">Duktape 2.7.0 엔진</span>
        <span class="badge">완전한 UTF-8 / 다국어 지원</span>
        <span class="badge">도킹 가능 도구 패널</span>
        <span class="badge">도구 선반 도크</span>
        <span class="badge">네트워크 라이브러리 (libcurl)</span>
    </div>
</div>

<h2>1. 소개 및 JavaScript 아키텍처</h2>
<p>
    <strong>minicut</strong>은 고성능 <strong>JavaScript</strong> 임베디드 엔진(Duktape 2.7.0)을 내장하고 있어 완벽한 UTF-8 및 다국어 유니코드 문자열 처리를 지원합니다.
    Lua API와 동일한 기능을 완벽하게 제공하여 다국어 플러그인 도구를 손쉽게 개발할 수 있습니다.
</p>

<h3>스크립트 디렉터리 경로</h3>
<table>
<thead><tr><th style="width: 32%;">경로</th><th style="width: 28%;">실행 시점</th><th style="width: 40%;">설명</th></tr></thead>
<tbody>
    <tr><td><code>~/.config/minicut/autorun/</code></td><td>시작 시 자동 실행</td><td><code>.js</code> 파일은 시작 시 로드되어 도크에 아이콘이 생성됩니다.</td></tr>
    <tr><td><code>~/.config/minicut/scripts/</code></td><td>메뉴 클릭 시</td><td><code>.js</code> 파일은 상단 '수정자 (Modifiers)' 메뉴에 등록됩니다.</td></tr>
    <tr><td><code>~/.config/minicut/shaders/gl/</code></td><td>커스텀 GL 셰이더</td><td>OpenGL 버텍스/프래그먼트 셰이더.</td></tr>
    <tr><td><code>~/.config/minicut/shaders/vulkan/</code></td><td>커스텀 Vulkan 셰이더</td><td>Vulkan용 GLSL 셰이더.</td></tr>
    <tr><td><code>~/.config/minicut/icons/</code></td><td>아이콘 이미지</td><td>스크립트명과 일치하는 PNG 아이콘 파일입니다.</td></tr>
</tbody>
</table>

<h2>2. JavaScript 플러그인 구조</h2>
<pre>var targetTrack = 0;
var clipDuration = 4.0;
var mediaPath = "tests/assets/test_sample.png";

function placeMedia() {{
    minicut.push_undo();
    var playhead = timeline.get_playhead();
    var cid = timeline.place_clip(mediaPath, targetTrack, playhead, clipDuration);
    if (cid && cid.length > 0) {{
        clip.set_scale(cid, 1.25);
        timeline.add_keyframe(cid, "opacity", 0.0, 0.0);
        timeline.add_keyframe(cid, "opacity", 1.0, 1.0);
        minicut.set_status("클립 배치 완료: " + cid);
    }}
}}

function browseFile() {{
    gui.file_dialog("파일 선택 (JS)", function(selected) {{
        if (selected && selected.length > 0) {{
            mediaPath = selected;
            minicut.set_status("선택됨: " + selected);
        }}
    }});
}}

plugin.init_panel = function() {{
    gui.text_colored(0.24, 0.68, 0.91, 1.0, "미디어 배치 도구 (JS)");
    gui.separator();
    targetTrack = gui.slider_int("트랙 번호", targetTrack, 0, 4);
    clipDuration = gui.slider_float("길이(초)", clipDuration, 0.5, 30.0);
    gui.button("파일 찾아보기...", browseFile);
    gui.button("타임라인에 배치", placeMedia);
}};</pre>

<h2>3. 코어 모듈 API</h2>
<p>JavaScript 환경에서는 <code>project</code>, <code>timeline</code>, <code>clip</code>, <code>gui</code>, <code>net</code>, <code>minicut</code>, <code>export</code> 모듈을 동일하게 사용할 수 있습니다.</p>

<h3>3.1. 클립 변형 & 속성 (<code>clip</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">함수</th><th style="width: 22%;">반환값</th><th style="width: 40%;">설명</th></tr></thead>
<tbody>
    <tr><td><code>clip.get_position(id)</code></td><td><code>array [x, y]</code></td><td>캔버스 중심 기준 (x, y) 좌표를 반환합니다.</td></tr>
    <tr><td><code>clip.set_position(id, x, y)</code></td><td><code>void</code></td><td>중심 좌표를 설정합니다.</td></tr>
    <tr><td><code>clip.get_scale(id)</code></td><td><code>number</code></td><td>크기 비율(1.0 = 100%)을 반환합니다.</td></tr>
    <tr><td><code>clip.set_scale(id, s)</code></td><td><code>void</code></td><td>크기 비율을 설정합니다.</td></tr>
    <tr><td><code>clip.get_opacity(id)</code></td><td><code>number</code></td><td>불투명도 [0.0, 1.0]를 반환합니다.</td></tr>
    <tr><td><code>clip.set_opacity(id, o)</code></td><td><code>void</code></td><td>불투명도를 설정합니다.</td></tr>
    <tr><td><code>clip.get_volume(id)</code></td><td><code>number</code></td><td>오디오 볼륨 게인 [0.0, 2.0]을 반환합니다.</td></tr>
    <tr><td><code>clip.set_volume(id, v)</code></td><td><code>void</code></td><td>오디오 볼륨 게인을 설정합니다.</td></tr>
    <tr><td><code>clip.get_transform(id)</code></td><td><code>object</code></td><td>변형 객체 <code>{{x, y, scale, rotation}}</code>을 반환합니다.</td></tr>
    <tr><td><code>clip.set_transform(id, x, y, s, r)</code></td><td><code>void</code></td><td>위치(X, Y), 크기 비율, 회전 각도를 일괄 설정합니다.</td></tr>
    <tr><td><code>clip.reset_transform(id)</code></td><td><code>void</code></td><td>클립 변형을 기본값(x=0, y=0, scale=1.0, rotation=0)으로 초기화합니다.</td></tr>
</tbody>
</table>

<h3>3.2. GUI 위젯 (<code>gui</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">함수</th><th style="width: 22%;">반환값</th><th style="width: 40%;">설명</th></tr></thead>
<tbody>
    <tr><td><code>gui.text(text)</code></td><td><code>void</code></td><td>텍스트 라벨을 렌더링합니다.</td></tr>
    <tr><td><code>gui.text_colored(r, g, b, a, text)</code></td><td><code>void</code></td><td>색상 텍스트를 렌더링합니다.</td></tr>
    <tr><td><code>gui.button(label, [callback])</code></td><td><code>boolean</code></td><td>버튼을 렌더링하고 클릭 시 콜백을 실행합니다.</td></tr>
    <tr><td><code>gui.slider_float(label, val, min, max)</code></td><td><code>number</code></td><td>실수형 슬라이더를 렌더링합니다.</td></tr>
    <tr><td><code>gui.slider_int(label, val, min, max)</code></td><td><code>integer</code></td><td>정수형 슬라이더를 렌더링합니다.</td></tr>
    <tr><td><code>gui.input_text(label, text)</code></td><td><code>string</code></td><td>텍스트 입력 상자를 렌더링합니다.</td></tr>
    <tr><td><code>gui.file_dialog(title, cb, [filter])</code></td><td><code>void</code></td><td>파일 대화상자를 열고 결과를 콜백으로 전달합니다.</td></tr>
    <tr><td><code>gui.popout_preview(popped)</code></td><td><code>void</code></td><td>미리보기 창을 독립 플로팅 창으로 분리(true)하거나 도킹(false)합니다.</td></tr>
    <tr><td><code>gui.is_preview_popped_out()</code></td><td><code>boolean</code></td><td>미리보기가 분리된 플로팅 상태인지 여부를 반환합니다.</td></tr>
    <tr><td><code>gui.set_preview_aspect_mode(mode)</code></td><td><code>void</code></td><td>종횡비 모드(0:자동, 1:16:9, 2:9:16, 3:4:3, 4:1:1, 5:21:9, 6:4:5)를 설정합니다.</td></tr>
    <tr><td><code>gui.get_preview_aspect_mode()</code></td><td><code>number</code></td><td>현재 종횡비 모드 번호(0~6)를 반환합니다.</td></tr>
    <tr><td><code>gui.set_transform_tool(tool)</code></td><td><code>void</code></td><td>기즈모 변형 도구("select", "translate", "scale", "rotate")를 설정합니다.</td></tr>
    <tr><td><code>gui.get_transform_tool()</code></td><td><code>string</code></td><td>현재 활성 변형 도구 이름을 반환합니다.</td></tr>
</tbody>
</table>

<h3>3.3. 내보내기 자동화 및 진행률 (<code>export</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">함수</th><th style="width: 22%;">반환값</th><th style="width: 40%;">설명</th></tr></thead>
<tbody>
    <tr><td><code>export.get_audio_progress()</code></td><td><code>number</code></td><td>오디오 믹싱 진행률(스테레오 및 키프레임, 0.0~1.0)을 반환합니다.</td></tr>
    <tr><td><code>export.get_progress()</code></td><td><code>number</code></td><td>전체 내보내기 파이프라인 진행률(0.0~1.0)을 반환합니다.</td></tr>
    <tr><td><code>export.is_exporting()</code></td><td><code>boolean</code></td><td>내보내기 작업이 진행 중인지 여부를 반환합니다.</td></tr>
    <tr><td><code>export.get_status()</code></td><td><code>string</code></td><td>내보내기 상태 설명을 반환합니다.</td></tr>
</tbody>
</table>

<div class="footer">
    minicut v1.5.0 • 가벼운 리눅스 멀티트랙 비디오 편집기 • 자동 생성 문서
</div>
</body>
</html>"""

def generate_js_uk_html():
    return f"""<!DOCTYPE html>
<html lang="uk">
<head>
<meta charset="UTF-8">
<title>minicut Довідник з JavaScript та Plugin API</title>
<style>{CSS_TEMPLATE}</style>
</head>
<body>
<div class="cover">
    <h1>Відеоредактор minicut</h1>
    <div class="subtitle">Вбудований рушій JavaScript & Довідник з Plugin API</div>
    <div>
        <span class="badge">Рушій Duktape 2.7.0</span>
        <span class="badge">Повна підтримка UTF-8</span>
        <span class="badge">Панелі інструментів</span>
        <span class="badge">Док інструментів</span>
        <span class="badge">Мережева бібліотека (libcurl)</span>
    </div>
</div>

<h2>1. Вступ та JavaScript архітектура</h2>
<p>
    <strong>minicut</strong> містить вбудований рушій <strong>JavaScript</strong> (Duktape 2.7.0) із повноцінною підтримкою кодування UTF-8.
    Скрипти JavaScript мають повну функціональну еквівалентність із Lua API для розробки потужних локалізованих інструментів.
</p>

<h3>Каталоги розташування скриптів</h3>
<table>
<thead><tr><th style="width: 32%;">Розташування</th><th style="width: 28%;">Роль</th><th style="width: 40%;">Опис</th></tr></thead>
<tbody>
    <tr><td><code>~/.config/minicut/autorun/</code></td><td>Автозапуск при старті</td><td>Скрипти <code>.js</code> виконуються під час запуску та додають кнопку на панель Доку.</td></tr>
    <tr><td><code>~/.config/minicut/scripts/</code></td><td>Запуск з меню</td><td>Скрипти <code>.js</code> відображаються в меню <strong>Модифікатори (Modifiers)</strong>.</td></tr>
    <tr><td><code>~/.config/minicut/shaders/gl/</code></td><td>Користувацькі GL шейдери</td><td>Вершинні та фрагментні шейдери OpenGL.</td></tr>
    <tr><td><code>~/.config/minicut/shaders/vulkan/</code></td><td>Користувацькі Vulkan шейдери</td><td>Шейдери GLSL для Vulkan.</td></tr>
    <tr><td><code>~/.config/minicut/icons/</code></td><td>Іконки Доку</td><td>PNG файли іконок, що відповідають іменам скриптів.</td></tr>
</tbody>
</table>

<h2>2. Архітектура JavaScript плагіна</h2>
<pre>var targetTrack = 0;
var clipDuration = 4.0;
var mediaPath = "tests/assets/test_sample.png";

function placeMedia() {{
    minicut.push_undo();
    var playhead = timeline.get_playhead();
    var cid = timeline.place_clip(mediaPath, targetTrack, playhead, clipDuration);
    if (cid && cid.length > 0) {{
        clip.set_scale(cid, 1.25);
        timeline.add_keyframe(cid, "opacity", 0.0, 0.0);
        timeline.add_keyframe(cid, "opacity", 1.0, 1.0);
        minicut.set_status("Кліп розміщено: " + cid);
    }}
}}

function browseFile() {{
    gui.file_dialog("Оберіть файл (JS)", function(selected) {{
        if (selected && selected.length > 0) {{
            mediaPath = selected;
            minicut.set_status("Обрано: " + selected);
        }}
    }});
}}

plugin.init_panel = function() {{
    gui.text_colored(0.24, 0.68, 0.91, 1.0, "Інструмент розміщення медіа (JS)");
    gui.separator();
    targetTrack = gui.slider_int("Номер доріжки", targetTrack, 0, 4);
    clipDuration = gui.slider_float("Тривалість (с)", clipDuration, 0.5, 30.0);
    gui.button("Огляд файлів...", browseFile);
    gui.button("Розмістити на таймлайні", placeMedia);
}};</pre>

<h2>3. Модулі API</h2>
<p>У JavaScript доступні всі основні модулі: <code>project</code>, <code>timeline</code>, <code>clip</code>, <code>gui</code>, <code>net</code>, <code>minicut</code>, <code>export</code>.</p>

<h3>3.1. Трансформація та властивості кліпів (<code>clip</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">Функція</th><th style="width: 22%;">Повертає</th><th style="width: 40%;">Опис</th></tr></thead>
<tbody>
    <tr><td><code>clip.get_position(id)</code></td><td><code>array [x, y]</code></td><td>Отримує координати (x, y) відносно центру.</td></tr>
    <tr><td><code>clip.set_position(id, x, y)</code></td><td><code>void</code></td><td>Встановлює координати центру кліпу.</td></tr>
    <tr><td><code>clip.get_scale(id)</code></td><td><code>number</code></td><td>Отримує коефіцієнт масштабування (1.0 = 100%).</td></tr>
    <tr><td><code>clip.set_scale(id, s)</code></td><td><code>void</code></td><td>Встановлює коефіцієнт масштабування.</td></tr>
    <tr><td><code>clip.get_opacity(id)</code></td><td><code>number</code></td><td>Отримує рівень непрозорості [0.0, 1.0].</td></tr>
    <tr><td><code>clip.set_opacity(id, o)</code></td><td><code>void</code></td><td>Встановлює непрозорість кліпу.</td></tr>
    <tr><td><code>clip.get_volume(id)</code></td><td><code>number</code></td><td>Отримує підсилення гучності [0.0, 2.0].</td></tr>
    <tr><td><code>clip.set_volume(id, v)</code></td><td><code>void</code></td><td>Встановлює підсилення гучності.</td></tr>
    <tr><td><code>clip.get_transform(id)</code></td><td><code>object</code></td><td>Отримує об'єкт трансформацій <code>{{x, y, scale, rotation}}</code>.</td></tr>
    <tr><td><code>clip.set_transform(id, x, y, s, r)</code></td><td><code>void</code></td><td>Одночасно встановлює зміщення X, Y, масштаб та обертання.</td></tr>
    <tr><td><code>clip.reset_transform(id)</code></td><td><code>void</code></td><td>Скидає трансформацію до початкового стану (x=0, y=0, scale=1.0, rotation=0).</td></tr>
</tbody>
</table>

<h3>3.2. Графічні елементи керування (<code>gui</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">Функція</th><th style="width: 22%;">Повертає</th><th style="width: 40%;">Опис</th></tr></thead>
<tbody>
    <tr><td><code>gui.text(text)</code></td><td><code>void</code></td><td>Виводить текстовий напис.</td></tr>
    <tr><td><code>gui.text_colored(r, g, b, a, text)</code></td><td><code>void</code></td><td>Виводить кольоровий текст RGBA.</td></tr>
    <tr><td><code>gui.button(label, [callback])</code></td><td><code>boolean</code></td><td>Створює кнопку з можливістю виклику функції.</td></tr>
    <tr><td><code>gui.slider_float(label, val, min, max)</code></td><td><code>number</code></td><td>Створює повзунок для дійсних чисел.</td></tr>
    <tr><td><code>gui.slider_int(label, val, min, max)</code></td><td><code>integer</code></td><td>Створює повзунок для цілих чисел.</td></tr>
    <tr><td><code>gui.input_text(label, text)</code></td><td><code>string</code></td><td>Створює поле введення тексту.</td></tr>
    <tr><td><code>gui.file_dialog(title, cb, [filter])</code></td><td><code>void</code></td><td>Відкриває діалогове вікно вибору файлів.</td></tr>
    <tr><td><code>gui.popout_preview(popped)</code></td><td><code>void</code></td><td>Відокремлює (true) або прикріплює (false) вікно попереднього перегляду.</td></tr>
    <tr><td><code>gui.is_preview_popped_out()</code></td><td><code>boolean</code></td><td>Повертає, чи є вікно попереднього перегляду плаваючим.</td></tr>
    <tr><td><code>gui.set_preview_aspect_mode(mode)</code></td><td><code>void</code></td><td>Встановлює режим співвідношення сторін (0: Авто, 1: 16:9, 2: 9:16, 3: 4:3, 4: 1:1, 5: 21:9, 6: 4:5).</td></tr>
    <tr><td><code>gui.get_preview_aspect_mode()</code></td><td><code>number</code></td><td>Повертає поточний індекс режиму співвідношення сторін (0–6).</td></tr>
    <tr><td><code>gui.set_transform_tool(tool)</code></td><td><code>void</code></td><td>Встановлює інструмент трансформації ("select", "translate", "scale", "rotate").</td></tr>
    <tr><td><code>gui.get_transform_tool()</code></td><td><code>string</code></td><td>Повертає назву активного інструмента трансформації.</td></tr>
</tbody>
</table>

<h3>3.3. Автоматизація експорту та прогрес (<code>export</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">Функція</th><th style="width: 22%;">Повертає</th><th style="width: 40%;">Опис</th></tr></thead>
<tbody>
    <tr><td><code>export.get_audio_progress()</code></td><td><code>number</code></td><td>Прогрес мікшування аудіо (стереозведення та ключові кадри, 0.0 до 1.0).</td></tr>
    <tr><td><code>export.get_progress()</code></td><td><code>number</code></td><td>Загальний прогрес конвеєра експорту (0.0 до 1.0).</td></tr>
    <tr><td><code>export.is_exporting()</code></td><td><code>boolean</code></td><td>Повертає true, якщо фоновий рендеринг/кодування активні.</td></tr>
    <tr><td><code>export.get_status()</code></td><td><code>string</code></td><td>Текстовий опис поточного стану експорту.</td></tr>
</tbody>
</table>

<div class="footer">
    minicut v1.5.0 • Легкий мультитрековий відеоредактор для Linux • Автоматична документація
</div>
</body>
</html>"""

def generate_js_ar_html():
    return f"""<!DOCTYPE html>
<html lang="ar" dir="rtl">
<head>
<meta charset="UTF-8">
<title>minicut دليل مرجعي للبرمجة بلغة JavaScript وواجهة Plugin API</title>
<style>{CSS_TEMPLATE}</style>
</head>
<body class="rtl">
<div class="cover">
    <h1>محرر الفيديو minicut</h1>
    <div class="subtitle">دليل مرجعي لبيئة البرمجة JavaScript والإضافات البرمجية</div>
    <div>
        <span class="badge">محرك Duktape 2.7.0</span>
        <span class="badge">دعم كامل لترميز UTF-8</span>
        <span class="badge">لوحات أدوات قابلة للتثبيت</span>
        <span class="badge">منصة الأدوات (Dock)</span>
        <span class="badge">مكتبة الشبكة (libcurl)</span>
    </div>
</div>

<h2>1. المقدمة والبنية البرمجية لـ JavaScript</h2>
<p>
    يحتوي برنامج <strong>minicut</strong> على محرك برمجيات مدمج بلغة <strong>JavaScript</strong> (Duktape 2.7.0) مع دعم كامل وأصلي لترميز النصوص متعددة اللغات (UTF-8).
    توفر واجهة JavaScript توافقاً تاماً مع كافة دوال وميزات محرك minicut.
</p>

<h3>مسارات مجلدات السكربتات</h3>
<table>
<thead><tr><th style="width: 32%;">المسار</th><th style="width: 28%;">الدور</th><th style="width: 40%;">الوصف</th></tr></thead>
<tbody>
    <tr><td><code>~/.config/minicut/autorun/</code></td><td>التشغيل التلقائي</td><td>يتم تشغيل ملفات <code>.js</code> عند بدء التطبيق وتظهر أيقوناتها في منصة الأدوات (Dock).</td></tr>
    <tr><td><code>~/.config/minicut/scripts/</code></td><td>التشغيل عند الطلب</td><td>تظهر ملفات <code>.js</code> مباشرة في قائمة <strong>المعدِّلات (Modifiers)</strong>.</td></tr>
    <tr><td><code>~/.config/minicut/shaders/gl/</code></td><td>مظللات GL المخصصة</td><td>مظللات الرؤوس والبكسلات لـ OpenGL.</td></tr>
    <tr><td><code>~/.config/minicut/shaders/vulkan/</code></td><td>مظللات Vulkan المخصصة</td><td>مظللات GLSL لمحرك Vulkan.</td></tr>
    <tr><td><code>~/.config/minicut/icons/</code></td><td>أيقونات الأدوات</td><td>ملفات أيقونات PNG مطابقة لاسم السكربت.</td></tr>
</tbody>
</table>

<h2>2. بنية سكربت JavaScript</h2>
<pre>var targetTrack = 0;
var clipDuration = 4.0;
var mediaPath = "tests/assets/test_sample.png";

function placeMedia() {{
    minicut.push_undo();
    var playhead = timeline.get_playhead();
    var cid = timeline.place_clip(mediaPath, targetTrack, playhead, clipDuration);
    if (cid && cid.length > 0) {{
        clip.set_scale(cid, 1.25);
        timeline.add_keyframe(cid, "opacity", 0.0, 0.0);
        timeline.add_keyframe(cid, "opacity", 1.0, 1.0);
        minicut.set_status("تم إدراج المقطع: " + cid);
    }}
}}

function browseFile() {{
    gui.file_dialog("اختر ملفاً (JS)", function(selected) {{
        if (selected && selected.length > 0) {{
            mediaPath = selected;
            minicut.set_status("تم اختيار: " + selected);
        }}
    }});
}}

plugin.init_panel = function() {{
    gui.text_colored(0.24, 0.68, 0.91, 1.0, "أداة إدراج الوسائط (JS)");
    gui.separator();
    targetTrack = gui.slider_int("رقم المسار", targetTrack, 0, 4);
    clipDuration = gui.slider_float("المدة (ث)", clipDuration, 0.5, 30.0);
    gui.button("تصفح الملفات...", browseFile);
    gui.button("إدراج في المخطط الزمني", placeMedia);
}};</pre>

<h2>3. الوحدات البرمجية المتوفرة</h2>
<p>توفر بيئة JavaScript كافة الوحدات البرمجية: <code>project</code> و<code>timeline</code> و<code>clip</code> و<code>gui</code> و<code>net</code> و<code>minicut</code> و<code>export</code>.</p>

<h3>3.1. تحويلات وخصائص المقاطع (<code>clip</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">الدالة</th><th style="width: 22%;">القيمة المرجعة</th><th style="width: 40%;">الوصف</th></tr></thead>
<tbody>
    <tr><td><code>clip.get_position(id)</code></td><td><code>array [x, y]</code></td><td>الحصول على إحداثيات (x, y) بالنسبة لمركز الشاشة.</td></tr>
    <tr><td><code>clip.set_position(id, x, y)</code></td><td><code>void</code></td><td>تعيين موقع مركز المقطع.</td></tr>
    <tr><td><code>clip.get_scale(id)</code></td><td><code>number</code></td><td>الحصول على معامل المقياس (1.0 = 100%).</td></tr>
    <tr><td><code>clip.set_scale(id, s)</code></td><td><code>void</code></td><td>تعيين معامل المقياس.</td></tr>
    <tr><td><code>clip.get_opacity(id)</code></td><td><code>number</code></td><td>الحصول على درجة الشفافية [0.0, 1.0].</td></tr>
    <tr><td><code>clip.set_opacity(id, o)</code></td><td><code>void</code></td><td>تعيين درجة الشفافية.</td></tr>
    <tr><td><code>clip.get_volume(id)</code></td><td><code>number</code></td><td>الحصول على مضخم الصوت [0.0, 2.0].</td></tr>
    <tr><td><code>clip.set_volume(id, v)</code></td><td><code>void</code></td><td>تعيين مستوى الصوت.</td></tr>
    <tr><td><code>clip.get_transform(id)</code></td><td><code>object</code></td><td>الحصول على كائن التحويلات <code>{{x, y, scale, rotation}}</code>.</td></tr>
    <tr><td><code>clip.set_transform(id, x, y, s, r)</code></td><td><code>void</code></td><td>تعيين الإزاحة الأفقية والعمودية والمقياس والدوران دفعة واحدة.</td></tr>
    <tr><td><code>clip.reset_transform(id)</code></td><td><code>void</code></td><td>إعادة تعيين التحويل للوضع الافتراضي (x=0, y=0, scale=1.0, rotation=0).</td></tr>
</tbody>
</table>

<h3>3.2. عناصر الواجهة الرسومية (<code>gui</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">الدالة</th><th style="width: 22%;">القيمة المرجعة</th><th style="width: 40%;">الوصف</th></tr></thead>
<tbody>
    <tr><td><code>gui.text(text)</code></td><td><code>void</code></td><td>عرض نص في اللوحة.</td></tr>
    <tr><td><code>gui.text_colored(r, g, b, a, text)</code></td><td><code>void</code></td><td>عرض نص ملون بتنسيق RGBA.</td></tr>
    <tr><td><code>gui.button(label, [callback])</code></td><td><code>boolean</code></td><td>إنشاء زر وتنفيذ دالة عند الضغط عليه.</td></tr>
    <tr><td><code>gui.slider_float(label, val, min, max)</code></td><td><code>number</code></td><td>شريط تمرير للأرقام العشرية.</td></tr>
    <tr><td><code>gui.slider_int(label, val, min, max)</code></td><td><code>integer</code></td><td>شريط تمرير للأرقام الصحيحة.</td></tr>
    <tr><td><code>gui.input_text(label, text)</code></td><td><code>string</code></td><td>حقل إدخال نصي.</td></tr>
    <tr><td><code>gui.file_dialog(title, cb, [filter])</code></td><td><code>void</code></td><td>فتح نافذة حوار اختيار الملفات.</td></tr>
    <tr><td><code>gui.popout_preview(popped)</code></td><td><code>void</code></td><td>فصل نافذة المعاينة كنافذة عائمة مستقلة (true) أو إرساؤها (false).</td></tr>
    <tr><td><code>gui.is_preview_popped_out()</code></td><td><code>boolean</code></td><td>معرفة ما إذا كانت نافذة المعاينة عائمة حالياً.</td></tr>
    <tr><td><code>gui.set_preview_aspect_mode(mode)</code></td><td><code>void</code></td><td>تعيين نسبة أبعاد المعاينة (0: تلقائي، 1: 16:9، 2: 9:16، 3: 4:3، 4: 1:1، 5: 21:9، 6: 4:5).</td></tr>
    <tr><td><code>gui.get_preview_aspect_mode()</code></td><td><code>number</code></td><td>الحصول على مؤشر نسبة الأبعاد الحالي (0-6).</td></tr>
    <tr><td><code>gui.set_transform_tool(tool)</code></td><td><code>void</code></td><td>تعيين أداة التحويل النشطة ("select", "translate", "scale", "rotate").</td></tr>
    <tr><td><code>gui.get_transform_tool()</code></td><td><code>string</code></td><td>الحصول على اسم أداة التحويل النشطة للمعاينة.</td></tr>
</tbody>
</table>

<h3>3.3. أتمتة التصدير ومتابعة التقدم (<code>export</code>)</h3>
<table>
<thead><tr><th style="width: 38%;">الدالة</th><th style="width: 22%;">القيمة المرجعة</th><th style="width: 40%;">الوصف</th></tr></thead>
<tbody>
    <tr><td><code>export.get_audio_progress()</code></td><td><code>number</code></td><td>نسبة تقدم معالجة ومزج الصوت المجسم والإطارات المفتاحية (0.0 إلى 1.0).</td></tr>
    <tr><td><code>export.get_progress()</code></td><td><code>number</code></td><td>نسبة التقدم الإجمالية لعملية التصدير (0.0 إلى 1.0).</td></tr>
    <tr><td><code>export.is_exporting()</code></td><td><code>boolean</code></td><td>معرفة ما إذا كانت عملية التصدير والترميز قيد التشغيل حالياً.</td></tr>
    <tr><td><code>export.get_status()</code></td><td><code>string</code></td><td>وصف نصي لحالة خط أنابيب التصدير الحالية.</td></tr>
</tbody>
</table>

<div class="footer">
    minicut v1.5.0 • محرر فيديو خفيف ومتعدد المسارات لنظام لينكس • تم إنشاء الدليل تلقائياً
</div>
</body>
</html>"""

def convert_html_to_pdf(html_path, out_dir):
    cmd = [
        "libreoffice",
        "--headless",
        "--convert-to",
        "pdf",
        html_path,
        "--outdir",
        out_dir
    ]
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0:
        print(f"Error converting {html_path} to PDF: {res.stderr}")
        return False
    return True

def main():
    docs_dir = "/home/kaiju/.local/bin/docs"
    os.makedirs(docs_dir, exist_ok=True)

    targets = [
        ("minicut_lua_api.html", "minicut_lua_api.pdf", generate_lua_en_html()),
        ("minicut_lua_api_ja.html", "minicut_lua_api_ja.pdf", generate_lua_ja_html()),
        ("minicut_lua_api_ko.html", "minicut_lua_api_ko.pdf", generate_lua_ko_html()),
        ("minicut_lua_api_uk.html", "minicut_lua_api_uk.pdf", generate_lua_uk_html()),
        ("minicut_lua_api_ar.html", "minicut_lua_api_ar.pdf", generate_lua_ar_html()),

        ("minicut_js_api.html", "minicut_js_api.pdf", generate_js_en_html()),
        ("minicut_js_api_ja.html", "minicut_js_api_ja.pdf", generate_js_ja_html()),
        ("minicut_js_api_ko.html", "minicut_js_api_ko.pdf", generate_js_ko_html()),
        ("minicut_js_api_uk.html", "minicut_js_api_uk.pdf", generate_js_uk_html()),
        ("minicut_js_api_ar.html", "minicut_js_api_ar.pdf", generate_js_ar_html()),
    ]

    for html_name, pdf_name, content in targets:
        html_path = os.path.join(docs_dir, html_name)
        with open(html_path, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"Generated HTML: {html_path}")

        ok = convert_html_to_pdf(html_path, docs_dir)
        pdf_path = os.path.join(docs_dir, pdf_name)
        if ok and os.path.exists(pdf_path):
            print(f"Successfully generated PDF: {pdf_path} ({os.path.getsize(pdf_path)} bytes)")
        else:
            print(f"Failed to generate PDF for {pdf_name}")
            sys.exit(1)

    dest_dirs = [
        os.path.expanduser("~/.config/minicut/docs"),
        "/home/kaiju/Git/minicut/docs",
        "/home/kaiju/Projects/minicut/docs"
    ]

    for dest in dest_dirs:
        os.makedirs(dest, exist_ok=True)
        for _, pdf_name, _ in targets:
            src_pdf = os.path.join(docs_dir, pdf_name)
            dst_pdf = os.path.join(dest, pdf_name)
            shutil.copy2(src_pdf, dst_pdf)
            print(f"Synced {pdf_name} -> {dst_pdf}")

    print(f"\nAll {len(targets)} API reference manuals successfully generated and deployed!")

if __name__ == "__main__":
    main()
