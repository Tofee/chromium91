// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_TEST_MODEL_MOCK_MODEL_TYPE_CHANGE_PROCESSOR_H_
#define COMPONENTS_SYNC_TEST_MODEL_MOCK_MODEL_TYPE_CHANGE_PROCESSOR_H_

#include <memory>
#include <string>

#include "components/sync/model/metadata_batch.h"
#include "components/sync/model/model_type_change_processor.h"
#include "components/sync/model/model_type_sync_bridge.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace syncer {

class MockModelTypeChangeProcessor : public ModelTypeChangeProcessor {
 public:
  MockModelTypeChangeProcessor();
  ~MockModelTypeChangeProcessor() override;
  MOCK_METHOD(void,
              Put,
              (const std::string& storage_key,
               std::unique_ptr<EntityData> entity_data,
               MetadataChangeList* metadata_change_list),
              (override));
  MOCK_METHOD(void,
              Delete,
              (const std::string& storage_key,
               MetadataChangeList* metadata_change_list),
              (override));
  MOCK_METHOD(void,
              UpdateStorageKey,
              (const EntityData& entity_data,
               const std::string& storage_key,
               MetadataChangeList* metadata_change_list),
              (override));
  MOCK_METHOD(void,
              UntrackEntityForStorageKey,
              (const std::string& storage_key),
              (override));
  MOCK_METHOD(void,
              UntrackEntityForClientTagHash,
              (const ClientTagHash& client_tag_hash),
              (override));
  MOCK_METHOD(bool,
              IsEntityUnsynced,
              (const std::string& storage_key),
              (override));
  MOCK_METHOD(base::Time,
              GetEntityCreationTime,
              (const std::string& storage_key),
              (const override));
  MOCK_METHOD(base::Time,
              GetEntityModificationTime,
              (const std::string& storage_key),
              (const override));
  MOCK_METHOD(void,
              OnModelStarting,
              (ModelTypeSyncBridge * bridge),
              (override));
  MOCK_METHOD(void,
              ModelReadyToSync,
              (std::unique_ptr<MetadataBatch> batch),
              (override));
  MOCK_METHOD(bool, IsTrackingMetadata, (), (const override));
  MOCK_METHOD(std::string, TrackedAccountId, (), (override));
  MOCK_METHOD(std::string, TrackedCacheGuid, (), (override));
  MOCK_METHOD(void, ReportError, (const ModelError& error), (override));
  MOCK_METHOD(base::Optional<ModelError>, GetError, (), (const override));
  MOCK_METHOD(base::WeakPtr<ModelTypeControllerDelegate>,
              GetControllerDelegate,
              (),
              (override));

  // Returns a processor that forwards all calls to
  // |this|. |*this| must outlive the returned processor.
  std::unique_ptr<ModelTypeChangeProcessor> CreateForwardingProcessor();

  // Delegates all calls to another instance. |delegate| must not be null and
  // must outlive this object.
  void DelegateCallsByDefaultTo(ModelTypeChangeProcessor* delegate);

 private:
  DISALLOW_COPY_AND_ASSIGN(MockModelTypeChangeProcessor);
};

}  //  namespace syncer

#endif  // COMPONENTS_SYNC_TEST_MODEL_MOCK_MODEL_TYPE_CHANGE_PROCESSOR_H_
