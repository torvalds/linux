//===- CountVisits.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/CountVisits.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;

#define DEBUG_TYPE "count-visits"

STATISTIC(MaxVisited, "Max number of times we visited a function");

PreservedAnalyses CountVisitsPass::run(Function &F, FunctionAnalysisManager &) {
  uint32_t Count = Counts[F.getName()] + 1;
  Counts[F.getName()] = Count;
  if (Count > MaxVisited)
    MaxVisited = Count;
  return PreservedAnalyses::all();
}
