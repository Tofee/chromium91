// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/renderer_host/policy_container_navigation_bundle.h"

#include "content/browser/renderer_host/frame_tree_node.h"
#include "content/browser/renderer_host/navigation_entry_impl.h"
#include "content/browser/renderer_host/render_frame_host_impl.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/content_browser_test.h"
#include "content/shell/browser/shell.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/url_constants.h"

namespace content {
namespace {

using ::testing::ByRef;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::Pointee;

// Extracted from |GetPolicies()| because ASSERT_* macros can only be used in
// functions that return void.
void AssertHasPolicyContainerHost(RenderFrameHostImpl* frame) {
  ASSERT_TRUE(frame->policy_container_host());
}

const PolicyContainerPolicies& GetPolicies(RenderFrameHostImpl* frame) {
  AssertHasPolicyContainerHost(frame);
  return frame->policy_container_host()->policies();
}

GURL AboutBlankUrl() {
  return GURL(url::kAboutBlankURL);
}

GURL AboutSrcdocUrl() {
  return GURL(url::kAboutSrcdocURL);
}

// See also the unit tests for PolicyContainerNavigationBundle, which exercise
// simpler parts of the API. We use browser tests to exercise behavior in the
// presence of navigation history in particular.
class PolicyContainerNavigationBundleBrowserTest : public ContentBrowserTest {
 protected:
  explicit PolicyContainerNavigationBundleBrowserTest() { StartServer(); }

  // Returns a pointer to the current root RenderFrameHostImpl.
  RenderFrameHostImpl* root_frame_host() {
    return static_cast<RenderFrameHostImpl*>(
        shell()->web_contents()->GetMainFrame());
  }

  // Returns the URL of a page in the local address space.
  GURL LocalUrl() const { return embedded_test_server()->GetURL("/echo"); }

  // Returns the URL of a page in the public address space.
  GURL PublicUrl() const {
    return embedded_test_server()->GetURL(
        "/set-header?Content-Security-Policy: treat-as-public-address");
  }

  // Returns the FrameNavigationEntry for the root node in the last committed
  // navigation entry.
  // Returns nullptr if there is no committed navigation entry.
  FrameNavigationEntry* GetLastCommittedFrameNavigationEntry() {
    auto* entry = static_cast<NavigationEntryImpl*>(
        shell()->web_contents()->GetController().GetLastCommittedEntry());
    if (!entry) {
      return nullptr;
    }

    return entry->root_node()->frame_entry.get();
  }

 private:
  // Constructor helper. We cannot use ASSERT_* macros in constructors.
  void StartServer() { ASSERT_TRUE(embedded_test_server()->Start()); }
};

// Verifies that HistoryPolicies() returns nullptr in the absence of a history
// entry.
//
// Even though this could be a unit test, we define this here so as to keep all
// tests of HistoryPolicies() in the same place.
IN_PROC_BROWSER_TEST_F(PolicyContainerNavigationBundleBrowserTest,
                       HistoryPoliciesWithoutEntry) {
  EXPECT_THAT(PolicyContainerNavigationBundle(nullptr, nullptr, nullptr)
                  .HistoryPolicies(),
              IsNull());
}

// Verifies that HistoryPolicies() returns nullptr if the history entry given to
// the bundle contains no policies.
IN_PROC_BROWSER_TEST_F(PolicyContainerNavigationBundleBrowserTest,
                       HistoryPoliciesWithoutEntryPolicies) {
  // Navigate to a document with a network scheme. Its history entry will have
  // nullptr policies, since those should always be reconstructed from the
  // network response.
  EXPECT_TRUE(NavigateToURL(shell()->web_contents(), LocalUrl()));

  PolicyContainerNavigationBundle bundle(
      nullptr, nullptr, GetLastCommittedFrameNavigationEntry());

  EXPECT_THAT(bundle.HistoryPolicies(), IsNull());
}

// Verifies that SetFrameNavigationEntry() copies the policies of the given
// entry, if any, or resets those policies when given nullptr.
IN_PROC_BROWSER_TEST_F(PolicyContainerNavigationBundleBrowserTest,
                       HistoryPoliciesWithEntry) {
  RenderFrameHostImpl* root = root_frame_host();

  // First navigate to a local scheme with non-default policies. To do that, we
  // first navigate to a document with a public address space, then have that
  // document navigate itself to `about:blank`. The final blank document
  // inherits its policies from the first document, and stores them in its
  // frame navigation entry for restoring later.
  EXPECT_TRUE(NavigateToURL(shell()->web_contents(), PublicUrl()));
  EXPECT_TRUE(NavigateToURLFromRenderer(root, AboutBlankUrl()));

  const PolicyContainerPolicies& root_policies = GetPolicies(root);
  EXPECT_EQ(root_policies.ip_address_space,
            network::mojom::IPAddressSpace::kPublic);

  // Now that we have set up a navigation entry with non-default policies, we
  // can run the test itself.
  PolicyContainerNavigationBundle bundle(
      nullptr, nullptr, GetLastCommittedFrameNavigationEntry());

  EXPECT_THAT(bundle.HistoryPolicies(), Pointee(Eq(ByRef(root_policies))));
}

// Verifies that CreatePolicyContainerForBlink() returns a policy container
// containing a copy of the bundle's final policies.
IN_PROC_BROWSER_TEST_F(PolicyContainerNavigationBundleBrowserTest,
                       CreatePolicyContainerForBlink) {
  PolicyContainerNavigationBundle bundle(nullptr, nullptr, nullptr);
  bundle.SetIPAddressSpace(network::mojom::IPAddressSpace::kPublic);

  bundle.ComputePolicies(GURL());

  // This must be called on a task runner, hence the need for this test to be
  // a browser test and not a simple unit test.
  blink::mojom::PolicyContainerPtr container =
      bundle.CreatePolicyContainerForBlink();
  ASSERT_FALSE(container.is_null());
  ASSERT_FALSE(container->policies.is_null());

  const blink::mojom::PolicyContainerPolicies& policies = *container->policies;
  EXPECT_EQ(policies.referrer_policy, bundle.FinalPolicies().referrer_policy);
  EXPECT_EQ(policies.ip_address_space, bundle.FinalPolicies().ip_address_space);
}

// Verifies that when the URL of the document to commit is `about:blank`, and
// when a navigation entry with policies is given, then the navigation
// initiator's policies are ignored in favor of the policies from the entry.
IN_PROC_BROWSER_TEST_F(PolicyContainerNavigationBundleBrowserTest,
                       FinalPoliciesAboutBlankWithInitiatorAndHistory) {
  RenderFrameHostImpl* root = root_frame_host();

  // First navigate to a local scheme with non-default policies. To do that, we
  // first navigate to a document with a public address space, then have that
  // document navigate itself to `about:blank`. The final blank document
  // inherits its policies from the first document, and stores them in its frame
  // navigation entry for restoring later.
  EXPECT_TRUE(NavigateToURL(shell()->web_contents(), PublicUrl()));
  EXPECT_TRUE(NavigateToURLFromRenderer(root, AboutBlankUrl()));

  auto initiator_policies = std::make_unique<PolicyContainerPolicies>();
  initiator_policies->ip_address_space = network::mojom::IPAddressSpace::kLocal;

  blink::LocalFrameToken token;
  auto initiator_host =
      base::MakeRefCounted<PolicyContainerHost>(std::move(initiator_policies));
  initiator_host->AssociateWithFrameToken(token);

  PolicyContainerNavigationBundle bundle(
      nullptr, &token, GetLastCommittedFrameNavigationEntry());

  EXPECT_NE(*bundle.HistoryPolicies(), *bundle.InitiatorPolicies());

  std::unique_ptr<PolicyContainerPolicies> history_policies =
      bundle.HistoryPolicies()->Clone();
  bundle.ComputePolicies(AboutBlankUrl());

  EXPECT_EQ(bundle.FinalPolicies(), *history_policies);
}

// Verifies that when the URL of the document to commit is `about:srcdoc`, and
// when a navigation entry with policies is given, then the parent's policies
// are ignored in favor of the policies from the entry.
IN_PROC_BROWSER_TEST_F(PolicyContainerNavigationBundleBrowserTest,
                       FinalPoliciesAboutSrcDocWithParentAndHistory) {
  RenderFrameHostImpl* root = root_frame_host();

  // First navigate to a local scheme with non-default policies. To do that, we
  // first navigate to a document with a public address space, then have that
  // document navigate itself to `about:blank`. The final blank document
  // inherits its policies from the first document, and stores them in its
  // frame navigation entry for restoring later.
  EXPECT_TRUE(NavigateToURL(shell()->web_contents(), PublicUrl()));
  EXPECT_TRUE(NavigateToURLFromRenderer(root, AboutBlankUrl()));

  // Embed another frame with different policies, to use as the "parent".
  std::string script_template = R"(
    new Promise((resolve) => {
      const iframe = document.createElement("iframe");
      iframe.src = $1;
      iframe.onload = () => { resolve(true); }
      document.body.appendChild(iframe);
    })
  )";
  EXPECT_EQ(true, EvalJs(root, JsReplace(script_template, LocalUrl())));

  RenderFrameHostImpl* parent = root->child_at(0)->current_frame_host();
  PolicyContainerNavigationBundle bundle(
      parent, nullptr, GetLastCommittedFrameNavigationEntry());

  EXPECT_NE(*bundle.HistoryPolicies(), *bundle.ParentPolicies());

  std::unique_ptr<PolicyContainerPolicies> history_policies =
      bundle.HistoryPolicies()->Clone();
  bundle.ComputePolicies(AboutSrcdocUrl());

  EXPECT_EQ(bundle.FinalPolicies(), *history_policies);
}

// Verifies that history policies are ignored in the case of error pages.
IN_PROC_BROWSER_TEST_F(PolicyContainerNavigationBundleBrowserTest,
                       FinalPoliciesErrorPageWithHistory) {
  // First navigate to a local scheme with non-default policies. To do that, we
  // first navigate to a document with a public address space, then have that
  // document navigate itself to `about:blank`. The final blank document
  // inherits its policies from the first document, and stores them in its
  // frame navigation entry for restoring later.
  EXPECT_TRUE(NavigateToURL(shell()->web_contents(), PublicUrl()));
  EXPECT_TRUE(NavigateToURLFromRenderer(root_frame_host(), AboutBlankUrl()));

  PolicyContainerNavigationBundle bundle(
      nullptr, nullptr, GetLastCommittedFrameNavigationEntry());

  bundle.ComputePoliciesForError();

  // Error pages commit with default policies, ignoring the history policies.
  EXPECT_EQ(bundle.FinalPolicies(), PolicyContainerPolicies());
}

// After |ComputePolicies()| or |ComputePoliciesForError()|, the history
// policies are still accessible.
IN_PROC_BROWSER_TEST_F(PolicyContainerNavigationBundleBrowserTest,
                       AccessHistoryAfterComputingPolicies) {
  // First navigate to a local scheme with non-default policies. To do that, we
  // first navigate to a document with a public address space, then have that
  // document navigate itself to `about:blank`. The final blank document
  // inherits its policies from the first document, and stores them in its
  // frame navigation entry for restoring later.
  EXPECT_TRUE(NavigateToURL(shell()->web_contents(), PublicUrl()));
  EXPECT_TRUE(NavigateToURLFromRenderer(root_frame_host(), AboutBlankUrl()));

  PolicyContainerNavigationBundle bundle(
      nullptr, nullptr, GetLastCommittedFrameNavigationEntry());

  std::unique_ptr<PolicyContainerPolicies> history_policies =
      bundle.HistoryPolicies()->Clone();

  bundle.ComputePolicies(AboutBlankUrl());
  EXPECT_THAT(bundle.HistoryPolicies(), Pointee(Eq(ByRef(*history_policies))));

  bundle.ComputePoliciesForError();
  EXPECT_THAT(bundle.HistoryPolicies(), Pointee(Eq(ByRef(*history_policies))));
}

}  // namespace
}  // namespace content
