//===-- tsan_ignoreset.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//
#include "tsan_ignoreset.h"

namespace __tsan {

const uptr IgnoreSet::kMaxSize;

IgnoreSet::IgnoreSet()
    : size_() {
}

void IgnoreSet::Add(StackID stack_id) {
  if (size_ == kMaxSize)
    return;
  for (uptr i = 0; i < size_; i++) {
    if (stacks_[i] == stack_id)
      return;
  }
  stacks_[size_++] = stack_id;
}

StackID IgnoreSet::At(uptr i) const {
  CHECK_LT(i, size_);
  CHECK_LE(size_, kMaxSize);
  return stacks_[i];
}

}  // namespace __tsan
