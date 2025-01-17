// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/performance_manager/graph/frame_node_impl.h"

#include <utility>

#include "base/bind.h"
#include "components/performance_manager/graph/graph_impl.h"
#include "components/performance_manager/graph/graph_impl_util.h"
#include "components/performance_manager/graph/page_node_impl.h"
#include "components/performance_manager/graph/process_node_impl.h"
#include "components/performance_manager/graph/worker_node_impl.h"
#include "components/performance_manager/public/v8_memory/web_memory.h"

namespace performance_manager {

// static
constexpr char FrameNodeImpl::kDefaultPriorityReason[] =
    "default frame priority";

using PriorityAndReason = execution_context_priority::PriorityAndReason;

FrameNodeImpl::FrameNodeImpl(ProcessNodeImpl* process_node,
                             PageNodeImpl* page_node,
                             FrameNodeImpl* parent_frame_node,
                             int frame_tree_node_id,
                             int render_frame_id,
                             const blink::LocalFrameToken& frame_token,
                             int32_t browsing_instance_id,
                             int32_t site_instance_id)
    : parent_frame_node_(parent_frame_node),
      page_node_(page_node),
      process_node_(process_node),
      frame_tree_node_id_(frame_tree_node_id),
      render_frame_id_(render_frame_id),
      frame_token_(frame_token),
      browsing_instance_id_(browsing_instance_id),
      site_instance_id_(site_instance_id),
      render_frame_host_proxy_(content::GlobalFrameRoutingId(
          process_node->render_process_host_proxy()
              .render_process_host_id()
              .value(),
          render_frame_id)) {
  weak_this_ = weak_factory_.GetWeakPtr();

  DCHECK(process_node);
  DCHECK(page_node);
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

FrameNodeImpl::~FrameNodeImpl() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(child_worker_nodes_.empty());
  DCHECK(opened_page_nodes_.empty());
  DCHECK(!execution_context_);
}

void FrameNodeImpl::Bind(
    mojo::PendingReceiver<mojom::DocumentCoordinationUnit> receiver) {
  // It is possible to receive a mojo::PendingReceiver<DocumentCoordinationUnit>
  // when |receiver_| is already bound in these cases:
  // - Navigation from the initial empty document to the first real document.
  // - Navigation rejected by RenderFrameHostImpl::ValidateDidCommitParams().
  // See discussion:
  // https://chromium-review.googlesource.com/c/chromium/src/+/1572459/6#message-bd31f3e73f96bd9f7721be81ba6ac0076d053147
  receiver_.reset();
  receiver_.Bind(std::move(receiver));
}

void FrameNodeImpl::SetNetworkAlmostIdle() {
  document_.network_almost_idle.SetAndMaybeNotify(this, true);
}

void FrameNodeImpl::SetLifecycleState(mojom::LifecycleState state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  lifecycle_state_.SetAndMaybeNotify(this, state);
}

void FrameNodeImpl::SetHasNonEmptyBeforeUnload(bool has_nonempty_beforeunload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  document_.has_nonempty_beforeunload = has_nonempty_beforeunload;
}

void FrameNodeImpl::SetIsAdFrame(bool is_ad_frame) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  is_ad_frame_.SetAndMaybeNotify(this, is_ad_frame);
}

void FrameNodeImpl::SetHadFormInteraction() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  document_.had_form_interaction.SetAndMaybeNotify(this, true);
}

void FrameNodeImpl::OnNonPersistentNotificationCreated() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto* observer : GetObservers())
    observer->OnNonPersistentNotificationCreated(this);
}

void FrameNodeImpl::OnFirstContentfulPaint(
    base::TimeDelta time_since_navigation_start) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto* observer : GetObservers())
    observer->OnFirstContentfulPaint(this, time_since_navigation_start);
}

const RenderFrameHostProxy& FrameNodeImpl::GetRenderFrameHostProxy() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return render_frame_host_proxy();
}

bool FrameNodeImpl::IsMainFrame() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return !parent_frame_node_;
}

FrameNodeImpl* FrameNodeImpl::parent_frame_node() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return parent_frame_node_;
}

PageNodeImpl* FrameNodeImpl::page_node() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return page_node_;
}

ProcessNodeImpl* FrameNodeImpl::process_node() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return process_node_;
}

int FrameNodeImpl::frame_tree_node_id() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return frame_tree_node_id_;
}

int FrameNodeImpl::render_frame_id() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return render_frame_id_;
}

const blink::LocalFrameToken& FrameNodeImpl::frame_token() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return frame_token_;
}

int32_t FrameNodeImpl::browsing_instance_id() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return browsing_instance_id_;
}

int32_t FrameNodeImpl::site_instance_id() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return site_instance_id_;
}

const RenderFrameHostProxy& FrameNodeImpl::render_frame_host_proxy() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return render_frame_host_proxy_;
}

const base::flat_set<FrameNodeImpl*>& FrameNodeImpl::child_frame_nodes() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return child_frame_nodes_;
}

const base::flat_set<PageNodeImpl*>& FrameNodeImpl::opened_page_nodes() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return opened_page_nodes_;
}

mojom::LifecycleState FrameNodeImpl::lifecycle_state() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return lifecycle_state_.value();
}

bool FrameNodeImpl::has_nonempty_beforeunload() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return document_.has_nonempty_beforeunload;
}

const GURL& FrameNodeImpl::url() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return document_.url.value();
}

bool FrameNodeImpl::is_current() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return is_current_.value();
}

bool FrameNodeImpl::network_almost_idle() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return document_.network_almost_idle.value();
}

bool FrameNodeImpl::is_ad_frame() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return is_ad_frame_.value();
}

bool FrameNodeImpl::is_holding_weblock() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return is_holding_weblock_.value();
}

bool FrameNodeImpl::is_holding_indexeddb_lock() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return is_holding_indexeddb_lock_.value();
}

const base::flat_set<WorkerNodeImpl*>& FrameNodeImpl::child_worker_nodes()
    const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return child_worker_nodes_;
}

const PriorityAndReason& FrameNodeImpl::priority_and_reason() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return priority_and_reason_.value();
}

bool FrameNodeImpl::had_form_interaction() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return document_.had_form_interaction.value();
}

bool FrameNodeImpl::is_audible() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return is_audible_.value();
}

const base::Optional<gfx::Rect>& FrameNodeImpl::viewport_intersection() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // The viewport intersection of the main frame is not tracked.
  DCHECK(!IsMainFrame());
  return viewport_intersection_.value();
}

FrameNode::Visibility FrameNodeImpl::visibility() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return visibility_.value();
}

void FrameNodeImpl::SetIsCurrent(bool is_current) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  is_current_.SetAndMaybeNotify(this, is_current);

#if DCHECK_IS_ON()
  // We maintain an invariant that of all sibling nodes with the same
  // |frame_tree_node_id|, at most one may be current.
  if (is_current) {
    const base::flat_set<FrameNodeImpl*>* siblings = nullptr;
    if (parent_frame_node_) {
      siblings = &parent_frame_node_->child_frame_nodes();
    } else {
      siblings = &page_node_->main_frame_nodes();
    }

    size_t current_siblings = 0;
    for (auto* frame : *siblings) {
      if (frame->frame_tree_node_id() == frame_tree_node_id_ &&
          frame->is_current()) {
        ++current_siblings;
      }
    }
    DCHECK_EQ(1u, current_siblings);
  }
#endif
}

void FrameNodeImpl::SetIsHoldingWebLock(bool is_holding_weblock) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_NE(is_holding_weblock, is_holding_weblock_.value());
  is_holding_weblock_.SetAndMaybeNotify(this, is_holding_weblock);
}

void FrameNodeImpl::SetIsHoldingIndexedDBLock(bool is_holding_indexeddb_lock) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_NE(is_holding_indexeddb_lock, is_holding_indexeddb_lock_.value());
  is_holding_indexeddb_lock_.SetAndMaybeNotify(this, is_holding_indexeddb_lock);
}

void FrameNodeImpl::SetIsAudible(bool is_audible) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_NE(is_audible, is_audible_.value());
  is_audible_.SetAndMaybeNotify(this, is_audible);
}

void FrameNodeImpl::SetViewportIntersection(
    const gfx::Rect& viewport_intersection) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // The viewport intersection of the main frame is not tracked.
  DCHECK(!IsMainFrame());
  viewport_intersection_.SetAndMaybeNotify(this, viewport_intersection);
}

void FrameNodeImpl::SetVisibility(Visibility visibility) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  visibility_.SetAndMaybeNotify(this, visibility);
}

void FrameNodeImpl::OnNavigationCommitted(const GURL& url, bool same_document) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  if (same_document) {
    document_.url.SetAndMaybeNotify(this, url);
    return;
  }

  // Close |receiver_| to ensure that messages queued by the previous document
  // before the navigation commit are dropped.
  //
  // Note: It is guaranteed that |receiver_| isn't yet bound to the new
  // document.
  //       This is important because it would be incorrect to close the new
  //       document's binding.
  //
  //       Renderer: blink::DocumentLoader::DidCommitNavigation
  //                   ... content::RenderFrameImpl::DidCommitProvisionalLoad
  //                     ... mojom::FrameHost::DidCommitProvisionalLoad
  //       Browser:  RenderFrameHostImpl::DidCommitNavigation
  //                   Bind the new document's interface provider [A]
  //                   PMTabHelper::DidFinishNavigation
  //                     (async) FrameNodeImpl::OnNavigationCommitted [B]
  //       Renderer: Request DocumentCoordinationUnit interface
  //       Browser:  PMTabHelper::OnInterfaceRequestFromFrame [C]
  //                   (async) FrameNodeImpl::Bind [D]
  //
  //       A happens before C, because no interface request can be processed
  //       before the interface provider is bound. A posts B to PM sequence and
  //       C posts D to PM sequence, therefore B happens before D.
  receiver_.reset();

  // Reset properties.
  document_.Reset(this, url);
}

void FrameNodeImpl::AddChildWorker(WorkerNodeImpl* worker_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  bool inserted = child_worker_nodes_.insert(worker_node).second;
  DCHECK(inserted);
}

void FrameNodeImpl::RemoveChildWorker(WorkerNodeImpl* worker_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  size_t removed = child_worker_nodes_.erase(worker_node);
  DCHECK_EQ(1u, removed);
}

void FrameNodeImpl::SetPriorityAndReason(
    const PriorityAndReason& priority_and_reason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  priority_and_reason_.SetAndMaybeNotify(this, priority_and_reason);
}

base::WeakPtr<FrameNodeImpl> FrameNodeImpl::GetWeakPtrOnUIThread() {
  // TODO(siggi): Validate the thread context here.
  return weak_this_;
}

base::WeakPtr<FrameNodeImpl> FrameNodeImpl::GetWeakPtr() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return weak_factory_.GetWeakPtr();
}

void FrameNodeImpl::AddOpenedPage(base::PassKey<PageNodeImpl>,
                                  PageNodeImpl* page_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(page_node);
  DCHECK_NE(page_node_, page_node);
  DCHECK(graph()->NodeInGraph(page_node));
  DCHECK_EQ(this, page_node->opener_frame_node());
  bool inserted = opened_page_nodes_.insert(page_node).second;
  DCHECK(inserted);
}

void FrameNodeImpl::RemoveOpenedPage(base::PassKey<PageNodeImpl>,
                                     PageNodeImpl* page_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(page_node);
  DCHECK_NE(page_node_, page_node);
  DCHECK(graph()->NodeInGraph(page_node));
  DCHECK_EQ(this, page_node->opener_frame_node());
  size_t removed = opened_page_nodes_.erase(page_node);
  DCHECK_EQ(1u, removed);
}

const FrameNode* FrameNodeImpl::GetParentFrameNode() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return parent_frame_node();
}

const PageNode* FrameNodeImpl::GetPageNode() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return page_node();
}

const ProcessNode* FrameNodeImpl::GetProcessNode() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return process_node();
}

int FrameNodeImpl::GetFrameTreeNodeId() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return frame_tree_node_id();
}

const blink::LocalFrameToken& FrameNodeImpl::GetFrameToken() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return frame_token();
}

int32_t FrameNodeImpl::GetBrowsingInstanceId() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return browsing_instance_id();
}

int32_t FrameNodeImpl::GetSiteInstanceId() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return site_instance_id();
}

bool FrameNodeImpl::VisitChildFrameNodes(
    const FrameNodeVisitor& visitor) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto* frame_impl : child_frame_nodes()) {
    const FrameNode* frame = frame_impl;
    if (!visitor.Run(frame))
      return false;
  }
  return true;
}

const base::flat_set<const FrameNode*> FrameNodeImpl::GetChildFrameNodes()
    const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  return UpcastNodeSet<FrameNode>(child_frame_nodes());
}

bool FrameNodeImpl::VisitOpenedPageNodes(const PageNodeVisitor& visitor) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto* page_impl : opened_page_nodes()) {
    const PageNode* page = page_impl;
    if (!visitor.Run(page))
      return false;
  }
  return true;
}

const base::flat_set<const PageNode*> FrameNodeImpl::GetOpenedPageNodes()
    const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return UpcastNodeSet<PageNode>(opened_page_nodes());
}

FrameNodeImpl::LifecycleState FrameNodeImpl::GetLifecycleState() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return lifecycle_state();
}

bool FrameNodeImpl::HasNonemptyBeforeUnload() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return has_nonempty_beforeunload();
}

const GURL& FrameNodeImpl::GetURL() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return url();
}

bool FrameNodeImpl::IsCurrent() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return is_current();
}

bool FrameNodeImpl::GetNetworkAlmostIdle() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return network_almost_idle();
}

bool FrameNodeImpl::IsAdFrame() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return is_ad_frame();
}

bool FrameNodeImpl::IsHoldingWebLock() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return is_holding_weblock();
}

bool FrameNodeImpl::IsHoldingIndexedDBLock() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return is_holding_indexeddb_lock();
}

const base::flat_set<const WorkerNode*> FrameNodeImpl::GetChildWorkerNodes()
    const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return UpcastNodeSet<WorkerNode>(child_worker_nodes());
}

bool FrameNodeImpl::VisitChildDedicatedWorkers(
    const WorkerNodeVisitor& visitor) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto* worker_node_impl : child_worker_nodes()) {
    const WorkerNode* node = worker_node_impl;
    if (node->GetWorkerType() == WorkerNode::WorkerType::kDedicated &&
        !visitor.Run(node))
      return false;
  }
  return true;
}

const PriorityAndReason& FrameNodeImpl::GetPriorityAndReason() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return priority_and_reason();
}

bool FrameNodeImpl::HadFormInteraction() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return had_form_interaction();
}

bool FrameNodeImpl::IsAudible() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return is_audible();
}

const base::Optional<gfx::Rect>& FrameNodeImpl::GetViewportIntersection()
    const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return viewport_intersection();
}

FrameNode::Visibility FrameNodeImpl::GetVisibility() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return visibility();
}

void FrameNodeImpl::AddChildFrame(FrameNodeImpl* child_frame_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(child_frame_node);
  DCHECK_EQ(this, child_frame_node->parent_frame_node());
  DCHECK_NE(this, child_frame_node);
  DCHECK(graph()->NodeInGraph(child_frame_node));
  DCHECK(!HasFrameNodeInAncestors(child_frame_node) &&
         !child_frame_node->HasFrameNodeInDescendants(this));

  bool inserted = child_frame_nodes_.insert(child_frame_node).second;
  DCHECK(inserted);
}

void FrameNodeImpl::RemoveChildFrame(FrameNodeImpl* child_frame_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(child_frame_node);
  DCHECK_EQ(this, child_frame_node->parent_frame_node());
  DCHECK_NE(this, child_frame_node);
  DCHECK(graph()->NodeInGraph(child_frame_node));

  size_t removed = child_frame_nodes_.erase(child_frame_node);
  DCHECK_EQ(1u, removed);
}

void FrameNodeImpl::OnJoiningGraph() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Enable querying this node using process and frame routing ids.
  graph()->RegisterFrameNodeForId(process_node_->GetRenderProcessId(),
                                  render_frame_id_, this);

  // Set the initial frame visibility. This is done on the graph because the
  // page node must be accessed. OnFrameNodeAdded() has not been called yet for
  // this frame, so it is important to avoid sending a notification for this
  // property change.
  visibility_.Set(this, GetInitialFrameVisibility());

  // Wire this up to the other nodes in the graph.
  if (parent_frame_node_)
    parent_frame_node_->AddChildFrame(this);
  page_node_->AddFrame(base::PassKey<FrameNodeImpl>(), this);
  process_node_->AddFrame(this);
}

void FrameNodeImpl::OnBeforeLeavingGraph() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  DCHECK(child_frame_nodes_.empty());

  // Sever opener relationships.
  SeverOpenedPagesAndMaybeReparent();

  // Leave the page.
  DCHECK(graph()->NodeInGraph(page_node_));
  page_node_->RemoveFrame(base::PassKey<FrameNodeImpl>(), this);

  // Leave the frame hierarchy.
  if (parent_frame_node_) {
    DCHECK(graph()->NodeInGraph(parent_frame_node_));
    parent_frame_node_->RemoveChildFrame(this);
  }

  // And leave the process.
  DCHECK(graph()->NodeInGraph(process_node_));
  process_node_->RemoveFrame(this);

  // Disable querying this node using process and frame routing ids.
  graph()->UnregisterFrameNodeForId(process_node_->GetRenderProcessId(),
                                    render_frame_id_, this);
}

void FrameNodeImpl::RemoveNodeAttachedData() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  execution_context_.reset();
}

void FrameNodeImpl::SeverOpenedPagesAndMaybeReparent() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Copy |opened_page_nodes_| as we'll be modifying it in this loop: when we
  // call PageNodeImpl::(Set|Clear)OpenerFrameNodeAndOpenedType() this will call
  // back into this frame node and call RemoveOpenedPage().
  base::flat_set<PageNodeImpl*> opened_nodes = opened_page_nodes_;
  for (auto* opened_node : opened_nodes) {
    auto opened_type = opened_node->opened_type();

    // Reparent opened pages to this frame's parent to maintain the relationship
    // between the frame trees for bookkeeping. For the relationship to be
    // finally severed one of the frame trees must completely disappear, or it
    // must be explicitly severed (this can happen with portals).
    if (parent_frame_node_) {
      opened_node->SetOpenerFrameNodeAndOpenedType(parent_frame_node_,
                                                   opened_type);
    } else {
      // There's no new parent, so simply clear the opener.
      opened_node->ClearOpenerFrameNodeAndOpenedType();
    }
  }

  // Expect each page node to have called RemoveOpenedPage(), and for this to
  // now be empty.
  DCHECK(opened_page_nodes_.empty());
}

FrameNodeImpl* FrameNodeImpl::GetFrameTreeRoot() const {
  FrameNodeImpl* root = const_cast<FrameNodeImpl*>(this);
  while (root->parent_frame_node())
    root = parent_frame_node();
  DCHECK_NE(nullptr, root);
  return root;
}

bool FrameNodeImpl::HasFrameNodeInAncestors(FrameNodeImpl* frame_node) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (parent_frame_node_ == frame_node ||
      (parent_frame_node_ &&
       parent_frame_node_->HasFrameNodeInAncestors(frame_node))) {
    return true;
  }
  return false;
}

bool FrameNodeImpl::HasFrameNodeInDescendants(FrameNodeImpl* frame_node) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (FrameNodeImpl* child : child_frame_nodes_) {
    if (child == frame_node || child->HasFrameNodeInDescendants(frame_node)) {
      return true;
    }
  }
  return false;
}

bool FrameNodeImpl::HasFrameNodeInTree(FrameNodeImpl* frame_node) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return GetFrameTreeRoot() == frame_node->GetFrameTreeRoot();
}

FrameNode::Visibility FrameNodeImpl::GetInitialFrameVisibility() const {
  DCHECK(!viewport_intersection_.value());

  // If the page hosting this frame is not visible, then the frame is also not
  // visible.
  if (!page_node()->is_visible())
    return FrameNode::Visibility::kNotVisible;

  // The visibility of the frame depends on the viewport intersection of said
  // frame. Since a main frame has no viewport intersection, it is always
  // visible in the page.
  if (IsMainFrame())
    return FrameNode::Visibility::kVisible;

  // Since the viewport intersection of a frame is not initially available, the
  // visibility of a child frame is initially unknown.
  return FrameNode::Visibility::kUnknown;
}

FrameNodeImpl::DocumentProperties::DocumentProperties() = default;
FrameNodeImpl::DocumentProperties::~DocumentProperties() = default;

void FrameNodeImpl::DocumentProperties::Reset(FrameNodeImpl* frame_node,
                                              const GURL& url_in) {
  url.SetAndMaybeNotify(frame_node, url_in);
  has_nonempty_beforeunload = false;
  // Network is busy on navigation.
  network_almost_idle.SetAndMaybeNotify(frame_node, false);
  had_form_interaction.SetAndMaybeNotify(frame_node, false);
}

void FrameNodeImpl::OnWebMemoryMeasurementRequested(
    mojom::WebMemoryMeasurement::Mode mode,
    OnWebMemoryMeasurementRequestedCallback callback) {
  v8_memory::WebMeasureMemory(
      this, mode, v8_memory::WebMeasureMemorySecurityChecker::Create(),
      std::move(callback), mojo::GetBadMessageCallback());
}

}  // namespace performance_manager
