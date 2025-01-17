// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/account_manager_core/account_manager_facade_impl.h"

#include <limits>
#include <memory>

#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/gmock_callback_support.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/mock_callback.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "chromeos/crosapi/mojom/account_manager.mojom.h"
#include "components/account_manager_core/account.h"
#include "components/account_manager_core/account_addition_result.h"
#include "components/account_manager_core/account_manager_facade.h"
#include "components/account_manager_core/account_manager_test_util.h"
#include "components/account_manager_core/account_manager_util.h"
#include "google_apis/gaia/oauth2_access_token_consumer.h"
#include "google_apis/gaia/oauth2_access_token_fetcher.h"
#include "mojo/public/cpp/bindings/pending_remote.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/receiver_set.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "mojo/public/cpp/bindings/remote_set.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace account_manager {

namespace {

using base::MockOnceCallback;
using ::testing::_;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Invoke;
using ::testing::WithArgs;

constexpr char kTestAccountEmail[] = "test@gmail.com";
constexpr char kAnotherTestAccountEmail[] = "another_test@gmail.com";
constexpr char kFakeOAuthConsumerName[] = "fake-oauth-consumer-name";
constexpr char kFakeClientId[] = "fake-client-id";
constexpr char kFakeClientSecret[] = "fake-client-secret";
constexpr char kFakeAccessToken[] = "fake-access-token";
constexpr char kFakeIdToken[] = "fake-id-token";

void AccessTokenFetchSuccess(
    base::OnceCallback<void(crosapi::mojom::AccessTokenResultPtr)> callback) {
  crosapi::mojom::AccessTokenInfoPtr access_token_info =
      crosapi::mojom::AccessTokenInfo::New(kFakeAccessToken, base::Time::Now(),
                                           kFakeIdToken);
  crosapi::mojom::AccessTokenResultPtr result =
      crosapi::mojom::AccessTokenResult::NewAccessTokenInfo(
          std::move(access_token_info));
  std::move(callback).Run(std::move(result));
}

void AccessTokenFetchServiceError(
    base::OnceCallback<void(crosapi::mojom::AccessTokenResultPtr)> callback) {
  crosapi::mojom::AccessTokenResultPtr result =
      crosapi::mojom::AccessTokenResult::NewError(
          account_manager::ToMojoGoogleServiceAuthError(
              GoogleServiceAuthError(GoogleServiceAuthError::SERVICE_ERROR)));
  std::move(callback).Run(std::move(result));
}

class MockAccessTokenFetcher : public crosapi::mojom::AccessTokenFetcher {
 public:
  MockAccessTokenFetcher() : receiver_(this) {}
  MockAccessTokenFetcher(const MockAccessTokenFetcher&) = delete;
  MockAccessTokenFetcher& operator=(const MockAccessTokenFetcher&) = delete;
  ~MockAccessTokenFetcher() override = default;

  void Bind(
      mojo::PendingReceiver<crosapi::mojom::AccessTokenFetcher> receiver) {
    receiver_.Bind(std::move(receiver));
  }

  // crosapi::mojom::AccessTokenFetcher override.
  MOCK_METHOD(void,
              Start,
              (const std::vector<std::string>& scopes, StartCallback callback),
              (override));

 private:
  mojo::Receiver<crosapi::mojom::AccessTokenFetcher> receiver_;
};

class MockOAuthConsumer : public OAuth2AccessTokenConsumer {
 public:
  MockOAuthConsumer() = default;
  MockOAuthConsumer(const MockOAuthConsumer&) = delete;
  MockOAuthConsumer& operator=(const MockOAuthConsumer&) = delete;
  ~MockOAuthConsumer() override = default;

  // OAuth2AccessTokenConsumer overrides.
  MOCK_METHOD(void,
              OnGetTokenSuccess,
              (const TokenResponse& token_response),
              (override));
  MOCK_METHOD(void,
              OnGetTokenFailure,
              (const GoogleServiceAuthError& error),
              (override));
};

class FakeAccountManager : public crosapi::mojom::AccountManager {
 public:
  FakeAccountManager() = default;
  FakeAccountManager(const FakeAccountManager&) = delete;
  FakeAccountManager& operator=(const FakeAccountManager&) = delete;
  ~FakeAccountManager() override = default;

  void IsInitialized(IsInitializedCallback cb) override {
    std::move(cb).Run(is_initialized_);
  }

  void SetIsInitialized(bool is_initialized) {
    is_initialized_ = is_initialized;
  }

  void AddObserver(AddObserverCallback cb) override {
    mojo::Remote<crosapi::mojom::AccountManagerObserver> observer;
    std::move(cb).Run(observer.BindNewPipeAndPassReceiver());
    observers_.Add(std::move(observer));
  }

  void GetAccounts(GetAccountsCallback callback) override {
    std::vector<crosapi::mojom::AccountPtr> mojo_accounts;
    std::transform(std::begin(accounts_), std::end(accounts_),
                   std::back_inserter(mojo_accounts), &ToMojoAccount);
    std::move(callback).Run(std::move(mojo_accounts));
  }

  void GetPersistentErrorForAccount(
      crosapi::mojom::AccountKeyPtr mojo_account_key,
      GetPersistentErrorForAccountCallback callback) override {
    base::Optional<AccountKey> account_key =
        FromMojoAccountKey(mojo_account_key);
    DCHECK(account_key.has_value());
    auto it = persistent_errors_.find(account_key.value());
    if (it != persistent_errors_.end()) {
      std::move(callback).Run(ToMojoGoogleServiceAuthError(it->second));
      return;
    }
    std::move(callback).Run(
        ToMojoGoogleServiceAuthError(GoogleServiceAuthError::AuthErrorNone()));
  }

  void ShowAddAccountDialog(ShowAddAccountDialogCallback callback) override {
    show_add_account_dialog_calls_++;
    std::move(callback).Run(
        account_manager::ToMojoAccountAdditionResult(add_account_result_));
  }

  void ShowReauthAccountDialog(const std::string& email,
                               base::OnceClosure closure) override {
    show_reauth_account_dialog_calls_++;
    std::move(closure).Run();
  }

  void ShowManageAccountsSettings() override {
    show_manage_accounts_settings_calls_++;
  }

  void SetMockAccessTokenFetcher(
      std::unique_ptr<MockAccessTokenFetcher> mock_access_token_fetcher) {
    access_token_fetcher_ = std::move(mock_access_token_fetcher);
  }

  void CreateAccessTokenFetcher(
      crosapi::mojom::AccountKeyPtr mojo_account_key,
      const std::string& oauth_consumer_name,
      CreateAccessTokenFetcherCallback callback) override {
    if (!access_token_fetcher_)
      access_token_fetcher_ = std::make_unique<MockAccessTokenFetcher>();
    mojo::PendingRemote<crosapi::mojom::AccessTokenFetcher> pending_remote;
    access_token_fetcher_->Bind(
        pending_remote.InitWithNewPipeAndPassReceiver());
    std::move(callback).Run(std::move(pending_remote));
  }

  mojo::Remote<crosapi::mojom::AccountManager> CreateRemote() {
    mojo::Remote<crosapi::mojom::AccountManager> remote;
    receivers_.Add(this, remote.BindNewPipeAndPassReceiver());
    return remote;
  }

  void NotifyOnTokenUpsertedObservers(const Account& account) {
    for (auto& observer : observers_) {
      observer->OnTokenUpserted(ToMojoAccount(account));
    }
  }

  void NotifyOnAccountRemovedObservers(const Account& account) {
    for (auto& observer : observers_) {
      observer->OnAccountRemoved(ToMojoAccount(account));
    }
  }

  void SetAccounts(const std::vector<Account>& accounts) {
    accounts_ = accounts;
  }

  void SetPersistentErrorForAccount(const AccountKey& account,
                                    GoogleServiceAuthError error) {
    persistent_errors_.emplace(account, error);
  }

  void SetAccountAdditionResult(
      const account_manager::AccountAdditionResult& result) {
    add_account_result_ = result;
  }

  void ClearReceivers() { receivers_.Clear(); }

  int show_add_account_dialog_calls() const {
    return show_add_account_dialog_calls_;
  }

  int show_reauth_account_dialog_calls() const {
    return show_reauth_account_dialog_calls_;
  }

  int show_manage_accounts_settings_calls() const {
    return show_manage_accounts_settings_calls_;
  }

 private:
  int show_add_account_dialog_calls_ = 0;
  int show_reauth_account_dialog_calls_ = 0;
  int show_manage_accounts_settings_calls_ = 0;
  bool is_initialized_ = false;
  std::vector<Account> accounts_;
  std::map<AccountKey, GoogleServiceAuthError> persistent_errors_;
  AccountAdditionResult add_account_result_{
      AccountAdditionResult::Status::kUnexpectedResponse};
  std::unique_ptr<MockAccessTokenFetcher> access_token_fetcher_;
  mojo::ReceiverSet<crosapi::mojom::AccountManager> receivers_;
  mojo::RemoteSet<crosapi::mojom::AccountManagerObserver> observers_;
};

class MockObserver : public AccountManagerFacade::Observer {
 public:
  MockObserver() = default;
  MockObserver(const MockObserver&) = delete;
  MockObserver& operator=(const MockObserver&) = delete;
  ~MockObserver() override = default;

  MOCK_METHOD(void, OnAccountUpserted, (const Account& account), (override));
  MOCK_METHOD(void, OnAccountRemoved, (const Account& account), (override));
};

MATCHER_P(AccountEq, expected_account, "") {
  return testing::ExplainMatchResult(
             testing::Field(&Account::key, testing::Eq(expected_account.key)),
             arg, result_listener) &&
         testing::ExplainMatchResult(
             testing::Field(&Account::raw_email,
                            testing::StrEq(expected_account.raw_email)),
             arg, result_listener);
}

}  // namespace

class AccountManagerFacadeImplTest : public testing::Test {
 public:
  AccountManagerFacadeImplTest() = default;
  AccountManagerFacadeImplTest(const AccountManagerFacadeImplTest&) = delete;
  AccountManagerFacadeImplTest& operator=(const AccountManagerFacadeImplTest&) =
      delete;
  ~AccountManagerFacadeImplTest() override = default;

 protected:
  FakeAccountManager& account_manager() { return account_manager_; }

  std::unique_ptr<AccountManagerFacadeImpl> CreateFacade() {
    base::RunLoop run_loop;
    auto result = std::make_unique<AccountManagerFacadeImpl>(
        account_manager().CreateRemote(),
        /* remote_version= */ std::numeric_limits<uint32_t>::max(),
        run_loop.QuitClosure());
    run_loop.Run();
    return result;
  }

 private:
  base::test::SingleThreadTaskEnvironment task_environment_;
  FakeAccountManager account_manager_;
};

TEST_F(AccountManagerFacadeImplTest, InitializationStatusIsCorrectlySet) {
  // This will wait for an initialization callback to be called.
  std::unique_ptr<AccountManagerFacadeImpl> account_manager_facade =
      CreateFacade();
  EXPECT_TRUE(account_manager_facade->IsInitialized());
}

TEST_F(AccountManagerFacadeImplTest, OnTokenUpsertedIsPropagatedToObservers) {
  std::unique_ptr<AccountManagerFacadeImpl> account_manager_facade =
      CreateFacade();
  testing::StrictMock<MockObserver> observer;
  account_manager_facade->AddObserver(&observer);

  Account account = CreateTestGaiaAccount(kTestAccountEmail);
  base::RunLoop run_loop;
  EXPECT_CALL(observer, OnAccountUpserted(AccountEq(account)))
      .WillOnce(base::test::RunClosure(run_loop.QuitClosure()));
  account_manager().NotifyOnTokenUpsertedObservers(account);
  run_loop.Run();
}

TEST_F(AccountManagerFacadeImplTest, OnAccountRemovedIsPropagatedToObservers) {
  std::unique_ptr<AccountManagerFacadeImpl> account_manager_facade =
      CreateFacade();
  testing::StrictMock<MockObserver> observer;
  account_manager_facade->AddObserver(&observer);

  Account account = CreateTestGaiaAccount(kTestAccountEmail);
  base::RunLoop run_loop;
  EXPECT_CALL(observer, OnAccountRemoved(AccountEq(account)))
      .WillOnce(base::test::RunClosure(run_loop.QuitClosure()));
  account_manager().NotifyOnAccountRemovedObservers(account);
  run_loop.Run();
}

TEST_F(AccountManagerFacadeImplTest,
       GetAccountsReturnsEmptyListOfAccountsWhenAccountManagerAshIsEmpty) {
  std::unique_ptr<AccountManagerFacadeImpl> account_manager_facade =
      CreateFacade();
  account_manager().SetAccounts({});

  MockOnceCallback<void(const std::vector<Account>&)> callback;
  base::RunLoop run_loop;
  EXPECT_CALL(callback, Run(testing::IsEmpty()))
      .WillOnce(base::test::RunClosure(run_loop.QuitClosure()));
  account_manager_facade->GetAccounts(callback.Get());
  run_loop.Run();
}

TEST_F(AccountManagerFacadeImplTest, GetAccountsCorrectlyMarshalsTwoAccounts) {
  std::unique_ptr<AccountManagerFacadeImpl> account_manager_facade =
      CreateFacade();
  Account account1 = CreateTestGaiaAccount(kTestAccountEmail);
  Account account2 = CreateTestGaiaAccount(kAnotherTestAccountEmail);
  account_manager().SetAccounts({account1, account2});

  MockOnceCallback<void(const std::vector<Account>&)> callback;
  base::RunLoop run_loop;
  EXPECT_CALL(callback, Run(testing::ElementsAre(AccountEq(account1),
                                                 AccountEq(account2))))
      .WillOnce(base::test::RunClosure(run_loop.QuitClosure()));
  account_manager_facade->GetAccounts(callback.Get());
  run_loop.Run();
}

TEST_F(AccountManagerFacadeImplTest,
       GetAccountsIsSafeToCallBeforeAccountManagerFacadeIsInitialized) {
  Account account = CreateTestGaiaAccount(kTestAccountEmail);
  account_manager().SetAccounts({account});

  // |CreateFacade| waits for the AccountManagerFacadeImpl's initialization
  // sequence to be finished. To avoid this, create it directly here.
  auto account_manager_facade = std::make_unique<AccountManagerFacadeImpl>(
      account_manager().CreateRemote(),
      /* remote_version= */ std::numeric_limits<uint32_t>::max());

  MockOnceCallback<void(const std::vector<Account>&)> callback;
  base::RunLoop run_loop;
  EXPECT_CALL(callback, Run(testing::ElementsAre(AccountEq(account))))
      .WillOnce(base::test::RunClosure(run_loop.QuitClosure()));
  account_manager_facade->GetAccounts(callback.Get());
  run_loop.Run();
}

TEST_F(AccountManagerFacadeImplTest,
       GetAccountsReturnsEmptyListOfAccountsWhenRemoteIsNull) {
  auto account_manager_facade = std::make_unique<AccountManagerFacadeImpl>(
      mojo::Remote<crosapi::mojom::AccountManager>(),
      /* remote_version= */ std::numeric_limits<uint32_t>::max());

  MockOnceCallback<void(const std::vector<Account>&)> callback;
  base::RunLoop run_loop;
  EXPECT_CALL(callback, Run(testing::IsEmpty()))
      .WillOnce(base::test::RunClosure(run_loop.QuitClosure()));
  account_manager_facade->GetAccounts(callback.Get());
  run_loop.Run();
}

TEST_F(AccountManagerFacadeImplTest, GetPersistentErrorMarshalsAuthErrorNone) {
  std::unique_ptr<AccountManagerFacadeImpl> account_manager_facade =
      CreateFacade();
  Account account = CreateTestGaiaAccount(kTestAccountEmail);

  MockOnceCallback<void(const GoogleServiceAuthError&)> callback;
  base::RunLoop run_loop;
  EXPECT_CALL(callback, Run(GoogleServiceAuthError::AuthErrorNone()))
      .WillOnce(base::test::RunClosure(run_loop.QuitClosure()));
  account_manager_facade->GetPersistentErrorForAccount(account.key,
                                                       callback.Get());
  run_loop.Run();
}

TEST_F(AccountManagerFacadeImplTest,
       GetPersistentErrorMarshalsCredentialsRejectedByClient) {
  std::unique_ptr<AccountManagerFacadeImpl> account_manager_facade =
      CreateFacade();
  Account account = CreateTestGaiaAccount(kTestAccountEmail);
  GoogleServiceAuthError error =
      GoogleServiceAuthError::FromInvalidGaiaCredentialsReason(
          GoogleServiceAuthError::InvalidGaiaCredentialsReason::
              CREDENTIALS_REJECTED_BY_CLIENT);
  account_manager().SetPersistentErrorForAccount(account.key, error);

  MockOnceCallback<void(const GoogleServiceAuthError&)> callback;
  base::RunLoop run_loop;
  EXPECT_CALL(callback, Run(error))
      .WillOnce(base::test::RunClosure(run_loop.QuitClosure()));
  account_manager_facade->GetPersistentErrorForAccount(account.key,
                                                       callback.Get());
  run_loop.Run();
}

TEST_F(AccountManagerFacadeImplTest, ShowAddAccountDialogCallsMojo) {
  std::unique_ptr<AccountManagerFacadeImpl> account_manager_facade =
      CreateFacade();
  EXPECT_EQ(0, account_manager().show_add_account_dialog_calls());
  account_manager_facade->ShowAddAccountDialog(
      account_manager::AccountManagerFacade::AccountAdditionSource::
          kSettingsAddAccountButton);
  account_manager_facade->FlushMojoForTesting();
  EXPECT_EQ(1, account_manager().show_add_account_dialog_calls());
}

TEST_F(AccountManagerFacadeImplTest, ShowAddAccountDialogUMA) {
  base::HistogramTester tester;
  std::unique_ptr<AccountManagerFacadeImpl> account_manager_facade =
      CreateFacade();
  auto result = account_manager::AccountAdditionResult(
      account_manager::AccountAdditionResult::Status::kAlreadyInProgress);
  account_manager().SetAccountAdditionResult(result);
  auto source = account_manager::AccountManagerFacade::AccountAdditionSource::
      kSettingsAddAccountButton;

  account_manager_facade->ShowAddAccountDialog(source);
  account_manager_facade->FlushMojoForTesting();

  // Check that UMA stats were sent.
  tester.ExpectUniqueSample(
      account_manager::AccountManagerFacade::kAccountAdditionSource,
      /*sample=*/source, /*expected_count=*/1);
  tester.ExpectUniqueSample(
      AccountManagerFacadeImpl::
          GetAccountAdditionResultStatusHistogramNameForTesting(),
      /*sample=*/result.status, /*expected_count=*/1);
}

TEST_F(AccountManagerFacadeImplTest, ShowReauthAccountDialogCallsMojo) {
  std::unique_ptr<AccountManagerFacadeImpl> account_manager_facade =
      CreateFacade();
  EXPECT_EQ(0, account_manager().show_reauth_account_dialog_calls());
  account_manager_facade->ShowReauthAccountDialog(
      account_manager::AccountManagerFacade::AccountAdditionSource::
          kSettingsAddAccountButton,
      kTestAccountEmail);
  account_manager_facade->FlushMojoForTesting();
  EXPECT_EQ(1, account_manager().show_reauth_account_dialog_calls());
}

TEST_F(AccountManagerFacadeImplTest, ShowReauthAccountDialogUMA) {
  base::HistogramTester tester;
  std::unique_ptr<AccountManagerFacadeImpl> account_manager_facade =
      CreateFacade();
  auto source = AccountManagerFacade::AccountAdditionSource::kContentArea;

  account_manager_facade->ShowReauthAccountDialog(source, kTestAccountEmail);
  account_manager_facade->FlushMojoForTesting();

  // Check that UMA stats were sent.
  tester.ExpectUniqueSample(AccountManagerFacade::kAccountAdditionSource,
                            /*sample=*/source, /*expected_count=*/1);
}

TEST_F(AccountManagerFacadeImplTest, ShowManageAccountsSettingsCallsMojo) {
  std::unique_ptr<AccountManagerFacadeImpl> account_manager_facade =
      CreateFacade();
  EXPECT_EQ(0, account_manager().show_manage_accounts_settings_calls());
  account_manager_facade->ShowManageAccountsSettings();
  account_manager_facade->FlushMojoForTesting();
  EXPECT_EQ(1, account_manager().show_manage_accounts_settings_calls());
}

TEST_F(AccountManagerFacadeImplTest,
       AccessTokenFetcherReturnsAnErrorForUninitializedRemote) {
  auto account_manager_facade = std::make_unique<AccountManagerFacadeImpl>(
      mojo::Remote<crosapi::mojom::AccountManager>(),
      /*remote_version=*/std::numeric_limits<uint32_t>::max());
  const Account account = CreateTestGaiaAccount(kTestAccountEmail);

  MockOAuthConsumer consumer;
  GoogleServiceAuthError error(GoogleServiceAuthError::SERVICE_ERROR);
  EXPECT_CALL(consumer, OnGetTokenFailure(Eq(error)));

  std::unique_ptr<OAuth2AccessTokenFetcher> access_token_fetcher =
      account_manager_facade->CreateAccessTokenFetcher(
          account.key, kFakeOAuthConsumerName, &consumer);

  access_token_fetcher->Start(kFakeClientId, kFakeClientSecret, /*scopes=*/{});
  base::RunLoop().RunUntilIdle();
}

TEST_F(AccountManagerFacadeImplTest,
       AccessTokenFetcherCanBeCreatedBeforeAccountManagerFacadeInitialization) {
  auto account_manager_facade = std::make_unique<AccountManagerFacadeImpl>(
      account_manager().CreateRemote(),
      /*remote_version=*/std::numeric_limits<uint32_t>::max());
  const Account account = CreateTestGaiaAccount(kTestAccountEmail);

  auto mock_access_token_fetcher = std::make_unique<MockAccessTokenFetcher>();
  EXPECT_CALL(*mock_access_token_fetcher.get(), Start(_, _))
      .WillOnce(WithArgs<1>(Invoke(&AccessTokenFetchSuccess)));
  account_manager().SetMockAccessTokenFetcher(
      std::move(mock_access_token_fetcher));
  MockOAuthConsumer consumer;

  std::unique_ptr<OAuth2AccessTokenFetcher> access_token_fetcher =
      account_manager_facade->CreateAccessTokenFetcher(
          account.key, kFakeOAuthConsumerName, &consumer);
  EXPECT_FALSE(account_manager_facade->IsInitialized());
  access_token_fetcher->Start(kFakeClientId, kFakeClientSecret, /*scopes=*/{});
  EXPECT_CALL(consumer,
              OnGetTokenSuccess(
                  Field(&OAuth2AccessTokenConsumer::TokenResponse::access_token,
                        Eq(kFakeAccessToken))));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(account_manager_facade->IsInitialized());
}

TEST_F(AccountManagerFacadeImplTest,
       AccessTokenFetcherCanHandleMojoRemoteDisconnection) {
  account_manager().SetIsInitialized(true);
  std::unique_ptr<AccountManagerFacadeImpl> account_manager_facade =
      CreateFacade();
  const Account account = CreateTestGaiaAccount(kTestAccountEmail);

  MockOAuthConsumer consumer;
  GoogleServiceAuthError error(GoogleServiceAuthError::SERVICE_ERROR);
  EXPECT_CALL(consumer, OnGetTokenFailure(Eq(error)));

  std::unique_ptr<OAuth2AccessTokenFetcher> access_token_fetcher =
      account_manager_facade->CreateAccessTokenFetcher(
          account.key, kFakeOAuthConsumerName, &consumer);
  access_token_fetcher->Start(kFakeClientId, kFakeClientSecret, /*scopes=*/{});
  account_manager().ClearReceivers();
  base::RunLoop().RunUntilIdle();
}

TEST_F(AccountManagerFacadeImplTest, AccessTokenFetchSucceeds) {
  account_manager().SetIsInitialized(true);
  std::unique_ptr<AccountManagerFacadeImpl> account_manager_facade =
      CreateFacade();
  const Account account = CreateTestGaiaAccount(kTestAccountEmail);

  auto mock_access_token_fetcher = std::make_unique<MockAccessTokenFetcher>();
  EXPECT_CALL(*mock_access_token_fetcher.get(), Start(_, _))
      .WillOnce(WithArgs<1>(Invoke(&AccessTokenFetchSuccess)));
  account_manager().SetMockAccessTokenFetcher(
      std::move(mock_access_token_fetcher));
  MockOAuthConsumer consumer;
  EXPECT_CALL(consumer,
              OnGetTokenSuccess(
                  Field(&OAuth2AccessTokenConsumer::TokenResponse::access_token,
                        Eq(kFakeAccessToken))));

  std::unique_ptr<OAuth2AccessTokenFetcher> access_token_fetcher =
      account_manager_facade->CreateAccessTokenFetcher(
          account.key, kFakeOAuthConsumerName, &consumer);
  access_token_fetcher->Start(kFakeClientId, kFakeClientSecret, /*scopes=*/{});
  base::RunLoop().RunUntilIdle();
}

TEST_F(AccountManagerFacadeImplTest, AccessTokenFetchErrorResponse) {
  account_manager().SetIsInitialized(true);
  std::unique_ptr<AccountManagerFacadeImpl> account_manager_facade =
      CreateFacade();
  const Account account = CreateTestGaiaAccount(kTestAccountEmail);

  auto mock_access_token_fetcher = std::make_unique<MockAccessTokenFetcher>();
  EXPECT_CALL(*mock_access_token_fetcher.get(), Start(_, _))
      .WillOnce(WithArgs<1>(Invoke(&AccessTokenFetchServiceError)));
  account_manager().SetMockAccessTokenFetcher(
      std::move(mock_access_token_fetcher));
  MockOAuthConsumer consumer;
  GoogleServiceAuthError error(GoogleServiceAuthError::SERVICE_ERROR);
  EXPECT_CALL(consumer, OnGetTokenFailure(Eq(error)));

  std::unique_ptr<OAuth2AccessTokenFetcher> access_token_fetcher =
      account_manager_facade->CreateAccessTokenFetcher(
          account.key, kFakeOAuthConsumerName, &consumer);
  access_token_fetcher->Start(kFakeClientId, kFakeClientSecret, /*scopes=*/{});
  base::RunLoop().RunUntilIdle();
}

}  // namespace account_manager
