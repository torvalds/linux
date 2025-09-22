//===-- xray_x86_64.inc -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of XRay, a dynamic runtime instrumentation system.
//
//===----------------------------------------------------------------------===//

#include <cstdint>

#include "sanitizer_common/sanitizer_internal_defs.h"
#include "xray_defs.h"

namespace __xray {

ALWAYS_INLINE uint64_t readTSC(uint8_t &CPU) XRAY_NEVER_INSTRUMENT {
  unsigned LongCPU;
  unsigned long Rax, Rdx;
  __asm__ __volatile__("rdtscp\n" : "=a"(Rax), "=d"(Rdx), "=c"(LongCPU) ::);
  CPU = LongCPU;
  return (Rdx << 32) + Rax;
}

uint64_t getTSCFrequency();

bool probeRequiredCPUFeatures();

} // namespace __xray
