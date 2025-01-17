// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_CRYPTOHOME_USERDATAAUTH_UTIL_H_
#define CHROMEOS_CRYPTOHOME_USERDATAAUTH_UTIL_H_

#include "base/component_export.h"
#include "base/optional.h"
#include "chromeos/cryptohome/cryptohome_parameters.h"
#include "chromeos/dbus/cryptohome/UserDataAuth.pb.h"
#include "chromeos/dbus/cryptohome/key.pb.h"
#include "chromeos/dbus/cryptohome/rpc.pb.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace user_data_auth {

// Returns a MountError code from |reply|, returning MOUNT_ERROR_NONE
// if the reply is well-formed and there is no error.
template <typename ReplyType>
COMPONENT_EXPORT(CHROMEOS_CRYPTOHOME)
cryptohome::MountError
    ReplyToMountError(const base::Optional<ReplyType>& reply);

// Converts the key metadata in GetKeyDataReply into cryptohome::KeyDefinition
// format.
COMPONENT_EXPORT(CHROMEOS_CRYPTOHOME)
std::vector<cryptohome::KeyDefinition> GetKeyDataReplyToKeyDefinitions(
    const base::Optional<GetKeyDataReply>& reply);

// Extracts the account's disk usage size from |reply|.
// If |reply| is malformed, returns -1.
COMPONENT_EXPORT(CHROMEOS_CRYPTOHOME)
int64_t AccountDiskUsageReplyToUsageSize(
    const base::Optional<GetAccountDiskUsageReply>& reply);

// Converts user_data_auth::CryptohomeErrorCode to cryptohome::MountError.
COMPONENT_EXPORT(CHROMEOS_CRYPTOHOME)
cryptohome::MountError CryptohomeErrorToMountError(CryptohomeErrorCode code);

}  // namespace user_data_auth

#endif  // CHROMEOS_CRYPTOHOME_USERDATAAUTH_UTIL_H_
