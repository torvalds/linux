//===-- SPIRVInstrInfo.cpp - SPIR-V Instruction Information ------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the SPIR-V implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "SPIRVInstrInfo.h"
#include "SPIRV.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/Support/ErrorHandling.h"

#define GET_INSTRINFO_CTOR_DTOR
#include "SPIRVGenInstrInfo.inc"

using namespace llvm;

SPIRVInstrInfo::SPIRVInstrInfo() : SPIRVGenInstrInfo() {}

bool SPIRVInstrInfo::isConstantInstr(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case SPIRV::OpConstantTrue:
  case SPIRV::OpConstantFalse:
  case SPIRV::OpConstantI:
  case SPIRV::OpConstantF:
  case SPIRV::OpConstantComposite:
  case SPIRV::OpConstantSampler:
  case SPIRV::OpConstantNull:
  case SPIRV::OpSpecConstantTrue:
  case SPIRV::OpSpecConstantFalse:
  case SPIRV::OpSpecConstant:
  case SPIRV::OpSpecConstantComposite:
  case SPIRV::OpSpecConstantOp:
  case SPIRV::OpUndef:
  case SPIRV::OpConstantFunctionPointerINTEL:
    return true;
  default:
    return false;
  }
}

bool SPIRVInstrInfo::isInlineAsmDefInstr(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case SPIRV::OpAsmTargetINTEL:
  case SPIRV::OpAsmINTEL:
    return true;
  default:
    return false;
  }
}

bool SPIRVInstrInfo::isTypeDeclInstr(const MachineInstr &MI) const {
  auto &MRI = MI.getMF()->getRegInfo();
  if (MI.getNumDefs() >= 1 && MI.getOperand(0).isReg()) {
    auto DefRegClass = MRI.getRegClassOrNull(MI.getOperand(0).getReg());
    return DefRegClass && DefRegClass->getID() == SPIRV::TYPERegClass.getID();
  } else {
    return MI.getOpcode() == SPIRV::OpTypeForwardPointer;
  }
}

bool SPIRVInstrInfo::isDecorationInstr(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case SPIRV::OpDecorate:
  case SPIRV::OpDecorateId:
  case SPIRV::OpDecorateString:
  case SPIRV::OpMemberDecorate:
  case SPIRV::OpMemberDecorateString:
    return true;
  default:
    return false;
  }
}

bool SPIRVInstrInfo::isHeaderInstr(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case SPIRV::OpCapability:
  case SPIRV::OpExtension:
  case SPIRV::OpExtInstImport:
  case SPIRV::OpMemoryModel:
  case SPIRV::OpEntryPoint:
  case SPIRV::OpExecutionMode:
  case SPIRV::OpExecutionModeId:
  case SPIRV::OpString:
  case SPIRV::OpSourceExtension:
  case SPIRV::OpSource:
  case SPIRV::OpSourceContinued:
  case SPIRV::OpName:
  case SPIRV::OpMemberName:
  case SPIRV::OpModuleProcessed:
    return true;
  default:
    return isTypeDeclInstr(MI) || isConstantInstr(MI) || isDecorationInstr(MI);
  }
}

bool SPIRVInstrInfo::canUseFastMathFlags(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case SPIRV::OpFAddS:
  case SPIRV::OpFSubS:
  case SPIRV::OpFMulS:
  case SPIRV::OpFDivS:
  case SPIRV::OpFRemS:
  case SPIRV::OpFAddV:
  case SPIRV::OpFSubV:
  case SPIRV::OpFMulV:
  case SPIRV::OpFDivV:
  case SPIRV::OpFRemV:
  case SPIRV::OpFMod:
    return true;
  default:
    return false;
  }
}

bool SPIRVInstrInfo::canUseNSW(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case SPIRV::OpIAddS:
  case SPIRV::OpIAddV:
  case SPIRV::OpISubS:
  case SPIRV::OpISubV:
  case SPIRV::OpIMulS:
  case SPIRV::OpIMulV:
  case SPIRV::OpShiftLeftLogicalS:
  case SPIRV::OpShiftLeftLogicalV:
  case SPIRV::OpSNegate:
    return true;
  default:
    return false;
  }
}

bool SPIRVInstrInfo::canUseNUW(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case SPIRV::OpIAddS:
  case SPIRV::OpIAddV:
  case SPIRV::OpISubS:
  case SPIRV::OpISubV:
  case SPIRV::OpIMulS:
  case SPIRV::OpIMulV:
    return true;
  default:
    return false;
  }
}

// Analyze the branching code at the end of MBB, returning
// true if it cannot be understood (e.g. it's a switch dispatch or isn't
// implemented for a target).  Upon success, this returns false and returns
// with the following information in various cases:
//
// 1. If this block ends with no branches (it just falls through to its succ)
//    just return false, leaving TBB/FBB null.
// 2. If this block ends with only an unconditional branch, it sets TBB to be
//    the destination block.
// 3. If this block ends with a conditional branch and it falls through to a
//    successor block, it sets TBB to be the branch destination block and a
//    list of operands that evaluate the condition. These operands can be
//    passed to other TargetInstrInfo methods to create new branches.
// 4. If this block ends with a conditional branch followed by an
//    unconditional branch, it returns the 'true' destination in TBB, the
//    'false' destination in FBB, and a list of operands that evaluate the
//    condition.  These operands can be passed to other TargetInstrInfo
//    methods to create new branches.
//
// Note that removeBranch and insertBranch must be implemented to support
// cases where this method returns success.
//
// If AllowModify is true, then this routine is allowed to modify the basic
// block (e.g. delete instructions after the unconditional branch).
//
// The CFG information in MBB.Predecessors and MBB.Successors must be valid
// before calling this function.
bool SPIRVInstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                   MachineBasicBlock *&TBB,
                                   MachineBasicBlock *&FBB,
                                   SmallVectorImpl<MachineOperand> &Cond,
                                   bool AllowModify) const {
  TBB = nullptr;
  FBB = nullptr;
  if (MBB.empty())
    return false;
  auto MI = MBB.getLastNonDebugInstr();
  if (!MI.isValid())
    return false;
  if (MI->getOpcode() == SPIRV::OpBranch) {
    TBB = MI->getOperand(0).getMBB();
    return false;
  } else if (MI->getOpcode() == SPIRV::OpBranchConditional) {
    Cond.push_back(MI->getOperand(0));
    TBB = MI->getOperand(1).getMBB();
    if (MI->getNumOperands() == 3) {
      FBB = MI->getOperand(2).getMBB();
    }
    return false;
  } else {
    return true;
  }
}

// Remove the branching code at the end of the specific MBB.
// This is only invoked in cases where analyzeBranch returns success. It
// returns the number of instructions that were removed.
// If \p BytesRemoved is non-null, report the change in code size from the
// removed instructions.
unsigned SPIRVInstrInfo::removeBranch(MachineBasicBlock &MBB,
                                      int *BytesRemoved) const {
  report_fatal_error("Branch removal not supported, as MBB info not propagated"
                     " to OpPhi instructions. Try using -O0 instead.");
}

// Insert branch code into the end of the specified MachineBasicBlock. The
// operands to this method are the same as those returned by analyzeBranch.
// This is only invoked in cases where analyzeBranch returns success. It
// returns the number of instructions inserted. If \p BytesAdded is non-null,
// report the change in code size from the added instructions.
//
// It is also invoked by tail merging to add unconditional branches in
// cases where analyzeBranch doesn't apply because there was no original
// branch to analyze.  At least this much must be implemented, else tail
// merging needs to be disabled.
//
// The CFG information in MBB.Predecessors and MBB.Successors must be valid
// before calling this function.
unsigned SPIRVInstrInfo::insertBranch(
    MachineBasicBlock &MBB, MachineBasicBlock *TBB, MachineBasicBlock *FBB,
    ArrayRef<MachineOperand> Cond, const DebugLoc &DL, int *BytesAdded) const {
  report_fatal_error("Branch insertion not supported, as MBB info not "
                     "propagated to OpPhi instructions. Try using "
                     "-O0 instead.");
}

void SPIRVInstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I,
                                 const DebugLoc &DL, MCRegister DestReg,
                                 MCRegister SrcReg, bool KillSrc) const {
  // Actually we don't need this COPY instruction. However if we do nothing with
  // it, post RA pseudo instrs expansion just removes it and we get the code
  // with undef registers. Therefore, we need to replace all uses of dst with
  // the src register. COPY instr itself will be safely removed later.
  assert(I->isCopy() && "Copy instruction is expected");
  auto DstOp = I->getOperand(0);
  auto SrcOp = I->getOperand(1);
  assert(DstOp.isReg() && SrcOp.isReg() &&
         "Register operands are expected in COPY");
  auto &MRI = I->getMF()->getRegInfo();
  MRI.replaceRegWith(DstOp.getReg(), SrcOp.getReg());
}

bool SPIRVInstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  if (MI.getOpcode() == SPIRV::GET_ID || MI.getOpcode() == SPIRV::GET_ID64 ||
      MI.getOpcode() == SPIRV::GET_fID || MI.getOpcode() == SPIRV::GET_fID64 ||
      MI.getOpcode() == SPIRV::GET_pID32 ||
      MI.getOpcode() == SPIRV::GET_pID64 || MI.getOpcode() == SPIRV::GET_vfID ||
      MI.getOpcode() == SPIRV::GET_vID || MI.getOpcode() == SPIRV::GET_vpID32 ||
      MI.getOpcode() == SPIRV::GET_vpID64) {
    auto &MRI = MI.getMF()->getRegInfo();
    MRI.replaceRegWith(MI.getOperand(0).getReg(), MI.getOperand(1).getReg());
    MI.eraseFromParent();
    return true;
  }
  return false;
}
