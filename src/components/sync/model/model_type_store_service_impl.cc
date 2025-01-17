// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/model/model_type_store_service_impl.h"

#include <utility>

#include "base/bind.h"
#include "base/callback_helpers.h"
#include "base/feature_list.h"
#include "base/memory/ptr_util.h"
#include "base/optional.h"
#include "base/task/post_task.h"
#include "base/task/task_traits.h"
#include "base/task/thread_pool.h"
#include "base/task_runner_util.h"
#include "base/threading/sequenced_task_runner_handle.h"
#include "components/sync/base/sync_base_switches.h"
#include "components/sync/model/blocking_model_type_store_impl.h"
#include "components/sync/model/model_type_store_backend.h"
#include "components/sync/model/model_type_store_impl.h"

namespace syncer {
namespace {

constexpr base::FilePath::CharType kSyncDataFolderName[] =
    FILE_PATH_LITERAL("Sync Data");

constexpr base::FilePath::CharType kLevelDBFolderName[] =
    FILE_PATH_LITERAL("LevelDB");

// Initialized ModelTypeStoreBackend, on the backend sequence.
void InitOnBackendSequence(const base::FilePath& level_db_path,
                           scoped_refptr<ModelTypeStoreBackend> store_backend) {
  base::Optional<ModelError> error = store_backend->Init(level_db_path);
  if (error) {
    LOG(ERROR) << "Failed to initialize ModelTypeStore backend: "
               << error->ToString();
    return;
  }
}

std::unique_ptr<BlockingModelTypeStoreImpl, base::OnTaskRunnerDeleter>
CreateBlockingModelTypeStoreOnBackendSequence(
    ModelType type,
    scoped_refptr<ModelTypeStoreBackend> store_backend) {
  BlockingModelTypeStoreImpl* blocking_store = nullptr;
  if (store_backend->IsInitialized()) {
    blocking_store = new BlockingModelTypeStoreImpl(type, store_backend);
  }
  return std::unique_ptr<BlockingModelTypeStoreImpl,
                         base::OnTaskRunnerDeleter /*[]*/>(
      blocking_store,
      base::OnTaskRunnerDeleter(base::SequencedTaskRunnerHandle::Get()));
}

void ConstructModelTypeStoreOnFrontendSequence(
    ModelType type,
    scoped_refptr<base::SequencedTaskRunner> backend_task_runner,
    ModelTypeStore::InitCallback callback,
    std::unique_ptr<BlockingModelTypeStoreImpl, base::OnTaskRunnerDeleter>
        blocking_store) {
  if (blocking_store) {
    std::move(callback).Run(
        /*error=*/base::nullopt,
        std::make_unique<ModelTypeStoreImpl>(type, std::move(blocking_store),
                                             backend_task_runner));
  } else {
    std::move(callback).Run(
        ModelError(FROM_HERE, "ModelTypeStore backend initialization failed"),
        /*store=*/nullptr);
  }
}

void CreateModelTypeStoreOnFrontendSequence(
    scoped_refptr<base::SequencedTaskRunner> backend_task_runner,
    scoped_refptr<ModelTypeStoreBackend> store_backend,
    ModelType type,
    ModelTypeStore::InitCallback callback) {
  // BlockingModelTypeStoreImpl must be instantiated in the backend sequence.
  // This also guarantees that the creation is sequenced with the backend's
  // initialization, since we can't know for sure that InitOnBackendSequence()
  // has already run.
  auto task = base::BindOnce(&CreateBlockingModelTypeStoreOnBackendSequence,
                             type, store_backend);

  auto reply = base::BindOnce(&ConstructModelTypeStoreOnFrontendSequence, type,
                              backend_task_runner, std::move(callback));

  base::PostTaskAndReplyWithResult(backend_task_runner.get(), FROM_HERE,
                                   std::move(task), std::move(reply));
}

}  // namespace

ModelTypeStoreServiceImpl::ModelTypeStoreServiceImpl(
    const base::FilePath& base_path)
    : sync_path_(base_path.Append(base::FilePath(kSyncDataFolderName))),
      leveldb_path_(sync_path_.Append(base::FilePath(kLevelDBFolderName))),
      backend_task_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskShutdownBehavior::SKIP_ON_SHUTDOWN})),
      store_backend_(ModelTypeStoreBackend::CreateUninitialized()) {
  DCHECK(backend_task_runner_);
  backend_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&InitOnBackendSequence, leveldb_path_, store_backend_));
}

ModelTypeStoreServiceImpl::~ModelTypeStoreServiceImpl() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
}

const base::FilePath& ModelTypeStoreServiceImpl::GetSyncDataPath() const {
  return sync_path_;
}

RepeatingModelTypeStoreFactory ModelTypeStoreServiceImpl::GetStoreFactory() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  return base::BindRepeating(&CreateModelTypeStoreOnFrontendSequence,
                             backend_task_runner_, store_backend_);
}

scoped_refptr<base::SequencedTaskRunner>
ModelTypeStoreServiceImpl::GetBackendTaskRunner() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  return backend_task_runner_;
}

}  // namespace syncer
