//===- CtorUtils.h - Helpers for working with global_ctors ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines functions that are used to process llvm.global_ctors.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_CTORUTILS_H
#define LLVM_TRANSFORMS_UTILS_CTORUTILS_H

#include "llvm/ADT/STLFunctionalExtras.h"

namespace llvm {

class Function;
class Module;

/// Call "ShouldRemove" for every entry in M's global_ctor list and remove the
/// entries for which it returns true.  Return true if anything changed.
bool optimizeGlobalCtorsList(
    Module &M, function_ref<bool(uint32_t, Function *)> ShouldRemove);

} // namespace llvm

#endif
