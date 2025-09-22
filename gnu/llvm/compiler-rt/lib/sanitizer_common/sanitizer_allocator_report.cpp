//===-- sanitizer_allocator_report.cpp --------------------------*- C++ -*-===//
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

#include "sanitizer_allocator.h"
#include "sanitizer_allocator_report.h"
#include "sanitizer_common.h"
#include "sanitizer_report_decorator.h"

namespace __sanitizer {

class ScopedAllocatorErrorReport {
 public:
  ScopedAllocatorErrorReport(const char *error_summary_,
                             const StackTrace *stack_)
      : error_summary(error_summary_),
        stack(stack_) {
    Printf("%s", d.Error());
  }
  ~ScopedAllocatorErrorReport() {
    Printf("%s", d.Default());
    stack->Print();
    PrintHintAllocatorCannotReturnNull();
    ReportErrorSummary(error_summary, stack);
  }

 private:
  ScopedErrorReportLock lock;
  const char *error_summary;
  const StackTrace* const stack;
  const SanitizerCommonDecorator d;
};

void NORETURN ReportCallocOverflow(uptr count, uptr size,
                                   const StackTrace *stack) {
  {
    ScopedAllocatorErrorReport report("calloc-overflow", stack);
    Report("ERROR: %s: calloc parameters overflow: count * size (%zd * %zd) "
           "cannot be represented in type size_t\n", SanitizerToolName, count,
           size);
  }
  Die();
}

void NORETURN ReportReallocArrayOverflow(uptr count, uptr size,
                                         const StackTrace *stack) {
  {
    ScopedAllocatorErrorReport report("reallocarray-overflow", stack);
    Report(
        "ERROR: %s: reallocarray parameters overflow: count * size (%zd * %zd) "
        "cannot be represented in type size_t\n",
        SanitizerToolName, count, size);
  }
  Die();
}

void NORETURN ReportPvallocOverflow(uptr size, const StackTrace *stack) {
  {
    ScopedAllocatorErrorReport report("pvalloc-overflow", stack);
    Report("ERROR: %s: pvalloc parameters overflow: size 0x%zx rounded up to "
           "system page size 0x%zx cannot be represented in type size_t\n",
           SanitizerToolName, size, GetPageSizeCached());
  }
  Die();
}

void NORETURN ReportInvalidAllocationAlignment(uptr alignment,
                                               const StackTrace *stack) {
  {
    ScopedAllocatorErrorReport report("invalid-allocation-alignment", stack);
    Report("ERROR: %s: invalid allocation alignment: %zd, alignment must be a "
           "power of two\n", SanitizerToolName, alignment);
  }
  Die();
}

void NORETURN ReportInvalidAlignedAllocAlignment(uptr size, uptr alignment,
                                                 const StackTrace *stack) {
  {
    ScopedAllocatorErrorReport report("invalid-aligned-alloc-alignment", stack);
#if SANITIZER_POSIX
    Report("ERROR: %s: invalid alignment requested in "
           "aligned_alloc: %zd, alignment must be a power of two and the "
           "requested size 0x%zx must be a multiple of alignment\n",
           SanitizerToolName, alignment, size);
#else
    Report("ERROR: %s: invalid alignment requested in aligned_alloc: %zd, "
           "the requested size 0x%zx must be a multiple of alignment\n",
           SanitizerToolName, alignment, size);
#endif
  }
  Die();
}

void NORETURN ReportInvalidPosixMemalignAlignment(uptr alignment,
                                                  const StackTrace *stack) {
  {
    ScopedAllocatorErrorReport report("invalid-posix-memalign-alignment",
                                      stack);
    Report(
        "ERROR: %s: invalid alignment requested in "
        "posix_memalign: %zd, alignment must be a power of two and a "
        "multiple of sizeof(void*) == %zd\n",
        SanitizerToolName, alignment, sizeof(void *));
  }
  Die();
}

void NORETURN ReportAllocationSizeTooBig(uptr user_size, uptr max_size,
                                         const StackTrace *stack) {
  {
    ScopedAllocatorErrorReport report("allocation-size-too-big", stack);
    Report("ERROR: %s: requested allocation size 0x%zx exceeds maximum "
           "supported size of 0x%zx\n", SanitizerToolName, user_size, max_size);
  }
  Die();
}

void NORETURN ReportOutOfMemory(uptr requested_size, const StackTrace *stack) {
  {
    ScopedAllocatorErrorReport report("out-of-memory", stack);
    ERROR_OOM("allocator is trying to allocate 0x%zx bytes\n", requested_size);
  }
  Die();
}

void NORETURN ReportRssLimitExceeded(const StackTrace *stack) {
  {
    ScopedAllocatorErrorReport report("rss-limit-exceeded", stack);
    Report("ERROR: %s: allocator exceeded the RSS limit\n", SanitizerToolName);
  }
  Die();
}

}  // namespace __sanitizer
