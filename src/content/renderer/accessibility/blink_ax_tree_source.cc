// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/accessibility/blink_ax_tree_source.h"

#include <stddef.h>

#include <algorithm>
#include <set>

#include "base/command_line.h"
#include "base/memory/ptr_util.h"
#include "base/stl_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "build/chromeos_buildflags.h"
#include "content/common/ax_serialization_utils.h"
#include "content/public/common/content_features.h"
#include "content/renderer/accessibility/ax_image_annotator.h"
#include "content/renderer/accessibility/render_accessibility_impl.h"
#include "content/renderer/render_frame_impl.h"
#include "content/renderer/render_frame_proxy.h"
#include "content/renderer/render_view_impl.h"
#include "third_party/blink/public/platform/web_string.h"
#include "third_party/blink/public/platform/web_vector.h"
#include "third_party/blink/public/web/web_ax_enums.h"
#include "third_party/blink/public/web/web_ax_object.h"
#include "third_party/blink/public/web/web_disallow_transition_scope.h"
#include "third_party/blink/public/web/web_document.h"
#include "third_party/blink/public/web/web_element.h"
#include "third_party/blink/public/web/web_form_control_element.h"
#include "third_party/blink/public/web/web_frame.h"
#include "third_party/blink/public/web/web_local_frame.h"
#include "third_party/blink/public/web/web_node.h"
#include "third_party/blink/public/web/web_plugin.h"
#include "third_party/blink/public/web/web_plugin_container.h"
#include "third_party/blink/public/web/web_view.h"
#include "ui/accessibility/accessibility_features.h"
#include "ui/accessibility/accessibility_switches.h"
#include "ui/accessibility/ax_common.h"
#include "ui/accessibility/ax_enum_util.h"
#include "ui/accessibility/ax_role_properties.h"
#include "ui/accessibility/ax_tree_id.h"
#include "ui/gfx/geometry/vector2d_f.h"
#include "url/gurl.h"
#include "url/url_constants.h"

using base::ASCIIToUTF16;
using base::UTF16ToUTF8;
using blink::WebAXObject;
using blink::WebDocument;
using blink::WebElement;
using blink::WebFrame;
using blink::WebLocalFrame;
using blink::WebNode;
using blink::WebPlugin;
using blink::WebPluginContainer;
using blink::WebVector;
using blink::WebView;

namespace content {

namespace {

// Images smaller than this number, in CSS pixels, will never get annotated.
// Note that OCR works on pretty small images, so this shouldn't be too large.
const int kMinImageAnnotationWidth = 16;
const int kMinImageAnnotationHeight = 16;

void AddIntListAttributeFromWebObjects(ax::mojom::IntListAttribute attr,
                                       const WebVector<WebAXObject>& objects,
                                       ui::AXNodeData* dst) {
  std::vector<int32_t> ids;
  for (size_t i = 0; i < objects.size(); i++)
    ids.push_back(objects[i].AxID());
  if (!ids.empty())
    dst->AddIntListAttribute(attr, ids);
}

#if DCHECK_IS_ON()
WebAXObject ParentObjectUnignored(WebAXObject child) {
  WebAXObject parent = child.ParentObject();
  while (!parent.IsDetached() && !parent.AccessibilityIsIncludedInTree())
    parent = parent.ParentObject();
  return parent;
}

// Check that |parent| is the first unignored parent of |child|.
void CheckParentUnignoredOf(WebAXObject parent, WebAXObject child) {
  WebAXObject preexisting_parent = ParentObjectUnignored(child);
  DCHECK(preexisting_parent.Equals(parent))
      << "Child thinks it has a different preexisting parent:"
      << "\nChild: " << child.ToString(true).Utf8()
      << "\nPassed-in parent: " << parent.ToString(true).Utf8()
      << "\nPreexisting parent: " << preexisting_parent.ToString(true).Utf8();
}
#endif

// Helper function that searches in the subtree of |obj| to a max
// depth of |max_depth| for an image.
//
// Returns true on success, or false if it finds more than one image,
// or any node with a name, or anything deeper than |max_depth|.
bool SearchForExactlyOneInnerImage(WebAXObject obj,
                                   WebAXObject* inner_image,
                                   int max_depth) {
  DCHECK(inner_image);

  // If it's the first image, set |inner_image|. If we already
  // found an image, fail.
  if (ui::IsImage(obj.Role())) {
    if (!inner_image->IsDetached())
      return false;
    *inner_image = obj;
  } else {
    // If we found something else with a name, fail.
    if (!ui::IsPlatformDocument(obj.Role()) && !ui::IsLink(obj.Role())) {
      blink::WebString web_name = obj.GetName();
      if (!base::ContainsOnlyChars(web_name.Utf8(), base::kWhitespaceASCII)) {
        return false;
      }
    }
  }

  // Fail if we recursed to |max_depth| and there's more of a subtree.
  if (max_depth == 0 && obj.ChildCount())
    return false;

  // Don't count ignored nodes toward depth.
  int next_depth = obj.AccessibilityIsIgnored() ? max_depth : max_depth - 1;

  // Recurse.
  for (unsigned int i = 0; i < obj.ChildCount(); i++) {
    if (!SearchForExactlyOneInnerImage(obj.ChildAt(i), inner_image, next_depth))
      return false;
  }

  return !inner_image->IsDetached();
}

// Return true if the subtree of |obj|, to a max depth of 3, contains
// exactly one image. Return that image in |inner_image|.
bool FindExactlyOneInnerImageInMaxDepthThree(WebAXObject obj,
                                             WebAXObject* inner_image) {
  DCHECK(inner_image);
  return SearchForExactlyOneInnerImage(obj, inner_image, /* max_depth = */ 3);
}

}  // namespace

ScopedFreezeBlinkAXTreeSource::ScopedFreezeBlinkAXTreeSource(
    BlinkAXTreeSource* tree_source)
    : tree_source_(tree_source) {
  tree_source_->Freeze();
}

ScopedFreezeBlinkAXTreeSource::~ScopedFreezeBlinkAXTreeSource() {
  tree_source_->Thaw();
}

BlinkAXTreeSource::BlinkAXTreeSource(RenderFrameImpl* render_frame,
                                     ui::AXMode mode)
    : render_frame_(render_frame), accessibility_mode_(mode), frozen_(false) {
  image_annotation_debugging_ =
      base::CommandLine::ForCurrentProcess()->HasSwitch(
          ::switches::kEnableExperimentalAccessibilityLabelsDebugging);
}

BlinkAXTreeSource::~BlinkAXTreeSource() {}

void BlinkAXTreeSource::Freeze() {
  CHECK(!frozen_);
  frozen_ = true;

  if (render_frame_ && render_frame_->GetWebFrame())
    document_ = render_frame_->GetWebFrame()->GetDocument();
  else
    document_ = WebDocument();

  root_ = ComputeRoot();

  if (!document_.IsNull())
    focus_ = WebAXObject::FromWebDocumentFocused(document_);
  else
    focus_ = WebAXObject();

  WebAXObject::Freeze(document_);
}

void BlinkAXTreeSource::Thaw() {
  CHECK(frozen_);
  WebAXObject::Thaw(document_);
  frozen_ = false;
}

void BlinkAXTreeSource::SetRoot(WebAXObject root) {
  CHECK(!frozen_);
  explicit_root_ = root;
}

#if defined(AX_FAIL_FAST_BUILD)
// TODO(accessibility) Remove once it's clear this never triggers.
bool BlinkAXTreeSource::IsInTree(WebAXObject node) const {
  CHECK(frozen_);
  while (IsValid(node)) {
    if (node.Equals(root()))
      return true;
    node = GetParent(node);
  }
  return false;
}
#endif

void BlinkAXTreeSource::SetAccessibilityMode(ui::AXMode new_mode) {
  if (accessibility_mode_ == new_mode)
    return;
  accessibility_mode_ = new_mode;
}

bool BlinkAXTreeSource::ShouldLoadInlineTextBoxes(
    const blink::WebAXObject& obj) const {
#if !defined(OS_ANDROID)
  // If inline text boxes are enabled globally, no need to explicitly load them.
  if (accessibility_mode_.has_mode(ui::AXMode::kInlineTextBoxes))
    return false;
#endif

  // On some platforms, like Android, we only load inline text boxes for
  // a subset of nodes:
  //
  // Within the subtree of a focused editable text area.
  // When specifically enabled for a subtree via |load_inline_text_boxes_ids_|.

  int32_t focus_id = focus().AxID();
  WebAXObject ancestor = obj;
  while (!ancestor.IsDetached()) {
    int32_t ancestor_id = ancestor.AxID();
    if (base::Contains(load_inline_text_boxes_ids_, ancestor_id) ||
        (ancestor_id == focus_id && ancestor.IsEditable())) {
      return true;
    }
    ancestor = ancestor.ParentObject();
  }

  return false;
}

void BlinkAXTreeSource::SetLoadInlineTextBoxesForId(int32_t id) {
  // Keeping stale IDs in the set is harmless but we don't want it to keep
  // growing without bound, so clear out any unnecessary IDs whenever this
  // method is called.
  for (auto iter = load_inline_text_boxes_ids_.begin();
       iter != load_inline_text_boxes_ids_.end();) {
    if (GetFromId(*iter).IsDetached())
      iter = load_inline_text_boxes_ids_.erase(iter);
    else
      ++iter;
  }

  load_inline_text_boxes_ids_.insert(id);
}

void BlinkAXTreeSource::PopulateAXRelativeBounds(WebAXObject obj,
                                                 ui::AXRelativeBounds* bounds,
                                                 bool* clips_children) const {
  WebAXObject offset_container;
  gfx::RectF bounds_in_container;
  SkMatrix44 web_container_transform;
  obj.GetRelativeBounds(offset_container, bounds_in_container,
                        web_container_transform, clips_children);
  bounds->bounds = bounds_in_container;
  if (!offset_container.IsDetached())
    bounds->offset_container_id = offset_container.AxID();

  if (content::AXShouldIncludePageScaleFactorInRoot() && obj.Equals(root())) {
    const WebView* web_view = render_frame_->GetRenderView()->GetWebView();
    std::unique_ptr<gfx::Transform> container_transform =
        std::make_unique<gfx::Transform>(web_container_transform);
    container_transform->Scale(web_view->PageScaleFactor(),
                               web_view->PageScaleFactor());
    container_transform->Translate(
        -web_view->VisualViewportOffset().OffsetFromOrigin());
    if (!container_transform->IsIdentity())
      bounds->transform = std::move(container_transform);
  } else if (!web_container_transform.isIdentity()) {
    bounds->transform =
        base::WrapUnique(new gfx::Transform(web_container_transform));
  }
}

bool BlinkAXTreeSource::HasCachedBoundingBox(int32_t id) const {
  return base::Contains(cached_bounding_boxes_, id);
}

const ui::AXRelativeBounds& BlinkAXTreeSource::GetCachedBoundingBox(
    int32_t id) const {
  auto iter = cached_bounding_boxes_.find(id);
  DCHECK(iter != cached_bounding_boxes_.end());
  return iter->second;
}

void BlinkAXTreeSource::SetCachedBoundingBox(
    int32_t id,
    const ui::AXRelativeBounds& bounds) {
  cached_bounding_boxes_[id] = bounds;
}

size_t BlinkAXTreeSource::GetCachedBoundingBoxCount() const {
  return cached_bounding_boxes_.size();
}

bool BlinkAXTreeSource::GetTreeData(ui::AXTreeData* tree_data) const {
  CHECK(frozen_);
  tree_data->doctype = "html";
  tree_data->loaded = root().IsLoaded();
  tree_data->loading_progress = root().EstimatedLoadingProgress();
  tree_data->mimetype =
      document().IsXHTMLDocument() ? "text/xhtml" : "text/html";
  tree_data->title = document().Title().Utf8();
  tree_data->url = document().Url().GetString().Utf8();

  if (!focus().IsNull())
    tree_data->focus_id = focus().AxID();

  bool is_selection_backward = false;
  WebAXObject anchor_object, focus_object;
  int anchor_offset, focus_offset;
  ax::mojom::TextAffinity anchor_affinity, focus_affinity;
  root().Selection(is_selection_backward, anchor_object, anchor_offset,
                   anchor_affinity, focus_object, focus_offset, focus_affinity);
  if (!anchor_object.IsNull() && !focus_object.IsNull() && anchor_offset >= 0 &&
      focus_offset >= 0) {
    int32_t anchor_id = anchor_object.AxID();
    int32_t focus_id = focus_object.AxID();
    tree_data->sel_is_backward = is_selection_backward;
    tree_data->sel_anchor_object_id = anchor_id;
    tree_data->sel_anchor_offset = anchor_offset;
    tree_data->sel_focus_object_id = focus_id;
    tree_data->sel_focus_offset = focus_offset;
    tree_data->sel_anchor_affinity = anchor_affinity;
    tree_data->sel_focus_affinity = focus_affinity;
  }

  // Get the tree ID for this frame.
  if (WebLocalFrame* web_frame = document().GetFrame())
    tree_data->tree_id = web_frame->GetAXTreeID();

  tree_data->root_scroller_id = root().RootScroller().AxID();

  return true;
}

WebAXObject BlinkAXTreeSource::GetRoot() const {
  if (frozen_)
    return root_;
  else
    return ComputeRoot();
}

WebAXObject BlinkAXTreeSource::GetFromId(int32_t id) const {
  return WebAXObject::FromWebDocumentByID(GetMainDocument(), id);
}

int32_t BlinkAXTreeSource::GetId(WebAXObject node) const {
  return node.AxID();
}

void BlinkAXTreeSource::GetChildren(
    WebAXObject parent,
    std::vector<WebAXObject>* out_children) const {
  CHECK(frozen_);

  if (ui::CanHaveInlineTextBoxChildren(parent.Role()) &&
      ShouldLoadInlineTextBoxes(parent)) {
    parent.LoadInlineTextBoxes();
  }

  bool is_iframe = false;
  WebNode node = parent.GetNode();
  if (!node.IsNull() && node.IsElementNode())
    is_iframe = node.To<WebElement>().HasHTMLTagName("iframe");

  for (unsigned i = 0; i < parent.ChildCount(); i++) {
    WebAXObject child = parent.ChildAt(i);

    // The child may be invalid due to issues in blink accessibility code.
    if (child.IsDetached()) {
      NOTREACHED() << "Should not try to serialize an invalid child:"
                   << "\nParent: " << parent.ToString(true).Utf8()
                   << "\nChild: " << child.ToString(true).Utf8();
      continue;
    }

    if (!child.AccessibilityIsIncludedInTree()) {
      NOTREACHED() << "Should not receive unincluded child."
                   << "\nChild: " << child.ToString(true).Utf8()
                   << "\nParent: " << parent.ToString(true).Utf8();
      continue;
    }

#if DCHECK_IS_ON()
    CheckParentUnignoredOf(parent, child);
#endif

    // These should not be produced by Blink. They are only needed on Mac and
    // handled in AXTableInfo on the browser side.
    DCHECK_NE(child.Role(), ax::mojom::Role::kColumn);
    DCHECK_NE(child.Role(), ax::mojom::Role::kTableHeaderContainer);

    // If an optional exclude_offscreen flag is set (only intended to be
    // used for a one-time snapshot of the accessibility tree), prune any
    // node that's entirely offscreen from the tree.
    if (exclude_offscreen() && child.IsOffScreen())
      continue;

    out_children->push_back(child);
  }
}

WebAXObject BlinkAXTreeSource::GetParent(WebAXObject node) const {
  CHECK(frozen_);

  // Blink returns ignored objects when walking up the parent chain,
  // we have to skip those here. Also, stop when we get to the root
  // element.
  do {
    if (node.Equals(root()))
      return WebAXObject();
    node = node.ParentObject();
  } while (!node.IsDetached() && !node.AccessibilityIsIncludedInTree());

  return node;
}

bool BlinkAXTreeSource::IsIgnored(WebAXObject node) const {
  return node.AccessibilityIsIgnored();
}

bool BlinkAXTreeSource::IsValid(WebAXObject node) const {
  return !node.IsDetached();  // This also checks if it's null.
}

bool BlinkAXTreeSource::IsEqual(WebAXObject node1, WebAXObject node2) const {
  return node1.Equals(node2);
}

WebAXObject BlinkAXTreeSource::GetNull() const {
  return WebAXObject();
}

std::string BlinkAXTreeSource::GetDebugString(blink::WebAXObject node) const {
  return node.ToString(true).Utf8();
}

void BlinkAXTreeSource::SerializerClearedNode(int32_t node_id) {
  cached_bounding_boxes_.erase(node_id);
}

void BlinkAXTreeSource::SerializeNode(WebAXObject src,
                                      ui::AXNodeData* dst) const {
#if DCHECK_IS_ON()
  // Never causes a document lifecycle change during serialization,
  // because the assumption is that layout is in a safe, stable state.
  WebDocument document = GetMainDocument();
  blink::WebDisallowTransitionScope disallow(&document);
#endif

  dst->id = src.AxID();
  dst->role = src.Role();

  if (src.IsDetached() || !src.AccessibilityIsIncludedInTree()) {
    dst->AddState(ax::mojom::State::kIgnored);
    NOTREACHED();
    return;
  }

  // TODO(crbug.com/1068668): AX onion soup - finish migrating the rest of
  // this function inside of AXObject::Serialize and removing
  // unneeded WebAXObject interfaces.
  src.Serialize(dst, accessibility_mode_);

  TRACE_EVENT2("accessibility", "BlinkAXTreeSource::SerializeNode", "role",
               ui::ToString(dst->role), "id", dst->id);

  if (accessibility_mode_.has_mode(ui::AXMode::kPDF)) {
    SerializeNameAndDescriptionAttributes(src, dst);
    // Return early. None of the following attributes are needed for PDFs.
    return;
  }

  // Bounding boxes are needed on all nodes, including ignored, for hit testing.
  SerializeBoundingBoxAttributes(src, dst);
  cached_bounding_boxes_[dst->id] = dst->relative_bounds;

  // Return early. The following attributes are unnecessary for ignored nodes.
  // Exception: focusable ignored nodes are fully serialized, so that reasonable
  // verbalizations can be made if they actually receive focus.
  if (src.AccessibilityIsIgnored() &&
      !dst->HasState(ax::mojom::State::kFocusable)) {
    // The name is important for exposing the selection around ignored nodes.
    // TODO(accessibility) Remove this and still pass this content_browsertest:
    // All/DumpAccessibilityTreeTest.AccessibilityIgnoredSelection/blink
    if (src.Role() == ax::mojom::Role::kStaticText)
      SerializeNameAndDescriptionAttributes(src, dst);
    return;
  }

  SerializeNameAndDescriptionAttributes(src, dst);

  if (accessibility_mode_.has_mode(ui::AXMode::kScreenReader)) {
    if (src.IsInLiveRegion())
      SerializeLiveRegionAttributes(src, dst);
    SerializeOtherScreenReaderAttributes(src, dst);
  }

  WebNode node = src.GetNode();
  bool is_iframe = false;
  if (!node.IsNull() && node.IsElementNode()) {
    WebElement element = node.To<WebElement>();
    is_iframe = element.HasHTMLTagName("iframe");

    // Presence of other ARIA attributes.
    if (src.HasAriaAttribute())
      dst->AddBoolAttribute(ax::mojom::BoolAttribute::kHasAriaAttribute, true);
  }

  if (dst->id == image_data_node_id_) {
    // In general, string attributes should be truncated using
    // TruncateAndAddStringAttribute, but ImageDataUrl contains a data url
    // representing an image, so add it directly using AddStringAttribute.
    dst->AddStringAttribute(ax::mojom::StringAttribute::kImageDataUrl,
                            src.ImageDataUrl(max_image_data_size_).Utf8());
  }
}

void BlinkAXTreeSource::SerializeBoundingBoxAttributes(
    WebAXObject src,
    ui::AXNodeData* dst) const {
  bool clips_children = false;
  PopulateAXRelativeBounds(src, &dst->relative_bounds, &clips_children);
  if (clips_children)
    dst->AddBoolAttribute(ax::mojom::BoolAttribute::kClipsChildren, true);

  if (src.IsLineBreakingObject()) {
    dst->AddBoolAttribute(ax::mojom::BoolAttribute::kIsLineBreakingObject,
                          true);
  }
}

void BlinkAXTreeSource::SerializeNameAndDescriptionAttributes(
    WebAXObject src,
    ui::AXNodeData* dst) const {
  ax::mojom::NameFrom name_from;
  blink::WebVector<WebAXObject> name_objects;
  blink::WebString web_name = src.GetName(name_from, name_objects);
  if ((!web_name.IsEmpty() && !web_name.IsNull()) ||
      name_from == ax::mojom::NameFrom::kAttributeExplicitlyEmpty) {
    int max_length = dst->role == ax::mojom::Role::kStaticText
                         ? kMaxStaticTextLength
                         : kMaxStringAttributeLength;
    TruncateAndAddStringAttribute(dst, ax::mojom::StringAttribute::kName,
                                  web_name.Utf8(), max_length);
    dst->SetNameFrom(name_from);
    AddIntListAttributeFromWebObjects(
        ax::mojom::IntListAttribute::kLabelledbyIds, name_objects, dst);
  }

  ax::mojom::DescriptionFrom description_from;
  blink::WebVector<WebAXObject> description_objects;
  blink::WebString web_description =
      src.Description(name_from, description_from, description_objects);
  if (!web_description.IsEmpty()) {
    TruncateAndAddStringAttribute(dst, ax::mojom::StringAttribute::kDescription,
                                  web_description.Utf8());
    dst->SetDescriptionFrom(description_from);
    AddIntListAttributeFromWebObjects(
        ax::mojom::IntListAttribute::kDescribedbyIds, description_objects, dst);
  }

  blink::WebString web_title = src.Title(name_from);
  if (!web_title.IsEmpty()) {
    TruncateAndAddStringAttribute(dst, ax::mojom::StringAttribute::kTooltip,
                                  web_title.Utf8());
  }

  if (accessibility_mode_.has_mode(ui::AXMode::kScreenReader)) {
    blink::WebString web_placeholder = src.Placeholder(name_from);
    if (!web_placeholder.IsEmpty())
      TruncateAndAddStringAttribute(dst,
                                    ax::mojom::StringAttribute::kPlaceholder,
                                    web_placeholder.Utf8());
  }
}

void BlinkAXTreeSource::SerializeInlineTextBoxAttributes(
    WebAXObject src,
    ui::AXNodeData* dst) const {
  DCHECK_EQ(ax::mojom::Role::kInlineTextBox, dst->role);

  WebVector<int> src_character_offsets;
  src.CharacterOffsets(src_character_offsets);
  dst->AddIntListAttribute(ax::mojom::IntListAttribute::kCharacterOffsets,
                           src_character_offsets.ReleaseVector());

  WebVector<int> src_word_starts;
  WebVector<int> src_word_ends;
  src.GetWordBoundaries(src_word_starts, src_word_ends);
  dst->AddIntListAttribute(ax::mojom::IntListAttribute::kWordStarts,
                           src_word_starts.ReleaseVector());
  dst->AddIntListAttribute(ax::mojom::IntListAttribute::kWordEnds,
                           src_word_ends.ReleaseVector());
}

void BlinkAXTreeSource::SerializeLiveRegionAttributes(
    WebAXObject src,
    ui::AXNodeData* dst) const {
  DCHECK(src.IsInLiveRegion());

  dst->AddBoolAttribute(ax::mojom::BoolAttribute::kLiveAtomic,
                        src.LiveRegionAtomic());
  if (!src.LiveRegionStatus().IsEmpty()) {
    TruncateAndAddStringAttribute(dst, ax::mojom::StringAttribute::kLiveStatus,
                                  src.LiveRegionStatus().Utf8());
  }
  TruncateAndAddStringAttribute(dst, ax::mojom::StringAttribute::kLiveRelevant,
                                src.LiveRegionRelevant().Utf8());
  // If we are not at the root of an atomic live region.
  if (src.ContainerLiveRegionAtomic() && !src.LiveRegionRoot().IsDetached() &&
      !src.LiveRegionAtomic()) {
    dst->AddIntAttribute(ax::mojom::IntAttribute::kMemberOfId,
                         src.LiveRegionRoot().AxID());
  }
  dst->AddBoolAttribute(ax::mojom::BoolAttribute::kContainerLiveAtomic,
                        src.ContainerLiveRegionAtomic());
  dst->AddBoolAttribute(ax::mojom::BoolAttribute::kContainerLiveBusy,
                        src.ContainerLiveRegionBusy());
  TruncateAndAddStringAttribute(
      dst, ax::mojom::StringAttribute::kContainerLiveStatus,
      src.ContainerLiveRegionStatus().Utf8());
  TruncateAndAddStringAttribute(
      dst, ax::mojom::StringAttribute::kContainerLiveRelevant,
      src.ContainerLiveRegionRelevant().Utf8());
}

void BlinkAXTreeSource::SerializeOtherScreenReaderAttributes(
    WebAXObject src,
    ui::AXNodeData* dst) const {
  if (dst->role == ax::mojom::Role::kColorWell)
    dst->AddIntAttribute(ax::mojom::IntAttribute::kColorValue,
                         src.ColorValue());

  if (dst->role == ax::mojom::Role::kLink) {
    WebAXObject target = src.InPageLinkTarget();
    if (!target.IsNull()) {
      int32_t target_id = target.AxID();
      dst->AddIntAttribute(ax::mojom::IntAttribute::kInPageLinkTargetId,
                           target_id);
    }
  }

  if (dst->role == ax::mojom::Role::kRadioButton) {
    AddIntListAttributeFromWebObjects(
        ax::mojom::IntListAttribute::kRadioGroupIds, src.RadioButtonsInGroup(),
        dst);
  }

  if (src.AriaCurrentState() != ax::mojom::AriaCurrentState::kNone) {
    dst->AddIntAttribute(ax::mojom::IntAttribute::kAriaCurrentState,
                         static_cast<int32_t>(src.AriaCurrentState()));
  }

  if (src.InvalidState() != ax::mojom::InvalidState::kNone)
    dst->SetInvalidState(src.InvalidState());
  if (src.InvalidState() == ax::mojom::InvalidState::kOther &&
      src.AriaInvalidValue().length()) {
    TruncateAndAddStringAttribute(dst,
                                  ax::mojom::StringAttribute::kAriaInvalidValue,
                                  src.AriaInvalidValue().Utf8());
  }

  if (src.CheckedState() != ax::mojom::CheckedState::kNone) {
    dst->SetCheckedState(src.CheckedState());
  }

  if (dst->role == ax::mojom::Role::kInlineTextBox) {
    SerializeInlineTextBoxAttributes(src, dst);
  }

  if (src.AccessKey().length()) {
    TruncateAndAddStringAttribute(dst, ax::mojom::StringAttribute::kAccessKey,
                                  src.AccessKey().Utf8());
  }

  if (src.AutoComplete().length()) {
    TruncateAndAddStringAttribute(dst,
                                  ax::mojom::StringAttribute::kAutoComplete,
                                  src.AutoComplete().Utf8());
  }

  if (src.Action() != ax::mojom::DefaultActionVerb::kNone) {
    dst->SetDefaultActionVerb(src.Action());
  }

  blink::WebString display_style = src.ComputedStyleDisplay();
  if (!display_style.IsEmpty()) {
    TruncateAndAddStringAttribute(dst, ax::mojom::StringAttribute::kDisplay,
                                  display_style.Utf8());
  }

  if (src.KeyboardShortcut().length() &&
      !dst->HasStringAttribute(ax::mojom::StringAttribute::kKeyShortcuts)) {
    TruncateAndAddStringAttribute(dst,
                                  ax::mojom::StringAttribute::kKeyShortcuts,
                                  src.KeyboardShortcut().Utf8());
  }

  if (!src.NextOnLine().IsDetached()) {
    dst->AddIntAttribute(ax::mojom::IntAttribute::kNextOnLineId,
                         src.NextOnLine().AxID());
  }

  if (!src.PreviousOnLine().IsDetached()) {
    dst->AddIntAttribute(ax::mojom::IntAttribute::kPreviousOnLineId,
                         src.PreviousOnLine().AxID());
  }

  if (!src.AriaActiveDescendant().IsDetached()) {
    dst->AddIntAttribute(ax::mojom::IntAttribute::kActivedescendantId,
                         src.AriaActiveDescendant().AxID());
  }

  if (!src.ErrorMessage().IsDetached()) {
    dst->AddIntAttribute(ax::mojom::IntAttribute::kErrormessageId,
                         src.ErrorMessage().AxID());
  }

  if (ui::SupportsHierarchicalLevel(dst->role) && src.HierarchicalLevel()) {
    dst->AddIntAttribute(ax::mojom::IntAttribute::kHierarchicalLevel,
                         src.HierarchicalLevel());
  }

  if (src.CanvasHasFallbackContent())
    dst->AddBoolAttribute(ax::mojom::BoolAttribute::kCanvasHasFallback, true);

  if (dst->role == ax::mojom::Role::kProgressIndicator ||
      dst->role == ax::mojom::Role::kMeter ||
      dst->role == ax::mojom::Role::kScrollBar ||
      dst->role == ax::mojom::Role::kSlider ||
      dst->role == ax::mojom::Role::kSpinButton ||
      (dst->role == ax::mojom::Role::kSplitter &&
       dst->HasState(ax::mojom::State::kFocusable))) {
    float value;
    if (src.ValueForRange(&value))
      dst->AddFloatAttribute(ax::mojom::FloatAttribute::kValueForRange, value);

    float max_value;
    if (src.MaxValueForRange(&max_value)) {
      dst->AddFloatAttribute(ax::mojom::FloatAttribute::kMaxValueForRange,
                             max_value);
    }

    float min_value;
    if (src.MinValueForRange(&min_value)) {
      dst->AddFloatAttribute(ax::mojom::FloatAttribute::kMinValueForRange,
                             min_value);
    }

    float step_value;
    if (src.StepValueForRange(&step_value)) {
      dst->AddFloatAttribute(ax::mojom::FloatAttribute::kStepValueForRange,
                             step_value);
    }
  }

  if (ui::IsDialog(dst->role)) {
    dst->AddBoolAttribute(ax::mojom::BoolAttribute::kModal, src.IsModal());
  }

  if (ui::IsImage(dst->role))
    AddImageAnnotations(src, dst);

  // If a link or web area isn't otherwise labeled and contains exactly one
  // image (searching only to a max depth of 2), and the link doesn't have
  // accessible text from an attribute like aria-label, then annotate the
  // link/web area with the image's annotation, too.
  if ((ui::IsLink(dst->role) || ui::IsPlatformDocument(dst->role)) &&
      dst->GetNameFrom() != ax::mojom::NameFrom::kAttribute) {
    WebAXObject inner_image;
    if (FindExactlyOneInnerImageInMaxDepthThree(src, &inner_image))
      AddImageAnnotations(inner_image, dst);
  }

  WebNode node = src.GetNode();
  if (!node.IsNull() && node.IsElementNode()) {
    WebElement element = node.To<WebElement>();
    if (element.HasHTMLTagName("input") && element.HasAttribute("type")) {
      TruncateAndAddStringAttribute(dst, ax::mojom::StringAttribute::kInputType,
                                    element.GetAttribute("type").Utf8());
    }
  }

  // aria-dropeffect is deprecated in WAI-ARIA 1.1.
  WebVector<ax::mojom::Dropeffect> src_dropeffects;
  src.Dropeffects(src_dropeffects);
  if (!src_dropeffects.empty()) {
    for (auto&& dropeffect : src_dropeffects) {
      dst->AddDropeffect(dropeffect);
    }
  }
}

blink::WebDocument BlinkAXTreeSource::GetMainDocument() const {
  CHECK(frozen_);
  return document_;
}

WebAXObject BlinkAXTreeSource::ComputeRoot() const {
  if (!explicit_root_.IsNull())
    return explicit_root_;

  if (!render_frame_ || !render_frame_->GetWebFrame())
    return WebAXObject();

  WebDocument document = render_frame_->GetWebFrame()->GetDocument();
  if (!document.IsNull())
    return WebAXObject::FromWebDocument(document);

  return WebAXObject();
}

void BlinkAXTreeSource::TruncateAndAddStringAttribute(
    ui::AXNodeData* dst,
    ax::mojom::StringAttribute attribute,
    const std::string& value,
    uint32_t max_len) const {
  if (value.size() > max_len) {
    std::string truncated;
    base::TruncateUTF8ToByteSize(value, max_len, &truncated);
    dst->AddStringAttribute(attribute, truncated);
  } else {
    dst->AddStringAttribute(attribute, value);
  }
}

void BlinkAXTreeSource::AddImageAnnotations(blink::WebAXObject& src,
                                            ui::AXNodeData* dst) const {
  if (!base::FeatureList::IsEnabled(features::kExperimentalAccessibilityLabels))
    return;

  // Reject ignored objects
  if (src.AccessibilityIsIgnored()) {
    return;
  }

  // Reject images that are explicitly empty, or that have a
  // meaningful name already.
  ax::mojom::NameFrom name_from;
  blink::WebVector<WebAXObject> name_objects;
  blink::WebString web_name = src.GetName(name_from, name_objects);

  // If an image has a nonempty name, compute whether we should add an
  // image annotation or not.
  bool should_annotate_image_with_nonempty_name = false;

  // When visual debugging is enabled, the "title" attribute is set to a
  // string beginning with a "%". If the name comes from that string we
  // can ignore it, and treat the name as empty.
  if (image_annotation_debugging_ &&
      base::StartsWith(web_name.Utf8(), "%", base::CompareCase::SENSITIVE))
    should_annotate_image_with_nonempty_name = true;

  if (features::IsAugmentExistingImageLabelsEnabled()) {
    // If the name consists of mostly stopwords, we can add an image
    // annotations. See ax_image_stopwords.h for details.
    if (image_annotator_->ImageNameHasMostlyStopwords(web_name.Utf8()))
      should_annotate_image_with_nonempty_name = true;
  }

  // If the image's name is explicitly empty, or if it has a name (and
  // we're not treating the name as empty), then it's ineligible for
  // an annotation.
  if ((name_from == ax::mojom::NameFrom::kAttributeExplicitlyEmpty ||
       !web_name.IsEmpty()) &&
      !should_annotate_image_with_nonempty_name) {
    dst->SetImageAnnotationStatus(
        ax::mojom::ImageAnnotationStatus::kIneligibleForAnnotation);
    return;
  }

  // If the name of a document (root web area) starts with the filename,
  // it probably means the user opened an image in a new tab.
  // If so, we can treat the name as empty and give it an annotation.
  std::string dst_name =
      dst->GetStringAttribute(ax::mojom::StringAttribute::kName);
  if (ui::IsPlatformDocument(dst->role)) {
    std::string filename = GURL(document().Url()).ExtractFileName();
    if (base::StartsWith(dst_name, filename, base::CompareCase::SENSITIVE))
      should_annotate_image_with_nonempty_name = true;
  }

  // |dst| may be a document or link containing an image. Skip annotating
  // it if it already has text other than whitespace.
  if (!base::ContainsOnlyChars(dst_name, base::kWhitespaceASCII) &&
      !should_annotate_image_with_nonempty_name) {
    dst->SetImageAnnotationStatus(
        ax::mojom::ImageAnnotationStatus::kIneligibleForAnnotation);
    return;
  }

  // Skip images that are too small to label. This also catches
  // unloaded images where the size is unknown.
  WebAXObject offset_container;
  gfx::RectF bounds;
  SkMatrix44 container_transform;
  bool clips_children = false;
  src.GetRelativeBounds(offset_container, bounds, container_transform,
                        &clips_children);
  if (bounds.width() < kMinImageAnnotationWidth ||
      bounds.height() < kMinImageAnnotationHeight) {
    dst->SetImageAnnotationStatus(
        ax::mojom::ImageAnnotationStatus::kIneligibleForAnnotation);
    return;
  }

  // Skip images in documents which are not http, https, file and data schemes.
  GURL gurl = document().Url();
  if (!(gurl.SchemeIsHTTPOrHTTPS() || gurl.SchemeIsFile() ||
        gurl.SchemeIs(url::kDataScheme))) {
    dst->SetImageAnnotationStatus(
        ax::mojom::ImageAnnotationStatus::kWillNotAnnotateDueToScheme);
    return;
  }

  if (!image_annotator_) {
    if (!first_unlabeled_image_id_.has_value() ||
        first_unlabeled_image_id_.value() == src.AxID()) {
      dst->SetImageAnnotationStatus(
          ax::mojom::ImageAnnotationStatus::kEligibleForAnnotation);
      first_unlabeled_image_id_ = src.AxID();
    } else {
      dst->SetImageAnnotationStatus(
          ax::mojom::ImageAnnotationStatus::kSilentlyEligibleForAnnotation);
    }
    return;
  }

  if (image_annotator_->HasAnnotationInCache(src)) {
    dst->AddStringAttribute(ax::mojom::StringAttribute::kImageAnnotation,
                            image_annotator_->GetImageAnnotation(src));
    dst->SetImageAnnotationStatus(
        image_annotator_->GetImageAnnotationStatus(src));
  } else if (image_annotator_->HasImageInCache(src)) {
    image_annotator_->OnImageUpdated(src);
    dst->SetImageAnnotationStatus(
        ax::mojom::ImageAnnotationStatus::kAnnotationPending);
  } else if (!image_annotator_->HasImageInCache(src)) {
    image_annotator_->OnImageAdded(src);
    dst->SetImageAnnotationStatus(
        ax::mojom::ImageAnnotationStatus::kAnnotationPending);
  }
}

}  // namespace content
