// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/translate/content/android/translate_utils.h"

#include "base/android/jni_array.h"
#include "base/macros.h"
#include "base/strings/utf_string_conversions.h"
#include "components/metrics/metrics_log.h"
#include "components/translate/core/browser/mock_translate_infobar_delegate.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::Test;

using translate::testing::MockTranslateInfoBarDelegate;
using translate::testing::MockTranslateInfoBarDelegateFactory;

namespace translate {
namespace {
const std::vector<std::string> kCodes = {"en", "de", "pl"};
}  // namespace
class TranslateUtilsTest : public ::testing::Test {
 public:
  TranslateUtilsTest() = default;

 protected:
  void SetUp() override {
    delegate_factory_ = new MockTranslateInfoBarDelegateFactory("en", "pl");
    delegate_ = delegate_factory_->GetMockTranslateInfoBarDelegate();
    env_ = base::android::AttachCurrentThread();
  }

  MockTranslateInfoBarDelegateFactory* delegate_factory_;
  MockTranslateInfoBarDelegate* delegate_;
  JNIEnv* env_;
};
// Tests that content languages information in the java format is correct for
// content languages (names, native names, codes are as expected, hashcodes are
// empty).
TEST_F(TranslateUtilsTest, GetJavaContentLangauges) {
  // Set up the mock delegate.
  std::vector<std::string> test_languages = {"en", "de", "pl"};
  delegate_->SetContentLanguagesCodesForTest(test_languages);

  base::android::ScopedJavaLocalRef<jobjectArray> contentLanguages =
      TranslateUtils::GetContentLanguagesInJavaFormat(env_, delegate_);

  // Test language codes are as expected.
  std::vector<std::string> actual_codes;
  base::android::AppendJavaStringArrayToStringVector(env_, contentLanguages,
                                                     &actual_codes);
  EXPECT_THAT(actual_codes, ::testing::ContainerEq(kCodes));
}

// Tests that application handles empty content language data gracefully.
TEST_F(TranslateUtilsTest, GetJavaContentLangaugesEmpty) {
  std::vector<std::string> empty;
  delegate_->SetContentLanguagesCodesForTest(empty);
  base::android::ScopedJavaLocalRef<jobjectArray> contentLanguages =
      TranslateUtils::GetContentLanguagesInJavaFormat(env_, delegate_);

  // Test language codes are empty.
  std::vector<std::string> actual_codes;
  base::android::AppendJavaStringArrayToStringVector(env_, contentLanguages,
                                                     &actual_codes);
  ASSERT_TRUE(actual_codes.empty());
}

// Test that language information in the java format is correct for all
// translate languages (names, codes and hashcodes are as expected, no native
// names).
TEST_F(TranslateUtilsTest, GetJavaLangauges) {
  std::vector<std::pair<std::string, std::u16string>> translate_languages = {
      {"en", u"English"}, {"de", u"German"}, {"pl", u"Polish"}};
  std::vector<std::u16string> expectedLanguageNames = {u"English", u"German",
                                                       u"Polish"};
  std::vector<int> expectedHashCodes = {metrics::MetricsLog::Hash("en"),
                                        metrics::MetricsLog::Hash("de"),
                                        metrics::MetricsLog::Hash("pl")};

  delegate_->SetTranslateLanguagesForTest(translate_languages);
  // Test that all languages in Java format are returned property.
  JavaLanguageInfoWrapper contentLanguages =
      TranslateUtils::GetTranslateLanguagesInJavaFormat(env_, delegate_);

  // Test language names are as expected.
  std::vector<std::u16string> actual_language_names;
  base::android::AppendJavaStringArrayToStringVector(
      env_, contentLanguages.java_languages, &actual_language_names);
  EXPECT_THAT(actual_language_names,
              ::testing::ContainerEq(expectedLanguageNames));

  // Test language codes
  std::vector<std::string> actual_codes;
  base::android::AppendJavaStringArrayToStringVector(
      env_, contentLanguages.java_codes, &actual_codes);
  EXPECT_THAT(actual_codes, ::testing::ContainerEq(kCodes));

  std::vector<int> actual_hash_codes;
  base::android::JavaIntArrayToIntVector(env_, contentLanguages.java_hash_codes,
                                         &actual_hash_codes);
  EXPECT_THAT(actual_hash_codes, ::testing::ContainerEq(expectedHashCodes));
}

}  // namespace translate
