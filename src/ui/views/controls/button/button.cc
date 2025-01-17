// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/controls/button/button.h"

#include <utility>

#include "base/strings/utf_string_conversions.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/class_property.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/animation/throb_animation.h"
#include "ui/gfx/color_palette.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/animation/ink_drop_highlight.h"
#include "ui/views/animation/ink_drop_impl.h"
#include "ui/views/controls/button/button_controller.h"
#include "ui/views/controls/button/button_controller_delegate.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/button/menu_button.h"
#include "ui/views/controls/button/radio_button.h"
#include "ui/views/controls/button/toggle_button.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/metadata/metadata_impl_macros.h"
#include "ui/views/painter.h"
#include "ui/views/style/platform_style.h"

#if defined(USE_AURA)
#include "ui/aura/client/capture_client.h"
#include "ui/aura/window.h"
#endif

namespace views {

namespace {

DEFINE_UI_CLASS_PROPERTY_KEY(bool, kIsButtonProperty, false)

}  // namespace

Button::DefaultButtonControllerDelegate::DefaultButtonControllerDelegate(
    Button* button)
    : ButtonControllerDelegate(button) {}

Button::DefaultButtonControllerDelegate::~DefaultButtonControllerDelegate() =
    default;

void Button::DefaultButtonControllerDelegate::RequestFocusFromEvent() {
  button()->RequestFocusFromEvent();
}

void Button::DefaultButtonControllerDelegate::NotifyClick(
    const ui::Event& event) {
  button()->NotifyClick(event);
}

void Button::DefaultButtonControllerDelegate::OnClickCanceled(
    const ui::Event& event) {
  button()->OnClickCanceled(event);
}

bool Button::DefaultButtonControllerDelegate::IsTriggerableEvent(
    const ui::Event& event) {
  return button()->IsTriggerableEvent(event);
}

bool Button::DefaultButtonControllerDelegate::ShouldEnterPushedState(
    const ui::Event& event) {
  return button()->ShouldEnterPushedState(event);
}

bool Button::DefaultButtonControllerDelegate::ShouldEnterHoveredState() {
  return button()->ShouldEnterHoveredState();
}

InkDrop* Button::DefaultButtonControllerDelegate::GetInkDrop() {
  return button()->GetInkDrop();
}

int Button::DefaultButtonControllerDelegate::GetDragOperations(
    const gfx::Point& press_pt) {
  return button()->GetDragOperations(press_pt);
}

bool Button::DefaultButtonControllerDelegate::InDrag() {
  return button()->InDrag();
}

Button::PressedCallback::PressedCallback(
    Button::PressedCallback::Callback callback)
    : callback_(std::move(callback)) {}

Button::PressedCallback::PressedCallback(base::RepeatingClosure closure)
    : callback_(
          base::BindRepeating([](base::RepeatingClosure closure,
                                 const ui::Event& event) { closure.Run(); },
                              std::move(closure))) {}

Button::PressedCallback::PressedCallback(const PressedCallback&) = default;

Button::PressedCallback::PressedCallback(PressedCallback&&) = default;

Button::PressedCallback& Button::PressedCallback::operator=(
    const PressedCallback&) = default;

Button::PressedCallback& Button::PressedCallback::operator=(PressedCallback&&) =
    default;

Button::PressedCallback::~PressedCallback() = default;

// static
constexpr Button::ButtonState Button::kButtonStates[STATE_COUNT];

// static
const Button* Button::AsButton(const views::View* view) {
  return AsButton(const_cast<View*>(view));
}

// static
Button* Button::AsButton(views::View* view) {
  if (view && view->GetProperty(kIsButtonProperty))
    return static_cast<Button*>(view);
  return nullptr;
}

// static
Button::ButtonState Button::GetButtonStateFrom(ui::NativeTheme::State state) {
  switch (state) {
    case ui::NativeTheme::kDisabled:
      return Button::STATE_DISABLED;
    case ui::NativeTheme::kHovered:
      return Button::STATE_HOVERED;
    case ui::NativeTheme::kNormal:
      return Button::STATE_NORMAL;
    case ui::NativeTheme::kPressed:
      return Button::STATE_PRESSED;
    case ui::NativeTheme::kNumStates:
      NOTREACHED();
  }
  return Button::STATE_NORMAL;
}

Button::~Button() = default;

void Button::SetTooltipText(const std::u16string& tooltip_text) {
  if (tooltip_text == tooltip_text_)
    return;
  tooltip_text_ = tooltip_text;
  OnSetTooltipText(tooltip_text);
  TooltipTextChanged();
  OnPropertyChanged(&tooltip_text_, kPropertyEffectsNone);
  NotifyAccessibilityEvent(ax::mojom::Event::kTextChanged, true);
}

std::u16string Button::GetTooltipText() const {
  return tooltip_text_;
}

void Button::SetAccessibleName(const std::u16string& name) {
  if (name == accessible_name_)
    return;
  accessible_name_ = name;
  OnPropertyChanged(&accessible_name_, kPropertyEffectsNone);
  NotifyAccessibilityEvent(ax::mojom::Event::kTextChanged, true);
}

const std::u16string& Button::GetAccessibleName() const {
  return accessible_name_.empty() ? tooltip_text_ : accessible_name_;
}

Button::ButtonState Button::GetState() const {
  return state_;
}

void Button::SetState(ButtonState state) {
  if (state == state_)
    return;

  if (animate_on_state_change_ &&
      (!is_throbbing_ || !hover_animation_.is_animating())) {
    is_throbbing_ = false;
    if ((state_ == STATE_HOVERED) && (state == STATE_NORMAL)) {
      // For HOVERED -> NORMAL, animate from hovered (1) to not hovered (0).
      hover_animation_.Hide();
    } else if (state != STATE_HOVERED) {
      // For HOVERED -> PRESSED/DISABLED, or any transition not involving
      // HOVERED at all, simply set the state to not hovered (0).
      hover_animation_.Reset();
    } else if (state_ == STATE_NORMAL) {
      // For NORMAL -> HOVERED, animate from not hovered (0) to hovered (1).
      hover_animation_.Show();
    } else {
      // For PRESSED/DISABLED -> HOVERED, simply set the state to hovered (1).
      hover_animation_.Reset(1);
    }
  }

  ButtonState old_state = state_;
  state_ = state;
  StateChanged(old_state);
  OnPropertyChanged(&state_, kPropertyEffectsPaint);
}

void Button::StartThrobbing(int cycles_til_stop) {
  if (!animate_on_state_change_)
    return;
  is_throbbing_ = true;
  hover_animation_.StartThrobbing(cycles_til_stop);
}

void Button::StopThrobbing() {
  if (hover_animation_.is_animating()) {
    hover_animation_.Stop();
    SchedulePaint();
  }
}

void Button::SetAnimationDuration(base::TimeDelta duration) {
  hover_animation_.SetSlideDuration(duration);
}

void Button::SetTriggerableEventFlags(int triggerable_event_flags) {
  if (triggerable_event_flags == triggerable_event_flags_)
    return;
  triggerable_event_flags_ = triggerable_event_flags;
  OnPropertyChanged(&triggerable_event_flags_, kPropertyEffectsNone);
}

int Button::GetTriggerableEventFlags() const {
  return triggerable_event_flags_;
}

void Button::SetRequestFocusOnPress(bool value) {
// On Mac, buttons should not request focus on a mouse press. Hence keep the
// default value i.e. false.
#if !defined(OS_APPLE)
  if (request_focus_on_press_ == value)
    return;
  request_focus_on_press_ = value;
  OnPropertyChanged(&request_focus_on_press_, kPropertyEffectsNone);
#endif
}

bool Button::GetRequestFocusOnPress() const {
  return request_focus_on_press_;
}

void Button::SetAnimateOnStateChange(bool value) {
  if (value == animate_on_state_change_)
    return;
  animate_on_state_change_ = value;
  OnPropertyChanged(&animate_on_state_change_, kPropertyEffectsNone);
}

bool Button::GetAnimateOnStateChange() const {
  return animate_on_state_change_;
}

void Button::SetHideInkDropWhenShowingContextMenu(bool value) {
  if (value == hide_ink_drop_when_showing_context_menu_)
    return;
  hide_ink_drop_when_showing_context_menu_ = value;
  OnPropertyChanged(&hide_ink_drop_when_showing_context_menu_,
                    kPropertyEffectsNone);
}

bool Button::GetHideInkDropWhenShowingContextMenu() const {
  return hide_ink_drop_when_showing_context_menu_;
}

void Button::SetShowInkDropWhenHotTracked(bool value) {
  if (value == show_ink_drop_when_hot_tracked_)
    return;
  show_ink_drop_when_hot_tracked_ = value;
  OnPropertyChanged(&show_ink_drop_when_hot_tracked_, kPropertyEffectsNone);
}

bool Button::GetShowInkDropWhenHotTracked() const {
  return show_ink_drop_when_hot_tracked_;
}

void Button::SetInkDropBaseColor(SkColor color) {
  if (color == ink_drop_base_color_)
    return;
  ink_drop_base_color_ = color;
  OnPropertyChanged(&ink_drop_base_color_, kPropertyEffectsNone);
}

void Button::SetHasInkDropActionOnClick(bool value) {
  if (value == has_ink_drop_action_on_click_)
    return;
  has_ink_drop_action_on_click_ = value;
  OnPropertyChanged(&has_ink_drop_action_on_click_, kPropertyEffectsNone);
}

bool Button::GetHasInkDropActionOnClick() const {
  return has_ink_drop_action_on_click_;
}

void Button::SetInstallFocusRingOnFocus(bool install) {
  if (install == GetInstallFocusRingOnFocus())
    return;
  if (focus_ring_ && !install) {
    RemoveChildViewT(focus_ring_);
    focus_ring_ = nullptr;
  } else if (!focus_ring_ && install) {
    focus_ring_ = FocusRing::Install(this);
  }
  OnPropertyChanged(&focus_ring_, kPropertyEffectsPaint);
}

bool Button::GetInstallFocusRingOnFocus() const {
  return !!focus_ring_;
}

void Button::SetHotTracked(bool is_hot_tracked) {
  if (state_ != STATE_DISABLED) {
    SetState(is_hot_tracked ? STATE_HOVERED : STATE_NORMAL);
    if (show_ink_drop_when_hot_tracked_) {
      AnimateInkDrop(is_hot_tracked ? views::InkDropState::ACTIVATED
                                    : views::InkDropState::HIDDEN,
                     nullptr);
    }
  }

  if (is_hot_tracked)
    NotifyAccessibilityEvent(ax::mojom::Event::kHover, true);
}

bool Button::IsHotTracked() const {
  return state_ == STATE_HOVERED;
}

void Button::SetFocusPainter(std::unique_ptr<Painter> focus_painter) {
  focus_painter_ = std::move(focus_painter);
}

void Button::SetHighlighted(bool bubble_visible) {
  AnimateInkDrop(bubble_visible ? views::InkDropState::ACTIVATED
                                : views::InkDropState::DEACTIVATED,
                 nullptr);
}

base::CallbackListSubscription Button::AddStateChangedCallback(
    PropertyChangedCallback callback) {
  return AddPropertyChangedCallback(&state_, std::move(callback));
}

Button::KeyClickAction Button::GetKeyClickActionForEvent(
    const ui::KeyEvent& event) {
  if (event.key_code() == ui::VKEY_SPACE)
    return PlatformStyle::kKeyClickActionOnSpace;
  // Note that default buttons also have VKEY_RETURN installed as an accelerator
  // in LabelButton::SetIsDefault(). On platforms where
  // PlatformStyle::kReturnClicksFocusedControl, the logic here will take
  // precedence over that.
  if (event.key_code() == ui::VKEY_RETURN &&
      PlatformStyle::kReturnClicksFocusedControl)
    return KeyClickAction::kOnKeyPress;
  return KeyClickAction::kNone;
}

void Button::SetButtonController(
    std::unique_ptr<ButtonController> button_controller) {
  button_controller_ = std::move(button_controller);
}

gfx::Point Button::GetMenuPosition() const {
  gfx::Rect lb = GetLocalBounds();

  // Offset of the associated menu position.
  constexpr gfx::Vector2d kMenuOffset{-2, -4};

  // The position of the menu depends on whether or not the locale is
  // right-to-left.
  gfx::Point menu_position(lb.right(), lb.bottom());
  if (base::i18n::IsRTL())
    menu_position.set_x(lb.x());

  View::ConvertPointToScreen(this, &menu_position);
  if (base::i18n::IsRTL())
    menu_position.Offset(-kMenuOffset.x(), kMenuOffset.y());
  else
    menu_position += kMenuOffset;

  DCHECK(GetWidget());
  const int max_x_coordinate =
      GetWidget()->GetWorkAreaBoundsInScreen().right() - 1;
  if (max_x_coordinate && max_x_coordinate <= menu_position.x())
    menu_position.set_x(max_x_coordinate - 1);
  return menu_position;
}

bool Button::OnMousePressed(const ui::MouseEvent& event) {
  return button_controller_->OnMousePressed(event);
}

bool Button::OnMouseDragged(const ui::MouseEvent& event) {
  if (state_ != STATE_DISABLED) {
    const bool should_enter_pushed = ShouldEnterPushedState(event);
    const bool should_show_pending =
        should_enter_pushed &&
        button_controller_->notify_action() ==
            ButtonController::NotifyAction::kOnRelease &&
        !InDrag();
    if (HitTestPoint(event.location())) {
      SetState(should_enter_pushed ? STATE_PRESSED : STATE_HOVERED);
      if (should_show_pending && GetInkDrop()->GetTargetInkDropState() ==
                                     views::InkDropState::HIDDEN) {
        AnimateInkDrop(views::InkDropState::ACTION_PENDING, &event);
      }
    } else {
      SetState(STATE_NORMAL);
      if (should_show_pending && GetInkDrop()->GetTargetInkDropState() ==
                                     views::InkDropState::ACTION_PENDING) {
        AnimateInkDrop(views::InkDropState::HIDDEN, &event);
      }
    }
  }
  return true;
}

void Button::OnMouseReleased(const ui::MouseEvent& event) {
  button_controller_->OnMouseReleased(event);
}

void Button::OnMouseCaptureLost() {
  // Starting a drag results in a MouseCaptureLost. Reset button state.
  // TODO(varkha): Reset the state even while in drag. The same logic may
  // applies everywhere so gather any feedback and update.
  if (state_ != STATE_DISABLED)
    SetState(STATE_NORMAL);
  AnimateInkDrop(views::InkDropState::HIDDEN, nullptr /* event */);
  GetInkDrop()->SetHovered(false);
  InkDropHostView::OnMouseCaptureLost();
}

void Button::OnMouseEntered(const ui::MouseEvent& event) {
  button_controller_->OnMouseEntered(event);
}

void Button::OnMouseExited(const ui::MouseEvent& event) {
  button_controller_->OnMouseExited(event);
}

void Button::OnMouseMoved(const ui::MouseEvent& event) {
  button_controller_->OnMouseMoved(event);
}

bool Button::OnKeyPressed(const ui::KeyEvent& event) {
  return button_controller_->OnKeyPressed(event);
}

bool Button::OnKeyReleased(const ui::KeyEvent& event) {
  return button_controller_->OnKeyReleased(event);
}

void Button::OnGestureEvent(ui::GestureEvent* event) {
  button_controller_->OnGestureEvent(event);
}

bool Button::AcceleratorPressed(const ui::Accelerator& accelerator) {
  SetState(STATE_NORMAL);
  NotifyClick(accelerator.ToKeyEvent());
  return true;
}

bool Button::SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) {
  // If this button is focused and the user presses space or enter, don't let
  // that be treated as an accelerator if there is a key click action
  // corresponding to it.
  return GetKeyClickActionForEvent(event) != KeyClickAction::kNone;
}

std::u16string Button::GetTooltipText(const gfx::Point& p) const {
  return tooltip_text_;
}

void Button::ShowContextMenu(const gfx::Point& p,
                             ui::MenuSourceType source_type) {
  if (!context_menu_controller())
    return;

  // We're about to show the context menu. Showing the context menu likely means
  // we won't get a mouse exited and reset state. Reset it now to be sure.
  if (state_ != STATE_DISABLED)
    SetState(STATE_NORMAL);
  if (hide_ink_drop_when_showing_context_menu_) {
    GetInkDrop()->SetHovered(false);
    AnimateInkDrop(InkDropState::HIDDEN, nullptr /* event */);
  }
  InkDropHostView::ShowContextMenu(p, source_type);
}

void Button::OnDragDone() {
  // Only reset the state to normal if the button isn't currently disabled
  // (since disabled buttons may still be able to be dragged).
  if (state_ != STATE_DISABLED)
    SetState(STATE_NORMAL);
  AnimateInkDrop(InkDropState::HIDDEN, nullptr /* event */);
}

void Button::OnPaint(gfx::Canvas* canvas) {
  InkDropHostView::OnPaint(canvas);
  PaintButtonContents(canvas);
  Painter::PaintFocusPainter(this, canvas, focus_painter_.get());
}

void Button::GetAccessibleNodeData(ui::AXNodeData* node_data) {
  node_data->role = ax::mojom::Role::kButton;
  node_data->SetName(GetAccessibleName());
  if (!GetEnabled())
    node_data->SetRestriction(ax::mojom::Restriction::kDisabled);

  switch (state_) {
    case STATE_HOVERED:
      node_data->AddState(ax::mojom::State::kHovered);
      break;
    case STATE_PRESSED:
      node_data->SetCheckedState(ax::mojom::CheckedState::kTrue);
      break;
    case STATE_DISABLED:
      node_data->SetRestriction(ax::mojom::Restriction::kDisabled);
      break;
    case STATE_NORMAL:
    case STATE_COUNT:
      // No additional accessibility node_data set for this button node_data.
      break;
  }
  if (GetEnabled())
    node_data->SetDefaultActionVerb(ax::mojom::DefaultActionVerb::kPress);

  button_controller_->UpdateAccessibleNodeData(node_data);
}

void Button::VisibilityChanged(View* starting_from, bool visible) {
  InkDropHostView::VisibilityChanged(starting_from, visible);
  if (state_ == STATE_DISABLED)
    return;
  SetState(visible && ShouldEnterHoveredState() ? STATE_HOVERED : STATE_NORMAL);
}

void Button::ViewHierarchyChanged(const ViewHierarchyChangedDetails& details) {
  if (!details.is_add && state_ != STATE_DISABLED && details.child == this)
    SetState(STATE_NORMAL);
  InkDropHostView::ViewHierarchyChanged(details);
}

void Button::OnFocus() {
  InkDropHostView::OnFocus();
  if (focus_painter_)
    SchedulePaint();
}

void Button::OnBlur() {
  InkDropHostView::OnBlur();
  if (IsHotTracked() || state_ == STATE_PRESSED) {
    SetState(STATE_NORMAL);
    if (GetInkDrop()->GetTargetInkDropState() != views::InkDropState::HIDDEN)
      AnimateInkDrop(views::InkDropState::HIDDEN, nullptr /* event */);
    // TODO(bruthig) : Fix Buttons to work well when multiple input
    // methods are interacting with a button. e.g. By animating to HIDDEN here
    // it is possible for a Mouse Release to trigger an action however there
    // would be no visual cue to the user that this will occur.
  }
  if (focus_painter_)
    SchedulePaint();
}

std::unique_ptr<InkDrop> Button::CreateInkDrop() {
  std::unique_ptr<InkDrop> ink_drop = InkDropHostView::CreateInkDrop();
  ink_drop->SetShowHighlightOnFocus(!focus_ring_);
  return ink_drop;
}

SkColor Button::GetInkDropBaseColor() const {
  return ink_drop_base_color_;
}

void Button::AnimationProgressed(const gfx::Animation* animation) {
  SchedulePaint();
}

Button::Button(PressedCallback callback)
    : AnimationDelegateViews(this),
      callback_(std::move(callback)),
      ink_drop_base_color_(gfx::kPlaceholderColor) {
  SetFocusBehavior(PlatformStyle::kDefaultFocusBehavior);
  SetProperty(kIsButtonProperty, true);
  hover_animation_.SetSlideDuration(base::TimeDelta::FromMilliseconds(150));
  SetInstallFocusRingOnFocus(true);
  button_controller_ = std::make_unique<ButtonController>(
      this, std::make_unique<DefaultButtonControllerDelegate>(this));
}

void Button::RequestFocusFromEvent() {
  if (request_focus_on_press_)
    RequestFocus();
}

void Button::NotifyClick(const ui::Event& event) {
  if (has_ink_drop_action_on_click_) {
    AnimateInkDrop(InkDropState::ACTION_TRIGGERED,
                   ui::LocatedEvent::FromIfValid(&event));
  }

  if (callback_)
    callback_.Run(event);
}

void Button::OnClickCanceled(const ui::Event& event) {
  if (ShouldUpdateInkDropOnClickCanceled()) {
    if (GetInkDrop()->GetTargetInkDropState() ==
            views::InkDropState::ACTION_PENDING ||
        GetInkDrop()->GetTargetInkDropState() ==
            views::InkDropState::ALTERNATE_ACTION_PENDING) {
      AnimateInkDrop(views::InkDropState::HIDDEN,
                     ui::LocatedEvent::FromIfValid(&event));
    }
  }
}

void Button::OnSetTooltipText(const std::u16string& tooltip_text) {}

void Button::StateChanged(ButtonState old_state) {}

bool Button::IsTriggerableEvent(const ui::Event& event) {
  return button_controller_->IsTriggerableEvent(event);
}

bool Button::ShouldUpdateInkDropOnClickCanceled() const {
  return true;
}

bool Button::ShouldEnterPushedState(const ui::Event& event) {
  return IsTriggerableEvent(event);
}

void Button::PaintButtonContents(gfx::Canvas* canvas) {}

bool Button::ShouldEnterHoveredState() {
  if (!GetVisible())
    return false;

  bool check_mouse_position = true;
#if defined(USE_AURA)
  // If another window has capture, we shouldn't check the current mouse
  // position because the button won't receive any mouse events - so if the
  // mouse was hovered, the button would be stuck in a hovered state (since it
  // would never receive OnMouseExited).
  const Widget* widget = GetWidget();
  if (widget && widget->GetNativeWindow()) {
    aura::Window* root_window = widget->GetNativeWindow()->GetRootWindow();
    aura::client::CaptureClient* capture_client =
        aura::client::GetCaptureClient(root_window);
    aura::Window* capture_window =
        capture_client ? capture_client->GetGlobalCaptureWindow() : nullptr;
    check_mouse_position = !capture_window || capture_window == root_window;
  }
#endif

  return check_mouse_position && IsMouseHovered();
}

void Button::OnEnabledChanged() {
  if (GetEnabled() ? (state_ != STATE_DISABLED) : (state_ == STATE_DISABLED))
    return;

  if (GetEnabled()) {
    bool should_enter_hover_state = ShouldEnterHoveredState();
    SetState(should_enter_hover_state ? STATE_HOVERED : STATE_NORMAL);
    GetInkDrop()->SetHovered(should_enter_hover_state);
  } else {
    SetState(STATE_DISABLED);
    GetInkDrop()->SetHovered(false);
  }
}

DEFINE_ENUM_CONVERTERS(Button::ButtonState,
                       {Button::STATE_NORMAL, u"STATE_NORMAL"},
                       {Button::STATE_HOVERED, u"STATE_HOVERED"},
                       {Button::STATE_PRESSED, u"STATE_PRESSED"},
                       {Button::STATE_DISABLED, u"STATE_DISABLED"})

BEGIN_METADATA(Button, InkDropHostView)
ADD_PROPERTY_METADATA(std::u16string, AccessibleName)
ADD_PROPERTY_METADATA(PressedCallback, Callback)
ADD_PROPERTY_METADATA(bool, AnimateOnStateChange)
ADD_PROPERTY_METADATA(bool, HasInkDropActionOnClick)
ADD_PROPERTY_METADATA(bool, HideInkDropWhenShowingContextMenu)
ADD_PROPERTY_METADATA(SkColor, InkDropBaseColor, metadata::SkColorConverter)
ADD_PROPERTY_METADATA(bool, InstallFocusRingOnFocus)
ADD_PROPERTY_METADATA(bool, RequestFocusOnPress)
ADD_PROPERTY_METADATA(ButtonState, State)
ADD_PROPERTY_METADATA(std::u16string, TooltipText)
ADD_PROPERTY_METADATA(int, TriggerableEventFlags)
END_METADATA

}  // namespace views
