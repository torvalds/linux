//===----- LegalizeIntegerTypes.cpp - Legalization of integer types -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements integer type expansion and promotion for LegalizeTypes.
// Promotion is the act of changing a computation in an illegal type into a
// computation in a larger type.  For example, implementing i8 arithmetic in an
// i32 register (often needed on powerpc).
// Expansion is the act of changing a computation in an illegal type into a
// computation in two identical registers of a smaller type.  For example,
// implementing i64 arithmetic in two i32 registers (often needed on 32-bit
// targets).
//
//===----------------------------------------------------------------------===//

#include "LegalizeTypes.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
using namespace llvm;

#define DEBUG_TYPE "legalize-types"

//===----------------------------------------------------------------------===//
//  Integer Result Promotion
//===----------------------------------------------------------------------===//

/// PromoteIntegerResult - This method is called when a result of a node is
/// found to be in need of promotion to a larger type.  At this point, the node
/// may also have invalid operands or may have other results that need
/// expansion, we just know that (at least) one result needs promotion.
void DAGTypeLegalizer::PromoteIntegerResult(SDNode *N, unsigned ResNo) {
  LLVM_DEBUG(dbgs() << "Promote integer result: "; N->dump(&DAG));
  SDValue Res = SDValue();

  // See if the target wants to custom expand this node.
  if (CustomLowerNode(N, N->getValueType(ResNo), true)) {
    LLVM_DEBUG(dbgs() << "Node has been custom expanded, done\n");
    return;
  }

  switch (N->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "PromoteIntegerResult #" << ResNo << ": ";
    N->dump(&DAG); dbgs() << "\n";
#endif
    report_fatal_error("Do not know how to promote this operator!");
  case ISD::MERGE_VALUES:Res = PromoteIntRes_MERGE_VALUES(N, ResNo); break;
  case ISD::AssertSext:  Res = PromoteIntRes_AssertSext(N); break;
  case ISD::AssertZext:  Res = PromoteIntRes_AssertZext(N); break;
  case ISD::BITCAST:     Res = PromoteIntRes_BITCAST(N); break;
  case ISD::VP_BITREVERSE:
  case ISD::BITREVERSE:  Res = PromoteIntRes_BITREVERSE(N); break;
  case ISD::VP_BSWAP:
  case ISD::BSWAP:       Res = PromoteIntRes_BSWAP(N); break;
  case ISD::BUILD_PAIR:  Res = PromoteIntRes_BUILD_PAIR(N); break;
  case ISD::Constant:    Res = PromoteIntRes_Constant(N); break;
  case ISD::VP_CTLZ_ZERO_UNDEF:
  case ISD::VP_CTLZ:
  case ISD::CTLZ_ZERO_UNDEF:
  case ISD::CTLZ:        Res = PromoteIntRes_CTLZ(N); break;
  case ISD::PARITY:
  case ISD::VP_CTPOP:
  case ISD::CTPOP:       Res = PromoteIntRes_CTPOP_PARITY(N); break;
  case ISD::VP_CTTZ_ZERO_UNDEF:
  case ISD::VP_CTTZ:
  case ISD::CTTZ_ZERO_UNDEF:
  case ISD::CTTZ:        Res = PromoteIntRes_CTTZ(N); break;
  case ISD::VP_CTTZ_ELTS_ZERO_UNDEF:
  case ISD::VP_CTTZ_ELTS:
    Res = PromoteIntRes_VP_CttzElements(N);
    break;
  case ISD::EXTRACT_VECTOR_ELT:
                         Res = PromoteIntRes_EXTRACT_VECTOR_ELT(N); break;
  case ISD::LOAD:        Res = PromoteIntRes_LOAD(cast<LoadSDNode>(N)); break;
  case ISD::MLOAD:       Res = PromoteIntRes_MLOAD(cast<MaskedLoadSDNode>(N));
    break;
  case ISD::MGATHER:     Res = PromoteIntRes_MGATHER(cast<MaskedGatherSDNode>(N));
    break;
  case ISD::VECTOR_COMPRESS:
    Res = PromoteIntRes_VECTOR_COMPRESS(N);
    break;
  case ISD::SELECT:
  case ISD::VSELECT:
  case ISD::VP_SELECT:
  case ISD::VP_MERGE:
    Res = PromoteIntRes_Select(N);
    break;
  case ISD::SELECT_CC:   Res = PromoteIntRes_SELECT_CC(N); break;
  case ISD::STRICT_FSETCC:
  case ISD::STRICT_FSETCCS:
  case ISD::SETCC:       Res = PromoteIntRes_SETCC(N); break;
  case ISD::SMIN:
  case ISD::SMAX:        Res = PromoteIntRes_SExtIntBinOp(N); break;
  case ISD::UMIN:
  case ISD::UMAX:        Res = PromoteIntRes_UMINUMAX(N); break;

  case ISD::SHL:
  case ISD::VP_SHL:      Res = PromoteIntRes_SHL(N); break;
  case ISD::SIGN_EXTEND_INREG:
                         Res = PromoteIntRes_SIGN_EXTEND_INREG(N); break;
  case ISD::SRA:
  case ISD::VP_SRA:      Res = PromoteIntRes_SRA(N); break;
  case ISD::SRL:
  case ISD::VP_SRL:      Res = PromoteIntRes_SRL(N); break;
  case ISD::VP_TRUNCATE:
  case ISD::TRUNCATE:    Res = PromoteIntRes_TRUNCATE(N); break;
  case ISD::UNDEF:       Res = PromoteIntRes_UNDEF(N); break;
  case ISD::VAARG:       Res = PromoteIntRes_VAARG(N); break;
  case ISD::VSCALE:      Res = PromoteIntRes_VSCALE(N); break;

  case ISD::EXTRACT_SUBVECTOR:
                         Res = PromoteIntRes_EXTRACT_SUBVECTOR(N); break;
  case ISD::INSERT_SUBVECTOR:
                         Res = PromoteIntRes_INSERT_SUBVECTOR(N); break;
  case ISD::VECTOR_REVERSE:
                         Res = PromoteIntRes_VECTOR_REVERSE(N); break;
  case ISD::VECTOR_SHUFFLE:
                         Res = PromoteIntRes_VECTOR_SHUFFLE(N); break;
  case ISD::VECTOR_SPLICE:
                         Res = PromoteIntRes_VECTOR_SPLICE(N); break;
  case ISD::VECTOR_INTERLEAVE:
  case ISD::VECTOR_DEINTERLEAVE:
    Res = PromoteIntRes_VECTOR_INTERLEAVE_DEINTERLEAVE(N);
    return;
  case ISD::INSERT_VECTOR_ELT:
                         Res = PromoteIntRes_INSERT_VECTOR_ELT(N); break;
  case ISD::BUILD_VECTOR:
    Res = PromoteIntRes_BUILD_VECTOR(N);
    break;
  case ISD::SPLAT_VECTOR:
  case ISD::SCALAR_TO_VECTOR:
  case ISD::EXPERIMENTAL_VP_SPLAT:
    Res = PromoteIntRes_ScalarOp(N);
    break;
  case ISD::STEP_VECTOR: Res = PromoteIntRes_STEP_VECTOR(N); break;
  case ISD::CONCAT_VECTORS:
                         Res = PromoteIntRes_CONCAT_VECTORS(N); break;

  case ISD::ANY_EXTEND_VECTOR_INREG:
  case ISD::SIGN_EXTEND_VECTOR_INREG:
  case ISD::ZERO_EXTEND_VECTOR_INREG:
                         Res = PromoteIntRes_EXTEND_VECTOR_INREG(N); break;

  case ISD::SIGN_EXTEND:
  case ISD::VP_SIGN_EXTEND:
  case ISD::ZERO_EXTEND:
  case ISD::VP_ZERO_EXTEND:
  case ISD::ANY_EXTEND:  Res = PromoteIntRes_INT_EXTEND(N); break;

  case ISD::VP_FP_TO_SINT:
  case ISD::VP_FP_TO_UINT:
  case ISD::STRICT_FP_TO_SINT:
  case ISD::STRICT_FP_TO_UINT:
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:  Res = PromoteIntRes_FP_TO_XINT(N); break;

  case ISD::FP_TO_SINT_SAT:
  case ISD::FP_TO_UINT_SAT:
                         Res = PromoteIntRes_FP_TO_XINT_SAT(N); break;

  case ISD::FP_TO_BF16:
  case ISD::FP_TO_FP16:
    Res = PromoteIntRes_FP_TO_FP16_BF16(N);
    break;
  case ISD::STRICT_FP_TO_BF16:
  case ISD::STRICT_FP_TO_FP16:
    Res = PromoteIntRes_STRICT_FP_TO_FP16_BF16(N);
    break;
  case ISD::GET_ROUNDING: Res = PromoteIntRes_GET_ROUNDING(N); break;

  case ISD::AND:
  case ISD::OR:
  case ISD::XOR:
  case ISD::ADD:
  case ISD::SUB:
  case ISD::MUL:
  case ISD::VP_AND:
  case ISD::VP_OR:
  case ISD::VP_XOR:
  case ISD::VP_ADD:
  case ISD::VP_SUB:
  case ISD::VP_MUL:      Res = PromoteIntRes_SimpleIntBinOp(N); break;

  case ISD::AVGCEILS:
  case ISD::AVGFLOORS:
  case ISD::VP_SMIN:
  case ISD::VP_SMAX:
  case ISD::SDIV:
  case ISD::SREM:
  case ISD::VP_SDIV:
  case ISD::VP_SREM:     Res = PromoteIntRes_SExtIntBinOp(N); break;

  case ISD::AVGCEILU:
  case ISD::AVGFLOORU:
  case ISD::VP_UMIN:
  case ISD::VP_UMAX:
  case ISD::UDIV:
  case ISD::UREM:
  case ISD::VP_UDIV:
  case ISD::VP_UREM:     Res = PromoteIntRes_ZExtIntBinOp(N); break;

  case ISD::SADDO:
  case ISD::SSUBO:       Res = PromoteIntRes_SADDSUBO(N, ResNo); break;
  case ISD::UADDO:
  case ISD::USUBO:       Res = PromoteIntRes_UADDSUBO(N, ResNo); break;
  case ISD::SMULO:
  case ISD::UMULO:       Res = PromoteIntRes_XMULO(N, ResNo); break;

  case ISD::ADDE:
  case ISD::SUBE:
  case ISD::UADDO_CARRY:
  case ISD::USUBO_CARRY: Res = PromoteIntRes_UADDSUBO_CARRY(N, ResNo); break;

  case ISD::SADDO_CARRY:
  case ISD::SSUBO_CARRY: Res = PromoteIntRes_SADDSUBO_CARRY(N, ResNo); break;

  case ISD::SADDSAT:
  case ISD::UADDSAT:
  case ISD::SSUBSAT:
  case ISD::USUBSAT:
  case ISD::SSHLSAT:
  case ISD::USHLSAT:
    Res = PromoteIntRes_ADDSUBSHLSAT<EmptyMatchContext>(N);
    break;
  case ISD::VP_SADDSAT:
  case ISD::VP_UADDSAT:
  case ISD::VP_SSUBSAT:
  case ISD::VP_USUBSAT:
    Res = PromoteIntRes_ADDSUBSHLSAT<VPMatchContext>(N);
    break;

  case ISD::SCMP:
  case ISD::UCMP:
    Res = PromoteIntRes_CMP(N);
    break;

  case ISD::SMULFIX:
  case ISD::SMULFIXSAT:
  case ISD::UMULFIX:
  case ISD::UMULFIXSAT:  Res = PromoteIntRes_MULFIX(N); break;

  case ISD::SDIVFIX:
  case ISD::SDIVFIXSAT:
  case ISD::UDIVFIX:
  case ISD::UDIVFIXSAT:  Res = PromoteIntRes_DIVFIX(N); break;

  case ISD::ABS:         Res = PromoteIntRes_ABS(N); break;

  case ISD::ATOMIC_LOAD:
    Res = PromoteIntRes_Atomic0(cast<AtomicSDNode>(N)); break;

  case ISD::ATOMIC_LOAD_ADD:
  case ISD::ATOMIC_LOAD_SUB:
  case ISD::ATOMIC_LOAD_AND:
  case ISD::ATOMIC_LOAD_CLR:
  case ISD::ATOMIC_LOAD_OR:
  case ISD::ATOMIC_LOAD_XOR:
  case ISD::ATOMIC_LOAD_NAND:
  case ISD::ATOMIC_LOAD_MIN:
  case ISD::ATOMIC_LOAD_MAX:
  case ISD::ATOMIC_LOAD_UMIN:
  case ISD::ATOMIC_LOAD_UMAX:
  case ISD::ATOMIC_SWAP:
    Res = PromoteIntRes_Atomic1(cast<AtomicSDNode>(N)); break;

  case ISD::ATOMIC_CMP_SWAP:
  case ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS:
    Res = PromoteIntRes_AtomicCmpSwap(cast<AtomicSDNode>(N), ResNo);
    break;

  case ISD::VECREDUCE_ADD:
  case ISD::VECREDUCE_MUL:
  case ISD::VECREDUCE_AND:
  case ISD::VECREDUCE_OR:
  case ISD::VECREDUCE_XOR:
  case ISD::VECREDUCE_SMAX:
  case ISD::VECREDUCE_SMIN:
  case ISD::VECREDUCE_UMAX:
  case ISD::VECREDUCE_UMIN:
    Res = PromoteIntRes_VECREDUCE(N);
    break;

  case ISD::VP_REDUCE_ADD:
  case ISD::VP_REDUCE_MUL:
  case ISD::VP_REDUCE_AND:
  case ISD::VP_REDUCE_OR:
  case ISD::VP_REDUCE_XOR:
  case ISD::VP_REDUCE_SMAX:
  case ISD::VP_REDUCE_SMIN:
  case ISD::VP_REDUCE_UMAX:
  case ISD::VP_REDUCE_UMIN:
    Res = PromoteIntRes_VP_REDUCE(N);
    break;

  case ISD::FREEZE:
    Res = PromoteIntRes_FREEZE(N);
    break;

  case ISD::ROTL:
  case ISD::ROTR:
    Res = PromoteIntRes_Rotate(N);
    break;

  case ISD::FSHL:
  case ISD::FSHR:
    Res = PromoteIntRes_FunnelShift(N);
    break;

  case ISD::VP_FSHL:
  case ISD::VP_FSHR:
    Res = PromoteIntRes_VPFunnelShift(N);
    break;

  case ISD::IS_FPCLASS:
    Res = PromoteIntRes_IS_FPCLASS(N);
    break;
  case ISD::FFREXP:
    Res = PromoteIntRes_FFREXP(N);
    break;

  case ISD::LRINT:
  case ISD::LLRINT:
    Res = PromoteIntRes_XRINT(N);
    break;

  case ISD::PATCHPOINT:
    Res = PromoteIntRes_PATCHPOINT(N);
    break;
  }

  // If the result is null then the sub-method took care of registering it.
  if (Res.getNode())
    SetPromotedInteger(SDValue(N, ResNo), Res);
}

SDValue DAGTypeLegalizer::PromoteIntRes_MERGE_VALUES(SDNode *N,
                                                     unsigned ResNo) {
  SDValue Op = DisintegrateMERGE_VALUES(N, ResNo);
  return GetPromotedInteger(Op);
}

SDValue DAGTypeLegalizer::PromoteIntRes_AssertSext(SDNode *N) {
  // Sign-extend the new bits, and continue the assertion.
  SDValue Op = SExtPromotedInteger(N->getOperand(0));
  return DAG.getNode(ISD::AssertSext, SDLoc(N),
                     Op.getValueType(), Op, N->getOperand(1));
}

SDValue DAGTypeLegalizer::PromoteIntRes_AssertZext(SDNode *N) {
  // Zero the new bits, and continue the assertion.
  SDValue Op = ZExtPromotedInteger(N->getOperand(0));
  return DAG.getNode(ISD::AssertZext, SDLoc(N),
                     Op.getValueType(), Op, N->getOperand(1));
}

SDValue DAGTypeLegalizer::PromoteIntRes_Atomic0(AtomicSDNode *N) {
  EVT ResVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Res = DAG.getAtomic(N->getOpcode(), SDLoc(N),
                              N->getMemoryVT(), ResVT,
                              N->getChain(), N->getBasePtr(),
                              N->getMemOperand());
  if (N->getOpcode() == ISD::ATOMIC_LOAD) {
    ISD::LoadExtType ETy = cast<AtomicSDNode>(N)->getExtensionType();
    if (ETy == ISD::NON_EXTLOAD) {
      switch (TLI.getExtendForAtomicOps()) {
      case ISD::SIGN_EXTEND:
        ETy = ISD::SEXTLOAD;
        break;
      case ISD::ZERO_EXTEND:
        ETy = ISD::ZEXTLOAD;
        break;
      case ISD::ANY_EXTEND:
        ETy = ISD::EXTLOAD;
        break;
      default:
        llvm_unreachable("Invalid atomic op extension");
      }
    }
    cast<AtomicSDNode>(Res)->setExtensionType(ETy);
  }

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  return Res;
}

SDValue DAGTypeLegalizer::PromoteIntRes_Atomic1(AtomicSDNode *N) {
  SDValue Op2 = GetPromotedInteger(N->getOperand(2));
  SDValue Res = DAG.getAtomic(N->getOpcode(), SDLoc(N),
                              N->getMemoryVT(),
                              N->getChain(), N->getBasePtr(),
                              Op2, N->getMemOperand());
  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  return Res;
}

SDValue DAGTypeLegalizer::PromoteIntRes_AtomicCmpSwap(AtomicSDNode *N,
                                                      unsigned ResNo) {
  if (ResNo == 1) {
    assert(N->getOpcode() == ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS);
    EVT SVT = getSetCCResultType(N->getOperand(2).getValueType());
    EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(1));

    // Only use the result of getSetCCResultType if it is legal,
    // otherwise just use the promoted result type (NVT).
    if (!TLI.isTypeLegal(SVT))
      SVT = NVT;

    SDVTList VTs = DAG.getVTList(N->getValueType(0), SVT, MVT::Other);
    SDValue Res = DAG.getAtomicCmpSwap(
        ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS, SDLoc(N), N->getMemoryVT(), VTs,
        N->getChain(), N->getBasePtr(), N->getOperand(2), N->getOperand(3),
        N->getMemOperand());
    ReplaceValueWith(SDValue(N, 0), Res.getValue(0));
    ReplaceValueWith(SDValue(N, 2), Res.getValue(2));
    return DAG.getSExtOrTrunc(Res.getValue(1), SDLoc(N), NVT);
  }

  // Op2 is used for the comparison and thus must be extended according to the
  // target's atomic operations. Op3 is merely stored and so can be left alone.
  SDValue Op2 = N->getOperand(2);
  SDValue Op3 = GetPromotedInteger(N->getOperand(3));
  switch (TLI.getExtendForAtomicCmpSwapArg()) {
  case ISD::SIGN_EXTEND:
    Op2 = SExtPromotedInteger(Op2);
    break;
  case ISD::ZERO_EXTEND:
    Op2 = ZExtPromotedInteger(Op2);
    break;
  case ISD::ANY_EXTEND:
    Op2 = GetPromotedInteger(Op2);
    break;
  default:
    llvm_unreachable("Invalid atomic op extension");
  }

  SDVTList VTs =
      DAG.getVTList(Op2.getValueType(), N->getValueType(1), MVT::Other);
  SDValue Res = DAG.getAtomicCmpSwap(
      N->getOpcode(), SDLoc(N), N->getMemoryVT(), VTs, N->getChain(),
      N->getBasePtr(), Op2, Op3, N->getMemOperand());
  // Update the use to N with the newly created Res.
  for (unsigned i = 1, NumResults = N->getNumValues(); i < NumResults; ++i)
    ReplaceValueWith(SDValue(N, i), Res.getValue(i));
  return Res;
}

SDValue DAGTypeLegalizer::PromoteIntRes_BITCAST(SDNode *N) {
  SDValue InOp = N->getOperand(0);
  EVT InVT = InOp.getValueType();
  EVT NInVT = TLI.getTypeToTransformTo(*DAG.getContext(), InVT);
  EVT OutVT = N->getValueType(0);
  EVT NOutVT = TLI.getTypeToTransformTo(*DAG.getContext(), OutVT);
  SDLoc dl(N);

  switch (getTypeAction(InVT)) {
  case TargetLowering::TypeLegal:
    break;
  case TargetLowering::TypePromoteInteger:
    if (NOutVT.bitsEq(NInVT) && !NOutVT.isVector() && !NInVT.isVector())
      // The input promotes to the same size.  Convert the promoted value.
      return DAG.getNode(ISD::BITCAST, dl, NOutVT, GetPromotedInteger(InOp));
    break;
  case TargetLowering::TypeSoftenFloat:
    // Promote the integer operand by hand.
    return DAG.getNode(ISD::ANY_EXTEND, dl, NOutVT, GetSoftenedFloat(InOp));
  case TargetLowering::TypeSoftPromoteHalf:
    // Promote the integer operand by hand.
    return DAG.getNode(ISD::ANY_EXTEND, dl, NOutVT, GetSoftPromotedHalf(InOp));
  case TargetLowering::TypePromoteFloat: {
    // Convert the promoted float by hand.
    if (!NOutVT.isVector())
      return DAG.getNode(ISD::FP_TO_FP16, dl, NOutVT, GetPromotedFloat(InOp));
    break;
  }
  case TargetLowering::TypeExpandInteger:
  case TargetLowering::TypeExpandFloat:
    break;
  case TargetLowering::TypeScalarizeVector:
    // Convert the element to an integer and promote it by hand.
    if (!NOutVT.isVector())
      return DAG.getNode(ISD::ANY_EXTEND, dl, NOutVT,
                         BitConvertToInteger(GetScalarizedVector(InOp)));
    break;
  case TargetLowering::TypeScalarizeScalableVector:
    report_fatal_error("Scalarization of scalable vectors is not supported.");
  case TargetLowering::TypeSplitVector: {
    if (!NOutVT.isVector()) {
      // For example, i32 = BITCAST v2i16 on alpha.  Convert the split
      // pieces of the input into integers and reassemble in the final type.
      SDValue Lo, Hi;
      GetSplitVector(N->getOperand(0), Lo, Hi);
      Lo = BitConvertToInteger(Lo);
      Hi = BitConvertToInteger(Hi);

      if (DAG.getDataLayout().isBigEndian())
        std::swap(Lo, Hi);

      InOp = DAG.getNode(ISD::ANY_EXTEND, dl,
                         EVT::getIntegerVT(*DAG.getContext(),
                                           NOutVT.getSizeInBits()),
                         JoinIntegers(Lo, Hi));
      return DAG.getNode(ISD::BITCAST, dl, NOutVT, InOp);
    }
    break;
  }
  case TargetLowering::TypeWidenVector:
    // The input is widened to the same size. Convert to the widened value.
    // Make sure that the outgoing value is not a vector, because this would
    // make us bitcast between two vectors which are legalized in different ways.
    if (NOutVT.bitsEq(NInVT) && !NOutVT.isVector()) {
      SDValue Res =
        DAG.getNode(ISD::BITCAST, dl, NOutVT, GetWidenedVector(InOp));

      // For big endian targets we need to shift the casted value or the
      // interesting bits will end up at the wrong place.
      if (DAG.getDataLayout().isBigEndian()) {
        unsigned ShiftAmt = NInVT.getSizeInBits() - InVT.getSizeInBits();
        assert(ShiftAmt < NOutVT.getSizeInBits() && "Too large shift amount!");
        Res = DAG.getNode(ISD::SRL, dl, NOutVT, Res,
                          DAG.getShiftAmountConstant(ShiftAmt, NOutVT, dl));
      }
      return Res;
    }
    // If the output type is also a vector and widening it to the same size
    // as the widened input type would be a legal type, we can widen the bitcast
    // and handle the promotion after.
    if (NOutVT.isVector()) {
      TypeSize WidenInSize = NInVT.getSizeInBits();
      TypeSize OutSize = OutVT.getSizeInBits();
      if (WidenInSize.hasKnownScalarFactor(OutSize)) {
        unsigned Scale = WidenInSize.getKnownScalarFactor(OutSize);
        EVT WideOutVT =
            EVT::getVectorVT(*DAG.getContext(), OutVT.getVectorElementType(),
                             OutVT.getVectorElementCount() * Scale);
        if (isTypeLegal(WideOutVT)) {
          InOp = DAG.getBitcast(WideOutVT, GetWidenedVector(InOp));
          InOp = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, OutVT, InOp,
                             DAG.getVectorIdxConstant(0, dl));
          return DAG.getNode(ISD::ANY_EXTEND, dl, NOutVT, InOp);
        }
      }
    }
  }

  return DAG.getNode(ISD::ANY_EXTEND, dl, NOutVT,
                     CreateStackStoreLoad(InOp, OutVT));
}

SDValue DAGTypeLegalizer::PromoteIntRes_FREEZE(SDNode *N) {
  SDValue V = GetPromotedInteger(N->getOperand(0));
  return DAG.getNode(ISD::FREEZE, SDLoc(N),
                     V.getValueType(), V);
}

SDValue DAGTypeLegalizer::PromoteIntRes_BSWAP(SDNode *N) {
  SDValue Op = GetPromotedInteger(N->getOperand(0));
  EVT OVT = N->getValueType(0);
  EVT NVT = Op.getValueType();
  SDLoc dl(N);

  // If the larger BSWAP isn't supported by the target, try to expand now.
  // If we expand later we'll end up with more operations since we lost the
  // original type. We only do this for scalars since we have a shuffle
  // based lowering for vectors in LegalizeVectorOps.
  if (!OVT.isVector() &&
      !TLI.isOperationLegalOrCustomOrPromote(ISD::BSWAP, NVT)) {
    if (SDValue Res = TLI.expandBSWAP(N, DAG))
      return DAG.getNode(ISD::ANY_EXTEND, dl, NVT, Res);
  }

  unsigned DiffBits = NVT.getScalarSizeInBits() - OVT.getScalarSizeInBits();
  SDValue ShAmt = DAG.getShiftAmountConstant(DiffBits, NVT, dl);
  if (N->getOpcode() == ISD::BSWAP)
    return DAG.getNode(ISD::SRL, dl, NVT, DAG.getNode(ISD::BSWAP, dl, NVT, Op),
                       ShAmt);
  SDValue Mask = N->getOperand(1);
  SDValue EVL = N->getOperand(2);
  return DAG.getNode(ISD::VP_SRL, dl, NVT,
                     DAG.getNode(ISD::VP_BSWAP, dl, NVT, Op, Mask, EVL), ShAmt,
                     Mask, EVL);
}

SDValue DAGTypeLegalizer::PromoteIntRes_BITREVERSE(SDNode *N) {
  SDValue Op = GetPromotedInteger(N->getOperand(0));
  EVT OVT = N->getValueType(0);
  EVT NVT = Op.getValueType();
  SDLoc dl(N);

  // If the larger BITREVERSE isn't supported by the target, try to expand now.
  // If we expand later we'll end up with more operations since we lost the
  // original type. We only do this for scalars since we have a shuffle
  // based lowering for vectors in LegalizeVectorOps.
  if (!OVT.isVector() && OVT.isSimple() &&
      !TLI.isOperationLegalOrCustomOrPromote(ISD::BITREVERSE, NVT)) {
    if (SDValue Res = TLI.expandBITREVERSE(N, DAG))
      return DAG.getNode(ISD::ANY_EXTEND, dl, NVT, Res);
  }

  unsigned DiffBits = NVT.getScalarSizeInBits() - OVT.getScalarSizeInBits();
  SDValue ShAmt = DAG.getShiftAmountConstant(DiffBits, NVT, dl);
  if (N->getOpcode() == ISD::BITREVERSE)
    return DAG.getNode(ISD::SRL, dl, NVT,
                       DAG.getNode(ISD::BITREVERSE, dl, NVT, Op), ShAmt);
  SDValue Mask = N->getOperand(1);
  SDValue EVL = N->getOperand(2);
  return DAG.getNode(ISD::VP_SRL, dl, NVT,
                     DAG.getNode(ISD::VP_BITREVERSE, dl, NVT, Op, Mask, EVL),
                     ShAmt, Mask, EVL);
}

SDValue DAGTypeLegalizer::PromoteIntRes_BUILD_PAIR(SDNode *N) {
  // The pair element type may be legal, or may not promote to the same type as
  // the result, for example i14 = BUILD_PAIR (i7, i7).  Handle all cases.
  return DAG.getNode(ISD::ANY_EXTEND, SDLoc(N),
                     TLI.getTypeToTransformTo(*DAG.getContext(),
                     N->getValueType(0)), JoinIntegers(N->getOperand(0),
                     N->getOperand(1)));
}

SDValue DAGTypeLegalizer::PromoteIntRes_Constant(SDNode *N) {
  EVT VT = N->getValueType(0);
  // FIXME there is no actual debug info here
  SDLoc dl(N);
  // Zero extend things like i1, sign extend everything else.  It shouldn't
  // matter in theory which one we pick, but this tends to give better code?
  unsigned Opc = VT.isByteSized() ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
  SDValue Result = DAG.getNode(Opc, dl,
                               TLI.getTypeToTransformTo(*DAG.getContext(), VT),
                               SDValue(N, 0));
  assert(isa<ConstantSDNode>(Result) && "Didn't constant fold ext?");
  return Result;
}

SDValue DAGTypeLegalizer::PromoteIntRes_CTLZ(SDNode *N) {
  EVT OVT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), OVT);
  SDLoc dl(N);

  // If the larger CTLZ isn't supported by the target, try to expand now.
  // If we expand later we'll end up with more operations since we lost the
  // original type.
  if (!OVT.isVector() && TLI.isTypeLegal(NVT) &&
      !TLI.isOperationLegalOrCustomOrPromote(ISD::CTLZ, NVT) &&
      !TLI.isOperationLegalOrCustomOrPromote(ISD::CTLZ_ZERO_UNDEF, NVT)) {
    if (SDValue Result = TLI.expandCTLZ(N, DAG)) {
      Result = DAG.getNode(ISD::ANY_EXTEND, dl, NVT, Result);
      return Result;
    }
  }

  unsigned CtlzOpcode = N->getOpcode();
  if (CtlzOpcode == ISD::CTLZ || CtlzOpcode == ISD::VP_CTLZ) {
    // Subtract off the extra leading bits in the bigger type.
    SDValue ExtractLeadingBits = DAG.getConstant(
        NVT.getScalarSizeInBits() - OVT.getScalarSizeInBits(), dl, NVT);

    if (!N->isVPOpcode()) {
      // Zero extend to the promoted type and do the count there.
      SDValue Op = ZExtPromotedInteger(N->getOperand(0));
      return DAG.getNode(ISD::SUB, dl, NVT,
                         DAG.getNode(N->getOpcode(), dl, NVT, Op),
                         ExtractLeadingBits);
    }
    SDValue Mask = N->getOperand(1);
    SDValue EVL = N->getOperand(2);
    // Zero extend to the promoted type and do the count there.
    SDValue Op = VPZExtPromotedInteger(N->getOperand(0), Mask, EVL);
    return DAG.getNode(ISD::VP_SUB, dl, NVT,
                       DAG.getNode(N->getOpcode(), dl, NVT, Op, Mask, EVL),
                       ExtractLeadingBits, Mask, EVL);
  }
  if (CtlzOpcode == ISD::CTLZ_ZERO_UNDEF ||
      CtlzOpcode == ISD::VP_CTLZ_ZERO_UNDEF) {
    // Any Extend the argument
    SDValue Op = GetPromotedInteger(N->getOperand(0));
    // Op = Op << (sizeinbits(NVT) - sizeinbits(Old VT))
    unsigned SHLAmount = NVT.getScalarSizeInBits() - OVT.getScalarSizeInBits();
    auto ShiftConst =
        DAG.getShiftAmountConstant(SHLAmount, Op.getValueType(), dl);
    if (!N->isVPOpcode()) {
      Op = DAG.getNode(ISD::SHL, dl, NVT, Op, ShiftConst);
      return DAG.getNode(CtlzOpcode, dl, NVT, Op);
    }

    SDValue Mask = N->getOperand(1);
    SDValue EVL = N->getOperand(2);
    Op = DAG.getNode(ISD::VP_SHL, dl, NVT, Op, ShiftConst, Mask, EVL);
    return DAG.getNode(CtlzOpcode, dl, NVT, Op, Mask, EVL);
  }
  llvm_unreachable("Invalid CTLZ Opcode");
}

SDValue DAGTypeLegalizer::PromoteIntRes_CTPOP_PARITY(SDNode *N) {
  EVT OVT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), OVT);

  // If the larger CTPOP isn't supported by the target, try to expand now.
  // If we expand later we'll end up with more operations since we lost the
  // original type.
  // TODO: Expand ISD::PARITY. Need to move ExpandPARITY from LegalizeDAG to
  // TargetLowering.
  if (N->getOpcode() == ISD::CTPOP && !OVT.isVector() && TLI.isTypeLegal(NVT) &&
      !TLI.isOperationLegalOrCustomOrPromote(ISD::CTPOP, NVT)) {
    if (SDValue Result = TLI.expandCTPOP(N, DAG)) {
      Result = DAG.getNode(ISD::ANY_EXTEND, SDLoc(N), NVT, Result);
      return Result;
    }
  }

  // Zero extend to the promoted type and do the count or parity there.
  if (!N->isVPOpcode()) {
    SDValue Op = ZExtPromotedInteger(N->getOperand(0));
    return DAG.getNode(N->getOpcode(), SDLoc(N), Op.getValueType(), Op);
  }

  SDValue Mask = N->getOperand(1);
  SDValue EVL = N->getOperand(2);
  SDValue Op = VPZExtPromotedInteger(N->getOperand(0), Mask, EVL);
  return DAG.getNode(N->getOpcode(), SDLoc(N), Op.getValueType(), Op, Mask,
                     EVL);
}

SDValue DAGTypeLegalizer::PromoteIntRes_CTTZ(SDNode *N) {
  SDValue Op = GetPromotedInteger(N->getOperand(0));
  EVT OVT = N->getValueType(0);
  EVT NVT = Op.getValueType();
  SDLoc dl(N);

  // If the larger CTTZ isn't supported by the target, try to expand now.
  // If we expand later we'll end up with more operations since we lost the
  // original type. Don't expand if we can use CTPOP or CTLZ expansion on the
  // larger type.
  if (!OVT.isVector() && TLI.isTypeLegal(NVT) &&
      !TLI.isOperationLegalOrCustomOrPromote(ISD::CTTZ, NVT) &&
      !TLI.isOperationLegalOrCustomOrPromote(ISD::CTTZ_ZERO_UNDEF, NVT) &&
      !TLI.isOperationLegal(ISD::CTPOP, NVT) &&
      !TLI.isOperationLegal(ISD::CTLZ, NVT)) {
    if (SDValue Result = TLI.expandCTTZ(N, DAG)) {
      Result = DAG.getNode(ISD::ANY_EXTEND, dl, NVT, Result);
      return Result;
    }
  }

  unsigned NewOpc = N->getOpcode();
  if (NewOpc == ISD::CTTZ || NewOpc == ISD::VP_CTTZ) {
    // The count is the same in the promoted type except if the original
    // value was zero.  This can be handled by setting the bit just off
    // the top of the original type.
    auto TopBit = APInt::getOneBitSet(NVT.getScalarSizeInBits(),
                                      OVT.getScalarSizeInBits());
    if (NewOpc == ISD::CTTZ) {
      Op = DAG.getNode(ISD::OR, dl, NVT, Op, DAG.getConstant(TopBit, dl, NVT));
      NewOpc = ISD::CTTZ_ZERO_UNDEF;
    } else {
      Op =
          DAG.getNode(ISD::VP_OR, dl, NVT, Op, DAG.getConstant(TopBit, dl, NVT),
                      N->getOperand(1), N->getOperand(2));
      NewOpc = ISD::VP_CTTZ_ZERO_UNDEF;
    }
  }
  if (!N->isVPOpcode())
    return DAG.getNode(NewOpc, dl, NVT, Op);
  return DAG.getNode(NewOpc, dl, NVT, Op, N->getOperand(1), N->getOperand(2));
}

SDValue DAGTypeLegalizer::PromoteIntRes_VP_CttzElements(SDNode *N) {
  SDLoc DL(N);
  EVT NewVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  return DAG.getNode(N->getOpcode(), DL, NewVT, N->ops());
}

SDValue DAGTypeLegalizer::PromoteIntRes_EXTRACT_VECTOR_ELT(SDNode *N) {
  SDLoc dl(N);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));

  SDValue Op0 = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);

  // If the input also needs to be promoted, do that first so we can get a
  // get a good idea for the output type.
  if (TLI.getTypeAction(*DAG.getContext(), Op0.getValueType())
      == TargetLowering::TypePromoteInteger) {
    SDValue In = GetPromotedInteger(Op0);

    // If the new type is larger than NVT, use it. We probably won't need to
    // promote it again.
    EVT SVT = In.getValueType().getScalarType();
    if (SVT.bitsGE(NVT)) {
      SDValue Ext = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, SVT, In, Op1);
      return DAG.getAnyExtOrTrunc(Ext, dl, NVT);
    }
  }

  return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, NVT, Op0, Op1);
}

SDValue DAGTypeLegalizer::PromoteIntRes_FP_TO_XINT(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  unsigned NewOpc = N->getOpcode();
  SDLoc dl(N);

  // If we're promoting a UINT to a larger size and the larger FP_TO_UINT is
  // not Legal, check to see if we can use FP_TO_SINT instead.  (If both UINT
  // and SINT conversions are Custom, there is no way to tell which is
  // preferable. We choose SINT because that's the right thing on PPC.)
  if (N->getOpcode() == ISD::FP_TO_UINT &&
      !TLI.isOperationLegal(ISD::FP_TO_UINT, NVT) &&
      TLI.isOperationLegalOrCustom(ISD::FP_TO_SINT, NVT))
    NewOpc = ISD::FP_TO_SINT;

  if (N->getOpcode() == ISD::STRICT_FP_TO_UINT &&
      !TLI.isOperationLegal(ISD::STRICT_FP_TO_UINT, NVT) &&
      TLI.isOperationLegalOrCustom(ISD::STRICT_FP_TO_SINT, NVT))
    NewOpc = ISD::STRICT_FP_TO_SINT;

  if (N->getOpcode() == ISD::VP_FP_TO_UINT &&
      !TLI.isOperationLegal(ISD::VP_FP_TO_UINT, NVT) &&
      TLI.isOperationLegalOrCustom(ISD::VP_FP_TO_SINT, NVT))
    NewOpc = ISD::VP_FP_TO_SINT;

  SDValue Res;
  if (N->isStrictFPOpcode()) {
    Res = DAG.getNode(NewOpc, dl, {NVT, MVT::Other},
                      {N->getOperand(0), N->getOperand(1)});
    // Legalize the chain result - switch anything that used the old chain to
    // use the new one.
    ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  } else if (NewOpc == ISD::VP_FP_TO_SINT || NewOpc == ISD::VP_FP_TO_UINT) {
    Res = DAG.getNode(NewOpc, dl, NVT, {N->getOperand(0), N->getOperand(1),
                      N->getOperand(2)});
  } else {
    Res = DAG.getNode(NewOpc, dl, NVT, N->getOperand(0));
  }

  // Assert that the converted value fits in the original type.  If it doesn't
  // (eg: because the value being converted is too big), then the result of the
  // original operation was undefined anyway, so the assert is still correct.
  //
  // NOTE: fp-to-uint to fp-to-sint promotion guarantees zero extend. For example:
  //   before legalization: fp-to-uint16, 65534. -> 0xfffe
  //   after legalization: fp-to-sint32, 65534. -> 0x0000fffe
  return DAG.getNode((N->getOpcode() == ISD::FP_TO_UINT ||
                      N->getOpcode() == ISD::STRICT_FP_TO_UINT ||
                      N->getOpcode() == ISD::VP_FP_TO_UINT)
                         ? ISD::AssertZext
                         : ISD::AssertSext,
                     dl, NVT, Res,
                     DAG.getValueType(N->getValueType(0).getScalarType()));
}

SDValue DAGTypeLegalizer::PromoteIntRes_FP_TO_XINT_SAT(SDNode *N) {
  // Promote the result type, while keeping the original width in Op1.
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDLoc dl(N);
  return DAG.getNode(N->getOpcode(), dl, NVT, N->getOperand(0),
                     N->getOperand(1));
}

SDValue DAGTypeLegalizer::PromoteIntRes_FP_TO_FP16_BF16(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDLoc dl(N);

  return DAG.getNode(N->getOpcode(), dl, NVT, N->getOperand(0));
}

SDValue DAGTypeLegalizer::PromoteIntRes_STRICT_FP_TO_FP16_BF16(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDLoc dl(N);

  SDValue Res = DAG.getNode(N->getOpcode(), dl, DAG.getVTList(NVT, MVT::Other),
                            N->getOperand(0), N->getOperand(1));
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  return Res;
}

SDValue DAGTypeLegalizer::PromoteIntRes_XRINT(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDLoc dl(N);
  return DAG.getNode(N->getOpcode(), dl, NVT, N->getOperand(0));
}

SDValue DAGTypeLegalizer::PromoteIntRes_GET_ROUNDING(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDLoc dl(N);

  SDValue Res =
      DAG.getNode(N->getOpcode(), dl, {NVT, MVT::Other}, N->getOperand(0));

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  return Res;
}

SDValue DAGTypeLegalizer::PromoteIntRes_INT_EXTEND(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDLoc dl(N);

  if (getTypeAction(N->getOperand(0).getValueType())
      == TargetLowering::TypePromoteInteger) {
    SDValue Res = GetPromotedInteger(N->getOperand(0));
    assert(Res.getValueType().bitsLE(NVT) && "Extension doesn't make sense!");

    // If the result and operand types are the same after promotion, simplify
    // to an in-register extension. Unless this is a VP_*_EXTEND.
    if (NVT == Res.getValueType() && N->getNumOperands() == 1) {
      // The high bits are not guaranteed to be anything.  Insert an extend.
      if (N->getOpcode() == ISD::SIGN_EXTEND)
        return DAG.getNode(ISD::SIGN_EXTEND_INREG, dl, NVT, Res,
                           DAG.getValueType(N->getOperand(0).getValueType()));
      if (N->getOpcode() == ISD::ZERO_EXTEND)
        return DAG.getZeroExtendInReg(Res, dl, N->getOperand(0).getValueType());
      assert(N->getOpcode() == ISD::ANY_EXTEND && "Unknown integer extension!");
      return Res;
    }
  }

  // Otherwise, just extend the original operand all the way to the larger type.
  if (N->getNumOperands() != 1) {
    assert(N->getNumOperands() == 3 && "Unexpected number of operands!");
    assert(N->isVPOpcode() && "Expected VP opcode");
    return DAG.getNode(N->getOpcode(), dl, NVT, N->getOperand(0),
                       N->getOperand(1), N->getOperand(2));
  }
  return DAG.getNode(N->getOpcode(), dl, NVT, N->getOperand(0));
}

SDValue DAGTypeLegalizer::PromoteIntRes_LOAD(LoadSDNode *N) {
  assert(ISD::isUNINDEXEDLoad(N) && "Indexed load during type legalization!");
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  ISD::LoadExtType ExtType =
    ISD::isNON_EXTLoad(N) ? ISD::EXTLOAD : N->getExtensionType();
  SDLoc dl(N);
  SDValue Res = DAG.getExtLoad(ExtType, dl, NVT, N->getChain(), N->getBasePtr(),
                               N->getMemoryVT(), N->getMemOperand());

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  return Res;
}

SDValue DAGTypeLegalizer::PromoteIntRes_MLOAD(MaskedLoadSDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue ExtPassThru = GetPromotedInteger(N->getPassThru());

  ISD::LoadExtType ExtType = N->getExtensionType();
  if (ExtType == ISD::NON_EXTLOAD)
    ExtType = ISD::EXTLOAD;

  SDLoc dl(N);
  SDValue Res = DAG.getMaskedLoad(NVT, dl, N->getChain(), N->getBasePtr(),
                                  N->getOffset(), N->getMask(), ExtPassThru,
                                  N->getMemoryVT(), N->getMemOperand(),
                                  N->getAddressingMode(), ExtType,
                                  N->isExpandingLoad());
  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  return Res;
}

SDValue DAGTypeLegalizer::PromoteIntRes_MGATHER(MaskedGatherSDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue ExtPassThru = GetPromotedInteger(N->getPassThru());
  assert(NVT == ExtPassThru.getValueType() &&
      "Gather result type and the passThru argument type should be the same");

  ISD::LoadExtType ExtType = N->getExtensionType();
  if (ExtType == ISD::NON_EXTLOAD)
    ExtType = ISD::EXTLOAD;

  SDLoc dl(N);
  SDValue Ops[] = {N->getChain(), ExtPassThru, N->getMask(), N->getBasePtr(),
                   N->getIndex(), N->getScale() };
  SDValue Res = DAG.getMaskedGather(DAG.getVTList(NVT, MVT::Other),
                                    N->getMemoryVT(), dl, Ops,
                                    N->getMemOperand(), N->getIndexType(),
                                    ExtType);
  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  return Res;
}

SDValue DAGTypeLegalizer::PromoteIntRes_VECTOR_COMPRESS(SDNode *N) {
  SDValue Vec = GetPromotedInteger(N->getOperand(0));
  SDValue Passthru = GetPromotedInteger(N->getOperand(2));
  return DAG.getNode(ISD::VECTOR_COMPRESS, SDLoc(N), Vec.getValueType(), Vec,
                     N->getOperand(1), Passthru);
}

/// Promote the overflow flag of an overflowing arithmetic node.
SDValue DAGTypeLegalizer::PromoteIntRes_Overflow(SDNode *N) {
  // Change the return type of the boolean result while obeying
  // getSetCCResultType.
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(1));
  EVT VT = N->getValueType(0);
  EVT SVT = getSetCCResultType(VT);
  SDValue Ops[3] = { N->getOperand(0), N->getOperand(1) };
  unsigned NumOps = N->getNumOperands();
  assert(NumOps <= 3 && "Too many operands");
  if (NumOps == 3)
    Ops[2] = PromoteTargetBoolean(N->getOperand(2), VT);

  SDLoc dl(N);
  SDValue Res = DAG.getNode(N->getOpcode(), dl, DAG.getVTList(VT, SVT),
                            ArrayRef(Ops, NumOps));

  // Modified the sum result - switch anything that used the old sum to use
  // the new one.
  ReplaceValueWith(SDValue(N, 0), Res);

  // Convert to the expected type.
  return DAG.getBoolExtOrTrunc(Res.getValue(1), dl, NVT, VT);
}

template <class MatchContextClass>
SDValue DAGTypeLegalizer::PromoteIntRes_ADDSUBSHLSAT(SDNode *N) {
  // If the promoted type is legal, we can convert this to:
  //   1. ANY_EXTEND iN to iM
  //   2. SHL by M-N
  //   3. [US][ADD|SUB|SHL]SAT
  //   4. L/ASHR by M-N
  // Else it is more efficient to convert this to a min and a max
  // operation in the higher precision arithmetic.
  SDLoc dl(N);
  SDValue Op1 = N->getOperand(0);
  SDValue Op2 = N->getOperand(1);
  MatchContextClass matcher(DAG, TLI, N);
  unsigned OldBits = Op1.getScalarValueSizeInBits();

  unsigned Opcode = matcher.getRootBaseOpcode();
  bool IsShift = Opcode == ISD::USHLSAT || Opcode == ISD::SSHLSAT;

  // FIXME: We need vp-aware PromotedInteger functions.
  SDValue Op1Promoted, Op2Promoted;
  if (IsShift) {
    Op1Promoted = GetPromotedInteger(Op1);
    Op2Promoted = ZExtPromotedInteger(Op2);
  } else if (Opcode == ISD::UADDSAT || Opcode == ISD::USUBSAT) {
    Op1Promoted = ZExtPromotedInteger(Op1);
    Op2Promoted = ZExtPromotedInteger(Op2);
  } else {
    Op1Promoted = SExtPromotedInteger(Op1);
    Op2Promoted = SExtPromotedInteger(Op2);
  }
  EVT PromotedType = Op1Promoted.getValueType();
  unsigned NewBits = PromotedType.getScalarSizeInBits();

  if (Opcode == ISD::UADDSAT) {
    APInt MaxVal = APInt::getAllOnes(OldBits).zext(NewBits);
    SDValue SatMax = DAG.getConstant(MaxVal, dl, PromotedType);
    SDValue Add =
        matcher.getNode(ISD::ADD, dl, PromotedType, Op1Promoted, Op2Promoted);
    return matcher.getNode(ISD::UMIN, dl, PromotedType, Add, SatMax);
  }

  // USUBSAT can always be promoted as long as we have zero-extended the args.
  if (Opcode == ISD::USUBSAT)
    return matcher.getNode(ISD::USUBSAT, dl, PromotedType, Op1Promoted,
                           Op2Promoted);

  // Shift cannot use a min/max expansion, we can't detect overflow if all of
  // the bits have been shifted out.
  if (IsShift || matcher.isOperationLegal(Opcode, PromotedType)) {
    unsigned ShiftOp;
    switch (Opcode) {
    case ISD::SADDSAT:
    case ISD::SSUBSAT:
    case ISD::SSHLSAT:
      ShiftOp = ISD::SRA;
      break;
    case ISD::USHLSAT:
      ShiftOp = ISD::SRL;
      break;
    default:
      llvm_unreachable("Expected opcode to be signed or unsigned saturation "
                       "addition, subtraction or left shift");
    }

    unsigned SHLAmount = NewBits - OldBits;
    SDValue ShiftAmount =
        DAG.getShiftAmountConstant(SHLAmount, PromotedType, dl);
    Op1Promoted =
        DAG.getNode(ISD::SHL, dl, PromotedType, Op1Promoted, ShiftAmount);
    if (!IsShift)
      Op2Promoted =
          matcher.getNode(ISD::SHL, dl, PromotedType, Op2Promoted, ShiftAmount);

    SDValue Result =
        matcher.getNode(Opcode, dl, PromotedType, Op1Promoted, Op2Promoted);
    return matcher.getNode(ShiftOp, dl, PromotedType, Result, ShiftAmount);
  }

  unsigned AddOp = Opcode == ISD::SADDSAT ? ISD::ADD : ISD::SUB;
  APInt MinVal = APInt::getSignedMinValue(OldBits).sext(NewBits);
  APInt MaxVal = APInt::getSignedMaxValue(OldBits).sext(NewBits);
  SDValue SatMin = DAG.getConstant(MinVal, dl, PromotedType);
  SDValue SatMax = DAG.getConstant(MaxVal, dl, PromotedType);
  SDValue Result =
      matcher.getNode(AddOp, dl, PromotedType, Op1Promoted, Op2Promoted);
  Result = matcher.getNode(ISD::SMIN, dl, PromotedType, Result, SatMax);
  Result = matcher.getNode(ISD::SMAX, dl, PromotedType, Result, SatMin);
  return Result;
}

SDValue DAGTypeLegalizer::PromoteIntRes_MULFIX(SDNode *N) {
  // Can just promote the operands then continue with operation.
  SDLoc dl(N);
  SDValue Op1Promoted, Op2Promoted;
  bool Signed =
      N->getOpcode() == ISD::SMULFIX || N->getOpcode() == ISD::SMULFIXSAT;
  bool Saturating =
      N->getOpcode() == ISD::SMULFIXSAT || N->getOpcode() == ISD::UMULFIXSAT;
  if (Signed) {
    Op1Promoted = SExtPromotedInteger(N->getOperand(0));
    Op2Promoted = SExtPromotedInteger(N->getOperand(1));
  } else {
    Op1Promoted = ZExtPromotedInteger(N->getOperand(0));
    Op2Promoted = ZExtPromotedInteger(N->getOperand(1));
  }
  EVT OldType = N->getOperand(0).getValueType();
  EVT PromotedType = Op1Promoted.getValueType();
  unsigned DiffSize =
      PromotedType.getScalarSizeInBits() - OldType.getScalarSizeInBits();

  if (Saturating) {
    // Promoting the operand and result values changes the saturation width,
    // which is extends the values that we clamp to on saturation. This could be
    // resolved by shifting one of the operands the same amount, which would
    // also shift the result we compare against, then shifting back.
    Op1Promoted =
        DAG.getNode(ISD::SHL, dl, PromotedType, Op1Promoted,
                    DAG.getShiftAmountConstant(DiffSize, PromotedType, dl));
    SDValue Result = DAG.getNode(N->getOpcode(), dl, PromotedType, Op1Promoted,
                                 Op2Promoted, N->getOperand(2));
    unsigned ShiftOp = Signed ? ISD::SRA : ISD::SRL;
    return DAG.getNode(ShiftOp, dl, PromotedType, Result,
                       DAG.getShiftAmountConstant(DiffSize, PromotedType, dl));
  }
  return DAG.getNode(N->getOpcode(), dl, PromotedType, Op1Promoted, Op2Promoted,
                     N->getOperand(2));
}

static SDValue SaturateWidenedDIVFIX(SDValue V, SDLoc &dl,
                                     unsigned SatW, bool Signed,
                                     const TargetLowering &TLI,
                                     SelectionDAG &DAG) {
  EVT VT = V.getValueType();
  unsigned VTW = VT.getScalarSizeInBits();

  if (!Signed) {
    // Saturate to the unsigned maximum by getting the minimum of V and the
    // maximum.
    return DAG.getNode(ISD::UMIN, dl, VT, V,
                       DAG.getConstant(APInt::getLowBitsSet(VTW, SatW),
                                       dl, VT));
  }

  // Saturate to the signed maximum (the low SatW - 1 bits) by taking the
  // signed minimum of it and V.
  V = DAG.getNode(ISD::SMIN, dl, VT, V,
                  DAG.getConstant(APInt::getLowBitsSet(VTW, SatW - 1),
                                  dl, VT));
  // Saturate to the signed minimum (the high SatW + 1 bits) by taking the
  // signed maximum of it and V.
  V = DAG.getNode(ISD::SMAX, dl, VT, V,
                  DAG.getConstant(APInt::getHighBitsSet(VTW, VTW - SatW + 1),
                                  dl, VT));
  return V;
}

static SDValue earlyExpandDIVFIX(SDNode *N, SDValue LHS, SDValue RHS,
                                 unsigned Scale, const TargetLowering &TLI,
                                 SelectionDAG &DAG, unsigned SatW = 0) {
  EVT VT = LHS.getValueType();
  unsigned VTSize = VT.getScalarSizeInBits();
  bool Signed = N->getOpcode() == ISD::SDIVFIX ||
                N->getOpcode() == ISD::SDIVFIXSAT;
  bool Saturating = N->getOpcode() == ISD::SDIVFIXSAT ||
                    N->getOpcode() == ISD::UDIVFIXSAT;

  SDLoc dl(N);
  // Widen the types by a factor of two. This is guaranteed to expand, since it
  // will always have enough high bits in the LHS to shift into.
  EVT WideVT = EVT::getIntegerVT(*DAG.getContext(), VTSize * 2);
  if (VT.isVector())
    WideVT = EVT::getVectorVT(*DAG.getContext(), WideVT,
                              VT.getVectorElementCount());
  LHS = DAG.getExtOrTrunc(Signed, LHS, dl, WideVT);
  RHS = DAG.getExtOrTrunc(Signed, RHS, dl, WideVT);
  SDValue Res = TLI.expandFixedPointDiv(N->getOpcode(), dl, LHS, RHS, Scale,
                                        DAG);
  assert(Res && "Expanding DIVFIX with wide type failed?");
  if (Saturating) {
    // If the caller has told us to saturate at something less, use that width
    // instead of the type before doubling. However, it cannot be more than
    // what we just widened!
    assert(SatW <= VTSize &&
           "Tried to saturate to more than the original type?");
    Res = SaturateWidenedDIVFIX(Res, dl, SatW == 0 ? VTSize : SatW, Signed,
                                TLI, DAG);
  }
  return DAG.getZExtOrTrunc(Res, dl, VT);
}

SDValue DAGTypeLegalizer::PromoteIntRes_DIVFIX(SDNode *N) {
  SDLoc dl(N);
  SDValue Op1Promoted, Op2Promoted;
  bool Signed = N->getOpcode() == ISD::SDIVFIX ||
                N->getOpcode() == ISD::SDIVFIXSAT;
  bool Saturating = N->getOpcode() == ISD::SDIVFIXSAT ||
                    N->getOpcode() == ISD::UDIVFIXSAT;
  if (Signed) {
    Op1Promoted = SExtPromotedInteger(N->getOperand(0));
    Op2Promoted = SExtPromotedInteger(N->getOperand(1));
  } else {
    Op1Promoted = ZExtPromotedInteger(N->getOperand(0));
    Op2Promoted = ZExtPromotedInteger(N->getOperand(1));
  }
  EVT PromotedType = Op1Promoted.getValueType();
  unsigned Scale = N->getConstantOperandVal(2);

  // If the type is already legal and the operation is legal in that type, we
  // should not early expand.
  if (TLI.isTypeLegal(PromotedType)) {
    TargetLowering::LegalizeAction Action =
        TLI.getFixedPointOperationAction(N->getOpcode(), PromotedType, Scale);
    if (Action == TargetLowering::Legal || Action == TargetLowering::Custom) {
      unsigned Diff = PromotedType.getScalarSizeInBits() -
                      N->getValueType(0).getScalarSizeInBits();
      if (Saturating)
        Op1Promoted =
            DAG.getNode(ISD::SHL, dl, PromotedType, Op1Promoted,
                        DAG.getShiftAmountConstant(Diff, PromotedType, dl));
      SDValue Res = DAG.getNode(N->getOpcode(), dl, PromotedType, Op1Promoted,
                                Op2Promoted, N->getOperand(2));
      if (Saturating)
        Res = DAG.getNode(Signed ? ISD::SRA : ISD::SRL, dl, PromotedType, Res,
                          DAG.getShiftAmountConstant(Diff, PromotedType, dl));
      return Res;
    }
  }

  // See if we can perform the division in this type without expanding.
  if (SDValue Res = TLI.expandFixedPointDiv(N->getOpcode(), dl, Op1Promoted,
                                        Op2Promoted, Scale, DAG)) {
    if (Saturating)
      Res = SaturateWidenedDIVFIX(Res, dl,
                                  N->getValueType(0).getScalarSizeInBits(),
                                  Signed, TLI, DAG);
    return Res;
  }
  // If we cannot, expand it to twice the type width. If we are saturating, give
  // it the original width as a saturating width so we don't need to emit
  // two saturations.
  return earlyExpandDIVFIX(N, Op1Promoted, Op2Promoted, Scale, TLI, DAG,
                            N->getValueType(0).getScalarSizeInBits());
}

SDValue DAGTypeLegalizer::PromoteIntRes_SADDSUBO(SDNode *N, unsigned ResNo) {
  if (ResNo == 1)
    return PromoteIntRes_Overflow(N);

  // The operation overflowed iff the result in the larger type is not the
  // sign extension of its truncation to the original type.
  SDValue LHS = SExtPromotedInteger(N->getOperand(0));
  SDValue RHS = SExtPromotedInteger(N->getOperand(1));
  EVT OVT = N->getOperand(0).getValueType();
  EVT NVT = LHS.getValueType();
  SDLoc dl(N);

  // Do the arithmetic in the larger type.
  unsigned Opcode = N->getOpcode() == ISD::SADDO ? ISD::ADD : ISD::SUB;
  SDValue Res = DAG.getNode(Opcode, dl, NVT, LHS, RHS);

  // Calculate the overflow flag: sign extend the arithmetic result from
  // the original type.
  SDValue Ofl = DAG.getNode(ISD::SIGN_EXTEND_INREG, dl, NVT, Res,
                            DAG.getValueType(OVT));
  // Overflowed if and only if this is not equal to Res.
  Ofl = DAG.getSetCC(dl, N->getValueType(1), Ofl, Res, ISD::SETNE);

  // Use the calculated overflow everywhere.
  ReplaceValueWith(SDValue(N, 1), Ofl);

  return Res;
}

SDValue DAGTypeLegalizer::PromoteIntRes_CMP(SDNode *N) {
  EVT PromotedResultTy =
      TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  return DAG.getNode(N->getOpcode(), SDLoc(N), PromotedResultTy,
                     N->getOperand(0), N->getOperand(1));
}

SDValue DAGTypeLegalizer::PromoteIntRes_Select(SDNode *N) {
  SDValue Mask = N->getOperand(0);

  SDValue LHS = GetPromotedInteger(N->getOperand(1));
  SDValue RHS = GetPromotedInteger(N->getOperand(2));

  unsigned Opcode = N->getOpcode();
  if (Opcode == ISD::VP_SELECT || Opcode == ISD::VP_MERGE)
    return DAG.getNode(Opcode, SDLoc(N), LHS.getValueType(), Mask, LHS, RHS,
                       N->getOperand(3));
  return DAG.getNode(Opcode, SDLoc(N), LHS.getValueType(), Mask, LHS, RHS);
}

SDValue DAGTypeLegalizer::PromoteIntRes_SELECT_CC(SDNode *N) {
  SDValue LHS = GetPromotedInteger(N->getOperand(2));
  SDValue RHS = GetPromotedInteger(N->getOperand(3));
  return DAG.getNode(ISD::SELECT_CC, SDLoc(N),
                     LHS.getValueType(), N->getOperand(0),
                     N->getOperand(1), LHS, RHS, N->getOperand(4));
}

SDValue DAGTypeLegalizer::PromoteIntRes_SETCC(SDNode *N) {
  unsigned OpNo = N->isStrictFPOpcode() ? 1 : 0;
  EVT InVT = N->getOperand(OpNo).getValueType();
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));

  EVT SVT = getSetCCResultType(InVT);

  // If we got back a type that needs to be promoted, this likely means the
  // the input type also needs to be promoted. So get the promoted type for
  // the input and try the query again.
  if (getTypeAction(SVT) == TargetLowering::TypePromoteInteger) {
    if (getTypeAction(InVT) == TargetLowering::TypePromoteInteger) {
      InVT = TLI.getTypeToTransformTo(*DAG.getContext(), InVT);
      SVT = getSetCCResultType(InVT);
    } else {
      // Input type isn't promoted, just use the default promoted type.
      SVT = NVT;
    }
  }

  SDLoc dl(N);
  assert(SVT.isVector() == N->getOperand(OpNo).getValueType().isVector() &&
         "Vector compare must return a vector result!");

  // Get the SETCC result using the canonical SETCC type.
  SDValue SetCC;
  if (N->isStrictFPOpcode()) {
    SDVTList VTs = DAG.getVTList({SVT, MVT::Other});
    SDValue Opers[] = {N->getOperand(0), N->getOperand(1),
                       N->getOperand(2), N->getOperand(3)};
    SetCC = DAG.getNode(N->getOpcode(), dl, VTs, Opers, N->getFlags());
    // Legalize the chain result - switch anything that used the old chain to
    // use the new one.
    ReplaceValueWith(SDValue(N, 1), SetCC.getValue(1));
  } else
    SetCC = DAG.getNode(N->getOpcode(), dl, SVT, N->getOperand(0),
                        N->getOperand(1), N->getOperand(2), N->getFlags());

  // Convert to the expected type.
  return DAG.getSExtOrTrunc(SetCC, dl, NVT);
}

SDValue DAGTypeLegalizer::PromoteIntRes_IS_FPCLASS(SDNode *N) {
  SDLoc DL(N);
  SDValue Arg = N->getOperand(0);
  SDValue Test = N->getOperand(1);
  EVT NResVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  return DAG.getNode(ISD::IS_FPCLASS, DL, NResVT, Arg, Test);
}

SDValue DAGTypeLegalizer::PromoteIntRes_FFREXP(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(1));
  EVT VT = N->getValueType(0);

  SDLoc dl(N);
  SDValue Res =
      DAG.getNode(N->getOpcode(), dl, DAG.getVTList(VT, NVT), N->getOperand(0));

  ReplaceValueWith(SDValue(N, 0), Res);
  return Res.getValue(1);
}

SDValue DAGTypeLegalizer::PromoteIntRes_SHL(SDNode *N) {
  SDValue LHS = GetPromotedInteger(N->getOperand(0));
  SDValue RHS = N->getOperand(1);
  if (N->getOpcode() != ISD::VP_SHL) {
    if (getTypeAction(RHS.getValueType()) == TargetLowering::TypePromoteInteger)
      RHS = ZExtPromotedInteger(RHS);

    return DAG.getNode(N->getOpcode(), SDLoc(N), LHS.getValueType(), LHS, RHS);
  }

  SDValue Mask = N->getOperand(2);
  SDValue EVL = N->getOperand(3);
  if (getTypeAction(RHS.getValueType()) == TargetLowering::TypePromoteInteger)
    RHS = VPZExtPromotedInteger(RHS, Mask, EVL);
  return DAG.getNode(N->getOpcode(), SDLoc(N), LHS.getValueType(), LHS, RHS,
                     Mask, EVL);
}

SDValue DAGTypeLegalizer::PromoteIntRes_SIGN_EXTEND_INREG(SDNode *N) {
  SDValue Op = GetPromotedInteger(N->getOperand(0));
  return DAG.getNode(ISD::SIGN_EXTEND_INREG, SDLoc(N),
                     Op.getValueType(), Op, N->getOperand(1));
}

SDValue DAGTypeLegalizer::PromoteIntRes_SimpleIntBinOp(SDNode *N) {
  // The input may have strange things in the top bits of the registers, but
  // these operations don't care.  They may have weird bits going out, but
  // that too is okay if they are integer operations.
  SDValue LHS = GetPromotedInteger(N->getOperand(0));
  SDValue RHS = GetPromotedInteger(N->getOperand(1));
  if (N->getNumOperands() == 2)
    return DAG.getNode(N->getOpcode(), SDLoc(N), LHS.getValueType(), LHS, RHS);
  assert(N->getNumOperands() == 4 && "Unexpected number of operands!");
  assert(N->isVPOpcode() && "Expected VP opcode");
  return DAG.getNode(N->getOpcode(), SDLoc(N), LHS.getValueType(), LHS, RHS,
                     N->getOperand(2), N->getOperand(3));
}

SDValue DAGTypeLegalizer::PromoteIntRes_SExtIntBinOp(SDNode *N) {
  if (N->getNumOperands() == 2) {
    // Sign extend the input.
    SDValue LHS = SExtPromotedInteger(N->getOperand(0));
    SDValue RHS = SExtPromotedInteger(N->getOperand(1));
    return DAG.getNode(N->getOpcode(), SDLoc(N), LHS.getValueType(), LHS, RHS);
  }
  assert(N->getNumOperands() == 4 && "Unexpected number of operands!");
  assert(N->isVPOpcode() && "Expected VP opcode");
  SDValue Mask = N->getOperand(2);
  SDValue EVL = N->getOperand(3);
  // Sign extend the input.
  SDValue LHS = VPSExtPromotedInteger(N->getOperand(0), Mask, EVL);
  SDValue RHS = VPSExtPromotedInteger(N->getOperand(1), Mask, EVL);
  return DAG.getNode(N->getOpcode(), SDLoc(N), LHS.getValueType(), LHS, RHS,
                     Mask, EVL);
}

SDValue DAGTypeLegalizer::PromoteIntRes_ZExtIntBinOp(SDNode *N) {
  if (N->getNumOperands() == 2) {
    // Zero extend the input.
    SDValue LHS = ZExtPromotedInteger(N->getOperand(0));
    SDValue RHS = ZExtPromotedInteger(N->getOperand(1));
    return DAG.getNode(N->getOpcode(), SDLoc(N), LHS.getValueType(), LHS, RHS);
  }
  assert(N->getNumOperands() == 4 && "Unexpected number of operands!");
  assert(N->isVPOpcode() && "Expected VP opcode");
  // Zero extend the input.
  SDValue Mask = N->getOperand(2);
  SDValue EVL = N->getOperand(3);
  SDValue LHS = VPZExtPromotedInteger(N->getOperand(0), Mask, EVL);
  SDValue RHS = VPZExtPromotedInteger(N->getOperand(1), Mask, EVL);
  return DAG.getNode(N->getOpcode(), SDLoc(N), LHS.getValueType(), LHS, RHS,
                     Mask, EVL);
}

SDValue DAGTypeLegalizer::PromoteIntRes_UMINUMAX(SDNode *N) {
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  // It doesn't matter if we sign extend or zero extend in the inputs. So do
  // whatever is best for the target and the promoted operands.
  SExtOrZExtPromotedOperands(LHS, RHS);

  return DAG.getNode(N->getOpcode(), SDLoc(N),
                     LHS.getValueType(), LHS, RHS);
}

SDValue DAGTypeLegalizer::PromoteIntRes_SRA(SDNode *N) {
  SDValue RHS = N->getOperand(1);
  if (N->getOpcode() != ISD::VP_SRA) {
    // The input value must be properly sign extended.
    SDValue LHS = SExtPromotedInteger(N->getOperand(0));
    if (getTypeAction(RHS.getValueType()) == TargetLowering::TypePromoteInteger)
      RHS = ZExtPromotedInteger(RHS);
    return DAG.getNode(N->getOpcode(), SDLoc(N), LHS.getValueType(), LHS, RHS);
  }

  SDValue Mask = N->getOperand(2);
  SDValue EVL = N->getOperand(3);
  // The input value must be properly sign extended.
  SDValue LHS = VPSExtPromotedInteger(N->getOperand(0), Mask, EVL);
  if (getTypeAction(RHS.getValueType()) == TargetLowering::TypePromoteInteger)
    RHS = VPZExtPromotedInteger(RHS, Mask, EVL);
  return DAG.getNode(N->getOpcode(), SDLoc(N), LHS.getValueType(), LHS, RHS,
                     Mask, EVL);
}

SDValue DAGTypeLegalizer::PromoteIntRes_SRL(SDNode *N) {
  SDValue RHS = N->getOperand(1);
  if (N->getOpcode() != ISD::VP_SRL) {
    // The input value must be properly zero extended.
    SDValue LHS = ZExtPromotedInteger(N->getOperand(0));
    if (getTypeAction(RHS.getValueType()) == TargetLowering::TypePromoteInteger)
      RHS = ZExtPromotedInteger(RHS);
    return DAG.getNode(N->getOpcode(), SDLoc(N), LHS.getValueType(), LHS, RHS);
  }

  SDValue Mask = N->getOperand(2);
  SDValue EVL = N->getOperand(3);
  // The input value must be properly zero extended.
  SDValue LHS = VPZExtPromotedInteger(N->getOperand(0), Mask, EVL);
  if (getTypeAction(RHS.getValueType()) == TargetLowering::TypePromoteInteger)
    RHS = VPZExtPromotedInteger(RHS, Mask, EVL);
  return DAG.getNode(N->getOpcode(), SDLoc(N), LHS.getValueType(), LHS, RHS,
                     Mask, EVL);
}

SDValue DAGTypeLegalizer::PromoteIntRes_Rotate(SDNode *N) {
  // Lower the rotate to shifts and ORs which can be promoted.
  SDValue Res = TLI.expandROT(N, true /*AllowVectorOps*/, DAG);
  ReplaceValueWith(SDValue(N, 0), Res);
  return SDValue();
}

SDValue DAGTypeLegalizer::PromoteIntRes_FunnelShift(SDNode *N) {
  SDValue Hi = GetPromotedInteger(N->getOperand(0));
  SDValue Lo = GetPromotedInteger(N->getOperand(1));
  SDValue Amt = N->getOperand(2);
  if (getTypeAction(Amt.getValueType()) == TargetLowering::TypePromoteInteger)
    Amt = ZExtPromotedInteger(Amt);
  EVT AmtVT = Amt.getValueType();

  SDLoc DL(N);
  EVT OldVT = N->getOperand(0).getValueType();
  EVT VT = Lo.getValueType();
  unsigned Opcode = N->getOpcode();
  bool IsFSHR = Opcode == ISD::FSHR;
  unsigned OldBits = OldVT.getScalarSizeInBits();
  unsigned NewBits = VT.getScalarSizeInBits();

  // Amount has to be interpreted modulo the old bit width.
  Amt = DAG.getNode(ISD::UREM, DL, AmtVT, Amt,
                    DAG.getConstant(OldBits, DL, AmtVT));

  // If the promoted type is twice the size (or more), then we use the
  // traditional funnel 'double' shift codegen. This isn't necessary if the
  // shift amount is constant.
  // fshl(x,y,z) -> (((aext(x) << bw) | zext(y)) << (z % bw)) >> bw.
  // fshr(x,y,z) -> (((aext(x) << bw) | zext(y)) >> (z % bw)).
  if (NewBits >= (2 * OldBits) && !isa<ConstantSDNode>(Amt) &&
      !TLI.isOperationLegalOrCustom(Opcode, VT)) {
    SDValue HiShift = DAG.getConstant(OldBits, DL, VT);
    Hi = DAG.getNode(ISD::SHL, DL, VT, Hi, HiShift);
    Lo = DAG.getZeroExtendInReg(Lo, DL, OldVT);
    SDValue Res = DAG.getNode(ISD::OR, DL, VT, Hi, Lo);
    Res = DAG.getNode(IsFSHR ? ISD::SRL : ISD::SHL, DL, VT, Res, Amt);
    if (!IsFSHR)
      Res = DAG.getNode(ISD::SRL, DL, VT, Res, HiShift);
    return Res;
  }

  // Shift Lo up to occupy the upper bits of the promoted type.
  SDValue ShiftOffset = DAG.getConstant(NewBits - OldBits, DL, AmtVT);
  Lo = DAG.getNode(ISD::SHL, DL, VT, Lo, ShiftOffset);

  // Increase Amount to shift the result into the lower bits of the promoted
  // type.
  if (IsFSHR)
    Amt = DAG.getNode(ISD::ADD, DL, AmtVT, Amt, ShiftOffset);

  return DAG.getNode(Opcode, DL, VT, Hi, Lo, Amt);
}

// A vp version of PromoteIntRes_FunnelShift.
SDValue DAGTypeLegalizer::PromoteIntRes_VPFunnelShift(SDNode *N) {
  SDValue Hi = GetPromotedInteger(N->getOperand(0));
  SDValue Lo = GetPromotedInteger(N->getOperand(1));
  SDValue Amt = N->getOperand(2);
  SDValue Mask = N->getOperand(3);
  SDValue EVL = N->getOperand(4);
  if (getTypeAction(Amt.getValueType()) == TargetLowering::TypePromoteInteger)
    Amt = VPZExtPromotedInteger(Amt, Mask, EVL);
  EVT AmtVT = Amt.getValueType();

  SDLoc DL(N);
  EVT OldVT = N->getOperand(0).getValueType();
  EVT VT = Lo.getValueType();
  unsigned Opcode = N->getOpcode();
  bool IsFSHR = Opcode == ISD::VP_FSHR;
  unsigned OldBits = OldVT.getScalarSizeInBits();
  unsigned NewBits = VT.getScalarSizeInBits();

  // Amount has to be interpreted modulo the old bit width.
  Amt = DAG.getNode(ISD::VP_UREM, DL, AmtVT, Amt,
                    DAG.getConstant(OldBits, DL, AmtVT), Mask, EVL);

  // If the promoted type is twice the size (or more), then we use the
  // traditional funnel 'double' shift codegen. This isn't necessary if the
  // shift amount is constant.
  // fshl(x,y,z) -> (((aext(x) << bw) | zext(y)) << (z % bw)) >> bw.
  // fshr(x,y,z) -> (((aext(x) << bw) | zext(y)) >> (z % bw)).
  if (NewBits >= (2 * OldBits) && !isa<ConstantSDNode>(Amt) &&
      !TLI.isOperationLegalOrCustom(Opcode, VT)) {
    SDValue HiShift = DAG.getConstant(OldBits, DL, VT);
    Hi = DAG.getNode(ISD::VP_SHL, DL, VT, Hi, HiShift, Mask, EVL);
    Lo = DAG.getVPZeroExtendInReg(Lo, Mask, EVL, DL, OldVT);
    SDValue Res = DAG.getNode(ISD::VP_OR, DL, VT, Hi, Lo, Mask, EVL);
    Res = DAG.getNode(IsFSHR ? ISD::VP_SRL : ISD::VP_SHL, DL, VT, Res, Amt,
                      Mask, EVL);
    if (!IsFSHR)
      Res = DAG.getNode(ISD::VP_SRL, DL, VT, Res, HiShift, Mask, EVL);
    return Res;
  }

  // Shift Lo up to occupy the upper bits of the promoted type.
  SDValue ShiftOffset = DAG.getConstant(NewBits - OldBits, DL, AmtVT);
  Lo = DAG.getNode(ISD::VP_SHL, DL, VT, Lo, ShiftOffset, Mask, EVL);

  // Increase Amount to shift the result into the lower bits of the promoted
  // type.
  if (IsFSHR)
    Amt = DAG.getNode(ISD::VP_ADD, DL, AmtVT, Amt, ShiftOffset, Mask, EVL);

  return DAG.getNode(Opcode, DL, VT, Hi, Lo, Amt, Mask, EVL);
}

SDValue DAGTypeLegalizer::PromoteIntRes_TRUNCATE(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Res;
  SDValue InOp = N->getOperand(0);
  SDLoc dl(N);

  switch (getTypeAction(InOp.getValueType())) {
  default: llvm_unreachable("Unknown type action!");
  case TargetLowering::TypeLegal:
  case TargetLowering::TypeExpandInteger:
    Res = InOp;
    break;
  case TargetLowering::TypePromoteInteger:
    Res = GetPromotedInteger(InOp);
    break;
  case TargetLowering::TypeSplitVector: {
    EVT InVT = InOp.getValueType();
    assert(InVT.isVector() && "Cannot split scalar types");
    ElementCount NumElts = InVT.getVectorElementCount();
    assert(NumElts == NVT.getVectorElementCount() &&
           "Dst and Src must have the same number of elements");
    assert(isPowerOf2_32(NumElts.getKnownMinValue()) &&
           "Promoted vector type must be a power of two");

    SDValue EOp1, EOp2;
    GetSplitVector(InOp, EOp1, EOp2);

    EVT HalfNVT = EVT::getVectorVT(*DAG.getContext(), NVT.getScalarType(),
                                   NumElts.divideCoefficientBy(2));
    if (N->getOpcode() == ISD::TRUNCATE) {
      EOp1 = DAG.getNode(ISD::TRUNCATE, dl, HalfNVT, EOp1);
      EOp2 = DAG.getNode(ISD::TRUNCATE, dl, HalfNVT, EOp2);
    } else {
      assert(N->getOpcode() == ISD::VP_TRUNCATE &&
             "Expected VP_TRUNCATE opcode");
      SDValue MaskLo, MaskHi, EVLLo, EVLHi;
      std::tie(MaskLo, MaskHi) = SplitMask(N->getOperand(1));
      std::tie(EVLLo, EVLHi) =
          DAG.SplitEVL(N->getOperand(2), N->getValueType(0), dl);
      EOp1 = DAG.getNode(ISD::VP_TRUNCATE, dl, HalfNVT, EOp1, MaskLo, EVLLo);
      EOp2 = DAG.getNode(ISD::VP_TRUNCATE, dl, HalfNVT, EOp2, MaskHi, EVLHi);
    }
    return DAG.getNode(ISD::CONCAT_VECTORS, dl, NVT, EOp1, EOp2);
  }
  // TODO: VP_TRUNCATE need to handle when TypeWidenVector access to some
  // targets.
  case TargetLowering::TypeWidenVector: {
    SDValue WideInOp = GetWidenedVector(InOp);

    // Truncate widened InOp.
    unsigned NumElem = WideInOp.getValueType().getVectorNumElements();
    EVT TruncVT = EVT::getVectorVT(*DAG.getContext(),
                                   N->getValueType(0).getScalarType(), NumElem);
    SDValue WideTrunc = DAG.getNode(ISD::TRUNCATE, dl, TruncVT, WideInOp);

    // Zero extend so that the elements are of same type as those of NVT
    EVT ExtVT = EVT::getVectorVT(*DAG.getContext(), NVT.getVectorElementType(),
                                 NumElem);
    SDValue WideExt = DAG.getNode(ISD::ZERO_EXTEND, dl, ExtVT, WideTrunc);

    // Extract the low NVT subvector.
    SDValue ZeroIdx = DAG.getVectorIdxConstant(0, dl);
    return DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, NVT, WideExt, ZeroIdx);
  }
  }

  // Truncate to NVT instead of VT
  if (N->getOpcode() == ISD::VP_TRUNCATE)
    return DAG.getNode(ISD::VP_TRUNCATE, dl, NVT, Res, N->getOperand(1),
                       N->getOperand(2));
  return DAG.getNode(ISD::TRUNCATE, dl, NVT, Res);
}

SDValue DAGTypeLegalizer::PromoteIntRes_UADDSUBO(SDNode *N, unsigned ResNo) {
  if (ResNo == 1)
    return PromoteIntRes_Overflow(N);

  // The operation overflowed iff the result in the larger type is not the
  // zero extension of its truncation to the original type.
  SDValue LHS = ZExtPromotedInteger(N->getOperand(0));
  SDValue RHS = ZExtPromotedInteger(N->getOperand(1));
  EVT OVT = N->getOperand(0).getValueType();
  EVT NVT = LHS.getValueType();
  SDLoc dl(N);

  // Do the arithmetic in the larger type.
  unsigned Opcode = N->getOpcode() == ISD::UADDO ? ISD::ADD : ISD::SUB;
  SDValue Res = DAG.getNode(Opcode, dl, NVT, LHS, RHS);

  // Calculate the overflow flag: zero extend the arithmetic result from
  // the original type.
  SDValue Ofl = DAG.getZeroExtendInReg(Res, dl, OVT);
  // Overflowed if and only if this is not equal to Res.
  Ofl = DAG.getSetCC(dl, N->getValueType(1), Ofl, Res, ISD::SETNE);

  // Use the calculated overflow everywhere.
  ReplaceValueWith(SDValue(N, 1), Ofl);

  return Res;
}

// Handle promotion for the ADDE/SUBE/UADDO_CARRY/USUBO_CARRY nodes. Notice that
// the third operand of ADDE/SUBE nodes is carry flag, which differs from
// the UADDO_CARRY/USUBO_CARRY nodes in that the third operand is carry Boolean.
SDValue DAGTypeLegalizer::PromoteIntRes_UADDSUBO_CARRY(SDNode *N,
                                                       unsigned ResNo) {
  if (ResNo == 1)
    return PromoteIntRes_Overflow(N);

  // We need to sign-extend the operands so the carry value computed by the
  // wide operation will be equivalent to the carry value computed by the
  // narrow operation.
  // An UADDO_CARRY can generate carry only if any of the operands has its
  // most significant bit set. Sign extension propagates the most significant
  // bit into the higher bits which means the extra bit that the narrow
  // addition would need (i.e. the carry) will be propagated through the higher
  // bits of the wide addition.
  // A USUBO_CARRY can generate borrow only if LHS < RHS and this property will
  // be preserved by sign extension.
  SDValue LHS = SExtPromotedInteger(N->getOperand(0));
  SDValue RHS = SExtPromotedInteger(N->getOperand(1));

  EVT ValueVTs[] = {LHS.getValueType(), N->getValueType(1)};

  // Do the arithmetic in the wide type.
  SDValue Res = DAG.getNode(N->getOpcode(), SDLoc(N), DAG.getVTList(ValueVTs),
                            LHS, RHS, N->getOperand(2));

  // Update the users of the original carry/borrow value.
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));

  return SDValue(Res.getNode(), 0);
}

SDValue DAGTypeLegalizer::PromoteIntRes_SADDSUBO_CARRY(SDNode *N,
                                                       unsigned ResNo) {
  assert(ResNo == 1 && "Don't know how to promote other results yet.");
  return PromoteIntRes_Overflow(N);
}

SDValue DAGTypeLegalizer::PromoteIntRes_ABS(SDNode *N) {
  EVT OVT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), OVT);

  // If a larger ABS or SMAX isn't supported by the target, try to expand now.
  // If we expand later we'll end up sign extending more than just the sra input
  // in sra+xor+sub expansion.
  if (!OVT.isVector() &&
      !TLI.isOperationLegalOrCustomOrPromote(ISD::ABS, NVT) &&
      !TLI.isOperationLegal(ISD::SMAX, NVT)) {
    if (SDValue Res = TLI.expandABS(N, DAG))
      return DAG.getNode(ISD::ANY_EXTEND, SDLoc(N), NVT, Res);
  }

  SDValue Op0 = SExtPromotedInteger(N->getOperand(0));
  return DAG.getNode(ISD::ABS, SDLoc(N), Op0.getValueType(), Op0);
}

SDValue DAGTypeLegalizer::PromoteIntRes_XMULO(SDNode *N, unsigned ResNo) {
  // Promote the overflow bit trivially.
  if (ResNo == 1)
    return PromoteIntRes_Overflow(N);

  SDValue LHS = N->getOperand(0), RHS = N->getOperand(1);
  SDLoc DL(N);
  EVT SmallVT = LHS.getValueType();

  // To determine if the result overflowed in a larger type, we extend the
  // input to the larger type, do the multiply (checking if it overflows),
  // then also check the high bits of the result to see if overflow happened
  // there.
  if (N->getOpcode() == ISD::SMULO) {
    LHS = SExtPromotedInteger(LHS);
    RHS = SExtPromotedInteger(RHS);
  } else {
    LHS = ZExtPromotedInteger(LHS);
    RHS = ZExtPromotedInteger(RHS);
  }
  SDVTList VTs = DAG.getVTList(LHS.getValueType(), N->getValueType(1));
  SDValue Mul = DAG.getNode(N->getOpcode(), DL, VTs, LHS, RHS);

  // Overflow occurred if it occurred in the larger type, or if the high part
  // of the result does not zero/sign-extend the low part.  Check this second
  // possibility first.
  SDValue Overflow;
  if (N->getOpcode() == ISD::UMULO) {
    // Unsigned overflow occurred if the high part is non-zero.
    unsigned Shift = SmallVT.getScalarSizeInBits();
    SDValue Hi =
        DAG.getNode(ISD::SRL, DL, Mul.getValueType(), Mul,
                    DAG.getShiftAmountConstant(Shift, Mul.getValueType(), DL));
    Overflow = DAG.getSetCC(DL, N->getValueType(1), Hi,
                            DAG.getConstant(0, DL, Hi.getValueType()),
                            ISD::SETNE);
  } else {
    // Signed overflow occurred if the high part does not sign extend the low.
    SDValue SExt = DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, Mul.getValueType(),
                               Mul, DAG.getValueType(SmallVT));
    Overflow = DAG.getSetCC(DL, N->getValueType(1), SExt, Mul, ISD::SETNE);
  }

  // The only other way for overflow to occur is if the multiplication in the
  // larger type itself overflowed.
  Overflow = DAG.getNode(ISD::OR, DL, N->getValueType(1), Overflow,
                         SDValue(Mul.getNode(), 1));

  // Use the calculated overflow everywhere.
  ReplaceValueWith(SDValue(N, 1), Overflow);
  return Mul;
}

SDValue DAGTypeLegalizer::PromoteIntRes_UNDEF(SDNode *N) {
  return DAG.getUNDEF(TLI.getTypeToTransformTo(*DAG.getContext(),
                                               N->getValueType(0)));
}

SDValue DAGTypeLegalizer::PromoteIntRes_VSCALE(SDNode *N) {
  EVT VT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));

  const APInt &MulImm = N->getConstantOperandAPInt(0);
  return DAG.getVScale(SDLoc(N), VT, MulImm.sext(VT.getSizeInBits()));
}

SDValue DAGTypeLegalizer::PromoteIntRes_VAARG(SDNode *N) {
  SDValue Chain = N->getOperand(0); // Get the chain.
  SDValue Ptr = N->getOperand(1); // Get the pointer.
  EVT VT = N->getValueType(0);
  SDLoc dl(N);

  MVT RegVT = TLI.getRegisterType(*DAG.getContext(), VT);
  unsigned NumRegs = TLI.getNumRegisters(*DAG.getContext(), VT);
  // The argument is passed as NumRegs registers of type RegVT.

  SmallVector<SDValue, 8> Parts(NumRegs);
  for (unsigned i = 0; i < NumRegs; ++i) {
    Parts[i] = DAG.getVAArg(RegVT, dl, Chain, Ptr, N->getOperand(2),
                            N->getConstantOperandVal(3));
    Chain = Parts[i].getValue(1);
  }

  // Handle endianness of the load.
  if (DAG.getDataLayout().isBigEndian())
    std::reverse(Parts.begin(), Parts.end());

  // Assemble the parts in the promoted type.
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Res = DAG.getNode(ISD::ZERO_EXTEND, dl, NVT, Parts[0]);
  for (unsigned i = 1; i < NumRegs; ++i) {
    SDValue Part = DAG.getNode(ISD::ZERO_EXTEND, dl, NVT, Parts[i]);
    // Shift it to the right position and "or" it in.
    Part = DAG.getNode(ISD::SHL, dl, NVT, Part,
                       DAG.getConstant(i * RegVT.getSizeInBits(), dl,
                                       TLI.getPointerTy(DAG.getDataLayout())));
    Res = DAG.getNode(ISD::OR, dl, NVT, Res, Part);
  }

  // Modified the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Chain);

  return Res;
}

//===----------------------------------------------------------------------===//
//  Integer Operand Promotion
//===----------------------------------------------------------------------===//

/// PromoteIntegerOperand - This method is called when the specified operand of
/// the specified node is found to need promotion.  At this point, all of the
/// result types of the node are known to be legal, but other operands of the
/// node may need promotion or expansion as well as the specified one.
bool DAGTypeLegalizer::PromoteIntegerOperand(SDNode *N, unsigned OpNo) {
  LLVM_DEBUG(dbgs() << "Promote integer operand: "; N->dump(&DAG));
  SDValue Res = SDValue();
  if (CustomLowerNode(N, N->getOperand(OpNo).getValueType(), false)) {
    LLVM_DEBUG(dbgs() << "Node has been custom lowered, done\n");
    return false;
  }

  switch (N->getOpcode()) {
    default:
  #ifndef NDEBUG
    dbgs() << "PromoteIntegerOperand Op #" << OpNo << ": ";
    N->dump(&DAG); dbgs() << "\n";
  #endif
    report_fatal_error("Do not know how to promote this operator's operand!");

  case ISD::ANY_EXTEND:   Res = PromoteIntOp_ANY_EXTEND(N); break;
  case ISD::ATOMIC_STORE:
    Res = PromoteIntOp_ATOMIC_STORE(cast<AtomicSDNode>(N));
    break;
  case ISD::BITCAST:      Res = PromoteIntOp_BITCAST(N); break;
  case ISD::BR_CC:        Res = PromoteIntOp_BR_CC(N, OpNo); break;
  case ISD::BRCOND:       Res = PromoteIntOp_BRCOND(N, OpNo); break;
  case ISD::BUILD_PAIR:   Res = PromoteIntOp_BUILD_PAIR(N); break;
  case ISD::BUILD_VECTOR: Res = PromoteIntOp_BUILD_VECTOR(N); break;
  case ISD::CONCAT_VECTORS: Res = PromoteIntOp_CONCAT_VECTORS(N); break;
  case ISD::EXTRACT_VECTOR_ELT: Res = PromoteIntOp_EXTRACT_VECTOR_ELT(N); break;
  case ISD::INSERT_VECTOR_ELT:
    Res = PromoteIntOp_INSERT_VECTOR_ELT(N, OpNo);
    break;
  case ISD::SPLAT_VECTOR:
  case ISD::SCALAR_TO_VECTOR:
  case ISD::EXPERIMENTAL_VP_SPLAT:
    Res = PromoteIntOp_ScalarOp(N);
    break;
  case ISD::VSELECT:
  case ISD::SELECT:       Res = PromoteIntOp_SELECT(N, OpNo); break;
  case ISD::SELECT_CC:    Res = PromoteIntOp_SELECT_CC(N, OpNo); break;
  case ISD::VP_SETCC:
  case ISD::SETCC:        Res = PromoteIntOp_SETCC(N, OpNo); break;
  case ISD::SIGN_EXTEND:  Res = PromoteIntOp_SIGN_EXTEND(N); break;
  case ISD::VP_SIGN_EXTEND: Res = PromoteIntOp_VP_SIGN_EXTEND(N); break;
  case ISD::VP_SINT_TO_FP:
  case ISD::SINT_TO_FP:   Res = PromoteIntOp_SINT_TO_FP(N); break;
  case ISD::STRICT_SINT_TO_FP: Res = PromoteIntOp_STRICT_SINT_TO_FP(N); break;
  case ISD::STORE:        Res = PromoteIntOp_STORE(cast<StoreSDNode>(N),
                                                   OpNo); break;
  case ISD::MSTORE:       Res = PromoteIntOp_MSTORE(cast<MaskedStoreSDNode>(N),
                                                    OpNo); break;
  case ISD::MLOAD:        Res = PromoteIntOp_MLOAD(cast<MaskedLoadSDNode>(N),
                                                    OpNo); break;
  case ISD::MGATHER:  Res = PromoteIntOp_MGATHER(cast<MaskedGatherSDNode>(N),
                                                 OpNo); break;
  case ISD::MSCATTER: Res = PromoteIntOp_MSCATTER(cast<MaskedScatterSDNode>(N),
                                                  OpNo); break;
  case ISD::VECTOR_COMPRESS:
    Res = PromoteIntOp_VECTOR_COMPRESS(N, OpNo);
    break;
  case ISD::VP_TRUNCATE:
  case ISD::TRUNCATE:     Res = PromoteIntOp_TRUNCATE(N); break;
  case ISD::BF16_TO_FP:
  case ISD::FP16_TO_FP:
  case ISD::VP_UINT_TO_FP:
  case ISD::UINT_TO_FP:   Res = PromoteIntOp_UINT_TO_FP(N); break;
  case ISD::STRICT_FP16_TO_FP:
  case ISD::STRICT_UINT_TO_FP:  Res = PromoteIntOp_STRICT_UINT_TO_FP(N); break;
  case ISD::ZERO_EXTEND:  Res = PromoteIntOp_ZERO_EXTEND(N); break;
  case ISD::VP_ZERO_EXTEND: Res = PromoteIntOp_VP_ZERO_EXTEND(N); break;
  case ISD::EXTRACT_SUBVECTOR: Res = PromoteIntOp_EXTRACT_SUBVECTOR(N); break;
  case ISD::INSERT_SUBVECTOR: Res = PromoteIntOp_INSERT_SUBVECTOR(N); break;

  case ISD::SHL:
  case ISD::SRA:
  case ISD::SRL:
  case ISD::ROTL:
  case ISD::ROTR: Res = PromoteIntOp_Shift(N); break;

  case ISD::SCMP:
  case ISD::UCMP: Res = PromoteIntOp_CMP(N); break;

  case ISD::FSHL:
  case ISD::FSHR: Res = PromoteIntOp_FunnelShift(N); break;

  case ISD::FRAMEADDR:
  case ISD::RETURNADDR: Res = PromoteIntOp_FRAMERETURNADDR(N); break;

  case ISD::SMULFIX:
  case ISD::SMULFIXSAT:
  case ISD::UMULFIX:
  case ISD::UMULFIXSAT:
  case ISD::SDIVFIX:
  case ISD::SDIVFIXSAT:
  case ISD::UDIVFIX:
  case ISD::UDIVFIXSAT: Res = PromoteIntOp_FIX(N); break;
  case ISD::FPOWI:
  case ISD::STRICT_FPOWI:
  case ISD::FLDEXP:
  case ISD::STRICT_FLDEXP: Res = PromoteIntOp_ExpOp(N); break;
  case ISD::VECREDUCE_ADD:
  case ISD::VECREDUCE_MUL:
  case ISD::VECREDUCE_AND:
  case ISD::VECREDUCE_OR:
  case ISD::VECREDUCE_XOR:
  case ISD::VECREDUCE_SMAX:
  case ISD::VECREDUCE_SMIN:
  case ISD::VECREDUCE_UMAX:
  case ISD::VECREDUCE_UMIN: Res = PromoteIntOp_VECREDUCE(N); break;
  case ISD::VP_REDUCE_ADD:
  case ISD::VP_REDUCE_MUL:
  case ISD::VP_REDUCE_AND:
  case ISD::VP_REDUCE_OR:
  case ISD::VP_REDUCE_XOR:
  case ISD::VP_REDUCE_SMAX:
  case ISD::VP_REDUCE_SMIN:
  case ISD::VP_REDUCE_UMAX:
  case ISD::VP_REDUCE_UMIN:
    Res = PromoteIntOp_VP_REDUCE(N, OpNo);
    break;

  case ISD::SET_ROUNDING: Res = PromoteIntOp_SET_ROUNDING(N); break;
  case ISD::STACKMAP:
    Res = PromoteIntOp_STACKMAP(N, OpNo);
    break;
  case ISD::PATCHPOINT:
    Res = PromoteIntOp_PATCHPOINT(N, OpNo);
    break;
  case ISD::EXPERIMENTAL_VP_STRIDED_LOAD:
  case ISD::EXPERIMENTAL_VP_STRIDED_STORE:
    Res = PromoteIntOp_VP_STRIDED(N, OpNo);
    break;
  case ISD::EXPERIMENTAL_VP_SPLICE:
    Res = PromoteIntOp_VP_SPLICE(N, OpNo);
    break;
  }

  // If the result is null, the sub-method took care of registering results etc.
  if (!Res.getNode()) return false;

  // If the result is N, the sub-method updated N in place.  Tell the legalizer
  // core about this.
  if (Res.getNode() == N)
    return true;

  const bool IsStrictFp = N->isStrictFPOpcode();
  assert(Res.getValueType() == N->getValueType(0) &&
         N->getNumValues() == (IsStrictFp ? 2 : 1) &&
         "Invalid operand expansion");
  LLVM_DEBUG(dbgs() << "Replacing: "; N->dump(&DAG); dbgs() << "     with: ";
             Res.dump());

  ReplaceValueWith(SDValue(N, 0), Res);
  if (IsStrictFp)
    ReplaceValueWith(SDValue(N, 1), SDValue(Res.getNode(), 1));

  return false;
}

// These operands can be either sign extended or zero extended as long as we
// treat them the same. If an extension is free, choose that. Otherwise, follow
// target preference.
void DAGTypeLegalizer::SExtOrZExtPromotedOperands(SDValue &LHS, SDValue &RHS) {
  SDValue OpL = GetPromotedInteger(LHS);
  SDValue OpR = GetPromotedInteger(RHS);

  if (TLI.isSExtCheaperThanZExt(LHS.getValueType(), OpL.getValueType())) {
    // The target would prefer to promote the comparison operand with sign
    // extension. Honor that unless the promoted values are already zero
    // extended.
    unsigned OpLEffectiveBits =
        DAG.computeKnownBits(OpL).countMaxActiveBits();
    unsigned OpREffectiveBits =
        DAG.computeKnownBits(OpR).countMaxActiveBits();
    if (OpLEffectiveBits <= LHS.getScalarValueSizeInBits() &&
        OpREffectiveBits <= RHS.getScalarValueSizeInBits()) {
      LHS = OpL;
      RHS = OpR;
      return;
    }

    // The promoted values aren't zero extended, use a sext_inreg.
    LHS = SExtPromotedInteger(LHS);
    RHS = SExtPromotedInteger(RHS);
    return;
  }

  // Prefer to promote the comparison operand with zero extension.

  // If the width of OpL/OpR excluding the duplicated sign bits is no greater
  // than the width of LHS/RHS, we can avoid/ inserting a zext_inreg operation
  // that we might not be able to remove.
  unsigned OpLEffectiveBits = DAG.ComputeMaxSignificantBits(OpL);
  unsigned OpREffectiveBits = DAG.ComputeMaxSignificantBits(OpR);
  if (OpLEffectiveBits <= LHS.getScalarValueSizeInBits() &&
      OpREffectiveBits <= RHS.getScalarValueSizeInBits()) {
    LHS = OpL;
    RHS = OpR;
    return;
  }

  // Otherwise, use zext_inreg.
  LHS = ZExtPromotedInteger(LHS);
  RHS = ZExtPromotedInteger(RHS);
}

/// PromoteSetCCOperands - Promote the operands of a comparison.  This code is
/// shared among BR_CC, SELECT_CC, and SETCC handlers.
void DAGTypeLegalizer::PromoteSetCCOperands(SDValue &LHS, SDValue &RHS,
                                            ISD::CondCode CCCode) {
  // We have to insert explicit sign or zero extends. Note that we could
  // insert sign extends for ALL conditions. For those operations where either
  // zero or sign extension would be valid, we ask the target which extension
  // it would prefer.

  // Signed comparisons always require sign extension.
  if (ISD::isSignedIntSetCC(CCCode)) {
    LHS = SExtPromotedInteger(LHS);
    RHS = SExtPromotedInteger(RHS);
    return;
  }

  assert((ISD::isUnsignedIntSetCC(CCCode) || ISD::isIntEqualitySetCC(CCCode)) &&
         "Unknown integer comparison!");

  SExtOrZExtPromotedOperands(LHS, RHS);
}

SDValue DAGTypeLegalizer::PromoteIntOp_ANY_EXTEND(SDNode *N) {
  SDValue Op = GetPromotedInteger(N->getOperand(0));
  return DAG.getNode(ISD::ANY_EXTEND, SDLoc(N), N->getValueType(0), Op);
}

SDValue DAGTypeLegalizer::PromoteIntOp_ATOMIC_STORE(AtomicSDNode *N) {
  SDValue Op1 = GetPromotedInteger(N->getOperand(1));
  return DAG.getAtomic(N->getOpcode(), SDLoc(N), N->getMemoryVT(),
                       N->getChain(), Op1, N->getBasePtr(), N->getMemOperand());
}

SDValue DAGTypeLegalizer::PromoteIntOp_BITCAST(SDNode *N) {
  // This should only occur in unusual situations like bitcasting to an
  // x86_fp80, so just turn it into a store+load
  return CreateStackStoreLoad(N->getOperand(0), N->getValueType(0));
}

SDValue DAGTypeLegalizer::PromoteIntOp_BR_CC(SDNode *N, unsigned OpNo) {
  assert(OpNo == 2 && "Don't know how to promote this operand!");

  SDValue LHS = N->getOperand(2);
  SDValue RHS = N->getOperand(3);
  PromoteSetCCOperands(LHS, RHS, cast<CondCodeSDNode>(N->getOperand(1))->get());

  // The chain (Op#0), CC (#1) and basic block destination (Op#4) are always
  // legal types.
  return SDValue(DAG.UpdateNodeOperands(N, N->getOperand(0),
                                N->getOperand(1), LHS, RHS, N->getOperand(4)),
                 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_BRCOND(SDNode *N, unsigned OpNo) {
  assert(OpNo == 1 && "only know how to promote condition");

  // Promote all the way up to the canonical SetCC type.
  SDValue Cond = PromoteTargetBoolean(N->getOperand(1), MVT::Other);

  // The chain (Op#0) and basic block destination (Op#2) are always legal types.
  return SDValue(DAG.UpdateNodeOperands(N, N->getOperand(0), Cond,
                                        N->getOperand(2)), 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_BUILD_PAIR(SDNode *N) {
  // Since the result type is legal, the operands must promote to it.
  EVT OVT = N->getOperand(0).getValueType();
  SDValue Lo = ZExtPromotedInteger(N->getOperand(0));
  SDValue Hi = GetPromotedInteger(N->getOperand(1));
  assert(Lo.getValueType() == N->getValueType(0) && "Operand over promoted?");
  SDLoc dl(N);

  Hi = DAG.getNode(ISD::SHL, dl, N->getValueType(0), Hi,
                   DAG.getConstant(OVT.getSizeInBits(), dl,
                                   TLI.getPointerTy(DAG.getDataLayout())));
  return DAG.getNode(ISD::OR, dl, N->getValueType(0), Lo, Hi);
}

SDValue DAGTypeLegalizer::PromoteIntOp_BUILD_VECTOR(SDNode *N) {
  // The vector type is legal but the element type is not.  This implies
  // that the vector is a power-of-two in length and that the element
  // type does not have a strange size (eg: it is not i1).
  EVT VecVT = N->getValueType(0);
  unsigned NumElts = VecVT.getVectorNumElements();
  assert(!((NumElts & 1) && (!TLI.isTypeLegal(VecVT))) &&
         "Legal vector of one illegal element?");

  // Promote the inserted value.  The type does not need to match the
  // vector element type.  Check that any extra bits introduced will be
  // truncated away.
  assert(N->getOperand(0).getValueSizeInBits() >=
         N->getValueType(0).getScalarSizeInBits() &&
         "Type of inserted value narrower than vector element type!");

  SmallVector<SDValue, 16> NewOps;
  for (unsigned i = 0; i < NumElts; ++i)
    NewOps.push_back(GetPromotedInteger(N->getOperand(i)));

  return SDValue(DAG.UpdateNodeOperands(N, NewOps), 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_INSERT_VECTOR_ELT(SDNode *N,
                                                         unsigned OpNo) {
  if (OpNo == 1) {
    // Promote the inserted value.  This is valid because the type does not
    // have to match the vector element type.

    // Check that any extra bits introduced will be truncated away.
    assert(N->getOperand(1).getValueSizeInBits() >=
           N->getValueType(0).getScalarSizeInBits() &&
           "Type of inserted value narrower than vector element type!");
    return SDValue(DAG.UpdateNodeOperands(N, N->getOperand(0),
                                  GetPromotedInteger(N->getOperand(1)),
                                  N->getOperand(2)),
                   0);
  }

  assert(OpNo == 2 && "Different operand and result vector types?");

  // Promote the index.
  SDValue Idx = DAG.getZExtOrTrunc(N->getOperand(2), SDLoc(N),
                                   TLI.getVectorIdxTy(DAG.getDataLayout()));
  return SDValue(DAG.UpdateNodeOperands(N, N->getOperand(0),
                                N->getOperand(1), Idx), 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_ScalarOp(SDNode *N) {
  SDValue Op = GetPromotedInteger(N->getOperand(0));
  if (N->getOpcode() == ISD::EXPERIMENTAL_VP_SPLAT)
    return SDValue(
        DAG.UpdateNodeOperands(N, Op, N->getOperand(1), N->getOperand(2)), 0);

  // Integer SPLAT_VECTOR/SCALAR_TO_VECTOR operands are implicitly truncated,
  // so just promote the operand in place.
  return SDValue(DAG.UpdateNodeOperands(N, Op), 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_SELECT(SDNode *N, unsigned OpNo) {
  assert(OpNo == 0 && "Only know how to promote the condition!");
  SDValue Cond = N->getOperand(0);
  EVT OpTy = N->getOperand(1).getValueType();

  if (N->getOpcode() == ISD::VSELECT)
    if (SDValue Res = WidenVSELECTMask(N))
      return DAG.getNode(N->getOpcode(), SDLoc(N), N->getValueType(0),
                         Res, N->getOperand(1), N->getOperand(2));

  // Promote all the way up to the canonical SetCC type.
  EVT OpVT = N->getOpcode() == ISD::SELECT ? OpTy.getScalarType() : OpTy;
  Cond = PromoteTargetBoolean(Cond, OpVT);

  return SDValue(DAG.UpdateNodeOperands(N, Cond, N->getOperand(1),
                                        N->getOperand(2)), 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_SELECT_CC(SDNode *N, unsigned OpNo) {
  assert(OpNo == 0 && "Don't know how to promote this operand!");

  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  PromoteSetCCOperands(LHS, RHS, cast<CondCodeSDNode>(N->getOperand(4))->get());

  // The CC (#4) and the possible return values (#2 and #3) have legal types.
  return SDValue(DAG.UpdateNodeOperands(N, LHS, RHS, N->getOperand(2),
                                N->getOperand(3), N->getOperand(4)), 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_SETCC(SDNode *N, unsigned OpNo) {
  assert(OpNo == 0 && "Don't know how to promote this operand!");

  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  PromoteSetCCOperands(LHS, RHS, cast<CondCodeSDNode>(N->getOperand(2))->get());

  // The CC (#2) is always legal.
  if (N->getOpcode() == ISD::SETCC)
    return SDValue(DAG.UpdateNodeOperands(N, LHS, RHS, N->getOperand(2)), 0);

  assert(N->getOpcode() == ISD::VP_SETCC && "Expected VP_SETCC opcode");

  return SDValue(DAG.UpdateNodeOperands(N, LHS, RHS, N->getOperand(2),
                                        N->getOperand(3), N->getOperand(4)),
                 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_Shift(SDNode *N) {
  return SDValue(DAG.UpdateNodeOperands(N, N->getOperand(0),
                                ZExtPromotedInteger(N->getOperand(1))), 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_CMP(SDNode *N) {
  SDValue LHS = N->getOpcode() == ISD::UCMP
                    ? ZExtPromotedInteger(N->getOperand(0))
                    : SExtPromotedInteger(N->getOperand(0));
  SDValue RHS = N->getOpcode() == ISD::UCMP
                    ? ZExtPromotedInteger(N->getOperand(1))
                    : SExtPromotedInteger(N->getOperand(1));

  return SDValue(DAG.UpdateNodeOperands(N, LHS, RHS), 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_FunnelShift(SDNode *N) {
  return SDValue(DAG.UpdateNodeOperands(N, N->getOperand(0), N->getOperand(1),
                                ZExtPromotedInteger(N->getOperand(2))), 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_SIGN_EXTEND(SDNode *N) {
  SDValue Op = GetPromotedInteger(N->getOperand(0));
  SDLoc dl(N);
  Op = DAG.getNode(ISD::ANY_EXTEND, dl, N->getValueType(0), Op);
  return DAG.getNode(ISD::SIGN_EXTEND_INREG, dl, Op.getValueType(),
                     Op, DAG.getValueType(N->getOperand(0).getValueType()));
}

SDValue DAGTypeLegalizer::PromoteIntOp_VP_SIGN_EXTEND(SDNode *N) {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  SDValue Op = GetPromotedInteger(N->getOperand(0));
  // FIXME: There is no VP_ANY_EXTEND yet.
  Op = DAG.getNode(ISD::VP_ZERO_EXTEND, dl, VT, Op, N->getOperand(1),
                   N->getOperand(2));
  unsigned Diff =
      VT.getScalarSizeInBits() - N->getOperand(0).getScalarValueSizeInBits();
  SDValue ShAmt = DAG.getShiftAmountConstant(Diff, VT, dl);
  // FIXME: There is no VP_SIGN_EXTEND_INREG so use a pair of shifts.
  SDValue Shl = DAG.getNode(ISD::VP_SHL, dl, VT, Op, ShAmt, N->getOperand(1),
                            N->getOperand(2));
  return DAG.getNode(ISD::VP_SRA, dl, VT, Shl, ShAmt, N->getOperand(1),
                     N->getOperand(2));
}

SDValue DAGTypeLegalizer::PromoteIntOp_SINT_TO_FP(SDNode *N) {
  if (N->getOpcode() == ISD::VP_SINT_TO_FP)
    return SDValue(DAG.UpdateNodeOperands(N,
                                          SExtPromotedInteger(N->getOperand(0)),
                                          N->getOperand(1), N->getOperand(2)),
                   0);
  return SDValue(DAG.UpdateNodeOperands(N,
                                SExtPromotedInteger(N->getOperand(0))), 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_STRICT_SINT_TO_FP(SDNode *N) {
  return SDValue(DAG.UpdateNodeOperands(N, N->getOperand(0),
                                SExtPromotedInteger(N->getOperand(1))), 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_STORE(StoreSDNode *N, unsigned OpNo){
  assert(ISD::isUNINDEXEDStore(N) && "Indexed store during type legalization!");
  SDValue Ch = N->getChain(), Ptr = N->getBasePtr();
  SDLoc dl(N);

  SDValue Val = GetPromotedInteger(N->getValue());  // Get promoted value.

  // Truncate the value and store the result.
  return DAG.getTruncStore(Ch, dl, Val, Ptr,
                           N->getMemoryVT(), N->getMemOperand());
}

SDValue DAGTypeLegalizer::PromoteIntOp_MSTORE(MaskedStoreSDNode *N,
                                              unsigned OpNo) {
  SDValue DataOp = N->getValue();
  SDValue Mask = N->getMask();

  if (OpNo == 4) {
    // The Mask. Update in place.
    EVT DataVT = DataOp.getValueType();
    Mask = PromoteTargetBoolean(Mask, DataVT);
    SmallVector<SDValue, 4> NewOps(N->op_begin(), N->op_end());
    NewOps[4] = Mask;
    return SDValue(DAG.UpdateNodeOperands(N, NewOps), 0);
  }

  assert(OpNo == 1 && "Unexpected operand for promotion");
  DataOp = GetPromotedInteger(DataOp);

  return DAG.getMaskedStore(N->getChain(), SDLoc(N), DataOp, N->getBasePtr(),
                            N->getOffset(), Mask, N->getMemoryVT(),
                            N->getMemOperand(), N->getAddressingMode(),
                            /*IsTruncating*/ true, N->isCompressingStore());
}

SDValue DAGTypeLegalizer::PromoteIntOp_MLOAD(MaskedLoadSDNode *N,
                                             unsigned OpNo) {
  assert(OpNo == 3 && "Only know how to promote the mask!");
  EVT DataVT = N->getValueType(0);
  SDValue Mask = PromoteTargetBoolean(N->getOperand(OpNo), DataVT);
  SmallVector<SDValue, 4> NewOps(N->op_begin(), N->op_end());
  NewOps[OpNo] = Mask;
  SDNode *Res = DAG.UpdateNodeOperands(N, NewOps);
  if (Res == N)
    return SDValue(Res, 0);

  // Update triggered CSE, do our own replacement since caller can't.
  ReplaceValueWith(SDValue(N, 0), SDValue(Res, 0));
  ReplaceValueWith(SDValue(N, 1), SDValue(Res, 1));
  return SDValue();
}

SDValue DAGTypeLegalizer::PromoteIntOp_MGATHER(MaskedGatherSDNode *N,
                                               unsigned OpNo) {
  SmallVector<SDValue, 5> NewOps(N->op_begin(), N->op_end());

  if (OpNo == 2) {
    // The Mask
    EVT DataVT = N->getValueType(0);
    NewOps[OpNo] = PromoteTargetBoolean(N->getOperand(OpNo), DataVT);
  } else if (OpNo == 4) {
    // The Index
    if (N->isIndexSigned())
      // Need to sign extend the index since the bits will likely be used.
      NewOps[OpNo] = SExtPromotedInteger(N->getOperand(OpNo));
    else
      NewOps[OpNo] = ZExtPromotedInteger(N->getOperand(OpNo));
  } else
    NewOps[OpNo] = GetPromotedInteger(N->getOperand(OpNo));

  SDNode *Res = DAG.UpdateNodeOperands(N, NewOps);
  if (Res == N)
    return SDValue(Res, 0);

  // Update triggered CSE, do our own replacement since caller can't.
  ReplaceValueWith(SDValue(N, 0), SDValue(Res, 0));
  ReplaceValueWith(SDValue(N, 1), SDValue(Res, 1));
  return SDValue();
}

SDValue DAGTypeLegalizer::PromoteIntOp_MSCATTER(MaskedScatterSDNode *N,
                                                unsigned OpNo) {
  bool TruncateStore = N->isTruncatingStore();
  SmallVector<SDValue, 5> NewOps(N->op_begin(), N->op_end());

  if (OpNo == 2) {
    // The Mask
    EVT DataVT = N->getValue().getValueType();
    NewOps[OpNo] = PromoteTargetBoolean(N->getOperand(OpNo), DataVT);
  } else if (OpNo == 4) {
    // The Index
    if (N->isIndexSigned())
      // Need to sign extend the index since the bits will likely be used.
      NewOps[OpNo] = SExtPromotedInteger(N->getOperand(OpNo));
    else
      NewOps[OpNo] = ZExtPromotedInteger(N->getOperand(OpNo));
  } else {
    NewOps[OpNo] = GetPromotedInteger(N->getOperand(OpNo));
    TruncateStore = true;
  }

  return DAG.getMaskedScatter(DAG.getVTList(MVT::Other), N->getMemoryVT(),
                              SDLoc(N), NewOps, N->getMemOperand(),
                              N->getIndexType(), TruncateStore);
}

SDValue DAGTypeLegalizer::PromoteIntOp_VECTOR_COMPRESS(SDNode *N,
                                                       unsigned OpNo) {
  assert(OpNo == 1 && "Can only promote VECTOR_COMPRESS mask.");
  SDValue Vec = N->getOperand(0);
  EVT VT = Vec.getValueType();
  SDValue Passthru = N->getOperand(2);
  SDValue Mask = PromoteTargetBoolean(N->getOperand(1), VT);
  return DAG.getNode(ISD::VECTOR_COMPRESS, SDLoc(N), VT, Vec, Mask, Passthru);
}

SDValue DAGTypeLegalizer::PromoteIntOp_TRUNCATE(SDNode *N) {
  SDValue Op = GetPromotedInteger(N->getOperand(0));
  if (N->getOpcode() == ISD::VP_TRUNCATE)
    return DAG.getNode(ISD::VP_TRUNCATE, SDLoc(N), N->getValueType(0), Op,
                       N->getOperand(1), N->getOperand(2));
  return DAG.getNode(ISD::TRUNCATE, SDLoc(N), N->getValueType(0), Op);
}

SDValue DAGTypeLegalizer::PromoteIntOp_UINT_TO_FP(SDNode *N) {
  if (N->getOpcode() == ISD::VP_UINT_TO_FP)
    return SDValue(DAG.UpdateNodeOperands(N,
                                          ZExtPromotedInteger(N->getOperand(0)),
                                          N->getOperand(1), N->getOperand(2)),
                   0);
  return SDValue(DAG.UpdateNodeOperands(N,
                                ZExtPromotedInteger(N->getOperand(0))), 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_STRICT_UINT_TO_FP(SDNode *N) {
  return SDValue(DAG.UpdateNodeOperands(N, N->getOperand(0),
                                ZExtPromotedInteger(N->getOperand(1))), 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_ZERO_EXTEND(SDNode *N) {
  SDLoc dl(N);
  SDValue Op = GetPromotedInteger(N->getOperand(0));
  Op = DAG.getNode(ISD::ANY_EXTEND, dl, N->getValueType(0), Op);
  return DAG.getZeroExtendInReg(Op, dl, N->getOperand(0).getValueType());
}

SDValue DAGTypeLegalizer::PromoteIntOp_VP_ZERO_EXTEND(SDNode *N) {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  SDValue Op = GetPromotedInteger(N->getOperand(0));
  // FIXME: There is no VP_ANY_EXTEND yet.
  Op = DAG.getNode(ISD::VP_ZERO_EXTEND, dl, VT, Op, N->getOperand(1),
                   N->getOperand(2));
  return DAG.getVPZeroExtendInReg(Op, N->getOperand(1), N->getOperand(2), dl,
                                  N->getOperand(0).getValueType());
}

SDValue DAGTypeLegalizer::PromoteIntOp_FIX(SDNode *N) {
  SDValue Op2 = ZExtPromotedInteger(N->getOperand(2));
  return SDValue(
      DAG.UpdateNodeOperands(N, N->getOperand(0), N->getOperand(1), Op2), 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_FRAMERETURNADDR(SDNode *N) {
  // Promote the RETURNADDR/FRAMEADDR argument to a supported integer width.
  SDValue Op = ZExtPromotedInteger(N->getOperand(0));
  return SDValue(DAG.UpdateNodeOperands(N, Op), 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_ExpOp(SDNode *N) {
  bool IsStrict = N->isStrictFPOpcode();
  SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();

  bool IsPowI =
      N->getOpcode() == ISD::FPOWI || N->getOpcode() == ISD::STRICT_FPOWI;

  // The integer operand is the last operand in FPOWI (or FLDEXP) (so the result
  // and floating point operand is already type legalized).
  RTLIB::Libcall LC = IsPowI ? RTLIB::getPOWI(N->getValueType(0))
                             : RTLIB::getLDEXP(N->getValueType(0));

  if (LC == RTLIB::UNKNOWN_LIBCALL || !TLI.getLibcallName(LC)) {
    SDValue Op = SExtPromotedInteger(N->getOperand(1));
    return SDValue(DAG.UpdateNodeOperands(N, N->getOperand(0), Op), 0);
  }

  // We can't just promote the exponent type in FPOWI, since we want to lower
  // the node to a libcall and we if we promote to a type larger than
  // sizeof(int) the libcall might not be according to the targets ABI. Instead
  // we rewrite to a libcall here directly, letting makeLibCall handle promotion
  // if the target accepts it according to shouldSignExtendTypeInLibCall.

  unsigned OpOffset = IsStrict ? 1 : 0;
  // The exponent should fit in a sizeof(int) type for the libcall to be valid.
  assert(DAG.getLibInfo().getIntSize() ==
             N->getOperand(1 + OpOffset).getValueType().getSizeInBits() &&
         "POWI exponent should match with sizeof(int) when doing the libcall.");
  TargetLowering::MakeLibCallOptions CallOptions;
  CallOptions.setSExt(true);
  SDValue Ops[2] = {N->getOperand(0 + OpOffset), N->getOperand(1 + OpOffset)};
  std::pair<SDValue, SDValue> Tmp = TLI.makeLibCall(
      DAG, LC, N->getValueType(0), Ops, CallOptions, SDLoc(N), Chain);
  ReplaceValueWith(SDValue(N, 0), Tmp.first);
  if (IsStrict)
    ReplaceValueWith(SDValue(N, 1), Tmp.second);
  return SDValue();
}

static unsigned getExtendForIntVecReduction(SDNode *N) {
  switch (N->getOpcode()) {
  default:
    llvm_unreachable("Expected integer vector reduction");
  case ISD::VECREDUCE_ADD:
  case ISD::VECREDUCE_MUL:
  case ISD::VECREDUCE_AND:
  case ISD::VECREDUCE_OR:
  case ISD::VECREDUCE_XOR:
  case ISD::VP_REDUCE_ADD:
  case ISD::VP_REDUCE_MUL:
  case ISD::VP_REDUCE_AND:
  case ISD::VP_REDUCE_OR:
  case ISD::VP_REDUCE_XOR:
    return ISD::ANY_EXTEND;
  case ISD::VECREDUCE_SMAX:
  case ISD::VECREDUCE_SMIN:
  case ISD::VP_REDUCE_SMAX:
  case ISD::VP_REDUCE_SMIN:
    return ISD::SIGN_EXTEND;
  case ISD::VECREDUCE_UMAX:
  case ISD::VECREDUCE_UMIN:
  case ISD::VP_REDUCE_UMAX:
  case ISD::VP_REDUCE_UMIN:
    return ISD::ZERO_EXTEND;
  }
}

SDValue DAGTypeLegalizer::PromoteIntOpVectorReduction(SDNode *N, SDValue V) {
  switch (getExtendForIntVecReduction(N)) {
  default:
    llvm_unreachable("Impossible extension kind for integer reduction");
  case ISD::ANY_EXTEND:
    return GetPromotedInteger(V);
  case ISD::SIGN_EXTEND:
    return SExtPromotedInteger(V);
  case ISD::ZERO_EXTEND:
    return ZExtPromotedInteger(V);
  }
}

SDValue DAGTypeLegalizer::PromoteIntOp_VECREDUCE(SDNode *N) {
  SDLoc dl(N);
  SDValue Op = PromoteIntOpVectorReduction(N, N->getOperand(0));

  EVT OrigEltVT = N->getOperand(0).getValueType().getVectorElementType();
  EVT InVT = Op.getValueType();
  EVT EltVT = InVT.getVectorElementType();
  EVT ResVT = N->getValueType(0);
  unsigned Opcode = N->getOpcode();

  // An i1 vecreduce_xor is equivalent to vecreduce_add, use that instead if
  // vecreduce_xor is not legal
  if (Opcode == ISD::VECREDUCE_XOR && OrigEltVT == MVT::i1 &&
      !TLI.isOperationLegalOrCustom(ISD::VECREDUCE_XOR, InVT) &&
      TLI.isOperationLegalOrCustom(ISD::VECREDUCE_ADD, InVT))
    Opcode = ISD::VECREDUCE_ADD;

  // An i1 vecreduce_or is equivalent to vecreduce_umax, use that instead if
  // vecreduce_or is not legal
  else if (Opcode == ISD::VECREDUCE_OR && OrigEltVT == MVT::i1 &&
           !TLI.isOperationLegalOrCustom(ISD::VECREDUCE_OR, InVT) &&
           TLI.isOperationLegalOrCustom(ISD::VECREDUCE_UMAX, InVT)) {
    Opcode = ISD::VECREDUCE_UMAX;
    // Can't use promoteTargetBoolean here because we still need
    // to either sign_ext or zero_ext in the undefined case.
    switch (TLI.getBooleanContents(InVT)) {
    case TargetLoweringBase::UndefinedBooleanContent:
    case TargetLoweringBase::ZeroOrOneBooleanContent:
      Op = ZExtPromotedInteger(N->getOperand(0));
      break;
    case TargetLoweringBase::ZeroOrNegativeOneBooleanContent:
      Op = SExtPromotedInteger(N->getOperand(0));
      break;
    }
  }

  // An i1 vecreduce_and is equivalent to vecreduce_umin, use that instead if
  // vecreduce_and is not legal
  else if (Opcode == ISD::VECREDUCE_AND && OrigEltVT == MVT::i1 &&
           !TLI.isOperationLegalOrCustom(ISD::VECREDUCE_AND, InVT) &&
           TLI.isOperationLegalOrCustom(ISD::VECREDUCE_UMIN, InVT)) {
    Opcode = ISD::VECREDUCE_UMIN;
    // Can't use promoteTargetBoolean here because we still need
    // to either sign_ext or zero_ext in the undefined case.
    switch (TLI.getBooleanContents(InVT)) {
    case TargetLoweringBase::UndefinedBooleanContent:
    case TargetLoweringBase::ZeroOrOneBooleanContent:
      Op = ZExtPromotedInteger(N->getOperand(0));
      break;
    case TargetLoweringBase::ZeroOrNegativeOneBooleanContent:
      Op = SExtPromotedInteger(N->getOperand(0));
      break;
    }
  }

  if (ResVT.bitsGE(EltVT))
    return DAG.getNode(Opcode, SDLoc(N), ResVT, Op);

  // Result size must be >= element size. If this is not the case after
  // promotion, also promote the result type and then truncate.
  SDValue Reduce = DAG.getNode(Opcode, dl, EltVT, Op);
  return DAG.getNode(ISD::TRUNCATE, dl, ResVT, Reduce);
}

SDValue DAGTypeLegalizer::PromoteIntOp_VP_REDUCE(SDNode *N, unsigned OpNo) {
  SDLoc DL(N);
  SDValue Op = N->getOperand(OpNo);
  SmallVector<SDValue, 4> NewOps(N->op_begin(), N->op_end());

  if (OpNo == 2) { // Mask
    // Update in place.
    NewOps[2] = PromoteTargetBoolean(Op, N->getOperand(1).getValueType());
    return SDValue(DAG.UpdateNodeOperands(N, NewOps), 0);
  }

  assert(OpNo == 1 && "Unexpected operand for promotion");

  Op = PromoteIntOpVectorReduction(N, Op);

  NewOps[OpNo] = Op;

  EVT VT = N->getValueType(0);
  EVT EltVT = Op.getValueType().getScalarType();

  if (VT.bitsGE(EltVT))
    return DAG.getNode(N->getOpcode(), SDLoc(N), VT, NewOps);

  // Result size must be >= element/start-value size. If this is not the case
  // after promotion, also promote both the start value and result type and
  // then truncate.
  NewOps[0] =
      DAG.getNode(getExtendForIntVecReduction(N), DL, EltVT, N->getOperand(0));
  SDValue Reduce = DAG.getNode(N->getOpcode(), DL, EltVT, NewOps);
  return DAG.getNode(ISD::TRUNCATE, DL, VT, Reduce);
}

SDValue DAGTypeLegalizer::PromoteIntOp_SET_ROUNDING(SDNode *N) {
  SDValue Op = ZExtPromotedInteger(N->getOperand(1));
  return SDValue(DAG.UpdateNodeOperands(N, N->getOperand(0), Op), 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_STACKMAP(SDNode *N, unsigned OpNo) {
  assert(OpNo > 1); // Because the first two arguments are guaranteed legal.
  SmallVector<SDValue> NewOps(N->ops().begin(), N->ops().end());
  SDValue Operand = N->getOperand(OpNo);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), Operand.getValueType());
  NewOps[OpNo] = DAG.getNode(ISD::ANY_EXTEND, SDLoc(N), NVT, Operand);
  return SDValue(DAG.UpdateNodeOperands(N, NewOps), 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_PATCHPOINT(SDNode *N, unsigned OpNo) {
  assert(OpNo >= 7);
  SmallVector<SDValue> NewOps(N->ops().begin(), N->ops().end());
  SDValue Operand = N->getOperand(OpNo);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), Operand.getValueType());
  NewOps[OpNo] = DAG.getNode(ISD::ANY_EXTEND, SDLoc(N), NVT, Operand);
  return SDValue(DAG.UpdateNodeOperands(N, NewOps), 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_VP_STRIDED(SDNode *N, unsigned OpNo) {
  assert((N->getOpcode() == ISD::EXPERIMENTAL_VP_STRIDED_LOAD && OpNo == 3) ||
         (N->getOpcode() == ISD::EXPERIMENTAL_VP_STRIDED_STORE && OpNo == 4));

  SmallVector<SDValue, 8> NewOps(N->op_begin(), N->op_end());
  NewOps[OpNo] = SExtPromotedInteger(N->getOperand(OpNo));

  return SDValue(DAG.UpdateNodeOperands(N, NewOps), 0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_VP_SPLICE(SDNode *N, unsigned OpNo) {
  SmallVector<SDValue, 6> NewOps(N->op_begin(), N->op_end());

  if (OpNo == 2) { // Offset operand
    NewOps[OpNo] = SExtPromotedInteger(N->getOperand(OpNo));
    return SDValue(DAG.UpdateNodeOperands(N, NewOps), 0);
  }

  assert((OpNo == 4 || OpNo == 5) && "Unexpected operand for promotion");

  NewOps[OpNo] = ZExtPromotedInteger(N->getOperand(OpNo));
  return SDValue(DAG.UpdateNodeOperands(N, NewOps), 0);
}

//===----------------------------------------------------------------------===//
//  Integer Result Expansion
//===----------------------------------------------------------------------===//

/// ExpandIntegerResult - This method is called when the specified result of the
/// specified node is found to need expansion.  At this point, the node may also
/// have invalid operands or may have other results that need promotion, we just
/// know that (at least) one result needs expansion.
void DAGTypeLegalizer::ExpandIntegerResult(SDNode *N, unsigned ResNo) {
  LLVM_DEBUG(dbgs() << "Expand integer result: "; N->dump(&DAG));
  SDValue Lo, Hi;
  Lo = Hi = SDValue();

  // See if the target wants to custom expand this node.
  if (CustomLowerNode(N, N->getValueType(ResNo), true))
    return;

  switch (N->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "ExpandIntegerResult #" << ResNo << ": ";
    N->dump(&DAG); dbgs() << "\n";
#endif
    report_fatal_error("Do not know how to expand the result of this "
                       "operator!");

  case ISD::ARITH_FENCE:  SplitRes_ARITH_FENCE(N, Lo, Hi); break;
  case ISD::MERGE_VALUES: SplitRes_MERGE_VALUES(N, ResNo, Lo, Hi); break;
  case ISD::SELECT:       SplitRes_Select(N, Lo, Hi); break;
  case ISD::SELECT_CC:    SplitRes_SELECT_CC(N, Lo, Hi); break;
  case ISD::UNDEF:        SplitRes_UNDEF(N, Lo, Hi); break;
  case ISD::FREEZE:       SplitRes_FREEZE(N, Lo, Hi); break;

  case ISD::BITCAST:            ExpandRes_BITCAST(N, Lo, Hi); break;
  case ISD::BUILD_PAIR:         ExpandRes_BUILD_PAIR(N, Lo, Hi); break;
  case ISD::EXTRACT_ELEMENT:    ExpandRes_EXTRACT_ELEMENT(N, Lo, Hi); break;
  case ISD::EXTRACT_VECTOR_ELT: ExpandRes_EXTRACT_VECTOR_ELT(N, Lo, Hi); break;
  case ISD::VAARG:              ExpandRes_VAARG(N, Lo, Hi); break;

  case ISD::ANY_EXTEND:  ExpandIntRes_ANY_EXTEND(N, Lo, Hi); break;
  case ISD::AssertSext:  ExpandIntRes_AssertSext(N, Lo, Hi); break;
  case ISD::AssertZext:  ExpandIntRes_AssertZext(N, Lo, Hi); break;
  case ISD::BITREVERSE:  ExpandIntRes_BITREVERSE(N, Lo, Hi); break;
  case ISD::BSWAP:       ExpandIntRes_BSWAP(N, Lo, Hi); break;
  case ISD::PARITY:      ExpandIntRes_PARITY(N, Lo, Hi); break;
  case ISD::Constant:    ExpandIntRes_Constant(N, Lo, Hi); break;
  case ISD::ABS:         ExpandIntRes_ABS(N, Lo, Hi); break;
  case ISD::CTLZ_ZERO_UNDEF:
  case ISD::CTLZ:        ExpandIntRes_CTLZ(N, Lo, Hi); break;
  case ISD::CTPOP:       ExpandIntRes_CTPOP(N, Lo, Hi); break;
  case ISD::CTTZ_ZERO_UNDEF:
  case ISD::CTTZ:        ExpandIntRes_CTTZ(N, Lo, Hi); break;
  case ISD::GET_ROUNDING:ExpandIntRes_GET_ROUNDING(N, Lo, Hi); break;
  case ISD::STRICT_FP_TO_SINT:
  case ISD::FP_TO_SINT:
  case ISD::STRICT_FP_TO_UINT:
  case ISD::FP_TO_UINT:  ExpandIntRes_FP_TO_XINT(N, Lo, Hi); break;
  case ISD::FP_TO_SINT_SAT:
  case ISD::FP_TO_UINT_SAT: ExpandIntRes_FP_TO_XINT_SAT(N, Lo, Hi); break;
  case ISD::STRICT_LROUND:
  case ISD::STRICT_LRINT:
  case ISD::LROUND:
  case ISD::LRINT:
  case ISD::STRICT_LLROUND:
  case ISD::STRICT_LLRINT:
  case ISD::LLROUND:
  case ISD::LLRINT:      ExpandIntRes_XROUND_XRINT(N, Lo, Hi); break;
  case ISD::LOAD:        ExpandIntRes_LOAD(cast<LoadSDNode>(N), Lo, Hi); break;
  case ISD::MUL:         ExpandIntRes_MUL(N, Lo, Hi); break;
  case ISD::READCYCLECOUNTER:
  case ISD::READSTEADYCOUNTER: ExpandIntRes_READCOUNTER(N, Lo, Hi); break;
  case ISD::SDIV:        ExpandIntRes_SDIV(N, Lo, Hi); break;
  case ISD::SIGN_EXTEND: ExpandIntRes_SIGN_EXTEND(N, Lo, Hi); break;
  case ISD::SIGN_EXTEND_INREG: ExpandIntRes_SIGN_EXTEND_INREG(N, Lo, Hi); break;
  case ISD::SREM:        ExpandIntRes_SREM(N, Lo, Hi); break;
  case ISD::TRUNCATE:    ExpandIntRes_TRUNCATE(N, Lo, Hi); break;
  case ISD::UDIV:        ExpandIntRes_UDIV(N, Lo, Hi); break;
  case ISD::UREM:        ExpandIntRes_UREM(N, Lo, Hi); break;
  case ISD::ZERO_EXTEND: ExpandIntRes_ZERO_EXTEND(N, Lo, Hi); break;
  case ISD::ATOMIC_LOAD: ExpandIntRes_ATOMIC_LOAD(N, Lo, Hi); break;

  case ISD::ATOMIC_LOAD_ADD:
  case ISD::ATOMIC_LOAD_SUB:
  case ISD::ATOMIC_LOAD_AND:
  case ISD::ATOMIC_LOAD_CLR:
  case ISD::ATOMIC_LOAD_OR:
  case ISD::ATOMIC_LOAD_XOR:
  case ISD::ATOMIC_LOAD_NAND:
  case ISD::ATOMIC_LOAD_MIN:
  case ISD::ATOMIC_LOAD_MAX:
  case ISD::ATOMIC_LOAD_UMIN:
  case ISD::ATOMIC_LOAD_UMAX:
  case ISD::ATOMIC_SWAP:
  case ISD::ATOMIC_CMP_SWAP: {
    std::pair<SDValue, SDValue> Tmp = ExpandAtomic(N);
    SplitInteger(Tmp.first, Lo, Hi);
    ReplaceValueWith(SDValue(N, 1), Tmp.second);
    break;
  }
  case ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS: {
    AtomicSDNode *AN = cast<AtomicSDNode>(N);
    SDVTList VTs = DAG.getVTList(N->getValueType(0), MVT::Other);
    SDValue Tmp = DAG.getAtomicCmpSwap(
        ISD::ATOMIC_CMP_SWAP, SDLoc(N), AN->getMemoryVT(), VTs,
        N->getOperand(0), N->getOperand(1), N->getOperand(2), N->getOperand(3),
        AN->getMemOperand());

    // Expanding to the strong ATOMIC_CMP_SWAP node means we can determine
    // success simply by comparing the loaded value against the ingoing
    // comparison.
    SDValue Success = DAG.getSetCC(SDLoc(N), N->getValueType(1), Tmp,
                                   N->getOperand(2), ISD::SETEQ);

    SplitInteger(Tmp, Lo, Hi);
    ReplaceValueWith(SDValue(N, 1), Success);
    ReplaceValueWith(SDValue(N, 2), Tmp.getValue(1));
    break;
  }

  case ISD::AND:
  case ISD::OR:
  case ISD::XOR: ExpandIntRes_Logical(N, Lo, Hi); break;

  case ISD::UMAX:
  case ISD::SMAX:
  case ISD::UMIN:
  case ISD::SMIN: ExpandIntRes_MINMAX(N, Lo, Hi); break;

  case ISD::SCMP:
  case ISD::UCMP: ExpandIntRes_CMP(N, Lo, Hi); break;

  case ISD::ADD:
  case ISD::SUB: ExpandIntRes_ADDSUB(N, Lo, Hi); break;

  case ISD::ADDC:
  case ISD::SUBC: ExpandIntRes_ADDSUBC(N, Lo, Hi); break;

  case ISD::ADDE:
  case ISD::SUBE: ExpandIntRes_ADDSUBE(N, Lo, Hi); break;

  case ISD::UADDO_CARRY:
  case ISD::USUBO_CARRY: ExpandIntRes_UADDSUBO_CARRY(N, Lo, Hi); break;

  case ISD::SADDO_CARRY:
  case ISD::SSUBO_CARRY: ExpandIntRes_SADDSUBO_CARRY(N, Lo, Hi); break;

  case ISD::SHL:
  case ISD::SRA:
  case ISD::SRL: ExpandIntRes_Shift(N, Lo, Hi); break;

  case ISD::SADDO:
  case ISD::SSUBO: ExpandIntRes_SADDSUBO(N, Lo, Hi); break;
  case ISD::UADDO:
  case ISD::USUBO: ExpandIntRes_UADDSUBO(N, Lo, Hi); break;
  case ISD::UMULO:
  case ISD::SMULO: ExpandIntRes_XMULO(N, Lo, Hi); break;

  case ISD::SADDSAT:
  case ISD::UADDSAT:
  case ISD::SSUBSAT:
  case ISD::USUBSAT: ExpandIntRes_ADDSUBSAT(N, Lo, Hi); break;

  case ISD::SSHLSAT:
  case ISD::USHLSAT: ExpandIntRes_SHLSAT(N, Lo, Hi); break;

  case ISD::AVGCEILS:
  case ISD::AVGCEILU: 
  case ISD::AVGFLOORS:
  case ISD::AVGFLOORU: ExpandIntRes_AVG(N, Lo, Hi); break;

  case ISD::SMULFIX:
  case ISD::SMULFIXSAT:
  case ISD::UMULFIX:
  case ISD::UMULFIXSAT: ExpandIntRes_MULFIX(N, Lo, Hi); break;

  case ISD::SDIVFIX:
  case ISD::SDIVFIXSAT:
  case ISD::UDIVFIX:
  case ISD::UDIVFIXSAT: ExpandIntRes_DIVFIX(N, Lo, Hi); break;

  case ISD::VECREDUCE_ADD:
  case ISD::VECREDUCE_MUL:
  case ISD::VECREDUCE_AND:
  case ISD::VECREDUCE_OR:
  case ISD::VECREDUCE_XOR:
  case ISD::VECREDUCE_SMAX:
  case ISD::VECREDUCE_SMIN:
  case ISD::VECREDUCE_UMAX:
  case ISD::VECREDUCE_UMIN: ExpandIntRes_VECREDUCE(N, Lo, Hi); break;

  case ISD::ROTL:
  case ISD::ROTR:
    ExpandIntRes_Rotate(N, Lo, Hi);
    break;

  case ISD::FSHL:
  case ISD::FSHR:
    ExpandIntRes_FunnelShift(N, Lo, Hi);
    break;

  case ISD::VSCALE:
    ExpandIntRes_VSCALE(N, Lo, Hi);
    break;
  }

  // If Lo/Hi is null, the sub-method took care of registering results etc.
  if (Lo.getNode())
    SetExpandedInteger(SDValue(N, ResNo), Lo, Hi);
}

/// Lower an atomic node to the appropriate builtin call.
std::pair <SDValue, SDValue> DAGTypeLegalizer::ExpandAtomic(SDNode *Node) {
  unsigned Opc = Node->getOpcode();
  MVT VT = cast<AtomicSDNode>(Node)->getMemoryVT().getSimpleVT();
  AtomicOrdering order = cast<AtomicSDNode>(Node)->getMergedOrdering();
  // Lower to outline atomic libcall if outline atomics enabled,
  // or to sync libcall otherwise
  RTLIB::Libcall LC = RTLIB::getOUTLINE_ATOMIC(Opc, order, VT);
  EVT RetVT = Node->getValueType(0);
  TargetLowering::MakeLibCallOptions CallOptions;
  SmallVector<SDValue, 4> Ops;
  if (TLI.getLibcallName(LC)) {
    Ops.append(Node->op_begin() + 2, Node->op_end());
    Ops.push_back(Node->getOperand(1));
  } else {
    LC = RTLIB::getSYNC(Opc, VT);
    assert(LC != RTLIB::UNKNOWN_LIBCALL &&
           "Unexpected atomic op or value type!");
    Ops.append(Node->op_begin() + 1, Node->op_end());
  }
  return TLI.makeLibCall(DAG, LC, RetVT, Ops, CallOptions, SDLoc(Node),
                         Node->getOperand(0));
}

/// N is a shift by a value that needs to be expanded,
/// and the shift amount is a constant 'Amt'.  Expand the operation.
void DAGTypeLegalizer::ExpandShiftByConstant(SDNode *N, const APInt &Amt,
                                             SDValue &Lo, SDValue &Hi) {
  SDLoc DL(N);
  // Expand the incoming operand to be shifted, so that we have its parts
  SDValue InL, InH;
  GetExpandedInteger(N->getOperand(0), InL, InH);

  // Though Amt shouldn't usually be 0, it's possible. E.g. when legalization
  // splitted a vector shift, like this: <op1, op2> SHL <0, 2>.
  if (!Amt) {
    Lo = InL;
    Hi = InH;
    return;
  }

  EVT NVT = InL.getValueType();
  unsigned VTBits = N->getValueType(0).getSizeInBits();
  unsigned NVTBits = NVT.getSizeInBits();

  if (N->getOpcode() == ISD::SHL) {
    if (Amt.uge(VTBits)) {
      Lo = Hi = DAG.getConstant(0, DL, NVT);
    } else if (Amt.ugt(NVTBits)) {
      Lo = DAG.getConstant(0, DL, NVT);
      Hi = DAG.getNode(ISD::SHL, DL, NVT, InL,
                       DAG.getShiftAmountConstant(Amt - NVTBits, NVT, DL));
    } else if (Amt == NVTBits) {
      Lo = DAG.getConstant(0, DL, NVT);
      Hi = InL;
    } else {
      Lo = DAG.getNode(ISD::SHL, DL, NVT, InL,
                       DAG.getShiftAmountConstant(Amt, NVT, DL));
      Hi = DAG.getNode(
          ISD::OR, DL, NVT,
          DAG.getNode(ISD::SHL, DL, NVT, InH,
                      DAG.getShiftAmountConstant(Amt, NVT, DL)),
          DAG.getNode(ISD::SRL, DL, NVT, InL,
                      DAG.getShiftAmountConstant(-Amt + NVTBits, NVT, DL)));
    }
    return;
  }

  if (N->getOpcode() == ISD::SRL) {
    if (Amt.uge(VTBits)) {
      Lo = Hi = DAG.getConstant(0, DL, NVT);
    } else if (Amt.ugt(NVTBits)) {
      Lo = DAG.getNode(ISD::SRL, DL, NVT, InH,
                       DAG.getShiftAmountConstant(Amt - NVTBits, NVT, DL));
      Hi = DAG.getConstant(0, DL, NVT);
    } else if (Amt == NVTBits) {
      Lo = InH;
      Hi = DAG.getConstant(0, DL, NVT);
    } else {
      Lo = DAG.getNode(
          ISD::OR, DL, NVT,
          DAG.getNode(ISD::SRL, DL, NVT, InL,
                      DAG.getShiftAmountConstant(Amt, NVT, DL)),
          DAG.getNode(ISD::SHL, DL, NVT, InH,
                      DAG.getShiftAmountConstant(-Amt + NVTBits, NVT, DL)));
      Hi = DAG.getNode(ISD::SRL, DL, NVT, InH,
                       DAG.getShiftAmountConstant(Amt, NVT, DL));
    }
    return;
  }

  assert(N->getOpcode() == ISD::SRA && "Unknown shift!");
  if (Amt.uge(VTBits)) {
    Hi = Lo = DAG.getNode(ISD::SRA, DL, NVT, InH,
                          DAG.getShiftAmountConstant(NVTBits - 1, NVT, DL));
  } else if (Amt.ugt(NVTBits)) {
    Lo = DAG.getNode(ISD::SRA, DL, NVT, InH,
                     DAG.getShiftAmountConstant(Amt - NVTBits, NVT, DL));
    Hi = DAG.getNode(ISD::SRA, DL, NVT, InH,
                     DAG.getShiftAmountConstant(NVTBits - 1, NVT, DL));
  } else if (Amt == NVTBits) {
    Lo = InH;
    Hi = DAG.getNode(ISD::SRA, DL, NVT, InH,
                     DAG.getShiftAmountConstant(NVTBits - 1, NVT, DL));
  } else {
    Lo = DAG.getNode(
        ISD::OR, DL, NVT,
        DAG.getNode(ISD::SRL, DL, NVT, InL,
                    DAG.getShiftAmountConstant(Amt, NVT, DL)),
        DAG.getNode(ISD::SHL, DL, NVT, InH,
                    DAG.getShiftAmountConstant(-Amt + NVTBits, NVT, DL)));
    Hi = DAG.getNode(ISD::SRA, DL, NVT, InH,
                     DAG.getShiftAmountConstant(Amt, NVT, DL));
  }
}

/// ExpandShiftWithKnownAmountBit - Try to determine whether we can simplify
/// this shift based on knowledge of the high bit of the shift amount.  If we
/// can tell this, we know that it is >= 32 or < 32, without knowing the actual
/// shift amount.
bool DAGTypeLegalizer::
ExpandShiftWithKnownAmountBit(SDNode *N, SDValue &Lo, SDValue &Hi) {
  unsigned Opc = N->getOpcode();
  SDValue In = N->getOperand(0);
  SDValue Amt = N->getOperand(1);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  EVT ShTy = Amt.getValueType();
  unsigned ShBits = ShTy.getScalarSizeInBits();
  unsigned NVTBits = NVT.getScalarSizeInBits();
  assert(isPowerOf2_32(NVTBits) &&
         "Expanded integer type size not a power of two!");
  SDLoc dl(N);

  APInt HighBitMask = APInt::getHighBitsSet(ShBits, ShBits - Log2_32(NVTBits));
  KnownBits Known = DAG.computeKnownBits(Amt);

  // If we don't know anything about the high bits, exit.
  if (((Known.Zero | Known.One) & HighBitMask) == 0)
    return false;

  // Get the incoming operand to be shifted.
  SDValue InL, InH;
  GetExpandedInteger(In, InL, InH);

  // If we know that any of the high bits of the shift amount are one, then we
  // can do this as a couple of simple shifts.
  if (Known.One.intersects(HighBitMask)) {
    // Mask out the high bit, which we know is set.
    Amt = DAG.getNode(ISD::AND, dl, ShTy, Amt,
                      DAG.getConstant(~HighBitMask, dl, ShTy));

    switch (Opc) {
    default: llvm_unreachable("Unknown shift");
    case ISD::SHL:
      Lo = DAG.getConstant(0, dl, NVT);              // Low part is zero.
      Hi = DAG.getNode(ISD::SHL, dl, NVT, InL, Amt); // High part from Lo part.
      return true;
    case ISD::SRL:
      Hi = DAG.getConstant(0, dl, NVT);              // Hi part is zero.
      Lo = DAG.getNode(ISD::SRL, dl, NVT, InH, Amt); // Lo part from Hi part.
      return true;
    case ISD::SRA:
      Hi = DAG.getNode(ISD::SRA, dl, NVT, InH,       // Sign extend high part.
                       DAG.getConstant(NVTBits - 1, dl, ShTy));
      Lo = DAG.getNode(ISD::SRA, dl, NVT, InH, Amt); // Lo part from Hi part.
      return true;
    }
  }

  // If we know that all of the high bits of the shift amount are zero, then we
  // can do this as a couple of simple shifts.
  if (HighBitMask.isSubsetOf(Known.Zero)) {
    // Calculate 31-x. 31 is used instead of 32 to avoid creating an undefined
    // shift if x is zero.  We can use XOR here because x is known to be smaller
    // than 32.
    SDValue Amt2 = DAG.getNode(ISD::XOR, dl, ShTy, Amt,
                               DAG.getConstant(NVTBits - 1, dl, ShTy));

    unsigned Op1, Op2;
    switch (Opc) {
    default: llvm_unreachable("Unknown shift");
    case ISD::SHL:  Op1 = ISD::SHL; Op2 = ISD::SRL; break;
    case ISD::SRL:
    case ISD::SRA:  Op1 = ISD::SRL; Op2 = ISD::SHL; break;
    }

    // When shifting right the arithmetic for Lo and Hi is swapped.
    if (Opc != ISD::SHL)
      std::swap(InL, InH);

    // Use a little trick to get the bits that move from Lo to Hi. First
    // shift by one bit.
    SDValue Sh1 = DAG.getNode(Op2, dl, NVT, InL, DAG.getConstant(1, dl, ShTy));
    // Then compute the remaining shift with amount-1.
    SDValue Sh2 = DAG.getNode(Op2, dl, NVT, Sh1, Amt2);

    Lo = DAG.getNode(Opc, dl, NVT, InL, Amt);
    Hi = DAG.getNode(ISD::OR, dl, NVT, DAG.getNode(Op1, dl, NVT, InH, Amt),Sh2);

    if (Opc != ISD::SHL)
      std::swap(Hi, Lo);
    return true;
  }

  return false;
}

/// ExpandShiftWithUnknownAmountBit - Fully general expansion of integer shift
/// of any size.
bool DAGTypeLegalizer::
ExpandShiftWithUnknownAmountBit(SDNode *N, SDValue &Lo, SDValue &Hi) {
  SDValue Amt = N->getOperand(1);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  EVT ShTy = Amt.getValueType();
  unsigned NVTBits = NVT.getSizeInBits();
  assert(isPowerOf2_32(NVTBits) &&
         "Expanded integer type size not a power of two!");
  SDLoc dl(N);

  // Get the incoming operand to be shifted.
  SDValue InL, InH;
  GetExpandedInteger(N->getOperand(0), InL, InH);

  SDValue NVBitsNode = DAG.getConstant(NVTBits, dl, ShTy);
  SDValue AmtExcess = DAG.getNode(ISD::SUB, dl, ShTy, Amt, NVBitsNode);
  SDValue AmtLack = DAG.getNode(ISD::SUB, dl, ShTy, NVBitsNode, Amt);
  SDValue isShort = DAG.getSetCC(dl, getSetCCResultType(ShTy),
                                 Amt, NVBitsNode, ISD::SETULT);
  SDValue isZero = DAG.getSetCC(dl, getSetCCResultType(ShTy),
                                Amt, DAG.getConstant(0, dl, ShTy),
                                ISD::SETEQ);

  SDValue LoS, HiS, LoL, HiL;
  switch (N->getOpcode()) {
  default: llvm_unreachable("Unknown shift");
  case ISD::SHL:
    // Short: ShAmt < NVTBits
    LoS = DAG.getNode(ISD::SHL, dl, NVT, InL, Amt);
    HiS = DAG.getNode(ISD::OR, dl, NVT,
                      DAG.getNode(ISD::SHL, dl, NVT, InH, Amt),
                      DAG.getNode(ISD::SRL, dl, NVT, InL, AmtLack));

    // Long: ShAmt >= NVTBits
    LoL = DAG.getConstant(0, dl, NVT);                    // Lo part is zero.
    HiL = DAG.getNode(ISD::SHL, dl, NVT, InL, AmtExcess); // Hi from Lo part.

    Lo = DAG.getSelect(dl, NVT, isShort, LoS, LoL);
    Hi = DAG.getSelect(dl, NVT, isZero, InH,
                       DAG.getSelect(dl, NVT, isShort, HiS, HiL));
    return true;
  case ISD::SRL:
    // Short: ShAmt < NVTBits
    HiS = DAG.getNode(ISD::SRL, dl, NVT, InH, Amt);
    LoS = DAG.getNode(ISD::OR, dl, NVT,
                      DAG.getNode(ISD::SRL, dl, NVT, InL, Amt),
    // FIXME: If Amt is zero, the following shift generates an undefined result
    // on some architectures.
                      DAG.getNode(ISD::SHL, dl, NVT, InH, AmtLack));

    // Long: ShAmt >= NVTBits
    HiL = DAG.getConstant(0, dl, NVT);                    // Hi part is zero.
    LoL = DAG.getNode(ISD::SRL, dl, NVT, InH, AmtExcess); // Lo from Hi part.

    Lo = DAG.getSelect(dl, NVT, isZero, InL,
                       DAG.getSelect(dl, NVT, isShort, LoS, LoL));
    Hi = DAG.getSelect(dl, NVT, isShort, HiS, HiL);
    return true;
  case ISD::SRA:
    // Short: ShAmt < NVTBits
    HiS = DAG.getNode(ISD::SRA, dl, NVT, InH, Amt);
    LoS = DAG.getNode(ISD::OR, dl, NVT,
                      DAG.getNode(ISD::SRL, dl, NVT, InL, Amt),
                      DAG.getNode(ISD::SHL, dl, NVT, InH, AmtLack));

    // Long: ShAmt >= NVTBits
    HiL = DAG.getNode(ISD::SRA, dl, NVT, InH,             // Sign of Hi part.
                      DAG.getConstant(NVTBits - 1, dl, ShTy));
    LoL = DAG.getNode(ISD::SRA, dl, NVT, InH, AmtExcess); // Lo from Hi part.

    Lo = DAG.getSelect(dl, NVT, isZero, InL,
                       DAG.getSelect(dl, NVT, isShort, LoS, LoL));
    Hi = DAG.getSelect(dl, NVT, isShort, HiS, HiL);
    return true;
  }
}

static std::pair<ISD::CondCode, ISD::NodeType> getExpandedMinMaxOps(int Op) {

  switch (Op) {
    default: llvm_unreachable("invalid min/max opcode");
    case ISD::SMAX:
      return std::make_pair(ISD::SETGT, ISD::UMAX);
    case ISD::UMAX:
      return std::make_pair(ISD::SETUGT, ISD::UMAX);
    case ISD::SMIN:
      return std::make_pair(ISD::SETLT, ISD::UMIN);
    case ISD::UMIN:
      return std::make_pair(ISD::SETULT, ISD::UMIN);
  }
}

void DAGTypeLegalizer::ExpandIntRes_MINMAX(SDNode *N,
                                           SDValue &Lo, SDValue &Hi) {
  SDLoc DL(N);

  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  // If the upper halves are all sign bits, then we can perform the MINMAX on
  // the lower half and sign-extend the result to the upper half.
  unsigned NumBits = N->getValueType(0).getScalarSizeInBits();
  unsigned NumHalfBits = NumBits / 2;
  if (DAG.ComputeNumSignBits(LHS) > NumHalfBits &&
      DAG.ComputeNumSignBits(RHS) > NumHalfBits) {
    SDValue LHSL, LHSH, RHSL, RHSH;
    GetExpandedInteger(LHS, LHSL, LHSH);
    GetExpandedInteger(RHS, RHSL, RHSH);
    EVT NVT = LHSL.getValueType();

    Lo = DAG.getNode(N->getOpcode(), DL, NVT, LHSL, RHSL);
    Hi = DAG.getNode(ISD::SRA, DL, NVT, Lo,
                     DAG.getShiftAmountConstant(NumHalfBits - 1, NVT, DL));
    return;
  }

  // The Lo of smin(X, -1) is LHSL if X is negative. Otherwise it's -1.
  // The Lo of smax(X, 0) is 0 if X is negative. Otherwise it's LHSL.
  if ((N->getOpcode() == ISD::SMAX && isNullConstant(RHS)) ||
      (N->getOpcode() == ISD::SMIN && isAllOnesConstant(RHS))) {
    SDValue LHSL, LHSH, RHSL, RHSH;
    GetExpandedInteger(LHS, LHSL, LHSH);
    GetExpandedInteger(RHS, RHSL, RHSH);
    EVT NVT = LHSL.getValueType();
    EVT CCT = getSetCCResultType(NVT);

    SDValue HiNeg =
        DAG.getSetCC(DL, CCT, LHSH, DAG.getConstant(0, DL, NVT), ISD::SETLT);
    if (N->getOpcode() == ISD::SMIN) {
      Lo = DAG.getSelect(DL, NVT, HiNeg, LHSL, DAG.getConstant(-1, DL, NVT));
    } else {
      Lo = DAG.getSelect(DL, NVT, HiNeg, DAG.getConstant(0, DL, NVT), LHSL);
    }
    Hi = DAG.getNode(N->getOpcode(), DL, NVT, {LHSH, RHSH});
    return;
  }

  const APInt *RHSVal = nullptr;
  if (auto *RHSConst = dyn_cast<ConstantSDNode>(RHS))
    RHSVal = &RHSConst->getAPIntValue();

  // The high half of MIN/MAX is always just the the MIN/MAX of the
  // high halves of the operands.  Expand this way if it appears profitable.
  if (RHSVal && (N->getOpcode() == ISD::UMIN || N->getOpcode() == ISD::UMAX) &&
                 (RHSVal->countLeadingOnes() >= NumHalfBits ||
                  RHSVal->countLeadingZeros() >= NumHalfBits)) {
    SDValue LHSL, LHSH, RHSL, RHSH;
    GetExpandedInteger(LHS, LHSL, LHSH);
    GetExpandedInteger(RHS, RHSL, RHSH);
    EVT NVT = LHSL.getValueType();
    EVT CCT = getSetCCResultType(NVT);

    ISD::NodeType LoOpc;
    ISD::CondCode CondC;
    std::tie(CondC, LoOpc) = getExpandedMinMaxOps(N->getOpcode());

    Hi = DAG.getNode(N->getOpcode(), DL, NVT, {LHSH, RHSH});
    // We need to know whether to select Lo part that corresponds to 'winning'
    // Hi part or if Hi parts are equal.
    SDValue IsHiLeft = DAG.getSetCC(DL, CCT, LHSH, RHSH, CondC);
    SDValue IsHiEq = DAG.getSetCC(DL, CCT, LHSH, RHSH, ISD::SETEQ);

    // Lo part corresponding to the 'winning' Hi part
    SDValue LoCmp = DAG.getSelect(DL, NVT, IsHiLeft, LHSL, RHSL);

    // Recursed Lo part if Hi parts are equal, this uses unsigned version
    SDValue LoMinMax = DAG.getNode(LoOpc, DL, NVT, {LHSL, RHSL});

    Lo = DAG.getSelect(DL, NVT, IsHiEq, LoMinMax, LoCmp);
    return;
  }

  // Expand to "a < b ? a : b" etc.  Prefer ge/le if that simplifies
  // the compare.
  ISD::CondCode Pred;
  switch (N->getOpcode()) {
  default: llvm_unreachable("How did we get here?");
  case ISD::SMAX:
    if (RHSVal && RHSVal->countTrailingZeros() >= NumHalfBits)
      Pred = ISD::SETGE;
    else
      Pred = ISD::SETGT;
    break;
  case ISD::SMIN:
    if (RHSVal && RHSVal->countTrailingOnes() >= NumHalfBits)
      Pred = ISD::SETLE;
    else
      Pred = ISD::SETLT;
    break;
  case ISD::UMAX:
    if (RHSVal && RHSVal->countTrailingZeros() >= NumHalfBits)
      Pred = ISD::SETUGE;
    else
      Pred = ISD::SETUGT;
    break;
  case ISD::UMIN:
    if (RHSVal && RHSVal->countTrailingOnes() >= NumHalfBits)
      Pred = ISD::SETULE;
    else
      Pred = ISD::SETULT;
    break;
  }
  EVT VT = N->getValueType(0);
  EVT CCT = getSetCCResultType(VT);
  SDValue Cond = DAG.getSetCC(DL, CCT, LHS, RHS, Pred);
  SDValue Result = DAG.getSelect(DL, VT, Cond, LHS, RHS);
  SplitInteger(Result, Lo, Hi);
}

void DAGTypeLegalizer::ExpandIntRes_CMP(SDNode *N, SDValue &Lo, SDValue &Hi) {
  SDValue ExpandedCMP = TLI.expandCMP(N, DAG);
  SplitInteger(ExpandedCMP, Lo, Hi);
}

void DAGTypeLegalizer::ExpandIntRes_ADDSUB(SDNode *N,
                                           SDValue &Lo, SDValue &Hi) {
  SDLoc dl(N);
  // Expand the subcomponents.
  SDValue LHSL, LHSH, RHSL, RHSH;
  GetExpandedInteger(N->getOperand(0), LHSL, LHSH);
  GetExpandedInteger(N->getOperand(1), RHSL, RHSH);

  EVT NVT = LHSL.getValueType();
  SDValue LoOps[2] = { LHSL, RHSL };
  SDValue HiOps[3] = { LHSH, RHSH };

  bool HasOpCarry = TLI.isOperationLegalOrCustom(
      N->getOpcode() == ISD::ADD ? ISD::UADDO_CARRY : ISD::USUBO_CARRY,
      TLI.getTypeToExpandTo(*DAG.getContext(), NVT));
  if (HasOpCarry) {
    SDVTList VTList = DAG.getVTList(NVT, getSetCCResultType(NVT));
    if (N->getOpcode() == ISD::ADD) {
      Lo = DAG.getNode(ISD::UADDO, dl, VTList, LoOps);
      HiOps[2] = Lo.getValue(1);
      Hi = DAG.computeKnownBits(HiOps[2]).isZero()
               ? DAG.getNode(ISD::UADDO, dl, VTList, ArrayRef(HiOps, 2))
               : DAG.getNode(ISD::UADDO_CARRY, dl, VTList, HiOps);
    } else {
      Lo = DAG.getNode(ISD::USUBO, dl, VTList, LoOps);
      HiOps[2] = Lo.getValue(1);
      Hi = DAG.computeKnownBits(HiOps[2]).isZero()
               ? DAG.getNode(ISD::USUBO, dl, VTList, ArrayRef(HiOps, 2))
               : DAG.getNode(ISD::USUBO_CARRY, dl, VTList, HiOps);
    }
    return;
  }

  // Do not generate ADDC/ADDE or SUBC/SUBE if the target does not support
  // them.  TODO: Teach operation legalization how to expand unsupported
  // ADDC/ADDE/SUBC/SUBE.  The problem is that these operations generate
  // a carry of type MVT::Glue, but there doesn't seem to be any way to
  // generate a value of this type in the expanded code sequence.
  bool hasCarry =
    TLI.isOperationLegalOrCustom(N->getOpcode() == ISD::ADD ?
                                   ISD::ADDC : ISD::SUBC,
                                 TLI.getTypeToExpandTo(*DAG.getContext(), NVT));

  if (hasCarry) {
    SDVTList VTList = DAG.getVTList(NVT, MVT::Glue);
    if (N->getOpcode() == ISD::ADD) {
      Lo = DAG.getNode(ISD::ADDC, dl, VTList, LoOps);
      HiOps[2] = Lo.getValue(1);
      Hi = DAG.getNode(ISD::ADDE, dl, VTList, HiOps);
    } else {
      Lo = DAG.getNode(ISD::SUBC, dl, VTList, LoOps);
      HiOps[2] = Lo.getValue(1);
      Hi = DAG.getNode(ISD::SUBE, dl, VTList, HiOps);
    }
    return;
  }

  bool hasOVF =
    TLI.isOperationLegalOrCustom(N->getOpcode() == ISD::ADD ?
                                   ISD::UADDO : ISD::USUBO,
                                 TLI.getTypeToExpandTo(*DAG.getContext(), NVT));
  TargetLoweringBase::BooleanContent BoolType = TLI.getBooleanContents(NVT);

  if (hasOVF) {
    EVT OvfVT = getSetCCResultType(NVT);
    SDVTList VTList = DAG.getVTList(NVT, OvfVT);
    int RevOpc;
    if (N->getOpcode() == ISD::ADD) {
      RevOpc = ISD::SUB;
      Lo = DAG.getNode(ISD::UADDO, dl, VTList, LoOps);
      Hi = DAG.getNode(ISD::ADD, dl, NVT, ArrayRef(HiOps, 2));
    } else {
      RevOpc = ISD::ADD;
      Lo = DAG.getNode(ISD::USUBO, dl, VTList, LoOps);
      Hi = DAG.getNode(ISD::SUB, dl, NVT, ArrayRef(HiOps, 2));
    }
    SDValue OVF = Lo.getValue(1);

    switch (BoolType) {
    case TargetLoweringBase::UndefinedBooleanContent:
      OVF = DAG.getNode(ISD::AND, dl, OvfVT, DAG.getConstant(1, dl, OvfVT), OVF);
      [[fallthrough]];
    case TargetLoweringBase::ZeroOrOneBooleanContent:
      OVF = DAG.getZExtOrTrunc(OVF, dl, NVT);
      Hi = DAG.getNode(N->getOpcode(), dl, NVT, Hi, OVF);
      break;
    case TargetLoweringBase::ZeroOrNegativeOneBooleanContent:
      OVF = DAG.getSExtOrTrunc(OVF, dl, NVT);
      Hi = DAG.getNode(RevOpc, dl, NVT, Hi, OVF);
    }
    return;
  }

  if (N->getOpcode() == ISD::ADD) {
    Lo = DAG.getNode(ISD::ADD, dl, NVT, LoOps);
    Hi = DAG.getNode(ISD::ADD, dl, NVT, ArrayRef(HiOps, 2));
    SDValue Cmp;
    // Special case: X+1 has a carry out if X+1==0. This may reduce the live
    // range of X. We assume comparing with 0 is cheap.
    if (isOneConstant(LoOps[1]))
      Cmp = DAG.getSetCC(dl, getSetCCResultType(NVT), Lo,
                         DAG.getConstant(0, dl, NVT), ISD::SETEQ);
    else if (isAllOnesConstant(LoOps[1])) {
      if (isAllOnesConstant(HiOps[1]))
        Cmp = DAG.getSetCC(dl, getSetCCResultType(NVT), LoOps[0],
                           DAG.getConstant(0, dl, NVT), ISD::SETEQ);
      else
        Cmp = DAG.getSetCC(dl, getSetCCResultType(NVT), LoOps[0],
                           DAG.getConstant(0, dl, NVT), ISD::SETNE);
    } else
      Cmp = DAG.getSetCC(dl, getSetCCResultType(NVT), Lo, LoOps[0],
                         ISD::SETULT);

    SDValue Carry;
    if (BoolType == TargetLoweringBase::ZeroOrOneBooleanContent)
      Carry = DAG.getZExtOrTrunc(Cmp, dl, NVT);
    else
      Carry = DAG.getSelect(dl, NVT, Cmp, DAG.getConstant(1, dl, NVT),
                             DAG.getConstant(0, dl, NVT));

    if (isAllOnesConstant(LoOps[1]) && isAllOnesConstant(HiOps[1]))
      Hi = DAG.getNode(ISD::SUB, dl, NVT, HiOps[0], Carry);
    else
      Hi = DAG.getNode(ISD::ADD, dl, NVT, Hi, Carry);
  } else {
    Lo = DAG.getNode(ISD::SUB, dl, NVT, LoOps);
    Hi = DAG.getNode(ISD::SUB, dl, NVT, ArrayRef(HiOps, 2));
    SDValue Cmp =
      DAG.getSetCC(dl, getSetCCResultType(LoOps[0].getValueType()),
                   LoOps[0], LoOps[1], ISD::SETULT);

    SDValue Borrow;
    if (BoolType == TargetLoweringBase::ZeroOrOneBooleanContent)
      Borrow = DAG.getZExtOrTrunc(Cmp, dl, NVT);
    else
      Borrow = DAG.getSelect(dl, NVT, Cmp, DAG.getConstant(1, dl, NVT),
                             DAG.getConstant(0, dl, NVT));

    Hi = DAG.getNode(ISD::SUB, dl, NVT, Hi, Borrow);
  }
}

void DAGTypeLegalizer::ExpandIntRes_ADDSUBC(SDNode *N,
                                            SDValue &Lo, SDValue &Hi) {
  // Expand the subcomponents.
  SDValue LHSL, LHSH, RHSL, RHSH;
  SDLoc dl(N);
  GetExpandedInteger(N->getOperand(0), LHSL, LHSH);
  GetExpandedInteger(N->getOperand(1), RHSL, RHSH);
  SDVTList VTList = DAG.getVTList(LHSL.getValueType(), MVT::Glue);
  SDValue LoOps[2] = { LHSL, RHSL };
  SDValue HiOps[3] = { LHSH, RHSH };

  if (N->getOpcode() == ISD::ADDC) {
    Lo = DAG.getNode(ISD::ADDC, dl, VTList, LoOps);
    HiOps[2] = Lo.getValue(1);
    Hi = DAG.getNode(ISD::ADDE, dl, VTList, HiOps);
  } else {
    Lo = DAG.getNode(ISD::SUBC, dl, VTList, LoOps);
    HiOps[2] = Lo.getValue(1);
    Hi = DAG.getNode(ISD::SUBE, dl, VTList, HiOps);
  }

  // Legalized the flag result - switch anything that used the old flag to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Hi.getValue(1));
}

void DAGTypeLegalizer::ExpandIntRes_ADDSUBE(SDNode *N,
                                            SDValue &Lo, SDValue &Hi) {
  // Expand the subcomponents.
  SDValue LHSL, LHSH, RHSL, RHSH;
  SDLoc dl(N);
  GetExpandedInteger(N->getOperand(0), LHSL, LHSH);
  GetExpandedInteger(N->getOperand(1), RHSL, RHSH);
  SDVTList VTList = DAG.getVTList(LHSL.getValueType(), MVT::Glue);
  SDValue LoOps[3] = { LHSL, RHSL, N->getOperand(2) };
  SDValue HiOps[3] = { LHSH, RHSH };

  Lo = DAG.getNode(N->getOpcode(), dl, VTList, LoOps);
  HiOps[2] = Lo.getValue(1);
  Hi = DAG.getNode(N->getOpcode(), dl, VTList, HiOps);

  // Legalized the flag result - switch anything that used the old flag to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Hi.getValue(1));
}

void DAGTypeLegalizer::ExpandIntRes_UADDSUBO(SDNode *N,
                                             SDValue &Lo, SDValue &Hi) {
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  SDLoc dl(N);

  SDValue Ovf;

  unsigned CarryOp, NoCarryOp;
  ISD::CondCode Cond;
  switch(N->getOpcode()) {
    case ISD::UADDO:
      CarryOp = ISD::UADDO_CARRY;
      NoCarryOp = ISD::ADD;
      Cond = ISD::SETULT;
      break;
    case ISD::USUBO:
      CarryOp = ISD::USUBO_CARRY;
      NoCarryOp = ISD::SUB;
      Cond = ISD::SETUGT;
      break;
    default:
      llvm_unreachable("Node has unexpected Opcode");
  }

  bool HasCarryOp = TLI.isOperationLegalOrCustom(
      CarryOp, TLI.getTypeToExpandTo(*DAG.getContext(), LHS.getValueType()));

  if (HasCarryOp) {
    // Expand the subcomponents.
    SDValue LHSL, LHSH, RHSL, RHSH;
    GetExpandedInteger(LHS, LHSL, LHSH);
    GetExpandedInteger(RHS, RHSL, RHSH);
    SDVTList VTList = DAG.getVTList(LHSL.getValueType(), N->getValueType(1));
    SDValue LoOps[2] = { LHSL, RHSL };
    SDValue HiOps[3] = { LHSH, RHSH };

    Lo = DAG.getNode(N->getOpcode(), dl, VTList, LoOps);
    HiOps[2] = Lo.getValue(1);
    Hi = DAG.getNode(CarryOp, dl, VTList, HiOps);

    Ovf = Hi.getValue(1);
  } else {
    // Expand the result by simply replacing it with the equivalent
    // non-overflow-checking operation.
    SDValue Sum = DAG.getNode(NoCarryOp, dl, LHS.getValueType(), LHS, RHS);
    SplitInteger(Sum, Lo, Hi);

    if (N->getOpcode() == ISD::UADDO && isOneConstant(RHS)) {
      // Special case: uaddo X, 1 overflowed if X+1 == 0. We can detect this
      // with (Lo | Hi) == 0.
      SDValue Or = DAG.getNode(ISD::OR, dl, Lo.getValueType(), Lo, Hi);
      Ovf = DAG.getSetCC(dl, N->getValueType(1), Or,
                         DAG.getConstant(0, dl, Lo.getValueType()), ISD::SETEQ);
    } else if (N->getOpcode() == ISD::UADDO && isAllOnesConstant(RHS)) {
      // Special case: uaddo X, -1 overflows if X == 0.
      Ovf =
          DAG.getSetCC(dl, N->getValueType(1), LHS,
                       DAG.getConstant(0, dl, LHS.getValueType()), ISD::SETNE);
    } else {
      // Calculate the overflow: addition overflows iff a + b < a, and
      // subtraction overflows iff a - b > a.
      Ovf = DAG.getSetCC(dl, N->getValueType(1), Sum, LHS, Cond);
    }
  }

  // Legalized the flag result - switch anything that used the old flag to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Ovf);
}

void DAGTypeLegalizer::ExpandIntRes_UADDSUBO_CARRY(SDNode *N, SDValue &Lo,
                                                   SDValue &Hi) {
  // Expand the subcomponents.
  SDValue LHSL, LHSH, RHSL, RHSH;
  SDLoc dl(N);
  GetExpandedInteger(N->getOperand(0), LHSL, LHSH);
  GetExpandedInteger(N->getOperand(1), RHSL, RHSH);
  SDVTList VTList = DAG.getVTList(LHSL.getValueType(), N->getValueType(1));
  SDValue LoOps[3] = { LHSL, RHSL, N->getOperand(2) };
  SDValue HiOps[3] = { LHSH, RHSH, SDValue() };

  Lo = DAG.getNode(N->getOpcode(), dl, VTList, LoOps);
  HiOps[2] = Lo.getValue(1);
  Hi = DAG.getNode(N->getOpcode(), dl, VTList, HiOps);

  // Legalized the flag result - switch anything that used the old flag to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Hi.getValue(1));
}

void DAGTypeLegalizer::ExpandIntRes_SADDSUBO_CARRY(SDNode *N,
                                                   SDValue &Lo, SDValue &Hi) {
  // Expand the subcomponents.
  SDValue LHSL, LHSH, RHSL, RHSH;
  SDLoc dl(N);
  GetExpandedInteger(N->getOperand(0), LHSL, LHSH);
  GetExpandedInteger(N->getOperand(1), RHSL, RHSH);
  SDVTList VTList = DAG.getVTList(LHSL.getValueType(), N->getValueType(1));

  // We need to use an unsigned carry op for the lo part.
  unsigned CarryOp =
      N->getOpcode() == ISD::SADDO_CARRY ? ISD::UADDO_CARRY : ISD::USUBO_CARRY;
  Lo = DAG.getNode(CarryOp, dl, VTList, { LHSL, RHSL, N->getOperand(2) });
  Hi = DAG.getNode(N->getOpcode(), dl, VTList, { LHSH, RHSH, Lo.getValue(1) });

  // Legalized the flag result - switch anything that used the old flag to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Hi.getValue(1));
}

void DAGTypeLegalizer::ExpandIntRes_ANY_EXTEND(SDNode *N,
                                               SDValue &Lo, SDValue &Hi) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDLoc dl(N);
  SDValue Op = N->getOperand(0);
  if (Op.getValueType().bitsLE(NVT)) {
    // The low part is any extension of the input (which degenerates to a copy).
    Lo = DAG.getNode(ISD::ANY_EXTEND, dl, NVT, Op);
    Hi = DAG.getUNDEF(NVT);   // The high part is undefined.
  } else {
    // For example, extension of an i48 to an i64.  The operand type necessarily
    // promotes to the result type, so will end up being expanded too.
    assert(getTypeAction(Op.getValueType()) ==
           TargetLowering::TypePromoteInteger &&
           "Only know how to promote this result!");
    SDValue Res = GetPromotedInteger(Op);
    assert(Res.getValueType() == N->getValueType(0) &&
           "Operand over promoted?");
    // Split the promoted operand.  This will simplify when it is expanded.
    SplitInteger(Res, Lo, Hi);
  }
}

void DAGTypeLegalizer::ExpandIntRes_AssertSext(SDNode *N,
                                               SDValue &Lo, SDValue &Hi) {
  SDLoc dl(N);
  GetExpandedInteger(N->getOperand(0), Lo, Hi);
  EVT NVT = Lo.getValueType();
  EVT EVT = cast<VTSDNode>(N->getOperand(1))->getVT();
  unsigned NVTBits = NVT.getSizeInBits();
  unsigned EVTBits = EVT.getSizeInBits();

  if (NVTBits < EVTBits) {
    Hi = DAG.getNode(ISD::AssertSext, dl, NVT, Hi,
                     DAG.getValueType(EVT::getIntegerVT(*DAG.getContext(),
                                                        EVTBits - NVTBits)));
  } else {
    Lo = DAG.getNode(ISD::AssertSext, dl, NVT, Lo, DAG.getValueType(EVT));
    // The high part replicates the sign bit of Lo, make it explicit.
    Hi = DAG.getNode(ISD::SRA, dl, NVT, Lo,
                     DAG.getConstant(NVTBits - 1, dl,
                                     TLI.getPointerTy(DAG.getDataLayout())));
  }
}

void DAGTypeLegalizer::ExpandIntRes_AssertZext(SDNode *N,
                                               SDValue &Lo, SDValue &Hi) {
  SDLoc dl(N);
  GetExpandedInteger(N->getOperand(0), Lo, Hi);
  EVT NVT = Lo.getValueType();
  EVT EVT = cast<VTSDNode>(N->getOperand(1))->getVT();
  unsigned NVTBits = NVT.getSizeInBits();
  unsigned EVTBits = EVT.getSizeInBits();

  if (NVTBits < EVTBits) {
    Hi = DAG.getNode(ISD::AssertZext, dl, NVT, Hi,
                     DAG.getValueType(EVT::getIntegerVT(*DAG.getContext(),
                                                        EVTBits - NVTBits)));
  } else {
    Lo = DAG.getNode(ISD::AssertZext, dl, NVT, Lo, DAG.getValueType(EVT));
    // The high part must be zero, make it explicit.
    Hi = DAG.getConstant(0, dl, NVT);
  }
}

void DAGTypeLegalizer::ExpandIntRes_BITREVERSE(SDNode *N,
                                               SDValue &Lo, SDValue &Hi) {
  SDLoc dl(N);
  GetExpandedInteger(N->getOperand(0), Hi, Lo);  // Note swapped operands.
  Lo = DAG.getNode(ISD::BITREVERSE, dl, Lo.getValueType(), Lo);
  Hi = DAG.getNode(ISD::BITREVERSE, dl, Hi.getValueType(), Hi);
}

void DAGTypeLegalizer::ExpandIntRes_BSWAP(SDNode *N,
                                          SDValue &Lo, SDValue &Hi) {
  SDLoc dl(N);
  GetExpandedInteger(N->getOperand(0), Hi, Lo);  // Note swapped operands.
  Lo = DAG.getNode(ISD::BSWAP, dl, Lo.getValueType(), Lo);
  Hi = DAG.getNode(ISD::BSWAP, dl, Hi.getValueType(), Hi);
}

void DAGTypeLegalizer::ExpandIntRes_PARITY(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  SDLoc dl(N);
  // parity(HiLo) -> parity(Lo^Hi)
  GetExpandedInteger(N->getOperand(0), Lo, Hi);
  EVT NVT = Lo.getValueType();
  Lo =
      DAG.getNode(ISD::PARITY, dl, NVT, DAG.getNode(ISD::XOR, dl, NVT, Lo, Hi));
  Hi = DAG.getConstant(0, dl, NVT);
}

void DAGTypeLegalizer::ExpandIntRes_Constant(SDNode *N,
                                             SDValue &Lo, SDValue &Hi) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  unsigned NBitWidth = NVT.getSizeInBits();
  auto Constant = cast<ConstantSDNode>(N);
  const APInt &Cst = Constant->getAPIntValue();
  bool IsTarget = Constant->isTargetOpcode();
  bool IsOpaque = Constant->isOpaque();
  SDLoc dl(N);
  Lo = DAG.getConstant(Cst.trunc(NBitWidth), dl, NVT, IsTarget, IsOpaque);
  Hi = DAG.getConstant(Cst.lshr(NBitWidth).trunc(NBitWidth), dl, NVT, IsTarget,
                       IsOpaque);
}

void DAGTypeLegalizer::ExpandIntRes_ABS(SDNode *N, SDValue &Lo, SDValue &Hi) {
  SDLoc dl(N);

  SDValue N0 = N->getOperand(0);
  GetExpandedInteger(N0, Lo, Hi);
  EVT NVT = Lo.getValueType();

  // If the upper half is all sign bits, then we can perform the ABS on the
  // lower half and zero-extend.
  if (DAG.ComputeNumSignBits(N0) > NVT.getScalarSizeInBits()) {
    Lo = DAG.getNode(ISD::ABS, dl, NVT, Lo);
    Hi = DAG.getConstant(0, dl, NVT);
    return;
  }

  // If we have USUBO_CARRY, use the expanded form of the sra+xor+sub sequence
  // we use in LegalizeDAG. The SUB part of the expansion is based on
  // ExpandIntRes_ADDSUB which also uses USUBO_CARRY/USUBO after checking that
  // USUBO_CARRY is LegalOrCustom. Each of the pieces here can be further
  // expanded if needed. Shift expansion has a special case for filling with
  // sign bits so that we will only end up with one SRA.
  bool HasSubCarry = TLI.isOperationLegalOrCustom(
      ISD::USUBO_CARRY, TLI.getTypeToExpandTo(*DAG.getContext(), NVT));
  if (HasSubCarry) {
    SDValue Sign = DAG.getNode(
        ISD::SRA, dl, NVT, Hi,
        DAG.getShiftAmountConstant(NVT.getSizeInBits() - 1, NVT, dl));
    SDVTList VTList = DAG.getVTList(NVT, getSetCCResultType(NVT));
    Lo = DAG.getNode(ISD::XOR, dl, NVT, Lo, Sign);
    Hi = DAG.getNode(ISD::XOR, dl, NVT, Hi, Sign);
    Lo = DAG.getNode(ISD::USUBO, dl, VTList, Lo, Sign);
    Hi = DAG.getNode(ISD::USUBO_CARRY, dl, VTList, Hi, Sign, Lo.getValue(1));
    return;
  }

  // abs(HiLo) -> (Hi < 0 ? -HiLo : HiLo)
  EVT VT = N->getValueType(0);
  SDValue Neg = DAG.getNode(ISD::SUB, dl, VT,
                            DAG.getConstant(0, dl, VT), N0);
  SDValue NegLo, NegHi;
  SplitInteger(Neg, NegLo, NegHi);

  SDValue HiIsNeg = DAG.getSetCC(dl, getSetCCResultType(NVT), Hi,
                                 DAG.getConstant(0, dl, NVT), ISD::SETLT);
  Lo = DAG.getSelect(dl, NVT, HiIsNeg, NegLo, Lo);
  Hi = DAG.getSelect(dl, NVT, HiIsNeg, NegHi, Hi);
}

void DAGTypeLegalizer::ExpandIntRes_CTLZ(SDNode *N,
                                         SDValue &Lo, SDValue &Hi) {
  SDLoc dl(N);
  // ctlz (HiLo) -> Hi != 0 ? ctlz(Hi) : (ctlz(Lo)+32)
  GetExpandedInteger(N->getOperand(0), Lo, Hi);
  EVT NVT = Lo.getValueType();

  SDValue HiNotZero = DAG.getSetCC(dl, getSetCCResultType(NVT), Hi,
                                   DAG.getConstant(0, dl, NVT), ISD::SETNE);

  SDValue LoLZ = DAG.getNode(N->getOpcode(), dl, NVT, Lo);
  SDValue HiLZ = DAG.getNode(ISD::CTLZ_ZERO_UNDEF, dl, NVT, Hi);

  Lo = DAG.getSelect(dl, NVT, HiNotZero, HiLZ,
                     DAG.getNode(ISD::ADD, dl, NVT, LoLZ,
                                 DAG.getConstant(NVT.getSizeInBits(), dl,
                                                 NVT)));
  Hi = DAG.getConstant(0, dl, NVT);
}

void DAGTypeLegalizer::ExpandIntRes_CTPOP(SDNode *N,
                                          SDValue &Lo, SDValue &Hi) {
  SDLoc dl(N);
  // ctpop(HiLo) -> ctpop(Hi)+ctpop(Lo)
  GetExpandedInteger(N->getOperand(0), Lo, Hi);
  EVT NVT = Lo.getValueType();
  Lo = DAG.getNode(ISD::ADD, dl, NVT, DAG.getNode(ISD::CTPOP, dl, NVT, Lo),
                   DAG.getNode(ISD::CTPOP, dl, NVT, Hi));
  Hi = DAG.getConstant(0, dl, NVT);
}

void DAGTypeLegalizer::ExpandIntRes_CTTZ(SDNode *N,
                                         SDValue &Lo, SDValue &Hi) {
  SDLoc dl(N);
  // cttz (HiLo) -> Lo != 0 ? cttz(Lo) : (cttz(Hi)+32)
  GetExpandedInteger(N->getOperand(0), Lo, Hi);
  EVT NVT = Lo.getValueType();

  SDValue LoNotZero = DAG.getSetCC(dl, getSetCCResultType(NVT), Lo,
                                   DAG.getConstant(0, dl, NVT), ISD::SETNE);

  SDValue LoLZ = DAG.getNode(ISD::CTTZ_ZERO_UNDEF, dl, NVT, Lo);
  SDValue HiLZ = DAG.getNode(N->getOpcode(), dl, NVT, Hi);

  Lo = DAG.getSelect(dl, NVT, LoNotZero, LoLZ,
                     DAG.getNode(ISD::ADD, dl, NVT, HiLZ,
                                 DAG.getConstant(NVT.getSizeInBits(), dl,
                                                 NVT)));
  Hi = DAG.getConstant(0, dl, NVT);
}

void DAGTypeLegalizer::ExpandIntRes_GET_ROUNDING(SDNode *N, SDValue &Lo,
                                               SDValue &Hi) {
  SDLoc dl(N);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  unsigned NBitWidth = NVT.getSizeInBits();

  Lo = DAG.getNode(ISD::GET_ROUNDING, dl, {NVT, MVT::Other}, N->getOperand(0));
  SDValue Chain = Lo.getValue(1);
  // The high part is the sign of Lo, as -1 is a valid value for GET_ROUNDING
  Hi = DAG.getNode(ISD::SRA, dl, NVT, Lo,
                   DAG.getShiftAmountConstant(NBitWidth - 1, NVT, dl));

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Chain);
}

// Helper for producing an FP_EXTEND/STRICT_FP_EXTEND of Op.
static SDValue fpExtendHelper(SDValue Op, SDValue &Chain, bool IsStrict, EVT VT,
                              SDLoc DL, SelectionDAG &DAG) {
  if (IsStrict) {
    Op = DAG.getNode(ISD::STRICT_FP_EXTEND, DL, {VT, MVT::Other}, {Chain, Op});
    Chain = Op.getValue(1);
    return Op;
  }
  return DAG.getNode(ISD::FP_EXTEND, DL, VT, Op);
}

void DAGTypeLegalizer::ExpandIntRes_FP_TO_XINT(SDNode *N, SDValue &Lo,
                                               SDValue &Hi) {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);

  bool IsSigned = N->getOpcode() == ISD::FP_TO_SINT ||
                  N->getOpcode() == ISD::STRICT_FP_TO_SINT;
  bool IsStrict = N->isStrictFPOpcode();
  SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();
  SDValue Op = N->getOperand(IsStrict ? 1 : 0);
  if (getTypeAction(Op.getValueType()) == TargetLowering::TypePromoteFloat)
    Op = GetPromotedFloat(Op);

  if (getTypeAction(Op.getValueType()) == TargetLowering::TypeSoftPromoteHalf) {
    EVT OFPVT = Op.getValueType();
    EVT NFPVT = TLI.getTypeToTransformTo(*DAG.getContext(), OFPVT);
    Op = GetSoftPromotedHalf(Op);
    Op = DAG.getNode(OFPVT == MVT::f16 ? ISD::FP16_TO_FP : ISD::BF16_TO_FP, dl,
                     NFPVT, Op);
    Op = DAG.getNode(IsSigned ? ISD::FP_TO_SINT : ISD::FP_TO_UINT, dl, VT, Op);
    SplitInteger(Op, Lo, Hi);
    return;
  }

  if (Op.getValueType() == MVT::bf16) {
    // Extend to f32 as there is no bf16 libcall.
    Op = fpExtendHelper(Op, Chain, IsStrict, MVT::f32, dl, DAG);
  }

  RTLIB::Libcall LC = IsSigned ? RTLIB::getFPTOSINT(Op.getValueType(), VT)
                               : RTLIB::getFPTOUINT(Op.getValueType(), VT);
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unexpected fp-to-xint conversion!");
  TargetLowering::MakeLibCallOptions CallOptions;
  CallOptions.setSExt(true);
  std::pair<SDValue, SDValue> Tmp = TLI.makeLibCall(DAG, LC, VT, Op,
                                                    CallOptions, dl, Chain);
  SplitInteger(Tmp.first, Lo, Hi);

  if (IsStrict)
    ReplaceValueWith(SDValue(N, 1), Tmp.second);
}

void DAGTypeLegalizer::ExpandIntRes_FP_TO_XINT_SAT(SDNode *N, SDValue &Lo,
                                                   SDValue &Hi) {
  SDValue Res = TLI.expandFP_TO_INT_SAT(N, DAG);
  SplitInteger(Res, Lo, Hi);
}

void DAGTypeLegalizer::ExpandIntRes_XROUND_XRINT(SDNode *N, SDValue &Lo,
                                                 SDValue &Hi) {
  SDLoc dl(N);
  bool IsStrict = N->isStrictFPOpcode();
  SDValue Op = N->getOperand(IsStrict ? 1 : 0);
  SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();

  assert(getTypeAction(Op.getValueType()) != TargetLowering::TypePromoteFloat &&
         "Input type needs to be promoted!");

  EVT VT = Op.getValueType();

  if (VT == MVT::f16) {
    // Extend to f32.
    VT = MVT::f32;
    Op = fpExtendHelper(Op, Chain, IsStrict, VT, dl, DAG);
  }

  RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
  if (N->getOpcode() == ISD::LROUND ||
      N->getOpcode() == ISD::STRICT_LROUND) {
    if (VT == MVT::f32)
      LC = RTLIB::LROUND_F32;
    else if (VT == MVT::f64)
      LC = RTLIB::LROUND_F64;
    else if (VT == MVT::f80)
      LC = RTLIB::LROUND_F80;
    else if (VT == MVT::f128)
      LC = RTLIB::LROUND_F128;
    else if (VT == MVT::ppcf128)
      LC = RTLIB::LROUND_PPCF128;
    assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unexpected lround input type!");
  } else if (N->getOpcode() == ISD::LRINT ||
             N->getOpcode() == ISD::STRICT_LRINT) {
    if (VT == MVT::f32)
      LC = RTLIB::LRINT_F32;
    else if (VT == MVT::f64)
      LC = RTLIB::LRINT_F64;
    else if (VT == MVT::f80)
      LC = RTLIB::LRINT_F80;
    else if (VT == MVT::f128)
      LC = RTLIB::LRINT_F128;
    else if (VT == MVT::ppcf128)
      LC = RTLIB::LRINT_PPCF128;
    assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unexpected lrint input type!");
  } else if (N->getOpcode() == ISD::LLROUND ||
      N->getOpcode() == ISD::STRICT_LLROUND) {
    if (VT == MVT::f32)
      LC = RTLIB::LLROUND_F32;
    else if (VT == MVT::f64)
      LC = RTLIB::LLROUND_F64;
    else if (VT == MVT::f80)
      LC = RTLIB::LLROUND_F80;
    else if (VT == MVT::f128)
      LC = RTLIB::LLROUND_F128;
    else if (VT == MVT::ppcf128)
      LC = RTLIB::LLROUND_PPCF128;
    assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unexpected llround input type!");
  } else if (N->getOpcode() == ISD::LLRINT ||
             N->getOpcode() == ISD::STRICT_LLRINT) {
    if (VT == MVT::f32)
      LC = RTLIB::LLRINT_F32;
    else if (VT == MVT::f64)
      LC = RTLIB::LLRINT_F64;
    else if (VT == MVT::f80)
      LC = RTLIB::LLRINT_F80;
    else if (VT == MVT::f128)
      LC = RTLIB::LLRINT_F128;
    else if (VT == MVT::ppcf128)
      LC = RTLIB::LLRINT_PPCF128;
    assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unexpected llrint input type!");
  } else
    llvm_unreachable("Unexpected opcode!");

  EVT RetVT = N->getValueType(0);

  TargetLowering::MakeLibCallOptions CallOptions;
  CallOptions.setSExt(true);
  std::pair<SDValue, SDValue> Tmp = TLI.makeLibCall(DAG, LC, RetVT,
                                                    Op, CallOptions, dl,
                                                    Chain);
  SplitInteger(Tmp.first, Lo, Hi);

  if (N->isStrictFPOpcode())
    ReplaceValueWith(SDValue(N, 1), Tmp.second);
}

void DAGTypeLegalizer::ExpandIntRes_LOAD(LoadSDNode *N,
                                         SDValue &Lo, SDValue &Hi) {
  assert(!N->isAtomic() && "Should have been a ATOMIC_LOAD?");

  if (ISD::isNormalLoad(N)) {
    ExpandRes_NormalLoad(N, Lo, Hi);
    return;
  }

  assert(ISD::isUNINDEXEDLoad(N) && "Indexed load during type legalization!");

  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue Ch  = N->getChain();
  SDValue Ptr = N->getBasePtr();
  ISD::LoadExtType ExtType = N->getExtensionType();
  MachineMemOperand::Flags MMOFlags = N->getMemOperand()->getFlags();
  AAMDNodes AAInfo = N->getAAInfo();
  SDLoc dl(N);

  assert(NVT.isByteSized() && "Expanded type not byte sized!");

  if (N->getMemoryVT().bitsLE(NVT)) {
    EVT MemVT = N->getMemoryVT();

    Lo = DAG.getExtLoad(ExtType, dl, NVT, Ch, Ptr, N->getPointerInfo(), MemVT,
                        N->getOriginalAlign(), MMOFlags, AAInfo);

    // Remember the chain.
    Ch = Lo.getValue(1);

    if (ExtType == ISD::SEXTLOAD) {
      // The high part is obtained by SRA'ing all but one of the bits of the
      // lo part.
      unsigned LoSize = Lo.getValueSizeInBits();
      Hi = DAG.getNode(ISD::SRA, dl, NVT, Lo,
                       DAG.getConstant(LoSize - 1, dl,
                                       TLI.getPointerTy(DAG.getDataLayout())));
    } else if (ExtType == ISD::ZEXTLOAD) {
      // The high part is just a zero.
      Hi = DAG.getConstant(0, dl, NVT);
    } else {
      assert(ExtType == ISD::EXTLOAD && "Unknown extload!");
      // The high part is undefined.
      Hi = DAG.getUNDEF(NVT);
    }
  } else if (DAG.getDataLayout().isLittleEndian()) {
    // Little-endian - low bits are at low addresses.
    Lo = DAG.getLoad(NVT, dl, Ch, Ptr, N->getPointerInfo(),
                     N->getOriginalAlign(), MMOFlags, AAInfo);

    unsigned ExcessBits =
      N->getMemoryVT().getSizeInBits() - NVT.getSizeInBits();
    EVT NEVT = EVT::getIntegerVT(*DAG.getContext(), ExcessBits);

    // Increment the pointer to the other half.
    unsigned IncrementSize = NVT.getSizeInBits()/8;
    Ptr = DAG.getMemBasePlusOffset(Ptr, TypeSize::getFixed(IncrementSize), dl);
    Hi = DAG.getExtLoad(ExtType, dl, NVT, Ch, Ptr,
                        N->getPointerInfo().getWithOffset(IncrementSize), NEVT,
                        N->getOriginalAlign(), MMOFlags, AAInfo);

    // Build a factor node to remember that this load is independent of the
    // other one.
    Ch = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo.getValue(1),
                     Hi.getValue(1));
  } else {
    // Big-endian - high bits are at low addresses.  Favor aligned loads at
    // the cost of some bit-fiddling.
    EVT MemVT = N->getMemoryVT();
    unsigned EBytes = MemVT.getStoreSize();
    unsigned IncrementSize = NVT.getSizeInBits()/8;
    unsigned ExcessBits = (EBytes - IncrementSize)*8;

    // Load both the high bits and maybe some of the low bits.
    Hi = DAG.getExtLoad(ExtType, dl, NVT, Ch, Ptr, N->getPointerInfo(),
                        EVT::getIntegerVT(*DAG.getContext(),
                                          MemVT.getSizeInBits() - ExcessBits),
                        N->getOriginalAlign(), MMOFlags, AAInfo);

    // Increment the pointer to the other half.
    Ptr = DAG.getMemBasePlusOffset(Ptr, TypeSize::getFixed(IncrementSize), dl);
    // Load the rest of the low bits.
    Lo = DAG.getExtLoad(ISD::ZEXTLOAD, dl, NVT, Ch, Ptr,
                        N->getPointerInfo().getWithOffset(IncrementSize),
                        EVT::getIntegerVT(*DAG.getContext(), ExcessBits),
                        N->getOriginalAlign(), MMOFlags, AAInfo);

    // Build a factor node to remember that this load is independent of the
    // other one.
    Ch = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo.getValue(1),
                     Hi.getValue(1));

    if (ExcessBits < NVT.getSizeInBits()) {
      // Transfer low bits from the bottom of Hi to the top of Lo.
      Lo = DAG.getNode(
          ISD::OR, dl, NVT, Lo,
          DAG.getNode(ISD::SHL, dl, NVT, Hi,
                      DAG.getConstant(ExcessBits, dl,
                                      TLI.getPointerTy(DAG.getDataLayout()))));
      // Move high bits to the right position in Hi.
      Hi = DAG.getNode(ExtType == ISD::SEXTLOAD ? ISD::SRA : ISD::SRL, dl, NVT,
                       Hi,
                       DAG.getConstant(NVT.getSizeInBits() - ExcessBits, dl,
                                       TLI.getPointerTy(DAG.getDataLayout())));
    }
  }

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Ch);
}

void DAGTypeLegalizer::ExpandIntRes_Logical(SDNode *N,
                                            SDValue &Lo, SDValue &Hi) {
  SDLoc dl(N);
  SDValue LL, LH, RL, RH;
  GetExpandedInteger(N->getOperand(0), LL, LH);
  GetExpandedInteger(N->getOperand(1), RL, RH);
  Lo = DAG.getNode(N->getOpcode(), dl, LL.getValueType(), LL, RL);
  Hi = DAG.getNode(N->getOpcode(), dl, LL.getValueType(), LH, RH);
}

void DAGTypeLegalizer::ExpandIntRes_MUL(SDNode *N,
                                        SDValue &Lo, SDValue &Hi) {
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDLoc dl(N);

  SDValue LL, LH, RL, RH;
  GetExpandedInteger(N->getOperand(0), LL, LH);
  GetExpandedInteger(N->getOperand(1), RL, RH);

  if (TLI.expandMUL(N, Lo, Hi, NVT, DAG,
                    TargetLowering::MulExpansionKind::OnlyLegalOrCustom,
                    LL, LH, RL, RH))
    return;

  // If nothing else, we can make a libcall.
  RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
  if (VT == MVT::i16)
    LC = RTLIB::MUL_I16;
  else if (VT == MVT::i32)
    LC = RTLIB::MUL_I32;
  else if (VT == MVT::i64)
    LC = RTLIB::MUL_I64;
  else if (VT == MVT::i128)
    LC = RTLIB::MUL_I128;

  if (LC == RTLIB::UNKNOWN_LIBCALL || !TLI.getLibcallName(LC)) {
    // Perform a wide multiplication where the wide type is the original VT and
    // the 4 parts are the split arguments.
    TLI.forceExpandWideMUL(DAG, dl, /*Signed=*/true, VT, LL, LH, RL, RH, Lo,
                           Hi);
    return;
  }

  // Note that we don't need to do a wide MUL here since we don't care about the
  // upper half of the result if it exceeds VT.
  SDValue Ops[2] = { N->getOperand(0), N->getOperand(1) };
  TargetLowering::MakeLibCallOptions CallOptions;
  CallOptions.setSExt(true);
  SplitInteger(TLI.makeLibCall(DAG, LC, VT, Ops, CallOptions, dl).first,
               Lo, Hi);
}

void DAGTypeLegalizer::ExpandIntRes_READCOUNTER(SDNode *N, SDValue &Lo,
                                                SDValue &Hi) {
  SDLoc DL(N);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDVTList VTs = DAG.getVTList(NVT, NVT, MVT::Other);
  SDValue R = DAG.getNode(N->getOpcode(), DL, VTs, N->getOperand(0));
  Lo = R.getValue(0);
  Hi = R.getValue(1);
  ReplaceValueWith(SDValue(N, 1), R.getValue(2));
}

void DAGTypeLegalizer::ExpandIntRes_AVG(SDNode *N, SDValue &Lo, SDValue &Hi) {
  SDValue Result = TLI.expandAVG(N, DAG);
  SplitInteger(Result, Lo, Hi);
}

void DAGTypeLegalizer::ExpandIntRes_ADDSUBSAT(SDNode *N, SDValue &Lo,
                                              SDValue &Hi) {
  SDValue Result = TLI.expandAddSubSat(N, DAG);
  SplitInteger(Result, Lo, Hi);
}

void DAGTypeLegalizer::ExpandIntRes_SHLSAT(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  SDValue Result = TLI.expandShlSat(N, DAG);
  SplitInteger(Result, Lo, Hi);
}

/// This performs an expansion of the integer result for a fixed point
/// multiplication. The default expansion performs rounding down towards
/// negative infinity, though targets that do care about rounding should specify
/// a target hook for rounding and provide their own expansion or lowering of
/// fixed point multiplication to be consistent with rounding.
void DAGTypeLegalizer::ExpandIntRes_MULFIX(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  unsigned VTSize = VT.getScalarSizeInBits();
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  uint64_t Scale = N->getConstantOperandVal(2);
  bool Saturating = (N->getOpcode() == ISD::SMULFIXSAT ||
                     N->getOpcode() == ISD::UMULFIXSAT);
  bool Signed = (N->getOpcode() == ISD::SMULFIX ||
                 N->getOpcode() == ISD::SMULFIXSAT);

  // Handle special case when scale is equal to zero.
  if (!Scale) {
    SDValue Result;
    if (!Saturating) {
      Result = DAG.getNode(ISD::MUL, dl, VT, LHS, RHS);
    } else {
      EVT BoolVT = getSetCCResultType(VT);
      unsigned MulOp = Signed ? ISD::SMULO : ISD::UMULO;
      Result = DAG.getNode(MulOp, dl, DAG.getVTList(VT, BoolVT), LHS, RHS);
      SDValue Product = Result.getValue(0);
      SDValue Overflow = Result.getValue(1);
      if (Signed) {
        APInt MinVal = APInt::getSignedMinValue(VTSize);
        APInt MaxVal = APInt::getSignedMaxValue(VTSize);
        SDValue SatMin = DAG.getConstant(MinVal, dl, VT);
        SDValue SatMax = DAG.getConstant(MaxVal, dl, VT);
        SDValue Zero = DAG.getConstant(0, dl, VT);
        // Xor the inputs, if resulting sign bit is 0 the product will be
        // positive, else negative.
        SDValue Xor = DAG.getNode(ISD::XOR, dl, VT, LHS, RHS);
        SDValue ProdNeg = DAG.getSetCC(dl, BoolVT, Xor, Zero, ISD::SETLT);
        Result = DAG.getSelect(dl, VT, ProdNeg, SatMin, SatMax);
        Result = DAG.getSelect(dl, VT, Overflow, Result, Product);
      } else {
        // For unsigned multiplication, we only need to check the max since we
        // can't really overflow towards zero.
        APInt MaxVal = APInt::getMaxValue(VTSize);
        SDValue SatMax = DAG.getConstant(MaxVal, dl, VT);
        Result = DAG.getSelect(dl, VT, Overflow, SatMax, Product);
      }
    }
    SplitInteger(Result, Lo, Hi);
    return;
  }

  // For SMULFIX[SAT] we only expect to find Scale<VTSize, but this assert will
  // cover for unhandled cases below, while still being valid for UMULFIX[SAT].
  assert(Scale <= VTSize && "Scale can't be larger than the value type size.");

  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue LL, LH, RL, RH;
  GetExpandedInteger(LHS, LL, LH);
  GetExpandedInteger(RHS, RL, RH);
  SmallVector<SDValue, 4> Result;

  unsigned LoHiOp = Signed ? ISD::SMUL_LOHI : ISD::UMUL_LOHI;
  if (!TLI.expandMUL_LOHI(LoHiOp, VT, dl, LHS, RHS, Result, NVT, DAG,
                          TargetLowering::MulExpansionKind::OnlyLegalOrCustom,
                          LL, LH, RL, RH)) {
    Result.clear();
    Result.resize(4);

    SDValue LoTmp, HiTmp;
    TLI.forceExpandWideMUL(DAG, dl, Signed, LHS, RHS, LoTmp, HiTmp);
    SplitInteger(LoTmp, Result[0], Result[1]);
    SplitInteger(HiTmp, Result[2], Result[3]);
  }
  assert(Result.size() == 4 && "Unexpected number of partlets in the result");

  unsigned NVTSize = NVT.getScalarSizeInBits();
  assert((VTSize == NVTSize * 2) && "Expected the new value type to be half "
                                    "the size of the current value type");

  // After getting the multiplication result in 4 parts, we need to perform a
  // shift right by the amount of the scale to get the result in that scale.
  //
  // Let's say we multiply 2 64 bit numbers. The resulting value can be held in
  // 128 bits that are cut into 4 32-bit parts:
  //
  //      HH       HL       LH       LL
  //  |---32---|---32---|---32---|---32---|
  // 128      96       64       32        0
  //
  //                    |------VTSize-----|
  //
  //                             |NVTSize-|
  //
  // The resulting Lo and Hi would normally be in LL and LH after the shift. But
  // to avoid unneccessary shifting of all 4 parts, we can adjust the shift
  // amount and get Lo and Hi using two funnel shifts. Or for the special case
  // when Scale is a multiple of NVTSize we can just pick the result without
  // shifting.
  uint64_t Part0 = Scale / NVTSize; // Part holding lowest bit needed.
  if (Scale % NVTSize) {
    SDValue ShiftAmount = DAG.getShiftAmountConstant(Scale % NVTSize, NVT, dl);
    Lo = DAG.getNode(ISD::FSHR, dl, NVT, Result[Part0 + 1], Result[Part0],
                     ShiftAmount);
    Hi = DAG.getNode(ISD::FSHR, dl, NVT, Result[Part0 + 2], Result[Part0 + 1],
                     ShiftAmount);
  } else {
    Lo = Result[Part0];
    Hi = Result[Part0 + 1];
  }

  // Unless saturation is requested we are done. The result is in <Hi,Lo>.
  if (!Saturating)
    return;

  // Can not overflow when there is no integer part.
  if (Scale == VTSize)
    return;

  // To handle saturation we must check for overflow in the multiplication.
  //
  // Unsigned overflow happened if the upper (VTSize - Scale) bits (of Result)
  // aren't all zeroes.
  //
  // Signed overflow happened if the upper (VTSize - Scale + 1) bits (of Result)
  // aren't all ones or all zeroes.
  //
  // We cannot overflow past HH when multiplying 2 ints of size VTSize, so the
  // highest bit of HH determines saturation direction in the event of signed
  // saturation.

  SDValue ResultHL = Result[2];
  SDValue ResultHH = Result[3];

  SDValue SatMax, SatMin;
  SDValue NVTZero = DAG.getConstant(0, dl, NVT);
  SDValue NVTNeg1 = DAG.getConstant(-1, dl, NVT);
  EVT BoolNVT = getSetCCResultType(NVT);

  if (!Signed) {
    if (Scale < NVTSize) {
      // Overflow happened if ((HH | (HL >> Scale)) != 0).
      SDValue HLAdjusted =
          DAG.getNode(ISD::SRL, dl, NVT, ResultHL,
                      DAG.getShiftAmountConstant(Scale, NVT, dl));
      SDValue Tmp = DAG.getNode(ISD::OR, dl, NVT, HLAdjusted, ResultHH);
      SatMax = DAG.getSetCC(dl, BoolNVT, Tmp, NVTZero, ISD::SETNE);
    } else if (Scale == NVTSize) {
      // Overflow happened if (HH != 0).
      SatMax = DAG.getSetCC(dl, BoolNVT, ResultHH, NVTZero, ISD::SETNE);
    } else if (Scale < VTSize) {
      // Overflow happened if ((HH >> (Scale - NVTSize)) != 0).
      SDValue HLAdjusted =
          DAG.getNode(ISD::SRL, dl, NVT, ResultHL,
                      DAG.getShiftAmountConstant(Scale - NVTSize, NVT, dl));
      SatMax = DAG.getSetCC(dl, BoolNVT, HLAdjusted, NVTZero, ISD::SETNE);
    } else
      llvm_unreachable("Scale must be less or equal to VTSize for UMULFIXSAT"
                       "(and saturation can't happen with Scale==VTSize).");

    Hi = DAG.getSelect(dl, NVT, SatMax, NVTNeg1, Hi);
    Lo = DAG.getSelect(dl, NVT, SatMax, NVTNeg1, Lo);
    return;
  }

  if (Scale < NVTSize) {
    // The number of overflow bits we can check are VTSize - Scale + 1 (we
    // include the sign bit). If these top bits are > 0, then we overflowed past
    // the max value. If these top bits are < -1, then we overflowed past the
    // min value. Otherwise, we did not overflow.
    unsigned OverflowBits = VTSize - Scale + 1;
    assert(OverflowBits <= VTSize && OverflowBits > NVTSize &&
           "Extent of overflow bits must start within HL");
    SDValue HLHiMask = DAG.getConstant(
        APInt::getHighBitsSet(NVTSize, OverflowBits - NVTSize), dl, NVT);
    SDValue HLLoMask = DAG.getConstant(
        APInt::getLowBitsSet(NVTSize, VTSize - OverflowBits), dl, NVT);
    // We overflow max if HH > 0 or (HH == 0 && HL > HLLoMask).
    SDValue HHGT0 = DAG.getSetCC(dl, BoolNVT, ResultHH, NVTZero, ISD::SETGT);
    SDValue HHEQ0 = DAG.getSetCC(dl, BoolNVT, ResultHH, NVTZero, ISD::SETEQ);
    SDValue HLUGT = DAG.getSetCC(dl, BoolNVT, ResultHL, HLLoMask, ISD::SETUGT);
    SatMax = DAG.getNode(ISD::OR, dl, BoolNVT, HHGT0,
                         DAG.getNode(ISD::AND, dl, BoolNVT, HHEQ0, HLUGT));
    // We overflow min if HH < -1 or (HH == -1 && HL < HLHiMask).
    SDValue HHLT = DAG.getSetCC(dl, BoolNVT, ResultHH, NVTNeg1, ISD::SETLT);
    SDValue HHEQ = DAG.getSetCC(dl, BoolNVT, ResultHH, NVTNeg1, ISD::SETEQ);
    SDValue HLULT = DAG.getSetCC(dl, BoolNVT, ResultHL, HLHiMask, ISD::SETULT);
    SatMin = DAG.getNode(ISD::OR, dl, BoolNVT, HHLT,
                         DAG.getNode(ISD::AND, dl, BoolNVT, HHEQ, HLULT));
  } else if (Scale == NVTSize) {
    // We overflow max if HH > 0 or (HH == 0 && HL sign bit is 1).
    SDValue HHGT0 = DAG.getSetCC(dl, BoolNVT, ResultHH, NVTZero, ISD::SETGT);
    SDValue HHEQ0 = DAG.getSetCC(dl, BoolNVT, ResultHH, NVTZero, ISD::SETEQ);
    SDValue HLNeg = DAG.getSetCC(dl, BoolNVT, ResultHL, NVTZero, ISD::SETLT);
    SatMax = DAG.getNode(ISD::OR, dl, BoolNVT, HHGT0,
                         DAG.getNode(ISD::AND, dl, BoolNVT, HHEQ0, HLNeg));
    // We overflow min if HH < -1 or (HH == -1 && HL sign bit is 0).
    SDValue HHLT = DAG.getSetCC(dl, BoolNVT, ResultHH, NVTNeg1, ISD::SETLT);
    SDValue HHEQ = DAG.getSetCC(dl, BoolNVT, ResultHH, NVTNeg1, ISD::SETEQ);
    SDValue HLPos = DAG.getSetCC(dl, BoolNVT, ResultHL, NVTZero, ISD::SETGE);
    SatMin = DAG.getNode(ISD::OR, dl, BoolNVT, HHLT,
                         DAG.getNode(ISD::AND, dl, BoolNVT, HHEQ, HLPos));
  } else if (Scale < VTSize) {
    // This is similar to the case when we saturate if Scale < NVTSize, but we
    // only need to check HH.
    unsigned OverflowBits = VTSize - Scale + 1;
    SDValue HHHiMask = DAG.getConstant(
        APInt::getHighBitsSet(NVTSize, OverflowBits), dl, NVT);
    SDValue HHLoMask = DAG.getConstant(
        APInt::getLowBitsSet(NVTSize, NVTSize - OverflowBits), dl, NVT);
    SatMax = DAG.getSetCC(dl, BoolNVT, ResultHH, HHLoMask, ISD::SETGT);
    SatMin = DAG.getSetCC(dl, BoolNVT, ResultHH, HHHiMask, ISD::SETLT);
  } else
    llvm_unreachable("Illegal scale for signed fixed point mul.");

  // Saturate to signed maximum.
  APInt MaxHi = APInt::getSignedMaxValue(NVTSize);
  APInt MaxLo = APInt::getAllOnes(NVTSize);
  Hi = DAG.getSelect(dl, NVT, SatMax, DAG.getConstant(MaxHi, dl, NVT), Hi);
  Lo = DAG.getSelect(dl, NVT, SatMax, DAG.getConstant(MaxLo, dl, NVT), Lo);
  // Saturate to signed minimum.
  APInt MinHi = APInt::getSignedMinValue(NVTSize);
  Hi = DAG.getSelect(dl, NVT, SatMin, DAG.getConstant(MinHi, dl, NVT), Hi);
  Lo = DAG.getSelect(dl, NVT, SatMin, NVTZero, Lo);
}

void DAGTypeLegalizer::ExpandIntRes_DIVFIX(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  SDLoc dl(N);
  // Try expanding in the existing type first.
  SDValue Res = TLI.expandFixedPointDiv(N->getOpcode(), dl, N->getOperand(0),
                                        N->getOperand(1),
                                        N->getConstantOperandVal(2), DAG);

  if (!Res)
    Res = earlyExpandDIVFIX(N, N->getOperand(0), N->getOperand(1),
                            N->getConstantOperandVal(2), TLI, DAG);
  SplitInteger(Res, Lo, Hi);
}

void DAGTypeLegalizer::ExpandIntRes_SADDSUBO(SDNode *Node,
                                             SDValue &Lo, SDValue &Hi) {
  assert((Node->getOpcode() == ISD::SADDO || Node->getOpcode() == ISD::SSUBO) &&
         "Node has unexpected Opcode");
  SDValue LHS = Node->getOperand(0);
  SDValue RHS = Node->getOperand(1);
  SDLoc dl(Node);

  SDValue Ovf;

  bool IsAdd = Node->getOpcode() == ISD::SADDO;
  unsigned CarryOp = IsAdd ? ISD::SADDO_CARRY : ISD::SSUBO_CARRY;

  bool HasCarryOp = TLI.isOperationLegalOrCustom(
      CarryOp, TLI.getTypeToExpandTo(*DAG.getContext(), LHS.getValueType()));

  if (HasCarryOp) {
    // Expand the subcomponents.
    SDValue LHSL, LHSH, RHSL, RHSH;
    GetExpandedInteger(LHS, LHSL, LHSH);
    GetExpandedInteger(RHS, RHSL, RHSH);
    SDVTList VTList = DAG.getVTList(LHSL.getValueType(), Node->getValueType(1));

    Lo = DAG.getNode(IsAdd ? ISD::UADDO : ISD::USUBO, dl, VTList, {LHSL, RHSL});
    Hi = DAG.getNode(CarryOp, dl, VTList, { LHSH, RHSH, Lo.getValue(1) });

    Ovf = Hi.getValue(1);
  } else {
    // Expand the result by simply replacing it with the equivalent
    // non-overflow-checking operation.
    SDValue Sum = DAG.getNode(Node->getOpcode() == ISD::SADDO ?
                              ISD::ADD : ISD::SUB, dl, LHS.getValueType(),
                              LHS, RHS);
    SplitInteger(Sum, Lo, Hi);

    // Compute the overflow.
    //
    //   LHSSign -> LHS < 0
    //   RHSSign -> RHS < 0
    //   SumSign -> Sum < 0
    //
    //   Add:
    //   Overflow -> (LHSSign == RHSSign) && (LHSSign != SumSign)
    //   Sub:
    //   Overflow -> (LHSSign != RHSSign) && (LHSSign != SumSign)
    //
    // To get better codegen we can rewrite this by doing bitwise math on
    // the integers and extract the final sign bit at the end. So the
    // above becomes:
    //
    //   Add:
    //   Overflow -> (~(LHS ^ RHS) & (LHS ^ Sum)) < 0
    //   Sub:
    //   Overflow -> ((LHS ^ RHS) & (LHS ^ Sum)) < 0
    //
    // NOTE: This is different than the expansion we do in expandSADDSUBO
    // because it is more costly to determine the RHS is > 0 for SSUBO with the
    // integers split.
    EVT VT = LHS.getValueType();
    SDValue SignsMatch = DAG.getNode(ISD::XOR, dl, VT, LHS, RHS);
    if (IsAdd)
      SignsMatch = DAG.getNOT(dl, SignsMatch, VT);

    SDValue SumSignNE = DAG.getNode(ISD::XOR, dl, VT, LHS, Sum);
    Ovf = DAG.getNode(ISD::AND, dl, VT, SignsMatch, SumSignNE);
    EVT OType = Node->getValueType(1);
    Ovf = DAG.getSetCC(dl, OType, Ovf, DAG.getConstant(0, dl, VT), ISD::SETLT);
  }

  // Use the calculated overflow everywhere.
  ReplaceValueWith(SDValue(Node, 1), Ovf);
}

void DAGTypeLegalizer::ExpandIntRes_SDIV(SDNode *N,
                                         SDValue &Lo, SDValue &Hi) {
  EVT VT = N->getValueType(0);
  SDLoc dl(N);
  SDValue Ops[2] = { N->getOperand(0), N->getOperand(1) };

  if (TLI.getOperationAction(ISD::SDIVREM, VT) == TargetLowering::Custom) {
    SDValue Res = DAG.getNode(ISD::SDIVREM, dl, DAG.getVTList(VT, VT), Ops);
    SplitInteger(Res.getValue(0), Lo, Hi);
    return;
  }

  RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
  if (VT == MVT::i16)
    LC = RTLIB::SDIV_I16;
  else if (VT == MVT::i32)
    LC = RTLIB::SDIV_I32;
  else if (VT == MVT::i64)
    LC = RTLIB::SDIV_I64;
  else if (VT == MVT::i128)
    LC = RTLIB::SDIV_I128;
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported SDIV!");

  TargetLowering::MakeLibCallOptions CallOptions;
  CallOptions.setSExt(true);
  SplitInteger(TLI.makeLibCall(DAG, LC, VT, Ops, CallOptions, dl).first, Lo, Hi);
}

void DAGTypeLegalizer::ExpandIntRes_ShiftThroughStack(SDNode *N, SDValue &Lo,
                                                      SDValue &Hi) {
  SDLoc dl(N);
  SDValue Shiftee = N->getOperand(0);
  EVT VT = Shiftee.getValueType();
  SDValue ShAmt = N->getOperand(1);
  EVT ShAmtVT = ShAmt.getValueType();

  // This legalization is optimal when the shift is by a multiple of byte width,
  //   %x * 8 <-> %x << 3   so 3 low bits should be be known zero.
  bool ShiftByByteMultiple =
      DAG.computeKnownBits(ShAmt).countMinTrailingZeros() >= 3;

  // If we can't do it as one step, we'll have two uses of shift amount,
  // and thus must freeze it.
  if (!ShiftByByteMultiple)
    ShAmt = DAG.getFreeze(ShAmt);

  unsigned VTBitWidth = VT.getScalarSizeInBits();
  assert(VTBitWidth % 8 == 0 && "Shifting a not byte multiple value?");
  unsigned VTByteWidth = VTBitWidth / 8;
  assert(isPowerOf2_32(VTByteWidth) &&
         "Shiftee type size is not a power of two!");
  unsigned StackSlotByteWidth = 2 * VTByteWidth;
  unsigned StackSlotBitWidth = 8 * StackSlotByteWidth;
  EVT StackSlotVT = EVT::getIntegerVT(*DAG.getContext(), StackSlotBitWidth);

  // Get a temporary stack slot 2x the width of our VT.
  // FIXME: reuse stack slots?
  // FIXME: should we be more picky about alignment?
  Align StackSlotAlignment(1);
  SDValue StackPtr = DAG.CreateStackTemporary(
      TypeSize::getFixed(StackSlotByteWidth), StackSlotAlignment);
  EVT PtrTy = StackPtr.getValueType();
  SDValue Ch = DAG.getEntryNode();

  MachinePointerInfo StackPtrInfo = MachinePointerInfo::getFixedStack(
      DAG.getMachineFunction(),
      cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex());

  // Extend the value, that is being shifted, to the entire stack slot's width.
  SDValue Init;
  if (N->getOpcode() != ISD::SHL) {
    unsigned WideningOpc =
        N->getOpcode() == ISD::SRA ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
    Init = DAG.getNode(WideningOpc, dl, StackSlotVT, Shiftee);
  } else {
    // For left-shifts, pad the Shiftee's LSB with zeros to twice it's width.
    SDValue AllZeros = DAG.getConstant(0, dl, VT);
    Init = DAG.getNode(ISD::BUILD_PAIR, dl, StackSlotVT, AllZeros, Shiftee);
  }
  // And spill it into the stack slot.
  Ch = DAG.getStore(Ch, dl, Init, StackPtr, StackPtrInfo, StackSlotAlignment);

  // Now, compute the full-byte offset into stack slot from where we can load.
  // We have shift amount, which is in bits, but in multiples of byte.
  // So just divide by CHAR_BIT.
  SDNodeFlags Flags;
  if (ShiftByByteMultiple)
    Flags.setExact(true);
  SDValue ByteOffset = DAG.getNode(ISD::SRL, dl, ShAmtVT, ShAmt,
                                   DAG.getConstant(3, dl, ShAmtVT), Flags);
  // And clamp it, because OOB load is an immediate UB,
  // while shift overflow would have *just* been poison.
  ByteOffset = DAG.getNode(ISD::AND, dl, ShAmtVT, ByteOffset,
                           DAG.getConstant(VTByteWidth - 1, dl, ShAmtVT));
  // We have exactly two strategies on indexing into stack slot here:
  // 1. upwards starting from the beginning of the slot
  // 2. downwards starting from the middle of the slot
  // On little-endian machine, we pick 1. for right shifts and 2. for left-shift
  // and vice versa on big-endian machine.
  bool WillIndexUpwards = N->getOpcode() != ISD::SHL;
  if (DAG.getDataLayout().isBigEndian())
    WillIndexUpwards = !WillIndexUpwards;

  SDValue AdjStackPtr;
  if (WillIndexUpwards) {
    AdjStackPtr = StackPtr;
  } else {
    AdjStackPtr = DAG.getMemBasePlusOffset(
        StackPtr, DAG.getConstant(VTByteWidth, dl, PtrTy), dl);
    ByteOffset = DAG.getNegative(ByteOffset, dl, ShAmtVT);
  }

  // Get the pointer somewhere into the stack slot from which we need to load.
  ByteOffset = DAG.getSExtOrTrunc(ByteOffset, dl, PtrTy);
  AdjStackPtr = DAG.getMemBasePlusOffset(AdjStackPtr, ByteOffset, dl);

  // And load it! While the load is not legal, legalizing it is obvious.
  SDValue Res = DAG.getLoad(
      VT, dl, Ch, AdjStackPtr,
      MachinePointerInfo::getUnknownStack(DAG.getMachineFunction()), Align(1));
  // We've performed the shift by a CHAR_BIT * [_ShAmt / CHAR_BIT_]

  // If we may still have a less-than-CHAR_BIT to shift by, do so now.
  if (!ShiftByByteMultiple) {
    SDValue ShAmtRem = DAG.getNode(ISD::AND, dl, ShAmtVT, ShAmt,
                                   DAG.getConstant(7, dl, ShAmtVT));
    Res = DAG.getNode(N->getOpcode(), dl, VT, Res, ShAmtRem);
  }

  // Finally, split the computed value.
  SplitInteger(Res, Lo, Hi);
}

void DAGTypeLegalizer::ExpandIntRes_Shift(SDNode *N,
                                          SDValue &Lo, SDValue &Hi) {
  EVT VT = N->getValueType(0);
  unsigned Opc = N->getOpcode();
  SDLoc dl(N);

  // If we can emit an efficient shift operation, do so now.  Check to see if
  // the RHS is a constant.
  if (ConstantSDNode *CN = dyn_cast<ConstantSDNode>(N->getOperand(1)))
    return ExpandShiftByConstant(N, CN->getAPIntValue(), Lo, Hi);

  // If we can determine that the high bit of the shift is zero or one, even if
  // the low bits are variable, emit this shift in an optimized form.
  if (ExpandShiftWithKnownAmountBit(N, Lo, Hi))
    return;

  // If this target supports shift_PARTS, use it.  First, map to the _PARTS opc.
  unsigned PartsOpc;
  if (Opc == ISD::SHL) {
    PartsOpc = ISD::SHL_PARTS;
  } else if (Opc == ISD::SRL) {
    PartsOpc = ISD::SRL_PARTS;
  } else {
    assert(Opc == ISD::SRA && "Unknown shift!");
    PartsOpc = ISD::SRA_PARTS;
  }

  // Next check to see if the target supports this SHL_PARTS operation or if it
  // will custom expand it. Don't lower this to SHL_PARTS when we optimise for
  // size, but create a libcall instead.
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  TargetLowering::LegalizeAction Action = TLI.getOperationAction(PartsOpc, NVT);
  const bool LegalOrCustom =
    (Action == TargetLowering::Legal && TLI.isTypeLegal(NVT)) ||
    Action == TargetLowering::Custom;

  unsigned ExpansionFactor = 1;
  // That VT->NVT expansion is one step. But will we re-expand NVT?
  for (EVT TmpVT = NVT;;) {
    EVT NewTMPVT = TLI.getTypeToTransformTo(*DAG.getContext(), TmpVT);
    if (NewTMPVT == TmpVT)
      break;
    TmpVT = NewTMPVT;
    ++ExpansionFactor;
  }

  TargetLowering::ShiftLegalizationStrategy S =
      TLI.preferredShiftLegalizationStrategy(DAG, N, ExpansionFactor);

  if (S == TargetLowering::ShiftLegalizationStrategy::ExpandThroughStack)
    return ExpandIntRes_ShiftThroughStack(N, Lo, Hi);

  if (LegalOrCustom &&
      S != TargetLowering::ShiftLegalizationStrategy::LowerToLibcall) {
    // Expand the subcomponents.
    SDValue LHSL, LHSH;
    GetExpandedInteger(N->getOperand(0), LHSL, LHSH);
    EVT VT = LHSL.getValueType();

    // If the shift amount operand is coming from a vector legalization it may
    // have an illegal type.  Fix that first by casting the operand, otherwise
    // the new SHL_PARTS operation would need further legalization.
    SDValue ShiftOp = N->getOperand(1);
    EVT ShiftTy = TLI.getShiftAmountTy(VT, DAG.getDataLayout());
    if (ShiftOp.getValueType() != ShiftTy)
      ShiftOp = DAG.getZExtOrTrunc(ShiftOp, dl, ShiftTy);

    SDValue Ops[] = { LHSL, LHSH, ShiftOp };
    Lo = DAG.getNode(PartsOpc, dl, DAG.getVTList(VT, VT), Ops);
    Hi = Lo.getValue(1);
    return;
  }

  // Otherwise, emit a libcall.
  RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
  bool isSigned;
  if (Opc == ISD::SHL) {
    isSigned = false; /*sign irrelevant*/
    if (VT == MVT::i16)
      LC = RTLIB::SHL_I16;
    else if (VT == MVT::i32)
      LC = RTLIB::SHL_I32;
    else if (VT == MVT::i64)
      LC = RTLIB::SHL_I64;
    else if (VT == MVT::i128)
      LC = RTLIB::SHL_I128;
  } else if (Opc == ISD::SRL) {
    isSigned = false;
    if (VT == MVT::i16)
      LC = RTLIB::SRL_I16;
    else if (VT == MVT::i32)
      LC = RTLIB::SRL_I32;
    else if (VT == MVT::i64)
      LC = RTLIB::SRL_I64;
    else if (VT == MVT::i128)
      LC = RTLIB::SRL_I128;
  } else {
    assert(Opc == ISD::SRA && "Unknown shift!");
    isSigned = true;
    if (VT == MVT::i16)
      LC = RTLIB::SRA_I16;
    else if (VT == MVT::i32)
      LC = RTLIB::SRA_I32;
    else if (VT == MVT::i64)
      LC = RTLIB::SRA_I64;
    else if (VT == MVT::i128)
      LC = RTLIB::SRA_I128;
  }

  if (LC != RTLIB::UNKNOWN_LIBCALL && TLI.getLibcallName(LC)) {
    EVT ShAmtTy =
        EVT::getIntegerVT(*DAG.getContext(), DAG.getLibInfo().getIntSize());
    SDValue ShAmt = DAG.getZExtOrTrunc(N->getOperand(1), dl, ShAmtTy);
    SDValue Ops[2] = {N->getOperand(0), ShAmt};
    TargetLowering::MakeLibCallOptions CallOptions;
    CallOptions.setSExt(isSigned);
    SplitInteger(TLI.makeLibCall(DAG, LC, VT, Ops, CallOptions, dl).first, Lo, Hi);
    return;
  }

  if (!ExpandShiftWithUnknownAmountBit(N, Lo, Hi))
    llvm_unreachable("Unsupported shift!");
}

void DAGTypeLegalizer::ExpandIntRes_SIGN_EXTEND(SDNode *N,
                                                SDValue &Lo, SDValue &Hi) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDLoc dl(N);
  SDValue Op = N->getOperand(0);
  if (Op.getValueType().bitsLE(NVT)) {
    // The low part is sign extension of the input (degenerates to a copy).
    Lo = DAG.getNode(ISD::SIGN_EXTEND, dl, NVT, N->getOperand(0));
    // The high part is obtained by SRA'ing all but one of the bits of low part.
    unsigned LoSize = NVT.getSizeInBits();
    Hi = DAG.getNode(
        ISD::SRA, dl, NVT, Lo,
        DAG.getConstant(LoSize - 1, dl, TLI.getPointerTy(DAG.getDataLayout())));
  } else {
    // For example, extension of an i48 to an i64.  The operand type necessarily
    // promotes to the result type, so will end up being expanded too.
    assert(getTypeAction(Op.getValueType()) ==
           TargetLowering::TypePromoteInteger &&
           "Only know how to promote this result!");
    SDValue Res = GetPromotedInteger(Op);
    assert(Res.getValueType() == N->getValueType(0) &&
           "Operand over promoted?");
    // Split the promoted operand.  This will simplify when it is expanded.
    SplitInteger(Res, Lo, Hi);
    unsigned ExcessBits = Op.getValueSizeInBits() - NVT.getSizeInBits();
    Hi = DAG.getNode(ISD::SIGN_EXTEND_INREG, dl, Hi.getValueType(), Hi,
                     DAG.getValueType(EVT::getIntegerVT(*DAG.getContext(),
                                                        ExcessBits)));
  }
}

void DAGTypeLegalizer::
ExpandIntRes_SIGN_EXTEND_INREG(SDNode *N, SDValue &Lo, SDValue &Hi) {
  SDLoc dl(N);
  GetExpandedInteger(N->getOperand(0), Lo, Hi);
  EVT EVT = cast<VTSDNode>(N->getOperand(1))->getVT();

  if (EVT.bitsLE(Lo.getValueType())) {
    // sext_inreg the low part if needed.
    Lo = DAG.getNode(ISD::SIGN_EXTEND_INREG, dl, Lo.getValueType(), Lo,
                     N->getOperand(1));

    // The high part gets the sign extension from the lo-part.  This handles
    // things like sextinreg V:i64 from i8.
    Hi = DAG.getNode(ISD::SRA, dl, Hi.getValueType(), Lo,
                     DAG.getConstant(Hi.getValueSizeInBits() - 1, dl,
                                     TLI.getPointerTy(DAG.getDataLayout())));
  } else {
    // For example, extension of an i48 to an i64.  Leave the low part alone,
    // sext_inreg the high part.
    unsigned ExcessBits = EVT.getSizeInBits() - Lo.getValueSizeInBits();
    Hi = DAG.getNode(ISD::SIGN_EXTEND_INREG, dl, Hi.getValueType(), Hi,
                     DAG.getValueType(EVT::getIntegerVT(*DAG.getContext(),
                                                        ExcessBits)));
  }
}

void DAGTypeLegalizer::ExpandIntRes_SREM(SDNode *N,
                                         SDValue &Lo, SDValue &Hi) {
  EVT VT = N->getValueType(0);
  SDLoc dl(N);
  SDValue Ops[2] = { N->getOperand(0), N->getOperand(1) };

  if (TLI.getOperationAction(ISD::SDIVREM, VT) == TargetLowering::Custom) {
    SDValue Res = DAG.getNode(ISD::SDIVREM, dl, DAG.getVTList(VT, VT), Ops);
    SplitInteger(Res.getValue(1), Lo, Hi);
    return;
  }

  RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
  if (VT == MVT::i16)
    LC = RTLIB::SREM_I16;
  else if (VT == MVT::i32)
    LC = RTLIB::SREM_I32;
  else if (VT == MVT::i64)
    LC = RTLIB::SREM_I64;
  else if (VT == MVT::i128)
    LC = RTLIB::SREM_I128;
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported SREM!");

  TargetLowering::MakeLibCallOptions CallOptions;
  CallOptions.setSExt(true);
  SplitInteger(TLI.makeLibCall(DAG, LC, VT, Ops, CallOptions, dl).first, Lo, Hi);
}

void DAGTypeLegalizer::ExpandIntRes_TRUNCATE(SDNode *N,
                                             SDValue &Lo, SDValue &Hi) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDLoc dl(N);
  Lo = DAG.getNode(ISD::TRUNCATE, dl, NVT, N->getOperand(0));
  Hi = DAG.getNode(ISD::SRL, dl, N->getOperand(0).getValueType(),
                   N->getOperand(0),
                   DAG.getConstant(NVT.getSizeInBits(), dl,
                                   TLI.getPointerTy(DAG.getDataLayout())));
  Hi = DAG.getNode(ISD::TRUNCATE, dl, NVT, Hi);
}

void DAGTypeLegalizer::ExpandIntRes_XMULO(SDNode *N,
                                          SDValue &Lo, SDValue &Hi) {
  EVT VT = N->getValueType(0);
  SDLoc dl(N);

  if (N->getOpcode() == ISD::UMULO) {
    // This section expands the operation into the following sequence of
    // instructions. `iNh` here refers to a type which has half the bit width of
    // the type the original operation operated on.
    //
    // %0 = %LHS.HI != 0 && %RHS.HI != 0
    // %1 = { iNh, i1 } @umul.with.overflow.iNh(iNh %LHS.HI, iNh %RHS.LO)
    // %2 = { iNh, i1 } @umul.with.overflow.iNh(iNh %RHS.HI, iNh %LHS.LO)
    // %3 = mul nuw iN (%LHS.LOW as iN), (%RHS.LOW as iN)
    // %4 = add iNh %1.0, %2.0 as iN
    // %5 = { iNh, i1 } @uadd.with.overflow.iNh(iNh %4, iNh %3.HIGH)
    //
    // %lo = %3.LO
    // %hi = %5.0
    // %ovf = %0 || %1.1 || %2.1 || %5.1
    SDValue LHS = N->getOperand(0), RHS = N->getOperand(1);
    SDValue LHSHigh, LHSLow, RHSHigh, RHSLow;
    GetExpandedInteger(LHS, LHSLow, LHSHigh);
    GetExpandedInteger(RHS, RHSLow, RHSHigh);
    EVT HalfVT = LHSLow.getValueType();
    EVT BitVT = N->getValueType(1);
    SDVTList VTHalfWithO = DAG.getVTList(HalfVT, BitVT);

    SDValue HalfZero = DAG.getConstant(0, dl, HalfVT);
    SDValue Overflow = DAG.getNode(ISD::AND, dl, BitVT,
      DAG.getSetCC(dl, BitVT, LHSHigh, HalfZero, ISD::SETNE),
      DAG.getSetCC(dl, BitVT, RHSHigh, HalfZero, ISD::SETNE));

    SDValue One = DAG.getNode(ISD::UMULO, dl, VTHalfWithO, LHSHigh, RHSLow);
    Overflow = DAG.getNode(ISD::OR, dl, BitVT, Overflow, One.getValue(1));

    SDValue Two = DAG.getNode(ISD::UMULO, dl, VTHalfWithO, RHSHigh, LHSLow);
    Overflow = DAG.getNode(ISD::OR, dl, BitVT, Overflow, Two.getValue(1));

    SDValue HighSum = DAG.getNode(ISD::ADD, dl, HalfVT, One, Two);

    // Cannot use `UMUL_LOHI` directly, because some 32-bit targets (ARM) do not
    // know how to expand `i64,i64 = umul_lohi a, b` and abort (why isn’t this
    // operation recursively legalized?).
    //
    // Many backends understand this pattern and will convert into LOHI
    // themselves, if applicable.
    SDValue Three = DAG.getNode(ISD::MUL, dl, VT,
      DAG.getNode(ISD::ZERO_EXTEND, dl, VT, LHSLow),
      DAG.getNode(ISD::ZERO_EXTEND, dl, VT, RHSLow));
    SplitInteger(Three, Lo, Hi);

    Hi = DAG.getNode(ISD::UADDO, dl, VTHalfWithO, Hi, HighSum);
    Overflow = DAG.getNode(ISD::OR, dl, BitVT, Overflow, Hi.getValue(1));
    ReplaceValueWith(SDValue(N, 1), Overflow);
    return;
  }

  Type *RetTy = VT.getTypeForEVT(*DAG.getContext());
  EVT PtrVT = TLI.getPointerTy(DAG.getDataLayout());
  Type *PtrTy = PtrVT.getTypeForEVT(*DAG.getContext());

  // Replace this with a libcall that will check overflow.
  RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
  if (VT == MVT::i32)
    LC = RTLIB::MULO_I32;
  else if (VT == MVT::i64)
    LC = RTLIB::MULO_I64;
  else if (VT == MVT::i128)
    LC = RTLIB::MULO_I128;

  // If we don't have the libcall or if the function we are compiling is the
  // implementation of the expected libcall (avoid inf-loop), expand inline.
  if (LC == RTLIB::UNKNOWN_LIBCALL || !TLI.getLibcallName(LC) ||
      TLI.getLibcallName(LC) == DAG.getMachineFunction().getName()) {
    // FIXME: This is not an optimal expansion, but better than crashing.
    EVT WideVT =
        EVT::getIntegerVT(*DAG.getContext(), VT.getScalarSizeInBits() * 2);
    SDValue LHS = DAG.getNode(ISD::SIGN_EXTEND, dl, WideVT, N->getOperand(0));
    SDValue RHS = DAG.getNode(ISD::SIGN_EXTEND, dl, WideVT, N->getOperand(1));
    SDValue Mul = DAG.getNode(ISD::MUL, dl, WideVT, LHS, RHS);
    SDValue MulLo, MulHi;
    SplitInteger(Mul, MulLo, MulHi);
    SDValue SRA =
        DAG.getNode(ISD::SRA, dl, VT, MulLo,
                    DAG.getConstant(VT.getScalarSizeInBits() - 1, dl, VT));
    SDValue Overflow =
        DAG.getSetCC(dl, N->getValueType(1), MulHi, SRA, ISD::SETNE);
    SplitInteger(MulLo, Lo, Hi);
    ReplaceValueWith(SDValue(N, 1), Overflow);
    return;
  }

  SDValue Temp = DAG.CreateStackTemporary(PtrVT);
  // Temporary for the overflow value, default it to zero.
  SDValue Chain =
      DAG.getStore(DAG.getEntryNode(), dl, DAG.getConstant(0, dl, PtrVT), Temp,
                   MachinePointerInfo());

  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;
  for (const SDValue &Op : N->op_values()) {
    EVT ArgVT = Op.getValueType();
    Type *ArgTy = ArgVT.getTypeForEVT(*DAG.getContext());
    Entry.Node = Op;
    Entry.Ty = ArgTy;
    Entry.IsSExt = true;
    Entry.IsZExt = false;
    Args.push_back(Entry);
  }

  // Also pass the address of the overflow check.
  Entry.Node = Temp;
  Entry.Ty = PointerType::getUnqual(PtrTy->getContext());
  Entry.IsSExt = true;
  Entry.IsZExt = false;
  Args.push_back(Entry);

  SDValue Func = DAG.getExternalSymbol(TLI.getLibcallName(LC), PtrVT);

  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl)
      .setChain(Chain)
      .setLibCallee(TLI.getLibcallCallingConv(LC), RetTy, Func, std::move(Args))
      .setSExtResult();

  std::pair<SDValue, SDValue> CallInfo = TLI.LowerCallTo(CLI);

  SplitInteger(CallInfo.first, Lo, Hi);
  SDValue Temp2 =
      DAG.getLoad(PtrVT, dl, CallInfo.second, Temp, MachinePointerInfo());
  SDValue Ofl = DAG.getSetCC(dl, N->getValueType(1), Temp2,
                             DAG.getConstant(0, dl, PtrVT),
                             ISD::SETNE);
  // Use the overflow from the libcall everywhere.
  ReplaceValueWith(SDValue(N, 1), Ofl);
}

void DAGTypeLegalizer::ExpandIntRes_UDIV(SDNode *N,
                                         SDValue &Lo, SDValue &Hi) {
  EVT VT = N->getValueType(0);
  SDLoc dl(N);
  SDValue Ops[2] = { N->getOperand(0), N->getOperand(1) };

  if (TLI.getOperationAction(ISD::UDIVREM, VT) == TargetLowering::Custom) {
    SDValue Res = DAG.getNode(ISD::UDIVREM, dl, DAG.getVTList(VT, VT), Ops);
    SplitInteger(Res.getValue(0), Lo, Hi);
    return;
  }

  // Try to expand UDIV by constant.
  if (isa<ConstantSDNode>(N->getOperand(1))) {
    EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
    // Only if the new type is legal.
    if (isTypeLegal(NVT)) {
      SDValue InL, InH;
      GetExpandedInteger(N->getOperand(0), InL, InH);
      SmallVector<SDValue> Result;
      if (TLI.expandDIVREMByConstant(N, Result, NVT, DAG, InL, InH)) {
        Lo = Result[0];
        Hi = Result[1];
        return;
      }
    }
  }

  RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
  if (VT == MVT::i16)
    LC = RTLIB::UDIV_I16;
  else if (VT == MVT::i32)
    LC = RTLIB::UDIV_I32;
  else if (VT == MVT::i64)
    LC = RTLIB::UDIV_I64;
  else if (VT == MVT::i128)
    LC = RTLIB::UDIV_I128;
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported UDIV!");

  TargetLowering::MakeLibCallOptions CallOptions;
  SplitInteger(TLI.makeLibCall(DAG, LC, VT, Ops, CallOptions, dl).first, Lo, Hi);
}

void DAGTypeLegalizer::ExpandIntRes_UREM(SDNode *N,
                                         SDValue &Lo, SDValue &Hi) {
  EVT VT = N->getValueType(0);
  SDLoc dl(N);
  SDValue Ops[2] = { N->getOperand(0), N->getOperand(1) };

  if (TLI.getOperationAction(ISD::UDIVREM, VT) == TargetLowering::Custom) {
    SDValue Res = DAG.getNode(ISD::UDIVREM, dl, DAG.getVTList(VT, VT), Ops);
    SplitInteger(Res.getValue(1), Lo, Hi);
    return;
  }

  // Try to expand UREM by constant.
  if (isa<ConstantSDNode>(N->getOperand(1))) {
    EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
    // Only if the new type is legal.
    if (isTypeLegal(NVT)) {
      SDValue InL, InH;
      GetExpandedInteger(N->getOperand(0), InL, InH);
      SmallVector<SDValue> Result;
      if (TLI.expandDIVREMByConstant(N, Result, NVT, DAG, InL, InH)) {
        Lo = Result[0];
        Hi = Result[1];
        return;
      }
    }
  }

  RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
  if (VT == MVT::i16)
    LC = RTLIB::UREM_I16;
  else if (VT == MVT::i32)
    LC = RTLIB::UREM_I32;
  else if (VT == MVT::i64)
    LC = RTLIB::UREM_I64;
  else if (VT == MVT::i128)
    LC = RTLIB::UREM_I128;
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported UREM!");

  TargetLowering::MakeLibCallOptions CallOptions;
  SplitInteger(TLI.makeLibCall(DAG, LC, VT, Ops, CallOptions, dl).first, Lo, Hi);
}

void DAGTypeLegalizer::ExpandIntRes_ZERO_EXTEND(SDNode *N,
                                                SDValue &Lo, SDValue &Hi) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDLoc dl(N);
  SDValue Op = N->getOperand(0);
  if (Op.getValueType().bitsLE(NVT)) {
    // The low part is zero extension of the input (degenerates to a copy).
    Lo = DAG.getNode(ISD::ZERO_EXTEND, dl, NVT, N->getOperand(0));
    Hi = DAG.getConstant(0, dl, NVT);   // The high part is just a zero.
  } else {
    // For example, extension of an i48 to an i64.  The operand type necessarily
    // promotes to the result type, so will end up being expanded too.
    assert(getTypeAction(Op.getValueType()) ==
           TargetLowering::TypePromoteInteger &&
           "Only know how to promote this result!");
    SDValue Res = GetPromotedInteger(Op);
    assert(Res.getValueType() == N->getValueType(0) &&
           "Operand over promoted?");
    // Split the promoted operand.  This will simplify when it is expanded.
    SplitInteger(Res, Lo, Hi);
    unsigned ExcessBits = Op.getValueSizeInBits() - NVT.getSizeInBits();
    Hi = DAG.getZeroExtendInReg(Hi, dl,
                                EVT::getIntegerVT(*DAG.getContext(),
                                                  ExcessBits));
  }
}

void DAGTypeLegalizer::ExpandIntRes_ATOMIC_LOAD(SDNode *N,
                                                SDValue &Lo, SDValue &Hi) {
  SDLoc dl(N);
  EVT VT = cast<AtomicSDNode>(N)->getMemoryVT();
  SDVTList VTs = DAG.getVTList(VT, MVT::i1, MVT::Other);
  SDValue Zero = DAG.getConstant(0, dl, VT);
  SDValue Swap = DAG.getAtomicCmpSwap(
      ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS, dl,
      cast<AtomicSDNode>(N)->getMemoryVT(), VTs, N->getOperand(0),
      N->getOperand(1), Zero, Zero, cast<AtomicSDNode>(N)->getMemOperand());

  ReplaceValueWith(SDValue(N, 0), Swap.getValue(0));
  ReplaceValueWith(SDValue(N, 1), Swap.getValue(2));
}

void DAGTypeLegalizer::ExpandIntRes_VECREDUCE(SDNode *N,
                                              SDValue &Lo, SDValue &Hi) {
  // TODO For VECREDUCE_(AND|OR|XOR) we could split the vector and calculate
  // both halves independently.
  SDValue Res = TLI.expandVecReduce(N, DAG);
  SplitInteger(Res, Lo, Hi);
}

void DAGTypeLegalizer::ExpandIntRes_Rotate(SDNode *N,
                                           SDValue &Lo, SDValue &Hi) {
  // Delegate to funnel-shift expansion.
  SDLoc DL(N);
  unsigned Opcode = N->getOpcode() == ISD::ROTL ? ISD::FSHL : ISD::FSHR;
  SDValue Res = DAG.getNode(Opcode, DL, N->getValueType(0), N->getOperand(0),
                            N->getOperand(0), N->getOperand(1));
  SplitInteger(Res, Lo, Hi);
}

void DAGTypeLegalizer::ExpandIntRes_FunnelShift(SDNode *N, SDValue &Lo,
                                                SDValue &Hi) {
  // Values numbered from least significant to most significant.
  SDValue In1, In2, In3, In4;
  GetExpandedInteger(N->getOperand(0), In3, In4);
  GetExpandedInteger(N->getOperand(1), In1, In2);
  EVT HalfVT = In1.getValueType();

  SDLoc DL(N);
  unsigned Opc = N->getOpcode();
  SDValue ShAmt = N->getOperand(2);
  EVT ShAmtVT = ShAmt.getValueType();
  EVT ShAmtCCVT = getSetCCResultType(ShAmtVT);

  // If the shift amount is at least half the bitwidth, swap the inputs.
  unsigned HalfVTBits = HalfVT.getScalarSizeInBits();
  SDValue AndNode = DAG.getNode(ISD::AND, DL, ShAmtVT, ShAmt,
                                DAG.getConstant(HalfVTBits, DL, ShAmtVT));
  SDValue Cond =
      DAG.getSetCC(DL, ShAmtCCVT, AndNode, DAG.getConstant(0, DL, ShAmtVT),
                   Opc == ISD::FSHL ? ISD::SETNE : ISD::SETEQ);

  // Expand to a pair of funnel shifts.
  EVT NewShAmtVT = TLI.getShiftAmountTy(HalfVT, DAG.getDataLayout());
  SDValue NewShAmt = DAG.getAnyExtOrTrunc(ShAmt, DL, NewShAmtVT);

  SDValue Select1 = DAG.getNode(ISD::SELECT, DL, HalfVT, Cond, In1, In2);
  SDValue Select2 = DAG.getNode(ISD::SELECT, DL, HalfVT, Cond, In2, In3);
  SDValue Select3 = DAG.getNode(ISD::SELECT, DL, HalfVT, Cond, In3, In4);
  Lo = DAG.getNode(Opc, DL, HalfVT, Select2, Select1, NewShAmt);
  Hi = DAG.getNode(Opc, DL, HalfVT, Select3, Select2, NewShAmt);
}

void DAGTypeLegalizer::ExpandIntRes_VSCALE(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  EVT VT = N->getValueType(0);
  EVT HalfVT =
      EVT::getIntegerVT(*DAG.getContext(), N->getValueSizeInBits(0) / 2);
  SDLoc dl(N);

  // We assume VSCALE(1) fits into a legal integer.
  APInt One(HalfVT.getSizeInBits(), 1);
  SDValue VScaleBase = DAG.getVScale(dl, HalfVT, One);
  VScaleBase = DAG.getNode(ISD::ZERO_EXTEND, dl, VT, VScaleBase);
  SDValue Res = DAG.getNode(ISD::MUL, dl, VT, VScaleBase, N->getOperand(0));
  SplitInteger(Res, Lo, Hi);
}

//===----------------------------------------------------------------------===//
//  Integer Operand Expansion
//===----------------------------------------------------------------------===//

/// ExpandIntegerOperand - This method is called when the specified operand of
/// the specified node is found to need expansion.  At this point, all of the
/// result types of the node are known to be legal, but other operands of the
/// node may need promotion or expansion as well as the specified one.
bool DAGTypeLegalizer::ExpandIntegerOperand(SDNode *N, unsigned OpNo) {
  LLVM_DEBUG(dbgs() << "Expand integer operand: "; N->dump(&DAG));
  SDValue Res = SDValue();

  if (CustomLowerNode(N, N->getOperand(OpNo).getValueType(), false))
    return false;

  switch (N->getOpcode()) {
  default:
  #ifndef NDEBUG
    dbgs() << "ExpandIntegerOperand Op #" << OpNo << ": ";
    N->dump(&DAG); dbgs() << "\n";
  #endif
    report_fatal_error("Do not know how to expand this operator's operand!");

  case ISD::BITCAST:           Res = ExpandOp_BITCAST(N); break;
  case ISD::BR_CC:             Res = ExpandIntOp_BR_CC(N); break;
  case ISD::BUILD_VECTOR:      Res = ExpandOp_BUILD_VECTOR(N); break;
  case ISD::EXTRACT_ELEMENT:   Res = ExpandOp_EXTRACT_ELEMENT(N); break;
  case ISD::INSERT_VECTOR_ELT: Res = ExpandOp_INSERT_VECTOR_ELT(N); break;
  case ISD::SCALAR_TO_VECTOR:  Res = ExpandOp_SCALAR_TO_VECTOR(N); break;
  case ISD::EXPERIMENTAL_VP_SPLAT:
  case ISD::SPLAT_VECTOR:      Res = ExpandIntOp_SPLAT_VECTOR(N); break;
  case ISD::SELECT_CC:         Res = ExpandIntOp_SELECT_CC(N); break;
  case ISD::SETCC:             Res = ExpandIntOp_SETCC(N); break;
  case ISD::SETCCCARRY:        Res = ExpandIntOp_SETCCCARRY(N); break;
  case ISD::STRICT_SINT_TO_FP:
  case ISD::SINT_TO_FP:
  case ISD::STRICT_UINT_TO_FP:
  case ISD::UINT_TO_FP:        Res = ExpandIntOp_XINT_TO_FP(N); break;
  case ISD::STORE:   Res = ExpandIntOp_STORE(cast<StoreSDNode>(N), OpNo); break;
  case ISD::TRUNCATE:          Res = ExpandIntOp_TRUNCATE(N); break;

  case ISD::SHL:
  case ISD::SRA:
  case ISD::SRL:
  case ISD::ROTL:
  case ISD::ROTR:              Res = ExpandIntOp_Shift(N); break;
  case ISD::RETURNADDR:
  case ISD::FRAMEADDR:         Res = ExpandIntOp_RETURNADDR(N); break;

  case ISD::SCMP:
  case ISD::UCMP:              Res = ExpandIntOp_CMP(N); break;

  case ISD::ATOMIC_STORE:      Res = ExpandIntOp_ATOMIC_STORE(N); break;
  case ISD::STACKMAP:
    Res = ExpandIntOp_STACKMAP(N, OpNo);
    break;
  case ISD::PATCHPOINT:
    Res = ExpandIntOp_PATCHPOINT(N, OpNo);
    break;
  case ISD::EXPERIMENTAL_VP_STRIDED_LOAD:
  case ISD::EXPERIMENTAL_VP_STRIDED_STORE:
    Res = ExpandIntOp_VP_STRIDED(N, OpNo);
    break;
  }

  // If the result is null, the sub-method took care of registering results etc.
  if (!Res.getNode()) return false;

  // If the result is N, the sub-method updated N in place.  Tell the legalizer
  // core about this.
  if (Res.getNode() == N)
    return true;

  assert(Res.getValueType() == N->getValueType(0) && N->getNumValues() == 1 &&
         "Invalid operand expansion");

  ReplaceValueWith(SDValue(N, 0), Res);
  return false;
}

/// IntegerExpandSetCCOperands - Expand the operands of a comparison.  This code
/// is shared among BR_CC, SELECT_CC, and SETCC handlers.
void DAGTypeLegalizer::IntegerExpandSetCCOperands(SDValue &NewLHS,
                                                  SDValue &NewRHS,
                                                  ISD::CondCode &CCCode,
                                                  const SDLoc &dl) {
  SDValue LHSLo, LHSHi, RHSLo, RHSHi;
  GetExpandedInteger(NewLHS, LHSLo, LHSHi);
  GetExpandedInteger(NewRHS, RHSLo, RHSHi);

  if (CCCode == ISD::SETEQ || CCCode == ISD::SETNE) {
    if (RHSLo == RHSHi && isAllOnesConstant(RHSLo)) {
      // Equality comparison to -1.
      NewLHS = DAG.getNode(ISD::AND, dl, LHSLo.getValueType(), LHSLo, LHSHi);
      NewRHS = RHSLo;
      return;
    }

    NewLHS = DAG.getNode(ISD::XOR, dl, LHSLo.getValueType(), LHSLo, RHSLo);
    NewRHS = DAG.getNode(ISD::XOR, dl, LHSLo.getValueType(), LHSHi, RHSHi);
    NewLHS = DAG.getNode(ISD::OR, dl, NewLHS.getValueType(), NewLHS, NewRHS);
    NewRHS = DAG.getConstant(0, dl, NewLHS.getValueType());
    return;
  }

  // If this is a comparison of the sign bit, just look at the top part.
  // X > -1,  x < 0
  if (ConstantSDNode *CST = dyn_cast<ConstantSDNode>(NewRHS))
    if ((CCCode == ISD::SETLT && CST->isZero()) ||    // X < 0
        (CCCode == ISD::SETGT && CST->isAllOnes())) { // X > -1
      NewLHS = LHSHi;
      NewRHS = RHSHi;
      return;
    }

  // FIXME: This generated code sucks.
  ISD::CondCode LowCC;
  switch (CCCode) {
  default: llvm_unreachable("Unknown integer setcc!");
  case ISD::SETLT:
  case ISD::SETULT: LowCC = ISD::SETULT; break;
  case ISD::SETGT:
  case ISD::SETUGT: LowCC = ISD::SETUGT; break;
  case ISD::SETLE:
  case ISD::SETULE: LowCC = ISD::SETULE; break;
  case ISD::SETGE:
  case ISD::SETUGE: LowCC = ISD::SETUGE; break;
  }

  // LoCmp = lo(op1) < lo(op2)   // Always unsigned comparison
  // HiCmp = hi(op1) < hi(op2)   // Signedness depends on operands
  // dest  = hi(op1) == hi(op2) ? LoCmp : HiCmp;

  // NOTE: on targets without efficient SELECT of bools, we can always use
  // this identity: (B1 ? B2 : B3) --> (B1 & B2)|(!B1&B3)
  TargetLowering::DAGCombinerInfo DagCombineInfo(DAG, AfterLegalizeTypes, true,
                                                 nullptr);
  SDValue LoCmp, HiCmp;
  if (TLI.isTypeLegal(LHSLo.getValueType()) &&
      TLI.isTypeLegal(RHSLo.getValueType()))
    LoCmp = TLI.SimplifySetCC(getSetCCResultType(LHSLo.getValueType()), LHSLo,
                              RHSLo, LowCC, false, DagCombineInfo, dl);
  if (!LoCmp.getNode())
    LoCmp = DAG.getSetCC(dl, getSetCCResultType(LHSLo.getValueType()), LHSLo,
                         RHSLo, LowCC);
  if (TLI.isTypeLegal(LHSHi.getValueType()) &&
      TLI.isTypeLegal(RHSHi.getValueType()))
    HiCmp = TLI.SimplifySetCC(getSetCCResultType(LHSHi.getValueType()), LHSHi,
                              RHSHi, CCCode, false, DagCombineInfo, dl);
  if (!HiCmp.getNode())
    HiCmp =
        DAG.getNode(ISD::SETCC, dl, getSetCCResultType(LHSHi.getValueType()),
                    LHSHi, RHSHi, DAG.getCondCode(CCCode));

  ConstantSDNode *LoCmpC = dyn_cast<ConstantSDNode>(LoCmp.getNode());
  ConstantSDNode *HiCmpC = dyn_cast<ConstantSDNode>(HiCmp.getNode());

  bool EqAllowed = ISD::isTrueWhenEqual(CCCode);

  // FIXME: Is the HiCmpC->isOne() here correct for
  // ZeroOrNegativeOneBooleanContent.
  if ((EqAllowed && (HiCmpC && HiCmpC->isZero())) ||
      (!EqAllowed &&
       ((HiCmpC && HiCmpC->isOne()) || (LoCmpC && LoCmpC->isZero())))) {
    // For LE / GE, if high part is known false, ignore the low part.
    // For LT / GT: if low part is known false, return the high part.
    //              if high part is known true, ignore the low part.
    NewLHS = HiCmp;
    NewRHS = SDValue();
    return;
  }

  if (LHSHi == RHSHi) {
    // Comparing the low bits is enough.
    NewLHS = LoCmp;
    NewRHS = SDValue();
    return;
  }

  // Lower with SETCCCARRY if the target supports it.
  EVT HiVT = LHSHi.getValueType();
  EVT ExpandVT = TLI.getTypeToExpandTo(*DAG.getContext(), HiVT);
  bool HasSETCCCARRY = TLI.isOperationLegalOrCustom(ISD::SETCCCARRY, ExpandVT);

  // FIXME: Make all targets support this, then remove the other lowering.
  if (HasSETCCCARRY) {
    // SETCCCARRY can detect < and >= directly. For > and <=, flip
    // operands and condition code.
    bool FlipOperands = false;
    switch (CCCode) {
    case ISD::SETGT:  CCCode = ISD::SETLT;  FlipOperands = true; break;
    case ISD::SETUGT: CCCode = ISD::SETULT; FlipOperands = true; break;
    case ISD::SETLE:  CCCode = ISD::SETGE;  FlipOperands = true; break;
    case ISD::SETULE: CCCode = ISD::SETUGE; FlipOperands = true; break;
    default: break;
    }
    if (FlipOperands) {
      std::swap(LHSLo, RHSLo);
      std::swap(LHSHi, RHSHi);
    }
    // Perform a wide subtraction, feeding the carry from the low part into
    // SETCCCARRY. The SETCCCARRY operation is essentially looking at the high
    // part of the result of LHS - RHS. It is negative iff LHS < RHS. It is
    // zero or positive iff LHS >= RHS.
    EVT LoVT = LHSLo.getValueType();
    SDVTList VTList = DAG.getVTList(LoVT, getSetCCResultType(LoVT));
    SDValue LowCmp = DAG.getNode(ISD::USUBO, dl, VTList, LHSLo, RHSLo);
    SDValue Res = DAG.getNode(ISD::SETCCCARRY, dl, getSetCCResultType(HiVT),
                              LHSHi, RHSHi, LowCmp.getValue(1),
                              DAG.getCondCode(CCCode));
    NewLHS = Res;
    NewRHS = SDValue();
    return;
  }

  NewLHS = TLI.SimplifySetCC(getSetCCResultType(HiVT), LHSHi, RHSHi, ISD::SETEQ,
                             false, DagCombineInfo, dl);
  if (!NewLHS.getNode())
    NewLHS =
        DAG.getSetCC(dl, getSetCCResultType(HiVT), LHSHi, RHSHi, ISD::SETEQ);
  NewLHS = DAG.getSelect(dl, LoCmp.getValueType(), NewLHS, LoCmp, HiCmp);
  NewRHS = SDValue();
}

SDValue DAGTypeLegalizer::ExpandIntOp_BR_CC(SDNode *N) {
  SDValue NewLHS = N->getOperand(2), NewRHS = N->getOperand(3);
  ISD::CondCode CCCode = cast<CondCodeSDNode>(N->getOperand(1))->get();
  IntegerExpandSetCCOperands(NewLHS, NewRHS, CCCode, SDLoc(N));

  // If ExpandSetCCOperands returned a scalar, we need to compare the result
  // against zero to select between true and false values.
  if (!NewRHS.getNode()) {
    NewRHS = DAG.getConstant(0, SDLoc(N), NewLHS.getValueType());
    CCCode = ISD::SETNE;
  }

  // Update N to have the operands specified.
  return SDValue(DAG.UpdateNodeOperands(N, N->getOperand(0),
                                DAG.getCondCode(CCCode), NewLHS, NewRHS,
                                N->getOperand(4)), 0);
}

SDValue DAGTypeLegalizer::ExpandIntOp_SELECT_CC(SDNode *N) {
  SDValue NewLHS = N->getOperand(0), NewRHS = N->getOperand(1);
  ISD::CondCode CCCode = cast<CondCodeSDNode>(N->getOperand(4))->get();
  IntegerExpandSetCCOperands(NewLHS, NewRHS, CCCode, SDLoc(N));

  // If ExpandSetCCOperands returned a scalar, we need to compare the result
  // against zero to select between true and false values.
  if (!NewRHS.getNode()) {
    NewRHS = DAG.getConstant(0, SDLoc(N), NewLHS.getValueType());
    CCCode = ISD::SETNE;
  }

  // Update N to have the operands specified.
  return SDValue(DAG.UpdateNodeOperands(N, NewLHS, NewRHS,
                                N->getOperand(2), N->getOperand(3),
                                DAG.getCondCode(CCCode)), 0);
}

SDValue DAGTypeLegalizer::ExpandIntOp_SETCC(SDNode *N) {
  SDValue NewLHS = N->getOperand(0), NewRHS = N->getOperand(1);
  ISD::CondCode CCCode = cast<CondCodeSDNode>(N->getOperand(2))->get();
  IntegerExpandSetCCOperands(NewLHS, NewRHS, CCCode, SDLoc(N));

  // If ExpandSetCCOperands returned a scalar, use it.
  if (!NewRHS.getNode()) {
    assert(NewLHS.getValueType() == N->getValueType(0) &&
           "Unexpected setcc expansion!");
    return NewLHS;
  }

  // Otherwise, update N to have the operands specified.
  return SDValue(
      DAG.UpdateNodeOperands(N, NewLHS, NewRHS, DAG.getCondCode(CCCode)), 0);
}

SDValue DAGTypeLegalizer::ExpandIntOp_SETCCCARRY(SDNode *N) {
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  SDValue Carry = N->getOperand(2);
  SDValue Cond = N->getOperand(3);
  SDLoc dl = SDLoc(N);

  SDValue LHSLo, LHSHi, RHSLo, RHSHi;
  GetExpandedInteger(LHS, LHSLo, LHSHi);
  GetExpandedInteger(RHS, RHSLo, RHSHi);

  // Expand to a USUBO_CARRY for the low part and a SETCCCARRY for the high.
  SDVTList VTList = DAG.getVTList(LHSLo.getValueType(), Carry.getValueType());
  SDValue LowCmp =
      DAG.getNode(ISD::USUBO_CARRY, dl, VTList, LHSLo, RHSLo, Carry);
  return DAG.getNode(ISD::SETCCCARRY, dl, N->getValueType(0), LHSHi, RHSHi,
                     LowCmp.getValue(1), Cond);
}

SDValue DAGTypeLegalizer::ExpandIntOp_SPLAT_VECTOR(SDNode *N) {
  // Split the operand and replace with SPLAT_VECTOR_PARTS.
  SDValue Lo, Hi;
  GetExpandedInteger(N->getOperand(0), Lo, Hi);
  return DAG.getNode(ISD::SPLAT_VECTOR_PARTS, SDLoc(N), N->getValueType(0), Lo,
                     Hi);
}

SDValue DAGTypeLegalizer::ExpandIntOp_Shift(SDNode *N) {
  // The value being shifted is legal, but the shift amount is too big.
  // It follows that either the result of the shift is undefined, or the
  // upper half of the shift amount is zero.  Just use the lower half.
  SDValue Lo, Hi;
  GetExpandedInteger(N->getOperand(1), Lo, Hi);
  return SDValue(DAG.UpdateNodeOperands(N, N->getOperand(0), Lo), 0);
}

SDValue DAGTypeLegalizer::ExpandIntOp_CMP(SDNode *N) {
  return TLI.expandCMP(N, DAG);
}

SDValue DAGTypeLegalizer::ExpandIntOp_RETURNADDR(SDNode *N) {
  // The argument of RETURNADDR / FRAMEADDR builtin is 32 bit contant.  This
  // surely makes pretty nice problems on 8/16 bit targets. Just truncate this
  // constant to valid type.
  SDValue Lo, Hi;
  GetExpandedInteger(N->getOperand(0), Lo, Hi);
  return SDValue(DAG.UpdateNodeOperands(N, Lo), 0);
}

SDValue DAGTypeLegalizer::ExpandIntOp_XINT_TO_FP(SDNode *N) {
  bool IsStrict = N->isStrictFPOpcode();
  bool IsSigned = N->getOpcode() == ISD::SINT_TO_FP ||
                  N->getOpcode() == ISD::STRICT_SINT_TO_FP;
  SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();
  SDValue Op = N->getOperand(IsStrict ? 1 : 0);
  EVT DstVT = N->getValueType(0);
  RTLIB::Libcall LC = IsSigned ? RTLIB::getSINTTOFP(Op.getValueType(), DstVT)
                               : RTLIB::getUINTTOFP(Op.getValueType(), DstVT);
  assert(LC != RTLIB::UNKNOWN_LIBCALL &&
         "Don't know how to expand this XINT_TO_FP!");
  TargetLowering::MakeLibCallOptions CallOptions;
  CallOptions.setSExt(true);
  std::pair<SDValue, SDValue> Tmp =
      TLI.makeLibCall(DAG, LC, DstVT, Op, CallOptions, SDLoc(N), Chain);

  if (!IsStrict)
    return Tmp.first;

  ReplaceValueWith(SDValue(N, 1), Tmp.second);
  ReplaceValueWith(SDValue(N, 0), Tmp.first);
  return SDValue();
}

SDValue DAGTypeLegalizer::ExpandIntOp_STORE(StoreSDNode *N, unsigned OpNo) {
  assert(!N->isAtomic() && "Should have been a ATOMIC_STORE?");

  if (ISD::isNormalStore(N))
    return ExpandOp_NormalStore(N, OpNo);

  assert(ISD::isUNINDEXEDStore(N) && "Indexed store during type legalization!");
  assert(OpNo == 1 && "Can only expand the stored value so far");

  EVT VT = N->getOperand(1).getValueType();
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue Ch  = N->getChain();
  SDValue Ptr = N->getBasePtr();
  MachineMemOperand::Flags MMOFlags = N->getMemOperand()->getFlags();
  AAMDNodes AAInfo = N->getAAInfo();
  SDLoc dl(N);
  SDValue Lo, Hi;

  assert(NVT.isByteSized() && "Expanded type not byte sized!");

  if (N->getMemoryVT().bitsLE(NVT)) {
    GetExpandedInteger(N->getValue(), Lo, Hi);
    return DAG.getTruncStore(Ch, dl, Lo, Ptr, N->getPointerInfo(),
                             N->getMemoryVT(), N->getOriginalAlign(), MMOFlags,
                             AAInfo);
  }

  if (DAG.getDataLayout().isLittleEndian()) {
    // Little-endian - low bits are at low addresses.
    GetExpandedInteger(N->getValue(), Lo, Hi);

    Lo = DAG.getStore(Ch, dl, Lo, Ptr, N->getPointerInfo(),
                      N->getOriginalAlign(), MMOFlags, AAInfo);

    unsigned ExcessBits =
      N->getMemoryVT().getSizeInBits() - NVT.getSizeInBits();
    EVT NEVT = EVT::getIntegerVT(*DAG.getContext(), ExcessBits);

    // Increment the pointer to the other half.
    unsigned IncrementSize = NVT.getSizeInBits()/8;
    Ptr = DAG.getObjectPtrOffset(dl, Ptr, TypeSize::getFixed(IncrementSize));
    Hi = DAG.getTruncStore(Ch, dl, Hi, Ptr,
                           N->getPointerInfo().getWithOffset(IncrementSize),
                           NEVT, N->getOriginalAlign(), MMOFlags, AAInfo);
    return DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo, Hi);
  }

  // Big-endian - high bits are at low addresses.  Favor aligned stores at
  // the cost of some bit-fiddling.
  GetExpandedInteger(N->getValue(), Lo, Hi);

  EVT ExtVT = N->getMemoryVT();
  unsigned EBytes = ExtVT.getStoreSize();
  unsigned IncrementSize = NVT.getSizeInBits()/8;
  unsigned ExcessBits = (EBytes - IncrementSize)*8;
  EVT HiVT = EVT::getIntegerVT(*DAG.getContext(),
                               ExtVT.getSizeInBits() - ExcessBits);

  if (ExcessBits < NVT.getSizeInBits()) {
    // Transfer high bits from the top of Lo to the bottom of Hi.
    Hi = DAG.getNode(ISD::SHL, dl, NVT, Hi,
                     DAG.getConstant(NVT.getSizeInBits() - ExcessBits, dl,
                                     TLI.getPointerTy(DAG.getDataLayout())));
    Hi = DAG.getNode(
        ISD::OR, dl, NVT, Hi,
        DAG.getNode(ISD::SRL, dl, NVT, Lo,
                    DAG.getConstant(ExcessBits, dl,
                                    TLI.getPointerTy(DAG.getDataLayout()))));
  }

  // Store both the high bits and maybe some of the low bits.
  Hi = DAG.getTruncStore(Ch, dl, Hi, Ptr, N->getPointerInfo(), HiVT,
                         N->getOriginalAlign(), MMOFlags, AAInfo);

  // Increment the pointer to the other half.
  Ptr = DAG.getObjectPtrOffset(dl, Ptr, TypeSize::getFixed(IncrementSize));
  // Store the lowest ExcessBits bits in the second half.
  Lo = DAG.getTruncStore(Ch, dl, Lo, Ptr,
                         N->getPointerInfo().getWithOffset(IncrementSize),
                         EVT::getIntegerVT(*DAG.getContext(), ExcessBits),
                         N->getOriginalAlign(), MMOFlags, AAInfo);
  return DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo, Hi);
}

SDValue DAGTypeLegalizer::ExpandIntOp_TRUNCATE(SDNode *N) {
  SDValue InL, InH;
  GetExpandedInteger(N->getOperand(0), InL, InH);
  // Just truncate the low part of the source.
  return DAG.getNode(ISD::TRUNCATE, SDLoc(N), N->getValueType(0), InL);
}

SDValue DAGTypeLegalizer::ExpandIntOp_ATOMIC_STORE(SDNode *N) {
  SDLoc dl(N);
  SDValue Swap =
      DAG.getAtomic(ISD::ATOMIC_SWAP, dl, cast<AtomicSDNode>(N)->getMemoryVT(),
                    N->getOperand(0), N->getOperand(2), N->getOperand(1),
                    cast<AtomicSDNode>(N)->getMemOperand());
  return Swap.getValue(1);
}

SDValue DAGTypeLegalizer::ExpandIntOp_VP_STRIDED(SDNode *N, unsigned OpNo) {
  assert((N->getOpcode() == ISD::EXPERIMENTAL_VP_STRIDED_LOAD && OpNo == 3) ||
         (N->getOpcode() == ISD::EXPERIMENTAL_VP_STRIDED_STORE && OpNo == 4));

  SDValue Hi; // The upper half is dropped out.
  SmallVector<SDValue, 8> NewOps(N->op_begin(), N->op_end());
  GetExpandedInteger(NewOps[OpNo], NewOps[OpNo], Hi);

  return SDValue(DAG.UpdateNodeOperands(N, NewOps), 0);
}

SDValue DAGTypeLegalizer::PromoteIntRes_VECTOR_SPLICE(SDNode *N) {
  SDLoc dl(N);

  SDValue V0 = GetPromotedInteger(N->getOperand(0));
  SDValue V1 = GetPromotedInteger(N->getOperand(1));
  EVT OutVT = V0.getValueType();

  return DAG.getNode(ISD::VECTOR_SPLICE, dl, OutVT, V0, V1, N->getOperand(2));
}

SDValue DAGTypeLegalizer::PromoteIntRes_VECTOR_INTERLEAVE_DEINTERLEAVE(SDNode *N) {
  SDLoc dl(N);

  SDValue V0 = GetPromotedInteger(N->getOperand(0));
  SDValue V1 = GetPromotedInteger(N->getOperand(1));
  EVT ResVT = V0.getValueType();
  SDValue Res = DAG.getNode(N->getOpcode(), dl,
                            DAG.getVTList(ResVT, ResVT), V0, V1);
  SetPromotedInteger(SDValue(N, 0), Res.getValue(0));
  SetPromotedInteger(SDValue(N, 1), Res.getValue(1));
  return SDValue();
}

SDValue DAGTypeLegalizer::PromoteIntRes_EXTRACT_SUBVECTOR(SDNode *N) {

  EVT OutVT = N->getValueType(0);
  EVT NOutVT = TLI.getTypeToTransformTo(*DAG.getContext(), OutVT);
  assert(NOutVT.isVector() && "This type must be promoted to a vector type");
  EVT NOutVTElem = NOutVT.getVectorElementType();

  SDLoc dl(N);
  SDValue BaseIdx = N->getOperand(1);

  // TODO: We may be able to use this for types other than scalable
  // vectors and fix those tests that expect BUILD_VECTOR to be used
  if (OutVT.isScalableVector()) {
    SDValue InOp0 = N->getOperand(0);
    EVT InVT = InOp0.getValueType();

    // Try and extract from a smaller type so that it eventually falls
    // into the promotion code below.
    if (getTypeAction(InVT) == TargetLowering::TypeSplitVector ||
        getTypeAction(InVT) == TargetLowering::TypeLegal) {
      EVT NInVT = InVT.getHalfNumVectorElementsVT(*DAG.getContext());
      unsigned NElts = NInVT.getVectorMinNumElements();
      uint64_t IdxVal = BaseIdx->getAsZExtVal();

      SDValue Step1 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, NInVT, InOp0,
                                  DAG.getConstant(alignDown(IdxVal, NElts), dl,
                                                  BaseIdx.getValueType()));
      SDValue Step2 = DAG.getNode(
          ISD::EXTRACT_SUBVECTOR, dl, OutVT, Step1,
          DAG.getConstant(IdxVal % NElts, dl, BaseIdx.getValueType()));
      return DAG.getNode(ISD::ANY_EXTEND, dl, NOutVT, Step2);
    }

    // Try and extract from a widened type.
    if (getTypeAction(InVT) == TargetLowering::TypeWidenVector) {
      SDValue Ops[] = {GetWidenedVector(InOp0), BaseIdx};
      SDValue Ext = DAG.getNode(ISD::EXTRACT_SUBVECTOR, SDLoc(N), OutVT, Ops);
      return DAG.getNode(ISD::ANY_EXTEND, dl, NOutVT, Ext);
    }

    // Promote operands and see if this is handled by target lowering,
    // Otherwise, use the BUILD_VECTOR approach below
    if (getTypeAction(InVT) == TargetLowering::TypePromoteInteger) {
      // Collect the (promoted) operands
      SDValue Ops[] = { GetPromotedInteger(InOp0), BaseIdx };

      EVT PromEltVT = Ops[0].getValueType().getVectorElementType();
      assert(PromEltVT.bitsLE(NOutVTElem) &&
             "Promoted operand has an element type greater than result");

      EVT ExtVT = NOutVT.changeVectorElementType(PromEltVT);
      SDValue Ext = DAG.getNode(ISD::EXTRACT_SUBVECTOR, SDLoc(N), ExtVT, Ops);
      return DAG.getNode(ISD::ANY_EXTEND, dl, NOutVT, Ext);
    }
  }

  if (OutVT.isScalableVector())
    report_fatal_error("Unable to promote scalable types using BUILD_VECTOR");

  SDValue InOp0 = N->getOperand(0);
  if (getTypeAction(InOp0.getValueType()) == TargetLowering::TypePromoteInteger)
    InOp0 = GetPromotedInteger(InOp0);

  EVT InVT = InOp0.getValueType();
  EVT InSVT = InVT.getVectorElementType();

  unsigned OutNumElems = OutVT.getVectorNumElements();
  SmallVector<SDValue, 8> Ops;
  Ops.reserve(OutNumElems);
  for (unsigned i = 0; i != OutNumElems; ++i) {
    // Extract the element from the original vector.
    SDValue Index = DAG.getNode(ISD::ADD, dl, BaseIdx.getValueType(), BaseIdx,
                                DAG.getConstant(i, dl, BaseIdx.getValueType()));
    SDValue Ext = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, InSVT,
                              N->getOperand(0), Index);
    SDValue Op = DAG.getAnyExtOrTrunc(Ext, dl, NOutVTElem);
    // Insert the converted element to the new vector.
    Ops.push_back(Op);
  }

  return DAG.getBuildVector(NOutVT, dl, Ops);
}

SDValue DAGTypeLegalizer::PromoteIntRes_INSERT_SUBVECTOR(SDNode *N) {
  EVT OutVT = N->getValueType(0);
  EVT NOutVT = TLI.getTypeToTransformTo(*DAG.getContext(), OutVT);
  assert(NOutVT.isVector() && "This type must be promoted to a vector type");

  SDLoc dl(N);
  SDValue Vec = N->getOperand(0);
  SDValue SubVec = N->getOperand(1);
  SDValue Idx = N->getOperand(2);

  EVT SubVecVT = SubVec.getValueType();
  EVT NSubVT =
      EVT::getVectorVT(*DAG.getContext(), NOutVT.getVectorElementType(),
                       SubVecVT.getVectorElementCount());

  Vec = GetPromotedInteger(Vec);
  SubVec = DAG.getNode(ISD::ANY_EXTEND, dl, NSubVT, SubVec);

  return DAG.getNode(ISD::INSERT_SUBVECTOR, dl, NOutVT, Vec, SubVec, Idx);
}

SDValue DAGTypeLegalizer::PromoteIntRes_VECTOR_REVERSE(SDNode *N) {
  SDLoc dl(N);

  SDValue V0 = GetPromotedInteger(N->getOperand(0));
  EVT OutVT = V0.getValueType();

  return DAG.getNode(ISD::VECTOR_REVERSE, dl, OutVT, V0);
}

SDValue DAGTypeLegalizer::PromoteIntRes_VECTOR_SHUFFLE(SDNode *N) {
  ShuffleVectorSDNode *SV = cast<ShuffleVectorSDNode>(N);
  EVT VT = N->getValueType(0);
  SDLoc dl(N);

  ArrayRef<int> NewMask = SV->getMask().slice(0, VT.getVectorNumElements());

  SDValue V0 = GetPromotedInteger(N->getOperand(0));
  SDValue V1 = GetPromotedInteger(N->getOperand(1));
  EVT OutVT = V0.getValueType();

  return DAG.getVectorShuffle(OutVT, dl, V0, V1, NewMask);
}

SDValue DAGTypeLegalizer::PromoteIntRes_BUILD_VECTOR(SDNode *N) {
  EVT OutVT = N->getValueType(0);
  EVT NOutVT = TLI.getTypeToTransformTo(*DAG.getContext(), OutVT);
  assert(NOutVT.isVector() && "This type must be promoted to a vector type");
  unsigned NumElems = N->getNumOperands();
  EVT NOutVTElem = NOutVT.getVectorElementType();
  TargetLoweringBase::BooleanContent NOutBoolType = TLI.getBooleanContents(NOutVT);
  unsigned NOutExtOpc = TargetLowering::getExtendForContent(NOutBoolType);
  SDLoc dl(N);

  SmallVector<SDValue, 8> Ops;
  Ops.reserve(NumElems);
  for (unsigned i = 0; i != NumElems; ++i) {
    SDValue Op = N->getOperand(i);
    EVT OpVT = Op.getValueType();
    // BUILD_VECTOR integer operand types are allowed to be larger than the
    // result's element type. This may still be true after the promotion. For
    // example, we might be promoting (<v?i1> = BV <i32>, <i32>, ...) to
    // (v?i16 = BV <i32>, <i32>, ...), and we can't any_extend <i32> to <i16>.
    if (OpVT.bitsLT(NOutVTElem)) {
      unsigned ExtOpc = ISD::ANY_EXTEND;
      // Attempt to extend constant bool vectors to match target's BooleanContent.
      // While not necessary, this improves chances of the constant correctly
      // folding with compare results (e.g. for NOT patterns).
      if (OpVT == MVT::i1 && Op.getOpcode() == ISD::Constant)
        ExtOpc = NOutExtOpc;
      Op = DAG.getNode(ExtOpc, dl, NOutVTElem, Op);
    }
    Ops.push_back(Op);
  }

  return DAG.getBuildVector(NOutVT, dl, Ops);
}

SDValue DAGTypeLegalizer::PromoteIntRes_ScalarOp(SDNode *N) {

  SDLoc dl(N);

  assert(!N->getOperand(0).getValueType().isVector() &&
         "Input must be a scalar");

  EVT OutVT = N->getValueType(0);
  EVT NOutVT = TLI.getTypeToTransformTo(*DAG.getContext(), OutVT);
  assert(NOutVT.isVector() && "This type must be promoted to a vector type");
  EVT NOutElemVT = NOutVT.getVectorElementType();

  SDValue Op = DAG.getNode(ISD::ANY_EXTEND, dl, NOutElemVT, N->getOperand(0));
  if (N->isVPOpcode())
    return DAG.getNode(N->getOpcode(), dl, NOutVT, Op, N->getOperand(1),
                       N->getOperand(2));

  return DAG.getNode(N->getOpcode(), dl, NOutVT, Op);
}

SDValue DAGTypeLegalizer::PromoteIntRes_STEP_VECTOR(SDNode *N) {
  SDLoc dl(N);
  EVT OutVT = N->getValueType(0);
  EVT NOutVT = TLI.getTypeToTransformTo(*DAG.getContext(), OutVT);
  assert(NOutVT.isScalableVector() &&
         "Type must be promoted to a scalable vector type");
  const APInt &StepVal = N->getConstantOperandAPInt(0);
  return DAG.getStepVector(dl, NOutVT,
                           StepVal.sext(NOutVT.getScalarSizeInBits()));
}

SDValue DAGTypeLegalizer::PromoteIntRes_CONCAT_VECTORS(SDNode *N) {
  SDLoc dl(N);

  EVT OutVT = N->getValueType(0);
  EVT NOutVT = TLI.getTypeToTransformTo(*DAG.getContext(), OutVT);
  assert(NOutVT.isVector() && "This type must be promoted to a vector type");

  unsigned NumOperands = N->getNumOperands();
  unsigned NumOutElem = NOutVT.getVectorMinNumElements();
  EVT OutElemTy = NOutVT.getVectorElementType();
  if (OutVT.isScalableVector()) {
    // Find the largest promoted element type for each of the operands.
    SDUse *MaxSizedValue = std::max_element(
        N->op_begin(), N->op_end(), [](const SDValue &A, const SDValue &B) {
          EVT AVT = A.getValueType().getVectorElementType();
          EVT BVT = B.getValueType().getVectorElementType();
          return AVT.getScalarSizeInBits() < BVT.getScalarSizeInBits();
        });
    EVT MaxElementVT = MaxSizedValue->getValueType().getVectorElementType();

    // Then promote all vectors to the largest element type.
    SmallVector<SDValue, 8> Ops;
    for (unsigned I = 0; I < NumOperands; ++I) {
      SDValue Op = N->getOperand(I);
      EVT OpVT = Op.getValueType();
      if (getTypeAction(OpVT) == TargetLowering::TypePromoteInteger)
        Op = GetPromotedInteger(Op);
      else
        assert(getTypeAction(OpVT) == TargetLowering::TypeLegal &&
               "Unhandled legalization type");

      if (OpVT.getVectorElementType().getScalarSizeInBits() <
          MaxElementVT.getScalarSizeInBits())
        Op = DAG.getAnyExtOrTrunc(Op, dl,
                                  OpVT.changeVectorElementType(MaxElementVT));
      Ops.push_back(Op);
    }

    // Do the CONCAT on the promoted type and finally truncate to (the promoted)
    // NOutVT.
    return DAG.getAnyExtOrTrunc(
        DAG.getNode(ISD::CONCAT_VECTORS, dl,
                    OutVT.changeVectorElementType(MaxElementVT), Ops),
        dl, NOutVT);
  }

  unsigned NumElem = N->getOperand(0).getValueType().getVectorNumElements();
  assert(NumElem * NumOperands == NumOutElem &&
         "Unexpected number of elements");

  // Take the elements from the first vector.
  SmallVector<SDValue, 8> Ops(NumOutElem);
  for (unsigned i = 0; i < NumOperands; ++i) {
    SDValue Op = N->getOperand(i);
    if (getTypeAction(Op.getValueType()) == TargetLowering::TypePromoteInteger)
      Op = GetPromotedInteger(Op);
    EVT SclrTy = Op.getValueType().getVectorElementType();
    assert(NumElem == Op.getValueType().getVectorNumElements() &&
           "Unexpected number of elements");

    for (unsigned j = 0; j < NumElem; ++j) {
      SDValue Ext = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, SclrTy, Op,
                                DAG.getVectorIdxConstant(j, dl));
      Ops[i * NumElem + j] = DAG.getAnyExtOrTrunc(Ext, dl, OutElemTy);
    }
  }

  return DAG.getBuildVector(NOutVT, dl, Ops);
}

SDValue DAGTypeLegalizer::PromoteIntRes_EXTEND_VECTOR_INREG(SDNode *N) {
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  assert(NVT.isVector() && "This type must be promoted to a vector type");

  SDLoc dl(N);

  // For operands whose TypeAction is to promote, extend the promoted node
  // appropriately (ZERO_EXTEND or SIGN_EXTEND) from the original pre-promotion
  // type, and then construct a new *_EXTEND_VECTOR_INREG node to the promote-to
  // type..
  if (getTypeAction(N->getOperand(0).getValueType())
      == TargetLowering::TypePromoteInteger) {
    SDValue Promoted;

    switch(N->getOpcode()) {
      case ISD::SIGN_EXTEND_VECTOR_INREG:
        Promoted = SExtPromotedInteger(N->getOperand(0));
        break;
      case ISD::ZERO_EXTEND_VECTOR_INREG:
        Promoted = ZExtPromotedInteger(N->getOperand(0));
        break;
      case ISD::ANY_EXTEND_VECTOR_INREG:
        Promoted = GetPromotedInteger(N->getOperand(0));
        break;
      default:
        llvm_unreachable("Node has unexpected Opcode");
    }
    return DAG.getNode(N->getOpcode(), dl, NVT, Promoted);
  }

  // Directly extend to the appropriate transform-to type.
  return DAG.getNode(N->getOpcode(), dl, NVT, N->getOperand(0));
}

SDValue DAGTypeLegalizer::PromoteIntRes_INSERT_VECTOR_ELT(SDNode *N) {
  EVT OutVT = N->getValueType(0);
  EVT NOutVT = TLI.getTypeToTransformTo(*DAG.getContext(), OutVT);
  assert(NOutVT.isVector() && "This type must be promoted to a vector type");

  EVT NOutVTElem = NOutVT.getVectorElementType();

  SDLoc dl(N);
  SDValue V0 = GetPromotedInteger(N->getOperand(0));

  SDValue ConvElem = DAG.getNode(ISD::ANY_EXTEND, dl,
    NOutVTElem, N->getOperand(1));
  return DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, NOutVT,
    V0, ConvElem, N->getOperand(2));
}

SDValue DAGTypeLegalizer::PromoteIntRes_VECREDUCE(SDNode *N) {
  // The VECREDUCE result size may be larger than the element size, so
  // we can simply change the result type.
  SDLoc dl(N);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  return DAG.getNode(N->getOpcode(), dl, NVT, N->ops());
}

SDValue DAGTypeLegalizer::PromoteIntRes_VP_REDUCE(SDNode *N) {
  // The VP_REDUCE result size may be larger than the element size, so we can
  // simply change the result type. However the start value and result must be
  // the same.
  SDLoc DL(N);
  SDValue Start = PromoteIntOpVectorReduction(N, N->getOperand(0));
  return DAG.getNode(N->getOpcode(), DL, Start.getValueType(), Start,
                     N->getOperand(1), N->getOperand(2), N->getOperand(3));
}

SDValue DAGTypeLegalizer::PromoteIntRes_PATCHPOINT(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDLoc dl(N);

  assert(N->getNumValues() == 3 && "Expected 3 values for PATCHPOINT");
  SDVTList VTList = DAG.getVTList({NVT, MVT::Other, MVT::Glue});

  SmallVector<SDValue> Ops(N->ops());
  SDValue Res = DAG.getNode(ISD::PATCHPOINT, dl, VTList, Ops);

  // Replace chain and glue uses with the new patchpoint.
  SDValue From[] = {SDValue(N, 1), SDValue(N, 2)};
  SDValue To[] = {Res.getValue(1), Res.getValue(2)};
  DAG.ReplaceAllUsesOfValuesWith(From, To, 2);

  return Res.getValue(0);
}

SDValue DAGTypeLegalizer::PromoteIntOp_EXTRACT_VECTOR_ELT(SDNode *N) {
  SDLoc dl(N);
  SDValue V0 = GetPromotedInteger(N->getOperand(0));
  SDValue V1 = DAG.getZExtOrTrunc(N->getOperand(1), dl,
                                  TLI.getVectorIdxTy(DAG.getDataLayout()));
  SDValue Ext = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl,
    V0->getValueType(0).getScalarType(), V0, V1);

  // EXTRACT_VECTOR_ELT can return types which are wider than the incoming
  // element types. If this is the case then we need to expand the outgoing
  // value and not truncate it.
  return DAG.getAnyExtOrTrunc(Ext, dl, N->getValueType(0));
}

SDValue DAGTypeLegalizer::PromoteIntOp_INSERT_SUBVECTOR(SDNode *N) {
  SDLoc dl(N);
  // The result type is equal to the first input operand's type, so the
  // type that needs promoting must be the second source vector.
  SDValue V0 = N->getOperand(0);
  SDValue V1 = GetPromotedInteger(N->getOperand(1));
  SDValue Idx = N->getOperand(2);
  EVT PromVT = EVT::getVectorVT(*DAG.getContext(),
                                V1.getValueType().getVectorElementType(),
                                V0.getValueType().getVectorElementCount());
  V0 = DAG.getAnyExtOrTrunc(V0, dl, PromVT);
  SDValue Ext = DAG.getNode(ISD::INSERT_SUBVECTOR, dl, PromVT, V0, V1, Idx);
  return DAG.getAnyExtOrTrunc(Ext, dl, N->getValueType(0));
}

SDValue DAGTypeLegalizer::PromoteIntOp_EXTRACT_SUBVECTOR(SDNode *N) {
  SDLoc dl(N);
  SDValue V0 = GetPromotedInteger(N->getOperand(0));
  MVT InVT = V0.getValueType().getSimpleVT();
  MVT OutVT = MVT::getVectorVT(InVT.getVectorElementType(),
                               N->getValueType(0).getVectorNumElements());
  SDValue Ext = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, OutVT, V0, N->getOperand(1));
  return DAG.getNode(ISD::TRUNCATE, dl, N->getValueType(0), Ext);
}

SDValue DAGTypeLegalizer::PromoteIntOp_CONCAT_VECTORS(SDNode *N) {
  SDLoc dl(N);

  EVT ResVT = N->getValueType(0);
  unsigned NumElems = N->getNumOperands();

  if (ResVT.isScalableVector()) {
    SDValue ResVec = DAG.getUNDEF(ResVT);

    for (unsigned OpIdx = 0; OpIdx < NumElems; ++OpIdx) {
      SDValue Op = N->getOperand(OpIdx);
      unsigned OpNumElts = Op.getValueType().getVectorMinNumElements();
      ResVec = DAG.getNode(ISD::INSERT_SUBVECTOR, dl, ResVT, ResVec, Op,
                           DAG.getIntPtrConstant(OpIdx * OpNumElts, dl));
    }

    return ResVec;
  }

  EVT RetSclrTy = N->getValueType(0).getVectorElementType();

  SmallVector<SDValue, 8> NewOps;
  NewOps.reserve(NumElems);

  // For each incoming vector
  for (unsigned VecIdx = 0; VecIdx != NumElems; ++VecIdx) {
    SDValue Incoming = GetPromotedInteger(N->getOperand(VecIdx));
    EVT SclrTy = Incoming->getValueType(0).getVectorElementType();
    unsigned NumElem = Incoming->getValueType(0).getVectorNumElements();

    for (unsigned i=0; i<NumElem; ++i) {
      // Extract element from incoming vector
      SDValue Ex = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, SclrTy, Incoming,
                               DAG.getVectorIdxConstant(i, dl));
      SDValue Tr = DAG.getNode(ISD::TRUNCATE, dl, RetSclrTy, Ex);
      NewOps.push_back(Tr);
    }
  }

  return DAG.getBuildVector(N->getValueType(0), dl, NewOps);
}

SDValue DAGTypeLegalizer::ExpandIntOp_STACKMAP(SDNode *N, unsigned OpNo) {
  assert(OpNo > 1);
  SDValue Op = N->getOperand(OpNo);

  // FIXME: Non-constant operands are not yet handled:
  //  - https://github.com/llvm/llvm-project/issues/26431
  //  - https://github.com/llvm/llvm-project/issues/55957
  ConstantSDNode *CN = dyn_cast<ConstantSDNode>(Op);
  if (!CN)
    return SDValue();

  // Copy operands before the one being expanded.
  SmallVector<SDValue> NewOps;
  for (unsigned I = 0; I < OpNo; I++)
    NewOps.push_back(N->getOperand(I));

  EVT Ty = Op.getValueType();
  SDLoc DL = SDLoc(N);
  if (CN->getConstantIntValue()->getValue().getActiveBits() < 64) {
    NewOps.push_back(
        DAG.getTargetConstant(StackMaps::ConstantOp, DL, MVT::i64));
    NewOps.push_back(DAG.getTargetConstant(CN->getZExtValue(), DL, Ty));
  } else {
    // FIXME: https://github.com/llvm/llvm-project/issues/55609
    return SDValue();
  }

  // Copy remaining operands.
  for (unsigned I = OpNo + 1; I < N->getNumOperands(); I++)
    NewOps.push_back(N->getOperand(I));

  SDValue NewNode = DAG.getNode(N->getOpcode(), DL, N->getVTList(), NewOps);

  for (unsigned ResNum = 0; ResNum < N->getNumValues(); ResNum++)
    ReplaceValueWith(SDValue(N, ResNum), NewNode.getValue(ResNum));

  return SDValue(); // Signal that we have replaced the node already.
}

SDValue DAGTypeLegalizer::ExpandIntOp_PATCHPOINT(SDNode *N, unsigned OpNo) {
  assert(OpNo >= 7);
  SDValue Op = N->getOperand(OpNo);

  // FIXME: Non-constant operands are not yet handled:
  //  - https://github.com/llvm/llvm-project/issues/26431
  //  - https://github.com/llvm/llvm-project/issues/55957
  ConstantSDNode *CN = dyn_cast<ConstantSDNode>(Op);
  if (!CN)
    return SDValue();

  // Copy operands before the one being expanded.
  SmallVector<SDValue> NewOps;
  for (unsigned I = 0; I < OpNo; I++)
    NewOps.push_back(N->getOperand(I));

  EVT Ty = Op.getValueType();
  SDLoc DL = SDLoc(N);
  if (CN->getConstantIntValue()->getValue().getActiveBits() < 64) {
    NewOps.push_back(
        DAG.getTargetConstant(StackMaps::ConstantOp, DL, MVT::i64));
    NewOps.push_back(DAG.getTargetConstant(CN->getZExtValue(), DL, Ty));
  } else {
    // FIXME: https://github.com/llvm/llvm-project/issues/55609
    return SDValue();
  }

  // Copy remaining operands.
  for (unsigned I = OpNo + 1; I < N->getNumOperands(); I++)
    NewOps.push_back(N->getOperand(I));

  SDValue NewNode = DAG.getNode(N->getOpcode(), DL, N->getVTList(), NewOps);

  for (unsigned ResNum = 0; ResNum < N->getNumValues(); ResNum++)
    ReplaceValueWith(SDValue(N, ResNum), NewNode.getValue(ResNum));

  return SDValue(); // Signal that we have replaced the node already.
}
