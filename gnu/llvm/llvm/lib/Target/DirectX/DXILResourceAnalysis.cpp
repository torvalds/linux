//===- DXILResourceAnalysis.cpp - DXIL Resource analysis-------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file This file contains Analysis for information about DXIL resources.
///
//===----------------------------------------------------------------------===//

#include "DXILResourceAnalysis.h"
#include "DirectX.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;

#define DEBUG_TYPE "dxil-resource-analysis"

dxil::Resources DXILResourceAnalysis::run(Module &M,
                                          ModuleAnalysisManager &AM) {
  dxil::Resources R;
  R.collect(M);
  return R;
}

AnalysisKey DXILResourceAnalysis::Key;

PreservedAnalyses DXILResourcePrinterPass::run(Module &M,
                                               ModuleAnalysisManager &AM) {
  dxil::Resources Res = AM.getResult<DXILResourceAnalysis>(M);
  Res.print(OS);
  return PreservedAnalyses::all();
}

char DXILResourceWrapper::ID = 0;
INITIALIZE_PASS_BEGIN(DXILResourceWrapper, DEBUG_TYPE,
                      "DXIL resource Information", true, true)
INITIALIZE_PASS_END(DXILResourceWrapper, DEBUG_TYPE,
                    "DXIL resource Information", true, true)

bool DXILResourceWrapper::runOnModule(Module &M) {
  Resources.collect(M);
  return false;
}

DXILResourceWrapper::DXILResourceWrapper() : ModulePass(ID) {}

void DXILResourceWrapper::print(raw_ostream &OS, const Module *) const {
  Resources.print(OS);
}
