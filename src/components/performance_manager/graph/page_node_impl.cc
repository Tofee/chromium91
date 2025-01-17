// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/performance_manager/graph/page_node_impl.h"

#include <memory>

#include "base/bind.h"
#include "base/check_op.h"
#include "base/stl_util.h"
#include "base/time/default_tick_clock.h"
#include "components/performance_manager/graph/frame_node_impl.h"
#include "components/performance_manager/graph/graph_impl.h"
#include "components/performance_manager/graph/graph_impl_operations.h"
#include "components/performance_manager/graph/process_node_impl.h"

namespace performance_manager {

PageNodeImpl::PageNodeImpl(const WebContentsProxy& contents_proxy,
                           const std::string& browser_context_id,
                           const GURL& visible_url,
                           bool is_visible,
                           bool is_audible,
                           base::TimeTicks visibility_change_time)
    : contents_proxy_(contents_proxy),
      visibility_change_time_(visibility_change_time),
      main_frame_url_(visible_url),
      browser_context_id_(browser_context_id),
      is_visible_(is_visible),
      is_audible_(is_audible) {
  weak_this_ = weak_factory_.GetWeakPtr();

  DETACH_FROM_SEQUENCE(sequence_checker_);
}

PageNodeImpl::~PageNodeImpl() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_EQ(nullptr, opener_frame_node_);
  DCHECK_EQ(OpenedType::kInvalid, opened_type_);
  DCHECK(!page_load_tracker_data_);
  DCHECK(!site_data_);
  DCHECK(!frozen_frame_data_);
  DCHECK(!page_aggregator_data_);
}

const WebContentsProxy& PageNodeImpl::contents_proxy() const {
  return contents_proxy_;
}

void PageNodeImpl::AddFrame(base::PassKey<FrameNodeImpl>,
                            FrameNodeImpl* frame_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(frame_node);
  DCHECK_EQ(this, frame_node->page_node());
  DCHECK(graph()->NodeInGraph(frame_node));

  ++frame_node_count_;
  if (frame_node->parent_frame_node() == nullptr)
    main_frame_nodes_.insert(frame_node);
}

void PageNodeImpl::RemoveFrame(base::PassKey<FrameNodeImpl>,
                               FrameNodeImpl* frame_node) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(frame_node);
  DCHECK_EQ(this, frame_node->page_node());
  DCHECK(graph()->NodeInGraph(frame_node));

  --frame_node_count_;
  if (frame_node->parent_frame_node() == nullptr) {
    size_t removed = main_frame_nodes_.erase(frame_node);
    DCHECK_EQ(1u, removed);
  }
}

void PageNodeImpl::SetLoadingState(LoadingState loading_state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  loading_state_.SetAndMaybeNotify(this, loading_state);
}

void PageNodeImpl::SetIsVisible(bool is_visible) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_visible_.SetAndMaybeNotify(this, is_visible)) {
    // The change time needs to be updated after observers are notified, as they
    // use this to determine time passed since the *previous* visibility state
    // change. They can infer the current state change time themselves via
    // NowTicks.
    visibility_change_time_ = base::TimeTicks::Now();
  }
}

void PageNodeImpl::SetIsAudible(bool is_audible) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  is_audible_.SetAndMaybeNotify(this, is_audible);
}

void PageNodeImpl::SetUkmSourceId(ukm::SourceId ukm_source_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ukm_source_id_.SetAndMaybeNotify(this, ukm_source_id);
}

void PageNodeImpl::OnFaviconUpdated() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto* observer : GetObservers())
    observer->OnFaviconUpdated(this);
}

void PageNodeImpl::OnTitleUpdated() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto* observer : GetObservers())
    observer->OnTitleUpdated(this);
}

void PageNodeImpl::OnMainFrameNavigationCommitted(
    bool same_document,
    base::TimeTicks navigation_committed_time,
    int64_t navigation_id,
    const GURL& url,
    const std::string& contents_mime_type) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // This should never be invoked with a null navigation, nor should it be
  // called twice for the same navigation.
  DCHECK_NE(0, navigation_id);
  DCHECK_NE(navigation_id_, navigation_id);
  navigation_committed_time_ = navigation_committed_time;
  navigation_id_ = navigation_id;
  contents_mime_type_ = contents_mime_type;
  main_frame_url_.SetAndMaybeNotify(this, url);

  // No mainframe document change notification on same-document navigations.
  if (same_document)
    return;

  for (auto* observer : GetObservers())
    observer->OnMainFrameDocumentChanged(this);
}

base::TimeDelta PageNodeImpl::TimeSinceLastNavigation() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (navigation_committed_time_.is_null())
    return base::TimeDelta();
  return base::TimeTicks::Now() - navigation_committed_time_;
}

base::TimeDelta PageNodeImpl::TimeSinceLastVisibilityChange() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return base::TimeTicks::Now() - visibility_change_time_;
}

FrameNodeImpl* PageNodeImpl::GetMainFrameNodeImpl() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (main_frame_nodes_.empty())
    return nullptr;

  // Return the current frame node if there is one. Iterating over this set is
  // fine because it is almost always of length 1 or 2.
  for (auto* frame : main_frame_nodes_) {
    if (frame->is_current())
      return frame;
  }

  // Otherwise, return any old main frame node.
  return *main_frame_nodes_.begin();
}

FrameNodeImpl* PageNodeImpl::opener_frame_node() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(opener_frame_node_ || opened_type_ == OpenedType::kInvalid);
  return opener_frame_node_;
}

PageNodeImpl::OpenedType PageNodeImpl::opened_type() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(opener_frame_node_ || opened_type_ == OpenedType::kInvalid);
  return opened_type_;
}

bool PageNodeImpl::is_visible() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return is_visible_.value();
}

bool PageNodeImpl::is_audible() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return is_audible_.value();
}

PageNode::LoadingState PageNodeImpl::loading_state() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return loading_state_.value();
}

ukm::SourceId PageNodeImpl::ukm_source_id() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return ukm_source_id_.value();
}

PageNodeImpl::LifecycleState PageNodeImpl::lifecycle_state() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return lifecycle_state_.value();
}

bool PageNodeImpl::is_holding_weblock() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return is_holding_weblock_.value();
}

bool PageNodeImpl::is_holding_indexeddb_lock() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return is_holding_indexeddb_lock_.value();
}

const base::flat_set<FrameNodeImpl*>& PageNodeImpl::main_frame_nodes() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return main_frame_nodes_;
}

base::TimeTicks PageNodeImpl::usage_estimate_time() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return usage_estimate_time_;
}

uint64_t PageNodeImpl::private_footprint_kb_estimate() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return private_footprint_kb_estimate_;
}

const std::string& PageNodeImpl::browser_context_id() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return browser_context_id_;
}

const GURL& PageNodeImpl::main_frame_url() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return main_frame_url_.value();
}

int64_t PageNodeImpl::navigation_id() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return navigation_id_;
}

const std::string& PageNodeImpl::contents_mime_type() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return contents_mime_type_;
}

bool PageNodeImpl::had_form_interaction() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return had_form_interaction_.value();
}

const base::Optional<freezing::FreezingVote>& PageNodeImpl::freezing_vote()
    const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return freezing_vote_.value();
}

void PageNodeImpl::SetOpenerFrameNodeAndOpenedType(FrameNodeImpl* opener,
                                                   OpenedType opened_type) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK(opener);
  DCHECK(graph()->NodeInGraph(opener));
  DCHECK_NE(this, opener->page_node());
  DCHECK_NE(OpenedType::kInvalid, opened_type);

  auto* previous_opener = opener_frame_node_;
  auto previous_type = opened_type_;

  if (previous_opener)
    previous_opener->RemoveOpenedPage(PassKey(), this);
  opener_frame_node_ = opener;
  opened_type_ = opened_type;
  opener->AddOpenedPage(PassKey(), this);

  for (auto* observer : GetObservers())
    observer->OnOpenerFrameNodeChanged(this, previous_opener, previous_type);
}

void PageNodeImpl::ClearOpenerFrameNodeAndOpenedType() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_NE(nullptr, opener_frame_node_);
  DCHECK_NE(OpenedType::kInvalid, opened_type_);

  auto* previous_opener = opener_frame_node_;
  auto previous_type = opened_type_;

  opener_frame_node_->RemoveOpenedPage(PassKey(), this);
  opener_frame_node_ = nullptr;
  opened_type_ = OpenedType::kInvalid;

  for (auto* observer : GetObservers())
    observer->OnOpenerFrameNodeChanged(this, previous_opener, previous_type);
}

void PageNodeImpl::set_usage_estimate_time(
    base::TimeTicks usage_estimate_time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  usage_estimate_time_ = usage_estimate_time;
}

void PageNodeImpl::set_private_footprint_kb_estimate(
    uint64_t private_footprint_kb_estimate) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  private_footprint_kb_estimate_ = private_footprint_kb_estimate;
}

void PageNodeImpl::set_has_nonempty_beforeunload(
    bool has_nonempty_beforeunload) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  has_nonempty_beforeunload_ = has_nonempty_beforeunload;
}

void PageNodeImpl::set_freezing_vote(
    base::Optional<freezing::FreezingVote> freezing_vote) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  freezing_vote_.SetAndMaybeNotify(this, freezing_vote);
}

void PageNodeImpl::OnJoiningGraph() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
#if DCHECK_IS_ON()
  // Dereferencing the WeakPtr associated with this node will bind it to the
  // current sequence (all subsequent calls to |GetWeakPtr| will return the
  // same WeakPtr).
  GetWeakPtr()->GetImpl();
#endif
}

void PageNodeImpl::OnBeforeLeavingGraph() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Sever opener relationships.
  if (opener_frame_node_)
    ClearOpenerFrameNodeAndOpenedType();

  DCHECK_EQ(0u, frame_node_count_);
}

void PageNodeImpl::RemoveNodeAttachedData() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  page_load_tracker_data_.reset();
  site_data_.reset();
  frozen_frame_data_.Reset();
  page_aggregator_data_.Reset();
}

const std::string& PageNodeImpl::GetBrowserContextID() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return browser_context_id();
}

const FrameNode* PageNodeImpl::GetOpenerFrameNode() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return opener_frame_node();
}

PageNodeImpl::OpenedType PageNodeImpl::GetOpenedType() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return opened_type();
}

bool PageNodeImpl::IsVisible() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return is_visible();
}

base::TimeDelta PageNodeImpl::GetTimeSinceLastVisibilityChange() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return TimeSinceLastVisibilityChange();
}

bool PageNodeImpl::IsAudible() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return is_audible();
}

PageNode::LoadingState PageNodeImpl::GetLoadingState() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return loading_state();
}

ukm::SourceId PageNodeImpl::GetUkmSourceID() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return ukm_source_id();
}

PageNodeImpl::LifecycleState PageNodeImpl::GetLifecycleState() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return lifecycle_state();
}

bool PageNodeImpl::IsHoldingWebLock() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return is_holding_weblock();
}

bool PageNodeImpl::IsHoldingIndexedDBLock() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return is_holding_indexeddb_lock();
}

int64_t PageNodeImpl::GetNavigationID() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return navigation_id();
}

const std::string& PageNodeImpl::GetContentsMimeType() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return contents_mime_type();
}

base::TimeDelta PageNodeImpl::GetTimeSinceLastNavigation() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return TimeSinceLastNavigation();
}

const FrameNode* PageNodeImpl::GetMainFrameNode() const {
  return GetMainFrameNodeImpl();
}

bool PageNodeImpl::VisitMainFrameNodes(const FrameNodeVisitor& visitor) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  for (auto* frame_impl : main_frame_nodes_) {
    const FrameNode* frame = frame_impl;
    if (!visitor.Run(frame))
      return false;
  }
  return true;
}

const base::flat_set<const FrameNode*> PageNodeImpl::GetMainFrameNodes() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::flat_set<const FrameNode*> main_frame_nodes(main_frame_nodes_.begin(),
                                                    main_frame_nodes_.end());
  return main_frame_nodes;
}

const GURL& PageNodeImpl::GetMainFrameUrl() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return main_frame_url();
}

bool PageNodeImpl::HadFormInteraction() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return had_form_interaction();
}

const WebContentsProxy& PageNodeImpl::GetContentsProxy() const {
  return contents_proxy();
}

const base::Optional<freezing::FreezingVote>& PageNodeImpl::GetFreezingVote()
    const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return freezing_vote();
}

void PageNodeImpl::SetLifecycleState(LifecycleState lifecycle_state) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  lifecycle_state_.SetAndMaybeNotify(this, lifecycle_state);
}

void PageNodeImpl::SetIsHoldingWebLock(bool is_holding_weblock) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  is_holding_weblock_.SetAndMaybeNotify(this, is_holding_weblock);
}

void PageNodeImpl::SetIsHoldingIndexedDBLock(bool is_holding_indexeddb_lock) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  is_holding_indexeddb_lock_.SetAndMaybeNotify(this, is_holding_indexeddb_lock);
}

void PageNodeImpl::SetHadFormInteraction(bool had_form_interaction) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  had_form_interaction_.SetAndMaybeNotify(this, had_form_interaction);
}

}  // namespace performance_manager
