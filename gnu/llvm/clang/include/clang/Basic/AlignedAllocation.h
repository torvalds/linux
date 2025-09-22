//===--- AlignedAllocation.h - Aligned Allocation ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Defines a function that returns the minimum OS versions supporting
/// C++17's aligned allocation functions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_ALIGNEDALLOCATION_H
#define LLVM_CLANG_BASIC_ALIGNEDALLOCATION_H

#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/TargetParser/Triple.h"

namespace clang {

inline llvm::VersionTuple alignedAllocMinVersion(llvm::Triple::OSType OS) {
  switch (OS) {
  default:
    break;
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX: // Earliest supporting version is 10.13.
    return llvm::VersionTuple(10U, 13U);
  case llvm::Triple::IOS:
  case llvm::Triple::TvOS: // Earliest supporting version is 11.0.0.
    return llvm::VersionTuple(11U);
  case llvm::Triple::WatchOS: // Earliest supporting version is 4.0.0.
    return llvm::VersionTuple(4U);
  case llvm::Triple::ZOS:
    return llvm::VersionTuple(); // All z/OS versions have no support.
  }

  llvm_unreachable("Unexpected OS");
}

} // end namespace clang

#endif // LLVM_CLANG_BASIC_ALIGNEDALLOCATION_H
