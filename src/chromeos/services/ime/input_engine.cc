// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/ime/input_engine.h"

#include "base/notreached.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chromeos/services/ime/public/cpp/rulebased/engine.h"

namespace chromeos {
namespace ime {

namespace {

std::string GetIdFromImeSpec(const std::string& ime_spec) {
  static const std::string kPrefix("m17n:");
  return base::StartsWith(ime_spec, kPrefix, base::CompareCase::SENSITIVE)
             ? ime_spec.substr(kPrefix.length())
             : std::string();
}

uint8_t GenerateModifierValueForRulebased(
    const mojom::ModifierStatePtr& modifier_state,
    bool isAltRightDown) {
  uint8_t modifiers = 0;
  if (modifier_state->shift)
    modifiers |= rulebased::MODIFIER_SHIFT;
  if (modifier_state->alt_graph || isAltRightDown)
    modifiers |= rulebased::MODIFIER_ALTGR;
  if (modifier_state->caps_lock)
    modifiers |= rulebased::MODIFIER_CAPSLOCK;
  return modifiers;
}

mojom::KeypressResponseForRulebasedPtr GenerateKeypressResponseForRulebased(
    rulebased::ProcessKeyResult& process_key_result) {
  mojom::KeypressResponseForRulebasedPtr keypress_response =
      mojom::KeypressResponseForRulebased::New();
  keypress_response->result = process_key_result.key_handled;
  if (!process_key_result.commit_text.empty()) {
    keypress_response->operations.push_back(mojom::OperationForRulebased::New(
        mojom::OperationMethodForRulebased::COMMIT_TEXT,
        process_key_result.commit_text));
  }
  // Need to add the setComposition operation to the result when the key is
  // handled and commit_text and composition_text are both empty.
  // That is the case of using Backspace to delete the last character in
  // composition.
  if (!process_key_result.composition_text.empty() ||
      (process_key_result.key_handled &&
       process_key_result.commit_text.empty())) {
    keypress_response->operations.push_back(mojom::OperationForRulebased::New(
        mojom::OperationMethodForRulebased::SET_COMPOSITION,
        process_key_result.composition_text));
  }
  return keypress_response;
}

bool IsModifierKey(const std::string& key_code) {
  return key_code == "AltLeft" || key_code == "AltRight" ||
         key_code == "ShiftLeft" || key_code == "ShiftRight" ||
         key_code == "ControlLeft" || key_code == "ControlRight" ||
         key_code == "CapsLock";
}

}  // namespace

InputEngineContext::InputEngineContext(const std::string& ime) : ime_spec(ime) {
  // The |ime_spec|'s format for rule based imes is: "m17n:<id>".
  std::string id = GetIdFromImeSpec(ime_spec);
  if (rulebased::Engine::IsImeSupported(id)) {
    engine = std::make_unique<rulebased::Engine>();
    engine->Activate(id);
  }
}

InputEngineContext::~InputEngineContext() {}

InputEngine::InputEngine() {}

InputEngine::~InputEngine() {}

bool InputEngine::BindRequest(
    const std::string& ime_spec,
    mojo::PendingReceiver<mojom::InputChannel> receiver,
    mojo::PendingRemote<mojom::InputChannel> remote,
    const std::vector<uint8_t>& extra) {
  if (!IsImeSupportedByRulebased(ime_spec))
    return false;

  channel_receivers_.Add(this, std::move(receiver),
                         std::make_unique<InputEngineContext>(ime_spec));

  return true;
  // TODO(https://crbug.com/837156): Registry connection error handler.
}

bool InputEngine::IsImeSupportedByRulebased(const std::string& ime_spec) {
  return rulebased::Engine::IsImeSupported(GetIdFromImeSpec(ime_spec));
}

void InputEngine::ProcessMessage(const std::vector<uint8_t>& message,
                                 ProcessMessageCallback callback) {
  NOTIMPLEMENTED();  // Protobuf message is not used in the rulebased engine.
}

void InputEngine::OnInputMethodChanged(const std::string& engine_id) {
  NOTIMPLEMENTED();  // Not used in the rulebased engine.
}

void InputEngine::OnFocus(mojom::InputFieldInfoPtr input_field_info) {
  NOTIMPLEMENTED();  // Not used in the rulebased engine.
}

void InputEngine::OnBlur() {
  NOTIMPLEMENTED();  // Not used in the rulebased engine.
}

void InputEngine::OnSurroundingTextChanged(
    const std::string& text,
    uint32_t offset,
    mojom::SelectionRangePtr selection_range) {
  NOTIMPLEMENTED();  // Not used in the rulebased engine.
}

void InputEngine::OnCompositionCanceled() {
  NOTIMPLEMENTED();  // Not used in the rulebased engine.
}

void InputEngine::ProcessKeypressForRulebased(
    mojom::PhysicalKeyEventPtr event,
    ProcessKeypressForRulebasedCallback callback) {
  auto& context = channel_receivers_.current_context();
  auto& engine = context.get()->engine;

  // According to the W3C spec, |altKey| is false if the AltGr key
  // is pressed [1]. However, all rule-based input methods on Chrome OS use
  // the US QWERTY layout as a base layout, with AltGr implemented at this
  // layer. This means the right Alt key reports as being a normal Alt key, so
  // |altKey| is true. Thus, we need to take |altKey| and exclude the
  // right Alt key to determine the status of the "true" Alt key.
  // [1] https://www.w3.org/TR/uievents-key/#keys-modifier
  // TODO(https://crbug.com/1014778): Change the base layouts for the
  // rule-based input methods so that |altKey| is false when AltGr is pressed.
  if (event->code == "AltRight") {
    isAltRightDown_ = event->type == mojom::KeyEventType::kKeyDown;
  }

  const bool isAltDown = event->modifier_state->alt && !isAltRightDown_;

  // - Shift/AltRight/Caps/Ctrl are modifier keys for the characters which the
  // Mojo service may accept, but don't send the keys themselves to Mojo.
  // - Ctrl+? and Alt+? are shortcut keys, so don't send them to the rule based
  // engine.
  if (!engine || event->type != mojom::KeyEventType::kKeyDown ||
      (IsModifierKey(event->code) || event->modifier_state->control ||
       isAltDown)) {
    std::move(callback).Run(mojom::KeypressResponseForRulebased::New(
        false, std::vector<mojom::OperationForRulebasedPtr>(0)));
    return;
  }

  rulebased::ProcessKeyResult process_key_result = engine->ProcessKey(
      event->code, GenerateModifierValueForRulebased(event->modifier_state,
                                                     isAltRightDown_));
  mojom::KeypressResponseForRulebasedPtr keypress_response =
      GenerateKeypressResponseForRulebased(process_key_result);

  std::move(callback).Run(std::move(keypress_response));
}

void InputEngine::OnKeyEvent(mojom::PhysicalKeyEventPtr event,
                             OnKeyEventCallback callback) {
  NOTIMPLEMENTED();  // Not used in the rulebased engine.
}

void InputEngine::ResetForRulebased() {
  auto& context = channel_receivers_.current_context();
  auto& engine = context.get()->engine;
  // TODO(https://crbug.com/1633694) Handle the case when the engine is not
  // defined
  if (engine) {
    engine->Reset();
  }
  isAltRightDown_ = false;
}

void InputEngine::GetRulebasedKeypressCountForTesting(
    GetRulebasedKeypressCountForTestingCallback callback) {
  auto& context = channel_receivers_.current_context();
  auto& engine = context.get()->engine;
  std::move(callback).Run(engine ? engine->process_key_count() : -1);
}

void InputEngine::CommitText(const std::string& text,
                             mojom::CommitTextCursorBehavior cursor_behavior) {
  NOTIMPLEMENTED();  // Not used in the rulebased engine.
}

void InputEngine::SetComposition(const std::string& text) {
  NOTIMPLEMENTED();  // Not used in the rulebased engine.
}

void InputEngine::SetCompositionRange(uint32_t start, uint32_t end) {
  NOTIMPLEMENTED();  // Not used in the rulebased engine.
}

void InputEngine::FinishComposition() {
  NOTIMPLEMENTED();  // Not used in the rulebased engine.
}

void InputEngine::DeleteSurroundingText(uint32_t num_bytes_before_cursor,
                                        uint32_t num_bytes_after_cursor) {
  NOTIMPLEMENTED();  // Not used in the rulebased engine.
}

void InputEngine::HandleAutocorrect(
    mojom::AutocorrectSpanPtr autocorrect_span) {
  NOTIMPLEMENTED();  // Not used in the rulebased engine.
}

}  // namespace ime
}  // namespace chromeos
