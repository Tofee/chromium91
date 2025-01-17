// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/phonehub/phone_hub_ui_controller.h"

#include <memory>

#include "ash/session/session_controller_impl.h"
#include "ash/shell.h"
#include "ash/system/phonehub/bluetooth_disabled_view.h"
#include "ash/system/phonehub/onboarding_view.h"
#include "ash/system/phonehub/phone_connected_view.h"
#include "ash/system/phonehub/phone_connecting_view.h"
#include "ash/system/phonehub/phone_disconnected_view.h"
#include "ash/system/phonehub/phone_hub_content_view.h"
#include "ash/system/phonehub/tether_connection_pending_view.h"
#include "base/logging.h"
#include "base/time/time.h"
#include "chromeos/components/phonehub/browser_tabs_model_provider.h"
#include "chromeos/components/phonehub/connection_scheduler.h"
#include "chromeos/components/phonehub/phone_hub_manager.h"
#include "chromeos/components/phonehub/tether_controller.h"
#include "chromeos/components/phonehub/user_action_recorder.h"

using FeatureStatus = chromeos::phonehub::FeatureStatus;
using TetherStatus = chromeos::phonehub::TetherController::Status;

namespace ash {

namespace {

constexpr base::TimeDelta kConnectingViewGracePeriod =
    base::TimeDelta::FromSeconds(40);

}  // namespace

PhoneHubUiController::PhoneHubUiController() {
  // ash::Shell may not exist in tests.
  if (ash::Shell::HasInstance())
    Shell::Get()->session_controller()->AddObserver(this);
}

PhoneHubUiController::~PhoneHubUiController() {
  // ash::Shell may not exist in tests.
  if (ash::Shell::HasInstance())
    Shell::Get()->session_controller()->RemoveObserver(this);
  CleanUpPhoneHubManager();
}

void PhoneHubUiController::SetPhoneHubManager(
    chromeos::phonehub::PhoneHubManager* phone_hub_manager) {
  if (phone_hub_manager == phone_hub_manager_)
    return;

  CleanUpPhoneHubManager();

  phone_hub_manager_ = phone_hub_manager;
  if (phone_hub_manager_) {
    phone_hub_manager_->GetFeatureStatusProvider()->AddObserver(this);
    phone_hub_manager_->GetOnboardingUiTracker()->AddObserver(this);
    phone_hub_manager_->GetPhoneModel()->AddObserver(this);
  }

  UpdateUiState(GetUiStateFromPhoneHubManager());
}

std::unique_ptr<views::View> PhoneHubUiController::CreateStatusHeaderView(
    PhoneStatusView::Delegate* delegate) {
  if (!phone_hub_manager_)
    return nullptr;
  return std::make_unique<PhoneStatusView>(phone_hub_manager_->GetPhoneModel(),
                                           delegate);
}

std::unique_ptr<PhoneHubContentView> PhoneHubUiController::CreateContentView(
    OnboardingView::Delegate* delegate) {
  switch (ui_state_) {
    case UiState::kHidden:
      return nullptr;
    case UiState::kOnboardingWithoutPhone:
      return std::make_unique<OnboardingView>(
          phone_hub_manager_->GetOnboardingUiTracker(), delegate,
          OnboardingView::kNewMultideviceUser);
    case UiState::kOnboardingWithPhone:
      return std::make_unique<OnboardingView>(
          phone_hub_manager_->GetOnboardingUiTracker(), delegate,
          OnboardingView::kExistingMultideviceUser);
    case UiState::kBluetoothDisabled:
      return std::make_unique<BluetoothDisabledView>();
    case UiState::kPhoneConnecting:
      return std::make_unique<PhoneConnectingView>();
    case UiState::kTetherConnectionPending:
      return std::make_unique<TetherConnectionPendingView>();
    case UiState::kPhoneDisconnected:
      if (connecting_view_grace_period_timer_.IsRunning())
        return std::make_unique<PhoneConnectingView>();
      return std::make_unique<PhoneDisconnectedView>(
          phone_hub_manager_->GetConnectionScheduler());
    case UiState::kPhoneConnected:
      return std::make_unique<PhoneConnectedView>(phone_hub_manager_);
  }
}

void PhoneHubUiController::HandleBubbleOpened() {
  if (!phone_hub_manager_)
    return;

  auto feature_status =
      phone_hub_manager_->GetFeatureStatusProvider()->GetStatus();
  if (feature_status == FeatureStatus::kEnabledButDisconnected)
    phone_hub_manager_->GetConnectionScheduler()->ScheduleConnectionNow();

  phone_hub_manager_->GetBrowserTabsModelProvider()->TriggerRefresh();
  phone_hub_manager_->GetUserActionRecorder()->RecordUiOpened();

  bool is_feature_enabled =
      feature_status == FeatureStatus::kEnabledAndConnected ||
      feature_status == FeatureStatus::kEnabledButDisconnected ||
      feature_status == FeatureStatus::kEnabledAndConnected;

  if (!is_feature_enabled)
    return;

  if (!has_requested_tether_scan_during_session_ &&
      phone_hub_manager_->GetTetherController()->GetStatus() ==
          TetherStatus::kConnectionUnavailable) {
    phone_hub_manager_->GetTetherController()->ScanForAvailableConnection();
    has_requested_tether_scan_during_session_ = true;
  }
}

void PhoneHubUiController::AddObserver(Observer* observer) {
  observer_list_.AddObserver(observer);
}

void PhoneHubUiController::RemoveObserver(Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

void PhoneHubUiController::OnFeatureStatusChanged() {
  UpdateUiState(GetUiStateFromPhoneHubManager());
}

void PhoneHubUiController::OnShouldShowOnboardingUiChanged() {
  UpdateUiState(GetUiStateFromPhoneHubManager());
}

void PhoneHubUiController::OnModelChanged() {
  UpdateUiState(GetUiStateFromPhoneHubManager());
}

void PhoneHubUiController::OnActiveUserSessionChanged(
    const AccountId& account_id) {
  UpdateUiState(GetUiStateFromPhoneHubManager());
}

void PhoneHubUiController::UpdateUiState(
    PhoneHubUiController::UiState new_state) {
  if (new_state == ui_state_)
    return;

  ui_state_ = new_state;
  for (auto& observer : observer_list_)
    observer.OnPhoneHubUiStateChanged();
}

PhoneHubUiController::UiState
PhoneHubUiController::GetUiStateFromPhoneHubManager() {
  if (!Shell::Get()->session_controller()->IsUserPrimary() ||
      !phone_hub_manager_)
    return UiState::kHidden;

  auto feature_status =
      phone_hub_manager_->GetFeatureStatusProvider()->GetStatus();

  auto* tracker = phone_hub_manager_->GetOnboardingUiTracker();
  auto* phone_model = phone_hub_manager_->GetPhoneModel();
  bool should_show_onboarding_ui = tracker->ShouldShowOnboardingUi();
  bool is_tether_connecting =
      phone_hub_manager_->GetTetherController()->GetStatus() ==
      TetherStatus::kConnecting;

  switch (feature_status) {
    case FeatureStatus::kPhoneSelectedAndPendingSetup:
      FALLTHROUGH;
    case FeatureStatus::kNotEligibleForFeature:
      return UiState::kHidden;

    case FeatureStatus::kEligiblePhoneButNotSetUp:
      return should_show_onboarding_ui ? UiState::kOnboardingWithoutPhone
                                       : UiState::kHidden;

    case FeatureStatus::kDisabled:
      return should_show_onboarding_ui ? UiState::kOnboardingWithPhone
                                       : UiState::kHidden;

    case FeatureStatus::kUnavailableBluetoothOff:
      return UiState::kBluetoothDisabled;

    case FeatureStatus::kEnabledButDisconnected:
      return UiState::kPhoneDisconnected;

    case FeatureStatus::kEnabledAndConnecting:
      connecting_view_grace_period_timer_.Start(
          FROM_HERE, kConnectingViewGracePeriod,
          base::BindOnce(&PhoneHubUiController::OnConnectingViewTimerEnd,
                         base::Unretained(this)));

      // If a tether network is being connected to, or the |ui_state_|
      // was UiState::kTetherConnectionPending, continue returning
      // the UiState::kTetherConnectionPending state.
      return is_tether_connecting ||
                     ui_state_ == UiState::kTetherConnectionPending
                 ? UiState::kTetherConnectionPending
                 : UiState::kPhoneConnecting;

    case FeatureStatus::kEnabledAndConnected:
      // If the timer is running, reset the timer so if we disconnect, we will
      // show the connecting view instead of the disconnecting view.
      if (connecting_view_grace_period_timer_.IsRunning())
        connecting_view_grace_period_timer_.Reset();

      // Delay displaying the connected view until the phone model is ready.
      if (phone_model->phone_status_model().has_value())
        return UiState::kPhoneConnected;

      // If the the |ui_state_| was UiState::kTetherConnectionPending, continue
      // returning the UiState::kTetherConnectionPending state.
      if (ui_state_ == UiState::kTetherConnectionPending)
        return UiState::kTetherConnectionPending;

      return UiState::kPhoneConnecting;

    case FeatureStatus::kLockOrSuspended:
      return UiState::kHidden;
  }
}

void PhoneHubUiController::OnConnectingViewTimerEnd() {
  // Update the UI state if the UI state has changed.
  if (ui_state_ != UiState::kPhoneDisconnected) {
    UpdateUiState(GetUiStateFromPhoneHubManager());
    return;
  }

  // If we are still disconnected, force the observation. We cannot call
  // |GetUiStateFromPhoneHubManager()| in this case because it will reset the
  // timer, and thus the disconnected view will never be shown. This way, the
  // disconnected view will be shown.
  for (auto& observer : observer_list_)
    observer.OnPhoneHubUiStateChanged();
}

void PhoneHubUiController::CleanUpPhoneHubManager() {
  if (!phone_hub_manager_)
    return;

  phone_hub_manager_->GetFeatureStatusProvider()->RemoveObserver(this);
  phone_hub_manager_->GetOnboardingUiTracker()->RemoveObserver(this);
  phone_hub_manager_->GetPhoneModel()->RemoveObserver(this);
}

}  // namespace ash
