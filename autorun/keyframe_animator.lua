-- ============================================================================
-- keyframe_animator.lua (Autorun Script)
-- Demonstrates Keyframe Animation generation for selected clips
-- ============================================================================

-- gui state
local anim_duration = 2.0
local start_opacity = 0.0
local end_opacity = 1.0
local start_x = -200.0
local end_x = 0.0
local animate_pos = true
local animate_fade = true

-- timeline logic goes here
function logic_callback()
    local sel_clip = timeline.get_selected_clip()
    if not sel_clip or sel_clip == "" then
        minicut.set_status("No clip selected! Select a clip on the timeline first.")
        return
    end

    minicut.log("Generating keyframes on clip: " .. sel_clip)
    minicut.push_undo()

    if animate_fade then
        timeline.clear_keyframes(sel_clip, "opacity")
        timeline.add_keyframe(sel_clip, "opacity", 0.0, start_opacity)
        timeline.add_keyframe(sel_clip, "opacity", anim_duration, end_opacity)
    end

    if animate_pos then
        timeline.clear_keyframes(sel_clip, "pos_x")
        timeline.add_keyframe(sel_clip, "pos_x", 0.0, start_x)
        timeline.add_keyframe(sel_clip, "pos_x", anim_duration, end_x)
    end

    minicut.set_status("Created animated keyframes for clip " .. sel_clip)
end

function plugin:init_panel()
    gui.text_colored(0.18, 0.80, 0.44, 1.0, "Keyframe Animation Generator")
    gui.separator()

    local sel = timeline.get_selected_clip()
    if sel and sel ~= "" then
        gui.text("Target Clip: " .. sel)
    else
        gui.text_disabled("Target Clip: (None selected on timeline)")
    end
    gui.separator()

    gui.text("Animation Span (seconds):")
    anim_duration = gui.slider_float("##anim_dur", anim_duration, 0.2, 10.0)

    gui.separator()
    animate_fade = gui.checkbox("Animate Fade (Opacity)", animate_fade)
    if animate_fade then
        start_opacity = gui.slider_float("Start Opacity", start_opacity, 0.0, 1.0)
        end_opacity = gui.slider_float("End Opacity", end_opacity, 0.0, 1.0)
    end

    gui.separator()
    animate_pos = gui.checkbox("Animate Slide (Pos X)", animate_pos)
    if animate_pos then
        start_x = gui.drag_float("Start X", start_x, 5.0, -1000.0, 1000.0)
        end_x = gui.drag_float("End X", end_x, 5.0, -1000.0, 1000.0)
    end

    gui.spacing()
    gui.button("Generate Animation Keyframes", logic_callback)
end
