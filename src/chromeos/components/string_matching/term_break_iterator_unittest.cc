// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/components/string_matching/term_break_iterator.h"

#include "base/strings/utf_string_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace chromeos {
namespace string_matching {

using base::UTF8ToUTF16;

TEST(TermBreakIteratorTest, EmptyWord) {
  std::u16string empty;
  TermBreakIterator iter(empty);
  EXPECT_FALSE(iter.Advance());
}

TEST(TermBreakIteratorTest, Simple) {
  std::u16string word(u"simple");
  TermBreakIterator iter(word);
  EXPECT_TRUE(iter.Advance());
  EXPECT_EQ(u"simple", iter.GetCurrentTerm());
  EXPECT_FALSE(iter.Advance());  // Test unexpected advance after end.
}

TEST(TermBreakIteratorTest, CamelCase) {
  std::u16string word(u"CamelCase");
  TermBreakIterator iter(word);
  EXPECT_TRUE(iter.Advance());
  EXPECT_EQ(u"Camel", iter.GetCurrentTerm());
  EXPECT_TRUE(iter.Advance());
  EXPECT_EQ(u"Case", iter.GetCurrentTerm());
  EXPECT_FALSE(iter.Advance());  // Test unexpected advance after end.
}

TEST(TermBreakIteratorTest, LowerToUpper) {
  std::u16string word(u"lowerToUpper");
  TermBreakIterator iter(word);
  EXPECT_TRUE(iter.Advance());
  EXPECT_EQ(u"lower", iter.GetCurrentTerm());
  EXPECT_TRUE(iter.Advance());
  EXPECT_EQ(u"To", iter.GetCurrentTerm());
  EXPECT_TRUE(iter.Advance());
  EXPECT_EQ(u"Upper", iter.GetCurrentTerm());
  EXPECT_FALSE(iter.Advance());  // Test unexpected advance after end.
}

TEST(TermBreakIteratorTest, AlphaNumber) {
  std::u16string word(u"Chromium26.0.0.0");
  TermBreakIterator iter(word);
  EXPECT_TRUE(iter.Advance());
  EXPECT_EQ(u"Chromium", iter.GetCurrentTerm());
  EXPECT_TRUE(iter.Advance());
  EXPECT_EQ(u"26.0.0.0", iter.GetCurrentTerm());
  EXPECT_FALSE(iter.Advance());  // Test unexpected advance after end.
}

TEST(TermBreakIteratorTest, StartsWithNumber) {
  std::u16string word(u"123startWithNumber");
  TermBreakIterator iter(word);
  EXPECT_TRUE(iter.Advance());
  EXPECT_EQ(u"123", iter.GetCurrentTerm());
  EXPECT_TRUE(iter.Advance());
  EXPECT_EQ(u"start", iter.GetCurrentTerm());
  EXPECT_TRUE(iter.Advance());
  EXPECT_EQ(u"With", iter.GetCurrentTerm());
  EXPECT_TRUE(iter.Advance());
  EXPECT_EQ(u"Number", iter.GetCurrentTerm());
  EXPECT_FALSE(iter.Advance());  // Test unexpected advance after end.
}

TEST(TermBreakIteratorTest, CaseAndNoCase) {
  // "English" + two Chinese chars U+4E2D U+6587 + "Word"
  std::u16string word(UTF8ToUTF16("English\xe4\xb8\xad\xe6\x96\x87Word"));
  TermBreakIterator iter(word);
  EXPECT_TRUE(iter.Advance());
  EXPECT_EQ(u"English", iter.GetCurrentTerm());
  EXPECT_TRUE(iter.Advance());
  EXPECT_EQ(UTF8ToUTF16("\xe4\xb8\xad\xe6\x96\x87"), iter.GetCurrentTerm());
  EXPECT_TRUE(iter.Advance());
  EXPECT_EQ(u"Word", iter.GetCurrentTerm());
  EXPECT_FALSE(iter.Advance());  // Test unexpected advance after end.
}

}  // namespace string_matching
}  // namespace chromeos
