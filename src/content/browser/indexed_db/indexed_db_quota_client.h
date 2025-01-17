// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_INDEXED_DB_INDEXED_DB_QUOTA_CLIENT_H_
#define CONTENT_BROWSER_INDEXED_DB_INDEXED_DB_QUOTA_CLIENT_H_

#include <set>
#include <string>

#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/thread_annotations.h"
#include "components/services/storage/public/mojom/quota_client.mojom.h"
#include "content/common/content_export.h"
#include "storage/browser/quota/quota_client_type.h"
#include "storage/browser/quota/quota_task.h"
#include "third_party/blink/public/mojom/quota/quota_types.mojom.h"
#include "url/origin.h"

namespace content {
class IndexedDBContextImpl;

// Integrates IndexedDB with the quota management system.
//
// Each instance is owned by an IndexedDBContextImpl.
class IndexedDBQuotaClient : public storage::mojom::QuotaClient {
 public:
  CONTENT_EXPORT explicit IndexedDBQuotaClient(
      IndexedDBContextImpl& indexed_db_context);

  IndexedDBQuotaClient(const IndexedDBQuotaClient&) = delete;
  IndexedDBQuotaClient& operator=(const IndexedDBQuotaClient&) = delete;

  CONTENT_EXPORT ~IndexedDBQuotaClient() override;

  // QuotaClient implementation:
  void GetOriginUsage(const url::Origin& origin,
                      blink::mojom::StorageType type,
                      GetOriginUsageCallback callback) override;
  void GetOriginsForType(blink::mojom::StorageType type,
                         GetOriginsForTypeCallback callback) override;
  void GetOriginsForHost(blink::mojom::StorageType type,
                         const std::string& host,
                         GetOriginsForHostCallback callback) override;
  void DeleteOriginData(const url::Origin& origin,
                        blink::mojom::StorageType type,
                        DeleteOriginDataCallback callback) override;
  void PerformStorageCleanup(blink::mojom::StorageType type,
                             PerformStorageCleanupCallback callback) override;

 private:
  SEQUENCE_CHECKER(sequence_checker_);

  // Raw pointer use is safe here because the IndexedDBContextImpl owns this.
  IndexedDBContextImpl& indexed_db_context_
      GUARDED_BY_CONTEXT(sequence_checker_);

  base::WeakPtrFactory<IndexedDBQuotaClient> weak_ptr_factory_
      GUARDED_BY_CONTEXT(sequence_checker_){this};
};

}  // namespace content

#endif  // CONTENT_BROWSER_INDEXED_DB_INDEXED_DB_QUOTA_CLIENT_H_
