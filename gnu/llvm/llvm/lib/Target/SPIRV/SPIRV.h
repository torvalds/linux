//===-- SPIRV.h - Top-level interface for SPIR-V representation -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPIRV_SPIRV_H
#define LLVM_LIB_TARGET_SPIRV_SPIRV_H

#include "MCTargetDesc/SPIRVMCTargetDesc.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {
class SPIRVTargetMachine;
class SPIRVSubtarget;
class InstructionSelector;
class RegisterBankInfo;

ModulePass *createSPIRVPrepareFunctionsPass(const SPIRVTargetMachine &TM);
FunctionPass *createSPIRVMergeRegionExitTargetsPass();
FunctionPass *createSPIRVStripConvergenceIntrinsicsPass();
FunctionPass *createSPIRVRegularizerPass();
FunctionPass *createSPIRVPreLegalizerPass();
FunctionPass *createSPIRVPostLegalizerPass();
ModulePass *createSPIRVEmitIntrinsicsPass(SPIRVTargetMachine *TM);
InstructionSelector *
createSPIRVInstructionSelector(const SPIRVTargetMachine &TM,
                               const SPIRVSubtarget &Subtarget,
                               const RegisterBankInfo &RBI);

void initializeSPIRVModuleAnalysisPass(PassRegistry &);
void initializeSPIRVConvergenceRegionAnalysisWrapperPassPass(PassRegistry &);
void initializeSPIRVPreLegalizerPass(PassRegistry &);
void initializeSPIRVPostLegalizerPass(PassRegistry &);
void initializeSPIRVEmitIntrinsicsPass(PassRegistry &);
} // namespace llvm

#endif // LLVM_LIB_TARGET_SPIRV_SPIRV_H
