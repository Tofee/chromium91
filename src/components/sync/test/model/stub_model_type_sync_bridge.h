// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_TEST_MODEL_STUB_MODEL_TYPE_SYNC_BRIDGE_H_
#define COMPONENTS_SYNC_TEST_MODEL_STUB_MODEL_TYPE_SYNC_BRIDGE_H_

#include <memory>
#include <string>

#include "base/optional.h"
#include "components/sync/model/model_type_sync_bridge.h"

namespace syncer {

// A non-functional implementation of ModelTypeSyncBridge for
// testing purposes.
class StubModelTypeSyncBridge : public ModelTypeSyncBridge {
 public:
  explicit StubModelTypeSyncBridge(
      std::unique_ptr<ModelTypeChangeProcessor> change_processor);
  ~StubModelTypeSyncBridge() override;

  std::unique_ptr<MetadataChangeList> CreateMetadataChangeList() override;
  base::Optional<ModelError> MergeSyncData(
      std::unique_ptr<MetadataChangeList> metadata_change_list,
      EntityChangeList entity_data) override;
  base::Optional<ModelError> ApplySyncChanges(
      std::unique_ptr<MetadataChangeList> metadata_change_list,
      EntityChangeList entity_changes) override;
  void GetData(StorageKeyList storage_keys, DataCallback callback) override;
  void GetAllDataForDebugging(DataCallback callback) override;
  std::string GetClientTag(const EntityData& entity_data) override;
  std::string GetStorageKey(const EntityData& entity_data) override;
};

}  // namespace syncer

#endif  // COMPONENTS_SYNC_TEST_MODEL_STUB_MODEL_TYPE_SYNC_BRIDGE_H_
