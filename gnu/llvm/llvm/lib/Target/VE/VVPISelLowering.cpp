//===-- VVPISelLowering.cpp - VE DAG Lowering Implementation --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the lowering and legalization of vector instructions to
// VVP_*layer SDNodes.
//
//===----------------------------------------------------------------------===//

#include "VECustomDAG.h"
#include "VEISelLowering.h"

using namespace llvm;

#define DEBUG_TYPE "ve-lower"

SDValue VETargetLowering::splitMaskArithmetic(SDValue Op,
                                              SelectionDAG &DAG) const {
  VECustomDAG CDAG(DAG, Op);
  SDValue AVL =
      CDAG.getConstant(Op.getValueType().getVectorNumElements(), MVT::i32);
  SDValue A = Op->getOperand(0);
  SDValue B = Op->getOperand(1);
  SDValue LoA = CDAG.getUnpack(MVT::v256i1, A, PackElem::Lo, AVL);
  SDValue HiA = CDAG.getUnpack(MVT::v256i1, A, PackElem::Hi, AVL);
  SDValue LoB = CDAG.getUnpack(MVT::v256i1, B, PackElem::Lo, AVL);
  SDValue HiB = CDAG.getUnpack(MVT::v256i1, B, PackElem::Hi, AVL);
  unsigned Opc = Op.getOpcode();
  auto LoRes = CDAG.getNode(Opc, MVT::v256i1, {LoA, LoB});
  auto HiRes = CDAG.getNode(Opc, MVT::v256i1, {HiA, HiB});
  return CDAG.getPack(MVT::v512i1, LoRes, HiRes, AVL);
}

SDValue VETargetLowering::lowerToVVP(SDValue Op, SelectionDAG &DAG) const {
  // Can we represent this as a VVP node.
  const unsigned Opcode = Op->getOpcode();
  auto VVPOpcodeOpt = getVVPOpcode(Opcode);
  if (!VVPOpcodeOpt)
    return SDValue();
  unsigned VVPOpcode = *VVPOpcodeOpt;
  const bool FromVP = ISD::isVPOpcode(Opcode);

  // The representative and legalized vector type of this operation.
  VECustomDAG CDAG(DAG, Op);
  // Dispatch to complex lowering functions.
  switch (VVPOpcode) {
  case VEISD::VVP_LOAD:
  case VEISD::VVP_STORE:
    return lowerVVP_LOAD_STORE(Op, CDAG);
  case VEISD::VVP_GATHER:
  case VEISD::VVP_SCATTER:
    return lowerVVP_GATHER_SCATTER(Op, CDAG);
  }

  EVT OpVecVT = *getIdiomaticVectorType(Op.getNode());
  EVT LegalVecVT = getTypeToTransformTo(*DAG.getContext(), OpVecVT);
  auto Packing = getTypePacking(LegalVecVT.getSimpleVT());

  SDValue AVL;
  SDValue Mask;

  if (FromVP) {
    // All upstream VP SDNodes always have a mask and avl.
    auto MaskIdx = ISD::getVPMaskIdx(Opcode);
    auto AVLIdx = ISD::getVPExplicitVectorLengthIdx(Opcode);
    if (MaskIdx)
      Mask = Op->getOperand(*MaskIdx);
    if (AVLIdx)
      AVL = Op->getOperand(*AVLIdx);
  }

  // Materialize default mask and avl.
  if (!AVL)
    AVL = CDAG.getConstant(OpVecVT.getVectorNumElements(), MVT::i32);
  if (!Mask)
    Mask = CDAG.getConstantMask(Packing, true);

  assert(LegalVecVT.isSimple());
  if (isVVPUnaryOp(VVPOpcode))
    return CDAG.getNode(VVPOpcode, LegalVecVT, {Op->getOperand(0), Mask, AVL});
  if (isVVPBinaryOp(VVPOpcode))
    return CDAG.getNode(VVPOpcode, LegalVecVT,
                        {Op->getOperand(0), Op->getOperand(1), Mask, AVL});
  if (isVVPReductionOp(VVPOpcode)) {
    auto SrcHasStart = hasReductionStartParam(Op->getOpcode());
    SDValue StartV = SrcHasStart ? Op->getOperand(0) : SDValue();
    SDValue VectorV = Op->getOperand(SrcHasStart ? 1 : 0);
    return CDAG.getLegalReductionOpVVP(VVPOpcode, Op.getValueType(), StartV,
                                       VectorV, Mask, AVL, Op->getFlags());
  }

  switch (VVPOpcode) {
  default:
    llvm_unreachable("lowerToVVP called for unexpected SDNode.");
  case VEISD::VVP_FFMA: {
    // VE has a swizzled operand order in FMA (compared to LLVM IR and
    // SDNodes).
    auto X = Op->getOperand(2);
    auto Y = Op->getOperand(0);
    auto Z = Op->getOperand(1);
    return CDAG.getNode(VVPOpcode, LegalVecVT, {X, Y, Z, Mask, AVL});
  }
  case VEISD::VVP_SELECT: {
    auto Mask = Op->getOperand(0);
    auto OnTrue = Op->getOperand(1);
    auto OnFalse = Op->getOperand(2);
    return CDAG.getNode(VVPOpcode, LegalVecVT, {OnTrue, OnFalse, Mask, AVL});
  }
  case VEISD::VVP_SETCC: {
    EVT LegalResVT = getTypeToTransformTo(*DAG.getContext(), Op.getValueType());
    auto LHS = Op->getOperand(0);
    auto RHS = Op->getOperand(1);
    auto Pred = Op->getOperand(2);
    return CDAG.getNode(VVPOpcode, LegalResVT, {LHS, RHS, Pred, Mask, AVL});
  }
  }
}

SDValue VETargetLowering::lowerVVP_LOAD_STORE(SDValue Op,
                                              VECustomDAG &CDAG) const {
  auto VVPOpc = *getVVPOpcode(Op->getOpcode());
  const bool IsLoad = (VVPOpc == VEISD::VVP_LOAD);

  // Shares.
  SDValue BasePtr = getMemoryPtr(Op);
  SDValue Mask = getNodeMask(Op);
  SDValue Chain = getNodeChain(Op);
  SDValue AVL = getNodeAVL(Op);
  // Store specific.
  SDValue Data = getStoredValue(Op);
  // Load specific.
  SDValue PassThru = getNodePassthru(Op);

  SDValue StrideV = getLoadStoreStride(Op, CDAG);

  auto DataVT = *getIdiomaticVectorType(Op.getNode());
  auto Packing = getTypePacking(DataVT);

  // TODO: Infer lower AVL from mask.
  if (!AVL)
    AVL = CDAG.getConstant(DataVT.getVectorNumElements(), MVT::i32);

  // Default to the all-true mask.
  if (!Mask)
    Mask = CDAG.getConstantMask(Packing, true);

  if (IsLoad) {
    MVT LegalDataVT = getLegalVectorType(
        Packing, DataVT.getVectorElementType().getSimpleVT());

    auto NewLoadV = CDAG.getNode(VEISD::VVP_LOAD, {LegalDataVT, MVT::Other},
                                 {Chain, BasePtr, StrideV, Mask, AVL});

    if (!PassThru || PassThru->isUndef())
      return NewLoadV;

    // Convert passthru to an explicit select node.
    SDValue DataV = CDAG.getNode(VEISD::VVP_SELECT, DataVT,
                                 {NewLoadV, PassThru, Mask, AVL});
    SDValue NewLoadChainV = SDValue(NewLoadV.getNode(), 1);

    // Merge them back into one node.
    return CDAG.getMergeValues({DataV, NewLoadChainV});
  }

  // VVP_STORE
  assert(VVPOpc == VEISD::VVP_STORE);
  if (getTypeAction(*CDAG.getDAG()->getContext(), Data.getValueType()) !=
      TargetLowering::TypeLegal)
    // Doesn't lower store instruction if an operand is not lowered yet.
    // If it isn't, return SDValue().  In this way, LLVM will try to lower
    // store instruction again after lowering all operands.
    return SDValue();
  return CDAG.getNode(VEISD::VVP_STORE, Op.getNode()->getVTList(),
                      {Chain, Data, BasePtr, StrideV, Mask, AVL});
}

SDValue VETargetLowering::splitPackedLoadStore(SDValue Op,
                                               VECustomDAG &CDAG) const {
  auto VVPOC = *getVVPOpcode(Op.getOpcode());
  assert((VVPOC == VEISD::VVP_LOAD) || (VVPOC == VEISD::VVP_STORE));

  MVT DataVT = getIdiomaticVectorType(Op.getNode())->getSimpleVT();
  assert(getTypePacking(DataVT) == Packing::Dense &&
         "Can only split packed load/store");
  MVT SplitDataVT = splitVectorType(DataVT);

  assert(!getNodePassthru(Op) &&
         "Should have been folded in lowering to VVP layer");

  // Analyze the operation
  SDValue PackedMask = getNodeMask(Op);
  SDValue PackedAVL = getAnnotatedNodeAVL(Op).first;
  SDValue PackPtr = getMemoryPtr(Op);
  SDValue PackData = getStoredValue(Op);
  SDValue PackStride = getLoadStoreStride(Op, CDAG);

  unsigned ChainResIdx = PackData ? 0 : 1;

  SDValue PartOps[2];

  SDValue UpperPartAVL; // we will use this for packing things back together
  for (PackElem Part : {PackElem::Hi, PackElem::Lo}) {
    // VP ops already have an explicit mask and AVL. When expanding from non-VP
    // attach those additional inputs here.
    auto SplitTM = CDAG.getTargetSplitMask(PackedMask, PackedAVL, Part);

    // Keep track of the (higher) lvl.
    if (Part == PackElem::Hi)
      UpperPartAVL = SplitTM.AVL;

    // Attach non-predicating value operands
    SmallVector<SDValue, 4> OpVec;

    // Chain
    OpVec.push_back(getNodeChain(Op));

    // Data
    if (PackData) {
      SDValue PartData =
          CDAG.getUnpack(SplitDataVT, PackData, Part, SplitTM.AVL);
      OpVec.push_back(PartData);
    }

    // Ptr & Stride
    // Push (ptr + ElemBytes * <Part>, 2 * ElemBytes)
    // Stride info
    // EVT DataVT = LegalizeVectorType(getMemoryDataVT(Op), Op, DAG, Mode);
    OpVec.push_back(CDAG.getSplitPtrOffset(PackPtr, PackStride, Part));
    OpVec.push_back(CDAG.getSplitPtrStride(PackStride));

    // Add predicating args and generate part node
    OpVec.push_back(SplitTM.Mask);
    OpVec.push_back(SplitTM.AVL);

    if (PackData) {
      // Store
      PartOps[(int)Part] = CDAG.getNode(VVPOC, MVT::Other, OpVec);
    } else {
      // Load
      PartOps[(int)Part] =
          CDAG.getNode(VVPOC, {SplitDataVT, MVT::Other}, OpVec);
    }
  }

  // Merge the chains
  SDValue LowChain = SDValue(PartOps[(int)PackElem::Lo].getNode(), ChainResIdx);
  SDValue HiChain = SDValue(PartOps[(int)PackElem::Hi].getNode(), ChainResIdx);
  SDValue FusedChains =
      CDAG.getNode(ISD::TokenFactor, MVT::Other, {LowChain, HiChain});

  // Chain only [store]
  if (PackData)
    return FusedChains;

  // Re-pack into full packed vector result
  MVT PackedVT =
      getLegalVectorType(Packing::Dense, DataVT.getVectorElementType());
  SDValue PackedVals = CDAG.getPack(PackedVT, PartOps[(int)PackElem::Lo],
                                    PartOps[(int)PackElem::Hi], UpperPartAVL);

  return CDAG.getMergeValues({PackedVals, FusedChains});
}

SDValue VETargetLowering::lowerVVP_GATHER_SCATTER(SDValue Op,
                                                  VECustomDAG &CDAG) const {
  EVT DataVT = *getIdiomaticVectorType(Op.getNode());
  auto Packing = getTypePacking(DataVT);
  MVT LegalDataVT =
      getLegalVectorType(Packing, DataVT.getVectorElementType().getSimpleVT());

  SDValue AVL = getAnnotatedNodeAVL(Op).first;
  SDValue Index = getGatherScatterIndex(Op);
  SDValue BasePtr = getMemoryPtr(Op);
  SDValue Mask = getNodeMask(Op);
  SDValue Chain = getNodeChain(Op);
  SDValue Scale = getGatherScatterScale(Op);
  SDValue PassThru = getNodePassthru(Op);
  SDValue StoredValue = getStoredValue(Op);
  if (PassThru && PassThru->isUndef())
    PassThru = SDValue();

  bool IsScatter = (bool)StoredValue;

  // TODO: Infer lower AVL from mask.
  if (!AVL)
    AVL = CDAG.getConstant(DataVT.getVectorNumElements(), MVT::i32);

  // Default to the all-true mask.
  if (!Mask)
    Mask = CDAG.getConstantMask(Packing, true);

  SDValue AddressVec =
      CDAG.getGatherScatterAddress(BasePtr, Scale, Index, Mask, AVL);
  if (IsScatter)
    return CDAG.getNode(VEISD::VVP_SCATTER, MVT::Other,
                        {Chain, StoredValue, AddressVec, Mask, AVL});

  // Gather.
  SDValue NewLoadV = CDAG.getNode(VEISD::VVP_GATHER, {LegalDataVT, MVT::Other},
                                  {Chain, AddressVec, Mask, AVL});

  if (!PassThru)
    return NewLoadV;

  // TODO: Use vvp_select
  SDValue DataV = CDAG.getNode(VEISD::VVP_SELECT, LegalDataVT,
                               {NewLoadV, PassThru, Mask, AVL});
  SDValue NewLoadChainV = SDValue(NewLoadV.getNode(), 1);
  return CDAG.getMergeValues({DataV, NewLoadChainV});
}

SDValue VETargetLowering::legalizeInternalLoadStoreOp(SDValue Op,
                                                      VECustomDAG &CDAG) const {
  LLVM_DEBUG(dbgs() << "::legalizeInternalLoadStoreOp\n";);
  MVT DataVT = getIdiomaticVectorType(Op.getNode())->getSimpleVT();

  // TODO: Recognize packable load,store.
  if (isPackedVectorType(DataVT))
    return splitPackedLoadStore(Op, CDAG);

  return legalizePackedAVL(Op, CDAG);
}

SDValue VETargetLowering::legalizeInternalVectorOp(SDValue Op,
                                                   SelectionDAG &DAG) const {
  LLVM_DEBUG(dbgs() << "::legalizeInternalVectorOp\n";);
  VECustomDAG CDAG(DAG, Op);

  // Dispatch to specialized legalization functions.
  switch (Op->getOpcode()) {
  case VEISD::VVP_LOAD:
  case VEISD::VVP_STORE:
    return legalizeInternalLoadStoreOp(Op, CDAG);
  }

  EVT IdiomVT = Op.getValueType();
  if (isPackedVectorType(IdiomVT) &&
      !supportsPackedMode(Op.getOpcode(), IdiomVT))
    return splitVectorOp(Op, CDAG);

  // TODO: Implement odd/even splitting.
  return legalizePackedAVL(Op, CDAG);
}

SDValue VETargetLowering::splitVectorOp(SDValue Op, VECustomDAG &CDAG) const {
  MVT ResVT = splitVectorType(Op.getValue(0).getSimpleValueType());

  auto AVLPos = getAVLPos(Op->getOpcode());
  auto MaskPos = getMaskPos(Op->getOpcode());

  SDValue PackedMask = getNodeMask(Op);
  auto AVLPair = getAnnotatedNodeAVL(Op);
  SDValue PackedAVL = AVLPair.first;
  assert(!AVLPair.second && "Expecting non pack-legalized oepration");

  // request the parts
  SDValue PartOps[2];

  SDValue UpperPartAVL; // we will use this for packing things back together
  for (PackElem Part : {PackElem::Hi, PackElem::Lo}) {
    // VP ops already have an explicit mask and AVL. When expanding from non-VP
    // attach those additional inputs here.
    auto SplitTM = CDAG.getTargetSplitMask(PackedMask, PackedAVL, Part);

    if (Part == PackElem::Hi)
      UpperPartAVL = SplitTM.AVL;

    // Attach non-predicating value operands
    SmallVector<SDValue, 4> OpVec;
    for (unsigned i = 0; i < Op.getNumOperands(); ++i) {
      if (AVLPos && ((int)i) == *AVLPos)
        continue;
      if (MaskPos && ((int)i) == *MaskPos)
        continue;

      // Value operand
      auto PackedOperand = Op.getOperand(i);
      auto UnpackedOpVT = splitVectorType(PackedOperand.getSimpleValueType());
      SDValue PartV =
          CDAG.getUnpack(UnpackedOpVT, PackedOperand, Part, SplitTM.AVL);
      OpVec.push_back(PartV);
    }

    // Add predicating args and generate part node.
    OpVec.push_back(SplitTM.Mask);
    OpVec.push_back(SplitTM.AVL);
    // Emit legal VVP nodes.
    PartOps[(int)Part] =
        CDAG.getNode(Op.getOpcode(), ResVT, OpVec, Op->getFlags());
  }

  // Re-package vectors.
  return CDAG.getPack(Op.getValueType(), PartOps[(int)PackElem::Lo],
                      PartOps[(int)PackElem::Hi], UpperPartAVL);
}

SDValue VETargetLowering::legalizePackedAVL(SDValue Op,
                                            VECustomDAG &CDAG) const {
  LLVM_DEBUG(dbgs() << "::legalizePackedAVL\n";);
  // Only required for VEC and VVP ops.
  if (!isVVPOrVEC(Op->getOpcode()))
    return Op;

  // Operation already has a legal AVL.
  auto AVL = getNodeAVL(Op);
  if (isLegalAVL(AVL))
    return Op;

  // Half and round up EVL for 32bit element types.
  SDValue LegalAVL = AVL;
  MVT IdiomVT = getIdiomaticVectorType(Op.getNode())->getSimpleVT();
  if (isPackedVectorType(IdiomVT)) {
    assert(maySafelyIgnoreMask(Op) &&
           "TODO Shift predication from EVL into Mask");

    if (auto *ConstAVL = dyn_cast<ConstantSDNode>(AVL)) {
      LegalAVL = CDAG.getConstant((ConstAVL->getZExtValue() + 1) / 2, MVT::i32);
    } else {
      auto ConstOne = CDAG.getConstant(1, MVT::i32);
      auto PlusOne = CDAG.getNode(ISD::ADD, MVT::i32, {AVL, ConstOne});
      LegalAVL = CDAG.getNode(ISD::SRL, MVT::i32, {PlusOne, ConstOne});
    }
  }

  SDValue AnnotatedLegalAVL = CDAG.annotateLegalAVL(LegalAVL);

  // Copy the operand list.
  int NumOp = Op->getNumOperands();
  auto AVLPos = getAVLPos(Op->getOpcode());
  std::vector<SDValue> FixedOperands;
  for (int i = 0; i < NumOp; ++i) {
    if (AVLPos && (i == *AVLPos)) {
      FixedOperands.push_back(AnnotatedLegalAVL);
      continue;
    }
    FixedOperands.push_back(Op->getOperand(i));
  }

  // Clone the operation with fixed operands.
  auto Flags = Op->getFlags();
  SDValue NewN =
      CDAG.getNode(Op->getOpcode(), Op->getVTList(), FixedOperands, Flags);
  return NewN;
}
