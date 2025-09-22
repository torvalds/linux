//===- FuzzerExtraCountersWindows.cpp - Extra coverage counters for Win32 -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Extra coverage counters defined by user code for Windows.
//===----------------------------------------------------------------------===//

#include "FuzzerPlatform.h"
#include <cstdint>

#if LIBFUZZER_WINDOWS
#include <windows.h>

namespace fuzzer {

//
// The __start___libfuzzer_extra_counters variable is align 16, size 16 to
// ensure the padding between it and the next variable in this section (either
// __libfuzzer_extra_counters or __stop___libfuzzer_extra_counters) will be
// located at (__start___libfuzzer_extra_counters +
// sizeof(__start___libfuzzer_extra_counters)). Otherwise, the calculation of
// (stop - (start + sizeof(start))) might be skewed.
//
// The section name, __libfuzzer_extra_countaaa ends with "aaa", so it sorts
// before __libfuzzer_extra_counters alphabetically. We want the start symbol to
// be placed in the section just before the user supplied counters (if present).
//
#pragma section(".data$__libfuzzer_extra_countaaa")
ATTRIBUTE_ALIGNED(16)
__declspec(allocate(".data$__libfuzzer_extra_countaaa")) uint8_t
    __start___libfuzzer_extra_counters[16] = {0};

//
// Example of what the user-supplied counters should look like. First, the
// pragma to create the section name. It will fall alphabetically between
// ".data$__libfuzzer_extra_countaaa" and ".data$__libfuzzer_extra_countzzz".
// Next, the declspec to allocate the variable inside the specified section.
// Finally, some array, struct, whatever that is used to track the counter data.
// The size of this variable is computed at runtime by finding the difference of
// __stop___libfuzzer_extra_counters and __start___libfuzzer_extra_counters +
// sizeof(__start___libfuzzer_extra_counters).
//

//
//     #pragma section(".data$__libfuzzer_extra_counters")
//     __declspec(allocate(".data$__libfuzzer_extra_counters"))
//         uint8_t any_name_variable[64 * 1024];
//

//
// Here, the section name, __libfuzzer_extra_countzzz ends with "zzz", so it
// sorts after __libfuzzer_extra_counters alphabetically. We want the stop
// symbol to be placed in the section just after the user supplied counters (if
// present). Align to 1 so there isn't any padding placed between this and the
// previous variable.
//
#pragma section(".data$__libfuzzer_extra_countzzz")
ATTRIBUTE_ALIGNED(1)
__declspec(allocate(".data$__libfuzzer_extra_countzzz")) uint8_t
    __stop___libfuzzer_extra_counters = 0;

uint8_t *ExtraCountersBegin() {
  return __start___libfuzzer_extra_counters +
         sizeof(__start___libfuzzer_extra_counters);
}

uint8_t *ExtraCountersEnd() { return &__stop___libfuzzer_extra_counters; }

ATTRIBUTE_NO_SANITIZE_ALL
void ClearExtraCounters() {
  uint8_t *Beg = ExtraCountersBegin();
  SecureZeroMemory(Beg, ExtraCountersEnd() - Beg);
}

} // namespace fuzzer

#endif
