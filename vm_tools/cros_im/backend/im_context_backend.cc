// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backend/im_context_backend.h"

#include <cassert>
#include <cstring>
#include <utility>

#include "backend/text_input.h"
#include "backend/wayland_client.h"
#include "backend/wayland_manager.h"

namespace cros_im {

namespace {

constexpr char kVirtualKeyboardEnv[] = "CROS_IM_VIRTUAL_KEYBOARD";
constexpr char kVirtualKeyboardEnabled[] = "1";

template <auto F, typename... Args>
auto Fwd(void* data, zwp_text_input_v1* text_input, Args... args) {
  // The backend object should still be alive as libwayland-client drops events
  // sent to destroyed objects.
  return (reinterpret_cast<IMContextBackend*>(data)->*F)(args...);
}

template <typename... Args>
auto DoNothing(void* data, zwp_text_input_v1* text_input, Args... args) {}

template <auto F, typename... Args>
auto FwdExtended(void* data,
                 zcr_extended_text_input_v1* extendedtext_input,
                 Args... args) {
  return (reinterpret_cast<IMContextBackend*>(data)->*F)(args...);
}

template <typename... Args>
auto DoNothingExtended(void* data,
                       zcr_extended_text_input_v1* text_input,
                       Args... args) {}

}  // namespace

const zwp_text_input_v1_listener IMContextBackend::text_input_listener_ = {
    .enter = DoNothing,
    .leave = DoNothing,
    .modifiers_map = DoNothing,
    .input_panel_state = DoNothing,
    .preedit_string = Fwd<&IMContextBackend::SetPreedit>,
    .preedit_styling = Fwd<&IMContextBackend::SetPreeditStyling>,
    .preedit_cursor = Fwd<&IMContextBackend::SetPreeditCursor>,
    .commit_string = Fwd<&IMContextBackend::Commit>,
    .cursor_position = DoNothing,
    .delete_surrounding_text = DoNothing,
    .keysym = Fwd<&IMContextBackend::KeySym>,
    .language = DoNothing,
    .text_direction = DoNothing,
};

const zcr_extended_text_input_v1_listener
    IMContextBackend::extended_text_input_listener_ = {
        .set_preedit_region = FwdExtended<&IMContextBackend::SetPreeditRegion>,
        .clear_grammar_fragments = DoNothingExtended,
        .add_grammar_fragment = DoNothingExtended,
        .set_autocorrect_range = DoNothingExtended,
};

IMContextBackend::IMContextBackend(Observer* observer) : observer_(observer) {
  assert(WaylandManager::HasInstance());

  const char* env = std::getenv(kVirtualKeyboardEnv);
  virtual_keyboard_enabled_ =
      env && std::string(env) == kVirtualKeyboardEnabled;

  MaybeInitialize();
}

IMContextBackend::~IMContextBackend() {
  if (text_input_)
    zwp_text_input_v1_destroy(text_input_);
}

void IMContextBackend::Activate(wl_surface* surface) {
  MaybeInitialize();

  if (!text_input_) {
    printf("The text input manager is not ready yet or not available.\n");
    return;
  }

  is_active_ = true;
  zwp_text_input_v1_activate(text_input_, WaylandManager::Get()->GetSeat(),
                             surface);
}

void IMContextBackend::Deactivate() {
  if (!text_input_)
    return;
  if (!is_active_) {
    printf("Attempted to deactivate text input which was not activated.\n");
    return;
  }

  if (virtual_keyboard_enabled_)
    zwp_text_input_v1_hide_input_panel(text_input_);
  zwp_text_input_v1_deactivate(text_input_, WaylandManager::Get()->GetSeat());
  is_active_ = false;
}

void IMContextBackend::ShowInputPanel() {
  if (!text_input_ || !virtual_keyboard_enabled_)
    return;
  zwp_text_input_v1_show_input_panel(text_input_);
}

void IMContextBackend::Reset() {
  if (!text_input_)
    return;
  zwp_text_input_v1_reset(text_input_);
}

void IMContextBackend::SetSurrounding(const char* text, int cursor_index) {
  if (!text_input_)
    return;
  zwp_text_input_v1_set_surrounding_text(text_input_, text, cursor_index,
                                         cursor_index);
}

void IMContextBackend::SetContentType(ContentType content_type) {
  if (!text_input_)
    return;
  zwp_text_input_v1_set_content_type(text_input_, content_type.hints,
                                     content_type.purpose);
}

void IMContextBackend::SetCursorLocation(int x, int y, int width, int height) {
  if (!text_input_)
    return;
  zwp_text_input_v1_set_cursor_rectangle(text_input_, x, y, width, height);
}

void IMContextBackend::MaybeInitialize() {
  if (text_input_)
    return;

  text_input_ =
      WaylandManager::Get()->CreateTextInput(&text_input_listener_, this);
  if (text_input_) {
    extended_text_input_ = WaylandManager::Get()->CreateExtendedTextInput(
        text_input_, &extended_text_input_listener_, this);
    assert(extended_text_input_);
  }
}

void IMContextBackend::SetPreeditStyling(uint32_t index,
                                         uint32_t length,
                                         uint32_t style) {
  styles_.push_back(
      {.index = index,
       .length = length,
       .style = static_cast<zwp_text_input_v1_preedit_style>(style)});
}

void IMContextBackend::SetPreeditCursor(uint32_t cursor) {
  cursor_pos_ = cursor;
}

// TODO(timloh): Work out what we need to do with serials.

void IMContextBackend::SetPreedit(uint32_t serial,
                                  const char* text,
                                  const char* commit) {
  observer_->SetPreedit(text, cursor_pos_, styles_);
  cursor_pos_ = 0;
  styles_.clear();
}

void IMContextBackend::Commit(uint32_t serial, const char* text) {
  styles_.clear();
  observer_->Commit(text);
}

void IMContextBackend::KeySym(uint32_t serial,
                              uint32_t time,
                              uint32_t sym,
                              uint32_t state,
                              uint32_t modifiers) {
  // TODO(timloh): Handle remaining arguments.
  observer_->KeySym(sym, state == WL_KEYBOARD_KEY_STATE_PRESSED
                             ? KeyState::kPressed
                             : KeyState::kReleased);
}

void IMContextBackend::SetPreeditRegion(int32_t index,
                                        uint32_t length_unsigned) {
  int length = length_unsigned;
  if (index > 0 || index + length < 0 || length <= 0) {
    printf("SetPreeditRegion(%d, %u) is for unsupported range.\n", index,
           length);
  } else {
    observer_->SetPreeditRegion(index, length, styles_);
  }
  cursor_pos_ = 0;
  styles_.clear();
}

}  // namespace cros_im
