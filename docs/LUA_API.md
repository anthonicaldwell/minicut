# minicut Lua Scripting & Plugin API Reference Manual

**minicut** features an embedded, high-performance **LuaJIT 5.1** scripting runtime with Dear ImGui bindings, timeline/clip positioning, keyframing, project automation, and native network capabilities (`libcurl`).

---

## 1. Directory Structure

Scripts and shelf icons are placed in the user configuration directory:

| Directory | Purpose |
|---|---|
| `~/.config/minicut/autorun/` | Scripts executed at startup. Generates a tool icon button on the **Tool Shelf Dock** directly beneath the menu bar. |
| `~/.config/minicut/scripts/` | Scripts placed in the top-level **Scripts** menu in the main menu bar. |
| `~/.config/minicut/icons/` | 32x32 PNG icons matching script stem (e.g. `quick_placement.png` for `quick_placement.lua`). |
| `~/.config/minicut/docs/` | Contains this manual (`minicut_lua_api.pdf`). |

---

## 2. Plugin Architecture & Tool Panels

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
| `assets.create_text(text, [opts])`<br>`project.create_text_asset(text, [opts])` | `string (asset_id)` | Renders text asset via FreeType 2 and FT_Stroker. Supports font, size, colors, background, outline, letter/line spacing, justification, and Arabic RTL shaping. |

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
| `gui.file_dialog([opts])` | Opens file dialog choosing native vs built-in per user settings. Supports sync return or callback. |
| `gui.native_file_dialog([opts])` | Forces system native file dialog (kdialog, zenity, or portal). |
| `gui.builtin_file_dialog([opts])` | Forces built-in ImGui file dialog modal. |
| `gui.popout_preview(popped)` | Detaches (true) or docks (false) the video preview panel into a floating window. |
| `gui.is_preview_popped_out()` | Returns `boolean` whether preview is currently detached/floating. |
| `gui.set_preview_aspect_mode(mode)` | Sets aspect mode: 0 (Auto), 1 (16:9), 2 (9:16), 3 (4:3), 4 (1:1), 5 (21:9), 6 (4:5). |
| `gui.get_preview_aspect_mode()` | Returns current preview aspect mode index (0-6). |
| `gui.set_transform_tool(tool)` | Sets active preview gizmo tool (`"select"`, `"translate"`, `"scale"`, `"rotate"`). |
| `gui.get_transform_tool()` | Returns active preview gizmo tool name (`"select"`, `"translate"`, `"scale"`, `"rotate"`). |

### 3.4. Timeline & Tracks (`timeline`)

| Function | Description |
|---|---|
| `timeline.place_clip(path, track, t, dur)` | Imports and places clip on timeline lane. |
| `timeline.place_text_clip(text, track, [t], [dur], [opts])` | Creates a text asset and immediately places it on track lane. |
| `timeline.get_playhead()` | Returns playhead time in seconds. |
| `timeline.set_playhead(t)` | Seeks playhead and refreshes preview frame. |
| `timeline.get_duration()` | Returns total project duration in seconds. |
| `timeline.get_track_count()` | Returns number of tracks. |
| `timeline.get_track_depth(track_idx)` | Returns Z-depth compositing order (0-100). |
| `timeline.set_track_depth(track_idx, z)` | Sets Z-depth compositing order. |
| `timeline.add_track(track_type)` | Adds new track (`"video"` or `"audio"`). |
| `timeline.move_track_up(track_idx)` | Moves track up one position. |
| `timeline.move_track_down(track_idx)` | Moves track down one position. |
| `timeline.move_track(from_idx, to_idx)` | Moves track from index to index. |
| `timeline.swap_tracks(idx_a, idx_b)` | Swaps two tracks. |
| `timeline.set_track_name(track_idx, name)` | Renames track. |
| `timeline.get_track_name(track_idx)` | Returns track name. |
| `timeline.get_selected_clips()` | Returns table array of selected clip IDs. |
| `timeline.select_clips(clip_ids_table)` | Selects multiple clips. |
| `timeline.clear_selection()` | Deselects all clips. |
| `timeline.select_all()` | Selects all clips across all tracks. |
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
| `clip.get_transform(clip_id)` | Returns table `{ x, y, scale, rotation }`. |
| `clip.set_transform(clip_id, [x, y, s, r])` | Sets translation X, Y, scale, and rotation simultaneously. |
| `clip.reset_transform(clip_id)` | Resets clip transform to origin (`x=0, y=0, scale=1.0, rotation=0.0`). |
| `timeline.add_keyframe(clip_id, prop, t, val)` | Adds animation keyframe. |
| `timeline.evaluate_keyframe(clip_id, prop, t, def)` | Evaluates interpolated value. |

### 3.6. Engine Utilities (`minicut`)

| Function | Description |
|---|---|
| `minicut.push_undo()` | Creates an undo snapshot. |
| `minicut.set_status(msg, [dur])` | Shows green toast in menu bar. |
| `minicut.log(message)` | Prints message to console. |

### 3.7. Export Automation & Progress (`export`)

| Function | Returns | Description |
|---|---|---|
| `export.get_audio_progress()` | `number` | Audio mixing progress (stereo mixing & keyframes, 0.0 to 1.0). |
| `export.get_progress()` | `number` | Overall export pipeline progress (0.0 to 1.0). |
| `export.is_exporting()` | `boolean` | True if background render/encode is actively running. |
| `export.get_status()` | `string` | Human-readable pipeline status description. |

