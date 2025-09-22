//===-- AArch64ISelDAGToDAG.cpp - A dag to dag inst selector for AArch64 --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines an instruction selector for the AArch64 target.
//
//===----------------------------------------------------------------------===//

#include "AArch64MachineFunctionInfo.h"
#include "AArch64TargetMachine.h"
#include "MCTargetDesc/AArch64AddressingModes.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/SelectionDAGISel.h"
#include "llvm/IR/Function.h" // To access function attributes.
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAArch64.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "aarch64-isel"
#define PASS_NAME "AArch64 Instruction Selection"

//===--------------------------------------------------------------------===//
/// AArch64DAGToDAGISel - AArch64 specific code to select AArch64 machine
/// instructions for SelectionDAG operations.
///
namespace {

class AArch64DAGToDAGISel : public SelectionDAGISel {

  /// Subtarget - Keep a pointer to the AArch64Subtarget around so that we can
  /// make the right decision when generating code for different targets.
  const AArch64Subtarget *Subtarget;

public:
  AArch64DAGToDAGISel() = delete;

  explicit AArch64DAGToDAGISel(AArch64TargetMachine &tm,
                               CodeGenOptLevel OptLevel)
      : SelectionDAGISel(tm, OptLevel), Subtarget(nullptr) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    Subtarget = &MF.getSubtarget<AArch64Subtarget>();
    return SelectionDAGISel::runOnMachineFunction(MF);
  }

  void Select(SDNode *Node) override;

  /// SelectInlineAsmMemoryOperand - Implement addressing mode selection for
  /// inline asm expressions.
  bool SelectInlineAsmMemoryOperand(const SDValue &Op,
                                    InlineAsm::ConstraintCode ConstraintID,
                                    std::vector<SDValue> &OutOps) override;

  template <signed Low, signed High, signed Scale>
  bool SelectRDVLImm(SDValue N, SDValue &Imm);

  bool SelectArithExtendedRegister(SDValue N, SDValue &Reg, SDValue &Shift);
  bool SelectArithUXTXRegister(SDValue N, SDValue &Reg, SDValue &Shift);
  bool SelectArithImmed(SDValue N, SDValue &Val, SDValue &Shift);
  bool SelectNegArithImmed(SDValue N, SDValue &Val, SDValue &Shift);
  bool SelectArithShiftedRegister(SDValue N, SDValue &Reg, SDValue &Shift) {
    return SelectShiftedRegister(N, false, Reg, Shift);
  }
  bool SelectLogicalShiftedRegister(SDValue N, SDValue &Reg, SDValue &Shift) {
    return SelectShiftedRegister(N, true, Reg, Shift);
  }
  bool SelectAddrModeIndexed7S8(SDValue N, SDValue &Base, SDValue &OffImm) {
    return SelectAddrModeIndexed7S(N, 1, Base, OffImm);
  }
  bool SelectAddrModeIndexed7S16(SDValue N, SDValue &Base, SDValue &OffImm) {
    return SelectAddrModeIndexed7S(N, 2, Base, OffImm);
  }
  bool SelectAddrModeIndexed7S32(SDValue N, SDValue &Base, SDValue &OffImm) {
    return SelectAddrModeIndexed7S(N, 4, Base, OffImm);
  }
  bool SelectAddrModeIndexed7S64(SDValue N, SDValue &Base, SDValue &OffImm) {
    return SelectAddrModeIndexed7S(N, 8, Base, OffImm);
  }
  bool SelectAddrModeIndexed7S128(SDValue N, SDValue &Base, SDValue &OffImm) {
    return SelectAddrModeIndexed7S(N, 16, Base, OffImm);
  }
  bool SelectAddrModeIndexedS9S128(SDValue N, SDValue &Base, SDValue &OffImm) {
    return SelectAddrModeIndexedBitWidth(N, true, 9, 16, Base, OffImm);
  }
  bool SelectAddrModeIndexedU6S128(SDValue N, SDValue &Base, SDValue &OffImm) {
    return SelectAddrModeIndexedBitWidth(N, false, 6, 16, Base, OffImm);
  }
  bool SelectAddrModeIndexed8(SDValue N, SDValue &Base, SDValue &OffImm) {
    return SelectAddrModeIndexed(N, 1, Base, OffImm);
  }
  bool SelectAddrModeIndexed16(SDValue N, SDValue &Base, SDValue &OffImm) {
    return SelectAddrModeIndexed(N, 2, Base, OffImm);
  }
  bool SelectAddrModeIndexed32(SDValue N, SDValue &Base, SDValue &OffImm) {
    return SelectAddrModeIndexed(N, 4, Base, OffImm);
  }
  bool SelectAddrModeIndexed64(SDValue N, SDValue &Base, SDValue &OffImm) {
    return SelectAddrModeIndexed(N, 8, Base, OffImm);
  }
  bool SelectAddrModeIndexed128(SDValue N, SDValue &Base, SDValue &OffImm) {
    return SelectAddrModeIndexed(N, 16, Base, OffImm);
  }
  bool SelectAddrModeUnscaled8(SDValue N, SDValue &Base, SDValue &OffImm) {
    return SelectAddrModeUnscaled(N, 1, Base, OffImm);
  }
  bool SelectAddrModeUnscaled16(SDValue N, SDValue &Base, SDValue &OffImm) {
    return SelectAddrModeUnscaled(N, 2, Base, OffImm);
  }
  bool SelectAddrModeUnscaled32(SDValue N, SDValue &Base, SDValue &OffImm) {
    return SelectAddrModeUnscaled(N, 4, Base, OffImm);
  }
  bool SelectAddrModeUnscaled64(SDValue N, SDValue &Base, SDValue &OffImm) {
    return SelectAddrModeUnscaled(N, 8, Base, OffImm);
  }
  bool SelectAddrModeUnscaled128(SDValue N, SDValue &Base, SDValue &OffImm) {
    return SelectAddrModeUnscaled(N, 16, Base, OffImm);
  }
  template <unsigned Size, unsigned Max>
  bool SelectAddrModeIndexedUImm(SDValue N, SDValue &Base, SDValue &OffImm) {
    // Test if there is an appropriate addressing mode and check if the
    // immediate fits.
    bool Found = SelectAddrModeIndexed(N, Size, Base, OffImm);
    if (Found) {
      if (auto *CI = dyn_cast<ConstantSDNode>(OffImm)) {
        int64_t C = CI->getSExtValue();
        if (C <= Max)
          return true;
      }
    }

    // Otherwise, base only, materialize address in register.
    Base = N;
    OffImm = CurDAG->getTargetConstant(0, SDLoc(N), MVT::i64);
    return true;
  }

  template<int Width>
  bool SelectAddrModeWRO(SDValue N, SDValue &Base, SDValue &Offset,
                         SDValue &SignExtend, SDValue &DoShift) {
    return SelectAddrModeWRO(N, Width / 8, Base, Offset, SignExtend, DoShift);
  }

  template<int Width>
  bool SelectAddrModeXRO(SDValue N, SDValue &Base, SDValue &Offset,
                         SDValue &SignExtend, SDValue &DoShift) {
    return SelectAddrModeXRO(N, Width / 8, Base, Offset, SignExtend, DoShift);
  }

  bool SelectExtractHigh(SDValue N, SDValue &Res) {
    if (Subtarget->isLittleEndian() && N->getOpcode() == ISD::BITCAST)
      N = N->getOperand(0);
    if (N->getOpcode() != ISD::EXTRACT_SUBVECTOR ||
        !isa<ConstantSDNode>(N->getOperand(1)))
      return false;
    EVT VT = N->getValueType(0);
    EVT LVT = N->getOperand(0).getValueType();
    unsigned Index = N->getConstantOperandVal(1);
    if (!VT.is64BitVector() || !LVT.is128BitVector() ||
        Index != VT.getVectorNumElements())
      return false;
    Res = N->getOperand(0);
    return true;
  }

  bool SelectRoundingVLShr(SDValue N, SDValue &Res1, SDValue &Res2) {
    if (N.getOpcode() != AArch64ISD::VLSHR)
      return false;
    SDValue Op = N->getOperand(0);
    EVT VT = Op.getValueType();
    unsigned ShtAmt = N->getConstantOperandVal(1);
    if (ShtAmt > VT.getScalarSizeInBits() / 2 || Op.getOpcode() != ISD::ADD)
      return false;

    APInt Imm;
    if (Op.getOperand(1).getOpcode() == AArch64ISD::MOVIshift)
      Imm = APInt(VT.getScalarSizeInBits(),
                  Op.getOperand(1).getConstantOperandVal(0)
                      << Op.getOperand(1).getConstantOperandVal(1));
    else if (Op.getOperand(1).getOpcode() == AArch64ISD::DUP &&
             isa<ConstantSDNode>(Op.getOperand(1).getOperand(0)))
      Imm = APInt(VT.getScalarSizeInBits(),
                  Op.getOperand(1).getConstantOperandVal(0));
    else
      return false;

    if (Imm != 1ULL << (ShtAmt - 1))
      return false;

    Res1 = Op.getOperand(0);
    Res2 = CurDAG->getTargetConstant(ShtAmt, SDLoc(N), MVT::i32);
    return true;
  }

  bool SelectDupZeroOrUndef(SDValue N) {
    switch(N->getOpcode()) {
    case ISD::UNDEF:
      return true;
    case AArch64ISD::DUP:
    case ISD::SPLAT_VECTOR: {
      auto Opnd0 = N->getOperand(0);
      if (isNullConstant(Opnd0))
        return true;
      if (isNullFPConstant(Opnd0))
        return true;
      break;
    }
    default:
      break;
    }

    return false;
  }

  bool SelectDupZero(SDValue N) {
    switch(N->getOpcode()) {
    case AArch64ISD::DUP:
    case ISD::SPLAT_VECTOR: {
      auto Opnd0 = N->getOperand(0);
      if (isNullConstant(Opnd0))
        return true;
      if (isNullFPConstant(Opnd0))
        return true;
      break;
    }
    }

    return false;
  }

  bool SelectDupNegativeZero(SDValue N) {
    switch(N->getOpcode()) {
    case AArch64ISD::DUP:
    case ISD::SPLAT_VECTOR: {
      ConstantFPSDNode *Const = dyn_cast<ConstantFPSDNode>(N->getOperand(0));
      return Const && Const->isZero() && Const->isNegative();
    }
    }

    return false;
  }

  template<MVT::SimpleValueType VT>
  bool SelectSVEAddSubImm(SDValue N, SDValue &Imm, SDValue &Shift) {
    return SelectSVEAddSubImm(N, VT, Imm, Shift);
  }

  template <MVT::SimpleValueType VT, bool Negate>
  bool SelectSVEAddSubSSatImm(SDValue N, SDValue &Imm, SDValue &Shift) {
    return SelectSVEAddSubSSatImm(N, VT, Imm, Shift, Negate);
  }

  template <MVT::SimpleValueType VT>
  bool SelectSVECpyDupImm(SDValue N, SDValue &Imm, SDValue &Shift) {
    return SelectSVECpyDupImm(N, VT, Imm, Shift);
  }

  template <MVT::SimpleValueType VT, bool Invert = false>
  bool SelectSVELogicalImm(SDValue N, SDValue &Imm) {
    return SelectSVELogicalImm(N, VT, Imm, Invert);
  }

  template <MVT::SimpleValueType VT>
  bool SelectSVEArithImm(SDValue N, SDValue &Imm) {
    return SelectSVEArithImm(N, VT, Imm);
  }

  template <unsigned Low, unsigned High, bool AllowSaturation = false>
  bool SelectSVEShiftImm(SDValue N, SDValue &Imm) {
    return SelectSVEShiftImm(N, Low, High, AllowSaturation, Imm);
  }

  bool SelectSVEShiftSplatImmR(SDValue N, SDValue &Imm) {
    if (N->getOpcode() != ISD::SPLAT_VECTOR)
      return false;

    EVT EltVT = N->getValueType(0).getVectorElementType();
    return SelectSVEShiftImm(N->getOperand(0), /* Low */ 1,
                             /* High */ EltVT.getFixedSizeInBits(),
                             /* AllowSaturation */ true, Imm);
  }

  // Returns a suitable CNT/INC/DEC/RDVL multiplier to calculate VSCALE*N.
  template<signed Min, signed Max, signed Scale, bool Shift>
  bool SelectCntImm(SDValue N, SDValue &Imm) {
    if (!isa<ConstantSDNode>(N))
      return false;

    int64_t MulImm = cast<ConstantSDNode>(N)->getSExtValue();
    if (Shift)
      MulImm = 1LL << MulImm;

    if ((MulImm % std::abs(Scale)) != 0)
      return false;

    MulImm /= Scale;
    if ((MulImm >= Min) && (MulImm <= Max)) {
      Imm = CurDAG->getTargetConstant(MulImm, SDLoc(N), MVT::i32);
      return true;
    }

    return false;
  }

  template <signed Max, signed Scale>
  bool SelectEXTImm(SDValue N, SDValue &Imm) {
    if (!isa<ConstantSDNode>(N))
      return false;

    int64_t MulImm = cast<ConstantSDNode>(N)->getSExtValue();

    if (MulImm >= 0 && MulImm <= Max) {
      MulImm *= Scale;
      Imm = CurDAG->getTargetConstant(MulImm, SDLoc(N), MVT::i32);
      return true;
    }

    return false;
  }

  template <unsigned BaseReg, unsigned Max>
  bool ImmToReg(SDValue N, SDValue &Imm) {
    if (auto *CI = dyn_cast<ConstantSDNode>(N)) {
      uint64_t C = CI->getZExtValue();

      if (C > Max)
        return false;

      Imm = CurDAG->getRegister(BaseReg + C, MVT::Other);
      return true;
    }
    return false;
  }

  /// Form sequences of consecutive 64/128-bit registers for use in NEON
  /// instructions making use of a vector-list (e.g. ldN, tbl). Vecs must have
  /// between 1 and 4 elements. If it contains a single element that is returned
  /// unchanged; otherwise a REG_SEQUENCE value is returned.
  SDValue createDTuple(ArrayRef<SDValue> Vecs);
  SDValue createQTuple(ArrayRef<SDValue> Vecs);
  // Form a sequence of SVE registers for instructions using list of vectors,
  // e.g. structured loads and stores (ldN, stN).
  SDValue createZTuple(ArrayRef<SDValue> Vecs);

  // Similar to above, except the register must start at a multiple of the
  // tuple, e.g. z2 for a 2-tuple, or z8 for a 4-tuple.
  SDValue createZMulTuple(ArrayRef<SDValue> Regs);

  /// Generic helper for the createDTuple/createQTuple
  /// functions. Those should almost always be called instead.
  SDValue createTuple(ArrayRef<SDValue> Vecs, const unsigned RegClassIDs[],
                      const unsigned SubRegs[]);

  void SelectTable(SDNode *N, unsigned NumVecs, unsigned Opc, bool isExt);

  bool tryIndexedLoad(SDNode *N);

  void SelectPtrauthAuth(SDNode *N);
  void SelectPtrauthResign(SDNode *N);

  bool trySelectStackSlotTagP(SDNode *N);
  void SelectTagP(SDNode *N);

  void SelectLoad(SDNode *N, unsigned NumVecs, unsigned Opc,
                     unsigned SubRegIdx);
  void SelectPostLoad(SDNode *N, unsigned NumVecs, unsigned Opc,
                         unsigned SubRegIdx);
  void SelectLoadLane(SDNode *N, unsigned NumVecs, unsigned Opc);
  void SelectPostLoadLane(SDNode *N, unsigned NumVecs, unsigned Opc);
  void SelectPredicatedLoad(SDNode *N, unsigned NumVecs, unsigned Scale,
                            unsigned Opc_rr, unsigned Opc_ri,
                            bool IsIntr = false);
  void SelectContiguousMultiVectorLoad(SDNode *N, unsigned NumVecs,
                                       unsigned Scale, unsigned Opc_ri,
                                       unsigned Opc_rr);
  void SelectDestructiveMultiIntrinsic(SDNode *N, unsigned NumVecs,
                                       bool IsZmMulti, unsigned Opcode,
                                       bool HasPred = false);
  void SelectPExtPair(SDNode *N, unsigned Opc);
  void SelectWhilePair(SDNode *N, unsigned Opc);
  void SelectCVTIntrinsic(SDNode *N, unsigned NumVecs, unsigned Opcode);
  void SelectClamp(SDNode *N, unsigned NumVecs, unsigned Opcode);
  void SelectUnaryMultiIntrinsic(SDNode *N, unsigned NumOutVecs,
                                 bool IsTupleInput, unsigned Opc);
  void SelectFrintFromVT(SDNode *N, unsigned NumVecs, unsigned Opcode);

  template <unsigned MaxIdx, unsigned Scale>
  void SelectMultiVectorMove(SDNode *N, unsigned NumVecs, unsigned BaseReg,
                             unsigned Op);
  void SelectMultiVectorMoveZ(SDNode *N, unsigned NumVecs,
                              unsigned Op, unsigned MaxIdx, unsigned Scale,
                              unsigned BaseReg = 0);
  bool SelectAddrModeFrameIndexSVE(SDValue N, SDValue &Base, SDValue &OffImm);
  /// SVE Reg+Imm addressing mode.
  template <int64_t Min, int64_t Max>
  bool SelectAddrModeIndexedSVE(SDNode *Root, SDValue N, SDValue &Base,
                                SDValue &OffImm);
  /// SVE Reg+Reg address mode.
  template <unsigned Scale>
  bool SelectSVERegRegAddrMode(SDValue N, SDValue &Base, SDValue &Offset) {
    return SelectSVERegRegAddrMode(N, Scale, Base, Offset);
  }

  void SelectMultiVectorLuti(SDNode *Node, unsigned NumOutVecs, unsigned Opc,
                             uint32_t MaxImm);

  template <unsigned MaxIdx, unsigned Scale>
  bool SelectSMETileSlice(SDValue N, SDValue &Vector, SDValue &Offset) {
    return SelectSMETileSlice(N, MaxIdx, Vector, Offset, Scale);
  }

  void SelectStore(SDNode *N, unsigned NumVecs, unsigned Opc);
  void SelectPostStore(SDNode *N, unsigned NumVecs, unsigned Opc);
  void SelectStoreLane(SDNode *N, unsigned NumVecs, unsigned Opc);
  void SelectPostStoreLane(SDNode *N, unsigned NumVecs, unsigned Opc);
  void SelectPredicatedStore(SDNode *N, unsigned NumVecs, unsigned Scale,
                             unsigned Opc_rr, unsigned Opc_ri);
  std::tuple<unsigned, SDValue, SDValue>
  findAddrModeSVELoadStore(SDNode *N, unsigned Opc_rr, unsigned Opc_ri,
                           const SDValue &OldBase, const SDValue &OldOffset,
                           unsigned Scale);

  bool tryBitfieldExtractOp(SDNode *N);
  bool tryBitfieldExtractOpFromSExt(SDNode *N);
  bool tryBitfieldInsertOp(SDNode *N);
  bool tryBitfieldInsertInZeroOp(SDNode *N);
  bool tryShiftAmountMod(SDNode *N);

  bool tryReadRegister(SDNode *N);
  bool tryWriteRegister(SDNode *N);

  bool trySelectCastFixedLengthToScalableVector(SDNode *N);
  bool trySelectCastScalableToFixedLengthVector(SDNode *N);

  bool trySelectXAR(SDNode *N);

// Include the pieces autogenerated from the target description.
#include "AArch64GenDAGISel.inc"

private:
  bool SelectShiftedRegister(SDValue N, bool AllowROR, SDValue &Reg,
                             SDValue &Shift);
  bool SelectShiftedRegisterFromAnd(SDValue N, SDValue &Reg, SDValue &Shift);
  bool SelectAddrModeIndexed7S(SDValue N, unsigned Size, SDValue &Base,
                               SDValue &OffImm) {
    return SelectAddrModeIndexedBitWidth(N, true, 7, Size, Base, OffImm);
  }
  bool SelectAddrModeIndexedBitWidth(SDValue N, bool IsSignedImm, unsigned BW,
                                     unsigned Size, SDValue &Base,
                                     SDValue &OffImm);
  bool SelectAddrModeIndexed(SDValue N, unsigned Size, SDValue &Base,
                             SDValue &OffImm);
  bool SelectAddrModeUnscaled(SDValue N, unsigned Size, SDValue &Base,
                              SDValue &OffImm);
  bool SelectAddrModeWRO(SDValue N, unsigned Size, SDValue &Base,
                         SDValue &Offset, SDValue &SignExtend,
                         SDValue &DoShift);
  bool SelectAddrModeXRO(SDValue N, unsigned Size, SDValue &Base,
                         SDValue &Offset, SDValue &SignExtend,
                         SDValue &DoShift);
  bool isWorthFoldingALU(SDValue V, bool LSL = false) const;
  bool isWorthFoldingAddr(SDValue V, unsigned Size) const;
  bool SelectExtendedSHL(SDValue N, unsigned Size, bool WantExtend,
                         SDValue &Offset, SDValue &SignExtend);

  template<unsigned RegWidth>
  bool SelectCVTFixedPosOperand(SDValue N, SDValue &FixedPos) {
    return SelectCVTFixedPosOperand(N, FixedPos, RegWidth);
  }

  bool SelectCVTFixedPosOperand(SDValue N, SDValue &FixedPos, unsigned Width);

  template<unsigned RegWidth>
  bool SelectCVTFixedPosRecipOperand(SDValue N, SDValue &FixedPos) {
    return SelectCVTFixedPosRecipOperand(N, FixedPos, RegWidth);
  }

  bool SelectCVTFixedPosRecipOperand(SDValue N, SDValue &FixedPos,
                                     unsigned Width);

  bool SelectCMP_SWAP(SDNode *N);

  bool SelectSVEAddSubImm(SDValue N, MVT VT, SDValue &Imm, SDValue &Shift);
  bool SelectSVEAddSubSSatImm(SDValue N, MVT VT, SDValue &Imm, SDValue &Shift,
                              bool Negate);
  bool SelectSVECpyDupImm(SDValue N, MVT VT, SDValue &Imm, SDValue &Shift);
  bool SelectSVELogicalImm(SDValue N, MVT VT, SDValue &Imm, bool Invert);

  bool SelectSVESignedArithImm(SDValue N, SDValue &Imm);
  bool SelectSVEShiftImm(SDValue N, uint64_t Low, uint64_t High,
                         bool AllowSaturation, SDValue &Imm);

  bool SelectSVEArithImm(SDValue N, MVT VT, SDValue &Imm);
  bool SelectSVERegRegAddrMode(SDValue N, unsigned Scale, SDValue &Base,
                               SDValue &Offset);
  bool SelectSMETileSlice(SDValue N, unsigned MaxSize, SDValue &Vector,
                          SDValue &Offset, unsigned Scale = 1);

  bool SelectAllActivePredicate(SDValue N);
  bool SelectAnyPredicate(SDValue N);
};

class AArch64DAGToDAGISelLegacy : public SelectionDAGISelLegacy {
public:
  static char ID;
  explicit AArch64DAGToDAGISelLegacy(AArch64TargetMachine &tm,
                                     CodeGenOptLevel OptLevel)
      : SelectionDAGISelLegacy(
            ID, std::make_unique<AArch64DAGToDAGISel>(tm, OptLevel)) {}
};
} // end anonymous namespace

char AArch64DAGToDAGISelLegacy::ID = 0;

INITIALIZE_PASS(AArch64DAGToDAGISelLegacy, DEBUG_TYPE, PASS_NAME, false, false)

/// isIntImmediate - This method tests to see if the node is a constant
/// operand. If so Imm will receive the 32-bit value.
static bool isIntImmediate(const SDNode *N, uint64_t &Imm) {
  if (const ConstantSDNode *C = dyn_cast<const ConstantSDNode>(N)) {
    Imm = C->getZExtValue();
    return true;
  }
  return false;
}

// isIntImmediate - This method tests to see if a constant operand.
// If so Imm will receive the value.
static bool isIntImmediate(SDValue N, uint64_t &Imm) {
  return isIntImmediate(N.getNode(), Imm);
}

// isOpcWithIntImmediate - This method tests to see if the node is a specific
// opcode and that it has a immediate integer right operand.
// If so Imm will receive the 32 bit value.
static bool isOpcWithIntImmediate(const SDNode *N, unsigned Opc,
                                  uint64_t &Imm) {
  return N->getOpcode() == Opc &&
         isIntImmediate(N->getOperand(1).getNode(), Imm);
}

// isIntImmediateEq - This method tests to see if N is a constant operand that
// is equivalent to 'ImmExpected'.
#ifndef NDEBUG
static bool isIntImmediateEq(SDValue N, const uint64_t ImmExpected) {
  uint64_t Imm;
  if (!isIntImmediate(N.getNode(), Imm))
    return false;
  return Imm == ImmExpected;
}
#endif

bool AArch64DAGToDAGISel::SelectInlineAsmMemoryOperand(
    const SDValue &Op, const InlineAsm::ConstraintCode ConstraintID,
    std::vector<SDValue> &OutOps) {
  switch(ConstraintID) {
  default:
    llvm_unreachable("Unexpected asm memory constraint");
  case InlineAsm::ConstraintCode::m:
  case InlineAsm::ConstraintCode::o:
  case InlineAsm::ConstraintCode::Q:
    // We need to make sure that this one operand does not end up in XZR, thus
    // require the address to be in a PointerRegClass register.
    const TargetRegisterInfo *TRI = Subtarget->getRegisterInfo();
    const TargetRegisterClass *TRC = TRI->getPointerRegClass(*MF);
    SDLoc dl(Op);
    SDValue RC = CurDAG->getTargetConstant(TRC->getID(), dl, MVT::i64);
    SDValue NewOp =
        SDValue(CurDAG->getMachineNode(TargetOpcode::COPY_TO_REGCLASS,
                                       dl, Op.getValueType(),
                                       Op, RC), 0);
    OutOps.push_back(NewOp);
    return false;
  }
  return true;
}

/// SelectArithImmed - Select an immediate value that can be represented as
/// a 12-bit value shifted left by either 0 or 12.  If so, return true with
/// Val set to the 12-bit value and Shift set to the shifter operand.
bool AArch64DAGToDAGISel::SelectArithImmed(SDValue N, SDValue &Val,
                                           SDValue &Shift) {
  // This function is called from the addsub_shifted_imm ComplexPattern,
  // which lists [imm] as the list of opcode it's interested in, however
  // we still need to check whether the operand is actually an immediate
  // here because the ComplexPattern opcode list is only used in
  // root-level opcode matching.
  if (!isa<ConstantSDNode>(N.getNode()))
    return false;

  uint64_t Immed = N.getNode()->getAsZExtVal();
  unsigned ShiftAmt;

  if (Immed >> 12 == 0) {
    ShiftAmt = 0;
  } else if ((Immed & 0xfff) == 0 && Immed >> 24 == 0) {
    ShiftAmt = 12;
    Immed = Immed >> 12;
  } else
    return false;

  unsigned ShVal = AArch64_AM::getShifterImm(AArch64_AM::LSL, ShiftAmt);
  SDLoc dl(N);
  Val = CurDAG->getTargetConstant(Immed, dl, MVT::i32);
  Shift = CurDAG->getTargetConstant(ShVal, dl, MVT::i32);
  return true;
}

/// SelectNegArithImmed - As above, but negates the value before trying to
/// select it.
bool AArch64DAGToDAGISel::SelectNegArithImmed(SDValue N, SDValue &Val,
                                              SDValue &Shift) {
  // This function is called from the addsub_shifted_imm ComplexPattern,
  // which lists [imm] as the list of opcode it's interested in, however
  // we still need to check whether the operand is actually an immediate
  // here because the ComplexPattern opcode list is only used in
  // root-level opcode matching.
  if (!isa<ConstantSDNode>(N.getNode()))
    return false;

  // The immediate operand must be a 24-bit zero-extended immediate.
  uint64_t Immed = N.getNode()->getAsZExtVal();

  // This negation is almost always valid, but "cmp wN, #0" and "cmn wN, #0"
  // have the opposite effect on the C flag, so this pattern mustn't match under
  // those circumstances.
  if (Immed == 0)
    return false;

  if (N.getValueType() == MVT::i32)
    Immed = ~((uint32_t)Immed) + 1;
  else
    Immed = ~Immed + 1ULL;
  if (Immed & 0xFFFFFFFFFF000000ULL)
    return false;

  Immed &= 0xFFFFFFULL;
  return SelectArithImmed(CurDAG->getConstant(Immed, SDLoc(N), MVT::i32), Val,
                          Shift);
}

/// getShiftTypeForNode - Translate a shift node to the corresponding
/// ShiftType value.
static AArch64_AM::ShiftExtendType getShiftTypeForNode(SDValue N) {
  switch (N.getOpcode()) {
  default:
    return AArch64_AM::InvalidShiftExtend;
  case ISD::SHL:
    return AArch64_AM::LSL;
  case ISD::SRL:
    return AArch64_AM::LSR;
  case ISD::SRA:
    return AArch64_AM::ASR;
  case ISD::ROTR:
    return AArch64_AM::ROR;
  }
}

/// Determine whether it is worth it to fold SHL into the addressing
/// mode.
static bool isWorthFoldingSHL(SDValue V) {
  assert(V.getOpcode() == ISD::SHL && "invalid opcode");
  // It is worth folding logical shift of up to three places.
  auto *CSD = dyn_cast<ConstantSDNode>(V.getOperand(1));
  if (!CSD)
    return false;
  unsigned ShiftVal = CSD->getZExtValue();
  if (ShiftVal > 3)
    return false;

  // Check if this particular node is reused in any non-memory related
  // operation.  If yes, do not try to fold this node into the address
  // computation, since the computation will be kept.
  const SDNode *Node = V.getNode();
  for (SDNode *UI : Node->uses())
    if (!isa<MemSDNode>(*UI))
      for (SDNode *UII : UI->uses())
        if (!isa<MemSDNode>(*UII))
          return false;
  return true;
}

/// Determine whether it is worth to fold V into an extended register addressing
/// mode.
bool AArch64DAGToDAGISel::isWorthFoldingAddr(SDValue V, unsigned Size) const {
  // Trivial if we are optimizing for code size or if there is only
  // one use of the value.
  if (CurDAG->shouldOptForSize() || V.hasOneUse())
    return true;

  // If a subtarget has a slow shift, folding a shift into multiple loads
  // costs additional micro-ops.
  if (Subtarget->hasAddrLSLSlow14() && (Size == 2 || Size == 16))
    return false;

  // Check whether we're going to emit the address arithmetic anyway because
  // it's used by a non-address operation.
  if (V.getOpcode() == ISD::SHL && isWorthFoldingSHL(V))
    return true;
  if (V.getOpcode() == ISD::ADD) {
    const SDValue LHS = V.getOperand(0);
    const SDValue RHS = V.getOperand(1);
    if (LHS.getOpcode() == ISD::SHL && isWorthFoldingSHL(LHS))
      return true;
    if (RHS.getOpcode() == ISD::SHL && isWorthFoldingSHL(RHS))
      return true;
  }

  // It hurts otherwise, since the value will be reused.
  return false;
}

/// and (shl/srl/sra, x, c), mask --> shl (srl/sra, x, c1), c2
/// to select more shifted register
bool AArch64DAGToDAGISel::SelectShiftedRegisterFromAnd(SDValue N, SDValue &Reg,
                                                       SDValue &Shift) {
  EVT VT = N.getValueType();
  if (VT != MVT::i32 && VT != MVT::i64)
    return false;

  if (N->getOpcode() != ISD::AND || !N->hasOneUse())
    return false;
  SDValue LHS = N.getOperand(0);
  if (!LHS->hasOneUse())
    return false;

  unsigned LHSOpcode = LHS->getOpcode();
  if (LHSOpcode != ISD::SHL && LHSOpcode != ISD::SRL && LHSOpcode != ISD::SRA)
    return false;

  ConstantSDNode *ShiftAmtNode = dyn_cast<ConstantSDNode>(LHS.getOperand(1));
  if (!ShiftAmtNode)
    return false;

  uint64_t ShiftAmtC = ShiftAmtNode->getZExtValue();
  ConstantSDNode *RHSC = dyn_cast<ConstantSDNode>(N.getOperand(1));
  if (!RHSC)
    return false;

  APInt AndMask = RHSC->getAPIntValue();
  unsigned LowZBits, MaskLen;
  if (!AndMask.isShiftedMask(LowZBits, MaskLen))
    return false;

  unsigned BitWidth = N.getValueSizeInBits();
  SDLoc DL(LHS);
  uint64_t NewShiftC;
  unsigned NewShiftOp;
  if (LHSOpcode == ISD::SHL) {
    // LowZBits <= ShiftAmtC will fall into isBitfieldPositioningOp
    // BitWidth != LowZBits + MaskLen doesn't match the pattern
    if (LowZBits <= ShiftAmtC || (BitWidth != LowZBits + MaskLen))
      return false;

    NewShiftC = LowZBits - ShiftAmtC;
    NewShiftOp = VT == MVT::i64 ? AArch64::UBFMXri : AArch64::UBFMWri;
  } else {
    if (LowZBits == 0)
      return false;

    // NewShiftC >= BitWidth will fall into isBitfieldExtractOp
    NewShiftC = LowZBits + ShiftAmtC;
    if (NewShiftC >= BitWidth)
      return false;

    // SRA need all high bits
    if (LHSOpcode == ISD::SRA && (BitWidth != (LowZBits + MaskLen)))
      return false;

    // SRL high bits can be 0 or 1
    if (LHSOpcode == ISD::SRL && (BitWidth > (NewShiftC + MaskLen)))
      return false;

    if (LHSOpcode == ISD::SRL)
      NewShiftOp = VT == MVT::i64 ? AArch64::UBFMXri : AArch64::UBFMWri;
    else
      NewShiftOp = VT == MVT::i64 ? AArch64::SBFMXri : AArch64::SBFMWri;
  }

  assert(NewShiftC < BitWidth && "Invalid shift amount");
  SDValue NewShiftAmt = CurDAG->getTargetConstant(NewShiftC, DL, VT);
  SDValue BitWidthMinus1 = CurDAG->getTargetConstant(BitWidth - 1, DL, VT);
  Reg = SDValue(CurDAG->getMachineNode(NewShiftOp, DL, VT, LHS->getOperand(0),
                                       NewShiftAmt, BitWidthMinus1),
                0);
  unsigned ShVal = AArch64_AM::getShifterImm(AArch64_AM::LSL, LowZBits);
  Shift = CurDAG->getTargetConstant(ShVal, DL, MVT::i32);
  return true;
}

/// getExtendTypeForNode - Translate an extend node to the corresponding
/// ExtendType value.
static AArch64_AM::ShiftExtendType
getExtendTypeForNode(SDValue N, bool IsLoadStore = false) {
  if (N.getOpcode() == ISD::SIGN_EXTEND ||
      N.getOpcode() == ISD::SIGN_EXTEND_INREG) {
    EVT SrcVT;
    if (N.getOpcode() == ISD::SIGN_EXTEND_INREG)
      SrcVT = cast<VTSDNode>(N.getOperand(1))->getVT();
    else
      SrcVT = N.getOperand(0).getValueType();

    if (!IsLoadStore && SrcVT == MVT::i8)
      return AArch64_AM::SXTB;
    else if (!IsLoadStore && SrcVT == MVT::i16)
      return AArch64_AM::SXTH;
    else if (SrcVT == MVT::i32)
      return AArch64_AM::SXTW;
    assert(SrcVT != MVT::i64 && "extend from 64-bits?");

    return AArch64_AM::InvalidShiftExtend;
  } else if (N.getOpcode() == ISD::ZERO_EXTEND ||
             N.getOpcode() == ISD::ANY_EXTEND) {
    EVT SrcVT = N.getOperand(0).getValueType();
    if (!IsLoadStore && SrcVT == MVT::i8)
      return AArch64_AM::UXTB;
    else if (!IsLoadStore && SrcVT == MVT::i16)
      return AArch64_AM::UXTH;
    else if (SrcVT == MVT::i32)
      return AArch64_AM::UXTW;
    assert(SrcVT != MVT::i64 && "extend from 64-bits?");

    return AArch64_AM::InvalidShiftExtend;
  } else if (N.getOpcode() == ISD::AND) {
    ConstantSDNode *CSD = dyn_cast<ConstantSDNode>(N.getOperand(1));
    if (!CSD)
      return AArch64_AM::InvalidShiftExtend;
    uint64_t AndMask = CSD->getZExtValue();

    switch (AndMask) {
    default:
      return AArch64_AM::InvalidShiftExtend;
    case 0xFF:
      return !IsLoadStore ? AArch64_AM::UXTB : AArch64_AM::InvalidShiftExtend;
    case 0xFFFF:
      return !IsLoadStore ? AArch64_AM::UXTH : AArch64_AM::InvalidShiftExtend;
    case 0xFFFFFFFF:
      return AArch64_AM::UXTW;
    }
  }

  return AArch64_AM::InvalidShiftExtend;
}

/// Determine whether it is worth to fold V into an extended register of an
/// Add/Sub. LSL means we are folding into an `add w0, w1, w2, lsl #N`
/// instruction, and the shift should be treated as worth folding even if has
/// multiple uses.
bool AArch64DAGToDAGISel::isWorthFoldingALU(SDValue V, bool LSL) const {
  // Trivial if we are optimizing for code size or if there is only
  // one use of the value.
  if (CurDAG->shouldOptForSize() || V.hasOneUse())
    return true;

  // If a subtarget has a fastpath LSL we can fold a logical shift into
  // the add/sub and save a cycle.
  if (LSL && Subtarget->hasALULSLFast() && V.getOpcode() == ISD::SHL &&
      V.getConstantOperandVal(1) <= 4 &&
      getExtendTypeForNode(V.getOperand(0)) == AArch64_AM::InvalidShiftExtend)
    return true;

  // It hurts otherwise, since the value will be reused.
  return false;
}

/// SelectShiftedRegister - Select a "shifted register" operand.  If the value
/// is not shifted, set the Shift operand to default of "LSL 0".  The logical
/// instructions allow the shifted register to be rotated, but the arithmetic
/// instructions do not.  The AllowROR parameter specifies whether ROR is
/// supported.
bool AArch64DAGToDAGISel::SelectShiftedRegister(SDValue N, bool AllowROR,
                                                SDValue &Reg, SDValue &Shift) {
  if (SelectShiftedRegisterFromAnd(N, Reg, Shift))
    return true;

  AArch64_AM::ShiftExtendType ShType = getShiftTypeForNode(N);
  if (ShType == AArch64_AM::InvalidShiftExtend)
    return false;
  if (!AllowROR && ShType == AArch64_AM::ROR)
    return false;

  if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
    unsigned BitSize = N.getValueSizeInBits();
    unsigned Val = RHS->getZExtValue() & (BitSize - 1);
    unsigned ShVal = AArch64_AM::getShifterImm(ShType, Val);

    Reg = N.getOperand(0);
    Shift = CurDAG->getTargetConstant(ShVal, SDLoc(N), MVT::i32);
    return isWorthFoldingALU(N, true);
  }

  return false;
}

/// Instructions that accept extend modifiers like UXTW expect the register
/// being extended to be a GPR32, but the incoming DAG might be acting on a
/// GPR64 (either via SEXT_INREG or AND). Extract the appropriate low bits if
/// this is the case.
static SDValue narrowIfNeeded(SelectionDAG *CurDAG, SDValue N) {
  if (N.getValueType() == MVT::i32)
    return N;

  SDLoc dl(N);
  return CurDAG->getTargetExtractSubreg(AArch64::sub_32, dl, MVT::i32, N);
}

// Returns a suitable CNT/INC/DEC/RDVL multiplier to calculate VSCALE*N.
template<signed Low, signed High, signed Scale>
bool AArch64DAGToDAGISel::SelectRDVLImm(SDValue N, SDValue &Imm) {
  if (!isa<ConstantSDNode>(N))
    return false;

  int64_t MulImm = cast<ConstantSDNode>(N)->getSExtValue();
  if ((MulImm % std::abs(Scale)) == 0) {
    int64_t RDVLImm = MulImm / Scale;
    if ((RDVLImm >= Low) && (RDVLImm <= High)) {
      Imm = CurDAG->getTargetConstant(RDVLImm, SDLoc(N), MVT::i32);
      return true;
    }
  }

  return false;
}

/// SelectArithExtendedRegister - Select a "extended register" operand.  This
/// operand folds in an extend followed by an optional left shift.
bool AArch64DAGToDAGISel::SelectArithExtendedRegister(SDValue N, SDValue &Reg,
                                                      SDValue &Shift) {
  unsigned ShiftVal = 0;
  AArch64_AM::ShiftExtendType Ext;

  if (N.getOpcode() == ISD::SHL) {
    ConstantSDNode *CSD = dyn_cast<ConstantSDNode>(N.getOperand(1));
    if (!CSD)
      return false;
    ShiftVal = CSD->getZExtValue();
    if (ShiftVal > 4)
      return false;

    Ext = getExtendTypeForNode(N.getOperand(0));
    if (Ext == AArch64_AM::InvalidShiftExtend)
      return false;

    Reg = N.getOperand(0).getOperand(0);
  } else {
    Ext = getExtendTypeForNode(N);
    if (Ext == AArch64_AM::InvalidShiftExtend)
      return false;

    Reg = N.getOperand(0);

    // Don't match if free 32-bit -> 64-bit zext can be used instead. Use the
    // isDef32 as a heuristic for when the operand is likely to be a 32bit def.
    auto isDef32 = [](SDValue N) {
      unsigned Opc = N.getOpcode();
      return Opc != ISD::TRUNCATE && Opc != TargetOpcode::EXTRACT_SUBREG &&
             Opc != ISD::CopyFromReg && Opc != ISD::AssertSext &&
             Opc != ISD::AssertZext && Opc != ISD::AssertAlign &&
             Opc != ISD::FREEZE;
    };
    if (Ext == AArch64_AM::UXTW && Reg->getValueType(0).getSizeInBits() == 32 &&
        isDef32(Reg))
      return false;
  }

  // AArch64 mandates that the RHS of the operation must use the smallest
  // register class that could contain the size being extended from.  Thus,
  // if we're folding a (sext i8), we need the RHS to be a GPR32, even though
  // there might not be an actual 32-bit value in the program.  We can
  // (harmlessly) synthesize one by injected an EXTRACT_SUBREG here.
  assert(Ext != AArch64_AM::UXTX && Ext != AArch64_AM::SXTX);
  Reg = narrowIfNeeded(CurDAG, Reg);
  Shift = CurDAG->getTargetConstant(getArithExtendImm(Ext, ShiftVal), SDLoc(N),
                                    MVT::i32);
  return isWorthFoldingALU(N);
}

/// SelectArithUXTXRegister - Select a "UXTX register" operand. This
/// operand is refered by the instructions have SP operand
bool AArch64DAGToDAGISel::SelectArithUXTXRegister(SDValue N, SDValue &Reg,
                                                  SDValue &Shift) {
  unsigned ShiftVal = 0;
  AArch64_AM::ShiftExtendType Ext;

  if (N.getOpcode() != ISD::SHL)
    return false;

  ConstantSDNode *CSD = dyn_cast<ConstantSDNode>(N.getOperand(1));
  if (!CSD)
    return false;
  ShiftVal = CSD->getZExtValue();
  if (ShiftVal > 4)
    return false;

  Ext = AArch64_AM::UXTX;
  Reg = N.getOperand(0);
  Shift = CurDAG->getTargetConstant(getArithExtendImm(Ext, ShiftVal), SDLoc(N),
                                    MVT::i32);
  return isWorthFoldingALU(N);
}

/// If there's a use of this ADDlow that's not itself a load/store then we'll
/// need to create a real ADD instruction from it anyway and there's no point in
/// folding it into the mem op. Theoretically, it shouldn't matter, but there's
/// a single pseudo-instruction for an ADRP/ADD pair so over-aggressive folding
/// leads to duplicated ADRP instructions.
static bool isWorthFoldingADDlow(SDValue N) {
  for (auto *Use : N->uses()) {
    if (Use->getOpcode() != ISD::LOAD && Use->getOpcode() != ISD::STORE &&
        Use->getOpcode() != ISD::ATOMIC_LOAD &&
        Use->getOpcode() != ISD::ATOMIC_STORE)
      return false;

    // ldar and stlr have much more restrictive addressing modes (just a
    // register).
    if (isStrongerThanMonotonic(cast<MemSDNode>(Use)->getSuccessOrdering()))
      return false;
  }

  return true;
}

/// Check if the immediate offset is valid as a scaled immediate.
static bool isValidAsScaledImmediate(int64_t Offset, unsigned Range,
                                     unsigned Size) {
  if ((Offset & (Size - 1)) == 0 && Offset >= 0 &&
      Offset < (Range << Log2_32(Size)))
    return true;
  return false;
}

/// SelectAddrModeIndexedBitWidth - Select a "register plus scaled (un)signed BW-bit
/// immediate" address.  The "Size" argument is the size in bytes of the memory
/// reference, which determines the scale.
bool AArch64DAGToDAGISel::SelectAddrModeIndexedBitWidth(SDValue N, bool IsSignedImm,
                                                        unsigned BW, unsigned Size,
                                                        SDValue &Base,
                                                        SDValue &OffImm) {
  SDLoc dl(N);
  const DataLayout &DL = CurDAG->getDataLayout();
  const TargetLowering *TLI = getTargetLowering();
  if (N.getOpcode() == ISD::FrameIndex) {
    int FI = cast<FrameIndexSDNode>(N)->getIndex();
    Base = CurDAG->getTargetFrameIndex(FI, TLI->getPointerTy(DL));
    OffImm = CurDAG->getTargetConstant(0, dl, MVT::i64);
    return true;
  }

  // As opposed to the (12-bit) Indexed addressing mode below, the 7/9-bit signed
  // selected here doesn't support labels/immediates, only base+offset.
  if (CurDAG->isBaseWithConstantOffset(N)) {
    if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
      if (IsSignedImm) {
        int64_t RHSC = RHS->getSExtValue();
        unsigned Scale = Log2_32(Size);
        int64_t Range = 0x1LL << (BW - 1);

        if ((RHSC & (Size - 1)) == 0 && RHSC >= -(Range << Scale) &&
            RHSC < (Range << Scale)) {
          Base = N.getOperand(0);
          if (Base.getOpcode() == ISD::FrameIndex) {
            int FI = cast<FrameIndexSDNode>(Base)->getIndex();
            Base = CurDAG->getTargetFrameIndex(FI, TLI->getPointerTy(DL));
          }
          OffImm = CurDAG->getTargetConstant(RHSC >> Scale, dl, MVT::i64);
          return true;
        }
      } else {
        // unsigned Immediate
        uint64_t RHSC = RHS->getZExtValue();
        unsigned Scale = Log2_32(Size);
        uint64_t Range = 0x1ULL << BW;

        if ((RHSC & (Size - 1)) == 0 && RHSC < (Range << Scale)) {
          Base = N.getOperand(0);
          if (Base.getOpcode() == ISD::FrameIndex) {
            int FI = cast<FrameIndexSDNode>(Base)->getIndex();
            Base = CurDAG->getTargetFrameIndex(FI, TLI->getPointerTy(DL));
          }
          OffImm = CurDAG->getTargetConstant(RHSC >> Scale, dl, MVT::i64);
          return true;
        }
      }
    }
  }
  // Base only. The address will be materialized into a register before
  // the memory is accessed.
  //    add x0, Xbase, #offset
  //    stp x1, x2, [x0]
  Base = N;
  OffImm = CurDAG->getTargetConstant(0, dl, MVT::i64);
  return true;
}

/// SelectAddrModeIndexed - Select a "register plus scaled unsigned 12-bit
/// immediate" address.  The "Size" argument is the size in bytes of the memory
/// reference, which determines the scale.
bool AArch64DAGToDAGISel::SelectAddrModeIndexed(SDValue N, unsigned Size,
                                              SDValue &Base, SDValue &OffImm) {
  SDLoc dl(N);
  const DataLayout &DL = CurDAG->getDataLayout();
  const TargetLowering *TLI = getTargetLowering();
  if (N.getOpcode() == ISD::FrameIndex) {
    int FI = cast<FrameIndexSDNode>(N)->getIndex();
    Base = CurDAG->getTargetFrameIndex(FI, TLI->getPointerTy(DL));
    OffImm = CurDAG->getTargetConstant(0, dl, MVT::i64);
    return true;
  }

  if (N.getOpcode() == AArch64ISD::ADDlow && isWorthFoldingADDlow(N)) {
    GlobalAddressSDNode *GAN =
        dyn_cast<GlobalAddressSDNode>(N.getOperand(1).getNode());
    Base = N.getOperand(0);
    OffImm = N.getOperand(1);
    if (!GAN)
      return true;

    if (GAN->getOffset() % Size == 0 &&
        GAN->getGlobal()->getPointerAlignment(DL) >= Size)
      return true;
  }

  if (CurDAG->isBaseWithConstantOffset(N)) {
    if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
      int64_t RHSC = (int64_t)RHS->getZExtValue();
      unsigned Scale = Log2_32(Size);
      if (isValidAsScaledImmediate(RHSC, 0x1000, Size)) {
        Base = N.getOperand(0);
        if (Base.getOpcode() == ISD::FrameIndex) {
          int FI = cast<FrameIndexSDNode>(Base)->getIndex();
          Base = CurDAG->getTargetFrameIndex(FI, TLI->getPointerTy(DL));
        }
        OffImm = CurDAG->getTargetConstant(RHSC >> Scale, dl, MVT::i64);
        return true;
      }
    }
  }

  // Before falling back to our general case, check if the unscaled
  // instructions can handle this. If so, that's preferable.
  if (SelectAddrModeUnscaled(N, Size, Base, OffImm))
    return false;

  // Base only. The address will be materialized into a register before
  // the memory is accessed.
  //    add x0, Xbase, #offset
  //    ldr x0, [x0]
  Base = N;
  OffImm = CurDAG->getTargetConstant(0, dl, MVT::i64);
  return true;
}

/// SelectAddrModeUnscaled - Select a "register plus unscaled signed 9-bit
/// immediate" address.  This should only match when there is an offset that
/// is not valid for a scaled immediate addressing mode.  The "Size" argument
/// is the size in bytes of the memory reference, which is needed here to know
/// what is valid for a scaled immediate.
bool AArch64DAGToDAGISel::SelectAddrModeUnscaled(SDValue N, unsigned Size,
                                                 SDValue &Base,
                                                 SDValue &OffImm) {
  if (!CurDAG->isBaseWithConstantOffset(N))
    return false;
  if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
    int64_t RHSC = RHS->getSExtValue();
    if (RHSC >= -256 && RHSC < 256) {
      Base = N.getOperand(0);
      if (Base.getOpcode() == ISD::FrameIndex) {
        int FI = cast<FrameIndexSDNode>(Base)->getIndex();
        const TargetLowering *TLI = getTargetLowering();
        Base = CurDAG->getTargetFrameIndex(
            FI, TLI->getPointerTy(CurDAG->getDataLayout()));
      }
      OffImm = CurDAG->getTargetConstant(RHSC, SDLoc(N), MVT::i64);
      return true;
    }
  }
  return false;
}

static SDValue Widen(SelectionDAG *CurDAG, SDValue N) {
  SDLoc dl(N);
  SDValue ImpDef = SDValue(
      CurDAG->getMachineNode(TargetOpcode::IMPLICIT_DEF, dl, MVT::i64), 0);
  return CurDAG->getTargetInsertSubreg(AArch64::sub_32, dl, MVT::i64, ImpDef,
                                       N);
}

/// Check if the given SHL node (\p N), can be used to form an
/// extended register for an addressing mode.
bool AArch64DAGToDAGISel::SelectExtendedSHL(SDValue N, unsigned Size,
                                            bool WantExtend, SDValue &Offset,
                                            SDValue &SignExtend) {
  assert(N.getOpcode() == ISD::SHL && "Invalid opcode.");
  ConstantSDNode *CSD = dyn_cast<ConstantSDNode>(N.getOperand(1));
  if (!CSD || (CSD->getZExtValue() & 0x7) != CSD->getZExtValue())
    return false;

  SDLoc dl(N);
  if (WantExtend) {
    AArch64_AM::ShiftExtendType Ext =
        getExtendTypeForNode(N.getOperand(0), true);
    if (Ext == AArch64_AM::InvalidShiftExtend)
      return false;

    Offset = narrowIfNeeded(CurDAG, N.getOperand(0).getOperand(0));
    SignExtend = CurDAG->getTargetConstant(Ext == AArch64_AM::SXTW, dl,
                                           MVT::i32);
  } else {
    Offset = N.getOperand(0);
    SignExtend = CurDAG->getTargetConstant(0, dl, MVT::i32);
  }

  unsigned LegalShiftVal = Log2_32(Size);
  unsigned ShiftVal = CSD->getZExtValue();

  if (ShiftVal != 0 && ShiftVal != LegalShiftVal)
    return false;

  return isWorthFoldingAddr(N, Size);
}

bool AArch64DAGToDAGISel::SelectAddrModeWRO(SDValue N, unsigned Size,
                                            SDValue &Base, SDValue &Offset,
                                            SDValue &SignExtend,
                                            SDValue &DoShift) {
  if (N.getOpcode() != ISD::ADD)
    return false;
  SDValue LHS = N.getOperand(0);
  SDValue RHS = N.getOperand(1);
  SDLoc dl(N);

  // We don't want to match immediate adds here, because they are better lowered
  // to the register-immediate addressing modes.
  if (isa<ConstantSDNode>(LHS) || isa<ConstantSDNode>(RHS))
    return false;

  // Check if this particular node is reused in any non-memory related
  // operation.  If yes, do not try to fold this node into the address
  // computation, since the computation will be kept.
  const SDNode *Node = N.getNode();
  for (SDNode *UI : Node->uses()) {
    if (!isa<MemSDNode>(*UI))
      return false;
  }

  // Remember if it is worth folding N when it produces extended register.
  bool IsExtendedRegisterWorthFolding = isWorthFoldingAddr(N, Size);

  // Try to match a shifted extend on the RHS.
  if (IsExtendedRegisterWorthFolding && RHS.getOpcode() == ISD::SHL &&
      SelectExtendedSHL(RHS, Size, true, Offset, SignExtend)) {
    Base = LHS;
    DoShift = CurDAG->getTargetConstant(true, dl, MVT::i32);
    return true;
  }

  // Try to match a shifted extend on the LHS.
  if (IsExtendedRegisterWorthFolding && LHS.getOpcode() == ISD::SHL &&
      SelectExtendedSHL(LHS, Size, true, Offset, SignExtend)) {
    Base = RHS;
    DoShift = CurDAG->getTargetConstant(true, dl, MVT::i32);
    return true;
  }

  // There was no shift, whatever else we find.
  DoShift = CurDAG->getTargetConstant(false, dl, MVT::i32);

  AArch64_AM::ShiftExtendType Ext = AArch64_AM::InvalidShiftExtend;
  // Try to match an unshifted extend on the LHS.
  if (IsExtendedRegisterWorthFolding &&
      (Ext = getExtendTypeForNode(LHS, true)) !=
          AArch64_AM::InvalidShiftExtend) {
    Base = RHS;
    Offset = narrowIfNeeded(CurDAG, LHS.getOperand(0));
    SignExtend = CurDAG->getTargetConstant(Ext == AArch64_AM::SXTW, dl,
                                           MVT::i32);
    if (isWorthFoldingAddr(LHS, Size))
      return true;
  }

  // Try to match an unshifted extend on the RHS.
  if (IsExtendedRegisterWorthFolding &&
      (Ext = getExtendTypeForNode(RHS, true)) !=
          AArch64_AM::InvalidShiftExtend) {
    Base = LHS;
    Offset = narrowIfNeeded(CurDAG, RHS.getOperand(0));
    SignExtend = CurDAG->getTargetConstant(Ext == AArch64_AM::SXTW, dl,
                                           MVT::i32);
    if (isWorthFoldingAddr(RHS, Size))
      return true;
  }

  return false;
}

// Check if the given immediate is preferred by ADD. If an immediate can be
// encoded in an ADD, or it can be encoded in an "ADD LSL #12" and can not be
// encoded by one MOVZ, return true.
static bool isPreferredADD(int64_t ImmOff) {
  // Constant in [0x0, 0xfff] can be encoded in ADD.
  if ((ImmOff & 0xfffffffffffff000LL) == 0x0LL)
    return true;
  // Check if it can be encoded in an "ADD LSL #12".
  if ((ImmOff & 0xffffffffff000fffLL) == 0x0LL)
    // As a single MOVZ is faster than a "ADD of LSL #12", ignore such constant.
    return (ImmOff & 0xffffffffff00ffffLL) != 0x0LL &&
           (ImmOff & 0xffffffffffff0fffLL) != 0x0LL;
  return false;
}

bool AArch64DAGToDAGISel::SelectAddrModeXRO(SDValue N, unsigned Size,
                                            SDValue &Base, SDValue &Offset,
                                            SDValue &SignExtend,
                                            SDValue &DoShift) {
  if (N.getOpcode() != ISD::ADD)
    return false;
  SDValue LHS = N.getOperand(0);
  SDValue RHS = N.getOperand(1);
  SDLoc DL(N);

  // Check if this particular node is reused in any non-memory related
  // operation.  If yes, do not try to fold this node into the address
  // computation, since the computation will be kept.
  const SDNode *Node = N.getNode();
  for (SDNode *UI : Node->uses()) {
    if (!isa<MemSDNode>(*UI))
      return false;
  }

  // Watch out if RHS is a wide immediate, it can not be selected into
  // [BaseReg+Imm] addressing mode. Also it may not be able to be encoded into
  // ADD/SUB. Instead it will use [BaseReg + 0] address mode and generate
  // instructions like:
  //     MOV  X0, WideImmediate
  //     ADD  X1, BaseReg, X0
  //     LDR  X2, [X1, 0]
  // For such situation, using [BaseReg, XReg] addressing mode can save one
  // ADD/SUB:
  //     MOV  X0, WideImmediate
  //     LDR  X2, [BaseReg, X0]
  if (isa<ConstantSDNode>(RHS)) {
    int64_t ImmOff = (int64_t)RHS->getAsZExtVal();
    // Skip the immediate can be selected by load/store addressing mode.
    // Also skip the immediate can be encoded by a single ADD (SUB is also
    // checked by using -ImmOff).
    if (isValidAsScaledImmediate(ImmOff, 0x1000, Size) ||
        isPreferredADD(ImmOff) || isPreferredADD(-ImmOff))
      return false;

    SDValue Ops[] = { RHS };
    SDNode *MOVI =
        CurDAG->getMachineNode(AArch64::MOVi64imm, DL, MVT::i64, Ops);
    SDValue MOVIV = SDValue(MOVI, 0);
    // This ADD of two X register will be selected into [Reg+Reg] mode.
    N = CurDAG->getNode(ISD::ADD, DL, MVT::i64, LHS, MOVIV);
  }

  // Remember if it is worth folding N when it produces extended register.
  bool IsExtendedRegisterWorthFolding = isWorthFoldingAddr(N, Size);

  // Try to match a shifted extend on the RHS.
  if (IsExtendedRegisterWorthFolding && RHS.getOpcode() == ISD::SHL &&
      SelectExtendedSHL(RHS, Size, false, Offset, SignExtend)) {
    Base = LHS;
    DoShift = CurDAG->getTargetConstant(true, DL, MVT::i32);
    return true;
  }

  // Try to match a shifted extend on the LHS.
  if (IsExtendedRegisterWorthFolding && LHS.getOpcode() == ISD::SHL &&
      SelectExtendedSHL(LHS, Size, false, Offset, SignExtend)) {
    Base = RHS;
    DoShift = CurDAG->getTargetConstant(true, DL, MVT::i32);
    return true;
  }

  // Match any non-shifted, non-extend, non-immediate add expression.
  Base = LHS;
  Offset = RHS;
  SignExtend = CurDAG->getTargetConstant(false, DL, MVT::i32);
  DoShift = CurDAG->getTargetConstant(false, DL, MVT::i32);
  // Reg1 + Reg2 is free: no check needed.
  return true;
}

SDValue AArch64DAGToDAGISel::createDTuple(ArrayRef<SDValue> Regs) {
  static const unsigned RegClassIDs[] = {
      AArch64::DDRegClassID, AArch64::DDDRegClassID, AArch64::DDDDRegClassID};
  static const unsigned SubRegs[] = {AArch64::dsub0, AArch64::dsub1,
                                     AArch64::dsub2, AArch64::dsub3};

  return createTuple(Regs, RegClassIDs, SubRegs);
}

SDValue AArch64DAGToDAGISel::createQTuple(ArrayRef<SDValue> Regs) {
  static const unsigned RegClassIDs[] = {
      AArch64::QQRegClassID, AArch64::QQQRegClassID, AArch64::QQQQRegClassID};
  static const unsigned SubRegs[] = {AArch64::qsub0, AArch64::qsub1,
                                     AArch64::qsub2, AArch64::qsub3};

  return createTuple(Regs, RegClassIDs, SubRegs);
}

SDValue AArch64DAGToDAGISel::createZTuple(ArrayRef<SDValue> Regs) {
  static const unsigned RegClassIDs[] = {AArch64::ZPR2RegClassID,
                                         AArch64::ZPR3RegClassID,
                                         AArch64::ZPR4RegClassID};
  static const unsigned SubRegs[] = {AArch64::zsub0, AArch64::zsub1,
                                     AArch64::zsub2, AArch64::zsub3};

  return createTuple(Regs, RegClassIDs, SubRegs);
}

SDValue AArch64DAGToDAGISel::createZMulTuple(ArrayRef<SDValue> Regs) {
  assert(Regs.size() == 2 || Regs.size() == 4);

  // The createTuple interface requires 3 RegClassIDs for each possible
  // tuple type even though we only have them for ZPR2 and ZPR4.
  static const unsigned RegClassIDs[] = {AArch64::ZPR2Mul2RegClassID, 0,
                                         AArch64::ZPR4Mul4RegClassID};
  static const unsigned SubRegs[] = {AArch64::zsub0, AArch64::zsub1,
                                     AArch64::zsub2, AArch64::zsub3};
  return createTuple(Regs, RegClassIDs, SubRegs);
}

SDValue AArch64DAGToDAGISel::createTuple(ArrayRef<SDValue> Regs,
                                         const unsigned RegClassIDs[],
                                         const unsigned SubRegs[]) {
  // There's no special register-class for a vector-list of 1 element: it's just
  // a vector.
  if (Regs.size() == 1)
    return Regs[0];

  assert(Regs.size() >= 2 && Regs.size() <= 4);

  SDLoc DL(Regs[0]);

  SmallVector<SDValue, 4> Ops;

  // First operand of REG_SEQUENCE is the desired RegClass.
  Ops.push_back(
      CurDAG->getTargetConstant(RegClassIDs[Regs.size() - 2], DL, MVT::i32));

  // Then we get pairs of source & subregister-position for the components.
  for (unsigned i = 0; i < Regs.size(); ++i) {
    Ops.push_back(Regs[i]);
    Ops.push_back(CurDAG->getTargetConstant(SubRegs[i], DL, MVT::i32));
  }

  SDNode *N =
      CurDAG->getMachineNode(TargetOpcode::REG_SEQUENCE, DL, MVT::Untyped, Ops);
  return SDValue(N, 0);
}

void AArch64DAGToDAGISel::SelectTable(SDNode *N, unsigned NumVecs, unsigned Opc,
                                      bool isExt) {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);

  unsigned ExtOff = isExt;

  // Form a REG_SEQUENCE to force register allocation.
  unsigned Vec0Off = ExtOff + 1;
  SmallVector<SDValue, 4> Regs(N->op_begin() + Vec0Off,
                               N->op_begin() + Vec0Off + NumVecs);
  SDValue RegSeq = createQTuple(Regs);

  SmallVector<SDValue, 6> Ops;
  if (isExt)
    Ops.push_back(N->getOperand(1));
  Ops.push_back(RegSeq);
  Ops.push_back(N->getOperand(NumVecs + ExtOff + 1));
  ReplaceNode(N, CurDAG->getMachineNode(Opc, dl, VT, Ops));
}

static std::tuple<SDValue, SDValue>
extractPtrauthBlendDiscriminators(SDValue Disc, SelectionDAG *DAG) {
  SDLoc DL(Disc);
  SDValue AddrDisc;
  SDValue ConstDisc;

  // If this is a blend, remember the constant and address discriminators.
  // Otherwise, it's either a constant discriminator, or a non-blended
  // address discriminator.
  if (Disc->getOpcode() == ISD::INTRINSIC_WO_CHAIN &&
      Disc->getConstantOperandVal(0) == Intrinsic::ptrauth_blend) {
    AddrDisc = Disc->getOperand(1);
    ConstDisc = Disc->getOperand(2);
  } else {
    ConstDisc = Disc;
  }

  // If the constant discriminator (either the blend RHS, or the entire
  // discriminator value) isn't a 16-bit constant, bail out, and let the
  // discriminator be computed separately.
  auto *ConstDiscN = dyn_cast<ConstantSDNode>(ConstDisc);
  if (!ConstDiscN || !isUInt<16>(ConstDiscN->getZExtValue()))
    return std::make_tuple(DAG->getTargetConstant(0, DL, MVT::i64), Disc);

  // If there's no address discriminator, use XZR directly.
  if (!AddrDisc)
    AddrDisc = DAG->getRegister(AArch64::XZR, MVT::i64);

  return std::make_tuple(
      DAG->getTargetConstant(ConstDiscN->getZExtValue(), DL, MVT::i64),
      AddrDisc);
}

void AArch64DAGToDAGISel::SelectPtrauthAuth(SDNode *N) {
  SDLoc DL(N);
  // IntrinsicID is operand #0
  SDValue Val = N->getOperand(1);
  SDValue AUTKey = N->getOperand(2);
  SDValue AUTDisc = N->getOperand(3);

  unsigned AUTKeyC = cast<ConstantSDNode>(AUTKey)->getZExtValue();
  AUTKey = CurDAG->getTargetConstant(AUTKeyC, DL, MVT::i64);

  SDValue AUTAddrDisc, AUTConstDisc;
  std::tie(AUTConstDisc, AUTAddrDisc) =
      extractPtrauthBlendDiscriminators(AUTDisc, CurDAG);

  SDValue X16Copy = CurDAG->getCopyToReg(CurDAG->getEntryNode(), DL,
                                         AArch64::X16, Val, SDValue());
  SDValue Ops[] = {AUTKey, AUTConstDisc, AUTAddrDisc, X16Copy.getValue(1)};

  SDNode *AUT = CurDAG->getMachineNode(AArch64::AUT, DL, MVT::i64, Ops);
  ReplaceNode(N, AUT);
  return;
}

void AArch64DAGToDAGISel::SelectPtrauthResign(SDNode *N) {
  SDLoc DL(N);
  // IntrinsicID is operand #0
  SDValue Val = N->getOperand(1);
  SDValue AUTKey = N->getOperand(2);
  SDValue AUTDisc = N->getOperand(3);
  SDValue PACKey = N->getOperand(4);
  SDValue PACDisc = N->getOperand(5);

  unsigned AUTKeyC = cast<ConstantSDNode>(AUTKey)->getZExtValue();
  unsigned PACKeyC = cast<ConstantSDNode>(PACKey)->getZExtValue();

  AUTKey = CurDAG->getTargetConstant(AUTKeyC, DL, MVT::i64);
  PACKey = CurDAG->getTargetConstant(PACKeyC, DL, MVT::i64);

  SDValue AUTAddrDisc, AUTConstDisc;
  std::tie(AUTConstDisc, AUTAddrDisc) =
      extractPtrauthBlendDiscriminators(AUTDisc, CurDAG);

  SDValue PACAddrDisc, PACConstDisc;
  std::tie(PACConstDisc, PACAddrDisc) =
      extractPtrauthBlendDiscriminators(PACDisc, CurDAG);

  SDValue X16Copy = CurDAG->getCopyToReg(CurDAG->getEntryNode(), DL,
                                         AArch64::X16, Val, SDValue());

  SDValue Ops[] = {AUTKey,       AUTConstDisc, AUTAddrDisc,        PACKey,
                   PACConstDisc, PACAddrDisc,  X16Copy.getValue(1)};

  SDNode *AUTPAC = CurDAG->getMachineNode(AArch64::AUTPAC, DL, MVT::i64, Ops);
  ReplaceNode(N, AUTPAC);
  return;
}

bool AArch64DAGToDAGISel::tryIndexedLoad(SDNode *N) {
  LoadSDNode *LD = cast<LoadSDNode>(N);
  if (LD->isUnindexed())
    return false;
  EVT VT = LD->getMemoryVT();
  EVT DstVT = N->getValueType(0);
  ISD::MemIndexedMode AM = LD->getAddressingMode();
  bool IsPre = AM == ISD::PRE_INC || AM == ISD::PRE_DEC;

  // We're not doing validity checking here. That was done when checking
  // if we should mark the load as indexed or not. We're just selecting
  // the right instruction.
  unsigned Opcode = 0;

  ISD::LoadExtType ExtType = LD->getExtensionType();
  bool InsertTo64 = false;
  if (VT == MVT::i64)
    Opcode = IsPre ? AArch64::LDRXpre : AArch64::LDRXpost;
  else if (VT == MVT::i32) {
    if (ExtType == ISD::NON_EXTLOAD)
      Opcode = IsPre ? AArch64::LDRWpre : AArch64::LDRWpost;
    else if (ExtType == ISD::SEXTLOAD)
      Opcode = IsPre ? AArch64::LDRSWpre : AArch64::LDRSWpost;
    else {
      Opcode = IsPre ? AArch64::LDRWpre : AArch64::LDRWpost;
      InsertTo64 = true;
      // The result of the load is only i32. It's the subreg_to_reg that makes
      // it into an i64.
      DstVT = MVT::i32;
    }
  } else if (VT == MVT::i16) {
    if (ExtType == ISD::SEXTLOAD) {
      if (DstVT == MVT::i64)
        Opcode = IsPre ? AArch64::LDRSHXpre : AArch64::LDRSHXpost;
      else
        Opcode = IsPre ? AArch64::LDRSHWpre : AArch64::LDRSHWpost;
    } else {
      Opcode = IsPre ? AArch64::LDRHHpre : AArch64::LDRHHpost;
      InsertTo64 = DstVT == MVT::i64;
      // The result of the load is only i32. It's the subreg_to_reg that makes
      // it into an i64.
      DstVT = MVT::i32;
    }
  } else if (VT == MVT::i8) {
    if (ExtType == ISD::SEXTLOAD) {
      if (DstVT == MVT::i64)
        Opcode = IsPre ? AArch64::LDRSBXpre : AArch64::LDRSBXpost;
      else
        Opcode = IsPre ? AArch64::LDRSBWpre : AArch64::LDRSBWpost;
    } else {
      Opcode = IsPre ? AArch64::LDRBBpre : AArch64::LDRBBpost;
      InsertTo64 = DstVT == MVT::i64;
      // The result of the load is only i32. It's the subreg_to_reg that makes
      // it into an i64.
      DstVT = MVT::i32;
    }
  } else if (VT == MVT::f16) {
    Opcode = IsPre ? AArch64::LDRHpre : AArch64::LDRHpost;
  } else if (VT == MVT::bf16) {
    Opcode = IsPre ? AArch64::LDRHpre : AArch64::LDRHpost;
  } else if (VT == MVT::f32) {
    Opcode = IsPre ? AArch64::LDRSpre : AArch64::LDRSpost;
  } else if (VT == MVT::f64 || VT.is64BitVector()) {
    Opcode = IsPre ? AArch64::LDRDpre : AArch64::LDRDpost;
  } else if (VT.is128BitVector()) {
    Opcode = IsPre ? AArch64::LDRQpre : AArch64::LDRQpost;
  } else
    return false;
  SDValue Chain = LD->getChain();
  SDValue Base = LD->getBasePtr();
  ConstantSDNode *OffsetOp = cast<ConstantSDNode>(LD->getOffset());
  int OffsetVal = (int)OffsetOp->getZExtValue();
  SDLoc dl(N);
  SDValue Offset = CurDAG->getTargetConstant(OffsetVal, dl, MVT::i64);
  SDValue Ops[] = { Base, Offset, Chain };
  SDNode *Res = CurDAG->getMachineNode(Opcode, dl, MVT::i64, DstVT,
                                       MVT::Other, Ops);

  // Transfer memoperands.
  MachineMemOperand *MemOp = cast<MemSDNode>(N)->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(Res), {MemOp});

  // Either way, we're replacing the node, so tell the caller that.
  SDValue LoadedVal = SDValue(Res, 1);
  if (InsertTo64) {
    SDValue SubReg = CurDAG->getTargetConstant(AArch64::sub_32, dl, MVT::i32);
    LoadedVal =
        SDValue(CurDAG->getMachineNode(
                    AArch64::SUBREG_TO_REG, dl, MVT::i64,
                    CurDAG->getTargetConstant(0, dl, MVT::i64), LoadedVal,
                    SubReg),
                0);
  }

  ReplaceUses(SDValue(N, 0), LoadedVal);
  ReplaceUses(SDValue(N, 1), SDValue(Res, 0));
  ReplaceUses(SDValue(N, 2), SDValue(Res, 2));
  CurDAG->RemoveDeadNode(N);
  return true;
}

void AArch64DAGToDAGISel::SelectLoad(SDNode *N, unsigned NumVecs, unsigned Opc,
                                     unsigned SubRegIdx) {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  SDValue Chain = N->getOperand(0);

  SDValue Ops[] = {N->getOperand(2), // Mem operand;
                   Chain};

  const EVT ResTys[] = {MVT::Untyped, MVT::Other};

  SDNode *Ld = CurDAG->getMachineNode(Opc, dl, ResTys, Ops);
  SDValue SuperReg = SDValue(Ld, 0);
  for (unsigned i = 0; i < NumVecs; ++i)
    ReplaceUses(SDValue(N, i),
        CurDAG->getTargetExtractSubreg(SubRegIdx + i, dl, VT, SuperReg));

  ReplaceUses(SDValue(N, NumVecs), SDValue(Ld, 1));

  // Transfer memoperands. In the case of AArch64::LD64B, there won't be one,
  // because it's too simple to have needed special treatment during lowering.
  if (auto *MemIntr = dyn_cast<MemIntrinsicSDNode>(N)) {
    MachineMemOperand *MemOp = MemIntr->getMemOperand();
    CurDAG->setNodeMemRefs(cast<MachineSDNode>(Ld), {MemOp});
  }

  CurDAG->RemoveDeadNode(N);
}

void AArch64DAGToDAGISel::SelectPostLoad(SDNode *N, unsigned NumVecs,
                                         unsigned Opc, unsigned SubRegIdx) {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  SDValue Chain = N->getOperand(0);

  SDValue Ops[] = {N->getOperand(1), // Mem operand
                   N->getOperand(2), // Incremental
                   Chain};

  const EVT ResTys[] = {MVT::i64, // Type of the write back register
                        MVT::Untyped, MVT::Other};

  SDNode *Ld = CurDAG->getMachineNode(Opc, dl, ResTys, Ops);

  // Update uses of write back register
  ReplaceUses(SDValue(N, NumVecs), SDValue(Ld, 0));

  // Update uses of vector list
  SDValue SuperReg = SDValue(Ld, 1);
  if (NumVecs == 1)
    ReplaceUses(SDValue(N, 0), SuperReg);
  else
    for (unsigned i = 0; i < NumVecs; ++i)
      ReplaceUses(SDValue(N, i),
          CurDAG->getTargetExtractSubreg(SubRegIdx + i, dl, VT, SuperReg));

  // Update the chain
  ReplaceUses(SDValue(N, NumVecs + 1), SDValue(Ld, 2));
  CurDAG->RemoveDeadNode(N);
}

/// Optimize \param OldBase and \param OldOffset selecting the best addressing
/// mode. Returns a tuple consisting of an Opcode, an SDValue representing the
/// new Base and an SDValue representing the new offset.
std::tuple<unsigned, SDValue, SDValue>
AArch64DAGToDAGISel::findAddrModeSVELoadStore(SDNode *N, unsigned Opc_rr,
                                              unsigned Opc_ri,
                                              const SDValue &OldBase,
                                              const SDValue &OldOffset,
                                              unsigned Scale) {
  SDValue NewBase = OldBase;
  SDValue NewOffset = OldOffset;
  // Detect a possible Reg+Imm addressing mode.
  const bool IsRegImm = SelectAddrModeIndexedSVE</*Min=*/-8, /*Max=*/7>(
      N, OldBase, NewBase, NewOffset);

  // Detect a possible reg+reg addressing mode, but only if we haven't already
  // detected a Reg+Imm one.
  const bool IsRegReg =
      !IsRegImm && SelectSVERegRegAddrMode(OldBase, Scale, NewBase, NewOffset);

  // Select the instruction.
  return std::make_tuple(IsRegReg ? Opc_rr : Opc_ri, NewBase, NewOffset);
}

enum class SelectTypeKind {
  Int1 = 0,
  Int = 1,
  FP = 2,
  AnyType = 3,
};

/// This function selects an opcode from a list of opcodes, which is
/// expected to be the opcode for { 8-bit, 16-bit, 32-bit, 64-bit }
/// element types, in this order.
template <SelectTypeKind Kind>
static unsigned SelectOpcodeFromVT(EVT VT, ArrayRef<unsigned> Opcodes) {
  // Only match scalable vector VTs
  if (!VT.isScalableVector())
    return 0;

  EVT EltVT = VT.getVectorElementType();
  unsigned Key = VT.getVectorMinNumElements();
  switch (Kind) {
  case SelectTypeKind::AnyType:
    break;
  case SelectTypeKind::Int:
    if (EltVT != MVT::i8 && EltVT != MVT::i16 && EltVT != MVT::i32 &&
        EltVT != MVT::i64)
      return 0;
    break;
  case SelectTypeKind::Int1:
    if (EltVT != MVT::i1)
      return 0;
    break;
  case SelectTypeKind::FP:
    if (EltVT == MVT::bf16)
      Key = 16;
    else if (EltVT != MVT::bf16 && EltVT != MVT::f16 && EltVT != MVT::f32 &&
             EltVT != MVT::f64)
      return 0;
    break;
  }

  unsigned Offset;
  switch (Key) {
  case 16: // 8-bit or bf16
    Offset = 0;
    break;
  case 8: // 16-bit
    Offset = 1;
    break;
  case 4: // 32-bit
    Offset = 2;
    break;
  case 2: // 64-bit
    Offset = 3;
    break;
  default:
    return 0;
  }

  return (Opcodes.size() <= Offset) ? 0 : Opcodes[Offset];
}

// This function is almost identical to SelectWhilePair, but has an
// extra check on the range of the immediate operand.
// TODO: Merge these two functions together at some point?
void AArch64DAGToDAGISel::SelectPExtPair(SDNode *N, unsigned Opc) {
  // Immediate can be either 0 or 1.
  if (ConstantSDNode *Imm = dyn_cast<ConstantSDNode>(N->getOperand(2)))
    if (Imm->getZExtValue() > 1)
      return;

  SDLoc DL(N);
  EVT VT = N->getValueType(0);
  SDValue Ops[] = {N->getOperand(1), N->getOperand(2)};
  SDNode *WhilePair = CurDAG->getMachineNode(Opc, DL, MVT::Untyped, Ops);
  SDValue SuperReg = SDValue(WhilePair, 0);

  for (unsigned I = 0; I < 2; ++I)
    ReplaceUses(SDValue(N, I), CurDAG->getTargetExtractSubreg(
                                   AArch64::psub0 + I, DL, VT, SuperReg));

  CurDAG->RemoveDeadNode(N);
}

void AArch64DAGToDAGISel::SelectWhilePair(SDNode *N, unsigned Opc) {
  SDLoc DL(N);
  EVT VT = N->getValueType(0);

  SDValue Ops[] = {N->getOperand(1), N->getOperand(2)};

  SDNode *WhilePair = CurDAG->getMachineNode(Opc, DL, MVT::Untyped, Ops);
  SDValue SuperReg = SDValue(WhilePair, 0);

  for (unsigned I = 0; I < 2; ++I)
    ReplaceUses(SDValue(N, I), CurDAG->getTargetExtractSubreg(
                                   AArch64::psub0 + I, DL, VT, SuperReg));

  CurDAG->RemoveDeadNode(N);
}

void AArch64DAGToDAGISel::SelectCVTIntrinsic(SDNode *N, unsigned NumVecs,
                                             unsigned Opcode) {
  EVT VT = N->getValueType(0);
  SmallVector<SDValue, 4> Regs(N->op_begin() + 1, N->op_begin() + 1 + NumVecs);
  SDValue Ops = createZTuple(Regs);
  SDLoc DL(N);
  SDNode *Intrinsic = CurDAG->getMachineNode(Opcode, DL, MVT::Untyped, Ops);
  SDValue SuperReg = SDValue(Intrinsic, 0);
  for (unsigned i = 0; i < NumVecs; ++i)
    ReplaceUses(SDValue(N, i), CurDAG->getTargetExtractSubreg(
                                   AArch64::zsub0 + i, DL, VT, SuperReg));

  CurDAG->RemoveDeadNode(N);
}

void AArch64DAGToDAGISel::SelectDestructiveMultiIntrinsic(SDNode *N,
                                                          unsigned NumVecs,
                                                          bool IsZmMulti,
                                                          unsigned Opcode,
                                                          bool HasPred) {
  assert(Opcode != 0 && "Unexpected opcode");

  SDLoc DL(N);
  EVT VT = N->getValueType(0);
  unsigned FirstVecIdx = HasPred ? 2 : 1;

  auto GetMultiVecOperand = [=](unsigned StartIdx) {
    SmallVector<SDValue, 4> Regs(N->op_begin() + StartIdx,
                                 N->op_begin() + StartIdx + NumVecs);
    return createZMulTuple(Regs);
  };

  SDValue Zdn = GetMultiVecOperand(FirstVecIdx);

  SDValue Zm;
  if (IsZmMulti)
    Zm = GetMultiVecOperand(NumVecs + FirstVecIdx);
  else
    Zm = N->getOperand(NumVecs + FirstVecIdx);

  SDNode *Intrinsic;
  if (HasPred)
    Intrinsic = CurDAG->getMachineNode(Opcode, DL, MVT::Untyped,
                                       N->getOperand(1), Zdn, Zm);
  else
    Intrinsic = CurDAG->getMachineNode(Opcode, DL, MVT::Untyped, Zdn, Zm);
  SDValue SuperReg = SDValue(Intrinsic, 0);
  for (unsigned i = 0; i < NumVecs; ++i)
    ReplaceUses(SDValue(N, i), CurDAG->getTargetExtractSubreg(
                                   AArch64::zsub0 + i, DL, VT, SuperReg));

  CurDAG->RemoveDeadNode(N);
}

void AArch64DAGToDAGISel::SelectPredicatedLoad(SDNode *N, unsigned NumVecs,
                                               unsigned Scale, unsigned Opc_ri,
                                               unsigned Opc_rr, bool IsIntr) {
  assert(Scale < 5 && "Invalid scaling value.");
  SDLoc DL(N);
  EVT VT = N->getValueType(0);
  SDValue Chain = N->getOperand(0);

  // Optimize addressing mode.
  SDValue Base, Offset;
  unsigned Opc;
  std::tie(Opc, Base, Offset) = findAddrModeSVELoadStore(
      N, Opc_rr, Opc_ri, N->getOperand(IsIntr ? 3 : 2),
      CurDAG->getTargetConstant(0, DL, MVT::i64), Scale);

  SDValue Ops[] = {N->getOperand(IsIntr ? 2 : 1), // Predicate
                   Base,                          // Memory operand
                   Offset, Chain};

  const EVT ResTys[] = {MVT::Untyped, MVT::Other};

  SDNode *Load = CurDAG->getMachineNode(Opc, DL, ResTys, Ops);
  SDValue SuperReg = SDValue(Load, 0);
  for (unsigned i = 0; i < NumVecs; ++i)
    ReplaceUses(SDValue(N, i), CurDAG->getTargetExtractSubreg(
                                   AArch64::zsub0 + i, DL, VT, SuperReg));

  // Copy chain
  unsigned ChainIdx = NumVecs;
  ReplaceUses(SDValue(N, ChainIdx), SDValue(Load, 1));
  CurDAG->RemoveDeadNode(N);
}

void AArch64DAGToDAGISel::SelectContiguousMultiVectorLoad(SDNode *N,
                                                          unsigned NumVecs,
                                                          unsigned Scale,
                                                          unsigned Opc_ri,
                                                          unsigned Opc_rr) {
  assert(Scale < 4 && "Invalid scaling value.");
  SDLoc DL(N);
  EVT VT = N->getValueType(0);
  SDValue Chain = N->getOperand(0);

  SDValue PNg = N->getOperand(2);
  SDValue Base = N->getOperand(3);
  SDValue Offset = CurDAG->getTargetConstant(0, DL, MVT::i64);
  unsigned Opc;
  std::tie(Opc, Base, Offset) =
      findAddrModeSVELoadStore(N, Opc_rr, Opc_ri, Base, Offset, Scale);

  SDValue Ops[] = {PNg,            // Predicate-as-counter
                   Base,           // Memory operand
                   Offset, Chain};

  const EVT ResTys[] = {MVT::Untyped, MVT::Other};

  SDNode *Load = CurDAG->getMachineNode(Opc, DL, ResTys, Ops);
  SDValue SuperReg = SDValue(Load, 0);
  for (unsigned i = 0; i < NumVecs; ++i)
    ReplaceUses(SDValue(N, i), CurDAG->getTargetExtractSubreg(
                                   AArch64::zsub0 + i, DL, VT, SuperReg));

  // Copy chain
  unsigned ChainIdx = NumVecs;
  ReplaceUses(SDValue(N, ChainIdx), SDValue(Load, 1));
  CurDAG->RemoveDeadNode(N);
}

void AArch64DAGToDAGISel::SelectFrintFromVT(SDNode *N, unsigned NumVecs,
                                            unsigned Opcode) {
  if (N->getValueType(0) != MVT::nxv4f32)
    return;
  SelectUnaryMultiIntrinsic(N, NumVecs, true, Opcode);
}

void AArch64DAGToDAGISel::SelectMultiVectorLuti(SDNode *Node,
                                                unsigned NumOutVecs,
                                                unsigned Opc, uint32_t MaxImm) {
  if (ConstantSDNode *Imm = dyn_cast<ConstantSDNode>(Node->getOperand(4)))
    if (Imm->getZExtValue() > MaxImm)
      return;

  SDValue ZtValue;
  if (!ImmToReg<AArch64::ZT0, 0>(Node->getOperand(2), ZtValue))
    return;
  SDValue Ops[] = {ZtValue, Node->getOperand(3), Node->getOperand(4)};
  SDLoc DL(Node);
  EVT VT = Node->getValueType(0);

  SDNode *Instruction =
      CurDAG->getMachineNode(Opc, DL, {MVT::Untyped, MVT::Other}, Ops);
  SDValue SuperReg = SDValue(Instruction, 0);

  for (unsigned I = 0; I < NumOutVecs; ++I)
    ReplaceUses(SDValue(Node, I), CurDAG->getTargetExtractSubreg(
                                      AArch64::zsub0 + I, DL, VT, SuperReg));

  // Copy chain
  unsigned ChainIdx = NumOutVecs;
  ReplaceUses(SDValue(Node, ChainIdx), SDValue(Instruction, 1));
  CurDAG->RemoveDeadNode(Node);
}

void AArch64DAGToDAGISel::SelectClamp(SDNode *N, unsigned NumVecs,
                                      unsigned Op) {
  SDLoc DL(N);
  EVT VT = N->getValueType(0);

  SmallVector<SDValue, 4> Regs(N->op_begin() + 1, N->op_begin() + 1 + NumVecs);
  SDValue Zd = createZMulTuple(Regs);
  SDValue Zn = N->getOperand(1 + NumVecs);
  SDValue Zm = N->getOperand(2 + NumVecs);

  SDValue Ops[] = {Zd, Zn, Zm};

  SDNode *Intrinsic = CurDAG->getMachineNode(Op, DL, MVT::Untyped, Ops);
  SDValue SuperReg = SDValue(Intrinsic, 0);
  for (unsigned i = 0; i < NumVecs; ++i)
    ReplaceUses(SDValue(N, i), CurDAG->getTargetExtractSubreg(
                                   AArch64::zsub0 + i, DL, VT, SuperReg));

  CurDAG->RemoveDeadNode(N);
}

bool SelectSMETile(unsigned &BaseReg, unsigned TileNum) {
  switch (BaseReg) {
  default:
    return false;
  case AArch64::ZA:
  case AArch64::ZAB0:
    if (TileNum == 0)
      break;
    return false;
  case AArch64::ZAH0:
    if (TileNum <= 1)
      break;
    return false;
  case AArch64::ZAS0:
    if (TileNum <= 3)
      break;
    return false;
  case AArch64::ZAD0:
    if (TileNum <= 7)
      break;
    return false;
  }

  BaseReg += TileNum;
  return true;
}

template <unsigned MaxIdx, unsigned Scale>
void AArch64DAGToDAGISel::SelectMultiVectorMove(SDNode *N, unsigned NumVecs,
                                                unsigned BaseReg, unsigned Op) {
  unsigned TileNum = 0;
  if (BaseReg != AArch64::ZA)
    TileNum = N->getConstantOperandVal(2);

  if (!SelectSMETile(BaseReg, TileNum))
    return;

  SDValue SliceBase, Base, Offset;
  if (BaseReg == AArch64::ZA)
    SliceBase = N->getOperand(2);
  else
    SliceBase = N->getOperand(3);

  if (!SelectSMETileSlice(SliceBase, MaxIdx, Base, Offset, Scale))
    return;

  SDLoc DL(N);
  SDValue SubReg = CurDAG->getRegister(BaseReg, MVT::Other);
  SDValue Ops[] = {SubReg, Base, Offset, /*Chain*/ N->getOperand(0)};
  SDNode *Mov = CurDAG->getMachineNode(Op, DL, {MVT::Untyped, MVT::Other}, Ops);

  EVT VT = N->getValueType(0);
  for (unsigned I = 0; I < NumVecs; ++I)
    ReplaceUses(SDValue(N, I),
                CurDAG->getTargetExtractSubreg(AArch64::zsub0 + I, DL, VT,
                                               SDValue(Mov, 0)));
  // Copy chain
  unsigned ChainIdx = NumVecs;
  ReplaceUses(SDValue(N, ChainIdx), SDValue(Mov, 1));
  CurDAG->RemoveDeadNode(N);
}

void AArch64DAGToDAGISel::SelectMultiVectorMoveZ(SDNode *N, unsigned NumVecs,
                                                 unsigned Op, unsigned MaxIdx,
                                                 unsigned Scale, unsigned BaseReg) {
  // Slice can be in different positions
  // The array to vector: llvm.aarch64.sme.readz.<h/v>.<sz>(slice)
  // The tile to vector: llvm.aarch64.sme.readz.<h/v>.<sz>(tile, slice)
  SDValue SliceBase = N->getOperand(2);
  if (BaseReg != AArch64::ZA)
    SliceBase = N->getOperand(3);

  SDValue Base, Offset;
  if (!SelectSMETileSlice(SliceBase, MaxIdx, Base, Offset, Scale))
    return;
  // The correct Za tile number is computed in Machine Instruction
  // See EmitZAInstr
  // DAG cannot select Za tile as an output register with ZReg
  SDLoc DL(N);
  SmallVector<SDValue, 6> Ops;
  if (BaseReg != AArch64::ZA )
    Ops.push_back(N->getOperand(2));
  Ops.push_back(Base);
  Ops.push_back(Offset);
  Ops.push_back(N->getOperand(0)); //Chain
  SDNode *Mov = CurDAG->getMachineNode(Op, DL, {MVT::Untyped, MVT::Other}, Ops);

  EVT VT = N->getValueType(0);
  for (unsigned I = 0; I < NumVecs; ++I)
    ReplaceUses(SDValue(N, I),
                CurDAG->getTargetExtractSubreg(AArch64::zsub0 + I, DL, VT,
                                               SDValue(Mov, 0)));

  // Copy chain
  unsigned ChainIdx = NumVecs;
  ReplaceUses(SDValue(N, ChainIdx), SDValue(Mov, 1));
  CurDAG->RemoveDeadNode(N);
}

void AArch64DAGToDAGISel::SelectUnaryMultiIntrinsic(SDNode *N,
                                                    unsigned NumOutVecs,
                                                    bool IsTupleInput,
                                                    unsigned Opc) {
  SDLoc DL(N);
  EVT VT = N->getValueType(0);
  unsigned NumInVecs = N->getNumOperands() - 1;

  SmallVector<SDValue, 6> Ops;
  if (IsTupleInput) {
    assert((NumInVecs == 2 || NumInVecs == 4) &&
           "Don't know how to handle multi-register input!");
    SmallVector<SDValue, 4> Regs(N->op_begin() + 1,
                                 N->op_begin() + 1 + NumInVecs);
    Ops.push_back(createZMulTuple(Regs));
  } else {
    // All intrinsic nodes have the ID as the first operand, hence the "1 + I".
    for (unsigned I = 0; I < NumInVecs; I++)
      Ops.push_back(N->getOperand(1 + I));
  }

  SDNode *Res = CurDAG->getMachineNode(Opc, DL, MVT::Untyped, Ops);
  SDValue SuperReg = SDValue(Res, 0);

  for (unsigned I = 0; I < NumOutVecs; I++)
    ReplaceUses(SDValue(N, I), CurDAG->getTargetExtractSubreg(
                                   AArch64::zsub0 + I, DL, VT, SuperReg));
  CurDAG->RemoveDeadNode(N);
}

void AArch64DAGToDAGISel::SelectStore(SDNode *N, unsigned NumVecs,
                                      unsigned Opc) {
  SDLoc dl(N);
  EVT VT = N->getOperand(2)->getValueType(0);

  // Form a REG_SEQUENCE to force register allocation.
  bool Is128Bit = VT.getSizeInBits() == 128;
  SmallVector<SDValue, 4> Regs(N->op_begin() + 2, N->op_begin() + 2 + NumVecs);
  SDValue RegSeq = Is128Bit ? createQTuple(Regs) : createDTuple(Regs);

  SDValue Ops[] = {RegSeq, N->getOperand(NumVecs + 2), N->getOperand(0)};
  SDNode *St = CurDAG->getMachineNode(Opc, dl, N->getValueType(0), Ops);

  // Transfer memoperands.
  MachineMemOperand *MemOp = cast<MemIntrinsicSDNode>(N)->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(St), {MemOp});

  ReplaceNode(N, St);
}

void AArch64DAGToDAGISel::SelectPredicatedStore(SDNode *N, unsigned NumVecs,
                                                unsigned Scale, unsigned Opc_rr,
                                                unsigned Opc_ri) {
  SDLoc dl(N);

  // Form a REG_SEQUENCE to force register allocation.
  SmallVector<SDValue, 4> Regs(N->op_begin() + 2, N->op_begin() + 2 + NumVecs);
  SDValue RegSeq = createZTuple(Regs);

  // Optimize addressing mode.
  unsigned Opc;
  SDValue Offset, Base;
  std::tie(Opc, Base, Offset) = findAddrModeSVELoadStore(
      N, Opc_rr, Opc_ri, N->getOperand(NumVecs + 3),
      CurDAG->getTargetConstant(0, dl, MVT::i64), Scale);

  SDValue Ops[] = {RegSeq, N->getOperand(NumVecs + 2), // predicate
                   Base,                               // address
                   Offset,                             // offset
                   N->getOperand(0)};                  // chain
  SDNode *St = CurDAG->getMachineNode(Opc, dl, N->getValueType(0), Ops);

  ReplaceNode(N, St);
}

bool AArch64DAGToDAGISel::SelectAddrModeFrameIndexSVE(SDValue N, SDValue &Base,
                                                      SDValue &OffImm) {
  SDLoc dl(N);
  const DataLayout &DL = CurDAG->getDataLayout();
  const TargetLowering *TLI = getTargetLowering();

  // Try to match it for the frame address
  if (auto FINode = dyn_cast<FrameIndexSDNode>(N)) {
    int FI = FINode->getIndex();
    Base = CurDAG->getTargetFrameIndex(FI, TLI->getPointerTy(DL));
    OffImm = CurDAG->getTargetConstant(0, dl, MVT::i64);
    return true;
  }

  return false;
}

void AArch64DAGToDAGISel::SelectPostStore(SDNode *N, unsigned NumVecs,
                                          unsigned Opc) {
  SDLoc dl(N);
  EVT VT = N->getOperand(2)->getValueType(0);
  const EVT ResTys[] = {MVT::i64,    // Type of the write back register
                        MVT::Other}; // Type for the Chain

  // Form a REG_SEQUENCE to force register allocation.
  bool Is128Bit = VT.getSizeInBits() == 128;
  SmallVector<SDValue, 4> Regs(N->op_begin() + 1, N->op_begin() + 1 + NumVecs);
  SDValue RegSeq = Is128Bit ? createQTuple(Regs) : createDTuple(Regs);

  SDValue Ops[] = {RegSeq,
                   N->getOperand(NumVecs + 1), // base register
                   N->getOperand(NumVecs + 2), // Incremental
                   N->getOperand(0)};          // Chain
  SDNode *St = CurDAG->getMachineNode(Opc, dl, ResTys, Ops);

  ReplaceNode(N, St);
}

namespace {
/// WidenVector - Given a value in the V64 register class, produce the
/// equivalent value in the V128 register class.
class WidenVector {
  SelectionDAG &DAG;

public:
  WidenVector(SelectionDAG &DAG) : DAG(DAG) {}

  SDValue operator()(SDValue V64Reg) {
    EVT VT = V64Reg.getValueType();
    unsigned NarrowSize = VT.getVectorNumElements();
    MVT EltTy = VT.getVectorElementType().getSimpleVT();
    MVT WideTy = MVT::getVectorVT(EltTy, 2 * NarrowSize);
    SDLoc DL(V64Reg);

    SDValue Undef =
        SDValue(DAG.getMachineNode(TargetOpcode::IMPLICIT_DEF, DL, WideTy), 0);
    return DAG.getTargetInsertSubreg(AArch64::dsub, DL, WideTy, Undef, V64Reg);
  }
};
} // namespace

/// NarrowVector - Given a value in the V128 register class, produce the
/// equivalent value in the V64 register class.
static SDValue NarrowVector(SDValue V128Reg, SelectionDAG &DAG) {
  EVT VT = V128Reg.getValueType();
  unsigned WideSize = VT.getVectorNumElements();
  MVT EltTy = VT.getVectorElementType().getSimpleVT();
  MVT NarrowTy = MVT::getVectorVT(EltTy, WideSize / 2);

  return DAG.getTargetExtractSubreg(AArch64::dsub, SDLoc(V128Reg), NarrowTy,
                                    V128Reg);
}

void AArch64DAGToDAGISel::SelectLoadLane(SDNode *N, unsigned NumVecs,
                                         unsigned Opc) {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  bool Narrow = VT.getSizeInBits() == 64;

  // Form a REG_SEQUENCE to force register allocation.
  SmallVector<SDValue, 4> Regs(N->op_begin() + 2, N->op_begin() + 2 + NumVecs);

  if (Narrow)
    transform(Regs, Regs.begin(),
                   WidenVector(*CurDAG));

  SDValue RegSeq = createQTuple(Regs);

  const EVT ResTys[] = {MVT::Untyped, MVT::Other};

  unsigned LaneNo = N->getConstantOperandVal(NumVecs + 2);

  SDValue Ops[] = {RegSeq, CurDAG->getTargetConstant(LaneNo, dl, MVT::i64),
                   N->getOperand(NumVecs + 3), N->getOperand(0)};
  SDNode *Ld = CurDAG->getMachineNode(Opc, dl, ResTys, Ops);
  SDValue SuperReg = SDValue(Ld, 0);

  EVT WideVT = RegSeq.getOperand(1)->getValueType(0);
  static const unsigned QSubs[] = { AArch64::qsub0, AArch64::qsub1,
                                    AArch64::qsub2, AArch64::qsub3 };
  for (unsigned i = 0; i < NumVecs; ++i) {
    SDValue NV = CurDAG->getTargetExtractSubreg(QSubs[i], dl, WideVT, SuperReg);
    if (Narrow)
      NV = NarrowVector(NV, *CurDAG);
    ReplaceUses(SDValue(N, i), NV);
  }

  ReplaceUses(SDValue(N, NumVecs), SDValue(Ld, 1));
  CurDAG->RemoveDeadNode(N);
}

void AArch64DAGToDAGISel::SelectPostLoadLane(SDNode *N, unsigned NumVecs,
                                             unsigned Opc) {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  bool Narrow = VT.getSizeInBits() == 64;

  // Form a REG_SEQUENCE to force register allocation.
  SmallVector<SDValue, 4> Regs(N->op_begin() + 1, N->op_begin() + 1 + NumVecs);

  if (Narrow)
    transform(Regs, Regs.begin(),
                   WidenVector(*CurDAG));

  SDValue RegSeq = createQTuple(Regs);

  const EVT ResTys[] = {MVT::i64, // Type of the write back register
                        RegSeq->getValueType(0), MVT::Other};

  unsigned LaneNo = N->getConstantOperandVal(NumVecs + 1);

  SDValue Ops[] = {RegSeq,
                   CurDAG->getTargetConstant(LaneNo, dl,
                                             MVT::i64),         // Lane Number
                   N->getOperand(NumVecs + 2),                  // Base register
                   N->getOperand(NumVecs + 3),                  // Incremental
                   N->getOperand(0)};
  SDNode *Ld = CurDAG->getMachineNode(Opc, dl, ResTys, Ops);

  // Update uses of the write back register
  ReplaceUses(SDValue(N, NumVecs), SDValue(Ld, 0));

  // Update uses of the vector list
  SDValue SuperReg = SDValue(Ld, 1);
  if (NumVecs == 1) {
    ReplaceUses(SDValue(N, 0),
                Narrow ? NarrowVector(SuperReg, *CurDAG) : SuperReg);
  } else {
    EVT WideVT = RegSeq.getOperand(1)->getValueType(0);
    static const unsigned QSubs[] = { AArch64::qsub0, AArch64::qsub1,
                                      AArch64::qsub2, AArch64::qsub3 };
    for (unsigned i = 0; i < NumVecs; ++i) {
      SDValue NV = CurDAG->getTargetExtractSubreg(QSubs[i], dl, WideVT,
                                                  SuperReg);
      if (Narrow)
        NV = NarrowVector(NV, *CurDAG);
      ReplaceUses(SDValue(N, i), NV);
    }
  }

  // Update the Chain
  ReplaceUses(SDValue(N, NumVecs + 1), SDValue(Ld, 2));
  CurDAG->RemoveDeadNode(N);
}

void AArch64DAGToDAGISel::SelectStoreLane(SDNode *N, unsigned NumVecs,
                                          unsigned Opc) {
  SDLoc dl(N);
  EVT VT = N->getOperand(2)->getValueType(0);
  bool Narrow = VT.getSizeInBits() == 64;

  // Form a REG_SEQUENCE to force register allocation.
  SmallVector<SDValue, 4> Regs(N->op_begin() + 2, N->op_begin() + 2 + NumVecs);

  if (Narrow)
    transform(Regs, Regs.begin(),
                   WidenVector(*CurDAG));

  SDValue RegSeq = createQTuple(Regs);

  unsigned LaneNo = N->getConstantOperandVal(NumVecs + 2);

  SDValue Ops[] = {RegSeq, CurDAG->getTargetConstant(LaneNo, dl, MVT::i64),
                   N->getOperand(NumVecs + 3), N->getOperand(0)};
  SDNode *St = CurDAG->getMachineNode(Opc, dl, MVT::Other, Ops);

  // Transfer memoperands.
  MachineMemOperand *MemOp = cast<MemIntrinsicSDNode>(N)->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(St), {MemOp});

  ReplaceNode(N, St);
}

void AArch64DAGToDAGISel::SelectPostStoreLane(SDNode *N, unsigned NumVecs,
                                              unsigned Opc) {
  SDLoc dl(N);
  EVT VT = N->getOperand(2)->getValueType(0);
  bool Narrow = VT.getSizeInBits() == 64;

  // Form a REG_SEQUENCE to force register allocation.
  SmallVector<SDValue, 4> Regs(N->op_begin() + 1, N->op_begin() + 1 + NumVecs);

  if (Narrow)
    transform(Regs, Regs.begin(),
                   WidenVector(*CurDAG));

  SDValue RegSeq = createQTuple(Regs);

  const EVT ResTys[] = {MVT::i64, // Type of the write back register
                        MVT::Other};

  unsigned LaneNo = N->getConstantOperandVal(NumVecs + 1);

  SDValue Ops[] = {RegSeq, CurDAG->getTargetConstant(LaneNo, dl, MVT::i64),
                   N->getOperand(NumVecs + 2), // Base Register
                   N->getOperand(NumVecs + 3), // Incremental
                   N->getOperand(0)};
  SDNode *St = CurDAG->getMachineNode(Opc, dl, ResTys, Ops);

  // Transfer memoperands.
  MachineMemOperand *MemOp = cast<MemIntrinsicSDNode>(N)->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(St), {MemOp});

  ReplaceNode(N, St);
}

static bool isBitfieldExtractOpFromAnd(SelectionDAG *CurDAG, SDNode *N,
                                       unsigned &Opc, SDValue &Opd0,
                                       unsigned &LSB, unsigned &MSB,
                                       unsigned NumberOfIgnoredLowBits,
                                       bool BiggerPattern) {
  assert(N->getOpcode() == ISD::AND &&
         "N must be a AND operation to call this function");

  EVT VT = N->getValueType(0);

  // Here we can test the type of VT and return false when the type does not
  // match, but since it is done prior to that call in the current context
  // we turned that into an assert to avoid redundant code.
  assert((VT == MVT::i32 || VT == MVT::i64) &&
         "Type checking must have been done before calling this function");

  // FIXME: simplify-demanded-bits in DAGCombine will probably have
  // changed the AND node to a 32-bit mask operation. We'll have to
  // undo that as part of the transform here if we want to catch all
  // the opportunities.
  // Currently the NumberOfIgnoredLowBits argument helps to recover
  // from these situations when matching bigger pattern (bitfield insert).

  // For unsigned extracts, check for a shift right and mask
  uint64_t AndImm = 0;
  if (!isOpcWithIntImmediate(N, ISD::AND, AndImm))
    return false;

  const SDNode *Op0 = N->getOperand(0).getNode();

  // Because of simplify-demanded-bits in DAGCombine, the mask may have been
  // simplified. Try to undo that
  AndImm |= maskTrailingOnes<uint64_t>(NumberOfIgnoredLowBits);

  // The immediate is a mask of the low bits iff imm & (imm+1) == 0
  if (AndImm & (AndImm + 1))
    return false;

  bool ClampMSB = false;
  uint64_t SrlImm = 0;
  // Handle the SRL + ANY_EXTEND case.
  if (VT == MVT::i64 && Op0->getOpcode() == ISD::ANY_EXTEND &&
      isOpcWithIntImmediate(Op0->getOperand(0).getNode(), ISD::SRL, SrlImm)) {
    // Extend the incoming operand of the SRL to 64-bit.
    Opd0 = Widen(CurDAG, Op0->getOperand(0).getOperand(0));
    // Make sure to clamp the MSB so that we preserve the semantics of the
    // original operations.
    ClampMSB = true;
  } else if (VT == MVT::i32 && Op0->getOpcode() == ISD::TRUNCATE &&
             isOpcWithIntImmediate(Op0->getOperand(0).getNode(), ISD::SRL,
                                   SrlImm)) {
    // If the shift result was truncated, we can still combine them.
    Opd0 = Op0->getOperand(0).getOperand(0);

    // Use the type of SRL node.
    VT = Opd0->getValueType(0);
  } else if (isOpcWithIntImmediate(Op0, ISD::SRL, SrlImm)) {
    Opd0 = Op0->getOperand(0);
    ClampMSB = (VT == MVT::i32);
  } else if (BiggerPattern) {
    // Let's pretend a 0 shift right has been performed.
    // The resulting code will be at least as good as the original one
    // plus it may expose more opportunities for bitfield insert pattern.
    // FIXME: Currently we limit this to the bigger pattern, because
    // some optimizations expect AND and not UBFM.
    Opd0 = N->getOperand(0);
  } else
    return false;

  // Bail out on large immediates. This happens when no proper
  // combining/constant folding was performed.
  if (!BiggerPattern && (SrlImm <= 0 || SrlImm >= VT.getSizeInBits())) {
    LLVM_DEBUG(
        (dbgs() << N
                << ": Found large shift immediate, this should not happen\n"));
    return false;
  }

  LSB = SrlImm;
  MSB = SrlImm +
        (VT == MVT::i32 ? llvm::countr_one<uint32_t>(AndImm)
                        : llvm::countr_one<uint64_t>(AndImm)) -
        1;
  if (ClampMSB)
    // Since we're moving the extend before the right shift operation, we need
    // to clamp the MSB to make sure we don't shift in undefined bits instead of
    // the zeros which would get shifted in with the original right shift
    // operation.
    MSB = MSB > 31 ? 31 : MSB;

  Opc = VT == MVT::i32 ? AArch64::UBFMWri : AArch64::UBFMXri;
  return true;
}

static bool isBitfieldExtractOpFromSExtInReg(SDNode *N, unsigned &Opc,
                                             SDValue &Opd0, unsigned &Immr,
                                             unsigned &Imms) {
  assert(N->getOpcode() == ISD::SIGN_EXTEND_INREG);

  EVT VT = N->getValueType(0);
  unsigned BitWidth = VT.getSizeInBits();
  assert((VT == MVT::i32 || VT == MVT::i64) &&
         "Type checking must have been done before calling this function");

  SDValue Op = N->getOperand(0);
  if (Op->getOpcode() == ISD::TRUNCATE) {
    Op = Op->getOperand(0);
    VT = Op->getValueType(0);
    BitWidth = VT.getSizeInBits();
  }

  uint64_t ShiftImm;
  if (!isOpcWithIntImmediate(Op.getNode(), ISD::SRL, ShiftImm) &&
      !isOpcWithIntImmediate(Op.getNode(), ISD::SRA, ShiftImm))
    return false;

  unsigned Width = cast<VTSDNode>(N->getOperand(1))->getVT().getSizeInBits();
  if (ShiftImm + Width > BitWidth)
    return false;

  Opc = (VT == MVT::i32) ? AArch64::SBFMWri : AArch64::SBFMXri;
  Opd0 = Op.getOperand(0);
  Immr = ShiftImm;
  Imms = ShiftImm + Width - 1;
  return true;
}

static bool isSeveralBitsExtractOpFromShr(SDNode *N, unsigned &Opc,
                                          SDValue &Opd0, unsigned &LSB,
                                          unsigned &MSB) {
  // We are looking for the following pattern which basically extracts several
  // continuous bits from the source value and places it from the LSB of the
  // destination value, all other bits of the destination value or set to zero:
  //
  // Value2 = AND Value, MaskImm
  // SRL Value2, ShiftImm
  //
  // with MaskImm >> ShiftImm to search for the bit width.
  //
  // This gets selected into a single UBFM:
  //
  // UBFM Value, ShiftImm, Log2_64(MaskImm)
  //

  if (N->getOpcode() != ISD::SRL)
    return false;

  uint64_t AndMask = 0;
  if (!isOpcWithIntImmediate(N->getOperand(0).getNode(), ISD::AND, AndMask))
    return false;

  Opd0 = N->getOperand(0).getOperand(0);

  uint64_t SrlImm = 0;
  if (!isIntImmediate(N->getOperand(1), SrlImm))
    return false;

  // Check whether we really have several bits extract here.
  if (!isMask_64(AndMask >> SrlImm))
    return false;

  Opc = N->getValueType(0) == MVT::i32 ? AArch64::UBFMWri : AArch64::UBFMXri;
  LSB = SrlImm;
  MSB = llvm::Log2_64(AndMask);
  return true;
}

static bool isBitfieldExtractOpFromShr(SDNode *N, unsigned &Opc, SDValue &Opd0,
                                       unsigned &Immr, unsigned &Imms,
                                       bool BiggerPattern) {
  assert((N->getOpcode() == ISD::SRA || N->getOpcode() == ISD::SRL) &&
         "N must be a SHR/SRA operation to call this function");

  EVT VT = N->getValueType(0);

  // Here we can test the type of VT and return false when the type does not
  // match, but since it is done prior to that call in the current context
  // we turned that into an assert to avoid redundant code.
  assert((VT == MVT::i32 || VT == MVT::i64) &&
         "Type checking must have been done before calling this function");

  // Check for AND + SRL doing several bits extract.
  if (isSeveralBitsExtractOpFromShr(N, Opc, Opd0, Immr, Imms))
    return true;

  // We're looking for a shift of a shift.
  uint64_t ShlImm = 0;
  uint64_t TruncBits = 0;
  if (isOpcWithIntImmediate(N->getOperand(0).getNode(), ISD::SHL, ShlImm)) {
    Opd0 = N->getOperand(0).getOperand(0);
  } else if (VT == MVT::i32 && N->getOpcode() == ISD::SRL &&
             N->getOperand(0).getNode()->getOpcode() == ISD::TRUNCATE) {
    // We are looking for a shift of truncate. Truncate from i64 to i32 could
    // be considered as setting high 32 bits as zero. Our strategy here is to
    // always generate 64bit UBFM. This consistency will help the CSE pass
    // later find more redundancy.
    Opd0 = N->getOperand(0).getOperand(0);
    TruncBits = Opd0->getValueType(0).getSizeInBits() - VT.getSizeInBits();
    VT = Opd0.getValueType();
    assert(VT == MVT::i64 && "the promoted type should be i64");
  } else if (BiggerPattern) {
    // Let's pretend a 0 shift left has been performed.
    // FIXME: Currently we limit this to the bigger pattern case,
    // because some optimizations expect AND and not UBFM
    Opd0 = N->getOperand(0);
  } else
    return false;

  // Missing combines/constant folding may have left us with strange
  // constants.
  if (ShlImm >= VT.getSizeInBits()) {
    LLVM_DEBUG(
        (dbgs() << N
                << ": Found large shift immediate, this should not happen\n"));
    return false;
  }

  uint64_t SrlImm = 0;
  if (!isIntImmediate(N->getOperand(1), SrlImm))
    return false;

  assert(SrlImm > 0 && SrlImm < VT.getSizeInBits() &&
         "bad amount in shift node!");
  int immr = SrlImm - ShlImm;
  Immr = immr < 0 ? immr + VT.getSizeInBits() : immr;
  Imms = VT.getSizeInBits() - ShlImm - TruncBits - 1;
  // SRA requires a signed extraction
  if (VT == MVT::i32)
    Opc = N->getOpcode() == ISD::SRA ? AArch64::SBFMWri : AArch64::UBFMWri;
  else
    Opc = N->getOpcode() == ISD::SRA ? AArch64::SBFMXri : AArch64::UBFMXri;
  return true;
}

bool AArch64DAGToDAGISel::tryBitfieldExtractOpFromSExt(SDNode *N) {
  assert(N->getOpcode() == ISD::SIGN_EXTEND);

  EVT VT = N->getValueType(0);
  EVT NarrowVT = N->getOperand(0)->getValueType(0);
  if (VT != MVT::i64 || NarrowVT != MVT::i32)
    return false;

  uint64_t ShiftImm;
  SDValue Op = N->getOperand(0);
  if (!isOpcWithIntImmediate(Op.getNode(), ISD::SRA, ShiftImm))
    return false;

  SDLoc dl(N);
  // Extend the incoming operand of the shift to 64-bits.
  SDValue Opd0 = Widen(CurDAG, Op.getOperand(0));
  unsigned Immr = ShiftImm;
  unsigned Imms = NarrowVT.getSizeInBits() - 1;
  SDValue Ops[] = {Opd0, CurDAG->getTargetConstant(Immr, dl, VT),
                   CurDAG->getTargetConstant(Imms, dl, VT)};
  CurDAG->SelectNodeTo(N, AArch64::SBFMXri, VT, Ops);
  return true;
}

static bool isBitfieldExtractOp(SelectionDAG *CurDAG, SDNode *N, unsigned &Opc,
                                SDValue &Opd0, unsigned &Immr, unsigned &Imms,
                                unsigned NumberOfIgnoredLowBits = 0,
                                bool BiggerPattern = false) {
  if (N->getValueType(0) != MVT::i32 && N->getValueType(0) != MVT::i64)
    return false;

  switch (N->getOpcode()) {
  default:
    if (!N->isMachineOpcode())
      return false;
    break;
  case ISD::AND:
    return isBitfieldExtractOpFromAnd(CurDAG, N, Opc, Opd0, Immr, Imms,
                                      NumberOfIgnoredLowBits, BiggerPattern);
  case ISD::SRL:
  case ISD::SRA:
    return isBitfieldExtractOpFromShr(N, Opc, Opd0, Immr, Imms, BiggerPattern);

  case ISD::SIGN_EXTEND_INREG:
    return isBitfieldExtractOpFromSExtInReg(N, Opc, Opd0, Immr, Imms);
  }

  unsigned NOpc = N->getMachineOpcode();
  switch (NOpc) {
  default:
    return false;
  case AArch64::SBFMWri:
  case AArch64::UBFMWri:
  case AArch64::SBFMXri:
  case AArch64::UBFMXri:
    Opc = NOpc;
    Opd0 = N->getOperand(0);
    Immr = N->getConstantOperandVal(1);
    Imms = N->getConstantOperandVal(2);
    return true;
  }
  // Unreachable
  return false;
}

bool AArch64DAGToDAGISel::tryBitfieldExtractOp(SDNode *N) {
  unsigned Opc, Immr, Imms;
  SDValue Opd0;
  if (!isBitfieldExtractOp(CurDAG, N, Opc, Opd0, Immr, Imms))
    return false;

  EVT VT = N->getValueType(0);
  SDLoc dl(N);

  // If the bit extract operation is 64bit but the original type is 32bit, we
  // need to add one EXTRACT_SUBREG.
  if ((Opc == AArch64::SBFMXri || Opc == AArch64::UBFMXri) && VT == MVT::i32) {
    SDValue Ops64[] = {Opd0, CurDAG->getTargetConstant(Immr, dl, MVT::i64),
                       CurDAG->getTargetConstant(Imms, dl, MVT::i64)};

    SDNode *BFM = CurDAG->getMachineNode(Opc, dl, MVT::i64, Ops64);
    SDValue Inner = CurDAG->getTargetExtractSubreg(AArch64::sub_32, dl,
                                                   MVT::i32, SDValue(BFM, 0));
    ReplaceNode(N, Inner.getNode());
    return true;
  }

  SDValue Ops[] = {Opd0, CurDAG->getTargetConstant(Immr, dl, VT),
                   CurDAG->getTargetConstant(Imms, dl, VT)};
  CurDAG->SelectNodeTo(N, Opc, VT, Ops);
  return true;
}

/// Does DstMask form a complementary pair with the mask provided by
/// BitsToBeInserted, suitable for use in a BFI instruction. Roughly speaking,
/// this asks whether DstMask zeroes precisely those bits that will be set by
/// the other half.
static bool isBitfieldDstMask(uint64_t DstMask, const APInt &BitsToBeInserted,
                              unsigned NumberOfIgnoredHighBits, EVT VT) {
  assert((VT == MVT::i32 || VT == MVT::i64) &&
         "i32 or i64 mask type expected!");
  unsigned BitWidth = VT.getSizeInBits() - NumberOfIgnoredHighBits;

  APInt SignificantDstMask = APInt(BitWidth, DstMask);
  APInt SignificantBitsToBeInserted = BitsToBeInserted.zextOrTrunc(BitWidth);

  return (SignificantDstMask & SignificantBitsToBeInserted) == 0 &&
         (SignificantDstMask | SignificantBitsToBeInserted).isAllOnes();
}

// Look for bits that will be useful for later uses.
// A bit is consider useless as soon as it is dropped and never used
// before it as been dropped.
// E.g., looking for useful bit of x
// 1. y = x & 0x7
// 2. z = y >> 2
// After #1, x useful bits are 0x7, then the useful bits of x, live through
// y.
// After #2, the useful bits of x are 0x4.
// However, if x is used on an unpredicatable instruction, then all its bits
// are useful.
// E.g.
// 1. y = x & 0x7
// 2. z = y >> 2
// 3. str x, [@x]
static void getUsefulBits(SDValue Op, APInt &UsefulBits, unsigned Depth = 0);

static void getUsefulBitsFromAndWithImmediate(SDValue Op, APInt &UsefulBits,
                                              unsigned Depth) {
  uint64_t Imm =
      cast<const ConstantSDNode>(Op.getOperand(1).getNode())->getZExtValue();
  Imm = AArch64_AM::decodeLogicalImmediate(Imm, UsefulBits.getBitWidth());
  UsefulBits &= APInt(UsefulBits.getBitWidth(), Imm);
  getUsefulBits(Op, UsefulBits, Depth + 1);
}

static void getUsefulBitsFromBitfieldMoveOpd(SDValue Op, APInt &UsefulBits,
                                             uint64_t Imm, uint64_t MSB,
                                             unsigned Depth) {
  // inherit the bitwidth value
  APInt OpUsefulBits(UsefulBits);
  OpUsefulBits = 1;

  if (MSB >= Imm) {
    OpUsefulBits <<= MSB - Imm + 1;
    --OpUsefulBits;
    // The interesting part will be in the lower part of the result
    getUsefulBits(Op, OpUsefulBits, Depth + 1);
    // The interesting part was starting at Imm in the argument
    OpUsefulBits <<= Imm;
  } else {
    OpUsefulBits <<= MSB + 1;
    --OpUsefulBits;
    // The interesting part will be shifted in the result
    OpUsefulBits <<= OpUsefulBits.getBitWidth() - Imm;
    getUsefulBits(Op, OpUsefulBits, Depth + 1);
    // The interesting part was at zero in the argument
    OpUsefulBits.lshrInPlace(OpUsefulBits.getBitWidth() - Imm);
  }

  UsefulBits &= OpUsefulBits;
}

static void getUsefulBitsFromUBFM(SDValue Op, APInt &UsefulBits,
                                  unsigned Depth) {
  uint64_t Imm =
      cast<const ConstantSDNode>(Op.getOperand(1).getNode())->getZExtValue();
  uint64_t MSB =
      cast<const ConstantSDNode>(Op.getOperand(2).getNode())->getZExtValue();

  getUsefulBitsFromBitfieldMoveOpd(Op, UsefulBits, Imm, MSB, Depth);
}

static void getUsefulBitsFromOrWithShiftedReg(SDValue Op, APInt &UsefulBits,
                                              unsigned Depth) {
  uint64_t ShiftTypeAndValue =
      cast<const ConstantSDNode>(Op.getOperand(2).getNode())->getZExtValue();
  APInt Mask(UsefulBits);
  Mask.clearAllBits();
  Mask.flipAllBits();

  if (AArch64_AM::getShiftType(ShiftTypeAndValue) == AArch64_AM::LSL) {
    // Shift Left
    uint64_t ShiftAmt = AArch64_AM::getShiftValue(ShiftTypeAndValue);
    Mask <<= ShiftAmt;
    getUsefulBits(Op, Mask, Depth + 1);
    Mask.lshrInPlace(ShiftAmt);
  } else if (AArch64_AM::getShiftType(ShiftTypeAndValue) == AArch64_AM::LSR) {
    // Shift Right
    // We do not handle AArch64_AM::ASR, because the sign will change the
    // number of useful bits
    uint64_t ShiftAmt = AArch64_AM::getShiftValue(ShiftTypeAndValue);
    Mask.lshrInPlace(ShiftAmt);
    getUsefulBits(Op, Mask, Depth + 1);
    Mask <<= ShiftAmt;
  } else
    return;

  UsefulBits &= Mask;
}

static void getUsefulBitsFromBFM(SDValue Op, SDValue Orig, APInt &UsefulBits,
                                 unsigned Depth) {
  uint64_t Imm =
      cast<const ConstantSDNode>(Op.getOperand(2).getNode())->getZExtValue();
  uint64_t MSB =
      cast<const ConstantSDNode>(Op.getOperand(3).getNode())->getZExtValue();

  APInt OpUsefulBits(UsefulBits);
  OpUsefulBits = 1;

  APInt ResultUsefulBits(UsefulBits.getBitWidth(), 0);
  ResultUsefulBits.flipAllBits();
  APInt Mask(UsefulBits.getBitWidth(), 0);

  getUsefulBits(Op, ResultUsefulBits, Depth + 1);

  if (MSB >= Imm) {
    // The instruction is a BFXIL.
    uint64_t Width = MSB - Imm + 1;
    uint64_t LSB = Imm;

    OpUsefulBits <<= Width;
    --OpUsefulBits;

    if (Op.getOperand(1) == Orig) {
      // Copy the low bits from the result to bits starting from LSB.
      Mask = ResultUsefulBits & OpUsefulBits;
      Mask <<= LSB;
    }

    if (Op.getOperand(0) == Orig)
      // Bits starting from LSB in the input contribute to the result.
      Mask |= (ResultUsefulBits & ~OpUsefulBits);
  } else {
    // The instruction is a BFI.
    uint64_t Width = MSB + 1;
    uint64_t LSB = UsefulBits.getBitWidth() - Imm;

    OpUsefulBits <<= Width;
    --OpUsefulBits;
    OpUsefulBits <<= LSB;

    if (Op.getOperand(1) == Orig) {
      // Copy the bits from the result to the zero bits.
      Mask = ResultUsefulBits & OpUsefulBits;
      Mask.lshrInPlace(LSB);
    }

    if (Op.getOperand(0) == Orig)
      Mask |= (ResultUsefulBits & ~OpUsefulBits);
  }

  UsefulBits &= Mask;
}

static void getUsefulBitsForUse(SDNode *UserNode, APInt &UsefulBits,
                                SDValue Orig, unsigned Depth) {

  // Users of this node should have already been instruction selected
  // FIXME: Can we turn that into an assert?
  if (!UserNode->isMachineOpcode())
    return;

  switch (UserNode->getMachineOpcode()) {
  default:
    return;
  case AArch64::ANDSWri:
  case AArch64::ANDSXri:
  case AArch64::ANDWri:
  case AArch64::ANDXri:
    // We increment Depth only when we call the getUsefulBits
    return getUsefulBitsFromAndWithImmediate(SDValue(UserNode, 0), UsefulBits,
                                             Depth);
  case AArch64::UBFMWri:
  case AArch64::UBFMXri:
    return getUsefulBitsFromUBFM(SDValue(UserNode, 0), UsefulBits, Depth);

  case AArch64::ORRWrs:
  case AArch64::ORRXrs:
    if (UserNode->getOperand(0) != Orig && UserNode->getOperand(1) == Orig)
      getUsefulBitsFromOrWithShiftedReg(SDValue(UserNode, 0), UsefulBits,
                                        Depth);
    return;
  case AArch64::BFMWri:
  case AArch64::BFMXri:
    return getUsefulBitsFromBFM(SDValue(UserNode, 0), Orig, UsefulBits, Depth);

  case AArch64::STRBBui:
  case AArch64::STURBBi:
    if (UserNode->getOperand(0) != Orig)
      return;
    UsefulBits &= APInt(UsefulBits.getBitWidth(), 0xff);
    return;

  case AArch64::STRHHui:
  case AArch64::STURHHi:
    if (UserNode->getOperand(0) != Orig)
      return;
    UsefulBits &= APInt(UsefulBits.getBitWidth(), 0xffff);
    return;
  }
}

static void getUsefulBits(SDValue Op, APInt &UsefulBits, unsigned Depth) {
  if (Depth >= SelectionDAG::MaxRecursionDepth)
    return;
  // Initialize UsefulBits
  if (!Depth) {
    unsigned Bitwidth = Op.getScalarValueSizeInBits();
    // At the beginning, assume every produced bits is useful
    UsefulBits = APInt(Bitwidth, 0);
    UsefulBits.flipAllBits();
  }
  APInt UsersUsefulBits(UsefulBits.getBitWidth(), 0);

  for (SDNode *Node : Op.getNode()->uses()) {
    // A use cannot produce useful bits
    APInt UsefulBitsForUse = APInt(UsefulBits);
    getUsefulBitsForUse(Node, UsefulBitsForUse, Op, Depth);
    UsersUsefulBits |= UsefulBitsForUse;
  }
  // UsefulBits contains the produced bits that are meaningful for the
  // current definition, thus a user cannot make a bit meaningful at
  // this point
  UsefulBits &= UsersUsefulBits;
}

/// Create a machine node performing a notional SHL of Op by ShlAmount. If
/// ShlAmount is negative, do a (logical) right-shift instead. If ShlAmount is
/// 0, return Op unchanged.
static SDValue getLeftShift(SelectionDAG *CurDAG, SDValue Op, int ShlAmount) {
  if (ShlAmount == 0)
    return Op;

  EVT VT = Op.getValueType();
  SDLoc dl(Op);
  unsigned BitWidth = VT.getSizeInBits();
  unsigned UBFMOpc = BitWidth == 32 ? AArch64::UBFMWri : AArch64::UBFMXri;

  SDNode *ShiftNode;
  if (ShlAmount > 0) {
    // LSL wD, wN, #Amt == UBFM wD, wN, #32-Amt, #31-Amt
    ShiftNode = CurDAG->getMachineNode(
        UBFMOpc, dl, VT, Op,
        CurDAG->getTargetConstant(BitWidth - ShlAmount, dl, VT),
        CurDAG->getTargetConstant(BitWidth - 1 - ShlAmount, dl, VT));
  } else {
    // LSR wD, wN, #Amt == UBFM wD, wN, #Amt, #32-1
    assert(ShlAmount < 0 && "expected right shift");
    int ShrAmount = -ShlAmount;
    ShiftNode = CurDAG->getMachineNode(
        UBFMOpc, dl, VT, Op, CurDAG->getTargetConstant(ShrAmount, dl, VT),
        CurDAG->getTargetConstant(BitWidth - 1, dl, VT));
  }

  return SDValue(ShiftNode, 0);
}

// For bit-field-positioning pattern "(and (shl VAL, N), ShiftedMask)".
static bool isBitfieldPositioningOpFromAnd(SelectionDAG *CurDAG, SDValue Op,
                                           bool BiggerPattern,
                                           const uint64_t NonZeroBits,
                                           SDValue &Src, int &DstLSB,
                                           int &Width);

// For bit-field-positioning pattern "shl VAL, N)".
static bool isBitfieldPositioningOpFromShl(SelectionDAG *CurDAG, SDValue Op,
                                           bool BiggerPattern,
                                           const uint64_t NonZeroBits,
                                           SDValue &Src, int &DstLSB,
                                           int &Width);

/// Does this tree qualify as an attempt to move a bitfield into position,
/// essentially "(and (shl VAL, N), Mask)" or (shl VAL, N).
static bool isBitfieldPositioningOp(SelectionDAG *CurDAG, SDValue Op,
                                    bool BiggerPattern, SDValue &Src,
                                    int &DstLSB, int &Width) {
  EVT VT = Op.getValueType();
  unsigned BitWidth = VT.getSizeInBits();
  (void)BitWidth;
  assert(BitWidth == 32 || BitWidth == 64);

  KnownBits Known = CurDAG->computeKnownBits(Op);

  // Non-zero in the sense that they're not provably zero, which is the key
  // point if we want to use this value
  const uint64_t NonZeroBits = (~Known.Zero).getZExtValue();
  if (!isShiftedMask_64(NonZeroBits))
    return false;

  switch (Op.getOpcode()) {
  default:
    break;
  case ISD::AND:
    return isBitfieldPositioningOpFromAnd(CurDAG, Op, BiggerPattern,
                                          NonZeroBits, Src, DstLSB, Width);
  case ISD::SHL:
    return isBitfieldPositioningOpFromShl(CurDAG, Op, BiggerPattern,
                                          NonZeroBits, Src, DstLSB, Width);
  }

  return false;
}

static bool isBitfieldPositioningOpFromAnd(SelectionDAG *CurDAG, SDValue Op,
                                           bool BiggerPattern,
                                           const uint64_t NonZeroBits,
                                           SDValue &Src, int &DstLSB,
                                           int &Width) {
  assert(isShiftedMask_64(NonZeroBits) && "Caller guaranteed");

  EVT VT = Op.getValueType();
  assert((VT == MVT::i32 || VT == MVT::i64) &&
         "Caller guarantees VT is one of i32 or i64");
  (void)VT;

  uint64_t AndImm;
  if (!isOpcWithIntImmediate(Op.getNode(), ISD::AND, AndImm))
    return false;

  // If (~AndImm & NonZeroBits) is not zero at POS, we know that
  //   1) (AndImm & (1 << POS) == 0)
  //   2) the result of AND is not zero at POS bit (according to NonZeroBits)
  //
  // 1) and 2) don't agree so something must be wrong (e.g., in
  // 'SelectionDAG::computeKnownBits')
  assert((~AndImm & NonZeroBits) == 0 &&
         "Something must be wrong (e.g., in SelectionDAG::computeKnownBits)");

  SDValue AndOp0 = Op.getOperand(0);

  uint64_t ShlImm;
  SDValue ShlOp0;
  if (isOpcWithIntImmediate(AndOp0.getNode(), ISD::SHL, ShlImm)) {
    // For pattern "and(shl(val, N), shifted-mask)", 'ShlOp0' is set to 'val'.
    ShlOp0 = AndOp0.getOperand(0);
  } else if (VT == MVT::i64 && AndOp0.getOpcode() == ISD::ANY_EXTEND &&
             isOpcWithIntImmediate(AndOp0.getOperand(0).getNode(), ISD::SHL,
                                   ShlImm)) {
    // For pattern "and(any_extend(shl(val, N)), shifted-mask)"

    // ShlVal == shl(val, N), which is a left shift on a smaller type.
    SDValue ShlVal = AndOp0.getOperand(0);

    // Since this is after type legalization and ShlVal is extended to MVT::i64,
    // expect VT to be MVT::i32.
    assert((ShlVal.getValueType() == MVT::i32) && "Expect VT to be MVT::i32.");

    // Widens 'val' to MVT::i64 as the source of bit field positioning.
    ShlOp0 = Widen(CurDAG, ShlVal.getOperand(0));
  } else
    return false;

  // For !BiggerPattern, bail out if the AndOp0 has more than one use, since
  // then we'll end up generating AndOp0+UBFIZ instead of just keeping
  // AndOp0+AND.
  if (!BiggerPattern && !AndOp0.hasOneUse())
    return false;

  DstLSB = llvm::countr_zero(NonZeroBits);
  Width = llvm::countr_one(NonZeroBits >> DstLSB);

  // Bail out on large Width. This happens when no proper combining / constant
  // folding was performed.
  if (Width >= (int)VT.getSizeInBits()) {
    // If VT is i64, Width > 64 is insensible since NonZeroBits is uint64_t, and
    // Width == 64 indicates a missed dag-combine from "(and val, AllOnes)" to
    // "val".
    // If VT is i32, what Width >= 32 means:
    // - For "(and (any_extend(shl val, N)), shifted-mask)", the`and` Op
    //   demands at least 'Width' bits (after dag-combiner). This together with
    //   `any_extend` Op (undefined higher bits) indicates missed combination
    //   when lowering the 'and' IR instruction to an machine IR instruction.
    LLVM_DEBUG(
        dbgs()
        << "Found large Width in bit-field-positioning -- this indicates no "
           "proper combining / constant folding was performed\n");
    return false;
  }

  // BFI encompasses sufficiently many nodes that it's worth inserting an extra
  // LSL/LSR if the mask in NonZeroBits doesn't quite match up with the ISD::SHL
  // amount.  BiggerPattern is true when this pattern is being matched for BFI,
  // BiggerPattern is false when this pattern is being matched for UBFIZ, in
  // which case it is not profitable to insert an extra shift.
  if (ShlImm != uint64_t(DstLSB) && !BiggerPattern)
    return false;

  Src = getLeftShift(CurDAG, ShlOp0, ShlImm - DstLSB);
  return true;
}

// For node (shl (and val, mask), N)), returns true if the node is equivalent to
// UBFIZ.
static bool isSeveralBitsPositioningOpFromShl(const uint64_t ShlImm, SDValue Op,
                                              SDValue &Src, int &DstLSB,
                                              int &Width) {
  // Caller should have verified that N is a left shift with constant shift
  // amount; asserts that.
  assert(Op.getOpcode() == ISD::SHL &&
         "Op.getNode() should be a SHL node to call this function");
  assert(isIntImmediateEq(Op.getOperand(1), ShlImm) &&
         "Op.getNode() should shift ShlImm to call this function");

  uint64_t AndImm = 0;
  SDValue Op0 = Op.getOperand(0);
  if (!isOpcWithIntImmediate(Op0.getNode(), ISD::AND, AndImm))
    return false;

  const uint64_t ShiftedAndImm = ((AndImm << ShlImm) >> ShlImm);
  if (isMask_64(ShiftedAndImm)) {
    // AndImm is a superset of (AllOnes >> ShlImm); in other words, AndImm
    // should end with Mask, and could be prefixed with random bits if those
    // bits are shifted out.
    //
    // For example, xyz11111 (with {x,y,z} being 0 or 1) is fine if ShlImm >= 3;
    // the AND result corresponding to those bits are shifted out, so it's fine
    // to not extract them.
    Width = llvm::countr_one(ShiftedAndImm);
    DstLSB = ShlImm;
    Src = Op0.getOperand(0);
    return true;
  }
  return false;
}

static bool isBitfieldPositioningOpFromShl(SelectionDAG *CurDAG, SDValue Op,
                                           bool BiggerPattern,
                                           const uint64_t NonZeroBits,
                                           SDValue &Src, int &DstLSB,
                                           int &Width) {
  assert(isShiftedMask_64(NonZeroBits) && "Caller guaranteed");

  EVT VT = Op.getValueType();
  assert((VT == MVT::i32 || VT == MVT::i64) &&
         "Caller guarantees that type is i32 or i64");
  (void)VT;

  uint64_t ShlImm;
  if (!isOpcWithIntImmediate(Op.getNode(), ISD::SHL, ShlImm))
    return false;

  if (!BiggerPattern && !Op.hasOneUse())
    return false;

  if (isSeveralBitsPositioningOpFromShl(ShlImm, Op, Src, DstLSB, Width))
    return true;

  DstLSB = llvm::countr_zero(NonZeroBits);
  Width = llvm::countr_one(NonZeroBits >> DstLSB);

  if (ShlImm != uint64_t(DstLSB) && !BiggerPattern)
    return false;

  Src = getLeftShift(CurDAG, Op.getOperand(0), ShlImm - DstLSB);
  return true;
}

static bool isShiftedMask(uint64_t Mask, EVT VT) {
  assert(VT == MVT::i32 || VT == MVT::i64);
  if (VT == MVT::i32)
    return isShiftedMask_32(Mask);
  return isShiftedMask_64(Mask);
}

// Generate a BFI/BFXIL from 'or (and X, MaskImm), OrImm' iff the value being
// inserted only sets known zero bits.
static bool tryBitfieldInsertOpFromOrAndImm(SDNode *N, SelectionDAG *CurDAG) {
  assert(N->getOpcode() == ISD::OR && "Expect a OR operation");

  EVT VT = N->getValueType(0);
  if (VT != MVT::i32 && VT != MVT::i64)
    return false;

  unsigned BitWidth = VT.getSizeInBits();

  uint64_t OrImm;
  if (!isOpcWithIntImmediate(N, ISD::OR, OrImm))
    return false;

  // Skip this transformation if the ORR immediate can be encoded in the ORR.
  // Otherwise, we'll trade an AND+ORR for ORR+BFI/BFXIL, which is most likely
  // performance neutral.
  if (AArch64_AM::isLogicalImmediate(OrImm, BitWidth))
    return false;

  uint64_t MaskImm;
  SDValue And = N->getOperand(0);
  // Must be a single use AND with an immediate operand.
  if (!And.hasOneUse() ||
      !isOpcWithIntImmediate(And.getNode(), ISD::AND, MaskImm))
    return false;

  // Compute the Known Zero for the AND as this allows us to catch more general
  // cases than just looking for AND with imm.
  KnownBits Known = CurDAG->computeKnownBits(And);

  // Non-zero in the sense that they're not provably zero, which is the key
  // point if we want to use this value.
  uint64_t NotKnownZero = (~Known.Zero).getZExtValue();

  // The KnownZero mask must be a shifted mask (e.g., 1110..011, 11100..00).
  if (!isShiftedMask(Known.Zero.getZExtValue(), VT))
    return false;

  // The bits being inserted must only set those bits that are known to be zero.
  if ((OrImm & NotKnownZero) != 0) {
    // FIXME:  It's okay if the OrImm sets NotKnownZero bits to 1, but we don't
    // currently handle this case.
    return false;
  }

  // BFI/BFXIL dst, src, #lsb, #width.
  int LSB = llvm::countr_one(NotKnownZero);
  int Width = BitWidth - APInt(BitWidth, NotKnownZero).popcount();

  // BFI/BFXIL is an alias of BFM, so translate to BFM operands.
  unsigned ImmR = (BitWidth - LSB) % BitWidth;
  unsigned ImmS = Width - 1;

  // If we're creating a BFI instruction avoid cases where we need more
  // instructions to materialize the BFI constant as compared to the original
  // ORR.  A BFXIL will use the same constant as the original ORR, so the code
  // should be no worse in this case.
  bool IsBFI = LSB != 0;
  uint64_t BFIImm = OrImm >> LSB;
  if (IsBFI && !AArch64_AM::isLogicalImmediate(BFIImm, BitWidth)) {
    // We have a BFI instruction and we know the constant can't be materialized
    // with a ORR-immediate with the zero register.
    unsigned OrChunks = 0, BFIChunks = 0;
    for (unsigned Shift = 0; Shift < BitWidth; Shift += 16) {
      if (((OrImm >> Shift) & 0xFFFF) != 0)
        ++OrChunks;
      if (((BFIImm >> Shift) & 0xFFFF) != 0)
        ++BFIChunks;
    }
    if (BFIChunks > OrChunks)
      return false;
  }

  // Materialize the constant to be inserted.
  SDLoc DL(N);
  unsigned MOVIOpc = VT == MVT::i32 ? AArch64::MOVi32imm : AArch64::MOVi64imm;
  SDNode *MOVI = CurDAG->getMachineNode(
      MOVIOpc, DL, VT, CurDAG->getTargetConstant(BFIImm, DL, VT));

  // Create the BFI/BFXIL instruction.
  SDValue Ops[] = {And.getOperand(0), SDValue(MOVI, 0),
                   CurDAG->getTargetConstant(ImmR, DL, VT),
                   CurDAG->getTargetConstant(ImmS, DL, VT)};
  unsigned Opc = (VT == MVT::i32) ? AArch64::BFMWri : AArch64::BFMXri;
  CurDAG->SelectNodeTo(N, Opc, VT, Ops);
  return true;
}

static bool isWorthFoldingIntoOrrWithShift(SDValue Dst, SelectionDAG *CurDAG,
                                           SDValue &ShiftedOperand,
                                           uint64_t &EncodedShiftImm) {
  // Avoid folding Dst into ORR-with-shift if Dst has other uses than ORR.
  if (!Dst.hasOneUse())
    return false;

  EVT VT = Dst.getValueType();
  assert((VT == MVT::i32 || VT == MVT::i64) &&
         "Caller should guarantee that VT is one of i32 or i64");
  const unsigned SizeInBits = VT.getSizeInBits();

  SDLoc DL(Dst.getNode());
  uint64_t AndImm, ShlImm;
  if (isOpcWithIntImmediate(Dst.getNode(), ISD::AND, AndImm) &&
      isShiftedMask_64(AndImm)) {
    // Avoid transforming 'DstOp0' if it has other uses than the AND node.
    SDValue DstOp0 = Dst.getOperand(0);
    if (!DstOp0.hasOneUse())
      return false;

    // An example to illustrate the transformation
    // From:
    //    lsr     x8, x1, #1
    //    and     x8, x8, #0x3f80
    //    bfxil   x8, x1, #0, #7
    // To:
    //    and    x8, x23, #0x7f
    //    ubfx   x9, x23, #8, #7
    //    orr    x23, x8, x9, lsl #7
    //
    // The number of instructions remains the same, but ORR is faster than BFXIL
    // on many AArch64 processors (or as good as BFXIL if not faster). Besides,
    // the dependency chain is improved after the transformation.
    uint64_t SrlImm;
    if (isOpcWithIntImmediate(DstOp0.getNode(), ISD::SRL, SrlImm)) {
      uint64_t NumTrailingZeroInShiftedMask = llvm::countr_zero(AndImm);
      if ((SrlImm + NumTrailingZeroInShiftedMask) < SizeInBits) {
        unsigned MaskWidth =
            llvm::countr_one(AndImm >> NumTrailingZeroInShiftedMask);
        unsigned UBFMOpc =
            (VT == MVT::i32) ? AArch64::UBFMWri : AArch64::UBFMXri;
        SDNode *UBFMNode = CurDAG->getMachineNode(
            UBFMOpc, DL, VT, DstOp0.getOperand(0),
            CurDAG->getTargetConstant(SrlImm + NumTrailingZeroInShiftedMask, DL,
                                      VT),
            CurDAG->getTargetConstant(
                SrlImm + NumTrailingZeroInShiftedMask + MaskWidth - 1, DL, VT));
        ShiftedOperand = SDValue(UBFMNode, 0);
        EncodedShiftImm = AArch64_AM::getShifterImm(
            AArch64_AM::LSL, NumTrailingZeroInShiftedMask);
        return true;
      }
    }
    return false;
  }

  if (isOpcWithIntImmediate(Dst.getNode(), ISD::SHL, ShlImm)) {
    ShiftedOperand = Dst.getOperand(0);
    EncodedShiftImm = AArch64_AM::getShifterImm(AArch64_AM::LSL, ShlImm);
    return true;
  }

  uint64_t SrlImm;
  if (isOpcWithIntImmediate(Dst.getNode(), ISD::SRL, SrlImm)) {
    ShiftedOperand = Dst.getOperand(0);
    EncodedShiftImm = AArch64_AM::getShifterImm(AArch64_AM::LSR, SrlImm);
    return true;
  }
  return false;
}

// Given an 'ISD::OR' node that is going to be selected as BFM, analyze
// the operands and select it to AArch64::ORR with shifted registers if
// that's more efficient. Returns true iff selection to AArch64::ORR happens.
static bool tryOrrWithShift(SDNode *N, SDValue OrOpd0, SDValue OrOpd1,
                            SDValue Src, SDValue Dst, SelectionDAG *CurDAG,
                            const bool BiggerPattern) {
  EVT VT = N->getValueType(0);
  assert(N->getOpcode() == ISD::OR && "Expect N to be an OR node");
  assert(((N->getOperand(0) == OrOpd0 && N->getOperand(1) == OrOpd1) ||
          (N->getOperand(1) == OrOpd0 && N->getOperand(0) == OrOpd1)) &&
         "Expect OrOpd0 and OrOpd1 to be operands of ISD::OR");
  assert((VT == MVT::i32 || VT == MVT::i64) &&
         "Expect result type to be i32 or i64 since N is combinable to BFM");
  SDLoc DL(N);

  // Bail out if BFM simplifies away one node in BFM Dst.
  if (OrOpd1 != Dst)
    return false;

  const unsigned OrrOpc = (VT == MVT::i32) ? AArch64::ORRWrs : AArch64::ORRXrs;
  // For "BFM Rd, Rn, #immr, #imms", it's known that BFM simplifies away fewer
  // nodes from Rn (or inserts additional shift node) if BiggerPattern is true.
  if (BiggerPattern) {
    uint64_t SrcAndImm;
    if (isOpcWithIntImmediate(OrOpd0.getNode(), ISD::AND, SrcAndImm) &&
        isMask_64(SrcAndImm) && OrOpd0.getOperand(0) == Src) {
      // OrOpd0 = AND Src, #Mask
      // So BFM simplifies away one AND node from Src and doesn't simplify away
      // nodes from Dst. If ORR with left-shifted operand also simplifies away
      // one node (from Rd), ORR is better since it has higher throughput and
      // smaller latency than BFM on many AArch64 processors (and for the rest
      // ORR is at least as good as BFM).
      SDValue ShiftedOperand;
      uint64_t EncodedShiftImm;
      if (isWorthFoldingIntoOrrWithShift(Dst, CurDAG, ShiftedOperand,
                                         EncodedShiftImm)) {
        SDValue Ops[] = {OrOpd0, ShiftedOperand,
                         CurDAG->getTargetConstant(EncodedShiftImm, DL, VT)};
        CurDAG->SelectNodeTo(N, OrrOpc, VT, Ops);
        return true;
      }
    }
    return false;
  }

  assert((!BiggerPattern) && "BiggerPattern should be handled above");

  uint64_t ShlImm;
  if (isOpcWithIntImmediate(OrOpd0.getNode(), ISD::SHL, ShlImm)) {
    if (OrOpd0.getOperand(0) == Src && OrOpd0.hasOneUse()) {
      SDValue Ops[] = {
          Dst, Src,
          CurDAG->getTargetConstant(
              AArch64_AM::getShifterImm(AArch64_AM::LSL, ShlImm), DL, VT)};
      CurDAG->SelectNodeTo(N, OrrOpc, VT, Ops);
      return true;
    }

    // Select the following pattern to left-shifted operand rather than BFI.
    // %val1 = op ..
    // %val2 = shl %val1, #imm
    // %res = or %val1, %val2
    //
    // If N is selected to be BFI, we know that
    // 1) OrOpd0 would be the operand from which extract bits (i.e., folded into
    // BFI) 2) OrOpd1 would be the destination operand (i.e., preserved)
    //
    // Instead of selecting N to BFI, fold OrOpd0 as a left shift directly.
    if (OrOpd0.getOperand(0) == OrOpd1) {
      SDValue Ops[] = {
          OrOpd1, OrOpd1,
          CurDAG->getTargetConstant(
              AArch64_AM::getShifterImm(AArch64_AM::LSL, ShlImm), DL, VT)};
      CurDAG->SelectNodeTo(N, OrrOpc, VT, Ops);
      return true;
    }
  }

  uint64_t SrlImm;
  if (isOpcWithIntImmediate(OrOpd0.getNode(), ISD::SRL, SrlImm)) {
    // Select the following pattern to right-shifted operand rather than BFXIL.
    // %val1 = op ..
    // %val2 = lshr %val1, #imm
    // %res = or %val1, %val2
    //
    // If N is selected to be BFXIL, we know that
    // 1) OrOpd0 would be the operand from which extract bits (i.e., folded into
    // BFXIL) 2) OrOpd1 would be the destination operand (i.e., preserved)
    //
    // Instead of selecting N to BFXIL, fold OrOpd0 as a right shift directly.
    if (OrOpd0.getOperand(0) == OrOpd1) {
      SDValue Ops[] = {
          OrOpd1, OrOpd1,
          CurDAG->getTargetConstant(
              AArch64_AM::getShifterImm(AArch64_AM::LSR, SrlImm), DL, VT)};
      CurDAG->SelectNodeTo(N, OrrOpc, VT, Ops);
      return true;
    }
  }

  return false;
}

static bool tryBitfieldInsertOpFromOr(SDNode *N, const APInt &UsefulBits,
                                      SelectionDAG *CurDAG) {
  assert(N->getOpcode() == ISD::OR && "Expect a OR operation");

  EVT VT = N->getValueType(0);
  if (VT != MVT::i32 && VT != MVT::i64)
    return false;

  unsigned BitWidth = VT.getSizeInBits();

  // Because of simplify-demanded-bits in DAGCombine, involved masks may not
  // have the expected shape. Try to undo that.

  unsigned NumberOfIgnoredLowBits = UsefulBits.countr_zero();
  unsigned NumberOfIgnoredHighBits = UsefulBits.countl_zero();

  // Given a OR operation, check if we have the following pattern
  // ubfm c, b, imm, imm2 (or something that does the same jobs, see
  //                       isBitfieldExtractOp)
  // d = e & mask2 ; where mask is a binary sequence of 1..10..0 and
  //                 countTrailingZeros(mask2) == imm2 - imm + 1
  // f = d | c
  // if yes, replace the OR instruction with:
  // f = BFM Opd0, Opd1, LSB, MSB ; where LSB = imm, and MSB = imm2

  // OR is commutative, check all combinations of operand order and values of
  // BiggerPattern, i.e.
  //     Opd0, Opd1, BiggerPattern=false
  //     Opd1, Opd0, BiggerPattern=false
  //     Opd0, Opd1, BiggerPattern=true
  //     Opd1, Opd0, BiggerPattern=true
  // Several of these combinations may match, so check with BiggerPattern=false
  // first since that will produce better results by matching more instructions
  // and/or inserting fewer extra instructions.
  for (int I = 0; I < 4; ++I) {

    SDValue Dst, Src;
    unsigned ImmR, ImmS;
    bool BiggerPattern = I / 2;
    SDValue OrOpd0Val = N->getOperand(I % 2);
    SDNode *OrOpd0 = OrOpd0Val.getNode();
    SDValue OrOpd1Val = N->getOperand((I + 1) % 2);
    SDNode *OrOpd1 = OrOpd1Val.getNode();

    unsigned BFXOpc;
    int DstLSB, Width;
    if (isBitfieldExtractOp(CurDAG, OrOpd0, BFXOpc, Src, ImmR, ImmS,
                            NumberOfIgnoredLowBits, BiggerPattern)) {
      // Check that the returned opcode is compatible with the pattern,
      // i.e., same type and zero extended (U and not S)
      if ((BFXOpc != AArch64::UBFMXri && VT == MVT::i64) ||
          (BFXOpc != AArch64::UBFMWri && VT == MVT::i32))
        continue;

      // Compute the width of the bitfield insertion
      DstLSB = 0;
      Width = ImmS - ImmR + 1;
      // FIXME: This constraint is to catch bitfield insertion we may
      // want to widen the pattern if we want to grab general bitfied
      // move case
      if (Width <= 0)
        continue;

      // If the mask on the insertee is correct, we have a BFXIL operation. We
      // can share the ImmR and ImmS values from the already-computed UBFM.
    } else if (isBitfieldPositioningOp(CurDAG, OrOpd0Val,
                                       BiggerPattern,
                                       Src, DstLSB, Width)) {
      ImmR = (BitWidth - DstLSB) % BitWidth;
      ImmS = Width - 1;
    } else
      continue;

    // Check the second part of the pattern
    EVT VT = OrOpd1Val.getValueType();
    assert((VT == MVT::i32 || VT == MVT::i64) && "unexpected OR operand");

    // Compute the Known Zero for the candidate of the first operand.
    // This allows to catch more general case than just looking for
    // AND with imm. Indeed, simplify-demanded-bits may have removed
    // the AND instruction because it proves it was useless.
    KnownBits Known = CurDAG->computeKnownBits(OrOpd1Val);

    // Check if there is enough room for the second operand to appear
    // in the first one
    APInt BitsToBeInserted =
        APInt::getBitsSet(Known.getBitWidth(), DstLSB, DstLSB + Width);

    if ((BitsToBeInserted & ~Known.Zero) != 0)
      continue;

    // Set the first operand
    uint64_t Imm;
    if (isOpcWithIntImmediate(OrOpd1, ISD::AND, Imm) &&
        isBitfieldDstMask(Imm, BitsToBeInserted, NumberOfIgnoredHighBits, VT))
      // In that case, we can eliminate the AND
      Dst = OrOpd1->getOperand(0);
    else
      // Maybe the AND has been removed by simplify-demanded-bits
      // or is useful because it discards more bits
      Dst = OrOpd1Val;

    // Before selecting ISD::OR node to AArch64::BFM, see if an AArch64::ORR
    // with shifted operand is more efficient.
    if (tryOrrWithShift(N, OrOpd0Val, OrOpd1Val, Src, Dst, CurDAG,
                        BiggerPattern))
      return true;

    // both parts match
    SDLoc DL(N);
    SDValue Ops[] = {Dst, Src, CurDAG->getTargetConstant(ImmR, DL, VT),
                     CurDAG->getTargetConstant(ImmS, DL, VT)};
    unsigned Opc = (VT == MVT::i32) ? AArch64::BFMWri : AArch64::BFMXri;
    CurDAG->SelectNodeTo(N, Opc, VT, Ops);
    return true;
  }

  // Generate a BFXIL from 'or (and X, Mask0Imm), (and Y, Mask1Imm)' iff
  // Mask0Imm and ~Mask1Imm are equivalent and one of the MaskImms is a shifted
  // mask (e.g., 0x000ffff0).
  uint64_t Mask0Imm, Mask1Imm;
  SDValue And0 = N->getOperand(0);
  SDValue And1 = N->getOperand(1);
  if (And0.hasOneUse() && And1.hasOneUse() &&
      isOpcWithIntImmediate(And0.getNode(), ISD::AND, Mask0Imm) &&
      isOpcWithIntImmediate(And1.getNode(), ISD::AND, Mask1Imm) &&
      APInt(BitWidth, Mask0Imm) == ~APInt(BitWidth, Mask1Imm) &&
      (isShiftedMask(Mask0Imm, VT) || isShiftedMask(Mask1Imm, VT))) {

    // ORR is commutative, so canonicalize to the form 'or (and X, Mask0Imm),
    // (and Y, Mask1Imm)' where Mask1Imm is the shifted mask masking off the
    // bits to be inserted.
    if (isShiftedMask(Mask0Imm, VT)) {
      std::swap(And0, And1);
      std::swap(Mask0Imm, Mask1Imm);
    }

    SDValue Src = And1->getOperand(0);
    SDValue Dst = And0->getOperand(0);
    unsigned LSB = llvm::countr_zero(Mask1Imm);
    int Width = BitWidth - APInt(BitWidth, Mask0Imm).popcount();

    // The BFXIL inserts the low-order bits from a source register, so right
    // shift the needed bits into place.
    SDLoc DL(N);
    unsigned ShiftOpc = (VT == MVT::i32) ? AArch64::UBFMWri : AArch64::UBFMXri;
    uint64_t LsrImm = LSB;
    if (Src->hasOneUse() &&
        isOpcWithIntImmediate(Src.getNode(), ISD::SRL, LsrImm) &&
        (LsrImm + LSB) < BitWidth) {
      Src = Src->getOperand(0);
      LsrImm += LSB;
    }

    SDNode *LSR = CurDAG->getMachineNode(
        ShiftOpc, DL, VT, Src, CurDAG->getTargetConstant(LsrImm, DL, VT),
        CurDAG->getTargetConstant(BitWidth - 1, DL, VT));

    // BFXIL is an alias of BFM, so translate to BFM operands.
    unsigned ImmR = (BitWidth - LSB) % BitWidth;
    unsigned ImmS = Width - 1;

    // Create the BFXIL instruction.
    SDValue Ops[] = {Dst, SDValue(LSR, 0),
                     CurDAG->getTargetConstant(ImmR, DL, VT),
                     CurDAG->getTargetConstant(ImmS, DL, VT)};
    unsigned Opc = (VT == MVT::i32) ? AArch64::BFMWri : AArch64::BFMXri;
    CurDAG->SelectNodeTo(N, Opc, VT, Ops);
    return true;
  }

  return false;
}

bool AArch64DAGToDAGISel::tryBitfieldInsertOp(SDNode *N) {
  if (N->getOpcode() != ISD::OR)
    return false;

  APInt NUsefulBits;
  getUsefulBits(SDValue(N, 0), NUsefulBits);

  // If all bits are not useful, just return UNDEF.
  if (!NUsefulBits) {
    CurDAG->SelectNodeTo(N, TargetOpcode::IMPLICIT_DEF, N->getValueType(0));
    return true;
  }

  if (tryBitfieldInsertOpFromOr(N, NUsefulBits, CurDAG))
    return true;

  return tryBitfieldInsertOpFromOrAndImm(N, CurDAG);
}

/// SelectBitfieldInsertInZeroOp - Match a UBFIZ instruction that is the
/// equivalent of a left shift by a constant amount followed by an and masking
/// out a contiguous set of bits.
bool AArch64DAGToDAGISel::tryBitfieldInsertInZeroOp(SDNode *N) {
  if (N->getOpcode() != ISD::AND)
    return false;

  EVT VT = N->getValueType(0);
  if (VT != MVT::i32 && VT != MVT::i64)
    return false;

  SDValue Op0;
  int DstLSB, Width;
  if (!isBitfieldPositioningOp(CurDAG, SDValue(N, 0), /*BiggerPattern=*/false,
                               Op0, DstLSB, Width))
    return false;

  // ImmR is the rotate right amount.
  unsigned ImmR = (VT.getSizeInBits() - DstLSB) % VT.getSizeInBits();
  // ImmS is the most significant bit of the source to be moved.
  unsigned ImmS = Width - 1;

  SDLoc DL(N);
  SDValue Ops[] = {Op0, CurDAG->getTargetConstant(ImmR, DL, VT),
                   CurDAG->getTargetConstant(ImmS, DL, VT)};
  unsigned Opc = (VT == MVT::i32) ? AArch64::UBFMWri : AArch64::UBFMXri;
  CurDAG->SelectNodeTo(N, Opc, VT, Ops);
  return true;
}

/// tryShiftAmountMod - Take advantage of built-in mod of shift amount in
/// variable shift/rotate instructions.
bool AArch64DAGToDAGISel::tryShiftAmountMod(SDNode *N) {
  EVT VT = N->getValueType(0);

  unsigned Opc;
  switch (N->getOpcode()) {
  case ISD::ROTR:
    Opc = (VT == MVT::i32) ? AArch64::RORVWr : AArch64::RORVXr;
    break;
  case ISD::SHL:
    Opc = (VT == MVT::i32) ? AArch64::LSLVWr : AArch64::LSLVXr;
    break;
  case ISD::SRL:
    Opc = (VT == MVT::i32) ? AArch64::LSRVWr : AArch64::LSRVXr;
    break;
  case ISD::SRA:
    Opc = (VT == MVT::i32) ? AArch64::ASRVWr : AArch64::ASRVXr;
    break;
  default:
    return false;
  }

  uint64_t Size;
  uint64_t Bits;
  if (VT == MVT::i32) {
    Bits = 5;
    Size = 32;
  } else if (VT == MVT::i64) {
    Bits = 6;
    Size = 64;
  } else
    return false;

  SDValue ShiftAmt = N->getOperand(1);
  SDLoc DL(N);
  SDValue NewShiftAmt;

  // Skip over an extend of the shift amount.
  if (ShiftAmt->getOpcode() == ISD::ZERO_EXTEND ||
      ShiftAmt->getOpcode() == ISD::ANY_EXTEND)
    ShiftAmt = ShiftAmt->getOperand(0);

  if (ShiftAmt->getOpcode() == ISD::ADD || ShiftAmt->getOpcode() == ISD::SUB) {
    SDValue Add0 = ShiftAmt->getOperand(0);
    SDValue Add1 = ShiftAmt->getOperand(1);
    uint64_t Add0Imm;
    uint64_t Add1Imm;
    if (isIntImmediate(Add1, Add1Imm) && (Add1Imm % Size == 0)) {
      // If we are shifting by X+/-N where N == 0 mod Size, then just shift by X
      // to avoid the ADD/SUB.
      NewShiftAmt = Add0;
    } else if (ShiftAmt->getOpcode() == ISD::SUB &&
               isIntImmediate(Add0, Add0Imm) && Add0Imm != 0 &&
               (Add0Imm % Size == 0)) {
      // If we are shifting by N-X where N == 0 mod Size, then just shift by -X
      // to generate a NEG instead of a SUB from a constant.
      unsigned NegOpc;
      unsigned ZeroReg;
      EVT SubVT = ShiftAmt->getValueType(0);
      if (SubVT == MVT::i32) {
        NegOpc = AArch64::SUBWrr;
        ZeroReg = AArch64::WZR;
      } else {
        assert(SubVT == MVT::i64);
        NegOpc = AArch64::SUBXrr;
        ZeroReg = AArch64::XZR;
      }
      SDValue Zero =
          CurDAG->getCopyFromReg(CurDAG->getEntryNode(), DL, ZeroReg, SubVT);
      MachineSDNode *Neg =
          CurDAG->getMachineNode(NegOpc, DL, SubVT, Zero, Add1);
      NewShiftAmt = SDValue(Neg, 0);
    } else if (ShiftAmt->getOpcode() == ISD::SUB &&
               isIntImmediate(Add0, Add0Imm) && (Add0Imm % Size == Size - 1)) {
      // If we are shifting by N-X where N == -1 mod Size, then just shift by ~X
      // to generate a NOT instead of a SUB from a constant.
      unsigned NotOpc;
      unsigned ZeroReg;
      EVT SubVT = ShiftAmt->getValueType(0);
      if (SubVT == MVT::i32) {
        NotOpc = AArch64::ORNWrr;
        ZeroReg = AArch64::WZR;
      } else {
        assert(SubVT == MVT::i64);
        NotOpc = AArch64::ORNXrr;
        ZeroReg = AArch64::XZR;
      }
      SDValue Zero =
          CurDAG->getCopyFromReg(CurDAG->getEntryNode(), DL, ZeroReg, SubVT);
      MachineSDNode *Not =
          CurDAG->getMachineNode(NotOpc, DL, SubVT, Zero, Add1);
      NewShiftAmt = SDValue(Not, 0);
    } else
      return false;
  } else {
    // If the shift amount is masked with an AND, check that the mask covers the
    // bits that are implicitly ANDed off by the above opcodes and if so, skip
    // the AND.
    uint64_t MaskImm;
    if (!isOpcWithIntImmediate(ShiftAmt.getNode(), ISD::AND, MaskImm) &&
        !isOpcWithIntImmediate(ShiftAmt.getNode(), AArch64ISD::ANDS, MaskImm))
      return false;

    if ((unsigned)llvm::countr_one(MaskImm) < Bits)
      return false;

    NewShiftAmt = ShiftAmt->getOperand(0);
  }

  // Narrow/widen the shift amount to match the size of the shift operation.
  if (VT == MVT::i32)
    NewShiftAmt = narrowIfNeeded(CurDAG, NewShiftAmt);
  else if (VT == MVT::i64 && NewShiftAmt->getValueType(0) == MVT::i32) {
    SDValue SubReg = CurDAG->getTargetConstant(AArch64::sub_32, DL, MVT::i32);
    MachineSDNode *Ext = CurDAG->getMachineNode(
        AArch64::SUBREG_TO_REG, DL, VT,
        CurDAG->getTargetConstant(0, DL, MVT::i64), NewShiftAmt, SubReg);
    NewShiftAmt = SDValue(Ext, 0);
  }

  SDValue Ops[] = {N->getOperand(0), NewShiftAmt};
  CurDAG->SelectNodeTo(N, Opc, VT, Ops);
  return true;
}

static bool checkCVTFixedPointOperandWithFBits(SelectionDAG *CurDAG, SDValue N,
                                               SDValue &FixedPos,
                                               unsigned RegWidth,
                                               bool isReciprocal) {
  APFloat FVal(0.0);
  if (ConstantFPSDNode *CN = dyn_cast<ConstantFPSDNode>(N))
    FVal = CN->getValueAPF();
  else if (LoadSDNode *LN = dyn_cast<LoadSDNode>(N)) {
    // Some otherwise illegal constants are allowed in this case.
    if (LN->getOperand(1).getOpcode() != AArch64ISD::ADDlow ||
        !isa<ConstantPoolSDNode>(LN->getOperand(1)->getOperand(1)))
      return false;

    ConstantPoolSDNode *CN =
        dyn_cast<ConstantPoolSDNode>(LN->getOperand(1)->getOperand(1));
    FVal = cast<ConstantFP>(CN->getConstVal())->getValueAPF();
  } else
    return false;

  // An FCVT[SU] instruction performs: convertToInt(Val * 2^fbits) where fbits
  // is between 1 and 32 for a destination w-register, or 1 and 64 for an
  // x-register.
  //
  // By this stage, we've detected (fp_to_[su]int (fmul Val, THIS_NODE)) so we
  // want THIS_NODE to be 2^fbits. This is much easier to deal with using
  // integers.
  bool IsExact;

  if (isReciprocal)
    if (!FVal.getExactInverse(&FVal))
      return false;

  // fbits is between 1 and 64 in the worst-case, which means the fmul
  // could have 2^64 as an actual operand. Need 65 bits of precision.
  APSInt IntVal(65, true);
  FVal.convertToInteger(IntVal, APFloat::rmTowardZero, &IsExact);

  // N.b. isPowerOf2 also checks for > 0.
  if (!IsExact || !IntVal.isPowerOf2())
    return false;
  unsigned FBits = IntVal.logBase2();

  // Checks above should have guaranteed that we haven't lost information in
  // finding FBits, but it must still be in range.
  if (FBits == 0 || FBits > RegWidth) return false;

  FixedPos = CurDAG->getTargetConstant(FBits, SDLoc(N), MVT::i32);
  return true;
}

bool AArch64DAGToDAGISel::SelectCVTFixedPosOperand(SDValue N, SDValue &FixedPos,
                                                   unsigned RegWidth) {
  return checkCVTFixedPointOperandWithFBits(CurDAG, N, FixedPos, RegWidth,
                                            false);
}

bool AArch64DAGToDAGISel::SelectCVTFixedPosRecipOperand(SDValue N,
                                                        SDValue &FixedPos,
                                                        unsigned RegWidth) {
  return checkCVTFixedPointOperandWithFBits(CurDAG, N, FixedPos, RegWidth,
                                            true);
}

// Inspects a register string of the form o0:op1:CRn:CRm:op2 gets the fields
// of the string and obtains the integer values from them and combines these
// into a single value to be used in the MRS/MSR instruction.
static int getIntOperandFromRegisterString(StringRef RegString) {
  SmallVector<StringRef, 5> Fields;
  RegString.split(Fields, ':');

  if (Fields.size() == 1)
    return -1;

  assert(Fields.size() == 5
            && "Invalid number of fields in read register string");

  SmallVector<int, 5> Ops;
  bool AllIntFields = true;

  for (StringRef Field : Fields) {
    unsigned IntField;
    AllIntFields &= !Field.getAsInteger(10, IntField);
    Ops.push_back(IntField);
  }

  assert(AllIntFields &&
          "Unexpected non-integer value in special register string.");
  (void)AllIntFields;

  // Need to combine the integer fields of the string into a single value
  // based on the bit encoding of MRS/MSR instruction.
  return (Ops[0] << 14) | (Ops[1] << 11) | (Ops[2] << 7) |
         (Ops[3] << 3) | (Ops[4]);
}

// Lower the read_register intrinsic to an MRS instruction node if the special
// register string argument is either of the form detailed in the ALCE (the
// form described in getIntOperandsFromRegsterString) or is a named register
// known by the MRS SysReg mapper.
bool AArch64DAGToDAGISel::tryReadRegister(SDNode *N) {
  const auto *MD = cast<MDNodeSDNode>(N->getOperand(1));
  const auto *RegString = cast<MDString>(MD->getMD()->getOperand(0));
  SDLoc DL(N);

  bool ReadIs128Bit = N->getOpcode() == AArch64ISD::MRRS;

  unsigned Opcode64Bit = AArch64::MRS;
  int Imm = getIntOperandFromRegisterString(RegString->getString());
  if (Imm == -1) {
    // No match, Use the sysreg mapper to map the remaining possible strings to
    // the value for the register to be used for the instruction operand.
    const auto *TheReg =
        AArch64SysReg::lookupSysRegByName(RegString->getString());
    if (TheReg && TheReg->Readable &&
        TheReg->haveFeatures(Subtarget->getFeatureBits()))
      Imm = TheReg->Encoding;
    else
      Imm = AArch64SysReg::parseGenericRegister(RegString->getString());

    if (Imm == -1) {
      // Still no match, see if this is "pc" or give up.
      if (!ReadIs128Bit && RegString->getString() == "pc") {
        Opcode64Bit = AArch64::ADR;
        Imm = 0;
      } else {
        return false;
      }
    }
  }

  SDValue InChain = N->getOperand(0);
  SDValue SysRegImm = CurDAG->getTargetConstant(Imm, DL, MVT::i32);
  if (!ReadIs128Bit) {
    CurDAG->SelectNodeTo(N, Opcode64Bit, MVT::i64, MVT::Other /* Chain */,
                         {SysRegImm, InChain});
  } else {
    SDNode *MRRS = CurDAG->getMachineNode(
        AArch64::MRRS, DL,
        {MVT::Untyped /* XSeqPair */, MVT::Other /* Chain */},
        {SysRegImm, InChain});

    // Sysregs are not endian. The even register always contains the low half
    // of the register.
    SDValue Lo = CurDAG->getTargetExtractSubreg(AArch64::sube64, DL, MVT::i64,
                                                SDValue(MRRS, 0));
    SDValue Hi = CurDAG->getTargetExtractSubreg(AArch64::subo64, DL, MVT::i64,
                                                SDValue(MRRS, 0));
    SDValue OutChain = SDValue(MRRS, 1);

    ReplaceUses(SDValue(N, 0), Lo);
    ReplaceUses(SDValue(N, 1), Hi);
    ReplaceUses(SDValue(N, 2), OutChain);
  };
  return true;
}

// Lower the write_register intrinsic to an MSR instruction node if the special
// register string argument is either of the form detailed in the ALCE (the
// form described in getIntOperandsFromRegsterString) or is a named register
// known by the MSR SysReg mapper.
bool AArch64DAGToDAGISel::tryWriteRegister(SDNode *N) {
  const auto *MD = cast<MDNodeSDNode>(N->getOperand(1));
  const auto *RegString = cast<MDString>(MD->getMD()->getOperand(0));
  SDLoc DL(N);

  bool WriteIs128Bit = N->getOpcode() == AArch64ISD::MSRR;

  if (!WriteIs128Bit) {
    // Check if the register was one of those allowed as the pstatefield value
    // in the MSR (immediate) instruction. To accept the values allowed in the
    // pstatefield for the MSR (immediate) instruction, we also require that an
    // immediate value has been provided as an argument, we know that this is
    // the case as it has been ensured by semantic checking.
    auto trySelectPState = [&](auto PMapper, unsigned State) {
      if (PMapper) {
        assert(isa<ConstantSDNode>(N->getOperand(2)) &&
               "Expected a constant integer expression.");
        unsigned Reg = PMapper->Encoding;
        uint64_t Immed = N->getConstantOperandVal(2);
        CurDAG->SelectNodeTo(
            N, State, MVT::Other, CurDAG->getTargetConstant(Reg, DL, MVT::i32),
            CurDAG->getTargetConstant(Immed, DL, MVT::i16), N->getOperand(0));
        return true;
      }
      return false;
    };

    if (trySelectPState(
            AArch64PState::lookupPStateImm0_15ByName(RegString->getString()),
            AArch64::MSRpstateImm4))
      return true;
    if (trySelectPState(
            AArch64PState::lookupPStateImm0_1ByName(RegString->getString()),
            AArch64::MSRpstateImm1))
      return true;
  }

  int Imm = getIntOperandFromRegisterString(RegString->getString());
  if (Imm == -1) {
    // Use the sysreg mapper to attempt to map the remaining possible strings
    // to the value for the register to be used for the MSR (register)
    // instruction operand.
    auto TheReg = AArch64SysReg::lookupSysRegByName(RegString->getString());
    if (TheReg && TheReg->Writeable &&
        TheReg->haveFeatures(Subtarget->getFeatureBits()))
      Imm = TheReg->Encoding;
    else
      Imm = AArch64SysReg::parseGenericRegister(RegString->getString());

    if (Imm == -1)
      return false;
  }

  SDValue InChain = N->getOperand(0);
  if (!WriteIs128Bit) {
    CurDAG->SelectNodeTo(N, AArch64::MSR, MVT::Other,
                         CurDAG->getTargetConstant(Imm, DL, MVT::i32),
                         N->getOperand(2), InChain);
  } else {
    // No endian swap. The lower half always goes into the even subreg, and the
    // higher half always into the odd supreg.
    SDNode *Pair = CurDAG->getMachineNode(
        TargetOpcode::REG_SEQUENCE, DL, MVT::Untyped /* XSeqPair */,
        {CurDAG->getTargetConstant(AArch64::XSeqPairsClassRegClass.getID(), DL,
                                   MVT::i32),
         N->getOperand(2),
         CurDAG->getTargetConstant(AArch64::sube64, DL, MVT::i32),
         N->getOperand(3),
         CurDAG->getTargetConstant(AArch64::subo64, DL, MVT::i32)});

    CurDAG->SelectNodeTo(N, AArch64::MSRR, MVT::Other,
                         CurDAG->getTargetConstant(Imm, DL, MVT::i32),
                         SDValue(Pair, 0), InChain);
  }

  return true;
}

/// We've got special pseudo-instructions for these
bool AArch64DAGToDAGISel::SelectCMP_SWAP(SDNode *N) {
  unsigned Opcode;
  EVT MemTy = cast<MemSDNode>(N)->getMemoryVT();

  // Leave IR for LSE if subtarget supports it.
  if (Subtarget->hasLSE()) return false;

  if (MemTy == MVT::i8)
    Opcode = AArch64::CMP_SWAP_8;
  else if (MemTy == MVT::i16)
    Opcode = AArch64::CMP_SWAP_16;
  else if (MemTy == MVT::i32)
    Opcode = AArch64::CMP_SWAP_32;
  else if (MemTy == MVT::i64)
    Opcode = AArch64::CMP_SWAP_64;
  else
    llvm_unreachable("Unknown AtomicCmpSwap type");

  MVT RegTy = MemTy == MVT::i64 ? MVT::i64 : MVT::i32;
  SDValue Ops[] = {N->getOperand(1), N->getOperand(2), N->getOperand(3),
                   N->getOperand(0)};
  SDNode *CmpSwap = CurDAG->getMachineNode(
      Opcode, SDLoc(N),
      CurDAG->getVTList(RegTy, MVT::i32, MVT::Other), Ops);

  MachineMemOperand *MemOp = cast<MemSDNode>(N)->getMemOperand();
  CurDAG->setNodeMemRefs(cast<MachineSDNode>(CmpSwap), {MemOp});

  ReplaceUses(SDValue(N, 0), SDValue(CmpSwap, 0));
  ReplaceUses(SDValue(N, 1), SDValue(CmpSwap, 2));
  CurDAG->RemoveDeadNode(N);

  return true;
}

bool AArch64DAGToDAGISel::SelectSVEAddSubImm(SDValue N, MVT VT, SDValue &Imm,
                                             SDValue &Shift) {
  if (!isa<ConstantSDNode>(N))
    return false;

  SDLoc DL(N);
  uint64_t Val = cast<ConstantSDNode>(N)
                     ->getAPIntValue()
                     .trunc(VT.getFixedSizeInBits())
                     .getZExtValue();

  switch (VT.SimpleTy) {
  case MVT::i8:
    // All immediates are supported.
    Shift = CurDAG->getTargetConstant(0, DL, MVT::i32);
    Imm = CurDAG->getTargetConstant(Val, DL, MVT::i32);
    return true;
  case MVT::i16:
  case MVT::i32:
  case MVT::i64:
    // Support 8bit unsigned immediates.
    if (Val <= 255) {
      Shift = CurDAG->getTargetConstant(0, DL, MVT::i32);
      Imm = CurDAG->getTargetConstant(Val, DL, MVT::i32);
      return true;
    }
    // Support 16bit unsigned immediates that are a multiple of 256.
    if (Val <= 65280 && Val % 256 == 0) {
      Shift = CurDAG->getTargetConstant(8, DL, MVT::i32);
      Imm = CurDAG->getTargetConstant(Val >> 8, DL, MVT::i32);
      return true;
    }
    break;
  default:
    break;
  }

  return false;
}

bool AArch64DAGToDAGISel::SelectSVEAddSubSSatImm(SDValue N, MVT VT,
                                                 SDValue &Imm, SDValue &Shift,
                                                 bool Negate) {
  if (!isa<ConstantSDNode>(N))
    return false;

  SDLoc DL(N);
  int64_t Val = cast<ConstantSDNode>(N)
                    ->getAPIntValue()
                    .trunc(VT.getFixedSizeInBits())
                    .getSExtValue();

  if (Negate)
    Val = -Val;

  // Signed saturating instructions treat their immediate operand as unsigned,
  // whereas the related intrinsics define their operands to be signed. This
  // means we can only use the immediate form when the operand is non-negative.
  if (Val < 0)
    return false;

  switch (VT.SimpleTy) {
  case MVT::i8:
    // All positive immediates are supported.
    Shift = CurDAG->getTargetConstant(0, DL, MVT::i32);
    Imm = CurDAG->getTargetConstant(Val, DL, MVT::i32);
    return true;
  case MVT::i16:
  case MVT::i32:
  case MVT::i64:
    // Support 8bit positive immediates.
    if (Val <= 255) {
      Shift = CurDAG->getTargetConstant(0, DL, MVT::i32);
      Imm = CurDAG->getTargetConstant(Val, DL, MVT::i32);
      return true;
    }
    // Support 16bit positive immediates that are a multiple of 256.
    if (Val <= 65280 && Val % 256 == 0) {
      Shift = CurDAG->getTargetConstant(8, DL, MVT::i32);
      Imm = CurDAG->getTargetConstant(Val >> 8, DL, MVT::i32);
      return true;
    }
    break;
  default:
    break;
  }

  return false;
}

bool AArch64DAGToDAGISel::SelectSVECpyDupImm(SDValue N, MVT VT, SDValue &Imm,
                                             SDValue &Shift) {
  if (!isa<ConstantSDNode>(N))
    return false;

  SDLoc DL(N);
  int64_t Val = cast<ConstantSDNode>(N)
                    ->getAPIntValue()
                    .trunc(VT.getFixedSizeInBits())
                    .getSExtValue();

  switch (VT.SimpleTy) {
  case MVT::i8:
    // All immediates are supported.
    Shift = CurDAG->getTargetConstant(0, DL, MVT::i32);
    Imm = CurDAG->getTargetConstant(Val & 0xFF, DL, MVT::i32);
    return true;
  case MVT::i16:
  case MVT::i32:
  case MVT::i64:
    // Support 8bit signed immediates.
    if (Val >= -128 && Val <= 127) {
      Shift = CurDAG->getTargetConstant(0, DL, MVT::i32);
      Imm = CurDAG->getTargetConstant(Val & 0xFF, DL, MVT::i32);
      return true;
    }
    // Support 16bit signed immediates that are a multiple of 256.
    if (Val >= -32768 && Val <= 32512 && Val % 256 == 0) {
      Shift = CurDAG->getTargetConstant(8, DL, MVT::i32);
      Imm = CurDAG->getTargetConstant((Val >> 8) & 0xFF, DL, MVT::i32);
      return true;
    }
    break;
  default:
    break;
  }

  return false;
}

bool AArch64DAGToDAGISel::SelectSVESignedArithImm(SDValue N, SDValue &Imm) {
  if (auto CNode = dyn_cast<ConstantSDNode>(N)) {
    int64_t ImmVal = CNode->getSExtValue();
    SDLoc DL(N);
    if (ImmVal >= -128 && ImmVal < 128) {
      Imm = CurDAG->getTargetConstant(ImmVal, DL, MVT::i32);
      return true;
    }
  }
  return false;
}

bool AArch64DAGToDAGISel::SelectSVEArithImm(SDValue N, MVT VT, SDValue &Imm) {
  if (auto CNode = dyn_cast<ConstantSDNode>(N)) {
    uint64_t ImmVal = CNode->getZExtValue();

    switch (VT.SimpleTy) {
    case MVT::i8:
      ImmVal &= 0xFF;
      break;
    case MVT::i16:
      ImmVal &= 0xFFFF;
      break;
    case MVT::i32:
      ImmVal &= 0xFFFFFFFF;
      break;
    case MVT::i64:
      break;
    default:
      llvm_unreachable("Unexpected type");
    }

    if (ImmVal < 256) {
      Imm = CurDAG->getTargetConstant(ImmVal, SDLoc(N), MVT::i32);
      return true;
    }
  }
  return false;
}

bool AArch64DAGToDAGISel::SelectSVELogicalImm(SDValue N, MVT VT, SDValue &Imm,
                                              bool Invert) {
  if (auto CNode = dyn_cast<ConstantSDNode>(N)) {
    uint64_t ImmVal = CNode->getZExtValue();
    SDLoc DL(N);

    if (Invert)
      ImmVal = ~ImmVal;

    // Shift mask depending on type size.
    switch (VT.SimpleTy) {
    case MVT::i8:
      ImmVal &= 0xFF;
      ImmVal |= ImmVal << 8;
      ImmVal |= ImmVal << 16;
      ImmVal |= ImmVal << 32;
      break;
    case MVT::i16:
      ImmVal &= 0xFFFF;
      ImmVal |= ImmVal << 16;
      ImmVal |= ImmVal << 32;
      break;
    case MVT::i32:
      ImmVal &= 0xFFFFFFFF;
      ImmVal |= ImmVal << 32;
      break;
    case MVT::i64:
      break;
    default:
      llvm_unreachable("Unexpected type");
    }

    uint64_t encoding;
    if (AArch64_AM::processLogicalImmediate(ImmVal, 64, encoding)) {
      Imm = CurDAG->getTargetConstant(encoding, DL, MVT::i64);
      return true;
    }
  }
  return false;
}

// SVE shift intrinsics allow shift amounts larger than the element's bitwidth.
// Rather than attempt to normalise everything we can sometimes saturate the
// shift amount during selection. This function also allows for consistent
// isel patterns by ensuring the resulting "Imm" node is of the i32 type
// required by the instructions.
bool AArch64DAGToDAGISel::SelectSVEShiftImm(SDValue N, uint64_t Low,
                                            uint64_t High, bool AllowSaturation,
                                            SDValue &Imm) {
  if (auto *CN = dyn_cast<ConstantSDNode>(N)) {
    uint64_t ImmVal = CN->getZExtValue();

    // Reject shift amounts that are too small.
    if (ImmVal < Low)
      return false;

    // Reject or saturate shift amounts that are too big.
    if (ImmVal > High) {
      if (!AllowSaturation)
        return false;
      ImmVal = High;
    }

    Imm = CurDAG->getTargetConstant(ImmVal, SDLoc(N), MVT::i32);
    return true;
  }

  return false;
}

bool AArch64DAGToDAGISel::trySelectStackSlotTagP(SDNode *N) {
  // tagp(FrameIndex, IRGstack, tag_offset):
  // since the offset between FrameIndex and IRGstack is a compile-time
  // constant, this can be lowered to a single ADDG instruction.
  if (!(isa<FrameIndexSDNode>(N->getOperand(1)))) {
    return false;
  }

  SDValue IRG_SP = N->getOperand(2);
  if (IRG_SP->getOpcode() != ISD::INTRINSIC_W_CHAIN ||
      IRG_SP->getConstantOperandVal(1) != Intrinsic::aarch64_irg_sp) {
    return false;
  }

  const TargetLowering *TLI = getTargetLowering();
  SDLoc DL(N);
  int FI = cast<FrameIndexSDNode>(N->getOperand(1))->getIndex();
  SDValue FiOp = CurDAG->getTargetFrameIndex(
      FI, TLI->getPointerTy(CurDAG->getDataLayout()));
  int TagOffset = N->getConstantOperandVal(3);

  SDNode *Out = CurDAG->getMachineNode(
      AArch64::TAGPstack, DL, MVT::i64,
      {FiOp, CurDAG->getTargetConstant(0, DL, MVT::i64), N->getOperand(2),
       CurDAG->getTargetConstant(TagOffset, DL, MVT::i64)});
  ReplaceNode(N, Out);
  return true;
}

void AArch64DAGToDAGISel::SelectTagP(SDNode *N) {
  assert(isa<ConstantSDNode>(N->getOperand(3)) &&
         "llvm.aarch64.tagp third argument must be an immediate");
  if (trySelectStackSlotTagP(N))
    return;
  // FIXME: above applies in any case when offset between Op1 and Op2 is a
  // compile-time constant, not just for stack allocations.

  // General case for unrelated pointers in Op1 and Op2.
  SDLoc DL(N);
  int TagOffset = N->getConstantOperandVal(3);
  SDNode *N1 = CurDAG->getMachineNode(AArch64::SUBP, DL, MVT::i64,
                                      {N->getOperand(1), N->getOperand(2)});
  SDNode *N2 = CurDAG->getMachineNode(AArch64::ADDXrr, DL, MVT::i64,
                                      {SDValue(N1, 0), N->getOperand(2)});
  SDNode *N3 = CurDAG->getMachineNode(
      AArch64::ADDG, DL, MVT::i64,
      {SDValue(N2, 0), CurDAG->getTargetConstant(0, DL, MVT::i64),
       CurDAG->getTargetConstant(TagOffset, DL, MVT::i64)});
  ReplaceNode(N, N3);
}

bool AArch64DAGToDAGISel::trySelectCastFixedLengthToScalableVector(SDNode *N) {
  assert(N->getOpcode() == ISD::INSERT_SUBVECTOR && "Invalid Node!");

  // Bail when not a "cast" like insert_subvector.
  if (N->getConstantOperandVal(2) != 0)
    return false;
  if (!N->getOperand(0).isUndef())
    return false;

  // Bail when normal isel should do the job.
  EVT VT = N->getValueType(0);
  EVT InVT = N->getOperand(1).getValueType();
  if (VT.isFixedLengthVector() || InVT.isScalableVector())
    return false;
  if (InVT.getSizeInBits() <= 128)
    return false;

  // NOTE: We can only get here when doing fixed length SVE code generation.
  // We do manual selection because the types involved are not linked to real
  // registers (despite being legal) and must be coerced into SVE registers.

  assert(VT.getSizeInBits().getKnownMinValue() == AArch64::SVEBitsPerBlock &&
         "Expected to insert into a packed scalable vector!");

  SDLoc DL(N);
  auto RC = CurDAG->getTargetConstant(AArch64::ZPRRegClassID, DL, MVT::i64);
  ReplaceNode(N, CurDAG->getMachineNode(TargetOpcode::COPY_TO_REGCLASS, DL, VT,
                                        N->getOperand(1), RC));
  return true;
}

bool AArch64DAGToDAGISel::trySelectCastScalableToFixedLengthVector(SDNode *N) {
  assert(N->getOpcode() == ISD::EXTRACT_SUBVECTOR && "Invalid Node!");

  // Bail when not a "cast" like extract_subvector.
  if (N->getConstantOperandVal(1) != 0)
    return false;

  // Bail when normal isel can do the job.
  EVT VT = N->getValueType(0);
  EVT InVT = N->getOperand(0).getValueType();
  if (VT.isScalableVector() || InVT.isFixedLengthVector())
    return false;
  if (VT.getSizeInBits() <= 128)
    return false;

  // NOTE: We can only get here when doing fixed length SVE code generation.
  // We do manual selection because the types involved are not linked to real
  // registers (despite being legal) and must be coerced into SVE registers.

  assert(InVT.getSizeInBits().getKnownMinValue() == AArch64::SVEBitsPerBlock &&
         "Expected to extract from a packed scalable vector!");

  SDLoc DL(N);
  auto RC = CurDAG->getTargetConstant(AArch64::ZPRRegClassID, DL, MVT::i64);
  ReplaceNode(N, CurDAG->getMachineNode(TargetOpcode::COPY_TO_REGCLASS, DL, VT,
                                        N->getOperand(0), RC));
  return true;
}

bool AArch64DAGToDAGISel::trySelectXAR(SDNode *N) {
  assert(N->getOpcode() == ISD::OR && "Expected OR instruction");

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  EVT VT = N->getValueType(0);

  // Essentially: rotr (xor(x, y), imm) -> xar (x, y, imm)
  // Rotate by a constant is a funnel shift in IR which is exanded to
  // an OR with shifted operands.
  // We do the following transform:
  //   OR N0, N1 -> xar (x, y, imm)
  // Where:
  //   N1 = SRL_PRED true, V, splat(imm)  --> rotr amount
  //   N0 = SHL_PRED true, V, splat(bits-imm)
  //   V = (xor x, y)
  if (VT.isScalableVector() &&
      (Subtarget->hasSVE2() ||
       (Subtarget->hasSME() && Subtarget->isStreaming()))) {
    if (N0.getOpcode() != AArch64ISD::SHL_PRED ||
        N1.getOpcode() != AArch64ISD::SRL_PRED)
      std::swap(N0, N1);
    if (N0.getOpcode() != AArch64ISD::SHL_PRED ||
        N1.getOpcode() != AArch64ISD::SRL_PRED)
      return false;

    auto *TLI = static_cast<const AArch64TargetLowering *>(getTargetLowering());
    if (!TLI->isAllActivePredicate(*CurDAG, N0.getOperand(0)) ||
        !TLI->isAllActivePredicate(*CurDAG, N1.getOperand(0)))
      return false;

    SDValue XOR = N0.getOperand(1);
    if (XOR.getOpcode() != ISD::XOR || XOR != N1.getOperand(1))
      return false;

    APInt ShlAmt, ShrAmt;
    if (!ISD::isConstantSplatVector(N0.getOperand(2).getNode(), ShlAmt) ||
        !ISD::isConstantSplatVector(N1.getOperand(2).getNode(), ShrAmt))
      return false;

    if (ShlAmt + ShrAmt != VT.getScalarSizeInBits())
      return false;

    SDLoc DL(N);
    SDValue Imm =
        CurDAG->getTargetConstant(ShrAmt.getZExtValue(), DL, MVT::i32);

    SDValue Ops[] = {XOR.getOperand(0), XOR.getOperand(1), Imm};
    if (auto Opc = SelectOpcodeFromVT<SelectTypeKind::Int>(
            VT, {AArch64::XAR_ZZZI_B, AArch64::XAR_ZZZI_H, AArch64::XAR_ZZZI_S,
                 AArch64::XAR_ZZZI_D})) {
      CurDAG->SelectNodeTo(N, Opc, VT, Ops);
      return true;
    }
    return false;
  }

  if (!Subtarget->hasSHA3())
    return false;

  if (N0->getOpcode() != AArch64ISD::VSHL ||
      N1->getOpcode() != AArch64ISD::VLSHR)
    return false;

  if (N0->getOperand(0) != N1->getOperand(0) ||
      N1->getOperand(0)->getOpcode() != ISD::XOR)
    return false;

  SDValue XOR = N0.getOperand(0);
  SDValue R1 = XOR.getOperand(0);
  SDValue R2 = XOR.getOperand(1);

  unsigned HsAmt = N0.getConstantOperandVal(1);
  unsigned ShAmt = N1.getConstantOperandVal(1);

  SDLoc DL = SDLoc(N0.getOperand(1));
  SDValue Imm = CurDAG->getTargetConstant(
      ShAmt, DL, N0.getOperand(1).getValueType(), false);

  if (ShAmt + HsAmt != 64)
    return false;

  SDValue Ops[] = {R1, R2, Imm};
  CurDAG->SelectNodeTo(N, AArch64::XAR, N0.getValueType(), Ops);

  return true;
}

void AArch64DAGToDAGISel::Select(SDNode *Node) {
  // If we have a custom node, we already have selected!
  if (Node->isMachineOpcode()) {
    LLVM_DEBUG(errs() << "== "; Node->dump(CurDAG); errs() << "\n");
    Node->setNodeId(-1);
    return;
  }

  // Few custom selection stuff.
  EVT VT = Node->getValueType(0);

  switch (Node->getOpcode()) {
  default:
    break;

  case ISD::ATOMIC_CMP_SWAP:
    if (SelectCMP_SWAP(Node))
      return;
    break;

  case ISD::READ_REGISTER:
  case AArch64ISD::MRRS:
    if (tryReadRegister(Node))
      return;
    break;

  case ISD::WRITE_REGISTER:
  case AArch64ISD::MSRR:
    if (tryWriteRegister(Node))
      return;
    break;

  case ISD::LOAD: {
    // Try to select as an indexed load. Fall through to normal processing
    // if we can't.
    if (tryIndexedLoad(Node))
      return;
    break;
  }

  case ISD::SRL:
  case ISD::AND:
  case ISD::SRA:
  case ISD::SIGN_EXTEND_INREG:
    if (tryBitfieldExtractOp(Node))
      return;
    if (tryBitfieldInsertInZeroOp(Node))
      return;
    [[fallthrough]];
  case ISD::ROTR:
  case ISD::SHL:
    if (tryShiftAmountMod(Node))
      return;
    break;

  case ISD::SIGN_EXTEND:
    if (tryBitfieldExtractOpFromSExt(Node))
      return;
    break;

  case ISD::OR:
    if (tryBitfieldInsertOp(Node))
      return;
    if (trySelectXAR(Node))
      return;
    break;

  case ISD::EXTRACT_SUBVECTOR: {
    if (trySelectCastScalableToFixedLengthVector(Node))
      return;
    break;
  }

  case ISD::INSERT_SUBVECTOR: {
    if (trySelectCastFixedLengthToScalableVector(Node))
      return;
    break;
  }

  case ISD::Constant: {
    // Materialize zero constants as copies from WZR/XZR.  This allows
    // the coalescer to propagate these into other instructions.
    ConstantSDNode *ConstNode = cast<ConstantSDNode>(Node);
    if (ConstNode->isZero()) {
      if (VT == MVT::i32) {
        SDValue New = CurDAG->getCopyFromReg(
            CurDAG->getEntryNode(), SDLoc(Node), AArch64::WZR, MVT::i32);
        ReplaceNode(Node, New.getNode());
        return;
      } else if (VT == MVT::i64) {
        SDValue New = CurDAG->getCopyFromReg(
            CurDAG->getEntryNode(), SDLoc(Node), AArch64::XZR, MVT::i64);
        ReplaceNode(Node, New.getNode());
        return;
      }
    }
    break;
  }

  case ISD::FrameIndex: {
    // Selects to ADDXri FI, 0 which in turn will become ADDXri SP, imm.
    int FI = cast<FrameIndexSDNode>(Node)->getIndex();
    unsigned Shifter = AArch64_AM::getShifterImm(AArch64_AM::LSL, 0);
    const TargetLowering *TLI = getTargetLowering();
    SDValue TFI = CurDAG->getTargetFrameIndex(
        FI, TLI->getPointerTy(CurDAG->getDataLayout()));
    SDLoc DL(Node);
    SDValue Ops[] = { TFI, CurDAG->getTargetConstant(0, DL, MVT::i32),
                      CurDAG->getTargetConstant(Shifter, DL, MVT::i32) };
    CurDAG->SelectNodeTo(Node, AArch64::ADDXri, MVT::i64, Ops);
    return;
  }
  case ISD::INTRINSIC_W_CHAIN: {
    unsigned IntNo = Node->getConstantOperandVal(1);
    switch (IntNo) {
    default:
      break;
    case Intrinsic::aarch64_gcsss: {
      SDLoc DL(Node);
      SDValue Chain = Node->getOperand(0);
      SDValue Val = Node->getOperand(2);
      SDValue Zero = CurDAG->getCopyFromReg(Chain, DL, AArch64::XZR, MVT::i64);
      SDNode *SS1 =
          CurDAG->getMachineNode(AArch64::GCSSS1, DL, MVT::Other, Val, Chain);
      SDNode *SS2 = CurDAG->getMachineNode(AArch64::GCSSS2, DL, MVT::i64,
                                           MVT::Other, Zero, SDValue(SS1, 0));
      ReplaceNode(Node, SS2);
      return;
    }
    case Intrinsic::aarch64_ldaxp:
    case Intrinsic::aarch64_ldxp: {
      unsigned Op =
          IntNo == Intrinsic::aarch64_ldaxp ? AArch64::LDAXPX : AArch64::LDXPX;
      SDValue MemAddr = Node->getOperand(2);
      SDLoc DL(Node);
      SDValue Chain = Node->getOperand(0);

      SDNode *Ld = CurDAG->getMachineNode(Op, DL, MVT::i64, MVT::i64,
                                          MVT::Other, MemAddr, Chain);

      // Transfer memoperands.
      MachineMemOperand *MemOp =
          cast<MemIntrinsicSDNode>(Node)->getMemOperand();
      CurDAG->setNodeMemRefs(cast<MachineSDNode>(Ld), {MemOp});
      ReplaceNode(Node, Ld);
      return;
    }
    case Intrinsic::aarch64_stlxp:
    case Intrinsic::aarch64_stxp: {
      unsigned Op =
          IntNo == Intrinsic::aarch64_stlxp ? AArch64::STLXPX : AArch64::STXPX;
      SDLoc DL(Node);
      SDValue Chain = Node->getOperand(0);
      SDValue ValLo = Node->getOperand(2);
      SDValue ValHi = Node->getOperand(3);
      SDValue MemAddr = Node->getOperand(4);

      // Place arguments in the right order.
      SDValue Ops[] = {ValLo, ValHi, MemAddr, Chain};

      SDNode *St = CurDAG->getMachineNode(Op, DL, MVT::i32, MVT::Other, Ops);
      // Transfer memoperands.
      MachineMemOperand *MemOp =
          cast<MemIntrinsicSDNode>(Node)->getMemOperand();
      CurDAG->setNodeMemRefs(cast<MachineSDNode>(St), {MemOp});

      ReplaceNode(Node, St);
      return;
    }
    case Intrinsic::aarch64_neon_ld1x2:
      if (VT == MVT::v8i8) {
        SelectLoad(Node, 2, AArch64::LD1Twov8b, AArch64::dsub0);
        return;
      } else if (VT == MVT::v16i8) {
        SelectLoad(Node, 2, AArch64::LD1Twov16b, AArch64::qsub0);
        return;
      } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
        SelectLoad(Node, 2, AArch64::LD1Twov4h, AArch64::dsub0);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v8bf16) {
        SelectLoad(Node, 2, AArch64::LD1Twov8h, AArch64::qsub0);
        return;
      } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
        SelectLoad(Node, 2, AArch64::LD1Twov2s, AArch64::dsub0);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
        SelectLoad(Node, 2, AArch64::LD1Twov4s, AArch64::qsub0);
        return;
      } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
        SelectLoad(Node, 2, AArch64::LD1Twov1d, AArch64::dsub0);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
        SelectLoad(Node, 2, AArch64::LD1Twov2d, AArch64::qsub0);
        return;
      }
      break;
    case Intrinsic::aarch64_neon_ld1x3:
      if (VT == MVT::v8i8) {
        SelectLoad(Node, 3, AArch64::LD1Threev8b, AArch64::dsub0);
        return;
      } else if (VT == MVT::v16i8) {
        SelectLoad(Node, 3, AArch64::LD1Threev16b, AArch64::qsub0);
        return;
      } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
        SelectLoad(Node, 3, AArch64::LD1Threev4h, AArch64::dsub0);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v8bf16) {
        SelectLoad(Node, 3, AArch64::LD1Threev8h, AArch64::qsub0);
        return;
      } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
        SelectLoad(Node, 3, AArch64::LD1Threev2s, AArch64::dsub0);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
        SelectLoad(Node, 3, AArch64::LD1Threev4s, AArch64::qsub0);
        return;
      } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
        SelectLoad(Node, 3, AArch64::LD1Threev1d, AArch64::dsub0);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
        SelectLoad(Node, 3, AArch64::LD1Threev2d, AArch64::qsub0);
        return;
      }
      break;
    case Intrinsic::aarch64_neon_ld1x4:
      if (VT == MVT::v8i8) {
        SelectLoad(Node, 4, AArch64::LD1Fourv8b, AArch64::dsub0);
        return;
      } else if (VT == MVT::v16i8) {
        SelectLoad(Node, 4, AArch64::LD1Fourv16b, AArch64::qsub0);
        return;
      } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
        SelectLoad(Node, 4, AArch64::LD1Fourv4h, AArch64::dsub0);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v8bf16) {
        SelectLoad(Node, 4, AArch64::LD1Fourv8h, AArch64::qsub0);
        return;
      } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
        SelectLoad(Node, 4, AArch64::LD1Fourv2s, AArch64::dsub0);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
        SelectLoad(Node, 4, AArch64::LD1Fourv4s, AArch64::qsub0);
        return;
      } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
        SelectLoad(Node, 4, AArch64::LD1Fourv1d, AArch64::dsub0);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
        SelectLoad(Node, 4, AArch64::LD1Fourv2d, AArch64::qsub0);
        return;
      }
      break;
    case Intrinsic::aarch64_neon_ld2:
      if (VT == MVT::v8i8) {
        SelectLoad(Node, 2, AArch64::LD2Twov8b, AArch64::dsub0);
        return;
      } else if (VT == MVT::v16i8) {
        SelectLoad(Node, 2, AArch64::LD2Twov16b, AArch64::qsub0);
        return;
      } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
        SelectLoad(Node, 2, AArch64::LD2Twov4h, AArch64::dsub0);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v8bf16) {
        SelectLoad(Node, 2, AArch64::LD2Twov8h, AArch64::qsub0);
        return;
      } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
        SelectLoad(Node, 2, AArch64::LD2Twov2s, AArch64::dsub0);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
        SelectLoad(Node, 2, AArch64::LD2Twov4s, AArch64::qsub0);
        return;
      } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
        SelectLoad(Node, 2, AArch64::LD1Twov1d, AArch64::dsub0);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
        SelectLoad(Node, 2, AArch64::LD2Twov2d, AArch64::qsub0);
        return;
      }
      break;
    case Intrinsic::aarch64_neon_ld3:
      if (VT == MVT::v8i8) {
        SelectLoad(Node, 3, AArch64::LD3Threev8b, AArch64::dsub0);
        return;
      } else if (VT == MVT::v16i8) {
        SelectLoad(Node, 3, AArch64::LD3Threev16b, AArch64::qsub0);
        return;
      } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
        SelectLoad(Node, 3, AArch64::LD3Threev4h, AArch64::dsub0);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v8bf16) {
        SelectLoad(Node, 3, AArch64::LD3Threev8h, AArch64::qsub0);
        return;
      } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
        SelectLoad(Node, 3, AArch64::LD3Threev2s, AArch64::dsub0);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
        SelectLoad(Node, 3, AArch64::LD3Threev4s, AArch64::qsub0);
        return;
      } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
        SelectLoad(Node, 3, AArch64::LD1Threev1d, AArch64::dsub0);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
        SelectLoad(Node, 3, AArch64::LD3Threev2d, AArch64::qsub0);
        return;
      }
      break;
    case Intrinsic::aarch64_neon_ld4:
      if (VT == MVT::v8i8) {
        SelectLoad(Node, 4, AArch64::LD4Fourv8b, AArch64::dsub0);
        return;
      } else if (VT == MVT::v16i8) {
        SelectLoad(Node, 4, AArch64::LD4Fourv16b, AArch64::qsub0);
        return;
      } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
        SelectLoad(Node, 4, AArch64::LD4Fourv4h, AArch64::dsub0);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v8bf16) {
        SelectLoad(Node, 4, AArch64::LD4Fourv8h, AArch64::qsub0);
        return;
      } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
        SelectLoad(Node, 4, AArch64::LD4Fourv2s, AArch64::dsub0);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
        SelectLoad(Node, 4, AArch64::LD4Fourv4s, AArch64::qsub0);
        return;
      } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
        SelectLoad(Node, 4, AArch64::LD1Fourv1d, AArch64::dsub0);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
        SelectLoad(Node, 4, AArch64::LD4Fourv2d, AArch64::qsub0);
        return;
      }
      break;
    case Intrinsic::aarch64_neon_ld2r:
      if (VT == MVT::v8i8) {
        SelectLoad(Node, 2, AArch64::LD2Rv8b, AArch64::dsub0);
        return;
      } else if (VT == MVT::v16i8) {
        SelectLoad(Node, 2, AArch64::LD2Rv16b, AArch64::qsub0);
        return;
      } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
        SelectLoad(Node, 2, AArch64::LD2Rv4h, AArch64::dsub0);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v8bf16) {
        SelectLoad(Node, 2, AArch64::LD2Rv8h, AArch64::qsub0);
        return;
      } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
        SelectLoad(Node, 2, AArch64::LD2Rv2s, AArch64::dsub0);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
        SelectLoad(Node, 2, AArch64::LD2Rv4s, AArch64::qsub0);
        return;
      } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
        SelectLoad(Node, 2, AArch64::LD2Rv1d, AArch64::dsub0);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
        SelectLoad(Node, 2, AArch64::LD2Rv2d, AArch64::qsub0);
        return;
      }
      break;
    case Intrinsic::aarch64_neon_ld3r:
      if (VT == MVT::v8i8) {
        SelectLoad(Node, 3, AArch64::LD3Rv8b, AArch64::dsub0);
        return;
      } else if (VT == MVT::v16i8) {
        SelectLoad(Node, 3, AArch64::LD3Rv16b, AArch64::qsub0);
        return;
      } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
        SelectLoad(Node, 3, AArch64::LD3Rv4h, AArch64::dsub0);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v8bf16) {
        SelectLoad(Node, 3, AArch64::LD3Rv8h, AArch64::qsub0);
        return;
      } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
        SelectLoad(Node, 3, AArch64::LD3Rv2s, AArch64::dsub0);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
        SelectLoad(Node, 3, AArch64::LD3Rv4s, AArch64::qsub0);
        return;
      } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
        SelectLoad(Node, 3, AArch64::LD3Rv1d, AArch64::dsub0);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
        SelectLoad(Node, 3, AArch64::LD3Rv2d, AArch64::qsub0);
        return;
      }
      break;
    case Intrinsic::aarch64_neon_ld4r:
      if (VT == MVT::v8i8) {
        SelectLoad(Node, 4, AArch64::LD4Rv8b, AArch64::dsub0);
        return;
      } else if (VT == MVT::v16i8) {
        SelectLoad(Node, 4, AArch64::LD4Rv16b, AArch64::qsub0);
        return;
      } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
        SelectLoad(Node, 4, AArch64::LD4Rv4h, AArch64::dsub0);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v8bf16) {
        SelectLoad(Node, 4, AArch64::LD4Rv8h, AArch64::qsub0);
        return;
      } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
        SelectLoad(Node, 4, AArch64::LD4Rv2s, AArch64::dsub0);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
        SelectLoad(Node, 4, AArch64::LD4Rv4s, AArch64::qsub0);
        return;
      } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
        SelectLoad(Node, 4, AArch64::LD4Rv1d, AArch64::dsub0);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
        SelectLoad(Node, 4, AArch64::LD4Rv2d, AArch64::qsub0);
        return;
      }
      break;
    case Intrinsic::aarch64_neon_ld2lane:
      if (VT == MVT::v16i8 || VT == MVT::v8i8) {
        SelectLoadLane(Node, 2, AArch64::LD2i8);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v4i16 || VT == MVT::v4f16 ||
                 VT == MVT::v8f16 || VT == MVT::v4bf16 || VT == MVT::v8bf16) {
        SelectLoadLane(Node, 2, AArch64::LD2i16);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v2i32 || VT == MVT::v4f32 ||
                 VT == MVT::v2f32) {
        SelectLoadLane(Node, 2, AArch64::LD2i32);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v1i64 || VT == MVT::v2f64 ||
                 VT == MVT::v1f64) {
        SelectLoadLane(Node, 2, AArch64::LD2i64);
        return;
      }
      break;
    case Intrinsic::aarch64_neon_ld3lane:
      if (VT == MVT::v16i8 || VT == MVT::v8i8) {
        SelectLoadLane(Node, 3, AArch64::LD3i8);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v4i16 || VT == MVT::v4f16 ||
                 VT == MVT::v8f16 || VT == MVT::v4bf16 || VT == MVT::v8bf16) {
        SelectLoadLane(Node, 3, AArch64::LD3i16);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v2i32 || VT == MVT::v4f32 ||
                 VT == MVT::v2f32) {
        SelectLoadLane(Node, 3, AArch64::LD3i32);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v1i64 || VT == MVT::v2f64 ||
                 VT == MVT::v1f64) {
        SelectLoadLane(Node, 3, AArch64::LD3i64);
        return;
      }
      break;
    case Intrinsic::aarch64_neon_ld4lane:
      if (VT == MVT::v16i8 || VT == MVT::v8i8) {
        SelectLoadLane(Node, 4, AArch64::LD4i8);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v4i16 || VT == MVT::v4f16 ||
                 VT == MVT::v8f16 || VT == MVT::v4bf16 || VT == MVT::v8bf16) {
        SelectLoadLane(Node, 4, AArch64::LD4i16);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v2i32 || VT == MVT::v4f32 ||
                 VT == MVT::v2f32) {
        SelectLoadLane(Node, 4, AArch64::LD4i32);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v1i64 || VT == MVT::v2f64 ||
                 VT == MVT::v1f64) {
        SelectLoadLane(Node, 4, AArch64::LD4i64);
        return;
      }
      break;
    case Intrinsic::aarch64_ld64b:
      SelectLoad(Node, 8, AArch64::LD64B, AArch64::x8sub_0);
      return;
    case Intrinsic::aarch64_sve_ld2q_sret: {
      SelectPredicatedLoad(Node, 2, 4, AArch64::LD2Q_IMM, AArch64::LD2Q, true);
      return;
    }
    case Intrinsic::aarch64_sve_ld3q_sret: {
      SelectPredicatedLoad(Node, 3, 4, AArch64::LD3Q_IMM, AArch64::LD3Q, true);
      return;
    }
    case Intrinsic::aarch64_sve_ld4q_sret: {
      SelectPredicatedLoad(Node, 4, 4, AArch64::LD4Q_IMM, AArch64::LD4Q, true);
      return;
    }
    case Intrinsic::aarch64_sve_ld2_sret: {
      if (VT == MVT::nxv16i8) {
        SelectPredicatedLoad(Node, 2, 0, AArch64::LD2B_IMM, AArch64::LD2B,
                             true);
        return;
      } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
                 VT == MVT::nxv8bf16) {
        SelectPredicatedLoad(Node, 2, 1, AArch64::LD2H_IMM, AArch64::LD2H,
                             true);
        return;
      } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
        SelectPredicatedLoad(Node, 2, 2, AArch64::LD2W_IMM, AArch64::LD2W,
                             true);
        return;
      } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
        SelectPredicatedLoad(Node, 2, 3, AArch64::LD2D_IMM, AArch64::LD2D,
                             true);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_sve_ld1_pn_x2: {
      if (VT == MVT::nxv16i8) {
        if (Subtarget->hasSME2())
          SelectContiguousMultiVectorLoad(
              Node, 2, 0, AArch64::LD1B_2Z_IMM_PSEUDO, AArch64::LD1B_2Z_PSEUDO);
        else if (Subtarget->hasSVE2p1())
          SelectContiguousMultiVectorLoad(Node, 2, 0, AArch64::LD1B_2Z_IMM,
                                          AArch64::LD1B_2Z);
        else
          break;
        return;
      } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
                 VT == MVT::nxv8bf16) {
        if (Subtarget->hasSME2())
          SelectContiguousMultiVectorLoad(
              Node, 2, 1, AArch64::LD1H_2Z_IMM_PSEUDO, AArch64::LD1H_2Z_PSEUDO);
        else if (Subtarget->hasSVE2p1())
          SelectContiguousMultiVectorLoad(Node, 2, 1, AArch64::LD1H_2Z_IMM,
                                          AArch64::LD1H_2Z);
        else
          break;
        return;
      } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
        if (Subtarget->hasSME2())
          SelectContiguousMultiVectorLoad(
              Node, 2, 2, AArch64::LD1W_2Z_IMM_PSEUDO, AArch64::LD1W_2Z_PSEUDO);
        else if (Subtarget->hasSVE2p1())
          SelectContiguousMultiVectorLoad(Node, 2, 2, AArch64::LD1W_2Z_IMM,
                                          AArch64::LD1W_2Z);
        else
          break;
        return;
      } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
        if (Subtarget->hasSME2())
          SelectContiguousMultiVectorLoad(
              Node, 2, 3, AArch64::LD1D_2Z_IMM_PSEUDO, AArch64::LD1D_2Z_PSEUDO);
        else if (Subtarget->hasSVE2p1())
          SelectContiguousMultiVectorLoad(Node, 2, 3, AArch64::LD1D_2Z_IMM,
                                          AArch64::LD1D_2Z);
        else
          break;
        return;
      }
      break;
    }
    case Intrinsic::aarch64_sve_ld1_pn_x4: {
      if (VT == MVT::nxv16i8) {
        if (Subtarget->hasSME2())
          SelectContiguousMultiVectorLoad(
              Node, 4, 0, AArch64::LD1B_4Z_IMM_PSEUDO, AArch64::LD1B_4Z_PSEUDO);
        else if (Subtarget->hasSVE2p1())
          SelectContiguousMultiVectorLoad(Node, 4, 0, AArch64::LD1B_4Z_IMM,
                                          AArch64::LD1B_4Z);
        else
          break;
        return;
      } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
                 VT == MVT::nxv8bf16) {
        if (Subtarget->hasSME2())
          SelectContiguousMultiVectorLoad(
              Node, 4, 1, AArch64::LD1H_4Z_IMM_PSEUDO, AArch64::LD1H_4Z_PSEUDO);
        else if (Subtarget->hasSVE2p1())
          SelectContiguousMultiVectorLoad(Node, 4, 1, AArch64::LD1H_4Z_IMM,
                                          AArch64::LD1H_4Z);
        else
          break;
        return;
      } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
        if (Subtarget->hasSME2())
          SelectContiguousMultiVectorLoad(
              Node, 4, 2, AArch64::LD1W_4Z_IMM_PSEUDO, AArch64::LD1W_4Z_PSEUDO);
        else if (Subtarget->hasSVE2p1())
          SelectContiguousMultiVectorLoad(Node, 4, 2, AArch64::LD1W_4Z_IMM,
                                          AArch64::LD1W_4Z);
        else
          break;
        return;
      } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
        if (Subtarget->hasSME2())
          SelectContiguousMultiVectorLoad(
              Node, 4, 3, AArch64::LD1D_4Z_IMM_PSEUDO, AArch64::LD1D_4Z_PSEUDO);
        else if (Subtarget->hasSVE2p1())
          SelectContiguousMultiVectorLoad(Node, 4, 3, AArch64::LD1D_4Z_IMM,
                                          AArch64::LD1D_4Z);
        else
          break;
        return;
      }
      break;
    }
    case Intrinsic::aarch64_sve_ldnt1_pn_x2: {
      if (VT == MVT::nxv16i8) {
        if (Subtarget->hasSME2())
          SelectContiguousMultiVectorLoad(Node, 2, 0,
                                          AArch64::LDNT1B_2Z_IMM_PSEUDO,
                                          AArch64::LDNT1B_2Z_PSEUDO);
        else if (Subtarget->hasSVE2p1())
          SelectContiguousMultiVectorLoad(Node, 2, 0, AArch64::LDNT1B_2Z_IMM,
                                          AArch64::LDNT1B_2Z);
        else
          break;
        return;
      } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
                 VT == MVT::nxv8bf16) {
        if (Subtarget->hasSME2())
          SelectContiguousMultiVectorLoad(Node, 2, 1,
                                          AArch64::LDNT1H_2Z_IMM_PSEUDO,
                                          AArch64::LDNT1H_2Z_PSEUDO);
        else if (Subtarget->hasSVE2p1())
          SelectContiguousMultiVectorLoad(Node, 2, 1, AArch64::LDNT1H_2Z_IMM,
                                          AArch64::LDNT1H_2Z);
        else
          break;
        return;
      } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
        if (Subtarget->hasSME2())
          SelectContiguousMultiVectorLoad(Node, 2, 2,
                                          AArch64::LDNT1W_2Z_IMM_PSEUDO,
                                          AArch64::LDNT1W_2Z_PSEUDO);
        else if (Subtarget->hasSVE2p1())
          SelectContiguousMultiVectorLoad(Node, 2, 2, AArch64::LDNT1W_2Z_IMM,
                                          AArch64::LDNT1W_2Z);
        else
          break;
        return;
      } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
        if (Subtarget->hasSME2())
          SelectContiguousMultiVectorLoad(Node, 2, 3,
                                          AArch64::LDNT1D_2Z_IMM_PSEUDO,
                                          AArch64::LDNT1D_2Z_PSEUDO);
        else if (Subtarget->hasSVE2p1())
          SelectContiguousMultiVectorLoad(Node, 2, 3, AArch64::LDNT1D_2Z_IMM,
                                          AArch64::LDNT1D_2Z);
        else
          break;
        return;
      }
      break;
    }
    case Intrinsic::aarch64_sve_ldnt1_pn_x4: {
      if (VT == MVT::nxv16i8) {
        if (Subtarget->hasSME2())
          SelectContiguousMultiVectorLoad(Node, 4, 0,
                                          AArch64::LDNT1B_4Z_IMM_PSEUDO,
                                          AArch64::LDNT1B_4Z_PSEUDO);
        else if (Subtarget->hasSVE2p1())
          SelectContiguousMultiVectorLoad(Node, 4, 0, AArch64::LDNT1B_4Z_IMM,
                                          AArch64::LDNT1B_4Z);
        else
          break;
        return;
      } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
                 VT == MVT::nxv8bf16) {
        if (Subtarget->hasSME2())
          SelectContiguousMultiVectorLoad(Node, 4, 1,
                                          AArch64::LDNT1H_4Z_IMM_PSEUDO,
                                          AArch64::LDNT1H_4Z_PSEUDO);
        else if (Subtarget->hasSVE2p1())
          SelectContiguousMultiVectorLoad(Node, 4, 1, AArch64::LDNT1H_4Z_IMM,
                                          AArch64::LDNT1H_4Z);
        else
          break;
        return;
      } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
        if (Subtarget->hasSME2())
          SelectContiguousMultiVectorLoad(Node, 4, 2,
                                          AArch64::LDNT1W_4Z_IMM_PSEUDO,
                                          AArch64::LDNT1W_4Z_PSEUDO);
        else if (Subtarget->hasSVE2p1())
          SelectContiguousMultiVectorLoad(Node, 4, 2, AArch64::LDNT1W_4Z_IMM,
                                          AArch64::LDNT1W_4Z);
        else
          break;
        return;
      } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
        if (Subtarget->hasSME2())
          SelectContiguousMultiVectorLoad(Node, 4, 3,
                                          AArch64::LDNT1D_4Z_IMM_PSEUDO,
                                          AArch64::LDNT1D_4Z_PSEUDO);
        else if (Subtarget->hasSVE2p1())
          SelectContiguousMultiVectorLoad(Node, 4, 3, AArch64::LDNT1D_4Z_IMM,
                                          AArch64::LDNT1D_4Z);
        else
          break;
        return;
      }
      break;
    }
    case Intrinsic::aarch64_sve_ld3_sret: {
      if (VT == MVT::nxv16i8) {
        SelectPredicatedLoad(Node, 3, 0, AArch64::LD3B_IMM, AArch64::LD3B,
                             true);
        return;
      } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
                 VT == MVT::nxv8bf16) {
        SelectPredicatedLoad(Node, 3, 1, AArch64::LD3H_IMM, AArch64::LD3H,
                             true);
        return;
      } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
        SelectPredicatedLoad(Node, 3, 2, AArch64::LD3W_IMM, AArch64::LD3W,
                             true);
        return;
      } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
        SelectPredicatedLoad(Node, 3, 3, AArch64::LD3D_IMM, AArch64::LD3D,
                             true);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_sve_ld4_sret: {
      if (VT == MVT::nxv16i8) {
        SelectPredicatedLoad(Node, 4, 0, AArch64::LD4B_IMM, AArch64::LD4B,
                             true);
        return;
      } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
                 VT == MVT::nxv8bf16) {
        SelectPredicatedLoad(Node, 4, 1, AArch64::LD4H_IMM, AArch64::LD4H,
                             true);
        return;
      } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
        SelectPredicatedLoad(Node, 4, 2, AArch64::LD4W_IMM, AArch64::LD4W,
                             true);
        return;
      } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
        SelectPredicatedLoad(Node, 4, 3, AArch64::LD4D_IMM, AArch64::LD4D,
                             true);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_sme_read_hor_vg2: {
      if (VT == MVT::nxv16i8) {
        SelectMultiVectorMove<14, 2>(Node, 2, AArch64::ZAB0,
                                     AArch64::MOVA_2ZMXI_H_B);
        return;
      } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
                 VT == MVT::nxv8bf16) {
        SelectMultiVectorMove<6, 2>(Node, 2, AArch64::ZAH0,
                                    AArch64::MOVA_2ZMXI_H_H);
        return;
      } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
        SelectMultiVectorMove<2, 2>(Node, 2, AArch64::ZAS0,
                                    AArch64::MOVA_2ZMXI_H_S);
        return;
      } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
        SelectMultiVectorMove<0, 2>(Node, 2, AArch64::ZAD0,
                                    AArch64::MOVA_2ZMXI_H_D);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_sme_read_ver_vg2: {
      if (VT == MVT::nxv16i8) {
        SelectMultiVectorMove<14, 2>(Node, 2, AArch64::ZAB0,
                                     AArch64::MOVA_2ZMXI_V_B);
        return;
      } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
                 VT == MVT::nxv8bf16) {
        SelectMultiVectorMove<6, 2>(Node, 2, AArch64::ZAH0,
                                    AArch64::MOVA_2ZMXI_V_H);
        return;
      } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
        SelectMultiVectorMove<2, 2>(Node, 2, AArch64::ZAS0,
                                    AArch64::MOVA_2ZMXI_V_S);
        return;
      } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
        SelectMultiVectorMove<0, 2>(Node, 2, AArch64::ZAD0,
                                    AArch64::MOVA_2ZMXI_V_D);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_sme_read_hor_vg4: {
      if (VT == MVT::nxv16i8) {
        SelectMultiVectorMove<12, 4>(Node, 4, AArch64::ZAB0,
                                     AArch64::MOVA_4ZMXI_H_B);
        return;
      } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
                 VT == MVT::nxv8bf16) {
        SelectMultiVectorMove<4, 4>(Node, 4, AArch64::ZAH0,
                                    AArch64::MOVA_4ZMXI_H_H);
        return;
      } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
        SelectMultiVectorMove<0, 2>(Node, 4, AArch64::ZAS0,
                                    AArch64::MOVA_4ZMXI_H_S);
        return;
      } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
        SelectMultiVectorMove<0, 2>(Node, 4, AArch64::ZAD0,
                                    AArch64::MOVA_4ZMXI_H_D);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_sme_read_ver_vg4: {
      if (VT == MVT::nxv16i8) {
        SelectMultiVectorMove<12, 4>(Node, 4, AArch64::ZAB0,
                                     AArch64::MOVA_4ZMXI_V_B);
        return;
      } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
                 VT == MVT::nxv8bf16) {
        SelectMultiVectorMove<4, 4>(Node, 4, AArch64::ZAH0,
                                    AArch64::MOVA_4ZMXI_V_H);
        return;
      } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
        SelectMultiVectorMove<0, 4>(Node, 4, AArch64::ZAS0,
                                    AArch64::MOVA_4ZMXI_V_S);
        return;
      } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
        SelectMultiVectorMove<0, 4>(Node, 4, AArch64::ZAD0,
                                    AArch64::MOVA_4ZMXI_V_D);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_sme_read_vg1x2: {
      SelectMultiVectorMove<7, 1>(Node, 2, AArch64::ZA,
                                  AArch64::MOVA_VG2_2ZMXI);
      return;
    }
    case Intrinsic::aarch64_sme_read_vg1x4: {
      SelectMultiVectorMove<7, 1>(Node, 4, AArch64::ZA,
                                  AArch64::MOVA_VG4_4ZMXI);
      return;
    }
    case Intrinsic::aarch64_sme_readz_horiz_x2: {
      if (VT == MVT::nxv16i8) {
        SelectMultiVectorMoveZ(Node, 2, AArch64::MOVAZ_2ZMI_H_B_PSEUDO, 14, 2);
        return;
      } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
                 VT == MVT::nxv8bf16) {
        SelectMultiVectorMoveZ(Node, 2, AArch64::MOVAZ_2ZMI_H_H_PSEUDO, 6, 2);
        return;
      } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
        SelectMultiVectorMoveZ(Node, 2, AArch64::MOVAZ_2ZMI_H_S_PSEUDO, 2, 2);
        return;
      } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
        SelectMultiVectorMoveZ(Node, 2, AArch64::MOVAZ_2ZMI_H_D_PSEUDO, 0, 2);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_sme_readz_vert_x2: {
      if (VT == MVT::nxv16i8) {
        SelectMultiVectorMoveZ(Node, 2, AArch64::MOVAZ_2ZMI_V_B_PSEUDO, 14, 2);
        return;
      } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
                 VT == MVT::nxv8bf16) {
        SelectMultiVectorMoveZ(Node, 2, AArch64::MOVAZ_2ZMI_V_H_PSEUDO, 6, 2);
        return;
      } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
        SelectMultiVectorMoveZ(Node, 2, AArch64::MOVAZ_2ZMI_V_S_PSEUDO, 2, 2);
        return;
      } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
        SelectMultiVectorMoveZ(Node, 2, AArch64::MOVAZ_2ZMI_V_D_PSEUDO, 0, 2);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_sme_readz_horiz_x4: {
      if (VT == MVT::nxv16i8) {
        SelectMultiVectorMoveZ(Node, 4, AArch64::MOVAZ_4ZMI_H_B_PSEUDO, 12, 4);
        return;
      } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
                 VT == MVT::nxv8bf16) {
        SelectMultiVectorMoveZ(Node, 4, AArch64::MOVAZ_4ZMI_H_H_PSEUDO, 4, 4);
        return;
      } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
        SelectMultiVectorMoveZ(Node, 4, AArch64::MOVAZ_4ZMI_H_S_PSEUDO, 0, 4);
        return;
      } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
        SelectMultiVectorMoveZ(Node, 4, AArch64::MOVAZ_4ZMI_H_D_PSEUDO, 0, 4);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_sme_readz_vert_x4: {
      if (VT == MVT::nxv16i8) {
        SelectMultiVectorMoveZ(Node, 4, AArch64::MOVAZ_4ZMI_V_B_PSEUDO, 12, 4);
        return;
      } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
                 VT == MVT::nxv8bf16) {
        SelectMultiVectorMoveZ(Node, 4, AArch64::MOVAZ_4ZMI_V_H_PSEUDO, 4, 4);
        return;
      } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
        SelectMultiVectorMoveZ(Node, 4, AArch64::MOVAZ_4ZMI_V_S_PSEUDO, 0, 4);
        return;
      } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
        SelectMultiVectorMoveZ(Node, 4, AArch64::MOVAZ_4ZMI_V_D_PSEUDO, 0, 4);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_sme_readz_x2: {
      SelectMultiVectorMoveZ(Node, 2, AArch64::MOVAZ_VG2_2ZMXI_PSEUDO, 7, 1,
                             AArch64::ZA);
      return;
    }
    case Intrinsic::aarch64_sme_readz_x4: {
      SelectMultiVectorMoveZ(Node, 4, AArch64::MOVAZ_VG4_4ZMXI_PSEUDO, 7, 1,
                             AArch64::ZA);
      return;
    }
    case Intrinsic::swift_async_context_addr: {
      SDLoc DL(Node);
      SDValue Chain = Node->getOperand(0);
      SDValue CopyFP = CurDAG->getCopyFromReg(Chain, DL, AArch64::FP, MVT::i64);
      SDValue Res = SDValue(
          CurDAG->getMachineNode(AArch64::SUBXri, DL, MVT::i64, CopyFP,
                                 CurDAG->getTargetConstant(8, DL, MVT::i32),
                                 CurDAG->getTargetConstant(0, DL, MVT::i32)),
          0);
      ReplaceUses(SDValue(Node, 0), Res);
      ReplaceUses(SDValue(Node, 1), CopyFP.getValue(1));
      CurDAG->RemoveDeadNode(Node);

      auto &MF = CurDAG->getMachineFunction();
      MF.getFrameInfo().setFrameAddressIsTaken(true);
      MF.getInfo<AArch64FunctionInfo>()->setHasSwiftAsyncContext(true);
      return;
    }
    case Intrinsic::aarch64_sme_luti2_lane_zt_x4: {
      if (auto Opc = SelectOpcodeFromVT<SelectTypeKind::AnyType>(
              Node->getValueType(0),
              {AArch64::LUTI2_4ZTZI_B, AArch64::LUTI2_4ZTZI_H,
               AArch64::LUTI2_4ZTZI_S}))
        // Second Immediate must be <= 3:
        SelectMultiVectorLuti(Node, 4, Opc, 3);
      return;
    }
    case Intrinsic::aarch64_sme_luti4_lane_zt_x4: {
      if (auto Opc = SelectOpcodeFromVT<SelectTypeKind::AnyType>(
              Node->getValueType(0),
              {0, AArch64::LUTI4_4ZTZI_H, AArch64::LUTI4_4ZTZI_S}))
        // Second Immediate must be <= 1:
        SelectMultiVectorLuti(Node, 4, Opc, 1);
      return;
    }
    case Intrinsic::aarch64_sme_luti2_lane_zt_x2: {
      if (auto Opc = SelectOpcodeFromVT<SelectTypeKind::AnyType>(
              Node->getValueType(0),
              {AArch64::LUTI2_2ZTZI_B, AArch64::LUTI2_2ZTZI_H,
               AArch64::LUTI2_2ZTZI_S}))
        // Second Immediate must be <= 7:
        SelectMultiVectorLuti(Node, 2, Opc, 7);
      return;
    }
    case Intrinsic::aarch64_sme_luti4_lane_zt_x2: {
      if (auto Opc = SelectOpcodeFromVT<SelectTypeKind::AnyType>(
              Node->getValueType(0),
              {AArch64::LUTI4_2ZTZI_B, AArch64::LUTI4_2ZTZI_H,
               AArch64::LUTI4_2ZTZI_S}))
        // Second Immediate must be <= 3:
        SelectMultiVectorLuti(Node, 2, Opc, 3);
      return;
    }
    }
  } break;
  case ISD::INTRINSIC_WO_CHAIN: {
    unsigned IntNo = Node->getConstantOperandVal(0);
    switch (IntNo) {
    default:
      break;
    case Intrinsic::aarch64_tagp:
      SelectTagP(Node);
      return;

    case Intrinsic::ptrauth_auth:
      SelectPtrauthAuth(Node);
      return;

    case Intrinsic::ptrauth_resign:
      SelectPtrauthResign(Node);
      return;

    case Intrinsic::aarch64_neon_tbl2:
      SelectTable(Node, 2,
                  VT == MVT::v8i8 ? AArch64::TBLv8i8Two : AArch64::TBLv16i8Two,
                  false);
      return;
    case Intrinsic::aarch64_neon_tbl3:
      SelectTable(Node, 3, VT == MVT::v8i8 ? AArch64::TBLv8i8Three
                                           : AArch64::TBLv16i8Three,
                  false);
      return;
    case Intrinsic::aarch64_neon_tbl4:
      SelectTable(Node, 4, VT == MVT::v8i8 ? AArch64::TBLv8i8Four
                                           : AArch64::TBLv16i8Four,
                  false);
      return;
    case Intrinsic::aarch64_neon_tbx2:
      SelectTable(Node, 2,
                  VT == MVT::v8i8 ? AArch64::TBXv8i8Two : AArch64::TBXv16i8Two,
                  true);
      return;
    case Intrinsic::aarch64_neon_tbx3:
      SelectTable(Node, 3, VT == MVT::v8i8 ? AArch64::TBXv8i8Three
                                           : AArch64::TBXv16i8Three,
                  true);
      return;
    case Intrinsic::aarch64_neon_tbx4:
      SelectTable(Node, 4, VT == MVT::v8i8 ? AArch64::TBXv8i8Four
                                           : AArch64::TBXv16i8Four,
                  true);
      return;
    case Intrinsic::aarch64_sve_srshl_single_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::SRSHL_VG2_2ZZ_B, AArch64::SRSHL_VG2_2ZZ_H,
               AArch64::SRSHL_VG2_2ZZ_S, AArch64::SRSHL_VG2_2ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, false, Op);
      return;
    case Intrinsic::aarch64_sve_srshl_single_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::SRSHL_VG4_4ZZ_B, AArch64::SRSHL_VG4_4ZZ_H,
               AArch64::SRSHL_VG4_4ZZ_S, AArch64::SRSHL_VG4_4ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, false, Op);
      return;
    case Intrinsic::aarch64_sve_urshl_single_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::URSHL_VG2_2ZZ_B, AArch64::URSHL_VG2_2ZZ_H,
               AArch64::URSHL_VG2_2ZZ_S, AArch64::URSHL_VG2_2ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, false, Op);
      return;
    case Intrinsic::aarch64_sve_urshl_single_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::URSHL_VG4_4ZZ_B, AArch64::URSHL_VG4_4ZZ_H,
               AArch64::URSHL_VG4_4ZZ_S, AArch64::URSHL_VG4_4ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, false, Op);
      return;
    case Intrinsic::aarch64_sve_srshl_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::SRSHL_VG2_2Z2Z_B, AArch64::SRSHL_VG2_2Z2Z_H,
               AArch64::SRSHL_VG2_2Z2Z_S, AArch64::SRSHL_VG2_2Z2Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, true, Op);
      return;
    case Intrinsic::aarch64_sve_srshl_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::SRSHL_VG4_4Z4Z_B, AArch64::SRSHL_VG4_4Z4Z_H,
               AArch64::SRSHL_VG4_4Z4Z_S, AArch64::SRSHL_VG4_4Z4Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, true, Op);
      return;
    case Intrinsic::aarch64_sve_urshl_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::URSHL_VG2_2Z2Z_B, AArch64::URSHL_VG2_2Z2Z_H,
               AArch64::URSHL_VG2_2Z2Z_S, AArch64::URSHL_VG2_2Z2Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, true, Op);
      return;
    case Intrinsic::aarch64_sve_urshl_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::URSHL_VG4_4Z4Z_B, AArch64::URSHL_VG4_4Z4Z_H,
               AArch64::URSHL_VG4_4Z4Z_S, AArch64::URSHL_VG4_4Z4Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, true, Op);
      return;
    case Intrinsic::aarch64_sve_sqdmulh_single_vgx2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::SQDMULH_VG2_2ZZ_B, AArch64::SQDMULH_VG2_2ZZ_H,
               AArch64::SQDMULH_VG2_2ZZ_S, AArch64::SQDMULH_VG2_2ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, false, Op);
      return;
    case Intrinsic::aarch64_sve_sqdmulh_single_vgx4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::SQDMULH_VG4_4ZZ_B, AArch64::SQDMULH_VG4_4ZZ_H,
               AArch64::SQDMULH_VG4_4ZZ_S, AArch64::SQDMULH_VG4_4ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, false, Op);
      return;
    case Intrinsic::aarch64_sve_sqdmulh_vgx2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::SQDMULH_VG2_2Z2Z_B, AArch64::SQDMULH_VG2_2Z2Z_H,
               AArch64::SQDMULH_VG2_2Z2Z_S, AArch64::SQDMULH_VG2_2Z2Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, true, Op);
      return;
    case Intrinsic::aarch64_sve_sqdmulh_vgx4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::SQDMULH_VG4_4Z4Z_B, AArch64::SQDMULH_VG4_4Z4Z_H,
               AArch64::SQDMULH_VG4_4Z4Z_S, AArch64::SQDMULH_VG4_4Z4Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, true, Op);
      return;
    case Intrinsic::aarch64_sve_whilege_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int1>(
              Node->getValueType(0),
              {AArch64::WHILEGE_2PXX_B, AArch64::WHILEGE_2PXX_H,
               AArch64::WHILEGE_2PXX_S, AArch64::WHILEGE_2PXX_D}))
        SelectWhilePair(Node, Op);
      return;
    case Intrinsic::aarch64_sve_whilegt_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int1>(
              Node->getValueType(0),
              {AArch64::WHILEGT_2PXX_B, AArch64::WHILEGT_2PXX_H,
               AArch64::WHILEGT_2PXX_S, AArch64::WHILEGT_2PXX_D}))
        SelectWhilePair(Node, Op);
      return;
    case Intrinsic::aarch64_sve_whilehi_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int1>(
              Node->getValueType(0),
              {AArch64::WHILEHI_2PXX_B, AArch64::WHILEHI_2PXX_H,
               AArch64::WHILEHI_2PXX_S, AArch64::WHILEHI_2PXX_D}))
        SelectWhilePair(Node, Op);
      return;
    case Intrinsic::aarch64_sve_whilehs_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int1>(
              Node->getValueType(0),
              {AArch64::WHILEHS_2PXX_B, AArch64::WHILEHS_2PXX_H,
               AArch64::WHILEHS_2PXX_S, AArch64::WHILEHS_2PXX_D}))
        SelectWhilePair(Node, Op);
      return;
    case Intrinsic::aarch64_sve_whilele_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int1>(
              Node->getValueType(0),
              {AArch64::WHILELE_2PXX_B, AArch64::WHILELE_2PXX_H,
               AArch64::WHILELE_2PXX_S, AArch64::WHILELE_2PXX_D}))
      SelectWhilePair(Node, Op);
      return;
    case Intrinsic::aarch64_sve_whilelo_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int1>(
              Node->getValueType(0),
              {AArch64::WHILELO_2PXX_B, AArch64::WHILELO_2PXX_H,
               AArch64::WHILELO_2PXX_S, AArch64::WHILELO_2PXX_D}))
      SelectWhilePair(Node, Op);
      return;
    case Intrinsic::aarch64_sve_whilels_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int1>(
              Node->getValueType(0),
              {AArch64::WHILELS_2PXX_B, AArch64::WHILELS_2PXX_H,
               AArch64::WHILELS_2PXX_S, AArch64::WHILELS_2PXX_D}))
        SelectWhilePair(Node, Op);
      return;
    case Intrinsic::aarch64_sve_whilelt_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int1>(
              Node->getValueType(0),
              {AArch64::WHILELT_2PXX_B, AArch64::WHILELT_2PXX_H,
               AArch64::WHILELT_2PXX_S, AArch64::WHILELT_2PXX_D}))
        SelectWhilePair(Node, Op);
      return;
    case Intrinsic::aarch64_sve_smax_single_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::SMAX_VG2_2ZZ_B, AArch64::SMAX_VG2_2ZZ_H,
               AArch64::SMAX_VG2_2ZZ_S, AArch64::SMAX_VG2_2ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, false, Op);
      return;
    case Intrinsic::aarch64_sve_umax_single_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::UMAX_VG2_2ZZ_B, AArch64::UMAX_VG2_2ZZ_H,
               AArch64::UMAX_VG2_2ZZ_S, AArch64::UMAX_VG2_2ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, false, Op);
      return;
    case Intrinsic::aarch64_sve_fmax_single_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::FP>(
              Node->getValueType(0),
              {AArch64::BFMAX_VG2_2ZZ_H, AArch64::FMAX_VG2_2ZZ_H,
               AArch64::FMAX_VG2_2ZZ_S, AArch64::FMAX_VG2_2ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, false, Op);
      return;
    case Intrinsic::aarch64_sve_smax_single_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::SMAX_VG4_4ZZ_B, AArch64::SMAX_VG4_4ZZ_H,
               AArch64::SMAX_VG4_4ZZ_S, AArch64::SMAX_VG4_4ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, false, Op);
      return;
    case Intrinsic::aarch64_sve_umax_single_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::UMAX_VG4_4ZZ_B, AArch64::UMAX_VG4_4ZZ_H,
               AArch64::UMAX_VG4_4ZZ_S, AArch64::UMAX_VG4_4ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, false, Op);
      return;
    case Intrinsic::aarch64_sve_fmax_single_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::FP>(
              Node->getValueType(0),
              {AArch64::BFMAX_VG4_4ZZ_H, AArch64::FMAX_VG4_4ZZ_H,
               AArch64::FMAX_VG4_4ZZ_S, AArch64::FMAX_VG4_4ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, false, Op);
      return;
    case Intrinsic::aarch64_sve_smin_single_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::SMIN_VG2_2ZZ_B, AArch64::SMIN_VG2_2ZZ_H,
               AArch64::SMIN_VG2_2ZZ_S, AArch64::SMIN_VG2_2ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, false, Op);
      return;
    case Intrinsic::aarch64_sve_umin_single_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::UMIN_VG2_2ZZ_B, AArch64::UMIN_VG2_2ZZ_H,
               AArch64::UMIN_VG2_2ZZ_S, AArch64::UMIN_VG2_2ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, false, Op);
      return;
    case Intrinsic::aarch64_sve_fmin_single_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::FP>(
              Node->getValueType(0),
              {AArch64::BFMIN_VG2_2ZZ_H, AArch64::FMIN_VG2_2ZZ_H,
               AArch64::FMIN_VG2_2ZZ_S, AArch64::FMIN_VG2_2ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, false, Op);
      return;
    case Intrinsic::aarch64_sve_smin_single_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::SMIN_VG4_4ZZ_B, AArch64::SMIN_VG4_4ZZ_H,
               AArch64::SMIN_VG4_4ZZ_S, AArch64::SMIN_VG4_4ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, false, Op);
      return;
    case Intrinsic::aarch64_sve_umin_single_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::UMIN_VG4_4ZZ_B, AArch64::UMIN_VG4_4ZZ_H,
               AArch64::UMIN_VG4_4ZZ_S, AArch64::UMIN_VG4_4ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, false, Op);
      return;
    case Intrinsic::aarch64_sve_fmin_single_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::FP>(
              Node->getValueType(0),
              {AArch64::BFMIN_VG4_4ZZ_H, AArch64::FMIN_VG4_4ZZ_H,
               AArch64::FMIN_VG4_4ZZ_S, AArch64::FMIN_VG4_4ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, false, Op);
      return;
    case Intrinsic::aarch64_sve_smax_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::SMAX_VG2_2Z2Z_B, AArch64::SMAX_VG2_2Z2Z_H,
               AArch64::SMAX_VG2_2Z2Z_S, AArch64::SMAX_VG2_2Z2Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, true, Op);
      return;
    case Intrinsic::aarch64_sve_umax_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::UMAX_VG2_2Z2Z_B, AArch64::UMAX_VG2_2Z2Z_H,
               AArch64::UMAX_VG2_2Z2Z_S, AArch64::UMAX_VG2_2Z2Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, true, Op);
      return;
    case Intrinsic::aarch64_sve_fmax_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::FP>(
              Node->getValueType(0),
              {AArch64::BFMAX_VG2_2Z2Z_H, AArch64::FMAX_VG2_2Z2Z_H,
               AArch64::FMAX_VG2_2Z2Z_S, AArch64::FMAX_VG2_2Z2Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, true, Op);
      return;
    case Intrinsic::aarch64_sve_smax_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::SMAX_VG4_4Z4Z_B, AArch64::SMAX_VG4_4Z4Z_H,
               AArch64::SMAX_VG4_4Z4Z_S, AArch64::SMAX_VG4_4Z4Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, true, Op);
      return;
    case Intrinsic::aarch64_sve_umax_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::UMAX_VG4_4Z4Z_B, AArch64::UMAX_VG4_4Z4Z_H,
               AArch64::UMAX_VG4_4Z4Z_S, AArch64::UMAX_VG4_4Z4Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, true, Op);
      return;
    case Intrinsic::aarch64_sve_fmax_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::FP>(
              Node->getValueType(0),
              {AArch64::BFMAX_VG4_4Z2Z_H, AArch64::FMAX_VG4_4Z4Z_H,
               AArch64::FMAX_VG4_4Z4Z_S, AArch64::FMAX_VG4_4Z4Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, true, Op);
      return;
    case Intrinsic::aarch64_sve_smin_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::SMIN_VG2_2Z2Z_B, AArch64::SMIN_VG2_2Z2Z_H,
               AArch64::SMIN_VG2_2Z2Z_S, AArch64::SMIN_VG2_2Z2Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, true, Op);
      return;
    case Intrinsic::aarch64_sve_umin_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::UMIN_VG2_2Z2Z_B, AArch64::UMIN_VG2_2Z2Z_H,
               AArch64::UMIN_VG2_2Z2Z_S, AArch64::UMIN_VG2_2Z2Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, true, Op);
      return;
    case Intrinsic::aarch64_sve_fmin_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::FP>(
              Node->getValueType(0),
              {AArch64::BFMIN_VG2_2Z2Z_H, AArch64::FMIN_VG2_2Z2Z_H,
               AArch64::FMIN_VG2_2Z2Z_S, AArch64::FMIN_VG2_2Z2Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, true, Op);
      return;
    case Intrinsic::aarch64_sve_smin_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::SMIN_VG4_4Z4Z_B, AArch64::SMIN_VG4_4Z4Z_H,
               AArch64::SMIN_VG4_4Z4Z_S, AArch64::SMIN_VG4_4Z4Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, true, Op);
      return;
    case Intrinsic::aarch64_sve_umin_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::UMIN_VG4_4Z4Z_B, AArch64::UMIN_VG4_4Z4Z_H,
               AArch64::UMIN_VG4_4Z4Z_S, AArch64::UMIN_VG4_4Z4Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, true, Op);
      return;
    case Intrinsic::aarch64_sve_fmin_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::FP>(
              Node->getValueType(0),
              {AArch64::BFMIN_VG4_4Z2Z_H, AArch64::FMIN_VG4_4Z4Z_H,
               AArch64::FMIN_VG4_4Z4Z_S, AArch64::FMIN_VG4_4Z4Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, true, Op);
      return;
    case Intrinsic::aarch64_sve_fmaxnm_single_x2 :
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::FP>(
              Node->getValueType(0),
              {AArch64::BFMAXNM_VG2_2ZZ_H, AArch64::FMAXNM_VG2_2ZZ_H,
               AArch64::FMAXNM_VG2_2ZZ_S, AArch64::FMAXNM_VG2_2ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, false, Op);
      return;
    case Intrinsic::aarch64_sve_fmaxnm_single_x4 :
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::FP>(
              Node->getValueType(0),
              {AArch64::BFMAXNM_VG4_4ZZ_H, AArch64::FMAXNM_VG4_4ZZ_H,
               AArch64::FMAXNM_VG4_4ZZ_S, AArch64::FMAXNM_VG4_4ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, false, Op);
      return;
    case Intrinsic::aarch64_sve_fminnm_single_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::FP>(
              Node->getValueType(0),
              {AArch64::BFMINNM_VG2_2ZZ_H, AArch64::FMINNM_VG2_2ZZ_H,
               AArch64::FMINNM_VG2_2ZZ_S, AArch64::FMINNM_VG2_2ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, false, Op);
      return;
    case Intrinsic::aarch64_sve_fminnm_single_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::FP>(
              Node->getValueType(0),
              {AArch64::BFMINNM_VG4_4ZZ_H, AArch64::FMINNM_VG4_4ZZ_H,
               AArch64::FMINNM_VG4_4ZZ_S, AArch64::FMINNM_VG4_4ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, false, Op);
      return;
    case Intrinsic::aarch64_sve_fmaxnm_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::FP>(
              Node->getValueType(0),
              {AArch64::BFMAXNM_VG2_2Z2Z_H, AArch64::FMAXNM_VG2_2Z2Z_H,
               AArch64::FMAXNM_VG2_2Z2Z_S, AArch64::FMAXNM_VG2_2Z2Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, true, Op);
      return;
    case Intrinsic::aarch64_sve_fmaxnm_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::FP>(
              Node->getValueType(0),
              {AArch64::BFMAXNM_VG4_4Z2Z_H, AArch64::FMAXNM_VG4_4Z4Z_H,
               AArch64::FMAXNM_VG4_4Z4Z_S, AArch64::FMAXNM_VG4_4Z4Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, true, Op);
      return;
    case Intrinsic::aarch64_sve_fminnm_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::FP>(
              Node->getValueType(0),
              {AArch64::BFMINNM_VG2_2Z2Z_H, AArch64::FMINNM_VG2_2Z2Z_H,
               AArch64::FMINNM_VG2_2Z2Z_S, AArch64::FMINNM_VG2_2Z2Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, true, Op);
      return;
    case Intrinsic::aarch64_sve_fminnm_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::FP>(
              Node->getValueType(0),
              {AArch64::BFMINNM_VG4_4Z2Z_H, AArch64::FMINNM_VG4_4Z4Z_H,
               AArch64::FMINNM_VG4_4Z4Z_S, AArch64::FMINNM_VG4_4Z4Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, true, Op);
      return;
    case Intrinsic::aarch64_sve_fcvtzs_x2:
      SelectCVTIntrinsic(Node, 2, AArch64::FCVTZS_2Z2Z_StoS);
      return;
    case Intrinsic::aarch64_sve_scvtf_x2:
      SelectCVTIntrinsic(Node, 2, AArch64::SCVTF_2Z2Z_StoS);
      return;
    case Intrinsic::aarch64_sve_fcvtzu_x2:
      SelectCVTIntrinsic(Node, 2, AArch64::FCVTZU_2Z2Z_StoS);
      return;
    case Intrinsic::aarch64_sve_ucvtf_x2:
      SelectCVTIntrinsic(Node, 2, AArch64::UCVTF_2Z2Z_StoS);
      return;
    case Intrinsic::aarch64_sve_fcvtzs_x4:
      SelectCVTIntrinsic(Node, 4, AArch64::FCVTZS_4Z4Z_StoS);
      return;
    case Intrinsic::aarch64_sve_scvtf_x4:
      SelectCVTIntrinsic(Node, 4, AArch64::SCVTF_4Z4Z_StoS);
      return;
    case Intrinsic::aarch64_sve_fcvtzu_x4:
      SelectCVTIntrinsic(Node, 4, AArch64::FCVTZU_4Z4Z_StoS);
      return;
    case Intrinsic::aarch64_sve_ucvtf_x4:
      SelectCVTIntrinsic(Node, 4, AArch64::UCVTF_4Z4Z_StoS);
      return;
    case Intrinsic::aarch64_sve_fcvt_widen_x2:
      SelectUnaryMultiIntrinsic(Node, 2, false, AArch64::FCVT_2ZZ_H_S);
      return;
    case Intrinsic::aarch64_sve_fcvtl_widen_x2:
      SelectUnaryMultiIntrinsic(Node, 2, false, AArch64::FCVTL_2ZZ_H_S);
      return;
    case Intrinsic::aarch64_sve_sclamp_single_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::SCLAMP_VG2_2Z2Z_B, AArch64::SCLAMP_VG2_2Z2Z_H,
               AArch64::SCLAMP_VG2_2Z2Z_S, AArch64::SCLAMP_VG2_2Z2Z_D}))
        SelectClamp(Node, 2, Op);
      return;
    case Intrinsic::aarch64_sve_uclamp_single_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::UCLAMP_VG2_2Z2Z_B, AArch64::UCLAMP_VG2_2Z2Z_H,
               AArch64::UCLAMP_VG2_2Z2Z_S, AArch64::UCLAMP_VG2_2Z2Z_D}))
        SelectClamp(Node, 2, Op);
      return;
    case Intrinsic::aarch64_sve_fclamp_single_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::FP>(
              Node->getValueType(0),
              {0, AArch64::FCLAMP_VG2_2Z2Z_H, AArch64::FCLAMP_VG2_2Z2Z_S,
               AArch64::FCLAMP_VG2_2Z2Z_D}))
        SelectClamp(Node, 2, Op);
      return;
    case Intrinsic::aarch64_sve_bfclamp_single_x2:
      SelectClamp(Node, 2, AArch64::BFCLAMP_VG2_2ZZZ_H);
      return;
    case Intrinsic::aarch64_sve_sclamp_single_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::SCLAMP_VG4_4Z4Z_B, AArch64::SCLAMP_VG4_4Z4Z_H,
               AArch64::SCLAMP_VG4_4Z4Z_S, AArch64::SCLAMP_VG4_4Z4Z_D}))
        SelectClamp(Node, 4, Op);
      return;
    case Intrinsic::aarch64_sve_uclamp_single_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::UCLAMP_VG4_4Z4Z_B, AArch64::UCLAMP_VG4_4Z4Z_H,
               AArch64::UCLAMP_VG4_4Z4Z_S, AArch64::UCLAMP_VG4_4Z4Z_D}))
        SelectClamp(Node, 4, Op);
      return;
    case Intrinsic::aarch64_sve_fclamp_single_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::FP>(
              Node->getValueType(0),
              {0, AArch64::FCLAMP_VG4_4Z4Z_H, AArch64::FCLAMP_VG4_4Z4Z_S,
               AArch64::FCLAMP_VG4_4Z4Z_D}))
        SelectClamp(Node, 4, Op);
      return;
    case Intrinsic::aarch64_sve_bfclamp_single_x4:
      SelectClamp(Node, 4, AArch64::BFCLAMP_VG4_4ZZZ_H);
      return;
    case Intrinsic::aarch64_sve_add_single_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::ADD_VG2_2ZZ_B, AArch64::ADD_VG2_2ZZ_H,
               AArch64::ADD_VG2_2ZZ_S, AArch64::ADD_VG2_2ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, false, Op);
      return;
    case Intrinsic::aarch64_sve_add_single_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {AArch64::ADD_VG4_4ZZ_B, AArch64::ADD_VG4_4ZZ_H,
               AArch64::ADD_VG4_4ZZ_S, AArch64::ADD_VG4_4ZZ_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, false, Op);
      return;
    case Intrinsic::aarch64_sve_zip_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::AnyType>(
              Node->getValueType(0),
              {AArch64::ZIP_VG2_2ZZZ_B, AArch64::ZIP_VG2_2ZZZ_H,
               AArch64::ZIP_VG2_2ZZZ_S, AArch64::ZIP_VG2_2ZZZ_D}))
        SelectUnaryMultiIntrinsic(Node, 2, /*IsTupleInput=*/false, Op);
      return;
    case Intrinsic::aarch64_sve_zipq_x2:
      SelectUnaryMultiIntrinsic(Node, 2, /*IsTupleInput=*/false,
                                AArch64::ZIP_VG2_2ZZZ_Q);
      return;
    case Intrinsic::aarch64_sve_zip_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::AnyType>(
              Node->getValueType(0),
              {AArch64::ZIP_VG4_4Z4Z_B, AArch64::ZIP_VG4_4Z4Z_H,
               AArch64::ZIP_VG4_4Z4Z_S, AArch64::ZIP_VG4_4Z4Z_D}))
        SelectUnaryMultiIntrinsic(Node, 4, /*IsTupleInput=*/true, Op);
      return;
    case Intrinsic::aarch64_sve_zipq_x4:
      SelectUnaryMultiIntrinsic(Node, 4, /*IsTupleInput=*/true,
                                AArch64::ZIP_VG4_4Z4Z_Q);
      return;
    case Intrinsic::aarch64_sve_uzp_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::AnyType>(
              Node->getValueType(0),
              {AArch64::UZP_VG2_2ZZZ_B, AArch64::UZP_VG2_2ZZZ_H,
               AArch64::UZP_VG2_2ZZZ_S, AArch64::UZP_VG2_2ZZZ_D}))
        SelectUnaryMultiIntrinsic(Node, 2, /*IsTupleInput=*/false, Op);
      return;
    case Intrinsic::aarch64_sve_uzpq_x2:
      SelectUnaryMultiIntrinsic(Node, 2, /*IsTupleInput=*/false,
                                AArch64::UZP_VG2_2ZZZ_Q);
      return;
    case Intrinsic::aarch64_sve_uzp_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::AnyType>(
              Node->getValueType(0),
              {AArch64::UZP_VG4_4Z4Z_B, AArch64::UZP_VG4_4Z4Z_H,
               AArch64::UZP_VG4_4Z4Z_S, AArch64::UZP_VG4_4Z4Z_D}))
        SelectUnaryMultiIntrinsic(Node, 4, /*IsTupleInput=*/true, Op);
      return;
    case Intrinsic::aarch64_sve_uzpq_x4:
      SelectUnaryMultiIntrinsic(Node, 4, /*IsTupleInput=*/true,
                                AArch64::UZP_VG4_4Z4Z_Q);
      return;
    case Intrinsic::aarch64_sve_sel_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::AnyType>(
              Node->getValueType(0),
              {AArch64::SEL_VG2_2ZC2Z2Z_B, AArch64::SEL_VG2_2ZC2Z2Z_H,
               AArch64::SEL_VG2_2ZC2Z2Z_S, AArch64::SEL_VG2_2ZC2Z2Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 2, true, Op, /*HasPred=*/true);
      return;
    case Intrinsic::aarch64_sve_sel_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::AnyType>(
              Node->getValueType(0),
              {AArch64::SEL_VG4_4ZC4Z4Z_B, AArch64::SEL_VG4_4ZC4Z4Z_H,
               AArch64::SEL_VG4_4ZC4Z4Z_S, AArch64::SEL_VG4_4ZC4Z4Z_D}))
        SelectDestructiveMultiIntrinsic(Node, 4, true, Op, /*HasPred=*/true);
      return;
    case Intrinsic::aarch64_sve_frinta_x2:
      SelectFrintFromVT(Node, 2, AArch64::FRINTA_2Z2Z_S);
      return;
    case Intrinsic::aarch64_sve_frinta_x4:
      SelectFrintFromVT(Node, 4, AArch64::FRINTA_4Z4Z_S);
      return;
    case Intrinsic::aarch64_sve_frintm_x2:
      SelectFrintFromVT(Node, 2, AArch64::FRINTM_2Z2Z_S);
      return;
    case Intrinsic::aarch64_sve_frintm_x4:
      SelectFrintFromVT(Node, 4, AArch64::FRINTM_4Z4Z_S);
      return;
    case Intrinsic::aarch64_sve_frintn_x2:
      SelectFrintFromVT(Node, 2, AArch64::FRINTN_2Z2Z_S);
      return;
    case Intrinsic::aarch64_sve_frintn_x4:
      SelectFrintFromVT(Node, 4, AArch64::FRINTN_4Z4Z_S);
      return;
    case Intrinsic::aarch64_sve_frintp_x2:
      SelectFrintFromVT(Node, 2, AArch64::FRINTP_2Z2Z_S);
      return;
    case Intrinsic::aarch64_sve_frintp_x4:
      SelectFrintFromVT(Node, 4, AArch64::FRINTP_4Z4Z_S);
      return;
    case Intrinsic::aarch64_sve_sunpk_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {0, AArch64::SUNPK_VG2_2ZZ_H, AArch64::SUNPK_VG2_2ZZ_S,
               AArch64::SUNPK_VG2_2ZZ_D}))
        SelectUnaryMultiIntrinsic(Node, 2, /*IsTupleInput=*/false, Op);
      return;
    case Intrinsic::aarch64_sve_uunpk_x2:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {0, AArch64::UUNPK_VG2_2ZZ_H, AArch64::UUNPK_VG2_2ZZ_S,
               AArch64::UUNPK_VG2_2ZZ_D}))
        SelectUnaryMultiIntrinsic(Node, 2, /*IsTupleInput=*/false, Op);
      return;
    case Intrinsic::aarch64_sve_sunpk_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {0, AArch64::SUNPK_VG4_4Z2Z_H, AArch64::SUNPK_VG4_4Z2Z_S,
               AArch64::SUNPK_VG4_4Z2Z_D}))
        SelectUnaryMultiIntrinsic(Node, 4, /*IsTupleInput=*/true, Op);
      return;
    case Intrinsic::aarch64_sve_uunpk_x4:
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::Int>(
              Node->getValueType(0),
              {0, AArch64::UUNPK_VG4_4Z2Z_H, AArch64::UUNPK_VG4_4Z2Z_S,
               AArch64::UUNPK_VG4_4Z2Z_D}))
        SelectUnaryMultiIntrinsic(Node, 4, /*IsTupleInput=*/true, Op);
      return;
    case Intrinsic::aarch64_sve_pext_x2: {
      if (auto Op = SelectOpcodeFromVT<SelectTypeKind::AnyType>(
              Node->getValueType(0),
              {AArch64::PEXT_2PCI_B, AArch64::PEXT_2PCI_H, AArch64::PEXT_2PCI_S,
               AArch64::PEXT_2PCI_D}))
        SelectPExtPair(Node, Op);
      return;
    }
    }
    break;
  }
  case ISD::INTRINSIC_VOID: {
    unsigned IntNo = Node->getConstantOperandVal(1);
    if (Node->getNumOperands() >= 3)
      VT = Node->getOperand(2)->getValueType(0);
    switch (IntNo) {
    default:
      break;
    case Intrinsic::aarch64_neon_st1x2: {
      if (VT == MVT::v8i8) {
        SelectStore(Node, 2, AArch64::ST1Twov8b);
        return;
      } else if (VT == MVT::v16i8) {
        SelectStore(Node, 2, AArch64::ST1Twov16b);
        return;
      } else if (VT == MVT::v4i16 || VT == MVT::v4f16 ||
                 VT == MVT::v4bf16) {
        SelectStore(Node, 2, AArch64::ST1Twov4h);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v8f16 ||
                 VT == MVT::v8bf16) {
        SelectStore(Node, 2, AArch64::ST1Twov8h);
        return;
      } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
        SelectStore(Node, 2, AArch64::ST1Twov2s);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
        SelectStore(Node, 2, AArch64::ST1Twov4s);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
        SelectStore(Node, 2, AArch64::ST1Twov2d);
        return;
      } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
        SelectStore(Node, 2, AArch64::ST1Twov1d);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_neon_st1x3: {
      if (VT == MVT::v8i8) {
        SelectStore(Node, 3, AArch64::ST1Threev8b);
        return;
      } else if (VT == MVT::v16i8) {
        SelectStore(Node, 3, AArch64::ST1Threev16b);
        return;
      } else if (VT == MVT::v4i16 || VT == MVT::v4f16 ||
                 VT == MVT::v4bf16) {
        SelectStore(Node, 3, AArch64::ST1Threev4h);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v8f16 ||
                 VT == MVT::v8bf16) {
        SelectStore(Node, 3, AArch64::ST1Threev8h);
        return;
      } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
        SelectStore(Node, 3, AArch64::ST1Threev2s);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
        SelectStore(Node, 3, AArch64::ST1Threev4s);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
        SelectStore(Node, 3, AArch64::ST1Threev2d);
        return;
      } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
        SelectStore(Node, 3, AArch64::ST1Threev1d);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_neon_st1x4: {
      if (VT == MVT::v8i8) {
        SelectStore(Node, 4, AArch64::ST1Fourv8b);
        return;
      } else if (VT == MVT::v16i8) {
        SelectStore(Node, 4, AArch64::ST1Fourv16b);
        return;
      } else if (VT == MVT::v4i16 || VT == MVT::v4f16 ||
                 VT == MVT::v4bf16) {
        SelectStore(Node, 4, AArch64::ST1Fourv4h);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v8f16 ||
                 VT == MVT::v8bf16) {
        SelectStore(Node, 4, AArch64::ST1Fourv8h);
        return;
      } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
        SelectStore(Node, 4, AArch64::ST1Fourv2s);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
        SelectStore(Node, 4, AArch64::ST1Fourv4s);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
        SelectStore(Node, 4, AArch64::ST1Fourv2d);
        return;
      } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
        SelectStore(Node, 4, AArch64::ST1Fourv1d);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_neon_st2: {
      if (VT == MVT::v8i8) {
        SelectStore(Node, 2, AArch64::ST2Twov8b);
        return;
      } else if (VT == MVT::v16i8) {
        SelectStore(Node, 2, AArch64::ST2Twov16b);
        return;
      } else if (VT == MVT::v4i16 || VT == MVT::v4f16 ||
                 VT == MVT::v4bf16) {
        SelectStore(Node, 2, AArch64::ST2Twov4h);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v8f16 ||
                 VT == MVT::v8bf16) {
        SelectStore(Node, 2, AArch64::ST2Twov8h);
        return;
      } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
        SelectStore(Node, 2, AArch64::ST2Twov2s);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
        SelectStore(Node, 2, AArch64::ST2Twov4s);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
        SelectStore(Node, 2, AArch64::ST2Twov2d);
        return;
      } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
        SelectStore(Node, 2, AArch64::ST1Twov1d);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_neon_st3: {
      if (VT == MVT::v8i8) {
        SelectStore(Node, 3, AArch64::ST3Threev8b);
        return;
      } else if (VT == MVT::v16i8) {
        SelectStore(Node, 3, AArch64::ST3Threev16b);
        return;
      } else if (VT == MVT::v4i16 || VT == MVT::v4f16 ||
                 VT == MVT::v4bf16) {
        SelectStore(Node, 3, AArch64::ST3Threev4h);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v8f16 ||
                 VT == MVT::v8bf16) {
        SelectStore(Node, 3, AArch64::ST3Threev8h);
        return;
      } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
        SelectStore(Node, 3, AArch64::ST3Threev2s);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
        SelectStore(Node, 3, AArch64::ST3Threev4s);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
        SelectStore(Node, 3, AArch64::ST3Threev2d);
        return;
      } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
        SelectStore(Node, 3, AArch64::ST1Threev1d);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_neon_st4: {
      if (VT == MVT::v8i8) {
        SelectStore(Node, 4, AArch64::ST4Fourv8b);
        return;
      } else if (VT == MVT::v16i8) {
        SelectStore(Node, 4, AArch64::ST4Fourv16b);
        return;
      } else if (VT == MVT::v4i16 || VT == MVT::v4f16 ||
                 VT == MVT::v4bf16) {
        SelectStore(Node, 4, AArch64::ST4Fourv4h);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v8f16 ||
                 VT == MVT::v8bf16) {
        SelectStore(Node, 4, AArch64::ST4Fourv8h);
        return;
      } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
        SelectStore(Node, 4, AArch64::ST4Fourv2s);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
        SelectStore(Node, 4, AArch64::ST4Fourv4s);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
        SelectStore(Node, 4, AArch64::ST4Fourv2d);
        return;
      } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
        SelectStore(Node, 4, AArch64::ST1Fourv1d);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_neon_st2lane: {
      if (VT == MVT::v16i8 || VT == MVT::v8i8) {
        SelectStoreLane(Node, 2, AArch64::ST2i8);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v4i16 || VT == MVT::v4f16 ||
                 VT == MVT::v8f16 || VT == MVT::v4bf16 || VT == MVT::v8bf16) {
        SelectStoreLane(Node, 2, AArch64::ST2i16);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v2i32 || VT == MVT::v4f32 ||
                 VT == MVT::v2f32) {
        SelectStoreLane(Node, 2, AArch64::ST2i32);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v1i64 || VT == MVT::v2f64 ||
                 VT == MVT::v1f64) {
        SelectStoreLane(Node, 2, AArch64::ST2i64);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_neon_st3lane: {
      if (VT == MVT::v16i8 || VT == MVT::v8i8) {
        SelectStoreLane(Node, 3, AArch64::ST3i8);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v4i16 || VT == MVT::v4f16 ||
                 VT == MVT::v8f16 || VT == MVT::v4bf16 || VT == MVT::v8bf16) {
        SelectStoreLane(Node, 3, AArch64::ST3i16);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v2i32 || VT == MVT::v4f32 ||
                 VT == MVT::v2f32) {
        SelectStoreLane(Node, 3, AArch64::ST3i32);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v1i64 || VT == MVT::v2f64 ||
                 VT == MVT::v1f64) {
        SelectStoreLane(Node, 3, AArch64::ST3i64);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_neon_st4lane: {
      if (VT == MVT::v16i8 || VT == MVT::v8i8) {
        SelectStoreLane(Node, 4, AArch64::ST4i8);
        return;
      } else if (VT == MVT::v8i16 || VT == MVT::v4i16 || VT == MVT::v4f16 ||
                 VT == MVT::v8f16 || VT == MVT::v4bf16 || VT == MVT::v8bf16) {
        SelectStoreLane(Node, 4, AArch64::ST4i16);
        return;
      } else if (VT == MVT::v4i32 || VT == MVT::v2i32 || VT == MVT::v4f32 ||
                 VT == MVT::v2f32) {
        SelectStoreLane(Node, 4, AArch64::ST4i32);
        return;
      } else if (VT == MVT::v2i64 || VT == MVT::v1i64 || VT == MVT::v2f64 ||
                 VT == MVT::v1f64) {
        SelectStoreLane(Node, 4, AArch64::ST4i64);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_sve_st2q: {
      SelectPredicatedStore(Node, 2, 4, AArch64::ST2Q, AArch64::ST2Q_IMM);
      return;
    }
    case Intrinsic::aarch64_sve_st3q: {
      SelectPredicatedStore(Node, 3, 4, AArch64::ST3Q, AArch64::ST3Q_IMM);
      return;
    }
    case Intrinsic::aarch64_sve_st4q: {
      SelectPredicatedStore(Node, 4, 4, AArch64::ST4Q, AArch64::ST4Q_IMM);
      return;
    }
    case Intrinsic::aarch64_sve_st2: {
      if (VT == MVT::nxv16i8) {
        SelectPredicatedStore(Node, 2, 0, AArch64::ST2B, AArch64::ST2B_IMM);
        return;
      } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
                 VT == MVT::nxv8bf16) {
        SelectPredicatedStore(Node, 2, 1, AArch64::ST2H, AArch64::ST2H_IMM);
        return;
      } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
        SelectPredicatedStore(Node, 2, 2, AArch64::ST2W, AArch64::ST2W_IMM);
        return;
      } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
        SelectPredicatedStore(Node, 2, 3, AArch64::ST2D, AArch64::ST2D_IMM);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_sve_st3: {
      if (VT == MVT::nxv16i8) {
        SelectPredicatedStore(Node, 3, 0, AArch64::ST3B, AArch64::ST3B_IMM);
        return;
      } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
                 VT == MVT::nxv8bf16) {
        SelectPredicatedStore(Node, 3, 1, AArch64::ST3H, AArch64::ST3H_IMM);
        return;
      } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
        SelectPredicatedStore(Node, 3, 2, AArch64::ST3W, AArch64::ST3W_IMM);
        return;
      } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
        SelectPredicatedStore(Node, 3, 3, AArch64::ST3D, AArch64::ST3D_IMM);
        return;
      }
      break;
    }
    case Intrinsic::aarch64_sve_st4: {
      if (VT == MVT::nxv16i8) {
        SelectPredicatedStore(Node, 4, 0, AArch64::ST4B, AArch64::ST4B_IMM);
        return;
      } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
                 VT == MVT::nxv8bf16) {
        SelectPredicatedStore(Node, 4, 1, AArch64::ST4H, AArch64::ST4H_IMM);
        return;
      } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
        SelectPredicatedStore(Node, 4, 2, AArch64::ST4W, AArch64::ST4W_IMM);
        return;
      } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
        SelectPredicatedStore(Node, 4, 3, AArch64::ST4D, AArch64::ST4D_IMM);
        return;
      }
      break;
    }
    }
    break;
  }
  case AArch64ISD::LD2post: {
    if (VT == MVT::v8i8) {
      SelectPostLoad(Node, 2, AArch64::LD2Twov8b_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v16i8) {
      SelectPostLoad(Node, 2, AArch64::LD2Twov16b_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
      SelectPostLoad(Node, 2, AArch64::LD2Twov4h_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v8f16  || VT == MVT::v8bf16) {
      SelectPostLoad(Node, 2, AArch64::LD2Twov8h_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
      SelectPostLoad(Node, 2, AArch64::LD2Twov2s_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
      SelectPostLoad(Node, 2, AArch64::LD2Twov4s_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
      SelectPostLoad(Node, 2, AArch64::LD1Twov1d_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
      SelectPostLoad(Node, 2, AArch64::LD2Twov2d_POST, AArch64::qsub0);
      return;
    }
    break;
  }
  case AArch64ISD::LD3post: {
    if (VT == MVT::v8i8) {
      SelectPostLoad(Node, 3, AArch64::LD3Threev8b_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v16i8) {
      SelectPostLoad(Node, 3, AArch64::LD3Threev16b_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
      SelectPostLoad(Node, 3, AArch64::LD3Threev4h_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v8f16  || VT == MVT::v8bf16) {
      SelectPostLoad(Node, 3, AArch64::LD3Threev8h_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
      SelectPostLoad(Node, 3, AArch64::LD3Threev2s_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
      SelectPostLoad(Node, 3, AArch64::LD3Threev4s_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
      SelectPostLoad(Node, 3, AArch64::LD1Threev1d_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
      SelectPostLoad(Node, 3, AArch64::LD3Threev2d_POST, AArch64::qsub0);
      return;
    }
    break;
  }
  case AArch64ISD::LD4post: {
    if (VT == MVT::v8i8) {
      SelectPostLoad(Node, 4, AArch64::LD4Fourv8b_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v16i8) {
      SelectPostLoad(Node, 4, AArch64::LD4Fourv16b_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
      SelectPostLoad(Node, 4, AArch64::LD4Fourv4h_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v8f16  || VT == MVT::v8bf16) {
      SelectPostLoad(Node, 4, AArch64::LD4Fourv8h_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
      SelectPostLoad(Node, 4, AArch64::LD4Fourv2s_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
      SelectPostLoad(Node, 4, AArch64::LD4Fourv4s_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
      SelectPostLoad(Node, 4, AArch64::LD1Fourv1d_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
      SelectPostLoad(Node, 4, AArch64::LD4Fourv2d_POST, AArch64::qsub0);
      return;
    }
    break;
  }
  case AArch64ISD::LD1x2post: {
    if (VT == MVT::v8i8) {
      SelectPostLoad(Node, 2, AArch64::LD1Twov8b_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v16i8) {
      SelectPostLoad(Node, 2, AArch64::LD1Twov16b_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
      SelectPostLoad(Node, 2, AArch64::LD1Twov4h_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v8f16  || VT == MVT::v8bf16) {
      SelectPostLoad(Node, 2, AArch64::LD1Twov8h_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
      SelectPostLoad(Node, 2, AArch64::LD1Twov2s_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
      SelectPostLoad(Node, 2, AArch64::LD1Twov4s_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
      SelectPostLoad(Node, 2, AArch64::LD1Twov1d_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
      SelectPostLoad(Node, 2, AArch64::LD1Twov2d_POST, AArch64::qsub0);
      return;
    }
    break;
  }
  case AArch64ISD::LD1x3post: {
    if (VT == MVT::v8i8) {
      SelectPostLoad(Node, 3, AArch64::LD1Threev8b_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v16i8) {
      SelectPostLoad(Node, 3, AArch64::LD1Threev16b_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
      SelectPostLoad(Node, 3, AArch64::LD1Threev4h_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v8f16  || VT == MVT::v8bf16) {
      SelectPostLoad(Node, 3, AArch64::LD1Threev8h_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
      SelectPostLoad(Node, 3, AArch64::LD1Threev2s_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
      SelectPostLoad(Node, 3, AArch64::LD1Threev4s_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
      SelectPostLoad(Node, 3, AArch64::LD1Threev1d_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
      SelectPostLoad(Node, 3, AArch64::LD1Threev2d_POST, AArch64::qsub0);
      return;
    }
    break;
  }
  case AArch64ISD::LD1x4post: {
    if (VT == MVT::v8i8) {
      SelectPostLoad(Node, 4, AArch64::LD1Fourv8b_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v16i8) {
      SelectPostLoad(Node, 4, AArch64::LD1Fourv16b_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
      SelectPostLoad(Node, 4, AArch64::LD1Fourv4h_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v8f16  || VT == MVT::v8bf16) {
      SelectPostLoad(Node, 4, AArch64::LD1Fourv8h_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
      SelectPostLoad(Node, 4, AArch64::LD1Fourv2s_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
      SelectPostLoad(Node, 4, AArch64::LD1Fourv4s_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
      SelectPostLoad(Node, 4, AArch64::LD1Fourv1d_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
      SelectPostLoad(Node, 4, AArch64::LD1Fourv2d_POST, AArch64::qsub0);
      return;
    }
    break;
  }
  case AArch64ISD::LD1DUPpost: {
    if (VT == MVT::v8i8) {
      SelectPostLoad(Node, 1, AArch64::LD1Rv8b_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v16i8) {
      SelectPostLoad(Node, 1, AArch64::LD1Rv16b_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
      SelectPostLoad(Node, 1, AArch64::LD1Rv4h_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v8f16  || VT == MVT::v8bf16) {
      SelectPostLoad(Node, 1, AArch64::LD1Rv8h_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
      SelectPostLoad(Node, 1, AArch64::LD1Rv2s_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
      SelectPostLoad(Node, 1, AArch64::LD1Rv4s_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
      SelectPostLoad(Node, 1, AArch64::LD1Rv1d_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
      SelectPostLoad(Node, 1, AArch64::LD1Rv2d_POST, AArch64::qsub0);
      return;
    }
    break;
  }
  case AArch64ISD::LD2DUPpost: {
    if (VT == MVT::v8i8) {
      SelectPostLoad(Node, 2, AArch64::LD2Rv8b_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v16i8) {
      SelectPostLoad(Node, 2, AArch64::LD2Rv16b_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
      SelectPostLoad(Node, 2, AArch64::LD2Rv4h_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v8f16  || VT == MVT::v8bf16) {
      SelectPostLoad(Node, 2, AArch64::LD2Rv8h_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
      SelectPostLoad(Node, 2, AArch64::LD2Rv2s_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
      SelectPostLoad(Node, 2, AArch64::LD2Rv4s_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
      SelectPostLoad(Node, 2, AArch64::LD2Rv1d_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
      SelectPostLoad(Node, 2, AArch64::LD2Rv2d_POST, AArch64::qsub0);
      return;
    }
    break;
  }
  case AArch64ISD::LD3DUPpost: {
    if (VT == MVT::v8i8) {
      SelectPostLoad(Node, 3, AArch64::LD3Rv8b_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v16i8) {
      SelectPostLoad(Node, 3, AArch64::LD3Rv16b_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
      SelectPostLoad(Node, 3, AArch64::LD3Rv4h_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v8f16  || VT == MVT::v8bf16) {
      SelectPostLoad(Node, 3, AArch64::LD3Rv8h_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
      SelectPostLoad(Node, 3, AArch64::LD3Rv2s_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
      SelectPostLoad(Node, 3, AArch64::LD3Rv4s_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
      SelectPostLoad(Node, 3, AArch64::LD3Rv1d_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
      SelectPostLoad(Node, 3, AArch64::LD3Rv2d_POST, AArch64::qsub0);
      return;
    }
    break;
  }
  case AArch64ISD::LD4DUPpost: {
    if (VT == MVT::v8i8) {
      SelectPostLoad(Node, 4, AArch64::LD4Rv8b_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v16i8) {
      SelectPostLoad(Node, 4, AArch64::LD4Rv16b_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
      SelectPostLoad(Node, 4, AArch64::LD4Rv4h_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v8f16  || VT == MVT::v8bf16) {
      SelectPostLoad(Node, 4, AArch64::LD4Rv8h_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
      SelectPostLoad(Node, 4, AArch64::LD4Rv2s_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
      SelectPostLoad(Node, 4, AArch64::LD4Rv4s_POST, AArch64::qsub0);
      return;
    } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
      SelectPostLoad(Node, 4, AArch64::LD4Rv1d_POST, AArch64::dsub0);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
      SelectPostLoad(Node, 4, AArch64::LD4Rv2d_POST, AArch64::qsub0);
      return;
    }
    break;
  }
  case AArch64ISD::LD1LANEpost: {
    if (VT == MVT::v16i8 || VT == MVT::v8i8) {
      SelectPostLoadLane(Node, 1, AArch64::LD1i8_POST);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v4i16 || VT == MVT::v4f16 ||
               VT == MVT::v8f16 || VT == MVT::v4bf16 || VT == MVT::v8bf16) {
      SelectPostLoadLane(Node, 1, AArch64::LD1i16_POST);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v2i32 || VT == MVT::v4f32 ||
               VT == MVT::v2f32) {
      SelectPostLoadLane(Node, 1, AArch64::LD1i32_POST);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v1i64 || VT == MVT::v2f64 ||
               VT == MVT::v1f64) {
      SelectPostLoadLane(Node, 1, AArch64::LD1i64_POST);
      return;
    }
    break;
  }
  case AArch64ISD::LD2LANEpost: {
    if (VT == MVT::v16i8 || VT == MVT::v8i8) {
      SelectPostLoadLane(Node, 2, AArch64::LD2i8_POST);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v4i16 || VT == MVT::v4f16 ||
               VT == MVT::v8f16 || VT == MVT::v4bf16 || VT == MVT::v8bf16) {
      SelectPostLoadLane(Node, 2, AArch64::LD2i16_POST);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v2i32 || VT == MVT::v4f32 ||
               VT == MVT::v2f32) {
      SelectPostLoadLane(Node, 2, AArch64::LD2i32_POST);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v1i64 || VT == MVT::v2f64 ||
               VT == MVT::v1f64) {
      SelectPostLoadLane(Node, 2, AArch64::LD2i64_POST);
      return;
    }
    break;
  }
  case AArch64ISD::LD3LANEpost: {
    if (VT == MVT::v16i8 || VT == MVT::v8i8) {
      SelectPostLoadLane(Node, 3, AArch64::LD3i8_POST);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v4i16 || VT == MVT::v4f16 ||
               VT == MVT::v8f16 || VT == MVT::v4bf16 || VT == MVT::v8bf16) {
      SelectPostLoadLane(Node, 3, AArch64::LD3i16_POST);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v2i32 || VT == MVT::v4f32 ||
               VT == MVT::v2f32) {
      SelectPostLoadLane(Node, 3, AArch64::LD3i32_POST);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v1i64 || VT == MVT::v2f64 ||
               VT == MVT::v1f64) {
      SelectPostLoadLane(Node, 3, AArch64::LD3i64_POST);
      return;
    }
    break;
  }
  case AArch64ISD::LD4LANEpost: {
    if (VT == MVT::v16i8 || VT == MVT::v8i8) {
      SelectPostLoadLane(Node, 4, AArch64::LD4i8_POST);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v4i16 || VT == MVT::v4f16 ||
               VT == MVT::v8f16 || VT == MVT::v4bf16 || VT == MVT::v8bf16) {
      SelectPostLoadLane(Node, 4, AArch64::LD4i16_POST);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v2i32 || VT == MVT::v4f32 ||
               VT == MVT::v2f32) {
      SelectPostLoadLane(Node, 4, AArch64::LD4i32_POST);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v1i64 || VT == MVT::v2f64 ||
               VT == MVT::v1f64) {
      SelectPostLoadLane(Node, 4, AArch64::LD4i64_POST);
      return;
    }
    break;
  }
  case AArch64ISD::ST2post: {
    VT = Node->getOperand(1).getValueType();
    if (VT == MVT::v8i8) {
      SelectPostStore(Node, 2, AArch64::ST2Twov8b_POST);
      return;
    } else if (VT == MVT::v16i8) {
      SelectPostStore(Node, 2, AArch64::ST2Twov16b_POST);
      return;
    } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
      SelectPostStore(Node, 2, AArch64::ST2Twov4h_POST);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v8bf16) {
      SelectPostStore(Node, 2, AArch64::ST2Twov8h_POST);
      return;
    } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
      SelectPostStore(Node, 2, AArch64::ST2Twov2s_POST);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
      SelectPostStore(Node, 2, AArch64::ST2Twov4s_POST);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
      SelectPostStore(Node, 2, AArch64::ST2Twov2d_POST);
      return;
    } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
      SelectPostStore(Node, 2, AArch64::ST1Twov1d_POST);
      return;
    }
    break;
  }
  case AArch64ISD::ST3post: {
    VT = Node->getOperand(1).getValueType();
    if (VT == MVT::v8i8) {
      SelectPostStore(Node, 3, AArch64::ST3Threev8b_POST);
      return;
    } else if (VT == MVT::v16i8) {
      SelectPostStore(Node, 3, AArch64::ST3Threev16b_POST);
      return;
    } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
      SelectPostStore(Node, 3, AArch64::ST3Threev4h_POST);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v8bf16) {
      SelectPostStore(Node, 3, AArch64::ST3Threev8h_POST);
      return;
    } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
      SelectPostStore(Node, 3, AArch64::ST3Threev2s_POST);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
      SelectPostStore(Node, 3, AArch64::ST3Threev4s_POST);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
      SelectPostStore(Node, 3, AArch64::ST3Threev2d_POST);
      return;
    } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
      SelectPostStore(Node, 3, AArch64::ST1Threev1d_POST);
      return;
    }
    break;
  }
  case AArch64ISD::ST4post: {
    VT = Node->getOperand(1).getValueType();
    if (VT == MVT::v8i8) {
      SelectPostStore(Node, 4, AArch64::ST4Fourv8b_POST);
      return;
    } else if (VT == MVT::v16i8) {
      SelectPostStore(Node, 4, AArch64::ST4Fourv16b_POST);
      return;
    } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
      SelectPostStore(Node, 4, AArch64::ST4Fourv4h_POST);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v8bf16) {
      SelectPostStore(Node, 4, AArch64::ST4Fourv8h_POST);
      return;
    } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
      SelectPostStore(Node, 4, AArch64::ST4Fourv2s_POST);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
      SelectPostStore(Node, 4, AArch64::ST4Fourv4s_POST);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
      SelectPostStore(Node, 4, AArch64::ST4Fourv2d_POST);
      return;
    } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
      SelectPostStore(Node, 4, AArch64::ST1Fourv1d_POST);
      return;
    }
    break;
  }
  case AArch64ISD::ST1x2post: {
    VT = Node->getOperand(1).getValueType();
    if (VT == MVT::v8i8) {
      SelectPostStore(Node, 2, AArch64::ST1Twov8b_POST);
      return;
    } else if (VT == MVT::v16i8) {
      SelectPostStore(Node, 2, AArch64::ST1Twov16b_POST);
      return;
    } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
      SelectPostStore(Node, 2, AArch64::ST1Twov4h_POST);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v8bf16) {
      SelectPostStore(Node, 2, AArch64::ST1Twov8h_POST);
      return;
    } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
      SelectPostStore(Node, 2, AArch64::ST1Twov2s_POST);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
      SelectPostStore(Node, 2, AArch64::ST1Twov4s_POST);
      return;
    } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
      SelectPostStore(Node, 2, AArch64::ST1Twov1d_POST);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
      SelectPostStore(Node, 2, AArch64::ST1Twov2d_POST);
      return;
    }
    break;
  }
  case AArch64ISD::ST1x3post: {
    VT = Node->getOperand(1).getValueType();
    if (VT == MVT::v8i8) {
      SelectPostStore(Node, 3, AArch64::ST1Threev8b_POST);
      return;
    } else if (VT == MVT::v16i8) {
      SelectPostStore(Node, 3, AArch64::ST1Threev16b_POST);
      return;
    } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
      SelectPostStore(Node, 3, AArch64::ST1Threev4h_POST);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v8bf16 ) {
      SelectPostStore(Node, 3, AArch64::ST1Threev8h_POST);
      return;
    } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
      SelectPostStore(Node, 3, AArch64::ST1Threev2s_POST);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
      SelectPostStore(Node, 3, AArch64::ST1Threev4s_POST);
      return;
    } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
      SelectPostStore(Node, 3, AArch64::ST1Threev1d_POST);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
      SelectPostStore(Node, 3, AArch64::ST1Threev2d_POST);
      return;
    }
    break;
  }
  case AArch64ISD::ST1x4post: {
    VT = Node->getOperand(1).getValueType();
    if (VT == MVT::v8i8) {
      SelectPostStore(Node, 4, AArch64::ST1Fourv8b_POST);
      return;
    } else if (VT == MVT::v16i8) {
      SelectPostStore(Node, 4, AArch64::ST1Fourv16b_POST);
      return;
    } else if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4bf16) {
      SelectPostStore(Node, 4, AArch64::ST1Fourv4h_POST);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v8bf16) {
      SelectPostStore(Node, 4, AArch64::ST1Fourv8h_POST);
      return;
    } else if (VT == MVT::v2i32 || VT == MVT::v2f32) {
      SelectPostStore(Node, 4, AArch64::ST1Fourv2s_POST);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v4f32) {
      SelectPostStore(Node, 4, AArch64::ST1Fourv4s_POST);
      return;
    } else if (VT == MVT::v1i64 || VT == MVT::v1f64) {
      SelectPostStore(Node, 4, AArch64::ST1Fourv1d_POST);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v2f64) {
      SelectPostStore(Node, 4, AArch64::ST1Fourv2d_POST);
      return;
    }
    break;
  }
  case AArch64ISD::ST2LANEpost: {
    VT = Node->getOperand(1).getValueType();
    if (VT == MVT::v16i8 || VT == MVT::v8i8) {
      SelectPostStoreLane(Node, 2, AArch64::ST2i8_POST);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v4i16 || VT == MVT::v4f16 ||
               VT == MVT::v8f16 || VT == MVT::v4bf16 || VT == MVT::v8bf16) {
      SelectPostStoreLane(Node, 2, AArch64::ST2i16_POST);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v2i32 || VT == MVT::v4f32 ||
               VT == MVT::v2f32) {
      SelectPostStoreLane(Node, 2, AArch64::ST2i32_POST);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v1i64 || VT == MVT::v2f64 ||
               VT == MVT::v1f64) {
      SelectPostStoreLane(Node, 2, AArch64::ST2i64_POST);
      return;
    }
    break;
  }
  case AArch64ISD::ST3LANEpost: {
    VT = Node->getOperand(1).getValueType();
    if (VT == MVT::v16i8 || VT == MVT::v8i8) {
      SelectPostStoreLane(Node, 3, AArch64::ST3i8_POST);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v4i16 || VT == MVT::v4f16 ||
               VT == MVT::v8f16 || VT == MVT::v4bf16 || VT == MVT::v8bf16) {
      SelectPostStoreLane(Node, 3, AArch64::ST3i16_POST);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v2i32 || VT == MVT::v4f32 ||
               VT == MVT::v2f32) {
      SelectPostStoreLane(Node, 3, AArch64::ST3i32_POST);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v1i64 || VT == MVT::v2f64 ||
               VT == MVT::v1f64) {
      SelectPostStoreLane(Node, 3, AArch64::ST3i64_POST);
      return;
    }
    break;
  }
  case AArch64ISD::ST4LANEpost: {
    VT = Node->getOperand(1).getValueType();
    if (VT == MVT::v16i8 || VT == MVT::v8i8) {
      SelectPostStoreLane(Node, 4, AArch64::ST4i8_POST);
      return;
    } else if (VT == MVT::v8i16 || VT == MVT::v4i16 || VT == MVT::v4f16 ||
               VT == MVT::v8f16 || VT == MVT::v4bf16 || VT == MVT::v8bf16) {
      SelectPostStoreLane(Node, 4, AArch64::ST4i16_POST);
      return;
    } else if (VT == MVT::v4i32 || VT == MVT::v2i32 || VT == MVT::v4f32 ||
               VT == MVT::v2f32) {
      SelectPostStoreLane(Node, 4, AArch64::ST4i32_POST);
      return;
    } else if (VT == MVT::v2i64 || VT == MVT::v1i64 || VT == MVT::v2f64 ||
               VT == MVT::v1f64) {
      SelectPostStoreLane(Node, 4, AArch64::ST4i64_POST);
      return;
    }
    break;
  }
  case AArch64ISD::SVE_LD2_MERGE_ZERO: {
    if (VT == MVT::nxv16i8) {
      SelectPredicatedLoad(Node, 2, 0, AArch64::LD2B_IMM, AArch64::LD2B);
      return;
    } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
               VT == MVT::nxv8bf16) {
      SelectPredicatedLoad(Node, 2, 1, AArch64::LD2H_IMM, AArch64::LD2H);
      return;
    } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
      SelectPredicatedLoad(Node, 2, 2, AArch64::LD2W_IMM, AArch64::LD2W);
      return;
    } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
      SelectPredicatedLoad(Node, 2, 3, AArch64::LD2D_IMM, AArch64::LD2D);
      return;
    }
    break;
  }
  case AArch64ISD::SVE_LD3_MERGE_ZERO: {
    if (VT == MVT::nxv16i8) {
      SelectPredicatedLoad(Node, 3, 0, AArch64::LD3B_IMM, AArch64::LD3B);
      return;
    } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
               VT == MVT::nxv8bf16) {
      SelectPredicatedLoad(Node, 3, 1, AArch64::LD3H_IMM, AArch64::LD3H);
      return;
    } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
      SelectPredicatedLoad(Node, 3, 2, AArch64::LD3W_IMM, AArch64::LD3W);
      return;
    } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
      SelectPredicatedLoad(Node, 3, 3, AArch64::LD3D_IMM, AArch64::LD3D);
      return;
    }
    break;
  }
  case AArch64ISD::SVE_LD4_MERGE_ZERO: {
    if (VT == MVT::nxv16i8) {
      SelectPredicatedLoad(Node, 4, 0, AArch64::LD4B_IMM, AArch64::LD4B);
      return;
    } else if (VT == MVT::nxv8i16 || VT == MVT::nxv8f16 ||
               VT == MVT::nxv8bf16) {
      SelectPredicatedLoad(Node, 4, 1, AArch64::LD4H_IMM, AArch64::LD4H);
      return;
    } else if (VT == MVT::nxv4i32 || VT == MVT::nxv4f32) {
      SelectPredicatedLoad(Node, 4, 2, AArch64::LD4W_IMM, AArch64::LD4W);
      return;
    } else if (VT == MVT::nxv2i64 || VT == MVT::nxv2f64) {
      SelectPredicatedLoad(Node, 4, 3, AArch64::LD4D_IMM, AArch64::LD4D);
      return;
    }
    break;
  }
  }

  // Select the default instruction
  SelectCode(Node);
}

/// createAArch64ISelDag - This pass converts a legalized DAG into a
/// AArch64-specific DAG, ready for instruction scheduling.
FunctionPass *llvm::createAArch64ISelDag(AArch64TargetMachine &TM,
                                         CodeGenOptLevel OptLevel) {
  return new AArch64DAGToDAGISelLegacy(TM, OptLevel);
}

/// When \p PredVT is a scalable vector predicate in the form
/// MVT::nx<M>xi1, it builds the correspondent scalable vector of
/// integers MVT::nx<M>xi<bits> s.t. M x bits = 128. When targeting
/// structured vectors (NumVec >1), the output data type is
/// MVT::nx<M*NumVec>xi<bits> s.t. M x bits = 128. If the input
/// PredVT is not in the form MVT::nx<M>xi1, it returns an invalid
/// EVT.
static EVT getPackedVectorTypeFromPredicateType(LLVMContext &Ctx, EVT PredVT,
                                                unsigned NumVec) {
  assert(NumVec > 0 && NumVec < 5 && "Invalid number of vectors.");
  if (!PredVT.isScalableVector() || PredVT.getVectorElementType() != MVT::i1)
    return EVT();

  if (PredVT != MVT::nxv16i1 && PredVT != MVT::nxv8i1 &&
      PredVT != MVT::nxv4i1 && PredVT != MVT::nxv2i1)
    return EVT();

  ElementCount EC = PredVT.getVectorElementCount();
  EVT ScalarVT =
      EVT::getIntegerVT(Ctx, AArch64::SVEBitsPerBlock / EC.getKnownMinValue());
  EVT MemVT = EVT::getVectorVT(Ctx, ScalarVT, EC * NumVec);

  return MemVT;
}

/// Return the EVT of the data associated to a memory operation in \p
/// Root. If such EVT cannot be retrived, it returns an invalid EVT.
static EVT getMemVTFromNode(LLVMContext &Ctx, SDNode *Root) {
  if (isa<MemSDNode>(Root))
    return cast<MemSDNode>(Root)->getMemoryVT();

  if (isa<MemIntrinsicSDNode>(Root))
    return cast<MemIntrinsicSDNode>(Root)->getMemoryVT();

  const unsigned Opcode = Root->getOpcode();
  // For custom ISD nodes, we have to look at them individually to extract the
  // type of the data moved to/from memory.
  switch (Opcode) {
  case AArch64ISD::LD1_MERGE_ZERO:
  case AArch64ISD::LD1S_MERGE_ZERO:
  case AArch64ISD::LDNF1_MERGE_ZERO:
  case AArch64ISD::LDNF1S_MERGE_ZERO:
    return cast<VTSDNode>(Root->getOperand(3))->getVT();
  case AArch64ISD::ST1_PRED:
    return cast<VTSDNode>(Root->getOperand(4))->getVT();
  case AArch64ISD::SVE_LD2_MERGE_ZERO:
    return getPackedVectorTypeFromPredicateType(
        Ctx, Root->getOperand(1)->getValueType(0), /*NumVec=*/2);
  case AArch64ISD::SVE_LD3_MERGE_ZERO:
    return getPackedVectorTypeFromPredicateType(
        Ctx, Root->getOperand(1)->getValueType(0), /*NumVec=*/3);
  case AArch64ISD::SVE_LD4_MERGE_ZERO:
    return getPackedVectorTypeFromPredicateType(
        Ctx, Root->getOperand(1)->getValueType(0), /*NumVec=*/4);
  default:
    break;
  }

  if (Opcode != ISD::INTRINSIC_VOID && Opcode != ISD::INTRINSIC_W_CHAIN)
    return EVT();

  switch (Root->getConstantOperandVal(1)) {
  default:
    return EVT();
  case Intrinsic::aarch64_sme_ldr:
  case Intrinsic::aarch64_sme_str:
    return MVT::nxv16i8;
  case Intrinsic::aarch64_sve_prf:
    // We are using an SVE prefetch intrinsic. Type must be inferred from the
    // width of the predicate.
    return getPackedVectorTypeFromPredicateType(
        Ctx, Root->getOperand(2)->getValueType(0), /*NumVec=*/1);
  case Intrinsic::aarch64_sve_ld2_sret:
  case Intrinsic::aarch64_sve_ld2q_sret:
    return getPackedVectorTypeFromPredicateType(
        Ctx, Root->getOperand(2)->getValueType(0), /*NumVec=*/2);
  case Intrinsic::aarch64_sve_st2q:
    return getPackedVectorTypeFromPredicateType(
        Ctx, Root->getOperand(4)->getValueType(0), /*NumVec=*/2);
  case Intrinsic::aarch64_sve_ld3_sret:
  case Intrinsic::aarch64_sve_ld3q_sret:
    return getPackedVectorTypeFromPredicateType(
        Ctx, Root->getOperand(2)->getValueType(0), /*NumVec=*/3);
  case Intrinsic::aarch64_sve_st3q:
    return getPackedVectorTypeFromPredicateType(
        Ctx, Root->getOperand(5)->getValueType(0), /*NumVec=*/3);
  case Intrinsic::aarch64_sve_ld4_sret:
  case Intrinsic::aarch64_sve_ld4q_sret:
    return getPackedVectorTypeFromPredicateType(
        Ctx, Root->getOperand(2)->getValueType(0), /*NumVec=*/4);
  case Intrinsic::aarch64_sve_st4q:
    return getPackedVectorTypeFromPredicateType(
        Ctx, Root->getOperand(6)->getValueType(0), /*NumVec=*/4);
  case Intrinsic::aarch64_sve_ld1udq:
  case Intrinsic::aarch64_sve_st1dq:
    return EVT(MVT::nxv1i64);
  case Intrinsic::aarch64_sve_ld1uwq:
  case Intrinsic::aarch64_sve_st1wq:
    return EVT(MVT::nxv1i32);
  }
}

/// SelectAddrModeIndexedSVE - Attempt selection of the addressing mode:
/// Base + OffImm * sizeof(MemVT) for Min >= OffImm <= Max
/// where Root is the memory access using N for its address.
template <int64_t Min, int64_t Max>
bool AArch64DAGToDAGISel::SelectAddrModeIndexedSVE(SDNode *Root, SDValue N,
                                                   SDValue &Base,
                                                   SDValue &OffImm) {
  const EVT MemVT = getMemVTFromNode(*(CurDAG->getContext()), Root);
  const DataLayout &DL = CurDAG->getDataLayout();
  const MachineFrameInfo &MFI = MF->getFrameInfo();

  if (N.getOpcode() == ISD::FrameIndex) {
    int FI = cast<FrameIndexSDNode>(N)->getIndex();
    // We can only encode VL scaled offsets, so only fold in frame indexes
    // referencing SVE objects.
    if (MFI.getStackID(FI) == TargetStackID::ScalableVector) {
      Base = CurDAG->getTargetFrameIndex(FI, TLI->getPointerTy(DL));
      OffImm = CurDAG->getTargetConstant(0, SDLoc(N), MVT::i64);
      return true;
    }

    return false;
  }

  if (MemVT == EVT())
    return false;

  if (N.getOpcode() != ISD::ADD)
    return false;

  SDValue VScale = N.getOperand(1);
  if (VScale.getOpcode() != ISD::VSCALE)
    return false;

  TypeSize TS = MemVT.getSizeInBits();
  int64_t MemWidthBytes = static_cast<int64_t>(TS.getKnownMinValue()) / 8;
  int64_t MulImm = cast<ConstantSDNode>(VScale.getOperand(0))->getSExtValue();

  if ((MulImm % MemWidthBytes) != 0)
    return false;

  int64_t Offset = MulImm / MemWidthBytes;
  if (Offset < Min || Offset > Max)
    return false;

  Base = N.getOperand(0);
  if (Base.getOpcode() == ISD::FrameIndex) {
    int FI = cast<FrameIndexSDNode>(Base)->getIndex();
    // We can only encode VL scaled offsets, so only fold in frame indexes
    // referencing SVE objects.
    if (MFI.getStackID(FI) == TargetStackID::ScalableVector)
      Base = CurDAG->getTargetFrameIndex(FI, TLI->getPointerTy(DL));
  }

  OffImm = CurDAG->getTargetConstant(Offset, SDLoc(N), MVT::i64);
  return true;
}

/// Select register plus register addressing mode for SVE, with scaled
/// offset.
bool AArch64DAGToDAGISel::SelectSVERegRegAddrMode(SDValue N, unsigned Scale,
                                                  SDValue &Base,
                                                  SDValue &Offset) {
  if (N.getOpcode() != ISD::ADD)
    return false;

  // Process an ADD node.
  const SDValue LHS = N.getOperand(0);
  const SDValue RHS = N.getOperand(1);

  // 8 bit data does not come with the SHL node, so it is treated
  // separately.
  if (Scale == 0) {
    Base = LHS;
    Offset = RHS;
    return true;
  }

  if (auto C = dyn_cast<ConstantSDNode>(RHS)) {
    int64_t ImmOff = C->getSExtValue();
    unsigned Size = 1 << Scale;

    // To use the reg+reg addressing mode, the immediate must be a multiple of
    // the vector element's byte size.
    if (ImmOff % Size)
      return false;

    SDLoc DL(N);
    Base = LHS;
    Offset = CurDAG->getTargetConstant(ImmOff >> Scale, DL, MVT::i64);
    SDValue Ops[] = {Offset};
    SDNode *MI = CurDAG->getMachineNode(AArch64::MOVi64imm, DL, MVT::i64, Ops);
    Offset = SDValue(MI, 0);
    return true;
  }

  // Check if the RHS is a shift node with a constant.
  if (RHS.getOpcode() != ISD::SHL)
    return false;

  const SDValue ShiftRHS = RHS.getOperand(1);
  if (auto *C = dyn_cast<ConstantSDNode>(ShiftRHS))
    if (C->getZExtValue() == Scale) {
      Base = LHS;
      Offset = RHS.getOperand(0);
      return true;
    }

  return false;
}

bool AArch64DAGToDAGISel::SelectAllActivePredicate(SDValue N) {
  const AArch64TargetLowering *TLI =
      static_cast<const AArch64TargetLowering *>(getTargetLowering());

  return TLI->isAllActivePredicate(*CurDAG, N);
}

bool AArch64DAGToDAGISel::SelectAnyPredicate(SDValue N) {
  EVT VT = N.getValueType();
  return VT.isScalableVector() && VT.getVectorElementType() == MVT::i1;
}

bool AArch64DAGToDAGISel::SelectSMETileSlice(SDValue N, unsigned MaxSize,
                                             SDValue &Base, SDValue &Offset,
                                             unsigned Scale) {
  // Try to untangle an ADD node into a 'reg + offset'
  if (N.getOpcode() == ISD::ADD)
    if (auto C = dyn_cast<ConstantSDNode>(N.getOperand(1))) {
      int64_t ImmOff = C->getSExtValue();
      if ((ImmOff > 0 && ImmOff <= MaxSize && (ImmOff % Scale == 0))) {
        Base = N.getOperand(0);
        Offset = CurDAG->getTargetConstant(ImmOff / Scale, SDLoc(N), MVT::i64);
        return true;
      }
    }

  // By default, just match reg + 0.
  Base = N;
  Offset = CurDAG->getTargetConstant(0, SDLoc(N), MVT::i64);
  return true;
}
