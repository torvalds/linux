//==-- AArch64.h - Top-level interface for AArch64  --------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// AArch64 back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AARCH64_AARCH64_H
#define LLVM_LIB_TARGET_AARCH64_AARCH64_H

#include "MCTargetDesc/AArch64MCTargetDesc.h"
#include "Utils/AArch64BaseInfo.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

class AArch64RegisterBankInfo;
class AArch64Subtarget;
class AArch64TargetMachine;
class FunctionPass;
class InstructionSelector;

FunctionPass *createAArch64DeadRegisterDefinitions();
FunctionPass *createAArch64RedundantCopyEliminationPass();
FunctionPass *createAArch64CondBrTuning();
FunctionPass *createAArch64CompressJumpTablesPass();
FunctionPass *createAArch64ConditionalCompares();
FunctionPass *createAArch64AdvSIMDScalar();
FunctionPass *createAArch64ISelDag(AArch64TargetMachine &TM,
                                   CodeGenOptLevel OptLevel);
FunctionPass *createAArch64StorePairSuppressPass();
FunctionPass *createAArch64ExpandPseudoPass();
FunctionPass *createAArch64SLSHardeningPass();
FunctionPass *createAArch64SpeculationHardeningPass();
FunctionPass *createAArch64LoadStoreOptimizationPass();
ModulePass *createAArch64LowerHomogeneousPrologEpilogPass();
FunctionPass *createAArch64SIMDInstrOptPass();
ModulePass *createAArch64PromoteConstantPass();
FunctionPass *createAArch64ConditionOptimizerPass();
FunctionPass *createAArch64A57FPLoadBalancing();
FunctionPass *createAArch64A53Fix835769();
FunctionPass *createFalkorHWPFFixPass();
FunctionPass *createFalkorMarkStridedAccessesPass();
FunctionPass *createAArch64PointerAuthPass();
FunctionPass *createAArch64BranchTargetsPass();
FunctionPass *createAArch64MIPeepholeOptPass();
FunctionPass *createAArch64PostCoalescerPass();

FunctionPass *createAArch64CleanupLocalDynamicTLSPass();

FunctionPass *createAArch64CollectLOHPass();
FunctionPass *createSMEABIPass();
ModulePass *createSVEIntrinsicOptsPass();
InstructionSelector *
createAArch64InstructionSelector(const AArch64TargetMachine &,
                                 const AArch64Subtarget &,
                                 const AArch64RegisterBankInfo &);
FunctionPass *createAArch64O0PreLegalizerCombiner();
FunctionPass *createAArch64PreLegalizerCombiner();
FunctionPass *createAArch64PostLegalizerCombiner(bool IsOptNone);
FunctionPass *createAArch64PostLegalizerLowering();
FunctionPass *createAArch64PostSelectOptimize();
FunctionPass *createAArch64StackTaggingPass(bool IsOptNone);
FunctionPass *createAArch64StackTaggingPreRAPass();
ModulePass *createAArch64GlobalsTaggingPass();
ModulePass *createAArch64Arm64ECCallLoweringPass();

void initializeAArch64A53Fix835769Pass(PassRegistry&);
void initializeAArch64A57FPLoadBalancingPass(PassRegistry&);
void initializeAArch64AdvSIMDScalarPass(PassRegistry&);
void initializeAArch64PointerAuthPass(PassRegistry&);
void initializeAArch64BranchTargetsPass(PassRegistry&);
void initializeAArch64CFIFixupPass(PassRegistry&);
void initializeAArch64CollectLOHPass(PassRegistry &);
void initializeAArch64CompressJumpTablesPass(PassRegistry&);
void initializeAArch64CondBrTuningPass(PassRegistry &);
void initializeAArch64ConditionOptimizerPass(PassRegistry&);
void initializeAArch64ConditionalComparesPass(PassRegistry &);
void initializeAArch64DAGToDAGISelLegacyPass(PassRegistry &);
void initializeAArch64DeadRegisterDefinitionsPass(PassRegistry&);
void initializeAArch64ExpandPseudoPass(PassRegistry &);
void initializeAArch64GlobalsTaggingPass(PassRegistry &);
void initializeAArch64LoadStoreOptPass(PassRegistry&);
void initializeAArch64LowerHomogeneousPrologEpilogPass(PassRegistry &);
void initializeAArch64MIPeepholeOptPass(PassRegistry &);
void initializeAArch64O0PreLegalizerCombinerPass(PassRegistry &);
void initializeAArch64PostCoalescerPass(PassRegistry &);
void initializeAArch64PostLegalizerCombinerPass(PassRegistry &);
void initializeAArch64PostLegalizerLoweringPass(PassRegistry &);
void initializeAArch64PostSelectOptimizePass(PassRegistry &);
void initializeAArch64PreLegalizerCombinerPass(PassRegistry &);
void initializeAArch64PromoteConstantPass(PassRegistry&);
void initializeAArch64RedundantCopyEliminationPass(PassRegistry&);
void initializeAArch64SIMDInstrOptPass(PassRegistry &);
void initializeAArch64SLSHardeningPass(PassRegistry &);
void initializeAArch64SpeculationHardeningPass(PassRegistry &);
void initializeAArch64StackTaggingPass(PassRegistry &);
void initializeAArch64StackTaggingPreRAPass(PassRegistry &);
void initializeAArch64StorePairSuppressPass(PassRegistry&);
void initializeFalkorHWPFFixPass(PassRegistry&);
void initializeFalkorMarkStridedAccessesLegacyPass(PassRegistry&);
void initializeLDTLSCleanupPass(PassRegistry&);
void initializeSMEABIPass(PassRegistry &);
void initializeSVEIntrinsicOptsPass(PassRegistry &);
void initializeAArch64Arm64ECCallLoweringPass(PassRegistry &);
} // end namespace llvm

#endif
