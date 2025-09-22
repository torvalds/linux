//===- IVUsersPrinter.cpp - Induction Variable Users Printer ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Scalar/IVUsersPrinter.h"
#include "llvm/Analysis/IVUsers.h"
using namespace llvm;

#define DEBUG_TYPE "iv-users"

PreservedAnalyses IVUsersPrinterPass::run(Loop &L, LoopAnalysisManager &AM,
                                          LoopStandardAnalysisResults &AR,
                                          LPMUpdater &U) {
  AM.getResult<IVUsersAnalysis>(L, AR).print(OS);
  return PreservedAnalyses::all();
}
