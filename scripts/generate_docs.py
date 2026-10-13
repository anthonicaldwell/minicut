#!/usr/bin/env python3
"""
Generates the minicut Lua API Reference manual as HTML, Markdown, and PDF.
"""

import os
import subprocess
import sys

HTML_CONTENT = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>minicut Lua Scripting & Plugin API Reference Manual</title>
<style>
@page {
    size: letter portrait;
    margin: 0.8in;
    @bottom-right {
        content: counter(page);
    }
}
body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "DejaVu Sans", "Helvetica Neue", Arial, sans-serif;
    color: #232629;
    background: #ffffff;
    line-height: 1.55;
    font-size: 10pt;
    margin: 0;
    padding: 0;
}
.cover {
    text-align: center;
    padding: 60px 0 40px 0;
    border-bottom: 3px solid #1a73e8;
    margin-bottom: 30px;
}
.cover h1 {
    font-size: 26pt;
    color: #1a73e8;
    margin: 0 0 10px 0;
    font-weight: 700;
}
.cover .subtitle {
    font-size: 14pt;
    color: #5f6368;
    margin: 0 0 20px 0;
}
.cover .badge {
    display: inline-block;
    background: #e8f0fe;
    color: #1a73e8;
    font-weight: bold;
    padding: 6px 14px;
    border-radius: 16px;
    font-size: 9.5pt;
    margin: 5px;
}
h2 {
    color: #0d47a1;
    font-size: 14pt;
    border-bottom: 2px solid #e0e0e0;
    padding-bottom: 5px;
    margin-top: 32px;
    page-break-after: avoid;
}
h3 {
    color: #1565c0;
    font-size: 11pt;
    margin-top: 20px;
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
    font-size: 9pt;
    page-break-inside: avoid;
}
th {
    background-color: #1a73e8;
    color: #ffffff;
    font-weight: 600;
    text-align: left;
    padding: 8px 10px;
    border: 1px solid #1a73e8;
}
td {
    padding: 7px 10px;
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
    margin-top: 40px;
    border-top: 1px solid #e0e0e0;
    padding-top: 15px;
}
</style>
</head>
<body>

<div class="cover">
    <h1>minicut Video Editor</h1>
    <div class="subtitle">Embedded LuaJIT & Plugin API Reference Manual</div>
    <div>
        <span class="badge">LuaJIT 5.1 Runtime</span>
        <span class="badge">MEL-Style Panels</span>
        <span class="badge">Maya Shelf Dock</span>
        <span class="badge">Built-in Network (libcurl)</span>
    </div>
</div>

<h2>1. Introduction & Architecture</h2>
<p>
    <strong>minicut</strong> features a fully embedded, high-performance <strong>LuaJIT</strong> scripting engine. Similar to Maya's MEL and Python architecture, minicut enables automation, timeline editing, clip keyframing, and custom interactive dockable tool panels.
</p>

<h3>Script Directory Locations</h3>
<p>Scripts and icons are discovered in the user's home configuration directory:</p>
<table>
<thead>
    <tr><th style="width: 32%;">Location</th><th style="width: 28%;">Trigger / Role</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr>
        <td><code>~/.config/minicut/autorun/</code></td>
        <td>Startup / Auto-register</td>
        <td>Scripts execute on startup and generate an icon button on the horizontal Maya Shelf Dock below the main menu bar.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/scripts/</code></td>
        <td>Menu Click / On Demand</td>
        <td>Scripts appear in the top-level <strong>Scripts</strong> menu on the main menu bar. Clicking runs the script and opens its tool panel.</td>
    </tr>
    <tr>
        <td><code>~/.config/minicut/icons/</code></td>
        <td>Shelf Icon Asset</td>
        <td>32x32 PNG icon images matching script stems (e.g., <code>quick_placement.png</code> for <code>quick_placement.lua</code>).</td>
    </tr>
</tbody>
</table>

<div class="callout">
    <div class="callout-title">Maya Shelf Fallback Badges</div>
    If an autorun script does not have a corresponding PNG icon in <code>~/.config/minicut/icons/</code>, minicut automatically creates a sleek 2-letter uppercase badge (e.g., <code>QP</code> for Quick Placement) on the shelf button.
</div>

<h2>2. MEL-Style Script Architecture</h2>
<p>
    Every minicut script follows an intuitive, modular structure. A script defines timeline and clip manipulation callbacks, followed by a <code>plugin:init_panel()</code> method that registers Dear ImGui UI controls.
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
    local clip_id = timeline.place_clip(path, target_track, timeline.get_playhead(), 5.0)
    if clip_id then
        clip.set_scale(clip_id, clip_scale)
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

<p>
    When executed, each script automatically opens an independent, dockable tool panel situated to the right of the preview and timeline panels.
</p>

<h2>3. Core Lua Modules</h2>

<h3>3.1. Project & File Automation (<code>project</code> / Global)</h3>
<table>
<thead>
    <tr><th style="width: 35%;">Function</th><th style="width: 25%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr>
        <td><code>project_save([path])</code><br><code>project.save([path])</code></td>
        <td><code>boolean, string</code></td>
        <td>Saves current project state to a <code>.minicut</code> JSON file. If path is omitted, saves to the current project file path.</td>
    </tr>
    <tr>
        <td><code>project_load(path)</code><br><code>project.load(path)</code></td>
        <td><code>boolean, string</code></td>
        <td>Loads a project from <code>path</code>, updates timeline, resets playhead, and refreshes the audio engine and video preview.</td>
    </tr>
    <tr>
        <td><code>project.get_name()</code></td>
        <td><code>string</code></td>
        <td>Returns the current project name.</td>
    </tr>
    <tr>
        <td><code>project.set_name(name)</code></td>
        <td><code>void</code></td>
        <td>Sets the current project name.</td>
    </tr>
    <tr>
        <td><code>project.import_video(path)</code></td>
        <td><code>string (asset_id)</code></td>
        <td>Imports a video file into the project's media asset pool.</td>
    </tr>
    <tr>
        <td><code>project.import_audio(path)</code></td>
        <td><code>string (asset_id)</code></td>
        <td>Imports an audio file into the project's asset pool.</td>
    </tr>
    <tr>
        <td><code>project.import_image(path, [dur])</code></td>
        <td><code>string (asset_id)</code></td>
        <td>Imports a static image file into the project's asset pool (default duration: 5.0s).</td>
    </tr>
</tbody>
</table>

<h3>3.2. Network & Internet Library (<code>net</code>)</h3>
<p>
    Powered by embedded <strong>libcurl</strong>, the <code>net</code> module enables downloading remote media, uploading exports to web servers, and calling REST APIs.
</p>
<table>
<thead>
    <tr><th style="width: 38%;">Function</th><th style="width: 22%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr>
        <td><code>net.get(url, [opts])</code></td>
        <td><code>table | (nil, err)</code></td>
        <td>Sends an HTTP GET request. Returns table: <code>{ status, ok, body, headers }</code>.</td>
    </tr>
    <tr>
        <td><code>net.post(url, body, [opts])</code></td>
        <td><code>table | (nil, err)</code></td>
        <td>Sends an HTTP POST request with string or key-value table body.</td>
    </tr>
    <tr>
        <td><code>net.download(url, dest, [opts])</code></td>
        <td><code>boolean, string</code></td>
        <td>Streams remote file directly to disk at <code>dest</code> without RAM overhead.</td>
    </tr>
    <tr>
        <td><code>net.upload(url, file, [opts])</code></td>
        <td><code>table | (nil, err)</code></td>
        <td>Uploads local file via multipart/form-data. Supports custom <code>field_name</code> and <code>extra_fields</code>.</td>
    </tr>
    <tr>
        <td><code>net.request(opts)</code></td>
        <td><code>table | (nil, err)</code></td>
        <td>Generic HTTP request with custom method (GET, POST, PUT, DELETE, PATCH).</td>
    </tr>
</tbody>
</table>

<pre>-- Example: Downloading stock music and importing directly to timeline
local ok, path = net.download("https://example.com/soundtrack.mp3", "/tmp/soundtrack.mp3")
if ok then
    local clip_id = timeline.place_clip(path, 1, 0.0, 30.0)
    minicut.set_status("Downloaded and placed soundtrack!")
end</pre>

<h3>3.3. GUI Controls (<code>gui</code>)</h3>
<table>
<thead>
    <tr><th style="width: 38%;">Function</th><th style="width: 22%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr>
        <td><code>gui.button(label, [callback])</code></td>
        <td><code>boolean</code></td>
        <td>Standard push button. Invokes <code>callback</code> when clicked.</td>
    </tr>
    <tr>
        <td><code>gui.small_button(label, [cb])</code></td>
        <td><code>boolean</code></td>
        <td>Compact button without vertical frame padding.</td>
    </tr>
    <tr>
        <td><code>gui.text(text)</code></td>
        <td><code>void</code></td>
        <td>Standard text label.</td>
    </tr>
    <tr>
        <td><code>gui.text_colored(text, r, g, b, [a])</code></td>
        <td><code>void</code></td>
        <td>Colored text label (color components 0.0 to 1.0).</td>
    </tr>
    <tr>
        <td><code>gui.text_disabled(text)</code></td>
        <td><code>void</code></td>
        <td>Muted gray text label.</td>
    </tr>
    <tr>
        <td><code>gui.separator()</code></td>
        <td><code>void</code></td>
        <td>Horizontal dividing line.</td>
    </tr>
    <tr>
        <td><code>gui.same_line([offset], [spacing])</code></td>
        <td><code>void</code></td>
        <td>Places the next control on the same horizontal line.</td>
    </tr>
    <tr>
        <td><code>gui.slider_float(lbl, val, min, max)</code></td>
        <td><code>number</code></td>
        <td>Interactive floating-point horizontal slider.</td>
    </tr>
    <tr>
        <td><code>gui.slider_int(lbl, val, min, max)</code></td>
        <td><code>integer</code></td>
        <td>Interactive integer horizontal slider.</td>
    </tr>
    <tr>
        <td><code>gui.drag_float(lbl, val, [spd, min, max])</code></td>
        <td><code>number</code></td>
        <td>Click-and-drag floating-point number field.</td>
    </tr>
    <tr>
        <td><code>gui.checkbox(lbl, val)</code></td>
        <td><code>boolean</code></td>
        <td>Toggle checkbox.</td>
    </tr>
    <tr>
        <td><code>gui.input_text(lbl, val)</code></td>
        <td><code>string</code></td>
        <td>Single-line text input field.</td>
    </tr>
    <tr>
        <td><code>gui.progress_bar(fraction, [text])</code></td>
        <td><code>void</code></td>
        <td>Progress bar (0.0 to 1.0).</td>
    </tr>
    <tr>
        <td><code>gui.tooltip(text)</code></td>
        <td><code>void</code></td>
        <td>Displays hover tooltip over preceding item.</td>
    </tr>
</tbody>
</table>

<h3>3.4. Timeline & Tracks (<code>timeline</code>)</h3>
<table>
<thead>
    <tr><th style="width: 40%;">Function</th><th style="width: 20%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr>
        <td><code>timeline.place_clip(path, track, t, dur)</code></td>
        <td><code>string (clip_id)</code></td>
        <td>Imports media and places clip onto track lane at timestamp <code>t</code>.</td>
    </tr>
    <tr>
        <td><code>timeline.get_playhead()</code></td>
        <td><code>number</code></td>
        <td>Returns current playhead time in seconds.</td>
    </tr>
    <tr>
        <td><code>timeline.set_playhead(time_sec)</code></td>
        <td><code>void</code></td>
        <td>Seeks playhead to specified time and updates video/audio preview.</td>
    </tr>
    <tr>
        <td><code>timeline.get_duration()</code></td>
        <td><code>number</code></td>
        <td>Returns total project duration in seconds.</td>
    </tr>
    <tr>
        <td><code>timeline.get_track_count()</code></td>
        <td><code>integer</code></td>
        <td>Returns total number of timeline tracks.</td>
    </tr>
    <tr>
        <td><code>timeline.get_track_depth(track_idx)</code></td>
        <td><code>integer</code></td>
        <td>Returns Z-depth compositing order for track (0=back, 100=front).</td>
    </tr>
    <tr>
        <td><code>timeline.set_track_depth(track_idx, z)</code></td>
        <td><code>void</code></td>
        <td>Sets Z-depth compositing order for track.</td>
    </tr>
    <tr>
        <td><code>timeline.add_track(track_type)</code></td>
        <td><code>string (id)</code></td>
        <td>Appends a new track (<code>"video"</code> or <code>"audio"</code>).</td>
    </tr>
    <tr>
        <td><code>timeline.delete_clip(clip_id)</code></td>
        <td><code>boolean</code></td>
        <td>Deletes clip from timeline.</td>
    </tr>
    <tr>
        <td><code>timeline.split_clip(clip_id, split_t)</code></td>
        <td><code>boolean</code></td>
        <td>Splits clip into two halves at timestamp <code>split_t</code>.</td>
    </tr>
</tbody>
</table>

<h3>3.5. Clip Transforms & Keyframes (<code>clip</code> & <code>timeline</code>)</h3>
<table>
<thead>
    <tr><th style="width: 42%;">Function</th><th style="width: 18%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr>
        <td><code>clip.get_selected_id()</code></td>
        <td><code>string</code></td>
        <td>Returns ID of currently selected clip.</td>
    </tr>
    <tr>
        <td><code>clip.get_position(clip_id)</code></td>
        <td><code>x, y</code></td>
        <td>Returns clip pixel translation offsets.</td>
    </tr>
    <tr>
        <td><code>clip.set_position(clip_id, x, y)</code></td>
        <td><code>void</code></td>
        <td>Sets clip translation offsets.</td>
    </tr>
    <tr>
        <td><code>clip.get_scale(clip_id)</code></td>
        <td><code>number</code></td>
        <td>Returns scale multiplier (default 1.0).</td>
    </tr>
    <tr>
        <td><code>clip.set_scale(clip_id, scale)</code></td>
        <td><code>void</code></td>
        <td>Sets clip scale multiplier.</td>
    </tr>
    <tr>
        <td><code>clip.get_rotation(clip_id)</code></td>
        <td><code>number</code></td>
        <td>Returns rotation angle in degrees.</td>
    </tr>
    <tr>
        <td><code>clip.set_rotation(clip_id, deg)</code></td>
        <td><code>void</code></td>
        <td>Sets rotation angle in degrees.</td>
    </tr>
    <tr>
        <td><code>clip.get_opacity(clip_id)</code></td>
        <td><code>number</code></td>
        <td>Returns opacity (0.0=invisible, 1.0=solid).</td>
    </tr>
    <tr>
        <td><code>clip.set_opacity(clip_id, alpha)</code></td>
        <td><code>void</code></td>
        <td>Sets clip opacity.</td>
    </tr>
    <tr>
        <td><code>clip.get_volume(clip_id)</code></td>
        <td><code>number</code></td>
        <td>Returns audio gain (1.0 = 100%).</td>
    </tr>
    <tr>
        <td><code>clip.set_volume(clip_id, vol)</code></td>
        <td><code>void</code></td>
        <td>Sets audio gain.</td>
    </tr>
    <tr>
        <td><code>timeline.add_keyframe(id, prop, t, val)</code></td>
        <td><code>void</code></td>
        <td>Adds animation keyframe (prop: <code>"pos_x"</code>, <code>"pos_y"</code>, <code>"scale"</code>, <code>"rotation"</code>, <code>"opacity"</code>, <code>"volume"</code>).</td>
    </tr>
    <tr>
        <td><code>timeline.evaluate_keyframe(id, prop, t, def)</code></td>
        <td><code>number</code></td>
        <td>Evaluates interpolated value at timestamp <code>t</code>.</td>
    </tr>
</tbody>
</table>

<h3>3.6. Engine Utilities (<code>minicut</code>)</h3>
<table>
<thead>
    <tr><th style="width: 40%;">Function</th><th style="width: 20%;">Returns</th><th style="width: 40%;">Description</th></tr>
</thead>
<tbody>
    <tr>
        <td><code>minicut.push_undo()</code></td>
        <td><code>void</code></td>
        <td>Pushes the current project snapshot onto the Undo stack.</td>
    </tr>
    <tr>
        <td><code>minicut.set_status(msg, [dur])</code></td>
        <td><code>void</code></td>
        <td>Displays green notification toast in the main menu bar.</td>
    </tr>
    <tr>
        <td><code>minicut.log(message)</code></td>
        <td><code>void</code></td>
        <td>Logs message to the console standard output.</td>
    </tr>
</tbody>
</table>

<h2>4. Comprehensive Plugin Examples</h2>

<h3>Example 1: Keyframe Bouncing Logo Animator</h3>
<pre>-- ~/.config/minicut/autorun/logo_bounce.lua
local bounce_dur = 2.0
local peak_height = -150.0

function apply_bounce_animation()
    local cid = clip.get_selected_id()
    if cid == "" then
        minicut.set_status("Please select a clip first!")
        return
    end

    minicut.push_undo()
    local cur_t = timeline.get_playhead()
    timeline.clear_keyframes(cid, "pos_y")
    
    -- Animate bounce cycle
    timeline.add_keyframe(cid, "pos_y", cur_t, 0.0)
    timeline.add_keyframe(cid, "pos_y", cur_t + (bounce_dur * 0.5), peak_height)
    timeline.add_keyframe(cid, "pos_y", cur_t + bounce_dur, 0.0)
    
    minicut.set_status("Applied bounce keyframes to clip!")
end

function plugin:init_panel()
    gui.text("Bouncing Logo Animator")
    gui.separator()
    bounce_dur = gui.slider_float("Cycle Duration (s)", bounce_dur, 0.2, 5.0)
    peak_height = gui.slider_float("Peak Jump (px)", peak_height, -400.0, 0.0)
    gui.button("Apply Bounce Animation", apply_bounce_animation)
end</pre>

<h3>Example 2: Automated Cloud Project Backup</h3>
<pre>-- ~/.config/minicut/scripts/cloud_backup.lua
local upload_endpoint = "https://httpbin.org/post"

function execute_backup()
    local temp_file = "/tmp/minicut_auto_backup.minicut"
    local save_ok, err = project_save(temp_file)
    if not save_ok then
        minicut.set_status("Failed to save local snapshot: " .. tostring(err))
        return
    end

    minicut.set_status("Uploading project backup...")
    local resp = net.upload(upload_endpoint, temp_file, {
        field_name = "project_file",
        extra_fields = {
            timestamp = tostring(os.time()),
            project_name = project.get_name()
        }
    })

    if resp and resp.ok then
        minicut.set_status("Project successfully backed up to cloud!")
    else
        minicut.set_status("Backup upload failed: " .. tostring(resp and resp.status or "network error"))
    end
end

function plugin:init_panel()
    gui.text("Cloud Project Backup Tool")
    gui.separator()
    upload_endpoint = gui.input_text("Endpoint URL", upload_endpoint)
    gui.button("Backup Project Now", execute_backup)
end</pre>

<div class="footer">
    minicut Video Editor &bull; Embedded LuaJIT &bull; Comprehensive API Reference Manual
</div>

</body>
</html>
"""

MARKDOWN_CONTENT = """# minicut Lua Scripting & Plugin API Reference Manual

**minicut** features an embedded, high-performance **LuaJIT 5.1** scripting runtime with Dear ImGui bindings, timeline/clip positioning, keyframing, project automation, and native network capabilities (`libcurl`).

---

## 1. Directory Structure

Scripts and shelf icons are placed in the user configuration directory:

| Directory | Purpose |
|---|---|
| `~/.config/minicut/autorun/` | Scripts executed at startup. Generates a tool icon button on the **Maya Shelf Dock** directly beneath the menu bar. |
| `~/.config/minicut/scripts/` | Scripts placed in the top-level **Scripts** menu in the main menu bar. |
| `~/.config/minicut/icons/` | 32x32 PNG icons matching script stem (e.g. `quick_placement.png` for `quick_placement.lua`). |
| `~/.config/minicut/docs/` | Contains this manual (`minicut_lua_api.pdf`). |

---

## 2. MEL-Style Plugin Architecture

Every script can define timeline callbacks and register Dear ImGui controls in `plugin:init_panel()`.
When clicked, the script creates an independent, dockable tool panel positioned to the right of the preview and timeline.

```lua
-- GUI state variables
local insert_track = 0
local clip_scale = 1.0

-- Callback action
function logic_callback()
    minicut.push_undo()
    local path = "/path/to/media.mp4"
    local clip_id = timeline.place_clip(path, insert_track, timeline.get_playhead(), 5.0)
    if clip_id then
        clip.set_scale(clip_id, clip_scale)
        minicut.set_status("Media clip placed on track " .. insert_track)
    end
end

-- Panel registration
function plugin:init_panel()
    gui.text("Custom Media Tool")
    gui.separator()
    insert_track = gui.slider_int("Target Track", insert_track, 0, 4)
    clip_scale = gui.slider_float("Initial Scale", clip_scale, 0.1, 4.0)
    gui.button("Execute Placement", logic_callback)
end
```

---

## 3. Core Lua Modules

### 3.1. Project & File Automation (`project` & Global)

| Function | Returns | Description |
|---|---|---|
| `project_save([path])`<br>`project.save([path])` | `boolean, string` | Saves project state to `.minicut` file. |
| `project_load(path)`<br>`project.load(path)` | `boolean, string` | Loads project, updates timeline, and refreshes preview. |
| `project.get_name()` | `string` | Returns project name. |
| `project.set_name(name)` | `void` | Sets project name. |
| `project.import_video(path)` | `string (asset_id)` | Imports video file into pool. |
| `project.import_audio(path)` | `string (asset_id)` | Imports audio file into pool. |
| `project.import_image(path, [dur])` | `string (asset_id)` | Imports image file into pool (default dur: 5.0s). |

### 3.2. Network & Internet Library (`net`)

Powered by `libcurl`:

| Function | Returns | Description |
|---|---|---|
| `net.get(url, [opts])` | `table` | HTTP GET request `{ status, ok, body, headers }`. |
| `net.post(url, body, [opts])` | `table` | HTTP POST request with string or key-value table. |
| `net.download(url, dest, [opts])` | `boolean, string` | Direct-to-disk streaming download. |
| `net.upload(url, file, [opts])` | `table` | Multipart/form-data file upload. |
| `net.request(opts)` | `table` | Custom HTTP request (GET, POST, PUT, DELETE). |

### 3.3. GUI Controls (`gui`)

| Function | Description |
|---|---|
| `gui.button(label, [callback])` | Push button. |
| `gui.small_button(label, [callback])` | Compact push button. |
| `gui.text(text)` | Normal text label. |
| `gui.text_colored(text, r, g, b, [a])` | Colored text label (0.0 to 1.0). |
| `gui.text_disabled(text)` | Disabled / muted text. |
| `gui.separator()` | Horizontal dividing line. |
| `gui.same_line([offset], [spacing])` | Puts next widget on same line. |
| `gui.slider_float(label, val, min, max)` | Float slider. |
| `gui.slider_int(label, val, min, max)` | Integer slider. |
| `gui.drag_float(label, val, [spd, min, max])` | Drag-to-adjust float field. |
| `gui.checkbox(label, val)` | Toggle checkbox. |
| `gui.input_text(label, val)` | Text input box. |
| `gui.progress_bar(fraction, [text])` | Progress bar (0.0 to 1.0). |
| `gui.tooltip(text)` | Hover tooltip. |

### 3.4. Timeline & Tracks (`timeline`)

| Function | Description |
|---|---|
| `timeline.place_clip(path, track, t, dur)` | Imports and places clip on timeline lane. |
| `timeline.get_playhead()` | Returns playhead time in seconds. |
| `timeline.set_playhead(t)` | Seeks playhead and refreshes preview frame. |
| `timeline.get_duration()` | Returns total project duration in seconds. |
| `timeline.get_track_count()` | Returns number of tracks. |
| `timeline.get_track_depth(track_idx)` | Returns Z-depth compositing order (0-100). |
| `timeline.set_track_depth(track_idx, z)` | Sets Z-depth compositing order. |
| `timeline.add_track(track_type)` | Adds new track (`"video"` or `"audio"`). |
| `timeline.delete_clip(clip_id)` | Removes clip from timeline. |
| `timeline.split_clip(clip_id, split_t)` | Splits clip at time. |

### 3.5. Clip Transforms & Keyframes (`clip` & `timeline`)

| Function | Description |
|---|---|
| `clip.get_selected_id()` | Returns ID of active clip. |
| `clip.get_position(clip_id)` / `clip.set_position(clip_id, x, y)` | Translation offsets. |
| `clip.get_scale(clip_id)` / `clip.set_scale(clip_id, s)` | Scale multiplier. |
| `clip.get_rotation(clip_id)` / `clip.set_rotation(clip_id, deg)` | Rotation in degrees. |
| `clip.get_opacity(clip_id)` / `clip.set_opacity(clip_id, a)` | Opacity (0.0 to 1.0). |
| `clip.get_volume(clip_id)` / `clip.set_volume(clip_id, v)` | Audio gain multiplier. |
| `timeline.add_keyframe(clip_id, prop, t, val)` | Adds animation keyframe. |
| `timeline.evaluate_keyframe(clip_id, prop, t, def)` | Evaluates interpolated value. |

### 3.6. Engine Utilities (`minicut`)

| Function | Description |
|---|---|
| `minicut.push_undo()` | Creates an undo snapshot. |
| `minicut.set_status(msg, [dur])` | Shows green toast in menu bar. |
| `minicut.log(message)` | Prints message to console. |
"""

def main():
    docs_dir = "/home/kaiju/.local/bin/docs"
    os.makedirs(docs_dir, exist_ok=True)

    html_path = os.path.join(docs_dir, "minicut_lua_api.html")
    with open(html_path, "w", encoding="utf-8") as f:
        f.write(HTML_CONTENT)
    print(f"Generated HTML documentation: {html_path}")

    md_path = os.path.join(docs_dir, "LUA_API.md")
    with open(md_path, "w", encoding="utf-8") as f:
        f.write(MARKDOWN_CONTENT)
    print(f"Generated Markdown documentation: {md_path}")

    # Compile HTML to PDF via headless LibreOffice
    print("Converting HTML to PDF using LibreOffice...")
    cmd = [
        "libreoffice",
        "--headless",
        "--convert-to",
        "pdf",
        html_path,
        "--outdir",
        docs_dir
    ]
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode == 0:
        pdf_path = os.path.join(docs_dir, "minicut_lua_api.pdf")
        if os.path.exists(pdf_path):
            print(f"Successfully generated PDF manual: {pdf_path} (size: {os.path.getsize(pdf_path)} bytes)")
        else:
            print("PDF generation completed but file not found!")
    else:
        print(f"Error converting to PDF: {res.stderr}")
        sys.exit(1)

if __name__ == "__main__":
    main()
