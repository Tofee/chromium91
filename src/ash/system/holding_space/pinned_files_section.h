// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_HOLDING_SPACE_PINNED_FILES_SECTION_H_
#define ASH_SYSTEM_HOLDING_SPACE_PINNED_FILES_SECTION_H_

#include <memory>

#include "ash/system/holding_space/holding_space_item_views_section.h"

namespace ash {

// Section for pinned files in the `PinnedFilesBubble`.
class PinnedFilesSection : public HoldingSpaceItemViewsSection {
 public:
  explicit PinnedFilesSection(HoldingSpaceItemViewDelegate* delegate);
  PinnedFilesSection(const PinnedFilesSection& other) = delete;
  PinnedFilesSection& operator=(const PinnedFilesSection& other) = delete;
  ~PinnedFilesSection() override;

  // HoldingSpaceItemViewsSection:
  const char* GetClassName() const override;
  gfx::Size GetMinimumSize() const override;
  std::unique_ptr<views::View> CreateHeader() override;
  std::unique_ptr<views::View> CreateContainer() override;
  std::unique_ptr<HoldingSpaceItemView> CreateView(
      const HoldingSpaceItem* item) override;
  std::unique_ptr<views::View> CreatePlaceholder() override;
};

}  // namespace ash

#endif  // ASH_SYSTEM_HOLDING_SPACE_PINNED_FILES_SECTION_H_
