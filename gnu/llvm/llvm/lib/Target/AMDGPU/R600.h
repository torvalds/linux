//===-- AMDGPU.h - MachineFunction passes hw codegen --------------*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
/// \file
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_R600_H
#define LLVM_LIB_TARGET_AMDGPU_R600_H

#include "llvm/Support/CodeGen.h"

namespace llvm {

class FunctionPass;
class TargetMachine;
class ModulePass;
class PassRegistry;

// R600 Passes
FunctionPass *createR600VectorRegMerger();
FunctionPass *createR600ExpandSpecialInstrsPass();
FunctionPass *createR600EmitClauseMarkers();
FunctionPass *createR600ClauseMergePass();
FunctionPass *createR600Packetizer();
FunctionPass *createR600ControlFlowFinalizer();
FunctionPass *createR600MachineCFGStructurizerPass();
FunctionPass *createR600ISelDag(TargetMachine &TM, CodeGenOptLevel OptLevel);
ModulePass *createR600OpenCLImageTypeLoweringPass();

void initializeR600ClauseMergePassPass(PassRegistry &);
extern char &R600ClauseMergePassID;

void initializeR600ControlFlowFinalizerPass(PassRegistry &);
extern char &R600ControlFlowFinalizerID;

void initializeR600ExpandSpecialInstrsPassPass(PassRegistry &);
extern char &R600ExpandSpecialInstrsPassID;

void initializeR600VectorRegMergerPass(PassRegistry &);
extern char &R600VectorRegMergerID;

void initializeR600PacketizerPass(PassRegistry &);
extern char &R600PacketizerID;

} // End namespace llvm

#endif
