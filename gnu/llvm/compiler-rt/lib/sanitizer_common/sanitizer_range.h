//===-- sanitizer_range.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contais Range and related utilities.
//
//===----------------------------------------------------------------------===//

#ifndef SANITIZER_RANGE_H
#define SANITIZER_RANGE_H

#include "sanitizer_common.h"
#include "sanitizer_common/sanitizer_array_ref.h"

namespace __sanitizer {

struct Range {
  uptr begin;
  uptr end;
};

inline bool operator==(const Range &lhs, const Range &rhs) {
  return lhs.begin == rhs.begin && lhs.end == rhs.end;
}

inline bool operator!=(const Range &lhs, const Range &rhs) {
  return !(lhs == rhs);
}

// Calculates intersection of two sets of regions in O(N log N) time.
void Intersect(ArrayRef<Range> a, ArrayRef<Range> b,
               InternalMmapVectorNoCtor<Range> &output);

}  // namespace __sanitizer

#endif  // SANITIZER_RANGE_H
