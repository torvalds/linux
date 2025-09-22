//===- llvm/Support/DivisionByConstantInfo.h ---------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This file implements support for optimizing divisions by a constant
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_DIVISIONBYCONSTANTINFO_H
#define LLVM_SUPPORT_DIVISIONBYCONSTANTINFO_H

#include "llvm/ADT/APInt.h"

namespace llvm {

/// Magic data for optimising signed division by a constant.
struct SignedDivisionByConstantInfo {
  static SignedDivisionByConstantInfo get(const APInt &D);
  APInt Magic;          ///< magic number
  unsigned ShiftAmount; ///< shift amount
};

/// Magic data for optimising unsigned division by a constant.
struct UnsignedDivisionByConstantInfo {
  static UnsignedDivisionByConstantInfo
  get(const APInt &D, unsigned LeadingZeros = 0,
      bool AllowEvenDivisorOptimization = true);
  APInt Magic;          ///< magic number
  bool IsAdd;           ///< add indicator
  unsigned PostShift;   ///< post-shift amount
  unsigned PreShift;    ///< pre-shift amount
};

} // namespace llvm

#endif
