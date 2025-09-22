//===-- BPF.h - Top-level interface for BPF representation ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_BPF_BPF_H
#define LLVM_LIB_TARGET_BPF_BPF_H

#include "MCTargetDesc/BPFMCTargetDesc.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
class BPFRegisterBankInfo;
class BPFSubtarget;
class BPFTargetMachine;
class InstructionSelector;
class PassRegistry;

ModulePass *createBPFCheckAndAdjustIR();

FunctionPass *createBPFISelDag(BPFTargetMachine &TM);
FunctionPass *createBPFMISimplifyPatchablePass();
FunctionPass *createBPFMIPeepholePass();
FunctionPass *createBPFMIPreEmitPeepholePass();
FunctionPass *createBPFMIPreEmitCheckingPass();

InstructionSelector *createBPFInstructionSelector(const BPFTargetMachine &,
                                                  const BPFSubtarget &,
                                                  const BPFRegisterBankInfo &);

void initializeBPFCheckAndAdjustIRPass(PassRegistry&);
void initializeBPFDAGToDAGISelLegacyPass(PassRegistry &);
void initializeBPFMIPeepholePass(PassRegistry &);
void initializeBPFMIPreEmitCheckingPass(PassRegistry&);
void initializeBPFMIPreEmitPeepholePass(PassRegistry &);
void initializeBPFMISimplifyPatchablePass(PassRegistry &);

class BPFAbstractMemberAccessPass
    : public PassInfoMixin<BPFAbstractMemberAccessPass> {
  BPFTargetMachine *TM;

public:
  BPFAbstractMemberAccessPass(BPFTargetMachine *TM) : TM(TM) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  static bool isRequired() { return true; }
};

class BPFPreserveDITypePass : public PassInfoMixin<BPFPreserveDITypePass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  static bool isRequired() { return true; }
};

class BPFIRPeepholePass : public PassInfoMixin<BPFIRPeepholePass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  static bool isRequired() { return true; }
};

class BPFASpaceCastSimplifyPass
    : public PassInfoMixin<BPFASpaceCastSimplifyPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  static bool isRequired() { return true; }
};

class BPFAdjustOptPass : public PassInfoMixin<BPFAdjustOptPass> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

class BPFPreserveStaticOffsetPass
    : public PassInfoMixin<BPFPreserveStaticOffsetPass> {
  bool AllowPartial;

public:
  BPFPreserveStaticOffsetPass(bool AllowPartial) : AllowPartial(AllowPartial) {}
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  static bool isRequired() { return true; }

  static std::pair<GetElementPtrInst *, LoadInst *>
  reconstructLoad(CallInst *Call);

  static std::pair<GetElementPtrInst *, StoreInst *>
  reconstructStore(CallInst *Call);
};

} // namespace llvm

#endif
