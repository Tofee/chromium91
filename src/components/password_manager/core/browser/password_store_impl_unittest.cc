// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/password_store_impl.h"

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/run_loop.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "components/os_crypt/os_crypt_mocker.h"
#include "components/password_manager/core/browser/login_database.h"
#include "components/password_manager/core/browser/password_manager_test_utils.h"
#include "components/password_manager/core/browser/password_store_change.h"
#include "components/password_manager/core/browser/password_store_consumer.h"
#include "components/password_manager/core/browser/password_store_origin_unittest.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::ElementsAre;
using testing::ElementsAreArray;
using testing::IsEmpty;

namespace password_manager {

namespace {

class MockPasswordStoreConsumer : public PasswordStoreConsumer {
 public:
  MOCK_METHOD1(OnGetPasswordStoreResultsConstRef,
               void(const std::vector<std::unique_ptr<PasswordForm>>&));

  // GMock cannot mock methods with move-only args.
  void OnGetPasswordStoreResults(
      std::vector<std::unique_ptr<PasswordForm>> results) override {
    OnGetPasswordStoreResultsConstRef(results);
  }
};

// A mock LoginDatabase that simulates a failing Init() method.
class BadLoginDatabase : public LoginDatabase {
 public:
  BadLoginDatabase() : LoginDatabase(base::FilePath(), IsAccountStore(false)) {}

  // LoginDatabase:
  bool Init() override { return false; }

 private:
  DISALLOW_COPY_AND_ASSIGN(BadLoginDatabase);
};

PasswordFormData CreateTestPasswordFormData() {
  PasswordFormData data = {PasswordForm::Scheme::kHtml,
                           "http://bar.example.com",
                           "http://bar.example.com/origin",
                           "http://bar.example.com/action",
                           u"submit_element",
                           u"username_element",
                           u"password_element",
                           u"username_value",
                           u"password_value",
                           true,
                           1};
  return data;
}

class PasswordStoreImplTestDelegate {
 public:
  PasswordStoreImplTestDelegate();
  explicit PasswordStoreImplTestDelegate(
      std::unique_ptr<LoginDatabase> database);
  ~PasswordStoreImplTestDelegate();

  PasswordStoreImpl* store() { return store_.get(); }

  void FinishAsyncProcessing();

 private:
  void SetupTempDir();

  void ClosePasswordStore();

  scoped_refptr<PasswordStoreImpl> CreateInitializedStore(
      std::unique_ptr<LoginDatabase> database);

  base::FilePath test_login_db_file_path() const;

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::UI};
  base::ScopedTempDir temp_dir_;
  TestingPrefServiceSimple prefs_;
  scoped_refptr<PasswordStoreImpl> store_;

  DISALLOW_COPY_AND_ASSIGN(PasswordStoreImplTestDelegate);
};

PasswordStoreImplTestDelegate::PasswordStoreImplTestDelegate() {
  OSCryptMocker::SetUp();
  SetupTempDir();
  store_ = CreateInitializedStore(std::make_unique<LoginDatabase>(
      test_login_db_file_path(), IsAccountStore(false)));
}

PasswordStoreImplTestDelegate::PasswordStoreImplTestDelegate(
    std::unique_ptr<LoginDatabase> database) {
  OSCryptMocker::SetUp();
  SetupTempDir();
  store_ = CreateInitializedStore(std::move(database));
}

PasswordStoreImplTestDelegate::~PasswordStoreImplTestDelegate() {
  ClosePasswordStore();
  OSCryptMocker::TearDown();
}

void PasswordStoreImplTestDelegate::FinishAsyncProcessing() {
  task_environment_.RunUntilIdle();
}

void PasswordStoreImplTestDelegate::SetupTempDir() {
  ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
}

void PasswordStoreImplTestDelegate::ClosePasswordStore() {
  store_->ShutdownOnUIThread();
  FinishAsyncProcessing();
  ASSERT_TRUE(temp_dir_.Delete());
}

scoped_refptr<PasswordStoreImpl>
PasswordStoreImplTestDelegate::CreateInitializedStore(
    std::unique_ptr<LoginDatabase> database) {
  scoped_refptr<PasswordStoreImpl> store(
      new PasswordStoreImpl(std::move(database)));
  store->Init(&prefs_);

  return store;
}

base::FilePath PasswordStoreImplTestDelegate::test_login_db_file_path() const {
  return temp_dir_.GetPath().Append(FILE_PATH_LITERAL("login_test"));
}

}  // anonymous namespace

INSTANTIATE_TYPED_TEST_SUITE_P(Default,
                               PasswordStoreOriginTest,
                               PasswordStoreImplTestDelegate);

TEST(PasswordStoreImplTest, NonASCIIData) {
  PasswordStoreImplTestDelegate delegate;
  PasswordStoreImpl* store = delegate.store();

  // Some non-ASCII password form data.
  static const PasswordFormData form_data[] = {
      {PasswordForm::Scheme::kHtml, "http://foo.example.com",
       "http://foo.example.com/origin", "http://foo.example.com/action",
       u"มีสีสัน", u"お元気ですか?", u"盆栽", u"أحب كرة", u"£éä국수çà", true, 1},
  };

  // Build the expected forms vector and add the forms to the store.
  std::vector<std::unique_ptr<PasswordForm>> expected_forms;
  for (const auto& data : form_data) {
    expected_forms.push_back(FillPasswordFormWithData(data));
    store->AddLogin(*expected_forms.back());
  }

  MockPasswordStoreConsumer consumer;

  // We expect to get the same data back, even though it's not all ASCII.
  EXPECT_CALL(
      consumer,
      OnGetPasswordStoreResultsConstRef(
          password_manager::UnorderedPasswordFormElementsAre(&expected_forms)));
  store->GetAutofillableLogins(&consumer);

  delegate.FinishAsyncProcessing();
}

TEST(PasswordStoreImplTest, Notifications) {
  PasswordStoreImplTestDelegate delegate;
  PasswordStoreImpl* store = delegate.store();

  std::unique_ptr<PasswordForm> form =
      FillPasswordFormWithData(CreateTestPasswordFormData());

  MockPasswordStoreObserver observer;
  store->AddObserver(&observer);

  const PasswordStoreChange expected_add_changes[] = {
      PasswordStoreChange(PasswordStoreChange::ADD, *form),
  };

  EXPECT_CALL(observer,
              OnLoginsChanged(ElementsAreArray(expected_add_changes)));

  // Adding a login should trigger a notification.
  store->AddLogin(*form);

  // Change the password.
  form->password_value = u"a different password";

  const PasswordStoreChange expected_update_changes[] = {
      PasswordStoreChange(PasswordStoreChange::UPDATE, *form),
  };

  EXPECT_CALL(observer,
              OnLoginsChanged(ElementsAreArray(expected_update_changes)));

  // Updating the login with the new password should trigger a notification.
  store->UpdateLogin(*form);

  const PasswordStoreChange expected_delete_changes[] = {
      PasswordStoreChange(PasswordStoreChange::REMOVE, *form),
  };

  EXPECT_CALL(observer,
              OnLoginsChanged(ElementsAreArray(expected_delete_changes)));

  // Deleting the login should trigger a notification.
  store->RemoveLogin(*form);
  // Run the tasks to allow all the above expected calls to take place.
  delegate.FinishAsyncProcessing();

  store->RemoveObserver(&observer);
}

// Verify that operations on a PasswordStore with a bad database cause no
// explosions, but fail without side effect, return no data and trigger no
// notifications.
TEST(PasswordStoreImplTest, OperationsOnABadDatabaseSilentlyFail) {
  PasswordStoreImplTestDelegate delegate(std::make_unique<BadLoginDatabase>());
  PasswordStoreImpl* bad_store = delegate.store();
  delegate.FinishAsyncProcessing();
  ASSERT_EQ(nullptr, bad_store->login_db());

  testing::StrictMock<MockPasswordStoreObserver> mock_observer;
  bad_store->AddObserver(&mock_observer);

  // Add a new autofillable login + a blocked login.
  std::unique_ptr<PasswordForm> form =
      FillPasswordFormWithData(CreateTestPasswordFormData());
  std::unique_ptr<PasswordForm> blocked_form(new PasswordForm(*form));
  blocked_form->signon_realm = "http://foo.example.com";
  blocked_form->url = GURL("http://foo.example.com/origin");
  blocked_form->action = GURL("http://foo.example.com/action");
  blocked_form->blocked_by_user = true;
  bad_store->AddLogin(*form);
  bad_store->AddLogin(*blocked_form);
  delegate.FinishAsyncProcessing();

  // Get all logins; autofillable logins; blocked logins.
  testing::StrictMock<MockPasswordStoreConsumer> mock_consumer;
  EXPECT_CALL(mock_consumer, OnGetPasswordStoreResultsConstRef(IsEmpty()));
  bad_store->GetLogins(PasswordStore::FormDigest(*form), &mock_consumer);
  delegate.FinishAsyncProcessing();
  testing::Mock::VerifyAndClearExpectations(&mock_consumer);
  EXPECT_CALL(mock_consumer, OnGetPasswordStoreResultsConstRef(IsEmpty()));
  bad_store->GetAutofillableLogins(&mock_consumer);
  delegate.FinishAsyncProcessing();
  testing::Mock::VerifyAndClearExpectations(&mock_consumer);
  EXPECT_CALL(mock_consumer, OnGetPasswordStoreResultsConstRef(IsEmpty()));
  bad_store->GetAllLogins(&mock_consumer);
  delegate.FinishAsyncProcessing();
  testing::Mock::VerifyAndClearExpectations(&mock_consumer);

  // Report metrics.
  bad_store->ReportMetrics("Test Username", true, false);
  delegate.FinishAsyncProcessing();

  // Change the login.
  form->password_value = u"a different password";
  bad_store->UpdateLogin(*form);
  delegate.FinishAsyncProcessing();

  // Delete one login; a range of logins.
  bad_store->RemoveLogin(*form);
  delegate.FinishAsyncProcessing();
  base::RunLoop run_loop;
  bad_store->RemoveLoginsCreatedBetween(base::Time(), base::Time::Max(),
                                        run_loop.QuitClosure());
  run_loop.Run();
  delegate.FinishAsyncProcessing();

  // Ensure no notifications and no explosions during shutdown either.
  bad_store->RemoveObserver(&mock_observer);
}

}  // namespace password_manager
