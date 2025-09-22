//===--- BitmaskEnum.h - wrapper of LLVM's bitmask enum facility-*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Provides LLVM's BitmaskEnum facility to enumeration types declared in
/// namespace clang.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_BITMASKENUM_H
#define LLVM_CLANG_BASIC_BITMASKENUM_H

#include "llvm/ADT/BitmaskEnum.h"

namespace clang {
  LLVM_ENABLE_BITMASK_ENUMS_IN_NAMESPACE();
}

#endif
