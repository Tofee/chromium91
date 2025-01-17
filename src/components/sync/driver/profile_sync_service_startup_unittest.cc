// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/driver/profile_sync_service.h"

#include "base/bind.h"
#include "base/test/task_environment.h"
#include "build/chromeos_buildflags.h"
#include "components/prefs/pref_service.h"
#include "components/signin/public/identity_manager/identity_test_environment.h"
#include "components/sync/base/pref_names.h"
#include "components/sync/driver/data_type_manager_impl.h"
#include "components/sync/driver/fake_data_type_controller.h"
#include "components/sync/driver/fake_sync_api_component_factory.h"
#include "components/sync/driver/profile_sync_service_bundle.h"
#include "components/sync/driver/sync_client_mock.h"
#include "components/sync/driver/sync_driver_switches.h"
#include "components/sync/test/engine/fake_sync_engine.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::ByMove;
using testing::Return;

namespace syncer {

namespace {

const char kEmail[] = "test_user@gmail.com";

class MockSyncServiceObserver : public SyncServiceObserver {
 public:
  MockSyncServiceObserver() = default;

  MOCK_METHOD(void, OnStateChanged, (SyncService*), (override));
};

}  // namespace

class ProfileSyncServiceStartupTest : public testing::Test {
 public:
  ProfileSyncServiceStartupTest()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME),
        sync_prefs_(profile_sync_service_bundle_.pref_service()) {
    profile_sync_service_bundle_.identity_test_env()
        ->SetAutomaticIssueOfAccessTokens(true);
  }

  ~ProfileSyncServiceStartupTest() override { sync_service_->Shutdown(); }

  void CreateSyncService(
      ProfileSyncService::StartBehavior start_behavior,
      ModelTypeSet registered_types = ModelTypeSet(BOOKMARKS)) {
    DataTypeController::TypeVector controllers;
    for (ModelType type : registered_types) {
      auto controller = std::make_unique<FakeDataTypeController>(type);
      // Hold a raw pointer to directly interact with the controller.
      controller_map_[type] = controller.get();
      controllers.push_back(std::move(controller));
    }

    std::unique_ptr<SyncClientMock> sync_client =
        profile_sync_service_bundle_.CreateSyncClientMock();
    ON_CALL(*sync_client, CreateDataTypeControllers)
        .WillByDefault(Return(ByMove(std::move(controllers))));

    sync_service_ = std::make_unique<ProfileSyncService>(
        profile_sync_service_bundle_.CreateBasicInitParams(
            start_behavior, std::move(sync_client)));
  }

  void SimulateTestUserSignin() {
    profile_sync_service_bundle_.identity_test_env()
        ->MakePrimaryAccountAvailable(kEmail);
  }

  void SimulateTestUserSigninWithoutRefreshToken() {
    // Set the primary account *without* providing an OAuth token.
    profile_sync_service_bundle_.identity_test_env()->SetPrimaryAccount(kEmail);
  }

  void UpdateCredentials() {
    profile_sync_service_bundle_.identity_test_env()
        ->SetRefreshTokenForPrimaryAccount();
  }

  // Sets a special invalid refresh token. This is what happens when the primary
  // (and sync-consented) account signs out on the web.
  void SimulateWebSignout() {
    profile_sync_service_bundle_.identity_test_env()
        ->SetInvalidRefreshTokenForPrimaryAccount();
  }

  void DisableAutomaticIssueOfAccessTokens() {
    profile_sync_service_bundle_.identity_test_env()
        ->SetAutomaticIssueOfAccessTokens(false);
  }

  void RespondToTokenRequest() {
    profile_sync_service_bundle_.identity_test_env()
        ->WaitForAccessTokenRequestIfNecessaryAndRespondWithToken(
            "access_token", base::Time::Max());
  }

  SyncPrefs* sync_prefs() { return &sync_prefs_; }

  ProfileSyncService* sync_service() { return sync_service_.get(); }

  PrefService* pref_service() {
    return profile_sync_service_bundle_.pref_service();
  }

  FakeSyncApiComponentFactory* component_factory() {
    return profile_sync_service_bundle_.component_factory();
  }

  DataTypeManagerImpl* data_type_manager() {
    return component_factory()->last_created_data_type_manager();
  }

  FakeSyncEngine* engine() {
    return component_factory()->last_created_engine();
  }

  FakeDataTypeController* get_controller(ModelType type) {
    return controller_map_[type];
  }

  void FastForwardUntilNoTasksRemain() {
    task_environment_.FastForwardUntilNoTasksRemain();
  }

 private:
  base::test::TaskEnvironment task_environment_;
  ProfileSyncServiceBundle profile_sync_service_bundle_;
  SyncPrefs sync_prefs_;
  std::unique_ptr<ProfileSyncService> sync_service_;
  // The controllers are owned by |sync_service_|.
  std::map<ModelType, FakeDataTypeController*> controller_map_;
};

// ChromeOS does not support sign-in after startup
#if !BUILDFLAG(IS_CHROMEOS_ASH)
TEST_F(ProfileSyncServiceStartupTest, StartFirstTime) {
  // We've never completed startup.
  ASSERT_FALSE(sync_prefs()->IsFirstSetupComplete());

  CreateSyncService(ProfileSyncService::MANUAL_START);

  // Should not actually start, rather just clean things up and wait
  // to be enabled.
  sync_service()->Initialize();
  EXPECT_EQ(
      SyncService::DisableReasonSet(SyncService::DISABLE_REASON_NOT_SIGNED_IN,
                                    SyncService::DISABLE_REASON_USER_CHOICE),
      sync_service()->GetDisableReasons());
  EXPECT_EQ(SyncService::TransportState::DISABLED,
            sync_service()->GetTransportState());
  EXPECT_EQ(nullptr, data_type_manager());
  EXPECT_FALSE(engine());

  // Preferences should be back to defaults.
  EXPECT_EQ(base::Time(), sync_service()->GetLastSyncedTimeForDebugging());
  EXPECT_FALSE(sync_prefs()->IsFirstSetupComplete());

  // This tells the ProfileSyncService that setup is now in progress, which
  // causes it to try starting up the engine. We're not signed in yet though, so
  // that won't work.
  sync_service()->GetUserSettings()->SetSyncRequested(true);
  auto sync_blocker = sync_service()->GetSetupInProgressHandle();
  EXPECT_FALSE(sync_service()->IsEngineInitialized());
  EXPECT_EQ(
      SyncService::DisableReasonSet(SyncService::DISABLE_REASON_NOT_SIGNED_IN),
      sync_service()->GetDisableReasons());
  EXPECT_EQ(SyncService::TransportState::DISABLED,
            sync_service()->GetTransportState());

  SimulateTestUserSignin();

  // Now we're signed in, so the engine can start. Engine initialization is
  // immediate in this test, so we bypass the INITIALIZING state.
  EXPECT_TRUE(sync_service()->IsEngineInitialized());
  EXPECT_EQ(SyncService::DisableReasonSet(),
            sync_service()->GetDisableReasons());
  EXPECT_EQ(SyncService::TransportState::PENDING_DESIRED_CONFIGURATION,
            sync_service()->GetTransportState());

  // Simulate the UI telling sync it has finished setting up. Note that this is
  // a two-step process: Releasing the SetupInProgressHandle, and marking first
  // setup complete.
  // Since standalone transport is enabled, completed first-time setup is not a
  // requirement, so the service will start up as soon as the setup handle is
  // released.
  sync_blocker.reset();
  ASSERT_FALSE(sync_service()->IsSetupInProgress());
  EXPECT_EQ(DataTypeManager::CONFIGURED, data_type_manager()->state());
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            sync_service()->GetTransportState());
  // Sync-the-feature is still not active, but rather pending confirmation.
  EXPECT_FALSE(sync_service()->IsSyncFeatureEnabled());
  EXPECT_FALSE(sync_service()->IsSyncFeatureActive());

  // Marking first setup complete will let ProfileSyncService reconfigure the
  // DataTypeManager in full Sync-the-feature mode.
  sync_service()->GetUserSettings()->SetFirstSetupComplete(
      syncer::SyncFirstSetupCompleteSource::BASIC_FLOW);
  EXPECT_EQ(DataTypeManager::CONFIGURED, data_type_manager()->state());

  // This should have fully enabled sync.
  EXPECT_TRUE(sync_service()->IsSyncFeatureEnabled());
  EXPECT_TRUE(sync_service()->IsSyncFeatureActive());
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            sync_service()->GetTransportState());
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

TEST_F(ProfileSyncServiceStartupTest, StartNoCredentials) {
  // We're already signed in, but don't have a refresh token.
  SimulateTestUserSigninWithoutRefreshToken();
  sync_prefs()->SetFirstSetupComplete();

  CreateSyncService(ProfileSyncService::MANUAL_START);
  sync_service()->Initialize();

  // ProfileSyncService should now be active, but of course not have an access
  // token.
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            sync_service()->GetTransportState());
  EXPECT_TRUE(sync_service()->GetAccessTokenForTest().empty());
  // Note that ProfileSyncService is not in an auth error state - no auth was
  // attempted, so no error.
}

TEST_F(ProfileSyncServiceStartupTest, WebSignoutBeforeInitialization) {
  // There is a primary account, but it's in a "web signout" aka sync-paused
  // state.
  SimulateTestUserSignin();
  SimulateWebSignout();
  sync_prefs()->SetFirstSetupComplete();

  CreateSyncService(ProfileSyncService::MANUAL_START);

  sync_service()->Initialize();

  // ProfileSyncService should now be in the paused state.
  EXPECT_EQ(SyncService::TransportState::PAUSED,
            sync_service()->GetTransportState());
}

TEST_F(ProfileSyncServiceStartupTest, WebSignoutDuringDeferredStartup) {
  // There is a primary account. It is theoretically in the "web signout" aka
  // sync-paused error state, but the identity code hasn't detected that yet
  // (because auth errors are not persisted).
  SimulateTestUserSignin();
  sync_prefs()->SetFirstSetupComplete();

  // Note: Deferred startup is only enabled if SESSIONS is among the preferred
  // data types.
  CreateSyncService(ProfileSyncService::MANUAL_START, {TYPED_URLS, SESSIONS});
  sync_service()->Initialize();

  ASSERT_EQ(SyncService::TransportState::START_DEFERRED,
            sync_service()->GetTransportState());

  MockSyncServiceObserver observer;
  sync_service()->AddObserver(&observer);

  // Entering the sync-paused state should trigger a notification.
  EXPECT_CALL(observer, OnStateChanged(sync_service())).WillOnce([&]() {
    EXPECT_EQ(SyncService::TransportState::PAUSED,
              sync_service()->GetTransportState());
  });

  // Now sign out on the web to enter the sync-paused state.
  SimulateWebSignout();

  // ProfileSyncService should now be in the paused state.
  EXPECT_EQ(SyncService::TransportState::PAUSED,
            sync_service()->GetTransportState());

  sync_service()->RemoveObserver(&observer);
}

TEST_F(ProfileSyncServiceStartupTest, WebSignoutAfterInitialization) {
  // This test has to wait for the access token request to complete, so disable
  // automatic issuing of tokens.
  DisableAutomaticIssueOfAccessTokens();

  SimulateTestUserSignin();
  sync_prefs()->SetFirstSetupComplete();

  CreateSyncService(ProfileSyncService::MANUAL_START);
  sync_service()->Initialize();

  // Respond to the token request to finish the initialization flow.
  RespondToTokenRequest();

  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            sync_service()->GetTransportState());

  MockSyncServiceObserver observer;
  sync_service()->AddObserver(&observer);

  // Entering the sync-paused state should trigger a notification.
  EXPECT_CALL(observer, OnStateChanged(sync_service())).WillOnce([&]() {
    EXPECT_EQ(SyncService::TransportState::PAUSED,
              sync_service()->GetTransportState());
  });

  // Now sign out on the web to enter the sync-paused state.
  SimulateWebSignout();

  // ProfileSyncService should now be in the paused state.
  EXPECT_EQ(SyncService::TransportState::PAUSED,
            sync_service()->GetTransportState());

  sync_service()->RemoveObserver(&observer);
}

TEST_F(ProfileSyncServiceStartupTest, StartInvalidCredentials) {
  SimulateTestUserSignin();
  sync_prefs()->SetSyncRequested(true);
  sync_prefs()->SetFirstSetupComplete();

  CreateSyncService(ProfileSyncService::MANUAL_START);

  // Prevent automatic (and successful) completion of engine initialization.
  component_factory()->AllowFakeEngineInitCompletion(false);
  sync_service()->Initialize();
  // Simulate an auth error while downloading control types.
  engine()->TriggerInitializationCompletion(/*success=*/false);

  // Engine initialization failures puts the service into an unrecoverable error
  // state. It'll take either a browser restart or a full sign-out+sign-in to
  // get out of this.
  EXPECT_TRUE(sync_service()->HasUnrecoverableError());
  EXPECT_EQ(SyncService::DisableReasonSet(
                SyncService::DISABLE_REASON_UNRECOVERABLE_ERROR),
            sync_service()->GetDisableReasons());
  EXPECT_EQ(SyncService::TransportState::DISABLED,
            sync_service()->GetTransportState());
}

TEST_F(ProfileSyncServiceStartupTest, StartCrosNoCredentials) {
  // We've never completed startup.
  ASSERT_FALSE(sync_prefs()->IsFirstSetupComplete());

  // On ChromeOS, the user is always immediately signed in, but a refresh token
  // isn't necessarily available yet.
  SimulateTestUserSigninWithoutRefreshToken();

  CreateSyncService(ProfileSyncService::AUTO_START);

  // Calling Initialize should cause the service to immediately create and
  // initialize the engine, and configure the DataTypeManager.
  sync_service()->Initialize();
  EXPECT_EQ(DataTypeManager::CONFIGURED, data_type_manager()->state());

  // Sync should be considered active, even though there is no refresh token.
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            sync_service()->GetTransportState());
  // Since we're in AUTO_START mode, FirstSetupComplete gets set automatically.
  EXPECT_TRUE(sync_service()->GetUserSettings()->IsFirstSetupComplete());
}

TEST_F(ProfileSyncServiceStartupTest, StartCrosFirstTime) {
  // On ChromeOS, the user is always immediately signed in, but a refresh token
  // isn't necessarily available yet.
  SimulateTestUserSigninWithoutRefreshToken();

  CreateSyncService(ProfileSyncService::AUTO_START);
  ASSERT_FALSE(sync_prefs()->IsFirstSetupComplete());

  // The primary account is already populated, all that's left to do is provide
  // a refresh token.
  UpdateCredentials();
  sync_service()->Initialize();
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            sync_service()->GetTransportState());
}

TEST_F(ProfileSyncServiceStartupTest, StartNormal) {
  // We have previously completed the initial Sync setup, and the user is
  // already signed in.
  sync_prefs()->SetFirstSetupComplete();
  SimulateTestUserSignin();

  CreateSyncService(ProfileSyncService::MANUAL_START);

  // Since all conditions for starting Sync are already fulfilled, calling
  // Initialize should immediately create and initialize the engine and
  // configure the DataTypeManager. In this test, all of these operations are
  // synchronous.
  sync_service()->Initialize();
  EXPECT_NE(nullptr, data_type_manager());
  EXPECT_EQ(DataTypeManager::CONFIGURED, data_type_manager()->state());
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            sync_service()->GetTransportState());
}

TEST_F(ProfileSyncServiceStartupTest, StopSync) {
  sync_prefs()->SetFirstSetupComplete();
  CreateSyncService(ProfileSyncService::MANUAL_START);
  SimulateTestUserSignin();

  sync_service()->Initialize();
  ASSERT_EQ(DataTypeManager::CONFIGURED, data_type_manager()->state());
  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            sync_service()->GetTransportState());

  // On SetSyncRequested(false), the sync service will immediately start up
  // again in transport mode.
  sync_service()->GetUserSettings()->SetSyncRequested(false);

  // Sync-the-feature is still considered off.
  EXPECT_FALSE(sync_service()->IsSyncFeatureEnabled());
  EXPECT_FALSE(sync_service()->IsSyncFeatureActive());
  EXPECT_EQ(DataTypeManager::CONFIGURED, data_type_manager()->state());
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            sync_service()->GetTransportState());
}

TEST_F(ProfileSyncServiceStartupTest, DisableSync) {
  sync_prefs()->SetSyncRequested(true);
  sync_prefs()->SetFirstSetupComplete();
  SimulateTestUserSignin();
  CreateSyncService(ProfileSyncService::MANUAL_START);

  sync_service()->Initialize();
  ASSERT_TRUE(sync_service()->IsSyncFeatureActive());
  ASSERT_EQ(DataTypeManager::CONFIGURED, data_type_manager()->state());
  ASSERT_EQ(SyncService::TransportState::ACTIVE,
            sync_service()->GetTransportState());

  // On StopAndClear(), the sync service will immediately start up again in
  // transport mode.
  sync_service()->StopAndClear();
  EXPECT_EQ(DataTypeManager::CONFIGURED, data_type_manager()->state());
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            sync_service()->GetTransportState());

  // Sync-the-feature is still considered off.
  EXPECT_FALSE(sync_service()->IsSyncFeatureEnabled());
  EXPECT_FALSE(sync_service()->IsSyncFeatureActive());

  // Call StopAndClear() again while the sync service is already in transport
  // mode. It should immediately start up again in transport mode.
  sync_service()->StopAndClear();
  EXPECT_EQ(DataTypeManager::CONFIGURED, data_type_manager()->state());
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            sync_service()->GetTransportState());
}

// Test that we can recover from a case where a bug in the code resulted in
// OnUserChoseDatatypes not being properly called and datatype preferences
// therefore being left unset.
TEST_F(ProfileSyncServiceStartupTest, StartRecoverDatatypePrefs) {
  // Clear the datatype preference fields (simulating bug 154940).
  pref_service()->ClearPref(prefs::kSyncKeepEverythingSynced);
  for (UserSelectableType type : UserSelectableTypeSet::All()) {
    pref_service()->ClearPref(SyncPrefs::GetPrefNameForType(type));
  }

  sync_prefs()->SetFirstSetupComplete();
  CreateSyncService(ProfileSyncService::MANUAL_START);
  SimulateTestUserSignin();

  sync_service()->Initialize();

  EXPECT_TRUE(sync_prefs()->HasKeepEverythingSynced());
}

// Verify that the recovery of datatype preferences doesn't overwrite a valid
// case where only bookmarks are enabled.
TEST_F(ProfileSyncServiceStartupTest, StartDontRecoverDatatypePrefs) {
  // Explicitly set Keep Everything Synced to false and have only bookmarks
  // enabled.
  sync_prefs()->SetSelectedTypes(
      /*keep_everything_synced=*/false,
      /*choosable_types=*/UserSelectableTypeSet::All(),
      /*chosen_types=*/{UserSelectableType::kBookmarks});

  sync_prefs()->SetFirstSetupComplete();
  CreateSyncService(ProfileSyncService::MANUAL_START);
  SimulateTestUserSignin();

  sync_service()->Initialize();

  EXPECT_FALSE(sync_prefs()->HasKeepEverythingSynced());
}

TEST_F(ProfileSyncServiceStartupTest, ManagedStartup) {
  // Sync was previously enabled, but a policy was set while Chrome wasn't
  // running.
  sync_prefs()->SetManagedForTest(true);
  sync_prefs()->SetSyncRequested(true);
  sync_prefs()->SetFirstSetupComplete();

  SimulateTestUserSignin();
  CreateSyncService(ProfileSyncService::MANUAL_START);

  sync_service()->Initialize();
  // Sync was disabled due to the policy, setting SyncRequested to false and
  // causing DISABLE_REASON_USER_CHOICE.
  EXPECT_EQ(SyncService::DisableReasonSet(
                SyncService::DISABLE_REASON_ENTERPRISE_POLICY,
                SyncService::DISABLE_REASON_USER_CHOICE),
            sync_service()->GetDisableReasons());
  // Service should not be started by Initialize() since it's managed.
  EXPECT_EQ(nullptr, data_type_manager());
  EXPECT_FALSE(engine());
}

TEST_F(ProfileSyncServiceStartupTest, SwitchManaged) {
  // Sync starts out fully set up and enabled.
  sync_prefs()->SetSyncRequested(true);
  sync_prefs()->SetFirstSetupComplete();
  SimulateTestUserSignin();
  CreateSyncService(ProfileSyncService::MANUAL_START);

  // Initialize() should be enough to kick off Sync startup (which is instant in
  // this test).
  sync_service()->Initialize();
  EXPECT_TRUE(sync_service()->IsEngineInitialized());
  EXPECT_EQ(SyncService::DisableReasonSet(),
            sync_service()->GetDisableReasons());
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            sync_service()->GetTransportState());
  EXPECT_TRUE(sync_service()->IsSyncFeatureEnabled());
  EXPECT_TRUE(sync_service()->IsSyncFeatureActive());
  ASSERT_EQ(0, get_controller(BOOKMARKS)->model()->clear_metadata_call_count());

  // The service should stop when switching to managed mode.
  sync_prefs()->SetManagedForTest(true);
  // Sync was disabled due to the policy, setting SyncRequested to false and
  // causing DISABLE_REASON_USER_CHOICE.
  ASSERT_EQ(SyncService::DisableReasonSet(
                SyncService::DISABLE_REASON_ENTERPRISE_POLICY,
                SyncService::DISABLE_REASON_USER_CHOICE),
            sync_service()->GetDisableReasons());
  EXPECT_FALSE(sync_service()->IsEngineInitialized());
  EXPECT_EQ(SyncService::TransportState::DISABLED,
            sync_service()->GetTransportState());
  EXPECT_FALSE(sync_service()->IsSyncFeatureEnabled());
  EXPECT_FALSE(sync_service()->IsSyncFeatureActive());
  EXPECT_EQ(1, get_controller(BOOKMARKS)->model()->clear_metadata_call_count());
  // Note that PSS no longer references |data_type_manager| after stopping.

  // When switching back to unmanaged, Sync-the-transport should start up
  // automatically, which causes (re)creation of SyncEngine and
  // DataTypeManager.
  sync_prefs()->SetManagedForTest(false);

  ASSERT_EQ(
      SyncService::DisableReasonSet(SyncService::DISABLE_REASON_USER_CHOICE),
      sync_service()->GetDisableReasons());

  EXPECT_TRUE(sync_service()->IsEngineInitialized());
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            sync_service()->GetTransportState());
  // Sync-the-feature is still considered off because disabling Sync through
  // policy also reset the sync-requested and first-setup-complete flags.
  EXPECT_FALSE(sync_service()->GetUserSettings()->IsFirstSetupComplete());
  EXPECT_FALSE(sync_service()->IsSyncFeatureEnabled());
  EXPECT_FALSE(sync_service()->IsSyncFeatureActive());
}

TEST_F(ProfileSyncServiceStartupTest, StartDownloadFailed) {
  sync_prefs()->SetSyncRequested(true);
  CreateSyncService(ProfileSyncService::MANUAL_START);
  SimulateTestUserSignin();
  ASSERT_FALSE(sync_prefs()->IsFirstSetupComplete());

  // Prevent automatic (and successful) completion of engine initialization.
  component_factory()->AllowFakeEngineInitCompletion(false);
  sync_service()->Initialize();

  // Simulate a failure while downloading control types.
  engine()->TriggerInitializationCompletion(/*success=*/false);

  auto sync_blocker = sync_service()->GetSetupInProgressHandle();
  sync_blocker.reset();
  EXPECT_EQ(SyncService::DisableReasonSet(
                SyncService::DISABLE_REASON_UNRECOVERABLE_ERROR),
            sync_service()->GetDisableReasons());
  EXPECT_EQ(SyncService::TransportState::DISABLED,
            sync_service()->GetTransportState());
}

// ChromeOS does not support sign-in after startup
#if !BUILDFLAG(IS_CHROMEOS_ASH)
TEST_F(ProfileSyncServiceStartupTest, FullStartupSequenceFirstTime) {
  // We've never completed startup.
  ASSERT_FALSE(sync_prefs()->IsFirstSetupComplete());

  // Note: Deferred startup is only enabled if SESSIONS is among the preferred
  // data types.
  CreateSyncService(ProfileSyncService::MANUAL_START,
                    ModelTypeSet(SESSIONS, TYPED_URLS));
  sync_service()->Initialize();
  ASSERT_FALSE(sync_service()->CanSyncFeatureStart());

  // There is no signed-in user, so also nobody has decided that Sync should be
  // started.
  EXPECT_EQ(
      SyncService::DisableReasonSet(SyncService::DISABLE_REASON_NOT_SIGNED_IN,
                                    SyncService::DISABLE_REASON_USER_CHOICE),
      sync_service()->GetDisableReasons());
  EXPECT_EQ(SyncService::TransportState::DISABLED,
            sync_service()->GetTransportState());

  // Sign in. Now Sync-the-transport can start. Since this was triggered by an
  // explicit user event, deferred startup is bypassed.
  // Sync-the-feature still doesn't start until the user says they want it.
  component_factory()->AllowFakeEngineInitCompletion(false);
  SimulateTestUserSignin();
  EXPECT_EQ(
      SyncService::DisableReasonSet(SyncService::DISABLE_REASON_USER_CHOICE),
      sync_service()->GetDisableReasons());
  EXPECT_EQ(SyncService::TransportState::INITIALIZING,
            sync_service()->GetTransportState());
  EXPECT_FALSE(sync_service()->IsSyncFeatureEnabled());
  EXPECT_TRUE(engine());

  // Initiate Sync (the feature) setup before the engine initializes itself in
  // transport mode.
  sync_service()->GetUserSettings()->SetSyncRequested(true);
  auto setup_in_progress_handle = sync_service()->GetSetupInProgressHandle();

  // Once the engine calls back and says it's initialized, we're just waiting
  // for the user to finish the initial configuration (choosing data types etc.)
  // before actually syncing data.
  engine()->TriggerInitializationCompletion(/*success=*/true);
  ASSERT_TRUE(sync_service()->IsEngineInitialized());
  EXPECT_EQ(SyncService::TransportState::PENDING_DESIRED_CONFIGURATION,
            sync_service()->GetTransportState());
  EXPECT_FALSE(sync_service()->IsSyncFeatureEnabled());

  // Once the user finishes the initial setup, the service can actually start
  // configuring the data types. Just marking the initial setup as complete
  // isn't enough though, because setup is still considered in progress (we
  // haven't released the setup-in-progress handle).
  sync_service()->GetUserSettings()->SetFirstSetupComplete(
      syncer::SyncFirstSetupCompleteSource::BASIC_FLOW);
  EXPECT_EQ(SyncService::TransportState::PENDING_DESIRED_CONFIGURATION,
            sync_service()->GetTransportState());
  EXPECT_TRUE(sync_service()->IsSyncFeatureEnabled());

  // Prevent immediate configuration of one datatype, to verify the state
  // during CONFIGURING.
  ASSERT_EQ(DataTypeController::NOT_RUNNING, get_controller(SESSIONS)->state());
  get_controller(SESSIONS)->model()->EnableManualModelStart();

  // Releasing the setup in progress handle lets the service actually configure
  // the DataTypeManager.
  setup_in_progress_handle.reset();

  // While DataTypeManager configuration is ongoing, the overall state is still
  // CONFIGURING.
  EXPECT_EQ(SyncService::TransportState::CONFIGURING,
            sync_service()->GetTransportState());
  EXPECT_TRUE(sync_service()->IsSyncFeatureActive());
  EXPECT_NE(nullptr, data_type_manager());
  EXPECT_TRUE(engine());

  // Finally, once the DataTypeManager says it's done with configuration, Sync
  // is actually fully up and running.
  get_controller(SESSIONS)->model()->SimulateModelStartFinished();
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            sync_service()->GetTransportState());
  EXPECT_TRUE(sync_service()->IsSyncFeatureActive());
}
#endif  // BUILDFLAG(IS_CHROMEOS_ASH)

TEST_F(ProfileSyncServiceStartupTest, FullStartupSequenceNthTime) {
  // The user is already signed in and has completed Sync setup before.
  SimulateTestUserSignin();
  sync_prefs()->SetFirstSetupComplete();
  sync_prefs()->SetSyncRequested(true);

  // Note: Deferred startup is only enabled if SESSIONS is among the preferred
  // data types.
  CreateSyncService(ProfileSyncService::MANUAL_START,
                    ModelTypeSet(SESSIONS, TYPED_URLS));
  sync_service()->Initialize();
  ASSERT_TRUE(sync_service()->CanSyncFeatureStart());

  // Nothing is preventing Sync from starting, but it should be deferred so as
  // to now slow down browser startup.
  EXPECT_EQ(SyncService::TransportState::START_DEFERRED,
            sync_service()->GetTransportState());
  EXPECT_EQ(nullptr, data_type_manager());
  EXPECT_FALSE(engine());

  // Wait for the deferred startup timer to expire. The Sync service will start
  // and initialize the engine.
  component_factory()->AllowFakeEngineInitCompletion(false);
  FastForwardUntilNoTasksRemain();
  EXPECT_EQ(SyncService::TransportState::INITIALIZING,
            sync_service()->GetTransportState());
  EXPECT_EQ(nullptr, data_type_manager());
  EXPECT_TRUE(engine());

  // Prevent immediate configuration of one datatype, to verify the state
  // during CONFIGURING.
  ASSERT_EQ(DataTypeController::NOT_RUNNING, get_controller(SESSIONS)->state());
  get_controller(SESSIONS)->model()->EnableManualModelStart();

  // Once the engine calls back and says it's initialized, the DataTypeManager
  // will start configuring, since initial setup is already done.
  engine()->TriggerInitializationCompletion(/*success=*/true);

  ASSERT_EQ(DataTypeController::MODEL_STARTING,
            get_controller(SESSIONS)->state());
  EXPECT_NE(nullptr, data_type_manager());
  EXPECT_TRUE(engine());

  // Finally, once the DataTypeManager says it's done with configuration, Sync
  // is actually fully up and running.
  get_controller(SESSIONS)->model()->SimulateModelStartFinished();
  EXPECT_EQ(SyncService::TransportState::ACTIVE,
            sync_service()->GetTransportState());
}

}  // namespace syncer
