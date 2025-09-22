//===-- options.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef GWP_ASAN_OPTIONS_H_
#define GWP_ASAN_OPTIONS_H_

#include <stddef.h>
#include <stdint.h>

namespace gwp_asan {
namespace options {
// ================================ Requirements ===============================
// This function is required to be either implemented by the supporting
// allocator, or one of the two provided implementations may be used
// (RTGwpAsanBacktraceLibc or RTGwpAsanBacktraceSanitizerCommon).
// ================================ Description ================================
// This function shall collect the backtrace for the calling thread and place
// the result in `TraceBuffer`. This function should elide itself and all frames
// below itself from `TraceBuffer`, i.e. the caller's frame should be in
// TraceBuffer[0], and subsequent frames 1..n into TraceBuffer[1..n], where a
// maximum of `Size` frames are stored. Returns the number of frames stored into
// `TraceBuffer`, and zero on failure. If the return value of this function is
// equal to `Size`, it may indicate that the backtrace is truncated.
// =================================== Notes ===================================
// This function may directly or indirectly call malloc(), as the
// GuardedPoolAllocator contains a reentrancy barrier to prevent infinite
// recursion. Any allocation made inside this function will be served by the
// supporting allocator, and will not have GWP-ASan protections.
typedef size_t (*Backtrace_t)(uintptr_t *TraceBuffer, size_t Size);

struct Options {
  Backtrace_t Backtrace = nullptr;

  // Read the options from the included definitions file.
#define GWP_ASAN_OPTION(Type, Name, DefaultValue, Description)                 \
  Type Name = DefaultValue;
#include "gwp_asan/options.inc"
#undef GWP_ASAN_OPTION

  void setDefaults() {
#define GWP_ASAN_OPTION(Type, Name, DefaultValue, Description)                 \
  Name = DefaultValue;
#include "gwp_asan/options.inc"
#undef GWP_ASAN_OPTION

    Backtrace = nullptr;
  }
};
} // namespace options
} // namespace gwp_asan

#endif // GWP_ASAN_OPTIONS_H_
