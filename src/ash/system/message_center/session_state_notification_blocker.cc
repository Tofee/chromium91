// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/message_center/session_state_notification_blocker.h"

#include "ash/session/session_controller_impl.h"
#include "ash/shell.h"
#include "ash/system/message_center/ash_message_center_lock_screen_controller.h"
#include "ui/message_center/message_center.h"
#include "ui/message_center/public/cpp/notification.h"
#include "ui/message_center/public/cpp/notification_types.h"
#include "ui/message_center/public/cpp/notifier_id.h"

using session_manager::SessionState;

namespace ash {

namespace {

constexpr base::TimeDelta kLoginNotificationDelay =
    base::TimeDelta::FromSeconds(6);

// Set to false for tests so notifications can be generated without a delay.
bool g_use_login_delay_for_test = true;

bool CalculateShouldShowNotification() {
  SessionControllerImpl* const session_controller =
      Shell::Get()->session_controller();

  return !session_controller->IsRunningInAppMode();
}

bool CalculateShouldShowPopup() {
  SessionControllerImpl* const session_controller =
      Shell::Get()->session_controller();

  // Enable popup in OOBE and login screen to display system notifications
  // (wifi, etc.).
  if (session_controller->GetSessionState() == SessionState::OOBE ||
      session_controller->GetSessionState() == SessionState::LOGIN_PRIMARY)
    return true;

  if (session_controller->IsRunningInAppMode() ||
      session_controller->GetSessionState() != SessionState::ACTIVE) {
    return false;
  }

  const UserSession* active_user_session =
      session_controller->GetUserSession(0);
  return active_user_session && session_controller->GetUserPrefServiceForUser(
                                    active_user_session->user_info.account_id);
}

}  // namespace

SessionStateNotificationBlocker::SessionStateNotificationBlocker(
    message_center::MessageCenter* message_center)
    : NotificationBlocker(message_center),
      should_show_notification_(CalculateShouldShowNotification()),
      should_show_popup_(CalculateShouldShowPopup()) {
  Shell::Get()->session_controller()->AddObserver(this);
}

SessionStateNotificationBlocker::~SessionStateNotificationBlocker() {
  Shell::Get()->session_controller()->RemoveObserver(this);
}

// static
void SessionStateNotificationBlocker::SetUseLoginNotificationDelayForTest(
    bool use_delay) {
  g_use_login_delay_for_test = use_delay;
}

void SessionStateNotificationBlocker::OnLoginTimerEnded() {
  NotifyBlockingStateChanged();
}

bool SessionStateNotificationBlocker::ShouldShowNotification(
    const message_center::Notification& notification) const {
  // Do not show non system notifications for `kLoginNotificationsDelay`
  // duration.
  if (notification.notifier_id().type !=
          message_center::NotifierType::SYSTEM_COMPONENT &&
      login_delay_timer_.IsRunning()) {
    return false;
  }

  return should_show_notification_;
}

bool SessionStateNotificationBlocker::ShouldShowNotificationAsPopup(
    const message_center::Notification& notification) const {
  SessionControllerImpl* const session_controller =
      Shell::Get()->session_controller();

  // Never show notifications in kiosk mode.
  if (session_controller->IsRunningInAppMode())
    return false;

  if (notification.notifier_id().profile_id.empty() &&
      notification.priority() >= message_center::SYSTEM_PRIORITY) {
    return true;
  }

  return should_show_popup_;
}

void SessionStateNotificationBlocker::OnFirstSessionStarted() {
  if (!g_use_login_delay_for_test)
    return;
  login_delay_timer_.Start(FROM_HERE, kLoginNotificationDelay, this,
                           &SessionStateNotificationBlocker::OnLoginTimerEnded);
}

void SessionStateNotificationBlocker::OnSessionStateChanged(
    SessionState state) {
  CheckStateAndNotifyIfChanged();
}

void SessionStateNotificationBlocker::OnActiveUserPrefServiceChanged(
    PrefService* pref_service) {
  CheckStateAndNotifyIfChanged();
}

void SessionStateNotificationBlocker::CheckStateAndNotifyIfChanged() {
  const bool new_should_show_notification = CalculateShouldShowNotification();
  const bool new_should_show_popup = CalculateShouldShowPopup();
  if (new_should_show_notification == should_show_notification_ &&
      new_should_show_popup == should_show_popup_) {
    return;
  }

  should_show_notification_ = new_should_show_notification;
  should_show_popup_ = new_should_show_popup;
  NotifyBlockingStateChanged();
}

}  // namespace ash
