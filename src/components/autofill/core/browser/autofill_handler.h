// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_AUTOFILL_CORE_BROWSER_AUTOFILL_HANDLER_H_
#define COMPONENTS_AUTOFILL_CORE_BROWSER_AUTOFILL_HANDLER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/cancelable_callback.h"
#include "base/compiler_specific.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "components/autofill/core/browser/autofill_download_manager.h"
#include "components/autofill/core/browser/autofill_driver.h"
#include "components/autofill/core/browser/autofill_metrics.h"
#include "components/autofill/core/common/dense_set.h"
#include "components/autofill/core/common/form_data.h"
#include "components/autofill/core/common/language_code.h"
#include "components/autofill/core/common/mojom/autofill_types.mojom.h"
#include "components/autofill/core/common/signatures.h"
#include "components/autofill/core/common/unique_ids.h"
#include "components/translate/core/browser/translate_driver.h"
#include "components/version_info/channel.h"

namespace gfx {
class RectF;
}

namespace autofill {

class AutofillField;
struct FormData;
struct FormFieldData;
class FormStructure;
class LogManager;

// This class defines the interface should be implemented by autofill
// implementation in browser side to interact with AutofillDriver.
class AutofillHandler
    : public AutofillDownloadManager::Observer,
      public translate::TranslateDriver::LanguageDetectionObserver {
 public:
  enum AutofillDownloadManagerState {
    ENABLE_AUTOFILL_DOWNLOAD_MANAGER,
    DISABLE_AUTOFILL_DOWNLOAD_MANAGER,
  };

  // An observer class used by browsertests that gets notified whenever
  // particular actions occur.
  class ObserverForTest {
   public:
    virtual void OnFormParsed() = 0;
  };

  // Rich queries are enabled by feature flag iff this chrome instance is
  // neither on the STABLE nor BETA release channel.
  static bool IsRichQueryEnabled(version_info::Channel channel);

  // Raw metadata uploading enabled iff this Chrome instance is on Canary or Dev
  // channel.
  static bool IsRawMetadataUploadingEnabled(version_info::Channel channel);

  // TODO(crbug.com/1151542): Move to anonymous namespace once
  // AutofillManager::OnLoadedServerPredictions() moves to AutofillHandler.
  static void LogAutofillTypePredictionsAvailable(
      LogManager* log_manager,
      const std::vector<FormStructure*>& forms);

  ~AutofillHandler() override;

  AutofillClient* client() { return client_; }
  const AutofillClient* client() const { return client_; }

  // Invoked when the value of textfield is changed.
  void OnTextFieldDidChange(const FormData& form,
                            const FormFieldData& field,
                            const gfx::RectF& bounding_box,
                            const base::TimeTicks timestamp);

  // Invoked when the textfield is scrolled.
  void OnTextFieldDidScroll(const FormData& form,
                            const FormFieldData& field,
                            const gfx::RectF& bounding_box);

  // Invoked when the value of select is changed.
  void OnSelectControlDidChange(const FormData& form,
                                const FormFieldData& field,
                                const gfx::RectF& bounding_box);

  // Invoked when the |form| needs to be autofilled, the |bounding_box| is
  // a window relative value of |field|.
  void OnQueryFormFieldAutofill(int query_id,
                                const FormData& form,
                                const FormFieldData& field,
                                const gfx::RectF& bounding_box,
                                bool autoselect_first_suggestion);

  // Invoked when |form|'s |field| has focus.
  void OnFocusOnFormField(const FormData& form,
                          const FormFieldData& field,
                          const gfx::RectF& bounding_box);

  // Invoked when |form| has been submitted.
  // Processes the submitted |form|, saving any new Autofill data to the user's
  // personal profile.
  void OnFormSubmitted(const FormData& form,
                       bool known_success,
                       mojom::SubmissionSource source);

  // Invoked when |forms| has been detected.
  void OnFormsSeen(const std::vector<FormData>& forms);

  // Invoked when focus is no longer on form. |had_interacted_form| indicates
  // whether focus was previously on a form with which the user had interacted.
  virtual void OnFocusNoLongerOnForm(bool had_interacted_form) = 0;

  // Invoked when |form| has been filled with the value given by
  // SendFormDataToRenderer.
  virtual void OnDidFillAutofillFormData(const FormData& form,
                                         const base::TimeTicks timestamp) = 0;

  // Invoked when preview autofill value has been shown.
  virtual void OnDidPreviewAutofillFormData() = 0;

  // Invoked when textfeild editing ended
  virtual void OnDidEndTextFieldEditing() = 0;

  // Invoked when popup window should be hidden.
  virtual void OnHidePopup() = 0;

  // Invoked when the options of a select element in the |form| changed.
  virtual void SelectFieldOptionsDidChange(const FormData& form) = 0;

  // Invoked when the field type predictions are downloaded from the autofill
  // server.
  virtual void PropagateAutofillPredictions(
      content::RenderFrameHost* rfh,
      const std::vector<FormStructure*>& forms) = 0;

  // Resets cache.
  virtual void Reset();

  // translate::TranslateDriver::LanguageDetectionObserver:
  void OnTranslateDriverDestroyed(
      translate::TranslateDriver* translate_driver) override;
  // Invoked when the language has been detected by the Translate component.
  // As this usually happens after Autofill has parsed the forms for the first
  // time, the heuristics need to be re-run by this function in order to run
  // use language-specific patterns.
  void OnLanguageDetermined(
      const translate::LanguageDetectionDetails& details) override;

  // Send the form |data| to renderer for the specified |action|.
  void SendFormDataToRenderer(int query_id,
                              AutofillDriver::RendererFormDataAction action,
                              const FormData& data);

  // Fills |form_structure| and |autofill_field| with the cached elements
  // corresponding to |form| and |field|.  This might have the side-effect of
  // updating the cache.  Returns false if the |form| is not autofillable, or if
  // it is not already present in the cache and the cache is full.
  bool GetCachedFormAndField(const FormData& form,
                             const FormFieldData& field,
                             FormStructure** form_structure,
                             AutofillField** autofill_field) WARN_UNUSED_RESULT;

  // Returns nullptr if no cached form structure is found with a matching
  // |form_id|. Runs in logarithmic time.
  FormStructure* FindCachedFormByRendererId(FormGlobalId form_id) const;

  // Returns the number of forms this Autofill handler is aware of.
  size_t NumFormsDetected() const { return form_structures_.size(); }

  void SetEventObserverForTesting(ObserverForTest* observer) {
    observer_for_testing_ = observer;
  }

  // Returns the present form structures seen by Autofill handler.
  const std::map<FormGlobalId, std::unique_ptr<FormStructure>>&
  form_structures() const {
    return form_structures_;
  }

  AutofillDriver* driver() { return driver_; }

  AutofillDownloadManager* download_manager() {
    return download_manager_.get();
  }

  // The return value shouldn't be cached, retrieve it as needed.
  AutofillMetrics::FormInteractionsUkmLogger* form_interactions_ukm_logger() {
    return form_interactions_ukm_logger_.get();
  }

  // A public wrapper that calls |OnLoadedServerPredictions| for testing
  // purposes only, it is used by WebView integration test and unit test, so it
  // can't be in #ifdef UNIT_TEST.
  void OnLoadedServerPredictionsForTest(
      std::string response,
      const std::vector<FormSignature>& queried_form_signatures) {
    OnLoadedServerPredictions(response, queried_form_signatures);
  }
  void OnServerRequestErrorForTest(
      FormSignature form_signature,
      AutofillDownloadManager::RequestType request_type,
      int http_error) {
    OnServerRequestError(form_signature, request_type, http_error);
  }
#ifdef UNIT_TEST
  // A public wrapper that calls |mutable_form_structures| for testing purposes
  // only.
  std::map<FormGlobalId, std::unique_ptr<FormStructure>>*
  mutable_form_structures_for_test() {
    return mutable_form_structures();
  }

  // A public wrapper that calls |ParseForm| for testing purposes only.
  FormStructure* ParseFormForTest(const FormData& form) {
    return ParseForm(form, nullptr);
  }

#endif  // UNIT_TEST

 protected:
  AutofillHandler(AutofillDriver* driver,
                  AutofillClient* client,
                  AutofillDownloadManagerState enable_download_manager);
  AutofillHandler(AutofillDriver* driver,
                  AutofillClient* client,
                  AutofillDownloadManagerState enable_download_manager,
                  version_info::Channel channel);

  LogManager* log_manager() { return log_manager_; }

  // Retrieves the page language from |client_|
  LanguageCode GetCurrentPageLanguage() const;

  virtual void OnFormSubmittedImpl(const FormData& form,
                                   bool known_success,
                                   mojom::SubmissionSource source) = 0;

  virtual void OnTextFieldDidChangeImpl(const FormData& form,
                                        const FormFieldData& field,
                                        const gfx::RectF& bounding_box,
                                        const base::TimeTicks timestamp) = 0;

  virtual void OnTextFieldDidScrollImpl(const FormData& form,
                                        const FormFieldData& field,
                                        const gfx::RectF& bounding_box) = 0;

  virtual void OnQueryFormFieldAutofillImpl(
      int query_id,
      const FormData& form,
      const FormFieldData& field,
      const gfx::RectF& bounding_box,
      bool autoselect_first_suggestion) = 0;

  virtual void OnFocusOnFormFieldImpl(const FormData& form,
                                      const FormFieldData& field,
                                      const gfx::RectF& bounding_box) = 0;

  virtual void OnSelectControlDidChangeImpl(const FormData& form,
                                            const FormFieldData& field,
                                            const gfx::RectF& bounding_box) = 0;

  // Return whether the |forms| from OnFormSeen() should be parsed to
  // form_structures.
  virtual bool ShouldParseForms(const std::vector<FormData>& forms) = 0;

  // Invoked before parsing the forms.
  virtual void OnBeforeProcessParsedForms() = 0;

  // Invoked when the given |form| has been processed to the given
  // |form_structure|.
  virtual void OnFormProcessed(const FormData& form,
                               const FormStructure& form_structure) = 0;
  // Invoked after all forms have been processed, |form_types| is a set of
  // FormType found.
  virtual void OnAfterProcessParsedForms(
      const DenseSet<FormType>& form_types) = 0;

  // Returns the number of FormStructures with the given |form_signature| and
  // appends them to |form_structures|. Runs in linear time.
  size_t FindCachedFormsBySignature(
      FormSignature form_signature,
      std::vector<FormStructure*>* form_structures) const;

  // Parses the |form| with the server data retrieved from the |cached_form|
  // (if any). Returns nullptr if the form should not be parsed. Otherwise, adds
  // the returned form structure to the |form_structures_|.
  FormStructure* ParseForm(const FormData& form,
                           const FormStructure* cached_form);

  bool value_from_dynamic_change_form_ = false;

  std::map<FormGlobalId, std::unique_ptr<FormStructure>>*
  mutable_form_structures() {
    return &form_structures_;
  }

#ifdef UNIT_TEST
  // Exposed for testing.
  void set_download_manager_for_test(
      std::unique_ptr<AutofillDownloadManager> manager) {
    download_manager_ = std::move(manager);
  }

  // Exposed for testing.
  bool is_rich_query_enabled() const { return is_rich_query_enabled_; }
#endif  // UNIT_TEST

 private:
  // AutofillDownloadManager::Observer:
  void OnLoadedServerPredictions(
      std::string response,
      const std::vector<FormSignature>& queried_form_signatures) override;
  void OnServerRequestError(FormSignature form_signature,
                            AutofillDownloadManager::RequestType request_type,
                            int http_error) override;

  // Invoked when forms from OnFormsSeen() have been parsed to
  // |form_structures|.
  void OnFormsParsed(const std::vector<const FormData*>& forms);

  void PropagateAutofillPredictionsToDriver(
      const std::vector<FormStructure*>& forms);

  std::unique_ptr<AutofillMetrics::FormInteractionsUkmLogger>
  CreateFormInteractionsUkmLogger();

  // Provides driver-level context to the shared code of the component. Must
  // outlive this object.
  AutofillDriver* const driver_;

  AutofillClient* const client_;

  LogManager* const log_manager_;

  // Observer needed to re-run heuristics when the language has been detected.
  base::ScopedObservation<
      translate::TranslateDriver,
      translate::TranslateDriver::LanguageDetectionObserver,
      &translate::TranslateDriver::AddLanguageDetectionObserver,
      &translate::TranslateDriver::RemoveLanguageDetectionObserver>
      translate_observation_{this};

  // Our copy of the form data.
  std::map<FormGlobalId, std::unique_ptr<FormStructure>> form_structures_;

  // Handles queries and uploads to Autofill servers. Will be nullptr if
  // the download manager functionality is disabled.
  std::unique_ptr<AutofillDownloadManager> download_manager_;

  // Utility for logging URL keyed metrics.
  std::unique_ptr<AutofillMetrics::FormInteractionsUkmLogger>
      form_interactions_ukm_logger_;

  // Tracks whether or not rich query encoding is enabled for this client.
  const bool is_rich_query_enabled_ = false;

  // Task to delay propagate the query result to driver for testing.
  base::CancelableOnceCallback<void(const std::vector<FormStructure*>&)>
      query_result_delay_task_;

  // Will be not null only for |SaveCardBubbleViewsFullFormBrowserTest|.
  ObserverForTest* observer_for_testing_ = nullptr;

  DISALLOW_COPY_AND_ASSIGN(AutofillHandler);
};

}  // namespace autofill

#endif  // COMPONENTS_AUTOFILL_CORE_BROWSER_AUTOFILL_HANDLER_H_
