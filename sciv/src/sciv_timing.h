// sciv - ReXGlue Recompiled Project
//
// Host timer-resolution fix for frame pacing.
//
// 360 titles throttle their main loop with guest timed waits
// (KeWaitForSingleObject with a timeout / KeDelayExecutionThread), which the SDK
// lowers onto Win32 Sleep / WaitForSingleObjectEx with millisecond timeouts.
// Those honour the *system timer resolution*, which defaults to the scheduler
// quantum (~15.6 ms). A ~16.6 ms one-frame wait can then overshoot to the next
// ~15.6 ms boundary, randomly landing at ~31 ms -- so the game advances less
// often than once per displayed frame (jitter, slowdown). Raising the resolution
// to 1 ms (timeBeginPeriod) tightens those waits so they land within ~1 ms of the
// requested interval. This was the exact MSXX slowdown fix; whether SCIV's pacing
// needs it is confirmed during bring-up, but 1 ms resolution is a safe default.
//
// Project-side glue only -- no SDK fork. Windows-only (matches the build).

#pragma once

namespace sciv {

// Request 1 ms system timer resolution and log the outcome (so a run log
// confirms whether the fix took). Call once, after logging is up
// (OnPostInitLogging).
void RaiseTimerResolution();

}  // namespace sciv
