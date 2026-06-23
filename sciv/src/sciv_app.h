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
    sciv::LoadConfig();
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
