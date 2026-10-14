-- ============================================================================
-- quick_placement.lua (Autorun Script)
-- Demonstrates GUI controls, media placement on timeline, transforms & status
-- Uses gui.file_dialog for dynamic media file selection
-- ============================================================================

-- gui state
local media_path = "test_sample.png"
local target_track = 0
local clip_duration = 4.0
local offset_x = 0.0
local offset_y = 0.0
local clip_scale = 1.0

-- timeline logic goes here
function logic_callback()
    local playhead = timeline.get_playhead()
    minicut.log("Placing media at playhead: " .. playhead)

    local clip_id = timeline.place_image(media_path, target_track, playhead, clip_duration)
    if clip_id and clip_id ~= "" then
        clip.set_position(clip_id, offset_x, offset_y)
        clip.set_scale(clip_id, clip_scale)
        minicut.set_status("Placed image clip '" .. clip_id .. "' at " .. string.format("%.2fs", playhead))
    else
        minicut.set_status("Failed to place media from path: " .. media_path)
    end
end

function choose_media_file()
    gui.file_dialog("Select Media File", function(selected)
        if selected and selected ~= "" then
            media_path = selected
            minicut.set_status("Selected media: " .. selected)
        end
    end, "Media Files (*.png *.jpg *.jpeg *.mp4 *.mov *.mkv *.ogv *.ogg)|*.png;*.jpg;*.jpeg;*.mp4;*.mov;*.mkv;*.ogv;*.ogg|All Files (*.*)|*.*")
end

function plugin:init_panel()
    -- add your gui controls here
    gui.text_colored(0.24, 0.68, 0.91, 1.0, "Quick Media Placement Tool")
    gui.separator()

    gui.text("Media File Path:")
    media_path = gui.input_text("##file_path", media_path)
    gui.same_line()
    gui.button("Browse...##media_browse", choose_media_file)

    gui.text("Target Track:")
    gui.same_line()
    gui.set_next_item_width(100.0)
    target_track = gui.slider_int("##trk", target_track, 0, math.max(0, timeline.get_track_count() - 1))

    gui.text("Duration (sec):")
    gui.same_line()
    gui.set_next_item_width(100.0)
    clip_duration = gui.slider_float("##dur", clip_duration, 0.5, 30.0)

    gui.separator()
    gui.text("Initial Transform:")
    gui.set_next_item_width(120.0)
    offset_x = gui.drag_float("Pos X", offset_x, 2.0, -1000.0, 1000.0)
    gui.set_next_item_width(120.0)
    offset_y = gui.drag_float("Pos Y", offset_y, 2.0, -1000.0, 1000.0)
    gui.set_next_item_width(120.0)
    clip_scale = gui.slider_float("Scale", clip_scale, 0.1, 4.0)

    gui.separator()
    gui.spacing()
    gui.button("Place Media At Playhead", logic_callback)
end
