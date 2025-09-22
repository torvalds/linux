//===-- stats.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Data definitions for sanitizer statistics gathering.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_STATS_STATS_H
#define SANITIZER_STATS_STATS_H

#include "sanitizer_common/sanitizer_internal_defs.h"

namespace __sanitizer {

// Number of bits in data that are used for the sanitizer kind. Needs to match
// llvm::kSanitizerStatKindBits in
// llvm/include/llvm/Transforms/Utils/SanitizerStats.h
enum { kKindBits = 3 };

struct StatInfo {
  uptr addr;
  uptr data;
};

struct StatModule {
  StatModule *next;
  u32 size;
  StatInfo infos[1];
};

inline uptr CountFromData(uptr data) {
  return data & ((1ull << (sizeof(uptr) * 8 - kKindBits)) - 1);
}

}

#endif
