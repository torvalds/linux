//===-- RISCVMoveMerger.cpp - RISC-V move merge pass ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a pass that performs move related peephole optimizations
// as Zcmp has specified. This pass should be run after register allocation.
//
//===----------------------------------------------------------------------===//

#include "RISCVInstrInfo.h"
#include "RISCVMachineFunctionInfo.h"

using namespace llvm;

#define RISCV_MOVE_MERGE_NAME "RISC-V Zcmp move merging pass"

namespace {
struct RISCVMoveMerge : public MachineFunctionPass {
  static char ID;

  RISCVMoveMerge() : MachineFunctionPass(ID) {}

  const RISCVInstrInfo *TII;
  const TargetRegisterInfo *TRI;

  // Track which register units have been modified and used.
  LiveRegUnits ModifiedRegUnits, UsedRegUnits;

  bool isCandidateToMergeMVA01S(const DestSourcePair &RegPair);
  bool isCandidateToMergeMVSA01(const DestSourcePair &RegPair);
  // Merge the two instructions indicated into a single pair instruction.
  MachineBasicBlock::iterator
  mergePairedInsns(MachineBasicBlock::iterator I,
                   MachineBasicBlock::iterator Paired, unsigned Opcode);

  // Look for C.MV instruction that can be combined with
  // the given instruction into CM.MVA01S or CM.MVSA01. Return the matching
  // instruction if one exists.
  MachineBasicBlock::iterator
  findMatchingInst(MachineBasicBlock::iterator &MBBI, unsigned InstOpcode,
                   const DestSourcePair &RegPair);
  bool mergeMoveSARegPair(MachineBasicBlock &MBB);
  bool runOnMachineFunction(MachineFunction &Fn) override;

  StringRef getPassName() const override { return RISCV_MOVE_MERGE_NAME; }
};

char RISCVMoveMerge::ID = 0;

} // end of anonymous namespace

INITIALIZE_PASS(RISCVMoveMerge, "riscv-move-merge", RISCV_MOVE_MERGE_NAME,
                false, false)

// Check if registers meet CM.MVA01S constraints.
bool RISCVMoveMerge::isCandidateToMergeMVA01S(const DestSourcePair &RegPair) {
  Register Destination = RegPair.Destination->getReg();
  Register Source = RegPair.Source->getReg();
  // If destination is not a0 or a1.
  if ((Destination == RISCV::X10 || Destination == RISCV::X11) &&
      RISCV::SR07RegClass.contains(Source))
    return true;
  return false;
}

// Check if registers meet CM.MVSA01 constraints.
bool RISCVMoveMerge::isCandidateToMergeMVSA01(const DestSourcePair &RegPair) {
  Register Destination = RegPair.Destination->getReg();
  Register Source = RegPair.Source->getReg();
  // If Source is s0 - s7.
  if ((Source == RISCV::X10 || Source == RISCV::X11) &&
      RISCV::SR07RegClass.contains(Destination))
    return true;
  return false;
}

MachineBasicBlock::iterator
RISCVMoveMerge::mergePairedInsns(MachineBasicBlock::iterator I,
                                 MachineBasicBlock::iterator Paired,
                                 unsigned Opcode) {
  const MachineOperand *Sreg1, *Sreg2;
  MachineBasicBlock::iterator E = I->getParent()->end();
  MachineBasicBlock::iterator NextI = next_nodbg(I, E);
  DestSourcePair FirstPair = TII->isCopyInstrImpl(*I).value();
  DestSourcePair PairedRegs = TII->isCopyInstrImpl(*Paired).value();
  Register ARegInFirstPair = Opcode == RISCV::CM_MVA01S
                                 ? FirstPair.Destination->getReg()
                                 : FirstPair.Source->getReg();

  if (NextI == Paired)
    NextI = next_nodbg(NextI, E);
  DebugLoc DL = I->getDebugLoc();

  // The order of S-reg depends on which instruction holds A0, instead of
  // the order of register pair.
  // e,g.
  //   mv a1, s1
  //   mv a0, s2    =>  cm.mva01s s2,s1
  //
  //   mv a0, s2
  //   mv a1, s1    =>  cm.mva01s s2,s1
  bool StartWithX10 = ARegInFirstPair == RISCV::X10;
  if (Opcode == RISCV::CM_MVA01S) {
    Sreg1 = StartWithX10 ? FirstPair.Source : PairedRegs.Source;
    Sreg2 = StartWithX10 ? PairedRegs.Source : FirstPair.Source;
  } else {
    Sreg1 = StartWithX10 ? FirstPair.Destination : PairedRegs.Destination;
    Sreg2 = StartWithX10 ? PairedRegs.Destination : FirstPair.Destination;
  }

  BuildMI(*I->getParent(), I, DL, TII->get(Opcode)).add(*Sreg1).add(*Sreg2);

  I->eraseFromParent();
  Paired->eraseFromParent();
  return NextI;
}

MachineBasicBlock::iterator
RISCVMoveMerge::findMatchingInst(MachineBasicBlock::iterator &MBBI,
                                 unsigned InstOpcode,
                                 const DestSourcePair &RegPair) {
  MachineBasicBlock::iterator E = MBBI->getParent()->end();

  // Track which register units have been modified and used between the first
  // insn and the second insn.
  ModifiedRegUnits.clear();
  UsedRegUnits.clear();

  for (MachineBasicBlock::iterator I = next_nodbg(MBBI, E); I != E;
       I = next_nodbg(I, E)) {

    MachineInstr &MI = *I;

    if (auto SecondPair = TII->isCopyInstrImpl(MI)) {
      Register SourceReg = SecondPair->Source->getReg();
      Register DestReg = SecondPair->Destination->getReg();

      if (InstOpcode == RISCV::CM_MVA01S &&
          isCandidateToMergeMVA01S(*SecondPair)) {
        // If register pair is valid and destination registers are different.
        if ((RegPair.Destination->getReg() == DestReg))
          return E;

        //  If paired destination register was modified or used, the source reg
        //  was modified, there is no possibility of finding matching
        //  instruction so exit early.
        if (!ModifiedRegUnits.available(DestReg) ||
            !UsedRegUnits.available(DestReg) ||
            !ModifiedRegUnits.available(SourceReg))
          return E;

        return I;
      } else if (InstOpcode == RISCV::CM_MVSA01 &&
                 isCandidateToMergeMVSA01(*SecondPair)) {
        if ((RegPair.Source->getReg() == SourceReg) ||
            (RegPair.Destination->getReg() == DestReg))
          return E;

        if (!ModifiedRegUnits.available(DestReg) ||
            !UsedRegUnits.available(DestReg) ||
            !ModifiedRegUnits.available(SourceReg))
          return E;

        return I;
      }
    }
    // Update modified / used register units.
    LiveRegUnits::accumulateUsedDefed(MI, ModifiedRegUnits, UsedRegUnits, TRI);
  }
  return E;
}

// Finds instructions, which could be represented as C.MV instructions and
// merged into CM.MVA01S or CM.MVSA01.
bool RISCVMoveMerge::mergeMoveSARegPair(MachineBasicBlock &MBB) {
  bool Modified = false;

  for (MachineBasicBlock::iterator MBBI = MBB.begin(), E = MBB.end();
       MBBI != E;) {
    // Check if the instruction can be compressed to C.MV instruction. If it
    // can, return Dest/Src register pair.
    auto RegPair = TII->isCopyInstrImpl(*MBBI);
    if (RegPair.has_value()) {
      unsigned Opcode = 0;

      if (isCandidateToMergeMVA01S(*RegPair))
        Opcode = RISCV::CM_MVA01S;
      else if (isCandidateToMergeMVSA01(*RegPair))
        Opcode = RISCV::CM_MVSA01;
      else {
        ++MBBI;
        continue;
      }

      MachineBasicBlock::iterator Paired =
          findMatchingInst(MBBI, Opcode, RegPair.value());
      // If matching instruction can be found merge them.
      if (Paired != E) {
        MBBI = mergePairedInsns(MBBI, Paired, Opcode);
        Modified = true;
        continue;
      }
    }
    ++MBBI;
  }
  return Modified;
}

bool RISCVMoveMerge::runOnMachineFunction(MachineFunction &Fn) {
  if (skipFunction(Fn.getFunction()))
    return false;

  const RISCVSubtarget *Subtarget = &Fn.getSubtarget<RISCVSubtarget>();
  if (!Subtarget->hasStdExtZcmp())
    return false;

  TII = Subtarget->getInstrInfo();
  TRI = Subtarget->getRegisterInfo();
  // Resize the modified and used register unit trackers.  We do this once
  // per function and then clear the register units each time we optimize a
  // move.
  ModifiedRegUnits.init(*TRI);
  UsedRegUnits.init(*TRI);
  bool Modified = false;
  for (auto &MBB : Fn)
    Modified |= mergeMoveSARegPair(MBB);
  return Modified;
}

/// createRISCVMoveMergePass - returns an instance of the
/// move merge pass.
FunctionPass *llvm::createRISCVMoveMergePass() { return new RISCVMoveMerge(); }
