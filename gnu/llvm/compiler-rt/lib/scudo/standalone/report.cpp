//===-- report.cpp ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "report.h"

#include "atomic_helpers.h"
#include "string_utils.h"

#include <stdarg.h>

namespace scudo {

class ScopedErrorReport {
public:
  ScopedErrorReport() : Message() { Message.append("Scudo ERROR: "); }
  void append(const char *Format, ...) {
    va_list Args;
    va_start(Args, Format);
    Message.vappend(Format, Args);
    va_end(Args);
  }
  NORETURN ~ScopedErrorReport() { reportRawError(Message.data()); }

private:
  ScopedString Message;
};

inline void NORETURN trap() { __builtin_trap(); }

// This could potentially be called recursively if a CHECK fails in the reports.
void NORETURN reportCheckFailed(const char *File, int Line,
                                const char *Condition, u64 Value1, u64 Value2) {
  static atomic_u32 NumberOfCalls;
  if (atomic_fetch_add(&NumberOfCalls, 1, memory_order_relaxed) > 2) {
    // TODO(kostyak): maybe sleep here?
    trap();
  }
  ScopedErrorReport Report;
  Report.append("CHECK failed @ %s:%d %s ((u64)op1=%llu, (u64)op2=%llu)\n",
                File, Line, Condition, Value1, Value2);
}

// Generic string fatal error message.
void NORETURN reportError(const char *Message) {
  ScopedErrorReport Report;
  Report.append("%s\n", Message);
}

// Generic fatal error message without ScopedString.
void NORETURN reportRawError(const char *Message) {
  outputRaw(Message);
  setAbortMessage(Message);
  die();
}

void NORETURN reportInvalidFlag(const char *FlagType, const char *Value) {
  ScopedErrorReport Report;
  Report.append("invalid value for %s option: '%s'\n", FlagType, Value);
}

// The checksum of a chunk header is invalid. This could be caused by an
// {over,under}write of the header, a pointer that is not an actual chunk.
void NORETURN reportHeaderCorruption(void *Ptr) {
  ScopedErrorReport Report;
  Report.append("corrupted chunk header at address %p\n", Ptr);
}

// The allocator was compiled with parameters that conflict with field size
// requirements.
void NORETURN reportSanityCheckError(const char *Field) {
  ScopedErrorReport Report;
  Report.append("maximum possible %s doesn't fit in header\n", Field);
}

// We enforce a maximum alignment, to keep fields smaller and generally prevent
// integer overflows, or unexpected corner cases.
void NORETURN reportAlignmentTooBig(uptr Alignment, uptr MaxAlignment) {
  ScopedErrorReport Report;
  Report.append("invalid allocation alignment: %zu exceeds maximum supported "
                "alignment of %zu\n",
                Alignment, MaxAlignment);
}

// See above, we also enforce a maximum size.
void NORETURN reportAllocationSizeTooBig(uptr UserSize, uptr TotalSize,
                                         uptr MaxSize) {
  ScopedErrorReport Report;
  Report.append("requested allocation size %zu (%zu after adjustments) exceeds "
                "maximum supported size of %zu\n",
                UserSize, TotalSize, MaxSize);
}

void NORETURN reportOutOfBatchClass() {
  ScopedErrorReport Report;
  Report.append("BatchClass region is used up, can't hold any free block\n");
}

void NORETURN reportOutOfMemory(uptr RequestedSize) {
  ScopedErrorReport Report;
  Report.append("out of memory trying to allocate %zu bytes\n", RequestedSize);
}

static const char *stringifyAction(AllocatorAction Action) {
  switch (Action) {
  case AllocatorAction::Recycling:
    return "recycling";
  case AllocatorAction::Deallocating:
    return "deallocating";
  case AllocatorAction::Reallocating:
    return "reallocating";
  case AllocatorAction::Sizing:
    return "sizing";
  }
  return "<invalid action>";
}

// The chunk is not in a state congruent with the operation we want to perform.
// This is usually the case with a double-free, a realloc of a freed pointer.
void NORETURN reportInvalidChunkState(AllocatorAction Action, void *Ptr) {
  ScopedErrorReport Report;
  Report.append("invalid chunk state when %s address %p\n",
                stringifyAction(Action), Ptr);
}

void NORETURN reportMisalignedPointer(AllocatorAction Action, void *Ptr) {
  ScopedErrorReport Report;
  Report.append("misaligned pointer when %s address %p\n",
                stringifyAction(Action), Ptr);
}

// The deallocation function used is at odds with the one used to allocate the
// chunk (eg: new[]/delete or malloc/delete, and so on).
void NORETURN reportDeallocTypeMismatch(AllocatorAction Action, void *Ptr,
                                        u8 TypeA, u8 TypeB) {
  ScopedErrorReport Report;
  Report.append("allocation type mismatch when %s address %p (%d vs %d)\n",
                stringifyAction(Action), Ptr, TypeA, TypeB);
}

// The size specified to the delete operator does not match the one that was
// passed to new when allocating the chunk.
void NORETURN reportDeleteSizeMismatch(void *Ptr, uptr Size,
                                       uptr ExpectedSize) {
  ScopedErrorReport Report;
  Report.append(
      "invalid sized delete when deallocating address %p (%zu vs %zu)\n", Ptr,
      Size, ExpectedSize);
}

void NORETURN reportAlignmentNotPowerOfTwo(uptr Alignment) {
  ScopedErrorReport Report;
  Report.append(
      "invalid allocation alignment: %zu, alignment must be a power of two\n",
      Alignment);
}

void NORETURN reportCallocOverflow(uptr Count, uptr Size) {
  ScopedErrorReport Report;
  Report.append("calloc parameters overflow: count * size (%zu * %zu) cannot "
                "be represented with type size_t\n",
                Count, Size);
}

void NORETURN reportInvalidPosixMemalignAlignment(uptr Alignment) {
  ScopedErrorReport Report;
  Report.append(
      "invalid alignment requested in posix_memalign: %zu, alignment must be a "
      "power of two and a multiple of sizeof(void *) == %zu\n",
      Alignment, sizeof(void *));
}

void NORETURN reportPvallocOverflow(uptr Size) {
  ScopedErrorReport Report;
  Report.append("pvalloc parameters overflow: size %zu rounded up to system "
                "page size %zu cannot be represented in type size_t\n",
                Size, getPageSizeCached());
}

void NORETURN reportInvalidAlignedAllocAlignment(uptr Alignment, uptr Size) {
  ScopedErrorReport Report;
  Report.append("invalid alignment requested in aligned_alloc: %zu, alignment "
                "must be a power of two and the requested size %zu must be a "
                "multiple of alignment\n",
                Alignment, Size);
}

} // namespace scudo
