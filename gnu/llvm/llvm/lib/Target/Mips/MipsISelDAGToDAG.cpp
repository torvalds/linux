//===-- MipsISelDAGToDAG.cpp - A Dag to Dag Inst Selector for Mips --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines an instruction selector for the MIPS target.
//
//===----------------------------------------------------------------------===//

#include "MipsISelDAGToDAG.h"
#include "MCTargetDesc/MipsBaseInfo.h"
#include "Mips.h"
#include "Mips16ISelDAGToDAG.h"
#include "MipsMachineFunction.h"
#include "MipsRegisterInfo.h"
#include "MipsSEISelDAGToDAG.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/StackProtector.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

#define DEBUG_TYPE "mips-isel"
#define PASS_NAME "MIPS DAG->DAG Pattern Instruction Selection"

//===----------------------------------------------------------------------===//
// Instruction Selector Implementation
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// MipsDAGToDAGISel - MIPS specific code to select MIPS machine
// instructions for SelectionDAG operations.
//===----------------------------------------------------------------------===//

void MipsDAGToDAGISelLegacy::getAnalysisUsage(AnalysisUsage &AU) const {
  // There are multiple MipsDAGToDAGISel instances added to the pass pipeline.
  // We need to preserve StackProtector for the next one.
  AU.addPreserved<StackProtector>();
  SelectionDAGISelLegacy::getAnalysisUsage(AU);
}

bool MipsDAGToDAGISel::runOnMachineFunction(MachineFunction &MF) {
  Subtarget = &MF.getSubtarget<MipsSubtarget>();
  bool Ret = SelectionDAGISel::runOnMachineFunction(MF);

  processFunctionAfterISel(MF);

  return Ret;
}

/// getGlobalBaseReg - Output the instructions required to put the
/// GOT address into a register.
SDNode *MipsDAGToDAGISel::getGlobalBaseReg() {
  Register GlobalBaseReg = MF->getInfo<MipsFunctionInfo>()->getGlobalBaseReg(*MF);
  return CurDAG->getRegister(GlobalBaseReg, getTargetLowering()->getPointerTy(
                                                CurDAG->getDataLayout()))
      .getNode();
}

/// ComplexPattern used on MipsInstrInfo
/// Used on Mips Load/Store instructions
bool MipsDAGToDAGISel::selectAddrRegImm(SDValue Addr, SDValue &Base,
                                        SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectAddrDefault(SDValue Addr, SDValue &Base,
                                         SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectIntAddr(SDValue Addr, SDValue &Base,
                                     SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectIntAddr11MM(SDValue Addr, SDValue &Base,
                                       SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectIntAddr12MM(SDValue Addr, SDValue &Base,
                                       SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectIntAddr16MM(SDValue Addr, SDValue &Base,
                                       SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectIntAddrLSL2MM(SDValue Addr, SDValue &Base,
                                           SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectIntAddrSImm10(SDValue Addr, SDValue &Base,
                                           SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectIntAddrSImm10Lsl1(SDValue Addr, SDValue &Base,
                                               SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectIntAddrSImm10Lsl2(SDValue Addr, SDValue &Base,
                                               SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectIntAddrSImm10Lsl3(SDValue Addr, SDValue &Base,
                                               SDValue &Offset) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectAddr16(SDValue Addr, SDValue &Base,
                                    SDValue &Offset) {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectAddr16SP(SDValue Addr, SDValue &Base,
                                      SDValue &Offset) {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplat(SDNode *N, APInt &Imm,
                                    unsigned MinSizeInBits) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatUimm1(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatUimm2(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatUimm3(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatUimm4(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatUimm5(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatUimm6(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatUimm8(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatSimm5(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatUimmPow2(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatUimmInvPow2(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatMaskL(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatMaskR(SDValue N, SDValue &Imm) const {
  llvm_unreachable("Unimplemented function.");
  return false;
}

bool MipsDAGToDAGISel::selectVSplatImmEq1(SDValue N) const {
  llvm_unreachable("Unimplemented function.");
}

/// Convert vector addition with vector subtraction if that allows to encode
/// constant as an immediate and thus avoid extra 'ldi' instruction.
/// add X, <-1, -1...> --> sub X, <1, 1...>
bool MipsDAGToDAGISel::selectVecAddAsVecSubIfProfitable(SDNode *Node) {
  assert(Node->getOpcode() == ISD::ADD && "Should only get 'add' here.");

  EVT VT = Node->getValueType(0);
  assert(VT.isVector() && "Should only be called for vectors.");

  SDValue X = Node->getOperand(0);
  SDValue C = Node->getOperand(1);

  auto *BVN = dyn_cast<BuildVectorSDNode>(C);
  if (!BVN)
    return false;

  APInt SplatValue, SplatUndef;
  unsigned SplatBitSize;
  bool HasAnyUndefs;

  if (!BVN->isConstantSplat(SplatValue, SplatUndef, SplatBitSize, HasAnyUndefs,
                            8, !Subtarget->isLittle()))
    return false;

  auto IsInlineConstant = [](const APInt &Imm) { return Imm.isIntN(5); };

  if (IsInlineConstant(SplatValue))
    return false; // Can already be encoded as an immediate.

  APInt NegSplatValue = 0 - SplatValue;
  if (!IsInlineConstant(NegSplatValue))
    return false; // Even if we negate it it won't help.

  SDLoc DL(Node);

  SDValue NegC = CurDAG->FoldConstantArithmetic(
      ISD::SUB, DL, VT, {CurDAG->getConstant(0, DL, VT), C});
  assert(NegC && "Constant-folding failed!");
  SDValue NewNode = CurDAG->getNode(ISD::SUB, DL, VT, X, NegC);

  ReplaceNode(Node, NewNode.getNode());
  SelectCode(NewNode.getNode());
  return true;
}

/// Select instructions not customized! Used for
/// expanded, promoted and normal instructions
void MipsDAGToDAGISel::Select(SDNode *Node) {
  unsigned Opcode = Node->getOpcode();

  // If we have a custom node, we already have selected!
  if (Node->isMachineOpcode()) {
    LLVM_DEBUG(errs() << "== "; Node->dump(CurDAG); errs() << "\n");
    Node->setNodeId(-1);
    return;
  }

  // See if subclasses can handle this node.
  if (trySelect(Node))
    return;

  switch(Opcode) {
  default: break;

  case ISD::ADD:
    if (Node->getSimpleValueType(0).isVector() &&
        selectVecAddAsVecSubIfProfitable(Node))
      return;
    break;

  // Get target GOT address.
  case ISD::GLOBAL_OFFSET_TABLE:
    ReplaceNode(Node, getGlobalBaseReg());
    return;

#ifndef NDEBUG
  case ISD::LOAD:
  case ISD::STORE:
    assert((Subtarget->systemSupportsUnalignedAccess() ||
            cast<MemSDNode>(Node)->getAlign() >=
                cast<MemSDNode>(Node)->getMemoryVT().getStoreSize()) &&
           "Unexpected unaligned loads/stores.");
    break;
#endif
  }

  // Select the default instruction
  SelectCode(Node);
}

bool MipsDAGToDAGISel::SelectInlineAsmMemoryOperand(
    const SDValue &Op, InlineAsm::ConstraintCode ConstraintID,
    std::vector<SDValue> &OutOps) {
  // All memory constraints can at least accept raw pointers.
  switch(ConstraintID) {
  default:
    llvm_unreachable("Unexpected asm memory constraint");
  case InlineAsm::ConstraintCode::m:
  case InlineAsm::ConstraintCode::R:
  case InlineAsm::ConstraintCode::ZC:
    OutOps.push_back(Op);
    return false;
  }
  return true;
}

bool MipsDAGToDAGISel::isUnneededShiftMask(SDNode *N,
                                           unsigned ShAmtBits) const {
  assert(N->getOpcode() == ISD::AND && "Unexpected opcode");

  const APInt &RHS = N->getConstantOperandAPInt(1);
  if (RHS.countr_one() >= ShAmtBits) {
    LLVM_DEBUG(
        dbgs()
        << DEBUG_TYPE
        << " Need optimize 'and & shl/srl/sra' and operand value bits is "
        << RHS.countr_one() << "\n");
    return true;
  }

  KnownBits Known = CurDAG->computeKnownBits(N->getOperand(0));
  return (Known.Zero | RHS).countr_one() >= ShAmtBits;
}

char MipsDAGToDAGISelLegacy::ID = 0;

MipsDAGToDAGISelLegacy::MipsDAGToDAGISelLegacy(
    std::unique_ptr<SelectionDAGISel> S)
    : SelectionDAGISelLegacy(ID, std::move(S)) {}

INITIALIZE_PASS(MipsDAGToDAGISelLegacy, DEBUG_TYPE, PASS_NAME, false, false)
