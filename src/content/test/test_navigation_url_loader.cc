// Copyright (c) 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/test/test_navigation_url_loader.h"

#include <utility>

#include "content/browser/loader/navigation_early_hints_manager.h"
#include "content/browser/loader/navigation_url_loader_delegate.h"
#include "content/browser/navigation_subresource_loader_params.h"
#include "content/public/browser/global_request_id.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/ssl_status.h"
#include "content/public/browser/web_contents.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "net/url_request/redirect_info.h"
#include "services/network/public/mojom/url_loader_factory.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "third_party/blink/public/common/navigation/navigation_policy.h"

namespace content {

TestNavigationURLLoader::TestNavigationURLLoader(
    std::unique_ptr<NavigationRequestInfo> request_info,
    NavigationURLLoaderDelegate* delegate,
    NavigationURLLoader::LoaderType loader_type)
    : request_info_(std::move(request_info)),
      delegate_(delegate),
      redirect_count_(0),
      loader_type_(loader_type) {}

void TestNavigationURLLoader::FollowRedirect(
    const std::vector<std::string>& removed_headers,
    const net::HttpRequestHeaders& modified_headers,
    const net::HttpRequestHeaders& modified_cors_exempt_headers,
    blink::PreviewsState new_previews_state) {
  DCHECK_EQ(loader_type_, NavigationURLLoader::LoaderType::kRegular);
  redirect_count_++;
}

void TestNavigationURLLoader::SimulateServerRedirect(const GURL& redirect_url) {
  DCHECK_EQ(loader_type_, NavigationURLLoader::LoaderType::kRegular);
  net::RedirectInfo redirect_info;
  redirect_info.status_code = 302;
  redirect_info.new_method = "GET";
  redirect_info.new_url = redirect_url;
  redirect_info.new_site_for_cookies =
      net::SiteForCookies::FromUrl(redirect_url);
  auto response_head = network::mojom::URLResponseHead::New();
  CallOnRequestRedirected(redirect_info, std::move(response_head));
}

void TestNavigationURLLoader::SimulateError(int error_code) {
  DCHECK_EQ(loader_type_, NavigationURLLoader::LoaderType::kRegular);
  SimulateErrorWithStatus(network::URLLoaderCompletionStatus(error_code));
}

void TestNavigationURLLoader::SimulateErrorWithStatus(
    const network::URLLoaderCompletionStatus& status) {
  DCHECK_EQ(loader_type_, NavigationURLLoader::LoaderType::kRegular);
  delegate_->OnRequestFailed(status);
}

void TestNavigationURLLoader::SimulateEarlyHintsPreloadLinkHeaderReceived() {
  was_early_hints_preload_link_header_received_ = true;
}

void TestNavigationURLLoader::CallOnRequestRedirected(
    const net::RedirectInfo& redirect_info,
    network::mojom::URLResponseHeadPtr response_head) {
  DCHECK_EQ(loader_type_, NavigationURLLoader::LoaderType::kRegular);
  response_head->parsed_headers = network::mojom::ParsedHeaders::New();
  delegate_->OnRequestRedirected(
      redirect_info, request_info_->isolation_info.network_isolation_key(),
      std::move(response_head));
}

void TestNavigationURLLoader::CallOnResponseStarted(
    network::mojom::URLResponseHeadPtr response_head) {
  if (!response_head->parsed_headers)
    response_head->parsed_headers = network::mojom::ParsedHeaders::New();
  // Create a bidirectionnal communication pipe between a URLLoader and a
  // URLLoaderClient. It will be closed at the end of this function. The sole
  // purpose of this is not to violate some DCHECKs when the navigation commits.
  mojo::PendingRemote<network::mojom::URLLoaderClient> url_loader_client_remote;
  mojo::PendingRemote<network::mojom::URLLoader> url_loader_remote;
  ignore_result(url_loader_remote.InitWithNewPipeAndPassReceiver());
  auto url_loader_client_endpoints =
      network::mojom::URLLoaderClientEndpoints::New(
          std::move(url_loader_remote),
          url_loader_client_remote.InitWithNewPipeAndPassReceiver());

  NavigationURLLoaderDelegate::EarlyHints early_hints;
  early_hints.was_preload_link_header_received =
      was_early_hints_preload_link_header_received_;

  delegate_->OnResponseStarted(
      std::move(url_loader_client_endpoints), std::move(response_head),
      mojo::ScopedDataPipeConsumerHandle(),
      GlobalRequestID::MakeBrowserInitiated(), false,
      blink::NavigationDownloadPolicy(),
      request_info_->isolation_info.network_isolation_key(), base::nullopt,
      std::move(early_hints));
}

TestNavigationURLLoader::~TestNavigationURLLoader() {}

}  // namespace content
