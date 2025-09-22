//===-- sanitizer_allocator_report.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Shared allocator error reporting for ThreadSanitizer, MemorySanitizer, etc.
///
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_ALLOCATOR_REPORT_H
#define SANITIZER_ALLOCATOR_REPORT_H

#include "sanitizer_internal_defs.h"
#include "sanitizer_stacktrace.h"

namespace __sanitizer {

void NORETURN ReportCallocOverflow(uptr count, uptr size,
                                   const StackTrace *stack);
void NORETURN ReportReallocArrayOverflow(uptr count, uptr size,
                                         const StackTrace *stack);
void NORETURN ReportPvallocOverflow(uptr size, const StackTrace *stack);
void NORETURN ReportInvalidAllocationAlignment(uptr alignment,
                                               const StackTrace *stack);
void NORETURN ReportInvalidAlignedAllocAlignment(uptr size, uptr alignment,
                                                 const StackTrace *stack);
void NORETURN ReportInvalidPosixMemalignAlignment(uptr alignment,
                                                  const StackTrace *stack);
void NORETURN ReportAllocationSizeTooBig(uptr user_size, uptr max_size,
                                         const StackTrace *stack);
void NORETURN ReportOutOfMemory(uptr requested_size, const StackTrace *stack);
void NORETURN ReportRssLimitExceeded(const StackTrace *stack);

}  // namespace __sanitizer

#endif  // SANITIZER_ALLOCATOR_REPORT_H
