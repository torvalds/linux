//===- ProvenanceAnalysisEvaluator.cpp - ObjC ARC Optimization ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ProvenanceAnalysis.h"
#include "llvm/Transforms/ObjCARC.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::objcarc;

static StringRef getName(Value *V) {
  StringRef Name = V->getName();
  if (Name.starts_with("\1"))
    return Name.substr(1);
  return Name;
}

static void insertIfNamed(SetVector<Value *> &Values, Value *V) {
  if (!V->hasName())
    return;
  Values.insert(V);
}

PreservedAnalyses PAEvalPass::run(Function &F, FunctionAnalysisManager &AM) {
  SetVector<Value *> Values;

  for (auto &Arg : F.args())
    insertIfNamed(Values, &Arg);

  for (Instruction &I : instructions(F)) {
    insertIfNamed(Values, &I);

    for (auto &Op : I.operands())
      insertIfNamed(Values, Op);
  }

  ProvenanceAnalysis PA;
  PA.setAA(&AM.getResult<AAManager>(F));

  for (Value *V1 : Values) {
    StringRef NameV1 = getName(V1);
    for (Value *V2 : Values) {
      StringRef NameV2 = getName(V2);
      if (NameV1 >= NameV2)
        continue;
      errs() << NameV1 << " and " << NameV2;
      if (PA.related(V1, V2))
        errs() << " are related.\n";
      else
        errs() << " are not related.\n";
    }
  }

  return PreservedAnalyses::all();
}
