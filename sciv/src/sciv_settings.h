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
#include <string>

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

  // [Keyboard1] / [Keyboard2]
  //
  // Basic keyboard-as-controller support. [Keyboard1] drives player 1's emulated
  // Xbox 360 pad and [Keyboard2] a second player's, both alongside any real
  // controller and both reading the same physical keyboard -- so for local co-op
  // their bindings must not overlap. Each binding is a key NAME (the same
  // vocabulary the SDK keybinds use), NOT an SDL keycode: on Windows the game
  // window is a native Win32 window and key presses arrive as Win32 virtual keys.
  // Accepted names include the letters A-Z, the digits 0-9, F1-F24, and: Up,
  // Down, Left, Right, Return, Escape, Space, Tab, Backspace, Delete, Insert,
  // Home, End, PageUp, PageDown, Shift, Control, Alt, Numpad0-Numpad9,
  // NumpadEnter, NumpadPlus, NumpadMinus, NumpadStar, NumpadSlash. An unknown
  // name keeps that binding's default (and is logged); a blank value unbinds it.
  // [Keyboard1] drives the SDK `keyboard_mode` / `kb_*` cvars and [Keyboard2] the
  // `keyboard2_mode` / `kb2_*` cvars in OnPostInitLogging.
  //
  // Player 1 default layout:
  //   Up/Down/Left/Right -> D-pad     Space -> A   D -> X   S -> Y   A -> B
  //   Shift -> LT   Z -> RT   X -> LB   C -> RB
  //   Return -> Start   Backspace -> Back/Select   IJKL -> Right stick
  bool keyboard_enabled = true;
  std::string kb_dpad_up = "Up";
  std::string kb_dpad_down = "Down";
  std::string kb_dpad_left = "Left";
  std::string kb_dpad_right = "Right";
  std::string kb_button_a = "Space";
  std::string kb_button_x = "D";
  std::string kb_button_y = "S";
  std::string kb_button_b = "A";
  std::string kb_ltrigger = "Shift";
  std::string kb_rtrigger = "Z";
  std::string kb_lshoulder = "X";
  std::string kb_rshoulder = "C";
  std::string kb_start = "Return";
  std::string kb_back = "Backspace";
  std::string kb_rstick_up = "I";
  std::string kb_rstick_down = "K";
  std::string kb_rstick_left = "J";
  std::string kb_rstick_right = "L";

  // Player 2. Disabled by default; defaults to the numpad so it does not collide
  // with player 1's keys when both share one keyboard. The numpad has no keys to
  // spare for a right stick, so P2's right-stick binds are unbound by default.
  //   Numpad8/2/4/6 -> D-pad     Numpad1 -> A   Numpad7 -> X   Numpad9 -> Y
  //   Numpad3 -> B   NumpadSlash -> LB   NumpadStar -> RB
  //   NumpadMinus -> LT   NumpadPlus -> RT   NumpadEnter -> Start   Numpad0 -> Back
  bool keyboard2_enabled = false;
  std::string kb2_dpad_up = "Numpad8";
  std::string kb2_dpad_down = "Numpad2";
  std::string kb2_dpad_left = "Numpad4";
  std::string kb2_dpad_right = "Numpad6";
  std::string kb2_button_a = "Numpad1";
  std::string kb2_button_x = "Numpad7";
  std::string kb2_button_y = "Numpad9";
  std::string kb2_button_b = "Numpad3";
  std::string kb2_lshoulder = "NumpadSlash";
  std::string kb2_rshoulder = "NumpadStar";
  std::string kb2_ltrigger = "NumpadMinus";
  std::string kb2_rtrigger = "NumpadPlus";
  std::string kb2_start = "NumpadEnter";
  std::string kb2_back = "Numpad0";
  std::string kb2_rstick_up = "";
  std::string kb2_rstick_down = "";
  std::string kb2_rstick_left = "";
  std::string kb2_rstick_right = "";
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
