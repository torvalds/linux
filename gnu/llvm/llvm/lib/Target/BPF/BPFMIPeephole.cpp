//===-------------- BPFMIPeephole.cpp - MI Peephole Cleanups  -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass performs peephole optimizations to cleanup ugly code sequences at
// MachineInstruction layer.
//
// Currently, there are two optimizations implemented:
//  - One pre-RA MachineSSA pass to eliminate type promotion sequences, those
//    zero extend 32-bit subregisters to 64-bit registers, if the compiler
//    could prove the subregisters is defined by 32-bit operations in which
//    case the upper half of the underlying 64-bit registers were zeroed
//    implicitly.
//
//  - One post-RA PreEmit pass to do final cleanup on some redundant
//    instructions generated due to bad RA on subregister.
//===----------------------------------------------------------------------===//

#include "BPF.h"
#include "BPFInstrInfo.h"
#include "BPFTargetMachine.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Support/Debug.h"
#include <set>

using namespace llvm;

#define DEBUG_TYPE "bpf-mi-zext-elim"

static cl::opt<int> GotolAbsLowBound("gotol-abs-low-bound", cl::Hidden,
  cl::init(INT16_MAX >> 1), cl::desc("Specify gotol lower bound"));

STATISTIC(ZExtElemNum, "Number of zero extension shifts eliminated");

namespace {

struct BPFMIPeephole : public MachineFunctionPass {

  static char ID;
  const BPFInstrInfo *TII;
  MachineFunction *MF;
  MachineRegisterInfo *MRI;

  BPFMIPeephole() : MachineFunctionPass(ID) {
    initializeBPFMIPeepholePass(*PassRegistry::getPassRegistry());
  }

private:
  // Initialize class variables.
  void initialize(MachineFunction &MFParm);

  bool isCopyFrom32Def(MachineInstr *CopyMI);
  bool isInsnFrom32Def(MachineInstr *DefInsn);
  bool isPhiFrom32Def(MachineInstr *MovMI);
  bool isMovFrom32Def(MachineInstr *MovMI);
  bool eliminateZExtSeq();
  bool eliminateZExt();

  std::set<MachineInstr *> PhiInsns;

public:

  // Main entry point for this pass.
  bool runOnMachineFunction(MachineFunction &MF) override {
    if (skipFunction(MF.getFunction()))
      return false;

    initialize(MF);

    // First try to eliminate (zext, lshift, rshift) and then
    // try to eliminate zext.
    bool ZExtSeqExist, ZExtExist;
    ZExtSeqExist = eliminateZExtSeq();
    ZExtExist = eliminateZExt();
    return ZExtSeqExist || ZExtExist;
  }
};

// Initialize class variables.
void BPFMIPeephole::initialize(MachineFunction &MFParm) {
  MF = &MFParm;
  MRI = &MF->getRegInfo();
  TII = MF->getSubtarget<BPFSubtarget>().getInstrInfo();
  LLVM_DEBUG(dbgs() << "*** BPF MachineSSA ZEXT Elim peephole pass ***\n\n");
}

bool BPFMIPeephole::isCopyFrom32Def(MachineInstr *CopyMI)
{
  MachineOperand &opnd = CopyMI->getOperand(1);

  if (!opnd.isReg())
    return false;

  // Return false if getting value from a 32bit physical register.
  // Most likely, this physical register is aliased to
  // function call return value or current function parameters.
  Register Reg = opnd.getReg();
  if (!Reg.isVirtual())
    return false;

  if (MRI->getRegClass(Reg) == &BPF::GPRRegClass)
    return false;

  MachineInstr *DefInsn = MRI->getVRegDef(Reg);
  if (!isInsnFrom32Def(DefInsn))
    return false;

  return true;
}

bool BPFMIPeephole::isPhiFrom32Def(MachineInstr *PhiMI)
{
  for (unsigned i = 1, e = PhiMI->getNumOperands(); i < e; i += 2) {
    MachineOperand &opnd = PhiMI->getOperand(i);

    if (!opnd.isReg())
      return false;

    MachineInstr *PhiDef = MRI->getVRegDef(opnd.getReg());
    if (!PhiDef)
      return false;
    if (PhiDef->isPHI()) {
      if (!PhiInsns.insert(PhiDef).second)
        return false;
      if (!isPhiFrom32Def(PhiDef))
        return false;
    }
    if (PhiDef->getOpcode() == BPF::COPY && !isCopyFrom32Def(PhiDef))
      return false;
  }

  return true;
}

// The \p DefInsn instruction defines a virtual register.
bool BPFMIPeephole::isInsnFrom32Def(MachineInstr *DefInsn)
{
  if (!DefInsn)
    return false;

  if (DefInsn->isPHI()) {
    if (!PhiInsns.insert(DefInsn).second)
      return false;
    if (!isPhiFrom32Def(DefInsn))
      return false;
  } else if (DefInsn->getOpcode() == BPF::COPY) {
    if (!isCopyFrom32Def(DefInsn))
      return false;
  }

  return true;
}

bool BPFMIPeephole::isMovFrom32Def(MachineInstr *MovMI)
{
  MachineInstr *DefInsn = MRI->getVRegDef(MovMI->getOperand(1).getReg());

  LLVM_DEBUG(dbgs() << "  Def of Mov Src:");
  LLVM_DEBUG(DefInsn->dump());

  PhiInsns.clear();
  if (!isInsnFrom32Def(DefInsn))
    return false;

  LLVM_DEBUG(dbgs() << "  One ZExt elim sequence identified.\n");

  return true;
}

bool BPFMIPeephole::eliminateZExtSeq() {
  MachineInstr* ToErase = nullptr;
  bool Eliminated = false;

  for (MachineBasicBlock &MBB : *MF) {
    for (MachineInstr &MI : MBB) {
      // If the previous instruction was marked for elimination, remove it now.
      if (ToErase) {
        ToErase->eraseFromParent();
        ToErase = nullptr;
      }

      // Eliminate the 32-bit to 64-bit zero extension sequence when possible.
      //
      //   MOV_32_64 rB, wA
      //   SLL_ri    rB, rB, 32
      //   SRL_ri    rB, rB, 32
      if (MI.getOpcode() == BPF::SRL_ri &&
          MI.getOperand(2).getImm() == 32) {
        Register DstReg = MI.getOperand(0).getReg();
        Register ShfReg = MI.getOperand(1).getReg();
        MachineInstr *SllMI = MRI->getVRegDef(ShfReg);

        LLVM_DEBUG(dbgs() << "Starting SRL found:");
        LLVM_DEBUG(MI.dump());

        if (!SllMI ||
            SllMI->isPHI() ||
            SllMI->getOpcode() != BPF::SLL_ri ||
            SllMI->getOperand(2).getImm() != 32)
          continue;

        LLVM_DEBUG(dbgs() << "  SLL found:");
        LLVM_DEBUG(SllMI->dump());

        MachineInstr *MovMI = MRI->getVRegDef(SllMI->getOperand(1).getReg());
        if (!MovMI ||
            MovMI->isPHI() ||
            MovMI->getOpcode() != BPF::MOV_32_64)
          continue;

        LLVM_DEBUG(dbgs() << "  Type cast Mov found:");
        LLVM_DEBUG(MovMI->dump());

        Register SubReg = MovMI->getOperand(1).getReg();
        if (!isMovFrom32Def(MovMI)) {
          LLVM_DEBUG(dbgs()
                     << "  One ZExt elim sequence failed qualifying elim.\n");
          continue;
        }

        BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(BPF::SUBREG_TO_REG), DstReg)
          .addImm(0).addReg(SubReg).addImm(BPF::sub_32);

        SllMI->eraseFromParent();
        MovMI->eraseFromParent();
        // MI is the right shift, we can't erase it in it's own iteration.
        // Mark it to ToErase, and erase in the next iteration.
        ToErase = &MI;
        ZExtElemNum++;
        Eliminated = true;
      }
    }
  }

  return Eliminated;
}

bool BPFMIPeephole::eliminateZExt() {
  MachineInstr* ToErase = nullptr;
  bool Eliminated = false;

  for (MachineBasicBlock &MBB : *MF) {
    for (MachineInstr &MI : MBB) {
      // If the previous instruction was marked for elimination, remove it now.
      if (ToErase) {
        ToErase->eraseFromParent();
        ToErase = nullptr;
      }

      if (MI.getOpcode() != BPF::MOV_32_64)
        continue;

      // Eliminate MOV_32_64 if possible.
      //   MOV_32_64 rA, wB
      //
      // If wB has been zero extended, replace it with a SUBREG_TO_REG.
      // This is to workaround BPF programs where pkt->{data, data_end}
      // is encoded as u32, but actually the verifier populates them
      // as 64bit pointer. The MOV_32_64 will zero out the top 32 bits.
      LLVM_DEBUG(dbgs() << "Candidate MOV_32_64 instruction:");
      LLVM_DEBUG(MI.dump());

      if (!isMovFrom32Def(&MI))
        continue;

      LLVM_DEBUG(dbgs() << "Removing the MOV_32_64 instruction\n");

      Register dst = MI.getOperand(0).getReg();
      Register src = MI.getOperand(1).getReg();

      // Build a SUBREG_TO_REG instruction.
      BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(BPF::SUBREG_TO_REG), dst)
        .addImm(0).addReg(src).addImm(BPF::sub_32);

      ToErase = &MI;
      Eliminated = true;
    }
  }

  return Eliminated;
}

} // end default namespace

INITIALIZE_PASS(BPFMIPeephole, DEBUG_TYPE,
                "BPF MachineSSA Peephole Optimization For ZEXT Eliminate",
                false, false)

char BPFMIPeephole::ID = 0;
FunctionPass* llvm::createBPFMIPeepholePass() { return new BPFMIPeephole(); }

STATISTIC(RedundantMovElemNum, "Number of redundant moves eliminated");

namespace {

struct BPFMIPreEmitPeephole : public MachineFunctionPass {

  static char ID;
  MachineFunction *MF;
  const TargetRegisterInfo *TRI;
  const BPFInstrInfo *TII;
  bool SupportGotol;

  BPFMIPreEmitPeephole() : MachineFunctionPass(ID) {
    initializeBPFMIPreEmitPeepholePass(*PassRegistry::getPassRegistry());
  }

private:
  // Initialize class variables.
  void initialize(MachineFunction &MFParm);

  bool in16BitRange(int Num);
  bool eliminateRedundantMov();
  bool adjustBranch();

public:

  // Main entry point for this pass.
  bool runOnMachineFunction(MachineFunction &MF) override {
    if (skipFunction(MF.getFunction()))
      return false;

    initialize(MF);

    bool Changed;
    Changed = eliminateRedundantMov();
    if (SupportGotol)
      Changed = adjustBranch() || Changed;
    return Changed;
  }
};

// Initialize class variables.
void BPFMIPreEmitPeephole::initialize(MachineFunction &MFParm) {
  MF = &MFParm;
  TII = MF->getSubtarget<BPFSubtarget>().getInstrInfo();
  TRI = MF->getSubtarget<BPFSubtarget>().getRegisterInfo();
  SupportGotol = MF->getSubtarget<BPFSubtarget>().hasGotol();
  LLVM_DEBUG(dbgs() << "*** BPF PreEmit peephole pass ***\n\n");
}

bool BPFMIPreEmitPeephole::eliminateRedundantMov() {
  MachineInstr* ToErase = nullptr;
  bool Eliminated = false;

  for (MachineBasicBlock &MBB : *MF) {
    for (MachineInstr &MI : MBB) {
      // If the previous instruction was marked for elimination, remove it now.
      if (ToErase) {
        LLVM_DEBUG(dbgs() << "  Redundant Mov Eliminated:");
        LLVM_DEBUG(ToErase->dump());
        ToErase->eraseFromParent();
        ToErase = nullptr;
      }

      // Eliminate identical move:
      //
      //   MOV rA, rA
      //
      // Note that we cannot remove
      //   MOV_32_64  rA, wA
      //   MOV_rr_32  wA, wA
      // as these two instructions having side effects, zeroing out
      // top 32 bits of rA.
      unsigned Opcode = MI.getOpcode();
      if (Opcode == BPF::MOV_rr) {
        Register dst = MI.getOperand(0).getReg();
        Register src = MI.getOperand(1).getReg();

        if (dst != src)
          continue;

        ToErase = &MI;
        RedundantMovElemNum++;
        Eliminated = true;
      }
    }
  }

  return Eliminated;
}

bool BPFMIPreEmitPeephole::in16BitRange(int Num) {
  // Well, the cut-off is not precisely at 16bit range since
  // new codes are added during the transformation. So let us
  // a little bit conservative.
  return Num >= -GotolAbsLowBound && Num <= GotolAbsLowBound;
}

// Before cpu=v4, only 16bit branch target offset (-0x8000 to 0x7fff)
// is supported for both unconditional (JMP) and condition (JEQ, JSGT,
// etc.) branches. In certain cases, e.g., full unrolling, the branch
// target offset might exceed 16bit range. If this happens, the llvm
// will generate incorrect code as the offset is truncated to 16bit.
//
// To fix this rare case, a new insn JMPL is introduced. This new
// insn supports supports 32bit branch target offset. The compiler
// does not use this insn during insn selection. Rather, BPF backend
// will estimate the branch target offset and do JMP -> JMPL and
// JEQ -> JEQ + JMPL conversion if the estimated branch target offset
// is beyond 16bit.
bool BPFMIPreEmitPeephole::adjustBranch() {
  bool Changed = false;
  int CurrNumInsns = 0;
  DenseMap<MachineBasicBlock *, int> SoFarNumInsns;
  DenseMap<MachineBasicBlock *, MachineBasicBlock *> FollowThroughBB;
  std::vector<MachineBasicBlock *> MBBs;

  MachineBasicBlock *PrevBB = nullptr;
  for (MachineBasicBlock &MBB : *MF) {
    // MBB.size() is the number of insns in this basic block, including some
    // debug info, e.g., DEBUG_VALUE, so we may over-count a little bit.
    // Typically we have way more normal insns than DEBUG_VALUE insns.
    // Also, if we indeed need to convert conditional branch like JEQ to
    // JEQ + JMPL, we actually introduced some new insns like below.
    CurrNumInsns += (int)MBB.size();
    SoFarNumInsns[&MBB] = CurrNumInsns;
    if (PrevBB != nullptr)
      FollowThroughBB[PrevBB] = &MBB;
    PrevBB = &MBB;
    // A list of original BBs to make later traveral easier.
    MBBs.push_back(&MBB);
  }
  FollowThroughBB[PrevBB] = nullptr;

  for (unsigned i = 0; i < MBBs.size(); i++) {
    // We have four cases here:
    //  (1). no terminator, simple follow through.
    //  (2). jmp to another bb.
    //  (3). conditional jmp to another bb or follow through.
    //  (4). conditional jmp followed by an unconditional jmp.
    MachineInstr *CondJmp = nullptr, *UncondJmp = nullptr;

    MachineBasicBlock *MBB = MBBs[i];
    for (MachineInstr &Term : MBB->terminators()) {
      if (Term.isConditionalBranch()) {
        assert(CondJmp == nullptr);
        CondJmp = &Term;
      } else if (Term.isUnconditionalBranch()) {
        assert(UncondJmp == nullptr);
        UncondJmp = &Term;
      }
    }

    // (1). no terminator, simple follow through.
    if (!CondJmp && !UncondJmp)
      continue;

    MachineBasicBlock *CondTargetBB, *JmpBB;
    CurrNumInsns = SoFarNumInsns[MBB];

    // (2). jmp to another bb.
    if (!CondJmp && UncondJmp) {
      JmpBB = UncondJmp->getOperand(0).getMBB();
      if (in16BitRange(SoFarNumInsns[JmpBB] - JmpBB->size() - CurrNumInsns))
        continue;

      // replace this insn as a JMPL.
      BuildMI(MBB, UncondJmp->getDebugLoc(), TII->get(BPF::JMPL)).addMBB(JmpBB);
      UncondJmp->eraseFromParent();
      Changed = true;
      continue;
    }

    const BasicBlock *TermBB = MBB->getBasicBlock();
    int Dist;

    // (3). conditional jmp to another bb or follow through.
    if (!UncondJmp) {
      CondTargetBB = CondJmp->getOperand(2).getMBB();
      MachineBasicBlock *FollowBB = FollowThroughBB[MBB];
      Dist = SoFarNumInsns[CondTargetBB] - CondTargetBB->size() - CurrNumInsns;
      if (in16BitRange(Dist))
        continue;

      // We have
      //   B2: ...
      //       if (cond) goto B5
      //   B3: ...
      // where B2 -> B5 is beyond 16bit range.
      //
      // We do not have 32bit cond jmp insn. So we try to do
      // the following.
      //   B2:     ...
      //           if (cond) goto New_B1
      //   New_B0  goto B3
      //   New_B1: gotol B5
      //   B3: ...
      // Basically two new basic blocks are created.
      MachineBasicBlock *New_B0 = MF->CreateMachineBasicBlock(TermBB);
      MachineBasicBlock *New_B1 = MF->CreateMachineBasicBlock(TermBB);

      // Insert New_B0 and New_B1 into function block list.
      MachineFunction::iterator MBB_I  = ++MBB->getIterator();
      MF->insert(MBB_I, New_B0);
      MF->insert(MBB_I, New_B1);

      // replace B2 cond jump
      if (CondJmp->getOperand(1).isReg())
        BuildMI(*MBB, MachineBasicBlock::iterator(*CondJmp), CondJmp->getDebugLoc(), TII->get(CondJmp->getOpcode()))
            .addReg(CondJmp->getOperand(0).getReg())
            .addReg(CondJmp->getOperand(1).getReg())
            .addMBB(New_B1);
      else
        BuildMI(*MBB, MachineBasicBlock::iterator(*CondJmp), CondJmp->getDebugLoc(), TII->get(CondJmp->getOpcode()))
            .addReg(CondJmp->getOperand(0).getReg())
            .addImm(CondJmp->getOperand(1).getImm())
            .addMBB(New_B1);

      // it is possible that CondTargetBB and FollowBB are the same. But the
      // above Dist checking should already filtered this case.
      MBB->removeSuccessor(CondTargetBB);
      MBB->removeSuccessor(FollowBB);
      MBB->addSuccessor(New_B0);
      MBB->addSuccessor(New_B1);

      // Populate insns in New_B0 and New_B1.
      BuildMI(New_B0, CondJmp->getDebugLoc(), TII->get(BPF::JMP)).addMBB(FollowBB);
      BuildMI(New_B1, CondJmp->getDebugLoc(), TII->get(BPF::JMPL))
          .addMBB(CondTargetBB);

      New_B0->addSuccessor(FollowBB);
      New_B1->addSuccessor(CondTargetBB);
      CondJmp->eraseFromParent();
      Changed = true;
      continue;
    }

    //  (4). conditional jmp followed by an unconditional jmp.
    CondTargetBB = CondJmp->getOperand(2).getMBB();
    JmpBB = UncondJmp->getOperand(0).getMBB();

    // We have
    //   B2: ...
    //       if (cond) goto B5
    //       JMP B7
    //   B3: ...
    //
    // If only B2->B5 is out of 16bit range, we can do
    //   B2: ...
    //       if (cond) goto new_B
    //       JMP B7
    //   New_B: gotol B5
    //   B3: ...
    //
    // If only 'JMP B7' is out of 16bit range, we can replace
    // 'JMP B7' with 'JMPL B7'.
    //
    // If both B2->B5 and 'JMP B7' is out of range, just do
    // both the above transformations.
    Dist = SoFarNumInsns[CondTargetBB] - CondTargetBB->size() - CurrNumInsns;
    if (!in16BitRange(Dist)) {
      MachineBasicBlock *New_B = MF->CreateMachineBasicBlock(TermBB);

      // Insert New_B0 into function block list.
      MF->insert(++MBB->getIterator(), New_B);

      // replace B2 cond jump
      if (CondJmp->getOperand(1).isReg())
        BuildMI(*MBB, MachineBasicBlock::iterator(*CondJmp), CondJmp->getDebugLoc(), TII->get(CondJmp->getOpcode()))
            .addReg(CondJmp->getOperand(0).getReg())
            .addReg(CondJmp->getOperand(1).getReg())
            .addMBB(New_B);
      else
        BuildMI(*MBB, MachineBasicBlock::iterator(*CondJmp), CondJmp->getDebugLoc(), TII->get(CondJmp->getOpcode()))
            .addReg(CondJmp->getOperand(0).getReg())
            .addImm(CondJmp->getOperand(1).getImm())
            .addMBB(New_B);

      if (CondTargetBB != JmpBB)
        MBB->removeSuccessor(CondTargetBB);
      MBB->addSuccessor(New_B);

      // Populate insn in New_B.
      BuildMI(New_B, CondJmp->getDebugLoc(), TII->get(BPF::JMPL)).addMBB(CondTargetBB);

      New_B->addSuccessor(CondTargetBB);
      CondJmp->eraseFromParent();
      Changed = true;
    }

    if (!in16BitRange(SoFarNumInsns[JmpBB] - CurrNumInsns)) {
      BuildMI(MBB, UncondJmp->getDebugLoc(), TII->get(BPF::JMPL)).addMBB(JmpBB);
      UncondJmp->eraseFromParent();
      Changed = true;
    }
  }

  return Changed;
}

} // end default namespace

INITIALIZE_PASS(BPFMIPreEmitPeephole, "bpf-mi-pemit-peephole",
                "BPF PreEmit Peephole Optimization", false, false)

char BPFMIPreEmitPeephole::ID = 0;
FunctionPass* llvm::createBPFMIPreEmitPeepholePass()
{
  return new BPFMIPreEmitPeephole();
}
