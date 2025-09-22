//===-- NVPTXISelLowering.cpp - NVPTX DAG Lowering Implementation ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that NVPTX uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "NVPTXISelLowering.h"
#include "MCTargetDesc/NVPTXBaseInfo.h"
#include "NVPTX.h"
#include "NVPTXSubtarget.h"
#include "NVPTXTargetMachine.h"
#include "NVPTXTargetObjectFile.h"
#include "NVPTXUtilities.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetCallingConv.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/FPEnv.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicsNVPTX.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#define DEBUG_TYPE "nvptx-lower"

using namespace llvm;

static std::atomic<unsigned> GlobalUniqueCallSite;

static cl::opt<bool> sched4reg(
    "nvptx-sched4reg",
    cl::desc("NVPTX Specific: schedule for register pressue"), cl::init(false));

static cl::opt<unsigned> FMAContractLevelOpt(
    "nvptx-fma-level", cl::Hidden,
    cl::desc("NVPTX Specific: FMA contraction (0: don't do it"
             " 1: do it  2: do it aggressively"),
    cl::init(2));

static cl::opt<int> UsePrecDivF32(
    "nvptx-prec-divf32", cl::Hidden,
    cl::desc("NVPTX Specifies: 0 use div.approx, 1 use div.full, 2 use"
             " IEEE Compliant F32 div.rnd if available."),
    cl::init(2));

static cl::opt<bool> UsePrecSqrtF32(
    "nvptx-prec-sqrtf32", cl::Hidden,
    cl::desc("NVPTX Specific: 0 use sqrt.approx, 1 use sqrt.rn."),
    cl::init(true));

static cl::opt<bool> ForceMinByValParamAlign(
    "nvptx-force-min-byval-param-align", cl::Hidden,
    cl::desc("NVPTX Specific: force 4-byte minimal alignment for byval"
             " params of device functions."),
    cl::init(false));

int NVPTXTargetLowering::getDivF32Level() const {
  if (UsePrecDivF32.getNumOccurrences() > 0) {
    // If nvptx-prec-div32=N is used on the command-line, always honor it
    return UsePrecDivF32;
  } else {
    // Otherwise, use div.approx if fast math is enabled
    if (getTargetMachine().Options.UnsafeFPMath)
      return 0;
    else
      return 2;
  }
}

bool NVPTXTargetLowering::usePrecSqrtF32() const {
  if (UsePrecSqrtF32.getNumOccurrences() > 0) {
    // If nvptx-prec-sqrtf32 is used on the command-line, always honor it
    return UsePrecSqrtF32;
  } else {
    // Otherwise, use sqrt.approx if fast math is enabled
    return !getTargetMachine().Options.UnsafeFPMath;
  }
}

bool NVPTXTargetLowering::useF32FTZ(const MachineFunction &MF) const {
  return MF.getDenormalMode(APFloat::IEEEsingle()).Output ==
         DenormalMode::PreserveSign;
}

static bool IsPTXVectorType(MVT VT) {
  switch (VT.SimpleTy) {
  default:
    return false;
  case MVT::v2i1:
  case MVT::v4i1:
  case MVT::v2i8:
  case MVT::v4i8:
  case MVT::v2i16:
  case MVT::v4i16:
  case MVT::v8i16: // <4 x i16x2>
  case MVT::v2i32:
  case MVT::v4i32:
  case MVT::v2i64:
  case MVT::v2f16:
  case MVT::v4f16:
  case MVT::v8f16: // <4 x f16x2>
  case MVT::v2bf16:
  case MVT::v4bf16:
  case MVT::v8bf16: // <4 x bf16x2>
  case MVT::v2f32:
  case MVT::v4f32:
  case MVT::v2f64:
    return true;
  }
}

static bool Is16bitsType(MVT VT) {
  return (VT.SimpleTy == MVT::f16 || VT.SimpleTy == MVT::bf16 ||
          VT.SimpleTy == MVT::i16);
}

/// ComputePTXValueVTs - For the given Type \p Ty, returns the set of primitive
/// EVTs that compose it.  Unlike ComputeValueVTs, this will break apart vectors
/// into their primitive components.
/// NOTE: This is a band-aid for code that expects ComputeValueVTs to return the
/// same number of types as the Ins/Outs arrays in LowerFormalArguments,
/// LowerCall, and LowerReturn.
static void ComputePTXValueVTs(const TargetLowering &TLI, const DataLayout &DL,
                               Type *Ty, SmallVectorImpl<EVT> &ValueVTs,
                               SmallVectorImpl<uint64_t> *Offsets = nullptr,
                               uint64_t StartingOffset = 0) {
  SmallVector<EVT, 16> TempVTs;
  SmallVector<uint64_t, 16> TempOffsets;

  // Special case for i128 - decompose to (i64, i64)
  if (Ty->isIntegerTy(128)) {
    ValueVTs.push_back(EVT(MVT::i64));
    ValueVTs.push_back(EVT(MVT::i64));

    if (Offsets) {
      Offsets->push_back(StartingOffset + 0);
      Offsets->push_back(StartingOffset + 8);
    }

    return;
  }

  // Given a struct type, recursively traverse the elements with custom ComputePTXValueVTs.
  if (StructType *STy = dyn_cast<StructType>(Ty)) {
    auto const *SL = DL.getStructLayout(STy);
    auto ElementNum = 0;
    for(auto *EI : STy->elements()) {
      ComputePTXValueVTs(TLI, DL, EI, ValueVTs, Offsets,
                         StartingOffset + SL->getElementOffset(ElementNum));
      ++ElementNum;
    }
    return;
  }

  ComputeValueVTs(TLI, DL, Ty, TempVTs, &TempOffsets, StartingOffset);
  for (unsigned i = 0, e = TempVTs.size(); i != e; ++i) {
    EVT VT = TempVTs[i];
    uint64_t Off = TempOffsets[i];
    // Split vectors into individual elements, except for v2f16, which
    // we will pass as a single scalar.
    if (VT.isVector()) {
      unsigned NumElts = VT.getVectorNumElements();
      EVT EltVT = VT.getVectorElementType();
      // Vectors with an even number of f16 elements will be passed to
      // us as an array of v2f16/v2bf16 elements. We must match this so we
      // stay in sync with Ins/Outs.
      if ((Is16bitsType(EltVT.getSimpleVT())) && NumElts % 2 == 0) {
        switch (EltVT.getSimpleVT().SimpleTy) {
        case MVT::f16:
          EltVT = MVT::v2f16;
          break;
        case MVT::bf16:
          EltVT = MVT::v2bf16;
          break;
        case MVT::i16:
          EltVT = MVT::v2i16;
          break;
        default:
          llvm_unreachable("Unexpected type");
        }
        NumElts /= 2;
      } else if (EltVT.getSimpleVT() == MVT::i8 &&
                 (NumElts % 4 == 0 || NumElts == 3)) {
        // v*i8 are formally lowered as v4i8
        EltVT = MVT::v4i8;
        NumElts = (NumElts + 3) / 4;
      } else if (EltVT.getSimpleVT() == MVT::i8 && NumElts == 2) {
        // v2i8 is promoted to v2i16
        NumElts = 1;
        EltVT = MVT::v2i16;
      }
      for (unsigned j = 0; j != NumElts; ++j) {
        ValueVTs.push_back(EltVT);
        if (Offsets)
          Offsets->push_back(Off + j * EltVT.getStoreSize());
      }
    } else {
      ValueVTs.push_back(VT);
      if (Offsets)
        Offsets->push_back(Off);
    }
  }
}

/// PromoteScalarIntegerPTX
/// Used to make sure the arguments/returns are suitable for passing
/// and promote them to a larger size if they're not.
///
/// The promoted type is placed in \p PromoteVT if the function returns true.
static bool PromoteScalarIntegerPTX(const EVT &VT, MVT *PromotedVT) {
  if (VT.isScalarInteger()) {
    switch (PowerOf2Ceil(VT.getFixedSizeInBits())) {
    default:
      llvm_unreachable(
          "Promotion is not suitable for scalars of size larger than 64-bits");
    case 1:
      *PromotedVT = MVT::i1;
      break;
    case 2:
    case 4:
    case 8:
      *PromotedVT = MVT::i8;
      break;
    case 16:
      *PromotedVT = MVT::i16;
      break;
    case 32:
      *PromotedVT = MVT::i32;
      break;
    case 64:
      *PromotedVT = MVT::i64;
      break;
    }
    return EVT(*PromotedVT) != VT;
  }
  return false;
}

// Check whether we can merge loads/stores of some of the pieces of a
// flattened function parameter or return value into a single vector
// load/store.
//
// The flattened parameter is represented as a list of EVTs and
// offsets, and the whole structure is aligned to ParamAlignment. This
// function determines whether we can load/store pieces of the
// parameter starting at index Idx using a single vectorized op of
// size AccessSize. If so, it returns the number of param pieces
// covered by the vector op. Otherwise, it returns 1.
static unsigned CanMergeParamLoadStoresStartingAt(
    unsigned Idx, uint32_t AccessSize, const SmallVectorImpl<EVT> &ValueVTs,
    const SmallVectorImpl<uint64_t> &Offsets, Align ParamAlignment) {

  // Can't vectorize if param alignment is not sufficient.
  if (ParamAlignment < AccessSize)
    return 1;
  // Can't vectorize if offset is not aligned.
  if (Offsets[Idx] & (AccessSize - 1))
    return 1;

  EVT EltVT = ValueVTs[Idx];
  unsigned EltSize = EltVT.getStoreSize();

  // Element is too large to vectorize.
  if (EltSize >= AccessSize)
    return 1;

  unsigned NumElts = AccessSize / EltSize;
  // Can't vectorize if AccessBytes if not a multiple of EltSize.
  if (AccessSize != EltSize * NumElts)
    return 1;

  // We don't have enough elements to vectorize.
  if (Idx + NumElts > ValueVTs.size())
    return 1;

  // PTX ISA can only deal with 2- and 4-element vector ops.
  if (NumElts != 4 && NumElts != 2)
    return 1;

  for (unsigned j = Idx + 1; j < Idx + NumElts; ++j) {
    // Types do not match.
    if (ValueVTs[j] != EltVT)
      return 1;

    // Elements are not contiguous.
    if (Offsets[j] - Offsets[j - 1] != EltSize)
      return 1;
  }
  // OK. We can vectorize ValueVTs[i..i+NumElts)
  return NumElts;
}

// Flags for tracking per-element vectorization state of loads/stores
// of a flattened function parameter or return value.
enum ParamVectorizationFlags {
  PVF_INNER = 0x0, // Middle elements of a vector.
  PVF_FIRST = 0x1, // First element of the vector.
  PVF_LAST = 0x2,  // Last element of the vector.
  // Scalar is effectively a 1-element vector.
  PVF_SCALAR = PVF_FIRST | PVF_LAST
};

// Computes whether and how we can vectorize the loads/stores of a
// flattened function parameter or return value.
//
// The flattened parameter is represented as the list of ValueVTs and
// Offsets, and is aligned to ParamAlignment bytes. We return a vector
// of the same size as ValueVTs indicating how each piece should be
// loaded/stored (i.e. as a scalar, or as part of a vector
// load/store).
static SmallVector<ParamVectorizationFlags, 16>
VectorizePTXValueVTs(const SmallVectorImpl<EVT> &ValueVTs,
                     const SmallVectorImpl<uint64_t> &Offsets,
                     Align ParamAlignment, bool IsVAArg = false) {
  // Set vector size to match ValueVTs and mark all elements as
  // scalars by default.
  SmallVector<ParamVectorizationFlags, 16> VectorInfo;
  VectorInfo.assign(ValueVTs.size(), PVF_SCALAR);

  if (IsVAArg)
    return VectorInfo;

  // Check what we can vectorize using 128/64/32-bit accesses.
  for (int I = 0, E = ValueVTs.size(); I != E; ++I) {
    // Skip elements we've already processed.
    assert(VectorInfo[I] == PVF_SCALAR && "Unexpected vector info state.");
    for (unsigned AccessSize : {16, 8, 4, 2}) {
      unsigned NumElts = CanMergeParamLoadStoresStartingAt(
          I, AccessSize, ValueVTs, Offsets, ParamAlignment);
      // Mark vectorized elements.
      switch (NumElts) {
      default:
        llvm_unreachable("Unexpected return value");
      case 1:
        // Can't vectorize using this size, try next smaller size.
        continue;
      case 2:
        assert(I + 1 < E && "Not enough elements.");
        VectorInfo[I] = PVF_FIRST;
        VectorInfo[I + 1] = PVF_LAST;
        I += 1;
        break;
      case 4:
        assert(I + 3 < E && "Not enough elements.");
        VectorInfo[I] = PVF_FIRST;
        VectorInfo[I + 1] = PVF_INNER;
        VectorInfo[I + 2] = PVF_INNER;
        VectorInfo[I + 3] = PVF_LAST;
        I += 3;
        break;
      }
      // Break out of the inner loop because we've already succeeded
      // using largest possible AccessSize.
      break;
    }
  }
  return VectorInfo;
}

// NVPTXTargetLowering Constructor.
NVPTXTargetLowering::NVPTXTargetLowering(const NVPTXTargetMachine &TM,
                                         const NVPTXSubtarget &STI)
    : TargetLowering(TM), nvTM(&TM), STI(STI) {
  // always lower memset, memcpy, and memmove intrinsics to load/store
  // instructions, rather
  // then generating calls to memset, mempcy or memmove.
  MaxStoresPerMemset = MaxStoresPerMemsetOptSize = (unsigned)0xFFFFFFFF;
  MaxStoresPerMemcpy = MaxStoresPerMemcpyOptSize = (unsigned) 0xFFFFFFFF;
  MaxStoresPerMemmove = MaxStoresPerMemmoveOptSize = (unsigned) 0xFFFFFFFF;

  setBooleanContents(ZeroOrNegativeOneBooleanContent);
  setBooleanVectorContents(ZeroOrNegativeOneBooleanContent);

  // Jump is Expensive. Don't create extra control flow for 'and', 'or'
  // condition branches.
  setJumpIsExpensive(true);

  // Wide divides are _very_ slow. Try to reduce the width of the divide if
  // possible.
  addBypassSlowDiv(64, 32);

  // By default, use the Source scheduling
  if (sched4reg)
    setSchedulingPreference(Sched::RegPressure);
  else
    setSchedulingPreference(Sched::Source);

  auto setFP16OperationAction = [&](unsigned Op, MVT VT, LegalizeAction Action,
                                    LegalizeAction NoF16Action) {
    setOperationAction(Op, VT, STI.allowFP16Math() ? Action : NoF16Action);
  };

  auto setBF16OperationAction = [&](unsigned Op, MVT VT, LegalizeAction Action,
                                    LegalizeAction NoBF16Action) {
    bool IsOpSupported = STI.hasBF16Math();
    // Few instructions are available on sm_90 only
    switch(Op) {
      case ISD::FADD:
      case ISD::FMUL:
      case ISD::FSUB:
      case ISD::SELECT:
      case ISD::SELECT_CC:
      case ISD::SETCC:
      case ISD::FEXP2:
      case ISD::FCEIL:
      case ISD::FFLOOR:
      case ISD::FNEARBYINT:
      case ISD::FRINT:
      case ISD::FROUNDEVEN:
      case ISD::FTRUNC:
        IsOpSupported = STI.getSmVersion() >= 90 && STI.getPTXVersion() >= 78;
        break;
    }
    setOperationAction(
        Op, VT, IsOpSupported ? Action : NoBF16Action);
  };

  auto setI16x2OperationAction = [&](unsigned Op, MVT VT, LegalizeAction Action,
                                     LegalizeAction NoI16x2Action) {
    bool IsOpSupported = false;
    // instructions are available on sm_90 only
    switch (Op) {
    case ISD::ADD:
    case ISD::SMAX:
    case ISD::SMIN:
    case ISD::UMIN:
    case ISD::UMAX:
      IsOpSupported = STI.getSmVersion() >= 90 && STI.getPTXVersion() >= 80;
      break;
    }
    setOperationAction(Op, VT, IsOpSupported ? Action : NoI16x2Action);
  };

  addRegisterClass(MVT::i1, &NVPTX::Int1RegsRegClass);
  addRegisterClass(MVT::i16, &NVPTX::Int16RegsRegClass);
  addRegisterClass(MVT::v2i16, &NVPTX::Int32RegsRegClass);
  addRegisterClass(MVT::v4i8, &NVPTX::Int32RegsRegClass);
  addRegisterClass(MVT::i32, &NVPTX::Int32RegsRegClass);
  addRegisterClass(MVT::i64, &NVPTX::Int64RegsRegClass);
  addRegisterClass(MVT::f32, &NVPTX::Float32RegsRegClass);
  addRegisterClass(MVT::f64, &NVPTX::Float64RegsRegClass);
  addRegisterClass(MVT::f16, &NVPTX::Int16RegsRegClass);
  addRegisterClass(MVT::v2f16, &NVPTX::Int32RegsRegClass);
  addRegisterClass(MVT::bf16, &NVPTX::Int16RegsRegClass);
  addRegisterClass(MVT::v2bf16, &NVPTX::Int32RegsRegClass);

  // Conversion to/from FP16/FP16x2 is always legal.
  setOperationAction(ISD::BUILD_VECTOR, MVT::v2f16, Custom);
  setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v2f16, Custom);
  setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::v2f16, Expand);
  setOperationAction(ISD::VECTOR_SHUFFLE, MVT::v2f16, Expand);

  setOperationAction(ISD::READCYCLECOUNTER, MVT::i64, Legal);
  if (STI.getSmVersion() >= 30 && STI.getPTXVersion() > 31)
    setOperationAction(ISD::READSTEADYCOUNTER, MVT::i64, Legal);

  setFP16OperationAction(ISD::SETCC, MVT::f16, Legal, Promote);
  setFP16OperationAction(ISD::SETCC, MVT::v2f16, Legal, Expand);

  // Conversion to/from BFP16/BFP16x2 is always legal.
  setOperationAction(ISD::BUILD_VECTOR, MVT::v2bf16, Custom);
  setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v2bf16, Custom);
  setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::v2bf16, Expand);
  setOperationAction(ISD::VECTOR_SHUFFLE, MVT::v2bf16, Expand);

  setBF16OperationAction(ISD::SETCC, MVT::v2bf16, Legal, Expand);
  setBF16OperationAction(ISD::SETCC, MVT::bf16, Legal, Promote);
  if (getOperationAction(ISD::SETCC, MVT::bf16) == Promote)
    AddPromotedToType(ISD::SETCC, MVT::bf16, MVT::f32);

  // Conversion to/from i16/i16x2 is always legal.
  setOperationAction(ISD::BUILD_VECTOR, MVT::v2i16, Custom);
  setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v2i16, Custom);
  setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::v2i16, Expand);
  setOperationAction(ISD::VECTOR_SHUFFLE, MVT::v2i16, Expand);

  setOperationAction(ISD::BUILD_VECTOR, MVT::v4i8, Custom);
  setOperationAction(ISD::EXTRACT_VECTOR_ELT, MVT::v4i8, Custom);
  setOperationAction(ISD::INSERT_VECTOR_ELT, MVT::v4i8, Custom);
  setOperationAction(ISD::VECTOR_SHUFFLE, MVT::v4i8, Custom);
  // Only logical ops can be done on v4i8 directly, others must be done
  // elementwise.
  setOperationAction(
      {ISD::ABS,         ISD::ADD,        ISD::ADDC,        ISD::ADDE,
       ISD::BITREVERSE,  ISD::CTLZ,       ISD::CTPOP,       ISD::CTTZ,
       ISD::FP_TO_SINT,  ISD::FP_TO_UINT, ISD::FSHL,        ISD::FSHR,
       ISD::MUL,         ISD::MULHS,      ISD::MULHU,       ISD::PARITY,
       ISD::ROTL,        ISD::ROTR,       ISD::SADDO,       ISD::SADDO_CARRY,
       ISD::SADDSAT,     ISD::SDIV,       ISD::SDIVREM,     ISD::SELECT_CC,
       ISD::SETCC,       ISD::SHL,        ISD::SINT_TO_FP,  ISD::SMAX,
       ISD::SMIN,        ISD::SMULO,      ISD::SMUL_LOHI,   ISD::SRA,
       ISD::SREM,        ISD::SRL,        ISD::SSHLSAT,     ISD::SSUBO,
       ISD::SSUBO_CARRY, ISD::SSUBSAT,    ISD::SUB,         ISD::SUBC,
       ISD::SUBE,        ISD::UADDO,      ISD::UADDO_CARRY, ISD::UADDSAT,
       ISD::UDIV,        ISD::UDIVREM,    ISD::UINT_TO_FP,  ISD::UMAX,
       ISD::UMIN,        ISD::UMULO,      ISD::UMUL_LOHI,   ISD::UREM,
       ISD::USHLSAT,     ISD::USUBO,      ISD::USUBO_CARRY, ISD::VSELECT,
       ISD::USUBSAT},
      MVT::v4i8, Expand);

  // Operations not directly supported by NVPTX.
  for (MVT VT : {MVT::bf16, MVT::f16, MVT::v2bf16, MVT::v2f16, MVT::f32,
                 MVT::f64, MVT::i1, MVT::i8, MVT::i16, MVT::v2i16, MVT::v4i8,
                 MVT::i32, MVT::i64}) {
    setOperationAction(ISD::SELECT_CC, VT, Expand);
    setOperationAction(ISD::BR_CC, VT, Expand);
  }

  // Some SIGN_EXTEND_INREG can be done using cvt instruction.
  // For others we will expand to a SHL/SRA pair.
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i64, Legal);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i32, Legal);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i16, Legal);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i8 , Legal);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::v2i16, Expand);

  setOperationAction(ISD::SHL_PARTS, MVT::i32  , Custom);
  setOperationAction(ISD::SRA_PARTS, MVT::i32  , Custom);
  setOperationAction(ISD::SRL_PARTS, MVT::i32  , Custom);
  setOperationAction(ISD::SHL_PARTS, MVT::i64  , Custom);
  setOperationAction(ISD::SRA_PARTS, MVT::i64  , Custom);
  setOperationAction(ISD::SRL_PARTS, MVT::i64  , Custom);

  setOperationAction(ISD::BITREVERSE, MVT::i32, Legal);
  setOperationAction(ISD::BITREVERSE, MVT::i64, Legal);

  // TODO: we may consider expanding ROTL/ROTR on older GPUs.  Currently on GPUs
  // that don't have h/w rotation we lower them to multi-instruction assembly.
  // See ROT*_sw in NVPTXIntrInfo.td
  setOperationAction(ISD::ROTL, MVT::i64, Legal);
  setOperationAction(ISD::ROTR, MVT::i64, Legal);
  setOperationAction(ISD::ROTL, MVT::i32, Legal);
  setOperationAction(ISD::ROTR, MVT::i32, Legal);

  setOperationAction(ISD::ROTL, MVT::i16, Expand);
  setOperationAction(ISD::ROTL, MVT::v2i16, Expand);
  setOperationAction(ISD::ROTR, MVT::i16, Expand);
  setOperationAction(ISD::ROTR, MVT::v2i16, Expand);
  setOperationAction(ISD::ROTL, MVT::i8, Expand);
  setOperationAction(ISD::ROTR, MVT::i8, Expand);
  setOperationAction(ISD::BSWAP, MVT::i16, Expand);

  // Indirect branch is not supported.
  // This also disables Jump Table creation.
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::BRIND, MVT::Other, Expand);

  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);
  setOperationAction(ISD::GlobalAddress, MVT::i64, Custom);

  // We want to legalize constant related memmove and memcopy
  // intrinsics.
  setOperationAction(ISD::INTRINSIC_W_CHAIN, MVT::Other, Custom);

  // Turn FP extload into load/fpextend
  setLoadExtAction(ISD::EXTLOAD, MVT::f32, MVT::f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::f32, MVT::bf16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::bf16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f32, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v2f32, MVT::v2f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v2f64, MVT::v2f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v2f32, MVT::v2bf16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v2f64, MVT::v2bf16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v2f64, MVT::v2f32, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v4f32, MVT::v4f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v4f64, MVT::v4f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v4f32, MVT::v4bf16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v4f64, MVT::v4bf16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v4f64, MVT::v4f32, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v8f32, MVT::v8f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v8f64, MVT::v8f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v8f32, MVT::v8bf16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v8f64, MVT::v8bf16, Expand);
  // Turn FP truncstore into trunc + store.
  // FIXME: vector types should also be expanded
  setTruncStoreAction(MVT::f32, MVT::f16, Expand);
  setTruncStoreAction(MVT::f64, MVT::f16, Expand);
  setTruncStoreAction(MVT::f32, MVT::bf16, Expand);
  setTruncStoreAction(MVT::f64, MVT::bf16, Expand);
  setTruncStoreAction(MVT::f64, MVT::f32, Expand);

  // PTX does not support load / store predicate registers
  setOperationAction(ISD::LOAD, MVT::i1, Custom);
  setOperationAction(ISD::STORE, MVT::i1, Custom);

  for (MVT VT : MVT::integer_valuetypes()) {
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::ZEXTLOAD, VT, MVT::i1, Promote);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::i1, Promote);
    setTruncStoreAction(VT, MVT::i1, Expand);
  }

  // expand extload of vector of integers.
  setLoadExtAction({ISD::EXTLOAD, ISD::SEXTLOAD, ISD::ZEXTLOAD}, MVT::v2i16,
                   MVT::v2i8, Expand);
  setTruncStoreAction(MVT::v2i16, MVT::v2i8, Expand);

  // This is legal in NVPTX
  setOperationAction(ISD::ConstantFP, MVT::f64, Legal);
  setOperationAction(ISD::ConstantFP, MVT::f32, Legal);
  setOperationAction(ISD::ConstantFP, MVT::f16, Legal);
  setOperationAction(ISD::ConstantFP, MVT::bf16, Legal);

  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32, Custom);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i64, Custom);

  // TRAP can be lowered to PTX trap
  setOperationAction(ISD::TRAP, MVT::Other, Legal);

  // Register custom handling for vector loads/stores
  for (MVT VT : MVT::fixedlen_vector_valuetypes()) {
    if (IsPTXVectorType(VT)) {
      setOperationAction(ISD::LOAD, VT, Custom);
      setOperationAction(ISD::STORE, VT, Custom);
      setOperationAction(ISD::INTRINSIC_W_CHAIN, VT, Custom);
    }
  }

  // Support varargs.
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction(ISD::VAARG, MVT::Other, Custom);
  setOperationAction(ISD::VACOPY, MVT::Other, Expand);
  setOperationAction(ISD::VAEND, MVT::Other, Expand);

  // Custom handling for i8 intrinsics
  setOperationAction(ISD::INTRINSIC_W_CHAIN, MVT::i8, Custom);

  for (const auto& Ty : {MVT::i16, MVT::i32, MVT::i64}) {
    setOperationAction(ISD::ABS,  Ty, Legal);
    setOperationAction(ISD::SMIN, Ty, Legal);
    setOperationAction(ISD::SMAX, Ty, Legal);
    setOperationAction(ISD::UMIN, Ty, Legal);
    setOperationAction(ISD::UMAX, Ty, Legal);

    setOperationAction(ISD::CTPOP, Ty, Legal);
    setOperationAction(ISD::CTLZ, Ty, Legal);
  }

  setI16x2OperationAction(ISD::ABS, MVT::v2i16, Legal, Custom);
  setI16x2OperationAction(ISD::SMIN, MVT::v2i16, Legal, Custom);
  setI16x2OperationAction(ISD::SMAX, MVT::v2i16, Legal, Custom);
  setI16x2OperationAction(ISD::UMIN, MVT::v2i16, Legal, Custom);
  setI16x2OperationAction(ISD::UMAX, MVT::v2i16, Legal, Custom);
  setI16x2OperationAction(ISD::CTPOP, MVT::v2i16, Legal, Expand);
  setI16x2OperationAction(ISD::CTLZ, MVT::v2i16, Legal, Expand);

  setI16x2OperationAction(ISD::ADD, MVT::v2i16, Legal, Custom);
  setI16x2OperationAction(ISD::SUB, MVT::v2i16, Legal, Custom);
  setI16x2OperationAction(ISD::MUL, MVT::v2i16, Legal, Custom);
  setI16x2OperationAction(ISD::SHL, MVT::v2i16, Legal, Custom);
  setI16x2OperationAction(ISD::SREM, MVT::v2i16, Legal, Custom);
  setI16x2OperationAction(ISD::UREM, MVT::v2i16, Legal, Custom);

  // Other arithmetic and logic ops are unsupported.
  setOperationAction({ISD::SDIV, ISD::UDIV, ISD::SRA, ISD::SRL, ISD::MULHS,
                      ISD::MULHU, ISD::FP_TO_SINT, ISD::FP_TO_UINT,
                      ISD::SINT_TO_FP, ISD::UINT_TO_FP},
                     MVT::v2i16, Expand);

  setOperationAction(ISD::ADDC, MVT::i32, Legal);
  setOperationAction(ISD::ADDE, MVT::i32, Legal);
  setOperationAction(ISD::SUBC, MVT::i32, Legal);
  setOperationAction(ISD::SUBE, MVT::i32, Legal);
  if (STI.getPTXVersion() >= 43) {
    setOperationAction(ISD::ADDC, MVT::i64, Legal);
    setOperationAction(ISD::ADDE, MVT::i64, Legal);
    setOperationAction(ISD::SUBC, MVT::i64, Legal);
    setOperationAction(ISD::SUBE, MVT::i64, Legal);
  }

  setOperationAction(ISD::CTTZ, MVT::i16, Expand);
  setOperationAction(ISD::CTTZ, MVT::v2i16, Expand);
  setOperationAction(ISD::CTTZ, MVT::i32, Expand);
  setOperationAction(ISD::CTTZ, MVT::i64, Expand);

  // PTX does not directly support SELP of i1, so promote to i32 first
  setOperationAction(ISD::SELECT, MVT::i1, Custom);

  // PTX cannot multiply two i64s in a single instruction.
  setOperationAction(ISD::SMUL_LOHI, MVT::i64, Expand);
  setOperationAction(ISD::UMUL_LOHI, MVT::i64, Expand);

  // We have some custom DAG combine patterns for these nodes
  setTargetDAGCombine({ISD::ADD, ISD::AND, ISD::EXTRACT_VECTOR_ELT, ISD::FADD,
                       ISD::LOAD, ISD::MUL, ISD::SHL, ISD::SREM, ISD::UREM,
                       ISD::VSELECT});

  // setcc for f16x2 and bf16x2 needs special handling to prevent
  // legalizer's attempt to scalarize it due to v2i1 not being legal.
  if (STI.allowFP16Math() || STI.hasBF16Math())
    setTargetDAGCombine(ISD::SETCC);

  // Promote fp16 arithmetic if fp16 hardware isn't available or the
  // user passed --nvptx-no-fp16-math. The flag is useful because,
  // although sm_53+ GPUs have some sort of FP16 support in
  // hardware, only sm_53 and sm_60 have full implementation. Others
  // only have token amount of hardware and are likely to run faster
  // by using fp32 units instead.
  for (const auto &Op : {ISD::FADD, ISD::FMUL, ISD::FSUB, ISD::FMA}) {
    setFP16OperationAction(Op, MVT::f16, Legal, Promote);
    setFP16OperationAction(Op, MVT::v2f16, Legal, Expand);
    setBF16OperationAction(Op, MVT::v2bf16, Legal, Expand);
    // bf16 must be promoted to f32.
    setBF16OperationAction(Op, MVT::bf16, Legal, Promote);
    if (getOperationAction(Op, MVT::bf16) == Promote)
      AddPromotedToType(Op, MVT::bf16, MVT::f32);
  }

  // f16/f16x2 neg was introduced in PTX 60, SM_53.
  const bool IsFP16FP16x2NegAvailable = STI.getSmVersion() >= 53 &&
                                        STI.getPTXVersion() >= 60 &&
                                        STI.allowFP16Math();
  for (const auto &VT : {MVT::f16, MVT::v2f16})
    setOperationAction(ISD::FNEG, VT,
                       IsFP16FP16x2NegAvailable ? Legal : Expand);

  setBF16OperationAction(ISD::FNEG, MVT::bf16, Legal, Expand);
  setBF16OperationAction(ISD::FNEG, MVT::v2bf16, Legal, Expand);
  // (would be) Library functions.

  // These map to conversion instructions for scalar FP types.
  for (const auto &Op : {ISD::FCEIL, ISD::FFLOOR, ISD::FNEARBYINT, ISD::FRINT,
                         ISD::FROUNDEVEN, ISD::FTRUNC}) {
    setOperationAction(Op, MVT::f16, Legal);
    setOperationAction(Op, MVT::f32, Legal);
    setOperationAction(Op, MVT::f64, Legal);
    setOperationAction(Op, MVT::v2f16, Expand);
    setOperationAction(Op, MVT::v2bf16, Expand);
    setBF16OperationAction(Op, MVT::bf16, Legal, Promote);
    if (getOperationAction(Op, MVT::bf16) == Promote)
      AddPromotedToType(Op, MVT::bf16, MVT::f32);
  }

  if (STI.getSmVersion() < 80 || STI.getPTXVersion() < 71) {
    setOperationAction(ISD::BF16_TO_FP, MVT::f32, Expand);
  }
  if (STI.getSmVersion() < 90 || STI.getPTXVersion() < 78) {
    for (MVT VT : {MVT::bf16, MVT::f32, MVT::f64}) {
      setOperationAction(ISD::FP_EXTEND, VT, Custom);
      setOperationAction(ISD::FP_ROUND, VT, Custom);
    }
  }

  // sm_80 only has conversions between f32 and bf16. Custom lower all other
  // bf16 conversions.
  if (STI.getSmVersion() < 90 || STI.getPTXVersion() < 78) {
    for (MVT VT : {MVT::i1, MVT::i16, MVT::i32, MVT::i64}) {
      setOperationAction(
          {ISD::SINT_TO_FP, ISD::UINT_TO_FP, ISD::FP_TO_SINT, ISD::FP_TO_UINT},
          VT, Custom);
    }
    setOperationAction(
        {ISD::SINT_TO_FP, ISD::UINT_TO_FP, ISD::FP_TO_SINT, ISD::FP_TO_UINT},
        MVT::bf16, Custom);
  }

  setOperationAction(ISD::FROUND, MVT::f16, Promote);
  setOperationAction(ISD::FROUND, MVT::v2f16, Expand);
  setOperationAction(ISD::FROUND, MVT::v2bf16, Expand);
  setOperationAction(ISD::FROUND, MVT::f32, Custom);
  setOperationAction(ISD::FROUND, MVT::f64, Custom);
  setOperationAction(ISD::FROUND, MVT::bf16, Promote);
  AddPromotedToType(ISD::FROUND, MVT::bf16, MVT::f32);

  // 'Expand' implements FCOPYSIGN without calling an external library.
  setOperationAction(ISD::FCOPYSIGN, MVT::f16, Expand);
  setOperationAction(ISD::FCOPYSIGN, MVT::v2f16, Expand);
  setOperationAction(ISD::FCOPYSIGN, MVT::bf16, Expand);
  setOperationAction(ISD::FCOPYSIGN, MVT::v2bf16, Expand);
  setOperationAction(ISD::FCOPYSIGN, MVT::f32, Expand);
  setOperationAction(ISD::FCOPYSIGN, MVT::f64, Expand);

  // These map to corresponding instructions for f32/f64. f16 must be
  // promoted to f32. v2f16 is expanded to f16, which is then promoted
  // to f32.
  for (const auto &Op :
       {ISD::FDIV, ISD::FREM, ISD::FSQRT, ISD::FSIN, ISD::FCOS}) {
    setOperationAction(Op, MVT::f16, Promote);
    setOperationAction(Op, MVT::f32, Legal);
    setOperationAction(Op, MVT::f64, Legal);
    setOperationAction(Op, MVT::v2f16, Expand);
    setOperationAction(Op, MVT::v2bf16, Expand);
    setOperationAction(Op, MVT::bf16, Promote);
    AddPromotedToType(Op, MVT::bf16, MVT::f32);
  }
  for (const auto &Op : {ISD::FABS}) {
    setOperationAction(Op, MVT::f16, Promote);
    setOperationAction(Op, MVT::f32, Legal);
    setOperationAction(Op, MVT::f64, Legal);
    setOperationAction(Op, MVT::v2f16, Expand);
    setBF16OperationAction(Op, MVT::v2bf16, Legal, Expand);
    setBF16OperationAction(Op, MVT::bf16, Legal, Promote);
    if (getOperationAction(Op, MVT::bf16) == Promote)
      AddPromotedToType(Op, MVT::bf16, MVT::f32);
  }

  // max.f16, max.f16x2 and max.NaN are supported on sm_80+.
  auto GetMinMaxAction = [&](LegalizeAction NotSm80Action) {
    bool IsAtLeastSm80 = STI.getSmVersion() >= 80 && STI.getPTXVersion() >= 70;
    return IsAtLeastSm80 ? Legal : NotSm80Action;
  };
  for (const auto &Op : {ISD::FMINNUM, ISD::FMAXNUM}) {
    setFP16OperationAction(Op, MVT::f16, GetMinMaxAction(Promote), Promote);
    setOperationAction(Op, MVT::f32, Legal);
    setOperationAction(Op, MVT::f64, Legal);
    setFP16OperationAction(Op, MVT::v2f16, GetMinMaxAction(Expand), Expand);
    setBF16OperationAction(Op, MVT::v2bf16, Legal, Expand);
    setBF16OperationAction(Op, MVT::bf16, Legal, Promote);
    if (getOperationAction(Op, MVT::bf16) == Promote)
      AddPromotedToType(Op, MVT::bf16, MVT::f32);
  }
  for (const auto &Op : {ISD::FMINIMUM, ISD::FMAXIMUM}) {
    setFP16OperationAction(Op, MVT::f16, GetMinMaxAction(Expand), Expand);
    setFP16OperationAction(Op, MVT::bf16, Legal, Expand);
    setOperationAction(Op, MVT::f32, GetMinMaxAction(Expand));
    setFP16OperationAction(Op, MVT::v2f16, GetMinMaxAction(Expand), Expand);
    setBF16OperationAction(Op, MVT::v2bf16, Legal, Expand);
  }

  // Custom lowering for inline asm with 128-bit operands
  setOperationAction(ISD::CopyToReg, MVT::i128, Custom);
  setOperationAction(ISD::CopyFromReg, MVT::i128, Custom);

  // No FEXP2, FLOG2.  The PTX ex2 and log2 functions are always approximate.
  // No FPOW or FREM in PTX.

  // Now deduce the information based on the above mentioned
  // actions
  computeRegisterProperties(STI.getRegisterInfo());

  setMinCmpXchgSizeInBits(32);
  setMaxAtomicSizeInBitsSupported(64);
  setMaxDivRemBitWidthSupported(64);
}

const char *NVPTXTargetLowering::getTargetNodeName(unsigned Opcode) const {

#define MAKE_CASE(V)                                                           \
  case V:                                                                      \
    return #V;

  switch ((NVPTXISD::NodeType)Opcode) {
  case NVPTXISD::FIRST_NUMBER:
    break;

    MAKE_CASE(NVPTXISD::CALL)
    MAKE_CASE(NVPTXISD::RET_GLUE)
    MAKE_CASE(NVPTXISD::LOAD_PARAM)
    MAKE_CASE(NVPTXISD::Wrapper)
    MAKE_CASE(NVPTXISD::DeclareParam)
    MAKE_CASE(NVPTXISD::DeclareScalarParam)
    MAKE_CASE(NVPTXISD::DeclareRet)
    MAKE_CASE(NVPTXISD::DeclareScalarRet)
    MAKE_CASE(NVPTXISD::DeclareRetParam)
    MAKE_CASE(NVPTXISD::PrintCall)
    MAKE_CASE(NVPTXISD::PrintConvergentCall)
    MAKE_CASE(NVPTXISD::PrintCallUni)
    MAKE_CASE(NVPTXISD::PrintConvergentCallUni)
    MAKE_CASE(NVPTXISD::LoadParam)
    MAKE_CASE(NVPTXISD::LoadParamV2)
    MAKE_CASE(NVPTXISD::LoadParamV4)
    MAKE_CASE(NVPTXISD::StoreParam)
    MAKE_CASE(NVPTXISD::StoreParamV2)
    MAKE_CASE(NVPTXISD::StoreParamV4)
    MAKE_CASE(NVPTXISD::StoreParamS32)
    MAKE_CASE(NVPTXISD::StoreParamU32)
    MAKE_CASE(NVPTXISD::CallArgBegin)
    MAKE_CASE(NVPTXISD::CallArg)
    MAKE_CASE(NVPTXISD::LastCallArg)
    MAKE_CASE(NVPTXISD::CallArgEnd)
    MAKE_CASE(NVPTXISD::CallVoid)
    MAKE_CASE(NVPTXISD::CallVal)
    MAKE_CASE(NVPTXISD::CallSymbol)
    MAKE_CASE(NVPTXISD::Prototype)
    MAKE_CASE(NVPTXISD::MoveParam)
    MAKE_CASE(NVPTXISD::StoreRetval)
    MAKE_CASE(NVPTXISD::StoreRetvalV2)
    MAKE_CASE(NVPTXISD::StoreRetvalV4)
    MAKE_CASE(NVPTXISD::PseudoUseParam)
    MAKE_CASE(NVPTXISD::RETURN)
    MAKE_CASE(NVPTXISD::CallSeqBegin)
    MAKE_CASE(NVPTXISD::CallSeqEnd)
    MAKE_CASE(NVPTXISD::CallPrototype)
    MAKE_CASE(NVPTXISD::ProxyReg)
    MAKE_CASE(NVPTXISD::LoadV2)
    MAKE_CASE(NVPTXISD::LoadV4)
    MAKE_CASE(NVPTXISD::LDGV2)
    MAKE_CASE(NVPTXISD::LDGV4)
    MAKE_CASE(NVPTXISD::LDUV2)
    MAKE_CASE(NVPTXISD::LDUV4)
    MAKE_CASE(NVPTXISD::StoreV2)
    MAKE_CASE(NVPTXISD::StoreV4)
    MAKE_CASE(NVPTXISD::FUN_SHFL_CLAMP)
    MAKE_CASE(NVPTXISD::FUN_SHFR_CLAMP)
    MAKE_CASE(NVPTXISD::IMAD)
    MAKE_CASE(NVPTXISD::BFE)
    MAKE_CASE(NVPTXISD::BFI)
    MAKE_CASE(NVPTXISD::PRMT)
    MAKE_CASE(NVPTXISD::DYNAMIC_STACKALLOC)
    MAKE_CASE(NVPTXISD::SETP_F16X2)
    MAKE_CASE(NVPTXISD::SETP_BF16X2)
    MAKE_CASE(NVPTXISD::Dummy)
    MAKE_CASE(NVPTXISD::MUL_WIDE_SIGNED)
    MAKE_CASE(NVPTXISD::MUL_WIDE_UNSIGNED)
    MAKE_CASE(NVPTXISD::Tex1DFloatS32)
    MAKE_CASE(NVPTXISD::Tex1DFloatFloat)
    MAKE_CASE(NVPTXISD::Tex1DFloatFloatLevel)
    MAKE_CASE(NVPTXISD::Tex1DFloatFloatGrad)
    MAKE_CASE(NVPTXISD::Tex1DS32S32)
    MAKE_CASE(NVPTXISD::Tex1DS32Float)
    MAKE_CASE(NVPTXISD::Tex1DS32FloatLevel)
    MAKE_CASE(NVPTXISD::Tex1DS32FloatGrad)
    MAKE_CASE(NVPTXISD::Tex1DU32S32)
    MAKE_CASE(NVPTXISD::Tex1DU32Float)
    MAKE_CASE(NVPTXISD::Tex1DU32FloatLevel)
    MAKE_CASE(NVPTXISD::Tex1DU32FloatGrad)
    MAKE_CASE(NVPTXISD::Tex1DArrayFloatS32)
    MAKE_CASE(NVPTXISD::Tex1DArrayFloatFloat)
    MAKE_CASE(NVPTXISD::Tex1DArrayFloatFloatLevel)
    MAKE_CASE(NVPTXISD::Tex1DArrayFloatFloatGrad)
    MAKE_CASE(NVPTXISD::Tex1DArrayS32S32)
    MAKE_CASE(NVPTXISD::Tex1DArrayS32Float)
    MAKE_CASE(NVPTXISD::Tex1DArrayS32FloatLevel)
    MAKE_CASE(NVPTXISD::Tex1DArrayS32FloatGrad)
    MAKE_CASE(NVPTXISD::Tex1DArrayU32S32)
    MAKE_CASE(NVPTXISD::Tex1DArrayU32Float)
    MAKE_CASE(NVPTXISD::Tex1DArrayU32FloatLevel)
    MAKE_CASE(NVPTXISD::Tex1DArrayU32FloatGrad)
    MAKE_CASE(NVPTXISD::Tex2DFloatS32)
    MAKE_CASE(NVPTXISD::Tex2DFloatFloat)
    MAKE_CASE(NVPTXISD::Tex2DFloatFloatLevel)
    MAKE_CASE(NVPTXISD::Tex2DFloatFloatGrad)
    MAKE_CASE(NVPTXISD::Tex2DS32S32)
    MAKE_CASE(NVPTXISD::Tex2DS32Float)
    MAKE_CASE(NVPTXISD::Tex2DS32FloatLevel)
    MAKE_CASE(NVPTXISD::Tex2DS32FloatGrad)
    MAKE_CASE(NVPTXISD::Tex2DU32S32)
    MAKE_CASE(NVPTXISD::Tex2DU32Float)
    MAKE_CASE(NVPTXISD::Tex2DU32FloatLevel)
    MAKE_CASE(NVPTXISD::Tex2DU32FloatGrad)
    MAKE_CASE(NVPTXISD::Tex2DArrayFloatS32)
    MAKE_CASE(NVPTXISD::Tex2DArrayFloatFloat)
    MAKE_CASE(NVPTXISD::Tex2DArrayFloatFloatLevel)
    MAKE_CASE(NVPTXISD::Tex2DArrayFloatFloatGrad)
    MAKE_CASE(NVPTXISD::Tex2DArrayS32S32)
    MAKE_CASE(NVPTXISD::Tex2DArrayS32Float)
    MAKE_CASE(NVPTXISD::Tex2DArrayS32FloatLevel)
    MAKE_CASE(NVPTXISD::Tex2DArrayS32FloatGrad)
    MAKE_CASE(NVPTXISD::Tex2DArrayU32S32)
    MAKE_CASE(NVPTXISD::Tex2DArrayU32Float)
    MAKE_CASE(NVPTXISD::Tex2DArrayU32FloatLevel)
    MAKE_CASE(NVPTXISD::Tex2DArrayU32FloatGrad)
    MAKE_CASE(NVPTXISD::Tex3DFloatS32)
    MAKE_CASE(NVPTXISD::Tex3DFloatFloat)
    MAKE_CASE(NVPTXISD::Tex3DFloatFloatLevel)
    MAKE_CASE(NVPTXISD::Tex3DFloatFloatGrad)
    MAKE_CASE(NVPTXISD::Tex3DS32S32)
    MAKE_CASE(NVPTXISD::Tex3DS32Float)
    MAKE_CASE(NVPTXISD::Tex3DS32FloatLevel)
    MAKE_CASE(NVPTXISD::Tex3DS32FloatGrad)
    MAKE_CASE(NVPTXISD::Tex3DU32S32)
    MAKE_CASE(NVPTXISD::Tex3DU32Float)
    MAKE_CASE(NVPTXISD::Tex3DU32FloatLevel)
    MAKE_CASE(NVPTXISD::Tex3DU32FloatGrad)
    MAKE_CASE(NVPTXISD::TexCubeFloatFloat)
    MAKE_CASE(NVPTXISD::TexCubeFloatFloatLevel)
    MAKE_CASE(NVPTXISD::TexCubeS32Float)
    MAKE_CASE(NVPTXISD::TexCubeS32FloatLevel)
    MAKE_CASE(NVPTXISD::TexCubeU32Float)
    MAKE_CASE(NVPTXISD::TexCubeU32FloatLevel)
    MAKE_CASE(NVPTXISD::TexCubeArrayFloatFloat)
    MAKE_CASE(NVPTXISD::TexCubeArrayFloatFloatLevel)
    MAKE_CASE(NVPTXISD::TexCubeArrayS32Float)
    MAKE_CASE(NVPTXISD::TexCubeArrayS32FloatLevel)
    MAKE_CASE(NVPTXISD::TexCubeArrayU32Float)
    MAKE_CASE(NVPTXISD::TexCubeArrayU32FloatLevel)
    MAKE_CASE(NVPTXISD::Tld4R2DFloatFloat)
    MAKE_CASE(NVPTXISD::Tld4G2DFloatFloat)
    MAKE_CASE(NVPTXISD::Tld4B2DFloatFloat)
    MAKE_CASE(NVPTXISD::Tld4A2DFloatFloat)
    MAKE_CASE(NVPTXISD::Tld4R2DS64Float)
    MAKE_CASE(NVPTXISD::Tld4G2DS64Float)
    MAKE_CASE(NVPTXISD::Tld4B2DS64Float)
    MAKE_CASE(NVPTXISD::Tld4A2DS64Float)
    MAKE_CASE(NVPTXISD::Tld4R2DU64Float)
    MAKE_CASE(NVPTXISD::Tld4G2DU64Float)
    MAKE_CASE(NVPTXISD::Tld4B2DU64Float)
    MAKE_CASE(NVPTXISD::Tld4A2DU64Float)

    MAKE_CASE(NVPTXISD::TexUnified1DFloatS32)
    MAKE_CASE(NVPTXISD::TexUnified1DFloatFloat)
    MAKE_CASE(NVPTXISD::TexUnified1DFloatFloatLevel)
    MAKE_CASE(NVPTXISD::TexUnified1DFloatFloatGrad)
    MAKE_CASE(NVPTXISD::TexUnified1DS32S32)
    MAKE_CASE(NVPTXISD::TexUnified1DS32Float)
    MAKE_CASE(NVPTXISD::TexUnified1DS32FloatLevel)
    MAKE_CASE(NVPTXISD::TexUnified1DS32FloatGrad)
    MAKE_CASE(NVPTXISD::TexUnified1DU32S32)
    MAKE_CASE(NVPTXISD::TexUnified1DU32Float)
    MAKE_CASE(NVPTXISD::TexUnified1DU32FloatLevel)
    MAKE_CASE(NVPTXISD::TexUnified1DU32FloatGrad)
    MAKE_CASE(NVPTXISD::TexUnified1DArrayFloatS32)
    MAKE_CASE(NVPTXISD::TexUnified1DArrayFloatFloat)
    MAKE_CASE(NVPTXISD::TexUnified1DArrayFloatFloatLevel)
    MAKE_CASE(NVPTXISD::TexUnified1DArrayFloatFloatGrad)
    MAKE_CASE(NVPTXISD::TexUnified1DArrayS32S32)
    MAKE_CASE(NVPTXISD::TexUnified1DArrayS32Float)
    MAKE_CASE(NVPTXISD::TexUnified1DArrayS32FloatLevel)
    MAKE_CASE(NVPTXISD::TexUnified1DArrayS32FloatGrad)
    MAKE_CASE(NVPTXISD::TexUnified1DArrayU32S32)
    MAKE_CASE(NVPTXISD::TexUnified1DArrayU32Float)
    MAKE_CASE(NVPTXISD::TexUnified1DArrayU32FloatLevel)
    MAKE_CASE(NVPTXISD::TexUnified1DArrayU32FloatGrad)
    MAKE_CASE(NVPTXISD::TexUnified2DFloatS32)
    MAKE_CASE(NVPTXISD::TexUnified2DFloatFloat)
    MAKE_CASE(NVPTXISD::TexUnified2DFloatFloatLevel)
    MAKE_CASE(NVPTXISD::TexUnified2DFloatFloatGrad)
    MAKE_CASE(NVPTXISD::TexUnified2DS32S32)
    MAKE_CASE(NVPTXISD::TexUnified2DS32Float)
    MAKE_CASE(NVPTXISD::TexUnified2DS32FloatLevel)
    MAKE_CASE(NVPTXISD::TexUnified2DS32FloatGrad)
    MAKE_CASE(NVPTXISD::TexUnified2DU32S32)
    MAKE_CASE(NVPTXISD::TexUnified2DU32Float)
    MAKE_CASE(NVPTXISD::TexUnified2DU32FloatLevel)
    MAKE_CASE(NVPTXISD::TexUnified2DU32FloatGrad)
    MAKE_CASE(NVPTXISD::TexUnified2DArrayFloatS32)
    MAKE_CASE(NVPTXISD::TexUnified2DArrayFloatFloat)
    MAKE_CASE(NVPTXISD::TexUnified2DArrayFloatFloatLevel)
    MAKE_CASE(NVPTXISD::TexUnified2DArrayFloatFloatGrad)
    MAKE_CASE(NVPTXISD::TexUnified2DArrayS32S32)
    MAKE_CASE(NVPTXISD::TexUnified2DArrayS32Float)
    MAKE_CASE(NVPTXISD::TexUnified2DArrayS32FloatLevel)
    MAKE_CASE(NVPTXISD::TexUnified2DArrayS32FloatGrad)
    MAKE_CASE(NVPTXISD::TexUnified2DArrayU32S32)
    MAKE_CASE(NVPTXISD::TexUnified2DArrayU32Float)
    MAKE_CASE(NVPTXISD::TexUnified2DArrayU32FloatLevel)
    MAKE_CASE(NVPTXISD::TexUnified2DArrayU32FloatGrad)
    MAKE_CASE(NVPTXISD::TexUnified3DFloatS32)
    MAKE_CASE(NVPTXISD::TexUnified3DFloatFloat)
    MAKE_CASE(NVPTXISD::TexUnified3DFloatFloatLevel)
    MAKE_CASE(NVPTXISD::TexUnified3DFloatFloatGrad)
    MAKE_CASE(NVPTXISD::TexUnified3DS32S32)
    MAKE_CASE(NVPTXISD::TexUnified3DS32Float)
    MAKE_CASE(NVPTXISD::TexUnified3DS32FloatLevel)
    MAKE_CASE(NVPTXISD::TexUnified3DS32FloatGrad)
    MAKE_CASE(NVPTXISD::TexUnified3DU32S32)
    MAKE_CASE(NVPTXISD::TexUnified3DU32Float)
    MAKE_CASE(NVPTXISD::TexUnified3DU32FloatLevel)
    MAKE_CASE(NVPTXISD::TexUnified3DU32FloatGrad)
    MAKE_CASE(NVPTXISD::TexUnifiedCubeFloatFloat)
    MAKE_CASE(NVPTXISD::TexUnifiedCubeFloatFloatLevel)
    MAKE_CASE(NVPTXISD::TexUnifiedCubeS32Float)
    MAKE_CASE(NVPTXISD::TexUnifiedCubeS32FloatLevel)
    MAKE_CASE(NVPTXISD::TexUnifiedCubeU32Float)
    MAKE_CASE(NVPTXISD::TexUnifiedCubeU32FloatLevel)
    MAKE_CASE(NVPTXISD::TexUnifiedCubeArrayFloatFloat)
    MAKE_CASE(NVPTXISD::TexUnifiedCubeArrayFloatFloatLevel)
    MAKE_CASE(NVPTXISD::TexUnifiedCubeArrayS32Float)
    MAKE_CASE(NVPTXISD::TexUnifiedCubeArrayS32FloatLevel)
    MAKE_CASE(NVPTXISD::TexUnifiedCubeArrayU32Float)
    MAKE_CASE(NVPTXISD::TexUnifiedCubeArrayU32FloatLevel)
    MAKE_CASE(NVPTXISD::TexUnifiedCubeFloatFloatGrad)
    MAKE_CASE(NVPTXISD::TexUnifiedCubeS32FloatGrad)
    MAKE_CASE(NVPTXISD::TexUnifiedCubeU32FloatGrad)
    MAKE_CASE(NVPTXISD::TexUnifiedCubeArrayFloatFloatGrad)
    MAKE_CASE(NVPTXISD::TexUnifiedCubeArrayS32FloatGrad)
    MAKE_CASE(NVPTXISD::TexUnifiedCubeArrayU32FloatGrad)
    MAKE_CASE(NVPTXISD::Tld4UnifiedR2DFloatFloat)
    MAKE_CASE(NVPTXISD::Tld4UnifiedG2DFloatFloat)
    MAKE_CASE(NVPTXISD::Tld4UnifiedB2DFloatFloat)
    MAKE_CASE(NVPTXISD::Tld4UnifiedA2DFloatFloat)
    MAKE_CASE(NVPTXISD::Tld4UnifiedR2DS64Float)
    MAKE_CASE(NVPTXISD::Tld4UnifiedG2DS64Float)
    MAKE_CASE(NVPTXISD::Tld4UnifiedB2DS64Float)
    MAKE_CASE(NVPTXISD::Tld4UnifiedA2DS64Float)
    MAKE_CASE(NVPTXISD::Tld4UnifiedR2DU64Float)
    MAKE_CASE(NVPTXISD::Tld4UnifiedG2DU64Float)
    MAKE_CASE(NVPTXISD::Tld4UnifiedB2DU64Float)
    MAKE_CASE(NVPTXISD::Tld4UnifiedA2DU64Float)

    MAKE_CASE(NVPTXISD::Suld1DI8Clamp)
    MAKE_CASE(NVPTXISD::Suld1DI16Clamp)
    MAKE_CASE(NVPTXISD::Suld1DI32Clamp)
    MAKE_CASE(NVPTXISD::Suld1DI64Clamp)
    MAKE_CASE(NVPTXISD::Suld1DV2I8Clamp)
    MAKE_CASE(NVPTXISD::Suld1DV2I16Clamp)
    MAKE_CASE(NVPTXISD::Suld1DV2I32Clamp)
    MAKE_CASE(NVPTXISD::Suld1DV2I64Clamp)
    MAKE_CASE(NVPTXISD::Suld1DV4I8Clamp)
    MAKE_CASE(NVPTXISD::Suld1DV4I16Clamp)
    MAKE_CASE(NVPTXISD::Suld1DV4I32Clamp)

    MAKE_CASE(NVPTXISD::Suld1DArrayI8Clamp)
    MAKE_CASE(NVPTXISD::Suld1DArrayI16Clamp)
    MAKE_CASE(NVPTXISD::Suld1DArrayI32Clamp)
    MAKE_CASE(NVPTXISD::Suld1DArrayI64Clamp)
    MAKE_CASE(NVPTXISD::Suld1DArrayV2I8Clamp)
    MAKE_CASE(NVPTXISD::Suld1DArrayV2I16Clamp)
    MAKE_CASE(NVPTXISD::Suld1DArrayV2I32Clamp)
    MAKE_CASE(NVPTXISD::Suld1DArrayV2I64Clamp)
    MAKE_CASE(NVPTXISD::Suld1DArrayV4I8Clamp)
    MAKE_CASE(NVPTXISD::Suld1DArrayV4I16Clamp)
    MAKE_CASE(NVPTXISD::Suld1DArrayV4I32Clamp)

    MAKE_CASE(NVPTXISD::Suld2DI8Clamp)
    MAKE_CASE(NVPTXISD::Suld2DI16Clamp)
    MAKE_CASE(NVPTXISD::Suld2DI32Clamp)
    MAKE_CASE(NVPTXISD::Suld2DI64Clamp)
    MAKE_CASE(NVPTXISD::Suld2DV2I8Clamp)
    MAKE_CASE(NVPTXISD::Suld2DV2I16Clamp)
    MAKE_CASE(NVPTXISD::Suld2DV2I32Clamp)
    MAKE_CASE(NVPTXISD::Suld2DV2I64Clamp)
    MAKE_CASE(NVPTXISD::Suld2DV4I8Clamp)
    MAKE_CASE(NVPTXISD::Suld2DV4I16Clamp)
    MAKE_CASE(NVPTXISD::Suld2DV4I32Clamp)

    MAKE_CASE(NVPTXISD::Suld2DArrayI8Clamp)
    MAKE_CASE(NVPTXISD::Suld2DArrayI16Clamp)
    MAKE_CASE(NVPTXISD::Suld2DArrayI32Clamp)
    MAKE_CASE(NVPTXISD::Suld2DArrayI64Clamp)
    MAKE_CASE(NVPTXISD::Suld2DArrayV2I8Clamp)
    MAKE_CASE(NVPTXISD::Suld2DArrayV2I16Clamp)
    MAKE_CASE(NVPTXISD::Suld2DArrayV2I32Clamp)
    MAKE_CASE(NVPTXISD::Suld2DArrayV2I64Clamp)
    MAKE_CASE(NVPTXISD::Suld2DArrayV4I8Clamp)
    MAKE_CASE(NVPTXISD::Suld2DArrayV4I16Clamp)
    MAKE_CASE(NVPTXISD::Suld2DArrayV4I32Clamp)

    MAKE_CASE(NVPTXISD::Suld3DI8Clamp)
    MAKE_CASE(NVPTXISD::Suld3DI16Clamp)
    MAKE_CASE(NVPTXISD::Suld3DI32Clamp)
    MAKE_CASE(NVPTXISD::Suld3DI64Clamp)
    MAKE_CASE(NVPTXISD::Suld3DV2I8Clamp)
    MAKE_CASE(NVPTXISD::Suld3DV2I16Clamp)
    MAKE_CASE(NVPTXISD::Suld3DV2I32Clamp)
    MAKE_CASE(NVPTXISD::Suld3DV2I64Clamp)
    MAKE_CASE(NVPTXISD::Suld3DV4I8Clamp)
    MAKE_CASE(NVPTXISD::Suld3DV4I16Clamp)
    MAKE_CASE(NVPTXISD::Suld3DV4I32Clamp)

    MAKE_CASE(NVPTXISD::Suld1DI8Trap)
    MAKE_CASE(NVPTXISD::Suld1DI16Trap)
    MAKE_CASE(NVPTXISD::Suld1DI32Trap)
    MAKE_CASE(NVPTXISD::Suld1DI64Trap)
    MAKE_CASE(NVPTXISD::Suld1DV2I8Trap)
    MAKE_CASE(NVPTXISD::Suld1DV2I16Trap)
    MAKE_CASE(NVPTXISD::Suld1DV2I32Trap)
    MAKE_CASE(NVPTXISD::Suld1DV2I64Trap)
    MAKE_CASE(NVPTXISD::Suld1DV4I8Trap)
    MAKE_CASE(NVPTXISD::Suld1DV4I16Trap)
    MAKE_CASE(NVPTXISD::Suld1DV4I32Trap)

    MAKE_CASE(NVPTXISD::Suld1DArrayI8Trap)
    MAKE_CASE(NVPTXISD::Suld1DArrayI16Trap)
    MAKE_CASE(NVPTXISD::Suld1DArrayI32Trap)
    MAKE_CASE(NVPTXISD::Suld1DArrayI64Trap)
    MAKE_CASE(NVPTXISD::Suld1DArrayV2I8Trap)
    MAKE_CASE(NVPTXISD::Suld1DArrayV2I16Trap)
    MAKE_CASE(NVPTXISD::Suld1DArrayV2I32Trap)
    MAKE_CASE(NVPTXISD::Suld1DArrayV2I64Trap)
    MAKE_CASE(NVPTXISD::Suld1DArrayV4I8Trap)
    MAKE_CASE(NVPTXISD::Suld1DArrayV4I16Trap)
    MAKE_CASE(NVPTXISD::Suld1DArrayV4I32Trap)

    MAKE_CASE(NVPTXISD::Suld2DI8Trap)
    MAKE_CASE(NVPTXISD::Suld2DI16Trap)
    MAKE_CASE(NVPTXISD::Suld2DI32Trap)
    MAKE_CASE(NVPTXISD::Suld2DI64Trap)
    MAKE_CASE(NVPTXISD::Suld2DV2I8Trap)
    MAKE_CASE(NVPTXISD::Suld2DV2I16Trap)
    MAKE_CASE(NVPTXISD::Suld2DV2I32Trap)
    MAKE_CASE(NVPTXISD::Suld2DV2I64Trap)
    MAKE_CASE(NVPTXISD::Suld2DV4I8Trap)
    MAKE_CASE(NVPTXISD::Suld2DV4I16Trap)
    MAKE_CASE(NVPTXISD::Suld2DV4I32Trap)

    MAKE_CASE(NVPTXISD::Suld2DArrayI8Trap)
    MAKE_CASE(NVPTXISD::Suld2DArrayI16Trap)
    MAKE_CASE(NVPTXISD::Suld2DArrayI32Trap)
    MAKE_CASE(NVPTXISD::Suld2DArrayI64Trap)
    MAKE_CASE(NVPTXISD::Suld2DArrayV2I8Trap)
    MAKE_CASE(NVPTXISD::Suld2DArrayV2I16Trap)
    MAKE_CASE(NVPTXISD::Suld2DArrayV2I32Trap)
    MAKE_CASE(NVPTXISD::Suld2DArrayV2I64Trap)
    MAKE_CASE(NVPTXISD::Suld2DArrayV4I8Trap)
    MAKE_CASE(NVPTXISD::Suld2DArrayV4I16Trap)
    MAKE_CASE(NVPTXISD::Suld2DArrayV4I32Trap)

    MAKE_CASE(NVPTXISD::Suld3DI8Trap)
    MAKE_CASE(NVPTXISD::Suld3DI16Trap)
    MAKE_CASE(NVPTXISD::Suld3DI32Trap)
    MAKE_CASE(NVPTXISD::Suld3DI64Trap)
    MAKE_CASE(NVPTXISD::Suld3DV2I8Trap)
    MAKE_CASE(NVPTXISD::Suld3DV2I16Trap)
    MAKE_CASE(NVPTXISD::Suld3DV2I32Trap)
    MAKE_CASE(NVPTXISD::Suld3DV2I64Trap)
    MAKE_CASE(NVPTXISD::Suld3DV4I8Trap)
    MAKE_CASE(NVPTXISD::Suld3DV4I16Trap)
    MAKE_CASE(NVPTXISD::Suld3DV4I32Trap)

    MAKE_CASE(NVPTXISD::Suld1DI8Zero)
    MAKE_CASE(NVPTXISD::Suld1DI16Zero)
    MAKE_CASE(NVPTXISD::Suld1DI32Zero)
    MAKE_CASE(NVPTXISD::Suld1DI64Zero)
    MAKE_CASE(NVPTXISD::Suld1DV2I8Zero)
    MAKE_CASE(NVPTXISD::Suld1DV2I16Zero)
    MAKE_CASE(NVPTXISD::Suld1DV2I32Zero)
    MAKE_CASE(NVPTXISD::Suld1DV2I64Zero)
    MAKE_CASE(NVPTXISD::Suld1DV4I8Zero)
    MAKE_CASE(NVPTXISD::Suld1DV4I16Zero)
    MAKE_CASE(NVPTXISD::Suld1DV4I32Zero)

    MAKE_CASE(NVPTXISD::Suld1DArrayI8Zero)
    MAKE_CASE(NVPTXISD::Suld1DArrayI16Zero)
    MAKE_CASE(NVPTXISD::Suld1DArrayI32Zero)
    MAKE_CASE(NVPTXISD::Suld1DArrayI64Zero)
    MAKE_CASE(NVPTXISD::Suld1DArrayV2I8Zero)
    MAKE_CASE(NVPTXISD::Suld1DArrayV2I16Zero)
    MAKE_CASE(NVPTXISD::Suld1DArrayV2I32Zero)
    MAKE_CASE(NVPTXISD::Suld1DArrayV2I64Zero)
    MAKE_CASE(NVPTXISD::Suld1DArrayV4I8Zero)
    MAKE_CASE(NVPTXISD::Suld1DArrayV4I16Zero)
    MAKE_CASE(NVPTXISD::Suld1DArrayV4I32Zero)

    MAKE_CASE(NVPTXISD::Suld2DI8Zero)
    MAKE_CASE(NVPTXISD::Suld2DI16Zero)
    MAKE_CASE(NVPTXISD::Suld2DI32Zero)
    MAKE_CASE(NVPTXISD::Suld2DI64Zero)
    MAKE_CASE(NVPTXISD::Suld2DV2I8Zero)
    MAKE_CASE(NVPTXISD::Suld2DV2I16Zero)
    MAKE_CASE(NVPTXISD::Suld2DV2I32Zero)
    MAKE_CASE(NVPTXISD::Suld2DV2I64Zero)
    MAKE_CASE(NVPTXISD::Suld2DV4I8Zero)
    MAKE_CASE(NVPTXISD::Suld2DV4I16Zero)
    MAKE_CASE(NVPTXISD::Suld2DV4I32Zero)

    MAKE_CASE(NVPTXISD::Suld2DArrayI8Zero)
    MAKE_CASE(NVPTXISD::Suld2DArrayI16Zero)
    MAKE_CASE(NVPTXISD::Suld2DArrayI32Zero)
    MAKE_CASE(NVPTXISD::Suld2DArrayI64Zero)
    MAKE_CASE(NVPTXISD::Suld2DArrayV2I8Zero)
    MAKE_CASE(NVPTXISD::Suld2DArrayV2I16Zero)
    MAKE_CASE(NVPTXISD::Suld2DArrayV2I32Zero)
    MAKE_CASE(NVPTXISD::Suld2DArrayV2I64Zero)
    MAKE_CASE(NVPTXISD::Suld2DArrayV4I8Zero)
    MAKE_CASE(NVPTXISD::Suld2DArrayV4I16Zero)
    MAKE_CASE(NVPTXISD::Suld2DArrayV4I32Zero)

    MAKE_CASE(NVPTXISD::Suld3DI8Zero)
    MAKE_CASE(NVPTXISD::Suld3DI16Zero)
    MAKE_CASE(NVPTXISD::Suld3DI32Zero)
    MAKE_CASE(NVPTXISD::Suld3DI64Zero)
    MAKE_CASE(NVPTXISD::Suld3DV2I8Zero)
    MAKE_CASE(NVPTXISD::Suld3DV2I16Zero)
    MAKE_CASE(NVPTXISD::Suld3DV2I32Zero)
    MAKE_CASE(NVPTXISD::Suld3DV2I64Zero)
    MAKE_CASE(NVPTXISD::Suld3DV4I8Zero)
    MAKE_CASE(NVPTXISD::Suld3DV4I16Zero)
    MAKE_CASE(NVPTXISD::Suld3DV4I32Zero)
  }
  return nullptr;

#undef MAKE_CASE
}

TargetLoweringBase::LegalizeTypeAction
NVPTXTargetLowering::getPreferredVectorAction(MVT VT) const {
  if (!VT.isScalableVector() && VT.getVectorNumElements() != 1 &&
      VT.getScalarType() == MVT::i1)
    return TypeSplitVector;
  if (Isv2x16VT(VT))
    return TypeLegal;
  return TargetLoweringBase::getPreferredVectorAction(VT);
}

SDValue NVPTXTargetLowering::getSqrtEstimate(SDValue Operand, SelectionDAG &DAG,
                                             int Enabled, int &ExtraSteps,
                                             bool &UseOneConst,
                                             bool Reciprocal) const {
  if (!(Enabled == ReciprocalEstimate::Enabled ||
        (Enabled == ReciprocalEstimate::Unspecified && !usePrecSqrtF32())))
    return SDValue();

  if (ExtraSteps == ReciprocalEstimate::Unspecified)
    ExtraSteps = 0;

  SDLoc DL(Operand);
  EVT VT = Operand.getValueType();
  bool Ftz = useF32FTZ(DAG.getMachineFunction());

  auto MakeIntrinsicCall = [&](Intrinsic::ID IID) {
    return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, DL, VT,
                       DAG.getConstant(IID, DL, MVT::i32), Operand);
  };

  // The sqrt and rsqrt refinement processes assume we always start out with an
  // approximation of the rsqrt.  Therefore, if we're going to do any refinement
  // (i.e. ExtraSteps > 0), we must return an rsqrt.  But if we're *not* doing
  // any refinement, we must return a regular sqrt.
  if (Reciprocal || ExtraSteps > 0) {
    if (VT == MVT::f32)
      return MakeIntrinsicCall(Ftz ? Intrinsic::nvvm_rsqrt_approx_ftz_f
                                   : Intrinsic::nvvm_rsqrt_approx_f);
    else if (VT == MVT::f64)
      return MakeIntrinsicCall(Intrinsic::nvvm_rsqrt_approx_d);
    else
      return SDValue();
  } else {
    if (VT == MVT::f32)
      return MakeIntrinsicCall(Ftz ? Intrinsic::nvvm_sqrt_approx_ftz_f
                                   : Intrinsic::nvvm_sqrt_approx_f);
    else {
      // There's no sqrt.approx.f64 instruction, so we emit
      // reciprocal(rsqrt(x)).  This is faster than
      // select(x == 0, 0, x * rsqrt(x)).  (In fact, it's faster than plain
      // x * rsqrt(x).)
      return DAG.getNode(
          ISD::INTRINSIC_WO_CHAIN, DL, VT,
          DAG.getConstant(Intrinsic::nvvm_rcp_approx_ftz_d, DL, MVT::i32),
          MakeIntrinsicCall(Intrinsic::nvvm_rsqrt_approx_d));
    }
  }
}

SDValue
NVPTXTargetLowering::LowerGlobalAddress(SDValue Op, SelectionDAG &DAG) const {
  SDLoc dl(Op);
  const GlobalAddressSDNode *GAN = cast<GlobalAddressSDNode>(Op);
  auto PtrVT = getPointerTy(DAG.getDataLayout(), GAN->getAddressSpace());
  Op = DAG.getTargetGlobalAddress(GAN->getGlobal(), dl, PtrVT);
  return DAG.getNode(NVPTXISD::Wrapper, dl, PtrVT, Op);
}

static bool IsTypePassedAsArray(const Type *Ty) {
  return Ty->isAggregateType() || Ty->isVectorTy() || Ty->isIntegerTy(128) ||
         Ty->isHalfTy() || Ty->isBFloatTy();
}

std::string NVPTXTargetLowering::getPrototype(
    const DataLayout &DL, Type *retTy, const ArgListTy &Args,
    const SmallVectorImpl<ISD::OutputArg> &Outs, MaybeAlign retAlignment,
    std::optional<std::pair<unsigned, const APInt &>> VAInfo,
    const CallBase &CB, unsigned UniqueCallSite) const {
  auto PtrVT = getPointerTy(DL);

  bool isABI = (STI.getSmVersion() >= 20);
  assert(isABI && "Non-ABI compilation is not supported");
  if (!isABI)
    return "";

  std::string Prototype;
  raw_string_ostream O(Prototype);
  O << "prototype_" << UniqueCallSite << " : .callprototype ";

  if (retTy->getTypeID() == Type::VoidTyID) {
    O << "()";
  } else {
    O << "(";
    if ((retTy->isFloatingPointTy() || retTy->isIntegerTy()) &&
        !IsTypePassedAsArray(retTy)) {
      unsigned size = 0;
      if (auto *ITy = dyn_cast<IntegerType>(retTy)) {
        size = ITy->getBitWidth();
      } else {
        assert(retTy->isFloatingPointTy() &&
               "Floating point type expected here");
        size = retTy->getPrimitiveSizeInBits();
      }
      // PTX ABI requires all scalar return values to be at least 32
      // bits in size.  fp16 normally uses .b16 as its storage type in
      // PTX, so its size must be adjusted here, too.
      size = promoteScalarArgumentSize(size);

      O << ".param .b" << size << " _";
    } else if (isa<PointerType>(retTy)) {
      O << ".param .b" << PtrVT.getSizeInBits() << " _";
    } else if (IsTypePassedAsArray(retTy)) {
      O << ".param .align " << (retAlignment ? retAlignment->value() : 0)
        << " .b8 _[" << DL.getTypeAllocSize(retTy) << "]";
    } else {
      llvm_unreachable("Unknown return type");
    }
    O << ") ";
  }
  O << "_ (";

  bool first = true;

  unsigned NumArgs = VAInfo ? VAInfo->first : Args.size();
  for (unsigned i = 0, OIdx = 0; i != NumArgs; ++i, ++OIdx) {
    Type *Ty = Args[i].Ty;
    if (!first) {
      O << ", ";
    }
    first = false;

    if (!Outs[OIdx].Flags.isByVal()) {
      if (IsTypePassedAsArray(Ty)) {
        Align ParamAlign =
            getArgumentAlignment(&CB, Ty, i + AttributeList::FirstArgIndex, DL);
        O << ".param .align " << ParamAlign.value() << " .b8 ";
        O << "_";
        O << "[" << DL.getTypeAllocSize(Ty) << "]";
        // update the index for Outs
        SmallVector<EVT, 16> vtparts;
        ComputeValueVTs(*this, DL, Ty, vtparts);
        if (unsigned len = vtparts.size())
          OIdx += len - 1;
        continue;
      }
      // i8 types in IR will be i16 types in SDAG
      assert((getValueType(DL, Ty) == Outs[OIdx].VT ||
              (getValueType(DL, Ty) == MVT::i8 && Outs[OIdx].VT == MVT::i16)) &&
             "type mismatch between callee prototype and arguments");
      // scalar type
      unsigned sz = 0;
      if (isa<IntegerType>(Ty)) {
        sz = cast<IntegerType>(Ty)->getBitWidth();
        sz = promoteScalarArgumentSize(sz);
      } else if (isa<PointerType>(Ty)) {
        sz = PtrVT.getSizeInBits();
      } else {
        sz = Ty->getPrimitiveSizeInBits();
      }
      O << ".param .b" << sz << " ";
      O << "_";
      continue;
    }

    // Indirect calls need strict ABI alignment so we disable optimizations by
    // not providing a function to optimize.
    Type *ETy = Args[i].IndirectType;
    Align InitialAlign = Outs[OIdx].Flags.getNonZeroByValAlign();
    Align ParamByValAlign =
        getFunctionByValParamAlign(/*F=*/nullptr, ETy, InitialAlign, DL);

    O << ".param .align " << ParamByValAlign.value() << " .b8 ";
    O << "_";
    O << "[" << Outs[OIdx].Flags.getByValSize() << "]";
  }

  if (VAInfo)
    O << (first ? "" : ",") << " .param .align " << VAInfo->second
      << " .b8 _[]\n";
  O << ")";
  if (shouldEmitPTXNoReturn(&CB, *nvTM))
    O << " .noreturn";
  O << ";";

  return Prototype;
}

Align NVPTXTargetLowering::getFunctionArgumentAlignment(
    const Function *F, Type *Ty, unsigned Idx, const DataLayout &DL) const {
  return getAlign(*F, Idx).value_or(getFunctionParamOptimizedAlign(F, Ty, DL));
}

Align NVPTXTargetLowering::getArgumentAlignment(const CallBase *CB, Type *Ty,
                                                unsigned Idx,
                                                const DataLayout &DL) const {
  if (!CB) {
    // CallSite is zero, fallback to ABI type alignment
    return DL.getABITypeAlign(Ty);
  }

  const Function *DirectCallee = CB->getCalledFunction();

  if (!DirectCallee) {
    // We don't have a direct function symbol, but that may be because of
    // constant cast instructions in the call.

    // With bitcast'd call targets, the instruction will be the call
    if (const auto *CI = dyn_cast<CallInst>(CB)) {
      // Check if we have call alignment metadata
      if (MaybeAlign StackAlign = getAlign(*CI, Idx))
        return StackAlign.value();
    }
    DirectCallee = getMaybeBitcastedCallee(CB);
  }

  // Check for function alignment information if we found that the
  // ultimate target is a Function
  if (DirectCallee)
    return getFunctionArgumentAlignment(DirectCallee, Ty, Idx, DL);

  // Call is indirect, fall back to the ABI type alignment
  return DL.getABITypeAlign(Ty);
}

static bool adjustElementType(EVT &ElementType) {
  switch (ElementType.getSimpleVT().SimpleTy) {
  default:
    return false;
  case MVT::f16:
  case MVT::bf16:
    ElementType = MVT::i16;
    return true;
  case MVT::f32:
  case MVT::v2f16:
  case MVT::v2bf16:
    ElementType = MVT::i32;
    return true;
  case MVT::f64:
    ElementType = MVT::i64;
    return true;
  }
}

// Use byte-store when the param address of the argument value is unaligned.
// This may happen when the return value is a field of a packed structure.
//
// This is called in LowerCall() when passing the param values.
static SDValue LowerUnalignedStoreParam(SelectionDAG &DAG, SDValue Chain,
                                        uint64_t Offset, EVT ElementType,
                                        SDValue StVal, SDValue &InGlue,
                                        unsigned ArgID, const SDLoc &dl) {
  // Bit logic only works on integer types
  if (adjustElementType(ElementType))
    StVal = DAG.getNode(ISD::BITCAST, dl, ElementType, StVal);

  // Store each byte
  SDVTList StoreVTs = DAG.getVTList(MVT::Other, MVT::Glue);
  for (unsigned i = 0, n = ElementType.getSizeInBits() / 8; i < n; i++) {
    // Shift the byte to the last byte position
    SDValue ShiftVal = DAG.getNode(ISD::SRL, dl, ElementType, StVal,
                                   DAG.getConstant(i * 8, dl, MVT::i32));
    SDValue StoreOperands[] = {Chain, DAG.getConstant(ArgID, dl, MVT::i32),
                               DAG.getConstant(Offset + i, dl, MVT::i32),
                               ShiftVal, InGlue};
    // Trunc store only the last byte by using
    //     st.param.b8
    // The register type can be larger than b8.
    Chain = DAG.getMemIntrinsicNode(
        NVPTXISD::StoreParam, dl, StoreVTs, StoreOperands, MVT::i8,
        MachinePointerInfo(), Align(1), MachineMemOperand::MOStore);
    InGlue = Chain.getValue(1);
  }
  return Chain;
}

// Use byte-load when the param adress of the returned value is unaligned.
// This may happen when the returned value is a field of a packed structure.
static SDValue
LowerUnalignedLoadRetParam(SelectionDAG &DAG, SDValue &Chain, uint64_t Offset,
                           EVT ElementType, SDValue &InGlue,
                           SmallVectorImpl<SDValue> &TempProxyRegOps,
                           const SDLoc &dl) {
  // Bit logic only works on integer types
  EVT MergedType = ElementType;
  adjustElementType(MergedType);

  // Load each byte and construct the whole value. Initial value to 0
  SDValue RetVal = DAG.getConstant(0, dl, MergedType);
  // LoadParamMemI8 loads into i16 register only
  SDVTList LoadVTs = DAG.getVTList(MVT::i16, MVT::Other, MVT::Glue);
  for (unsigned i = 0, n = ElementType.getSizeInBits() / 8; i < n; i++) {
    SDValue LoadOperands[] = {Chain, DAG.getConstant(1, dl, MVT::i32),
                              DAG.getConstant(Offset + i, dl, MVT::i32),
                              InGlue};
    // This will be selected to LoadParamMemI8
    SDValue LdVal =
        DAG.getMemIntrinsicNode(NVPTXISD::LoadParam, dl, LoadVTs, LoadOperands,
                                MVT::i8, MachinePointerInfo(), Align(1));
    SDValue TmpLdVal = LdVal.getValue(0);
    Chain = LdVal.getValue(1);
    InGlue = LdVal.getValue(2);

    TmpLdVal = DAG.getNode(NVPTXISD::ProxyReg, dl,
                           TmpLdVal.getSimpleValueType(), TmpLdVal);
    TempProxyRegOps.push_back(TmpLdVal);

    SDValue CMask = DAG.getConstant(255, dl, MergedType);
    SDValue CShift = DAG.getConstant(i * 8, dl, MVT::i32);
    // Need to extend the i16 register to the whole width.
    TmpLdVal = DAG.getNode(ISD::ZERO_EXTEND, dl, MergedType, TmpLdVal);
    // Mask off the high bits. Leave only the lower 8bits.
    // Do this because we are using loadparam.b8.
    TmpLdVal = DAG.getNode(ISD::AND, dl, MergedType, TmpLdVal, CMask);
    // Shift and merge
    TmpLdVal = DAG.getNode(ISD::SHL, dl, MergedType, TmpLdVal, CShift);
    RetVal = DAG.getNode(ISD::OR, dl, MergedType, RetVal, TmpLdVal);
  }
  if (ElementType != MergedType)
    RetVal = DAG.getNode(ISD::BITCAST, dl, ElementType, RetVal);

  return RetVal;
}

SDValue NVPTXTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                                       SmallVectorImpl<SDValue> &InVals) const {

  if (CLI.IsVarArg && (STI.getPTXVersion() < 60 || STI.getSmVersion() < 30))
    report_fatal_error(
        "Support for variadic functions (unsized array parameter) introduced "
        "in PTX ISA version 6.0 and requires target sm_30.");

  SelectionDAG &DAG = CLI.DAG;
  SDLoc dl = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  bool &isTailCall = CLI.IsTailCall;
  ArgListTy &Args = CLI.getArgs();
  Type *RetTy = CLI.RetTy;
  const CallBase *CB = CLI.CB;
  const DataLayout &DL = DAG.getDataLayout();

  bool isABI = (STI.getSmVersion() >= 20);
  assert(isABI && "Non-ABI compilation is not supported");
  if (!isABI)
    return Chain;

  // Variadic arguments.
  //
  // Normally, for each argument, we declare a param scalar or a param
  // byte array in the .param space, and store the argument value to that
  // param scalar or array starting at offset 0.
  //
  // In the case of the first variadic argument, we declare a vararg byte array
  // with size 0. The exact size of this array isn't known at this point, so
  // it'll be patched later. All the variadic arguments will be stored to this
  // array at a certain offset (which gets tracked by 'VAOffset'). The offset is
  // initially set to 0, so it can be used for non-variadic arguments (which use
  // 0 offset) to simplify the code.
  //
  // After all vararg is processed, 'VAOffset' holds the size of the
  // vararg byte array.

  SDValue VADeclareParam;                 // vararg byte array
  unsigned FirstVAArg = CLI.NumFixedArgs; // position of the first variadic
  unsigned VAOffset = 0;                  // current offset in the param array

  unsigned UniqueCallSite = GlobalUniqueCallSite.fetch_add(1);
  SDValue TempChain = Chain;
  Chain = DAG.getCALLSEQ_START(Chain, UniqueCallSite, 0, dl);
  SDValue InGlue = Chain.getValue(1);

  unsigned ParamCount = 0;
  // Args.size() and Outs.size() need not match.
  // Outs.size() will be larger
  //   * if there is an aggregate argument with multiple fields (each field
  //     showing up separately in Outs)
  //   * if there is a vector argument with more than typical vector-length
  //     elements (generally if more than 4) where each vector element is
  //     individually present in Outs.
  // So a different index should be used for indexing into Outs/OutVals.
  // See similar issue in LowerFormalArguments.
  unsigned OIdx = 0;
  // Declare the .params or .reg need to pass values
  // to the function
  for (unsigned i = 0, e = Args.size(); i != e; ++i, ++OIdx) {
    EVT VT = Outs[OIdx].VT;
    Type *Ty = Args[i].Ty;
    bool IsVAArg = (i >= CLI.NumFixedArgs);
    bool IsByVal = Outs[OIdx].Flags.isByVal();

    SmallVector<EVT, 16> VTs;
    SmallVector<uint64_t, 16> Offsets;

    assert((!IsByVal || Args[i].IndirectType) &&
           "byval arg must have indirect type");
    Type *ETy = (IsByVal ? Args[i].IndirectType : Ty);
    ComputePTXValueVTs(*this, DL, ETy, VTs, &Offsets, IsByVal ? 0 : VAOffset);

    Align ArgAlign;
    if (IsByVal) {
      // The ByValAlign in the Outs[OIdx].Flags is always set at this point,
      // so we don't need to worry whether it's naturally aligned or not.
      // See TargetLowering::LowerCallTo().
      Align InitialAlign = Outs[OIdx].Flags.getNonZeroByValAlign();
      ArgAlign = getFunctionByValParamAlign(CB->getCalledFunction(), ETy,
                                            InitialAlign, DL);
      if (IsVAArg)
        VAOffset = alignTo(VAOffset, ArgAlign);
    } else {
      ArgAlign = getArgumentAlignment(CB, Ty, ParamCount + 1, DL);
    }

    unsigned TypeSize =
        (IsByVal ? Outs[OIdx].Flags.getByValSize() : DL.getTypeAllocSize(Ty));
    SDVTList DeclareParamVTs = DAG.getVTList(MVT::Other, MVT::Glue);

    bool NeedAlign; // Does argument declaration specify alignment?
    bool PassAsArray = IsByVal || IsTypePassedAsArray(Ty);
    if (IsVAArg) {
      if (ParamCount == FirstVAArg) {
        SDValue DeclareParamOps[] = {
            Chain, DAG.getConstant(STI.getMaxRequiredAlignment(), dl, MVT::i32),
            DAG.getConstant(ParamCount, dl, MVT::i32),
            DAG.getConstant(1, dl, MVT::i32), InGlue};
        VADeclareParam = Chain = DAG.getNode(NVPTXISD::DeclareParam, dl,
                                             DeclareParamVTs, DeclareParamOps);
      }
      NeedAlign = PassAsArray;
    } else if (PassAsArray) {
      // declare .param .align <align> .b8 .param<n>[<size>];
      SDValue DeclareParamOps[] = {
          Chain, DAG.getConstant(ArgAlign.value(), dl, MVT::i32),
          DAG.getConstant(ParamCount, dl, MVT::i32),
          DAG.getConstant(TypeSize, dl, MVT::i32), InGlue};
      Chain = DAG.getNode(NVPTXISD::DeclareParam, dl, DeclareParamVTs,
                          DeclareParamOps);
      NeedAlign = true;
    } else {
      // declare .param .b<size> .param<n>;
      if (VT.isInteger() || VT.isFloatingPoint()) {
        // PTX ABI requires integral types to be at least 32 bits in
        // size. FP16 is loaded/stored using i16, so it's handled
        // here as well.
        TypeSize = promoteScalarArgumentSize(TypeSize * 8) / 8;
      }
      SDValue DeclareScalarParamOps[] = {
          Chain, DAG.getConstant(ParamCount, dl, MVT::i32),
          DAG.getConstant(TypeSize * 8, dl, MVT::i32),
          DAG.getConstant(0, dl, MVT::i32), InGlue};
      Chain = DAG.getNode(NVPTXISD::DeclareScalarParam, dl, DeclareParamVTs,
                          DeclareScalarParamOps);
      NeedAlign = false;
    }
    InGlue = Chain.getValue(1);

    // PTX Interoperability Guide 3.3(A): [Integer] Values shorter
    // than 32-bits are sign extended or zero extended, depending on
    // whether they are signed or unsigned types. This case applies
    // only to scalar parameters and not to aggregate values.
    bool ExtendIntegerParam =
        Ty->isIntegerTy() && DL.getTypeAllocSizeInBits(Ty) < 32;

    auto VectorInfo = VectorizePTXValueVTs(VTs, Offsets, ArgAlign, IsVAArg);
    SmallVector<SDValue, 6> StoreOperands;
    for (unsigned j = 0, je = VTs.size(); j != je; ++j) {
      EVT EltVT = VTs[j];
      int CurOffset = Offsets[j];
      MaybeAlign PartAlign;
      if (NeedAlign)
        PartAlign = commonAlignment(ArgAlign, CurOffset);

      SDValue StVal = OutVals[OIdx];

      MVT PromotedVT;
      if (PromoteScalarIntegerPTX(EltVT, &PromotedVT)) {
        EltVT = EVT(PromotedVT);
      }
      if (PromoteScalarIntegerPTX(StVal.getValueType(), &PromotedVT)) {
        llvm::ISD::NodeType Ext =
            Outs[OIdx].Flags.isSExt() ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
        StVal = DAG.getNode(Ext, dl, PromotedVT, StVal);
      }

      if (IsByVal) {
        auto PtrVT = getPointerTy(DL);
        SDValue srcAddr = DAG.getNode(ISD::ADD, dl, PtrVT, StVal,
                                      DAG.getConstant(CurOffset, dl, PtrVT));
        StVal = DAG.getLoad(EltVT, dl, TempChain, srcAddr, MachinePointerInfo(),
                            PartAlign);
      } else if (ExtendIntegerParam) {
        assert(VTs.size() == 1 && "Scalar can't have multiple parts.");
        // zext/sext to i32
        StVal = DAG.getNode(Outs[OIdx].Flags.isSExt() ? ISD::SIGN_EXTEND
                                                      : ISD::ZERO_EXTEND,
                            dl, MVT::i32, StVal);
      }

      if (!ExtendIntegerParam && EltVT.getSizeInBits() < 16) {
        // Use 16-bit registers for small stores as it's the
        // smallest general purpose register size supported by NVPTX.
        StVal = DAG.getNode(ISD::ANY_EXTEND, dl, MVT::i16, StVal);
      }

      // If we have a PVF_SCALAR entry, it may not be sufficiently aligned for a
      // scalar store. In such cases, fall back to byte stores.
      if (VectorInfo[j] == PVF_SCALAR && !IsVAArg && PartAlign.has_value() &&
          PartAlign.value() <
              DL.getABITypeAlign(EltVT.getTypeForEVT(*DAG.getContext()))) {
        assert(StoreOperands.empty() && "Unfinished preceeding store.");
        Chain = LowerUnalignedStoreParam(
            DAG, Chain, IsByVal ? CurOffset + VAOffset : CurOffset, EltVT,
            StVal, InGlue, ParamCount, dl);

        // LowerUnalignedStoreParam took care of inserting the necessary nodes
        // into the SDAG, so just move on to the next element.
        if (!IsByVal)
          ++OIdx;
        continue;
      }

      // New store.
      if (VectorInfo[j] & PVF_FIRST) {
        assert(StoreOperands.empty() && "Unfinished preceding store.");
        StoreOperands.push_back(Chain);
        StoreOperands.push_back(
            DAG.getConstant(IsVAArg ? FirstVAArg : ParamCount, dl, MVT::i32));

        StoreOperands.push_back(DAG.getConstant(
            IsByVal ? CurOffset + VAOffset : (IsVAArg ? VAOffset : CurOffset),
            dl, MVT::i32));
      }

      // Record the value to store.
      StoreOperands.push_back(StVal);

      if (VectorInfo[j] & PVF_LAST) {
        unsigned NumElts = StoreOperands.size() - 3;
        NVPTXISD::NodeType Op;
        switch (NumElts) {
        case 1:
          Op = NVPTXISD::StoreParam;
          break;
        case 2:
          Op = NVPTXISD::StoreParamV2;
          break;
        case 4:
          Op = NVPTXISD::StoreParamV4;
          break;
        default:
          llvm_unreachable("Invalid vector info.");
        }

        StoreOperands.push_back(InGlue);

        // Adjust type of the store op if we've extended the scalar
        // return value.
        EVT TheStoreType = ExtendIntegerParam ? MVT::i32 : EltVT;

        Chain = DAG.getMemIntrinsicNode(
            Op, dl, DAG.getVTList(MVT::Other, MVT::Glue), StoreOperands,
            TheStoreType, MachinePointerInfo(), PartAlign,
            MachineMemOperand::MOStore);
        InGlue = Chain.getValue(1);

        // Cleanup.
        StoreOperands.clear();

        // TODO: We may need to support vector types that can be passed
        // as scalars in variadic arguments.
        if (!IsByVal && IsVAArg) {
          assert(NumElts == 1 &&
                 "Vectorization is expected to be disabled for variadics.");
          VAOffset += DL.getTypeAllocSize(
              TheStoreType.getTypeForEVT(*DAG.getContext()));
        }
      }
      if (!IsByVal)
        ++OIdx;
    }
    assert(StoreOperands.empty() && "Unfinished parameter store.");
    if (!IsByVal && VTs.size() > 0)
      --OIdx;
    ++ParamCount;
    if (IsByVal && IsVAArg)
      VAOffset += TypeSize;
  }

  GlobalAddressSDNode *Func = dyn_cast<GlobalAddressSDNode>(Callee.getNode());
  MaybeAlign retAlignment = std::nullopt;

  // Handle Result
  if (Ins.size() > 0) {
    SmallVector<EVT, 16> resvtparts;
    ComputeValueVTs(*this, DL, RetTy, resvtparts);

    // Declare
    //  .param .align N .b8 retval0[<size-in-bytes>], or
    //  .param .b<size-in-bits> retval0
    unsigned resultsz = DL.getTypeAllocSizeInBits(RetTy);
    if (!IsTypePassedAsArray(RetTy)) {
      resultsz = promoteScalarArgumentSize(resultsz);
      SDVTList DeclareRetVTs = DAG.getVTList(MVT::Other, MVT::Glue);
      SDValue DeclareRetOps[] = { Chain, DAG.getConstant(1, dl, MVT::i32),
                                  DAG.getConstant(resultsz, dl, MVT::i32),
                                  DAG.getConstant(0, dl, MVT::i32), InGlue };
      Chain = DAG.getNode(NVPTXISD::DeclareRet, dl, DeclareRetVTs,
                          DeclareRetOps);
      InGlue = Chain.getValue(1);
    } else {
      retAlignment = getArgumentAlignment(CB, RetTy, 0, DL);
      assert(retAlignment && "retAlignment is guaranteed to be set");
      SDVTList DeclareRetVTs = DAG.getVTList(MVT::Other, MVT::Glue);
      SDValue DeclareRetOps[] = {
          Chain, DAG.getConstant(retAlignment->value(), dl, MVT::i32),
          DAG.getConstant(resultsz / 8, dl, MVT::i32),
          DAG.getConstant(0, dl, MVT::i32), InGlue};
      Chain = DAG.getNode(NVPTXISD::DeclareRetParam, dl, DeclareRetVTs,
                          DeclareRetOps);
      InGlue = Chain.getValue(1);
    }
  }

  bool HasVAArgs = CLI.IsVarArg && (CLI.Args.size() > CLI.NumFixedArgs);
  // Set the size of the vararg param byte array if the callee is a variadic
  // function and the variadic part is not empty.
  if (HasVAArgs) {
    SDValue DeclareParamOps[] = {
        VADeclareParam.getOperand(0), VADeclareParam.getOperand(1),
        VADeclareParam.getOperand(2), DAG.getConstant(VAOffset, dl, MVT::i32),
        VADeclareParam.getOperand(4)};
    DAG.MorphNodeTo(VADeclareParam.getNode(), VADeclareParam.getOpcode(),
                    VADeclareParam->getVTList(), DeclareParamOps);
  }

  // Both indirect calls and libcalls have nullptr Func. In order to distinguish
  // between them we must rely on the call site value which is valid for
  // indirect calls but is always null for libcalls.
  bool isIndirectCall = !Func && CB;

  if (isa<ExternalSymbolSDNode>(Callee)) {
    Function* CalleeFunc = nullptr;

    // Try to find the callee in the current module.
    Callee = DAG.getSymbolFunctionGlobalAddress(Callee, &CalleeFunc);
    assert(CalleeFunc != nullptr && "Libcall callee must be set.");

    // Set the "libcall callee" attribute to indicate that the function
    // must always have a declaration.
    CalleeFunc->addFnAttr("nvptx-libcall-callee", "true");
  }

  if (isIndirectCall) {
    // This is indirect function call case : PTX requires a prototype of the
    // form
    // proto_0 : .callprototype(.param .b32 _) _ (.param .b32 _);
    // to be emitted, and the label has to used as the last arg of call
    // instruction.
    // The prototype is embedded in a string and put as the operand for a
    // CallPrototype SDNode which will print out to the value of the string.
    SDVTList ProtoVTs = DAG.getVTList(MVT::Other, MVT::Glue);
    std::string Proto = getPrototype(
        DL, RetTy, Args, Outs, retAlignment,
        HasVAArgs
            ? std::optional<std::pair<unsigned, const APInt &>>(std::make_pair(
                  CLI.NumFixedArgs, VADeclareParam->getConstantOperandAPInt(1)))
            : std::nullopt,
        *CB, UniqueCallSite);
    const char *ProtoStr = nvTM->getStrPool().save(Proto).data();
    SDValue ProtoOps[] = {
        Chain,
        DAG.getTargetExternalSymbol(ProtoStr, MVT::i32),
        InGlue,
    };
    Chain = DAG.getNode(NVPTXISD::CallPrototype, dl, ProtoVTs, ProtoOps);
    InGlue = Chain.getValue(1);
  }
  // Op to just print "call"
  SDVTList PrintCallVTs = DAG.getVTList(MVT::Other, MVT::Glue);
  SDValue PrintCallOps[] = {
    Chain, DAG.getConstant((Ins.size() == 0) ? 0 : 1, dl, MVT::i32), InGlue
  };
  // We model convergent calls as separate opcodes.
  unsigned Opcode = isIndirectCall ? NVPTXISD::PrintCall : NVPTXISD::PrintCallUni;
  if (CLI.IsConvergent)
    Opcode = Opcode == NVPTXISD::PrintCallUni ? NVPTXISD::PrintConvergentCallUni
                                              : NVPTXISD::PrintConvergentCall;
  Chain = DAG.getNode(Opcode, dl, PrintCallVTs, PrintCallOps);
  InGlue = Chain.getValue(1);

  // Ops to print out the function name
  SDVTList CallVoidVTs = DAG.getVTList(MVT::Other, MVT::Glue);
  SDValue CallVoidOps[] = { Chain, Callee, InGlue };
  Chain = DAG.getNode(NVPTXISD::CallVoid, dl, CallVoidVTs, CallVoidOps);
  InGlue = Chain.getValue(1);

  // Ops to print out the param list
  SDVTList CallArgBeginVTs = DAG.getVTList(MVT::Other, MVT::Glue);
  SDValue CallArgBeginOps[] = { Chain, InGlue };
  Chain = DAG.getNode(NVPTXISD::CallArgBegin, dl, CallArgBeginVTs,
                      CallArgBeginOps);
  InGlue = Chain.getValue(1);

  for (unsigned i = 0, e = std::min(CLI.NumFixedArgs + 1, ParamCount); i != e;
       ++i) {
    unsigned opcode;
    if (i == (e - 1))
      opcode = NVPTXISD::LastCallArg;
    else
      opcode = NVPTXISD::CallArg;
    SDVTList CallArgVTs = DAG.getVTList(MVT::Other, MVT::Glue);
    SDValue CallArgOps[] = { Chain, DAG.getConstant(1, dl, MVT::i32),
                             DAG.getConstant(i, dl, MVT::i32), InGlue };
    Chain = DAG.getNode(opcode, dl, CallArgVTs, CallArgOps);
    InGlue = Chain.getValue(1);
  }
  SDVTList CallArgEndVTs = DAG.getVTList(MVT::Other, MVT::Glue);
  SDValue CallArgEndOps[] = { Chain,
                              DAG.getConstant(isIndirectCall ? 0 : 1, dl, MVT::i32),
                              InGlue };
  Chain = DAG.getNode(NVPTXISD::CallArgEnd, dl, CallArgEndVTs, CallArgEndOps);
  InGlue = Chain.getValue(1);

  if (isIndirectCall) {
    SDVTList PrototypeVTs = DAG.getVTList(MVT::Other, MVT::Glue);
    SDValue PrototypeOps[] = {
        Chain, DAG.getConstant(UniqueCallSite, dl, MVT::i32), InGlue};
    Chain = DAG.getNode(NVPTXISD::Prototype, dl, PrototypeVTs, PrototypeOps);
    InGlue = Chain.getValue(1);
  }

  SmallVector<SDValue, 16> ProxyRegOps;
  SmallVector<std::optional<MVT>, 16> ProxyRegTruncates;
  // An item of the vector is filled if the element does not need a ProxyReg
  // operation on it and should be added to InVals as is. ProxyRegOps and
  // ProxyRegTruncates contain empty/none items at the same index.
  SmallVector<SDValue, 16> RetElts;
  // A temporary ProxyReg operations inserted in `LowerUnalignedLoadRetParam()`
  // to use the values of `LoadParam`s and to be replaced later then
  // `CALLSEQ_END` is added.
  SmallVector<SDValue, 16> TempProxyRegOps;

  // Generate loads from param memory/moves from registers for result
  if (Ins.size() > 0) {
    SmallVector<EVT, 16> VTs;
    SmallVector<uint64_t, 16> Offsets;
    ComputePTXValueVTs(*this, DL, RetTy, VTs, &Offsets, 0);
    assert(VTs.size() == Ins.size() && "Bad value decomposition");

    Align RetAlign = getArgumentAlignment(CB, RetTy, 0, DL);
    auto VectorInfo = VectorizePTXValueVTs(VTs, Offsets, RetAlign);

    SmallVector<EVT, 6> LoadVTs;
    int VecIdx = -1; // Index of the first element of the vector.

    // PTX Interoperability Guide 3.3(A): [Integer] Values shorter than
    // 32-bits are sign extended or zero extended, depending on whether
    // they are signed or unsigned types.
    bool ExtendIntegerRetVal =
        RetTy->isIntegerTy() && DL.getTypeAllocSizeInBits(RetTy) < 32;

    for (unsigned i = 0, e = VTs.size(); i != e; ++i) {
      bool needTruncate = false;
      EVT TheLoadType = VTs[i];
      EVT EltType = Ins[i].VT;
      Align EltAlign = commonAlignment(RetAlign, Offsets[i]);
      MVT PromotedVT;

      if (PromoteScalarIntegerPTX(TheLoadType, &PromotedVT)) {
        TheLoadType = EVT(PromotedVT);
        EltType = EVT(PromotedVT);
        needTruncate = true;
      }

      if (ExtendIntegerRetVal) {
        TheLoadType = MVT::i32;
        EltType = MVT::i32;
        needTruncate = true;
      } else if (TheLoadType.getSizeInBits() < 16) {
        if (VTs[i].isInteger())
          needTruncate = true;
        EltType = MVT::i16;
      }

      // If we have a PVF_SCALAR entry, it may not be sufficiently aligned for a
      // scalar load. In such cases, fall back to byte loads.
      if (VectorInfo[i] == PVF_SCALAR && RetTy->isAggregateType() &&
          EltAlign < DL.getABITypeAlign(
                         TheLoadType.getTypeForEVT(*DAG.getContext()))) {
        assert(VecIdx == -1 && LoadVTs.empty() && "Orphaned operand list.");
        SDValue Ret = LowerUnalignedLoadRetParam(
            DAG, Chain, Offsets[i], TheLoadType, InGlue, TempProxyRegOps, dl);
        ProxyRegOps.push_back(SDValue());
        ProxyRegTruncates.push_back(std::optional<MVT>());
        RetElts.resize(i);
        RetElts.push_back(Ret);

        continue;
      }

      // Record index of the very first element of the vector.
      if (VectorInfo[i] & PVF_FIRST) {
        assert(VecIdx == -1 && LoadVTs.empty() && "Orphaned operand list.");
        VecIdx = i;
      }

      LoadVTs.push_back(EltType);

      if (VectorInfo[i] & PVF_LAST) {
        unsigned NumElts = LoadVTs.size();
        LoadVTs.push_back(MVT::Other);
        LoadVTs.push_back(MVT::Glue);
        NVPTXISD::NodeType Op;
        switch (NumElts) {
        case 1:
          Op = NVPTXISD::LoadParam;
          break;
        case 2:
          Op = NVPTXISD::LoadParamV2;
          break;
        case 4:
          Op = NVPTXISD::LoadParamV4;
          break;
        default:
          llvm_unreachable("Invalid vector info.");
        }

        SDValue LoadOperands[] = {
            Chain, DAG.getConstant(1, dl, MVT::i32),
            DAG.getConstant(Offsets[VecIdx], dl, MVT::i32), InGlue};
        SDValue RetVal = DAG.getMemIntrinsicNode(
            Op, dl, DAG.getVTList(LoadVTs), LoadOperands, TheLoadType,
            MachinePointerInfo(), EltAlign,
            MachineMemOperand::MOLoad);

        for (unsigned j = 0; j < NumElts; ++j) {
          ProxyRegOps.push_back(RetVal.getValue(j));

          if (needTruncate)
            ProxyRegTruncates.push_back(std::optional<MVT>(Ins[VecIdx + j].VT));
          else
            ProxyRegTruncates.push_back(std::optional<MVT>());
        }

        Chain = RetVal.getValue(NumElts);
        InGlue = RetVal.getValue(NumElts + 1);

        // Cleanup
        VecIdx = -1;
        LoadVTs.clear();
      }
    }
  }

  Chain =
      DAG.getCALLSEQ_END(Chain, UniqueCallSite, UniqueCallSite + 1, InGlue, dl);
  InGlue = Chain.getValue(1);

  // Append ProxyReg instructions to the chain to make sure that `callseq_end`
  // will not get lost. Otherwise, during libcalls expansion, the nodes can become
  // dangling.
  for (unsigned i = 0; i < ProxyRegOps.size(); ++i) {
    if (i < RetElts.size() && RetElts[i]) {
      InVals.push_back(RetElts[i]);
      continue;
    }

    SDValue Ret = DAG.getNode(
      NVPTXISD::ProxyReg, dl,
      DAG.getVTList(ProxyRegOps[i].getSimpleValueType(), MVT::Other, MVT::Glue),
      { Chain, ProxyRegOps[i], InGlue }
    );

    Chain = Ret.getValue(1);
    InGlue = Ret.getValue(2);

    if (ProxyRegTruncates[i]) {
      Ret = DAG.getNode(ISD::TRUNCATE, dl, *ProxyRegTruncates[i], Ret);
    }

    InVals.push_back(Ret);
  }

  for (SDValue &T : TempProxyRegOps) {
    SDValue Repl = DAG.getNode(
        NVPTXISD::ProxyReg, dl,
        DAG.getVTList(T.getSimpleValueType(), MVT::Other, MVT::Glue),
        {Chain, T.getOperand(0), InGlue});
    DAG.ReplaceAllUsesWith(T, Repl);
    DAG.RemoveDeadNode(T.getNode());

    Chain = Repl.getValue(1);
    InGlue = Repl.getValue(2);
  }

  // set isTailCall to false for now, until we figure out how to express
  // tail call optimization in PTX
  isTailCall = false;
  return Chain;
}

SDValue NVPTXTargetLowering::LowerDYNAMIC_STACKALLOC(SDValue Op,
                                                     SelectionDAG &DAG) const {

  if (STI.getPTXVersion() < 73 || STI.getSmVersion() < 52) {
    const Function &Fn = DAG.getMachineFunction().getFunction();

    DiagnosticInfoUnsupported NoDynamicAlloca(
        Fn,
        "Support for dynamic alloca introduced in PTX ISA version 7.3 and "
        "requires target sm_52.",
        SDLoc(Op).getDebugLoc());
    DAG.getContext()->diagnose(NoDynamicAlloca);
    auto Ops = {DAG.getConstant(0, SDLoc(), Op.getValueType()),
                Op.getOperand(0)};
    return DAG.getMergeValues(Ops, SDLoc());
  }

  SDValue Chain = Op.getOperand(0);
  SDValue Size = Op.getOperand(1);
  uint64_t Align = cast<ConstantSDNode>(Op.getOperand(2))->getZExtValue();
  SDLoc DL(Op.getNode());

  // The size for ptx alloca instruction is 64-bit for m64 and 32-bit for m32.
  if (nvTM->is64Bit())
    Size = DAG.getZExtOrTrunc(Size, DL, MVT::i64);
  else
    Size = DAG.getZExtOrTrunc(Size, DL, MVT::i32);

  SDValue AllocOps[] = {Chain, Size,
                        DAG.getTargetConstant(Align, DL, MVT::i32)};
  SDValue Alloca = DAG.getNode(NVPTXISD::DYNAMIC_STACKALLOC, DL,
                               nvTM->is64Bit() ? MVT::i64 : MVT::i32, AllocOps);

  SDValue MergeOps[] = {Alloca, Chain};
  return DAG.getMergeValues(MergeOps, DL);
}

// By default CONCAT_VECTORS is lowered by ExpandVectorBuildThroughStack()
// (see LegalizeDAG.cpp). This is slow and uses local memory.
// We use extract/insert/build vector just as what LegalizeOp() does in llvm 2.5
SDValue
NVPTXTargetLowering::LowerCONCAT_VECTORS(SDValue Op, SelectionDAG &DAG) const {
  SDNode *Node = Op.getNode();
  SDLoc dl(Node);
  SmallVector<SDValue, 8> Ops;
  unsigned NumOperands = Node->getNumOperands();
  for (unsigned i = 0; i < NumOperands; ++i) {
    SDValue SubOp = Node->getOperand(i);
    EVT VVT = SubOp.getNode()->getValueType(0);
    EVT EltVT = VVT.getVectorElementType();
    unsigned NumSubElem = VVT.getVectorNumElements();
    for (unsigned j = 0; j < NumSubElem; ++j) {
      Ops.push_back(DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, SubOp,
                                DAG.getIntPtrConstant(j, dl)));
    }
  }
  return DAG.getBuildVector(Node->getValueType(0), dl, Ops);
}

// We can init constant f16x2/v2i16/v4i8 with a single .b32 move.  Normally it
// would get lowered as two constant loads and vector-packing move.
// Instead we want just a constant move:
//        mov.b32         %r2, 0x40003C00
SDValue NVPTXTargetLowering::LowerBUILD_VECTOR(SDValue Op,
                                               SelectionDAG &DAG) const {
  EVT VT = Op->getValueType(0);
  if (!(Isv2x16VT(VT) || VT == MVT::v4i8))
    return Op;

  SDLoc DL(Op);

  if (!llvm::all_of(Op->ops(), [](SDValue Operand) {
        return Operand->isUndef() || isa<ConstantSDNode>(Operand) ||
               isa<ConstantFPSDNode>(Operand);
      })) {
    // Lower non-const v4i8 vector as byte-wise constructed i32, which allows us
    // to optimize calculation of constant parts.
    if (VT == MVT::v4i8) {
      SDValue C8 = DAG.getConstant(8, DL, MVT::i32);
      SDValue E01 = DAG.getNode(
          NVPTXISD::BFI, DL, MVT::i32,
          DAG.getAnyExtOrTrunc(Op->getOperand(1), DL, MVT::i32),
          DAG.getAnyExtOrTrunc(Op->getOperand(0), DL, MVT::i32), C8, C8);
      SDValue E012 =
          DAG.getNode(NVPTXISD::BFI, DL, MVT::i32,
                      DAG.getAnyExtOrTrunc(Op->getOperand(2), DL, MVT::i32),
                      E01, DAG.getConstant(16, DL, MVT::i32), C8);
      SDValue E0123 =
          DAG.getNode(NVPTXISD::BFI, DL, MVT::i32,
                      DAG.getAnyExtOrTrunc(Op->getOperand(3), DL, MVT::i32),
                      E012, DAG.getConstant(24, DL, MVT::i32), C8);
      return DAG.getNode(ISD::BITCAST, DL, VT, E0123);
    }
    return Op;
  }

  // Get value or the Nth operand as an APInt(32). Undef values treated as 0.
  auto GetOperand = [](SDValue Op, int N) -> APInt {
    const SDValue &Operand = Op->getOperand(N);
    EVT VT = Op->getValueType(0);
    if (Operand->isUndef())
      return APInt(32, 0);
    APInt Value;
    if (VT == MVT::v2f16 || VT == MVT::v2bf16)
      Value = cast<ConstantFPSDNode>(Operand)->getValueAPF().bitcastToAPInt();
    else if (VT == MVT::v2i16 || VT == MVT::v4i8)
      Value = Operand->getAsAPIntVal();
    else
      llvm_unreachable("Unsupported type");
    // i8 values are carried around as i16, so we need to zero out upper bits,
    // so they do not get in the way of combining individual byte values
    if (VT == MVT::v4i8)
      Value = Value.trunc(8);
    return Value.zext(32);
  };
  APInt Value;
  if (Isv2x16VT(VT)) {
    Value = GetOperand(Op, 0) | GetOperand(Op, 1).shl(16);
  } else if (VT == MVT::v4i8) {
    Value = GetOperand(Op, 0) | GetOperand(Op, 1).shl(8) |
            GetOperand(Op, 2).shl(16) | GetOperand(Op, 3).shl(24);
  } else {
    llvm_unreachable("Unsupported type");
  }
  SDValue Const = DAG.getConstant(Value, SDLoc(Op), MVT::i32);
  return DAG.getNode(ISD::BITCAST, SDLoc(Op), Op->getValueType(0), Const);
}

SDValue NVPTXTargetLowering::LowerEXTRACT_VECTOR_ELT(SDValue Op,
                                                     SelectionDAG &DAG) const {
  SDValue Index = Op->getOperand(1);
  SDValue Vector = Op->getOperand(0);
  SDLoc DL(Op);
  EVT VectorVT = Vector.getValueType();

  if (VectorVT == MVT::v4i8) {
    SDValue BFE =
        DAG.getNode(NVPTXISD::BFE, DL, MVT::i32,
                    {Vector,
                     DAG.getNode(ISD::MUL, DL, MVT::i32,
                                 DAG.getZExtOrTrunc(Index, DL, MVT::i32),
                                 DAG.getConstant(8, DL, MVT::i32)),
                     DAG.getConstant(8, DL, MVT::i32)});
    return DAG.getAnyExtOrTrunc(BFE, DL, Op->getValueType(0));
  }

  // Constant index will be matched by tablegen.
  if (isa<ConstantSDNode>(Index.getNode()))
    return Op;

  // Extract individual elements and select one of them.
  assert(Isv2x16VT(VectorVT) && "Unexpected vector type.");
  EVT EltVT = VectorVT.getVectorElementType();

  SDLoc dl(Op.getNode());
  SDValue E0 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, Vector,
                           DAG.getIntPtrConstant(0, dl));
  SDValue E1 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, Vector,
                           DAG.getIntPtrConstant(1, dl));
  return DAG.getSelectCC(dl, Index, DAG.getIntPtrConstant(0, dl), E0, E1,
                         ISD::CondCode::SETEQ);
}

SDValue NVPTXTargetLowering::LowerINSERT_VECTOR_ELT(SDValue Op,
                                                    SelectionDAG &DAG) const {
  SDValue Vector = Op->getOperand(0);
  EVT VectorVT = Vector.getValueType();

  if (VectorVT != MVT::v4i8)
    return Op;
  SDLoc DL(Op);
  SDValue Value = Op->getOperand(1);
  if (Value->isUndef())
    return Vector;

  SDValue Index = Op->getOperand(2);

  SDValue BFI =
      DAG.getNode(NVPTXISD::BFI, DL, MVT::i32,
                  {DAG.getZExtOrTrunc(Value, DL, MVT::i32), Vector,
                   DAG.getNode(ISD::MUL, DL, MVT::i32,
                               DAG.getZExtOrTrunc(Index, DL, MVT::i32),
                               DAG.getConstant(8, DL, MVT::i32)),
                   DAG.getConstant(8, DL, MVT::i32)});
  return DAG.getNode(ISD::BITCAST, DL, Op->getValueType(0), BFI);
}

SDValue NVPTXTargetLowering::LowerVECTOR_SHUFFLE(SDValue Op,
                                                 SelectionDAG &DAG) const {
  SDValue V1 = Op.getOperand(0);
  EVT VectorVT = V1.getValueType();
  if (VectorVT != MVT::v4i8 || Op.getValueType() != MVT::v4i8)
    return Op;

  // Lower shuffle to PRMT instruction.
  const ShuffleVectorSDNode *SVN = cast<ShuffleVectorSDNode>(Op.getNode());
  SDValue V2 = Op.getOperand(1);
  uint32_t Selector = 0;
  for (auto I : llvm::enumerate(SVN->getMask())) {
    if (I.value() != -1) // -1 is a placeholder for undef.
      Selector |= (I.value() << (I.index() * 4));
  }

  SDLoc DL(Op);
  return DAG.getNode(NVPTXISD::PRMT, DL, MVT::v4i8, V1, V2,
                     DAG.getConstant(Selector, DL, MVT::i32),
                     DAG.getConstant(NVPTX::PTXPrmtMode::NONE, DL, MVT::i32));
}
/// LowerShiftRightParts - Lower SRL_PARTS, SRA_PARTS, which
/// 1) returns two i32 values and take a 2 x i32 value to shift plus a shift
///    amount, or
/// 2) returns two i64 values and take a 2 x i64 value to shift plus a shift
///    amount.
SDValue NVPTXTargetLowering::LowerShiftRightParts(SDValue Op,
                                                  SelectionDAG &DAG) const {
  assert(Op.getNumOperands() == 3 && "Not a double-shift!");
  assert(Op.getOpcode() == ISD::SRA_PARTS || Op.getOpcode() == ISD::SRL_PARTS);

  EVT VT = Op.getValueType();
  unsigned VTBits = VT.getSizeInBits();
  SDLoc dl(Op);
  SDValue ShOpLo = Op.getOperand(0);
  SDValue ShOpHi = Op.getOperand(1);
  SDValue ShAmt  = Op.getOperand(2);
  unsigned Opc = (Op.getOpcode() == ISD::SRA_PARTS) ? ISD::SRA : ISD::SRL;

  if (VTBits == 32 && STI.getSmVersion() >= 35) {
    // For 32bit and sm35, we can use the funnel shift 'shf' instruction.
    // {dHi, dLo} = {aHi, aLo} >> Amt
    //   dHi = aHi >> Amt
    //   dLo = shf.r.clamp aLo, aHi, Amt

    SDValue Hi = DAG.getNode(Opc, dl, VT, ShOpHi, ShAmt);
    SDValue Lo = DAG.getNode(NVPTXISD::FUN_SHFR_CLAMP, dl, VT, ShOpLo, ShOpHi,
                             ShAmt);

    SDValue Ops[2] = { Lo, Hi };
    return DAG.getMergeValues(Ops, dl);
  }
  else {
    // {dHi, dLo} = {aHi, aLo} >> Amt
    // - if (Amt>=size) then
    //      dLo = aHi >> (Amt-size)
    //      dHi = aHi >> Amt (this is either all 0 or all 1)
    //   else
    //      dLo = (aLo >>logic Amt) | (aHi << (size-Amt))
    //      dHi = aHi >> Amt

    SDValue RevShAmt = DAG.getNode(ISD::SUB, dl, MVT::i32,
                                   DAG.getConstant(VTBits, dl, MVT::i32),
                                   ShAmt);
    SDValue Tmp1 = DAG.getNode(ISD::SRL, dl, VT, ShOpLo, ShAmt);
    SDValue ExtraShAmt = DAG.getNode(ISD::SUB, dl, MVT::i32, ShAmt,
                                     DAG.getConstant(VTBits, dl, MVT::i32));
    SDValue Tmp2 = DAG.getNode(ISD::SHL, dl, VT, ShOpHi, RevShAmt);
    SDValue FalseVal = DAG.getNode(ISD::OR, dl, VT, Tmp1, Tmp2);
    SDValue TrueVal = DAG.getNode(Opc, dl, VT, ShOpHi, ExtraShAmt);

    SDValue Cmp = DAG.getSetCC(dl, MVT::i1, ShAmt,
                               DAG.getConstant(VTBits, dl, MVT::i32),
                               ISD::SETGE);
    SDValue Hi = DAG.getNode(Opc, dl, VT, ShOpHi, ShAmt);
    SDValue Lo = DAG.getNode(ISD::SELECT, dl, VT, Cmp, TrueVal, FalseVal);

    SDValue Ops[2] = { Lo, Hi };
    return DAG.getMergeValues(Ops, dl);
  }
}

/// LowerShiftLeftParts - Lower SHL_PARTS, which
/// 1) returns two i32 values and take a 2 x i32 value to shift plus a shift
///    amount, or
/// 2) returns two i64 values and take a 2 x i64 value to shift plus a shift
///    amount.
SDValue NVPTXTargetLowering::LowerShiftLeftParts(SDValue Op,
                                                 SelectionDAG &DAG) const {
  assert(Op.getNumOperands() == 3 && "Not a double-shift!");
  assert(Op.getOpcode() == ISD::SHL_PARTS);

  EVT VT = Op.getValueType();
  unsigned VTBits = VT.getSizeInBits();
  SDLoc dl(Op);
  SDValue ShOpLo = Op.getOperand(0);
  SDValue ShOpHi = Op.getOperand(1);
  SDValue ShAmt  = Op.getOperand(2);

  if (VTBits == 32 && STI.getSmVersion() >= 35) {
    // For 32bit and sm35, we can use the funnel shift 'shf' instruction.
    // {dHi, dLo} = {aHi, aLo} << Amt
    //   dHi = shf.l.clamp aLo, aHi, Amt
    //   dLo = aLo << Amt

    SDValue Hi = DAG.getNode(NVPTXISD::FUN_SHFL_CLAMP, dl, VT, ShOpLo, ShOpHi,
                             ShAmt);
    SDValue Lo = DAG.getNode(ISD::SHL, dl, VT, ShOpLo, ShAmt);

    SDValue Ops[2] = { Lo, Hi };
    return DAG.getMergeValues(Ops, dl);
  }
  else {
    // {dHi, dLo} = {aHi, aLo} << Amt
    // - if (Amt>=size) then
    //      dLo = aLo << Amt (all 0)
    //      dLo = aLo << (Amt-size)
    //   else
    //      dLo = aLo << Amt
    //      dHi = (aHi << Amt) | (aLo >> (size-Amt))

    SDValue RevShAmt = DAG.getNode(ISD::SUB, dl, MVT::i32,
                                   DAG.getConstant(VTBits, dl, MVT::i32),
                                   ShAmt);
    SDValue Tmp1 = DAG.getNode(ISD::SHL, dl, VT, ShOpHi, ShAmt);
    SDValue ExtraShAmt = DAG.getNode(ISD::SUB, dl, MVT::i32, ShAmt,
                                     DAG.getConstant(VTBits, dl, MVT::i32));
    SDValue Tmp2 = DAG.getNode(ISD::SRL, dl, VT, ShOpLo, RevShAmt);
    SDValue FalseVal = DAG.getNode(ISD::OR, dl, VT, Tmp1, Tmp2);
    SDValue TrueVal = DAG.getNode(ISD::SHL, dl, VT, ShOpLo, ExtraShAmt);

    SDValue Cmp = DAG.getSetCC(dl, MVT::i1, ShAmt,
                               DAG.getConstant(VTBits, dl, MVT::i32),
                               ISD::SETGE);
    SDValue Lo = DAG.getNode(ISD::SHL, dl, VT, ShOpLo, ShAmt);
    SDValue Hi = DAG.getNode(ISD::SELECT, dl, VT, Cmp, TrueVal, FalseVal);

    SDValue Ops[2] = { Lo, Hi };
    return DAG.getMergeValues(Ops, dl);
  }
}

SDValue NVPTXTargetLowering::LowerFROUND(SDValue Op, SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();

  if (VT == MVT::f32)
    return LowerFROUND32(Op, DAG);

  if (VT == MVT::f64)
    return LowerFROUND64(Op, DAG);

  llvm_unreachable("unhandled type");
}

// This is the the rounding method used in CUDA libdevice in C like code:
// float roundf(float A)
// {
//   float RoundedA = (float) (int) ( A > 0 ? (A + 0.5f) : (A - 0.5f));
//   RoundedA = abs(A) > 0x1.0p23 ? A : RoundedA;
//   return abs(A) < 0.5 ? (float)(int)A : RoundedA;
// }
SDValue NVPTXTargetLowering::LowerFROUND32(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue A = Op.getOperand(0);
  EVT VT = Op.getValueType();

  SDValue AbsA = DAG.getNode(ISD::FABS, SL, VT, A);

  // RoundedA = (float) (int) ( A > 0 ? (A + 0.5f) : (A - 0.5f))
  SDValue Bitcast  = DAG.getNode(ISD::BITCAST, SL, MVT::i32, A);
  const int SignBitMask = 0x80000000;
  SDValue Sign = DAG.getNode(ISD::AND, SL, MVT::i32, Bitcast,
                             DAG.getConstant(SignBitMask, SL, MVT::i32));
  const int PointFiveInBits = 0x3F000000;
  SDValue PointFiveWithSignRaw =
      DAG.getNode(ISD::OR, SL, MVT::i32, Sign,
                  DAG.getConstant(PointFiveInBits, SL, MVT::i32));
  SDValue PointFiveWithSign =
      DAG.getNode(ISD::BITCAST, SL, VT, PointFiveWithSignRaw);
  SDValue AdjustedA = DAG.getNode(ISD::FADD, SL, VT, A, PointFiveWithSign);
  SDValue RoundedA = DAG.getNode(ISD::FTRUNC, SL, VT, AdjustedA);

  // RoundedA = abs(A) > 0x1.0p23 ? A : RoundedA;
  EVT SetCCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
  SDValue IsLarge =
      DAG.getSetCC(SL, SetCCVT, AbsA, DAG.getConstantFP(pow(2.0, 23.0), SL, VT),
                   ISD::SETOGT);
  RoundedA = DAG.getNode(ISD::SELECT, SL, VT, IsLarge, A, RoundedA);

  // return abs(A) < 0.5 ? (float)(int)A : RoundedA;
  SDValue IsSmall =DAG.getSetCC(SL, SetCCVT, AbsA,
                                DAG.getConstantFP(0.5, SL, VT), ISD::SETOLT);
  SDValue RoundedAForSmallA = DAG.getNode(ISD::FTRUNC, SL, VT, A);
  return DAG.getNode(ISD::SELECT, SL, VT, IsSmall, RoundedAForSmallA, RoundedA);
}

// The implementation of round(double) is similar to that of round(float) in
// that they both separate the value range into three regions and use a method
// specific to the region to round the values. However, round(double) first
// calculates the round of the absolute value and then adds the sign back while
// round(float) directly rounds the value with sign.
SDValue NVPTXTargetLowering::LowerFROUND64(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue A = Op.getOperand(0);
  EVT VT = Op.getValueType();

  SDValue AbsA = DAG.getNode(ISD::FABS, SL, VT, A);

  // double RoundedA = (double) (int) (abs(A) + 0.5f);
  SDValue AdjustedA = DAG.getNode(ISD::FADD, SL, VT, AbsA,
                                  DAG.getConstantFP(0.5, SL, VT));
  SDValue RoundedA = DAG.getNode(ISD::FTRUNC, SL, VT, AdjustedA);

  // RoundedA = abs(A) < 0.5 ? (double)0 : RoundedA;
  EVT SetCCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
  SDValue IsSmall =DAG.getSetCC(SL, SetCCVT, AbsA,
                                DAG.getConstantFP(0.5, SL, VT), ISD::SETOLT);
  RoundedA = DAG.getNode(ISD::SELECT, SL, VT, IsSmall,
                         DAG.getConstantFP(0, SL, VT),
                         RoundedA);

  // Add sign to rounded_A
  RoundedA = DAG.getNode(ISD::FCOPYSIGN, SL, VT, RoundedA, A);
  DAG.getNode(ISD::FTRUNC, SL, VT, A);

  // RoundedA = abs(A) > 0x1.0p52 ? A : RoundedA;
  SDValue IsLarge =
      DAG.getSetCC(SL, SetCCVT, AbsA, DAG.getConstantFP(pow(2.0, 52.0), SL, VT),
                   ISD::SETOGT);
  return DAG.getNode(ISD::SELECT, SL, VT, IsLarge, A, RoundedA);
}

SDValue NVPTXTargetLowering::LowerINT_TO_FP(SDValue Op,
                                            SelectionDAG &DAG) const {
  assert(STI.getSmVersion() < 90 || STI.getPTXVersion() < 78);

  if (Op.getValueType() == MVT::bf16) {
    SDLoc Loc(Op);
    return DAG.getNode(
        ISD::FP_ROUND, Loc, MVT::bf16,
        DAG.getNode(Op.getOpcode(), Loc, MVT::f32, Op.getOperand(0)),
        DAG.getIntPtrConstant(0, Loc));
  }

  // Everything else is considered legal.
  return Op;
}

SDValue NVPTXTargetLowering::LowerFP_TO_INT(SDValue Op,
                                            SelectionDAG &DAG) const {
  assert(STI.getSmVersion() < 90 || STI.getPTXVersion() < 78);

  if (Op.getOperand(0).getValueType() == MVT::bf16) {
    SDLoc Loc(Op);
    return DAG.getNode(
        Op.getOpcode(), Loc, Op.getValueType(),
        DAG.getNode(ISD::FP_EXTEND, Loc, MVT::f32, Op.getOperand(0)));
  }

  // Everything else is considered legal.
  return Op;
}

SDValue NVPTXTargetLowering::LowerFP_ROUND(SDValue Op,
                                           SelectionDAG &DAG) const {
  EVT NarrowVT = Op.getValueType();
  SDValue Wide = Op.getOperand(0);
  EVT WideVT = Wide.getValueType();
  if (NarrowVT.getScalarType() == MVT::bf16) {
    const TargetLowering *TLI = STI.getTargetLowering();
    if (STI.getSmVersion() < 80 || STI.getPTXVersion() < 70) {
      return TLI->expandFP_ROUND(Op.getNode(), DAG);
    }
    if (STI.getSmVersion() < 90 || STI.getPTXVersion() < 78) {
      // This combination was the first to support f32 -> bf16.
      if (STI.getSmVersion() >= 80 && STI.getPTXVersion() >= 70) {
        if (WideVT.getScalarType() == MVT::f32) {
          return Op;
        }
        if (WideVT.getScalarType() == MVT::f64) {
          SDLoc Loc(Op);
          // Round-inexact-to-odd f64 to f32, then do the final rounding using
          // the hardware f32 -> bf16 instruction.
          SDValue rod = TLI->expandRoundInexactToOdd(
              WideVT.isVector() ? WideVT.changeVectorElementType(MVT::f32)
                                : MVT::f32,
              Wide, Loc, DAG);
          return DAG.getFPExtendOrRound(rod, Loc, NarrowVT);
        }
      }
      return TLI->expandFP_ROUND(Op.getNode(), DAG);
    }
  }

  // Everything else is considered legal.
  return Op;
}

SDValue NVPTXTargetLowering::LowerFP_EXTEND(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDValue Narrow = Op.getOperand(0);
  EVT NarrowVT = Narrow.getValueType();
  EVT WideVT = Op.getValueType();
  if (NarrowVT.getScalarType() == MVT::bf16) {
    if (WideVT.getScalarType() == MVT::f32 &&
        (STI.getSmVersion() < 80 || STI.getPTXVersion() < 71)) {
      SDLoc Loc(Op);
      return DAG.getNode(ISD::BF16_TO_FP, Loc, WideVT, Narrow);
    }
    if (WideVT.getScalarType() == MVT::f64 &&
        (STI.getSmVersion() < 90 || STI.getPTXVersion() < 78)) {
      EVT F32 = NarrowVT.isVector() ? NarrowVT.changeVectorElementType(MVT::f32)
                                    : MVT::f32;
      SDLoc Loc(Op);
      if (STI.getSmVersion() >= 80 && STI.getPTXVersion() >= 71) {
        Op = DAG.getNode(ISD::FP_EXTEND, Loc, F32, Narrow);
      } else {
        Op = DAG.getNode(ISD::BF16_TO_FP, Loc, F32, Narrow);
      }
      return DAG.getNode(ISD::FP_EXTEND, Loc, WideVT, Op);
    }
  }

  // Everything else is considered legal.
  return Op;
}

static SDValue LowerVectorArith(SDValue Op, SelectionDAG &DAG) {
  SDLoc DL(Op);
  if (Op.getValueType() != MVT::v2i16)
    return Op;
  EVT EltVT = Op.getValueType().getVectorElementType();
  SmallVector<SDValue> VecElements;
  for (int I = 0, E = Op.getValueType().getVectorNumElements(); I < E; I++) {
    SmallVector<SDValue> ScalarArgs;
    llvm::transform(Op->ops(), std::back_inserter(ScalarArgs),
                    [&](const SDUse &O) {
                      return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, EltVT,
                                         O.get(), DAG.getIntPtrConstant(I, DL));
                    });
    VecElements.push_back(DAG.getNode(Op.getOpcode(), DL, EltVT, ScalarArgs));
  }
  SDValue V =
      DAG.getNode(ISD::BUILD_VECTOR, DL, Op.getValueType(), VecElements);
  return V;
}

SDValue
NVPTXTargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::RETURNADDR:
    return SDValue();
  case ISD::FRAMEADDR:
    return SDValue();
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::INTRINSIC_W_CHAIN:
    return Op;
  case ISD::BUILD_VECTOR:
    return LowerBUILD_VECTOR(Op, DAG);
  case ISD::EXTRACT_SUBVECTOR:
    return Op;
  case ISD::EXTRACT_VECTOR_ELT:
    return LowerEXTRACT_VECTOR_ELT(Op, DAG);
  case ISD::INSERT_VECTOR_ELT:
    return LowerINSERT_VECTOR_ELT(Op, DAG);
  case ISD::VECTOR_SHUFFLE:
    return LowerVECTOR_SHUFFLE(Op, DAG);
  case ISD::CONCAT_VECTORS:
    return LowerCONCAT_VECTORS(Op, DAG);
  case ISD::STORE:
    return LowerSTORE(Op, DAG);
  case ISD::LOAD:
    return LowerLOAD(Op, DAG);
  case ISD::SHL_PARTS:
    return LowerShiftLeftParts(Op, DAG);
  case ISD::SRA_PARTS:
  case ISD::SRL_PARTS:
    return LowerShiftRightParts(Op, DAG);
  case ISD::SELECT:
    return LowerSelect(Op, DAG);
  case ISD::FROUND:
    return LowerFROUND(Op, DAG);
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:
    return LowerINT_TO_FP(Op, DAG);
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
    return LowerFP_TO_INT(Op, DAG);
  case ISD::FP_ROUND:
    return LowerFP_ROUND(Op, DAG);
  case ISD::FP_EXTEND:
    return LowerFP_EXTEND(Op, DAG);
  case ISD::VAARG:
    return LowerVAARG(Op, DAG);
  case ISD::VASTART:
    return LowerVASTART(Op, DAG);
  case ISD::ABS:
  case ISD::SMIN:
  case ISD::SMAX:
  case ISD::UMIN:
  case ISD::UMAX:
  case ISD::ADD:
  case ISD::SUB:
  case ISD::MUL:
  case ISD::SHL:
  case ISD::SREM:
  case ISD::UREM:
    return LowerVectorArith(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC:
    return LowerDYNAMIC_STACKALLOC(Op, DAG);
  case ISD::CopyToReg:
    return LowerCopyToReg_128(Op, DAG);
  default:
    llvm_unreachable("Custom lowering not defined for operation");
  }
}

// This function is almost a copy of SelectionDAG::expandVAArg().
// The only diff is that this one produces loads from local address space.
SDValue NVPTXTargetLowering::LowerVAARG(SDValue Op, SelectionDAG &DAG) const {
  const TargetLowering *TLI = STI.getTargetLowering();
  SDLoc DL(Op);

  SDNode *Node = Op.getNode();
  const Value *V = cast<SrcValueSDNode>(Node->getOperand(2))->getValue();
  EVT VT = Node->getValueType(0);
  auto *Ty = VT.getTypeForEVT(*DAG.getContext());
  SDValue Tmp1 = Node->getOperand(0);
  SDValue Tmp2 = Node->getOperand(1);
  const MaybeAlign MA(Node->getConstantOperandVal(3));

  SDValue VAListLoad = DAG.getLoad(TLI->getPointerTy(DAG.getDataLayout()), DL,
                                   Tmp1, Tmp2, MachinePointerInfo(V));
  SDValue VAList = VAListLoad;

  if (MA && *MA > TLI->getMinStackArgumentAlignment()) {
    VAList = DAG.getNode(
        ISD::ADD, DL, VAList.getValueType(), VAList,
        DAG.getConstant(MA->value() - 1, DL, VAList.getValueType()));

    VAList = DAG.getNode(
        ISD::AND, DL, VAList.getValueType(), VAList,
        DAG.getConstant(-(int64_t)MA->value(), DL, VAList.getValueType()));
  }

  // Increment the pointer, VAList, to the next vaarg
  Tmp1 = DAG.getNode(ISD::ADD, DL, VAList.getValueType(), VAList,
                     DAG.getConstant(DAG.getDataLayout().getTypeAllocSize(Ty),
                                     DL, VAList.getValueType()));

  // Store the incremented VAList to the legalized pointer
  Tmp1 = DAG.getStore(VAListLoad.getValue(1), DL, Tmp1, Tmp2,
                      MachinePointerInfo(V));

  const Value *SrcV =
      Constant::getNullValue(PointerType::get(Ty, ADDRESS_SPACE_LOCAL));

  // Load the actual argument out of the pointer VAList
  return DAG.getLoad(VT, DL, Tmp1, VAList, MachinePointerInfo(SrcV));
}

SDValue NVPTXTargetLowering::LowerVASTART(SDValue Op, SelectionDAG &DAG) const {
  const TargetLowering *TLI = STI.getTargetLowering();
  SDLoc DL(Op);
  EVT PtrVT = TLI->getPointerTy(DAG.getDataLayout());

  // Store the address of unsized array <function>_vararg[] in the ap object.
  SDValue Arg = getParamSymbol(DAG, /* vararg */ -1, PtrVT);
  SDValue VAReg = DAG.getNode(NVPTXISD::Wrapper, DL, PtrVT, Arg);

  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), DL, VAReg, Op.getOperand(1),
                      MachinePointerInfo(SV));
}

SDValue NVPTXTargetLowering::LowerSelect(SDValue Op, SelectionDAG &DAG) const {
  SDValue Op0 = Op->getOperand(0);
  SDValue Op1 = Op->getOperand(1);
  SDValue Op2 = Op->getOperand(2);
  SDLoc DL(Op.getNode());

  assert(Op.getValueType() == MVT::i1 && "Custom lowering enabled only for i1");

  Op1 = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i32, Op1);
  Op2 = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i32, Op2);
  SDValue Select = DAG.getNode(ISD::SELECT, DL, MVT::i32, Op0, Op1, Op2);
  SDValue Trunc = DAG.getNode(ISD::TRUNCATE, DL, MVT::i1, Select);

  return Trunc;
}

SDValue NVPTXTargetLowering::LowerLOAD(SDValue Op, SelectionDAG &DAG) const {
  if (Op.getValueType() == MVT::i1)
    return LowerLOADi1(Op, DAG);

  // v2f16/v2bf16/v2i16/v4i8 are legal, so we can't rely on legalizer to handle
  // unaligned loads and have to handle it here.
  EVT VT = Op.getValueType();
  if (Isv2x16VT(VT) || VT == MVT::v4i8) {
    LoadSDNode *Load = cast<LoadSDNode>(Op);
    EVT MemVT = Load->getMemoryVT();
    if (!allowsMemoryAccessForAlignment(*DAG.getContext(), DAG.getDataLayout(),
                                        MemVT, *Load->getMemOperand())) {
      SDValue Ops[2];
      std::tie(Ops[0], Ops[1]) = expandUnalignedLoad(Load, DAG);
      return DAG.getMergeValues(Ops, SDLoc(Op));
    }
  }

  return SDValue();
}

// v = ld i1* addr
//   =>
// v1 = ld i8* addr (-> i16)
// v = trunc i16 to i1
SDValue NVPTXTargetLowering::LowerLOADi1(SDValue Op, SelectionDAG &DAG) const {
  SDNode *Node = Op.getNode();
  LoadSDNode *LD = cast<LoadSDNode>(Node);
  SDLoc dl(Node);
  assert(LD->getExtensionType() == ISD::NON_EXTLOAD);
  assert(Node->getValueType(0) == MVT::i1 &&
         "Custom lowering for i1 load only");
  SDValue newLD = DAG.getExtLoad(ISD::ZEXTLOAD, dl, MVT::i16, LD->getChain(),
                                 LD->getBasePtr(), LD->getPointerInfo(),
                                 MVT::i8, LD->getAlign(),
                                 LD->getMemOperand()->getFlags());
  SDValue result = DAG.getNode(ISD::TRUNCATE, dl, MVT::i1, newLD);
  // The legalizer (the caller) is expecting two values from the legalized
  // load, so we build a MergeValues node for it. See ExpandUnalignedLoad()
  // in LegalizeDAG.cpp which also uses MergeValues.
  SDValue Ops[] = { result, LD->getChain() };
  return DAG.getMergeValues(Ops, dl);
}

SDValue NVPTXTargetLowering::LowerSTORE(SDValue Op, SelectionDAG &DAG) const {
  StoreSDNode *Store = cast<StoreSDNode>(Op);
  EVT VT = Store->getMemoryVT();

  if (VT == MVT::i1)
    return LowerSTOREi1(Op, DAG);

  // v2f16 is legal, so we can't rely on legalizer to handle unaligned
  // stores and have to handle it here.
  if ((Isv2x16VT(VT) || VT == MVT::v4i8) &&
      !allowsMemoryAccessForAlignment(*DAG.getContext(), DAG.getDataLayout(),
                                      VT, *Store->getMemOperand()))
    return expandUnalignedStore(Store, DAG);

  // v2f16, v2bf16 and v2i16 don't need special handling.
  if (Isv2x16VT(VT) || VT == MVT::v4i8)
    return SDValue();

  if (VT.isVector())
    return LowerSTOREVector(Op, DAG);

  return SDValue();
}

SDValue
NVPTXTargetLowering::LowerSTOREVector(SDValue Op, SelectionDAG &DAG) const {
  SDNode *N = Op.getNode();
  SDValue Val = N->getOperand(1);
  SDLoc DL(N);
  EVT ValVT = Val.getValueType();

  if (ValVT.isVector()) {
    // We only handle "native" vector sizes for now, e.g. <4 x double> is not
    // legal.  We can (and should) split that into 2 stores of <2 x double> here
    // but I'm leaving that as a TODO for now.
    if (!ValVT.isSimple())
      return SDValue();
    switch (ValVT.getSimpleVT().SimpleTy) {
    default:
      return SDValue();
    case MVT::v2i8:
    case MVT::v2i16:
    case MVT::v2i32:
    case MVT::v2i64:
    case MVT::v2f16:
    case MVT::v2bf16:
    case MVT::v2f32:
    case MVT::v2f64:
    case MVT::v4i8:
    case MVT::v4i16:
    case MVT::v4i32:
    case MVT::v4f16:
    case MVT::v4bf16:
    case MVT::v4f32:
    case MVT::v8f16: // <4 x f16x2>
    case MVT::v8bf16: // <4 x bf16x2>
    case MVT::v8i16:  // <4 x i16x2>
      // This is a "native" vector type
      break;
    }

    MemSDNode *MemSD = cast<MemSDNode>(N);
    const DataLayout &TD = DAG.getDataLayout();

    Align Alignment = MemSD->getAlign();
    Align PrefAlign =
        TD.getPrefTypeAlign(ValVT.getTypeForEVT(*DAG.getContext()));
    if (Alignment < PrefAlign) {
      // This store is not sufficiently aligned, so bail out and let this vector
      // store be scalarized.  Note that we may still be able to emit smaller
      // vector stores.  For example, if we are storing a <4 x float> with an
      // alignment of 8, this check will fail but the legalizer will try again
      // with 2 x <2 x float>, which will succeed with an alignment of 8.
      return SDValue();
    }

    unsigned Opcode = 0;
    EVT EltVT = ValVT.getVectorElementType();
    unsigned NumElts = ValVT.getVectorNumElements();

    // Since StoreV2 is a target node, we cannot rely on DAG type legalization.
    // Therefore, we must ensure the type is legal.  For i1 and i8, we set the
    // stored type to i16 and propagate the "real" type as the memory type.
    bool NeedExt = false;
    if (EltVT.getSizeInBits() < 16)
      NeedExt = true;

    bool StoreF16x2 = false;
    switch (NumElts) {
    default:
      return SDValue();
    case 2:
      Opcode = NVPTXISD::StoreV2;
      break;
    case 4:
      Opcode = NVPTXISD::StoreV4;
      break;
    case 8:
      // v8f16 is a special case. PTX doesn't have st.v8.f16
      // instruction. Instead, we split the vector into v2f16 chunks and
      // store them with st.v4.b32.
      assert(Is16bitsType(EltVT.getSimpleVT()) && "Wrong type for the vector.");
      Opcode = NVPTXISD::StoreV4;
      StoreF16x2 = true;
      break;
    }

    SmallVector<SDValue, 8> Ops;

    // First is the chain
    Ops.push_back(N->getOperand(0));

    if (StoreF16x2) {
      // Combine f16,f16 -> v2f16
      NumElts /= 2;
      for (unsigned i = 0; i < NumElts; ++i) {
        SDValue E0 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, EltVT, Val,
                                 DAG.getIntPtrConstant(i * 2, DL));
        SDValue E1 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, EltVT, Val,
                                 DAG.getIntPtrConstant(i * 2 + 1, DL));
        EVT VecVT = EVT::getVectorVT(*DAG.getContext(), EltVT, 2);
        SDValue V2 = DAG.getNode(ISD::BUILD_VECTOR, DL, VecVT, E0, E1);
        Ops.push_back(V2);
      }
    } else {
      // Then the split values
      for (unsigned i = 0; i < NumElts; ++i) {
        SDValue ExtVal = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, EltVT, Val,
                                     DAG.getIntPtrConstant(i, DL));
        if (NeedExt)
          ExtVal = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i16, ExtVal);
        Ops.push_back(ExtVal);
      }
    }

    // Then any remaining arguments
    Ops.append(N->op_begin() + 2, N->op_end());

    SDValue NewSt =
        DAG.getMemIntrinsicNode(Opcode, DL, DAG.getVTList(MVT::Other), Ops,
                                MemSD->getMemoryVT(), MemSD->getMemOperand());

    // return DCI.CombineTo(N, NewSt, true);
    return NewSt;
  }

  return SDValue();
}

// st i1 v, addr
//    =>
// v1 = zxt v to i16
// st.u8 i16, addr
SDValue NVPTXTargetLowering::LowerSTOREi1(SDValue Op, SelectionDAG &DAG) const {
  SDNode *Node = Op.getNode();
  SDLoc dl(Node);
  StoreSDNode *ST = cast<StoreSDNode>(Node);
  SDValue Tmp1 = ST->getChain();
  SDValue Tmp2 = ST->getBasePtr();
  SDValue Tmp3 = ST->getValue();
  assert(Tmp3.getValueType() == MVT::i1 && "Custom lowering for i1 store only");
  Tmp3 = DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::i16, Tmp3);
  SDValue Result =
      DAG.getTruncStore(Tmp1, dl, Tmp3, Tmp2, ST->getPointerInfo(), MVT::i8,
                        ST->getAlign(), ST->getMemOperand()->getFlags());
  return Result;
}

SDValue NVPTXTargetLowering::LowerCopyToReg_128(SDValue Op,
                                                SelectionDAG &DAG) const {
  // Change the CopyToReg to take in two 64-bit operands instead of a 128-bit
  // operand so that it can pass the legalization.

  assert(Op.getOperand(1).getValueType() == MVT::i128 &&
         "Custom lowering for 128-bit CopyToReg only");

  SDNode *Node = Op.getNode();
  SDLoc DL(Node);

  SDValue Cast = DAG.getBitcast(MVT::v2i64, Op->getOperand(2));
  SDValue Lo = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::i64, Cast,
                           DAG.getIntPtrConstant(0, DL));
  SDValue Hi = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::i64, Cast,
                           DAG.getIntPtrConstant(1, DL));

  SmallVector<SDValue, 5> NewOps(Op->getNumOperands() + 1);
  SmallVector<EVT, 3> ResultsType(Node->values());

  NewOps[0] = Op->getOperand(0); // Chain
  NewOps[1] = Op->getOperand(1); // Dst Reg
  NewOps[2] = Lo;                // Lower 64-bit
  NewOps[3] = Hi;                // Higher 64-bit
  if (Op.getNumOperands() == 4)
    NewOps[4] = Op->getOperand(3); // Glue if exists

  return DAG.getNode(ISD::CopyToReg, DL, ResultsType, NewOps);
}

unsigned NVPTXTargetLowering::getNumRegisters(
    LLVMContext &Context, EVT VT,
    std::optional<MVT> RegisterVT = std::nullopt) const {
  if (VT == MVT::i128 && RegisterVT == MVT::i128)
    return 1;
  return TargetLoweringBase::getNumRegisters(Context, VT, RegisterVT);
}

bool NVPTXTargetLowering::splitValueIntoRegisterParts(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Val, SDValue *Parts,
    unsigned NumParts, MVT PartVT, std::optional<CallingConv::ID> CC) const {
  if (Val.getValueType() == MVT::i128 && NumParts == 1) {
    Parts[0] = Val;
    return true;
  }
  return false;
}

// This creates target external symbol for a function parameter.
// Name of the symbol is composed from its index and the function name.
// Negative index corresponds to special parameter (unsized array) used for
// passing variable arguments.
SDValue NVPTXTargetLowering::getParamSymbol(SelectionDAG &DAG, int idx,
                                            EVT v) const {
  StringRef SavedStr = nvTM->getStrPool().save(
      getParamName(&DAG.getMachineFunction().getFunction(), idx));
  return DAG.getTargetExternalSymbol(SavedStr.data(), v);
}

SDValue NVPTXTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  const DataLayout &DL = DAG.getDataLayout();
  auto PtrVT = getPointerTy(DAG.getDataLayout());

  const Function *F = &MF.getFunction();
  const AttributeList &PAL = F->getAttributes();
  const TargetLowering *TLI = STI.getTargetLowering();

  SDValue Root = DAG.getRoot();
  std::vector<SDValue> OutChains;

  bool isABI = (STI.getSmVersion() >= 20);
  assert(isABI && "Non-ABI compilation is not supported");
  if (!isABI)
    return Chain;

  std::vector<Type *> argTypes;
  std::vector<const Argument *> theArgs;
  for (const Argument &I : F->args()) {
    theArgs.push_back(&I);
    argTypes.push_back(I.getType());
  }
  // argTypes.size() (or theArgs.size()) and Ins.size() need not match.
  // Ins.size() will be larger
  //   * if there is an aggregate argument with multiple fields (each field
  //     showing up separately in Ins)
  //   * if there is a vector argument with more than typical vector-length
  //     elements (generally if more than 4) where each vector element is
  //     individually present in Ins.
  // So a different index should be used for indexing into Ins.
  // See similar issue in LowerCall.
  unsigned InsIdx = 0;

  for (unsigned i = 0, e = theArgs.size(); i != e; ++i, ++InsIdx) {
    Type *Ty = argTypes[i];

    if (theArgs[i]->use_empty()) {
      // argument is dead
      if (IsTypePassedAsArray(Ty) && !Ty->isVectorTy()) {
        SmallVector<EVT, 16> vtparts;

        ComputePTXValueVTs(*this, DAG.getDataLayout(), Ty, vtparts);
        if (vtparts.empty())
          report_fatal_error("Empty parameter types are not supported");

        for (unsigned parti = 0, parte = vtparts.size(); parti != parte;
             ++parti) {
          InVals.push_back(DAG.getNode(ISD::UNDEF, dl, Ins[InsIdx].VT));
          ++InsIdx;
        }
        if (vtparts.size() > 0)
          --InsIdx;
        continue;
      }
      if (Ty->isVectorTy()) {
        EVT ObjectVT = getValueType(DL, Ty);
        unsigned NumRegs = TLI->getNumRegisters(F->getContext(), ObjectVT);
        for (unsigned parti = 0; parti < NumRegs; ++parti) {
          InVals.push_back(DAG.getNode(ISD::UNDEF, dl, Ins[InsIdx].VT));
          ++InsIdx;
        }
        if (NumRegs > 0)
          --InsIdx;
        continue;
      }
      InVals.push_back(DAG.getNode(ISD::UNDEF, dl, Ins[InsIdx].VT));
      continue;
    }

    // In the following cases, assign a node order of "i+1"
    // to newly created nodes. The SDNodes for params have to
    // appear in the same order as their order of appearance
    // in the original function. "i+1" holds that order.
    if (!PAL.hasParamAttr(i, Attribute::ByVal)) {
      bool aggregateIsPacked = false;
      if (StructType *STy = dyn_cast<StructType>(Ty))
        aggregateIsPacked = STy->isPacked();

      SmallVector<EVT, 16> VTs;
      SmallVector<uint64_t, 16> Offsets;
      ComputePTXValueVTs(*this, DL, Ty, VTs, &Offsets, 0);
      if (VTs.empty())
        report_fatal_error("Empty parameter types are not supported");

      Align ArgAlign = getFunctionArgumentAlignment(
          F, Ty, i + AttributeList::FirstArgIndex, DL);
      auto VectorInfo = VectorizePTXValueVTs(VTs, Offsets, ArgAlign);

      SDValue Arg = getParamSymbol(DAG, i, PtrVT);
      int VecIdx = -1; // Index of the first element of the current vector.
      for (unsigned parti = 0, parte = VTs.size(); parti != parte; ++parti) {
        if (VectorInfo[parti] & PVF_FIRST) {
          assert(VecIdx == -1 && "Orphaned vector.");
          VecIdx = parti;
        }

        // That's the last element of this store op.
        if (VectorInfo[parti] & PVF_LAST) {
          unsigned NumElts = parti - VecIdx + 1;
          EVT EltVT = VTs[parti];
          // i1 is loaded/stored as i8.
          EVT LoadVT = EltVT;
          if (EltVT == MVT::i1)
            LoadVT = MVT::i8;
          else if (Isv2x16VT(EltVT) || EltVT == MVT::v4i8)
            // getLoad needs a vector type, but it can't handle
            // vectors which contain v2f16 or v2bf16 elements. So we must load
            // using i32 here and then bitcast back.
            LoadVT = MVT::i32;

          EVT VecVT = EVT::getVectorVT(F->getContext(), LoadVT, NumElts);
          SDValue VecAddr =
              DAG.getNode(ISD::ADD, dl, PtrVT, Arg,
                          DAG.getConstant(Offsets[VecIdx], dl, PtrVT));
          Value *srcValue = Constant::getNullValue(PointerType::get(
              EltVT.getTypeForEVT(F->getContext()), ADDRESS_SPACE_PARAM));

          const MaybeAlign PartAlign = [&]() -> MaybeAlign {
            if (aggregateIsPacked)
              return Align(1);
            if (NumElts != 1)
              return std::nullopt;
            Align PartAlign =
                DL.getABITypeAlign(EltVT.getTypeForEVT(F->getContext()));
            return commonAlignment(PartAlign, Offsets[parti]);
          }();
          SDValue P = DAG.getLoad(VecVT, dl, Root, VecAddr,
                                  MachinePointerInfo(srcValue), PartAlign,
                                  MachineMemOperand::MODereferenceable |
                                      MachineMemOperand::MOInvariant);
          if (P.getNode())
            P.getNode()->setIROrder(i + 1);
          for (unsigned j = 0; j < NumElts; ++j) {
            SDValue Elt = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, LoadVT, P,
                                      DAG.getIntPtrConstant(j, dl));
            // We've loaded i1 as an i8 and now must truncate it back to i1
            if (EltVT == MVT::i1)
              Elt = DAG.getNode(ISD::TRUNCATE, dl, MVT::i1, Elt);
            // v2f16 was loaded as an i32. Now we must bitcast it back.
            else if (EltVT != LoadVT)
              Elt = DAG.getNode(ISD::BITCAST, dl, EltVT, Elt);

            // If a promoted integer type is used, truncate down to the original
            MVT PromotedVT;
            if (PromoteScalarIntegerPTX(EltVT, &PromotedVT)) {
              Elt = DAG.getNode(ISD::TRUNCATE, dl, EltVT, Elt);
            }

            // Extend the element if necessary (e.g. an i8 is loaded
            // into an i16 register)
            if (Ins[InsIdx].VT.isInteger() &&
                Ins[InsIdx].VT.getFixedSizeInBits() >
                    LoadVT.getFixedSizeInBits()) {
              unsigned Extend = Ins[InsIdx].Flags.isSExt() ? ISD::SIGN_EXTEND
                                                           : ISD::ZERO_EXTEND;
              Elt = DAG.getNode(Extend, dl, Ins[InsIdx].VT, Elt);
            }
            InVals.push_back(Elt);
          }

          // Reset vector tracking state.
          VecIdx = -1;
        }
        ++InsIdx;
      }
      if (VTs.size() > 0)
        --InsIdx;
      continue;
    }

    // Param has ByVal attribute
    // Return MoveParam(param symbol).
    // Ideally, the param symbol can be returned directly,
    // but when SDNode builder decides to use it in a CopyToReg(),
    // machine instruction fails because TargetExternalSymbol
    // (not lowered) is target dependent, and CopyToReg assumes
    // the source is lowered.
    EVT ObjectVT = getValueType(DL, Ty);
    assert(ObjectVT == Ins[InsIdx].VT &&
           "Ins type did not match function type");
    SDValue Arg = getParamSymbol(DAG, i, PtrVT);
    SDValue p = DAG.getNode(NVPTXISD::MoveParam, dl, ObjectVT, Arg);
    if (p.getNode())
      p.getNode()->setIROrder(i + 1);
    InVals.push_back(p);
  }

  if (!OutChains.empty())
    DAG.setRoot(DAG.getNode(ISD::TokenFactor, dl, MVT::Other, OutChains));

  return Chain;
}

// Use byte-store when the param adress of the return value is unaligned.
// This may happen when the return value is a field of a packed structure.
static SDValue LowerUnalignedStoreRet(SelectionDAG &DAG, SDValue Chain,
                                      uint64_t Offset, EVT ElementType,
                                      SDValue RetVal, const SDLoc &dl) {
  // Bit logic only works on integer types
  if (adjustElementType(ElementType))
    RetVal = DAG.getNode(ISD::BITCAST, dl, ElementType, RetVal);

  // Store each byte
  for (unsigned i = 0, n = ElementType.getSizeInBits() / 8; i < n; i++) {
    // Shift the byte to the last byte position
    SDValue ShiftVal = DAG.getNode(ISD::SRL, dl, ElementType, RetVal,
                                   DAG.getConstant(i * 8, dl, MVT::i32));
    SDValue StoreOperands[] = {Chain, DAG.getConstant(Offset + i, dl, MVT::i32),
                               ShiftVal};
    // Trunc store only the last byte by using
    //     st.param.b8
    // The register type can be larger than b8.
    Chain = DAG.getMemIntrinsicNode(NVPTXISD::StoreRetval, dl,
                                    DAG.getVTList(MVT::Other), StoreOperands,
                                    MVT::i8, MachinePointerInfo(), std::nullopt,
                                    MachineMemOperand::MOStore);
  }
  return Chain;
}

SDValue
NVPTXTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                                 bool isVarArg,
                                 const SmallVectorImpl<ISD::OutputArg> &Outs,
                                 const SmallVectorImpl<SDValue> &OutVals,
                                 const SDLoc &dl, SelectionDAG &DAG) const {
  const MachineFunction &MF = DAG.getMachineFunction();
  const Function &F = MF.getFunction();
  Type *RetTy = MF.getFunction().getReturnType();

  bool isABI = (STI.getSmVersion() >= 20);
  assert(isABI && "Non-ABI compilation is not supported");
  if (!isABI)
    return Chain;

  const DataLayout &DL = DAG.getDataLayout();
  SmallVector<SDValue, 16> PromotedOutVals;
  SmallVector<EVT, 16> VTs;
  SmallVector<uint64_t, 16> Offsets;
  ComputePTXValueVTs(*this, DL, RetTy, VTs, &Offsets);
  assert(VTs.size() == OutVals.size() && "Bad return value decomposition");

  for (unsigned i = 0, e = VTs.size(); i != e; ++i) {
    SDValue PromotedOutVal = OutVals[i];
    MVT PromotedVT;
    if (PromoteScalarIntegerPTX(VTs[i], &PromotedVT)) {
      VTs[i] = EVT(PromotedVT);
    }
    if (PromoteScalarIntegerPTX(PromotedOutVal.getValueType(), &PromotedVT)) {
      llvm::ISD::NodeType Ext =
          Outs[i].Flags.isSExt() ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
      PromotedOutVal = DAG.getNode(Ext, dl, PromotedVT, PromotedOutVal);
    }
    PromotedOutVals.push_back(PromotedOutVal);
  }

  auto VectorInfo = VectorizePTXValueVTs(
      VTs, Offsets,
      RetTy->isSized() ? getFunctionParamOptimizedAlign(&F, RetTy, DL)
                       : Align(1));

  // PTX Interoperability Guide 3.3(A): [Integer] Values shorter than
  // 32-bits are sign extended or zero extended, depending on whether
  // they are signed or unsigned types.
  bool ExtendIntegerRetVal =
      RetTy->isIntegerTy() && DL.getTypeAllocSizeInBits(RetTy) < 32;

  SmallVector<SDValue, 6> StoreOperands;
  for (unsigned i = 0, e = VTs.size(); i != e; ++i) {
    SDValue OutVal = OutVals[i];
    SDValue RetVal = PromotedOutVals[i];

    if (ExtendIntegerRetVal) {
      RetVal = DAG.getNode(Outs[i].Flags.isSExt() ? ISD::SIGN_EXTEND
                                                  : ISD::ZERO_EXTEND,
                           dl, MVT::i32, RetVal);
    } else if (OutVal.getValueSizeInBits() < 16) {
      // Use 16-bit registers for small load-stores as it's the
      // smallest general purpose register size supported by NVPTX.
      RetVal = DAG.getNode(ISD::ANY_EXTEND, dl, MVT::i16, RetVal);
    }

    // If we have a PVF_SCALAR entry, it may not even be sufficiently aligned
    // for a scalar store. In such cases, fall back to byte stores.
    if (VectorInfo[i] == PVF_SCALAR && RetTy->isAggregateType()) {
      EVT ElementType = ExtendIntegerRetVal ? MVT::i32 : VTs[i];
      Align ElementTypeAlign =
          DL.getABITypeAlign(ElementType.getTypeForEVT(RetTy->getContext()));
      Align ElementAlign =
          commonAlignment(DL.getABITypeAlign(RetTy), Offsets[i]);
      if (ElementAlign < ElementTypeAlign) {
        assert(StoreOperands.empty() && "Orphaned operand list.");
        Chain = LowerUnalignedStoreRet(DAG, Chain, Offsets[i], ElementType,
                                       RetVal, dl);

        // The call to LowerUnalignedStoreRet inserted the necessary SDAG nodes
        // into the graph, so just move on to the next element.
        continue;
      }
    }

    // New load/store. Record chain and offset operands.
    if (VectorInfo[i] & PVF_FIRST) {
      assert(StoreOperands.empty() && "Orphaned operand list.");
      StoreOperands.push_back(Chain);
      StoreOperands.push_back(DAG.getConstant(Offsets[i], dl, MVT::i32));
    }

    // Record the value to return.
    StoreOperands.push_back(RetVal);

    // That's the last element of this store op.
    if (VectorInfo[i] & PVF_LAST) {
      NVPTXISD::NodeType Op;
      unsigned NumElts = StoreOperands.size() - 2;
      switch (NumElts) {
      case 1:
        Op = NVPTXISD::StoreRetval;
        break;
      case 2:
        Op = NVPTXISD::StoreRetvalV2;
        break;
      case 4:
        Op = NVPTXISD::StoreRetvalV4;
        break;
      default:
        llvm_unreachable("Invalid vector info.");
      }

      // Adjust type of load/store op if we've extended the scalar
      // return value.
      EVT TheStoreType = ExtendIntegerRetVal ? MVT::i32 : VTs[i];
      Chain = DAG.getMemIntrinsicNode(
          Op, dl, DAG.getVTList(MVT::Other), StoreOperands, TheStoreType,
          MachinePointerInfo(), Align(1), MachineMemOperand::MOStore);
      // Cleanup vector state.
      StoreOperands.clear();
    }
  }

  return DAG.getNode(NVPTXISD::RET_GLUE, dl, MVT::Other, Chain);
}

void NVPTXTargetLowering::LowerAsmOperandForConstraint(
    SDValue Op, StringRef Constraint, std::vector<SDValue> &Ops,
    SelectionDAG &DAG) const {
  if (Constraint.size() > 1)
    return;
  TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops, DAG);
}

static unsigned getOpcForTextureInstr(unsigned Intrinsic) {
  switch (Intrinsic) {
  default:
    return 0;

  case Intrinsic::nvvm_tex_1d_v4f32_s32:
    return NVPTXISD::Tex1DFloatS32;
  case Intrinsic::nvvm_tex_1d_v4f32_f32:
    return NVPTXISD::Tex1DFloatFloat;
  case Intrinsic::nvvm_tex_1d_level_v4f32_f32:
    return NVPTXISD::Tex1DFloatFloatLevel;
  case Intrinsic::nvvm_tex_1d_grad_v4f32_f32:
    return NVPTXISD::Tex1DFloatFloatGrad;
  case Intrinsic::nvvm_tex_1d_v4s32_s32:
    return NVPTXISD::Tex1DS32S32;
  case Intrinsic::nvvm_tex_1d_v4s32_f32:
    return NVPTXISD::Tex1DS32Float;
  case Intrinsic::nvvm_tex_1d_level_v4s32_f32:
    return NVPTXISD::Tex1DS32FloatLevel;
  case Intrinsic::nvvm_tex_1d_grad_v4s32_f32:
    return NVPTXISD::Tex1DS32FloatGrad;
  case Intrinsic::nvvm_tex_1d_v4u32_s32:
    return NVPTXISD::Tex1DU32S32;
  case Intrinsic::nvvm_tex_1d_v4u32_f32:
    return NVPTXISD::Tex1DU32Float;
  case Intrinsic::nvvm_tex_1d_level_v4u32_f32:
    return NVPTXISD::Tex1DU32FloatLevel;
  case Intrinsic::nvvm_tex_1d_grad_v4u32_f32:
    return NVPTXISD::Tex1DU32FloatGrad;

  case Intrinsic::nvvm_tex_1d_array_v4f32_s32:
    return NVPTXISD::Tex1DArrayFloatS32;
  case Intrinsic::nvvm_tex_1d_array_v4f32_f32:
    return NVPTXISD::Tex1DArrayFloatFloat;
  case Intrinsic::nvvm_tex_1d_array_level_v4f32_f32:
    return NVPTXISD::Tex1DArrayFloatFloatLevel;
  case Intrinsic::nvvm_tex_1d_array_grad_v4f32_f32:
    return NVPTXISD::Tex1DArrayFloatFloatGrad;
  case Intrinsic::nvvm_tex_1d_array_v4s32_s32:
    return NVPTXISD::Tex1DArrayS32S32;
  case Intrinsic::nvvm_tex_1d_array_v4s32_f32:
    return NVPTXISD::Tex1DArrayS32Float;
  case Intrinsic::nvvm_tex_1d_array_level_v4s32_f32:
    return NVPTXISD::Tex1DArrayS32FloatLevel;
  case Intrinsic::nvvm_tex_1d_array_grad_v4s32_f32:
    return NVPTXISD::Tex1DArrayS32FloatGrad;
  case Intrinsic::nvvm_tex_1d_array_v4u32_s32:
    return NVPTXISD::Tex1DArrayU32S32;
  case Intrinsic::nvvm_tex_1d_array_v4u32_f32:
    return NVPTXISD::Tex1DArrayU32Float;
  case Intrinsic::nvvm_tex_1d_array_level_v4u32_f32:
    return NVPTXISD::Tex1DArrayU32FloatLevel;
  case Intrinsic::nvvm_tex_1d_array_grad_v4u32_f32:
    return NVPTXISD::Tex1DArrayU32FloatGrad;

  case Intrinsic::nvvm_tex_2d_v4f32_s32:
    return NVPTXISD::Tex2DFloatS32;
  case Intrinsic::nvvm_tex_2d_v4f32_f32:
    return NVPTXISD::Tex2DFloatFloat;
  case Intrinsic::nvvm_tex_2d_level_v4f32_f32:
    return NVPTXISD::Tex2DFloatFloatLevel;
  case Intrinsic::nvvm_tex_2d_grad_v4f32_f32:
    return NVPTXISD::Tex2DFloatFloatGrad;
  case Intrinsic::nvvm_tex_2d_v4s32_s32:
    return NVPTXISD::Tex2DS32S32;
  case Intrinsic::nvvm_tex_2d_v4s32_f32:
    return NVPTXISD::Tex2DS32Float;
  case Intrinsic::nvvm_tex_2d_level_v4s32_f32:
    return NVPTXISD::Tex2DS32FloatLevel;
  case Intrinsic::nvvm_tex_2d_grad_v4s32_f32:
    return NVPTXISD::Tex2DS32FloatGrad;
  case Intrinsic::nvvm_tex_2d_v4u32_s32:
    return NVPTXISD::Tex2DU32S32;
  case Intrinsic::nvvm_tex_2d_v4u32_f32:
    return NVPTXISD::Tex2DU32Float;
  case Intrinsic::nvvm_tex_2d_level_v4u32_f32:
    return NVPTXISD::Tex2DU32FloatLevel;
  case Intrinsic::nvvm_tex_2d_grad_v4u32_f32:
    return NVPTXISD::Tex2DU32FloatGrad;

  case Intrinsic::nvvm_tex_2d_array_v4f32_s32:
    return NVPTXISD::Tex2DArrayFloatS32;
  case Intrinsic::nvvm_tex_2d_array_v4f32_f32:
    return NVPTXISD::Tex2DArrayFloatFloat;
  case Intrinsic::nvvm_tex_2d_array_level_v4f32_f32:
    return NVPTXISD::Tex2DArrayFloatFloatLevel;
  case Intrinsic::nvvm_tex_2d_array_grad_v4f32_f32:
    return NVPTXISD::Tex2DArrayFloatFloatGrad;
  case Intrinsic::nvvm_tex_2d_array_v4s32_s32:
    return NVPTXISD::Tex2DArrayS32S32;
  case Intrinsic::nvvm_tex_2d_array_v4s32_f32:
    return NVPTXISD::Tex2DArrayS32Float;
  case Intrinsic::nvvm_tex_2d_array_level_v4s32_f32:
    return NVPTXISD::Tex2DArrayS32FloatLevel;
  case Intrinsic::nvvm_tex_2d_array_grad_v4s32_f32:
    return NVPTXISD::Tex2DArrayS32FloatGrad;
  case Intrinsic::nvvm_tex_2d_array_v4u32_s32:
    return NVPTXISD::Tex2DArrayU32S32;
  case Intrinsic::nvvm_tex_2d_array_v4u32_f32:
    return NVPTXISD::Tex2DArrayU32Float;
  case Intrinsic::nvvm_tex_2d_array_level_v4u32_f32:
    return NVPTXISD::Tex2DArrayU32FloatLevel;
  case Intrinsic::nvvm_tex_2d_array_grad_v4u32_f32:
    return NVPTXISD::Tex2DArrayU32FloatGrad;

  case Intrinsic::nvvm_tex_3d_v4f32_s32:
    return NVPTXISD::Tex3DFloatS32;
  case Intrinsic::nvvm_tex_3d_v4f32_f32:
    return NVPTXISD::Tex3DFloatFloat;
  case Intrinsic::nvvm_tex_3d_level_v4f32_f32:
    return NVPTXISD::Tex3DFloatFloatLevel;
  case Intrinsic::nvvm_tex_3d_grad_v4f32_f32:
    return NVPTXISD::Tex3DFloatFloatGrad;
  case Intrinsic::nvvm_tex_3d_v4s32_s32:
    return NVPTXISD::Tex3DS32S32;
  case Intrinsic::nvvm_tex_3d_v4s32_f32:
    return NVPTXISD::Tex3DS32Float;
  case Intrinsic::nvvm_tex_3d_level_v4s32_f32:
    return NVPTXISD::Tex3DS32FloatLevel;
  case Intrinsic::nvvm_tex_3d_grad_v4s32_f32:
    return NVPTXISD::Tex3DS32FloatGrad;
  case Intrinsic::nvvm_tex_3d_v4u32_s32:
    return NVPTXISD::Tex3DU32S32;
  case Intrinsic::nvvm_tex_3d_v4u32_f32:
    return NVPTXISD::Tex3DU32Float;
  case Intrinsic::nvvm_tex_3d_level_v4u32_f32:
    return NVPTXISD::Tex3DU32FloatLevel;
  case Intrinsic::nvvm_tex_3d_grad_v4u32_f32:
    return NVPTXISD::Tex3DU32FloatGrad;

  case Intrinsic::nvvm_tex_cube_v4f32_f32:
    return NVPTXISD::TexCubeFloatFloat;
  case Intrinsic::nvvm_tex_cube_level_v4f32_f32:
    return NVPTXISD::TexCubeFloatFloatLevel;
  case Intrinsic::nvvm_tex_cube_v4s32_f32:
    return NVPTXISD::TexCubeS32Float;
  case Intrinsic::nvvm_tex_cube_level_v4s32_f32:
    return NVPTXISD::TexCubeS32FloatLevel;
  case Intrinsic::nvvm_tex_cube_v4u32_f32:
    return NVPTXISD::TexCubeU32Float;
  case Intrinsic::nvvm_tex_cube_level_v4u32_f32:
    return NVPTXISD::TexCubeU32FloatLevel;

  case Intrinsic::nvvm_tex_cube_array_v4f32_f32:
    return NVPTXISD::TexCubeArrayFloatFloat;
  case Intrinsic::nvvm_tex_cube_array_level_v4f32_f32:
    return NVPTXISD::TexCubeArrayFloatFloatLevel;
  case Intrinsic::nvvm_tex_cube_array_v4s32_f32:
    return NVPTXISD::TexCubeArrayS32Float;
  case Intrinsic::nvvm_tex_cube_array_level_v4s32_f32:
    return NVPTXISD::TexCubeArrayS32FloatLevel;
  case Intrinsic::nvvm_tex_cube_array_v4u32_f32:
    return NVPTXISD::TexCubeArrayU32Float;
  case Intrinsic::nvvm_tex_cube_array_level_v4u32_f32:
    return NVPTXISD::TexCubeArrayU32FloatLevel;

  case Intrinsic::nvvm_tld4_r_2d_v4f32_f32:
    return NVPTXISD::Tld4R2DFloatFloat;
  case Intrinsic::nvvm_tld4_g_2d_v4f32_f32:
    return NVPTXISD::Tld4G2DFloatFloat;
  case Intrinsic::nvvm_tld4_b_2d_v4f32_f32:
    return NVPTXISD::Tld4B2DFloatFloat;
  case Intrinsic::nvvm_tld4_a_2d_v4f32_f32:
    return NVPTXISD::Tld4A2DFloatFloat;
  case Intrinsic::nvvm_tld4_r_2d_v4s32_f32:
    return NVPTXISD::Tld4R2DS64Float;
  case Intrinsic::nvvm_tld4_g_2d_v4s32_f32:
    return NVPTXISD::Tld4G2DS64Float;
  case Intrinsic::nvvm_tld4_b_2d_v4s32_f32:
    return NVPTXISD::Tld4B2DS64Float;
  case Intrinsic::nvvm_tld4_a_2d_v4s32_f32:
    return NVPTXISD::Tld4A2DS64Float;
  case Intrinsic::nvvm_tld4_r_2d_v4u32_f32:
    return NVPTXISD::Tld4R2DU64Float;
  case Intrinsic::nvvm_tld4_g_2d_v4u32_f32:
    return NVPTXISD::Tld4G2DU64Float;
  case Intrinsic::nvvm_tld4_b_2d_v4u32_f32:
    return NVPTXISD::Tld4B2DU64Float;
  case Intrinsic::nvvm_tld4_a_2d_v4u32_f32:
    return NVPTXISD::Tld4A2DU64Float;

  case Intrinsic::nvvm_tex_unified_1d_v4f32_s32:
    return NVPTXISD::TexUnified1DFloatS32;
  case Intrinsic::nvvm_tex_unified_1d_v4f32_f32:
    return NVPTXISD::TexUnified1DFloatFloat;
  case Intrinsic::nvvm_tex_unified_1d_level_v4f32_f32:
    return NVPTXISD::TexUnified1DFloatFloatLevel;
  case Intrinsic::nvvm_tex_unified_1d_grad_v4f32_f32:
    return NVPTXISD::TexUnified1DFloatFloatGrad;
  case Intrinsic::nvvm_tex_unified_1d_v4s32_s32:
    return NVPTXISD::TexUnified1DS32S32;
  case Intrinsic::nvvm_tex_unified_1d_v4s32_f32:
    return NVPTXISD::TexUnified1DS32Float;
  case Intrinsic::nvvm_tex_unified_1d_level_v4s32_f32:
    return NVPTXISD::TexUnified1DS32FloatLevel;
  case Intrinsic::nvvm_tex_unified_1d_grad_v4s32_f32:
    return NVPTXISD::TexUnified1DS32FloatGrad;
  case Intrinsic::nvvm_tex_unified_1d_v4u32_s32:
    return NVPTXISD::TexUnified1DU32S32;
  case Intrinsic::nvvm_tex_unified_1d_v4u32_f32:
    return NVPTXISD::TexUnified1DU32Float;
  case Intrinsic::nvvm_tex_unified_1d_level_v4u32_f32:
    return NVPTXISD::TexUnified1DU32FloatLevel;
  case Intrinsic::nvvm_tex_unified_1d_grad_v4u32_f32:
    return NVPTXISD::TexUnified1DU32FloatGrad;

  case Intrinsic::nvvm_tex_unified_1d_array_v4f32_s32:
    return NVPTXISD::TexUnified1DArrayFloatS32;
  case Intrinsic::nvvm_tex_unified_1d_array_v4f32_f32:
    return NVPTXISD::TexUnified1DArrayFloatFloat;
  case Intrinsic::nvvm_tex_unified_1d_array_level_v4f32_f32:
    return NVPTXISD::TexUnified1DArrayFloatFloatLevel;
  case Intrinsic::nvvm_tex_unified_1d_array_grad_v4f32_f32:
    return NVPTXISD::TexUnified1DArrayFloatFloatGrad;
  case Intrinsic::nvvm_tex_unified_1d_array_v4s32_s32:
    return NVPTXISD::TexUnified1DArrayS32S32;
  case Intrinsic::nvvm_tex_unified_1d_array_v4s32_f32:
    return NVPTXISD::TexUnified1DArrayS32Float;
  case Intrinsic::nvvm_tex_unified_1d_array_level_v4s32_f32:
    return NVPTXISD::TexUnified1DArrayS32FloatLevel;
  case Intrinsic::nvvm_tex_unified_1d_array_grad_v4s32_f32:
    return NVPTXISD::TexUnified1DArrayS32FloatGrad;
  case Intrinsic::nvvm_tex_unified_1d_array_v4u32_s32:
    return NVPTXISD::TexUnified1DArrayU32S32;
  case Intrinsic::nvvm_tex_unified_1d_array_v4u32_f32:
    return NVPTXISD::TexUnified1DArrayU32Float;
  case Intrinsic::nvvm_tex_unified_1d_array_level_v4u32_f32:
    return NVPTXISD::TexUnified1DArrayU32FloatLevel;
  case Intrinsic::nvvm_tex_unified_1d_array_grad_v4u32_f32:
    return NVPTXISD::TexUnified1DArrayU32FloatGrad;

  case Intrinsic::nvvm_tex_unified_2d_v4f32_s32:
    return NVPTXISD::TexUnified2DFloatS32;
  case Intrinsic::nvvm_tex_unified_2d_v4f32_f32:
    return NVPTXISD::TexUnified2DFloatFloat;
  case Intrinsic::nvvm_tex_unified_2d_level_v4f32_f32:
    return NVPTXISD::TexUnified2DFloatFloatLevel;
  case Intrinsic::nvvm_tex_unified_2d_grad_v4f32_f32:
    return NVPTXISD::TexUnified2DFloatFloatGrad;
  case Intrinsic::nvvm_tex_unified_2d_v4s32_s32:
    return NVPTXISD::TexUnified2DS32S32;
  case Intrinsic::nvvm_tex_unified_2d_v4s32_f32:
    return NVPTXISD::TexUnified2DS32Float;
  case Intrinsic::nvvm_tex_unified_2d_level_v4s32_f32:
    return NVPTXISD::TexUnified2DS32FloatLevel;
  case Intrinsic::nvvm_tex_unified_2d_grad_v4s32_f32:
    return NVPTXISD::TexUnified2DS32FloatGrad;
  case Intrinsic::nvvm_tex_unified_2d_v4u32_s32:
    return NVPTXISD::TexUnified2DU32S32;
  case Intrinsic::nvvm_tex_unified_2d_v4u32_f32:
    return NVPTXISD::TexUnified2DU32Float;
  case Intrinsic::nvvm_tex_unified_2d_level_v4u32_f32:
    return NVPTXISD::TexUnified2DU32FloatLevel;
  case Intrinsic::nvvm_tex_unified_2d_grad_v4u32_f32:
    return NVPTXISD::TexUnified2DU32FloatGrad;

  case Intrinsic::nvvm_tex_unified_2d_array_v4f32_s32:
    return NVPTXISD::TexUnified2DArrayFloatS32;
  case Intrinsic::nvvm_tex_unified_2d_array_v4f32_f32:
    return NVPTXISD::TexUnified2DArrayFloatFloat;
  case Intrinsic::nvvm_tex_unified_2d_array_level_v4f32_f32:
    return NVPTXISD::TexUnified2DArrayFloatFloatLevel;
  case Intrinsic::nvvm_tex_unified_2d_array_grad_v4f32_f32:
    return NVPTXISD::TexUnified2DArrayFloatFloatGrad;
  case Intrinsic::nvvm_tex_unified_2d_array_v4s32_s32:
    return NVPTXISD::TexUnified2DArrayS32S32;
  case Intrinsic::nvvm_tex_unified_2d_array_v4s32_f32:
    return NVPTXISD::TexUnified2DArrayS32Float;
  case Intrinsic::nvvm_tex_unified_2d_array_level_v4s32_f32:
    return NVPTXISD::TexUnified2DArrayS32FloatLevel;
  case Intrinsic::nvvm_tex_unified_2d_array_grad_v4s32_f32:
    return NVPTXISD::TexUnified2DArrayS32FloatGrad;
  case Intrinsic::nvvm_tex_unified_2d_array_v4u32_s32:
    return NVPTXISD::TexUnified2DArrayU32S32;
  case Intrinsic::nvvm_tex_unified_2d_array_v4u32_f32:
    return NVPTXISD::TexUnified2DArrayU32Float;
  case Intrinsic::nvvm_tex_unified_2d_array_level_v4u32_f32:
    return NVPTXISD::TexUnified2DArrayU32FloatLevel;
  case Intrinsic::nvvm_tex_unified_2d_array_grad_v4u32_f32:
    return NVPTXISD::TexUnified2DArrayU32FloatGrad;

  case Intrinsic::nvvm_tex_unified_3d_v4f32_s32:
    return NVPTXISD::TexUnified3DFloatS32;
  case Intrinsic::nvvm_tex_unified_3d_v4f32_f32:
    return NVPTXISD::TexUnified3DFloatFloat;
  case Intrinsic::nvvm_tex_unified_3d_level_v4f32_f32:
    return NVPTXISD::TexUnified3DFloatFloatLevel;
  case Intrinsic::nvvm_tex_unified_3d_grad_v4f32_f32:
    return NVPTXISD::TexUnified3DFloatFloatGrad;
  case Intrinsic::nvvm_tex_unified_3d_v4s32_s32:
    return NVPTXISD::TexUnified3DS32S32;
  case Intrinsic::nvvm_tex_unified_3d_v4s32_f32:
    return NVPTXISD::TexUnified3DS32Float;
  case Intrinsic::nvvm_tex_unified_3d_level_v4s32_f32:
    return NVPTXISD::TexUnified3DS32FloatLevel;
  case Intrinsic::nvvm_tex_unified_3d_grad_v4s32_f32:
    return NVPTXISD::TexUnified3DS32FloatGrad;
  case Intrinsic::nvvm_tex_unified_3d_v4u32_s32:
    return NVPTXISD::TexUnified3DU32S32;
  case Intrinsic::nvvm_tex_unified_3d_v4u32_f32:
    return NVPTXISD::TexUnified3DU32Float;
  case Intrinsic::nvvm_tex_unified_3d_level_v4u32_f32:
    return NVPTXISD::TexUnified3DU32FloatLevel;
  case Intrinsic::nvvm_tex_unified_3d_grad_v4u32_f32:
    return NVPTXISD::TexUnified3DU32FloatGrad;

  case Intrinsic::nvvm_tex_unified_cube_v4f32_f32:
    return NVPTXISD::TexUnifiedCubeFloatFloat;
  case Intrinsic::nvvm_tex_unified_cube_level_v4f32_f32:
    return NVPTXISD::TexUnifiedCubeFloatFloatLevel;
  case Intrinsic::nvvm_tex_unified_cube_v4s32_f32:
    return NVPTXISD::TexUnifiedCubeS32Float;
  case Intrinsic::nvvm_tex_unified_cube_level_v4s32_f32:
    return NVPTXISD::TexUnifiedCubeS32FloatLevel;
  case Intrinsic::nvvm_tex_unified_cube_v4u32_f32:
    return NVPTXISD::TexUnifiedCubeU32Float;
  case Intrinsic::nvvm_tex_unified_cube_level_v4u32_f32:
    return NVPTXISD::TexUnifiedCubeU32FloatLevel;

  case Intrinsic::nvvm_tex_unified_cube_array_v4f32_f32:
    return NVPTXISD::TexUnifiedCubeArrayFloatFloat;
  case Intrinsic::nvvm_tex_unified_cube_array_level_v4f32_f32:
    return NVPTXISD::TexUnifiedCubeArrayFloatFloatLevel;
  case Intrinsic::nvvm_tex_unified_cube_array_v4s32_f32:
    return NVPTXISD::TexUnifiedCubeArrayS32Float;
  case Intrinsic::nvvm_tex_unified_cube_array_level_v4s32_f32:
    return NVPTXISD::TexUnifiedCubeArrayS32FloatLevel;
  case Intrinsic::nvvm_tex_unified_cube_array_v4u32_f32:
    return NVPTXISD::TexUnifiedCubeArrayU32Float;
  case Intrinsic::nvvm_tex_unified_cube_array_level_v4u32_f32:
    return NVPTXISD::TexUnifiedCubeArrayU32FloatLevel;

  case Intrinsic::nvvm_tex_unified_cube_grad_v4f32_f32:
    return NVPTXISD::TexUnifiedCubeFloatFloatGrad;
  case Intrinsic::nvvm_tex_unified_cube_grad_v4s32_f32:
    return NVPTXISD::TexUnifiedCubeS32FloatGrad;
  case Intrinsic::nvvm_tex_unified_cube_grad_v4u32_f32:
    return NVPTXISD::TexUnifiedCubeU32FloatGrad;
  case Intrinsic::nvvm_tex_unified_cube_array_grad_v4f32_f32:
    return NVPTXISD::TexUnifiedCubeArrayFloatFloatGrad;
  case Intrinsic::nvvm_tex_unified_cube_array_grad_v4s32_f32:
    return NVPTXISD::TexUnifiedCubeArrayS32FloatGrad;
  case Intrinsic::nvvm_tex_unified_cube_array_grad_v4u32_f32:
    return NVPTXISD::TexUnifiedCubeArrayU32FloatGrad;

  case Intrinsic::nvvm_tld4_unified_r_2d_v4f32_f32:
    return NVPTXISD::Tld4UnifiedR2DFloatFloat;
  case Intrinsic::nvvm_tld4_unified_g_2d_v4f32_f32:
    return NVPTXISD::Tld4UnifiedG2DFloatFloat;
  case Intrinsic::nvvm_tld4_unified_b_2d_v4f32_f32:
    return NVPTXISD::Tld4UnifiedB2DFloatFloat;
  case Intrinsic::nvvm_tld4_unified_a_2d_v4f32_f32:
    return NVPTXISD::Tld4UnifiedA2DFloatFloat;
  case Intrinsic::nvvm_tld4_unified_r_2d_v4s32_f32:
    return NVPTXISD::Tld4UnifiedR2DS64Float;
  case Intrinsic::nvvm_tld4_unified_g_2d_v4s32_f32:
    return NVPTXISD::Tld4UnifiedG2DS64Float;
  case Intrinsic::nvvm_tld4_unified_b_2d_v4s32_f32:
    return NVPTXISD::Tld4UnifiedB2DS64Float;
  case Intrinsic::nvvm_tld4_unified_a_2d_v4s32_f32:
    return NVPTXISD::Tld4UnifiedA2DS64Float;
  case Intrinsic::nvvm_tld4_unified_r_2d_v4u32_f32:
    return NVPTXISD::Tld4UnifiedR2DU64Float;
  case Intrinsic::nvvm_tld4_unified_g_2d_v4u32_f32:
    return NVPTXISD::Tld4UnifiedG2DU64Float;
  case Intrinsic::nvvm_tld4_unified_b_2d_v4u32_f32:
    return NVPTXISD::Tld4UnifiedB2DU64Float;
  case Intrinsic::nvvm_tld4_unified_a_2d_v4u32_f32:
    return NVPTXISD::Tld4UnifiedA2DU64Float;
  }
}

static unsigned getOpcForSurfaceInstr(unsigned Intrinsic) {
  switch (Intrinsic) {
  default:
    return 0;
  case Intrinsic::nvvm_suld_1d_i8_clamp:
    return NVPTXISD::Suld1DI8Clamp;
  case Intrinsic::nvvm_suld_1d_i16_clamp:
    return NVPTXISD::Suld1DI16Clamp;
  case Intrinsic::nvvm_suld_1d_i32_clamp:
    return NVPTXISD::Suld1DI32Clamp;
  case Intrinsic::nvvm_suld_1d_i64_clamp:
    return NVPTXISD::Suld1DI64Clamp;
  case Intrinsic::nvvm_suld_1d_v2i8_clamp:
    return NVPTXISD::Suld1DV2I8Clamp;
  case Intrinsic::nvvm_suld_1d_v2i16_clamp:
    return NVPTXISD::Suld1DV2I16Clamp;
  case Intrinsic::nvvm_suld_1d_v2i32_clamp:
    return NVPTXISD::Suld1DV2I32Clamp;
  case Intrinsic::nvvm_suld_1d_v2i64_clamp:
    return NVPTXISD::Suld1DV2I64Clamp;
  case Intrinsic::nvvm_suld_1d_v4i8_clamp:
    return NVPTXISD::Suld1DV4I8Clamp;
  case Intrinsic::nvvm_suld_1d_v4i16_clamp:
    return NVPTXISD::Suld1DV4I16Clamp;
  case Intrinsic::nvvm_suld_1d_v4i32_clamp:
    return NVPTXISD::Suld1DV4I32Clamp;
  case Intrinsic::nvvm_suld_1d_array_i8_clamp:
    return NVPTXISD::Suld1DArrayI8Clamp;
  case Intrinsic::nvvm_suld_1d_array_i16_clamp:
    return NVPTXISD::Suld1DArrayI16Clamp;
  case Intrinsic::nvvm_suld_1d_array_i32_clamp:
    return NVPTXISD::Suld1DArrayI32Clamp;
  case Intrinsic::nvvm_suld_1d_array_i64_clamp:
    return NVPTXISD::Suld1DArrayI64Clamp;
  case Intrinsic::nvvm_suld_1d_array_v2i8_clamp:
    return NVPTXISD::Suld1DArrayV2I8Clamp;
  case Intrinsic::nvvm_suld_1d_array_v2i16_clamp:
    return NVPTXISD::Suld1DArrayV2I16Clamp;
  case Intrinsic::nvvm_suld_1d_array_v2i32_clamp:
    return NVPTXISD::Suld1DArrayV2I32Clamp;
  case Intrinsic::nvvm_suld_1d_array_v2i64_clamp:
    return NVPTXISD::Suld1DArrayV2I64Clamp;
  case Intrinsic::nvvm_suld_1d_array_v4i8_clamp:
    return NVPTXISD::Suld1DArrayV4I8Clamp;
  case Intrinsic::nvvm_suld_1d_array_v4i16_clamp:
    return NVPTXISD::Suld1DArrayV4I16Clamp;
  case Intrinsic::nvvm_suld_1d_array_v4i32_clamp:
    return NVPTXISD::Suld1DArrayV4I32Clamp;
  case Intrinsic::nvvm_suld_2d_i8_clamp:
    return NVPTXISD::Suld2DI8Clamp;
  case Intrinsic::nvvm_suld_2d_i16_clamp:
    return NVPTXISD::Suld2DI16Clamp;
  case Intrinsic::nvvm_suld_2d_i32_clamp:
    return NVPTXISD::Suld2DI32Clamp;
  case Intrinsic::nvvm_suld_2d_i64_clamp:
    return NVPTXISD::Suld2DI64Clamp;
  case Intrinsic::nvvm_suld_2d_v2i8_clamp:
    return NVPTXISD::Suld2DV2I8Clamp;
  case Intrinsic::nvvm_suld_2d_v2i16_clamp:
    return NVPTXISD::Suld2DV2I16Clamp;
  case Intrinsic::nvvm_suld_2d_v2i32_clamp:
    return NVPTXISD::Suld2DV2I32Clamp;
  case Intrinsic::nvvm_suld_2d_v2i64_clamp:
    return NVPTXISD::Suld2DV2I64Clamp;
  case Intrinsic::nvvm_suld_2d_v4i8_clamp:
    return NVPTXISD::Suld2DV4I8Clamp;
  case Intrinsic::nvvm_suld_2d_v4i16_clamp:
    return NVPTXISD::Suld2DV4I16Clamp;
  case Intrinsic::nvvm_suld_2d_v4i32_clamp:
    return NVPTXISD::Suld2DV4I32Clamp;
  case Intrinsic::nvvm_suld_2d_array_i8_clamp:
    return NVPTXISD::Suld2DArrayI8Clamp;
  case Intrinsic::nvvm_suld_2d_array_i16_clamp:
    return NVPTXISD::Suld2DArrayI16Clamp;
  case Intrinsic::nvvm_suld_2d_array_i32_clamp:
    return NVPTXISD::Suld2DArrayI32Clamp;
  case Intrinsic::nvvm_suld_2d_array_i64_clamp:
    return NVPTXISD::Suld2DArrayI64Clamp;
  case Intrinsic::nvvm_suld_2d_array_v2i8_clamp:
    return NVPTXISD::Suld2DArrayV2I8Clamp;
  case Intrinsic::nvvm_suld_2d_array_v2i16_clamp:
    return NVPTXISD::Suld2DArrayV2I16Clamp;
  case Intrinsic::nvvm_suld_2d_array_v2i32_clamp:
    return NVPTXISD::Suld2DArrayV2I32Clamp;
  case Intrinsic::nvvm_suld_2d_array_v2i64_clamp:
    return NVPTXISD::Suld2DArrayV2I64Clamp;
  case Intrinsic::nvvm_suld_2d_array_v4i8_clamp:
    return NVPTXISD::Suld2DArrayV4I8Clamp;
  case Intrinsic::nvvm_suld_2d_array_v4i16_clamp:
    return NVPTXISD::Suld2DArrayV4I16Clamp;
  case Intrinsic::nvvm_suld_2d_array_v4i32_clamp:
    return NVPTXISD::Suld2DArrayV4I32Clamp;
  case Intrinsic::nvvm_suld_3d_i8_clamp:
    return NVPTXISD::Suld3DI8Clamp;
  case Intrinsic::nvvm_suld_3d_i16_clamp:
    return NVPTXISD::Suld3DI16Clamp;
  case Intrinsic::nvvm_suld_3d_i32_clamp:
    return NVPTXISD::Suld3DI32Clamp;
  case Intrinsic::nvvm_suld_3d_i64_clamp:
    return NVPTXISD::Suld3DI64Clamp;
  case Intrinsic::nvvm_suld_3d_v2i8_clamp:
    return NVPTXISD::Suld3DV2I8Clamp;
  case Intrinsic::nvvm_suld_3d_v2i16_clamp:
    return NVPTXISD::Suld3DV2I16Clamp;
  case Intrinsic::nvvm_suld_3d_v2i32_clamp:
    return NVPTXISD::Suld3DV2I32Clamp;
  case Intrinsic::nvvm_suld_3d_v2i64_clamp:
    return NVPTXISD::Suld3DV2I64Clamp;
  case Intrinsic::nvvm_suld_3d_v4i8_clamp:
    return NVPTXISD::Suld3DV4I8Clamp;
  case Intrinsic::nvvm_suld_3d_v4i16_clamp:
    return NVPTXISD::Suld3DV4I16Clamp;
  case Intrinsic::nvvm_suld_3d_v4i32_clamp:
    return NVPTXISD::Suld3DV4I32Clamp;
  case Intrinsic::nvvm_suld_1d_i8_trap:
    return NVPTXISD::Suld1DI8Trap;
  case Intrinsic::nvvm_suld_1d_i16_trap:
    return NVPTXISD::Suld1DI16Trap;
  case Intrinsic::nvvm_suld_1d_i32_trap:
    return NVPTXISD::Suld1DI32Trap;
  case Intrinsic::nvvm_suld_1d_i64_trap:
    return NVPTXISD::Suld1DI64Trap;
  case Intrinsic::nvvm_suld_1d_v2i8_trap:
    return NVPTXISD::Suld1DV2I8Trap;
  case Intrinsic::nvvm_suld_1d_v2i16_trap:
    return NVPTXISD::Suld1DV2I16Trap;
  case Intrinsic::nvvm_suld_1d_v2i32_trap:
    return NVPTXISD::Suld1DV2I32Trap;
  case Intrinsic::nvvm_suld_1d_v2i64_trap:
    return NVPTXISD::Suld1DV2I64Trap;
  case Intrinsic::nvvm_suld_1d_v4i8_trap:
    return NVPTXISD::Suld1DV4I8Trap;
  case Intrinsic::nvvm_suld_1d_v4i16_trap:
    return NVPTXISD::Suld1DV4I16Trap;
  case Intrinsic::nvvm_suld_1d_v4i32_trap:
    return NVPTXISD::Suld1DV4I32Trap;
  case Intrinsic::nvvm_suld_1d_array_i8_trap:
    return NVPTXISD::Suld1DArrayI8Trap;
  case Intrinsic::nvvm_suld_1d_array_i16_trap:
    return NVPTXISD::Suld1DArrayI16Trap;
  case Intrinsic::nvvm_suld_1d_array_i32_trap:
    return NVPTXISD::Suld1DArrayI32Trap;
  case Intrinsic::nvvm_suld_1d_array_i64_trap:
    return NVPTXISD::Suld1DArrayI64Trap;
  case Intrinsic::nvvm_suld_1d_array_v2i8_trap:
    return NVPTXISD::Suld1DArrayV2I8Trap;
  case Intrinsic::nvvm_suld_1d_array_v2i16_trap:
    return NVPTXISD::Suld1DArrayV2I16Trap;
  case Intrinsic::nvvm_suld_1d_array_v2i32_trap:
    return NVPTXISD::Suld1DArrayV2I32Trap;
  case Intrinsic::nvvm_suld_1d_array_v2i64_trap:
    return NVPTXISD::Suld1DArrayV2I64Trap;
  case Intrinsic::nvvm_suld_1d_array_v4i8_trap:
    return NVPTXISD::Suld1DArrayV4I8Trap;
  case Intrinsic::nvvm_suld_1d_array_v4i16_trap:
    return NVPTXISD::Suld1DArrayV4I16Trap;
  case Intrinsic::nvvm_suld_1d_array_v4i32_trap:
    return NVPTXISD::Suld1DArrayV4I32Trap;
  case Intrinsic::nvvm_suld_2d_i8_trap:
    return NVPTXISD::Suld2DI8Trap;
  case Intrinsic::nvvm_suld_2d_i16_trap:
    return NVPTXISD::Suld2DI16Trap;
  case Intrinsic::nvvm_suld_2d_i32_trap:
    return NVPTXISD::Suld2DI32Trap;
  case Intrinsic::nvvm_suld_2d_i64_trap:
    return NVPTXISD::Suld2DI64Trap;
  case Intrinsic::nvvm_suld_2d_v2i8_trap:
    return NVPTXISD::Suld2DV2I8Trap;
  case Intrinsic::nvvm_suld_2d_v2i16_trap:
    return NVPTXISD::Suld2DV2I16Trap;
  case Intrinsic::nvvm_suld_2d_v2i32_trap:
    return NVPTXISD::Suld2DV2I32Trap;
  case Intrinsic::nvvm_suld_2d_v2i64_trap:
    return NVPTXISD::Suld2DV2I64Trap;
  case Intrinsic::nvvm_suld_2d_v4i8_trap:
    return NVPTXISD::Suld2DV4I8Trap;
  case Intrinsic::nvvm_suld_2d_v4i16_trap:
    return NVPTXISD::Suld2DV4I16Trap;
  case Intrinsic::nvvm_suld_2d_v4i32_trap:
    return NVPTXISD::Suld2DV4I32Trap;
  case Intrinsic::nvvm_suld_2d_array_i8_trap:
    return NVPTXISD::Suld2DArrayI8Trap;
  case Intrinsic::nvvm_suld_2d_array_i16_trap:
    return NVPTXISD::Suld2DArrayI16Trap;
  case Intrinsic::nvvm_suld_2d_array_i32_trap:
    return NVPTXISD::Suld2DArrayI32Trap;
  case Intrinsic::nvvm_suld_2d_array_i64_trap:
    return NVPTXISD::Suld2DArrayI64Trap;
  case Intrinsic::nvvm_suld_2d_array_v2i8_trap:
    return NVPTXISD::Suld2DArrayV2I8Trap;
  case Intrinsic::nvvm_suld_2d_array_v2i16_trap:
    return NVPTXISD::Suld2DArrayV2I16Trap;
  case Intrinsic::nvvm_suld_2d_array_v2i32_trap:
    return NVPTXISD::Suld2DArrayV2I32Trap;
  case Intrinsic::nvvm_suld_2d_array_v2i64_trap:
    return NVPTXISD::Suld2DArrayV2I64Trap;
  case Intrinsic::nvvm_suld_2d_array_v4i8_trap:
    return NVPTXISD::Suld2DArrayV4I8Trap;
  case Intrinsic::nvvm_suld_2d_array_v4i16_trap:
    return NVPTXISD::Suld2DArrayV4I16Trap;
  case Intrinsic::nvvm_suld_2d_array_v4i32_trap:
    return NVPTXISD::Suld2DArrayV4I32Trap;
  case Intrinsic::nvvm_suld_3d_i8_trap:
    return NVPTXISD::Suld3DI8Trap;
  case Intrinsic::nvvm_suld_3d_i16_trap:
    return NVPTXISD::Suld3DI16Trap;
  case Intrinsic::nvvm_suld_3d_i32_trap:
    return NVPTXISD::Suld3DI32Trap;
  case Intrinsic::nvvm_suld_3d_i64_trap:
    return NVPTXISD::Suld3DI64Trap;
  case Intrinsic::nvvm_suld_3d_v2i8_trap:
    return NVPTXISD::Suld3DV2I8Trap;
  case Intrinsic::nvvm_suld_3d_v2i16_trap:
    return NVPTXISD::Suld3DV2I16Trap;
  case Intrinsic::nvvm_suld_3d_v2i32_trap:
    return NVPTXISD::Suld3DV2I32Trap;
  case Intrinsic::nvvm_suld_3d_v2i64_trap:
    return NVPTXISD::Suld3DV2I64Trap;
  case Intrinsic::nvvm_suld_3d_v4i8_trap:
    return NVPTXISD::Suld3DV4I8Trap;
  case Intrinsic::nvvm_suld_3d_v4i16_trap:
    return NVPTXISD::Suld3DV4I16Trap;
  case Intrinsic::nvvm_suld_3d_v4i32_trap:
    return NVPTXISD::Suld3DV4I32Trap;
  case Intrinsic::nvvm_suld_1d_i8_zero:
    return NVPTXISD::Suld1DI8Zero;
  case Intrinsic::nvvm_suld_1d_i16_zero:
    return NVPTXISD::Suld1DI16Zero;
  case Intrinsic::nvvm_suld_1d_i32_zero:
    return NVPTXISD::Suld1DI32Zero;
  case Intrinsic::nvvm_suld_1d_i64_zero:
    return NVPTXISD::Suld1DI64Zero;
  case Intrinsic::nvvm_suld_1d_v2i8_zero:
    return NVPTXISD::Suld1DV2I8Zero;
  case Intrinsic::nvvm_suld_1d_v2i16_zero:
    return NVPTXISD::Suld1DV2I16Zero;
  case Intrinsic::nvvm_suld_1d_v2i32_zero:
    return NVPTXISD::Suld1DV2I32Zero;
  case Intrinsic::nvvm_suld_1d_v2i64_zero:
    return NVPTXISD::Suld1DV2I64Zero;
  case Intrinsic::nvvm_suld_1d_v4i8_zero:
    return NVPTXISD::Suld1DV4I8Zero;
  case Intrinsic::nvvm_suld_1d_v4i16_zero:
    return NVPTXISD::Suld1DV4I16Zero;
  case Intrinsic::nvvm_suld_1d_v4i32_zero:
    return NVPTXISD::Suld1DV4I32Zero;
  case Intrinsic::nvvm_suld_1d_array_i8_zero:
    return NVPTXISD::Suld1DArrayI8Zero;
  case Intrinsic::nvvm_suld_1d_array_i16_zero:
    return NVPTXISD::Suld1DArrayI16Zero;
  case Intrinsic::nvvm_suld_1d_array_i32_zero:
    return NVPTXISD::Suld1DArrayI32Zero;
  case Intrinsic::nvvm_suld_1d_array_i64_zero:
    return NVPTXISD::Suld1DArrayI64Zero;
  case Intrinsic::nvvm_suld_1d_array_v2i8_zero:
    return NVPTXISD::Suld1DArrayV2I8Zero;
  case Intrinsic::nvvm_suld_1d_array_v2i16_zero:
    return NVPTXISD::Suld1DArrayV2I16Zero;
  case Intrinsic::nvvm_suld_1d_array_v2i32_zero:
    return NVPTXISD::Suld1DArrayV2I32Zero;
  case Intrinsic::nvvm_suld_1d_array_v2i64_zero:
    return NVPTXISD::Suld1DArrayV2I64Zero;
  case Intrinsic::nvvm_suld_1d_array_v4i8_zero:
    return NVPTXISD::Suld1DArrayV4I8Zero;
  case Intrinsic::nvvm_suld_1d_array_v4i16_zero:
    return NVPTXISD::Suld1DArrayV4I16Zero;
  case Intrinsic::nvvm_suld_1d_array_v4i32_zero:
    return NVPTXISD::Suld1DArrayV4I32Zero;
  case Intrinsic::nvvm_suld_2d_i8_zero:
    return NVPTXISD::Suld2DI8Zero;
  case Intrinsic::nvvm_suld_2d_i16_zero:
    return NVPTXISD::Suld2DI16Zero;
  case Intrinsic::nvvm_suld_2d_i32_zero:
    return NVPTXISD::Suld2DI32Zero;
  case Intrinsic::nvvm_suld_2d_i64_zero:
    return NVPTXISD::Suld2DI64Zero;
  case Intrinsic::nvvm_suld_2d_v2i8_zero:
    return NVPTXISD::Suld2DV2I8Zero;
  case Intrinsic::nvvm_suld_2d_v2i16_zero:
    return NVPTXISD::Suld2DV2I16Zero;
  case Intrinsic::nvvm_suld_2d_v2i32_zero:
    return NVPTXISD::Suld2DV2I32Zero;
  case Intrinsic::nvvm_suld_2d_v2i64_zero:
    return NVPTXISD::Suld2DV2I64Zero;
  case Intrinsic::nvvm_suld_2d_v4i8_zero:
    return NVPTXISD::Suld2DV4I8Zero;
  case Intrinsic::nvvm_suld_2d_v4i16_zero:
    return NVPTXISD::Suld2DV4I16Zero;
  case Intrinsic::nvvm_suld_2d_v4i32_zero:
    return NVPTXISD::Suld2DV4I32Zero;
  case Intrinsic::nvvm_suld_2d_array_i8_zero:
    return NVPTXISD::Suld2DArrayI8Zero;
  case Intrinsic::nvvm_suld_2d_array_i16_zero:
    return NVPTXISD::Suld2DArrayI16Zero;
  case Intrinsic::nvvm_suld_2d_array_i32_zero:
    return NVPTXISD::Suld2DArrayI32Zero;
  case Intrinsic::nvvm_suld_2d_array_i64_zero:
    return NVPTXISD::Suld2DArrayI64Zero;
  case Intrinsic::nvvm_suld_2d_array_v2i8_zero:
    return NVPTXISD::Suld2DArrayV2I8Zero;
  case Intrinsic::nvvm_suld_2d_array_v2i16_zero:
    return NVPTXISD::Suld2DArrayV2I16Zero;
  case Intrinsic::nvvm_suld_2d_array_v2i32_zero:
    return NVPTXISD::Suld2DArrayV2I32Zero;
  case Intrinsic::nvvm_suld_2d_array_v2i64_zero:
    return NVPTXISD::Suld2DArrayV2I64Zero;
  case Intrinsic::nvvm_suld_2d_array_v4i8_zero:
    return NVPTXISD::Suld2DArrayV4I8Zero;
  case Intrinsic::nvvm_suld_2d_array_v4i16_zero:
    return NVPTXISD::Suld2DArrayV4I16Zero;
  case Intrinsic::nvvm_suld_2d_array_v4i32_zero:
    return NVPTXISD::Suld2DArrayV4I32Zero;
  case Intrinsic::nvvm_suld_3d_i8_zero:
    return NVPTXISD::Suld3DI8Zero;
  case Intrinsic::nvvm_suld_3d_i16_zero:
    return NVPTXISD::Suld3DI16Zero;
  case Intrinsic::nvvm_suld_3d_i32_zero:
    return NVPTXISD::Suld3DI32Zero;
  case Intrinsic::nvvm_suld_3d_i64_zero:
    return NVPTXISD::Suld3DI64Zero;
  case Intrinsic::nvvm_suld_3d_v2i8_zero:
    return NVPTXISD::Suld3DV2I8Zero;
  case Intrinsic::nvvm_suld_3d_v2i16_zero:
    return NVPTXISD::Suld3DV2I16Zero;
  case Intrinsic::nvvm_suld_3d_v2i32_zero:
    return NVPTXISD::Suld3DV2I32Zero;
  case Intrinsic::nvvm_suld_3d_v2i64_zero:
    return NVPTXISD::Suld3DV2I64Zero;
  case Intrinsic::nvvm_suld_3d_v4i8_zero:
    return NVPTXISD::Suld3DV4I8Zero;
  case Intrinsic::nvvm_suld_3d_v4i16_zero:
    return NVPTXISD::Suld3DV4I16Zero;
  case Intrinsic::nvvm_suld_3d_v4i32_zero:
    return NVPTXISD::Suld3DV4I32Zero;
  }
}

// llvm.ptx.memcpy.const and llvm.ptx.memmove.const need to be modeled as
// TgtMemIntrinsic
// because we need the information that is only available in the "Value" type
// of destination
// pointer. In particular, the address space information.
bool NVPTXTargetLowering::getTgtMemIntrinsic(
    IntrinsicInfo &Info, const CallInst &I,
    MachineFunction &MF, unsigned Intrinsic) const {
  switch (Intrinsic) {
  default:
    return false;
  case Intrinsic::nvvm_match_all_sync_i32p:
  case Intrinsic::nvvm_match_all_sync_i64p:
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    // memVT is bogus. These intrinsics have IntrInaccessibleMemOnly attribute
    // in order to model data exchange with other threads, but perform no real
    // memory accesses.
    Info.memVT = MVT::i1;

    // Our result depends on both our and other thread's arguments.
    Info.flags = MachineMemOperand::MOLoad | MachineMemOperand::MOStore;
    return true;
  case Intrinsic::nvvm_wmma_m16n16k16_load_a_f16_col:
  case Intrinsic::nvvm_wmma_m16n16k16_load_a_f16_row:
  case Intrinsic::nvvm_wmma_m16n16k16_load_a_f16_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_a_f16_row_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_b_f16_col:
  case Intrinsic::nvvm_wmma_m16n16k16_load_b_f16_row:
  case Intrinsic::nvvm_wmma_m16n16k16_load_b_f16_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_b_f16_row_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_a_f16_col:
  case Intrinsic::nvvm_wmma_m32n8k16_load_a_f16_row:
  case Intrinsic::nvvm_wmma_m32n8k16_load_a_f16_col_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_a_f16_row_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_b_f16_col:
  case Intrinsic::nvvm_wmma_m32n8k16_load_b_f16_row:
  case Intrinsic::nvvm_wmma_m32n8k16_load_b_f16_col_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_b_f16_row_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_a_f16_col:
  case Intrinsic::nvvm_wmma_m8n32k16_load_a_f16_row:
  case Intrinsic::nvvm_wmma_m8n32k16_load_a_f16_col_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_a_f16_row_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_b_f16_col:
  case Intrinsic::nvvm_wmma_m8n32k16_load_b_f16_row:
  case Intrinsic::nvvm_wmma_m8n32k16_load_b_f16_col_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_b_f16_row_stride: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::v8f16;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = Align(16);
    return true;
  }
  case Intrinsic::nvvm_wmma_m16n16k16_load_a_s8_col:
  case Intrinsic::nvvm_wmma_m16n16k16_load_a_s8_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_a_u8_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_a_u8_col:
  case Intrinsic::nvvm_wmma_m16n16k16_load_a_s8_row:
  case Intrinsic::nvvm_wmma_m16n16k16_load_a_s8_row_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_a_u8_row_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_a_u8_row:
  case Intrinsic::nvvm_wmma_m8n32k16_load_a_bf16_col:
  case Intrinsic::nvvm_wmma_m8n32k16_load_a_bf16_col_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_a_bf16_row:
  case Intrinsic::nvvm_wmma_m8n32k16_load_a_bf16_row_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_b_s8_col:
  case Intrinsic::nvvm_wmma_m16n16k16_load_b_s8_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_b_u8_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_b_u8_col:
  case Intrinsic::nvvm_wmma_m16n16k16_load_b_s8_row:
  case Intrinsic::nvvm_wmma_m16n16k16_load_b_s8_row_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_b_u8_row_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_b_u8_row:
  case Intrinsic::nvvm_wmma_m32n8k16_load_b_bf16_col:
  case Intrinsic::nvvm_wmma_m32n8k16_load_b_bf16_col_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_b_bf16_row:
  case Intrinsic::nvvm_wmma_m32n8k16_load_b_bf16_row_stride: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::v2i32;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = Align(8);
    return true;
  }

  case Intrinsic::nvvm_wmma_m32n8k16_load_a_s8_col:
  case Intrinsic::nvvm_wmma_m32n8k16_load_a_s8_col_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_a_u8_col_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_a_u8_col:
  case Intrinsic::nvvm_wmma_m32n8k16_load_a_s8_row:
  case Intrinsic::nvvm_wmma_m32n8k16_load_a_s8_row_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_a_u8_row_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_a_u8_row:
  case Intrinsic::nvvm_wmma_m16n16k16_load_a_bf16_col:
  case Intrinsic::nvvm_wmma_m16n16k16_load_a_bf16_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_a_bf16_row:
  case Intrinsic::nvvm_wmma_m16n16k16_load_a_bf16_row_stride:
  case Intrinsic::nvvm_wmma_m16n16k8_load_a_tf32_col:
  case Intrinsic::nvvm_wmma_m16n16k8_load_a_tf32_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k8_load_a_tf32_row:
  case Intrinsic::nvvm_wmma_m16n16k8_load_a_tf32_row_stride:

  case Intrinsic::nvvm_wmma_m8n32k16_load_b_s8_col:
  case Intrinsic::nvvm_wmma_m8n32k16_load_b_s8_col_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_b_u8_col_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_b_u8_col:
  case Intrinsic::nvvm_wmma_m8n32k16_load_b_s8_row:
  case Intrinsic::nvvm_wmma_m8n32k16_load_b_s8_row_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_b_u8_row_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_b_u8_row:
  case Intrinsic::nvvm_wmma_m16n16k16_load_b_bf16_col:
  case Intrinsic::nvvm_wmma_m16n16k16_load_b_bf16_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_b_bf16_row:
  case Intrinsic::nvvm_wmma_m16n16k16_load_b_bf16_row_stride:
  case Intrinsic::nvvm_wmma_m16n16k8_load_b_tf32_col:
  case Intrinsic::nvvm_wmma_m16n16k8_load_b_tf32_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k8_load_b_tf32_row:
  case Intrinsic::nvvm_wmma_m16n16k8_load_b_tf32_row_stride:
  case Intrinsic::nvvm_ldmatrix_sync_aligned_m8n8_x4_b16:
  case Intrinsic::nvvm_ldmatrix_sync_aligned_m8n8_x4_trans_b16: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::v4i32;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = Align(16);
    return true;
  }

  case Intrinsic::nvvm_wmma_m32n8k16_load_b_s8_col:
  case Intrinsic::nvvm_wmma_m32n8k16_load_b_s8_col_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_b_u8_col_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_b_u8_col:
  case Intrinsic::nvvm_wmma_m32n8k16_load_b_s8_row:
  case Intrinsic::nvvm_wmma_m32n8k16_load_b_s8_row_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_b_u8_row_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_b_u8_row:

  case Intrinsic::nvvm_wmma_m8n32k16_load_a_s8_col:
  case Intrinsic::nvvm_wmma_m8n32k16_load_a_s8_col_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_a_u8_col_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_a_u8_col:
  case Intrinsic::nvvm_wmma_m8n32k16_load_a_s8_row:
  case Intrinsic::nvvm_wmma_m8n32k16_load_a_s8_row_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_a_u8_row_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_a_u8_row:
  case Intrinsic::nvvm_wmma_m8n8k128_load_a_b1_row:
  case Intrinsic::nvvm_wmma_m8n8k128_load_a_b1_row_stride:
  case Intrinsic::nvvm_wmma_m8n8k128_load_b_b1_col:
  case Intrinsic::nvvm_wmma_m8n8k128_load_b_b1_col_stride:
  case Intrinsic::nvvm_wmma_m8n8k32_load_a_s4_row:
  case Intrinsic::nvvm_wmma_m8n8k32_load_a_s4_row_stride:
  case Intrinsic::nvvm_wmma_m8n8k32_load_a_u4_row_stride:
  case Intrinsic::nvvm_wmma_m8n8k32_load_a_u4_row:
  case Intrinsic::nvvm_wmma_m8n8k32_load_b_s4_col:
  case Intrinsic::nvvm_wmma_m8n8k32_load_b_s4_col_stride:
  case Intrinsic::nvvm_wmma_m8n8k32_load_b_u4_col_stride:
  case Intrinsic::nvvm_wmma_m8n8k32_load_b_u4_col:
  case Intrinsic::nvvm_ldmatrix_sync_aligned_m8n8_x1_b16:
  case Intrinsic::nvvm_ldmatrix_sync_aligned_m8n8_x1_trans_b16: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::i32;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = Align(4);
    return true;
  }

  case Intrinsic::nvvm_wmma_m16n16k16_load_c_f16_col:
  case Intrinsic::nvvm_wmma_m16n16k16_load_c_f16_row:
  case Intrinsic::nvvm_wmma_m16n16k16_load_c_f16_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_c_f16_row_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_c_f16_col:
  case Intrinsic::nvvm_wmma_m32n8k16_load_c_f16_row:
  case Intrinsic::nvvm_wmma_m32n8k16_load_c_f16_col_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_c_f16_row_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_c_f16_col:
  case Intrinsic::nvvm_wmma_m8n32k16_load_c_f16_row:
  case Intrinsic::nvvm_wmma_m8n32k16_load_c_f16_col_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_c_f16_row_stride: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::v4f16;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = Align(16);
    return true;
  }

  case Intrinsic::nvvm_wmma_m16n16k16_load_c_f32_col:
  case Intrinsic::nvvm_wmma_m16n16k16_load_c_f32_row:
  case Intrinsic::nvvm_wmma_m16n16k16_load_c_f32_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_c_f32_row_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_c_f32_col:
  case Intrinsic::nvvm_wmma_m32n8k16_load_c_f32_row:
  case Intrinsic::nvvm_wmma_m32n8k16_load_c_f32_col_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_c_f32_row_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_c_f32_col:
  case Intrinsic::nvvm_wmma_m8n32k16_load_c_f32_row:
  case Intrinsic::nvvm_wmma_m8n32k16_load_c_f32_col_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_c_f32_row_stride:
  case Intrinsic::nvvm_wmma_m16n16k8_load_c_f32_col:
  case Intrinsic::nvvm_wmma_m16n16k8_load_c_f32_row:
  case Intrinsic::nvvm_wmma_m16n16k8_load_c_f32_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k8_load_c_f32_row_stride: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::v8f32;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = Align(16);
    return true;
  }

  case Intrinsic::nvvm_wmma_m32n8k16_load_a_bf16_col:
  case Intrinsic::nvvm_wmma_m32n8k16_load_a_bf16_col_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_a_bf16_row:
  case Intrinsic::nvvm_wmma_m32n8k16_load_a_bf16_row_stride:

  case Intrinsic::nvvm_wmma_m8n32k16_load_b_bf16_col:
  case Intrinsic::nvvm_wmma_m8n32k16_load_b_bf16_col_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_b_bf16_row:
  case Intrinsic::nvvm_wmma_m8n32k16_load_b_bf16_row_stride:

  case Intrinsic::nvvm_wmma_m16n16k16_load_c_s32_col:
  case Intrinsic::nvvm_wmma_m16n16k16_load_c_s32_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_load_c_s32_row:
  case Intrinsic::nvvm_wmma_m16n16k16_load_c_s32_row_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_c_s32_col:
  case Intrinsic::nvvm_wmma_m32n8k16_load_c_s32_col_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_load_c_s32_row:
  case Intrinsic::nvvm_wmma_m32n8k16_load_c_s32_row_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_c_s32_col:
  case Intrinsic::nvvm_wmma_m8n32k16_load_c_s32_col_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_load_c_s32_row:
  case Intrinsic::nvvm_wmma_m8n32k16_load_c_s32_row_stride: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::v8i32;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = Align(16);
    return true;
  }

  case Intrinsic::nvvm_wmma_m8n8k128_load_c_s32_col:
  case Intrinsic::nvvm_wmma_m8n8k128_load_c_s32_col_stride:
  case Intrinsic::nvvm_wmma_m8n8k128_load_c_s32_row:
  case Intrinsic::nvvm_wmma_m8n8k128_load_c_s32_row_stride:
  case Intrinsic::nvvm_wmma_m8n8k32_load_c_s32_col:
  case Intrinsic::nvvm_wmma_m8n8k32_load_c_s32_col_stride:
  case Intrinsic::nvvm_wmma_m8n8k32_load_c_s32_row:
  case Intrinsic::nvvm_wmma_m8n8k32_load_c_s32_row_stride:
  case Intrinsic::nvvm_ldmatrix_sync_aligned_m8n8_x2_b16:
  case Intrinsic::nvvm_ldmatrix_sync_aligned_m8n8_x2_trans_b16: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::v2i32;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = Align(8);
    return true;
  }

  case Intrinsic::nvvm_wmma_m8n8k4_load_a_f64_col:
  case Intrinsic::nvvm_wmma_m8n8k4_load_a_f64_col_stride:
  case Intrinsic::nvvm_wmma_m8n8k4_load_a_f64_row:
  case Intrinsic::nvvm_wmma_m8n8k4_load_a_f64_row_stride:

  case Intrinsic::nvvm_wmma_m8n8k4_load_b_f64_col:
  case Intrinsic::nvvm_wmma_m8n8k4_load_b_f64_col_stride:
  case Intrinsic::nvvm_wmma_m8n8k4_load_b_f64_row:
  case Intrinsic::nvvm_wmma_m8n8k4_load_b_f64_row_stride: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::f64;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = Align(8);
    return true;
  }

  case Intrinsic::nvvm_wmma_m8n8k4_load_c_f64_col:
  case Intrinsic::nvvm_wmma_m8n8k4_load_c_f64_col_stride:
  case Intrinsic::nvvm_wmma_m8n8k4_load_c_f64_row:
  case Intrinsic::nvvm_wmma_m8n8k4_load_c_f64_row_stride: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::v2f64;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = Align(16);
    return true;
  }

  case Intrinsic::nvvm_wmma_m16n16k16_store_d_f16_col:
  case Intrinsic::nvvm_wmma_m16n16k16_store_d_f16_row:
  case Intrinsic::nvvm_wmma_m16n16k16_store_d_f16_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_store_d_f16_row_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_store_d_f16_col:
  case Intrinsic::nvvm_wmma_m32n8k16_store_d_f16_row:
  case Intrinsic::nvvm_wmma_m32n8k16_store_d_f16_col_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_store_d_f16_row_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_store_d_f16_col:
  case Intrinsic::nvvm_wmma_m8n32k16_store_d_f16_row:
  case Intrinsic::nvvm_wmma_m8n32k16_store_d_f16_col_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_store_d_f16_row_stride: {
    Info.opc = ISD::INTRINSIC_VOID;
    Info.memVT = MVT::v4f16;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOStore;
    Info.align = Align(16);
    return true;
  }

  case Intrinsic::nvvm_wmma_m16n16k16_store_d_f32_col:
  case Intrinsic::nvvm_wmma_m16n16k16_store_d_f32_row:
  case Intrinsic::nvvm_wmma_m16n16k16_store_d_f32_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_store_d_f32_row_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_store_d_f32_col:
  case Intrinsic::nvvm_wmma_m32n8k16_store_d_f32_row:
  case Intrinsic::nvvm_wmma_m32n8k16_store_d_f32_col_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_store_d_f32_row_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_store_d_f32_col:
  case Intrinsic::nvvm_wmma_m8n32k16_store_d_f32_row:
  case Intrinsic::nvvm_wmma_m8n32k16_store_d_f32_col_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_store_d_f32_row_stride:
  case Intrinsic::nvvm_wmma_m16n16k8_store_d_f32_col:
  case Intrinsic::nvvm_wmma_m16n16k8_store_d_f32_row:
  case Intrinsic::nvvm_wmma_m16n16k8_store_d_f32_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k8_store_d_f32_row_stride: {
    Info.opc = ISD::INTRINSIC_VOID;
    Info.memVT = MVT::v8f32;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOStore;
    Info.align = Align(16);
    return true;
  }

  case Intrinsic::nvvm_wmma_m16n16k16_store_d_s32_col:
  case Intrinsic::nvvm_wmma_m16n16k16_store_d_s32_col_stride:
  case Intrinsic::nvvm_wmma_m16n16k16_store_d_s32_row:
  case Intrinsic::nvvm_wmma_m16n16k16_store_d_s32_row_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_store_d_s32_col:
  case Intrinsic::nvvm_wmma_m32n8k16_store_d_s32_col_stride:
  case Intrinsic::nvvm_wmma_m32n8k16_store_d_s32_row:
  case Intrinsic::nvvm_wmma_m32n8k16_store_d_s32_row_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_store_d_s32_col:
  case Intrinsic::nvvm_wmma_m8n32k16_store_d_s32_col_stride:
  case Intrinsic::nvvm_wmma_m8n32k16_store_d_s32_row:
  case Intrinsic::nvvm_wmma_m8n32k16_store_d_s32_row_stride: {
    Info.opc = ISD::INTRINSIC_VOID;
    Info.memVT = MVT::v8i32;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOStore;
    Info.align = Align(16);
    return true;
  }

  case Intrinsic::nvvm_wmma_m8n8k128_store_d_s32_col:
  case Intrinsic::nvvm_wmma_m8n8k128_store_d_s32_col_stride:
  case Intrinsic::nvvm_wmma_m8n8k128_store_d_s32_row:
  case Intrinsic::nvvm_wmma_m8n8k128_store_d_s32_row_stride:
  case Intrinsic::nvvm_wmma_m8n8k32_store_d_s32_col:
  case Intrinsic::nvvm_wmma_m8n8k32_store_d_s32_col_stride:
  case Intrinsic::nvvm_wmma_m8n8k32_store_d_s32_row:
  case Intrinsic::nvvm_wmma_m8n8k32_store_d_s32_row_stride: {
    Info.opc = ISD::INTRINSIC_VOID;
    Info.memVT = MVT::v2i32;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOStore;
    Info.align = Align(8);
    return true;
  }

  case Intrinsic::nvvm_wmma_m8n8k4_store_d_f64_col:
  case Intrinsic::nvvm_wmma_m8n8k4_store_d_f64_col_stride:
  case Intrinsic::nvvm_wmma_m8n8k4_store_d_f64_row:
  case Intrinsic::nvvm_wmma_m8n8k4_store_d_f64_row_stride: {
    Info.opc = ISD::INTRINSIC_VOID;
    Info.memVT = MVT::v2f64;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOStore;
    Info.align = Align(16);
    return true;
  }

  case Intrinsic::nvvm_atomic_load_inc_32:
  case Intrinsic::nvvm_atomic_load_dec_32:

  case Intrinsic::nvvm_atomic_add_gen_f_cta:
  case Intrinsic::nvvm_atomic_add_gen_f_sys:
  case Intrinsic::nvvm_atomic_add_gen_i_cta:
  case Intrinsic::nvvm_atomic_add_gen_i_sys:
  case Intrinsic::nvvm_atomic_and_gen_i_cta:
  case Intrinsic::nvvm_atomic_and_gen_i_sys:
  case Intrinsic::nvvm_atomic_cas_gen_i_cta:
  case Intrinsic::nvvm_atomic_cas_gen_i_sys:
  case Intrinsic::nvvm_atomic_dec_gen_i_cta:
  case Intrinsic::nvvm_atomic_dec_gen_i_sys:
  case Intrinsic::nvvm_atomic_inc_gen_i_cta:
  case Intrinsic::nvvm_atomic_inc_gen_i_sys:
  case Intrinsic::nvvm_atomic_max_gen_i_cta:
  case Intrinsic::nvvm_atomic_max_gen_i_sys:
  case Intrinsic::nvvm_atomic_min_gen_i_cta:
  case Intrinsic::nvvm_atomic_min_gen_i_sys:
  case Intrinsic::nvvm_atomic_or_gen_i_cta:
  case Intrinsic::nvvm_atomic_or_gen_i_sys:
  case Intrinsic::nvvm_atomic_exch_gen_i_cta:
  case Intrinsic::nvvm_atomic_exch_gen_i_sys:
  case Intrinsic::nvvm_atomic_xor_gen_i_cta:
  case Intrinsic::nvvm_atomic_xor_gen_i_sys: {
    auto &DL = I.getDataLayout();
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = getValueType(DL, I.getType());
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad | MachineMemOperand::MOStore;
    Info.align.reset();
    return true;
  }

  case Intrinsic::nvvm_ldu_global_i:
  case Intrinsic::nvvm_ldu_global_f:
  case Intrinsic::nvvm_ldu_global_p: {
    auto &DL = I.getDataLayout();
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    if (Intrinsic == Intrinsic::nvvm_ldu_global_i)
      Info.memVT = getValueType(DL, I.getType());
    else if(Intrinsic == Intrinsic::nvvm_ldu_global_p)
      Info.memVT = getPointerTy(DL);
    else
      Info.memVT = getValueType(DL, I.getType());
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = cast<ConstantInt>(I.getArgOperand(1))->getMaybeAlignValue();

    return true;
  }
  case Intrinsic::nvvm_ldg_global_i:
  case Intrinsic::nvvm_ldg_global_f:
  case Intrinsic::nvvm_ldg_global_p: {
    auto &DL = I.getDataLayout();

    Info.opc = ISD::INTRINSIC_W_CHAIN;
    if (Intrinsic == Intrinsic::nvvm_ldg_global_i)
      Info.memVT = getValueType(DL, I.getType());
    else if(Intrinsic == Intrinsic::nvvm_ldg_global_p)
      Info.memVT = getPointerTy(DL);
    else
      Info.memVT = getValueType(DL, I.getType());
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = cast<ConstantInt>(I.getArgOperand(1))->getMaybeAlignValue();

    return true;
  }

  case Intrinsic::nvvm_tex_1d_v4f32_s32:
  case Intrinsic::nvvm_tex_1d_v4f32_f32:
  case Intrinsic::nvvm_tex_1d_level_v4f32_f32:
  case Intrinsic::nvvm_tex_1d_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_1d_array_v4f32_s32:
  case Intrinsic::nvvm_tex_1d_array_v4f32_f32:
  case Intrinsic::nvvm_tex_1d_array_level_v4f32_f32:
  case Intrinsic::nvvm_tex_1d_array_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_2d_v4f32_s32:
  case Intrinsic::nvvm_tex_2d_v4f32_f32:
  case Intrinsic::nvvm_tex_2d_level_v4f32_f32:
  case Intrinsic::nvvm_tex_2d_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_2d_array_v4f32_s32:
  case Intrinsic::nvvm_tex_2d_array_v4f32_f32:
  case Intrinsic::nvvm_tex_2d_array_level_v4f32_f32:
  case Intrinsic::nvvm_tex_2d_array_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_3d_v4f32_s32:
  case Intrinsic::nvvm_tex_3d_v4f32_f32:
  case Intrinsic::nvvm_tex_3d_level_v4f32_f32:
  case Intrinsic::nvvm_tex_3d_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_cube_v4f32_f32:
  case Intrinsic::nvvm_tex_cube_level_v4f32_f32:
  case Intrinsic::nvvm_tex_cube_array_v4f32_f32:
  case Intrinsic::nvvm_tex_cube_array_level_v4f32_f32:
  case Intrinsic::nvvm_tld4_r_2d_v4f32_f32:
  case Intrinsic::nvvm_tld4_g_2d_v4f32_f32:
  case Intrinsic::nvvm_tld4_b_2d_v4f32_f32:
  case Intrinsic::nvvm_tld4_a_2d_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_1d_v4f32_s32:
  case Intrinsic::nvvm_tex_unified_1d_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_1d_level_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_1d_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_1d_array_v4f32_s32:
  case Intrinsic::nvvm_tex_unified_1d_array_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_1d_array_level_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_1d_array_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_2d_v4f32_s32:
  case Intrinsic::nvvm_tex_unified_2d_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_2d_level_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_2d_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_2d_array_v4f32_s32:
  case Intrinsic::nvvm_tex_unified_2d_array_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_2d_array_level_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_2d_array_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_3d_v4f32_s32:
  case Intrinsic::nvvm_tex_unified_3d_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_3d_level_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_3d_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_cube_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_cube_level_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_cube_array_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_cube_array_level_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_cube_grad_v4f32_f32:
  case Intrinsic::nvvm_tex_unified_cube_array_grad_v4f32_f32:
  case Intrinsic::nvvm_tld4_unified_r_2d_v4f32_f32:
  case Intrinsic::nvvm_tld4_unified_g_2d_v4f32_f32:
  case Intrinsic::nvvm_tld4_unified_b_2d_v4f32_f32:
  case Intrinsic::nvvm_tld4_unified_a_2d_v4f32_f32:
    Info.opc = getOpcForTextureInstr(Intrinsic);
    Info.memVT = MVT::v4f32;
    Info.ptrVal = nullptr;
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = Align(16);
    return true;

  case Intrinsic::nvvm_tex_1d_v4s32_s32:
  case Intrinsic::nvvm_tex_1d_v4s32_f32:
  case Intrinsic::nvvm_tex_1d_level_v4s32_f32:
  case Intrinsic::nvvm_tex_1d_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_1d_array_v4s32_s32:
  case Intrinsic::nvvm_tex_1d_array_v4s32_f32:
  case Intrinsic::nvvm_tex_1d_array_level_v4s32_f32:
  case Intrinsic::nvvm_tex_1d_array_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_2d_v4s32_s32:
  case Intrinsic::nvvm_tex_2d_v4s32_f32:
  case Intrinsic::nvvm_tex_2d_level_v4s32_f32:
  case Intrinsic::nvvm_tex_2d_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_2d_array_v4s32_s32:
  case Intrinsic::nvvm_tex_2d_array_v4s32_f32:
  case Intrinsic::nvvm_tex_2d_array_level_v4s32_f32:
  case Intrinsic::nvvm_tex_2d_array_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_3d_v4s32_s32:
  case Intrinsic::nvvm_tex_3d_v4s32_f32:
  case Intrinsic::nvvm_tex_3d_level_v4s32_f32:
  case Intrinsic::nvvm_tex_3d_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_cube_v4s32_f32:
  case Intrinsic::nvvm_tex_cube_level_v4s32_f32:
  case Intrinsic::nvvm_tex_cube_array_v4s32_f32:
  case Intrinsic::nvvm_tex_cube_array_level_v4s32_f32:
  case Intrinsic::nvvm_tex_cube_v4u32_f32:
  case Intrinsic::nvvm_tex_cube_level_v4u32_f32:
  case Intrinsic::nvvm_tex_cube_array_v4u32_f32:
  case Intrinsic::nvvm_tex_cube_array_level_v4u32_f32:
  case Intrinsic::nvvm_tex_1d_v4u32_s32:
  case Intrinsic::nvvm_tex_1d_v4u32_f32:
  case Intrinsic::nvvm_tex_1d_level_v4u32_f32:
  case Intrinsic::nvvm_tex_1d_grad_v4u32_f32:
  case Intrinsic::nvvm_tex_1d_array_v4u32_s32:
  case Intrinsic::nvvm_tex_1d_array_v4u32_f32:
  case Intrinsic::nvvm_tex_1d_array_level_v4u32_f32:
  case Intrinsic::nvvm_tex_1d_array_grad_v4u32_f32:
  case Intrinsic::nvvm_tex_2d_v4u32_s32:
  case Intrinsic::nvvm_tex_2d_v4u32_f32:
  case Intrinsic::nvvm_tex_2d_level_v4u32_f32:
  case Intrinsic::nvvm_tex_2d_grad_v4u32_f32:
  case Intrinsic::nvvm_tex_2d_array_v4u32_s32:
  case Intrinsic::nvvm_tex_2d_array_v4u32_f32:
  case Intrinsic::nvvm_tex_2d_array_level_v4u32_f32:
  case Intrinsic::nvvm_tex_2d_array_grad_v4u32_f32:
  case Intrinsic::nvvm_tex_3d_v4u32_s32:
  case Intrinsic::nvvm_tex_3d_v4u32_f32:
  case Intrinsic::nvvm_tex_3d_level_v4u32_f32:
  case Intrinsic::nvvm_tex_3d_grad_v4u32_f32:
  case Intrinsic::nvvm_tld4_r_2d_v4s32_f32:
  case Intrinsic::nvvm_tld4_g_2d_v4s32_f32:
  case Intrinsic::nvvm_tld4_b_2d_v4s32_f32:
  case Intrinsic::nvvm_tld4_a_2d_v4s32_f32:
  case Intrinsic::nvvm_tld4_r_2d_v4u32_f32:
  case Intrinsic::nvvm_tld4_g_2d_v4u32_f32:
  case Intrinsic::nvvm_tld4_b_2d_v4u32_f32:
  case Intrinsic::nvvm_tld4_a_2d_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_1d_v4s32_s32:
  case Intrinsic::nvvm_tex_unified_1d_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_1d_level_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_1d_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_1d_array_v4s32_s32:
  case Intrinsic::nvvm_tex_unified_1d_array_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_1d_array_level_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_1d_array_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_2d_v4s32_s32:
  case Intrinsic::nvvm_tex_unified_2d_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_2d_level_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_2d_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_2d_array_v4s32_s32:
  case Intrinsic::nvvm_tex_unified_2d_array_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_2d_array_level_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_2d_array_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_3d_v4s32_s32:
  case Intrinsic::nvvm_tex_unified_3d_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_3d_level_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_3d_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_1d_v4u32_s32:
  case Intrinsic::nvvm_tex_unified_1d_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_1d_level_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_1d_grad_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_1d_array_v4u32_s32:
  case Intrinsic::nvvm_tex_unified_1d_array_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_1d_array_level_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_1d_array_grad_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_2d_v4u32_s32:
  case Intrinsic::nvvm_tex_unified_2d_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_2d_level_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_2d_grad_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_2d_array_v4u32_s32:
  case Intrinsic::nvvm_tex_unified_2d_array_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_2d_array_level_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_2d_array_grad_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_3d_v4u32_s32:
  case Intrinsic::nvvm_tex_unified_3d_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_3d_level_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_3d_grad_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_cube_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_cube_level_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_cube_array_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_cube_array_level_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_cube_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_cube_level_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_cube_array_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_cube_array_level_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_cube_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_cube_grad_v4u32_f32:
  case Intrinsic::nvvm_tex_unified_cube_array_grad_v4s32_f32:
  case Intrinsic::nvvm_tex_unified_cube_array_grad_v4u32_f32:
  case Intrinsic::nvvm_tld4_unified_r_2d_v4s32_f32:
  case Intrinsic::nvvm_tld4_unified_g_2d_v4s32_f32:
  case Intrinsic::nvvm_tld4_unified_b_2d_v4s32_f32:
  case Intrinsic::nvvm_tld4_unified_a_2d_v4s32_f32:
  case Intrinsic::nvvm_tld4_unified_r_2d_v4u32_f32:
  case Intrinsic::nvvm_tld4_unified_g_2d_v4u32_f32:
  case Intrinsic::nvvm_tld4_unified_b_2d_v4u32_f32:
  case Intrinsic::nvvm_tld4_unified_a_2d_v4u32_f32:
    Info.opc = getOpcForTextureInstr(Intrinsic);
    Info.memVT = MVT::v4i32;
    Info.ptrVal = nullptr;
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = Align(16);
    return true;

  case Intrinsic::nvvm_suld_1d_i8_clamp:
  case Intrinsic::nvvm_suld_1d_v2i8_clamp:
  case Intrinsic::nvvm_suld_1d_v4i8_clamp:
  case Intrinsic::nvvm_suld_1d_array_i8_clamp:
  case Intrinsic::nvvm_suld_1d_array_v2i8_clamp:
  case Intrinsic::nvvm_suld_1d_array_v4i8_clamp:
  case Intrinsic::nvvm_suld_2d_i8_clamp:
  case Intrinsic::nvvm_suld_2d_v2i8_clamp:
  case Intrinsic::nvvm_suld_2d_v4i8_clamp:
  case Intrinsic::nvvm_suld_2d_array_i8_clamp:
  case Intrinsic::nvvm_suld_2d_array_v2i8_clamp:
  case Intrinsic::nvvm_suld_2d_array_v4i8_clamp:
  case Intrinsic::nvvm_suld_3d_i8_clamp:
  case Intrinsic::nvvm_suld_3d_v2i8_clamp:
  case Intrinsic::nvvm_suld_3d_v4i8_clamp:
  case Intrinsic::nvvm_suld_1d_i8_trap:
  case Intrinsic::nvvm_suld_1d_v2i8_trap:
  case Intrinsic::nvvm_suld_1d_v4i8_trap:
  case Intrinsic::nvvm_suld_1d_array_i8_trap:
  case Intrinsic::nvvm_suld_1d_array_v2i8_trap:
  case Intrinsic::nvvm_suld_1d_array_v4i8_trap:
  case Intrinsic::nvvm_suld_2d_i8_trap:
  case Intrinsic::nvvm_suld_2d_v2i8_trap:
  case Intrinsic::nvvm_suld_2d_v4i8_trap:
  case Intrinsic::nvvm_suld_2d_array_i8_trap:
  case Intrinsic::nvvm_suld_2d_array_v2i8_trap:
  case Intrinsic::nvvm_suld_2d_array_v4i8_trap:
  case Intrinsic::nvvm_suld_3d_i8_trap:
  case Intrinsic::nvvm_suld_3d_v2i8_trap:
  case Intrinsic::nvvm_suld_3d_v4i8_trap:
  case Intrinsic::nvvm_suld_1d_i8_zero:
  case Intrinsic::nvvm_suld_1d_v2i8_zero:
  case Intrinsic::nvvm_suld_1d_v4i8_zero:
  case Intrinsic::nvvm_suld_1d_array_i8_zero:
  case Intrinsic::nvvm_suld_1d_array_v2i8_zero:
  case Intrinsic::nvvm_suld_1d_array_v4i8_zero:
  case Intrinsic::nvvm_suld_2d_i8_zero:
  case Intrinsic::nvvm_suld_2d_v2i8_zero:
  case Intrinsic::nvvm_suld_2d_v4i8_zero:
  case Intrinsic::nvvm_suld_2d_array_i8_zero:
  case Intrinsic::nvvm_suld_2d_array_v2i8_zero:
  case Intrinsic::nvvm_suld_2d_array_v4i8_zero:
  case Intrinsic::nvvm_suld_3d_i8_zero:
  case Intrinsic::nvvm_suld_3d_v2i8_zero:
  case Intrinsic::nvvm_suld_3d_v4i8_zero:
    Info.opc = getOpcForSurfaceInstr(Intrinsic);
    Info.memVT = MVT::i8;
    Info.ptrVal = nullptr;
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = Align(16);
    return true;

  case Intrinsic::nvvm_suld_1d_i16_clamp:
  case Intrinsic::nvvm_suld_1d_v2i16_clamp:
  case Intrinsic::nvvm_suld_1d_v4i16_clamp:
  case Intrinsic::nvvm_suld_1d_array_i16_clamp:
  case Intrinsic::nvvm_suld_1d_array_v2i16_clamp:
  case Intrinsic::nvvm_suld_1d_array_v4i16_clamp:
  case Intrinsic::nvvm_suld_2d_i16_clamp:
  case Intrinsic::nvvm_suld_2d_v2i16_clamp:
  case Intrinsic::nvvm_suld_2d_v4i16_clamp:
  case Intrinsic::nvvm_suld_2d_array_i16_clamp:
  case Intrinsic::nvvm_suld_2d_array_v2i16_clamp:
  case Intrinsic::nvvm_suld_2d_array_v4i16_clamp:
  case Intrinsic::nvvm_suld_3d_i16_clamp:
  case Intrinsic::nvvm_suld_3d_v2i16_clamp:
  case Intrinsic::nvvm_suld_3d_v4i16_clamp:
  case Intrinsic::nvvm_suld_1d_i16_trap:
  case Intrinsic::nvvm_suld_1d_v2i16_trap:
  case Intrinsic::nvvm_suld_1d_v4i16_trap:
  case Intrinsic::nvvm_suld_1d_array_i16_trap:
  case Intrinsic::nvvm_suld_1d_array_v2i16_trap:
  case Intrinsic::nvvm_suld_1d_array_v4i16_trap:
  case Intrinsic::nvvm_suld_2d_i16_trap:
  case Intrinsic::nvvm_suld_2d_v2i16_trap:
  case Intrinsic::nvvm_suld_2d_v4i16_trap:
  case Intrinsic::nvvm_suld_2d_array_i16_trap:
  case Intrinsic::nvvm_suld_2d_array_v2i16_trap:
  case Intrinsic::nvvm_suld_2d_array_v4i16_trap:
  case Intrinsic::nvvm_suld_3d_i16_trap:
  case Intrinsic::nvvm_suld_3d_v2i16_trap:
  case Intrinsic::nvvm_suld_3d_v4i16_trap:
  case Intrinsic::nvvm_suld_1d_i16_zero:
  case Intrinsic::nvvm_suld_1d_v2i16_zero:
  case Intrinsic::nvvm_suld_1d_v4i16_zero:
  case Intrinsic::nvvm_suld_1d_array_i16_zero:
  case Intrinsic::nvvm_suld_1d_array_v2i16_zero:
  case Intrinsic::nvvm_suld_1d_array_v4i16_zero:
  case Intrinsic::nvvm_suld_2d_i16_zero:
  case Intrinsic::nvvm_suld_2d_v2i16_zero:
  case Intrinsic::nvvm_suld_2d_v4i16_zero:
  case Intrinsic::nvvm_suld_2d_array_i16_zero:
  case Intrinsic::nvvm_suld_2d_array_v2i16_zero:
  case Intrinsic::nvvm_suld_2d_array_v4i16_zero:
  case Intrinsic::nvvm_suld_3d_i16_zero:
  case Intrinsic::nvvm_suld_3d_v2i16_zero:
  case Intrinsic::nvvm_suld_3d_v4i16_zero:
    Info.opc = getOpcForSurfaceInstr(Intrinsic);
    Info.memVT = MVT::i16;
    Info.ptrVal = nullptr;
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = Align(16);
    return true;

  case Intrinsic::nvvm_suld_1d_i32_clamp:
  case Intrinsic::nvvm_suld_1d_v2i32_clamp:
  case Intrinsic::nvvm_suld_1d_v4i32_clamp:
  case Intrinsic::nvvm_suld_1d_array_i32_clamp:
  case Intrinsic::nvvm_suld_1d_array_v2i32_clamp:
  case Intrinsic::nvvm_suld_1d_array_v4i32_clamp:
  case Intrinsic::nvvm_suld_2d_i32_clamp:
  case Intrinsic::nvvm_suld_2d_v2i32_clamp:
  case Intrinsic::nvvm_suld_2d_v4i32_clamp:
  case Intrinsic::nvvm_suld_2d_array_i32_clamp:
  case Intrinsic::nvvm_suld_2d_array_v2i32_clamp:
  case Intrinsic::nvvm_suld_2d_array_v4i32_clamp:
  case Intrinsic::nvvm_suld_3d_i32_clamp:
  case Intrinsic::nvvm_suld_3d_v2i32_clamp:
  case Intrinsic::nvvm_suld_3d_v4i32_clamp:
  case Intrinsic::nvvm_suld_1d_i32_trap:
  case Intrinsic::nvvm_suld_1d_v2i32_trap:
  case Intrinsic::nvvm_suld_1d_v4i32_trap:
  case Intrinsic::nvvm_suld_1d_array_i32_trap:
  case Intrinsic::nvvm_suld_1d_array_v2i32_trap:
  case Intrinsic::nvvm_suld_1d_array_v4i32_trap:
  case Intrinsic::nvvm_suld_2d_i32_trap:
  case Intrinsic::nvvm_suld_2d_v2i32_trap:
  case Intrinsic::nvvm_suld_2d_v4i32_trap:
  case Intrinsic::nvvm_suld_2d_array_i32_trap:
  case Intrinsic::nvvm_suld_2d_array_v2i32_trap:
  case Intrinsic::nvvm_suld_2d_array_v4i32_trap:
  case Intrinsic::nvvm_suld_3d_i32_trap:
  case Intrinsic::nvvm_suld_3d_v2i32_trap:
  case Intrinsic::nvvm_suld_3d_v4i32_trap:
  case Intrinsic::nvvm_suld_1d_i32_zero:
  case Intrinsic::nvvm_suld_1d_v2i32_zero:
  case Intrinsic::nvvm_suld_1d_v4i32_zero:
  case Intrinsic::nvvm_suld_1d_array_i32_zero:
  case Intrinsic::nvvm_suld_1d_array_v2i32_zero:
  case Intrinsic::nvvm_suld_1d_array_v4i32_zero:
  case Intrinsic::nvvm_suld_2d_i32_zero:
  case Intrinsic::nvvm_suld_2d_v2i32_zero:
  case Intrinsic::nvvm_suld_2d_v4i32_zero:
  case Intrinsic::nvvm_suld_2d_array_i32_zero:
  case Intrinsic::nvvm_suld_2d_array_v2i32_zero:
  case Intrinsic::nvvm_suld_2d_array_v4i32_zero:
  case Intrinsic::nvvm_suld_3d_i32_zero:
  case Intrinsic::nvvm_suld_3d_v2i32_zero:
  case Intrinsic::nvvm_suld_3d_v4i32_zero:
    Info.opc = getOpcForSurfaceInstr(Intrinsic);
    Info.memVT = MVT::i32;
    Info.ptrVal = nullptr;
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = Align(16);
    return true;

  case Intrinsic::nvvm_suld_1d_i64_clamp:
  case Intrinsic::nvvm_suld_1d_v2i64_clamp:
  case Intrinsic::nvvm_suld_1d_array_i64_clamp:
  case Intrinsic::nvvm_suld_1d_array_v2i64_clamp:
  case Intrinsic::nvvm_suld_2d_i64_clamp:
  case Intrinsic::nvvm_suld_2d_v2i64_clamp:
  case Intrinsic::nvvm_suld_2d_array_i64_clamp:
  case Intrinsic::nvvm_suld_2d_array_v2i64_clamp:
  case Intrinsic::nvvm_suld_3d_i64_clamp:
  case Intrinsic::nvvm_suld_3d_v2i64_clamp:
  case Intrinsic::nvvm_suld_1d_i64_trap:
  case Intrinsic::nvvm_suld_1d_v2i64_trap:
  case Intrinsic::nvvm_suld_1d_array_i64_trap:
  case Intrinsic::nvvm_suld_1d_array_v2i64_trap:
  case Intrinsic::nvvm_suld_2d_i64_trap:
  case Intrinsic::nvvm_suld_2d_v2i64_trap:
  case Intrinsic::nvvm_suld_2d_array_i64_trap:
  case Intrinsic::nvvm_suld_2d_array_v2i64_trap:
  case Intrinsic::nvvm_suld_3d_i64_trap:
  case Intrinsic::nvvm_suld_3d_v2i64_trap:
  case Intrinsic::nvvm_suld_1d_i64_zero:
  case Intrinsic::nvvm_suld_1d_v2i64_zero:
  case Intrinsic::nvvm_suld_1d_array_i64_zero:
  case Intrinsic::nvvm_suld_1d_array_v2i64_zero:
  case Intrinsic::nvvm_suld_2d_i64_zero:
  case Intrinsic::nvvm_suld_2d_v2i64_zero:
  case Intrinsic::nvvm_suld_2d_array_i64_zero:
  case Intrinsic::nvvm_suld_2d_array_v2i64_zero:
  case Intrinsic::nvvm_suld_3d_i64_zero:
  case Intrinsic::nvvm_suld_3d_v2i64_zero:
    Info.opc = getOpcForSurfaceInstr(Intrinsic);
    Info.memVT = MVT::i64;
    Info.ptrVal = nullptr;
    Info.offset = 0;
    Info.flags = MachineMemOperand::MOLoad;
    Info.align = Align(16);
    return true;
  }
  return false;
}

/// getFunctionParamOptimizedAlign - since function arguments are passed via
/// .param space, we may want to increase their alignment in a way that
/// ensures that we can effectively vectorize their loads & stores. We can
/// increase alignment only if the function has internal or has private
/// linkage as for other linkage types callers may already rely on default
/// alignment. To allow using 128-bit vectorized loads/stores, this function
/// ensures that alignment is 16 or greater.
Align NVPTXTargetLowering::getFunctionParamOptimizedAlign(
    const Function *F, Type *ArgTy, const DataLayout &DL) const {
  // Capping the alignment to 128 bytes as that is the maximum alignment
  // supported by PTX.
  const Align ABITypeAlign = std::min(Align(128), DL.getABITypeAlign(ArgTy));

  // If a function has linkage different from internal or private, we
  // must use default ABI alignment as external users rely on it. Same
  // for a function that may be called from a function pointer.
  if (!F || !F->hasLocalLinkage() ||
      F->hasAddressTaken(/*Users=*/nullptr,
                         /*IgnoreCallbackUses=*/false,
                         /*IgnoreAssumeLikeCalls=*/true,
                         /*IgnoreLLVMUsed=*/true))
    return ABITypeAlign;

  assert(!isKernelFunction(*F) && "Expect kernels to have non-local linkage");
  return std::max(Align(16), ABITypeAlign);
}

/// Helper for computing alignment of a device function byval parameter.
Align NVPTXTargetLowering::getFunctionByValParamAlign(
    const Function *F, Type *ArgTy, Align InitialAlign,
    const DataLayout &DL) const {
  Align ArgAlign = InitialAlign;
  // Try to increase alignment to enhance vectorization options.
  if (F)
    ArgAlign = std::max(ArgAlign, getFunctionParamOptimizedAlign(F, ArgTy, DL));

  // Old ptx versions have a bug. When PTX code takes address of
  // byval parameter with alignment < 4, ptxas generates code to
  // spill argument into memory. Alas on sm_50+ ptxas generates
  // SASS code that fails with misaligned access. To work around
  // the problem, make sure that we align byval parameters by at
  // least 4. This bug seems to be fixed at least starting from
  // ptxas > 9.0.
  // TODO: remove this after verifying the bug is not reproduced
  // on non-deprecated ptxas versions.
  if (ForceMinByValParamAlign)
    ArgAlign = std::max(ArgAlign, Align(4));

  return ArgAlign;
}

// Helper for getting a function parameter name. Name is composed from
// its index and the function name. Negative index corresponds to special
// parameter (unsized array) used for passing variable arguments.
std::string NVPTXTargetLowering::getParamName(const Function *F,
                                              int Idx) const {
  std::string ParamName;
  raw_string_ostream ParamStr(ParamName);

  ParamStr << getTargetMachine().getSymbol(F)->getName();
  if (Idx < 0)
    ParamStr << "_vararg";
  else
    ParamStr << "_param_" << Idx;

  return ParamName;
}

/// isLegalAddressingMode - Return true if the addressing mode represented
/// by AM is legal for this target, for a load/store of the specified type.
/// Used to guide target specific optimizations, like loop strength reduction
/// (LoopStrengthReduce.cpp) and memory optimization for address mode
/// (CodeGenPrepare.cpp)
bool NVPTXTargetLowering::isLegalAddressingMode(const DataLayout &DL,
                                                const AddrMode &AM, Type *Ty,
                                                unsigned AS, Instruction *I) const {
  // AddrMode - This represents an addressing mode of:
  //    BaseGV + BaseOffs + BaseReg + Scale*ScaleReg
  //
  // The legal address modes are
  // - [avar]
  // - [areg]
  // - [areg+immoff]
  // - [immAddr]

  // immoff must fit in a signed 32-bit int
  if (!APInt(64, AM.BaseOffs).isSignedIntN(32))
    return false;

  if (AM.BaseGV)
    return !AM.BaseOffs && !AM.HasBaseReg && !AM.Scale;

  switch (AM.Scale) {
  case 0: // "r", "r+i" or "i" is allowed
    break;
  case 1:
    if (AM.HasBaseReg) // "r+r+i" or "r+r" is not allowed.
      return false;
    // Otherwise we have r+i.
    break;
  default:
    // No scale > 1 is allowed
    return false;
  }
  return true;
}

//===----------------------------------------------------------------------===//
//                         NVPTX Inline Assembly Support
//===----------------------------------------------------------------------===//

/// getConstraintType - Given a constraint letter, return the type of
/// constraint it is for this target.
NVPTXTargetLowering::ConstraintType
NVPTXTargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default:
      break;
    case 'b':
    case 'r':
    case 'h':
    case 'c':
    case 'l':
    case 'f':
    case 'd':
    case 'q':
    case '0':
    case 'N':
      return C_RegisterClass;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

std::pair<unsigned, const TargetRegisterClass *>
NVPTXTargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                  StringRef Constraint,
                                                  MVT VT) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'b':
      return std::make_pair(0U, &NVPTX::Int1RegsRegClass);
    case 'c':
      return std::make_pair(0U, &NVPTX::Int16RegsRegClass);
    case 'h':
      return std::make_pair(0U, &NVPTX::Int16RegsRegClass);
    case 'r':
      return std::make_pair(0U, &NVPTX::Int32RegsRegClass);
    case 'l':
    case 'N':
      return std::make_pair(0U, &NVPTX::Int64RegsRegClass);
    case 'q': {
      if (STI.getSmVersion() < 70)
        report_fatal_error("Inline asm with 128 bit operands is only "
                           "supported for sm_70 and higher!");
      return std::make_pair(0U, &NVPTX::Int128RegsRegClass);
    }
    case 'f':
      return std::make_pair(0U, &NVPTX::Float32RegsRegClass);
    case 'd':
      return std::make_pair(0U, &NVPTX::Float64RegsRegClass);
    }
  }
  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

//===----------------------------------------------------------------------===//
//                         NVPTX DAG Combining
//===----------------------------------------------------------------------===//

bool NVPTXTargetLowering::allowFMA(MachineFunction &MF,
                                   CodeGenOptLevel OptLevel) const {
  // Always honor command-line argument
  if (FMAContractLevelOpt.getNumOccurrences() > 0)
    return FMAContractLevelOpt > 0;

  // Do not contract if we're not optimizing the code.
  if (OptLevel == CodeGenOptLevel::None)
    return false;

  // Honor TargetOptions flags that explicitly say fusion is okay.
  if (MF.getTarget().Options.AllowFPOpFusion == FPOpFusion::Fast)
    return true;

  return allowUnsafeFPMath(MF);
}

bool NVPTXTargetLowering::allowUnsafeFPMath(MachineFunction &MF) const {
  // Honor TargetOptions flags that explicitly say unsafe math is okay.
  if (MF.getTarget().Options.UnsafeFPMath)
    return true;

  // Allow unsafe math if unsafe-fp-math attribute explicitly says so.
  const Function &F = MF.getFunction();
  return F.getFnAttribute("unsafe-fp-math").getValueAsBool();
}

static bool isConstZero(const SDValue &Operand) {
  const auto *Const = dyn_cast<ConstantSDNode>(Operand);
  return Const && Const->getZExtValue() == 0;
}

/// PerformADDCombineWithOperands - Try DAG combinations for an ADD with
/// operands N0 and N1.  This is a helper for PerformADDCombine that is
/// called with the default operands, and if that fails, with commuted
/// operands.
static SDValue
PerformADDCombineWithOperands(SDNode *N, SDValue N0, SDValue N1,
                              TargetLowering::DAGCombinerInfo &DCI) {
  EVT VT = N0.getValueType();

  // Since integer multiply-add costs the same as integer multiply
  // but is more costly than integer add, do the fusion only when
  // the mul is only used in the add.
  // TODO: this may not be true for later architectures, consider relaxing this
  if (!N0.getNode()->hasOneUse())
    return SDValue();

  // fold (add (mul a, b), c) -> (mad a, b, c)
  //
  if (N0.getOpcode() == ISD::MUL)
    return DCI.DAG.getNode(NVPTXISD::IMAD, SDLoc(N), VT, N0.getOperand(0),
                           N0.getOperand(1), N1);

  // fold (add (select cond, 0, (mul a, b)), c)
  //   -> (select cond, c, (mad a, b, c))
  //
  if (N0.getOpcode() == ISD::SELECT) {
    unsigned ZeroOpNum;
    if (isConstZero(N0->getOperand(1)))
      ZeroOpNum = 1;
    else if (isConstZero(N0->getOperand(2)))
      ZeroOpNum = 2;
    else
      return SDValue();

    SDValue M = N0->getOperand((ZeroOpNum == 1) ? 2 : 1);
    if (M->getOpcode() != ISD::MUL || !M.getNode()->hasOneUse())
      return SDValue();

    SDValue MAD = DCI.DAG.getNode(NVPTXISD::IMAD, SDLoc(N), VT,
                                  M->getOperand(0), M->getOperand(1), N1);
    return DCI.DAG.getSelect(SDLoc(N), VT, N0->getOperand(0),
                             ((ZeroOpNum == 1) ? N1 : MAD),
                             ((ZeroOpNum == 1) ? MAD : N1));
  }

  return SDValue();
}

static SDValue
PerformFADDCombineWithOperands(SDNode *N, SDValue N0, SDValue N1,
                               TargetLowering::DAGCombinerInfo &DCI,
                               CodeGenOptLevel OptLevel) {
  EVT VT = N0.getValueType();
  if (N0.getOpcode() == ISD::FMUL) {
    const auto *TLI = static_cast<const NVPTXTargetLowering *>(
        &DCI.DAG.getTargetLoweringInfo());
    if (!TLI->allowFMA(DCI.DAG.getMachineFunction(), OptLevel))
      return SDValue();

    // For floating point:
    // Do the fusion only when the mul has less than 5 uses and all
    // are add.
    // The heuristic is that if a use is not an add, then that use
    // cannot be fused into fma, therefore mul is still needed anyway.
    // If there are more than 4 uses, even if they are all add, fusing
    // them will increase register pressue.
    //
    int numUses = 0;
    int nonAddCount = 0;
    for (const SDNode *User : N0.getNode()->uses()) {
      numUses++;
      if (User->getOpcode() != ISD::FADD)
        ++nonAddCount;
      if (numUses >= 5)
        return SDValue();
    }
    if (nonAddCount) {
      int orderNo = N->getIROrder();
      int orderNo2 = N0.getNode()->getIROrder();
      // simple heuristics here for considering potential register
      // pressure, the logics here is that the differnce are used
      // to measure the distance between def and use, the longer distance
      // more likely cause register pressure.
      if (orderNo - orderNo2 < 500)
        return SDValue();

      // Now, check if at least one of the FMUL's operands is live beyond the
      // node N, which guarantees that the FMA will not increase register
      // pressure at node N.
      bool opIsLive = false;
      const SDNode *left = N0.getOperand(0).getNode();
      const SDNode *right = N0.getOperand(1).getNode();

      if (isa<ConstantSDNode>(left) || isa<ConstantSDNode>(right))
        opIsLive = true;

      if (!opIsLive)
        for (const SDNode *User : left->uses()) {
          int orderNo3 = User->getIROrder();
          if (orderNo3 > orderNo) {
            opIsLive = true;
            break;
          }
        }

      if (!opIsLive)
        for (const SDNode *User : right->uses()) {
          int orderNo3 = User->getIROrder();
          if (orderNo3 > orderNo) {
            opIsLive = true;
            break;
          }
        }

      if (!opIsLive)
        return SDValue();
    }

    return DCI.DAG.getNode(ISD::FMA, SDLoc(N), VT, N0.getOperand(0),
                           N0.getOperand(1), N1);
  }

  return SDValue();
}

static SDValue PerformStoreCombineHelper(SDNode *N, std::size_t Front,
                                         std::size_t Back) {
  if (all_of(N->ops().drop_front(Front).drop_back(Back),
             [](const SDUse &U) { return U.get()->isUndef(); }))
    // Operand 0 is the previous value in the chain. Cannot return EntryToken
    // as the previous value will become unused and eliminated later.
    return N->getOperand(0);

  return SDValue();
}

static SDValue PerformStoreParamCombine(SDNode *N) {
  // Operands from the 3rd to the 2nd last one are the values to be stored.
  //   {Chain, ArgID, Offset, Val, Glue}
  return PerformStoreCombineHelper(N, 3, 1);
}

static SDValue PerformStoreRetvalCombine(SDNode *N) {
  // Operands from the 2nd to the last one are the values to be stored
  return PerformStoreCombineHelper(N, 2, 0);
}

/// PerformADDCombine - Target-specific dag combine xforms for ISD::ADD.
///
static SDValue PerformADDCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 CodeGenOptLevel OptLevel) {
  if (OptLevel == CodeGenOptLevel::None)
    return SDValue();

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  // Skip non-integer, non-scalar case
  EVT VT = N0.getValueType();
  if (VT.isVector() || VT != MVT::i32)
    return SDValue();

  // First try with the default operand order.
  if (SDValue Result = PerformADDCombineWithOperands(N, N0, N1, DCI))
    return Result;

  // If that didn't work, try again with the operands commuted.
  return PerformADDCombineWithOperands(N, N1, N0, DCI);
}

/// PerformFADDCombine - Target-specific dag combine xforms for ISD::FADD.
///
static SDValue PerformFADDCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 CodeGenOptLevel OptLevel) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  EVT VT = N0.getValueType();
  if (VT.isVector() || !(VT == MVT::f32 || VT == MVT::f64))
    return SDValue();

  // First try with the default operand order.
  if (SDValue Result = PerformFADDCombineWithOperands(N, N0, N1, DCI, OptLevel))
    return Result;

  // If that didn't work, try again with the operands commuted.
  return PerformFADDCombineWithOperands(N, N1, N0, DCI, OptLevel);
}

static SDValue PerformANDCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI) {
  // The type legalizer turns a vector load of i8 values into a zextload to i16
  // registers, optionally ANY_EXTENDs it (if target type is integer),
  // and ANDs off the high 8 bits. Since we turn this load into a
  // target-specific DAG node, the DAG combiner fails to eliminate these AND
  // nodes. Do that here.
  SDValue Val = N->getOperand(0);
  SDValue Mask = N->getOperand(1);

  if (isa<ConstantSDNode>(Val)) {
    std::swap(Val, Mask);
  }

  SDValue AExt;

  // Convert BFE-> truncate i16 -> and 255
  // To just BFE-> truncate i16, as the value already has all the bits in the
  // right places.
  if (Val.getOpcode() == ISD::TRUNCATE) {
    SDValue BFE = Val.getOperand(0);
    if (BFE.getOpcode() != NVPTXISD::BFE)
      return SDValue();

    ConstantSDNode *BFEBits = dyn_cast<ConstantSDNode>(BFE.getOperand(0));
    if (!BFEBits)
      return SDValue();
    uint64_t BFEBitsVal = BFEBits->getZExtValue();

    ConstantSDNode *MaskCnst = dyn_cast<ConstantSDNode>(Mask);
    if (!MaskCnst) {
      // Not an AND with a constant
      return SDValue();
    }
    uint64_t MaskVal = MaskCnst->getZExtValue();

    if (MaskVal != (uint64_t(1) << BFEBitsVal) - 1)
      return SDValue();
    // If we get here, the AND is unnecessary.  Just replace it with the trunc
    DCI.CombineTo(N, Val, false);
  }
  // Generally, we will see zextload -> IMOV16rr -> ANY_EXTEND -> and
  if (Val.getOpcode() == ISD::ANY_EXTEND) {
    AExt = Val;
    Val = Val->getOperand(0);
  }

  if (Val->isMachineOpcode() && Val->getMachineOpcode() == NVPTX::IMOV16rr) {
    Val = Val->getOperand(0);
  }

  if (Val->getOpcode() == NVPTXISD::LoadV2 ||
      Val->getOpcode() == NVPTXISD::LoadV4) {
    ConstantSDNode *MaskCnst = dyn_cast<ConstantSDNode>(Mask);
    if (!MaskCnst) {
      // Not an AND with a constant
      return SDValue();
    }

    uint64_t MaskVal = MaskCnst->getZExtValue();
    if (MaskVal != 0xff) {
      // Not an AND that chops off top 8 bits
      return SDValue();
    }

    MemSDNode *Mem = dyn_cast<MemSDNode>(Val);
    if (!Mem) {
      // Not a MemSDNode?!?
      return SDValue();
    }

    EVT MemVT = Mem->getMemoryVT();
    if (MemVT != MVT::v2i8 && MemVT != MVT::v4i8) {
      // We only handle the i8 case
      return SDValue();
    }

    unsigned ExtType = Val->getConstantOperandVal(Val->getNumOperands() - 1);
    if (ExtType == ISD::SEXTLOAD) {
      // If for some reason the load is a sextload, the and is needed to zero
      // out the high 8 bits
      return SDValue();
    }

    bool AddTo = false;
    if (AExt.getNode() != nullptr) {
      // Re-insert the ext as a zext.
      Val = DCI.DAG.getNode(ISD::ZERO_EXTEND, SDLoc(N),
                            AExt.getValueType(), Val);
      AddTo = true;
    }

    // If we get here, the AND is unnecessary.  Just replace it with the load
    DCI.CombineTo(N, Val, AddTo);
  }

  return SDValue();
}

static SDValue PerformREMCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 CodeGenOptLevel OptLevel) {
  assert(N->getOpcode() == ISD::SREM || N->getOpcode() == ISD::UREM);

  // Don't do anything at less than -O2.
  if (OptLevel < CodeGenOptLevel::Default)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);
  EVT VT = N->getValueType(0);
  bool IsSigned = N->getOpcode() == ISD::SREM;
  unsigned DivOpc = IsSigned ? ISD::SDIV : ISD::UDIV;

  const SDValue &Num = N->getOperand(0);
  const SDValue &Den = N->getOperand(1);

  for (const SDNode *U : Num->uses()) {
    if (U->getOpcode() == DivOpc && U->getOperand(0) == Num &&
        U->getOperand(1) == Den) {
      // Num % Den -> Num - (Num / Den) * Den
      return DAG.getNode(ISD::SUB, DL, VT, Num,
                         DAG.getNode(ISD::MUL, DL, VT,
                                     DAG.getNode(DivOpc, DL, VT, Num, Den),
                                     Den));
    }
  }
  return SDValue();
}

enum OperandSignedness {
  Signed = 0,
  Unsigned,
  Unknown
};

/// IsMulWideOperandDemotable - Checks if the provided DAG node is an operand
/// that can be demoted to \p OptSize bits without loss of information. The
/// signedness of the operand, if determinable, is placed in \p S.
static bool IsMulWideOperandDemotable(SDValue Op,
                                      unsigned OptSize,
                                      OperandSignedness &S) {
  S = Unknown;

  if (Op.getOpcode() == ISD::SIGN_EXTEND ||
      Op.getOpcode() == ISD::SIGN_EXTEND_INREG) {
    EVT OrigVT = Op.getOperand(0).getValueType();
    if (OrigVT.getFixedSizeInBits() <= OptSize) {
      S = Signed;
      return true;
    }
  } else if (Op.getOpcode() == ISD::ZERO_EXTEND) {
    EVT OrigVT = Op.getOperand(0).getValueType();
    if (OrigVT.getFixedSizeInBits() <= OptSize) {
      S = Unsigned;
      return true;
    }
  }

  return false;
}

/// AreMulWideOperandsDemotable - Checks if the given LHS and RHS operands can
/// be demoted to \p OptSize bits without loss of information. If the operands
/// contain a constant, it should appear as the RHS operand. The signedness of
/// the operands is placed in \p IsSigned.
static bool AreMulWideOperandsDemotable(SDValue LHS, SDValue RHS,
                                        unsigned OptSize,
                                        bool &IsSigned) {
  OperandSignedness LHSSign;

  // The LHS operand must be a demotable op
  if (!IsMulWideOperandDemotable(LHS, OptSize, LHSSign))
    return false;

  // We should have been able to determine the signedness from the LHS
  if (LHSSign == Unknown)
    return false;

  IsSigned = (LHSSign == Signed);

  // The RHS can be a demotable op or a constant
  if (ConstantSDNode *CI = dyn_cast<ConstantSDNode>(RHS)) {
    const APInt &Val = CI->getAPIntValue();
    if (LHSSign == Unsigned) {
      return Val.isIntN(OptSize);
    } else {
      return Val.isSignedIntN(OptSize);
    }
  } else {
    OperandSignedness RHSSign;
    if (!IsMulWideOperandDemotable(RHS, OptSize, RHSSign))
      return false;

    return LHSSign == RHSSign;
  }
}

/// TryMULWIDECombine - Attempt to replace a multiply of M bits with a multiply
/// of M/2 bits that produces an M-bit result (i.e. mul.wide). This transform
/// works on both multiply DAG nodes and SHL DAG nodes with a constant shift
/// amount.
static SDValue TryMULWIDECombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI) {
  EVT MulType = N->getValueType(0);
  if (MulType != MVT::i32 && MulType != MVT::i64) {
    return SDValue();
  }

  SDLoc DL(N);
  unsigned OptSize = MulType.getSizeInBits() >> 1;
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  // Canonicalize the multiply so the constant (if any) is on the right
  if (N->getOpcode() == ISD::MUL) {
    if (isa<ConstantSDNode>(LHS)) {
      std::swap(LHS, RHS);
    }
  }

  // If we have a SHL, determine the actual multiply amount
  if (N->getOpcode() == ISD::SHL) {
    ConstantSDNode *ShlRHS = dyn_cast<ConstantSDNode>(RHS);
    if (!ShlRHS) {
      return SDValue();
    }

    APInt ShiftAmt = ShlRHS->getAPIntValue();
    unsigned BitWidth = MulType.getSizeInBits();
    if (ShiftAmt.sge(0) && ShiftAmt.slt(BitWidth)) {
      APInt MulVal = APInt(BitWidth, 1) << ShiftAmt;
      RHS = DCI.DAG.getConstant(MulVal, DL, MulType);
    } else {
      return SDValue();
    }
  }

  bool Signed;
  // Verify that our operands are demotable
  if (!AreMulWideOperandsDemotable(LHS, RHS, OptSize, Signed)) {
    return SDValue();
  }

  EVT DemotedVT;
  if (MulType == MVT::i32) {
    DemotedVT = MVT::i16;
  } else {
    DemotedVT = MVT::i32;
  }

  // Truncate the operands to the correct size. Note that these are just for
  // type consistency and will (likely) be eliminated in later phases.
  SDValue TruncLHS =
    DCI.DAG.getNode(ISD::TRUNCATE, DL, DemotedVT, LHS);
  SDValue TruncRHS =
    DCI.DAG.getNode(ISD::TRUNCATE, DL, DemotedVT, RHS);

  unsigned Opc;
  if (Signed) {
    Opc = NVPTXISD::MUL_WIDE_SIGNED;
  } else {
    Opc = NVPTXISD::MUL_WIDE_UNSIGNED;
  }

  return DCI.DAG.getNode(Opc, DL, MulType, TruncLHS, TruncRHS);
}

static bool isConstOne(const SDValue &Operand) {
  const auto *Const = dyn_cast<ConstantSDNode>(Operand);
  return Const && Const->getZExtValue() == 1;
}

static SDValue matchMADConstOnePattern(SDValue Add) {
  if (Add->getOpcode() != ISD::ADD)
    return SDValue();

  if (isConstOne(Add->getOperand(0)))
    return Add->getOperand(1);

  if (isConstOne(Add->getOperand(1)))
    return Add->getOperand(0);

  return SDValue();
}

static SDValue combineMADConstOne(SDValue X, SDValue Add, EVT VT, SDLoc DL,
                                  TargetLowering::DAGCombinerInfo &DCI) {

  if (SDValue Y = matchMADConstOnePattern(Add))
    return DCI.DAG.getNode(NVPTXISD::IMAD, DL, VT, X, Y, X);

  return SDValue();
}

static SDValue combineMulSelectConstOne(SDValue X, SDValue Select, EVT VT,
                                        SDLoc DL,
                                        TargetLowering::DAGCombinerInfo &DCI) {
  if (Select->getOpcode() != ISD::SELECT)
    return SDValue();

  SDValue Cond = Select->getOperand(0);

  unsigned ConstOpNo;
  if (isConstOne(Select->getOperand(1)))
    ConstOpNo = 1;
  else if (isConstOne(Select->getOperand(2)))
    ConstOpNo = 2;
  else
    return SDValue();

  SDValue Y = Select->getOperand((ConstOpNo == 1) ? 2 : 1);

  // Do not combine if the resulting sequence is not obviously profitable.
  if (!matchMADConstOnePattern(Y))
    return SDValue();

  SDValue NewMul = DCI.DAG.getNode(ISD::MUL, DL, VT, X, Y);

  return DCI.DAG.getNode(ISD::SELECT, DL, VT, Cond,
                         (ConstOpNo == 1) ? X : NewMul,
                         (ConstOpNo == 1) ? NewMul : X);
}

static SDValue
PerformMULCombineWithOperands(SDNode *N, SDValue N0, SDValue N1,
                              TargetLowering::DAGCombinerInfo &DCI) {

  EVT VT = N0.getValueType();
  if (VT.isVector())
    return SDValue();

  if (VT != MVT::i16 && VT != MVT::i32 && VT != MVT::i64)
    return SDValue();

  SDLoc DL(N);

  // (mul x, (add y, 1)) -> (mad x, y, x)
  if (SDValue Res = combineMADConstOne(N0, N1, VT, DL, DCI))
    return Res;
  if (SDValue Res = combineMADConstOne(N1, N0, VT, DL, DCI))
    return Res;

  // (mul x, (select y, 1)) -> (select (mul x, y), x)
  if (SDValue Res = combineMulSelectConstOne(N0, N1, VT, DL, DCI))
    return Res;
  if (SDValue Res = combineMulSelectConstOne(N1, N0, VT, DL, DCI))
    return Res;

  return SDValue();
}

/// PerformMULCombine - Runs PTX-specific DAG combine patterns on MUL nodes.
static SDValue PerformMULCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 CodeGenOptLevel OptLevel) {
  if (OptLevel == CodeGenOptLevel::None)
    return SDValue();

  if (SDValue Ret = TryMULWIDECombine(N, DCI))
    return Ret;

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  return PerformMULCombineWithOperands(N, N0, N1, DCI);
}

/// PerformSHLCombine - Runs PTX-specific DAG combine patterns on SHL nodes.
static SDValue PerformSHLCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 CodeGenOptLevel OptLevel) {
  if (OptLevel > CodeGenOptLevel::None) {
    // Try mul.wide combining at OptLevel > 0
    if (SDValue Ret = TryMULWIDECombine(N, DCI))
      return Ret;
  }

  return SDValue();
}

static SDValue PerformSETCCCombine(SDNode *N,
                                   TargetLowering::DAGCombinerInfo &DCI,
                                   unsigned int SmVersion) {
  EVT CCType = N->getValueType(0);
  SDValue A = N->getOperand(0);
  SDValue B = N->getOperand(1);

  EVT AType = A.getValueType();
  if (!(CCType == MVT::v2i1 && (AType == MVT::v2f16 || AType == MVT::v2bf16)))
    return SDValue();

  if (A.getValueType() == MVT::v2bf16 && SmVersion < 90)
    return SDValue();

  SDLoc DL(N);
  // setp.f16x2 returns two scalar predicates, which we need to
  // convert back to v2i1. The returned result will be scalarized by
  // the legalizer, but the comparison will remain a single vector
  // instruction.
  SDValue CCNode = DCI.DAG.getNode(
      A.getValueType() == MVT::v2f16 ? NVPTXISD::SETP_F16X2
                                     : NVPTXISD::SETP_BF16X2,
      DL, DCI.DAG.getVTList(MVT::i1, MVT::i1), {A, B, N->getOperand(2)});
  return DCI.DAG.getNode(ISD::BUILD_VECTOR, DL, CCType, CCNode.getValue(0),
                         CCNode.getValue(1));
}

static SDValue PerformEXTRACTCombine(SDNode *N,
                                     TargetLowering::DAGCombinerInfo &DCI) {
  SDValue Vector = N->getOperand(0);
  SDLoc DL(N);
  EVT VectorVT = Vector.getValueType();
  if (Vector->getOpcode() == ISD::LOAD && VectorVT.isSimple() &&
      IsPTXVectorType(VectorVT.getSimpleVT()))
    return SDValue(); // Native vector loads already combine nicely w/
                      // extract_vector_elt.
  // Don't mess with singletons or v2*16, v4i8 and v8i8 types, we already
  // handle them OK.
  if (VectorVT.getVectorNumElements() == 1 || Isv2x16VT(VectorVT) ||
      VectorVT == MVT::v4i8 || VectorVT == MVT::v8i8)
    return SDValue();

  // Don't mess with undef values as sra may be simplified to 0, not undef.
  if (Vector->isUndef() || ISD::allOperandsUndef(Vector.getNode()))
    return SDValue();

  uint64_t VectorBits = VectorVT.getSizeInBits();
  // We only handle the types we can extract in-register.
  if (!(VectorBits == 16 || VectorBits == 32 || VectorBits == 64))
    return SDValue();

  ConstantSDNode *Index = dyn_cast<ConstantSDNode>(N->getOperand(1));
  // Index == 0 is handled by generic DAG combiner.
  if (!Index || Index->getZExtValue() == 0)
    return SDValue();

  MVT IVT = MVT::getIntegerVT(VectorBits);
  EVT EltVT = VectorVT.getVectorElementType();
  EVT EltIVT = EltVT.changeTypeToInteger();
  uint64_t EltBits = EltVT.getScalarSizeInBits();

  SDValue Result = DCI.DAG.getNode(
      ISD::TRUNCATE, DL, EltIVT,
      DCI.DAG.getNode(
          ISD::SRA, DL, IVT, DCI.DAG.getNode(ISD::BITCAST, DL, IVT, Vector),
          DCI.DAG.getConstant(Index->getZExtValue() * EltBits, DL, IVT)));

  // If element has non-integer type, bitcast it back to the expected type.
  if (EltVT != EltIVT)
    Result = DCI.DAG.getNode(ISD::BITCAST, DL, EltVT, Result);
  // Past legalizer, we may need to extent i8 -> i16 to match the register type.
  if (EltVT != N->getValueType(0))
    Result = DCI.DAG.getNode(ISD::ANY_EXTEND, DL, N->getValueType(0), Result);

  return Result;
}

static SDValue PerformVSELECTCombine(SDNode *N,
                                     TargetLowering::DAGCombinerInfo &DCI) {
  SDValue VA = N->getOperand(1);
  EVT VectorVT = VA.getValueType();
  if (VectorVT != MVT::v4i8)
    return SDValue();

  // We need to split vselect into individual per-element operations Because we
  // use BFE/BFI instruction for byte extraction/insertion, we do end up with
  // 32-bit values, so we may as well do comparison as i32 to avoid conversions
  // to/from i16 normally used for i8 values.
  SmallVector<SDValue, 4> E;
  SDLoc DL(N);
  SDValue VCond = N->getOperand(0);
  SDValue VB = N->getOperand(2);
  for (int I = 0; I < 4; ++I) {
    SDValue C = DCI.DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::i1, VCond,
                                DCI.DAG.getConstant(I, DL, MVT::i32));
    SDValue EA = DCI.DAG.getAnyExtOrTrunc(
        DCI.DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::i8, VA,
                        DCI.DAG.getConstant(I, DL, MVT::i32)),
        DL, MVT::i32);
    SDValue EB = DCI.DAG.getAnyExtOrTrunc(
        DCI.DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::i8, VB,
                        DCI.DAG.getConstant(I, DL, MVT::i32)),
        DL, MVT::i32);
    E.push_back(DCI.DAG.getAnyExtOrTrunc(
        DCI.DAG.getNode(ISD::SELECT, DL, MVT::i32, C, EA, EB), DL, MVT::i8));
  }
  return DCI.DAG.getNode(ISD::BUILD_VECTOR, DL, MVT::v4i8, E);
}

static SDValue PerformLOADCombine(SDNode *N,
                                  TargetLowering::DAGCombinerInfo &DCI) {
  SelectionDAG &DAG = DCI.DAG;
  LoadSDNode *LD = cast<LoadSDNode>(N);

  // Lower a v16i8 load into a LoadV4 operation with i32 results instead of
  // letting ReplaceLoadVector split it into smaller loads during legalization.
  // This is done at dag-combine1 time, so that vector operations with i8
  // elements can be optimised away instead of being needlessly split during
  // legalization, which involves storing to the stack and loading it back.
  EVT VT = N->getValueType(0);
  if (VT != MVT::v16i8)
    return SDValue();

  SDLoc DL(N);

  // Create a v4i32 vector load operation, effectively <4 x v4i8>.
  unsigned Opc = NVPTXISD::LoadV4;
  EVT NewVT = MVT::v4i32;
  EVT EltVT = NewVT.getVectorElementType();
  unsigned NumElts = NewVT.getVectorNumElements();
  EVT RetVTs[] = {EltVT, EltVT, EltVT, EltVT, MVT::Other};
  SDVTList RetVTList = DAG.getVTList(RetVTs);
  SmallVector<SDValue, 8> Ops(N->ops());
  Ops.push_back(DAG.getIntPtrConstant(LD->getExtensionType(), DL));
  SDValue NewLoad = DAG.getMemIntrinsicNode(Opc, DL, RetVTList, Ops, NewVT,
                                            LD->getMemOperand());
  SDValue NewChain = NewLoad.getValue(NumElts);

  // Create a vector of the same type returned by the original load.
  SmallVector<SDValue, 4> Elts;
  for (unsigned i = 0; i < NumElts; i++)
    Elts.push_back(NewLoad.getValue(i));
  return DCI.DAG.getMergeValues(
      {DCI.DAG.getBitcast(VT, DCI.DAG.getBuildVector(NewVT, DL, Elts)),
       NewChain},
      DL);
}

SDValue NVPTXTargetLowering::PerformDAGCombine(SDNode *N,
                                               DAGCombinerInfo &DCI) const {
  CodeGenOptLevel OptLevel = getTargetMachine().getOptLevel();
  switch (N->getOpcode()) {
    default: break;
    case ISD::ADD:
      return PerformADDCombine(N, DCI, OptLevel);
    case ISD::FADD:
      return PerformFADDCombine(N, DCI, OptLevel);
    case ISD::MUL:
      return PerformMULCombine(N, DCI, OptLevel);
    case ISD::SHL:
      return PerformSHLCombine(N, DCI, OptLevel);
    case ISD::AND:
      return PerformANDCombine(N, DCI);
    case ISD::UREM:
    case ISD::SREM:
      return PerformREMCombine(N, DCI, OptLevel);
    case ISD::SETCC:
      return PerformSETCCCombine(N, DCI, STI.getSmVersion());
    case ISD::LOAD:
      return PerformLOADCombine(N, DCI);
    case NVPTXISD::StoreRetval:
    case NVPTXISD::StoreRetvalV2:
    case NVPTXISD::StoreRetvalV4:
      return PerformStoreRetvalCombine(N);
    case NVPTXISD::StoreParam:
    case NVPTXISD::StoreParamV2:
    case NVPTXISD::StoreParamV4:
      return PerformStoreParamCombine(N);
    case ISD::EXTRACT_VECTOR_ELT:
      return PerformEXTRACTCombine(N, DCI);
    case ISD::VSELECT:
      return PerformVSELECTCombine(N, DCI);
  }
  return SDValue();
}

/// ReplaceVectorLoad - Convert vector loads into multi-output scalar loads.
static void ReplaceLoadVector(SDNode *N, SelectionDAG &DAG,
                              SmallVectorImpl<SDValue> &Results) {
  EVT ResVT = N->getValueType(0);
  SDLoc DL(N);

  assert(ResVT.isVector() && "Vector load must have vector type");

  // We only handle "native" vector sizes for now, e.g. <4 x double> is not
  // legal.  We can (and should) split that into 2 loads of <2 x double> here
  // but I'm leaving that as a TODO for now.
  assert(ResVT.isSimple() && "Can only handle simple types");
  switch (ResVT.getSimpleVT().SimpleTy) {
  default:
    return;
  case MVT::v2i8:
  case MVT::v2i16:
  case MVT::v2i32:
  case MVT::v2i64:
  case MVT::v2f16:
  case MVT::v2f32:
  case MVT::v2f64:
  case MVT::v4i8:
  case MVT::v4i16:
  case MVT::v4i32:
  case MVT::v4f16:
  case MVT::v4f32:
  case MVT::v8f16:  // <4 x f16x2>
  case MVT::v8bf16: // <4 x bf16x2>
  case MVT::v8i16:  // <4 x i16x2>
    // This is a "native" vector type
    break;
  }

  LoadSDNode *LD = cast<LoadSDNode>(N);

  Align Alignment = LD->getAlign();
  auto &TD = DAG.getDataLayout();
  Align PrefAlign =
      TD.getPrefTypeAlign(LD->getMemoryVT().getTypeForEVT(*DAG.getContext()));
  if (Alignment < PrefAlign) {
    // This load is not sufficiently aligned, so bail out and let this vector
    // load be scalarized.  Note that we may still be able to emit smaller
    // vector loads.  For example, if we are loading a <4 x float> with an
    // alignment of 8, this check will fail but the legalizer will try again
    // with 2 x <2 x float>, which will succeed with an alignment of 8.
    return;
  }

  EVT EltVT = ResVT.getVectorElementType();
  unsigned NumElts = ResVT.getVectorNumElements();

  // Since LoadV2 is a target node, we cannot rely on DAG type legalization.
  // Therefore, we must ensure the type is legal.  For i1 and i8, we set the
  // loaded type to i16 and propagate the "real" type as the memory type.
  bool NeedTrunc = false;
  if (EltVT.getSizeInBits() < 16) {
    EltVT = MVT::i16;
    NeedTrunc = true;
  }

  unsigned Opcode = 0;
  SDVTList LdResVTs;
  bool Load16x2 = false;

  switch (NumElts) {
  default:
    return;
  case 2:
    Opcode = NVPTXISD::LoadV2;
    LdResVTs = DAG.getVTList(EltVT, EltVT, MVT::Other);
    break;
  case 4: {
    Opcode = NVPTXISD::LoadV4;
    EVT ListVTs[] = { EltVT, EltVT, EltVT, EltVT, MVT::Other };
    LdResVTs = DAG.getVTList(ListVTs);
    break;
  }
  case 8: {
    // v8f16 is a special case. PTX doesn't have ld.v8.f16
    // instruction. Instead, we split the vector into v2f16 chunks and
    // load them with ld.v4.b32.
    assert(Is16bitsType(EltVT.getSimpleVT()) && "Unsupported v8 vector type.");
    Load16x2 = true;
    Opcode = NVPTXISD::LoadV4;
    EVT VVT;
    switch (EltVT.getSimpleVT().SimpleTy) {
    case MVT::f16:
      VVT = MVT::v2f16;
      break;
    case MVT::bf16:
      VVT = MVT::v2bf16;
      break;
    case MVT::i16:
      VVT = MVT::v2i16;
      break;
    default:
      llvm_unreachable("Unsupported v8 vector type.");
    }
    EVT ListVTs[] = {VVT, VVT, VVT, VVT, MVT::Other};
    LdResVTs = DAG.getVTList(ListVTs);
    break;
  }
  }

  // Copy regular operands
  SmallVector<SDValue, 8> OtherOps(N->op_begin(), N->op_end());

  // The select routine does not have access to the LoadSDNode instance, so
  // pass along the extension information
  OtherOps.push_back(DAG.getIntPtrConstant(LD->getExtensionType(), DL));

  SDValue NewLD = DAG.getMemIntrinsicNode(Opcode, DL, LdResVTs, OtherOps,
                                          LD->getMemoryVT(),
                                          LD->getMemOperand());

  SmallVector<SDValue, 8> ScalarRes;
  if (Load16x2) {
    // Split v2f16 subvectors back into individual elements.
    NumElts /= 2;
    for (unsigned i = 0; i < NumElts; ++i) {
      SDValue SubVector = NewLD.getValue(i);
      SDValue E0 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, EltVT, SubVector,
                               DAG.getIntPtrConstant(0, DL));
      SDValue E1 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, EltVT, SubVector,
                               DAG.getIntPtrConstant(1, DL));
      ScalarRes.push_back(E0);
      ScalarRes.push_back(E1);
    }
  } else {
    for (unsigned i = 0; i < NumElts; ++i) {
      SDValue Res = NewLD.getValue(i);
      if (NeedTrunc)
        Res = DAG.getNode(ISD::TRUNCATE, DL, ResVT.getVectorElementType(), Res);
      ScalarRes.push_back(Res);
    }
  }

  SDValue LoadChain = NewLD.getValue(NumElts);

  SDValue BuildVec = DAG.getBuildVector(ResVT, DL, ScalarRes);

  Results.push_back(BuildVec);
  Results.push_back(LoadChain);
}

static void ReplaceINTRINSIC_W_CHAIN(SDNode *N, SelectionDAG &DAG,
                                     SmallVectorImpl<SDValue> &Results) {
  SDValue Chain = N->getOperand(0);
  SDValue Intrin = N->getOperand(1);
  SDLoc DL(N);

  // Get the intrinsic ID
  unsigned IntrinNo = Intrin.getNode()->getAsZExtVal();
  switch (IntrinNo) {
  default:
    return;
  case Intrinsic::nvvm_ldg_global_i:
  case Intrinsic::nvvm_ldg_global_f:
  case Intrinsic::nvvm_ldg_global_p:
  case Intrinsic::nvvm_ldu_global_i:
  case Intrinsic::nvvm_ldu_global_f:
  case Intrinsic::nvvm_ldu_global_p: {
    EVT ResVT = N->getValueType(0);

    if (ResVT.isVector()) {
      // Vector LDG/LDU

      unsigned NumElts = ResVT.getVectorNumElements();
      EVT EltVT = ResVT.getVectorElementType();

      // Since LDU/LDG are target nodes, we cannot rely on DAG type
      // legalization.
      // Therefore, we must ensure the type is legal.  For i1 and i8, we set the
      // loaded type to i16 and propagate the "real" type as the memory type.
      bool NeedTrunc = false;
      if (EltVT.getSizeInBits() < 16) {
        EltVT = MVT::i16;
        NeedTrunc = true;
      }

      unsigned Opcode = 0;
      SDVTList LdResVTs;

      switch (NumElts) {
      default:
        return;
      case 2:
        switch (IntrinNo) {
        default:
          return;
        case Intrinsic::nvvm_ldg_global_i:
        case Intrinsic::nvvm_ldg_global_f:
        case Intrinsic::nvvm_ldg_global_p:
          Opcode = NVPTXISD::LDGV2;
          break;
        case Intrinsic::nvvm_ldu_global_i:
        case Intrinsic::nvvm_ldu_global_f:
        case Intrinsic::nvvm_ldu_global_p:
          Opcode = NVPTXISD::LDUV2;
          break;
        }
        LdResVTs = DAG.getVTList(EltVT, EltVT, MVT::Other);
        break;
      case 4: {
        switch (IntrinNo) {
        default:
          return;
        case Intrinsic::nvvm_ldg_global_i:
        case Intrinsic::nvvm_ldg_global_f:
        case Intrinsic::nvvm_ldg_global_p:
          Opcode = NVPTXISD::LDGV4;
          break;
        case Intrinsic::nvvm_ldu_global_i:
        case Intrinsic::nvvm_ldu_global_f:
        case Intrinsic::nvvm_ldu_global_p:
          Opcode = NVPTXISD::LDUV4;
          break;
        }
        EVT ListVTs[] = { EltVT, EltVT, EltVT, EltVT, MVT::Other };
        LdResVTs = DAG.getVTList(ListVTs);
        break;
      }
      }

      SmallVector<SDValue, 8> OtherOps;

      // Copy regular operands

      OtherOps.push_back(Chain); // Chain
                                 // Skip operand 1 (intrinsic ID)
      // Others
      OtherOps.append(N->op_begin() + 2, N->op_end());

      MemIntrinsicSDNode *MemSD = cast<MemIntrinsicSDNode>(N);

      SDValue NewLD = DAG.getMemIntrinsicNode(Opcode, DL, LdResVTs, OtherOps,
                                              MemSD->getMemoryVT(),
                                              MemSD->getMemOperand());

      SmallVector<SDValue, 4> ScalarRes;

      for (unsigned i = 0; i < NumElts; ++i) {
        SDValue Res = NewLD.getValue(i);
        if (NeedTrunc)
          Res =
              DAG.getNode(ISD::TRUNCATE, DL, ResVT.getVectorElementType(), Res);
        ScalarRes.push_back(Res);
      }

      SDValue LoadChain = NewLD.getValue(NumElts);

      SDValue BuildVec =
          DAG.getBuildVector(ResVT, DL, ScalarRes);

      Results.push_back(BuildVec);
      Results.push_back(LoadChain);
    } else {
      // i8 LDG/LDU
      assert(ResVT.isSimple() && ResVT.getSimpleVT().SimpleTy == MVT::i8 &&
             "Custom handling of non-i8 ldu/ldg?");

      // Just copy all operands as-is
      SmallVector<SDValue, 4> Ops(N->op_begin(), N->op_end());

      // Force output to i16
      SDVTList LdResVTs = DAG.getVTList(MVT::i16, MVT::Other);

      MemIntrinsicSDNode *MemSD = cast<MemIntrinsicSDNode>(N);

      // We make sure the memory type is i8, which will be used during isel
      // to select the proper instruction.
      SDValue NewLD =
          DAG.getMemIntrinsicNode(ISD::INTRINSIC_W_CHAIN, DL, LdResVTs, Ops,
                                  MVT::i8, MemSD->getMemOperand());

      Results.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i8,
                                    NewLD.getValue(0)));
      Results.push_back(NewLD.getValue(1));
    }
  }
  }
}

static void ReplaceCopyFromReg_128(SDNode *N, SelectionDAG &DAG,
                                   SmallVectorImpl<SDValue> &Results) {
  // Change the CopyFromReg to output 2 64-bit results instead of a 128-bit
  // result so that it can pass the legalization
  SDLoc DL(N);
  SDValue Chain = N->getOperand(0);
  SDValue Reg = N->getOperand(1);
  SDValue Glue = N->getOperand(2);

  assert(Reg.getValueType() == MVT::i128 &&
         "Custom lowering for CopyFromReg with 128-bit reg only");
  SmallVector<EVT, 4> ResultsType = {MVT::i64, MVT::i64, N->getValueType(1),
                                     N->getValueType(2)};
  SmallVector<SDValue, 3> NewOps = {Chain, Reg, Glue};

  SDValue NewValue = DAG.getNode(ISD::CopyFromReg, DL, ResultsType, NewOps);
  SDValue Pair = DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i128,
                             {NewValue.getValue(0), NewValue.getValue(1)});

  Results.push_back(Pair);
  Results.push_back(NewValue.getValue(2));
  Results.push_back(NewValue.getValue(3));
}

void NVPTXTargetLowering::ReplaceNodeResults(
    SDNode *N, SmallVectorImpl<SDValue> &Results, SelectionDAG &DAG) const {
  switch (N->getOpcode()) {
  default:
    report_fatal_error("Unhandled custom legalization");
  case ISD::LOAD:
    ReplaceLoadVector(N, DAG, Results);
    return;
  case ISD::INTRINSIC_W_CHAIN:
    ReplaceINTRINSIC_W_CHAIN(N, DAG, Results);
    return;
  case ISD::CopyFromReg:
    ReplaceCopyFromReg_128(N, DAG, Results);
    return;
  }
}

NVPTXTargetLowering::AtomicExpansionKind
NVPTXTargetLowering::shouldExpandAtomicRMWInIR(AtomicRMWInst *AI) const {
  Type *Ty = AI->getValOperand()->getType();

  if (AI->isFloatingPointOperation()) {
    if (AI->getOperation() == AtomicRMWInst::BinOp::FAdd) {
      if (Ty->isHalfTy() && STI.getSmVersion() >= 70 &&
          STI.getPTXVersion() >= 63)
        return AtomicExpansionKind::None;
      if (Ty->isBFloatTy() && STI.getSmVersion() >= 90 &&
          STI.getPTXVersion() >= 78)
        return AtomicExpansionKind::None;
      if (Ty->isFloatTy())
        return AtomicExpansionKind::None;
      if (Ty->isDoubleTy() && STI.hasAtomAddF64())
        return AtomicExpansionKind::None;
    }
    return AtomicExpansionKind::CmpXChg;
  }

  assert(Ty->isIntegerTy() && "Ty should be integer at this point");
  auto ITy = cast<llvm::IntegerType>(Ty);

  switch (AI->getOperation()) {
  default:
    return AtomicExpansionKind::CmpXChg;
  case AtomicRMWInst::BinOp::And:
  case AtomicRMWInst::BinOp::Or:
  case AtomicRMWInst::BinOp::Xor:
  case AtomicRMWInst::BinOp::Xchg:
    switch (ITy->getBitWidth()) {
    case 8:
    case 16:
      return AtomicExpansionKind::CmpXChg;
    case 32:
      return AtomicExpansionKind::None;
    case 64:
      if (STI.hasAtomBitwise64())
        return AtomicExpansionKind::None;
      return AtomicExpansionKind::CmpXChg;
    default:
      llvm_unreachable("unsupported width encountered");
    }
  case AtomicRMWInst::BinOp::Add:
  case AtomicRMWInst::BinOp::Sub:
  case AtomicRMWInst::BinOp::Max:
  case AtomicRMWInst::BinOp::Min:
  case AtomicRMWInst::BinOp::UMax:
  case AtomicRMWInst::BinOp::UMin:
    switch (ITy->getBitWidth()) {
    case 8:
    case 16:
      return AtomicExpansionKind::CmpXChg;
    case 32:
      return AtomicExpansionKind::None;
    case 64:
      if (STI.hasAtomMinMax64())
        return AtomicExpansionKind::None;
      return AtomicExpansionKind::CmpXChg;
    default:
      llvm_unreachable("unsupported width encountered");
    }
  }

  return AtomicExpansionKind::CmpXChg;
}

// Pin NVPTXTargetObjectFile's vtables to this file.
NVPTXTargetObjectFile::~NVPTXTargetObjectFile() = default;

MCSection *NVPTXTargetObjectFile::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  return getDataSection();
}
