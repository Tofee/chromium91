// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_PROJECTOR_TEST_MOCK_PROJECTOR_METADATA_CONTROLLER_H_
#define ASH_PROJECTOR_TEST_MOCK_PROJECTOR_METADATA_CONTROLLER_H_

#include "ash/projector/projector_metadata_controller.h"
#include "base/files/file_path.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace ash {

// A mock implementation of ProjectorMetadataController for use in tests.
class ASH_EXPORT MockProjectorMetadataController
    : public ProjectorMetadataController {
 public:
  MockProjectorMetadataController();

  MockProjectorMetadataController(const MockProjectorMetadataController&) =
      delete;
  MockProjectorMetadataController& operator=(
      const MockProjectorMetadataController&) = delete;

  ~MockProjectorMetadataController() override;

  // ProjectorMetadataController:
  MOCK_METHOD0(OnRecordingStarted, void());
  MOCK_METHOD4(RecordTranscription,
               void(const std::string& transcription,
                    const base::TimeDelta start_time,
                    const base::TimeDelta end_time,
                    const std::vector<base::TimeDelta>& word_alignments));
  MOCK_METHOD0(RecordKeyIdea, void());
  MOCK_METHOD1(SaveMetadata, void(const base::FilePath& video_file_path));
};

}  // namespace ash
#endif  // ASH_PROJECTOR_TEST_MOCK_PROJECTOR_METADATA_CONTROLLER_H_
