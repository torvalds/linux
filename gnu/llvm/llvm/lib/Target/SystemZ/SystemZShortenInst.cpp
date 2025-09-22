//===-- SystemZShortenInst.cpp - Instruction-shortening pass --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass tries to replace instructions with shorter forms.  For example,
// IILF can be replaced with LLILL or LLILH if the constant fits and if the
// other 32 bits of the GR64 destination are not live.
//
//===----------------------------------------------------------------------===//

#include "SystemZTargetMachine.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

using namespace llvm;

#define DEBUG_TYPE "systemz-shorten-inst"

namespace {
class SystemZShortenInst : public MachineFunctionPass {
public:
  static char ID;
  SystemZShortenInst();

  bool processBlock(MachineBasicBlock &MBB);
  bool runOnMachineFunction(MachineFunction &F) override;
  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

private:
  bool shortenIIF(MachineInstr &MI, unsigned LLIxL, unsigned LLIxH);
  bool shortenOn0(MachineInstr &MI, unsigned Opcode);
  bool shortenOn01(MachineInstr &MI, unsigned Opcode);
  bool shortenOn001(MachineInstr &MI, unsigned Opcode);
  bool shortenOn001AddCC(MachineInstr &MI, unsigned Opcode);
  bool shortenFPConv(MachineInstr &MI, unsigned Opcode);
  bool shortenFusedFPOp(MachineInstr &MI, unsigned Opcode);

  const SystemZInstrInfo *TII;
  const TargetRegisterInfo *TRI;
  LiveRegUnits LiveRegs;
};

char SystemZShortenInst::ID = 0;
} // end anonymous namespace

INITIALIZE_PASS(SystemZShortenInst, DEBUG_TYPE,
                "SystemZ Instruction Shortening", false, false)

FunctionPass *llvm::createSystemZShortenInstPass(SystemZTargetMachine &TM) {
  return new SystemZShortenInst();
}

SystemZShortenInst::SystemZShortenInst()
    : MachineFunctionPass(ID), TII(nullptr) {
  initializeSystemZShortenInstPass(*PassRegistry::getPassRegistry());
}

// Tie operands if MI has become a two-address instruction.
static void tieOpsIfNeeded(MachineInstr &MI) {
  if (MI.getDesc().getOperandConstraint(1, MCOI::TIED_TO) == 0 &&
      !MI.getOperand(0).isTied())
    MI.tieOperands(0, 1);
}

// MI loads one word of a GPR using an IIxF instruction and LLIxL and LLIxH
// are the halfword immediate loads for the same word.  Try to use one of them
// instead of IIxF.
bool SystemZShortenInst::shortenIIF(MachineInstr &MI, unsigned LLIxL,
                                    unsigned LLIxH) {
  Register Reg = MI.getOperand(0).getReg();
  // The new opcode will clear the other half of the GR64 reg, so
  // cancel if that is live.
  unsigned thisSubRegIdx =
      (SystemZ::GRH32BitRegClass.contains(Reg) ? SystemZ::subreg_h32
                                               : SystemZ::subreg_l32);
  unsigned otherSubRegIdx =
      (thisSubRegIdx == SystemZ::subreg_l32 ? SystemZ::subreg_h32
                                            : SystemZ::subreg_l32);
  unsigned GR64BitReg =
      TRI->getMatchingSuperReg(Reg, thisSubRegIdx, &SystemZ::GR64BitRegClass);
  Register OtherReg = TRI->getSubReg(GR64BitReg, otherSubRegIdx);
  if (!LiveRegs.available(OtherReg))
    return false;

  uint64_t Imm = MI.getOperand(1).getImm();
  if (SystemZ::isImmLL(Imm)) {
    MI.setDesc(TII->get(LLIxL));
    MI.getOperand(0).setReg(SystemZMC::getRegAsGR64(Reg));
    return true;
  }
  if (SystemZ::isImmLH(Imm)) {
    MI.setDesc(TII->get(LLIxH));
    MI.getOperand(0).setReg(SystemZMC::getRegAsGR64(Reg));
    MI.getOperand(1).setImm(Imm >> 16);
    return true;
  }
  return false;
}

// Change MI's opcode to Opcode if register operand 0 has a 4-bit encoding.
bool SystemZShortenInst::shortenOn0(MachineInstr &MI, unsigned Opcode) {
  if (SystemZMC::getFirstReg(MI.getOperand(0).getReg()) < 16) {
    MI.setDesc(TII->get(Opcode));
    return true;
  }
  return false;
}

// Change MI's opcode to Opcode if register operands 0 and 1 have a
// 4-bit encoding.
bool SystemZShortenInst::shortenOn01(MachineInstr &MI, unsigned Opcode) {
  if (SystemZMC::getFirstReg(MI.getOperand(0).getReg()) < 16 &&
      SystemZMC::getFirstReg(MI.getOperand(1).getReg()) < 16) {
    MI.setDesc(TII->get(Opcode));
    return true;
  }
  return false;
}

// Change MI's opcode to Opcode if register operands 0, 1 and 2 have a
// 4-bit encoding and if operands 0 and 1 are tied. Also ties op 0
// with op 1, if MI becomes 2-address.
bool SystemZShortenInst::shortenOn001(MachineInstr &MI, unsigned Opcode) {
  if (SystemZMC::getFirstReg(MI.getOperand(0).getReg()) < 16 &&
      MI.getOperand(1).getReg() == MI.getOperand(0).getReg() &&
      SystemZMC::getFirstReg(MI.getOperand(2).getReg()) < 16) {
    MI.setDesc(TII->get(Opcode));
    tieOpsIfNeeded(MI);
    return true;
  }
  return false;
}

// Calls shortenOn001 if CCLive is false. CC def operand is added in
// case of success.
bool SystemZShortenInst::shortenOn001AddCC(MachineInstr &MI, unsigned Opcode) {
  if (LiveRegs.available(SystemZ::CC) && shortenOn001(MI, Opcode)) {
    MachineInstrBuilder(*MI.getParent()->getParent(), &MI)
      .addReg(SystemZ::CC, RegState::ImplicitDefine | RegState::Dead);
    return true;
  }
  return false;
}

// MI is a vector-style conversion instruction with the operand order:
// destination, source, exact-suppress, rounding-mode.  If both registers
// have a 4-bit encoding then change it to Opcode, which has operand order:
// destination, rouding-mode, source, exact-suppress.
bool SystemZShortenInst::shortenFPConv(MachineInstr &MI, unsigned Opcode) {
  if (SystemZMC::getFirstReg(MI.getOperand(0).getReg()) < 16 &&
      SystemZMC::getFirstReg(MI.getOperand(1).getReg()) < 16) {
    MachineOperand Dest(MI.getOperand(0));
    MachineOperand Src(MI.getOperand(1));
    MachineOperand Suppress(MI.getOperand(2));
    MachineOperand Mode(MI.getOperand(3));
    MI.removeOperand(3);
    MI.removeOperand(2);
    MI.removeOperand(1);
    MI.removeOperand(0);
    MI.setDesc(TII->get(Opcode));
    MachineInstrBuilder(*MI.getParent()->getParent(), &MI)
        .add(Dest)
        .add(Mode)
        .add(Src)
        .add(Suppress);
    return true;
  }
  return false;
}

bool SystemZShortenInst::shortenFusedFPOp(MachineInstr &MI, unsigned Opcode) {
  MachineOperand &DstMO = MI.getOperand(0);
  MachineOperand &LHSMO = MI.getOperand(1);
  MachineOperand &RHSMO = MI.getOperand(2);
  MachineOperand &AccMO = MI.getOperand(3);
  if (SystemZMC::getFirstReg(DstMO.getReg()) < 16 &&
      SystemZMC::getFirstReg(LHSMO.getReg()) < 16 &&
      SystemZMC::getFirstReg(RHSMO.getReg()) < 16 &&
      SystemZMC::getFirstReg(AccMO.getReg()) < 16 &&
      DstMO.getReg() == AccMO.getReg()) {
    MachineOperand Lhs(LHSMO);
    MachineOperand Rhs(RHSMO);
    MachineOperand Src(AccMO);
    MI.removeOperand(3);
    MI.removeOperand(2);
    MI.removeOperand(1);
    MI.setDesc(TII->get(Opcode));
    MachineInstrBuilder(*MI.getParent()->getParent(), &MI)
        .add(Src)
        .add(Lhs)
        .add(Rhs);
    return true;
  }
  return false;
}

// Process all instructions in MBB.  Return true if something changed.
bool SystemZShortenInst::processBlock(MachineBasicBlock &MBB) {
  bool Changed = false;

  // Set up the set of live registers at the end of MBB (live out)
  LiveRegs.clear();
  LiveRegs.addLiveOuts(MBB);

  // Iterate backwards through the block looking for instructions to change.
  for (MachineInstr &MI : llvm::reverse(MBB)) {
    switch (MI.getOpcode()) {
    case SystemZ::IILF:
      Changed |= shortenIIF(MI, SystemZ::LLILL, SystemZ::LLILH);
      break;

    case SystemZ::IIHF:
      Changed |= shortenIIF(MI, SystemZ::LLIHL, SystemZ::LLIHH);
      break;

    case SystemZ::WFADB:
      Changed |= shortenOn001AddCC(MI, SystemZ::ADBR);
      break;

    case SystemZ::WFASB:
      Changed |= shortenOn001AddCC(MI, SystemZ::AEBR);
      break;

    case SystemZ::WFDDB:
      Changed |= shortenOn001(MI, SystemZ::DDBR);
      break;

    case SystemZ::WFDSB:
      Changed |= shortenOn001(MI, SystemZ::DEBR);
      break;

    case SystemZ::WFIDB:
      Changed |= shortenFPConv(MI, SystemZ::FIDBRA);
      break;

    case SystemZ::WFISB:
      Changed |= shortenFPConv(MI, SystemZ::FIEBRA);
      break;

    case SystemZ::WLDEB:
      Changed |= shortenOn01(MI, SystemZ::LDEBR);
      break;

    case SystemZ::WLEDB:
      Changed |= shortenFPConv(MI, SystemZ::LEDBRA);
      break;

    case SystemZ::WFMDB:
      Changed |= shortenOn001(MI, SystemZ::MDBR);
      break;

    case SystemZ::WFMSB:
      Changed |= shortenOn001(MI, SystemZ::MEEBR);
      break;

    case SystemZ::WFMADB:
      Changed |= shortenFusedFPOp(MI, SystemZ::MADBR);
      break;

    case SystemZ::WFMASB:
      Changed |= shortenFusedFPOp(MI, SystemZ::MAEBR);
      break;

    case SystemZ::WFMSDB:
      Changed |= shortenFusedFPOp(MI, SystemZ::MSDBR);
      break;

    case SystemZ::WFMSSB:
      Changed |= shortenFusedFPOp(MI, SystemZ::MSEBR);
      break;

    case SystemZ::WFLCDB:
      Changed |= shortenOn01(MI, SystemZ::LCDFR);
      break;

    case SystemZ::WFLCSB:
      Changed |= shortenOn01(MI, SystemZ::LCDFR_32);
      break;

    case SystemZ::WFLNDB:
      Changed |= shortenOn01(MI, SystemZ::LNDFR);
      break;

    case SystemZ::WFLNSB:
      Changed |= shortenOn01(MI, SystemZ::LNDFR_32);
      break;

    case SystemZ::WFLPDB:
      Changed |= shortenOn01(MI, SystemZ::LPDFR);
      break;

    case SystemZ::WFLPSB:
      Changed |= shortenOn01(MI, SystemZ::LPDFR_32);
      break;

    case SystemZ::WFSQDB:
      Changed |= shortenOn01(MI, SystemZ::SQDBR);
      break;

    case SystemZ::WFSQSB:
      Changed |= shortenOn01(MI, SystemZ::SQEBR);
      break;

    case SystemZ::WFSDB:
      Changed |= shortenOn001AddCC(MI, SystemZ::SDBR);
      break;

    case SystemZ::WFSSB:
      Changed |= shortenOn001AddCC(MI, SystemZ::SEBR);
      break;

    case SystemZ::WFCDB:
      Changed |= shortenOn01(MI, SystemZ::CDBR);
      break;

    case SystemZ::WFCSB:
      Changed |= shortenOn01(MI, SystemZ::CEBR);
      break;

    case SystemZ::WFKDB:
      Changed |= shortenOn01(MI, SystemZ::KDBR);
      break;

    case SystemZ::WFKSB:
      Changed |= shortenOn01(MI, SystemZ::KEBR);
      break;

    case SystemZ::VL32:
      // For z13 we prefer LDE over LE to avoid partial register dependencies.
      Changed |= shortenOn0(MI, SystemZ::LDE32);
      break;

    case SystemZ::VST32:
      Changed |= shortenOn0(MI, SystemZ::STE);
      break;

    case SystemZ::VL64:
      Changed |= shortenOn0(MI, SystemZ::LD);
      break;

    case SystemZ::VST64:
      Changed |= shortenOn0(MI, SystemZ::STD);
      break;

    default: {
      int TwoOperandOpcode = SystemZ::getTwoOperandOpcode(MI.getOpcode());
      if (TwoOperandOpcode == -1)
        break;

      if ((MI.getOperand(0).getReg() != MI.getOperand(1).getReg()) &&
          (!MI.isCommutable() ||
           MI.getOperand(0).getReg() != MI.getOperand(2).getReg() ||
           !TII->commuteInstruction(MI, false, 1, 2)))
          break;

      MI.setDesc(TII->get(TwoOperandOpcode));
      MI.tieOperands(0, 1);
      if (TwoOperandOpcode == SystemZ::SLL ||
          TwoOperandOpcode == SystemZ::SLA ||
          TwoOperandOpcode == SystemZ::SRL ||
          TwoOperandOpcode == SystemZ::SRA) {
        // These shifts only use the low 6 bits of the shift count.
        MachineOperand &ImmMO = MI.getOperand(3);
        ImmMO.setImm(ImmMO.getImm() & 0xfff);
      }
      Changed = true;
      break;
    }
    }

    LiveRegs.stepBackward(MI);
  }

  return Changed;
}

bool SystemZShortenInst::runOnMachineFunction(MachineFunction &F) {
  if (skipFunction(F.getFunction()))
    return false;

  const SystemZSubtarget &ST = F.getSubtarget<SystemZSubtarget>();
  TII = ST.getInstrInfo();
  TRI = ST.getRegisterInfo();
  LiveRegs.init(*TRI);

  bool Changed = false;
  for (auto &MBB : F)
    Changed |= processBlock(MBB);

  return Changed;
}
