# minicut Lua Scripting & Plugin API Reference Manual (v1.5.0)

**minicut** features an embedded, high-performance **LuaJIT 5.1** scripting runtime with Dear ImGui bindings, timeline/clip positioning, keyframing, project automation, native network capabilities (`libcurl`), AI assistant integration, custom GLSL shader pipelines, and real-time audio/video SFX processing.

---

## 1. Directory Structure & Script Discovery

Scripts and shelf icons are placed in the user configuration directory:

| Directory | Purpose |
|---|---|
| `~/.config/minicut/autorun/` | Scripts executed at startup. Automatically registers a tool icon button on the **Tool Shelf Dock** directly beneath the menu bar. |
| `~/.config/minicut/scripts/` | Scripts placed directly in the **Modifiers** menu in the main menu bar. |
| `~/.config/minicut/shaders/gl/` | Custom OpenGL GLSL vertex (`.vert`) and fragment (`.frag`) shaders loaded by name (e.g. `shader.create("ripple")`). |
| `~/.config/minicut/shaders/vulkan/` | Custom Vulkan GLSL vertex (`.vert`) and fragment (`.frag`) shaders loaded by name. |
| `~/.config/minicut/icons/` | 32x32 PNG icons matching script stem (e.g. `ripple_effect_tool.png` for `ripple_effect_tool.lua`). |
| `~/.config/minicut/docs/` | Contains compiled API documentation and reference guides (`minicut_lua_api.pdf`). |

---

## 2. Plugin Architecture & Tool Panels

Every script can define timeline callbacks and register Dear ImGui controls in `plugin:init_panel()`.
When clicked from the menu bar or tool shelf, the script creates an independent, dockable tool panel positioned to the right of the preview and timeline.

```lua
-- Plugin metadata
plugin.id = "media_placement_tool"
plugin.title = "Media Placement Tool"

-- GUI state variables
local insert_track = 0
local clip_scale = 1.0

-- Callback action
function execute_placement()
    minicut.push_undo()
    local path = "/path/to/media.mp4"
    local clip_id = timeline.place_video(path, insert_track, timeline.get_playhead(), 5.0)
    if clip_id and clip_id ~= "" then
        clip.set_scale(clip_id, clip_scale)
        timeline.add_keyframe(clip_id, "opacity", 0.0, 0.0)
        timeline.add_keyframe(clip_id, "opacity", 1.0, 1.0)
        minicut.set_status("Media clip placed on track " .. insert_track)
    end
end

-- Panel registration
function plugin:init_panel()
    gui.text("Custom Media Tool")
    gui.separator()
    insert_track = gui.slider_int("Target Track", insert_track, 0, 4)
    clip_scale = gui.slider_float("Initial Scale", clip_scale, 0.1, 4.0)
    gui.button("Execute Placement", execute_placement)
end
```

---

## 3. Core Lua Modules (13 Modules)

### 3.1. GUI Controls (`gui`)

Provides Dear ImGui UI controls and window management inside dockable script panels:

| Function | Returns | Description |
|---|---|---|
| `gui.text(text)` | `void` | Normal text label. |
| `gui.text_colored(r, g, b, [a], text)` | `void` | Colored text label (0.0 to 1.0 per channel). |
| `gui.text_disabled(text)` | `void` | Disabled / muted text label. |
| `gui.button(label, [callback])` | `boolean` | Push button. Returns true when clicked; optionally executes callback. |
| `gui.small_button(label, [callback])` | `boolean` | Compact push button. |
| `gui.slider_float(label, val, min, max)` | `number` | Float slider widget. Returns updated value. |
| `gui.slider_int(label, val, min, max)` | `integer` | Integer slider widget. Returns updated value. |
| `gui.drag_float(label, val, [spd, min, max])` | `number` | Drag-to-adjust float field. |
| `gui.drag_int(label, val, [spd, min, max])` | `integer` | Drag-to-adjust integer field. |
| `gui.input_text(label, val)` | `string` | Single-line text input field. |
| `gui.input_float(label, val)` | `number` | Float number input field. |
| `gui.checkbox(label, val)` | `boolean` | Toggle checkbox widget. Returns updated boolean state. |
| `gui.same_line()` | `void` | Positions subsequent widget on the same horizontal line. |
| `gui.separator()` | `void` | Horizontal dividing rule. |
| `gui.spacing()` | `void` | Vertical spacing unit. |
| `gui.tooltip(text)` | `void` | Displays hover tooltip over the preceding item. |
| `gui.set_next_item_width(w)` | `void` | Sets the pixel width of the next widget. |
| `gui.file_dialog([opts])` | `string` / `void` | Opens file picker respecting user setting (Native vs Built-in). Supports sync return or callback. |
| `gui.native_file_dialog([opts])` | `string` / `void` | Forces system native file dialog (`kdialog`, `zenity`, or desktop portal). |
| `gui.builtin_file_dialog([opts])` | `string` / `void` | Forces built-in ImGui modal file dialog. |
| `gui.popout_preview(popped)` | `void` | Detaches (`true`) or docks (`false`) the video preview window. |
| `gui.is_preview_popped_out()` | `boolean` | Returns true if the video preview is currently detached/floating. |
| `gui.set_preview_aspect_mode(mode)` | `void` | Sets preview aspect mode: `0` (Auto), `1` (16:9), `2` (9:16), `3` (4:3), `4` (1:1), `5` (21:9), `6` (4:5). |
| `gui.get_preview_aspect_mode()` | `integer` | Returns active preview aspect mode index (0-6). |
| `gui.set_transform_tool(tool)` | `void` | Activates preview viewport gizmo: `"select"`, `"translate"`, `"scale"`, or `"rotate"`. |
| `gui.get_transform_tool()` | `string` | Returns active preview viewport gizmo name. |

---

### 3.2. Timeline & Tracks (`timeline`)

Controls timeline lanes, playback cursor, track hierarchy, and keyframing:

| Function | Returns | Description |
|---|---|---|
| `timeline.get_playhead()` | `number` | Returns current playhead position in seconds. |
| `timeline.set_playhead(t)` | `void` | Moves timeline playhead and refreshes preview frame. |
| `timeline.get_duration()` | `number` | Returns total project duration in seconds. |
| `timeline.get_track_count()` | `integer` | Returns total number of tracks. |
| `timeline.add_track(track_type)` | `integer` | Appends a new track (`"video"` or `"audio"`). Returns new track index. |
| `timeline.place_video(path, track, t, dur)` | `string` | Places a video asset onto target track. Returns clip ID. |
| `timeline.place_audio(path, track, t, dur)` | `string` | Places an audio asset onto target track. Returns clip ID. |
| `timeline.place_image(path, track, t, dur)` | `string` | Places an image asset onto target track. Returns clip ID. |
| `timeline.place_text_clip(text, track, t, dur, [opts])` | `string` | Renders a styled FreeType text asset and places it directly on track lane. |
| `timeline.get_selected_clip()` | `string` | Returns ID of primary selected clip, or empty string. |
| `timeline.get_selected_clips()` | `table` | Returns array table of all currently selected clip IDs. |
| `timeline.select_clip(clip_id, [multi, range])` | `void` | Selects a clip with optional multi-select or range-select flags. |
| `timeline.select_clips(ids_table)` | `void` | Replaces selection with specified array of clip IDs. |
| `timeline.clear_selection()` | `void` | Deselects all clips on timeline. |
| `timeline.select_all()` | `void` | Selects all clips across all tracks. |
| `timeline.delete_clip(clip_id)` | `void` | Deletes clip from timeline. |
| `timeline.split_clip(clip_id, t)` | `string` | Splits clip at time `t`. Returns new second clip ID. |
| `timeline.move_track_up(track_idx)` | `void` | Moves track up one position in layer stack. |
| `timeline.move_track_down(track_idx)` | `void` | Moves track down one position in layer stack. |
| `timeline.move_track(from_idx, to_idx)` | `void` | Moves track from source index to target index. |
| `timeline.swap_tracks(idx_a, idx_b)` | `void` | Swaps positions of two tracks. |
| `timeline.set_track_name(track_idx, name)` | `void` | Renames track at index. |
| `timeline.get_track_name(track_idx)` | `string` | Returns track name at index. |
| `timeline.add_keyframe(clip_id, param, t, val)` | `void` | Adds or updates keyframe (e.g. `"opacity"`, `"volume"`, `"pos_x"`). |
| `timeline.remove_keyframe(clip_id, param, t)` | `void` | Removes keyframe at time `t`. |
| `timeline.clear_keyframes(clip_id, param)` | `void` | Clears all keyframes for parameter on clip. |
| `timeline.get_keyframes(clip_id, param)` | `table` | Returns array of `{ time, value }` tables for parameter. |
| `timeline.evaluate_keyframe(clip_id, param, t, def)` | `number` | Evaluates interpolated parameter value at relative time `t`. |

---

### 3.3. Clip Transforms & SFX Manipulation (`clip`)

Manipulates spatial positioning, non-uniform scaling, opacity, audio gain, and raw video frames:

| Function | Returns | Description |
|---|---|---|
| `clip.get_position(clip_id)` | `number, number` | Returns `pos_x, pos_y` relative to canvas center. |
| `clip.set_position(clip_id, x, y)` | `void` | Sets `pos_x, pos_y` translation in pixels. |
| `clip.get_scale(clip_id)` | `number` | Returns uniform scale multiplier (1.0 = 100%). |
| `clip.get_scale_xy(clip_id)` | `number, number` | Returns independent `scale_x, scale_y` multipliers. |
| `clip.get_scale_x(clip_id)` / `clip.get_scale_y(clip_id)` | `number` | Returns horizontal / vertical scale multiplier. |
| `clip.set_scale(clip_id, s, [sy])` | `void` | Sets uniform scale, or non-uniform scale if `sy` provided. |
| `clip.set_scale_x(clip_id, sx)` / `clip.set_scale_y(clip_id, sy)` | `void` | Sets individual horizontal / vertical scale multiplier. |
| `clip.get_rotation(clip_id)` | `number` | Returns rotation in degrees. |
| `clip.set_rotation(clip_id, deg)` | `void` | Sets rotation in degrees. |
| `clip.get_opacity(clip_id)` | `number` | Returns opacity [0.0, 1.0]. |
| `clip.set_opacity(clip_id, opacity)` | `void` | Sets opacity [0.0, 1.0]. |
| `clip.get_volume(clip_id)` | `number` | Returns audio volume multiplier [0.0, 2.0]. |
| `clip.set_volume(clip_id, vol)` | `void` | Sets audio volume multiplier. |
| `clip.get_time(clip_id)` | `number, number, number` | Returns `start_time, duration, offset`. |
| `clip.set_time(clip_id, start, dur, offset)` | `void` | Updates clip timeline timing and media offset. |
| `clip.get_info(clip_id)` | `table` | Returns table with complete clip properties and metadata. |
| `clip.get_transform(clip_id)` | `table` | Returns `{ x, y, scale, scale_x, scale_y, rotation }`. |
| `clip.set_transform(clip_id, x, y, s, r)` | `void` | Sets X, Y, scale, and rotation simultaneously. |
| `clip.reset_transform(clip_id)` | `void` | Resets transform to defaults (`x=0, y=0, scale=1, rot=0`). |
| `clip.get_frame(clip_id, [t, w, h])` | `table` | Extracts raw RGBA frame: `{ width, height, data, clip_id }`. Pass `"active"` or `""` for currently selected clip. |
| `clip.set_frame(clip_id, frame_table, [t])` | `boolean` | Reinserts modified RGBA pixel frame into clip media. Triggers multi-frame shader rendering if shader is attached. |
| `clip.create_from_frame(frame_table, dur, [name])` | `string` | Creates new asset and clip from raw RGBA pixel buffer. |
| `clip.get_selected_id()` / `clip.get_selected_clip()` | `string` | Returns active clip ID. |

#### Frame Buffer Object Methods & Fluent Piping
When `clip.get_frame(clip_id, [time, width, height])` is called (or passing `"active"` or `""` to target the active timeline selection), the returned frame buffer object provides fluent object-oriented method chaining:

- `frame:apply_shader(shader_id_or_name, [uniforms_table])`: Applies a compiled shader effect or named custom shader to the frame buffer, returning the updated frame buffer object.
- `frame:reinsert([target_clip_id, time])`: Reinserts the processed frame buffer into the clip. If a shader effect is attached, automatically runs the multi-frame video rendering pipeline across the **entire clip** in the background.
- `frame:create_clip(track_index, start_time, duration, [asset_name])`: Creates a brand-new media asset and timeline clip on the specified track from the processed frame buffer.

```lua
-- Fluent chaining: get frame from active clip, apply ripple effect, and reinsert across entire clip
local ok = clip.get_frame("active"):apply_shader("ripple", { amplitude = 12.0, frequency = 0.08 }):reinsert()

-- Fluent chaining: apply shader and synthesize a new clip on track 0
local new_clip = clip.get_frame("active"):apply_shader("ripple"):create_clip(0, 0.0, 5.0, "Ripple Wave")
```

---

### 3.4. Project & File Automation (`project` & Global)

| Function | Returns | Description |
|---|---|---|
| `project.save([path])`<br>`project_save([path])` | `boolean, string` | Saves project state to `.minicut` JSON file. |
| `project.load(path)`<br>`project_load(path)` | `boolean, string` | Loads project, re-extracts audio, refreshes timeline and preview. |
| `project.get_name()` | `string` | Returns current project name. |
| `project.set_name(name)` | `void` | Sets project name. |
| `project.import_video(path)` | `string (asset_id)` | Probes and imports video file into asset pool. |
| `project.import_audio(path)` | `string (asset_id)` | Probes and imports audio file into asset pool. |
| `project.import_image(path, [dur])` | `string (asset_id)` | Imports image into pool (default duration 5.0s). |
| `project.create_text_asset(text, [opts])` | `string (asset_id)` | Renders text asset via FreeType 2 and FT_Stroker. |

---

### 3.5. Asset Pool Management (`assets`)

| Function | Returns | Description |
|---|---|---|
| `assets.create_text(text, [opts])` | `string (asset_id)` | Generates styled text asset with color, stroke, background, and Arabic RTL shaping. |
| `assets.get_scale(asset_id)` | `number` | Returns asset default uniform scale multiplier. |
| `assets.get_scale_x(asset_id)` / `assets.get_scale_y(asset_id)` | `number` | Returns asset horizontal / vertical scale multiplier. |
| `assets.set_scale(asset_id, s, [sy])` | `void` | Sets asset default scale multiplier. |
| `assets.set_scale_x(asset_id, sx)` / `assets.set_scale_y(asset_id, sy)` | `void` | Sets asset default horizontal / vertical scale multiplier. |

---

### 3.6. Engine Utilities (`minicut`)

| Function | Returns | Description |
|---|---|---|
| `minicut.push_undo()` | `void` | Records undo state before destructive edits. |
| `minicut.set_status(msg, [dur])` | `void` | Displays green toast alert in main menu bar. |
| `minicut.log(...)` | `void` | Prints message to standard output console. |
| `minicut.get_selected_clip()` / `minicut.get_selected_id()` | `string` | Returns primary selected clip ID. |

---

### 3.7. Network & Internet Library (`net`)

Powered by native `libcurl` bindings, the `net` module provides HTTP, HTTPS, FTP, and secure SFTP capabilities:

| Function | Returns | Description |
|---|---|---|
| `net.get(url, [opts])` | `table` | HTTP GET request `{ status, ok, body, headers }`. |
| `net.post(url, body, [opts])` | `table` | HTTP POST request with string or key-value payload. |
| `net.download(url, dest, [opts])` | `boolean, string` | Streams remote file directly to disk path. Supports `http://`, `https://`, `ftp://`, and `sftp://`. |
| `net.upload(url, file, [opts])` | `table, string` | Uploads local file. Supports HTTP multipart POST as well as direct binary stream uploads for `ftp://` and `sftp://` (e.g. `sftp://user:pass@host/path/file.mp4`). |
| `net.request(opts)` | `table` | Custom network request (`method`, `url`, `headers`, `body`). Supports HTTP/HTTPS/SFTP protocols. |

```lua
-- SFTP File Transfer Examples
-- Uploading a rendered video file to remote SFTP storage
local ok, err = net.upload("sftp://user:secret@192.168.1.50:22/srv/media/rendered_output.mp4", "/home/user/exports/output.mp4")
if ok then
    minicut.set_status("SFTP upload completed successfully!")
else
    minicut.log("SFTP upload error: " .. tostring(err))
end

-- Downloading remote assets over SFTP directly into local cache
local ok_down, err_down = net.download("sftp://user:secret@192.168.1.50:22/srv/media/remote_b_roll.mp4", "/home/user/cache/remote_b_roll.mp4")
if ok_down then
    local asset_id = project.import_video("/home/user/cache/remote_b_roll.mp4")
    minicut.set_status("Downloaded and imported asset: " .. asset_id)
end
```

---

### 3.8. Export Automation & Progress (`export`)

| Function | Returns | Description |
|---|---|---|
| `export.get_audio_progress()` | `number` | Audio mixing and keyframe evaluation progress (0.0 to 1.0). |
| `export.get_progress()` | `number` | Overall export rendering and encoding progress (0.0 to 1.0). |
| `export.is_exporting()` | `boolean` | Returns true if export thread is actively encoding. |
| `export.get_status()` | `string` | Status string describing current export step. |

---

### 3.9. Localization System (`locale` / `tr`)

The `locale` module provides complete i18n support across English, Japanese, Korean, Ukrainian, and Arabic:

```lua
-- Directly callable function
local hello = locale("hello_key", "Default Greeting")
local msg = tr("export_render", "Export")

-- Inspecting active language and text direction
local lang = locale.get_language() -- "en", "ja", "ko", "uk", "ar"
local rtl = locale.is_rtl()        -- true for Arabic

-- Dynamically registering translations
locale.register("my_plugin_title", {
    en = "Ripple Wave Effect",
    ja = "リップル波形エフェクト",
    ko = "리플 웨이브 효과",
    uk = "Ефект хвилі брижі",
    ar = "تأثير موجة التموج"
})
```

| Function | Returns | Description |
|---|---|---|
| `locale(key, [def])`<br>`tr(key, [def])`<br>`locale.get(key, [def])` | `string` | Translates key to active language. |
| `locale.get_language()` | `string` | Returns two-letter language code (`"en"`, `"ja"`, `"ko"`, `"uk"`, `"ar"`). |
| `locale.set_language(code)` | `void` | Changes application active language dynamically. |
| `locale.is_rtl()` | `boolean` | Returns true if active language is Right-to-Left (Arabic). |
| `locale.register(key, dict)`<br>`locale.add(key, dict)` | `void` | Registers a multi-language translation dictionary for key. |

---

### 3.10. AI Assistant Integration (`ai`)

Allows scripts to invoke LLM intelligence for transcriptions, script generation, and structured video editing:

```lua
if ai.is_available() and ai.has_api_key() then
    local prompt = "Generate a 3-track timeline layout for a 30s product commercial."
    local result = ai.prompt(prompt)
    minicut.log("AI Plan: " .. result)
    
    -- Request structured JSON responses
    local schema = '{"tracks": [{"name": "string", "type": "string"}]}'
    local json_data = ai.prompt_json(prompt, schema)
    local obj = ai.parse_json(json_data)
end
```

| Function | Returns | Description |
|---|---|---|
| `ai.is_available()` | `boolean` | Returns true if AI subsystem is compiled and initialized. |
| `ai.is_enabled()` | `boolean` | Returns true if AI features are enabled in user settings. |
| `ai.set_enabled(bool)` | `void` | Enables or disables AI subsystem. |
| `ai.has_api_key()` | `boolean` | Returns true if Gemini / AI API key is configured. |
| `ai.set_api_key(key)` | `void` | Sets API key for AI assistant. |
| `ai.is_busy()` | `boolean` | Returns true if background LLM request is pending. |
| `ai.get_status()` | `string` | Returns status string of AI core. |
| `ai.prompt(prompt_text, [system_inst])` | `string` | Sends synchronous text prompt to LLM. |
| `ai.prompt_json(prompt_text, schema_json)` | `string` | Prompts LLM enforcing structured JSON schema output. |
| `ai.parse_json(json_str)` | `table` | Parses JSON string into a Lua table. |
| `ai.to_json(table)` | `string` | Serializes Lua table into a JSON string. |
| `ai.set_mock_response(resp)` / `ai.clear_mock_response()` | `void` | Sets mock response for automated unit testing. |

---

### 3.11. Custom GLSL Shader Pipeline (`shader`)

Allows scripts to load custom vertex and fragment shaders by name, compile raw GLSL fragment shaders, supply uniforms, and render pixel effects across entire video clips:

#### Custom Shader Loading & Discovery
Shaders can be loaded by simple name (e.g., `"ripple"`, `"grayscale"`, `"invert"`, `"vignette"`) from the user configuration directories:
- **OpenGL**: `~/.config/minicut/shaders/gl/<name>.frag` and `<name>.vert`
- **Vulkan**: `~/.config/minicut/shaders/vulkan/<name>.frag` and `<name>.vert`

If a raw GLSL source string is passed instead, it will be compiled directly in memory.

#### Whole-Clip Multi-Frame Processing
When applied to a clip (via `shader.apply(shader_id, clip_id, [uniforms])` or `frame:apply_shader(...):reinsert()`), minicut executes the shader effect across the **entire video clip frame-by-frame**, rather than only the first preview frame. The video stream is processed asynchronously via a background rendering thread, encoded using FFmpeg, and the clip's underlying media asset is automatically updated upon completion without blocking UI interactivity.

#### Uniforms
Uniforms can be set individually via `shader.set_uniform(sid, name, val)` or passed as a dictionary table directly to `shader.apply()` or `:apply_shader()`. Uniform names are automatically matched whether written with or without the `u_` prefix (e.g. `amplitude` and `u_amplitude`, `frequency` and `u_frequency`).

```lua
-- Example 1: Loading a named shader from ~/.config/minicut/shaders/gl/
local sid = shader.create("ripple")
local uniforms = {
    frequency = 40.0,
    amplitude = 0.03,
    phase = 0.0
}
shader.apply(sid, "active", uniforms)

-- Example 2: Compiling in-memory GLSL shader and applying to selected clip
local shader_src = [[
    #version 330 core
    in vec2 TexCoord;
    out vec4 FragColor;
    uniform sampler2D u_texture;
    uniform float u_time;
    uniform float u_frequency;
    uniform float u_amplitude;
    void main() {
        vec2 p = TexCoord - 0.5;
        float r = length(p);
        float angle = atan(p.y, p.x);
        r += sin(r * u_frequency - u_time * 4.0) * u_amplitude;
        vec2 uv = vec2(0.5 + r * cos(angle), 0.5 + r * sin(angle));
        FragColor = texture(u_texture, uv);
    }
]]

local sh = shader.create(shader_src)
shader.set_uniform(sh, "u_frequency", 40.0)
shader.set_uniform(sh, "u_amplitude", 0.03)
shader.apply(sh, "active")
```

| Function | Returns | Description |
|---|---|---|
| `shader.create(name_or_source)` | `integer (shader_id)` | Compiles custom GLSL shader string or loads by name from `~/.config/minicut/shaders/gl/` or `~/.config/minicut/shaders/vulkan/`. Returns handle. |
| `shader.set_uniform(shader_id, name, val)` | `void` | Sets float, int, or table vector uniform parameter. |
| `shader.get_uniform(shader_id, name)` | `number / table` | Gets current value of uniform parameter. |
| `shader.apply(shader_id, target, [uniforms])` | `boolean / table` | If target is a clip ID (`string` or `"active"`), renders shader across all video frames of the clip asynchronously. If target is a frame buffer (`table`), transforms and returns the frame buffer. |

---

### 3.12. Audio & Video SFX Pipeline (`sfx`)

Combines frame extraction, GLSL shader application, and new media asset creation:

| Function | Returns | Description |
|---|---|---|
| `sfx.get_frame(clip_id, [time])` | `table` | Retrieves raw RGBA frame buffer `{ width, height, data }`. |
| `sfx.apply_shader(shader_id, clip_id)` | `boolean` | Applies GLSL shader program to clip. |
| `sfx.reinsert_frame(clip_id, frame_table, [time])` | `boolean` | Overwrites specific video frame with modified RGBA data. |
| `sfx.create_clip(frame_table, duration, [name])` | `string` | Synthesizes a new video asset and timeline clip from frame. |
| `sfx.create_shader(glsl_source)` | `integer` | Compiles GLSL shader program. |

---

### 3.13. Plugin Lifecycle & Panel Registration (`plugin`)

| Property / Method | Type | Description |
|---|---|---|
| `plugin.id` | `string` | Unique identifier for the plugin tool window. |
| `plugin.title` | `string` | Display title shown on the panel tab and window title bar. |
| `plugin:init_panel()` | `function` | Invoked each frame when the plugin tool panel is open. Renders Dear ImGui controls. |

---

## 4. Practical Lua Script Example

Here is a complete, working tool script that adds a animated ripple wave effect to the selected video clip:

```lua
-- ripple_effect_tool.lua
plugin.id = "ripple_effect_tool"
plugin.title = "Ripple Wave SFX"

local frequency = 30.0
local amplitude = 0.02
local speed = 3.0

function apply_ripple()
    local clip_id = clip.get_selected_id()
    if not clip_id or clip_id == "" then
        minicut.set_status("Please select a video clip on the timeline first!")
        return
    end

    minicut.push_undo()
    local glsl = [[
        #version 330 core
        in vec2 TexCoord;
        out vec4 FragColor;
        uniform sampler2D u_texture;
        uniform float u_time;
        uniform float u_frequency;
        uniform float u_amplitude;
        void main() {
            vec2 p = TexCoord - 0.5;
            float r = length(p);
            float angle = atan(p.y, p.x);
            r += sin(r * u_frequency - u_time * 4.0) * u_amplitude;
            vec2 uv = vec2(0.5 + r * cos(angle), 0.5 + r * sin(angle));
            FragColor = texture(u_texture, uv);
        }
    ]]

    local sh = shader.create(glsl)
    shader.set_uniform(sh, "u_frequency", frequency)
    shader.set_uniform(sh, "u_amplitude", amplitude)
    shader.set_uniform(sh, "u_time", speed)

    if shader.apply(sh, clip_id) then
        minicut.set_status("Ripple effect successfully applied to clip!")
    else
        minicut.set_status("Failed to apply shader effect.")
    end
end

function plugin:init_panel()
    gui.text("Water Ripple Wave Effect")
    gui.separator()
    frequency = gui.slider_float("Wave Frequency", frequency, 5.0, 100.0)
    amplitude = gui.slider_float("Wave Amplitude", amplitude, 0.005, 0.1)
    speed = gui.slider_float("Distortion Speed", speed, 0.5, 10.0)
    gui.spacing()
    gui.button("Apply Effect to Selected Clip", apply_ripple)
end
```
