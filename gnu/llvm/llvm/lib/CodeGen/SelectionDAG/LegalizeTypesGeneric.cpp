//===-------- LegalizeTypesGeneric.cpp - Generic type legalization --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements generic type expansion and splitting for LegalizeTypes.
// The routines here perform legalization when the details of the type (such as
// whether it is an integer or a float) do not matter.
// Expansion is the act of changing a computation in an illegal type to be a
// computation in two identical registers of a smaller type.  The Lo/Hi part
// is required to be stored first in memory on little/big-endian machines.
// Splitting is the act of changing a computation in an illegal type to be a
// computation in two not necessarily identical registers of a smaller type.
// There are no requirements on how the type is represented in memory.
//
//===----------------------------------------------------------------------===//

#include "LegalizeTypes.h"
#include "llvm/IR/DataLayout.h"
using namespace llvm;

#define DEBUG_TYPE "legalize-types"

//===----------------------------------------------------------------------===//
// Generic Result Expansion.
//===----------------------------------------------------------------------===//

// These routines assume that the Lo/Hi part is stored first in memory on
// little/big-endian machines, followed by the Hi/Lo part.  This means that
// they cannot be used as is on vectors, for which Lo is always stored first.
void DAGTypeLegalizer::ExpandRes_MERGE_VALUES(SDNode *N, unsigned ResNo,
                                              SDValue &Lo, SDValue &Hi) {
  SDValue Op = DisintegrateMERGE_VALUES(N, ResNo);
  GetExpandedOp(Op, Lo, Hi);
}

void DAGTypeLegalizer::ExpandRes_BITCAST(SDNode *N, SDValue &Lo, SDValue &Hi) {
  EVT OutVT = N->getValueType(0);
  EVT NOutVT = TLI.getTypeToTransformTo(*DAG.getContext(), OutVT);
  SDValue InOp = N->getOperand(0);
  EVT InVT = InOp.getValueType();
  SDLoc dl(N);

  // Handle some special cases efficiently.
  switch (getTypeAction(InVT)) {
    case TargetLowering::TypeLegal:
    case TargetLowering::TypePromoteInteger:
      break;
    case TargetLowering::TypePromoteFloat:
    case TargetLowering::TypeSoftPromoteHalf:
      llvm_unreachable("Bitcast of a promotion-needing float should never need"
                       "expansion");
    case TargetLowering::TypeSoftenFloat:
      SplitInteger(GetSoftenedFloat(InOp), Lo, Hi);
      Lo = DAG.getNode(ISD::BITCAST, dl, NOutVT, Lo);
      Hi = DAG.getNode(ISD::BITCAST, dl, NOutVT, Hi);
      return;
    case TargetLowering::TypeExpandInteger:
    case TargetLowering::TypeExpandFloat: {
      auto &DL = DAG.getDataLayout();
      // Convert the expanded pieces of the input.
      GetExpandedOp(InOp, Lo, Hi);
      if (TLI.hasBigEndianPartOrdering(InVT, DL) !=
          TLI.hasBigEndianPartOrdering(OutVT, DL))
        std::swap(Lo, Hi);
      Lo = DAG.getNode(ISD::BITCAST, dl, NOutVT, Lo);
      Hi = DAG.getNode(ISD::BITCAST, dl, NOutVT, Hi);
      return;
    }
    case TargetLowering::TypeSplitVector:
      GetSplitVector(InOp, Lo, Hi);
      if (TLI.hasBigEndianPartOrdering(OutVT, DAG.getDataLayout()))
        std::swap(Lo, Hi);
      Lo = DAG.getNode(ISD::BITCAST, dl, NOutVT, Lo);
      Hi = DAG.getNode(ISD::BITCAST, dl, NOutVT, Hi);
      return;
    case TargetLowering::TypeScalarizeVector:
      // Convert the element instead.
      SplitInteger(BitConvertToInteger(GetScalarizedVector(InOp)), Lo, Hi);
      Lo = DAG.getNode(ISD::BITCAST, dl, NOutVT, Lo);
      Hi = DAG.getNode(ISD::BITCAST, dl, NOutVT, Hi);
      return;
    case TargetLowering::TypeScalarizeScalableVector:
      report_fatal_error("Scalarization of scalable vectors is not supported.");
    case TargetLowering::TypeWidenVector: {
      assert(!(InVT.getVectorNumElements() & 1) && "Unsupported BITCAST");
      InOp = GetWidenedVector(InOp);
      EVT LoVT, HiVT;
      std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(InVT);
      std::tie(Lo, Hi) = DAG.SplitVector(InOp, dl, LoVT, HiVT);
      if (TLI.hasBigEndianPartOrdering(OutVT, DAG.getDataLayout()))
        std::swap(Lo, Hi);
      Lo = DAG.getNode(ISD::BITCAST, dl, NOutVT, Lo);
      Hi = DAG.getNode(ISD::BITCAST, dl, NOutVT, Hi);
      return;
    }
  }

  if (InVT.isVector() && OutVT.isInteger()) {
    // Handle cases like i64 = BITCAST v1i64 on x86, where the operand
    // is legal but the result is not.
    unsigned NumElems = 2;
    EVT ElemVT = NOutVT;
    EVT NVT = EVT::getVectorVT(*DAG.getContext(), ElemVT, NumElems);

    // If <ElemVT * N> is not a legal type, try <ElemVT/2 * (N*2)>.
    while (!isTypeLegal(NVT)) {
      unsigned NewSizeInBits = ElemVT.getSizeInBits() / 2;
      // If the element size is smaller than byte, bail.
      if (NewSizeInBits < 8)
        break;
      NumElems *= 2;
      ElemVT = EVT::getIntegerVT(*DAG.getContext(), NewSizeInBits);
      NVT = EVT::getVectorVT(*DAG.getContext(), ElemVT, NumElems);
    }

    if (isTypeLegal(NVT)) {
      SDValue CastInOp = DAG.getNode(ISD::BITCAST, dl, NVT, InOp);

      SmallVector<SDValue, 8> Vals;
      for (unsigned i = 0; i < NumElems; ++i)
        Vals.push_back(DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, ElemVT,
                                   CastInOp, DAG.getVectorIdxConstant(i, dl)));

      // Build Lo, Hi pair by pairing extracted elements if needed.
      unsigned Slot = 0;
      for (unsigned e = Vals.size(); e - Slot > 2; Slot += 2, e += 1) {
        // Each iteration will BUILD_PAIR two nodes and append the result until
        // there are only two nodes left, i.e. Lo and Hi.
        SDValue LHS = Vals[Slot];
        SDValue RHS = Vals[Slot + 1];

        if (DAG.getDataLayout().isBigEndian())
          std::swap(LHS, RHS);

        Vals.push_back(DAG.getNode(
            ISD::BUILD_PAIR, dl,
            EVT::getIntegerVT(*DAG.getContext(), LHS.getValueSizeInBits() << 1),
            LHS, RHS));
      }
      Lo = Vals[Slot++];
      Hi = Vals[Slot++];

      if (DAG.getDataLayout().isBigEndian())
        std::swap(Lo, Hi);

      return;
    }
  }

  // Lower the bit-convert to a store/load from the stack.
  assert(NOutVT.isByteSized() && "Expanded type not byte sized!");

  // Create the stack frame object.  Make sure it is aligned for both
  // the source and expanded destination types.

  // In cases where the vector is illegal it will be broken down into parts
  // and stored in parts - we should use the alignment for the smallest part.
  Align InAlign = DAG.getReducedAlign(InVT, /*UseABI=*/false);
  Align NOutAlign = DAG.getReducedAlign(NOutVT, /*UseABI=*/false);
  Align Align = std::max(InAlign, NOutAlign);
  SDValue StackPtr = DAG.CreateStackTemporary(InVT.getStoreSize(), Align);
  int SPFI = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();
  MachinePointerInfo PtrInfo =
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), SPFI);

  // Emit a store to the stack slot.
  SDValue Store = DAG.getStore(DAG.getEntryNode(), dl, InOp, StackPtr, PtrInfo);

  // Load the first half from the stack slot.
  Lo = DAG.getLoad(NOutVT, dl, Store, StackPtr, PtrInfo, NOutAlign);

  // Increment the pointer to the other half.
  unsigned IncrementSize = NOutVT.getSizeInBits() / 8;
  StackPtr =
      DAG.getMemBasePlusOffset(StackPtr, TypeSize::getFixed(IncrementSize), dl);

  // Load the second half from the stack slot.
  Hi = DAG.getLoad(NOutVT, dl, Store, StackPtr,
                   PtrInfo.getWithOffset(IncrementSize), NOutAlign);

  // Handle endianness of the load.
  if (TLI.hasBigEndianPartOrdering(OutVT, DAG.getDataLayout()))
    std::swap(Lo, Hi);
}

void DAGTypeLegalizer::ExpandRes_BUILD_PAIR(SDNode *N, SDValue &Lo,
                                            SDValue &Hi) {
  // Return the operands.
  Lo = N->getOperand(0);
  Hi = N->getOperand(1);
}

void DAGTypeLegalizer::ExpandRes_EXTRACT_ELEMENT(SDNode *N, SDValue &Lo,
                                                 SDValue &Hi) {
  GetExpandedOp(N->getOperand(0), Lo, Hi);
  SDValue Part = N->getConstantOperandVal(1) ? Hi : Lo;

  assert(Part.getValueType() == N->getValueType(0) &&
         "Type twice as big as expanded type not itself expanded!");

  GetPairElements(Part, Lo, Hi);
}

void DAGTypeLegalizer::ExpandRes_EXTRACT_VECTOR_ELT(SDNode *N, SDValue &Lo,
                                                    SDValue &Hi) {
  SDValue OldVec = N->getOperand(0);
  ElementCount OldEltCount = OldVec.getValueType().getVectorElementCount();
  EVT OldEltVT = OldVec.getValueType().getVectorElementType();
  SDLoc dl(N);

  // Convert to a vector of the expanded element type, for example
  // <3 x i64> -> <6 x i32>.
  EVT OldVT = N->getValueType(0);
  EVT NewVT = TLI.getTypeToTransformTo(*DAG.getContext(), OldVT);

  if (OldVT != OldEltVT) {
    // The result of EXTRACT_VECTOR_ELT may be larger than the element type of
    // the input vector.  If so, extend the elements of the input vector to the
    // same bitwidth as the result before expanding.
    assert(OldEltVT.bitsLT(OldVT) && "Result type smaller then element type!");
    EVT NVecVT = EVT::getVectorVT(*DAG.getContext(), OldVT, OldEltCount);
    OldVec = DAG.getNode(ISD::ANY_EXTEND, dl, NVecVT, N->getOperand(0));
  }

  SDValue NewVec = DAG.getNode(
      ISD::BITCAST, dl,
      EVT::getVectorVT(*DAG.getContext(), NewVT, OldEltCount * 2), OldVec);

  // Extract the elements at 2 * Idx and 2 * Idx + 1 from the new vector.
  SDValue Idx = N->getOperand(1);

  Idx = DAG.getNode(ISD::ADD, dl, Idx.getValueType(), Idx, Idx);
  Lo = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, NewVT, NewVec, Idx);

  Idx = DAG.getNode(ISD::ADD, dl, Idx.getValueType(), Idx,
                    DAG.getConstant(1, dl, Idx.getValueType()));
  Hi = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, NewVT, NewVec, Idx);

  if (DAG.getDataLayout().isBigEndian())
    std::swap(Lo, Hi);
}

void DAGTypeLegalizer::ExpandRes_NormalLoad(SDNode *N, SDValue &Lo,
                                            SDValue &Hi) {
  assert(ISD::isNormalLoad(N) && "This routine only for normal loads!");
  SDLoc dl(N);

  LoadSDNode *LD = cast<LoadSDNode>(N);
  assert(!LD->isAtomic() && "Atomics can not be split");
  EVT ValueVT = LD->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), ValueVT);
  SDValue Chain = LD->getChain();
  SDValue Ptr = LD->getBasePtr();
  AAMDNodes AAInfo = LD->getAAInfo();

  assert(NVT.isByteSized() && "Expanded type not byte sized!");

  Lo = DAG.getLoad(NVT, dl, Chain, Ptr, LD->getPointerInfo(),
                   LD->getOriginalAlign(), LD->getMemOperand()->getFlags(),
                   AAInfo);

  // Increment the pointer to the other half.
  unsigned IncrementSize = NVT.getSizeInBits() / 8;
  Ptr = DAG.getMemBasePlusOffset(Ptr, TypeSize::getFixed(IncrementSize), dl);
  Hi = DAG.getLoad(
      NVT, dl, Chain, Ptr, LD->getPointerInfo().getWithOffset(IncrementSize),
      LD->getOriginalAlign(), LD->getMemOperand()->getFlags(), AAInfo);

  // Build a factor node to remember that this load is independent of the
  // other one.
  Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo.getValue(1),
                      Hi.getValue(1));

  // Handle endianness of the load.
  if (TLI.hasBigEndianPartOrdering(ValueVT, DAG.getDataLayout()))
    std::swap(Lo, Hi);

  // Modified the chain - switch anything that used the old chain to use
  // the new one.
  ReplaceValueWith(SDValue(N, 1), Chain);
}

void DAGTypeLegalizer::ExpandRes_VAARG(SDNode *N, SDValue &Lo, SDValue &Hi) {
  EVT OVT = N->getValueType(0);
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), OVT);
  SDValue Chain = N->getOperand(0);
  SDValue Ptr = N->getOperand(1);
  SDLoc dl(N);
  const unsigned Align = N->getConstantOperandVal(3);

  Lo = DAG.getVAArg(NVT, dl, Chain, Ptr, N->getOperand(2), Align);
  Hi = DAG.getVAArg(NVT, dl, Lo.getValue(1), Ptr, N->getOperand(2), 0);
  Chain = Hi.getValue(1);

  // Handle endianness of the load.
  if (TLI.hasBigEndianPartOrdering(OVT, DAG.getDataLayout()))
    std::swap(Lo, Hi);

  // Modified the chain - switch anything that used the old chain to use
  // the new one.
  ReplaceValueWith(SDValue(N, 1), Chain);
}


//===--------------------------------------------------------------------===//
// Generic Operand Expansion.
//===--------------------------------------------------------------------===//

void DAGTypeLegalizer::IntegerToVector(SDValue Op, unsigned NumElements,
                                       SmallVectorImpl<SDValue> &Ops,
                                       EVT EltVT) {
  assert(Op.getValueType().isInteger());
  SDLoc DL(Op);
  SDValue Parts[2];

  if (NumElements > 1) {
    NumElements >>= 1;
    SplitInteger(Op, Parts[0], Parts[1]);
    if (DAG.getDataLayout().isBigEndian())
      std::swap(Parts[0], Parts[1]);
    IntegerToVector(Parts[0], NumElements, Ops, EltVT);
    IntegerToVector(Parts[1], NumElements, Ops, EltVT);
  } else {
    Ops.push_back(DAG.getNode(ISD::BITCAST, DL, EltVT, Op));
  }
}

SDValue DAGTypeLegalizer::ExpandOp_BITCAST(SDNode *N) {
  SDLoc dl(N);
  if (N->getValueType(0).isVector() &&
      N->getOperand(0).getValueType().isInteger()) {
    // An illegal expanding type is being converted to a legal vector type.
    // Make a two element vector out of the expanded parts and convert that
    // instead, but only if the new vector type is legal (otherwise there
    // is no point, and it might create expansion loops).  For example, on
    // x86 this turns v1i64 = BITCAST i64 into v1i64 = BITCAST v2i32.
    //
    // FIXME: I'm not sure why we are first trying to split the input into
    // a 2 element vector, so I'm leaving it here to maintain the current
    // behavior.
    unsigned NumElts = 2;
    EVT OVT = N->getOperand(0).getValueType();
    EVT NVT = EVT::getVectorVT(*DAG.getContext(),
                               TLI.getTypeToTransformTo(*DAG.getContext(), OVT),
                               NumElts);
    if (!isTypeLegal(NVT)) {
      // If we can't find a legal type by splitting the integer in half,
      // then we can use the node's value type.
      NumElts = N->getValueType(0).getVectorNumElements();
      NVT = N->getValueType(0);
    }

    SmallVector<SDValue, 8> Ops;
    IntegerToVector(N->getOperand(0), NumElts, Ops, NVT.getVectorElementType());

    SDValue Vec = DAG.getBuildVector(NVT, dl, ArrayRef(Ops.data(), NumElts));
    return DAG.getNode(ISD::BITCAST, dl, N->getValueType(0), Vec);
  }

  // Otherwise, store to a temporary and load out again as the new type.
  return CreateStackStoreLoad(N->getOperand(0), N->getValueType(0));
}

SDValue DAGTypeLegalizer::ExpandOp_BUILD_VECTOR(SDNode *N) {
  // The vector type is legal but the element type needs expansion.
  EVT VecVT = N->getValueType(0);
  unsigned NumElts = VecVT.getVectorNumElements();
  EVT OldVT = N->getOperand(0).getValueType();
  EVT NewVT = TLI.getTypeToTransformTo(*DAG.getContext(), OldVT);
  SDLoc dl(N);

  assert(OldVT == VecVT.getVectorElementType() &&
         "BUILD_VECTOR operand type doesn't match vector element type!");

  // Build a vector of twice the length out of the expanded elements.
  // For example <3 x i64> -> <6 x i32>.
  SmallVector<SDValue, 16> NewElts;
  NewElts.reserve(NumElts*2);

  for (unsigned i = 0; i < NumElts; ++i) {
    SDValue Lo, Hi;
    GetExpandedOp(N->getOperand(i), Lo, Hi);
    if (DAG.getDataLayout().isBigEndian())
      std::swap(Lo, Hi);
    NewElts.push_back(Lo);
    NewElts.push_back(Hi);
  }

  EVT NewVecVT = EVT::getVectorVT(*DAG.getContext(), NewVT, NewElts.size());
  SDValue NewVec = DAG.getBuildVector(NewVecVT, dl, NewElts);

  // Convert the new vector to the old vector type.
  return DAG.getNode(ISD::BITCAST, dl, VecVT, NewVec);
}

SDValue DAGTypeLegalizer::ExpandOp_EXTRACT_ELEMENT(SDNode *N) {
  SDValue Lo, Hi;
  GetExpandedOp(N->getOperand(0), Lo, Hi);
  return N->getConstantOperandVal(1) ? Hi : Lo;
}

SDValue DAGTypeLegalizer::ExpandOp_INSERT_VECTOR_ELT(SDNode *N) {
  // The vector type is legal but the element type needs expansion.
  EVT VecVT = N->getValueType(0);
  unsigned NumElts = VecVT.getVectorNumElements();
  SDLoc dl(N);

  SDValue Val = N->getOperand(1);
  EVT OldEVT = Val.getValueType();
  EVT NewEVT = TLI.getTypeToTransformTo(*DAG.getContext(), OldEVT);

  assert(OldEVT == VecVT.getVectorElementType() &&
         "Inserted element type doesn't match vector element type!");

  // Bitconvert to a vector of twice the length with elements of the expanded
  // type, insert the expanded vector elements, and then convert back.
  EVT NewVecVT = EVT::getVectorVT(*DAG.getContext(), NewEVT, NumElts*2);
  SDValue NewVec = DAG.getNode(ISD::BITCAST, dl,
                               NewVecVT, N->getOperand(0));

  SDValue Lo, Hi;
  GetExpandedOp(Val, Lo, Hi);
  if (DAG.getDataLayout().isBigEndian())
    std::swap(Lo, Hi);

  SDValue Idx = N->getOperand(2);
  Idx = DAG.getNode(ISD::ADD, dl, Idx.getValueType(), Idx, Idx);
  NewVec = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, NewVecVT, NewVec, Lo, Idx);
  Idx = DAG.getNode(ISD::ADD, dl,
                    Idx.getValueType(), Idx,
                    DAG.getConstant(1, dl, Idx.getValueType()));
  NewVec =  DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, NewVecVT, NewVec, Hi, Idx);

  // Convert the new vector to the old vector type.
  return DAG.getNode(ISD::BITCAST, dl, VecVT, NewVec);
}

SDValue DAGTypeLegalizer::ExpandOp_SCALAR_TO_VECTOR(SDNode *N) {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  assert(VT.getVectorElementType() == N->getOperand(0).getValueType() &&
         "SCALAR_TO_VECTOR operand type doesn't match vector element type!");
  unsigned NumElts = VT.getVectorNumElements();
  SmallVector<SDValue, 16> Ops(NumElts);
  Ops[0] = N->getOperand(0);
  SDValue UndefVal = DAG.getUNDEF(Ops[0].getValueType());
  for (unsigned i = 1; i < NumElts; ++i)
    Ops[i] = UndefVal;
  return DAG.getBuildVector(VT, dl, Ops);
}

SDValue DAGTypeLegalizer::ExpandOp_NormalStore(SDNode *N, unsigned OpNo) {
  assert(ISD::isNormalStore(N) && "This routine only for normal stores!");
  assert(OpNo == 1 && "Can only expand the stored value so far");
  SDLoc dl(N);

  StoreSDNode *St = cast<StoreSDNode>(N);
  assert(!St->isAtomic() && "Atomics can not be split");
  EVT ValueVT = St->getValue().getValueType();
  EVT NVT = TLI.getTypeToTransformTo(*DAG.getContext(), ValueVT);
  SDValue Chain = St->getChain();
  SDValue Ptr = St->getBasePtr();
  AAMDNodes AAInfo = St->getAAInfo();

  assert(NVT.isByteSized() && "Expanded type not byte sized!");
  unsigned IncrementSize = NVT.getSizeInBits() / 8;

  SDValue Lo, Hi;
  GetExpandedOp(St->getValue(), Lo, Hi);

  if (TLI.hasBigEndianPartOrdering(ValueVT, DAG.getDataLayout()))
    std::swap(Lo, Hi);

  Lo = DAG.getStore(Chain, dl, Lo, Ptr, St->getPointerInfo(),
                    St->getOriginalAlign(), St->getMemOperand()->getFlags(),
                    AAInfo);

  Ptr = DAG.getObjectPtrOffset(dl, Ptr, TypeSize::getFixed(IncrementSize));
  Hi = DAG.getStore(
      Chain, dl, Hi, Ptr, St->getPointerInfo().getWithOffset(IncrementSize),
      St->getOriginalAlign(), St->getMemOperand()->getFlags(), AAInfo);

  return DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Lo, Hi);
}


//===--------------------------------------------------------------------===//
// Generic Result Splitting.
//===--------------------------------------------------------------------===//

// Be careful to make no assumptions about which of Lo/Hi is stored first in
// memory (for vectors it is always Lo first followed by Hi in the following
// bytes; for integers and floats it is Lo first if and only if the machine is
// little-endian).

void DAGTypeLegalizer::SplitRes_MERGE_VALUES(SDNode *N, unsigned ResNo,
                                             SDValue &Lo, SDValue &Hi) {
  SDValue Op = DisintegrateMERGE_VALUES(N, ResNo);
  GetSplitOp(Op, Lo, Hi);
}

void DAGTypeLegalizer::SplitRes_Select(SDNode *N, SDValue &Lo, SDValue &Hi) {
  SDValue LL, LH, RL, RH, CL, CH;
  SDLoc dl(N);
  unsigned Opcode = N->getOpcode();
  GetSplitOp(N->getOperand(1), LL, LH);
  GetSplitOp(N->getOperand(2), RL, RH);

  SDValue Cond = N->getOperand(0);
  CL = CH = Cond;
  if (Cond.getValueType().isVector()) {
    if (SDValue Res = WidenVSELECTMask(N))
      std::tie(CL, CH) = DAG.SplitVector(Res, dl);
    // Check if there are already splitted versions of the vector available and
    // use those instead of splitting the mask operand again.
    else if (getTypeAction(Cond.getValueType()) ==
             TargetLowering::TypeSplitVector)
      GetSplitVector(Cond, CL, CH);
    // It seems to improve code to generate two narrow SETCCs as opposed to
    // splitting a wide result vector.
    else if (Cond.getOpcode() == ISD::SETCC) {
      // If the condition is a vXi1 vector, and the LHS of the setcc is a legal
      // type and the setcc result type is the same vXi1, then leave the setcc
      // alone.
      EVT CondLHSVT = Cond.getOperand(0).getValueType();
      if (Cond.getValueType().getVectorElementType() == MVT::i1 &&
          isTypeLegal(CondLHSVT) &&
          getSetCCResultType(CondLHSVT) == Cond.getValueType())
        std::tie(CL, CH) = DAG.SplitVector(Cond, dl);
      else
        SplitVecRes_SETCC(Cond.getNode(), CL, CH);
    } else
      std::tie(CL, CH) = DAG.SplitVector(Cond, dl);
  }

  if (Opcode != ISD::VP_SELECT && Opcode != ISD::VP_MERGE) {
    Lo = DAG.getNode(Opcode, dl, LL.getValueType(), CL, LL, RL);
    Hi = DAG.getNode(Opcode, dl, LH.getValueType(), CH, LH, RH);
    return;
  }

  SDValue EVLLo, EVLHi;
  std::tie(EVLLo, EVLHi) =
      DAG.SplitEVL(N->getOperand(3), N->getValueType(0), dl);

  Lo = DAG.getNode(Opcode, dl, LL.getValueType(), CL, LL, RL, EVLLo);
  Hi = DAG.getNode(Opcode, dl, LH.getValueType(), CH, LH, RH, EVLHi);
}

void DAGTypeLegalizer::SplitRes_SELECT_CC(SDNode *N, SDValue &Lo,
                                          SDValue &Hi) {
  SDValue LL, LH, RL, RH;
  SDLoc dl(N);
  GetSplitOp(N->getOperand(2), LL, LH);
  GetSplitOp(N->getOperand(3), RL, RH);

  Lo = DAG.getNode(ISD::SELECT_CC, dl, LL.getValueType(), N->getOperand(0),
                   N->getOperand(1), LL, RL, N->getOperand(4));
  Hi = DAG.getNode(ISD::SELECT_CC, dl, LH.getValueType(), N->getOperand(0),
                   N->getOperand(1), LH, RH, N->getOperand(4));
}

void DAGTypeLegalizer::SplitRes_UNDEF(SDNode *N, SDValue &Lo, SDValue &Hi) {
  EVT LoVT, HiVT;
  std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(N->getValueType(0));
  Lo = DAG.getUNDEF(LoVT);
  Hi = DAG.getUNDEF(HiVT);
}

void DAGTypeLegalizer::SplitVecRes_AssertZext(SDNode *N, SDValue &Lo,
                                              SDValue &Hi) {
  SDValue L, H;
  SDLoc dl(N);
  GetSplitOp(N->getOperand(0), L, H);

  Lo = DAG.getNode(ISD::AssertZext, dl, L.getValueType(), L, N->getOperand(1));
  Hi = DAG.getNode(ISD::AssertZext, dl, H.getValueType(), H, N->getOperand(1));
}

void DAGTypeLegalizer::SplitRes_FREEZE(SDNode *N, SDValue &Lo, SDValue &Hi) {
  SDValue L, H;
  SDLoc dl(N);
  GetSplitOp(N->getOperand(0), L, H);

  Lo = DAG.getNode(ISD::FREEZE, dl, L.getValueType(), L);
  Hi = DAG.getNode(ISD::FREEZE, dl, H.getValueType(), H);
}

void DAGTypeLegalizer::SplitRes_ARITH_FENCE(SDNode *N, SDValue &Lo,
                                            SDValue &Hi) {
  SDValue L, H;
  SDLoc DL(N);
  GetSplitOp(N->getOperand(0), L, H);

  Lo = DAG.getNode(ISD::ARITH_FENCE, DL, L.getValueType(), L);
  Hi = DAG.getNode(ISD::ARITH_FENCE, DL, H.getValueType(), H);
}
