// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/examples/menu_example.h"

#include <memory>
#include <set>

#include "base/macros.h"
#include "base/strings/utf_string_conversions.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/simple_menu_model.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/menu_button.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/examples/grit/views_examples_resources.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

using l10n_util::GetStringUTF16;
using l10n_util::GetStringUTF8;

namespace views {
namespace examples {

namespace {

class ExampleMenuModel : public ui::SimpleMenuModel,
                         public ui::SimpleMenuModel::Delegate {
 public:
  ExampleMenuModel();

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  enum GroupID {
    GROUP_MAKE_DECISION,
  };

  enum CommandID {
    COMMAND_DO_SOMETHING,
    COMMAND_SELECT_ASCII,
    COMMAND_SELECT_UTF8,
    COMMAND_SELECT_UTF16,
    COMMAND_CHECK_APPLE,
    COMMAND_CHECK_ORANGE,
    COMMAND_CHECK_KIWI,
    COMMAND_GO_HOME,
  };

  std::unique_ptr<ui::SimpleMenuModel> submenu_;
  std::set<int> checked_fruits_;
  int current_encoding_command_id_ = COMMAND_SELECT_ASCII;

  DISALLOW_COPY_AND_ASSIGN(ExampleMenuModel);
};

class ExampleMenuButton : public MenuButton {
 public:
  explicit ExampleMenuButton(const std::u16string& test);
  ~ExampleMenuButton() override;

 private:
  void ButtonPressed();

  ui::SimpleMenuModel* GetMenuModel();

  std::unique_ptr<ExampleMenuModel> menu_model_;
  std::unique_ptr<MenuRunner> menu_runner_;

  DISALLOW_COPY_AND_ASSIGN(ExampleMenuButton);
};

// ExampleMenuModel ---------------------------------------------------------

ExampleMenuModel::ExampleMenuModel() : ui::SimpleMenuModel(this) {
  AddItem(COMMAND_DO_SOMETHING, GetStringUTF16(IDS_MENU_DO_SOMETHING_LABEL));
  AddSeparator(ui::NORMAL_SEPARATOR);
  AddRadioItem(COMMAND_SELECT_ASCII, GetStringUTF16(IDS_MENU_ASCII_LABEL),
               GROUP_MAKE_DECISION);
  AddRadioItem(COMMAND_SELECT_UTF8, GetStringUTF16(IDS_MENU_UTF8_LABEL),
               GROUP_MAKE_DECISION);
  AddRadioItem(COMMAND_SELECT_UTF16, GetStringUTF16(IDS_MENU_UTF16_LABEL),
               GROUP_MAKE_DECISION);
  AddSeparator(ui::NORMAL_SEPARATOR);
  AddCheckItem(COMMAND_CHECK_APPLE, GetStringUTF16(IDS_MENU_APPLE_LABEL));
  AddCheckItem(COMMAND_CHECK_ORANGE, GetStringUTF16(IDS_MENU_ORANGE_LABEL));
  AddCheckItem(COMMAND_CHECK_KIWI, GetStringUTF16(IDS_MENU_KIWI_LABEL));
  AddSeparator(ui::NORMAL_SEPARATOR);
  AddItem(COMMAND_GO_HOME, GetStringUTF16(IDS_MENU_GO_HOME_LABEL));

  submenu_ = std::make_unique<ui::SimpleMenuModel>(this);
  submenu_->AddItem(COMMAND_DO_SOMETHING,
                    GetStringUTF16(IDS_MENU_DO_SOMETHING_2_LABEL));
  AddSubMenu(0, GetStringUTF16(IDS_MENU_SUBMENU_LABEL), submenu_.get());
}

bool ExampleMenuModel::IsCommandIdChecked(int command_id) const {
  // Radio items.
  if (command_id == current_encoding_command_id_)
    return true;

  // Check items.
  if (checked_fruits_.find(command_id) != checked_fruits_.end())
    return true;

  return false;
}

bool ExampleMenuModel::IsCommandIdEnabled(int command_id) const {
  // All commands are enabled except for COMMAND_GO_HOME.
  return command_id != COMMAND_GO_HOME;
}

void ExampleMenuModel::ExecuteCommand(int command_id, int event_flags) {
  switch (command_id) {
    case COMMAND_DO_SOMETHING: {
      VLOG(0) << "Done something";
      break;
    }

    // Radio items.
    case COMMAND_SELECT_ASCII: {
      current_encoding_command_id_ = COMMAND_SELECT_ASCII;
      VLOG(0) << "Selected ASCII";
      break;
    }
    case COMMAND_SELECT_UTF8: {
      current_encoding_command_id_ = COMMAND_SELECT_UTF8;
      VLOG(0) << "Selected UTF-8";
      break;
    }
    case COMMAND_SELECT_UTF16: {
      current_encoding_command_id_ = COMMAND_SELECT_UTF16;
      VLOG(0) << "Selected UTF-16";
      break;
    }

    // Check items.
    case COMMAND_CHECK_APPLE:
    case COMMAND_CHECK_ORANGE:
    case COMMAND_CHECK_KIWI: {
      // Print what fruit is checked.
      const char* checked_fruit = "";
      if (command_id == COMMAND_CHECK_APPLE)
        checked_fruit = "Apple";
      else if (command_id == COMMAND_CHECK_ORANGE)
        checked_fruit = "Orange";
      else if (command_id == COMMAND_CHECK_KIWI)
        checked_fruit = "Kiwi";

      // Update the check status.
      auto iter = checked_fruits_.find(command_id);
      if (iter == checked_fruits_.end()) {
        DVLOG(1) << "Checked " << checked_fruit;
        checked_fruits_.insert(command_id);
      } else {
        DVLOG(1) << "Unchecked " << checked_fruit;
        checked_fruits_.erase(iter);
      }
      break;
    }
  }
}

// ExampleMenuButton -----------------------------------------------------------

ExampleMenuButton::ExampleMenuButton(const std::u16string& test)
    : MenuButton(base::BindRepeating(&ExampleMenuButton::ButtonPressed,
                                     base::Unretained(this)),
                 test) {}

ExampleMenuButton::~ExampleMenuButton() = default;

void ExampleMenuButton::ButtonPressed() {
  menu_runner_ =
      std::make_unique<MenuRunner>(GetMenuModel(), MenuRunner::HAS_MNEMONICS);

  menu_runner_->RunMenuAt(GetWidget()->GetTopLevelWidget(), button_controller(),
                          gfx::Rect(GetMenuPosition(), gfx::Size()),
                          MenuAnchorPosition::kTopRight, ui::MENU_SOURCE_NONE);
}

ui::SimpleMenuModel* ExampleMenuButton::GetMenuModel() {
  if (!menu_model_)
    menu_model_ = std::make_unique<ExampleMenuModel>();
  return menu_model_.get();
}

}  // namespace

MenuExample::MenuExample()
    : ExampleBase(GetStringUTF8(IDS_MENU_SELECT_LABEL).c_str()) {}

MenuExample::~MenuExample() = default;

void MenuExample::CreateExampleView(View* container) {
  // We add a button to open a menu.
  container->SetLayoutManager(std::make_unique<FillLayout>());
  container->AddChildView(std::make_unique<ExampleMenuButton>(
      GetStringUTF16(IDS_MENU_BUTTON_LABEL)));
}

}  // namespace examples
}  // namespace views
