// Copyright (c) 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_ERROR_PAGE_COMMON_ALT_GAME_IMAGES_H_
#define COMPONENTS_ERROR_PAGE_COMMON_ALT_GAME_IMAGES_H_

#include <string>

#include "base/feature_list.h"

namespace error_page {

extern const base::Feature kNetErrorAltGameMode;
extern const base::FeatureParam<std::string> kNetErrorAltGameModeKey;

// Gets the value of kNetErrorAltGameMode.
bool EnableAltGameMode();

// Returns a data URL corresponding to the image ID and scale.
std::string GetAltGameImage(int image_id, int scale);

// Returns an image ID.
int ChooseAltGame();

}  // namespace error_page

#endif  // COMPONENTS_ERROR_PAGE_COMMON_ALT_GAME_IMAGES_H_
