//===- HexagonVExtract.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This pass will replace multiple occurrences of V6_extractw from the same
// vector register with a combination of a vector store and scalar loads.
//===----------------------------------------------------------------------===//

#include "Hexagon.h"
#include "HexagonInstrInfo.h"
#include "HexagonMachineFunctionInfo.h"
#include "HexagonRegisterInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Pass.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/CommandLine.h"

#include <map>

using namespace llvm;

static cl::opt<unsigned> VExtractThreshold(
    "hexagon-vextract-threshold", cl::Hidden, cl::init(1),
    cl::desc("Threshold for triggering vextract replacement"));

namespace llvm {
  void initializeHexagonVExtractPass(PassRegistry& Registry);
  FunctionPass *createHexagonVExtract();
}

namespace {
  class HexagonVExtract : public MachineFunctionPass {
  public:
    static char ID;
    HexagonVExtract() : MachineFunctionPass(ID) {}

    StringRef getPassName() const override {
      return "Hexagon optimize vextract";
    }
    void getAnalysisUsage(AnalysisUsage &AU) const override {
      MachineFunctionPass::getAnalysisUsage(AU);
    }
    bool runOnMachineFunction(MachineFunction &MF) override;

  private:
    const HexagonSubtarget *HST = nullptr;
    const HexagonInstrInfo *HII = nullptr;

    unsigned genElemLoad(MachineInstr *ExtI, unsigned BaseR,
                         MachineRegisterInfo &MRI);
  };

  char HexagonVExtract::ID = 0;
}

INITIALIZE_PASS(HexagonVExtract, "hexagon-vextract",
  "Hexagon optimize vextract", false, false)

unsigned HexagonVExtract::genElemLoad(MachineInstr *ExtI, unsigned BaseR,
                                      MachineRegisterInfo &MRI) {
  MachineBasicBlock &ExtB = *ExtI->getParent();
  DebugLoc DL = ExtI->getDebugLoc();
  Register ElemR = MRI.createVirtualRegister(&Hexagon::IntRegsRegClass);

  Register ExtIdxR = ExtI->getOperand(2).getReg();
  unsigned ExtIdxS = ExtI->getOperand(2).getSubReg();

  // Simplified check for a compile-time constant value of ExtIdxR.
  if (ExtIdxS == 0) {
    MachineInstr *DI = MRI.getVRegDef(ExtIdxR);
    if (DI->getOpcode() == Hexagon::A2_tfrsi) {
      unsigned V = DI->getOperand(1).getImm();
      V &= (HST->getVectorLength()-1) & -4u;

      BuildMI(ExtB, ExtI, DL, HII->get(Hexagon::L2_loadri_io), ElemR)
        .addReg(BaseR)
        .addImm(V);
      return ElemR;
    }
  }

  Register IdxR = MRI.createVirtualRegister(&Hexagon::IntRegsRegClass);
  BuildMI(ExtB, ExtI, DL, HII->get(Hexagon::A2_andir), IdxR)
    .add(ExtI->getOperand(2))
    .addImm(-4);
  BuildMI(ExtB, ExtI, DL, HII->get(Hexagon::L4_loadri_rr), ElemR)
    .addReg(BaseR)
    .addReg(IdxR)
    .addImm(0);
  return ElemR;
}

bool HexagonVExtract::runOnMachineFunction(MachineFunction &MF) {
  HST = &MF.getSubtarget<HexagonSubtarget>();
  HII = HST->getInstrInfo();
  const auto &HRI = *HST->getRegisterInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  Register AR =
      MF.getInfo<HexagonMachineFunctionInfo>()->getStackAlignBaseReg();
  std::map<unsigned, SmallVector<MachineInstr *, 4>> VExtractMap;
  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      unsigned Opc = MI.getOpcode();
      if (Opc != Hexagon::V6_extractw)
        continue;
      Register VecR = MI.getOperand(1).getReg();
      VExtractMap[VecR].push_back(&MI);
    }
  }

  auto EmitAddr = [&] (MachineBasicBlock &BB, MachineBasicBlock::iterator At,
                       DebugLoc dl, int FI, unsigned Offset) {
    Register AddrR = MRI.createVirtualRegister(&Hexagon::IntRegsRegClass);
    unsigned FiOpc = AR != 0 ? Hexagon::PS_fia : Hexagon::PS_fi;
    auto MIB = BuildMI(BB, At, dl, HII->get(FiOpc), AddrR);
    if (AR)
      MIB.addReg(AR);
    MIB.addFrameIndex(FI).addImm(Offset);
    return AddrR;
  };

  MaybeAlign MaxAlign;
  for (auto &P : VExtractMap) {
    unsigned VecR = P.first;
    if (P.second.size() <= VExtractThreshold)
      continue;

    const auto &VecRC = *MRI.getRegClass(VecR);
    Align Alignment = HRI.getSpillAlign(VecRC);
    MaxAlign = std::max(MaxAlign.valueOrOne(), Alignment);
    // Make sure this is not a spill slot: spill slots cannot be aligned
    // if there are variable-sized objects on the stack. They must be
    // accessible via FP (which is not aligned), because SP is unknown,
    // and AP may not be available at the location of the load/store.
    int FI = MFI.CreateStackObject(HRI.getSpillSize(VecRC), Alignment,
                                   /*isSpillSlot*/ false);

    MachineInstr *DefI = MRI.getVRegDef(VecR);
    MachineBasicBlock::iterator At = std::next(DefI->getIterator());
    MachineBasicBlock &DefB = *DefI->getParent();
    unsigned StoreOpc = VecRC.getID() == Hexagon::HvxVRRegClassID
                          ? Hexagon::V6_vS32b_ai
                          : Hexagon::PS_vstorerw_ai;
    Register AddrR = EmitAddr(DefB, At, DefI->getDebugLoc(), FI, 0);
    BuildMI(DefB, At, DefI->getDebugLoc(), HII->get(StoreOpc))
      .addReg(AddrR)
      .addImm(0)
      .addReg(VecR);

    unsigned VecSize = HRI.getRegSizeInBits(VecRC) / 8;

    for (MachineInstr *ExtI : P.second) {
      assert(ExtI->getOpcode() == Hexagon::V6_extractw);
      unsigned SR = ExtI->getOperand(1).getSubReg();
      assert(ExtI->getOperand(1).getReg() == VecR);

      MachineBasicBlock &ExtB = *ExtI->getParent();
      DebugLoc DL = ExtI->getDebugLoc();
      Register BaseR = EmitAddr(ExtB, ExtI, ExtI->getDebugLoc(), FI,
                                SR == 0 ? 0 : VecSize/2);

      unsigned ElemR = genElemLoad(ExtI, BaseR, MRI);
      Register ExtR = ExtI->getOperand(0).getReg();
      MRI.replaceRegWith(ExtR, ElemR);
      ExtB.erase(ExtI);
      Changed = true;
    }
  }

  if (AR && MaxAlign) {
    // Update the required stack alignment.
    MachineInstr *AlignaI = MRI.getVRegDef(AR);
    assert(AlignaI->getOpcode() == Hexagon::PS_aligna);
    MachineOperand &Op = AlignaI->getOperand(1);
    if (*MaxAlign > Op.getImm())
      Op.setImm(MaxAlign->value());
  }

  return Changed;
}

FunctionPass *llvm::createHexagonVExtract() {
  return new HexagonVExtract();
}
