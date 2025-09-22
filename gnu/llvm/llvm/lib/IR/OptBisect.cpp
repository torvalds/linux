//===- llvm/IR/OptBisect/Bisect.cpp - LLVM Bisect support -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file implements support for a bisecting optimizations based on a
/// command line option.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/OptBisect.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace llvm;

static OptBisect &getOptBisector() {
  static OptBisect OptBisector;
  return OptBisector;
}

static cl::opt<int> OptBisectLimit("opt-bisect-limit", cl::Hidden,
                                   cl::init(OptBisect::Disabled), cl::Optional,
                                   cl::cb<void, int>([](int Limit) {
                                     getOptBisector().setLimit(Limit);
                                   }),
                                   cl::desc("Maximum optimization to perform"));

static void printPassMessage(const StringRef &Name, int PassNum,
                             StringRef TargetDesc, bool Running) {
  StringRef Status = Running ? "" : "NOT ";
  errs() << "BISECT: " << Status << "running pass "
         << "(" << PassNum << ") " << Name << " on " << TargetDesc << "\n";
}

bool OptBisect::shouldRunPass(const StringRef PassName,
                              StringRef IRDescription) {
  assert(isEnabled());

  int CurBisectNum = ++LastBisectNum;
  bool ShouldRun = (BisectLimit == -1 || CurBisectNum <= BisectLimit);
  printPassMessage(PassName, CurBisectNum, IRDescription, ShouldRun);
  return ShouldRun;
}

const int OptBisect::Disabled;

OptPassGate &llvm::getGlobalPassGate() { return getOptBisector(); }
