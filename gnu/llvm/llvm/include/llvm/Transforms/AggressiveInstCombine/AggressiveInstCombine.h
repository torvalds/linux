//===- AggressiveInstCombine.h - AggressiveInstCombine pass -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// AggressiveInstCombiner - Combine expression patterns to form expressions
/// with fewer, simple instructions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_AGGRESSIVEINSTCOMBINE_AGGRESSIVEINSTCOMBINE_H
#define LLVM_TRANSFORMS_AGGRESSIVEINSTCOMBINE_AGGRESSIVEINSTCOMBINE_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class AggressiveInstCombinePass
    : public PassInfoMixin<AggressiveInstCombinePass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
}

#endif
