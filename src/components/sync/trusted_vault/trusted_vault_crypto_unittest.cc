// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/trusted_vault/trusted_vault_crypto.h"

#include <memory>

#include "base/strings/string_number_conversions.h"
#include "components/sync/trusted_vault/securebox.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace syncer {

namespace {

using testing::Eq;
using testing::Ne;

const char kEncodedPrivateKey[] =
    "49e052293c29b5a50b0013eec9d030ac2ad70a42fe093be084264647cb04e16f";

std::unique_ptr<SecureBoxKeyPair> MakeTestKeyPair() {
  std::vector<uint8_t> private_key_bytes;
  bool success = base::HexStringToBytes(kEncodedPrivateKey, &private_key_bytes);
  DCHECK(success);
  return SecureBoxKeyPair::CreateByPrivateKeyImport(private_key_bytes);
}

TEST(TrustedVaultCrypto, ShouldHandleDecryptionFailure) {
  EXPECT_THAT(DecryptTrustedVaultWrappedKey(
                  MakeTestKeyPair()->private_key(),
                  /*wrapped_key=*/std::vector<uint8_t>{1, 2, 3, 4}),
              Eq(base::nullopt));
}

TEST(TrustedVaultCrypto, ShouldEncryptAndDecryptWrappedKey) {
  const std::vector<uint8_t> trusted_vault_key = {1, 2, 3, 4};
  const std::unique_ptr<SecureBoxKeyPair> key_pair = MakeTestKeyPair();
  base::Optional<std::vector<uint8_t>> decrypted_trusted_vault_key =
      DecryptTrustedVaultWrappedKey(
          key_pair->private_key(),
          /*wrapped_key=*/ComputeTrustedVaultWrappedKey(key_pair->public_key(),
                                                        trusted_vault_key));
  ASSERT_THAT(decrypted_trusted_vault_key, Ne(base::nullopt));
  EXPECT_THAT(*decrypted_trusted_vault_key, Eq(trusted_vault_key));
}

TEST(TrustedVaultCrypto, ShouldComputeAndVerifyHMAC) {
  const std::vector<uint8_t> key = {1, 2, 3, 4};
  const std::vector<uint8_t> data = {1, 2, 3, 5};
  EXPECT_TRUE(
      VerifyTrustedVaultHMAC(key, data,
                             /*digest=*/ComputeTrustedVaultHMAC(key, data)));
}

TEST(TrustedVaultCrypto, ShouldDetectIncorrectHMAC) {
  const std::vector<uint8_t> correct_key = {1, 2, 3, 4};
  const std::vector<uint8_t> incorrect_key = {1, 2, 3, 5};
  const std::vector<uint8_t> data = {1, 2, 3, 6};
  EXPECT_FALSE(VerifyTrustedVaultHMAC(
      correct_key, data,
      /*digest=*/ComputeTrustedVaultHMAC(incorrect_key, data)));
}

}  // namespace

}  // namespace syncer
