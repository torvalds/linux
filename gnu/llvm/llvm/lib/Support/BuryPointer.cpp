//===- BuryPointer.cpp - Memory Manipulation/Leak ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/BuryPointer.h"
#include "llvm/Support/Compiler.h"
#include <atomic>

namespace llvm {

void BuryPointer(const void *Ptr) {
  // This function may be called only a small fixed amount of times per each
  // invocation, otherwise we do actually have a leak which we want to report.
  // If this function is called more than kGraveYardMaxSize times, the pointers
  // will not be properly buried and a leak detector will report a leak, which
  // is what we want in such case.
  static const size_t kGraveYardMaxSize = 16;
  LLVM_ATTRIBUTE_USED static const void *GraveYard[kGraveYardMaxSize];
  static std::atomic<unsigned> GraveYardSize;
  unsigned Idx = GraveYardSize++;
  if (Idx >= kGraveYardMaxSize)
    return;
  GraveYard[Idx] = Ptr;
}

} // namespace llvm
