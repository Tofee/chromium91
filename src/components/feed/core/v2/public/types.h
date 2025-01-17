// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_FEED_CORE_V2_PUBLIC_TYPES_H_
#define COMPONENTS_FEED_CORE_V2_PUBLIC_TYPES_H_

#include <iosfwd>
#include <map>
#include <string>

#include "base/optional.h"
#include "base/strings/string_piece.h"
#include "base/time/time.h"
#include "base/util/type_safety/id_type.h"
#include "base/version.h"
#include "components/version_info/channel.h"
#include "url/gurl.h"

namespace feed {

enum class RefreshTaskId {
  kRefreshForYouFeed,
  kRefreshWebFeed,
};

// Information about the Chrome build.
struct ChromeInfo {
  version_info::Channel channel{};
  base::Version version;
};
// Device display metrics.
struct DisplayMetrics {
  float density;
  uint32_t width_pixels;
  uint32_t height_pixels;
};

// A unique ID for an ephemeral change.
using EphemeralChangeId = util::IdTypeU32<class EphemeralChangeIdClass>;
using SurfaceId = util::IdTypeU32<class SurfaceIdClass>;
using ImageFetchId = util::IdTypeU32<class ImageFetchIdClass>;

// A map of trial names (key) to group names (value) that is
// sent from the server.
typedef std::map<std::string, std::string> Experiments;

struct NetworkResponseInfo {
  NetworkResponseInfo();
  ~NetworkResponseInfo();
  NetworkResponseInfo(const NetworkResponseInfo&);
  NetworkResponseInfo& operator=(const NetworkResponseInfo&);

  // A union of net::Error (if the request failed) and the http
  // status code(if the request succeeded in reaching the server).
  int32_t status_code = 0;
  base::TimeDelta fetch_duration;
  base::Time fetch_time;
  std::string bless_nonce;
  GURL base_request_url;
  size_t response_body_bytes = 0;
  bool was_signed_in = false;
};

struct NetworkResponse {
  // HTTP response body.
  std::string response_bytes;
  // HTTP status code if available, or net::Error otherwise.
  int status_code;

  NetworkResponse() = default;
  NetworkResponse(NetworkResponse&& other) = default;
  NetworkResponse& operator=(NetworkResponse&& other) = default;
};

// For the snippets-internals page.
struct DebugStreamData {
  static const int kVersion = 1;  // If a field changes, increment.

  DebugStreamData();
  ~DebugStreamData();
  DebugStreamData(const DebugStreamData&);
  DebugStreamData& operator=(const DebugStreamData&);

  base::Optional<NetworkResponseInfo> fetch_info;
  base::Optional<NetworkResponseInfo> upload_info;
  std::string load_stream_status;
};

std::string SerializeDebugStreamData(const DebugStreamData& data);
base::Optional<DebugStreamData> DeserializeDebugStreamData(
    base::StringPiece base64_encoded);

// Information about a web page which may be used to determine an associated web
// feed.
class WebFeedPageInformation {
 public:
  WebFeedPageInformation() = default;
  // The URL for the page. `url().has_ref()` is always false.
  GURL url() const { return url_; }

  // Set the URL for the page. Trims off the URL ref.
  void SetUrl(const GURL& url);

 private:
  GURL url_;
  // TODO(crbug/1152592): There will be additional optional information.
};

// GENERATED_JAVA_ENUM_PACKAGE: org.chromium.chrome.browser.feed.webfeed
enum class WebFeedSubscriptionStatus {
  kUnknown = 0,
  kSubscribed = 1,
  kNotSubscribed = 2,
  kSubscribeInProgress = 3,
  kUnsubscribeInProgress = 4,
};
std::ostream& operator<<(std::ostream& out, WebFeedSubscriptionStatus value);

// Information about a web feed.
struct WebFeedMetadata {
  WebFeedMetadata();
  WebFeedMetadata(const WebFeedMetadata&);
  WebFeedMetadata(WebFeedMetadata&&);
  WebFeedMetadata& operator=(const WebFeedMetadata&);
  WebFeedMetadata& operator=(WebFeedMetadata&&);

  // Unique ID of the web feed. Empty if the client knows of no web feed.
  std::string web_feed_id;
  // Whether the subscribed Web Feed has content available for fetching.
  bool is_active = false;
  // Whether the Web Feed is recommended by the web feeds service.
  bool is_recommended = false;
  std::string title;
  GURL publisher_url;
  WebFeedSubscriptionStatus subscription_status =
      WebFeedSubscriptionStatus::kUnknown;
};
std::ostream& operator<<(std::ostream& out, const WebFeedMetadata& value);

// GENERATED_JAVA_ENUM_PACKAGE: org.chromium.chrome.browser.feed.webfeed
enum class WebFeedSubscriptionRequestStatus {
  kUnknown = 0,
  kSuccess = 1,
  kFailedOffline = 2,
  kFailedTooManySubscriptions = 3,
  kFailedUnknownError = 4,
  kAbortWebFeedSubscriptionPendingClearAll = 5,
};
std::ostream& operator<<(std::ostream& out,
                         WebFeedSubscriptionRequestStatus value);

}  // namespace feed

#endif  // COMPONENTS_FEED_CORE_V2_PUBLIC_TYPES_H_
