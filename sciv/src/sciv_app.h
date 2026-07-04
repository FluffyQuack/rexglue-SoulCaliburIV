// sciv - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <filesystem>

#include <rex/cvar.h>
#include <rex/logging/api.h>
#include <rex/logging/macros.h>
#include <rex/rex_app.h>
#include <rex/ui/keybinds.h>

#include "sciv_crash.h"
#include "sciv_screenshot.h"
#include "sciv_settings.h"
#include "sciv_timing.h"

class ScivApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<ScivApp>(new ScivApp(ctx, "sciv",
        PPCImageConfig));
  }

  // Install last-chance native-crash diagnostics as soon as the logger is up,
  // so an access violation in recompiled code yields a symbolized backtrace
  // (the recompiled `sub_<guestaddr>` frames) instead of a silent exit. Also
  // raise the host timer resolution to 1 ms so guest frame-pacing waits don't
  // overshoot the default ~15.6 ms quantum (see sciv_timing.h). LoadConfig is
  // idempotent -- OnConfigurePaths ran first and already read the INI; this just
  // logs the resolved settings.
  void OnPostInitLogging() override {
    sciv::InstallCrashHandler();
    sciv::RaiseTimerResolution();
    const sciv::Config& cfg = sciv::LoadConfig();

    // Keyboard-as-controller bindings. Drive the SDK keyboard input driver's
    // cvars from the INI [Keyboard1]/[Keyboard2] sections (key NAMES, validated
    // on load). The drivers read these per-frame, so setting them here is in
    // time. [Keyboard2] is a second local player on the same keyboard and is
    // disabled by default. See sciv_settings.h and
    // src/input/keyboard/keyboard_input_driver.cpp.
    rex::cvar::SetFlagByName("keyboard_mode", cfg.keyboard_enabled ? "true" : "false");
    rex::cvar::SetFlagByName("kb_dpad_up", cfg.kb_dpad_up);
    rex::cvar::SetFlagByName("kb_dpad_down", cfg.kb_dpad_down);
    rex::cvar::SetFlagByName("kb_dpad_left", cfg.kb_dpad_left);
    rex::cvar::SetFlagByName("kb_dpad_right", cfg.kb_dpad_right);
    rex::cvar::SetFlagByName("kb_a", cfg.kb_button_a);
    rex::cvar::SetFlagByName("kb_b", cfg.kb_button_b);
    rex::cvar::SetFlagByName("kb_x", cfg.kb_button_x);
    rex::cvar::SetFlagByName("kb_y", cfg.kb_button_y);
    rex::cvar::SetFlagByName("kb_lshoulder", cfg.kb_lshoulder);
    rex::cvar::SetFlagByName("kb_rshoulder", cfg.kb_rshoulder);
    rex::cvar::SetFlagByName("kb_ltrigger", cfg.kb_ltrigger);
    rex::cvar::SetFlagByName("kb_rtrigger", cfg.kb_rtrigger);
    rex::cvar::SetFlagByName("kb_start", cfg.kb_start);
    rex::cvar::SetFlagByName("kb_back", cfg.kb_back);
    rex::cvar::SetFlagByName("kb_rstick_up", cfg.kb_rstick_up);
    rex::cvar::SetFlagByName("kb_rstick_down", cfg.kb_rstick_down);
    rex::cvar::SetFlagByName("kb_rstick_left", cfg.kb_rstick_left);
    rex::cvar::SetFlagByName("kb_rstick_right", cfg.kb_rstick_right);
    REXLOG_INFO("keyboard1: Enabled = {} | Dpad U/D/L/R = {}/{}/{}/{} | A/B/X/Y = {}/{}/{}/{} | "
                "LB/RB = {}/{} | LT/RT = {}/{} | Start/Back = {}/{} | RStick U/D/L/R = {}/{}/{}/{}",
                cfg.keyboard_enabled ? "True" : "False", cfg.kb_dpad_up, cfg.kb_dpad_down,
                cfg.kb_dpad_left, cfg.kb_dpad_right, cfg.kb_button_a, cfg.kb_button_b,
                cfg.kb_button_x, cfg.kb_button_y, cfg.kb_lshoulder, cfg.kb_rshoulder,
                cfg.kb_ltrigger, cfg.kb_rtrigger, cfg.kb_start, cfg.kb_back, cfg.kb_rstick_up,
                cfg.kb_rstick_down, cfg.kb_rstick_left, cfg.kb_rstick_right);

    rex::cvar::SetFlagByName("keyboard2_mode", cfg.keyboard2_enabled ? "true" : "false");
    rex::cvar::SetFlagByName("kb2_dpad_up", cfg.kb2_dpad_up);
    rex::cvar::SetFlagByName("kb2_dpad_down", cfg.kb2_dpad_down);
    rex::cvar::SetFlagByName("kb2_dpad_left", cfg.kb2_dpad_left);
    rex::cvar::SetFlagByName("kb2_dpad_right", cfg.kb2_dpad_right);
    rex::cvar::SetFlagByName("kb2_a", cfg.kb2_button_a);
    rex::cvar::SetFlagByName("kb2_b", cfg.kb2_button_b);
    rex::cvar::SetFlagByName("kb2_x", cfg.kb2_button_x);
    rex::cvar::SetFlagByName("kb2_y", cfg.kb2_button_y);
    rex::cvar::SetFlagByName("kb2_lshoulder", cfg.kb2_lshoulder);
    rex::cvar::SetFlagByName("kb2_rshoulder", cfg.kb2_rshoulder);
    rex::cvar::SetFlagByName("kb2_ltrigger", cfg.kb2_ltrigger);
    rex::cvar::SetFlagByName("kb2_rtrigger", cfg.kb2_rtrigger);
    rex::cvar::SetFlagByName("kb2_start", cfg.kb2_start);
    rex::cvar::SetFlagByName("kb2_back", cfg.kb2_back);
    rex::cvar::SetFlagByName("kb2_rstick_up", cfg.kb2_rstick_up);
    rex::cvar::SetFlagByName("kb2_rstick_down", cfg.kb2_rstick_down);
    rex::cvar::SetFlagByName("kb2_rstick_left", cfg.kb2_rstick_left);
    rex::cvar::SetFlagByName("kb2_rstick_right", cfg.kb2_rstick_right);
    REXLOG_INFO("keyboard2: Enabled = {} | Dpad U/D/L/R = {}/{}/{}/{} | A/B/X/Y = {}/{}/{}/{} | "
                "LB/RB = {}/{} | LT/RT = {}/{} | Start/Back = {}/{}",
                cfg.keyboard2_enabled ? "True" : "False", cfg.kb2_dpad_up, cfg.kb2_dpad_down,
                cfg.kb2_dpad_left, cfg.kb2_dpad_right, cfg.kb2_button_a, cfg.kb2_button_b,
                cfg.kb2_button_x, cfg.kb2_button_y, cfg.kb2_lshoulder, cfg.kb2_rshoulder,
                cfg.kb2_ltrigger, cfg.kb2_rtrigger, cfg.kb2_start, cfg.kb2_back);
  }

  // Register project-specific keybinds. Called by the SDK right after the
  // built-in overlay binds (F3/F4/Backtick) are set up and the presenter is
  // live, so the presenter is reachable from the callback via runtime().
  //
  // F12 -> dump the guest-output framebuffer (pre present-scaling) to a PNG next
  // to the exe (via lodepng). General-purpose screenshot + a bring-up diagnostic
  // for comparing PC output against hardware captures: see sciv_screenshot.h.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* /*drawer*/) override {
    rex::ui::RegisterBind("bind_guest_capture", "F12", "Save guest-output screenshot (PNG)",
                          [this] { sciv::DumpGuestOutput(runtime()); });
  }

  // On quit, re-write sciv.ini from the live config so an INI from an older
  // build gets any newly-added keys populated (and comments refreshed), while
  // keeping whatever the user already set. See sciv_settings.h.
  void OnShutdown() override { sciv::SaveConfig(); }

  // Resolve the writable-data paths before the runtime locks them in. Two
  // project conventions (see sciv_settings.h):
  //   1. If --game_data_root was not given, default it to the working directory
  //      (`./`) so the game runs against the data sitting next to it.
  //   2. [System] Portable (default True) keeps the install self-contained: the
  //      SDK's user data, shader cache, and `<name>.toml` go in `./` instead of
  //      the per-user platform folders. This is the earliest hook that can read
  //      the INI (LoadConfig is idempotent; OnPostInitLogging reuses the result).
  void OnConfigurePaths(rex::PathConfig& paths) override {
    namespace fs = std::filesystem;

    // SCIV-specific GPU default: enable CPU readback of render-to-texture
    // resolves. SCIV's bloom bright-pass otherwise blows out (effect is far too
    // strong) because the resolved target it samples is never read back to guest
    // memory. The engine default is "none"; we raise it to "fast" only for this
    // game. This runs before rex::cvar::LoadConfig (see ReXApp init order), so a
    // readback_resolve key in sciv.toml still overrides it. This is the pragmatic
    // fix -- a more correct (resolve-path-accurate) solution can replace it later.
    rex::cvar::SetFlagByName("readback_resolve", "fast");

    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (ec) return;  // can't resolve `./`; leave SDK defaults untouched

    if (paths.game_data_root.empty()) {
      paths.game_data_root = cwd;
      REXLOG_INFO("paths: --game_data_root not given; defaulting to working dir {}", cwd.string());
    }

    if (sciv::LoadConfig().portable) {
      paths.user_data_root = cwd;
      paths.cache_root = cwd / "cache";
      paths.config_path = cwd / (GetName() + ".toml");
      REXLOG_INFO("paths: [System] Portable = True; writable data in working dir {}", cwd.string());
    }
  }

  // Override virtual hooks for customization:
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostSetup() override {}
};
