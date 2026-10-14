// ==============================================================================
// media_browser_tool.js (Menu Script)
// Demonstrates Duktape JavaScript Engine, gui.file_dialog, and timeline placement
// ==============================================================================

var mediaFilePath = "test_sample.png";
var targetTrack = 0;
var clipDuration = 4.0;
var posX = 0.0;
var posY = 0.0;
var clipScale = 1.0;
var statusMessage = "Ready";

function browseMediaFile() {
    gui.file_dialog(
        "Select Media File (JS)",
        function(selectedPath) {
            if (selectedPath && selectedPath.length > 0) {
                mediaFilePath = selectedPath;
                statusMessage = "Selected: " + selectedPath;
                minicut.set_status("Selected file: " + selectedPath);
            }
        },
        "Media Files (*.png *.jpg *.jpeg *.mp4 *.mov *.mkv *.ogv *.ogg)|*.png;*.jpg;*.jpeg;*.mp4;*.mov;*.mkv;*.ogv;*.ogg|All Files (*.*)|*.*"
    );
}

function placeMediaAtPlayhead() {
    if (!mediaFilePath || mediaFilePath.length === 0) {
        statusMessage = "Please select a media file first!";
        minicut.set_status(statusMessage);
        return;
    }

    var playhead = timeline.get_playhead();
    minicut.log("Placing media via JS at playhead: " + playhead);
    minicut.push_undo();

    var clipId = timeline.place_clip(mediaFilePath, targetTrack, playhead, clipDuration);
    if (clipId && clipId.length > 0) {
        clip.set_position(clipId, posX, posY);
        clip.set_scale(clipId, clipScale);
        statusMessage = "Placed clip " + clipId + " at " + playhead.toFixed(2) + "s";
        minicut.set_status(statusMessage);
    } else {
        statusMessage = "Failed to place media: " + mediaFilePath;
        minicut.set_status(statusMessage);
    }
}

plugin.init_panel = function() {
    gui.text_colored(0.35, 0.75, 1.0, 1.0, "Media Browser & Placer (JS)");
    gui.separator();

    gui.text("Media File:");
    mediaFilePath = gui.input_text("##js_media_path", mediaFilePath);
    gui.same_line();
    gui.button("Browse...##js_browse", browseMediaFile);

    gui.text("Track Lane:");
    var maxTracks = Math.max(0, timeline.get_track_count() - 1);
    targetTrack = gui.slider_int("##js_track", targetTrack, 0, maxTracks);

    gui.text("Duration (sec):");
    clipDuration = gui.slider_float("##js_dur", clipDuration, 0.5, 30.0);

    gui.separator();
    gui.text("Transform Coordinates:");
    posX = gui.drag_float("Pos X##js_px", posX, 2.0, -1000.0, 1000.0);
    posY = gui.drag_float("Pos Y##js_py", posY, 2.0, -1000.0, 1000.0);
    clipScale = gui.slider_float("Scale##js_sc", clipScale, 0.1, 4.0);

    gui.separator();
    gui.spacing();
    gui.button("Place Media on Timeline", placeMediaAtPlayhead);

    gui.separator();
    gui.text_disabled("Status: " + statusMessage);
};
