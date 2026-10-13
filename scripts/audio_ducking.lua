-- ============================================================================
-- audio_ducking.lua (Menu Script)
-- Demonstrates volume keyframing for audio fade-in & fade-out
-- ============================================================================

local fade_in_sec = 1.5
local fade_out_sec = 1.5
local target_vol = 1.0

function logic_callback()
    local sel_clip = timeline.get_selected_clip()
    if not sel_clip or sel_clip == "" then
        minicut.set_status("Please select an audio or video clip first.")
        return
    end

    local start_t, dur, offset = clip.get_time(sel_clip)
    if dur <= 0.0 then return end

    minicut.push_undo()
    timeline.clear_keyframes(sel_clip, "volume")

    -- Fade In
    timeline.add_keyframe(sel_clip, "volume", 0.0, 0.0)
    timeline.add_keyframe(sel_clip, "volume", math.min(fade_in_sec, dur * 0.5), target_vol)

    -- Fade Out
    if dur > fade_out_sec then
        timeline.add_keyframe(sel_clip, "volume", dur - fade_out_sec, target_vol)
        timeline.add_keyframe(sel_clip, "volume", dur, 0.0)
    end

    minicut.set_status("Applied audio volume fade keyframes to " .. sel_clip)
end

function plugin:init_panel()
    gui.text_colored(0.95, 0.65, 0.15, 1.0, "Audio Fade & Volume Tool")
    gui.separator()

    local sel = timeline.get_selected_clip()
    gui.text("Selected: " .. (sel ~= "" and sel or "(none)"))

    gui.text("Fade-In Duration (s):")
    fade_in_sec = gui.slider_float("##fade_in", fade_in_sec, 0.1, 5.0)

    gui.text("Fade-Out Duration (s):")
    fade_out_sec = gui.slider_float("##fade_out", fade_out_sec, 0.1, 5.0)

    gui.text("Target Volume:")
    target_vol = gui.slider_float("##vol", target_vol, 0.0, 2.0)

    gui.spacing()
    gui.button("Apply Audio Fades", logic_callback)
end
