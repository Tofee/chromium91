// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_NG_INLINE_NG_LINE_INFO_H_
#define THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_NG_INLINE_NG_LINE_INFO_H_

#include "third_party/blink/renderer/core/layout/ng/geometry/ng_bfc_offset.h"
#include "third_party/blink/renderer/core/layout/ng/inline/ng_inline_item_result.h"
#include "third_party/blink/renderer/core/style/computed_style_base_constants.h"

namespace blink {

class ComputedStyle;
class NGInlineNode;
struct NGInlineItemsData;

// Represents a line to build.
//
// This is a transient context object only while building line boxes.
//
// NGLineBreaker produces, and NGInlineLayoutAlgorithm consumes.
class CORE_EXPORT NGLineInfo {
  STACK_ALLOCATED();

 public:
  const NGInlineItemsData& ItemsData() const {
    DCHECK(items_data_);
    return *items_data_;
  }

  // The style to use for the line.
  const ComputedStyle& LineStyle() const {
    DCHECK(line_style_);
    return *line_style_;
  }
  void SetLineStyle(const NGInlineNode&,
                    const NGInlineItemsData&,
                    bool use_first_line_style);

  // Use ::first-line style if true.
  // https://drafts.csswg.org/css-pseudo/#selectordef-first-line
  // This is false for the "first formatted line" if '::first-line' rule is not
  // used in the document.
  // https://www.w3.org/TR/CSS22/selector.html#first-formatted-line
  bool UseFirstLineStyle() const { return use_first_line_style_; }

  // The last line of a block, or the line ends with a forced line break.
  // https://drafts.csswg.org/css-text-3/#propdef-text-align-last
  bool IsLastLine() const { return is_last_line_; }
  void SetIsLastLine(bool is_last_line) { is_last_line_ = is_last_line; }

  // If the line is marked as empty, it means that there's no content that
  // requires it to be present at all, e.g. when there are only close tags with
  // no margin/border/padding.
  bool IsEmptyLine() const { return is_empty_line_; }
  void SetIsEmptyLine() { is_empty_line_ = true; }

  // NGInlineItemResults for this line.
  NGInlineItemResults* MutableResults() { return &results_; }
  const NGInlineItemResults& Results() const { return results_; }

  void SetTextIndent(LayoutUnit indent) { text_indent_ = indent; }
  LayoutUnit TextIndent() const { return text_indent_; }

  ETextAlign TextAlign() const { return text_align_; }
  // Update |TextAlign()| and related fields. This depends on |IsLastLine()| and
  // that must be called after |SetIsLastLine()|.
  void UpdateTextAlign();

  NGBfcOffset BfcOffset() const { return bfc_offset_; }
  LayoutUnit AvailableWidth() const { return available_width_; }

  // The width of this line. Includes trailing spaces if they were preserved.
  // Negative width created by negative 'text-indent' is clamped to zero.
  LayoutUnit Width() const { return width_.ClampNegativeToZero(); }
  // Same as |Width()| but returns negative value as is. Preserved trailing
  // spaces may or may not be included, depends on |ShouldHangTrailingSpaces()|.
  LayoutUnit WidthForAlignment() const { return width_ - hang_width_; }
  // Width that hangs over the end of the line; e.g., preserved trailing spaces.
  LayoutUnit HangWidth() const { return hang_width_; }
  // Compute |Width()| from |Results()|. Used during line breaking, before
  // |Width()| is set. After line breaking, this should match to |Width()|
  // without clamping.
  LayoutUnit ComputeWidth() const;

#if DCHECK_IS_ON()
  // Returns width in float. This function is used for avoiding |LayoutUnit|
  // saturated addition of items in line.
  float ComputeWidthInFloat() const;
#endif

  bool HasTrailingSpaces() const { return has_trailing_spaces_; }
  void SetHasTrailingSpaces() { has_trailing_spaces_ = true; }
  bool ShouldHangTrailingSpaces() const;

  // True if this line has overflow, excluding preserved trailing spaces.
  bool HasOverflow() const { return has_overflow_; }
  void SetHasOverflow(bool value = true) { has_overflow_ = value; }

  void SetBfcOffset(const NGBfcOffset& bfc_offset) { bfc_offset_ = bfc_offset; }
  void SetWidth(LayoutUnit available_width, LayoutUnit width) {
    available_width_ = available_width;
    width_ = width;
  }

  // Start text offset of this line.
  unsigned StartOffset() const { return start_offset_; }
  void SetStartOffset(unsigned offset) { start_offset_ = offset; }
  // End text offset of this line, excluding out-of-flow objects such as
  // floating or positioned.
  unsigned InflowEndOffset() const;
  // End text offset for `text-align: justify`. This excludes preserved trailing
  // spaces. Available only when |TextAlign()| is |kJustify|.
  unsigned EndOffsetForJustify() const {
    DCHECK_EQ(text_align_, ETextAlign::kJustify);
    return end_offset_for_justify_;
  }
  // End item index of this line.
  unsigned EndItemIndex() const { return end_item_index_; }
  void SetEndItemIndex(unsigned index) { end_item_index_ = index; }

  // The base direction of this line for the bidi algorithm.
  TextDirection BaseDirection() const { return base_direction_; }
  void SetBaseDirection(TextDirection direction) {
    base_direction_ = direction;
  }

  // Whether an accurate end position is needed, typically for end, center, and
  // justify alignment.
  bool NeedsAccurateEndPosition() const { return needs_accurate_end_position_; }

 private:
  ETextAlign GetTextAlign(bool is_last_line = false) const;
  bool ComputeNeedsAccurateEndPosition() const;

  // The width of preserved trailing spaces.
  LayoutUnit ComputeTrailingSpaceWidth(
      unsigned* end_offset_out = nullptr) const;

  const NGInlineItemsData* items_data_ = nullptr;
  const ComputedStyle* line_style_ = nullptr;
  NGInlineItemResults results_;

  NGBfcOffset bfc_offset_;

  LayoutUnit available_width_;
  LayoutUnit width_;
  LayoutUnit hang_width_;
  LayoutUnit text_indent_;

  unsigned start_offset_;
  unsigned end_item_index_;
  unsigned end_offset_for_justify_;

  ETextAlign text_align_ = ETextAlign::kLeft;
  TextDirection base_direction_ = TextDirection::kLtr;

  bool use_first_line_style_ = false;
  bool is_last_line_ = false;
  bool is_empty_line_ = false;
  bool has_overflow_ = false;
  bool has_trailing_spaces_ = false;
  bool needs_accurate_end_position_ = false;
  bool is_ruby_base_ = false;
  bool is_ruby_text_ = false;
};

std::ostream& operator<<(std::ostream& ostream, const NGLineInfo& line_info);

}  // namespace blink

#endif  // THIRD_PARTY_BLINK_RENDERER_CORE_LAYOUT_NG_INLINE_NG_LINE_INFO_H_
