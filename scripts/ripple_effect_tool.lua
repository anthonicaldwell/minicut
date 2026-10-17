-- ============================================================================
-- ripple_effect_tool.lua (minicut SFX Pipeline Plugin)
-- Pulls raw frame data from the active sequence clip, applies a ripple shader,
-- and reinserts or creates a new SFX clip.
-- ============================================================================

local amplitude = 18.0
local frequency = 0.06
local phase = 0.0
local decay = 0.002
local use_custom_center = false
local center_x = 0.5
local center_y = 0.5
local create_new_clip = false
local target_track = 0
local status_msg = "Select a clip on the timeline and click 'Apply Ripple Shader'."

-- Register localizations for this plugin
locale.register("en", {
    ripple_title = "Ripple & Wave SFX Generator",
    ripple_amp = "Wave Amplitude",
    ripple_freq = "Wave Frequency",
    ripple_phase = "Phase Offset",
    ripple_decay = "Distance Decay",
    ripple_center_toggle = "Custom Center Origin",
    ripple_btn = "Apply Ripple Shader to Active Clip",
    ripple_create_new = "Create New Clip Instead of Overwriting",
    ripple_success_reinsert = "Successfully applied ripple shader to clip: ",
    ripple_success_new = "Created new ripple SFX clip: ",
    ripple_no_clip = "No clip selected or found on the timeline!",
    ripple_frame_fail = "Failed to extract raw frame from clip!"
})

function apply_ripple_effect()
    -- 1. Identify active / selected clip ID
    local clip_id = clip.get_selected_id and clip.get_selected_id() or ""
    if not clip_id or clip_id == "" then
        clip_id = minicut.get_selected_clip and minicut.get_selected_clip() or ""
    end
    if not clip_id or clip_id == "" then
        clip_id = "active"
    end

    minicut.log("[Ripple Tool] Fetching frame from clip: " .. tostring(clip_id))

    -- 2. Pull raw frame data from sequence clip at current playhead
    local frame = clip.get_frame(clip_id)
    if not frame then
        status_msg = locale("ripple_frame_fail", "Failed to extract raw frame from clip!")
        minicut.set_status(status_msg)
        return
    end

    local cx = frame.width * (use_custom_center and center_x or 0.5)
    local cy = frame.height * (use_custom_center and center_y or 0.5)

    -- 3. Create or prepare the ripple shader with uniform parameters
    local sid = shader.create("ripple")
    local uniforms = {
        center_x = cx,
        center_y = cy,
        amplitude = amplitude,
        frequency = frequency,
        phase = phase,
        decay = decay
    }

    minicut.log(string.format("[Ripple Tool] Applying ripple (amp=%.1f, freq=%.3f, size=%dx%d)",
                              amplitude, frequency, frame.width, frame.height))

    -- 4. Apply shader to frame
    local ripple_frame = shader.apply(sid, frame, uniforms)
    if not ripple_frame then
        status_msg = "Shader execution failed!"
        minicut.set_status(status_msg)
        return
    end

    -- 5. Reinsert into active clip or spawn new timeline clip
    if create_new_clip then
        local playhead = timeline.get_playhead()
        local new_id = ripple_frame:create_clip(target_track, playhead, 5.0)
        if new_id and new_id ~= "" then
            status_msg = locale("ripple_success_new", "Created new ripple SFX clip: ") .. new_id
            minicut.set_status(status_msg)
        else
            status_msg = "Failed to create new clip from processed frame."
            minicut.set_status(status_msg)
        end
    else
        local ok = ripple_frame:reinsert(frame.clip_id)
        if ok then
            status_msg = locale("ripple_success_reinsert", "Successfully applied ripple shader to clip: ") .. tostring(frame.clip_id)
            minicut.set_status(status_msg)
        else
            status_msg = "Failed to reinsert frame back into clip!"
            minicut.set_status(status_msg)
        end
    end
end

-- Plugin UI Panel rendered in minicut's Script / Plugin dock
function plugin:init_panel()
    gui.text_colored(0.3, 0.8, 1.0, 1.0, locale("ripple_title", "Ripple & Wave SFX Generator (Lua)"))
    gui.separator()

    gui.text(locale("ripple_amp", "Wave Amplitude") .. ":")
    amplitude = gui.slider_float("##lua_ripple_amp", amplitude, 0.0, 60.0)

    gui.text(locale("ripple_freq", "Wave Frequency") .. ":")
    frequency = gui.slider_float("##lua_ripple_freq", frequency, 0.01, 0.25)

    gui.text(locale("ripple_phase", "Phase Offset") .. ":")
    phase = gui.slider_float("##lua_ripple_phase", phase, 0.0, 6.28)

    gui.text(locale("ripple_decay", "Distance Decay") .. ":")
    decay = gui.slider_float("##lua_ripple_decay", decay, 0.0, 0.01)

    gui.spacing()
    use_custom_center = gui.checkbox(locale("ripple_center_toggle", "Custom Center Origin") .. "##lua_ripple_center_toggle", use_custom_center)
    if use_custom_center then
        gui.text("Origin X (Normalized 0.0 - 1.0):")
        center_x = gui.slider_float("##lua_ripple_cx", center_x, 0.0, 1.0)
        gui.text("Origin Y (Normalized 0.0 - 1.0):")
        center_y = gui.slider_float("##lua_ripple_cy", center_y, 0.0, 1.0)
    end

    gui.separator()
    create_new_clip = gui.checkbox(locale("ripple_create_new", "Create New Clip Instead of Overwriting") .. "##lua_ripple_create_new", create_new_clip)
    if create_new_clip then
        gui.text("Target Track:")
        local max_tracks = math.max(0, timeline.get_track_count() - 1)
        target_track = gui.slider_int("##lua_ripple_target_track", target_track, 0, max_tracks)
    end

    gui.spacing()
    gui.button(locale("ripple_btn", "Apply Ripple Shader to Active Clip") .. "##lua_ripple_apply_btn", apply_ripple_effect)

    gui.separator()
    gui.text_disabled("Status: " .. tostring(status_msg))
end
