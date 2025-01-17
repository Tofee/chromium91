// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_PLATFORM_TESTING_PAINT_PROPERTY_TEST_HELPERS_H_
#define THIRD_PARTY_BLINK_RENDERER_PLATFORM_TESTING_PAINT_PROPERTY_TEST_HELPERS_H_

#include "third_party/blink/renderer/platform/graphics/paint/clip_paint_property_node.h"
#include "third_party/blink/renderer/platform/graphics/paint/effect_paint_property_node.h"
#include "third_party/blink/renderer/platform/graphics/paint/property_tree_state.h"
#include "third_party/blink/renderer/platform/graphics/paint/ref_counted_property_tree_state.h"
#include "third_party/blink/renderer/platform/graphics/paint/transform_paint_property_node.h"
#include "third_party/blink/renderer/platform/testing/paint_property_test_helpers.h"

namespace blink {

// Convenient shorthands.
inline const TransformPaintPropertyNode& t0() {
  return TransformPaintPropertyNode::Root();
}
inline const ClipPaintPropertyNode& c0() {
  return ClipPaintPropertyNode::Root();
}
inline const EffectPaintPropertyNode& e0() {
  return EffectPaintPropertyNode::Root();
}
inline const ScrollPaintPropertyNode& s0() {
  return ScrollPaintPropertyNode::Root();
}

constexpr int c0_id = 1;
constexpr int e0_id = 1;
constexpr int t0_id = 1;

inline scoped_refptr<EffectPaintPropertyNode> CreateOpacityEffect(
    const EffectPaintPropertyNodeOrAlias& parent,
    const TransformPaintPropertyNodeOrAlias& local_transform_space,
    const ClipPaintPropertyNodeOrAlias* output_clip,
    float opacity,
    CompositingReasons compositing_reasons = CompositingReason::kNone) {
  EffectPaintPropertyNode::State state;
  state.local_transform_space = &local_transform_space;
  state.output_clip = output_clip;
  state.opacity = opacity;
  state.direct_compositing_reasons = compositing_reasons;
  state.compositor_element_id = CompositorElementIdFromUniqueObjectId(
      NewUniqueObjectId(), CompositorElementIdNamespace::kPrimary);
  return EffectPaintPropertyNode::Create(parent, std::move(state));
}

inline scoped_refptr<EffectPaintPropertyNode> CreateOpacityEffect(
    const EffectPaintPropertyNodeOrAlias& parent,
    float opacity,
    CompositingReasons compositing_reasons = CompositingReason::kNone) {
  return CreateOpacityEffect(parent, parent.Unalias().LocalTransformSpace(),
                             parent.Unalias().OutputClip(), opacity,
                             compositing_reasons);
}

inline scoped_refptr<EffectPaintPropertyNode> CreateAnimatingOpacityEffect(
    const EffectPaintPropertyNodeOrAlias& parent,
    float opacity = 1.f,
    const ClipPaintPropertyNodeOrAlias* output_clip = nullptr) {
  EffectPaintPropertyNode::State state;
  state.local_transform_space = &parent.Unalias().LocalTransformSpace();
  state.output_clip = output_clip;
  state.opacity = opacity;
  state.direct_compositing_reasons = CompositingReason::kActiveOpacityAnimation;
  state.has_active_opacity_animation = true;
  state.compositor_element_id = CompositorElementIdFromUniqueObjectId(
      NewUniqueObjectId(), CompositorElementIdNamespace::kPrimaryEffect);
  return EffectPaintPropertyNode::Create(parent, std::move(state));
}

inline scoped_refptr<EffectPaintPropertyNode> CreateFilterEffect(
    const EffectPaintPropertyNodeOrAlias& parent,
    const TransformPaintPropertyNodeOrAlias& local_transform_space,
    const ClipPaintPropertyNodeOrAlias* output_clip,
    CompositorFilterOperations filter,
    CompositingReasons compositing_reasons = CompositingReason::kNone) {
  EffectPaintPropertyNode::State state;
  state.local_transform_space = &local_transform_space;
  state.output_clip = output_clip;
  state.filter = std::move(filter);
  state.direct_compositing_reasons = compositing_reasons;
  state.compositor_element_id = CompositorElementIdFromUniqueObjectId(
      NewUniqueObjectId(), CompositorElementIdNamespace::kEffectFilter);
  return EffectPaintPropertyNode::Create(parent, std::move(state));
}

inline scoped_refptr<EffectPaintPropertyNode> CreateFilterEffect(
    const EffectPaintPropertyNodeOrAlias& parent,
    CompositorFilterOperations filter,
    CompositingReasons compositing_reasons = CompositingReason::kNone) {
  return CreateFilterEffect(parent, parent.Unalias().LocalTransformSpace(),
                            parent.Unalias().OutputClip(), filter,
                            compositing_reasons);
}

inline scoped_refptr<EffectPaintPropertyNode> CreateAnimatingFilterEffect(
    const EffectPaintPropertyNodeOrAlias& parent,
    CompositorFilterOperations filter = CompositorFilterOperations(),
    const ClipPaintPropertyNodeOrAlias* output_clip = nullptr) {
  EffectPaintPropertyNode::State state;
  state.local_transform_space = &parent.Unalias().LocalTransformSpace();
  state.output_clip = output_clip;
  state.filter = std::move(filter);
  state.direct_compositing_reasons = CompositingReason::kActiveFilterAnimation;
  state.has_active_filter_animation = true;
  state.compositor_element_id = CompositorElementIdFromUniqueObjectId(
      NewUniqueObjectId(), CompositorElementIdNamespace::kEffectFilter);
  return EffectPaintPropertyNode::Create(parent, std::move(state));
}

inline scoped_refptr<EffectPaintPropertyNode> CreateBackdropFilterEffect(
    const EffectPaintPropertyNodeOrAlias& parent,
    const TransformPaintPropertyNodeOrAlias& local_transform_space,
    const ClipPaintPropertyNodeOrAlias* output_clip,
    CompositorFilterOperations backdrop_filter,
    float opacity = 1.0f) {
  EffectPaintPropertyNode::State state;
  state.local_transform_space = &local_transform_space;
  state.output_clip = output_clip;
  if (!backdrop_filter.IsEmpty()) {
    state.backdrop_filter_info =
        base::WrapUnique(new EffectPaintPropertyNode::BackdropFilterInfo{
            std::move(backdrop_filter)});
  }
  state.direct_compositing_reasons = CompositingReason::kBackdropFilter;
  state.compositor_element_id = CompositorElementIdFromUniqueObjectId(
      NewUniqueObjectId(), CompositorElementIdNamespace::kPrimary);
  state.opacity = opacity;
  return EffectPaintPropertyNode::Create(parent, std::move(state));
}

inline scoped_refptr<EffectPaintPropertyNode> CreateBackdropFilterEffect(
    const EffectPaintPropertyNodeOrAlias& parent,
    CompositorFilterOperations backdrop_filter) {
  return CreateBackdropFilterEffect(
      parent, parent.Unalias().LocalTransformSpace(),
      parent.Unalias().OutputClip(), backdrop_filter);
}

inline scoped_refptr<EffectPaintPropertyNode>
CreateAnimatingBackdropFilterEffect(
    const EffectPaintPropertyNodeOrAlias& parent,
    CompositorFilterOperations backdrop_filter = CompositorFilterOperations(),
    const ClipPaintPropertyNodeOrAlias* output_clip = nullptr) {
  EffectPaintPropertyNode::State state;
  state.local_transform_space = &parent.Unalias().LocalTransformSpace();
  state.output_clip = output_clip;
  if (!backdrop_filter.IsEmpty()) {
    state.backdrop_filter_info =
        base::WrapUnique(new EffectPaintPropertyNode::BackdropFilterInfo{
            std::move(backdrop_filter)});
  }
  state.direct_compositing_reasons =
      CompositingReason::kActiveBackdropFilterAnimation;
  state.has_active_backdrop_filter_animation = true;
  state.compositor_element_id = CompositorElementIdFromUniqueObjectId(
      NewUniqueObjectId(), CompositorElementIdNamespace::kPrimaryEffect);
  return EffectPaintPropertyNode::Create(parent, std::move(state));
}

inline scoped_refptr<ClipPaintPropertyNode> CreateClip(
    const ClipPaintPropertyNodeOrAlias& parent,
    const TransformPaintPropertyNodeOrAlias& local_transform_space,
    const FloatRoundedRect& clip_rect) {
  ClipPaintPropertyNode::State state(&local_transform_space, clip_rect);
  return ClipPaintPropertyNode::Create(parent, std::move(state));
}

inline scoped_refptr<ClipPaintPropertyNode> CreateClip(
    const ClipPaintPropertyNodeOrAlias& parent,
    const TransformPaintPropertyNodeOrAlias& local_transform_space,
    const FloatRoundedRect& clip_rect,
    const FloatRoundedRect& pixel_snapped_clip_rect) {
  ClipPaintPropertyNode::State state(&local_transform_space, clip_rect,
                                     pixel_snapped_clip_rect);
  return ClipPaintPropertyNode::Create(parent, std::move(state));
}

inline scoped_refptr<ClipPaintPropertyNode> CreateClipPathClip(
    const ClipPaintPropertyNodeOrAlias& parent,
    const TransformPaintPropertyNodeOrAlias& local_transform_space,
    const FloatRoundedRect& clip_rect) {
  ClipPaintPropertyNode::State state(&local_transform_space, clip_rect);
  state.clip_path = base::AdoptRef(new RefCountedPath);
  return ClipPaintPropertyNode::Create(parent, std::move(state));
}

inline scoped_refptr<TransformPaintPropertyNode> Create2DTranslation(
    const TransformPaintPropertyNodeOrAlias& parent,
    float x,
    float y) {
  return TransformPaintPropertyNode::Create(
      parent, TransformPaintPropertyNode::State{FloatSize(x, y)});
}

inline scoped_refptr<TransformPaintPropertyNode> CreateTransform(
    const TransformPaintPropertyNodeOrAlias& parent,
    const TransformationMatrix& matrix,
    const FloatPoint3D& origin = FloatPoint3D(),
    CompositingReasons compositing_reasons = CompositingReason::kNone) {
  TransformPaintPropertyNode::State state{{matrix, origin}};
  state.direct_compositing_reasons = compositing_reasons;
  return TransformPaintPropertyNode::Create(parent, std::move(state));
}

inline scoped_refptr<TransformPaintPropertyNode> CreateAnimatingTransform(
    const TransformPaintPropertyNodeOrAlias& parent,
    const TransformationMatrix& matrix = TransformationMatrix(),
    const FloatPoint3D& origin = FloatPoint3D()) {
  TransformPaintPropertyNode::State state{{matrix, origin}};
  state.direct_compositing_reasons =
      CompositingReason::kActiveTransformAnimation;
  state.compositor_element_id = CompositorElementIdFromUniqueObjectId(
      NewUniqueObjectId(), CompositorElementIdNamespace::kPrimaryTransform);
  return TransformPaintPropertyNode::Create(parent, std::move(state));
}

inline scoped_refptr<TransformPaintPropertyNode> CreateScrollTranslation(
    const TransformPaintPropertyNodeOrAlias& parent,
    float offset_x,
    float offset_y,
    const ScrollPaintPropertyNode& scroll,
    CompositingReasons compositing_reasons = CompositingReason::kNone) {
  TransformPaintPropertyNode::State state{FloatSize(offset_x, offset_y)};
  state.direct_compositing_reasons = compositing_reasons;
  state.scroll = &scroll;
  return TransformPaintPropertyNode::Create(parent, std::move(state));
}

inline scoped_refptr<TransformPaintPropertyNode> CreateScrollTranslation(
    const TransformPaintPropertyNodeOrAlias& parent,
    float offset_x,
    float offset_y,
    const IntRect& container_rect,
    const IntSize& contents_size,
    CompositingReasons compositing_reasons = CompositingReason::kNone) {
  const auto* parent_scroll_translation = &parent.Unalias();
  while (!parent_scroll_translation->ScrollNode())
    parent_scroll_translation = parent_scroll_translation->UnaliasedParent();
  ScrollPaintPropertyNode::State scroll_state;
  scroll_state.container_rect = container_rect;
  scroll_state.contents_size = contents_size;
  TransformPaintPropertyNode::State translation_state{
      FloatSize(offset_x, offset_y)};
  translation_state.direct_compositing_reasons = compositing_reasons;
  translation_state.scroll = ScrollPaintPropertyNode::Create(
      *parent_scroll_translation->ScrollNode(), std::move(scroll_state));
  return TransformPaintPropertyNode::Create(parent,
                                            std::move(translation_state));
}

inline scoped_refptr<TransformPaintPropertyNode>
CreateCompositedScrollTranslation(
    const TransformPaintPropertyNodeOrAlias& parent,
    float offset_x,
    float offset_y,
    const ScrollPaintPropertyNode& scroll) {
  return CreateScrollTranslation(parent, offset_x, offset_y, scroll,
                                 CompositingReason::kOverflowScrolling);
}

inline scoped_refptr<TransformPaintPropertyNode>
CreateCompositedScrollTranslation(
    const TransformPaintPropertyNodeOrAlias& parent,
    float offset_x,
    float offset_y,
    const IntRect& container_rect,
    const IntSize& contents_size) {
  return CreateScrollTranslation(parent, offset_x, offset_y, container_rect,
                                 contents_size,
                                 CompositingReason::kOverflowScrolling);
}

inline RefCountedPropertyTreeState CreateScrollTranslationState(
    const PropertyTreeState& parent_state,
    float offset_x,
    float offset_y,
    const IntRect& container_rect,
    const IntSize& contents_size,
    CompositingReasons compositing_reasons = CompositingReason::kNone) {
  return RefCountedPropertyTreeState(PropertyTreeState(
      *CreateScrollTranslation(parent_state.Transform(), offset_x, offset_y,
                               container_rect, contents_size,
                               compositing_reasons),
      *CreateClip(parent_state.Clip(), parent_state.Transform(),
                  FloatRoundedRect(container_rect)),
      e0()));
}

inline RefCountedPropertyTreeState CreateCompositedScrollTranslationState(
    const PropertyTreeState& parent_state,
    float offset_x,
    float offset_y,
    const IntRect& container_rect,
    const IntSize& contents_size,
    CompositingReasons compositing_reasons = CompositingReason::kNone) {
  return CreateScrollTranslationState(parent_state, offset_x, offset_y,
                                      container_rect, contents_size,
                                      CompositingReason::kOverflowScrolling);
}

inline PropertyTreeState DefaultPaintChunkProperties() {
  return PropertyTreeState::Root();
}

// Checked downcast from *PaintPropertyNodeOrAlias to *PaintPropertyNode.
// This is used in tests that expect the node to be an unaliased node.
inline const ClipPaintPropertyNode& ToUnaliased(
    const ClipPaintPropertyNodeOrAlias& node) {
  DCHECK(!node.IsParentAlias());
  return static_cast<const ClipPaintPropertyNode&>(node);
}
inline const EffectPaintPropertyNode& ToUnaliased(
    const EffectPaintPropertyNodeOrAlias& node) {
  DCHECK(!node.IsParentAlias());
  return static_cast<const EffectPaintPropertyNode&>(node);
}
inline const TransformPaintPropertyNode& ToUnaliased(
    const TransformPaintPropertyNodeOrAlias& node) {
  DCHECK(!node.IsParentAlias());
  return static_cast<const TransformPaintPropertyNode&>(node);
}

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_PLATFORM_TESTING_PAINT_PROPERTY_TEST_HELPERS_H_
