//===-------- LegalizeFloatTypes.cpp - Legalization of float types --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements float type expansion and softening for LegalizeTypes.
// Softening is the act of turning a computation in an illegal floating point
// type into a computation in an integer type of the same size; also known as
// "soft float".  For example, turning f32 arithmetic into operations using i32.
// The resulting integer value is the same as what you would get by performing
// the floating point operation and bitcasting the result to the integer type.
// Expansion is the act of changing a computation in an illegal type to be a
// computation in two identical registers of a smaller type.  For example,
// implementing ppcf128 arithmetic in two f64 registers.
//
//===----------------------------------------------------------------------===//

#include "LegalizeTypes.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "legalize-types"

/// GetFPLibCall - Return the right libcall for the given floating point type.
/// FIXME: This is a local version of RTLIB::getFPLibCall that should be
///        refactored away (see RTLIB::getPOWI for an example).
static RTLIB::Libcall GetFPLibCall(EVT VT,
                                   RTLIB::Libcall Call_F32,
                                   RTLIB::Libcall Call_F64,
                                   RTLIB::Libcall Call_F80,
                                   RTLIB::Libcall Call_F128,
                                   RTLIB::Libcall Call_PPCF128) {
  return
    VT == MVT::f32 ? Call_F32 :
    VT == MVT::f64 ? Call_F64 :
    VT == MVT::f80 ? Call_F80 :
    VT == MVT::f128 ? Call_F128 :
    VT == MVT::ppcf128 ? Call_PPCF128 :
    RTLIB::UNKNOWN_LIBCALL;
}

//===----------------------------------------------------------------------===//
//  Convert Float Results to Integer
//===----------------------------------------------------------------------===//

void DAGTypeLegalizer::SoftenFloatResult(SDNode *N, unsigned ResNo) {
  LLVM_DEBUG(dbgs() << "Soften float result " << ResNo << ": "; N->dump(&DAG));
  SDValue R = SDValue();

  switch (N->getOpcode()) {
    // clang-format off
  default:
#ifndef NDEBUG
    dbgs() << "SoftenFloatResult #" << ResNo << ": ";
    N->dump(&DAG); dbgs() << "\n";
#endif
    report_fatal_error("Do not know how to soften the result of this "
                       "operator!");
    case ISD::EXTRACT_ELEMENT: R = SoftenFloatRes_EXTRACT_ELEMENT(N); break;
    case ISD::ARITH_FENCE: R = SoftenFloatRes_ARITH_FENCE(N); break;
    case ISD::MERGE_VALUES:R = SoftenFloatRes_MERGE_VALUES(N, ResNo); break;
    case ISD::BITCAST:     R = SoftenFloatRes_BITCAST(N); break;
    case ISD::BUILD_PAIR:  R = SoftenFloatRes_BUILD_PAIR(N); break;
    case ISD::ConstantFP:  R = SoftenFloatRes_ConstantFP(N); break;
    case ISD::EXTRACT_VECTOR_ELT:
      R = SoftenFloatRes_EXTRACT_VECTOR_ELT(N, ResNo); break;
    case ISD::FABS:        R = SoftenFloatRes_FABS(N); break;
    case ISD::STRICT_FMINNUM:
    case ISD::FMINNUM:     R = SoftenFloatRes_FMINNUM(N); break;
    case ISD::STRICT_FMAXNUM:
    case ISD::FMAXNUM:     R = SoftenFloatRes_FMAXNUM(N); break;
    case ISD::STRICT_FADD:
    case ISD::FADD:        R = SoftenFloatRes_FADD(N); break;
    case ISD::STRICT_FACOS:
    case ISD::FACOS:       R = SoftenFloatRes_FACOS(N); break;
    case ISD::STRICT_FASIN:
    case ISD::FASIN:       R = SoftenFloatRes_FASIN(N); break;
    case ISD::STRICT_FATAN:
    case ISD::FATAN:       R = SoftenFloatRes_FATAN(N); break;
    case ISD::FCBRT:       R = SoftenFloatRes_FCBRT(N); break;
    case ISD::STRICT_FCEIL:
    case ISD::FCEIL:       R = SoftenFloatRes_FCEIL(N); break;
    case ISD::FCOPYSIGN:   R = SoftenFloatRes_FCOPYSIGN(N); break;
    case ISD::STRICT_FCOS:
    case ISD::FCOS:        R = SoftenFloatRes_FCOS(N); break;
    case ISD::STRICT_FCOSH:
    case ISD::FCOSH:       R = SoftenFloatRes_FCOSH(N); break;
    case ISD::STRICT_FDIV:
    case ISD::FDIV:        R = SoftenFloatRes_FDIV(N); break;
    case ISD::STRICT_FEXP:
    case ISD::FEXP:        R = SoftenFloatRes_FEXP(N); break;
    case ISD::STRICT_FEXP2:
    case ISD::FEXP2:       R = SoftenFloatRes_FEXP2(N); break;
    case ISD::FEXP10:      R = SoftenFloatRes_FEXP10(N); break;
    case ISD::STRICT_FFLOOR:
    case ISD::FFLOOR:      R = SoftenFloatRes_FFLOOR(N); break;
    case ISD::STRICT_FLOG:
    case ISD::FLOG:        R = SoftenFloatRes_FLOG(N); break;
    case ISD::STRICT_FLOG2:
    case ISD::FLOG2:       R = SoftenFloatRes_FLOG2(N); break;
    case ISD::STRICT_FLOG10:
    case ISD::FLOG10:      R = SoftenFloatRes_FLOG10(N); break;
    case ISD::STRICT_FMA:
    case ISD::FMA:         R = SoftenFloatRes_FMA(N); break;
    case ISD::STRICT_FMUL:
    case ISD::FMUL:        R = SoftenFloatRes_FMUL(N); break;
    case ISD::STRICT_FNEARBYINT:
    case ISD::FNEARBYINT:  R = SoftenFloatRes_FNEARBYINT(N); break;
    case ISD::FNEG:        R = SoftenFloatRes_FNEG(N); break;
    case ISD::STRICT_FP_EXTEND:
    case ISD::FP_EXTEND:   R = SoftenFloatRes_FP_EXTEND(N); break;
    case ISD::STRICT_FP_ROUND:
    case ISD::FP_ROUND:    R = SoftenFloatRes_FP_ROUND(N); break;
    case ISD::FP16_TO_FP:  R = SoftenFloatRes_FP16_TO_FP(N); break;
    case ISD::BF16_TO_FP:  R = SoftenFloatRes_BF16_TO_FP(N); break;
    case ISD::STRICT_FPOW:
    case ISD::FPOW:        R = SoftenFloatRes_FPOW(N); break;
    case ISD::STRICT_FPOWI:
    case ISD::FPOWI:
    case ISD::FLDEXP:
    case ISD::STRICT_FLDEXP: R = SoftenFloatRes_ExpOp(N); break;
    case ISD::FFREXP:        R = SoftenFloatRes_FFREXP(N); break;
    case ISD::STRICT_FREM:
    case ISD::FREM:        R = SoftenFloatRes_FREM(N); break;
    case ISD::STRICT_FRINT:
    case ISD::FRINT:       R = SoftenFloatRes_FRINT(N); break;
    case ISD::STRICT_FROUND:
    case ISD::FROUND:      R = SoftenFloatRes_FROUND(N); break;
    case ISD::STRICT_FROUNDEVEN:
    case ISD::FROUNDEVEN:  R = SoftenFloatRes_FROUNDEVEN(N); break;
    case ISD::STRICT_FSIN:
    case ISD::FSIN:        R = SoftenFloatRes_FSIN(N); break;
    case ISD::STRICT_FSINH:
    case ISD::FSINH:       R = SoftenFloatRes_FSINH(N); break;
    case ISD::STRICT_FSQRT:
    case ISD::FSQRT:       R = SoftenFloatRes_FSQRT(N); break;
    case ISD::STRICT_FSUB:
    case ISD::FSUB:        R = SoftenFloatRes_FSUB(N); break;
    case ISD::STRICT_FTAN:
    case ISD::FTAN:        R = SoftenFloatRes_FTAN(N); break;
    case ISD::STRICT_FTANH:
    case ISD::FTANH:       R = SoftenFloatRes_FTANH(N); break;
    case ISD::STRICT_FTRUNC:
    case ISD::FTRUNC:      R = SoftenFloatRes_FTRUNC(N); break;
    case ISD::LOAD:        R = SoftenFloatRes_LOAD(N); break;
    case ISD::ATOMIC_LOAD: R = SoftenFloatRes_ATOMIC_LOAD(N); break;
    case ISD::ATOMIC_SWAP: R = BitcastToInt_ATOMIC_SWAP(N); break;
    case ISD::SELECT:      R = SoftenFloatRes_SELECT(N); break;
    case ISD::SELECT_CC:   R = SoftenFloatRes_SELECT_CC(N); break;
    case ISD::FREEZE:      R = SoftenFloatRes_FREEZE(N); break;
    case ISD::STRICT_SINT_TO_FP:
    case ISD::STRICT_UINT_TO_FP:
    case ISD::SINT_TO_FP:
    case ISD::UINT_TO_FP:  R = SoftenFloatRes_XINT_TO_FP(N); break;
    case ISD::UNDEF:       R = SoftenFloatRes_UNDEF(N); break;
    case ISD::VAARG:       R = SoftenFloatRes_VAARG(N); break;
    case ISD::VECREDUCE_FADD:
    case ISD::VECREDUCE_FMUL:
    case ISD::VECREDUCE_FMIN:
    case ISD::VECREDUCE_FMAX:
    case ISD::VECREDUCE_FMAXIMUM:
    case ISD::VECREDUCE_FMINIMUM: R = SoftenFloatRes_VECREDUCE(N); break;
    case ISD::VECREDUCE_SEQ_FADD:
    case ISD::VECREDUCE_SEQ_FMUL: R = SoftenFloatRes_VECREDUCE_SEQ(N); break;
      // clang-format on
    }

  // If R is null, the sub-method took care of registering the result.
  if (R.getNode()) {
    assert(R.getNode() != N);
    SetSoftenedFloat(SDValue(N, ResNo), R);
  }
}

SDValue DAGTypeLegalizer::SoftenFloatRes_Unary(SDNode *N, RTLIB::Libcall LC) {
  bool IsStrict = N->isStrictFPOpcode();
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  unsigned Offset = IsStrict ? 1 : 0;
  assert(N->getNumOperands() == (1 + Offset) &&
         "Unexpected number of operands!");
  SDValue Op = GetSoftenedFloat(N->getOperand(0 + Offset));
  SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();
  TargetLowering::MakeLibCallOptions CallOptions;
  EVT OpVT = N->getOperand(0 + Offset).getValueType();
  CallOptions.setTypeListBeforeSoften(OpVT, N->getValueType(0), true);
  std::pair<SDValue, SDValue> Tmp = TLI.makeLibCall(DAG, LC, NVT, Op,
                                                    CallOptions, SDLoc(N),
                                                    Chain);
  if (IsStrict)
    ReplaceValueWith(SDValue(N, 1), Tmp.second);
  return Tmp.first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_Binary(SDNode *N, RTLIB::Libcall LC) {
  bool IsStrict = N->isStrictFPOpcode();
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  unsigned Offset = IsStrict ? 1 : 0;
  assert(N->getNumOperands() == (2 + Offset) &&
         "Unexpected number of operands!");
  SDValue Ops[2] = { GetSoftenedFloat(N->getOperand(0 + Offset)),
                     GetSoftenedFloat(N->getOperand(1 + Offset)) };
  SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();
  TargetLowering::MakeLibCallOptions CallOptions;
  EVT OpsVT[2] = { N->getOperand(0 + Offset).getValueType(),
                   N->getOperand(1 + Offset).getValueType() };
  CallOptions.setTypeListBeforeSoften(OpsVT, N->getValueType(0), true);
  std::pair<SDValue, SDValue> Tmp = TLI.makeLibCall(DAG, LC, NVT, Ops,
                                                    CallOptions, SDLoc(N),
                                                    Chain);
  if (IsStrict)
    ReplaceValueWith(SDValue(N, 1), Tmp.second);
  return Tmp.first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_BITCAST(SDNode *N) {
  return BitConvertToInteger(N->getOperand(0));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FREEZE(SDNode *N) {
  EVT Ty = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  return DAG.getNode(ISD::FREEZE, SDLoc(N), Ty,
                     GetSoftenedFloat(N->getOperand(0)));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_ARITH_FENCE(SDNode *N) {
  EVT Ty = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue NewFence = DAG.getNode(ISD::ARITH_FENCE, SDLoc(N), Ty,
                                 GetSoftenedFloat(N->getOperand(0)));
  return NewFence;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_MERGE_VALUES(SDNode *N,
                                                      unsigned ResNo) {
  SDValue Op = DisintegrateMERGE_VALUES(N, ResNo);
  return BitConvertToInteger(Op);
}

SDValue DAGTypeLegalizer::SoftenFloatRes_BUILD_PAIR(SDNode *N) {
  // Convert the inputs to integers, and build a new pair out of them.
  return DAG.getNode(ISD::BUILD_PAIR, SDLoc(N),
                     TLI.getTypeToTransformTo(*DAG.getContext(),
                                              N->getValueType(0)),
                     BitConvertToInteger(N->getOperand(0)),
                     BitConvertToInteger(N->getOperand(1)));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_ConstantFP(SDNode *N) {
  ConstantFPSDNode *CN = cast<ConstantFPSDNode>(N);
  // In ppcf128, the high 64 bits are always first in memory regardless
  // of Endianness. LLVM's APFloat representation is not Endian sensitive,
  // and so always converts into a 128-bit APInt in a non-Endian-sensitive
  // way. However, APInt's are serialized in an Endian-sensitive fashion,
  // so on big-Endian targets, the two doubles are output in the wrong
  // order. Fix this by manually flipping the order of the high 64 bits
  // and the low 64 bits here.
  if (DAG.getDataLayout().isBigEndian() &&
      CN->getValueType(0).getSimpleVT() == llvm::MVT::ppcf128) {
    uint64_t words[2] = { CN->getValueAPF().bitcastToAPInt().getRawData()[1],
                          CN->getValueAPF().bitcastToAPInt().getRawData()[0] };
    APInt Val(128, words);
    return DAG.getConstant(Val, SDLoc(CN),
                           TLI.getTypeToTransformTo(*DAG.getContext(),
                                                    CN->getValueType(0)));
  } else {
    return DAG.getConstant(CN->getValueAPF().bitcastToAPInt(), SDLoc(CN),
                           TLI.getTypeToTransformTo(*DAG.getContext(),
                                                    CN->getValueType(0)));
  }
}

SDValue DAGTypeLegalizer::SoftenFloatRes_EXTRACT_ELEMENT(SDNode *N) {
  SDValue Src = N->getOperand(0);
  assert(Src.getValueType() == MVT::ppcf128 &&
         "In floats only ppcf128 can be extracted by element!");
  return DAG.getNode(ISD::EXTRACT_ELEMENT, SDLoc(N),
                     N->getValueType(0).changeTypeToInteger(),
                     DAG.getBitcast(MVT::i128, Src), N->getOperand(1));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_EXTRACT_VECTOR_ELT(SDNode *N, unsigned ResNo) {
  SDValue NewOp = BitConvertVectorToIntegerVector(N->getOperand(0));
  return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SDLoc(N),
                     NewOp.getValueType().getVectorElementType(),
                     NewOp, N->getOperand(1));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FABS(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  unsigned Size = NVT.getSizeInBits();

  // Mask = ~(1 << (Size-1))
  APInt API = APInt::getAllOnes(Size);
  API.clearBit(Size - 1);
  SDValue Mask = DAG.getConstant(API, SDLoc(N), NVT);
  SDValue Op = GetSoftenedFloat(N->getOperand(0));
  return DAG.getNode(ISD::AND, SDLoc(N), NVT, Op, Mask);
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FMINNUM(SDNode *N) {
  if (SDValue SelCC = TLI.createSelectForFMINNUM_FMAXNUM(N, DAG))
    return SoftenFloatRes_SELECT_CC(SelCC.getNode());
  return SoftenFloatRes_Binary(N, GetFPLibCall(N->getValueType(0),
                                               RTLIB::FMIN_F32,
                                               RTLIB::FMIN_F64,
                                               RTLIB::FMIN_F80,
                                               RTLIB::FMIN_F128,
                                               RTLIB::FMIN_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FMAXNUM(SDNode *N) {
  if (SDValue SelCC = TLI.createSelectForFMINNUM_FMAXNUM(N, DAG))
    return SoftenFloatRes_SELECT_CC(SelCC.getNode());
  return SoftenFloatRes_Binary(N, GetFPLibCall(N->getValueType(0),
                                               RTLIB::FMAX_F32,
                                               RTLIB::FMAX_F64,
                                               RTLIB::FMAX_F80,
                                               RTLIB::FMAX_F128,
                                               RTLIB::FMAX_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FADD(SDNode *N) {
  return SoftenFloatRes_Binary(N, GetFPLibCall(N->getValueType(0),
                                               RTLIB::ADD_F32,
                                               RTLIB::ADD_F64,
                                               RTLIB::ADD_F80,
                                               RTLIB::ADD_F128,
                                               RTLIB::ADD_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FACOS(SDNode *N) {
  return SoftenFloatRes_Unary(
      N, GetFPLibCall(N->getValueType(0), RTLIB::ACOS_F32, RTLIB::ACOS_F64,
                      RTLIB::ACOS_F80, RTLIB::ACOS_F128, RTLIB::ACOS_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FASIN(SDNode *N) {
  return SoftenFloatRes_Unary(
      N, GetFPLibCall(N->getValueType(0), RTLIB::ASIN_F32, RTLIB::ASIN_F64,
                      RTLIB::ASIN_F80, RTLIB::ASIN_F128, RTLIB::ASIN_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FATAN(SDNode *N) {
  return SoftenFloatRes_Unary(
      N, GetFPLibCall(N->getValueType(0), RTLIB::ATAN_F32, RTLIB::ATAN_F64,
                      RTLIB::ATAN_F80, RTLIB::ATAN_F128, RTLIB::ATAN_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FCBRT(SDNode *N) {
  return SoftenFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                           RTLIB::CBRT_F32,
                                           RTLIB::CBRT_F64,
                                           RTLIB::CBRT_F80,
                                           RTLIB::CBRT_F128,
                                           RTLIB::CBRT_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FCEIL(SDNode *N) {
  return SoftenFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                              RTLIB::CEIL_F32,
                                              RTLIB::CEIL_F64,
                                              RTLIB::CEIL_F80,
                                              RTLIB::CEIL_F128,
                                              RTLIB::CEIL_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FCOPYSIGN(SDNode *N) {
  SDValue LHS = GetSoftenedFloat(N->getOperand(0));
  SDValue RHS = BitConvertToInteger(N->getOperand(1));
  SDLoc dl(N);

  EVT LVT = LHS.getValueType();
  EVT RVT = RHS.getValueType();

  unsigned LSize = LVT.getSizeInBits();
  unsigned RSize = RVT.getSizeInBits();

  // First get the sign bit of second operand.
  SDValue SignBit = DAG.getNode(
      ISD::SHL, dl, RVT, DAG.getConstant(1, dl, RVT),
      DAG.getConstant(RSize - 1, dl,
                      TLI.getShiftAmountTy(RVT, DAG.getDataLayout())));
  SignBit = DAG.getNode(ISD::AND, dl, RVT, RHS, SignBit);

  // Shift right or sign-extend it if the two operands have different types.
  int SizeDiff = RVT.getSizeInBits() - LVT.getSizeInBits();
  if (SizeDiff > 0) {
    SignBit =
        DAG.getNode(ISD::SRL, dl, RVT, SignBit,
                    DAG.getConstant(SizeDiff, dl,
                                    TLI.getShiftAmountTy(SignBit.getValueType(),
                                                         DAG.getDataLayout())));
    SignBit = DAG.getNode(ISD::TRUNCATE, dl, LVT, SignBit);
  } else if (SizeDiff < 0) {
    SignBit = DAG.getNode(ISD::ANY_EXTEND, dl, LVT, SignBit);
    SignBit =
        DAG.getNode(ISD::SHL, dl, LVT, SignBit,
                    DAG.getConstant(-SizeDiff, dl,
                                    TLI.getShiftAmountTy(SignBit.getValueType(),
                                                         DAG.getDataLayout())));
  }

  // Clear the sign bit of the first operand.
  SDValue Mask = DAG.getNode(
      ISD::SHL, dl, LVT, DAG.getConstant(1, dl, LVT),
      DAG.getConstant(LSize - 1, dl,
                      TLI.getShiftAmountTy(LVT, DAG.getDataLayout())));
  Mask = DAG.getNode(ISD::SUB, dl, LVT, Mask, DAG.getConstant(1, dl, LVT));
  LHS = DAG.getNode(ISD::AND, dl, LVT, LHS, Mask);

  // Or the value with the sign bit.
  return DAG.getNode(ISD::OR, dl, LVT, LHS, SignBit);
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FCOS(SDNode *N) {
  return SoftenFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                              RTLIB::COS_F32,
                                              RTLIB::COS_F64,
                                              RTLIB::COS_F80,
                                              RTLIB::COS_F128,
                                              RTLIB::COS_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FCOSH(SDNode *N) {
  return SoftenFloatRes_Unary(
      N, GetFPLibCall(N->getValueType(0), RTLIB::COSH_F32, RTLIB::COSH_F64,
                      RTLIB::COSH_F80, RTLIB::COSH_F128, RTLIB::COSH_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FDIV(SDNode *N) {
  return SoftenFloatRes_Binary(N, GetFPLibCall(N->getValueType(0),
                                               RTLIB::DIV_F32,
                                               RTLIB::DIV_F64,
                                               RTLIB::DIV_F80,
                                               RTLIB::DIV_F128,
                                               RTLIB::DIV_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FEXP(SDNode *N) {
  return SoftenFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                              RTLIB::EXP_F32,
                                              RTLIB::EXP_F64,
                                              RTLIB::EXP_F80,
                                              RTLIB::EXP_F128,
                                              RTLIB::EXP_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FEXP2(SDNode *N) {
  return SoftenFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                              RTLIB::EXP2_F32,
                                              RTLIB::EXP2_F64,
                                              RTLIB::EXP2_F80,
                                              RTLIB::EXP2_F128,
                                              RTLIB::EXP2_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FEXP10(SDNode *N) {
  return SoftenFloatRes_Unary(
      N,
      GetFPLibCall(N->getValueType(0), RTLIB::EXP10_F32, RTLIB::EXP10_F64,
                   RTLIB::EXP10_F80, RTLIB::EXP10_F128, RTLIB::EXP10_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FFLOOR(SDNode *N) {
  return SoftenFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                              RTLIB::FLOOR_F32,
                                              RTLIB::FLOOR_F64,
                                              RTLIB::FLOOR_F80,
                                              RTLIB::FLOOR_F128,
                                              RTLIB::FLOOR_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FLOG(SDNode *N) {
  return SoftenFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                              RTLIB::LOG_F32,
                                              RTLIB::LOG_F64,
                                              RTLIB::LOG_F80,
                                              RTLIB::LOG_F128,
                                              RTLIB::LOG_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FLOG2(SDNode *N) {
  return SoftenFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                              RTLIB::LOG2_F32,
                                              RTLIB::LOG2_F64,
                                              RTLIB::LOG2_F80,
                                              RTLIB::LOG2_F128,
                                              RTLIB::LOG2_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FLOG10(SDNode *N) {
  return SoftenFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                              RTLIB::LOG10_F32,
                                              RTLIB::LOG10_F64,
                                              RTLIB::LOG10_F80,
                                              RTLIB::LOG10_F128,
                                              RTLIB::LOG10_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FMA(SDNode *N) {
  bool IsStrict = N->isStrictFPOpcode();
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  unsigned Offset = IsStrict ? 1 : 0;
  SDValue Ops[3] = { GetSoftenedFloat(N->getOperand(0 + Offset)),
                     GetSoftenedFloat(N->getOperand(1 + Offset)),
                     GetSoftenedFloat(N->getOperand(2 + Offset)) };
  SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();
  TargetLowering::MakeLibCallOptions CallOptions;
  EVT OpsVT[3] = { N->getOperand(0 + Offset).getValueType(),
                   N->getOperand(1 + Offset).getValueType(),
                   N->getOperand(2 + Offset).getValueType() };
  CallOptions.setTypeListBeforeSoften(OpsVT, N->getValueType(0), true);
  std::pair<SDValue, SDValue> Tmp = TLI.makeLibCall(DAG,
                                                    GetFPLibCall(N->getValueType(0),
                                                                 RTLIB::FMA_F32,
                                                                 RTLIB::FMA_F64,
                                                                 RTLIB::FMA_F80,
                                                                 RTLIB::FMA_F128,
                                                                 RTLIB::FMA_PPCF128),
                         NVT, Ops, CallOptions, SDLoc(N), Chain);
  if (IsStrict)
    ReplaceValueWith(SDValue(N, 1), Tmp.second);
  return Tmp.first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FMUL(SDNode *N) {
  return SoftenFloatRes_Binary(N, GetFPLibCall(N->getValueType(0),
                                               RTLIB::MUL_F32,
                                               RTLIB::MUL_F64,
                                               RTLIB::MUL_F80,
                                               RTLIB::MUL_F128,
                                               RTLIB::MUL_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FNEARBYINT(SDNode *N) {
  return SoftenFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                              RTLIB::NEARBYINT_F32,
                                              RTLIB::NEARBYINT_F64,
                                              RTLIB::NEARBYINT_F80,
                                              RTLIB::NEARBYINT_F128,
                                              RTLIB::NEARBYINT_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FNEG(SDNode *N) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDLoc dl(N);

  // Expand Y = FNEG(X) -> Y = X ^ sign mask
  APInt SignMask = APInt::getSignMask(NVT.getSizeInBits());
  return DAG.getNode(ISD::XOR, dl, NVT, GetSoftenedFloat(N->getOperand(0)),
                     DAG.getConstant(SignMask, dl, NVT));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FP_EXTEND(SDNode *N) {
  bool IsStrict = N->isStrictFPOpcode();
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Op = N->getOperand(IsStrict ? 1 : 0);

  SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();

  if (getTypeAction(Op.getValueType()) == TargetLowering::TypePromoteFloat) {
    Op = GetPromotedFloat(Op);
    // If the promotion did the FP_EXTEND to the destination type for us,
    // there's nothing left to do here.
    if (Op.getValueType() == N->getValueType(0)) {
      if (IsStrict)
        ReplaceValueWith(SDValue(N, 1), Chain);
      return BitConvertToInteger(Op);
    }
  }

  // There's only a libcall for f16 -> f32 and shifting is only valid for bf16
  // -> f32, so proceed in two stages. Also, it's entirely possible for both
  // f16 and f32 to be legal, so use the fully hard-float FP_EXTEND rather
  // than FP16_TO_FP.
  if ((Op.getValueType() == MVT::f16 || Op.getValueType() == MVT::bf16) &&
      N->getValueType(0) != MVT::f32) {
    if (IsStrict) {
      Op = DAG.getNode(ISD::STRICT_FP_EXTEND, SDLoc(N),
                       { MVT::f32, MVT::Other }, { Chain, Op });
      Chain = Op.getValue(1);
    } else {
      Op = DAG.getNode(ISD::FP_EXTEND, SDLoc(N), MVT::f32, Op);
    }
  }

  if (Op.getValueType() == MVT::bf16) {
    // FIXME: Need ReplaceValueWith on chain in strict case
    return SoftenFloatRes_BF16_TO_FP(N);
  }

  RTLIB::Libcall LC = RTLIB::getFPEXT(Op.getValueType(), N->getValueType(0));
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported FP_EXTEND!");
  TargetLowering::MakeLibCallOptions CallOptions;
  EVT OpVT = N->getOperand(IsStrict ? 1 : 0).getValueType();
  CallOptions.setTypeListBeforeSoften(OpVT, N->getValueType(0), true);
  std::pair<SDValue, SDValue> Tmp = TLI.makeLibCall(DAG, LC, NVT, Op,
                                                    CallOptions, SDLoc(N),
                                                    Chain);
  if (IsStrict)
    ReplaceValueWith(SDValue(N, 1), Tmp.second);
  return Tmp.first;
}

// FIXME: Should we just use 'normal' FP_EXTEND / FP_TRUNC instead of special
// nodes?
SDValue DAGTypeLegalizer::SoftenFloatRes_FP16_TO_FP(SDNode *N) {
  EVT MidVT = TLI.getTypeToTransformTo(*DAG.getContext(), MVT::f32);
  SDValue Op = N->getOperand(0);
  TargetLowering::MakeLibCallOptions CallOptions;
  EVT OpsVT[1] = { N->getOperand(0).getValueType() };
  CallOptions.setTypeListBeforeSoften(OpsVT, N->getValueType(0), true);
  SDValue Res32 = TLI.makeLibCall(DAG, RTLIB::FPEXT_F16_F32, MidVT, Op,
                                  CallOptions, SDLoc(N)).first;
  if (N->getValueType(0) == MVT::f32)
    return Res32;

  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  RTLIB::Libcall LC = RTLIB::getFPEXT(MVT::f32, N->getValueType(0));
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported FP_EXTEND!");
  return TLI.makeLibCall(DAG, LC, NVT, Res32, CallOptions, SDLoc(N)).first;
}

// FIXME: Should we just use 'normal' FP_EXTEND / FP_TRUNC instead of special
// nodes?
SDValue DAGTypeLegalizer::SoftenFloatRes_BF16_TO_FP(SDNode *N) {
  assert(N->getValueType(0) == MVT::f32 &&
         "Can only soften BF16_TO_FP with f32 result");
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), MVT::f32);
  SDValue Op = N->getOperand(0);
  SDLoc DL(N);
  Op = DAG.getNode(ISD::ANY_EXTEND, DL, NVT,
                   DAG.getNode(ISD::BITCAST, DL, MVT::i16, Op));
  SDValue Res = DAG.getNode(ISD::SHL, DL, NVT, Op,
                            DAG.getShiftAmountConstant(16, NVT, DL));
  return Res;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FP_ROUND(SDNode *N) {
  bool IsStrict = N->isStrictFPOpcode();
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Op = N->getOperand(IsStrict ? 1 : 0);
  SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();
  RTLIB::Libcall LC = RTLIB::getFPROUND(Op.getValueType(), N->getValueType(0));
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported FP_ROUND!");
  TargetLowering::MakeLibCallOptions CallOptions;
  EVT OpVT = N->getOperand(IsStrict ? 1 : 0).getValueType();
  CallOptions.setTypeListBeforeSoften(OpVT, N->getValueType(0), true);
  std::pair<SDValue, SDValue> Tmp = TLI.makeLibCall(DAG, LC, NVT, Op,
                                                    CallOptions, SDLoc(N),
                                                    Chain);
  if (IsStrict)
    ReplaceValueWith(SDValue(N, 1), Tmp.second);
  return Tmp.first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FPOW(SDNode *N) {
  return SoftenFloatRes_Binary(N, GetFPLibCall(N->getValueType(0),
                                               RTLIB::POW_F32,
                                               RTLIB::POW_F64,
                                               RTLIB::POW_F80,
                                               RTLIB::POW_F128,
                                               RTLIB::POW_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_ExpOp(SDNode *N) {
  bool IsStrict = N->isStrictFPOpcode();
  unsigned Offset = IsStrict ? 1 : 0;
  assert((N->getOperand(1 + Offset).getValueType() == MVT::i16 ||
          N->getOperand(1 + Offset).getValueType() == MVT::i32) &&
         "Unsupported power type!");
  bool IsPowI =
      N->getOpcode() == ISD::FPOWI || N->getOpcode() == ISD::STRICT_FPOWI;

  RTLIB::Libcall LC = IsPowI ? RTLIB::getPOWI(N->getValueType(0))
                             : RTLIB::getLDEXP(N->getValueType(0));
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unexpected fpowi.");
  if (!TLI.getLibcallName(LC)) {
    // Some targets don't have a powi libcall; use pow instead.
    // FIXME: Implement this if some target needs it.
    DAG.getContext()->emitError("Don't know how to soften fpowi to fpow");
    return DAG.getUNDEF(N->getValueType(0));
  }

  if (DAG.getLibInfo().getIntSize() !=
      N->getOperand(1 + Offset).getValueType().getSizeInBits()) {
    // If the exponent does not match with sizeof(int) a libcall to RTLIB::POWI
    // would use the wrong type for the argument.
    DAG.getContext()->emitError("POWI exponent does not match sizeof(int)");
    return DAG.getUNDEF(N->getValueType(0));
  }

  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Ops[2] = { GetSoftenedFloat(N->getOperand(0 + Offset)),
                     N->getOperand(1 + Offset) };
  SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();
  TargetLowering::MakeLibCallOptions CallOptions;
  EVT OpsVT[2] = { N->getOperand(0 + Offset).getValueType(),
                   N->getOperand(1 + Offset).getValueType() };
  CallOptions.setTypeListBeforeSoften(OpsVT, N->getValueType(0), true);
  std::pair<SDValue, SDValue> Tmp = TLI.makeLibCall(DAG, LC, NVT, Ops,
                                                    CallOptions, SDLoc(N),
                                                    Chain);
  if (IsStrict)
    ReplaceValueWith(SDValue(N, 1), Tmp.second);
  return Tmp.first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FFREXP(SDNode *N) {
  assert(!N->isStrictFPOpcode() && "strictfp not implemented for frexp");
  EVT VT0 = N->getValueType(0);
  EVT VT1 = N->getValueType(1);
  RTLIB::Libcall LC = RTLIB::getFREXP(VT0);

  if (DAG.getLibInfo().getIntSize() != VT1.getSizeInBits()) {
    // If the exponent does not match with sizeof(int) a libcall would use the
    // wrong type for the argument.
    // TODO: Should be able to handle mismatches.
    DAG.getContext()->emitError("ffrexp exponent does not match sizeof(int)");
    return DAG.getUNDEF(N->getValueType(0));
  }

  EVT NVT0 = TLI.getTypeToTransformTo(*DAG.getContext(), VT0);
  SDValue StackSlot = DAG.CreateStackTemporary(VT1);

  SDLoc DL(N);

  TargetLowering::MakeLibCallOptions CallOptions;
  SDValue Ops[2] = {GetSoftenedFloat(N->getOperand(0)), StackSlot};
  EVT OpsVT[2] = {VT0, StackSlot.getValueType()};

  // TODO: setTypeListBeforeSoften can't properly express multiple return types,
  // but we only really need to handle the 0th one for softening anyway.
  CallOptions.setTypeListBeforeSoften({OpsVT}, VT0, true);

  auto [ReturnVal, Chain] = TLI.makeLibCall(DAG, LC, NVT0, Ops, CallOptions, DL,
                                            /*Chain=*/SDValue());
  int FrameIdx = cast<FrameIndexSDNode>(StackSlot)->getIndex();
  auto PtrInfo =
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FrameIdx);

  SDValue LoadExp = DAG.getLoad(VT1, DL, Chain, StackSlot, PtrInfo);

  ReplaceValueWith(SDValue(N, 1), LoadExp);
  return ReturnVal;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FREM(SDNode *N) {
  return SoftenFloatRes_Binary(N, GetFPLibCall(N->getValueType(0),
                                               RTLIB::REM_F32,
                                               RTLIB::REM_F64,
                                               RTLIB::REM_F80,
                                               RTLIB::REM_F128,
                                               RTLIB::REM_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FRINT(SDNode *N) {
  return SoftenFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                              RTLIB::RINT_F32,
                                              RTLIB::RINT_F64,
                                              RTLIB::RINT_F80,
                                              RTLIB::RINT_F128,
                                              RTLIB::RINT_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FROUND(SDNode *N) {
  return SoftenFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                              RTLIB::ROUND_F32,
                                              RTLIB::ROUND_F64,
                                              RTLIB::ROUND_F80,
                                              RTLIB::ROUND_F128,
                                              RTLIB::ROUND_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FROUNDEVEN(SDNode *N) {
  return SoftenFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                              RTLIB::ROUNDEVEN_F32,
                                              RTLIB::ROUNDEVEN_F64,
                                              RTLIB::ROUNDEVEN_F80,
                                              RTLIB::ROUNDEVEN_F128,
                                              RTLIB::ROUNDEVEN_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FSIN(SDNode *N) {
  return SoftenFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                              RTLIB::SIN_F32,
                                              RTLIB::SIN_F64,
                                              RTLIB::SIN_F80,
                                              RTLIB::SIN_F128,
                                              RTLIB::SIN_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FSINH(SDNode *N) {
  return SoftenFloatRes_Unary(
      N, GetFPLibCall(N->getValueType(0), RTLIB::SINH_F32, RTLIB::SINH_F64,
                      RTLIB::SINH_F80, RTLIB::SINH_F128, RTLIB::SINH_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FSQRT(SDNode *N) {
  return SoftenFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                              RTLIB::SQRT_F32,
                                              RTLIB::SQRT_F64,
                                              RTLIB::SQRT_F80,
                                              RTLIB::SQRT_F128,
                                              RTLIB::SQRT_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FSUB(SDNode *N) {
  return SoftenFloatRes_Binary(N, GetFPLibCall(N->getValueType(0),
                                               RTLIB::SUB_F32,
                                               RTLIB::SUB_F64,
                                               RTLIB::SUB_F80,
                                               RTLIB::SUB_F128,
                                               RTLIB::SUB_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FTAN(SDNode *N) {
  return SoftenFloatRes_Unary(
      N, GetFPLibCall(N->getValueType(0), RTLIB::TAN_F32, RTLIB::TAN_F64,
                      RTLIB::TAN_F80, RTLIB::TAN_F128, RTLIB::TAN_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FTANH(SDNode *N) {
  return SoftenFloatRes_Unary(
      N, GetFPLibCall(N->getValueType(0), RTLIB::TANH_F32, RTLIB::TANH_F64,
                      RTLIB::TANH_F80, RTLIB::TANH_F128, RTLIB::TANH_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_FTRUNC(SDNode *N) {
  return SoftenFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                              RTLIB::TRUNC_F32,
                                              RTLIB::TRUNC_F64,
                                              RTLIB::TRUNC_F80,
                                              RTLIB::TRUNC_F128,
                                              RTLIB::TRUNC_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_LOAD(SDNode *N) {
  LoadSDNode *L = cast<LoadSDNode>(N);
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDLoc dl(N);

  auto MMOFlags =
      L->getMemOperand()->getFlags() &
      ~(MachineMemOperand::MOInvariant | MachineMemOperand::MODereferenceable);
  SDValue NewL;
  if (L->getExtensionType() == ISD::NON_EXTLOAD) {
    NewL = DAG.getLoad(L->getAddressingMode(), L->getExtensionType(), NVT, dl,
                       L->getChain(), L->getBasePtr(), L->getOffset(),
                       L->getPointerInfo(), NVT, L->getOriginalAlign(),
                       MMOFlags, L->getAAInfo());
    // Legalized the chain result - switch anything that used the old chain to
    // use the new one.
    ReplaceValueWith(SDValue(N, 1), NewL.getValue(1));
    return NewL;
  }

  // Do a non-extending load followed by FP_EXTEND.
  NewL = DAG.getLoad(L->getAddressingMode(), ISD::NON_EXTLOAD, L->getMemoryVT(),
                     dl, L->getChain(), L->getBasePtr(), L->getOffset(),
                     L->getPointerInfo(), L->getMemoryVT(),
                     L->getOriginalAlign(), MMOFlags, L->getAAInfo());
  // Legalized the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), NewL.getValue(1));
  auto ExtendNode = DAG.getNode(ISD::FP_EXTEND, dl, VT, NewL);
  return BitConvertToInteger(ExtendNode);
}

SDValue DAGTypeLegalizer::SoftenFloatRes_ATOMIC_LOAD(SDNode *N) {
  AtomicSDNode *L = cast<AtomicSDNode>(N);
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDLoc dl(N);

  if (L->getExtensionType() == ISD::NON_EXTLOAD) {
    SDValue NewL =
        DAG.getAtomic(ISD::ATOMIC_LOAD, dl, NVT, DAG.getVTList(NVT, MVT::Other),
                      {L->getChain(), L->getBasePtr()}, L->getMemOperand());

    // Legalized the chain result - switch anything that used the old chain to
    // use the new one.
    ReplaceValueWith(SDValue(N, 1), NewL.getValue(1));
    return NewL;
  }

  report_fatal_error("softening fp extending atomic load not handled");
}

SDValue DAGTypeLegalizer::SoftenFloatRes_SELECT(SDNode *N) {
  SDValue LHS = GetSoftenedFloat(N->getOperand(1));
  SDValue RHS = GetSoftenedFloat(N->getOperand(2));
  return DAG.getSelect(SDLoc(N),
                       LHS.getValueType(), N->getOperand(0), LHS, RHS);
}

SDValue DAGTypeLegalizer::SoftenFloatRes_SELECT_CC(SDNode *N) {
  SDValue LHS = GetSoftenedFloat(N->getOperand(2));
  SDValue RHS = GetSoftenedFloat(N->getOperand(3));
  return DAG.getNode(ISD::SELECT_CC, SDLoc(N),
                     LHS.getValueType(), N->getOperand(0),
                     N->getOperand(1), LHS, RHS, N->getOperand(4));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_UNDEF(SDNode *N) {
  return DAG.getUNDEF(TLI.getTypeToTransformTo(*DAG.getContext(),
                                               N->getValueType(0)));
}

SDValue DAGTypeLegalizer::SoftenFloatRes_VAARG(SDNode *N) {
  SDValue Chain = N->getOperand(0); // Get the chain.
  SDValue Ptr = N->getOperand(1); // Get the pointer.
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDLoc dl(N);

  SDValue NewVAARG;
  NewVAARG = DAG.getVAArg(NVT, dl, Chain, Ptr, N->getOperand(2),
                          N->getConstantOperandVal(3));

  // Legalized the chain result - switch anything that used the old chain to
  // use the new one.
  if (N != NewVAARG.getValue(1).getNode())
    ReplaceValueWith(SDValue(N, 1), NewVAARG.getValue(1));
  return NewVAARG;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_XINT_TO_FP(SDNode *N) {
  bool IsStrict = N->isStrictFPOpcode();
  bool Signed = N->getOpcode() == ISD::SINT_TO_FP ||
                N->getOpcode() == ISD::STRICT_SINT_TO_FP;
  EVT SVT = N->getOperand(IsStrict ? 1 : 0).getValueType();
  EVT RVT = N->getValueType(0);
  EVT NVT = EVT();
  SDLoc dl(N);

  // If the input is not legal, eg: i1 -> fp, then it needs to be promoted to
  // a larger type, eg: i8 -> fp.  Even if it is legal, no libcall may exactly
  // match.  Look for an appropriate libcall.
  RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
  for (unsigned t = MVT::FIRST_INTEGER_VALUETYPE;
       t <= MVT::LAST_INTEGER_VALUETYPE && LC == RTLIB::UNKNOWN_LIBCALL; ++t) {
    NVT = (MVT::SimpleValueType)t;
    // The source needs to big enough to hold the operand.
    if (NVT.bitsGE(SVT))
      LC = Signed ? RTLIB::getSINTTOFP(NVT, RVT):RTLIB::getUINTTOFP (NVT, RVT);
  }
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported XINT_TO_FP!");

  SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();
  // Sign/zero extend the argument if the libcall takes a larger type.
  SDValue Op = DAG.getNode(Signed ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND, dl,
                           NVT, N->getOperand(IsStrict ? 1 : 0));
  TargetLowering::MakeLibCallOptions CallOptions;
  CallOptions.setSExt(Signed);
  CallOptions.setTypeListBeforeSoften(SVT, RVT, true);
  std::pair<SDValue, SDValue> Tmp =
      TLI.makeLibCall(DAG, LC, TLI.getTypeToTransformTo(*DAG.getContext(), RVT),
                      Op, CallOptions, dl, Chain);

  if (IsStrict)
    ReplaceValueWith(SDValue(N, 1), Tmp.second);
  return Tmp.first;
}

SDValue DAGTypeLegalizer::SoftenFloatRes_VECREDUCE(SDNode *N) {
  // Expand and soften recursively.
  ReplaceValueWith(SDValue(N, 0), TLI.expandVecReduce(N, DAG));
  return SDValue();
}

SDValue DAGTypeLegalizer::SoftenFloatRes_VECREDUCE_SEQ(SDNode *N) {
  ReplaceValueWith(SDValue(N, 0), TLI.expandVecReduceSeq(N, DAG));
  return SDValue();
}

//===----------------------------------------------------------------------===//
//  Convert Float Operand to Integer
//===----------------------------------------------------------------------===//

bool DAGTypeLegalizer::SoftenFloatOperand(SDNode *N, unsigned OpNo) {
  LLVM_DEBUG(dbgs() << "Soften float operand " << OpNo << ": "; N->dump(&DAG));
  SDValue Res = SDValue();

  switch (N->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "SoftenFloatOperand Op #" << OpNo << ": ";
    N->dump(&DAG); dbgs() << "\n";
#endif
    report_fatal_error("Do not know how to soften this operator's operand!");

  case ISD::BITCAST:     Res = SoftenFloatOp_BITCAST(N); break;
  case ISD::BR_CC:       Res = SoftenFloatOp_BR_CC(N); break;
  case ISD::STRICT_FP_TO_FP16:
  case ISD::FP_TO_FP16:  // Same as FP_ROUND for softening purposes
  case ISD::FP_TO_BF16:
  case ISD::STRICT_FP_TO_BF16:
  case ISD::STRICT_FP_ROUND:
  case ISD::FP_ROUND:    Res = SoftenFloatOp_FP_ROUND(N); break;
  case ISD::STRICT_FP_TO_SINT:
  case ISD::STRICT_FP_TO_UINT:
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:  Res = SoftenFloatOp_FP_TO_XINT(N); break;
  case ISD::FP_TO_SINT_SAT:
  case ISD::FP_TO_UINT_SAT:
                         Res = SoftenFloatOp_FP_TO_XINT_SAT(N); break;
  case ISD::STRICT_LROUND:
  case ISD::LROUND:      Res = SoftenFloatOp_LROUND(N); break;
  case ISD::STRICT_LLROUND:
  case ISD::LLROUND:     Res = SoftenFloatOp_LLROUND(N); break;
  case ISD::STRICT_LRINT:
  case ISD::LRINT:       Res = SoftenFloatOp_LRINT(N); break;
  case ISD::STRICT_LLRINT:
  case ISD::LLRINT:      Res = SoftenFloatOp_LLRINT(N); break;
  case ISD::SELECT_CC:   Res = SoftenFloatOp_SELECT_CC(N); break;
  case ISD::STRICT_FSETCC:
  case ISD::STRICT_FSETCCS:
  case ISD::SETCC:       Res = SoftenFloatOp_SETCC(N); break;
  case ISD::STORE:       Res = SoftenFloatOp_STORE(N, OpNo); break;
  case ISD::ATOMIC_STORE:
    Res = SoftenFloatOp_ATOMIC_STORE(N, OpNo);
    break;
  case ISD::FCOPYSIGN:   Res = SoftenFloatOp_FCOPYSIGN(N); break;
  }

  // If the result is null, the sub-method took care of registering results etc.
  if (!Res.getNode()) return false;

  // If the result is N, the sub-method updated N in place.  Tell the legalizer
  // core about this to re-analyze.
  if (Res.getNode() == N)
    return true;

  assert(Res.getValueType() == N->getValueType(0) && N->getNumValues() == 1 &&
         "Invalid operand softening");

  ReplaceValueWith(SDValue(N, 0), Res);
  return false;
}

SDValue DAGTypeLegalizer::SoftenFloatOp_BITCAST(SDNode *N) {
  SDValue Op0 = GetSoftenedFloat(N->getOperand(0));

  return DAG.getNode(ISD::BITCAST, SDLoc(N), N->getValueType(0), Op0);
}

SDValue DAGTypeLegalizer::SoftenFloatOp_FP_ROUND(SDNode *N) {
  // We actually deal with the partially-softened FP_TO_FP16 node too, which
  // returns an i16 so doesn't meet the constraints necessary for FP_ROUND.
  assert(N->getOpcode() == ISD::FP_ROUND || N->getOpcode() == ISD::FP_TO_FP16 ||
         N->getOpcode() == ISD::STRICT_FP_TO_FP16 ||
         N->getOpcode() == ISD::FP_TO_BF16 ||
         N->getOpcode() == ISD::STRICT_FP_TO_BF16 ||
         N->getOpcode() == ISD::STRICT_FP_ROUND);

  bool IsStrict = N->isStrictFPOpcode();
  SDValue Op = N->getOperand(IsStrict ? 1 : 0);
  EVT SVT = Op.getValueType();
  EVT RVT = N->getValueType(0);
  EVT FloatRVT = RVT;
  if (N->getOpcode() == ISD::FP_TO_FP16 ||
      N->getOpcode() == ISD::STRICT_FP_TO_FP16)
    FloatRVT = MVT::f16;
  else if (N->getOpcode() == ISD::FP_TO_BF16 ||
           N->getOpcode() == ISD::STRICT_FP_TO_BF16)
    FloatRVT = MVT::bf16;

  RTLIB::Libcall LC = RTLIB::getFPROUND(SVT, FloatRVT);
  assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported FP_ROUND libcall");

  SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();
  Op = GetSoftenedFloat(Op);
  TargetLowering::MakeLibCallOptions CallOptions;
  CallOptions.setTypeListBeforeSoften(SVT, RVT, true);
  std::pair<SDValue, SDValue> Tmp = TLI.makeLibCall(DAG, LC, RVT, Op,
                                                    CallOptions, SDLoc(N),
                                                    Chain);
  if (IsStrict) {
    ReplaceValueWith(SDValue(N, 1), Tmp.second);
    ReplaceValueWith(SDValue(N, 0), Tmp.first);
    return SDValue();
  }
  return Tmp.first;
}

SDValue DAGTypeLegalizer::SoftenFloatOp_BR_CC(SDNode *N) {
  SDValue NewLHS = N->getOperand(2), NewRHS = N->getOperand(3);
  ISD::CondCode CCCode = cast<CondCodeSDNode>(N->getOperand(1))->get();

  EVT VT = NewLHS.getValueType();
  NewLHS = GetSoftenedFloat(NewLHS);
  NewRHS = GetSoftenedFloat(NewRHS);
  TLI.softenSetCCOperands(DAG, VT, NewLHS, NewRHS, CCCode, SDLoc(N),
                          N->getOperand(2), N->getOperand(3));

  // If softenSetCCOperands returned a scalar, we need to compare the result
  // against zero to select between true and false values.
  if (!NewRHS.getNode()) {
    NewRHS = DAG.getConstant(0, SDLoc(N), NewLHS.getValueType());
    CCCode = ISD::SETNE;
  }

  // Update N to have the operands specified.
  return SDValue(DAG.UpdateNodeOperands(N, N->getOperand(0),
                                DAG.getCondCode(CCCode), NewLHS, NewRHS,
                                N->getOperand(4)),
                 0);
}

// Even if the result type is legal, no libcall may exactly match. (e.g. We
// don't have FP-i8 conversions) This helper method looks for an appropriate
// promoted libcall.
static RTLIB::Libcall findFPToIntLibcall(EVT SrcVT, EVT RetVT, EVT &Promoted,
                                         bool Signed) {
  RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
  for (unsigned IntVT = MVT::FIRST_INTEGER_VALUETYPE;
       IntVT <= MVT::LAST_INTEGER_VALUETYPE && LC == RTLIB::UNKNOWN_LIBCALL;
       ++IntVT) {
    Promoted = (MVT::SimpleValueType)IntVT;
    // The type needs to big enough to hold the result.
    if (Promoted.bitsGE(RetVT))
      LC = Signed ? RTLIB::getFPTOSINT(SrcVT, Promoted)
                  : RTLIB::getFPTOUINT(SrcVT, Promoted);
  }
  return LC;
}

SDValue DAGTypeLegalizer::SoftenFloatOp_FP_TO_XINT(SDNode *N) {
  bool IsStrict = N->isStrictFPOpcode();
  bool Signed = N->getOpcode() == ISD::FP_TO_SINT ||
                N->getOpcode() == ISD::STRICT_FP_TO_SINT;

  SDValue Op = N->getOperand(IsStrict ? 1 : 0);
  EVT SVT = Op.getValueType();
  EVT RVT = N->getValueType(0);
  EVT NVT = EVT();
  SDLoc dl(N);

  // If the result is not legal, eg: fp -> i1, then it needs to be promoted to
  // a larger type, eg: fp -> i32. Even if it is legal, no libcall may exactly
  // match, eg. we don't have fp -> i8 conversions.
  // Look for an appropriate libcall.
  RTLIB::Libcall LC = findFPToIntLibcall(SVT, RVT, NVT, Signed);
  assert(LC != RTLIB::UNKNOWN_LIBCALL && NVT.isSimple() &&
         "Unsupported FP_TO_XINT!");

  Op = GetSoftenedFloat(Op);
  SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();
  TargetLowering::MakeLibCallOptions CallOptions;
  CallOptions.setTypeListBeforeSoften(SVT, RVT, true);
  std::pair<SDValue, SDValue> Tmp = TLI.makeLibCall(DAG, LC, NVT, Op,
                                                    CallOptions, dl, Chain);

  // Truncate the result if the libcall returns a larger type.
  SDValue Res = DAG.getNode(ISD::TRUNCATE, dl, RVT, Tmp.first);

  if (!IsStrict)
    return Res;

  ReplaceValueWith(SDValue(N, 1), Tmp.second);
  ReplaceValueWith(SDValue(N, 0), Res);
  return SDValue();
}

SDValue DAGTypeLegalizer::SoftenFloatOp_FP_TO_XINT_SAT(SDNode *N) {
  SDValue Res = TLI.expandFP_TO_INT_SAT(N, DAG);
  return Res;
}

SDValue DAGTypeLegalizer::SoftenFloatOp_SELECT_CC(SDNode *N) {
  SDValue NewLHS = N->getOperand(0), NewRHS = N->getOperand(1);
  ISD::CondCode CCCode = cast<CondCodeSDNode>(N->getOperand(4))->get();

  EVT VT = NewLHS.getValueType();
  NewLHS = GetSoftenedFloat(NewLHS);
  NewRHS = GetSoftenedFloat(NewRHS);
  TLI.softenSetCCOperands(DAG, VT, NewLHS, NewRHS, CCCode, SDLoc(N),
                          N->getOperand(0), N->getOperand(1));

  // If softenSetCCOperands returned a scalar, we need to compare the result
  // against zero to select between true and false values.
  if (!NewRHS.getNode()) {
    NewRHS = DAG.getConstant(0, SDLoc(N), NewLHS.getValueType());
    CCCode = ISD::SETNE;
  }

  // Update N to have the operands specified.
  return SDValue(DAG.UpdateNodeOperands(N, NewLHS, NewRHS,
                                N->getOperand(2), N->getOperand(3),
                                DAG.getCondCode(CCCode)),
                 0);
}

SDValue DAGTypeLegalizer::SoftenFloatOp_SETCC(SDNode *N) {
  bool IsStrict = N->isStrictFPOpcode();
  SDValue Op0 = N->getOperand(IsStrict ? 1 : 0);
  SDValue Op1 = N->getOperand(IsStrict ? 2 : 1);
  SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();
  ISD::CondCode CCCode =
      cast<CondCodeSDNode>(N->getOperand(IsStrict ? 3 : 2))->get();

  EVT VT = Op0.getValueType();
  SDValue NewLHS = GetSoftenedFloat(Op0);
  SDValue NewRHS = GetSoftenedFloat(Op1);
  TLI.softenSetCCOperands(DAG, VT, NewLHS, NewRHS, CCCode, SDLoc(N), Op0, Op1,
                          Chain, N->getOpcode() == ISD::STRICT_FSETCCS);

  // Update N to have the operands specified.
  if (NewRHS.getNode()) {
    if (IsStrict)
      NewLHS = DAG.getNode(ISD::SETCC, SDLoc(N), N->getValueType(0), NewLHS,
                           NewRHS, DAG.getCondCode(CCCode));
    else
      return SDValue(DAG.UpdateNodeOperands(N, NewLHS, NewRHS,
                                            DAG.getCondCode(CCCode)), 0);
  }

  // Otherwise, softenSetCCOperands returned a scalar, use it.
  assert((NewRHS.getNode() || NewLHS.getValueType() == N->getValueType(0)) &&
         "Unexpected setcc expansion!");

  if (IsStrict) {
    ReplaceValueWith(SDValue(N, 0), NewLHS);
    ReplaceValueWith(SDValue(N, 1), Chain);
    return SDValue();
  }
  return NewLHS;
}

SDValue DAGTypeLegalizer::SoftenFloatOp_STORE(SDNode *N, unsigned OpNo) {
  assert(ISD::isUNINDEXEDStore(N) && "Indexed store during type legalization!");
  assert(OpNo == 1 && "Can only soften the stored value!");
  StoreSDNode *ST = cast<StoreSDNode>(N);
  SDValue Val = ST->getValue();
  SDLoc dl(N);

  if (ST->isTruncatingStore())
    // Do an FP_ROUND followed by a non-truncating store.
    Val = BitConvertToInteger(
        DAG.getNode(ISD::FP_ROUND, dl, ST->getMemoryVT(), Val,
                    DAG.getIntPtrConstant(0, dl, /*isTarget=*/true)));
  else
    Val = GetSoftenedFloat(Val);

  return DAG.getStore(ST->getChain(), dl, Val, ST->getBasePtr(),
                      ST->getMemOperand());
}

SDValue DAGTypeLegalizer::SoftenFloatOp_ATOMIC_STORE(SDNode *N, unsigned OpNo) {
  assert(OpNo == 1 && "Can only soften the stored value!");
  AtomicSDNode *ST = cast<AtomicSDNode>(N);
  SDValue Val = ST->getVal();
  EVT VT = Val.getValueType();
  SDLoc dl(N);

  assert(ST->getMemoryVT() == VT && "truncating atomic store not handled");

  SDValue NewVal = GetSoftenedFloat(Val);
  return DAG.getAtomic(ISD::ATOMIC_STORE, dl, VT, ST->getChain(), NewVal,
                       ST->getBasePtr(), ST->getMemOperand());
}

SDValue DAGTypeLegalizer::SoftenFloatOp_FCOPYSIGN(SDNode *N) {
  SDValue LHS = N->getOperand(0);
  SDValue RHS = BitConvertToInteger(N->getOperand(1));
  SDLoc dl(N);

  EVT LVT = LHS.getValueType();
  EVT ILVT = EVT::getIntegerVT(*DAG.getContext(), LVT.getSizeInBits());
  EVT RVT = RHS.getValueType();

  unsigned LSize = LVT.getSizeInBits();
  unsigned RSize = RVT.getSizeInBits();

  // Shift right or sign-extend it if the two operands have different types.
  int SizeDiff = RSize - LSize;
  if (SizeDiff > 0) {
    RHS =
        DAG.getNode(ISD::SRL, dl, RVT, RHS,
                    DAG.getConstant(SizeDiff, dl,
                                    TLI.getShiftAmountTy(RHS.getValueType(),
                                                         DAG.getDataLayout())));
    RHS = DAG.getNode(ISD::TRUNCATE, dl, ILVT, RHS);
  } else if (SizeDiff < 0) {
    RHS = DAG.getNode(ISD::ANY_EXTEND, dl, LVT, RHS);
    RHS =
        DAG.getNode(ISD::SHL, dl, ILVT, RHS,
                    DAG.getConstant(-SizeDiff, dl,
                                    TLI.getShiftAmountTy(RHS.getValueType(),
                                                         DAG.getDataLayout())));
  }

  RHS = DAG.getBitcast(LVT, RHS);
  return DAG.getNode(ISD::FCOPYSIGN, dl, LVT, LHS, RHS);
}

SDValue DAGTypeLegalizer::SoftenFloatOp_Unary(SDNode *N, RTLIB::Libcall LC) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  bool IsStrict = N->isStrictFPOpcode();
  unsigned Offset = IsStrict ? 1 : 0;
  SDValue Op = GetSoftenedFloat(N->getOperand(0 + Offset));
  SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();
  TargetLowering::MakeLibCallOptions CallOptions;
  EVT OpVT = N->getOperand(0 + Offset).getValueType();
  CallOptions.setTypeListBeforeSoften(OpVT, N->getValueType(0), true);
  std::pair<SDValue, SDValue> Tmp = TLI.makeLibCall(DAG, LC, NVT, Op,
                                                    CallOptions, SDLoc(N),
                                                    Chain);
  if (IsStrict) {
    ReplaceValueWith(SDValue(N, 1), Tmp.second);
    ReplaceValueWith(SDValue(N, 0), Tmp.first);
    return SDValue();
  }

  return Tmp.first;
}

SDValue DAGTypeLegalizer::SoftenFloatOp_LROUND(SDNode *N) {
  EVT OpVT = N->getOperand(N->isStrictFPOpcode() ? 1 : 0).getValueType();
  return SoftenFloatOp_Unary(N, GetFPLibCall(OpVT,
                                             RTLIB::LROUND_F32,
                                             RTLIB::LROUND_F64,
                                             RTLIB::LROUND_F80,
                                             RTLIB::LROUND_F128,
                                             RTLIB::LROUND_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatOp_LLROUND(SDNode *N) {
  EVT OpVT = N->getOperand(N->isStrictFPOpcode() ? 1 : 0).getValueType();
  return SoftenFloatOp_Unary(N, GetFPLibCall(OpVT,
                                             RTLIB::LLROUND_F32,
                                             RTLIB::LLROUND_F64,
                                             RTLIB::LLROUND_F80,
                                             RTLIB::LLROUND_F128,
                                             RTLIB::LLROUND_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatOp_LRINT(SDNode *N) {
  EVT OpVT = N->getOperand(N->isStrictFPOpcode() ? 1 : 0).getValueType();
  return SoftenFloatOp_Unary(N, GetFPLibCall(OpVT,
                                             RTLIB::LRINT_F32,
                                             RTLIB::LRINT_F64,
                                             RTLIB::LRINT_F80,
                                             RTLIB::LRINT_F128,
                                             RTLIB::LRINT_PPCF128));
}

SDValue DAGTypeLegalizer::SoftenFloatOp_LLRINT(SDNode *N) {
  EVT OpVT = N->getOperand(N->isStrictFPOpcode() ? 1 : 0).getValueType();
  return SoftenFloatOp_Unary(N, GetFPLibCall(OpVT,
                                             RTLIB::LLRINT_F32,
                                             RTLIB::LLRINT_F64,
                                             RTLIB::LLRINT_F80,
                                             RTLIB::LLRINT_F128,
                                             RTLIB::LLRINT_PPCF128));
}

//===----------------------------------------------------------------------===//
//  Float Result Expansion
//===----------------------------------------------------------------------===//

/// ExpandFloatResult - This method is called when the specified result of the
/// specified node is found to need expansion.  At this point, the node may also
/// have invalid operands or may have other results that need promotion, we just
/// know that (at least) one result needs expansion.
void DAGTypeLegalizer::ExpandFloatResult(SDNode *N, unsigned ResNo) {
  LLVM_DEBUG(dbgs() << "Expand float result: "; N->dump(&DAG));
  SDValue Lo, Hi;
  Lo = Hi = SDValue();

  // See if the target wants to custom expand this node.
  if (CustomLowerNode(N, N->getValueType(ResNo), true))
    return;

  switch (N->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "ExpandFloatResult #" << ResNo << ": ";
    N->dump(&DAG); dbgs() << "\n";
#endif
    report_fatal_error("Do not know how to expand the result of this "
                       "operator!");
    // clang-format off
  case ISD::UNDEF:        SplitRes_UNDEF(N, Lo, Hi); break;
  case ISD::SELECT:       SplitRes_Select(N, Lo, Hi); break;
  case ISD::SELECT_CC:    SplitRes_SELECT_CC(N, Lo, Hi); break;

  case ISD::MERGE_VALUES:       ExpandRes_MERGE_VALUES(N, ResNo, Lo, Hi); break;
  case ISD::BITCAST:            ExpandRes_BITCAST(N, Lo, Hi); break;
  case ISD::BUILD_PAIR:         ExpandRes_BUILD_PAIR(N, Lo, Hi); break;
  case ISD::EXTRACT_ELEMENT:    ExpandRes_EXTRACT_ELEMENT(N, Lo, Hi); break;
  case ISD::EXTRACT_VECTOR_ELT: ExpandRes_EXTRACT_VECTOR_ELT(N, Lo, Hi); break;
  case ISD::VAARG:              ExpandRes_VAARG(N, Lo, Hi); break;

  case ISD::ConstantFP: ExpandFloatRes_ConstantFP(N, Lo, Hi); break;
  case ISD::FABS:       ExpandFloatRes_FABS(N, Lo, Hi); break;
  case ISD::STRICT_FMINNUM:
  case ISD::FMINNUM:    ExpandFloatRes_FMINNUM(N, Lo, Hi); break;
  case ISD::STRICT_FMAXNUM:
  case ISD::FMAXNUM:    ExpandFloatRes_FMAXNUM(N, Lo, Hi); break;
  case ISD::STRICT_FADD:
  case ISD::FADD:       ExpandFloatRes_FADD(N, Lo, Hi); break;
  case ISD::STRICT_FACOS:
  case ISD::FACOS:      ExpandFloatRes_FACOS(N, Lo, Hi); break;
  case ISD::STRICT_FASIN:
  case ISD::FASIN:      ExpandFloatRes_FASIN(N, Lo, Hi); break;
  case ISD::STRICT_FATAN:
  case ISD::FATAN:      ExpandFloatRes_FATAN(N, Lo, Hi); break;
  case ISD::FCBRT:      ExpandFloatRes_FCBRT(N, Lo, Hi); break;
  case ISD::STRICT_FCEIL:
  case ISD::FCEIL:      ExpandFloatRes_FCEIL(N, Lo, Hi); break;
  case ISD::FCOPYSIGN:  ExpandFloatRes_FCOPYSIGN(N, Lo, Hi); break;
  case ISD::STRICT_FCOS:
  case ISD::FCOS:       ExpandFloatRes_FCOS(N, Lo, Hi); break;
  case ISD::STRICT_FCOSH:
  case ISD::FCOSH:       ExpandFloatRes_FCOSH(N, Lo, Hi); break;
  case ISD::STRICT_FDIV:
  case ISD::FDIV:       ExpandFloatRes_FDIV(N, Lo, Hi); break;
  case ISD::STRICT_FEXP:
  case ISD::FEXP:       ExpandFloatRes_FEXP(N, Lo, Hi); break;
  case ISD::STRICT_FEXP2:
  case ISD::FEXP2:      ExpandFloatRes_FEXP2(N, Lo, Hi); break;
  case ISD::FEXP10:     ExpandFloatRes_FEXP10(N, Lo, Hi); break;
  case ISD::STRICT_FFLOOR:
  case ISD::FFLOOR:     ExpandFloatRes_FFLOOR(N, Lo, Hi); break;
  case ISD::STRICT_FLOG:
  case ISD::FLOG:       ExpandFloatRes_FLOG(N, Lo, Hi); break;
  case ISD::STRICT_FLOG2:
  case ISD::FLOG2:      ExpandFloatRes_FLOG2(N, Lo, Hi); break;
  case ISD::STRICT_FLOG10:
  case ISD::FLOG10:     ExpandFloatRes_FLOG10(N, Lo, Hi); break;
  case ISD::STRICT_FMA:
  case ISD::FMA:        ExpandFloatRes_FMA(N, Lo, Hi); break;
  case ISD::STRICT_FMUL:
  case ISD::FMUL:       ExpandFloatRes_FMUL(N, Lo, Hi); break;
  case ISD::STRICT_FNEARBYINT:
  case ISD::FNEARBYINT: ExpandFloatRes_FNEARBYINT(N, Lo, Hi); break;
  case ISD::FNEG:       ExpandFloatRes_FNEG(N, Lo, Hi); break;
  case ISD::STRICT_FP_EXTEND:
  case ISD::FP_EXTEND:  ExpandFloatRes_FP_EXTEND(N, Lo, Hi); break;
  case ISD::STRICT_FPOW:
  case ISD::FPOW:       ExpandFloatRes_FPOW(N, Lo, Hi); break;
  case ISD::STRICT_FPOWI:
  case ISD::FPOWI:      ExpandFloatRes_FPOWI(N, Lo, Hi); break;
  case ISD::FLDEXP:
  case ISD::STRICT_FLDEXP: ExpandFloatRes_FLDEXP(N, Lo, Hi); break;
  case ISD::FREEZE:     ExpandFloatRes_FREEZE(N, Lo, Hi); break;
  case ISD::STRICT_FRINT:
  case ISD::FRINT:      ExpandFloatRes_FRINT(N, Lo, Hi); break;
  case ISD::STRICT_FROUND:
  case ISD::FROUND:     ExpandFloatRes_FROUND(N, Lo, Hi); break;
  case ISD::STRICT_FROUNDEVEN:
  case ISD::FROUNDEVEN: ExpandFloatRes_FROUNDEVEN(N, Lo, Hi); break;
  case ISD::STRICT_FSIN:
  case ISD::FSIN:       ExpandFloatRes_FSIN(N, Lo, Hi); break;
  case ISD::STRICT_FSINH:
  case ISD::FSINH:       ExpandFloatRes_FSINH(N, Lo, Hi); break;
  case ISD::STRICT_FSQRT:
  case ISD::FSQRT:      ExpandFloatRes_FSQRT(N, Lo, Hi); break;
  case ISD::STRICT_FSUB:
  case ISD::FSUB:       ExpandFloatRes_FSUB(N, Lo, Hi); break;
  case ISD::STRICT_FTAN:
  case ISD::FTAN:       ExpandFloatRes_FTAN(N, Lo, Hi); break;
  case ISD::STRICT_FTANH:
  case ISD::FTANH:       ExpandFloatRes_FTANH(N, Lo, Hi); break;
  case ISD::STRICT_FTRUNC:
  case ISD::FTRUNC:     ExpandFloatRes_FTRUNC(N, Lo, Hi); break;
  case ISD::LOAD:       ExpandFloatRes_LOAD(N, Lo, Hi); break;
  case ISD::STRICT_SINT_TO_FP:
  case ISD::STRICT_UINT_TO_FP:
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP: ExpandFloatRes_XINT_TO_FP(N, Lo, Hi); break;
  case ISD::STRICT_FREM:
  case ISD::FREM:       ExpandFloatRes_FREM(N, Lo, Hi); break;
    // clang-format on
  }

  // If Lo/Hi is null, the sub-method took care of registering results etc.
  if (Lo.getNode())
    SetExpandedFloat(SDValue(N, ResNo), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_ConstantFP(SDNode *N, SDValue &Lo,
                                                 SDValue &Hi) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  assert(NVT.getSizeInBits() == 64 &&
         "Do not know how to expand this float constant!");
  APInt C = cast<ConstantFPSDNode>(N)->getValueAPF().bitcastToAPInt();
  SDLoc dl(N);
  Lo = DAG.getConstantFP(APFloat(DAG.EVTToAPFloatSemantics(NVT),
                                 APInt(64, C.getRawData()[1])),
                         dl, NVT);
  Hi = DAG.getConstantFP(APFloat(DAG.EVTToAPFloatSemantics(NVT),
                                 APInt(64, C.getRawData()[0])),
                         dl, NVT);
}

void DAGTypeLegalizer::ExpandFloatRes_Unary(SDNode *N, RTLIB::Libcall LC,
                                            SDValue &Lo, SDValue &Hi) {
  bool IsStrict = N->isStrictFPOpcode();
  unsigned Offset = IsStrict ? 1 : 0;
  SDValue Op = N->getOperand(0 + Offset);
  SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();
  TargetLowering::MakeLibCallOptions CallOptions;
  std::pair<SDValue, SDValue> Tmp = TLI.makeLibCall(DAG, LC, N->getValueType(0),
                                                    Op, CallOptions, SDLoc(N),
                                                    Chain);
  if (IsStrict)
    ReplaceValueWith(SDValue(N, 1), Tmp.second);
  GetPairElements(Tmp.first, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_Binary(SDNode *N, RTLIB::Libcall LC,
                                             SDValue &Lo, SDValue &Hi) {
  bool IsStrict = N->isStrictFPOpcode();
  unsigned Offset = IsStrict ? 1 : 0;
  SDValue Ops[] = { N->getOperand(0 + Offset), N->getOperand(1 + Offset) };
  SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();
  TargetLowering::MakeLibCallOptions CallOptions;
  std::pair<SDValue, SDValue> Tmp = TLI.makeLibCall(DAG, LC, N->getValueType(0),
                                                    Ops, CallOptions, SDLoc(N),
                                                    Chain);
  if (IsStrict)
    ReplaceValueWith(SDValue(N, 1), Tmp.second);
  GetPairElements(Tmp.first, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FABS(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  assert(N->getValueType(0) == MVT::ppcf128 &&
         "Logic only correct for ppcf128!");
  SDLoc dl(N);
  SDValue Tmp;
  GetExpandedFloat(N->getOperand(0), Lo, Tmp);
  Hi = DAG.getNode(ISD::FABS, dl, Tmp.getValueType(), Tmp);
  // Lo = Hi==fabs(Hi) ? Lo : -Lo;
  Lo = DAG.getSelectCC(dl, Tmp, Hi, Lo,
                   DAG.getNode(ISD::FNEG, dl, Lo.getValueType(), Lo),
                   ISD::SETEQ);
}

void DAGTypeLegalizer::ExpandFloatRes_FMINNUM(SDNode *N, SDValue &Lo,
                                              SDValue &Hi) {
  ExpandFloatRes_Binary(N, GetFPLibCall(N->getValueType(0),
                                       RTLIB::FMIN_F32, RTLIB::FMIN_F64,
                                       RTLIB::FMIN_F80, RTLIB::FMIN_F128,
                                       RTLIB::FMIN_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FMAXNUM(SDNode *N, SDValue &Lo,
                                              SDValue &Hi) {
  ExpandFloatRes_Binary(N, GetFPLibCall(N->getValueType(0),
                                        RTLIB::FMAX_F32, RTLIB::FMAX_F64,
                                        RTLIB::FMAX_F80, RTLIB::FMAX_F128,
                                        RTLIB::FMAX_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FADD(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  ExpandFloatRes_Binary(N, GetFPLibCall(N->getValueType(0),
                                        RTLIB::ADD_F32, RTLIB::ADD_F64,
                                        RTLIB::ADD_F80, RTLIB::ADD_F128,
                                        RTLIB::ADD_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FACOS(SDNode *N, SDValue &Lo,
                                            SDValue &Hi) {
  ExpandFloatRes_Unary(N,
                       GetFPLibCall(N->getValueType(0), RTLIB::ACOS_F32,
                                    RTLIB::ACOS_F64, RTLIB::ACOS_F80,
                                    RTLIB::ACOS_F128, RTLIB::ACOS_PPCF128),
                       Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FASIN(SDNode *N, SDValue &Lo,
                                            SDValue &Hi) {
  ExpandFloatRes_Unary(N,
                       GetFPLibCall(N->getValueType(0), RTLIB::ASIN_F32,
                                    RTLIB::ASIN_F64, RTLIB::ASIN_F80,
                                    RTLIB::ASIN_F128, RTLIB::ASIN_PPCF128),
                       Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FATAN(SDNode *N, SDValue &Lo,
                                            SDValue &Hi) {
  ExpandFloatRes_Unary(N,
                       GetFPLibCall(N->getValueType(0), RTLIB::ATAN_F32,
                                    RTLIB::ATAN_F64, RTLIB::ATAN_F80,
                                    RTLIB::ATAN_F128, RTLIB::ATAN_PPCF128),
                       Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FCBRT(SDNode *N, SDValue &Lo,
                                            SDValue &Hi) {
  ExpandFloatRes_Unary(N, GetFPLibCall(N->getValueType(0), RTLIB::CBRT_F32,
                                       RTLIB::CBRT_F64, RTLIB::CBRT_F80,
                                       RTLIB::CBRT_F128,
                                       RTLIB::CBRT_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FCEIL(SDNode *N,
                                            SDValue &Lo, SDValue &Hi) {
  ExpandFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                       RTLIB::CEIL_F32, RTLIB::CEIL_F64,
                                       RTLIB::CEIL_F80, RTLIB::CEIL_F128,
                                       RTLIB::CEIL_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FCOPYSIGN(SDNode *N,
                                                SDValue &Lo, SDValue &Hi) {
  ExpandFloatRes_Binary(N, GetFPLibCall(N->getValueType(0),
                                        RTLIB::COPYSIGN_F32,
                                        RTLIB::COPYSIGN_F64,
                                        RTLIB::COPYSIGN_F80,
                                        RTLIB::COPYSIGN_F128,
                                        RTLIB::COPYSIGN_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FCOS(SDNode *N,
                                           SDValue &Lo, SDValue &Hi) {
  ExpandFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                       RTLIB::COS_F32, RTLIB::COS_F64,
                                       RTLIB::COS_F80, RTLIB::COS_F128,
                                       RTLIB::COS_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FCOSH(SDNode *N, SDValue &Lo,
                                            SDValue &Hi) {
  ExpandFloatRes_Unary(N,
                       GetFPLibCall(N->getValueType(0), RTLIB::COSH_F32,
                                    RTLIB::COSH_F64, RTLIB::COSH_F80,
                                    RTLIB::COSH_F128, RTLIB::COSH_PPCF128),
                       Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FDIV(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  ExpandFloatRes_Binary(N, GetFPLibCall(N->getValueType(0),
                                        RTLIB::DIV_F32,
                                        RTLIB::DIV_F64,
                                        RTLIB::DIV_F80,
                                        RTLIB::DIV_F128,
                                        RTLIB::DIV_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FEXP(SDNode *N,
                                           SDValue &Lo, SDValue &Hi) {
  ExpandFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                       RTLIB::EXP_F32, RTLIB::EXP_F64,
                                       RTLIB::EXP_F80, RTLIB::EXP_F128,
                                       RTLIB::EXP_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FEXP2(SDNode *N,
                                            SDValue &Lo, SDValue &Hi) {
  ExpandFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                       RTLIB::EXP2_F32, RTLIB::EXP2_F64,
                                       RTLIB::EXP2_F80, RTLIB::EXP2_F128,
                                       RTLIB::EXP2_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FEXP10(SDNode *N, SDValue &Lo,
                                             SDValue &Hi) {
  ExpandFloatRes_Unary(N,
                       GetFPLibCall(N->getValueType(0), RTLIB::EXP10_F32,
                                    RTLIB::EXP10_F64, RTLIB::EXP10_F80,
                                    RTLIB::EXP10_F128, RTLIB::EXP10_PPCF128),
                       Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FFLOOR(SDNode *N,
                                             SDValue &Lo, SDValue &Hi) {
  ExpandFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                       RTLIB::FLOOR_F32, RTLIB::FLOOR_F64,
                                       RTLIB::FLOOR_F80, RTLIB::FLOOR_F128,
                                       RTLIB::FLOOR_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FLOG(SDNode *N,
                                           SDValue &Lo, SDValue &Hi) {
  ExpandFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                       RTLIB::LOG_F32, RTLIB::LOG_F64,
                                       RTLIB::LOG_F80, RTLIB::LOG_F128,
                                       RTLIB::LOG_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FLOG2(SDNode *N,
                                            SDValue &Lo, SDValue &Hi) {
  ExpandFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                       RTLIB::LOG2_F32, RTLIB::LOG2_F64,
                                       RTLIB::LOG2_F80, RTLIB::LOG2_F128,
                                       RTLIB::LOG2_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FLOG10(SDNode *N,
                                             SDValue &Lo, SDValue &Hi) {
  ExpandFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                       RTLIB::LOG10_F32, RTLIB::LOG10_F64,
                                       RTLIB::LOG10_F80, RTLIB::LOG10_F128,
                                       RTLIB::LOG10_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FMA(SDNode *N, SDValue &Lo,
                                          SDValue &Hi) {
  bool IsStrict = N->isStrictFPOpcode();
  unsigned Offset = IsStrict ? 1 : 0;
  SDValue Ops[3] = { N->getOperand(0 + Offset), N->getOperand(1 + Offset),
                     N->getOperand(2 + Offset) };
  SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();
  TargetLowering::MakeLibCallOptions CallOptions;
  std::pair<SDValue, SDValue> Tmp = TLI.makeLibCall(DAG, GetFPLibCall(N->getValueType(0),
                                                   RTLIB::FMA_F32,
                                                   RTLIB::FMA_F64,
                                                   RTLIB::FMA_F80,
                                                   RTLIB::FMA_F128,
                                                   RTLIB::FMA_PPCF128),
                                 N->getValueType(0), Ops, CallOptions,
                                 SDLoc(N), Chain);
  if (IsStrict)
    ReplaceValueWith(SDValue(N, 1), Tmp.second);
  GetPairElements(Tmp.first, Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FMUL(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  ExpandFloatRes_Binary(N, GetFPLibCall(N->getValueType(0),
                                                   RTLIB::MUL_F32,
                                                   RTLIB::MUL_F64,
                                                   RTLIB::MUL_F80,
                                                   RTLIB::MUL_F128,
                                                   RTLIB::MUL_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FNEARBYINT(SDNode *N,
                                                 SDValue &Lo, SDValue &Hi) {
  ExpandFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                       RTLIB::NEARBYINT_F32,
                                       RTLIB::NEARBYINT_F64,
                                       RTLIB::NEARBYINT_F80,
                                       RTLIB::NEARBYINT_F128,
                                       RTLIB::NEARBYINT_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FNEG(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  SDLoc dl(N);
  GetExpandedFloat(N->getOperand(0), Lo, Hi);
  Lo = DAG.getNode(ISD::FNEG, dl, Lo.getValueType(), Lo);
  Hi = DAG.getNode(ISD::FNEG, dl, Hi.getValueType(), Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FP_EXTEND(SDNode *N, SDValue &Lo,
                                                SDValue &Hi) {
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDLoc dl(N);
  bool IsStrict = N->isStrictFPOpcode();

  SDValue Chain;
  if (IsStrict) {
    // If the expanded type is the same as the input type, just bypass the node.
    if (NVT == N->getOperand(1).getValueType()) {
      Hi = N->getOperand(1);
      Chain = N->getOperand(0);
    } else {
      // Other we need to extend.
      Hi = DAG.getNode(ISD::STRICT_FP_EXTEND, dl, { NVT, MVT::Other },
                       { N->getOperand(0), N->getOperand(1) });
      Chain = Hi.getValue(1);
    }
  } else {
    Hi = DAG.getNode(ISD::FP_EXTEND, dl, NVT, N->getOperand(0));
  }

  Lo = DAG.getConstantFP(APFloat(DAG.EVTToAPFloatSemantics(NVT),
                                 APInt(NVT.getSizeInBits(), 0)), dl, NVT);

  if (IsStrict)
    ReplaceValueWith(SDValue(N, 1), Chain);
}

void DAGTypeLegalizer::ExpandFloatRes_FPOW(SDNode *N,
                                           SDValue &Lo, SDValue &Hi) {
  ExpandFloatRes_Binary(N, GetFPLibCall(N->getValueType(0),
                                        RTLIB::POW_F32, RTLIB::POW_F64,
                                        RTLIB::POW_F80, RTLIB::POW_F128,
                                        RTLIB::POW_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FPOWI(SDNode *N,
                                            SDValue &Lo, SDValue &Hi) {
  ExpandFloatRes_Binary(N, RTLIB::getPOWI(N->getValueType(0)), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FLDEXP(SDNode *N, SDValue &Lo,
                                             SDValue &Hi) {
  ExpandFloatRes_Binary(N, RTLIB::getLDEXP(N->getValueType(0)), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FREEZE(SDNode *N,
                                             SDValue &Lo, SDValue &Hi) {
  assert(N->getValueType(0) == MVT::ppcf128 &&
         "Logic only correct for ppcf128!");

  SDLoc dl(N);
  GetExpandedFloat(N->getOperand(0), Lo, Hi);
  Lo = DAG.getNode(ISD::FREEZE, dl, Lo.getValueType(), Lo);
  Hi = DAG.getNode(ISD::FREEZE, dl, Hi.getValueType(), Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FREM(SDNode *N,
                                           SDValue &Lo, SDValue &Hi) {
  ExpandFloatRes_Binary(N, GetFPLibCall(N->getValueType(0),
                                        RTLIB::REM_F32, RTLIB::REM_F64,
                                        RTLIB::REM_F80, RTLIB::REM_F128,
                                        RTLIB::REM_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FRINT(SDNode *N,
                                            SDValue &Lo, SDValue &Hi) {
  ExpandFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                       RTLIB::RINT_F32, RTLIB::RINT_F64,
                                       RTLIB::RINT_F80, RTLIB::RINT_F128,
                                       RTLIB::RINT_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FROUND(SDNode *N,
                                             SDValue &Lo, SDValue &Hi) {
  ExpandFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                       RTLIB::ROUND_F32,
                                       RTLIB::ROUND_F64,
                                       RTLIB::ROUND_F80,
                                       RTLIB::ROUND_F128,
                                       RTLIB::ROUND_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FROUNDEVEN(SDNode *N,
                                             SDValue &Lo, SDValue &Hi) {
  ExpandFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                       RTLIB::ROUNDEVEN_F32,
                                       RTLIB::ROUNDEVEN_F64,
                                       RTLIB::ROUNDEVEN_F80,
                                       RTLIB::ROUNDEVEN_F128,
                                       RTLIB::ROUNDEVEN_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FSIN(SDNode *N,
                                           SDValue &Lo, SDValue &Hi) {
  ExpandFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                       RTLIB::SIN_F32, RTLIB::SIN_F64,
                                       RTLIB::SIN_F80, RTLIB::SIN_F128,
                                       RTLIB::SIN_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FSINH(SDNode *N, SDValue &Lo,
                                            SDValue &Hi) {
  ExpandFloatRes_Unary(N,
                       GetFPLibCall(N->getValueType(0), RTLIB::SINH_F32,
                                    RTLIB::SINH_F64, RTLIB::SINH_F80,
                                    RTLIB::SINH_F128, RTLIB::SINH_PPCF128),
                       Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FSQRT(SDNode *N,
                                            SDValue &Lo, SDValue &Hi) {
  ExpandFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                       RTLIB::SQRT_F32, RTLIB::SQRT_F64,
                                       RTLIB::SQRT_F80, RTLIB::SQRT_F128,
                                       RTLIB::SQRT_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FSUB(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  ExpandFloatRes_Binary(N, GetFPLibCall(N->getValueType(0),
                                        RTLIB::SUB_F32,
                                        RTLIB::SUB_F64,
                                        RTLIB::SUB_F80,
                                        RTLIB::SUB_F128,
                                        RTLIB::SUB_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FTAN(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  ExpandFloatRes_Unary(N,
                       GetFPLibCall(N->getValueType(0), RTLIB::TAN_F32,
                                    RTLIB::TAN_F64, RTLIB::TAN_F80,
                                    RTLIB::TAN_F128, RTLIB::TAN_PPCF128),
                       Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FTANH(SDNode *N, SDValue &Lo,
                                            SDValue &Hi) {
  ExpandFloatRes_Unary(N,
                       GetFPLibCall(N->getValueType(0), RTLIB::TANH_F32,
                                    RTLIB::TANH_F64, RTLIB::TANH_F80,
                                    RTLIB::TANH_F128, RTLIB::TANH_PPCF128),
                       Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_FTRUNC(SDNode *N,
                                             SDValue &Lo, SDValue &Hi) {
  ExpandFloatRes_Unary(N, GetFPLibCall(N->getValueType(0),
                                       RTLIB::TRUNC_F32, RTLIB::TRUNC_F64,
                                       RTLIB::TRUNC_F80, RTLIB::TRUNC_F128,
                                       RTLIB::TRUNC_PPCF128), Lo, Hi);
}

void DAGTypeLegalizer::ExpandFloatRes_LOAD(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  if (ISD::isNormalLoad(N)) {
    ExpandRes_NormalLoad(N, Lo, Hi);
    return;
  }

  assert(ISD::isUNINDEXEDLoad(N) && "Indexed load during type legalization!");
  LoadSDNode *LD = cast<LoadSDNode>(N);
  SDValue Chain = LD->getChain();
  SDValue Ptr = LD->getBasePtr();
  SDLoc dl(N);

  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), LD->getValueType(0));
  assert(NVT.isByteSized() && "Expanded type not byte sized!");
  assert(LD->getMemoryVT().bitsLE(NVT) && "Float type not round?");

  Hi = DAG.getExtLoad(LD->getExtensionType(), dl, NVT, Chain, Ptr,
                      LD->getMemoryVT(), LD->getMemOperand());

  // Remember the chain.
  Chain = Hi.getValue(1);

  // The low part is zero.
  Lo = DAG.getConstantFP(APFloat(DAG.EVTToAPFloatSemantics(NVT),
                                 APInt(NVT.getSizeInBits(), 0)), dl, NVT);

  // Modified the chain - switch anything that used the old chain to use the
  // new one.
  ReplaceValueWith(SDValue(LD, 1), Chain);
}

void DAGTypeLegalizer::ExpandFloatRes_XINT_TO_FP(SDNode *N, SDValue &Lo,
                                                 SDValue &Hi) {
  assert(N->getValueType(0) == MVT::ppcf128 && "Unsupported XINT_TO_FP!");
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  bool Strict = N->isStrictFPOpcode();
  SDValue Src = N->getOperand(Strict ? 1 : 0);
  EVT SrcVT = Src.getValueType();
  bool isSigned = N->getOpcode() == ISD::SINT_TO_FP ||
                  N->getOpcode() == ISD::STRICT_SINT_TO_FP;
  SDLoc dl(N);
  SDValue Chain = Strict ? N->getOperand(0) : DAG.getEntryNode();

  // TODO: Any other flags to propagate?
  SDNodeFlags Flags;
  Flags.setNoFPExcept(N->getFlags().hasNoFPExcept());

  // First do an SINT_TO_FP, whether the original was signed or unsigned.
  // When promoting partial word types to i32 we must honor the signedness,
  // though.
  if (SrcVT.bitsLE(MVT::i32)) {
    // The integer can be represented exactly in an f64.
    Lo = DAG.getConstantFP(APFloat(DAG.EVTToAPFloatSemantics(NVT),
                                   APInt(NVT.getSizeInBits(), 0)), dl, NVT);
    if (Strict) {
      Hi = DAG.getNode(N->getOpcode(), dl, DAG.getVTList(NVT, MVT::Other),
                       {Chain, Src}, Flags);
      Chain = Hi.getValue(1);
    } else
      Hi = DAG.getNode(N->getOpcode(), dl, NVT, Src);
  } else {
    RTLIB::Libcall LC = RTLIB::UNKNOWN_LIBCALL;
    if (SrcVT.bitsLE(MVT::i64)) {
      Src = DAG.getNode(isSigned ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND, dl,
                        MVT::i64, Src);
      LC = RTLIB::SINTTOFP_I64_PPCF128;
    } else if (SrcVT.bitsLE(MVT::i128)) {
      Src = DAG.getNode(ISD::SIGN_EXTEND, dl, MVT::i128, Src);
      LC = RTLIB::SINTTOFP_I128_PPCF128;
    }
    assert(LC != RTLIB::UNKNOWN_LIBCALL && "Unsupported XINT_TO_FP!");

    TargetLowering::MakeLibCallOptions CallOptions;
    CallOptions.setSExt(true);
    std::pair<SDValue, SDValue> Tmp =
        TLI.makeLibCall(DAG, LC, VT, Src, CallOptions, dl, Chain);
    if (Strict)
      Chain = Tmp.second;
    GetPairElements(Tmp.first, Lo, Hi);
  }

  // No need to complement for unsigned 32-bit integers
  if (isSigned || SrcVT.bitsLE(MVT::i32)) {
    if (Strict)
      ReplaceValueWith(SDValue(N, 1), Chain);

    return;
  }

  // Unsigned - fix up the SINT_TO_FP value just calculated.
  // FIXME: For unsigned i128 to ppc_fp128 conversion, we need to carefully
  // keep semantics correctness if the integer is not exactly representable
  // here. See ExpandLegalINT_TO_FP.
  Hi = DAG.getNode(ISD::BUILD_PAIR, dl, VT, Lo, Hi);
  SrcVT = Src.getValueType();

  // x>=0 ? (ppcf128)(iN)x : (ppcf128)(iN)x + 2^N; N=32,64,128.
  static const uint64_t TwoE32[]  = { 0x41f0000000000000LL, 0 };
  static const uint64_t TwoE64[]  = { 0x43f0000000000000LL, 0 };
  static const uint64_t TwoE128[] = { 0x47f0000000000000LL, 0 };
  ArrayRef<uint64_t> Parts;

  switch (SrcVT.getSimpleVT().SimpleTy) {
  default:
    llvm_unreachable("Unsupported UINT_TO_FP!");
  case MVT::i32:
    Parts = TwoE32;
    break;
  case MVT::i64:
    Parts = TwoE64;
    break;
  case MVT::i128:
    Parts = TwoE128;
    break;
  }

  // TODO: Are there other fast-math-flags to propagate to this FADD?
  SDValue NewLo = DAG.getConstantFP(
      APFloat(APFloat::PPCDoubleDouble(), APInt(128, Parts)), dl, MVT::ppcf128);
  if (Strict) {
    Lo = DAG.getNode(ISD::STRICT_FADD, dl, DAG.getVTList(VT, MVT::Other),
                     {Chain, Hi, NewLo}, Flags);
    Chain = Lo.getValue(1);
    ReplaceValueWith(SDValue(N, 1), Chain);
  } else
    Lo = DAG.getNode(ISD::FADD, dl, VT, Hi, NewLo);
  Lo = DAG.getSelectCC(dl, Src, DAG.getConstant(0, dl, SrcVT),
                       Lo, Hi, ISD::SETLT);
  GetPairElements(Lo, Lo, Hi);
}


//===----------------------------------------------------------------------===//
//  Float Operand Expansion
//===----------------------------------------------------------------------===//

/// ExpandFloatOperand - This method is called when the specified operand of the
/// specified node is found to need expansion.  At this point, all of the result
/// types of the node are known to be legal, but other operands of the node may
/// need promotion or expansion as well as the specified one.
bool DAGTypeLegalizer::ExpandFloatOperand(SDNode *N, unsigned OpNo) {
  LLVM_DEBUG(dbgs() << "Expand float operand: "; N->dump(&DAG));
  SDValue Res = SDValue();

  // See if the target wants to custom expand this node.
  if (CustomLowerNode(N, N->getOperand(OpNo).getValueType(), false))
    return false;

  switch (N->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "ExpandFloatOperand Op #" << OpNo << ": ";
    N->dump(&DAG); dbgs() << "\n";
#endif
    report_fatal_error("Do not know how to expand this operator's operand!");

  case ISD::BITCAST:         Res = ExpandOp_BITCAST(N); break;
  case ISD::BUILD_VECTOR:    Res = ExpandOp_BUILD_VECTOR(N); break;
  case ISD::EXTRACT_ELEMENT: Res = ExpandOp_EXTRACT_ELEMENT(N); break;

  case ISD::BR_CC:      Res = ExpandFloatOp_BR_CC(N); break;
  case ISD::FCOPYSIGN:  Res = ExpandFloatOp_FCOPYSIGN(N); break;
  case ISD::STRICT_FP_ROUND:
  case ISD::FP_ROUND:   Res = ExpandFloatOp_FP_ROUND(N); break;
  case ISD::STRICT_FP_TO_SINT:
  case ISD::STRICT_FP_TO_UINT:
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT: Res = ExpandFloatOp_FP_TO_XINT(N); break;
  case ISD::LROUND:     Res = ExpandFloatOp_LROUND(N); break;
  case ISD::LLROUND:    Res = ExpandFloatOp_LLROUND(N); break;
  case ISD::LRINT:      Res = ExpandFloatOp_LRINT(N); break;
  case ISD::LLRINT:     Res = ExpandFloatOp_LLRINT(N); break;
  case ISD::SELECT_CC:  Res = ExpandFloatOp_SELECT_CC(N); break;
  case ISD::STRICT_FSETCC:
  case ISD::STRICT_FSETCCS:
  case ISD::SETCC:      Res = ExpandFloatOp_SETCC(N); break;
  case ISD::STORE:      Res = ExpandFloatOp_STORE(cast<StoreSDNode>(N),
                                                  OpNo); break;
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

/// FloatExpandSetCCOperands - Expand the operands of a comparison.  This code
/// is shared among BR_CC, SELECT_CC, and SETCC handlers.
void DAGTypeLegalizer::FloatExpandSetCCOperands(SDValue &NewLHS,
                                                SDValue &NewRHS,
                                                ISD::CondCode &CCCode,
                                                const SDLoc &dl, SDValue &Chain,
                                                bool IsSignaling) {
  SDValue LHSLo, LHSHi, RHSLo, RHSHi;
  GetExpandedFloat(NewLHS, LHSLo, LHSHi);
  GetExpandedFloat(NewRHS, RHSLo, RHSHi);

  assert(NewLHS.getValueType() == MVT::ppcf128 && "Unsupported setcc type!");

  // FIXME:  This generated code sucks.  We want to generate
  //         FCMPU crN, hi1, hi2
  //         BNE crN, L:
  //         FCMPU crN, lo1, lo2
  // The following can be improved, but not that much.
  SDValue Tmp1, Tmp2, Tmp3, OutputChain;
  Tmp1 = DAG.getSetCC(dl, getSetCCResultType(LHSHi.getValueType()), LHSHi,
                      RHSHi, ISD::SETOEQ, Chain, IsSignaling);
  OutputChain = Tmp1->getNumValues() > 1 ? Tmp1.getValue(1) : SDValue();
  Tmp2 = DAG.getSetCC(dl, getSetCCResultType(LHSLo.getValueType()), LHSLo,
                      RHSLo, CCCode, OutputChain, IsSignaling);
  OutputChain = Tmp2->getNumValues() > 1 ? Tmp2.getValue(1) : SDValue();
  Tmp3 = DAG.getNode(ISD::AND, dl, Tmp1.getValueType(), Tmp1, Tmp2);
  Tmp1 =
      DAG.getSetCC(dl, getSetCCResultType(LHSHi.getValueType()), LHSHi, RHSHi,
                   ISD::SETUNE, OutputChain, IsSignaling);
  OutputChain = Tmp1->getNumValues() > 1 ? Tmp1.getValue(1) : SDValue();
  Tmp2 = DAG.getSetCC(dl, getSetCCResultType(LHSHi.getValueType()), LHSHi,
                      RHSHi, CCCode, OutputChain, IsSignaling);
  OutputChain = Tmp2->getNumValues() > 1 ? Tmp2.getValue(1) : SDValue();
  Tmp1 = DAG.getNode(ISD::AND, dl, Tmp1.getValueType(), Tmp1, Tmp2);
  NewLHS = DAG.getNode(ISD::OR, dl, Tmp1.getValueType(), Tmp1, Tmp3);
  NewRHS = SDValue();   // LHS is the result, not a compare.
  Chain = OutputChain;
}

SDValue DAGTypeLegalizer::ExpandFloatOp_BR_CC(SDNode *N) {
  SDValue NewLHS = N->getOperand(2), NewRHS = N->getOperand(3);
  ISD::CondCode CCCode = cast<CondCodeSDNode>(N->getOperand(1))->get();
  SDValue Chain;
  FloatExpandSetCCOperands(NewLHS, NewRHS, CCCode, SDLoc(N), Chain);

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

SDValue DAGTypeLegalizer::ExpandFloatOp_FCOPYSIGN(SDNode *N) {
  assert(N->getOperand(1).getValueType() == MVT::ppcf128 &&
         "Logic only correct for ppcf128!");
  SDValue Lo, Hi;
  GetExpandedFloat(N->getOperand(1), Lo, Hi);
  // The ppcf128 value is providing only the sign; take it from the
  // higher-order double (which must have the larger magnitude).
  return DAG.getNode(ISD::FCOPYSIGN, SDLoc(N),
                     N->getValueType(0), N->getOperand(0), Hi);
}

SDValue DAGTypeLegalizer::ExpandFloatOp_FP_ROUND(SDNode *N) {
  bool IsStrict = N->isStrictFPOpcode();
  assert(N->getOperand(IsStrict ? 1 : 0).getValueType() == MVT::ppcf128 &&
         "Logic only correct for ppcf128!");
  SDValue Lo, Hi;
  GetExpandedFloat(N->getOperand(IsStrict ? 1 : 0), Lo, Hi);

  if (!IsStrict)
    // Round it the rest of the way (e.g. to f32) if needed.
    return DAG.getNode(ISD::FP_ROUND, SDLoc(N),
                       N->getValueType(0), Hi, N->getOperand(1));

  // Eliminate the node if the input float type is the same as the output float
  // type.
  if (Hi.getValueType() == N->getValueType(0)) {
    // Connect the output chain to the input chain, unlinking the node.
    ReplaceValueWith(SDValue(N, 1), N->getOperand(0));
    ReplaceValueWith(SDValue(N, 0), Hi);
    return SDValue();
  }

  SDValue Expansion = DAG.getNode(ISD::STRICT_FP_ROUND, SDLoc(N),
                                  {N->getValueType(0), MVT::Other},
                                  {N->getOperand(0), Hi, N->getOperand(2)});
  ReplaceValueWith(SDValue(N, 1), Expansion.getValue(1));
  ReplaceValueWith(SDValue(N, 0), Expansion);
  return SDValue();
}

SDValue DAGTypeLegalizer::ExpandFloatOp_FP_TO_XINT(SDNode *N) {
  EVT RVT = N->getValueType(0);
  SDLoc dl(N);

  bool IsStrict = N->isStrictFPOpcode();
  bool Signed = N->getOpcode() == ISD::FP_TO_SINT ||
                N->getOpcode() == ISD::STRICT_FP_TO_SINT;
  SDValue Op = N->getOperand(IsStrict ? 1 : 0);
  SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();

  EVT NVT;
  RTLIB::Libcall LC = findFPToIntLibcall(Op.getValueType(), RVT, NVT, Signed);
  assert(LC != RTLIB::UNKNOWN_LIBCALL && NVT.isSimple() &&
         "Unsupported FP_TO_XINT!");
  TargetLowering::MakeLibCallOptions CallOptions;
  std::pair<SDValue, SDValue> Tmp =
      TLI.makeLibCall(DAG, LC, NVT, Op, CallOptions, dl, Chain);
  if (!IsStrict)
    return Tmp.first;

  ReplaceValueWith(SDValue(N, 1), Tmp.second);
  ReplaceValueWith(SDValue(N, 0), Tmp.first);
  return SDValue();
}

SDValue DAGTypeLegalizer::ExpandFloatOp_SELECT_CC(SDNode *N) {
  SDValue NewLHS = N->getOperand(0), NewRHS = N->getOperand(1);
  ISD::CondCode CCCode = cast<CondCodeSDNode>(N->getOperand(4))->get();
  SDValue Chain;
  FloatExpandSetCCOperands(NewLHS, NewRHS, CCCode, SDLoc(N), Chain);

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

SDValue DAGTypeLegalizer::ExpandFloatOp_SETCC(SDNode *N) {
  bool IsStrict = N->isStrictFPOpcode();
  SDValue NewLHS = N->getOperand(IsStrict ? 1 : 0);
  SDValue NewRHS = N->getOperand(IsStrict ? 2 : 1);
  SDValue Chain = IsStrict ? N->getOperand(0) : SDValue();
  ISD::CondCode CCCode =
      cast<CondCodeSDNode>(N->getOperand(IsStrict ? 3 : 2))->get();
  FloatExpandSetCCOperands(NewLHS, NewRHS, CCCode, SDLoc(N), Chain,
                           N->getOpcode() == ISD::STRICT_FSETCCS);

  // FloatExpandSetCCOperands always returned a scalar.
  assert(!NewRHS.getNode() && "Expect to return scalar");
  assert(NewLHS.getValueType() == N->getValueType(0) &&
         "Unexpected setcc expansion!");
  if (Chain) {
    ReplaceValueWith(SDValue(N, 0), NewLHS);
    ReplaceValueWith(SDValue(N, 1), Chain);
    return SDValue();
  }
  return NewLHS;
}

SDValue DAGTypeLegalizer::ExpandFloatOp_STORE(SDNode *N, unsigned OpNo) {
  if (ISD::isNormalStore(N))
    return ExpandOp_NormalStore(N, OpNo);

  assert(ISD::isUNINDEXEDStore(N) && "Indexed store during type legalization!");
  assert(OpNo == 1 && "Can only expand the stored value so far");
  StoreSDNode *ST = cast<StoreSDNode>(N);

  SDValue Chain = ST->getChain();
  SDValue Ptr = ST->getBasePtr();

  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(),
                                     ST->getValue().getValueType());
  assert(NVT.isByteSized() && "Expanded type not byte sized!");
  assert(ST->getMemoryVT().bitsLE(NVT) && "Float type not round?");
  (void)NVT;

  SDValue Lo, Hi;
  GetExpandedOp(ST->getValue(), Lo, Hi);

  return DAG.getTruncStore(Chain, SDLoc(N), Hi, Ptr,
                           ST->getMemoryVT(), ST->getMemOperand());
}

SDValue DAGTypeLegalizer::ExpandFloatOp_LROUND(SDNode *N) {
  EVT RVT = N->getValueType(0);
  EVT RetVT = N->getOperand(0).getValueType();
  TargetLowering::MakeLibCallOptions CallOptions;
  return TLI.makeLibCall(DAG, GetFPLibCall(RetVT,
                                           RTLIB::LROUND_F32,
                                           RTLIB::LROUND_F64,
                                           RTLIB::LROUND_F80,
                                           RTLIB::LROUND_F128,
                                           RTLIB::LROUND_PPCF128),
                         RVT, N->getOperand(0), CallOptions, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::ExpandFloatOp_LLROUND(SDNode *N) {
  EVT RVT = N->getValueType(0);
  EVT RetVT = N->getOperand(0).getValueType();
  TargetLowering::MakeLibCallOptions CallOptions;
  return TLI.makeLibCall(DAG, GetFPLibCall(RetVT,
                                           RTLIB::LLROUND_F32,
                                           RTLIB::LLROUND_F64,
                                           RTLIB::LLROUND_F80,
                                           RTLIB::LLROUND_F128,
                                           RTLIB::LLROUND_PPCF128),
                         RVT, N->getOperand(0), CallOptions, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::ExpandFloatOp_LRINT(SDNode *N) {
  EVT RVT = N->getValueType(0);
  EVT RetVT = N->getOperand(0).getValueType();
  TargetLowering::MakeLibCallOptions CallOptions;
  return TLI.makeLibCall(DAG, GetFPLibCall(RetVT,
                                           RTLIB::LRINT_F32,
                                           RTLIB::LRINT_F64,
                                           RTLIB::LRINT_F80,
                                           RTLIB::LRINT_F128,
                                           RTLIB::LRINT_PPCF128),
                         RVT, N->getOperand(0), CallOptions, SDLoc(N)).first;
}

SDValue DAGTypeLegalizer::ExpandFloatOp_LLRINT(SDNode *N) {
  EVT RVT = N->getValueType(0);
  EVT RetVT = N->getOperand(0).getValueType();
  TargetLowering::MakeLibCallOptions CallOptions;
  return TLI.makeLibCall(DAG, GetFPLibCall(RetVT,
                                           RTLIB::LLRINT_F32,
                                           RTLIB::LLRINT_F64,
                                           RTLIB::LLRINT_F80,
                                           RTLIB::LLRINT_F128,
                                           RTLIB::LLRINT_PPCF128),
                         RVT, N->getOperand(0), CallOptions, SDLoc(N)).first;
}

//===----------------------------------------------------------------------===//
//  Float Operand Promotion
//===----------------------------------------------------------------------===//
//

static ISD::NodeType GetPromotionOpcode(EVT OpVT, EVT RetVT) {
  if (OpVT == MVT::f16) {
    return ISD::FP16_TO_FP;
  } else if (RetVT == MVT::f16) {
    return ISD::FP_TO_FP16;
  } else if (OpVT == MVT::bf16) {
    return ISD::BF16_TO_FP;
  } else if (RetVT == MVT::bf16) {
    return ISD::FP_TO_BF16;
  }

  report_fatal_error("Attempt at an invalid promotion-related conversion");
}

static ISD::NodeType GetPromotionOpcodeStrict(EVT OpVT, EVT RetVT) {
  if (OpVT == MVT::f16)
    return ISD::STRICT_FP16_TO_FP;

  if (RetVT == MVT::f16)
    return ISD::STRICT_FP_TO_FP16;

  if (OpVT == MVT::bf16)
    return ISD::STRICT_BF16_TO_FP;

  if (RetVT == MVT::bf16)
    return ISD::STRICT_FP_TO_BF16;

  report_fatal_error("Attempt at an invalid promotion-related conversion");
}

bool DAGTypeLegalizer::PromoteFloatOperand(SDNode *N, unsigned OpNo) {
  LLVM_DEBUG(dbgs() << "Promote float operand " << OpNo << ": "; N->dump(&DAG));
  SDValue R = SDValue();

  if (CustomLowerNode(N, N->getOperand(OpNo).getValueType(), false)) {
    LLVM_DEBUG(dbgs() << "Node has been custom lowered, done\n");
    return false;
  }

  // Nodes that use a promotion-requiring floating point operand, but doesn't
  // produce a promotion-requiring floating point result, need to be legalized
  // to use the promoted float operand.  Nodes that produce at least one
  // promotion-requiring floating point result have their operands legalized as
  // a part of PromoteFloatResult.
  // clang-format off
  switch (N->getOpcode()) {
    default:
  #ifndef NDEBUG
      dbgs() << "PromoteFloatOperand Op #" << OpNo << ": ";
      N->dump(&DAG); dbgs() << "\n";
  #endif
      report_fatal_error("Do not know how to promote this operator's operand!");

    case ISD::BITCAST:    R = PromoteFloatOp_BITCAST(N, OpNo); break;
    case ISD::FCOPYSIGN:  R = PromoteFloatOp_FCOPYSIGN(N, OpNo); break;
    case ISD::FP_TO_SINT:
    case ISD::FP_TO_UINT:
    case ISD::LRINT:
    case ISD::LLRINT:     R = PromoteFloatOp_UnaryOp(N, OpNo); break;
    case ISD::FP_TO_SINT_SAT:
    case ISD::FP_TO_UINT_SAT:
                          R = PromoteFloatOp_FP_TO_XINT_SAT(N, OpNo); break;
    case ISD::FP_EXTEND:  R = PromoteFloatOp_FP_EXTEND(N, OpNo); break;
    case ISD::STRICT_FP_EXTEND:
      R = PromoteFloatOp_STRICT_FP_EXTEND(N, OpNo);
      break;
    case ISD::SELECT_CC:  R = PromoteFloatOp_SELECT_CC(N, OpNo); break;
    case ISD::SETCC:      R = PromoteFloatOp_SETCC(N, OpNo); break;
    case ISD::STORE:      R = PromoteFloatOp_STORE(N, OpNo); break;
    case ISD::ATOMIC_STORE: R = PromoteFloatOp_ATOMIC_STORE(N, OpNo); break;
  }
  // clang-format on

  if (R.getNode())
    ReplaceValueWith(SDValue(N, 0), R);
  return false;
}

SDValue DAGTypeLegalizer::PromoteFloatOp_BITCAST(SDNode *N, unsigned OpNo) {
  SDValue Op = N->getOperand(0);
  EVT OpVT = Op->getValueType(0);

  SDValue Promoted = GetPromotedFloat(N->getOperand(0));
  EVT PromotedVT = Promoted->getValueType(0);

  // Convert the promoted float value to the desired IVT.
  EVT IVT = EVT::getIntegerVT(*DAG.getContext(), OpVT.getSizeInBits());
  SDValue Convert = DAG.getNode(GetPromotionOpcode(PromotedVT, OpVT), SDLoc(N),
                                IVT, Promoted);
  // The final result type might not be an scalar so we need a bitcast. The
  // bitcast will be further legalized if needed.
  return DAG.getBitcast(N->getValueType(0), Convert);
}

// Promote Operand 1 of FCOPYSIGN.  Operand 0 ought to be handled by
// PromoteFloatRes_FCOPYSIGN.
SDValue DAGTypeLegalizer::PromoteFloatOp_FCOPYSIGN(SDNode *N, unsigned OpNo) {
  assert (OpNo == 1 && "Only Operand 1 must need promotion here");
  SDValue Op1 = GetPromotedFloat(N->getOperand(1));

  return DAG.getNode(N->getOpcode(), SDLoc(N), N->getValueType(0),
                     N->getOperand(0), Op1);
}

// Convert the promoted float value to the desired integer type
SDValue DAGTypeLegalizer::PromoteFloatOp_UnaryOp(SDNode *N, unsigned OpNo) {
  SDValue Op = GetPromotedFloat(N->getOperand(0));
  return DAG.getNode(N->getOpcode(), SDLoc(N), N->getValueType(0), Op);
}

SDValue DAGTypeLegalizer::PromoteFloatOp_FP_TO_XINT_SAT(SDNode *N,
                                                        unsigned OpNo) {
  SDValue Op = GetPromotedFloat(N->getOperand(0));
  return DAG.getNode(N->getOpcode(), SDLoc(N), N->getValueType(0), Op,
                     N->getOperand(1));
}

SDValue DAGTypeLegalizer::PromoteFloatOp_FP_EXTEND(SDNode *N, unsigned OpNo) {
  SDValue Op = GetPromotedFloat(N->getOperand(0));
  EVT VT = N->getValueType(0);

  // Desired VT is same as promoted type.  Use promoted float directly.
  if (VT == Op->getValueType(0))
    return Op;

  // Else, extend the promoted float value to the desired VT.
  return DAG.getNode(ISD::FP_EXTEND, SDLoc(N), VT, Op);
}

SDValue DAGTypeLegalizer::PromoteFloatOp_STRICT_FP_EXTEND(SDNode *N,
                                                          unsigned OpNo) {
  assert(OpNo == 1 && "Promoting unpromotable operand");

  SDValue Op = GetPromotedFloat(N->getOperand(1));
  EVT VT = N->getValueType(0);

  // Desired VT is same as promoted type.  Use promoted float directly.
  if (VT == Op->getValueType(0)) {
    ReplaceValueWith(SDValue(N, 1), N->getOperand(0));
    return Op;
  }

  // Else, extend the promoted float value to the desired VT.
  SDValue Res = DAG.getNode(ISD::STRICT_FP_EXTEND, SDLoc(N), N->getVTList(),
                            N->getOperand(0), Op);
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  return Res;
}

// Promote the float operands used for comparison.  The true- and false-
// operands have the same type as the result and are promoted, if needed, by
// PromoteFloatRes_SELECT_CC
SDValue DAGTypeLegalizer::PromoteFloatOp_SELECT_CC(SDNode *N, unsigned OpNo) {
  SDValue LHS = GetPromotedFloat(N->getOperand(0));
  SDValue RHS = GetPromotedFloat(N->getOperand(1));

  return DAG.getNode(ISD::SELECT_CC, SDLoc(N), N->getValueType(0),
                     LHS, RHS, N->getOperand(2), N->getOperand(3),
                     N->getOperand(4));
}

// Construct a SETCC that compares the promoted values and sets the conditional
// code.
SDValue DAGTypeLegalizer::PromoteFloatOp_SETCC(SDNode *N, unsigned OpNo) {
  EVT VT = N->getValueType(0);
  SDValue Op0 = GetPromotedFloat(N->getOperand(0));
  SDValue Op1 = GetPromotedFloat(N->getOperand(1));
  ISD::CondCode CCCode = cast<CondCodeSDNode>(N->getOperand(2))->get();

  return DAG.getSetCC(SDLoc(N), VT, Op0, Op1, CCCode);

}

// Lower the promoted Float down to the integer value of same size and construct
// a STORE of the integer value.
SDValue DAGTypeLegalizer::PromoteFloatOp_STORE(SDNode *N, unsigned OpNo) {
  StoreSDNode *ST = cast<StoreSDNode>(N);
  SDValue Val = ST->getValue();
  SDLoc DL(N);

  SDValue Promoted = GetPromotedFloat(Val);
  EVT VT = ST->getOperand(1).getValueType();
  EVT IVT = EVT::getIntegerVT(*DAG.getContext(), VT.getSizeInBits());

  SDValue NewVal;
  NewVal = DAG.getNode(GetPromotionOpcode(Promoted.getValueType(), VT), DL,
                       IVT, Promoted);

  return DAG.getStore(ST->getChain(), DL, NewVal, ST->getBasePtr(),
                      ST->getMemOperand());
}

SDValue DAGTypeLegalizer::PromoteFloatOp_ATOMIC_STORE(SDNode *N,
                                                      unsigned OpNo) {
  AtomicSDNode *ST = cast<AtomicSDNode>(N);
  SDValue Val = ST->getVal();
  SDLoc DL(N);

  SDValue Promoted = GetPromotedFloat(Val);
  EVT VT = ST->getOperand(1).getValueType();
  EVT IVT = EVT::getIntegerVT(*DAG.getContext(), VT.getSizeInBits());

  SDValue NewVal = DAG.getNode(GetPromotionOpcode(Promoted.getValueType(), VT),
                               DL, IVT, Promoted);

  return DAG.getAtomic(ISD::ATOMIC_STORE, DL, IVT, ST->getChain(), NewVal,
                       ST->getBasePtr(), ST->getMemOperand());
}

//===----------------------------------------------------------------------===//
//  Float Result Promotion
//===----------------------------------------------------------------------===//

void DAGTypeLegalizer::PromoteFloatResult(SDNode *N, unsigned ResNo) {
  LLVM_DEBUG(dbgs() << "Promote float result " << ResNo << ": "; N->dump(&DAG));
  SDValue R = SDValue();

  // See if the target wants to custom expand this node.
  if (CustomLowerNode(N, N->getValueType(ResNo), true)) {
    LLVM_DEBUG(dbgs() << "Node has been custom expanded, done\n");
    return;
  }

  switch (N->getOpcode()) {
    // These opcodes cannot appear if promotion of FP16 is done in the backend
    // instead of Clang
    case ISD::FP16_TO_FP:
    case ISD::FP_TO_FP16:
    default:
#ifndef NDEBUG
      dbgs() << "PromoteFloatResult #" << ResNo << ": ";
      N->dump(&DAG); dbgs() << "\n";
#endif
      report_fatal_error("Do not know how to promote this operator's result!");

    case ISD::BITCAST:    R = PromoteFloatRes_BITCAST(N); break;
    case ISD::ConstantFP: R = PromoteFloatRes_ConstantFP(N); break;
    case ISD::EXTRACT_VECTOR_ELT:
                          R = PromoteFloatRes_EXTRACT_VECTOR_ELT(N); break;
    case ISD::FCOPYSIGN:  R = PromoteFloatRes_FCOPYSIGN(N); break;

    // Unary FP Operations
    case ISD::FABS:
    case ISD::FACOS:
    case ISD::FASIN:
    case ISD::FATAN:
    case ISD::FCBRT:
    case ISD::FCEIL:
    case ISD::FCOS:
    case ISD::FCOSH:
    case ISD::FEXP:
    case ISD::FEXP2:
    case ISD::FEXP10:
    case ISD::FFLOOR:
    case ISD::FLOG:
    case ISD::FLOG2:
    case ISD::FLOG10:
    case ISD::FNEARBYINT:
    case ISD::FNEG:
    case ISD::FRINT:
    case ISD::FROUND:
    case ISD::FROUNDEVEN:
    case ISD::FSIN:
    case ISD::FSINH:
    case ISD::FSQRT:
    case ISD::FTRUNC:
    case ISD::FTAN:
    case ISD::FTANH:
    case ISD::FCANONICALIZE: R = PromoteFloatRes_UnaryOp(N); break;

    // Binary FP Operations
    case ISD::FADD:
    case ISD::FDIV:
    case ISD::FMAXIMUM:
    case ISD::FMINIMUM:
    case ISD::FMAXNUM:
    case ISD::FMINNUM:
    case ISD::FMAXNUM_IEEE:
    case ISD::FMINNUM_IEEE:
    case ISD::FMUL:
    case ISD::FPOW:
    case ISD::FREM:
    case ISD::FSUB:       R = PromoteFloatRes_BinOp(N); break;

    case ISD::FMA:        // FMA is same as FMAD
    case ISD::FMAD:       R = PromoteFloatRes_FMAD(N); break;

    case ISD::FPOWI:
    case ISD::FLDEXP:     R = PromoteFloatRes_ExpOp(N); break;
    case ISD::FFREXP:     R = PromoteFloatRes_FFREXP(N); break;

    case ISD::FP_ROUND:   R = PromoteFloatRes_FP_ROUND(N); break;
    case ISD::STRICT_FP_ROUND:
      R = PromoteFloatRes_STRICT_FP_ROUND(N);
      break;
    case ISD::LOAD:       R = PromoteFloatRes_LOAD(N); break;
    case ISD::ATOMIC_LOAD:
      R = PromoteFloatRes_ATOMIC_LOAD(N);
      break;
    case ISD::SELECT:     R = PromoteFloatRes_SELECT(N); break;
    case ISD::SELECT_CC:  R = PromoteFloatRes_SELECT_CC(N); break;

    case ISD::SINT_TO_FP:
    case ISD::UINT_TO_FP: R = PromoteFloatRes_XINT_TO_FP(N); break;
    case ISD::UNDEF:      R = PromoteFloatRes_UNDEF(N); break;
    case ISD::ATOMIC_SWAP: R = BitcastToInt_ATOMIC_SWAP(N); break;
    case ISD::VECREDUCE_FADD:
    case ISD::VECREDUCE_FMUL:
    case ISD::VECREDUCE_FMIN:
    case ISD::VECREDUCE_FMAX:
    case ISD::VECREDUCE_FMAXIMUM:
    case ISD::VECREDUCE_FMINIMUM:
      R = PromoteFloatRes_VECREDUCE(N);
      break;
    case ISD::VECREDUCE_SEQ_FADD:
    case ISD::VECREDUCE_SEQ_FMUL:
      R = PromoteFloatRes_VECREDUCE_SEQ(N);
      break;
  }

  if (R.getNode())
    SetPromotedFloat(SDValue(N, ResNo), R);
}

// Bitcast from i16 to f16:  convert the i16 to a f32 value instead.
// At this point, it is not possible to determine if the bitcast value is
// eventually stored to memory or promoted to f32 or promoted to a floating
// point at a higher precision.  Some of these cases are handled by FP_EXTEND,
// STORE promotion handlers.
SDValue DAGTypeLegalizer::PromoteFloatRes_BITCAST(SDNode *N) {
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  // Input type isn't guaranteed to be a scalar int so bitcast if not. The
  // bitcast will be legalized further if necessary.
  EVT IVT = EVT::getIntegerVT(*DAG.getContext(),
                              N->getOperand(0).getValueType().getSizeInBits());
  SDValue Cast = DAG.getBitcast(IVT, N->getOperand(0));
  return DAG.getNode(GetPromotionOpcode(VT, NVT), SDLoc(N), NVT, Cast);
}

SDValue DAGTypeLegalizer::PromoteFloatRes_ConstantFP(SDNode *N) {
  ConstantFPSDNode *CFPNode = cast<ConstantFPSDNode>(N);
  EVT VT = N->getValueType(0);
  SDLoc DL(N);

  // Get the (bit-cast) APInt of the APFloat and build an integer constant
  EVT IVT = EVT::getIntegerVT(*DAG.getContext(), VT.getSizeInBits());
  SDValue C = DAG.getConstant(CFPNode->getValueAPF().bitcastToAPInt(), DL,
                              IVT);

  // Convert the Constant to the desired FP type
  // FIXME We might be able to do the conversion during compilation and get rid
  // of it from the object code
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  return DAG.getNode(GetPromotionOpcode(VT, NVT), DL, NVT, C);
}

// If the Index operand is a constant, try to redirect the extract operation to
// the correct legalized vector.  If not, bit-convert the input vector to
// equivalent integer vector.  Extract the element as an (bit-cast) integer
// value and convert it to the promoted type.
SDValue DAGTypeLegalizer::PromoteFloatRes_EXTRACT_VECTOR_ELT(SDNode *N) {
  SDLoc DL(N);

  // If the index is constant, try to extract the value from the legalized
  // vector type.
  if (isa<ConstantSDNode>(N->getOperand(1))) {
    SDValue Vec = N->getOperand(0);
    SDValue Idx = N->getOperand(1);
    EVT VecVT = Vec->getValueType(0);
    EVT EltVT = VecVT.getVectorElementType();

    uint64_t IdxVal = Idx->getAsZExtVal();

    switch (getTypeAction(VecVT)) {
    default: break;
    case TargetLowering::TypeScalarizeVector: {
      SDValue Res = GetScalarizedVector(N->getOperand(0));
      ReplaceValueWith(SDValue(N, 0), Res);
      return SDValue();
    }
    case TargetLowering::TypeWidenVector: {
      Vec = GetWidenedVector(Vec);
      SDValue Res = DAG.getNode(N->getOpcode(), DL, EltVT, Vec, Idx);
      ReplaceValueWith(SDValue(N, 0), Res);
      return SDValue();
    }
    case TargetLowering::TypeSplitVector: {
      SDValue Lo, Hi;
      GetSplitVector(Vec, Lo, Hi);

      uint64_t LoElts = Lo.getValueType().getVectorNumElements();
      SDValue Res;
      if (IdxVal < LoElts)
        Res = DAG.getNode(N->getOpcode(), DL, EltVT, Lo, Idx);
      else
        Res = DAG.getNode(N->getOpcode(), DL, EltVT, Hi,
                          DAG.getConstant(IdxVal - LoElts, DL,
                                          Idx.getValueType()));
      ReplaceValueWith(SDValue(N, 0), Res);
      return SDValue();
    }

    }
  }

  // Bit-convert the input vector to the equivalent integer vector
  SDValue NewOp = BitConvertVectorToIntegerVector(N->getOperand(0));
  EVT IVT = NewOp.getValueType().getVectorElementType();

  // Extract the element as an (bit-cast) integer value
  SDValue NewVal = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, IVT,
                               NewOp, N->getOperand(1));

  // Convert the element to the desired FP type
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  return DAG.getNode(GetPromotionOpcode(VT, NVT), SDLoc(N), NVT, NewVal);
}

// FCOPYSIGN(X, Y) returns the value of X with the sign of Y.  If the result
// needs promotion, so does the argument X.  Note that Y, if needed, will be
// handled during operand promotion.
SDValue DAGTypeLegalizer::PromoteFloatRes_FCOPYSIGN(SDNode *N) {
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue Op0 = GetPromotedFloat(N->getOperand(0));

  SDValue Op1 = N->getOperand(1);

  return DAG.getNode(N->getOpcode(), SDLoc(N), NVT, Op0, Op1);
}

// Unary operation where the result and the operand have PromoteFloat type
// action.  Construct a new SDNode with the promoted float value of the old
// operand.
SDValue DAGTypeLegalizer::PromoteFloatRes_UnaryOp(SDNode *N) {
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue Op = GetPromotedFloat(N->getOperand(0));

  return DAG.getNode(N->getOpcode(), SDLoc(N), NVT, Op);
}

// Binary operations where the result and both operands have PromoteFloat type
// action.  Construct a new SDNode with the promoted float values of the old
// operands.
SDValue DAGTypeLegalizer::PromoteFloatRes_BinOp(SDNode *N) {
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue Op0 = GetPromotedFloat(N->getOperand(0));
  SDValue Op1 = GetPromotedFloat(N->getOperand(1));
  return DAG.getNode(N->getOpcode(), SDLoc(N), NVT, Op0, Op1, N->getFlags());
}

SDValue DAGTypeLegalizer::PromoteFloatRes_FMAD(SDNode *N) {
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue Op0 = GetPromotedFloat(N->getOperand(0));
  SDValue Op1 = GetPromotedFloat(N->getOperand(1));
  SDValue Op2 = GetPromotedFloat(N->getOperand(2));

  return DAG.getNode(N->getOpcode(), SDLoc(N), NVT, Op0, Op1, Op2);
}

// Promote the Float (first) operand and retain the Integer (second) operand
SDValue DAGTypeLegalizer::PromoteFloatRes_ExpOp(SDNode *N) {
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue Op0 = GetPromotedFloat(N->getOperand(0));
  SDValue Op1 = N->getOperand(1);

  return DAG.getNode(N->getOpcode(), SDLoc(N), NVT, Op0, Op1);
}

SDValue DAGTypeLegalizer::PromoteFloatRes_FFREXP(SDNode *N) {
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue Op = GetPromotedFloat(N->getOperand(0));
  SDValue Res =
      DAG.getNode(N->getOpcode(), SDLoc(N), {NVT, N->getValueType(1)}, Op);

  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  return Res;
}

// Explicit operation to reduce precision.  Reduce the value to half precision
// and promote it back to the legal type.
SDValue DAGTypeLegalizer::PromoteFloatRes_FP_ROUND(SDNode *N) {
  SDLoc DL(N);

  SDValue Op = N->getOperand(0);
  EVT VT = N->getValueType(0);
  EVT OpVT = Op->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  EVT IVT = EVT::getIntegerVT(*DAG.getContext(), VT.getSizeInBits());

  // Round promoted float to desired precision
  SDValue Round = DAG.getNode(GetPromotionOpcode(OpVT, VT), DL, IVT, Op);
  // Promote it back to the legal output type
  return DAG.getNode(GetPromotionOpcode(VT, NVT), DL, NVT, Round);
}

// Explicit operation to reduce precision.  Reduce the value to half precision
// and promote it back to the legal type.
SDValue DAGTypeLegalizer::PromoteFloatRes_STRICT_FP_ROUND(SDNode *N) {
  SDLoc DL(N);

  SDValue Chain = N->getOperand(0);
  SDValue Op = N->getOperand(1);
  EVT VT = N->getValueType(0);
  EVT OpVT = Op->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  EVT IVT = EVT::getIntegerVT(*DAG.getContext(), VT.getSizeInBits());

  // Round promoted float to desired precision
  SDValue Round = DAG.getNode(GetPromotionOpcodeStrict(OpVT, VT), DL,
                              DAG.getVTList(IVT, MVT::Other), Chain, Op);
  // Promote it back to the legal output type
  SDValue Res =
      DAG.getNode(GetPromotionOpcodeStrict(VT, NVT), DL,
                  DAG.getVTList(NVT, MVT::Other), Round.getValue(1), Round);
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  return Res;
}

SDValue DAGTypeLegalizer::PromoteFloatRes_LOAD(SDNode *N) {
  LoadSDNode *L = cast<LoadSDNode>(N);
  EVT VT = N->getValueType(0);

  // Load the value as an integer value with the same number of bits.
  EVT IVT = EVT::getIntegerVT(*DAG.getContext(), VT.getSizeInBits());
  SDValue newL = DAG.getLoad(
      L->getAddressingMode(), L->getExtensionType(), IVT, SDLoc(N),
      L->getChain(), L->getBasePtr(), L->getOffset(), L->getPointerInfo(), IVT,
      L->getOriginalAlign(), L->getMemOperand()->getFlags(), L->getAAInfo());
  // Legalize the chain result by replacing uses of the old value chain with the
  // new one
  ReplaceValueWith(SDValue(N, 1), newL.getValue(1));

  // Convert the integer value to the desired FP type
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  return DAG.getNode(GetPromotionOpcode(VT, NVT), SDLoc(N), NVT, newL);
}

SDValue DAGTypeLegalizer::PromoteFloatRes_ATOMIC_LOAD(SDNode *N) {
  AtomicSDNode *AM = cast<AtomicSDNode>(N);
  EVT VT = AM->getValueType(0);

  // Load the value as an integer value with the same number of bits.
  EVT IVT = EVT::getIntegerVT(*DAG.getContext(), VT.getSizeInBits());
  SDValue newL = DAG.getAtomic(
      ISD::ATOMIC_LOAD, SDLoc(N), IVT, DAG.getVTList(IVT, MVT::Other),
      {AM->getChain(), AM->getBasePtr()}, AM->getMemOperand());

  // Legalize the chain result by replacing uses of the old value chain with the
  // new one
  ReplaceValueWith(SDValue(N, 1), newL.getValue(1));

  // Convert the integer value to the desired FP type
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  return DAG.getNode(GetPromotionOpcode(VT, IVT), SDLoc(N), NVT, newL);
}

// Construct a new SELECT node with the promoted true- and false- values.
SDValue DAGTypeLegalizer::PromoteFloatRes_SELECT(SDNode *N) {
  SDValue TrueVal = GetPromotedFloat(N->getOperand(1));
  SDValue FalseVal = GetPromotedFloat(N->getOperand(2));

  return DAG.getNode(ISD::SELECT, SDLoc(N), TrueVal->getValueType(0),
                     N->getOperand(0), TrueVal, FalseVal);
}

// Construct a new SELECT_CC node with the promoted true- and false- values.
// The operands used for comparison are promoted by PromoteFloatOp_SELECT_CC.
SDValue DAGTypeLegalizer::PromoteFloatRes_SELECT_CC(SDNode *N) {
  SDValue TrueVal = GetPromotedFloat(N->getOperand(2));
  SDValue FalseVal = GetPromotedFloat(N->getOperand(3));

  return DAG.getNode(ISD::SELECT_CC, SDLoc(N),
                     TrueVal.getNode()->getValueType(0), N->getOperand(0),
                     N->getOperand(1), TrueVal, FalseVal, N->getOperand(4));
}

// Construct a SDNode that transforms the SINT or UINT operand to the promoted
// float type.
SDValue DAGTypeLegalizer::PromoteFloatRes_XINT_TO_FP(SDNode *N) {
  SDLoc DL(N);
  EVT VT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue NV = DAG.getNode(N->getOpcode(), DL, NVT, N->getOperand(0));
  // Round the value to the desired precision (that of the source type).
  return DAG.getNode(
      ISD::FP_EXTEND, DL, NVT,
      DAG.getNode(ISD::FP_ROUND, DL, VT, NV,
                  DAG.getIntPtrConstant(0, DL, /*isTarget=*/true)));
}

SDValue DAGTypeLegalizer::PromoteFloatRes_UNDEF(SDNode *N) {
  return DAG.getUNDEF(TLI.getTypeToTransformTo(*DAG.getContext(),
                                               N->getValueType(0)));
}

SDValue DAGTypeLegalizer::PromoteFloatRes_VECREDUCE(SDNode *N) {
  // Expand and promote recursively.
  // TODO: This is non-optimal, but dealing with the concurrently happening
  // vector-legalization is non-trivial. We could do something similar to
  // PromoteFloatRes_EXTRACT_VECTOR_ELT here.
  ReplaceValueWith(SDValue(N, 0), TLI.expandVecReduce(N, DAG));
  return SDValue();
}

SDValue DAGTypeLegalizer::PromoteFloatRes_VECREDUCE_SEQ(SDNode *N) {
  ReplaceValueWith(SDValue(N, 0), TLI.expandVecReduceSeq(N, DAG));
  return SDValue();
}

SDValue DAGTypeLegalizer::BitcastToInt_ATOMIC_SWAP(SDNode *N) {
  EVT VT = N->getValueType(0);

  AtomicSDNode *AM = cast<AtomicSDNode>(N);
  SDLoc SL(N);

  SDValue CastVal = BitConvertToInteger(AM->getVal());
  EVT CastVT = CastVal.getValueType();

  SDValue NewAtomic
    = DAG.getAtomic(ISD::ATOMIC_SWAP, SL, CastVT,
                    DAG.getVTList(CastVT, MVT::Other),
                    { AM->getChain(), AM->getBasePtr(), CastVal },
                    AM->getMemOperand());

  SDValue Result = NewAtomic;

  if (getTypeAction(VT) == TargetLowering::TypePromoteFloat) {
    EVT NFPVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
    Result = DAG.getNode(GetPromotionOpcode(VT, NFPVT), SL, NFPVT,
                                     NewAtomic);
  }

  // Legalize the chain result by replacing uses of the old value chain with the
  // new one
  ReplaceValueWith(SDValue(N, 1), NewAtomic.getValue(1));

  return Result;

}

//===----------------------------------------------------------------------===//
//  Half Result Soft Promotion
//===----------------------------------------------------------------------===//

void DAGTypeLegalizer::SoftPromoteHalfResult(SDNode *N, unsigned ResNo) {
  LLVM_DEBUG(dbgs() << "Soft promote half result " << ResNo << ": ";
             N->dump(&DAG));
  SDValue R = SDValue();

  // See if the target wants to custom expand this node.
  if (CustomLowerNode(N, N->getValueType(ResNo), true)) {
    LLVM_DEBUG(dbgs() << "Node has been custom expanded, done\n");
    return;
  }

  switch (N->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "SoftPromoteHalfResult #" << ResNo << ": ";
    N->dump(&DAG); dbgs() << "\n";
#endif
    report_fatal_error("Do not know how to soft promote this operator's "
                       "result!");

  case ISD::ARITH_FENCE:
    R = SoftPromoteHalfRes_ARITH_FENCE(N); break;
  case ISD::BITCAST:    R = SoftPromoteHalfRes_BITCAST(N); break;
  case ISD::ConstantFP: R = SoftPromoteHalfRes_ConstantFP(N); break;
  case ISD::EXTRACT_VECTOR_ELT:
    R = SoftPromoteHalfRes_EXTRACT_VECTOR_ELT(N); break;
  case ISD::FCOPYSIGN:  R = SoftPromoteHalfRes_FCOPYSIGN(N); break;
  case ISD::STRICT_FP_ROUND:
  case ISD::FP_ROUND:   R = SoftPromoteHalfRes_FP_ROUND(N); break;

  // Unary FP Operations
  case ISD::FABS:
  case ISD::FACOS:
  case ISD::FASIN:
  case ISD::FATAN:
  case ISD::FCBRT:
  case ISD::FCEIL:
  case ISD::FCOS:
  case ISD::FCOSH:
  case ISD::FEXP:
  case ISD::FEXP2:
  case ISD::FEXP10:
  case ISD::FFLOOR:
  case ISD::FLOG:
  case ISD::FLOG2:
  case ISD::FLOG10:
  case ISD::FNEARBYINT:
  case ISD::FNEG:
  case ISD::FREEZE:
  case ISD::FRINT:
  case ISD::FROUND:
  case ISD::FROUNDEVEN:
  case ISD::FSIN:
  case ISD::FSINH:
  case ISD::FSQRT:
  case ISD::FTRUNC:
  case ISD::FTAN:
  case ISD::FTANH:
  case ISD::FCANONICALIZE: R = SoftPromoteHalfRes_UnaryOp(N); break;

  // Binary FP Operations
  case ISD::FADD:
  case ISD::FDIV:
  case ISD::FMAXIMUM:
  case ISD::FMINIMUM:
  case ISD::FMAXNUM:
  case ISD::FMINNUM:
  case ISD::FMUL:
  case ISD::FPOW:
  case ISD::FREM:
  case ISD::FSUB:        R = SoftPromoteHalfRes_BinOp(N); break;

  case ISD::FMA:         // FMA is same as FMAD
  case ISD::FMAD:        R = SoftPromoteHalfRes_FMAD(N); break;

  case ISD::FPOWI:
  case ISD::FLDEXP:      R = SoftPromoteHalfRes_ExpOp(N); break;

  case ISD::FFREXP:      R = SoftPromoteHalfRes_FFREXP(N); break;

  case ISD::LOAD:        R = SoftPromoteHalfRes_LOAD(N); break;
  case ISD::ATOMIC_LOAD:
    R = SoftPromoteHalfRes_ATOMIC_LOAD(N);
    break;
  case ISD::SELECT:      R = SoftPromoteHalfRes_SELECT(N); break;
  case ISD::SELECT_CC:   R = SoftPromoteHalfRes_SELECT_CC(N); break;
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:  R = SoftPromoteHalfRes_XINT_TO_FP(N); break;
  case ISD::UNDEF:       R = SoftPromoteHalfRes_UNDEF(N); break;
  case ISD::ATOMIC_SWAP: R = BitcastToInt_ATOMIC_SWAP(N); break;
  case ISD::VECREDUCE_FADD:
  case ISD::VECREDUCE_FMUL:
  case ISD::VECREDUCE_FMIN:
  case ISD::VECREDUCE_FMAX:
  case ISD::VECREDUCE_FMAXIMUM:
  case ISD::VECREDUCE_FMINIMUM:
    R = SoftPromoteHalfRes_VECREDUCE(N);
    break;
  case ISD::VECREDUCE_SEQ_FADD:
  case ISD::VECREDUCE_SEQ_FMUL:
    R = SoftPromoteHalfRes_VECREDUCE_SEQ(N);
    break;
  }

  if (R.getNode())
    SetSoftPromotedHalf(SDValue(N, ResNo), R);
}

SDValue DAGTypeLegalizer::SoftPromoteHalfRes_ARITH_FENCE(SDNode *N) {
  return DAG.getNode(ISD::ARITH_FENCE, SDLoc(N), MVT::i16,
                     BitConvertToInteger(N->getOperand(0)));
}

SDValue DAGTypeLegalizer::SoftPromoteHalfRes_BITCAST(SDNode *N) {
  return BitConvertToInteger(N->getOperand(0));
}

SDValue DAGTypeLegalizer::SoftPromoteHalfRes_ConstantFP(SDNode *N) {
  ConstantFPSDNode *CN = cast<ConstantFPSDNode>(N);

  // Get the (bit-cast) APInt of the APFloat and build an integer constant
  return DAG.getConstant(CN->getValueAPF().bitcastToAPInt(), SDLoc(CN),
                         MVT::i16);
}

SDValue DAGTypeLegalizer::SoftPromoteHalfRes_EXTRACT_VECTOR_ELT(SDNode *N) {
  SDValue NewOp = BitConvertVectorToIntegerVector(N->getOperand(0));
  return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SDLoc(N),
                     NewOp.getValueType().getVectorElementType(), NewOp,
                     N->getOperand(1));
}

SDValue DAGTypeLegalizer::SoftPromoteHalfRes_FCOPYSIGN(SDNode *N) {
  SDValue LHS = GetSoftPromotedHalf(N->getOperand(0));
  SDValue RHS = BitConvertToInteger(N->getOperand(1));
  SDLoc dl(N);

  EVT LVT = LHS.getValueType();
  EVT RVT = RHS.getValueType();

  unsigned LSize = LVT.getSizeInBits();
  unsigned RSize = RVT.getSizeInBits();

  // First get the sign bit of second operand.
  SDValue SignBit = DAG.getNode(
      ISD::SHL, dl, RVT, DAG.getConstant(1, dl, RVT),
      DAG.getConstant(RSize - 1, dl,
                      TLI.getShiftAmountTy(RVT, DAG.getDataLayout())));
  SignBit = DAG.getNode(ISD::AND, dl, RVT, RHS, SignBit);

  // Shift right or sign-extend it if the two operands have different types.
  int SizeDiff = RVT.getSizeInBits() - LVT.getSizeInBits();
  if (SizeDiff > 0) {
    SignBit =
        DAG.getNode(ISD::SRL, dl, RVT, SignBit,
                    DAG.getConstant(SizeDiff, dl,
                                    TLI.getShiftAmountTy(SignBit.getValueType(),
                                                         DAG.getDataLayout())));
    SignBit = DAG.getNode(ISD::TRUNCATE, dl, LVT, SignBit);
  } else if (SizeDiff < 0) {
    SignBit = DAG.getNode(ISD::ANY_EXTEND, dl, LVT, SignBit);
    SignBit =
        DAG.getNode(ISD::SHL, dl, LVT, SignBit,
                    DAG.getConstant(-SizeDiff, dl,
                                    TLI.getShiftAmountTy(SignBit.getValueType(),
                                                         DAG.getDataLayout())));
  }

  // Clear the sign bit of the first operand.
  SDValue Mask = DAG.getNode(
      ISD::SHL, dl, LVT, DAG.getConstant(1, dl, LVT),
      DAG.getConstant(LSize - 1, dl,
                      TLI.getShiftAmountTy(LVT, DAG.getDataLayout())));
  Mask = DAG.getNode(ISD::SUB, dl, LVT, Mask, DAG.getConstant(1, dl, LVT));
  LHS = DAG.getNode(ISD::AND, dl, LVT, LHS, Mask);

  // Or the value with the sign bit.
  return DAG.getNode(ISD::OR, dl, LVT, LHS, SignBit);
}

SDValue DAGTypeLegalizer::SoftPromoteHalfRes_FMAD(SDNode *N) {
  EVT OVT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), OVT);
  SDValue Op0 = GetSoftPromotedHalf(N->getOperand(0));
  SDValue Op1 = GetSoftPromotedHalf(N->getOperand(1));
  SDValue Op2 = GetSoftPromotedHalf(N->getOperand(2));
  SDLoc dl(N);

  // Promote to the larger FP type.
  auto PromotionOpcode = GetPromotionOpcode(OVT, NVT);
  Op0 = DAG.getNode(PromotionOpcode, dl, NVT, Op0);
  Op1 = DAG.getNode(PromotionOpcode, dl, NVT, Op1);
  Op2 = DAG.getNode(PromotionOpcode, dl, NVT, Op2);

  SDValue Res = DAG.getNode(N->getOpcode(), dl, NVT, Op0, Op1, Op2);

  // Convert back to FP16 as an integer.
  return DAG.getNode(GetPromotionOpcode(NVT, OVT), dl, MVT::i16, Res);
}

SDValue DAGTypeLegalizer::SoftPromoteHalfRes_ExpOp(SDNode *N) {
  EVT OVT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), OVT);
  SDValue Op0 = GetSoftPromotedHalf(N->getOperand(0));
  SDValue Op1 = N->getOperand(1);
  SDLoc dl(N);

  // Promote to the larger FP type.
  Op0 = DAG.getNode(GetPromotionOpcode(OVT, NVT), dl, NVT, Op0);

  SDValue Res = DAG.getNode(N->getOpcode(), dl, NVT, Op0, Op1);

  // Convert back to FP16 as an integer.
  return DAG.getNode(GetPromotionOpcode(NVT, OVT), dl, MVT::i16, Res);
}

SDValue DAGTypeLegalizer::SoftPromoteHalfRes_FFREXP(SDNode *N) {
  EVT OVT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), OVT);
  SDValue Op = GetSoftPromotedHalf(N->getOperand(0));
  SDLoc dl(N);

  // Promote to the larger FP type.
  Op = DAG.getNode(GetPromotionOpcode(OVT, NVT), dl, NVT, Op);

  SDValue Res = DAG.getNode(N->getOpcode(), dl,
                            DAG.getVTList(NVT, N->getValueType(1)), Op);

  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));

  // Convert back to FP16 as an integer.
  return DAG.getNode(GetPromotionOpcode(NVT, OVT), dl, MVT::i16, Res);
}

SDValue DAGTypeLegalizer::SoftPromoteHalfRes_FP_ROUND(SDNode *N) {
  EVT RVT = N->getValueType(0);
  EVT SVT = N->getOperand(0).getValueType();

  if (N->isStrictFPOpcode()) {
    // FIXME: assume we only have two f16 variants for now.
    unsigned Opcode;
    if (RVT == MVT::f16)
      Opcode = ISD::STRICT_FP_TO_FP16;
    else if (RVT == MVT::bf16)
      Opcode = ISD::STRICT_FP_TO_BF16;
    else
      llvm_unreachable("unknown half type");
    SDValue Res = DAG.getNode(Opcode, SDLoc(N), {MVT::i16, MVT::Other},
                              {N->getOperand(0), N->getOperand(1)});
    ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
    return Res;
  }

  return DAG.getNode(GetPromotionOpcode(SVT, RVT), SDLoc(N), MVT::i16,
                     N->getOperand(0));
}

SDValue DAGTypeLegalizer::SoftPromoteHalfRes_LOAD(SDNode *N) {
  LoadSDNode *L = cast<LoadSDNode>(N);

  // Load the value as an integer value with the same number of bits.
  assert(L->getExtensionType() == ISD::NON_EXTLOAD && "Unexpected extension!");
  SDValue NewL =
      DAG.getLoad(L->getAddressingMode(), L->getExtensionType(), MVT::i16,
                  SDLoc(N), L->getChain(), L->getBasePtr(), L->getOffset(),
                  L->getPointerInfo(), MVT::i16, L->getOriginalAlign(),
                  L->getMemOperand()->getFlags(), L->getAAInfo());
  // Legalize the chain result by replacing uses of the old value chain with the
  // new one
  ReplaceValueWith(SDValue(N, 1), NewL.getValue(1));
  return NewL;
}

SDValue DAGTypeLegalizer::SoftPromoteHalfRes_ATOMIC_LOAD(SDNode *N) {
  AtomicSDNode *AM = cast<AtomicSDNode>(N);

  // Load the value as an integer value with the same number of bits.
  SDValue NewL = DAG.getAtomic(
      ISD::ATOMIC_LOAD, SDLoc(N), MVT::i16, DAG.getVTList(MVT::i16, MVT::Other),
      {AM->getChain(), AM->getBasePtr()}, AM->getMemOperand());

  // Legalize the chain result by replacing uses of the old value chain with the
  // new one
  ReplaceValueWith(SDValue(N, 1), NewL.getValue(1));
  return NewL;
}

SDValue DAGTypeLegalizer::SoftPromoteHalfRes_SELECT(SDNode *N) {
  SDValue Op1 = GetSoftPromotedHalf(N->getOperand(1));
  SDValue Op2 = GetSoftPromotedHalf(N->getOperand(2));
  return DAG.getSelect(SDLoc(N), Op1.getValueType(), N->getOperand(0), Op1,
                       Op2);
}

SDValue DAGTypeLegalizer::SoftPromoteHalfRes_SELECT_CC(SDNode *N) {
  SDValue Op2 = GetSoftPromotedHalf(N->getOperand(2));
  SDValue Op3 = GetSoftPromotedHalf(N->getOperand(3));
  return DAG.getNode(ISD::SELECT_CC, SDLoc(N), Op2.getValueType(),
                     N->getOperand(0), N->getOperand(1), Op2, Op3,
                     N->getOperand(4));
}

SDValue DAGTypeLegalizer::SoftPromoteHalfRes_XINT_TO_FP(SDNode *N) {
  EVT OVT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), OVT);
  SDLoc dl(N);

  SDValue Res = DAG.getNode(N->getOpcode(), dl, NVT, N->getOperand(0));

  // Round the value to the softened type.
  return DAG.getNode(GetPromotionOpcode(NVT, OVT), dl, MVT::i16, Res);
}

SDValue DAGTypeLegalizer::SoftPromoteHalfRes_UNDEF(SDNode *N) {
  return DAG.getUNDEF(MVT::i16);
}

SDValue DAGTypeLegalizer::SoftPromoteHalfRes_UnaryOp(SDNode *N) {
  EVT OVT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), OVT);
  SDValue Op = GetSoftPromotedHalf(N->getOperand(0));
  SDLoc dl(N);

  // Promote to the larger FP type.
  Op = DAG.getNode(GetPromotionOpcode(OVT, NVT), dl, NVT, Op);

  SDValue Res = DAG.getNode(N->getOpcode(), dl, NVT, Op);

  // Convert back to FP16 as an integer.
  return DAG.getNode(GetPromotionOpcode(NVT, OVT), dl, MVT::i16, Res);
}

SDValue DAGTypeLegalizer::SoftPromoteHalfRes_BinOp(SDNode *N) {
  EVT OVT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), OVT);
  SDValue Op0 = GetSoftPromotedHalf(N->getOperand(0));
  SDValue Op1 = GetSoftPromotedHalf(N->getOperand(1));
  SDLoc dl(N);

  // Promote to the larger FP type.
  auto PromotionOpcode = GetPromotionOpcode(OVT, NVT);
  Op0 = DAG.getNode(PromotionOpcode, dl, NVT, Op0);
  Op1 = DAG.getNode(PromotionOpcode, dl, NVT, Op1);

  SDValue Res = DAG.getNode(N->getOpcode(), dl, NVT, Op0, Op1);

  // Convert back to FP16 as an integer.
  return DAG.getNode(GetPromotionOpcode(NVT, OVT), dl, MVT::i16, Res);
}

SDValue DAGTypeLegalizer::SoftPromoteHalfRes_VECREDUCE(SDNode *N) {
  // Expand and soften recursively.
  ReplaceValueWith(SDValue(N, 0), TLI.expandVecReduce(N, DAG));
  return SDValue();
}

SDValue DAGTypeLegalizer::SoftPromoteHalfRes_VECREDUCE_SEQ(SDNode *N) {
  // Expand and soften.
  ReplaceValueWith(SDValue(N, 0), TLI.expandVecReduceSeq(N, DAG));
  return SDValue();
}

//===----------------------------------------------------------------------===//
//  Half Operand Soft Promotion
//===----------------------------------------------------------------------===//

bool DAGTypeLegalizer::SoftPromoteHalfOperand(SDNode *N, unsigned OpNo) {
  LLVM_DEBUG(dbgs() << "Soft promote half operand " << OpNo << ": ";
             N->dump(&DAG));
  SDValue Res = SDValue();

  if (CustomLowerNode(N, N->getOperand(OpNo).getValueType(), false)) {
    LLVM_DEBUG(dbgs() << "Node has been custom lowered, done\n");
    return false;
  }

  // Nodes that use a promotion-requiring floating point operand, but doesn't
  // produce a soft promotion-requiring floating point result, need to be
  // legalized to use the soft promoted float operand.  Nodes that produce at
  // least one soft promotion-requiring floating point result have their
  // operands legalized as a part of PromoteFloatResult.
  switch (N->getOpcode()) {
  default:
  #ifndef NDEBUG
    dbgs() << "SoftPromoteHalfOperand Op #" << OpNo << ": ";
    N->dump(&DAG); dbgs() << "\n";
  #endif
    report_fatal_error("Do not know how to soft promote this operator's "
                       "operand!");

  case ISD::BITCAST:    Res = SoftPromoteHalfOp_BITCAST(N); break;
  case ISD::FCOPYSIGN:  Res = SoftPromoteHalfOp_FCOPYSIGN(N, OpNo); break;
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT: Res = SoftPromoteHalfOp_FP_TO_XINT(N); break;
  case ISD::FP_TO_SINT_SAT:
  case ISD::FP_TO_UINT_SAT:
                        Res = SoftPromoteHalfOp_FP_TO_XINT_SAT(N); break;
  case ISD::STRICT_FP_EXTEND:
  case ISD::FP_EXTEND:  Res = SoftPromoteHalfOp_FP_EXTEND(N); break;
  case ISD::SELECT_CC:  Res = SoftPromoteHalfOp_SELECT_CC(N, OpNo); break;
  case ISD::SETCC:      Res = SoftPromoteHalfOp_SETCC(N); break;
  case ISD::STORE:      Res = SoftPromoteHalfOp_STORE(N, OpNo); break;
  case ISD::ATOMIC_STORE:
    Res = SoftPromoteHalfOp_ATOMIC_STORE(N, OpNo);
    break;
  case ISD::STACKMAP:
    Res = SoftPromoteHalfOp_STACKMAP(N, OpNo);
    break;
  case ISD::PATCHPOINT:
    Res = SoftPromoteHalfOp_PATCHPOINT(N, OpNo);
    break;
  }

  if (!Res.getNode())
    return false;

  assert(Res.getNode() != N && "Expected a new node!");

  assert(Res.getValueType() == N->getValueType(0) && N->getNumValues() == 1 &&
         "Invalid operand expansion");

  ReplaceValueWith(SDValue(N, 0), Res);
  return false;
}

SDValue DAGTypeLegalizer::SoftPromoteHalfOp_BITCAST(SDNode *N) {
  SDValue Op0 = GetSoftPromotedHalf(N->getOperand(0));

  return DAG.getNode(ISD::BITCAST, SDLoc(N), N->getValueType(0), Op0);
}

SDValue DAGTypeLegalizer::SoftPromoteHalfOp_FCOPYSIGN(SDNode *N,
                                                      unsigned OpNo) {
  assert(OpNo == 1 && "Only Operand 1 must need promotion here");
  SDValue Op1 = N->getOperand(1);
  EVT RVT = Op1.getValueType();
  SDLoc dl(N);

  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), Op1.getValueType());

  Op1 = GetSoftPromotedHalf(Op1);
  Op1 = DAG.getNode(GetPromotionOpcode(RVT, NVT), dl, NVT, Op1);

  return DAG.getNode(N->getOpcode(), dl, N->getValueType(0), N->getOperand(0),
                     Op1);
}

SDValue DAGTypeLegalizer::SoftPromoteHalfOp_FP_EXTEND(SDNode *N) {
  EVT RVT = N->getValueType(0);
  bool IsStrict = N->isStrictFPOpcode();
  SDValue Op = N->getOperand(IsStrict ? 1 : 0);
  EVT SVT = Op.getValueType();
  Op = GetSoftPromotedHalf(N->getOperand(IsStrict ? 1 : 0));

  if (IsStrict) {
    unsigned Opcode;
    if (SVT == MVT::f16)
      Opcode = ISD::STRICT_FP16_TO_FP;
    else if (SVT == MVT::bf16)
      Opcode = ISD::STRICT_BF16_TO_FP;
    else
      llvm_unreachable("unknown half type");
    SDValue Res =
        DAG.getNode(Opcode, SDLoc(N), {N->getValueType(0), MVT::Other},
                    {N->getOperand(0), Op});
    ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
    ReplaceValueWith(SDValue(N, 0), Res);
    return SDValue();
  }

  return DAG.getNode(GetPromotionOpcode(SVT, RVT), SDLoc(N), RVT, Op);
}

SDValue DAGTypeLegalizer::SoftPromoteHalfOp_FP_TO_XINT(SDNode *N) {
  EVT RVT = N->getValueType(0);
  SDValue Op = N->getOperand(0);
  EVT SVT = Op.getValueType();
  SDLoc dl(N);

  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), Op.getValueType());

  Op = GetSoftPromotedHalf(Op);

  SDValue Res = DAG.getNode(GetPromotionOpcode(SVT, RVT), dl, NVT, Op);

  return DAG.getNode(N->getOpcode(), dl, N->getValueType(0), Res);
}

SDValue DAGTypeLegalizer::SoftPromoteHalfOp_FP_TO_XINT_SAT(SDNode *N) {
  EVT RVT = N->getValueType(0);
  SDValue Op = N->getOperand(0);
  EVT SVT = Op.getValueType();
  SDLoc dl(N);

  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), Op.getValueType());

  Op = GetSoftPromotedHalf(Op);

  SDValue Res = DAG.getNode(GetPromotionOpcode(SVT, RVT), dl, NVT, Op);

  return DAG.getNode(N->getOpcode(), dl, N->getValueType(0), Res,
                     N->getOperand(1));
}

SDValue DAGTypeLegalizer::SoftPromoteHalfOp_SELECT_CC(SDNode *N,
                                                      unsigned OpNo) {
  assert(OpNo == 0 && "Can only soften the comparison values");
  SDValue Op0 = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);
  SDLoc dl(N);

  EVT SVT = Op0.getValueType();
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), SVT);

  Op0 = GetSoftPromotedHalf(Op0);
  Op1 = GetSoftPromotedHalf(Op1);

  // Promote to the larger FP type.
  auto PromotionOpcode = GetPromotionOpcode(SVT, NVT);
  Op0 = DAG.getNode(PromotionOpcode, dl, NVT, Op0);
  Op1 = DAG.getNode(PromotionOpcode, dl, NVT, Op1);

  return DAG.getNode(ISD::SELECT_CC, SDLoc(N), N->getValueType(0), Op0, Op1,
                     N->getOperand(2), N->getOperand(3), N->getOperand(4));
}

SDValue DAGTypeLegalizer::SoftPromoteHalfOp_SETCC(SDNode *N) {
  SDValue Op0 = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);
  ISD::CondCode CCCode = cast<CondCodeSDNode>(N->getOperand(2))->get();
  SDLoc dl(N);

  EVT SVT = Op0.getValueType();
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), Op0.getValueType());

  Op0 = GetSoftPromotedHalf(Op0);
  Op1 = GetSoftPromotedHalf(Op1);

  // Promote to the larger FP type.
  auto PromotionOpcode = GetPromotionOpcode(SVT, NVT);
  Op0 = DAG.getNode(PromotionOpcode, dl, NVT, Op0);
  Op1 = DAG.getNode(PromotionOpcode, dl, NVT, Op1);

  return DAG.getSetCC(SDLoc(N), N->getValueType(0), Op0, Op1, CCCode);
}

SDValue DAGTypeLegalizer::SoftPromoteHalfOp_STORE(SDNode *N, unsigned OpNo) {
  assert(OpNo == 1 && "Can only soften the stored value!");
  StoreSDNode *ST = cast<StoreSDNode>(N);
  SDValue Val = ST->getValue();
  SDLoc dl(N);

  assert(!ST->isTruncatingStore() && "Unexpected truncating store.");
  SDValue Promoted = GetSoftPromotedHalf(Val);
  return DAG.getStore(ST->getChain(), dl, Promoted, ST->getBasePtr(),
                      ST->getMemOperand());
}

SDValue DAGTypeLegalizer::SoftPromoteHalfOp_ATOMIC_STORE(SDNode *N,
                                                         unsigned OpNo) {
  assert(OpNo == 1 && "Can only soften the stored value!");
  AtomicSDNode *ST = cast<AtomicSDNode>(N);
  SDValue Val = ST->getVal();
  SDLoc dl(N);

  SDValue Promoted = GetSoftPromotedHalf(Val);
  return DAG.getAtomic(ISD::ATOMIC_STORE, dl, Promoted.getValueType(),
                       ST->getChain(), Promoted, ST->getBasePtr(),
                       ST->getMemOperand());
}

SDValue DAGTypeLegalizer::SoftPromoteHalfOp_STACKMAP(SDNode *N, unsigned OpNo) {
  assert(OpNo > 1); // Because the first two arguments are guaranteed legal.
  SmallVector<SDValue> NewOps(N->ops().begin(), N->ops().end());
  SDValue Op = N->getOperand(OpNo);
  NewOps[OpNo] = GetSoftPromotedHalf(Op);
  SDValue NewNode =
      DAG.getNode(N->getOpcode(), SDLoc(N), N->getVTList(), NewOps);

  for (unsigned ResNum = 0; ResNum < N->getNumValues(); ResNum++)
    ReplaceValueWith(SDValue(N, ResNum), NewNode.getValue(ResNum));

  return SDValue(); // Signal that we replaced the node ourselves.
}

SDValue DAGTypeLegalizer::SoftPromoteHalfOp_PATCHPOINT(SDNode *N,
                                                       unsigned OpNo) {
  assert(OpNo >= 7);
  SmallVector<SDValue> NewOps(N->ops().begin(), N->ops().end());
  SDValue Op = N->getOperand(OpNo);
  NewOps[OpNo] = GetSoftPromotedHalf(Op);
  SDValue NewNode =
      DAG.getNode(N->getOpcode(), SDLoc(N), N->getVTList(), NewOps);

  for (unsigned ResNum = 0; ResNum < N->getNumValues(); ResNum++)
    ReplaceValueWith(SDValue(N, ResNum), NewNode.getValue(ResNum));

  return SDValue(); // Signal that we replaced the node ourselves.
}
