//===-- common.cpp ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/common.h"
#include "gwp_asan/stack_trace_compressor.h"

#include <assert.h>

using AllocationMetadata = gwp_asan::AllocationMetadata;
using Error = gwp_asan::Error;

namespace gwp_asan {

const char *ErrorToString(const Error &E) {
  switch (E) {
  case Error::UNKNOWN:
    return "Unknown";
  case Error::USE_AFTER_FREE:
    return "Use After Free";
  case Error::DOUBLE_FREE:
    return "Double Free";
  case Error::INVALID_FREE:
    return "Invalid (Wild) Free";
  case Error::BUFFER_OVERFLOW:
    return "Buffer Overflow";
  case Error::BUFFER_UNDERFLOW:
    return "Buffer Underflow";
  }
  __builtin_trap();
}

constexpr size_t AllocationMetadata::kStackFrameStorageBytes;
constexpr size_t AllocationMetadata::kMaxTraceLengthToCollect;

void AllocationMetadata::RecordAllocation(uintptr_t AllocAddr,
                                          size_t AllocSize) {
  Addr = AllocAddr;
  RequestedSize = AllocSize;
  IsDeallocated = false;

  AllocationTrace.ThreadID = getThreadID();
  DeallocationTrace.TraceSize = 0;
  DeallocationTrace.ThreadID = kInvalidThreadID;
}

void AllocationMetadata::RecordDeallocation() {
  IsDeallocated = true;
  DeallocationTrace.ThreadID = getThreadID();
}

void AllocationMetadata::CallSiteInfo::RecordBacktrace(
    options::Backtrace_t Backtrace) {
  TraceSize = 0;
  if (!Backtrace)
    return;

  uintptr_t UncompressedBuffer[kMaxTraceLengthToCollect];
  size_t BacktraceLength =
      Backtrace(UncompressedBuffer, kMaxTraceLengthToCollect);
  // Backtrace() returns the number of available frames, which may be greater
  // than the number of frames in the buffer. In this case, we need to only pack
  // the number of frames that are in the buffer.
  if (BacktraceLength > kMaxTraceLengthToCollect)
    BacktraceLength = kMaxTraceLengthToCollect;
  TraceSize =
      compression::pack(UncompressedBuffer, BacktraceLength, CompressedTrace,
                        AllocationMetadata::kStackFrameStorageBytes);
}

size_t AllocatorState::maximumAllocationSize() const { return PageSize; }

uintptr_t AllocatorState::slotToAddr(size_t N) const {
  return GuardedPagePool + (PageSize * (1 + N)) + (maximumAllocationSize() * N);
}

bool AllocatorState::isGuardPage(uintptr_t Ptr) const {
  assert(pointerIsMine(reinterpret_cast<void *>(Ptr)));
  size_t PageOffsetFromPoolStart = (Ptr - GuardedPagePool) / PageSize;
  size_t PagesPerSlot = maximumAllocationSize() / PageSize;
  return (PageOffsetFromPoolStart % (PagesPerSlot + 1)) == 0;
}

static size_t addrToSlot(const AllocatorState *State, uintptr_t Ptr) {
  size_t ByteOffsetFromPoolStart = Ptr - State->GuardedPagePool;
  return ByteOffsetFromPoolStart /
         (State->maximumAllocationSize() + State->PageSize);
}

size_t AllocatorState::getNearestSlot(uintptr_t Ptr) const {
  if (Ptr <= GuardedPagePool + PageSize)
    return 0;
  if (Ptr > GuardedPagePoolEnd - PageSize)
    return MaxSimultaneousAllocations - 1;

  if (!isGuardPage(Ptr))
    return addrToSlot(this, Ptr);

  if (Ptr % PageSize <= PageSize / 2)
    return addrToSlot(this, Ptr - PageSize); // Round down.
  return addrToSlot(this, Ptr + PageSize);   // Round up.
}

uintptr_t AllocatorState::internallyDetectedErrorFaultAddress() const {
  return GuardedPagePoolEnd - 0x10;
}

} // namespace gwp_asan
