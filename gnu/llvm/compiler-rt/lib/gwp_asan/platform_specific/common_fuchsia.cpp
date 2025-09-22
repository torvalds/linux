//===-- common_fuchsia.cpp --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/common.h"

namespace gwp_asan {
// This is only used for AllocationTrace.ThreadID and allocation traces are not
// yet supported on Fuchsia.
uint64_t getThreadID() { return kInvalidThreadID; }
} // namespace gwp_asan
