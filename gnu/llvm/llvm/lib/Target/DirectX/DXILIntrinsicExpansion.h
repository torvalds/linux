//===- DXILIntrinsicExpansion.h - Prepare LLVM Module for DXIL encoding----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TARGET_DIRECTX_DXILINTRINSICEXPANSION_H
#define LLVM_TARGET_DIRECTX_DXILINTRINSICEXPANSION_H

#include "DXILResource.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"

namespace llvm {

/// A pass that transforms DXIL Intrinsics that don't have DXIL opCodes
class DXILIntrinsicExpansion : public PassInfoMixin<DXILIntrinsicExpansion> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);
};

class DXILIntrinsicExpansionLegacy : public ModulePass {

public:
  bool runOnModule(Module &M) override;
  DXILIntrinsicExpansionLegacy() : ModulePass(ID) {}

  static char ID; // Pass identification.
};
} // namespace llvm

#endif // LLVM_TARGET_DIRECTX_DXILINTRINSICEXPANSION_H
