-- ==============================================================================
-- minicut Script: Project Auto-Saver & Snapshot Manager
-- Demonstrates project_save(), project_load(), and gui.file_dialog
-- ==============================================================================

local snapshot_name = "snapshot_backup.minicut"
local auto_save_status = "Ready"

function save_current_snapshot()
    minicut.push_undo()
    local ok, res = project_save(snapshot_name)
    if ok then
        auto_save_status = "Saved: " .. snapshot_name
        minicut.set_status("Project snapshot saved: " .. snapshot_name)
    else
        auto_save_status = "Error: " .. tostring(res)
        minicut.set_status("Snapshot save failed!")
    end
end

function restore_last_snapshot()
    local ok, res = project_load(snapshot_name)
    if ok then
        auto_save_status = "Restored: " .. snapshot_name
        minicut.set_status("Project restored from snapshot!")
    else
        auto_save_status = "Restore failed: " .. tostring(res)
        minicut.set_status("Restore failed: " .. tostring(res))
    end
end

function choose_snapshot_file()
    gui.file_dialog("Select Project Snapshot", function(selected)
        if selected and selected ~= "" then
            snapshot_name = selected
            minicut.set_status("Selected snapshot path: " .. selected)
        end
    end, "Minicut Projects (*.minicut)|*.minicut|All Files (*.*)|*.*")
end

function plugin:init_panel()
    gui.text("Project Snapshot & Auto-Save")
    gui.separator()
    gui.text_colored("Current Project: " .. project.get_name(), 0.24, 0.68, 0.91)
    
    snapshot_name = gui.input_text("Snapshot Filename", snapshot_name)
    gui.same_line()
    gui.button("Browse...##snap_browse", choose_snapshot_file)
    
    if gui.button("Save Snapshot", save_current_snapshot) then
    end
    
    gui.same_line()
    
    if gui.button("Restore Snapshot", restore_last_snapshot) then
    end
    
    gui.separator()
    gui.text_disabled("Status: " .. auto_save_status)
end
