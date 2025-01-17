// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/fast_ink/laser/laser_pointer_controller_test_api.h"

#include "ash/fast_ink/fast_ink_points.h"
#include "ash/fast_ink/laser/laser_pointer_controller.h"
#include "ash/fast_ink/laser/laser_pointer_view.h"

namespace ash {

LaserPointerControllerTestApi::LaserPointerControllerTestApi(
    LaserPointerController* instance)
    : instance_(instance) {}

LaserPointerControllerTestApi::~LaserPointerControllerTestApi() = default;

void LaserPointerControllerTestApi::SetEnabled(bool enabled) {
  instance_->SetEnabled(enabled);
}

bool LaserPointerControllerTestApi::IsEnabled() const {
  return instance_->is_enabled();
}

bool LaserPointerControllerTestApi::IsShowingLaserPointer() const {
  return !!instance_->laser_pointer_view_widget_;
}

bool LaserPointerControllerTestApi::IsFadingAway() const {
  return IsShowingLaserPointer() &&
         !instance_->GetLaserPointerView()->fadeout_done_.is_null();
}

const fast_ink::FastInkPoints& LaserPointerControllerTestApi::laser_points()
    const {
  return instance_->GetLaserPointerView()->laser_points_;
}

const fast_ink::FastInkPoints&
LaserPointerControllerTestApi::predicted_laser_points() const {
  return instance_->GetLaserPointerView()->predicted_laser_points_;
}

}  // namespace ash
