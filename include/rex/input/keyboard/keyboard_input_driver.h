/**
 * @file        rex/input/keyboard/keyboard_input_driver.h
 * @brief       Basic keyboard input driver - maps the keyboard to an Xbox 360
 *              controller. Keyboard only (no mouse); drives the digital controls
 *              plus the right analog stick. A simple foundation to expand on.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#pragma once

#include <rex/input/input_driver.h>
#include <rex/ui/window_listener.h>

#include <cstdint>
#include <mutex>
#include <string>

namespace rex::input::keyboard {

// One keyboard "slot": the set of cvar accessors a KeyboardInputDriver reads
// each frame. Bundling them as function pointers (each member is the accessor
// the REXCVAR_GET macro expands to) lets two independent instances -- e.g. two
// local players sharing one physical keyboard -- each point at their own cvars
// while keeping the fast direct-storage read path. The two predefined sets are
// Keyboard1Bindings() (player 1, enabled by default) and Keyboard2Bindings()
// (player 2, disabled by default); see the .cpp.
struct KeyboardBindings {
  bool& (*enabled)();         // keyboard_mode  / keyboard2_mode
  int32_t& (*user_index)();   // keyboard_user_index / keyboard2_user_index
  std::string& (*dpad_up)();
  std::string& (*dpad_down)();
  std::string& (*dpad_left)();
  std::string& (*dpad_right)();
  std::string& (*button_x)();
  std::string& (*button_a)();
  std::string& (*button_b)();
  std::string& (*button_y)();
  std::string& (*lshoulder)();
  std::string& (*rshoulder)();
  std::string& (*ltrigger)();
  std::string& (*rtrigger)();
  std::string& (*start)();
  std::string& (*back)();
  // Right analog stick (digital keys emulate a fully-deflected stick). Unbound
  // by default in the SDK; games opt in via their config (SCIV maps I/J/K/L).
  std::string& (*rstick_up)();
  std::string& (*rstick_down)();
  std::string& (*rstick_left)();
  std::string& (*rstick_right)();
};

// The cvar sets for the two keyboard slots (storage defined in the .cpp).
const KeyboardBindings& Keyboard1Bindings();
const KeyboardBindings& Keyboard2Bindings();

// Maps keyboard keys to a single emulated Xbox 360 gamepad. Unlike the MnK
// driver this does no mouse handling and drives the digital controls the game
// uses (D-pad + face buttons + shoulders/triggers + Start/Back) plus the right
// analog stick. Bindings are rebindable via the `kb_*`/`kb2_*` cvars (see the
// .cpp); defaults match an arcade-style layout. Each instance is bound to one
// KeyboardBindings slot at construction so two drivers can share the keyboard
// while driving different controller ports.
class KeyboardInputDriver final : public InputDriver,
                                  public rex::ui::WindowInputListener,
                                  public rex::ui::WindowListener {
 public:
  // `bindings` selects which cvar set this instance reads; it must outlive the
  // driver (the predefined sets are function-local statics, so this holds).
  KeyboardInputDriver(rex::ui::Window* window, size_t window_z_order,
                      const KeyboardBindings& bindings = Keyboard1Bindings());
  ~KeyboardInputDriver() override;

  X_STATUS Setup() override;

  X_RESULT GetCapabilities(uint32_t user_index, uint32_t flags,
                           X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT GetState(uint32_t user_index, X_INPUT_STATE* out_state) override;
  X_RESULT SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) override;
  X_RESULT GetKeystroke(uint32_t user_index, uint32_t flags,
                        X_INPUT_KEYSTROKE* out_keystroke) override;

  void OnWindowAvailable(rex::ui::Window* window) override;

  // WindowInputListener
  void OnKeyDown(rex::ui::KeyEvent& e) override;
  void OnKeyUp(rex::ui::KeyEvent& e) override;

  // WindowListener
  void OnClosing(rex::ui::UIEvent& e) override;
  void OnLostFocus(rex::ui::UISetupEvent& e) override;
  void OnGotFocus(rex::ui::UISetupEvent& e) override;

 private:
  uint32_t UserIndex() const;
  bool IsEnabled() const;
  void SetKeyState(uint16_t vk, bool down);

  // Collapses the currently-held keys into an internal keyfield (one bit per
  // emulated control; see kKeyfieldBits in the .cpp). Caller must hold the lock.
  uint32_t ComputeKeyfield() const;

  // The cvar slot this instance reads (player 1 or player 2). Never null.
  const KeyboardBindings* bindings_;

  rex::ui::Window* attached_window_ = nullptr;

  std::mutex state_mutex_;
  bool key_down_[256] = {};

  // Keystroke generation state. Menus read input via XamInputGetKeystroke ->
  // GetKeystroke (not GetState), so keystrokes are derived there by edge-
  // detecting against this mask, with auto-repeat for held directions/buttons.
  enum class RepeatState { kIdle, kWaiting, kRepeating };
  uint32_t last_ks_keyfield_ = 0;
  RepeatState ks_repeat_state_ = RepeatState::kIdle;
  uint32_t ks_repeat_time_ = 0;
  uint8_t ks_repeat_bit_ = 0;

  bool has_focus_ = true;
  uint32_t packet_number_ = 0;
};

}  // namespace rex::input::keyboard
