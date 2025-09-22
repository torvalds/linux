//===- llvm/Support/BuryPointer.h - Memory Manipulation/Leak ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BURYPOINTER_H
#define LLVM_SUPPORT_BURYPOINTER_H

#include <memory>

namespace llvm {

// In tools that will exit soon anyway, going through the process of explicitly
// deallocating resources can be unnecessary - better to leak the resources and
// let the OS clean them up when the process ends. Use this function to ensure
// the memory is not misdiagnosed as an unintentional leak by leak detection
// tools (this is achieved by preserving pointers to the object in a globally
// visible array).
void BuryPointer(const void *Ptr);
template <typename T> void BuryPointer(std::unique_ptr<T> Ptr) {
  BuryPointer(Ptr.release());
}

} // namespace llvm

#endif
