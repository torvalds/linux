//===-- common_posix.cpp ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/common.h"

#include <stdint.h>
#include <sys/syscall.h> // IWYU pragma: keep
// IWYU pragma: no_include <syscall.h>
#include <unistd.h>

namespace gwp_asan {

uint64_t getThreadID() {
#ifdef SYS_gettid
  return syscall(SYS_gettid);
#else
  return kInvalidThreadID;
#endif
}

} // namespace gwp_asan
