//=== ValueProfilePlugins.inc - set of plugins used by ValueProfileCollector =//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a set of plugin classes used in ValueProfileCollectorImpl.
// Each plugin is responsible for collecting Value Profiling candidates for a
// particular optimization.
// Each plugin must satisfy the interface described in ValueProfileCollector.cpp
//
//===----------------------------------------------------------------------===//

#include "ValueProfileCollector.h"
#include "llvm/Analysis/IndirectCallVisitor.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/InstVisitor.h"

using namespace llvm;
using CandidateInfo = ValueProfileCollector::CandidateInfo;

extern cl::opt<bool> MemOPOptMemcmpBcmp;

///--------------------------- MemIntrinsicPlugin ------------------------------
class MemIntrinsicPlugin : public InstVisitor<MemIntrinsicPlugin> {
  Function &F;
  TargetLibraryInfo &TLI;
  std::vector<CandidateInfo> *Candidates;

public:
  static constexpr InstrProfValueKind Kind = IPVK_MemOPSize;

  MemIntrinsicPlugin(Function &Fn, TargetLibraryInfo &TLI)
      : F(Fn), TLI(TLI), Candidates(nullptr) {}

  void run(std::vector<CandidateInfo> &Cs) {
    Candidates = &Cs;
    visit(F);
    Candidates = nullptr;
  }
  void visitMemIntrinsic(MemIntrinsic &MI) {
    Value *Length = MI.getLength();
    // Not instrument constant length calls.
    if (isa<ConstantInt>(Length))
      return;

    Instruction *InsertPt = &MI;
    Instruction *AnnotatedInst = &MI;
    Candidates->emplace_back(CandidateInfo{Length, InsertPt, AnnotatedInst});
  }
  void visitCallInst(CallInst &CI) {
    if (!MemOPOptMemcmpBcmp)
      return;
    auto *F = CI.getCalledFunction();
    if (!F)
      return;
    LibFunc Func;
    if (TLI.getLibFunc(CI, Func) &&
        (Func == LibFunc_memcmp || Func == LibFunc_bcmp)) {
      Value *Length = CI.getArgOperand(2);
      // Not instrument constant length calls.
      if (isa<ConstantInt>(Length))
        return;
      Instruction *InsertPt = &CI;
      Instruction *AnnotatedInst = &CI;
      Candidates->emplace_back(CandidateInfo{Length, InsertPt, AnnotatedInst});
    }
  }
};

///------------------------ IndirectCallPromotionPlugin ------------------------
class IndirectCallPromotionPlugin {
  Function &F;

public:
  static constexpr InstrProfValueKind Kind = IPVK_IndirectCallTarget;

  IndirectCallPromotionPlugin(Function &Fn, TargetLibraryInfo &TLI) : F(Fn) {}

  void run(std::vector<CandidateInfo> &Candidates) {
    std::vector<CallBase *> Result = findIndirectCalls(F);
    for (Instruction *I : Result) {
      Value *Callee = cast<CallBase>(I)->getCalledOperand();
      Instruction *InsertPt = I;
      Instruction *AnnotatedInst = I;
      Candidates.emplace_back(CandidateInfo{Callee, InsertPt, AnnotatedInst});
    }
  }
};

///--------------------- VirtualTableValueProfilingPlugin --------------------
class VTableProfilingPlugin {
  Function &F;

public:
  static constexpr InstrProfValueKind Kind = IPVK_VTableTarget;

  VTableProfilingPlugin(Function &Fn, TargetLibraryInfo &TLI) : F(Fn) {}

  void run(std::vector<CandidateInfo> &Candidates) {
    std::vector<Instruction *> Result = findVTableAddrs(F);
    for (Instruction *I : Result) {
      Instruction *InsertPt = I->getNextNonDebugInstruction();
      // When finding an insertion point, keep PHI and EH pad instructions
      // before vp intrinsics. This is similar to
      // `BasicBlock::getFirstInsertionPt`.
      while (InsertPt && (dyn_cast<PHINode>(InsertPt) || InsertPt->isEHPad()))
        InsertPt = InsertPt->getNextNonDebugInstruction();
      // Skip instrumentating the value if InsertPt is the last instruction.
      // FIXME: Set InsertPt to the end of basic block to instrument the value
      // if InsertPt is the last instruction.
      if (InsertPt == nullptr)
        continue;

      Instruction *AnnotatedInst = I;
      Candidates.emplace_back(CandidateInfo{I, InsertPt, AnnotatedInst});
    }
  }
};

///----------------------- Registration of the plugins -------------------------
/// For now, registering a plugin with the ValueProfileCollector is done by
/// adding the plugin type to the VP_PLUGIN_LIST macro.
#define VP_PLUGIN_LIST                                                         \
  MemIntrinsicPlugin, IndirectCallPromotionPlugin, VTableProfilingPlugin
