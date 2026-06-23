// sciv - ReXGlue Recompiled Project
//
// Tiny INI-style settings read at startup from `sciv.ini`, located in the
// working directory (the same folder the SDK uses for `<name>.toml` and
// `logs/`).
//
// Design contract:
//   * Running with no INI is fine -- a default INI is written on first run.
//   * A missing file, missing section, or missing key all fall back to the
//     compiled-in defaults (the `Config` member initializers below).
//   * The format is forgiving: `[Section]` headers, `Key = Value` pairs,
//     `//`, `;` and `#` line comments, blank lines, and case-insensitive
//     section/key names.
//
// Kept deliberately minimal: only settings whose underlying SDK support exists
// in the clean baseline are exposed. Game-specific knobs (MSXX's trial/license,
// pixel-art upscale filters, keyboard-as-controller binds) depended on SDK fork
// changes that SCIV does not yet carry, and are added here as that support lands.
// Naming follows the MSXX lesson: `sciv_settings.*`, never `sciv_config.*`, to
// avoid colliding with the generated/codegen config names.

#pragma once

#include <filesystem>

namespace sciv {

// Parsed settings, pre-populated with the defaults used when a value is absent.
struct Config {
  // [System]
  //
  // Portable = True : keep this install self-contained -- the SDK's writable
  //                   data (user_data_root, the shader cache, and the runtime's
  //                   `<name>.toml`) lives in the working directory `./` instead
  //                   of the per-user platform folders (default). Combined with
  //                   game_data_root defaulting to `./`, the whole thing can be
  //                   dropped on a USB stick and run in place.
  // Portable = False: use the SDK's platform defaults (e.g. %USERPROFILE% on
  //                   Windows) for that writable data.
  //
  // Note `sciv.ini` itself always lives in `./` -- it is the bootstrap file that
  // carries this flag, so its location can't depend on it. Applied in
  // OnConfigurePaths (which runs before the runtime's paths are locked in).
  bool portable = true;

  // [Game]
  //
  // SkipLogos = True : fast-forward the five boot logos (BANDAI NAMCO, Havok,
  //                    CRI, CRIWARE, Project Soul) that play before the title.
  //                    Each normally fades in/holds/fades out for ~4 s (~22 s
  //                    total). The logos are drawn by the generic modal display
  //                    loop (guest sub_824307F0) for the boot-intro modal (state
  //                    0x82891D50); their fades+holds are paced by the guest
  //                    time-base (mftb). Two mid-ASM hooks accelerate the guest
  //                    clock while that modal runs and restore it to 1x the
  //                    instant the opening cinematic (D:\event\scene\...) loads,
  //                    so the logos blow past quickly but the cinematic plays at
  //                    normal speed. Tunable via env SCIV_LOGO_TIMESCALE. See
  //                    sciv_hooks.cpp / sciv_fixups.toml.
  // SkipLogos = False: play the logos at normal speed.
  bool skip_logos = true;
};

// Loads `sciv.ini` from the working directory, writing a commented default file
// if none exists. Logs what it resolved. Safe to call once at startup (after
// logging is up). Returns a reference to the process-wide config.
const Config& LoadConfig();

// Returns the config resolved by LoadConfig() (defaults if never loaded).
const Config& config();

// Re-writes `sciv.ini` from the live config, so any keys absent from an older
// INI get populated (and comments refreshed) while keeping the values the user
// already set. Written atomically (temp file + rename). Intended to be called
// once on shutdown. Logs the outcome; never throws.
void SaveConfig();

// Resolved path of the INI file (working directory / "sciv.ini").
std::filesystem::path ConfigPath();

}  // namespace sciv
