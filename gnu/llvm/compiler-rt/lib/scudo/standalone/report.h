//===-- report.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef SCUDO_REPORT_H_
#define SCUDO_REPORT_H_

#include "internal_defs.h"

namespace scudo {

// Reports are *fatal* unless stated otherwise.

// Generic error, adds newline to end of message.
void NORETURN reportError(const char *Message);

// Generic error, but the message is not modified.
void NORETURN reportRawError(const char *Message);

// Flags related errors.
void NORETURN reportInvalidFlag(const char *FlagType, const char *Value);

// Chunk header related errors.
void NORETURN reportHeaderCorruption(void *Ptr);

// Sanity checks related error.
void NORETURN reportSanityCheckError(const char *Field);

// Combined allocator errors.
void NORETURN reportAlignmentTooBig(uptr Alignment, uptr MaxAlignment);
void NORETURN reportAllocationSizeTooBig(uptr UserSize, uptr TotalSize,
                                         uptr MaxSize);
void NORETURN reportOutOfBatchClass();
void NORETURN reportOutOfMemory(uptr RequestedSize);
enum class AllocatorAction : u8 {
  Recycling,
  Deallocating,
  Reallocating,
  Sizing,
};
void NORETURN reportInvalidChunkState(AllocatorAction Action, void *Ptr);
void NORETURN reportMisalignedPointer(AllocatorAction Action, void *Ptr);
void NORETURN reportDeallocTypeMismatch(AllocatorAction Action, void *Ptr,
                                        u8 TypeA, u8 TypeB);
void NORETURN reportDeleteSizeMismatch(void *Ptr, uptr Size, uptr ExpectedSize);

// C wrappers errors.
void NORETURN reportAlignmentNotPowerOfTwo(uptr Alignment);
void NORETURN reportInvalidPosixMemalignAlignment(uptr Alignment);
void NORETURN reportCallocOverflow(uptr Count, uptr Size);
void NORETURN reportPvallocOverflow(uptr Size);
void NORETURN reportInvalidAlignedAllocAlignment(uptr Size, uptr Alignment);

} // namespace scudo

#endif // SCUDO_REPORT_H_
