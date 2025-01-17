// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_BASE_IME_LINUX_INPUT_METHOD_AURALINUX_H_
#define UI_BASE_IME_LINUX_INPUT_METHOD_AURALINUX_H_

#include <memory>

#include "base/component_export.h"
#include "base/optional.h"
#include "ui/base/ime/composition_text.h"
#include "ui/base/ime/input_method_base.h"
#include "ui/base/ime/linux/linux_input_method_context.h"

namespace ui {

// A ui::InputMethod implementation for Aura on Linux platforms. The
// implementation details are separated to ui::LinuxInputMethodContext
// interface.
class COMPONENT_EXPORT(UI_BASE_IME_LINUX) InputMethodAuraLinux
    : public InputMethodBase,
      public LinuxInputMethodContextDelegate {
 public:
  explicit InputMethodAuraLinux(internal::InputMethodDelegate* delegate,
                                ///@name USE_NEVA_APPRUNTIME
                                ///@{
                                unsigned handle = 0
                                ///@}
                                );
  InputMethodAuraLinux(const InputMethodAuraLinux&) = delete;
  InputMethodAuraLinux& operator=(const InputMethodAuraLinux&) = delete;
  ~InputMethodAuraLinux() override;

  LinuxInputMethodContext* GetContextForTesting(bool is_simple);

  // Overriden from InputMethod.
  ui::EventDispatchDetails DispatchKeyEvent(ui::KeyEvent* event) override;
  void OnTextInputTypeChanged(const TextInputClient* client) override;
  void OnCaretBoundsChanged(const TextInputClient* client) override;
  void CancelComposition(const TextInputClient* client) override;
  bool IsCandidatePopupOpen() const override;

  ///@name USE_NEVA_APPRUNTIME
  ///@{
  LinuxInputMethodContext* GetInputMethodContext() override;
  ///@}

  // Overriden from ui::LinuxInputMethodContextDelegate
  void OnCommit(const std::u16string& text) override;
  void OnDeleteSurroundingText(int32_t index, uint32_t length) override;
  void OnPreeditChanged(const CompositionText& composition_text) override;
  void OnPreeditEnd() override;
  void OnPreeditStart() override {}

 protected:
  // Overridden from InputMethodBase.
  void OnWillChangeFocusedClient(TextInputClient* focused_before,
                                 TextInputClient* focused) override;
  void OnDidChangeFocusedClient(TextInputClient* focused_before,
                                TextInputClient* focused) override;

 private:
  ///@name USE_NEVA_APPRUNTIME
  ///@{
  friend class InputMethodAuraLinuxNeva;
  ///@}

  // Continues to dispatch the ET_KEY_PRESSED event to the client.
  // This needs to be called "before" committing the result string or
  // the composition string.
  ui::EventDispatchDetails DispatchImeFilteredKeyPressEvent(
      ui::KeyEvent* event);
  enum class CommitResult {
    kSuccess,          // Successfully committed at least one character.
    kNoCommitString,   // No available string to commit.
    kTargetDestroyed,  // Target was destroyed during the commit.
  };
  CommitResult MaybeCommitResult(bool filtered, const KeyEvent& event);
  bool MaybeUpdateComposition(bool text_committed);

  // Shared implementation of OnPreeditChanged and OnPreeditEnd.
  // |force_update_client| is designed to dispatch key event/update
  // the client's composition string, specifically for async-mode case.
  void OnPreeditUpdate(const ui::CompositionText& composition_text,
                       bool force_update_client);
  void ConfirmCompositionText();
  bool HasInputMethodResult();
  bool NeedInsertChar(const std::u16string& result_text) const;
  ui::EventDispatchDetails SendFakeProcessKeyEvent(ui::KeyEvent* event) const
      WARN_UNUSED_RESULT;
  void UpdateContextFocusState();
  void ResetContext();
  bool IgnoringNonKeyInput() const;

  std::unique_ptr<LinuxInputMethodContext> context_;
  std::unique_ptr<LinuxInputMethodContext> context_simple_;

  // The last key event that IME is probably in process in
  // async-mode.
  base::Optional<ui::KeyEvent> ime_filtered_key_event_;

  std::u16string result_text_;

  ui::CompositionText composition_;

  // The current text input type used to indicates if |context_| and
  // |context_simple_| are focused or not.
  TextInputType text_input_type_;

  // Indicates if currently in sync mode when handling a key event.
  // This is used in OnXXX callbacks from GTK IM module.
  bool is_sync_mode_;

  // Indicates if the composition text is changed or deleted.
  bool composition_changed_;

  // Ignore commit/preedit-changed/preedit-end signals if this time is still in
  // the future.
  base::TimeTicks suppress_non_key_input_until_ = base::TimeTicks::UnixEpoch();

  // Used for making callbacks.
  base::WeakPtrFactory<InputMethodAuraLinux> weak_ptr_factory_{this};
};

}  // namespace ui

#endif  // UI_BASE_IME_LINUX_INPUT_METHOD_AURALINUX_H_
