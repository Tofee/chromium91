// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_FORM_PARSING_PARSING_TEST_UTILS_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_FORM_PARSING_PARSING_TEST_UTILS_H_

#include <memory>
#include <string>
#include <vector>

#include "base/macros.h"
#include "base/strings/utf_string_conversions.h"
#include "components/autofill/core/browser/autofill_field.h"
#include "components/autofill/core/browser/field_types.h"
#include "components/autofill/core/browser/form_parsing/address_field.h"
#include "components/autofill/core/browser/form_parsing/autofill_scanner.h"
#include "components/autofill/core/common/form_field_data.h"
#include "components/autofill/core/common/language_code.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace autofill {

enum class ParseResult {
  // The form was successfully parsed and at least one type was assigned.
  PARSED = 0,
  // Not a single type was assigned.
  NOT_PARSED,
  kMaxValue = NOT_PARSED
};

class FormFieldTestBase {
 public:
  FormFieldTestBase(const FormFieldTestBase&) = delete;
  FormFieldTestBase();
  ~FormFieldTestBase();
  FormFieldTestBase& operator=(const FormFieldTestBase&) = delete;

 protected:
  // Add a field with |control_type|, the |name|, the |label| the expected
  // parsed type |expected_type|.
  void AddFormFieldData(std::string control_type,
                        std::string name,
                        std::string label,
                        ServerFieldType expected_type);

  // Convenience wrapper for text control elements with a maximal length.
  void AddFormFieldDataWithLength(std::string control_type,
                                  std::string name,
                                  std::string label,
                                  int max_length,
                                  ServerFieldType expected_type);

  // Convenience wrapper for text control elements.
  void AddTextFormFieldData(std::string name,
                            std::string label,
                            ServerFieldType expected_classification);

  // Convenience wrapper for 'select-one' elements with a max length.
  void AddSelectOneFormFieldDataWithLength(
      std::string name,
      std::string label,
      int max_length,
      const std::vector<std::string>& options_contents,
      const std::vector<std::string>& options_values,
      ServerFieldType expected_type);

  // Convenience wrapper for 'select-one' elements.
  void AddSelectOneFormFieldData(
      std::string name,
      std::string label,
      const std::vector<std::string>& options_contents,
      const std::vector<std::string>& options_values,
      ServerFieldType expected_type);

  // Apply parsing and verify the expected types.
  // |parsed| indicates if at least one field could be parsed successfully.
  // |page_language| the language to be used for parsing, default empty value
  // means the language is unknown and patterns of all languages are used.
  void ClassifyAndVerify(ParseResult parse_result = ParseResult::PARSED,
                         const LanguageCode& page_language = LanguageCode(""));

  // Test the parsed verifications against the expectations.
  void TestClassificationExpectations();

  // Apply the parsing with a specific parser.
  virtual std::unique_ptr<FormField> Parse(
      AutofillScanner* scanner,
      const LanguageCode& page_language) = 0;

  FieldRendererId MakeFieldRendererId();

  std::vector<std::unique_ptr<AutofillField>> list_;
  std::unique_ptr<FormField> field_;
  FieldCandidatesMap field_candidates_map_;
  std::map<FieldGlobalId, ServerFieldType> expected_classifications_;

 private:
  uint64_t id_counter_ = 0;
};

class FormFieldTest : public FormFieldTestBase, public testing::Test {
 public:
  FormFieldTest(const FormFieldTest&) = delete;
  FormFieldTest();
  ~FormFieldTest() override;
  FormFieldTest& operator=(const FormFieldTest&) = delete;
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_FORM_PARSING_PARSING_TEST_UTILS_H_
