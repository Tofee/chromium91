// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SYNC_DRIVER_SYNC_DRIVER_SWITCHES_H_
#define COMPONENTS_SYNC_DRIVER_SYNC_DRIVER_SWITCHES_H_

#include "base/feature_list.h"

namespace switches {

// Returns whether sync is allowed to run based on command-line switches.
// Profile::IsSyncAllowed() is probably a better signal than this function.
// This function can be called from any thread, and the implementation doesn't
// assume it's running on the UI thread.
bool IsSyncAllowedByFlag();

// Defines all the command-line switches used by sync driver. All switches in
// alphabetical order. The switches should be documented alongside the
// definition of their values in the .cc file.
extern const char kDisableSync[];
extern const char kSyncDeferredStartupTimeoutSeconds[];
extern const char kSyncDisableDeferredStartup[];
extern const char kSyncIncludeSpecificsInProtocolLog[];
extern const char kSyncShortInitialRetryOverride[];
extern const char kSyncShortNudgeDelayForTest[];

extern const base::Feature
    kSyncAllowWalletDataInTransportModeWithCustomPassphrase;
extern const base::Feature kSyncAutofillWalletOfferData;
extern const base::Feature kSyncWifiConfigurations;
extern const base::Feature kDecoupleSyncFromAndroidMasterSync;
extern const base::Feature kFollowTrustedVaultKeyRotation;
extern const base::Feature kAllowSilentTrustedVaultDeviceRegistration;
extern const base::FeatureParam<base::TimeDelta>
    kTrustedVaultServiceThrottlingDuration;

extern const base::Feature kSyncRequiresPoliciesLoaded;
extern const base::FeatureParam<base::TimeDelta> kSyncPolicyLoadTimeout;

extern const base::Feature kSyncSupportTrustedVaultPassphraseRecovery;

}  // namespace switches

#endif  // COMPONENTS_SYNC_DRIVER_SYNC_DRIVER_SWITCHES_H_
