//===-- RegisterValue.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///
/// Defines a Target independent value for a Register. This is useful to explore
/// the influence of the instruction input values on its execution time.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_EXEGESIS_REGISTERVALUE_H
#define LLVM_TOOLS_LLVM_EXEGESIS_REGISTERVALUE_H

#include <llvm/ADT/APFloat.h>
#include <llvm/ADT/APInt.h>

namespace llvm {
namespace exegesis {

// A simple object storing the value for a particular register.
struct RegisterValue {
  static RegisterValue zero(unsigned Reg) { return {Reg, APInt()}; }
  unsigned Register;
  APInt Value;
};

enum class PredefinedValues {
  POS_ZERO,       // Positive zero
  NEG_ZERO,       // Negative zero
  ONE,            // 1.0
  TWO,            // 2.0
  INF,            // Infinity
  QNAN,           // Quiet NaN
  ULP,            // One Unit in the last place
  SMALLEST = ULP, // The minimum subnormal number
  SMALLEST_NORM,  // The minimum normal number
  LARGEST,        // The maximum normal number
  ONE_PLUS_ULP,   // The value just after 1.0
};

APInt bitcastFloatValue(const fltSemantics &FltSemantics,
                        PredefinedValues Value);

} // namespace exegesis
} // namespace llvm

#endif // LLVM_TOOLS_LLVM_EXEGESIS_REGISTERVALUE_H
