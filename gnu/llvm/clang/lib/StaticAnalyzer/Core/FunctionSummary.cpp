//===- FunctionSummary.cpp - Stores summaries of functions. ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a summary of a function gathered/used by static analysis.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/FunctionSummary.h"

using namespace clang;
using namespace ento;

unsigned FunctionSummariesTy::getTotalNumBasicBlocks() {
  unsigned Total = 0;
  for (const auto &I : Map)
    Total += I.second.TotalBasicBlocks;
  return Total;
}

unsigned FunctionSummariesTy::getTotalNumVisitedBasicBlocks() {
  unsigned Total = 0;
  for (const auto &I : Map)
    Total += I.second.VisitedBasicBlocks.count();
  return Total;
}
