// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_SEND_TAB_TO_SELF_FEATURES_H_
#define COMPONENTS_SEND_TAB_TO_SELF_FEATURES_H_

#include "base/feature_list.h"

class PrefService;

namespace send_tab_to_self {

// If this feature is enabled, we will use signed-in, ephemeral data rather than
// persistent sync data. Users who are signed in can use the feature regardless
// of whether they have the sync feature enabled.
extern const base::Feature kSendTabToSelfWhenSignedIn;

// If this feature is enabled, use a fake backend implementation that supplies a
// hardcoded list of share targets, for UI work & debugging.
extern const base::Feature kSendTabToSelfUseFakeBackend;

// Returns whether the receiving components of the feature is enabled on this
// device. This doesn't rely on the SendTabToSelfSyncService to be actively up
// and ready.
bool IsReceivingEnabledByUserOnThisDevice(PrefService* prefs);

}  // namespace send_tab_to_self

#endif  // COMPONENTS_SEND_TAB_TO_SELF_FEATURES_H_
