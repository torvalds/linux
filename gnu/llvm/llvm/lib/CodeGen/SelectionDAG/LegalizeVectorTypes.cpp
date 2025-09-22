//===------- LegalizeVectorTypes.cpp - Legalization of vector types -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file performs vector type splitting and scalarization for LegalizeTypes.
// Scalarization is the act of changing a computation in an illegal one-element
// vector type to be a computation in its scalar element type.  For example,
// implementing <1 x f32> arithmetic in a scalar f32 register.  This is needed
// as a base case when scalarizing vector arithmetic like <4 x f32>, which
// eventually decomposes to scalars if the target doesn't support v4f32 or v2f32
// types.
// Splitting is the act of changing a computation in an invalid vector type to
// be a computation in two vectors of half the size.  For example, implementing
// <128 x f32> operations in terms of two <64 x f32> operations.
//
//===----------------------------------------------------------------------===//

#include "LegalizeTypes.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TypeSize.h"
#include "llvm/Support/raw_ostream.h"
#include <numeric>

using namespace llvm;

#define DEBUG_TYPE "legalize-types"

//===----------------------------------------------------------------------===//
//  Result Vector Scalarization: <1 x ty> -> ty.
//===----------------------------------------------------------------------===//

void DAGTypeLegalizer::ScalarizeVectorResult(SDNode *N, unsigned ResNo) {
  LLVM_DEBUG(dbgs() << "Scalarize node result " << ResNo << ": ";
             N->dump(&DAG));
  SDValue R = SDValue();

  switch (N->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "ScalarizeVectorResult #" << ResNo << ": ";
    N->dump(&DAG);
    dbgs() << "\n";
#endif
    report_fatal_error("Do not know how to scalarize the result of this "
                       "operator!\n");

  case ISD::MERGE_VALUES:      R = ScalarizeVecRes_MERGE_VALUES(N, ResNo);break;
  case ISD::BITCAST:           R = ScalarizeVecRes_BITCAST(N); break;
  case ISD::BUILD_VECTOR:      R = ScalarizeVecRes_BUILD_VECTOR(N); break;
  case ISD::EXTRACT_SUBVECTOR: R = ScalarizeVecRes_EXTRACT_SUBVECTOR(N); break;
  case ISD::FP_ROUND:          R = ScalarizeVecRes_FP_ROUND(N); break;
  case ISD::AssertZext:
  case ISD::AssertSext:
  case ISD::FPOWI:
    R = ScalarizeVecRes_UnaryOpWithExtraInput(N);
    break;
  case ISD::INSERT_VECTOR_ELT: R = ScalarizeVecRes_INSERT_VECTOR_ELT(N); break;
  case ISD::LOAD:           R = ScalarizeVecRes_LOAD(cast<LoadSDNode>(N));break;
  case ISD::SCALAR_TO_VECTOR:  R = ScalarizeVecRes_SCALAR_TO_VECTOR(N); break;
  case ISD::SIGN_EXTEND_INREG: R = ScalarizeVecRes_InregOp(N); break;
  case ISD::VSELECT:           R = ScalarizeVecRes_VSELECT(N); break;
  case ISD::SELECT:            R = ScalarizeVecRes_SELECT(N); break;
  case ISD::SELECT_CC:         R = ScalarizeVecRes_SELECT_CC(N); break;
  case ISD::SETCC:             R = ScalarizeVecRes_SETCC(N); break;
  case ISD::UNDEF:             R = ScalarizeVecRes_UNDEF(N); break;
  case ISD::VECTOR_SHUFFLE:    R = ScalarizeVecRes_VECTOR_SHUFFLE(N); break;
  case ISD::IS_FPCLASS:        R = ScalarizeVecRes_IS_FPCLASS(N); break;
  case ISD::ANY_EXTEND_VECTOR_INREG:
  case ISD::SIGN_EXTEND_VECTOR_INREG:
  case ISD::ZERO_EXTEND_VECTOR_INREG:
    R = ScalarizeVecRes_VecInregOp(N);
    break;
  case ISD::ABS:
  case ISD::ANY_EXTEND:
  case ISD::BITREVERSE:
  case ISD::BSWAP:
  case ISD::CTLZ:
  case ISD::CTLZ_ZERO_UNDEF:
  case ISD::CTPOP:
  case ISD::CTTZ:
  case ISD::CTTZ_ZERO_UNDEF:
  case ISD::FABS:
  case ISD::FACOS:
  case ISD::FASIN:
  case ISD::FATAN:
  case ISD::FCEIL:
  case ISD::FCOS:
  case ISD::FCOSH:
  case ISD::FEXP:
  case ISD::FEXP2:
  case ISD::FEXP10:
  case ISD::FFLOOR:
  case ISD::FLOG:
  case ISD::FLOG10:
  case ISD::FLOG2:
  case ISD::FNEARBYINT:
  case ISD::FNEG:
  case ISD::FREEZE:
  case ISD::ARITH_FENCE:
  case ISD::FP_EXTEND:
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
  case ISD::FRINT:
  case ISD::LRINT:
  case ISD::LLRINT:
  case ISD::FROUND:
  case ISD::FROUNDEVEN:
  case ISD::FSIN:
  case ISD::FSINH:
  case ISD::FSQRT:
  case ISD::FTAN:
  case ISD::FTANH:
  case ISD::FTRUNC:
  case ISD::SIGN_EXTEND:
  case ISD::SINT_TO_FP:
  case ISD::TRUNCATE:
  case ISD::UINT_TO_FP:
  case ISD::ZERO_EXTEND:
  case ISD::FCANONICALIZE:
    R = ScalarizeVecRes_UnaryOp(N);
    break;
  case ISD::ADDRSPACECAST:
    R = ScalarizeVecRes_ADDRSPACECAST(N);
    break;
  case ISD::FFREXP:
    R = ScalarizeVecRes_FFREXP(N, ResNo);
    break;
  case ISD::ADD:
  case ISD::AND:
  case ISD::AVGCEILS:
  case ISD::AVGCEILU:
  case ISD::AVGFLOORS:
  case ISD::AVGFLOORU:
  case ISD::FADD:
  case ISD::FCOPYSIGN:
  case ISD::FDIV:
  case ISD::FMUL:
  case ISD::FMINNUM:
  case ISD::FMAXNUM:
  case ISD::FMINNUM_IEEE:
  case ISD::FMAXNUM_IEEE:
  case ISD::FMINIMUM:
  case ISD::FMAXIMUM:
  case ISD::FLDEXP:
  case ISD::SMIN:
  case ISD::SMAX:
  case ISD::UMIN:
  case ISD::UMAX:

  case ISD::SADDSAT:
  case ISD::UADDSAT:
  case ISD::SSUBSAT:
  case ISD::USUBSAT:
  case ISD::SSHLSAT:
  case ISD::USHLSAT:

  case ISD::FPOW:
  case ISD::FREM:
  case ISD::FSUB:
  case ISD::MUL:
  case ISD::MULHS:
  case ISD::MULHU:
  case ISD::OR:
  case ISD::SDIV:
  case ISD::SREM:
  case ISD::SUB:
  case ISD::UDIV:
  case ISD::UREM:
  case ISD::XOR:
  case ISD::SHL:
  case ISD::SRA:
  case ISD::SRL:
  case ISD::ROTL:
  case ISD::ROTR:
    R = ScalarizeVecRes_BinOp(N);
    break;

  case ISD::SCMP:
  case ISD::UCMP:
    R = ScalarizeVecRes_CMP(N);
    break;

  case ISD::FMA:
  case ISD::FSHL:
  case ISD::FSHR:
    R = ScalarizeVecRes_TernaryOp(N);
    break;

#define DAG_INSTRUCTION(NAME, NARG, ROUND_MODE, INTRINSIC, DAGN)               \
  case ISD::STRICT_##DAGN:
#include "llvm/IR/ConstrainedOps.def"
    R = ScalarizeVecRes_StrictFPOp(N);
    break;

  case ISD::FP_TO_UINT_SAT:
  case ISD::FP_TO_SINT_SAT:
    R = ScalarizeVecRes_FP_TO_XINT_SAT(N);
    break;

  case ISD::UADDO:
  case ISD::SADDO:
  case ISD::USUBO:
  case ISD::SSUBO:
  case ISD::UMULO:
  case ISD::SMULO:
    R = ScalarizeVecRes_OverflowOp(N, ResNo);
    break;
  case ISD::SMULFIX:
  case ISD::SMULFIXSAT:
  case ISD::UMULFIX:
  case ISD::UMULFIXSAT:
  case ISD::SDIVFIX:
  case ISD::SDIVFIXSAT:
  case ISD::UDIVFIX:
  case ISD::UDIVFIXSAT:
    R = ScalarizeVecRes_FIX(N);
    break;
  }

  // If R is null, the sub-method took care of registering the result.
  if (R.getNode())
    SetScalarizedVector(SDValue(N, ResNo), R);
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_BinOp(SDNode *N) {
  SDValue LHS = GetScalarizedVector(N->getOperand(0));
  SDValue RHS = GetScalarizedVector(N->getOperand(1));
  return DAG.getNode(N->getOpcode(), SDLoc(N),
                     LHS.getValueType(), LHS, RHS, N->getFlags());
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_CMP(SDNode *N) {
  SDLoc DL(N);

  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  if (getTypeAction(LHS.getValueType()) ==
      TargetLowering::TypeScalarizeVector) {
    LHS = GetScalarizedVector(LHS);
    RHS = GetScalarizedVector(RHS);
  } else {
    EVT VT = LHS.getValueType().getVectorElementType();
    LHS = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, VT, LHS,
                      DAG.getVectorIdxConstant(0, DL));
    RHS = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, VT, RHS,
                      DAG.getVectorIdxConstant(0, DL));
  }

  return DAG.getNode(N->getOpcode(), SDLoc(N),
                     N->getValueType(0).getVectorElementType(), LHS, RHS);
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_TernaryOp(SDNode *N) {
  SDValue Op0 = GetScalarizedVector(N->getOperand(0));
  SDValue Op1 = GetScalarizedVector(N->getOperand(1));
  SDValue Op2 = GetScalarizedVector(N->getOperand(2));
  return DAG.getNode(N->getOpcode(), SDLoc(N), Op0.getValueType(), Op0, Op1,
                     Op2, N->getFlags());
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_FIX(SDNode *N) {
  SDValue Op0 = GetScalarizedVector(N->getOperand(0));
  SDValue Op1 = GetScalarizedVector(N->getOperand(1));
  SDValue Op2 = N->getOperand(2);
  return DAG.getNode(N->getOpcode(), SDLoc(N), Op0.getValueType(), Op0, Op1,
                     Op2, N->getFlags());
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_FFREXP(SDNode *N, unsigned ResNo) {
  assert(N->getValueType(0).getVectorNumElements() == 1 &&
         "Unexpected vector type!");
  SDValue Elt = GetScalarizedVector(N->getOperand(0));

  EVT VT0 = N->getValueType(0);
  EVT VT1 = N->getValueType(1);
  SDLoc dl(N);

  SDNode *ScalarNode =
      DAG.getNode(N->getOpcode(), dl,
                  {VT0.getScalarType(), VT1.getScalarType()}, Elt)
          .getNode();

  // Replace the other vector result not being explicitly scalarized here.
  unsigned OtherNo = 1 - ResNo;
  EVT OtherVT = N->getValueType(OtherNo);
  if (getTypeAction(OtherVT) == TargetLowering::TypeScalarizeVector) {
    SetScalarizedVector(SDValue(N, OtherNo), SDValue(ScalarNode, OtherNo));
  } else {
    SDValue OtherVal = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, OtherVT,
                                   SDValue(ScalarNode, OtherNo));
    ReplaceValueWith(SDValue(N, OtherNo), OtherVal);
  }

  return SDValue(ScalarNode, ResNo);
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_StrictFPOp(SDNode *N) {
  EVT VT = N->getValueType(0).getVectorElementType();
  unsigned NumOpers = N->getNumOperands();
  SDValue Chain = N->getOperand(0);
  EVT ValueVTs[] = {VT, MVT::Other};
  SDLoc dl(N);

  SmallVector<SDValue, 4> Opers(NumOpers);

  // The Chain is the first operand.
  Opers[0] = Chain;

  // Now process the remaining operands.
  for (unsigned i = 1; i < NumOpers; ++i) {
    SDValue Oper = N->getOperand(i);
    EVT OperVT = Oper.getValueType();

    if (OperVT.isVector()) {
      if (getTypeAction(OperVT) == TargetLowering::TypeScalarizeVector)
        Oper = GetScalarizedVector(Oper);
      else
        Oper = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl,
                           OperVT.getVectorElementType(), Oper,
                           DAG.getVectorIdxConstant(0, dl));
    }

    Opers[i] = Oper;
  }

  SDValue Result = DAG.getNode(N->getOpcode(), dl, DAG.getVTList(ValueVTs),
                               Opers, N->getFlags());

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Result.getValue(1));
  return Result;
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_OverflowOp(SDNode *N,
                                                     unsigned ResNo) {
  SDLoc DL(N);
  EVT ResVT = N->getValueType(0);
  EVT OvVT = N->getValueType(1);

  SDValue ScalarLHS, ScalarRHS;
  if (getTypeAction(ResVT) == TargetLowering::TypeScalarizeVector) {
    ScalarLHS = GetScalarizedVector(N->getOperand(0));
    ScalarRHS = GetScalarizedVector(N->getOperand(1));
  } else {
    SmallVector<SDValue, 1> ElemsLHS, ElemsRHS;
    DAG.ExtractVectorElements(N->getOperand(0), ElemsLHS);
    DAG.ExtractVectorElements(N->getOperand(1), ElemsRHS);
    ScalarLHS = ElemsLHS[0];
    ScalarRHS = ElemsRHS[0];
  }

  SDVTList ScalarVTs = DAG.getVTList(
      ResVT.getVectorElementType(), OvVT.getVectorElementType());
  SDNode *ScalarNode = DAG.getNode(
      N->getOpcode(), DL, ScalarVTs, ScalarLHS, ScalarRHS).getNode();
  ScalarNode->setFlags(N->getFlags());

  // Replace the other vector result not being explicitly scalarized here.
  unsigned OtherNo = 1 - ResNo;
  EVT OtherVT = N->getValueType(OtherNo);
  if (getTypeAction(OtherVT) == TargetLowering::TypeScalarizeVector) {
    SetScalarizedVector(SDValue(N, OtherNo), SDValue(ScalarNode, OtherNo));
  } else {
    SDValue OtherVal = DAG.getNode(
        ISD::SCALAR_TO_VECTOR, DL, OtherVT, SDValue(ScalarNode, OtherNo));
    ReplaceValueWith(SDValue(N, OtherNo), OtherVal);
  }

  return SDValue(ScalarNode, ResNo);
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_MERGE_VALUES(SDNode *N,
                                                       unsigned ResNo) {
  SDValue Op = DisintegrateMERGE_VALUES(N, ResNo);
  return GetScalarizedVector(Op);
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_BITCAST(SDNode *N) {
  SDValue Op = N->getOperand(0);
  if (Op.getValueType().isVector()
      && Op.getValueType().getVectorNumElements() == 1
      && !isSimpleLegalType(Op.getValueType()))
    Op = GetScalarizedVector(Op);
  EVT NewVT = N->getValueType(0).getVectorElementType();
  return DAG.getNode(ISD::BITCAST, SDLoc(N),
                     NewVT, Op);
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_BUILD_VECTOR(SDNode *N) {
  EVT EltVT = N->getValueType(0).getVectorElementType();
  SDValue InOp = N->getOperand(0);
  // The BUILD_VECTOR operands may be of wider element types and
  // we may need to truncate them back to the requested return type.
  if (EltVT.isInteger())
    return DAG.getNode(ISD::TRUNCATE, SDLoc(N), EltVT, InOp);
  return InOp;
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_EXTRACT_SUBVECTOR(SDNode *N) {
  return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SDLoc(N),
                     N->getValueType(0).getVectorElementType(),
                     N->getOperand(0), N->getOperand(1));
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_FP_ROUND(SDNode *N) {
  SDLoc DL(N);
  SDValue Op = N->getOperand(0);
  EVT OpVT = Op.getValueType();
  // The result needs scalarizing, but it's not a given that the source does.
  // See similar logic in ScalarizeVecRes_UnaryOp.
  if (getTypeAction(OpVT) == TargetLowering::TypeScalarizeVector) {
    Op = GetScalarizedVector(Op);
  } else {
    EVT VT = OpVT.getVectorElementType();
    Op = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, VT, Op,
                     DAG.getVectorIdxConstant(0, DL));
  }
  return DAG.getNode(ISD::FP_ROUND, DL,
                     N->getValueType(0).getVectorElementType(), Op,
                     N->getOperand(1));
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_UnaryOpWithExtraInput(SDNode *N) {
  SDValue Op = GetScalarizedVector(N->getOperand(0));
  return DAG.getNode(N->getOpcode(), SDLoc(N), Op.getValueType(), Op,
                     N->getOperand(1));
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_INSERT_VECTOR_ELT(SDNode *N) {
  // The value to insert may have a wider type than the vector element type,
  // so be sure to truncate it to the element type if necessary.
  SDValue Op = N->getOperand(1);
  EVT EltVT = N->getValueType(0).getVectorElementType();
  if (Op.getValueType() != EltVT)
    // FIXME: Can this happen for floating point types?
    Op = DAG.getNode(ISD::TRUNCATE, SDLoc(N), EltVT, Op);
  return Op;
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_LOAD(LoadSDNode *N) {
  assert(N->isUnindexed() && "Indexed vector load?");

  SDValue Result = DAG.getLoad(
      ISD::UNINDEXED, N->getExtensionType(),
      N->getValueType(0).getVectorElementType(), SDLoc(N), N->getChain(),
      N->getBasePtr(), DAG.getUNDEF(N->getBasePtr().getValueType()),
      N->getPointerInfo(), N->getMemoryVT().getVectorElementType(),
      N->getOriginalAlign(), N->getMemOperand()->getFlags(), N->getAAInfo());

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Result.getValue(1));
  return Result;
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_UnaryOp(SDNode *N) {
  // Get the dest type - it doesn't always match the input type, e.g. int_to_fp.
  EVT DestVT = N->getValueType(0).getVectorElementType();
  SDValue Op = N->getOperand(0);
  EVT OpVT = Op.getValueType();
  SDLoc DL(N);
  // The result needs scalarizing, but it's not a given that the source does.
  // This is a workaround for targets where it's impossible to scalarize the
  // result of a conversion, because the source type is legal.
  // For instance, this happens on AArch64: v1i1 is illegal but v1i{8,16,32}
  // are widened to v8i8, v4i16, and v2i32, which is legal, because v1i64 is
  // legal and was not scalarized.
  // See the similar logic in ScalarizeVecRes_SETCC
  if (getTypeAction(OpVT) == TargetLowering::TypeScalarizeVector) {
    Op = GetScalarizedVector(Op);
  } else {
    EVT VT = OpVT.getVectorElementType();
    Op = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, VT, Op,
                     DAG.getVectorIdxConstant(0, DL));
  }
  return DAG.getNode(N->getOpcode(), SDLoc(N), DestVT, Op, N->getFlags());
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_InregOp(SDNode *N) {
  EVT EltVT = N->getValueType(0).getVectorElementType();
  EVT ExtVT = cast<VTSDNode>(N->getOperand(1))->getVT().getVectorElementType();
  SDValue LHS = GetScalarizedVector(N->getOperand(0));
  return DAG.getNode(N->getOpcode(), SDLoc(N), EltVT,
                     LHS, DAG.getValueType(ExtVT));
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_VecInregOp(SDNode *N) {
  SDLoc DL(N);
  SDValue Op = N->getOperand(0);

  EVT OpVT = Op.getValueType();
  EVT OpEltVT = OpVT.getVectorElementType();
  EVT EltVT = N->getValueType(0).getVectorElementType();

  if (getTypeAction(OpVT) == TargetLowering::TypeScalarizeVector) {
    Op = GetScalarizedVector(Op);
  } else {
    Op = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, OpEltVT, Op,
                     DAG.getVectorIdxConstant(0, DL));
  }

  switch (N->getOpcode()) {
  case ISD::ANY_EXTEND_VECTOR_INREG:
    return DAG.getNode(ISD::ANY_EXTEND, DL, EltVT, Op);
  case ISD::SIGN_EXTEND_VECTOR_INREG:
    return DAG.getNode(ISD::SIGN_EXTEND, DL, EltVT, Op);
  case ISD::ZERO_EXTEND_VECTOR_INREG:
    return DAG.getNode(ISD::ZERO_EXTEND, DL, EltVT, Op);
  }

  llvm_unreachable("Illegal extend_vector_inreg opcode");
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_ADDRSPACECAST(SDNode *N) {
  EVT DestVT = N->getValueType(0).getVectorElementType();
  SDValue Op = N->getOperand(0);
  EVT OpVT = Op.getValueType();
  SDLoc DL(N);
  // The result needs scalarizing, but it's not a given that the source does.
  // This is a workaround for targets where it's impossible to scalarize the
  // result of a conversion, because the source type is legal.
  // For instance, this happens on AArch64: v1i1 is illegal but v1i{8,16,32}
  // are widened to v8i8, v4i16, and v2i32, which is legal, because v1i64 is
  // legal and was not scalarized.
  // See the similar logic in ScalarizeVecRes_SETCC
  if (getTypeAction(OpVT) == TargetLowering::TypeScalarizeVector) {
    Op = GetScalarizedVector(Op);
  } else {
    EVT VT = OpVT.getVectorElementType();
    Op = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, VT, Op,
                     DAG.getVectorIdxConstant(0, DL));
  }
  auto *AddrSpaceCastN = cast<AddrSpaceCastSDNode>(N);
  unsigned SrcAS = AddrSpaceCastN->getSrcAddressSpace();
  unsigned DestAS = AddrSpaceCastN->getDestAddressSpace();
  return DAG.getAddrSpaceCast(DL, DestVT, Op, SrcAS, DestAS);
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_SCALAR_TO_VECTOR(SDNode *N) {
  // If the operand is wider than the vector element type then it is implicitly
  // truncated.  Make that explicit here.
  EVT EltVT = N->getValueType(0).getVectorElementType();
  SDValue InOp = N->getOperand(0);
  if (InOp.getValueType() != EltVT)
    return DAG.getNode(ISD::TRUNCATE, SDLoc(N), EltVT, InOp);
  return InOp;
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_VSELECT(SDNode *N) {
  SDValue Cond = N->getOperand(0);
  EVT OpVT = Cond.getValueType();
  SDLoc DL(N);
  // The vselect result and true/value operands needs scalarizing, but it's
  // not a given that the Cond does. For instance, in AVX512 v1i1 is legal.
  // See the similar logic in ScalarizeVecRes_SETCC
  if (getTypeAction(OpVT) == TargetLowering::TypeScalarizeVector) {
    Cond = GetScalarizedVector(Cond);
  } else {
    EVT VT = OpVT.getVectorElementType();
    Cond = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, VT, Cond,
                       DAG.getVectorIdxConstant(0, DL));
  }

  SDValue LHS = GetScalarizedVector(N->getOperand(1));
  TargetLowering::BooleanContent ScalarBool =
      TLI.getBooleanContents(false, false);
  TargetLowering::BooleanContent VecBool = TLI.getBooleanContents(true, false);

  // If integer and float booleans have different contents then we can't
  // reliably optimize in all cases. There is a full explanation for this in
  // DAGCombiner::visitSELECT() where the same issue affects folding
  // (select C, 0, 1) to (xor C, 1).
  if (TLI.getBooleanContents(false, false) !=
      TLI.getBooleanContents(false, true)) {
    // At least try the common case where the boolean is generated by a
    // comparison.
    if (Cond->getOpcode() == ISD::SETCC) {
      EVT OpVT = Cond->getOperand(0).getValueType();
      ScalarBool = TLI.getBooleanContents(OpVT.getScalarType());
      VecBool = TLI.getBooleanContents(OpVT);
    } else
      ScalarBool = TargetLowering::UndefinedBooleanContent;
  }

  EVT CondVT = Cond.getValueType();
  if (ScalarBool != VecBool) {
    switch (ScalarBool) {
      case TargetLowering::UndefinedBooleanContent:
        break;
      case TargetLowering::ZeroOrOneBooleanContent:
        assert(VecBool == TargetLowering::UndefinedBooleanContent ||
               VecBool == TargetLowering::ZeroOrNegativeOneBooleanContent);
        // Vector read from all ones, scalar expects a single 1 so mask.
        Cond = DAG.getNode(ISD::AND, SDLoc(N), CondVT,
                           Cond, DAG.getConstant(1, SDLoc(N), CondVT));
        break;
      case TargetLowering::ZeroOrNegativeOneBooleanContent:
        assert(VecBool == TargetLowering::UndefinedBooleanContent ||
               VecBool == TargetLowering::ZeroOrOneBooleanContent);
        // Vector reads from a one, scalar from all ones so sign extend.
        Cond = DAG.getNode(ISD::SIGN_EXTEND_INREG, SDLoc(N), CondVT,
                           Cond, DAG.getValueType(MVT::i1));
        break;
    }
  }

  // Truncate the condition if needed
  auto BoolVT = getSetCCResultType(CondVT);
  if (BoolVT.bitsLT(CondVT))
    Cond = DAG.getNode(ISD::TRUNCATE, SDLoc(N), BoolVT, Cond);

  return DAG.getSelect(SDLoc(N),
                       LHS.getValueType(), Cond, LHS,
                       GetScalarizedVector(N->getOperand(2)));
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_SELECT(SDNode *N) {
  SDValue LHS = GetScalarizedVector(N->getOperand(1));
  return DAG.getSelect(SDLoc(N),
                       LHS.getValueType(), N->getOperand(0), LHS,
                       GetScalarizedVector(N->getOperand(2)));
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_SELECT_CC(SDNode *N) {
  SDValue LHS = GetScalarizedVector(N->getOperand(2));
  return DAG.getNode(ISD::SELECT_CC, SDLoc(N), LHS.getValueType(),
                     N->getOperand(0), N->getOperand(1),
                     LHS, GetScalarizedVector(N->getOperand(3)),
                     N->getOperand(4));
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_UNDEF(SDNode *N) {
  return DAG.getUNDEF(N->getValueType(0).getVectorElementType());
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_VECTOR_SHUFFLE(SDNode *N) {
  // Figure out if the scalar is the LHS or RHS and return it.
  SDValue Arg = N->getOperand(2).getOperand(0);
  if (Arg.isUndef())
    return DAG.getUNDEF(N->getValueType(0).getVectorElementType());
  unsigned Op = !cast<ConstantSDNode>(Arg)->isZero();
  return GetScalarizedVector(N->getOperand(Op));
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_FP_TO_XINT_SAT(SDNode *N) {
  SDValue Src = N->getOperand(0);
  EVT SrcVT = Src.getValueType();
  SDLoc dl(N);

  // Handle case where result is scalarized but operand is not
  if (getTypeAction(SrcVT) == TargetLowering::TypeScalarizeVector)
    Src = GetScalarizedVector(Src);
  else
    Src = DAG.getNode(
        ISD::EXTRACT_VECTOR_ELT, dl, SrcVT.getVectorElementType(), Src,
        DAG.getConstant(0, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));

  EVT DstVT = N->getValueType(0).getVectorElementType();
  return DAG.getNode(N->getOpcode(), dl, DstVT, Src, N->getOperand(1));
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_SETCC(SDNode *N) {
  assert(N->getValueType(0).isVector() &&
         N->getOperand(0).getValueType().isVector() &&
         "Operand types must be vectors");
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  EVT OpVT = LHS.getValueType();
  EVT NVT = N->getValueType(0).getVectorElementType();
  SDLoc DL(N);

  // The result needs scalarizing, but it's not a given that the source does.
  if (getTypeAction(OpVT) == TargetLowering::TypeScalarizeVector) {
    LHS = GetScalarizedVector(LHS);
    RHS = GetScalarizedVector(RHS);
  } else {
    EVT VT = OpVT.getVectorElementType();
    LHS = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, VT, LHS,
                      DAG.getVectorIdxConstant(0, DL));
    RHS = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, VT, RHS,
                      DAG.getVectorIdxConstant(0, DL));
  }

  // Turn it into a scalar SETCC.
  SDValue Res = DAG.getNode(ISD::SETCC, DL, MVT::i1, LHS, RHS,
                            N->getOperand(2));
  // Vectors may have a different boolean contents to scalars.  Promote the
  // value appropriately.
  ISD::NodeType ExtendCode =
      TargetLowering::getExtendForContent(TLI.getBooleanContents(OpVT));
  return DAG.getNode(ExtendCode, DL, NVT, Res);
}

SDValue DAGTypeLegalizer::ScalarizeVecRes_IS_FPCLASS(SDNode *N) {
  SDLoc DL(N);
  SDValue Arg = N->getOperand(0);
  SDValue Test = N->getOperand(1);
  EVT ArgVT = Arg.getValueType();
  EVT ResultVT = N->getValueType(0).getVectorElementType();

  if (getTypeAction(ArgVT) == TargetLowering::TypeScalarizeVector) {
    Arg = GetScalarizedVector(Arg);
  } else {
    EVT VT = ArgVT.getVectorElementType();
    Arg = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, VT, Arg,
                      DAG.getVectorIdxConstant(0, DL));
  }

  SDValue Res =
      DAG.getNode(ISD::IS_FPCLASS, DL, MVT::i1, {Arg, Test}, N->getFlags());
  // Vectors may have a different boolean contents to scalars.  Promote the
  // value appropriately.
  ISD::NodeType ExtendCode =
      TargetLowering::getExtendForContent(TLI.getBooleanContents(ArgVT));
  return DAG.getNode(ExtendCode, DL, ResultVT, Res);
}

//===----------------------------------------------------------------------===//
//  Operand Vector Scalarization <1 x ty> -> ty.
//===----------------------------------------------------------------------===//

bool DAGTypeLegalizer::ScalarizeVectorOperand(SDNode *N, unsigned OpNo) {
  LLVM_DEBUG(dbgs() << "Scalarize node operand " << OpNo << ": ";
             N->dump(&DAG));
  SDValue Res = SDValue();

  switch (N->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "ScalarizeVectorOperand Op #" << OpNo << ": ";
    N->dump(&DAG);
    dbgs() << "\n";
#endif
    report_fatal_error("Do not know how to scalarize this operator's "
                       "operand!\n");
  case ISD::BITCAST:
    Res = ScalarizeVecOp_BITCAST(N);
    break;
  case ISD::ANY_EXTEND:
  case ISD::ZERO_EXTEND:
  case ISD::SIGN_EXTEND:
  case ISD::TRUNCATE:
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:
  case ISD::LRINT:
  case ISD::LLRINT:
    Res = ScalarizeVecOp_UnaryOp(N);
    break;
  case ISD::STRICT_SINT_TO_FP:
  case ISD::STRICT_UINT_TO_FP:
  case ISD::STRICT_FP_TO_SINT:
  case ISD::STRICT_FP_TO_UINT:
    Res = ScalarizeVecOp_UnaryOp_StrictFP(N);
    break;
  case ISD::CONCAT_VECTORS:
    Res = ScalarizeVecOp_CONCAT_VECTORS(N);
    break;
  case ISD::EXTRACT_VECTOR_ELT:
    Res = ScalarizeVecOp_EXTRACT_VECTOR_ELT(N);
    break;
  case ISD::VSELECT:
    Res = ScalarizeVecOp_VSELECT(N);
    break;
  case ISD::SETCC:
    Res = ScalarizeVecOp_VSETCC(N);
    break;
  case ISD::STORE:
    Res = ScalarizeVecOp_STORE(cast<StoreSDNode>(N), OpNo);
    break;
  case ISD::STRICT_FP_ROUND:
    Res = ScalarizeVecOp_STRICT_FP_ROUND(N, OpNo);
    break;
  case ISD::FP_ROUND:
    Res = ScalarizeVecOp_FP_ROUND(N, OpNo);
    break;
  case ISD::STRICT_FP_EXTEND:
    Res = ScalarizeVecOp_STRICT_FP_EXTEND(N);
    break;
  case ISD::FP_EXTEND:
    Res = ScalarizeVecOp_FP_EXTEND(N);
    break;
  case ISD::VECREDUCE_FADD:
  case ISD::VECREDUCE_FMUL:
  case ISD::VECREDUCE_ADD:
  case ISD::VECREDUCE_MUL:
  case ISD::VECREDUCE_AND:
  case ISD::VECREDUCE_OR:
  case ISD::VECREDUCE_XOR:
  case ISD::VECREDUCE_SMAX:
  case ISD::VECREDUCE_SMIN:
  case ISD::VECREDUCE_UMAX:
  case ISD::VECREDUCE_UMIN:
  case ISD::VECREDUCE_FMAX:
  case ISD::VECREDUCE_FMIN:
  case ISD::VECREDUCE_FMAXIMUM:
  case ISD::VECREDUCE_FMINIMUM:
    Res = ScalarizeVecOp_VECREDUCE(N);
    break;
  case ISD::VECREDUCE_SEQ_FADD:
  case ISD::VECREDUCE_SEQ_FMUL:
    Res = ScalarizeVecOp_VECREDUCE_SEQ(N);
    break;
  case ISD::SCMP:
  case ISD::UCMP:
    Res = ScalarizeVecOp_CMP(N);
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

/// If the value to convert is a vector that needs to be scalarized, it must be
/// <1 x ty>. Convert the element instead.
SDValue DAGTypeLegalizer::ScalarizeVecOp_BITCAST(SDNode *N) {
  SDValue Elt = GetScalarizedVector(N->getOperand(0));
  return DAG.getNode(ISD::BITCAST, SDLoc(N),
                     N->getValueType(0), Elt);
}

/// If the input is a vector that needs to be scalarized, it must be <1 x ty>.
/// Do the operation on the element instead.
SDValue DAGTypeLegalizer::ScalarizeVecOp_UnaryOp(SDNode *N) {
  assert(N->getValueType(0).getVectorNumElements() == 1 &&
         "Unexpected vector type!");
  SDValue Elt = GetScalarizedVector(N->getOperand(0));
  SDValue Op = DAG.getNode(N->getOpcode(), SDLoc(N),
                           N->getValueType(0).getScalarType(), Elt);
  // Revectorize the result so the types line up with what the uses of this
  // expression expect.
  return DAG.getNode(ISD::SCALAR_TO_VECTOR, SDLoc(N), N->getValueType(0), Op);
}

/// If the input is a vector that needs to be scalarized, it must be <1 x ty>.
/// Do the strict FP operation on the element instead.
SDValue DAGTypeLegalizer::ScalarizeVecOp_UnaryOp_StrictFP(SDNode *N) {
  assert(N->getValueType(0).getVectorNumElements() == 1 &&
         "Unexpected vector type!");
  SDValue Elt = GetScalarizedVector(N->getOperand(1));
  SDValue Res = DAG.getNode(N->getOpcode(), SDLoc(N),
                            { N->getValueType(0).getScalarType(), MVT::Other },
                            { N->getOperand(0), Elt });
  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  // Revectorize the result so the types line up with what the uses of this
  // expression expect.
  Res = DAG.getNode(ISD::SCALAR_TO_VECTOR, SDLoc(N), N->getValueType(0), Res);

  // Do our own replacement and return SDValue() to tell the caller that we
  // handled all replacements since caller can only handle a single result.
  ReplaceValueWith(SDValue(N, 0), Res);
  return SDValue();
}

/// The vectors to concatenate have length one - use a BUILD_VECTOR instead.
SDValue DAGTypeLegalizer::ScalarizeVecOp_CONCAT_VECTORS(SDNode *N) {
  SmallVector<SDValue, 8> Ops(N->getNumOperands());
  for (unsigned i = 0, e = N->getNumOperands(); i < e; ++i)
    Ops[i] = GetScalarizedVector(N->getOperand(i));
  return DAG.getBuildVector(N->getValueType(0), SDLoc(N), Ops);
}

/// If the input is a vector that needs to be scalarized, it must be <1 x ty>,
/// so just return the element, ignoring the index.
SDValue DAGTypeLegalizer::ScalarizeVecOp_EXTRACT_VECTOR_ELT(SDNode *N) {
  EVT VT = N->getValueType(0);
  SDValue Res = GetScalarizedVector(N->getOperand(0));
  if (Res.getValueType() != VT)
    Res = VT.isFloatingPoint()
              ? DAG.getNode(ISD::FP_EXTEND, SDLoc(N), VT, Res)
              : DAG.getNode(ISD::ANY_EXTEND, SDLoc(N), VT, Res);
  return Res;
}

/// If the input condition is a vector that needs to be scalarized, it must be
/// <1 x i1>, so just convert to a normal ISD::SELECT
/// (still with vector output type since that was acceptable if we got here).
SDValue DAGTypeLegalizer::ScalarizeVecOp_VSELECT(SDNode *N) {
  SDValue ScalarCond = GetScalarizedVector(N->getOperand(0));
  EVT VT = N->getValueType(0);

  return DAG.getNode(ISD::SELECT, SDLoc(N), VT, ScalarCond, N->getOperand(1),
                     N->getOperand(2));
}

/// If the operand is a vector that needs to be scalarized then the
/// result must be v1i1, so just convert to a scalar SETCC and wrap
/// with a scalar_to_vector since the res type is legal if we got here
SDValue DAGTypeLegalizer::ScalarizeVecOp_VSETCC(SDNode *N) {
  assert(N->getValueType(0).isVector() &&
         N->getOperand(0).getValueType().isVector() &&
         "Operand types must be vectors");
  assert(N->getValueType(0) == MVT::v1i1 && "Expected v1i1 type");

  EVT VT = N->getValueType(0);
  SDValue LHS = GetScalarizedVector(N->getOperand(0));
  SDValue RHS = GetScalarizedVector(N->getOperand(1));

  EVT OpVT = N->getOperand(0).getValueType();
  EVT NVT = VT.getVectorElementType();
  SDLoc DL(N);
  // Turn it into a scalar SETCC.
  SDValue Res = DAG.getNode(ISD::SETCC, DL, MVT::i1, LHS, RHS,
      N->getOperand(2));

  // Vectors may have a different boolean contents to scalars.  Promote the
  // value appropriately.
  ISD::NodeType ExtendCode =
      TargetLowering::getExtendForContent(TLI.getBooleanContents(OpVT));

  Res = DAG.getNode(ExtendCode, DL, NVT, Res);

  return DAG.getNode(ISD::SCALAR_TO_VECTOR, DL, VT, Res);
}

/// If the value to store is a vector that needs to be scalarized, it must be
/// <1 x ty>. Just store the element.
SDValue DAGTypeLegalizer::ScalarizeVecOp_STORE(StoreSDNode *N, unsigned OpNo){
  assert(N->isUnindexed() && "Indexed store of one-element vector?");
  assert(OpNo == 1 && "Do not know how to scalarize this operand!");
  SDLoc dl(N);

  if (N->isTruncatingStore())
    return DAG.getTruncStore(
        N->getChain(), dl, GetScalarizedVector(N->getOperand(1)),
        N->getBasePtr(), N->getPointerInfo(),
        N->getMemoryVT().getVectorElementType(), N->getOriginalAlign(),
        N->getMemOperand()->getFlags(), N->getAAInfo());

  return DAG.getStore(N->getChain(), dl, GetScalarizedVector(N->getOperand(1)),
                      N->getBasePtr(), N->getPointerInfo(),
                      N->getOriginalAlign(), N->getMemOperand()->getFlags(),
                      N->getAAInfo());
}

/// If the value to round is a vector that needs to be scalarized, it must be
/// <1 x ty>. Convert the element instead.
SDValue DAGTypeLegalizer::ScalarizeVecOp_FP_ROUND(SDNode *N, unsigned OpNo) {
  assert(OpNo == 0 && "Wrong operand for scalarization!");
  SDValue Elt = GetScalarizedVector(N->getOperand(0));
  SDValue Res = DAG.getNode(ISD::FP_ROUND, SDLoc(N),
                            N->getValueType(0).getVectorElementType(), Elt,
                            N->getOperand(1));
  return DAG.getNode(ISD::SCALAR_TO_VECTOR, SDLoc(N), N->getValueType(0), Res);
}

SDValue DAGTypeLegalizer::ScalarizeVecOp_STRICT_FP_ROUND(SDNode *N, 
                                                         unsigned OpNo) {
  assert(OpNo == 1 && "Wrong operand for scalarization!");
  SDValue Elt = GetScalarizedVector(N->getOperand(1));
  SDValue Res = DAG.getNode(ISD::STRICT_FP_ROUND, SDLoc(N),
                            { N->getValueType(0).getVectorElementType(), 
                              MVT::Other },
                            { N->getOperand(0), Elt, N->getOperand(2) });
  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));

  Res = DAG.getNode(ISD::SCALAR_TO_VECTOR, SDLoc(N), N->getValueType(0), Res);

  // Do our own replacement and return SDValue() to tell the caller that we
  // handled all replacements since caller can only handle a single result.
  ReplaceValueWith(SDValue(N, 0), Res);
  return SDValue();
}

/// If the value to extend is a vector that needs to be scalarized, it must be
/// <1 x ty>. Convert the element instead.
SDValue DAGTypeLegalizer::ScalarizeVecOp_FP_EXTEND(SDNode *N) {
  SDValue Elt = GetScalarizedVector(N->getOperand(0));
  SDValue Res = DAG.getNode(ISD::FP_EXTEND, SDLoc(N),
                            N->getValueType(0).getVectorElementType(), Elt);
  return DAG.getNode(ISD::SCALAR_TO_VECTOR, SDLoc(N), N->getValueType(0), Res);
}

/// If the value to extend is a vector that needs to be scalarized, it must be
/// <1 x ty>. Convert the element instead.
SDValue DAGTypeLegalizer::ScalarizeVecOp_STRICT_FP_EXTEND(SDNode *N) {
  SDValue Elt = GetScalarizedVector(N->getOperand(1));
  SDValue Res =
      DAG.getNode(ISD::STRICT_FP_EXTEND, SDLoc(N),
                  {N->getValueType(0).getVectorElementType(), MVT::Other},
                  {N->getOperand(0), Elt});
  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));

  Res = DAG.getNode(ISD::SCALAR_TO_VECTOR, SDLoc(N), N->getValueType(0), Res);

  // Do our own replacement and return SDValue() to tell the caller that we
  // handled all replacements since caller can only handle a single result.
  ReplaceValueWith(SDValue(N, 0), Res);
  return SDValue();
}

SDValue DAGTypeLegalizer::ScalarizeVecOp_VECREDUCE(SDNode *N) {
  SDValue Res = GetScalarizedVector(N->getOperand(0));
  // Result type may be wider than element type.
  if (Res.getValueType() != N->getValueType(0))
    Res = DAG.getNode(ISD::ANY_EXTEND, SDLoc(N), N->getValueType(0), Res);
  return Res;
}

SDValue DAGTypeLegalizer::ScalarizeVecOp_VECREDUCE_SEQ(SDNode *N) {
  SDValue AccOp = N->getOperand(0);
  SDValue VecOp = N->getOperand(1);

  unsigned BaseOpc = ISD::getVecReduceBaseOpcode(N->getOpcode());

  SDValue Op = GetScalarizedVector(VecOp);
  return DAG.getNode(BaseOpc, SDLoc(N), N->getValueType(0),
                     AccOp, Op, N->getFlags());
}

SDValue DAGTypeLegalizer::ScalarizeVecOp_CMP(SDNode *N) {
  SDValue LHS = GetScalarizedVector(N->getOperand(0));
  SDValue RHS = GetScalarizedVector(N->getOperand(1));

  EVT ResVT = N->getValueType(0).getVectorElementType();
  SDValue Cmp = DAG.getNode(N->getOpcode(), SDLoc(N), ResVT, LHS, RHS);
  return DAG.getNode(ISD::SCALAR_TO_VECTOR, SDLoc(N), N->getValueType(0), Cmp);
}

//===----------------------------------------------------------------------===//
//  Result Vector Splitting
//===----------------------------------------------------------------------===//

/// This method is called when the specified result of the specified node is
/// found to need vector splitting. At this point, the node may also have
/// invalid operands or may have other results that need legalization, we just
/// know that (at least) one result needs vector splitting.
void DAGTypeLegalizer::SplitVectorResult(SDNode *N, unsigned ResNo) {
  LLVM_DEBUG(dbgs() << "Split node result: "; N->dump(&DAG));
  SDValue Lo, Hi;

  // See if the target wants to custom expand this node.
  if (CustomLowerNode(N, N->getValueType(ResNo), true))
    return;

  switch (N->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "SplitVectorResult #" << ResNo << ": ";
    N->dump(&DAG);
    dbgs() << "\n";
#endif
    report_fatal_error("Do not know how to split the result of this "
                       "operator!\n");

  case ISD::MERGE_VALUES: SplitRes_MERGE_VALUES(N, ResNo, Lo, Hi); break;
  case ISD::AssertZext:   SplitVecRes_AssertZext(N, Lo, Hi); break;
  case ISD::VSELECT:
  case ISD::SELECT:
  case ISD::VP_MERGE:
  case ISD::VP_SELECT:    SplitRes_Select(N, Lo, Hi); break;
  case ISD::SELECT_CC:    SplitRes_SELECT_CC(N, Lo, Hi); break;
  case ISD::UNDEF:        SplitRes_UNDEF(N, Lo, Hi); break;
  case ISD::BITCAST:           SplitVecRes_BITCAST(N, Lo, Hi); break;
  case ISD::BUILD_VECTOR:      SplitVecRes_BUILD_VECTOR(N, Lo, Hi); break;
  case ISD::CONCAT_VECTORS:    SplitVecRes_CONCAT_VECTORS(N, Lo, Hi); break;
  case ISD::EXTRACT_SUBVECTOR: SplitVecRes_EXTRACT_SUBVECTOR(N, Lo, Hi); break;
  case ISD::INSERT_SUBVECTOR:  SplitVecRes_INSERT_SUBVECTOR(N, Lo, Hi); break;
  case ISD::FPOWI:
  case ISD::FLDEXP:
  case ISD::FCOPYSIGN:         SplitVecRes_FPOp_MultiType(N, Lo, Hi); break;
  case ISD::IS_FPCLASS:        SplitVecRes_IS_FPCLASS(N, Lo, Hi); break;
  case ISD::INSERT_VECTOR_ELT: SplitVecRes_INSERT_VECTOR_ELT(N, Lo, Hi); break;
  case ISD::EXPERIMENTAL_VP_SPLAT: SplitVecRes_VP_SPLAT(N, Lo, Hi); break;
  case ISD::SPLAT_VECTOR:
  case ISD::SCALAR_TO_VECTOR:
    SplitVecRes_ScalarOp(N, Lo, Hi);
    break;
  case ISD::STEP_VECTOR:
    SplitVecRes_STEP_VECTOR(N, Lo, Hi);
    break;
  case ISD::SIGN_EXTEND_INREG: SplitVecRes_InregOp(N, Lo, Hi); break;
  case ISD::LOAD:
    SplitVecRes_LOAD(cast<LoadSDNode>(N), Lo, Hi);
    break;
  case ISD::VP_LOAD:
    SplitVecRes_VP_LOAD(cast<VPLoadSDNode>(N), Lo, Hi);
    break;
  case ISD::EXPERIMENTAL_VP_STRIDED_LOAD:
    SplitVecRes_VP_STRIDED_LOAD(cast<VPStridedLoadSDNode>(N), Lo, Hi);
    break;
  case ISD::MLOAD:
    SplitVecRes_MLOAD(cast<MaskedLoadSDNode>(N), Lo, Hi);
    break;
  case ISD::MGATHER:
  case ISD::VP_GATHER:
    SplitVecRes_Gather(cast<MemSDNode>(N), Lo, Hi, /*SplitSETCC*/ true);
    break;
  case ISD::VECTOR_COMPRESS:
    SplitVecRes_VECTOR_COMPRESS(N, Lo, Hi);
    break;
  case ISD::SETCC:
  case ISD::VP_SETCC:
    SplitVecRes_SETCC(N, Lo, Hi);
    break;
  case ISD::VECTOR_REVERSE:
    SplitVecRes_VECTOR_REVERSE(N, Lo, Hi);
    break;
  case ISD::VECTOR_SHUFFLE:
    SplitVecRes_VECTOR_SHUFFLE(cast<ShuffleVectorSDNode>(N), Lo, Hi);
    break;
  case ISD::VECTOR_SPLICE:
    SplitVecRes_VECTOR_SPLICE(N, Lo, Hi);
    break;
  case ISD::VECTOR_DEINTERLEAVE:
    SplitVecRes_VECTOR_DEINTERLEAVE(N);
    return;
  case ISD::VECTOR_INTERLEAVE:
    SplitVecRes_VECTOR_INTERLEAVE(N);
    return;
  case ISD::VAARG:
    SplitVecRes_VAARG(N, Lo, Hi);
    break;

  case ISD::ANY_EXTEND_VECTOR_INREG:
  case ISD::SIGN_EXTEND_VECTOR_INREG:
  case ISD::ZERO_EXTEND_VECTOR_INREG:
    SplitVecRes_ExtVecInRegOp(N, Lo, Hi);
    break;

  case ISD::ABS:
  case ISD::VP_ABS:
  case ISD::BITREVERSE:
  case ISD::VP_BITREVERSE:
  case ISD::BSWAP:
  case ISD::VP_BSWAP:
  case ISD::CTLZ:
  case ISD::VP_CTLZ:
  case ISD::CTTZ:
  case ISD::VP_CTTZ:
  case ISD::CTLZ_ZERO_UNDEF:
  case ISD::VP_CTLZ_ZERO_UNDEF:
  case ISD::CTTZ_ZERO_UNDEF:
  case ISD::VP_CTTZ_ZERO_UNDEF:
  case ISD::CTPOP:
  case ISD::VP_CTPOP:
  case ISD::FABS: case ISD::VP_FABS:
  case ISD::FACOS:
  case ISD::FASIN:
  case ISD::FATAN:
  case ISD::FCEIL:
  case ISD::VP_FCEIL:
  case ISD::FCOS:
  case ISD::FCOSH:
  case ISD::FEXP:
  case ISD::FEXP2:
  case ISD::FEXP10:
  case ISD::FFLOOR:
  case ISD::VP_FFLOOR:
  case ISD::FLOG:
  case ISD::FLOG10:
  case ISD::FLOG2:
  case ISD::FNEARBYINT:
  case ISD::VP_FNEARBYINT:
  case ISD::FNEG: case ISD::VP_FNEG:
  case ISD::FREEZE:
  case ISD::ARITH_FENCE:
  case ISD::FP_EXTEND:
  case ISD::VP_FP_EXTEND:
  case ISD::FP_ROUND:
  case ISD::VP_FP_ROUND:
  case ISD::FP_TO_SINT:
  case ISD::VP_FP_TO_SINT:
  case ISD::FP_TO_UINT:
  case ISD::VP_FP_TO_UINT:
  case ISD::FRINT:
  case ISD::VP_FRINT:
  case ISD::LRINT:
  case ISD::VP_LRINT:
  case ISD::LLRINT:
  case ISD::VP_LLRINT:
  case ISD::FROUND:
  case ISD::VP_FROUND:
  case ISD::FROUNDEVEN:
  case ISD::VP_FROUNDEVEN:
  case ISD::FSIN:
  case ISD::FSINH:
  case ISD::FSQRT: case ISD::VP_SQRT:
  case ISD::FTAN:
  case ISD::FTANH:
  case ISD::FTRUNC:
  case ISD::VP_FROUNDTOZERO:
  case ISD::SINT_TO_FP:
  case ISD::VP_SINT_TO_FP:
  case ISD::TRUNCATE:
  case ISD::VP_TRUNCATE:
  case ISD::UINT_TO_FP:
  case ISD::VP_UINT_TO_FP:
  case ISD::FCANONICALIZE:
    SplitVecRes_UnaryOp(N, Lo, Hi);
    break;
  case ISD::ADDRSPACECAST:
    SplitVecRes_ADDRSPACECAST(N, Lo, Hi);
    break;
  case ISD::FFREXP:
    SplitVecRes_FFREXP(N, ResNo, Lo, Hi);
    break;

  case ISD::ANY_EXTEND:
  case ISD::SIGN_EXTEND:
  case ISD::ZERO_EXTEND:
  case ISD::VP_SIGN_EXTEND:
  case ISD::VP_ZERO_EXTEND:
    SplitVecRes_ExtendOp(N, Lo, Hi);
    break;

  case ISD::ADD: case ISD::VP_ADD:
  case ISD::SUB: case ISD::VP_SUB:
  case ISD::MUL: case ISD::VP_MUL:
  case ISD::MULHS:
  case ISD::MULHU:
  case ISD::AVGCEILS:
  case ISD::AVGCEILU:
  case ISD::AVGFLOORS:
  case ISD::AVGFLOORU:
  case ISD::FADD: case ISD::VP_FADD:
  case ISD::FSUB: case ISD::VP_FSUB:
  case ISD::FMUL: case ISD::VP_FMUL:
  case ISD::FMINNUM:
  case ISD::FMINNUM_IEEE:
  case ISD::VP_FMINNUM:
  case ISD::FMAXNUM:
  case ISD::FMAXNUM_IEEE:
  case ISD::VP_FMAXNUM:
  case ISD::FMINIMUM:
  case ISD::VP_FMINIMUM:
  case ISD::FMAXIMUM:
  case ISD::VP_FMAXIMUM:
  case ISD::SDIV: case ISD::VP_SDIV:
  case ISD::UDIV: case ISD::VP_UDIV:
  case ISD::FDIV: case ISD::VP_FDIV:
  case ISD::FPOW:
  case ISD::AND: case ISD::VP_AND:
  case ISD::OR: case ISD::VP_OR:
  case ISD::XOR: case ISD::VP_XOR:
  case ISD::SHL: case ISD::VP_SHL:
  case ISD::SRA: case ISD::VP_SRA:
  case ISD::SRL: case ISD::VP_SRL:
  case ISD::UREM: case ISD::VP_UREM:
  case ISD::SREM: case ISD::VP_SREM:
  case ISD::FREM: case ISD::VP_FREM:
  case ISD::SMIN: case ISD::VP_SMIN:
  case ISD::SMAX: case ISD::VP_SMAX:
  case ISD::UMIN: case ISD::VP_UMIN:
  case ISD::UMAX: case ISD::VP_UMAX:
  case ISD::SADDSAT: case ISD::VP_SADDSAT:
  case ISD::UADDSAT: case ISD::VP_UADDSAT:
  case ISD::SSUBSAT: case ISD::VP_SSUBSAT:
  case ISD::USUBSAT: case ISD::VP_USUBSAT:
  case ISD::SSHLSAT:
  case ISD::USHLSAT:
  case ISD::ROTL:
  case ISD::ROTR:
  case ISD::VP_FCOPYSIGN:
    SplitVecRes_BinOp(N, Lo, Hi);
    break;
  case ISD::FMA: case ISD::VP_FMA:
  case ISD::FSHL:
  case ISD::VP_FSHL:
  case ISD::FSHR:
  case ISD::VP_FSHR:
    SplitVecRes_TernaryOp(N, Lo, Hi);
    break;

  case ISD::SCMP: case ISD::UCMP:
    SplitVecRes_CMP(N, Lo, Hi);
    break;

#define DAG_INSTRUCTION(NAME, NARG, ROUND_MODE, INTRINSIC, DAGN)               \
  case ISD::STRICT_##DAGN:
#include "llvm/IR/ConstrainedOps.def"
    SplitVecRes_StrictFPOp(N, Lo, Hi);
    break;

  case ISD::FP_TO_UINT_SAT:
  case ISD::FP_TO_SINT_SAT:
    SplitVecRes_FP_TO_XINT_SAT(N, Lo, Hi);
    break;

  case ISD::UADDO:
  case ISD::SADDO:
  case ISD::USUBO:
  case ISD::SSUBO:
  case ISD::UMULO:
  case ISD::SMULO:
    SplitVecRes_OverflowOp(N, ResNo, Lo, Hi);
    break;
  case ISD::SMULFIX:
  case ISD::SMULFIXSAT:
  case ISD::UMULFIX:
  case ISD::UMULFIXSAT:
  case ISD::SDIVFIX:
  case ISD::SDIVFIXSAT:
  case ISD::UDIVFIX:
  case ISD::UDIVFIXSAT:
    SplitVecRes_FIX(N, Lo, Hi);
    break;
  case ISD::EXPERIMENTAL_VP_REVERSE:
    SplitVecRes_VP_REVERSE(N, Lo, Hi);
    break;
  }

  // If Lo/Hi is null, the sub-method took care of registering results etc.
  if (Lo.getNode())
    SetSplitVector(SDValue(N, ResNo), Lo, Hi);
}

void DAGTypeLegalizer::IncrementPointer(MemSDNode *N, EVT MemVT,
                                        MachinePointerInfo &MPI, SDValue &Ptr,
                                        uint64_t *ScaledOffset) {
  SDLoc DL(N);
  unsigned IncrementSize = MemVT.getSizeInBits().getKnownMinValue() / 8;

  if (MemVT.isScalableVector()) {
    SDNodeFlags Flags;
    SDValue BytesIncrement = DAG.getVScale(
        DL, Ptr.getValueType(),
        APInt(Ptr.getValueSizeInBits().getFixedValue(), IncrementSize));
    MPI = MachinePointerInfo(N->getPointerInfo().getAddrSpace());
    Flags.setNoUnsignedWrap(true);
    if (ScaledOffset)
      *ScaledOffset += IncrementSize;
    Ptr = DAG.getNode(ISD::ADD, DL, Ptr.getValueType(), Ptr, BytesIncrement,
                      Flags);
  } else {
    MPI = N->getPointerInfo().getWithOffset(IncrementSize);
    // Increment the pointer to the other half.
    Ptr = DAG.getObjectPtrOffset(DL, Ptr, TypeSize::getFixed(IncrementSize));
  }
}

std::pair<SDValue, SDValue> DAGTypeLegalizer::SplitMask(SDValue Mask) {
  return SplitMask(Mask, SDLoc(Mask));
}

std::pair<SDValue, SDValue> DAGTypeLegalizer::SplitMask(SDValue Mask,
                                                        const SDLoc &DL) {
  SDValue MaskLo, MaskHi;
  EVT MaskVT = Mask.getValueType();
  if (getTypeAction(MaskVT) == TargetLowering::TypeSplitVector)
    GetSplitVector(Mask, MaskLo, MaskHi);
  else
    std::tie(MaskLo, MaskHi) = DAG.SplitVector(Mask, DL);
  return std::make_pair(MaskLo, MaskHi);
}

void DAGTypeLegalizer::SplitVecRes_BinOp(SDNode *N, SDValue &Lo, SDValue &Hi) {
  SDValue LHSLo, LHSHi;
  GetSplitVector(N->getOperand(0), LHSLo, LHSHi);
  SDValue RHSLo, RHSHi;
  GetSplitVector(N->getOperand(1), RHSLo, RHSHi);
  SDLoc dl(N);

  const SDNodeFlags Flags = N->getFlags();
  unsigned Opcode = N->getOpcode();
  if (N->getNumOperands() == 2) {
    Lo = DAG.getNode(Opcode, dl, LHSLo.getValueType(), LHSLo, RHSLo, Flags);
    Hi = DAG.getNode(Opcode, dl, LHSHi.getValueType(), LHSHi, RHSHi, Flags);
    return;
  }

  assert(N->getNumOperands() == 4 && "Unexpected number of operands!");
  assert(N->isVPOpcode() && "Expected VP opcode");

  SDValue MaskLo, MaskHi;
  std::tie(MaskLo, MaskHi) = SplitMask(N->getOperand(2));

  SDValue EVLLo, EVLHi;
  std::tie(EVLLo, EVLHi) =
      DAG.SplitEVL(N->getOperand(3), N->getValueType(0), dl);

  Lo = DAG.getNode(Opcode, dl, LHSLo.getValueType(),
                   {LHSLo, RHSLo, MaskLo, EVLLo}, Flags);
  Hi = DAG.getNode(Opcode, dl, LHSHi.getValueType(),
                   {LHSHi, RHSHi, MaskHi, EVLHi}, Flags);
}

void DAGTypeLegalizer::SplitVecRes_TernaryOp(SDNode *N, SDValue &Lo,
                                             SDValue &Hi) {
  SDValue Op0Lo, Op0Hi;
  GetSplitVector(N->getOperand(0), Op0Lo, Op0Hi);
  SDValue Op1Lo, Op1Hi;
  GetSplitVector(N->getOperand(1), Op1Lo, Op1Hi);
  SDValue Op2Lo, Op2Hi;
  GetSplitVector(N->getOperand(2), Op2Lo, Op2Hi);
  SDLoc dl(N);

  const SDNodeFlags Flags = N->getFlags();
  unsigned Opcode = N->getOpcode();
  if (N->getNumOperands() == 3) {
    Lo = DAG.getNode(Opcode, dl, Op0Lo.getValueType(), Op0Lo, Op1Lo, Op2Lo, Flags);
    Hi = DAG.getNode(Opcode, dl, Op0Hi.getValueType(), Op0Hi, Op1Hi, Op2Hi, Flags);
    return;
  }

  assert(N->getNumOperands() == 5 && "Unexpected number of operands!");
  assert(N->isVPOpcode() && "Expected VP opcode");

  SDValue MaskLo, MaskHi;
  std::tie(MaskLo, MaskHi) = SplitMask(N->getOperand(3));

  SDValue EVLLo, EVLHi;
  std::tie(EVLLo, EVLHi) =
      DAG.SplitEVL(N->getOperand(4), N->getValueType(0), dl);

  Lo = DAG.getNode(Opcode, dl, Op0Lo.getValueType(),
                   {Op0Lo, Op1Lo, Op2Lo, MaskLo, EVLLo}, Flags);
  Hi = DAG.getNode(Opcode, dl, Op0Hi.getValueType(),
                   {Op0Hi, Op1Hi, Op2Hi, MaskHi, EVLHi}, Flags);
}

void DAGTypeLegalizer::SplitVecRes_CMP(SDNode *N, SDValue &Lo, SDValue &Hi) {
  LLVMContext &Ctxt = *DAG.getContext();
  SDLoc dl(N);

  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  SDValue LHSLo, LHSHi, RHSLo, RHSHi;
  if (getTypeAction(LHS.getValueType()) == TargetLowering::TypeSplitVector) {
    GetSplitVector(LHS, LHSLo, LHSHi);
    GetSplitVector(RHS, RHSLo, RHSHi);
  } else {
    std::tie(LHSLo, LHSHi) = DAG.SplitVector(LHS, dl);
    std::tie(RHSLo, RHSHi) = DAG.SplitVector(RHS, dl);
  }

  EVT SplitResVT = N->getValueType(0).getHalfNumVectorElementsVT(Ctxt);
  Lo = DAG.getNode(N->getOpcode(), dl, SplitResVT, LHSLo, RHSLo);
  Hi = DAG.getNode(N->getOpcode(), dl, SplitResVT, LHSHi, RHSHi);
}

void DAGTypeLegalizer::SplitVecRes_FIX(SDNode *N, SDValue &Lo, SDValue &Hi) {
  SDValue LHSLo, LHSHi;
  GetSplitVector(N->getOperand(0), LHSLo, LHSHi);
  SDValue RHSLo, RHSHi;
  GetSplitVector(N->getOperand(1), RHSLo, RHSHi);
  SDLoc dl(N);
  SDValue Op2 = N->getOperand(2);

  unsigned Opcode = N->getOpcode();
  Lo = DAG.getNode(Opcode, dl, LHSLo.getValueType(), LHSLo, RHSLo, Op2,
                   N->getFlags());
  Hi = DAG.getNode(Opcode, dl, LHSHi.getValueType(), LHSHi, RHSHi, Op2,
                   N->getFlags());
}

void DAGTypeLegalizer::SplitVecRes_BITCAST(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  // We know the result is a vector.  The input may be either a vector or a
  // scalar value.
  EVT LoVT, HiVT;
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));
  SDLoc dl(N);

  SDValue InOp = N->getOperand(0);
  EVT InVT = InOp.getValueType();

  // Handle some special cases efficiently.
  switch (getTypeAction(InVT)) {
  case TargetLowering::TypeLegal:
  case TargetLowering::TypePromoteInteger:
  case TargetLowering::TypePromoteFloat:
  case TargetLowering::TypeSoftPromoteHalf:
  case TargetLowering::TypeSoftenFloat:
  case TargetLowering::TypeScalarizeVector:
  case TargetLowering::TypeWidenVector:
    break;
  case TargetLowering::TypeExpandInteger:
  case TargetLowering::TypeExpandFloat:
    // A scalar to vector conversion, where the scalar needs expansion.
    // If the vector is being split in two then we can just convert the
    // expanded pieces.
    if (LoVT == HiVT) {
      GetExpandedOp(InOp, Lo, Hi);
      if (DAG.getDataLayout().isBigEndian())
        std::swap(Lo, Hi);
      Lo = DAG.getNode(ISD::BITCAST, dl, LoVT, Lo);
      Hi = DAG.getNode(ISD::BITCAST, dl, HiVT, Hi);
      return;
    }
    break;
  case TargetLowering::TypeSplitVector:
    // If the input is a vector that needs to be split, convert each split
    // piece of the input now.
    GetSplitVector(InOp, Lo, Hi);
    Lo = DAG.getNode(ISD::BITCAST, dl, LoVT, Lo);
    Hi = DAG.getNode(ISD::BITCAST, dl, HiVT, Hi);
    return;
  case TargetLowering::TypeScalarizeScalableVector:
    report_fatal_error("Scalarization of scalable vectors is not supported.");
  }

  if (LoVT.isScalableVector()) {
    auto [InLo, InHi] = DAG.SplitVectorOperand(N, 0);
    Lo = DAG.getNode(ISD::BITCAST, dl, LoVT, InLo);
    Hi = DAG.getNode(ISD::BITCAST, dl, HiVT, InHi);
    return;
  }

  // In the general case, convert the input to an integer and split it by hand.
  EVT LoIntVT = EVT::getIntegerVT(*DAG.getContext(), LoVT.getSizeInBits());
  EVT HiIntVT = EVT::getIntegerVT(*DAG.getContext(), HiVT.getSizeInBits());
  if (DAG.getDataLayout().isBigEndian())
    std::swap(LoIntVT, HiIntVT);

  SplitInteger(BitConvertToInteger(InOp), LoIntVT, HiIntVT, Lo, Hi);

  if (DAG.getDataLayout().isBigEndian())
    std::swap(Lo, Hi);
  Lo = DAG.getNode(ISD::BITCAST, dl, LoVT, Lo);
  Hi = DAG.getNode(ISD::BITCAST, dl, HiVT, Hi);
}

void DAGTypeLegalizer::SplitVecRes_BUILD_VECTOR(SDNode *N, SDValue &Lo,
                                                SDValue &Hi) {
  EVT LoVT, HiVT;
  SDLoc dl(N);
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));
  unsigned LoNumElts = LoVT.getVectorNumElements();
  SmallVector<SDValue, 8> LoOps(N->op_begin(), N->op_begin()+LoNumElts);
  Lo = DAG.getBuildVector(LoVT, dl, LoOps);

  SmallVector<SDValue, 8> HiOps(N->op_begin()+LoNumElts, N->op_end());
  Hi = DAG.getBuildVector(HiVT, dl, HiOps);
}

void DAGTypeLegalizer::SplitVecRes_CONCAT_VECTORS(SDNode *N, SDValue &Lo,
                                                  SDValue &Hi) {
  assert(!(N->getNumOperands() & 1) && "Unsupported CONCAT_VECTORS");
  SDLoc dl(N);
  unsigned NumSubvectors = N->getNumOperands() / 2;
  if (NumSubvectors == 1) {
    Lo = N->getOperand(0);
    Hi = N->getOperand(1);
    return;
  }

  EVT LoVT, HiVT;
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));

  SmallVector<SDValue, 8> LoOps(N->op_begin(), N->op_begin()+NumSubvectors);
  Lo = DAG.getNode(ISD::CONCAT_VECTORS, dl, LoVT, LoOps);

  SmallVector<SDValue, 8> HiOps(N->op_begin()+NumSubvectors, N->op_end());
  Hi = DAG.getNode(ISD::CONCAT_VECTORS, dl, HiVT, HiOps);
}

void DAGTypeLegalizer::SplitVecRes_EXTRACT_SUBVECTOR(SDNode *N, SDValue &Lo,
                                                     SDValue &Hi) {
  SDValue Vec = N->getOperand(0);
  SDValue Idx = N->getOperand(1);
  SDLoc dl(N);

  EVT LoVT, HiVT;
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));

  Lo = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, LoVT, Vec, Idx);
  uint64_t IdxVal = Idx->getAsZExtVal();
  Hi = DAG.getNode(
      ISD::EXTRACT_SUBVECTOR, dl, HiVT, Vec,
      DAG.getVectorIdxConstant(IdxVal + LoVT.getVectorMinNumElements(), dl));
}

void DAGTypeLegalizer::SplitVecRes_INSERT_SUBVECTOR(SDNode *N, SDValue &Lo,
                                                    SDValue &Hi) {
  SDValue Vec = N->getOperand(0);
  SDValue SubVec = N->getOperand(1);
  SDValue Idx = N->getOperand(2);
  SDLoc dl(N);
  GetSplitVector(Vec, Lo, Hi);

  EVT VecVT = Vec.getValueType();
  EVT LoVT = Lo.getValueType();
  EVT SubVecVT = SubVec.getValueType();
  unsigned VecElems = VecVT.getVectorMinNumElements();
  unsigned SubElems = SubVecVT.getVectorMinNumElements();
  unsigned LoElems = LoVT.getVectorMinNumElements();

  // If we know the index is in the first half, and we know the subvector
  // doesn't cross the boundary between the halves, we can avoid spilling the
  // vector, and insert into the lower half of the split vector directly.
  unsigned IdxVal = Idx->getAsZExtVal();
  if (IdxVal + SubElems <= LoElems) {
    Lo = DAG.getNode(ISD::INSERT_SUBVECTOR, dl, LoVT, Lo, SubVec, Idx);
    return;
  }
  // Similarly if the subvector is fully in the high half, but mind that we
  // can't tell whether a fixed-length subvector is fully within the high half
  // of a scalable vector.
  if (VecVT.isScalableVector() == SubVecVT.isScalableVector() &&
      IdxVal >= LoElems && IdxVal + SubElems <= VecElems) {
    Hi = DAG.getNode(ISD::INSERT_SUBVECTOR, dl, Hi.getValueType(), Hi, SubVec,
                     DAG.getVectorIdxConstant(IdxVal - LoElems, dl));
    return;
  }

  // Spill the vector to the stack.
  // In cases where the vector is illegal it will be broken down into parts
  // and stored in parts - we should use the alignment for the smallest part.
  Align SmallestAlign = DAG.getReducedAlign(VecVT, /*UseABI=*/false);
  SDValue StackPtr =
      DAG.CreateStackTemporary(VecVT.getStoreSize(), SmallestAlign);
  auto &MF = DAG.getMachineFunction();
  auto FrameIndex = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();
  auto PtrInfo = MachinePointerInfo::getFixedStack(MF, FrameIndex);

  SDValue Store = DAG.getStore(DAG.getEntryNode(), dl, Vec, StackPtr, PtrInfo,
                               SmallestAlign);

  // Store the new subvector into the specified index.
  SDValue SubVecPtr =
      TLI.getVectorSubVecPointer(DAG, StackPtr, VecVT, SubVecVT, Idx);
  Store = DAG.getStore(Store, dl, SubVec, SubVecPtr,
                       MachinePointerInfo::getUnknownStack(MF));

  // Load the Lo part from the stack slot.
  Lo = DAG.getLoad(Lo.getValueType(), dl, Store, StackPtr, PtrInfo,
                   SmallestAlign);

  // Increment the pointer to the other part.
  auto *Load = cast<LoadSDNode>(Lo);
  MachinePointerInfo MPI = Load->getPointerInfo();
  IncrementPointer(Load, LoVT, MPI, StackPtr);

  // Load the Hi part from the stack slot.
  Hi = DAG.getLoad(Hi.getValueType(), dl, Store, StackPtr, MPI, SmallestAlign);
}

// Handle splitting an FP where the second operand does not match the first
// type. The second operand may be a scalar, or a vector that has exactly as
// many elements as the first
void DAGTypeLegalizer::SplitVecRes_FPOp_MultiType(SDNode *N, SDValue &Lo,
                                                  SDValue &Hi) {
  SDValue LHSLo, LHSHi;
  GetSplitVector(N->getOperand(0), LHSLo, LHSHi);
  SDLoc DL(N);

  SDValue RHSLo, RHSHi;
  SDValue RHS = N->getOperand(1);
  EVT RHSVT = RHS.getValueType();
  if (RHSVT.isVector()) {
    if (getTypeAction(RHSVT) == TargetLowering::TypeSplitVector)
      GetSplitVector(RHS, RHSLo, RHSHi);
    else
      std::tie(RHSLo, RHSHi) = DAG.SplitVector(RHS, SDLoc(RHS));

    Lo = DAG.getNode(N->getOpcode(), DL, LHSLo.getValueType(), LHSLo, RHSLo);
    Hi = DAG.getNode(N->getOpcode(), DL, LHSHi.getValueType(), LHSHi, RHSHi);
  } else {
    Lo = DAG.getNode(N->getOpcode(), DL, LHSLo.getValueType(), LHSLo, RHS);
    Hi = DAG.getNode(N->getOpcode(), DL, LHSHi.getValueType(), LHSHi, RHS);
  }
}

void DAGTypeLegalizer::SplitVecRes_IS_FPCLASS(SDNode *N, SDValue &Lo,
                                              SDValue &Hi) {
  SDLoc DL(N);
  SDValue ArgLo, ArgHi;
  SDValue Test = N->getOperand(1);
  SDValue FpValue = N->getOperand(0);
  if (getTypeAction(FpValue.getValueType()) == TargetLowering::TypeSplitVector)
    GetSplitVector(FpValue, ArgLo, ArgHi);
  else
    std::tie(ArgLo, ArgHi) = DAG.SplitVector(FpValue, SDLoc(FpValue));
  EVT LoVT, HiVT;
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));

  Lo = DAG.getNode(ISD::IS_FPCLASS, DL, LoVT, ArgLo, Test, N->getFlags());
  Hi = DAG.getNode(ISD::IS_FPCLASS, DL, HiVT, ArgHi, Test, N->getFlags());
}

void DAGTypeLegalizer::SplitVecRes_InregOp(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  SDValue LHSLo, LHSHi;
  GetSplitVector(N->getOperand(0), LHSLo, LHSHi);
  SDLoc dl(N);

  EVT LoVT, HiVT;
  std::tie(LoVT, HiVT) =
    DAG.GetSplitDestVTs(cast<VTSDNode>(N->getOperand(1))->getVT());

  Lo = DAG.getNode(N->getOpcode(), dl, LHSLo.getValueType(), LHSLo,
                   DAG.getValueType(LoVT));
  Hi = DAG.getNode(N->getOpcode(), dl, LHSHi.getValueType(), LHSHi,
                   DAG.getValueType(HiVT));
}

void DAGTypeLegalizer::SplitVecRes_ExtVecInRegOp(SDNode *N, SDValue &Lo,
                                                 SDValue &Hi) {
  unsigned Opcode = N->getOpcode();
  SDValue N0 = N->getOperand(0);

  SDLoc dl(N);
  SDValue InLo, InHi;

  if (getTypeAction(N0.getValueType()) == TargetLowering::TypeSplitVector)
    GetSplitVector(N0, InLo, InHi);
  else
    std::tie(InLo, InHi) = DAG.SplitVectorOperand(N, 0);

  EVT InLoVT = InLo.getValueType();
  unsigned InNumElements = InLoVT.getVectorNumElements();

  EVT OutLoVT, OutHiVT;
  std::tie(OutLoVT, OutHiVT) = DAG.GetSplitDestVTs(N->getValueType(0));
  unsigned OutNumElements = OutLoVT.getVectorNumElements();
  assert((2 * OutNumElements) <= InNumElements &&
         "Illegal extend vector in reg split");

  // *_EXTEND_VECTOR_INREG instructions extend the lowest elements of the
  // input vector (i.e. we only use InLo):
  // OutLo will extend the first OutNumElements from InLo.
  // OutHi will extend the next OutNumElements from InLo.

  // Shuffle the elements from InLo for OutHi into the bottom elements to
  // create a 'fake' InHi.
  SmallVector<int, 8> SplitHi(InNumElements, -1);
  for (unsigned i = 0; i != OutNumElements; ++i)
    SplitHi[i] = i + OutNumElements;
  InHi = DAG.getVectorShuffle(InLoVT, dl, InLo, DAG.getUNDEF(InLoVT), SplitHi);

  Lo = DAG.getNode(Opcode, dl, OutLoVT, InLo);
  Hi = DAG.getNode(Opcode, dl, OutHiVT, InHi);
}

void DAGTypeLegalizer::SplitVecRes_StrictFPOp(SDNode *N, SDValue &Lo,
                                              SDValue &Hi) {
  unsigned NumOps = N->getNumOperands();
  SDValue Chain = N->getOperand(0);
  EVT LoVT, HiVT;
  SDLoc dl(N);
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));

  SmallVector<SDValue, 4> OpsLo(NumOps);
  SmallVector<SDValue, 4> OpsHi(NumOps);

  // The Chain is the first operand.
  OpsLo[0] = Chain;
  OpsHi[0] = Chain;

  // Now process the remaining operands.
  for (unsigned i = 1; i < NumOps; ++i) {
    SDValue Op = N->getOperand(i);
    SDValue OpLo = Op;
    SDValue OpHi = Op;

    EVT InVT = Op.getValueType();
    if (InVT.isVector()) {
      // If the input also splits, handle it directly for a
      // compile time speedup. Otherwise split it by hand.
      if (getTypeAction(InVT) == TargetLowering::TypeSplitVector)
        GetSplitVector(Op, OpLo, OpHi);
      else
        std::tie(OpLo, OpHi) = DAG.SplitVectorOperand(N, i);
    }

    OpsLo[i] = OpLo;
    OpsHi[i] = OpHi;
  }

  EVT LoValueVTs[] = {LoVT, MVT::Other};
  EVT HiValueVTs[] = {HiVT, MVT::Other};
  Lo = DAG.getNode(N->getOpcode(), dl, DAG.getVTList(LoValueVTs), OpsLo,
                   N->getFlags());
  Hi = DAG.getNode(N->getOpcode(), dl, DAG.getVTList(HiValueVTs), OpsHi,
                   N->getFlags());

  // Build a factor node to remember that this Op is independent of the
  // other one.
  Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                      Lo.getValue(1), Hi.getValue(1));

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Chain);
}

SDValue DAGTypeLegalizer::UnrollVectorOp_StrictFP(SDNode *N, unsigned ResNE) {
  SDValue Chain = N->getOperand(0);
  EVT VT = N->getValueType(0);
  unsigned NE = VT.getVectorNumElements();
  EVT EltVT = VT.getVectorElementType();
  SDLoc dl(N);

  SmallVector<SDValue, 8> Scalars;
  SmallVector<SDValue, 4> Operands(N->getNumOperands());

  // If ResNE is 0, fully unroll the vector op.
  if (ResNE == 0)
    ResNE = NE;
  else if (NE > ResNE)
    NE = ResNE;

  //The results of each unrolled operation, including the chain.
  EVT ChainVTs[] = {EltVT, MVT::Other};
  SmallVector<SDValue, 8> Chains;

  unsigned i;
  for (i = 0; i != NE; ++i) {
    Operands[0] = Chain;
    for (unsigned j = 1, e = N->getNumOperands(); j != e; ++j) {
      SDValue Operand = N->getOperand(j);
      EVT OperandVT = Operand.getValueType();
      if (OperandVT.isVector()) {
        EVT OperandEltVT = OperandVT.getVectorElementType();
        Operands[j] = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, OperandEltVT,
                                  Operand, DAG.getVectorIdxConstant(i, dl));
      } else {
        Operands[j] = Operand;
      }
    }
    SDValue Scalar = DAG.getNode(N->getOpcode(), dl, ChainVTs, Operands);
    Scalar.getNode()->setFlags(N->getFlags());

    //Add in the scalar as well as its chain value to the
    //result vectors.
    Scalars.push_back(Scalar);
    Chains.push_back(Scalar.getValue(1));
  }

  for (; i < ResNE; ++i)
    Scalars.push_back(DAG.getUNDEF(EltVT));

  // Build a new factor node to connect the chain back together.
  Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Chains);
  ReplaceValueWith(SDValue(N, 1), Chain);

  // Create a new BUILD_VECTOR node
  EVT VecVT = EVT::getVectorVT(*DAG.getContext(), EltVT, ResNE);
  return DAG.getBuildVector(VecVT, dl, Scalars);
}

void DAGTypeLegalizer::SplitVecRes_OverflowOp(SDNode *N, unsigned ResNo,
                                              SDValue &Lo, SDValue &Hi) {
  SDLoc dl(N);
  EVT ResVT = N->getValueType(0);
  EVT OvVT = N->getValueType(1);
  EVT LoResVT, HiResVT, LoOvVT, HiOvVT;
  std::tie(LoResVT, HiResVT) = DAG.GetSplitDestVTs(ResVT);
  std::tie(LoOvVT, HiOvVT) = DAG.GetSplitDestVTs(OvVT);

  SDValue LoLHS, HiLHS, LoRHS, HiRHS;
  if (getTypeAction(ResVT) == TargetLowering::TypeSplitVector) {
    GetSplitVector(N->getOperand(0), LoLHS, HiLHS);
    GetSplitVector(N->getOperand(1), LoRHS, HiRHS);
  } else {
    std::tie(LoLHS, HiLHS) = DAG.SplitVectorOperand(N, 0);
    std::tie(LoRHS, HiRHS) = DAG.SplitVectorOperand(N, 1);
  }

  unsigned Opcode = N->getOpcode();
  SDVTList LoVTs = DAG.getVTList(LoResVT, LoOvVT);
  SDVTList HiVTs = DAG.getVTList(HiResVT, HiOvVT);
  SDNode *LoNode = DAG.getNode(Opcode, dl, LoVTs, LoLHS, LoRHS).getNode();
  SDNode *HiNode = DAG.getNode(Opcode, dl, HiVTs, HiLHS, HiRHS).getNode();
  LoNode->setFlags(N->getFlags());
  HiNode->setFlags(N->getFlags());

  Lo = SDValue(LoNode, ResNo);
  Hi = SDValue(HiNode, ResNo);

  // Replace the other vector result not being explicitly split here.
  unsigned OtherNo = 1 - ResNo;
  EVT OtherVT = N->getValueType(OtherNo);
  if (getTypeAction(OtherVT) == TargetLowering::TypeSplitVector) {
    SetSplitVector(SDValue(N, OtherNo),
                   SDValue(LoNode, OtherNo), SDValue(HiNode, OtherNo));
  } else {
    SDValue OtherVal = DAG.getNode(
        ISD::CONCAT_VECTORS, dl, OtherVT,
        SDValue(LoNode, OtherNo), SDValue(HiNode, OtherNo));
    ReplaceValueWith(SDValue(N, OtherNo), OtherVal);
  }
}

void DAGTypeLegalizer::SplitVecRes_INSERT_VECTOR_ELT(SDNode *N, SDValue &Lo,
                                                     SDValue &Hi) {
  SDValue Vec = N->getOperand(0);
  SDValue Elt = N->getOperand(1);
  SDValue Idx = N->getOperand(2);
  SDLoc dl(N);
  GetSplitVector(Vec, Lo, Hi);

  if (ConstantSDNode *CIdx = dyn_cast<ConstantSDNode>(Idx)) {
    unsigned IdxVal = CIdx->getZExtValue();
    unsigned LoNumElts = Lo.getValueType().getVectorMinNumElements();
    if (IdxVal < LoNumElts) {
      Lo = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl,
                       Lo.getValueType(), Lo, Elt, Idx);
      return;
    } else if (!Vec.getValueType().isScalableVector()) {
      Hi = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, Hi.getValueType(), Hi, Elt,
                       DAG.getVectorIdxConstant(IdxVal - LoNumElts, dl));
      return;
    }
  }

  // Make the vector elements byte-addressable if they aren't already.
  EVT VecVT = Vec.getValueType();
  EVT EltVT = VecVT.getVectorElementType();
  if (!EltVT.isByteSized()) {
    EltVT = EltVT.changeTypeToInteger().getRoundIntegerType(*DAG.getContext());
    VecVT = VecVT.changeElementType(EltVT);
    Vec = DAG.getNode(ISD::ANY_EXTEND, dl, VecVT, Vec);
    // Extend the element type to match if needed.
    if (EltVT.bitsGT(Elt.getValueType()))
      Elt = DAG.getNode(ISD::ANY_EXTEND, dl, EltVT, Elt);
  }

  // Spill the vector to the stack.
  // In cases where the vector is illegal it will be broken down into parts
  // and stored in parts - we should use the alignment for the smallest part.
  Align SmallestAlign = DAG.getReducedAlign(VecVT, /*UseABI=*/false);
  SDValue StackPtr =
      DAG.CreateStackTemporary(VecVT.getStoreSize(), SmallestAlign);
  auto &MF = DAG.getMachineFunction();
  auto FrameIndex = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();
  auto PtrInfo = MachinePointerInfo::getFixedStack(MF, FrameIndex);

  SDValue Store = DAG.getStore(DAG.getEntryNode(), dl, Vec, StackPtr, PtrInfo,
                               SmallestAlign);

  // Store the new element.  This may be larger than the vector element type,
  // so use a truncating store.
  SDValue EltPtr = TLI.getVectorElementPointer(DAG, StackPtr, VecVT, Idx);
  Store = DAG.getTruncStore(
      Store, dl, Elt, EltPtr, MachinePointerInfo::getUnknownStack(MF), EltVT,
      commonAlignment(SmallestAlign,
                      EltVT.getFixedSizeInBits() / 8));

  EVT LoVT, HiVT;
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(VecVT);

  // Load the Lo part from the stack slot.
  Lo = DAG.getLoad(LoVT, dl, Store, StackPtr, PtrInfo, SmallestAlign);

  // Increment the pointer to the other part.
  auto Load = cast<LoadSDNode>(Lo);
  MachinePointerInfo MPI = Load->getPointerInfo();
  IncrementPointer(Load, LoVT, MPI, StackPtr);

  Hi = DAG.getLoad(HiVT, dl, Store, StackPtr, MPI, SmallestAlign);

  // If we adjusted the original type, we need to truncate the results.
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));
  if (LoVT != Lo.getValueType())
    Lo = DAG.getNode(ISD::TRUNCATE, dl, LoVT, Lo);
  if (HiVT != Hi.getValueType())
    Hi = DAG.getNode(ISD::TRUNCATE, dl, HiVT, Hi);
}

void DAGTypeLegalizer::SplitVecRes_STEP_VECTOR(SDNode *N, SDValue &Lo,
                                               SDValue &Hi) {
  EVT LoVT, HiVT;
  SDLoc dl(N);
  assert(N->getValueType(0).isScalableVector() &&
         "Only scalable vectors are supported for STEP_VECTOR");
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));
  SDValue Step = N->getOperand(0);

  Lo = DAG.getNode(ISD::STEP_VECTOR, dl, LoVT, Step);

  // Hi = Lo + (EltCnt * Step)
  EVT EltVT = Step.getValueType();
  APInt StepVal = Step->getAsAPIntVal();
  SDValue StartOfHi =
      DAG.getVScale(dl, EltVT, StepVal * LoVT.getVectorMinNumElements());
  StartOfHi = DAG.getSExtOrTrunc(StartOfHi, dl, HiVT.getVectorElementType());
  StartOfHi = DAG.getNode(ISD::SPLAT_VECTOR, dl, HiVT, StartOfHi);

  Hi = DAG.getNode(ISD::STEP_VECTOR, dl, HiVT, Step);
  Hi = DAG.getNode(ISD::ADD, dl, HiVT, Hi, StartOfHi);
}

void DAGTypeLegalizer::SplitVecRes_ScalarOp(SDNode *N, SDValue &Lo,
                                            SDValue &Hi) {
  EVT LoVT, HiVT;
  SDLoc dl(N);
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));
  Lo = DAG.getNode(N->getOpcode(), dl, LoVT, N->getOperand(0));
  if (N->getOpcode() == ISD::SCALAR_TO_VECTOR) {
    Hi = DAG.getUNDEF(HiVT);
  } else {
    assert(N->getOpcode() == ISD::SPLAT_VECTOR && "Unexpected opcode");
    Hi = Lo;
  }
}

void DAGTypeLegalizer::SplitVecRes_VP_SPLAT(SDNode *N, SDValue &Lo,
                                            SDValue &Hi) {
  SDLoc dl(N);
  auto [LoVT, HiVT] = DAG.GetSplitDestVTs(N->getValueType(0));
  auto [MaskLo, MaskHi] = SplitMask(N->getOperand(1));
  auto [EVLLo, EVLHi] = DAG.SplitEVL(N->getOperand(2), N->getValueType(0), dl);
  Lo = DAG.getNode(N->getOpcode(), dl, LoVT, N->getOperand(0), MaskLo, EVLLo);
  Hi = DAG.getNode(N->getOpcode(), dl, HiVT, N->getOperand(0), MaskHi, EVLHi);
}

void DAGTypeLegalizer::SplitVecRes_LOAD(LoadSDNode *LD, SDValue &Lo,
                                        SDValue &Hi) {
  assert(ISD::isUNINDEXEDLoad(LD) && "Indexed load during type legalization!");
  EVT LoVT, HiVT;
  SDLoc dl(LD);
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(LD->getValueType(0));

  ISD::LoadExtType ExtType = LD->getExtensionType();
  SDValue Ch = LD->getChain();
  SDValue Ptr = LD->getBasePtr();
  SDValue Offset = DAG.getUNDEF(Ptr.getValueType());
  EVT MemoryVT = LD->getMemoryVT();
  MachineMemOperand::Flags MMOFlags = LD->getMemOperand()->getFlags();
  AAMDNodes AAInfo = LD->getAAInfo();

  EVT LoMemVT, HiMemVT;
  std::tie(LoMemVT, HiMemVT) = DAG.GetSplitDestVTs(MemoryVT);

  if (!LoMemVT.isByteSized() || !HiMemVT.isByteSized()) {
    SDValue Value, NewChain;
    std::tie(Value, NewChain) = TLI.scalarizeVectorLoad(LD, DAG);
    std::tie(Lo, Hi) = DAG.SplitVector(Value, dl);
    ReplaceValueWith(SDValue(LD, 1), NewChain);
    return;
  }

  Lo = DAG.getLoad(ISD::UNINDEXED, ExtType, LoVT, dl, Ch, Ptr, Offset,
                   LD->getPointerInfo(), LoMemVT, LD->getOriginalAlign(),
                   MMOFlags, AAInfo);

  MachinePointerInfo MPI;
  IncrementPointer(LD, LoMemVT, MPI, Ptr);

  Hi = DAG.getLoad(ISD::UNINDEXED, ExtType, HiVT, dl, Ch, Ptr, Offset, MPI,
                   HiMemVT, LD->getOriginalAlign(), MMOFlags, AAInfo);

  // Build a factor node to remember that this load is independent of the
  // other one.
  Ch = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo.getValue(1),
                   Hi.getValue(1));

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(LD, 1), Ch);
}

void DAGTypeLegalizer::SplitVecRes_VP_LOAD(VPLoadSDNode *LD, SDValue &Lo,
                                           SDValue &Hi) {
  assert(LD->isUnindexed() && "Indexed VP load during type legalization!");
  EVT LoVT, HiVT;
  SDLoc dl(LD);
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(LD->getValueType(0));

  ISD::LoadExtType ExtType = LD->getExtensionType();
  SDValue Ch = LD->getChain();
  SDValue Ptr = LD->getBasePtr();
  SDValue Offset = LD->getOffset();
  assert(Offset.isUndef() && "Unexpected indexed variable-length load offset");
  Align Alignment = LD->getOriginalAlign();
  SDValue Mask = LD->getMask();
  SDValue EVL = LD->getVectorLength();
  EVT MemoryVT = LD->getMemoryVT();

  EVT LoMemVT, HiMemVT;
  bool HiIsEmpty = false;
  std::tie(LoMemVT, HiMemVT) =
      DAG.GetDependentSplitDestVTs(MemoryVT, LoVT, &HiIsEmpty);

  // Split Mask operand
  SDValue MaskLo, MaskHi;
  if (Mask.getOpcode() == ISD::SETCC) {
    SplitVecRes_SETCC(Mask.getNode(), MaskLo, MaskHi);
  } else {
    if (getTypeAction(Mask.getValueType()) == TargetLowering::TypeSplitVector)
      GetSplitVector(Mask, MaskLo, MaskHi);
    else
      std::tie(MaskLo, MaskHi) = DAG.SplitVector(Mask, dl);
  }

  // Split EVL operand
  SDValue EVLLo, EVLHi;
  std::tie(EVLLo, EVLHi) = DAG.SplitEVL(EVL, LD->getValueType(0), dl);

  MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
      LD->getPointerInfo(), MachineMemOperand::MOLoad,
      LocationSize::beforeOrAfterPointer(), Alignment, LD->getAAInfo(),
      LD->getRanges());

  Lo =
      DAG.getLoadVP(LD->getAddressingMode(), ExtType, LoVT, dl, Ch, Ptr, Offset,
                    MaskLo, EVLLo, LoMemVT, MMO, LD->isExpandingLoad());

  if (HiIsEmpty) {
    // The hi vp_load has zero storage size. We therefore simply set it to
    // the low vp_load and rely on subsequent removal from the chain.
    Hi = Lo;
  } else {
    // Generate hi vp_load.
    Ptr = TLI.IncrementMemoryAddress(Ptr, MaskLo, dl, LoMemVT, DAG,
                                     LD->isExpandingLoad());

    MachinePointerInfo MPI;
    if (LoMemVT.isScalableVector())
      MPI = MachinePointerInfo(LD->getPointerInfo().getAddrSpace());
    else
      MPI = LD->getPointerInfo().getWithOffset(
          LoMemVT.getStoreSize().getFixedValue());

    MMO = DAG.getMachineFunction().getMachineMemOperand(
        MPI, MachineMemOperand::MOLoad, LocationSize::beforeOrAfterPointer(),
        Alignment, LD->getAAInfo(), LD->getRanges());

    Hi = DAG.getLoadVP(LD->getAddressingMode(), ExtType, HiVT, dl, Ch, Ptr,
                       Offset, MaskHi, EVLHi, HiMemVT, MMO,
                       LD->isExpandingLoad());
  }

  // Build a factor node to remember that this load is independent of the
  // other one.
  Ch = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo.getValue(1),
                   Hi.getValue(1));

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(LD, 1), Ch);
}

void DAGTypeLegalizer::SplitVecRes_VP_STRIDED_LOAD(VPStridedLoadSDNode *SLD,
                                                   SDValue &Lo, SDValue &Hi) {
  assert(SLD->isUnindexed() &&
         "Indexed VP strided load during type legalization!");
  assert(SLD->getOffset().isUndef() &&
         "Unexpected indexed variable-length load offset");

  SDLoc DL(SLD);

  EVT LoVT, HiVT;
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(SLD->getValueType(0));

  EVT LoMemVT, HiMemVT;
  bool HiIsEmpty = false;
  std::tie(LoMemVT, HiMemVT) =
      DAG.GetDependentSplitDestVTs(SLD->getMemoryVT(), LoVT, &HiIsEmpty);

  SDValue Mask = SLD->getMask();
  SDValue LoMask, HiMask;
  if (Mask.getOpcode() == ISD::SETCC) {
    SplitVecRes_SETCC(Mask.getNode(), LoMask, HiMask);
  } else {
    if (getTypeAction(Mask.getValueType()) == TargetLowering::TypeSplitVector)
      GetSplitVector(Mask, LoMask, HiMask);
    else
      std::tie(LoMask, HiMask) = DAG.SplitVector(Mask, DL);
  }

  SDValue LoEVL, HiEVL;
  std::tie(LoEVL, HiEVL) =
      DAG.SplitEVL(SLD->getVectorLength(), SLD->getValueType(0), DL);

  // Generate the low vp_strided_load
  Lo = DAG.getStridedLoadVP(
      SLD->getAddressingMode(), SLD->getExtensionType(), LoVT, DL,
      SLD->getChain(), SLD->getBasePtr(), SLD->getOffset(), SLD->getStride(),
      LoMask, LoEVL, LoMemVT, SLD->getMemOperand(), SLD->isExpandingLoad());

  if (HiIsEmpty) {
    // The high vp_strided_load has zero storage size. We therefore simply set
    // it to the low vp_strided_load and rely on subsequent removal from the
    // chain.
    Hi = Lo;
  } else {
    // Generate the high vp_strided_load.
    // To calculate the high base address, we need to sum to the low base
    // address stride number of bytes for each element already loaded by low,
    // that is: Ptr = Ptr + (LoEVL * Stride)
    EVT PtrVT = SLD->getBasePtr().getValueType();
    SDValue Increment =
        DAG.getNode(ISD::MUL, DL, PtrVT, LoEVL,
                    DAG.getSExtOrTrunc(SLD->getStride(), DL, PtrVT));
    SDValue Ptr =
        DAG.getNode(ISD::ADD, DL, PtrVT, SLD->getBasePtr(), Increment);

    Align Alignment = SLD->getOriginalAlign();
    if (LoMemVT.isScalableVector())
      Alignment = commonAlignment(
          Alignment, LoMemVT.getSizeInBits().getKnownMinValue() / 8);

    MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
        MachinePointerInfo(SLD->getPointerInfo().getAddrSpace()),
        MachineMemOperand::MOLoad, LocationSize::beforeOrAfterPointer(),
        Alignment, SLD->getAAInfo(), SLD->getRanges());

    Hi = DAG.getStridedLoadVP(SLD->getAddressingMode(), SLD->getExtensionType(),
                              HiVT, DL, SLD->getChain(), Ptr, SLD->getOffset(),
                              SLD->getStride(), HiMask, HiEVL, HiMemVT, MMO,
                              SLD->isExpandingLoad());
  }

  // Build a factor node to remember that this load is independent of the
  // other one.
  SDValue Ch = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Lo.getValue(1),
                           Hi.getValue(1));

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(SLD, 1), Ch);
}

void DAGTypeLegalizer::SplitVecRes_MLOAD(MaskedLoadSDNode *MLD,
                                         SDValue &Lo, SDValue &Hi) {
  assert(MLD->isUnindexed() && "Indexed masked load during type legalization!");
  EVT LoVT, HiVT;
  SDLoc dl(MLD);
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(MLD->getValueType(0));

  SDValue Ch = MLD->getChain();
  SDValue Ptr = MLD->getBasePtr();
  SDValue Offset = MLD->getOffset();
  assert(Offset.isUndef() && "Unexpected indexed masked load offset");
  SDValue Mask = MLD->getMask();
  SDValue PassThru = MLD->getPassThru();
  Align Alignment = MLD->getOriginalAlign();
  ISD::LoadExtType ExtType = MLD->getExtensionType();

  // Split Mask operand
  SDValue MaskLo, MaskHi;
  if (Mask.getOpcode() == ISD::SETCC) {
    SplitVecRes_SETCC(Mask.getNode(), MaskLo, MaskHi);
  } else {
    if (getTypeAction(Mask.getValueType()) == TargetLowering::TypeSplitVector)
      GetSplitVector(Mask, MaskLo, MaskHi);
    else
      std::tie(MaskLo, MaskHi) = DAG.SplitVector(Mask, dl);
  }

  EVT MemoryVT = MLD->getMemoryVT();
  EVT LoMemVT, HiMemVT;
  bool HiIsEmpty = false;
  std::tie(LoMemVT, HiMemVT) =
      DAG.GetDependentSplitDestVTs(MemoryVT, LoVT, &HiIsEmpty);

  SDValue PassThruLo, PassThruHi;
  if (getTypeAction(PassThru.getValueType()) == TargetLowering::TypeSplitVector)
    GetSplitVector(PassThru, PassThruLo, PassThruHi);
  else
    std::tie(PassThruLo, PassThruHi) = DAG.SplitVector(PassThru, dl);

  MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
      MLD->getPointerInfo(), MachineMemOperand::MOLoad,
      LocationSize::beforeOrAfterPointer(), Alignment, MLD->getAAInfo(),
      MLD->getRanges());

  Lo = DAG.getMaskedLoad(LoVT, dl, Ch, Ptr, Offset, MaskLo, PassThruLo, LoMemVT,
                         MMO, MLD->getAddressingMode(), ExtType,
                         MLD->isExpandingLoad());

  if (HiIsEmpty) {
    // The hi masked load has zero storage size. We therefore simply set it to
    // the low masked load and rely on subsequent removal from the chain.
    Hi = Lo;
  } else {
    // Generate hi masked load.
    Ptr = TLI.IncrementMemoryAddress(Ptr, MaskLo, dl, LoMemVT, DAG,
                                     MLD->isExpandingLoad());

    MachinePointerInfo MPI;
    if (LoMemVT.isScalableVector())
      MPI = MachinePointerInfo(MLD->getPointerInfo().getAddrSpace());
    else
      MPI = MLD->getPointerInfo().getWithOffset(
          LoMemVT.getStoreSize().getFixedValue());

    MMO = DAG.getMachineFunction().getMachineMemOperand(
        MPI, MachineMemOperand::MOLoad, LocationSize::beforeOrAfterPointer(),
        Alignment, MLD->getAAInfo(), MLD->getRanges());

    Hi = DAG.getMaskedLoad(HiVT, dl, Ch, Ptr, Offset, MaskHi, PassThruHi,
                           HiMemVT, MMO, MLD->getAddressingMode(), ExtType,
                           MLD->isExpandingLoad());
  }

  // Build a factor node to remember that this load is independent of the
  // other one.
  Ch = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo.getValue(1),
                   Hi.getValue(1));

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(MLD, 1), Ch);

}

void DAGTypeLegalizer::SplitVecRes_Gather(MemSDNode *N, SDValue &Lo,
                                          SDValue &Hi, bool SplitSETCC) {
  EVT LoVT, HiVT;
  SDLoc dl(N);
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));

  SDValue Ch = N->getChain();
  SDValue Ptr = N->getBasePtr();
  struct Operands {
    SDValue Mask;
    SDValue Index;
    SDValue Scale;
  } Ops = [&]() -> Operands {
    if (auto *MSC = dyn_cast<MaskedGatherSDNode>(N)) {
      return {MSC->getMask(), MSC->getIndex(), MSC->getScale()};
    }
    auto *VPSC = cast<VPGatherSDNode>(N);
    return {VPSC->getMask(), VPSC->getIndex(), VPSC->getScale()};
  }();

  EVT MemoryVT = N->getMemoryVT();
  Align Alignment = N->getOriginalAlign();

  // Split Mask operand
  SDValue MaskLo, MaskHi;
  if (SplitSETCC && Ops.Mask.getOpcode() == ISD::SETCC) {
    SplitVecRes_SETCC(Ops.Mask.getNode(), MaskLo, MaskHi);
  } else {
    std::tie(MaskLo, MaskHi) = SplitMask(Ops.Mask, dl);
  }

  EVT LoMemVT, HiMemVT;
  // Split MemoryVT
  std::tie(LoMemVT, HiMemVT) = DAG.GetSplitDestVTs(MemoryVT);

  SDValue IndexHi, IndexLo;
  if (getTypeAction(Ops.Index.getValueType()) ==
      TargetLowering::TypeSplitVector)
    GetSplitVector(Ops.Index, IndexLo, IndexHi);
  else
    std::tie(IndexLo, IndexHi) = DAG.SplitVector(Ops.Index, dl);

  MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
      N->getPointerInfo(), MachineMemOperand::MOLoad,
      LocationSize::beforeOrAfterPointer(), Alignment, N->getAAInfo(),
      N->getRanges());

  if (auto *MGT = dyn_cast<MaskedGatherSDNode>(N)) {
    SDValue PassThru = MGT->getPassThru();
    SDValue PassThruLo, PassThruHi;
    if (getTypeAction(PassThru.getValueType()) ==
        TargetLowering::TypeSplitVector)
      GetSplitVector(PassThru, PassThruLo, PassThruHi);
    else
      std::tie(PassThruLo, PassThruHi) = DAG.SplitVector(PassThru, dl);

    ISD::LoadExtType ExtType = MGT->getExtensionType();
    ISD::MemIndexType IndexTy = MGT->getIndexType();

    SDValue OpsLo[] = {Ch, PassThruLo, MaskLo, Ptr, IndexLo, Ops.Scale};
    Lo = DAG.getMaskedGather(DAG.getVTList(LoVT, MVT::Other), LoMemVT, dl,
                             OpsLo, MMO, IndexTy, ExtType);

    SDValue OpsHi[] = {Ch, PassThruHi, MaskHi, Ptr, IndexHi, Ops.Scale};
    Hi = DAG.getMaskedGather(DAG.getVTList(HiVT, MVT::Other), HiMemVT, dl,
                             OpsHi, MMO, IndexTy, ExtType);
  } else {
    auto *VPGT = cast<VPGatherSDNode>(N);
    SDValue EVLLo, EVLHi;
    std::tie(EVLLo, EVLHi) =
        DAG.SplitEVL(VPGT->getVectorLength(), MemoryVT, dl);

    SDValue OpsLo[] = {Ch, Ptr, IndexLo, Ops.Scale, MaskLo, EVLLo};
    Lo = DAG.getGatherVP(DAG.getVTList(LoVT, MVT::Other), LoMemVT, dl, OpsLo,
                         MMO, VPGT->getIndexType());

    SDValue OpsHi[] = {Ch, Ptr, IndexHi, Ops.Scale, MaskHi, EVLHi};
    Hi = DAG.getGatherVP(DAG.getVTList(HiVT, MVT::Other), HiMemVT, dl, OpsHi,
                         MMO, VPGT->getIndexType());
  }

  // Build a factor node to remember that this load is independent of the
  // other one.
  Ch = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo.getValue(1),
                   Hi.getValue(1));

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Ch);
}

void DAGTypeLegalizer::SplitVecRes_VECTOR_COMPRESS(SDNode *N, SDValue &Lo,
                                                   SDValue &Hi) {
  // This is not "trivial", as there is a dependency between the two subvectors.
  // Depending on the number of 1s in the mask, the elements from the Hi vector
  // need to be moved to the Lo vector. So we just perform this as one "big"
  // operation and then extract the Lo and Hi vectors from that. This gets rid
  // of VECTOR_COMPRESS and all other operands can be legalized later.
  SDValue Compressed = TLI.expandVECTOR_COMPRESS(N, DAG);
  std::tie(Lo, Hi) = DAG.SplitVector(Compressed, SDLoc(N));
}

void DAGTypeLegalizer::SplitVecRes_SETCC(SDNode *N, SDValue &Lo, SDValue &Hi) {
  assert(N->getValueType(0).isVector() &&
         N->getOperand(0).getValueType().isVector() &&
         "Operand types must be vectors");

  EVT LoVT, HiVT;
  SDLoc DL(N);
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));

  // If the input also splits, handle it directly. Otherwise split it by hand.
  SDValue LL, LH, RL, RH;
  if (getTypeAction(N->getOperand(0).getValueType()) ==
      TargetLowering::TypeSplitVector)
    GetSplitVector(N->getOperand(0), LL, LH);
  else
    std::tie(LL, LH) = DAG.SplitVectorOperand(N, 0);

  if (getTypeAction(N->getOperand(1).getValueType()) ==
      TargetLowering::TypeSplitVector)
    GetSplitVector(N->getOperand(1), RL, RH);
  else
    std::tie(RL, RH) = DAG.SplitVectorOperand(N, 1);

  if (N->getOpcode() == ISD::SETCC) {
    Lo = DAG.getNode(N->getOpcode(), DL, LoVT, LL, RL, N->getOperand(2));
    Hi = DAG.getNode(N->getOpcode(), DL, HiVT, LH, RH, N->getOperand(2));
  } else {
    assert(N->getOpcode() == ISD::VP_SETCC && "Expected VP_SETCC opcode");
    SDValue MaskLo, MaskHi, EVLLo, EVLHi;
    std::tie(MaskLo, MaskHi) = SplitMask(N->getOperand(3));
    std::tie(EVLLo, EVLHi) =
        DAG.SplitEVL(N->getOperand(4), N->getValueType(0), DL);
    Lo = DAG.getNode(N->getOpcode(), DL, LoVT, LL, RL, N->getOperand(2), MaskLo,
                     EVLLo);
    Hi = DAG.getNode(N->getOpcode(), DL, HiVT, LH, RH, N->getOperand(2), MaskHi,
                     EVLHi);
  }
}

void DAGTypeLegalizer::SplitVecRes_UnaryOp(SDNode *N, SDValue &Lo,
                                           SDValue &Hi) {
  // Get the dest types - they may not match the input types, e.g. int_to_fp.
  EVT LoVT, HiVT;
  SDLoc dl(N);
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));

  // If the input also splits, handle it directly for a compile time speedup.
  // Otherwise split it by hand.
  EVT InVT = N->getOperand(0).getValueType();
  if (getTypeAction(InVT) == TargetLowering::TypeSplitVector)
    GetSplitVector(N->getOperand(0), Lo, Hi);
  else
    std::tie(Lo, Hi) = DAG.SplitVectorOperand(N, 0);

  const SDNodeFlags Flags = N->getFlags();
  unsigned Opcode = N->getOpcode();
  if (N->getNumOperands() <= 2) {
    if (Opcode == ISD::FP_ROUND) {
      Lo = DAG.getNode(Opcode, dl, LoVT, Lo, N->getOperand(1), Flags);
      Hi = DAG.getNode(Opcode, dl, HiVT, Hi, N->getOperand(1), Flags);
    } else {
      Lo = DAG.getNode(Opcode, dl, LoVT, Lo, Flags);
      Hi = DAG.getNode(Opcode, dl, HiVT, Hi, Flags);
    }
    return;
  }

  assert(N->getNumOperands() == 3 && "Unexpected number of operands!");
  assert(N->isVPOpcode() && "Expected VP opcode");

  SDValue MaskLo, MaskHi;
  std::tie(MaskLo, MaskHi) = SplitMask(N->getOperand(1));

  SDValue EVLLo, EVLHi;
  std::tie(EVLLo, EVLHi) =
      DAG.SplitEVL(N->getOperand(2), N->getValueType(0), dl);

  Lo = DAG.getNode(Opcode, dl, LoVT, {Lo, MaskLo, EVLLo}, Flags);
  Hi = DAG.getNode(Opcode, dl, HiVT, {Hi, MaskHi, EVLHi}, Flags);
}

void DAGTypeLegalizer::SplitVecRes_ADDRSPACECAST(SDNode *N, SDValue &Lo,
                                                 SDValue &Hi) {
  SDLoc dl(N);
  auto [LoVT, HiVT] = DAG.GetSplitDestVTs(N->getValueType(0));

  // If the input also splits, handle it directly for a compile time speedup.
  // Otherwise split it by hand.
  EVT InVT = N->getOperand(0).getValueType();
  if (getTypeAction(InVT) == TargetLowering::TypeSplitVector)
    GetSplitVector(N->getOperand(0), Lo, Hi);
  else
    std::tie(Lo, Hi) = DAG.SplitVectorOperand(N, 0);

  auto *AddrSpaceCastN = cast<AddrSpaceCastSDNode>(N);
  unsigned SrcAS = AddrSpaceCastN->getSrcAddressSpace();
  unsigned DestAS = AddrSpaceCastN->getDestAddressSpace();
  Lo = DAG.getAddrSpaceCast(dl, LoVT, Lo, SrcAS, DestAS);
  Hi = DAG.getAddrSpaceCast(dl, HiVT, Hi, SrcAS, DestAS);
}

void DAGTypeLegalizer::SplitVecRes_FFREXP(SDNode *N, unsigned ResNo,
                                          SDValue &Lo, SDValue &Hi) {
  SDLoc dl(N);
  auto [LoVT, HiVT] = DAG.GetSplitDestVTs(N->getValueType(0));
  auto [LoVT1, HiVT1] = DAG.GetSplitDestVTs(N->getValueType(1));

  // If the input also splits, handle it directly for a compile time speedup.
  // Otherwise split it by hand.
  EVT InVT = N->getOperand(0).getValueType();
  if (getTypeAction(InVT) == TargetLowering::TypeSplitVector)
    GetSplitVector(N->getOperand(0), Lo, Hi);
  else
    std::tie(Lo, Hi) = DAG.SplitVectorOperand(N, 0);

  Lo = DAG.getNode(N->getOpcode(), dl, {LoVT, LoVT1}, Lo);
  Hi = DAG.getNode(N->getOpcode(), dl, {HiVT, HiVT1}, Hi);
  Lo->setFlags(N->getFlags());
  Hi->setFlags(N->getFlags());

  SDNode *HiNode = Hi.getNode();
  SDNode *LoNode = Lo.getNode();

  // Replace the other vector result not being explicitly split here.
  unsigned OtherNo = 1 - ResNo;
  EVT OtherVT = N->getValueType(OtherNo);
  if (getTypeAction(OtherVT) == TargetLowering::TypeSplitVector) {
    SetSplitVector(SDValue(N, OtherNo), SDValue(LoNode, OtherNo),
                   SDValue(HiNode, OtherNo));
  } else {
    SDValue OtherVal =
        DAG.getNode(ISD::CONCAT_VECTORS, dl, OtherVT, SDValue(LoNode, OtherNo),
                    SDValue(HiNode, OtherNo));
    ReplaceValueWith(SDValue(N, OtherNo), OtherVal);
  }
}

void DAGTypeLegalizer::SplitVecRes_ExtendOp(SDNode *N, SDValue &Lo,
                                            SDValue &Hi) {
  SDLoc dl(N);
  EVT SrcVT = N->getOperand(0).getValueType();
  EVT DestVT = N->getValueType(0);
  EVT LoVT, HiVT;
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(DestVT);

  // We can do better than a generic split operation if the extend is doing
  // more than just doubling the width of the elements and the following are
  // true:
  //   - The number of vector elements is even,
  //   - the source type is legal,
  //   - the type of a split source is illegal,
  //   - the type of an extended (by doubling element size) source is legal, and
  //   - the type of that extended source when split is legal.
  //
  // This won't necessarily completely legalize the operation, but it will
  // more effectively move in the right direction and prevent falling down
  // to scalarization in many cases due to the input vector being split too
  // far.
  if (SrcVT.getVectorElementCount().isKnownEven() &&
      SrcVT.getScalarSizeInBits() * 2 < DestVT.getScalarSizeInBits()) {
    LLVMContext &Ctx = *DAG.getContext();
    EVT NewSrcVT = SrcVT.widenIntegerVectorElementType(Ctx);
    EVT SplitSrcVT = SrcVT.getHalfNumVectorElementsVT(Ctx);

    EVT SplitLoVT, SplitHiVT;
    std::tie(SplitLoVT, SplitHiVT) = DAG.GetSplitDestVTs(NewSrcVT);
    if (TLI.isTypeLegal(SrcVT) && !TLI.isTypeLegal(SplitSrcVT) &&
        TLI.isTypeLegal(NewSrcVT) && TLI.isTypeLegal(SplitLoVT)) {
      LLVM_DEBUG(dbgs() << "Split vector extend via incremental extend:";
                 N->dump(&DAG); dbgs() << "\n");
      if (!N->isVPOpcode()) {
        // Extend the source vector by one step.
        SDValue NewSrc =
            DAG.getNode(N->getOpcode(), dl, NewSrcVT, N->getOperand(0));
        // Get the low and high halves of the new, extended one step, vector.
        std::tie(Lo, Hi) = DAG.SplitVector(NewSrc, dl);
        // Extend those vector halves the rest of the way.
        Lo = DAG.getNode(N->getOpcode(), dl, LoVT, Lo);
        Hi = DAG.getNode(N->getOpcode(), dl, HiVT, Hi);
        return;
      }

      // Extend the source vector by one step.
      SDValue NewSrc =
          DAG.getNode(N->getOpcode(), dl, NewSrcVT, N->getOperand(0),
                      N->getOperand(1), N->getOperand(2));
      // Get the low and high halves of the new, extended one step, vector.
      std::tie(Lo, Hi) = DAG.SplitVector(NewSrc, dl);

      SDValue MaskLo, MaskHi;
      std::tie(MaskLo, MaskHi) = SplitMask(N->getOperand(1));

      SDValue EVLLo, EVLHi;
      std::tie(EVLLo, EVLHi) =
          DAG.SplitEVL(N->getOperand(2), N->getValueType(0), dl);
      // Extend those vector halves the rest of the way.
      Lo = DAG.getNode(N->getOpcode(), dl, LoVT, {Lo, MaskLo, EVLLo});
      Hi = DAG.getNode(N->getOpcode(), dl, HiVT, {Hi, MaskHi, EVLHi});
      return;
    }
  }
  // Fall back to the generic unary operator splitting otherwise.
  SplitVecRes_UnaryOp(N, Lo, Hi);
}

void DAGTypeLegalizer::SplitVecRes_VECTOR_SHUFFLE(ShuffleVectorSDNode *N,
                                                  SDValue &Lo, SDValue &Hi) {
  // The low and high parts of the original input give four input vectors.
  SDValue Inputs[4];
  SDLoc DL(N);
  GetSplitVector(N->getOperand(0), Inputs[0], Inputs[1]);
  GetSplitVector(N->getOperand(1), Inputs[2], Inputs[3]);
  EVT NewVT = Inputs[0].getValueType();
  unsigned NewElts = NewVT.getVectorNumElements();

  auto &&IsConstant = [](const SDValue &N) {
    APInt SplatValue;
    return N.getResNo() == 0 &&
           (ISD::isConstantSplatVector(N.getNode(), SplatValue) ||
            ISD::isBuildVectorOfConstantSDNodes(N.getNode()));
  };
  auto &&BuildVector = [NewElts, &DAG = DAG, NewVT, &DL](SDValue &Input1,
                                                         SDValue &Input2,
                                                         ArrayRef<int> Mask) {
    assert(Input1->getOpcode() == ISD::BUILD_VECTOR &&
           Input2->getOpcode() == ISD::BUILD_VECTOR &&
           "Expected build vector node.");
    EVT EltVT = NewVT.getVectorElementType();
    SmallVector<SDValue> Ops(NewElts, DAG.getUNDEF(EltVT));
    for (unsigned I = 0; I < NewElts; ++I) {
      if (Mask[I] == PoisonMaskElem)
        continue;
      unsigned Idx = Mask[I];
      if (Idx >= NewElts)
        Ops[I] = Input2.getOperand(Idx - NewElts);
      else
        Ops[I] = Input1.getOperand(Idx);
      // Make the type of all elements the same as the element type.
      if (Ops[I].getValueType().bitsGT(EltVT))
        Ops[I] = DAG.getNode(ISD::TRUNCATE, DL, EltVT, Ops[I]);
    }
    return DAG.getBuildVector(NewVT, DL, Ops);
  };

  // If Lo or Hi uses elements from at most two of the four input vectors, then
  // express it as a vector shuffle of those two inputs.  Otherwise extract the
  // input elements by hand and construct the Lo/Hi output using a BUILD_VECTOR.
  SmallVector<int> OrigMask(N->getMask());
  // Try to pack incoming shuffles/inputs.
  auto &&TryPeekThroughShufflesInputs = [&Inputs, &NewVT, this, NewElts,
                                         &DL](SmallVectorImpl<int> &Mask) {
    // Check if all inputs are shuffles of the same operands or non-shuffles.
    MapVector<std::pair<SDValue, SDValue>, SmallVector<unsigned>> ShufflesIdxs;
    for (unsigned Idx = 0; Idx < std::size(Inputs); ++Idx) {
      SDValue Input = Inputs[Idx];
      auto *Shuffle = dyn_cast<ShuffleVectorSDNode>(Input.getNode());
      if (!Shuffle ||
          Input.getOperand(0).getValueType() != Input.getValueType())
        continue;
      ShufflesIdxs[std::make_pair(Input.getOperand(0), Input.getOperand(1))]
          .push_back(Idx);
      ShufflesIdxs[std::make_pair(Input.getOperand(1), Input.getOperand(0))]
          .push_back(Idx);
    }
    for (auto &P : ShufflesIdxs) {
      if (P.second.size() < 2)
        continue;
      // Use shuffles operands instead of shuffles themselves.
      // 1. Adjust mask.
      for (int &Idx : Mask) {
        if (Idx == PoisonMaskElem)
          continue;
        unsigned SrcRegIdx = Idx / NewElts;
        if (Inputs[SrcRegIdx].isUndef()) {
          Idx = PoisonMaskElem;
          continue;
        }
        auto *Shuffle =
            dyn_cast<ShuffleVectorSDNode>(Inputs[SrcRegIdx].getNode());
        if (!Shuffle || !is_contained(P.second, SrcRegIdx))
          continue;
        int MaskElt = Shuffle->getMaskElt(Idx % NewElts);
        if (MaskElt == PoisonMaskElem) {
          Idx = PoisonMaskElem;
          continue;
        }
        Idx = MaskElt % NewElts +
              P.second[Shuffle->getOperand(MaskElt / NewElts) == P.first.first
                           ? 0
                           : 1] *
                  NewElts;
      }
      // 2. Update inputs.
      Inputs[P.second[0]] = P.first.first;
      Inputs[P.second[1]] = P.first.second;
      // Clear the pair data.
      P.second.clear();
      ShufflesIdxs[std::make_pair(P.first.second, P.first.first)].clear();
    }
    // Check if any concat_vectors can be simplified.
    SmallBitVector UsedSubVector(2 * std::size(Inputs));
    for (int &Idx : Mask) {
      if (Idx == PoisonMaskElem)
        continue;
      unsigned SrcRegIdx = Idx / NewElts;
      if (Inputs[SrcRegIdx].isUndef()) {
        Idx = PoisonMaskElem;
        continue;
      }
      TargetLowering::LegalizeTypeAction TypeAction =
          getTypeAction(Inputs[SrcRegIdx].getValueType());
      if (Inputs[SrcRegIdx].getOpcode() == ISD::CONCAT_VECTORS &&
          Inputs[SrcRegIdx].getNumOperands() == 2 &&
          !Inputs[SrcRegIdx].getOperand(1).isUndef() &&
          (TypeAction == TargetLowering::TypeLegal ||
           TypeAction == TargetLowering::TypeWidenVector))
        UsedSubVector.set(2 * SrcRegIdx + (Idx % NewElts) / (NewElts / 2));
    }
    if (UsedSubVector.count() > 1) {
      SmallVector<SmallVector<std::pair<unsigned, int>, 2>> Pairs;
      for (unsigned I = 0; I < std::size(Inputs); ++I) {
        if (UsedSubVector.test(2 * I) == UsedSubVector.test(2 * I + 1))
          continue;
        if (Pairs.empty() || Pairs.back().size() == 2)
          Pairs.emplace_back();
        if (UsedSubVector.test(2 * I)) {
          Pairs.back().emplace_back(I, 0);
        } else {
          assert(UsedSubVector.test(2 * I + 1) &&
                 "Expected to be used one of the subvectors.");
          Pairs.back().emplace_back(I, 1);
        }
      }
      if (!Pairs.empty() && Pairs.front().size() > 1) {
        // Adjust mask.
        for (int &Idx : Mask) {
          if (Idx == PoisonMaskElem)
            continue;
          unsigned SrcRegIdx = Idx / NewElts;
          auto *It = find_if(
              Pairs, [SrcRegIdx](ArrayRef<std::pair<unsigned, int>> Idxs) {
                return Idxs.front().first == SrcRegIdx ||
                       Idxs.back().first == SrcRegIdx;
              });
          if (It == Pairs.end())
            continue;
          Idx = It->front().first * NewElts + (Idx % NewElts) % (NewElts / 2) +
                (SrcRegIdx == It->front().first ? 0 : (NewElts / 2));
        }
        // Adjust inputs.
        for (ArrayRef<std::pair<unsigned, int>> Idxs : Pairs) {
          Inputs[Idxs.front().first] = DAG.getNode(
              ISD::CONCAT_VECTORS, DL,
              Inputs[Idxs.front().first].getValueType(),
              Inputs[Idxs.front().first].getOperand(Idxs.front().second),
              Inputs[Idxs.back().first].getOperand(Idxs.back().second));
        }
      }
    }
    bool Changed;
    do {
      // Try to remove extra shuffles (except broadcasts) and shuffles with the
      // reused operands.
      Changed = false;
      for (unsigned I = 0; I < std::size(Inputs); ++I) {
        auto *Shuffle = dyn_cast<ShuffleVectorSDNode>(Inputs[I].getNode());
        if (!Shuffle)
          continue;
        if (Shuffle->getOperand(0).getValueType() != NewVT)
          continue;
        int Op = -1;
        if (!Inputs[I].hasOneUse() && Shuffle->getOperand(1).isUndef() &&
            !Shuffle->isSplat()) {
          Op = 0;
        } else if (!Inputs[I].hasOneUse() &&
                   !Shuffle->getOperand(1).isUndef()) {
          // Find the only used operand, if possible.
          for (int &Idx : Mask) {
            if (Idx == PoisonMaskElem)
              continue;
            unsigned SrcRegIdx = Idx / NewElts;
            if (SrcRegIdx != I)
              continue;
            int MaskElt = Shuffle->getMaskElt(Idx % NewElts);
            if (MaskElt == PoisonMaskElem) {
              Idx = PoisonMaskElem;
              continue;
            }
            int OpIdx = MaskElt / NewElts;
            if (Op == -1) {
              Op = OpIdx;
              continue;
            }
            if (Op != OpIdx) {
              Op = -1;
              break;
            }
          }
        }
        if (Op < 0) {
          // Try to check if one of the shuffle operands is used already.
          for (int OpIdx = 0; OpIdx < 2; ++OpIdx) {
            if (Shuffle->getOperand(OpIdx).isUndef())
              continue;
            auto *It = find(Inputs, Shuffle->getOperand(OpIdx));
            if (It == std::end(Inputs))
              continue;
            int FoundOp = std::distance(std::begin(Inputs), It);
            // Found that operand is used already.
            // 1. Fix the mask for the reused operand.
            for (int &Idx : Mask) {
              if (Idx == PoisonMaskElem)
                continue;
              unsigned SrcRegIdx = Idx / NewElts;
              if (SrcRegIdx != I)
                continue;
              int MaskElt = Shuffle->getMaskElt(Idx % NewElts);
              if (MaskElt == PoisonMaskElem) {
                Idx = PoisonMaskElem;
                continue;
              }
              int MaskIdx = MaskElt / NewElts;
              if (OpIdx == MaskIdx)
                Idx = MaskElt % NewElts + FoundOp * NewElts;
            }
            // 2. Set Op to the unused OpIdx.
            Op = (OpIdx + 1) % 2;
            break;
          }
        }
        if (Op >= 0) {
          Changed = true;
          Inputs[I] = Shuffle->getOperand(Op);
          // Adjust mask.
          for (int &Idx : Mask) {
            if (Idx == PoisonMaskElem)
              continue;
            unsigned SrcRegIdx = Idx / NewElts;
            if (SrcRegIdx != I)
              continue;
            int MaskElt = Shuffle->getMaskElt(Idx % NewElts);
            int OpIdx = MaskElt / NewElts;
            if (OpIdx != Op)
              continue;
            Idx = MaskElt % NewElts + SrcRegIdx * NewElts;
          }
        }
      }
    } while (Changed);
  };
  TryPeekThroughShufflesInputs(OrigMask);
  // Proces unique inputs.
  auto &&MakeUniqueInputs = [&Inputs, &IsConstant,
                             NewElts](SmallVectorImpl<int> &Mask) {
    SetVector<SDValue> UniqueInputs;
    SetVector<SDValue> UniqueConstantInputs;
    for (const auto &I : Inputs) {
      if (IsConstant(I))
        UniqueConstantInputs.insert(I);
      else if (!I.isUndef())
        UniqueInputs.insert(I);
    }
    // Adjust mask in case of reused inputs. Also, need to insert constant
    // inputs at first, otherwise it affects the final outcome.
    if (UniqueInputs.size() != std::size(Inputs)) {
      auto &&UniqueVec = UniqueInputs.takeVector();
      auto &&UniqueConstantVec = UniqueConstantInputs.takeVector();
      unsigned ConstNum = UniqueConstantVec.size();
      for (int &Idx : Mask) {
        if (Idx == PoisonMaskElem)
          continue;
        unsigned SrcRegIdx = Idx / NewElts;
        if (Inputs[SrcRegIdx].isUndef()) {
          Idx = PoisonMaskElem;
          continue;
        }
        const auto It = find(UniqueConstantVec, Inputs[SrcRegIdx]);
        if (It != UniqueConstantVec.end()) {
          Idx = (Idx % NewElts) +
                NewElts * std::distance(UniqueConstantVec.begin(), It);
          assert(Idx >= 0 && "Expected defined mask idx.");
          continue;
        }
        const auto RegIt = find(UniqueVec, Inputs[SrcRegIdx]);
        assert(RegIt != UniqueVec.end() && "Cannot find non-const value.");
        Idx = (Idx % NewElts) +
              NewElts * (std::distance(UniqueVec.begin(), RegIt) + ConstNum);
        assert(Idx >= 0 && "Expected defined mask idx.");
      }
      copy(UniqueConstantVec, std::begin(Inputs));
      copy(UniqueVec, std::next(std::begin(Inputs), ConstNum));
    }
  };
  MakeUniqueInputs(OrigMask);
  SDValue OrigInputs[4];
  copy(Inputs, std::begin(OrigInputs));
  for (unsigned High = 0; High < 2; ++High) {
    SDValue &Output = High ? Hi : Lo;

    // Build a shuffle mask for the output, discovering on the fly which
    // input vectors to use as shuffle operands.
    unsigned FirstMaskIdx = High * NewElts;
    SmallVector<int> Mask(NewElts * std::size(Inputs), PoisonMaskElem);
    copy(ArrayRef(OrigMask).slice(FirstMaskIdx, NewElts), Mask.begin());
    assert(!Output && "Expected default initialized initial value.");
    TryPeekThroughShufflesInputs(Mask);
    MakeUniqueInputs(Mask);
    SDValue TmpInputs[4];
    copy(Inputs, std::begin(TmpInputs));
    // Track changes in the output registers.
    int UsedIdx = -1;
    bool SecondIteration = false;
    auto &&AccumulateResults = [&UsedIdx, &SecondIteration](unsigned Idx) {
      if (UsedIdx < 0) {
        UsedIdx = Idx;
        return false;
      }
      if (UsedIdx >= 0 && static_cast<unsigned>(UsedIdx) == Idx)
        SecondIteration = true;
      return SecondIteration;
    };
    processShuffleMasks(
        Mask, std::size(Inputs), std::size(Inputs),
        /*NumOfUsedRegs=*/1,
        [&Output, &DAG = DAG, NewVT]() { Output = DAG.getUNDEF(NewVT); },
        [&Output, &DAG = DAG, NewVT, &DL, &Inputs,
         &BuildVector](ArrayRef<int> Mask, unsigned Idx, unsigned /*Unused*/) {
          if (Inputs[Idx]->getOpcode() == ISD::BUILD_VECTOR)
            Output = BuildVector(Inputs[Idx], Inputs[Idx], Mask);
          else
            Output = DAG.getVectorShuffle(NewVT, DL, Inputs[Idx],
                                          DAG.getUNDEF(NewVT), Mask);
          Inputs[Idx] = Output;
        },
        [&AccumulateResults, &Output, &DAG = DAG, NewVT, &DL, &Inputs,
         &TmpInputs,
         &BuildVector](ArrayRef<int> Mask, unsigned Idx1, unsigned Idx2) {
          if (AccumulateResults(Idx1)) {
            if (Inputs[Idx1]->getOpcode() == ISD::BUILD_VECTOR &&
                Inputs[Idx2]->getOpcode() == ISD::BUILD_VECTOR)
              Output = BuildVector(Inputs[Idx1], Inputs[Idx2], Mask);
            else
              Output = DAG.getVectorShuffle(NewVT, DL, Inputs[Idx1],
                                            Inputs[Idx2], Mask);
          } else {
            if (TmpInputs[Idx1]->getOpcode() == ISD::BUILD_VECTOR &&
                TmpInputs[Idx2]->getOpcode() == ISD::BUILD_VECTOR)
              Output = BuildVector(TmpInputs[Idx1], TmpInputs[Idx2], Mask);
            else
              Output = DAG.getVectorShuffle(NewVT, DL, TmpInputs[Idx1],
                                            TmpInputs[Idx2], Mask);
          }
          Inputs[Idx1] = Output;
        });
    copy(OrigInputs, std::begin(Inputs));
  }
}

void DAGTypeLegalizer::SplitVecRes_VAARG(SDNode *N, SDValue &Lo, SDValue &Hi) {
  EVT OVT = N->getValueType(0);
  EVT NVT = OVT.getHalfNumVectorElementsVT(*DAG.getContext());
  SDValue Chain = N->getOperand(0);
  SDValue Ptr = N->getOperand(1);
  SDValue SV = N->getOperand(2);
  SDLoc dl(N);

  const Align Alignment =
      DAG.getDataLayout().getABITypeAlign(NVT.getTypeForEVT(*DAG.getContext()));

  Lo = DAG.getVAArg(NVT, dl, Chain, Ptr, SV, Alignment.value());
  Hi = DAG.getVAArg(NVT, dl, Lo.getValue(1), Ptr, SV, Alignment.value());
  Chain = Hi.getValue(1);

  // Modified the chain - switch anything that used the old chain to use
  // the new one.
  ReplaceValueWith(SDValue(N, 1), Chain);
}

void DAGTypeLegalizer::SplitVecRes_FP_TO_XINT_SAT(SDNode *N, SDValue &Lo,
                                                  SDValue &Hi) {
  EVT DstVTLo, DstVTHi;
  std::tie(DstVTLo, DstVTHi) = DAG.GetSplitDestVTs(N->getValueType(0));
  SDLoc dl(N);

  SDValue SrcLo, SrcHi;
  EVT SrcVT = N->getOperand(0).getValueType();
  if (getTypeAction(SrcVT) == TargetLowering::TypeSplitVector)
    GetSplitVector(N->getOperand(0), SrcLo, SrcHi);
  else
    std::tie(SrcLo, SrcHi) = DAG.SplitVectorOperand(N, 0);

  Lo = DAG.getNode(N->getOpcode(), dl, DstVTLo, SrcLo, N->getOperand(1));
  Hi = DAG.getNode(N->getOpcode(), dl, DstVTHi, SrcHi, N->getOperand(1));
}

void DAGTypeLegalizer::SplitVecRes_VECTOR_REVERSE(SDNode *N, SDValue &Lo,
                                                  SDValue &Hi) {
  SDValue InLo, InHi;
  GetSplitVector(N->getOperand(0), InLo, InHi);
  SDLoc DL(N);

  Lo = DAG.getNode(ISD::VECTOR_REVERSE, DL, InHi.getValueType(), InHi);
  Hi = DAG.getNode(ISD::VECTOR_REVERSE, DL, InLo.getValueType(), InLo);
}

void DAGTypeLegalizer::SplitVecRes_VECTOR_SPLICE(SDNode *N, SDValue &Lo,
                                                 SDValue &Hi) {
  SDLoc DL(N);

  SDValue Expanded = TLI.expandVectorSplice(N, DAG);
  std::tie(Lo, Hi) = DAG.SplitVector(Expanded, DL);
}

void DAGTypeLegalizer::SplitVecRes_VP_REVERSE(SDNode *N, SDValue &Lo,
                                              SDValue &Hi) {
  EVT VT = N->getValueType(0);
  SDValue Val = N->getOperand(0);
  SDValue Mask = N->getOperand(1);
  SDValue EVL = N->getOperand(2);
  SDLoc DL(N);

  // Fallback to VP_STRIDED_STORE to stack followed by VP_LOAD.
  Align Alignment = DAG.getReducedAlign(VT, /*UseABI=*/false);

  EVT MemVT = EVT::getVectorVT(*DAG.getContext(), VT.getVectorElementType(),
                               VT.getVectorElementCount());
  SDValue StackPtr = DAG.CreateStackTemporary(MemVT.getStoreSize(), Alignment);
  EVT PtrVT = StackPtr.getValueType();
  auto &MF = DAG.getMachineFunction();
  auto FrameIndex = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();
  auto PtrInfo = MachinePointerInfo::getFixedStack(MF, FrameIndex);

  MachineMemOperand *StoreMMO = DAG.getMachineFunction().getMachineMemOperand(
      PtrInfo, MachineMemOperand::MOStore, LocationSize::beforeOrAfterPointer(),
      Alignment);
  MachineMemOperand *LoadMMO = DAG.getMachineFunction().getMachineMemOperand(
      PtrInfo, MachineMemOperand::MOLoad, LocationSize::beforeOrAfterPointer(),
      Alignment);

  unsigned EltWidth = VT.getScalarSizeInBits() / 8;
  SDValue NumElemMinus1 =
      DAG.getNode(ISD::SUB, DL, PtrVT, DAG.getZExtOrTrunc(EVL, DL, PtrVT),
                  DAG.getConstant(1, DL, PtrVT));
  SDValue StartOffset = DAG.getNode(ISD::MUL, DL, PtrVT, NumElemMinus1,
                                    DAG.getConstant(EltWidth, DL, PtrVT));
  SDValue StorePtr = DAG.getNode(ISD::ADD, DL, PtrVT, StackPtr, StartOffset);
  SDValue Stride = DAG.getConstant(-(int64_t)EltWidth, DL, PtrVT);

  SDValue TrueMask = DAG.getBoolConstant(true, DL, Mask.getValueType(), VT);
  SDValue Store = DAG.getStridedStoreVP(DAG.getEntryNode(), DL, Val, StorePtr,
                                        DAG.getUNDEF(PtrVT), Stride, TrueMask,
                                        EVL, MemVT, StoreMMO, ISD::UNINDEXED);

  SDValue Load = DAG.getLoadVP(VT, DL, Store, StackPtr, Mask, EVL, LoadMMO);

  std::tie(Lo, Hi) = DAG.SplitVector(Load, DL);
}

void DAGTypeLegalizer::SplitVecRes_VECTOR_DEINTERLEAVE(SDNode *N) {

  SDValue Op0Lo, Op0Hi, Op1Lo, Op1Hi;
  GetSplitVector(N->getOperand(0), Op0Lo, Op0Hi);
  GetSplitVector(N->getOperand(1), Op1Lo, Op1Hi);
  EVT VT = Op0Lo.getValueType();
  SDLoc DL(N);
  SDValue ResLo = DAG.getNode(ISD::VECTOR_DEINTERLEAVE, DL,
                              DAG.getVTList(VT, VT), Op0Lo, Op0Hi);
  SDValue ResHi = DAG.getNode(ISD::VECTOR_DEINTERLEAVE, DL,
                              DAG.getVTList(VT, VT), Op1Lo, Op1Hi);

  SetSplitVector(SDValue(N, 0), ResLo.getValue(0), ResHi.getValue(0));
  SetSplitVector(SDValue(N, 1), ResLo.getValue(1), ResHi.getValue(1));
}

void DAGTypeLegalizer::SplitVecRes_VECTOR_INTERLEAVE(SDNode *N) {
  SDValue Op0Lo, Op0Hi, Op1Lo, Op1Hi;
  GetSplitVector(N->getOperand(0), Op0Lo, Op0Hi);
  GetSplitVector(N->getOperand(1), Op1Lo, Op1Hi);
  EVT VT = Op0Lo.getValueType();
  SDLoc DL(N);
  SDValue Res[] = {DAG.getNode(ISD::VECTOR_INTERLEAVE, DL,
                               DAG.getVTList(VT, VT), Op0Lo, Op1Lo),
                   DAG.getNode(ISD::VECTOR_INTERLEAVE, DL,
                               DAG.getVTList(VT, VT), Op0Hi, Op1Hi)};

  SetSplitVector(SDValue(N, 0), Res[0].getValue(0), Res[0].getValue(1));
  SetSplitVector(SDValue(N, 1), Res[1].getValue(0), Res[1].getValue(1));
}

//===----------------------------------------------------------------------===//
//  Operand Vector Splitting
//===----------------------------------------------------------------------===//

/// This method is called when the specified operand of the specified node is
/// found to need vector splitting. At this point, all of the result types of
/// the node are known to be legal, but other operands of the node may need
/// legalization as well as the specified one.
bool DAGTypeLegalizer::SplitVectorOperand(SDNode *N, unsigned OpNo) {
  LLVM_DEBUG(dbgs() << "Split node operand: "; N->dump(&DAG));
  SDValue Res = SDValue();

  // See if the target wants to custom split this node.
  if (CustomLowerNode(N, N->getOperand(OpNo).getValueType(), false))
    return false;

  switch (N->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "SplitVectorOperand Op #" << OpNo << ": ";
    N->dump(&DAG);
    dbgs() << "\n";
#endif
    report_fatal_error("Do not know how to split this operator's "
                       "operand!\n");

  case ISD::VP_SETCC:
  case ISD::STRICT_FSETCC:
  case ISD::SETCC:             Res = SplitVecOp_VSETCC(N); break;
  case ISD::BITCAST:           Res = SplitVecOp_BITCAST(N); break;
  case ISD::EXTRACT_SUBVECTOR: Res = SplitVecOp_EXTRACT_SUBVECTOR(N); break;
  case ISD::INSERT_SUBVECTOR:  Res = SplitVecOp_INSERT_SUBVECTOR(N, OpNo); break;
  case ISD::EXTRACT_VECTOR_ELT:Res = SplitVecOp_EXTRACT_VECTOR_ELT(N); break;
  case ISD::CONCAT_VECTORS:    Res = SplitVecOp_CONCAT_VECTORS(N); break;
  case ISD::VP_TRUNCATE:
  case ISD::TRUNCATE:
    Res = SplitVecOp_TruncateHelper(N);
    break;
  case ISD::STRICT_FP_ROUND:
  case ISD::VP_FP_ROUND:
  case ISD::FP_ROUND:          Res = SplitVecOp_FP_ROUND(N); break;
  case ISD::FCOPYSIGN:         Res = SplitVecOp_FPOpDifferentTypes(N); break;
  case ISD::STORE:
    Res = SplitVecOp_STORE(cast<StoreSDNode>(N), OpNo);
    break;
  case ISD::VP_STORE:
    Res = SplitVecOp_VP_STORE(cast<VPStoreSDNode>(N), OpNo);
    break;
  case ISD::EXPERIMENTAL_VP_STRIDED_STORE:
    Res = SplitVecOp_VP_STRIDED_STORE(cast<VPStridedStoreSDNode>(N), OpNo);
    break;
  case ISD::MSTORE:
    Res = SplitVecOp_MSTORE(cast<MaskedStoreSDNode>(N), OpNo);
    break;
  case ISD::MSCATTER:
  case ISD::VP_SCATTER:
    Res = SplitVecOp_Scatter(cast<MemSDNode>(N), OpNo);
    break;
  case ISD::MGATHER:
  case ISD::VP_GATHER:
    Res = SplitVecOp_Gather(cast<MemSDNode>(N), OpNo);
    break;
  case ISD::VSELECT:
    Res = SplitVecOp_VSELECT(N, OpNo);
    break;
  case ISD::STRICT_SINT_TO_FP:
  case ISD::STRICT_UINT_TO_FP:
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:
  case ISD::VP_SINT_TO_FP:
  case ISD::VP_UINT_TO_FP:
    if (N->getValueType(0).bitsLT(
            N->getOperand(N->isStrictFPOpcode() ? 1 : 0).getValueType()))
      Res = SplitVecOp_TruncateHelper(N);
    else
      Res = SplitVecOp_UnaryOp(N);
    break;
  case ISD::FP_TO_SINT_SAT:
  case ISD::FP_TO_UINT_SAT:
    Res = SplitVecOp_FP_TO_XINT_SAT(N);
    break;
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
  case ISD::VP_FP_TO_SINT:
  case ISD::VP_FP_TO_UINT:
  case ISD::STRICT_FP_TO_SINT:
  case ISD::STRICT_FP_TO_UINT:
  case ISD::STRICT_FP_EXTEND:
  case ISD::FP_EXTEND:
  case ISD::SIGN_EXTEND:
  case ISD::ZERO_EXTEND:
  case ISD::ANY_EXTEND:
  case ISD::FTRUNC:
  case ISD::LRINT:
  case ISD::LLRINT:
    Res = SplitVecOp_UnaryOp(N);
    break;
  case ISD::FLDEXP:
    Res = SplitVecOp_FPOpDifferentTypes(N);
    break;

  case ISD::SCMP:
  case ISD::UCMP:
    Res = SplitVecOp_CMP(N);
    break;

  case ISD::ANY_EXTEND_VECTOR_INREG:
  case ISD::SIGN_EXTEND_VECTOR_INREG:
  case ISD::ZERO_EXTEND_VECTOR_INREG:
    Res = SplitVecOp_ExtVecInRegOp(N);
    break;

  case ISD::VECREDUCE_FADD:
  case ISD::VECREDUCE_FMUL:
  case ISD::VECREDUCE_ADD:
  case ISD::VECREDUCE_MUL:
  case ISD::VECREDUCE_AND:
  case ISD::VECREDUCE_OR:
  case ISD::VECREDUCE_XOR:
  case ISD::VECREDUCE_SMAX:
  case ISD::VECREDUCE_SMIN:
  case ISD::VECREDUCE_UMAX:
  case ISD::VECREDUCE_UMIN:
  case ISD::VECREDUCE_FMAX:
  case ISD::VECREDUCE_FMIN:
  case ISD::VECREDUCE_FMAXIMUM:
  case ISD::VECREDUCE_FMINIMUM:
    Res = SplitVecOp_VECREDUCE(N, OpNo);
    break;
  case ISD::VECREDUCE_SEQ_FADD:
  case ISD::VECREDUCE_SEQ_FMUL:
    Res = SplitVecOp_VECREDUCE_SEQ(N);
    break;
  case ISD::VP_REDUCE_FADD:
  case ISD::VP_REDUCE_SEQ_FADD:
  case ISD::VP_REDUCE_FMUL:
  case ISD::VP_REDUCE_SEQ_FMUL:
  case ISD::VP_REDUCE_ADD:
  case ISD::VP_REDUCE_MUL:
  case ISD::VP_REDUCE_AND:
  case ISD::VP_REDUCE_OR:
  case ISD::VP_REDUCE_XOR:
  case ISD::VP_REDUCE_SMAX:
  case ISD::VP_REDUCE_SMIN:
  case ISD::VP_REDUCE_UMAX:
  case ISD::VP_REDUCE_UMIN:
  case ISD::VP_REDUCE_FMAX:
  case ISD::VP_REDUCE_FMIN:
  case ISD::VP_REDUCE_FMAXIMUM:
  case ISD::VP_REDUCE_FMINIMUM:
    Res = SplitVecOp_VP_REDUCE(N, OpNo);
    break;
  case ISD::VP_CTTZ_ELTS:
  case ISD::VP_CTTZ_ELTS_ZERO_UNDEF:
    Res = SplitVecOp_VP_CttzElements(N);
    break;
  }

  // If the result is null, the sub-method took care of registering results etc.
  if (!Res.getNode()) return false;

  // If the result is N, the sub-method updated N in place.  Tell the legalizer
  // core about this.
  if (Res.getNode() == N)
    return true;

  if (N->isStrictFPOpcode())
    assert(Res.getValueType() == N->getValueType(0) && N->getNumValues() == 2 &&
           "Invalid operand expansion");
  else
    assert(Res.getValueType() == N->getValueType(0) && N->getNumValues() == 1 &&
         "Invalid operand expansion");

  ReplaceValueWith(SDValue(N, 0), Res);
  return false;
}

SDValue DAGTypeLegalizer::SplitVecOp_VSELECT(SDNode *N, unsigned OpNo) {
  // The only possibility for an illegal operand is the mask, since result type
  // legalization would have handled this node already otherwise.
  assert(OpNo == 0 && "Illegal operand must be mask");

  SDValue Mask = N->getOperand(0);
  SDValue Src0 = N->getOperand(1);
  SDValue Src1 = N->getOperand(2);
  EVT Src0VT = Src0.getValueType();
  SDLoc DL(N);
  assert(Mask.getValueType().isVector() && "VSELECT without a vector mask?");

  SDValue Lo, Hi;
  GetSplitVector(N->getOperand(0), Lo, Hi);
  assert(Lo.getValueType() == Hi.getValueType() &&
         "Lo and Hi have differing types");

  EVT LoOpVT, HiOpVT;
  std::tie(LoOpVT, HiOpVT) = DAG.GetSplitDestVTs(Src0VT);
  assert(LoOpVT == HiOpVT && "Asymmetric vector split?");

  SDValue LoOp0, HiOp0, LoOp1, HiOp1, LoMask, HiMask;
  std::tie(LoOp0, HiOp0) = DAG.SplitVector(Src0, DL);
  std::tie(LoOp1, HiOp1) = DAG.SplitVector(Src1, DL);
  std::tie(LoMask, HiMask) = DAG.SplitVector(Mask, DL);

  SDValue LoSelect =
    DAG.getNode(ISD::VSELECT, DL, LoOpVT, LoMask, LoOp0, LoOp1);
  SDValue HiSelect =
    DAG.getNode(ISD::VSELECT, DL, HiOpVT, HiMask, HiOp0, HiOp1);

  return DAG.getNode(ISD::CONCAT_VECTORS, DL, Src0VT, LoSelect, HiSelect);
}

SDValue DAGTypeLegalizer::SplitVecOp_VECREDUCE(SDNode *N, unsigned OpNo) {
  EVT ResVT = N->getValueType(0);
  SDValue Lo, Hi;
  SDLoc dl(N);

  SDValue VecOp = N->getOperand(OpNo);
  EVT VecVT = VecOp.getValueType();
  assert(VecVT.isVector() && "Can only split reduce vector operand");
  GetSplitVector(VecOp, Lo, Hi);
  EVT LoOpVT, HiOpVT;
  std::tie(LoOpVT, HiOpVT) = DAG.GetSplitDestVTs(VecVT);

  // Use the appropriate scalar instruction on the split subvectors before
  // reducing the now partially reduced smaller vector.
  unsigned CombineOpc = ISD::getVecReduceBaseOpcode(N->getOpcode());
  SDValue Partial = DAG.getNode(CombineOpc, dl, LoOpVT, Lo, Hi, N->getFlags());
  return DAG.getNode(N->getOpcode(), dl, ResVT, Partial, N->getFlags());
}

SDValue DAGTypeLegalizer::SplitVecOp_VECREDUCE_SEQ(SDNode *N) {
  EVT ResVT = N->getValueType(0);
  SDValue Lo, Hi;
  SDLoc dl(N);

  SDValue AccOp = N->getOperand(0);
  SDValue VecOp = N->getOperand(1);
  SDNodeFlags Flags = N->getFlags();

  EVT VecVT = VecOp.getValueType();
  assert(VecVT.isVector() && "Can only split reduce vector operand");
  GetSplitVector(VecOp, Lo, Hi);
  EVT LoOpVT, HiOpVT;
  std::tie(LoOpVT, HiOpVT) = DAG.GetSplitDestVTs(VecVT);

  // Reduce low half.
  SDValue Partial = DAG.getNode(N->getOpcode(), dl, ResVT, AccOp, Lo, Flags);

  // Reduce high half, using low half result as initial value.
  return DAG.getNode(N->getOpcode(), dl, ResVT, Partial, Hi, Flags);
}

SDValue DAGTypeLegalizer::SplitVecOp_VP_REDUCE(SDNode *N, unsigned OpNo) {
  assert(N->isVPOpcode() && "Expected VP opcode");
  assert(OpNo == 1 && "Can only split reduce vector operand");

  unsigned Opc = N->getOpcode();
  EVT ResVT = N->getValueType(0);
  SDValue Lo, Hi;
  SDLoc dl(N);

  SDValue VecOp = N->getOperand(OpNo);
  EVT VecVT = VecOp.getValueType();
  assert(VecVT.isVector() && "Can only split reduce vector operand");
  GetSplitVector(VecOp, Lo, Hi);

  SDValue MaskLo, MaskHi;
  std::tie(MaskLo, MaskHi) = SplitMask(N->getOperand(2));

  SDValue EVLLo, EVLHi;
  std::tie(EVLLo, EVLHi) = DAG.SplitEVL(N->getOperand(3), VecVT, dl);

  const SDNodeFlags Flags = N->getFlags();

  SDValue ResLo =
      DAG.getNode(Opc, dl, ResVT, {N->getOperand(0), Lo, MaskLo, EVLLo}, Flags);
  return DAG.getNode(Opc, dl, ResVT, {ResLo, Hi, MaskHi, EVLHi}, Flags);
}

SDValue DAGTypeLegalizer::SplitVecOp_UnaryOp(SDNode *N) {
  // The result has a legal vector type, but the input needs splitting.
  EVT ResVT = N->getValueType(0);
  SDValue Lo, Hi;
  SDLoc dl(N);
  GetSplitVector(N->getOperand(N->isStrictFPOpcode() ? 1 : 0), Lo, Hi);
  EVT InVT = Lo.getValueType();

  EVT OutVT = EVT::getVectorVT(*DAG.getContext(), ResVT.getVectorElementType(),
                               InVT.getVectorElementCount());

  if (N->isStrictFPOpcode()) {
    Lo = DAG.getNode(N->getOpcode(), dl, { OutVT, MVT::Other }, 
                     { N->getOperand(0), Lo });
    Hi = DAG.getNode(N->getOpcode(), dl, { OutVT, MVT::Other }, 
                     { N->getOperand(0), Hi });

    // Build a factor node to remember that this operation is independent
    // of the other one.
    SDValue Ch = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo.getValue(1),
                             Hi.getValue(1));
  
    // Legalize the chain result - switch anything that used the old chain to
    // use the new one.
    ReplaceValueWith(SDValue(N, 1), Ch);
  } else if (N->getNumOperands() == 3) {
    assert(N->isVPOpcode() && "Expected VP opcode");
    SDValue MaskLo, MaskHi, EVLLo, EVLHi;
    std::tie(MaskLo, MaskHi) = SplitMask(N->getOperand(1));
    std::tie(EVLLo, EVLHi) =
        DAG.SplitEVL(N->getOperand(2), N->getValueType(0), dl);
    Lo = DAG.getNode(N->getOpcode(), dl, OutVT, Lo, MaskLo, EVLLo);
    Hi = DAG.getNode(N->getOpcode(), dl, OutVT, Hi, MaskHi, EVLHi);
  } else {
    Lo = DAG.getNode(N->getOpcode(), dl, OutVT, Lo);
    Hi = DAG.getNode(N->getOpcode(), dl, OutVT, Hi);
  }

  return DAG.getNode(ISD::CONCAT_VECTORS, dl, ResVT, Lo, Hi);
}

SDValue DAGTypeLegalizer::SplitVecOp_BITCAST(SDNode *N) {
  // For example, i64 = BITCAST v4i16 on alpha.  Typically the vector will
  // end up being split all the way down to individual components.  Convert the
  // split pieces into integers and reassemble.
  EVT ResVT = N->getValueType(0);
  SDValue Lo, Hi;
  GetSplitVector(N->getOperand(0), Lo, Hi);
  SDLoc dl(N);

  if (ResVT.isScalableVector()) {
    auto [LoVT, HiVT] = DAG.GetSplitDestVTs(ResVT);
    Lo = DAG.getNode(ISD::BITCAST, dl, LoVT, Lo);
    Hi = DAG.getNode(ISD::BITCAST, dl, HiVT, Hi);
    return DAG.getNode(ISD::CONCAT_VECTORS, dl, ResVT, Lo, Hi);
  }

  Lo = BitConvertToInteger(Lo);
  Hi = BitConvertToInteger(Hi);

  if (DAG.getDataLayout().isBigEndian())
    std::swap(Lo, Hi);

  return DAG.getNode(ISD::BITCAST, dl, ResVT, JoinIntegers(Lo, Hi));
}

SDValue DAGTypeLegalizer::SplitVecOp_INSERT_SUBVECTOR(SDNode *N,
                                                      unsigned OpNo) {
  assert(OpNo == 1 && "Invalid OpNo; can only split SubVec.");
  // We know that the result type is legal.
  EVT ResVT = N->getValueType(0);

  SDValue Vec = N->getOperand(0);
  SDValue SubVec = N->getOperand(1);
  SDValue Idx = N->getOperand(2);
  SDLoc dl(N);

  SDValue Lo, Hi;
  GetSplitVector(SubVec, Lo, Hi);

  uint64_t IdxVal = Idx->getAsZExtVal();
  uint64_t LoElts = Lo.getValueType().getVectorMinNumElements();

  SDValue FirstInsertion =
      DAG.getNode(ISD::INSERT_SUBVECTOR, dl, ResVT, Vec, Lo, Idx);
  SDValue SecondInsertion =
      DAG.getNode(ISD::INSERT_SUBVECTOR, dl, ResVT, FirstInsertion, Hi,
                  DAG.getVectorIdxConstant(IdxVal + LoElts, dl));

  return SecondInsertion;
}

SDValue DAGTypeLegalizer::SplitVecOp_EXTRACT_SUBVECTOR(SDNode *N) {
  // We know that the extracted result type is legal.
  EVT SubVT = N->getValueType(0);
  SDValue Idx = N->getOperand(1);
  SDLoc dl(N);
  SDValue Lo, Hi;

  GetSplitVector(N->getOperand(0), Lo, Hi);

  uint64_t LoEltsMin = Lo.getValueType().getVectorMinNumElements();
  uint64_t IdxVal = Idx->getAsZExtVal();

  if (IdxVal < LoEltsMin) {
    assert(IdxVal + SubVT.getVectorMinNumElements() <= LoEltsMin &&
           "Extracted subvector crosses vector split!");
    return DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, SubVT, Lo, Idx);
  } else if (SubVT.isScalableVector() ==
             N->getOperand(0).getValueType().isScalableVector())
    return DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, SubVT, Hi,
                       DAG.getVectorIdxConstant(IdxVal - LoEltsMin, dl));

  // After this point the DAG node only permits extracting fixed-width
  // subvectors from scalable vectors.
  assert(SubVT.isFixedLengthVector() &&
         "Extracting scalable subvector from fixed-width unsupported");

  // If the element type is i1 and we're not promoting the result, then we may
  // end up loading the wrong data since the bits are packed tightly into
  // bytes. For example, if we extract a v4i1 (legal) from a nxv4i1 (legal)
  // type at index 4, then we will load a byte starting at index 0.
  if (SubVT.getScalarType() == MVT::i1)
    report_fatal_error("Don't know how to extract fixed-width predicate "
                       "subvector from a scalable predicate vector");

  // Spill the vector to the stack. We should use the alignment for
  // the smallest part.
  SDValue Vec = N->getOperand(0);
  EVT VecVT = Vec.getValueType();
  Align SmallestAlign = DAG.getReducedAlign(VecVT, /*UseABI=*/false);
  SDValue StackPtr =
      DAG.CreateStackTemporary(VecVT.getStoreSize(), SmallestAlign);
  auto &MF = DAG.getMachineFunction();
  auto FrameIndex = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();
  auto PtrInfo = MachinePointerInfo::getFixedStack(MF, FrameIndex);

  SDValue Store = DAG.getStore(DAG.getEntryNode(), dl, Vec, StackPtr, PtrInfo,
                               SmallestAlign);

  // Extract the subvector by loading the correct part.
  StackPtr = TLI.getVectorSubVecPointer(DAG, StackPtr, VecVT, SubVT, Idx);

  return DAG.getLoad(
      SubVT, dl, Store, StackPtr,
      MachinePointerInfo::getUnknownStack(DAG.getMachineFunction()));
}

SDValue DAGTypeLegalizer::SplitVecOp_EXTRACT_VECTOR_ELT(SDNode *N) {
  SDValue Vec = N->getOperand(0);
  SDValue Idx = N->getOperand(1);
  EVT VecVT = Vec.getValueType();

  if (const ConstantSDNode *Index = dyn_cast<ConstantSDNode>(Idx)) {
    uint64_t IdxVal = Index->getZExtValue();

    SDValue Lo, Hi;
    GetSplitVector(Vec, Lo, Hi);

    uint64_t LoElts = Lo.getValueType().getVectorMinNumElements();

    if (IdxVal < LoElts)
      return SDValue(DAG.UpdateNodeOperands(N, Lo, Idx), 0);
    else if (!Vec.getValueType().isScalableVector())
      return SDValue(DAG.UpdateNodeOperands(N, Hi,
                                    DAG.getConstant(IdxVal - LoElts, SDLoc(N),
                                                    Idx.getValueType())), 0);
  }

  // See if the target wants to custom expand this node.
  if (CustomLowerNode(N, N->getValueType(0), true))
    return SDValue();

  // Make the vector elements byte-addressable if they aren't already.
  SDLoc dl(N);
  EVT EltVT = VecVT.getVectorElementType();
  if (!EltVT.isByteSized()) {
    EltVT = EltVT.changeTypeToInteger().getRoundIntegerType(*DAG.getContext());
    VecVT = VecVT.changeElementType(EltVT);
    Vec = DAG.getNode(ISD::ANY_EXTEND, dl, VecVT, Vec);
    SDValue NewExtract =
        DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, Vec, Idx);
    return DAG.getAnyExtOrTrunc(NewExtract, dl, N->getValueType(0));
  }

  // Store the vector to the stack.
  // In cases where the vector is illegal it will be broken down into parts
  // and stored in parts - we should use the alignment for the smallest part.
  Align SmallestAlign = DAG.getReducedAlign(VecVT, /*UseABI=*/false);
  SDValue StackPtr =
      DAG.CreateStackTemporary(VecVT.getStoreSize(), SmallestAlign);
  auto &MF = DAG.getMachineFunction();
  auto FrameIndex = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();
  auto PtrInfo = MachinePointerInfo::getFixedStack(MF, FrameIndex);
  SDValue Store = DAG.getStore(DAG.getEntryNode(), dl, Vec, StackPtr, PtrInfo,
                               SmallestAlign);

  // Load back the required element.
  StackPtr = TLI.getVectorElementPointer(DAG, StackPtr, VecVT, Idx);

  // EXTRACT_VECTOR_ELT can extend the element type to the width of the return
  // type, leaving the high bits undefined. But it can't truncate.
  assert(N->getValueType(0).bitsGE(EltVT) && "Illegal EXTRACT_VECTOR_ELT.");

  return DAG.getExtLoad(
      ISD::EXTLOAD, dl, N->getValueType(0), Store, StackPtr,
      MachinePointerInfo::getUnknownStack(DAG.getMachineFunction()), EltVT,
      commonAlignment(SmallestAlign, EltVT.getFixedSizeInBits() / 8));
}

SDValue DAGTypeLegalizer::SplitVecOp_ExtVecInRegOp(SDNode *N) {
  SDValue Lo, Hi;

  // *_EXTEND_VECTOR_INREG only reference the lower half of the input, so
  // splitting the result has the same effect as splitting the input operand.
  SplitVecRes_ExtVecInRegOp(N, Lo, Hi);

  return DAG.getNode(ISD::CONCAT_VECTORS, SDLoc(N), N->getValueType(0), Lo, Hi);
}

SDValue DAGTypeLegalizer::SplitVecOp_Gather(MemSDNode *N, unsigned OpNo) {
  (void)OpNo;
  SDValue Lo, Hi;
  SplitVecRes_Gather(N, Lo, Hi);

  SDValue Res = DAG.getNode(ISD::CONCAT_VECTORS, N, N->getValueType(0), Lo, Hi);
  ReplaceValueWith(SDValue(N, 0), Res);
  return SDValue();
}

SDValue DAGTypeLegalizer::SplitVecOp_VP_STORE(VPStoreSDNode *N, unsigned OpNo) {
  assert(N->isUnindexed() && "Indexed vp_store of vector?");
  SDValue Ch = N->getChain();
  SDValue Ptr = N->getBasePtr();
  SDValue Offset = N->getOffset();
  assert(Offset.isUndef() && "Unexpected VP store offset");
  SDValue Mask = N->getMask();
  SDValue EVL = N->getVectorLength();
  SDValue Data = N->getValue();
  Align Alignment = N->getOriginalAlign();
  SDLoc DL(N);

  SDValue DataLo, DataHi;
  if (getTypeAction(Data.getValueType()) == TargetLowering::TypeSplitVector)
    // Split Data operand
    GetSplitVector(Data, DataLo, DataHi);
  else
    std::tie(DataLo, DataHi) = DAG.SplitVector(Data, DL);

  // Split Mask operand
  SDValue MaskLo, MaskHi;
  if (OpNo == 1 && Mask.getOpcode() == ISD::SETCC) {
    SplitVecRes_SETCC(Mask.getNode(), MaskLo, MaskHi);
  } else {
    if (getTypeAction(Mask.getValueType()) == TargetLowering::TypeSplitVector)
      GetSplitVector(Mask, MaskLo, MaskHi);
    else
      std::tie(MaskLo, MaskHi) = DAG.SplitVector(Mask, DL);
  }

  EVT MemoryVT = N->getMemoryVT();
  EVT LoMemVT, HiMemVT;
  bool HiIsEmpty = false;
  std::tie(LoMemVT, HiMemVT) =
      DAG.GetDependentSplitDestVTs(MemoryVT, DataLo.getValueType(), &HiIsEmpty);

  // Split EVL
  SDValue EVLLo, EVLHi;
  std::tie(EVLLo, EVLHi) = DAG.SplitEVL(EVL, Data.getValueType(), DL);

  SDValue Lo, Hi;
  MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
      N->getPointerInfo(), MachineMemOperand::MOStore,
      LocationSize::beforeOrAfterPointer(), Alignment, N->getAAInfo(),
      N->getRanges());

  Lo = DAG.getStoreVP(Ch, DL, DataLo, Ptr, Offset, MaskLo, EVLLo, LoMemVT, MMO,
                      N->getAddressingMode(), N->isTruncatingStore(),
                      N->isCompressingStore());

  // If the hi vp_store has zero storage size, only the lo vp_store is needed.
  if (HiIsEmpty)
    return Lo;

  Ptr = TLI.IncrementMemoryAddress(Ptr, MaskLo, DL, LoMemVT, DAG,
                                   N->isCompressingStore());

  MachinePointerInfo MPI;
  if (LoMemVT.isScalableVector()) {
    Alignment = commonAlignment(Alignment,
                                LoMemVT.getSizeInBits().getKnownMinValue() / 8);
    MPI = MachinePointerInfo(N->getPointerInfo().getAddrSpace());
  } else
    MPI = N->getPointerInfo().getWithOffset(
        LoMemVT.getStoreSize().getFixedValue());

  MMO = DAG.getMachineFunction().getMachineMemOperand(
      MPI, MachineMemOperand::MOStore, LocationSize::beforeOrAfterPointer(),
      Alignment, N->getAAInfo(), N->getRanges());

  Hi = DAG.getStoreVP(Ch, DL, DataHi, Ptr, Offset, MaskHi, EVLHi, HiMemVT, MMO,
                      N->getAddressingMode(), N->isTruncatingStore(),
                      N->isCompressingStore());

  // Build a factor node to remember that this store is independent of the
  // other one.
  return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Lo, Hi);
}

SDValue DAGTypeLegalizer::SplitVecOp_VP_STRIDED_STORE(VPStridedStoreSDNode *N,
                                                      unsigned OpNo) {
  assert(N->isUnindexed() && "Indexed vp_strided_store of a vector?");
  assert(N->getOffset().isUndef() && "Unexpected VP strided store offset");

  SDLoc DL(N);

  SDValue Data = N->getValue();
  SDValue LoData, HiData;
  if (getTypeAction(Data.getValueType()) == TargetLowering::TypeSplitVector)
    GetSplitVector(Data, LoData, HiData);
  else
    std::tie(LoData, HiData) = DAG.SplitVector(Data, DL);

  EVT LoMemVT, HiMemVT;
  bool HiIsEmpty = false;
  std::tie(LoMemVT, HiMemVT) = DAG.GetDependentSplitDestVTs(
      N->getMemoryVT(), LoData.getValueType(), &HiIsEmpty);

  SDValue Mask = N->getMask();
  SDValue LoMask, HiMask;
  if (OpNo == 1 && Mask.getOpcode() == ISD::SETCC)
    SplitVecRes_SETCC(Mask.getNode(), LoMask, HiMask);
  else if (getTypeAction(Mask.getValueType()) ==
           TargetLowering::TypeSplitVector)
    GetSplitVector(Mask, LoMask, HiMask);
  else
    std::tie(LoMask, HiMask) = DAG.SplitVector(Mask, DL);

  SDValue LoEVL, HiEVL;
  std::tie(LoEVL, HiEVL) =
      DAG.SplitEVL(N->getVectorLength(), Data.getValueType(), DL);

  // Generate the low vp_strided_store
  SDValue Lo = DAG.getStridedStoreVP(
      N->getChain(), DL, LoData, N->getBasePtr(), N->getOffset(),
      N->getStride(), LoMask, LoEVL, LoMemVT, N->getMemOperand(),
      N->getAddressingMode(), N->isTruncatingStore(), N->isCompressingStore());

  // If the high vp_strided_store has zero storage size, only the low
  // vp_strided_store is needed.
  if (HiIsEmpty)
    return Lo;

  // Generate the high vp_strided_store.
  // To calculate the high base address, we need to sum to the low base
  // address stride number of bytes for each element already stored by low,
  // that is: Ptr = Ptr + (LoEVL * Stride)
  EVT PtrVT = N->getBasePtr().getValueType();
  SDValue Increment =
      DAG.getNode(ISD::MUL, DL, PtrVT, LoEVL,
                  DAG.getSExtOrTrunc(N->getStride(), DL, PtrVT));
  SDValue Ptr = DAG.getNode(ISD::ADD, DL, PtrVT, N->getBasePtr(), Increment);

  Align Alignment = N->getOriginalAlign();
  if (LoMemVT.isScalableVector())
    Alignment = commonAlignment(Alignment,
                                LoMemVT.getSizeInBits().getKnownMinValue() / 8);

  MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
      MachinePointerInfo(N->getPointerInfo().getAddrSpace()),
      MachineMemOperand::MOStore, LocationSize::beforeOrAfterPointer(),
      Alignment, N->getAAInfo(), N->getRanges());

  SDValue Hi = DAG.getStridedStoreVP(
      N->getChain(), DL, HiData, Ptr, N->getOffset(), N->getStride(), HiMask,
      HiEVL, HiMemVT, MMO, N->getAddressingMode(), N->isTruncatingStore(),
      N->isCompressingStore());

  // Build a factor node to remember that this store is independent of the
  // other one.
  return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Lo, Hi);
}

SDValue DAGTypeLegalizer::SplitVecOp_MSTORE(MaskedStoreSDNode *N,
                                            unsigned OpNo) {
  assert(N->isUnindexed() && "Indexed masked store of vector?");
  SDValue Ch  = N->getChain();
  SDValue Ptr = N->getBasePtr();
  SDValue Offset = N->getOffset();
  assert(Offset.isUndef() && "Unexpected indexed masked store offset");
  SDValue Mask = N->getMask();
  SDValue Data = N->getValue();
  Align Alignment = N->getOriginalAlign();
  SDLoc DL(N);

  SDValue DataLo, DataHi;
  if (getTypeAction(Data.getValueType()) == TargetLowering::TypeSplitVector)
    // Split Data operand
    GetSplitVector(Data, DataLo, DataHi);
  else
    std::tie(DataLo, DataHi) = DAG.SplitVector(Data, DL);

  // Split Mask operand
  SDValue MaskLo, MaskHi;
  if (OpNo == 1 && Mask.getOpcode() == ISD::SETCC) {
    SplitVecRes_SETCC(Mask.getNode(), MaskLo, MaskHi);
  } else {
    if (getTypeAction(Mask.getValueType()) == TargetLowering::TypeSplitVector)
      GetSplitVector(Mask, MaskLo, MaskHi);
    else
      std::tie(MaskLo, MaskHi) = DAG.SplitVector(Mask, DL);
  }

  EVT MemoryVT = N->getMemoryVT();
  EVT LoMemVT, HiMemVT;
  bool HiIsEmpty = false;
  std::tie(LoMemVT, HiMemVT) =
      DAG.GetDependentSplitDestVTs(MemoryVT, DataLo.getValueType(), &HiIsEmpty);

  SDValue Lo, Hi, Res;
  MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
      N->getPointerInfo(), MachineMemOperand::MOStore,
      LocationSize::beforeOrAfterPointer(), Alignment, N->getAAInfo(),
      N->getRanges());

  Lo = DAG.getMaskedStore(Ch, DL, DataLo, Ptr, Offset, MaskLo, LoMemVT, MMO,
                          N->getAddressingMode(), N->isTruncatingStore(),
                          N->isCompressingStore());

  if (HiIsEmpty) {
    // The hi masked store has zero storage size.
    // Only the lo masked store is needed.
    Res = Lo;
  } else {

    Ptr = TLI.IncrementMemoryAddress(Ptr, MaskLo, DL, LoMemVT, DAG,
                                     N->isCompressingStore());

    MachinePointerInfo MPI;
    if (LoMemVT.isScalableVector()) {
      Alignment = commonAlignment(
          Alignment, LoMemVT.getSizeInBits().getKnownMinValue() / 8);
      MPI = MachinePointerInfo(N->getPointerInfo().getAddrSpace());
    } else
      MPI = N->getPointerInfo().getWithOffset(
          LoMemVT.getStoreSize().getFixedValue());

    MMO = DAG.getMachineFunction().getMachineMemOperand(
        MPI, MachineMemOperand::MOStore, LocationSize::beforeOrAfterPointer(),
        Alignment, N->getAAInfo(), N->getRanges());

    Hi = DAG.getMaskedStore(Ch, DL, DataHi, Ptr, Offset, MaskHi, HiMemVT, MMO,
                            N->getAddressingMode(), N->isTruncatingStore(),
                            N->isCompressingStore());

    // Build a factor node to remember that this store is independent of the
    // other one.
    Res = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Lo, Hi);
  }

  return Res;
}

SDValue DAGTypeLegalizer::SplitVecOp_Scatter(MemSDNode *N, unsigned OpNo) {
  SDValue Ch = N->getChain();
  SDValue Ptr = N->getBasePtr();
  EVT MemoryVT = N->getMemoryVT();
  Align Alignment = N->getOriginalAlign();
  SDLoc DL(N);
  struct Operands {
    SDValue Mask;
    SDValue Index;
    SDValue Scale;
    SDValue Data;
  } Ops = [&]() -> Operands {
    if (auto *MSC = dyn_cast<MaskedScatterSDNode>(N)) {
      return {MSC->getMask(), MSC->getIndex(), MSC->getScale(),
              MSC->getValue()};
    }
    auto *VPSC = cast<VPScatterSDNode>(N);
    return {VPSC->getMask(), VPSC->getIndex(), VPSC->getScale(),
            VPSC->getValue()};
  }();
  // Split all operands

  EVT LoMemVT, HiMemVT;
  std::tie(LoMemVT, HiMemVT) = DAG.GetSplitDestVTs(MemoryVT);

  SDValue DataLo, DataHi;
  if (getTypeAction(Ops.Data.getValueType()) == TargetLowering::TypeSplitVector)
    // Split Data operand
    GetSplitVector(Ops.Data, DataLo, DataHi);
  else
    std::tie(DataLo, DataHi) = DAG.SplitVector(Ops.Data, DL);

  // Split Mask operand
  SDValue MaskLo, MaskHi;
  if (OpNo == 1 && Ops.Mask.getOpcode() == ISD::SETCC) {
    SplitVecRes_SETCC(Ops.Mask.getNode(), MaskLo, MaskHi);
  } else {
    std::tie(MaskLo, MaskHi) = SplitMask(Ops.Mask, DL);
  }

  SDValue IndexHi, IndexLo;
  if (getTypeAction(Ops.Index.getValueType()) ==
      TargetLowering::TypeSplitVector)
    GetSplitVector(Ops.Index, IndexLo, IndexHi);
  else
    std::tie(IndexLo, IndexHi) = DAG.SplitVector(Ops.Index, DL);

  SDValue Lo;
  MachineMemOperand *MMO = DAG.getMachineFunction().getMachineMemOperand(
      N->getPointerInfo(), MachineMemOperand::MOStore,
      LocationSize::beforeOrAfterPointer(), Alignment, N->getAAInfo(),
      N->getRanges());

  if (auto *MSC = dyn_cast<MaskedScatterSDNode>(N)) {
    SDValue OpsLo[] = {Ch, DataLo, MaskLo, Ptr, IndexLo, Ops.Scale};
    Lo =
        DAG.getMaskedScatter(DAG.getVTList(MVT::Other), LoMemVT, DL, OpsLo, MMO,
                             MSC->getIndexType(), MSC->isTruncatingStore());

    // The order of the Scatter operation after split is well defined. The "Hi"
    // part comes after the "Lo". So these two operations should be chained one
    // after another.
    SDValue OpsHi[] = {Lo, DataHi, MaskHi, Ptr, IndexHi, Ops.Scale};
    return DAG.getMaskedScatter(DAG.getVTList(MVT::Other), HiMemVT, DL, OpsHi,
                                MMO, MSC->getIndexType(),
                                MSC->isTruncatingStore());
  }
  auto *VPSC = cast<VPScatterSDNode>(N);
  SDValue EVLLo, EVLHi;
  std::tie(EVLLo, EVLHi) =
      DAG.SplitEVL(VPSC->getVectorLength(), Ops.Data.getValueType(), DL);

  SDValue OpsLo[] = {Ch, DataLo, Ptr, IndexLo, Ops.Scale, MaskLo, EVLLo};
  Lo = DAG.getScatterVP(DAG.getVTList(MVT::Other), LoMemVT, DL, OpsLo, MMO,
                        VPSC->getIndexType());

  // The order of the Scatter operation after split is well defined. The "Hi"
  // part comes after the "Lo". So these two operations should be chained one
  // after another.
  SDValue OpsHi[] = {Lo, DataHi, Ptr, IndexHi, Ops.Scale, MaskHi, EVLHi};
  return DAG.getScatterVP(DAG.getVTList(MVT::Other), HiMemVT, DL, OpsHi, MMO,
                          VPSC->getIndexType());
}

SDValue DAGTypeLegalizer::SplitVecOp_STORE(StoreSDNode *N, unsigned OpNo) {
  assert(N->isUnindexed() && "Indexed store of vector?");
  assert(OpNo == 1 && "Can only split the stored value");
  SDLoc DL(N);

  bool isTruncating = N->isTruncatingStore();
  SDValue Ch  = N->getChain();
  SDValue Ptr = N->getBasePtr();
  EVT MemoryVT = N->getMemoryVT();
  Align Alignment = N->getOriginalAlign();
  MachineMemOperand::Flags MMOFlags = N->getMemOperand()->getFlags();
  AAMDNodes AAInfo = N->getAAInfo();
  SDValue Lo, Hi;
  GetSplitVector(N->getOperand(1), Lo, Hi);

  EVT LoMemVT, HiMemVT;
  std::tie(LoMemVT, HiMemVT) = DAG.GetSplitDestVTs(MemoryVT);

  // Scalarize if the split halves are not byte-sized.
  if (!LoMemVT.isByteSized() || !HiMemVT.isByteSized())
    return TLI.scalarizeVectorStore(N, DAG);

  if (isTruncating)
    Lo = DAG.getTruncStore(Ch, DL, Lo, Ptr, N->getPointerInfo(), LoMemVT,
                           Alignment, MMOFlags, AAInfo);
  else
    Lo = DAG.getStore(Ch, DL, Lo, Ptr, N->getPointerInfo(), Alignment, MMOFlags,
                      AAInfo);

  MachinePointerInfo MPI;
  IncrementPointer(N, LoMemVT, MPI, Ptr);

  if (isTruncating)
    Hi = DAG.getTruncStore(Ch, DL, Hi, Ptr, MPI,
                           HiMemVT, Alignment, MMOFlags, AAInfo);
  else
    Hi = DAG.getStore(Ch, DL, Hi, Ptr, MPI, Alignment, MMOFlags, AAInfo);

  return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Lo, Hi);
}

SDValue DAGTypeLegalizer::SplitVecOp_CONCAT_VECTORS(SDNode *N) {
  SDLoc DL(N);

  // The input operands all must have the same type, and we know the result
  // type is valid.  Convert this to a buildvector which extracts all the
  // input elements.
  // TODO: If the input elements are power-two vectors, we could convert this to
  // a new CONCAT_VECTORS node with elements that are half-wide.
  SmallVector<SDValue, 32> Elts;
  EVT EltVT = N->getValueType(0).getVectorElementType();
  for (const SDValue &Op : N->op_values()) {
    for (unsigned i = 0, e = Op.getValueType().getVectorNumElements();
         i != e; ++i) {
      Elts.push_back(DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, EltVT, Op,
                                 DAG.getVectorIdxConstant(i, DL)));
    }
  }

  return DAG.getBuildVector(N->getValueType(0), DL, Elts);
}

SDValue DAGTypeLegalizer::SplitVecOp_TruncateHelper(SDNode *N) {
  // The result type is legal, but the input type is illegal.  If splitting
  // ends up with the result type of each half still being legal, just
  // do that.  If, however, that would result in an illegal result type,
  // we can try to get more clever with power-two vectors. Specifically,
  // split the input type, but also widen the result element size, then
  // concatenate the halves and truncate again.  For example, consider a target
  // where v8i8 is legal and v8i32 is not (ARM, which doesn't have 256-bit
  // vectors). To perform a "%res = v8i8 trunc v8i32 %in" we do:
  //   %inlo = v4i32 extract_subvector %in, 0
  //   %inhi = v4i32 extract_subvector %in, 4
  //   %lo16 = v4i16 trunc v4i32 %inlo
  //   %hi16 = v4i16 trunc v4i32 %inhi
  //   %in16 = v8i16 concat_vectors v4i16 %lo16, v4i16 %hi16
  //   %res = v8i8 trunc v8i16 %in16
  //
  // Without this transform, the original truncate would end up being
  // scalarized, which is pretty much always a last resort.
  unsigned OpNo = N->isStrictFPOpcode() ? 1 : 0;
  SDValue InVec = N->getOperand(OpNo);
  EVT InVT = InVec->getValueType(0);
  EVT OutVT = N->getValueType(0);
  ElementCount NumElements = OutVT.getVectorElementCount();
  bool IsFloat = OutVT.isFloatingPoint();

  unsigned InElementSize = InVT.getScalarSizeInBits();
  unsigned OutElementSize = OutVT.getScalarSizeInBits();

  // Determine the split output VT. If its legal we can just split dirctly.
  EVT LoOutVT, HiOutVT;
  std::tie(LoOutVT, HiOutVT) = DAG.GetSplitDestVTs(OutVT);
  assert(LoOutVT == HiOutVT && "Unequal split?");

  // If the input elements are only 1/2 the width of the result elements,
  // just use the normal splitting. Our trick only work if there's room
  // to split more than once.
  if (isTypeLegal(LoOutVT) ||
      InElementSize <= OutElementSize * 2)
    return SplitVecOp_UnaryOp(N);
  SDLoc DL(N);

  // Don't touch if this will be scalarized.
  EVT FinalVT = InVT;
  while (getTypeAction(FinalVT) == TargetLowering::TypeSplitVector)
    FinalVT = FinalVT.getHalfNumVectorElementsVT(*DAG.getContext());

  if (getTypeAction(FinalVT) == TargetLowering::TypeScalarizeVector)
    return SplitVecOp_UnaryOp(N);

  // Get the split input vector.
  SDValue InLoVec, InHiVec;
  GetSplitVector(InVec, InLoVec, InHiVec);

  // Truncate them to 1/2 the element size.
  //
  // This assumes the number of elements is a power of two; any vector that
  // isn't should be widened, not split.
  EVT HalfElementVT = IsFloat ?
    EVT::getFloatingPointVT(InElementSize/2) :
    EVT::getIntegerVT(*DAG.getContext(), InElementSize/2);
  EVT HalfVT = EVT::getVectorVT(*DAG.getContext(), HalfElementVT,
                                NumElements.divideCoefficientBy(2));

  SDValue HalfLo;
  SDValue HalfHi;
  SDValue Chain;
  if (N->isStrictFPOpcode()) {
    HalfLo = DAG.getNode(N->getOpcode(), DL, {HalfVT, MVT::Other},
                         {N->getOperand(0), InLoVec});
    HalfHi = DAG.getNode(N->getOpcode(), DL, {HalfVT, MVT::Other},
                         {N->getOperand(0), InHiVec});
    // Legalize the chain result - switch anything that used the old chain to
    // use the new one.
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, HalfLo.getValue(1),
                        HalfHi.getValue(1));
  } else {
    HalfLo = DAG.getNode(N->getOpcode(), DL, HalfVT, InLoVec);
    HalfHi = DAG.getNode(N->getOpcode(), DL, HalfVT, InHiVec);
  }

  // Concatenate them to get the full intermediate truncation result.
  EVT InterVT = EVT::getVectorVT(*DAG.getContext(), HalfElementVT, NumElements);
  SDValue InterVec = DAG.getNode(ISD::CONCAT_VECTORS, DL, InterVT, HalfLo,
                                 HalfHi);
  // Now finish up by truncating all the way down to the original result
  // type. This should normally be something that ends up being legal directly,
  // but in theory if a target has very wide vectors and an annoyingly
  // restricted set of legal types, this split can chain to build things up.

  if (N->isStrictFPOpcode()) {
    SDValue Res = DAG.getNode(
        ISD::STRICT_FP_ROUND, DL, {OutVT, MVT::Other},
        {Chain, InterVec,
         DAG.getTargetConstant(0, DL, TLI.getPointerTy(DAG.getDataLayout()))});
    // Relink the chain
    ReplaceValueWith(SDValue(N, 1), SDValue(Res.getNode(), 1));
    return Res;
  }

  return IsFloat
             ? DAG.getNode(ISD::FP_ROUND, DL, OutVT, InterVec,
                           DAG.getTargetConstant(
                               0, DL, TLI.getPointerTy(DAG.getDataLayout())))
             : DAG.getNode(ISD::TRUNCATE, DL, OutVT, InterVec);
}

SDValue DAGTypeLegalizer::SplitVecOp_VSETCC(SDNode *N) {
  bool isStrict = N->getOpcode() == ISD::STRICT_FSETCC;
  assert(N->getValueType(0).isVector() &&
         N->getOperand(isStrict ? 1 : 0).getValueType().isVector() &&
         "Operand types must be vectors");
  // The result has a legal vector type, but the input needs splitting.
  SDValue Lo0, Hi0, Lo1, Hi1, LoRes, HiRes;
  SDLoc DL(N);
  GetSplitVector(N->getOperand(isStrict ? 1 : 0), Lo0, Hi0);
  GetSplitVector(N->getOperand(isStrict ? 2 : 1), Lo1, Hi1);

  auto PartEltCnt = Lo0.getValueType().getVectorElementCount();

  LLVMContext &Context = *DAG.getContext();
  EVT PartResVT = EVT::getVectorVT(Context, MVT::i1, PartEltCnt);
  EVT WideResVT = EVT::getVectorVT(Context, MVT::i1, PartEltCnt*2);

  if (N->getOpcode() == ISD::SETCC) {
    LoRes = DAG.getNode(ISD::SETCC, DL, PartResVT, Lo0, Lo1, N->getOperand(2));
    HiRes = DAG.getNode(ISD::SETCC, DL, PartResVT, Hi0, Hi1, N->getOperand(2));
  } else if (N->getOpcode() == ISD::STRICT_FSETCC) {
    LoRes = DAG.getNode(ISD::STRICT_FSETCC, DL,
                        DAG.getVTList(PartResVT, N->getValueType(1)),
                        N->getOperand(0), Lo0, Lo1, N->getOperand(3));
    HiRes = DAG.getNode(ISD::STRICT_FSETCC, DL,
                        DAG.getVTList(PartResVT, N->getValueType(1)),
                        N->getOperand(0), Hi0, Hi1, N->getOperand(3));
    SDValue NewChain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other,
                                   LoRes.getValue(1), HiRes.getValue(1));
    ReplaceValueWith(SDValue(N, 1), NewChain);
  } else {
    assert(N->getOpcode() == ISD::VP_SETCC && "Expected VP_SETCC opcode");
    SDValue MaskLo, MaskHi, EVLLo, EVLHi;
    std::tie(MaskLo, MaskHi) = SplitMask(N->getOperand(3));
    std::tie(EVLLo, EVLHi) =
        DAG.SplitEVL(N->getOperand(4), N->getValueType(0), DL);
    LoRes = DAG.getNode(ISD::VP_SETCC, DL, PartResVT, Lo0, Lo1,
                        N->getOperand(2), MaskLo, EVLLo);
    HiRes = DAG.getNode(ISD::VP_SETCC, DL, PartResVT, Hi0, Hi1,
                        N->getOperand(2), MaskHi, EVLHi);
  }
  SDValue Con = DAG.getNode(ISD::CONCAT_VECTORS, DL, WideResVT, LoRes, HiRes);

  EVT OpVT = N->getOperand(0).getValueType();
  ISD::NodeType ExtendCode =
      TargetLowering::getExtendForContent(TLI.getBooleanContents(OpVT));
  return DAG.getNode(ExtendCode, DL, N->getValueType(0), Con);
}


SDValue DAGTypeLegalizer::SplitVecOp_FP_ROUND(SDNode *N) {
  // The result has a legal vector type, but the input needs splitting.
  EVT ResVT = N->getValueType(0);
  SDValue Lo, Hi;
  SDLoc DL(N);
  GetSplitVector(N->getOperand(N->isStrictFPOpcode() ? 1 : 0), Lo, Hi);
  EVT InVT = Lo.getValueType();

  EVT OutVT = EVT::getVectorVT(*DAG.getContext(), ResVT.getVectorElementType(),
                               InVT.getVectorElementCount());

  if (N->isStrictFPOpcode()) {
    Lo = DAG.getNode(N->getOpcode(), DL, { OutVT, MVT::Other }, 
                     { N->getOperand(0), Lo, N->getOperand(2) });
    Hi = DAG.getNode(N->getOpcode(), DL, { OutVT, MVT::Other }, 
                     { N->getOperand(0), Hi, N->getOperand(2) });
    // Legalize the chain result - switch anything that used the old chain to
    // use the new one.
    SDValue NewChain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, 
                                   Lo.getValue(1), Hi.getValue(1));
    ReplaceValueWith(SDValue(N, 1), NewChain);
  } else if (N->getOpcode() == ISD::VP_FP_ROUND) {
    SDValue MaskLo, MaskHi, EVLLo, EVLHi;
    std::tie(MaskLo, MaskHi) = SplitMask(N->getOperand(1));
    std::tie(EVLLo, EVLHi) =
        DAG.SplitEVL(N->getOperand(2), N->getValueType(0), DL);
    Lo = DAG.getNode(ISD::VP_FP_ROUND, DL, OutVT, Lo, MaskLo, EVLLo);
    Hi = DAG.getNode(ISD::VP_FP_ROUND, DL, OutVT, Hi, MaskHi, EVLHi);
  } else {
    Lo = DAG.getNode(ISD::FP_ROUND, DL, OutVT, Lo, N->getOperand(1));
    Hi = DAG.getNode(ISD::FP_ROUND, DL, OutVT, Hi, N->getOperand(1));
  }

  return DAG.getNode(ISD::CONCAT_VECTORS, DL, ResVT, Lo, Hi);
}

// Split a vector type in an FP binary operation where the second operand has a
// different type from the first.
//
// The result (and the first input) has a legal vector type, but the second
// input needs splitting.
SDValue DAGTypeLegalizer::SplitVecOp_FPOpDifferentTypes(SDNode *N) {
  SDLoc DL(N);

  EVT LHSLoVT, LHSHiVT;
  std::tie(LHSLoVT, LHSHiVT) = DAG.GetSplitDestVTs(N->getValueType(0));

  if (!isTypeLegal(LHSLoVT) || !isTypeLegal(LHSHiVT))
    return DAG.UnrollVectorOp(N, N->getValueType(0).getVectorNumElements());

  SDValue LHSLo, LHSHi;
  std::tie(LHSLo, LHSHi) =
      DAG.SplitVector(N->getOperand(0), DL, LHSLoVT, LHSHiVT);

  SDValue RHSLo, RHSHi;
  std::tie(RHSLo, RHSHi) = DAG.SplitVector(N->getOperand(1), DL);

  SDValue Lo = DAG.getNode(N->getOpcode(), DL, LHSLoVT, LHSLo, RHSLo);
  SDValue Hi = DAG.getNode(N->getOpcode(), DL, LHSHiVT, LHSHi, RHSHi);

  return DAG.getNode(ISD::CONCAT_VECTORS, DL, N->getValueType(0), Lo, Hi);
}

SDValue DAGTypeLegalizer::SplitVecOp_CMP(SDNode *N) {
  LLVMContext &Ctxt = *DAG.getContext();
  SDLoc dl(N);

  SDValue LHSLo, LHSHi, RHSLo, RHSHi;
  GetSplitVector(N->getOperand(0), LHSLo, LHSHi);
  GetSplitVector(N->getOperand(1), RHSLo, RHSHi);

  EVT ResVT = N->getValueType(0);
  ElementCount SplitOpEC = LHSLo.getValueType().getVectorElementCount();
  EVT NewResVT =
      EVT::getVectorVT(Ctxt, ResVT.getVectorElementType(), SplitOpEC);

  SDValue Lo = DAG.getNode(N->getOpcode(), dl, NewResVT, LHSLo, RHSLo);
  SDValue Hi = DAG.getNode(N->getOpcode(), dl, NewResVT, LHSHi, RHSHi);

  return DAG.getNode(ISD::CONCAT_VECTORS, dl, ResVT, Lo, Hi);
}

SDValue DAGTypeLegalizer::SplitVecOp_FP_TO_XINT_SAT(SDNode *N) {
  EVT ResVT = N->getValueType(0);
  SDValue Lo, Hi;
  SDLoc dl(N);
  GetSplitVector(N->getOperand(0), Lo, Hi);
  EVT InVT = Lo.getValueType();

  EVT NewResVT =
      EVT::getVectorVT(*DAG.getContext(), ResVT.getVectorElementType(),
                       InVT.getVectorElementCount());

  Lo = DAG.getNode(N->getOpcode(), dl, NewResVT, Lo, N->getOperand(1));
  Hi = DAG.getNode(N->getOpcode(), dl, NewResVT, Hi, N->getOperand(1));

  return DAG.getNode(ISD::CONCAT_VECTORS, dl, ResVT, Lo, Hi);
}

SDValue DAGTypeLegalizer::SplitVecOp_VP_CttzElements(SDNode *N) {
  SDLoc DL(N);
  EVT ResVT = N->getValueType(0);

  SDValue Lo, Hi;
  SDValue VecOp = N->getOperand(0);
  GetSplitVector(VecOp, Lo, Hi);

  auto [MaskLo, MaskHi] = SplitMask(N->getOperand(1));
  auto [EVLLo, EVLHi] =
      DAG.SplitEVL(N->getOperand(2), VecOp.getValueType(), DL);
  SDValue VLo = DAG.getZExtOrTrunc(EVLLo, DL, ResVT);

  // if VP_CTTZ_ELTS(Lo) != EVLLo => VP_CTTZ_ELTS(Lo).
  // else => EVLLo + (VP_CTTZ_ELTS(Hi) or VP_CTTZ_ELTS_ZERO_UNDEF(Hi)).
  SDValue ResLo = DAG.getNode(ISD::VP_CTTZ_ELTS, DL, ResVT, Lo, MaskLo, EVLLo);
  SDValue ResLoNotEVL =
      DAG.getSetCC(DL, getSetCCResultType(ResVT), ResLo, VLo, ISD::SETNE);
  SDValue ResHi = DAG.getNode(N->getOpcode(), DL, ResVT, Hi, MaskHi, EVLHi);
  return DAG.getSelect(DL, ResVT, ResLoNotEVL, ResLo,
                       DAG.getNode(ISD::ADD, DL, ResVT, VLo, ResHi));
}

//===----------------------------------------------------------------------===//
//  Result Vector Widening
//===----------------------------------------------------------------------===//

void DAGTypeLegalizer::WidenVectorResult(SDNode *N, unsigned ResNo) {
  LLVM_DEBUG(dbgs() << "Widen node result " << ResNo << ": "; N->dump(&DAG));

  // See if the target wants to custom widen this node.
  if (CustomWidenLowerNode(N, N->getValueType(ResNo)))
    return;

  SDValue Res = SDValue();

  auto unrollExpandedOp = [&]() {
    // We're going to widen this vector op to a legal type by padding with undef
    // elements. If the wide vector op is eventually going to be expanded to
    // scalar libcalls, then unroll into scalar ops now to avoid unnecessary
    // libcalls on the undef elements.
    EVT VT = N->getValueType(0);
    EVT WideVecVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
    if (!TLI.isOperationLegalOrCustom(N->getOpcode(), WideVecVT) &&
        TLI.isOperationExpand(N->getOpcode(), VT.getScalarType())) {
      Res = DAG.UnrollVectorOp(N, WideVecVT.getVectorNumElements());
      return true;
    }
    return false;
  };

  switch (N->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "WidenVectorResult #" << ResNo << ": ";
    N->dump(&DAG);
    dbgs() << "\n";
#endif
    report_fatal_error("Do not know how to widen the result of this operator!");

  case ISD::MERGE_VALUES:      Res = WidenVecRes_MERGE_VALUES(N, ResNo); break;
  case ISD::ADDRSPACECAST:
    Res = WidenVecRes_ADDRSPACECAST(N);
    break;
  case ISD::AssertZext:        Res = WidenVecRes_AssertZext(N); break;
  case ISD::BITCAST:           Res = WidenVecRes_BITCAST(N); break;
  case ISD::BUILD_VECTOR:      Res = WidenVecRes_BUILD_VECTOR(N); break;
  case ISD::CONCAT_VECTORS:    Res = WidenVecRes_CONCAT_VECTORS(N); break;
  case ISD::INSERT_SUBVECTOR:
    Res = WidenVecRes_INSERT_SUBVECTOR(N);
    break;
  case ISD::EXTRACT_SUBVECTOR: Res = WidenVecRes_EXTRACT_SUBVECTOR(N); break;
  case ISD::INSERT_VECTOR_ELT: Res = WidenVecRes_INSERT_VECTOR_ELT(N); break;
  case ISD::LOAD:              Res = WidenVecRes_LOAD(N); break;
  case ISD::STEP_VECTOR:
  case ISD::SPLAT_VECTOR:
  case ISD::SCALAR_TO_VECTOR:
  case ISD::EXPERIMENTAL_VP_SPLAT:
    Res = WidenVecRes_ScalarOp(N);
    break;
  case ISD::SIGN_EXTEND_INREG: Res = WidenVecRes_InregOp(N); break;
  case ISD::VSELECT:
  case ISD::SELECT:
  case ISD::VP_SELECT:
  case ISD::VP_MERGE:
    Res = WidenVecRes_Select(N);
    break;
  case ISD::SELECT_CC:         Res = WidenVecRes_SELECT_CC(N); break;
  case ISD::VP_SETCC:
  case ISD::SETCC:             Res = WidenVecRes_SETCC(N); break;
  case ISD::UNDEF:             Res = WidenVecRes_UNDEF(N); break;
  case ISD::VECTOR_SHUFFLE:
    Res = WidenVecRes_VECTOR_SHUFFLE(cast<ShuffleVectorSDNode>(N));
    break;
  case ISD::VP_LOAD:
    Res = WidenVecRes_VP_LOAD(cast<VPLoadSDNode>(N));
    break;
  case ISD::EXPERIMENTAL_VP_STRIDED_LOAD:
    Res = WidenVecRes_VP_STRIDED_LOAD(cast<VPStridedLoadSDNode>(N));
    break;
  case ISD::VECTOR_COMPRESS:
    Res = WidenVecRes_VECTOR_COMPRESS(N);
    break;
  case ISD::MLOAD:
    Res = WidenVecRes_MLOAD(cast<MaskedLoadSDNode>(N));
    break;
  case ISD::MGATHER:
    Res = WidenVecRes_MGATHER(cast<MaskedGatherSDNode>(N));
    break;
  case ISD::VP_GATHER:
    Res = WidenVecRes_VP_GATHER(cast<VPGatherSDNode>(N));
    break;
  case ISD::VECTOR_REVERSE:
    Res = WidenVecRes_VECTOR_REVERSE(N);
    break;

  case ISD::ADD: case ISD::VP_ADD:
  case ISD::AND: case ISD::VP_AND:
  case ISD::MUL: case ISD::VP_MUL:
  case ISD::MULHS:
  case ISD::MULHU:
  case ISD::OR: case ISD::VP_OR:
  case ISD::SUB: case ISD::VP_SUB:
  case ISD::XOR: case ISD::VP_XOR:
  case ISD::SHL: case ISD::VP_SHL:
  case ISD::SRA: case ISD::VP_SRA:
  case ISD::SRL: case ISD::VP_SRL:
  case ISD::FMINNUM:
  case ISD::FMINNUM_IEEE:
  case ISD::VP_FMINNUM:
  case ISD::FMAXNUM:
  case ISD::FMAXNUM_IEEE:
  case ISD::VP_FMAXNUM:
  case ISD::FMINIMUM:
  case ISD::VP_FMINIMUM:
  case ISD::FMAXIMUM:
  case ISD::VP_FMAXIMUM:
  case ISD::SMIN: case ISD::VP_SMIN:
  case ISD::SMAX: case ISD::VP_SMAX:
  case ISD::UMIN: case ISD::VP_UMIN:
  case ISD::UMAX: case ISD::VP_UMAX:
  case ISD::UADDSAT: case ISD::VP_UADDSAT:
  case ISD::SADDSAT: case ISD::VP_SADDSAT:
  case ISD::USUBSAT: case ISD::VP_USUBSAT:
  case ISD::SSUBSAT: case ISD::VP_SSUBSAT:
  case ISD::SSHLSAT:
  case ISD::USHLSAT:
  case ISD::ROTL:
  case ISD::ROTR:
  case ISD::AVGFLOORS:
  case ISD::AVGFLOORU:
  case ISD::AVGCEILS:
  case ISD::AVGCEILU:
  // Vector-predicated binary op widening. Note that -- unlike the
  // unpredicated versions -- we don't have to worry about trapping on
  // operations like UDIV, FADD, etc., as we pass on the original vector
  // length parameter. This means the widened elements containing garbage
  // aren't active.
  case ISD::VP_SDIV:
  case ISD::VP_UDIV:
  case ISD::VP_SREM:
  case ISD::VP_UREM:
  case ISD::VP_FADD:
  case ISD::VP_FSUB:
  case ISD::VP_FMUL:
  case ISD::VP_FDIV:
  case ISD::VP_FREM:
  case ISD::VP_FCOPYSIGN:
    Res = WidenVecRes_Binary(N);
    break;

  case ISD::SCMP:
  case ISD::UCMP:
    Res = WidenVecRes_CMP(N);
    break;

  case ISD::FPOW:
  case ISD::FREM:
    if (unrollExpandedOp())
      break;
    // If the target has custom/legal support for the scalar FP intrinsic ops
    // (they are probably not destined to become libcalls), then widen those
    // like any other binary ops.
    [[fallthrough]];

  case ISD::FADD:
  case ISD::FMUL:
  case ISD::FSUB:
  case ISD::FDIV:
  case ISD::SDIV:
  case ISD::UDIV:
  case ISD::SREM:
  case ISD::UREM:
    Res = WidenVecRes_BinaryCanTrap(N);
    break;

  case ISD::SMULFIX:
  case ISD::SMULFIXSAT:
  case ISD::UMULFIX:
  case ISD::UMULFIXSAT:
    // These are binary operations, but with an extra operand that shouldn't
    // be widened (the scale).
    Res = WidenVecRes_BinaryWithExtraScalarOp(N);
    break;

#define DAG_INSTRUCTION(NAME, NARG, ROUND_MODE, INTRINSIC, DAGN)               \
  case ISD::STRICT_##DAGN:
#include "llvm/IR/ConstrainedOps.def"
    Res = WidenVecRes_StrictFP(N);
    break;

  case ISD::UADDO:
  case ISD::SADDO:
  case ISD::USUBO:
  case ISD::SSUBO:
  case ISD::UMULO:
  case ISD::SMULO:
    Res = WidenVecRes_OverflowOp(N, ResNo);
    break;

  case ISD::FCOPYSIGN:
    Res = WidenVecRes_FCOPYSIGN(N);
    break;

  case ISD::IS_FPCLASS:
  case ISD::FPTRUNC_ROUND:
    Res = WidenVecRes_UnarySameEltsWithScalarArg(N);
    break;

  case ISD::FLDEXP:
  case ISD::FPOWI:
    if (!unrollExpandedOp())
      Res = WidenVecRes_ExpOp(N);
    break;

  case ISD::ANY_EXTEND_VECTOR_INREG:
  case ISD::SIGN_EXTEND_VECTOR_INREG:
  case ISD::ZERO_EXTEND_VECTOR_INREG:
    Res = WidenVecRes_EXTEND_VECTOR_INREG(N);
    break;

  case ISD::ANY_EXTEND:
  case ISD::FP_EXTEND:
  case ISD::VP_FP_EXTEND:
  case ISD::FP_ROUND:
  case ISD::VP_FP_ROUND:
  case ISD::FP_TO_SINT:
  case ISD::VP_FP_TO_SINT:
  case ISD::FP_TO_UINT:
  case ISD::VP_FP_TO_UINT:
  case ISD::SIGN_EXTEND:
  case ISD::VP_SIGN_EXTEND:
  case ISD::SINT_TO_FP:
  case ISD::VP_SINT_TO_FP:
  case ISD::VP_TRUNCATE:
  case ISD::TRUNCATE:
  case ISD::UINT_TO_FP:
  case ISD::VP_UINT_TO_FP:
  case ISD::ZERO_EXTEND:
  case ISD::VP_ZERO_EXTEND:
    Res = WidenVecRes_Convert(N);
    break;

  case ISD::FP_TO_SINT_SAT:
  case ISD::FP_TO_UINT_SAT:
    Res = WidenVecRes_FP_TO_XINT_SAT(N);
    break;

  case ISD::LRINT:
  case ISD::LLRINT:
  case ISD::VP_LRINT:
  case ISD::VP_LLRINT:
    Res = WidenVecRes_XRINT(N);
    break;

  case ISD::FABS:
  case ISD::FACOS:
  case ISD::FASIN:
  case ISD::FATAN:
  case ISD::FCEIL:
  case ISD::FCOS:
  case ISD::FCOSH:
  case ISD::FEXP:
  case ISD::FEXP2:
  case ISD::FEXP10:
  case ISD::FFLOOR:
  case ISD::FLOG:
  case ISD::FLOG10:
  case ISD::FLOG2:
  case ISD::FNEARBYINT:
  case ISD::FRINT:
  case ISD::FROUND:
  case ISD::FROUNDEVEN:
  case ISD::FSIN:
  case ISD::FSINH:
  case ISD::FSQRT:
  case ISD::FTAN:
  case ISD::FTANH:
  case ISD::FTRUNC:
    if (unrollExpandedOp())
      break;
    // If the target has custom/legal support for the scalar FP intrinsic ops
    // (they are probably not destined to become libcalls), then widen those
    // like any other unary ops.
    [[fallthrough]];

  case ISD::ABS:
  case ISD::VP_ABS:
  case ISD::BITREVERSE:
  case ISD::VP_BITREVERSE:
  case ISD::BSWAP:
  case ISD::VP_BSWAP:
  case ISD::CTLZ:
  case ISD::VP_CTLZ:
  case ISD::CTLZ_ZERO_UNDEF:
  case ISD::VP_CTLZ_ZERO_UNDEF:
  case ISD::CTPOP:
  case ISD::VP_CTPOP:
  case ISD::CTTZ:
  case ISD::VP_CTTZ:
  case ISD::CTTZ_ZERO_UNDEF:
  case ISD::VP_CTTZ_ZERO_UNDEF:
  case ISD::FNEG: case ISD::VP_FNEG:
  case ISD::VP_FABS:
  case ISD::VP_SQRT:
  case ISD::VP_FCEIL:
  case ISD::VP_FFLOOR:
  case ISD::VP_FRINT:
  case ISD::VP_FNEARBYINT:
  case ISD::VP_FROUND:
  case ISD::VP_FROUNDEVEN:
  case ISD::VP_FROUNDTOZERO:
  case ISD::FREEZE:
  case ISD::ARITH_FENCE:
  case ISD::FCANONICALIZE:
    Res = WidenVecRes_Unary(N);
    break;
  case ISD::FMA: case ISD::VP_FMA:
  case ISD::FSHL:
  case ISD::VP_FSHL:
  case ISD::FSHR:
  case ISD::VP_FSHR:
    Res = WidenVecRes_Ternary(N);
    break;
  }

  // If Res is null, the sub-method took care of registering the result.
  if (Res.getNode())
    SetWidenedVector(SDValue(N, ResNo), Res);
}

SDValue DAGTypeLegalizer::WidenVecRes_Ternary(SDNode *N) {
  // Ternary op widening.
  SDLoc dl(N);
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue InOp1 = GetWidenedVector(N->getOperand(0));
  SDValue InOp2 = GetWidenedVector(N->getOperand(1));
  SDValue InOp3 = GetWidenedVector(N->getOperand(2));
  if (N->getNumOperands() == 3)
    return DAG.getNode(N->getOpcode(), dl, WidenVT, InOp1, InOp2, InOp3);

  assert(N->getNumOperands() == 5 && "Unexpected number of operands!");
  assert(N->isVPOpcode() && "Expected VP opcode");

  SDValue Mask =
      GetWidenedMask(N->getOperand(3), WidenVT.getVectorElementCount());
  return DAG.getNode(N->getOpcode(), dl, WidenVT,
                     {InOp1, InOp2, InOp3, Mask, N->getOperand(4)});
}

SDValue DAGTypeLegalizer::WidenVecRes_Binary(SDNode *N) {
  // Binary op widening.
  SDLoc dl(N);
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue InOp1 = GetWidenedVector(N->getOperand(0));
  SDValue InOp2 = GetWidenedVector(N->getOperand(1));
  if (N->getNumOperands() == 2)
    return DAG.getNode(N->getOpcode(), dl, WidenVT, InOp1, InOp2,
                       N->getFlags());

  assert(N->getNumOperands() == 4 && "Unexpected number of operands!");
  assert(N->isVPOpcode() && "Expected VP opcode");

  SDValue Mask =
      GetWidenedMask(N->getOperand(2), WidenVT.getVectorElementCount());
  return DAG.getNode(N->getOpcode(), dl, WidenVT,
                     {InOp1, InOp2, Mask, N->getOperand(3)}, N->getFlags());
}

SDValue DAGTypeLegalizer::WidenVecRes_CMP(SDNode *N) {
  LLVMContext &Ctxt = *DAG.getContext();
  SDLoc dl(N);

  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  EVT OpVT = LHS.getValueType();
  if (getTypeAction(OpVT) == TargetLowering::TypeWidenVector) {
    LHS = GetWidenedVector(LHS);
    RHS = GetWidenedVector(RHS);
    OpVT = LHS.getValueType();
  }

  EVT WidenResVT = TLI.getTypeToTransformTo(Ctxt, N->getValueType(0));
  ElementCount WidenResEC = WidenResVT.getVectorElementCount();
  if (WidenResEC == OpVT.getVectorElementCount()) {
    return DAG.getNode(N->getOpcode(), dl, WidenResVT, LHS, RHS);
  }

  return DAG.UnrollVectorOp(N, WidenResVT.getVectorNumElements());
}

SDValue DAGTypeLegalizer::WidenVecRes_BinaryWithExtraScalarOp(SDNode *N) {
  // Binary op widening, but with an extra operand that shouldn't be widened.
  SDLoc dl(N);
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue InOp1 = GetWidenedVector(N->getOperand(0));
  SDValue InOp2 = GetWidenedVector(N->getOperand(1));
  SDValue InOp3 = N->getOperand(2);
  return DAG.getNode(N->getOpcode(), dl, WidenVT, InOp1, InOp2, InOp3,
                     N->getFlags());
}

// Given a vector of operations that have been broken up to widen, see
// if we can collect them together into the next widest legal VT. This
// implementation is trap-safe.
static SDValue CollectOpsToWiden(SelectionDAG &DAG, const TargetLowering &TLI,
                                 SmallVectorImpl<SDValue> &ConcatOps,
                                 unsigned ConcatEnd, EVT VT, EVT MaxVT,
                                 EVT WidenVT) {
  // Check to see if we have a single operation with the widen type.
  if (ConcatEnd == 1) {
    VT = ConcatOps[0].getValueType();
    if (VT == WidenVT)
      return ConcatOps[0];
  }

  SDLoc dl(ConcatOps[0]);
  EVT WidenEltVT = WidenVT.getVectorElementType();

  // while (Some element of ConcatOps is not of type MaxVT) {
  //   From the end of ConcatOps, collect elements of the same type and put
  //   them into an op of the next larger supported type
  // }
  while (ConcatOps[ConcatEnd-1].getValueType() != MaxVT) {
    int Idx = ConcatEnd - 1;
    VT = ConcatOps[Idx--].getValueType();
    while (Idx >= 0 && ConcatOps[Idx].getValueType() == VT)
      Idx--;

    int NextSize = VT.isVector() ? VT.getVectorNumElements() : 1;
    EVT NextVT;
    do {
      NextSize *= 2;
      NextVT = EVT::getVectorVT(*DAG.getContext(), WidenEltVT, NextSize);
    } while (!TLI.isTypeLegal(NextVT));

    if (!VT.isVector()) {
      // Scalar type, create an INSERT_VECTOR_ELEMENT of type NextVT
      SDValue VecOp = DAG.getUNDEF(NextVT);
      unsigned NumToInsert = ConcatEnd - Idx - 1;
      for (unsigned i = 0, OpIdx = Idx+1; i < NumToInsert; i++, OpIdx++) {
        VecOp = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, NextVT, VecOp,
                            ConcatOps[OpIdx], DAG.getVectorIdxConstant(i, dl));
      }
      ConcatOps[Idx+1] = VecOp;
      ConcatEnd = Idx + 2;
    } else {
      // Vector type, create a CONCAT_VECTORS of type NextVT
      SDValue undefVec = DAG.getUNDEF(VT);
      unsigned OpsToConcat = NextSize/VT.getVectorNumElements();
      SmallVector<SDValue, 16> SubConcatOps(OpsToConcat);
      unsigned RealVals = ConcatEnd - Idx - 1;
      unsigned SubConcatEnd = 0;
      unsigned SubConcatIdx = Idx + 1;
      while (SubConcatEnd < RealVals)
        SubConcatOps[SubConcatEnd++] = ConcatOps[++Idx];
      while (SubConcatEnd < OpsToConcat)
        SubConcatOps[SubConcatEnd++] = undefVec;
      ConcatOps[SubConcatIdx] = DAG.getNode(ISD::CONCAT_VECTORS, dl,
                                            NextVT, SubConcatOps);
      ConcatEnd = SubConcatIdx + 1;
    }
  }

  // Check to see if we have a single operation with the widen type.
  if (ConcatEnd == 1) {
    VT = ConcatOps[0].getValueType();
    if (VT == WidenVT)
      return ConcatOps[0];
  }

  // add undefs of size MaxVT until ConcatOps grows to length of WidenVT
  unsigned NumOps = WidenVT.getVectorNumElements()/MaxVT.getVectorNumElements();
  if (NumOps != ConcatEnd ) {
    SDValue UndefVal = DAG.getUNDEF(MaxVT);
    for (unsigned j = ConcatEnd; j < NumOps; ++j)
      ConcatOps[j] = UndefVal;
  }
  return DAG.getNode(ISD::CONCAT_VECTORS, dl, WidenVT,
                     ArrayRef(ConcatOps.data(), NumOps));
}

SDValue DAGTypeLegalizer::WidenVecRes_BinaryCanTrap(SDNode *N) {
  // Binary op widening for operations that can trap.
  unsigned Opcode = N->getOpcode();
  SDLoc dl(N);
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  EVT WidenEltVT = WidenVT.getVectorElementType();
  EVT VT = WidenVT;
  unsigned NumElts = VT.getVectorMinNumElements();
  const SDNodeFlags Flags = N->getFlags();
  while (!TLI.isTypeLegal(VT) && NumElts != 1) {
    NumElts = NumElts / 2;
    VT = EVT::getVectorVT(*DAG.getContext(), WidenEltVT, NumElts);
  }

  if (NumElts != 1 && !TLI.canOpTrap(N->getOpcode(), VT)) {
    // Operation doesn't trap so just widen as normal.
    SDValue InOp1 = GetWidenedVector(N->getOperand(0));
    SDValue InOp2 = GetWidenedVector(N->getOperand(1));
    return DAG.getNode(N->getOpcode(), dl, WidenVT, InOp1, InOp2, Flags);
  }

  // FIXME: Improve support for scalable vectors.
  assert(!VT.isScalableVector() && "Scalable vectors not handled yet.");

  // No legal vector version so unroll the vector operation and then widen.
  if (NumElts == 1)
    return DAG.UnrollVectorOp(N, WidenVT.getVectorNumElements());

  // Since the operation can trap, apply operation on the original vector.
  EVT MaxVT = VT;
  SDValue InOp1 = GetWidenedVector(N->getOperand(0));
  SDValue InOp2 = GetWidenedVector(N->getOperand(1));
  unsigned CurNumElts = N->getValueType(0).getVectorNumElements();

  SmallVector<SDValue, 16> ConcatOps(CurNumElts);
  unsigned ConcatEnd = 0;  // Current ConcatOps index.
  int Idx = 0;        // Current Idx into input vectors.

  // NumElts := greatest legal vector size (at most WidenVT)
  // while (orig. vector has unhandled elements) {
  //   take munches of size NumElts from the beginning and add to ConcatOps
  //   NumElts := next smaller supported vector size or 1
  // }
  while (CurNumElts != 0) {
    while (CurNumElts >= NumElts) {
      SDValue EOp1 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, VT, InOp1,
                                 DAG.getVectorIdxConstant(Idx, dl));
      SDValue EOp2 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, VT, InOp2,
                                 DAG.getVectorIdxConstant(Idx, dl));
      ConcatOps[ConcatEnd++] = DAG.getNode(Opcode, dl, VT, EOp1, EOp2, Flags);
      Idx += NumElts;
      CurNumElts -= NumElts;
    }
    do {
      NumElts = NumElts / 2;
      VT = EVT::getVectorVT(*DAG.getContext(), WidenEltVT, NumElts);
    } while (!TLI.isTypeLegal(VT) && NumElts != 1);

    if (NumElts == 1) {
      for (unsigned i = 0; i != CurNumElts; ++i, ++Idx) {
        SDValue EOp1 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, WidenEltVT,
                                   InOp1, DAG.getVectorIdxConstant(Idx, dl));
        SDValue EOp2 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, WidenEltVT,
                                   InOp2, DAG.getVectorIdxConstant(Idx, dl));
        ConcatOps[ConcatEnd++] = DAG.getNode(Opcode, dl, WidenEltVT,
                                             EOp1, EOp2, Flags);
      }
      CurNumElts = 0;
    }
  }

  return CollectOpsToWiden(DAG, TLI, ConcatOps, ConcatEnd, VT, MaxVT, WidenVT);
}

SDValue DAGTypeLegalizer::WidenVecRes_StrictFP(SDNode *N) {
  switch (N->getOpcode()) {
  case ISD::STRICT_FSETCC:
  case ISD::STRICT_FSETCCS:
    return WidenVecRes_STRICT_FSETCC(N);
  case ISD::STRICT_FP_EXTEND:
  case ISD::STRICT_FP_ROUND:
  case ISD::STRICT_FP_TO_SINT:
  case ISD::STRICT_FP_TO_UINT:
  case ISD::STRICT_SINT_TO_FP:
  case ISD::STRICT_UINT_TO_FP:
   return WidenVecRes_Convert_StrictFP(N);
  default:
    break;
  }

  // StrictFP op widening for operations that can trap.
  unsigned NumOpers = N->getNumOperands();
  unsigned Opcode = N->getOpcode();
  SDLoc dl(N);
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  EVT WidenEltVT = WidenVT.getVectorElementType();
  EVT VT = WidenVT;
  unsigned NumElts = VT.getVectorNumElements();
  while (!TLI.isTypeLegal(VT) && NumElts != 1) {
    NumElts = NumElts / 2;
    VT = EVT::getVectorVT(*DAG.getContext(), WidenEltVT, NumElts);
  }

  // No legal vector version so unroll the vector operation and then widen.
  if (NumElts == 1)
    return UnrollVectorOp_StrictFP(N, WidenVT.getVectorNumElements());

  // Since the operation can trap, apply operation on the original vector.
  EVT MaxVT = VT;
  SmallVector<SDValue, 4> InOps;
  unsigned CurNumElts = N->getValueType(0).getVectorNumElements();

  SmallVector<SDValue, 16> ConcatOps(CurNumElts);
  SmallVector<SDValue, 16> Chains;
  unsigned ConcatEnd = 0;  // Current ConcatOps index.
  int Idx = 0;        // Current Idx into input vectors.

  // The Chain is the first operand.
  InOps.push_back(N->getOperand(0));

  // Now process the remaining operands.
  for (unsigned i = 1; i < NumOpers; ++i) {
    SDValue Oper = N->getOperand(i);

    EVT OpVT = Oper.getValueType();
    if (OpVT.isVector()) {
      if (getTypeAction(OpVT) == TargetLowering::TypeWidenVector)
        Oper = GetWidenedVector(Oper);
      else {
        EVT WideOpVT =
            EVT::getVectorVT(*DAG.getContext(), OpVT.getVectorElementType(),
                             WidenVT.getVectorElementCount());
        Oper = DAG.getNode(ISD::INSERT_SUBVECTOR, dl, WideOpVT,
                           DAG.getUNDEF(WideOpVT), Oper,
                           DAG.getVectorIdxConstant(0, dl));
      }
    }

    InOps.push_back(Oper);
  }

  // NumElts := greatest legal vector size (at most WidenVT)
  // while (orig. vector has unhandled elements) {
  //   take munches of size NumElts from the beginning and add to ConcatOps
  //   NumElts := next smaller supported vector size or 1
  // }
  while (CurNumElts != 0) {
    while (CurNumElts >= NumElts) {
      SmallVector<SDValue, 4> EOps;

      for (unsigned i = 0; i < NumOpers; ++i) {
        SDValue Op = InOps[i];

        EVT OpVT = Op.getValueType();
        if (OpVT.isVector()) {
          EVT OpExtractVT =
              EVT::getVectorVT(*DAG.getContext(), OpVT.getVectorElementType(),
                               VT.getVectorElementCount());
          Op = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, OpExtractVT, Op,
                           DAG.getVectorIdxConstant(Idx, dl));
        }

        EOps.push_back(Op);
      }

      EVT OperVT[] = {VT, MVT::Other};
      SDValue Oper = DAG.getNode(Opcode, dl, OperVT, EOps);
      ConcatOps[ConcatEnd++] = Oper;
      Chains.push_back(Oper.getValue(1));
      Idx += NumElts;
      CurNumElts -= NumElts;
    }
    do {
      NumElts = NumElts / 2;
      VT = EVT::getVectorVT(*DAG.getContext(), WidenEltVT, NumElts);
    } while (!TLI.isTypeLegal(VT) && NumElts != 1);

    if (NumElts == 1) {
      for (unsigned i = 0; i != CurNumElts; ++i, ++Idx) {
        SmallVector<SDValue, 4> EOps;

        for (unsigned i = 0; i < NumOpers; ++i) {
          SDValue Op = InOps[i];

          EVT OpVT = Op.getValueType();
          if (OpVT.isVector())
            Op = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl,
                             OpVT.getVectorElementType(), Op,
                             DAG.getVectorIdxConstant(Idx, dl));

          EOps.push_back(Op);
        }

        EVT WidenVT[] = {WidenEltVT, MVT::Other}; 
        SDValue Oper = DAG.getNode(Opcode, dl, WidenVT, EOps);
        ConcatOps[ConcatEnd++] = Oper;
        Chains.push_back(Oper.getValue(1));
      }
      CurNumElts = 0;
    }
  }

  // Build a factor node to remember all the Ops that have been created.
  SDValue NewChain;
  if (Chains.size() == 1)
    NewChain = Chains[0];
  else
    NewChain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Chains);
  ReplaceValueWith(SDValue(N, 1), NewChain);

  return CollectOpsToWiden(DAG, TLI, ConcatOps, ConcatEnd, VT, MaxVT, WidenVT);
}

SDValue DAGTypeLegalizer::WidenVecRes_OverflowOp(SDNode *N, unsigned ResNo) {
  SDLoc DL(N);
  EVT ResVT = N->getValueType(0);
  EVT OvVT = N->getValueType(1);
  EVT WideResVT, WideOvVT;
  SDValue WideLHS, WideRHS;

  // TODO: This might result in a widen/split loop.
  if (ResNo == 0) {
    WideResVT = TLI.getTypeToTransformTo(*DAG.getContext(), ResVT);
    WideOvVT = EVT::getVectorVT(
        *DAG.getContext(), OvVT.getVectorElementType(),
        WideResVT.getVectorNumElements());

    WideLHS = GetWidenedVector(N->getOperand(0));
    WideRHS = GetWidenedVector(N->getOperand(1));
  } else {
    WideOvVT = TLI.getTypeToTransformTo(*DAG.getContext(), OvVT);
    WideResVT = EVT::getVectorVT(
        *DAG.getContext(), ResVT.getVectorElementType(),
        WideOvVT.getVectorNumElements());

    SDValue Zero = DAG.getVectorIdxConstant(0, DL);
    WideLHS = DAG.getNode(
        ISD::INSERT_SUBVECTOR, DL, WideResVT, DAG.getUNDEF(WideResVT),
        N->getOperand(0), Zero);
    WideRHS = DAG.getNode(
        ISD::INSERT_SUBVECTOR, DL, WideResVT, DAG.getUNDEF(WideResVT),
        N->getOperand(1), Zero);
  }

  SDVTList WideVTs = DAG.getVTList(WideResVT, WideOvVT);
  SDNode *WideNode = DAG.getNode(
      N->getOpcode(), DL, WideVTs, WideLHS, WideRHS).getNode();

  // Replace the other vector result not being explicitly widened here.
  unsigned OtherNo = 1 - ResNo;
  EVT OtherVT = N->getValueType(OtherNo);
  if (getTypeAction(OtherVT) == TargetLowering::TypeWidenVector) {
    SetWidenedVector(SDValue(N, OtherNo), SDValue(WideNode, OtherNo));
  } else {
    SDValue Zero = DAG.getVectorIdxConstant(0, DL);
    SDValue OtherVal = DAG.getNode(
        ISD::EXTRACT_SUBVECTOR, DL, OtherVT, SDValue(WideNode, OtherNo), Zero);
    ReplaceValueWith(SDValue(N, OtherNo), OtherVal);
  }

  return SDValue(WideNode, ResNo);
}

SDValue DAGTypeLegalizer::WidenVecRes_Convert(SDNode *N) {
  LLVMContext &Ctx = *DAG.getContext();
  SDValue InOp = N->getOperand(0);
  SDLoc DL(N);

  EVT WidenVT = TLI.getTypeToTransformTo(Ctx, N->getValueType(0));
  ElementCount WidenEC = WidenVT.getVectorElementCount();

  EVT InVT = InOp.getValueType();

  unsigned Opcode = N->getOpcode();
  const SDNodeFlags Flags = N->getFlags();

  // Handle the case of ZERO_EXTEND where the promoted InVT element size does
  // not equal that of WidenVT.
  if (N->getOpcode() == ISD::ZERO_EXTEND &&
      getTypeAction(InVT) == TargetLowering::TypePromoteInteger &&
      TLI.getTypeToTransformTo(Ctx, InVT).getScalarSizeInBits() !=
      WidenVT.getScalarSizeInBits()) {
    InOp = ZExtPromotedInteger(InOp);
    InVT = InOp.getValueType();
    if (WidenVT.getScalarSizeInBits() < InVT.getScalarSizeInBits())
      Opcode = ISD::TRUNCATE;
  }

  EVT InEltVT = InVT.getVectorElementType();
  EVT InWidenVT = EVT::getVectorVT(Ctx, InEltVT, WidenEC);
  ElementCount InVTEC = InVT.getVectorElementCount();

  if (getTypeAction(InVT) == TargetLowering::TypeWidenVector) {
    InOp = GetWidenedVector(N->getOperand(0));
    InVT = InOp.getValueType();
    InVTEC = InVT.getVectorElementCount();
    if (InVTEC == WidenEC) {
      if (N->getNumOperands() == 1)
        return DAG.getNode(Opcode, DL, WidenVT, InOp);
      if (N->getNumOperands() == 3) {
        assert(N->isVPOpcode() && "Expected VP opcode");
        SDValue Mask =
            GetWidenedMask(N->getOperand(1), WidenVT.getVectorElementCount());
        return DAG.getNode(Opcode, DL, WidenVT, InOp, Mask, N->getOperand(2));
      }
      return DAG.getNode(Opcode, DL, WidenVT, InOp, N->getOperand(1), Flags);
    }
    if (WidenVT.getSizeInBits() == InVT.getSizeInBits()) {
      // If both input and result vector types are of same width, extend
      // operations should be done with SIGN/ZERO_EXTEND_VECTOR_INREG, which
      // accepts fewer elements in the result than in the input.
      if (Opcode == ISD::ANY_EXTEND)
        return DAG.getNode(ISD::ANY_EXTEND_VECTOR_INREG, DL, WidenVT, InOp);
      if (Opcode == ISD::SIGN_EXTEND)
        return DAG.getNode(ISD::SIGN_EXTEND_VECTOR_INREG, DL, WidenVT, InOp);
      if (Opcode == ISD::ZERO_EXTEND)
        return DAG.getNode(ISD::ZERO_EXTEND_VECTOR_INREG, DL, WidenVT, InOp);
    }
  }

  if (TLI.isTypeLegal(InWidenVT)) {
    // Because the result and the input are different vector types, widening
    // the result could create a legal type but widening the input might make
    // it an illegal type that might lead to repeatedly splitting the input
    // and then widening it. To avoid this, we widen the input only if
    // it results in a legal type.
    if (WidenEC.isKnownMultipleOf(InVTEC.getKnownMinValue())) {
      // Widen the input and call convert on the widened input vector.
      unsigned NumConcat =
          WidenEC.getKnownMinValue() / InVTEC.getKnownMinValue();
      SmallVector<SDValue, 16> Ops(NumConcat, DAG.getUNDEF(InVT));
      Ops[0] = InOp;
      SDValue InVec = DAG.getNode(ISD::CONCAT_VECTORS, DL, InWidenVT, Ops);
      if (N->getNumOperands() == 1)
        return DAG.getNode(Opcode, DL, WidenVT, InVec);
      return DAG.getNode(Opcode, DL, WidenVT, InVec, N->getOperand(1), Flags);
    }

    if (InVTEC.isKnownMultipleOf(WidenEC.getKnownMinValue())) {
      SDValue InVal = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, InWidenVT, InOp,
                                  DAG.getVectorIdxConstant(0, DL));
      // Extract the input and convert the shorten input vector.
      if (N->getNumOperands() == 1)
        return DAG.getNode(Opcode, DL, WidenVT, InVal);
      return DAG.getNode(Opcode, DL, WidenVT, InVal, N->getOperand(1), Flags);
    }
  }

  // Otherwise unroll into some nasty scalar code and rebuild the vector.
  EVT EltVT = WidenVT.getVectorElementType();
  SmallVector<SDValue, 16> Ops(WidenEC.getFixedValue(), DAG.getUNDEF(EltVT));
  // Use the original element count so we don't do more scalar opts than
  // necessary.
  unsigned MinElts = N->getValueType(0).getVectorNumElements();
  for (unsigned i=0; i < MinElts; ++i) {
    SDValue Val = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, InEltVT, InOp,
                              DAG.getVectorIdxConstant(i, DL));
    if (N->getNumOperands() == 1)
      Ops[i] = DAG.getNode(Opcode, DL, EltVT, Val);
    else
      Ops[i] = DAG.getNode(Opcode, DL, EltVT, Val, N->getOperand(1), Flags);
  }

  return DAG.getBuildVector(WidenVT, DL, Ops);
}

SDValue DAGTypeLegalizer::WidenVecRes_FP_TO_XINT_SAT(SDNode *N) {
  SDLoc dl(N);
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  ElementCount WidenNumElts = WidenVT.getVectorElementCount();

  SDValue Src = N->getOperand(0);
  EVT SrcVT = Src.getValueType();

  // Also widen the input.
  if (getTypeAction(SrcVT) == TargetLowering::TypeWidenVector) {
    Src = GetWidenedVector(Src);
    SrcVT = Src.getValueType();
  }

  // Input and output not widened to the same size, give up.
  if (WidenNumElts != SrcVT.getVectorElementCount())
    return DAG.UnrollVectorOp(N, WidenNumElts.getKnownMinValue());

  return DAG.getNode(N->getOpcode(), dl, WidenVT, Src, N->getOperand(1));
}

SDValue DAGTypeLegalizer::WidenVecRes_XRINT(SDNode *N) {
  SDLoc dl(N);
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  ElementCount WidenNumElts = WidenVT.getVectorElementCount();

  SDValue Src = N->getOperand(0);
  EVT SrcVT = Src.getValueType();

  // Also widen the input.
  if (getTypeAction(SrcVT) == TargetLowering::TypeWidenVector) {
    Src = GetWidenedVector(Src);
    SrcVT = Src.getValueType();
  }

  // Input and output not widened to the same size, give up.
  if (WidenNumElts != SrcVT.getVectorElementCount())
    return DAG.UnrollVectorOp(N, WidenNumElts.getKnownMinValue());

  if (N->getNumOperands() == 1)
    return DAG.getNode(N->getOpcode(), dl, WidenVT, Src);

  assert(N->getNumOperands() == 3 && "Unexpected number of operands!");
  assert(N->isVPOpcode() && "Expected VP opcode");

  SDValue Mask =
      GetWidenedMask(N->getOperand(1), WidenVT.getVectorElementCount());
  return DAG.getNode(N->getOpcode(), dl, WidenVT, Src, Mask, N->getOperand(2));
}

SDValue DAGTypeLegalizer::WidenVecRes_Convert_StrictFP(SDNode *N) {
  SDValue InOp = N->getOperand(1);
  SDLoc DL(N);
  SmallVector<SDValue, 4> NewOps(N->op_begin(), N->op_end());

  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  unsigned WidenNumElts = WidenVT.getVectorNumElements();

  EVT InVT = InOp.getValueType();
  EVT InEltVT = InVT.getVectorElementType();

  unsigned Opcode = N->getOpcode();

  // FIXME: Optimizations need to be implemented here.

  // Otherwise unroll into some nasty scalar code and rebuild the vector.
  EVT EltVT = WidenVT.getVectorElementType();
  std::array<EVT, 2> EltVTs = {{EltVT, MVT::Other}};
  SmallVector<SDValue, 16> Ops(WidenNumElts, DAG.getUNDEF(EltVT));
  SmallVector<SDValue, 32> OpChains;
  // Use the original element count so we don't do more scalar opts than
  // necessary.
  unsigned MinElts = N->getValueType(0).getVectorNumElements();
  for (unsigned i=0; i < MinElts; ++i) {
    NewOps[1] = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, InEltVT, InOp,
                            DAG.getVectorIdxConstant(i, DL));
    Ops[i] = DAG.getNode(Opcode, DL, EltVTs, NewOps);
    OpChains.push_back(Ops[i].getValue(1));
  }
  SDValue NewChain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, OpChains);
  ReplaceValueWith(SDValue(N, 1), NewChain);

  return DAG.getBuildVector(WidenVT, DL, Ops);
}

SDValue DAGTypeLegalizer::WidenVecRes_EXTEND_VECTOR_INREG(SDNode *N) {
  unsigned Opcode = N->getOpcode();
  SDValue InOp = N->getOperand(0);
  SDLoc DL(N);

  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  EVT WidenSVT = WidenVT.getVectorElementType();
  unsigned WidenNumElts = WidenVT.getVectorNumElements();

  EVT InVT = InOp.getValueType();
  EVT InSVT = InVT.getVectorElementType();
  unsigned InVTNumElts = InVT.getVectorNumElements();

  if (getTypeAction(InVT) == TargetLowering::TypeWidenVector) {
    InOp = GetWidenedVector(InOp);
    InVT = InOp.getValueType();
    if (InVT.getSizeInBits() == WidenVT.getSizeInBits()) {
      switch (Opcode) {
      case ISD::ANY_EXTEND_VECTOR_INREG:
      case ISD::SIGN_EXTEND_VECTOR_INREG:
      case ISD::ZERO_EXTEND_VECTOR_INREG:
        return DAG.getNode(Opcode, DL, WidenVT, InOp);
      }
    }
  }

  // Unroll, extend the scalars and rebuild the vector.
  SmallVector<SDValue, 16> Ops;
  for (unsigned i = 0, e = std::min(InVTNumElts, WidenNumElts); i != e; ++i) {
    SDValue Val = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, InSVT, InOp,
                              DAG.getVectorIdxConstant(i, DL));
    switch (Opcode) {
    case ISD::ANY_EXTEND_VECTOR_INREG:
      Val = DAG.getNode(ISD::ANY_EXTEND, DL, WidenSVT, Val);
      break;
    case ISD::SIGN_EXTEND_VECTOR_INREG:
      Val = DAG.getNode(ISD::SIGN_EXTEND, DL, WidenSVT, Val);
      break;
    case ISD::ZERO_EXTEND_VECTOR_INREG:
      Val = DAG.getNode(ISD::ZERO_EXTEND, DL, WidenSVT, Val);
      break;
    default:
      llvm_unreachable("A *_EXTEND_VECTOR_INREG node was expected");
    }
    Ops.push_back(Val);
  }

  while (Ops.size() != WidenNumElts)
    Ops.push_back(DAG.getUNDEF(WidenSVT));

  return DAG.getBuildVector(WidenVT, DL, Ops);
}

SDValue DAGTypeLegalizer::WidenVecRes_FCOPYSIGN(SDNode *N) {
  // If this is an FCOPYSIGN with same input types, we can treat it as a
  // normal (can trap) binary op.
  if (N->getOperand(0).getValueType() == N->getOperand(1).getValueType())
    return WidenVecRes_BinaryCanTrap(N);

  // If the types are different, fall back to unrolling.
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  return DAG.UnrollVectorOp(N, WidenVT.getVectorNumElements());
}

/// Result and first source operand are different scalar types, but must have
/// the same number of elements. There is an additional control argument which
/// should be passed through unchanged.
SDValue DAGTypeLegalizer::WidenVecRes_UnarySameEltsWithScalarArg(SDNode *N) {
  SDValue FpValue = N->getOperand(0);
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  if (getTypeAction(FpValue.getValueType()) != TargetLowering::TypeWidenVector)
    return DAG.UnrollVectorOp(N, WidenVT.getVectorNumElements());
  SDValue Arg = GetWidenedVector(FpValue);
  return DAG.getNode(N->getOpcode(), SDLoc(N), WidenVT, {Arg, N->getOperand(1)},
                     N->getFlags());
}

SDValue DAGTypeLegalizer::WidenVecRes_ExpOp(SDNode *N) {
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue InOp = GetWidenedVector(N->getOperand(0));
  SDValue RHS = N->getOperand(1);
  EVT ExpVT = RHS.getValueType();
  SDValue ExpOp = RHS;
  if (ExpVT.isVector()) {
    EVT WideExpVT =
        WidenVT.changeVectorElementType(ExpVT.getVectorElementType());
    ExpOp = ModifyToType(RHS, WideExpVT);
  }

  return DAG.getNode(N->getOpcode(), SDLoc(N), WidenVT, InOp, ExpOp);
}

SDValue DAGTypeLegalizer::WidenVecRes_Unary(SDNode *N) {
  // Unary op widening.
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue InOp = GetWidenedVector(N->getOperand(0));
  if (N->getNumOperands() == 1)
    return DAG.getNode(N->getOpcode(), SDLoc(N), WidenVT, InOp, N->getFlags());

  assert(N->getNumOperands() == 3 && "Unexpected number of operands!");
  assert(N->isVPOpcode() && "Expected VP opcode");

  SDValue Mask =
      GetWidenedMask(N->getOperand(1), WidenVT.getVectorElementCount());
  return DAG.getNode(N->getOpcode(), SDLoc(N), WidenVT,
                     {InOp, Mask, N->getOperand(2)});
}

SDValue DAGTypeLegalizer::WidenVecRes_InregOp(SDNode *N) {
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  EVT ExtVT = EVT::getVectorVT(*DAG.getContext(),
                               cast<VTSDNode>(N->getOperand(1))->getVT()
                                 .getVectorElementType(),
                               WidenVT.getVectorNumElements());
  SDValue WidenLHS = GetWidenedVector(N->getOperand(0));
  return DAG.getNode(N->getOpcode(), SDLoc(N),
                     WidenVT, WidenLHS, DAG.getValueType(ExtVT));
}

SDValue DAGTypeLegalizer::WidenVecRes_MERGE_VALUES(SDNode *N, unsigned ResNo) {
  SDValue WidenVec = DisintegrateMERGE_VALUES(N, ResNo);
  return GetWidenedVector(WidenVec);
}

SDValue DAGTypeLegalizer::WidenVecRes_ADDRSPACECAST(SDNode *N) {
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue InOp = GetWidenedVector(N->getOperand(0));
  auto *AddrSpaceCastN = cast<AddrSpaceCastSDNode>(N);

  return DAG.getAddrSpaceCast(SDLoc(N), WidenVT, InOp,
                              AddrSpaceCastN->getSrcAddressSpace(),
                              AddrSpaceCastN->getDestAddressSpace());
}

SDValue DAGTypeLegalizer::WidenVecRes_BITCAST(SDNode *N) {
  SDValue InOp = N->getOperand(0);
  EVT InVT = InOp.getValueType();
  EVT VT = N->getValueType(0);
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDLoc dl(N);

  switch (getTypeAction(InVT)) {
  case TargetLowering::TypeLegal:
    break;
  case TargetLowering::TypeScalarizeScalableVector:
    report_fatal_error("Scalarization of scalable vectors is not supported.");
  case TargetLowering::TypePromoteInteger: {
    // If the incoming type is a vector that is being promoted, then
    // we know that the elements are arranged differently and that we
    // must perform the conversion using a stack slot.
    if (InVT.isVector())
      break;

    // If the InOp is promoted to the same size, convert it.  Otherwise,
    // fall out of the switch and widen the promoted input.
    SDValue NInOp = GetPromotedInteger(InOp);
    EVT NInVT = NInOp.getValueType();
    if (WidenVT.bitsEq(NInVT)) {
      // For big endian targets we need to shift the input integer or the
      // interesting bits will end up at the wrong place.
      if (DAG.getDataLayout().isBigEndian()) {
        unsigned ShiftAmt = NInVT.getSizeInBits() - InVT.getSizeInBits();
        EVT ShiftAmtTy = TLI.getShiftAmountTy(NInVT, DAG.getDataLayout());
        assert(ShiftAmt < WidenVT.getSizeInBits() && "Too large shift amount!");
        NInOp = DAG.getNode(ISD::SHL, dl, NInVT, NInOp,
                           DAG.getConstant(ShiftAmt, dl, ShiftAmtTy));
      }
      return DAG.getNode(ISD::BITCAST, dl, WidenVT, NInOp);
    }
    InOp = NInOp;
    InVT = NInVT;
    break;
  }
  case TargetLowering::TypeSoftenFloat:
  case TargetLowering::TypePromoteFloat:
  case TargetLowering::TypeSoftPromoteHalf:
  case TargetLowering::TypeExpandInteger:
  case TargetLowering::TypeExpandFloat:
  case TargetLowering::TypeScalarizeVector:
  case TargetLowering::TypeSplitVector:
    break;
  case TargetLowering::TypeWidenVector:
    // If the InOp is widened to the same size, convert it.  Otherwise, fall
    // out of the switch and widen the widened input.
    InOp = GetWidenedVector(InOp);
    InVT = InOp.getValueType();
    if (WidenVT.bitsEq(InVT))
      // The input widens to the same size. Convert to the widen value.
      return DAG.getNode(ISD::BITCAST, dl, WidenVT, InOp);
    break;
  }

  unsigned WidenSize = WidenVT.getSizeInBits();
  unsigned InSize = InVT.getSizeInBits();
  unsigned InScalarSize = InVT.getScalarSizeInBits();
  // x86mmx is not an acceptable vector element type, so don't try.
  if (WidenSize % InScalarSize == 0 && InVT != MVT::x86mmx) {
    // Determine new input vector type.  The new input vector type will use
    // the same element type (if its a vector) or use the input type as a
    // vector.  It is the same size as the type to widen to.
    EVT NewInVT;
    unsigned NewNumParts = WidenSize / InSize;
    if (InVT.isVector()) {
      EVT InEltVT = InVT.getVectorElementType();
      NewInVT = EVT::getVectorVT(*DAG.getContext(), InEltVT,
                                 WidenSize / InEltVT.getSizeInBits());
    } else {
      // For big endian systems, using the promoted input scalar type
      // to produce the scalar_to_vector would put the desired bits into
      // the least significant byte(s) of the wider element zero. This
      // will mean that the users of the result vector are using incorrect
      // bits. Use the original input type instead. Although either input
      // type can be used on little endian systems, for consistency we
      // use the original type there as well.
      EVT OrigInVT = N->getOperand(0).getValueType();
      NewNumParts = WidenSize / OrigInVT.getSizeInBits();
      NewInVT = EVT::getVectorVT(*DAG.getContext(), OrigInVT, NewNumParts);
    }

    if (TLI.isTypeLegal(NewInVT)) {
      SDValue NewVec;
      if (InVT.isVector()) {
        // Because the result and the input are different vector types, widening
        // the result could create a legal type but widening the input might
        // make it an illegal type that might lead to repeatedly splitting the
        // input and then widening it. To avoid this, we widen the input only if
        // it results in a legal type.
        if (WidenSize % InSize == 0) {
          SmallVector<SDValue, 16> Ops(NewNumParts, DAG.getUNDEF(InVT));
          Ops[0] = InOp;

          NewVec = DAG.getNode(ISD::CONCAT_VECTORS, dl, NewInVT, Ops);
        } else {
          SmallVector<SDValue, 16> Ops;
          DAG.ExtractVectorElements(InOp, Ops);
          Ops.append(WidenSize / InScalarSize - Ops.size(),
                     DAG.getUNDEF(InVT.getVectorElementType()));

          NewVec = DAG.getNode(ISD::BUILD_VECTOR, dl, NewInVT, Ops);
        }
      } else {
        NewVec = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, NewInVT, InOp);
      }
      return DAG.getNode(ISD::BITCAST, dl, WidenVT, NewVec);
    }
  }

  return CreateStackStoreLoad(InOp, WidenVT);
}

SDValue DAGTypeLegalizer::WidenVecRes_BUILD_VECTOR(SDNode *N) {
  SDLoc dl(N);
  // Build a vector with undefined for the new nodes.
  EVT VT = N->getValueType(0);

  // Integer BUILD_VECTOR operands may be larger than the node's vector element
  // type. The UNDEFs need to have the same type as the existing operands.
  EVT EltVT = N->getOperand(0).getValueType();
  unsigned NumElts = VT.getVectorNumElements();

  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  unsigned WidenNumElts = WidenVT.getVectorNumElements();

  SmallVector<SDValue, 16> NewOps(N->op_begin(), N->op_end());
  assert(WidenNumElts >= NumElts && "Shrinking vector instead of widening!");
  NewOps.append(WidenNumElts - NumElts, DAG.getUNDEF(EltVT));

  return DAG.getBuildVector(WidenVT, dl, NewOps);
}

SDValue DAGTypeLegalizer::WidenVecRes_CONCAT_VECTORS(SDNode *N) {
  EVT InVT = N->getOperand(0).getValueType();
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDLoc dl(N);
  unsigned NumOperands = N->getNumOperands();

  bool InputWidened = false; // Indicates we need to widen the input.
  if (getTypeAction(InVT) != TargetLowering::TypeWidenVector) {
    unsigned WidenNumElts = WidenVT.getVectorMinNumElements();
    unsigned NumInElts = InVT.getVectorMinNumElements();
    if (WidenNumElts % NumInElts == 0) {
      // Add undef vectors to widen to correct length.
      unsigned NumConcat = WidenNumElts / NumInElts;
      SDValue UndefVal = DAG.getUNDEF(InVT);
      SmallVector<SDValue, 16> Ops(NumConcat);
      for (unsigned i=0; i < NumOperands; ++i)
        Ops[i] = N->getOperand(i);
      for (unsigned i = NumOperands; i != NumConcat; ++i)
        Ops[i] = UndefVal;
      return DAG.getNode(ISD::CONCAT_VECTORS, dl, WidenVT, Ops);
    }
  } else {
    InputWidened = true;
    if (WidenVT == TLI.getTypeToTransformTo(*DAG.getContext(), InVT)) {
      // The inputs and the result are widen to the same value.
      unsigned i;
      for (i=1; i < NumOperands; ++i)
        if (!N->getOperand(i).isUndef())
          break;

      if (i == NumOperands)
        // Everything but the first operand is an UNDEF so just return the
        // widened first operand.
        return GetWidenedVector(N->getOperand(0));

      if (NumOperands == 2) {
        assert(!WidenVT.isScalableVector() &&
               "Cannot use vector shuffles to widen CONCAT_VECTOR result");
        unsigned WidenNumElts = WidenVT.getVectorNumElements();
        unsigned NumInElts = InVT.getVectorNumElements();

        // Replace concat of two operands with a shuffle.
        SmallVector<int, 16> MaskOps(WidenNumElts, -1);
        for (unsigned i = 0; i < NumInElts; ++i) {
          MaskOps[i] = i;
          MaskOps[i + NumInElts] = i + WidenNumElts;
        }
        return DAG.getVectorShuffle(WidenVT, dl,
                                    GetWidenedVector(N->getOperand(0)),
                                    GetWidenedVector(N->getOperand(1)),
                                    MaskOps);
      }
    }
  }

  assert(!WidenVT.isScalableVector() &&
         "Cannot use build vectors to widen CONCAT_VECTOR result");
  unsigned WidenNumElts = WidenVT.getVectorNumElements();
  unsigned NumInElts = InVT.getVectorNumElements();

  // Fall back to use extracts and build vector.
  EVT EltVT = WidenVT.getVectorElementType();
  SmallVector<SDValue, 16> Ops(WidenNumElts);
  unsigned Idx = 0;
  for (unsigned i=0; i < NumOperands; ++i) {
    SDValue InOp = N->getOperand(i);
    if (InputWidened)
      InOp = GetWidenedVector(InOp);
    for (unsigned j = 0; j < NumInElts; ++j)
      Ops[Idx++] = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, InOp,
                               DAG.getVectorIdxConstant(j, dl));
  }
  SDValue UndefVal = DAG.getUNDEF(EltVT);
  for (; Idx < WidenNumElts; ++Idx)
    Ops[Idx] = UndefVal;
  return DAG.getBuildVector(WidenVT, dl, Ops);
}

SDValue DAGTypeLegalizer::WidenVecRes_INSERT_SUBVECTOR(SDNode *N) {
  EVT VT = N->getValueType(0);
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue InOp1 = GetWidenedVector(N->getOperand(0));
  SDValue InOp2 = N->getOperand(1);
  SDValue Idx = N->getOperand(2);
  SDLoc dl(N);
  return DAG.getNode(ISD::INSERT_SUBVECTOR, dl, WidenVT, InOp1, InOp2, Idx);
}

SDValue DAGTypeLegalizer::WidenVecRes_EXTRACT_SUBVECTOR(SDNode *N) {
  EVT VT = N->getValueType(0);
  EVT EltVT = VT.getVectorElementType();
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue InOp = N->getOperand(0);
  SDValue Idx = N->getOperand(1);
  SDLoc dl(N);

  auto InOpTypeAction = getTypeAction(InOp.getValueType());
  if (InOpTypeAction == TargetLowering::TypeWidenVector)
    InOp = GetWidenedVector(InOp);

  EVT InVT = InOp.getValueType();

  // Check if we can just return the input vector after widening.
  uint64_t IdxVal = Idx->getAsZExtVal();
  if (IdxVal == 0 && InVT == WidenVT)
    return InOp;

  // Check if we can extract from the vector.
  unsigned WidenNumElts = WidenVT.getVectorMinNumElements();
  unsigned InNumElts = InVT.getVectorMinNumElements();
  unsigned VTNumElts = VT.getVectorMinNumElements();
  assert(IdxVal % VTNumElts == 0 &&
         "Expected Idx to be a multiple of subvector minimum vector length");
  if (IdxVal % WidenNumElts == 0 && IdxVal + WidenNumElts < InNumElts)
    return DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, WidenVT, InOp, Idx);

  if (VT.isScalableVector()) {
    // Try to split the operation up into smaller extracts and concat the
    // results together, e.g.
    //    nxv6i64 extract_subvector(nxv12i64, 6)
    // <->
    //  nxv8i64 concat(
    //    nxv2i64 extract_subvector(nxv16i64, 6)
    //    nxv2i64 extract_subvector(nxv16i64, 8)
    //    nxv2i64 extract_subvector(nxv16i64, 10)
    //    undef)
    unsigned GCD = std::gcd(VTNumElts, WidenNumElts);
    assert((IdxVal % GCD) == 0 && "Expected Idx to be a multiple of the broken "
                                  "down type's element count");
    EVT PartVT = EVT::getVectorVT(*DAG.getContext(), EltVT,
                                  ElementCount::getScalable(GCD));
    // Avoid recursion around e.g. nxv1i8.
    if (getTypeAction(PartVT) != TargetLowering::TypeWidenVector) {
      SmallVector<SDValue> Parts;
      unsigned I = 0;
      for (; I < VTNumElts / GCD; ++I)
        Parts.push_back(
            DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, PartVT, InOp,
                        DAG.getVectorIdxConstant(IdxVal + I * GCD, dl)));
      for (; I < WidenNumElts / GCD; ++I)
        Parts.push_back(DAG.getUNDEF(PartVT));

      return DAG.getNode(ISD::CONCAT_VECTORS, dl, WidenVT, Parts);
    }

    report_fatal_error("Don't know how to widen the result of "
                       "EXTRACT_SUBVECTOR for scalable vectors");
  }

  // We could try widening the input to the right length but for now, extract
  // the original elements, fill the rest with undefs and build a vector.
  SmallVector<SDValue, 16> Ops(WidenNumElts);
  unsigned i;
  for (i = 0; i < VTNumElts; ++i)
    Ops[i] = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, InOp,
                         DAG.getVectorIdxConstant(IdxVal + i, dl));

  SDValue UndefVal = DAG.getUNDEF(EltVT);
  for (; i < WidenNumElts; ++i)
    Ops[i] = UndefVal;
  return DAG.getBuildVector(WidenVT, dl, Ops);
}

SDValue DAGTypeLegalizer::WidenVecRes_AssertZext(SDNode *N) {
  SDValue InOp = ModifyToType(
      N->getOperand(0),
      TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0)), true);
  return DAG.getNode(ISD::AssertZext, SDLoc(N), InOp.getValueType(), InOp,
                     N->getOperand(1));
}

SDValue DAGTypeLegalizer::WidenVecRes_INSERT_VECTOR_ELT(SDNode *N) {
  SDValue InOp = GetWidenedVector(N->getOperand(0));
  return DAG.getNode(ISD::INSERT_VECTOR_ELT, SDLoc(N),
                     InOp.getValueType(), InOp,
                     N->getOperand(1), N->getOperand(2));
}

SDValue DAGTypeLegalizer::WidenVecRes_LOAD(SDNode *N) {
  LoadSDNode *LD = cast<LoadSDNode>(N);
  ISD::LoadExtType ExtType = LD->getExtensionType();

  // A vector must always be stored in memory as-is, i.e. without any padding
  // between the elements, since various code depend on it, e.g. in the
  // handling of a bitcast of a vector type to int, which may be done with a
  // vector store followed by an integer load. A vector that does not have
  // elements that are byte-sized must therefore be stored as an integer
  // built out of the extracted vector elements.
  if (!LD->getMemoryVT().isByteSized()) {
    SDValue Value, NewChain;
    std::tie(Value, NewChain) = TLI.scalarizeVectorLoad(LD, DAG);
    ReplaceValueWith(SDValue(LD, 0), Value);
    ReplaceValueWith(SDValue(LD, 1), NewChain);
    return SDValue();
  }

  // Generate a vector-predicated load if it is custom/legal on the target. To
  // avoid possible recursion, only do this if the widened mask type is legal.
  // FIXME: Not all targets may support EVL in VP_LOAD. These will have been
  // removed from the IR by the ExpandVectorPredication pass but we're
  // reintroducing them here.
  EVT LdVT = LD->getMemoryVT();
  EVT WideVT = TLI.getTypeToTransformTo(*DAG.getContext(), LdVT);
  EVT WideMaskVT = EVT::getVectorVT(*DAG.getContext(), MVT::i1,
                                    WideVT.getVectorElementCount());
  if (ExtType == ISD::NON_EXTLOAD &&
      TLI.isOperationLegalOrCustom(ISD::VP_LOAD, WideVT) &&
      TLI.isTypeLegal(WideMaskVT)) {
    SDLoc DL(N);
    SDValue Mask = DAG.getAllOnesConstant(DL, WideMaskVT);
    SDValue EVL = DAG.getElementCount(DL, TLI.getVPExplicitVectorLengthTy(),
                                      LdVT.getVectorElementCount());
    const auto *MMO = LD->getMemOperand();
    SDValue NewLoad =
        DAG.getLoadVP(WideVT, DL, LD->getChain(), LD->getBasePtr(), Mask, EVL,
                      MMO->getPointerInfo(), MMO->getAlign(), MMO->getFlags(),
                      MMO->getAAInfo());

    // Modified the chain - switch anything that used the old chain to use
    // the new one.
    ReplaceValueWith(SDValue(N, 1), NewLoad.getValue(1));

    return NewLoad;
  }

  SDValue Result;
  SmallVector<SDValue, 16> LdChain; // Chain for the series of load
  if (ExtType != ISD::NON_EXTLOAD)
    Result = GenWidenVectorExtLoads(LdChain, LD, ExtType);
  else
    Result = GenWidenVectorLoads(LdChain, LD);

  if (Result) {
    // If we generate a single load, we can use that for the chain.  Otherwise,
    // build a factor node to remember the multiple loads are independent and
    // chain to that.
    SDValue NewChain;
    if (LdChain.size() == 1)
      NewChain = LdChain[0];
    else
      NewChain = DAG.getNode(ISD::TokenFactor, SDLoc(LD), MVT::Other, LdChain);

    // Modified the chain - switch anything that used the old chain to use
    // the new one.
    ReplaceValueWith(SDValue(N, 1), NewChain);

    return Result;
  }

  report_fatal_error("Unable to widen vector load");
}

SDValue DAGTypeLegalizer::WidenVecRes_VP_LOAD(VPLoadSDNode *N) {
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Mask = N->getMask();
  SDValue EVL = N->getVectorLength();
  ISD::LoadExtType ExtType = N->getExtensionType();
  SDLoc dl(N);

  // The mask should be widened as well
  assert(getTypeAction(Mask.getValueType()) ==
             TargetLowering::TypeWidenVector &&
         "Unable to widen binary VP op");
  Mask = GetWidenedVector(Mask);
  assert(Mask.getValueType().getVectorElementCount() ==
             TLI.getTypeToTransformTo(*DAG.getContext(), Mask.getValueType())
                 .getVectorElementCount() &&
         "Unable to widen vector load");

  SDValue Res =
      DAG.getLoadVP(N->getAddressingMode(), ExtType, WidenVT, dl, N->getChain(),
                    N->getBasePtr(), N->getOffset(), Mask, EVL,
                    N->getMemoryVT(), N->getMemOperand(), N->isExpandingLoad());
  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  return Res;
}

SDValue DAGTypeLegalizer::WidenVecRes_VP_STRIDED_LOAD(VPStridedLoadSDNode *N) {
  SDLoc DL(N);

  // The mask should be widened as well
  SDValue Mask = N->getMask();
  assert(getTypeAction(Mask.getValueType()) ==
             TargetLowering::TypeWidenVector &&
         "Unable to widen VP strided load");
  Mask = GetWidenedVector(Mask);

  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  assert(Mask.getValueType().getVectorElementCount() ==
             WidenVT.getVectorElementCount() &&
         "Data and mask vectors should have the same number of elements");

  SDValue Res = DAG.getStridedLoadVP(
      N->getAddressingMode(), N->getExtensionType(), WidenVT, DL, N->getChain(),
      N->getBasePtr(), N->getOffset(), N->getStride(), Mask,
      N->getVectorLength(), N->getMemoryVT(), N->getMemOperand(),
      N->isExpandingLoad());

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  return Res;
}

SDValue DAGTypeLegalizer::WidenVecRes_VECTOR_COMPRESS(SDNode *N) {
  SDValue Vec = N->getOperand(0);
  SDValue Mask = N->getOperand(1);
  SDValue Passthru = N->getOperand(2);
  EVT WideVecVT =
      TLI.getTypeToTransformTo(*DAG.getContext(), Vec.getValueType());
  EVT WideMaskVT = EVT::getVectorVT(*DAG.getContext(),
                                    Mask.getValueType().getVectorElementType(),
                                    WideVecVT.getVectorNumElements());

  SDValue WideVec = ModifyToType(Vec, WideVecVT);
  SDValue WideMask = ModifyToType(Mask, WideMaskVT, /*FillWithZeroes=*/true);
  SDValue WidePassthru = ModifyToType(Passthru, WideVecVT);
  return DAG.getNode(ISD::VECTOR_COMPRESS, SDLoc(N), WideVecVT, WideVec,
                     WideMask, WidePassthru);
}

SDValue DAGTypeLegalizer::WidenVecRes_MLOAD(MaskedLoadSDNode *N) {

  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(),N->getValueType(0));
  SDValue Mask = N->getMask();
  EVT MaskVT = Mask.getValueType();
  SDValue PassThru = GetWidenedVector(N->getPassThru());
  ISD::LoadExtType ExtType = N->getExtensionType();
  SDLoc dl(N);

  // The mask should be widened as well
  EVT WideMaskVT = EVT::getVectorVT(*DAG.getContext(),
                                    MaskVT.getVectorElementType(),
                                    WidenVT.getVectorNumElements());
  Mask = ModifyToType(Mask, WideMaskVT, true);

  SDValue Res = DAG.getMaskedLoad(
      WidenVT, dl, N->getChain(), N->getBasePtr(), N->getOffset(), Mask,
      PassThru, N->getMemoryVT(), N->getMemOperand(), N->getAddressingMode(),
      ExtType, N->isExpandingLoad());
  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  return Res;
}

SDValue DAGTypeLegalizer::WidenVecRes_MGATHER(MaskedGatherSDNode *N) {

  EVT WideVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Mask = N->getMask();
  EVT MaskVT = Mask.getValueType();
  SDValue PassThru = GetWidenedVector(N->getPassThru());
  SDValue Scale = N->getScale();
  unsigned NumElts = WideVT.getVectorNumElements();
  SDLoc dl(N);

  // The mask should be widened as well
  EVT WideMaskVT = EVT::getVectorVT(*DAG.getContext(),
                                    MaskVT.getVectorElementType(),
                                    WideVT.getVectorNumElements());
  Mask = ModifyToType(Mask, WideMaskVT, true);

  // Widen the Index operand
  SDValue Index = N->getIndex();
  EVT WideIndexVT = EVT::getVectorVT(*DAG.getContext(),
                                     Index.getValueType().getScalarType(),
                                     NumElts);
  Index = ModifyToType(Index, WideIndexVT);
  SDValue Ops[] = { N->getChain(), PassThru, Mask, N->getBasePtr(), Index,
                    Scale };

  // Widen the MemoryType
  EVT WideMemVT = EVT::getVectorVT(*DAG.getContext(),
                                   N->getMemoryVT().getScalarType(), NumElts);
  SDValue Res = DAG.getMaskedGather(DAG.getVTList(WideVT, MVT::Other),
                                    WideMemVT, dl, Ops, N->getMemOperand(),
                                    N->getIndexType(), N->getExtensionType());

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  return Res;
}

SDValue DAGTypeLegalizer::WidenVecRes_VP_GATHER(VPGatherSDNode *N) {
  EVT WideVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  SDValue Mask = N->getMask();
  SDValue Scale = N->getScale();
  ElementCount WideEC = WideVT.getVectorElementCount();
  SDLoc dl(N);

  SDValue Index = GetWidenedVector(N->getIndex());
  EVT WideMemVT = EVT::getVectorVT(*DAG.getContext(),
                                   N->getMemoryVT().getScalarType(), WideEC);
  Mask = GetWidenedMask(Mask, WideEC);

  SDValue Ops[] = {N->getChain(), N->getBasePtr(),     Index, Scale,
                   Mask,          N->getVectorLength()};
  SDValue Res = DAG.getGatherVP(DAG.getVTList(WideVT, MVT::Other), WideMemVT,
                                dl, Ops, N->getMemOperand(), N->getIndexType());

  // Legalize the chain result - switch anything that used the old chain to
  // use the new one.
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  return Res;
}

SDValue DAGTypeLegalizer::WidenVecRes_ScalarOp(SDNode *N) {
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  if (N->isVPOpcode())
    return DAG.getNode(N->getOpcode(), SDLoc(N), WidenVT, N->getOperand(0),
                       N->getOperand(1), N->getOperand(2));
  return DAG.getNode(N->getOpcode(), SDLoc(N), WidenVT, N->getOperand(0));
}

// Return true is this is a SETCC node or a strict version of it.
static inline bool isSETCCOp(unsigned Opcode) {
  switch (Opcode) {
  case ISD::SETCC:
  case ISD::STRICT_FSETCC:
  case ISD::STRICT_FSETCCS:
    return true;
  }
  return false;
}

// Return true if this is a node that could have two SETCCs as operands.
static inline bool isLogicalMaskOp(unsigned Opcode) {
  switch (Opcode) {
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR:
    return true;
  }
  return false;
}

// If N is a SETCC or a strict variant of it, return the type
// of the compare operands.
static inline EVT getSETCCOperandType(SDValue N) {
  unsigned OpNo = N->isStrictFPOpcode() ? 1 : 0;
  return N->getOperand(OpNo).getValueType();
}

// This is used just for the assert in convertMask(). Check that this either
// a SETCC or a previously handled SETCC by convertMask().
#ifndef NDEBUG
static inline bool isSETCCorConvertedSETCC(SDValue N) {
  if (N.getOpcode() == ISD::EXTRACT_SUBVECTOR)
    N = N.getOperand(0);
  else if (N.getOpcode() == ISD::CONCAT_VECTORS) {
    for (unsigned i = 1; i < N->getNumOperands(); ++i)
      if (!N->getOperand(i)->isUndef())
        return false;
    N = N.getOperand(0);
  }

  if (N.getOpcode() == ISD::TRUNCATE)
    N = N.getOperand(0);
  else if (N.getOpcode() == ISD::SIGN_EXTEND)
    N = N.getOperand(0);

  if (isLogicalMaskOp(N.getOpcode()))
    return isSETCCorConvertedSETCC(N.getOperand(0)) &&
           isSETCCorConvertedSETCC(N.getOperand(1));

  return (isSETCCOp(N.getOpcode()) ||
          ISD::isBuildVectorOfConstantSDNodes(N.getNode()));
}
#endif

// Return a mask of vector type MaskVT to replace InMask. Also adjust MaskVT
// to ToMaskVT if needed with vector extension or truncation.
SDValue DAGTypeLegalizer::convertMask(SDValue InMask, EVT MaskVT,
                                      EVT ToMaskVT) {
  // Currently a SETCC or a AND/OR/XOR with two SETCCs are handled.
  // FIXME: This code seems to be too restrictive, we might consider
  // generalizing it or dropping it.
  assert(isSETCCorConvertedSETCC(InMask) && "Unexpected mask argument.");

  // Make a new Mask node, with a legal result VT.
  SDValue Mask;
  SmallVector<SDValue, 4> Ops;
  for (unsigned i = 0, e = InMask->getNumOperands(); i < e; ++i)
    Ops.push_back(InMask->getOperand(i));
  if (InMask->isStrictFPOpcode()) {
    Mask = DAG.getNode(InMask->getOpcode(), SDLoc(InMask),
                       { MaskVT, MVT::Other }, Ops);
    ReplaceValueWith(InMask.getValue(1), Mask.getValue(1));
  }
  else
    Mask = DAG.getNode(InMask->getOpcode(), SDLoc(InMask), MaskVT, Ops);

  // If MaskVT has smaller or bigger elements than ToMaskVT, a vector sign
  // extend or truncate is needed.
  LLVMContext &Ctx = *DAG.getContext();
  unsigned MaskScalarBits = MaskVT.getScalarSizeInBits();
  unsigned ToMaskScalBits = ToMaskVT.getScalarSizeInBits();
  if (MaskScalarBits < ToMaskScalBits) {
    EVT ExtVT = EVT::getVectorVT(Ctx, ToMaskVT.getVectorElementType(),
                                 MaskVT.getVectorNumElements());
    Mask = DAG.getNode(ISD::SIGN_EXTEND, SDLoc(Mask), ExtVT, Mask);
  } else if (MaskScalarBits > ToMaskScalBits) {
    EVT TruncVT = EVT::getVectorVT(Ctx, ToMaskVT.getVectorElementType(),
                                   MaskVT.getVectorNumElements());
    Mask = DAG.getNode(ISD::TRUNCATE, SDLoc(Mask), TruncVT, Mask);
  }

  assert(Mask->getValueType(0).getScalarSizeInBits() ==
             ToMaskVT.getScalarSizeInBits() &&
         "Mask should have the right element size by now.");

  // Adjust Mask to the right number of elements.
  unsigned CurrMaskNumEls = Mask->getValueType(0).getVectorNumElements();
  if (CurrMaskNumEls > ToMaskVT.getVectorNumElements()) {
    SDValue ZeroIdx = DAG.getVectorIdxConstant(0, SDLoc(Mask));
    Mask = DAG.getNode(ISD::EXTRACT_SUBVECTOR, SDLoc(Mask), ToMaskVT, Mask,
                       ZeroIdx);
  } else if (CurrMaskNumEls < ToMaskVT.getVectorNumElements()) {
    unsigned NumSubVecs = (ToMaskVT.getVectorNumElements() / CurrMaskNumEls);
    EVT SubVT = Mask->getValueType(0);
    SmallVector<SDValue, 16> SubOps(NumSubVecs, DAG.getUNDEF(SubVT));
    SubOps[0] = Mask;
    Mask = DAG.getNode(ISD::CONCAT_VECTORS, SDLoc(Mask), ToMaskVT, SubOps);
  }

  assert((Mask->getValueType(0) == ToMaskVT) &&
         "A mask of ToMaskVT should have been produced by now.");

  return Mask;
}

// This method tries to handle some special cases for the vselect mask
// and if needed adjusting the mask vector type to match that of the VSELECT.
// Without it, many cases end up with scalarization of the SETCC, with many
// unnecessary instructions.
SDValue DAGTypeLegalizer::WidenVSELECTMask(SDNode *N) {
  LLVMContext &Ctx = *DAG.getContext();
  SDValue Cond = N->getOperand(0);

  if (N->getOpcode() != ISD::VSELECT)
    return SDValue();

  if (!isSETCCOp(Cond->getOpcode()) && !isLogicalMaskOp(Cond->getOpcode()))
    return SDValue();

  // If this is a splitted VSELECT that was previously already handled, do
  // nothing.
  EVT CondVT = Cond->getValueType(0);
  if (CondVT.getScalarSizeInBits() != 1)
    return SDValue();

  EVT VSelVT = N->getValueType(0);

  // This method can't handle scalable vector types.
  // FIXME: This support could be added in the future.
  if (VSelVT.isScalableVector())
    return SDValue();

  // Only handle vector types which are a power of 2.
  if (!isPowerOf2_64(VSelVT.getSizeInBits()))
    return SDValue();

  // Don't touch if this will be scalarized.
  EVT FinalVT = VSelVT;
  while (getTypeAction(FinalVT) == TargetLowering::TypeSplitVector)
    FinalVT = FinalVT.getHalfNumVectorElementsVT(Ctx);

  if (FinalVT.getVectorNumElements() == 1)
    return SDValue();

  // If there is support for an i1 vector mask, don't touch.
  if (isSETCCOp(Cond.getOpcode())) {
    EVT SetCCOpVT = getSETCCOperandType(Cond);
    while (TLI.getTypeAction(Ctx, SetCCOpVT) != TargetLowering::TypeLegal)
      SetCCOpVT = TLI.getTypeToTransformTo(Ctx, SetCCOpVT);
    EVT SetCCResVT = getSetCCResultType(SetCCOpVT);
    if (SetCCResVT.getScalarSizeInBits() == 1)
      return SDValue();
  } else if (CondVT.getScalarType() == MVT::i1) {
    // If there is support for an i1 vector mask (or only scalar i1 conditions),
    // don't touch.
    while (TLI.getTypeAction(Ctx, CondVT) != TargetLowering::TypeLegal)
      CondVT = TLI.getTypeToTransformTo(Ctx, CondVT);

    if (CondVT.getScalarType() == MVT::i1)
      return SDValue();
  }

  // Widen the vselect result type if needed.
  if (getTypeAction(VSelVT) == TargetLowering::TypeWidenVector)
    VSelVT = TLI.getTypeToTransformTo(Ctx, VSelVT);

  // The mask of the VSELECT should have integer elements.
  EVT ToMaskVT = VSelVT;
  if (!ToMaskVT.getScalarType().isInteger())
    ToMaskVT = ToMaskVT.changeVectorElementTypeToInteger();

  SDValue Mask;
  if (isSETCCOp(Cond->getOpcode())) {
    EVT MaskVT = getSetCCResultType(getSETCCOperandType(Cond));
    Mask = convertMask(Cond, MaskVT, ToMaskVT);
  } else if (isLogicalMaskOp(Cond->getOpcode()) &&
             isSETCCOp(Cond->getOperand(0).getOpcode()) &&
             isSETCCOp(Cond->getOperand(1).getOpcode())) {
    // Cond is (AND/OR/XOR (SETCC, SETCC))
    SDValue SETCC0 = Cond->getOperand(0);
    SDValue SETCC1 = Cond->getOperand(1);
    EVT VT0 = getSetCCResultType(getSETCCOperandType(SETCC0));
    EVT VT1 = getSetCCResultType(getSETCCOperandType(SETCC1));
    unsigned ScalarBits0 = VT0.getScalarSizeInBits();
    unsigned ScalarBits1 = VT1.getScalarSizeInBits();
    unsigned ScalarBits_ToMask = ToMaskVT.getScalarSizeInBits();
    EVT MaskVT;
    // If the two SETCCs have different VTs, either extend/truncate one of
    // them to the other "towards" ToMaskVT, or truncate one and extend the
    // other to ToMaskVT.
    if (ScalarBits0 != ScalarBits1) {
      EVT NarrowVT = ((ScalarBits0 < ScalarBits1) ? VT0 : VT1);
      EVT WideVT = ((NarrowVT == VT0) ? VT1 : VT0);
      if (ScalarBits_ToMask >= WideVT.getScalarSizeInBits())
        MaskVT = WideVT;
      else if (ScalarBits_ToMask <= NarrowVT.getScalarSizeInBits())
        MaskVT = NarrowVT;
      else
        MaskVT = ToMaskVT;
    } else
      // If the two SETCCs have the same VT, don't change it.
      MaskVT = VT0;

    // Make new SETCCs and logical nodes.
    SETCC0 = convertMask(SETCC0, VT0, MaskVT);
    SETCC1 = convertMask(SETCC1, VT1, MaskVT);
    Cond = DAG.getNode(Cond->getOpcode(), SDLoc(Cond), MaskVT, SETCC0, SETCC1);

    // Convert the logical op for VSELECT if needed.
    Mask = convertMask(Cond, MaskVT, ToMaskVT);
  } else
    return SDValue();

  return Mask;
}

SDValue DAGTypeLegalizer::WidenVecRes_Select(SDNode *N) {
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  ElementCount WidenEC = WidenVT.getVectorElementCount();

  SDValue Cond1 = N->getOperand(0);
  EVT CondVT = Cond1.getValueType();
  unsigned Opcode = N->getOpcode();
  if (CondVT.isVector()) {
    if (SDValue WideCond = WidenVSELECTMask(N)) {
      SDValue InOp1 = GetWidenedVector(N->getOperand(1));
      SDValue InOp2 = GetWidenedVector(N->getOperand(2));
      assert(InOp1.getValueType() == WidenVT && InOp2.getValueType() == WidenVT);
      return DAG.getNode(Opcode, SDLoc(N), WidenVT, WideCond, InOp1, InOp2);
    }

    EVT CondEltVT = CondVT.getVectorElementType();
    EVT CondWidenVT = EVT::getVectorVT(*DAG.getContext(), CondEltVT, WidenEC);
    if (getTypeAction(CondVT) == TargetLowering::TypeWidenVector)
      Cond1 = GetWidenedVector(Cond1);

    // If we have to split the condition there is no point in widening the
    // select. This would result in an cycle of widening the select ->
    // widening the condition operand -> splitting the condition operand ->
    // splitting the select -> widening the select. Instead split this select
    // further and widen the resulting type.
    if (getTypeAction(CondVT) == TargetLowering::TypeSplitVector) {
      SDValue SplitSelect = SplitVecOp_VSELECT(N, 0);
      SDValue Res = ModifyToType(SplitSelect, WidenVT);
      return Res;
    }

    if (Cond1.getValueType() != CondWidenVT)
      Cond1 = ModifyToType(Cond1, CondWidenVT);
  }

  SDValue InOp1 = GetWidenedVector(N->getOperand(1));
  SDValue InOp2 = GetWidenedVector(N->getOperand(2));
  assert(InOp1.getValueType() == WidenVT && InOp2.getValueType() == WidenVT);
  if (Opcode == ISD::VP_SELECT || Opcode == ISD::VP_MERGE)
    return DAG.getNode(Opcode, SDLoc(N), WidenVT, Cond1, InOp1, InOp2,
                       N->getOperand(3));
  return DAG.getNode(Opcode, SDLoc(N), WidenVT, Cond1, InOp1, InOp2);
}

SDValue DAGTypeLegalizer::WidenVecRes_SELECT_CC(SDNode *N) {
  SDValue InOp1 = GetWidenedVector(N->getOperand(2));
  SDValue InOp2 = GetWidenedVector(N->getOperand(3));
  return DAG.getNode(ISD::SELECT_CC, SDLoc(N),
                     InOp1.getValueType(), N->getOperand(0),
                     N->getOperand(1), InOp1, InOp2, N->getOperand(4));
}

SDValue DAGTypeLegalizer::WidenVecRes_UNDEF(SDNode *N) {
 EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
 return DAG.getUNDEF(WidenVT);
}

SDValue DAGTypeLegalizer::WidenVecRes_VECTOR_SHUFFLE(ShuffleVectorSDNode *N) {
  EVT VT = N->getValueType(0);
  SDLoc dl(N);

  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  unsigned NumElts = VT.getVectorNumElements();
  unsigned WidenNumElts = WidenVT.getVectorNumElements();

  SDValue InOp1 = GetWidenedVector(N->getOperand(0));
  SDValue InOp2 = GetWidenedVector(N->getOperand(1));

  // Adjust mask based on new input vector length.
  SmallVector<int, 16> NewMask;
  for (unsigned i = 0; i != NumElts; ++i) {
    int Idx = N->getMaskElt(i);
    if (Idx < (int)NumElts)
      NewMask.push_back(Idx);
    else
      NewMask.push_back(Idx - NumElts + WidenNumElts);
  }
  for (unsigned i = NumElts; i != WidenNumElts; ++i)
    NewMask.push_back(-1);
  return DAG.getVectorShuffle(WidenVT, dl, InOp1, InOp2, NewMask);
}

SDValue DAGTypeLegalizer::WidenVecRes_VECTOR_REVERSE(SDNode *N) {
  EVT VT = N->getValueType(0);
  EVT EltVT = VT.getVectorElementType();
  SDLoc dl(N);

  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  SDValue OpValue = GetWidenedVector(N->getOperand(0));
  assert(WidenVT == OpValue.getValueType() && "Unexpected widened vector type");

  SDValue ReverseVal = DAG.getNode(ISD::VECTOR_REVERSE, dl, WidenVT, OpValue);
  unsigned WidenNumElts = WidenVT.getVectorMinNumElements();
  unsigned VTNumElts = VT.getVectorMinNumElements();
  unsigned IdxVal = WidenNumElts - VTNumElts;

  if (VT.isScalableVector()) {
    // Try to split the 'Widen ReverseVal' into smaller extracts and concat the
    // results together, e.g.(nxv6i64 -> nxv8i64)
    //    nxv8i64 vector_reverse
    // <->
    //  nxv8i64 concat(
    //    nxv2i64 extract_subvector(nxv8i64, 2)
    //    nxv2i64 extract_subvector(nxv8i64, 4)
    //    nxv2i64 extract_subvector(nxv8i64, 6)
    //    nxv2i64 undef)

    unsigned GCD = std::gcd(VTNumElts, WidenNumElts);
    EVT PartVT = EVT::getVectorVT(*DAG.getContext(), EltVT,
                                  ElementCount::getScalable(GCD));
    assert((IdxVal % GCD) == 0 && "Expected Idx to be a multiple of the broken "
                                  "down type's element count");
    SmallVector<SDValue> Parts;
    unsigned i = 0;
    for (; i < VTNumElts / GCD; ++i)
      Parts.push_back(
          DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, PartVT, ReverseVal,
                      DAG.getVectorIdxConstant(IdxVal + i * GCD, dl)));
    for (; i < WidenNumElts / GCD; ++i)
      Parts.push_back(DAG.getUNDEF(PartVT));

    return DAG.getNode(ISD::CONCAT_VECTORS, dl, WidenVT, Parts);
  }

  // Use VECTOR_SHUFFLE to combine new vector from 'ReverseVal' for
  // fixed-vectors.
  SmallVector<int, 16> Mask;
  for (unsigned i = 0; i != VTNumElts; ++i) {
    Mask.push_back(IdxVal + i);
  }
  for (unsigned i = VTNumElts; i != WidenNumElts; ++i)
    Mask.push_back(-1);

  return DAG.getVectorShuffle(WidenVT, dl, ReverseVal, DAG.getUNDEF(WidenVT),
                              Mask);
}

SDValue DAGTypeLegalizer::WidenVecRes_SETCC(SDNode *N) {
  assert(N->getValueType(0).isVector() &&
         N->getOperand(0).getValueType().isVector() &&
         "Operands must be vectors");
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), N->getValueType(0));
  ElementCount WidenEC = WidenVT.getVectorElementCount();

  SDValue InOp1 = N->getOperand(0);
  EVT InVT = InOp1.getValueType();
  assert(InVT.isVector() && "can not widen non-vector type");
  EVT WidenInVT =
      EVT::getVectorVT(*DAG.getContext(), InVT.getVectorElementType(), WidenEC);

  // The input and output types often differ here, and it could be that while
  // we'd prefer to widen the result type, the input operands have been split.
  // In this case, we also need to split the result of this node as well.
  if (getTypeAction(InVT) == TargetLowering::TypeSplitVector) {
    SDValue SplitVSetCC = SplitVecOp_VSETCC(N);
    SDValue Res = ModifyToType(SplitVSetCC, WidenVT);
    return Res;
  }

  // If the inputs also widen, handle them directly. Otherwise widen by hand.
  SDValue InOp2 = N->getOperand(1);
  if (getTypeAction(InVT) == TargetLowering::TypeWidenVector) {
    InOp1 = GetWidenedVector(InOp1);
    InOp2 = GetWidenedVector(InOp2);
  } else {
    InOp1 = DAG.WidenVector(InOp1, SDLoc(N));
    InOp2 = DAG.WidenVector(InOp2, SDLoc(N));
  }

  // Assume that the input and output will be widen appropriately.  If not,
  // we will have to unroll it at some point.
  assert(InOp1.getValueType() == WidenInVT &&
         InOp2.getValueType() == WidenInVT &&
         "Input not widened to expected type!");
  (void)WidenInVT;
  if (N->getOpcode() == ISD::VP_SETCC) {
    SDValue Mask =
        GetWidenedMask(N->getOperand(3), WidenVT.getVectorElementCount());
    return DAG.getNode(ISD::VP_SETCC, SDLoc(N), WidenVT, InOp1, InOp2,
                       N->getOperand(2), Mask, N->getOperand(4));
  }
  return DAG.getNode(ISD::SETCC, SDLoc(N), WidenVT, InOp1, InOp2,
                     N->getOperand(2));
}

SDValue DAGTypeLegalizer::WidenVecRes_STRICT_FSETCC(SDNode *N) {
  assert(N->getValueType(0).isVector() &&
         N->getOperand(1).getValueType().isVector() &&
         "Operands must be vectors");
  EVT VT = N->getValueType(0);
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(), VT);
  unsigned WidenNumElts = WidenVT.getVectorNumElements();
  unsigned NumElts = VT.getVectorNumElements();
  EVT EltVT = VT.getVectorElementType();

  SDLoc dl(N);
  SDValue Chain = N->getOperand(0);
  SDValue LHS = N->getOperand(1);
  SDValue RHS = N->getOperand(2);
  SDValue CC = N->getOperand(3);
  EVT TmpEltVT = LHS.getValueType().getVectorElementType();

  // Fully unroll and reassemble.
  SmallVector<SDValue, 8> Scalars(WidenNumElts, DAG.getUNDEF(EltVT));
  SmallVector<SDValue, 8> Chains(NumElts);
  for (unsigned i = 0; i != NumElts; ++i) {
    SDValue LHSElem = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, TmpEltVT, LHS,
                                  DAG.getVectorIdxConstant(i, dl));
    SDValue RHSElem = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, TmpEltVT, RHS,
                                  DAG.getVectorIdxConstant(i, dl));

    Scalars[i] = DAG.getNode(N->getOpcode(), dl, {MVT::i1, MVT::Other},
                             {Chain, LHSElem, RHSElem, CC});
    Chains[i] = Scalars[i].getValue(1);
    Scalars[i] = DAG.getSelect(dl, EltVT, Scalars[i],
                               DAG.getBoolConstant(true, dl, EltVT, VT),
                               DAG.getBoolConstant(false, dl, EltVT, VT));
  }

  SDValue NewChain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Chains);
  ReplaceValueWith(SDValue(N, 1), NewChain);

  return DAG.getBuildVector(WidenVT, dl, Scalars);
}

//===----------------------------------------------------------------------===//
// Widen Vector Operand
//===----------------------------------------------------------------------===//
bool DAGTypeLegalizer::WidenVectorOperand(SDNode *N, unsigned OpNo) {
  LLVM_DEBUG(dbgs() << "Widen node operand " << OpNo << ": "; N->dump(&DAG));
  SDValue Res = SDValue();

  // See if the target wants to custom widen this node.
  if (CustomLowerNode(N, N->getOperand(OpNo).getValueType(), false))
    return false;

  switch (N->getOpcode()) {
  default:
#ifndef NDEBUG
    dbgs() << "WidenVectorOperand op #" << OpNo << ": ";
    N->dump(&DAG);
    dbgs() << "\n";
#endif
    report_fatal_error("Do not know how to widen this operator's operand!");

  case ISD::BITCAST:            Res = WidenVecOp_BITCAST(N); break;
  case ISD::CONCAT_VECTORS:     Res = WidenVecOp_CONCAT_VECTORS(N); break;
  case ISD::INSERT_SUBVECTOR:   Res = WidenVecOp_INSERT_SUBVECTOR(N); break;
  case ISD::EXTRACT_SUBVECTOR:  Res = WidenVecOp_EXTRACT_SUBVECTOR(N); break;
  case ISD::EXTRACT_VECTOR_ELT: Res = WidenVecOp_EXTRACT_VECTOR_ELT(N); break;
  case ISD::STORE:              Res = WidenVecOp_STORE(N); break;
  case ISD::VP_STORE:           Res = WidenVecOp_VP_STORE(N, OpNo); break;
  case ISD::EXPERIMENTAL_VP_STRIDED_STORE:
    Res = WidenVecOp_VP_STRIDED_STORE(N, OpNo);
    break;
  case ISD::ANY_EXTEND_VECTOR_INREG:
  case ISD::SIGN_EXTEND_VECTOR_INREG:
  case ISD::ZERO_EXTEND_VECTOR_INREG:
    Res = WidenVecOp_EXTEND_VECTOR_INREG(N);
    break;
  case ISD::MSTORE:             Res = WidenVecOp_MSTORE(N, OpNo); break;
  case ISD::MGATHER:            Res = WidenVecOp_MGATHER(N, OpNo); break;
  case ISD::MSCATTER:           Res = WidenVecOp_MSCATTER(N, OpNo); break;
  case ISD::VP_SCATTER:         Res = WidenVecOp_VP_SCATTER(N, OpNo); break;
  case ISD::SETCC:              Res = WidenVecOp_SETCC(N); break;
  case ISD::STRICT_FSETCC:
  case ISD::STRICT_FSETCCS:     Res = WidenVecOp_STRICT_FSETCC(N); break;
  case ISD::VSELECT:            Res = WidenVecOp_VSELECT(N); break;
  case ISD::FLDEXP:
  case ISD::FCOPYSIGN:
  case ISD::LRINT:
  case ISD::LLRINT:
    Res = WidenVecOp_UnrollVectorOp(N);
    break;
  case ISD::IS_FPCLASS:         Res = WidenVecOp_IS_FPCLASS(N); break;

  case ISD::ANY_EXTEND:
  case ISD::SIGN_EXTEND:
  case ISD::ZERO_EXTEND:
    Res = WidenVecOp_EXTEND(N);
    break;

  case ISD::SCMP:
  case ISD::UCMP:
    Res = WidenVecOp_CMP(N);
    break;

  case ISD::FP_EXTEND:
  case ISD::STRICT_FP_EXTEND:
  case ISD::FP_ROUND:
  case ISD::STRICT_FP_ROUND:
  case ISD::FP_TO_SINT:
  case ISD::STRICT_FP_TO_SINT:
  case ISD::FP_TO_UINT:
  case ISD::STRICT_FP_TO_UINT:
  case ISD::SINT_TO_FP:
  case ISD::STRICT_SINT_TO_FP:
  case ISD::UINT_TO_FP:
  case ISD::STRICT_UINT_TO_FP:
  case ISD::TRUNCATE:
    Res = WidenVecOp_Convert(N);
    break;

  case ISD::FP_TO_SINT_SAT:
  case ISD::FP_TO_UINT_SAT:
    Res = WidenVecOp_FP_TO_XINT_SAT(N);
    break;

  case ISD::EXPERIMENTAL_VP_SPLAT:
    Res = WidenVecOp_VP_SPLAT(N, OpNo);
    break;

  case ISD::VECREDUCE_FADD:
  case ISD::VECREDUCE_FMUL:
  case ISD::VECREDUCE_ADD:
  case ISD::VECREDUCE_MUL:
  case ISD::VECREDUCE_AND:
  case ISD::VECREDUCE_OR:
  case ISD::VECREDUCE_XOR:
  case ISD::VECREDUCE_SMAX:
  case ISD::VECREDUCE_SMIN:
  case ISD::VECREDUCE_UMAX:
  case ISD::VECREDUCE_UMIN:
  case ISD::VECREDUCE_FMAX:
  case ISD::VECREDUCE_FMIN:
  case ISD::VECREDUCE_FMAXIMUM:
  case ISD::VECREDUCE_FMINIMUM:
    Res = WidenVecOp_VECREDUCE(N);
    break;
  case ISD::VECREDUCE_SEQ_FADD:
  case ISD::VECREDUCE_SEQ_FMUL:
    Res = WidenVecOp_VECREDUCE_SEQ(N);
    break;
  case ISD::VP_REDUCE_FADD:
  case ISD::VP_REDUCE_SEQ_FADD:
  case ISD::VP_REDUCE_FMUL:
  case ISD::VP_REDUCE_SEQ_FMUL:
  case ISD::VP_REDUCE_ADD:
  case ISD::VP_REDUCE_MUL:
  case ISD::VP_REDUCE_AND:
  case ISD::VP_REDUCE_OR:
  case ISD::VP_REDUCE_XOR:
  case ISD::VP_REDUCE_SMAX:
  case ISD::VP_REDUCE_SMIN:
  case ISD::VP_REDUCE_UMAX:
  case ISD::VP_REDUCE_UMIN:
  case ISD::VP_REDUCE_FMAX:
  case ISD::VP_REDUCE_FMIN:
  case ISD::VP_REDUCE_FMAXIMUM:
  case ISD::VP_REDUCE_FMINIMUM:
    Res = WidenVecOp_VP_REDUCE(N);
    break;
  case ISD::VP_CTTZ_ELTS:
  case ISD::VP_CTTZ_ELTS_ZERO_UNDEF:
    Res = WidenVecOp_VP_CttzElements(N);
    break;
  }

  // If Res is null, the sub-method took care of registering the result.
  if (!Res.getNode()) return false;

  // If the result is N, the sub-method updated N in place.  Tell the legalizer
  // core about this.
  if (Res.getNode() == N)
    return true;


  if (N->isStrictFPOpcode())
    assert(Res.getValueType() == N->getValueType(0) && N->getNumValues() == 2 &&
           "Invalid operand expansion");
  else
    assert(Res.getValueType() == N->getValueType(0) && N->getNumValues() == 1 &&
           "Invalid operand expansion");

  ReplaceValueWith(SDValue(N, 0), Res);
  return false;
}

SDValue DAGTypeLegalizer::WidenVecOp_EXTEND(SDNode *N) {
  SDLoc DL(N);
  EVT VT = N->getValueType(0);

  SDValue InOp = N->getOperand(0);
  assert(getTypeAction(InOp.getValueType()) ==
             TargetLowering::TypeWidenVector &&
         "Unexpected type action");
  InOp = GetWidenedVector(InOp);
  assert(VT.getVectorNumElements() <
             InOp.getValueType().getVectorNumElements() &&
         "Input wasn't widened!");

  // We may need to further widen the operand until it has the same total
  // vector size as the result.
  EVT InVT = InOp.getValueType();
  if (InVT.getSizeInBits() != VT.getSizeInBits()) {
    EVT InEltVT = InVT.getVectorElementType();
    for (EVT FixedVT : MVT::vector_valuetypes()) {
      EVT FixedEltVT = FixedVT.getVectorElementType();
      if (TLI.isTypeLegal(FixedVT) &&
          FixedVT.getSizeInBits() == VT.getSizeInBits() &&
          FixedEltVT == InEltVT) {
        assert(FixedVT.getVectorNumElements() >= VT.getVectorNumElements() &&
               "Not enough elements in the fixed type for the operand!");
        assert(FixedVT.getVectorNumElements() != InVT.getVectorNumElements() &&
               "We can't have the same type as we started with!");
        if (FixedVT.getVectorNumElements() > InVT.getVectorNumElements())
          InOp = DAG.getNode(ISD::INSERT_SUBVECTOR, DL, FixedVT,
                             DAG.getUNDEF(FixedVT), InOp,
                             DAG.getVectorIdxConstant(0, DL));
        else
          InOp = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, FixedVT, InOp,
                             DAG.getVectorIdxConstant(0, DL));
        break;
      }
    }
    InVT = InOp.getValueType();
    if (InVT.getSizeInBits() != VT.getSizeInBits())
      // We couldn't find a legal vector type that was a widening of the input
      // and could be extended in-register to the result type, so we have to
      // scalarize.
      return WidenVecOp_Convert(N);
  }

  // Use special DAG nodes to represent the operation of extending the
  // low lanes.
  switch (N->getOpcode()) {
  default:
    llvm_unreachable("Extend legalization on extend operation!");
  case ISD::ANY_EXTEND:
    return DAG.getNode(ISD::ANY_EXTEND_VECTOR_INREG, DL, VT, InOp);
  case ISD::SIGN_EXTEND:
    return DAG.getNode(ISD::SIGN_EXTEND_VECTOR_INREG, DL, VT, InOp);
  case ISD::ZERO_EXTEND:
    return DAG.getNode(ISD::ZERO_EXTEND_VECTOR_INREG, DL, VT, InOp);
  }
}

SDValue DAGTypeLegalizer::WidenVecOp_CMP(SDNode *N) {
  SDLoc dl(N);

  EVT OpVT = N->getOperand(0).getValueType();
  EVT ResVT = N->getValueType(0);
  SDValue LHS = GetWidenedVector(N->getOperand(0));
  SDValue RHS = GetWidenedVector(N->getOperand(1));

  // 1. EXTRACT_SUBVECTOR
  // 2. SIGN_EXTEND/ZERO_EXTEND
  // 3. CMP
  LHS = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, OpVT, LHS,
                    DAG.getVectorIdxConstant(0, dl));
  RHS = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, OpVT, RHS,
                    DAG.getVectorIdxConstant(0, dl));

  // At this point the result type is guaranteed to be valid, so we can use it
  // as the operand type by extending it appropriately
  ISD::NodeType ExtendOpcode =
      N->getOpcode() == ISD::SCMP ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
  LHS = DAG.getNode(ExtendOpcode, dl, ResVT, LHS);
  RHS = DAG.getNode(ExtendOpcode, dl, ResVT, RHS);

  return DAG.getNode(N->getOpcode(), dl, ResVT, LHS, RHS);
}

SDValue DAGTypeLegalizer::WidenVecOp_UnrollVectorOp(SDNode *N) {
  // The result (and first input) is legal, but the second input is illegal.
  // We can't do much to fix that, so just unroll and let the extracts off of
  // the second input be widened as needed later.
  return DAG.UnrollVectorOp(N);
}

SDValue DAGTypeLegalizer::WidenVecOp_IS_FPCLASS(SDNode *N) {
  SDLoc DL(N);
  EVT ResultVT = N->getValueType(0);
  SDValue Test = N->getOperand(1);
  SDValue WideArg = GetWidenedVector(N->getOperand(0));

  // Process this node similarly to SETCC.
  EVT WideResultVT = getSetCCResultType(WideArg.getValueType());
  if (ResultVT.getScalarType() == MVT::i1)
    WideResultVT = EVT::getVectorVT(*DAG.getContext(), MVT::i1,
                                    WideResultVT.getVectorNumElements());

  SDValue WideNode = DAG.getNode(ISD::IS_FPCLASS, DL, WideResultVT,
                                 {WideArg, Test}, N->getFlags());

  // Extract the needed results from the result vector.
  EVT ResVT =
      EVT::getVectorVT(*DAG.getContext(), WideResultVT.getVectorElementType(),
                       ResultVT.getVectorNumElements());
  SDValue CC = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, ResVT, WideNode,
                           DAG.getVectorIdxConstant(0, DL));

  EVT OpVT = N->getOperand(0).getValueType();
  ISD::NodeType ExtendCode =
      TargetLowering::getExtendForContent(TLI.getBooleanContents(OpVT));
  return DAG.getNode(ExtendCode, DL, ResultVT, CC);
}

SDValue DAGTypeLegalizer::WidenVecOp_Convert(SDNode *N) {
  // Since the result is legal and the input is illegal.
  EVT VT = N->getValueType(0);
  EVT EltVT = VT.getVectorElementType();
  SDLoc dl(N);
  SDValue InOp = N->getOperand(N->isStrictFPOpcode() ? 1 : 0);
  assert(getTypeAction(InOp.getValueType()) ==
             TargetLowering::TypeWidenVector &&
         "Unexpected type action");
  InOp = GetWidenedVector(InOp);
  EVT InVT = InOp.getValueType();
  unsigned Opcode = N->getOpcode();

  // See if a widened result type would be legal, if so widen the node.
  // FIXME: This isn't safe for StrictFP. Other optimization here is needed.
  EVT WideVT = EVT::getVectorVT(*DAG.getContext(), EltVT,
                                InVT.getVectorElementCount());
  if (TLI.isTypeLegal(WideVT) && !N->isStrictFPOpcode()) {
    SDValue Res;
    if (N->isStrictFPOpcode()) {
      if (Opcode == ISD::STRICT_FP_ROUND)
        Res = DAG.getNode(Opcode, dl, { WideVT, MVT::Other },
                          { N->getOperand(0), InOp, N->getOperand(2) });
      else
        Res = DAG.getNode(Opcode, dl, { WideVT, MVT::Other },
                          { N->getOperand(0), InOp });
      // Legalize the chain result - switch anything that used the old chain to
      // use the new one.
      ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
    } else {
      if (Opcode == ISD::FP_ROUND)
        Res = DAG.getNode(Opcode, dl, WideVT, InOp, N->getOperand(1));
      else
        Res = DAG.getNode(Opcode, dl, WideVT, InOp);
    }
    return DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, VT, Res,
                       DAG.getVectorIdxConstant(0, dl));
  }

  EVT InEltVT = InVT.getVectorElementType();

  // Unroll the convert into some scalar code and create a nasty build vector.
  unsigned NumElts = VT.getVectorNumElements();
  SmallVector<SDValue, 16> Ops(NumElts);
  if (N->isStrictFPOpcode()) {
    SmallVector<SDValue, 4> NewOps(N->op_begin(), N->op_end());
    SmallVector<SDValue, 32> OpChains;
    for (unsigned i=0; i < NumElts; ++i) {
      NewOps[1] = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, InEltVT, InOp,
                              DAG.getVectorIdxConstant(i, dl));
      Ops[i] = DAG.getNode(Opcode, dl, { EltVT, MVT::Other }, NewOps);
      OpChains.push_back(Ops[i].getValue(1));
    }
    SDValue NewChain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, OpChains);
    ReplaceValueWith(SDValue(N, 1), NewChain);
  } else {
    for (unsigned i = 0; i < NumElts; ++i)
      Ops[i] = DAG.getNode(Opcode, dl, EltVT,
                           DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, InEltVT,
                                       InOp, DAG.getVectorIdxConstant(i, dl)));
  }

  return DAG.getBuildVector(VT, dl, Ops);
}

SDValue DAGTypeLegalizer::WidenVecOp_FP_TO_XINT_SAT(SDNode *N) {
  EVT DstVT = N->getValueType(0);
  SDValue Src = GetWidenedVector(N->getOperand(0));
  EVT SrcVT = Src.getValueType();
  ElementCount WideNumElts = SrcVT.getVectorElementCount();
  SDLoc dl(N);

  // See if a widened result type would be legal, if so widen the node.
  EVT WideDstVT = EVT::getVectorVT(*DAG.getContext(),
                                   DstVT.getVectorElementType(), WideNumElts);
  if (TLI.isTypeLegal(WideDstVT)) {
    SDValue Res =
        DAG.getNode(N->getOpcode(), dl, WideDstVT, Src, N->getOperand(1));
    return DAG.getNode(
        ISD::EXTRACT_SUBVECTOR, dl, DstVT, Res,
        DAG.getConstant(0, dl, TLI.getVectorIdxTy(DAG.getDataLayout())));
  }

  // Give up and unroll.
  return DAG.UnrollVectorOp(N);
}

SDValue DAGTypeLegalizer::WidenVecOp_BITCAST(SDNode *N) {
  EVT VT = N->getValueType(0);
  SDValue InOp = GetWidenedVector(N->getOperand(0));
  EVT InWidenVT = InOp.getValueType();
  SDLoc dl(N);

  // Check if we can convert between two legal vector types and extract.
  TypeSize InWidenSize = InWidenVT.getSizeInBits();
  TypeSize Size = VT.getSizeInBits();
  // x86mmx is not an acceptable vector element type, so don't try.
  if (!VT.isVector() && VT != MVT::x86mmx &&
      InWidenSize.hasKnownScalarFactor(Size)) {
    unsigned NewNumElts = InWidenSize.getKnownScalarFactor(Size);
    EVT NewVT = EVT::getVectorVT(*DAG.getContext(), VT, NewNumElts);
    if (TLI.isTypeLegal(NewVT)) {
      SDValue BitOp = DAG.getNode(ISD::BITCAST, dl, NewVT, InOp);
      return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, VT, BitOp,
                         DAG.getVectorIdxConstant(0, dl));
    }
  }

  // Handle a case like bitcast v12i8 -> v3i32. Normally that would get widened
  // to v16i8 -> v4i32, but for a target where v3i32 is legal but v12i8 is not,
  // we end up here. Handling the case here with EXTRACT_SUBVECTOR avoids
  // having to copy via memory.
  if (VT.isVector()) {
    EVT EltVT = VT.getVectorElementType();
    unsigned EltSize = EltVT.getFixedSizeInBits();
    if (InWidenSize.isKnownMultipleOf(EltSize)) {
      ElementCount NewNumElts =
          (InWidenVT.getVectorElementCount() * InWidenVT.getScalarSizeInBits())
              .divideCoefficientBy(EltSize);
      EVT NewVT = EVT::getVectorVT(*DAG.getContext(), EltVT, NewNumElts);
      if (TLI.isTypeLegal(NewVT)) {
        SDValue BitOp = DAG.getNode(ISD::BITCAST, dl, NewVT, InOp);
        return DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, VT, BitOp,
                           DAG.getVectorIdxConstant(0, dl));
      }
    }
  }

  return CreateStackStoreLoad(InOp, VT);
}

SDValue DAGTypeLegalizer::WidenVecOp_CONCAT_VECTORS(SDNode *N) {
  EVT VT = N->getValueType(0);
  EVT EltVT = VT.getVectorElementType();
  EVT InVT = N->getOperand(0).getValueType();
  SDLoc dl(N);

  // If the widen width for this operand is the same as the width of the concat
  // and all but the first operand is undef, just use the widened operand.
  unsigned NumOperands = N->getNumOperands();
  if (VT == TLI.getTypeToTransformTo(*DAG.getContext(), InVT)) {
    unsigned i;
    for (i = 1; i < NumOperands; ++i)
      if (!N->getOperand(i).isUndef())
        break;

    if (i == NumOperands)
      return GetWidenedVector(N->getOperand(0));
  }

  // Otherwise, fall back to a nasty build vector.
  unsigned NumElts = VT.getVectorNumElements();
  SmallVector<SDValue, 16> Ops(NumElts);

  unsigned NumInElts = InVT.getVectorNumElements();

  unsigned Idx = 0;
  for (unsigned i=0; i < NumOperands; ++i) {
    SDValue InOp = N->getOperand(i);
    assert(getTypeAction(InOp.getValueType()) ==
               TargetLowering::TypeWidenVector &&
           "Unexpected type action");
    InOp = GetWidenedVector(InOp);
    for (unsigned j = 0; j < NumInElts; ++j)
      Ops[Idx++] = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, InOp,
                               DAG.getVectorIdxConstant(j, dl));
  }
  return DAG.getBuildVector(VT, dl, Ops);
}

SDValue DAGTypeLegalizer::WidenVecOp_INSERT_SUBVECTOR(SDNode *N) {
  EVT VT = N->getValueType(0);
  SDValue SubVec = N->getOperand(1);
  SDValue InVec = N->getOperand(0);

  if (getTypeAction(SubVec.getValueType()) == TargetLowering::TypeWidenVector)
    SubVec = GetWidenedVector(SubVec);

  EVT SubVT = SubVec.getValueType();

  // Whether or not all the elements of the widened SubVec will be inserted into
  // valid indices of VT.
  bool IndicesValid = false;
  // If we statically know that VT can fit SubVT, the indices are valid.
  if (VT.knownBitsGE(SubVT))
    IndicesValid = true;
  else if (VT.isScalableVector() && SubVT.isFixedLengthVector()) {
    // Otherwise, if we're inserting a fixed vector into a scalable vector and
    // we know the minimum vscale we can work out if it's valid ourselves.
    Attribute Attr = DAG.getMachineFunction().getFunction().getFnAttribute(
        Attribute::VScaleRange);
    if (Attr.isValid()) {
      unsigned VScaleMin = Attr.getVScaleRangeMin();
      if (VT.getSizeInBits().getKnownMinValue() * VScaleMin >=
          SubVT.getFixedSizeInBits())
        IndicesValid = true;
    }
  }

  // We need to make sure that the indices are still valid, otherwise we might
  // widen what was previously well-defined to something undefined.
  if (IndicesValid && InVec.isUndef() && N->getConstantOperandVal(2) == 0)
    return DAG.getNode(ISD::INSERT_SUBVECTOR, SDLoc(N), VT, InVec, SubVec,
                       N->getOperand(2));

  report_fatal_error("Don't know how to widen the operands for "
                     "INSERT_SUBVECTOR");
}

SDValue DAGTypeLegalizer::WidenVecOp_EXTRACT_SUBVECTOR(SDNode *N) {
  SDValue InOp = GetWidenedVector(N->getOperand(0));
  return DAG.getNode(ISD::EXTRACT_SUBVECTOR, SDLoc(N),
                     N->getValueType(0), InOp, N->getOperand(1));
}

SDValue DAGTypeLegalizer::WidenVecOp_EXTRACT_VECTOR_ELT(SDNode *N) {
  SDValue InOp = GetWidenedVector(N->getOperand(0));
  return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SDLoc(N),
                     N->getValueType(0), InOp, N->getOperand(1));
}

SDValue DAGTypeLegalizer::WidenVecOp_EXTEND_VECTOR_INREG(SDNode *N) {
  SDValue InOp = GetWidenedVector(N->getOperand(0));
  return DAG.getNode(N->getOpcode(), SDLoc(N), N->getValueType(0), InOp);
}

SDValue DAGTypeLegalizer::WidenVecOp_STORE(SDNode *N) {
  // We have to widen the value, but we want only to store the original
  // vector type.
  StoreSDNode *ST = cast<StoreSDNode>(N);

  if (!ST->getMemoryVT().getScalarType().isByteSized())
    return TLI.scalarizeVectorStore(ST, DAG);

  if (ST->isTruncatingStore())
    return TLI.scalarizeVectorStore(ST, DAG);

  // Generate a vector-predicated store if it is custom/legal on the target.
  // To avoid possible recursion, only do this if the widened mask type is
  // legal.
  // FIXME: Not all targets may support EVL in VP_STORE. These will have been
  // removed from the IR by the ExpandVectorPredication pass but we're
  // reintroducing them here.
  SDValue StVal = ST->getValue();
  EVT StVT = StVal.getValueType();
  EVT WideVT = TLI.getTypeToTransformTo(*DAG.getContext(), StVT);
  EVT WideMaskVT = EVT::getVectorVT(*DAG.getContext(), MVT::i1,
                                    WideVT.getVectorElementCount());

  if (TLI.isOperationLegalOrCustom(ISD::VP_STORE, WideVT) &&
      TLI.isTypeLegal(WideMaskVT)) {
    // Widen the value.
    SDLoc DL(N);
    StVal = GetWidenedVector(StVal);
    SDValue Mask = DAG.getAllOnesConstant(DL, WideMaskVT);
    SDValue EVL = DAG.getElementCount(DL, TLI.getVPExplicitVectorLengthTy(),
                                      StVT.getVectorElementCount());
    return DAG.getStoreVP(ST->getChain(), DL, StVal, ST->getBasePtr(),
                          DAG.getUNDEF(ST->getBasePtr().getValueType()), Mask,
                          EVL, StVT, ST->getMemOperand(),
                          ST->getAddressingMode());
  }

  SmallVector<SDValue, 16> StChain;
  if (GenWidenVectorStores(StChain, ST)) {
    if (StChain.size() == 1)
      return StChain[0];

    return DAG.getNode(ISD::TokenFactor, SDLoc(ST), MVT::Other, StChain);
  }

  report_fatal_error("Unable to widen vector store");
}

SDValue DAGTypeLegalizer::WidenVecOp_VP_SPLAT(SDNode *N, unsigned OpNo) {
  assert(OpNo == 1 && "Can widen only mask operand of vp_splat");
  return DAG.getNode(N->getOpcode(), SDLoc(N), N->getValueType(0),
                     N->getOperand(0), GetWidenedVector(N->getOperand(1)),
                     N->getOperand(2));
}

SDValue DAGTypeLegalizer::WidenVecOp_VP_STORE(SDNode *N, unsigned OpNo) {
  assert((OpNo == 1 || OpNo == 3) &&
         "Can widen only data or mask operand of vp_store");
  VPStoreSDNode *ST = cast<VPStoreSDNode>(N);
  SDValue Mask = ST->getMask();
  SDValue StVal = ST->getValue();
  SDLoc dl(N);

  if (OpNo == 1) {
    // Widen the value.
    StVal = GetWidenedVector(StVal);

    // We only handle the case where the mask needs widening to an
    // identically-sized type as the vector inputs.
    assert(getTypeAction(Mask.getValueType()) ==
               TargetLowering::TypeWidenVector &&
           "Unable to widen VP store");
    Mask = GetWidenedVector(Mask);
  } else {
    Mask = GetWidenedVector(Mask);

    // We only handle the case where the stored value needs widening to an
    // identically-sized type as the mask.
    assert(getTypeAction(StVal.getValueType()) ==
               TargetLowering::TypeWidenVector &&
           "Unable to widen VP store");
    StVal = GetWidenedVector(StVal);
  }

  assert(Mask.getValueType().getVectorElementCount() ==
             StVal.getValueType().getVectorElementCount() &&
         "Mask and data vectors should have the same number of elements");
  return DAG.getStoreVP(ST->getChain(), dl, StVal, ST->getBasePtr(),
                        ST->getOffset(), Mask, ST->getVectorLength(),
                        ST->getMemoryVT(), ST->getMemOperand(),
                        ST->getAddressingMode(), ST->isTruncatingStore(),
                        ST->isCompressingStore());
}

SDValue DAGTypeLegalizer::WidenVecOp_VP_STRIDED_STORE(SDNode *N,
                                                      unsigned OpNo) {
  assert((OpNo == 1 || OpNo == 4) &&
         "Can widen only data or mask operand of vp_strided_store");
  VPStridedStoreSDNode *SST = cast<VPStridedStoreSDNode>(N);
  SDValue Mask = SST->getMask();
  SDValue StVal = SST->getValue();
  SDLoc DL(N);

  if (OpNo == 1)
    assert(getTypeAction(Mask.getValueType()) ==
               TargetLowering::TypeWidenVector &&
           "Unable to widen VP strided store");
  else
    assert(getTypeAction(StVal.getValueType()) ==
               TargetLowering::TypeWidenVector &&
           "Unable to widen VP strided store");

  StVal = GetWidenedVector(StVal);
  Mask = GetWidenedVector(Mask);

  assert(StVal.getValueType().getVectorElementCount() ==
             Mask.getValueType().getVectorElementCount() &&
         "Data and mask vectors should have the same number of elements");

  return DAG.getStridedStoreVP(
      SST->getChain(), DL, StVal, SST->getBasePtr(), SST->getOffset(),
      SST->getStride(), Mask, SST->getVectorLength(), SST->getMemoryVT(),
      SST->getMemOperand(), SST->getAddressingMode(), SST->isTruncatingStore(),
      SST->isCompressingStore());
}

SDValue DAGTypeLegalizer::WidenVecOp_MSTORE(SDNode *N, unsigned OpNo) {
  assert((OpNo == 1 || OpNo == 4) &&
         "Can widen only data or mask operand of mstore");
  MaskedStoreSDNode *MST = cast<MaskedStoreSDNode>(N);
  SDValue Mask = MST->getMask();
  EVT MaskVT = Mask.getValueType();
  SDValue StVal = MST->getValue();
  SDLoc dl(N);

  if (OpNo == 1) {
    // Widen the value.
    StVal = GetWidenedVector(StVal);

    // The mask should be widened as well.
    EVT WideVT = StVal.getValueType();
    EVT WideMaskVT = EVT::getVectorVT(*DAG.getContext(),
                                      MaskVT.getVectorElementType(),
                                      WideVT.getVectorNumElements());
    Mask = ModifyToType(Mask, WideMaskVT, true);
  } else {
    // Widen the mask.
    EVT WideMaskVT = TLI.getTypeToTransformTo(*DAG.getContext(), MaskVT);
    Mask = ModifyToType(Mask, WideMaskVT, true);

    EVT ValueVT = StVal.getValueType();
    EVT WideVT = EVT::getVectorVT(*DAG.getContext(),
                                  ValueVT.getVectorElementType(),
                                  WideMaskVT.getVectorNumElements());
    StVal = ModifyToType(StVal, WideVT);
  }

  assert(Mask.getValueType().getVectorNumElements() ==
         StVal.getValueType().getVectorNumElements() &&
         "Mask and data vectors should have the same number of elements");
  return DAG.getMaskedStore(MST->getChain(), dl, StVal, MST->getBasePtr(),
                            MST->getOffset(), Mask, MST->getMemoryVT(),
                            MST->getMemOperand(), MST->getAddressingMode(),
                            false, MST->isCompressingStore());
}

SDValue DAGTypeLegalizer::WidenVecOp_MGATHER(SDNode *N, unsigned OpNo) {
  assert(OpNo == 4 && "Can widen only the index of mgather");
  auto *MG = cast<MaskedGatherSDNode>(N);
  SDValue DataOp = MG->getPassThru();
  SDValue Mask = MG->getMask();
  SDValue Scale = MG->getScale();

  // Just widen the index. It's allowed to have extra elements.
  SDValue Index = GetWidenedVector(MG->getIndex());

  SDLoc dl(N);
  SDValue Ops[] = {MG->getChain(), DataOp, Mask, MG->getBasePtr(), Index,
                   Scale};
  SDValue Res = DAG.getMaskedGather(MG->getVTList(), MG->getMemoryVT(), dl, Ops,
                                    MG->getMemOperand(), MG->getIndexType(),
                                    MG->getExtensionType());
  ReplaceValueWith(SDValue(N, 1), Res.getValue(1));
  ReplaceValueWith(SDValue(N, 0), Res.getValue(0));
  return SDValue();
}

SDValue DAGTypeLegalizer::WidenVecOp_MSCATTER(SDNode *N, unsigned OpNo) {
  MaskedScatterSDNode *MSC = cast<MaskedScatterSDNode>(N);
  SDValue DataOp = MSC->getValue();
  SDValue Mask = MSC->getMask();
  SDValue Index = MSC->getIndex();
  SDValue Scale = MSC->getScale();
  EVT WideMemVT = MSC->getMemoryVT();

  if (OpNo == 1) {
    DataOp = GetWidenedVector(DataOp);
    unsigned NumElts = DataOp.getValueType().getVectorNumElements();

    // Widen index.
    EVT IndexVT = Index.getValueType();
    EVT WideIndexVT = EVT::getVectorVT(*DAG.getContext(),
                                       IndexVT.getVectorElementType(), NumElts);
    Index = ModifyToType(Index, WideIndexVT);

    // The mask should be widened as well.
    EVT MaskVT = Mask.getValueType();
    EVT WideMaskVT = EVT::getVectorVT(*DAG.getContext(),
                                      MaskVT.getVectorElementType(), NumElts);
    Mask = ModifyToType(Mask, WideMaskVT, true);

    // Widen the MemoryType
    WideMemVT = EVT::getVectorVT(*DAG.getContext(),
                                 MSC->getMemoryVT().getScalarType(), NumElts);
  } else if (OpNo == 4) {
    // Just widen the index. It's allowed to have extra elements.
    Index = GetWidenedVector(Index);
  } else
    llvm_unreachable("Can't widen this operand of mscatter");

  SDValue Ops[] = {MSC->getChain(), DataOp, Mask, MSC->getBasePtr(), Index,
                   Scale};
  return DAG.getMaskedScatter(DAG.getVTList(MVT::Other), WideMemVT, SDLoc(N),
                              Ops, MSC->getMemOperand(), MSC->getIndexType(),
                              MSC->isTruncatingStore());
}

SDValue DAGTypeLegalizer::WidenVecOp_VP_SCATTER(SDNode *N, unsigned OpNo) {
  VPScatterSDNode *VPSC = cast<VPScatterSDNode>(N);
  SDValue DataOp = VPSC->getValue();
  SDValue Mask = VPSC->getMask();
  SDValue Index = VPSC->getIndex();
  SDValue Scale = VPSC->getScale();
  EVT WideMemVT = VPSC->getMemoryVT();

  if (OpNo == 1) {
    DataOp = GetWidenedVector(DataOp);
    Index = GetWidenedVector(Index);
    const auto WideEC = DataOp.getValueType().getVectorElementCount();
    Mask = GetWidenedMask(Mask, WideEC);
    WideMemVT = EVT::getVectorVT(*DAG.getContext(),
                                 VPSC->getMemoryVT().getScalarType(), WideEC);
  } else if (OpNo == 3) {
    // Just widen the index. It's allowed to have extra elements.
    Index = GetWidenedVector(Index);
  } else
    llvm_unreachable("Can't widen this operand of VP_SCATTER");

  SDValue Ops[] = {
      VPSC->getChain(),       DataOp, VPSC->getBasePtr(), Index, Scale, Mask,
      VPSC->getVectorLength()};
  return DAG.getScatterVP(DAG.getVTList(MVT::Other), WideMemVT, SDLoc(N), Ops,
                          VPSC->getMemOperand(), VPSC->getIndexType());
}

SDValue DAGTypeLegalizer::WidenVecOp_SETCC(SDNode *N) {
  SDValue InOp0 = GetWidenedVector(N->getOperand(0));
  SDValue InOp1 = GetWidenedVector(N->getOperand(1));
  SDLoc dl(N);
  EVT VT = N->getValueType(0);

  // WARNING: In this code we widen the compare instruction with garbage.
  // This garbage may contain denormal floats which may be slow. Is this a real
  // concern ? Should we zero the unused lanes if this is a float compare ?

  // Get a new SETCC node to compare the newly widened operands.
  // Only some of the compared elements are legal.
  EVT SVT = getSetCCResultType(InOp0.getValueType());
  // The result type is legal, if its vXi1, keep vXi1 for the new SETCC.
  if (VT.getScalarType() == MVT::i1)
    SVT = EVT::getVectorVT(*DAG.getContext(), MVT::i1,
                           SVT.getVectorElementCount());

  SDValue WideSETCC = DAG.getNode(ISD::SETCC, SDLoc(N),
                                  SVT, InOp0, InOp1, N->getOperand(2));

  // Extract the needed results from the result vector.
  EVT ResVT = EVT::getVectorVT(*DAG.getContext(),
                               SVT.getVectorElementType(),
                               VT.getVectorElementCount());
  SDValue CC = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, ResVT, WideSETCC,
                           DAG.getVectorIdxConstant(0, dl));

  EVT OpVT = N->getOperand(0).getValueType();
  ISD::NodeType ExtendCode =
      TargetLowering::getExtendForContent(TLI.getBooleanContents(OpVT));
  return DAG.getNode(ExtendCode, dl, VT, CC);
}

SDValue DAGTypeLegalizer::WidenVecOp_STRICT_FSETCC(SDNode *N) {
  SDValue Chain = N->getOperand(0);
  SDValue LHS = GetWidenedVector(N->getOperand(1));
  SDValue RHS = GetWidenedVector(N->getOperand(2));
  SDValue CC = N->getOperand(3);
  SDLoc dl(N);

  EVT VT = N->getValueType(0);
  EVT EltVT = VT.getVectorElementType();
  EVT TmpEltVT = LHS.getValueType().getVectorElementType();
  unsigned NumElts = VT.getVectorNumElements();

  // Unroll into a build vector.
  SmallVector<SDValue, 8> Scalars(NumElts);
  SmallVector<SDValue, 8> Chains(NumElts);

  for (unsigned i = 0; i != NumElts; ++i) {
    SDValue LHSElem = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, TmpEltVT, LHS,
                                  DAG.getVectorIdxConstant(i, dl));
    SDValue RHSElem = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, TmpEltVT, RHS,
                                  DAG.getVectorIdxConstant(i, dl));

    Scalars[i] = DAG.getNode(N->getOpcode(), dl, {MVT::i1, MVT::Other},
                             {Chain, LHSElem, RHSElem, CC});
    Chains[i] = Scalars[i].getValue(1);
    Scalars[i] = DAG.getSelect(dl, EltVT, Scalars[i],
                               DAG.getBoolConstant(true, dl, EltVT, VT),
                               DAG.getBoolConstant(false, dl, EltVT, VT));
  }

  SDValue NewChain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Chains);
  ReplaceValueWith(SDValue(N, 1), NewChain);

  return DAG.getBuildVector(VT, dl, Scalars);
}

SDValue DAGTypeLegalizer::WidenVecOp_VECREDUCE(SDNode *N) {
  SDLoc dl(N);
  SDValue Op = GetWidenedVector(N->getOperand(0));
  EVT OrigVT = N->getOperand(0).getValueType();
  EVT WideVT = Op.getValueType();
  EVT ElemVT = OrigVT.getVectorElementType();
  SDNodeFlags Flags = N->getFlags();

  unsigned Opc = N->getOpcode();
  unsigned BaseOpc = ISD::getVecReduceBaseOpcode(Opc);
  SDValue NeutralElem = DAG.getNeutralElement(BaseOpc, dl, ElemVT, Flags);
  assert(NeutralElem && "Neutral element must exist");

  // Pad the vector with the neutral element.
  unsigned OrigElts = OrigVT.getVectorMinNumElements();
  unsigned WideElts = WideVT.getVectorMinNumElements();

  if (WideVT.isScalableVector()) {
    unsigned GCD = std::gcd(OrigElts, WideElts);
    EVT SplatVT = EVT::getVectorVT(*DAG.getContext(), ElemVT,
                                   ElementCount::getScalable(GCD));
    SDValue SplatNeutral = DAG.getSplatVector(SplatVT, dl, NeutralElem);
    for (unsigned Idx = OrigElts; Idx < WideElts; Idx = Idx + GCD)
      Op = DAG.getNode(ISD::INSERT_SUBVECTOR, dl, WideVT, Op, SplatNeutral,
                       DAG.getVectorIdxConstant(Idx, dl));
    return DAG.getNode(Opc, dl, N->getValueType(0), Op, Flags);
  }

  for (unsigned Idx = OrigElts; Idx < WideElts; Idx++)
    Op = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, WideVT, Op, NeutralElem,
                     DAG.getVectorIdxConstant(Idx, dl));

  return DAG.getNode(Opc, dl, N->getValueType(0), Op, Flags);
}

SDValue DAGTypeLegalizer::WidenVecOp_VECREDUCE_SEQ(SDNode *N) {
  SDLoc dl(N);
  SDValue AccOp = N->getOperand(0);
  SDValue VecOp = N->getOperand(1);
  SDValue Op = GetWidenedVector(VecOp);

  EVT OrigVT = VecOp.getValueType();
  EVT WideVT = Op.getValueType();
  EVT ElemVT = OrigVT.getVectorElementType();
  SDNodeFlags Flags = N->getFlags();

  unsigned Opc = N->getOpcode();
  unsigned BaseOpc = ISD::getVecReduceBaseOpcode(Opc);
  SDValue NeutralElem = DAG.getNeutralElement(BaseOpc, dl, ElemVT, Flags);

  // Pad the vector with the neutral element.
  unsigned OrigElts = OrigVT.getVectorMinNumElements();
  unsigned WideElts = WideVT.getVectorMinNumElements();

  if (WideVT.isScalableVector()) {
    unsigned GCD = std::gcd(OrigElts, WideElts);
    EVT SplatVT = EVT::getVectorVT(*DAG.getContext(), ElemVT,
                                   ElementCount::getScalable(GCD));
    SDValue SplatNeutral = DAG.getSplatVector(SplatVT, dl, NeutralElem);
    for (unsigned Idx = OrigElts; Idx < WideElts; Idx = Idx + GCD)
      Op = DAG.getNode(ISD::INSERT_SUBVECTOR, dl, WideVT, Op, SplatNeutral,
                       DAG.getVectorIdxConstant(Idx, dl));
    return DAG.getNode(Opc, dl, N->getValueType(0), AccOp, Op, Flags);
  }

  for (unsigned Idx = OrigElts; Idx < WideElts; Idx++)
    Op = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, WideVT, Op, NeutralElem,
                     DAG.getVectorIdxConstant(Idx, dl));

  return DAG.getNode(Opc, dl, N->getValueType(0), AccOp, Op, Flags);
}

SDValue DAGTypeLegalizer::WidenVecOp_VP_REDUCE(SDNode *N) {
  assert(N->isVPOpcode() && "Expected VP opcode");

  SDLoc dl(N);
  SDValue Op = GetWidenedVector(N->getOperand(1));
  SDValue Mask = GetWidenedMask(N->getOperand(2),
                                Op.getValueType().getVectorElementCount());

  return DAG.getNode(N->getOpcode(), dl, N->getValueType(0),
                     {N->getOperand(0), Op, Mask, N->getOperand(3)},
                     N->getFlags());
}

SDValue DAGTypeLegalizer::WidenVecOp_VSELECT(SDNode *N) {
  // This only gets called in the case that the left and right inputs and
  // result are of a legal odd vector type, and the condition is illegal i1 of
  // the same odd width that needs widening.
  EVT VT = N->getValueType(0);
  assert(VT.isVector() && !VT.isPow2VectorType() && isTypeLegal(VT));

  SDValue Cond = GetWidenedVector(N->getOperand(0));
  SDValue LeftIn = DAG.WidenVector(N->getOperand(1), SDLoc(N));
  SDValue RightIn = DAG.WidenVector(N->getOperand(2), SDLoc(N));
  SDLoc DL(N);

  SDValue Select = DAG.getNode(N->getOpcode(), DL, LeftIn.getValueType(), Cond,
                               LeftIn, RightIn);
  return DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, VT, Select,
                     DAG.getVectorIdxConstant(0, DL));
}

SDValue DAGTypeLegalizer::WidenVecOp_VP_CttzElements(SDNode *N) {
  SDLoc DL(N);
  SDValue Source = GetWidenedVector(N->getOperand(0));
  EVT SrcVT = Source.getValueType();
  SDValue Mask =
      GetWidenedMask(N->getOperand(1), SrcVT.getVectorElementCount());

  return DAG.getNode(N->getOpcode(), DL, N->getValueType(0),
                     {Source, Mask, N->getOperand(2)}, N->getFlags());
}

//===----------------------------------------------------------------------===//
// Vector Widening Utilities
//===----------------------------------------------------------------------===//

// Utility function to find the type to chop up a widen vector for load/store
//  TLI:       Target lowering used to determine legal types.
//  Width:     Width left need to load/store.
//  WidenVT:   The widen vector type to load to/store from
//  Align:     If 0, don't allow use of a wider type
//  WidenEx:   If Align is not 0, the amount additional we can load/store from.

static std::optional<EVT> findMemType(SelectionDAG &DAG,
                                      const TargetLowering &TLI, unsigned Width,
                                      EVT WidenVT, unsigned Align = 0,
                                      unsigned WidenEx = 0) {
  EVT WidenEltVT = WidenVT.getVectorElementType();
  const bool Scalable = WidenVT.isScalableVector();
  unsigned WidenWidth = WidenVT.getSizeInBits().getKnownMinValue();
  unsigned WidenEltWidth = WidenEltVT.getSizeInBits();
  unsigned AlignInBits = Align*8;

  // If we have one element to load/store, return it.
  EVT RetVT = WidenEltVT;
  if (!Scalable && Width == WidenEltWidth)
    return RetVT;

  // Don't bother looking for an integer type if the vector is scalable, skip
  // to vector types.
  if (!Scalable) {
    // See if there is larger legal integer than the element type to load/store.
    for (EVT MemVT : reverse(MVT::integer_valuetypes())) {
      unsigned MemVTWidth = MemVT.getSizeInBits();
      if (MemVT.getSizeInBits() <= WidenEltWidth)
        break;
      auto Action = TLI.getTypeAction(*DAG.getContext(), MemVT);
      if ((Action == TargetLowering::TypeLegal ||
           Action == TargetLowering::TypePromoteInteger) &&
          (WidenWidth % MemVTWidth) == 0 &&
          isPowerOf2_32(WidenWidth / MemVTWidth) &&
          (MemVTWidth <= Width ||
           (Align!=0 && MemVTWidth<=AlignInBits && MemVTWidth<=Width+WidenEx))) {
        if (MemVTWidth == WidenWidth)
          return MemVT;
        RetVT = MemVT;
        break;
      }
    }
  }

  // See if there is a larger vector type to load/store that has the same vector
  // element type and is evenly divisible with the WidenVT.
  for (EVT MemVT : reverse(MVT::vector_valuetypes())) {
    // Skip vector MVTs which don't match the scalable property of WidenVT.
    if (Scalable != MemVT.isScalableVector())
      continue;
    unsigned MemVTWidth = MemVT.getSizeInBits().getKnownMinValue();
    auto Action = TLI.getTypeAction(*DAG.getContext(), MemVT);
    if ((Action == TargetLowering::TypeLegal ||
         Action == TargetLowering::TypePromoteInteger) &&
        WidenEltVT == MemVT.getVectorElementType() &&
        (WidenWidth % MemVTWidth) == 0 &&
        isPowerOf2_32(WidenWidth / MemVTWidth) &&
        (MemVTWidth <= Width ||
         (Align!=0 && MemVTWidth<=AlignInBits && MemVTWidth<=Width+WidenEx))) {
      if (RetVT.getFixedSizeInBits() < MemVTWidth || MemVT == WidenVT)
        return MemVT;
    }
  }

  // Using element-wise loads and stores for widening operations is not
  // supported for scalable vectors
  if (Scalable)
    return std::nullopt;

  return RetVT;
}

// Builds a vector type from scalar loads
//  VecTy: Resulting Vector type
//  LDOps: Load operators to build a vector type
//  [Start,End) the list of loads to use.
static SDValue BuildVectorFromScalar(SelectionDAG& DAG, EVT VecTy,
                                     SmallVectorImpl<SDValue> &LdOps,
                                     unsigned Start, unsigned End) {
  SDLoc dl(LdOps[Start]);
  EVT LdTy = LdOps[Start].getValueType();
  unsigned Width = VecTy.getSizeInBits();
  unsigned NumElts = Width / LdTy.getSizeInBits();
  EVT NewVecVT = EVT::getVectorVT(*DAG.getContext(), LdTy, NumElts);

  unsigned Idx = 1;
  SDValue VecOp = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, NewVecVT,LdOps[Start]);

  for (unsigned i = Start + 1; i != End; ++i) {
    EVT NewLdTy = LdOps[i].getValueType();
    if (NewLdTy != LdTy) {
      NumElts = Width / NewLdTy.getSizeInBits();
      NewVecVT = EVT::getVectorVT(*DAG.getContext(), NewLdTy, NumElts);
      VecOp = DAG.getNode(ISD::BITCAST, dl, NewVecVT, VecOp);
      // Readjust position and vector position based on new load type.
      Idx = Idx * LdTy.getSizeInBits() / NewLdTy.getSizeInBits();
      LdTy = NewLdTy;
    }
    VecOp = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, NewVecVT, VecOp, LdOps[i],
                        DAG.getVectorIdxConstant(Idx++, dl));
  }
  return DAG.getNode(ISD::BITCAST, dl, VecTy, VecOp);
}

SDValue DAGTypeLegalizer::GenWidenVectorLoads(SmallVectorImpl<SDValue> &LdChain,
                                              LoadSDNode *LD) {
  // The strategy assumes that we can efficiently load power-of-two widths.
  // The routine chops the vector into the largest vector loads with the same
  // element type or scalar loads and then recombines it to the widen vector
  // type.
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(),LD->getValueType(0));
  EVT LdVT    = LD->getMemoryVT();
  SDLoc dl(LD);
  assert(LdVT.isVector() && WidenVT.isVector());
  assert(LdVT.isScalableVector() == WidenVT.isScalableVector());
  assert(LdVT.getVectorElementType() == WidenVT.getVectorElementType());

  // Load information
  SDValue Chain = LD->getChain();
  SDValue BasePtr = LD->getBasePtr();
  MachineMemOperand::Flags MMOFlags = LD->getMemOperand()->getFlags();
  AAMDNodes AAInfo = LD->getAAInfo();

  TypeSize LdWidth = LdVT.getSizeInBits();
  TypeSize WidenWidth = WidenVT.getSizeInBits();
  TypeSize WidthDiff = WidenWidth - LdWidth;
  // Allow wider loads if they are sufficiently aligned to avoid memory faults
  // and if the original load is simple.
  unsigned LdAlign =
      (!LD->isSimple() || LdVT.isScalableVector()) ? 0 : LD->getAlign().value();

  // Find the vector type that can load from.
  std::optional<EVT> FirstVT =
      findMemType(DAG, TLI, LdWidth.getKnownMinValue(), WidenVT, LdAlign,
                  WidthDiff.getKnownMinValue());

  if (!FirstVT)
    return SDValue();

  SmallVector<EVT, 8> MemVTs;
  TypeSize FirstVTWidth = FirstVT->getSizeInBits();

  // Unless we're able to load in one instruction we must work out how to load
  // the remainder.
  if (!TypeSize::isKnownLE(LdWidth, FirstVTWidth)) {
    std::optional<EVT> NewVT = FirstVT;
    TypeSize RemainingWidth = LdWidth;
    TypeSize NewVTWidth = FirstVTWidth;
    do {
      RemainingWidth -= NewVTWidth;
      if (TypeSize::isKnownLT(RemainingWidth, NewVTWidth)) {
        // The current type we are using is too large. Find a better size.
        NewVT = findMemType(DAG, TLI, RemainingWidth.getKnownMinValue(),
                            WidenVT, LdAlign, WidthDiff.getKnownMinValue());
        if (!NewVT)
          return SDValue();
        NewVTWidth = NewVT->getSizeInBits();
      }
      MemVTs.push_back(*NewVT);
    } while (TypeSize::isKnownGT(RemainingWidth, NewVTWidth));
  }

  SDValue LdOp = DAG.getLoad(*FirstVT, dl, Chain, BasePtr, LD->getPointerInfo(),
                             LD->getOriginalAlign(), MMOFlags, AAInfo);
  LdChain.push_back(LdOp.getValue(1));

  // Check if we can load the element with one instruction.
  if (MemVTs.empty()) {
    assert(TypeSize::isKnownLE(LdWidth, FirstVTWidth));
    if (!FirstVT->isVector()) {
      unsigned NumElts =
          WidenWidth.getFixedValue() / FirstVTWidth.getFixedValue();
      EVT NewVecVT = EVT::getVectorVT(*DAG.getContext(), *FirstVT, NumElts);
      SDValue VecOp = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, NewVecVT, LdOp);
      return DAG.getNode(ISD::BITCAST, dl, WidenVT, VecOp);
    }
    if (FirstVT == WidenVT)
      return LdOp;

    // TODO: We don't currently have any tests that exercise this code path.
    assert(WidenWidth.getFixedValue() % FirstVTWidth.getFixedValue() == 0);
    unsigned NumConcat =
        WidenWidth.getFixedValue() / FirstVTWidth.getFixedValue();
    SmallVector<SDValue, 16> ConcatOps(NumConcat);
    SDValue UndefVal = DAG.getUNDEF(*FirstVT);
    ConcatOps[0] = LdOp;
    for (unsigned i = 1; i != NumConcat; ++i)
      ConcatOps[i] = UndefVal;
    return DAG.getNode(ISD::CONCAT_VECTORS, dl, WidenVT, ConcatOps);
  }

  // Load vector by using multiple loads from largest vector to scalar.
  SmallVector<SDValue, 16> LdOps;
  LdOps.push_back(LdOp);

  uint64_t ScaledOffset = 0;
  MachinePointerInfo MPI = LD->getPointerInfo();

  // First incremement past the first load.
  IncrementPointer(cast<LoadSDNode>(LdOp), *FirstVT, MPI, BasePtr,
                   &ScaledOffset);

  for (EVT MemVT : MemVTs) {
    Align NewAlign = ScaledOffset == 0
                         ? LD->getOriginalAlign()
                         : commonAlignment(LD->getAlign(), ScaledOffset);
    SDValue L =
        DAG.getLoad(MemVT, dl, Chain, BasePtr, MPI, NewAlign, MMOFlags, AAInfo);

    LdOps.push_back(L);
    LdChain.push_back(L.getValue(1));
    IncrementPointer(cast<LoadSDNode>(L), MemVT, MPI, BasePtr, &ScaledOffset);
  }

  // Build the vector from the load operations.
  unsigned End = LdOps.size();
  if (!LdOps[0].getValueType().isVector())
    // All the loads are scalar loads.
    return BuildVectorFromScalar(DAG, WidenVT, LdOps, 0, End);

  // If the load contains vectors, build the vector using concat vector.
  // All of the vectors used to load are power-of-2, and the scalar loads can be
  // combined to make a power-of-2 vector.
  SmallVector<SDValue, 16> ConcatOps(End);
  int i = End - 1;
  int Idx = End;
  EVT LdTy = LdOps[i].getValueType();
  // First, combine the scalar loads to a vector.
  if (!LdTy.isVector())  {
    for (--i; i >= 0; --i) {
      LdTy = LdOps[i].getValueType();
      if (LdTy.isVector())
        break;
    }
    ConcatOps[--Idx] = BuildVectorFromScalar(DAG, LdTy, LdOps, i + 1, End);
  }

  ConcatOps[--Idx] = LdOps[i];
  for (--i; i >= 0; --i) {
    EVT NewLdTy = LdOps[i].getValueType();
    if (NewLdTy != LdTy) {
      // Create a larger vector.
      TypeSize LdTySize = LdTy.getSizeInBits();
      TypeSize NewLdTySize = NewLdTy.getSizeInBits();
      assert(NewLdTySize.isScalable() == LdTySize.isScalable() &&
             NewLdTySize.isKnownMultipleOf(LdTySize.getKnownMinValue()));
      unsigned NumOps =
          NewLdTySize.getKnownMinValue() / LdTySize.getKnownMinValue();
      SmallVector<SDValue, 16> WidenOps(NumOps);
      unsigned j = 0;
      for (; j != End-Idx; ++j)
        WidenOps[j] = ConcatOps[Idx+j];
      for (; j != NumOps; ++j)
        WidenOps[j] = DAG.getUNDEF(LdTy);

      ConcatOps[End-1] = DAG.getNode(ISD::CONCAT_VECTORS, dl, NewLdTy,
                                     WidenOps);
      Idx = End - 1;
      LdTy = NewLdTy;
    }
    ConcatOps[--Idx] = LdOps[i];
  }

  if (WidenWidth == LdTy.getSizeInBits() * (End - Idx))
    return DAG.getNode(ISD::CONCAT_VECTORS, dl, WidenVT,
                       ArrayRef(&ConcatOps[Idx], End - Idx));

  // We need to fill the rest with undefs to build the vector.
  unsigned NumOps =
      WidenWidth.getKnownMinValue() / LdTy.getSizeInBits().getKnownMinValue();
  SmallVector<SDValue, 16> WidenOps(NumOps);
  SDValue UndefVal = DAG.getUNDEF(LdTy);
  {
    unsigned i = 0;
    for (; i != End-Idx; ++i)
      WidenOps[i] = ConcatOps[Idx+i];
    for (; i != NumOps; ++i)
      WidenOps[i] = UndefVal;
  }
  return DAG.getNode(ISD::CONCAT_VECTORS, dl, WidenVT, WidenOps);
}

SDValue
DAGTypeLegalizer::GenWidenVectorExtLoads(SmallVectorImpl<SDValue> &LdChain,
                                         LoadSDNode *LD,
                                         ISD::LoadExtType ExtType) {
  // For extension loads, it may not be more efficient to chop up the vector
  // and then extend it. Instead, we unroll the load and build a new vector.
  EVT WidenVT = TLI.getTypeToTransformTo(*DAG.getContext(),LD->getValueType(0));
  EVT LdVT    = LD->getMemoryVT();
  SDLoc dl(LD);
  assert(LdVT.isVector() && WidenVT.isVector());
  assert(LdVT.isScalableVector() == WidenVT.isScalableVector());

  // Load information
  SDValue Chain = LD->getChain();
  SDValue BasePtr = LD->getBasePtr();
  MachineMemOperand::Flags MMOFlags = LD->getMemOperand()->getFlags();
  AAMDNodes AAInfo = LD->getAAInfo();

  if (LdVT.isScalableVector())
    report_fatal_error("Generating widen scalable extending vector loads is "
                       "not yet supported");

  EVT EltVT = WidenVT.getVectorElementType();
  EVT LdEltVT = LdVT.getVectorElementType();
  unsigned NumElts = LdVT.getVectorNumElements();

  // Load each element and widen.
  unsigned WidenNumElts = WidenVT.getVectorNumElements();
  SmallVector<SDValue, 16> Ops(WidenNumElts);
  unsigned Increment = LdEltVT.getSizeInBits() / 8;
  Ops[0] =
      DAG.getExtLoad(ExtType, dl, EltVT, Chain, BasePtr, LD->getPointerInfo(),
                     LdEltVT, LD->getOriginalAlign(), MMOFlags, AAInfo);
  LdChain.push_back(Ops[0].getValue(1));
  unsigned i = 0, Offset = Increment;
  for (i=1; i < NumElts; ++i, Offset += Increment) {
    SDValue NewBasePtr =
        DAG.getObjectPtrOffset(dl, BasePtr, TypeSize::getFixed(Offset));
    Ops[i] = DAG.getExtLoad(ExtType, dl, EltVT, Chain, NewBasePtr,
                            LD->getPointerInfo().getWithOffset(Offset), LdEltVT,
                            LD->getOriginalAlign(), MMOFlags, AAInfo);
    LdChain.push_back(Ops[i].getValue(1));
  }

  // Fill the rest with undefs.
  SDValue UndefVal = DAG.getUNDEF(EltVT);
  for (; i != WidenNumElts; ++i)
    Ops[i] = UndefVal;

  return DAG.getBuildVector(WidenVT, dl, Ops);
}

bool DAGTypeLegalizer::GenWidenVectorStores(SmallVectorImpl<SDValue> &StChain,
                                            StoreSDNode *ST) {
  // The strategy assumes that we can efficiently store power-of-two widths.
  // The routine chops the vector into the largest vector stores with the same
  // element type or scalar stores.
  SDValue  Chain = ST->getChain();
  SDValue  BasePtr = ST->getBasePtr();
  MachineMemOperand::Flags MMOFlags = ST->getMemOperand()->getFlags();
  AAMDNodes AAInfo = ST->getAAInfo();
  SDValue  ValOp = GetWidenedVector(ST->getValue());
  SDLoc dl(ST);

  EVT StVT = ST->getMemoryVT();
  TypeSize StWidth = StVT.getSizeInBits();
  EVT ValVT = ValOp.getValueType();
  TypeSize ValWidth = ValVT.getSizeInBits();
  EVT ValEltVT = ValVT.getVectorElementType();
  unsigned ValEltWidth = ValEltVT.getFixedSizeInBits();
  assert(StVT.getVectorElementType() == ValEltVT);
  assert(StVT.isScalableVector() == ValVT.isScalableVector() &&
         "Mismatch between store and value types");

  int Idx = 0;          // current index to store

  MachinePointerInfo MPI = ST->getPointerInfo();
  uint64_t ScaledOffset = 0;

  // A breakdown of how to widen this vector store. Each element of the vector
  // is a memory VT combined with the number of times it is to be stored to,
  // e,g., v5i32 -> {{v2i32,2},{i32,1}}
  SmallVector<std::pair<EVT, unsigned>, 4> MemVTs;

  while (StWidth.isNonZero()) {
    // Find the largest vector type we can store with.
    std::optional<EVT> NewVT =
        findMemType(DAG, TLI, StWidth.getKnownMinValue(), ValVT);
    if (!NewVT)
      return false;
    MemVTs.push_back({*NewVT, 0});
    TypeSize NewVTWidth = NewVT->getSizeInBits();

    do {
      StWidth -= NewVTWidth;
      MemVTs.back().second++;
    } while (StWidth.isNonZero() && TypeSize::isKnownGE(StWidth, NewVTWidth));
  }

  for (const auto &Pair : MemVTs) {
    EVT NewVT = Pair.first;
    unsigned Count = Pair.second;
    TypeSize NewVTWidth = NewVT.getSizeInBits();

    if (NewVT.isVector()) {
      unsigned NumVTElts = NewVT.getVectorMinNumElements();
      do {
        Align NewAlign = ScaledOffset == 0
                             ? ST->getOriginalAlign()
                             : commonAlignment(ST->getAlign(), ScaledOffset);
        SDValue EOp = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, NewVT, ValOp,
                                  DAG.getVectorIdxConstant(Idx, dl));
        SDValue PartStore = DAG.getStore(Chain, dl, EOp, BasePtr, MPI, NewAlign,
                                         MMOFlags, AAInfo);
        StChain.push_back(PartStore);

        Idx += NumVTElts;
        IncrementPointer(cast<StoreSDNode>(PartStore), NewVT, MPI, BasePtr,
                         &ScaledOffset);
      } while (--Count);
    } else {
      // Cast the vector to the scalar type we can store.
      unsigned NumElts = ValWidth.getFixedValue() / NewVTWidth.getFixedValue();
      EVT NewVecVT = EVT::getVectorVT(*DAG.getContext(), NewVT, NumElts);
      SDValue VecOp = DAG.getNode(ISD::BITCAST, dl, NewVecVT, ValOp);
      // Readjust index position based on new vector type.
      Idx = Idx * ValEltWidth / NewVTWidth.getFixedValue();
      do {
        SDValue EOp = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, NewVT, VecOp,
                                  DAG.getVectorIdxConstant(Idx++, dl));
        SDValue PartStore =
            DAG.getStore(Chain, dl, EOp, BasePtr, MPI, ST->getOriginalAlign(),
                         MMOFlags, AAInfo);
        StChain.push_back(PartStore);

        IncrementPointer(cast<StoreSDNode>(PartStore), NewVT, MPI, BasePtr);
      } while (--Count);
      // Restore index back to be relative to the original widen element type.
      Idx = Idx * NewVTWidth.getFixedValue() / ValEltWidth;
    }
  }

  return true;
}

/// Modifies a vector input (widen or narrows) to a vector of NVT.  The
/// input vector must have the same element type as NVT.
/// FillWithZeroes specifies that the vector should be widened with zeroes.
SDValue DAGTypeLegalizer::ModifyToType(SDValue InOp, EVT NVT,
                                       bool FillWithZeroes) {
  // Note that InOp might have been widened so it might already have
  // the right width or it might need be narrowed.
  EVT InVT = InOp.getValueType();
  assert(InVT.getVectorElementType() == NVT.getVectorElementType() &&
         "input and widen element type must match");
  assert(InVT.isScalableVector() == NVT.isScalableVector() &&
         "cannot modify scalable vectors in this way");
  SDLoc dl(InOp);

  // Check if InOp already has the right width.
  if (InVT == NVT)
    return InOp;

  ElementCount InEC = InVT.getVectorElementCount();
  ElementCount WidenEC = NVT.getVectorElementCount();
  if (WidenEC.hasKnownScalarFactor(InEC)) {
    unsigned NumConcat = WidenEC.getKnownScalarFactor(InEC);
    SmallVector<SDValue, 16> Ops(NumConcat);
    SDValue FillVal = FillWithZeroes ? DAG.getConstant(0, dl, InVT) :
      DAG.getUNDEF(InVT);
    Ops[0] = InOp;
    for (unsigned i = 1; i != NumConcat; ++i)
      Ops[i] = FillVal;

    return DAG.getNode(ISD::CONCAT_VECTORS, dl, NVT, Ops);
  }

  if (InEC.hasKnownScalarFactor(WidenEC))
    return DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, NVT, InOp,
                       DAG.getVectorIdxConstant(0, dl));

  assert(!InVT.isScalableVector() && !NVT.isScalableVector() &&
         "Scalable vectors should have been handled already.");

  unsigned InNumElts = InEC.getFixedValue();
  unsigned WidenNumElts = WidenEC.getFixedValue();

  // Fall back to extract and build (+ mask, if padding with zeros).
  SmallVector<SDValue, 16> Ops(WidenNumElts);
  EVT EltVT = NVT.getVectorElementType();
  unsigned MinNumElts = std::min(WidenNumElts, InNumElts);
  unsigned Idx;
  for (Idx = 0; Idx < MinNumElts; ++Idx)
    Ops[Idx] = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, InOp,
                           DAG.getVectorIdxConstant(Idx, dl));

  SDValue UndefVal = DAG.getUNDEF(EltVT);
  for (; Idx < WidenNumElts; ++Idx)
    Ops[Idx] = UndefVal;

  SDValue Widened = DAG.getBuildVector(NVT, dl, Ops);
  if (!FillWithZeroes)
    return Widened;

  assert(NVT.isInteger() &&
         "We expect to never want to FillWithZeroes for non-integral types.");

  SmallVector<SDValue, 16> MaskOps;
  MaskOps.append(MinNumElts, DAG.getAllOnesConstant(dl, EltVT));
  MaskOps.append(WidenNumElts - MinNumElts, DAG.getConstant(0, dl, EltVT));

  return DAG.getNode(ISD::AND, dl, NVT, Widened,
                     DAG.getBuildVector(NVT, dl, MaskOps));
}
