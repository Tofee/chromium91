// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_DATA_MODEL_CONTACT_INFO_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_DATA_MODEL_CONTACT_INFO_H_

#include <memory>
#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "components/autofill/core/browser/data_model/autofill_structured_address_name.h"
#include "components/autofill/core/browser/data_model/form_group.h"

namespace autofill {

class AutofillProfile;

// A form group that stores name information.
class NameInfo : public FormGroup {
 public:
  NameInfo();
  NameInfo(const NameInfo& info);
  ~NameInfo() override;

  NameInfo& operator=(const NameInfo& info);
  bool operator==(const NameInfo& other) const;
  bool operator!=(const NameInfo& other) const { return !operator==(other); }

  // FormGroup:
  std::u16string GetRawInfo(ServerFieldType type) const override;

  void GetMatchingTypes(const std::u16string& text,
                        const std::string& app_locale,
                        ServerFieldTypeSet* matching_types) const override;

  void SetRawInfoWithVerificationStatus(
      ServerFieldType type,
      const std::u16string& value,
      structured_address::VerificationStatus status) override;

  // Derives all missing tokens in the structured representation of the name by
  // either parsing missing tokens from their assigned parent or by formatting
  // them from their assigned children.
  // Return false if the completion is not possible either because no value is
  // set or because there are two conflicting values set. Two values are
  // conflicting iff they are on the same root-to-leaf path.
  // For example, NAME_FIRST is child of NAME_LAST and if both are set, the tree
  // cannot be completed.
  // |profile_is_verified| indicates that the profile is already verified.
  bool FinalizeAfterImport(bool profile_is_verified);

  // Convenience wrapper to invoke finalization for unverified profiles.
  bool FinalizeAfterImport() { return FinalizeAfterImport(false); }

  // Returns true if the structured-name information in |this| and |newer| are
  // mergeable. Note, returns false if |newer| is variant of |this| or vice
  // verda. A name variant is a variation that allows for abbreviations, a
  // reordering and omission of the tokens.
  bool IsStructuredNameMergeable(const NameInfo& newer) const;

  // Merges the structured name-information of |newer| into |this|.
  bool MergeStructuredName(const NameInfo& newer);

  // Merges the validation statuses of |newer| into |this|.
  // If two tokens of the same type have the exact same value, the validation
  // status is updated to the higher one.
  void MergeStructuredNameValidationStatuses(const NameInfo& newer);

  // Returns a constant reference to the structured name tree.
  const structured_address::AddressComponent& GetStructuredName() const {
    return *name_;
  }

 private:
  // FormGroup:
  void GetSupportedTypes(ServerFieldTypeSet* supported_types) const override;
  std::u16string GetInfoImpl(const AutofillType& type,
                             const std::string& app_locale) const override;

  bool SetInfoWithVerificationStatusImpl(
      const AutofillType& type,
      const std::u16string& value,
      const std::string& app_locale,
      structured_address::VerificationStatus status) override;

  // Return the verification status of a structured name value.
  structured_address::VerificationStatus GetVerificationStatusImpl(
      ServerFieldType type) const override;

  // Returns the full name, which is either |full_|, or if |full_| is empty,
  // is composed of given, middle and family.
  std::u16string FullName() const;

  // Returns the middle initial if |middle_| is non-empty.  Returns an empty
  // string otherwise.
  std::u16string MiddleInitial() const;

  // Sets |given_|, |middle_|, and |family_| to the tokenized |full|.
  void SetFullName(const std::u16string& full);

  // Legacy fields to store the unstructured representation of the name when
  // |features::kAutofillEnableSupportForMoreStructureInNames| is not enabled.
  std::u16string given_;
  std::u16string middle_;
  std::u16string family_;
  std::u16string full_;

  // This data structure stores the more-structured representation of the name
  // when |features::kAutofillEnableSupportForMoreStructureInNames| is enabled.
  const std::unique_ptr<structured_address::AddressComponent> name_;
};

class EmailInfo : public FormGroup {
 public:
  EmailInfo();
  EmailInfo(const EmailInfo& info);
  ~EmailInfo() override;

  EmailInfo& operator=(const EmailInfo& info);
  bool operator==(const EmailInfo& other) const;
  bool operator!=(const EmailInfo& other) const { return !operator==(other); }

  // FormGroup:
  std::u16string GetRawInfo(ServerFieldType type) const override;
  void SetRawInfoWithVerificationStatus(
      ServerFieldType type,
      const std::u16string& value,
      structured_address::VerificationStatus status) override;

 private:
  // FormGroup:
  void GetSupportedTypes(ServerFieldTypeSet* supported_types) const override;

  std::u16string email_;
};

class CompanyInfo : public FormGroup {
 public:
  CompanyInfo();
  CompanyInfo(const CompanyInfo& info);
  explicit CompanyInfo(const AutofillProfile* profile);
  ~CompanyInfo() override;

  CompanyInfo& operator=(const CompanyInfo& info);
  bool operator==(const CompanyInfo& other) const;
  bool operator!=(const CompanyInfo& other) const { return !operator==(other); }

  // FormGroup:
  std::u16string GetRawInfo(ServerFieldType type) const override;
  void SetRawInfoWithVerificationStatus(
      ServerFieldType type,
      const std::u16string& value,
      structured_address::VerificationStatus status) override;
  void set_profile(const AutofillProfile* profile) { profile_ = profile; }

 private:
  // FormGroup:
  void GetSupportedTypes(ServerFieldTypeSet* supported_types) const override;
  bool IsValidOrVerified(const std::u16string& value) const;

  std::u16string company_name_;
  const AutofillProfile* profile_ = nullptr;
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_DATA_MODEL_CONTACT_INFO_H_
