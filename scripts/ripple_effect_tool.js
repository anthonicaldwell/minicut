// ==============================================================================
// ripple_effect_tool.js (minicut SFX Pipeline Plugin - JavaScript)
// Pulls raw frame data from the active sequence clip, applies a ripple shader,
// and reinserts or creates a new SFX clip.
// ==============================================================================

var amplitude = 18.0;
var frequency = 0.06;
var phase = 0.0;
var decay = 0.002;
var useCustomCenter = false;
var centerX = 0.5;
var centerY = 0.5;
var createNewClip = false;
var targetTrack = 0;
var statusMessage = "Select a clip on the timeline and click 'Apply Ripple Shader'.";

// Register localizations for this plugin
locale.register("en", {
    ripple_title_js: "Ripple & Wave SFX Generator (JS)",
    ripple_amp_js: "Wave Amplitude",
    ripple_freq_js: "Wave Frequency",
    ripple_phase_js: "Phase Offset",
    ripple_decay_js: "Distance Decay",
    ripple_center_js: "Custom Center Origin",
    ripple_btn_js: "Apply Ripple Shader (JS)",
    ripple_create_new_js: "Create New Clip Instead of Overwriting",
    ripple_success_reinsert_js: "Successfully applied ripple shader to clip: ",
    ripple_success_new_js: "Created new ripple SFX clip: "
});

function applyRippleEffect() {
    var clipId = "active";
    if (typeof clip.get_selected_id === "function") {
        clipId = clip.get_selected_id() || "active";
    }

    minicut.log("[JS Ripple Tool] Fetching frame from clip: " + clipId);

    // 1. Pull raw frame data from sequence clip
    var frame = clip.get_frame(clipId);
    if (!frame || !frame.data) {
        statusMessage = "Failed to extract raw frame from clip!";
        minicut.set_status(statusMessage);
        return;
    }

    var cx = frame.width * (useCustomCenter ? centerX : 0.5);
    var cy = frame.height * (useCustomCenter ? centerY : 0.5);

    // 2. Prepare ripple shader and uniforms
    var sid = shader.create("ripple");
    var uniforms = {
        center_x: cx,
        center_y: cy,
        amplitude: amplitude,
        frequency: frequency,
        phase: phase,
        decay: decay
    };

    minicut.log("[JS Ripple Tool] Processing " + frame.width + "x" + frame.height + " frame...");

    // 3. Apply shader to frame
    var rippleFrame = shader.apply(sid, frame, uniforms);
    if (!rippleFrame) {
        statusMessage = "Shader execution failed!";
        minicut.set_status(statusMessage);
        return;
    }

    // 4. Reinsert into active clip or create new clip
    if (createNewClip) {
        var playhead = timeline.get_playhead();
        var newId = rippleFrame.create_clip(targetTrack, playhead, 5.0);
        if (newId && newId.length > 0) {
            statusMessage = locale("ripple_success_new_js", "Created new ripple SFX clip: ") + newId;
            minicut.set_status(statusMessage);
        } else {
            statusMessage = "Failed to create new clip from processed frame.";
            minicut.set_status(statusMessage);
        }
    } else {
        var ok = rippleFrame.reinsert(frame.clip_id);
        if (ok) {
            statusMessage = locale("ripple_success_reinsert_js", "Successfully applied ripple shader to clip: ") + frame.clip_id;
            minicut.set_status(statusMessage);
        } else {
            statusMessage = "Failed to reinsert frame back into clip!";
            minicut.set_status(statusMessage);
        }
    }
}

// Plugin UI Panel rendered in minicut's Script / Plugin dock
plugin.init_panel = function() {
    gui.text_colored(0.3, 0.8, 1.0, 1.0, locale("ripple_title_js", "Ripple & Wave SFX Generator (JS)"));
    gui.separator();

    gui.text(locale("ripple_amp_js", "Wave Amplitude") + ":");
    amplitude = gui.slider_float("##js_ripple_amp", amplitude, 0.0, 60.0);

    gui.text(locale("ripple_freq_js", "Wave Frequency") + ":");
    frequency = gui.slider_float("##js_ripple_freq", frequency, 0.01, 0.25);

    gui.text(locale("ripple_phase_js", "Phase Offset") + ":");
    phase = gui.slider_float("##js_ripple_phase", phase, 0.0, 6.28);

    gui.text(locale("ripple_decay_js", "Distance Decay") + ":");
    decay = gui.slider_float("##js_ripple_decay", decay, 0.0, 0.01);

    gui.spacing();
    useCustomCenter = gui.checkbox(locale("ripple_center_js", "Custom Center Origin") + "##js_ripple_center_toggle", useCustomCenter);
    if (useCustomCenter) {
        gui.text("Origin X (Normalized 0.0 - 1.0):");
        centerX = gui.slider_float("##js_ripple_cx", centerX, 0.0, 1.0);
        gui.text("Origin Y (Normalized 0.0 - 1.0):");
        centerY = gui.slider_float("##js_ripple_cy", centerY, 0.0, 1.0);
    }

    gui.separator();
    createNewClip = gui.checkbox(locale("ripple_create_new_js", "Create New Clip Instead of Overwriting") + "##js_ripple_create_new", createNewClip);
    if (createNewClip) {
        gui.text("Target Track:");
        var maxTracks = Math.max(0, timeline.get_track_count() - 1);
        targetTrack = gui.slider_int("##js_ripple_target_track", targetTrack, 0, maxTracks);
    }

    gui.spacing();
    gui.button(locale("ripple_btn_js", "Apply Ripple Shader (JS)") + "##js_ripple_apply_btn", applyRippleEffect);

    gui.separator();
    gui.text_disabled("Status: " + statusMessage);
};
