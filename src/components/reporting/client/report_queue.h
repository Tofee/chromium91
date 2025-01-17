// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_REPORTING_CLIENT_REPORT_QUEUE_H_
#define COMPONENTS_REPORTING_CLIENT_REPORT_QUEUE_H_

#include <memory>
#include <string>
#include <utility>

#include "base/callback.h"
#include "base/values.h"
#include "components/reporting/proto/record.pb.h"
#include "components/reporting/proto/record_constants.pb.h"
#include "components/reporting/util/status.h"
#include "components/reporting/util/statusor.h"
#include "third_party/protobuf/src/google/protobuf/message_lite.h"

namespace reporting {

// A |ReportQueue| is not meant to be created directly, instead it is
// instantiated by |ReportingClient|. |ReportQueue| allows a user
// to |Enqueue| a message for delivery to a handler specified by the
// |Destination| held by the provided |ReportQueueConfiguration|.
// |ReportQueue| implementation handles scheduling storage and
// delivery.
// Enqueue can also be used with a |base::Value| or |std::string|.
//
// Example Usage:
// void SendMessage(google::protobuf::ImportantMessage important_message,
//                  reporting::ReportQueue::EnqueueCallback done_cb) {
//   // Create configuration.
//   auto config_result = reporting::ReportQueueConfiguration::Create(...);
//   // Bail out if configuration failed to create.
//   if (!config_result.ok()) {
//     std::move(done_cb).Run(config_result.status());
//     return;
//   }
//   // Asynchronously instantiate ReportingQueue.
//   base::ThreadPool::PostTask(
//       FROM_HERE,
//       base::BindOnce(
//           [](google::protobuf::ImportantMessage important_message,
//              reporting::ReportQueue::EnqueueCallback done_cb,
//              std::unique_ptr<reporting::ReportQueueConfiguration> config) {
//             reporting::ReportQueueProvider::CreateQueue(
//                 std::move(config),
//                 base::BindOnce(
//                     [](google::protobuf::ImportantMessage important_message,
//                        reporting::ReportQueue::EnqueueCallback done_cb,
//                        reporting::StatusOr<std::unique_ptr<
//                            reporting::ReportQueue>> report_queue_result) {
//                       // Bail out if queue failed to create.
//                       if (!report_queue_result.ok()) {
//                         std::move(done_cb).Run(report_queue_result.status());
//                         return;
//                       }
//                       // Queue created successfully, enqueue the message.
//                       report_queue_result.ValueOrDie()->Enqueue(
//                           std::move(important_message), std::move(done_cb));
//                     },
//                     std::move(important_message), std::move(done_cb)));
//           },
//           std::move(important_message), std::move(done_cb),
//           std::move(config_result.ValueOrDie())));
// }

class ReportQueue {
 public:
  // An EnqueueCallback is called on the completion of any |Enqueue| call.
  using EnqueueCallback = base::OnceCallback<void(Status)>;

  // A FlushCallback is called on the completion of |Flush| call.
  using FlushCallback = base::OnceCallback<void(Status)>;

  virtual ~ReportQueue();

  // Enqueue asynchronously stores and delivers a record.  The |callback| will
  // be called on any errors. If storage is successful |callback| will be called
  // with an OK status.
  //
  // |priority| will Enqueue the record to the specified Priority queue.
  //
  // The current destinations have the following data requirements:
  // (destination : requirement)
  // UPLOAD_EVENTS : UploadEventsRequest
  //
  // |record| will be sent as a string with no conversion.
  void Enqueue(base::StringPiece record,
               Priority priority,
               EnqueueCallback callback) const;

  // |record| will be converted to a JSON string with base::JsonWriter::Write.
  void Enqueue(const base::Value& record,
               Priority priority,
               EnqueueCallback callback) const;

  // |record| will be converted to a string with SerializeToString(). The
  // handler is responsible for converting the record back to a proto with a
  // ParseFromString() call.
  void Enqueue(const google::protobuf::MessageLite* record,
               Priority priority,
               EnqueueCallback callback) const;

  // Initiates upload of collected records according to the priority.
  // Called usually for a queue with an infinite or very large upload period.
  // Multiple |Flush| calls can safely run in parallel.
  // Returns error if cannot start upload.
  virtual void Flush(Priority priority, FlushCallback callback) = 0;

 protected:
  virtual void AddRecord(base::StringPiece record,
                         Priority priority,
                         EnqueueCallback callback) const = 0;
};

}  // namespace reporting

#endif  // COMPONENTS_REPORTING_CLIENT_REPORT_QUEUE_H_
