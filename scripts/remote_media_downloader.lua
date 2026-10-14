-- ==============================================================================
-- minicut Script: Remote Media Downloader
-- Demonstrates net.download(), net.get(), and timeline placement with file dialog
-- ==============================================================================

local remote_url = "https://picsum.photos/1280/720"
local local_filename = "/tmp/downloaded_media.jpg"
local target_track = 0
local insert_duration = 5.0
local net_status = "Idle"

function download_and_insert()
    net_status = "Downloading..."
    minicut.set_status("Downloading media from " .. remote_url .. "...")
    
    local ok, err = net.download(remote_url, local_filename, { timeout = 30 })
    if ok then
        net_status = "Download complete. Placing on timeline..."
        minicut.push_undo()
        local cid = timeline.place_clip(local_filename, target_track, timeline.get_playhead(), insert_duration)
        if cid then
            net_status = "Placed clip on track " .. target_track
            minicut.set_status("Placed remote media on timeline!")
        else
            net_status = "Failed to place media onto track"
        end
    else
        net_status = "Download error: " .. tostring(err)
        minicut.set_status("Download failed: " .. tostring(err))
    end
end

function choose_save_destination()
    gui.file_dialog("Save Destination File", function(selected)
        if selected and selected ~= "" then
            local_filename = selected
            minicut.set_status("Set download target: " .. selected)
        end
    end, "Image Files (*.jpg *.png *.webp)|*.jpg;*.png;*.webp|All Files (*.*)|*.*")
end

function plugin:init_panel()
    gui.text("Remote Media Downloader")
    gui.separator()
    
    remote_url = gui.input_text("Remote Media URL", remote_url)
    local_filename = gui.input_text("Save Destination", local_filename)
    gui.same_line()
    gui.button("Browse...##dl_browse", choose_save_destination)

    target_track = gui.slider_int("Track Lane", target_track, 0, 4)
    insert_duration = gui.slider_float("Duration (s)", insert_duration, 1.0, 30.0)
    
    if gui.button("Download & Insert At Playhead", download_and_insert) then
    end
    
    gui.separator()
    gui.text_disabled("Status: " .. net_status)
end
