//===-- MathExtras.cpp - Implement the MathExtras header --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the MathExtras.h header
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/MathExtras.h"

#ifdef _MSC_VER
#include <limits>
#else
#include <cmath>
#endif

namespace llvm {

#if defined(_MSC_VER)
  // Visual Studio defines the HUGE_VAL class of macros using purposeful
  // constant arithmetic overflow, which it then warns on when encountered.
  const float huge_valf = std::numeric_limits<float>::infinity();
#else
  const float huge_valf = HUGE_VALF;
#endif

} // namespace llvm
