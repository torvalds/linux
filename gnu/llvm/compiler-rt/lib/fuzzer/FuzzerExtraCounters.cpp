//===- FuzzerExtraCounters.cpp - Extra coverage counters ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Extra coverage counters defined by user code.
//===----------------------------------------------------------------------===//

#include "FuzzerPlatform.h"
#include <cstdint>

#if LIBFUZZER_LINUX || LIBFUZZER_NETBSD || LIBFUZZER_FREEBSD ||                \
    LIBFUZZER_FUCHSIA || LIBFUZZER_EMSCRIPTEN
__attribute__((weak)) extern uint8_t __start___libfuzzer_extra_counters;
__attribute__((weak)) extern uint8_t __stop___libfuzzer_extra_counters;

namespace fuzzer {
uint8_t *ExtraCountersBegin() { return &__start___libfuzzer_extra_counters; }
uint8_t *ExtraCountersEnd() { return &__stop___libfuzzer_extra_counters; }
ATTRIBUTE_NO_SANITIZE_ALL
void ClearExtraCounters() {  // hand-written memset, don't asan-ify.
  uintptr_t *Beg = reinterpret_cast<uintptr_t*>(ExtraCountersBegin());
  uintptr_t *End = reinterpret_cast<uintptr_t*>(ExtraCountersEnd());
  for (; Beg < End; Beg++) {
    *Beg = 0;
    __asm__ __volatile__("" : : : "memory");
  }
}

}  // namespace fuzzer

#endif
