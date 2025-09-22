//===-- XCoreInstrInfo.cpp - XCore Instruction Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the XCore implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "XCoreInstrInfo.h"
#include "XCore.h"
#include "XCoreMachineFunctionInfo.h"
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

using namespace llvm;

#define GET_INSTRINFO_CTOR_DTOR
#include "XCoreGenInstrInfo.inc"

namespace llvm {
namespace XCore {

  // XCore Condition Codes
  enum CondCode {
    COND_TRUE,
    COND_FALSE,
    COND_INVALID
  };
}
}

// Pin the vtable to this file.
void XCoreInstrInfo::anchor() {}

XCoreInstrInfo::XCoreInstrInfo()
  : XCoreGenInstrInfo(XCore::ADJCALLSTACKDOWN, XCore::ADJCALLSTACKUP),
    RI() {
}

static bool isZeroImm(const MachineOperand &op) {
  return op.isImm() && op.getImm() == 0;
}

/// isLoadFromStackSlot - If the specified machine instruction is a direct
/// load from a stack slot, return the virtual or physical register number of
/// the destination along with the FrameIndex of the loaded stack slot.  If
/// not, return 0.  This predicate must return 0 if the instruction has
/// any side effects other than loading from the stack slot.
Register XCoreInstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                             int &FrameIndex) const {
  int Opcode = MI.getOpcode();
  if (Opcode == XCore::LDWFI)
  {
    if ((MI.getOperand(1).isFI()) &&  // is a stack slot
        (MI.getOperand(2).isImm()) && // the imm is zero
        (isZeroImm(MI.getOperand(2)))) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
  }
  return 0;
}

  /// isStoreToStackSlot - If the specified machine instruction is a direct
  /// store to a stack slot, return the virtual or physical register number of
  /// the source reg along with the FrameIndex of the loaded stack slot.  If
  /// not, return 0.  This predicate must return 0 if the instruction has
  /// any side effects other than storing to the stack slot.
Register XCoreInstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                            int &FrameIndex) const {
  int Opcode = MI.getOpcode();
  if (Opcode == XCore::STWFI)
  {
    if ((MI.getOperand(1).isFI()) &&  // is a stack slot
        (MI.getOperand(2).isImm()) && // the imm is zero
        (isZeroImm(MI.getOperand(2)))) {
      FrameIndex = MI.getOperand(1).getIndex();
      return MI.getOperand(0).getReg();
    }
  }
  return 0;
}

//===----------------------------------------------------------------------===//
// Branch Analysis
//===----------------------------------------------------------------------===//

static inline bool IsBRU(unsigned BrOpc) {
  return BrOpc == XCore::BRFU_u6
      || BrOpc == XCore::BRFU_lu6
      || BrOpc == XCore::BRBU_u6
      || BrOpc == XCore::BRBU_lu6;
}

static inline bool IsBRT(unsigned BrOpc) {
  return BrOpc == XCore::BRFT_ru6
      || BrOpc == XCore::BRFT_lru6
      || BrOpc == XCore::BRBT_ru6
      || BrOpc == XCore::BRBT_lru6;
}

static inline bool IsBRF(unsigned BrOpc) {
  return BrOpc == XCore::BRFF_ru6
      || BrOpc == XCore::BRFF_lru6
      || BrOpc == XCore::BRBF_ru6
      || BrOpc == XCore::BRBF_lru6;
}

static inline bool IsCondBranch(unsigned BrOpc) {
  return IsBRF(BrOpc) || IsBRT(BrOpc);
}

static inline bool IsBR_JT(unsigned BrOpc) {
  return BrOpc == XCore::BR_JT
      || BrOpc == XCore::BR_JT32;
}

/// GetCondFromBranchOpc - Return the XCore CC that matches
/// the correspondent Branch instruction opcode.
static XCore::CondCode GetCondFromBranchOpc(unsigned BrOpc)
{
  if (IsBRT(BrOpc)) {
    return XCore::COND_TRUE;
  } else if (IsBRF(BrOpc)) {
    return XCore::COND_FALSE;
  } else {
    return XCore::COND_INVALID;
  }
}

/// GetCondBranchFromCond - Return the Branch instruction
/// opcode that matches the cc.
static inline unsigned GetCondBranchFromCond(XCore::CondCode CC)
{
  switch (CC) {
  default: llvm_unreachable("Illegal condition code!");
  case XCore::COND_TRUE   : return XCore::BRFT_lru6;
  case XCore::COND_FALSE  : return XCore::BRFF_lru6;
  }
}

/// GetOppositeBranchCondition - Return the inverse of the specified
/// condition, e.g. turning COND_E to COND_NE.
static inline XCore::CondCode GetOppositeBranchCondition(XCore::CondCode CC)
{
  switch (CC) {
  default: llvm_unreachable("Illegal condition code!");
  case XCore::COND_TRUE   : return XCore::COND_FALSE;
  case XCore::COND_FALSE  : return XCore::COND_TRUE;
  }
}

/// analyzeBranch - Analyze the branching code at the end of MBB, returning
/// true if it cannot be understood (e.g. it's a switch dispatch or isn't
/// implemented for a target).  Upon success, this returns false and returns
/// with the following information in various cases:
///
/// 1. If this block ends with no branches (it just falls through to its succ)
///    just return false, leaving TBB/FBB null.
/// 2. If this block ends with only an unconditional branch, it sets TBB to be
///    the destination block.
/// 3. If this block ends with an conditional branch and it falls through to
///    an successor block, it sets TBB to be the branch destination block and a
///    list of operands that evaluate the condition. These
///    operands can be passed to other TargetInstrInfo methods to create new
///    branches.
/// 4. If this block ends with an conditional branch and an unconditional
///    block, it returns the 'true' destination in TBB, the 'false' destination
///    in FBB, and a list of operands that evaluate the condition. These
///    operands can be passed to other TargetInstrInfo methods to create new
///    branches.
///
/// Note that removeBranch and insertBranch must be implemented to support
/// cases where this method returns success.
///
bool XCoreInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                   MachineBasicBlock *&TBB,
                                   MachineBasicBlock *&FBB,
                                   SmallVectorImpl<MachineOperand> &Cond,
                                   bool AllowModify) const {
  // If the block has no terminators, it just falls into the block after it.
  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return false;

  if (!isUnpredicatedTerminator(*I))
    return false;

  // Get the last instruction in the block.
  MachineInstr *LastInst = &*I;

  // If there is only one terminator instruction, process it.
  if (I == MBB.begin() || !isUnpredicatedTerminator(*--I)) {
    if (IsBRU(LastInst->getOpcode())) {
      TBB = LastInst->getOperand(0).getMBB();
      return false;
    }

    XCore::CondCode BranchCode = GetCondFromBranchOpc(LastInst->getOpcode());
    if (BranchCode == XCore::COND_INVALID)
      return true;  // Can't handle indirect branch.

    // Conditional branch
    // Block ends with fall-through condbranch.

    TBB = LastInst->getOperand(1).getMBB();
    Cond.push_back(MachineOperand::CreateImm(BranchCode));
    Cond.push_back(LastInst->getOperand(0));
    return false;
  }

  // Get the instruction before it if it's a terminator.
  MachineInstr *SecondLastInst = &*I;

  // If there are three terminators, we don't know what sort of block this is.
  if (SecondLastInst && I != MBB.begin() && isUnpredicatedTerminator(*--I))
    return true;

  unsigned SecondLastOpc    = SecondLastInst->getOpcode();
  XCore::CondCode BranchCode = GetCondFromBranchOpc(SecondLastOpc);

  // If the block ends with conditional branch followed by unconditional,
  // handle it.
  if (BranchCode != XCore::COND_INVALID
    && IsBRU(LastInst->getOpcode())) {

    TBB = SecondLastInst->getOperand(1).getMBB();
    Cond.push_back(MachineOperand::CreateImm(BranchCode));
    Cond.push_back(SecondLastInst->getOperand(0));

    FBB = LastInst->getOperand(0).getMBB();
    return false;
  }

  // If the block ends with two unconditional branches, handle it.  The second
  // one is not executed, so remove it.
  if (IsBRU(SecondLastInst->getOpcode()) &&
      IsBRU(LastInst->getOpcode())) {
    TBB = SecondLastInst->getOperand(0).getMBB();
    I = LastInst;
    if (AllowModify)
      I->eraseFromParent();
    return false;
  }

  // Likewise if it ends with a branch table followed by an unconditional branch.
  if (IsBR_JT(SecondLastInst->getOpcode()) && IsBRU(LastInst->getOpcode())) {
    I = LastInst;
    if (AllowModify)
      I->eraseFromParent();
    return true;
  }

  // Otherwise, can't handle this.
  return true;
}

unsigned XCoreInstrInfo::insertBranch(MachineBasicBlock &MBB,
                                      MachineBasicBlock *TBB,
                                      MachineBasicBlock *FBB,
                                      ArrayRef<MachineOperand> Cond,
                                      const DebugLoc &DL,
                                      int *BytesAdded) const {
  // Shouldn't be a fall through.
  assert(TBB && "insertBranch must not be told to insert a fallthrough");
  assert((Cond.size() == 2 || Cond.size() == 0) &&
         "Unexpected number of components!");
  assert(!BytesAdded && "code size not handled");

  if (!FBB) { // One way branch.
    if (Cond.empty()) {
      // Unconditional branch
      BuildMI(&MBB, DL, get(XCore::BRFU_lu6)).addMBB(TBB);
    } else {
      // Conditional branch.
      unsigned Opc = GetCondBranchFromCond((XCore::CondCode)Cond[0].getImm());
      BuildMI(&MBB, DL, get(Opc)).addReg(Cond[1].getReg())
                             .addMBB(TBB);
    }
    return 1;
  }

  // Two-way Conditional branch.
  assert(Cond.size() == 2 && "Unexpected number of components!");
  unsigned Opc = GetCondBranchFromCond((XCore::CondCode)Cond[0].getImm());
  BuildMI(&MBB, DL, get(Opc)).addReg(Cond[1].getReg())
                         .addMBB(TBB);
  BuildMI(&MBB, DL, get(XCore::BRFU_lu6)).addMBB(FBB);
  return 2;
}

unsigned
XCoreInstrInfo::removeBranch(MachineBasicBlock &MBB, int *BytesRemoved) const {
  assert(!BytesRemoved && "code size not handled");

  MachineBasicBlock::iterator I = MBB.getLastNonDebugInstr();
  if (I == MBB.end())
    return 0;

  if (!IsBRU(I->getOpcode()) && !IsCondBranch(I->getOpcode()))
    return 0;

  // Remove the branch.
  I->eraseFromParent();

  I = MBB.end();

  if (I == MBB.begin()) return 1;
  --I;
  if (!IsCondBranch(I->getOpcode()))
    return 1;

  // Remove the branch.
  I->eraseFromParent();
  return 2;
}

void XCoreInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I,
                                 const DebugLoc &DL, MCRegister DestReg,
                                 MCRegister SrcReg, bool KillSrc) const {
  bool GRDest = XCore::GRRegsRegClass.contains(DestReg);
  bool GRSrc  = XCore::GRRegsRegClass.contains(SrcReg);

  if (GRDest && GRSrc) {
    BuildMI(MBB, I, DL, get(XCore::ADD_2rus), DestReg)
      .addReg(SrcReg, getKillRegState(KillSrc))
      .addImm(0);
    return;
  }

  if (GRDest && SrcReg == XCore::SP) {
    BuildMI(MBB, I, DL, get(XCore::LDAWSP_ru6), DestReg).addImm(0);
    return;
  }

  if (DestReg == XCore::SP && GRSrc) {
    BuildMI(MBB, I, DL, get(XCore::SETSP_1r))
      .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }
  llvm_unreachable("Impossible reg-to-reg copy");
}

void XCoreInstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator I, Register SrcReg,
    bool isKill, int FrameIndex, const TargetRegisterClass *RC,
    const TargetRegisterInfo *TRI, Register VReg) const {
  DebugLoc DL;
  if (I != MBB.end() && !I->isDebugInstr())
    DL = I->getDebugLoc();
  MachineFunction *MF = MBB.getParent();
  const MachineFrameInfo &MFI = MF->getFrameInfo();
  MachineMemOperand *MMO = MF->getMachineMemOperand(
      MachinePointerInfo::getFixedStack(*MF, FrameIndex),
      MachineMemOperand::MOStore, MFI.getObjectSize(FrameIndex),
      MFI.getObjectAlign(FrameIndex));
  BuildMI(MBB, I, DL, get(XCore::STWFI))
    .addReg(SrcReg, getKillRegState(isKill))
    .addFrameIndex(FrameIndex)
    .addImm(0)
    .addMemOperand(MMO);
}

void XCoreInstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                          MachineBasicBlock::iterator I,
                                          Register DestReg, int FrameIndex,
                                          const TargetRegisterClass *RC,
                                          const TargetRegisterInfo *TRI,
                                          Register VReg) const {
  DebugLoc DL;
  if (I != MBB.end() && !I->isDebugInstr())
    DL = I->getDebugLoc();
  MachineFunction *MF = MBB.getParent();
  const MachineFrameInfo &MFI = MF->getFrameInfo();
  MachineMemOperand *MMO = MF->getMachineMemOperand(
      MachinePointerInfo::getFixedStack(*MF, FrameIndex),
      MachineMemOperand::MOLoad, MFI.getObjectSize(FrameIndex),
      MFI.getObjectAlign(FrameIndex));
  BuildMI(MBB, I, DL, get(XCore::LDWFI), DestReg)
    .addFrameIndex(FrameIndex)
    .addImm(0)
    .addMemOperand(MMO);
}

bool XCoreInstrInfo::
reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const {
  assert((Cond.size() == 2) &&
          "Invalid XCore branch condition!");
  Cond[0].setImm(GetOppositeBranchCondition((XCore::CondCode)Cond[0].getImm()));
  return false;
}

static inline bool isImmU6(unsigned val) {
  return val < (1 << 6);
}

static inline bool isImmU16(unsigned val) {
  return val < (1 << 16);
}

static bool isImmMskBitp(unsigned val) {
  if (!isMask_32(val)) {
    return false;
  }
  int N = llvm::bit_width(val);
  return (N >= 1 && N <= 8) || N == 16 || N == 24 || N == 32;
}

MachineBasicBlock::iterator XCoreInstrInfo::loadImmediate(
                                              MachineBasicBlock &MBB,
                                              MachineBasicBlock::iterator MI,
                                              unsigned Reg, uint64_t Value) const {
  DebugLoc dl;
  if (MI != MBB.end() && !MI->isDebugInstr())
    dl = MI->getDebugLoc();
  if (isImmMskBitp(Value)) {
    int N = llvm::bit_width(Value);
    return BuildMI(MBB, MI, dl, get(XCore::MKMSK_rus), Reg)
        .addImm(N)
        .getInstr();
  }
  if (isImmU16(Value)) {
    int Opcode = isImmU6(Value) ? XCore::LDC_ru6 : XCore::LDC_lru6;
    return BuildMI(MBB, MI, dl, get(Opcode), Reg).addImm(Value).getInstr();
  }
  MachineConstantPool *ConstantPool = MBB.getParent()->getConstantPool();
  const Constant *C = ConstantInt::get(
        Type::getInt32Ty(MBB.getParent()->getFunction().getContext()), Value);
  unsigned Idx = ConstantPool->getConstantPoolIndex(C, Align(4));
  return BuildMI(MBB, MI, dl, get(XCore::LDWCP_lru6), Reg)
      .addConstantPoolIndex(Idx)
      .getInstr();
}
