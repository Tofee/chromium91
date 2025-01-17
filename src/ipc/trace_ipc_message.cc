// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ipc/trace_ipc_message.h"

#include <stdint.h>

#include "ipc/ipc_message_macros.h"
#include "ipc/ipc_message_start.h"
#include "third_party/perfetto/protos/perfetto/trace/track_event/chrome_legacy_ipc.pbzero.h"

namespace IPC {

using perfetto::protos::pbzero::ChromeLegacyIpc;

void WriteIpcMessageIdAsProtozero(uint32_t message_id,
                                  ChromeLegacyIpc* legacy_ipc) {
  ChromeLegacyIpc::MessageClass message_class =
      ChromeLegacyIpc::CLASS_UNSPECIFIED;
  switch (IPC_MESSAGE_ID_CLASS(message_id)) {
    case AutomationMsgStart:
      message_class = ChromeLegacyIpc::CLASS_AUTOMATION;
      break;
    case TestMsgStart:
      message_class = ChromeLegacyIpc::CLASS_TEST;
      break;
    case WorkerMsgStart:
      message_class = ChromeLegacyIpc::CLASS_WORKER;
      break;
    case NaClMsgStart:
      message_class = ChromeLegacyIpc::CLASS_NACL;
      break;
    case GpuChannelMsgStart:
      message_class = ChromeLegacyIpc::CLASS_GPU_CHANNEL;
      break;
    case MediaMsgStart:
      message_class = ChromeLegacyIpc::CLASS_MEDIA;
      break;
    case PpapiMsgStart:
      message_class = ChromeLegacyIpc::CLASS_PPAPI;
      break;
    case ExtensionMsgStart:
      message_class = ChromeLegacyIpc::CLASS_EXTENSION;
      break;
      break;
    case ChromotingMsgStart:
      message_class = ChromeLegacyIpc::CLASS_CHROMOTING;
      break;
    case NaClHostMsgStart:
      message_class = ChromeLegacyIpc::CLASS_NACL_HOST;
      break;
    case EncryptedMediaMsgStart:
      message_class = ChromeLegacyIpc::CLASS_ENCRYPTED_MEDIA;
      break;
    case GinJavaBridgeMsgStart:
      message_class = ChromeLegacyIpc::CLASS_GIN_JAVA_BRIDGE;
      break;
    case ChromeUtilityPrintingMsgStart:
      message_class = ChromeLegacyIpc::CLASS_CHROME_UTILITY_PRINTING;
      break;
    ///@name USE_NEVA_APPRUNTIME
    ///@{
    case OzoneGpuMsgStart:
      message_class = ChromeLegacyIpc::CLASS_OZONE_GPU;
      break;
    ///@}
    case ExtensionsGuestViewMsgStart:
      message_class = ChromeLegacyIpc::CLASS_EXTENSIONS_GUEST_VIEW;
      break;
    case GuestViewMsgStart:
      message_class = ChromeLegacyIpc::CLASS_GUEST_VIEW;
      break;
    case MediaPlayerDelegateMsgStart:
      message_class = ChromeLegacyIpc::CLASS_MEDIA_PLAYER_DELEGATE;
      break;
    case ExtensionWorkerMsgStart:
      message_class = ChromeLegacyIpc::CLASS_EXTENSION_WORKER;
      break;
  }
  legacy_ipc->set_message_class(message_class);
  legacy_ipc->set_message_line(IPC_MESSAGE_ID_LINE(message_id));
}

}  // namespace IPC
