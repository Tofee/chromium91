// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/base/ime/linux/input_method_auralinux.h"

#include "base/auto_reset.h"
#include "base/bind.h"
#include "base/environment.h"
#include "ui/base/ime/constants.h"
#include "ui/base/ime/linux/linux_input_method_context_factory.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/events/event.h"

namespace {

constexpr base::TimeDelta kIgnoreCommitsDuration =
    base::TimeDelta::FromMilliseconds(100);

bool IsEventFromVK(const ui::KeyEvent& event) {
  if (event.HasNativeEvent())
    return false;

  const auto* properties = event.properties();
  return properties &&
         properties->find(ui::kPropertyFromVK) != properties->end();
}

}  // namespace

namespace ui {

InputMethodAuraLinux::InputMethodAuraLinux(
    internal::InputMethodDelegate* delegate,
    ///@name USE_NEVA_APPRUNTIME
    ///@{
    unsigned handle
    ///@}
    )
    : InputMethodBase(delegate),
      text_input_type_(TEXT_INPUT_TYPE_NONE),
      is_sync_mode_(false),
      composition_changed_(false) {
  DCHECK(LinuxInputMethodContextFactory::instance())
      << "Trying to initialize InputMethodAuraLinux, but "
         "LinuxInputMethodContextFactory is not initialized yet.";
  ///@name USE_NEVA_APPRUNTIME
  ///@{
  if (!!handle) {
    context_ =
        LinuxInputMethodContextFactory::instance()->CreateInputMethodContext(
          this, handle, false);
    context_simple_ =
        LinuxInputMethodContextFactory::instance()->CreateInputMethodContext(
          this, handle, true);
  } else {
  ///@}
    context_ =
        LinuxInputMethodContextFactory::instance()->CreateInputMethodContext(
            this, false);
    context_simple_ =
        LinuxInputMethodContextFactory::instance()->CreateInputMethodContext(
            this, true);
  ///@name USE_NEVA_APPRUNTIME
  ///@{
  }
  ///@}
}

InputMethodAuraLinux::~InputMethodAuraLinux() = default;

LinuxInputMethodContext* InputMethodAuraLinux::GetContextForTesting(
    bool is_simple) {
  return is_simple ? context_simple_.get() : context_.get();
}

// Overriden from InputMethod.

ui::EventDispatchDetails InputMethodAuraLinux::DispatchKeyEvent(
    ui::KeyEvent* event) {
  DCHECK(event->type() == ET_KEY_PRESSED || event->type() == ET_KEY_RELEASED);
  ime_filtered_key_event_.reset();

  // If no text input client, do nothing.
  if (!GetTextInputClient())
    return DispatchKeyEventPostIME(event);

  if (IsEventFromVK(*event)) {
    // Faked key events that are sent from input.ime.sendKeyEvents.
    ui::EventDispatchDetails details = DispatchKeyEventPostIME(event);
    if (details.dispatcher_destroyed || details.target_destroyed ||
        event->stopped_propagation()) {
      return details;
    }
    if ((event->is_char() || event->GetDomKey().IsCharacter()) &&
        event->type() == ui::ET_KEY_PRESSED) {
      GetTextInputClient()->InsertChar(*event);
    }
    return details;
  }

  // Forward key event to IME.
  bool filtered = false;
  {
    suppress_non_key_input_until_ = base::TimeTicks::UnixEpoch();
    composition_changed_ = false;
    result_text_.clear();
    LinuxInputMethodContext* context =
        text_input_type_ != TEXT_INPUT_TYPE_NONE &&
                text_input_type_ != TEXT_INPUT_TYPE_PASSWORD
            ? context_.get()
            : context_simple_.get();
    base::AutoReset<bool> flipper(&is_sync_mode_, true);
    filtered = context->DispatchKeyEvent(*event);
  }

  // There are four cases here. They are a pair of two conditions:
  // - Whether KeyEvent is consumed by IME, which is represented by filtered.
  // - Whether IME updates the commit/preedit string synchronously
  //   (i.e. which is already completed here), or asynchronously (i.e. which
  //   will be done afterwords, so not yet done).
  //
  // Note that there's a case that KeyEvent is reported as NOT consumed by IME,
  // but IME still updates the commit/preedit. Please see below comment
  // for more details.
  //
  // Conceptually, after IME's update, there're three things to be done.
  // - Continue to dispatch the KeyEvent.
  // - Update TextInputClient by using committed text.
  // - Update TextInputClient by using preedit text.
  // The following code does those three, except in the case that KeyEvent is
  // consumed by IME and commit/preedit string update will happen
  // asynchronously. The remaining case is covered in OnCommit and
  // OnPreeditChanged/End.
  if (filtered && !HasInputMethodResult() && !IsTextInputTypeNone()) {
    ime_filtered_key_event_ = std::move(*event);
    return ui::EventDispatchDetails();
  }

  // First, if KeyEvent is consumed by IME, continue to dispatch it,
  // before updating commit/preedit string so that, e.g., JavaScript keydown
  // event is delivered to the page before keypress.
  ui::EventDispatchDetails details;
  if (event->type() == ui::ET_KEY_PRESSED && filtered) {
    details = DispatchImeFilteredKeyPressEvent(event);
    if (details.target_destroyed || details.dispatcher_destroyed ||
        event->stopped_propagation()) {
      return details;
    }
  }

  // Processes the result text before composition for sync mode.
  const auto commit_result = MaybeCommitResult(filtered, *event);
  if (commit_result == CommitResult::kTargetDestroyed) {
    details.target_destroyed = true;
    event->StopPropagation();
    return details;
  }
  // Stop the propagation if there's some committed characters.
  // Note that this have to be done after the key event dispatching,
  // specifically if key event is not reported as filtered.
  bool should_stop_propagation = commit_result == CommitResult::kSuccess;

  // Then update the composition, if necessary.
  // Should stop propagation of the event when composition is updated,
  // because the event is considered to be used for the composition.
  should_stop_propagation |=
      MaybeUpdateComposition(commit_result == CommitResult::kSuccess);

  // If the IME has not handled the key event, passes the keyevent back to the
  // previous processing flow.
  if (!filtered) {
    details = DispatchKeyEventPostIME(event);
    if (details.dispatcher_destroyed) {
      if (should_stop_propagation)
        event->StopPropagation();
      return details;
    }
    if (event->stopped_propagation() || details.target_destroyed) {
      ResetContext();
    } else if (event->type() == ui::ET_KEY_PRESSED) {
      // If a key event was not filtered by |context_| or |context_simple_|,
      // then it means the key event didn't generate any result text. For some
      // cases, the key event may still generate a valid character, eg. a
      // control-key event (ctrl-a, return, tab, etc.). We need to send the
      // character to the focused text input client by calling
      // TextInputClient::InsertChar().
      // Note: don't use |client| and use GetTextInputClient() here because
      // DispatchKeyEventPostIME may cause the current text input client change.
      char16_t ch = event->GetCharacter();
      if (ch && GetTextInputClient())
        GetTextInputClient()->InsertChar(*event);
      should_stop_propagation = true;
    }
  }

  if (should_stop_propagation)
    event->StopPropagation();

  return details;
}

ui::EventDispatchDetails InputMethodAuraLinux::DispatchImeFilteredKeyPressEvent(
    ui::KeyEvent* event) {
  // In general, 229 (VKEY_PROCESSKEY) should be used. However, in some IME
  // framework, such as iBus/fcitx + GTK, the behavior is not simple as follows,
  // in order to deal with synchronous API on asynchronous IME backend:
  // - First, IM module reports the KeyEvent is filtered synchronously.
  // - Then, it forwards the event to the IME engine asynchronously.
  // - When IM module receives the result, and it turns out the event is not
  //   consumed, then IM module generates the same key event (with a special
  //   flag), and sent it to the application (Chrome in our case).
  // - Then, the application forwards the event to IM module again, and in this
  //   time IM module synchronously commit the character.
  // (Note: new iBus GTK IMModule changed the behavior, so the second event
  // dispatch to the application won't happen).
  // InputMethodAuraLinux detects this case by the following condition:
  // - If result text is only one character, and
  // - there's no composing text, and no updated.
  // If the condition meets, that means IME did not consume the key event
  // conceptually, so continue to dispatch KeyEvent without overwriting by 229.
  ui::EventDispatchDetails details = NeedInsertChar(result_text_)
                                         ? DispatchKeyEventPostIME(event)
                                         : SendFakeProcessKeyEvent(event);
  if (details.dispatcher_destroyed)
    return details;
  // If the KEYDOWN is stopped propagation (e.g. triggered an accelerator),
  // don't InsertChar/InsertText to the input field.
  if (event->stopped_propagation() || details.target_destroyed)
    ResetContext();

  return details;
}

InputMethodAuraLinux::CommitResult InputMethodAuraLinux::MaybeCommitResult(
    bool filtered,
    const KeyEvent& event) {
  // Take the ownership of |result_text_|.
  std::u16string result_text = std::move(result_text_);
  result_text_.clear();

  // Note: |client| could be NULL because DispatchKeyEventPostIME could have
  // changed the text input client.
  TextInputClient* client = GetTextInputClient();
  if (!client || result_text.empty())
    return CommitResult::kNoCommitString;

  if (filtered && NeedInsertChar(result_text)) {
    for (const auto ch : result_text) {
      ui::KeyEvent ch_event(event);
      ch_event.set_character(ch);
      client->InsertChar(ch_event);
      // If the client changes we assume that the original target has been
      // destroyed.
      if (client != GetTextInputClient())
        return CommitResult::kTargetDestroyed;
    }
  } else {
    // If |filtered| is false, that means the IME wants to commit some text
    // but still release the key to the application. For example, Korean IME
    // handles ENTER key to confirm its composition but still release it for
    // the default behavior (e.g. trigger search, etc.)
    // In such case, don't do InsertChar because a key should only trigger the
    // keydown event once.
    client->InsertText(
        result_text,
        ui::TextInputClient::InsertTextCursorBehavior::kMoveCursorAfterText);
    // If the client changes we assume that the original target has been
    // destroyed.
    if (client != GetTextInputClient())
      return CommitResult::kTargetDestroyed;
  }

  return CommitResult::kSuccess;
}

bool InputMethodAuraLinux::MaybeUpdateComposition(bool text_committed) {
  TextInputClient* client = GetTextInputClient();
  bool update_composition =
      client && composition_changed_ && !IsTextInputTypeNone();
  if (update_composition) {
    // If composition changed, does SetComposition if composition is not empty.
    // And ClearComposition if composition is empty.
    if (!composition_.text.empty())
      client->SetCompositionText(composition_);
    else if (!text_committed)
      client->ClearCompositionText();
  }

  // Makes sure the cached composition is cleared after committing any text or
  // cleared composition.
  if (client && !client->HasCompositionText())
    composition_ = CompositionText();

  return update_composition;
}

void InputMethodAuraLinux::UpdateContextFocusState() {
  bool old_text_input_type = text_input_type_;
  text_input_type_ = GetTextInputType();

  // We only focus in |context_| when the focus is in a textfield.
  if (old_text_input_type != TEXT_INPUT_TYPE_NONE &&
      text_input_type_ == TEXT_INPUT_TYPE_NONE) {
    context_->Blur();
  } else if (old_text_input_type == TEXT_INPUT_TYPE_NONE &&
             text_input_type_ != TEXT_INPUT_TYPE_NONE) {
    context_->Focus();
  }

  // |context_simple_| can be used in any textfield, including password box, and
  // even if the focused text input client's text input type is
  // ui::TEXT_INPUT_TYPE_NONE.
  if (GetTextInputClient())
    context_simple_->Focus();
  else
    context_simple_->Blur();
}

void InputMethodAuraLinux::OnTextInputTypeChanged(
    const TextInputClient* client) {
  UpdateContextFocusState();
  InputMethodBase::OnTextInputTypeChanged(client);
  // TODO(yoichio): Support inputmode HTML attribute.
}

void InputMethodAuraLinux::OnCaretBoundsChanged(const TextInputClient* client) {
  if (!IsTextInputClientFocused(client))
    return;
  NotifyTextInputCaretBoundsChanged(client);
  context_->SetCursorLocation(GetTextInputClient()->GetCaretBounds());

  gfx::Range text_range, selection_range;
  std::u16string text;
  if (client->GetTextRange(&text_range) &&
      client->GetTextFromRange(text_range, &text) &&
      client->GetEditableSelectionRange(&selection_range)) {
    context_->SetSurroundingText(text, selection_range);
  }
}

void InputMethodAuraLinux::CancelComposition(const TextInputClient* client) {
  if (!IsTextInputClientFocused(client))
    return;

  ResetContext();
}

void InputMethodAuraLinux::ResetContext() {
  if (!GetTextInputClient())
    return;

  is_sync_mode_ = true;

  if (!composition_.text.empty()) {
    // If the IME has an open composition, ignore non-synchronous attempts to
    // commit text for a brief duration of time.
    suppress_non_key_input_until_ =
        base::TimeTicks::Now() + kIgnoreCommitsDuration;
  }

  context_->Reset();
  context_simple_->Reset();

  composition_ = CompositionText();
  result_text_.clear();
  is_sync_mode_ = false;
  composition_changed_ = false;
}

bool InputMethodAuraLinux::IgnoringNonKeyInput() const {
  return !is_sync_mode_ &&
         base::TimeTicks::Now() < suppress_non_key_input_until_;
}

bool InputMethodAuraLinux::IsCandidatePopupOpen() const {
  // There seems no way to detect candidate windows or any popups.
  return false;
}

///@name USE_NEVA_APPRUNTIME
///@{
LinuxInputMethodContext* InputMethodAuraLinux::GetInputMethodContext() {
  return context_.get();
}
///@}

// Overriden from ui::LinuxInputMethodContextDelegate

void InputMethodAuraLinux::OnCommit(const std::u16string& text) {
  if (IgnoringNonKeyInput() || !GetTextInputClient())
    return;

  // Discard the result iff in async-mode and the TextInputType is None
  // for backward compatibility.
  if (is_sync_mode_ || !IsTextInputTypeNone())
    result_text_.append(text);

  // Sync mode means this is called on a stack of DispatchKeyEvent(), so its
  // following code should handle the key dispatch and actual committing.
  // If we are not handling key event, do not bother sending text result if
  // the focused text input client does not support text input.
  if (!is_sync_mode_ && !IsTextInputTypeNone()) {
    ui::KeyEvent event =
        ime_filtered_key_event_.has_value()
            ? *ime_filtered_key_event_
            : ui::KeyEvent(ui::ET_KEY_PRESSED, ui::VKEY_PROCESSKEY, 0);
    ui::EventDispatchDetails details = DispatchImeFilteredKeyPressEvent(&event);
    if (details.target_destroyed || details.dispatcher_destroyed ||
        event.stopped_propagation()) {
      return;
    }
    MaybeCommitResult(/*filtered=*/true, event);
    composition_ = CompositionText();
  }
}

void InputMethodAuraLinux::OnDeleteSurroundingText(int32_t index,
                                                   uint32_t length) {
  if (GetTextInputClient() && composition_.text.empty()) {
    uint32_t before = index >= 0 ? 0U : static_cast<uint32_t>(-1 * index);
    GetTextInputClient()->ExtendSelectionAndDelete(before, length - before);
  }
}

void InputMethodAuraLinux::OnPreeditChanged(
    const CompositionText& composition_text) {
  OnPreeditUpdate(composition_text, !is_sync_mode_);
}

void InputMethodAuraLinux::OnPreeditEnd() {
  TextInputClient* client = GetTextInputClient();
  OnPreeditUpdate(CompositionText(),
                  !is_sync_mode_ && client && client->HasCompositionText());
}

// Overridden from InputMethodBase.

void InputMethodAuraLinux::OnWillChangeFocusedClient(
    TextInputClient* focused_before,
    TextInputClient* focused) {
  ConfirmCompositionText();
}

void InputMethodAuraLinux::OnDidChangeFocusedClient(
    TextInputClient* focused_before,
    TextInputClient* focused) {
  UpdateContextFocusState();

  // Force to update caret bounds, in case the View thinks that the caret
  // bounds has not changed.
  if (text_input_type_ != TEXT_INPUT_TYPE_NONE)
    OnCaretBoundsChanged(GetTextInputClient());

  InputMethodBase::OnDidChangeFocusedClient(focused_before, focused);
}

// private

void InputMethodAuraLinux::OnPreeditUpdate(
    const ui::CompositionText& composition_text,
    bool force_update_client) {
  if (IgnoringNonKeyInput() || IsTextInputTypeNone())
    return;

  composition_changed_ |= composition_ != composition_text;
  composition_ = composition_text;

  if (!force_update_client)
    return;
  ui::KeyEvent event =
      ime_filtered_key_event_.has_value()
          ? *ime_filtered_key_event_
          : ui::KeyEvent(ui::ET_KEY_PRESSED, ui::VKEY_PROCESSKEY, 0);
  ui::EventDispatchDetails details = DispatchImeFilteredKeyPressEvent(&event);
  if (details.target_destroyed || details.dispatcher_destroyed ||
      event.stopped_propagation()) {
    return;
  }
  MaybeUpdateComposition(/*text_committed=*/false);
}

bool InputMethodAuraLinux::HasInputMethodResult() {
  return !result_text_.empty() || composition_changed_;
}

bool InputMethodAuraLinux::NeedInsertChar(
    const std::u16string& result_text) const {
  return IsTextInputTypeNone() ||
         (!composition_changed_ && composition_.text.empty() &&
          result_text.length() == 1);
}

ui::EventDispatchDetails InputMethodAuraLinux::SendFakeProcessKeyEvent(
    ui::KeyEvent* event) const {
  KeyEvent key_event(ui::ET_KEY_PRESSED, ui::VKEY_PROCESSKEY, event->flags());
  ui::EventDispatchDetails details = DispatchKeyEventPostIME(&key_event);
  if (key_event.stopped_propagation())
    event->StopPropagation();
  return details;
}

void InputMethodAuraLinux::ConfirmCompositionText() {
  ResetContext();
}

}  // namespace ui
