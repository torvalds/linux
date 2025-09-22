//===-- WebAssemblyInstrInfo.cpp - WebAssembly Instruction Information ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the WebAssembly implementation of the
/// TargetInstrInfo class.
///
//===----------------------------------------------------------------------===//

#include "WebAssemblyInstrInfo.h"
#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssembly.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "WebAssemblySubtarget.h"
#include "WebAssemblyUtilities.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-instr-info"

#define GET_INSTRINFO_CTOR_DTOR
#include "WebAssemblyGenInstrInfo.inc"

// defines WebAssembly::getNamedOperandIdx
#define GET_INSTRINFO_NAMED_OPS
#include "WebAssemblyGenInstrInfo.inc"

WebAssemblyInstrInfo::WebAssemblyInstrInfo(const WebAssemblySubtarget &STI)
    : WebAssemblyGenInstrInfo(WebAssembly::ADJCALLSTACKDOWN,
                              WebAssembly::ADJCALLSTACKUP,
                              WebAssembly::CATCHRET),
      RI(STI.getTargetTriple()) {}

bool WebAssemblyInstrInfo::isReallyTriviallyReMaterializable(
    const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case WebAssembly::CONST_I32:
  case WebAssembly::CONST_I64:
  case WebAssembly::CONST_F32:
  case WebAssembly::CONST_F64:
    // TargetInstrInfo::isReallyTriviallyReMaterializable misses these
    // because of the ARGUMENTS implicit def, so we manualy override it here.
    return true;
  default:
    return TargetInstrInfo::isReallyTriviallyReMaterializable(MI);
  }
}

void WebAssemblyInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                       MachineBasicBlock::iterator I,
                                       const DebugLoc &DL, MCRegister DestReg,
                                       MCRegister SrcReg, bool KillSrc) const {
  // This method is called by post-RA expansion, which expects only pregs to
  // exist. However we need to handle both here.
  auto &MRI = MBB.getParent()->getRegInfo();
  const TargetRegisterClass *RC =
      Register::isVirtualRegister(DestReg)
          ? MRI.getRegClass(DestReg)
          : MRI.getTargetRegisterInfo()->getMinimalPhysRegClass(DestReg);

  unsigned CopyOpcode = WebAssembly::getCopyOpcodeForRegClass(RC);

  BuildMI(MBB, I, DL, get(CopyOpcode), DestReg)
      .addReg(SrcReg, KillSrc ? RegState::Kill : 0);
}

MachineInstr *WebAssemblyInstrInfo::commuteInstructionImpl(
    MachineInstr &MI, bool NewMI, unsigned OpIdx1, unsigned OpIdx2) const {
  // If the operands are stackified, we can't reorder them.
  WebAssemblyFunctionInfo &MFI =
      *MI.getParent()->getParent()->getInfo<WebAssemblyFunctionInfo>();
  if (MFI.isVRegStackified(MI.getOperand(OpIdx1).getReg()) ||
      MFI.isVRegStackified(MI.getOperand(OpIdx2).getReg()))
    return nullptr;

  // Otherwise use the default implementation.
  return TargetInstrInfo::commuteInstructionImpl(MI, NewMI, OpIdx1, OpIdx2);
}

// Branch analysis.
bool WebAssemblyInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                         MachineBasicBlock *&TBB,
                                         MachineBasicBlock *&FBB,
                                         SmallVectorImpl<MachineOperand> &Cond,
                                         bool /*AllowModify*/) const {
  const auto &MFI = *MBB.getParent()->getInfo<WebAssemblyFunctionInfo>();
  // WebAssembly has control flow that doesn't have explicit branches or direct
  // fallthrough (e.g. try/catch), which can't be modeled by analyzeBranch. It
  // is created after CFGStackify.
  if (MFI.isCFGStackified())
    return true;

  bool HaveCond = false;
  for (MachineInstr &MI : MBB.terminators()) {
    switch (MI.getOpcode()) {
    default:
      // Unhandled instruction; bail out.
      return true;
    case WebAssembly::BR_IF:
      if (HaveCond)
        return true;
      Cond.push_back(MachineOperand::CreateImm(true));
      Cond.push_back(MI.getOperand(1));
      TBB = MI.getOperand(0).getMBB();
      HaveCond = true;
      break;
    case WebAssembly::BR_UNLESS:
      if (HaveCond)
        return true;
      Cond.push_back(MachineOperand::CreateImm(false));
      Cond.push_back(MI.getOperand(1));
      TBB = MI.getOperand(0).getMBB();
      HaveCond = true;
      break;
    case WebAssembly::BR:
      if (!HaveCond)
        TBB = MI.getOperand(0).getMBB();
      else
        FBB = MI.getOperand(0).getMBB();
      break;
    }
    if (MI.isBarrier())
      break;
  }

  return false;
}

unsigned WebAssemblyInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                            int *BytesRemoved) const {
  assert(!BytesRemoved && "code size not handled");

  MachineBasicBlock::instr_iterator I = MBB.instr_end();
  unsigned Count = 0;

  while (I != MBB.instr_begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (!I->isTerminator())
      break;
    // Remove the branch.
    I->eraseFromParent();
    I = MBB.instr_end();
    ++Count;
  }

  return Count;
}

unsigned WebAssemblyInstrInfo::insertBranch(
    MachineBasicBlock &MBB, MachineBasicBlock *TBB, MachineBasicBlock *FBB,
    ArrayRef<MachineOperand> Cond, const DebugLoc &DL, int *BytesAdded) const {
  assert(!BytesAdded && "code size not handled");

  if (Cond.empty()) {
    if (!TBB)
      return 0;

    BuildMI(&MBB, DL, get(WebAssembly::BR)).addMBB(TBB);
    return 1;
  }

  assert(Cond.size() == 2 && "Expected a flag and a successor block");

  if (Cond[0].getImm())
    BuildMI(&MBB, DL, get(WebAssembly::BR_IF)).addMBB(TBB).add(Cond[1]);
  else
    BuildMI(&MBB, DL, get(WebAssembly::BR_UNLESS)).addMBB(TBB).add(Cond[1]);
  if (!FBB)
    return 1;

  BuildMI(&MBB, DL, get(WebAssembly::BR)).addMBB(FBB);
  return 2;
}

bool WebAssemblyInstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 2 && "Expected a flag and a condition expression");
  Cond.front() = MachineOperand::CreateImm(!Cond.front().getImm());
  return false;
}

ArrayRef<std::pair<int, const char *>>
WebAssemblyInstrInfo::getSerializableTargetIndices() const {
  static const std::pair<int, const char *> TargetIndices[] = {
      {WebAssembly::TI_LOCAL, "wasm-local"},
      {WebAssembly::TI_GLOBAL_FIXED, "wasm-global-fixed"},
      {WebAssembly::TI_OPERAND_STACK, "wasm-operand-stack"},
      {WebAssembly::TI_GLOBAL_RELOC, "wasm-global-reloc"},
      {WebAssembly::TI_LOCAL_INDIRECT, "wasm-local-indirect"}};
  return ArrayRef(TargetIndices);
}

const MachineOperand &
WebAssemblyInstrInfo::getCalleeOperand(const MachineInstr &MI) const {
  return WebAssembly::getCalleeOp(MI);
}

// This returns true when the instruction defines a value of a TargetIndex
// operand that can be tracked by offsets. For Wasm, this returns true for only
// local.set/local.tees. This is currently used by LiveDebugValues analysis.
//
// These are not included:
// - In theory we need to add global.set here too, but we don't have global
//   indices at this point because they are relocatable and we address them by
//   names until linking, so we don't have 'offsets' (which are used to store
//   local/global indices) to deal with in LiveDebugValues. And we don't
//   associate debug info in values in globals anyway.
// - All other value-producing instructions, i.e. instructions with defs, can
//   define values in the Wasm stack, which is represented by TI_OPERAND_STACK
//   TargetIndex. But they don't have offset info within the instruction itself,
//   and debug info analysis for them is handled separately in
//   WebAssemblyDebugFixup pass, so we don't worry about them here.
bool WebAssemblyInstrInfo::isExplicitTargetIndexDef(const MachineInstr &MI,
                                                    int &Index,
                                                    int64_t &Offset) const {
  unsigned Opc = MI.getOpcode();
  if (WebAssembly::isLocalSet(Opc) || WebAssembly::isLocalTee(Opc)) {
    Index = WebAssembly::TI_LOCAL;
    Offset = MI.explicit_uses().begin()->getImm();
    return true;
  }
  return false;
}
