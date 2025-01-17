// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/autofill/content/browser/content_autofill_driver.h"

#include <stdint.h>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "components/autofill/core/browser/autofill_external_delegate.h"
#include "components/autofill/core/browser/autofill_manager.h"
#include "components/autofill/core/browser/autofill_test_utils.h"
#include "components/autofill/core/browser/test_autofill_client.h"
#include "components/autofill/core/common/autofill_switches.h"
#include "components/autofill/core/common/form_data_predictions.h"
#include "components/version_info/version_info.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/ssl_status.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/mock_navigation_handle.h"
#include "content/public/test/test_renderer_host.h"
#include "mojo/public/cpp/bindings/associated_receiver_set.h"
#include "net/base/net_errors.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"

namespace autofill {

namespace {

const char kAppLocale[] = "en-US";
const AutofillManager::AutofillDownloadManagerState kDownloadState =
    AutofillManager::DISABLE_AUTOFILL_DOWNLOAD_MANAGER;

class FakeAutofillAgent : public mojom::AutofillAgent {
 public:
  FakeAutofillAgent()
      : fill_form_id_(-1),
        preview_form_id_(-1),
        called_clear_section_(false),
        called_clear_previewed_form_(false) {}

  ~FakeAutofillAgent() override {}

  void BindPendingReceiver(mojo::ScopedInterfaceEndpointHandle handle) {
    receivers_.Add(this, mojo::PendingAssociatedReceiver<mojom::AutofillAgent>(
                             std::move(handle)));
  }

  void SetQuitLoopClosure(base::OnceClosure closure) {
    quit_closure_ = std::move(closure);
  }

  // Returns the id and formdata received via
  // mojo interface method mojom::AutofillAgent::FillForm().
  bool GetAutofillFillFormMessage(int* page_id, FormData* results) {
    if (fill_form_id_ == -1)
      return false;
    if (!fill_form_form_)
      return false;

    if (page_id)
      *page_id = fill_form_id_;
    if (results)
      *results = *fill_form_form_;
    return true;
  }

  // Returns the id and formdata received via
  // mojo interface method mojom::AutofillAgent::PreviewForm().
  bool GetAutofillPreviewFormMessage(int* page_id, FormData* results) {
    if (preview_form_id_ == -1)
      return false;
    if (!preview_form_form_)
      return false;

    if (page_id)
      *page_id = preview_form_id_;
    if (results)
      *results = *preview_form_form_;
    return true;
  }

  // Returns data received via mojo interface method
  // mojom::AutofillAent::FieldTypePredictionsAvailable().
  bool GetFieldTypePredictionsAvailable(
      std::vector<FormDataPredictions>* predictions) {
    if (!predictions_)
      return false;
    if (predictions)
      *predictions = *predictions_;
    return true;
  }

  // Returns whether mojo interface method mojom::AutofillAgent::ClearForm() got
  // called.
  bool GetCalledClearSection() { return called_clear_section_; }

  // Returns whether mojo interface method
  // mojom::AutofillAgent::ClearPreviewedForm() got called.
  bool GetCalledClearPreviewedForm() { return called_clear_previewed_form_; }

  // Returns data received via mojo interface method
  // mojom::AutofillAent::FillFieldWithValue().
  bool GetString16FillFieldWithValue(const FieldGlobalId& field,
                                     std::u16string* value) {
    if (!value_fill_field_ || value_renderer_id_ != field.renderer_id)
      return false;
    if (value)
      *value = *value_fill_field_;
    return true;
  }

  // Returns data received via mojo interface method
  // mojom::AutofillAent::PreviewFieldWithValue().
  bool GetString16PreviewFieldWithValue(const FieldGlobalId field,
                                        std::u16string* value) {
    if (!value_preview_field_ || value_renderer_id_ != field.renderer_id)
      return false;
    if (value)
      *value = *value_preview_field_;
    return true;
  }

  // Returns data received via mojo interface method
  // mojom::AutofillAent::AcceptDataListSuggestion().
  bool GetString16AcceptDataListSuggestion(FieldGlobalId field,
                                           std::u16string* value) {
    if (!value_accept_data_ || value_renderer_id_ != field.renderer_id)
      return false;
    if (value)
      *value = *value_accept_data_;
    return true;
  }

  // Mocked mojom::AutofillAgent methods:
  MOCK_METHOD0(FirstUserGestureObservedInTab, void());
  MOCK_METHOD0(EnableHeavyFormDataScraping, void());

 private:
  void CallDone() {
    if (!quit_closure_.is_null())
      std::move(quit_closure_).Run();
  }

  // mojom::AutofillAgent:
  void FillForm(int32_t id, const FormData& form) override {
    fill_form_id_ = id;
    fill_form_form_ = form;
    CallDone();
  }

  void PreviewForm(int32_t id, const FormData& form) override {
    preview_form_id_ = id;
    preview_form_form_ = form;
    CallDone();
  }

  void FieldTypePredictionsAvailable(
      const std::vector<FormDataPredictions>& forms) override {
    predictions_ = forms;
    CallDone();
  }

  void ClearSection() override {
    called_clear_section_ = true;
    CallDone();
  }

  void ClearPreviewedForm() override {
    called_clear_previewed_form_ = true;
    CallDone();
  }

  void FillFieldWithValue(FieldRendererId field,
                          const std::u16string& value) override {
    value_renderer_id_ = field;
    value_fill_field_ = value;
    CallDone();
  }

  void PreviewFieldWithValue(FieldRendererId field,
                             const std::u16string& value) override {
    value_renderer_id_ = field;
    value_preview_field_ = value;
    CallDone();
  }

  void SetSuggestionAvailability(FieldRendererId field,
                                 const mojom::AutofillState state) override {
    value_renderer_id_ = field;
    if (state == mojom::AutofillState::kAutofillAvailable)
      suggestions_available_ = true;
    else if (state == mojom::AutofillState::kNoSuggestions)
      suggestions_available_ = false;
    CallDone();
  }

  void AcceptDataListSuggestion(FieldRendererId field,
                                const std::u16string& value) override {
    value_renderer_id_ = field;
    value_accept_data_ = value;
    CallDone();
  }

  void FillPasswordSuggestion(const std::u16string& username,
                              const std::u16string& password) override {}

  void PreviewPasswordSuggestion(const std::u16string& username,
                                 const std::u16string& password) override {}

  void SetUserGestureRequired(bool required) override {}

  void SetSecureContextRequired(bool required) override {}

  void SetFocusRequiresScroll(bool require) override {}

  void SetQueryPasswordSuggestion(bool query) override {}

  void GetElementFormAndFieldData(
      const std::vector<std::string>& selectors,
      GetElementFormAndFieldDataCallback callback) override {}

  void SetAssistantActionState(bool running) override {}

  mojo::AssociatedReceiverSet<mojom::AutofillAgent> receivers_;

  base::OnceClosure quit_closure_;

  // Records data received from FillForm() call.
  int32_t fill_form_id_;
  base::Optional<FormData> fill_form_form_;
  // Records data received from PreviewForm() call.
  int32_t preview_form_id_;
  base::Optional<FormData> preview_form_form_;
  // Records data received from FieldTypePredictionsAvailable() call.
  base::Optional<std::vector<FormDataPredictions>> predictions_;
  // Records whether ClearSection() got called.
  bool called_clear_section_;
  // Records whether ClearPreviewedForm() got called.
  bool called_clear_previewed_form_;
  // Records the ID received from FillFieldWithValue(), PreviewFieldWithValue(),
  // SetSuggestionAvailability(), or AcceptDataListSuggestion().
  base::Optional<FieldRendererId> value_renderer_id_;
  // Records string received from FillFieldWithValue() call.
  base::Optional<std::u16string> value_fill_field_;
  // Records string received from PreviewFieldWithValue() call.
  base::Optional<std::u16string> value_preview_field_;
  // Records string received from AcceptDataListSuggestion() call.
  base::Optional<std::u16string> value_accept_data_;
  // Records bool received from SetSuggestionAvailability() call.
  bool suggestions_available_;
};

}  // namespace

class MockAutofillManager : public AutofillManager {
 public:
  MockAutofillManager(AutofillDriver* driver, AutofillClient* client)
      : AutofillManager(driver, client, kAppLocale, kDownloadState) {}
  ~MockAutofillManager() override {}

  MOCK_METHOD0(Reset, void());
};

class MockAutofillClient : public TestAutofillClient {
 public:
  MOCK_METHOD0(OnFirstUserGestureObserved, void());
};

class TestContentAutofillDriver : public ContentAutofillDriver {
 public:
  TestContentAutofillDriver(content::RenderFrameHost* rfh,
                            AutofillClient* client)
      : ContentAutofillDriver(rfh,
                              client,
                              kAppLocale,
                              kDownloadState,
                              nullptr) {
    std::unique_ptr<AutofillManager> autofill_manager(
        new MockAutofillManager(this, client));
    SetAutofillManager(std::move(autofill_manager));
  }
  ~TestContentAutofillDriver() override {}

  virtual MockAutofillManager* mock_autofill_manager() {
    return static_cast<MockAutofillManager*>(autofill_manager());
  }

  using ContentAutofillDriver::DidNavigateFrame;
};

class ContentAutofillDriverTest : public content::RenderViewHostTestHarness {
 public:
  void SetUp() override {
    content::RenderViewHostTestHarness::SetUp();
    // This needed to keep the WebContentsObserverConsistencyChecker checks
    // happy for when AppendChild is called.
    NavigateAndCommit(GURL("about:blank"));

    test_autofill_client_ = std::make_unique<MockAutofillClient>();
    driver_ = std::make_unique<TestContentAutofillDriver>(
        web_contents()->GetMainFrame(), test_autofill_client_.get());

    blink::AssociatedInterfaceProvider* remote_interfaces =
        web_contents()->GetMainFrame()->GetRemoteAssociatedInterfaces();
    remote_interfaces->OverrideBinderForTesting(
        mojom::AutofillAgent::Name_,
        base::BindRepeating(&FakeAutofillAgent::BindPendingReceiver,
                            base::Unretained(&fake_agent_)));
  }

  void TearDown() override {
    // Reset the driver now to cause all pref observers to be removed and avoid
    // crashes that otherwise occur in the destructor.
    driver_.reset();
    content::RenderViewHostTestHarness::TearDown();
  }

  void Navigate(bool same_document, bool from_bfcache = false) {
    content::MockNavigationHandle navigation_handle(GURL(), main_rfh());
    navigation_handle.set_has_committed(true);
    navigation_handle.set_is_same_document(same_document);
    navigation_handle.set_is_served_from_bfcache(from_bfcache);
    driver_->DidNavigateFrame(&navigation_handle);
  }

 protected:
  std::unique_ptr<MockAutofillClient> test_autofill_client_;
  std::unique_ptr<TestContentAutofillDriver> driver_;

  FakeAutofillAgent fake_agent_;
};

TEST_F(ContentAutofillDriverTest, NavigatedMainFrameDifferentDocument) {
  EXPECT_CALL(*driver_->mock_autofill_manager(), Reset());
  Navigate(/*same_document=*/false);
}

TEST_F(ContentAutofillDriverTest, NavigatedMainFrameSameDocument) {
  EXPECT_CALL(*driver_->mock_autofill_manager(), Reset()).Times(0);
  Navigate(/*same_document=*/true);
}

TEST_F(ContentAutofillDriverTest, NavigatedMainFrameFromBackForwardCache) {
  EXPECT_CALL(*driver_->mock_autofill_manager(), Reset()).Times(0);
  Navigate(/*same_document=*/false, /*from_bfcache=*/true);
}

TEST_F(ContentAutofillDriverTest, FormDataSentToRenderer_FillForm) {
  int input_page_id = 42;
  FormData input_form_data;
  test::CreateTestAddressFormData(&input_form_data);
  base::RunLoop run_loop;
  fake_agent_.SetQuitLoopClosure(run_loop.QuitClosure());
  driver_->SendFormDataToRenderer(
      input_page_id, AutofillDriver::FORM_DATA_ACTION_FILL, input_form_data);

  run_loop.RunUntilIdle();

  int output_page_id = 0;
  FormData output_form_data;
  EXPECT_FALSE(fake_agent_.GetAutofillPreviewFormMessage(&output_page_id,
                                                         &output_form_data));
  EXPECT_TRUE(fake_agent_.GetAutofillFillFormMessage(&output_page_id,
                                                     &output_form_data));
  EXPECT_EQ(input_page_id, output_page_id);
  EXPECT_TRUE(input_form_data.SameFormAs(output_form_data));
}

TEST_F(ContentAutofillDriverTest, FormDataSentToRenderer_PreviewForm) {
  int input_page_id = 42;
  FormData input_form_data;
  test::CreateTestAddressFormData(&input_form_data);
  base::RunLoop run_loop;
  fake_agent_.SetQuitLoopClosure(run_loop.QuitClosure());
  driver_->SendFormDataToRenderer(
      input_page_id, AutofillDriver::FORM_DATA_ACTION_PREVIEW, input_form_data);

  run_loop.RunUntilIdle();

  int output_page_id = 0;
  FormData output_form_data;
  EXPECT_FALSE(fake_agent_.GetAutofillFillFormMessage(&output_page_id,
                                                      &output_form_data));
  EXPECT_TRUE(fake_agent_.GetAutofillPreviewFormMessage(&output_page_id,
                                                        &output_form_data));
  EXPECT_EQ(input_page_id, output_page_id);
  EXPECT_TRUE(input_form_data.SameFormAs(output_form_data));
}

TEST_F(ContentAutofillDriverTest, TypePredictionsSentToRendererWhenEnabled) {
  base::CommandLine::ForCurrentProcess()->AppendSwitch(
      switches::kShowAutofillTypePredictions);

  FormData form;
  test::CreateTestAddressFormData(&form);
  FormStructure form_structure(form);
  std::vector<FormStructure*> forms(1, &form_structure);
  std::vector<FormDataPredictions> expected_type_predictions =
      FormStructure::GetFieldTypePredictions(forms);

  base::RunLoop run_loop;
  fake_agent_.SetQuitLoopClosure(run_loop.QuitClosure());
  driver_->SendAutofillTypePredictionsToRenderer(forms);
  run_loop.RunUntilIdle();

  std::vector<FormDataPredictions> output_type_predictions;
  EXPECT_TRUE(
      fake_agent_.GetFieldTypePredictionsAvailable(&output_type_predictions));
  EXPECT_EQ(expected_type_predictions, output_type_predictions);
}

TEST_F(ContentAutofillDriverTest, AcceptDataListSuggestion) {
  FieldGlobalId field = test::MakeFieldGlobalId();
  std::u16string input_value(u"barfoo");
  std::u16string output_value;

  base::RunLoop run_loop;
  fake_agent_.SetQuitLoopClosure(run_loop.QuitClosure());
  driver_->RendererShouldAcceptDataListSuggestion(field, input_value);
  run_loop.RunUntilIdle();

  EXPECT_TRUE(
      fake_agent_.GetString16AcceptDataListSuggestion(field, &output_value));
  EXPECT_EQ(input_value, output_value);
}

TEST_F(ContentAutofillDriverTest, ClearFilledSectionSentToRenderer) {
  base::RunLoop run_loop;
  fake_agent_.SetQuitLoopClosure(run_loop.QuitClosure());
  driver_->RendererShouldClearFilledSection();
  run_loop.RunUntilIdle();

  EXPECT_TRUE(fake_agent_.GetCalledClearSection());
}

TEST_F(ContentAutofillDriverTest, ClearPreviewedFormSentToRenderer) {
  base::RunLoop run_loop;
  fake_agent_.SetQuitLoopClosure(run_loop.QuitClosure());
  driver_->RendererShouldClearPreviewedForm();
  run_loop.RunUntilIdle();

  EXPECT_TRUE(fake_agent_.GetCalledClearPreviewedForm());
}

TEST_F(ContentAutofillDriverTest, FillFieldWithValue) {
  FieldGlobalId field = test::MakeFieldGlobalId();
  std::u16string input_value(u"barqux");
  std::u16string output_value;

  base::RunLoop run_loop;
  fake_agent_.SetQuitLoopClosure(run_loop.QuitClosure());
  driver_->RendererShouldFillFieldWithValue(field, input_value);
  run_loop.RunUntilIdle();

  EXPECT_TRUE(fake_agent_.GetString16FillFieldWithValue(field, &output_value));
  EXPECT_EQ(input_value, output_value);
}

TEST_F(ContentAutofillDriverTest, PreviewFieldWithValue) {
  FieldGlobalId field = test::MakeFieldGlobalId();
  std::u16string input_value(u"barqux");
  std::u16string output_value;

  base::RunLoop run_loop;
  fake_agent_.SetQuitLoopClosure(run_loop.QuitClosure());
  driver_->RendererShouldPreviewFieldWithValue(field, input_value);
  run_loop.RunUntilIdle();

  EXPECT_TRUE(
      fake_agent_.GetString16PreviewFieldWithValue(field, &output_value));
  EXPECT_EQ(input_value, output_value);
}

TEST_F(ContentAutofillDriverTest, EnableHeavyFormDataScraping) {
  struct TestCase {
    version_info::Channel channel;
    bool heavy_scraping_enabled;
  } kTestCases[] = {{version_info::Channel::CANARY, true},
                    {version_info::Channel::DEV, true},
                    {version_info::Channel::UNKNOWN, false},
                    {version_info::Channel::BETA, false},
                    {version_info::Channel::STABLE, false}};

  for (auto test_case : kTestCases) {
    SCOPED_TRACE(testing::Message()
                 << "channel: "
                 << version_info::GetChannelString(test_case.channel));
    test_autofill_client_->set_channel_for_testing(test_case.channel);
    EXPECT_CALL(fake_agent_, EnableHeavyFormDataScraping())
        .Times(test_case.heavy_scraping_enabled ? 1 : 0);

    std::unique_ptr<ContentAutofillDriver> driver(new TestContentAutofillDriver(
        web_contents()->GetMainFrame(), test_autofill_client_.get()));

    base::RunLoop().RunUntilIdle();
    testing::Mock::VerifyAndClearExpectations(&fake_agent_);
  }
}

}  // namespace autofill
