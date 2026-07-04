/**
 * @file        input/keyboard/keyboard_input_driver.cpp
 * @brief       Basic keyboard -> Xbox 360 controller input driver.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */
#include <rex/input/keyboard/keyboard_input_driver.h>

#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/logging.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>

#include <cstring>
#include <iterator>
#include <string>

// --- Keyboard 1 (player 1) ----------------------------------------------------
REXCVAR_DEFINE_BOOL(keyboard_mode, true, "Input",
                    "Enable basic keyboard -> controller emulation (player 1)");
REXCVAR_DEFINE_INT32(keyboard_user_index, 0, "Input",
                     "Controller slot (0-3) keyboard 1 drives")
    .range(0, 3);

// Default layout: D-pad on the arrow keys, face buttons on WASD, shoulders on
// Q/E, triggers on 1/3, Start/Back on Return/Backspace, right stick on IJKL. All
// rebindable. The left analog stick is not emulated.
REXCVAR_DEFINE_STRING(kb_dpad_up, "Up", "Input/Keybinds/Keyboard", "P1 D-pad up");
REXCVAR_DEFINE_STRING(kb_dpad_down, "Down", "Input/Keybinds/Keyboard", "P1 D-pad down");
REXCVAR_DEFINE_STRING(kb_dpad_left, "Left", "Input/Keybinds/Keyboard", "P1 D-pad left");
REXCVAR_DEFINE_STRING(kb_dpad_right, "Right", "Input/Keybinds/Keyboard", "P1 D-pad right");
REXCVAR_DEFINE_STRING(kb_x, "A", "Input/Keybinds/Keyboard", "P1 X button");
REXCVAR_DEFINE_STRING(kb_a, "S", "Input/Keybinds/Keyboard", "P1 A button");
REXCVAR_DEFINE_STRING(kb_b, "D", "Input/Keybinds/Keyboard", "P1 B button");
REXCVAR_DEFINE_STRING(kb_y, "W", "Input/Keybinds/Keyboard", "P1 Y button");
REXCVAR_DEFINE_STRING(kb_lshoulder, "Q", "Input/Keybinds/Keyboard", "P1 Left shoulder (LB)");
REXCVAR_DEFINE_STRING(kb_rshoulder, "E", "Input/Keybinds/Keyboard", "P1 Right shoulder (RB)");
REXCVAR_DEFINE_STRING(kb_ltrigger, "1", "Input/Keybinds/Keyboard", "P1 Left trigger (LT)");
REXCVAR_DEFINE_STRING(kb_rtrigger, "3", "Input/Keybinds/Keyboard", "P1 Right trigger (RT)");
REXCVAR_DEFINE_STRING(kb_start, "Return", "Input/Keybinds/Keyboard", "P1 Start button");
REXCVAR_DEFINE_STRING(kb_back, "Backspace", "Input/Keybinds/Keyboard", "P1 Back/Select button");
REXCVAR_DEFINE_STRING(kb_rstick_up, "I", "Input/Keybinds/Keyboard", "P1 Right stick up");
REXCVAR_DEFINE_STRING(kb_rstick_down, "K", "Input/Keybinds/Keyboard", "P1 Right stick down");
REXCVAR_DEFINE_STRING(kb_rstick_left, "J", "Input/Keybinds/Keyboard", "P1 Right stick left");
REXCVAR_DEFINE_STRING(kb_rstick_right, "L", "Input/Keybinds/Keyboard", "P1 Right stick right");

// --- Keyboard 2 (player 2) ----------------------------------------------------
// A second keyboard slot for local co-op on the same physical keyboard. Disabled
// by default; defaults to the numpad so it does not collide with player 1's keys.
REXCVAR_DEFINE_BOOL(keyboard2_mode, false, "Input",
                    "Enable a second keyboard -> controller emulation (player 2)");
REXCVAR_DEFINE_INT32(keyboard2_user_index, 1, "Input",
                     "Controller slot (0-3) keyboard 2 drives")
    .range(0, 3);

REXCVAR_DEFINE_STRING(kb2_dpad_up, "Numpad8", "Input/Keybinds/Keyboard", "P2 D-pad up");
REXCVAR_DEFINE_STRING(kb2_dpad_down, "Numpad2", "Input/Keybinds/Keyboard", "P2 D-pad down");
REXCVAR_DEFINE_STRING(kb2_dpad_left, "Numpad4", "Input/Keybinds/Keyboard", "P2 D-pad left");
REXCVAR_DEFINE_STRING(kb2_dpad_right, "Numpad6", "Input/Keybinds/Keyboard", "P2 D-pad right");
REXCVAR_DEFINE_STRING(kb2_x, "Numpad7", "Input/Keybinds/Keyboard", "P2 X button");
REXCVAR_DEFINE_STRING(kb2_a, "Numpad1", "Input/Keybinds/Keyboard", "P2 A button");
REXCVAR_DEFINE_STRING(kb2_b, "Numpad3", "Input/Keybinds/Keyboard", "P2 B button");
REXCVAR_DEFINE_STRING(kb2_y, "Numpad9", "Input/Keybinds/Keyboard", "P2 Y button");
REXCVAR_DEFINE_STRING(kb2_lshoulder, "NumpadSlash", "Input/Keybinds/Keyboard",
                      "P2 Left shoulder (LB)");
REXCVAR_DEFINE_STRING(kb2_rshoulder, "NumpadStar", "Input/Keybinds/Keyboard",
                      "P2 Right shoulder (RB)");
REXCVAR_DEFINE_STRING(kb2_ltrigger, "NumpadMinus", "Input/Keybinds/Keyboard",
                      "P2 Left trigger (LT)");
REXCVAR_DEFINE_STRING(kb2_rtrigger, "NumpadPlus", "Input/Keybinds/Keyboard",
                      "P2 Right trigger (RT)");
REXCVAR_DEFINE_STRING(kb2_start, "NumpadEnter", "Input/Keybinds/Keyboard", "P2 Start button");
REXCVAR_DEFINE_STRING(kb2_back, "Numpad0", "Input/Keybinds/Keyboard", "P2 Back/Select button");
REXCVAR_DEFINE_STRING(kb2_rstick_up, "", "Input/Keybinds/Keyboard", "P2 Right stick up");
REXCVAR_DEFINE_STRING(kb2_rstick_down, "", "Input/Keybinds/Keyboard", "P2 Right stick down");
REXCVAR_DEFINE_STRING(kb2_rstick_left, "", "Input/Keybinds/Keyboard", "P2 Right stick left");
REXCVAR_DEFINE_STRING(kb2_rstick_right, "", "Input/Keybinds/Keyboard", "P2 Right stick right");

namespace rex::input::keyboard {

using rex::ui::VirtualKey;

// The two cvar sets, wiring each KeyboardBindings member to the accessor the
// REXCVAR_GET macro expands to (FLAGS_<name>_storage_). Function-local statics so
// the references handed out via the const& outlive every driver instance.
const KeyboardBindings& Keyboard1Bindings() {
  static const KeyboardBindings b{
      &FLAGS_keyboard_mode_storage_,       &FLAGS_keyboard_user_index_storage_,
      &FLAGS_kb_dpad_up_storage_,          &FLAGS_kb_dpad_down_storage_,
      &FLAGS_kb_dpad_left_storage_,        &FLAGS_kb_dpad_right_storage_,
      &FLAGS_kb_x_storage_,                &FLAGS_kb_a_storage_,
      &FLAGS_kb_b_storage_,                &FLAGS_kb_y_storage_,
      &FLAGS_kb_lshoulder_storage_,        &FLAGS_kb_rshoulder_storage_,
      &FLAGS_kb_ltrigger_storage_,         &FLAGS_kb_rtrigger_storage_,
      &FLAGS_kb_start_storage_,            &FLAGS_kb_back_storage_,
      &FLAGS_kb_rstick_up_storage_,        &FLAGS_kb_rstick_down_storage_,
      &FLAGS_kb_rstick_left_storage_,      &FLAGS_kb_rstick_right_storage_,
  };
  return b;
}

const KeyboardBindings& Keyboard2Bindings() {
  static const KeyboardBindings b{
      &FLAGS_keyboard2_mode_storage_,      &FLAGS_keyboard2_user_index_storage_,
      &FLAGS_kb2_dpad_up_storage_,         &FLAGS_kb2_dpad_down_storage_,
      &FLAGS_kb2_dpad_left_storage_,       &FLAGS_kb2_dpad_right_storage_,
      &FLAGS_kb2_x_storage_,               &FLAGS_kb2_a_storage_,
      &FLAGS_kb2_b_storage_,               &FLAGS_kb2_y_storage_,
      &FLAGS_kb2_lshoulder_storage_,       &FLAGS_kb2_rshoulder_storage_,
      &FLAGS_kb2_ltrigger_storage_,        &FLAGS_kb2_rtrigger_storage_,
      &FLAGS_kb2_start_storage_,           &FLAGS_kb2_back_storage_,
      &FLAGS_kb2_rstick_up_storage_,       &FLAGS_kb2_rstick_down_storage_,
      &FLAGS_kb2_rstick_left_storage_,     &FLAGS_kb2_rstick_right_storage_,
  };
  return b;
}

// Auto-repeat timing for the keystroke path, matching the SDL pad driver so
// menu scrolling feels identical whether you use a keyboard or a controller.
static constexpr uint32_t kRepeatDelayMs = 400;
static constexpr uint32_t kRepeatRateMs = 100;

// Fully-deflected analog value for a digital right-stick key press.
static constexpr int16_t kThumbMax = 32767;
static constexpr int16_t kThumbMin = -32768;

// Internal keyfield: one bit per emulated control. The same bitmask drives both
// GetState (translated to an X_INPUT_STATE) and GetKeystroke (edge-detected into
// VK_PAD keystrokes). Keep the entries in sync with kKeyfieldBits below.
enum KbBit : uint32_t {
  kBitDpadUp = 1u << 0,
  kBitDpadDown = 1u << 1,
  kBitDpadLeft = 1u << 2,
  kBitDpadRight = 1u << 3,
  kBitStart = 1u << 4,
  kBitBack = 1u << 5,
  kBitLShoulder = 1u << 6,
  kBitRShoulder = 1u << 7,
  kBitX = 1u << 8,
  kBitA = 1u << 9,
  kBitB = 1u << 10,
  kBitY = 1u << 11,
  kBitLTrigger = 1u << 12,
  kBitRTrigger = 1u << 13,
  kBitRStickUp = 1u << 14,
  kBitRStickDown = 1u << 15,
  kBitRStickLeft = 1u << 16,
  kBitRStickRight = 1u << 17,
};

// Translation table for the keyfield. Each entry maps a keyfield bit to the
// X_INPUT gamepad button it sets in GetState (0 for the triggers and the right
// stick, which drive analog bytes/axes instead) and to the VK_PAD virtual-key
// reported by the keystroke path. Table order is also the keystroke emission
// order for simultaneous changes (UP events flush before DOWN, like the SDL
// driver).
struct KeyfieldBit {
  uint32_t bit;
  uint16_t gamepad_button;
  uint16_t pad_vk;
};

static constexpr KeyfieldBit kKeyfieldBits[] = {
    {kBitDpadUp, X_INPUT_GAMEPAD_DPAD_UP, static_cast<uint16_t>(VirtualKey::kXInputPadDpadUp)},
    {kBitDpadDown, X_INPUT_GAMEPAD_DPAD_DOWN, static_cast<uint16_t>(VirtualKey::kXInputPadDpadDown)},
    {kBitDpadLeft, X_INPUT_GAMEPAD_DPAD_LEFT, static_cast<uint16_t>(VirtualKey::kXInputPadDpadLeft)},
    {kBitDpadRight, X_INPUT_GAMEPAD_DPAD_RIGHT,
     static_cast<uint16_t>(VirtualKey::kXInputPadDpadRight)},
    {kBitStart, X_INPUT_GAMEPAD_START, static_cast<uint16_t>(VirtualKey::kXInputPadStart)},
    {kBitBack, X_INPUT_GAMEPAD_BACK, static_cast<uint16_t>(VirtualKey::kXInputPadBack)},
    {kBitLShoulder, X_INPUT_GAMEPAD_LEFT_SHOULDER,
     static_cast<uint16_t>(VirtualKey::kXInputPadLShoulder)},
    {kBitRShoulder, X_INPUT_GAMEPAD_RIGHT_SHOULDER,
     static_cast<uint16_t>(VirtualKey::kXInputPadRShoulder)},
    {kBitX, X_INPUT_GAMEPAD_X, static_cast<uint16_t>(VirtualKey::kXInputPadX)},
    {kBitA, X_INPUT_GAMEPAD_A, static_cast<uint16_t>(VirtualKey::kXInputPadA)},
    {kBitB, X_INPUT_GAMEPAD_B, static_cast<uint16_t>(VirtualKey::kXInputPadB)},
    {kBitY, X_INPUT_GAMEPAD_Y, static_cast<uint16_t>(VirtualKey::kXInputPadY)},
    {kBitLTrigger, 0, static_cast<uint16_t>(VirtualKey::kXInputPadLTrigger)},
    {kBitRTrigger, 0, static_cast<uint16_t>(VirtualKey::kXInputPadRTrigger)},
    {kBitRStickUp, 0, static_cast<uint16_t>(VirtualKey::kXInputPadRThumbUp)},
    {kBitRStickDown, 0, static_cast<uint16_t>(VirtualKey::kXInputPadRThumbDown)},
    {kBitRStickLeft, 0, static_cast<uint16_t>(VirtualKey::kXInputPadRThumbLeft)},
    {kBitRStickRight, 0, static_cast<uint16_t>(VirtualKey::kXInputPadRThumbRight)},
};

KeyboardInputDriver::KeyboardInputDriver(rex::ui::Window* window, size_t window_z_order,
                                         const KeyboardBindings& bindings)
    : InputDriver(window, window_z_order), bindings_(&bindings) {}

KeyboardInputDriver::~KeyboardInputDriver() {
  if (attached_window_) {
    attached_window_->RemoveInputListener(this);
    attached_window_->RemoveListener(this);
    attached_window_ = nullptr;
  }
}

X_STATUS KeyboardInputDriver::Setup() {
  REXLOG_INFO("Keyboard input driver initialized (slot {}, {})", UserIndex(),
              IsEnabled() ? "enabled" : "disabled");
  return X_STATUS_SUCCESS;
}

void KeyboardInputDriver::OnWindowAvailable(rex::ui::Window* window) {
  if (window) {
    attached_window_ = window;
    window->AddInputListener(this, window_z_order());
    window->AddListener(this);
  }
}

void KeyboardInputDriver::OnClosing(rex::ui::UIEvent&) {
  if (attached_window_) {
    attached_window_->RemoveInputListener(this);
    attached_window_->RemoveListener(this);
    attached_window_ = nullptr;
  }
}

uint32_t KeyboardInputDriver::UserIndex() const {
  return static_cast<uint32_t>(bindings_->user_index());
}

bool KeyboardInputDriver::IsEnabled() const {
  return bindings_->enabled();
}

static bool IsBindPressed(const bool (&key_down)[256], const std::string& cvar_val) {
  VirtualKey vk = rex::ui::ParseVirtualKey(cvar_val);
  if (vk == VirtualKey::kNone)
    return false;
  uint16_t idx = static_cast<uint16_t>(vk);
  return idx < 256 && key_down[idx];
}

X_RESULT KeyboardInputDriver::GetCapabilities(uint32_t user_index, uint32_t /*flags*/,
                                              X_INPUT_CAPABILITIES* out_caps) {
  if (!IsEnabled() || user_index != UserIndex()) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (out_caps) {
    std::memset(out_caps, 0, sizeof(*out_caps));
    out_caps->type = 0x01;      // XINPUT_DEVTYPE_GAMEPAD
    out_caps->sub_type = 0x01;  // XINPUT_DEVSUBTYPE_GAMEPAD
    out_caps->flags = 0;
    out_caps->gamepad.buttons = 0xFFFF;
    out_caps->gamepad.left_trigger = 0xFF;
    out_caps->gamepad.right_trigger = 0xFF;
    out_caps->gamepad.thumb_lx = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_ly = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_rx = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_ry = static_cast<int16_t>(0x7FFF);
  }
  return X_ERROR_SUCCESS;
}

uint32_t KeyboardInputDriver::ComputeKeyfield() const {
  // Caller holds state_mutex_. Reads the live bindings each call so rebinds via
  // cvar take effect immediately.
  uint32_t f = 0;
  if (IsBindPressed(key_down_, bindings_->dpad_up()))
    f |= kBitDpadUp;
  if (IsBindPressed(key_down_, bindings_->dpad_down()))
    f |= kBitDpadDown;
  if (IsBindPressed(key_down_, bindings_->dpad_left()))
    f |= kBitDpadLeft;
  if (IsBindPressed(key_down_, bindings_->dpad_right()))
    f |= kBitDpadRight;
  if (IsBindPressed(key_down_, bindings_->start()))
    f |= kBitStart;
  if (IsBindPressed(key_down_, bindings_->back()))
    f |= kBitBack;
  if (IsBindPressed(key_down_, bindings_->lshoulder()))
    f |= kBitLShoulder;
  if (IsBindPressed(key_down_, bindings_->rshoulder()))
    f |= kBitRShoulder;
  if (IsBindPressed(key_down_, bindings_->button_x()))
    f |= kBitX;
  if (IsBindPressed(key_down_, bindings_->button_a()))
    f |= kBitA;
  if (IsBindPressed(key_down_, bindings_->button_b()))
    f |= kBitB;
  if (IsBindPressed(key_down_, bindings_->button_y()))
    f |= kBitY;
  if (IsBindPressed(key_down_, bindings_->ltrigger()))
    f |= kBitLTrigger;
  if (IsBindPressed(key_down_, bindings_->rtrigger()))
    f |= kBitRTrigger;
  if (IsBindPressed(key_down_, bindings_->rstick_up()))
    f |= kBitRStickUp;
  if (IsBindPressed(key_down_, bindings_->rstick_down()))
    f |= kBitRStickDown;
  if (IsBindPressed(key_down_, bindings_->rstick_left()))
    f |= kBitRStickLeft;
  if (IsBindPressed(key_down_, bindings_->rstick_right()))
    f |= kBitRStickRight;
  return f;
}

X_RESULT KeyboardInputDriver::GetState(uint32_t user_index, X_INPUT_STATE* out_state) {
  if (!IsEnabled() || user_index != UserIndex()) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  std::lock_guard lock(state_mutex_);

  // Only report presses while the window owns focus and the app is active;
  // otherwise treat everything as released so held keys flush cleanly.
  uint32_t keyfield = (is_active() && has_focus_) ? ComputeKeyfield() : 0;

  uint16_t buttons = 0;
  for (const auto& m : kKeyfieldBits) {
    if ((keyfield & m.bit) && m.gamepad_button)
      buttons |= m.gamepad_button;
  }
  uint8_t lt = (keyfield & kBitLTrigger) ? 0xFF : 0;
  uint8_t rt = (keyfield & kBitRTrigger) ? 0xFF : 0;

  // Right analog stick: opposing keys cancel; a single key deflects fully.
  int16_t rx = 0;
  if (keyfield & kBitRStickRight) rx += kThumbMax;
  if (keyfield & kBitRStickLeft) rx += kThumbMin;
  int16_t ry = 0;
  if (keyfield & kBitRStickUp) ry += kThumbMax;
  if (keyfield & kBitRStickDown) ry += kThumbMin;

  packet_number_++;

  if (out_state) {
    std::memset(out_state, 0, sizeof(*out_state));
    out_state->packet_number = packet_number_;
    out_state->gamepad.buttons = buttons;
    out_state->gamepad.left_trigger = lt;
    out_state->gamepad.right_trigger = rt;
    out_state->gamepad.thumb_rx = rx;
    out_state->gamepad.thumb_ry = ry;
  }
  return X_ERROR_SUCCESS;
}

X_RESULT KeyboardInputDriver::SetState(uint32_t user_index, X_INPUT_VIBRATION* /*vibration*/) {
  if (!IsEnabled() || user_index != UserIndex()) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  // No rumble on a keyboard; accept and ignore.
  return X_ERROR_SUCCESS;
}

X_RESULT KeyboardInputDriver::GetKeystroke(uint32_t user_index, uint32_t /*flags*/,
                                           X_INPUT_KEYSTROKE* out_keystroke) {
  // Menus read input here (via XamInputGetKeystroke), often with XUSER_INDEX_ANY
  // (0xFF) when local co-op is on, so accept the "any user" query as well as our
  // own slot. Keystrokes are derived directly from the held keys (not from a
  // queue fed by GetState) because menus do not necessarily call GetState.
  bool user_any = (user_index & 0xFF) == 0xFF;
  if (!IsEnabled() || (!user_any && user_index != UserIndex())) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (!out_keystroke) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  std::lock_guard lock(state_mutex_);

  // Treat everything as released while unfocused/inactive so held keys emit UP
  // events and the menu doesn't latch a direction.
  uint32_t curr = (is_active() && has_focus_) ? ComputeKeyfield() : 0;

  uint32_t now = rex::chrono::Clock::QueryGuestUptimeMillis();
  static_assert(kRepeatDelayMs >= kRepeatRateMs);

  // Auto-repeat the last-pressed control while it stays held, so menus scroll.
  if (ks_repeat_state_ == RepeatState::kWaiting && ks_repeat_time_ + kRepeatDelayMs < now) {
    ks_repeat_state_ = RepeatState::kRepeating;
  }
  if (ks_repeat_state_ == RepeatState::kRepeating && ks_repeat_time_ + kRepeatRateMs < now &&
      (curr & last_ks_keyfield_ & kKeyfieldBits[ks_repeat_bit_].bit)) {
    ks_repeat_time_ = now;
    *out_keystroke = {};
    out_keystroke->virtual_key = kKeyfieldBits[ks_repeat_bit_].pad_vk;
    out_keystroke->user_index = static_cast<uint8_t>(UserIndex());
    out_keystroke->flags = X_INPUT_KEYSTROKE_KEYDOWN | X_INPUT_KEYSTROKE_REPEAT;
    return X_ERROR_SUCCESS;
  }

  uint32_t changed = curr ^ last_ks_keyfield_;
  if (!changed) {
    return X_ERROR_EMPTY;
  }

  // Emit one event per call. Flush UP events (clear pass) before DOWN events so
  // simultaneous transitions release before they press, matching the SDL driver.
  for (int pass = 0; pass < 2; ++pass) {
    bool clear_pass = (pass == 0);
    for (uint8_t i = 0; i < static_cast<uint8_t>(std::size(kKeyfieldBits)); ++i) {
      uint32_t bit = kKeyfieldBits[i].bit;
      if (!(changed & bit)) {
        continue;
      }
      bool pressed = (curr & bit) != 0;
      *out_keystroke = {};
      out_keystroke->virtual_key = kKeyfieldBits[i].pad_vk;
      out_keystroke->user_index = static_cast<uint8_t>(UserIndex());

      if (clear_pass && !pressed) {
        out_keystroke->flags = X_INPUT_KEYSTROKE_KEYUP;
        last_ks_keyfield_ &= ~bit;
        ks_repeat_state_ = RepeatState::kIdle;
        return X_ERROR_SUCCESS;
      }
      if (!clear_pass && pressed) {
        out_keystroke->flags = X_INPUT_KEYSTROKE_KEYDOWN;
        last_ks_keyfield_ |= bit;
        ks_repeat_state_ = RepeatState::kWaiting;
        ks_repeat_bit_ = i;
        ks_repeat_time_ = now;
        return X_ERROR_SUCCESS;
      }
    }
  }
  return X_ERROR_EMPTY;
}

void KeyboardInputDriver::SetKeyState(uint16_t vk, bool down) {
  if (vk < 256) {
    key_down_[vk] = down;
  }
}

void KeyboardInputDriver::OnKeyDown(rex::ui::KeyEvent& e) {
  if (!IsEnabled() || !has_focus_)
    return;
  std::lock_guard lock(state_mutex_);
  SetKeyState(static_cast<uint16_t>(e.virtual_key()), true);
}

void KeyboardInputDriver::OnKeyUp(rex::ui::KeyEvent& e) {
  if (!IsEnabled())
    return;
  std::lock_guard lock(state_mutex_);
  SetKeyState(static_cast<uint16_t>(e.virtual_key()), false);
}

void KeyboardInputDriver::OnLostFocus(rex::ui::UISetupEvent&) {
  std::lock_guard lock(state_mutex_);
  has_focus_ = false;
  std::memset(key_down_, 0, sizeof(key_down_));
}

void KeyboardInputDriver::OnGotFocus(rex::ui::UISetupEvent&) {
  std::lock_guard lock(state_mutex_);
  has_focus_ = true;
}

}  // namespace rex::input::keyboard
