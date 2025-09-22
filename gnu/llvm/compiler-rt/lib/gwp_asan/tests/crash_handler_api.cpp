//===-- crash_handler_api.cpp -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/crash_handler.h"
#include "gwp_asan/guarded_pool_allocator.h"
#include "gwp_asan/stack_trace_compressor.h"
#include "gwp_asan/tests/harness.h"

using Error = gwp_asan::Error;
using GuardedPoolAllocator = gwp_asan::GuardedPoolAllocator;
using AllocationMetadata = gwp_asan::AllocationMetadata;
using AllocatorState = gwp_asan::AllocatorState;

class CrashHandlerAPITest : public ::testing::Test {
public:
  void SetUp() override { setupState(); }

protected:
  size_t metadata(uintptr_t Addr, uintptr_t Size, bool IsDeallocated) {
    // Should only be allocating the 0x3000, 0x5000, 0x7000, 0x9000 pages.
    EXPECT_GE(Addr, 0x3000u);
    EXPECT_LT(Addr, 0xa000u);

    size_t Slot = State.getNearestSlot(Addr);

    Metadata[Slot].Addr = Addr;
    Metadata[Slot].RequestedSize = Size;
    Metadata[Slot].IsDeallocated = IsDeallocated;
    Metadata[Slot].AllocationTrace.ThreadID = 123;
    Metadata[Slot].DeallocationTrace.ThreadID = 321;
    setupBacktraces(&Metadata[Slot]);

    return Slot;
  }

  void setupState() {
    State.GuardedPagePool = 0x2000;
    State.GuardedPagePoolEnd = 0xc000;
    InternalFaultAddr = State.GuardedPagePoolEnd - 0x10;
    State.MaxSimultaneousAllocations = 4; // 0x3000, 0x5000, 0x7000, 0x9000.
    State.PageSize = 0x1000;
  }

  void setupBacktraces(AllocationMetadata *Meta) {
    Meta->AllocationTrace.TraceSize = gwp_asan::compression::pack(
        BacktraceConstants, kNumBacktraceConstants,
        Meta->AllocationTrace.CompressedTrace,
        AllocationMetadata::kStackFrameStorageBytes);

    if (Meta->IsDeallocated)
      Meta->DeallocationTrace.TraceSize = gwp_asan::compression::pack(
          BacktraceConstants, kNumBacktraceConstants,
          Meta->DeallocationTrace.CompressedTrace,
          AllocationMetadata::kStackFrameStorageBytes);
  }

  void checkBacktrace(const AllocationMetadata *Meta, bool IsDeallocated) {
    uintptr_t Buffer[kNumBacktraceConstants];
    size_t NumBacktraceConstants = kNumBacktraceConstants;
    EXPECT_EQ(NumBacktraceConstants, __gwp_asan_get_allocation_trace(
                                         Meta, Buffer, kNumBacktraceConstants));
    for (size_t i = 0; i < kNumBacktraceConstants; ++i)
      EXPECT_EQ(Buffer[i], BacktraceConstants[i]);

    if (IsDeallocated) {
      EXPECT_EQ(NumBacktraceConstants,
                __gwp_asan_get_deallocation_trace(Meta, Buffer,
                                                  kNumBacktraceConstants));
      for (size_t i = 0; i < kNumBacktraceConstants; ++i)
        EXPECT_EQ(Buffer[i], BacktraceConstants[i]);
    }
  }

  void checkMetadata(size_t Index, uintptr_t ErrorPtr) {
    const AllocationMetadata *Meta =
        __gwp_asan_get_metadata(&State, Metadata, ErrorPtr);
    EXPECT_NE(nullptr, Meta);
    EXPECT_EQ(Metadata[Index].Addr, __gwp_asan_get_allocation_address(Meta));
    EXPECT_EQ(Metadata[Index].RequestedSize,
              __gwp_asan_get_allocation_size(Meta));
    EXPECT_EQ(Metadata[Index].AllocationTrace.ThreadID,
              __gwp_asan_get_allocation_thread_id(Meta));

    bool IsDeallocated = __gwp_asan_is_deallocated(Meta);
    EXPECT_EQ(Metadata[Index].IsDeallocated, IsDeallocated);
    checkBacktrace(Meta, IsDeallocated);

    if (!IsDeallocated)
      return;

    EXPECT_EQ(Metadata[Index].DeallocationTrace.ThreadID,
              __gwp_asan_get_deallocation_thread_id(Meta));
  }

  static constexpr size_t kNumBacktraceConstants = 4;
  static uintptr_t BacktraceConstants[kNumBacktraceConstants];
  AllocatorState State = {};
  AllocationMetadata Metadata[4] = {};
  uintptr_t InternalFaultAddr;
};

uintptr_t CrashHandlerAPITest::BacktraceConstants[kNumBacktraceConstants] = {
    0xdeadbeef, 0xdeadc0de, 0xbadc0ffe, 0xcafef00d};

TEST_F(CrashHandlerAPITest, PointerNotMine) {
  uintptr_t UnknownPtr = reinterpret_cast<uintptr_t>(&State);

  EXPECT_FALSE(__gwp_asan_error_is_mine(&State, 0));
  EXPECT_FALSE(__gwp_asan_error_is_mine(&State, UnknownPtr));

  EXPECT_EQ(Error::UNKNOWN, __gwp_asan_diagnose_error(&State, Metadata, 0));
  EXPECT_EQ(Error::UNKNOWN,
            __gwp_asan_diagnose_error(&State, Metadata, UnknownPtr));

  EXPECT_EQ(nullptr, __gwp_asan_get_metadata(&State, Metadata, 0));
  EXPECT_EQ(nullptr, __gwp_asan_get_metadata(&State, Metadata, UnknownPtr));
}

TEST_F(CrashHandlerAPITest, PointerNotAllocated) {
  uintptr_t FailureAddress = 0x9000;

  EXPECT_TRUE(__gwp_asan_error_is_mine(&State, FailureAddress));
  EXPECT_EQ(Error::UNKNOWN,
            __gwp_asan_diagnose_error(&State, Metadata, FailureAddress));
  EXPECT_EQ(0u, __gwp_asan_get_internal_crash_address(&State, FailureAddress));
  EXPECT_EQ(nullptr, __gwp_asan_get_metadata(&State, Metadata, FailureAddress));
}

TEST_F(CrashHandlerAPITest, DoubleFree) {
  size_t Index =
      metadata(/* Addr */ 0x7000, /* Size */ 0x20, /* IsDeallocated */ true);
  uintptr_t FailureAddress = 0x7000;

  State.FailureType = Error::DOUBLE_FREE;
  State.FailureAddress = FailureAddress;

  EXPECT_TRUE(__gwp_asan_error_is_mine(&State));
  EXPECT_EQ(Error::DOUBLE_FREE,
            __gwp_asan_diagnose_error(&State, Metadata, 0x0));
  EXPECT_EQ(FailureAddress,
            __gwp_asan_get_internal_crash_address(&State, InternalFaultAddr));
  checkMetadata(Index, FailureAddress);
}

TEST_F(CrashHandlerAPITest, InvalidFree) {
  size_t Index =
      metadata(/* Addr */ 0x7000, /* Size */ 0x20, /* IsDeallocated */ false);
  uintptr_t FailureAddress = 0x7001;

  State.FailureType = Error::INVALID_FREE;
  State.FailureAddress = FailureAddress;

  EXPECT_TRUE(__gwp_asan_error_is_mine(&State));
  EXPECT_EQ(Error::INVALID_FREE,
            __gwp_asan_diagnose_error(&State, Metadata, 0x0));
  EXPECT_EQ(FailureAddress,
            __gwp_asan_get_internal_crash_address(&State, InternalFaultAddr));
  checkMetadata(Index, FailureAddress);
}

TEST_F(CrashHandlerAPITest, InvalidFreeNoMetadata) {
  uintptr_t FailureAddress = 0x7001;

  State.FailureType = Error::INVALID_FREE;
  State.FailureAddress = FailureAddress;

  EXPECT_TRUE(__gwp_asan_error_is_mine(&State));
  EXPECT_EQ(Error::INVALID_FREE,
            __gwp_asan_diagnose_error(&State, Metadata, 0x0));
  EXPECT_EQ(FailureAddress,
            __gwp_asan_get_internal_crash_address(&State, InternalFaultAddr));
  EXPECT_EQ(nullptr, __gwp_asan_get_metadata(&State, Metadata, FailureAddress));
}

TEST_F(CrashHandlerAPITest, UseAfterFree) {
  size_t Index =
      metadata(/* Addr */ 0x7000, /* Size */ 0x20, /* IsDeallocated */ true);
  uintptr_t FailureAddress = 0x7001;

  EXPECT_TRUE(__gwp_asan_error_is_mine(&State, FailureAddress));
  EXPECT_EQ(Error::USE_AFTER_FREE,
            __gwp_asan_diagnose_error(&State, Metadata, FailureAddress));
  EXPECT_EQ(0u, __gwp_asan_get_internal_crash_address(&State, FailureAddress));
  checkMetadata(Index, FailureAddress);
}

TEST_F(CrashHandlerAPITest, BufferOverflow) {
  size_t Index =
      metadata(/* Addr */ 0x5f00, /* Size */ 0x100, /* IsDeallocated */ false);
  uintptr_t FailureAddress = 0x6000;

  EXPECT_TRUE(__gwp_asan_error_is_mine(&State, FailureAddress));
  EXPECT_EQ(Error::BUFFER_OVERFLOW,
            __gwp_asan_diagnose_error(&State, Metadata, FailureAddress));
  EXPECT_EQ(0u, __gwp_asan_get_internal_crash_address(&State, FailureAddress));
  checkMetadata(Index, FailureAddress);
}

TEST_F(CrashHandlerAPITest, BufferUnderflow) {
  size_t Index =
      metadata(/* Addr */ 0x3000, /* Size */ 0x10, /* IsDeallocated*/ false);
  uintptr_t FailureAddress = 0x2fff;

  EXPECT_TRUE(__gwp_asan_error_is_mine(&State, FailureAddress));
  EXPECT_EQ(Error::BUFFER_UNDERFLOW,
            __gwp_asan_diagnose_error(&State, Metadata, FailureAddress));
  EXPECT_EQ(0u, __gwp_asan_get_internal_crash_address(&State, FailureAddress));
  checkMetadata(Index, FailureAddress);
}
