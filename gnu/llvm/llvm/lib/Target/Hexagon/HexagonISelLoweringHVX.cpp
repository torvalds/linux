//===-- HexagonISelLoweringHVX.cpp --- Lowering HVX operations ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HexagonISelLowering.h"
#include "HexagonRegisterInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/IntrinsicsHexagon.h"
#include "llvm/Support/CommandLine.h"

#include <algorithm>
#include <string>
#include <utility>

using namespace llvm;

static cl::opt<unsigned> HvxWidenThreshold("hexagon-hvx-widen",
  cl::Hidden, cl::init(16),
  cl::desc("Lower threshold (in bytes) for widening to HVX vectors"));

static const MVT LegalV64[] =  { MVT::v64i8,  MVT::v32i16,  MVT::v16i32 };
static const MVT LegalW64[] =  { MVT::v128i8, MVT::v64i16,  MVT::v32i32 };
static const MVT LegalV128[] = { MVT::v128i8, MVT::v64i16,  MVT::v32i32 };
static const MVT LegalW128[] = { MVT::v256i8, MVT::v128i16, MVT::v64i32 };

static std::tuple<unsigned, unsigned, unsigned> getIEEEProperties(MVT Ty) {
  // For a float scalar type, return (exp-bits, exp-bias, fraction-bits)
  MVT ElemTy = Ty.getScalarType();
  switch (ElemTy.SimpleTy) {
    case MVT::f16:
      return std::make_tuple(5, 15, 10);
    case MVT::f32:
      return std::make_tuple(8, 127, 23);
    case MVT::f64:
      return std::make_tuple(11, 1023, 52);
    default:
      break;
  }
  llvm_unreachable(("Unexpected type: " + EVT(ElemTy).getEVTString()).c_str());
}

void
HexagonTargetLowering::initializeHVXLowering() {
  if (Subtarget.useHVX64BOps()) {
    addRegisterClass(MVT::v64i8,  &Hexagon::HvxVRRegClass);
    addRegisterClass(MVT::v32i16, &Hexagon::HvxVRRegClass);
    addRegisterClass(MVT::v16i32, &Hexagon::HvxVRRegClass);
    addRegisterClass(MVT::v128i8, &Hexagon::HvxWRRegClass);
    addRegisterClass(MVT::v64i16, &Hexagon::HvxWRRegClass);
    addRegisterClass(MVT::v32i32, &Hexagon::HvxWRRegClass);
    // These "short" boolean vector types should be legal because
    // they will appear as results of vector compares. If they were
    // not legal, type legalization would try to make them legal
    // and that would require using operations that do not use or
    // produce such types. That, in turn, would imply using custom
    // nodes, which would be unoptimizable by the DAG combiner.
    // The idea is to rely on target-independent operations as much
    // as possible.
    addRegisterClass(MVT::v16i1, &Hexagon::HvxQRRegClass);
    addRegisterClass(MVT::v32i1, &Hexagon::HvxQRRegClass);
    addRegisterClass(MVT::v64i1, &Hexagon::HvxQRRegClass);
  } else if (Subtarget.useHVX128BOps()) {
    addRegisterClass(MVT::v128i8,  &Hexagon::HvxVRRegClass);
    addRegisterClass(MVT::v64i16,  &Hexagon::HvxVRRegClass);
    addRegisterClass(MVT::v32i32,  &Hexagon::HvxVRRegClass);
    addRegisterClass(MVT::v256i8,  &Hexagon::HvxWRRegClass);
    addRegisterClass(MVT::v128i16, &Hexagon::HvxWRRegClass);
    addRegisterClass(MVT::v64i32,  &Hexagon::HvxWRRegClass);
    addRegisterClass(MVT::v32i1, &Hexagon::HvxQRRegClass);
    addRegisterClass(MVT::v64i1, &Hexagon::HvxQRRegClass);
    addRegisterClass(MVT::v128i1, &Hexagon::HvxQRRegClass);
    if (Subtarget.useHVXV68Ops() && Subtarget.useHVXFloatingPoint()) {
      addRegisterClass(MVT::v32f32, &Hexagon::HvxVRRegClass);
      addRegisterClass(MVT::v64f16, &Hexagon::HvxVRRegClass);
      addRegisterClass(MVT::v64f32, &Hexagon::HvxWRRegClass);
      addRegisterClass(MVT::v128f16, &Hexagon::HvxWRRegClass);
    }
  }

  // Set up operation actions.

  bool Use64b = Subtarget.useHVX64BOps();
  ArrayRef<MVT> LegalV = Use64b ? LegalV64 : LegalV128;
  ArrayRef<MVT> LegalW = Use64b ? LegalW64 : LegalW128;
  MVT ByteV = Use64b ?  MVT::v64i8 : MVT::v128i8;
  MVT WordV = Use64b ? MVT::v16i32 : MVT::v32i32;
  MVT ByteW = Use64b ? MVT::v128i8 : MVT::v256i8;

  auto setPromoteTo = [this] (unsigned Opc, MVT FromTy, MVT ToTy) {
    setOperationAction(Opc, FromTy, Promote);
    AddPromotedToType(Opc, FromTy, ToTy);
  };

  // Handle bitcasts of vector predicates to scalars (e.g. v32i1 to i32).
  // Note: v16i1 -> i16 is handled in type legalization instead of op
  // legalization.
  setOperationAction(ISD::BITCAST,              MVT::i16, Custom);
  setOperationAction(ISD::BITCAST,              MVT::i32, Custom);
  setOperationAction(ISD::BITCAST,              MVT::i64, Custom);
  setOperationAction(ISD::BITCAST,            MVT::v16i1, Custom);
  setOperationAction(ISD::BITCAST,           MVT::v128i1, Custom);
  setOperationAction(ISD::BITCAST,             MVT::i128, Custom);
  setOperationAction(ISD::VECTOR_SHUFFLE,          ByteV, Legal);
  setOperationAction(ISD::VECTOR_SHUFFLE,          ByteW, Legal);
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::Other, Custom);

  if (Subtarget.useHVX128BOps() && Subtarget.useHVXV68Ops() &&
      Subtarget.useHVXFloatingPoint()) {

    static const MVT FloatV[] = { MVT::v64f16, MVT::v32f32 };
    static const MVT FloatW[] = { MVT::v128f16, MVT::v64f32 };

    for (MVT T : FloatV) {
      setOperationAction(ISD::FADD,              T, Legal);
      setOperationAction(ISD::FSUB,              T, Legal);
      setOperationAction(ISD::FMUL,              T, Legal);
      setOperationAction(ISD::FMINNUM,           T, Legal);
      setOperationAction(ISD::FMAXNUM,           T, Legal);

      setOperationAction(ISD::INSERT_SUBVECTOR,  T, Custom);
      setOperationAction(ISD::EXTRACT_SUBVECTOR, T, Custom);

      setOperationAction(ISD::SPLAT_VECTOR,      T, Legal);
      setOperationAction(ISD::SPLAT_VECTOR,      T, Legal);

      setOperationAction(ISD::MLOAD,             T, Custom);
      setOperationAction(ISD::MSTORE,            T, Custom);
      // Custom-lower BUILD_VECTOR. The standard (target-independent)
      // handling of it would convert it to a load, which is not always
      // the optimal choice.
      setOperationAction(ISD::BUILD_VECTOR,      T, Custom);
    }


    // BUILD_VECTOR with f16 operands cannot be promoted without
    // promoting the result, so lower the node to vsplat or constant pool
    setOperationAction(ISD::BUILD_VECTOR,      MVT::f16, Custom);
    setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::f16, Custom);
    setOperationAction(ISD::SPLAT_VECTOR,      MVT::f16, Custom);

    // Vector shuffle is always promoted to ByteV and a bitcast to f16 is
    // generated.
    setPromoteTo(ISD::VECTOR_SHUFFLE, MVT::v128f16, ByteW);
    setPromoteTo(ISD::VECTOR_SHUFFLE,  MVT::v64f16, ByteV);
    setPromoteTo(ISD::VECTOR_SHUFFLE,  MVT::v64f32, ByteW);
    setPromoteTo(ISD::VECTOR_SHUFFLE,  MVT::v32f32, ByteV);

    for (MVT P : FloatW) {
      setOperationAction(ISD::LOAD,           P, Custom);
      setOperationAction(ISD::STORE,          P, Custom);
      setOperationAction(ISD::FADD,           P, Custom);
      setOperationAction(ISD::FSUB,           P, Custom);
      setOperationAction(ISD::FMUL,           P, Custom);
      setOperationAction(ISD::FMINNUM,        P, Custom);
      setOperationAction(ISD::FMAXNUM,        P, Custom);
      setOperationAction(ISD::SETCC,          P, Custom);
      setOperationAction(ISD::VSELECT,        P, Custom);

      // Custom-lower BUILD_VECTOR. The standard (target-independent)
      // handling of it would convert it to a load, which is not always
      // the optimal choice.
      setOperationAction(ISD::BUILD_VECTOR,   P, Custom);
      // Make concat-vectors custom to handle concats of more than 2 vectors.
      setOperationAction(ISD::CONCAT_VECTORS, P, Custom);

      setOperationAction(ISD::MLOAD,          P, Custom);
      setOperationAction(ISD::MSTORE,         P, Custom);
    }

    if (Subtarget.useHVXQFloatOps()) {
      setOperationAction(ISD::FP_EXTEND, MVT::v64f32, Custom);
      setOperationAction(ISD::FP_ROUND,  MVT::v64f16, Legal);
    } else if (Subtarget.useHVXIEEEFPOps()) {
      setOperationAction(ISD::FP_EXTEND, MVT::v64f32, Legal);
      setOperationAction(ISD::FP_ROUND,  MVT::v64f16, Legal);
    }
  }

  for (MVT T : LegalV) {
    setIndexedLoadAction(ISD::POST_INC,  T, Legal);
    setIndexedStoreAction(ISD::POST_INC, T, Legal);

    setOperationAction(ISD::ABS,            T, Legal);
    setOperationAction(ISD::AND,            T, Legal);
    setOperationAction(ISD::OR,             T, Legal);
    setOperationAction(ISD::XOR,            T, Legal);
    setOperationAction(ISD::ADD,            T, Legal);
    setOperationAction(ISD::SUB,            T, Legal);
    setOperationAction(ISD::MUL,            T, Legal);
    setOperationAction(ISD::CTPOP,          T, Legal);
    setOperationAction(ISD::CTLZ,           T, Legal);
    setOperationAction(ISD::SELECT,         T, Legal);
    setOperationAction(ISD::SPLAT_VECTOR,   T, Legal);
    if (T != ByteV) {
      setOperationAction(ISD::SIGN_EXTEND_VECTOR_INREG, T, Legal);
      setOperationAction(ISD::ZERO_EXTEND_VECTOR_INREG, T, Legal);
      setOperationAction(ISD::BSWAP,                    T, Legal);
    }

    setOperationAction(ISD::SMIN,           T, Legal);
    setOperationAction(ISD::SMAX,           T, Legal);
    if (T.getScalarType() != MVT::i32) {
      setOperationAction(ISD::UMIN,         T, Legal);
      setOperationAction(ISD::UMAX,         T, Legal);
    }

    setOperationAction(ISD::CTTZ,               T, Custom);
    setOperationAction(ISD::LOAD,               T, Custom);
    setOperationAction(ISD::MLOAD,              T, Custom);
    setOperationAction(ISD::MSTORE,             T, Custom);
    if (T.getScalarType() != MVT::i32) {
      setOperationAction(ISD::MULHS,              T, Legal);
      setOperationAction(ISD::MULHU,              T, Legal);
    }

    setOperationAction(ISD::BUILD_VECTOR,       T, Custom);
    // Make concat-vectors custom to handle concats of more than 2 vectors.
    setOperationAction(ISD::CONCAT_VECTORS,     T, Custom);
    setOperationAction(ISD::INSERT_SUBVECTOR,   T, Custom);
    setOperationAction(ISD::INSERT_VECTOR_ELT,  T, Custom);
    setOperationAction(ISD::EXTRACT_SUBVECTOR,  T, Custom);
    setOperationAction(ISD::EXTRACT_VECTOR_ELT, T, Custom);
    setOperationAction(ISD::ANY_EXTEND,         T, Custom);
    setOperationAction(ISD::SIGN_EXTEND,        T, Custom);
    setOperationAction(ISD::ZERO_EXTEND,        T, Custom);
    setOperationAction(ISD::FSHL,               T, Custom);
    setOperationAction(ISD::FSHR,               T, Custom);
    if (T != ByteV) {
      setOperationAction(ISD::ANY_EXTEND_VECTOR_INREG, T, Custom);
      // HVX only has shifts of words and halfwords.
      setOperationAction(ISD::SRA,                     T, Custom);
      setOperationAction(ISD::SHL,                     T, Custom);
      setOperationAction(ISD::SRL,                     T, Custom);

      // Promote all shuffles to operate on vectors of bytes.
      setPromoteTo(ISD::VECTOR_SHUFFLE, T, ByteV);
    }

    if (Subtarget.useHVXFloatingPoint()) {
      // Same action for both QFloat and IEEE.
      setOperationAction(ISD::SINT_TO_FP, T, Custom);
      setOperationAction(ISD::UINT_TO_FP, T, Custom);
      setOperationAction(ISD::FP_TO_SINT, T, Custom);
      setOperationAction(ISD::FP_TO_UINT, T, Custom);
    }

    setCondCodeAction(ISD::SETNE,  T, Expand);
    setCondCodeAction(ISD::SETLE,  T, Expand);
    setCondCodeAction(ISD::SETGE,  T, Expand);
    setCondCodeAction(ISD::SETLT,  T, Expand);
    setCondCodeAction(ISD::SETULE, T, Expand);
    setCondCodeAction(ISD::SETUGE, T, Expand);
    setCondCodeAction(ISD::SETULT, T, Expand);
  }

  for (MVT T : LegalW) {
    // Custom-lower BUILD_VECTOR for vector pairs. The standard (target-
    // independent) handling of it would convert it to a load, which is
    // not always the optimal choice.
    setOperationAction(ISD::BUILD_VECTOR,   T, Custom);
    // Make concat-vectors custom to handle concats of more than 2 vectors.
    setOperationAction(ISD::CONCAT_VECTORS, T, Custom);

    // Custom-lower these operations for pairs. Expand them into a concat
    // of the corresponding operations on individual vectors.
    setOperationAction(ISD::ANY_EXTEND,               T, Custom);
    setOperationAction(ISD::SIGN_EXTEND,              T, Custom);
    setOperationAction(ISD::ZERO_EXTEND,              T, Custom);
    setOperationAction(ISD::SIGN_EXTEND_INREG,        T, Custom);
    setOperationAction(ISD::ANY_EXTEND_VECTOR_INREG,  T, Custom);
    setOperationAction(ISD::SIGN_EXTEND_VECTOR_INREG, T, Legal);
    setOperationAction(ISD::ZERO_EXTEND_VECTOR_INREG, T, Legal);
    setOperationAction(ISD::SPLAT_VECTOR,             T, Custom);

    setOperationAction(ISD::LOAD,     T, Custom);
    setOperationAction(ISD::STORE,    T, Custom);
    setOperationAction(ISD::MLOAD,    T, Custom);
    setOperationAction(ISD::MSTORE,   T, Custom);
    setOperationAction(ISD::ABS,      T, Custom);
    setOperationAction(ISD::CTLZ,     T, Custom);
    setOperationAction(ISD::CTTZ,     T, Custom);
    setOperationAction(ISD::CTPOP,    T, Custom);

    setOperationAction(ISD::ADD,      T, Legal);
    setOperationAction(ISD::SUB,      T, Legal);
    setOperationAction(ISD::MUL,      T, Custom);
    setOperationAction(ISD::MULHS,    T, Custom);
    setOperationAction(ISD::MULHU,    T, Custom);
    setOperationAction(ISD::AND,      T, Custom);
    setOperationAction(ISD::OR,       T, Custom);
    setOperationAction(ISD::XOR,      T, Custom);
    setOperationAction(ISD::SETCC,    T, Custom);
    setOperationAction(ISD::VSELECT,  T, Custom);
    if (T != ByteW) {
      setOperationAction(ISD::SRA,      T, Custom);
      setOperationAction(ISD::SHL,      T, Custom);
      setOperationAction(ISD::SRL,      T, Custom);

      // Promote all shuffles to operate on vectors of bytes.
      setPromoteTo(ISD::VECTOR_SHUFFLE, T, ByteW);
    }
    setOperationAction(ISD::FSHL,     T, Custom);
    setOperationAction(ISD::FSHR,     T, Custom);

    setOperationAction(ISD::SMIN,     T, Custom);
    setOperationAction(ISD::SMAX,     T, Custom);
    if (T.getScalarType() != MVT::i32) {
      setOperationAction(ISD::UMIN,     T, Custom);
      setOperationAction(ISD::UMAX,     T, Custom);
    }

    if (Subtarget.useHVXFloatingPoint()) {
      // Same action for both QFloat and IEEE.
      setOperationAction(ISD::SINT_TO_FP, T, Custom);
      setOperationAction(ISD::UINT_TO_FP, T, Custom);
      setOperationAction(ISD::FP_TO_SINT, T, Custom);
      setOperationAction(ISD::FP_TO_UINT, T, Custom);
    }
  }

  // Legalize all of these to HexagonISD::[SU]MUL_LOHI.
  setOperationAction(ISD::MULHS,      WordV, Custom); // -> _LOHI
  setOperationAction(ISD::MULHU,      WordV, Custom); // -> _LOHI
  setOperationAction(ISD::SMUL_LOHI,  WordV, Custom);
  setOperationAction(ISD::UMUL_LOHI,  WordV, Custom);

  setCondCodeAction(ISD::SETNE,  MVT::v64f16, Expand);
  setCondCodeAction(ISD::SETLE,  MVT::v64f16, Expand);
  setCondCodeAction(ISD::SETGE,  MVT::v64f16, Expand);
  setCondCodeAction(ISD::SETLT,  MVT::v64f16, Expand);
  setCondCodeAction(ISD::SETONE, MVT::v64f16, Expand);
  setCondCodeAction(ISD::SETOLE, MVT::v64f16, Expand);
  setCondCodeAction(ISD::SETOGE, MVT::v64f16, Expand);
  setCondCodeAction(ISD::SETOLT, MVT::v64f16, Expand);
  setCondCodeAction(ISD::SETUNE, MVT::v64f16, Expand);
  setCondCodeAction(ISD::SETULE, MVT::v64f16, Expand);
  setCondCodeAction(ISD::SETUGE, MVT::v64f16, Expand);
  setCondCodeAction(ISD::SETULT, MVT::v64f16, Expand);

  setCondCodeAction(ISD::SETNE,  MVT::v32f32, Expand);
  setCondCodeAction(ISD::SETLE,  MVT::v32f32, Expand);
  setCondCodeAction(ISD::SETGE,  MVT::v32f32, Expand);
  setCondCodeAction(ISD::SETLT,  MVT::v32f32, Expand);
  setCondCodeAction(ISD::SETONE, MVT::v32f32, Expand);
  setCondCodeAction(ISD::SETOLE, MVT::v32f32, Expand);
  setCondCodeAction(ISD::SETOGE, MVT::v32f32, Expand);
  setCondCodeAction(ISD::SETOLT, MVT::v32f32, Expand);
  setCondCodeAction(ISD::SETUNE, MVT::v32f32, Expand);
  setCondCodeAction(ISD::SETULE, MVT::v32f32, Expand);
  setCondCodeAction(ISD::SETUGE, MVT::v32f32, Expand);
  setCondCodeAction(ISD::SETULT, MVT::v32f32, Expand);

  // Boolean vectors.

  for (MVT T : LegalW) {
    // Boolean types for vector pairs will overlap with the boolean
    // types for single vectors, e.g.
    //   v64i8  -> v64i1 (single)
    //   v64i16 -> v64i1 (pair)
    // Set these actions first, and allow the single actions to overwrite
    // any duplicates.
    MVT BoolW = MVT::getVectorVT(MVT::i1, T.getVectorNumElements());
    setOperationAction(ISD::SETCC,              BoolW, Custom);
    setOperationAction(ISD::AND,                BoolW, Custom);
    setOperationAction(ISD::OR,                 BoolW, Custom);
    setOperationAction(ISD::XOR,                BoolW, Custom);
    // Masked load/store takes a mask that may need splitting.
    setOperationAction(ISD::MLOAD,              BoolW, Custom);
    setOperationAction(ISD::MSTORE,             BoolW, Custom);
  }

  for (MVT T : LegalV) {
    MVT BoolV = MVT::getVectorVT(MVT::i1, T.getVectorNumElements());
    setOperationAction(ISD::BUILD_VECTOR,       BoolV, Custom);
    setOperationAction(ISD::CONCAT_VECTORS,     BoolV, Custom);
    setOperationAction(ISD::INSERT_SUBVECTOR,   BoolV, Custom);
    setOperationAction(ISD::INSERT_VECTOR_ELT,  BoolV, Custom);
    setOperationAction(ISD::EXTRACT_SUBVECTOR,  BoolV, Custom);
    setOperationAction(ISD::EXTRACT_VECTOR_ELT, BoolV, Custom);
    setOperationAction(ISD::SELECT,             BoolV, Custom);
    setOperationAction(ISD::AND,                BoolV, Legal);
    setOperationAction(ISD::OR,                 BoolV, Legal);
    setOperationAction(ISD::XOR,                BoolV, Legal);
  }

  if (Use64b) {
    for (MVT T: {MVT::v32i8, MVT::v32i16, MVT::v16i8, MVT::v16i16, MVT::v16i32})
      setOperationAction(ISD::SIGN_EXTEND_INREG, T, Legal);
  } else {
    for (MVT T: {MVT::v64i8, MVT::v64i16, MVT::v32i8, MVT::v32i16, MVT::v32i32})
      setOperationAction(ISD::SIGN_EXTEND_INREG, T, Legal);
  }

  // Handle store widening for short vectors.
  unsigned HwLen = Subtarget.getVectorLength();
  for (MVT ElemTy : Subtarget.getHVXElementTypes()) {
    if (ElemTy == MVT::i1)
      continue;
    int ElemWidth = ElemTy.getFixedSizeInBits();
    int MaxElems = (8*HwLen) / ElemWidth;
    for (int N = 2; N < MaxElems; N *= 2) {
      MVT VecTy = MVT::getVectorVT(ElemTy, N);
      auto Action = getPreferredVectorAction(VecTy);
      if (Action == TargetLoweringBase::TypeWidenVector) {
        setOperationAction(ISD::LOAD,         VecTy, Custom);
        setOperationAction(ISD::STORE,        VecTy, Custom);
        setOperationAction(ISD::SETCC,        VecTy, Custom);
        setOperationAction(ISD::TRUNCATE,     VecTy, Custom);
        setOperationAction(ISD::ANY_EXTEND,   VecTy, Custom);
        setOperationAction(ISD::SIGN_EXTEND,  VecTy, Custom);
        setOperationAction(ISD::ZERO_EXTEND,  VecTy, Custom);
        if (Subtarget.useHVXFloatingPoint()) {
          setOperationAction(ISD::FP_TO_SINT,   VecTy, Custom);
          setOperationAction(ISD::FP_TO_UINT,   VecTy, Custom);
          setOperationAction(ISD::SINT_TO_FP,   VecTy, Custom);
          setOperationAction(ISD::UINT_TO_FP,   VecTy, Custom);
        }

        MVT BoolTy = MVT::getVectorVT(MVT::i1, N);
        if (!isTypeLegal(BoolTy))
          setOperationAction(ISD::SETCC, BoolTy, Custom);
      }
    }
  }

  setTargetDAGCombine({ISD::CONCAT_VECTORS, ISD::TRUNCATE, ISD::VSELECT});
}

unsigned
HexagonTargetLowering::getPreferredHvxVectorAction(MVT VecTy) const {
  MVT ElemTy = VecTy.getVectorElementType();
  unsigned VecLen = VecTy.getVectorNumElements();
  unsigned HwLen = Subtarget.getVectorLength();

  // Split vectors of i1 that exceed byte vector length.
  if (ElemTy == MVT::i1 && VecLen > HwLen)
    return TargetLoweringBase::TypeSplitVector;

  ArrayRef<MVT> Tys = Subtarget.getHVXElementTypes();
  // For shorter vectors of i1, widen them if any of the corresponding
  // vectors of integers needs to be widened.
  if (ElemTy == MVT::i1) {
    for (MVT T : Tys) {
      assert(T != MVT::i1);
      auto A = getPreferredHvxVectorAction(MVT::getVectorVT(T, VecLen));
      if (A != ~0u)
        return A;
    }
    return ~0u;
  }

  // If the size of VecTy is at least half of the vector length,
  // widen the vector. Note: the threshold was not selected in
  // any scientific way.
  if (llvm::is_contained(Tys, ElemTy)) {
    unsigned VecWidth = VecTy.getSizeInBits();
    unsigned HwWidth = 8*HwLen;
    if (VecWidth > 2*HwWidth)
      return TargetLoweringBase::TypeSplitVector;

    bool HaveThreshold = HvxWidenThreshold.getNumOccurrences() > 0;
    if (HaveThreshold && 8*HvxWidenThreshold <= VecWidth)
      return TargetLoweringBase::TypeWidenVector;
    if (VecWidth >= HwWidth/2 && VecWidth < HwWidth)
      return TargetLoweringBase::TypeWidenVector;
  }

  // Defer to default.
  return ~0u;
}

unsigned
HexagonTargetLowering::getCustomHvxOperationAction(SDNode &Op) const {
  unsigned Opc = Op.getOpcode();
  switch (Opc) {
  case HexagonISD::SMUL_LOHI:
  case HexagonISD::UMUL_LOHI:
  case HexagonISD::USMUL_LOHI:
    return TargetLoweringBase::Custom;
  }
  return TargetLoweringBase::Legal;
}

SDValue
HexagonTargetLowering::getInt(unsigned IntId, MVT ResTy, ArrayRef<SDValue> Ops,
                              const SDLoc &dl, SelectionDAG &DAG) const {
  SmallVector<SDValue,4> IntOps;
  IntOps.push_back(DAG.getConstant(IntId, dl, MVT::i32));
  append_range(IntOps, Ops);
  return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, ResTy, IntOps);
}

MVT
HexagonTargetLowering::typeJoin(const TypePair &Tys) const {
  assert(Tys.first.getVectorElementType() == Tys.second.getVectorElementType());

  MVT ElemTy = Tys.first.getVectorElementType();
  return MVT::getVectorVT(ElemTy, Tys.first.getVectorNumElements() +
                                  Tys.second.getVectorNumElements());
}

HexagonTargetLowering::TypePair
HexagonTargetLowering::typeSplit(MVT VecTy) const {
  assert(VecTy.isVector());
  unsigned NumElem = VecTy.getVectorNumElements();
  assert((NumElem % 2) == 0 && "Expecting even-sized vector type");
  MVT HalfTy = MVT::getVectorVT(VecTy.getVectorElementType(), NumElem/2);
  return { HalfTy, HalfTy };
}

MVT
HexagonTargetLowering::typeExtElem(MVT VecTy, unsigned Factor) const {
  MVT ElemTy = VecTy.getVectorElementType();
  MVT NewElemTy = MVT::getIntegerVT(ElemTy.getSizeInBits() * Factor);
  return MVT::getVectorVT(NewElemTy, VecTy.getVectorNumElements());
}

MVT
HexagonTargetLowering::typeTruncElem(MVT VecTy, unsigned Factor) const {
  MVT ElemTy = VecTy.getVectorElementType();
  MVT NewElemTy = MVT::getIntegerVT(ElemTy.getSizeInBits() / Factor);
  return MVT::getVectorVT(NewElemTy, VecTy.getVectorNumElements());
}

SDValue
HexagonTargetLowering::opCastElem(SDValue Vec, MVT ElemTy,
                                  SelectionDAG &DAG) const {
  if (ty(Vec).getVectorElementType() == ElemTy)
    return Vec;
  MVT CastTy = tyVector(Vec.getValueType().getSimpleVT(), ElemTy);
  return DAG.getBitcast(CastTy, Vec);
}

SDValue
HexagonTargetLowering::opJoin(const VectorPair &Ops, const SDLoc &dl,
                              SelectionDAG &DAG) const {
  return DAG.getNode(ISD::CONCAT_VECTORS, dl, typeJoin(ty(Ops)),
                     Ops.first, Ops.second);
}

HexagonTargetLowering::VectorPair
HexagonTargetLowering::opSplit(SDValue Vec, const SDLoc &dl,
                               SelectionDAG &DAG) const {
  TypePair Tys = typeSplit(ty(Vec));
  if (Vec.getOpcode() == HexagonISD::QCAT)
    return VectorPair(Vec.getOperand(0), Vec.getOperand(1));
  return DAG.SplitVector(Vec, dl, Tys.first, Tys.second);
}

bool
HexagonTargetLowering::isHvxSingleTy(MVT Ty) const {
  return Subtarget.isHVXVectorType(Ty) &&
         Ty.getSizeInBits() == 8 * Subtarget.getVectorLength();
}

bool
HexagonTargetLowering::isHvxPairTy(MVT Ty) const {
  return Subtarget.isHVXVectorType(Ty) &&
         Ty.getSizeInBits() == 16 * Subtarget.getVectorLength();
}

bool
HexagonTargetLowering::isHvxBoolTy(MVT Ty) const {
  return Subtarget.isHVXVectorType(Ty, true) &&
         Ty.getVectorElementType() == MVT::i1;
}

bool HexagonTargetLowering::allowsHvxMemoryAccess(
    MVT VecTy, MachineMemOperand::Flags Flags, unsigned *Fast) const {
  // Bool vectors are excluded by default, but make it explicit to
  // emphasize that bool vectors cannot be loaded or stored.
  // Also, disallow double vector stores (to prevent unnecessary
  // store widening in DAG combiner).
  if (VecTy.getSizeInBits() > 8*Subtarget.getVectorLength())
    return false;
  if (!Subtarget.isHVXVectorType(VecTy, /*IncludeBool=*/false))
    return false;
  if (Fast)
    *Fast = 1;
  return true;
}

bool HexagonTargetLowering::allowsHvxMisalignedMemoryAccesses(
    MVT VecTy, MachineMemOperand::Flags Flags, unsigned *Fast) const {
  if (!Subtarget.isHVXVectorType(VecTy))
    return false;
  // XXX Should this be false?  vmemu are a bit slower than vmem.
  if (Fast)
    *Fast = 1;
  return true;
}

void HexagonTargetLowering::AdjustHvxInstrPostInstrSelection(
    MachineInstr &MI, SDNode *Node) const {
  unsigned Opc = MI.getOpcode();
  const TargetInstrInfo &TII = *Subtarget.getInstrInfo();
  MachineBasicBlock &MB = *MI.getParent();
  MachineFunction &MF = *MB.getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  DebugLoc DL = MI.getDebugLoc();
  auto At = MI.getIterator();

  switch (Opc) {
  case Hexagon::PS_vsplatib:
    if (Subtarget.useHVXV62Ops()) {
      // SplatV = A2_tfrsi #imm
      // OutV = V6_lvsplatb SplatV
      Register SplatV = MRI.createVirtualRegister(&Hexagon::IntRegsRegClass);
      BuildMI(MB, At, DL, TII.get(Hexagon::A2_tfrsi), SplatV)
        .add(MI.getOperand(1));
      Register OutV = MI.getOperand(0).getReg();
      BuildMI(MB, At, DL, TII.get(Hexagon::V6_lvsplatb), OutV)
        .addReg(SplatV);
    } else {
      // SplatV = A2_tfrsi #imm:#imm:#imm:#imm
      // OutV = V6_lvsplatw SplatV
      Register SplatV = MRI.createVirtualRegister(&Hexagon::IntRegsRegClass);
      const MachineOperand &InpOp = MI.getOperand(1);
      assert(InpOp.isImm());
      uint32_t V = InpOp.getImm() & 0xFF;
      BuildMI(MB, At, DL, TII.get(Hexagon::A2_tfrsi), SplatV)
          .addImm(V << 24 | V << 16 | V << 8 | V);
      Register OutV = MI.getOperand(0).getReg();
      BuildMI(MB, At, DL, TII.get(Hexagon::V6_lvsplatw), OutV).addReg(SplatV);
    }
    MB.erase(At);
    break;
  case Hexagon::PS_vsplatrb:
    if (Subtarget.useHVXV62Ops()) {
      // OutV = V6_lvsplatb Inp
      Register OutV = MI.getOperand(0).getReg();
      BuildMI(MB, At, DL, TII.get(Hexagon::V6_lvsplatb), OutV)
        .add(MI.getOperand(1));
    } else {
      Register SplatV = MRI.createVirtualRegister(&Hexagon::IntRegsRegClass);
      const MachineOperand &InpOp = MI.getOperand(1);
      BuildMI(MB, At, DL, TII.get(Hexagon::S2_vsplatrb), SplatV)
          .addReg(InpOp.getReg(), 0, InpOp.getSubReg());
      Register OutV = MI.getOperand(0).getReg();
      BuildMI(MB, At, DL, TII.get(Hexagon::V6_lvsplatw), OutV)
          .addReg(SplatV);
    }
    MB.erase(At);
    break;
  case Hexagon::PS_vsplatih:
    if (Subtarget.useHVXV62Ops()) {
      // SplatV = A2_tfrsi #imm
      // OutV = V6_lvsplath SplatV
      Register SplatV = MRI.createVirtualRegister(&Hexagon::IntRegsRegClass);
      BuildMI(MB, At, DL, TII.get(Hexagon::A2_tfrsi), SplatV)
        .add(MI.getOperand(1));
      Register OutV = MI.getOperand(0).getReg();
      BuildMI(MB, At, DL, TII.get(Hexagon::V6_lvsplath), OutV)
        .addReg(SplatV);
    } else {
      // SplatV = A2_tfrsi #imm:#imm
      // OutV = V6_lvsplatw SplatV
      Register SplatV = MRI.createVirtualRegister(&Hexagon::IntRegsRegClass);
      const MachineOperand &InpOp = MI.getOperand(1);
      assert(InpOp.isImm());
      uint32_t V = InpOp.getImm() & 0xFFFF;
      BuildMI(MB, At, DL, TII.get(Hexagon::A2_tfrsi), SplatV)
          .addImm(V << 16 | V);
      Register OutV = MI.getOperand(0).getReg();
      BuildMI(MB, At, DL, TII.get(Hexagon::V6_lvsplatw), OutV).addReg(SplatV);
    }
    MB.erase(At);
    break;
  case Hexagon::PS_vsplatrh:
    if (Subtarget.useHVXV62Ops()) {
      // OutV = V6_lvsplath Inp
      Register OutV = MI.getOperand(0).getReg();
      BuildMI(MB, At, DL, TII.get(Hexagon::V6_lvsplath), OutV)
        .add(MI.getOperand(1));
    } else {
      // SplatV = A2_combine_ll Inp, Inp
      // OutV = V6_lvsplatw SplatV
      Register SplatV = MRI.createVirtualRegister(&Hexagon::IntRegsRegClass);
      const MachineOperand &InpOp = MI.getOperand(1);
      BuildMI(MB, At, DL, TII.get(Hexagon::A2_combine_ll), SplatV)
          .addReg(InpOp.getReg(), 0, InpOp.getSubReg())
          .addReg(InpOp.getReg(), 0, InpOp.getSubReg());
      Register OutV = MI.getOperand(0).getReg();
      BuildMI(MB, At, DL, TII.get(Hexagon::V6_lvsplatw), OutV).addReg(SplatV);
    }
    MB.erase(At);
    break;
  case Hexagon::PS_vsplatiw:
  case Hexagon::PS_vsplatrw:
    if (Opc == Hexagon::PS_vsplatiw) {
      // SplatV = A2_tfrsi #imm
      Register SplatV = MRI.createVirtualRegister(&Hexagon::IntRegsRegClass);
      BuildMI(MB, At, DL, TII.get(Hexagon::A2_tfrsi), SplatV)
        .add(MI.getOperand(1));
      MI.getOperand(1).ChangeToRegister(SplatV, false);
    }
    // OutV = V6_lvsplatw SplatV/Inp
    MI.setDesc(TII.get(Hexagon::V6_lvsplatw));
    break;
  }
}

SDValue
HexagonTargetLowering::convertToByteIndex(SDValue ElemIdx, MVT ElemTy,
                                          SelectionDAG &DAG) const {
  if (ElemIdx.getValueType().getSimpleVT() != MVT::i32)
    ElemIdx = DAG.getBitcast(MVT::i32, ElemIdx);

  unsigned ElemWidth = ElemTy.getSizeInBits();
  if (ElemWidth == 8)
    return ElemIdx;

  unsigned L = Log2_32(ElemWidth/8);
  const SDLoc &dl(ElemIdx);
  return DAG.getNode(ISD::SHL, dl, MVT::i32,
                     {ElemIdx, DAG.getConstant(L, dl, MVT::i32)});
}

SDValue
HexagonTargetLowering::getIndexInWord32(SDValue Idx, MVT ElemTy,
                                        SelectionDAG &DAG) const {
  unsigned ElemWidth = ElemTy.getSizeInBits();
  assert(ElemWidth >= 8 && ElemWidth <= 32);
  if (ElemWidth == 32)
    return Idx;

  if (ty(Idx) != MVT::i32)
    Idx = DAG.getBitcast(MVT::i32, Idx);
  const SDLoc &dl(Idx);
  SDValue Mask = DAG.getConstant(32/ElemWidth - 1, dl, MVT::i32);
  SDValue SubIdx = DAG.getNode(ISD::AND, dl, MVT::i32, {Idx, Mask});
  return SubIdx;
}

SDValue
HexagonTargetLowering::getByteShuffle(const SDLoc &dl, SDValue Op0,
                                      SDValue Op1, ArrayRef<int> Mask,
                                      SelectionDAG &DAG) const {
  MVT OpTy = ty(Op0);
  assert(OpTy == ty(Op1));

  MVT ElemTy = OpTy.getVectorElementType();
  if (ElemTy == MVT::i8)
    return DAG.getVectorShuffle(OpTy, dl, Op0, Op1, Mask);
  assert(ElemTy.getSizeInBits() >= 8);

  MVT ResTy = tyVector(OpTy, MVT::i8);
  unsigned ElemSize = ElemTy.getSizeInBits() / 8;

  SmallVector<int,128> ByteMask;
  for (int M : Mask) {
    if (M < 0) {
      for (unsigned I = 0; I != ElemSize; ++I)
        ByteMask.push_back(-1);
    } else {
      int NewM = M*ElemSize;
      for (unsigned I = 0; I != ElemSize; ++I)
        ByteMask.push_back(NewM+I);
    }
  }
  assert(ResTy.getVectorNumElements() == ByteMask.size());
  return DAG.getVectorShuffle(ResTy, dl, opCastElem(Op0, MVT::i8, DAG),
                              opCastElem(Op1, MVT::i8, DAG), ByteMask);
}

SDValue
HexagonTargetLowering::buildHvxVectorReg(ArrayRef<SDValue> Values,
                                         const SDLoc &dl, MVT VecTy,
                                         SelectionDAG &DAG) const {
  unsigned VecLen = Values.size();
  MachineFunction &MF = DAG.getMachineFunction();
  MVT ElemTy = VecTy.getVectorElementType();
  unsigned ElemWidth = ElemTy.getSizeInBits();
  unsigned HwLen = Subtarget.getVectorLength();

  unsigned ElemSize = ElemWidth / 8;
  assert(ElemSize*VecLen == HwLen);
  SmallVector<SDValue,32> Words;

  if (VecTy.getVectorElementType() != MVT::i32 &&
      !(Subtarget.useHVXFloatingPoint() &&
      VecTy.getVectorElementType() == MVT::f32)) {
    assert((ElemSize == 1 || ElemSize == 2) && "Invalid element size");
    unsigned OpsPerWord = (ElemSize == 1) ? 4 : 2;
    MVT PartVT = MVT::getVectorVT(VecTy.getVectorElementType(), OpsPerWord);
    for (unsigned i = 0; i != VecLen; i += OpsPerWord) {
      SDValue W = buildVector32(Values.slice(i, OpsPerWord), dl, PartVT, DAG);
      Words.push_back(DAG.getBitcast(MVT::i32, W));
    }
  } else {
    for (SDValue V : Values)
      Words.push_back(DAG.getBitcast(MVT::i32, V));
  }
  auto isSplat = [] (ArrayRef<SDValue> Values, SDValue &SplatV) {
    unsigned NumValues = Values.size();
    assert(NumValues > 0);
    bool IsUndef = true;
    for (unsigned i = 0; i != NumValues; ++i) {
      if (Values[i].isUndef())
        continue;
      IsUndef = false;
      if (!SplatV.getNode())
        SplatV = Values[i];
      else if (SplatV != Values[i])
        return false;
    }
    if (IsUndef)
      SplatV = Values[0];
    return true;
  };

  unsigned NumWords = Words.size();
  SDValue SplatV;
  bool IsSplat = isSplat(Words, SplatV);
  if (IsSplat && isUndef(SplatV))
    return DAG.getUNDEF(VecTy);
  if (IsSplat) {
    assert(SplatV.getNode());
    if (isNullConstant(SplatV))
      return getZero(dl, VecTy, DAG);
    MVT WordTy = MVT::getVectorVT(MVT::i32, HwLen/4);
    SDValue S = DAG.getNode(ISD::SPLAT_VECTOR, dl, WordTy, SplatV);
    return DAG.getBitcast(VecTy, S);
  }

  // Delay recognizing constant vectors until here, so that we can generate
  // a vsplat.
  SmallVector<ConstantInt*, 128> Consts(VecLen);
  bool AllConst = getBuildVectorConstInts(Values, VecTy, DAG, Consts);
  if (AllConst) {
    ArrayRef<Constant*> Tmp((Constant**)Consts.begin(),
                            (Constant**)Consts.end());
    Constant *CV = ConstantVector::get(Tmp);
    Align Alignment(HwLen);
    SDValue CP =
        LowerConstantPool(DAG.getConstantPool(CV, VecTy, Alignment), DAG);
    return DAG.getLoad(VecTy, dl, DAG.getEntryNode(), CP,
                       MachinePointerInfo::getConstantPool(MF), Alignment);
  }

  // A special case is a situation where the vector is built entirely from
  // elements extracted from another vector. This could be done via a shuffle
  // more efficiently, but typically, the size of the source vector will not
  // match the size of the vector being built (which precludes the use of a
  // shuffle directly).
  // This only handles a single source vector, and the vector being built
  // should be of a sub-vector type of the source vector type.
  auto IsBuildFromExtracts = [this,&Values] (SDValue &SrcVec,
                                             SmallVectorImpl<int> &SrcIdx) {
    SDValue Vec;
    for (SDValue V : Values) {
      if (isUndef(V)) {
        SrcIdx.push_back(-1);
        continue;
      }
      if (V.getOpcode() != ISD::EXTRACT_VECTOR_ELT)
        return false;
      // All extracts should come from the same vector.
      SDValue T = V.getOperand(0);
      if (Vec.getNode() != nullptr && T.getNode() != Vec.getNode())
        return false;
      Vec = T;
      ConstantSDNode *C = dyn_cast<ConstantSDNode>(V.getOperand(1));
      if (C == nullptr)
        return false;
      int I = C->getSExtValue();
      assert(I >= 0 && "Negative element index");
      SrcIdx.push_back(I);
    }
    SrcVec = Vec;
    return true;
  };

  SmallVector<int,128> ExtIdx;
  SDValue ExtVec;
  if (IsBuildFromExtracts(ExtVec, ExtIdx)) {
    MVT ExtTy = ty(ExtVec);
    unsigned ExtLen = ExtTy.getVectorNumElements();
    if (ExtLen == VecLen || ExtLen == 2*VecLen) {
      // Construct a new shuffle mask that will produce a vector with the same
      // number of elements as the input vector, and such that the vector we
      // want will be the initial subvector of it.
      SmallVector<int,128> Mask;
      BitVector Used(ExtLen);

      for (int M : ExtIdx) {
        Mask.push_back(M);
        if (M >= 0)
          Used.set(M);
      }
      // Fill the rest of the mask with the unused elements of ExtVec in hopes
      // that it will result in a permutation of ExtVec's elements. It's still
      // fine if it doesn't (e.g. if undefs are present, or elements are
      // repeated), but permutations can always be done efficiently via vdelta
      // and vrdelta.
      for (unsigned I = 0; I != ExtLen; ++I) {
        if (Mask.size() == ExtLen)
          break;
        if (!Used.test(I))
          Mask.push_back(I);
      }

      SDValue S = DAG.getVectorShuffle(ExtTy, dl, ExtVec,
                                       DAG.getUNDEF(ExtTy), Mask);
      return ExtLen == VecLen ? S : LoHalf(S, DAG);
    }
  }

  // Find most common element to initialize vector with. This is to avoid
  // unnecessary vinsert/valign for cases where the same value is present
  // many times. Creates a histogram of the vector's elements to find the
  // most common element n.
  assert(4*Words.size() == Subtarget.getVectorLength());
  int VecHist[32];
  int n = 0;
  for (unsigned i = 0; i != NumWords; ++i) {
    VecHist[i] = 0;
    if (Words[i].isUndef())
      continue;
    for (unsigned j = i; j != NumWords; ++j)
      if (Words[i] == Words[j])
        VecHist[i]++;

    if (VecHist[i] > VecHist[n])
      n = i;
  }

  SDValue HalfV = getZero(dl, VecTy, DAG);
  if (VecHist[n] > 1) {
    SDValue SplatV = DAG.getNode(ISD::SPLAT_VECTOR, dl, VecTy, Words[n]);
    HalfV = DAG.getNode(HexagonISD::VALIGN, dl, VecTy,
                       {HalfV, SplatV, DAG.getConstant(HwLen/2, dl, MVT::i32)});
  }
  SDValue HalfV0 = HalfV;
  SDValue HalfV1 = HalfV;

  // Construct two halves in parallel, then or them together. Rn and Rm count
  // number of rotations needed before the next element. One last rotation is
  // performed post-loop to position the last element.
  int Rn = 0, Rm = 0;
  SDValue Sn, Sm;
  SDValue N = HalfV0;
  SDValue M = HalfV1;
  for (unsigned i = 0; i != NumWords/2; ++i) {
    // Rotate by element count since last insertion.
    if (Words[i] != Words[n] || VecHist[n] <= 1) {
      Sn = DAG.getConstant(Rn, dl, MVT::i32);
      HalfV0 = DAG.getNode(HexagonISD::VROR, dl, VecTy, {N, Sn});
      N = DAG.getNode(HexagonISD::VINSERTW0, dl, VecTy,
                      {HalfV0, Words[i]});
      Rn = 0;
    }
    if (Words[i+NumWords/2] != Words[n] || VecHist[n] <= 1) {
      Sm = DAG.getConstant(Rm, dl, MVT::i32);
      HalfV1 = DAG.getNode(HexagonISD::VROR, dl, VecTy, {M, Sm});
      M = DAG.getNode(HexagonISD::VINSERTW0, dl, VecTy,
                      {HalfV1, Words[i+NumWords/2]});
      Rm = 0;
    }
    Rn += 4;
    Rm += 4;
  }
  // Perform last rotation.
  Sn = DAG.getConstant(Rn+HwLen/2, dl, MVT::i32);
  Sm = DAG.getConstant(Rm, dl, MVT::i32);
  HalfV0 = DAG.getNode(HexagonISD::VROR, dl, VecTy, {N, Sn});
  HalfV1 = DAG.getNode(HexagonISD::VROR, dl, VecTy, {M, Sm});

  SDValue T0 = DAG.getBitcast(tyVector(VecTy, MVT::i32), HalfV0);
  SDValue T1 = DAG.getBitcast(tyVector(VecTy, MVT::i32), HalfV1);

  SDValue DstV = DAG.getNode(ISD::OR, dl, ty(T0), {T0, T1});

  SDValue OutV =
      DAG.getBitcast(tyVector(ty(DstV), VecTy.getVectorElementType()), DstV);
  return OutV;
}

SDValue
HexagonTargetLowering::createHvxPrefixPred(SDValue PredV, const SDLoc &dl,
      unsigned BitBytes, bool ZeroFill, SelectionDAG &DAG) const {
  MVT PredTy = ty(PredV);
  unsigned HwLen = Subtarget.getVectorLength();
  MVT ByteTy = MVT::getVectorVT(MVT::i8, HwLen);

  if (Subtarget.isHVXVectorType(PredTy, true)) {
    // Move the vector predicate SubV to a vector register, and scale it
    // down to match the representation (bytes per type element) that VecV
    // uses. The scaling down will pick every 2nd or 4th (every Scale-th
    // in general) element and put them at the front of the resulting
    // vector. This subvector will then be inserted into the Q2V of VecV.
    // To avoid having an operation that generates an illegal type (short
    // vector), generate a full size vector.
    //
    SDValue T = DAG.getNode(HexagonISD::Q2V, dl, ByteTy, PredV);
    SmallVector<int,128> Mask(HwLen);
    // Scale = BitBytes(PredV) / Given BitBytes.
    unsigned Scale = HwLen / (PredTy.getVectorNumElements() * BitBytes);
    unsigned BlockLen = PredTy.getVectorNumElements() * BitBytes;

    for (unsigned i = 0; i != HwLen; ++i) {
      unsigned Num = i % Scale;
      unsigned Off = i / Scale;
      Mask[BlockLen*Num + Off] = i;
    }
    SDValue S = DAG.getVectorShuffle(ByteTy, dl, T, DAG.getUNDEF(ByteTy), Mask);
    if (!ZeroFill)
      return S;
    // Fill the bytes beyond BlockLen with 0s.
    // V6_pred_scalar2 cannot fill the entire predicate, so it only works
    // when BlockLen < HwLen.
    assert(BlockLen < HwLen && "vsetq(v1) prerequisite");
    MVT BoolTy = MVT::getVectorVT(MVT::i1, HwLen);
    SDValue Q = getInstr(Hexagon::V6_pred_scalar2, dl, BoolTy,
                         {DAG.getConstant(BlockLen, dl, MVT::i32)}, DAG);
    SDValue M = DAG.getNode(HexagonISD::Q2V, dl, ByteTy, Q);
    return DAG.getNode(ISD::AND, dl, ByteTy, S, M);
  }

  // Make sure that this is a valid scalar predicate.
  assert(PredTy == MVT::v2i1 || PredTy == MVT::v4i1 || PredTy == MVT::v8i1);

  unsigned Bytes = 8 / PredTy.getVectorNumElements();
  SmallVector<SDValue,4> Words[2];
  unsigned IdxW = 0;

  SDValue W0 = isUndef(PredV)
                  ? DAG.getUNDEF(MVT::i64)
                  : DAG.getNode(HexagonISD::P2D, dl, MVT::i64, PredV);
  Words[IdxW].push_back(HiHalf(W0, DAG));
  Words[IdxW].push_back(LoHalf(W0, DAG));

  while (Bytes < BitBytes) {
    IdxW ^= 1;
    Words[IdxW].clear();

    if (Bytes < 4) {
      for (const SDValue &W : Words[IdxW ^ 1]) {
        SDValue T = expandPredicate(W, dl, DAG);
        Words[IdxW].push_back(HiHalf(T, DAG));
        Words[IdxW].push_back(LoHalf(T, DAG));
      }
    } else {
      for (const SDValue &W : Words[IdxW ^ 1]) {
        Words[IdxW].push_back(W);
        Words[IdxW].push_back(W);
      }
    }
    Bytes *= 2;
  }

  assert(Bytes == BitBytes);

  SDValue Vec = ZeroFill ? getZero(dl, ByteTy, DAG) : DAG.getUNDEF(ByteTy);
  SDValue S4 = DAG.getConstant(HwLen-4, dl, MVT::i32);
  for (const SDValue &W : Words[IdxW]) {
    Vec = DAG.getNode(HexagonISD::VROR, dl, ByteTy, Vec, S4);
    Vec = DAG.getNode(HexagonISD::VINSERTW0, dl, ByteTy, Vec, W);
  }

  return Vec;
}

SDValue
HexagonTargetLowering::buildHvxVectorPred(ArrayRef<SDValue> Values,
                                          const SDLoc &dl, MVT VecTy,
                                          SelectionDAG &DAG) const {
  // Construct a vector V of bytes, such that a comparison V >u 0 would
  // produce the required vector predicate.
  unsigned VecLen = Values.size();
  unsigned HwLen = Subtarget.getVectorLength();
  assert(VecLen <= HwLen || VecLen == 8*HwLen);
  SmallVector<SDValue,128> Bytes;
  bool AllT = true, AllF = true;

  auto IsTrue = [] (SDValue V) {
    if (const auto *N = dyn_cast<ConstantSDNode>(V.getNode()))
      return !N->isZero();
    return false;
  };
  auto IsFalse = [] (SDValue V) {
    if (const auto *N = dyn_cast<ConstantSDNode>(V.getNode()))
      return N->isZero();
    return false;
  };

  if (VecLen <= HwLen) {
    // In the hardware, each bit of a vector predicate corresponds to a byte
    // of a vector register. Calculate how many bytes does a bit of VecTy
    // correspond to.
    assert(HwLen % VecLen == 0);
    unsigned BitBytes = HwLen / VecLen;
    for (SDValue V : Values) {
      AllT &= IsTrue(V);
      AllF &= IsFalse(V);

      SDValue Ext = !V.isUndef() ? DAG.getZExtOrTrunc(V, dl, MVT::i8)
                                 : DAG.getUNDEF(MVT::i8);
      for (unsigned B = 0; B != BitBytes; ++B)
        Bytes.push_back(Ext);
    }
  } else {
    // There are as many i1 values, as there are bits in a vector register.
    // Divide the values into groups of 8 and check that each group consists
    // of the same value (ignoring undefs).
    for (unsigned I = 0; I != VecLen; I += 8) {
      unsigned B = 0;
      // Find the first non-undef value in this group.
      for (; B != 8; ++B) {
        if (!Values[I+B].isUndef())
          break;
      }
      SDValue F = Values[I+B];
      AllT &= IsTrue(F);
      AllF &= IsFalse(F);

      SDValue Ext = (B < 8) ? DAG.getZExtOrTrunc(F, dl, MVT::i8)
                            : DAG.getUNDEF(MVT::i8);
      Bytes.push_back(Ext);
      // Verify that the rest of values in the group are the same as the
      // first.
      for (; B != 8; ++B)
        assert(Values[I+B].isUndef() || Values[I+B] == F);
    }
  }

  if (AllT)
    return DAG.getNode(HexagonISD::QTRUE, dl, VecTy);
  if (AllF)
    return DAG.getNode(HexagonISD::QFALSE, dl, VecTy);

  MVT ByteTy = MVT::getVectorVT(MVT::i8, HwLen);
  SDValue ByteVec = buildHvxVectorReg(Bytes, dl, ByteTy, DAG);
  return DAG.getNode(HexagonISD::V2Q, dl, VecTy, ByteVec);
}

SDValue
HexagonTargetLowering::extractHvxElementReg(SDValue VecV, SDValue IdxV,
      const SDLoc &dl, MVT ResTy, SelectionDAG &DAG) const {
  MVT ElemTy = ty(VecV).getVectorElementType();

  unsigned ElemWidth = ElemTy.getSizeInBits();
  assert(ElemWidth >= 8 && ElemWidth <= 32);
  (void)ElemWidth;

  SDValue ByteIdx = convertToByteIndex(IdxV, ElemTy, DAG);
  SDValue ExWord = DAG.getNode(HexagonISD::VEXTRACTW, dl, MVT::i32,
                               {VecV, ByteIdx});
  if (ElemTy == MVT::i32)
    return ExWord;

  // Have an extracted word, need to extract the smaller element out of it.
  // 1. Extract the bits of (the original) IdxV that correspond to the index
  //    of the desired element in the 32-bit word.
  SDValue SubIdx = getIndexInWord32(IdxV, ElemTy, DAG);
  // 2. Extract the element from the word.
  SDValue ExVec = DAG.getBitcast(tyVector(ty(ExWord), ElemTy), ExWord);
  return extractVector(ExVec, SubIdx, dl, ElemTy, MVT::i32, DAG);
}

SDValue
HexagonTargetLowering::extractHvxElementPred(SDValue VecV, SDValue IdxV,
      const SDLoc &dl, MVT ResTy, SelectionDAG &DAG) const {
  // Implement other return types if necessary.
  assert(ResTy == MVT::i1);

  unsigned HwLen = Subtarget.getVectorLength();
  MVT ByteTy = MVT::getVectorVT(MVT::i8, HwLen);
  SDValue ByteVec = DAG.getNode(HexagonISD::Q2V, dl, ByteTy, VecV);

  unsigned Scale = HwLen / ty(VecV).getVectorNumElements();
  SDValue ScV = DAG.getConstant(Scale, dl, MVT::i32);
  IdxV = DAG.getNode(ISD::MUL, dl, MVT::i32, IdxV, ScV);

  SDValue ExtB = extractHvxElementReg(ByteVec, IdxV, dl, MVT::i32, DAG);
  SDValue Zero = DAG.getTargetConstant(0, dl, MVT::i32);
  return getInstr(Hexagon::C2_cmpgtui, dl, MVT::i1, {ExtB, Zero}, DAG);
}

SDValue
HexagonTargetLowering::insertHvxElementReg(SDValue VecV, SDValue IdxV,
      SDValue ValV, const SDLoc &dl, SelectionDAG &DAG) const {
  MVT ElemTy = ty(VecV).getVectorElementType();

  unsigned ElemWidth = ElemTy.getSizeInBits();
  assert(ElemWidth >= 8 && ElemWidth <= 32);
  (void)ElemWidth;

  auto InsertWord = [&DAG,&dl,this] (SDValue VecV, SDValue ValV,
                                     SDValue ByteIdxV) {
    MVT VecTy = ty(VecV);
    unsigned HwLen = Subtarget.getVectorLength();
    SDValue MaskV = DAG.getNode(ISD::AND, dl, MVT::i32,
                                {ByteIdxV, DAG.getConstant(-4, dl, MVT::i32)});
    SDValue RotV = DAG.getNode(HexagonISD::VROR, dl, VecTy, {VecV, MaskV});
    SDValue InsV = DAG.getNode(HexagonISD::VINSERTW0, dl, VecTy, {RotV, ValV});
    SDValue SubV = DAG.getNode(ISD::SUB, dl, MVT::i32,
                               {DAG.getConstant(HwLen, dl, MVT::i32), MaskV});
    SDValue TorV = DAG.getNode(HexagonISD::VROR, dl, VecTy, {InsV, SubV});
    return TorV;
  };

  SDValue ByteIdx = convertToByteIndex(IdxV, ElemTy, DAG);
  if (ElemTy == MVT::i32)
    return InsertWord(VecV, ValV, ByteIdx);

  // If this is not inserting a 32-bit word, convert it into such a thing.
  // 1. Extract the existing word from the target vector.
  SDValue WordIdx = DAG.getNode(ISD::SRL, dl, MVT::i32,
                                {ByteIdx, DAG.getConstant(2, dl, MVT::i32)});
  SDValue Ext = extractHvxElementReg(opCastElem(VecV, MVT::i32, DAG), WordIdx,
                                     dl, MVT::i32, DAG);

  // 2. Treating the extracted word as a 32-bit vector, insert the given
  //    value into it.
  SDValue SubIdx = getIndexInWord32(IdxV, ElemTy, DAG);
  MVT SubVecTy = tyVector(ty(Ext), ElemTy);
  SDValue Ins = insertVector(DAG.getBitcast(SubVecTy, Ext),
                             ValV, SubIdx, dl, ElemTy, DAG);

  // 3. Insert the 32-bit word back into the original vector.
  return InsertWord(VecV, Ins, ByteIdx);
}

SDValue
HexagonTargetLowering::insertHvxElementPred(SDValue VecV, SDValue IdxV,
      SDValue ValV, const SDLoc &dl, SelectionDAG &DAG) const {
  unsigned HwLen = Subtarget.getVectorLength();
  MVT ByteTy = MVT::getVectorVT(MVT::i8, HwLen);
  SDValue ByteVec = DAG.getNode(HexagonISD::Q2V, dl, ByteTy, VecV);

  unsigned Scale = HwLen / ty(VecV).getVectorNumElements();
  SDValue ScV = DAG.getConstant(Scale, dl, MVT::i32);
  IdxV = DAG.getNode(ISD::MUL, dl, MVT::i32, IdxV, ScV);
  ValV = DAG.getNode(ISD::SIGN_EXTEND, dl, MVT::i32, ValV);

  SDValue InsV = insertHvxElementReg(ByteVec, IdxV, ValV, dl, DAG);
  return DAG.getNode(HexagonISD::V2Q, dl, ty(VecV), InsV);
}

SDValue
HexagonTargetLowering::extractHvxSubvectorReg(SDValue OrigOp, SDValue VecV,
      SDValue IdxV, const SDLoc &dl, MVT ResTy, SelectionDAG &DAG) const {
  MVT VecTy = ty(VecV);
  unsigned HwLen = Subtarget.getVectorLength();
  unsigned Idx = IdxV.getNode()->getAsZExtVal();
  MVT ElemTy = VecTy.getVectorElementType();
  unsigned ElemWidth = ElemTy.getSizeInBits();

  // If the source vector is a vector pair, get the single vector containing
  // the subvector of interest. The subvector will never overlap two single
  // vectors.
  if (isHvxPairTy(VecTy)) {
    if (Idx * ElemWidth >= 8*HwLen)
      Idx -= VecTy.getVectorNumElements() / 2;

    VecV = OrigOp;
    if (typeSplit(VecTy).first == ResTy)
      return VecV;
  }

  // The only meaningful subvectors of a single HVX vector are those that
  // fit in a scalar register.
  assert(ResTy.getSizeInBits() == 32 || ResTy.getSizeInBits() == 64);

  MVT WordTy = tyVector(VecTy, MVT::i32);
  SDValue WordVec = DAG.getBitcast(WordTy, VecV);
  unsigned WordIdx = (Idx*ElemWidth) / 32;

  SDValue W0Idx = DAG.getConstant(WordIdx, dl, MVT::i32);
  SDValue W0 = extractHvxElementReg(WordVec, W0Idx, dl, MVT::i32, DAG);
  if (ResTy.getSizeInBits() == 32)
    return DAG.getBitcast(ResTy, W0);

  SDValue W1Idx = DAG.getConstant(WordIdx+1, dl, MVT::i32);
  SDValue W1 = extractHvxElementReg(WordVec, W1Idx, dl, MVT::i32, DAG);
  SDValue WW = getCombine(W1, W0, dl, MVT::i64, DAG);
  return DAG.getBitcast(ResTy, WW);
}

SDValue
HexagonTargetLowering::extractHvxSubvectorPred(SDValue VecV, SDValue IdxV,
      const SDLoc &dl, MVT ResTy, SelectionDAG &DAG) const {
  MVT VecTy = ty(VecV);
  unsigned HwLen = Subtarget.getVectorLength();
  MVT ByteTy = MVT::getVectorVT(MVT::i8, HwLen);
  SDValue ByteVec = DAG.getNode(HexagonISD::Q2V, dl, ByteTy, VecV);
  // IdxV is required to be a constant.
  unsigned Idx = IdxV.getNode()->getAsZExtVal();

  unsigned ResLen = ResTy.getVectorNumElements();
  unsigned BitBytes = HwLen / VecTy.getVectorNumElements();
  unsigned Offset = Idx * BitBytes;
  SDValue Undef = DAG.getUNDEF(ByteTy);
  SmallVector<int,128> Mask;

  if (Subtarget.isHVXVectorType(ResTy, true)) {
    // Converting between two vector predicates. Since the result is shorter
    // than the source, it will correspond to a vector predicate with the
    // relevant bits replicated. The replication count is the ratio of the
    // source and target vector lengths.
    unsigned Rep = VecTy.getVectorNumElements() / ResLen;
    assert(isPowerOf2_32(Rep) && HwLen % Rep == 0);
    for (unsigned i = 0; i != HwLen/Rep; ++i) {
      for (unsigned j = 0; j != Rep; ++j)
        Mask.push_back(i + Offset);
    }
    SDValue ShuffV = DAG.getVectorShuffle(ByteTy, dl, ByteVec, Undef, Mask);
    return DAG.getNode(HexagonISD::V2Q, dl, ResTy, ShuffV);
  }

  // Converting between a vector predicate and a scalar predicate. In the
  // vector predicate, a group of BitBytes bits will correspond to a single
  // i1 element of the source vector type. Those bits will all have the same
  // value. The same will be true for ByteVec, where each byte corresponds
  // to a bit in the vector predicate.
  // The algorithm is to traverse the ByteVec, going over the i1 values from
  // the source vector, and generate the corresponding representation in an
  // 8-byte vector. To avoid repeated extracts from ByteVec, shuffle the
  // elements so that the interesting 8 bytes will be in the low end of the
  // vector.
  unsigned Rep = 8 / ResLen;
  // Make sure the output fill the entire vector register, so repeat the
  // 8-byte groups as many times as necessary.
  for (unsigned r = 0; r != HwLen/ResLen; ++r) {
    // This will generate the indexes of the 8 interesting bytes.
    for (unsigned i = 0; i != ResLen; ++i) {
      for (unsigned j = 0; j != Rep; ++j)
        Mask.push_back(Offset + i*BitBytes);
    }
  }

  SDValue Zero = getZero(dl, MVT::i32, DAG);
  SDValue ShuffV = DAG.getVectorShuffle(ByteTy, dl, ByteVec, Undef, Mask);
  // Combine the two low words from ShuffV into a v8i8, and byte-compare
  // them against 0.
  SDValue W0 = DAG.getNode(HexagonISD::VEXTRACTW, dl, MVT::i32, {ShuffV, Zero});
  SDValue W1 = DAG.getNode(HexagonISD::VEXTRACTW, dl, MVT::i32,
                           {ShuffV, DAG.getConstant(4, dl, MVT::i32)});
  SDValue Vec64 = getCombine(W1, W0, dl, MVT::v8i8, DAG);
  return getInstr(Hexagon::A4_vcmpbgtui, dl, ResTy,
                  {Vec64, DAG.getTargetConstant(0, dl, MVT::i32)}, DAG);
}

SDValue
HexagonTargetLowering::insertHvxSubvectorReg(SDValue VecV, SDValue SubV,
      SDValue IdxV, const SDLoc &dl, SelectionDAG &DAG) const {
  MVT VecTy = ty(VecV);
  MVT SubTy = ty(SubV);
  unsigned HwLen = Subtarget.getVectorLength();
  MVT ElemTy = VecTy.getVectorElementType();
  unsigned ElemWidth = ElemTy.getSizeInBits();

  bool IsPair = isHvxPairTy(VecTy);
  MVT SingleTy = MVT::getVectorVT(ElemTy, (8*HwLen)/ElemWidth);
  // The two single vectors that VecV consists of, if it's a pair.
  SDValue V0, V1;
  SDValue SingleV = VecV;
  SDValue PickHi;

  if (IsPair) {
    V0 = LoHalf(VecV, DAG);
    V1 = HiHalf(VecV, DAG);

    SDValue HalfV = DAG.getConstant(SingleTy.getVectorNumElements(),
                                    dl, MVT::i32);
    PickHi = DAG.getSetCC(dl, MVT::i1, IdxV, HalfV, ISD::SETUGT);
    if (isHvxSingleTy(SubTy)) {
      if (const auto *CN = dyn_cast<const ConstantSDNode>(IdxV.getNode())) {
        unsigned Idx = CN->getZExtValue();
        assert(Idx == 0 || Idx == VecTy.getVectorNumElements()/2);
        unsigned SubIdx = (Idx == 0) ? Hexagon::vsub_lo : Hexagon::vsub_hi;
        return DAG.getTargetInsertSubreg(SubIdx, dl, VecTy, VecV, SubV);
      }
      // If IdxV is not a constant, generate the two variants: with the
      // SubV as the high and as the low subregister, and select the right
      // pair based on the IdxV.
      SDValue InLo = DAG.getNode(ISD::CONCAT_VECTORS, dl, VecTy, {SubV, V1});
      SDValue InHi = DAG.getNode(ISD::CONCAT_VECTORS, dl, VecTy, {V0, SubV});
      return DAG.getNode(ISD::SELECT, dl, VecTy, PickHi, InHi, InLo);
    }
    // The subvector being inserted must be entirely contained in one of
    // the vectors V0 or V1. Set SingleV to the correct one, and update
    // IdxV to be the index relative to the beginning of that vector.
    SDValue S = DAG.getNode(ISD::SUB, dl, MVT::i32, IdxV, HalfV);
    IdxV = DAG.getNode(ISD::SELECT, dl, MVT::i32, PickHi, S, IdxV);
    SingleV = DAG.getNode(ISD::SELECT, dl, SingleTy, PickHi, V1, V0);
  }

  // The only meaningful subvectors of a single HVX vector are those that
  // fit in a scalar register.
  assert(SubTy.getSizeInBits() == 32 || SubTy.getSizeInBits() == 64);
  // Convert IdxV to be index in bytes.
  auto *IdxN = dyn_cast<ConstantSDNode>(IdxV.getNode());
  if (!IdxN || !IdxN->isZero()) {
    IdxV = DAG.getNode(ISD::MUL, dl, MVT::i32, IdxV,
                       DAG.getConstant(ElemWidth/8, dl, MVT::i32));
    SingleV = DAG.getNode(HexagonISD::VROR, dl, SingleTy, SingleV, IdxV);
  }
  // When inserting a single word, the rotation back to the original position
  // would be by HwLen-Idx, but if two words are inserted, it will need to be
  // by (HwLen-4)-Idx.
  unsigned RolBase = HwLen;
  if (SubTy.getSizeInBits() == 32) {
    SDValue V = DAG.getBitcast(MVT::i32, SubV);
    SingleV = DAG.getNode(HexagonISD::VINSERTW0, dl, SingleTy, SingleV, V);
  } else {
    SDValue V = DAG.getBitcast(MVT::i64, SubV);
    SDValue R0 = LoHalf(V, DAG);
    SDValue R1 = HiHalf(V, DAG);
    SingleV = DAG.getNode(HexagonISD::VINSERTW0, dl, SingleTy, SingleV, R0);
    SingleV = DAG.getNode(HexagonISD::VROR, dl, SingleTy, SingleV,
                          DAG.getConstant(4, dl, MVT::i32));
    SingleV = DAG.getNode(HexagonISD::VINSERTW0, dl, SingleTy, SingleV, R1);
    RolBase = HwLen-4;
  }
  // If the vector wasn't ror'ed, don't ror it back.
  if (RolBase != 4 || !IdxN || !IdxN->isZero()) {
    SDValue RolV = DAG.getNode(ISD::SUB, dl, MVT::i32,
                               DAG.getConstant(RolBase, dl, MVT::i32), IdxV);
    SingleV = DAG.getNode(HexagonISD::VROR, dl, SingleTy, SingleV, RolV);
  }

  if (IsPair) {
    SDValue InLo = DAG.getNode(ISD::CONCAT_VECTORS, dl, VecTy, {SingleV, V1});
    SDValue InHi = DAG.getNode(ISD::CONCAT_VECTORS, dl, VecTy, {V0, SingleV});
    return DAG.getNode(ISD::SELECT, dl, VecTy, PickHi, InHi, InLo);
  }
  return SingleV;
}

SDValue
HexagonTargetLowering::insertHvxSubvectorPred(SDValue VecV, SDValue SubV,
      SDValue IdxV, const SDLoc &dl, SelectionDAG &DAG) const {
  MVT VecTy = ty(VecV);
  MVT SubTy = ty(SubV);
  assert(Subtarget.isHVXVectorType(VecTy, true));
  // VecV is an HVX vector predicate. SubV may be either an HVX vector
  // predicate as well, or it can be a scalar predicate.

  unsigned VecLen = VecTy.getVectorNumElements();
  unsigned HwLen = Subtarget.getVectorLength();
  assert(HwLen % VecLen == 0 && "Unexpected vector type");

  unsigned Scale = VecLen / SubTy.getVectorNumElements();
  unsigned BitBytes = HwLen / VecLen;
  unsigned BlockLen = HwLen / Scale;

  MVT ByteTy = MVT::getVectorVT(MVT::i8, HwLen);
  SDValue ByteVec = DAG.getNode(HexagonISD::Q2V, dl, ByteTy, VecV);
  SDValue ByteSub = createHvxPrefixPred(SubV, dl, BitBytes, false, DAG);
  SDValue ByteIdx;

  auto *IdxN = dyn_cast<ConstantSDNode>(IdxV.getNode());
  if (!IdxN || !IdxN->isZero()) {
    ByteIdx = DAG.getNode(ISD::MUL, dl, MVT::i32, IdxV,
                          DAG.getConstant(BitBytes, dl, MVT::i32));
    ByteVec = DAG.getNode(HexagonISD::VROR, dl, ByteTy, ByteVec, ByteIdx);
  }

  // ByteVec is the target vector VecV rotated in such a way that the
  // subvector should be inserted at index 0. Generate a predicate mask
  // and use vmux to do the insertion.
  assert(BlockLen < HwLen && "vsetq(v1) prerequisite");
  MVT BoolTy = MVT::getVectorVT(MVT::i1, HwLen);
  SDValue Q = getInstr(Hexagon::V6_pred_scalar2, dl, BoolTy,
                       {DAG.getConstant(BlockLen, dl, MVT::i32)}, DAG);
  ByteVec = getInstr(Hexagon::V6_vmux, dl, ByteTy, {Q, ByteSub, ByteVec}, DAG);
  // Rotate ByteVec back, and convert to a vector predicate.
  if (!IdxN || !IdxN->isZero()) {
    SDValue HwLenV = DAG.getConstant(HwLen, dl, MVT::i32);
    SDValue ByteXdi = DAG.getNode(ISD::SUB, dl, MVT::i32, HwLenV, ByteIdx);
    ByteVec = DAG.getNode(HexagonISD::VROR, dl, ByteTy, ByteVec, ByteXdi);
  }
  return DAG.getNode(HexagonISD::V2Q, dl, VecTy, ByteVec);
}

SDValue
HexagonTargetLowering::extendHvxVectorPred(SDValue VecV, const SDLoc &dl,
      MVT ResTy, bool ZeroExt, SelectionDAG &DAG) const {
  // Sign- and any-extending of a vector predicate to a vector register is
  // equivalent to Q2V. For zero-extensions, generate a vmux between 0 and
  // a vector of 1s (where the 1s are of type matching the vector type).
  assert(Subtarget.isHVXVectorType(ResTy));
  if (!ZeroExt)
    return DAG.getNode(HexagonISD::Q2V, dl, ResTy, VecV);

  assert(ty(VecV).getVectorNumElements() == ResTy.getVectorNumElements());
  SDValue True = DAG.getNode(ISD::SPLAT_VECTOR, dl, ResTy,
                             DAG.getConstant(1, dl, MVT::i32));
  SDValue False = getZero(dl, ResTy, DAG);
  return DAG.getSelect(dl, ResTy, VecV, True, False);
}

SDValue
HexagonTargetLowering::compressHvxPred(SDValue VecQ, const SDLoc &dl,
      MVT ResTy, SelectionDAG &DAG) const {
  // Given a predicate register VecQ, transfer bits VecQ[0..HwLen-1]
  // (i.e. the entire predicate register) to bits [0..HwLen-1] of a
  // vector register. The remaining bits of the vector register are
  // unspecified.

  MachineFunction &MF = DAG.getMachineFunction();
  unsigned HwLen = Subtarget.getVectorLength();
  MVT ByteTy = MVT::getVectorVT(MVT::i8, HwLen);
  MVT PredTy = ty(VecQ);
  unsigned PredLen = PredTy.getVectorNumElements();
  assert(HwLen % PredLen == 0);
  MVT VecTy = MVT::getVectorVT(MVT::getIntegerVT(8*HwLen/PredLen), PredLen);

  Type *Int8Ty = Type::getInt8Ty(*DAG.getContext());
  SmallVector<Constant*, 128> Tmp;
  // Create an array of bytes (hex): 01,02,04,08,10,20,40,80, 01,02,04,08,...
  // These are bytes with the LSB rotated left with respect to their index.
  for (unsigned i = 0; i != HwLen/8; ++i) {
    for (unsigned j = 0; j != 8; ++j)
      Tmp.push_back(ConstantInt::get(Int8Ty, 1ull << j));
  }
  Constant *CV = ConstantVector::get(Tmp);
  Align Alignment(HwLen);
  SDValue CP =
      LowerConstantPool(DAG.getConstantPool(CV, ByteTy, Alignment), DAG);
  SDValue Bytes =
      DAG.getLoad(ByteTy, dl, DAG.getEntryNode(), CP,
                  MachinePointerInfo::getConstantPool(MF), Alignment);

  // Select the bytes that correspond to true bits in the vector predicate.
  SDValue Sel = DAG.getSelect(dl, VecTy, VecQ, DAG.getBitcast(VecTy, Bytes),
      getZero(dl, VecTy, DAG));
  // Calculate the OR of all bytes in each group of 8. That will compress
  // all the individual bits into a single byte.
  // First, OR groups of 4, via vrmpy with 0x01010101.
  SDValue All1 =
      DAG.getSplatBuildVector(MVT::v4i8, dl, DAG.getConstant(1, dl, MVT::i32));
  SDValue Vrmpy = getInstr(Hexagon::V6_vrmpyub, dl, ByteTy, {Sel, All1}, DAG);
  // Then rotate the accumulated vector by 4 bytes, and do the final OR.
  SDValue Rot = getInstr(Hexagon::V6_valignbi, dl, ByteTy,
      {Vrmpy, Vrmpy, DAG.getTargetConstant(4, dl, MVT::i32)}, DAG);
  SDValue Vor = DAG.getNode(ISD::OR, dl, ByteTy, {Vrmpy, Rot});

  // Pick every 8th byte and coalesce them at the beginning of the output.
  // For symmetry, coalesce every 1+8th byte after that, then every 2+8th
  // byte and so on.
  SmallVector<int,128> Mask;
  for (unsigned i = 0; i != HwLen; ++i)
    Mask.push_back((8*i) % HwLen + i/(HwLen/8));
  SDValue Collect =
      DAG.getVectorShuffle(ByteTy, dl, Vor, DAG.getUNDEF(ByteTy), Mask);
  return DAG.getBitcast(ResTy, Collect);
}

SDValue
HexagonTargetLowering::resizeToWidth(SDValue VecV, MVT ResTy, bool Signed,
                                     const SDLoc &dl, SelectionDAG &DAG) const {
  // Take a vector and resize the element type to match the given type.
  MVT InpTy = ty(VecV);
  if (InpTy == ResTy)
    return VecV;

  unsigned InpWidth = InpTy.getSizeInBits();
  unsigned ResWidth = ResTy.getSizeInBits();

  if (InpTy.isFloatingPoint()) {
    return InpWidth < ResWidth ? DAG.getNode(ISD::FP_EXTEND, dl, ResTy, VecV)
                               : DAG.getNode(ISD::FP_ROUND, dl, ResTy, VecV,
                                             getZero(dl, MVT::i32, DAG));
  }

  assert(InpTy.isInteger());

  if (InpWidth < ResWidth) {
    unsigned ExtOpc = Signed ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
    return DAG.getNode(ExtOpc, dl, ResTy, VecV);
  } else {
    unsigned NarOpc = Signed ? HexagonISD::SSAT : HexagonISD::USAT;
    return DAG.getNode(NarOpc, dl, ResTy, VecV, DAG.getValueType(ResTy));
  }
}

SDValue
HexagonTargetLowering::extractSubvector(SDValue Vec, MVT SubTy, unsigned SubIdx,
      SelectionDAG &DAG) const {
  assert(ty(Vec).getSizeInBits() % SubTy.getSizeInBits() == 0);

  const SDLoc &dl(Vec);
  unsigned ElemIdx = SubIdx * SubTy.getVectorNumElements();
  return DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, SubTy,
                     {Vec, DAG.getConstant(ElemIdx, dl, MVT::i32)});
}

SDValue
HexagonTargetLowering::LowerHvxBuildVector(SDValue Op, SelectionDAG &DAG)
      const {
  const SDLoc &dl(Op);
  MVT VecTy = ty(Op);

  unsigned Size = Op.getNumOperands();
  SmallVector<SDValue,128> Ops;
  for (unsigned i = 0; i != Size; ++i)
    Ops.push_back(Op.getOperand(i));

  // First, split the BUILD_VECTOR for vector pairs. We could generate
  // some pairs directly (via splat), but splats should be generated
  // by the combiner prior to getting here.
  if (VecTy.getSizeInBits() == 16*Subtarget.getVectorLength()) {
    ArrayRef<SDValue> A(Ops);
    MVT SingleTy = typeSplit(VecTy).first;
    SDValue V0 = buildHvxVectorReg(A.take_front(Size/2), dl, SingleTy, DAG);
    SDValue V1 = buildHvxVectorReg(A.drop_front(Size/2), dl, SingleTy, DAG);
    return DAG.getNode(ISD::CONCAT_VECTORS, dl, VecTy, V0, V1);
  }

  if (VecTy.getVectorElementType() == MVT::i1)
    return buildHvxVectorPred(Ops, dl, VecTy, DAG);

  // In case of MVT::f16 BUILD_VECTOR, since MVT::f16 is
  // not a legal type, just bitcast the node to use i16
  // types and bitcast the result back to f16
  if (VecTy.getVectorElementType() == MVT::f16) {
    SmallVector<SDValue,64> NewOps;
    for (unsigned i = 0; i != Size; i++)
      NewOps.push_back(DAG.getBitcast(MVT::i16, Ops[i]));

    SDValue T0 = DAG.getNode(ISD::BUILD_VECTOR, dl,
        tyVector(VecTy, MVT::i16), NewOps);
    return DAG.getBitcast(tyVector(VecTy, MVT::f16), T0);
  }

  return buildHvxVectorReg(Ops, dl, VecTy, DAG);
}

SDValue
HexagonTargetLowering::LowerHvxSplatVector(SDValue Op, SelectionDAG &DAG)
      const {
  const SDLoc &dl(Op);
  MVT VecTy = ty(Op);
  MVT ArgTy = ty(Op.getOperand(0));

  if (ArgTy == MVT::f16) {
    MVT SplatTy =  MVT::getVectorVT(MVT::i16, VecTy.getVectorNumElements());
    SDValue ToInt16 = DAG.getBitcast(MVT::i16, Op.getOperand(0));
    SDValue ToInt32 = DAG.getNode(ISD::ANY_EXTEND, dl, MVT::i32, ToInt16);
    SDValue Splat = DAG.getNode(ISD::SPLAT_VECTOR, dl, SplatTy, ToInt32);
    return DAG.getBitcast(VecTy, Splat);
  }

  return SDValue();
}

SDValue
HexagonTargetLowering::LowerHvxConcatVectors(SDValue Op, SelectionDAG &DAG)
      const {
  // Vector concatenation of two integer (non-bool) vectors does not need
  // special lowering. Custom-lower concats of bool vectors and expand
  // concats of more than 2 vectors.
  MVT VecTy = ty(Op);
  const SDLoc &dl(Op);
  unsigned NumOp = Op.getNumOperands();
  if (VecTy.getVectorElementType() != MVT::i1) {
    if (NumOp == 2)
      return Op;
    // Expand the other cases into a build-vector.
    SmallVector<SDValue,8> Elems;
    for (SDValue V : Op.getNode()->ops())
      DAG.ExtractVectorElements(V, Elems);
    // A vector of i16 will be broken up into a build_vector of i16's.
    // This is a problem, since at the time of operation legalization,
    // all operations are expected to be type-legalized, and i16 is not
    // a legal type. If any of the extracted elements is not of a valid
    // type, sign-extend it to a valid one.
    for (unsigned i = 0, e = Elems.size(); i != e; ++i) {
      SDValue V = Elems[i];
      MVT Ty = ty(V);
      if (!isTypeLegal(Ty)) {
        MVT NTy = typeLegalize(Ty, DAG);
        if (V.getOpcode() == ISD::EXTRACT_VECTOR_ELT) {
          Elems[i] = DAG.getNode(ISD::SIGN_EXTEND_INREG, dl, NTy,
                                 DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, NTy,
                                             V.getOperand(0), V.getOperand(1)),
                                 DAG.getValueType(Ty));
          continue;
        }
        // A few less complicated cases.
        switch (V.getOpcode()) {
          case ISD::Constant:
            Elems[i] = DAG.getSExtOrTrunc(V, dl, NTy);
            break;
          case ISD::UNDEF:
            Elems[i] = DAG.getUNDEF(NTy);
            break;
          case ISD::TRUNCATE:
            Elems[i] = V.getOperand(0);
            break;
          default:
            llvm_unreachable("Unexpected vector element");
        }
      }
    }
    return DAG.getBuildVector(VecTy, dl, Elems);
  }

  assert(VecTy.getVectorElementType() == MVT::i1);
  unsigned HwLen = Subtarget.getVectorLength();
  assert(isPowerOf2_32(NumOp) && HwLen % NumOp == 0);

  SDValue Op0 = Op.getOperand(0);

  // If the operands are HVX types (i.e. not scalar predicates), then
  // defer the concatenation, and create QCAT instead.
  if (Subtarget.isHVXVectorType(ty(Op0), true)) {
    if (NumOp == 2)
      return DAG.getNode(HexagonISD::QCAT, dl, VecTy, Op0, Op.getOperand(1));

    ArrayRef<SDUse> U(Op.getNode()->ops());
    SmallVector<SDValue,4> SV(U.begin(), U.end());
    ArrayRef<SDValue> Ops(SV);

    MVT HalfTy = typeSplit(VecTy).first;
    SDValue V0 = DAG.getNode(ISD::CONCAT_VECTORS, dl, HalfTy,
                             Ops.take_front(NumOp/2));
    SDValue V1 = DAG.getNode(ISD::CONCAT_VECTORS, dl, HalfTy,
                             Ops.take_back(NumOp/2));
    return DAG.getNode(HexagonISD::QCAT, dl, VecTy, V0, V1);
  }

  // Count how many bytes (in a vector register) each bit in VecTy
  // corresponds to.
  unsigned BitBytes = HwLen / VecTy.getVectorNumElements();

  SmallVector<SDValue,8> Prefixes;
  for (SDValue V : Op.getNode()->op_values()) {
    SDValue P = createHvxPrefixPred(V, dl, BitBytes, true, DAG);
    Prefixes.push_back(P);
  }

  unsigned InpLen = ty(Op.getOperand(0)).getVectorNumElements();
  MVT ByteTy = MVT::getVectorVT(MVT::i8, HwLen);
  SDValue S = DAG.getConstant(InpLen*BitBytes, dl, MVT::i32);
  SDValue Res = getZero(dl, ByteTy, DAG);
  for (unsigned i = 0, e = Prefixes.size(); i != e; ++i) {
    Res = DAG.getNode(HexagonISD::VROR, dl, ByteTy, Res, S);
    Res = DAG.getNode(ISD::OR, dl, ByteTy, Res, Prefixes[e-i-1]);
  }
  return DAG.getNode(HexagonISD::V2Q, dl, VecTy, Res);
}

SDValue
HexagonTargetLowering::LowerHvxExtractElement(SDValue Op, SelectionDAG &DAG)
      const {
  // Change the type of the extracted element to i32.
  SDValue VecV = Op.getOperand(0);
  MVT ElemTy = ty(VecV).getVectorElementType();
  const SDLoc &dl(Op);
  SDValue IdxV = Op.getOperand(1);
  if (ElemTy == MVT::i1)
    return extractHvxElementPred(VecV, IdxV, dl, ty(Op), DAG);

  return extractHvxElementReg(VecV, IdxV, dl, ty(Op), DAG);
}

SDValue
HexagonTargetLowering::LowerHvxInsertElement(SDValue Op, SelectionDAG &DAG)
      const {
  const SDLoc &dl(Op);
  MVT VecTy = ty(Op);
  SDValue VecV = Op.getOperand(0);
  SDValue ValV = Op.getOperand(1);
  SDValue IdxV = Op.getOperand(2);
  MVT ElemTy = ty(VecV).getVectorElementType();
  if (ElemTy == MVT::i1)
    return insertHvxElementPred(VecV, IdxV, ValV, dl, DAG);

  if (ElemTy == MVT::f16) {
    SDValue T0 = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl,
        tyVector(VecTy, MVT::i16),
        DAG.getBitcast(tyVector(VecTy, MVT::i16), VecV),
        DAG.getBitcast(MVT::i16, ValV), IdxV);
    return DAG.getBitcast(tyVector(VecTy, MVT::f16), T0);
  }

  return insertHvxElementReg(VecV, IdxV, ValV, dl, DAG);
}

SDValue
HexagonTargetLowering::LowerHvxExtractSubvector(SDValue Op, SelectionDAG &DAG)
      const {
  SDValue SrcV = Op.getOperand(0);
  MVT SrcTy = ty(SrcV);
  MVT DstTy = ty(Op);
  SDValue IdxV = Op.getOperand(1);
  unsigned Idx = IdxV.getNode()->getAsZExtVal();
  assert(Idx % DstTy.getVectorNumElements() == 0);
  (void)Idx;
  const SDLoc &dl(Op);

  MVT ElemTy = SrcTy.getVectorElementType();
  if (ElemTy == MVT::i1)
    return extractHvxSubvectorPred(SrcV, IdxV, dl, DstTy, DAG);

  return extractHvxSubvectorReg(Op, SrcV, IdxV, dl, DstTy, DAG);
}

SDValue
HexagonTargetLowering::LowerHvxInsertSubvector(SDValue Op, SelectionDAG &DAG)
      const {
  // Idx does not need to be a constant.
  SDValue VecV = Op.getOperand(0);
  SDValue ValV = Op.getOperand(1);
  SDValue IdxV = Op.getOperand(2);

  const SDLoc &dl(Op);
  MVT VecTy = ty(VecV);
  MVT ElemTy = VecTy.getVectorElementType();
  if (ElemTy == MVT::i1)
    return insertHvxSubvectorPred(VecV, ValV, IdxV, dl, DAG);

  return insertHvxSubvectorReg(VecV, ValV, IdxV, dl, DAG);
}

SDValue
HexagonTargetLowering::LowerHvxAnyExt(SDValue Op, SelectionDAG &DAG) const {
  // Lower any-extends of boolean vectors to sign-extends, since they
  // translate directly to Q2V. Zero-extending could also be done equally
  // fast, but Q2V is used/recognized in more places.
  // For all other vectors, use zero-extend.
  MVT ResTy = ty(Op);
  SDValue InpV = Op.getOperand(0);
  MVT ElemTy = ty(InpV).getVectorElementType();
  if (ElemTy == MVT::i1 && Subtarget.isHVXVectorType(ResTy))
    return LowerHvxSignExt(Op, DAG);
  return DAG.getNode(ISD::ZERO_EXTEND, SDLoc(Op), ResTy, InpV);
}

SDValue
HexagonTargetLowering::LowerHvxSignExt(SDValue Op, SelectionDAG &DAG) const {
  MVT ResTy = ty(Op);
  SDValue InpV = Op.getOperand(0);
  MVT ElemTy = ty(InpV).getVectorElementType();
  if (ElemTy == MVT::i1 && Subtarget.isHVXVectorType(ResTy))
    return extendHvxVectorPred(InpV, SDLoc(Op), ty(Op), false, DAG);
  return Op;
}

SDValue
HexagonTargetLowering::LowerHvxZeroExt(SDValue Op, SelectionDAG &DAG) const {
  MVT ResTy = ty(Op);
  SDValue InpV = Op.getOperand(0);
  MVT ElemTy = ty(InpV).getVectorElementType();
  if (ElemTy == MVT::i1 && Subtarget.isHVXVectorType(ResTy))
    return extendHvxVectorPred(InpV, SDLoc(Op), ty(Op), true, DAG);
  return Op;
}

SDValue
HexagonTargetLowering::LowerHvxCttz(SDValue Op, SelectionDAG &DAG) const {
  // Lower vector CTTZ into a computation using CTLZ (Hacker's Delight):
  // cttz(x) = bitwidth(x) - ctlz(~x & (x-1))
  const SDLoc &dl(Op);
  MVT ResTy = ty(Op);
  SDValue InpV = Op.getOperand(0);
  assert(ResTy == ty(InpV));

  // Calculate the vectors of 1 and bitwidth(x).
  MVT ElemTy = ty(InpV).getVectorElementType();
  unsigned ElemWidth = ElemTy.getSizeInBits();

  SDValue Vec1 = DAG.getNode(ISD::SPLAT_VECTOR, dl, ResTy,
                             DAG.getConstant(1, dl, MVT::i32));
  SDValue VecW = DAG.getNode(ISD::SPLAT_VECTOR, dl, ResTy,
                             DAG.getConstant(ElemWidth, dl, MVT::i32));
  SDValue VecN1 = DAG.getNode(ISD::SPLAT_VECTOR, dl, ResTy,
                              DAG.getConstant(-1, dl, MVT::i32));

  // Do not use DAG.getNOT, because that would create BUILD_VECTOR with
  // a BITCAST. Here we can skip the BITCAST (so we don't have to handle
  // it separately in custom combine or selection).
  SDValue A = DAG.getNode(ISD::AND, dl, ResTy,
                          {DAG.getNode(ISD::XOR, dl, ResTy, {InpV, VecN1}),
                           DAG.getNode(ISD::SUB, dl, ResTy, {InpV, Vec1})});
  return DAG.getNode(ISD::SUB, dl, ResTy,
                     {VecW, DAG.getNode(ISD::CTLZ, dl, ResTy, A)});
}

SDValue
HexagonTargetLowering::LowerHvxMulh(SDValue Op, SelectionDAG &DAG) const {
  const SDLoc &dl(Op);
  MVT ResTy = ty(Op);
  assert(ResTy.getVectorElementType() == MVT::i32);

  SDValue Vs = Op.getOperand(0);
  SDValue Vt = Op.getOperand(1);

  SDVTList ResTys = DAG.getVTList(ResTy, ResTy);
  unsigned Opc = Op.getOpcode();

  // On HVX v62+ producing the full product is cheap, so legalize MULH to LOHI.
  if (Opc == ISD::MULHU)
    return DAG.getNode(HexagonISD::UMUL_LOHI, dl, ResTys, {Vs, Vt}).getValue(1);
  if (Opc == ISD::MULHS)
    return DAG.getNode(HexagonISD::SMUL_LOHI, dl, ResTys, {Vs, Vt}).getValue(1);

#ifndef NDEBUG
  Op.dump(&DAG);
#endif
  llvm_unreachable("Unexpected mulh operation");
}

SDValue
HexagonTargetLowering::LowerHvxMulLoHi(SDValue Op, SelectionDAG &DAG) const {
  const SDLoc &dl(Op);
  unsigned Opc = Op.getOpcode();
  SDValue Vu = Op.getOperand(0);
  SDValue Vv = Op.getOperand(1);

  // If the HI part is not used, convert it to a regular MUL.
  if (auto HiVal = Op.getValue(1); HiVal.use_empty()) {
    // Need to preserve the types and the number of values.
    SDValue Hi = DAG.getUNDEF(ty(HiVal));
    SDValue Lo = DAG.getNode(ISD::MUL, dl, ty(Op), {Vu, Vv});
    return DAG.getMergeValues({Lo, Hi}, dl);
  }

  bool SignedVu = Opc == HexagonISD::SMUL_LOHI;
  bool SignedVv = Opc == HexagonISD::SMUL_LOHI || Opc == HexagonISD::USMUL_LOHI;

  // Legal on HVX v62+, but lower it here because patterns can't handle multi-
  // valued nodes.
  if (Subtarget.useHVXV62Ops())
    return emitHvxMulLoHiV62(Vu, SignedVu, Vv, SignedVv, dl, DAG);

  if (Opc == HexagonISD::SMUL_LOHI) {
    // Direct MULHS expansion is cheaper than doing the whole SMUL_LOHI,
    // for other signedness LOHI is cheaper.
    if (auto LoVal = Op.getValue(0); LoVal.use_empty()) {
      SDValue Hi = emitHvxMulHsV60(Vu, Vv, dl, DAG);
      SDValue Lo = DAG.getUNDEF(ty(LoVal));
      return DAG.getMergeValues({Lo, Hi}, dl);
    }
  }

  return emitHvxMulLoHiV60(Vu, SignedVu, Vv, SignedVv, dl, DAG);
}

SDValue
HexagonTargetLowering::LowerHvxBitcast(SDValue Op, SelectionDAG &DAG) const {
  SDValue Val = Op.getOperand(0);
  MVT ResTy = ty(Op);
  MVT ValTy = ty(Val);
  const SDLoc &dl(Op);

  if (isHvxBoolTy(ValTy) && ResTy.isScalarInteger()) {
    unsigned HwLen = Subtarget.getVectorLength();
    MVT WordTy = MVT::getVectorVT(MVT::i32, HwLen/4);
    SDValue VQ = compressHvxPred(Val, dl, WordTy, DAG);
    unsigned BitWidth = ResTy.getSizeInBits();

    if (BitWidth < 64) {
      SDValue W0 = extractHvxElementReg(VQ, DAG.getConstant(0, dl, MVT::i32),
          dl, MVT::i32, DAG);
      if (BitWidth == 32)
        return W0;
      assert(BitWidth < 32u);
      return DAG.getZExtOrTrunc(W0, dl, ResTy);
    }

    // The result is >= 64 bits. The only options are 64 or 128.
    assert(BitWidth == 64 || BitWidth == 128);
    SmallVector<SDValue,4> Words;
    for (unsigned i = 0; i != BitWidth/32; ++i) {
      SDValue W = extractHvxElementReg(
          VQ, DAG.getConstant(i, dl, MVT::i32), dl, MVT::i32, DAG);
      Words.push_back(W);
    }
    SmallVector<SDValue,2> Combines;
    assert(Words.size() % 2 == 0);
    for (unsigned i = 0, e = Words.size(); i < e; i += 2) {
      SDValue C = getCombine(Words[i+1], Words[i], dl, MVT::i64, DAG);
      Combines.push_back(C);
    }

    if (BitWidth == 64)
      return Combines[0];

    return DAG.getNode(ISD::BUILD_PAIR, dl, ResTy, Combines);
  }
  if (isHvxBoolTy(ResTy) && ValTy.isScalarInteger()) {
    // Handle bitcast from i128 -> v128i1 and i64 -> v64i1.
    unsigned BitWidth = ValTy.getSizeInBits();
    unsigned HwLen = Subtarget.getVectorLength();
    assert(BitWidth == HwLen);

    MVT ValAsVecTy = MVT::getVectorVT(MVT::i8, BitWidth / 8);
    SDValue ValAsVec = DAG.getBitcast(ValAsVecTy, Val);
    // Splat each byte of Val 8 times.
    // Bytes = [(b0)x8, (b1)x8, ...., (b15)x8]
    // where b0, b1,..., b15 are least to most significant bytes of I.
    SmallVector<SDValue, 128> Bytes;
    // Tmp: 0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80, 0x01,0x02,0x04,0x08,...
    // These are bytes with the LSB rotated left with respect to their index.
    SmallVector<SDValue, 128> Tmp;
    for (unsigned I = 0; I != HwLen / 8; ++I) {
      SDValue Idx = DAG.getConstant(I, dl, MVT::i32);
      SDValue Byte =
          DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::i8, ValAsVec, Idx);
      for (unsigned J = 0; J != 8; ++J) {
        Bytes.push_back(Byte);
        Tmp.push_back(DAG.getConstant(1ull << J, dl, MVT::i8));
      }
    }

    MVT ConstantVecTy = MVT::getVectorVT(MVT::i8, HwLen);
    SDValue ConstantVec = DAG.getBuildVector(ConstantVecTy, dl, Tmp);
    SDValue I2V = buildHvxVectorReg(Bytes, dl, ConstantVecTy, DAG);

    // Each Byte in the I2V will be set iff corresponding bit is set in Val.
    I2V = DAG.getNode(ISD::AND, dl, ConstantVecTy, {I2V, ConstantVec});
    return DAG.getNode(HexagonISD::V2Q, dl, ResTy, I2V);
  }

  return Op;
}

SDValue
HexagonTargetLowering::LowerHvxExtend(SDValue Op, SelectionDAG &DAG) const {
  // Sign- and zero-extends are legal.
  assert(Op.getOpcode() == ISD::ANY_EXTEND_VECTOR_INREG);
  return DAG.getNode(ISD::ZERO_EXTEND_VECTOR_INREG, SDLoc(Op), ty(Op),
                     Op.getOperand(0));
}

SDValue
HexagonTargetLowering::LowerHvxSelect(SDValue Op, SelectionDAG &DAG) const {
  MVT ResTy = ty(Op);
  if (ResTy.getVectorElementType() != MVT::i1)
    return Op;

  const SDLoc &dl(Op);
  unsigned HwLen = Subtarget.getVectorLength();
  unsigned VecLen = ResTy.getVectorNumElements();
  assert(HwLen % VecLen == 0);
  unsigned ElemSize = HwLen / VecLen;

  MVT VecTy = MVT::getVectorVT(MVT::getIntegerVT(ElemSize * 8), VecLen);
  SDValue S =
      DAG.getNode(ISD::SELECT, dl, VecTy, Op.getOperand(0),
                  DAG.getNode(HexagonISD::Q2V, dl, VecTy, Op.getOperand(1)),
                  DAG.getNode(HexagonISD::Q2V, dl, VecTy, Op.getOperand(2)));
  return DAG.getNode(HexagonISD::V2Q, dl, ResTy, S);
}

SDValue
HexagonTargetLowering::LowerHvxShift(SDValue Op, SelectionDAG &DAG) const {
  if (SDValue S = getVectorShiftByInt(Op, DAG))
    return S;
  return Op;
}

SDValue
HexagonTargetLowering::LowerHvxFunnelShift(SDValue Op,
                                           SelectionDAG &DAG) const {
  unsigned Opc = Op.getOpcode();
  assert(Opc == ISD::FSHL || Opc == ISD::FSHR);

  // Make sure the shift amount is within the range of the bitwidth
  // of the element type.
  SDValue A = Op.getOperand(0);
  SDValue B = Op.getOperand(1);
  SDValue S = Op.getOperand(2);

  MVT InpTy = ty(A);
  MVT ElemTy = InpTy.getVectorElementType();

  const SDLoc &dl(Op);
  unsigned ElemWidth = ElemTy.getSizeInBits();
  bool IsLeft = Opc == ISD::FSHL;

  // The expansion into regular shifts produces worse code for i8 and for
  // right shift of i32 on v65+.
  bool UseShifts = ElemTy != MVT::i8;
  if (Subtarget.useHVXV65Ops() && ElemTy == MVT::i32)
    UseShifts = false;

  if (SDValue SplatV = getSplatValue(S, DAG); SplatV && UseShifts) {
    // If this is a funnel shift by a scalar, lower it into regular shifts.
    SDValue Mask = DAG.getConstant(ElemWidth - 1, dl, MVT::i32);
    SDValue ModS =
        DAG.getNode(ISD::AND, dl, MVT::i32,
                    {DAG.getZExtOrTrunc(SplatV, dl, MVT::i32), Mask});
    SDValue NegS =
        DAG.getNode(ISD::SUB, dl, MVT::i32,
                    {DAG.getConstant(ElemWidth, dl, MVT::i32), ModS});
    SDValue IsZero =
        DAG.getSetCC(dl, MVT::i1, ModS, getZero(dl, MVT::i32, DAG), ISD::SETEQ);
    // FSHL A, B  =>  A <<  | B >>n
    // FSHR A, B  =>  A <<n | B >>
    SDValue Part1 =
        DAG.getNode(HexagonISD::VASL, dl, InpTy, {A, IsLeft ? ModS : NegS});
    SDValue Part2 =
        DAG.getNode(HexagonISD::VLSR, dl, InpTy, {B, IsLeft ? NegS : ModS});
    SDValue Or = DAG.getNode(ISD::OR, dl, InpTy, {Part1, Part2});
    // If the shift amount was 0, pick A or B, depending on the direction.
    // The opposite shift will also be by 0, so the "Or" will be incorrect.
    return DAG.getNode(ISD::SELECT, dl, InpTy, {IsZero, (IsLeft ? A : B), Or});
  }

  SDValue Mask = DAG.getSplatBuildVector(
      InpTy, dl, DAG.getConstant(ElemWidth - 1, dl, ElemTy));

  unsigned MOpc = Opc == ISD::FSHL ? HexagonISD::MFSHL : HexagonISD::MFSHR;
  return DAG.getNode(MOpc, dl, ty(Op),
                     {A, B, DAG.getNode(ISD::AND, dl, InpTy, {S, Mask})});
}

SDValue
HexagonTargetLowering::LowerHvxIntrinsic(SDValue Op, SelectionDAG &DAG) const {
  const SDLoc &dl(Op);
  unsigned IntNo = Op.getConstantOperandVal(0);
  SmallVector<SDValue> Ops(Op->ops().begin(), Op->ops().end());

  auto Swap = [&](SDValue P) {
    return DAG.getMergeValues({P.getValue(1), P.getValue(0)}, dl);
  };

  switch (IntNo) {
  case Intrinsic::hexagon_V6_pred_typecast:
  case Intrinsic::hexagon_V6_pred_typecast_128B: {
    MVT ResTy = ty(Op), InpTy = ty(Ops[1]);
    if (isHvxBoolTy(ResTy) && isHvxBoolTy(InpTy)) {
      if (ResTy == InpTy)
        return Ops[1];
      return DAG.getNode(HexagonISD::TYPECAST, dl, ResTy, Ops[1]);
    }
    break;
  }
  case Intrinsic::hexagon_V6_vmpyss_parts:
  case Intrinsic::hexagon_V6_vmpyss_parts_128B:
    return Swap(DAG.getNode(HexagonISD::SMUL_LOHI, dl, Op->getVTList(),
                            {Ops[1], Ops[2]}));
  case Intrinsic::hexagon_V6_vmpyuu_parts:
  case Intrinsic::hexagon_V6_vmpyuu_parts_128B:
    return Swap(DAG.getNode(HexagonISD::UMUL_LOHI, dl, Op->getVTList(),
                            {Ops[1], Ops[2]}));
  case Intrinsic::hexagon_V6_vmpyus_parts:
  case Intrinsic::hexagon_V6_vmpyus_parts_128B: {
    return Swap(DAG.getNode(HexagonISD::USMUL_LOHI, dl, Op->getVTList(),
                            {Ops[1], Ops[2]}));
  }
  } // switch

  return Op;
}

SDValue
HexagonTargetLowering::LowerHvxMaskedOp(SDValue Op, SelectionDAG &DAG) const {
  const SDLoc &dl(Op);
  unsigned HwLen = Subtarget.getVectorLength();
  MachineFunction &MF = DAG.getMachineFunction();
  auto *MaskN = cast<MaskedLoadStoreSDNode>(Op.getNode());
  SDValue Mask = MaskN->getMask();
  SDValue Chain = MaskN->getChain();
  SDValue Base = MaskN->getBasePtr();
  auto *MemOp = MF.getMachineMemOperand(MaskN->getMemOperand(), 0, HwLen);

  unsigned Opc = Op->getOpcode();
  assert(Opc == ISD::MLOAD || Opc == ISD::MSTORE);

  if (Opc == ISD::MLOAD) {
    MVT ValTy = ty(Op);
    SDValue Load = DAG.getLoad(ValTy, dl, Chain, Base, MemOp);
    SDValue Thru = cast<MaskedLoadSDNode>(MaskN)->getPassThru();
    if (isUndef(Thru))
      return Load;
    SDValue VSel = DAG.getNode(ISD::VSELECT, dl, ValTy, Mask, Load, Thru);
    return DAG.getMergeValues({VSel, Load.getValue(1)}, dl);
  }

  // MSTORE
  // HVX only has aligned masked stores.

  // TODO: Fold negations of the mask into the store.
  unsigned StoreOpc = Hexagon::V6_vS32b_qpred_ai;
  SDValue Value = cast<MaskedStoreSDNode>(MaskN)->getValue();
  SDValue Offset0 = DAG.getTargetConstant(0, dl, ty(Base));

  if (MaskN->getAlign().value() % HwLen == 0) {
    SDValue Store = getInstr(StoreOpc, dl, MVT::Other,
                             {Mask, Base, Offset0, Value, Chain}, DAG);
    DAG.setNodeMemRefs(cast<MachineSDNode>(Store.getNode()), {MemOp});
    return Store;
  }

  // Unaligned case.
  auto StoreAlign = [&](SDValue V, SDValue A) {
    SDValue Z = getZero(dl, ty(V), DAG);
    // TODO: use funnel shifts?
    // vlalign(Vu,Vv,Rt) rotates the pair Vu:Vv left by Rt and takes the
    // upper half.
    SDValue LoV = getInstr(Hexagon::V6_vlalignb, dl, ty(V), {V, Z, A}, DAG);
    SDValue HiV = getInstr(Hexagon::V6_vlalignb, dl, ty(V), {Z, V, A}, DAG);
    return std::make_pair(LoV, HiV);
  };

  MVT ByteTy = MVT::getVectorVT(MVT::i8, HwLen);
  MVT BoolTy = MVT::getVectorVT(MVT::i1, HwLen);
  SDValue MaskV = DAG.getNode(HexagonISD::Q2V, dl, ByteTy, Mask);
  VectorPair Tmp = StoreAlign(MaskV, Base);
  VectorPair MaskU = {DAG.getNode(HexagonISD::V2Q, dl, BoolTy, Tmp.first),
                      DAG.getNode(HexagonISD::V2Q, dl, BoolTy, Tmp.second)};
  VectorPair ValueU = StoreAlign(Value, Base);

  SDValue Offset1 = DAG.getTargetConstant(HwLen, dl, MVT::i32);
  SDValue StoreLo =
      getInstr(StoreOpc, dl, MVT::Other,
               {MaskU.first, Base, Offset0, ValueU.first, Chain}, DAG);
  SDValue StoreHi =
      getInstr(StoreOpc, dl, MVT::Other,
               {MaskU.second, Base, Offset1, ValueU.second, Chain}, DAG);
  DAG.setNodeMemRefs(cast<MachineSDNode>(StoreLo.getNode()), {MemOp});
  DAG.setNodeMemRefs(cast<MachineSDNode>(StoreHi.getNode()), {MemOp});
  return DAG.getNode(ISD::TokenFactor, dl, MVT::Other, {StoreLo, StoreHi});
}

SDValue HexagonTargetLowering::LowerHvxFpExtend(SDValue Op,
                                                SelectionDAG &DAG) const {
  // This conversion only applies to QFloat. IEEE extension from f16 to f32
  // is legal (done via a pattern).
  assert(Subtarget.useHVXQFloatOps());

  assert(Op->getOpcode() == ISD::FP_EXTEND);

  MVT VecTy = ty(Op);
  MVT ArgTy = ty(Op.getOperand(0));
  const SDLoc &dl(Op);
  assert(VecTy == MVT::v64f32 && ArgTy == MVT::v64f16);

  SDValue F16Vec = Op.getOperand(0);

  APFloat FloatVal = APFloat(1.0f);
  bool Ignored;
  FloatVal.convert(APFloat::IEEEhalf(), APFloat::rmNearestTiesToEven, &Ignored);
  SDValue Fp16Ones = DAG.getConstantFP(FloatVal, dl, ArgTy);
  SDValue VmpyVec =
      getInstr(Hexagon::V6_vmpy_qf32_hf, dl, VecTy, {F16Vec, Fp16Ones}, DAG);

  MVT HalfTy = typeSplit(VecTy).first;
  VectorPair Pair = opSplit(VmpyVec, dl, DAG);
  SDValue LoVec =
      getInstr(Hexagon::V6_vconv_sf_qf32, dl, HalfTy, {Pair.first}, DAG);
  SDValue HiVec =
      getInstr(Hexagon::V6_vconv_sf_qf32, dl, HalfTy, {Pair.second}, DAG);

  SDValue ShuffVec =
      getInstr(Hexagon::V6_vshuffvdd, dl, VecTy,
               {HiVec, LoVec, DAG.getConstant(-4, dl, MVT::i32)}, DAG);

  return ShuffVec;
}

SDValue
HexagonTargetLowering::LowerHvxFpToInt(SDValue Op, SelectionDAG &DAG) const {
  // Catch invalid conversion ops (just in case).
  assert(Op.getOpcode() == ISD::FP_TO_SINT ||
         Op.getOpcode() == ISD::FP_TO_UINT);

  MVT ResTy = ty(Op);
  MVT FpTy = ty(Op.getOperand(0)).getVectorElementType();
  MVT IntTy = ResTy.getVectorElementType();

  if (Subtarget.useHVXIEEEFPOps()) {
    // There are only conversions from f16.
    if (FpTy == MVT::f16) {
      // Other int types aren't legal in HVX, so we shouldn't see them here.
      assert(IntTy == MVT::i8 || IntTy == MVT::i16 || IntTy == MVT::i32);
      // Conversions to i8 and i16 are legal.
      if (IntTy == MVT::i8 || IntTy == MVT::i16)
        return Op;
    }
  }

  if (IntTy.getSizeInBits() != FpTy.getSizeInBits())
    return EqualizeFpIntConversion(Op, DAG);

  return ExpandHvxFpToInt(Op, DAG);
}

SDValue
HexagonTargetLowering::LowerHvxIntToFp(SDValue Op, SelectionDAG &DAG) const {
  // Catch invalid conversion ops (just in case).
  assert(Op.getOpcode() == ISD::SINT_TO_FP ||
         Op.getOpcode() == ISD::UINT_TO_FP);

  MVT ResTy = ty(Op);
  MVT IntTy = ty(Op.getOperand(0)).getVectorElementType();
  MVT FpTy = ResTy.getVectorElementType();

  if (Subtarget.useHVXIEEEFPOps()) {
    // There are only conversions to f16.
    if (FpTy == MVT::f16) {
      // Other int types aren't legal in HVX, so we shouldn't see them here.
      assert(IntTy == MVT::i8 || IntTy == MVT::i16 || IntTy == MVT::i32);
      // i8, i16 -> f16 is legal.
      if (IntTy == MVT::i8 || IntTy == MVT::i16)
        return Op;
    }
  }

  if (IntTy.getSizeInBits() != FpTy.getSizeInBits())
    return EqualizeFpIntConversion(Op, DAG);

  return ExpandHvxIntToFp(Op, DAG);
}

HexagonTargetLowering::TypePair
HexagonTargetLowering::typeExtendToWider(MVT Ty0, MVT Ty1) const {
  // Compare the widths of elements of the two types, and extend the narrower
  // type to match the with of the wider type. For vector types, apply this
  // to the element type.
  assert(Ty0.isVector() == Ty1.isVector());

  MVT ElemTy0 = Ty0.getScalarType();
  MVT ElemTy1 = Ty1.getScalarType();

  unsigned Width0 = ElemTy0.getSizeInBits();
  unsigned Width1 = ElemTy1.getSizeInBits();
  unsigned MaxWidth = std::max(Width0, Width1);

  auto getScalarWithWidth = [](MVT ScalarTy, unsigned Width) {
    if (ScalarTy.isInteger())
      return MVT::getIntegerVT(Width);
    assert(ScalarTy.isFloatingPoint());
    return MVT::getFloatingPointVT(Width);
  };

  MVT WideETy0 = getScalarWithWidth(ElemTy0, MaxWidth);
  MVT WideETy1 = getScalarWithWidth(ElemTy1, MaxWidth);

  if (!Ty0.isVector()) {
    // Both types are scalars.
    return {WideETy0, WideETy1};
  }

  // Vector types.
  unsigned NumElem = Ty0.getVectorNumElements();
  assert(NumElem == Ty1.getVectorNumElements());

  return {MVT::getVectorVT(WideETy0, NumElem),
          MVT::getVectorVT(WideETy1, NumElem)};
}

HexagonTargetLowering::TypePair
HexagonTargetLowering::typeWidenToWider(MVT Ty0, MVT Ty1) const {
  // Compare the numbers of elements of two vector types, and widen the
  // narrower one to match the number of elements in the wider one.
  assert(Ty0.isVector() && Ty1.isVector());

  unsigned Len0 = Ty0.getVectorNumElements();
  unsigned Len1 = Ty1.getVectorNumElements();
  if (Len0 == Len1)
    return {Ty0, Ty1};

  unsigned MaxLen = std::max(Len0, Len1);
  return {MVT::getVectorVT(Ty0.getVectorElementType(), MaxLen),
          MVT::getVectorVT(Ty1.getVectorElementType(), MaxLen)};
}

MVT
HexagonTargetLowering::typeLegalize(MVT Ty, SelectionDAG &DAG) const {
  EVT LegalTy = getTypeToTransformTo(*DAG.getContext(), Ty);
  assert(LegalTy.isSimple());
  return LegalTy.getSimpleVT();
}

MVT
HexagonTargetLowering::typeWidenToHvx(MVT Ty) const {
  unsigned HwWidth = 8 * Subtarget.getVectorLength();
  assert(Ty.getSizeInBits() <= HwWidth);
  if (Ty.getSizeInBits() == HwWidth)
    return Ty;

  MVT ElemTy = Ty.getScalarType();
  return MVT::getVectorVT(ElemTy, HwWidth / ElemTy.getSizeInBits());
}

HexagonTargetLowering::VectorPair
HexagonTargetLowering::emitHvxAddWithOverflow(SDValue A, SDValue B,
      const SDLoc &dl, bool Signed, SelectionDAG &DAG) const {
  // Compute A+B, return {A+B, O}, where O = vector predicate indicating
  // whether an overflow has occured.
  MVT ResTy = ty(A);
  assert(ResTy == ty(B));
  MVT PredTy = MVT::getVectorVT(MVT::i1, ResTy.getVectorNumElements());

  if (!Signed) {
    // V62+ has V6_vaddcarry, but it requires input predicate, so it doesn't
    // save any instructions.
    SDValue Add = DAG.getNode(ISD::ADD, dl, ResTy, {A, B});
    SDValue Ovf = DAG.getSetCC(dl, PredTy, Add, A, ISD::SETULT);
    return {Add, Ovf};
  }

  // Signed overflow has happened, if:
  // (A, B have the same sign) and (A+B has a different sign from either)
  // i.e. (~A xor B) & ((A+B) xor B), then check the sign bit
  SDValue Add = DAG.getNode(ISD::ADD, dl, ResTy, {A, B});
  SDValue NotA =
      DAG.getNode(ISD::XOR, dl, ResTy, {A, DAG.getConstant(-1, dl, ResTy)});
  SDValue Xor0 = DAG.getNode(ISD::XOR, dl, ResTy, {NotA, B});
  SDValue Xor1 = DAG.getNode(ISD::XOR, dl, ResTy, {Add, B});
  SDValue And = DAG.getNode(ISD::AND, dl, ResTy, {Xor0, Xor1});
  SDValue MSB =
      DAG.getSetCC(dl, PredTy, And, getZero(dl, ResTy, DAG), ISD::SETLT);
  return {Add, MSB};
}

HexagonTargetLowering::VectorPair
HexagonTargetLowering::emitHvxShiftRightRnd(SDValue Val, unsigned Amt,
      bool Signed, SelectionDAG &DAG) const {
  // Shift Val right by Amt bits, round the result to the nearest integer,
  // tie-break by rounding halves to even integer.

  const SDLoc &dl(Val);
  MVT ValTy = ty(Val);

  // This should also work for signed integers.
  //
  //   uint tmp0 = inp + ((1 << (Amt-1)) - 1);
  //   bool ovf = (inp > tmp0);
  //   uint rup = inp & (1 << (Amt+1));
  //
  //   uint tmp1 = inp >> (Amt-1);    // tmp1 == tmp2 iff
  //   uint tmp2 = tmp0 >> (Amt-1);   // the Amt-1 lower bits were all 0
  //   uint tmp3 = tmp2 + rup;
  //   uint frac = (tmp1 != tmp2) ? tmp2 >> 1 : tmp3 >> 1;
  unsigned ElemWidth = ValTy.getVectorElementType().getSizeInBits();
  MVT ElemTy = MVT::getIntegerVT(ElemWidth);
  MVT IntTy = tyVector(ValTy, ElemTy);
  MVT PredTy = MVT::getVectorVT(MVT::i1, IntTy.getVectorNumElements());
  unsigned ShRight = Signed ? ISD::SRA : ISD::SRL;

  SDValue Inp = DAG.getBitcast(IntTy, Val);
  SDValue LowBits = DAG.getConstant((1ull << (Amt - 1)) - 1, dl, IntTy);

  SDValue AmtP1 = DAG.getConstant(1ull << Amt, dl, IntTy);
  SDValue And = DAG.getNode(ISD::AND, dl, IntTy, {Inp, AmtP1});
  SDValue Zero = getZero(dl, IntTy, DAG);
  SDValue Bit = DAG.getSetCC(dl, PredTy, And, Zero, ISD::SETNE);
  SDValue Rup = DAG.getZExtOrTrunc(Bit, dl, IntTy);
  auto [Tmp0, Ovf] = emitHvxAddWithOverflow(Inp, LowBits, dl, Signed, DAG);

  SDValue AmtM1 = DAG.getConstant(Amt - 1, dl, IntTy);
  SDValue Tmp1 = DAG.getNode(ShRight, dl, IntTy, Inp, AmtM1);
  SDValue Tmp2 = DAG.getNode(ShRight, dl, IntTy, Tmp0, AmtM1);
  SDValue Tmp3 = DAG.getNode(ISD::ADD, dl, IntTy, Tmp2, Rup);

  SDValue Eq = DAG.getSetCC(dl, PredTy, Tmp1, Tmp2, ISD::SETEQ);
  SDValue One = DAG.getConstant(1, dl, IntTy);
  SDValue Tmp4 = DAG.getNode(ShRight, dl, IntTy, {Tmp2, One});
  SDValue Tmp5 = DAG.getNode(ShRight, dl, IntTy, {Tmp3, One});
  SDValue Mux = DAG.getNode(ISD::VSELECT, dl, IntTy, {Eq, Tmp5, Tmp4});
  return {Mux, Ovf};
}

SDValue
HexagonTargetLowering::emitHvxMulHsV60(SDValue A, SDValue B, const SDLoc &dl,
                                       SelectionDAG &DAG) const {
  MVT VecTy = ty(A);
  MVT PairTy = typeJoin({VecTy, VecTy});
  assert(VecTy.getVectorElementType() == MVT::i32);

  SDValue S16 = DAG.getConstant(16, dl, MVT::i32);

  // mulhs(A,B) =
  //   = [(Hi(A)*2^16 + Lo(A)) *s (Hi(B)*2^16 + Lo(B))] >> 32
  //   = [Hi(A)*2^16 *s Hi(B)*2^16 + Hi(A) *su Lo(B)*2^16
  //      + Lo(A) *us (Hi(B)*2^16 + Lo(B))] >> 32
  //   = [Hi(A) *s Hi(B)*2^32 + Hi(A) *su Lo(B)*2^16 + Lo(A) *us B] >> 32
  // The low half of Lo(A)*Lo(B) will be discarded (it's not added to
  // anything, so it cannot produce any carry over to higher bits),
  // so everything in [] can be shifted by 16 without loss of precision.
  //   = [Hi(A) *s Hi(B)*2^16 + Hi(A)*su Lo(B) + Lo(A)*B >> 16] >> 16
  //   = [Hi(A) *s Hi(B)*2^16 + Hi(A)*su Lo(B) + V6_vmpyewuh(A,B)] >> 16
  // The final additions need to make sure to properly maintain any carry-
  // out bits.
  //
  //                Hi(B) Lo(B)
  //                Hi(A) Lo(A)
  //               --------------
  //                Lo(B)*Lo(A)  | T0 = V6_vmpyewuh(B,A) does this,
  //         Hi(B)*Lo(A)         |      + dropping the low 16 bits
  //         Hi(A)*Lo(B)   | T2
  //  Hi(B)*Hi(A)

  SDValue T0 = getInstr(Hexagon::V6_vmpyewuh, dl, VecTy, {B, A}, DAG);
  // T1 = get Hi(A) into low halves.
  SDValue T1 = getInstr(Hexagon::V6_vasrw, dl, VecTy, {A, S16}, DAG);
  // P0 = interleaved T1.h*B.uh (full precision product)
  SDValue P0 = getInstr(Hexagon::V6_vmpyhus, dl, PairTy, {T1, B}, DAG);
  // T2 = T1.even(h) * B.even(uh), i.e. Hi(A)*Lo(B)
  SDValue T2 = LoHalf(P0, DAG);
  // We need to add T0+T2, recording the carry-out, which will be 1<<16
  // added to the final sum.
  // P1 = interleaved even/odd 32-bit (unsigned) sums of 16-bit halves
  SDValue P1 = getInstr(Hexagon::V6_vadduhw, dl, PairTy, {T0, T2}, DAG);
  // P2 = interleaved even/odd 32-bit (signed) sums of 16-bit halves
  SDValue P2 = getInstr(Hexagon::V6_vaddhw, dl, PairTy, {T0, T2}, DAG);
  // T3 = full-precision(T0+T2) >> 16
  // The low halves are added-unsigned, the high ones are added-signed.
  SDValue T3 = getInstr(Hexagon::V6_vasrw_acc, dl, VecTy,
                        {HiHalf(P2, DAG), LoHalf(P1, DAG), S16}, DAG);
  SDValue T4 = getInstr(Hexagon::V6_vasrw, dl, VecTy, {B, S16}, DAG);
  // P3 = interleaved Hi(B)*Hi(A) (full precision),
  // which is now Lo(T1)*Lo(T4), so we want to keep the even product.
  SDValue P3 = getInstr(Hexagon::V6_vmpyhv, dl, PairTy, {T1, T4}, DAG);
  SDValue T5 = LoHalf(P3, DAG);
  // Add:
  SDValue T6 = DAG.getNode(ISD::ADD, dl, VecTy, {T3, T5});
  return T6;
}

SDValue
HexagonTargetLowering::emitHvxMulLoHiV60(SDValue A, bool SignedA, SDValue B,
                                         bool SignedB, const SDLoc &dl,
                                         SelectionDAG &DAG) const {
  MVT VecTy = ty(A);
  MVT PairTy = typeJoin({VecTy, VecTy});
  assert(VecTy.getVectorElementType() == MVT::i32);

  SDValue S16 = DAG.getConstant(16, dl, MVT::i32);

  if (SignedA && !SignedB) {
    // Make A:unsigned, B:signed.
    std::swap(A, B);
    std::swap(SignedA, SignedB);
  }

  // Do halfword-wise multiplications for unsigned*unsigned product, then
  // add corrections for signed and unsigned*signed.

  SDValue Lo, Hi;

  // P0:lo = (uu) products of low halves of A and B,
  // P0:hi = (uu) products of high halves.
  SDValue P0 = getInstr(Hexagon::V6_vmpyuhv, dl, PairTy, {A, B}, DAG);

  // Swap low/high halves in B
  SDValue T0 = getInstr(Hexagon::V6_lvsplatw, dl, VecTy,
                        {DAG.getConstant(0x02020202, dl, MVT::i32)}, DAG);
  SDValue T1 = getInstr(Hexagon::V6_vdelta, dl, VecTy, {B, T0}, DAG);
  // P1 = products of even/odd halfwords.
  // P1:lo = (uu) products of even(A.uh) * odd(B.uh)
  // P1:hi = (uu) products of odd(A.uh) * even(B.uh)
  SDValue P1 = getInstr(Hexagon::V6_vmpyuhv, dl, PairTy, {A, T1}, DAG);

  // P2:lo = low halves of P1:lo + P1:hi,
  // P2:hi = high halves of P1:lo + P1:hi.
  SDValue P2 = getInstr(Hexagon::V6_vadduhw, dl, PairTy,
                        {HiHalf(P1, DAG), LoHalf(P1, DAG)}, DAG);
  // Still need to add the high halves of P0:lo to P2:lo
  SDValue T2 =
      getInstr(Hexagon::V6_vlsrw, dl, VecTy, {LoHalf(P0, DAG), S16}, DAG);
  SDValue T3 = DAG.getNode(ISD::ADD, dl, VecTy, {LoHalf(P2, DAG), T2});

  // The high halves of T3 will contribute to the HI part of LOHI.
  SDValue T4 = getInstr(Hexagon::V6_vasrw_acc, dl, VecTy,
                        {HiHalf(P2, DAG), T3, S16}, DAG);

  // The low halves of P2 need to be added to high halves of the LO part.
  Lo = getInstr(Hexagon::V6_vaslw_acc, dl, VecTy,
                {LoHalf(P0, DAG), LoHalf(P2, DAG), S16}, DAG);
  Hi = DAG.getNode(ISD::ADD, dl, VecTy, {HiHalf(P0, DAG), T4});

  if (SignedA) {
    assert(SignedB && "Signed A and unsigned B should have been inverted");

    MVT PredTy = MVT::getVectorVT(MVT::i1, VecTy.getVectorNumElements());
    SDValue Zero = getZero(dl, VecTy, DAG);
    SDValue Q0 = DAG.getSetCC(dl, PredTy, A, Zero, ISD::SETLT);
    SDValue Q1 = DAG.getSetCC(dl, PredTy, B, Zero, ISD::SETLT);
    SDValue X0 = DAG.getNode(ISD::VSELECT, dl, VecTy, {Q0, B, Zero});
    SDValue X1 = getInstr(Hexagon::V6_vaddwq, dl, VecTy, {Q1, X0, A}, DAG);
    Hi = getInstr(Hexagon::V6_vsubw, dl, VecTy, {Hi, X1}, DAG);
  } else if (SignedB) {
    // Same correction as for mulhus:
    // mulhus(A.uw,B.w) = mulhu(A.uw,B.uw) - (A.w if B < 0)
    MVT PredTy = MVT::getVectorVT(MVT::i1, VecTy.getVectorNumElements());
    SDValue Zero = getZero(dl, VecTy, DAG);
    SDValue Q1 = DAG.getSetCC(dl, PredTy, B, Zero, ISD::SETLT);
    Hi = getInstr(Hexagon::V6_vsubwq, dl, VecTy, {Q1, Hi, A}, DAG);
  } else {
    assert(!SignedA && !SignedB);
  }

  return DAG.getMergeValues({Lo, Hi}, dl);
}

SDValue
HexagonTargetLowering::emitHvxMulLoHiV62(SDValue A, bool SignedA,
                                         SDValue B, bool SignedB,
                                         const SDLoc &dl,
                                         SelectionDAG &DAG) const {
  MVT VecTy = ty(A);
  MVT PairTy = typeJoin({VecTy, VecTy});
  assert(VecTy.getVectorElementType() == MVT::i32);

  if (SignedA && !SignedB) {
    // Make A:unsigned, B:signed.
    std::swap(A, B);
    std::swap(SignedA, SignedB);
  }

  // Do S*S first, then make corrections for U*S or U*U if needed.
  SDValue P0 = getInstr(Hexagon::V6_vmpyewuh_64, dl, PairTy, {A, B}, DAG);
  SDValue P1 =
      getInstr(Hexagon::V6_vmpyowh_64_acc, dl, PairTy, {P0, A, B}, DAG);
  SDValue Lo = LoHalf(P1, DAG);
  SDValue Hi = HiHalf(P1, DAG);

  if (!SignedB) {
    assert(!SignedA && "Signed A and unsigned B should have been inverted");
    SDValue Zero = getZero(dl, VecTy, DAG);
    MVT PredTy = MVT::getVectorVT(MVT::i1, VecTy.getVectorNumElements());

    // Mulhu(X, Y) = Mulhs(X, Y) + (X, if Y < 0) + (Y, if X < 0).
    // def: Pat<(VecI32 (mulhu HVI32:$A, HVI32:$B)),
    //          (V6_vaddw (HiHalf (Muls64O $A, $B)),
    //                    (V6_vaddwq (V6_vgtw (V6_vd0), $B),
    //                               (V6_vandvqv (V6_vgtw (V6_vd0), $A), $B),
    //                               $A))>;
    SDValue Q0 = DAG.getSetCC(dl, PredTy, A, Zero, ISD::SETLT);
    SDValue Q1 = DAG.getSetCC(dl, PredTy, B, Zero, ISD::SETLT);
    SDValue T0 = getInstr(Hexagon::V6_vandvqv, dl, VecTy, {Q0, B}, DAG);
    SDValue T1 = getInstr(Hexagon::V6_vaddwq, dl, VecTy, {Q1, T0, A}, DAG);
    Hi = getInstr(Hexagon::V6_vaddw, dl, VecTy, {Hi, T1}, DAG);
  } else if (!SignedA) {
    SDValue Zero = getZero(dl, VecTy, DAG);
    MVT PredTy = MVT::getVectorVT(MVT::i1, VecTy.getVectorNumElements());

    // Mulhus(unsigned X, signed Y) = Mulhs(X, Y) + (Y, if X < 0).
    // def: Pat<(VecI32 (HexagonMULHUS HVI32:$A, HVI32:$B)),
    //          (V6_vaddwq (V6_vgtw (V6_vd0), $A),
    //                     (HiHalf (Muls64O $A, $B)),
    //                     $B)>;
    SDValue Q0 = DAG.getSetCC(dl, PredTy, A, Zero, ISD::SETLT);
    Hi = getInstr(Hexagon::V6_vaddwq, dl, VecTy, {Q0, Hi, B}, DAG);
  }

  return DAG.getMergeValues({Lo, Hi}, dl);
}

SDValue
HexagonTargetLowering::EqualizeFpIntConversion(SDValue Op, SelectionDAG &DAG)
      const {
  // Rewrite conversion between integer and floating-point in such a way that
  // the integer type is extended/narrowed to match the bitwidth of the
  // floating-point type, combined with additional integer-integer extensions
  // or narrowings to match the original input/result types.
  // E.g.  f32 -> i8  ==>  f32 -> i32 -> i8
  //
  // The input/result types are not required to be legal, but if they are
  // legal, this function should not introduce illegal types.

  unsigned Opc = Op.getOpcode();
  assert(Opc == ISD::FP_TO_SINT || Opc == ISD::FP_TO_UINT ||
         Opc == ISD::SINT_TO_FP || Opc == ISD::UINT_TO_FP);

  SDValue Inp = Op.getOperand(0);
  MVT InpTy = ty(Inp);
  MVT ResTy = ty(Op);

  if (InpTy == ResTy)
    return Op;

  const SDLoc &dl(Op);
  bool Signed = Opc == ISD::FP_TO_SINT || Opc == ISD::SINT_TO_FP;

  auto [WInpTy, WResTy] = typeExtendToWider(InpTy, ResTy);
  SDValue WInp = resizeToWidth(Inp, WInpTy, Signed, dl, DAG);
  SDValue Conv = DAG.getNode(Opc, dl, WResTy, WInp);
  SDValue Res = resizeToWidth(Conv, ResTy, Signed, dl, DAG);
  return Res;
}

SDValue
HexagonTargetLowering::ExpandHvxFpToInt(SDValue Op, SelectionDAG &DAG) const {
  unsigned Opc = Op.getOpcode();
  assert(Opc == ISD::FP_TO_SINT || Opc == ISD::FP_TO_UINT);

  const SDLoc &dl(Op);
  SDValue Op0 = Op.getOperand(0);
  MVT InpTy = ty(Op0);
  MVT ResTy = ty(Op);
  assert(InpTy.changeTypeToInteger() == ResTy);

  // int32_t conv_f32_to_i32(uint32_t inp) {
  //   // s | exp8 | frac23
  //
  //   int neg = (int32_t)inp < 0;
  //
  //   // "expm1" is the actual exponent minus 1: instead of "bias", subtract
  //   // "bias+1". When the encoded exp is "all-1" (i.e. inf/nan), this will
  //   // produce a large positive "expm1", which will result in max u/int.
  //   // In all IEEE formats, bias is the largest positive number that can be
  //   // represented in bias-width bits (i.e. 011..1).
  //   int32_t expm1 = (inp << 1) - 0x80000000;
  //   expm1 >>= 24;
  //
  //   // Always insert the "implicit 1". Subnormal numbers will become 0
  //   // regardless.
  //   uint32_t frac = (inp << 8) | 0x80000000;
  //
  //   // "frac" is the fraction part represented as Q1.31. If it was
  //   // interpreted as uint32_t, it would be the fraction part multiplied
  //   // by 2^31.
  //
  //   // Calculate the amount of right shift, since shifting further to the
  //   // left would lose significant bits. Limit it to 32, because we want
  //   // shifts by 32+ to produce 0, whereas V6_vlsrwv treats the shift
  //   // amount as a 6-bit signed value (so 33 is same as -31, i.e. shift
  //   // left by 31). "rsh" can be negative.
  //   int32_t rsh = min(31 - (expm1 + 1), 32);
  //
  //   frac >>= rsh;   // rsh == 32 will produce 0
  //
  //   // Everything up to this point is the same for conversion to signed
  //   // unsigned integer.
  //
  //   if (neg)                 // Only for signed int
  //     frac = -frac;          //
  //   if (rsh <= 0 && neg)     //   bound = neg ? 0x80000000 : 0x7fffffff
  //     frac = 0x80000000;     //   frac = rsh <= 0 ? bound : frac
  //   if (rsh <= 0 && !neg)    //
  //     frac = 0x7fffffff;     //
  //
  //   if (neg)                 // Only for unsigned int
  //     frac = 0;              //
  //   if (rsh < 0 && !neg)     //   frac = rsh < 0 ? 0x7fffffff : frac;
  //     frac = 0x7fffffff;     //   frac = neg ? 0 : frac;
  //
  //   return frac;
  // }

  MVT PredTy = MVT::getVectorVT(MVT::i1, ResTy.getVectorElementCount());

  // Zero = V6_vd0();
  // Neg = V6_vgtw(Zero, Inp);
  // One = V6_lvsplatw(1);
  // M80 = V6_lvsplatw(0x80000000);
  // Exp00 = V6_vaslwv(Inp, One);
  // Exp01 = V6_vsubw(Exp00, M80);
  // ExpM1 = V6_vasrw(Exp01, 24);
  // Frc00 = V6_vaslw(Inp, 8);
  // Frc01 = V6_vor(Frc00, M80);
  // Rsh00 = V6_vsubw(V6_lvsplatw(30), ExpM1);
  // Rsh01 = V6_vminw(Rsh00, V6_lvsplatw(32));
  // Frc02 = V6_vlsrwv(Frc01, Rsh01);

  // if signed int:
  // Bnd = V6_vmux(Neg, M80, V6_lvsplatw(0x7fffffff))
  // Pos = V6_vgtw(Rsh01, Zero);
  // Frc13 = V6_vsubw(Zero, Frc02);
  // Frc14 = V6_vmux(Neg, Frc13, Frc02);
  // Int = V6_vmux(Pos, Frc14, Bnd);
  //
  // if unsigned int:
  // Rsn = V6_vgtw(Zero, Rsh01)
  // Frc23 = V6_vmux(Rsn, V6_lvsplatw(0x7fffffff), Frc02)
  // Int = V6_vmux(Neg, Zero, Frc23)

  auto [ExpWidth, ExpBias, FracWidth] = getIEEEProperties(InpTy);
  unsigned ElemWidth = 1 + ExpWidth + FracWidth;
  assert((1ull << (ExpWidth - 1)) == (1 + ExpBias));

  SDValue Inp = DAG.getBitcast(ResTy, Op0);
  SDValue Zero = getZero(dl, ResTy, DAG);
  SDValue Neg = DAG.getSetCC(dl, PredTy, Inp, Zero, ISD::SETLT);
  SDValue M80 = DAG.getConstant(1ull << (ElemWidth - 1), dl, ResTy);
  SDValue M7F = DAG.getConstant((1ull << (ElemWidth - 1)) - 1, dl, ResTy);
  SDValue One = DAG.getConstant(1, dl, ResTy);
  SDValue Exp00 = DAG.getNode(ISD::SHL, dl, ResTy, {Inp, One});
  SDValue Exp01 = DAG.getNode(ISD::SUB, dl, ResTy, {Exp00, M80});
  SDValue MNE = DAG.getConstant(ElemWidth - ExpWidth, dl, ResTy);
  SDValue ExpM1 = DAG.getNode(ISD::SRA, dl, ResTy, {Exp01, MNE});

  SDValue ExpW = DAG.getConstant(ExpWidth, dl, ResTy);
  SDValue Frc00 = DAG.getNode(ISD::SHL, dl, ResTy, {Inp, ExpW});
  SDValue Frc01 = DAG.getNode(ISD::OR, dl, ResTy, {Frc00, M80});

  SDValue MN2 = DAG.getConstant(ElemWidth - 2, dl, ResTy);
  SDValue Rsh00 = DAG.getNode(ISD::SUB, dl, ResTy, {MN2, ExpM1});
  SDValue MW = DAG.getConstant(ElemWidth, dl, ResTy);
  SDValue Rsh01 = DAG.getNode(ISD::SMIN, dl, ResTy, {Rsh00, MW});
  SDValue Frc02 = DAG.getNode(ISD::SRL, dl, ResTy, {Frc01, Rsh01});

  SDValue Int;

  if (Opc == ISD::FP_TO_SINT) {
    SDValue Bnd = DAG.getNode(ISD::VSELECT, dl, ResTy, {Neg, M80, M7F});
    SDValue Pos = DAG.getSetCC(dl, PredTy, Rsh01, Zero, ISD::SETGT);
    SDValue Frc13 = DAG.getNode(ISD::SUB, dl, ResTy, {Zero, Frc02});
    SDValue Frc14 = DAG.getNode(ISD::VSELECT, dl, ResTy, {Neg, Frc13, Frc02});
    Int = DAG.getNode(ISD::VSELECT, dl, ResTy, {Pos, Frc14, Bnd});
  } else {
    assert(Opc == ISD::FP_TO_UINT);
    SDValue Rsn = DAG.getSetCC(dl, PredTy, Rsh01, Zero, ISD::SETLT);
    SDValue Frc23 = DAG.getNode(ISD::VSELECT, dl, ResTy, Rsn, M7F, Frc02);
    Int = DAG.getNode(ISD::VSELECT, dl, ResTy, Neg, Zero, Frc23);
  }

  return Int;
}

SDValue
HexagonTargetLowering::ExpandHvxIntToFp(SDValue Op, SelectionDAG &DAG) const {
  unsigned Opc = Op.getOpcode();
  assert(Opc == ISD::SINT_TO_FP || Opc == ISD::UINT_TO_FP);

  const SDLoc &dl(Op);
  SDValue Op0 = Op.getOperand(0);
  MVT InpTy = ty(Op0);
  MVT ResTy = ty(Op);
  assert(ResTy.changeTypeToInteger() == InpTy);

  // uint32_t vnoc1_rnd(int32_t w) {
  //   int32_t iszero = w == 0;
  //   int32_t isneg = w < 0;
  //   uint32_t u = __builtin_HEXAGON_A2_abs(w);
  //
  //   uint32_t norm_left = __builtin_HEXAGON_S2_cl0(u) + 1;
  //   uint32_t frac0 = (uint64_t)u << norm_left;
  //
  //   // Rounding:
  //   uint32_t frac1 = frac0 + ((1 << 8) - 1);
  //   uint32_t renorm = (frac0 > frac1);
  //   uint32_t rup = (int)(frac0 << 22) < 0;
  //
  //   uint32_t frac2 = frac0 >> 8;
  //   uint32_t frac3 = frac1 >> 8;
  //   uint32_t frac = (frac2 != frac3) ? frac3 >> 1 : (frac3 + rup) >> 1;
  //
  //   int32_t exp = 32 - norm_left + renorm + 127;
  //   exp <<= 23;
  //
  //   uint32_t sign = 0x80000000 * isneg;
  //   uint32_t f = sign | exp | frac;
  //   return iszero ? 0 : f;
  // }

  MVT PredTy = MVT::getVectorVT(MVT::i1, InpTy.getVectorElementCount());
  bool Signed = Opc == ISD::SINT_TO_FP;

  auto [ExpWidth, ExpBias, FracWidth] = getIEEEProperties(ResTy);
  unsigned ElemWidth = 1 + ExpWidth + FracWidth;

  SDValue Zero = getZero(dl, InpTy, DAG);
  SDValue One = DAG.getConstant(1, dl, InpTy);
  SDValue IsZero = DAG.getSetCC(dl, PredTy, Op0, Zero, ISD::SETEQ);
  SDValue Abs = Signed ? DAG.getNode(ISD::ABS, dl, InpTy, Op0) : Op0;
  SDValue Clz = DAG.getNode(ISD::CTLZ, dl, InpTy, Abs);
  SDValue NLeft = DAG.getNode(ISD::ADD, dl, InpTy, {Clz, One});
  SDValue Frac0 = DAG.getNode(ISD::SHL, dl, InpTy, {Abs, NLeft});

  auto [Frac, Ovf] = emitHvxShiftRightRnd(Frac0, ExpWidth + 1, false, DAG);
  if (Signed) {
    SDValue IsNeg = DAG.getSetCC(dl, PredTy, Op0, Zero, ISD::SETLT);
    SDValue M80 = DAG.getConstant(1ull << (ElemWidth - 1), dl, InpTy);
    SDValue Sign = DAG.getNode(ISD::VSELECT, dl, InpTy, {IsNeg, M80, Zero});
    Frac = DAG.getNode(ISD::OR, dl, InpTy, {Sign, Frac});
  }

  SDValue Rnrm = DAG.getZExtOrTrunc(Ovf, dl, InpTy);
  SDValue Exp0 = DAG.getConstant(ElemWidth + ExpBias, dl, InpTy);
  SDValue Exp1 = DAG.getNode(ISD::ADD, dl, InpTy, {Rnrm, Exp0});
  SDValue Exp2 = DAG.getNode(ISD::SUB, dl, InpTy, {Exp1, NLeft});
  SDValue Exp3 = DAG.getNode(ISD::SHL, dl, InpTy,
                             {Exp2, DAG.getConstant(FracWidth, dl, InpTy)});
  SDValue Flt0 = DAG.getNode(ISD::OR, dl, InpTy, {Frac, Exp3});
  SDValue Flt1 = DAG.getNode(ISD::VSELECT, dl, InpTy, {IsZero, Zero, Flt0});
  SDValue Flt = DAG.getBitcast(ResTy, Flt1);

  return Flt;
}

SDValue
HexagonTargetLowering::CreateTLWrapper(SDValue Op, SelectionDAG &DAG) const {
  unsigned Opc = Op.getOpcode();
  unsigned TLOpc;
  switch (Opc) {
  case ISD::ANY_EXTEND:
  case ISD::SIGN_EXTEND:
  case ISD::ZERO_EXTEND:
    TLOpc = HexagonISD::TL_EXTEND;
    break;
  case ISD::TRUNCATE:
    TLOpc = HexagonISD::TL_TRUNCATE;
    break;
#ifndef NDEBUG
    Op.dump(&DAG);
#endif
    llvm_unreachable("Unepected operator");
  }

  const SDLoc &dl(Op);
  return DAG.getNode(TLOpc, dl, ty(Op), Op.getOperand(0),
                     DAG.getUNDEF(MVT::i128), // illegal type
                     DAG.getConstant(Opc, dl, MVT::i32));
}

SDValue
HexagonTargetLowering::RemoveTLWrapper(SDValue Op, SelectionDAG &DAG) const {
  assert(Op.getOpcode() == HexagonISD::TL_EXTEND ||
         Op.getOpcode() == HexagonISD::TL_TRUNCATE);
  unsigned Opc = Op.getConstantOperandVal(2);
  return DAG.getNode(Opc, SDLoc(Op), ty(Op), Op.getOperand(0));
}

HexagonTargetLowering::VectorPair
HexagonTargetLowering::SplitVectorOp(SDValue Op, SelectionDAG &DAG) const {
  assert(!Op.isMachineOpcode());
  SmallVector<SDValue, 2> OpsL, OpsH;
  const SDLoc &dl(Op);

  auto SplitVTNode = [&DAG, this](const VTSDNode *N) {
    MVT Ty = typeSplit(N->getVT().getSimpleVT()).first;
    SDValue TV = DAG.getValueType(Ty);
    return std::make_pair(TV, TV);
  };

  for (SDValue A : Op.getNode()->ops()) {
    auto [Lo, Hi] =
        ty(A).isVector() ? opSplit(A, dl, DAG) : std::make_pair(A, A);
    // Special case for type operand.
    switch (Op.getOpcode()) {
      case ISD::SIGN_EXTEND_INREG:
      case HexagonISD::SSAT:
      case HexagonISD::USAT:
        if (const auto *N = dyn_cast<const VTSDNode>(A.getNode()))
          std::tie(Lo, Hi) = SplitVTNode(N);
      break;
    }
    OpsL.push_back(Lo);
    OpsH.push_back(Hi);
  }

  MVT ResTy = ty(Op);
  MVT HalfTy = typeSplit(ResTy).first;
  SDValue L = DAG.getNode(Op.getOpcode(), dl, HalfTy, OpsL);
  SDValue H = DAG.getNode(Op.getOpcode(), dl, HalfTy, OpsH);
  return {L, H};
}

SDValue
HexagonTargetLowering::SplitHvxMemOp(SDValue Op, SelectionDAG &DAG) const {
  auto *MemN = cast<MemSDNode>(Op.getNode());

  MVT MemTy = MemN->getMemoryVT().getSimpleVT();
  if (!isHvxPairTy(MemTy))
    return Op;

  const SDLoc &dl(Op);
  unsigned HwLen = Subtarget.getVectorLength();
  MVT SingleTy = typeSplit(MemTy).first;
  SDValue Chain = MemN->getChain();
  SDValue Base0 = MemN->getBasePtr();
  SDValue Base1 =
      DAG.getMemBasePlusOffset(Base0, TypeSize::getFixed(HwLen), dl);
  unsigned MemOpc = MemN->getOpcode();

  MachineMemOperand *MOp0 = nullptr, *MOp1 = nullptr;
  if (MachineMemOperand *MMO = MemN->getMemOperand()) {
    MachineFunction &MF = DAG.getMachineFunction();
    uint64_t MemSize = (MemOpc == ISD::MLOAD || MemOpc == ISD::MSTORE)
                           ? (uint64_t)MemoryLocation::UnknownSize
                           : HwLen;
    MOp0 = MF.getMachineMemOperand(MMO, 0, MemSize);
    MOp1 = MF.getMachineMemOperand(MMO, HwLen, MemSize);
  }

  if (MemOpc == ISD::LOAD) {
    assert(cast<LoadSDNode>(Op)->isUnindexed());
    SDValue Load0 = DAG.getLoad(SingleTy, dl, Chain, Base0, MOp0);
    SDValue Load1 = DAG.getLoad(SingleTy, dl, Chain, Base1, MOp1);
    return DAG.getMergeValues(
        { DAG.getNode(ISD::CONCAT_VECTORS, dl, MemTy, Load0, Load1),
          DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                      Load0.getValue(1), Load1.getValue(1)) }, dl);
  }
  if (MemOpc == ISD::STORE) {
    assert(cast<StoreSDNode>(Op)->isUnindexed());
    VectorPair Vals = opSplit(cast<StoreSDNode>(Op)->getValue(), dl, DAG);
    SDValue Store0 = DAG.getStore(Chain, dl, Vals.first, Base0, MOp0);
    SDValue Store1 = DAG.getStore(Chain, dl, Vals.second, Base1, MOp1);
    return DAG.getNode(ISD::TokenFactor, dl, MVT::Other, Store0, Store1);
  }

  assert(MemOpc == ISD::MLOAD || MemOpc == ISD::MSTORE);

  auto MaskN = cast<MaskedLoadStoreSDNode>(Op);
  assert(MaskN->isUnindexed());
  VectorPair Masks = opSplit(MaskN->getMask(), dl, DAG);
  SDValue Offset = DAG.getUNDEF(MVT::i32);

  if (MemOpc == ISD::MLOAD) {
    VectorPair Thru =
        opSplit(cast<MaskedLoadSDNode>(Op)->getPassThru(), dl, DAG);
    SDValue MLoad0 =
        DAG.getMaskedLoad(SingleTy, dl, Chain, Base0, Offset, Masks.first,
                          Thru.first, SingleTy, MOp0, ISD::UNINDEXED,
                          ISD::NON_EXTLOAD, false);
    SDValue MLoad1 =
        DAG.getMaskedLoad(SingleTy, dl, Chain, Base1, Offset, Masks.second,
                          Thru.second, SingleTy, MOp1, ISD::UNINDEXED,
                          ISD::NON_EXTLOAD, false);
    return DAG.getMergeValues(
        { DAG.getNode(ISD::CONCAT_VECTORS, dl, MemTy, MLoad0, MLoad1),
          DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                      MLoad0.getValue(1), MLoad1.getValue(1)) }, dl);
  }
  if (MemOpc == ISD::MSTORE) {
    VectorPair Vals = opSplit(cast<MaskedStoreSDNode>(Op)->getValue(), dl, DAG);
    SDValue MStore0 = DAG.getMaskedStore(Chain, dl, Vals.first, Base0, Offset,
                                         Masks.first, SingleTy, MOp0,
                                         ISD::UNINDEXED, false, false);
    SDValue MStore1 = DAG.getMaskedStore(Chain, dl, Vals.second, Base1, Offset,
                                         Masks.second, SingleTy, MOp1,
                                         ISD::UNINDEXED, false, false);
    return DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MStore0, MStore1);
  }

  std::string Name = "Unexpected operation: " + Op->getOperationName(&DAG);
  llvm_unreachable(Name.c_str());
}

SDValue
HexagonTargetLowering::WidenHvxLoad(SDValue Op, SelectionDAG &DAG) const {
  const SDLoc &dl(Op);
  auto *LoadN = cast<LoadSDNode>(Op.getNode());
  assert(LoadN->isUnindexed() && "Not widening indexed loads yet");
  assert(LoadN->getMemoryVT().getVectorElementType() != MVT::i1 &&
         "Not widening loads of i1 yet");

  SDValue Chain = LoadN->getChain();
  SDValue Base = LoadN->getBasePtr();
  SDValue Offset = DAG.getUNDEF(MVT::i32);

  MVT ResTy = ty(Op);
  unsigned HwLen = Subtarget.getVectorLength();
  unsigned ResLen = ResTy.getStoreSize();
  assert(ResLen < HwLen && "vsetq(v1) prerequisite");

  MVT BoolTy = MVT::getVectorVT(MVT::i1, HwLen);
  SDValue Mask = getInstr(Hexagon::V6_pred_scalar2, dl, BoolTy,
                          {DAG.getConstant(ResLen, dl, MVT::i32)}, DAG);

  MVT LoadTy = MVT::getVectorVT(MVT::i8, HwLen);
  MachineFunction &MF = DAG.getMachineFunction();
  auto *MemOp = MF.getMachineMemOperand(LoadN->getMemOperand(), 0, HwLen);

  SDValue Load = DAG.getMaskedLoad(LoadTy, dl, Chain, Base, Offset, Mask,
                                   DAG.getUNDEF(LoadTy), LoadTy, MemOp,
                                   ISD::UNINDEXED, ISD::NON_EXTLOAD, false);
  SDValue Value = opCastElem(Load, ResTy.getVectorElementType(), DAG);
  return DAG.getMergeValues({Value, Load.getValue(1)}, dl);
}

SDValue
HexagonTargetLowering::WidenHvxStore(SDValue Op, SelectionDAG &DAG) const {
  const SDLoc &dl(Op);
  auto *StoreN = cast<StoreSDNode>(Op.getNode());
  assert(StoreN->isUnindexed() && "Not widening indexed stores yet");
  assert(StoreN->getMemoryVT().getVectorElementType() != MVT::i1 &&
         "Not widening stores of i1 yet");

  SDValue Chain = StoreN->getChain();
  SDValue Base = StoreN->getBasePtr();
  SDValue Offset = DAG.getUNDEF(MVT::i32);

  SDValue Value = opCastElem(StoreN->getValue(), MVT::i8, DAG);
  MVT ValueTy = ty(Value);
  unsigned ValueLen = ValueTy.getVectorNumElements();
  unsigned HwLen = Subtarget.getVectorLength();
  assert(isPowerOf2_32(ValueLen));

  for (unsigned Len = ValueLen; Len < HwLen; ) {
    Value = opJoin({Value, DAG.getUNDEF(ty(Value))}, dl, DAG);
    Len = ty(Value).getVectorNumElements(); // This is Len *= 2
  }
  assert(ty(Value).getVectorNumElements() == HwLen);  // Paranoia

  assert(ValueLen < HwLen && "vsetq(v1) prerequisite");
  MVT BoolTy = MVT::getVectorVT(MVT::i1, HwLen);
  SDValue Mask = getInstr(Hexagon::V6_pred_scalar2, dl, BoolTy,
                          {DAG.getConstant(ValueLen, dl, MVT::i32)}, DAG);
  MachineFunction &MF = DAG.getMachineFunction();
  auto *MemOp = MF.getMachineMemOperand(StoreN->getMemOperand(), 0, HwLen);
  return DAG.getMaskedStore(Chain, dl, Value, Base, Offset, Mask, ty(Value),
                            MemOp, ISD::UNINDEXED, false, false);
}

SDValue
HexagonTargetLowering::WidenHvxSetCC(SDValue Op, SelectionDAG &DAG) const {
  const SDLoc &dl(Op);
  SDValue Op0 = Op.getOperand(0), Op1 = Op.getOperand(1);
  MVT ElemTy = ty(Op0).getVectorElementType();
  unsigned HwLen = Subtarget.getVectorLength();

  unsigned WideOpLen = (8 * HwLen) / ElemTy.getSizeInBits();
  assert(WideOpLen * ElemTy.getSizeInBits() == 8 * HwLen);
  MVT WideOpTy = MVT::getVectorVT(ElemTy, WideOpLen);
  if (!Subtarget.isHVXVectorType(WideOpTy, true))
    return SDValue();

  SDValue WideOp0 = appendUndef(Op0, WideOpTy, DAG);
  SDValue WideOp1 = appendUndef(Op1, WideOpTy, DAG);
  EVT ResTy =
      getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), WideOpTy);
  SDValue SetCC = DAG.getNode(ISD::SETCC, dl, ResTy,
                              {WideOp0, WideOp1, Op.getOperand(2)});

  EVT RetTy = typeLegalize(ty(Op), DAG);
  return DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, RetTy,
                     {SetCC, getZero(dl, MVT::i32, DAG)});
}

SDValue
HexagonTargetLowering::LowerHvxOperation(SDValue Op, SelectionDAG &DAG) const {
  unsigned Opc = Op.getOpcode();
  bool IsPairOp = isHvxPairTy(ty(Op)) ||
                  llvm::any_of(Op.getNode()->ops(), [this] (SDValue V) {
                    return isHvxPairTy(ty(V));
                  });

  if (IsPairOp) {
    switch (Opc) {
      default:
        break;
      case ISD::LOAD:
      case ISD::STORE:
      case ISD::MLOAD:
      case ISD::MSTORE:
        return SplitHvxMemOp(Op, DAG);
      case ISD::SINT_TO_FP:
      case ISD::UINT_TO_FP:
      case ISD::FP_TO_SINT:
      case ISD::FP_TO_UINT:
        if (ty(Op).getSizeInBits() == ty(Op.getOperand(0)).getSizeInBits())
          return opJoin(SplitVectorOp(Op, DAG), SDLoc(Op), DAG);
        break;
      case ISD::ABS:
      case ISD::CTPOP:
      case ISD::CTLZ:
      case ISD::CTTZ:
      case ISD::MUL:
      case ISD::FADD:
      case ISD::FSUB:
      case ISD::FMUL:
      case ISD::FMINNUM:
      case ISD::FMAXNUM:
      case ISD::MULHS:
      case ISD::MULHU:
      case ISD::AND:
      case ISD::OR:
      case ISD::XOR:
      case ISD::SRA:
      case ISD::SHL:
      case ISD::SRL:
      case ISD::FSHL:
      case ISD::FSHR:
      case ISD::SMIN:
      case ISD::SMAX:
      case ISD::UMIN:
      case ISD::UMAX:
      case ISD::SETCC:
      case ISD::VSELECT:
      case ISD::SIGN_EXTEND_INREG:
      case ISD::SPLAT_VECTOR:
        return opJoin(SplitVectorOp(Op, DAG), SDLoc(Op), DAG);
      case ISD::SIGN_EXTEND:
      case ISD::ZERO_EXTEND:
        // In general, sign- and zero-extends can't be split and still
        // be legal. The only exception is extending bool vectors.
        if (ty(Op.getOperand(0)).getVectorElementType() == MVT::i1)
          return opJoin(SplitVectorOp(Op, DAG), SDLoc(Op), DAG);
        break;
    }
  }

  switch (Opc) {
    default:
      break;
    case ISD::BUILD_VECTOR:            return LowerHvxBuildVector(Op, DAG);
    case ISD::SPLAT_VECTOR:            return LowerHvxSplatVector(Op, DAG);
    case ISD::CONCAT_VECTORS:          return LowerHvxConcatVectors(Op, DAG);
    case ISD::INSERT_SUBVECTOR:        return LowerHvxInsertSubvector(Op, DAG);
    case ISD::INSERT_VECTOR_ELT:       return LowerHvxInsertElement(Op, DAG);
    case ISD::EXTRACT_SUBVECTOR:       return LowerHvxExtractSubvector(Op, DAG);
    case ISD::EXTRACT_VECTOR_ELT:      return LowerHvxExtractElement(Op, DAG);
    case ISD::BITCAST:                 return LowerHvxBitcast(Op, DAG);
    case ISD::ANY_EXTEND:              return LowerHvxAnyExt(Op, DAG);
    case ISD::SIGN_EXTEND:             return LowerHvxSignExt(Op, DAG);
    case ISD::ZERO_EXTEND:             return LowerHvxZeroExt(Op, DAG);
    case ISD::CTTZ:                    return LowerHvxCttz(Op, DAG);
    case ISD::SELECT:                  return LowerHvxSelect(Op, DAG);
    case ISD::SRA:
    case ISD::SHL:
    case ISD::SRL:                     return LowerHvxShift(Op, DAG);
    case ISD::FSHL:
    case ISD::FSHR:                    return LowerHvxFunnelShift(Op, DAG);
    case ISD::MULHS:
    case ISD::MULHU:                   return LowerHvxMulh(Op, DAG);
    case ISD::SMUL_LOHI:
    case ISD::UMUL_LOHI:               return LowerHvxMulLoHi(Op, DAG);
    case ISD::ANY_EXTEND_VECTOR_INREG: return LowerHvxExtend(Op, DAG);
    case ISD::SETCC:
    case ISD::INTRINSIC_VOID:          return Op;
    case ISD::INTRINSIC_WO_CHAIN:      return LowerHvxIntrinsic(Op, DAG);
    case ISD::MLOAD:
    case ISD::MSTORE:                  return LowerHvxMaskedOp(Op, DAG);
    // Unaligned loads will be handled by the default lowering.
    case ISD::LOAD:                    return SDValue();
    case ISD::FP_EXTEND:               return LowerHvxFpExtend(Op, DAG);
    case ISD::FP_TO_SINT:
    case ISD::FP_TO_UINT:              return LowerHvxFpToInt(Op, DAG);
    case ISD::SINT_TO_FP:
    case ISD::UINT_TO_FP:              return LowerHvxIntToFp(Op, DAG);

    // Special nodes:
    case HexagonISD::SMUL_LOHI:
    case HexagonISD::UMUL_LOHI:
    case HexagonISD::USMUL_LOHI:       return LowerHvxMulLoHi(Op, DAG);
  }
#ifndef NDEBUG
  Op.dumpr(&DAG);
#endif
  llvm_unreachable("Unhandled HVX operation");
}

SDValue
HexagonTargetLowering::ExpandHvxResizeIntoSteps(SDValue Op, SelectionDAG &DAG)
      const {
  // Rewrite the extension/truncation/saturation op into steps where each
  // step changes the type widths by a factor of 2.
  // E.g.  i8 -> i16 remains unchanged, but i8 -> i32  ==>  i8 -> i16 -> i32.
  //
  // Some of the vector types in Op may not be legal.

  unsigned Opc = Op.getOpcode();
  switch (Opc) {
    case HexagonISD::SSAT:
    case HexagonISD::USAT:
    case HexagonISD::TL_EXTEND:
    case HexagonISD::TL_TRUNCATE:
      break;
    case ISD::ANY_EXTEND:
    case ISD::ZERO_EXTEND:
    case ISD::SIGN_EXTEND:
    case ISD::TRUNCATE:
      llvm_unreachable("ISD:: ops will be auto-folded");
      break;
#ifndef NDEBUG
    Op.dump(&DAG);
#endif
    llvm_unreachable("Unexpected operation");
  }

  SDValue Inp = Op.getOperand(0);
  MVT InpTy = ty(Inp);
  MVT ResTy = ty(Op);

  unsigned InpWidth = InpTy.getVectorElementType().getSizeInBits();
  unsigned ResWidth = ResTy.getVectorElementType().getSizeInBits();
  assert(InpWidth != ResWidth);

  if (InpWidth == 2 * ResWidth || ResWidth == 2 * InpWidth)
    return Op;

  const SDLoc &dl(Op);
  unsigned NumElems = InpTy.getVectorNumElements();
  assert(NumElems == ResTy.getVectorNumElements());

  auto repeatOp = [&](unsigned NewWidth, SDValue Arg) {
    MVT Ty = MVT::getVectorVT(MVT::getIntegerVT(NewWidth), NumElems);
    switch (Opc) {
      case HexagonISD::SSAT:
      case HexagonISD::USAT:
        return DAG.getNode(Opc, dl, Ty, {Arg, DAG.getValueType(Ty)});
      case HexagonISD::TL_EXTEND:
      case HexagonISD::TL_TRUNCATE:
        return DAG.getNode(Opc, dl, Ty, {Arg, Op.getOperand(1), Op.getOperand(2)});
      default:
        llvm_unreachable("Unexpected opcode");
    }
  };

  SDValue S = Inp;
  if (InpWidth < ResWidth) {
    assert(ResWidth % InpWidth == 0 && isPowerOf2_32(ResWidth / InpWidth));
    while (InpWidth * 2 <= ResWidth)
      S = repeatOp(InpWidth *= 2, S);
  } else {
    // InpWidth > ResWidth
    assert(InpWidth % ResWidth == 0 && isPowerOf2_32(InpWidth / ResWidth));
    while (InpWidth / 2 >= ResWidth)
      S = repeatOp(InpWidth /= 2, S);
  }
  return S;
}

SDValue
HexagonTargetLowering::LegalizeHvxResize(SDValue Op, SelectionDAG &DAG) const {
  SDValue Inp0 = Op.getOperand(0);
  MVT InpTy = ty(Inp0);
  MVT ResTy = ty(Op);
  unsigned InpWidth = InpTy.getSizeInBits();
  unsigned ResWidth = ResTy.getSizeInBits();
  unsigned Opc = Op.getOpcode();

  if (shouldWidenToHvx(InpTy, DAG) || shouldWidenToHvx(ResTy, DAG)) {
    // First, make sure that the narrower type is widened to HVX.
    // This may cause the result to be wider than what the legalizer
    // expects, so insert EXTRACT_SUBVECTOR to bring it back to the
    // desired type.
    auto [WInpTy, WResTy] =
        InpWidth < ResWidth ? typeWidenToWider(typeWidenToHvx(InpTy), ResTy)
                            : typeWidenToWider(InpTy, typeWidenToHvx(ResTy));
    SDValue W = appendUndef(Inp0, WInpTy, DAG);
    SDValue S;
    if (Opc == HexagonISD::TL_EXTEND || Opc == HexagonISD::TL_TRUNCATE) {
      S = DAG.getNode(Opc, SDLoc(Op), WResTy, W, Op.getOperand(1),
                      Op.getOperand(2));
    } else {
      S = DAG.getNode(Opc, SDLoc(Op), WResTy, W, DAG.getValueType(WResTy));
    }
    SDValue T = ExpandHvxResizeIntoSteps(S, DAG);
    return extractSubvector(T, typeLegalize(ResTy, DAG), 0, DAG);
  } else if (shouldSplitToHvx(InpWidth < ResWidth ? ResTy : InpTy, DAG)) {
    return opJoin(SplitVectorOp(Op, DAG), SDLoc(Op), DAG);
  } else {
    assert(isTypeLegal(InpTy) && isTypeLegal(ResTy));
    return RemoveTLWrapper(Op, DAG);
  }
  llvm_unreachable("Unexpected situation");
}

void
HexagonTargetLowering::LowerHvxOperationWrapper(SDNode *N,
      SmallVectorImpl<SDValue> &Results, SelectionDAG &DAG) const {
  unsigned Opc = N->getOpcode();
  SDValue Op(N, 0);
  SDValue Inp0;   // Optional first argument.
  if (N->getNumOperands() > 0)
    Inp0 = Op.getOperand(0);

  switch (Opc) {
    case ISD::ANY_EXTEND:
    case ISD::SIGN_EXTEND:
    case ISD::ZERO_EXTEND:
    case ISD::TRUNCATE:
      if (Subtarget.isHVXElementType(ty(Op)) &&
          Subtarget.isHVXElementType(ty(Inp0))) {
        Results.push_back(CreateTLWrapper(Op, DAG));
      }
      break;
    case ISD::SETCC:
      if (shouldWidenToHvx(ty(Inp0), DAG)) {
        if (SDValue T = WidenHvxSetCC(Op, DAG))
          Results.push_back(T);
      }
      break;
    case ISD::STORE: {
      if (shouldWidenToHvx(ty(cast<StoreSDNode>(N)->getValue()), DAG)) {
        SDValue Store = WidenHvxStore(Op, DAG);
        Results.push_back(Store);
      }
      break;
    }
    case ISD::MLOAD:
      if (isHvxPairTy(ty(Op))) {
        SDValue S = SplitHvxMemOp(Op, DAG);
        assert(S->getOpcode() == ISD::MERGE_VALUES);
        Results.push_back(S.getOperand(0));
        Results.push_back(S.getOperand(1));
      }
      break;
    case ISD::MSTORE:
      if (isHvxPairTy(ty(Op->getOperand(1)))) {    // Stored value
        SDValue S = SplitHvxMemOp(Op, DAG);
        Results.push_back(S);
      }
      break;
    case ISD::SINT_TO_FP:
    case ISD::UINT_TO_FP:
    case ISD::FP_TO_SINT:
    case ISD::FP_TO_UINT:
      if (ty(Op).getSizeInBits() != ty(Inp0).getSizeInBits()) {
        SDValue T = EqualizeFpIntConversion(Op, DAG);
        Results.push_back(T);
      }
      break;
    case HexagonISD::SSAT:
    case HexagonISD::USAT:
    case HexagonISD::TL_EXTEND:
    case HexagonISD::TL_TRUNCATE:
      Results.push_back(LegalizeHvxResize(Op, DAG));
      break;
    default:
      break;
  }
}

void
HexagonTargetLowering::ReplaceHvxNodeResults(SDNode *N,
      SmallVectorImpl<SDValue> &Results, SelectionDAG &DAG) const {
  unsigned Opc = N->getOpcode();
  SDValue Op(N, 0);
  SDValue Inp0;   // Optional first argument.
  if (N->getNumOperands() > 0)
    Inp0 = Op.getOperand(0);

  switch (Opc) {
    case ISD::ANY_EXTEND:
    case ISD::SIGN_EXTEND:
    case ISD::ZERO_EXTEND:
    case ISD::TRUNCATE:
      if (Subtarget.isHVXElementType(ty(Op)) &&
          Subtarget.isHVXElementType(ty(Inp0))) {
        Results.push_back(CreateTLWrapper(Op, DAG));
      }
      break;
    case ISD::SETCC:
      if (shouldWidenToHvx(ty(Op), DAG)) {
        if (SDValue T = WidenHvxSetCC(Op, DAG))
          Results.push_back(T);
      }
      break;
    case ISD::LOAD: {
      if (shouldWidenToHvx(ty(Op), DAG)) {
        SDValue Load = WidenHvxLoad(Op, DAG);
        assert(Load->getOpcode() == ISD::MERGE_VALUES);
        Results.push_back(Load.getOperand(0));
        Results.push_back(Load.getOperand(1));
      }
      break;
    }
    case ISD::BITCAST:
      if (isHvxBoolTy(ty(Inp0))) {
        SDValue C = LowerHvxBitcast(Op, DAG);
        Results.push_back(C);
      }
      break;
    case ISD::FP_TO_SINT:
    case ISD::FP_TO_UINT:
      if (ty(Op).getSizeInBits() != ty(Inp0).getSizeInBits()) {
        SDValue T = EqualizeFpIntConversion(Op, DAG);
        Results.push_back(T);
      }
      break;
    case HexagonISD::SSAT:
    case HexagonISD::USAT:
    case HexagonISD::TL_EXTEND:
    case HexagonISD::TL_TRUNCATE:
      Results.push_back(LegalizeHvxResize(Op, DAG));
      break;
    default:
      break;
  }
}

SDValue
HexagonTargetLowering::combineTruncateBeforeLegal(SDValue Op,
                                                  DAGCombinerInfo &DCI) const {
  // Simplify V:v2NiB --(bitcast)--> vNi2B --(truncate)--> vNiB
  // to extract-subvector (shuffle V, pick even, pick odd)

  assert(Op.getOpcode() == ISD::TRUNCATE);
  SelectionDAG &DAG = DCI.DAG;
  const SDLoc &dl(Op);

  if (Op.getOperand(0).getOpcode() == ISD::BITCAST)
    return SDValue();
  SDValue Cast = Op.getOperand(0);
  SDValue Src = Cast.getOperand(0);

  EVT TruncTy = Op.getValueType();
  EVT CastTy = Cast.getValueType();
  EVT SrcTy = Src.getValueType();
  if (SrcTy.isSimple())
    return SDValue();
  if (SrcTy.getVectorElementType() != TruncTy.getVectorElementType())
    return SDValue();
  unsigned SrcLen = SrcTy.getVectorNumElements();
  unsigned CastLen = CastTy.getVectorNumElements();
  if (2 * CastLen != SrcLen)
    return SDValue();

  SmallVector<int, 128> Mask(SrcLen);
  for (int i = 0; i != static_cast<int>(CastLen); ++i) {
    Mask[i] = 2 * i;
    Mask[i + CastLen] = 2 * i + 1;
  }
  SDValue Deal =
      DAG.getVectorShuffle(SrcTy, dl, Src, DAG.getUNDEF(SrcTy), Mask);
  return opSplit(Deal, dl, DAG).first;
}

SDValue
HexagonTargetLowering::combineConcatVectorsBeforeLegal(
    SDValue Op, DAGCombinerInfo &DCI) const {
  // Fold
  //   concat (shuffle x, y, m1), (shuffle x, y, m2)
  // into
  //   shuffle (concat x, y), undef, m3
  if (Op.getNumOperands() != 2)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  const SDLoc &dl(Op);
  SDValue V0 = Op.getOperand(0);
  SDValue V1 = Op.getOperand(1);

  if (V0.getOpcode() != ISD::VECTOR_SHUFFLE)
    return SDValue();
  if (V1.getOpcode() != ISD::VECTOR_SHUFFLE)
    return SDValue();

  SetVector<SDValue> Order;
  Order.insert(V0.getOperand(0));
  Order.insert(V0.getOperand(1));
  Order.insert(V1.getOperand(0));
  Order.insert(V1.getOperand(1));

  if (Order.size() > 2)
    return SDValue();

  // In ISD::VECTOR_SHUFFLE, the types of each input and the type of the
  // result must be the same.
  EVT InpTy = V0.getValueType();
  assert(InpTy.isVector());
  unsigned InpLen = InpTy.getVectorNumElements();

  SmallVector<int, 128> LongMask;
  auto AppendToMask = [&](SDValue Shuffle) {
    auto *SV = cast<ShuffleVectorSDNode>(Shuffle.getNode());
    ArrayRef<int> Mask = SV->getMask();
    SDValue X = Shuffle.getOperand(0);
    SDValue Y = Shuffle.getOperand(1);
    for (int M : Mask) {
      if (M == -1) {
        LongMask.push_back(M);
        continue;
      }
      SDValue Src = static_cast<unsigned>(M) < InpLen ? X : Y;
      if (static_cast<unsigned>(M) >= InpLen)
        M -= InpLen;

      int OutOffset = Order[0] == Src ? 0 : InpLen;
      LongMask.push_back(M + OutOffset);
    }
  };

  AppendToMask(V0);
  AppendToMask(V1);

  SDValue C0 = Order.front();
  SDValue C1 = Order.back();  // Can be same as front
  EVT LongTy = InpTy.getDoubleNumVectorElementsVT(*DAG.getContext());

  SDValue Cat = DAG.getNode(ISD::CONCAT_VECTORS, dl, LongTy, {C0, C1});
  return DAG.getVectorShuffle(LongTy, dl, Cat, DAG.getUNDEF(LongTy), LongMask);
}

SDValue
HexagonTargetLowering::PerformHvxDAGCombine(SDNode *N, DAGCombinerInfo &DCI)
      const {
  const SDLoc &dl(N);
  SelectionDAG &DAG = DCI.DAG;
  SDValue Op(N, 0);
  unsigned Opc = Op.getOpcode();

  SmallVector<SDValue, 4> Ops(N->ops().begin(), N->ops().end());

  if (Opc == ISD::TRUNCATE)
    return combineTruncateBeforeLegal(Op, DCI);
  if (Opc == ISD::CONCAT_VECTORS)
    return combineConcatVectorsBeforeLegal(Op, DCI);

  if (DCI.isBeforeLegalizeOps())
    return SDValue();

  switch (Opc) {
    case ISD::VSELECT: {
      // (vselect (xor x, qtrue), v0, v1) -> (vselect x, v1, v0)
      SDValue Cond = Ops[0];
      if (Cond->getOpcode() == ISD::XOR) {
        SDValue C0 = Cond.getOperand(0), C1 = Cond.getOperand(1);
        if (C1->getOpcode() == HexagonISD::QTRUE)
          return DAG.getNode(ISD::VSELECT, dl, ty(Op), C0, Ops[2], Ops[1]);
      }
      break;
    }
    case HexagonISD::V2Q:
      if (Ops[0].getOpcode() == ISD::SPLAT_VECTOR) {
        if (const auto *C = dyn_cast<ConstantSDNode>(Ops[0].getOperand(0)))
          return C->isZero() ? DAG.getNode(HexagonISD::QFALSE, dl, ty(Op))
                             : DAG.getNode(HexagonISD::QTRUE, dl, ty(Op));
      }
      break;
    case HexagonISD::Q2V:
      if (Ops[0].getOpcode() == HexagonISD::QTRUE)
        return DAG.getNode(ISD::SPLAT_VECTOR, dl, ty(Op),
                           DAG.getConstant(-1, dl, MVT::i32));
      if (Ops[0].getOpcode() == HexagonISD::QFALSE)
        return getZero(dl, ty(Op), DAG);
      break;
    case HexagonISD::VINSERTW0:
      if (isUndef(Ops[1]))
        return Ops[0];
      break;
    case HexagonISD::VROR: {
      if (Ops[0].getOpcode() == HexagonISD::VROR) {
        SDValue Vec = Ops[0].getOperand(0);
        SDValue Rot0 = Ops[1], Rot1 = Ops[0].getOperand(1);
        SDValue Rot = DAG.getNode(ISD::ADD, dl, ty(Rot0), {Rot0, Rot1});
        return DAG.getNode(HexagonISD::VROR, dl, ty(Op), {Vec, Rot});
      }
      break;
    }
  }

  return SDValue();
}

bool
HexagonTargetLowering::shouldSplitToHvx(MVT Ty, SelectionDAG &DAG) const {
  if (Subtarget.isHVXVectorType(Ty, true))
    return false;
  auto Action = getPreferredHvxVectorAction(Ty);
  if (Action == TargetLoweringBase::TypeSplitVector)
    return Subtarget.isHVXVectorType(typeLegalize(Ty, DAG), true);
  return false;
}

bool
HexagonTargetLowering::shouldWidenToHvx(MVT Ty, SelectionDAG &DAG) const {
  if (Subtarget.isHVXVectorType(Ty, true))
    return false;
  auto Action = getPreferredHvxVectorAction(Ty);
  if (Action == TargetLoweringBase::TypeWidenVector)
    return Subtarget.isHVXVectorType(typeLegalize(Ty, DAG), true);
  return false;
}

bool
HexagonTargetLowering::isHvxOperation(SDNode *N, SelectionDAG &DAG) const {
  if (!Subtarget.useHVXOps())
    return false;
  // If the type of any result, or any operand type are HVX vector types,
  // this is an HVX operation.
  auto IsHvxTy = [this](EVT Ty) {
    return Ty.isSimple() && Subtarget.isHVXVectorType(Ty.getSimpleVT(), true);
  };
  auto IsHvxOp = [this](SDValue Op) {
    return Op.getValueType().isSimple() &&
           Subtarget.isHVXVectorType(ty(Op), true);
  };
  if (llvm::any_of(N->values(), IsHvxTy) || llvm::any_of(N->ops(), IsHvxOp))
    return true;

  // Check if this could be an HVX operation after type widening.
  auto IsWidenedToHvx = [this, &DAG](SDValue Op) {
    if (!Op.getValueType().isSimple())
      return false;
    MVT ValTy = ty(Op);
    return ValTy.isVector() && shouldWidenToHvx(ValTy, DAG);
  };

  for (int i = 0, e = N->getNumValues(); i != e; ++i) {
    if (IsWidenedToHvx(SDValue(N, i)))
      return true;
  }
  return llvm::any_of(N->ops(), IsWidenedToHvx);
}
