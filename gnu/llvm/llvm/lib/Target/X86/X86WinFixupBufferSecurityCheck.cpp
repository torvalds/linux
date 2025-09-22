//===- X86WinFixupBufferSecurityCheck.cpp Fix Buffer Security Check Call -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Buffer Security Check implementation inserts windows specific callback into
// code. On windows, __security_check_cookie call gets call everytime function
// is return without fixup. Since this function is defined in runtime library,
// it incures cost of call in dll which simply does comparison and returns most
// time. With Fixup, We selective move to call in DLL only if comparison fails.
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86FrameLowering.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Module.h"
#include <iterator>

using namespace llvm;

#define DEBUG_TYPE "x86-win-fixup-bscheck"

namespace {

class X86WinFixupBufferSecurityCheckPass : public MachineFunctionPass {
public:
  static char ID;

  X86WinFixupBufferSecurityCheckPass() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "X86 Windows Fixup Buffer Security Check";
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  std::pair<MachineBasicBlock *, MachineInstr *>
  getSecurityCheckerBasicBlock(MachineFunction &MF);

  void getGuardCheckSequence(MachineBasicBlock *CurMBB, MachineInstr *CheckCall,
                             MachineInstr *SeqMI[5]);

  void SplitBasicBlock(MachineBasicBlock *CurMBB, MachineBasicBlock *NewRetMBB,
                       MachineBasicBlock::iterator SplitIt);

  void FinishBlock(MachineBasicBlock *MBB);

  void FinishFunction(MachineBasicBlock *FailMBB, MachineBasicBlock *NewRetMBB);

  std::pair<MachineInstr *, MachineInstr *>
  CreateFailCheckSequence(MachineBasicBlock *CurMBB, MachineBasicBlock *FailMBB,
                          MachineInstr *SeqMI[5]);
};
} // end anonymous namespace

char X86WinFixupBufferSecurityCheckPass::ID = 0;

INITIALIZE_PASS(X86WinFixupBufferSecurityCheckPass, DEBUG_TYPE, DEBUG_TYPE,
                false, false)

FunctionPass *llvm::createX86WinFixupBufferSecurityCheckPass() {
  return new X86WinFixupBufferSecurityCheckPass();
}

void X86WinFixupBufferSecurityCheckPass::SplitBasicBlock(
    MachineBasicBlock *CurMBB, MachineBasicBlock *NewRetMBB,
    MachineBasicBlock::iterator SplitIt) {
  NewRetMBB->splice(NewRetMBB->end(), CurMBB, SplitIt, CurMBB->end());
}

std::pair<MachineBasicBlock *, MachineInstr *>
X86WinFixupBufferSecurityCheckPass::getSecurityCheckerBasicBlock(
    MachineFunction &MF) {
  MachineBasicBlock::reverse_iterator RBegin, REnd;

  for (auto &MBB : llvm::reverse(MF)) {
    for (RBegin = MBB.rbegin(), REnd = MBB.rend(); RBegin != REnd; ++RBegin) {
      auto &MI = *RBegin;
      if (MI.getOpcode() == X86::CALL64pcrel32 &&
          MI.getNumExplicitOperands() == 1) {
        auto MO = MI.getOperand(0);
        if (MO.isGlobal()) {
          auto Callee = dyn_cast<Function>(MO.getGlobal());
          if (Callee && Callee->getName() == "__security_check_cookie") {
            return std::make_pair(&MBB, &MI);
            break;
          }
        }
      }
    }
  }
  return std::make_pair(nullptr, nullptr);
}

void X86WinFixupBufferSecurityCheckPass::getGuardCheckSequence(
    MachineBasicBlock *CurMBB, MachineInstr *CheckCall,
    MachineInstr *SeqMI[5]) {

  MachineBasicBlock::iterator UIt(CheckCall);
  MachineBasicBlock::reverse_iterator DIt(CheckCall);
  // Seq From StackUp to Stack Down Is fixed.
  // ADJCALLSTACKUP64
  ++UIt;
  SeqMI[4] = &*UIt;

  // CALL __security_check_cookie
  SeqMI[3] = CheckCall;

  // COPY function slot cookie
  ++DIt;
  SeqMI[2] = &*DIt;

  // ADJCALLSTACKDOWN64
  ++DIt;
  SeqMI[1] = &*DIt;

  MachineBasicBlock::reverse_iterator XIt(SeqMI[1]);
  for (; XIt != CurMBB->rbegin(); ++XIt) {
    auto &CI = *XIt;
    if ((CI.getOpcode() == X86::XOR64_FP) || (CI.getOpcode() == X86::XOR32_FP))
      break;
  }
  SeqMI[0] = &*XIt;
}

std::pair<MachineInstr *, MachineInstr *>
X86WinFixupBufferSecurityCheckPass::CreateFailCheckSequence(
    MachineBasicBlock *CurMBB, MachineBasicBlock *FailMBB,
    MachineInstr *SeqMI[5]) {

  auto MF = CurMBB->getParent();

  Module &M = *MF->getFunction().getParent();
  GlobalVariable *GV = M.getGlobalVariable("__security_cookie");
  assert(GV && " Security Cookie was not installed!");

  const TargetInstrInfo *TII = MF->getSubtarget().getInstrInfo();

  MachineInstr *GuardXor = SeqMI[0];
  MachineBasicBlock::iterator InsertPt(GuardXor);
  ++InsertPt;

  // Compare security_Cookie with XOR_Val, if not same, we have violation
  auto CMI = BuildMI(*CurMBB, InsertPt, DebugLoc(), TII->get(X86::CMP64rm))
                 .addReg(GuardXor->getOperand(0).getReg())
                 .addReg(X86::RIP)
                 .addImm(1)
                 .addReg(X86::NoRegister)
                 .addGlobalAddress(GV)
                 .addReg(X86::NoRegister);

  BuildMI(*CurMBB, InsertPt, DebugLoc(), TII->get(X86::JCC_1))
      .addMBB(FailMBB)
      .addImm(X86::COND_NE);

  auto JMI = BuildMI(*CurMBB, InsertPt, DebugLoc(), TII->get(X86::JMP_1));

  return std::make_pair(CMI.getInstr(), JMI.getInstr());
}

void X86WinFixupBufferSecurityCheckPass::FinishBlock(MachineBasicBlock *MBB) {
  LivePhysRegs LiveRegs;
  computeAndAddLiveIns(LiveRegs, *MBB);
}

void X86WinFixupBufferSecurityCheckPass::FinishFunction(
    MachineBasicBlock *FailMBB, MachineBasicBlock *NewRetMBB) {
  FailMBB->getParent()->RenumberBlocks();
  // FailMBB includes call to MSCV RT  where is __security_check_cookie
  // function is called. This function uses regcall and it expects cookie
  // value from stack slot.( even if this is modified)
  // Before going further we compute back livein for this block to make sure
  // it is live and provided.
  FinishBlock(FailMBB);
  FinishBlock(NewRetMBB);
}

bool X86WinFixupBufferSecurityCheckPass::runOnMachineFunction(
    MachineFunction &MF) {
  bool Changed = false;
  const X86Subtarget &STI = MF.getSubtarget<X86Subtarget>();

  if (!(STI.isTargetWindowsItanium() || STI.isTargetWindowsMSVC()))
    return Changed;

  // Check if security cookie was installed or not
  Module &M = *MF.getFunction().getParent();
  GlobalVariable *GV = M.getGlobalVariable("__security_cookie");
  if (!GV)
    return Changed;

  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();

  // Check if security check cookie was installed or not
  auto [CurMBB, CheckCall] = getSecurityCheckerBasicBlock(MF);

  if (!CheckCall)
    return Changed;

  MachineBasicBlock *FailMBB = MF.CreateMachineBasicBlock();
  MachineBasicBlock *NewRetMBB = MF.CreateMachineBasicBlock();

  MF.insert(MF.end(), NewRetMBB);
  MF.insert(MF.end(), FailMBB);

  MachineInstr *SeqMI[5];
  getGuardCheckSequence(CurMBB, CheckCall, SeqMI);
  // MachineInstr * GuardXor  = SeqMI[0];

  auto FailSeqRange = CreateFailCheckSequence(CurMBB, FailMBB, SeqMI);
  MachineInstrBuilder JMI(MF, FailSeqRange.second);

  // After Inserting JMP_1, we can not have two terminators
  // in same block, split CurrentMBB after JMP_1
  MachineBasicBlock::iterator SplitIt(SeqMI[4]);
  ++SplitIt;
  SplitBasicBlock(CurMBB, NewRetMBB, SplitIt);

  // Fill up Failure Routine, move Fail Check Squence from CurMBB to FailMBB
  MachineBasicBlock::iterator U1It(SeqMI[1]);
  MachineBasicBlock::iterator U2It(SeqMI[4]);
  ++U2It;
  FailMBB->splice(FailMBB->end(), CurMBB, U1It, U2It);
  BuildMI(*FailMBB, FailMBB->end(), DebugLoc(), TII->get(X86::INT3));

  // Move left over instruction after StackUp
  // from Current Basic BLocks into New Return Block
  JMI.addMBB(NewRetMBB);
  MachineBasicBlock::iterator SplicePt(JMI.getInstr());
  ++SplicePt;
  if (SplicePt != CurMBB->end())
    NewRetMBB->splice(NewRetMBB->end(), CurMBB, SplicePt);

  // Restructure Basic Blocks
  CurMBB->addSuccessor(NewRetMBB);
  CurMBB->addSuccessor(FailMBB);

  FinishFunction(FailMBB, NewRetMBB);
  return !Changed;
}
