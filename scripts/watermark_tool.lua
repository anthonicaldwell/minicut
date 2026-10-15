-- ============================================================================
-- watermark_tool.lua (Menu Script)
-- Demonstrates placing and positioning an overlay watermark
-- Uses gui.file_dialog for dynamic file picking
-- ============================================================================

local watermark_path = "desktop/minicut.png"
do
    local f = io.open(watermark_path, "r")
    if f then
        f:close()
    else
        local f2 = io.open("icons/minicut.png", "r")
        if f2 then
            f2:close()
            watermark_path = "icons/minicut.png"
        else
            watermark_path = "minicut.png"
        end
    end
end
local wm_scale = 0.25
local wm_opacity = 0.75
local corner = 3 -- 0: Top-Left, 1: Top-Right, 2: Bottom-Left, 3: Bottom-Right

function logic_callback()
    local total_dur = timeline.get_duration()
    if total_dur <= 0.0 then total_dur = 10.0 end

    -- Place image on track 0 from start to end of project
    local clip_id = timeline.place_image(watermark_path, 0, 0.0, total_dur)
    if not clip_id or clip_id == "" then
        minicut.set_status("Failed to load watermark image: " .. watermark_path)
        return
    end

    local x, y = 300.0, 200.0
    if corner == 0 then x, y = -300.0, -200.0
    elseif corner == 1 then x, y = 300.0, -200.0
    elseif corner == 2 then x, y = -300.0, 200.0
    elseif corner == 3 then x, y = 300.0, 200.0
    end

    clip.set_position(clip_id, x, y)
    clip.set_scale(clip_id, wm_scale)
    clip.set_opacity(clip_id, wm_opacity)

    minicut.set_status("Watermark overlay placed across full timeline!")
end

function choose_file()
    gui.file_dialog("Select Watermark Image", function(selected)
        if selected and selected ~= "" then
            watermark_path = selected
            minicut.set_status("Selected watermark: " .. selected)
        end
    end, "Image Files (*.png *.jpg *.jpeg *.bmp *.webp)|*.png;*.jpg;*.jpeg;*.bmp;*.webp|All Files (*.*)|*.*")
end

function plugin:init_panel()
    gui.text_colored(0.4, 0.7, 1.0, 1.0, "Watermark Overlay Tool")
    gui.separator()

    gui.text("Watermark Image:")
    watermark_path = gui.input_text("##wm_path", watermark_path)
    gui.same_line()
    gui.button("Browse...##wm_browse", choose_file)

    gui.text("Scale:")
    wm_scale = gui.slider_float("##wm_s", wm_scale, 0.05, 1.0)

    gui.text("Opacity:")
    wm_opacity = gui.slider_float("##wm_o", wm_opacity, 0.1, 1.0)

    gui.text("Corner Preset (0:TL, 1:TR, 2:BL, 3:BR):")
    corner = gui.slider_int("##wm_pos", corner, 0, 3)

    gui.spacing()
    gui.button("Apply Watermark Across Project", logic_callback)
end
