// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_ENGINE_NET_HTTP_POST_PROVIDER_FACTORY_H_
#define COMPONENTS_SYNC_ENGINE_NET_HTTP_POST_PROVIDER_FACTORY_H_

#include <memory>
#include <string>

#include "base/callback.h"
#include "base/memory/scoped_refptr.h"
#include "components/sync/engine/net/network_time_update_callback.h"

namespace network {
class PendingSharedURLLoaderFactory;
}  // namespace network

namespace syncer {

class HttpPostProviderInterface;

// A factory to create HttpPostProviders to hide details about the
// implementations and dependencies.
// A factory instance itself should be owned by whomever uses it to create
// HttpPostProviders.
class HttpPostProviderFactory {
 public:
  virtual ~HttpPostProviderFactory() = default;

  // Obtain a new HttpPostProviderInterface instance, owned by caller.
  virtual scoped_refptr<HttpPostProviderInterface> Create() = 0;
};

using CreateHttpPostProviderFactory =
    base::RepeatingCallback<std::unique_ptr<HttpPostProviderFactory>(
        const std::string& user_agent,
        std::unique_ptr<network::PendingSharedURLLoaderFactory>
            pending_url_loader_factory,
        const NetworkTimeUpdateCallback& network_time_update_callback)>;

}  // namespace syncer

#endif  // COMPONENTS_SYNC_ENGINE_NET_HTTP_POST_PROVIDER_FACTORY_H_
