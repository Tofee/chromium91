// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/sync/trusted_vault/download_keys_response_handler.h"

#include <vector>

#include "base/strings/string_number_conversions.h"
#include "components/sync/protocol/vault.pb.h"
#include "components/sync/trusted_vault/proto_string_bytes_conversion.h"
#include "components/sync/trusted_vault/securebox.h"
#include "components/sync/trusted_vault/trusted_vault_crypto.h"
#include "components/sync/trusted_vault/trusted_vault_server_constants.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace syncer {

namespace {

using testing::ElementsAre;
using testing::Eq;
using testing::IsEmpty;

const char kEncodedPrivateKey[] =
    "49e052293c29b5a50b0013eec9d030ac2ad70a42fe093be084264647cb04e16f";

std::unique_ptr<SecureBoxKeyPair> MakeTestKeyPair() {
  std::vector<uint8_t> private_key_bytes;
  bool success = base::HexStringToBytes(kEncodedPrivateKey, &private_key_bytes);
  DCHECK(success);
  return SecureBoxKeyPair::CreateByPrivateKeyImport(private_key_bytes);
}

void AddSecurityDomainMembership(
    const std::string& security_domain_name,
    const SecureBoxPublicKey& member_public_key,
    const std::vector<std::vector<uint8_t>>& trusted_vault_keys,
    const std::vector<int>& trusted_vault_keys_versions,
    const std::vector<std::vector<uint8_t>>& signing_keys,
    sync_pb::SecurityDomainMember* member) {
  DCHECK(member);
  DCHECK_EQ(trusted_vault_keys.size(), trusted_vault_keys_versions.size());
  DCHECK_EQ(trusted_vault_keys.size(), signing_keys.size());

  sync_pb::SecurityDomainMember::SecurityDomainMembership* membership =
      member->add_memberships();
  membership->set_security_domain(security_domain_name);
  for (size_t i = 0; i < trusted_vault_keys.size(); ++i) {
    sync_pb::SharedMemberKey* shared_key = membership->add_keys();
    shared_key->set_epoch(trusted_vault_keys_versions[i]);
    AssignBytesToProtoString(
        ComputeTrustedVaultWrappedKey(member_public_key, trusted_vault_keys[i]),
        shared_key->mutable_wrapped_key());

    if (!signing_keys[i].empty()) {
      sync_pb::RotationProof* rotation_proof =
          membership->add_rotation_proofs();
      rotation_proof->set_new_epoch(trusted_vault_keys_versions[i]);
      AssignBytesToProtoString(
          ComputeTrustedVaultHMAC(signing_keys[i], trusted_vault_keys[i]),
          rotation_proof->mutable_rotation_proof());
    }
  }
}

std::string CreateGetSecurityDomainMemberResponseWithSyncMembership(
    const std::vector<std::vector<uint8_t>>& trusted_vault_keys,
    const std::vector<int>& trusted_vault_keys_versions,
    const std::vector<std::vector<uint8_t>>& signing_keys) {
  sync_pb::SecurityDomainMember member;
  AddSecurityDomainMembership(
      kSyncSecurityDomainName, MakeTestKeyPair()->public_key(),
      trusted_vault_keys, trusted_vault_keys_versions, signing_keys, &member);
  return member.SerializeAsString();
}

class DownloadKeysResponseHandlerTest : public testing::Test {
 public:
  DownloadKeysResponseHandlerTest()
      : handler_(TrustedVaultKeyAndVersion(kKnownTrustedVaultKey,
                                           kKnownTrustedVaultKeyVersion),
                 MakeTestKeyPair()) {}

  ~DownloadKeysResponseHandlerTest() override = default;

  const DownloadKeysResponseHandler& handler() const { return handler_; }

  const int kKnownTrustedVaultKeyVersion = 5;
  const std::vector<uint8_t> kKnownTrustedVaultKey = {1, 2, 3, 4};
  const std::vector<uint8_t> kTrustedVaultKey1 = {1, 2, 3, 5};
  const std::vector<uint8_t> kTrustedVaultKey2 = {1, 2, 3, 6};
  const std::vector<uint8_t> kTrustedVaultKey3 = {1, 2, 3, 7};

 private:
  const DownloadKeysResponseHandler handler_;
};

// All HttpStatuses except kSuccess should end up in kOtherError or
// kLocalDataObsolete reporting.
TEST_F(DownloadKeysResponseHandlerTest, ShouldHandleHttpErrors) {
  EXPECT_THAT(
      handler()
          .ProcessResponse(
              /*http_status=*/TrustedVaultRequest::HttpStatus::kNotFound,
              /*response_body=*/std::string())
          .status,
      Eq(TrustedVaultRequestStatus::kLocalDataObsolete));
  EXPECT_THAT(handler()
                  .ProcessResponse(
                      /*http_status=*/TrustedVaultRequest::HttpStatus::
                          kFailedPrecondition,
                      /*response_body=*/std::string())
                  .status,
              Eq(TrustedVaultRequestStatus::kLocalDataObsolete));
  EXPECT_THAT(
      handler()
          .ProcessResponse(
              /*http_status=*/TrustedVaultRequest::HttpStatus::kOtherError,
              /*response_body=*/std::string())
          .status,
      Eq(TrustedVaultRequestStatus::kOtherError));
}

// Simplest legitimate case of key rotation, server side state corresponds to
// kKnownTrustedVaultKey -> kTrustedVaultKey1 key chain.
TEST_F(DownloadKeysResponseHandlerTest, ShouldHandleSingleKeyRotation) {
  const DownloadKeysResponseHandler::ProcessedResponse processed_response =
      handler().ProcessResponse(
          /*http_status=*/TrustedVaultRequest::HttpStatus::kSuccess,
          /*response_body=*/
          CreateGetSecurityDomainMemberResponseWithSyncMembership(
              /*trusted_vault_keys=*/{kKnownTrustedVaultKey, kTrustedVaultKey1},
              /*trusted_vault_keys_versions=*/
              {kKnownTrustedVaultKeyVersion, kKnownTrustedVaultKeyVersion + 1},
              /*signing_keys=*/{{}, kKnownTrustedVaultKey}));

  EXPECT_THAT(processed_response.status,
              Eq(TrustedVaultRequestStatus::kSuccess));
  EXPECT_THAT(processed_response.new_keys, ElementsAre(kTrustedVaultKey1));
  EXPECT_THAT(processed_response.last_key_version,
              Eq(kKnownTrustedVaultKeyVersion + 1));
}

// Multiple key rotations may happen while client is offline, server-side key
// chain is kKnownTrustedVaultKey -> kTrustedVaultKey1 -> kTrustedVaultKey2.
TEST_F(DownloadKeysResponseHandlerTest, ShouldHandleMultipleKeyRotations) {
  const DownloadKeysResponseHandler::ProcessedResponse processed_response =
      handler().ProcessResponse(
          /*http_status=*/TrustedVaultRequest::HttpStatus::kSuccess,
          /*response_body=*/
          CreateGetSecurityDomainMemberResponseWithSyncMembership(
              /*trusted_vault_keys=*/
              {kKnownTrustedVaultKey, kTrustedVaultKey1, kTrustedVaultKey2},
              /*trusted_vault_keys_versions=*/
              {kKnownTrustedVaultKeyVersion, kKnownTrustedVaultKeyVersion + 1,
               kKnownTrustedVaultKeyVersion + 2},
              /*signing_keys=*/{{}, kKnownTrustedVaultKey, kTrustedVaultKey1}));

  EXPECT_THAT(processed_response.status,
              Eq(TrustedVaultRequestStatus::kSuccess));
  EXPECT_THAT(processed_response.new_keys,
              ElementsAre(kTrustedVaultKey1, kTrustedVaultKey2));
  EXPECT_THAT(processed_response.last_key_version,
              Eq(kKnownTrustedVaultKeyVersion + 2));
}

// There might be keys, that predates latest client-side trusted vault key.
// Server-side key chain is kTrustedVaultKey1 -> kKnownTrustedVaultKey ->
// kTrustedVaultKey2 -> kTrustedVaultKey3.
// Since kTrustedVaultKey1 can't be validated using kKnownTrustedVaultKey it
// shouldn't be included in processed response.
TEST_F(DownloadKeysResponseHandlerTest, ShouldHandlePriorKeys) {
  const DownloadKeysResponseHandler::ProcessedResponse processed_response =
      handler().ProcessResponse(
          /*http_status=*/TrustedVaultRequest::HttpStatus::kSuccess,
          /*response_body=*/
          CreateGetSecurityDomainMemberResponseWithSyncMembership(
              /*trusted_vault_keys=*/
              {kTrustedVaultKey1, kKnownTrustedVaultKey, kTrustedVaultKey2,
               kTrustedVaultKey3},
              /*trusted_vault_keys_versions=*/
              {kKnownTrustedVaultKeyVersion - 1, kKnownTrustedVaultKeyVersion,
               kKnownTrustedVaultKeyVersion + 1,
               kKnownTrustedVaultKeyVersion + 2},
              /*signing_keys=*/
              {{},
               kTrustedVaultKey1,
               kKnownTrustedVaultKey,
               kTrustedVaultKey2}));

  EXPECT_THAT(processed_response.status,
              Eq(TrustedVaultRequestStatus::kSuccess));
  EXPECT_THAT(processed_response.new_keys,
              ElementsAre(kTrustedVaultKey2, kTrustedVaultKey3));
  EXPECT_THAT(processed_response.last_key_version,
              Eq(kKnownTrustedVaultKeyVersion + 2));
}

// Server can already clean-up kKnownTrustedVaultKey, but it might still be
// possible to validate the key-chain.
// Full key chain is: kKnownTrustedVaultKey -> kTrustedVaultKey1 ->
// kTrustedVaultKey2.
// Server-side key chain is: kTrustedVaultKey1 -> kTrustedVaultKey2.
TEST_F(DownloadKeysResponseHandlerTest,
       ShouldHandleAbsenseOfKnownKeyWhenKeyChainIsRecoverable) {
  const DownloadKeysResponseHandler::ProcessedResponse processed_response =
      handler().ProcessResponse(
          /*http_status=*/TrustedVaultRequest::HttpStatus::kSuccess,
          /*response_body=*/
          CreateGetSecurityDomainMemberResponseWithSyncMembership(
              /*trusted_vault_keys=*/
              {kTrustedVaultKey1, kTrustedVaultKey2},
              /*trusted_vault_keys_versions=*/
              {kKnownTrustedVaultKeyVersion + 1,
               kKnownTrustedVaultKeyVersion + 2},
              /*signing_keys=*/
              {kKnownTrustedVaultKey, kTrustedVaultKey1}));

  EXPECT_THAT(processed_response.status,
              Eq(TrustedVaultRequestStatus::kSuccess));
  EXPECT_THAT(processed_response.new_keys,
              ElementsAre(kTrustedVaultKey1, kTrustedVaultKey2));
  EXPECT_THAT(processed_response.last_key_version,
              Eq(kKnownTrustedVaultKeyVersion + 2));
}

// Server can already clean-up kKnownTrustedVaultKey and the following key. In
// this case client state is not sufficient to silently download keys and
// kLocalDataObsolete should be reported.
// Possible full key chain is: kKnownTrustedVaultKey -> kTrustedVaultKey1 ->
// kTrustedVaultKey2 -> kTrustedVaultKey3.
// Server side key chain is: kTrustedVaultKey2 -> kTrustedVaultKey3.
TEST_F(DownloadKeysResponseHandlerTest,
       ShouldHandleAbsenseOfKnownKeyWhenKeyChainIsNotRecoverable) {
  const DownloadKeysResponseHandler::ProcessedResponse processed_response =
      handler().ProcessResponse(
          /*http_status=*/TrustedVaultRequest::HttpStatus::kSuccess,
          /*response_body=*/
          CreateGetSecurityDomainMemberResponseWithSyncMembership(
              /*trusted_vault_keys=*/
              {kTrustedVaultKey2, kTrustedVaultKey3},
              /*trusted_vault_keys_versions=*/
              {kKnownTrustedVaultKeyVersion + 2,
               kKnownTrustedVaultKeyVersion + 3},
              /*signing_keys=*/
              {kTrustedVaultKey1, kTrustedVaultKey2}));

  EXPECT_THAT(processed_response.status,
              Eq(TrustedVaultRequestStatus::kLocalDataObsolete));
  EXPECT_THAT(processed_response.new_keys, IsEmpty());
}

// The test populates undecryptable/corrupted |wrapped_key| field, handler
// should return kLocalDataObsolete to allow client to restore Member by
// re-registration.
TEST_F(DownloadKeysResponseHandlerTest, ShouldHandleUndecryptableKey) {
  sync_pb::SecurityDomainMember member;
  AddSecurityDomainMembership(
      kSyncSecurityDomainName, MakeTestKeyPair()->public_key(),
      /*trusted_vault_keys=*/{kKnownTrustedVaultKey, kTrustedVaultKey1},
      /*trusted_vault_keys_versions=*/
      {kKnownTrustedVaultKeyVersion, kKnownTrustedVaultKeyVersion + 1},
      /*signing_keys=*/{{}, kKnownTrustedVaultKey}, &member);

  // Corrupt wrapped key corresponding to kTrustedVaultKey1.
  member.mutable_memberships(0)->mutable_keys(1)->set_wrapped_key(
      "undecryptable_key");

  EXPECT_THAT(handler()
                  .ProcessResponse(
                      /*http_status=*/TrustedVaultRequest::HttpStatus::kSuccess,
                      /*response_body=*/member.SerializeAsString())
                  .status,
              Eq(TrustedVaultRequestStatus::kLocalDataObsolete));
}

// The test populates invalid |rotation_proof| field for the single key
// rotation. kTrustedVaultKey1 is expected to be signed with
// kKnownTrustedVaultKey, but instead it's signed with kTrustedVaultKey2.
TEST_F(DownloadKeysResponseHandlerTest,
       ShouldHandleInvalidKeyProofOnSingleKeyRotation) {
  const DownloadKeysResponseHandler::ProcessedResponse processed_response =
      handler().ProcessResponse(
          /*http_status=*/TrustedVaultRequest::HttpStatus::kSuccess,
          /*response_body=*/
          CreateGetSecurityDomainMemberResponseWithSyncMembership(
              /*trusted_vault_keys=*/{kKnownTrustedVaultKey, kTrustedVaultKey1},
              /*trusted_vault_keys_versions=*/
              {kKnownTrustedVaultKeyVersion, kKnownTrustedVaultKeyVersion + 1},
              /*signing_keys=*/{{}, kTrustedVaultKey2}));

  EXPECT_THAT(processed_response.status,
              Eq(TrustedVaultRequestStatus::kLocalDataObsolete));
  EXPECT_THAT(processed_response.new_keys, IsEmpty());
}

// The test populates invalid |rotation_proof| field for intermediate key when
// multiple key rotations have happened.
// kTrustedVaultKey1 is expected to be signed with kKnownTrustedVaultKey, but
// instead it's signed with kTrustedVaultKey2.
TEST_F(DownloadKeysResponseHandlerTest,
       ShouldHandleInvalidKeyProofOnMultipleKeyRotations) {
  const DownloadKeysResponseHandler::ProcessedResponse processed_response =
      handler().ProcessResponse(
          /*http_status=*/TrustedVaultRequest::HttpStatus::kSuccess,
          /*response_body=*/
          CreateGetSecurityDomainMemberResponseWithSyncMembership(
              /*trusted_vault_keys=*/{kKnownTrustedVaultKey, kTrustedVaultKey1,
                                      kTrustedVaultKey2},
              /*trusted_vault_keys_versions=*/
              {kKnownTrustedVaultKeyVersion, kKnownTrustedVaultKeyVersion + 1,
               kKnownTrustedVaultKeyVersion + 2},
              /*signing_keys=*/{{}, kTrustedVaultKey2, kTrustedVaultKey1}));

  EXPECT_THAT(processed_response.status,
              Eq(TrustedVaultRequestStatus::kLocalDataObsolete));
  EXPECT_THAT(processed_response.new_keys, IsEmpty());
}

// In this scenario client already has most recent trusted vault key. It should
// be reported as kLocalDataObsolete, because by issuing the request client
// indicates that there should be new keys and it's possible that key rotation
// has happened by Member state wasn't updated (requires re-registration).
TEST_F(DownloadKeysResponseHandlerTest, ShouldHandleAbsenseOfNewKeys) {
  EXPECT_THAT(handler()
                  .ProcessResponse(
                      /*http_status=*/TrustedVaultRequest::HttpStatus::kSuccess,
                      /*response_body=*/
                      CreateGetSecurityDomainMemberResponseWithSyncMembership(
                          /*trusted_vault_keys=*/{kKnownTrustedVaultKey},
                          /*trusted_vault_keys_versions=*/
                          {kKnownTrustedVaultKeyVersion},
                          /*signing_keys=*/{{}}))
                  .status,
              Eq(TrustedVaultRequestStatus::kLocalDataObsolete));
}

// Tests handling the situation, when response isn't a valid serialized
// SecurityDomainMemberProto proto.
TEST_F(DownloadKeysResponseHandlerTest, ShouldHandleCorruptedResponseProto) {
  EXPECT_THAT(handler()
                  .ProcessResponse(
                      /*http_status=*/TrustedVaultRequest::HttpStatus::kSuccess,
                      /*response_body=*/"corrupted_proto")
                  .status,
              Eq(TrustedVaultRequestStatus::kOtherError));
}

// Client expects that the sync security domain membership exists, but the
// response indicates it doesn't by having no memberships.
TEST_F(DownloadKeysResponseHandlerTest, ShouldHandleAbsenseOfMemberships) {
  EXPECT_THAT(handler()
                  .ProcessResponse(
                      /*http_status=*/TrustedVaultRequest::HttpStatus::kSuccess,
                      /*response_body=*/sync_pb::SecurityDomainMember()
                          .SerializeAsString())
                  .status,
              Eq(TrustedVaultRequestStatus::kLocalDataObsolete));
}

// Same as above, but there is a different security domain membership.
TEST_F(DownloadKeysResponseHandlerTest, ShouldHandleAbsenseOfSyncMembership) {
  sync_pb::SecurityDomainMember member;
  AddSecurityDomainMembership(
      "other_domain", MakeTestKeyPair()->public_key(),
      /*trusted_vault_keys=*/{kTrustedVaultKey1},
      /*trusted_vault_keys_versions=*/{kKnownTrustedVaultKeyVersion + 1},
      /*signing_keys=*/{kKnownTrustedVaultKey}, &member);

  EXPECT_THAT(handler()
                  .ProcessResponse(
                      /*http_status=*/TrustedVaultRequest::HttpStatus::kSuccess,
                      /*response_body=*/member.SerializeAsString())
                  .status,
              Eq(TrustedVaultRequestStatus::kLocalDataObsolete));
}

// Tests handling presence of other security domain memberships.
TEST_F(DownloadKeysResponseHandlerTest, ShouldHandleMultipleSecurityDomains) {
  sync_pb::SecurityDomainMember member;
  AddSecurityDomainMembership(
      "other_domain", MakeTestKeyPair()->public_key(),
      /*trusted_vault_keys=*/{kTrustedVaultKey1},
      /*trusted_vault_keys_versions=*/{kKnownTrustedVaultKeyVersion + 1},
      /*signing_keys=*/{{}}, &member);

  // Note: sync security domain membership is different by having correct
  // rotation proof.
  AddSecurityDomainMembership(
      kSyncSecurityDomainName, MakeTestKeyPair()->public_key(),
      /*trusted_vault_keys=*/{kTrustedVaultKey1},
      /*trusted_vault_keys_versions=*/{kKnownTrustedVaultKeyVersion + 1},
      /*signing_keys=*/{kKnownTrustedVaultKey}, &member);

  const DownloadKeysResponseHandler::ProcessedResponse processed_response =
      handler().ProcessResponse(
          /*http_status=*/TrustedVaultRequest::HttpStatus::kSuccess,
          /*response_body=*/member.SerializeAsString());

  EXPECT_THAT(processed_response.status,
              Eq(TrustedVaultRequestStatus::kSuccess));
  EXPECT_THAT(processed_response.new_keys, ElementsAre(kTrustedVaultKey1));
  EXPECT_THAT(processed_response.last_key_version,
              Eq(kKnownTrustedVaultKeyVersion + 1));
}

// Test scenario, when no trusted vault keys available on the current device
// (e.g. device was registered with constant key). In this case new keys
// shouldn't be validated.
TEST_F(DownloadKeysResponseHandlerTest, ShouldHandleEmptyLastKnownKey) {
  // This test uses custom parameters for the handler ctor, so create new
  // handler instead of using one from the fixture.
  DownloadKeysResponseHandler handler(base::nullopt, MakeTestKeyPair());

  const int kLastKeyVersion = 123;
  const DownloadKeysResponseHandler::ProcessedResponse processed_response =
      handler.ProcessResponse(
          /*http_status=*/TrustedVaultRequest::HttpStatus::kSuccess,
          /*response_body=*/
          CreateGetSecurityDomainMemberResponseWithSyncMembership(
              /*trusted_vault_keys=*/{kTrustedVaultKey1},
              /*trusted_vault_keys_versions=*/{kLastKeyVersion},
              /*signing_keys=*/{{}}));

  EXPECT_THAT(processed_response.status,
              Eq(TrustedVaultRequestStatus::kSuccess));
  EXPECT_THAT(processed_response.new_keys, ElementsAre(kTrustedVaultKey1));
  EXPECT_THAT(processed_response.last_key_version, Eq(kLastKeyVersion));
}

}  // namespace

}  // namespace syncer
