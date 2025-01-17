// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/feed/core/v2/feed_network.h"

#include "components/feed/core/proto/v2/wire/request.pb.h"
#include "components/feed/core/proto/v2/wire/response.pb.h"
#include "components/feed/core/proto/v2/wire/upload_actions_request.pb.h"
#include "components/feed/core/proto/v2/wire/upload_actions_response.pb.h"
#include "components/feed/core/proto/v2/wire/web_feeds.pb.h"
#include "components/feed/core/v2/metrics_reporter.h"

namespace feed {

FeedNetwork::QueryRequestResult::QueryRequestResult() = default;
FeedNetwork::QueryRequestResult::~QueryRequestResult() = default;
FeedNetwork::QueryRequestResult::QueryRequestResult(QueryRequestResult&&) =
    default;
FeedNetwork::QueryRequestResult& FeedNetwork::QueryRequestResult::operator=(
    QueryRequestResult&&) = default;

FeedNetwork::~FeedNetwork() = default;

// static
void FeedNetwork::ParseAndForwardApiResponseBegin(
    NetworkRequestType request_type,
    const RawResponse& raw_response) {
  MetricsReporter::NetworkRequestComplete(
      request_type, raw_response.response_info.status_code);
}

}  // namespace feed
