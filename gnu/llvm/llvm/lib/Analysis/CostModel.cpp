//===- CostModel.cpp ------ Cost Model Analysis ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the cost model analysis. It provides a very basic cost
// estimation for LLVM-IR. This analysis uses the services of the codegen
// to approximate the cost of any IR instruction when lowered to machine
// instructions. The cost results are unit-less and the cost number represents
// the throughput of the machine assuming that all loads hit the cache, all
// branches are predicted, etc. The cost numbers can be added in order to
// compare two or more transformation alternatives.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/CostModel.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/IntrinsicInst.h"
using namespace llvm;

static cl::opt<TargetTransformInfo::TargetCostKind> CostKind(
    "cost-kind", cl::desc("Target cost kind"),
    cl::init(TargetTransformInfo::TCK_RecipThroughput),
    cl::values(clEnumValN(TargetTransformInfo::TCK_RecipThroughput,
                          "throughput", "Reciprocal throughput"),
               clEnumValN(TargetTransformInfo::TCK_Latency,
                          "latency", "Instruction latency"),
               clEnumValN(TargetTransformInfo::TCK_CodeSize,
                          "code-size", "Code size"),
               clEnumValN(TargetTransformInfo::TCK_SizeAndLatency,
                          "size-latency", "Code size and latency")));

static cl::opt<bool> TypeBasedIntrinsicCost("type-based-intrinsic-cost",
    cl::desc("Calculate intrinsics cost based only on argument types"),
    cl::init(false));

#define CM_NAME "cost-model"
#define DEBUG_TYPE CM_NAME

PreservedAnalyses CostModelPrinterPass::run(Function &F,
                                            FunctionAnalysisManager &AM) {
  auto &TTI = AM.getResult<TargetIRAnalysis>(F);
  OS << "Printing analysis 'Cost Model Analysis' for function '" << F.getName() << "':\n";
  for (BasicBlock &B : F) {
    for (Instruction &Inst : B) {
      // TODO: Use a pass parameter instead of cl::opt CostKind to determine
      // which cost kind to print.
      InstructionCost Cost;
      auto *II = dyn_cast<IntrinsicInst>(&Inst);
      if (II && TypeBasedIntrinsicCost) {
        IntrinsicCostAttributes ICA(II->getIntrinsicID(), *II,
                                    InstructionCost::getInvalid(), true);
        Cost = TTI.getIntrinsicInstrCost(ICA, CostKind);
      }
      else {
        Cost = TTI.getInstructionCost(&Inst, CostKind);
      }

      if (auto CostVal = Cost.getValue())
        OS << "Cost Model: Found an estimated cost of " << *CostVal;
      else
        OS << "Cost Model: Invalid cost";

      OS << " for instruction: " << Inst << "\n";
    }
  }
  return PreservedAnalyses::all();
}
