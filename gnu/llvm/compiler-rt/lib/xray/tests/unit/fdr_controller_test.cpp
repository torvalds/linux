//===-- fdr_controller_test.cpp -------------------------------------------===//
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
#include <algorithm>
#include <memory>
#include <time.h>

#include "test_helpers.h"
#include "xray/xray_records.h"
#include "xray_buffer_queue.h"
#include "xray_fdr_controller.h"
#include "xray_fdr_log_writer.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/Testing/Support/Error.h"
#include "llvm/XRay/Trace.h"
#include "llvm/XRay/XRayRecord.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace __xray {
namespace {

using ::llvm::HasValue;
using ::llvm::xray::testing::FuncId;
using ::llvm::xray::testing::HasArg;
using ::llvm::xray::testing::RecordType;
using ::llvm::xray::testing::TSCIs;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::SizeIs;

class FunctionSequenceTest : public ::testing::Test {
protected:
  BufferQueue::Buffer B{};
  std::unique_ptr<BufferQueue> BQ;
  std::unique_ptr<FDRLogWriter> W;
  std::unique_ptr<FDRController<>> C;

public:
  void SetUp() override {
    bool Success;
    BQ = std::make_unique<BufferQueue>(4096, 1, Success);
    ASSERT_TRUE(Success);
    ASSERT_EQ(BQ->getBuffer(B), BufferQueue::ErrorCode::Ok);
    W = std::make_unique<FDRLogWriter>(B);
    C = std::make_unique<FDRController<>>(BQ.get(), B, *W, clock_gettime, 0);
  }
};

TEST_F(FunctionSequenceTest, DefaultInitFinalizeFlush) {
  ASSERT_TRUE(C->functionEnter(1, 2, 3));
  ASSERT_TRUE(C->functionExit(1, 2, 3));
  ASSERT_TRUE(C->flush());
  ASSERT_EQ(BQ->finalize(), BufferQueue::ErrorCode::Ok);

  // Serialize the buffers then test to see we find the expected records.
  std::string Serialized = serialize(*BQ, 3);
  llvm::DataExtractor DE(Serialized, true, 8);
  auto TraceOrErr = llvm::xray::loadTrace(DE);
  EXPECT_THAT_EXPECTED(
      TraceOrErr,
      HasValue(ElementsAre(
          AllOf(FuncId(1), RecordType(llvm::xray::RecordTypes::ENTER)),
          AllOf(FuncId(1), RecordType(llvm::xray::RecordTypes::EXIT)))));
}

TEST_F(FunctionSequenceTest, BoundaryFuncIdEncoding) {
  // We ensure that we can write function id's that are at the boundary of the
  // acceptable function ids.
  int32_t FId = (1 << 28) - 1;
  uint64_t TSC = 2;
  uint16_t CPU = 1;
  ASSERT_TRUE(C->functionEnter(FId, TSC++, CPU));
  ASSERT_TRUE(C->functionExit(FId, TSC++, CPU));
  ASSERT_TRUE(C->functionEnterArg(FId, TSC++, CPU, 1));
  ASSERT_TRUE(C->functionTailExit(FId, TSC++, CPU));
  ASSERT_TRUE(C->flush());
  ASSERT_EQ(BQ->finalize(), BufferQueue::ErrorCode::Ok);

  // Serialize the buffers then test to see we find the expected records.
  std::string Serialized = serialize(*BQ, 3);
  llvm::DataExtractor DE(Serialized, true, 8);
  auto TraceOrErr = llvm::xray::loadTrace(DE);
  EXPECT_THAT_EXPECTED(
      TraceOrErr,
      HasValue(ElementsAre(
          AllOf(FuncId(FId), RecordType(llvm::xray::RecordTypes::ENTER)),
          AllOf(FuncId(FId), RecordType(llvm::xray::RecordTypes::EXIT)),
          AllOf(FuncId(FId), RecordType(llvm::xray::RecordTypes::ENTER_ARG)),
          AllOf(FuncId(FId), RecordType(llvm::xray::RecordTypes::TAIL_EXIT)))));
}

TEST_F(FunctionSequenceTest, ThresholdsAreEnforced) {
  C = std::make_unique<FDRController<>>(BQ.get(), B, *W, clock_gettime, 1000);
  ASSERT_TRUE(C->functionEnter(1, 2, 3));
  ASSERT_TRUE(C->functionExit(1, 2, 3));
  ASSERT_TRUE(C->flush());
  ASSERT_EQ(BQ->finalize(), BufferQueue::ErrorCode::Ok);

  // Serialize the buffers then test to see we find the *no* records, because
  // the function entry-exit comes under the cycle threshold.
  std::string Serialized = serialize(*BQ, 3);
  llvm::DataExtractor DE(Serialized, true, 8);
  auto TraceOrErr = llvm::xray::loadTrace(DE);
  EXPECT_THAT_EXPECTED(TraceOrErr, HasValue(IsEmpty()));
}

TEST_F(FunctionSequenceTest, ArgsAreHandledAndKept) {
  C = std::make_unique<FDRController<>>(BQ.get(), B, *W, clock_gettime, 1000);
  ASSERT_TRUE(C->functionEnterArg(1, 2, 3, 4));
  ASSERT_TRUE(C->functionExit(1, 2, 3));
  ASSERT_TRUE(C->flush());
  ASSERT_EQ(BQ->finalize(), BufferQueue::ErrorCode::Ok);

  // Serialize the buffers then test to see we find the function enter arg
  // record with the specified argument.
  std::string Serialized = serialize(*BQ, 3);
  llvm::DataExtractor DE(Serialized, true, 8);
  auto TraceOrErr = llvm::xray::loadTrace(DE);
  EXPECT_THAT_EXPECTED(
      TraceOrErr,
      HasValue(ElementsAre(
          AllOf(FuncId(1), RecordType(llvm::xray::RecordTypes::ENTER_ARG),
                HasArg(4)),
          AllOf(FuncId(1), RecordType(llvm::xray::RecordTypes::EXIT)))));
}

TEST_F(FunctionSequenceTest, PreservedCallsHaveCorrectTSC) {
  C = std::make_unique<FDRController<>>(BQ.get(), B, *W, clock_gettime, 1000);
  uint64_t TSC = 1;
  uint16_t CPU = 0;
  ASSERT_TRUE(C->functionEnter(1, TSC++, CPU));
  ASSERT_TRUE(C->functionEnter(2, TSC++, CPU));
  ASSERT_TRUE(C->functionExit(2, TSC++, CPU));
  ASSERT_TRUE(C->functionExit(1, TSC += 1000, CPU));
  ASSERT_TRUE(C->flush());
  ASSERT_EQ(BQ->finalize(), BufferQueue::ErrorCode::Ok);

  // Serialize the buffers then test to see if we find the remaining records,
  // because the function entry-exit comes under the cycle threshold.
  std::string Serialized = serialize(*BQ, 3);
  llvm::DataExtractor DE(Serialized, true, 8);
  auto TraceOrErr = llvm::xray::loadTrace(DE);
  EXPECT_THAT_EXPECTED(
      TraceOrErr,
      HasValue(ElementsAre(
          AllOf(FuncId(1), RecordType(llvm::xray::RecordTypes::ENTER),
                TSCIs(Eq(1uL))),
          AllOf(FuncId(1), RecordType(llvm::xray::RecordTypes::EXIT),
                TSCIs(Gt(1000uL))))));
}

TEST_F(FunctionSequenceTest, PreservedCallsSupportLargeDeltas) {
  C = std::make_unique<FDRController<>>(BQ.get(), B, *W, clock_gettime, 1000);
  uint64_t TSC = 1;
  uint16_t CPU = 0;
  const auto LargeDelta = uint64_t{std::numeric_limits<int32_t>::max()};
  ASSERT_TRUE(C->functionEnter(1, TSC++, CPU));
  ASSERT_TRUE(C->functionExit(1, TSC += LargeDelta, CPU));
  ASSERT_TRUE(C->flush());
  ASSERT_EQ(BQ->finalize(), BufferQueue::ErrorCode::Ok);

  // Serialize the buffer then test to see if we find the right TSC with a large
  // delta.
  std::string Serialized = serialize(*BQ, 3);
  llvm::DataExtractor DE(Serialized, true, 8);
  auto TraceOrErr = llvm::xray::loadTrace(DE);
  EXPECT_THAT_EXPECTED(
      TraceOrErr,
      HasValue(ElementsAre(
          AllOf(FuncId(1), RecordType(llvm::xray::RecordTypes::ENTER),
                TSCIs(Eq(1uL))),
          AllOf(FuncId(1), RecordType(llvm::xray::RecordTypes::EXIT),
                TSCIs(Gt(LargeDelta))))));
}

TEST_F(FunctionSequenceTest, RewindingMultipleCalls) {
  C = std::make_unique<FDRController<>>(BQ.get(), B, *W, clock_gettime, 1000);

  // First we construct an arbitrarily deep function enter/call stack.
  // We also ensure that we are in the same CPU.
  uint64_t TSC = 1;
  uint16_t CPU = 1;
  ASSERT_TRUE(C->functionEnter(1, TSC++, CPU));
  ASSERT_TRUE(C->functionEnter(2, TSC++, CPU));
  ASSERT_TRUE(C->functionEnter(3, TSC++, CPU));

  // Then we exit them one at a time, in reverse order of entry.
  ASSERT_TRUE(C->functionExit(3, TSC++, CPU));
  ASSERT_TRUE(C->functionExit(2, TSC++, CPU));
  ASSERT_TRUE(C->functionExit(1, TSC++, CPU));

  ASSERT_TRUE(C->flush());
  ASSERT_EQ(BQ->finalize(), BufferQueue::ErrorCode::Ok);

  // Serialize the buffers then test to see we find that all the calls have been
  // unwound because all of them are under the cycle counter threshold.
  std::string Serialized = serialize(*BQ, 3);
  llvm::DataExtractor DE(Serialized, true, 8);
  auto TraceOrErr = llvm::xray::loadTrace(DE);
  EXPECT_THAT_EXPECTED(TraceOrErr, HasValue(IsEmpty()));
}

TEST_F(FunctionSequenceTest, RewindingIntermediaryTailExits) {
  C = std::make_unique<FDRController<>>(BQ.get(), B, *W, clock_gettime, 1000);

  // First we construct an arbitrarily deep function enter/call stack.
  // We also ensure that we are in the same CPU.
  uint64_t TSC = 1;
  uint16_t CPU = 1;
  ASSERT_TRUE(C->functionEnter(1, TSC++, CPU));
  ASSERT_TRUE(C->functionEnter(2, TSC++, CPU));
  ASSERT_TRUE(C->functionEnter(3, TSC++, CPU));

  // Next we tail-exit into a new function multiple times.
  ASSERT_TRUE(C->functionTailExit(3, TSC++, CPU));
  ASSERT_TRUE(C->functionEnter(4, TSC++, CPU));
  ASSERT_TRUE(C->functionTailExit(4, TSC++, CPU));
  ASSERT_TRUE(C->functionEnter(5, TSC++, CPU));
  ASSERT_TRUE(C->functionTailExit(5, TSC++, CPU));
  ASSERT_TRUE(C->functionEnter(6, TSC++, CPU));

  // Then we exit them one at a time, in reverse order of entry.
  ASSERT_TRUE(C->functionExit(6, TSC++, CPU));
  ASSERT_TRUE(C->functionExit(2, TSC++, CPU));
  ASSERT_TRUE(C->functionExit(1, TSC++, CPU));
  ASSERT_TRUE(C->flush());
  ASSERT_EQ(BQ->finalize(), BufferQueue::ErrorCode::Ok);

  // Serialize the buffers then test to see we find that all the calls have been
  // unwound because all of them are under the cycle counter threshold.
  std::string Serialized = serialize(*BQ, 3);
  llvm::DataExtractor DE(Serialized, true, 8);
  auto TraceOrErr = llvm::xray::loadTrace(DE);
  EXPECT_THAT_EXPECTED(TraceOrErr, HasValue(IsEmpty()));
}

TEST_F(FunctionSequenceTest, RewindingAfterMigration) {
  C = std::make_unique<FDRController<>>(BQ.get(), B, *W, clock_gettime, 1000);

  // First we construct an arbitrarily deep function enter/call stack.
  // We also ensure that we are in the same CPU.
  uint64_t TSC = 1;
  uint16_t CPU = 1;
  ASSERT_TRUE(C->functionEnter(1, TSC++, CPU));
  ASSERT_TRUE(C->functionEnter(2, TSC++, CPU));
  ASSERT_TRUE(C->functionEnter(3, TSC++, CPU));

  // Next we tail-exit into a new function multiple times.
  ASSERT_TRUE(C->functionTailExit(3, TSC++, CPU));
  ASSERT_TRUE(C->functionEnter(4, TSC++, CPU));
  ASSERT_TRUE(C->functionTailExit(4, TSC++, CPU));

  // But before we enter the next function, we migrate to a different CPU.
  CPU = 2;
  ASSERT_TRUE(C->functionEnter(5, TSC++, CPU));
  ASSERT_TRUE(C->functionTailExit(5, TSC++, CPU));
  ASSERT_TRUE(C->functionEnter(6, TSC++, CPU));

  // Then we exit them one at a time, in reverse order of entry.
  ASSERT_TRUE(C->functionExit(6, TSC++, CPU));
  ASSERT_TRUE(C->functionExit(2, TSC++, CPU));
  ASSERT_TRUE(C->functionExit(1, TSC++, CPU));

  ASSERT_TRUE(C->flush());
  ASSERT_EQ(BQ->finalize(), BufferQueue::ErrorCode::Ok);

  // Serialize buffers then test that we can find all the events that span the
  // CPU migration.
  std::string Serialized = serialize(*BQ, 3);
  llvm::DataExtractor DE(Serialized, true, 8);
  auto TraceOrErr = llvm::xray::loadTrace(DE);
  EXPECT_THAT_EXPECTED(
      TraceOrErr,
      HasValue(ElementsAre(
          AllOf(FuncId(1), RecordType(llvm::xray::RecordTypes::ENTER)),
          AllOf(FuncId(2), RecordType(llvm::xray::RecordTypes::ENTER)),
          AllOf(FuncId(2), RecordType(llvm::xray::RecordTypes::EXIT)),
          AllOf(FuncId(1), RecordType(llvm::xray::RecordTypes::EXIT)))));
}

class BufferManagementTest : public ::testing::Test {
protected:
  BufferQueue::Buffer B{};
  std::unique_ptr<BufferQueue> BQ;
  std::unique_ptr<FDRLogWriter> W;
  std::unique_ptr<FDRController<>> C;

  static constexpr size_t kBuffers = 10;

public:
  void SetUp() override {
    bool Success;
    BQ = std::make_unique<BufferQueue>(sizeof(MetadataRecord) * 5 +
                                            sizeof(FunctionRecord) * 2,
                                        kBuffers, Success);
    ASSERT_TRUE(Success);
    ASSERT_EQ(BQ->getBuffer(B), BufferQueue::ErrorCode::Ok);
    W = std::make_unique<FDRLogWriter>(B);
    C = std::make_unique<FDRController<>>(BQ.get(), B, *W, clock_gettime, 0);
  }
};

constexpr size_t BufferManagementTest::kBuffers;

TEST_F(BufferManagementTest, HandlesOverflow) {
  uint64_t TSC = 1;
  uint16_t CPU = 1;
  for (size_t I = 0; I < kBuffers + 1; ++I) {
    ASSERT_TRUE(C->functionEnter(1, TSC++, CPU));
    ASSERT_TRUE(C->functionExit(1, TSC++, CPU));
  }
  ASSERT_TRUE(C->flush());
  ASSERT_THAT(BQ->finalize(), Eq(BufferQueue::ErrorCode::Ok));

  std::string Serialized = serialize(*BQ, 3);
  llvm::DataExtractor DE(Serialized, true, 8);
  auto TraceOrErr = llvm::xray::loadTrace(DE);
  EXPECT_THAT_EXPECTED(TraceOrErr, HasValue(SizeIs(kBuffers * 2)));
}

TEST_F(BufferManagementTest, HandlesOverflowWithArgs) {
  uint64_t TSC = 1;
  uint16_t CPU = 1;
  uint64_t ARG = 1;
  for (size_t I = 0; I < kBuffers + 1; ++I) {
    ASSERT_TRUE(C->functionEnterArg(1, TSC++, CPU, ARG++));
    ASSERT_TRUE(C->functionExit(1, TSC++, CPU));
  }
  ASSERT_TRUE(C->flush());
  ASSERT_THAT(BQ->finalize(), Eq(BufferQueue::ErrorCode::Ok));

  std::string Serialized = serialize(*BQ, 3);
  llvm::DataExtractor DE(Serialized, true, 8);
  auto TraceOrErr = llvm::xray::loadTrace(DE);
  EXPECT_THAT_EXPECTED(TraceOrErr, HasValue(SizeIs(kBuffers)));
}

TEST_F(BufferManagementTest, HandlesOverflowWithCustomEvents) {
  uint64_t TSC = 1;
  uint16_t CPU = 1;
  int32_t D = 0x9009;
  for (size_t I = 0; I < kBuffers; ++I) {
    ASSERT_TRUE(C->functionEnter(1, TSC++, CPU));
    ASSERT_TRUE(C->functionExit(1, TSC++, CPU));
    ASSERT_TRUE(C->customEvent(TSC++, CPU, &D, sizeof(D)));
  }
  ASSERT_TRUE(C->flush());
  ASSERT_THAT(BQ->finalize(), Eq(BufferQueue::ErrorCode::Ok));

  std::string Serialized = serialize(*BQ, 3);
  llvm::DataExtractor DE(Serialized, true, 8);
  auto TraceOrErr = llvm::xray::loadTrace(DE);

  // We expect to also now count the kBuffers/2 custom event records showing up
  // in the Trace.
  EXPECT_THAT_EXPECTED(TraceOrErr, HasValue(SizeIs(kBuffers + (kBuffers / 2))));
}

TEST_F(BufferManagementTest, HandlesFinalizedBufferQueue) {
  uint64_t TSC = 1;
  uint16_t CPU = 1;

  // First write one function entry.
  ASSERT_TRUE(C->functionEnter(1, TSC++, CPU));

  // Then we finalize the buffer queue, simulating the case where the logging
  // has been finalized.
  ASSERT_EQ(BQ->finalize(), BufferQueue::ErrorCode::Ok);

  // At this point further calls to the controller must fail.
  ASSERT_FALSE(C->functionExit(1, TSC++, CPU));

  // But flushing should succeed.
  ASSERT_TRUE(C->flush());

  // We expect that we'll only be able to find the function enter event, but not
  // the function exit event.
  std::string Serialized = serialize(*BQ, 3);
  llvm::DataExtractor DE(Serialized, true, 8);
  auto TraceOrErr = llvm::xray::loadTrace(DE);
  EXPECT_THAT_EXPECTED(
      TraceOrErr, HasValue(ElementsAre(AllOf(
                      FuncId(1), RecordType(llvm::xray::RecordTypes::ENTER)))));
}

TEST_F(BufferManagementTest, HandlesGenerationalBufferQueue) {
  uint64_t TSC = 1;
  uint16_t CPU = 1;

  ASSERT_TRUE(C->functionEnter(1, TSC++, CPU));
  ASSERT_THAT(BQ->finalize(), Eq(BufferQueue::ErrorCode::Ok));
  ASSERT_THAT(BQ->init(sizeof(MetadataRecord) * 4 + sizeof(FunctionRecord) * 2,
                       kBuffers),
              Eq(BufferQueue::ErrorCode::Ok));
  EXPECT_TRUE(C->functionExit(1, TSC++, CPU));
  ASSERT_TRUE(C->flush());

  // We expect that we will only be able to find the function exit event, but
  // not the function enter event, since we only have information about the new
  // generation of the buffers.
  std::string Serialized = serialize(*BQ, 3);
  llvm::DataExtractor DE(Serialized, true, 8);
  auto TraceOrErr = llvm::xray::loadTrace(DE);
  EXPECT_THAT_EXPECTED(
      TraceOrErr, HasValue(ElementsAre(AllOf(
                      FuncId(1), RecordType(llvm::xray::RecordTypes::EXIT)))));
}

} // namespace
} // namespace __xray
