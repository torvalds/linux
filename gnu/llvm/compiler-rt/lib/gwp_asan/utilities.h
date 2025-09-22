//===-- utilities.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef GWP_ASAN_UTILITIES_H_
#define GWP_ASAN_UTILITIES_H_

#include "gwp_asan/definitions.h"

#include <stddef.h>

namespace gwp_asan {
// Terminates in a platform-specific way with `Message`.
void die(const char *Message);

// Checks that `Condition` is true, otherwise dies with `Message`.
GWP_ASAN_ALWAYS_INLINE void check(bool Condition, const char *Message) {
  if (Condition)
    return;
  die(Message);
}
} // namespace gwp_asan

#endif // GWP_ASAN_UTILITIES_H_
