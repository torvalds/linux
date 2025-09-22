//===-- printf.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef GWP_ASAN_OPTIONAL_PRINTF_H_
#define GWP_ASAN_OPTIONAL_PRINTF_H_

namespace gwp_asan {

// ================================ Requirements ===============================
// This function is required to be provided by the supporting allocator iff the
// allocator wants to use any of the optional components.
// ================================ Description ================================
// This function shall produce output according to a strict subset of the C
// standard library's printf() family. This function must support printing the
// following formats:
//   1. integers: "%([0-9]*)?(z|ll)?{d,u,x,X}"
//   2. pointers: "%p"
//   3. strings:  "%[-]([0-9]*)?(\\.\\*)?s"
//   4. chars:    "%c"
// This function must be implemented in a signal-safe manner, and thus must not
// malloc().
// =================================== Notes ===================================
// This function has a slightly different signature than the C standard
// library's printf(). Notably, it returns 'void' rather than 'int'.
typedef void (*Printf_t)(const char *Format, ...);

} // namespace gwp_asan
#endif // GWP_ASAN_OPTIONAL_PRINTF_H_
