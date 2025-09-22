//===-- printf_sanitizer_common.cpp -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gwp_asan/optional/printf.h"

#include "sanitizer_common/sanitizer_common.h"

namespace gwp_asan {
namespace test {

Printf_t getPrintfFunction() { return __sanitizer::Printf; }

} // namespace test
} // namespace gwp_asan
