//===- ConstantMerge.h - Merge duplicate global constants -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interface to a pass that merges duplicate global
// constants together into a single constant that is shared.  This is useful
// because some passes (ie TraceValues) insert a lot of string constants into
// the program, regardless of whether or not an existing string is available.
//
// Algorithm: ConstantMerge is designed to build up a map of available constants
// and eliminate duplicates when it is initialized.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_CONSTANTMERGE_H
#define LLVM_TRANSFORMS_IPO_CONSTANTMERGE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

/// A pass that merges duplicate global constants into a single constant.
class ConstantMergePass : public PassInfoMixin<ConstantMergePass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_CONSTANTMERGE_H
