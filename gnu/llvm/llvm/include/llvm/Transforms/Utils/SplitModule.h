//===- SplitModule.h - Split a module into partitions -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the function llvm::SplitModule, which splits a module
// into multiple linkable partitions. It can be used to implement parallel code
// generation for link-time optimization.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SPLITMODULE_H
#define LLVM_TRANSFORMS_UTILS_SPLITMODULE_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include <memory>

namespace llvm {

class Module;

/// Splits the module M into N linkable partitions. The function ModuleCallback
/// is called N times passing each individual partition as the MPart argument.
/// PreserveLocals: Split without externalizing locals.
/// RoundRobin: Use round-robin distribution of functions to modules instead
/// of the default name-hash-based one.
///
/// FIXME: This function does not deal with the somewhat subtle symbol
/// visibility issues around module splitting, including (but not limited to):
///
/// - Internal symbols should not collide with symbols defined outside the
///   module.
/// - Internal symbols defined in module-level inline asm should be visible to
///   each partition.
void SplitModule(
    Module &M, unsigned N,
    function_ref<void(std::unique_ptr<Module> MPart)> ModuleCallback,
    bool PreserveLocals = false, bool RoundRobin = false);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_SPLITMODULE_H
