//===-- llvm/CodeGen/MachineModuleInfo.cpp ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/MachineModuleSlotTracker.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/Module.h"

using namespace llvm;

void MachineModuleSlotTracker::processMachineFunctionMetadata(
    AbstractSlotTrackerStorage *AST, const MachineFunction &MF) {
  // Create metadata created within the backend.
  for (const MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB.instrs())
      for (const MachineMemOperand *MMO : MI.memoperands()) {
        AAMDNodes AAInfo = MMO->getAAInfo();
        if (AAInfo.TBAA)
          AST->createMetadataSlot(AAInfo.TBAA);
        if (AAInfo.TBAAStruct)
          AST->createMetadataSlot(AAInfo.TBAAStruct);
        if (AAInfo.Scope)
          AST->createMetadataSlot(AAInfo.Scope);
        if (AAInfo.NoAlias)
          AST->createMetadataSlot(AAInfo.NoAlias);
      }
}

void MachineModuleSlotTracker::processMachineModule(
    AbstractSlotTrackerStorage *AST, const Module *M,
    bool ShouldInitializeAllMetadata) {
  if (ShouldInitializeAllMetadata) {
    for (const Function &F : *M) {
      if (&F != &TheFunction)
        continue;
      MDNStartSlot = AST->getNextMetadataSlot();
      if (auto *MF = TheMMI.getMachineFunction(F))
        processMachineFunctionMetadata(AST, *MF);
      MDNEndSlot = AST->getNextMetadataSlot();
      break;
    }
  }
}

void MachineModuleSlotTracker::processMachineFunction(
    AbstractSlotTrackerStorage *AST, const Function *F,
    bool ShouldInitializeAllMetadata) {
  if (!ShouldInitializeAllMetadata && F == &TheFunction) {
    MDNStartSlot = AST->getNextMetadataSlot();
    if (auto *MF = TheMMI.getMachineFunction(*F))
      processMachineFunctionMetadata(AST, *MF);
    MDNEndSlot = AST->getNextMetadataSlot();
  }
}

void MachineModuleSlotTracker::collectMachineMDNodes(
    MachineMDNodeListType &L) const {
  collectMDNodes(L, MDNStartSlot, MDNEndSlot);
}

MachineModuleSlotTracker::MachineModuleSlotTracker(
    const MachineFunction *MF, bool ShouldInitializeAllMetadata)
    : ModuleSlotTracker(MF->getFunction().getParent(),
                        ShouldInitializeAllMetadata),
      TheFunction(MF->getFunction()), TheMMI(MF->getMMI()) {
  setProcessHook([this](AbstractSlotTrackerStorage *AST, const Module *M,
                        bool ShouldInitializeAllMetadata) {
    this->processMachineModule(AST, M, ShouldInitializeAllMetadata);
  });
  setProcessHook([this](AbstractSlotTrackerStorage *AST, const Function *F,
                        bool ShouldInitializeAllMetadata) {
    this->processMachineFunction(AST, F, ShouldInitializeAllMetadata);
  });
}

MachineModuleSlotTracker::~MachineModuleSlotTracker() = default;
