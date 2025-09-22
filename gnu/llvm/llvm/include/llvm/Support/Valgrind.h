//===- llvm/Support/Valgrind.h - Communication with Valgrind ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Methods for communicating with a valgrind instance this program is running
// under.  These are all no-ops unless LLVM was configured on a system with the
// valgrind headers installed and valgrind is controlling this process.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_VALGRIND_H
#define LLVM_SUPPORT_VALGRIND_H

#include <cstddef>

namespace llvm {
namespace sys {
  // True if Valgrind is controlling this process.
  bool RunningOnValgrind();

  // Discard valgrind's translation of code in the range [Addr .. Addr + Len).
  // Otherwise valgrind may continue to execute the old version of the code.
  void ValgrindDiscardTranslations(const void *Addr, size_t Len);
} // namespace sys
} // end namespace llvm

#endif // LLVM_SUPPORT_VALGRIND_H
