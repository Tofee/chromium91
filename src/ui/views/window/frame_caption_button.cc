// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/window/frame_caption_button.h"

#include <memory>
#include <utility>

#include "ui/base/hit_test.h"
#include "ui/gfx/animation/slide_animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/rrect_f.h"
#include "ui/views/animation/flood_fill_ink_drop_ripple.h"
#include "ui/views/animation/ink_drop_highlight.h"
#include "ui/views/animation/ink_drop_impl.h"
#include "ui/views/animation/ink_drop_ripple.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/metadata/metadata_impl_macros.h"
#include "ui/views/window/hit_test_utils.h"

namespace views {

namespace {

// Ink drop parameters.
constexpr float kInkDropVisibleOpacity = 0.06f;

// The duration of the fade out animation of the old icon during a crossfade
// animation as a ratio of the duration of |swap_images_animation_|.
constexpr float kFadeOutRatio = 0.5f;

// The ratio applied to the button's alpha when the button is disabled.
constexpr float kDisabledButtonAlphaRatio = 0.5f;

}  // namespace

// Custom highlight path generator for clipping ink drops and drawing focus
// rings.
class FrameCaptionButton::HighlightPathGenerator
    : public views::HighlightPathGenerator {
 public:
  explicit HighlightPathGenerator(FrameCaptionButton* frame_caption_button)
      : frame_caption_button_(frame_caption_button) {}
  HighlightPathGenerator(const HighlightPathGenerator&) = delete;
  HighlightPathGenerator& operator=(const HighlightPathGenerator&) = delete;
  ~HighlightPathGenerator() override = default;

  // views::HighlightPathGenerator:
  base::Optional<gfx::RRectF> GetRoundRect(const gfx::RectF& rect) override {
    gfx::Rect bounds = gfx::ToRoundedRect(rect);
    bounds.Inset(frame_caption_button_->GetInkdropInsets(bounds.size()));
    return gfx::RRectF(gfx::RectF(bounds),
                       frame_caption_button_->GetInkDropCornerRadius());
  }

 private:
  FrameCaptionButton* const frame_caption_button_;
};

FrameCaptionButton::FrameCaptionButton(PressedCallback callback,
                                       CaptionButtonIcon icon,
                                       int hit_test_type)
    : Button(std::move(callback)),
      icon_(icon),
      swap_images_animation_(std::make_unique<gfx::SlideAnimation>(this)) {
  views::SetHitTestComponent(this, hit_test_type);
  // Not focusable by default, only for accessibility.
  SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);

  SetAnimateOnStateChange(true);
  swap_images_animation_->Reset(1);

  SetHasInkDropActionOnClick(true);
  SetInkDropMode(InkDropMode::ON);
  SetInkDropVisibleOpacity(kInkDropVisibleOpacity);
  UpdateInkDropBaseColor();

  views::HighlightPathGenerator::Install(
      this, std::make_unique<HighlightPathGenerator>(this));

  // Do not flip the gfx::Canvas passed to the OnPaint() method. The snap left
  // and snap right button icons should not be flipped. The other icons are
  // horizontally symmetrical.
}

FrameCaptionButton::~FrameCaptionButton() = default;

// static
SkColor FrameCaptionButton::GetButtonColor(SkColor background_color) {
  // Use IsDark() to change target colors instead of PickContrastingColor(), so
  // that DefaultFrameHeader::GetTitleColor() (which uses different target
  // colors) can change between light/dark targets at the same time.  It looks
  // bad when the title and caption buttons disagree about whether to be light
  // or dark.
  const SkColor default_foreground = color_utils::IsDark(background_color)
                                         ? gfx::kGoogleGrey200
                                         : gfx::kGoogleGrey700;
  const SkColor high_contrast_foreground =
      color_utils::GetColorWithMaxContrast(background_color);
  return color_utils::BlendForMinContrast(
             default_foreground, background_color, high_contrast_foreground,
             color_utils::kMinimumVisibleContrastRatio)
      .color;
}

// static
float FrameCaptionButton::GetInactiveButtonColorAlphaRatio() {
  return 0.38f;
}

void FrameCaptionButton::SetImage(CaptionButtonIcon icon,
                                  Animate animate,
                                  const gfx::VectorIcon& icon_definition) {
  gfx::ImageSkia new_icon_image =
      gfx::CreateVectorIcon(icon_definition, GetButtonColor(background_color_));

  // The early return is dependent on |animate| because callers use SetImage()
  // with ANIMATE_NO to progress the crossfade animation to the end.
  if (icon == icon_ &&
      (animate == ANIMATE_YES || !swap_images_animation_->is_animating()) &&
      new_icon_image.BackedBySameObjectAs(icon_image_)) {
    return;
  }

  if (animate == ANIMATE_YES)
    crossfade_icon_image_ = icon_image_;

  icon_ = icon;
  icon_definition_ = &icon_definition;
  icon_image_ = new_icon_image;

  if (animate == ANIMATE_YES) {
    swap_images_animation_->Reset(0);
    swap_images_animation_->SetSlideDuration(
        base::TimeDelta::FromMilliseconds(200));
    swap_images_animation_->Show();
  } else {
    swap_images_animation_->Reset(1);
  }

  SchedulePaint();
}

bool FrameCaptionButton::IsAnimatingImageSwap() const {
  return swap_images_animation_->is_animating();
}

void FrameCaptionButton::SetAlpha(int alpha) {
  if (alpha_ != alpha) {
    alpha_ = alpha;
    SchedulePaint();
  }
}

void FrameCaptionButton::OnGestureEvent(ui::GestureEvent* event) {
  // Button does not become pressed when the user drags off and then back
  // onto the button. Make FrameCaptionButton pressed in this case because this
  // behavior is more consistent with AlternateFrameSizeButton.
  if (event->type() == ui::ET_GESTURE_SCROLL_BEGIN ||
      event->type() == ui::ET_GESTURE_SCROLL_UPDATE) {
    if (HitTestPoint(event->location())) {
      SetState(STATE_PRESSED);
      RequestFocus();
      event->StopPropagation();
    } else {
      SetState(STATE_NORMAL);
    }
  } else if (event->type() == ui::ET_GESTURE_SCROLL_END) {
    if (HitTestPoint(event->location())) {
      SetState(STATE_HOVERED);
      NotifyClick(*event);
      event->StopPropagation();
    }
  }

  if (!event->handled())
    Button::OnGestureEvent(event);
}

views::PaintInfo::ScaleType FrameCaptionButton::GetPaintScaleType() const {
  return views::PaintInfo::ScaleType::kUniformScaling;
}

std::unique_ptr<views::InkDrop> FrameCaptionButton::CreateInkDrop() {
  auto ink_drop = std::make_unique<views::InkDropImpl>(this, size());
  ink_drop->SetAutoHighlightMode(views::InkDropImpl::AutoHighlightMode::NONE);
  ink_drop->SetShowHighlightOnHover(false);
  return ink_drop;
}

std::unique_ptr<views::InkDropRipple> FrameCaptionButton::CreateInkDropRipple()
    const {
  return std::make_unique<views::FloodFillInkDropRipple>(
      size(), GetInkdropInsets(size()), GetInkDropCenterBasedOnLastEvent(),
      GetInkDropBaseColor(), GetInkDropVisibleOpacity());
}

void FrameCaptionButton::SetBackgroundColor(SkColor background_color) {
  if (background_color_ == background_color)
    return;

  background_color_ = background_color;
  // Refresh the icon since the color may have changed.
  if (icon_definition_)
    SetImage(icon_, ANIMATE_NO, *icon_definition_);
  UpdateInkDropBaseColor();
}

SkColor FrameCaptionButton::GetBackgroundColor() const {
  return background_color_;
}

void FrameCaptionButton::SetInkDropCornerRadius(int ink_drop_corner_radius) {
  ink_drop_corner_radius_ = ink_drop_corner_radius;
  // Changes to |ink_drop_corner_radius| will affect the ink drop. Therefore
  // this effect is handled by the ink drop.
  OnPropertyChanged(&ink_drop_corner_radius_, kPropertyEffectsNone);
}

int FrameCaptionButton::GetInkDropCornerRadius() const {
  return ink_drop_corner_radius_;
}

void FrameCaptionButton::SetPaintAsActive(bool paint_as_active) {
  if (paint_as_active == paint_as_active_)
    return;
  paint_as_active_ = paint_as_active;
  OnPropertyChanged(&paint_as_active_, kPropertyEffectsPaint);
}

bool FrameCaptionButton::GetPaintAsActive() const {
  return paint_as_active_;
}

void FrameCaptionButton::PaintButtonContents(gfx::Canvas* canvas) {
  constexpr SkAlpha kHighlightVisibleOpacity = 0x14;
  SkAlpha highlight_alpha = SK_AlphaTRANSPARENT;
  if (hover_animation().is_animating()) {
    highlight_alpha = hover_animation().CurrentValueBetween(
        SK_AlphaTRANSPARENT, kHighlightVisibleOpacity);
  } else if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED) {
    // Painting a circular highlight in both "hovered" and "pressed" states
    // simulates and ink drop highlight mode of
    // AutoHighlightMode::SHOW_ON_RIPPLE.
    highlight_alpha = kHighlightVisibleOpacity;
  }

  if (highlight_alpha != SK_AlphaTRANSPARENT) {
    // We paint the highlight manually here rather than relying on the ink drop
    // highlight as it doesn't work well when the button size is changing while
    // the window is moving as a result of the animation from normal to
    // maximized state or vice versa. https://crbug.com/840901.
    cc::PaintFlags flags;
    flags.setColor(GetInkDropBaseColor());
    flags.setAlpha(highlight_alpha);
    const gfx::Point center(GetMirroredRect(GetContentsBounds()).CenterPoint());
    canvas->DrawCircle(center, ink_drop_corner_radius_, flags);
  }

  int icon_alpha = swap_images_animation_->CurrentValueBetween(0, 255);
  int crossfade_icon_alpha = 0;
  if (icon_alpha < static_cast<int>(kFadeOutRatio * 255))
    crossfade_icon_alpha = static_cast<int>(255 - icon_alpha / kFadeOutRatio);

  int centered_origin_x = (width() - icon_image_.width()) / 2;
  int centered_origin_y = (height() - icon_image_.height()) / 2;

  if (crossfade_icon_alpha > 0 && !crossfade_icon_image_.isNull()) {
    canvas->SaveLayerAlpha(GetAlphaForIcon(alpha_));
    cc::PaintFlags flags;
    flags.setAlpha(icon_alpha);
    canvas->DrawImageInt(icon_image_, centered_origin_x, centered_origin_y,
                         flags);

    flags.setAlpha(crossfade_icon_alpha);
    flags.setBlendMode(SkBlendMode::kPlus);
    canvas->DrawImageInt(crossfade_icon_image_, centered_origin_x,
                         centered_origin_y, flags);
    canvas->Restore();
  } else {
    if (!swap_images_animation_->is_animating())
      icon_alpha = alpha_;
    cc::PaintFlags flags;
    flags.setAlpha(GetAlphaForIcon(icon_alpha));
    canvas->DrawImageInt(icon_image_, centered_origin_x, centered_origin_y,
                         flags);
  }
}

int FrameCaptionButton::GetAlphaForIcon(int base_alpha) const {
  if (!GetEnabled())
    return base_alpha * kDisabledButtonAlphaRatio;

  if (paint_as_active_)
    return base_alpha;

  // Paint icons as active when they are hovered over or pressed.
  double inactive_alpha = GetInactiveButtonColorAlphaRatio();

  if (hover_animation().is_animating()) {
    inactive_alpha =
        hover_animation().CurrentValueBetween(inactive_alpha, 1.0f);
  } else if (GetState() == STATE_PRESSED || GetState() == STATE_HOVERED) {
    inactive_alpha = 1.0f;
  }
  return base_alpha * inactive_alpha;
}

gfx::Insets FrameCaptionButton::GetInkdropInsets(
    const gfx::Size& button_size) const {
  const gfx::Size kInkDropHighlightSize{2 * ink_drop_corner_radius_,
                                        2 * ink_drop_corner_radius_};
  return gfx::Insets(
      (button_size.height() - kInkDropHighlightSize.height()) / 2,
      (button_size.width() - kInkDropHighlightSize.width()) / 2);
}

void FrameCaptionButton::UpdateInkDropBaseColor() {
  using color_utils::GetColorWithMaxContrast;
  // A typical implementation would simply do
  // GetColorWithMaxContrast(background_color_).  However, this could look odd
  // if we use a light button glyph and dark ink drop or vice versa.  So
  // instead, use the lightest/darkest color in the same direction as the button
  // glyph color.
  // TODO(pkasting): It would likely be better to make the button glyph always
  // be an alpha-blended version of GetColorWithMaxContrast(background_color_).
  const SkColor button_color = GetButtonColor(background_color_);
  SetInkDropBaseColor(
      GetColorWithMaxContrast(GetColorWithMaxContrast(button_color)));
}

DEFINE_ENUM_CONVERTERS(
    views::CaptionButtonIcon,
    {{views::CaptionButtonIcon::CAPTION_BUTTON_ICON_MINIMIZE,
      u"CAPTION_BUTTON_ICON_MINIMIZE"},
     {views::CaptionButtonIcon::CAPTION_BUTTON_ICON_MAXIMIZE_RESTORE,
      u"CAPTION_BUTTON_ICON_MAXIMIZE_RESTORE"},
     {views::CaptionButtonIcon::CAPTION_BUTTON_ICON_CLOSE,
      u"CAPTION_BUTTON_ICON_CLOSE"},
     {views::CaptionButtonIcon::CAPTION_BUTTON_ICON_LEFT_SNAPPED,
      u"CAPTION_BUTTON_ICON_LEFT_SNAPPED"},
     {views::CaptionButtonIcon::CAPTION_BUTTON_ICON_RIGHT_SNAPPED,
      u"CAPTION_BUTTON_ICON_RIGHT_SNAPPED"},
     {views::CaptionButtonIcon::CAPTION_BUTTON_ICON_BACK,
      u"CAPTION_BUTTON_ICON_BACK"},
     {views::CaptionButtonIcon::CAPTION_BUTTON_ICON_LOCATION,
      u"CAPTION_BUTTON_ICON_LOCATION"},
     {views::CaptionButtonIcon::CAPTION_BUTTON_ICON_MENU,
      u"CAPTION_BUTTON_ICON_MENU"},
     {views::CaptionButtonIcon::CAPTION_BUTTON_ICON_ZOOM,
      u"CAPTION_BUTTON_ICON_ZOOM"},
     {views::CaptionButtonIcon::CAPTION_BUTTON_ICON_COUNT,
      u"CAPTION_BUTTON_ICON_COUNT"}})

BEGIN_METADATA(FrameCaptionButton, Button)
ADD_PROPERTY_METADATA(SkColor, BackgroundColor, metadata::SkColorConverter)
ADD_PROPERTY_METADATA(int, InkDropCornerRadius)
ADD_READONLY_PROPERTY_METADATA(CaptionButtonIcon, Icon)
ADD_PROPERTY_METADATA(bool, PaintAsActive)
END_METADATA

}  // namespace views
