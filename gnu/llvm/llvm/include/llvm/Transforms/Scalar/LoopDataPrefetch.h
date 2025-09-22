//===-------- LoopDataPrefetch.h - Loop Data Prefetching Pass ---*- C++ -*-===//
//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the interface for LLVM's Loop Data Prefetching Pass.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPDATAPREFETCH_H
#define LLVM_TRANSFORMS_SCALAR_LOOPDATAPREFETCH_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// An optimization pass inserting data prefetches in loops.
class LoopDataPrefetchPass : public PassInfoMixin<LoopDataPrefetchPass> {
public:
  LoopDataPrefetchPass() = default;

  /// Run the pass over the function.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOOPDATAPREFETCH_H
