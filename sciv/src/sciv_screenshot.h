// sciv - ReXGlue Recompiled Project
//
// Guest-output capture: dumps the exact image the game handed to the presenter
// -- i.e. the guest framebuffer *before* the emulator's present-stage scaling
// (bilinear/CAS/FSR) -- to a PNG next to the executable (via lodepng).
//
// A general-purpose graphics-diagnostic tool: a 1:1 capture of the handed-off
// buffer shows its true dimensions and contents independent of present-stage
// scaling, which is the first thing to check when comparing the PC output
// against a hardware reference capture. The dimensions are logged on every
// capture, so the log alone is informative.

#pragma once

namespace rex {
class Runtime;
}  // namespace rex

namespace sciv {

// Captures the current guest-output framebuffer and writes it to
// `sciv_guest_<timestamp>.png` in the executable folder. Logs the result
// (including the captured dimensions). Safe to call from the UI thread; a no-op
// with a warning if no frame has been presented yet.
void DumpGuestOutput(rex::Runtime* runtime);

}  // namespace sciv
