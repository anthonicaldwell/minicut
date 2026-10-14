#!/usr/bin/env python3
"""
minicut Documentation Generator
Generates comprehensive English and Japanese API reference manuals for both
Lua and JavaScript as HTML, Markdown, and PDF.
Outputs:
  - minicut_lua_api.pdf (English Lua API)
  - minicut_lua_api_ja.pdf (Japanese Lua API)
  - minicut_js_api.pdf (English JavaScript API)
  - minicut_js_api_ja.pdf (Japanese JavaScript API)
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
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Noto Sans CJK JP", "Droid Sans Fallback", "DejaVu Sans", "Helvetica Neue", Arial, sans-serif;
    color: #232629;
    background: #ffffff;
    line-height: 1.55;
    font-size: 10pt;
    margin: 0;
    padding: 0;
}
.cover {
    text-align: center;
    padding: 50px 0 30px 0;
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
.callout {
    background-color: #e8f0fe;
    border-left: 4px solid #1a73e8;
    padding: 10px 14px;
    margin: 12px 0;
    border-radius: 0 4px 4px 0;
}
.callout-title {
    font-weight: bold;
    color: #1a73e8;
    margin-bottom: 4px;
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
    return """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>minicut Lua Scripting & Plugin API Reference Manual</title>
<style>" + CSS_TEMPLATE + "</style>
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
    <strong>minicut</strong> features a fully embedded, high-performance <strong>LuaJIT</strong> scripting engine. minicut enables automation, timeline editing, clip keyframing, and custom interactive dockable tool panels.
</p>

<h3>Script Directory Locations</h3>
<p>Scripts and icons are discovered in the user's configuration directory:</p>
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
        <td>Scripts appear in the top-level <strong>Scripts</strong> menu on the main menu bar. Clicking runs the script and opens its tool panel.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/icons/</code></td>
        <td>Dock Icon Asset</td>
        <td>32x32 PNG icon images matching script stems (e.g., <code>quick_placement.png</code> for <code>quick_placement.lua</code>).</td>
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

-- Timeline and clip manipulation logic
function execute_placement()
    minicut.push_undo()
    local path = "/path/to/media.mp4"
    local asset_id = project.import_video(path)
    local clip_id = timeline.place_video(target_track, asset_id, timeline.get_playhead(), 5.0)
    if clip_id ~= "" then
        clip.set_scale(clip_id, clip_scale)
        timeline.add_keyframe(clip_id, "opacity", 0.0, 0.0)
        timeline.add_keyframe(clip_id, "opacity", 1.0, 1.0)
        minicut.set_status("Placed media clip on track " .. target_track)
    end
end

-- GUI panel declaration
function plugin:init_panel()
    gui.text("Media Placement Tool")
    gui.separator()
    target_track = gui.slider_int("Track Lane", target_track, 0, 4)
    clip_scale = gui.slider_float("Initial Scale", clip_scale, 0.1, 5.0)
    
    if gui.button("Place Clip At Playhead", execute_placement) then
        -- Optional inline handling
    end
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
    <tr><td><code>project.import_video(path)</code></td><td><code>string (asset_id)</code></td><td>Imports video into the project media pool.</td></tr>
    <tr><td><code>project.import_audio(path)</code></td><td><code>string (asset_id)</code></td><td>Imports audio into the project media pool.</td></tr>
    <tr><td><code>project.import_image(path, [dur])</code></td><td><code>string (asset_id)</code></td><td>Imports image into the project media pool (default: 5.0s).</td></tr>
    <tr><td><code>assets.create_text(text, [opts])</code></td><td><code>string (asset_id)</code></td><td>Generates FreeType text asset with font, outline, and alignment.</td></tr>
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
    <tr><td><code>net.download(url, dest)</code></td><td><code>boolean</code></td><td>Downloads remote asset directly to local disk.</td></tr>
    <tr><td><code>net.upload(url, path)</code></td><td><code>table</code></td><td>HTTP POST/upload local file binary stream.</td></tr>
    <tr><td><code>net.request(options)</code></td><td><code>table</code></td><td>Custom HTTP request (url, method, headers, timeout).</td></tr>
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
    <tr><td><code>timeline.add_track(type, [name])</code></td><td><code>integer</code></td><td>Adds a track ("video" or "audio") with optional name.</td></tr>
    <tr><td><code>timeline.place_video(track, id, start, dur)</code></td><td><code>string</code></td><td>Places video clip on track and returns clip ID.</td></tr>
    <tr><td><code>timeline.place_audio(track, id, start, dur)</code></td><td><code>string</code></td><td>Places audio clip on track and returns clip ID.</td></tr>
    <tr><td><code>timeline.place_image(track, id, start, dur)</code></td><td><code>string</code></td><td>Places image clip on track and returns clip ID.</td></tr>
    <tr><td><code>timeline.place_text_clip(track, text, start, dur)</code></td><td><code>string</code></td><td>Generates and places text clip on track.</td></tr>
    <tr><td><code>timeline.delete_clip(clip_id)</code></td><td><code>boolean</code></td><td>Deletes clip and removes its associated keyframes.</td></tr>
    <tr><td><code>timeline.split_clip(clip_id, [t])</code></td><td><code>boolean</code></td><td>Splits clip at time <code>t</code> (defaults to playhead).</td></tr>
    <tr><td><code>timeline.select_clips(ids_table)</code></td><td><code>void</code></td><td>Selects multiple clips simultaneously.</td></tr>
    <tr><td><code>timeline.get_selected_clips()</code></td><td><code>table</code></td><td>Returns array of selected clip IDs.</td></tr>
    <tr><td><code>timeline.clear_selection()</code></td><td><code>void</code></td><td>Clears timeline selection.</td></tr>
    <tr><td><code>timeline.move_track_up(idx)</code></td><td><code>void</code></td><td>Moves track up in stack order.</td></tr>
    <tr><td><code>timeline.move_track_down(idx)</code></td><td><code>void</code></td><td>Moves track down in stack order.</td></tr>
    <tr><td><code>timeline.set_track_name(idx, name)</code></td><td><code>void</code></td><td>Renames track at index.</td></tr>
    <tr><td><code>timeline.get_track_name(idx)</code></td><td><code>string</code></td><td>Gets track name at index.</td></tr>
    <tr><td><code>timeline.add_keyframe(id, prop, t, val)</code></td><td><code>void</code></td><td>Adds animation keyframe (opacity, volume, pos_x, etc.).</td></tr>
    <tr><td><code>timeline.remove_keyframe(id, prop, t)</code></td><td><code>void</code></td><td>Removes keyframe near timestamp <code>t</code>.</td></tr>
    <tr><td><code>timeline.evaluate_keyframe(id, prop, t, [def])</code></td><td><code>number</code></td><td>Returns linearly interpolated parameter value.</td></tr>
</tbody>
</table>

<h3>3.4. Clip Transformations (<code>clip</code>)</h3>
<table>
<thead>
    <tr><th style="width: 38%;">Function</th><th style="width: 22%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr><td><code>clip.get_position(clip_id)</code></td><td><code>number, number</code></td><td>Returns (pos_x, pos_y) relative to canvas center.</td></tr>
    <tr><td><code>clip.set_position(clip_id, x, y)</code></td><td><code>void</code></td><td>Sets center position offset in pixels.</td></tr>
    <tr><td><code>clip.get_scale(clip_id)</code></td><td><code>number</code></td><td>Returns uniform scale multiplier (1.0 = 100%).</td></tr>
    <tr><td><code>clip.set_scale(clip_id, s)</code></td><td><code>void</code></td><td>Sets uniform scale multiplier.</td></tr>
    <tr><td><code>clip.get_rotation(clip_id)</code></td><td><code>number</code></td><td>Returns rotation angle in degrees.</td></tr>
    <tr><td><code>clip.set_rotation(clip_id, r)</code></td><td><code>void</code></td><td>Sets rotation angle in degrees.</td></tr>
    <tr><td><code>clip.get_opacity(clip_id)</code></td><td><code>number</code></td><td>Returns opacity multiplier [0.0, 1.0].</td></tr>
    <tr><td><code>clip.set_opacity(clip_id, o)</code></td><td><code>void</code></td><td>Sets opacity multiplier.</td></tr>
    <tr><td><code>clip.get_volume(clip_id)</code></td><td><code>number</code></td><td>Returns audio volume multiplier [0.0, 2.0].</td></tr>
    <tr><td><code>clip.set_volume(clip_id, v)</code></td><td><code>void</code></td><td>Sets audio volume multiplier.</td></tr>
    <tr><td><code>clip.get_time(clip_id)</code></td><td><code>number, number, number</code></td><td>Returns (start_time, duration, offset).</td></tr>
    <tr><td><code>clip.set_time(clip_id, start, dur, [off])</code></td><td><code>void</code></td><td>Modifies clip timeline timing and media offset.</td></tr>
    <tr><td><code>clip.get_info(clip_id)</code></td><td><code>table</code></td><td>Returns comprehensive metadata dictionary.</td></tr>
</tbody>
</table>

<h3>3.5. GUI & UI Elements (<code>gui</code>)</h3>
<table>
<thead>
    <tr><th style="width: 38%;">Function</th><th style="width: 22%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr><td><code>gui.text(text)</code></td><td><code>void</code></td><td>Displays a standard text string.</td></tr>
    <tr><td><code>gui.text_colored(r, g, b, a, text)</code></td><td><code>void</code></td><td>Displays colored text with RGBA float components.</td></tr>
    <tr><td><code>gui.button(label, [callback])</code></td><td><code>boolean</code></td><td>Renders push button, returns true if clicked.</td></tr>
    <tr><td><code>gui.slider_float(label, val, min, max, [cb])</code></td><td><code>number</code></td><td>Renders horizontal float slider.</td></tr>
    <tr><td><code>gui.slider_int(label, val, min, max, [cb])</code></td><td><code>integer</code></td><td>Renders integer slider.</td></tr>
    <tr><td><code>gui.drag_float(label, val, [spd, min, max, cb])</code></td><td><code>number</code></td><td>Renders click-and-drag float value editor.</td></tr>
    <tr><td><code>gui.input_text(label, text, [cb])</code></td><td><code>string</code></td><td>Single-line text entry input box.</td></tr>
    <tr><td><code>gui.checkbox(label, state, [cb])</code></td><td><code>boolean</code></td><td>Toggle checkbox.</td></tr>
    <tr><td><code>gui.same_line()</code></td><td><code>void</code></td><td>Positions next item on the same line horizontally.</td></tr>
    <tr><td><code>gui.separator()</code></td><td><code>void</code></td><td>Draws horizontal divider rule.</td></tr>
    <tr><td><code>gui.file_dialog(options)</code></td><td><code>void</code></td><td>Opens file dialog matching user settings (native or built-in).</td></tr>
</tbody>
</table>

<h3>3.6. Engine Utilities (<code>minicut</code>)</h3>
<table>
<thead>
    <tr><th style="width: 35%;">Function</th><th style="width: 25%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr><td><code>minicut.push_undo()</code></td><td><code>void</code></td><td>Pushes current project state onto undo history stack.</td></tr>
    <tr><td><code>minicut.set_status(msg, [dur])</code></td><td><code>void</code></td><td>Displays temporary status toast notification.</td></tr>
    <tr><td><code>minicut.log(msg)</code></td><td><code>void</code></td><td>Outputs debug line to standard console output.</td></tr>
</tbody>
</table>

<div class="footer">
    minicut v1.2.0 • Lightweight Multi-Track Video Editor • Documentation generated automatically
</div>
</body>
</html>"""

def generate_lua_ja_html():
    return """<!DOCTYPE html>
<html lang="ja">
<head>
<meta charset="UTF-8">
<title>minicut Lua スクリプティング & プラグイン API リファレンスマニュアル</title>
<style>" + CSS_TEMPLATE + "</style>
</head>
<body>
<div class="cover">
    <h1>minicut 動画編集エディタ</h1>
    <div class="subtitle">組み込み LuaJIT & プラグイン API リファレンスマニュアル</div>
    <div>
        <span class="badge">LuaJIT 5.1 ランタイム</span>
        <span class="badge">ドッキング可能ツールパネル</span>
        <span class="badge">ツールシェルフ・ドック</span>
        <span class="badge">ネットワークライブラリ (libcurl)</span>
        <span class="badge">FreeType テキスト生成</span>
    </div>
</div>

<h2>1. はじめに & アーキテクチャ</h2>
<p>
    <strong>minicut</strong> は高速な組み込み <strong>LuaJIT</strong> スクリプトエンジンを搭載しています。タイムライン編集、クリップ配置、キーフレームアニメーション、独自のインタラクティブなドッキングツールパネルの構築をすべてスクリプトから自動化できます。
</p>

<h3>スクリプトディレクトリの配置</h3>
<p>スクリプトやアイコンは、ユーザーの設定ディレクトリから自動的に検出・ロードされます：</p>
<table>
<thead>
    <tr><th style="width: 32%;">ディレクトリ</th><th style="width: 28%;">役割 / 実行タイミング</th><th style="width: 40%;">概要</th></tr>
</thead>
<tbody>
    <tr>
        <td><code>~/.config/minicut/autorun/</code></td>
        <td>起動時自動登録</td>
        <td>エディタ起動時に自動実行され、メニューバー下の水平「ドック (Dock)」にアイコンボタンを生成します。</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/scripts/</code></td>
        <td>メニュクリック時実行</td>
        <td>メインメニューバーの <strong>Scripts (スクリプト)</strong> メニューに登録され、クリック時にパネルを開いて実行します。</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/icons/</code></td>
        <td>ドック用アイコン画像</td>
        <td>スクリプト名と同名の 32x32 PNG アイコン画像（例：<code>quick_placement.lua</code> 用の <code>quick_placement.png</code>）。</td>
    </tr>
</tbody>
</table>

<h2>2. プラグインスクリプトの基本構造</h2>
<p>
    minicut のスクリプトは直感的なコールバック構造を持ちます。タイムラインやクリップのロジック関数を記述し、<code>plugin:init_panel()</code> メソッド内で Dear ImGui の UI コントロールを登録します。
</p>

<pre>-- ============================================================================
-- 基本スクリプト例: my_tool.lua
-- ============================================================================

local target_track = 0
local clip_scale = 1.25

-- タイムライン & クリップ操作ロジック
function execute_placement()
    minicut.push_undo()
    local path = "/path/to/media.mp4"
    local asset_id = project.import_video(path)
    local clip_id = timeline.place_video(target_track, asset_id, timeline.get_playhead(), 5.0)
    if clip_id ~= "" then
        clip.set_scale(clip_id, clip_scale)
        timeline.add_keyframe(clip_id, "opacity", 0.0, 0.0)
        timeline.add_keyframe(clip_id, "opacity", 1.0, 1.0)
        minicut.set_status("トラック " .. target_track .. " にメディアを配置しました")
    end
end

-- GUI パネル初期化
function plugin:init_panel()
    gui.text("メディア配置ツール")
    gui.separator()
    target_track = gui.slider_int("対象トラック", target_track, 0, 4)
    clip_scale = gui.slider_float("初期スケール", clip_scale, 0.1, 5.0)
    
    if gui.button("再生ヘッド位置に配置", execute_placement) then
        -- インライン処理も可能
    end
end</pre>

<h2>3. コアモジュールリファレンス</h2>

<h3>3.1. プロジェクト & アセット操作 (<code>project</code> / Global)</h3>
<table>
<thead>
    <tr><th style="width: 35%;">関数</th><th style="width: 25%;">戻り値</th><th style="width: 40%;">説明</th></tr>
</thead>
<tbody>
    <tr><td><code>project_save([path])</code><br><code>project.save([path])</code></td><td><code>boolean, string</code></td><td>プロジェクトを <code>.minicut</code> JSON ファイルとして保存します。</td></tr>
    <tr><td><code>project_load(path)</code><br><code>project.load(path)</code></td><td><code>boolean, string</code></td><td>指定パスからプロジェクトを読み込み、タイムラインを再構築します。</td></tr>
    <tr><td><code>project.get_name()</code></td><td><code>string</code></td><td>現在のプロジェクト名を取得します。</td></tr>
    <tr><td><code>project.set_name(name)</code></td><td><code>void</code></td><td>現在のプロジェクト名を設定します。</td></tr>
    <tr><td><code>project.import_video(path)</code></td><td><code>string (asset_id)</code></td><td>動画ファイルをアセットプールにインポートします。</td></tr>
    <tr><td><code>project.import_audio(path)</code></td><td><code>string (asset_id)</code></td><td>音声ファイルをアセットプールにインポートします。</td></tr>
    <tr><td><code>project.import_image(path, [dur])</code></td><td><code>string (asset_id)</code></td><td>静止画像をアセットプールにインポートします（初期表示時間：5秒）。</td></tr>
    <tr><td><code>assets.create_text(text, [opts])</code></td><td><code>string (asset_id)</code></td><td>FreeType を用いて高品質テキストアセットを生成します。</td></tr>
</tbody>
</table>

<h3>3.2. ネットワーク通信ライブラリ (<code>net</code>)</h3>
<table>
<thead>
    <tr><th style="width: 35%;">関数</th><th style="width: 25%;">戻り値</th><th style="width: 40%;">説明</th></tr>
</thead>
<tbody>
    <tr><td><code>net.get(url)</code></td><td><code>table</code></td><td>HTTP GET リクエストを送信します。<code>{{status, body, ok, headers}}</code> を返します。</td></tr>
    <tr><td><code>net.post(url, body)</code></td><td><code>table</code></td><td>HTTP POST リクエストを文字列または JSON で送信します。</td></tr>
    <tr><td><code>net.download(url, dest)</code></td><td><code>boolean</code></td><td>リモートファイルをローカルディスクに直接ダウンロード保存します。</td></tr>
    <tr><td><code>net.upload(url, path)</code></td><td><code>table</code></td><td>ローカルファイルをリモートサーバへアップロードします。</td></tr>
    <tr><td><code>net.request(options)</code></td><td><code>table</code></td><td>カスタム HTTP リクエスト（メソッド、ヘッダ、タイムアウト指定）。</td></tr>
</tbody>
</table>

<h3>3.3. タイムライン & トラック管理 (<code>timeline</code>)</h3>
<table>
<thead>
    <tr><th style="width: 38%;">関数</th><th style="width: 22%;">戻り値</th><th style="width: 40%;">説明</th></tr>
</thead>
<tbody>
    <tr><td><code>timeline.get_playhead()</code></td><td><code>number</code></td><td>現在の再生ヘッド位置（秒）を取得します。</td></tr>
    <tr><td><code>timeline.set_playhead(t)</code></td><td><code>void</code></td><td>再生ヘッドを指定時刻（秒）へ移動します。</td></tr>
    <tr><td><code>timeline.get_duration()</code></td><td><code>number</code></td><td>タイムライン全体の総再生時間を取得します。</td></tr>
    <tr><td><code>timeline.get_track_count()</code></td><td><code>integer</code></td><td>全トラック数を取得します。</td></tr>
    <tr><td><code>timeline.add_track(type, [name])</code></td><td><code>integer</code></td><td>新規トラック（"video" または "audio"）を追加します。</td></tr>
    <tr><td><code>timeline.place_video(track, id, start, dur)</code></td><td><code>string</code></td><td>動画クリップを配置し、クリップIDを返します。</td></tr>
    <tr><td><code>timeline.place_audio(track, id, start, dur)</code></td><td><code>string</code></td><td>音声クリップを配置し、クリップIDを返します。</td></tr>
    <tr><td><code>timeline.place_image(track, id, start, dur)</code></td><td><code>string</code></td><td>画像クリップを配置し、クリップIDを返します。</td></tr>
    <tr><td><code>timeline.place_text_clip(track, text, start, dur)</code></td><td><code>string</code></td><td>テキストクリップを生成して配置します。</td></tr>
    <tr><td><code>timeline.delete_clip(clip_id)</code></td><td><code>boolean</code></td><td>クリップおよび関連キーフレームを削除します。</td></tr>
    <tr><td><code>timeline.split_clip(clip_id, [t])</code></td><td><code>boolean</code></td><td>指定時刻でクリップを分割します（省略時は再生ヘッド位置）。</td></tr>
    <tr><td><code>timeline.select_clips(ids_table)</code></td><td><code>void</code></td><td>複数クリップを一括選択します。</td></tr>
    <tr><td><code>timeline.get_selected_clips()</code></td><td><code>table</code></td><td>選択中クリップIDの配列を取得します。</td></tr>
    <tr><td><code>timeline.clear_selection()</code></td><td><code>void</code></td><td>クリップの選択をすべて解除します。</td></tr>
    <tr><td><code>timeline.move_track_up(idx)</code></td><td><code>void</code></td><td>トラックの並び順を1つ上に移動します。</td></tr>
    <tr><td><code>timeline.move_track_down(idx)</code></td><td><code>void</code></td><td>トラックの並び順を1つ下に移動します。</td></tr>
    <tr><td><code>timeline.set_track_name(idx, name)</code></td><td><code>void</code></td><td>トラック名を変更します。</td></tr>
    <tr><td><code>timeline.get_track_name(idx)</code></td><td><code>string</code></td><td>トラック名を取得します。</td></tr>
    <tr><td><code>timeline.add_keyframe(id, prop, t, val)</code></td><td><code>void</code></td><td>アニメーションキーフレームを追加します（opacity, volume等）。</td></tr>
    <tr><td><code>timeline.remove_keyframe(id, prop, t)</code></td><td><code>void</code></td><td>指定時刻のキーフレームを削除します。</td></tr>
    <tr><td><code>timeline.evaluate_keyframe(id, prop, t, [def])</code></td><td><code>number</code></td><td>線形補間されたパラメータ値を取得します。</td></tr>
</tbody>
</table>

<h3>3.4. クリップ変形 & 属性操作 (<code>clip</code>)</h3>
<table>
<thead>
    <tr><th style="width: 38%;">関数</th><th style="width: 22%;">戻り値</th><th style="width: 40%;">説明</th></tr>
</thead>
<tbody>
    <tr><td><code>clip.get_position(clip_id)</code></td><td><code>number, number</code></td><td>キャンバス中央を基準とする (x, y) 座標を取得します。</td></tr>
    <tr><td><code>clip.set_position(clip_id, x, y)</code></td><td><code>void</code></td><td>クリップの中心座標を設定します。</td></tr>
    <tr><td><code>clip.get_scale(clip_id)</code></td><td><code>number</code></td><td>スケール倍率（1.0 = 100%）を取得します。</td></tr>
    <tr><td><code>clip.set_scale(clip_id, s)</code></td><td><code>void</code></td><td>スケール倍率を設定します。</td></tr>
    <tr><td><code>clip.get_rotation(clip_id)</code></td><td><code>number</code></td><td>回転角度（度数法）を取得します。</td></tr>
    <tr><td><code>clip.set_rotation(clip_id, r)</code></td><td><code>void</code></td><td>回転角度を設定します。</td></tr>
    <tr><td><code>clip.get_opacity(clip_id)</code></td><td><code>number</code></td><td>不透明度 [0.0, 1.0] を取得します。</td></tr>
    <tr><td><code>clip.set_opacity(clip_id, o)</code></td><td><code>void</code></td><td>不透明度を設定します。</td></tr>
    <tr><td><code>clip.get_volume(clip_id)</code></td><td><code>number</code></td><td>音量倍率 [0.0, 2.0] を取得します。</td></tr>
    <tr><td><code>clip.set_volume(clip_id, v)</code></td><td><code>void</code></td><td>音量倍率を設定します。</td></tr>
    <tr><td><code>clip.get_time(clip_id)</code></td><td><code>number, number, number</code></td><td>(開始時刻, 長さ, オフセット) を取得します。</td></tr>
    <tr><td><code>clip.set_time(clip_id, start, dur, [off])</code></td><td><code>void</code></td><td>タイムライン上の配置タイミングとオフセットを設定します。</td></tr>
    <tr><td><code>clip.get_info(clip_id)</code></td><td><code>table</code></td><td>クリップの詳細プロパティ辞書を取得します。</td></tr>
</tbody>
</table>

<h3>3.5. GUI UI要素 (<code>gui</code>)</h3>
<table>
<thead>
    <tr><th style="width: 38%;">関数</th><th style="width: 22%;">戻り値</th><th style="width: 40%;">説明</th></tr>
</thead>
<tbody>
    <tr><td><code>gui.text(text)</code></td><td><code>void</code></td><td>テキストを表示します。</td></tr>
    <tr><td><code>gui.text_colored(r, g, b, a, text)</code></td><td><code>void</code></td><td>RGBA色付きテキストを表示します。</td></tr>
    <tr><td><code>gui.button(label, [callback])</code></td><td><code>boolean</code></td><td>プッシュボタンを表示し、クリック時にコールバックを実行します。</td></tr>
    <tr><td><code>gui.slider_float(label, val, min, max, [cb])</code></td><td><code>number</code></td><td>浮動小数点スライダーを表示します。</td></tr>
    <tr><td><code>gui.slider_int(label, val, min, max, [cb])</code></td><td><code>integer</code></td><td>整数スライダーを表示します。</td></tr>
    <tr><td><code>gui.drag_float(label, val, [spd, min, max, cb])</code></td><td><code>number</code></td><td>数値ドラッグボックスを表示します。</td></tr>
    <tr><td><code>gui.input_text(label, text, [cb])</code></td><td><code>string</code></td><td>1行テキスト入力欄を表示します。</td></tr>
    <tr><td><code>gui.checkbox(label, state, [cb])</code></td><td><code>boolean</code></td><td>チェックボックスを表示します。</td></tr>
    <tr><td><code>gui.same_line()</code></td><td><code>void</code></td><td>次のウィジェットを同じ行に配置します。</td></tr>
    <tr><td><code>gui.separator()</code></td><td><code>void</code></td><td>水平区切り線を描画します。</td></tr>
    <tr><td><code>gui.file_dialog(options)</code></td><td><code>void</code></td><td>ユーザー設定に基づきファイルダイアログを開きます。</td></tr>
</tbody>
</table>

<h3>3.6. エンジンユーティリティ (<code>minicut</code>)</h3>
<table>
<thead>
    <tr><th style="width: 35%;">関数</th><th style="width: 25%;">戻り値</th><th style="width: 40%;">説明</th></tr>
</thead>
<tbody>
    <tr><td><code>minicut.push_undo()</code></td><td><code>void</code></td><td>元に戻す（Undo）用のスナップショットを記録します。</td></tr>
    <tr><td><code>minicut.set_status(msg, [dur])</code></td><td><code>void</code></td><td>ステータスバーに一時的な通知メッセージを表示します。</td></tr>
    <tr><td><code>minicut.log(msg)</code></td><td><code>void</code></td><td>コンソールへデバッグログを出力します。</td></tr>
</tbody>
</table>

<div class="footer">
    minicut v1.2.0 • 軽量マルチトラック動画編集エディタ • ドキュメント自動生成
</div>
</body>
</html>"""

def generate_js_en_html():
    return """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>minicut JavaScript Scripting & Plugin API Reference Manual</title>
<style>" + CSS_TEMPLATE + "</style>
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
    <strong>minicut</strong> embeds a complete <strong>JavaScript</strong> engine (Duktape 2.7.0) with native UTF-8 and Unicode string support. JavaScript plugins provide full parity with the minicut scripting API, making it easy to create internationalized tools in Japanese, Arabic, and other languages.
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
        <td><code>.js</code> scripts execute on startup and add an icon button on the horizontal Tool Shelf Dock below the menu bar.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/scripts/</code></td>
        <td>Menu Click / On Demand</td>
        <td><code>.js</code> scripts appear in the top-level <strong>Scripts</strong> menu on the main menu bar.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/icons/</code></td>
        <td>Dock Icon Asset</td>
        <td>32x32 PNG icon images matching script stems (e.g., <code>text_animator.png</code> for <code>text_animator.js</code>).</td>
    </tr>
</tbody>
</table>

<h2>2. JavaScript Plugin Architecture</h2>
<pre>// ============================================================================
// Example JavaScript Plugin: custom_animator.js
// ============================================================================

var targetTrack = 0;
var textContent = "日本語テキスト字幕";

function applyAnimation() {
    minicut.push_undo();
    var clipId = timeline.place_text_clip(targetTrack, textContent, timeline.get_playhead(), 5.0);
    if (clipId) {
        clip.set_position(clipId, 0.0, 150.0);
        clip.set_scale(clipId, 1.2);
        timeline.add_keyframe(clipId, "opacity", 0.0, 0.0);
        timeline.add_keyframe(clipId, "opacity", 0.5, 1.0);
        minicut.set_status("Placed text clip: " + textContent);
    }
}

plugin.init_panel = function() {
    gui.text("JavaScript Text Animator");
    gui.separator();
    targetTrack = gui.slider_int("Track Lane", targetTrack, 0, 4);
    textContent = gui.input_text("Caption Text", textContent);
    
    if (gui.button("Place Animated Text", applyAnimation)) {
        // inline logic
    }
};</pre>

<h2>3. Core JavaScript Modules</h2>

<h3>3.1. Project & File Automation (<code>project</code> / Global)</h3>
<table>
<thead>
    <tr><th style="width: 35%;">Function</th><th style="width: 25%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr><td><code>project.save([path])</code><br><code>project_save([path])</code></td><td><code>boolean</code></td><td>Saves current project state to <code>.minicut</code> file.</td></tr>
    <tr><td><code>project.load(path)</code><br><code>project_load(path)</code></td><td><code>boolean</code></td><td>Loads a project from <code>path</code>.</td></tr>
    <tr><td><code>project.get_name()</code></td><td><code>string</code></td><td>Returns the current project name.</td></tr>
    <tr><td><code>project.set_name(name)</code></td><td><code>void</code></td><td>Sets current project name.</td></tr>
    <tr><td><code>project.import_video(path)</code></td><td><code>string</code></td><td>Imports video into the project media asset pool.</td></tr>
    <tr><td><code>project.import_audio(path)</code></td><td><code>string</code></td><td>Imports audio into the project media asset pool.</td></tr>
    <tr><td><code>project.import_image(path)</code></td><td><code>string</code></td><td>Imports static image into media asset pool.</td></tr>
    <tr><td><code>assets.create_text(text, [opts])</code></td><td><code>string</code></td><td>Creates text asset with font, outline, and alignment.</td></tr>
</tbody>
</table>

<h3>3.2. Network & Internet Library (<code>net</code>)</h3>
<table>
<thead>
    <tr><th style="width: 35%;">Function</th><th style="width: 25%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr><td><code>net.get(url)</code></td><td><code>object</code></td><td>HTTP GET request. Returns <code>{{status, body, ok, error}}</code>.</td></tr>
    <tr><td><code>net.post(url, body)</code></td><td><code>object</code></td><td>HTTP POST request with string or JSON data.</td></tr>
    <tr><td><code>net.download(url, dest)</code></td><td><code>boolean</code></td><td>Downloads remote asset directly to file.</td></tr>
    <tr><td><code>net.upload(url, path)</code></td><td><code>object</code></td><td>Uploads file binary stream to remote URL.</td></tr>
    <tr><td><code>net.request(options)</code></td><td><code>object</code></td><td>Custom HTTP request with method, body, timeout.</td></tr>
</tbody>
</table>

<h3>3.3. Timeline & Track Management (<code>timeline</code>)</h3>
<table>
<thead>
    <tr><th style="width: 38%;">Function</th><th style="width: 22%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr><td><code>timeline.get_playhead()</code></td><td><code>number</code></td><td>Current playhead position in seconds.</td></tr>
    <tr><td><code>timeline.set_playhead(t)</code></td><td><code>void</code></td><td>Moves playhead to <code>t</code> seconds.</td></tr>
    <tr><td><code>timeline.get_duration()</code></td><td><code>number</code></td><td>Total duration of timeline in seconds.</td></tr>
    <tr><td><code>timeline.get_track_count()</code></td><td><code>number</code></td><td>Total count of tracks.</td></tr>
    <tr><td><code>timeline.add_track(type, [name])</code></td><td><code>number</code></td><td>Adds a track ("video" or "audio") and returns index.</td></tr>
    <tr><td><code>timeline.place_video(trk, id, start, dur)</code></td><td><code>string</code></td><td>Places video clip and returns clip ID.</td></tr>
    <tr><td><code>timeline.place_audio(trk, id, start, dur)</code></td><td><code>string</code></td><td>Places audio clip and returns clip ID.</td></tr>
    <tr><td><code>timeline.place_image(trk, id, start, dur)</code></td><td><code>string</code></td><td>Places image clip and returns clip ID.</td></tr>
    <tr><td><code>timeline.place_text_clip(trk, text, start, dur)</code></td><td><code>string</code></td><td>Generates and places text clip on track.</td></tr>
    <tr><td><code>timeline.delete_clip(clip_id)</code></td><td><code>boolean</code></td><td>Deletes clip and keyframes.</td></tr>
    <tr><td><code>timeline.split_clip(clip_id, [t])</code></td><td><code>boolean</code></td><td>Splits clip at time <code>t</code> (or playhead).</td></tr>
    <tr><td><code>timeline.select_clips(ids_array)</code></td><td><code>void</code></td><td>Selects multiple clips by ID array.</td></tr>
    <tr><td><code>timeline.get_selected_clips()</code></td><td><code>Array&lt;string&gt;</code></td><td>Returns array of selected clip IDs.</td></tr>
    <tr><td><code>timeline.clear_selection()</code></td><td><code>void</code></td><td>Clears clip selection.</td></tr>
    <tr><td><code>timeline.move_track_up(idx)</code></td><td><code>void</code></td><td>Moves track up in stack order.</td></tr>
    <tr><td><code>timeline.move_track_down(idx)</code></td><td><code>void</code></td><td>Moves track down in stack order.</td></tr>
    <tr><td><code>timeline.set_track_name(idx, name)</code></td><td><code>void</code></td><td>Renames track at index.</td></tr>
    <tr><td><code>timeline.get_track_name(idx)</code></td><td><code>string</code></td><td>Gets track name at index.</td></tr>
    <tr><td><code>timeline.add_keyframe(id, prop, t, val)</code></td><td><code>void</code></td><td>Adds keyframe for property at time <code>t</code>.</td></tr>
    <tr><td><code>timeline.remove_keyframe(id, prop, t)</code></td><td><code>void</code></td><td>Removes keyframe near timestamp <code>t</code>.</td></tr>
    <tr><td><code>timeline.evaluate_keyframe(id, prop, t, [def])</code></td><td><code>number</code></td><td>Calculates interpolated value at time <code>t</code>.</td></tr>
</tbody>
</table>

<h3>3.4. Clip Transformations (<code>clip</code>)</h3>
<table>
<thead>
    <tr><th style="width: 38%;">Function</th><th style="width: 22%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr><td><code>clip.get_position(clip_id)</code></td><td><code>{{x, y}}</code></td><td>Returns center offset object <code>{{x: 0, y: 0}}</code>.</td></tr>
    <tr><td><code>clip.set_position(clip_id, x, y)</code></td><td><code>void</code></td><td>Sets center position offset in pixels.</td></tr>
    <tr><td><code>clip.get_scale(clip_id)</code></td><td><code>number</code></td><td>Returns scale multiplier (1.0 = 100%).</td></tr>
    <tr><td><code>clip.set_scale(clip_id, s)</code></td><td><code>void</code></td><td>Sets uniform scale multiplier.</td></tr>
    <tr><td><code>clip.get_rotation(clip_id)</code></td><td><code>number</code></td><td>Returns rotation in degrees.</td></tr>
    <tr><td><code>clip.set_rotation(clip_id, r)</code></td><td><code>void</code></td><td>Sets rotation in degrees.</td></tr>
    <tr><td><code>clip.get_opacity(clip_id)</code></td><td><code>number</code></td><td>Returns opacity multiplier [0.0, 1.0].</td></tr>
    <tr><td><code>clip.set_opacity(clip_id, o)</code></td><td><code>void</code></td><td>Sets opacity multiplier.</td></tr>
    <tr><td><code>clip.get_volume(clip_id)</code></td><td><code>number</code></td><td>Returns volume multiplier [0.0, 2.0].</td></tr>
    <tr><td><code>clip.set_volume(clip_id, v)</code></td><td><code>void</code></td><td>Sets volume multiplier.</td></tr>
    <tr><td><code>clip.get_time(clip_id)</code></td><td><code>{{start_time, duration, offset}}</code></td><td>Returns timing object.</td></tr>
    <tr><td><code>clip.set_time(clip_id, start, dur, [off])</code></td><td><code>void</code></td><td>Updates clip timing and media offset.</td></tr>
    <tr><td><code>clip.get_info(clip_id)</code></td><td><code>object</code></td><td>Returns comprehensive metadata dictionary.</td></tr>
</tbody>
</table>

<h3>3.5. GUI UI Elements (<code>gui</code>)</h3>
<table>
<thead>
    <tr><th style="width: 38%;">Function</th><th style="width: 22%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr><td><code>gui.text(text)</code></td><td><code>void</code></td><td>Displays text string.</td></tr>
    <tr><td><code>gui.text_colored(r, g, b, a, text)</code></td><td><code>void</code></td><td>Displays RGBA colored text.</td></tr>
    <tr><td><code>gui.button(label, [callback])</code></td><td><code>boolean</code></td><td>Renders push button and calls callback if clicked.</td></tr>
    <tr><td><code>gui.slider_float(label, val, min, max, [cb])</code></td><td><code>number</code></td><td>Renders float slider.</td></tr>
    <tr><td><code>gui.slider_int(label, val, min, max, [cb])</code></td><td><code>number</code></td><td>Renders integer slider.</td></tr>
    <tr><td><code>gui.input_text(label, text, [cb])</code></td><td><code>string</code></td><td>Single-line text entry box.</td></tr>
    <tr><td><code>gui.checkbox(label, state, [cb])</code></td><td><code>boolean</code></td><td>Toggle checkbox.</td></tr>
    <tr><td><code>gui.same_line()</code></td><td><code>void</code></td><td>Places next widget horizontally on same line.</td></tr>
    <tr><td><code>gui.separator()</code></td><td><code>void</code></td><td>Draws horizontal divider.</td></tr>
    <tr><td><code>gui.file_dialog(options)</code></td><td><code>void</code></td><td>Opens file dialog matching user settings.</td></tr>
</tbody>
</table>

<h3>3.6. Engine Utilities (<code>minicut</code> / <code>console</code>)</h3>
<table>
<thead>
    <tr><th style="width: 35%;">Function</th><th style="width: 25%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr><td><code>minicut.push_undo()</code></td><td><code>void</code></td><td>Pushes undo state.</td></tr>
    <tr><td><code>minicut.set_status(msg, [dur])</code></td><td><code>void</code></td><td>Shows toast notification.</td></tr>
    <tr><td><code>minicut.log(...)</code><br><code>console.log(...)</code></td><td><code>void</code></td><td>Outputs debug log to terminal.</td></tr>
</tbody>
</table>

<div class="footer">
    minicut v1.2.0 • Lightweight Multi-Track Video Editor • Documentation generated automatically
</div>
</body>
</html>"""

def generate_js_ja_html():
    return """<!DOCTYPE html>
<html lang="ja">
<head>
<meta charset="UTF-8">
<title>minicut JavaScript スクリプティング & プラグイン API リファレンスマニュアル</title>
<style>" + CSS_TEMPLATE + "</style>
</head>
<body>
<div class="cover">
    <h1>minicut 動画編集エディタ</h1>
    <div class="subtitle">組み込み JavaScript & プラグイン API リファレンスマニュアル</div>
    <div>
        <span class="badge">Duktape 2.7.0 エンジン</span>
        <span class="badge">完全 UTF-8 / 日本語対応</span>
        <span class="badge">ドッキング可能ツールパネル</span>
        <span class="badge">ツールシェルフ・ドック</span>
        <span class="badge">ネットワークライブラリ (libcurl)</span>
    </div>
</div>

<h2>1. はじめに & JavaScript アーキテクチャ</h2>
<p>
    <strong>minicut</strong> は完全な <strong>JavaScript</strong> エンジン（Duktape 2.7.0）を内蔵しており、標準で完全な UTF-8 / Unicode 文字列操作をサポートしています。日本語の文字化けやバイト境界の問題がなく、国際化されたツールや自動化スクリプトをスムーズに開発できます。
</p>

<h3>スクリプトディレクトリの配置</h3>
<table>
<thead>
    <tr><th style="width: 32%;">ディレクトリ</th><th style="width: 28%;">役割 / 実行タイミング</th><th style="width: 40%;">概要</th></tr>
</thead>
<tbody>
    <tr>
        <td><code>~/.config/minicut/autorun/</code></td>
        <td>起動時自動登録</td>
        <td>起動時に自動実行され、メニューバー下の「ドック (Dock)」にアイコンボタンを登録します（<code>.js</code> ファイル対応）。</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/scripts/</code></td>
        <td>メニュークリック時実行</td>
        <td>メインメニューバーの <strong>Scripts (スクリプト)</strong> メニューに登録され、クリック時にパネルを開いて実行します。</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/icons/</code></td>
        <td>ドック用アイコン画像</td>
        <td>スクリプト名と同名の 32x32 PNG アイコン画像（例：<code>text_animator.js</code> 用の <code>text_animator.png</code>）。</td>
    </tr>
</tbody>
</table>

<h2>2. JavaScript プラグインの基本構造</h2>
<pre>// ============================================================================
// JavaScript プラグインの基本例: custom_animator.js
// ============================================================================

var targetTrack = 0;
var textContent = "日本語テキスト字幕";

function applyAnimation() {
    minicut.push_undo();
    var clipId = timeline.place_text_clip(targetTrack, textContent, timeline.get_playhead(), 5.0);
    if (clipId) {
        clip.set_position(clipId, 0.0, 150.0);
        clip.set_scale(clipId, 1.2);
        timeline.add_keyframe(clipId, "opacity", 0.0, 0.0);
        timeline.add_keyframe(clipId, "opacity", 0.5, 1.0);
        minicut.set_status("字幕クリップを配置しました: " + textContent);
    }
}

plugin.init_panel = function() {
    gui.text("JavaScript テキストアニメーションツール");
    gui.separator();
    targetTrack = gui.slider_int("対象トラック", targetTrack, 0, 4);
    textContent = gui.input_text("字幕テキスト", textContent);
    
    if (gui.button("アニメーションテキストを配置", applyAnimation)) {
        // インライン処理
    }
};</pre>

<h2>3. コアモジュールリファレンス</h2>

<h3>3.1. プロジェクト & アセット操作 (<code>project</code> / Global)</h3>
<table>
<thead>
    <tr><th style="width: 35%;">関数</th><th style="width: 25%;">戻り値</th><th style="width: 40%;">説明</th></tr>
</thead>
<tbody>
    <tr><td><code>project.save([path])</code><br><code>project_save([path])</code></td><td><code>boolean</code></td><td>プロジェクトを <code>.minicut</code> ファイルとして保存します。</td></tr>
    <tr><td><code>project.load(path)</code><br><code>project_load(path)</code></td><td><code>boolean</code></td><td>指定パスからプロジェクトを読み込みます。</td></tr>
    <tr><td><code>project.get_name()</code></td><td><code>string</code></td><td>プロジェクト名を取得します。</td></tr>
    <tr><td><code>project.set_name(name)</code></td><td><code>void</code></td><td>プロジェクト名を設定します。</td></tr>
    <tr><td><code>project.import_video(path)</code></td><td><code>string</code></td><td>動画ファイルをアセットプールへインポートします。</td></tr>
    <tr><td><code>project.import_audio(path)</code></td><td><code>string</code></td><td>音声ファイルをアセットプールへインポートします。</td></tr>
    <tr><td><code>project.import_image(path)</code></td><td><code>string</code></td><td>静止画像ファイルをアセットプールへインポートします。</td></tr>
    <tr><td><code>assets.create_text(text, [opts])</code></td><td><code>string</code></td><td>日本語や各言語のテキストアセットを生成します。</td></tr>
</tbody>
</table>

<h3>3.2. ネットワーク通信ライブラリ (<code>net</code>)</h3>
<table>
<thead>
    <tr><th style="width: 35%;">関数</th><th style="width: 25%;">戻り値</th><th style="width: 40%;">説明</th></tr>
</thead>
<tbody>
    <tr><td><code>net.get(url)</code></td><td><code>object</code></td><td>HTTP GET リクエスト。<code>{{status, body, ok, error}}</code> を返します。</td></tr>
    <tr><td><code>net.post(url, body)</code></td><td><code>object</code></td><td>HTTP POST リクエスト（文字列または JSON）。</td></tr>
    <tr><td><code>net.download(url, dest)</code></td><td><code>boolean</code></td><td>ファイルを直接ローカルディスクへダウンロードします。</td></tr>
    <tr><td><code>net.upload(url, path)</code></td><td><code>object</code></td><td>ローカルファイルをサーバへアップロードします。</td></tr>
    <tr><td><code>net.request(options)</code></td><td><code>object</code></td><td>カスタム HTTP リクエスト。</td></tr>
</tbody>
</table>

<h3>3.3. タイムライン & トラック管理 (<code>timeline</code>)</h3>
<table>
<thead>
    <tr><th style="width: 38%;">関数</th><th style="width: 22%;">戻り値</th><th style="width: 40%;">説明</th></tr>
</thead>
<tbody>
    <tr><td><code>timeline.get_playhead()</code></td><td><code>number</code></td><td>現在の再生ヘッド位置（秒）を取得します。</td></tr>
    <tr><td><code>timeline.set_playhead(t)</code></td><td><code>void</code></td><td>再生ヘッドを指定時刻へ移動します。</td></tr>
    <tr><td><code>timeline.get_duration()</code></td><td><code>number</code></td><td>タイムラインの総再生時間を取得します。</td></tr>
    <tr><td><code>timeline.get_track_count()</code></td><td><code>number</code></td><td>トラックの総数を取得します。</td></tr>
    <tr><td><code>timeline.add_track(type, [name])</code></td><td><code>number</code></td><td>新規トラックを追加し、トラック番号を返します。</td></tr>
    <tr><td><code>timeline.place_video(trk, id, start, dur)</code></td><td><code>string</code></td><td>動画クリップを配置し、クリップIDを返します。</td></tr>
    <tr><td><code>timeline.place_audio(trk, id, start, dur)</code></td><td><code>string</code></td><td>音声クリップを配置し、クリップIDを返します。</td></tr>
    <tr><td><code>timeline.place_image(trk, id, start, dur)</code></td><td><code>string</code></td><td>画像クリップを配置し、クリップIDを返します。</td></tr>
    <tr><td><code>timeline.place_text_clip(trk, text, start, dur)</code></td><td><code>string</code></td><td>テキストクリップを生成して配置します。</td></tr>
    <tr><td><code>timeline.delete_clip(clip_id)</code></td><td><code>boolean</code></td><td>クリップとキーフレームを削除します。</td></tr>
    <tr><td><code>timeline.split_clip(clip_id, [t])</code></td><td><code>boolean</code></td><td>指定時刻でクリップを分割します。</td></tr>
    <tr><td><code>timeline.select_clips(ids_array)</code></td><td><code>void</code></td><td>配列で指定した複数クリップを一括選択します。</td></tr>
    <tr><td><code>timeline.get_selected_clips()</code></td><td><code>Array&lt;string&gt;</code></td><td>選択中のクリップID配列を取得します。</td></tr>
    <tr><td><code>timeline.clear_selection()</code></td><td><code>void</code></td><td>クリップ選択を全解除します。</td></tr>
    <tr><td><code>timeline.move_track_up(idx)</code></td><td><code>void</code></td><td>トラックを1つ上に移動します。</td></tr>
    <tr><td><code>timeline.move_track_down(idx)</code></td><td><code>void</code></td><td>トラックを1つ下に移動します。</td></tr>
    <tr><td><code>timeline.set_track_name(idx, name)</code></td><td><code>void</code></td><td>トラック名を変更します。</td></tr>
    <tr><td><code>timeline.get_track_name(idx)</code></td><td><code>string</code></td><td>トラック名を取得します。</td></tr>
    <tr><td><code>timeline.add_keyframe(id, prop, t, val)</code></td><td><code>void</code></td><td>キーフレームアニメーションを追加します。</td></tr>
    <tr><td><code>timeline.remove_keyframe(id, prop, t)</code></td><td><code>void</code></td><td>指定時刻のキーフレームを削除します。</td></tr>
    <tr><td><code>timeline.evaluate_keyframe(id, prop, t, [def])</code></td><td><code>number</code></td><td>補間計算されたパラメータ値を取得します。</td></tr>
</tbody>
</table>

<h3>3.4. クリップ変形 & 属性操作 (<code>clip</code>)</h3>
<table>
<thead>
    <tr><th style="width: 38%;">関数</th><th style="width: 22%;">戻り値</th><th style="width: 40%;">説明</th></tr>
</thead>
<tbody>
    <tr><td><code>clip.get_position(clip_id)</code></td><td><code>{{x, y}}</code></td><td>中心座標オブジェクト <code>{{x: 0, y: 0}}</code> を返します。</td></tr>
    <tr><td><code>clip.set_position(clip_id, x, y)</code></td><td><code>void</code></td><td>クリップの中心座標を設定します。</td></tr>
    <tr><td><code>clip.get_scale(clip_id)</code></td><td><code>number</code></td><td>スケール倍率（1.0 = 100%）を取得します。</td></tr>
    <tr><td><code>clip.set_scale(clip_id, s)</code></td><td><code>void</code></td><td>スケール倍率を設定します。</td></tr>
    <tr><td><code>clip.get_rotation(clip_id)</code></td><td><code>number</code></td><td>回転角度（度数法）を取得します。</td></tr>
    <tr><td><code>clip.set_rotation(clip_id, r)</code></td><td><code>void</code></td><td>回転角度を設定します。</td></tr>
    <tr><td><code>clip.get_opacity(clip_id)</code></td><td><code>number</code></td><td>不透明度 [0.0, 1.0] を取得します。</td></tr>
    <tr><td><code>clip.set_opacity(clip_id, o)</code></td><td><code>void</code></td><td>不透明度を設定します。</td></tr>
    <tr><td><code>clip.get_volume(clip_id)</code></td><td><code>number</code></td><td>音量倍率 [0.0, 2.0] を取得します。</td></tr>
    <tr><td><code>clip.set_volume(clip_id, v)</code></td><td><code>void</code></td><td>音量倍率を設定します。</td></tr>
    <tr><td><code>clip.get_time(clip_id)</code></td><td><code>{{start_time, duration, offset}}</code></td><td>タイミング情報を取得します。</td></tr>
    <tr><td><code>clip.set_time(clip_id, start, dur, [off])</code></td><td><code>void</code></td><td>タイミングとオフセットを設定します。</td></tr>
    <tr><td><code>clip.get_info(clip_id)</code></td><td><code>object</code></td><td>詳細プロパティオブジェクトを取得します。</td></tr>
</tbody>
</table>

<h3>3.5. GUI UI要素 (<code>gui</code>)</h3>
<table>
<thead>
    <tr><th style="width: 38%;">関数</th><th style="width: 22%;">戻り値</th><th style="width: 40%;">説明</th></tr>
</thead>
<tbody>
    <tr><td><code>gui.text(text)</code></td><td><code>void</code></td><td>テキストを表示します。</td></tr>
    <tr><td><code>gui.text_colored(r, g, b, a, text)</code></td><td><code>void</code></td><td>RGBA色付きテキストを表示します。</td></tr>
    <tr><td><code>gui.button(label, [callback])</code></td><td><code>boolean</code></td><td>ボタンを表示し、クリック時にコールバックを実行します。</td></tr>
    <tr><td><code>gui.slider_float(label, val, min, max, [cb])</code></td><td><code>number</code></td><td>浮動小数点スライダーを表示します。</td></tr>
    <tr><td><code>gui.slider_int(label, val, min, max, [cb])</code></td><td><code>number</code></td><td>整数スライダーを表示します。</td></tr>
    <tr><td><code>gui.input_text(label, text, [cb])</code></td><td><code>string</code></td><td>1行テキスト入力欄を表示します。</td></tr>
    <tr><td><code>gui.checkbox(label, state, [cb])</code></td><td><code>boolean</code></td><td>チェックボックスを表示します。</td></tr>
    <tr><td><code>gui.same_line()</code></td><td><code>void</code></td><td>次のウィジェットを同一行に配置します。</td></tr>
    <tr><td><code>gui.separator()</code></td><td><code>void</code></td><td>水平区切り線を描画します。</td></tr>
    <tr><td><code>gui.file_dialog(options)</code></td><td><code>void</code></td><td>ファイルダイアログを開きます。</td></tr>
</tbody>
</table>

<h3>3.6. エンジンユーティリティ (<code>minicut</code> / <code>console</code>)</h3>
<table>
<thead>
    <tr><th style="width: 35%;">関数</th><th style="width: 25%;">戻り値</th><th style="width: 40%;">説明</th></tr>
</thead>
<tbody>
    <tr><td><code>minicut.push_undo()</code></td><td><code>void</code></td><td>Undo用スナップショットを記録します。</td></tr>
    <tr><td><code>minicut.set_status(msg, [dur])</code></td><td><code>void</code></td><td>トースト通知メッセージを表示します。</td></tr>
    <tr><td><code>minicut.log(...)</code><br><code>console.log(...)</code></td><td><code>void</code></td><td>ターミナルにデバッグログを出力します。</td></tr>
</tbody>
</table>

<div class="footer">
    minicut v1.2.0 • 軽量マルチトラック動画編集エディタ • ドキュメント自動生成
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
        ("minicut_js_api.html", "minicut_js_api.pdf", generate_js_en_html()),
        ("minicut_js_api_ja.html", "minicut_js_api_ja.pdf", generate_js_ja_html()),
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

    # Copy to user config directory and git project repository
    dest_dirs = [
        os.path.expanduser("~/.config/minicut/docs"),
        "/home/kaiju/Projects/minicut/docs"
    ]

    for dest in dest_dirs:
        os.makedirs(dest, exist_ok=True)
        for _, pdf_name, _ in targets:
            src_pdf = os.path.join(docs_dir, pdf_name)
            dst_pdf = os.path.join(dest, pdf_name)
            shutil.copy2(src_pdf, dst_pdf)
            print(f"Synced {pdf_name} -> {dst_pdf}")

    print("\nAll 4 API reference manuals successfully generated and deployed!")

if __name__ == "__main__":
    main()
