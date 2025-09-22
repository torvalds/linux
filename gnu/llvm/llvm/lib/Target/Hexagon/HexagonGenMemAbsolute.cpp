//===--- HexagonGenMemAbsolute.cpp - Generate Load/Store Set Absolute ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

// This pass traverses through all the basic blocks in a function and converts
// an indexed load/store with offset "0" to a absolute-set load/store
// instruction as long as the use of the register in the new instruction
// dominates the rest of the uses and there are more than 2 uses.

#include "HexagonTargetMachine.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

#define DEBUG_TYPE "hexagon-abs"

using namespace llvm;

STATISTIC(HexagonNumLoadAbsConversions,
          "Number of Load instructions converted to absolute-set form");
STATISTIC(HexagonNumStoreAbsConversions,
          "Number of Store instructions converted to absolute-set form");

namespace llvm {
FunctionPass *createHexagonGenMemAbsolute();
void initializeHexagonGenMemAbsolutePass(PassRegistry &Registry);
} // namespace llvm

namespace {

class HexagonGenMemAbsolute : public MachineFunctionPass {
  const HexagonInstrInfo *TII;
  MachineRegisterInfo *MRI;
  const TargetRegisterInfo *TRI;

public:
  static char ID;
  HexagonGenMemAbsolute() : MachineFunctionPass(ID), TII(0), MRI(0), TRI(0) {
    initializeHexagonGenMemAbsolutePass(*PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override {
    return "Hexagon Generate Load/Store Set Absolute Address Instruction";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    MachineFunctionPass::getAnalysisUsage(AU);
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    AU.addPreserved<MachineDominatorTreeWrapperPass>();
  }

  bool runOnMachineFunction(MachineFunction &Fn) override;

private:
  static bool isValidIndexedLoad(int &Opcode, int &NewOpcode);
  static bool isValidIndexedStore(int &Opcode, int &NewOpcode);
};
} // namespace

char HexagonGenMemAbsolute::ID = 0;

INITIALIZE_PASS(HexagonGenMemAbsolute, "hexagon-gen-load-absolute",
                "Hexagon Generate Load/Store Set Absolute Address Instruction",
                false, false)

bool HexagonGenMemAbsolute::runOnMachineFunction(MachineFunction &Fn) {
  if (skipFunction(Fn.getFunction()))
    return false;

  TII = Fn.getSubtarget<HexagonSubtarget>().getInstrInfo();
  MRI = &Fn.getRegInfo();
  TRI = Fn.getRegInfo().getTargetRegisterInfo();

  MachineDominatorTree &MDT =
      getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();

  // Loop over all of the basic blocks
  for (MachineFunction::iterator MBBb = Fn.begin(), MBBe = Fn.end();
       MBBb != MBBe; ++MBBb) {
    MachineBasicBlock *MBB = &*MBBb;
    // Traverse the basic block
    for (MachineBasicBlock::iterator MII = MBB->begin(); MII != MBB->end();
         ++MII) {
      MachineInstr *MI = &*MII;
      int Opc = MI->getOpcode();
      if (Opc != Hexagon::CONST32 && Opc != Hexagon::A2_tfrsi)
        continue;

      const MachineOperand &MO = MI->getOperand(0);
      if (!MO.isReg() || !MO.isDef())
        continue;

      unsigned DstReg = MO.getReg();
      if (MRI->use_nodbg_empty(DstReg))
        continue;

      typedef MachineRegisterInfo::use_nodbg_iterator use_iterator;
      use_iterator NextUseMI = MRI->use_nodbg_begin(DstReg);

      MachineInstr *NextMI = NextUseMI->getParent();
      int NextOpc = NextMI->getOpcode();
      int NewOpc;
      bool IsLoad = isValidIndexedLoad(NextOpc, NewOpc);

      if (!IsLoad && !isValidIndexedStore(NextOpc, NewOpc))
        continue;

      // Base and Offset positions for load and store instructions
      // Load R(dest), R(base), Imm -> R(dest) = mem(R(base) + Imm)
      // Store R(base), Imm, R (src) -> mem(R(base) + Imm) = R(src)
      unsigned BaseRegPos, ImmPos, RegPos;
      if (!TII->getBaseAndOffsetPosition(*NextMI, BaseRegPos, ImmPos))
        continue;
      RegPos = IsLoad ? 0 : 2;

      bool IsGlobal = MI->getOperand(1).isGlobal();
      if (!MI->getOperand(1).isImm() && !IsGlobal)
        continue;

      const MachineOperand *BaseOp = nullptr;
      int64_t Offset;
      bool Scalable;
      TII->getMemOperandWithOffset(*NextMI, BaseOp, Offset, Scalable, TRI);

      // Ensure BaseOp is non-null and register type.
      if (!BaseOp || !BaseOp->isReg())
        continue;

      if (Scalable)
        continue;

      unsigned BaseReg = BaseOp->getReg();
      if ((DstReg != BaseReg) || (Offset != 0))
        continue;

      const MachineOperand &MO0 = NextMI->getOperand(RegPos);

      if (!MO0.isReg())
        continue;

      unsigned LoadStoreReg = MO0.getReg();

      // Store: Bail out if the src and base are same (def and use on same
      // register).
      if (LoadStoreReg == BaseReg)
        continue;

      // Insert the absolute-set instruction "I" only if the use of the
      // BaseReg in "I" dominates the rest of the uses of BaseReg and if
      // there are more than 2 uses of this BaseReg.
      bool Dominates = true;
      unsigned Counter = 0;
      for (use_iterator I = NextUseMI, E = MRI->use_nodbg_end(); I != E; ++I) {
        Counter++;
        if (!MDT.dominates(NextMI, I->getParent()))
          Dominates = false;
      }

      if ((!Dominates) || (Counter < 3))
        continue;

      // If we reach here, we have met all the conditions required for the
      // replacement of the absolute instruction.
      LLVM_DEBUG({
        dbgs() << "Found a pair of instructions for absolute-set "
               << (IsLoad ? "load" : "store") << "\n";
        dbgs() << *MI;
        dbgs() << *NextMI;
      });
      MachineBasicBlock *ParentBlock = NextMI->getParent();
      MachineInstrBuilder MIB;
      if (IsLoad) { // Insert absolute-set load instruction
        ++HexagonNumLoadAbsConversions;
        MIB = BuildMI(*ParentBlock, NextMI, NextMI->getDebugLoc(),
                      TII->get(NewOpc), LoadStoreReg)
                  .addReg(DstReg, RegState::Define);
      } else { // Insert absolute-set store instruction
        ++HexagonNumStoreAbsConversions;
        MIB = BuildMI(*ParentBlock, NextMI, NextMI->getDebugLoc(),
                      TII->get(NewOpc), DstReg);
      }

      MachineOperand ImmOperand = MI->getOperand(1);
      if (IsGlobal)
        MIB.addGlobalAddress(ImmOperand.getGlobal(), ImmOperand.getOffset(),
                             ImmOperand.getTargetFlags());
      else
        MIB.addImm(ImmOperand.getImm());

      if (IsLoad)
        MIB->getOperand(0).setSubReg(MO0.getSubReg());
      else
        MIB.addReg(LoadStoreReg, 0, MO0.getSubReg());

      LLVM_DEBUG(dbgs() << "Replaced with " << *MIB << "\n");
      // Erase the instructions that got replaced.
      MII = MBB->erase(MI);
      --MII;
      NextMI->getParent()->erase(NextMI);
    }
  }

  return true;
}

bool HexagonGenMemAbsolute::isValidIndexedLoad(int &Opc, int &NewOpc) {

  bool Result = true;
  switch (Opc) {
  case Hexagon::L2_loadrb_io:
    NewOpc = Hexagon::L4_loadrb_ap;
    break;
  case Hexagon::L2_loadrh_io:
    NewOpc = Hexagon::L4_loadrh_ap;
    break;
  case Hexagon::L2_loadri_io:
    NewOpc = Hexagon::L4_loadri_ap;
    break;
  case Hexagon::L2_loadrd_io:
    NewOpc = Hexagon::L4_loadrd_ap;
    break;
  case Hexagon::L2_loadruh_io:
    NewOpc = Hexagon::L4_loadruh_ap;
    break;
  case Hexagon::L2_loadrub_io:
    NewOpc = Hexagon::L4_loadrub_ap;
    break;
  default:
    Result = false;
  }

  return Result;
}

bool HexagonGenMemAbsolute::isValidIndexedStore(int &Opc, int &NewOpc) {

  bool Result = true;
  switch (Opc) {
  case Hexagon::S2_storerd_io:
    NewOpc = Hexagon::S4_storerd_ap;
    break;
  case Hexagon::S2_storeri_io:
    NewOpc = Hexagon::S4_storeri_ap;
    break;
  case Hexagon::S2_storerh_io:
    NewOpc = Hexagon::S4_storerh_ap;
    break;
  case Hexagon::S2_storerb_io:
    NewOpc = Hexagon::S4_storerb_ap;
    break;
  default:
    Result = false;
  }

  return Result;
}

//===----------------------------------------------------------------------===//
//                         Public Constructor Functions
//===----------------------------------------------------------------------===//

FunctionPass *llvm::createHexagonGenMemAbsolute() {
  return new HexagonGenMemAbsolute();
}
