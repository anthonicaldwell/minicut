// ==============================================================================
// quick_fade_js.js (Autorun JavaScript Script)
// Demonstrates Duktape JavaScript Engine integration, Dock icon, and Keyframing
// ==============================================================================

var fadeInDuration = 1.0;
var fadeOutDuration = 1.0;
var maxOpacity = 1.0;

function applyFadeAnimation() {
    var clipId = timeline.get_selected_clip();
    if (!clipId || clipId === "") {
        minicut.set_status("No clip selected on timeline! Select a clip first.");
        return;
    }

    var timeInfo = clip.get_time(clipId);
    var duration = timeInfo && timeInfo.length > 1 ? timeInfo[1] : 5.0;

    minicut.push_undo();
    timeline.clear_keyframes(clipId, "opacity");

    // Fade-in keyframes
    timeline.add_keyframe(clipId, "opacity", 0.0, 0.0);
    timeline.add_keyframe(clipId, "opacity", Math.min(fadeInDuration, duration * 0.5), maxOpacity);

    // Fade-out keyframes
    if (duration > fadeOutDuration) {
        timeline.add_keyframe(clipId, "opacity", duration - fadeOutDuration, maxOpacity);
        timeline.add_keyframe(clipId, "opacity", duration, 0.0);
    }

    minicut.log("JS Quick Fade applied to clip: " + clipId);
    minicut.set_status("Applied fade animation to clip " + clipId);
}

plugin.init_panel = function() {
    gui.text_colored(0.95, 0.75, 0.25, 1.0, "Quick Fade & Transition (JS)");
    gui.separator();

    var selected = timeline.get_selected_clip();
    if (selected && selected !== "") {
        gui.text("Target Clip: " + selected);
    } else {
        gui.text_disabled("Target Clip: (None selected on timeline)");
    }
    gui.separator();

    gui.text("Fade In Span (seconds):");
    fadeInDuration = gui.slider_float("##fade_in_js", fadeInDuration, 0.1, 5.0);

    gui.text("Fade Out Span (seconds):");
    fadeOutDuration = gui.slider_float("##fade_out_js", fadeOutDuration, 0.1, 5.0);

    gui.text("Peak Opacity:");
    maxOpacity = gui.slider_float("##max_op_js", maxOpacity, 0.1, 1.0);

    gui.spacing();
    gui.separator();
    gui.button("Apply Fade to Selected Clip", applyFadeAnimation);
};
