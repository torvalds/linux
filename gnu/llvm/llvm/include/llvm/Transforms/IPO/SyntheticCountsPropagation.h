//=- SyntheticCountsPropagation.h - Propagate function counts -----*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_SYNTHETICCOUNTSPROPAGATION_H
#define LLVM_TRANSFORMS_IPO_SYNTHETICCOUNTSPROPAGATION_H

#include "llvm/IR/PassManager.h"

namespace llvm {
class Module;

class SyntheticCountsPropagation
    : public PassInfoMixin<SyntheticCountsPropagation> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);
};
} // namespace llvm
#endif
