// Copyright 2020 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/tracing/public/cpp/perfetto/producer_test_utils.h"

#include <deque>
#include <functional>
#include <utility>

#include "base/debug/leak_annotations.h"
#include "base/optional.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/perfetto/include/perfetto/ext/base/utils.h"
#include "third_party/perfetto/include/perfetto/ext/tracing/core/trace_writer.h"
#include "third_party/perfetto/include/perfetto/protozero/root_message.h"

namespace tracing {

namespace {

// For sequences/threads other than our own, we just want to ignore
// any events coming in.
class DummyTraceWriter : public perfetto::TraceWriter {
 public:
  DummyTraceWriter()
      : delegate_(perfetto::base::kPageSize), stream_(&delegate_) {}

  perfetto::TraceWriter::TracePacketHandle NewTracePacket() override {
    stream_.Reset(delegate_.GetNewBuffer());
    trace_packet_.Reset(&stream_);

    return perfetto::TraceWriter::TracePacketHandle(&trace_packet_);
  }

  void Flush(std::function<void()> callback = {}) override {}

  perfetto::WriterID writer_id() const override {
    return perfetto::WriterID(0);
  }

  uint64_t written() const override { return 0u; }

 private:
  protozero::RootMessage<perfetto::protos::pbzero::TracePacket> trace_packet_;
  protozero::ScatteredStreamWriterNullDelegate delegate_;
  protozero::ScatteredStreamWriter stream_;
};

}  // namespace

TestProducerClient::TestProducerClient(
    std::unique_ptr<base::tracing::PerfettoTaskRunner> main_thread_task_runner,
    bool log_only_main_thread)
    : ProducerClient(main_thread_task_runner.get()),
      delegate_(perfetto::base::kPageSize),
      stream_(&delegate_),
      main_thread_task_runner_(std::move(main_thread_task_runner)),
      log_only_main_thread_(log_only_main_thread) {
  trace_packet_.Reset(&stream_);
}

TestProducerClient::~TestProducerClient() = default;

std::unique_ptr<perfetto::TraceWriter> TestProducerClient::CreateTraceWriter(
    perfetto::BufferID target_buffer,
    perfetto::BufferExhaustedPolicy) {
  // We attempt to destroy TraceWriters on thread shutdown in
  // ThreadLocalStorage::Slot, by posting them to the ProducerClient taskrunner,
  // but there's no guarantee that this will succeed if that taskrunner is also
  // shut down.
  ANNOTATE_SCOPED_MEMORY_LEAK;
  if (!log_only_main_thread_ ||
      main_thread_task_runner_->GetOrCreateTaskRunner()
          ->RunsTasksInCurrentSequence()) {
    return std::make_unique<TestTraceWriter>(this);
  } else {
    return std::make_unique<DummyTraceWriter>();
  }
}

void TestProducerClient::FlushPacketIfPossible() {
  // GetNewBuffer() in ScatteredStreamWriterNullDelegate doesn't
  // actually return a new buffer, but rather lets us access the buffer
  // buffer already used by protozero to write the TracePacket into.
  protozero::ContiguousMemoryRange buffer = delegate_.GetNewBuffer();

  uint32_t message_size = trace_packet_.Finalize();
  if (message_size) {
    EXPECT_GE(buffer.size(), message_size);

    auto proto = std::make_unique<perfetto::protos::TracePacket>();
    EXPECT_TRUE(proto->ParseFromArray(buffer.begin, message_size));
    if (proto->has_chrome_events() &&
        proto->chrome_events().metadata().size() > 0) {
      legacy_metadata_packets_.push_back(std::move(proto));
    } else if (proto->has_chrome_metadata()) {
      proto_metadata_packets_.push_back(std::move(proto));
    } else {
      finalized_packets_.push_back(std::move(proto));
    }
  }

  stream_.Reset(buffer);
  trace_packet_.Reset(&stream_);
}

perfetto::protos::pbzero::TracePacket* TestProducerClient::NewTracePacket() {
  FlushPacketIfPossible();

  return &trace_packet_;
}

size_t TestProducerClient::GetFinalizedPacketCount() {
  FlushPacketIfPossible();
  return finalized_packets_.size();
}

const perfetto::protos::TracePacket* TestProducerClient::GetFinalizedPacket(
    size_t packet_index) {
  FlushPacketIfPossible();
  EXPECT_GT(finalized_packets_.size(), packet_index);
  return finalized_packets_[packet_index].get();
}

const google::protobuf::RepeatedPtrField<perfetto::protos::ChromeMetadata>*
TestProducerClient::GetChromeMetadata(size_t packet_index) {
  FlushPacketIfPossible();
  if (legacy_metadata_packets_.empty()) {
    return nullptr;
  }
  EXPECT_GT(legacy_metadata_packets_.size(), packet_index);

  const auto& event_bundle =
      legacy_metadata_packets_[packet_index]->chrome_events();
  return &event_bundle.metadata();
}

const perfetto::protos::ChromeMetadataPacket*
TestProducerClient::GetProtoChromeMetadata(size_t packet_index) {
  FlushPacketIfPossible();
  EXPECT_GT(proto_metadata_packets_.size(), packet_index);
  return &proto_metadata_packets_[packet_index]->chrome_metadata();
}

TestTraceWriter::TestTraceWriter(TestProducerClient* producer_client)
    : producer_client_(producer_client) {}

perfetto::TraceWriter::TracePacketHandle TestTraceWriter::NewTracePacket() {
  return perfetto::TraceWriter::TracePacketHandle(
      producer_client_->NewTracePacket());
}

perfetto::WriterID TestTraceWriter::writer_id() const {
  return perfetto::WriterID(0);
}

uint64_t TestTraceWriter::written() const {
  return 0u;
}

DataSourceTester::DataSourceTester(
    tracing::PerfettoTracedProcess::DataSourceBase* data_source)
    : data_source_(data_source) {
  tracing::PerfettoTracedProcess::ResetTaskRunnerForTesting();
  tracing::PerfettoTracedProcess::GetTaskRunner()->GetOrCreateTaskRunner();
  auto perfetto_wrapper = std::make_unique<base::tracing::PerfettoTaskRunner>(
      base::ThreadTaskRunnerHandle::Get());

  producer_ = std::make_unique<tracing::TestProducerClient>(
      std::move(perfetto_wrapper));
}

DataSourceTester::~DataSourceTester() {
  tracing::PerfettoTracedProcess::ResetTaskRunnerForTesting();
  base::RunLoop().RunUntilIdle();
}

}  // namespace tracing
