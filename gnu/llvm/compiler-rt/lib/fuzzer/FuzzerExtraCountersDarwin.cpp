//===- FuzzerExtraCountersDarwin.cpp - Extra coverage counters for Darwin -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Extra coverage counters defined by user code for Darwin.
//===----------------------------------------------------------------------===//

#include "FuzzerPlatform.h"
#include <cstdint>

#if LIBFUZZER_APPLE

namespace fuzzer {
uint8_t *ExtraCountersBegin() { return nullptr; }
uint8_t *ExtraCountersEnd() { return nullptr; }
void ClearExtraCounters() {}
} // namespace fuzzer

#endif
