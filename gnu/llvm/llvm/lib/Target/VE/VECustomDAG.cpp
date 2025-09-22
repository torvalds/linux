//===-- VECustomDAG.h - VE Custom DAG Nodes ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that VE uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "VECustomDAG.h"

#ifndef DEBUG_TYPE
#define DEBUG_TYPE "vecustomdag"
#endif

namespace llvm {

bool isPackedVectorType(EVT SomeVT) {
  if (!SomeVT.isVector())
    return false;
  return SomeVT.getVectorNumElements() > StandardVectorWidth;
}

MVT splitVectorType(MVT VT) {
  if (!VT.isVector())
    return VT;
  return MVT::getVectorVT(VT.getVectorElementType(), StandardVectorWidth);
}

MVT getLegalVectorType(Packing P, MVT ElemVT) {
  return MVT::getVectorVT(ElemVT, P == Packing::Normal ? StandardVectorWidth
                                                       : PackedVectorWidth);
}

Packing getTypePacking(EVT VT) {
  assert(VT.isVector());
  return isPackedVectorType(VT) ? Packing::Dense : Packing::Normal;
}

bool isMaskType(EVT SomeVT) {
  if (!SomeVT.isVector())
    return false;
  return SomeVT.getVectorElementType() == MVT::i1;
}

bool isMaskArithmetic(SDValue Op) {
  switch (Op.getOpcode()) {
  default:
    return false;
  case ISD::AND:
  case ISD::XOR:
  case ISD::OR:
    return isMaskType(Op.getValueType());
  }
}

/// \returns the VVP_* SDNode opcode corresponsing to \p OC.
std::optional<unsigned> getVVPOpcode(unsigned Opcode) {
  switch (Opcode) {
  case ISD::MLOAD:
    return VEISD::VVP_LOAD;
  case ISD::MSTORE:
    return VEISD::VVP_STORE;
#define HANDLE_VP_TO_VVP(VPOPC, VVPNAME)                                       \
  case ISD::VPOPC:                                                             \
    return VEISD::VVPNAME;
#define ADD_VVP_OP(VVPNAME, SDNAME)                                            \
  case VEISD::VVPNAME:                                                         \
  case ISD::SDNAME:                                                            \
    return VEISD::VVPNAME;
#include "VVPNodes.def"
  // TODO: Map those in VVPNodes.def too
  case ISD::EXPERIMENTAL_VP_STRIDED_LOAD:
    return VEISD::VVP_LOAD;
  case ISD::EXPERIMENTAL_VP_STRIDED_STORE:
    return VEISD::VVP_STORE;
  }
  return std::nullopt;
}

bool maySafelyIgnoreMask(SDValue Op) {
  auto VVPOpc = getVVPOpcode(Op->getOpcode());
  auto Opc = VVPOpc.value_or(Op->getOpcode());

  switch (Opc) {
  case VEISD::VVP_SDIV:
  case VEISD::VVP_UDIV:
  case VEISD::VVP_FDIV:
  case VEISD::VVP_SELECT:
    return false;

  default:
    return true;
  }
}

bool supportsPackedMode(unsigned Opcode, EVT IdiomVT) {
  bool IsPackedOp = isPackedVectorType(IdiomVT);
  bool IsMaskOp = isMaskType(IdiomVT);
  switch (Opcode) {
  default:
    return false;

  case VEISD::VEC_BROADCAST:
    return true;
#define REGISTER_PACKED(VVP_NAME) case VEISD::VVP_NAME:
#include "VVPNodes.def"
    return IsPackedOp && !IsMaskOp;
  }
}

bool isPackingSupportOpcode(unsigned Opc) {
  switch (Opc) {
  case VEISD::VEC_PACK:
  case VEISD::VEC_UNPACK_LO:
  case VEISD::VEC_UNPACK_HI:
    return true;
  }
  return false;
}

bool isVVPOrVEC(unsigned Opcode) {
  switch (Opcode) {
  case VEISD::VEC_BROADCAST:
#define ADD_VVP_OP(VVPNAME, ...) case VEISD::VVPNAME:
#include "VVPNodes.def"
    return true;
  }
  return false;
}

bool isVVPUnaryOp(unsigned VVPOpcode) {
  switch (VVPOpcode) {
#define ADD_UNARY_VVP_OP(VVPNAME, ...)                                         \
  case VEISD::VVPNAME:                                                         \
    return true;
#include "VVPNodes.def"
  }
  return false;
}

bool isVVPBinaryOp(unsigned VVPOpcode) {
  switch (VVPOpcode) {
#define ADD_BINARY_VVP_OP(VVPNAME, ...)                                        \
  case VEISD::VVPNAME:                                                         \
    return true;
#include "VVPNodes.def"
  }
  return false;
}

bool isVVPReductionOp(unsigned Opcode) {
  switch (Opcode) {
#define ADD_REDUCE_VVP_OP(VVP_NAME, SDNAME) case VEISD::VVP_NAME:
#include "VVPNodes.def"
    return true;
  }
  return false;
}

// Return the AVL operand position for this VVP or VEC Op.
std::optional<int> getAVLPos(unsigned Opc) {
  // This is only available for VP SDNodes
  auto PosOpt = ISD::getVPExplicitVectorLengthIdx(Opc);
  if (PosOpt)
    return *PosOpt;

  // VVP Opcodes.
  if (isVVPBinaryOp(Opc))
    return 3;

  // VM Opcodes.
  switch (Opc) {
  case VEISD::VEC_BROADCAST:
    return 1;
  case VEISD::VVP_SELECT:
    return 3;
  case VEISD::VVP_LOAD:
    return 4;
  case VEISD::VVP_STORE:
    return 5;
  }

  return std::nullopt;
}

std::optional<int> getMaskPos(unsigned Opc) {
  // This is only available for VP SDNodes
  auto PosOpt = ISD::getVPMaskIdx(Opc);
  if (PosOpt)
    return *PosOpt;

  // VVP Opcodes.
  if (isVVPBinaryOp(Opc))
    return 2;

  // Other opcodes.
  switch (Opc) {
  case ISD::MSTORE:
    return 4;
  case ISD::MLOAD:
    return 3;
  case VEISD::VVP_SELECT:
    return 2;
  }

  return std::nullopt;
}

bool isLegalAVL(SDValue AVL) { return AVL->getOpcode() == VEISD::LEGALAVL; }

/// Node Properties {

SDValue getNodeChain(SDValue Op) {
  if (MemSDNode *MemN = dyn_cast<MemSDNode>(Op.getNode()))
    return MemN->getChain();

  switch (Op->getOpcode()) {
  case VEISD::VVP_LOAD:
  case VEISD::VVP_STORE:
    return Op->getOperand(0);
  }
  return SDValue();
}

SDValue getMemoryPtr(SDValue Op) {
  if (auto *MemN = dyn_cast<MemSDNode>(Op.getNode()))
    return MemN->getBasePtr();

  switch (Op->getOpcode()) {
  case VEISD::VVP_LOAD:
    return Op->getOperand(1);
  case VEISD::VVP_STORE:
    return Op->getOperand(2);
  }
  return SDValue();
}

std::optional<EVT> getIdiomaticVectorType(SDNode *Op) {
  unsigned OC = Op->getOpcode();

  // For memory ops -> the transfered data type
  if (auto MemN = dyn_cast<MemSDNode>(Op))
    return MemN->getMemoryVT();

  switch (OC) {
  // Standard ISD.
  case ISD::SELECT: // not aliased with VVP_SELECT
  case ISD::CONCAT_VECTORS:
  case ISD::EXTRACT_SUBVECTOR:
  case ISD::VECTOR_SHUFFLE:
  case ISD::BUILD_VECTOR:
  case ISD::SCALAR_TO_VECTOR:
    return Op->getValueType(0);
  }

  // Translate to VVP where possible.
  unsigned OriginalOC = OC;
  if (auto VVPOpc = getVVPOpcode(OC))
    OC = *VVPOpc;

  if (isVVPReductionOp(OC))
    return Op->getOperand(hasReductionStartParam(OriginalOC) ? 1 : 0)
        .getValueType();

  switch (OC) {
  default:
  case VEISD::VVP_SETCC:
    return Op->getOperand(0).getValueType();

  case VEISD::VVP_SELECT:
#define ADD_BINARY_VVP_OP(VVP_NAME, ...) case VEISD::VVP_NAME:
#include "VVPNodes.def"
    return Op->getValueType(0);

  case VEISD::VVP_LOAD:
    return Op->getValueType(0);

  case VEISD::VVP_STORE:
    return Op->getOperand(1)->getValueType(0);

  // VEC
  case VEISD::VEC_BROADCAST:
    return Op->getValueType(0);
  }
}

SDValue getLoadStoreStride(SDValue Op, VECustomDAG &CDAG) {
  switch (Op->getOpcode()) {
  case VEISD::VVP_STORE:
    return Op->getOperand(3);
  case VEISD::VVP_LOAD:
    return Op->getOperand(2);
  }

  if (auto *StoreN = dyn_cast<VPStridedStoreSDNode>(Op.getNode()))
    return StoreN->getStride();
  if (auto *StoreN = dyn_cast<VPStridedLoadSDNode>(Op.getNode()))
    return StoreN->getStride();

  if (isa<MemSDNode>(Op.getNode())) {
    // Regular MLOAD/MSTORE/LOAD/STORE
    // No stride argument -> use the contiguous element size as stride.
    uint64_t ElemStride = getIdiomaticVectorType(Op.getNode())
                              ->getVectorElementType()
                              .getStoreSize();
    return CDAG.getConstant(ElemStride, MVT::i64);
  }
  return SDValue();
}

SDValue getGatherScatterIndex(SDValue Op) {
  if (auto *N = dyn_cast<MaskedGatherScatterSDNode>(Op.getNode()))
    return N->getIndex();
  if (auto *N = dyn_cast<VPGatherScatterSDNode>(Op.getNode()))
    return N->getIndex();
  return SDValue();
}

SDValue getGatherScatterScale(SDValue Op) {
  if (auto *N = dyn_cast<MaskedGatherScatterSDNode>(Op.getNode()))
    return N->getScale();
  if (auto *N = dyn_cast<VPGatherScatterSDNode>(Op.getNode()))
    return N->getScale();
  return SDValue();
}

SDValue getStoredValue(SDValue Op) {
  switch (Op->getOpcode()) {
  case ISD::EXPERIMENTAL_VP_STRIDED_STORE:
  case VEISD::VVP_STORE:
    return Op->getOperand(1);
  }
  if (auto *StoreN = dyn_cast<StoreSDNode>(Op.getNode()))
    return StoreN->getValue();
  if (auto *StoreN = dyn_cast<MaskedStoreSDNode>(Op.getNode()))
    return StoreN->getValue();
  if (auto *StoreN = dyn_cast<VPStridedStoreSDNode>(Op.getNode()))
    return StoreN->getValue();
  if (auto *StoreN = dyn_cast<VPStoreSDNode>(Op.getNode()))
    return StoreN->getValue();
  if (auto *StoreN = dyn_cast<MaskedScatterSDNode>(Op.getNode()))
    return StoreN->getValue();
  if (auto *StoreN = dyn_cast<VPScatterSDNode>(Op.getNode()))
    return StoreN->getValue();
  return SDValue();
}

SDValue getNodePassthru(SDValue Op) {
  if (auto *N = dyn_cast<MaskedLoadSDNode>(Op.getNode()))
    return N->getPassThru();
  if (auto *N = dyn_cast<MaskedGatherSDNode>(Op.getNode()))
    return N->getPassThru();

  return SDValue();
}

bool hasReductionStartParam(unsigned OPC) {
  // TODO: Ordered reduction opcodes.
  if (ISD::isVPReduction(OPC))
    return true;
  return false;
}

unsigned getScalarReductionOpcode(unsigned VVPOC, bool IsMask) {
  assert(!IsMask && "Mask reduction isel");

  switch (VVPOC) {
#define HANDLE_VVP_REDUCE_TO_SCALAR(VVP_RED_ISD, REDUCE_ISD)                   \
  case VEISD::VVP_RED_ISD:                                                     \
    return ISD::REDUCE_ISD;
#include "VVPNodes.def"
  default:
    break;
  }
  llvm_unreachable("Cannot not scalarize this reduction Opcode!");
}

/// } Node Properties

SDValue getNodeAVL(SDValue Op) {
  auto PosOpt = getAVLPos(Op->getOpcode());
  return PosOpt ? Op->getOperand(*PosOpt) : SDValue();
}

SDValue getNodeMask(SDValue Op) {
  auto PosOpt = getMaskPos(Op->getOpcode());
  return PosOpt ? Op->getOperand(*PosOpt) : SDValue();
}

std::pair<SDValue, bool> getAnnotatedNodeAVL(SDValue Op) {
  SDValue AVL = getNodeAVL(Op);
  if (!AVL)
    return {SDValue(), true};
  if (isLegalAVL(AVL))
    return {AVL->getOperand(0), true};
  return {AVL, false};
}

SDValue VECustomDAG::getConstant(uint64_t Val, EVT VT, bool IsTarget,
                                 bool IsOpaque) const {
  return DAG.getConstant(Val, DL, VT, IsTarget, IsOpaque);
}

SDValue VECustomDAG::getConstantMask(Packing Packing, bool AllTrue) const {
  auto MaskVT = getLegalVectorType(Packing, MVT::i1);

  // VEISelDAGtoDAG will replace this pattern with the constant-true VM.
  auto TrueVal = DAG.getConstant(-1, DL, MVT::i32);
  auto AVL = getConstant(MaskVT.getVectorNumElements(), MVT::i32);
  auto Res = getNode(VEISD::VEC_BROADCAST, MaskVT, {TrueVal, AVL});
  if (AllTrue)
    return Res;

  return DAG.getNOT(DL, Res, Res.getValueType());
}

SDValue VECustomDAG::getMaskBroadcast(EVT ResultVT, SDValue Scalar,
                                      SDValue AVL) const {
  // Constant mask splat.
  if (auto BcConst = dyn_cast<ConstantSDNode>(Scalar))
    return getConstantMask(getTypePacking(ResultVT),
                           BcConst->getSExtValue() != 0);

  // Expand the broadcast to a vector comparison.
  auto ScalarBoolVT = Scalar.getSimpleValueType();
  assert(ScalarBoolVT == MVT::i32);

  // Cast to i32 ty.
  SDValue CmpElem = DAG.getSExtOrTrunc(Scalar, DL, MVT::i32);
  unsigned ElemCount = ResultVT.getVectorNumElements();
  MVT CmpVecTy = MVT::getVectorVT(ScalarBoolVT, ElemCount);

  // Broadcast to vector.
  SDValue BCVec =
      DAG.getNode(VEISD::VEC_BROADCAST, DL, CmpVecTy, {CmpElem, AVL});
  SDValue ZeroVec =
      getBroadcast(CmpVecTy, {DAG.getConstant(0, DL, ScalarBoolVT)}, AVL);

  MVT BoolVecTy = MVT::getVectorVT(MVT::i1, ElemCount);

  // Broadcast(Data) != Broadcast(0)
  // TODO: Use a VVP operation for this.
  return DAG.getSetCC(DL, BoolVecTy, BCVec, ZeroVec, ISD::CondCode::SETNE);
}

SDValue VECustomDAG::getBroadcast(EVT ResultVT, SDValue Scalar,
                                  SDValue AVL) const {
  assert(ResultVT.isVector());
  auto ScaVT = Scalar.getValueType();

  if (isMaskType(ResultVT))
    return getMaskBroadcast(ResultVT, Scalar, AVL);

  if (isPackedVectorType(ResultVT)) {
    // v512x packed mode broadcast
    // Replicate the scalar reg (f32 or i32) onto the opposing half of the full
    // scalar register. If it's an I64 type, assume that this has already
    // happened.
    if (ScaVT == MVT::f32) {
      Scalar = getNode(VEISD::REPL_F32, MVT::i64, Scalar);
    } else if (ScaVT == MVT::i32) {
      Scalar = getNode(VEISD::REPL_I32, MVT::i64, Scalar);
    }
  }

  return getNode(VEISD::VEC_BROADCAST, ResultVT, {Scalar, AVL});
}

SDValue VECustomDAG::annotateLegalAVL(SDValue AVL) const {
  if (isLegalAVL(AVL))
    return AVL;
  return getNode(VEISD::LEGALAVL, AVL.getValueType(), AVL);
}

SDValue VECustomDAG::getUnpack(EVT DestVT, SDValue Vec, PackElem Part,
                               SDValue AVL) const {
  assert(getAnnotatedNodeAVL(AVL).second && "Expected a pack-legalized AVL");

  // TODO: Peek through VEC_PACK and VEC_BROADCAST(REPL_<sth> ..) operands.
  unsigned OC =
      (Part == PackElem::Lo) ? VEISD::VEC_UNPACK_LO : VEISD::VEC_UNPACK_HI;
  return DAG.getNode(OC, DL, DestVT, Vec, AVL);
}

SDValue VECustomDAG::getPack(EVT DestVT, SDValue LoVec, SDValue HiVec,
                             SDValue AVL) const {
  assert(getAnnotatedNodeAVL(AVL).second && "Expected a pack-legalized AVL");

  // TODO: Peek through VEC_UNPACK_LO|HI operands.
  return DAG.getNode(VEISD::VEC_PACK, DL, DestVT, LoVec, HiVec, AVL);
}

VETargetMasks VECustomDAG::getTargetSplitMask(SDValue RawMask, SDValue RawAVL,
                                              PackElem Part) const {
  // Adjust AVL for this part
  SDValue NewAVL;
  SDValue OneV = getConstant(1, MVT::i32);
  if (Part == PackElem::Hi)
    NewAVL = getNode(ISD::ADD, MVT::i32, {RawAVL, OneV});
  else
    NewAVL = RawAVL;
  NewAVL = getNode(ISD::SRL, MVT::i32, {NewAVL, OneV});

  NewAVL = annotateLegalAVL(NewAVL);

  // Legalize Mask (unpack or all-true)
  SDValue NewMask;
  if (!RawMask)
    NewMask = getConstantMask(Packing::Normal, true);
  else
    NewMask = getUnpack(MVT::v256i1, RawMask, Part, NewAVL);

  return VETargetMasks(NewMask, NewAVL);
}

SDValue VECustomDAG::getSplitPtrOffset(SDValue Ptr, SDValue ByteStride,
                                       PackElem Part) const {
  // High starts at base ptr but has more significant bits in the 64bit vector
  // element.
  if (Part == PackElem::Hi)
    return Ptr;
  return getNode(ISD::ADD, MVT::i64, {Ptr, ByteStride});
}

SDValue VECustomDAG::getSplitPtrStride(SDValue PackStride) const {
  if (auto ConstBytes = dyn_cast<ConstantSDNode>(PackStride))
    return getConstant(2 * ConstBytes->getSExtValue(), MVT::i64);
  return getNode(ISD::SHL, MVT::i64, {PackStride, getConstant(1, MVT::i32)});
}

SDValue VECustomDAG::getGatherScatterAddress(SDValue BasePtr, SDValue Scale,
                                             SDValue Index, SDValue Mask,
                                             SDValue AVL) const {
  EVT IndexVT = Index.getValueType();

  // Apply scale.
  SDValue ScaledIndex;
  if (!Scale || isOneConstant(Scale))
    ScaledIndex = Index;
  else {
    SDValue ScaleBroadcast = getBroadcast(IndexVT, Scale, AVL);
    ScaledIndex =
        getNode(VEISD::VVP_MUL, IndexVT, {Index, ScaleBroadcast, Mask, AVL});
  }

  // Add basePtr.
  if (isNullConstant(BasePtr))
    return ScaledIndex;

  // re-constitute pointer vector (basePtr + index * scale)
  SDValue BaseBroadcast = getBroadcast(IndexVT, BasePtr, AVL);
  auto ResPtr =
      getNode(VEISD::VVP_ADD, IndexVT, {BaseBroadcast, ScaledIndex, Mask, AVL});
  return ResPtr;
}

SDValue VECustomDAG::getLegalReductionOpVVP(unsigned VVPOpcode, EVT ResVT,
                                            SDValue StartV, SDValue VectorV,
                                            SDValue Mask, SDValue AVL,
                                            SDNodeFlags Flags) const {

  // Optionally attach the start param with a scalar op (where it is
  // unsupported).
  bool scalarizeStartParam = StartV && !hasReductionStartParam(VVPOpcode);
  bool IsMaskReduction = isMaskType(VectorV.getValueType());
  assert(!IsMaskReduction && "TODO Implement");
  auto AttachStartValue = [&](SDValue ReductionResV) {
    if (!scalarizeStartParam)
      return ReductionResV;
    auto ScalarOC = getScalarReductionOpcode(VVPOpcode, IsMaskReduction);
    return getNode(ScalarOC, ResVT, {StartV, ReductionResV});
  };

  // Fixup: Always Use sequential 'fmul' reduction.
  if (!scalarizeStartParam && StartV) {
    assert(hasReductionStartParam(VVPOpcode));
    return AttachStartValue(
        getNode(VVPOpcode, ResVT, {StartV, VectorV, Mask, AVL}, Flags));
  } else
    return AttachStartValue(
        getNode(VVPOpcode, ResVT, {VectorV, Mask, AVL}, Flags));
}

} // namespace llvm
