// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/feed/core/v2/proto_util.h"

#include <tuple>
#include <vector>

#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/system/sys_info.h"
#include "build/build_config.h"
#include "components/feed/core/proto/v2/store.pb.h"
#include "components/feed/core/proto/v2/wire/capability.pb.h"
#include "components/feed/core/proto/v2/wire/chrome_client_info.pb.h"
#include "components/feed/core/proto/v2/wire/feed_request.pb.h"
#include "components/feed/core/proto/v2/wire/request.pb.h"
#include "components/feed/core/v2/config.h"
#include "components/feed/core/v2/feed_stream.h"
#include "components/feed/feed_feature_list.h"

#if defined(OS_ANDROID)
#include "base/android/build_info.h"
#endif

namespace feed {
namespace {
feedwire::Version::Architecture GetBuildArchitecture() {
#if defined(ARCH_CPU_X86_64)
  return feedwire::Version::X86_64;
#elif defined(ARCH_CPU_X86)
  return feedwire::Version::X86;
#elif defined(ARCH_CPU_MIPS64)
  return feedwire::Version::MIPS64;
#elif defined(ARCH_CPU_MIPS)
  return feedwire::Version::MIPS;
#elif defined(ARCH_CPU_ARM64)
  return feedwire::Version::ARM64;
#elif defined(ARCH_CPU_ARMEL)
  return feedwire::Version::ARM;
#else
  return feedwire::Version::UNKNOWN_ARCHITECTURE;
#endif
}

feedwire::Version::Architecture GetSystemArchitecture() {
  // By default, use |GetBuildArchitecture()|.
  // In the case of x86 and ARM, the OS might be x86_64 or ARM_64.
  feedwire::Version::Architecture build_arch = GetBuildArchitecture();
  if (build_arch == feedwire::Version::X86 &&
      base::SysInfo::OperatingSystemArchitecture() == "x86_64") {
    return feedwire::Version::X86_64;
  }
  if (feedwire::Version::ARM &&
      base::SysInfo::OperatingSystemArchitecture() == "arm64") {
    return feedwire::Version::ARM64;
  }
  return build_arch;
}

feedwire::Version::BuildType GetBuildType(version_info::Channel channel) {
  switch (channel) {
    case version_info::Channel::CANARY:
      return feedwire::Version::ALPHA;
    case version_info::Channel::DEV:
      return feedwire::Version::DEV;
    case version_info::Channel::BETA:
      return feedwire::Version::BETA;
    case version_info::Channel::STABLE:
      return feedwire::Version::RELEASE;
    default:
      return feedwire::Version::UNKNOWN_BUILD_TYPE;
  }
}

feedwire::Version GetPlatformVersionMessage() {
  feedwire::Version result;
  result.set_architecture(GetSystemArchitecture());
  result.set_build_type(feedwire::Version::RELEASE);

  int32_t major, minor, revision;
  base::SysInfo::OperatingSystemVersionNumbers(&major, &minor, &revision);
  result.set_major(major);
  result.set_minor(minor);
  result.set_revision(revision);
#if defined(OS_ANDROID)
  result.set_api_version(base::android::BuildInfo::GetInstance()->sdk_int());
#endif
  return result;
}

feedwire::Version GetAppVersionMessage(const ChromeInfo& chrome_info) {
  feedwire::Version result;
  result.set_architecture(GetBuildArchitecture());
  result.set_build_type(GetBuildType(chrome_info.channel));
  // Chrome's version is in the format: MAJOR,MINOR,BUILD,PATCH.
  const std::vector<uint32_t>& numbers = chrome_info.version.components();
  if (numbers.size() > 3) {
    result.set_major(static_cast<int32_t>(numbers[0]));
    result.set_minor(static_cast<int32_t>(numbers[1]));
    result.set_build(static_cast<int32_t>(numbers[2]));
    result.set_revision(static_cast<int32_t>(numbers[3]));
  }

#if defined(OS_ANDROID)
  result.set_api_version(base::android::BuildInfo::GetInstance()->sdk_int());
#endif
  return result;
}

feedwire::Request CreateFeedQueryRequest(
    feedwire::FeedQuery::RequestReason request_reason,
    const RequestMetadata& request_metadata,
    const std::string& consistency_token,
    const std::string& next_page_token) {
  feedwire::Request request;
  request.set_request_version(feedwire::Request::FEED_QUERY);

  feedwire::FeedRequest& feed_request = *request.mutable_feed_request();
  feed_request.add_client_capability(feedwire::Capability::BASE_UI);
  feed_request.add_client_capability(feedwire::Capability::CARD_MENU);
  feed_request.add_client_capability(feedwire::Capability::LOTTIE_ANIMATIONS);
  feed_request.add_client_capability(
      feedwire::Capability::LONG_PRESS_CARD_MENU);
  // Add Share capability if sharing is turned on.
  if (base::FeatureList::IsEnabled(kFeedShare)) {
    feed_request.add_client_capability(feedwire::Capability::SHARE);
  }
  for (auto capability : GetFeedConfig().experimental_capabilities)
    feed_request.add_client_capability(capability);
  if (base::FeatureList::IsEnabled(kInterestFeedV2Hearts)) {
    feed_request.add_client_capability(feedwire::Capability::HEART);
  }
  if (base::FeatureList::IsEnabled(kInterestFeedV2Autoplay)) {
    feed_request.add_client_capability(
        feedwire::Capability::INLINE_VIDEO_AUTOPLAY);
  }

  *feed_request.mutable_client_info() = CreateClientInfo(request_metadata);
  feedwire::FeedQuery& query = *feed_request.mutable_feed_query();
  query.set_reason(request_reason);

  // |consistency_token|, for action reporting, is only applicable to signed-in
  // requests. The presence of |client_instance_id|, also signed-in only, can be
  // used a proxy for checking if we're creating a signed-in request.
  if (!consistency_token.empty() &&
      !request_metadata.client_instance_id.empty()) {
    feed_request.mutable_consistency_token()->set_token(consistency_token);
  }

  if (!next_page_token.empty()) {
    DCHECK_EQ(request_reason, feedwire::FeedQuery::NEXT_PAGE_SCROLL);
    query.mutable_next_page_token()
        ->mutable_next_page_token()
        ->set_next_page_token(next_page_token);
  }
  return request;
}

void SetNoticeCardAcknowledged(feedwire::Request* request,
                               const RequestMetadata& request_metadata) {
  if (request_metadata.notice_card_acknowledged) {
    request->mutable_feed_request()
        ->mutable_feed_query()
        ->mutable_chrome_fulfillment_info()
        ->set_notice_card_acknowledged(true);
  }
}

}  // namespace

std::string ContentIdString(const feedwire::ContentId& content_id) {
  return base::StrCat({content_id.content_domain(), ",",
                       base::NumberToString(content_id.type()), ",",
                       base::NumberToString(content_id.id())});
}

bool Equal(const feedwire::ContentId& a, const feedwire::ContentId& b) {
  return a.content_domain() == b.content_domain() && a.id() == b.id() &&
         a.type() == b.type();
}

bool CompareContentId(const feedwire::ContentId& a,
                      const feedwire::ContentId& b) {
  // Local variables because tie() needs l-values.
  const int a_id = a.id();
  const int b_id = b.id();
  const int a_type = a.type();
  const int b_type = b.type();
  return std::tie(a.content_domain(), a_id, a_type) <
         std::tie(b.content_domain(), b_id, b_type);
}

bool CompareContent(const feedstore::Content& a, const feedstore::Content& b) {
  const ContentId& a_id = a.content_id();
  const ContentId& b_id = b.content_id();
  if (a_id.id() < b_id.id())
    return true;
  if (a_id.id() > b_id.id())
    return false;
  if (a_id.type() < b_id.type())
    return true;
  if (a_id.type() > b_id.type())
    return false;
  return a.frame() < b.frame();
}

feedwire::ClientInfo CreateClientInfo(const RequestMetadata& request_metadata) {
  feedwire::ClientInfo client_info;

  feedwire::DisplayInfo& display_info = *client_info.add_display_info();
  display_info.set_screen_density(request_metadata.display_metrics.density);
  display_info.set_screen_width_in_pixels(
      request_metadata.display_metrics.width_pixels);
  display_info.set_screen_height_in_pixels(
      request_metadata.display_metrics.height_pixels);

  client_info.set_locale(request_metadata.language_tag);

#if defined(OS_ANDROID)
  client_info.set_platform_type(feedwire::ClientInfo::ANDROID_ID);
#elif defined(OS_IOS)
  client_info.set_platform_type(feedwire::ClientInfo::IOS);
#endif
  client_info.set_app_type(feedwire::ClientInfo::CHROME_ANDROID);
  *client_info.mutable_platform_version() = GetPlatformVersionMessage();
  *client_info.mutable_app_version() =
      GetAppVersionMessage(request_metadata.chrome_info);

  // client_instance_id and session_id should not both be set at the same time.
  DCHECK(request_metadata.client_instance_id.empty() ||
         request_metadata.session_id.empty());

  // Populate client_instance_id, session_id, or neither.
  if (!request_metadata.client_instance_id.empty()) {
    client_info.set_client_instance_id(request_metadata.client_instance_id);
  } else if (!request_metadata.session_id.empty()) {
    client_info.mutable_chrome_client_info()->set_session_id(
        request_metadata.session_id);
  }

  return client_info;
}

feedwire::Request CreateFeedQueryRefreshRequest(
    feedwire::FeedQuery::RequestReason request_reason,
    const RequestMetadata& request_metadata,
    const std::string& consistency_token) {
  feedwire::Request request = CreateFeedQueryRequest(
      request_reason, request_metadata, consistency_token, std::string());
  SetNoticeCardAcknowledged(&request, request_metadata);
  return request;
}

feedwire::Request CreateFeedQueryLoadMoreRequest(
    const RequestMetadata& request_metadata,
    const std::string& consistency_token,
    const std::string& next_page_token) {
  return CreateFeedQueryRequest(feedwire::FeedQuery::NEXT_PAGE_SCROLL,
                                request_metadata, consistency_token,
                                next_page_token);
}

}  // namespace feed
