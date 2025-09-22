//===-- SPIRVDuplicatesTracker.cpp - SPIR-V Duplicates Tracker --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// General infrastructure for keeping track of the values that according to
// the SPIR-V binary layout should be global to the whole module.
//
//===----------------------------------------------------------------------===//

#include "SPIRVDuplicatesTracker.h"

using namespace llvm;

template <typename T>
void SPIRVGeneralDuplicatesTracker::prebuildReg2Entry(
    SPIRVDuplicatesTracker<T> &DT, SPIRVReg2EntryTy &Reg2Entry) {
  for (auto &TPair : DT.getAllUses()) {
    for (auto &RegPair : TPair.second) {
      const MachineFunction *MF = RegPair.first;
      Register R = RegPair.second;
      MachineInstr *MI = MF->getRegInfo().getUniqueVRegDef(R);
      if (!MI)
        continue;
      Reg2Entry[&MI->getOperand(0)] = &TPair.second;
    }
  }
}

void SPIRVGeneralDuplicatesTracker::buildDepsGraph(
    std::vector<SPIRV::DTSortableEntry *> &Graph,
    MachineModuleInfo *MMI = nullptr) {
  SPIRVReg2EntryTy Reg2Entry;
  prebuildReg2Entry(TT, Reg2Entry);
  prebuildReg2Entry(CT, Reg2Entry);
  prebuildReg2Entry(GT, Reg2Entry);
  prebuildReg2Entry(FT, Reg2Entry);
  prebuildReg2Entry(AT, Reg2Entry);
  prebuildReg2Entry(MT, Reg2Entry);
  prebuildReg2Entry(ST, Reg2Entry);

  for (auto &Op2E : Reg2Entry) {
    SPIRV::DTSortableEntry *E = Op2E.second;
    Graph.push_back(E);
    for (auto &U : *E) {
      const MachineRegisterInfo &MRI = U.first->getRegInfo();
      MachineInstr *MI = MRI.getUniqueVRegDef(U.second);
      if (!MI)
        continue;
      assert(MI && MI->getParent() && "No MachineInstr created yet");
      for (auto i = MI->getNumDefs(); i < MI->getNumOperands(); i++) {
        MachineOperand &Op = MI->getOperand(i);
        if (!Op.isReg())
          continue;
        MachineInstr *VRegDef = MRI.getVRegDef(Op.getReg());
        // References to a function via function pointers generate virtual
        // registers without a definition. We are able to resolve this
        // reference using Globar Register info into an OpFunction instruction
        // but do not expect to find it in Reg2Entry.
        if (MI->getOpcode() == SPIRV::OpConstantFunctionPointerINTEL && i == 2)
          continue;
        MachineOperand *RegOp = &VRegDef->getOperand(0);
        assert((MI->getOpcode() == SPIRV::OpVariable && i == 3) ||
               Reg2Entry.count(RegOp));
        if (Reg2Entry.count(RegOp))
          E->addDep(Reg2Entry[RegOp]);
      }

      if (E->getIsFunc()) {
        MachineInstr *Next = MI->getNextNode();
        if (Next && (Next->getOpcode() == SPIRV::OpFunction ||
                     Next->getOpcode() == SPIRV::OpFunctionParameter)) {
          E->addDep(Reg2Entry[&Next->getOperand(0)]);
        }
      }
    }
  }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  if (MMI) {
    const Module *M = MMI->getModule();
    for (auto F = M->begin(), E = M->end(); F != E; ++F) {
      const MachineFunction *MF = MMI->getMachineFunction(*F);
      if (!MF)
        continue;
      for (const MachineBasicBlock &MBB : *MF) {
        for (const MachineInstr &CMI : MBB) {
          MachineInstr &MI = const_cast<MachineInstr &>(CMI);
          MI.dump();
          if (MI.getNumExplicitDefs() > 0 &&
              Reg2Entry.count(&MI.getOperand(0))) {
            dbgs() << "\t[";
            for (SPIRV::DTSortableEntry *D :
                 Reg2Entry.lookup(&MI.getOperand(0))->getDeps())
              dbgs() << Register::virtReg2Index(D->lookup(MF)) << ", ";
            dbgs() << "]\n";
          }
        }
      }
    }
  }
#endif
}
