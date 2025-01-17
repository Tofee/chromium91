// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/cdm/win/media_foundation_cdm.h"

#include <wchar.h>

#include "base/bind.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/mock_callback.h"
#include "base/test/task_environment.h"
#include "media/base/mock_filters.h"
#include "media/base/test_helpers.h"
#include "media/base/win/mf_helpers.h"
#include "media/base/win/mf_mocks.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::IsEmpty;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::StrEq;
using ::testing::StrictMock;
using ::testing::WithoutArgs;

namespace media {

namespace {

const char kSessionId[] = "session_id";
const double kExpirationMs = 123456789.0;
const auto kExpirationTime = base::Time::FromJsTime(kExpirationMs);

std::vector<uint8_t> StringToVector(const std::string& str) {
  return std::vector<uint8_t>(str.begin(), str.end());
}

}  // namespace

using Microsoft::WRL::ComPtr;

class MediaFoundationCdmTest : public testing::Test {
 public:
  MediaFoundationCdmTest()
      : mf_cdm_(MakeComPtr<MockMFCdm>()),
        mf_cdm_session_(MakeComPtr<MockMFCdmSession>()),
        cdm_(base::MakeRefCounted<MediaFoundationCdm>(
            mf_cdm_,
            base::BindRepeating(&MockCdmClient::OnSessionMessage,
                                base::Unretained(&cdm_client_)),
            base::BindRepeating(&MockCdmClient::OnSessionClosed,
                                base::Unretained(&cdm_client_)),
            base::BindRepeating(&MockCdmClient::OnSessionKeysChange,
                                base::Unretained(&cdm_client_)),
            base::BindRepeating(&MockCdmClient::OnSessionExpirationUpdate,
                                base::Unretained(&cdm_client_)))) {}

  ~MediaFoundationCdmTest() override = default;

  void SetGenerateRequestExpectations(
      ComPtr<MockMFCdmSession> mf_cdm_session,
      const char* session_id,
      IMFContentDecryptionModuleSessionCallbacks** mf_cdm_session_callbacks,
      bool expect_message = true) {
    std::vector<uint8_t> license_request = StringToVector("request");

    // Session ID to return. Will be released by |mf_cdm_session_|.
    std::wstring wide_session_id = base::UTF8ToWide(session_id);
    LPWSTR mf_session_id = nullptr;
    ASSERT_SUCCESS(
        CopyCoTaskMemWideString(wide_session_id.data(), &mf_session_id));

    COM_EXPECT_CALL(mf_cdm_session,
                    GenerateRequest(StrEq(L"webm"), NotNull(), _))
        .WillOnce(WithoutArgs([=] {  // Capture local variables by value.
          (*mf_cdm_session_callbacks)
              ->KeyMessage(MF_MEDIAKEYSESSION_MESSAGETYPE_LICENSE_REQUEST,
                           license_request.data(), license_request.size(),
                           nullptr);
          return S_OK;
        }));

    COM_EXPECT_CALL(mf_cdm_session, GetSessionId(_))
        .WillOnce(DoAll(SetArgPointee<0>(mf_session_id), Return(S_OK)));

    if (expect_message) {
      EXPECT_CALL(cdm_client_,
                  OnSessionMessage(session_id, CdmMessageType::LICENSE_REQUEST,
                                   license_request));
    }
  }

  void CreateSessionAndGenerateRequest() {
    std::vector<uint8_t> init_data = StringToVector("init_data");

    COM_EXPECT_CALL(mf_cdm_,
                    CreateSession(MF_MEDIAKEYSESSION_TYPE_TEMPORARY, _, _))
        .WillOnce(DoAll(SaveComPtr<1>(&mf_cdm_session_callbacks_),
                        SetComPointee<2>(mf_cdm_session_.Get()), Return(S_OK)));

    SetGenerateRequestExpectations(mf_cdm_session_, kSessionId,
                                   &mf_cdm_session_callbacks_);

    cdm_->CreateSessionAndGenerateRequest(
        CdmSessionType::kTemporary, EmeInitDataType::WEBM, init_data,
        std::make_unique<MockCdmSessionPromise>(/*expect_success=*/true,
                                                &session_id_));

    task_environment_.RunUntilIdle();
    EXPECT_EQ(session_id_, kSessionId);
  }

 protected:
  base::test::TaskEnvironment task_environment_;

  StrictMock<MockCdmClient> cdm_client_;
  ComPtr<MockMFCdm> mf_cdm_;
  ComPtr<MockMFCdmSession> mf_cdm_session_;
  ComPtr<IMFContentDecryptionModuleSessionCallbacks> mf_cdm_session_callbacks_;
  scoped_refptr<ContentDecryptionModule> cdm_;
  std::string session_id_;
};

TEST_F(MediaFoundationCdmTest, SetServerCertificate) {
  std::vector<uint8_t> certificate = StringToVector("certificate");
  COM_EXPECT_CALL(mf_cdm_,
                  SetServerCertificate(certificate.data(), certificate.size()))
      .WillOnce(Return(S_OK));

  cdm_->SetServerCertificate(
      certificate, std::make_unique<MockCdmPromise>(/*expect_success=*/true));
}

TEST_F(MediaFoundationCdmTest, SetServerCertificate_Failure) {
  std::vector<uint8_t> certificate = StringToVector("certificate");
  COM_EXPECT_CALL(mf_cdm_,
                  SetServerCertificate(certificate.data(), certificate.size()))
      .WillOnce(Return(E_FAIL));

  cdm_->SetServerCertificate(
      certificate, std::make_unique<MockCdmPromise>(/*expect_success=*/false));
}

TEST_F(MediaFoundationCdmTest, CreateSessionAndGenerateRequest) {
  CreateSessionAndGenerateRequest();
}

// Tests the case where two sessions are being created in parallel.
TEST_F(MediaFoundationCdmTest, CreateSessionAndGenerateRequest_Parallel) {
  std::vector<uint8_t> init_data = StringToVector("init_data");
  const char kSessionId1[] = "session_id_1";
  const char kSessionId2[] = "session_id_2";

  auto mf_cdm_session_1 = MakeComPtr<MockMFCdmSession>();
  auto mf_cdm_session_2 = MakeComPtr<MockMFCdmSession>();
  ComPtr<IMFContentDecryptionModuleSessionCallbacks> mf_cdm_session_callbacks_1;
  ComPtr<IMFContentDecryptionModuleSessionCallbacks> mf_cdm_session_callbacks_2;

  COM_EXPECT_CALL(mf_cdm_,
                  CreateSession(MF_MEDIAKEYSESSION_TYPE_TEMPORARY, _, _))
      .WillOnce(DoAll(SaveComPtr<1>(&mf_cdm_session_callbacks_1),
                      SetComPointee<2>(mf_cdm_session_1.Get()), Return(S_OK)))
      .WillOnce(DoAll(SaveComPtr<1>(&mf_cdm_session_callbacks_2),
                      SetComPointee<2>(mf_cdm_session_2.Get()), Return(S_OK)));

  SetGenerateRequestExpectations(mf_cdm_session_1, kSessionId1,
                                 &mf_cdm_session_callbacks_1);
  SetGenerateRequestExpectations(mf_cdm_session_2, kSessionId2,
                                 &mf_cdm_session_callbacks_2);

  std::string session_id_1;
  std::string session_id_2;
  cdm_->CreateSessionAndGenerateRequest(
      CdmSessionType::kTemporary, EmeInitDataType::WEBM, init_data,
      std::make_unique<MockCdmSessionPromise>(/*expect_success=*/true,
                                              &session_id_1));
  cdm_->CreateSessionAndGenerateRequest(
      CdmSessionType::kTemporary, EmeInitDataType::WEBM, init_data,
      std::make_unique<MockCdmSessionPromise>(/*expect_success=*/true,
                                              &session_id_2));

  task_environment_.RunUntilIdle();
  EXPECT_EQ(session_id_1, kSessionId1);
  EXPECT_EQ(session_id_2, kSessionId2);
}

TEST_F(MediaFoundationCdmTest,
       CreateSessionAndGenerateRequest_CreateSessionFailure) {
  std::vector<uint8_t> init_data = StringToVector("init_data");
  COM_EXPECT_CALL(mf_cdm_,
                  CreateSession(MF_MEDIAKEYSESSION_TYPE_TEMPORARY, _, _))
      .WillOnce(Return(E_FAIL));

  cdm_->CreateSessionAndGenerateRequest(
      CdmSessionType::kTemporary, EmeInitDataType::WEBM, init_data,
      std::make_unique<MockCdmSessionPromise>(/*expect_success=*/false,
                                              &session_id_));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(session_id_.empty());
}

TEST_F(MediaFoundationCdmTest,
       CreateSessionAndGenerateRequest_GenerateRequestFailure) {
  std::vector<uint8_t> init_data = StringToVector("init_data");

  COM_EXPECT_CALL(mf_cdm_,
                  CreateSession(MF_MEDIAKEYSESSION_TYPE_TEMPORARY, _, _))
      .WillOnce(DoAll(SaveComPtr<1>(&mf_cdm_session_callbacks_),
                      SetComPointee<2>(mf_cdm_session_.Get()), Return(S_OK)));

  COM_EXPECT_CALL(mf_cdm_session_,
                  GenerateRequest(StrEq(L"webm"), NotNull(), init_data.size()))
      .WillOnce(Return(E_FAIL));

  cdm_->CreateSessionAndGenerateRequest(
      CdmSessionType::kTemporary, EmeInitDataType::WEBM, init_data,
      std::make_unique<MockCdmSessionPromise>(/*expect_success=*/false,
                                              &session_id_));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(session_id_.empty());
}

// Duplicate session IDs cause session creation failure.
TEST_F(MediaFoundationCdmTest,
       CreateSessionAndGenerateRequest_DuplicateSessionId) {
  std::vector<uint8_t> init_data = StringToVector("init_data");

  auto mf_cdm_session_1 = MakeComPtr<MockMFCdmSession>();
  auto mf_cdm_session_2 = MakeComPtr<MockMFCdmSession>();
  ComPtr<IMFContentDecryptionModuleSessionCallbacks> mf_cdm_session_callbacks_1;
  ComPtr<IMFContentDecryptionModuleSessionCallbacks> mf_cdm_session_callbacks_2;

  COM_EXPECT_CALL(mf_cdm_,
                  CreateSession(MF_MEDIAKEYSESSION_TYPE_TEMPORARY, _, _))
      .WillOnce(DoAll(SaveComPtr<1>(&mf_cdm_session_callbacks_1),
                      SetComPointee<2>(mf_cdm_session_1.Get()), Return(S_OK)))
      .WillOnce(DoAll(SaveComPtr<1>(&mf_cdm_session_callbacks_2),
                      SetComPointee<2>(mf_cdm_session_2.Get()), Return(S_OK)));

  // In both sessions we return kSessionId. Session 1 succeeds. Session 2 fails
  // because of duplicate session ID.
  SetGenerateRequestExpectations(mf_cdm_session_1, kSessionId,
                                 &mf_cdm_session_callbacks_1);
  SetGenerateRequestExpectations(mf_cdm_session_2, kSessionId,
                                 &mf_cdm_session_callbacks_2,
                                 /*expect_message=*/false);
  std::string session_id_1;
  std::string session_id_2;
  cdm_->CreateSessionAndGenerateRequest(
      CdmSessionType::kTemporary, EmeInitDataType::WEBM, init_data,
      std::make_unique<MockCdmSessionPromise>(/*expect_success=*/true,
                                              &session_id_1));
  cdm_->CreateSessionAndGenerateRequest(
      CdmSessionType::kTemporary, EmeInitDataType::WEBM, init_data,
      std::make_unique<MockCdmSessionPromise>(/*expect_success=*/false,
                                              &session_id_2));

  task_environment_.RunUntilIdle();
  EXPECT_EQ(session_id_1, kSessionId);
  EXPECT_TRUE(session_id_2.empty());
}

// LoadSession() is not implemented.
TEST_F(MediaFoundationCdmTest, LoadSession) {
  cdm_->LoadSession(CdmSessionType::kPersistentLicense, kSessionId,
                    std::make_unique<MockCdmSessionPromise>(
                        /*expect_success=*/false, &session_id_));

  task_environment_.RunUntilIdle();
  EXPECT_TRUE(session_id_.empty());
}

TEST_F(MediaFoundationCdmTest, UpdateSession) {
  CreateSessionAndGenerateRequest();

  std::vector<uint8_t> response = StringToVector("response");
  COM_EXPECT_CALL(mf_cdm_session_, Update(NotNull(), response.size()))
      .WillOnce(DoAll([&] { mf_cdm_session_callbacks_->KeyStatusChanged(); },
                      Return(S_OK)));
  COM_EXPECT_CALL(mf_cdm_session_, GetKeyStatuses(_, _)).WillOnce(Return(S_OK));
  COM_EXPECT_CALL(mf_cdm_session_, GetExpiration(_))
      .WillOnce(DoAll(SetArgPointee<0>(kExpirationMs), Return(S_OK)));
  EXPECT_CALL(cdm_client_, OnSessionKeysChangeCalled(_, true));
  EXPECT_CALL(cdm_client_, OnSessionExpirationUpdate(_, kExpirationTime));

  cdm_->UpdateSession(
      session_id_, response,
      std::make_unique<MockCdmPromise>(/*expect_success=*/true));
  task_environment_.RunUntilIdle();
}

TEST_F(MediaFoundationCdmTest, UpdateSession_InvalidSessionId) {
  CreateSessionAndGenerateRequest();

  std::vector<uint8_t> response = StringToVector("response");
  cdm_->UpdateSession(
      "invalid_session_id", response,
      std::make_unique<MockCdmPromise>(/*expect_success=*/false));
  task_environment_.RunUntilIdle();
}

TEST_F(MediaFoundationCdmTest, UpdateSession_Failure) {
  CreateSessionAndGenerateRequest();

  std::vector<uint8_t> response = StringToVector("response");
  COM_EXPECT_CALL(mf_cdm_session_, Update(NotNull(), response.size()))
      .WillOnce(Return(E_FAIL));

  cdm_->UpdateSession(
      session_id_, response,
      std::make_unique<MockCdmPromise>(/*expect_success=*/false));
  task_environment_.RunUntilIdle();
}

TEST_F(MediaFoundationCdmTest, CloseSession) {
  CreateSessionAndGenerateRequest();

  COM_EXPECT_CALL(mf_cdm_session_, Close()).WillOnce(Return(S_OK));
  EXPECT_CALL(cdm_client_, OnSessionClosed(kSessionId));

  cdm_->CloseSession(session_id_,
                     std::make_unique<MockCdmPromise>(/*expect_success=*/true));
  task_environment_.RunUntilIdle();
}

TEST_F(MediaFoundationCdmTest, CloseSession_Failure) {
  CreateSessionAndGenerateRequest();

  COM_EXPECT_CALL(mf_cdm_session_, Close()).WillOnce(Return(E_FAIL));

  cdm_->CloseSession(
      session_id_, std::make_unique<MockCdmPromise>(/*expect_success=*/false));
  task_environment_.RunUntilIdle();
}

TEST_F(MediaFoundationCdmTest, RemoveSession) {
  CreateSessionAndGenerateRequest();

  COM_EXPECT_CALL(mf_cdm_session_, Remove()).WillOnce(Return(S_OK));
  COM_EXPECT_CALL(mf_cdm_session_, GetExpiration(_))
      .WillOnce(DoAll(SetArgPointee<0>(kExpirationMs), Return(S_OK)));
  EXPECT_CALL(cdm_client_, OnSessionExpirationUpdate(_, kExpirationTime));

  cdm_->RemoveSession(
      session_id_, std::make_unique<MockCdmPromise>(/*expect_success=*/true));
  task_environment_.RunUntilIdle();
}

TEST_F(MediaFoundationCdmTest, RemoveSession_Failure) {
  CreateSessionAndGenerateRequest();

  COM_EXPECT_CALL(mf_cdm_session_, Remove()).WillOnce(Return(E_FAIL));

  cdm_->RemoveSession(
      session_id_, std::make_unique<MockCdmPromise>(/*expect_success=*/false));
  task_environment_.RunUntilIdle();
}

}  // namespace media
