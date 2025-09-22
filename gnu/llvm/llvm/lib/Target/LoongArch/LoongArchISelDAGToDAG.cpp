//=- LoongArchISelDAGToDAG.cpp - A dag to dag inst selector for LoongArch -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines an instruction selector for the LoongArch target.
//
//===----------------------------------------------------------------------===//

#include "LoongArchISelDAGToDAG.h"
#include "LoongArchISelLowering.h"
#include "MCTargetDesc/LoongArchMCTargetDesc.h"
#include "MCTargetDesc/LoongArchMatInt.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "loongarch-isel"
#define PASS_NAME "LoongArch DAG->DAG Pattern Instruction Selection"

char LoongArchDAGToDAGISelLegacy::ID;

LoongArchDAGToDAGISelLegacy::LoongArchDAGToDAGISelLegacy(
    LoongArchTargetMachine &TM)
    : SelectionDAGISelLegacy(ID, std::make_unique<LoongArchDAGToDAGISel>(TM)) {}

INITIALIZE_PASS(LoongArchDAGToDAGISelLegacy, DEBUG_TYPE, PASS_NAME, false,
                false)

void LoongArchDAGToDAGISel::Select(SDNode *Node) {
  // If we have a custom node, we have already selected.
  if (Node->isMachineOpcode()) {
    LLVM_DEBUG(dbgs() << "== "; Node->dump(CurDAG); dbgs() << "\n");
    Node->setNodeId(-1);
    return;
  }

  // Instruction Selection not handled by the auto-generated tablegen selection
  // should be handled here.
  unsigned Opcode = Node->getOpcode();
  MVT GRLenVT = Subtarget->getGRLenVT();
  SDLoc DL(Node);
  MVT VT = Node->getSimpleValueType(0);

  switch (Opcode) {
  default:
    break;
  case ISD::Constant: {
    int64_t Imm = cast<ConstantSDNode>(Node)->getSExtValue();
    if (Imm == 0 && VT == GRLenVT) {
      SDValue New = CurDAG->getCopyFromReg(CurDAG->getEntryNode(), DL,
                                           LoongArch::R0, GRLenVT);
      ReplaceNode(Node, New.getNode());
      return;
    }
    SDNode *Result = nullptr;
    SDValue SrcReg = CurDAG->getRegister(LoongArch::R0, GRLenVT);
    // The instructions in the sequence are handled here.
    for (LoongArchMatInt::Inst &Inst : LoongArchMatInt::generateInstSeq(Imm)) {
      SDValue SDImm = CurDAG->getTargetConstant(Inst.Imm, DL, GRLenVT);
      if (Inst.Opc == LoongArch::LU12I_W)
        Result = CurDAG->getMachineNode(LoongArch::LU12I_W, DL, GRLenVT, SDImm);
      else
        Result = CurDAG->getMachineNode(Inst.Opc, DL, GRLenVT, SrcReg, SDImm);
      SrcReg = SDValue(Result, 0);
    }

    ReplaceNode(Node, Result);
    return;
  }
  case ISD::FrameIndex: {
    SDValue Imm = CurDAG->getTargetConstant(0, DL, GRLenVT);
    int FI = cast<FrameIndexSDNode>(Node)->getIndex();
    SDValue TFI = CurDAG->getTargetFrameIndex(FI, VT);
    unsigned ADDIOp =
        Subtarget->is64Bit() ? LoongArch::ADDI_D : LoongArch::ADDI_W;
    ReplaceNode(Node, CurDAG->getMachineNode(ADDIOp, DL, VT, TFI, Imm));
    return;
  }
  case ISD::BITCAST: {
    if (VT.is128BitVector() || VT.is256BitVector()) {
      ReplaceUses(SDValue(Node, 0), Node->getOperand(0));
      CurDAG->RemoveDeadNode(Node);
      return;
    }
    break;
  }
  case ISD::BUILD_VECTOR: {
    // Select appropriate [x]vrepli.[bhwd] instructions for constant splats of
    // 128/256-bit when LSX/LASX is enabled.
    BuildVectorSDNode *BVN = cast<BuildVectorSDNode>(Node);
    APInt SplatValue, SplatUndef;
    unsigned SplatBitSize;
    bool HasAnyUndefs;
    unsigned Op;
    EVT ViaVecTy;
    bool Is128Vec = BVN->getValueType(0).is128BitVector();
    bool Is256Vec = BVN->getValueType(0).is256BitVector();

    if (!Subtarget->hasExtLSX() || (!Is128Vec && !Is256Vec))
      break;
    if (!BVN->isConstantSplat(SplatValue, SplatUndef, SplatBitSize,
                              HasAnyUndefs, 8))
      break;

    switch (SplatBitSize) {
    default:
      break;
    case 8:
      Op = Is256Vec ? LoongArch::PseudoXVREPLI_B : LoongArch::PseudoVREPLI_B;
      ViaVecTy = Is256Vec ? MVT::v32i8 : MVT::v16i8;
      break;
    case 16:
      Op = Is256Vec ? LoongArch::PseudoXVREPLI_H : LoongArch::PseudoVREPLI_H;
      ViaVecTy = Is256Vec ? MVT::v16i16 : MVT::v8i16;
      break;
    case 32:
      Op = Is256Vec ? LoongArch::PseudoXVREPLI_W : LoongArch::PseudoVREPLI_W;
      ViaVecTy = Is256Vec ? MVT::v8i32 : MVT::v4i32;
      break;
    case 64:
      Op = Is256Vec ? LoongArch::PseudoXVREPLI_D : LoongArch::PseudoVREPLI_D;
      ViaVecTy = Is256Vec ? MVT::v4i64 : MVT::v2i64;
      break;
    }

    SDNode *Res;
    // If we have a signed 10 bit integer, we can splat it directly.
    if (SplatValue.isSignedIntN(10)) {
      SDValue Imm = CurDAG->getTargetConstant(SplatValue, DL,
                                              ViaVecTy.getVectorElementType());
      Res = CurDAG->getMachineNode(Op, DL, ViaVecTy, Imm);
      ReplaceNode(Node, Res);
      return;
    }
    break;
  }
  }

  // Select the default instruction.
  SelectCode(Node);
}

bool LoongArchDAGToDAGISel::SelectInlineAsmMemoryOperand(
    const SDValue &Op, InlineAsm::ConstraintCode ConstraintID,
    std::vector<SDValue> &OutOps) {
  SDValue Base = Op;
  SDValue Offset =
      CurDAG->getTargetConstant(0, SDLoc(Op), Subtarget->getGRLenVT());
  switch (ConstraintID) {
  default:
    llvm_unreachable("unexpected asm memory constraint");
  // Reg+Reg addressing.
  case InlineAsm::ConstraintCode::k:
    Base = Op.getOperand(0);
    Offset = Op.getOperand(1);
    break;
  // Reg+simm12 addressing.
  case InlineAsm::ConstraintCode::m:
    if (CurDAG->isBaseWithConstantOffset(Op)) {
      ConstantSDNode *CN = dyn_cast<ConstantSDNode>(Op.getOperand(1));
      if (isIntN(12, CN->getSExtValue())) {
        Base = Op.getOperand(0);
        Offset = CurDAG->getTargetConstant(CN->getZExtValue(), SDLoc(Op),
                                           Op.getValueType());
      }
    }
    break;
  // Reg+0 addressing.
  case InlineAsm::ConstraintCode::ZB:
    break;
  // Reg+(simm14<<2) addressing.
  case InlineAsm::ConstraintCode::ZC:
    if (CurDAG->isBaseWithConstantOffset(Op)) {
      ConstantSDNode *CN = dyn_cast<ConstantSDNode>(Op.getOperand(1));
      if (isIntN(16, CN->getSExtValue()) &&
          isAligned(Align(4ULL), CN->getZExtValue())) {
        Base = Op.getOperand(0);
        Offset = CurDAG->getTargetConstant(CN->getZExtValue(), SDLoc(Op),
                                           Op.getValueType());
      }
    }
    break;
  }
  OutOps.push_back(Base);
  OutOps.push_back(Offset);
  return false;
}

bool LoongArchDAGToDAGISel::SelectBaseAddr(SDValue Addr, SDValue &Base) {
  // If this is FrameIndex, select it directly. Otherwise just let it get
  // selected to a register independently.
  if (auto *FIN = dyn_cast<FrameIndexSDNode>(Addr))
    Base =
        CurDAG->getTargetFrameIndex(FIN->getIndex(), Subtarget->getGRLenVT());
  else
    Base = Addr;
  return true;
}

// Fold constant addresses.
bool LoongArchDAGToDAGISel::SelectAddrConstant(SDValue Addr, SDValue &Base,
                                               SDValue &Offset) {
  SDLoc DL(Addr);
  MVT VT = Addr.getSimpleValueType();

  if (!isa<ConstantSDNode>(Addr))
    return false;

  // If the constant is a simm12, we can fold the whole constant and use R0 as
  // the base.
  int64_t CVal = cast<ConstantSDNode>(Addr)->getSExtValue();
  if (!isInt<12>(CVal))
    return false;
  Base = CurDAG->getRegister(LoongArch::R0, VT);
  Offset = CurDAG->getTargetConstant(SignExtend64<12>(CVal), DL, VT);
  return true;
}

bool LoongArchDAGToDAGISel::selectNonFIBaseAddr(SDValue Addr, SDValue &Base) {
  // If this is FrameIndex, don't select it.
  if (isa<FrameIndexSDNode>(Addr))
    return false;
  Base = Addr;
  return true;
}

bool LoongArchDAGToDAGISel::selectShiftMask(SDValue N, unsigned ShiftWidth,
                                            SDValue &ShAmt) {
  // Shift instructions on LoongArch only read the lower 5 or 6 bits of the
  // shift amount. If there is an AND on the shift amount, we can bypass it if
  // it doesn't affect any of those bits.
  if (N.getOpcode() == ISD::AND && isa<ConstantSDNode>(N.getOperand(1))) {
    const APInt &AndMask = N->getConstantOperandAPInt(1);

    // Since the max shift amount is a power of 2 we can subtract 1 to make a
    // mask that covers the bits needed to represent all shift amounts.
    assert(isPowerOf2_32(ShiftWidth) && "Unexpected max shift amount!");
    APInt ShMask(AndMask.getBitWidth(), ShiftWidth - 1);

    if (ShMask.isSubsetOf(AndMask)) {
      ShAmt = N.getOperand(0);
      return true;
    }

    // SimplifyDemandedBits may have optimized the mask so try restoring any
    // bits that are known zero.
    KnownBits Known = CurDAG->computeKnownBits(N->getOperand(0));
    if (ShMask.isSubsetOf(AndMask | Known.Zero)) {
      ShAmt = N.getOperand(0);
      return true;
    }
  } else if (N.getOpcode() == LoongArchISD::BSTRPICK) {
    // Similar to the above AND, if there is a BSTRPICK on the shift amount, we
    // can bypass it.
    assert(isPowerOf2_32(ShiftWidth) && "Unexpected max shift amount!");
    assert(isa<ConstantSDNode>(N.getOperand(1)) && "Illegal msb operand!");
    assert(isa<ConstantSDNode>(N.getOperand(2)) && "Illegal lsb operand!");
    uint64_t msb = N.getConstantOperandVal(1), lsb = N.getConstantOperandVal(2);
    if (lsb == 0 && Log2_32(ShiftWidth) <= msb + 1) {
      ShAmt = N.getOperand(0);
      return true;
    }
  } else if (N.getOpcode() == ISD::SUB &&
             isa<ConstantSDNode>(N.getOperand(0))) {
    uint64_t Imm = N.getConstantOperandVal(0);
    // If we are shifting by N-X where N == 0 mod Size, then just shift by -X to
    // generate a NEG instead of a SUB of a constant.
    if (Imm != 0 && Imm % ShiftWidth == 0) {
      SDLoc DL(N);
      EVT VT = N.getValueType();
      SDValue Zero =
          CurDAG->getCopyFromReg(CurDAG->getEntryNode(), DL, LoongArch::R0, VT);
      unsigned NegOpc = VT == MVT::i64 ? LoongArch::SUB_D : LoongArch::SUB_W;
      MachineSDNode *Neg =
          CurDAG->getMachineNode(NegOpc, DL, VT, Zero, N.getOperand(1));
      ShAmt = SDValue(Neg, 0);
      return true;
    }
  }

  ShAmt = N;
  return true;
}

bool LoongArchDAGToDAGISel::selectSExti32(SDValue N, SDValue &Val) {
  if (N.getOpcode() == ISD::SIGN_EXTEND_INREG &&
      cast<VTSDNode>(N.getOperand(1))->getVT() == MVT::i32) {
    Val = N.getOperand(0);
    return true;
  }
  if (N.getOpcode() == LoongArchISD::BSTRPICK &&
      N.getConstantOperandVal(1) < UINT64_C(0X1F) &&
      N.getConstantOperandVal(2) == UINT64_C(0)) {
    Val = N;
    return true;
  }
  MVT VT = N.getSimpleValueType();
  if (CurDAG->ComputeNumSignBits(N) > (VT.getSizeInBits() - 32)) {
    Val = N;
    return true;
  }

  return false;
}

bool LoongArchDAGToDAGISel::selectZExti32(SDValue N, SDValue &Val) {
  if (N.getOpcode() == ISD::AND) {
    auto *C = dyn_cast<ConstantSDNode>(N.getOperand(1));
    if (C && C->getZExtValue() == UINT64_C(0xFFFFFFFF)) {
      Val = N.getOperand(0);
      return true;
    }
  }
  MVT VT = N.getSimpleValueType();
  APInt Mask = APInt::getHighBitsSet(VT.getSizeInBits(), 32);
  if (CurDAG->MaskedValueIsZero(N, Mask)) {
    Val = N;
    return true;
  }

  return false;
}

bool LoongArchDAGToDAGISel::selectVSplat(SDNode *N, APInt &Imm,
                                         unsigned MinSizeInBits) const {
  if (!Subtarget->hasExtLSX())
    return false;

  BuildVectorSDNode *Node = dyn_cast<BuildVectorSDNode>(N);

  if (!Node)
    return false;

  APInt SplatValue, SplatUndef;
  unsigned SplatBitSize;
  bool HasAnyUndefs;

  if (!Node->isConstantSplat(SplatValue, SplatUndef, SplatBitSize, HasAnyUndefs,
                             MinSizeInBits, /*IsBigEndian=*/false))
    return false;

  Imm = SplatValue;

  return true;
}

template <unsigned ImmBitSize, bool IsSigned>
bool LoongArchDAGToDAGISel::selectVSplatImm(SDValue N, SDValue &SplatVal) {
  APInt ImmValue;
  EVT EltTy = N->getValueType(0).getVectorElementType();

  if (N->getOpcode() == ISD::BITCAST)
    N = N->getOperand(0);

  if (selectVSplat(N.getNode(), ImmValue, EltTy.getSizeInBits()) &&
      ImmValue.getBitWidth() == EltTy.getSizeInBits()) {
    if (IsSigned && ImmValue.isSignedIntN(ImmBitSize)) {
      SplatVal = CurDAG->getTargetConstant(ImmValue.getSExtValue(), SDLoc(N),
                                           Subtarget->getGRLenVT());
      return true;
    }
    if (!IsSigned && ImmValue.isIntN(ImmBitSize)) {
      SplatVal = CurDAG->getTargetConstant(ImmValue.getZExtValue(), SDLoc(N),
                                           Subtarget->getGRLenVT());
      return true;
    }
  }

  return false;
}

bool LoongArchDAGToDAGISel::selectVSplatUimmInvPow2(SDValue N,
                                                    SDValue &SplatImm) const {
  APInt ImmValue;
  EVT EltTy = N->getValueType(0).getVectorElementType();

  if (N->getOpcode() == ISD::BITCAST)
    N = N->getOperand(0);

  if (selectVSplat(N.getNode(), ImmValue, EltTy.getSizeInBits()) &&
      ImmValue.getBitWidth() == EltTy.getSizeInBits()) {
    int32_t Log2 = (~ImmValue).exactLogBase2();

    if (Log2 != -1) {
      SplatImm = CurDAG->getTargetConstant(Log2, SDLoc(N), EltTy);
      return true;
    }
  }

  return false;
}

bool LoongArchDAGToDAGISel::selectVSplatUimmPow2(SDValue N,
                                                 SDValue &SplatImm) const {
  APInt ImmValue;
  EVT EltTy = N->getValueType(0).getVectorElementType();

  if (N->getOpcode() == ISD::BITCAST)
    N = N->getOperand(0);

  if (selectVSplat(N.getNode(), ImmValue, EltTy.getSizeInBits()) &&
      ImmValue.getBitWidth() == EltTy.getSizeInBits()) {
    int32_t Log2 = ImmValue.exactLogBase2();

    if (Log2 != -1) {
      SplatImm = CurDAG->getTargetConstant(Log2, SDLoc(N), EltTy);
      return true;
    }
  }

  return false;
}

// This pass converts a legalized DAG into a LoongArch-specific DAG, ready
// for instruction scheduling.
FunctionPass *llvm::createLoongArchISelDag(LoongArchTargetMachine &TM) {
  return new LoongArchDAGToDAGISelLegacy(TM);
}
