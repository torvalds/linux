//===-- AVRInstrInfo.cpp - AVR Instruction Information --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the AVR implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "AVRInstrInfo.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"

#include "AVR.h"
#include "AVRMachineFunctionInfo.h"
#include "AVRRegisterInfo.h"
#include "AVRTargetMachine.h"
#include "MCTargetDesc/AVRMCTargetDesc.h"

#define GET_INSTRINFO_CTOR_DTOR
#include "AVRGenInstrInfo.inc"

namespace llvm {

AVRInstrInfo::AVRInstrInfo(AVRSubtarget &STI)
    : AVRGenInstrInfo(AVR::ADJCALLSTACKDOWN, AVR::ADJCALLSTACKUP), RI(),
      STI(STI) {}

void AVRInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MI,
                               const DebugLoc &DL, MCRegister DestReg,
                               MCRegister SrcReg, bool KillSrc) const {
  const AVRRegisterInfo &TRI = *STI.getRegisterInfo();
  unsigned Opc;

  if (AVR::DREGSRegClass.contains(DestReg, SrcReg)) {
    // If our AVR has `movw`, let's emit that; otherwise let's emit two separate
    // `mov`s.
    if (STI.hasMOVW() && AVR::DREGSMOVWRegClass.contains(DestReg, SrcReg)) {
      BuildMI(MBB, MI, DL, get(AVR::MOVWRdRr), DestReg)
          .addReg(SrcReg, getKillRegState(KillSrc));
    } else {
      Register DestLo, DestHi, SrcLo, SrcHi;

      TRI.splitReg(DestReg, DestLo, DestHi);
      TRI.splitReg(SrcReg, SrcLo, SrcHi);

      // Emit the copies.
      // The original instruction was for a register pair, of which only one
      // register might have been live. Add 'undef' to satisfy the machine
      // verifier, when subreg liveness is enabled.
      // TODO: Eliminate these unnecessary copies.
      if (DestLo == SrcHi) {
        BuildMI(MBB, MI, DL, get(AVR::MOVRdRr), DestHi)
            .addReg(SrcHi, getKillRegState(KillSrc) | RegState::Undef);
        BuildMI(MBB, MI, DL, get(AVR::MOVRdRr), DestLo)
            .addReg(SrcLo, getKillRegState(KillSrc) | RegState::Undef);
      } else {
        BuildMI(MBB, MI, DL, get(AVR::MOVRdRr), DestLo)
            .addReg(SrcLo, getKillRegState(KillSrc) | RegState::Undef);
        BuildMI(MBB, MI, DL, get(AVR::MOVRdRr), DestHi)
            .addReg(SrcHi, getKillRegState(KillSrc) | RegState::Undef);
      }
    }
  } else {
    if (AVR::GPR8RegClass.contains(DestReg, SrcReg)) {
      Opc = AVR::MOVRdRr;
    } else if (SrcReg == AVR::SP && AVR::DREGSRegClass.contains(DestReg)) {
      Opc = AVR::SPREAD;
    } else if (DestReg == AVR::SP && AVR::DREGSRegClass.contains(SrcReg)) {
      Opc = AVR::SPWRITE;
    } else {
      llvm_unreachable("Impossible reg-to-reg copy");
    }

    BuildMI(MBB, MI, DL, get(Opc), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
  }
}

Register AVRInstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                           int &FrameIndex) const {
  switch (MI.getOpcode()) {
  case AVR::LDDRdPtrQ:
  case AVR::LDDWRdYQ: { //: FIXME: remove this once PR13375 gets fixed
    if (MI.getOperand(1).isFI() && MI.getOperand(2).isImm() &&
        MI.getOperand(2).getImm() == 0) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
    break;
  }
  default:
    break;
  }

  return 0;
}

Register AVRInstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                          int &FrameIndex) const {
  switch (MI.getOpcode()) {
  case AVR::STDPtrQRr:
  case AVR::STDWPtrQRr: {
    if (MI.getOperand(0).isFI() && MI.getOperand(1).isImm() &&
        MI.getOperand(1).getImm() == 0) {
      FrameIndex = MI.getOperand(0).getIndex();
      return MI.getOperand(2).getReg();
    }
    break;
  }
  default:
    break;
  }

  return 0;
}

void AVRInstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool isKill, int FrameIndex, const TargetRegisterClass *RC,
    const TargetRegisterInfo *TRI, Register VReg) const {
  MachineFunction &MF = *MBB.getParent();
  AVRMachineFunctionInfo *AFI = MF.getInfo<AVRMachineFunctionInfo>();

  AFI->setHasSpills(true);

  const MachineFrameInfo &MFI = MF.getFrameInfo();

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIndex),
      MachineMemOperand::MOStore, MFI.getObjectSize(FrameIndex),
      MFI.getObjectAlign(FrameIndex));

  unsigned Opcode = 0;
  if (TRI->isTypeLegalForClass(*RC, MVT::i8)) {
    Opcode = AVR::STDPtrQRr;
  } else if (TRI->isTypeLegalForClass(*RC, MVT::i16)) {
    Opcode = AVR::STDWPtrQRr;
  } else {
    llvm_unreachable("Cannot store this register into a stack slot!");
  }

  BuildMI(MBB, MI, DebugLoc(), get(Opcode))
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .addReg(SrcReg, getKillRegState(isKill))
      .addMemOperand(MMO);
}

void AVRInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MI,
                                        Register DestReg, int FrameIndex,
                                        const TargetRegisterClass *RC,
                                        const TargetRegisterInfo *TRI,
                                        Register VReg) const {
  MachineFunction &MF = *MBB.getParent();
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo::getFixedStack(MF, FrameIndex),
      MachineMemOperand::MOLoad, MFI.getObjectSize(FrameIndex),
      MFI.getObjectAlign(FrameIndex));

  unsigned Opcode = 0;
  if (TRI->isTypeLegalForClass(*RC, MVT::i8)) {
    Opcode = AVR::LDDRdPtrQ;
  } else if (TRI->isTypeLegalForClass(*RC, MVT::i16)) {
    // Opcode = AVR::LDDWRdPtrQ;
    //: FIXME: remove this once PR13375 gets fixed
    Opcode = AVR::LDDWRdYQ;
  } else {
    llvm_unreachable("Cannot load this register from a stack slot!");
  }

  BuildMI(MBB, MI, DebugLoc(), get(Opcode), DestReg)
      .addFrameIndex(FrameIndex)
      .addImm(0)
      .addMemOperand(MMO);
}

const MCInstrDesc &AVRInstrInfo::getBrCond(AVRCC::CondCodes CC) const {
  switch (CC) {
  default:
    llvm_unreachable("Unknown condition code!");
  case AVRCC::COND_EQ:
    return get(AVR::BREQk);
  case AVRCC::COND_NE:
    return get(AVR::BRNEk);
  case AVRCC::COND_GE:
    return get(AVR::BRGEk);
  case AVRCC::COND_LT:
    return get(AVR::BRLTk);
  case AVRCC::COND_SH:
    return get(AVR::BRSHk);
  case AVRCC::COND_LO:
    return get(AVR::BRLOk);
  case AVRCC::COND_MI:
    return get(AVR::BRMIk);
  case AVRCC::COND_PL:
    return get(AVR::BRPLk);
  }
}

AVRCC::CondCodes AVRInstrInfo::getCondFromBranchOpc(unsigned Opc) const {
  switch (Opc) {
  default:
    return AVRCC::COND_INVALID;
  case AVR::BREQk:
    return AVRCC::COND_EQ;
  case AVR::BRNEk:
    return AVRCC::COND_NE;
  case AVR::BRSHk:
    return AVRCC::COND_SH;
  case AVR::BRLOk:
    return AVRCC::COND_LO;
  case AVR::BRMIk:
    return AVRCC::COND_MI;
  case AVR::BRPLk:
    return AVRCC::COND_PL;
  case AVR::BRGEk:
    return AVRCC::COND_GE;
  case AVR::BRLTk:
    return AVRCC::COND_LT;
  }
}

AVRCC::CondCodes AVRInstrInfo::getOppositeCondition(AVRCC::CondCodes CC) const {
  switch (CC) {
  default:
    llvm_unreachable("Invalid condition!");
  case AVRCC::COND_EQ:
    return AVRCC::COND_NE;
  case AVRCC::COND_NE:
    return AVRCC::COND_EQ;
  case AVRCC::COND_SH:
    return AVRCC::COND_LO;
  case AVRCC::COND_LO:
    return AVRCC::COND_SH;
  case AVRCC::COND_GE:
    return AVRCC::COND_LT;
  case AVRCC::COND_LT:
    return AVRCC::COND_GE;
  case AVRCC::COND_MI:
    return AVRCC::COND_PL;
  case AVRCC::COND_PL:
    return AVRCC::COND_MI;
  }
}

bool AVRInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                 MachineBasicBlock *&TBB,
                                 MachineBasicBlock *&FBB,
                                 SmallVectorImpl<MachineOperand> &Cond,
                                 bool AllowModify) const {
  // Start from the bottom of the block and work up, examining the
  // terminator instructions.
  MachineBasicBlock::iterator I = MBB.end();
  MachineBasicBlock::iterator UnCondBrIter = MBB.end();

  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr()) {
      continue;
    }

    // Working from the bottom, when we see a non-terminator
    // instruction, we're done.
    if (!isUnpredicatedTerminator(*I)) {
      break;
    }

    // A terminator that isn't a branch can't easily be handled
    // by this analysis.
    if (!I->getDesc().isBranch()) {
      return true;
    }

    // Handle unconditional branches.
    //: TODO: add here jmp
    if (I->getOpcode() == AVR::RJMPk) {
      UnCondBrIter = I;

      if (!AllowModify) {
        TBB = I->getOperand(0).getMBB();
        continue;
      }

      // If the block has any instructions after a JMP, delete them.
      MBB.erase(std::next(I), MBB.end());

      Cond.clear();
      FBB = nullptr;

      // Delete the JMP if it's equivalent to a fall-through.
      if (MBB.isLayoutSuccessor(I->getOperand(0).getMBB())) {
        TBB = nullptr;
        I->eraseFromParent();
        I = MBB.end();
        UnCondBrIter = MBB.end();
        continue;
      }

      // TBB is used to indicate the unconditinal destination.
      TBB = I->getOperand(0).getMBB();
      continue;
    }

    // Handle conditional branches.
    AVRCC::CondCodes BranchCode = getCondFromBranchOpc(I->getOpcode());
    if (BranchCode == AVRCC::COND_INVALID) {
      return true; // Can't handle indirect branch.
    }

    // Working from the bottom, handle the first conditional branch.
    if (Cond.empty()) {
      MachineBasicBlock *TargetBB = I->getOperand(0).getMBB();
      if (AllowModify && UnCondBrIter != MBB.end() &&
          MBB.isLayoutSuccessor(TargetBB)) {
        // If we can modify the code and it ends in something like:
        //
        //     jCC L1
        //     jmp L2
        //   L1:
        //     ...
        //   L2:
        //
        // Then we can change this to:
        //
        //     jnCC L2
        //   L1:
        //     ...
        //   L2:
        //
        // Which is a bit more efficient.
        // We conditionally jump to the fall-through block.
        BranchCode = getOppositeCondition(BranchCode);
        unsigned JNCC = getBrCond(BranchCode).getOpcode();
        MachineBasicBlock::iterator OldInst = I;

        BuildMI(MBB, UnCondBrIter, MBB.findDebugLoc(I), get(JNCC))
            .addMBB(UnCondBrIter->getOperand(0).getMBB());
        BuildMI(MBB, UnCondBrIter, MBB.findDebugLoc(I), get(AVR::RJMPk))
            .addMBB(TargetBB);

        OldInst->eraseFromParent();
        UnCondBrIter->eraseFromParent();

        // Restart the analysis.
        UnCondBrIter = MBB.end();
        I = MBB.end();
        continue;
      }

      FBB = TBB;
      TBB = I->getOperand(0).getMBB();
      Cond.push_back(MachineOperand::CreateImm(BranchCode));
      continue;
    }

    // Handle subsequent conditional branches. Only handle the case where all
    // conditional branches branch to the same destination.
    assert(Cond.size() == 1);
    assert(TBB);

    // Only handle the case where all conditional branches branch to
    // the same destination.
    if (TBB != I->getOperand(0).getMBB()) {
      return true;
    }

    AVRCC::CondCodes OldBranchCode = (AVRCC::CondCodes)Cond[0].getImm();
    // If the conditions are the same, we can leave them alone.
    if (OldBranchCode == BranchCode) {
      continue;
    }

    return true;
  }

  return false;
}

unsigned AVRInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                    MachineBasicBlock *TBB,
                                    MachineBasicBlock *FBB,
                                    ArrayRef<MachineOperand> Cond,
                                    const DebugLoc &DL, int *BytesAdded) const {
  if (BytesAdded)
    *BytesAdded = 0;

  // Shouldn't be a fall through.
  assert(TBB && "insertBranch must not be told to insert a fallthrough");
  assert((Cond.size() == 1 || Cond.size() == 0) &&
         "AVR branch conditions have one component!");

  if (Cond.empty()) {
    assert(!FBB && "Unconditional branch with multiple successors!");
    auto &MI = *BuildMI(&MBB, DL, get(AVR::RJMPk)).addMBB(TBB);
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(MI);
    return 1;
  }

  // Conditional branch.
  unsigned Count = 0;
  AVRCC::CondCodes CC = (AVRCC::CondCodes)Cond[0].getImm();
  auto &CondMI = *BuildMI(&MBB, DL, getBrCond(CC)).addMBB(TBB);

  if (BytesAdded)
    *BytesAdded += getInstSizeInBytes(CondMI);
  ++Count;

  if (FBB) {
    // Two-way Conditional branch. Insert the second branch.
    auto &MI = *BuildMI(&MBB, DL, get(AVR::RJMPk)).addMBB(FBB);
    if (BytesAdded)
      *BytesAdded += getInstSizeInBytes(MI);
    ++Count;
  }

  return Count;
}

unsigned AVRInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                    int *BytesRemoved) const {
  if (BytesRemoved)
    *BytesRemoved = 0;

  MachineBasicBlock::iterator I = MBB.end();
  unsigned Count = 0;

  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr()) {
      continue;
    }
    //: TODO: add here the missing jmp instructions once they are implemented
    // like jmp, {e}ijmp, and other cond branches, ...
    if (I->getOpcode() != AVR::RJMPk &&
        getCondFromBranchOpc(I->getOpcode()) == AVRCC::COND_INVALID) {
      break;
    }

    // Remove the branch.
    if (BytesRemoved)
      *BytesRemoved += getInstSizeInBytes(*I);
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }

  return Count;
}

bool AVRInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 1 && "Invalid AVR branch condition!");

  AVRCC::CondCodes CC = static_cast<AVRCC::CondCodes>(Cond[0].getImm());
  Cond[0].setImm(getOppositeCondition(CC));

  return false;
}

unsigned AVRInstrInfo::getInstSizeInBytes(const MachineInstr &MI) const {
  unsigned Opcode = MI.getOpcode();

  switch (Opcode) {
  // A regular instruction
  default: {
    const MCInstrDesc &Desc = get(Opcode);
    return Desc.getSize();
  }
  case TargetOpcode::EH_LABEL:
  case TargetOpcode::IMPLICIT_DEF:
  case TargetOpcode::KILL:
  case TargetOpcode::DBG_VALUE:
    return 0;
  case TargetOpcode::INLINEASM:
  case TargetOpcode::INLINEASM_BR: {
    const MachineFunction &MF = *MI.getParent()->getParent();
    const AVRTargetMachine &TM =
        static_cast<const AVRTargetMachine &>(MF.getTarget());
    const TargetInstrInfo &TII = *STI.getInstrInfo();
    return TII.getInlineAsmLength(MI.getOperand(0).getSymbolName(),
                                  *TM.getMCAsmInfo());
  }
  }
}

MachineBasicBlock *
AVRInstrInfo::getBranchDestBlock(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  default:
    llvm_unreachable("unexpected opcode!");
  case AVR::JMPk:
  case AVR::CALLk:
  case AVR::RCALLk:
  case AVR::RJMPk:
  case AVR::BREQk:
  case AVR::BRNEk:
  case AVR::BRSHk:
  case AVR::BRLOk:
  case AVR::BRMIk:
  case AVR::BRPLk:
  case AVR::BRGEk:
  case AVR::BRLTk:
    return MI.getOperand(0).getMBB();
  case AVR::BRBSsk:
  case AVR::BRBCsk:
    return MI.getOperand(1).getMBB();
  case AVR::SBRCRrB:
  case AVR::SBRSRrB:
  case AVR::SBICAb:
  case AVR::SBISAb:
    llvm_unreachable("unimplemented branch instructions");
  }
}

bool AVRInstrInfo::isBranchOffsetInRange(unsigned BranchOp,
                                         int64_t BrOffset) const {

  switch (BranchOp) {
  default:
    llvm_unreachable("unexpected opcode!");
  case AVR::JMPk:
  case AVR::CALLk:
    return STI.hasJMPCALL();
  case AVR::RCALLk:
  case AVR::RJMPk:
    return isIntN(13, BrOffset);
  case AVR::BRBSsk:
  case AVR::BRBCsk:
  case AVR::BREQk:
  case AVR::BRNEk:
  case AVR::BRSHk:
  case AVR::BRLOk:
  case AVR::BRMIk:
  case AVR::BRPLk:
  case AVR::BRGEk:
  case AVR::BRLTk:
    return isIntN(7, BrOffset);
  }
}

void AVRInstrInfo::insertIndirectBranch(MachineBasicBlock &MBB,
                                        MachineBasicBlock &NewDestBB,
                                        MachineBasicBlock &RestoreBB,
                                        const DebugLoc &DL, int64_t BrOffset,
                                        RegScavenger *RS) const {
  // This method inserts a *direct* branch (JMP), despite its name.
  // LLVM calls this method to fixup unconditional branches; it never calls
  // insertBranch or some hypothetical "insertDirectBranch".
  // See lib/CodeGen/RegisterRelaxation.cpp for details.
  // We end up here when a jump is too long for a RJMP instruction.
  if (STI.hasJMPCALL())
    BuildMI(&MBB, DL, get(AVR::JMPk)).addMBB(&NewDestBB);
  else
    // The RJMP may jump to a far place beyond its legal range. We let the
    // linker to report 'out of range' rather than crash, or silently emit
    // incorrect assembly code.
    BuildMI(&MBB, DL, get(AVR::RJMPk)).addMBB(&NewDestBB);
}

} // end of namespace llvm
