//===-- ARM.h - Top-level interface for ARM representation ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the entry points for global functions defined in the LLVM
// ARM back-end.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARM_H
#define LLVM_LIB_TARGET_ARM_ARM_H

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/CodeGen.h"
#include <functional>

namespace llvm {

class ARMAsmPrinter;
class ARMBaseTargetMachine;
class ARMRegisterBankInfo;
class ARMSubtarget;
class Function;
class FunctionPass;
class InstructionSelector;
class MCInst;
class MachineInstr;
class PassRegistry;

Pass *createMVETailPredicationPass();
FunctionPass *createARMLowOverheadLoopsPass();
FunctionPass *createARMBlockPlacementPass();
Pass *createARMParallelDSPPass();
FunctionPass *createARMISelDag(ARMBaseTargetMachine &TM,
                               CodeGenOptLevel OptLevel);
FunctionPass *createA15SDOptimizerPass();
FunctionPass *createARMLoadStoreOptimizationPass(bool PreAlloc = false);
FunctionPass *createARMExpandPseudoPass();
FunctionPass *createARMBranchTargetsPass();
FunctionPass *createARMConstantIslandPass();
FunctionPass *createMLxExpansionPass();
FunctionPass *createThumb2ITBlockPass();
FunctionPass *createMVEVPTBlockPass();
FunctionPass *createMVETPAndVPTOptimisationsPass();
FunctionPass *createARMOptimizeBarriersPass();
FunctionPass *createThumb2SizeReductionPass(
    std::function<bool(const Function &)> Ftor = nullptr);
InstructionSelector *
createARMInstructionSelector(const ARMBaseTargetMachine &TM, const ARMSubtarget &STI,
                             const ARMRegisterBankInfo &RBI);
Pass *createMVEGatherScatterLoweringPass();
FunctionPass *createARMSLSHardeningPass();
FunctionPass *createARMIndirectThunks();
Pass *createMVELaneInterleavingPass();
FunctionPass *createARMFixCortexA57AES1742098Pass();

void LowerARMMachineInstrToMCInst(const MachineInstr *MI, MCInst &OutMI,
                                  ARMAsmPrinter &AP);

void initializeARMBlockPlacementPass(PassRegistry &);
void initializeARMBranchTargetsPass(PassRegistry &);
void initializeARMConstantIslandsPass(PassRegistry &);
void initializeARMDAGToDAGISelLegacyPass(PassRegistry &);
void initializeARMExpandPseudoPass(PassRegistry &);
void initializeARMFixCortexA57AES1742098Pass(PassRegistry &);
void initializeARMLoadStoreOptPass(PassRegistry &);
void initializeARMLowOverheadLoopsPass(PassRegistry &);
void initializeARMParallelDSPPass(PassRegistry &);
void initializeARMPreAllocLoadStoreOptPass(PassRegistry &);
void initializeARMSLSHardeningPass(PassRegistry &);
void initializeMVEGatherScatterLoweringPass(PassRegistry &);
void initializeMVELaneInterleavingPass(PassRegistry &);
void initializeMVETPAndVPTOptimisationsPass(PassRegistry &);
void initializeMVETailPredicationPass(PassRegistry &);
void initializeMVEVPTBlockPass(PassRegistry &);
void initializeThumb2ITBlockPass(PassRegistry &);
void initializeThumb2SizeReducePass(PassRegistry &);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARM_ARM_H
