//===-- fdr_log_writer_test.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a function call tracing system.
//
//===----------------------------------------------------------------------===//
#include <time.h>

#include "test_helpers.h"
#include "xray/xray_records.h"
#include "xray_fdr_log_writer.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Testing/Support/Error.h"
#include "llvm/XRay/Trace.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace __xray {
namespace {

static constexpr size_t kSize = 4096;

using ::llvm::HasValue;
using ::llvm::xray::testing::FuncId;
using ::llvm::xray::testing::RecordType;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::IsNull;

// Exercise the common code path where we initialize a buffer and are able to
// write some records successfully.
TEST(FdrLogWriterTest, WriteSomeRecords) {
  bool Success = false;
  BufferQueue Buffers(kSize, 1, Success);
  BufferQueue::Buffer B;
  ASSERT_EQ(Buffers.getBuffer(B), BufferQueue::ErrorCode::Ok);

  FDRLogWriter Writer(B);
  MetadataRecord Preamble[] = {
      createMetadataRecord<MetadataRecord::RecordKinds::NewBuffer>(int32_t{1}),
      createMetadataRecord<MetadataRecord::RecordKinds::WalltimeMarker>(
          int64_t{1}, int32_t{2}),
      createMetadataRecord<MetadataRecord::RecordKinds::Pid>(int32_t{1}),
  };
  ASSERT_THAT(Writer.writeMetadataRecords(Preamble),
              Eq(sizeof(MetadataRecord) * 3));
  ASSERT_TRUE(Writer.writeMetadata<MetadataRecord::RecordKinds::NewCPUId>(1));
  ASSERT_TRUE(
      Writer.writeFunction(FDRLogWriter::FunctionRecordKind::Enter, 1, 1));
  ASSERT_TRUE(
      Writer.writeFunction(FDRLogWriter::FunctionRecordKind::Exit, 1, 1));
  ASSERT_EQ(Buffers.releaseBuffer(B), BufferQueue::ErrorCode::Ok);
  ASSERT_EQ(B.Data, nullptr);
  ASSERT_EQ(Buffers.finalize(), BufferQueue::ErrorCode::Ok);

  // We then need to go through each element of the Buffers, and re-create a
  // flat buffer that we would see if they were laid out in a file. This also
  // means we need to write out the header manually.
  std::string Serialized = serialize(Buffers, 3);
  llvm::DataExtractor DE(Serialized, true, 8);
  auto TraceOrErr = llvm::xray::loadTrace(DE);
  EXPECT_THAT_EXPECTED(
      TraceOrErr,
      HasValue(ElementsAre(
          AllOf(FuncId(1), RecordType(llvm::xray::RecordTypes::ENTER)),
          AllOf(FuncId(1), RecordType(llvm::xray::RecordTypes::EXIT)))));
}

// Ensure that we can handle buffer re-use.
TEST(FdrLogWriterTest, ReuseBuffers) {
  bool Success = false;
  BufferQueue Buffers(kSize, 1, Success);
  BufferQueue::Buffer B;
  ASSERT_EQ(Buffers.getBuffer(B), BufferQueue::ErrorCode::Ok);

  FDRLogWriter Writer(B);
  MetadataRecord Preamble[] = {
      createMetadataRecord<MetadataRecord::RecordKinds::NewBuffer>(int32_t{1}),
      createMetadataRecord<MetadataRecord::RecordKinds::WalltimeMarker>(
          int64_t{1}, int32_t{2}),
      createMetadataRecord<MetadataRecord::RecordKinds::Pid>(int32_t{1}),
  };

  // First we write the first set of records into the single buffer in the
  // queue which includes one enter and one exit record.
  ASSERT_THAT(Writer.writeMetadataRecords(Preamble),
              Eq(sizeof(MetadataRecord) * 3));
  ASSERT_TRUE(Writer.writeMetadata<MetadataRecord::RecordKinds::NewCPUId>(
      uint16_t{1}, uint64_t{1}));
  uint64_t TSC = 1;
  ASSERT_TRUE(
      Writer.writeFunction(FDRLogWriter::FunctionRecordKind::Enter, 1, TSC++));
  ASSERT_TRUE(
      Writer.writeFunction(FDRLogWriter::FunctionRecordKind::Exit, 1, TSC++));
  ASSERT_EQ(Buffers.releaseBuffer(B), BufferQueue::ErrorCode::Ok);
  ASSERT_THAT(B.Data, IsNull());

  // Then we re-use the buffer, but only write one record.
  ASSERT_EQ(Buffers.getBuffer(B), BufferQueue::ErrorCode::Ok);
  Writer.resetRecord();
  ASSERT_THAT(Writer.writeMetadataRecords(Preamble),
              Eq(sizeof(MetadataRecord) * 3));
  ASSERT_TRUE(Writer.writeMetadata<MetadataRecord::RecordKinds::NewCPUId>(
      uint16_t{1}, uint64_t{1}));
  ASSERT_TRUE(
      Writer.writeFunction(FDRLogWriter::FunctionRecordKind::Enter, 1, TSC++));
  ASSERT_EQ(Buffers.releaseBuffer(B), BufferQueue::ErrorCode::Ok);
  ASSERT_THAT(B.Data, IsNull());
  ASSERT_EQ(Buffers.finalize(), BufferQueue::ErrorCode::Ok);

  // Then we validate that we only see the single enter record.
  std::string Serialized = serialize(Buffers, 3);
  llvm::DataExtractor DE(Serialized, true, 8);
  auto TraceOrErr = llvm::xray::loadTrace(DE);
  EXPECT_THAT_EXPECTED(
      TraceOrErr, HasValue(ElementsAre(AllOf(
                      FuncId(1), RecordType(llvm::xray::RecordTypes::ENTER)))));
}

TEST(FdrLogWriterTest, UnwriteRecords) {
  bool Success = false;
  BufferQueue Buffers(kSize, 1, Success);
  BufferQueue::Buffer B;
  ASSERT_EQ(Buffers.getBuffer(B), BufferQueue::ErrorCode::Ok);

  FDRLogWriter Writer(B);
  MetadataRecord Preamble[] = {
      createMetadataRecord<MetadataRecord::RecordKinds::NewBuffer>(int32_t{1}),
      createMetadataRecord<MetadataRecord::RecordKinds::WalltimeMarker>(
          int64_t{1}, int32_t{2}),
      createMetadataRecord<MetadataRecord::RecordKinds::Pid>(int32_t{1}),
  };
  ASSERT_THAT(Writer.writeMetadataRecords(Preamble),
              Eq(sizeof(MetadataRecord) * 3));
  ASSERT_TRUE(Writer.writeMetadata<MetadataRecord::RecordKinds::NewCPUId>(1));
  ASSERT_TRUE(
      Writer.writeFunction(FDRLogWriter::FunctionRecordKind::Enter, 1, 1));
  ASSERT_TRUE(
      Writer.writeFunction(FDRLogWriter::FunctionRecordKind::Exit, 1, 1));
  Writer.undoWrites(sizeof(FunctionRecord) * 2);
  ASSERT_EQ(Buffers.releaseBuffer(B), BufferQueue::ErrorCode::Ok);
  ASSERT_EQ(B.Data, nullptr);
  ASSERT_EQ(Buffers.finalize(), BufferQueue::ErrorCode::Ok);

  // We've un-done the two function records we've written, and now we expect
  // that we don't have any function records in the trace.
  std::string Serialized = serialize(Buffers, 3);
  llvm::DataExtractor DE(Serialized, true, 8);
  auto TraceOrErr = llvm::xray::loadTrace(DE);
  EXPECT_THAT_EXPECTED(TraceOrErr, HasValue(IsEmpty()));
}

} // namespace
} // namespace __xray
