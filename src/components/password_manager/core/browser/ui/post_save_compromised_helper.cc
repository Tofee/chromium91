// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/password_manager/core/browser/ui/post_save_compromised_helper.h"

#include "base/containers/contains.h"
#include "components/password_manager/core/browser/password_store.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/prefs/pref_service.h"

namespace password_manager {

// Maximum time since the last password check while the result is considered
// up to date.
constexpr auto kMaxTimeSinceLastCheck = base::TimeDelta::FromMinutes(30);

PostSaveCompromisedHelper::PostSaveCompromisedHelper(
    base::span<const InsecureCredential> compromised,
    const std::u16string& current_username) {
  for (const InsecureCredential& credential : compromised) {
    if (credential.username == current_username)
      current_leak_ = credential;
  }
}

PostSaveCompromisedHelper::~PostSaveCompromisedHelper() = default;

void PostSaveCompromisedHelper::AnalyzeLeakedCredentials(
    PasswordStore* profile_store,
    PasswordStore* account_store,
    PrefService* prefs,
    BubbleCallback callback) {
  DCHECK(profile_store);
  DCHECK(prefs);

  const double last_check_completed =
      prefs->GetDouble(prefs::kLastTimePasswordCheckCompleted);
  // If the check was never completed then |kLastTimePasswordCheckCompleted|
  // contains 0.
  if (!last_check_completed ||
      base::Time::Now() - base::Time::FromDoubleT(last_check_completed) >=
          kMaxTimeSinceLastCheck) {
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::BindOnce(std::move(callback), BubbleType::kNoBubble, 0));
    return;
  }

  callback_ = std::move(callback);
  insecure_credentials_reader_ =
      std::make_unique<InsecureCredentialsReader>(profile_store, account_store);
  // Unretained(this) is safe here since `this` outlives
  // `insecure_credentials_reader_`.
  insecure_credentials_reader_->GetAllInsecureCredentials(
      base::BindOnce(&PostSaveCompromisedHelper::OnGetAllInsecureCredentials,
                     base::Unretained(this)));
}

void PostSaveCompromisedHelper::OnGetAllInsecureCredentials(
    std::vector<InsecureCredential> insecure_credentials) {
  const bool compromised_password_changed =
      current_leak_ && !base::Contains(insecure_credentials, *current_leak_);
  if (compromised_password_changed) {
    bubble_type_ = insecure_credentials.empty()
                       ? BubbleType::kPasswordUpdatedSafeState
                       : BubbleType::kPasswordUpdatedWithMoreToFix;
  } else {
    bubble_type_ = BubbleType::kNoBubble;
  }
  compromised_count_ = insecure_credentials.size();
  std::move(callback_).Run(bubble_type_, compromised_count_);
}

}  // namespace password_manager
