//===- ARMISelLowering.cpp - ARM DAG Lowering Implementation --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the interfaces that ARM uses to lower LLVM code into a
// selection DAG.
//
//===----------------------------------------------------------------------===//

#include "ARMISelLowering.h"
#include "ARMBaseInstrInfo.h"
#include "ARMBaseRegisterInfo.h"
#include "ARMCallingConv.h"
#include "ARMConstantPoolValue.h"
#include "ARMMachineFunctionInfo.h"
#include "ARMPerfectShuffle.h"
#include "ARMRegisterInfo.h"
#include "ARMSelectionDAGInfo.h"
#include "ARMSubtarget.h"
#include "ARMTargetTransformInfo.h"
#include "MCTargetDesc/ARMAddressingModes.h"
#include "MCTargetDesc/ARMBaseInfo.h"
#include "Utils/ARMBaseInfo.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/VectorUtils.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/ComplexDeinterleavingPass.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/IntrinsicLowering.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineJumpTableInfo.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RuntimeLibcallUtil.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/SelectionDAGAddressAnalysis.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsARM.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/User.h"
#include "llvm/IR/Value.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;
using namespace llvm::PatternMatch;

#define DEBUG_TYPE "arm-isel"

STATISTIC(NumTailCalls, "Number of tail calls");
STATISTIC(NumMovwMovt, "Number of GAs materialized with movw + movt");
STATISTIC(NumLoopByVals, "Number of loops generated for byval arguments");
STATISTIC(NumConstpoolPromoted,
  "Number of constants with their storage promoted into constant pools");

static cl::opt<bool>
ARMInterworking("arm-interworking", cl::Hidden,
  cl::desc("Enable / disable ARM interworking (for debugging only)"),
  cl::init(true));

static cl::opt<bool> EnableConstpoolPromotion(
    "arm-promote-constant", cl::Hidden,
    cl::desc("Enable / disable promotion of unnamed_addr constants into "
             "constant pools"),
    cl::init(false)); // FIXME: set to true by default once PR32780 is fixed
static cl::opt<unsigned> ConstpoolPromotionMaxSize(
    "arm-promote-constant-max-size", cl::Hidden,
    cl::desc("Maximum size of constant to promote into a constant pool"),
    cl::init(64));
static cl::opt<unsigned> ConstpoolPromotionMaxTotal(
    "arm-promote-constant-max-total", cl::Hidden,
    cl::desc("Maximum size of ALL constants to promote into a constant pool"),
    cl::init(128));

cl::opt<unsigned>
MVEMaxSupportedInterleaveFactor("mve-max-interleave-factor", cl::Hidden,
  cl::desc("Maximum interleave factor for MVE VLDn to generate."),
  cl::init(2));

// The APCS parameter registers.
static const MCPhysReg GPRArgRegs[] = {
  ARM::R0, ARM::R1, ARM::R2, ARM::R3
};

static SDValue handleCMSEValue(const SDValue &Value, const ISD::InputArg &Arg,
                               SelectionDAG &DAG, const SDLoc &DL) {
  assert(Arg.ArgVT.isScalarInteger());
  assert(Arg.ArgVT.bitsLT(MVT::i32));
  SDValue Trunc = DAG.getNode(ISD::TRUNCATE, DL, Arg.ArgVT, Value);
  SDValue Ext =
      DAG.getNode(Arg.Flags.isSExt() ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND, DL,
                  MVT::i32, Trunc);
  return Ext;
}

void ARMTargetLowering::addTypeForNEON(MVT VT, MVT PromotedLdStVT) {
  if (VT != PromotedLdStVT) {
    setOperationAction(ISD::LOAD, VT, Promote);
    AddPromotedToType (ISD::LOAD, VT, PromotedLdStVT);

    setOperationAction(ISD::STORE, VT, Promote);
    AddPromotedToType (ISD::STORE, VT, PromotedLdStVT);
  }

  MVT ElemTy = VT.getVectorElementType();
  if (ElemTy != MVT::f64)
    setOperationAction(ISD::SETCC, VT, Custom);
  setOperationAction(ISD::INSERT_VECTOR_ELT, VT, Custom);
  setOperationAction(ISD::EXTRACT_VECTOR_ELT, VT, Custom);
  if (ElemTy == MVT::i32) {
    setOperationAction(ISD::SINT_TO_FP, VT, Custom);
    setOperationAction(ISD::UINT_TO_FP, VT, Custom);
    setOperationAction(ISD::FP_TO_SINT, VT, Custom);
    setOperationAction(ISD::FP_TO_UINT, VT, Custom);
  } else {
    setOperationAction(ISD::SINT_TO_FP, VT, Expand);
    setOperationAction(ISD::UINT_TO_FP, VT, Expand);
    setOperationAction(ISD::FP_TO_SINT, VT, Expand);
    setOperationAction(ISD::FP_TO_UINT, VT, Expand);
  }
  setOperationAction(ISD::BUILD_VECTOR,      VT, Custom);
  setOperationAction(ISD::VECTOR_SHUFFLE,    VT, Custom);
  setOperationAction(ISD::CONCAT_VECTORS,    VT, Legal);
  setOperationAction(ISD::EXTRACT_SUBVECTOR, VT, Legal);
  setOperationAction(ISD::SELECT,            VT, Expand);
  setOperationAction(ISD::SELECT_CC,         VT, Expand);
  setOperationAction(ISD::VSELECT,           VT, Expand);
  setOperationAction(ISD::SIGN_EXTEND_INREG, VT, Expand);
  if (VT.isInteger()) {
    setOperationAction(ISD::SHL, VT, Custom);
    setOperationAction(ISD::SRA, VT, Custom);
    setOperationAction(ISD::SRL, VT, Custom);
  }

  // Neon does not support vector divide/remainder operations.
  setOperationAction(ISD::SDIV, VT, Expand);
  setOperationAction(ISD::UDIV, VT, Expand);
  setOperationAction(ISD::FDIV, VT, Expand);
  setOperationAction(ISD::SREM, VT, Expand);
  setOperationAction(ISD::UREM, VT, Expand);
  setOperationAction(ISD::FREM, VT, Expand);
  setOperationAction(ISD::SDIVREM, VT, Expand);
  setOperationAction(ISD::UDIVREM, VT, Expand);

  if (!VT.isFloatingPoint() && VT != MVT::v2i64 && VT != MVT::v1i64)
    for (auto Opcode : {ISD::ABS, ISD::ABDS, ISD::ABDU, ISD::SMIN, ISD::SMAX,
                        ISD::UMIN, ISD::UMAX})
      setOperationAction(Opcode, VT, Legal);
  if (!VT.isFloatingPoint())
    for (auto Opcode : {ISD::SADDSAT, ISD::UADDSAT, ISD::SSUBSAT, ISD::USUBSAT})
      setOperationAction(Opcode, VT, Legal);
}

void ARMTargetLowering::addDRTypeForNEON(MVT VT) {
  addRegisterClass(VT, &ARM::DPRRegClass);
  addTypeForNEON(VT, MVT::f64);
}

void ARMTargetLowering::addQRTypeForNEON(MVT VT) {
  addRegisterClass(VT, &ARM::DPairRegClass);
  addTypeForNEON(VT, MVT::v2f64);
}

void ARMTargetLowering::setAllExpand(MVT VT) {
  for (unsigned Opc = 0; Opc < ISD::BUILTIN_OP_END; ++Opc)
    setOperationAction(Opc, VT, Expand);

  // We support these really simple operations even on types where all
  // the actual arithmetic has to be broken down into simpler
  // operations or turned into library calls.
  setOperationAction(ISD::BITCAST, VT, Legal);
  setOperationAction(ISD::LOAD, VT, Legal);
  setOperationAction(ISD::STORE, VT, Legal);
  setOperationAction(ISD::UNDEF, VT, Legal);
}

void ARMTargetLowering::addAllExtLoads(const MVT From, const MVT To,
                                       LegalizeAction Action) {
  setLoadExtAction(ISD::EXTLOAD,  From, To, Action);
  setLoadExtAction(ISD::ZEXTLOAD, From, To, Action);
  setLoadExtAction(ISD::SEXTLOAD, From, To, Action);
}

void ARMTargetLowering::addMVEVectorTypes(bool HasMVEFP) {
  const MVT IntTypes[] = { MVT::v16i8, MVT::v8i16, MVT::v4i32 };

  for (auto VT : IntTypes) {
    addRegisterClass(VT, &ARM::MQPRRegClass);
    setOperationAction(ISD::VECTOR_SHUFFLE, VT, Custom);
    setOperationAction(ISD::INSERT_VECTOR_ELT, VT, Custom);
    setOperationAction(ISD::EXTRACT_VECTOR_ELT, VT, Custom);
    setOperationAction(ISD::BUILD_VECTOR, VT, Custom);
    setOperationAction(ISD::SHL, VT, Custom);
    setOperationAction(ISD::SRA, VT, Custom);
    setOperationAction(ISD::SRL, VT, Custom);
    setOperationAction(ISD::SMIN, VT, Legal);
    setOperationAction(ISD::SMAX, VT, Legal);
    setOperationAction(ISD::UMIN, VT, Legal);
    setOperationAction(ISD::UMAX, VT, Legal);
    setOperationAction(ISD::ABS, VT, Legal);
    setOperationAction(ISD::SETCC, VT, Custom);
    setOperationAction(ISD::MLOAD, VT, Custom);
    setOperationAction(ISD::MSTORE, VT, Legal);
    setOperationAction(ISD::CTLZ, VT, Legal);
    setOperationAction(ISD::CTTZ, VT, Custom);
    setOperationAction(ISD::BITREVERSE, VT, Legal);
    setOperationAction(ISD::BSWAP, VT, Legal);
    setOperationAction(ISD::SADDSAT, VT, Legal);
    setOperationAction(ISD::UADDSAT, VT, Legal);
    setOperationAction(ISD::SSUBSAT, VT, Legal);
    setOperationAction(ISD::USUBSAT, VT, Legal);
    setOperationAction(ISD::ABDS, VT, Legal);
    setOperationAction(ISD::ABDU, VT, Legal);
    setOperationAction(ISD::AVGFLOORS, VT, Legal);
    setOperationAction(ISD::AVGFLOORU, VT, Legal);
    setOperationAction(ISD::AVGCEILS, VT, Legal);
    setOperationAction(ISD::AVGCEILU, VT, Legal);

    // No native support for these.
    setOperationAction(ISD::UDIV, VT, Expand);
    setOperationAction(ISD::SDIV, VT, Expand);
    setOperationAction(ISD::UREM, VT, Expand);
    setOperationAction(ISD::SREM, VT, Expand);
    setOperationAction(ISD::UDIVREM, VT, Expand);
    setOperationAction(ISD::SDIVREM, VT, Expand);
    setOperationAction(ISD::CTPOP, VT, Expand);
    setOperationAction(ISD::SELECT, VT, Expand);
    setOperationAction(ISD::SELECT_CC, VT, Expand);

    // Vector reductions
    setOperationAction(ISD::VECREDUCE_ADD, VT, Legal);
    setOperationAction(ISD::VECREDUCE_SMAX, VT, Legal);
    setOperationAction(ISD::VECREDUCE_UMAX, VT, Legal);
    setOperationAction(ISD::VECREDUCE_SMIN, VT, Legal);
    setOperationAction(ISD::VECREDUCE_UMIN, VT, Legal);
    setOperationAction(ISD::VECREDUCE_MUL, VT, Custom);
    setOperationAction(ISD::VECREDUCE_AND, VT, Custom);
    setOperationAction(ISD::VECREDUCE_OR, VT, Custom);
    setOperationAction(ISD::VECREDUCE_XOR, VT, Custom);

    if (!HasMVEFP) {
      setOperationAction(ISD::SINT_TO_FP, VT, Expand);
      setOperationAction(ISD::UINT_TO_FP, VT, Expand);
      setOperationAction(ISD::FP_TO_SINT, VT, Expand);
      setOperationAction(ISD::FP_TO_UINT, VT, Expand);
    } else {
      setOperationAction(ISD::FP_TO_SINT_SAT, VT, Custom);
      setOperationAction(ISD::FP_TO_UINT_SAT, VT, Custom);
    }

    // Pre and Post inc are supported on loads and stores
    for (unsigned im = (unsigned)ISD::PRE_INC;
         im != (unsigned)ISD::LAST_INDEXED_MODE; ++im) {
      setIndexedLoadAction(im, VT, Legal);
      setIndexedStoreAction(im, VT, Legal);
      setIndexedMaskedLoadAction(im, VT, Legal);
      setIndexedMaskedStoreAction(im, VT, Legal);
    }
  }

  const MVT FloatTypes[] = { MVT::v8f16, MVT::v4f32 };
  for (auto VT : FloatTypes) {
    addRegisterClass(VT, &ARM::MQPRRegClass);
    if (!HasMVEFP)
      setAllExpand(VT);

    // These are legal or custom whether we have MVE.fp or not
    setOperationAction(ISD::VECTOR_SHUFFLE, VT, Custom);
    setOperationAction(ISD::INSERT_VECTOR_ELT, VT, Custom);
    setOperationAction(ISD::INSERT_VECTOR_ELT, VT.getVectorElementType(), Custom);
    setOperationAction(ISD::EXTRACT_VECTOR_ELT, VT, Custom);
    setOperationAction(ISD::BUILD_VECTOR, VT, Custom);
    setOperationAction(ISD::BUILD_VECTOR, VT.getVectorElementType(), Custom);
    setOperationAction(ISD::SCALAR_TO_VECTOR, VT, Legal);
    setOperationAction(ISD::SETCC, VT, Custom);
    setOperationAction(ISD::MLOAD, VT, Custom);
    setOperationAction(ISD::MSTORE, VT, Legal);
    setOperationAction(ISD::SELECT, VT, Expand);
    setOperationAction(ISD::SELECT_CC, VT, Expand);

    // Pre and Post inc are supported on loads and stores
    for (unsigned im = (unsigned)ISD::PRE_INC;
         im != (unsigned)ISD::LAST_INDEXED_MODE; ++im) {
      setIndexedLoadAction(im, VT, Legal);
      setIndexedStoreAction(im, VT, Legal);
      setIndexedMaskedLoadAction(im, VT, Legal);
      setIndexedMaskedStoreAction(im, VT, Legal);
    }

    if (HasMVEFP) {
      setOperationAction(ISD::FMINNUM, VT, Legal);
      setOperationAction(ISD::FMAXNUM, VT, Legal);
      setOperationAction(ISD::FROUND, VT, Legal);
      setOperationAction(ISD::VECREDUCE_FADD, VT, Custom);
      setOperationAction(ISD::VECREDUCE_FMUL, VT, Custom);
      setOperationAction(ISD::VECREDUCE_FMIN, VT, Custom);
      setOperationAction(ISD::VECREDUCE_FMAX, VT, Custom);

      // No native support for these.
      setOperationAction(ISD::FDIV, VT, Expand);
      setOperationAction(ISD::FREM, VT, Expand);
      setOperationAction(ISD::FSQRT, VT, Expand);
      setOperationAction(ISD::FSIN, VT, Expand);
      setOperationAction(ISD::FCOS, VT, Expand);
      setOperationAction(ISD::FTAN, VT, Expand);
      setOperationAction(ISD::FPOW, VT, Expand);
      setOperationAction(ISD::FLOG, VT, Expand);
      setOperationAction(ISD::FLOG2, VT, Expand);
      setOperationAction(ISD::FLOG10, VT, Expand);
      setOperationAction(ISD::FEXP, VT, Expand);
      setOperationAction(ISD::FEXP2, VT, Expand);
      setOperationAction(ISD::FEXP10, VT, Expand);
      setOperationAction(ISD::FNEARBYINT, VT, Expand);
    }
  }

  // Custom Expand smaller than legal vector reductions to prevent false zero
  // items being added.
  setOperationAction(ISD::VECREDUCE_FADD, MVT::v4f16, Custom);
  setOperationAction(ISD::VECREDUCE_FMUL, MVT::v4f16, Custom);
  setOperationAction(ISD::VECREDUCE_FMIN, MVT::v4f16, Custom);
  setOperationAction(ISD::VECREDUCE_FMAX, MVT::v4f16, Custom);
  setOperationAction(ISD::VECREDUCE_FADD, MVT::v2f16, Custom);
  setOperationAction(ISD::VECREDUCE_FMUL, MVT::v2f16, Custom);
  setOperationAction(ISD::VECREDUCE_FMIN, MVT::v2f16, Custom);
  setOperationAction(ISD::VECREDUCE_FMAX, MVT::v2f16, Custom);

  // We 'support' these types up to bitcast/load/store level, regardless of
  // MVE integer-only / float support. Only doing FP data processing on the FP
  // vector types is inhibited at integer-only level.
  const MVT LongTypes[] = { MVT::v2i64, MVT::v2f64 };
  for (auto VT : LongTypes) {
    addRegisterClass(VT, &ARM::MQPRRegClass);
    setAllExpand(VT);
    setOperationAction(ISD::INSERT_VECTOR_ELT, VT, Custom);
    setOperationAction(ISD::EXTRACT_VECTOR_ELT, VT, Custom);
    setOperationAction(ISD::BUILD_VECTOR, VT, Custom);
    setOperationAction(ISD::VSELECT, VT, Legal);
    setOperationAction(ISD::VECTOR_SHUFFLE, VT, Custom);
  }
  setOperationAction(ISD::SCALAR_TO_VECTOR, MVT::v2f64, Legal);

  // We can do bitwise operations on v2i64 vectors
  setOperationAction(ISD::AND, MVT::v2i64, Legal);
  setOperationAction(ISD::OR, MVT::v2i64, Legal);
  setOperationAction(ISD::XOR, MVT::v2i64, Legal);

  // It is legal to extload from v4i8 to v4i16 or v4i32.
  addAllExtLoads(MVT::v8i16, MVT::v8i8, Legal);
  addAllExtLoads(MVT::v4i32, MVT::v4i16, Legal);
  addAllExtLoads(MVT::v4i32, MVT::v4i8, Legal);

  // It is legal to sign extend from v4i8/v4i16 to v4i32 or v8i8 to v8i16.
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::v4i8,  Legal);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::v4i16, Legal);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::v4i32, Legal);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::v8i8,  Legal);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::v8i16, Legal);

  // Some truncating stores are legal too.
  setTruncStoreAction(MVT::v4i32, MVT::v4i16, Legal);
  setTruncStoreAction(MVT::v4i32, MVT::v4i8,  Legal);
  setTruncStoreAction(MVT::v8i16, MVT::v8i8,  Legal);

  // Pre and Post inc on these are legal, given the correct extends
  for (unsigned im = (unsigned)ISD::PRE_INC;
       im != (unsigned)ISD::LAST_INDEXED_MODE; ++im) {
    for (auto VT : {MVT::v8i8, MVT::v4i8, MVT::v4i16}) {
      setIndexedLoadAction(im, VT, Legal);
      setIndexedStoreAction(im, VT, Legal);
      setIndexedMaskedLoadAction(im, VT, Legal);
      setIndexedMaskedStoreAction(im, VT, Legal);
    }
  }

  // Predicate types
  const MVT pTypes[] = {MVT::v16i1, MVT::v8i1, MVT::v4i1, MVT::v2i1};
  for (auto VT : pTypes) {
    addRegisterClass(VT, &ARM::VCCRRegClass);
    setOperationAction(ISD::BUILD_VECTOR, VT, Custom);
    setOperationAction(ISD::VECTOR_SHUFFLE, VT, Custom);
    setOperationAction(ISD::EXTRACT_SUBVECTOR, VT, Custom);
    setOperationAction(ISD::CONCAT_VECTORS, VT, Custom);
    setOperationAction(ISD::INSERT_VECTOR_ELT, VT, Custom);
    setOperationAction(ISD::EXTRACT_VECTOR_ELT, VT, Custom);
    setOperationAction(ISD::SETCC, VT, Custom);
    setOperationAction(ISD::SCALAR_TO_VECTOR, VT, Expand);
    setOperationAction(ISD::LOAD, VT, Custom);
    setOperationAction(ISD::STORE, VT, Custom);
    setOperationAction(ISD::TRUNCATE, VT, Custom);
    setOperationAction(ISD::VSELECT, VT, Expand);
    setOperationAction(ISD::SELECT, VT, Expand);
    setOperationAction(ISD::SELECT_CC, VT, Expand);

    if (!HasMVEFP) {
      setOperationAction(ISD::SINT_TO_FP, VT, Expand);
      setOperationAction(ISD::UINT_TO_FP, VT, Expand);
      setOperationAction(ISD::FP_TO_SINT, VT, Expand);
      setOperationAction(ISD::FP_TO_UINT, VT, Expand);
    }
  }
  setOperationAction(ISD::SETCC, MVT::v2i1, Expand);
  setOperationAction(ISD::TRUNCATE, MVT::v2i1, Expand);
  setOperationAction(ISD::AND, MVT::v2i1, Expand);
  setOperationAction(ISD::OR, MVT::v2i1, Expand);
  setOperationAction(ISD::XOR, MVT::v2i1, Expand);
  setOperationAction(ISD::SINT_TO_FP, MVT::v2i1, Expand);
  setOperationAction(ISD::UINT_TO_FP, MVT::v2i1, Expand);
  setOperationAction(ISD::FP_TO_SINT, MVT::v2i1, Expand);
  setOperationAction(ISD::FP_TO_UINT, MVT::v2i1, Expand);

  setOperationAction(ISD::SIGN_EXTEND, MVT::v8i32, Custom);
  setOperationAction(ISD::SIGN_EXTEND, MVT::v16i16, Custom);
  setOperationAction(ISD::SIGN_EXTEND, MVT::v16i32, Custom);
  setOperationAction(ISD::ZERO_EXTEND, MVT::v8i32, Custom);
  setOperationAction(ISD::ZERO_EXTEND, MVT::v16i16, Custom);
  setOperationAction(ISD::ZERO_EXTEND, MVT::v16i32, Custom);
  setOperationAction(ISD::TRUNCATE, MVT::v8i32, Custom);
  setOperationAction(ISD::TRUNCATE, MVT::v16i16, Custom);
}

ARMTargetLowering::ARMTargetLowering(const TargetMachine &TM,
                                     const ARMSubtarget &STI)
    : TargetLowering(TM), Subtarget(&STI) {
  RegInfo = Subtarget->getRegisterInfo();
  Itins = Subtarget->getInstrItineraryData();

  setBooleanContents(ZeroOrOneBooleanContent);
  setBooleanVectorContents(ZeroOrNegativeOneBooleanContent);

  if (!Subtarget->isTargetDarwin() && !Subtarget->isTargetIOS() &&
      !Subtarget->isTargetWatchOS() && !Subtarget->isTargetDriverKit()) {
    bool IsHFTarget = TM.Options.FloatABIType == FloatABI::Hard;
    for (int LCID = 0; LCID < RTLIB::UNKNOWN_LIBCALL; ++LCID)
      setLibcallCallingConv(static_cast<RTLIB::Libcall>(LCID),
                            IsHFTarget ? CallingConv::ARM_AAPCS_VFP
                                       : CallingConv::ARM_AAPCS);
  }

  if (Subtarget->isTargetMachO()) {
    // Uses VFP for Thumb libfuncs if available.
    if (Subtarget->isThumb() && Subtarget->hasVFP2Base() &&
        Subtarget->hasARMOps() && !Subtarget->useSoftFloat()) {
      static const struct {
        const RTLIB::Libcall Op;
        const char * const Name;
        const ISD::CondCode Cond;
      } LibraryCalls[] = {
        // Single-precision floating-point arithmetic.
        { RTLIB::ADD_F32, "__addsf3vfp", ISD::SETCC_INVALID },
        { RTLIB::SUB_F32, "__subsf3vfp", ISD::SETCC_INVALID },
        { RTLIB::MUL_F32, "__mulsf3vfp", ISD::SETCC_INVALID },
        { RTLIB::DIV_F32, "__divsf3vfp", ISD::SETCC_INVALID },

        // Double-precision floating-point arithmetic.
        { RTLIB::ADD_F64, "__adddf3vfp", ISD::SETCC_INVALID },
        { RTLIB::SUB_F64, "__subdf3vfp", ISD::SETCC_INVALID },
        { RTLIB::MUL_F64, "__muldf3vfp", ISD::SETCC_INVALID },
        { RTLIB::DIV_F64, "__divdf3vfp", ISD::SETCC_INVALID },

        // Single-precision comparisons.
        { RTLIB::OEQ_F32, "__eqsf2vfp",    ISD::SETNE },
        { RTLIB::UNE_F32, "__nesf2vfp",    ISD::SETNE },
        { RTLIB::OLT_F32, "__ltsf2vfp",    ISD::SETNE },
        { RTLIB::OLE_F32, "__lesf2vfp",    ISD::SETNE },
        { RTLIB::OGE_F32, "__gesf2vfp",    ISD::SETNE },
        { RTLIB::OGT_F32, "__gtsf2vfp",    ISD::SETNE },
        { RTLIB::UO_F32,  "__unordsf2vfp", ISD::SETNE },

        // Double-precision comparisons.
        { RTLIB::OEQ_F64, "__eqdf2vfp",    ISD::SETNE },
        { RTLIB::UNE_F64, "__nedf2vfp",    ISD::SETNE },
        { RTLIB::OLT_F64, "__ltdf2vfp",    ISD::SETNE },
        { RTLIB::OLE_F64, "__ledf2vfp",    ISD::SETNE },
        { RTLIB::OGE_F64, "__gedf2vfp",    ISD::SETNE },
        { RTLIB::OGT_F64, "__gtdf2vfp",    ISD::SETNE },
        { RTLIB::UO_F64,  "__unorddf2vfp", ISD::SETNE },

        // Floating-point to integer conversions.
        // i64 conversions are done via library routines even when generating VFP
        // instructions, so use the same ones.
        { RTLIB::FPTOSINT_F64_I32, "__fixdfsivfp",    ISD::SETCC_INVALID },
        { RTLIB::FPTOUINT_F64_I32, "__fixunsdfsivfp", ISD::SETCC_INVALID },
        { RTLIB::FPTOSINT_F32_I32, "__fixsfsivfp",    ISD::SETCC_INVALID },
        { RTLIB::FPTOUINT_F32_I32, "__fixunssfsivfp", ISD::SETCC_INVALID },

        // Conversions between floating types.
        { RTLIB::FPROUND_F64_F32, "__truncdfsf2vfp",  ISD::SETCC_INVALID },
        { RTLIB::FPEXT_F32_F64,   "__extendsfdf2vfp", ISD::SETCC_INVALID },

        // Integer to floating-point conversions.
        // i64 conversions are done via library routines even when generating VFP
        // instructions, so use the same ones.
        // FIXME: There appears to be some naming inconsistency in ARM libgcc:
        // e.g., __floatunsidf vs. __floatunssidfvfp.
        { RTLIB::SINTTOFP_I32_F64, "__floatsidfvfp",    ISD::SETCC_INVALID },
        { RTLIB::UINTTOFP_I32_F64, "__floatunssidfvfp", ISD::SETCC_INVALID },
        { RTLIB::SINTTOFP_I32_F32, "__floatsisfvfp",    ISD::SETCC_INVALID },
        { RTLIB::UINTTOFP_I32_F32, "__floatunssisfvfp", ISD::SETCC_INVALID },
      };

      for (const auto &LC : LibraryCalls) {
        setLibcallName(LC.Op, LC.Name);
        if (LC.Cond != ISD::SETCC_INVALID)
          setCmpLibcallCC(LC.Op, LC.Cond);
      }
    }
  }

  // RTLIB
  if (Subtarget->isAAPCS_ABI() &&
      (Subtarget->isTargetAEABI() || Subtarget->isTargetGNUAEABI() ||
       Subtarget->isTargetMuslAEABI() || Subtarget->isTargetAndroid())) {
    static const struct {
      const RTLIB::Libcall Op;
      const char * const Name;
      const CallingConv::ID CC;
      const ISD::CondCode Cond;
    } LibraryCalls[] = {
      // Double-precision floating-point arithmetic helper functions
      // RTABI chapter 4.1.2, Table 2
      { RTLIB::ADD_F64, "__aeabi_dadd", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::DIV_F64, "__aeabi_ddiv", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::MUL_F64, "__aeabi_dmul", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SUB_F64, "__aeabi_dsub", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },

      // Double-precision floating-point comparison helper functions
      // RTABI chapter 4.1.2, Table 3
      { RTLIB::OEQ_F64, "__aeabi_dcmpeq", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::UNE_F64, "__aeabi_dcmpeq", CallingConv::ARM_AAPCS, ISD::SETEQ },
      { RTLIB::OLT_F64, "__aeabi_dcmplt", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::OLE_F64, "__aeabi_dcmple", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::OGE_F64, "__aeabi_dcmpge", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::OGT_F64, "__aeabi_dcmpgt", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::UO_F64,  "__aeabi_dcmpun", CallingConv::ARM_AAPCS, ISD::SETNE },

      // Single-precision floating-point arithmetic helper functions
      // RTABI chapter 4.1.2, Table 4
      { RTLIB::ADD_F32, "__aeabi_fadd", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::DIV_F32, "__aeabi_fdiv", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::MUL_F32, "__aeabi_fmul", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SUB_F32, "__aeabi_fsub", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },

      // Single-precision floating-point comparison helper functions
      // RTABI chapter 4.1.2, Table 5
      { RTLIB::OEQ_F32, "__aeabi_fcmpeq", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::UNE_F32, "__aeabi_fcmpeq", CallingConv::ARM_AAPCS, ISD::SETEQ },
      { RTLIB::OLT_F32, "__aeabi_fcmplt", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::OLE_F32, "__aeabi_fcmple", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::OGE_F32, "__aeabi_fcmpge", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::OGT_F32, "__aeabi_fcmpgt", CallingConv::ARM_AAPCS, ISD::SETNE },
      { RTLIB::UO_F32,  "__aeabi_fcmpun", CallingConv::ARM_AAPCS, ISD::SETNE },

      // Floating-point to integer conversions.
      // RTABI chapter 4.1.2, Table 6
      { RTLIB::FPTOSINT_F64_I32, "__aeabi_d2iz",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::FPTOUINT_F64_I32, "__aeabi_d2uiz", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::FPTOSINT_F64_I64, "__aeabi_d2lz",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::FPTOUINT_F64_I64, "__aeabi_d2ulz", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::FPTOSINT_F32_I32, "__aeabi_f2iz",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::FPTOUINT_F32_I32, "__aeabi_f2uiz", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::FPTOSINT_F32_I64, "__aeabi_f2lz",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::FPTOUINT_F32_I64, "__aeabi_f2ulz", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },

      // Conversions between floating types.
      // RTABI chapter 4.1.2, Table 7
      { RTLIB::FPROUND_F64_F32, "__aeabi_d2f", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::FPROUND_F64_F16, "__aeabi_d2h", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::FPEXT_F32_F64,   "__aeabi_f2d", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },

      // Integer to floating-point conversions.
      // RTABI chapter 4.1.2, Table 8
      { RTLIB::SINTTOFP_I32_F64, "__aeabi_i2d",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::UINTTOFP_I32_F64, "__aeabi_ui2d", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SINTTOFP_I64_F64, "__aeabi_l2d",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::UINTTOFP_I64_F64, "__aeabi_ul2d", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SINTTOFP_I32_F32, "__aeabi_i2f",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::UINTTOFP_I32_F32, "__aeabi_ui2f", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SINTTOFP_I64_F32, "__aeabi_l2f",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::UINTTOFP_I64_F32, "__aeabi_ul2f", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },

      // Long long helper functions
      // RTABI chapter 4.2, Table 9
      { RTLIB::MUL_I64, "__aeabi_lmul", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SHL_I64, "__aeabi_llsl", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SRL_I64, "__aeabi_llsr", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SRA_I64, "__aeabi_lasr", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },

      // Integer division functions
      // RTABI chapter 4.3.1
      { RTLIB::SDIV_I8,  "__aeabi_idiv",     CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SDIV_I16, "__aeabi_idiv",     CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SDIV_I32, "__aeabi_idiv",     CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::SDIV_I64, "__aeabi_ldivmod",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::UDIV_I8,  "__aeabi_uidiv",    CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::UDIV_I16, "__aeabi_uidiv",    CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::UDIV_I32, "__aeabi_uidiv",    CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      { RTLIB::UDIV_I64, "__aeabi_uldivmod", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
    };

    for (const auto &LC : LibraryCalls) {
      setLibcallName(LC.Op, LC.Name);
      setLibcallCallingConv(LC.Op, LC.CC);
      if (LC.Cond != ISD::SETCC_INVALID)
        setCmpLibcallCC(LC.Op, LC.Cond);
    }

    // EABI dependent RTLIB
    if (TM.Options.EABIVersion == EABI::EABI4 ||
        TM.Options.EABIVersion == EABI::EABI5) {
      static const struct {
        const RTLIB::Libcall Op;
        const char *const Name;
        const CallingConv::ID CC;
        const ISD::CondCode Cond;
      } MemOpsLibraryCalls[] = {
        // Memory operations
        // RTABI chapter 4.3.4
        { RTLIB::MEMCPY,  "__aeabi_memcpy",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
        { RTLIB::MEMMOVE, "__aeabi_memmove", CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
        { RTLIB::MEMSET,  "__aeabi_memset",  CallingConv::ARM_AAPCS, ISD::SETCC_INVALID },
      };

      for (const auto &LC : MemOpsLibraryCalls) {
        setLibcallName(LC.Op, LC.Name);
        setLibcallCallingConv(LC.Op, LC.CC);
        if (LC.Cond != ISD::SETCC_INVALID)
          setCmpLibcallCC(LC.Op, LC.Cond);
      }
    }
  }

  if (Subtarget->isTargetWindows()) {
    static const struct {
      const RTLIB::Libcall Op;
      const char * const Name;
      const CallingConv::ID CC;
    } LibraryCalls[] = {
      { RTLIB::FPTOSINT_F32_I64, "__stoi64", CallingConv::ARM_AAPCS_VFP },
      { RTLIB::FPTOSINT_F64_I64, "__dtoi64", CallingConv::ARM_AAPCS_VFP },
      { RTLIB::FPTOUINT_F32_I64, "__stou64", CallingConv::ARM_AAPCS_VFP },
      { RTLIB::FPTOUINT_F64_I64, "__dtou64", CallingConv::ARM_AAPCS_VFP },
      { RTLIB::SINTTOFP_I64_F32, "__i64tos", CallingConv::ARM_AAPCS_VFP },
      { RTLIB::SINTTOFP_I64_F64, "__i64tod", CallingConv::ARM_AAPCS_VFP },
      { RTLIB::UINTTOFP_I64_F32, "__u64tos", CallingConv::ARM_AAPCS_VFP },
      { RTLIB::UINTTOFP_I64_F64, "__u64tod", CallingConv::ARM_AAPCS_VFP },
    };

    for (const auto &LC : LibraryCalls) {
      setLibcallName(LC.Op, LC.Name);
      setLibcallCallingConv(LC.Op, LC.CC);
    }
  }

  // Use divmod compiler-rt calls for iOS 5.0 and later.
  if (Subtarget->isTargetMachO() &&
      !(Subtarget->isTargetIOS() &&
        Subtarget->getTargetTriple().isOSVersionLT(5, 0))) {
    setLibcallName(RTLIB::SDIVREM_I32, "__divmodsi4");
    setLibcallName(RTLIB::UDIVREM_I32, "__udivmodsi4");
  }

  // The half <-> float conversion functions are always soft-float on
  // non-watchos platforms, but are needed for some targets which use a
  // hard-float calling convention by default.
  if (!Subtarget->isTargetWatchABI()) {
    if (Subtarget->isAAPCS_ABI()) {
      setLibcallCallingConv(RTLIB::FPROUND_F32_F16, CallingConv::ARM_AAPCS);
      setLibcallCallingConv(RTLIB::FPROUND_F64_F16, CallingConv::ARM_AAPCS);
      setLibcallCallingConv(RTLIB::FPEXT_F16_F32, CallingConv::ARM_AAPCS);
    } else {
      setLibcallCallingConv(RTLIB::FPROUND_F32_F16, CallingConv::ARM_APCS);
      setLibcallCallingConv(RTLIB::FPROUND_F64_F16, CallingConv::ARM_APCS);
      setLibcallCallingConv(RTLIB::FPEXT_F16_F32, CallingConv::ARM_APCS);
    }
  }

  // In EABI, these functions have an __aeabi_ prefix, but in GNUEABI they have
  // a __gnu_ prefix (which is the default).
  if (Subtarget->isTargetAEABI()) {
    static const struct {
      const RTLIB::Libcall Op;
      const char * const Name;
      const CallingConv::ID CC;
    } LibraryCalls[] = {
      { RTLIB::FPROUND_F32_F16, "__aeabi_f2h", CallingConv::ARM_AAPCS },
      { RTLIB::FPROUND_F64_F16, "__aeabi_d2h", CallingConv::ARM_AAPCS },
      { RTLIB::FPEXT_F16_F32, "__aeabi_h2f", CallingConv::ARM_AAPCS },
    };

    for (const auto &LC : LibraryCalls) {
      setLibcallName(LC.Op, LC.Name);
      setLibcallCallingConv(LC.Op, LC.CC);
    }
  }

  if (Subtarget->isThumb1Only())
    addRegisterClass(MVT::i32, &ARM::tGPRRegClass);
  else
    addRegisterClass(MVT::i32, &ARM::GPRRegClass);

  if (!Subtarget->useSoftFloat() && !Subtarget->isThumb1Only() &&
      Subtarget->hasFPRegs()) {
    addRegisterClass(MVT::f32, &ARM::SPRRegClass);
    addRegisterClass(MVT::f64, &ARM::DPRRegClass);

    setOperationAction(ISD::FP_TO_SINT_SAT, MVT::i32, Custom);
    setOperationAction(ISD::FP_TO_UINT_SAT, MVT::i32, Custom);
    setOperationAction(ISD::FP_TO_SINT_SAT, MVT::i64, Custom);
    setOperationAction(ISD::FP_TO_UINT_SAT, MVT::i64, Custom);

    if (!Subtarget->hasVFP2Base())
      setAllExpand(MVT::f32);
    if (!Subtarget->hasFP64())
      setAllExpand(MVT::f64);
  }

  if (Subtarget->hasFullFP16()) {
    addRegisterClass(MVT::f16, &ARM::HPRRegClass);
    setOperationAction(ISD::BITCAST, MVT::i16, Custom);
    setOperationAction(ISD::BITCAST, MVT::f16, Custom);

    setOperationAction(ISD::FMINNUM, MVT::f16, Legal);
    setOperationAction(ISD::FMAXNUM, MVT::f16, Legal);
  }

  if (Subtarget->hasBF16()) {
    addRegisterClass(MVT::bf16, &ARM::HPRRegClass);
    setAllExpand(MVT::bf16);
    if (!Subtarget->hasFullFP16())
      setOperationAction(ISD::BITCAST, MVT::bf16, Custom);
  }

  for (MVT VT : MVT::fixedlen_vector_valuetypes()) {
    for (MVT InnerVT : MVT::fixedlen_vector_valuetypes()) {
      setTruncStoreAction(VT, InnerVT, Expand);
      addAllExtLoads(VT, InnerVT, Expand);
    }

    setOperationAction(ISD::SMUL_LOHI, VT, Expand);
    setOperationAction(ISD::UMUL_LOHI, VT, Expand);

    setOperationAction(ISD::BSWAP, VT, Expand);
  }

  setOperationAction(ISD::ConstantFP, MVT::f32, Custom);
  setOperationAction(ISD::ConstantFP, MVT::f64, Custom);

  setOperationAction(ISD::READ_REGISTER, MVT::i64, Custom);
  setOperationAction(ISD::WRITE_REGISTER, MVT::i64, Custom);

  if (Subtarget->hasMVEIntegerOps())
    addMVEVectorTypes(Subtarget->hasMVEFloatOps());

  // Combine low-overhead loop intrinsics so that we can lower i1 types.
  if (Subtarget->hasLOB()) {
    setTargetDAGCombine({ISD::BRCOND, ISD::BR_CC});
  }

  if (Subtarget->hasNEON()) {
    addDRTypeForNEON(MVT::v2f32);
    addDRTypeForNEON(MVT::v8i8);
    addDRTypeForNEON(MVT::v4i16);
    addDRTypeForNEON(MVT::v2i32);
    addDRTypeForNEON(MVT::v1i64);

    addQRTypeForNEON(MVT::v4f32);
    addQRTypeForNEON(MVT::v2f64);
    addQRTypeForNEON(MVT::v16i8);
    addQRTypeForNEON(MVT::v8i16);
    addQRTypeForNEON(MVT::v4i32);
    addQRTypeForNEON(MVT::v2i64);

    if (Subtarget->hasFullFP16()) {
      addQRTypeForNEON(MVT::v8f16);
      addDRTypeForNEON(MVT::v4f16);
    }

    if (Subtarget->hasBF16()) {
      addQRTypeForNEON(MVT::v8bf16);
      addDRTypeForNEON(MVT::v4bf16);
    }
  }

  if (Subtarget->hasMVEIntegerOps() || Subtarget->hasNEON()) {
    // v2f64 is legal so that QR subregs can be extracted as f64 elements, but
    // none of Neon, MVE or VFP supports any arithmetic operations on it.
    setOperationAction(ISD::FADD, MVT::v2f64, Expand);
    setOperationAction(ISD::FSUB, MVT::v2f64, Expand);
    setOperationAction(ISD::FMUL, MVT::v2f64, Expand);
    // FIXME: Code duplication: FDIV and FREM are expanded always, see
    // ARMTargetLowering::addTypeForNEON method for details.
    setOperationAction(ISD::FDIV, MVT::v2f64, Expand);
    setOperationAction(ISD::FREM, MVT::v2f64, Expand);
    // FIXME: Create unittest.
    // In another words, find a way when "copysign" appears in DAG with vector
    // operands.
    setOperationAction(ISD::FCOPYSIGN, MVT::v2f64, Expand);
    // FIXME: Code duplication: SETCC has custom operation action, see
    // ARMTargetLowering::addTypeForNEON method for details.
    setOperationAction(ISD::SETCC, MVT::v2f64, Expand);
    // FIXME: Create unittest for FNEG and for FABS.
    setOperationAction(ISD::FNEG, MVT::v2f64, Expand);
    setOperationAction(ISD::FABS, MVT::v2f64, Expand);
    setOperationAction(ISD::FSQRT, MVT::v2f64, Expand);
    setOperationAction(ISD::FSIN, MVT::v2f64, Expand);
    setOperationAction(ISD::FCOS, MVT::v2f64, Expand);
    setOperationAction(ISD::FTAN, MVT::v2f64, Expand);
    setOperationAction(ISD::FPOW, MVT::v2f64, Expand);
    setOperationAction(ISD::FLOG, MVT::v2f64, Expand);
    setOperationAction(ISD::FLOG2, MVT::v2f64, Expand);
    setOperationAction(ISD::FLOG10, MVT::v2f64, Expand);
    setOperationAction(ISD::FEXP, MVT::v2f64, Expand);
    setOperationAction(ISD::FEXP2, MVT::v2f64, Expand);
    setOperationAction(ISD::FEXP10, MVT::v2f64, Expand);
    // FIXME: Create unittest for FCEIL, FTRUNC, FRINT, FNEARBYINT, FFLOOR.
    setOperationAction(ISD::FCEIL, MVT::v2f64, Expand);
    setOperationAction(ISD::FTRUNC, MVT::v2f64, Expand);
    setOperationAction(ISD::FRINT, MVT::v2f64, Expand);
    setOperationAction(ISD::FNEARBYINT, MVT::v2f64, Expand);
    setOperationAction(ISD::FFLOOR, MVT::v2f64, Expand);
    setOperationAction(ISD::FMA, MVT::v2f64, Expand);
  }

  if (Subtarget->hasNEON()) {
    // The same with v4f32. But keep in mind that vadd, vsub, vmul are natively
    // supported for v4f32.
    setOperationAction(ISD::FSQRT, MVT::v4f32, Expand);
    setOperationAction(ISD::FSIN, MVT::v4f32, Expand);
    setOperationAction(ISD::FCOS, MVT::v4f32, Expand);
    setOperationAction(ISD::FTAN, MVT::v4f32, Expand);
    setOperationAction(ISD::FPOW, MVT::v4f32, Expand);
    setOperationAction(ISD::FLOG, MVT::v4f32, Expand);
    setOperationAction(ISD::FLOG2, MVT::v4f32, Expand);
    setOperationAction(ISD::FLOG10, MVT::v4f32, Expand);
    setOperationAction(ISD::FEXP, MVT::v4f32, Expand);
    setOperationAction(ISD::FEXP2, MVT::v4f32, Expand);
    setOperationAction(ISD::FEXP10, MVT::v4f32, Expand);
    setOperationAction(ISD::FCEIL, MVT::v4f32, Expand);
    setOperationAction(ISD::FTRUNC, MVT::v4f32, Expand);
    setOperationAction(ISD::FRINT, MVT::v4f32, Expand);
    setOperationAction(ISD::FNEARBYINT, MVT::v4f32, Expand);
    setOperationAction(ISD::FFLOOR, MVT::v4f32, Expand);

    // Mark v2f32 intrinsics.
    setOperationAction(ISD::FSQRT, MVT::v2f32, Expand);
    setOperationAction(ISD::FSIN, MVT::v2f32, Expand);
    setOperationAction(ISD::FCOS, MVT::v2f32, Expand);
    setOperationAction(ISD::FTAN, MVT::v2f32, Expand);
    setOperationAction(ISD::FPOW, MVT::v2f32, Expand);
    setOperationAction(ISD::FLOG, MVT::v2f32, Expand);
    setOperationAction(ISD::FLOG2, MVT::v2f32, Expand);
    setOperationAction(ISD::FLOG10, MVT::v2f32, Expand);
    setOperationAction(ISD::FEXP, MVT::v2f32, Expand);
    setOperationAction(ISD::FEXP2, MVT::v2f32, Expand);
    setOperationAction(ISD::FEXP10, MVT::v2f32, Expand);
    setOperationAction(ISD::FCEIL, MVT::v2f32, Expand);
    setOperationAction(ISD::FTRUNC, MVT::v2f32, Expand);
    setOperationAction(ISD::FRINT, MVT::v2f32, Expand);
    setOperationAction(ISD::FNEARBYINT, MVT::v2f32, Expand);
    setOperationAction(ISD::FFLOOR, MVT::v2f32, Expand);

    // Neon does not support some operations on v1i64 and v2i64 types.
    setOperationAction(ISD::MUL, MVT::v1i64, Expand);
    // Custom handling for some quad-vector types to detect VMULL.
    setOperationAction(ISD::MUL, MVT::v8i16, Custom);
    setOperationAction(ISD::MUL, MVT::v4i32, Custom);
    setOperationAction(ISD::MUL, MVT::v2i64, Custom);
    // Custom handling for some vector types to avoid expensive expansions
    setOperationAction(ISD::SDIV, MVT::v4i16, Custom);
    setOperationAction(ISD::SDIV, MVT::v8i8, Custom);
    setOperationAction(ISD::UDIV, MVT::v4i16, Custom);
    setOperationAction(ISD::UDIV, MVT::v8i8, Custom);
    // Neon does not have single instruction SINT_TO_FP and UINT_TO_FP with
    // a destination type that is wider than the source, and nor does
    // it have a FP_TO_[SU]INT instruction with a narrower destination than
    // source.
    setOperationAction(ISD::SINT_TO_FP, MVT::v4i16, Custom);
    setOperationAction(ISD::SINT_TO_FP, MVT::v8i16, Custom);
    setOperationAction(ISD::UINT_TO_FP, MVT::v4i16, Custom);
    setOperationAction(ISD::UINT_TO_FP, MVT::v8i16, Custom);
    setOperationAction(ISD::FP_TO_UINT, MVT::v4i16, Custom);
    setOperationAction(ISD::FP_TO_UINT, MVT::v8i16, Custom);
    setOperationAction(ISD::FP_TO_SINT, MVT::v4i16, Custom);
    setOperationAction(ISD::FP_TO_SINT, MVT::v8i16, Custom);

    setOperationAction(ISD::FP_ROUND,   MVT::v2f32, Expand);
    setOperationAction(ISD::FP_EXTEND,  MVT::v2f64, Expand);

    // NEON does not have single instruction CTPOP for vectors with element
    // types wider than 8-bits.  However, custom lowering can leverage the
    // v8i8/v16i8 vcnt instruction.
    setOperationAction(ISD::CTPOP,      MVT::v2i32, Custom);
    setOperationAction(ISD::CTPOP,      MVT::v4i32, Custom);
    setOperationAction(ISD::CTPOP,      MVT::v4i16, Custom);
    setOperationAction(ISD::CTPOP,      MVT::v8i16, Custom);
    setOperationAction(ISD::CTPOP,      MVT::v1i64, Custom);
    setOperationAction(ISD::CTPOP,      MVT::v2i64, Custom);

    setOperationAction(ISD::CTLZ,       MVT::v1i64, Expand);
    setOperationAction(ISD::CTLZ,       MVT::v2i64, Expand);

    // NEON does not have single instruction CTTZ for vectors.
    setOperationAction(ISD::CTTZ, MVT::v8i8, Custom);
    setOperationAction(ISD::CTTZ, MVT::v4i16, Custom);
    setOperationAction(ISD::CTTZ, MVT::v2i32, Custom);
    setOperationAction(ISD::CTTZ, MVT::v1i64, Custom);

    setOperationAction(ISD::CTTZ, MVT::v16i8, Custom);
    setOperationAction(ISD::CTTZ, MVT::v8i16, Custom);
    setOperationAction(ISD::CTTZ, MVT::v4i32, Custom);
    setOperationAction(ISD::CTTZ, MVT::v2i64, Custom);

    setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::v8i8, Custom);
    setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::v4i16, Custom);
    setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::v2i32, Custom);
    setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::v1i64, Custom);

    setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::v16i8, Custom);
    setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::v8i16, Custom);
    setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::v4i32, Custom);
    setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::v2i64, Custom);

    for (MVT VT : MVT::fixedlen_vector_valuetypes()) {
      setOperationAction(ISD::MULHS, VT, Expand);
      setOperationAction(ISD::MULHU, VT, Expand);
    }

    // NEON only has FMA instructions as of VFP4.
    if (!Subtarget->hasVFP4Base()) {
      setOperationAction(ISD::FMA, MVT::v2f32, Expand);
      setOperationAction(ISD::FMA, MVT::v4f32, Expand);
    }

    setTargetDAGCombine({ISD::SHL, ISD::SRL, ISD::SRA, ISD::FP_TO_SINT,
                         ISD::FP_TO_UINT, ISD::FMUL, ISD::LOAD});

    // It is legal to extload from v4i8 to v4i16 or v4i32.
    for (MVT Ty : {MVT::v8i8, MVT::v4i8, MVT::v2i8, MVT::v4i16, MVT::v2i16,
                   MVT::v2i32}) {
      for (MVT VT : MVT::integer_fixedlen_vector_valuetypes()) {
        setLoadExtAction(ISD::EXTLOAD, VT, Ty, Legal);
        setLoadExtAction(ISD::ZEXTLOAD, VT, Ty, Legal);
        setLoadExtAction(ISD::SEXTLOAD, VT, Ty, Legal);
      }
    }

    for (auto VT : {MVT::v8i8, MVT::v4i16, MVT::v2i32, MVT::v16i8, MVT::v8i16,
                    MVT::v4i32}) {
      setOperationAction(ISD::VECREDUCE_SMAX, VT, Custom);
      setOperationAction(ISD::VECREDUCE_UMAX, VT, Custom);
      setOperationAction(ISD::VECREDUCE_SMIN, VT, Custom);
      setOperationAction(ISD::VECREDUCE_UMIN, VT, Custom);
    }
  }

  if (Subtarget->hasNEON() || Subtarget->hasMVEIntegerOps()) {
    setTargetDAGCombine(
        {ISD::BUILD_VECTOR, ISD::VECTOR_SHUFFLE, ISD::INSERT_SUBVECTOR,
         ISD::INSERT_VECTOR_ELT, ISD::EXTRACT_VECTOR_ELT,
         ISD::SIGN_EXTEND_INREG, ISD::STORE, ISD::SIGN_EXTEND, ISD::ZERO_EXTEND,
         ISD::ANY_EXTEND, ISD::INTRINSIC_WO_CHAIN, ISD::INTRINSIC_W_CHAIN,
         ISD::INTRINSIC_VOID, ISD::VECREDUCE_ADD, ISD::ADD, ISD::BITCAST});
  }
  if (Subtarget->hasMVEIntegerOps()) {
    setTargetDAGCombine({ISD::SMIN, ISD::UMIN, ISD::SMAX, ISD::UMAX,
                         ISD::FP_EXTEND, ISD::SELECT, ISD::SELECT_CC,
                         ISD::SETCC});
  }
  if (Subtarget->hasMVEFloatOps()) {
    setTargetDAGCombine(ISD::FADD);
  }

  if (!Subtarget->hasFP64()) {
    // When targeting a floating-point unit with only single-precision
    // operations, f64 is legal for the few double-precision instructions which
    // are present However, no double-precision operations other than moves,
    // loads and stores are provided by the hardware.
    setOperationAction(ISD::FADD,       MVT::f64, Expand);
    setOperationAction(ISD::FSUB,       MVT::f64, Expand);
    setOperationAction(ISD::FMUL,       MVT::f64, Expand);
    setOperationAction(ISD::FMA,        MVT::f64, Expand);
    setOperationAction(ISD::FDIV,       MVT::f64, Expand);
    setOperationAction(ISD::FREM,       MVT::f64, Expand);
    setOperationAction(ISD::FCOPYSIGN,  MVT::f64, Expand);
    setOperationAction(ISD::FGETSIGN,   MVT::f64, Expand);
    setOperationAction(ISD::FNEG,       MVT::f64, Expand);
    setOperationAction(ISD::FABS,       MVT::f64, Expand);
    setOperationAction(ISD::FSQRT,      MVT::f64, Expand);
    setOperationAction(ISD::FSIN,       MVT::f64, Expand);
    setOperationAction(ISD::FCOS,       MVT::f64, Expand);
    setOperationAction(ISD::FPOW,       MVT::f64, Expand);
    setOperationAction(ISD::FLOG,       MVT::f64, Expand);
    setOperationAction(ISD::FLOG2,      MVT::f64, Expand);
    setOperationAction(ISD::FLOG10,     MVT::f64, Expand);
    setOperationAction(ISD::FEXP,       MVT::f64, Expand);
    setOperationAction(ISD::FEXP2,      MVT::f64, Expand);
    setOperationAction(ISD::FEXP10,      MVT::f64, Expand);
    setOperationAction(ISD::FCEIL,      MVT::f64, Expand);
    setOperationAction(ISD::FTRUNC,     MVT::f64, Expand);
    setOperationAction(ISD::FRINT,      MVT::f64, Expand);
    setOperationAction(ISD::FNEARBYINT, MVT::f64, Expand);
    setOperationAction(ISD::FFLOOR,     MVT::f64, Expand);
    setOperationAction(ISD::SINT_TO_FP, MVT::i32, Custom);
    setOperationAction(ISD::UINT_TO_FP, MVT::i32, Custom);
    setOperationAction(ISD::FP_TO_SINT, MVT::i32, Custom);
    setOperationAction(ISD::FP_TO_UINT, MVT::i32, Custom);
    setOperationAction(ISD::FP_TO_SINT, MVT::f64, Custom);
    setOperationAction(ISD::FP_TO_UINT, MVT::f64, Custom);
    setOperationAction(ISD::FP_ROUND,   MVT::f32, Custom);
    setOperationAction(ISD::STRICT_FP_TO_SINT, MVT::i32, Custom);
    setOperationAction(ISD::STRICT_FP_TO_UINT, MVT::i32, Custom);
    setOperationAction(ISD::STRICT_FP_TO_SINT, MVT::f64, Custom);
    setOperationAction(ISD::STRICT_FP_TO_UINT, MVT::f64, Custom);
    setOperationAction(ISD::STRICT_FP_ROUND,   MVT::f32, Custom);
  }

  if (!Subtarget->hasFP64() || !Subtarget->hasFPARMv8Base()) {
    setOperationAction(ISD::FP_EXTEND,  MVT::f64, Custom);
    setOperationAction(ISD::STRICT_FP_EXTEND, MVT::f64, Custom);
    if (Subtarget->hasFullFP16()) {
      setOperationAction(ISD::FP_ROUND,  MVT::f16, Custom);
      setOperationAction(ISD::STRICT_FP_ROUND, MVT::f16, Custom);
    }
  }

  if (!Subtarget->hasFP16()) {
    setOperationAction(ISD::FP_EXTEND,  MVT::f32, Custom);
    setOperationAction(ISD::STRICT_FP_EXTEND, MVT::f32, Custom);
  }

  computeRegisterProperties(Subtarget->getRegisterInfo());

  // ARM does not have floating-point extending loads.
  for (MVT VT : MVT::fp_valuetypes()) {
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::f32, Expand);
    setLoadExtAction(ISD::EXTLOAD, VT, MVT::f16, Expand);
  }

  // ... or truncating stores
  setTruncStoreAction(MVT::f64, MVT::f32, Expand);
  setTruncStoreAction(MVT::f32, MVT::f16, Expand);
  setTruncStoreAction(MVT::f64, MVT::f16, Expand);

  // ARM does not have i1 sign extending load.
  for (MVT VT : MVT::integer_valuetypes())
    setLoadExtAction(ISD::SEXTLOAD, VT, MVT::i1, Promote);

  // ARM supports all 4 flavors of integer indexed load / store.
  if (!Subtarget->isThumb1Only()) {
    for (unsigned im = (unsigned)ISD::PRE_INC;
         im != (unsigned)ISD::LAST_INDEXED_MODE; ++im) {
      setIndexedLoadAction(im,  MVT::i1,  Legal);
      setIndexedLoadAction(im,  MVT::i8,  Legal);
      setIndexedLoadAction(im,  MVT::i16, Legal);
      setIndexedLoadAction(im,  MVT::i32, Legal);
      setIndexedStoreAction(im, MVT::i1,  Legal);
      setIndexedStoreAction(im, MVT::i8,  Legal);
      setIndexedStoreAction(im, MVT::i16, Legal);
      setIndexedStoreAction(im, MVT::i32, Legal);
    }
  } else {
    // Thumb-1 has limited post-inc load/store support - LDM r0!, {r1}.
    setIndexedLoadAction(ISD::POST_INC, MVT::i32,  Legal);
    setIndexedStoreAction(ISD::POST_INC, MVT::i32,  Legal);
  }

  setOperationAction(ISD::SADDO, MVT::i32, Custom);
  setOperationAction(ISD::UADDO, MVT::i32, Custom);
  setOperationAction(ISD::SSUBO, MVT::i32, Custom);
  setOperationAction(ISD::USUBO, MVT::i32, Custom);

  setOperationAction(ISD::UADDO_CARRY, MVT::i32, Custom);
  setOperationAction(ISD::USUBO_CARRY, MVT::i32, Custom);
  if (Subtarget->hasDSP()) {
    setOperationAction(ISD::SADDSAT, MVT::i8, Custom);
    setOperationAction(ISD::SSUBSAT, MVT::i8, Custom);
    setOperationAction(ISD::SADDSAT, MVT::i16, Custom);
    setOperationAction(ISD::SSUBSAT, MVT::i16, Custom);
    setOperationAction(ISD::UADDSAT, MVT::i8, Custom);
    setOperationAction(ISD::USUBSAT, MVT::i8, Custom);
    setOperationAction(ISD::UADDSAT, MVT::i16, Custom);
    setOperationAction(ISD::USUBSAT, MVT::i16, Custom);
  }
  if (Subtarget->hasBaseDSP()) {
    setOperationAction(ISD::SADDSAT, MVT::i32, Legal);
    setOperationAction(ISD::SSUBSAT, MVT::i32, Legal);
  }

  // i64 operation support.
  setOperationAction(ISD::MUL,     MVT::i64, Expand);
  setOperationAction(ISD::MULHU,   MVT::i32, Expand);
  if (Subtarget->isThumb1Only()) {
    setOperationAction(ISD::UMUL_LOHI, MVT::i32, Expand);
    setOperationAction(ISD::SMUL_LOHI, MVT::i32, Expand);
  }
  if (Subtarget->isThumb1Only() || !Subtarget->hasV6Ops()
      || (Subtarget->isThumb2() && !Subtarget->hasDSP()))
    setOperationAction(ISD::MULHS, MVT::i32, Expand);

  setOperationAction(ISD::SHL_PARTS, MVT::i32, Custom);
  setOperationAction(ISD::SRA_PARTS, MVT::i32, Custom);
  setOperationAction(ISD::SRL_PARTS, MVT::i32, Custom);
  setOperationAction(ISD::SRL,       MVT::i64, Custom);
  setOperationAction(ISD::SRA,       MVT::i64, Custom);
  setOperationAction(ISD::INTRINSIC_VOID, MVT::Other, Custom);
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::i64, Custom);
  setOperationAction(ISD::LOAD, MVT::i64, Custom);
  setOperationAction(ISD::STORE, MVT::i64, Custom);

  // MVE lowers 64 bit shifts to lsll and lsrl
  // assuming that ISD::SRL and SRA of i64 are already marked custom
  if (Subtarget->hasMVEIntegerOps())
    setOperationAction(ISD::SHL, MVT::i64, Custom);

  // Expand to __aeabi_l{lsl,lsr,asr} calls for Thumb1.
  if (Subtarget->isThumb1Only()) {
    setOperationAction(ISD::SHL_PARTS, MVT::i32, Expand);
    setOperationAction(ISD::SRA_PARTS, MVT::i32, Expand);
    setOperationAction(ISD::SRL_PARTS, MVT::i32, Expand);
  }

  if (!Subtarget->isThumb1Only() && Subtarget->hasV6T2Ops())
    setOperationAction(ISD::BITREVERSE, MVT::i32, Legal);

  // ARM does not have ROTL.
  setOperationAction(ISD::ROTL, MVT::i32, Expand);
  for (MVT VT : MVT::fixedlen_vector_valuetypes()) {
    setOperationAction(ISD::ROTL, VT, Expand);
    setOperationAction(ISD::ROTR, VT, Expand);
  }
  setOperationAction(ISD::CTTZ,  MVT::i32, Custom);
  setOperationAction(ISD::CTPOP, MVT::i32, Expand);
  if (!Subtarget->hasV5TOps() || Subtarget->isThumb1Only()) {
    setOperationAction(ISD::CTLZ, MVT::i32, Expand);
    setOperationAction(ISD::CTLZ_ZERO_UNDEF, MVT::i32, LibCall);
  }

  // @llvm.readcyclecounter requires the Performance Monitors extension.
  // Default to the 0 expansion on unsupported platforms.
  // FIXME: Technically there are older ARM CPUs that have
  // implementation-specific ways of obtaining this information.
  if (Subtarget->hasPerfMon())
    setOperationAction(ISD::READCYCLECOUNTER, MVT::i64, Custom);

  // Only ARMv6 has BSWAP.
  if (!Subtarget->hasV6Ops())
    setOperationAction(ISD::BSWAP, MVT::i32, Expand);

  bool hasDivide = Subtarget->isThumb() ? Subtarget->hasDivideInThumbMode()
                                        : Subtarget->hasDivideInARMMode();
  if (!hasDivide) {
    // These are expanded into libcalls if the cpu doesn't have HW divider.
    setOperationAction(ISD::SDIV,  MVT::i32, LibCall);
    setOperationAction(ISD::UDIV,  MVT::i32, LibCall);
  }

  if (Subtarget->isTargetWindows() && !Subtarget->hasDivideInThumbMode()) {
    setOperationAction(ISD::SDIV, MVT::i32, Custom);
    setOperationAction(ISD::UDIV, MVT::i32, Custom);

    setOperationAction(ISD::SDIV, MVT::i64, Custom);
    setOperationAction(ISD::UDIV, MVT::i64, Custom);
  }

  setOperationAction(ISD::SREM,  MVT::i32, Expand);
  setOperationAction(ISD::UREM,  MVT::i32, Expand);

  // Register based DivRem for AEABI (RTABI 4.2)
  if (Subtarget->isTargetAEABI() || Subtarget->isTargetAndroid() ||
      Subtarget->isTargetGNUAEABI() || Subtarget->isTargetMuslAEABI() ||
      Subtarget->isTargetWindows()) {
    setOperationAction(ISD::SREM, MVT::i64, Custom);
    setOperationAction(ISD::UREM, MVT::i64, Custom);
    HasStandaloneRem = false;

    if (Subtarget->isTargetWindows()) {
      const struct {
        const RTLIB::Libcall Op;
        const char * const Name;
        const CallingConv::ID CC;
      } LibraryCalls[] = {
        { RTLIB::SDIVREM_I8, "__rt_sdiv", CallingConv::ARM_AAPCS },
        { RTLIB::SDIVREM_I16, "__rt_sdiv", CallingConv::ARM_AAPCS },
        { RTLIB::SDIVREM_I32, "__rt_sdiv", CallingConv::ARM_AAPCS },
        { RTLIB::SDIVREM_I64, "__rt_sdiv64", CallingConv::ARM_AAPCS },

        { RTLIB::UDIVREM_I8, "__rt_udiv", CallingConv::ARM_AAPCS },
        { RTLIB::UDIVREM_I16, "__rt_udiv", CallingConv::ARM_AAPCS },
        { RTLIB::UDIVREM_I32, "__rt_udiv", CallingConv::ARM_AAPCS },
        { RTLIB::UDIVREM_I64, "__rt_udiv64", CallingConv::ARM_AAPCS },
      };

      for (const auto &LC : LibraryCalls) {
        setLibcallName(LC.Op, LC.Name);
        setLibcallCallingConv(LC.Op, LC.CC);
      }
    } else {
      const struct {
        const RTLIB::Libcall Op;
        const char * const Name;
        const CallingConv::ID CC;
      } LibraryCalls[] = {
        { RTLIB::SDIVREM_I8, "__aeabi_idivmod", CallingConv::ARM_AAPCS },
        { RTLIB::SDIVREM_I16, "__aeabi_idivmod", CallingConv::ARM_AAPCS },
        { RTLIB::SDIVREM_I32, "__aeabi_idivmod", CallingConv::ARM_AAPCS },
        { RTLIB::SDIVREM_I64, "__aeabi_ldivmod", CallingConv::ARM_AAPCS },

        { RTLIB::UDIVREM_I8, "__aeabi_uidivmod", CallingConv::ARM_AAPCS },
        { RTLIB::UDIVREM_I16, "__aeabi_uidivmod", CallingConv::ARM_AAPCS },
        { RTLIB::UDIVREM_I32, "__aeabi_uidivmod", CallingConv::ARM_AAPCS },
        { RTLIB::UDIVREM_I64, "__aeabi_uldivmod", CallingConv::ARM_AAPCS },
      };

      for (const auto &LC : LibraryCalls) {
        setLibcallName(LC.Op, LC.Name);
        setLibcallCallingConv(LC.Op, LC.CC);
      }
    }

    setOperationAction(ISD::SDIVREM, MVT::i32, Custom);
    setOperationAction(ISD::UDIVREM, MVT::i32, Custom);
    setOperationAction(ISD::SDIVREM, MVT::i64, Custom);
    setOperationAction(ISD::UDIVREM, MVT::i64, Custom);
  } else {
    setOperationAction(ISD::SDIVREM, MVT::i32, Expand);
    setOperationAction(ISD::UDIVREM, MVT::i32, Expand);
  }

  setOperationAction(ISD::GlobalAddress, MVT::i32,   Custom);
  setOperationAction(ISD::ConstantPool,  MVT::i32,   Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i32, Custom);
  setOperationAction(ISD::BlockAddress, MVT::i32, Custom);

  setOperationAction(ISD::TRAP, MVT::Other, Legal);
  setOperationAction(ISD::DEBUGTRAP, MVT::Other, Legal);

  // Use the default implementation.
  setOperationAction(ISD::VASTART,            MVT::Other, Custom);
  setOperationAction(ISD::VAARG,              MVT::Other, Expand);
  setOperationAction(ISD::VACOPY,             MVT::Other, Expand);
  setOperationAction(ISD::VAEND,              MVT::Other, Expand);
  setOperationAction(ISD::STACKSAVE,          MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE,       MVT::Other, Expand);

  if (Subtarget->isTargetWindows())
    setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32, Custom);
  else
    setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32, Expand);

  // ARMv6 Thumb1 (except for CPUs that support dmb / dsb) and earlier use
  // the default expansion.
  InsertFencesForAtomic = false;
  if (Subtarget->hasAnyDataBarrier() &&
      (!Subtarget->isThumb() || Subtarget->hasV8MBaselineOps())) {
    // ATOMIC_FENCE needs custom lowering; the others should have been expanded
    // to ldrex/strex loops already.
    setOperationAction(ISD::ATOMIC_FENCE,     MVT::Other, Custom);
    if (!Subtarget->isThumb() || !Subtarget->isMClass())
      setOperationAction(ISD::ATOMIC_CMP_SWAP,  MVT::i64, Custom);

    // On v8, we have particularly efficient implementations of atomic fences
    // if they can be combined with nearby atomic loads and stores.
    if (!Subtarget->hasAcquireRelease() ||
        getTargetMachine().getOptLevel() == CodeGenOptLevel::None) {
      // Automatically insert fences (dmb ish) around ATOMIC_SWAP etc.
      InsertFencesForAtomic = true;
    }
  } else {
    // If there's anything we can use as a barrier, go through custom lowering
    // for ATOMIC_FENCE.
    // If target has DMB in thumb, Fences can be inserted.
    if (Subtarget->hasDataBarrier())
      InsertFencesForAtomic = true;

    setOperationAction(ISD::ATOMIC_FENCE,   MVT::Other,
                       Subtarget->hasAnyDataBarrier() ? Custom : Expand);

    // Set them all for libcall, which will force libcalls.
    setOperationAction(ISD::ATOMIC_CMP_SWAP, MVT::i32, LibCall);
    setOperationAction(ISD::ATOMIC_SWAP, MVT::i32, LibCall);
    setOperationAction(ISD::ATOMIC_LOAD_ADD, MVT::i32, LibCall);
    setOperationAction(ISD::ATOMIC_LOAD_SUB, MVT::i32, LibCall);
    setOperationAction(ISD::ATOMIC_LOAD_AND, MVT::i32, LibCall);
    setOperationAction(ISD::ATOMIC_LOAD_OR, MVT::i32, LibCall);
    setOperationAction(ISD::ATOMIC_LOAD_XOR, MVT::i32, LibCall);
    setOperationAction(ISD::ATOMIC_LOAD_NAND, MVT::i32, LibCall);
    setOperationAction(ISD::ATOMIC_LOAD_MIN, MVT::i32, LibCall);
    setOperationAction(ISD::ATOMIC_LOAD_MAX, MVT::i32, LibCall);
    setOperationAction(ISD::ATOMIC_LOAD_UMIN, MVT::i32, LibCall);
    setOperationAction(ISD::ATOMIC_LOAD_UMAX, MVT::i32, LibCall);
    // Mark ATOMIC_LOAD and ATOMIC_STORE custom so we can handle the
    // Unordered/Monotonic case.
    if (!InsertFencesForAtomic) {
      setOperationAction(ISD::ATOMIC_LOAD, MVT::i32, Custom);
      setOperationAction(ISD::ATOMIC_STORE, MVT::i32, Custom);
    }
  }

  // Compute supported atomic widths.
  if (Subtarget->isTargetLinux() ||
      (!Subtarget->isMClass() && Subtarget->hasV6Ops())) {
    // For targets where __sync_* routines are reliably available, we use them
    // if necessary.
    //
    // ARM Linux always supports 64-bit atomics through kernel-assisted atomic
    // routines (kernel 3.1 or later). FIXME: Not with compiler-rt?
    //
    // ARMv6 targets have native instructions in ARM mode. For Thumb mode,
    // such targets should provide __sync_* routines, which use the ARM mode
    // instructions. (ARMv6 doesn't have dmb, but it has an equivalent
    // encoding; see ARMISD::MEMBARRIER_MCR.)
    setMaxAtomicSizeInBitsSupported(64);
  } else if ((Subtarget->isMClass() && Subtarget->hasV8MBaselineOps()) ||
             Subtarget->hasForced32BitAtomics()) {
    // Cortex-M (besides Cortex-M0) have 32-bit atomics.
    setMaxAtomicSizeInBitsSupported(32);
  } else {
    // We can't assume anything about other targets; just use libatomic
    // routines.
    setMaxAtomicSizeInBitsSupported(0);
  }

  setMaxDivRemBitWidthSupported(64);

  setOperationAction(ISD::PREFETCH,         MVT::Other, Custom);

  // Requires SXTB/SXTH, available on v6 and up in both ARM and Thumb modes.
  if (!Subtarget->hasV6Ops()) {
    setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i16, Expand);
    setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i8,  Expand);
  }
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i1, Expand);

  if (!Subtarget->useSoftFloat() && Subtarget->hasFPRegs() &&
      !Subtarget->isThumb1Only()) {
    // Turn f64->i64 into VMOVRRD, i64 -> f64 to VMOVDRR
    // iff target supports vfp2.
    setOperationAction(ISD::BITCAST, MVT::i64, Custom);
    setOperationAction(ISD::GET_ROUNDING, MVT::i32, Custom);
    setOperationAction(ISD::SET_ROUNDING, MVT::Other, Custom);
    setOperationAction(ISD::GET_FPENV, MVT::i32, Legal);
    setOperationAction(ISD::SET_FPENV, MVT::i32, Legal);
    setOperationAction(ISD::RESET_FPENV, MVT::Other, Legal);
    setOperationAction(ISD::GET_FPMODE, MVT::i32, Legal);
    setOperationAction(ISD::SET_FPMODE, MVT::i32, Custom);
    setOperationAction(ISD::RESET_FPMODE, MVT::Other, Custom);
  }

  // We want to custom lower some of our intrinsics.
  setOperationAction(ISD::INTRINSIC_WO_CHAIN, MVT::Other, Custom);
  setOperationAction(ISD::EH_SJLJ_SETJMP, MVT::i32, Custom);
  setOperationAction(ISD::EH_SJLJ_LONGJMP, MVT::Other, Custom);
  setOperationAction(ISD::EH_SJLJ_SETUP_DISPATCH, MVT::Other, Custom);
  if (Subtarget->useSjLjEH())
    setLibcallName(RTLIB::UNWIND_RESUME, "_Unwind_SjLj_Resume");

  setOperationAction(ISD::SETCC,     MVT::i32, Expand);
  setOperationAction(ISD::SETCC,     MVT::f32, Expand);
  setOperationAction(ISD::SETCC,     MVT::f64, Expand);
  setOperationAction(ISD::SELECT,    MVT::i32, Custom);
  setOperationAction(ISD::SELECT,    MVT::f32, Custom);
  setOperationAction(ISD::SELECT,    MVT::f64, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::f32, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::f64, Custom);
  if (Subtarget->hasFullFP16()) {
    setOperationAction(ISD::SETCC,     MVT::f16, Expand);
    setOperationAction(ISD::SELECT,    MVT::f16, Custom);
    setOperationAction(ISD::SELECT_CC, MVT::f16, Custom);
  }

  setOperationAction(ISD::SETCCCARRY, MVT::i32, Custom);

  setOperationAction(ISD::BRCOND,    MVT::Other, Custom);
  setOperationAction(ISD::BR_CC,     MVT::i32,   Custom);
  if (Subtarget->hasFullFP16())
      setOperationAction(ISD::BR_CC, MVT::f16,   Custom);
  setOperationAction(ISD::BR_CC,     MVT::f32,   Custom);
  setOperationAction(ISD::BR_CC,     MVT::f64,   Custom);
  setOperationAction(ISD::BR_JT,     MVT::Other, Custom);

  // We don't support sin/cos/fmod/copysign/pow
  setOperationAction(ISD::FSIN,      MVT::f64, Expand);
  setOperationAction(ISD::FSIN,      MVT::f32, Expand);
  setOperationAction(ISD::FCOS,      MVT::f32, Expand);
  setOperationAction(ISD::FCOS,      MVT::f64, Expand);
  setOperationAction(ISD::FSINCOS,   MVT::f64, Expand);
  setOperationAction(ISD::FSINCOS,   MVT::f32, Expand);
  setOperationAction(ISD::FREM,      MVT::f64, Expand);
  setOperationAction(ISD::FREM,      MVT::f32, Expand);
  if (!Subtarget->useSoftFloat() && Subtarget->hasVFP2Base() &&
      !Subtarget->isThumb1Only()) {
    setOperationAction(ISD::FCOPYSIGN, MVT::f64, Custom);
    setOperationAction(ISD::FCOPYSIGN, MVT::f32, Custom);
  }
  setOperationAction(ISD::FPOW,      MVT::f64, Expand);
  setOperationAction(ISD::FPOW,      MVT::f32, Expand);

  if (!Subtarget->hasVFP4Base()) {
    setOperationAction(ISD::FMA, MVT::f64, Expand);
    setOperationAction(ISD::FMA, MVT::f32, Expand);
  }

  // Various VFP goodness
  if (!Subtarget->useSoftFloat() && !Subtarget->isThumb1Only()) {
    // FP-ARMv8 adds f64 <-> f16 conversion. Before that it should be expanded.
    if (!Subtarget->hasFPARMv8Base() || !Subtarget->hasFP64()) {
      setOperationAction(ISD::FP16_TO_FP, MVT::f64, Expand);
      setOperationAction(ISD::FP_TO_FP16, MVT::f64, Expand);
    }

    // fp16 is a special v7 extension that adds f16 <-> f32 conversions.
    if (!Subtarget->hasFP16()) {
      setOperationAction(ISD::FP16_TO_FP, MVT::f32, Expand);
      setOperationAction(ISD::FP_TO_FP16, MVT::f32, Expand);
    }

    // Strict floating-point comparisons need custom lowering.
    setOperationAction(ISD::STRICT_FSETCC,  MVT::f16, Custom);
    setOperationAction(ISD::STRICT_FSETCCS, MVT::f16, Custom);
    setOperationAction(ISD::STRICT_FSETCC,  MVT::f32, Custom);
    setOperationAction(ISD::STRICT_FSETCCS, MVT::f32, Custom);
    setOperationAction(ISD::STRICT_FSETCC,  MVT::f64, Custom);
    setOperationAction(ISD::STRICT_FSETCCS, MVT::f64, Custom);
  }

  // Use __sincos_stret if available.
  if (getLibcallName(RTLIB::SINCOS_STRET_F32) != nullptr &&
      getLibcallName(RTLIB::SINCOS_STRET_F64) != nullptr) {
    setOperationAction(ISD::FSINCOS, MVT::f64, Custom);
    setOperationAction(ISD::FSINCOS, MVT::f32, Custom);
  }

  // FP-ARMv8 implements a lot of rounding-like FP operations.
  if (Subtarget->hasFPARMv8Base()) {
    setOperationAction(ISD::FFLOOR, MVT::f32, Legal);
    setOperationAction(ISD::FCEIL, MVT::f32, Legal);
    setOperationAction(ISD::FROUND, MVT::f32, Legal);
    setOperationAction(ISD::FTRUNC, MVT::f32, Legal);
    setOperationAction(ISD::FNEARBYINT, MVT::f32, Legal);
    setOperationAction(ISD::FRINT, MVT::f32, Legal);
    setOperationAction(ISD::FMINNUM, MVT::f32, Legal);
    setOperationAction(ISD::FMAXNUM, MVT::f32, Legal);
    if (Subtarget->hasNEON()) {
      setOperationAction(ISD::FMINNUM, MVT::v2f32, Legal);
      setOperationAction(ISD::FMAXNUM, MVT::v2f32, Legal);
      setOperationAction(ISD::FMINNUM, MVT::v4f32, Legal);
      setOperationAction(ISD::FMAXNUM, MVT::v4f32, Legal);
    }

    if (Subtarget->hasFP64()) {
      setOperationAction(ISD::FFLOOR, MVT::f64, Legal);
      setOperationAction(ISD::FCEIL, MVT::f64, Legal);
      setOperationAction(ISD::FROUND, MVT::f64, Legal);
      setOperationAction(ISD::FTRUNC, MVT::f64, Legal);
      setOperationAction(ISD::FNEARBYINT, MVT::f64, Legal);
      setOperationAction(ISD::FRINT, MVT::f64, Legal);
      setOperationAction(ISD::FMINNUM, MVT::f64, Legal);
      setOperationAction(ISD::FMAXNUM, MVT::f64, Legal);
    }
  }

  // FP16 often need to be promoted to call lib functions
  if (Subtarget->hasFullFP16()) {
    setOperationAction(ISD::FREM, MVT::f16, Promote);
    setOperationAction(ISD::FCOPYSIGN, MVT::f16, Expand);
    setOperationAction(ISD::FSIN, MVT::f16, Promote);
    setOperationAction(ISD::FCOS, MVT::f16, Promote);
    setOperationAction(ISD::FTAN, MVT::f16, Promote);
    setOperationAction(ISD::FSINCOS, MVT::f16, Promote);
    setOperationAction(ISD::FPOWI, MVT::f16, Promote);
    setOperationAction(ISD::FPOW, MVT::f16, Promote);
    setOperationAction(ISD::FEXP, MVT::f16, Promote);
    setOperationAction(ISD::FEXP2, MVT::f16, Promote);
    setOperationAction(ISD::FEXP10, MVT::f16, Promote);
    setOperationAction(ISD::FLOG, MVT::f16, Promote);
    setOperationAction(ISD::FLOG10, MVT::f16, Promote);
    setOperationAction(ISD::FLOG2, MVT::f16, Promote);

    setOperationAction(ISD::FROUND, MVT::f16, Legal);
  }

  if (Subtarget->hasNEON()) {
    // vmin and vmax aren't available in a scalar form, so we can use
    // a NEON instruction with an undef lane instead.
    setOperationAction(ISD::FMINIMUM, MVT::f32, Legal);
    setOperationAction(ISD::FMAXIMUM, MVT::f32, Legal);
    setOperationAction(ISD::FMINIMUM, MVT::f16, Legal);
    setOperationAction(ISD::FMAXIMUM, MVT::f16, Legal);
    setOperationAction(ISD::FMINIMUM, MVT::v2f32, Legal);
    setOperationAction(ISD::FMAXIMUM, MVT::v2f32, Legal);
    setOperationAction(ISD::FMINIMUM, MVT::v4f32, Legal);
    setOperationAction(ISD::FMAXIMUM, MVT::v4f32, Legal);

    if (Subtarget->hasFullFP16()) {
      setOperationAction(ISD::FMINNUM, MVT::v4f16, Legal);
      setOperationAction(ISD::FMAXNUM, MVT::v4f16, Legal);
      setOperationAction(ISD::FMINNUM, MVT::v8f16, Legal);
      setOperationAction(ISD::FMAXNUM, MVT::v8f16, Legal);

      setOperationAction(ISD::FMINIMUM, MVT::v4f16, Legal);
      setOperationAction(ISD::FMAXIMUM, MVT::v4f16, Legal);
      setOperationAction(ISD::FMINIMUM, MVT::v8f16, Legal);
      setOperationAction(ISD::FMAXIMUM, MVT::v8f16, Legal);
    }
  }

  // On MSVC, both 32-bit and 64-bit, ldexpf(f32) is not defined.  MinGW has
  // it, but it's just a wrapper around ldexp.
  if (Subtarget->isTargetWindows()) {
    for (ISD::NodeType Op : {ISD::FLDEXP, ISD::STRICT_FLDEXP, ISD::FFREXP})
      if (isOperationExpand(Op, MVT::f32))
        setOperationAction(Op, MVT::f32, Promote);
  }

  // LegalizeDAG currently can't expand fp16 LDEXP/FREXP on targets where i16
  // isn't legal.
  for (ISD::NodeType Op : {ISD::FLDEXP, ISD::STRICT_FLDEXP, ISD::FFREXP})
    if (isOperationExpand(Op, MVT::f16))
      setOperationAction(Op, MVT::f16, Promote);

  // We have target-specific dag combine patterns for the following nodes:
  // ARMISD::VMOVRRD  - No need to call setTargetDAGCombine
  setTargetDAGCombine(
      {ISD::ADD, ISD::SUB, ISD::MUL, ISD::AND, ISD::OR, ISD::XOR});

  if (Subtarget->hasMVEIntegerOps())
    setTargetDAGCombine(ISD::VSELECT);

  if (Subtarget->hasV6Ops())
    setTargetDAGCombine(ISD::SRL);
  if (Subtarget->isThumb1Only())
    setTargetDAGCombine(ISD::SHL);
  // Attempt to lower smin/smax to ssat/usat
  if ((!Subtarget->isThumb() && Subtarget->hasV6Ops()) ||
      Subtarget->isThumb2()) {
    setTargetDAGCombine({ISD::SMIN, ISD::SMAX});
  }

  setStackPointerRegisterToSaveRestore(ARM::SP);

  if (Subtarget->useSoftFloat() || Subtarget->isThumb1Only() ||
      !Subtarget->hasVFP2Base() || Subtarget->hasMinSize())
    setSchedulingPreference(Sched::RegPressure);
  else
    setSchedulingPreference(Sched::Hybrid);

  //// temporary - rewrite interface to use type
  MaxStoresPerMemset = 8;
  MaxStoresPerMemsetOptSize = 4;
  MaxStoresPerMemcpy = 4; // For @llvm.memcpy -> sequence of stores
  MaxStoresPerMemcpyOptSize = 2;
  MaxStoresPerMemmove = 4; // For @llvm.memmove -> sequence of stores
  MaxStoresPerMemmoveOptSize = 2;

  // On ARM arguments smaller than 4 bytes are extended, so all arguments
  // are at least 4 bytes aligned.
  setMinStackArgumentAlignment(Align(4));

  // Prefer likely predicted branches to selects on out-of-order cores.
  PredictableSelectIsExpensive = Subtarget->getSchedModel().isOutOfOrder();

  setPrefLoopAlignment(Align(1ULL << Subtarget->getPrefLoopLogAlignment()));
  setPrefFunctionAlignment(Align(1ULL << Subtarget->getPrefLoopLogAlignment()));

  setMinFunctionAlignment(Subtarget->isThumb() ? Align(2) : Align(4));
}

bool ARMTargetLowering::useSoftFloat() const {
  return Subtarget->useSoftFloat();
}

// FIXME: It might make sense to define the representative register class as the
// nearest super-register that has a non-null superset. For example, DPR_VFP2 is
// a super-register of SPR, and DPR is a superset if DPR_VFP2. Consequently,
// SPR's representative would be DPR_VFP2. This should work well if register
// pressure tracking were modified such that a register use would increment the
// pressure of the register class's representative and all of it's super
// classes' representatives transitively. We have not implemented this because
// of the difficulty prior to coalescing of modeling operand register classes
// due to the common occurrence of cross class copies and subregister insertions
// and extractions.
std::pair<const TargetRegisterClass *, uint8_t>
ARMTargetLowering::findRepresentativeClass(const TargetRegisterInfo *TRI,
                                           MVT VT) const {
  const TargetRegisterClass *RRC = nullptr;
  uint8_t Cost = 1;
  switch (VT.SimpleTy) {
  default:
    return TargetLowering::findRepresentativeClass(TRI, VT);
  // Use DPR as representative register class for all floating point
  // and vector types. Since there are 32 SPR registers and 32 DPR registers so
  // the cost is 1 for both f32 and f64.
  case MVT::f32: case MVT::f64: case MVT::v8i8: case MVT::v4i16:
  case MVT::v2i32: case MVT::v1i64: case MVT::v2f32:
    RRC = &ARM::DPRRegClass;
    // When NEON is used for SP, only half of the register file is available
    // because operations that define both SP and DP results will be constrained
    // to the VFP2 class (D0-D15). We currently model this constraint prior to
    // coalescing by double-counting the SP regs. See the FIXME above.
    if (Subtarget->useNEONForSinglePrecisionFP())
      Cost = 2;
    break;
  case MVT::v16i8: case MVT::v8i16: case MVT::v4i32: case MVT::v2i64:
  case MVT::v4f32: case MVT::v2f64:
    RRC = &ARM::DPRRegClass;
    Cost = 2;
    break;
  case MVT::v4i64:
    RRC = &ARM::DPRRegClass;
    Cost = 4;
    break;
  case MVT::v8i64:
    RRC = &ARM::DPRRegClass;
    Cost = 8;
    break;
  }
  return std::make_pair(RRC, Cost);
}

const char *ARMTargetLowering::getTargetNodeName(unsigned Opcode) const {
#define MAKE_CASE(V)                                                           \
  case V:                                                                      \
    return #V;
  switch ((ARMISD::NodeType)Opcode) {
  case ARMISD::FIRST_NUMBER:
    break;
    MAKE_CASE(ARMISD::Wrapper)
    MAKE_CASE(ARMISD::WrapperPIC)
    MAKE_CASE(ARMISD::WrapperJT)
    MAKE_CASE(ARMISD::COPY_STRUCT_BYVAL)
    MAKE_CASE(ARMISD::CALL)
    MAKE_CASE(ARMISD::CALL_PRED)
    MAKE_CASE(ARMISD::CALL_NOLINK)
    MAKE_CASE(ARMISD::tSECALL)
    MAKE_CASE(ARMISD::t2CALL_BTI)
    MAKE_CASE(ARMISD::BRCOND)
    MAKE_CASE(ARMISD::BR_JT)
    MAKE_CASE(ARMISD::BR2_JT)
    MAKE_CASE(ARMISD::RET_GLUE)
    MAKE_CASE(ARMISD::SERET_GLUE)
    MAKE_CASE(ARMISD::INTRET_GLUE)
    MAKE_CASE(ARMISD::PIC_ADD)
    MAKE_CASE(ARMISD::CMP)
    MAKE_CASE(ARMISD::CMN)
    MAKE_CASE(ARMISD::CMPZ)
    MAKE_CASE(ARMISD::CMPFP)
    MAKE_CASE(ARMISD::CMPFPE)
    MAKE_CASE(ARMISD::CMPFPw0)
    MAKE_CASE(ARMISD::CMPFPEw0)
    MAKE_CASE(ARMISD::BCC_i64)
    MAKE_CASE(ARMISD::FMSTAT)
    MAKE_CASE(ARMISD::CMOV)
    MAKE_CASE(ARMISD::SSAT)
    MAKE_CASE(ARMISD::USAT)
    MAKE_CASE(ARMISD::ASRL)
    MAKE_CASE(ARMISD::LSRL)
    MAKE_CASE(ARMISD::LSLL)
    MAKE_CASE(ARMISD::SRL_GLUE)
    MAKE_CASE(ARMISD::SRA_GLUE)
    MAKE_CASE(ARMISD::RRX)
    MAKE_CASE(ARMISD::ADDC)
    MAKE_CASE(ARMISD::ADDE)
    MAKE_CASE(ARMISD::SUBC)
    MAKE_CASE(ARMISD::SUBE)
    MAKE_CASE(ARMISD::LSLS)
    MAKE_CASE(ARMISD::VMOVRRD)
    MAKE_CASE(ARMISD::VMOVDRR)
    MAKE_CASE(ARMISD::VMOVhr)
    MAKE_CASE(ARMISD::VMOVrh)
    MAKE_CASE(ARMISD::VMOVSR)
    MAKE_CASE(ARMISD::EH_SJLJ_SETJMP)
    MAKE_CASE(ARMISD::EH_SJLJ_LONGJMP)
    MAKE_CASE(ARMISD::EH_SJLJ_SETUP_DISPATCH)
    MAKE_CASE(ARMISD::TC_RETURN)
    MAKE_CASE(ARMISD::THREAD_POINTER)
    MAKE_CASE(ARMISD::DYN_ALLOC)
    MAKE_CASE(ARMISD::MEMBARRIER_MCR)
    MAKE_CASE(ARMISD::PRELOAD)
    MAKE_CASE(ARMISD::LDRD)
    MAKE_CASE(ARMISD::STRD)
    MAKE_CASE(ARMISD::WIN__CHKSTK)
    MAKE_CASE(ARMISD::WIN__DBZCHK)
    MAKE_CASE(ARMISD::PREDICATE_CAST)
    MAKE_CASE(ARMISD::VECTOR_REG_CAST)
    MAKE_CASE(ARMISD::MVESEXT)
    MAKE_CASE(ARMISD::MVEZEXT)
    MAKE_CASE(ARMISD::MVETRUNC)
    MAKE_CASE(ARMISD::VCMP)
    MAKE_CASE(ARMISD::VCMPZ)
    MAKE_CASE(ARMISD::VTST)
    MAKE_CASE(ARMISD::VSHLs)
    MAKE_CASE(ARMISD::VSHLu)
    MAKE_CASE(ARMISD::VSHLIMM)
    MAKE_CASE(ARMISD::VSHRsIMM)
    MAKE_CASE(ARMISD::VSHRuIMM)
    MAKE_CASE(ARMISD::VRSHRsIMM)
    MAKE_CASE(ARMISD::VRSHRuIMM)
    MAKE_CASE(ARMISD::VRSHRNIMM)
    MAKE_CASE(ARMISD::VQSHLsIMM)
    MAKE_CASE(ARMISD::VQSHLuIMM)
    MAKE_CASE(ARMISD::VQSHLsuIMM)
    MAKE_CASE(ARMISD::VQSHRNsIMM)
    MAKE_CASE(ARMISD::VQSHRNuIMM)
    MAKE_CASE(ARMISD::VQSHRNsuIMM)
    MAKE_CASE(ARMISD::VQRSHRNsIMM)
    MAKE_CASE(ARMISD::VQRSHRNuIMM)
    MAKE_CASE(ARMISD::VQRSHRNsuIMM)
    MAKE_CASE(ARMISD::VSLIIMM)
    MAKE_CASE(ARMISD::VSRIIMM)
    MAKE_CASE(ARMISD::VGETLANEu)
    MAKE_CASE(ARMISD::VGETLANEs)
    MAKE_CASE(ARMISD::VMOVIMM)
    MAKE_CASE(ARMISD::VMVNIMM)
    MAKE_CASE(ARMISD::VMOVFPIMM)
    MAKE_CASE(ARMISD::VDUP)
    MAKE_CASE(ARMISD::VDUPLANE)
    MAKE_CASE(ARMISD::VEXT)
    MAKE_CASE(ARMISD::VREV64)
    MAKE_CASE(ARMISD::VREV32)
    MAKE_CASE(ARMISD::VREV16)
    MAKE_CASE(ARMISD::VZIP)
    MAKE_CASE(ARMISD::VUZP)
    MAKE_CASE(ARMISD::VTRN)
    MAKE_CASE(ARMISD::VTBL1)
    MAKE_CASE(ARMISD::VTBL2)
    MAKE_CASE(ARMISD::VMOVN)
    MAKE_CASE(ARMISD::VQMOVNs)
    MAKE_CASE(ARMISD::VQMOVNu)
    MAKE_CASE(ARMISD::VCVTN)
    MAKE_CASE(ARMISD::VCVTL)
    MAKE_CASE(ARMISD::VIDUP)
    MAKE_CASE(ARMISD::VMULLs)
    MAKE_CASE(ARMISD::VMULLu)
    MAKE_CASE(ARMISD::VQDMULH)
    MAKE_CASE(ARMISD::VADDVs)
    MAKE_CASE(ARMISD::VADDVu)
    MAKE_CASE(ARMISD::VADDVps)
    MAKE_CASE(ARMISD::VADDVpu)
    MAKE_CASE(ARMISD::VADDLVs)
    MAKE_CASE(ARMISD::VADDLVu)
    MAKE_CASE(ARMISD::VADDLVAs)
    MAKE_CASE(ARMISD::VADDLVAu)
    MAKE_CASE(ARMISD::VADDLVps)
    MAKE_CASE(ARMISD::VADDLVpu)
    MAKE_CASE(ARMISD::VADDLVAps)
    MAKE_CASE(ARMISD::VADDLVApu)
    MAKE_CASE(ARMISD::VMLAVs)
    MAKE_CASE(ARMISD::VMLAVu)
    MAKE_CASE(ARMISD::VMLAVps)
    MAKE_CASE(ARMISD::VMLAVpu)
    MAKE_CASE(ARMISD::VMLALVs)
    MAKE_CASE(ARMISD::VMLALVu)
    MAKE_CASE(ARMISD::VMLALVps)
    MAKE_CASE(ARMISD::VMLALVpu)
    MAKE_CASE(ARMISD::VMLALVAs)
    MAKE_CASE(ARMISD::VMLALVAu)
    MAKE_CASE(ARMISD::VMLALVAps)
    MAKE_CASE(ARMISD::VMLALVApu)
    MAKE_CASE(ARMISD::VMINVu)
    MAKE_CASE(ARMISD::VMINVs)
    MAKE_CASE(ARMISD::VMAXVu)
    MAKE_CASE(ARMISD::VMAXVs)
    MAKE_CASE(ARMISD::UMAAL)
    MAKE_CASE(ARMISD::UMLAL)
    MAKE_CASE(ARMISD::SMLAL)
    MAKE_CASE(ARMISD::SMLALBB)
    MAKE_CASE(ARMISD::SMLALBT)
    MAKE_CASE(ARMISD::SMLALTB)
    MAKE_CASE(ARMISD::SMLALTT)
    MAKE_CASE(ARMISD::SMULWB)
    MAKE_CASE(ARMISD::SMULWT)
    MAKE_CASE(ARMISD::SMLALD)
    MAKE_CASE(ARMISD::SMLALDX)
    MAKE_CASE(ARMISD::SMLSLD)
    MAKE_CASE(ARMISD::SMLSLDX)
    MAKE_CASE(ARMISD::SMMLAR)
    MAKE_CASE(ARMISD::SMMLSR)
    MAKE_CASE(ARMISD::QADD16b)
    MAKE_CASE(ARMISD::QSUB16b)
    MAKE_CASE(ARMISD::QADD8b)
    MAKE_CASE(ARMISD::QSUB8b)
    MAKE_CASE(ARMISD::UQADD16b)
    MAKE_CASE(ARMISD::UQSUB16b)
    MAKE_CASE(ARMISD::UQADD8b)
    MAKE_CASE(ARMISD::UQSUB8b)
    MAKE_CASE(ARMISD::BUILD_VECTOR)
    MAKE_CASE(ARMISD::BFI)
    MAKE_CASE(ARMISD::VORRIMM)
    MAKE_CASE(ARMISD::VBICIMM)
    MAKE_CASE(ARMISD::VBSP)
    MAKE_CASE(ARMISD::MEMCPY)
    MAKE_CASE(ARMISD::VLD1DUP)
    MAKE_CASE(ARMISD::VLD2DUP)
    MAKE_CASE(ARMISD::VLD3DUP)
    MAKE_CASE(ARMISD::VLD4DUP)
    MAKE_CASE(ARMISD::VLD1_UPD)
    MAKE_CASE(ARMISD::VLD2_UPD)
    MAKE_CASE(ARMISD::VLD3_UPD)
    MAKE_CASE(ARMISD::VLD4_UPD)
    MAKE_CASE(ARMISD::VLD1x2_UPD)
    MAKE_CASE(ARMISD::VLD1x3_UPD)
    MAKE_CASE(ARMISD::VLD1x4_UPD)
    MAKE_CASE(ARMISD::VLD2LN_UPD)
    MAKE_CASE(ARMISD::VLD3LN_UPD)
    MAKE_CASE(ARMISD::VLD4LN_UPD)
    MAKE_CASE(ARMISD::VLD1DUP_UPD)
    MAKE_CASE(ARMISD::VLD2DUP_UPD)
    MAKE_CASE(ARMISD::VLD3DUP_UPD)
    MAKE_CASE(ARMISD::VLD4DUP_UPD)
    MAKE_CASE(ARMISD::VST1_UPD)
    MAKE_CASE(ARMISD::VST2_UPD)
    MAKE_CASE(ARMISD::VST3_UPD)
    MAKE_CASE(ARMISD::VST4_UPD)
    MAKE_CASE(ARMISD::VST1x2_UPD)
    MAKE_CASE(ARMISD::VST1x3_UPD)
    MAKE_CASE(ARMISD::VST1x4_UPD)
    MAKE_CASE(ARMISD::VST2LN_UPD)
    MAKE_CASE(ARMISD::VST3LN_UPD)
    MAKE_CASE(ARMISD::VST4LN_UPD)
    MAKE_CASE(ARMISD::WLS)
    MAKE_CASE(ARMISD::WLSSETUP)
    MAKE_CASE(ARMISD::LE)
    MAKE_CASE(ARMISD::LOOP_DEC)
    MAKE_CASE(ARMISD::CSINV)
    MAKE_CASE(ARMISD::CSNEG)
    MAKE_CASE(ARMISD::CSINC)
    MAKE_CASE(ARMISD::MEMCPYLOOP)
    MAKE_CASE(ARMISD::MEMSETLOOP)
#undef MAKE_CASE
  }
  return nullptr;
}

EVT ARMTargetLowering::getSetCCResultType(const DataLayout &DL, LLVMContext &,
                                          EVT VT) const {
  if (!VT.isVector())
    return getPointerTy(DL);

  // MVE has a predicate register.
  if ((Subtarget->hasMVEIntegerOps() &&
       (VT == MVT::v2i64 || VT == MVT::v4i32 || VT == MVT::v8i16 ||
        VT == MVT::v16i8)) ||
      (Subtarget->hasMVEFloatOps() &&
       (VT == MVT::v2f64 || VT == MVT::v4f32 || VT == MVT::v8f16)))
    return MVT::getVectorVT(MVT::i1, VT.getVectorElementCount());
  return VT.changeVectorElementTypeToInteger();
}

/// getRegClassFor - Return the register class that should be used for the
/// specified value type.
const TargetRegisterClass *
ARMTargetLowering::getRegClassFor(MVT VT, bool isDivergent) const {
  (void)isDivergent;
  // Map v4i64 to QQ registers but do not make the type legal. Similarly map
  // v8i64 to QQQQ registers. v4i64 and v8i64 are only used for REG_SEQUENCE to
  // load / store 4 to 8 consecutive NEON D registers, or 2 to 4 consecutive
  // MVE Q registers.
  if (Subtarget->hasNEON()) {
    if (VT == MVT::v4i64)
      return &ARM::QQPRRegClass;
    if (VT == MVT::v8i64)
      return &ARM::QQQQPRRegClass;
  }
  if (Subtarget->hasMVEIntegerOps()) {
    if (VT == MVT::v4i64)
      return &ARM::MQQPRRegClass;
    if (VT == MVT::v8i64)
      return &ARM::MQQQQPRRegClass;
  }
  return TargetLowering::getRegClassFor(VT);
}

// memcpy, and other memory intrinsics, typically tries to use LDM/STM if the
// source/dest is aligned and the copy size is large enough. We therefore want
// to align such objects passed to memory intrinsics.
bool ARMTargetLowering::shouldAlignPointerArgs(CallInst *CI, unsigned &MinSize,
                                               Align &PrefAlign) const {
  if (!isa<MemIntrinsic>(CI))
    return false;
  MinSize = 8;
  // On ARM11 onwards (excluding M class) 8-byte aligned LDM is typically 1
  // cycle faster than 4-byte aligned LDM.
  PrefAlign =
      (Subtarget->hasV6Ops() && !Subtarget->isMClass() ? Align(8) : Align(4));
  return true;
}

// Create a fast isel object.
FastISel *
ARMTargetLowering::createFastISel(FunctionLoweringInfo &funcInfo,
                                  const TargetLibraryInfo *libInfo) const {
  return ARM::createFastISel(funcInfo, libInfo);
}

Sched::Preference ARMTargetLowering::getSchedulingPreference(SDNode *N) const {
  unsigned NumVals = N->getNumValues();
  if (!NumVals)
    return Sched::RegPressure;

  for (unsigned i = 0; i != NumVals; ++i) {
    EVT VT = N->getValueType(i);
    if (VT == MVT::Glue || VT == MVT::Other)
      continue;
    if (VT.isFloatingPoint() || VT.isVector())
      return Sched::ILP;
  }

  if (!N->isMachineOpcode())
    return Sched::RegPressure;

  // Load are scheduled for latency even if there instruction itinerary
  // is not available.
  const TargetInstrInfo *TII = Subtarget->getInstrInfo();
  const MCInstrDesc &MCID = TII->get(N->getMachineOpcode());

  if (MCID.getNumDefs() == 0)
    return Sched::RegPressure;
  if (!Itins->isEmpty() &&
      Itins->getOperandCycle(MCID.getSchedClass(), 0) > 2U)
    return Sched::ILP;

  return Sched::RegPressure;
}

//===----------------------------------------------------------------------===//
// Lowering Code
//===----------------------------------------------------------------------===//

static bool isSRL16(const SDValue &Op) {
  if (Op.getOpcode() != ISD::SRL)
    return false;
  if (auto Const = dyn_cast<ConstantSDNode>(Op.getOperand(1)))
    return Const->getZExtValue() == 16;
  return false;
}

static bool isSRA16(const SDValue &Op) {
  if (Op.getOpcode() != ISD::SRA)
    return false;
  if (auto Const = dyn_cast<ConstantSDNode>(Op.getOperand(1)))
    return Const->getZExtValue() == 16;
  return false;
}

static bool isSHL16(const SDValue &Op) {
  if (Op.getOpcode() != ISD::SHL)
    return false;
  if (auto Const = dyn_cast<ConstantSDNode>(Op.getOperand(1)))
    return Const->getZExtValue() == 16;
  return false;
}

// Check for a signed 16-bit value. We special case SRA because it makes it
// more simple when also looking for SRAs that aren't sign extending a
// smaller value. Without the check, we'd need to take extra care with
// checking order for some operations.
static bool isS16(const SDValue &Op, SelectionDAG &DAG) {
  if (isSRA16(Op))
    return isSHL16(Op.getOperand(0));
  return DAG.ComputeNumSignBits(Op) == 17;
}

/// IntCCToARMCC - Convert a DAG integer condition code to an ARM CC
static ARMCC::CondCodes IntCCToARMCC(ISD::CondCode CC) {
  switch (CC) {
  default: llvm_unreachable("Unknown condition code!");
  case ISD::SETNE:  return ARMCC::NE;
  case ISD::SETEQ:  return ARMCC::EQ;
  case ISD::SETGT:  return ARMCC::GT;
  case ISD::SETGE:  return ARMCC::GE;
  case ISD::SETLT:  return ARMCC::LT;
  case ISD::SETLE:  return ARMCC::LE;
  case ISD::SETUGT: return ARMCC::HI;
  case ISD::SETUGE: return ARMCC::HS;
  case ISD::SETULT: return ARMCC::LO;
  case ISD::SETULE: return ARMCC::LS;
  }
}

/// FPCCToARMCC - Convert a DAG fp condition code to an ARM CC.
static void FPCCToARMCC(ISD::CondCode CC, ARMCC::CondCodes &CondCode,
                        ARMCC::CondCodes &CondCode2) {
  CondCode2 = ARMCC::AL;
  switch (CC) {
  default: llvm_unreachable("Unknown FP condition!");
  case ISD::SETEQ:
  case ISD::SETOEQ: CondCode = ARMCC::EQ; break;
  case ISD::SETGT:
  case ISD::SETOGT: CondCode = ARMCC::GT; break;
  case ISD::SETGE:
  case ISD::SETOGE: CondCode = ARMCC::GE; break;
  case ISD::SETOLT: CondCode = ARMCC::MI; break;
  case ISD::SETOLE: CondCode = ARMCC::LS; break;
  case ISD::SETONE: CondCode = ARMCC::MI; CondCode2 = ARMCC::GT; break;
  case ISD::SETO:   CondCode = ARMCC::VC; break;
  case ISD::SETUO:  CondCode = ARMCC::VS; break;
  case ISD::SETUEQ: CondCode = ARMCC::EQ; CondCode2 = ARMCC::VS; break;
  case ISD::SETUGT: CondCode = ARMCC::HI; break;
  case ISD::SETUGE: CondCode = ARMCC::PL; break;
  case ISD::SETLT:
  case ISD::SETULT: CondCode = ARMCC::LT; break;
  case ISD::SETLE:
  case ISD::SETULE: CondCode = ARMCC::LE; break;
  case ISD::SETNE:
  case ISD::SETUNE: CondCode = ARMCC::NE; break;
  }
}

//===----------------------------------------------------------------------===//
//                      Calling Convention Implementation
//===----------------------------------------------------------------------===//

/// getEffectiveCallingConv - Get the effective calling convention, taking into
/// account presence of floating point hardware and calling convention
/// limitations, such as support for variadic functions.
CallingConv::ID
ARMTargetLowering::getEffectiveCallingConv(CallingConv::ID CC,
                                           bool isVarArg) const {
  switch (CC) {
  default:
    report_fatal_error("Unsupported calling convention");
  case CallingConv::ARM_AAPCS:
  case CallingConv::ARM_APCS:
  case CallingConv::GHC:
  case CallingConv::CFGuard_Check:
    return CC;
  case CallingConv::PreserveMost:
    return CallingConv::PreserveMost;
  case CallingConv::PreserveAll:
    return CallingConv::PreserveAll;
  case CallingConv::ARM_AAPCS_VFP:
  case CallingConv::Swift:
  case CallingConv::SwiftTail:
    return isVarArg ? CallingConv::ARM_AAPCS : CallingConv::ARM_AAPCS_VFP;
  case CallingConv::C:
  case CallingConv::Tail:
    if (!Subtarget->isAAPCS_ABI())
      return CallingConv::ARM_APCS;
    else if (Subtarget->hasFPRegs() && !Subtarget->isThumb1Only() &&
             getTargetMachine().Options.FloatABIType == FloatABI::Hard &&
             !isVarArg)
      return CallingConv::ARM_AAPCS_VFP;
    else
      return CallingConv::ARM_AAPCS;
  case CallingConv::Fast:
  case CallingConv::CXX_FAST_TLS:
    if (!Subtarget->isAAPCS_ABI()) {
      if (Subtarget->hasVFP2Base() && !Subtarget->isThumb1Only() && !isVarArg)
        return CallingConv::Fast;
      return CallingConv::ARM_APCS;
    } else if (Subtarget->hasVFP2Base() &&
               !Subtarget->isThumb1Only() && !isVarArg)
      return CallingConv::ARM_AAPCS_VFP;
    else
      return CallingConv::ARM_AAPCS;
  }
}

CCAssignFn *ARMTargetLowering::CCAssignFnForCall(CallingConv::ID CC,
                                                 bool isVarArg) const {
  return CCAssignFnForNode(CC, false, isVarArg);
}

CCAssignFn *ARMTargetLowering::CCAssignFnForReturn(CallingConv::ID CC,
                                                   bool isVarArg) const {
  return CCAssignFnForNode(CC, true, isVarArg);
}

/// CCAssignFnForNode - Selects the correct CCAssignFn for the given
/// CallingConvention.
CCAssignFn *ARMTargetLowering::CCAssignFnForNode(CallingConv::ID CC,
                                                 bool Return,
                                                 bool isVarArg) const {
  switch (getEffectiveCallingConv(CC, isVarArg)) {
  default:
    report_fatal_error("Unsupported calling convention");
  case CallingConv::ARM_APCS:
    return (Return ? RetCC_ARM_APCS : CC_ARM_APCS);
  case CallingConv::ARM_AAPCS:
    return (Return ? RetCC_ARM_AAPCS : CC_ARM_AAPCS);
  case CallingConv::ARM_AAPCS_VFP:
    return (Return ? RetCC_ARM_AAPCS_VFP : CC_ARM_AAPCS_VFP);
  case CallingConv::Fast:
    return (Return ? RetFastCC_ARM_APCS : FastCC_ARM_APCS);
  case CallingConv::GHC:
    return (Return ? RetCC_ARM_APCS : CC_ARM_APCS_GHC);
  case CallingConv::PreserveMost:
    return (Return ? RetCC_ARM_AAPCS : CC_ARM_AAPCS);
  case CallingConv::PreserveAll:
    return (Return ? RetCC_ARM_AAPCS : CC_ARM_AAPCS);
  case CallingConv::CFGuard_Check:
    return (Return ? RetCC_ARM_AAPCS : CC_ARM_Win32_CFGuard_Check);
  }
}

SDValue ARMTargetLowering::MoveToHPR(const SDLoc &dl, SelectionDAG &DAG,
                                     MVT LocVT, MVT ValVT, SDValue Val) const {
  Val = DAG.getNode(ISD::BITCAST, dl, MVT::getIntegerVT(LocVT.getSizeInBits()),
                    Val);
  if (Subtarget->hasFullFP16()) {
    Val = DAG.getNode(ARMISD::VMOVhr, dl, ValVT, Val);
  } else {
    Val = DAG.getNode(ISD::TRUNCATE, dl,
                      MVT::getIntegerVT(ValVT.getSizeInBits()), Val);
    Val = DAG.getNode(ISD::BITCAST, dl, ValVT, Val);
  }
  return Val;
}

SDValue ARMTargetLowering::MoveFromHPR(const SDLoc &dl, SelectionDAG &DAG,
                                       MVT LocVT, MVT ValVT,
                                       SDValue Val) const {
  if (Subtarget->hasFullFP16()) {
    Val = DAG.getNode(ARMISD::VMOVrh, dl,
                      MVT::getIntegerVT(LocVT.getSizeInBits()), Val);
  } else {
    Val = DAG.getNode(ISD::BITCAST, dl,
                      MVT::getIntegerVT(ValVT.getSizeInBits()), Val);
    Val = DAG.getNode(ISD::ZERO_EXTEND, dl,
                      MVT::getIntegerVT(LocVT.getSizeInBits()), Val);
  }
  return DAG.getNode(ISD::BITCAST, dl, LocVT, Val);
}

/// LowerCallResult - Lower the result values of a call into the
/// appropriate copies out of appropriate physical registers.
SDValue ARMTargetLowering::LowerCallResult(
    SDValue Chain, SDValue InGlue, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals, bool isThisReturn,
    SDValue ThisVal, bool isCmseNSCall) const {
  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeCallResult(Ins, CCAssignFnForReturn(CallConv, isVarArg));

  // Copy all of the result registers out of their specified physreg.
  for (unsigned i = 0; i != RVLocs.size(); ++i) {
    CCValAssign VA = RVLocs[i];

    // Pass 'this' value directly from the argument to return value, to avoid
    // reg unit interference
    if (i == 0 && isThisReturn) {
      assert(!VA.needsCustom() && VA.getLocVT() == MVT::i32 &&
             "unexpected return calling convention register assignment");
      InVals.push_back(ThisVal);
      continue;
    }

    SDValue Val;
    if (VA.needsCustom() &&
        (VA.getLocVT() == MVT::f64 || VA.getLocVT() == MVT::v2f64)) {
      // Handle f64 or half of a v2f64.
      SDValue Lo = DAG.getCopyFromReg(Chain, dl, VA.getLocReg(), MVT::i32,
                                      InGlue);
      Chain = Lo.getValue(1);
      InGlue = Lo.getValue(2);
      VA = RVLocs[++i]; // skip ahead to next loc
      SDValue Hi = DAG.getCopyFromReg(Chain, dl, VA.getLocReg(), MVT::i32,
                                      InGlue);
      Chain = Hi.getValue(1);
      InGlue = Hi.getValue(2);
      if (!Subtarget->isLittle())
        std::swap (Lo, Hi);
      Val = DAG.getNode(ARMISD::VMOVDRR, dl, MVT::f64, Lo, Hi);

      if (VA.getLocVT() == MVT::v2f64) {
        SDValue Vec = DAG.getNode(ISD::UNDEF, dl, MVT::v2f64);
        Vec = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, MVT::v2f64, Vec, Val,
                          DAG.getConstant(0, dl, MVT::i32));

        VA = RVLocs[++i]; // skip ahead to next loc
        Lo = DAG.getCopyFromReg(Chain, dl, VA.getLocReg(), MVT::i32, InGlue);
        Chain = Lo.getValue(1);
        InGlue = Lo.getValue(2);
        VA = RVLocs[++i]; // skip ahead to next loc
        Hi = DAG.getCopyFromReg(Chain, dl, VA.getLocReg(), MVT::i32, InGlue);
        Chain = Hi.getValue(1);
        InGlue = Hi.getValue(2);
        if (!Subtarget->isLittle())
          std::swap (Lo, Hi);
        Val = DAG.getNode(ARMISD::VMOVDRR, dl, MVT::f64, Lo, Hi);
        Val = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, MVT::v2f64, Vec, Val,
                          DAG.getConstant(1, dl, MVT::i32));
      }
    } else {
      Val = DAG.getCopyFromReg(Chain, dl, VA.getLocReg(), VA.getLocVT(),
                               InGlue);
      Chain = Val.getValue(1);
      InGlue = Val.getValue(2);
    }

    switch (VA.getLocInfo()) {
    default: llvm_unreachable("Unknown loc info!");
    case CCValAssign::Full: break;
    case CCValAssign::BCvt:
      Val = DAG.getNode(ISD::BITCAST, dl, VA.getValVT(), Val);
      break;
    }

    // f16 arguments have their size extended to 4 bytes and passed as if they
    // had been copied to the LSBs of a 32-bit register.
    // For that, it's passed extended to i32 (soft ABI) or to f32 (hard ABI)
    if (VA.needsCustom() &&
        (VA.getValVT() == MVT::f16 || VA.getValVT() == MVT::bf16))
      Val = MoveToHPR(dl, DAG, VA.getLocVT(), VA.getValVT(), Val);

    // On CMSE Non-secure Calls, call results (returned values) whose bitwidth
    // is less than 32 bits must be sign- or zero-extended after the call for
    // security reasons. Although the ABI mandates an extension done by the
    // callee, the latter cannot be trusted to follow the rules of the ABI.
    const ISD::InputArg &Arg = Ins[VA.getValNo()];
    if (isCmseNSCall && Arg.ArgVT.isScalarInteger() &&
        VA.getLocVT().isScalarInteger() && Arg.ArgVT.bitsLT(MVT::i32))
      Val = handleCMSEValue(Val, Arg, DAG, dl);

    InVals.push_back(Val);
  }

  return Chain;
}

std::pair<SDValue, MachinePointerInfo> ARMTargetLowering::computeAddrForCallArg(
    const SDLoc &dl, SelectionDAG &DAG, const CCValAssign &VA, SDValue StackPtr,
    bool IsTailCall, int SPDiff) const {
  SDValue DstAddr;
  MachinePointerInfo DstInfo;
  int32_t Offset = VA.getLocMemOffset();
  MachineFunction &MF = DAG.getMachineFunction();

  if (IsTailCall) {
        Offset += SPDiff;
        auto PtrVT = getPointerTy(DAG.getDataLayout());
        int Size = VA.getLocVT().getFixedSizeInBits() / 8;
        int FI = MF.getFrameInfo().CreateFixedObject(Size, Offset, true);
        DstAddr = DAG.getFrameIndex(FI, PtrVT);
        DstInfo =
            MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI);
  } else {
        SDValue PtrOff = DAG.getIntPtrConstant(Offset, dl);
        DstAddr = DAG.getNode(ISD::ADD, dl, getPointerTy(DAG.getDataLayout()),
                              StackPtr, PtrOff);
        DstInfo =
            MachinePointerInfo::getStack(DAG.getMachineFunction(), Offset);
  }

  return std::make_pair(DstAddr, DstInfo);
}

void ARMTargetLowering::PassF64ArgInRegs(const SDLoc &dl, SelectionDAG &DAG,
                                         SDValue Chain, SDValue &Arg,
                                         RegsToPassVector &RegsToPass,
                                         CCValAssign &VA, CCValAssign &NextVA,
                                         SDValue &StackPtr,
                                         SmallVectorImpl<SDValue> &MemOpChains,
                                         bool IsTailCall,
                                         int SPDiff) const {
  SDValue fmrrd = DAG.getNode(ARMISD::VMOVRRD, dl,
                              DAG.getVTList(MVT::i32, MVT::i32), Arg);
  unsigned id = Subtarget->isLittle() ? 0 : 1;
  RegsToPass.push_back(std::make_pair(VA.getLocReg(), fmrrd.getValue(id)));

  if (NextVA.isRegLoc())
    RegsToPass.push_back(std::make_pair(NextVA.getLocReg(), fmrrd.getValue(1-id)));
  else {
    assert(NextVA.isMemLoc());
    if (!StackPtr.getNode())
      StackPtr = DAG.getCopyFromReg(Chain, dl, ARM::SP,
                                    getPointerTy(DAG.getDataLayout()));

    SDValue DstAddr;
    MachinePointerInfo DstInfo;
    std::tie(DstAddr, DstInfo) =
        computeAddrForCallArg(dl, DAG, NextVA, StackPtr, IsTailCall, SPDiff);
    MemOpChains.push_back(
        DAG.getStore(Chain, dl, fmrrd.getValue(1 - id), DstAddr, DstInfo));
  }
}

static bool canGuaranteeTCO(CallingConv::ID CC, bool GuaranteeTailCalls) {
  return (CC == CallingConv::Fast && GuaranteeTailCalls) ||
         CC == CallingConv::Tail || CC == CallingConv::SwiftTail;
}

/// LowerCall - Lowering a call into a callseq_start <-
/// ARMISD:CALL <- callseq_end chain. Also add input and output parameter
/// nodes.
SDValue
ARMTargetLowering::LowerCall(TargetLowering::CallLoweringInfo &CLI,
                             SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG                     = CLI.DAG;
  SDLoc &dl                             = CLI.DL;
  SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  SmallVectorImpl<SDValue> &OutVals     = CLI.OutVals;
  SmallVectorImpl<ISD::InputArg> &Ins   = CLI.Ins;
  SDValue Chain                         = CLI.Chain;
  SDValue Callee                        = CLI.Callee;
  bool &isTailCall                      = CLI.IsTailCall;
  CallingConv::ID CallConv              = CLI.CallConv;
  bool doesNotRet                       = CLI.DoesNotReturn;
  bool isVarArg                         = CLI.IsVarArg;

  MachineFunction &MF = DAG.getMachineFunction();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  MachineFunction::CallSiteInfo CSInfo;
  bool isStructRet = (Outs.empty()) ? false : Outs[0].Flags.isSRet();
  bool isThisReturn = false;
  bool isCmseNSCall   = false;
  bool isSibCall = false;
  bool PreferIndirect = false;
  bool GuardWithBTI = false;

  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeCallOperands(Outs, CCAssignFnForCall(CallConv, isVarArg));

  // Lower 'returns_twice' calls to a pseudo-instruction.
  if (CLI.CB && CLI.CB->getAttributes().hasFnAttr(Attribute::ReturnsTwice) &&
      !Subtarget->noBTIAtReturnTwice())
    GuardWithBTI = AFI->branchTargetEnforcement();

  // Determine whether this is a non-secure function call.
  if (CLI.CB && CLI.CB->getAttributes().hasFnAttr("cmse_nonsecure_call"))
    isCmseNSCall = true;

  // Disable tail calls if they're not supported.
  if (!Subtarget->supportsTailCall())
    isTailCall = false;

  // For both the non-secure calls and the returns from a CMSE entry function,
  // the function needs to do some extra work afte r the call, or before the
  // return, respectively, thus it cannot end with atail call
  if (isCmseNSCall || AFI->isCmseNSEntryFunction())
    isTailCall = false;

  if (isa<GlobalAddressSDNode>(Callee)) {
    // If we're optimizing for minimum size and the function is called three or
    // more times in this block, we can improve codesize by calling indirectly
    // as BLXr has a 16-bit encoding.
    auto *GV = cast<GlobalAddressSDNode>(Callee)->getGlobal();
    if (CLI.CB) {
      auto *BB = CLI.CB->getParent();
      PreferIndirect = Subtarget->isThumb() && Subtarget->hasMinSize() &&
                       count_if(GV->users(), [&BB](const User *U) {
                         return isa<Instruction>(U) &&
                                cast<Instruction>(U)->getParent() == BB;
                       }) > 2;
    }
  }
  if (isTailCall) {
    // Check if it's really possible to do a tail call.
    isTailCall =
        IsEligibleForTailCallOptimization(CLI, CCInfo, ArgLocs, PreferIndirect);

    if (isTailCall && !getTargetMachine().Options.GuaranteedTailCallOpt &&
        CallConv != CallingConv::Tail && CallConv != CallingConv::SwiftTail)
      isSibCall = true;

    // We don't support GuaranteedTailCallOpt for ARM, only automatically
    // detected sibcalls.
    if (isTailCall)
      ++NumTailCalls;
  }

  if (!isTailCall && CLI.CB && CLI.CB->isMustTailCall())
    report_fatal_error("failed to perform tail call elimination on a call "
                       "site marked musttail");

  // Get a count of how many bytes are to be pushed on the stack.
  unsigned NumBytes = CCInfo.getStackSize();

  // SPDiff is the byte offset of the call's argument area from the callee's.
  // Stores to callee stack arguments will be placed in FixedStackSlots offset
  // by this amount for a tail call. In a sibling call it must be 0 because the
  // caller will deallocate the entire stack and the callee still expects its
  // arguments to begin at SP+0. Completely unused for non-tail calls.
  int SPDiff = 0;

  if (isTailCall && !isSibCall) {
    auto FuncInfo = MF.getInfo<ARMFunctionInfo>();
    unsigned NumReusableBytes = FuncInfo->getArgumentStackSize();

    // Since callee will pop argument stack as a tail call, we must keep the
    // popped size 16-byte aligned.
    Align StackAlign = DAG.getDataLayout().getStackAlignment();
    NumBytes = alignTo(NumBytes, StackAlign);

    // SPDiff will be negative if this tail call requires more space than we
    // would automatically have in our incoming argument space. Positive if we
    // can actually shrink the stack.
    SPDiff = NumReusableBytes - NumBytes;

    // If this call requires more stack than we have available from
    // LowerFormalArguments, tell FrameLowering to reserve space for it.
    if (SPDiff < 0 && AFI->getArgRegsSaveSize() < (unsigned)-SPDiff)
      AFI->setArgRegsSaveSize(-SPDiff);
  }

  if (isSibCall) {
    // For sibling tail calls, memory operands are available in our caller's stack.
    NumBytes = 0;
  } else {
    // Adjust the stack pointer for the new arguments...
    // These operations are automatically eliminated by the prolog/epilog pass
    Chain = DAG.getCALLSEQ_START(Chain, isTailCall ? 0 : NumBytes, 0, dl);
  }

  SDValue StackPtr =
      DAG.getCopyFromReg(Chain, dl, ARM::SP, getPointerTy(DAG.getDataLayout()));

  RegsToPassVector RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;

  // During a tail call, stores to the argument area must happen after all of
  // the function's incoming arguments have been loaded because they may alias.
  // This is done by folding in a TokenFactor from LowerFormalArguments, but
  // there's no point in doing so repeatedly so this tracks whether that's
  // happened yet.
  bool AfterFormalArgLoads = false;

  // Walk the register/memloc assignments, inserting copies/loads.  In the case
  // of tail call optimization, arguments are handled later.
  for (unsigned i = 0, realArgIdx = 0, e = ArgLocs.size();
       i != e;
       ++i, ++realArgIdx) {
    CCValAssign &VA = ArgLocs[i];
    SDValue Arg = OutVals[realArgIdx];
    ISD::ArgFlagsTy Flags = Outs[realArgIdx].Flags;
    bool isByVal = Flags.isByVal();

    // Promote the value if needed.
    switch (VA.getLocInfo()) {
    default: llvm_unreachable("Unknown loc info!");
    case CCValAssign::Full: break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, dl, VA.getLocVT(), Arg);
      break;
    case CCValAssign::BCvt:
      Arg = DAG.getNode(ISD::BITCAST, dl, VA.getLocVT(), Arg);
      break;
    }

    if (isTailCall && VA.isMemLoc() && !AfterFormalArgLoads) {
      Chain = DAG.getStackArgumentTokenFactor(Chain);
      AfterFormalArgLoads = true;
    }

    // f16 arguments have their size extended to 4 bytes and passed as if they
    // had been copied to the LSBs of a 32-bit register.
    // For that, it's passed extended to i32 (soft ABI) or to f32 (hard ABI)
    if (VA.needsCustom() &&
        (VA.getValVT() == MVT::f16 || VA.getValVT() == MVT::bf16)) {
      Arg = MoveFromHPR(dl, DAG, VA.getLocVT(), VA.getValVT(), Arg);
    } else {
      // f16 arguments could have been extended prior to argument lowering.
      // Mask them arguments if this is a CMSE nonsecure call.
      auto ArgVT = Outs[realArgIdx].ArgVT;
      if (isCmseNSCall && (ArgVT == MVT::f16)) {
        auto LocBits = VA.getLocVT().getSizeInBits();
        auto MaskValue = APInt::getLowBitsSet(LocBits, ArgVT.getSizeInBits());
        SDValue Mask =
            DAG.getConstant(MaskValue, dl, MVT::getIntegerVT(LocBits));
        Arg = DAG.getNode(ISD::BITCAST, dl, MVT::getIntegerVT(LocBits), Arg);
        Arg = DAG.getNode(ISD::AND, dl, MVT::getIntegerVT(LocBits), Arg, Mask);
        Arg = DAG.getNode(ISD::BITCAST, dl, VA.getLocVT(), Arg);
      }
    }

    // f64 and v2f64 might be passed in i32 pairs and must be split into pieces
    if (VA.needsCustom() && VA.getLocVT() == MVT::v2f64) {
      SDValue Op0 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::f64, Arg,
                                DAG.getConstant(0, dl, MVT::i32));
      SDValue Op1 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::f64, Arg,
                                DAG.getConstant(1, dl, MVT::i32));

      PassF64ArgInRegs(dl, DAG, Chain, Op0, RegsToPass, VA, ArgLocs[++i],
                       StackPtr, MemOpChains, isTailCall, SPDiff);

      VA = ArgLocs[++i]; // skip ahead to next loc
      if (VA.isRegLoc()) {
        PassF64ArgInRegs(dl, DAG, Chain, Op1, RegsToPass, VA, ArgLocs[++i],
                         StackPtr, MemOpChains, isTailCall, SPDiff);
      } else {
        assert(VA.isMemLoc());
        SDValue DstAddr;
        MachinePointerInfo DstInfo;
        std::tie(DstAddr, DstInfo) =
            computeAddrForCallArg(dl, DAG, VA, StackPtr, isTailCall, SPDiff);
        MemOpChains.push_back(DAG.getStore(Chain, dl, Op1, DstAddr, DstInfo));
      }
    } else if (VA.needsCustom() && VA.getLocVT() == MVT::f64) {
      PassF64ArgInRegs(dl, DAG, Chain, Arg, RegsToPass, VA, ArgLocs[++i],
                       StackPtr, MemOpChains, isTailCall, SPDiff);
    } else if (VA.isRegLoc()) {
      if (realArgIdx == 0 && Flags.isReturned() && !Flags.isSwiftSelf() &&
          Outs[0].VT == MVT::i32) {
        assert(VA.getLocVT() == MVT::i32 &&
               "unexpected calling convention register assignment");
        assert(!Ins.empty() && Ins[0].VT == MVT::i32 &&
               "unexpected use of 'returned'");
        isThisReturn = true;
      }
      const TargetOptions &Options = DAG.getTarget().Options;
      if (Options.EmitCallSiteInfo)
        CSInfo.ArgRegPairs.emplace_back(VA.getLocReg(), i);
      RegsToPass.push_back(std::make_pair(VA.getLocReg(), Arg));
    } else if (isByVal) {
      assert(VA.isMemLoc());
      unsigned offset = 0;

      // True if this byval aggregate will be split between registers
      // and memory.
      unsigned ByValArgsCount = CCInfo.getInRegsParamsCount();
      unsigned CurByValIdx = CCInfo.getInRegsParamsProcessed();

      if (CurByValIdx < ByValArgsCount) {

        unsigned RegBegin, RegEnd;
        CCInfo.getInRegsParamInfo(CurByValIdx, RegBegin, RegEnd);

        EVT PtrVT =
            DAG.getTargetLoweringInfo().getPointerTy(DAG.getDataLayout());
        unsigned int i, j;
        for (i = 0, j = RegBegin; j < RegEnd; i++, j++) {
          SDValue Const = DAG.getConstant(4*i, dl, MVT::i32);
          SDValue AddArg = DAG.getNode(ISD::ADD, dl, PtrVT, Arg, Const);
          SDValue Load =
              DAG.getLoad(PtrVT, dl, Chain, AddArg, MachinePointerInfo(),
                          DAG.InferPtrAlign(AddArg));
          MemOpChains.push_back(Load.getValue(1));
          RegsToPass.push_back(std::make_pair(j, Load));
        }

        // If parameter size outsides register area, "offset" value
        // helps us to calculate stack slot for remained part properly.
        offset = RegEnd - RegBegin;

        CCInfo.nextInRegsParam();
      }

      if (Flags.getByValSize() > 4*offset) {
        auto PtrVT = getPointerTy(DAG.getDataLayout());
        SDValue Dst;
        MachinePointerInfo DstInfo;
        std::tie(Dst, DstInfo) =
            computeAddrForCallArg(dl, DAG, VA, StackPtr, isTailCall, SPDiff);
        SDValue SrcOffset = DAG.getIntPtrConstant(4*offset, dl);
        SDValue Src = DAG.getNode(ISD::ADD, dl, PtrVT, Arg, SrcOffset);
        SDValue SizeNode = DAG.getConstant(Flags.getByValSize() - 4*offset, dl,
                                           MVT::i32);
        SDValue AlignNode =
            DAG.getConstant(Flags.getNonZeroByValAlign().value(), dl, MVT::i32);

        SDVTList VTs = DAG.getVTList(MVT::Other, MVT::Glue);
        SDValue Ops[] = { Chain, Dst, Src, SizeNode, AlignNode};
        MemOpChains.push_back(DAG.getNode(ARMISD::COPY_STRUCT_BYVAL, dl, VTs,
                                          Ops));
      }
    } else {
      assert(VA.isMemLoc());
      SDValue DstAddr;
      MachinePointerInfo DstInfo;
      std::tie(DstAddr, DstInfo) =
          computeAddrForCallArg(dl, DAG, VA, StackPtr, isTailCall, SPDiff);

      SDValue Store = DAG.getStore(Chain, dl, Arg, DstAddr, DstInfo);
      MemOpChains.push_back(Store);
    }
  }

  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOpChains);

  // Build a sequence of copy-to-reg nodes chained together with token chain
  // and flag operands which copy the outgoing args into the appropriate regs.
  SDValue InGlue;
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i) {
    Chain = DAG.getCopyToReg(Chain, dl, RegsToPass[i].first,
                             RegsToPass[i].second, InGlue);
    InGlue = Chain.getValue(1);
  }

  // If the callee is a GlobalAddress/ExternalSymbol node (quite common, every
  // direct call is) turn it into a TargetGlobalAddress/TargetExternalSymbol
  // node so that legalize doesn't hack it.
  bool isDirect = false;

  const TargetMachine &TM = getTargetMachine();
  const GlobalValue *GVal = nullptr;
  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee))
    GVal = G->getGlobal();
  bool isStub = !TM.shouldAssumeDSOLocal(GVal) && Subtarget->isTargetMachO();

  bool isARMFunc = !Subtarget->isThumb() || (isStub && !Subtarget->isMClass());
  bool isLocalARMFunc = false;
  auto PtrVt = getPointerTy(DAG.getDataLayout());

  if (Subtarget->genLongCalls()) {
    assert((!isPositionIndependent() || Subtarget->isTargetWindows()) &&
           "long-calls codegen is not position independent!");
    // Handle a global address or an external symbol. If it's not one of
    // those, the target's already in a register, so we don't need to do
    // anything extra.
    if (isa<GlobalAddressSDNode>(Callee)) {
      if (Subtarget->genExecuteOnly()) {
        if (Subtarget->useMovt())
          ++NumMovwMovt;
        Callee = DAG.getNode(ARMISD::Wrapper, dl, PtrVt,
                             DAG.getTargetGlobalAddress(GVal, dl, PtrVt));
      } else {
        // Create a constant pool entry for the callee address
        unsigned ARMPCLabelIndex = AFI->createPICLabelUId();
        ARMConstantPoolValue *CPV = ARMConstantPoolConstant::Create(
            GVal, ARMPCLabelIndex, ARMCP::CPValue, 0);

        // Get the address of the callee into a register
        SDValue Addr = DAG.getTargetConstantPool(CPV, PtrVt, Align(4));
        Addr = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, Addr);
        Callee = DAG.getLoad(
            PtrVt, dl, DAG.getEntryNode(), Addr,
            MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
      }
    } else if (ExternalSymbolSDNode *S=dyn_cast<ExternalSymbolSDNode>(Callee)) {
      const char *Sym = S->getSymbol();

      if (Subtarget->genExecuteOnly()) {
        if (Subtarget->useMovt())
          ++NumMovwMovt;
        Callee = DAG.getNode(ARMISD::Wrapper, dl, PtrVt,
                             DAG.getTargetGlobalAddress(GVal, dl, PtrVt));
      } else {
        // Create a constant pool entry for the callee address
        unsigned ARMPCLabelIndex = AFI->createPICLabelUId();
        ARMConstantPoolValue *CPV = ARMConstantPoolSymbol::Create(
            *DAG.getContext(), Sym, ARMPCLabelIndex, 0);

        // Get the address of the callee into a register
        SDValue Addr = DAG.getTargetConstantPool(CPV, PtrVt, Align(4));
        Addr = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, Addr);
        Callee = DAG.getLoad(
            PtrVt, dl, DAG.getEntryNode(), Addr,
            MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
      }
    }
  } else if (isa<GlobalAddressSDNode>(Callee)) {
    if (!PreferIndirect) {
      isDirect = true;
      bool isDef = GVal->isStrongDefinitionForLinker();

      // ARM call to a local ARM function is predicable.
      isLocalARMFunc = !Subtarget->isThumb() && (isDef || !ARMInterworking);
      // tBX takes a register source operand.
      if (isStub && Subtarget->isThumb1Only() && !Subtarget->hasV5TOps()) {
        assert(Subtarget->isTargetMachO() && "WrapperPIC use on non-MachO?");
        Callee = DAG.getNode(
            ARMISD::WrapperPIC, dl, PtrVt,
            DAG.getTargetGlobalAddress(GVal, dl, PtrVt, 0, ARMII::MO_NONLAZY));
        Callee = DAG.getLoad(
            PtrVt, dl, DAG.getEntryNode(), Callee,
            MachinePointerInfo::getGOT(DAG.getMachineFunction()), MaybeAlign(),
            MachineMemOperand::MODereferenceable |
                MachineMemOperand::MOInvariant);
      } else if (Subtarget->isTargetCOFF()) {
        assert(Subtarget->isTargetWindows() &&
               "Windows is the only supported COFF target");
        unsigned TargetFlags = ARMII::MO_NO_FLAG;
        if (GVal->hasDLLImportStorageClass())
          TargetFlags = ARMII::MO_DLLIMPORT;
        else if (!TM.shouldAssumeDSOLocal(GVal))
          TargetFlags = ARMII::MO_COFFSTUB;
        Callee = DAG.getTargetGlobalAddress(GVal, dl, PtrVt, /*offset=*/0,
                                            TargetFlags);
        if (TargetFlags & (ARMII::MO_DLLIMPORT | ARMII::MO_COFFSTUB))
          Callee =
              DAG.getLoad(PtrVt, dl, DAG.getEntryNode(),
                          DAG.getNode(ARMISD::Wrapper, dl, PtrVt, Callee),
                          MachinePointerInfo::getGOT(DAG.getMachineFunction()));
      } else {
        Callee = DAG.getTargetGlobalAddress(GVal, dl, PtrVt, 0, 0);
      }
    }
  } else if (ExternalSymbolSDNode *S = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    isDirect = true;
    // tBX takes a register source operand.
    const char *Sym = S->getSymbol();
    if (isARMFunc && Subtarget->isThumb1Only() && !Subtarget->hasV5TOps()) {
      unsigned ARMPCLabelIndex = AFI->createPICLabelUId();
      ARMConstantPoolValue *CPV =
        ARMConstantPoolSymbol::Create(*DAG.getContext(), Sym,
                                      ARMPCLabelIndex, 4);
      SDValue CPAddr = DAG.getTargetConstantPool(CPV, PtrVt, Align(4));
      CPAddr = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, CPAddr);
      Callee = DAG.getLoad(
          PtrVt, dl, DAG.getEntryNode(), CPAddr,
          MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
      SDValue PICLabel = DAG.getConstant(ARMPCLabelIndex, dl, MVT::i32);
      Callee = DAG.getNode(ARMISD::PIC_ADD, dl, PtrVt, Callee, PICLabel);
    } else {
      Callee = DAG.getTargetExternalSymbol(Sym, PtrVt, 0);
    }
  }

  if (isCmseNSCall) {
    assert(!isARMFunc && !isDirect &&
           "Cannot handle call to ARM function or direct call");
    if (NumBytes > 0) {
      DiagnosticInfoUnsupported Diag(DAG.getMachineFunction().getFunction(),
                                     "call to non-secure function would "
                                     "require passing arguments on stack",
                                     dl.getDebugLoc());
      DAG.getContext()->diagnose(Diag);
    }
    if (isStructRet) {
      DiagnosticInfoUnsupported Diag(
          DAG.getMachineFunction().getFunction(),
          "call to non-secure function would return value through pointer",
          dl.getDebugLoc());
      DAG.getContext()->diagnose(Diag);
    }
  }

  // FIXME: handle tail calls differently.
  unsigned CallOpc;
  if (Subtarget->isThumb()) {
    if (GuardWithBTI)
      CallOpc = ARMISD::t2CALL_BTI;
    else if (isCmseNSCall)
      CallOpc = ARMISD::tSECALL;
    else if ((!isDirect || isARMFunc) && !Subtarget->hasV5TOps())
      CallOpc = ARMISD::CALL_NOLINK;
    else
      CallOpc = ARMISD::CALL;
  } else {
    if (!isDirect && !Subtarget->hasV5TOps())
      CallOpc = ARMISD::CALL_NOLINK;
    else if (doesNotRet && isDirect && Subtarget->hasRetAddrStack() &&
             // Emit regular call when code size is the priority
             !Subtarget->hasMinSize())
      // "mov lr, pc; b _foo" to avoid confusing the RSP
      CallOpc = ARMISD::CALL_NOLINK;
    else
      CallOpc = isLocalARMFunc ? ARMISD::CALL_PRED : ARMISD::CALL;
  }

  // We don't usually want to end the call-sequence here because we would tidy
  // the frame up *after* the call, however in the ABI-changing tail-call case
  // we've carefully laid out the parameters so that when sp is reset they'll be
  // in the correct location.
  if (isTailCall && !isSibCall) {
    Chain = DAG.getCALLSEQ_END(Chain, 0, 0, InGlue, dl);
    InGlue = Chain.getValue(1);
  }

  std::vector<SDValue> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);

  if (isTailCall) {
    Ops.push_back(DAG.getTargetConstant(SPDiff, dl, MVT::i32));
  }

  // Add argument registers to the end of the list so that they are known live
  // into the call.
  for (unsigned i = 0, e = RegsToPass.size(); i != e; ++i)
    Ops.push_back(DAG.getRegister(RegsToPass[i].first,
                                  RegsToPass[i].second.getValueType()));

  // Add a register mask operand representing the call-preserved registers.
  const uint32_t *Mask;
  const ARMBaseRegisterInfo *ARI = Subtarget->getRegisterInfo();
  if (isThisReturn) {
    // For 'this' returns, use the R0-preserving mask if applicable
    Mask = ARI->getThisReturnPreservedMask(MF, CallConv);
    if (!Mask) {
      // Set isThisReturn to false if the calling convention is not one that
      // allows 'returned' to be modeled in this way, so LowerCallResult does
      // not try to pass 'this' straight through
      isThisReturn = false;
      Mask = ARI->getCallPreservedMask(MF, CallConv);
    }
  } else
    Mask = ARI->getCallPreservedMask(MF, CallConv);

  assert(Mask && "Missing call preserved mask for calling convention");
  Ops.push_back(DAG.getRegisterMask(Mask));

  if (InGlue.getNode())
    Ops.push_back(InGlue);

  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  if (isTailCall) {
    MF.getFrameInfo().setHasTailCall();
    SDValue Ret = DAG.getNode(ARMISD::TC_RETURN, dl, NodeTys, Ops);
    DAG.addNoMergeSiteInfo(Ret.getNode(), CLI.NoMerge);
    DAG.addCallSiteInfo(Ret.getNode(), std::move(CSInfo));
    return Ret;
  }

  // Returns a chain and a flag for retval copy to use.
  Chain = DAG.getNode(CallOpc, dl, NodeTys, Ops);
  DAG.addNoMergeSiteInfo(Chain.getNode(), CLI.NoMerge);
  InGlue = Chain.getValue(1);
  DAG.addCallSiteInfo(Chain.getNode(), std::move(CSInfo));

  // If we're guaranteeing tail-calls will be honoured, the callee must
  // pop its own argument stack on return. But this call is *not* a tail call so
  // we need to undo that after it returns to restore the status-quo.
  bool TailCallOpt = getTargetMachine().Options.GuaranteedTailCallOpt;
  uint64_t CalleePopBytes =
      canGuaranteeTCO(CallConv, TailCallOpt) ? alignTo(NumBytes, 16) : -1ULL;

  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, CalleePopBytes, InGlue, dl);
  if (!Ins.empty())
    InGlue = Chain.getValue(1);

  // Handle result values, copying them out of physregs into vregs that we
  // return.
  return LowerCallResult(Chain, InGlue, CallConv, isVarArg, Ins, dl, DAG,
                         InVals, isThisReturn,
                         isThisReturn ? OutVals[0] : SDValue(), isCmseNSCall);
}

/// HandleByVal - Every parameter *after* a byval parameter is passed
/// on the stack.  Remember the next parameter register to allocate,
/// and then confiscate the rest of the parameter registers to insure
/// this.
void ARMTargetLowering::HandleByVal(CCState *State, unsigned &Size,
                                    Align Alignment) const {
  // Byval (as with any stack) slots are always at least 4 byte aligned.
  Alignment = std::max(Alignment, Align(4));

  unsigned Reg = State->AllocateReg(GPRArgRegs);
  if (!Reg)
    return;

  unsigned AlignInRegs = Alignment.value() / 4;
  unsigned Waste = (ARM::R4 - Reg) % AlignInRegs;
  for (unsigned i = 0; i < Waste; ++i)
    Reg = State->AllocateReg(GPRArgRegs);

  if (!Reg)
    return;

  unsigned Excess = 4 * (ARM::R4 - Reg);

  // Special case when NSAA != SP and parameter size greater than size of
  // all remained GPR regs. In that case we can't split parameter, we must
  // send it to stack. We also must set NCRN to R4, so waste all
  // remained registers.
  const unsigned NSAAOffset = State->getStackSize();
  if (NSAAOffset != 0 && Size > Excess) {
    while (State->AllocateReg(GPRArgRegs))
      ;
    return;
  }

  // First register for byval parameter is the first register that wasn't
  // allocated before this method call, so it would be "reg".
  // If parameter is small enough to be saved in range [reg, r4), then
  // the end (first after last) register would be reg + param-size-in-regs,
  // else parameter would be splitted between registers and stack,
  // end register would be r4 in this case.
  unsigned ByValRegBegin = Reg;
  unsigned ByValRegEnd = std::min<unsigned>(Reg + Size / 4, ARM::R4);
  State->addInRegsParamInfo(ByValRegBegin, ByValRegEnd);
  // Note, first register is allocated in the beginning of function already,
  // allocate remained amount of registers we need.
  for (unsigned i = Reg + 1; i != ByValRegEnd; ++i)
    State->AllocateReg(GPRArgRegs);
  // A byval parameter that is split between registers and memory needs its
  // size truncated here.
  // In the case where the entire structure fits in registers, we set the
  // size in memory to zero.
  Size = std::max<int>(Size - Excess, 0);
}

/// MatchingStackOffset - Return true if the given stack call argument is
/// already available in the same position (relatively) of the caller's
/// incoming argument stack.
static
bool MatchingStackOffset(SDValue Arg, unsigned Offset, ISD::ArgFlagsTy Flags,
                         MachineFrameInfo &MFI, const MachineRegisterInfo *MRI,
                         const TargetInstrInfo *TII) {
  unsigned Bytes = Arg.getValueSizeInBits() / 8;
  int FI = std::numeric_limits<int>::max();
  if (Arg.getOpcode() == ISD::CopyFromReg) {
    Register VR = cast<RegisterSDNode>(Arg.getOperand(1))->getReg();
    if (!VR.isVirtual())
      return false;
    MachineInstr *Def = MRI->getVRegDef(VR);
    if (!Def)
      return false;
    if (!Flags.isByVal()) {
      if (!TII->isLoadFromStackSlot(*Def, FI))
        return false;
    } else {
      return false;
    }
  } else if (LoadSDNode *Ld = dyn_cast<LoadSDNode>(Arg)) {
    if (Flags.isByVal())
      // ByVal argument is passed in as a pointer but it's now being
      // dereferenced. e.g.
      // define @foo(%struct.X* %A) {
      //   tail call @bar(%struct.X* byval %A)
      // }
      return false;
    SDValue Ptr = Ld->getBasePtr();
    FrameIndexSDNode *FINode = dyn_cast<FrameIndexSDNode>(Ptr);
    if (!FINode)
      return false;
    FI = FINode->getIndex();
  } else
    return false;

  assert(FI != std::numeric_limits<int>::max());
  if (!MFI.isFixedObjectIndex(FI))
    return false;
  return Offset == MFI.getObjectOffset(FI) && Bytes == MFI.getObjectSize(FI);
}

/// IsEligibleForTailCallOptimization - Check whether the call is eligible
/// for tail call optimization. Targets which want to do tail call
/// optimization should implement this function. Note that this function also
/// processes musttail calls, so when this function returns false on a valid
/// musttail call, a fatal backend error occurs.
bool ARMTargetLowering::IsEligibleForTailCallOptimization(
    TargetLowering::CallLoweringInfo &CLI, CCState &CCInfo,
    SmallVectorImpl<CCValAssign> &ArgLocs, const bool isIndirect) const {
  CallingConv::ID CalleeCC = CLI.CallConv;
  SDValue Callee = CLI.Callee;
  bool isVarArg = CLI.IsVarArg;
  const SmallVectorImpl<ISD::OutputArg> &Outs = CLI.Outs;
  const SmallVectorImpl<SDValue> &OutVals = CLI.OutVals;
  const SmallVectorImpl<ISD::InputArg> &Ins = CLI.Ins;
  const SelectionDAG &DAG = CLI.DAG;
  MachineFunction &MF = DAG.getMachineFunction();
  const Function &CallerF = MF.getFunction();
  CallingConv::ID CallerCC = CallerF.getCallingConv();

  assert(Subtarget->supportsTailCall());

  // Indirect tail calls cannot be optimized for Thumb1 if the args
  // to the call take up r0-r3. The reason is that there are no legal registers
  // left to hold the pointer to the function to be called.
  // Similarly, if the function uses return address sign and authentication,
  // r12 is needed to hold the PAC and is not available to hold the callee
  // address.
  if (Outs.size() >= 4 &&
      (!isa<GlobalAddressSDNode>(Callee.getNode()) || isIndirect)) {
    if (Subtarget->isThumb1Only())
      return false;
    // Conservatively assume the function spills LR.
    if (MF.getInfo<ARMFunctionInfo>()->shouldSignReturnAddress(true))
      return false;
  }

  // Look for obvious safe cases to perform tail call optimization that do not
  // require ABI changes. This is what gcc calls sibcall.

  // Exception-handling functions need a special set of instructions to indicate
  // a return to the hardware. Tail-calling another function would probably
  // break this.
  if (CallerF.hasFnAttribute("interrupt"))
    return false;

  if (canGuaranteeTCO(CalleeCC, getTargetMachine().Options.GuaranteedTailCallOpt))
    return CalleeCC == CallerCC;

  // Also avoid sibcall optimization if either caller or callee uses struct
  // return semantics.
  bool isCalleeStructRet = Outs.empty() ? false : Outs[0].Flags.isSRet();
  bool isCallerStructRet = MF.getFunction().hasStructRetAttr();
  if (isCalleeStructRet || isCallerStructRet)
    return false;

  // Externally-defined functions with weak linkage should not be
  // tail-called on ARM when the OS does not support dynamic
  // pre-emption of symbols, as the AAELF spec requires normal calls
  // to undefined weak functions to be replaced with a NOP or jump to the
  // next instruction. The behaviour of branch instructions in this
  // situation (as used for tail calls) is implementation-defined, so we
  // cannot rely on the linker replacing the tail call with a return.
  if (GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    const GlobalValue *GV = G->getGlobal();
    const Triple &TT = getTargetMachine().getTargetTriple();
    if (GV->hasExternalWeakLinkage() &&
        (!TT.isOSWindows() || TT.isOSBinFormatELF() || TT.isOSBinFormatMachO()))
      return false;
  }

  // Check that the call results are passed in the same way.
  LLVMContext &C = *DAG.getContext();
  if (!CCState::resultsCompatible(
          getEffectiveCallingConv(CalleeCC, isVarArg),
          getEffectiveCallingConv(CallerCC, CallerF.isVarArg()), MF, C, Ins,
          CCAssignFnForReturn(CalleeCC, isVarArg),
          CCAssignFnForReturn(CallerCC, CallerF.isVarArg())))
    return false;
  // The callee has to preserve all registers the caller needs to preserve.
  const ARMBaseRegisterInfo *TRI = Subtarget->getRegisterInfo();
  const uint32_t *CallerPreserved = TRI->getCallPreservedMask(MF, CallerCC);
  if (CalleeCC != CallerCC) {
    const uint32_t *CalleePreserved = TRI->getCallPreservedMask(MF, CalleeCC);
    if (!TRI->regmaskSubsetEqual(CallerPreserved, CalleePreserved))
      return false;
  }

  // If Caller's vararg or byval argument has been split between registers and
  // stack, do not perform tail call, since part of the argument is in caller's
  // local frame.
  const ARMFunctionInfo *AFI_Caller = MF.getInfo<ARMFunctionInfo>();
  if (AFI_Caller->getArgRegsSaveSize())
    return false;

  // If the callee takes no arguments then go on to check the results of the
  // call.
  if (!Outs.empty()) {
    if (CCInfo.getStackSize()) {
      // Check if the arguments are already laid out in the right way as
      // the caller's fixed stack objects.
      MachineFrameInfo &MFI = MF.getFrameInfo();
      const MachineRegisterInfo *MRI = &MF.getRegInfo();
      const TargetInstrInfo *TII = Subtarget->getInstrInfo();
      for (unsigned i = 0, realArgIdx = 0, e = ArgLocs.size();
           i != e;
           ++i, ++realArgIdx) {
        CCValAssign &VA = ArgLocs[i];
        EVT RegVT = VA.getLocVT();
        SDValue Arg = OutVals[realArgIdx];
        ISD::ArgFlagsTy Flags = Outs[realArgIdx].Flags;
        if (VA.getLocInfo() == CCValAssign::Indirect)
          return false;
        if (VA.needsCustom() && (RegVT == MVT::f64 || RegVT == MVT::v2f64)) {
          // f64 and vector types are split into multiple registers or
          // register/stack-slot combinations.  The types will not match
          // the registers; give up on memory f64 refs until we figure
          // out what to do about this.
          if (!VA.isRegLoc())
            return false;
          if (!ArgLocs[++i].isRegLoc())
            return false;
          if (RegVT == MVT::v2f64) {
            if (!ArgLocs[++i].isRegLoc())
              return false;
            if (!ArgLocs[++i].isRegLoc())
              return false;
          }
        } else if (!VA.isRegLoc()) {
          if (!MatchingStackOffset(Arg, VA.getLocMemOffset(), Flags,
                                   MFI, MRI, TII))
            return false;
        }
      }
    }

    const MachineRegisterInfo &MRI = MF.getRegInfo();
    if (!parametersInCSRMatch(MRI, CallerPreserved, ArgLocs, OutVals))
      return false;
  }

  return true;
}

bool
ARMTargetLowering::CanLowerReturn(CallingConv::ID CallConv,
                                  MachineFunction &MF, bool isVarArg,
                                  const SmallVectorImpl<ISD::OutputArg> &Outs,
                                  LLVMContext &Context) const {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, isVarArg, MF, RVLocs, Context);
  return CCInfo.CheckReturn(Outs, CCAssignFnForReturn(CallConv, isVarArg));
}

static SDValue LowerInterruptReturn(SmallVectorImpl<SDValue> &RetOps,
                                    const SDLoc &DL, SelectionDAG &DAG) {
  const MachineFunction &MF = DAG.getMachineFunction();
  const Function &F = MF.getFunction();

  StringRef IntKind = F.getFnAttribute("interrupt").getValueAsString();

  // See ARM ARM v7 B1.8.3. On exception entry LR is set to a possibly offset
  // version of the "preferred return address". These offsets affect the return
  // instruction if this is a return from PL1 without hypervisor extensions.
  //    IRQ/FIQ: +4     "subs pc, lr, #4"
  //    SWI:     0      "subs pc, lr, #0"
  //    ABORT:   +4     "subs pc, lr, #4"
  //    UNDEF:   +4/+2  "subs pc, lr, #0"
  // UNDEF varies depending on where the exception came from ARM or Thumb
  // mode. Alongside GCC, we throw our hands up in disgust and pretend it's 0.

  int64_t LROffset;
  if (IntKind == "" || IntKind == "IRQ" || IntKind == "FIQ" ||
      IntKind == "ABORT")
    LROffset = 4;
  else if (IntKind == "SWI" || IntKind == "UNDEF")
    LROffset = 0;
  else
    report_fatal_error("Unsupported interrupt attribute. If present, value "
                       "must be one of: IRQ, FIQ, SWI, ABORT or UNDEF");

  RetOps.insert(RetOps.begin() + 1,
                DAG.getConstant(LROffset, DL, MVT::i32, false));

  return DAG.getNode(ARMISD::INTRET_GLUE, DL, MVT::Other, RetOps);
}

SDValue
ARMTargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                               bool isVarArg,
                               const SmallVectorImpl<ISD::OutputArg> &Outs,
                               const SmallVectorImpl<SDValue> &OutVals,
                               const SDLoc &dl, SelectionDAG &DAG) const {
  // CCValAssign - represent the assignment of the return value to a location.
  SmallVector<CCValAssign, 16> RVLocs;

  // CCState - Info about the registers and stack slots.
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  // Analyze outgoing return values.
  CCInfo.AnalyzeReturn(Outs, CCAssignFnForReturn(CallConv, isVarArg));

  SDValue Glue;
  SmallVector<SDValue, 4> RetOps;
  RetOps.push_back(Chain); // Operand #0 = Chain (updated below)
  bool isLittleEndian = Subtarget->isLittle();

  MachineFunction &MF = DAG.getMachineFunction();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  AFI->setReturnRegsCount(RVLocs.size());

 // Report error if cmse entry function returns structure through first ptr arg.
  if (AFI->isCmseNSEntryFunction() && MF.getFunction().hasStructRetAttr()) {
    // Note: using an empty SDLoc(), as the first line of the function is a
    // better place to report than the last line.
    DiagnosticInfoUnsupported Diag(
        DAG.getMachineFunction().getFunction(),
        "secure entry function would return value through pointer",
        SDLoc().getDebugLoc());
    DAG.getContext()->diagnose(Diag);
  }

  // Copy the result values into the output registers.
  for (unsigned i = 0, realRVLocIdx = 0;
       i != RVLocs.size();
       ++i, ++realRVLocIdx) {
    CCValAssign &VA = RVLocs[i];
    assert(VA.isRegLoc() && "Can only return in registers!");

    SDValue Arg = OutVals[realRVLocIdx];
    bool ReturnF16 = false;

    if (Subtarget->hasFullFP16() && Subtarget->isTargetHardFloat()) {
      // Half-precision return values can be returned like this:
      //
      // t11 f16 = fadd ...
      // t12: i16 = bitcast t11
      //   t13: i32 = zero_extend t12
      // t14: f32 = bitcast t13  <~~~~~~~ Arg
      //
      // to avoid code generation for bitcasts, we simply set Arg to the node
      // that produces the f16 value, t11 in this case.
      //
      if (Arg.getValueType() == MVT::f32 && Arg.getOpcode() == ISD::BITCAST) {
        SDValue ZE = Arg.getOperand(0);
        if (ZE.getOpcode() == ISD::ZERO_EXTEND && ZE.getValueType() == MVT::i32) {
          SDValue BC = ZE.getOperand(0);
          if (BC.getOpcode() == ISD::BITCAST && BC.getValueType() == MVT::i16) {
            Arg = BC.getOperand(0);
            ReturnF16 = true;
          }
        }
      }
    }

    switch (VA.getLocInfo()) {
    default: llvm_unreachable("Unknown loc info!");
    case CCValAssign::Full: break;
    case CCValAssign::BCvt:
      if (!ReturnF16)
        Arg = DAG.getNode(ISD::BITCAST, dl, VA.getLocVT(), Arg);
      break;
    }

    // Mask f16 arguments if this is a CMSE nonsecure entry.
    auto RetVT = Outs[realRVLocIdx].ArgVT;
    if (AFI->isCmseNSEntryFunction() && (RetVT == MVT::f16)) {
      if (VA.needsCustom() && VA.getValVT() == MVT::f16) {
        Arg = MoveFromHPR(dl, DAG, VA.getLocVT(), VA.getValVT(), Arg);
      } else {
        auto LocBits = VA.getLocVT().getSizeInBits();
        auto MaskValue = APInt::getLowBitsSet(LocBits, RetVT.getSizeInBits());
        SDValue Mask =
            DAG.getConstant(MaskValue, dl, MVT::getIntegerVT(LocBits));
        Arg = DAG.getNode(ISD::BITCAST, dl, MVT::getIntegerVT(LocBits), Arg);
        Arg = DAG.getNode(ISD::AND, dl, MVT::getIntegerVT(LocBits), Arg, Mask);
        Arg = DAG.getNode(ISD::BITCAST, dl, VA.getLocVT(), Arg);
      }
    }

    if (VA.needsCustom() &&
        (VA.getLocVT() == MVT::v2f64 || VA.getLocVT() == MVT::f64)) {
      if (VA.getLocVT() == MVT::v2f64) {
        // Extract the first half and return it in two registers.
        SDValue Half = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::f64, Arg,
                                   DAG.getConstant(0, dl, MVT::i32));
        SDValue HalfGPRs = DAG.getNode(ARMISD::VMOVRRD, dl,
                                       DAG.getVTList(MVT::i32, MVT::i32), Half);

        Chain =
            DAG.getCopyToReg(Chain, dl, VA.getLocReg(),
                             HalfGPRs.getValue(isLittleEndian ? 0 : 1), Glue);
        Glue = Chain.getValue(1);
        RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
        VA = RVLocs[++i]; // skip ahead to next loc
        Chain =
            DAG.getCopyToReg(Chain, dl, VA.getLocReg(),
                             HalfGPRs.getValue(isLittleEndian ? 1 : 0), Glue);
        Glue = Chain.getValue(1);
        RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
        VA = RVLocs[++i]; // skip ahead to next loc

        // Extract the 2nd half and fall through to handle it as an f64 value.
        Arg = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::f64, Arg,
                          DAG.getConstant(1, dl, MVT::i32));
      }
      // Legalize ret f64 -> ret 2 x i32.  We always have fmrrd if f64 is
      // available.
      SDValue fmrrd = DAG.getNode(ARMISD::VMOVRRD, dl,
                                  DAG.getVTList(MVT::i32, MVT::i32), Arg);
      Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(),
                               fmrrd.getValue(isLittleEndian ? 0 : 1), Glue);
      Glue = Chain.getValue(1);
      RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
      VA = RVLocs[++i]; // skip ahead to next loc
      Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(),
                               fmrrd.getValue(isLittleEndian ? 1 : 0), Glue);
    } else
      Chain = DAG.getCopyToReg(Chain, dl, VA.getLocReg(), Arg, Glue);

    // Guarantee that all emitted copies are
    // stuck together, avoiding something bad.
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(
        VA.getLocReg(), ReturnF16 ? Arg.getValueType() : VA.getLocVT()));
  }
  const ARMBaseRegisterInfo *TRI = Subtarget->getRegisterInfo();
  const MCPhysReg *I =
      TRI->getCalleeSavedRegsViaCopy(&DAG.getMachineFunction());
  if (I) {
    for (; *I; ++I) {
      if (ARM::GPRRegClass.contains(*I))
        RetOps.push_back(DAG.getRegister(*I, MVT::i32));
      else if (ARM::DPRRegClass.contains(*I))
        RetOps.push_back(DAG.getRegister(*I, MVT::getFloatingPointVT(64)));
      else
        llvm_unreachable("Unexpected register class in CSRsViaCopy!");
    }
  }

  // Update chain and glue.
  RetOps[0] = Chain;
  if (Glue.getNode())
    RetOps.push_back(Glue);

  // CPUs which aren't M-class use a special sequence to return from
  // exceptions (roughly, any instruction setting pc and cpsr simultaneously,
  // though we use "subs pc, lr, #N").
  //
  // M-class CPUs actually use a normal return sequence with a special
  // (hardware-provided) value in LR, so the normal code path works.
  if (DAG.getMachineFunction().getFunction().hasFnAttribute("interrupt") &&
      !Subtarget->isMClass()) {
    if (Subtarget->isThumb1Only())
      report_fatal_error("interrupt attribute is not supported in Thumb1");
    return LowerInterruptReturn(RetOps, dl, DAG);
  }

  ARMISD::NodeType RetNode = AFI->isCmseNSEntryFunction() ? ARMISD::SERET_GLUE :
                                                            ARMISD::RET_GLUE;
  return DAG.getNode(RetNode, dl, MVT::Other, RetOps);
}

bool ARMTargetLowering::isUsedByReturnOnly(SDNode *N, SDValue &Chain) const {
  if (N->getNumValues() != 1)
    return false;
  if (!N->hasNUsesOfValue(1, 0))
    return false;

  SDValue TCChain = Chain;
  SDNode *Copy = *N->use_begin();
  if (Copy->getOpcode() == ISD::CopyToReg) {
    // If the copy has a glue operand, we conservatively assume it isn't safe to
    // perform a tail call.
    if (Copy->getOperand(Copy->getNumOperands()-1).getValueType() == MVT::Glue)
      return false;
    TCChain = Copy->getOperand(0);
  } else if (Copy->getOpcode() == ARMISD::VMOVRRD) {
    SDNode *VMov = Copy;
    // f64 returned in a pair of GPRs.
    SmallPtrSet<SDNode*, 2> Copies;
    for (SDNode *U : VMov->uses()) {
      if (U->getOpcode() != ISD::CopyToReg)
        return false;
      Copies.insert(U);
    }
    if (Copies.size() > 2)
      return false;

    for (SDNode *U : VMov->uses()) {
      SDValue UseChain = U->getOperand(0);
      if (Copies.count(UseChain.getNode()))
        // Second CopyToReg
        Copy = U;
      else {
        // We are at the top of this chain.
        // If the copy has a glue operand, we conservatively assume it
        // isn't safe to perform a tail call.
        if (U->getOperand(U->getNumOperands() - 1).getValueType() == MVT::Glue)
          return false;
        // First CopyToReg
        TCChain = UseChain;
      }
    }
  } else if (Copy->getOpcode() == ISD::BITCAST) {
    // f32 returned in a single GPR.
    if (!Copy->hasOneUse())
      return false;
    Copy = *Copy->use_begin();
    if (Copy->getOpcode() != ISD::CopyToReg || !Copy->hasNUsesOfValue(1, 0))
      return false;
    // If the copy has a glue operand, we conservatively assume it isn't safe to
    // perform a tail call.
    if (Copy->getOperand(Copy->getNumOperands()-1).getValueType() == MVT::Glue)
      return false;
    TCChain = Copy->getOperand(0);
  } else {
    return false;
  }

  bool HasRet = false;
  for (const SDNode *U : Copy->uses()) {
    if (U->getOpcode() != ARMISD::RET_GLUE &&
        U->getOpcode() != ARMISD::INTRET_GLUE)
      return false;
    HasRet = true;
  }

  if (!HasRet)
    return false;

  Chain = TCChain;
  return true;
}

bool ARMTargetLowering::mayBeEmittedAsTailCall(const CallInst *CI) const {
  if (!Subtarget->supportsTailCall())
    return false;

  if (!CI->isTailCall())
    return false;

  return true;
}

// Trying to write a 64 bit value so need to split into two 32 bit values first,
// and pass the lower and high parts through.
static SDValue LowerWRITE_REGISTER(SDValue Op, SelectionDAG &DAG) {
  SDLoc DL(Op);
  SDValue WriteValue = Op->getOperand(2);

  // This function is only supposed to be called for i64 type argument.
  assert(WriteValue.getValueType() == MVT::i64
          && "LowerWRITE_REGISTER called for non-i64 type argument.");

  SDValue Lo, Hi;
  std::tie(Lo, Hi) = DAG.SplitScalar(WriteValue, DL, MVT::i32, MVT::i32);
  SDValue Ops[] = { Op->getOperand(0), Op->getOperand(1), Lo, Hi };
  return DAG.getNode(ISD::WRITE_REGISTER, DL, MVT::Other, Ops);
}

// ConstantPool, JumpTable, GlobalAddress, and ExternalSymbol are lowered as
// their target counterpart wrapped in the ARMISD::Wrapper node. Suppose N is
// one of the above mentioned nodes. It has to be wrapped because otherwise
// Select(N) returns N. So the raw TargetGlobalAddress nodes, etc. can only
// be used to form addressing mode. These wrapped nodes will be selected
// into MOVi.
SDValue ARMTargetLowering::LowerConstantPool(SDValue Op,
                                             SelectionDAG &DAG) const {
  EVT PtrVT = Op.getValueType();
  // FIXME there is no actual debug info here
  SDLoc dl(Op);
  ConstantPoolSDNode *CP = cast<ConstantPoolSDNode>(Op);
  SDValue Res;

  // When generating execute-only code Constant Pools must be promoted to the
  // global data section. It's a bit ugly that we can't share them across basic
  // blocks, but this way we guarantee that execute-only behaves correct with
  // position-independent addressing modes.
  if (Subtarget->genExecuteOnly()) {
    auto AFI = DAG.getMachineFunction().getInfo<ARMFunctionInfo>();
    auto T = const_cast<Type*>(CP->getType());
    auto C = const_cast<Constant*>(CP->getConstVal());
    auto M = const_cast<Module*>(DAG.getMachineFunction().
                                 getFunction().getParent());
    auto GV = new GlobalVariable(
                    *M, T, /*isConstant=*/true, GlobalVariable::InternalLinkage, C,
                    Twine(DAG.getDataLayout().getPrivateGlobalPrefix()) + "CP" +
                    Twine(DAG.getMachineFunction().getFunctionNumber()) + "_" +
                    Twine(AFI->createPICLabelUId())
                  );
    SDValue GA = DAG.getTargetGlobalAddress(dyn_cast<GlobalValue>(GV),
                                            dl, PtrVT);
    return LowerGlobalAddress(GA, DAG);
  }

  // The 16-bit ADR instruction can only encode offsets that are multiples of 4,
  // so we need to align to at least 4 bytes when we don't have 32-bit ADR.
  Align CPAlign = CP->getAlign();
  if (Subtarget->isThumb1Only())
    CPAlign = std::max(CPAlign, Align(4));
  if (CP->isMachineConstantPoolEntry())
    Res =
        DAG.getTargetConstantPool(CP->getMachineCPVal(), PtrVT, CPAlign);
  else
    Res = DAG.getTargetConstantPool(CP->getConstVal(), PtrVT, CPAlign);
  return DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, Res);
}

unsigned ARMTargetLowering::getJumpTableEncoding() const {
  // If we don't have a 32-bit pc-relative branch instruction then the jump
  // table consists of block addresses. Usually this is inline, but for
  // execute-only it must be placed out-of-line.
  if (Subtarget->genExecuteOnly() && !Subtarget->hasV8MBaselineOps())
    return MachineJumpTableInfo::EK_BlockAddress;
  return MachineJumpTableInfo::EK_Inline;
}

SDValue ARMTargetLowering::LowerBlockAddress(SDValue Op,
                                             SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  unsigned ARMPCLabelIndex = 0;
  SDLoc DL(Op);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  const BlockAddress *BA = cast<BlockAddressSDNode>(Op)->getBlockAddress();
  SDValue CPAddr;
  bool IsPositionIndependent = isPositionIndependent() || Subtarget->isROPI();
  if (!IsPositionIndependent) {
    CPAddr = DAG.getTargetConstantPool(BA, PtrVT, Align(4));
  } else {
    unsigned PCAdj = Subtarget->isThumb() ? 4 : 8;
    ARMPCLabelIndex = AFI->createPICLabelUId();
    ARMConstantPoolValue *CPV =
      ARMConstantPoolConstant::Create(BA, ARMPCLabelIndex,
                                      ARMCP::CPBlockAddress, PCAdj);
    CPAddr = DAG.getTargetConstantPool(CPV, PtrVT, Align(4));
  }
  CPAddr = DAG.getNode(ARMISD::Wrapper, DL, PtrVT, CPAddr);
  SDValue Result = DAG.getLoad(
      PtrVT, DL, DAG.getEntryNode(), CPAddr,
      MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
  if (!IsPositionIndependent)
    return Result;
  SDValue PICLabel = DAG.getConstant(ARMPCLabelIndex, DL, MVT::i32);
  return DAG.getNode(ARMISD::PIC_ADD, DL, PtrVT, Result, PICLabel);
}

/// Convert a TLS address reference into the correct sequence of loads
/// and calls to compute the variable's address for Darwin, and return an
/// SDValue containing the final node.

/// Darwin only has one TLS scheme which must be capable of dealing with the
/// fully general situation, in the worst case. This means:
///     + "extern __thread" declaration.
///     + Defined in a possibly unknown dynamic library.
///
/// The general system is that each __thread variable has a [3 x i32] descriptor
/// which contains information used by the runtime to calculate the address. The
/// only part of this the compiler needs to know about is the first word, which
/// contains a function pointer that must be called with the address of the
/// entire descriptor in "r0".
///
/// Since this descriptor may be in a different unit, in general access must
/// proceed along the usual ARM rules. A common sequence to produce is:
///
///     movw rT1, :lower16:_var$non_lazy_ptr
///     movt rT1, :upper16:_var$non_lazy_ptr
///     ldr r0, [rT1]
///     ldr rT2, [r0]
///     blx rT2
///     [...address now in r0...]
SDValue
ARMTargetLowering::LowerGlobalTLSAddressDarwin(SDValue Op,
                                               SelectionDAG &DAG) const {
  assert(Subtarget->isTargetDarwin() &&
         "This function expects a Darwin target");
  SDLoc DL(Op);

  // First step is to get the address of the actua global symbol. This is where
  // the TLS descriptor lives.
  SDValue DescAddr = LowerGlobalAddressDarwin(Op, DAG);

  // The first entry in the descriptor is a function pointer that we must call
  // to obtain the address of the variable.
  SDValue Chain = DAG.getEntryNode();
  SDValue FuncTLVGet = DAG.getLoad(
      MVT::i32, DL, Chain, DescAddr,
      MachinePointerInfo::getGOT(DAG.getMachineFunction()), Align(4),
      MachineMemOperand::MONonTemporal | MachineMemOperand::MODereferenceable |
          MachineMemOperand::MOInvariant);
  Chain = FuncTLVGet.getValue(1);

  MachineFunction &F = DAG.getMachineFunction();
  MachineFrameInfo &MFI = F.getFrameInfo();
  MFI.setAdjustsStack(true);

  // TLS calls preserve all registers except those that absolutely must be
  // trashed: R0 (it takes an argument), LR (it's a call) and CPSR (let's not be
  // silly).
  auto TRI =
      getTargetMachine().getSubtargetImpl(F.getFunction())->getRegisterInfo();
  auto ARI = static_cast<const ARMRegisterInfo *>(TRI);
  const uint32_t *Mask = ARI->getTLSCallPreservedMask(DAG.getMachineFunction());

  // Finally, we can make the call. This is just a degenerate version of a
  // normal AArch64 call node: r0 takes the address of the descriptor, and
  // returns the address of the variable in this thread.
  Chain = DAG.getCopyToReg(Chain, DL, ARM::R0, DescAddr, SDValue());
  Chain =
      DAG.getNode(ARMISD::CALL, DL, DAG.getVTList(MVT::Other, MVT::Glue),
                  Chain, FuncTLVGet, DAG.getRegister(ARM::R0, MVT::i32),
                  DAG.getRegisterMask(Mask), Chain.getValue(1));
  return DAG.getCopyFromReg(Chain, DL, ARM::R0, MVT::i32, Chain.getValue(1));
}

SDValue
ARMTargetLowering::LowerGlobalTLSAddressWindows(SDValue Op,
                                                SelectionDAG &DAG) const {
  assert(Subtarget->isTargetWindows() && "Windows specific TLS lowering");

  SDValue Chain = DAG.getEntryNode();
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDLoc DL(Op);

  // Load the current TEB (thread environment block)
  SDValue Ops[] = {Chain,
                   DAG.getTargetConstant(Intrinsic::arm_mrc, DL, MVT::i32),
                   DAG.getTargetConstant(15, DL, MVT::i32),
                   DAG.getTargetConstant(0, DL, MVT::i32),
                   DAG.getTargetConstant(13, DL, MVT::i32),
                   DAG.getTargetConstant(0, DL, MVT::i32),
                   DAG.getTargetConstant(2, DL, MVT::i32)};
  SDValue CurrentTEB = DAG.getNode(ISD::INTRINSIC_W_CHAIN, DL,
                                   DAG.getVTList(MVT::i32, MVT::Other), Ops);

  SDValue TEB = CurrentTEB.getValue(0);
  Chain = CurrentTEB.getValue(1);

  // Load the ThreadLocalStoragePointer from the TEB
  // A pointer to the TLS array is located at offset 0x2c from the TEB.
  SDValue TLSArray =
      DAG.getNode(ISD::ADD, DL, PtrVT, TEB, DAG.getIntPtrConstant(0x2c, DL));
  TLSArray = DAG.getLoad(PtrVT, DL, Chain, TLSArray, MachinePointerInfo());

  // The pointer to the thread's TLS data area is at the TLS Index scaled by 4
  // offset into the TLSArray.

  // Load the TLS index from the C runtime
  SDValue TLSIndex =
      DAG.getTargetExternalSymbol("_tls_index", PtrVT, ARMII::MO_NO_FLAG);
  TLSIndex = DAG.getNode(ARMISD::Wrapper, DL, PtrVT, TLSIndex);
  TLSIndex = DAG.getLoad(PtrVT, DL, Chain, TLSIndex, MachinePointerInfo());

  SDValue Slot = DAG.getNode(ISD::SHL, DL, PtrVT, TLSIndex,
                              DAG.getConstant(2, DL, MVT::i32));
  SDValue TLS = DAG.getLoad(PtrVT, DL, Chain,
                            DAG.getNode(ISD::ADD, DL, PtrVT, TLSArray, Slot),
                            MachinePointerInfo());

  // Get the offset of the start of the .tls section (section base)
  const auto *GA = cast<GlobalAddressSDNode>(Op);
  auto *CPV = ARMConstantPoolConstant::Create(GA->getGlobal(), ARMCP::SECREL);
  SDValue Offset = DAG.getLoad(
      PtrVT, DL, Chain,
      DAG.getNode(ARMISD::Wrapper, DL, MVT::i32,
                  DAG.getTargetConstantPool(CPV, PtrVT, Align(4))),
      MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));

  return DAG.getNode(ISD::ADD, DL, PtrVT, TLS, Offset);
}

// Lower ISD::GlobalTLSAddress using the "general dynamic" model
SDValue
ARMTargetLowering::LowerToTLSGeneralDynamicModel(GlobalAddressSDNode *GA,
                                                 SelectionDAG &DAG) const {
  SDLoc dl(GA);
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  unsigned char PCAdj = Subtarget->isThumb() ? 4 : 8;
  MachineFunction &MF = DAG.getMachineFunction();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  unsigned ARMPCLabelIndex = AFI->createPICLabelUId();
  ARMConstantPoolValue *CPV =
    ARMConstantPoolConstant::Create(GA->getGlobal(), ARMPCLabelIndex,
                                    ARMCP::CPValue, PCAdj, ARMCP::TLSGD, true);
  SDValue Argument = DAG.getTargetConstantPool(CPV, PtrVT, Align(4));
  Argument = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, Argument);
  Argument = DAG.getLoad(
      PtrVT, dl, DAG.getEntryNode(), Argument,
      MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
  SDValue Chain = Argument.getValue(1);

  SDValue PICLabel = DAG.getConstant(ARMPCLabelIndex, dl, MVT::i32);
  Argument = DAG.getNode(ARMISD::PIC_ADD, dl, PtrVT, Argument, PICLabel);

  // call __tls_get_addr.
  ArgListTy Args;
  ArgListEntry Entry;
  Entry.Node = Argument;
  Entry.Ty = (Type *) Type::getInt32Ty(*DAG.getContext());
  Args.push_back(Entry);

  // FIXME: is there useful debug info available here?
  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl).setChain(Chain).setLibCallee(
      CallingConv::C, Type::getInt32Ty(*DAG.getContext()),
      DAG.getExternalSymbol("__tls_get_addr", PtrVT), std::move(Args));

  std::pair<SDValue, SDValue> CallResult = LowerCallTo(CLI);
  return CallResult.first;
}

// Lower ISD::GlobalTLSAddress using the "initial exec" or
// "local exec" model.
SDValue
ARMTargetLowering::LowerToTLSExecModels(GlobalAddressSDNode *GA,
                                        SelectionDAG &DAG,
                                        TLSModel::Model model) const {
  const GlobalValue *GV = GA->getGlobal();
  SDLoc dl(GA);
  SDValue Offset;
  SDValue Chain = DAG.getEntryNode();
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  // Get the Thread Pointer
  SDValue ThreadPointer = DAG.getNode(ARMISD::THREAD_POINTER, dl, PtrVT);

  if (model == TLSModel::InitialExec) {
    MachineFunction &MF = DAG.getMachineFunction();
    ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
    unsigned ARMPCLabelIndex = AFI->createPICLabelUId();
    // Initial exec model.
    unsigned char PCAdj = Subtarget->isThumb() ? 4 : 8;
    ARMConstantPoolValue *CPV =
      ARMConstantPoolConstant::Create(GA->getGlobal(), ARMPCLabelIndex,
                                      ARMCP::CPValue, PCAdj, ARMCP::GOTTPOFF,
                                      true);
    Offset = DAG.getTargetConstantPool(CPV, PtrVT, Align(4));
    Offset = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, Offset);
    Offset = DAG.getLoad(
        PtrVT, dl, Chain, Offset,
        MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
    Chain = Offset.getValue(1);

    SDValue PICLabel = DAG.getConstant(ARMPCLabelIndex, dl, MVT::i32);
    Offset = DAG.getNode(ARMISD::PIC_ADD, dl, PtrVT, Offset, PICLabel);

    Offset = DAG.getLoad(
        PtrVT, dl, Chain, Offset,
        MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
  } else {
    // local exec model
    assert(model == TLSModel::LocalExec);
    ARMConstantPoolValue *CPV =
      ARMConstantPoolConstant::Create(GV, ARMCP::TPOFF);
    Offset = DAG.getTargetConstantPool(CPV, PtrVT, Align(4));
    Offset = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, Offset);
    Offset = DAG.getLoad(
        PtrVT, dl, Chain, Offset,
        MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
  }

  // The address of the thread local variable is the add of the thread
  // pointer with the offset of the variable.
  return DAG.getNode(ISD::ADD, dl, PtrVT, ThreadPointer, Offset);
}

SDValue
ARMTargetLowering::LowerGlobalTLSAddress(SDValue Op, SelectionDAG &DAG) const {
  GlobalAddressSDNode *GA = cast<GlobalAddressSDNode>(Op);
  if (DAG.getTarget().useEmulatedTLS())
    return LowerToTLSEmulatedModel(GA, DAG);

  if (Subtarget->isTargetDarwin())
    return LowerGlobalTLSAddressDarwin(Op, DAG);

  if (Subtarget->isTargetWindows())
    return LowerGlobalTLSAddressWindows(Op, DAG);

  // TODO: implement the "local dynamic" model
  assert(Subtarget->isTargetELF() && "Only ELF implemented here");
  TLSModel::Model model = getTargetMachine().getTLSModel(GA->getGlobal());

  switch (model) {
    case TLSModel::GeneralDynamic:
    case TLSModel::LocalDynamic:
      return LowerToTLSGeneralDynamicModel(GA, DAG);
    case TLSModel::InitialExec:
    case TLSModel::LocalExec:
      return LowerToTLSExecModels(GA, DAG, model);
  }
  llvm_unreachable("bogus TLS model");
}

/// Return true if all users of V are within function F, looking through
/// ConstantExprs.
static bool allUsersAreInFunction(const Value *V, const Function *F) {
  SmallVector<const User*,4> Worklist(V->users());
  while (!Worklist.empty()) {
    auto *U = Worklist.pop_back_val();
    if (isa<ConstantExpr>(U)) {
      append_range(Worklist, U->users());
      continue;
    }

    auto *I = dyn_cast<Instruction>(U);
    if (!I || I->getParent()->getParent() != F)
      return false;
  }
  return true;
}

static SDValue promoteToConstantPool(const ARMTargetLowering *TLI,
                                     const GlobalValue *GV, SelectionDAG &DAG,
                                     EVT PtrVT, const SDLoc &dl) {
  // If we're creating a pool entry for a constant global with unnamed address,
  // and the global is small enough, we can emit it inline into the constant pool
  // to save ourselves an indirection.
  //
  // This is a win if the constant is only used in one function (so it doesn't
  // need to be duplicated) or duplicating the constant wouldn't increase code
  // size (implying the constant is no larger than 4 bytes).
  const Function &F = DAG.getMachineFunction().getFunction();

  // We rely on this decision to inline being idemopotent and unrelated to the
  // use-site. We know that if we inline a variable at one use site, we'll
  // inline it elsewhere too (and reuse the constant pool entry). Fast-isel
  // doesn't know about this optimization, so bail out if it's enabled else
  // we could decide to inline here (and thus never emit the GV) but require
  // the GV from fast-isel generated code.
  if (!EnableConstpoolPromotion ||
      DAG.getMachineFunction().getTarget().Options.EnableFastISel)
      return SDValue();

  auto *GVar = dyn_cast<GlobalVariable>(GV);
  if (!GVar || !GVar->hasInitializer() ||
      !GVar->isConstant() || !GVar->hasGlobalUnnamedAddr() ||
      !GVar->hasLocalLinkage())
    return SDValue();

  // If we inline a value that contains relocations, we move the relocations
  // from .data to .text. This is not allowed in position-independent code.
  auto *Init = GVar->getInitializer();
  if ((TLI->isPositionIndependent() || TLI->getSubtarget()->isROPI()) &&
      Init->needsDynamicRelocation())
    return SDValue();

  // The constant islands pass can only really deal with alignment requests
  // <= 4 bytes and cannot pad constants itself. Therefore we cannot promote
  // any type wanting greater alignment requirements than 4 bytes. We also
  // can only promote constants that are multiples of 4 bytes in size or
  // are paddable to a multiple of 4. Currently we only try and pad constants
  // that are strings for simplicity.
  auto *CDAInit = dyn_cast<ConstantDataArray>(Init);
  unsigned Size = DAG.getDataLayout().getTypeAllocSize(Init->getType());
  Align PrefAlign = DAG.getDataLayout().getPreferredAlign(GVar);
  unsigned RequiredPadding = 4 - (Size % 4);
  bool PaddingPossible =
    RequiredPadding == 4 || (CDAInit && CDAInit->isString());
  if (!PaddingPossible || PrefAlign > 4 || Size > ConstpoolPromotionMaxSize ||
      Size == 0)
    return SDValue();

  unsigned PaddedSize = Size + ((RequiredPadding == 4) ? 0 : RequiredPadding);
  MachineFunction &MF = DAG.getMachineFunction();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();

  // We can't bloat the constant pool too much, else the ConstantIslands pass
  // may fail to converge. If we haven't promoted this global yet (it may have
  // multiple uses), and promoting it would increase the constant pool size (Sz
  // > 4), ensure we have space to do so up to MaxTotal.
  if (!AFI->getGlobalsPromotedToConstantPool().count(GVar) && Size > 4)
    if (AFI->getPromotedConstpoolIncrease() + PaddedSize - 4 >=
        ConstpoolPromotionMaxTotal)
      return SDValue();

  // This is only valid if all users are in a single function; we can't clone
  // the constant in general. The LLVM IR unnamed_addr allows merging
  // constants, but not cloning them.
  //
  // We could potentially allow cloning if we could prove all uses of the
  // constant in the current function don't care about the address, like
  // printf format strings. But that isn't implemented for now.
  if (!allUsersAreInFunction(GVar, &F))
    return SDValue();

  // We're going to inline this global. Pad it out if needed.
  if (RequiredPadding != 4) {
    StringRef S = CDAInit->getAsString();

    SmallVector<uint8_t,16> V(S.size());
    std::copy(S.bytes_begin(), S.bytes_end(), V.begin());
    while (RequiredPadding--)
      V.push_back(0);
    Init = ConstantDataArray::get(*DAG.getContext(), V);
  }

  auto CPVal = ARMConstantPoolConstant::Create(GVar, Init);
  SDValue CPAddr = DAG.getTargetConstantPool(CPVal, PtrVT, Align(4));
  if (!AFI->getGlobalsPromotedToConstantPool().count(GVar)) {
    AFI->markGlobalAsPromotedToConstantPool(GVar);
    AFI->setPromotedConstpoolIncrease(AFI->getPromotedConstpoolIncrease() +
                                      PaddedSize - 4);
  }
  ++NumConstpoolPromoted;
  return DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, CPAddr);
}

bool ARMTargetLowering::isReadOnly(const GlobalValue *GV) const {
  if (const GlobalAlias *GA = dyn_cast<GlobalAlias>(GV))
    if (!(GV = GA->getAliaseeObject()))
      return false;
  if (const auto *V = dyn_cast<GlobalVariable>(GV))
    return V->isConstant();
  return isa<Function>(GV);
}

SDValue ARMTargetLowering::LowerGlobalAddress(SDValue Op,
                                              SelectionDAG &DAG) const {
  switch (Subtarget->getTargetTriple().getObjectFormat()) {
  default: llvm_unreachable("unknown object format");
  case Triple::COFF:
    return LowerGlobalAddressWindows(Op, DAG);
  case Triple::ELF:
    return LowerGlobalAddressELF(Op, DAG);
  case Triple::MachO:
    return LowerGlobalAddressDarwin(Op, DAG);
  }
}

SDValue ARMTargetLowering::LowerGlobalAddressELF(SDValue Op,
                                                 SelectionDAG &DAG) const {
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDLoc dl(Op);
  const GlobalValue *GV = cast<GlobalAddressSDNode>(Op)->getGlobal();
  bool IsRO = isReadOnly(GV);

  // promoteToConstantPool only if not generating XO text section
  if (GV->isDSOLocal() && !Subtarget->genExecuteOnly())
    if (SDValue V = promoteToConstantPool(this, GV, DAG, PtrVT, dl))
      return V;

  if (isPositionIndependent()) {
    SDValue G = DAG.getTargetGlobalAddress(
        GV, dl, PtrVT, 0, GV->isDSOLocal() ? 0 : ARMII::MO_GOT);
    SDValue Result = DAG.getNode(ARMISD::WrapperPIC, dl, PtrVT, G);
    if (!GV->isDSOLocal())
      Result =
          DAG.getLoad(PtrVT, dl, DAG.getEntryNode(), Result,
                      MachinePointerInfo::getGOT(DAG.getMachineFunction()));
    return Result;
  } else if (Subtarget->isROPI() && IsRO) {
    // PC-relative.
    SDValue G = DAG.getTargetGlobalAddress(GV, dl, PtrVT);
    SDValue Result = DAG.getNode(ARMISD::WrapperPIC, dl, PtrVT, G);
    return Result;
  } else if (Subtarget->isRWPI() && !IsRO) {
    // SB-relative.
    SDValue RelAddr;
    if (Subtarget->useMovt()) {
      ++NumMovwMovt;
      SDValue G = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0, ARMII::MO_SBREL);
      RelAddr = DAG.getNode(ARMISD::Wrapper, dl, PtrVT, G);
    } else { // use literal pool for address constant
      ARMConstantPoolValue *CPV =
        ARMConstantPoolConstant::Create(GV, ARMCP::SBREL);
      SDValue CPAddr = DAG.getTargetConstantPool(CPV, PtrVT, Align(4));
      CPAddr = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, CPAddr);
      RelAddr = DAG.getLoad(
          PtrVT, dl, DAG.getEntryNode(), CPAddr,
          MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
    }
    SDValue SB = DAG.getCopyFromReg(DAG.getEntryNode(), dl, ARM::R9, PtrVT);
    SDValue Result = DAG.getNode(ISD::ADD, dl, PtrVT, SB, RelAddr);
    return Result;
  }

  // If we have T2 ops, we can materialize the address directly via movt/movw
  // pair. This is always cheaper. If need to generate Execute Only code, and we
  // only have Thumb1 available, we can't use a constant pool and are forced to
  // use immediate relocations.
  if (Subtarget->useMovt() || Subtarget->genExecuteOnly()) {
    if (Subtarget->useMovt())
      ++NumMovwMovt;
    // FIXME: Once remat is capable of dealing with instructions with register
    // operands, expand this into two nodes.
    return DAG.getNode(ARMISD::Wrapper, dl, PtrVT,
                       DAG.getTargetGlobalAddress(GV, dl, PtrVT));
  } else {
    SDValue CPAddr = DAG.getTargetConstantPool(GV, PtrVT, Align(4));
    CPAddr = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, CPAddr);
    return DAG.getLoad(
        PtrVT, dl, DAG.getEntryNode(), CPAddr,
        MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));
  }
}

SDValue ARMTargetLowering::LowerGlobalAddressDarwin(SDValue Op,
                                                    SelectionDAG &DAG) const {
  assert(!Subtarget->isROPI() && !Subtarget->isRWPI() &&
         "ROPI/RWPI not currently supported for Darwin");
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDLoc dl(Op);
  const GlobalValue *GV = cast<GlobalAddressSDNode>(Op)->getGlobal();

  if (Subtarget->useMovt())
    ++NumMovwMovt;

  // FIXME: Once remat is capable of dealing with instructions with register
  // operands, expand this into multiple nodes
  unsigned Wrapper =
      isPositionIndependent() ? ARMISD::WrapperPIC : ARMISD::Wrapper;

  SDValue G = DAG.getTargetGlobalAddress(GV, dl, PtrVT, 0, ARMII::MO_NONLAZY);
  SDValue Result = DAG.getNode(Wrapper, dl, PtrVT, G);

  if (Subtarget->isGVIndirectSymbol(GV))
    Result = DAG.getLoad(PtrVT, dl, DAG.getEntryNode(), Result,
                         MachinePointerInfo::getGOT(DAG.getMachineFunction()));
  return Result;
}

SDValue ARMTargetLowering::LowerGlobalAddressWindows(SDValue Op,
                                                     SelectionDAG &DAG) const {
  assert(Subtarget->isTargetWindows() && "non-Windows COFF is not supported");
  assert(Subtarget->useMovt() &&
         "Windows on ARM expects to use movw/movt");
  assert(!Subtarget->isROPI() && !Subtarget->isRWPI() &&
         "ROPI/RWPI not currently supported for Windows");

  const TargetMachine &TM = getTargetMachine();
  const GlobalValue *GV = cast<GlobalAddressSDNode>(Op)->getGlobal();
  ARMII::TOF TargetFlags = ARMII::MO_NO_FLAG;
  if (GV->hasDLLImportStorageClass())
    TargetFlags = ARMII::MO_DLLIMPORT;
  else if (!TM.shouldAssumeDSOLocal(GV))
    TargetFlags = ARMII::MO_COFFSTUB;
  EVT PtrVT = getPointerTy(DAG.getDataLayout());
  SDValue Result;
  SDLoc DL(Op);

  ++NumMovwMovt;

  // FIXME: Once remat is capable of dealing with instructions with register
  // operands, expand this into two nodes.
  Result = DAG.getNode(ARMISD::Wrapper, DL, PtrVT,
                       DAG.getTargetGlobalAddress(GV, DL, PtrVT, /*offset=*/0,
                                                  TargetFlags));
  if (TargetFlags & (ARMII::MO_DLLIMPORT | ARMII::MO_COFFSTUB))
    Result = DAG.getLoad(PtrVT, DL, DAG.getEntryNode(), Result,
                         MachinePointerInfo::getGOT(DAG.getMachineFunction()));
  return Result;
}

SDValue
ARMTargetLowering::LowerEH_SJLJ_SETJMP(SDValue Op, SelectionDAG &DAG) const {
  SDLoc dl(Op);
  SDValue Val = DAG.getConstant(0, dl, MVT::i32);
  return DAG.getNode(ARMISD::EH_SJLJ_SETJMP, dl,
                     DAG.getVTList(MVT::i32, MVT::Other), Op.getOperand(0),
                     Op.getOperand(1), Val);
}

SDValue
ARMTargetLowering::LowerEH_SJLJ_LONGJMP(SDValue Op, SelectionDAG &DAG) const {
  SDLoc dl(Op);
  return DAG.getNode(ARMISD::EH_SJLJ_LONGJMP, dl, MVT::Other, Op.getOperand(0),
                     Op.getOperand(1), DAG.getConstant(0, dl, MVT::i32));
}

SDValue ARMTargetLowering::LowerEH_SJLJ_SETUP_DISPATCH(SDValue Op,
                                                      SelectionDAG &DAG) const {
  SDLoc dl(Op);
  return DAG.getNode(ARMISD::EH_SJLJ_SETUP_DISPATCH, dl, MVT::Other,
                     Op.getOperand(0));
}

SDValue ARMTargetLowering::LowerINTRINSIC_VOID(
    SDValue Op, SelectionDAG &DAG, const ARMSubtarget *Subtarget) const {
  unsigned IntNo =
      Op.getConstantOperandVal(Op.getOperand(0).getValueType() == MVT::Other);
  switch (IntNo) {
    default:
      return SDValue();  // Don't custom lower most intrinsics.
    case Intrinsic::arm_gnu_eabi_mcount: {
      MachineFunction &MF = DAG.getMachineFunction();
      EVT PtrVT = getPointerTy(DAG.getDataLayout());
      SDLoc dl(Op);
      SDValue Chain = Op.getOperand(0);
      // call "\01__gnu_mcount_nc"
      const ARMBaseRegisterInfo *ARI = Subtarget->getRegisterInfo();
      const uint32_t *Mask =
          ARI->getCallPreservedMask(DAG.getMachineFunction(), CallingConv::C);
      assert(Mask && "Missing call preserved mask for calling convention");
      // Mark LR an implicit live-in.
      Register Reg = MF.addLiveIn(ARM::LR, getRegClassFor(MVT::i32));
      SDValue ReturnAddress =
          DAG.getCopyFromReg(DAG.getEntryNode(), dl, Reg, PtrVT);
      constexpr EVT ResultTys[] = {MVT::Other, MVT::Glue};
      SDValue Callee =
          DAG.getTargetExternalSymbol("\01__gnu_mcount_nc", PtrVT, 0);
      SDValue RegisterMask = DAG.getRegisterMask(Mask);
      if (Subtarget->isThumb())
        return SDValue(
            DAG.getMachineNode(
                ARM::tBL_PUSHLR, dl, ResultTys,
                {ReturnAddress, DAG.getTargetConstant(ARMCC::AL, dl, PtrVT),
                 DAG.getRegister(0, PtrVT), Callee, RegisterMask, Chain}),
            0);
      return SDValue(
          DAG.getMachineNode(ARM::BL_PUSHLR, dl, ResultTys,
                             {ReturnAddress, Callee, RegisterMask, Chain}),
          0);
    }
  }
}

SDValue
ARMTargetLowering::LowerINTRINSIC_WO_CHAIN(SDValue Op, SelectionDAG &DAG,
                                          const ARMSubtarget *Subtarget) const {
  unsigned IntNo = Op.getConstantOperandVal(0);
  SDLoc dl(Op);
  switch (IntNo) {
  default: return SDValue();    // Don't custom lower most intrinsics.
  case Intrinsic::thread_pointer: {
    EVT PtrVT = getPointerTy(DAG.getDataLayout());
    return DAG.getNode(ARMISD::THREAD_POINTER, dl, PtrVT);
  }
  case Intrinsic::arm_cls: {
    const SDValue &Operand = Op.getOperand(1);
    const EVT VTy = Op.getValueType();
    SDValue SRA =
        DAG.getNode(ISD::SRA, dl, VTy, Operand, DAG.getConstant(31, dl, VTy));
    SDValue XOR = DAG.getNode(ISD::XOR, dl, VTy, SRA, Operand);
    SDValue SHL =
        DAG.getNode(ISD::SHL, dl, VTy, XOR, DAG.getConstant(1, dl, VTy));
    SDValue OR =
        DAG.getNode(ISD::OR, dl, VTy, SHL, DAG.getConstant(1, dl, VTy));
    SDValue Result = DAG.getNode(ISD::CTLZ, dl, VTy, OR);
    return Result;
  }
  case Intrinsic::arm_cls64: {
    // cls(x) = if cls(hi(x)) != 31 then cls(hi(x))
    //          else 31 + clz(if hi(x) == 0 then lo(x) else not(lo(x)))
    const SDValue &Operand = Op.getOperand(1);
    const EVT VTy = Op.getValueType();
    SDValue Lo, Hi;
    std::tie(Lo, Hi) = DAG.SplitScalar(Operand, dl, VTy, VTy);
    SDValue Constant0 = DAG.getConstant(0, dl, VTy);
    SDValue Constant1 = DAG.getConstant(1, dl, VTy);
    SDValue Constant31 = DAG.getConstant(31, dl, VTy);
    SDValue SRAHi = DAG.getNode(ISD::SRA, dl, VTy, Hi, Constant31);
    SDValue XORHi = DAG.getNode(ISD::XOR, dl, VTy, SRAHi, Hi);
    SDValue SHLHi = DAG.getNode(ISD::SHL, dl, VTy, XORHi, Constant1);
    SDValue ORHi = DAG.getNode(ISD::OR, dl, VTy, SHLHi, Constant1);
    SDValue CLSHi = DAG.getNode(ISD::CTLZ, dl, VTy, ORHi);
    SDValue CheckLo =
        DAG.getSetCC(dl, MVT::i1, CLSHi, Constant31, ISD::CondCode::SETEQ);
    SDValue HiIsZero =
        DAG.getSetCC(dl, MVT::i1, Hi, Constant0, ISD::CondCode::SETEQ);
    SDValue AdjustedLo =
        DAG.getSelect(dl, VTy, HiIsZero, Lo, DAG.getNOT(dl, Lo, VTy));
    SDValue CLZAdjustedLo = DAG.getNode(ISD::CTLZ, dl, VTy, AdjustedLo);
    SDValue Result =
        DAG.getSelect(dl, VTy, CheckLo,
                      DAG.getNode(ISD::ADD, dl, VTy, CLZAdjustedLo, Constant31), CLSHi);
    return Result;
  }
  case Intrinsic::eh_sjlj_lsda: {
    MachineFunction &MF = DAG.getMachineFunction();
    ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
    unsigned ARMPCLabelIndex = AFI->createPICLabelUId();
    EVT PtrVT = getPointerTy(DAG.getDataLayout());
    SDValue CPAddr;
    bool IsPositionIndependent = isPositionIndependent();
    unsigned PCAdj = IsPositionIndependent ? (Subtarget->isThumb() ? 4 : 8) : 0;
    ARMConstantPoolValue *CPV =
      ARMConstantPoolConstant::Create(&MF.getFunction(), ARMPCLabelIndex,
                                      ARMCP::CPLSDA, PCAdj);
    CPAddr = DAG.getTargetConstantPool(CPV, PtrVT, Align(4));
    CPAddr = DAG.getNode(ARMISD::Wrapper, dl, MVT::i32, CPAddr);
    SDValue Result = DAG.getLoad(
        PtrVT, dl, DAG.getEntryNode(), CPAddr,
        MachinePointerInfo::getConstantPool(DAG.getMachineFunction()));

    if (IsPositionIndependent) {
      SDValue PICLabel = DAG.getConstant(ARMPCLabelIndex, dl, MVT::i32);
      Result = DAG.getNode(ARMISD::PIC_ADD, dl, PtrVT, Result, PICLabel);
    }
    return Result;
  }
  case Intrinsic::arm_neon_vabs:
    return DAG.getNode(ISD::ABS, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1));
  case Intrinsic::arm_neon_vabds:
    if (Op.getValueType().isInteger())
      return DAG.getNode(ISD::ABDS, SDLoc(Op), Op.getValueType(),
                         Op.getOperand(1), Op.getOperand(2));
    return SDValue();
  case Intrinsic::arm_neon_vabdu:
    return DAG.getNode(ISD::ABDU, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1), Op.getOperand(2));
  case Intrinsic::arm_neon_vmulls:
  case Intrinsic::arm_neon_vmullu: {
    unsigned NewOpc = (IntNo == Intrinsic::arm_neon_vmulls)
      ? ARMISD::VMULLs : ARMISD::VMULLu;
    return DAG.getNode(NewOpc, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1), Op.getOperand(2));
  }
  case Intrinsic::arm_neon_vminnm:
  case Intrinsic::arm_neon_vmaxnm: {
    unsigned NewOpc = (IntNo == Intrinsic::arm_neon_vminnm)
      ? ISD::FMINNUM : ISD::FMAXNUM;
    return DAG.getNode(NewOpc, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1), Op.getOperand(2));
  }
  case Intrinsic::arm_neon_vminu:
  case Intrinsic::arm_neon_vmaxu: {
    if (Op.getValueType().isFloatingPoint())
      return SDValue();
    unsigned NewOpc = (IntNo == Intrinsic::arm_neon_vminu)
      ? ISD::UMIN : ISD::UMAX;
    return DAG.getNode(NewOpc, SDLoc(Op), Op.getValueType(),
                         Op.getOperand(1), Op.getOperand(2));
  }
  case Intrinsic::arm_neon_vmins:
  case Intrinsic::arm_neon_vmaxs: {
    // v{min,max}s is overloaded between signed integers and floats.
    if (!Op.getValueType().isFloatingPoint()) {
      unsigned NewOpc = (IntNo == Intrinsic::arm_neon_vmins)
        ? ISD::SMIN : ISD::SMAX;
      return DAG.getNode(NewOpc, SDLoc(Op), Op.getValueType(),
                         Op.getOperand(1), Op.getOperand(2));
    }
    unsigned NewOpc = (IntNo == Intrinsic::arm_neon_vmins)
      ? ISD::FMINIMUM : ISD::FMAXIMUM;
    return DAG.getNode(NewOpc, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1), Op.getOperand(2));
  }
  case Intrinsic::arm_neon_vtbl1:
    return DAG.getNode(ARMISD::VTBL1, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1), Op.getOperand(2));
  case Intrinsic::arm_neon_vtbl2:
    return DAG.getNode(ARMISD::VTBL2, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1), Op.getOperand(2), Op.getOperand(3));
  case Intrinsic::arm_mve_pred_i2v:
  case Intrinsic::arm_mve_pred_v2i:
    return DAG.getNode(ARMISD::PREDICATE_CAST, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1));
  case Intrinsic::arm_mve_vreinterpretq:
    return DAG.getNode(ARMISD::VECTOR_REG_CAST, SDLoc(Op), Op.getValueType(),
                       Op.getOperand(1));
  case Intrinsic::arm_mve_lsll:
    return DAG.getNode(ARMISD::LSLL, SDLoc(Op), Op->getVTList(),
                       Op.getOperand(1), Op.getOperand(2), Op.getOperand(3));
  case Intrinsic::arm_mve_asrl:
    return DAG.getNode(ARMISD::ASRL, SDLoc(Op), Op->getVTList(),
                       Op.getOperand(1), Op.getOperand(2), Op.getOperand(3));
  }
}

static SDValue LowerATOMIC_FENCE(SDValue Op, SelectionDAG &DAG,
                                 const ARMSubtarget *Subtarget) {
  SDLoc dl(Op);
  auto SSID = static_cast<SyncScope::ID>(Op.getConstantOperandVal(2));
  if (SSID == SyncScope::SingleThread)
    return Op;

  if (!Subtarget->hasDataBarrier()) {
    // Some ARMv6 cpus can support data barriers with an mcr instruction.
    // Thumb1 and pre-v6 ARM mode use a libcall instead and should never get
    // here.
    assert(Subtarget->hasV6Ops() && !Subtarget->isThumb() &&
           "Unexpected ISD::ATOMIC_FENCE encountered. Should be libcall!");
    return DAG.getNode(ARMISD::MEMBARRIER_MCR, dl, MVT::Other, Op.getOperand(0),
                       DAG.getConstant(0, dl, MVT::i32));
  }

  AtomicOrdering Ord =
      static_cast<AtomicOrdering>(Op.getConstantOperandVal(1));
  ARM_MB::MemBOpt Domain = ARM_MB::ISH;
  if (Subtarget->isMClass()) {
    // Only a full system barrier exists in the M-class architectures.
    Domain = ARM_MB::SY;
  } else if (Subtarget->preferISHSTBarriers() &&
             Ord == AtomicOrdering::Release) {
    // Swift happens to implement ISHST barriers in a way that's compatible with
    // Release semantics but weaker than ISH so we'd be fools not to use
    // it. Beware: other processors probably don't!
    Domain = ARM_MB::ISHST;
  }

  return DAG.getNode(ISD::INTRINSIC_VOID, dl, MVT::Other, Op.getOperand(0),
                     DAG.getConstant(Intrinsic::arm_dmb, dl, MVT::i32),
                     DAG.getConstant(Domain, dl, MVT::i32));
}

static SDValue LowerPREFETCH(SDValue Op, SelectionDAG &DAG,
                             const ARMSubtarget *Subtarget) {
  // ARM pre v5TE and Thumb1 does not have preload instructions.
  if (!(Subtarget->isThumb2() ||
        (!Subtarget->isThumb1Only() && Subtarget->hasV5TEOps())))
    // Just preserve the chain.
    return Op.getOperand(0);

  SDLoc dl(Op);
  unsigned isRead = ~Op.getConstantOperandVal(2) & 1;
  if (!isRead &&
      (!Subtarget->hasV7Ops() || !Subtarget->hasMPExtension()))
    // ARMv7 with MP extension has PLDW.
    return Op.getOperand(0);

  unsigned isData = Op.getConstantOperandVal(4);
  if (Subtarget->isThumb()) {
    // Invert the bits.
    isRead = ~isRead & 1;
    isData = ~isData & 1;
  }

  return DAG.getNode(ARMISD::PRELOAD, dl, MVT::Other, Op.getOperand(0),
                     Op.getOperand(1), DAG.getConstant(isRead, dl, MVT::i32),
                     DAG.getConstant(isData, dl, MVT::i32));
}

static SDValue LowerVASTART(SDValue Op, SelectionDAG &DAG) {
  MachineFunction &MF = DAG.getMachineFunction();
  ARMFunctionInfo *FuncInfo = MF.getInfo<ARMFunctionInfo>();

  // vastart just stores the address of the VarArgsFrameIndex slot into the
  // memory location argument.
  SDLoc dl(Op);
  EVT PtrVT = DAG.getTargetLoweringInfo().getPointerTy(DAG.getDataLayout());
  SDValue FR = DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), PtrVT);
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), dl, FR, Op.getOperand(1),
                      MachinePointerInfo(SV));
}

SDValue ARMTargetLowering::GetF64FormalArgument(CCValAssign &VA,
                                                CCValAssign &NextVA,
                                                SDValue &Root,
                                                SelectionDAG &DAG,
                                                const SDLoc &dl) const {
  MachineFunction &MF = DAG.getMachineFunction();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();

  const TargetRegisterClass *RC;
  if (AFI->isThumb1OnlyFunction())
    RC = &ARM::tGPRRegClass;
  else
    RC = &ARM::GPRRegClass;

  // Transform the arguments stored in physical registers into virtual ones.
  Register Reg = MF.addLiveIn(VA.getLocReg(), RC);
  SDValue ArgValue = DAG.getCopyFromReg(Root, dl, Reg, MVT::i32);

  SDValue ArgValue2;
  if (NextVA.isMemLoc()) {
    MachineFrameInfo &MFI = MF.getFrameInfo();
    int FI = MFI.CreateFixedObject(4, NextVA.getLocMemOffset(), true);

    // Create load node to retrieve arguments from the stack.
    SDValue FIN = DAG.getFrameIndex(FI, getPointerTy(DAG.getDataLayout()));
    ArgValue2 = DAG.getLoad(
        MVT::i32, dl, Root, FIN,
        MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI));
  } else {
    Reg = MF.addLiveIn(NextVA.getLocReg(), RC);
    ArgValue2 = DAG.getCopyFromReg(Root, dl, Reg, MVT::i32);
  }
  if (!Subtarget->isLittle())
    std::swap (ArgValue, ArgValue2);
  return DAG.getNode(ARMISD::VMOVDRR, dl, MVT::f64, ArgValue, ArgValue2);
}

// The remaining GPRs hold either the beginning of variable-argument
// data, or the beginning of an aggregate passed by value (usually
// byval).  Either way, we allocate stack slots adjacent to the data
// provided by our caller, and store the unallocated registers there.
// If this is a variadic function, the va_list pointer will begin with
// these values; otherwise, this reassembles a (byval) structure that
// was split between registers and memory.
// Return: The frame index registers were stored into.
int ARMTargetLowering::StoreByValRegs(CCState &CCInfo, SelectionDAG &DAG,
                                      const SDLoc &dl, SDValue &Chain,
                                      const Value *OrigArg,
                                      unsigned InRegsParamRecordIdx,
                                      int ArgOffset, unsigned ArgSize) const {
  // Currently, two use-cases possible:
  // Case #1. Non-var-args function, and we meet first byval parameter.
  //          Setup first unallocated register as first byval register;
  //          eat all remained registers
  //          (these two actions are performed by HandleByVal method).
  //          Then, here, we initialize stack frame with
  //          "store-reg" instructions.
  // Case #2. Var-args function, that doesn't contain byval parameters.
  //          The same: eat all remained unallocated registers,
  //          initialize stack frame.

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();
  unsigned RBegin, REnd;
  if (InRegsParamRecordIdx < CCInfo.getInRegsParamsCount()) {
    CCInfo.getInRegsParamInfo(InRegsParamRecordIdx, RBegin, REnd);
  } else {
    unsigned RBeginIdx = CCInfo.getFirstUnallocated(GPRArgRegs);
    RBegin = RBeginIdx == 4 ? (unsigned)ARM::R4 : GPRArgRegs[RBeginIdx];
    REnd = ARM::R4;
  }

  if (REnd != RBegin)
    ArgOffset = -4 * (ARM::R4 - RBegin);

  auto PtrVT = getPointerTy(DAG.getDataLayout());
  int FrameIndex = MFI.CreateFixedObject(ArgSize, ArgOffset, false);
  SDValue FIN = DAG.getFrameIndex(FrameIndex, PtrVT);

  SmallVector<SDValue, 4> MemOps;
  const TargetRegisterClass *RC =
      AFI->isThumb1OnlyFunction() ? &ARM::tGPRRegClass : &ARM::GPRRegClass;

  for (unsigned Reg = RBegin, i = 0; Reg < REnd; ++Reg, ++i) {
    Register VReg = MF.addLiveIn(Reg, RC);
    SDValue Val = DAG.getCopyFromReg(Chain, dl, VReg, MVT::i32);
    SDValue Store = DAG.getStore(Val.getValue(1), dl, Val, FIN,
                                 MachinePointerInfo(OrigArg, 4 * i));
    MemOps.push_back(Store);
    FIN = DAG.getNode(ISD::ADD, dl, PtrVT, FIN, DAG.getConstant(4, dl, PtrVT));
  }

  if (!MemOps.empty())
    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other, MemOps);
  return FrameIndex;
}

// Setup stack frame, the va_list pointer will start from.
void ARMTargetLowering::VarArgStyleRegisters(CCState &CCInfo, SelectionDAG &DAG,
                                             const SDLoc &dl, SDValue &Chain,
                                             unsigned ArgOffset,
                                             unsigned TotalArgRegsSaveSize,
                                             bool ForceMutable) const {
  MachineFunction &MF = DAG.getMachineFunction();
  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();

  // Try to store any remaining integer argument regs
  // to their spots on the stack so that they may be loaded by dereferencing
  // the result of va_next.
  // If there is no regs to be stored, just point address after last
  // argument passed via stack.
  int FrameIndex = StoreByValRegs(
      CCInfo, DAG, dl, Chain, nullptr, CCInfo.getInRegsParamsCount(),
      CCInfo.getStackSize(), std::max(4U, TotalArgRegsSaveSize));
  AFI->setVarArgsFrameIndex(FrameIndex);
}

bool ARMTargetLowering::splitValueIntoRegisterParts(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Val, SDValue *Parts,
    unsigned NumParts, MVT PartVT, std::optional<CallingConv::ID> CC) const {
  EVT ValueVT = Val.getValueType();
  if ((ValueVT == MVT::f16 || ValueVT == MVT::bf16) && PartVT == MVT::f32) {
    unsigned ValueBits = ValueVT.getSizeInBits();
    unsigned PartBits = PartVT.getSizeInBits();
    Val = DAG.getNode(ISD::BITCAST, DL, MVT::getIntegerVT(ValueBits), Val);
    Val = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::getIntegerVT(PartBits), Val);
    Val = DAG.getNode(ISD::BITCAST, DL, PartVT, Val);
    Parts[0] = Val;
    return true;
  }
  return false;
}

SDValue ARMTargetLowering::joinRegisterPartsIntoValue(
    SelectionDAG &DAG, const SDLoc &DL, const SDValue *Parts, unsigned NumParts,
    MVT PartVT, EVT ValueVT, std::optional<CallingConv::ID> CC) const {
  if ((ValueVT == MVT::f16 || ValueVT == MVT::bf16) && PartVT == MVT::f32) {
    unsigned ValueBits = ValueVT.getSizeInBits();
    unsigned PartBits = PartVT.getSizeInBits();
    SDValue Val = Parts[0];

    Val = DAG.getNode(ISD::BITCAST, DL, MVT::getIntegerVT(PartBits), Val);
    Val = DAG.getNode(ISD::TRUNCATE, DL, MVT::getIntegerVT(ValueBits), Val);
    Val = DAG.getNode(ISD::BITCAST, DL, ValueVT, Val);
    return Val;
  }
  return SDValue();
}

SDValue ARMTargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &dl,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  ARMFunctionInfo *AFI = MF.getInfo<ARMFunctionInfo>();

  // Assign locations to all of the incoming arguments.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeFormalArguments(Ins, CCAssignFnForCall(CallConv, isVarArg));

  Function::const_arg_iterator CurOrigArg = MF.getFunction().arg_begin();
  unsigned CurArgIdx = 0;

  // Initially ArgRegsSaveSize is zero.
  // Then we increase this value each time we meet byval parameter.
  // We also increase this value in case of varargs function.
  AFI->setArgRegsSaveSize(0);

  // Calculate the amount of stack space that we need to allocate to store
  // byval and variadic arguments that are passed in registers.
  // We need to know this before we allocate the first byval or variadic
  // argument, as they will be allocated a stack slot below the CFA (Canonical
  // Frame Address, the stack pointer at entry to the function).
  unsigned ArgRegBegin = ARM::R4;
  for (const CCValAssign &VA : ArgLocs) {
    if (CCInfo.getInRegsParamsProcessed() >= CCInfo.getInRegsParamsCount())
      break;

    unsigned Index = VA.getValNo();
    ISD::ArgFlagsTy Flags = Ins[Index].Flags;
    if (!Flags.isByVal())
      continue;

    assert(VA.isMemLoc() && "unexpected byval pointer in reg");
    unsigned RBegin, REnd;
    CCInfo.getInRegsParamInfo(CCInfo.getInRegsParamsProcessed(), RBegin, REnd);
    ArgRegBegin = std::min(ArgRegBegin, RBegin);

    CCInfo.nextInRegsParam();
  }
  CCInfo.rewindByValRegsInfo();

  int lastInsIndex = -1;
  if (isVarArg && MFI.hasVAStart()) {
    unsigned RegIdx = CCInfo.getFirstUnallocated(GPRArgRegs);
    if (RegIdx != std::size(GPRArgRegs))
      ArgRegBegin = std::min(ArgRegBegin, (unsigned)GPRArgRegs[RegIdx]);
  }

  unsigned TotalArgRegsSaveSize = 4 * (ARM::R4 - ArgRegBegin);
  AFI->setArgRegsSaveSize(TotalArgRegsSaveSize);
  auto PtrVT = getPointerTy(DAG.getDataLayout());

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    if (Ins[VA.getValNo()].isOrigArg()) {
      std::advance(CurOrigArg,
                   Ins[VA.getValNo()].getOrigArgIndex() - CurArgIdx);
      CurArgIdx = Ins[VA.getValNo()].getOrigArgIndex();
    }
    // Arguments stored in registers.
    if (VA.isRegLoc()) {
      EVT RegVT = VA.getLocVT();
      SDValue ArgValue;

      if (VA.needsCustom() && VA.getLocVT() == MVT::v2f64) {
        // f64 and vector types are split up into multiple registers or
        // combinations of registers and stack slots.
        SDValue ArgValue1 =
            GetF64FormalArgument(VA, ArgLocs[++i], Chain, DAG, dl);
        VA = ArgLocs[++i]; // skip ahead to next loc
        SDValue ArgValue2;
        if (VA.isMemLoc()) {
          int FI = MFI.CreateFixedObject(8, VA.getLocMemOffset(), true);
          SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
          ArgValue2 = DAG.getLoad(
              MVT::f64, dl, Chain, FIN,
              MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI));
        } else {
          ArgValue2 = GetF64FormalArgument(VA, ArgLocs[++i], Chain, DAG, dl);
        }
        ArgValue = DAG.getNode(ISD::UNDEF, dl, MVT::v2f64);
        ArgValue = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, MVT::v2f64, ArgValue,
                               ArgValue1, DAG.getIntPtrConstant(0, dl));
        ArgValue = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, MVT::v2f64, ArgValue,
                               ArgValue2, DAG.getIntPtrConstant(1, dl));
      } else if (VA.needsCustom() && VA.getLocVT() == MVT::f64) {
        ArgValue = GetF64FormalArgument(VA, ArgLocs[++i], Chain, DAG, dl);
      } else {
        const TargetRegisterClass *RC;

        if (RegVT == MVT::f16 || RegVT == MVT::bf16)
          RC = &ARM::HPRRegClass;
        else if (RegVT == MVT::f32)
          RC = &ARM::SPRRegClass;
        else if (RegVT == MVT::f64 || RegVT == MVT::v4f16 ||
                 RegVT == MVT::v4bf16)
          RC = &ARM::DPRRegClass;
        else if (RegVT == MVT::v2f64 || RegVT == MVT::v8f16 ||
                 RegVT == MVT::v8bf16)
          RC = &ARM::QPRRegClass;
        else if (RegVT == MVT::i32)
          RC = AFI->isThumb1OnlyFunction() ? &ARM::tGPRRegClass
                                           : &ARM::GPRRegClass;
        else
          llvm_unreachable("RegVT not supported by FORMAL_ARGUMENTS Lowering");

        // Transform the arguments in physical registers into virtual ones.
        Register Reg = MF.addLiveIn(VA.getLocReg(), RC);
        ArgValue = DAG.getCopyFromReg(Chain, dl, Reg, RegVT);

        // If this value is passed in r0 and has the returned attribute (e.g.
        // C++ 'structors), record this fact for later use.
        if (VA.getLocReg() == ARM::R0 && Ins[VA.getValNo()].Flags.isReturned()) {
          AFI->setPreservesR0();
        }
      }

      // If this is an 8 or 16-bit value, it is really passed promoted
      // to 32 bits.  Insert an assert[sz]ext to capture this, then
      // truncate to the right size.
      switch (VA.getLocInfo()) {
      default: llvm_unreachable("Unknown loc info!");
      case CCValAssign::Full: break;
      case CCValAssign::BCvt:
        ArgValue = DAG.getNode(ISD::BITCAST, dl, VA.getValVT(), ArgValue);
        break;
      }

      // f16 arguments have their size extended to 4 bytes and passed as if they
      // had been copied to the LSBs of a 32-bit register.
      // For that, it's passed extended to i32 (soft ABI) or to f32 (hard ABI)
      if (VA.needsCustom() &&
          (VA.getValVT() == MVT::f16 || VA.getValVT() == MVT::bf16))
        ArgValue = MoveToHPR(dl, DAG, VA.getLocVT(), VA.getValVT(), ArgValue);

      // On CMSE Entry Functions, formal integer arguments whose bitwidth is
      // less than 32 bits must be sign- or zero-extended in the callee for
      // security reasons. Although the ABI mandates an extension done by the
      // caller, the latter cannot be trusted to follow the rules of the ABI.
      const ISD::InputArg &Arg = Ins[VA.getValNo()];
      if (AFI->isCmseNSEntryFunction() && Arg.ArgVT.isScalarInteger() &&
          RegVT.isScalarInteger() && Arg.ArgVT.bitsLT(MVT::i32))
        ArgValue = handleCMSEValue(ArgValue, Arg, DAG, dl);

      InVals.push_back(ArgValue);
    } else { // VA.isRegLoc()
      // Only arguments passed on the stack should make it here.
      assert(VA.isMemLoc());
      assert(VA.getValVT() != MVT::i64 && "i64 should already be lowered");

      int index = VA.getValNo();

      // Some Ins[] entries become multiple ArgLoc[] entries.
      // Process them only once.
      if (index != lastInsIndex)
        {
          ISD::ArgFlagsTy Flags = Ins[index].Flags;
          // FIXME: For now, all byval parameter objects are marked mutable.
          // This can be changed with more analysis.
          // In case of tail call optimization mark all arguments mutable.
          // Since they could be overwritten by lowering of arguments in case of
          // a tail call.
          if (Flags.isByVal()) {
            assert(Ins[index].isOrigArg() &&
                   "Byval arguments cannot be implicit");
            unsigned CurByValIndex = CCInfo.getInRegsParamsProcessed();

            int FrameIndex = StoreByValRegs(
                CCInfo, DAG, dl, Chain, &*CurOrigArg, CurByValIndex,
                VA.getLocMemOffset(), Flags.getByValSize());
            InVals.push_back(DAG.getFrameIndex(FrameIndex, PtrVT));
            CCInfo.nextInRegsParam();
          } else {
            unsigned FIOffset = VA.getLocMemOffset();
            int FI = MFI.CreateFixedObject(VA.getLocVT().getSizeInBits()/8,
                                           FIOffset, true);

            // Create load nodes to retrieve arguments from the stack.
            SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
            InVals.push_back(DAG.getLoad(VA.getValVT(), dl, Chain, FIN,
                                         MachinePointerInfo::getFixedStack(
                                             DAG.getMachineFunction(), FI)));
          }
          lastInsIndex = index;
        }
    }
  }

  // varargs
  if (isVarArg && MFI.hasVAStart()) {
    VarArgStyleRegisters(CCInfo, DAG, dl, Chain, CCInfo.getStackSize(),
                         TotalArgRegsSaveSize);
    if (AFI->isCmseNSEntryFunction()) {
      DiagnosticInfoUnsupported Diag(
          DAG.getMachineFunction().getFunction(),
          "secure entry function must not be variadic", dl.getDebugLoc());
      DAG.getContext()->diagnose(Diag);
    }
  }

  unsigned StackArgSize = CCInfo.getStackSize();
  bool TailCallOpt = MF.getTarget().Options.GuaranteedTailCallOpt;
  if (canGuaranteeTCO(CallConv, TailCallOpt)) {
    // The only way to guarantee a tail call is if the callee restores its
    // argument area, but it must also keep the stack aligned when doing so.
    const DataLayout &DL = DAG.getDataLayout();
    StackArgSize = alignTo(StackArgSize, DL.getStackAlignment());

    AFI->setArgumentStackToRestore(StackArgSize);
  }
  AFI->setArgumentStackSize(StackArgSize);

  if (CCInfo.getStackSize() > 0 && AFI->isCmseNSEntryFunction()) {
    DiagnosticInfoUnsupported Diag(
        DAG.getMachineFunction().getFunction(),
        "secure entry function requires arguments on stack", dl.getDebugLoc());
    DAG.getContext()->diagnose(Diag);
  }

  return Chain;
}

/// isFloatingPointZero - Return true if this is +0.0.
static bool isFloatingPointZero(SDValue Op) {
  if (ConstantFPSDNode *CFP = dyn_cast<ConstantFPSDNode>(Op))
    return CFP->getValueAPF().isPosZero();
  else if (ISD::isEXTLoad(Op.getNode()) || ISD::isNON_EXTLoad(Op.getNode())) {
    // Maybe this has already been legalized into the constant pool?
    if (Op.getOperand(1).getOpcode() == ARMISD::Wrapper) {
      SDValue WrapperOp = Op.getOperand(1).getOperand(0);
      if (ConstantPoolSDNode *CP = dyn_cast<ConstantPoolSDNode>(WrapperOp))
        if (const ConstantFP *CFP = dyn_cast<ConstantFP>(CP->getConstVal()))
          return CFP->getValueAPF().isPosZero();
    }
  } else if (Op->getOpcode() == ISD::BITCAST &&
             Op->getValueType(0) == MVT::f64) {
    // Handle (ISD::BITCAST (ARMISD::VMOVIMM (ISD::TargetConstant 0)) MVT::f64)
    // created by LowerConstantFP().
    SDValue BitcastOp = Op->getOperand(0);
    if (BitcastOp->getOpcode() == ARMISD::VMOVIMM &&
        isNullConstant(BitcastOp->getOperand(0)))
      return true;
  }
  return false;
}

/// Returns appropriate ARM CMP (cmp) and corresponding condition code for
/// the given operands.
SDValue ARMTargetLowering::getARMCmp(SDValue LHS, SDValue RHS, ISD::CondCode CC,
                                     SDValue &ARMcc, SelectionDAG &DAG,
                                     const SDLoc &dl) const {
  if (ConstantSDNode *RHSC = dyn_cast<ConstantSDNode>(RHS.getNode())) {
    unsigned C = RHSC->getZExtValue();
    if (!isLegalICmpImmediate((int32_t)C)) {
      // Constant does not fit, try adjusting it by one.
      switch (CC) {
      default: break;
      case ISD::SETLT:
      case ISD::SETGE:
        if (C != 0x80000000 && isLegalICmpImmediate(C-1)) {
          CC = (CC == ISD::SETLT) ? ISD::SETLE : ISD::SETGT;
          RHS = DAG.getConstant(C - 1, dl, MVT::i32);
        }
        break;
      case ISD::SETULT:
      case ISD::SETUGE:
        if (C != 0 && isLegalICmpImmediate(C-1)) {
          CC = (CC == ISD::SETULT) ? ISD::SETULE : ISD::SETUGT;
          RHS = DAG.getConstant(C - 1, dl, MVT::i32);
        }
        break;
      case ISD::SETLE:
      case ISD::SETGT:
        if (C != 0x7fffffff && isLegalICmpImmediate(C+1)) {
          CC = (CC == ISD::SETLE) ? ISD::SETLT : ISD::SETGE;
          RHS = DAG.getConstant(C + 1, dl, MVT::i32);
        }
        break;
      case ISD::SETULE:
      case ISD::SETUGT:
        if (C != 0xffffffff && isLegalICmpImmediate(C+1)) {
          CC = (CC == ISD::SETULE) ? ISD::SETULT : ISD::SETUGE;
          RHS = DAG.getConstant(C + 1, dl, MVT::i32);
        }
        break;
      }
    }
  } else if ((ARM_AM::getShiftOpcForNode(LHS.getOpcode()) != ARM_AM::no_shift) &&
             (ARM_AM::getShiftOpcForNode(RHS.getOpcode()) == ARM_AM::no_shift)) {
    // In ARM and Thumb-2, the compare instructions can shift their second
    // operand.
    CC = ISD::getSetCCSwappedOperands(CC);
    std::swap(LHS, RHS);
  }

  // Thumb1 has very limited immediate modes, so turning an "and" into a
  // shift can save multiple instructions.
  //
  // If we have (x & C1), and C1 is an appropriate mask, we can transform it
  // into "((x << n) >> n)".  But that isn't necessarily profitable on its
  // own. If it's the operand to an unsigned comparison with an immediate,
  // we can eliminate one of the shifts: we transform
  // "((x << n) >> n) == C2" to "(x << n) == (C2 << n)".
  //
  // We avoid transforming cases which aren't profitable due to encoding
  // details:
  //
  // 1. C2 fits into the immediate field of a cmp, and the transformed version
  // would not; in that case, we're essentially trading one immediate load for
  // another.
  // 2. C1 is 255 or 65535, so we can use uxtb or uxth.
  // 3. C2 is zero; we have other code for this special case.
  //
  // FIXME: Figure out profitability for Thumb2; we usually can't save an
  // instruction, since the AND is always one instruction anyway, but we could
  // use narrow instructions in some cases.
  if (Subtarget->isThumb1Only() && LHS->getOpcode() == ISD::AND &&
      LHS->hasOneUse() && isa<ConstantSDNode>(LHS.getOperand(1)) &&
      LHS.getValueType() == MVT::i32 && isa<ConstantSDNode>(RHS) &&
      !isSignedIntSetCC(CC)) {
    unsigned Mask = LHS.getConstantOperandVal(1);
    auto *RHSC = cast<ConstantSDNode>(RHS.getNode());
    uint64_t RHSV = RHSC->getZExtValue();
    if (isMask_32(Mask) && (RHSV & ~Mask) == 0 && Mask != 255 && Mask != 65535) {
      unsigned ShiftBits = llvm::countl_zero(Mask);
      if (RHSV && (RHSV > 255 || (RHSV << ShiftBits) <= 255)) {
        SDValue ShiftAmt = DAG.getConstant(ShiftBits, dl, MVT::i32);
        LHS = DAG.getNode(ISD::SHL, dl, MVT::i32, LHS.getOperand(0), ShiftAmt);
        RHS = DAG.getConstant(RHSV << ShiftBits, dl, MVT::i32);
      }
    }
  }

  // The specific comparison "(x<<c) > 0x80000000U" can be optimized to a
  // single "lsls x, c+1".  The shift sets the "C" and "Z" flags the same
  // way a cmp would.
  // FIXME: Add support for ARM/Thumb2; this would need isel patterns, and
  // some tweaks to the heuristics for the previous and->shift transform.
  // FIXME: Optimize cases where the LHS isn't a shift.
  if (Subtarget->isThumb1Only() && LHS->getOpcode() == ISD::SHL &&
      isa<ConstantSDNode>(RHS) && RHS->getAsZExtVal() == 0x80000000U &&
      CC == ISD::SETUGT && isa<ConstantSDNode>(LHS.getOperand(1)) &&
      LHS.getConstantOperandVal(1) < 31) {
    unsigned ShiftAmt = LHS.getConstantOperandVal(1) + 1;
    SDValue Shift = DAG.getNode(ARMISD::LSLS, dl,
                                DAG.getVTList(MVT::i32, MVT::i32),
                                LHS.getOperand(0),
                                DAG.getConstant(ShiftAmt, dl, MVT::i32));
    SDValue Chain = DAG.getCopyToReg(DAG.getEntryNode(), dl, ARM::CPSR,
                                     Shift.getValue(1), SDValue());
    ARMcc = DAG.getConstant(ARMCC::HI, dl, MVT::i32);
    return Chain.getValue(1);
  }

  ARMCC::CondCodes CondCode = IntCCToARMCC(CC);

  // If the RHS is a constant zero then the V (overflow) flag will never be
  // set. This can allow us to simplify GE to PL or LT to MI, which can be
  // simpler for other passes (like the peephole optimiser) to deal with.
  if (isNullConstant(RHS)) {
    switch (CondCode) {
      default: break;
      case ARMCC::GE:
        CondCode = ARMCC::PL;
        break;
      case ARMCC::LT:
        CondCode = ARMCC::MI;
        break;
    }
  }

  ARMISD::NodeType CompareType;
  switch (CondCode) {
  default:
    CompareType = ARMISD::CMP;
    break;
  case ARMCC::EQ:
  case ARMCC::NE:
    // Uses only Z Flag
    CompareType = ARMISD::CMPZ;
    break;
  }
  ARMcc = DAG.getConstant(CondCode, dl, MVT::i32);
  return DAG.getNode(CompareType, dl, MVT::Glue, LHS, RHS);
}

/// Returns a appropriate VFP CMP (fcmp{s|d}+fmstat) for the given operands.
SDValue ARMTargetLowering::getVFPCmp(SDValue LHS, SDValue RHS,
                                     SelectionDAG &DAG, const SDLoc &dl,
                                     bool Signaling) const {
  assert(Subtarget->hasFP64() || RHS.getValueType() != MVT::f64);
  SDValue Cmp;
  if (!isFloatingPointZero(RHS))
    Cmp = DAG.getNode(Signaling ? ARMISD::CMPFPE : ARMISD::CMPFP,
                      dl, MVT::Glue, LHS, RHS);
  else
    Cmp = DAG.getNode(Signaling ? ARMISD::CMPFPEw0 : ARMISD::CMPFPw0,
                      dl, MVT::Glue, LHS);
  return DAG.getNode(ARMISD::FMSTAT, dl, MVT::Glue, Cmp);
}

/// duplicateCmp - Glue values can have only one use, so this function
/// duplicates a comparison node.
SDValue
ARMTargetLowering::duplicateCmp(SDValue Cmp, SelectionDAG &DAG) const {
  unsigned Opc = Cmp.getOpcode();
  SDLoc DL(Cmp);
  if (Opc == ARMISD::CMP || Opc == ARMISD::CMPZ)
    return DAG.getNode(Opc, DL, MVT::Glue, Cmp.getOperand(0),Cmp.getOperand(1));

  assert(Opc == ARMISD::FMSTAT && "unexpected comparison operation");
  Cmp = Cmp.getOperand(0);
  Opc = Cmp.getOpcode();
  if (Opc == ARMISD::CMPFP)
    Cmp = DAG.getNode(Opc, DL, MVT::Glue, Cmp.getOperand(0),Cmp.getOperand(1));
  else {
    assert(Opc == ARMISD::CMPFPw0 && "unexpected operand of FMSTAT");
    Cmp = DAG.getNode(Opc, DL, MVT::Glue, Cmp.getOperand(0));
  }
  return DAG.getNode(ARMISD::FMSTAT, DL, MVT::Glue, Cmp);
}

// This function returns three things: the arithmetic computation itself
// (Value), a comparison (OverflowCmp), and a condition code (ARMcc).  The
// comparison and the condition code define the case in which the arithmetic
// computation *does not* overflow.
std::pair<SDValue, SDValue>
ARMTargetLowering::getARMXALUOOp(SDValue Op, SelectionDAG &DAG,
                                 SDValue &ARMcc) const {
  assert(Op.getValueType() == MVT::i32 &&  "Unsupported value type");

  SDValue Value, OverflowCmp;
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDLoc dl(Op);

  // FIXME: We are currently always generating CMPs because we don't support
  // generating CMN through the backend. This is not as good as the natural
  // CMP case because it causes a register dependency and cannot be folded
  // later.

  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("Unknown overflow instruction!");
  case ISD::SADDO:
    ARMcc = DAG.getConstant(ARMCC::VC, dl, MVT::i32);
    Value = DAG.getNode(ISD::ADD, dl, Op.getValueType(), LHS, RHS);
    OverflowCmp = DAG.getNode(ARMISD::CMP, dl, MVT::Glue, Value, LHS);
    break;
  case ISD::UADDO:
    ARMcc = DAG.getConstant(ARMCC::HS, dl, MVT::i32);
    // We use ADDC here to correspond to its use in LowerUnsignedALUO.
    // We do not use it in the USUBO case as Value may not be used.
    Value = DAG.getNode(ARMISD::ADDC, dl,
                        DAG.getVTList(Op.getValueType(), MVT::i32), LHS, RHS)
                .getValue(0);
    OverflowCmp = DAG.getNode(ARMISD::CMP, dl, MVT::Glue, Value, LHS);
    break;
  case ISD::SSUBO:
    ARMcc = DAG.getConstant(ARMCC::VC, dl, MVT::i32);
    Value = DAG.getNode(ISD::SUB, dl, Op.getValueType(), LHS, RHS);
    OverflowCmp = DAG.getNode(ARMISD::CMP, dl, MVT::Glue, LHS, RHS);
    break;
  case ISD::USUBO:
    ARMcc = DAG.getConstant(ARMCC::HS, dl, MVT::i32);
    Value = DAG.getNode(ISD::SUB, dl, Op.getValueType(), LHS, RHS);
    OverflowCmp = DAG.getNode(ARMISD::CMP, dl, MVT::Glue, LHS, RHS);
    break;
  case ISD::UMULO:
    // We generate a UMUL_LOHI and then check if the high word is 0.
    ARMcc = DAG.getConstant(ARMCC::EQ, dl, MVT::i32);
    Value = DAG.getNode(ISD::UMUL_LOHI, dl,
                        DAG.getVTList(Op.getValueType(), Op.getValueType()),
                        LHS, RHS);
    OverflowCmp = DAG.getNode(ARMISD::CMP, dl, MVT::Glue, Value.getValue(1),
                              DAG.getConstant(0, dl, MVT::i32));
    Value = Value.getValue(0); // We only want the low 32 bits for the result.
    break;
  case ISD::SMULO:
    // We generate a SMUL_LOHI and then check if all the bits of the high word
    // are the same as the sign bit of the low word.
    ARMcc = DAG.getConstant(ARMCC::EQ, dl, MVT::i32);
    Value = DAG.getNode(ISD::SMUL_LOHI, dl,
                        DAG.getVTList(Op.getValueType(), Op.getValueType()),
                        LHS, RHS);
    OverflowCmp = DAG.getNode(ARMISD::CMP, dl, MVT::Glue, Value.getValue(1),
                              DAG.getNode(ISD::SRA, dl, Op.getValueType(),
                                          Value.getValue(0),
                                          DAG.getConstant(31, dl, MVT::i32)));
    Value = Value.getValue(0); // We only want the low 32 bits for the result.
    break;
  } // switch (...)

  return std::make_pair(Value, OverflowCmp);
}

SDValue
ARMTargetLowering::LowerSignedALUO(SDValue Op, SelectionDAG &DAG) const {
  // Let legalize expand this if it isn't a legal type yet.
  if (!DAG.getTargetLoweringInfo().isTypeLegal(Op.getValueType()))
    return SDValue();

  SDValue Value, OverflowCmp;
  SDValue ARMcc;
  std::tie(Value, OverflowCmp) = getARMXALUOOp(Op, DAG, ARMcc);
  SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
  SDLoc dl(Op);
  // We use 0 and 1 as false and true values.
  SDValue TVal = DAG.getConstant(1, dl, MVT::i32);
  SDValue FVal = DAG.getConstant(0, dl, MVT::i32);
  EVT VT = Op.getValueType();

  SDValue Overflow = DAG.getNode(ARMISD::CMOV, dl, VT, TVal, FVal,
                                 ARMcc, CCR, OverflowCmp);

  SDVTList VTs = DAG.getVTList(Op.getValueType(), MVT::i32);
  return DAG.getNode(ISD::MERGE_VALUES, dl, VTs, Value, Overflow);
}

static SDValue ConvertBooleanCarryToCarryFlag(SDValue BoolCarry,
                                              SelectionDAG &DAG) {
  SDLoc DL(BoolCarry);
  EVT CarryVT = BoolCarry.getValueType();

  // This converts the boolean value carry into the carry flag by doing
  // ARMISD::SUBC Carry, 1
  SDValue Carry = DAG.getNode(ARMISD::SUBC, DL,
                              DAG.getVTList(CarryVT, MVT::i32),
                              BoolCarry, DAG.getConstant(1, DL, CarryVT));
  return Carry.getValue(1);
}

static SDValue ConvertCarryFlagToBooleanCarry(SDValue Flags, EVT VT,
                                              SelectionDAG &DAG) {
  SDLoc DL(Flags);

  // Now convert the carry flag into a boolean carry. We do this
  // using ARMISD:ADDE 0, 0, Carry
  return DAG.getNode(ARMISD::ADDE, DL, DAG.getVTList(VT, MVT::i32),
                     DAG.getConstant(0, DL, MVT::i32),
                     DAG.getConstant(0, DL, MVT::i32), Flags);
}

SDValue ARMTargetLowering::LowerUnsignedALUO(SDValue Op,
                                             SelectionDAG &DAG) const {
  // Let legalize expand this if it isn't a legal type yet.
  if (!DAG.getTargetLoweringInfo().isTypeLegal(Op.getValueType()))
    return SDValue();

  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDLoc dl(Op);

  EVT VT = Op.getValueType();
  SDVTList VTs = DAG.getVTList(VT, MVT::i32);
  SDValue Value;
  SDValue Overflow;
  switch (Op.getOpcode()) {
  default:
    llvm_unreachable("Unknown overflow instruction!");
  case ISD::UADDO:
    Value = DAG.getNode(ARMISD::ADDC, dl, VTs, LHS, RHS);
    // Convert the carry flag into a boolean value.
    Overflow = ConvertCarryFlagToBooleanCarry(Value.getValue(1), VT, DAG);
    break;
  case ISD::USUBO: {
    Value = DAG.getNode(ARMISD::SUBC, dl, VTs, LHS, RHS);
    // Convert the carry flag into a boolean value.
    Overflow = ConvertCarryFlagToBooleanCarry(Value.getValue(1), VT, DAG);
    // ARMISD::SUBC returns 0 when we have to borrow, so make it an overflow
    // value. So compute 1 - C.
    Overflow = DAG.getNode(ISD::SUB, dl, MVT::i32,
                           DAG.getConstant(1, dl, MVT::i32), Overflow);
    break;
  }
  }

  return DAG.getNode(ISD::MERGE_VALUES, dl, VTs, Value, Overflow);
}

static SDValue LowerADDSUBSAT(SDValue Op, SelectionDAG &DAG,
                              const ARMSubtarget *Subtarget) {
  EVT VT = Op.getValueType();
  if (!Subtarget->hasV6Ops() || !Subtarget->hasDSP() || Subtarget->isThumb1Only())
    return SDValue();
  if (!VT.isSimple())
    return SDValue();

  unsigned NewOpcode;
  switch (VT.getSimpleVT().SimpleTy) {
  default:
    return SDValue();
  case MVT::i8:
    switch (Op->getOpcode()) {
    case ISD::UADDSAT:
      NewOpcode = ARMISD::UQADD8b;
      break;
    case ISD::SADDSAT:
      NewOpcode = ARMISD::QADD8b;
      break;
    case ISD::USUBSAT:
      NewOpcode = ARMISD::UQSUB8b;
      break;
    case ISD::SSUBSAT:
      NewOpcode = ARMISD::QSUB8b;
      break;
    }
    break;
  case MVT::i16:
    switch (Op->getOpcode()) {
    case ISD::UADDSAT:
      NewOpcode = ARMISD::UQADD16b;
      break;
    case ISD::SADDSAT:
      NewOpcode = ARMISD::QADD16b;
      break;
    case ISD::USUBSAT:
      NewOpcode = ARMISD::UQSUB16b;
      break;
    case ISD::SSUBSAT:
      NewOpcode = ARMISD::QSUB16b;
      break;
    }
    break;
  }

  SDLoc dl(Op);
  SDValue Add =
      DAG.getNode(NewOpcode, dl, MVT::i32,
                  DAG.getSExtOrTrunc(Op->getOperand(0), dl, MVT::i32),
                  DAG.getSExtOrTrunc(Op->getOperand(1), dl, MVT::i32));
  return DAG.getNode(ISD::TRUNCATE, dl, VT, Add);
}

SDValue ARMTargetLowering::LowerSELECT(SDValue Op, SelectionDAG &DAG) const {
  SDValue Cond = Op.getOperand(0);
  SDValue SelectTrue = Op.getOperand(1);
  SDValue SelectFalse = Op.getOperand(2);
  SDLoc dl(Op);
  unsigned Opc = Cond.getOpcode();

  if (Cond.getResNo() == 1 &&
      (Opc == ISD::SADDO || Opc == ISD::UADDO || Opc == ISD::SSUBO ||
       Opc == ISD::USUBO)) {
    if (!DAG.getTargetLoweringInfo().isTypeLegal(Cond->getValueType(0)))
      return SDValue();

    SDValue Value, OverflowCmp;
    SDValue ARMcc;
    std::tie(Value, OverflowCmp) = getARMXALUOOp(Cond, DAG, ARMcc);
    SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
    EVT VT = Op.getValueType();

    return getCMOV(dl, VT, SelectTrue, SelectFalse, ARMcc, CCR,
                   OverflowCmp, DAG);
  }

  // Convert:
  //
  //   (select (cmov 1, 0, cond), t, f) -> (cmov t, f, cond)
  //   (select (cmov 0, 1, cond), t, f) -> (cmov f, t, cond)
  //
  if (Cond.getOpcode() == ARMISD::CMOV && Cond.hasOneUse()) {
    const ConstantSDNode *CMOVTrue =
      dyn_cast<ConstantSDNode>(Cond.getOperand(0));
    const ConstantSDNode *CMOVFalse =
      dyn_cast<ConstantSDNode>(Cond.getOperand(1));

    if (CMOVTrue && CMOVFalse) {
      unsigned CMOVTrueVal = CMOVTrue->getZExtValue();
      unsigned CMOVFalseVal = CMOVFalse->getZExtValue();

      SDValue True;
      SDValue False;
      if (CMOVTrueVal == 1 && CMOVFalseVal == 0) {
        True = SelectTrue;
        False = SelectFalse;
      } else if (CMOVTrueVal == 0 && CMOVFalseVal == 1) {
        True = SelectFalse;
        False = SelectTrue;
      }

      if (True.getNode() && False.getNode()) {
        EVT VT = Op.getValueType();
        SDValue ARMcc = Cond.getOperand(2);
        SDValue CCR = Cond.getOperand(3);
        SDValue Cmp = duplicateCmp(Cond.getOperand(4), DAG);
        assert(True.getValueType() == VT);
        return getCMOV(dl, VT, True, False, ARMcc, CCR, Cmp, DAG);
      }
    }
  }

  // ARM's BooleanContents value is UndefinedBooleanContent. Mask out the
  // undefined bits before doing a full-word comparison with zero.
  Cond = DAG.getNode(ISD::AND, dl, Cond.getValueType(), Cond,
                     DAG.getConstant(1, dl, Cond.getValueType()));

  return DAG.getSelectCC(dl, Cond,
                         DAG.getConstant(0, dl, Cond.getValueType()),
                         SelectTrue, SelectFalse, ISD::SETNE);
}

static void checkVSELConstraints(ISD::CondCode CC, ARMCC::CondCodes &CondCode,
                                 bool &swpCmpOps, bool &swpVselOps) {
  // Start by selecting the GE condition code for opcodes that return true for
  // 'equality'
  if (CC == ISD::SETUGE || CC == ISD::SETOGE || CC == ISD::SETOLE ||
      CC == ISD::SETULE || CC == ISD::SETGE  || CC == ISD::SETLE)
    CondCode = ARMCC::GE;

  // and GT for opcodes that return false for 'equality'.
  else if (CC == ISD::SETUGT || CC == ISD::SETOGT || CC == ISD::SETOLT ||
           CC == ISD::SETULT || CC == ISD::SETGT  || CC == ISD::SETLT)
    CondCode = ARMCC::GT;

  // Since we are constrained to GE/GT, if the opcode contains 'less', we need
  // to swap the compare operands.
  if (CC == ISD::SETOLE || CC == ISD::SETULE || CC == ISD::SETOLT ||
      CC == ISD::SETULT || CC == ISD::SETLE  || CC == ISD::SETLT)
    swpCmpOps = true;

  // Both GT and GE are ordered comparisons, and return false for 'unordered'.
  // If we have an unordered opcode, we need to swap the operands to the VSEL
  // instruction (effectively negating the condition).
  //
  // This also has the effect of swapping which one of 'less' or 'greater'
  // returns true, so we also swap the compare operands. It also switches
  // whether we return true for 'equality', so we compensate by picking the
  // opposite condition code to our original choice.
  if (CC == ISD::SETULE || CC == ISD::SETULT || CC == ISD::SETUGE ||
      CC == ISD::SETUGT) {
    swpCmpOps = !swpCmpOps;
    swpVselOps = !swpVselOps;
    CondCode = CondCode == ARMCC::GT ? ARMCC::GE : ARMCC::GT;
  }

  // 'ordered' is 'anything but unordered', so use the VS condition code and
  // swap the VSEL operands.
  if (CC == ISD::SETO) {
    CondCode = ARMCC::VS;
    swpVselOps = true;
  }

  // 'unordered or not equal' is 'anything but equal', so use the EQ condition
  // code and swap the VSEL operands. Also do this if we don't care about the
  // unordered case.
  if (CC == ISD::SETUNE || CC == ISD::SETNE) {
    CondCode = ARMCC::EQ;
    swpVselOps = true;
  }
}

SDValue ARMTargetLowering::getCMOV(const SDLoc &dl, EVT VT, SDValue FalseVal,
                                   SDValue TrueVal, SDValue ARMcc, SDValue CCR,
                                   SDValue Cmp, SelectionDAG &DAG) const {
  if (!Subtarget->hasFP64() && VT == MVT::f64) {
    FalseVal = DAG.getNode(ARMISD::VMOVRRD, dl,
                           DAG.getVTList(MVT::i32, MVT::i32), FalseVal);
    TrueVal = DAG.getNode(ARMISD::VMOVRRD, dl,
                          DAG.getVTList(MVT::i32, MVT::i32), TrueVal);

    SDValue TrueLow = TrueVal.getValue(0);
    SDValue TrueHigh = TrueVal.getValue(1);
    SDValue FalseLow = FalseVal.getValue(0);
    SDValue FalseHigh = FalseVal.getValue(1);

    SDValue Low = DAG.getNode(ARMISD::CMOV, dl, MVT::i32, FalseLow, TrueLow,
                              ARMcc, CCR, Cmp);
    SDValue High = DAG.getNode(ARMISD::CMOV, dl, MVT::i32, FalseHigh, TrueHigh,
                               ARMcc, CCR, duplicateCmp(Cmp, DAG));

    return DAG.getNode(ARMISD::VMOVDRR, dl, MVT::f64, Low, High);
  } else {
    return DAG.getNode(ARMISD::CMOV, dl, VT, FalseVal, TrueVal, ARMcc, CCR,
                       Cmp);
  }
}

static bool isGTorGE(ISD::CondCode CC) {
  return CC == ISD::SETGT || CC == ISD::SETGE;
}

static bool isLTorLE(ISD::CondCode CC) {
  return CC == ISD::SETLT || CC == ISD::SETLE;
}

// See if a conditional (LHS CC RHS ? TrueVal : FalseVal) is lower-saturating.
// All of these conditions (and their <= and >= counterparts) will do:
//          x < k ? k : x
//          x > k ? x : k
//          k < x ? x : k
//          k > x ? k : x
static bool isLowerSaturate(const SDValue LHS, const SDValue RHS,
                            const SDValue TrueVal, const SDValue FalseVal,
                            const ISD::CondCode CC, const SDValue K) {
  return (isGTorGE(CC) &&
          ((K == LHS && K == TrueVal) || (K == RHS && K == FalseVal))) ||
         (isLTorLE(CC) &&
          ((K == RHS && K == TrueVal) || (K == LHS && K == FalseVal)));
}

// Check if two chained conditionals could be converted into SSAT or USAT.
//
// SSAT can replace a set of two conditional selectors that bound a number to an
// interval of type [k, ~k] when k + 1 is a power of 2. Here are some examples:
//
//     x < -k ? -k : (x > k ? k : x)
//     x < -k ? -k : (x < k ? x : k)
//     x > -k ? (x > k ? k : x) : -k
//     x < k ? (x < -k ? -k : x) : k
//     etc.
//
// LLVM canonicalizes these to either a min(max()) or a max(min())
// pattern. This function tries to match one of these and will return a SSAT
// node if successful.
//
// USAT works similarily to SSAT but bounds on the interval [0, k] where k + 1
// is a power of 2.
static SDValue LowerSaturatingConditional(SDValue Op, SelectionDAG &DAG) {
  EVT VT = Op.getValueType();
  SDValue V1 = Op.getOperand(0);
  SDValue K1 = Op.getOperand(1);
  SDValue TrueVal1 = Op.getOperand(2);
  SDValue FalseVal1 = Op.getOperand(3);
  ISD::CondCode CC1 = cast<CondCodeSDNode>(Op.getOperand(4))->get();

  const SDValue Op2 = isa<ConstantSDNode>(TrueVal1) ? FalseVal1 : TrueVal1;
  if (Op2.getOpcode() != ISD::SELECT_CC)
    return SDValue();

  SDValue V2 = Op2.getOperand(0);
  SDValue K2 = Op2.getOperand(1);
  SDValue TrueVal2 = Op2.getOperand(2);
  SDValue FalseVal2 = Op2.getOperand(3);
  ISD::CondCode CC2 = cast<CondCodeSDNode>(Op2.getOperand(4))->get();

  SDValue V1Tmp = V1;
  SDValue V2Tmp = V2;

  // Check that the registers and the constants match a max(min()) or min(max())
  // pattern
  if (V1Tmp != TrueVal1 || V2Tmp != TrueVal2 || K1 != FalseVal1 ||
      K2 != FalseVal2 ||
      !((isGTorGE(CC1) && isLTorLE(CC2)) || (isLTorLE(CC1) && isGTorGE(CC2))))
    return SDValue();

  // Check that the constant in the lower-bound check is
  // the opposite of the constant in the upper-bound check
  // in 1's complement.
  if (!isa<ConstantSDNode>(K1) || !isa<ConstantSDNode>(K2))
    return SDValue();

  int64_t Val1 = cast<ConstantSDNode>(K1)->getSExtValue();
  int64_t Val2 = cast<ConstantSDNode>(K2)->getSExtValue();
  int64_t PosVal = std::max(Val1, Val2);
  int64_t NegVal = std::min(Val1, Val2);

  if (!((Val1 > Val2 && isLTorLE(CC1)) || (Val1 < Val2 && isLTorLE(CC2))) ||
      !isPowerOf2_64(PosVal + 1))
    return SDValue();

  // Handle the difference between USAT (unsigned) and SSAT (signed)
  // saturation
  // At this point, PosVal is guaranteed to be positive
  uint64_t K = PosVal;
  SDLoc dl(Op);
  if (Val1 == ~Val2)
    return DAG.getNode(ARMISD::SSAT, dl, VT, V2Tmp,
                       DAG.getConstant(llvm::countr_one(K), dl, VT));
  if (NegVal == 0)
    return DAG.getNode(ARMISD::USAT, dl, VT, V2Tmp,
                       DAG.getConstant(llvm::countr_one(K), dl, VT));

  return SDValue();
}

// Check if a condition of the type x < k ? k : x can be converted into a
// bit operation instead of conditional moves.
// Currently this is allowed given:
// - The conditions and values match up
// - k is 0 or -1 (all ones)
// This function will not check the last condition, thats up to the caller
// It returns true if the transformation can be made, and in such case
// returns x in V, and k in SatK.
static bool isLowerSaturatingConditional(const SDValue &Op, SDValue &V,
                                         SDValue &SatK)
{
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDValue TrueVal = Op.getOperand(2);
  SDValue FalseVal = Op.getOperand(3);

  SDValue *K = isa<ConstantSDNode>(LHS) ? &LHS : isa<ConstantSDNode>(RHS)
                                               ? &RHS
                                               : nullptr;

  // No constant operation in comparison, early out
  if (!K)
    return false;

  SDValue KTmp = isa<ConstantSDNode>(TrueVal) ? TrueVal : FalseVal;
  V = (KTmp == TrueVal) ? FalseVal : TrueVal;
  SDValue VTmp = (K && *K == LHS) ? RHS : LHS;

  // If the constant on left and right side, or variable on left and right,
  // does not match, early out
  if (*K != KTmp || V != VTmp)
    return false;

  if (isLowerSaturate(LHS, RHS, TrueVal, FalseVal, CC, *K)) {
    SatK = *K;
    return true;
  }

  return false;
}

bool ARMTargetLowering::isUnsupportedFloatingType(EVT VT) const {
  if (VT == MVT::f32)
    return !Subtarget->hasVFP2Base();
  if (VT == MVT::f64)
    return !Subtarget->hasFP64();
  if (VT == MVT::f16)
    return !Subtarget->hasFullFP16();
  return false;
}

SDValue ARMTargetLowering::LowerSELECT_CC(SDValue Op, SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  SDLoc dl(Op);

  // Try to convert two saturating conditional selects into a single SSAT
  if ((!Subtarget->isThumb() && Subtarget->hasV6Ops()) || Subtarget->isThumb2())
    if (SDValue SatValue = LowerSaturatingConditional(Op, DAG))
      return SatValue;

  // Try to convert expressions of the form x < k ? k : x (and similar forms)
  // into more efficient bit operations, which is possible when k is 0 or -1
  // On ARM and Thumb-2 which have flexible operand 2 this will result in
  // single instructions. On Thumb the shift and the bit operation will be two
  // instructions.
  // Only allow this transformation on full-width (32-bit) operations
  SDValue LowerSatConstant;
  SDValue SatValue;
  if (VT == MVT::i32 &&
      isLowerSaturatingConditional(Op, SatValue, LowerSatConstant)) {
    SDValue ShiftV = DAG.getNode(ISD::SRA, dl, VT, SatValue,
                                 DAG.getConstant(31, dl, VT));
    if (isNullConstant(LowerSatConstant)) {
      SDValue NotShiftV = DAG.getNode(ISD::XOR, dl, VT, ShiftV,
                                      DAG.getAllOnesConstant(dl, VT));
      return DAG.getNode(ISD::AND, dl, VT, SatValue, NotShiftV);
    } else if (isAllOnesConstant(LowerSatConstant))
      return DAG.getNode(ISD::OR, dl, VT, SatValue, ShiftV);
  }

  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(4))->get();
  SDValue TrueVal = Op.getOperand(2);
  SDValue FalseVal = Op.getOperand(3);
  ConstantSDNode *CFVal = dyn_cast<ConstantSDNode>(FalseVal);
  ConstantSDNode *CTVal = dyn_cast<ConstantSDNode>(TrueVal);

  if (Subtarget->hasV8_1MMainlineOps() && CFVal && CTVal &&
      LHS.getValueType() == MVT::i32 && RHS.getValueType() == MVT::i32) {
    unsigned TVal = CTVal->getZExtValue();
    unsigned FVal = CFVal->getZExtValue();
    unsigned Opcode = 0;

    if (TVal == ~FVal) {
      Opcode = ARMISD::CSINV;
    } else if (TVal == ~FVal + 1) {
      Opcode = ARMISD::CSNEG;
    } else if (TVal + 1 == FVal) {
      Opcode = ARMISD::CSINC;
    } else if (TVal == FVal + 1) {
      Opcode = ARMISD::CSINC;
      std::swap(TrueVal, FalseVal);
      std::swap(TVal, FVal);
      CC = ISD::getSetCCInverse(CC, LHS.getValueType());
    }

    if (Opcode) {
      // If one of the constants is cheaper than another, materialise the
      // cheaper one and let the csel generate the other.
      if (Opcode != ARMISD::CSINC &&
          HasLowerConstantMaterializationCost(FVal, TVal, Subtarget)) {
        std::swap(TrueVal, FalseVal);
        std::swap(TVal, FVal);
        CC = ISD::getSetCCInverse(CC, LHS.getValueType());
      }

      // Attempt to use ZR checking TVal is 0, possibly inverting the condition
      // to get there. CSINC not is invertable like the other two (~(~a) == a,
      // -(-a) == a, but (a+1)+1 != a).
      if (FVal == 0 && Opcode != ARMISD::CSINC) {
        std::swap(TrueVal, FalseVal);
        std::swap(TVal, FVal);
        CC = ISD::getSetCCInverse(CC, LHS.getValueType());
      }

      // Drops F's value because we can get it by inverting/negating TVal.
      FalseVal = TrueVal;

      SDValue ARMcc;
      SDValue Cmp = getARMCmp(LHS, RHS, CC, ARMcc, DAG, dl);
      EVT VT = TrueVal.getValueType();
      return DAG.getNode(Opcode, dl, VT, TrueVal, FalseVal, ARMcc, Cmp);
    }
  }

  if (isUnsupportedFloatingType(LHS.getValueType())) {
    DAG.getTargetLoweringInfo().softenSetCCOperands(
        DAG, LHS.getValueType(), LHS, RHS, CC, dl, LHS, RHS);

    // If softenSetCCOperands only returned one value, we should compare it to
    // zero.
    if (!RHS.getNode()) {
      RHS = DAG.getConstant(0, dl, LHS.getValueType());
      CC = ISD::SETNE;
    }
  }

  if (LHS.getValueType() == MVT::i32) {
    // Try to generate VSEL on ARMv8.
    // The VSEL instruction can't use all the usual ARM condition
    // codes: it only has two bits to select the condition code, so it's
    // constrained to use only GE, GT, VS and EQ.
    //
    // To implement all the various ISD::SETXXX opcodes, we sometimes need to
    // swap the operands of the previous compare instruction (effectively
    // inverting the compare condition, swapping 'less' and 'greater') and
    // sometimes need to swap the operands to the VSEL (which inverts the
    // condition in the sense of firing whenever the previous condition didn't)
    if (Subtarget->hasFPARMv8Base() && (TrueVal.getValueType() == MVT::f16 ||
                                        TrueVal.getValueType() == MVT::f32 ||
                                        TrueVal.getValueType() == MVT::f64)) {
      ARMCC::CondCodes CondCode = IntCCToARMCC(CC);
      if (CondCode == ARMCC::LT || CondCode == ARMCC::LE ||
          CondCode == ARMCC::VC || CondCode == ARMCC::NE) {
        CC = ISD::getSetCCInverse(CC, LHS.getValueType());
        std::swap(TrueVal, FalseVal);
      }
    }

    SDValue ARMcc;
    SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
    SDValue Cmp = getARMCmp(LHS, RHS, CC, ARMcc, DAG, dl);
    // Choose GE over PL, which vsel does now support
    if (ARMcc->getAsZExtVal() == ARMCC::PL)
      ARMcc = DAG.getConstant(ARMCC::GE, dl, MVT::i32);
    return getCMOV(dl, VT, FalseVal, TrueVal, ARMcc, CCR, Cmp, DAG);
  }

  ARMCC::CondCodes CondCode, CondCode2;
  FPCCToARMCC(CC, CondCode, CondCode2);

  // Normalize the fp compare. If RHS is zero we prefer to keep it there so we
  // match CMPFPw0 instead of CMPFP, though we don't do this for f16 because we
  // must use VSEL (limited condition codes), due to not having conditional f16
  // moves.
  if (Subtarget->hasFPARMv8Base() &&
      !(isFloatingPointZero(RHS) && TrueVal.getValueType() != MVT::f16) &&
      (TrueVal.getValueType() == MVT::f16 ||
       TrueVal.getValueType() == MVT::f32 ||
       TrueVal.getValueType() == MVT::f64)) {
    bool swpCmpOps = false;
    bool swpVselOps = false;
    checkVSELConstraints(CC, CondCode, swpCmpOps, swpVselOps);

    if (CondCode == ARMCC::GT || CondCode == ARMCC::GE ||
        CondCode == ARMCC::VS || CondCode == ARMCC::EQ) {
      if (swpCmpOps)
        std::swap(LHS, RHS);
      if (swpVselOps)
        std::swap(TrueVal, FalseVal);
    }
  }

  SDValue ARMcc = DAG.getConstant(CondCode, dl, MVT::i32);
  SDValue Cmp = getVFPCmp(LHS, RHS, DAG, dl);
  SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
  SDValue Result = getCMOV(dl, VT, FalseVal, TrueVal, ARMcc, CCR, Cmp, DAG);
  if (CondCode2 != ARMCC::AL) {
    SDValue ARMcc2 = DAG.getConstant(CondCode2, dl, MVT::i32);
    // FIXME: Needs another CMP because flag can have but one use.
    SDValue Cmp2 = getVFPCmp(LHS, RHS, DAG, dl);
    Result = getCMOV(dl, VT, Result, TrueVal, ARMcc2, CCR, Cmp2, DAG);
  }
  return Result;
}

/// canChangeToInt - Given the fp compare operand, return true if it is suitable
/// to morph to an integer compare sequence.
static bool canChangeToInt(SDValue Op, bool &SeenZero,
                           const ARMSubtarget *Subtarget) {
  SDNode *N = Op.getNode();
  if (!N->hasOneUse())
    // Otherwise it requires moving the value from fp to integer registers.
    return false;
  if (!N->getNumValues())
    return false;
  EVT VT = Op.getValueType();
  if (VT != MVT::f32 && !Subtarget->isFPBrccSlow())
    // f32 case is generally profitable. f64 case only makes sense when vcmpe +
    // vmrs are very slow, e.g. cortex-a8.
    return false;

  if (isFloatingPointZero(Op)) {
    SeenZero = true;
    return true;
  }
  return ISD::isNormalLoad(N);
}

static SDValue bitcastf32Toi32(SDValue Op, SelectionDAG &DAG) {
  if (isFloatingPointZero(Op))
    return DAG.getConstant(0, SDLoc(Op), MVT::i32);

  if (LoadSDNode *Ld = dyn_cast<LoadSDNode>(Op))
    return DAG.getLoad(MVT::i32, SDLoc(Op), Ld->getChain(), Ld->getBasePtr(),
                       Ld->getPointerInfo(), Ld->getAlign(),
                       Ld->getMemOperand()->getFlags());

  llvm_unreachable("Unknown VFP cmp argument!");
}

static void expandf64Toi32(SDValue Op, SelectionDAG &DAG,
                           SDValue &RetVal1, SDValue &RetVal2) {
  SDLoc dl(Op);

  if (isFloatingPointZero(Op)) {
    RetVal1 = DAG.getConstant(0, dl, MVT::i32);
    RetVal2 = DAG.getConstant(0, dl, MVT::i32);
    return;
  }

  if (LoadSDNode *Ld = dyn_cast<LoadSDNode>(Op)) {
    SDValue Ptr = Ld->getBasePtr();
    RetVal1 =
        DAG.getLoad(MVT::i32, dl, Ld->getChain(), Ptr, Ld->getPointerInfo(),
                    Ld->getAlign(), Ld->getMemOperand()->getFlags());

    EVT PtrType = Ptr.getValueType();
    SDValue NewPtr = DAG.getNode(ISD::ADD, dl,
                                 PtrType, Ptr, DAG.getConstant(4, dl, PtrType));
    RetVal2 = DAG.getLoad(MVT::i32, dl, Ld->getChain(), NewPtr,
                          Ld->getPointerInfo().getWithOffset(4),
                          commonAlignment(Ld->getAlign(), 4),
                          Ld->getMemOperand()->getFlags());
    return;
  }

  llvm_unreachable("Unknown VFP cmp argument!");
}

/// OptimizeVFPBrcond - With -enable-unsafe-fp-math, it's legal to optimize some
/// f32 and even f64 comparisons to integer ones.
SDValue
ARMTargetLowering::OptimizeVFPBrcond(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc dl(Op);

  bool LHSSeenZero = false;
  bool LHSOk = canChangeToInt(LHS, LHSSeenZero, Subtarget);
  bool RHSSeenZero = false;
  bool RHSOk = canChangeToInt(RHS, RHSSeenZero, Subtarget);
  if (LHSOk && RHSOk && (LHSSeenZero || RHSSeenZero)) {
    // If unsafe fp math optimization is enabled and there are no other uses of
    // the CMP operands, and the condition code is EQ or NE, we can optimize it
    // to an integer comparison.
    if (CC == ISD::SETOEQ)
      CC = ISD::SETEQ;
    else if (CC == ISD::SETUNE)
      CC = ISD::SETNE;

    SDValue Mask = DAG.getConstant(0x7fffffff, dl, MVT::i32);
    SDValue ARMcc;
    if (LHS.getValueType() == MVT::f32) {
      LHS = DAG.getNode(ISD::AND, dl, MVT::i32,
                        bitcastf32Toi32(LHS, DAG), Mask);
      RHS = DAG.getNode(ISD::AND, dl, MVT::i32,
                        bitcastf32Toi32(RHS, DAG), Mask);
      SDValue Cmp = getARMCmp(LHS, RHS, CC, ARMcc, DAG, dl);
      SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
      return DAG.getNode(ARMISD::BRCOND, dl, MVT::Other,
                         Chain, Dest, ARMcc, CCR, Cmp);
    }

    SDValue LHS1, LHS2;
    SDValue RHS1, RHS2;
    expandf64Toi32(LHS, DAG, LHS1, LHS2);
    expandf64Toi32(RHS, DAG, RHS1, RHS2);
    LHS2 = DAG.getNode(ISD::AND, dl, MVT::i32, LHS2, Mask);
    RHS2 = DAG.getNode(ISD::AND, dl, MVT::i32, RHS2, Mask);
    ARMCC::CondCodes CondCode = IntCCToARMCC(CC);
    ARMcc = DAG.getConstant(CondCode, dl, MVT::i32);
    SDVTList VTList = DAG.getVTList(MVT::Other, MVT::Glue);
    SDValue Ops[] = { Chain, ARMcc, LHS1, LHS2, RHS1, RHS2, Dest };
    return DAG.getNode(ARMISD::BCC_i64, dl, VTList, Ops);
  }

  return SDValue();
}

SDValue ARMTargetLowering::LowerBRCOND(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  SDValue Cond = Op.getOperand(1);
  SDValue Dest = Op.getOperand(2);
  SDLoc dl(Op);

  // Optimize {s|u}{add|sub|mul}.with.overflow feeding into a branch
  // instruction.
  unsigned Opc = Cond.getOpcode();
  bool OptimizeMul = (Opc == ISD::SMULO || Opc == ISD::UMULO) &&
                      !Subtarget->isThumb1Only();
  if (Cond.getResNo() == 1 &&
      (Opc == ISD::SADDO || Opc == ISD::UADDO || Opc == ISD::SSUBO ||
       Opc == ISD::USUBO || OptimizeMul)) {
    // Only lower legal XALUO ops.
    if (!DAG.getTargetLoweringInfo().isTypeLegal(Cond->getValueType(0)))
      return SDValue();

    // The actual operation with overflow check.
    SDValue Value, OverflowCmp;
    SDValue ARMcc;
    std::tie(Value, OverflowCmp) = getARMXALUOOp(Cond, DAG, ARMcc);

    // Reverse the condition code.
    ARMCC::CondCodes CondCode =
        (ARMCC::CondCodes)cast<const ConstantSDNode>(ARMcc)->getZExtValue();
    CondCode = ARMCC::getOppositeCondition(CondCode);
    ARMcc = DAG.getConstant(CondCode, SDLoc(ARMcc), MVT::i32);
    SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);

    return DAG.getNode(ARMISD::BRCOND, dl, MVT::Other, Chain, Dest, ARMcc, CCR,
                       OverflowCmp);
  }

  return SDValue();
}

SDValue ARMTargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);
  SDLoc dl(Op);

  if (isUnsupportedFloatingType(LHS.getValueType())) {
    DAG.getTargetLoweringInfo().softenSetCCOperands(
        DAG, LHS.getValueType(), LHS, RHS, CC, dl, LHS, RHS);

    // If softenSetCCOperands only returned one value, we should compare it to
    // zero.
    if (!RHS.getNode()) {
      RHS = DAG.getConstant(0, dl, LHS.getValueType());
      CC = ISD::SETNE;
    }
  }

  // Optimize {s|u}{add|sub|mul}.with.overflow feeding into a branch
  // instruction.
  unsigned Opc = LHS.getOpcode();
  bool OptimizeMul = (Opc == ISD::SMULO || Opc == ISD::UMULO) &&
                      !Subtarget->isThumb1Only();
  if (LHS.getResNo() == 1 && (isOneConstant(RHS) || isNullConstant(RHS)) &&
      (Opc == ISD::SADDO || Opc == ISD::UADDO || Opc == ISD::SSUBO ||
       Opc == ISD::USUBO || OptimizeMul) &&
      (CC == ISD::SETEQ || CC == ISD::SETNE)) {
    // Only lower legal XALUO ops.
    if (!DAG.getTargetLoweringInfo().isTypeLegal(LHS->getValueType(0)))
      return SDValue();

    // The actual operation with overflow check.
    SDValue Value, OverflowCmp;
    SDValue ARMcc;
    std::tie(Value, OverflowCmp) = getARMXALUOOp(LHS.getValue(0), DAG, ARMcc);

    if ((CC == ISD::SETNE) != isOneConstant(RHS)) {
      // Reverse the condition code.
      ARMCC::CondCodes CondCode =
          (ARMCC::CondCodes)cast<const ConstantSDNode>(ARMcc)->getZExtValue();
      CondCode = ARMCC::getOppositeCondition(CondCode);
      ARMcc = DAG.getConstant(CondCode, SDLoc(ARMcc), MVT::i32);
    }
    SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);

    return DAG.getNode(ARMISD::BRCOND, dl, MVT::Other, Chain, Dest, ARMcc, CCR,
                       OverflowCmp);
  }

  if (LHS.getValueType() == MVT::i32) {
    SDValue ARMcc;
    SDValue Cmp = getARMCmp(LHS, RHS, CC, ARMcc, DAG, dl);
    SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
    return DAG.getNode(ARMISD::BRCOND, dl, MVT::Other,
                       Chain, Dest, ARMcc, CCR, Cmp);
  }

  if (getTargetMachine().Options.UnsafeFPMath &&
      (CC == ISD::SETEQ || CC == ISD::SETOEQ ||
       CC == ISD::SETNE || CC == ISD::SETUNE)) {
    if (SDValue Result = OptimizeVFPBrcond(Op, DAG))
      return Result;
  }

  ARMCC::CondCodes CondCode, CondCode2;
  FPCCToARMCC(CC, CondCode, CondCode2);

  SDValue ARMcc = DAG.getConstant(CondCode, dl, MVT::i32);
  SDValue Cmp = getVFPCmp(LHS, RHS, DAG, dl);
  SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
  SDVTList VTList = DAG.getVTList(MVT::Other, MVT::Glue);
  SDValue Ops[] = { Chain, Dest, ARMcc, CCR, Cmp };
  SDValue Res = DAG.getNode(ARMISD::BRCOND, dl, VTList, Ops);
  if (CondCode2 != ARMCC::AL) {
    ARMcc = DAG.getConstant(CondCode2, dl, MVT::i32);
    SDValue Ops[] = { Res, Dest, ARMcc, CCR, Res.getValue(1) };
    Res = DAG.getNode(ARMISD::BRCOND, dl, VTList, Ops);
  }
  return Res;
}

SDValue ARMTargetLowering::LowerBR_JT(SDValue Op, SelectionDAG &DAG) const {
  SDValue Chain = Op.getOperand(0);
  SDValue Table = Op.getOperand(1);
  SDValue Index = Op.getOperand(2);
  SDLoc dl(Op);

  EVT PTy = getPointerTy(DAG.getDataLayout());
  JumpTableSDNode *JT = cast<JumpTableSDNode>(Table);
  SDValue JTI = DAG.getTargetJumpTable(JT->getIndex(), PTy);
  Table = DAG.getNode(ARMISD::WrapperJT, dl, MVT::i32, JTI);
  Index = DAG.getNode(ISD::MUL, dl, PTy, Index, DAG.getConstant(4, dl, PTy));
  SDValue Addr = DAG.getNode(ISD::ADD, dl, PTy, Table, Index);
  if (Subtarget->isThumb2() || (Subtarget->hasV8MBaselineOps() && Subtarget->isThumb())) {
    // Thumb2 and ARMv8-M use a two-level jump. That is, it jumps into the jump table
    // which does another jump to the destination. This also makes it easier
    // to translate it to TBB / TBH later (Thumb2 only).
    // FIXME: This might not work if the function is extremely large.
    return DAG.getNode(ARMISD::BR2_JT, dl, MVT::Other, Chain,
                       Addr, Op.getOperand(2), JTI);
  }
  if (isPositionIndependent() || Subtarget->isROPI()) {
    Addr =
        DAG.getLoad((EVT)MVT::i32, dl, Chain, Addr,
                    MachinePointerInfo::getJumpTable(DAG.getMachineFunction()));
    Chain = Addr.getValue(1);
    Addr = DAG.getNode(ISD::ADD, dl, PTy, Table, Addr);
    return DAG.getNode(ARMISD::BR_JT, dl, MVT::Other, Chain, Addr, JTI);
  } else {
    Addr =
        DAG.getLoad(PTy, dl, Chain, Addr,
                    MachinePointerInfo::getJumpTable(DAG.getMachineFunction()));
    Chain = Addr.getValue(1);
    return DAG.getNode(ARMISD::BR_JT, dl, MVT::Other, Chain, Addr, JTI);
  }
}

static SDValue LowerVectorFP_TO_INT(SDValue Op, SelectionDAG &DAG) {
  EVT VT = Op.getValueType();
  SDLoc dl(Op);

  if (Op.getValueType().getVectorElementType() == MVT::i32) {
    if (Op.getOperand(0).getValueType().getVectorElementType() == MVT::f32)
      return Op;
    return DAG.UnrollVectorOp(Op.getNode());
  }

  const bool HasFullFP16 = DAG.getSubtarget<ARMSubtarget>().hasFullFP16();

  EVT NewTy;
  const EVT OpTy = Op.getOperand(0).getValueType();
  if (OpTy == MVT::v4f32)
    NewTy = MVT::v4i32;
  else if (OpTy == MVT::v4f16 && HasFullFP16)
    NewTy = MVT::v4i16;
  else if (OpTy == MVT::v8f16 && HasFullFP16)
    NewTy = MVT::v8i16;
  else
    llvm_unreachable("Invalid type for custom lowering!");

  if (VT != MVT::v4i16 && VT != MVT::v8i16)
    return DAG.UnrollVectorOp(Op.getNode());

  Op = DAG.getNode(Op.getOpcode(), dl, NewTy, Op.getOperand(0));
  return DAG.getNode(ISD::TRUNCATE, dl, VT, Op);
}

SDValue ARMTargetLowering::LowerFP_TO_INT(SDValue Op, SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  if (VT.isVector())
    return LowerVectorFP_TO_INT(Op, DAG);

  bool IsStrict = Op->isStrictFPOpcode();
  SDValue SrcVal = Op.getOperand(IsStrict ? 1 : 0);

  if (isUnsupportedFloatingType(SrcVal.getValueType())) {
    RTLIB::Libcall LC;
    if (Op.getOpcode() == ISD::FP_TO_SINT ||
        Op.getOpcode() == ISD::STRICT_FP_TO_SINT)
      LC = RTLIB::getFPTOSINT(SrcVal.getValueType(),
                              Op.getValueType());
    else
      LC = RTLIB::getFPTOUINT(SrcVal.getValueType(),
                              Op.getValueType());
    SDLoc Loc(Op);
    MakeLibCallOptions CallOptions;
    SDValue Chain = IsStrict ? Op.getOperand(0) : SDValue();
    SDValue Result;
    std::tie(Result, Chain) = makeLibCall(DAG, LC, Op.getValueType(), SrcVal,
                                          CallOptions, Loc, Chain);
    return IsStrict ? DAG.getMergeValues({Result, Chain}, Loc) : Result;
  }

  // FIXME: Remove this when we have strict fp instruction selection patterns
  if (IsStrict) {
    SDLoc Loc(Op);
    SDValue Result =
        DAG.getNode(Op.getOpcode() == ISD::STRICT_FP_TO_SINT ? ISD::FP_TO_SINT
                                                             : ISD::FP_TO_UINT,
                    Loc, Op.getValueType(), SrcVal);
    return DAG.getMergeValues({Result, Op.getOperand(0)}, Loc);
  }

  return Op;
}

static SDValue LowerFP_TO_INT_SAT(SDValue Op, SelectionDAG &DAG,
                                  const ARMSubtarget *Subtarget) {
  EVT VT = Op.getValueType();
  EVT ToVT = cast<VTSDNode>(Op.getOperand(1))->getVT();
  EVT FromVT = Op.getOperand(0).getValueType();

  if (VT == MVT::i32 && ToVT == MVT::i32 && FromVT == MVT::f32)
    return Op;
  if (VT == MVT::i32 && ToVT == MVT::i32 && FromVT == MVT::f64 &&
      Subtarget->hasFP64())
    return Op;
  if (VT == MVT::i32 && ToVT == MVT::i32 && FromVT == MVT::f16 &&
      Subtarget->hasFullFP16())
    return Op;
  if (VT == MVT::v4i32 && ToVT == MVT::i32 && FromVT == MVT::v4f32 &&
      Subtarget->hasMVEFloatOps())
    return Op;
  if (VT == MVT::v8i16 && ToVT == MVT::i16 && FromVT == MVT::v8f16 &&
      Subtarget->hasMVEFloatOps())
    return Op;

  if (FromVT != MVT::v4f32 && FromVT != MVT::v8f16)
    return SDValue();

  SDLoc DL(Op);
  bool IsSigned = Op.getOpcode() == ISD::FP_TO_SINT_SAT;
  unsigned BW = ToVT.getScalarSizeInBits() - IsSigned;
  SDValue CVT = DAG.getNode(Op.getOpcode(), DL, VT, Op.getOperand(0),
                            DAG.getValueType(VT.getScalarType()));
  SDValue Max = DAG.getNode(IsSigned ? ISD::SMIN : ISD::UMIN, DL, VT, CVT,
                            DAG.getConstant((1 << BW) - 1, DL, VT));
  if (IsSigned)
    Max = DAG.getNode(ISD::SMAX, DL, VT, Max,
                      DAG.getConstant(-(1 << BW), DL, VT));
  return Max;
}

static SDValue LowerVectorINT_TO_FP(SDValue Op, SelectionDAG &DAG) {
  EVT VT = Op.getValueType();
  SDLoc dl(Op);

  if (Op.getOperand(0).getValueType().getVectorElementType() == MVT::i32) {
    if (VT.getVectorElementType() == MVT::f32)
      return Op;
    return DAG.UnrollVectorOp(Op.getNode());
  }

  assert((Op.getOperand(0).getValueType() == MVT::v4i16 ||
          Op.getOperand(0).getValueType() == MVT::v8i16) &&
         "Invalid type for custom lowering!");

  const bool HasFullFP16 = DAG.getSubtarget<ARMSubtarget>().hasFullFP16();

  EVT DestVecType;
  if (VT == MVT::v4f32)
    DestVecType = MVT::v4i32;
  else if (VT == MVT::v4f16 && HasFullFP16)
    DestVecType = MVT::v4i16;
  else if (VT == MVT::v8f16 && HasFullFP16)
    DestVecType = MVT::v8i16;
  else
    return DAG.UnrollVectorOp(Op.getNode());

  unsigned CastOpc;
  unsigned Opc;
  switch (Op.getOpcode()) {
  default: llvm_unreachable("Invalid opcode!");
  case ISD::SINT_TO_FP:
    CastOpc = ISD::SIGN_EXTEND;
    Opc = ISD::SINT_TO_FP;
    break;
  case ISD::UINT_TO_FP:
    CastOpc = ISD::ZERO_EXTEND;
    Opc = ISD::UINT_TO_FP;
    break;
  }

  Op = DAG.getNode(CastOpc, dl, DestVecType, Op.getOperand(0));
  return DAG.getNode(Opc, dl, VT, Op);
}

SDValue ARMTargetLowering::LowerINT_TO_FP(SDValue Op, SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  if (VT.isVector())
    return LowerVectorINT_TO_FP(Op, DAG);
  if (isUnsupportedFloatingType(VT)) {
    RTLIB::Libcall LC;
    if (Op.getOpcode() == ISD::SINT_TO_FP)
      LC = RTLIB::getSINTTOFP(Op.getOperand(0).getValueType(),
                              Op.getValueType());
    else
      LC = RTLIB::getUINTTOFP(Op.getOperand(0).getValueType(),
                              Op.getValueType());
    MakeLibCallOptions CallOptions;
    return makeLibCall(DAG, LC, Op.getValueType(), Op.getOperand(0),
                       CallOptions, SDLoc(Op)).first;
  }

  return Op;
}

SDValue ARMTargetLowering::LowerFCOPYSIGN(SDValue Op, SelectionDAG &DAG) const {
  // Implement fcopysign with a fabs and a conditional fneg.
  SDValue Tmp0 = Op.getOperand(0);
  SDValue Tmp1 = Op.getOperand(1);
  SDLoc dl(Op);
  EVT VT = Op.getValueType();
  EVT SrcVT = Tmp1.getValueType();
  bool InGPR = Tmp0.getOpcode() == ISD::BITCAST ||
    Tmp0.getOpcode() == ARMISD::VMOVDRR;
  bool UseNEON = !InGPR && Subtarget->hasNEON();

  if (UseNEON) {
    // Use VBSL to copy the sign bit.
    unsigned EncodedVal = ARM_AM::createVMOVModImm(0x6, 0x80);
    SDValue Mask = DAG.getNode(ARMISD::VMOVIMM, dl, MVT::v2i32,
                               DAG.getTargetConstant(EncodedVal, dl, MVT::i32));
    EVT OpVT = (VT == MVT::f32) ? MVT::v2i32 : MVT::v1i64;
    if (VT == MVT::f64)
      Mask = DAG.getNode(ARMISD::VSHLIMM, dl, OpVT,
                         DAG.getNode(ISD::BITCAST, dl, OpVT, Mask),
                         DAG.getConstant(32, dl, MVT::i32));
    else /*if (VT == MVT::f32)*/
      Tmp0 = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, MVT::v2f32, Tmp0);
    if (SrcVT == MVT::f32) {
      Tmp1 = DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, MVT::v2f32, Tmp1);
      if (VT == MVT::f64)
        Tmp1 = DAG.getNode(ARMISD::VSHLIMM, dl, OpVT,
                           DAG.getNode(ISD::BITCAST, dl, OpVT, Tmp1),
                           DAG.getConstant(32, dl, MVT::i32));
    } else if (VT == MVT::f32)
      Tmp1 = DAG.getNode(ARMISD::VSHRuIMM, dl, MVT::v1i64,
                         DAG.getNode(ISD::BITCAST, dl, MVT::v1i64, Tmp1),
                         DAG.getConstant(32, dl, MVT::i32));
    Tmp0 = DAG.getNode(ISD::BITCAST, dl, OpVT, Tmp0);
    Tmp1 = DAG.getNode(ISD::BITCAST, dl, OpVT, Tmp1);

    SDValue AllOnes = DAG.getTargetConstant(ARM_AM::createVMOVModImm(0xe, 0xff),
                                            dl, MVT::i32);
    AllOnes = DAG.getNode(ARMISD::VMOVIMM, dl, MVT::v8i8, AllOnes);
    SDValue MaskNot = DAG.getNode(ISD::XOR, dl, OpVT, Mask,
                                  DAG.getNode(ISD::BITCAST, dl, OpVT, AllOnes));

    SDValue Res = DAG.getNode(ISD::OR, dl, OpVT,
                              DAG.getNode(ISD::AND, dl, OpVT, Tmp1, Mask),
                              DAG.getNode(ISD::AND, dl, OpVT, Tmp0, MaskNot));
    if (VT == MVT::f32) {
      Res = DAG.getNode(ISD::BITCAST, dl, MVT::v2f32, Res);
      Res = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::f32, Res,
                        DAG.getConstant(0, dl, MVT::i32));
    } else {
      Res = DAG.getNode(ISD::BITCAST, dl, MVT::f64, Res);
    }

    return Res;
  }

  // Bitcast operand 1 to i32.
  if (SrcVT == MVT::f64)
    Tmp1 = DAG.getNode(ARMISD::VMOVRRD, dl, DAG.getVTList(MVT::i32, MVT::i32),
                       Tmp1).getValue(1);
  Tmp1 = DAG.getNode(ISD::BITCAST, dl, MVT::i32, Tmp1);

  // Or in the signbit with integer operations.
  SDValue Mask1 = DAG.getConstant(0x80000000, dl, MVT::i32);
  SDValue Mask2 = DAG.getConstant(0x7fffffff, dl, MVT::i32);
  Tmp1 = DAG.getNode(ISD::AND, dl, MVT::i32, Tmp1, Mask1);
  if (VT == MVT::f32) {
    Tmp0 = DAG.getNode(ISD::AND, dl, MVT::i32,
                       DAG.getNode(ISD::BITCAST, dl, MVT::i32, Tmp0), Mask2);
    return DAG.getNode(ISD::BITCAST, dl, MVT::f32,
                       DAG.getNode(ISD::OR, dl, MVT::i32, Tmp0, Tmp1));
  }

  // f64: Or the high part with signbit and then combine two parts.
  Tmp0 = DAG.getNode(ARMISD::VMOVRRD, dl, DAG.getVTList(MVT::i32, MVT::i32),
                     Tmp0);
  SDValue Lo = Tmp0.getValue(0);
  SDValue Hi = DAG.getNode(ISD::AND, dl, MVT::i32, Tmp0.getValue(1), Mask2);
  Hi = DAG.getNode(ISD::OR, dl, MVT::i32, Hi, Tmp1);
  return DAG.getNode(ARMISD::VMOVDRR, dl, MVT::f64, Lo, Hi);
}

SDValue ARMTargetLowering::LowerRETURNADDR(SDValue Op, SelectionDAG &DAG) const{
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setReturnAddressIsTaken(true);

  if (verifyReturnAddressArgumentIsConstant(Op, DAG))
    return SDValue();

  EVT VT = Op.getValueType();
  SDLoc dl(Op);
  unsigned Depth = Op.getConstantOperandVal(0);
  if (Depth) {
    SDValue FrameAddr = LowerFRAMEADDR(Op, DAG);
    SDValue Offset = DAG.getConstant(4, dl, MVT::i32);
    return DAG.getLoad(VT, dl, DAG.getEntryNode(),
                       DAG.getNode(ISD::ADD, dl, VT, FrameAddr, Offset),
                       MachinePointerInfo());
  }

  // Return LR, which contains the return address. Mark it an implicit live-in.
  Register Reg = MF.addLiveIn(ARM::LR, getRegClassFor(MVT::i32));
  return DAG.getCopyFromReg(DAG.getEntryNode(), dl, Reg, VT);
}

SDValue ARMTargetLowering::LowerFRAMEADDR(SDValue Op, SelectionDAG &DAG) const {
  const ARMBaseRegisterInfo &ARI =
    *static_cast<const ARMBaseRegisterInfo*>(RegInfo);
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  MFI.setFrameAddressIsTaken(true);

  EVT VT = Op.getValueType();
  SDLoc dl(Op);  // FIXME probably not meaningful
  unsigned Depth = Op.getConstantOperandVal(0);
  Register FrameReg = ARI.getFrameRegister(MF);
  SDValue FrameAddr = DAG.getCopyFromReg(DAG.getEntryNode(), dl, FrameReg, VT);
  while (Depth--)
    FrameAddr = DAG.getLoad(VT, dl, DAG.getEntryNode(), FrameAddr,
                            MachinePointerInfo());
  return FrameAddr;
}

// FIXME? Maybe this could be a TableGen attribute on some registers and
// this table could be generated automatically from RegInfo.
Register ARMTargetLowering::getRegisterByName(const char* RegName, LLT VT,
                                              const MachineFunction &MF) const {
  Register Reg = StringSwitch<unsigned>(RegName)
                       .Case("sp", ARM::SP)
                       .Default(0);
  if (Reg)
    return Reg;
  report_fatal_error(Twine("Invalid register name \""
                              + StringRef(RegName)  + "\"."));
}

// Result is 64 bit value so split into two 32 bit values and return as a
// pair of values.
static void ExpandREAD_REGISTER(SDNode *N, SmallVectorImpl<SDValue> &Results,
                                SelectionDAG &DAG) {
  SDLoc DL(N);

  // This function is only supposed to be called for i64 type destination.
  assert(N->getValueType(0) == MVT::i64
          && "ExpandREAD_REGISTER called for non-i64 type result.");

  SDValue Read = DAG.getNode(ISD::READ_REGISTER, DL,
                             DAG.getVTList(MVT::i32, MVT::i32, MVT::Other),
                             N->getOperand(0),
                             N->getOperand(1));

  Results.push_back(DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i64, Read.getValue(0),
                    Read.getValue(1)));
  Results.push_back(Read.getOperand(0));
}

/// \p BC is a bitcast that is about to be turned into a VMOVDRR.
/// When \p DstVT, the destination type of \p BC, is on the vector
/// register bank and the source of bitcast, \p Op, operates on the same bank,
/// it might be possible to combine them, such that everything stays on the
/// vector register bank.
/// \p return The node that would replace \p BT, if the combine
/// is possible.
static SDValue CombineVMOVDRRCandidateWithVecOp(const SDNode *BC,
                                                SelectionDAG &DAG) {
  SDValue Op = BC->getOperand(0);
  EVT DstVT = BC->getValueType(0);

  // The only vector instruction that can produce a scalar (remember,
  // since the bitcast was about to be turned into VMOVDRR, the source
  // type is i64) from a vector is EXTRACT_VECTOR_ELT.
  // Moreover, we can do this combine only if there is one use.
  // Finally, if the destination type is not a vector, there is not
  // much point on forcing everything on the vector bank.
  if (!DstVT.isVector() || Op.getOpcode() != ISD::EXTRACT_VECTOR_ELT ||
      !Op.hasOneUse())
    return SDValue();

  // If the index is not constant, we will introduce an additional
  // multiply that will stick.
  // Give up in that case.
  ConstantSDNode *Index = dyn_cast<ConstantSDNode>(Op.getOperand(1));
  if (!Index)
    return SDValue();
  unsigned DstNumElt = DstVT.getVectorNumElements();

  // Compute the new index.
  const APInt &APIntIndex = Index->getAPIntValue();
  APInt NewIndex(APIntIndex.getBitWidth(), DstNumElt);
  NewIndex *= APIntIndex;
  // Check if the new constant index fits into i32.
  if (NewIndex.getBitWidth() > 32)
    return SDValue();

  // vMTy bitcast(i64 extractelt vNi64 src, i32 index) ->
  // vMTy extractsubvector vNxMTy (bitcast vNi64 src), i32 index*M)
  SDLoc dl(Op);
  SDValue ExtractSrc = Op.getOperand(0);
  EVT VecVT = EVT::getVectorVT(
      *DAG.getContext(), DstVT.getScalarType(),
      ExtractSrc.getValueType().getVectorNumElements() * DstNumElt);
  SDValue BitCast = DAG.getNode(ISD::BITCAST, dl, VecVT, ExtractSrc);
  return DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, DstVT, BitCast,
                     DAG.getConstant(NewIndex.getZExtValue(), dl, MVT::i32));
}

/// ExpandBITCAST - If the target supports VFP, this function is called to
/// expand a bit convert where either the source or destination type is i64 to
/// use a VMOVDRR or VMOVRRD node.  This should not be done when the non-i64
/// operand type is illegal (e.g., v2f32 for a target that doesn't support
/// vectors), since the legalizer won't know what to do with that.
SDValue ARMTargetLowering::ExpandBITCAST(SDNode *N, SelectionDAG &DAG,
                                         const ARMSubtarget *Subtarget) const {
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDLoc dl(N);
  SDValue Op = N->getOperand(0);

  // This function is only supposed to be called for i16 and i64 types, either
  // as the source or destination of the bit convert.
  EVT SrcVT = Op.getValueType();
  EVT DstVT = N->getValueType(0);

  if ((SrcVT == MVT::i16 || SrcVT == MVT::i32) &&
      (DstVT == MVT::f16 || DstVT == MVT::bf16))
    return MoveToHPR(SDLoc(N), DAG, MVT::i32, DstVT.getSimpleVT(),
                     DAG.getNode(ISD::ZERO_EXTEND, SDLoc(N), MVT::i32, Op));

  if ((DstVT == MVT::i16 || DstVT == MVT::i32) &&
      (SrcVT == MVT::f16 || SrcVT == MVT::bf16))
    return DAG.getNode(
        ISD::TRUNCATE, SDLoc(N), DstVT,
        MoveFromHPR(SDLoc(N), DAG, MVT::i32, SrcVT.getSimpleVT(), Op));

  if (!(SrcVT == MVT::i64 || DstVT == MVT::i64))
    return SDValue();

  // Turn i64->f64 into VMOVDRR.
  if (SrcVT == MVT::i64 && TLI.isTypeLegal(DstVT)) {
    // Do not force values to GPRs (this is what VMOVDRR does for the inputs)
    // if we can combine the bitcast with its source.
    if (SDValue Val = CombineVMOVDRRCandidateWithVecOp(N, DAG))
      return Val;
    SDValue Lo, Hi;
    std::tie(Lo, Hi) = DAG.SplitScalar(Op, dl, MVT::i32, MVT::i32);
    return DAG.getNode(ISD::BITCAST, dl, DstVT,
                       DAG.getNode(ARMISD::VMOVDRR, dl, MVT::f64, Lo, Hi));
  }

  // Turn f64->i64 into VMOVRRD.
  if (DstVT == MVT::i64 && TLI.isTypeLegal(SrcVT)) {
    SDValue Cvt;
    if (DAG.getDataLayout().isBigEndian() && SrcVT.isVector() &&
        SrcVT.getVectorNumElements() > 1)
      Cvt = DAG.getNode(ARMISD::VMOVRRD, dl,
                        DAG.getVTList(MVT::i32, MVT::i32),
                        DAG.getNode(ARMISD::VREV64, dl, SrcVT, Op));
    else
      Cvt = DAG.getNode(ARMISD::VMOVRRD, dl,
                        DAG.getVTList(MVT::i32, MVT::i32), Op);
    // Merge the pieces into a single i64 value.
    return DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, Cvt, Cvt.getValue(1));
  }

  return SDValue();
}

/// getZeroVector - Returns a vector of specified type with all zero elements.
/// Zero vectors are used to represent vector negation and in those cases
/// will be implemented with the NEON VNEG instruction.  However, VNEG does
/// not support i64 elements, so sometimes the zero vectors will need to be
/// explicitly constructed.  Regardless, use a canonical VMOV to create the
/// zero vector.
static SDValue getZeroVector(EVT VT, SelectionDAG &DAG, const SDLoc &dl) {
  assert(VT.isVector() && "Expected a vector type");
  // The canonical modified immediate encoding of a zero vector is....0!
  SDValue EncodedVal = DAG.getTargetConstant(0, dl, MVT::i32);
  EVT VmovVT = VT.is128BitVector() ? MVT::v4i32 : MVT::v2i32;
  SDValue Vmov = DAG.getNode(ARMISD::VMOVIMM, dl, VmovVT, EncodedVal);
  return DAG.getNode(ISD::BITCAST, dl, VT, Vmov);
}

/// LowerShiftRightParts - Lower SRA_PARTS, which returns two
/// i32 values and take a 2 x i32 value to shift plus a shift amount.
SDValue ARMTargetLowering::LowerShiftRightParts(SDValue Op,
                                                SelectionDAG &DAG) const {
  assert(Op.getNumOperands() == 3 && "Not a double-shift!");
  EVT VT = Op.getValueType();
  unsigned VTBits = VT.getSizeInBits();
  SDLoc dl(Op);
  SDValue ShOpLo = Op.getOperand(0);
  SDValue ShOpHi = Op.getOperand(1);
  SDValue ShAmt  = Op.getOperand(2);
  SDValue ARMcc;
  SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
  unsigned Opc = (Op.getOpcode() == ISD::SRA_PARTS) ? ISD::SRA : ISD::SRL;

  assert(Op.getOpcode() == ISD::SRA_PARTS || Op.getOpcode() == ISD::SRL_PARTS);

  SDValue RevShAmt = DAG.getNode(ISD::SUB, dl, MVT::i32,
                                 DAG.getConstant(VTBits, dl, MVT::i32), ShAmt);
  SDValue Tmp1 = DAG.getNode(ISD::SRL, dl, VT, ShOpLo, ShAmt);
  SDValue ExtraShAmt = DAG.getNode(ISD::SUB, dl, MVT::i32, ShAmt,
                                   DAG.getConstant(VTBits, dl, MVT::i32));
  SDValue Tmp2 = DAG.getNode(ISD::SHL, dl, VT, ShOpHi, RevShAmt);
  SDValue LoSmallShift = DAG.getNode(ISD::OR, dl, VT, Tmp1, Tmp2);
  SDValue LoBigShift = DAG.getNode(Opc, dl, VT, ShOpHi, ExtraShAmt);
  SDValue CmpLo = getARMCmp(ExtraShAmt, DAG.getConstant(0, dl, MVT::i32),
                            ISD::SETGE, ARMcc, DAG, dl);
  SDValue Lo = DAG.getNode(ARMISD::CMOV, dl, VT, LoSmallShift, LoBigShift,
                           ARMcc, CCR, CmpLo);

  SDValue HiSmallShift = DAG.getNode(Opc, dl, VT, ShOpHi, ShAmt);
  SDValue HiBigShift = Opc == ISD::SRA
                           ? DAG.getNode(Opc, dl, VT, ShOpHi,
                                         DAG.getConstant(VTBits - 1, dl, VT))
                           : DAG.getConstant(0, dl, VT);
  SDValue CmpHi = getARMCmp(ExtraShAmt, DAG.getConstant(0, dl, MVT::i32),
                            ISD::SETGE, ARMcc, DAG, dl);
  SDValue Hi = DAG.getNode(ARMISD::CMOV, dl, VT, HiSmallShift, HiBigShift,
                           ARMcc, CCR, CmpHi);

  SDValue Ops[2] = { Lo, Hi };
  return DAG.getMergeValues(Ops, dl);
}

/// LowerShiftLeftParts - Lower SHL_PARTS, which returns two
/// i32 values and take a 2 x i32 value to shift plus a shift amount.
SDValue ARMTargetLowering::LowerShiftLeftParts(SDValue Op,
                                               SelectionDAG &DAG) const {
  assert(Op.getNumOperands() == 3 && "Not a double-shift!");
  EVT VT = Op.getValueType();
  unsigned VTBits = VT.getSizeInBits();
  SDLoc dl(Op);
  SDValue ShOpLo = Op.getOperand(0);
  SDValue ShOpHi = Op.getOperand(1);
  SDValue ShAmt  = Op.getOperand(2);
  SDValue ARMcc;
  SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);

  assert(Op.getOpcode() == ISD::SHL_PARTS);
  SDValue RevShAmt = DAG.getNode(ISD::SUB, dl, MVT::i32,
                                 DAG.getConstant(VTBits, dl, MVT::i32), ShAmt);
  SDValue Tmp1 = DAG.getNode(ISD::SRL, dl, VT, ShOpLo, RevShAmt);
  SDValue Tmp2 = DAG.getNode(ISD::SHL, dl, VT, ShOpHi, ShAmt);
  SDValue HiSmallShift = DAG.getNode(ISD::OR, dl, VT, Tmp1, Tmp2);

  SDValue ExtraShAmt = DAG.getNode(ISD::SUB, dl, MVT::i32, ShAmt,
                                   DAG.getConstant(VTBits, dl, MVT::i32));
  SDValue HiBigShift = DAG.getNode(ISD::SHL, dl, VT, ShOpLo, ExtraShAmt);
  SDValue CmpHi = getARMCmp(ExtraShAmt, DAG.getConstant(0, dl, MVT::i32),
                            ISD::SETGE, ARMcc, DAG, dl);
  SDValue Hi = DAG.getNode(ARMISD::CMOV, dl, VT, HiSmallShift, HiBigShift,
                           ARMcc, CCR, CmpHi);

  SDValue CmpLo = getARMCmp(ExtraShAmt, DAG.getConstant(0, dl, MVT::i32),
                          ISD::SETGE, ARMcc, DAG, dl);
  SDValue LoSmallShift = DAG.getNode(ISD::SHL, dl, VT, ShOpLo, ShAmt);
  SDValue Lo = DAG.getNode(ARMISD::CMOV, dl, VT, LoSmallShift,
                           DAG.getConstant(0, dl, VT), ARMcc, CCR, CmpLo);

  SDValue Ops[2] = { Lo, Hi };
  return DAG.getMergeValues(Ops, dl);
}

SDValue ARMTargetLowering::LowerGET_ROUNDING(SDValue Op,
                                             SelectionDAG &DAG) const {
  // The rounding mode is in bits 23:22 of the FPSCR.
  // The ARM rounding mode value to FLT_ROUNDS mapping is 0->1, 1->2, 2->3, 3->0
  // The formula we use to implement this is (((FPSCR + 1 << 22) >> 22) & 3)
  // so that the shift + and get folded into a bitfield extract.
  SDLoc dl(Op);
  SDValue Chain = Op.getOperand(0);
  SDValue Ops[] = {Chain,
                   DAG.getConstant(Intrinsic::arm_get_fpscr, dl, MVT::i32)};

  SDValue FPSCR =
      DAG.getNode(ISD::INTRINSIC_W_CHAIN, dl, {MVT::i32, MVT::Other}, Ops);
  Chain = FPSCR.getValue(1);
  SDValue FltRounds = DAG.getNode(ISD::ADD, dl, MVT::i32, FPSCR,
                                  DAG.getConstant(1U << 22, dl, MVT::i32));
  SDValue RMODE = DAG.getNode(ISD::SRL, dl, MVT::i32, FltRounds,
                              DAG.getConstant(22, dl, MVT::i32));
  SDValue And = DAG.getNode(ISD::AND, dl, MVT::i32, RMODE,
                            DAG.getConstant(3, dl, MVT::i32));
  return DAG.getMergeValues({And, Chain}, dl);
}

SDValue ARMTargetLowering::LowerSET_ROUNDING(SDValue Op,
                                             SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Chain = Op->getOperand(0);
  SDValue RMValue = Op->getOperand(1);

  // The rounding mode is in bits 23:22 of the FPSCR.
  // The llvm.set.rounding argument value to ARM rounding mode value mapping
  // is 0->3, 1->0, 2->1, 3->2. The formula we use to implement this is
  // ((arg - 1) & 3) << 22).
  //
  // It is expected that the argument of llvm.set.rounding is within the
  // segment [0, 3], so NearestTiesToAway (4) is not handled here. It is
  // responsibility of the code generated llvm.set.rounding to ensure this
  // condition.

  // Calculate new value of FPSCR[23:22].
  RMValue = DAG.getNode(ISD::SUB, DL, MVT::i32, RMValue,
                        DAG.getConstant(1, DL, MVT::i32));
  RMValue = DAG.getNode(ISD::AND, DL, MVT::i32, RMValue,
                        DAG.getConstant(0x3, DL, MVT::i32));
  RMValue = DAG.getNode(ISD::SHL, DL, MVT::i32, RMValue,
                        DAG.getConstant(ARM::RoundingBitsPos, DL, MVT::i32));

  // Get current value of FPSCR.
  SDValue Ops[] = {Chain,
                   DAG.getConstant(Intrinsic::arm_get_fpscr, DL, MVT::i32)};
  SDValue FPSCR =
      DAG.getNode(ISD::INTRINSIC_W_CHAIN, DL, {MVT::i32, MVT::Other}, Ops);
  Chain = FPSCR.getValue(1);
  FPSCR = FPSCR.getValue(0);

  // Put new rounding mode into FPSCR[23:22].
  const unsigned RMMask = ~(ARM::Rounding::rmMask << ARM::RoundingBitsPos);
  FPSCR = DAG.getNode(ISD::AND, DL, MVT::i32, FPSCR,
                      DAG.getConstant(RMMask, DL, MVT::i32));
  FPSCR = DAG.getNode(ISD::OR, DL, MVT::i32, FPSCR, RMValue);
  SDValue Ops2[] = {
      Chain, DAG.getConstant(Intrinsic::arm_set_fpscr, DL, MVT::i32), FPSCR};
  return DAG.getNode(ISD::INTRINSIC_VOID, DL, MVT::Other, Ops2);
}

SDValue ARMTargetLowering::LowerSET_FPMODE(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Chain = Op->getOperand(0);
  SDValue Mode = Op->getOperand(1);

  // Generate nodes to build:
  // FPSCR = (FPSCR & FPStatusBits) | (Mode & ~FPStatusBits)
  SDValue Ops[] = {Chain,
                   DAG.getConstant(Intrinsic::arm_get_fpscr, DL, MVT::i32)};
  SDValue FPSCR =
      DAG.getNode(ISD::INTRINSIC_W_CHAIN, DL, {MVT::i32, MVT::Other}, Ops);
  Chain = FPSCR.getValue(1);
  FPSCR = FPSCR.getValue(0);

  SDValue FPSCRMasked =
      DAG.getNode(ISD::AND, DL, MVT::i32, FPSCR,
                  DAG.getConstant(ARM::FPStatusBits, DL, MVT::i32));
  SDValue InputMasked =
      DAG.getNode(ISD::AND, DL, MVT::i32, Mode,
                  DAG.getConstant(~ARM::FPStatusBits, DL, MVT::i32));
  FPSCR = DAG.getNode(ISD::OR, DL, MVT::i32, FPSCRMasked, InputMasked);

  SDValue Ops2[] = {
      Chain, DAG.getConstant(Intrinsic::arm_set_fpscr, DL, MVT::i32), FPSCR};
  return DAG.getNode(ISD::INTRINSIC_VOID, DL, MVT::Other, Ops2);
}

SDValue ARMTargetLowering::LowerRESET_FPMODE(SDValue Op,
                                             SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Chain = Op->getOperand(0);

  // To get the default FP mode all control bits are cleared:
  // FPSCR = FPSCR & (FPStatusBits | FPReservedBits)
  SDValue Ops[] = {Chain,
                   DAG.getConstant(Intrinsic::arm_get_fpscr, DL, MVT::i32)};
  SDValue FPSCR =
      DAG.getNode(ISD::INTRINSIC_W_CHAIN, DL, {MVT::i32, MVT::Other}, Ops);
  Chain = FPSCR.getValue(1);
  FPSCR = FPSCR.getValue(0);

  SDValue FPSCRMasked = DAG.getNode(
      ISD::AND, DL, MVT::i32, FPSCR,
      DAG.getConstant(ARM::FPStatusBits | ARM::FPReservedBits, DL, MVT::i32));
  SDValue Ops2[] = {Chain,
                    DAG.getConstant(Intrinsic::arm_set_fpscr, DL, MVT::i32),
                    FPSCRMasked};
  return DAG.getNode(ISD::INTRINSIC_VOID, DL, MVT::Other, Ops2);
}

static SDValue LowerCTTZ(SDNode *N, SelectionDAG &DAG,
                         const ARMSubtarget *ST) {
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  if (VT.isVector() && ST->hasNEON()) {

    // Compute the least significant set bit: LSB = X & -X
    SDValue X = N->getOperand(0);
    SDValue NX = DAG.getNode(ISD::SUB, dl, VT, getZeroVector(VT, DAG, dl), X);
    SDValue LSB = DAG.getNode(ISD::AND, dl, VT, X, NX);

    EVT ElemTy = VT.getVectorElementType();

    if (ElemTy == MVT::i8) {
      // Compute with: cttz(x) = ctpop(lsb - 1)
      SDValue One = DAG.getNode(ARMISD::VMOVIMM, dl, VT,
                                DAG.getTargetConstant(1, dl, ElemTy));
      SDValue Bits = DAG.getNode(ISD::SUB, dl, VT, LSB, One);
      return DAG.getNode(ISD::CTPOP, dl, VT, Bits);
    }

    if ((ElemTy == MVT::i16 || ElemTy == MVT::i32) &&
        (N->getOpcode() == ISD::CTTZ_ZERO_UNDEF)) {
      // Compute with: cttz(x) = (width - 1) - ctlz(lsb), if x != 0
      unsigned NumBits = ElemTy.getSizeInBits();
      SDValue WidthMinus1 =
          DAG.getNode(ARMISD::VMOVIMM, dl, VT,
                      DAG.getTargetConstant(NumBits - 1, dl, ElemTy));
      SDValue CTLZ = DAG.getNode(ISD::CTLZ, dl, VT, LSB);
      return DAG.getNode(ISD::SUB, dl, VT, WidthMinus1, CTLZ);
    }

    // Compute with: cttz(x) = ctpop(lsb - 1)

    // Compute LSB - 1.
    SDValue Bits;
    if (ElemTy == MVT::i64) {
      // Load constant 0xffff'ffff'ffff'ffff to register.
      SDValue FF = DAG.getNode(ARMISD::VMOVIMM, dl, VT,
                               DAG.getTargetConstant(0x1eff, dl, MVT::i32));
      Bits = DAG.getNode(ISD::ADD, dl, VT, LSB, FF);
    } else {
      SDValue One = DAG.getNode(ARMISD::VMOVIMM, dl, VT,
                                DAG.getTargetConstant(1, dl, ElemTy));
      Bits = DAG.getNode(ISD::SUB, dl, VT, LSB, One);
    }
    return DAG.getNode(ISD::CTPOP, dl, VT, Bits);
  }

  if (!ST->hasV6T2Ops())
    return SDValue();

  SDValue rbit = DAG.getNode(ISD::BITREVERSE, dl, VT, N->getOperand(0));
  return DAG.getNode(ISD::CTLZ, dl, VT, rbit);
}

static SDValue LowerCTPOP(SDNode *N, SelectionDAG &DAG,
                          const ARMSubtarget *ST) {
  EVT VT = N->getValueType(0);
  SDLoc DL(N);

  assert(ST->hasNEON() && "Custom ctpop lowering requires NEON.");
  assert((VT == MVT::v1i64 || VT == MVT::v2i64 || VT == MVT::v2i32 ||
          VT == MVT::v4i32 || VT == MVT::v4i16 || VT == MVT::v8i16) &&
         "Unexpected type for custom ctpop lowering");

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT VT8Bit = VT.is64BitVector() ? MVT::v8i8 : MVT::v16i8;
  SDValue Res = DAG.getBitcast(VT8Bit, N->getOperand(0));
  Res = DAG.getNode(ISD::CTPOP, DL, VT8Bit, Res);

  // Widen v8i8/v16i8 CTPOP result to VT by repeatedly widening pairwise adds.
  unsigned EltSize = 8;
  unsigned NumElts = VT.is64BitVector() ? 8 : 16;
  while (EltSize != VT.getScalarSizeInBits()) {
    SmallVector<SDValue, 8> Ops;
    Ops.push_back(DAG.getConstant(Intrinsic::arm_neon_vpaddlu, DL,
                                  TLI.getPointerTy(DAG.getDataLayout())));
    Ops.push_back(Res);

    EltSize *= 2;
    NumElts /= 2;
    MVT WidenVT = MVT::getVectorVT(MVT::getIntegerVT(EltSize), NumElts);
    Res = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, DL, WidenVT, Ops);
  }

  return Res;
}

/// Getvshiftimm - Check if this is a valid build_vector for the immediate
/// operand of a vector shift operation, where all the elements of the
/// build_vector must have the same constant integer value.
static bool getVShiftImm(SDValue Op, unsigned ElementBits, int64_t &Cnt) {
  // Ignore bit_converts.
  while (Op.getOpcode() == ISD::BITCAST)
    Op = Op.getOperand(0);
  BuildVectorSDNode *BVN = dyn_cast<BuildVectorSDNode>(Op.getNode());
  APInt SplatBits, SplatUndef;
  unsigned SplatBitSize;
  bool HasAnyUndefs;
  if (!BVN ||
      !BVN->isConstantSplat(SplatBits, SplatUndef, SplatBitSize, HasAnyUndefs,
                            ElementBits) ||
      SplatBitSize > ElementBits)
    return false;
  Cnt = SplatBits.getSExtValue();
  return true;
}

/// isVShiftLImm - Check if this is a valid build_vector for the immediate
/// operand of a vector shift left operation.  That value must be in the range:
///   0 <= Value < ElementBits for a left shift; or
///   0 <= Value <= ElementBits for a long left shift.
static bool isVShiftLImm(SDValue Op, EVT VT, bool isLong, int64_t &Cnt) {
  assert(VT.isVector() && "vector shift count is not a vector type");
  int64_t ElementBits = VT.getScalarSizeInBits();
  if (!getVShiftImm(Op, ElementBits, Cnt))
    return false;
  return (Cnt >= 0 && (isLong ? Cnt - 1 : Cnt) < ElementBits);
}

/// isVShiftRImm - Check if this is a valid build_vector for the immediate
/// operand of a vector shift right operation.  For a shift opcode, the value
/// is positive, but for an intrinsic the value count must be negative. The
/// absolute value must be in the range:
///   1 <= |Value| <= ElementBits for a right shift; or
///   1 <= |Value| <= ElementBits/2 for a narrow right shift.
static bool isVShiftRImm(SDValue Op, EVT VT, bool isNarrow, bool isIntrinsic,
                         int64_t &Cnt) {
  assert(VT.isVector() && "vector shift count is not a vector type");
  int64_t ElementBits = VT.getScalarSizeInBits();
  if (!getVShiftImm(Op, ElementBits, Cnt))
    return false;
  if (!isIntrinsic)
    return (Cnt >= 1 && Cnt <= (isNarrow ? ElementBits / 2 : ElementBits));
  if (Cnt >= -(isNarrow ? ElementBits / 2 : ElementBits) && Cnt <= -1) {
    Cnt = -Cnt;
    return true;
  }
  return false;
}

static SDValue LowerShift(SDNode *N, SelectionDAG &DAG,
                          const ARMSubtarget *ST) {
  EVT VT = N->getValueType(0);
  SDLoc dl(N);
  int64_t Cnt;

  if (!VT.isVector())
    return SDValue();

  // We essentially have two forms here. Shift by an immediate and shift by a
  // vector register (there are also shift by a gpr, but that is just handled
  // with a tablegen pattern). We cannot easily match shift by an immediate in
  // tablegen so we do that here and generate a VSHLIMM/VSHRsIMM/VSHRuIMM.
  // For shifting by a vector, we don't have VSHR, only VSHL (which can be
  // signed or unsigned, and a negative shift indicates a shift right).
  if (N->getOpcode() == ISD::SHL) {
    if (isVShiftLImm(N->getOperand(1), VT, false, Cnt))
      return DAG.getNode(ARMISD::VSHLIMM, dl, VT, N->getOperand(0),
                         DAG.getConstant(Cnt, dl, MVT::i32));
    return DAG.getNode(ARMISD::VSHLu, dl, VT, N->getOperand(0),
                       N->getOperand(1));
  }

  assert((N->getOpcode() == ISD::SRA || N->getOpcode() == ISD::SRL) &&
         "unexpected vector shift opcode");

  if (isVShiftRImm(N->getOperand(1), VT, false, false, Cnt)) {
    unsigned VShiftOpc =
        (N->getOpcode() == ISD::SRA ? ARMISD::VSHRsIMM : ARMISD::VSHRuIMM);
    return DAG.getNode(VShiftOpc, dl, VT, N->getOperand(0),
                       DAG.getConstant(Cnt, dl, MVT::i32));
  }

  // Other right shifts we don't have operations for (we use a shift left by a
  // negative number).
  EVT ShiftVT = N->getOperand(1).getValueType();
  SDValue NegatedCount = DAG.getNode(
      ISD::SUB, dl, ShiftVT, getZeroVector(ShiftVT, DAG, dl), N->getOperand(1));
  unsigned VShiftOpc =
      (N->getOpcode() == ISD::SRA ? ARMISD::VSHLs : ARMISD::VSHLu);
  return DAG.getNode(VShiftOpc, dl, VT, N->getOperand(0), NegatedCount);
}

static SDValue Expand64BitShift(SDNode *N, SelectionDAG &DAG,
                                const ARMSubtarget *ST) {
  EVT VT = N->getValueType(0);
  SDLoc dl(N);

  // We can get here for a node like i32 = ISD::SHL i32, i64
  if (VT != MVT::i64)
    return SDValue();

  assert((N->getOpcode() == ISD::SRL || N->getOpcode() == ISD::SRA ||
          N->getOpcode() == ISD::SHL) &&
         "Unknown shift to lower!");

  unsigned ShOpc = N->getOpcode();
  if (ST->hasMVEIntegerOps()) {
    SDValue ShAmt = N->getOperand(1);
    unsigned ShPartsOpc = ARMISD::LSLL;
    ConstantSDNode *Con = dyn_cast<ConstantSDNode>(ShAmt);

    // If the shift amount is greater than 32 or has a greater bitwidth than 64
    // then do the default optimisation
    if ((!Con && ShAmt->getValueType(0).getSizeInBits() > 64) ||
        (Con && (Con->getAPIntValue() == 0 || Con->getAPIntValue().uge(32))))
      return SDValue();

    // Extract the lower 32 bits of the shift amount if it's not an i32
    if (ShAmt->getValueType(0) != MVT::i32)
      ShAmt = DAG.getZExtOrTrunc(ShAmt, dl, MVT::i32);

    if (ShOpc == ISD::SRL) {
      if (!Con)
        // There is no t2LSRLr instruction so negate and perform an lsll if the
        // shift amount is in a register, emulating a right shift.
        ShAmt = DAG.getNode(ISD::SUB, dl, MVT::i32,
                            DAG.getConstant(0, dl, MVT::i32), ShAmt);
      else
        // Else generate an lsrl on the immediate shift amount
        ShPartsOpc = ARMISD::LSRL;
    } else if (ShOpc == ISD::SRA)
      ShPartsOpc = ARMISD::ASRL;

    // Split Lower/Upper 32 bits of the destination/source
    SDValue Lo, Hi;
    std::tie(Lo, Hi) =
        DAG.SplitScalar(N->getOperand(0), dl, MVT::i32, MVT::i32);
    // Generate the shift operation as computed above
    Lo = DAG.getNode(ShPartsOpc, dl, DAG.getVTList(MVT::i32, MVT::i32), Lo, Hi,
                     ShAmt);
    // The upper 32 bits come from the second return value of lsll
    Hi = SDValue(Lo.getNode(), 1);
    return DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, Lo, Hi);
  }

  // We only lower SRA, SRL of 1 here, all others use generic lowering.
  if (!isOneConstant(N->getOperand(1)) || N->getOpcode() == ISD::SHL)
    return SDValue();

  // If we are in thumb mode, we don't have RRX.
  if (ST->isThumb1Only())
    return SDValue();

  // Okay, we have a 64-bit SRA or SRL of 1.  Lower this to an RRX expr.
  SDValue Lo, Hi;
  std::tie(Lo, Hi) = DAG.SplitScalar(N->getOperand(0), dl, MVT::i32, MVT::i32);

  // First, build a SRA_GLUE/SRL_GLUE op, which shifts the top part by one and
  // captures the result into a carry flag.
  unsigned Opc = N->getOpcode() == ISD::SRL ? ARMISD::SRL_GLUE:ARMISD::SRA_GLUE;
  Hi = DAG.getNode(Opc, dl, DAG.getVTList(MVT::i32, MVT::Glue), Hi);

  // The low part is an ARMISD::RRX operand, which shifts the carry in.
  Lo = DAG.getNode(ARMISD::RRX, dl, MVT::i32, Lo, Hi.getValue(1));

  // Merge the pieces into a single i64 value.
 return DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, Lo, Hi);
}

static SDValue LowerVSETCC(SDValue Op, SelectionDAG &DAG,
                           const ARMSubtarget *ST) {
  bool Invert = false;
  bool Swap = false;
  unsigned Opc = ARMCC::AL;

  SDValue Op0 = Op.getOperand(0);
  SDValue Op1 = Op.getOperand(1);
  SDValue CC = Op.getOperand(2);
  EVT VT = Op.getValueType();
  ISD::CondCode SetCCOpcode = cast<CondCodeSDNode>(CC)->get();
  SDLoc dl(Op);

  EVT CmpVT;
  if (ST->hasNEON())
    CmpVT = Op0.getValueType().changeVectorElementTypeToInteger();
  else {
    assert(ST->hasMVEIntegerOps() &&
           "No hardware support for integer vector comparison!");

    if (Op.getValueType().getVectorElementType() != MVT::i1)
      return SDValue();

    // Make sure we expand floating point setcc to scalar if we do not have
    // mve.fp, so that we can handle them from there.
    if (Op0.getValueType().isFloatingPoint() && !ST->hasMVEFloatOps())
      return SDValue();

    CmpVT = VT;
  }

  if (Op0.getValueType().getVectorElementType() == MVT::i64 &&
      (SetCCOpcode == ISD::SETEQ || SetCCOpcode == ISD::SETNE)) {
    // Special-case integer 64-bit equality comparisons. They aren't legal,
    // but they can be lowered with a few vector instructions.
    unsigned CmpElements = CmpVT.getVectorNumElements() * 2;
    EVT SplitVT = EVT::getVectorVT(*DAG.getContext(), MVT::i32, CmpElements);
    SDValue CastOp0 = DAG.getNode(ISD::BITCAST, dl, SplitVT, Op0);
    SDValue CastOp1 = DAG.getNode(ISD::BITCAST, dl, SplitVT, Op1);
    SDValue Cmp = DAG.getNode(ISD::SETCC, dl, SplitVT, CastOp0, CastOp1,
                              DAG.getCondCode(ISD::SETEQ));
    SDValue Reversed = DAG.getNode(ARMISD::VREV64, dl, SplitVT, Cmp);
    SDValue Merged = DAG.getNode(ISD::AND, dl, SplitVT, Cmp, Reversed);
    Merged = DAG.getNode(ISD::BITCAST, dl, CmpVT, Merged);
    if (SetCCOpcode == ISD::SETNE)
      Merged = DAG.getNOT(dl, Merged, CmpVT);
    Merged = DAG.getSExtOrTrunc(Merged, dl, VT);
    return Merged;
  }

  if (CmpVT.getVectorElementType() == MVT::i64)
    // 64-bit comparisons are not legal in general.
    return SDValue();

  if (Op1.getValueType().isFloatingPoint()) {
    switch (SetCCOpcode) {
    default: llvm_unreachable("Illegal FP comparison");
    case ISD::SETUNE:
    case ISD::SETNE:
      if (ST->hasMVEFloatOps()) {
        Opc = ARMCC::NE; break;
      } else {
        Invert = true; [[fallthrough]];
      }
    case ISD::SETOEQ:
    case ISD::SETEQ:  Opc = ARMCC::EQ; break;
    case ISD::SETOLT:
    case ISD::SETLT: Swap = true; [[fallthrough]];
    case ISD::SETOGT:
    case ISD::SETGT:  Opc = ARMCC::GT; break;
    case ISD::SETOLE:
    case ISD::SETLE:  Swap = true; [[fallthrough]];
    case ISD::SETOGE:
    case ISD::SETGE: Opc = ARMCC::GE; break;
    case ISD::SETUGE: Swap = true; [[fallthrough]];
    case ISD::SETULE: Invert = true; Opc = ARMCC::GT; break;
    case ISD::SETUGT: Swap = true; [[fallthrough]];
    case ISD::SETULT: Invert = true; Opc = ARMCC::GE; break;
    case ISD::SETUEQ: Invert = true; [[fallthrough]];
    case ISD::SETONE: {
      // Expand this to (OLT | OGT).
      SDValue TmpOp0 = DAG.getNode(ARMISD::VCMP, dl, CmpVT, Op1, Op0,
                                   DAG.getConstant(ARMCC::GT, dl, MVT::i32));
      SDValue TmpOp1 = DAG.getNode(ARMISD::VCMP, dl, CmpVT, Op0, Op1,
                                   DAG.getConstant(ARMCC::GT, dl, MVT::i32));
      SDValue Result = DAG.getNode(ISD::OR, dl, CmpVT, TmpOp0, TmpOp1);
      if (Invert)
        Result = DAG.getNOT(dl, Result, VT);
      return Result;
    }
    case ISD::SETUO: Invert = true; [[fallthrough]];
    case ISD::SETO: {
      // Expand this to (OLT | OGE).
      SDValue TmpOp0 = DAG.getNode(ARMISD::VCMP, dl, CmpVT, Op1, Op0,
                                   DAG.getConstant(ARMCC::GT, dl, MVT::i32));
      SDValue TmpOp1 = DAG.getNode(ARMISD::VCMP, dl, CmpVT, Op0, Op1,
                                   DAG.getConstant(ARMCC::GE, dl, MVT::i32));
      SDValue Result = DAG.getNode(ISD::OR, dl, CmpVT, TmpOp0, TmpOp1);
      if (Invert)
        Result = DAG.getNOT(dl, Result, VT);
      return Result;
    }
    }
  } else {
    // Integer comparisons.
    switch (SetCCOpcode) {
    default: llvm_unreachable("Illegal integer comparison");
    case ISD::SETNE:
      if (ST->hasMVEIntegerOps()) {
        Opc = ARMCC::NE; break;
      } else {
        Invert = true; [[fallthrough]];
      }
    case ISD::SETEQ:  Opc = ARMCC::EQ; break;
    case ISD::SETLT:  Swap = true; [[fallthrough]];
    case ISD::SETGT:  Opc = ARMCC::GT; break;
    case ISD::SETLE:  Swap = true; [[fallthrough]];
    case ISD::SETGE:  Opc = ARMCC::GE; break;
    case ISD::SETULT: Swap = true; [[fallthrough]];
    case ISD::SETUGT: Opc = ARMCC::HI; break;
    case ISD::SETULE: Swap = true; [[fallthrough]];
    case ISD::SETUGE: Opc = ARMCC::HS; break;
    }

    // Detect VTST (Vector Test Bits) = icmp ne (and (op0, op1), zero).
    if (ST->hasNEON() && Opc == ARMCC::EQ) {
      SDValue AndOp;
      if (ISD::isBuildVectorAllZeros(Op1.getNode()))
        AndOp = Op0;
      else if (ISD::isBuildVectorAllZeros(Op0.getNode()))
        AndOp = Op1;

      // Ignore bitconvert.
      if (AndOp.getNode() && AndOp.getOpcode() == ISD::BITCAST)
        AndOp = AndOp.getOperand(0);

      if (AndOp.getNode() && AndOp.getOpcode() == ISD::AND) {
        Op0 = DAG.getNode(ISD::BITCAST, dl, CmpVT, AndOp.getOperand(0));
        Op1 = DAG.getNode(ISD::BITCAST, dl, CmpVT, AndOp.getOperand(1));
        SDValue Result = DAG.getNode(ARMISD::VTST, dl, CmpVT, Op0, Op1);
        if (!Invert)
          Result = DAG.getNOT(dl, Result, VT);
        return Result;
      }
    }
  }

  if (Swap)
    std::swap(Op0, Op1);

  // If one of the operands is a constant vector zero, attempt to fold the
  // comparison to a specialized compare-against-zero form.
  if (ISD::isBuildVectorAllZeros(Op0.getNode()) &&
      (Opc == ARMCC::GE || Opc == ARMCC::GT || Opc == ARMCC::EQ ||
       Opc == ARMCC::NE)) {
    if (Opc == ARMCC::GE)
      Opc = ARMCC::LE;
    else if (Opc == ARMCC::GT)
      Opc = ARMCC::LT;
    std::swap(Op0, Op1);
  }

  SDValue Result;
  if (ISD::isBuildVectorAllZeros(Op1.getNode()) &&
      (Opc == ARMCC::GE || Opc == ARMCC::GT || Opc == ARMCC::LE ||
       Opc == ARMCC::LT || Opc == ARMCC::NE || Opc == ARMCC::EQ))
    Result = DAG.getNode(ARMISD::VCMPZ, dl, CmpVT, Op0,
                         DAG.getConstant(Opc, dl, MVT::i32));
  else
    Result = DAG.getNode(ARMISD::VCMP, dl, CmpVT, Op0, Op1,
                         DAG.getConstant(Opc, dl, MVT::i32));

  Result = DAG.getSExtOrTrunc(Result, dl, VT);

  if (Invert)
    Result = DAG.getNOT(dl, Result, VT);

  return Result;
}

static SDValue LowerSETCCCARRY(SDValue Op, SelectionDAG &DAG) {
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  SDValue Carry = Op.getOperand(2);
  SDValue Cond = Op.getOperand(3);
  SDLoc DL(Op);

  assert(LHS.getSimpleValueType().isInteger() && "SETCCCARRY is integer only.");

  // ARMISD::SUBE expects a carry not a borrow like ISD::USUBO_CARRY so we
  // have to invert the carry first.
  Carry = DAG.getNode(ISD::SUB, DL, MVT::i32,
                      DAG.getConstant(1, DL, MVT::i32), Carry);
  // This converts the boolean value carry into the carry flag.
  Carry = ConvertBooleanCarryToCarryFlag(Carry, DAG);

  SDVTList VTs = DAG.getVTList(LHS.getValueType(), MVT::i32);
  SDValue Cmp = DAG.getNode(ARMISD::SUBE, DL, VTs, LHS, RHS, Carry);

  SDValue FVal = DAG.getConstant(0, DL, MVT::i32);
  SDValue TVal = DAG.getConstant(1, DL, MVT::i32);
  SDValue ARMcc = DAG.getConstant(
      IntCCToARMCC(cast<CondCodeSDNode>(Cond)->get()), DL, MVT::i32);
  SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
  SDValue Chain = DAG.getCopyToReg(DAG.getEntryNode(), DL, ARM::CPSR,
                                   Cmp.getValue(1), SDValue());
  return DAG.getNode(ARMISD::CMOV, DL, Op.getValueType(), FVal, TVal, ARMcc,
                     CCR, Chain.getValue(1));
}

/// isVMOVModifiedImm - Check if the specified splat value corresponds to a
/// valid vector constant for a NEON or MVE instruction with a "modified
/// immediate" operand (e.g., VMOV).  If so, return the encoded value.
static SDValue isVMOVModifiedImm(uint64_t SplatBits, uint64_t SplatUndef,
                                 unsigned SplatBitSize, SelectionDAG &DAG,
                                 const SDLoc &dl, EVT &VT, EVT VectorVT,
                                 VMOVModImmType type) {
  unsigned OpCmode, Imm;
  bool is128Bits = VectorVT.is128BitVector();

  // SplatBitSize is set to the smallest size that splats the vector, so a
  // zero vector will always have SplatBitSize == 8.  However, NEON modified
  // immediate instructions others than VMOV do not support the 8-bit encoding
  // of a zero vector, and the default encoding of zero is supposed to be the
  // 32-bit version.
  if (SplatBits == 0)
    SplatBitSize = 32;

  switch (SplatBitSize) {
  case 8:
    if (type != VMOVModImm)
      return SDValue();
    // Any 1-byte value is OK.  Op=0, Cmode=1110.
    assert((SplatBits & ~0xff) == 0 && "one byte splat value is too big");
    OpCmode = 0xe;
    Imm = SplatBits;
    VT = is128Bits ? MVT::v16i8 : MVT::v8i8;
    break;

  case 16:
    // NEON's 16-bit VMOV supports splat values where only one byte is nonzero.
    VT = is128Bits ? MVT::v8i16 : MVT::v4i16;
    if ((SplatBits & ~0xff) == 0) {
      // Value = 0x00nn: Op=x, Cmode=100x.
      OpCmode = 0x8;
      Imm = SplatBits;
      break;
    }
    if ((SplatBits & ~0xff00) == 0) {
      // Value = 0xnn00: Op=x, Cmode=101x.
      OpCmode = 0xa;
      Imm = SplatBits >> 8;
      break;
    }
    return SDValue();

  case 32:
    // NEON's 32-bit VMOV supports splat values where:
    // * only one byte is nonzero, or
    // * the least significant byte is 0xff and the second byte is nonzero, or
    // * the least significant 2 bytes are 0xff and the third is nonzero.
    VT = is128Bits ? MVT::v4i32 : MVT::v2i32;
    if ((SplatBits & ~0xff) == 0) {
      // Value = 0x000000nn: Op=x, Cmode=000x.
      OpCmode = 0;
      Imm = SplatBits;
      break;
    }
    if ((SplatBits & ~0xff00) == 0) {
      // Value = 0x0000nn00: Op=x, Cmode=001x.
      OpCmode = 0x2;
      Imm = SplatBits >> 8;
      break;
    }
    if ((SplatBits & ~0xff0000) == 0) {
      // Value = 0x00nn0000: Op=x, Cmode=010x.
      OpCmode = 0x4;
      Imm = SplatBits >> 16;
      break;
    }
    if ((SplatBits & ~0xff000000) == 0) {
      // Value = 0xnn000000: Op=x, Cmode=011x.
      OpCmode = 0x6;
      Imm = SplatBits >> 24;
      break;
    }

    // cmode == 0b1100 and cmode == 0b1101 are not supported for VORR or VBIC
    if (type == OtherModImm) return SDValue();

    if ((SplatBits & ~0xffff) == 0 &&
        ((SplatBits | SplatUndef) & 0xff) == 0xff) {
      // Value = 0x0000nnff: Op=x, Cmode=1100.
      OpCmode = 0xc;
      Imm = SplatBits >> 8;
      break;
    }

    // cmode == 0b1101 is not supported for MVE VMVN
    if (type == MVEVMVNModImm)
      return SDValue();

    if ((SplatBits & ~0xffffff) == 0 &&
        ((SplatBits | SplatUndef) & 0xffff) == 0xffff) {
      // Value = 0x00nnffff: Op=x, Cmode=1101.
      OpCmode = 0xd;
      Imm = SplatBits >> 16;
      break;
    }

    // Note: there are a few 32-bit splat values (specifically: 00ffff00,
    // ff000000, ff0000ff, and ffff00ff) that are valid for VMOV.I64 but not
    // VMOV.I32.  A (very) minor optimization would be to replicate the value
    // and fall through here to test for a valid 64-bit splat.  But, then the
    // caller would also need to check and handle the change in size.
    return SDValue();

  case 64: {
    if (type != VMOVModImm)
      return SDValue();
    // NEON has a 64-bit VMOV splat where each byte is either 0 or 0xff.
    uint64_t BitMask = 0xff;
    unsigned ImmMask = 1;
    Imm = 0;
    for (int ByteNum = 0; ByteNum < 8; ++ByteNum) {
      if (((SplatBits | SplatUndef) & BitMask) == BitMask) {
        Imm |= ImmMask;
      } else if ((SplatBits & BitMask) != 0) {
        return SDValue();
      }
      BitMask <<= 8;
      ImmMask <<= 1;
    }

    if (DAG.getDataLayout().isBigEndian()) {
      // Reverse the order of elements within the vector.
      unsigned BytesPerElem = VectorVT.getScalarSizeInBits() / 8;
      unsigned Mask = (1 << BytesPerElem) - 1;
      unsigned NumElems = 8 / BytesPerElem;
      unsigned NewImm = 0;
      for (unsigned ElemNum = 0; ElemNum < NumElems; ++ElemNum) {
        unsigned Elem = ((Imm >> ElemNum * BytesPerElem) & Mask);
        NewImm |= Elem << (NumElems - ElemNum - 1) * BytesPerElem;
      }
      Imm = NewImm;
    }

    // Op=1, Cmode=1110.
    OpCmode = 0x1e;
    VT = is128Bits ? MVT::v2i64 : MVT::v1i64;
    break;
  }

  default:
    llvm_unreachable("unexpected size for isVMOVModifiedImm");
  }

  unsigned EncodedVal = ARM_AM::createVMOVModImm(OpCmode, Imm);
  return DAG.getTargetConstant(EncodedVal, dl, MVT::i32);
}

SDValue ARMTargetLowering::LowerConstantFP(SDValue Op, SelectionDAG &DAG,
                                           const ARMSubtarget *ST) const {
  EVT VT = Op.getValueType();
  bool IsDouble = (VT == MVT::f64);
  ConstantFPSDNode *CFP = cast<ConstantFPSDNode>(Op);
  const APFloat &FPVal = CFP->getValueAPF();

  // Prevent floating-point constants from using literal loads
  // when execute-only is enabled.
  if (ST->genExecuteOnly()) {
    // We shouldn't trigger this for v6m execute-only
    assert((!ST->isThumb1Only() || ST->hasV8MBaselineOps()) &&
           "Unexpected architecture");

    // If we can represent the constant as an immediate, don't lower it
    if (isFPImmLegal(FPVal, VT))
      return Op;
    // Otherwise, construct as integer, and move to float register
    APInt INTVal = FPVal.bitcastToAPInt();
    SDLoc DL(CFP);
    switch (VT.getSimpleVT().SimpleTy) {
      default:
        llvm_unreachable("Unknown floating point type!");
        break;
      case MVT::f64: {
        SDValue Lo = DAG.getConstant(INTVal.trunc(32), DL, MVT::i32);
        SDValue Hi = DAG.getConstant(INTVal.lshr(32).trunc(32), DL, MVT::i32);
        return DAG.getNode(ARMISD::VMOVDRR, DL, MVT::f64, Lo, Hi);
      }
      case MVT::f32:
          return DAG.getNode(ARMISD::VMOVSR, DL, VT,
              DAG.getConstant(INTVal, DL, MVT::i32));
    }
  }

  if (!ST->hasVFP3Base())
    return SDValue();

  // Use the default (constant pool) lowering for double constants when we have
  // an SP-only FPU
  if (IsDouble && !Subtarget->hasFP64())
    return SDValue();

  // Try splatting with a VMOV.f32...
  int ImmVal = IsDouble ? ARM_AM::getFP64Imm(FPVal) : ARM_AM::getFP32Imm(FPVal);

  if (ImmVal != -1) {
    if (IsDouble || !ST->useNEONForSinglePrecisionFP()) {
      // We have code in place to select a valid ConstantFP already, no need to
      // do any mangling.
      return Op;
    }

    // It's a float and we are trying to use NEON operations where
    // possible. Lower it to a splat followed by an extract.
    SDLoc DL(Op);
    SDValue NewVal = DAG.getTargetConstant(ImmVal, DL, MVT::i32);
    SDValue VecConstant = DAG.getNode(ARMISD::VMOVFPIMM, DL, MVT::v2f32,
                                      NewVal);
    return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::f32, VecConstant,
                       DAG.getConstant(0, DL, MVT::i32));
  }

  // The rest of our options are NEON only, make sure that's allowed before
  // proceeding..
  if (!ST->hasNEON() || (!IsDouble && !ST->useNEONForSinglePrecisionFP()))
    return SDValue();

  EVT VMovVT;
  uint64_t iVal = FPVal.bitcastToAPInt().getZExtValue();

  // It wouldn't really be worth bothering for doubles except for one very
  // important value, which does happen to match: 0.0. So make sure we don't do
  // anything stupid.
  if (IsDouble && (iVal & 0xffffffff) != (iVal >> 32))
    return SDValue();

  // Try a VMOV.i32 (FIXME: i8, i16, or i64 could work too).
  SDValue NewVal = isVMOVModifiedImm(iVal & 0xffffffffU, 0, 32, DAG, SDLoc(Op),
                                     VMovVT, VT, VMOVModImm);
  if (NewVal != SDValue()) {
    SDLoc DL(Op);
    SDValue VecConstant = DAG.getNode(ARMISD::VMOVIMM, DL, VMovVT,
                                      NewVal);
    if (IsDouble)
      return DAG.getNode(ISD::BITCAST, DL, MVT::f64, VecConstant);

    // It's a float: cast and extract a vector element.
    SDValue VecFConstant = DAG.getNode(ISD::BITCAST, DL, MVT::v2f32,
                                       VecConstant);
    return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::f32, VecFConstant,
                       DAG.getConstant(0, DL, MVT::i32));
  }

  // Finally, try a VMVN.i32
  NewVal = isVMOVModifiedImm(~iVal & 0xffffffffU, 0, 32, DAG, SDLoc(Op), VMovVT,
                             VT, VMVNModImm);
  if (NewVal != SDValue()) {
    SDLoc DL(Op);
    SDValue VecConstant = DAG.getNode(ARMISD::VMVNIMM, DL, VMovVT, NewVal);

    if (IsDouble)
      return DAG.getNode(ISD::BITCAST, DL, MVT::f64, VecConstant);

    // It's a float: cast and extract a vector element.
    SDValue VecFConstant = DAG.getNode(ISD::BITCAST, DL, MVT::v2f32,
                                       VecConstant);
    return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::f32, VecFConstant,
                       DAG.getConstant(0, DL, MVT::i32));
  }

  return SDValue();
}

// check if an VEXT instruction can handle the shuffle mask when the
// vector sources of the shuffle are the same.
static bool isSingletonVEXTMask(ArrayRef<int> M, EVT VT, unsigned &Imm) {
  unsigned NumElts = VT.getVectorNumElements();

  // Assume that the first shuffle index is not UNDEF.  Fail if it is.
  if (M[0] < 0)
    return false;

  Imm = M[0];

  // If this is a VEXT shuffle, the immediate value is the index of the first
  // element.  The other shuffle indices must be the successive elements after
  // the first one.
  unsigned ExpectedElt = Imm;
  for (unsigned i = 1; i < NumElts; ++i) {
    // Increment the expected index.  If it wraps around, just follow it
    // back to index zero and keep going.
    ++ExpectedElt;
    if (ExpectedElt == NumElts)
      ExpectedElt = 0;

    if (M[i] < 0) continue; // ignore UNDEF indices
    if (ExpectedElt != static_cast<unsigned>(M[i]))
      return false;
  }

  return true;
}

static bool isVEXTMask(ArrayRef<int> M, EVT VT,
                       bool &ReverseVEXT, unsigned &Imm) {
  unsigned NumElts = VT.getVectorNumElements();
  ReverseVEXT = false;

  // Assume that the first shuffle index is not UNDEF.  Fail if it is.
  if (M[0] < 0)
    return false;

  Imm = M[0];

  // If this is a VEXT shuffle, the immediate value is the index of the first
  // element.  The other shuffle indices must be the successive elements after
  // the first one.
  unsigned ExpectedElt = Imm;
  for (unsigned i = 1; i < NumElts; ++i) {
    // Increment the expected index.  If it wraps around, it may still be
    // a VEXT but the source vectors must be swapped.
    ExpectedElt += 1;
    if (ExpectedElt == NumElts * 2) {
      ExpectedElt = 0;
      ReverseVEXT = true;
    }

    if (M[i] < 0) continue; // ignore UNDEF indices
    if (ExpectedElt != static_cast<unsigned>(M[i]))
      return false;
  }

  // Adjust the index value if the source operands will be swapped.
  if (ReverseVEXT)
    Imm -= NumElts;

  return true;
}

static bool isVTBLMask(ArrayRef<int> M, EVT VT) {
  // We can handle <8 x i8> vector shuffles. If the index in the mask is out of
  // range, then 0 is placed into the resulting vector. So pretty much any mask
  // of 8 elements can work here.
  return VT == MVT::v8i8 && M.size() == 8;
}

static unsigned SelectPairHalf(unsigned Elements, ArrayRef<int> Mask,
                               unsigned Index) {
  if (Mask.size() == Elements * 2)
    return Index / Elements;
  return Mask[Index] == 0 ? 0 : 1;
}

// Checks whether the shuffle mask represents a vector transpose (VTRN) by
// checking that pairs of elements in the shuffle mask represent the same index
// in each vector, incrementing the expected index by 2 at each step.
// e.g. For v1,v2 of type v4i32 a valid shuffle mask is: [0, 4, 2, 6]
//  v1={a,b,c,d} => x=shufflevector v1, v2 shufflemask => x={a,e,c,g}
//  v2={e,f,g,h}
// WhichResult gives the offset for each element in the mask based on which
// of the two results it belongs to.
//
// The transpose can be represented either as:
// result1 = shufflevector v1, v2, result1_shuffle_mask
// result2 = shufflevector v1, v2, result2_shuffle_mask
// where v1/v2 and the shuffle masks have the same number of elements
// (here WhichResult (see below) indicates which result is being checked)
//
// or as:
// results = shufflevector v1, v2, shuffle_mask
// where both results are returned in one vector and the shuffle mask has twice
// as many elements as v1/v2 (here WhichResult will always be 0 if true) here we
// want to check the low half and high half of the shuffle mask as if it were
// the other case
static bool isVTRNMask(ArrayRef<int> M, EVT VT, unsigned &WhichResult) {
  unsigned EltSz = VT.getScalarSizeInBits();
  if (EltSz == 64)
    return false;

  unsigned NumElts = VT.getVectorNumElements();
  if (M.size() != NumElts && M.size() != NumElts*2)
    return false;

  // If the mask is twice as long as the input vector then we need to check the
  // upper and lower parts of the mask with a matching value for WhichResult
  // FIXME: A mask with only even values will be rejected in case the first
  // element is undefined, e.g. [-1, 4, 2, 6] will be rejected, because only
  // M[0] is used to determine WhichResult
  for (unsigned i = 0; i < M.size(); i += NumElts) {
    WhichResult = SelectPairHalf(NumElts, M, i);
    for (unsigned j = 0; j < NumElts; j += 2) {
      if ((M[i+j] >= 0 && (unsigned) M[i+j] != j + WhichResult) ||
          (M[i+j+1] >= 0 && (unsigned) M[i+j+1] != j + NumElts + WhichResult))
        return false;
    }
  }

  if (M.size() == NumElts*2)
    WhichResult = 0;

  return true;
}

/// isVTRN_v_undef_Mask - Special case of isVTRNMask for canonical form of
/// "vector_shuffle v, v", i.e., "vector_shuffle v, undef".
/// Mask is e.g., <0, 0, 2, 2> instead of <0, 4, 2, 6>.
static bool isVTRN_v_undef_Mask(ArrayRef<int> M, EVT VT, unsigned &WhichResult){
  unsigned EltSz = VT.getScalarSizeInBits();
  if (EltSz == 64)
    return false;

  unsigned NumElts = VT.getVectorNumElements();
  if (M.size() != NumElts && M.size() != NumElts*2)
    return false;

  for (unsigned i = 0; i < M.size(); i += NumElts) {
    WhichResult = SelectPairHalf(NumElts, M, i);
    for (unsigned j = 0; j < NumElts; j += 2) {
      if ((M[i+j] >= 0 && (unsigned) M[i+j] != j + WhichResult) ||
          (M[i+j+1] >= 0 && (unsigned) M[i+j+1] != j + WhichResult))
        return false;
    }
  }

  if (M.size() == NumElts*2)
    WhichResult = 0;

  return true;
}

// Checks whether the shuffle mask represents a vector unzip (VUZP) by checking
// that the mask elements are either all even and in steps of size 2 or all odd
// and in steps of size 2.
// e.g. For v1,v2 of type v4i32 a valid shuffle mask is: [0, 2, 4, 6]
//  v1={a,b,c,d} => x=shufflevector v1, v2 shufflemask => x={a,c,e,g}
//  v2={e,f,g,h}
// Requires similar checks to that of isVTRNMask with
// respect the how results are returned.
static bool isVUZPMask(ArrayRef<int> M, EVT VT, unsigned &WhichResult) {
  unsigned EltSz = VT.getScalarSizeInBits();
  if (EltSz == 64)
    return false;

  unsigned NumElts = VT.getVectorNumElements();
  if (M.size() != NumElts && M.size() != NumElts*2)
    return false;

  for (unsigned i = 0; i < M.size(); i += NumElts) {
    WhichResult = SelectPairHalf(NumElts, M, i);
    for (unsigned j = 0; j < NumElts; ++j) {
      if (M[i+j] >= 0 && (unsigned) M[i+j] != 2 * j + WhichResult)
        return false;
    }
  }

  if (M.size() == NumElts*2)
    WhichResult = 0;

  // VUZP.32 for 64-bit vectors is a pseudo-instruction alias for VTRN.32.
  if (VT.is64BitVector() && EltSz == 32)
    return false;

  return true;
}

/// isVUZP_v_undef_Mask - Special case of isVUZPMask for canonical form of
/// "vector_shuffle v, v", i.e., "vector_shuffle v, undef".
/// Mask is e.g., <0, 2, 0, 2> instead of <0, 2, 4, 6>,
static bool isVUZP_v_undef_Mask(ArrayRef<int> M, EVT VT, unsigned &WhichResult){
  unsigned EltSz = VT.getScalarSizeInBits();
  if (EltSz == 64)
    return false;

  unsigned NumElts = VT.getVectorNumElements();
  if (M.size() != NumElts && M.size() != NumElts*2)
    return false;

  unsigned Half = NumElts / 2;
  for (unsigned i = 0; i < M.size(); i += NumElts) {
    WhichResult = SelectPairHalf(NumElts, M, i);
    for (unsigned j = 0; j < NumElts; j += Half) {
      unsigned Idx = WhichResult;
      for (unsigned k = 0; k < Half; ++k) {
        int MIdx = M[i + j + k];
        if (MIdx >= 0 && (unsigned) MIdx != Idx)
          return false;
        Idx += 2;
      }
    }
  }

  if (M.size() == NumElts*2)
    WhichResult = 0;

  // VUZP.32 for 64-bit vectors is a pseudo-instruction alias for VTRN.32.
  if (VT.is64BitVector() && EltSz == 32)
    return false;

  return true;
}

// Checks whether the shuffle mask represents a vector zip (VZIP) by checking
// that pairs of elements of the shufflemask represent the same index in each
// vector incrementing sequentially through the vectors.
// e.g. For v1,v2 of type v4i32 a valid shuffle mask is: [0, 4, 1, 5]
//  v1={a,b,c,d} => x=shufflevector v1, v2 shufflemask => x={a,e,b,f}
//  v2={e,f,g,h}
// Requires similar checks to that of isVTRNMask with respect the how results
// are returned.
static bool isVZIPMask(ArrayRef<int> M, EVT VT, unsigned &WhichResult) {
  unsigned EltSz = VT.getScalarSizeInBits();
  if (EltSz == 64)
    return false;

  unsigned NumElts = VT.getVectorNumElements();
  if (M.size() != NumElts && M.size() != NumElts*2)
    return false;

  for (unsigned i = 0; i < M.size(); i += NumElts) {
    WhichResult = SelectPairHalf(NumElts, M, i);
    unsigned Idx = WhichResult * NumElts / 2;
    for (unsigned j = 0; j < NumElts; j += 2) {
      if ((M[i+j] >= 0 && (unsigned) M[i+j] != Idx) ||
          (M[i+j+1] >= 0 && (unsigned) M[i+j+1] != Idx + NumElts))
        return false;
      Idx += 1;
    }
  }

  if (M.size() == NumElts*2)
    WhichResult = 0;

  // VZIP.32 for 64-bit vectors is a pseudo-instruction alias for VTRN.32.
  if (VT.is64BitVector() && EltSz == 32)
    return false;

  return true;
}

/// isVZIP_v_undef_Mask - Special case of isVZIPMask for canonical form of
/// "vector_shuffle v, v", i.e., "vector_shuffle v, undef".
/// Mask is e.g., <0, 0, 1, 1> instead of <0, 4, 1, 5>.
static bool isVZIP_v_undef_Mask(ArrayRef<int> M, EVT VT, unsigned &WhichResult){
  unsigned EltSz = VT.getScalarSizeInBits();
  if (EltSz == 64)
    return false;

  unsigned NumElts = VT.getVectorNumElements();
  if (M.size() != NumElts && M.size() != NumElts*2)
    return false;

  for (unsigned i = 0; i < M.size(); i += NumElts) {
    WhichResult = SelectPairHalf(NumElts, M, i);
    unsigned Idx = WhichResult * NumElts / 2;
    for (unsigned j = 0; j < NumElts; j += 2) {
      if ((M[i+j] >= 0 && (unsigned) M[i+j] != Idx) ||
          (M[i+j+1] >= 0 && (unsigned) M[i+j+1] != Idx))
        return false;
      Idx += 1;
    }
  }

  if (M.size() == NumElts*2)
    WhichResult = 0;

  // VZIP.32 for 64-bit vectors is a pseudo-instruction alias for VTRN.32.
  if (VT.is64BitVector() && EltSz == 32)
    return false;

  return true;
}

/// Check if \p ShuffleMask is a NEON two-result shuffle (VZIP, VUZP, VTRN),
/// and return the corresponding ARMISD opcode if it is, or 0 if it isn't.
static unsigned isNEONTwoResultShuffleMask(ArrayRef<int> ShuffleMask, EVT VT,
                                           unsigned &WhichResult,
                                           bool &isV_UNDEF) {
  isV_UNDEF = false;
  if (isVTRNMask(ShuffleMask, VT, WhichResult))
    return ARMISD::VTRN;
  if (isVUZPMask(ShuffleMask, VT, WhichResult))
    return ARMISD::VUZP;
  if (isVZIPMask(ShuffleMask, VT, WhichResult))
    return ARMISD::VZIP;

  isV_UNDEF = true;
  if (isVTRN_v_undef_Mask(ShuffleMask, VT, WhichResult))
    return ARMISD::VTRN;
  if (isVUZP_v_undef_Mask(ShuffleMask, VT, WhichResult))
    return ARMISD::VUZP;
  if (isVZIP_v_undef_Mask(ShuffleMask, VT, WhichResult))
    return ARMISD::VZIP;

  return 0;
}

/// \return true if this is a reverse operation on an vector.
static bool isReverseMask(ArrayRef<int> M, EVT VT) {
  unsigned NumElts = VT.getVectorNumElements();
  // Make sure the mask has the right size.
  if (NumElts != M.size())
      return false;

  // Look for <15, ..., 3, -1, 1, 0>.
  for (unsigned i = 0; i != NumElts; ++i)
    if (M[i] >= 0 && M[i] != (int) (NumElts - 1 - i))
      return false;

  return true;
}

static bool isTruncMask(ArrayRef<int> M, EVT VT, bool Top, bool SingleSource) {
  unsigned NumElts = VT.getVectorNumElements();
  // Make sure the mask has the right size.
  if (NumElts != M.size() || (VT != MVT::v8i16 && VT != MVT::v16i8))
    return false;

  // Half-width truncation patterns (e.g. v4i32 -> v8i16):
  // !Top &&  SingleSource: <0, 2, 4, 6, 0, 2, 4, 6>
  // !Top && !SingleSource: <0, 2, 4, 6, 8, 10, 12, 14>
  //  Top &&  SingleSource: <1, 3, 5, 7, 1, 3, 5, 7>
  //  Top && !SingleSource: <1, 3, 5, 7, 9, 11, 13, 15>
  int Ofs = Top ? 1 : 0;
  int Upper = SingleSource ? 0 : NumElts;
  for (int i = 0, e = NumElts / 2; i != e; ++i) {
    if (M[i] >= 0 && M[i] != (i * 2) + Ofs)
      return false;
    if (M[i + e] >= 0 && M[i + e] != (i * 2) + Ofs + Upper)
      return false;
  }
  return true;
}

static bool isVMOVNMask(ArrayRef<int> M, EVT VT, bool Top, bool SingleSource) {
  unsigned NumElts = VT.getVectorNumElements();
  // Make sure the mask has the right size.
  if (NumElts != M.size() || (VT != MVT::v8i16 && VT != MVT::v16i8))
    return false;

  // If Top
  //   Look for <0, N, 2, N+2, 4, N+4, ..>.
  //   This inserts Input2 into Input1
  // else if not Top
  //   Look for <0, N+1, 2, N+3, 4, N+5, ..>
  //   This inserts Input1 into Input2
  unsigned Offset = Top ? 0 : 1;
  unsigned N = SingleSource ? 0 : NumElts;
  for (unsigned i = 0; i < NumElts; i += 2) {
    if (M[i] >= 0 && M[i] != (int)i)
      return false;
    if (M[i + 1] >= 0 && M[i + 1] != (int)(N + i + Offset))
      return false;
  }

  return true;
}

static bool isVMOVNTruncMask(ArrayRef<int> M, EVT ToVT, bool rev) {
  unsigned NumElts = ToVT.getVectorNumElements();
  if (NumElts != M.size())
    return false;

  // Test if the Trunc can be convertable to a VMOVN with this shuffle. We are
  // looking for patterns of:
  // !rev: 0 N/2 1 N/2+1 2 N/2+2 ...
  //  rev: N/2 0 N/2+1 1 N/2+2 2 ...

  unsigned Off0 = rev ? NumElts / 2 : 0;
  unsigned Off1 = rev ? 0 : NumElts / 2;
  for (unsigned i = 0; i < NumElts; i += 2) {
    if (M[i] >= 0 && M[i] != (int)(Off0 + i / 2))
      return false;
    if (M[i + 1] >= 0 && M[i + 1] != (int)(Off1 + i / 2))
      return false;
  }

  return true;
}

// Reconstruct an MVE VCVT from a BuildVector of scalar fptrunc, all extracted
// from a pair of inputs. For example:
// BUILDVECTOR(FP_ROUND(EXTRACT_ELT(X, 0),
//             FP_ROUND(EXTRACT_ELT(Y, 0),
//             FP_ROUND(EXTRACT_ELT(X, 1),
//             FP_ROUND(EXTRACT_ELT(Y, 1), ...)
static SDValue LowerBuildVectorOfFPTrunc(SDValue BV, SelectionDAG &DAG,
                                         const ARMSubtarget *ST) {
  assert(BV.getOpcode() == ISD::BUILD_VECTOR && "Unknown opcode!");
  if (!ST->hasMVEFloatOps())
    return SDValue();

  SDLoc dl(BV);
  EVT VT = BV.getValueType();
  if (VT != MVT::v8f16)
    return SDValue();

  // We are looking for a buildvector of fptrunc elements, where all the
  // elements are interleavingly extracted from two sources. Check the first two
  // items are valid enough and extract some info from them (they are checked
  // properly in the loop below).
  if (BV.getOperand(0).getOpcode() != ISD::FP_ROUND ||
      BV.getOperand(0).getOperand(0).getOpcode() != ISD::EXTRACT_VECTOR_ELT ||
      BV.getOperand(0).getOperand(0).getConstantOperandVal(1) != 0)
    return SDValue();
  if (BV.getOperand(1).getOpcode() != ISD::FP_ROUND ||
      BV.getOperand(1).getOperand(0).getOpcode() != ISD::EXTRACT_VECTOR_ELT ||
      BV.getOperand(1).getOperand(0).getConstantOperandVal(1) != 0)
    return SDValue();
  SDValue Op0 = BV.getOperand(0).getOperand(0).getOperand(0);
  SDValue Op1 = BV.getOperand(1).getOperand(0).getOperand(0);
  if (Op0.getValueType() != MVT::v4f32 || Op1.getValueType() != MVT::v4f32)
    return SDValue();

  // Check all the values in the BuildVector line up with our expectations.
  for (unsigned i = 1; i < 4; i++) {
    auto Check = [](SDValue Trunc, SDValue Op, unsigned Idx) {
      return Trunc.getOpcode() == ISD::FP_ROUND &&
             Trunc.getOperand(0).getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
             Trunc.getOperand(0).getOperand(0) == Op &&
             Trunc.getOperand(0).getConstantOperandVal(1) == Idx;
    };
    if (!Check(BV.getOperand(i * 2 + 0), Op0, i))
      return SDValue();
    if (!Check(BV.getOperand(i * 2 + 1), Op1, i))
      return SDValue();
  }

  SDValue N1 = DAG.getNode(ARMISD::VCVTN, dl, VT, DAG.getUNDEF(VT), Op0,
                           DAG.getConstant(0, dl, MVT::i32));
  return DAG.getNode(ARMISD::VCVTN, dl, VT, N1, Op1,
                     DAG.getConstant(1, dl, MVT::i32));
}

// Reconstruct an MVE VCVT from a BuildVector of scalar fpext, all extracted
// from a single input on alternating lanes. For example:
// BUILDVECTOR(FP_ROUND(EXTRACT_ELT(X, 0),
//             FP_ROUND(EXTRACT_ELT(X, 2),
//             FP_ROUND(EXTRACT_ELT(X, 4), ...)
static SDValue LowerBuildVectorOfFPExt(SDValue BV, SelectionDAG &DAG,
                                       const ARMSubtarget *ST) {
  assert(BV.getOpcode() == ISD::BUILD_VECTOR && "Unknown opcode!");
  if (!ST->hasMVEFloatOps())
    return SDValue();

  SDLoc dl(BV);
  EVT VT = BV.getValueType();
  if (VT != MVT::v4f32)
    return SDValue();

  // We are looking for a buildvector of fptext elements, where all the
  // elements are alternating lanes from a single source. For example <0,2,4,6>
  // or <1,3,5,7>. Check the first two items are valid enough and extract some
  // info from them (they are checked properly in the loop below).
  if (BV.getOperand(0).getOpcode() != ISD::FP_EXTEND ||
      BV.getOperand(0).getOperand(0).getOpcode() != ISD::EXTRACT_VECTOR_ELT)
    return SDValue();
  SDValue Op0 = BV.getOperand(0).getOperand(0).getOperand(0);
  int Offset = BV.getOperand(0).getOperand(0).getConstantOperandVal(1);
  if (Op0.getValueType() != MVT::v8f16 || (Offset != 0 && Offset != 1))
    return SDValue();

  // Check all the values in the BuildVector line up with our expectations.
  for (unsigned i = 1; i < 4; i++) {
    auto Check = [](SDValue Trunc, SDValue Op, unsigned Idx) {
      return Trunc.getOpcode() == ISD::FP_EXTEND &&
             Trunc.getOperand(0).getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
             Trunc.getOperand(0).getOperand(0) == Op &&
             Trunc.getOperand(0).getConstantOperandVal(1) == Idx;
    };
    if (!Check(BV.getOperand(i), Op0, 2 * i + Offset))
      return SDValue();
  }

  return DAG.getNode(ARMISD::VCVTL, dl, VT, Op0,
                     DAG.getConstant(Offset, dl, MVT::i32));
}

// If N is an integer constant that can be moved into a register in one
// instruction, return an SDValue of such a constant (will become a MOV
// instruction).  Otherwise return null.
static SDValue IsSingleInstrConstant(SDValue N, SelectionDAG &DAG,
                                     const ARMSubtarget *ST, const SDLoc &dl) {
  uint64_t Val;
  if (!isa<ConstantSDNode>(N))
    return SDValue();
  Val = N->getAsZExtVal();

  if (ST->isThumb1Only()) {
    if (Val <= 255 || ~Val <= 255)
      return DAG.getConstant(Val, dl, MVT::i32);
  } else {
    if (ARM_AM::getSOImmVal(Val) != -1 || ARM_AM::getSOImmVal(~Val) != -1)
      return DAG.getConstant(Val, dl, MVT::i32);
  }
  return SDValue();
}

static SDValue LowerBUILD_VECTOR_i1(SDValue Op, SelectionDAG &DAG,
                                    const ARMSubtarget *ST) {
  SDLoc dl(Op);
  EVT VT = Op.getValueType();

  assert(ST->hasMVEIntegerOps() && "LowerBUILD_VECTOR_i1 called without MVE!");

  unsigned NumElts = VT.getVectorNumElements();
  unsigned BoolMask;
  unsigned BitsPerBool;
  if (NumElts == 2) {
    BitsPerBool = 8;
    BoolMask = 0xff;
  } else if (NumElts == 4) {
    BitsPerBool = 4;
    BoolMask = 0xf;
  } else if (NumElts == 8) {
    BitsPerBool = 2;
    BoolMask = 0x3;
  } else if (NumElts == 16) {
    BitsPerBool = 1;
    BoolMask = 0x1;
  } else
    return SDValue();

  // If this is a single value copied into all lanes (a splat), we can just sign
  // extend that single value
  SDValue FirstOp = Op.getOperand(0);
  if (!isa<ConstantSDNode>(FirstOp) &&
      llvm::all_of(llvm::drop_begin(Op->ops()), [&FirstOp](const SDUse &U) {
        return U.get().isUndef() || U.get() == FirstOp;
      })) {
    SDValue Ext = DAG.getNode(ISD::SIGN_EXTEND_INREG, dl, MVT::i32, FirstOp,
                              DAG.getValueType(MVT::i1));
    return DAG.getNode(ARMISD::PREDICATE_CAST, dl, Op.getValueType(), Ext);
  }

  // First create base with bits set where known
  unsigned Bits32 = 0;
  for (unsigned i = 0; i < NumElts; ++i) {
    SDValue V = Op.getOperand(i);
    if (!isa<ConstantSDNode>(V) && !V.isUndef())
      continue;
    bool BitSet = V.isUndef() ? false : V->getAsZExtVal();
    if (BitSet)
      Bits32 |= BoolMask << (i * BitsPerBool);
  }

  // Add in unknown nodes
  SDValue Base = DAG.getNode(ARMISD::PREDICATE_CAST, dl, VT,
                             DAG.getConstant(Bits32, dl, MVT::i32));
  for (unsigned i = 0; i < NumElts; ++i) {
    SDValue V = Op.getOperand(i);
    if (isa<ConstantSDNode>(V) || V.isUndef())
      continue;
    Base = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, VT, Base, V,
                       DAG.getConstant(i, dl, MVT::i32));
  }

  return Base;
}

static SDValue LowerBUILD_VECTORToVIDUP(SDValue Op, SelectionDAG &DAG,
                                        const ARMSubtarget *ST) {
  if (!ST->hasMVEIntegerOps())
    return SDValue();

  // We are looking for a buildvector where each element is Op[0] + i*N
  EVT VT = Op.getValueType();
  SDValue Op0 = Op.getOperand(0);
  unsigned NumElts = VT.getVectorNumElements();

  // Get the increment value from operand 1
  SDValue Op1 = Op.getOperand(1);
  if (Op1.getOpcode() != ISD::ADD || Op1.getOperand(0) != Op0 ||
      !isa<ConstantSDNode>(Op1.getOperand(1)))
    return SDValue();
  unsigned N = Op1.getConstantOperandVal(1);
  if (N != 1 && N != 2 && N != 4 && N != 8)
    return SDValue();

  // Check that each other operand matches
  for (unsigned I = 2; I < NumElts; I++) {
    SDValue OpI = Op.getOperand(I);
    if (OpI.getOpcode() != ISD::ADD || OpI.getOperand(0) != Op0 ||
        !isa<ConstantSDNode>(OpI.getOperand(1)) ||
        OpI.getConstantOperandVal(1) != I * N)
      return SDValue();
  }

  SDLoc DL(Op);
  return DAG.getNode(ARMISD::VIDUP, DL, DAG.getVTList(VT, MVT::i32), Op0,
                     DAG.getConstant(N, DL, MVT::i32));
}

// Returns true if the operation N can be treated as qr instruction variant at
// operand Op.
static bool IsQRMVEInstruction(const SDNode *N, const SDNode *Op) {
  switch (N->getOpcode()) {
  case ISD::ADD:
  case ISD::MUL:
  case ISD::SADDSAT:
  case ISD::UADDSAT:
    return true;
  case ISD::SUB:
  case ISD::SSUBSAT:
  case ISD::USUBSAT:
    return N->getOperand(1).getNode() == Op;
  case ISD::INTRINSIC_WO_CHAIN:
    switch (N->getConstantOperandVal(0)) {
    case Intrinsic::arm_mve_add_predicated:
    case Intrinsic::arm_mve_mul_predicated:
    case Intrinsic::arm_mve_qadd_predicated:
    case Intrinsic::arm_mve_vhadd:
    case Intrinsic::arm_mve_hadd_predicated:
    case Intrinsic::arm_mve_vqdmulh:
    case Intrinsic::arm_mve_qdmulh_predicated:
    case Intrinsic::arm_mve_vqrdmulh:
    case Intrinsic::arm_mve_qrdmulh_predicated:
    case Intrinsic::arm_mve_vqdmull:
    case Intrinsic::arm_mve_vqdmull_predicated:
      return true;
    case Intrinsic::arm_mve_sub_predicated:
    case Intrinsic::arm_mve_qsub_predicated:
    case Intrinsic::arm_mve_vhsub:
    case Intrinsic::arm_mve_hsub_predicated:
      return N->getOperand(2).getNode() == Op;
    default:
      return false;
    }
  default:
    return false;
  }
}

// If this is a case we can't handle, return null and let the default
// expansion code take care of it.
SDValue ARMTargetLowering::LowerBUILD_VECTOR(SDValue Op, SelectionDAG &DAG,
                                             const ARMSubtarget *ST) const {
  BuildVectorSDNode *BVN = cast<BuildVectorSDNode>(Op.getNode());
  SDLoc dl(Op);
  EVT VT = Op.getValueType();

  if (ST->hasMVEIntegerOps() && VT.getScalarSizeInBits() == 1)
    return LowerBUILD_VECTOR_i1(Op, DAG, ST);

  if (SDValue R = LowerBUILD_VECTORToVIDUP(Op, DAG, ST))
    return R;

  APInt SplatBits, SplatUndef;
  unsigned SplatBitSize;
  bool HasAnyUndefs;
  if (BVN->isConstantSplat(SplatBits, SplatUndef, SplatBitSize, HasAnyUndefs)) {
    if (SplatUndef.isAllOnes())
      return DAG.getUNDEF(VT);

    // If all the users of this constant splat are qr instruction variants,
    // generate a vdup of the constant.
    if (ST->hasMVEIntegerOps() && VT.getScalarSizeInBits() == SplatBitSize &&
        (SplatBitSize == 8 || SplatBitSize == 16 || SplatBitSize == 32) &&
        all_of(BVN->uses(),
               [BVN](const SDNode *U) { return IsQRMVEInstruction(U, BVN); })) {
      EVT DupVT = SplatBitSize == 32   ? MVT::v4i32
                  : SplatBitSize == 16 ? MVT::v8i16
                                       : MVT::v16i8;
      SDValue Const = DAG.getConstant(SplatBits.getZExtValue(), dl, MVT::i32);
      SDValue VDup = DAG.getNode(ARMISD::VDUP, dl, DupVT, Const);
      return DAG.getNode(ARMISD::VECTOR_REG_CAST, dl, VT, VDup);
    }

    if ((ST->hasNEON() && SplatBitSize <= 64) ||
        (ST->hasMVEIntegerOps() && SplatBitSize <= 64)) {
      // Check if an immediate VMOV works.
      EVT VmovVT;
      SDValue Val =
          isVMOVModifiedImm(SplatBits.getZExtValue(), SplatUndef.getZExtValue(),
                            SplatBitSize, DAG, dl, VmovVT, VT, VMOVModImm);

      if (Val.getNode()) {
        SDValue Vmov = DAG.getNode(ARMISD::VMOVIMM, dl, VmovVT, Val);
        return DAG.getNode(ISD::BITCAST, dl, VT, Vmov);
      }

      // Try an immediate VMVN.
      uint64_t NegatedImm = (~SplatBits).getZExtValue();
      Val = isVMOVModifiedImm(
          NegatedImm, SplatUndef.getZExtValue(), SplatBitSize, DAG, dl, VmovVT,
          VT, ST->hasMVEIntegerOps() ? MVEVMVNModImm : VMVNModImm);
      if (Val.getNode()) {
        SDValue Vmov = DAG.getNode(ARMISD::VMVNIMM, dl, VmovVT, Val);
        return DAG.getNode(ISD::BITCAST, dl, VT, Vmov);
      }

      // Use vmov.f32 to materialize other v2f32 and v4f32 splats.
      if ((VT == MVT::v2f32 || VT == MVT::v4f32) && SplatBitSize == 32) {
        int ImmVal = ARM_AM::getFP32Imm(SplatBits);
        if (ImmVal != -1) {
          SDValue Val = DAG.getTargetConstant(ImmVal, dl, MVT::i32);
          return DAG.getNode(ARMISD::VMOVFPIMM, dl, VT, Val);
        }
      }

      // If we are under MVE, generate a VDUP(constant), bitcast to the original
      // type.
      if (ST->hasMVEIntegerOps() &&
          (SplatBitSize == 8 || SplatBitSize == 16 || SplatBitSize == 32)) {
        EVT DupVT = SplatBitSize == 32   ? MVT::v4i32
                    : SplatBitSize == 16 ? MVT::v8i16
                                         : MVT::v16i8;
        SDValue Const = DAG.getConstant(SplatBits.getZExtValue(), dl, MVT::i32);
        SDValue VDup = DAG.getNode(ARMISD::VDUP, dl, DupVT, Const);
        return DAG.getNode(ARMISD::VECTOR_REG_CAST, dl, VT, VDup);
      }
    }
  }

  // Scan through the operands to see if only one value is used.
  //
  // As an optimisation, even if more than one value is used it may be more
  // profitable to splat with one value then change some lanes.
  //
  // Heuristically we decide to do this if the vector has a "dominant" value,
  // defined as splatted to more than half of the lanes.
  unsigned NumElts = VT.getVectorNumElements();
  bool isOnlyLowElement = true;
  bool usesOnlyOneValue = true;
  bool hasDominantValue = false;
  bool isConstant = true;

  // Map of the number of times a particular SDValue appears in the
  // element list.
  DenseMap<SDValue, unsigned> ValueCounts;
  SDValue Value;
  for (unsigned i = 0; i < NumElts; ++i) {
    SDValue V = Op.getOperand(i);
    if (V.isUndef())
      continue;
    if (i > 0)
      isOnlyLowElement = false;
    if (!isa<ConstantFPSDNode>(V) && !isa<ConstantSDNode>(V))
      isConstant = false;

    ValueCounts.insert(std::make_pair(V, 0));
    unsigned &Count = ValueCounts[V];

    // Is this value dominant? (takes up more than half of the lanes)
    if (++Count > (NumElts / 2)) {
      hasDominantValue = true;
      Value = V;
    }
  }
  if (ValueCounts.size() != 1)
    usesOnlyOneValue = false;
  if (!Value.getNode() && !ValueCounts.empty())
    Value = ValueCounts.begin()->first;

  if (ValueCounts.empty())
    return DAG.getUNDEF(VT);

  // Loads are better lowered with insert_vector_elt/ARMISD::BUILD_VECTOR.
  // Keep going if we are hitting this case.
  if (isOnlyLowElement && !ISD::isNormalLoad(Value.getNode()))
    return DAG.getNode(ISD::SCALAR_TO_VECTOR, dl, VT, Value);

  unsigned EltSize = VT.getScalarSizeInBits();

  // Use VDUP for non-constant splats.  For f32 constant splats, reduce to
  // i32 and try again.
  if (hasDominantValue && EltSize <= 32) {
    if (!isConstant) {
      SDValue N;

      // If we are VDUPing a value that comes directly from a vector, that will
      // cause an unnecessary move to and from a GPR, where instead we could
      // just use VDUPLANE. We can only do this if the lane being extracted
      // is at a constant index, as the VDUP from lane instructions only have
      // constant-index forms.
      ConstantSDNode *constIndex;
      if (Value->getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
          (constIndex = dyn_cast<ConstantSDNode>(Value->getOperand(1)))) {
        // We need to create a new undef vector to use for the VDUPLANE if the
        // size of the vector from which we get the value is different than the
        // size of the vector that we need to create. We will insert the element
        // such that the register coalescer will remove unnecessary copies.
        if (VT != Value->getOperand(0).getValueType()) {
          unsigned index = constIndex->getAPIntValue().getLimitedValue() %
                             VT.getVectorNumElements();
          N =  DAG.getNode(ARMISD::VDUPLANE, dl, VT,
                 DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, VT, DAG.getUNDEF(VT),
                        Value, DAG.getConstant(index, dl, MVT::i32)),
                           DAG.getConstant(index, dl, MVT::i32));
        } else
          N = DAG.getNode(ARMISD::VDUPLANE, dl, VT,
                        Value->getOperand(0), Value->getOperand(1));
      } else
        N = DAG.getNode(ARMISD::VDUP, dl, VT, Value);

      if (!usesOnlyOneValue) {
        // The dominant value was splatted as 'N', but we now have to insert
        // all differing elements.
        for (unsigned I = 0; I < NumElts; ++I) {
          if (Op.getOperand(I) == Value)
            continue;
          SmallVector<SDValue, 3> Ops;
          Ops.push_back(N);
          Ops.push_back(Op.getOperand(I));
          Ops.push_back(DAG.getConstant(I, dl, MVT::i32));
          N = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, VT, Ops);
        }
      }
      return N;
    }
    if (VT.getVectorElementType().isFloatingPoint()) {
      SmallVector<SDValue, 8> Ops;
      MVT FVT = VT.getVectorElementType().getSimpleVT();
      assert(FVT == MVT::f32 || FVT == MVT::f16);
      MVT IVT = (FVT == MVT::f32) ? MVT::i32 : MVT::i16;
      for (unsigned i = 0; i < NumElts; ++i)
        Ops.push_back(DAG.getNode(ISD::BITCAST, dl, IVT,
                                  Op.getOperand(i)));
      EVT VecVT = EVT::getVectorVT(*DAG.getContext(), IVT, NumElts);
      SDValue Val = DAG.getBuildVector(VecVT, dl, Ops);
      Val = LowerBUILD_VECTOR(Val, DAG, ST);
      if (Val.getNode())
        return DAG.getNode(ISD::BITCAST, dl, VT, Val);
    }
    if (usesOnlyOneValue) {
      SDValue Val = IsSingleInstrConstant(Value, DAG, ST, dl);
      if (isConstant && Val.getNode())
        return DAG.getNode(ARMISD::VDUP, dl, VT, Val);
    }
  }

  // If all elements are constants and the case above didn't get hit, fall back
  // to the default expansion, which will generate a load from the constant
  // pool.
  if (isConstant)
    return SDValue();

  // Reconstruct the BUILDVECTOR to one of the legal shuffles (such as vext and
  // vmovn). Empirical tests suggest this is rarely worth it for vectors of
  // length <= 2.
  if (NumElts >= 4)
    if (SDValue shuffle = ReconstructShuffle(Op, DAG))
      return shuffle;

  // Attempt to turn a buildvector of scalar fptrunc's or fpext's back into
  // VCVT's
  if (SDValue VCVT = LowerBuildVectorOfFPTrunc(Op, DAG, Subtarget))
    return VCVT;
  if (SDValue VCVT = LowerBuildVectorOfFPExt(Op, DAG, Subtarget))
    return VCVT;

  if (ST->hasNEON() && VT.is128BitVector() && VT != MVT::v2f64 && VT != MVT::v4f32) {
    // If we haven't found an efficient lowering, try splitting a 128-bit vector
    // into two 64-bit vectors; we might discover a better way to lower it.
    SmallVector<SDValue, 64> Ops(Op->op_begin(), Op->op_begin() + NumElts);
    EVT ExtVT = VT.getVectorElementType();
    EVT HVT = EVT::getVectorVT(*DAG.getContext(), ExtVT, NumElts / 2);
    SDValue Lower = DAG.getBuildVector(HVT, dl, ArrayRef(&Ops[0], NumElts / 2));
    if (Lower.getOpcode() == ISD::BUILD_VECTOR)
      Lower = LowerBUILD_VECTOR(Lower, DAG, ST);
    SDValue Upper =
        DAG.getBuildVector(HVT, dl, ArrayRef(&Ops[NumElts / 2], NumElts / 2));
    if (Upper.getOpcode() == ISD::BUILD_VECTOR)
      Upper = LowerBUILD_VECTOR(Upper, DAG, ST);
    if (Lower && Upper)
      return DAG.getNode(ISD::CONCAT_VECTORS, dl, VT, Lower, Upper);
  }

  // Vectors with 32- or 64-bit elements can be built by directly assigning
  // the subregisters.  Lower it to an ARMISD::BUILD_VECTOR so the operands
  // will be legalized.
  if (EltSize >= 32) {
    // Do the expansion with floating-point types, since that is what the VFP
    // registers are defined to use, and since i64 is not legal.
    EVT EltVT = EVT::getFloatingPointVT(EltSize);
    EVT VecVT = EVT::getVectorVT(*DAG.getContext(), EltVT, NumElts);
    SmallVector<SDValue, 8> Ops;
    for (unsigned i = 0; i < NumElts; ++i)
      Ops.push_back(DAG.getNode(ISD::BITCAST, dl, EltVT, Op.getOperand(i)));
    SDValue Val = DAG.getNode(ARMISD::BUILD_VECTOR, dl, VecVT, Ops);
    return DAG.getNode(ISD::BITCAST, dl, VT, Val);
  }

  // If all else fails, just use a sequence of INSERT_VECTOR_ELT when we
  // know the default expansion would otherwise fall back on something even
  // worse. For a vector with one or two non-undef values, that's
  // scalar_to_vector for the elements followed by a shuffle (provided the
  // shuffle is valid for the target) and materialization element by element
  // on the stack followed by a load for everything else.
  if (!isConstant && !usesOnlyOneValue) {
    SDValue Vec = DAG.getUNDEF(VT);
    for (unsigned i = 0 ; i < NumElts; ++i) {
      SDValue V = Op.getOperand(i);
      if (V.isUndef())
        continue;
      SDValue LaneIdx = DAG.getConstant(i, dl, MVT::i32);
      Vec = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, VT, Vec, V, LaneIdx);
    }
    return Vec;
  }

  return SDValue();
}

// Gather data to see if the operation can be modelled as a
// shuffle in combination with VEXTs.
SDValue ARMTargetLowering::ReconstructShuffle(SDValue Op,
                                              SelectionDAG &DAG) const {
  assert(Op.getOpcode() == ISD::BUILD_VECTOR && "Unknown opcode!");
  SDLoc dl(Op);
  EVT VT = Op.getValueType();
  unsigned NumElts = VT.getVectorNumElements();

  struct ShuffleSourceInfo {
    SDValue Vec;
    unsigned MinElt = std::numeric_limits<unsigned>::max();
    unsigned MaxElt = 0;

    // We may insert some combination of BITCASTs and VEXT nodes to force Vec to
    // be compatible with the shuffle we intend to construct. As a result
    // ShuffleVec will be some sliding window into the original Vec.
    SDValue ShuffleVec;

    // Code should guarantee that element i in Vec starts at element "WindowBase
    // + i * WindowScale in ShuffleVec".
    int WindowBase = 0;
    int WindowScale = 1;

    ShuffleSourceInfo(SDValue Vec) : Vec(Vec), ShuffleVec(Vec) {}

    bool operator ==(SDValue OtherVec) { return Vec == OtherVec; }
  };

  // First gather all vectors used as an immediate source for this BUILD_VECTOR
  // node.
  SmallVector<ShuffleSourceInfo, 2> Sources;
  for (unsigned i = 0; i < NumElts; ++i) {
    SDValue V = Op.getOperand(i);
    if (V.isUndef())
      continue;
    else if (V.getOpcode() != ISD::EXTRACT_VECTOR_ELT) {
      // A shuffle can only come from building a vector from various
      // elements of other vectors.
      return SDValue();
    } else if (!isa<ConstantSDNode>(V.getOperand(1))) {
      // Furthermore, shuffles require a constant mask, whereas extractelts
      // accept variable indices.
      return SDValue();
    }

    // Add this element source to the list if it's not already there.
    SDValue SourceVec = V.getOperand(0);
    auto Source = llvm::find(Sources, SourceVec);
    if (Source == Sources.end())
      Source = Sources.insert(Sources.end(), ShuffleSourceInfo(SourceVec));

    // Update the minimum and maximum lane number seen.
    unsigned EltNo = V.getConstantOperandVal(1);
    Source->MinElt = std::min(Source->MinElt, EltNo);
    Source->MaxElt = std::max(Source->MaxElt, EltNo);
  }

  // Currently only do something sane when at most two source vectors
  // are involved.
  if (Sources.size() > 2)
    return SDValue();

  // Find out the smallest element size among result and two sources, and use
  // it as element size to build the shuffle_vector.
  EVT SmallestEltTy = VT.getVectorElementType();
  for (auto &Source : Sources) {
    EVT SrcEltTy = Source.Vec.getValueType().getVectorElementType();
    if (SrcEltTy.bitsLT(SmallestEltTy))
      SmallestEltTy = SrcEltTy;
  }
  unsigned ResMultiplier =
      VT.getScalarSizeInBits() / SmallestEltTy.getSizeInBits();
  NumElts = VT.getSizeInBits() / SmallestEltTy.getSizeInBits();
  EVT ShuffleVT = EVT::getVectorVT(*DAG.getContext(), SmallestEltTy, NumElts);

  // If the source vector is too wide or too narrow, we may nevertheless be able
  // to construct a compatible shuffle either by concatenating it with UNDEF or
  // extracting a suitable range of elements.
  for (auto &Src : Sources) {
    EVT SrcVT = Src.ShuffleVec.getValueType();

    uint64_t SrcVTSize = SrcVT.getFixedSizeInBits();
    uint64_t VTSize = VT.getFixedSizeInBits();
    if (SrcVTSize == VTSize)
      continue;

    // This stage of the search produces a source with the same element type as
    // the original, but with a total width matching the BUILD_VECTOR output.
    EVT EltVT = SrcVT.getVectorElementType();
    unsigned NumSrcElts = VTSize / EltVT.getFixedSizeInBits();
    EVT DestVT = EVT::getVectorVT(*DAG.getContext(), EltVT, NumSrcElts);

    if (SrcVTSize < VTSize) {
      if (2 * SrcVTSize != VTSize)
        return SDValue();
      // We can pad out the smaller vector for free, so if it's part of a
      // shuffle...
      Src.ShuffleVec =
          DAG.getNode(ISD::CONCAT_VECTORS, dl, DestVT, Src.ShuffleVec,
                      DAG.getUNDEF(Src.ShuffleVec.getValueType()));
      continue;
    }

    if (SrcVTSize != 2 * VTSize)
      return SDValue();

    if (Src.MaxElt - Src.MinElt >= NumSrcElts) {
      // Span too large for a VEXT to cope
      return SDValue();
    }

    if (Src.MinElt >= NumSrcElts) {
      // The extraction can just take the second half
      Src.ShuffleVec =
          DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, DestVT, Src.ShuffleVec,
                      DAG.getConstant(NumSrcElts, dl, MVT::i32));
      Src.WindowBase = -NumSrcElts;
    } else if (Src.MaxElt < NumSrcElts) {
      // The extraction can just take the first half
      Src.ShuffleVec =
          DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, DestVT, Src.ShuffleVec,
                      DAG.getConstant(0, dl, MVT::i32));
    } else {
      // An actual VEXT is needed
      SDValue VEXTSrc1 =
          DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, DestVT, Src.ShuffleVec,
                      DAG.getConstant(0, dl, MVT::i32));
      SDValue VEXTSrc2 =
          DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, DestVT, Src.ShuffleVec,
                      DAG.getConstant(NumSrcElts, dl, MVT::i32));

      Src.ShuffleVec = DAG.getNode(ARMISD::VEXT, dl, DestVT, VEXTSrc1,
                                   VEXTSrc2,
                                   DAG.getConstant(Src.MinElt, dl, MVT::i32));
      Src.WindowBase = -Src.MinElt;
    }
  }

  // Another possible incompatibility occurs from the vector element types. We
  // can fix this by bitcasting the source vectors to the same type we intend
  // for the shuffle.
  for (auto &Src : Sources) {
    EVT SrcEltTy = Src.ShuffleVec.getValueType().getVectorElementType();
    if (SrcEltTy == SmallestEltTy)
      continue;
    assert(ShuffleVT.getVectorElementType() == SmallestEltTy);
    Src.ShuffleVec = DAG.getNode(ARMISD::VECTOR_REG_CAST, dl, ShuffleVT, Src.ShuffleVec);
    Src.WindowScale = SrcEltTy.getSizeInBits() / SmallestEltTy.getSizeInBits();
    Src.WindowBase *= Src.WindowScale;
  }

  // Final check before we try to actually produce a shuffle.
  LLVM_DEBUG(for (auto Src
                  : Sources)
                 assert(Src.ShuffleVec.getValueType() == ShuffleVT););

  // The stars all align, our next step is to produce the mask for the shuffle.
  SmallVector<int, 8> Mask(ShuffleVT.getVectorNumElements(), -1);
  int BitsPerShuffleLane = ShuffleVT.getScalarSizeInBits();
  for (unsigned i = 0; i < VT.getVectorNumElements(); ++i) {
    SDValue Entry = Op.getOperand(i);
    if (Entry.isUndef())
      continue;

    auto Src = llvm::find(Sources, Entry.getOperand(0));
    int EltNo = cast<ConstantSDNode>(Entry.getOperand(1))->getSExtValue();

    // EXTRACT_VECTOR_ELT performs an implicit any_ext; BUILD_VECTOR an implicit
    // trunc. So only std::min(SrcBits, DestBits) actually get defined in this
    // segment.
    EVT OrigEltTy = Entry.getOperand(0).getValueType().getVectorElementType();
    int BitsDefined = std::min(OrigEltTy.getScalarSizeInBits(),
                               VT.getScalarSizeInBits());
    int LanesDefined = BitsDefined / BitsPerShuffleLane;

    // This source is expected to fill ResMultiplier lanes of the final shuffle,
    // starting at the appropriate offset.
    int *LaneMask = &Mask[i * ResMultiplier];

    int ExtractBase = EltNo * Src->WindowScale + Src->WindowBase;
    ExtractBase += NumElts * (Src - Sources.begin());
    for (int j = 0; j < LanesDefined; ++j)
      LaneMask[j] = ExtractBase + j;
  }


  // We can't handle more than two sources. This should have already
  // been checked before this point.
  assert(Sources.size() <= 2 && "Too many sources!");

  SDValue ShuffleOps[] = { DAG.getUNDEF(ShuffleVT), DAG.getUNDEF(ShuffleVT) };
  for (unsigned i = 0; i < Sources.size(); ++i)
    ShuffleOps[i] = Sources[i].ShuffleVec;

  SDValue Shuffle = buildLegalVectorShuffle(ShuffleVT, dl, ShuffleOps[0],
                                            ShuffleOps[1], Mask, DAG);
  if (!Shuffle)
    return SDValue();
  return DAG.getNode(ARMISD::VECTOR_REG_CAST, dl, VT, Shuffle);
}

enum ShuffleOpCodes {
  OP_COPY = 0, // Copy, used for things like <u,u,u,3> to say it is <0,1,2,3>
  OP_VREV,
  OP_VDUP0,
  OP_VDUP1,
  OP_VDUP2,
  OP_VDUP3,
  OP_VEXT1,
  OP_VEXT2,
  OP_VEXT3,
  OP_VUZPL, // VUZP, left result
  OP_VUZPR, // VUZP, right result
  OP_VZIPL, // VZIP, left result
  OP_VZIPR, // VZIP, right result
  OP_VTRNL, // VTRN, left result
  OP_VTRNR  // VTRN, right result
};

static bool isLegalMVEShuffleOp(unsigned PFEntry) {
  unsigned OpNum = (PFEntry >> 26) & 0x0F;
  switch (OpNum) {
  case OP_COPY:
  case OP_VREV:
  case OP_VDUP0:
  case OP_VDUP1:
  case OP_VDUP2:
  case OP_VDUP3:
    return true;
  }
  return false;
}

/// isShuffleMaskLegal - Targets can use this to indicate that they only
/// support *some* VECTOR_SHUFFLE operations, those with specific masks.
/// By default, if a target supports the VECTOR_SHUFFLE node, all mask values
/// are assumed to be legal.
bool ARMTargetLowering::isShuffleMaskLegal(ArrayRef<int> M, EVT VT) const {
  if (VT.getVectorNumElements() == 4 &&
      (VT.is128BitVector() || VT.is64BitVector())) {
    unsigned PFIndexes[4];
    for (unsigned i = 0; i != 4; ++i) {
      if (M[i] < 0)
        PFIndexes[i] = 8;
      else
        PFIndexes[i] = M[i];
    }

    // Compute the index in the perfect shuffle table.
    unsigned PFTableIndex =
      PFIndexes[0]*9*9*9+PFIndexes[1]*9*9+PFIndexes[2]*9+PFIndexes[3];
    unsigned PFEntry = PerfectShuffleTable[PFTableIndex];
    unsigned Cost = (PFEntry >> 30);

    if (Cost <= 4 && (Subtarget->hasNEON() || isLegalMVEShuffleOp(PFEntry)))
      return true;
  }

  bool ReverseVEXT, isV_UNDEF;
  unsigned Imm, WhichResult;

  unsigned EltSize = VT.getScalarSizeInBits();
  if (EltSize >= 32 ||
      ShuffleVectorSDNode::isSplatMask(&M[0], VT) ||
      ShuffleVectorInst::isIdentityMask(M, M.size()) ||
      isVREVMask(M, VT, 64) ||
      isVREVMask(M, VT, 32) ||
      isVREVMask(M, VT, 16))
    return true;
  else if (Subtarget->hasNEON() &&
           (isVEXTMask(M, VT, ReverseVEXT, Imm) ||
            isVTBLMask(M, VT) ||
            isNEONTwoResultShuffleMask(M, VT, WhichResult, isV_UNDEF)))
    return true;
  else if ((VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v16i8) &&
           isReverseMask(M, VT))
    return true;
  else if (Subtarget->hasMVEIntegerOps() &&
           (isVMOVNMask(M, VT, true, false) ||
            isVMOVNMask(M, VT, false, false) || isVMOVNMask(M, VT, true, true)))
    return true;
  else if (Subtarget->hasMVEIntegerOps() &&
           (isTruncMask(M, VT, false, false) ||
            isTruncMask(M, VT, false, true) ||
            isTruncMask(M, VT, true, false) || isTruncMask(M, VT, true, true)))
    return true;
  else
    return false;
}

/// GeneratePerfectShuffle - Given an entry in the perfect-shuffle table, emit
/// the specified operations to build the shuffle.
static SDValue GeneratePerfectShuffle(unsigned PFEntry, SDValue LHS,
                                      SDValue RHS, SelectionDAG &DAG,
                                      const SDLoc &dl) {
  unsigned OpNum = (PFEntry >> 26) & 0x0F;
  unsigned LHSID = (PFEntry >> 13) & ((1 << 13)-1);
  unsigned RHSID = (PFEntry >>  0) & ((1 << 13)-1);

  if (OpNum == OP_COPY) {
    if (LHSID == (1*9+2)*9+3) return LHS;
    assert(LHSID == ((4*9+5)*9+6)*9+7 && "Illegal OP_COPY!");
    return RHS;
  }

  SDValue OpLHS, OpRHS;
  OpLHS = GeneratePerfectShuffle(PerfectShuffleTable[LHSID], LHS, RHS, DAG, dl);
  OpRHS = GeneratePerfectShuffle(PerfectShuffleTable[RHSID], LHS, RHS, DAG, dl);
  EVT VT = OpLHS.getValueType();

  switch (OpNum) {
  default: llvm_unreachable("Unknown shuffle opcode!");
  case OP_VREV:
    // VREV divides the vector in half and swaps within the half.
    if (VT.getScalarSizeInBits() == 32)
      return DAG.getNode(ARMISD::VREV64, dl, VT, OpLHS);
    // vrev <4 x i16> -> VREV32
    if (VT.getScalarSizeInBits() == 16)
      return DAG.getNode(ARMISD::VREV32, dl, VT, OpLHS);
    // vrev <4 x i8> -> VREV16
    assert(VT.getScalarSizeInBits() == 8);
    return DAG.getNode(ARMISD::VREV16, dl, VT, OpLHS);
  case OP_VDUP0:
  case OP_VDUP1:
  case OP_VDUP2:
  case OP_VDUP3:
    return DAG.getNode(ARMISD::VDUPLANE, dl, VT,
                       OpLHS, DAG.getConstant(OpNum-OP_VDUP0, dl, MVT::i32));
  case OP_VEXT1:
  case OP_VEXT2:
  case OP_VEXT3:
    return DAG.getNode(ARMISD::VEXT, dl, VT,
                       OpLHS, OpRHS,
                       DAG.getConstant(OpNum - OP_VEXT1 + 1, dl, MVT::i32));
  case OP_VUZPL:
  case OP_VUZPR:
    return DAG.getNode(ARMISD::VUZP, dl, DAG.getVTList(VT, VT),
                       OpLHS, OpRHS).getValue(OpNum-OP_VUZPL);
  case OP_VZIPL:
  case OP_VZIPR:
    return DAG.getNode(ARMISD::VZIP, dl, DAG.getVTList(VT, VT),
                       OpLHS, OpRHS).getValue(OpNum-OP_VZIPL);
  case OP_VTRNL:
  case OP_VTRNR:
    return DAG.getNode(ARMISD::VTRN, dl, DAG.getVTList(VT, VT),
                       OpLHS, OpRHS).getValue(OpNum-OP_VTRNL);
  }
}

static SDValue LowerVECTOR_SHUFFLEv8i8(SDValue Op,
                                       ArrayRef<int> ShuffleMask,
                                       SelectionDAG &DAG) {
  // Check to see if we can use the VTBL instruction.
  SDValue V1 = Op.getOperand(0);
  SDValue V2 = Op.getOperand(1);
  SDLoc DL(Op);

  SmallVector<SDValue, 8> VTBLMask;
  for (int I : ShuffleMask)
    VTBLMask.push_back(DAG.getConstant(I, DL, MVT::i32));

  if (V2.getNode()->isUndef())
    return DAG.getNode(ARMISD::VTBL1, DL, MVT::v8i8, V1,
                       DAG.getBuildVector(MVT::v8i8, DL, VTBLMask));

  return DAG.getNode(ARMISD::VTBL2, DL, MVT::v8i8, V1, V2,
                     DAG.getBuildVector(MVT::v8i8, DL, VTBLMask));
}

static SDValue LowerReverse_VECTOR_SHUFFLE(SDValue Op, SelectionDAG &DAG) {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();

  assert((VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v16i8) &&
         "Expect an v8i16/v16i8 type");
  SDValue OpLHS = DAG.getNode(ARMISD::VREV64, DL, VT, Op.getOperand(0));
  // For a v16i8 type: After the VREV, we have got <7, ..., 0, 15, ..., 8>. Now,
  // extract the first 8 bytes into the top double word and the last 8 bytes
  // into the bottom double word, through a new vector shuffle that will be
  // turned into a VEXT on Neon, or a couple of VMOVDs on MVE.
  std::vector<int> NewMask;
  for (unsigned i = 0; i < VT.getVectorNumElements() / 2; i++)
    NewMask.push_back(VT.getVectorNumElements() / 2 + i);
  for (unsigned i = 0; i < VT.getVectorNumElements() / 2; i++)
    NewMask.push_back(i);
  return DAG.getVectorShuffle(VT, DL, OpLHS, OpLHS, NewMask);
}

static EVT getVectorTyFromPredicateVector(EVT VT) {
  switch (VT.getSimpleVT().SimpleTy) {
  case MVT::v2i1:
    return MVT::v2f64;
  case MVT::v4i1:
    return MVT::v4i32;
  case MVT::v8i1:
    return MVT::v8i16;
  case MVT::v16i1:
    return MVT::v16i8;
  default:
    llvm_unreachable("Unexpected vector predicate type");
  }
}

static SDValue PromoteMVEPredVector(SDLoc dl, SDValue Pred, EVT VT,
                                    SelectionDAG &DAG) {
  // Converting from boolean predicates to integers involves creating a vector
  // of all ones or all zeroes and selecting the lanes based upon the real
  // predicate.
  SDValue AllOnes =
      DAG.getTargetConstant(ARM_AM::createVMOVModImm(0xe, 0xff), dl, MVT::i32);
  AllOnes = DAG.getNode(ARMISD::VMOVIMM, dl, MVT::v16i8, AllOnes);

  SDValue AllZeroes =
      DAG.getTargetConstant(ARM_AM::createVMOVModImm(0xe, 0x0), dl, MVT::i32);
  AllZeroes = DAG.getNode(ARMISD::VMOVIMM, dl, MVT::v16i8, AllZeroes);

  // Get full vector type from predicate type
  EVT NewVT = getVectorTyFromPredicateVector(VT);

  SDValue RecastV1;
  // If the real predicate is an v8i1 or v4i1 (not v16i1) then we need to recast
  // this to a v16i1. This cannot be done with an ordinary bitcast because the
  // sizes are not the same. We have to use a MVE specific PREDICATE_CAST node,
  // since we know in hardware the sizes are really the same.
  if (VT != MVT::v16i1)
    RecastV1 = DAG.getNode(ARMISD::PREDICATE_CAST, dl, MVT::v16i1, Pred);
  else
    RecastV1 = Pred;

  // Select either all ones or zeroes depending upon the real predicate bits.
  SDValue PredAsVector =
      DAG.getNode(ISD::VSELECT, dl, MVT::v16i8, RecastV1, AllOnes, AllZeroes);

  // Recast our new predicate-as-integer v16i8 vector into something
  // appropriate for the shuffle, i.e. v4i32 for a real v4i1 predicate.
  return DAG.getNode(ISD::BITCAST, dl, NewVT, PredAsVector);
}

static SDValue LowerVECTOR_SHUFFLE_i1(SDValue Op, SelectionDAG &DAG,
                                      const ARMSubtarget *ST) {
  EVT VT = Op.getValueType();
  ShuffleVectorSDNode *SVN = cast<ShuffleVectorSDNode>(Op.getNode());
  ArrayRef<int> ShuffleMask = SVN->getMask();

  assert(ST->hasMVEIntegerOps() &&
         "No support for vector shuffle of boolean predicates");

  SDValue V1 = Op.getOperand(0);
  SDValue V2 = Op.getOperand(1);
  SDLoc dl(Op);
  if (isReverseMask(ShuffleMask, VT)) {
    SDValue cast = DAG.getNode(ARMISD::PREDICATE_CAST, dl, MVT::i32, V1);
    SDValue rbit = DAG.getNode(ISD::BITREVERSE, dl, MVT::i32, cast);
    SDValue srl = DAG.getNode(ISD::SRL, dl, MVT::i32, rbit,
                              DAG.getConstant(16, dl, MVT::i32));
    return DAG.getNode(ARMISD::PREDICATE_CAST, dl, VT, srl);
  }

  // Until we can come up with optimised cases for every single vector
  // shuffle in existence we have chosen the least painful strategy. This is
  // to essentially promote the boolean predicate to a 8-bit integer, where
  // each predicate represents a byte. Then we fall back on a normal integer
  // vector shuffle and convert the result back into a predicate vector. In
  // many cases the generated code might be even better than scalar code
  // operating on bits. Just imagine trying to shuffle 8 arbitrary 2-bit
  // fields in a register into 8 other arbitrary 2-bit fields!
  SDValue PredAsVector1 = PromoteMVEPredVector(dl, V1, VT, DAG);
  EVT NewVT = PredAsVector1.getValueType();
  SDValue PredAsVector2 = V2.isUndef() ? DAG.getUNDEF(NewVT)
                                       : PromoteMVEPredVector(dl, V2, VT, DAG);
  assert(PredAsVector2.getValueType() == NewVT &&
         "Expected identical vector type in expanded i1 shuffle!");

  // Do the shuffle!
  SDValue Shuffled = DAG.getVectorShuffle(NewVT, dl, PredAsVector1,
                                          PredAsVector2, ShuffleMask);

  // Now return the result of comparing the shuffled vector with zero,
  // which will generate a real predicate, i.e. v4i1, v8i1 or v16i1. For a v2i1
  // we convert to a v4i1 compare to fill in the two halves of the i64 as i32s.
  if (VT == MVT::v2i1) {
    SDValue BC = DAG.getNode(ARMISD::VECTOR_REG_CAST, dl, MVT::v4i32, Shuffled);
    SDValue Cmp = DAG.getNode(ARMISD::VCMPZ, dl, MVT::v4i1, BC,
                              DAG.getConstant(ARMCC::NE, dl, MVT::i32));
    return DAG.getNode(ARMISD::PREDICATE_CAST, dl, MVT::v2i1, Cmp);
  }
  return DAG.getNode(ARMISD::VCMPZ, dl, VT, Shuffled,
                     DAG.getConstant(ARMCC::NE, dl, MVT::i32));
}

static SDValue LowerVECTOR_SHUFFLEUsingMovs(SDValue Op,
                                            ArrayRef<int> ShuffleMask,
                                            SelectionDAG &DAG) {
  // Attempt to lower the vector shuffle using as many whole register movs as
  // possible. This is useful for types smaller than 32bits, which would
  // often otherwise become a series for grp movs.
  SDLoc dl(Op);
  EVT VT = Op.getValueType();
  if (VT.getScalarSizeInBits() >= 32)
    return SDValue();

  assert((VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v16i8) &&
         "Unexpected vector type");
  int NumElts = VT.getVectorNumElements();
  int QuarterSize = NumElts / 4;
  // The four final parts of the vector, as i32's
  SDValue Parts[4];

  // Look for full lane vmovs like <0,1,2,3> or <u,5,6,7> etc, (but not
  // <u,u,u,u>), returning the vmov lane index
  auto getMovIdx = [](ArrayRef<int> ShuffleMask, int Start, int Length) {
    // Detect which mov lane this would be from the first non-undef element.
    int MovIdx = -1;
    for (int i = 0; i < Length; i++) {
      if (ShuffleMask[Start + i] >= 0) {
        if (ShuffleMask[Start + i] % Length != i)
          return -1;
        MovIdx = ShuffleMask[Start + i] / Length;
        break;
      }
    }
    // If all items are undef, leave this for other combines
    if (MovIdx == -1)
      return -1;
    // Check the remaining values are the correct part of the same mov
    for (int i = 1; i < Length; i++) {
      if (ShuffleMask[Start + i] >= 0 &&
          (ShuffleMask[Start + i] / Length != MovIdx ||
           ShuffleMask[Start + i] % Length != i))
        return -1;
    }
    return MovIdx;
  };

  for (int Part = 0; Part < 4; ++Part) {
    // Does this part look like a mov
    int Elt = getMovIdx(ShuffleMask, Part * QuarterSize, QuarterSize);
    if (Elt != -1) {
      SDValue Input = Op->getOperand(0);
      if (Elt >= 4) {
        Input = Op->getOperand(1);
        Elt -= 4;
      }
      SDValue BitCast = DAG.getBitcast(MVT::v4f32, Input);
      Parts[Part] = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::f32, BitCast,
                                DAG.getConstant(Elt, dl, MVT::i32));
    }
  }

  // Nothing interesting found, just return
  if (!Parts[0] && !Parts[1] && !Parts[2] && !Parts[3])
    return SDValue();

  // The other parts need to be built with the old shuffle vector, cast to a
  // v4i32 and extract_vector_elts
  if (!Parts[0] || !Parts[1] || !Parts[2] || !Parts[3]) {
    SmallVector<int, 16> NewShuffleMask;
    for (int Part = 0; Part < 4; ++Part)
      for (int i = 0; i < QuarterSize; i++)
        NewShuffleMask.push_back(
            Parts[Part] ? -1 : ShuffleMask[Part * QuarterSize + i]);
    SDValue NewShuffle = DAG.getVectorShuffle(
        VT, dl, Op->getOperand(0), Op->getOperand(1), NewShuffleMask);
    SDValue BitCast = DAG.getBitcast(MVT::v4f32, NewShuffle);

    for (int Part = 0; Part < 4; ++Part)
      if (!Parts[Part])
        Parts[Part] = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::f32,
                                  BitCast, DAG.getConstant(Part, dl, MVT::i32));
  }
  // Build a vector out of the various parts and bitcast it back to the original
  // type.
  SDValue NewVec = DAG.getNode(ARMISD::BUILD_VECTOR, dl, MVT::v4f32, Parts);
  return DAG.getBitcast(VT, NewVec);
}

static SDValue LowerVECTOR_SHUFFLEUsingOneOff(SDValue Op,
                                              ArrayRef<int> ShuffleMask,
                                              SelectionDAG &DAG) {
  SDValue V1 = Op.getOperand(0);
  SDValue V2 = Op.getOperand(1);
  EVT VT = Op.getValueType();
  unsigned NumElts = VT.getVectorNumElements();

  // An One-Off Identity mask is one that is mostly an identity mask from as
  // single source but contains a single element out-of-place, either from a
  // different vector or from another position in the same vector. As opposed to
  // lowering this via a ARMISD::BUILD_VECTOR we can generate an extract/insert
  // pair directly.
  auto isOneOffIdentityMask = [](ArrayRef<int> Mask, EVT VT, int BaseOffset,
                                 int &OffElement) {
    OffElement = -1;
    int NonUndef = 0;
    for (int i = 0, NumMaskElts = Mask.size(); i < NumMaskElts; ++i) {
      if (Mask[i] == -1)
        continue;
      NonUndef++;
      if (Mask[i] != i + BaseOffset) {
        if (OffElement == -1)
          OffElement = i;
        else
          return false;
      }
    }
    return NonUndef > 2 && OffElement != -1;
  };
  int OffElement;
  SDValue VInput;
  if (isOneOffIdentityMask(ShuffleMask, VT, 0, OffElement))
    VInput = V1;
  else if (isOneOffIdentityMask(ShuffleMask, VT, NumElts, OffElement))
    VInput = V2;
  else
    return SDValue();

  SDLoc dl(Op);
  EVT SVT = VT.getScalarType() == MVT::i8 || VT.getScalarType() == MVT::i16
                ? MVT::i32
                : VT.getScalarType();
  SDValue Elt = DAG.getNode(
      ISD::EXTRACT_VECTOR_ELT, dl, SVT,
      ShuffleMask[OffElement] < (int)NumElts ? V1 : V2,
      DAG.getVectorIdxConstant(ShuffleMask[OffElement] % NumElts, dl));
  return DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, VT, VInput, Elt,
                     DAG.getVectorIdxConstant(OffElement % NumElts, dl));
}

static SDValue LowerVECTOR_SHUFFLE(SDValue Op, SelectionDAG &DAG,
                                   const ARMSubtarget *ST) {
  SDValue V1 = Op.getOperand(0);
  SDValue V2 = Op.getOperand(1);
  SDLoc dl(Op);
  EVT VT = Op.getValueType();
  ShuffleVectorSDNode *SVN = cast<ShuffleVectorSDNode>(Op.getNode());
  unsigned EltSize = VT.getScalarSizeInBits();

  if (ST->hasMVEIntegerOps() && EltSize == 1)
    return LowerVECTOR_SHUFFLE_i1(Op, DAG, ST);

  // Convert shuffles that are directly supported on NEON to target-specific
  // DAG nodes, instead of keeping them as shuffles and matching them again
  // during code selection.  This is more efficient and avoids the possibility
  // of inconsistencies between legalization and selection.
  // FIXME: floating-point vectors should be canonicalized to integer vectors
  // of the same time so that they get CSEd properly.
  ArrayRef<int> ShuffleMask = SVN->getMask();

  if (EltSize <= 32) {
    if (SVN->isSplat()) {
      int Lane = SVN->getSplatIndex();
      // If this is undef splat, generate it via "just" vdup, if possible.
      if (Lane == -1) Lane = 0;

      // Test if V1 is a SCALAR_TO_VECTOR.
      if (Lane == 0 && V1.getOpcode() == ISD::SCALAR_TO_VECTOR) {
        return DAG.getNode(ARMISD::VDUP, dl, VT, V1.getOperand(0));
      }
      // Test if V1 is a BUILD_VECTOR which is equivalent to a SCALAR_TO_VECTOR
      // (and probably will turn into a SCALAR_TO_VECTOR once legalization
      // reaches it).
      if (Lane == 0 && V1.getOpcode() == ISD::BUILD_VECTOR &&
          !isa<ConstantSDNode>(V1.getOperand(0))) {
        bool IsScalarToVector = true;
        for (unsigned i = 1, e = V1.getNumOperands(); i != e; ++i)
          if (!V1.getOperand(i).isUndef()) {
            IsScalarToVector = false;
            break;
          }
        if (IsScalarToVector)
          return DAG.getNode(ARMISD::VDUP, dl, VT, V1.getOperand(0));
      }
      return DAG.getNode(ARMISD::VDUPLANE, dl, VT, V1,
                         DAG.getConstant(Lane, dl, MVT::i32));
    }

    bool ReverseVEXT = false;
    unsigned Imm = 0;
    if (ST->hasNEON() && isVEXTMask(ShuffleMask, VT, ReverseVEXT, Imm)) {
      if (ReverseVEXT)
        std::swap(V1, V2);
      return DAG.getNode(ARMISD::VEXT, dl, VT, V1, V2,
                         DAG.getConstant(Imm, dl, MVT::i32));
    }

    if (isVREVMask(ShuffleMask, VT, 64))
      return DAG.getNode(ARMISD::VREV64, dl, VT, V1);
    if (isVREVMask(ShuffleMask, VT, 32))
      return DAG.getNode(ARMISD::VREV32, dl, VT, V1);
    if (isVREVMask(ShuffleMask, VT, 16))
      return DAG.getNode(ARMISD::VREV16, dl, VT, V1);

    if (ST->hasNEON() && V2->isUndef() && isSingletonVEXTMask(ShuffleMask, VT, Imm)) {
      return DAG.getNode(ARMISD::VEXT, dl, VT, V1, V1,
                         DAG.getConstant(Imm, dl, MVT::i32));
    }

    // Check for Neon shuffles that modify both input vectors in place.
    // If both results are used, i.e., if there are two shuffles with the same
    // source operands and with masks corresponding to both results of one of
    // these operations, DAG memoization will ensure that a single node is
    // used for both shuffles.
    unsigned WhichResult = 0;
    bool isV_UNDEF = false;
    if (ST->hasNEON()) {
      if (unsigned ShuffleOpc = isNEONTwoResultShuffleMask(
              ShuffleMask, VT, WhichResult, isV_UNDEF)) {
        if (isV_UNDEF)
          V2 = V1;
        return DAG.getNode(ShuffleOpc, dl, DAG.getVTList(VT, VT), V1, V2)
            .getValue(WhichResult);
      }
    }
    if (ST->hasMVEIntegerOps()) {
      if (isVMOVNMask(ShuffleMask, VT, false, false))
        return DAG.getNode(ARMISD::VMOVN, dl, VT, V2, V1,
                           DAG.getConstant(0, dl, MVT::i32));
      if (isVMOVNMask(ShuffleMask, VT, true, false))
        return DAG.getNode(ARMISD::VMOVN, dl, VT, V1, V2,
                           DAG.getConstant(1, dl, MVT::i32));
      if (isVMOVNMask(ShuffleMask, VT, true, true))
        return DAG.getNode(ARMISD::VMOVN, dl, VT, V1, V1,
                           DAG.getConstant(1, dl, MVT::i32));
    }

    // Also check for these shuffles through CONCAT_VECTORS: we canonicalize
    // shuffles that produce a result larger than their operands with:
    //   shuffle(concat(v1, undef), concat(v2, undef))
    // ->
    //   shuffle(concat(v1, v2), undef)
    // because we can access quad vectors (see PerformVECTOR_SHUFFLECombine).
    //
    // This is useful in the general case, but there are special cases where
    // native shuffles produce larger results: the two-result ops.
    //
    // Look through the concat when lowering them:
    //   shuffle(concat(v1, v2), undef)
    // ->
    //   concat(VZIP(v1, v2):0, :1)
    //
    if (ST->hasNEON() && V1->getOpcode() == ISD::CONCAT_VECTORS && V2->isUndef()) {
      SDValue SubV1 = V1->getOperand(0);
      SDValue SubV2 = V1->getOperand(1);
      EVT SubVT = SubV1.getValueType();

      // We expect these to have been canonicalized to -1.
      assert(llvm::all_of(ShuffleMask, [&](int i) {
        return i < (int)VT.getVectorNumElements();
      }) && "Unexpected shuffle index into UNDEF operand!");

      if (unsigned ShuffleOpc = isNEONTwoResultShuffleMask(
              ShuffleMask, SubVT, WhichResult, isV_UNDEF)) {
        if (isV_UNDEF)
          SubV2 = SubV1;
        assert((WhichResult == 0) &&
               "In-place shuffle of concat can only have one result!");
        SDValue Res = DAG.getNode(ShuffleOpc, dl, DAG.getVTList(SubVT, SubVT),
                                  SubV1, SubV2);
        return DAG.getNode(ISD::CONCAT_VECTORS, dl, VT, Res.getValue(0),
                           Res.getValue(1));
      }
    }
  }

  if (ST->hasMVEIntegerOps() && EltSize <= 32) {
    if (SDValue V = LowerVECTOR_SHUFFLEUsingOneOff(Op, ShuffleMask, DAG))
      return V;

    for (bool Top : {false, true}) {
      for (bool SingleSource : {false, true}) {
        if (isTruncMask(ShuffleMask, VT, Top, SingleSource)) {
          MVT FromSVT = MVT::getIntegerVT(EltSize * 2);
          MVT FromVT = MVT::getVectorVT(FromSVT, ShuffleMask.size() / 2);
          SDValue Lo = DAG.getNode(ARMISD::VECTOR_REG_CAST, dl, FromVT, V1);
          SDValue Hi = DAG.getNode(ARMISD::VECTOR_REG_CAST, dl, FromVT,
                                   SingleSource ? V1 : V2);
          if (Top) {
            SDValue Amt = DAG.getConstant(EltSize, dl, FromVT);
            Lo = DAG.getNode(ISD::SRL, dl, FromVT, Lo, Amt);
            Hi = DAG.getNode(ISD::SRL, dl, FromVT, Hi, Amt);
          }
          return DAG.getNode(ARMISD::MVETRUNC, dl, VT, Lo, Hi);
        }
      }
    }
  }

  // If the shuffle is not directly supported and it has 4 elements, use
  // the PerfectShuffle-generated table to synthesize it from other shuffles.
  unsigned NumElts = VT.getVectorNumElements();
  if (NumElts == 4) {
    unsigned PFIndexes[4];
    for (unsigned i = 0; i != 4; ++i) {
      if (ShuffleMask[i] < 0)
        PFIndexes[i] = 8;
      else
        PFIndexes[i] = ShuffleMask[i];
    }

    // Compute the index in the perfect shuffle table.
    unsigned PFTableIndex =
      PFIndexes[0]*9*9*9+PFIndexes[1]*9*9+PFIndexes[2]*9+PFIndexes[3];
    unsigned PFEntry = PerfectShuffleTable[PFTableIndex];
    unsigned Cost = (PFEntry >> 30);

    if (Cost <= 4) {
      if (ST->hasNEON())
        return GeneratePerfectShuffle(PFEntry, V1, V2, DAG, dl);
      else if (isLegalMVEShuffleOp(PFEntry)) {
        unsigned LHSID = (PFEntry >> 13) & ((1 << 13)-1);
        unsigned RHSID = (PFEntry >>  0) & ((1 << 13)-1);
        unsigned PFEntryLHS = PerfectShuffleTable[LHSID];
        unsigned PFEntryRHS = PerfectShuffleTable[RHSID];
        if (isLegalMVEShuffleOp(PFEntryLHS) && isLegalMVEShuffleOp(PFEntryRHS))
          return GeneratePerfectShuffle(PFEntry, V1, V2, DAG, dl);
      }
    }
  }

  // Implement shuffles with 32- or 64-bit elements as ARMISD::BUILD_VECTORs.
  if (EltSize >= 32) {
    // Do the expansion with floating-point types, since that is what the VFP
    // registers are defined to use, and since i64 is not legal.
    EVT EltVT = EVT::getFloatingPointVT(EltSize);
    EVT VecVT = EVT::getVectorVT(*DAG.getContext(), EltVT, NumElts);
    V1 = DAG.getNode(ISD::BITCAST, dl, VecVT, V1);
    V2 = DAG.getNode(ISD::BITCAST, dl, VecVT, V2);
    SmallVector<SDValue, 8> Ops;
    for (unsigned i = 0; i < NumElts; ++i) {
      if (ShuffleMask[i] < 0)
        Ops.push_back(DAG.getUNDEF(EltVT));
      else
        Ops.push_back(DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT,
                                  ShuffleMask[i] < (int)NumElts ? V1 : V2,
                                  DAG.getConstant(ShuffleMask[i] & (NumElts-1),
                                                  dl, MVT::i32)));
    }
    SDValue Val = DAG.getNode(ARMISD::BUILD_VECTOR, dl, VecVT, Ops);
    return DAG.getNode(ISD::BITCAST, dl, VT, Val);
  }

  if ((VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v16i8) &&
      isReverseMask(ShuffleMask, VT))
    return LowerReverse_VECTOR_SHUFFLE(Op, DAG);

  if (ST->hasNEON() && VT == MVT::v8i8)
    if (SDValue NewOp = LowerVECTOR_SHUFFLEv8i8(Op, ShuffleMask, DAG))
      return NewOp;

  if (ST->hasMVEIntegerOps())
    if (SDValue NewOp = LowerVECTOR_SHUFFLEUsingMovs(Op, ShuffleMask, DAG))
      return NewOp;

  return SDValue();
}

static SDValue LowerINSERT_VECTOR_ELT_i1(SDValue Op, SelectionDAG &DAG,
                                         const ARMSubtarget *ST) {
  EVT VecVT = Op.getOperand(0).getValueType();
  SDLoc dl(Op);

  assert(ST->hasMVEIntegerOps() &&
         "LowerINSERT_VECTOR_ELT_i1 called without MVE!");

  SDValue Conv =
      DAG.getNode(ARMISD::PREDICATE_CAST, dl, MVT::i32, Op->getOperand(0));
  unsigned Lane = Op.getConstantOperandVal(2);
  unsigned LaneWidth =
      getVectorTyFromPredicateVector(VecVT).getScalarSizeInBits() / 8;
  unsigned Mask = ((1 << LaneWidth) - 1) << Lane * LaneWidth;
  SDValue Ext = DAG.getNode(ISD::SIGN_EXTEND_INREG, dl, MVT::i32,
                            Op.getOperand(1), DAG.getValueType(MVT::i1));
  SDValue BFI = DAG.getNode(ARMISD::BFI, dl, MVT::i32, Conv, Ext,
                            DAG.getConstant(~Mask, dl, MVT::i32));
  return DAG.getNode(ARMISD::PREDICATE_CAST, dl, Op.getValueType(), BFI);
}

SDValue ARMTargetLowering::LowerINSERT_VECTOR_ELT(SDValue Op,
                                                  SelectionDAG &DAG) const {
  // INSERT_VECTOR_ELT is legal only for immediate indexes.
  SDValue Lane = Op.getOperand(2);
  if (!isa<ConstantSDNode>(Lane))
    return SDValue();

  SDValue Elt = Op.getOperand(1);
  EVT EltVT = Elt.getValueType();

  if (Subtarget->hasMVEIntegerOps() &&
      Op.getValueType().getScalarSizeInBits() == 1)
    return LowerINSERT_VECTOR_ELT_i1(Op, DAG, Subtarget);

  if (getTypeAction(*DAG.getContext(), EltVT) ==
      TargetLowering::TypeSoftPromoteHalf) {
    // INSERT_VECTOR_ELT doesn't want f16 operands promoting to f32,
    // but the type system will try to do that if we don't intervene.
    // Reinterpret any such vector-element insertion as one with the
    // corresponding integer types.

    SDLoc dl(Op);

    EVT IEltVT = MVT::getIntegerVT(EltVT.getScalarSizeInBits());
    assert(getTypeAction(*DAG.getContext(), IEltVT) !=
           TargetLowering::TypeSoftPromoteHalf);

    SDValue VecIn = Op.getOperand(0);
    EVT VecVT = VecIn.getValueType();
    EVT IVecVT = EVT::getVectorVT(*DAG.getContext(), IEltVT,
                                  VecVT.getVectorNumElements());

    SDValue IElt = DAG.getNode(ISD::BITCAST, dl, IEltVT, Elt);
    SDValue IVecIn = DAG.getNode(ISD::BITCAST, dl, IVecVT, VecIn);
    SDValue IVecOut = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, IVecVT,
                                  IVecIn, IElt, Lane);
    return DAG.getNode(ISD::BITCAST, dl, VecVT, IVecOut);
  }

  return Op;
}

static SDValue LowerEXTRACT_VECTOR_ELT_i1(SDValue Op, SelectionDAG &DAG,
                                          const ARMSubtarget *ST) {
  EVT VecVT = Op.getOperand(0).getValueType();
  SDLoc dl(Op);

  assert(ST->hasMVEIntegerOps() &&
         "LowerINSERT_VECTOR_ELT_i1 called without MVE!");

  SDValue Conv =
      DAG.getNode(ARMISD::PREDICATE_CAST, dl, MVT::i32, Op->getOperand(0));
  unsigned Lane = Op.getConstantOperandVal(1);
  unsigned LaneWidth =
      getVectorTyFromPredicateVector(VecVT).getScalarSizeInBits() / 8;
  SDValue Shift = DAG.getNode(ISD::SRL, dl, MVT::i32, Conv,
                              DAG.getConstant(Lane * LaneWidth, dl, MVT::i32));
  return Shift;
}

static SDValue LowerEXTRACT_VECTOR_ELT(SDValue Op, SelectionDAG &DAG,
                                       const ARMSubtarget *ST) {
  // EXTRACT_VECTOR_ELT is legal only for immediate indexes.
  SDValue Lane = Op.getOperand(1);
  if (!isa<ConstantSDNode>(Lane))
    return SDValue();

  SDValue Vec = Op.getOperand(0);
  EVT VT = Vec.getValueType();

  if (ST->hasMVEIntegerOps() && VT.getScalarSizeInBits() == 1)
    return LowerEXTRACT_VECTOR_ELT_i1(Op, DAG, ST);

  if (Op.getValueType() == MVT::i32 && Vec.getScalarValueSizeInBits() < 32) {
    SDLoc dl(Op);
    return DAG.getNode(ARMISD::VGETLANEu, dl, MVT::i32, Vec, Lane);
  }

  return Op;
}

static SDValue LowerCONCAT_VECTORS_i1(SDValue Op, SelectionDAG &DAG,
                                      const ARMSubtarget *ST) {
  SDLoc dl(Op);
  assert(Op.getValueType().getScalarSizeInBits() == 1 &&
         "Unexpected custom CONCAT_VECTORS lowering");
  assert(isPowerOf2_32(Op.getNumOperands()) &&
         "Unexpected custom CONCAT_VECTORS lowering");
  assert(ST->hasMVEIntegerOps() &&
         "CONCAT_VECTORS lowering only supported for MVE");

  auto ConcatPair = [&](SDValue V1, SDValue V2) {
    EVT Op1VT = V1.getValueType();
    EVT Op2VT = V2.getValueType();
    assert(Op1VT == Op2VT && "Operand types don't match!");
    assert((Op1VT == MVT::v2i1 || Op1VT == MVT::v4i1 || Op1VT == MVT::v8i1) &&
           "Unexpected i1 concat operations!");
    EVT VT = Op1VT.getDoubleNumVectorElementsVT(*DAG.getContext());

    SDValue NewV1 = PromoteMVEPredVector(dl, V1, Op1VT, DAG);
    SDValue NewV2 = PromoteMVEPredVector(dl, V2, Op2VT, DAG);

    // We now have Op1 + Op2 promoted to vectors of integers, where v8i1 gets
    // promoted to v8i16, etc.
    MVT ElType =
        getVectorTyFromPredicateVector(VT).getScalarType().getSimpleVT();
    unsigned NumElts = 2 * Op1VT.getVectorNumElements();

    EVT ConcatVT = MVT::getVectorVT(ElType, NumElts);
    if (Op1VT == MVT::v4i1 || Op1VT == MVT::v8i1) {
      // Use MVETRUNC to truncate the combined NewV1::NewV2 into the smaller
      // ConcatVT.
      SDValue ConVec =
          DAG.getNode(ARMISD::MVETRUNC, dl, ConcatVT, NewV1, NewV2);
      return DAG.getNode(ARMISD::VCMPZ, dl, VT, ConVec,
                         DAG.getConstant(ARMCC::NE, dl, MVT::i32));
    }

    // Extract the vector elements from Op1 and Op2 one by one and truncate them
    // to be the right size for the destination. For example, if Op1 is v4i1
    // then the promoted vector is v4i32. The result of concatenation gives a
    // v8i1, which when promoted is v8i16. That means each i32 element from Op1
    // needs truncating to i16 and inserting in the result.
    auto ExtractInto = [&DAG, &dl](SDValue NewV, SDValue ConVec, unsigned &j) {
      EVT NewVT = NewV.getValueType();
      EVT ConcatVT = ConVec.getValueType();
      unsigned ExtScale = 1;
      if (NewVT == MVT::v2f64) {
        NewV = DAG.getNode(ARMISD::VECTOR_REG_CAST, dl, MVT::v4i32, NewV);
        ExtScale = 2;
      }
      for (unsigned i = 0, e = NewVT.getVectorNumElements(); i < e; i++, j++) {
        SDValue Elt = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::i32, NewV,
                                  DAG.getIntPtrConstant(i * ExtScale, dl));
        ConVec = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, ConcatVT, ConVec, Elt,
                             DAG.getConstant(j, dl, MVT::i32));
      }
      return ConVec;
    };
    unsigned j = 0;
    SDValue ConVec = DAG.getNode(ISD::UNDEF, dl, ConcatVT);
    ConVec = ExtractInto(NewV1, ConVec, j);
    ConVec = ExtractInto(NewV2, ConVec, j);

    // Now return the result of comparing the subvector with zero, which will
    // generate a real predicate, i.e. v4i1, v8i1 or v16i1.
    return DAG.getNode(ARMISD::VCMPZ, dl, VT, ConVec,
                       DAG.getConstant(ARMCC::NE, dl, MVT::i32));
  };

  // Concat each pair of subvectors and pack into the lower half of the array.
  SmallVector<SDValue> ConcatOps(Op->op_begin(), Op->op_end());
  while (ConcatOps.size() > 1) {
    for (unsigned I = 0, E = ConcatOps.size(); I != E; I += 2) {
      SDValue V1 = ConcatOps[I];
      SDValue V2 = ConcatOps[I + 1];
      ConcatOps[I / 2] = ConcatPair(V1, V2);
    }
    ConcatOps.resize(ConcatOps.size() / 2);
  }
  return ConcatOps[0];
}

static SDValue LowerCONCAT_VECTORS(SDValue Op, SelectionDAG &DAG,
                                   const ARMSubtarget *ST) {
  EVT VT = Op->getValueType(0);
  if (ST->hasMVEIntegerOps() && VT.getScalarSizeInBits() == 1)
    return LowerCONCAT_VECTORS_i1(Op, DAG, ST);

  // The only time a CONCAT_VECTORS operation can have legal types is when
  // two 64-bit vectors are concatenated to a 128-bit vector.
  assert(Op.getValueType().is128BitVector() && Op.getNumOperands() == 2 &&
         "unexpected CONCAT_VECTORS");
  SDLoc dl(Op);
  SDValue Val = DAG.getUNDEF(MVT::v2f64);
  SDValue Op0 = Op.getOperand(0);
  SDValue Op1 = Op.getOperand(1);
  if (!Op0.isUndef())
    Val = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, MVT::v2f64, Val,
                      DAG.getNode(ISD::BITCAST, dl, MVT::f64, Op0),
                      DAG.getIntPtrConstant(0, dl));
  if (!Op1.isUndef())
    Val = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, MVT::v2f64, Val,
                      DAG.getNode(ISD::BITCAST, dl, MVT::f64, Op1),
                      DAG.getIntPtrConstant(1, dl));
  return DAG.getNode(ISD::BITCAST, dl, Op.getValueType(), Val);
}

static SDValue LowerEXTRACT_SUBVECTOR(SDValue Op, SelectionDAG &DAG,
                                      const ARMSubtarget *ST) {
  SDValue V1 = Op.getOperand(0);
  SDValue V2 = Op.getOperand(1);
  SDLoc dl(Op);
  EVT VT = Op.getValueType();
  EVT Op1VT = V1.getValueType();
  unsigned NumElts = VT.getVectorNumElements();
  unsigned Index = V2->getAsZExtVal();

  assert(VT.getScalarSizeInBits() == 1 &&
         "Unexpected custom EXTRACT_SUBVECTOR lowering");
  assert(ST->hasMVEIntegerOps() &&
         "EXTRACT_SUBVECTOR lowering only supported for MVE");

  SDValue NewV1 = PromoteMVEPredVector(dl, V1, Op1VT, DAG);

  // We now have Op1 promoted to a vector of integers, where v8i1 gets
  // promoted to v8i16, etc.

  MVT ElType = getVectorTyFromPredicateVector(VT).getScalarType().getSimpleVT();

  if (NumElts == 2) {
    EVT SubVT = MVT::v4i32;
    SDValue SubVec = DAG.getNode(ISD::UNDEF, dl, SubVT);
    for (unsigned i = Index, j = 0; i < (Index + NumElts); i++, j += 2) {
      SDValue Elt = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::i32, NewV1,
                                DAG.getIntPtrConstant(i, dl));
      SubVec = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, SubVT, SubVec, Elt,
                           DAG.getConstant(j, dl, MVT::i32));
      SubVec = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, SubVT, SubVec, Elt,
                           DAG.getConstant(j + 1, dl, MVT::i32));
    }
    SDValue Cmp = DAG.getNode(ARMISD::VCMPZ, dl, MVT::v4i1, SubVec,
                              DAG.getConstant(ARMCC::NE, dl, MVT::i32));
    return DAG.getNode(ARMISD::PREDICATE_CAST, dl, MVT::v2i1, Cmp);
  }

  EVT SubVT = MVT::getVectorVT(ElType, NumElts);
  SDValue SubVec = DAG.getNode(ISD::UNDEF, dl, SubVT);
  for (unsigned i = Index, j = 0; i < (Index + NumElts); i++, j++) {
    SDValue Elt = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::i32, NewV1,
                              DAG.getIntPtrConstant(i, dl));
    SubVec = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, SubVT, SubVec, Elt,
                         DAG.getConstant(j, dl, MVT::i32));
  }

  // Now return the result of comparing the subvector with zero,
  // which will generate a real predicate, i.e. v4i1, v8i1 or v16i1.
  return DAG.getNode(ARMISD::VCMPZ, dl, VT, SubVec,
                     DAG.getConstant(ARMCC::NE, dl, MVT::i32));
}

// Turn a truncate into a predicate (an i1 vector) into icmp(and(x, 1), 0).
static SDValue LowerTruncatei1(SDNode *N, SelectionDAG &DAG,
                               const ARMSubtarget *ST) {
  assert(ST->hasMVEIntegerOps() && "Expected MVE!");
  EVT VT = N->getValueType(0);
  assert((VT == MVT::v16i1 || VT == MVT::v8i1 || VT == MVT::v4i1) &&
         "Expected a vector i1 type!");
  SDValue Op = N->getOperand(0);
  EVT FromVT = Op.getValueType();
  SDLoc DL(N);

  SDValue And =
      DAG.getNode(ISD::AND, DL, FromVT, Op, DAG.getConstant(1, DL, FromVT));
  return DAG.getNode(ISD::SETCC, DL, VT, And, DAG.getConstant(0, DL, FromVT),
                     DAG.getCondCode(ISD::SETNE));
}

static SDValue LowerTruncate(SDNode *N, SelectionDAG &DAG,
                             const ARMSubtarget *Subtarget) {
  if (!Subtarget->hasMVEIntegerOps())
    return SDValue();

  EVT ToVT = N->getValueType(0);
  if (ToVT.getScalarType() == MVT::i1)
    return LowerTruncatei1(N, DAG, Subtarget);

  // MVE does not have a single instruction to perform the truncation of a v4i32
  // into the lower half of a v8i16, in the same way that a NEON vmovn would.
  // Most of the instructions in MVE follow the 'Beats' system, where moving
  // values from different lanes is usually something that the instructions
  // avoid.
  //
  // Instead it has top/bottom instructions such as VMOVLT/B and VMOVNT/B,
  // which take a the top/bottom half of a larger lane and extend it (or do the
  // opposite, truncating into the top/bottom lane from a larger lane). Note
  // that because of the way we widen lanes, a v4i16 is really a v4i32 using the
  // bottom 16bits from each vector lane. This works really well with T/B
  // instructions, but that doesn't extend to v8i32->v8i16 where the lanes need
  // to move order.
  //
  // But truncates and sext/zext are always going to be fairly common from llvm.
  // We have several options for how to deal with them:
  // - Wherever possible combine them into an instruction that makes them
  //   "free". This includes loads/stores, which can perform the trunc as part
  //   of the memory operation. Or certain shuffles that can be turned into
  //   VMOVN/VMOVL.
  // - Lane Interleaving to transform blocks surrounded by ext/trunc. So
  //   trunc(mul(sext(a), sext(b))) may become
  //   VMOVNT(VMUL(VMOVLB(a), VMOVLB(b)), VMUL(VMOVLT(a), VMOVLT(b))). (Which in
  //   this case can use VMULL). This is performed in the
  //   MVELaneInterleavingPass.
  // - Otherwise we have an option. By default we would expand the
  //   zext/sext/trunc into a series of lane extract/inserts going via GPR
  //   registers. One for each vector lane in the vector. This can obviously be
  //   very expensive.
  // - The other option is to use the fact that loads/store can extend/truncate
  //   to turn a trunc into two truncating stack stores and a stack reload. This
  //   becomes 3 back-to-back memory operations, but at least that is less than
  //   all the insert/extracts.
  //
  // In order to do the last, we convert certain trunc's into MVETRUNC, which
  // are either optimized where they can be, or eventually lowered into stack
  // stores/loads. This prevents us from splitting a v8i16 trunc into two stores
  // two early, where other instructions would be better, and stops us from
  // having to reconstruct multiple buildvector shuffles into loads/stores.
  if (ToVT != MVT::v8i16 && ToVT != MVT::v16i8)
    return SDValue();
  EVT FromVT = N->getOperand(0).getValueType();
  if (FromVT != MVT::v8i32 && FromVT != MVT::v16i16)
    return SDValue();

  SDValue Lo, Hi;
  std::tie(Lo, Hi) = DAG.SplitVectorOperand(N, 0);
  SDLoc DL(N);
  return DAG.getNode(ARMISD::MVETRUNC, DL, ToVT, Lo, Hi);
}

static SDValue LowerVectorExtend(SDNode *N, SelectionDAG &DAG,
                                 const ARMSubtarget *Subtarget) {
  if (!Subtarget->hasMVEIntegerOps())
    return SDValue();

  // See LowerTruncate above for an explanation of MVEEXT/MVETRUNC.

  EVT ToVT = N->getValueType(0);
  if (ToVT != MVT::v16i32 && ToVT != MVT::v8i32 && ToVT != MVT::v16i16)
    return SDValue();
  SDValue Op = N->getOperand(0);
  EVT FromVT = Op.getValueType();
  if (FromVT != MVT::v8i16 && FromVT != MVT::v16i8)
    return SDValue();

  SDLoc DL(N);
  EVT ExtVT = ToVT.getHalfNumVectorElementsVT(*DAG.getContext());
  if (ToVT.getScalarType() == MVT::i32 && FromVT.getScalarType() == MVT::i8)
    ExtVT = MVT::v8i16;

  unsigned Opcode =
      N->getOpcode() == ISD::SIGN_EXTEND ? ARMISD::MVESEXT : ARMISD::MVEZEXT;
  SDValue Ext = DAG.getNode(Opcode, DL, DAG.getVTList(ExtVT, ExtVT), Op);
  SDValue Ext1 = Ext.getValue(1);

  if (ToVT.getScalarType() == MVT::i32 && FromVT.getScalarType() == MVT::i8) {
    Ext = DAG.getNode(N->getOpcode(), DL, MVT::v8i32, Ext);
    Ext1 = DAG.getNode(N->getOpcode(), DL, MVT::v8i32, Ext1);
  }

  return DAG.getNode(ISD::CONCAT_VECTORS, DL, ToVT, Ext, Ext1);
}

/// isExtendedBUILD_VECTOR - Check if N is a constant BUILD_VECTOR where each
/// element has been zero/sign-extended, depending on the isSigned parameter,
/// from an integer type half its size.
static bool isExtendedBUILD_VECTOR(SDNode *N, SelectionDAG &DAG,
                                   bool isSigned) {
  // A v2i64 BUILD_VECTOR will have been legalized to a BITCAST from v4i32.
  EVT VT = N->getValueType(0);
  if (VT == MVT::v2i64 && N->getOpcode() == ISD::BITCAST) {
    SDNode *BVN = N->getOperand(0).getNode();
    if (BVN->getValueType(0) != MVT::v4i32 ||
        BVN->getOpcode() != ISD::BUILD_VECTOR)
      return false;
    unsigned LoElt = DAG.getDataLayout().isBigEndian() ? 1 : 0;
    unsigned HiElt = 1 - LoElt;
    ConstantSDNode *Lo0 = dyn_cast<ConstantSDNode>(BVN->getOperand(LoElt));
    ConstantSDNode *Hi0 = dyn_cast<ConstantSDNode>(BVN->getOperand(HiElt));
    ConstantSDNode *Lo1 = dyn_cast<ConstantSDNode>(BVN->getOperand(LoElt+2));
    ConstantSDNode *Hi1 = dyn_cast<ConstantSDNode>(BVN->getOperand(HiElt+2));
    if (!Lo0 || !Hi0 || !Lo1 || !Hi1)
      return false;
    if (isSigned) {
      if (Hi0->getSExtValue() == Lo0->getSExtValue() >> 32 &&
          Hi1->getSExtValue() == Lo1->getSExtValue() >> 32)
        return true;
    } else {
      if (Hi0->isZero() && Hi1->isZero())
        return true;
    }
    return false;
  }

  if (N->getOpcode() != ISD::BUILD_VECTOR)
    return false;

  for (unsigned i = 0, e = N->getNumOperands(); i != e; ++i) {
    SDNode *Elt = N->getOperand(i).getNode();
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Elt)) {
      unsigned EltSize = VT.getScalarSizeInBits();
      unsigned HalfSize = EltSize / 2;
      if (isSigned) {
        if (!isIntN(HalfSize, C->getSExtValue()))
          return false;
      } else {
        if (!isUIntN(HalfSize, C->getZExtValue()))
          return false;
      }
      continue;
    }
    return false;
  }

  return true;
}

/// isSignExtended - Check if a node is a vector value that is sign-extended
/// or a constant BUILD_VECTOR with sign-extended elements.
static bool isSignExtended(SDNode *N, SelectionDAG &DAG) {
  if (N->getOpcode() == ISD::SIGN_EXTEND || ISD::isSEXTLoad(N))
    return true;
  if (isExtendedBUILD_VECTOR(N, DAG, true))
    return true;
  return false;
}

/// isZeroExtended - Check if a node is a vector value that is zero-extended (or
/// any-extended) or a constant BUILD_VECTOR with zero-extended elements.
static bool isZeroExtended(SDNode *N, SelectionDAG &DAG) {
  if (N->getOpcode() == ISD::ZERO_EXTEND || N->getOpcode() == ISD::ANY_EXTEND ||
      ISD::isZEXTLoad(N))
    return true;
  if (isExtendedBUILD_VECTOR(N, DAG, false))
    return true;
  return false;
}

static EVT getExtensionTo64Bits(const EVT &OrigVT) {
  if (OrigVT.getSizeInBits() >= 64)
    return OrigVT;

  assert(OrigVT.isSimple() && "Expecting a simple value type");

  MVT::SimpleValueType OrigSimpleTy = OrigVT.getSimpleVT().SimpleTy;
  switch (OrigSimpleTy) {
  default: llvm_unreachable("Unexpected Vector Type");
  case MVT::v2i8:
  case MVT::v2i16:
     return MVT::v2i32;
  case MVT::v4i8:
    return  MVT::v4i16;
  }
}

/// AddRequiredExtensionForVMULL - Add a sign/zero extension to extend the total
/// value size to 64 bits. We need a 64-bit D register as an operand to VMULL.
/// We insert the required extension here to get the vector to fill a D register.
static SDValue AddRequiredExtensionForVMULL(SDValue N, SelectionDAG &DAG,
                                            const EVT &OrigTy,
                                            const EVT &ExtTy,
                                            unsigned ExtOpcode) {
  // The vector originally had a size of OrigTy. It was then extended to ExtTy.
  // We expect the ExtTy to be 128-bits total. If the OrigTy is less than
  // 64-bits we need to insert a new extension so that it will be 64-bits.
  assert(ExtTy.is128BitVector() && "Unexpected extension size");
  if (OrigTy.getSizeInBits() >= 64)
    return N;

  // Must extend size to at least 64 bits to be used as an operand for VMULL.
  EVT NewVT = getExtensionTo64Bits(OrigTy);

  return DAG.getNode(ExtOpcode, SDLoc(N), NewVT, N);
}

/// SkipLoadExtensionForVMULL - return a load of the original vector size that
/// does not do any sign/zero extension. If the original vector is less
/// than 64 bits, an appropriate extension will be added after the load to
/// reach a total size of 64 bits. We have to add the extension separately
/// because ARM does not have a sign/zero extending load for vectors.
static SDValue SkipLoadExtensionForVMULL(LoadSDNode *LD, SelectionDAG& DAG) {
  EVT ExtendedTy = getExtensionTo64Bits(LD->getMemoryVT());

  // The load already has the right type.
  if (ExtendedTy == LD->getMemoryVT())
    return DAG.getLoad(LD->getMemoryVT(), SDLoc(LD), LD->getChain(),
                       LD->getBasePtr(), LD->getPointerInfo(), LD->getAlign(),
                       LD->getMemOperand()->getFlags());

  // We need to create a zextload/sextload. We cannot just create a load
  // followed by a zext/zext node because LowerMUL is also run during normal
  // operation legalization where we can't create illegal types.
  return DAG.getExtLoad(LD->getExtensionType(), SDLoc(LD), ExtendedTy,
                        LD->getChain(), LD->getBasePtr(), LD->getPointerInfo(),
                        LD->getMemoryVT(), LD->getAlign(),
                        LD->getMemOperand()->getFlags());
}

/// SkipExtensionForVMULL - For a node that is a SIGN_EXTEND, ZERO_EXTEND,
/// ANY_EXTEND, extending load, or BUILD_VECTOR with extended elements, return
/// the unextended value. The unextended vector should be 64 bits so that it can
/// be used as an operand to a VMULL instruction. If the original vector size
/// before extension is less than 64 bits we add a an extension to resize
/// the vector to 64 bits.
static SDValue SkipExtensionForVMULL(SDNode *N, SelectionDAG &DAG) {
  if (N->getOpcode() == ISD::SIGN_EXTEND ||
      N->getOpcode() == ISD::ZERO_EXTEND || N->getOpcode() == ISD::ANY_EXTEND)
    return AddRequiredExtensionForVMULL(N->getOperand(0), DAG,
                                        N->getOperand(0)->getValueType(0),
                                        N->getValueType(0),
                                        N->getOpcode());

  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    assert((ISD::isSEXTLoad(LD) || ISD::isZEXTLoad(LD)) &&
           "Expected extending load");

    SDValue newLoad = SkipLoadExtensionForVMULL(LD, DAG);
    DAG.ReplaceAllUsesOfValueWith(SDValue(LD, 1), newLoad.getValue(1));
    unsigned Opcode = ISD::isSEXTLoad(LD) ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
    SDValue extLoad =
        DAG.getNode(Opcode, SDLoc(newLoad), LD->getValueType(0), newLoad);
    DAG.ReplaceAllUsesOfValueWith(SDValue(LD, 0), extLoad);

    return newLoad;
  }

  // Otherwise, the value must be a BUILD_VECTOR.  For v2i64, it will
  // have been legalized as a BITCAST from v4i32.
  if (N->getOpcode() == ISD::BITCAST) {
    SDNode *BVN = N->getOperand(0).getNode();
    assert(BVN->getOpcode() == ISD::BUILD_VECTOR &&
           BVN->getValueType(0) == MVT::v4i32 && "expected v4i32 BUILD_VECTOR");
    unsigned LowElt = DAG.getDataLayout().isBigEndian() ? 1 : 0;
    return DAG.getBuildVector(
        MVT::v2i32, SDLoc(N),
        {BVN->getOperand(LowElt), BVN->getOperand(LowElt + 2)});
  }
  // Construct a new BUILD_VECTOR with elements truncated to half the size.
  assert(N->getOpcode() == ISD::BUILD_VECTOR && "expected BUILD_VECTOR");
  EVT VT = N->getValueType(0);
  unsigned EltSize = VT.getScalarSizeInBits() / 2;
  unsigned NumElts = VT.getVectorNumElements();
  MVT TruncVT = MVT::getIntegerVT(EltSize);
  SmallVector<SDValue, 8> Ops;
  SDLoc dl(N);
  for (unsigned i = 0; i != NumElts; ++i) {
    const APInt &CInt = N->getConstantOperandAPInt(i);
    // Element types smaller than 32 bits are not legal, so use i32 elements.
    // The values are implicitly truncated so sext vs. zext doesn't matter.
    Ops.push_back(DAG.getConstant(CInt.zextOrTrunc(32), dl, MVT::i32));
  }
  return DAG.getBuildVector(MVT::getVectorVT(TruncVT, NumElts), dl, Ops);
}

static bool isAddSubSExt(SDNode *N, SelectionDAG &DAG) {
  unsigned Opcode = N->getOpcode();
  if (Opcode == ISD::ADD || Opcode == ISD::SUB) {
    SDNode *N0 = N->getOperand(0).getNode();
    SDNode *N1 = N->getOperand(1).getNode();
    return N0->hasOneUse() && N1->hasOneUse() &&
      isSignExtended(N0, DAG) && isSignExtended(N1, DAG);
  }
  return false;
}

static bool isAddSubZExt(SDNode *N, SelectionDAG &DAG) {
  unsigned Opcode = N->getOpcode();
  if (Opcode == ISD::ADD || Opcode == ISD::SUB) {
    SDNode *N0 = N->getOperand(0).getNode();
    SDNode *N1 = N->getOperand(1).getNode();
    return N0->hasOneUse() && N1->hasOneUse() &&
      isZeroExtended(N0, DAG) && isZeroExtended(N1, DAG);
  }
  return false;
}

static SDValue LowerMUL(SDValue Op, SelectionDAG &DAG) {
  // Multiplications are only custom-lowered for 128-bit vectors so that
  // VMULL can be detected.  Otherwise v2i64 multiplications are not legal.
  EVT VT = Op.getValueType();
  assert(VT.is128BitVector() && VT.isInteger() &&
         "unexpected type for custom-lowering ISD::MUL");
  SDNode *N0 = Op.getOperand(0).getNode();
  SDNode *N1 = Op.getOperand(1).getNode();
  unsigned NewOpc = 0;
  bool isMLA = false;
  bool isN0SExt = isSignExtended(N0, DAG);
  bool isN1SExt = isSignExtended(N1, DAG);
  if (isN0SExt && isN1SExt)
    NewOpc = ARMISD::VMULLs;
  else {
    bool isN0ZExt = isZeroExtended(N0, DAG);
    bool isN1ZExt = isZeroExtended(N1, DAG);
    if (isN0ZExt && isN1ZExt)
      NewOpc = ARMISD::VMULLu;
    else if (isN1SExt || isN1ZExt) {
      // Look for (s/zext A + s/zext B) * (s/zext C). We want to turn these
      // into (s/zext A * s/zext C) + (s/zext B * s/zext C)
      if (isN1SExt && isAddSubSExt(N0, DAG)) {
        NewOpc = ARMISD::VMULLs;
        isMLA = true;
      } else if (isN1ZExt && isAddSubZExt(N0, DAG)) {
        NewOpc = ARMISD::VMULLu;
        isMLA = true;
      } else if (isN0ZExt && isAddSubZExt(N1, DAG)) {
        std::swap(N0, N1);
        NewOpc = ARMISD::VMULLu;
        isMLA = true;
      }
    }

    if (!NewOpc) {
      if (VT == MVT::v2i64)
        // Fall through to expand this.  It is not legal.
        return SDValue();
      else
        // Other vector multiplications are legal.
        return Op;
    }
  }

  // Legalize to a VMULL instruction.
  SDLoc DL(Op);
  SDValue Op0;
  SDValue Op1 = SkipExtensionForVMULL(N1, DAG);
  if (!isMLA) {
    Op0 = SkipExtensionForVMULL(N0, DAG);
    assert(Op0.getValueType().is64BitVector() &&
           Op1.getValueType().is64BitVector() &&
           "unexpected types for extended operands to VMULL");
    return DAG.getNode(NewOpc, DL, VT, Op0, Op1);
  }

  // Optimizing (zext A + zext B) * C, to (VMULL A, C) + (VMULL B, C) during
  // isel lowering to take advantage of no-stall back to back vmul + vmla.
  //   vmull q0, d4, d6
  //   vmlal q0, d5, d6
  // is faster than
  //   vaddl q0, d4, d5
  //   vmovl q1, d6
  //   vmul  q0, q0, q1
  SDValue N00 = SkipExtensionForVMULL(N0->getOperand(0).getNode(), DAG);
  SDValue N01 = SkipExtensionForVMULL(N0->getOperand(1).getNode(), DAG);
  EVT Op1VT = Op1.getValueType();
  return DAG.getNode(N0->getOpcode(), DL, VT,
                     DAG.getNode(NewOpc, DL, VT,
                               DAG.getNode(ISD::BITCAST, DL, Op1VT, N00), Op1),
                     DAG.getNode(NewOpc, DL, VT,
                               DAG.getNode(ISD::BITCAST, DL, Op1VT, N01), Op1));
}

static SDValue LowerSDIV_v4i8(SDValue X, SDValue Y, const SDLoc &dl,
                              SelectionDAG &DAG) {
  // TODO: Should this propagate fast-math-flags?

  // Convert to float
  // float4 xf = vcvt_f32_s32(vmovl_s16(a.lo));
  // float4 yf = vcvt_f32_s32(vmovl_s16(b.lo));
  X = DAG.getNode(ISD::SIGN_EXTEND, dl, MVT::v4i32, X);
  Y = DAG.getNode(ISD::SIGN_EXTEND, dl, MVT::v4i32, Y);
  X = DAG.getNode(ISD::SINT_TO_FP, dl, MVT::v4f32, X);
  Y = DAG.getNode(ISD::SINT_TO_FP, dl, MVT::v4f32, Y);
  // Get reciprocal estimate.
  // float4 recip = vrecpeq_f32(yf);
  Y = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, MVT::v4f32,
                   DAG.getConstant(Intrinsic::arm_neon_vrecpe, dl, MVT::i32),
                   Y);
  // Because char has a smaller range than uchar, we can actually get away
  // without any newton steps.  This requires that we use a weird bias
  // of 0xb000, however (again, this has been exhaustively tested).
  // float4 result = as_float4(as_int4(xf*recip) + 0xb000);
  X = DAG.getNode(ISD::FMUL, dl, MVT::v4f32, X, Y);
  X = DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, X);
  Y = DAG.getConstant(0xb000, dl, MVT::v4i32);
  X = DAG.getNode(ISD::ADD, dl, MVT::v4i32, X, Y);
  X = DAG.getNode(ISD::BITCAST, dl, MVT::v4f32, X);
  // Convert back to short.
  X = DAG.getNode(ISD::FP_TO_SINT, dl, MVT::v4i32, X);
  X = DAG.getNode(ISD::TRUNCATE, dl, MVT::v4i16, X);
  return X;
}

static SDValue LowerSDIV_v4i16(SDValue N0, SDValue N1, const SDLoc &dl,
                               SelectionDAG &DAG) {
  // TODO: Should this propagate fast-math-flags?

  SDValue N2;
  // Convert to float.
  // float4 yf = vcvt_f32_s32(vmovl_s16(y));
  // float4 xf = vcvt_f32_s32(vmovl_s16(x));
  N0 = DAG.getNode(ISD::SIGN_EXTEND, dl, MVT::v4i32, N0);
  N1 = DAG.getNode(ISD::SIGN_EXTEND, dl, MVT::v4i32, N1);
  N0 = DAG.getNode(ISD::SINT_TO_FP, dl, MVT::v4f32, N0);
  N1 = DAG.getNode(ISD::SINT_TO_FP, dl, MVT::v4f32, N1);

  // Use reciprocal estimate and one refinement step.
  // float4 recip = vrecpeq_f32(yf);
  // recip *= vrecpsq_f32(yf, recip);
  N2 = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, MVT::v4f32,
                   DAG.getConstant(Intrinsic::arm_neon_vrecpe, dl, MVT::i32),
                   N1);
  N1 = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, MVT::v4f32,
                   DAG.getConstant(Intrinsic::arm_neon_vrecps, dl, MVT::i32),
                   N1, N2);
  N2 = DAG.getNode(ISD::FMUL, dl, MVT::v4f32, N1, N2);
  // Because short has a smaller range than ushort, we can actually get away
  // with only a single newton step.  This requires that we use a weird bias
  // of 89, however (again, this has been exhaustively tested).
  // float4 result = as_float4(as_int4(xf*recip) + 0x89);
  N0 = DAG.getNode(ISD::FMUL, dl, MVT::v4f32, N0, N2);
  N0 = DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, N0);
  N1 = DAG.getConstant(0x89, dl, MVT::v4i32);
  N0 = DAG.getNode(ISD::ADD, dl, MVT::v4i32, N0, N1);
  N0 = DAG.getNode(ISD::BITCAST, dl, MVT::v4f32, N0);
  // Convert back to integer and return.
  // return vmovn_s32(vcvt_s32_f32(result));
  N0 = DAG.getNode(ISD::FP_TO_SINT, dl, MVT::v4i32, N0);
  N0 = DAG.getNode(ISD::TRUNCATE, dl, MVT::v4i16, N0);
  return N0;
}

static SDValue LowerSDIV(SDValue Op, SelectionDAG &DAG,
                         const ARMSubtarget *ST) {
  EVT VT = Op.getValueType();
  assert((VT == MVT::v4i16 || VT == MVT::v8i8) &&
         "unexpected type for custom-lowering ISD::SDIV");

  SDLoc dl(Op);
  SDValue N0 = Op.getOperand(0);
  SDValue N1 = Op.getOperand(1);
  SDValue N2, N3;

  if (VT == MVT::v8i8) {
    N0 = DAG.getNode(ISD::SIGN_EXTEND, dl, MVT::v8i16, N0);
    N1 = DAG.getNode(ISD::SIGN_EXTEND, dl, MVT::v8i16, N1);

    N2 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, MVT::v4i16, N0,
                     DAG.getIntPtrConstant(4, dl));
    N3 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, MVT::v4i16, N1,
                     DAG.getIntPtrConstant(4, dl));
    N0 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, MVT::v4i16, N0,
                     DAG.getIntPtrConstant(0, dl));
    N1 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, MVT::v4i16, N1,
                     DAG.getIntPtrConstant(0, dl));

    N0 = LowerSDIV_v4i8(N0, N1, dl, DAG); // v4i16
    N2 = LowerSDIV_v4i8(N2, N3, dl, DAG); // v4i16

    N0 = DAG.getNode(ISD::CONCAT_VECTORS, dl, MVT::v8i16, N0, N2);
    N0 = LowerCONCAT_VECTORS(N0, DAG, ST);

    N0 = DAG.getNode(ISD::TRUNCATE, dl, MVT::v8i8, N0);
    return N0;
  }
  return LowerSDIV_v4i16(N0, N1, dl, DAG);
}

static SDValue LowerUDIV(SDValue Op, SelectionDAG &DAG,
                         const ARMSubtarget *ST) {
  // TODO: Should this propagate fast-math-flags?
  EVT VT = Op.getValueType();
  assert((VT == MVT::v4i16 || VT == MVT::v8i8) &&
         "unexpected type for custom-lowering ISD::UDIV");

  SDLoc dl(Op);
  SDValue N0 = Op.getOperand(0);
  SDValue N1 = Op.getOperand(1);
  SDValue N2, N3;

  if (VT == MVT::v8i8) {
    N0 = DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::v8i16, N0);
    N1 = DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::v8i16, N1);

    N2 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, MVT::v4i16, N0,
                     DAG.getIntPtrConstant(4, dl));
    N3 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, MVT::v4i16, N1,
                     DAG.getIntPtrConstant(4, dl));
    N0 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, MVT::v4i16, N0,
                     DAG.getIntPtrConstant(0, dl));
    N1 = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, MVT::v4i16, N1,
                     DAG.getIntPtrConstant(0, dl));

    N0 = LowerSDIV_v4i16(N0, N1, dl, DAG); // v4i16
    N2 = LowerSDIV_v4i16(N2, N3, dl, DAG); // v4i16

    N0 = DAG.getNode(ISD::CONCAT_VECTORS, dl, MVT::v8i16, N0, N2);
    N0 = LowerCONCAT_VECTORS(N0, DAG, ST);

    N0 = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, MVT::v8i8,
                     DAG.getConstant(Intrinsic::arm_neon_vqmovnsu, dl,
                                     MVT::i32),
                     N0);
    return N0;
  }

  // v4i16 sdiv ... Convert to float.
  // float4 yf = vcvt_f32_s32(vmovl_u16(y));
  // float4 xf = vcvt_f32_s32(vmovl_u16(x));
  N0 = DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::v4i32, N0);
  N1 = DAG.getNode(ISD::ZERO_EXTEND, dl, MVT::v4i32, N1);
  N0 = DAG.getNode(ISD::SINT_TO_FP, dl, MVT::v4f32, N0);
  SDValue BN1 = DAG.getNode(ISD::SINT_TO_FP, dl, MVT::v4f32, N1);

  // Use reciprocal estimate and two refinement steps.
  // float4 recip = vrecpeq_f32(yf);
  // recip *= vrecpsq_f32(yf, recip);
  // recip *= vrecpsq_f32(yf, recip);
  N2 = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, MVT::v4f32,
                   DAG.getConstant(Intrinsic::arm_neon_vrecpe, dl, MVT::i32),
                   BN1);
  N1 = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, MVT::v4f32,
                   DAG.getConstant(Intrinsic::arm_neon_vrecps, dl, MVT::i32),
                   BN1, N2);
  N2 = DAG.getNode(ISD::FMUL, dl, MVT::v4f32, N1, N2);
  N1 = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, MVT::v4f32,
                   DAG.getConstant(Intrinsic::arm_neon_vrecps, dl, MVT::i32),
                   BN1, N2);
  N2 = DAG.getNode(ISD::FMUL, dl, MVT::v4f32, N1, N2);
  // Simply multiplying by the reciprocal estimate can leave us a few ulps
  // too low, so we add 2 ulps (exhaustive testing shows that this is enough,
  // and that it will never cause us to return an answer too large).
  // float4 result = as_float4(as_int4(xf*recip) + 2);
  N0 = DAG.getNode(ISD::FMUL, dl, MVT::v4f32, N0, N2);
  N0 = DAG.getNode(ISD::BITCAST, dl, MVT::v4i32, N0);
  N1 = DAG.getConstant(2, dl, MVT::v4i32);
  N0 = DAG.getNode(ISD::ADD, dl, MVT::v4i32, N0, N1);
  N0 = DAG.getNode(ISD::BITCAST, dl, MVT::v4f32, N0);
  // Convert back to integer and return.
  // return vmovn_u32(vcvt_s32_f32(result));
  N0 = DAG.getNode(ISD::FP_TO_SINT, dl, MVT::v4i32, N0);
  N0 = DAG.getNode(ISD::TRUNCATE, dl, MVT::v4i16, N0);
  return N0;
}

static SDValue LowerUADDSUBO_CARRY(SDValue Op, SelectionDAG &DAG) {
  SDNode *N = Op.getNode();
  EVT VT = N->getValueType(0);
  SDVTList VTs = DAG.getVTList(VT, MVT::i32);

  SDValue Carry = Op.getOperand(2);

  SDLoc DL(Op);

  SDValue Result;
  if (Op.getOpcode() == ISD::UADDO_CARRY) {
    // This converts the boolean value carry into the carry flag.
    Carry = ConvertBooleanCarryToCarryFlag(Carry, DAG);

    // Do the addition proper using the carry flag we wanted.
    Result = DAG.getNode(ARMISD::ADDE, DL, VTs, Op.getOperand(0),
                         Op.getOperand(1), Carry);

    // Now convert the carry flag into a boolean value.
    Carry = ConvertCarryFlagToBooleanCarry(Result.getValue(1), VT, DAG);
  } else {
    // ARMISD::SUBE expects a carry not a borrow like ISD::USUBO_CARRY so we
    // have to invert the carry first.
    Carry = DAG.getNode(ISD::SUB, DL, MVT::i32,
                        DAG.getConstant(1, DL, MVT::i32), Carry);
    // This converts the boolean value carry into the carry flag.
    Carry = ConvertBooleanCarryToCarryFlag(Carry, DAG);

    // Do the subtraction proper using the carry flag we wanted.
    Result = DAG.getNode(ARMISD::SUBE, DL, VTs, Op.getOperand(0),
                         Op.getOperand(1), Carry);

    // Now convert the carry flag into a boolean value.
    Carry = ConvertCarryFlagToBooleanCarry(Result.getValue(1), VT, DAG);
    // But the carry returned by ARMISD::SUBE is not a borrow as expected
    // by ISD::USUBO_CARRY, so compute 1 - C.
    Carry = DAG.getNode(ISD::SUB, DL, MVT::i32,
                        DAG.getConstant(1, DL, MVT::i32), Carry);
  }

  // Return both values.
  return DAG.getNode(ISD::MERGE_VALUES, DL, N->getVTList(), Result, Carry);
}

SDValue ARMTargetLowering::LowerFSINCOS(SDValue Op, SelectionDAG &DAG) const {
  assert(Subtarget->isTargetDarwin());

  // For iOS, we want to call an alternative entry point: __sincos_stret,
  // return values are passed via sret.
  SDLoc dl(Op);
  SDValue Arg = Op.getOperand(0);
  EVT ArgVT = Arg.getValueType();
  Type *ArgTy = ArgVT.getTypeForEVT(*DAG.getContext());
  auto PtrVT = getPointerTy(DAG.getDataLayout());

  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  // Pair of floats / doubles used to pass the result.
  Type *RetTy = StructType::get(ArgTy, ArgTy);
  auto &DL = DAG.getDataLayout();

  ArgListTy Args;
  bool ShouldUseSRet = Subtarget->isAPCS_ABI();
  SDValue SRet;
  if (ShouldUseSRet) {
    // Create stack object for sret.
    const uint64_t ByteSize = DL.getTypeAllocSize(RetTy);
    const Align StackAlign = DL.getPrefTypeAlign(RetTy);
    int FrameIdx = MFI.CreateStackObject(ByteSize, StackAlign, false);
    SRet = DAG.getFrameIndex(FrameIdx, TLI.getPointerTy(DL));

    ArgListEntry Entry;
    Entry.Node = SRet;
    Entry.Ty = PointerType::getUnqual(RetTy->getContext());
    Entry.IsSExt = false;
    Entry.IsZExt = false;
    Entry.IsSRet = true;
    Args.push_back(Entry);
    RetTy = Type::getVoidTy(*DAG.getContext());
  }

  ArgListEntry Entry;
  Entry.Node = Arg;
  Entry.Ty = ArgTy;
  Entry.IsSExt = false;
  Entry.IsZExt = false;
  Args.push_back(Entry);

  RTLIB::Libcall LC =
      (ArgVT == MVT::f64) ? RTLIB::SINCOS_STRET_F64 : RTLIB::SINCOS_STRET_F32;
  const char *LibcallName = getLibcallName(LC);
  CallingConv::ID CC = getLibcallCallingConv(LC);
  SDValue Callee = DAG.getExternalSymbol(LibcallName, getPointerTy(DL));

  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl)
      .setChain(DAG.getEntryNode())
      .setCallee(CC, RetTy, Callee, std::move(Args))
      .setDiscardResult(ShouldUseSRet);
  std::pair<SDValue, SDValue> CallResult = LowerCallTo(CLI);

  if (!ShouldUseSRet)
    return CallResult.first;

  SDValue LoadSin =
      DAG.getLoad(ArgVT, dl, CallResult.second, SRet, MachinePointerInfo());

  // Address of cos field.
  SDValue Add = DAG.getNode(ISD::ADD, dl, PtrVT, SRet,
                            DAG.getIntPtrConstant(ArgVT.getStoreSize(), dl));
  SDValue LoadCos =
      DAG.getLoad(ArgVT, dl, LoadSin.getValue(1), Add, MachinePointerInfo());

  SDVTList Tys = DAG.getVTList(ArgVT, ArgVT);
  return DAG.getNode(ISD::MERGE_VALUES, dl, Tys,
                     LoadSin.getValue(0), LoadCos.getValue(0));
}

SDValue ARMTargetLowering::LowerWindowsDIVLibCall(SDValue Op, SelectionDAG &DAG,
                                                  bool Signed,
                                                  SDValue &Chain) const {
  EVT VT = Op.getValueType();
  assert((VT == MVT::i32 || VT == MVT::i64) &&
         "unexpected type for custom lowering DIV");
  SDLoc dl(Op);

  const auto &DL = DAG.getDataLayout();
  const auto &TLI = DAG.getTargetLoweringInfo();

  const char *Name = nullptr;
  if (Signed)
    Name = (VT == MVT::i32) ? "__rt_sdiv" : "__rt_sdiv64";
  else
    Name = (VT == MVT::i32) ? "__rt_udiv" : "__rt_udiv64";

  SDValue ES = DAG.getExternalSymbol(Name, TLI.getPointerTy(DL));

  ARMTargetLowering::ArgListTy Args;

  for (auto AI : {1, 0}) {
    ArgListEntry Arg;
    Arg.Node = Op.getOperand(AI);
    Arg.Ty = Arg.Node.getValueType().getTypeForEVT(*DAG.getContext());
    Args.push_back(Arg);
  }

  CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl)
    .setChain(Chain)
    .setCallee(CallingConv::ARM_AAPCS_VFP, VT.getTypeForEVT(*DAG.getContext()),
               ES, std::move(Args));

  return LowerCallTo(CLI).first;
}

// This is a code size optimisation: return the original SDIV node to
// DAGCombiner when we don't want to expand SDIV into a sequence of
// instructions, and an empty node otherwise which will cause the
// SDIV to be expanded in DAGCombine.
SDValue
ARMTargetLowering::BuildSDIVPow2(SDNode *N, const APInt &Divisor,
                                 SelectionDAG &DAG,
                                 SmallVectorImpl<SDNode *> &Created) const {
  // TODO: Support SREM
  if (N->getOpcode() != ISD::SDIV)
    return SDValue();

  const auto &ST = DAG.getSubtarget<ARMSubtarget>();
  const bool MinSize = ST.hasMinSize();
  const bool HasDivide = ST.isThumb() ? ST.hasDivideInThumbMode()
                                      : ST.hasDivideInARMMode();

  // Don't touch vector types; rewriting this may lead to scalarizing
  // the int divs.
  if (N->getOperand(0).getValueType().isVector())
    return SDValue();

  // Bail if MinSize is not set, and also for both ARM and Thumb mode we need
  // hwdiv support for this to be really profitable.
  if (!(MinSize && HasDivide))
    return SDValue();

  // ARM mode is a bit simpler than Thumb: we can handle large power
  // of 2 immediates with 1 mov instruction; no further checks required,
  // just return the sdiv node.
  if (!ST.isThumb())
    return SDValue(N, 0);

  // In Thumb mode, immediates larger than 128 need a wide 4-byte MOV,
  // and thus lose the code size benefits of a MOVS that requires only 2.
  // TargetTransformInfo and 'getIntImmCodeSizeCost' could be helpful here,
  // but as it's doing exactly this, it's not worth the trouble to get TTI.
  if (Divisor.sgt(128))
    return SDValue();

  return SDValue(N, 0);
}

SDValue ARMTargetLowering::LowerDIV_Windows(SDValue Op, SelectionDAG &DAG,
                                            bool Signed) const {
  assert(Op.getValueType() == MVT::i32 &&
         "unexpected type for custom lowering DIV");
  SDLoc dl(Op);

  SDValue DBZCHK = DAG.getNode(ARMISD::WIN__DBZCHK, dl, MVT::Other,
                               DAG.getEntryNode(), Op.getOperand(1));

  return LowerWindowsDIVLibCall(Op, DAG, Signed, DBZCHK);
}

static SDValue WinDBZCheckDenominator(SelectionDAG &DAG, SDNode *N, SDValue InChain) {
  SDLoc DL(N);
  SDValue Op = N->getOperand(1);
  if (N->getValueType(0) == MVT::i32)
    return DAG.getNode(ARMISD::WIN__DBZCHK, DL, MVT::Other, InChain, Op);
  SDValue Lo, Hi;
  std::tie(Lo, Hi) = DAG.SplitScalar(Op, DL, MVT::i32, MVT::i32);
  return DAG.getNode(ARMISD::WIN__DBZCHK, DL, MVT::Other, InChain,
                     DAG.getNode(ISD::OR, DL, MVT::i32, Lo, Hi));
}

void ARMTargetLowering::ExpandDIV_Windows(
    SDValue Op, SelectionDAG &DAG, bool Signed,
    SmallVectorImpl<SDValue> &Results) const {
  const auto &DL = DAG.getDataLayout();
  const auto &TLI = DAG.getTargetLoweringInfo();

  assert(Op.getValueType() == MVT::i64 &&
         "unexpected type for custom lowering DIV");
  SDLoc dl(Op);

  SDValue DBZCHK = WinDBZCheckDenominator(DAG, Op.getNode(), DAG.getEntryNode());

  SDValue Result = LowerWindowsDIVLibCall(Op, DAG, Signed, DBZCHK);

  SDValue Lower = DAG.getNode(ISD::TRUNCATE, dl, MVT::i32, Result);
  SDValue Upper = DAG.getNode(ISD::SRL, dl, MVT::i64, Result,
                              DAG.getConstant(32, dl, TLI.getPointerTy(DL)));
  Upper = DAG.getNode(ISD::TRUNCATE, dl, MVT::i32, Upper);

  Results.push_back(DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, Lower, Upper));
}

static SDValue LowerPredicateLoad(SDValue Op, SelectionDAG &DAG) {
  LoadSDNode *LD = cast<LoadSDNode>(Op.getNode());
  EVT MemVT = LD->getMemoryVT();
  assert((MemVT == MVT::v2i1 || MemVT == MVT::v4i1 || MemVT == MVT::v8i1 ||
          MemVT == MVT::v16i1) &&
         "Expected a predicate type!");
  assert(MemVT == Op.getValueType());
  assert(LD->getExtensionType() == ISD::NON_EXTLOAD &&
         "Expected a non-extending load");
  assert(LD->isUnindexed() && "Expected a unindexed load");

  // The basic MVE VLDR on a v2i1/v4i1/v8i1 actually loads the entire 16bit
  // predicate, with the "v4i1" bits spread out over the 16 bits loaded. We
  // need to make sure that 8/4/2 bits are actually loaded into the correct
  // place, which means loading the value and then shuffling the values into
  // the bottom bits of the predicate.
  // Equally, VLDR for an v16i1 will actually load 32bits (so will be incorrect
  // for BE).
  // Speaking of BE, apparently the rest of llvm will assume a reverse order to
  // a natural VMSR(load), so needs to be reversed.

  SDLoc dl(Op);
  SDValue Load = DAG.getExtLoad(
      ISD::EXTLOAD, dl, MVT::i32, LD->getChain(), LD->getBasePtr(),
      EVT::getIntegerVT(*DAG.getContext(), MemVT.getSizeInBits()),
      LD->getMemOperand());
  SDValue Val = Load;
  if (DAG.getDataLayout().isBigEndian())
    Val = DAG.getNode(ISD::SRL, dl, MVT::i32,
                      DAG.getNode(ISD::BITREVERSE, dl, MVT::i32, Load),
                      DAG.getConstant(32 - MemVT.getSizeInBits(), dl, MVT::i32));
  SDValue Pred = DAG.getNode(ARMISD::PREDICATE_CAST, dl, MVT::v16i1, Val);
  if (MemVT != MVT::v16i1)
    Pred = DAG.getNode(ISD::EXTRACT_SUBVECTOR, dl, MemVT, Pred,
                       DAG.getConstant(0, dl, MVT::i32));
  return DAG.getMergeValues({Pred, Load.getValue(1)}, dl);
}

void ARMTargetLowering::LowerLOAD(SDNode *N, SmallVectorImpl<SDValue> &Results,
                                  SelectionDAG &DAG) const {
  LoadSDNode *LD = cast<LoadSDNode>(N);
  EVT MemVT = LD->getMemoryVT();
  assert(LD->isUnindexed() && "Loads should be unindexed at this point.");

  if (MemVT == MVT::i64 && Subtarget->hasV5TEOps() &&
      !Subtarget->isThumb1Only() && LD->isVolatile() &&
      LD->getAlign() >= Subtarget->getDualLoadStoreAlignment()) {
    SDLoc dl(N);
    SDValue Result = DAG.getMemIntrinsicNode(
        ARMISD::LDRD, dl, DAG.getVTList({MVT::i32, MVT::i32, MVT::Other}),
        {LD->getChain(), LD->getBasePtr()}, MemVT, LD->getMemOperand());
    SDValue Lo = Result.getValue(DAG.getDataLayout().isLittleEndian() ? 0 : 1);
    SDValue Hi = Result.getValue(DAG.getDataLayout().isLittleEndian() ? 1 : 0);
    SDValue Pair = DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, Lo, Hi);
    Results.append({Pair, Result.getValue(2)});
  }
}

static SDValue LowerPredicateStore(SDValue Op, SelectionDAG &DAG) {
  StoreSDNode *ST = cast<StoreSDNode>(Op.getNode());
  EVT MemVT = ST->getMemoryVT();
  assert((MemVT == MVT::v2i1 || MemVT == MVT::v4i1 || MemVT == MVT::v8i1 ||
          MemVT == MVT::v16i1) &&
         "Expected a predicate type!");
  assert(MemVT == ST->getValue().getValueType());
  assert(!ST->isTruncatingStore() && "Expected a non-extending store");
  assert(ST->isUnindexed() && "Expected a unindexed store");

  // Only store the v2i1 or v4i1 or v8i1 worth of bits, via a buildvector with
  // top bits unset and a scalar store.
  SDLoc dl(Op);
  SDValue Build = ST->getValue();
  if (MemVT != MVT::v16i1) {
    SmallVector<SDValue, 16> Ops;
    for (unsigned I = 0; I < MemVT.getVectorNumElements(); I++) {
      unsigned Elt = DAG.getDataLayout().isBigEndian()
                         ? MemVT.getVectorNumElements() - I - 1
                         : I;
      Ops.push_back(DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::i32, Build,
                                DAG.getConstant(Elt, dl, MVT::i32)));
    }
    for (unsigned I = MemVT.getVectorNumElements(); I < 16; I++)
      Ops.push_back(DAG.getUNDEF(MVT::i32));
    Build = DAG.getNode(ISD::BUILD_VECTOR, dl, MVT::v16i1, Ops);
  }
  SDValue GRP = DAG.getNode(ARMISD::PREDICATE_CAST, dl, MVT::i32, Build);
  if (MemVT == MVT::v16i1 && DAG.getDataLayout().isBigEndian())
    GRP = DAG.getNode(ISD::SRL, dl, MVT::i32,
                      DAG.getNode(ISD::BITREVERSE, dl, MVT::i32, GRP),
                      DAG.getConstant(16, dl, MVT::i32));
  return DAG.getTruncStore(
      ST->getChain(), dl, GRP, ST->getBasePtr(),
      EVT::getIntegerVT(*DAG.getContext(), MemVT.getSizeInBits()),
      ST->getMemOperand());
}

static SDValue LowerSTORE(SDValue Op, SelectionDAG &DAG,
                          const ARMSubtarget *Subtarget) {
  StoreSDNode *ST = cast<StoreSDNode>(Op.getNode());
  EVT MemVT = ST->getMemoryVT();
  assert(ST->isUnindexed() && "Stores should be unindexed at this point.");

  if (MemVT == MVT::i64 && Subtarget->hasV5TEOps() &&
      !Subtarget->isThumb1Only() && ST->isVolatile() &&
      ST->getAlign() >= Subtarget->getDualLoadStoreAlignment()) {
    SDNode *N = Op.getNode();
    SDLoc dl(N);

    SDValue Lo = DAG.getNode(
        ISD::EXTRACT_ELEMENT, dl, MVT::i32, ST->getValue(),
        DAG.getTargetConstant(DAG.getDataLayout().isLittleEndian() ? 0 : 1, dl,
                              MVT::i32));
    SDValue Hi = DAG.getNode(
        ISD::EXTRACT_ELEMENT, dl, MVT::i32, ST->getValue(),
        DAG.getTargetConstant(DAG.getDataLayout().isLittleEndian() ? 1 : 0, dl,
                              MVT::i32));

    return DAG.getMemIntrinsicNode(ARMISD::STRD, dl, DAG.getVTList(MVT::Other),
                                   {ST->getChain(), Lo, Hi, ST->getBasePtr()},
                                   MemVT, ST->getMemOperand());
  } else if (Subtarget->hasMVEIntegerOps() &&
             ((MemVT == MVT::v2i1 || MemVT == MVT::v4i1 || MemVT == MVT::v8i1 ||
               MemVT == MVT::v16i1))) {
    return LowerPredicateStore(Op, DAG);
  }

  return SDValue();
}

static bool isZeroVector(SDValue N) {
  return (ISD::isBuildVectorAllZeros(N.getNode()) ||
          (N->getOpcode() == ARMISD::VMOVIMM &&
           isNullConstant(N->getOperand(0))));
}

static SDValue LowerMLOAD(SDValue Op, SelectionDAG &DAG) {
  MaskedLoadSDNode *N = cast<MaskedLoadSDNode>(Op.getNode());
  MVT VT = Op.getSimpleValueType();
  SDValue Mask = N->getMask();
  SDValue PassThru = N->getPassThru();
  SDLoc dl(Op);

  if (isZeroVector(PassThru))
    return Op;

  // MVE Masked loads use zero as the passthru value. Here we convert undef to
  // zero too, and other values are lowered to a select.
  SDValue ZeroVec = DAG.getNode(ARMISD::VMOVIMM, dl, VT,
                                DAG.getTargetConstant(0, dl, MVT::i32));
  SDValue NewLoad = DAG.getMaskedLoad(
      VT, dl, N->getChain(), N->getBasePtr(), N->getOffset(), Mask, ZeroVec,
      N->getMemoryVT(), N->getMemOperand(), N->getAddressingMode(),
      N->getExtensionType(), N->isExpandingLoad());
  SDValue Combo = NewLoad;
  bool PassThruIsCastZero = (PassThru.getOpcode() == ISD::BITCAST ||
                             PassThru.getOpcode() == ARMISD::VECTOR_REG_CAST) &&
                            isZeroVector(PassThru->getOperand(0));
  if (!PassThru.isUndef() && !PassThruIsCastZero)
    Combo = DAG.getNode(ISD::VSELECT, dl, VT, Mask, NewLoad, PassThru);
  return DAG.getMergeValues({Combo, NewLoad.getValue(1)}, dl);
}

static SDValue LowerVecReduce(SDValue Op, SelectionDAG &DAG,
                              const ARMSubtarget *ST) {
  if (!ST->hasMVEIntegerOps())
    return SDValue();

  SDLoc dl(Op);
  unsigned BaseOpcode = 0;
  switch (Op->getOpcode()) {
  default: llvm_unreachable("Expected VECREDUCE opcode");
  case ISD::VECREDUCE_FADD: BaseOpcode = ISD::FADD; break;
  case ISD::VECREDUCE_FMUL: BaseOpcode = ISD::FMUL; break;
  case ISD::VECREDUCE_MUL:  BaseOpcode = ISD::MUL; break;
  case ISD::VECREDUCE_AND:  BaseOpcode = ISD::AND; break;
  case ISD::VECREDUCE_OR:   BaseOpcode = ISD::OR; break;
  case ISD::VECREDUCE_XOR:  BaseOpcode = ISD::XOR; break;
  case ISD::VECREDUCE_FMAX: BaseOpcode = ISD::FMAXNUM; break;
  case ISD::VECREDUCE_FMIN: BaseOpcode = ISD::FMINNUM; break;
  }

  SDValue Op0 = Op->getOperand(0);
  EVT VT = Op0.getValueType();
  EVT EltVT = VT.getVectorElementType();
  unsigned NumElts = VT.getVectorNumElements();
  unsigned NumActiveLanes = NumElts;

  assert((NumActiveLanes == 16 || NumActiveLanes == 8 || NumActiveLanes == 4 ||
          NumActiveLanes == 2) &&
         "Only expected a power 2 vector size");

  // Use Mul(X, Rev(X)) until 4 items remain. Going down to 4 vector elements
  // allows us to easily extract vector elements from the lanes.
  while (NumActiveLanes > 4) {
    unsigned RevOpcode = NumActiveLanes == 16 ? ARMISD::VREV16 : ARMISD::VREV32;
    SDValue Rev = DAG.getNode(RevOpcode, dl, VT, Op0);
    Op0 = DAG.getNode(BaseOpcode, dl, VT, Op0, Rev);
    NumActiveLanes /= 2;
  }

  SDValue Res;
  if (NumActiveLanes == 4) {
    // The remaining 4 elements are summed sequentially
    SDValue Ext0 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, Op0,
                              DAG.getConstant(0 * NumElts / 4, dl, MVT::i32));
    SDValue Ext1 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, Op0,
                              DAG.getConstant(1 * NumElts / 4, dl, MVT::i32));
    SDValue Ext2 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, Op0,
                              DAG.getConstant(2 * NumElts / 4, dl, MVT::i32));
    SDValue Ext3 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, Op0,
                              DAG.getConstant(3 * NumElts / 4, dl, MVT::i32));
    SDValue Res0 = DAG.getNode(BaseOpcode, dl, EltVT, Ext0, Ext1, Op->getFlags());
    SDValue Res1 = DAG.getNode(BaseOpcode, dl, EltVT, Ext2, Ext3, Op->getFlags());
    Res = DAG.getNode(BaseOpcode, dl, EltVT, Res0, Res1, Op->getFlags());
  } else {
    SDValue Ext0 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, Op0,
                              DAG.getConstant(0, dl, MVT::i32));
    SDValue Ext1 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, Op0,
                              DAG.getConstant(1, dl, MVT::i32));
    Res = DAG.getNode(BaseOpcode, dl, EltVT, Ext0, Ext1, Op->getFlags());
  }

  // Result type may be wider than element type.
  if (EltVT != Op->getValueType(0))
    Res = DAG.getNode(ISD::ANY_EXTEND, dl, Op->getValueType(0), Res);
  return Res;
}

static SDValue LowerVecReduceF(SDValue Op, SelectionDAG &DAG,
                               const ARMSubtarget *ST) {
  if (!ST->hasMVEFloatOps())
    return SDValue();
  return LowerVecReduce(Op, DAG, ST);
}

static SDValue LowerVecReduceMinMax(SDValue Op, SelectionDAG &DAG,
                                    const ARMSubtarget *ST) {
  if (!ST->hasNEON())
    return SDValue();

  SDLoc dl(Op);
  SDValue Op0 = Op->getOperand(0);
  EVT VT = Op0.getValueType();
  EVT EltVT = VT.getVectorElementType();

  unsigned PairwiseIntrinsic = 0;
  switch (Op->getOpcode()) {
  default:
    llvm_unreachable("Expected VECREDUCE opcode");
  case ISD::VECREDUCE_UMIN:
    PairwiseIntrinsic = Intrinsic::arm_neon_vpminu;
    break;
  case ISD::VECREDUCE_UMAX:
    PairwiseIntrinsic = Intrinsic::arm_neon_vpmaxu;
    break;
  case ISD::VECREDUCE_SMIN:
    PairwiseIntrinsic = Intrinsic::arm_neon_vpmins;
    break;
  case ISD::VECREDUCE_SMAX:
    PairwiseIntrinsic = Intrinsic::arm_neon_vpmaxs;
    break;
  }
  SDValue PairwiseOp = DAG.getConstant(PairwiseIntrinsic, dl, MVT::i32);

  unsigned NumElts = VT.getVectorNumElements();
  unsigned NumActiveLanes = NumElts;

  assert((NumActiveLanes == 16 || NumActiveLanes == 8 || NumActiveLanes == 4 ||
          NumActiveLanes == 2) &&
         "Only expected a power 2 vector size");

  // Split 128-bit vectors, since vpmin/max takes 2 64-bit vectors.
  if (VT.is128BitVector()) {
    SDValue Lo, Hi;
    std::tie(Lo, Hi) = DAG.SplitVector(Op0, dl);
    VT = Lo.getValueType();
    Op0 = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, VT, {PairwiseOp, Lo, Hi});
    NumActiveLanes /= 2;
  }

  // Use pairwise reductions until one lane remains
  while (NumActiveLanes > 1) {
    Op0 = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, VT, {PairwiseOp, Op0, Op0});
    NumActiveLanes /= 2;
  }

  SDValue Res = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, EltVT, Op0,
                            DAG.getConstant(0, dl, MVT::i32));

  // Result type may be wider than element type.
  if (EltVT != Op.getValueType()) {
    unsigned Extend = 0;
    switch (Op->getOpcode()) {
    default:
      llvm_unreachable("Expected VECREDUCE opcode");
    case ISD::VECREDUCE_UMIN:
    case ISD::VECREDUCE_UMAX:
      Extend = ISD::ZERO_EXTEND;
      break;
    case ISD::VECREDUCE_SMIN:
    case ISD::VECREDUCE_SMAX:
      Extend = ISD::SIGN_EXTEND;
      break;
    }
    Res = DAG.getNode(Extend, dl, Op.getValueType(), Res);
  }
  return Res;
}

static SDValue LowerAtomicLoadStore(SDValue Op, SelectionDAG &DAG) {
  if (isStrongerThanMonotonic(cast<AtomicSDNode>(Op)->getSuccessOrdering()))
    // Acquire/Release load/store is not legal for targets without a dmb or
    // equivalent available.
    return SDValue();

  // Monotonic load/store is legal for all targets.
  return Op;
}

static void ReplaceREADCYCLECOUNTER(SDNode *N,
                                    SmallVectorImpl<SDValue> &Results,
                                    SelectionDAG &DAG,
                                    const ARMSubtarget *Subtarget) {
  SDLoc DL(N);
  // Under Power Management extensions, the cycle-count is:
  //    mrc p15, #0, <Rt>, c9, c13, #0
  SDValue Ops[] = { N->getOperand(0), // Chain
                    DAG.getTargetConstant(Intrinsic::arm_mrc, DL, MVT::i32),
                    DAG.getTargetConstant(15, DL, MVT::i32),
                    DAG.getTargetConstant(0, DL, MVT::i32),
                    DAG.getTargetConstant(9, DL, MVT::i32),
                    DAG.getTargetConstant(13, DL, MVT::i32),
                    DAG.getTargetConstant(0, DL, MVT::i32)
  };

  SDValue Cycles32 = DAG.getNode(ISD::INTRINSIC_W_CHAIN, DL,
                                 DAG.getVTList(MVT::i32, MVT::Other), Ops);
  Results.push_back(DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i64, Cycles32,
                                DAG.getConstant(0, DL, MVT::i32)));
  Results.push_back(Cycles32.getValue(1));
}

static SDValue createGPRPairNode(SelectionDAG &DAG, SDValue V) {
  SDLoc dl(V.getNode());
  auto [VLo, VHi] = DAG.SplitScalar(V, dl, MVT::i32, MVT::i32);
  bool isBigEndian = DAG.getDataLayout().isBigEndian();
  if (isBigEndian)
    std::swap (VLo, VHi);
  SDValue RegClass =
      DAG.getTargetConstant(ARM::GPRPairRegClassID, dl, MVT::i32);
  SDValue SubReg0 = DAG.getTargetConstant(ARM::gsub_0, dl, MVT::i32);
  SDValue SubReg1 = DAG.getTargetConstant(ARM::gsub_1, dl, MVT::i32);
  const SDValue Ops[] = { RegClass, VLo, SubReg0, VHi, SubReg1 };
  return SDValue(
      DAG.getMachineNode(TargetOpcode::REG_SEQUENCE, dl, MVT::Untyped, Ops), 0);
}

static void ReplaceCMP_SWAP_64Results(SDNode *N,
                                       SmallVectorImpl<SDValue> & Results,
                                       SelectionDAG &DAG) {
  assert(N->getValueType(0) == MVT::i64 &&
         "AtomicCmpSwap on types less than 64 should be legal");
  SDValue Ops[] = {N->getOperand(1),
                   createGPRPairNode(DAG, N->getOperand(2)),
                   createGPRPairNode(DAG, N->getOperand(3)),
                   N->getOperand(0)};
  SDNode *CmpSwap = DAG.getMachineNode(
      ARM::CMP_SWAP_64, SDLoc(N),
      DAG.getVTList(MVT::Untyped, MVT::i32, MVT::Other), Ops);

  MachineMemOperand *MemOp = cast<MemSDNode>(N)->getMemOperand();
  DAG.setNodeMemRefs(cast<MachineSDNode>(CmpSwap), {MemOp});

  bool isBigEndian = DAG.getDataLayout().isBigEndian();

  SDValue Lo =
      DAG.getTargetExtractSubreg(isBigEndian ? ARM::gsub_1 : ARM::gsub_0,
                                 SDLoc(N), MVT::i32, SDValue(CmpSwap, 0));
  SDValue Hi =
      DAG.getTargetExtractSubreg(isBigEndian ? ARM::gsub_0 : ARM::gsub_1,
                                 SDLoc(N), MVT::i32, SDValue(CmpSwap, 0));
  Results.push_back(DAG.getNode(ISD::BUILD_PAIR, SDLoc(N), MVT::i64, Lo, Hi));
  Results.push_back(SDValue(CmpSwap, 2));
}

SDValue ARMTargetLowering::LowerFSETCC(SDValue Op, SelectionDAG &DAG) const {
  SDLoc dl(Op);
  EVT VT = Op.getValueType();
  SDValue Chain = Op.getOperand(0);
  SDValue LHS = Op.getOperand(1);
  SDValue RHS = Op.getOperand(2);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(3))->get();
  bool IsSignaling = Op.getOpcode() == ISD::STRICT_FSETCCS;

  // If we don't have instructions of this float type then soften to a libcall
  // and use SETCC instead.
  if (isUnsupportedFloatingType(LHS.getValueType())) {
    DAG.getTargetLoweringInfo().softenSetCCOperands(
      DAG, LHS.getValueType(), LHS, RHS, CC, dl, LHS, RHS, Chain, IsSignaling);
    if (!RHS.getNode()) {
      RHS = DAG.getConstant(0, dl, LHS.getValueType());
      CC = ISD::SETNE;
    }
    SDValue Result = DAG.getNode(ISD::SETCC, dl, VT, LHS, RHS,
                                 DAG.getCondCode(CC));
    return DAG.getMergeValues({Result, Chain}, dl);
  }

  ARMCC::CondCodes CondCode, CondCode2;
  FPCCToARMCC(CC, CondCode, CondCode2);

  // FIXME: Chain is not handled correctly here. Currently the FPSCR is implicit
  // in CMPFP and CMPFPE, but instead it should be made explicit by these
  // instructions using a chain instead of glue. This would also fix the problem
  // here (and also in LowerSELECT_CC) where we generate two comparisons when
  // CondCode2 != AL.
  SDValue True = DAG.getConstant(1, dl, VT);
  SDValue False =  DAG.getConstant(0, dl, VT);
  SDValue ARMcc = DAG.getConstant(CondCode, dl, MVT::i32);
  SDValue CCR = DAG.getRegister(ARM::CPSR, MVT::i32);
  SDValue Cmp = getVFPCmp(LHS, RHS, DAG, dl, IsSignaling);
  SDValue Result = getCMOV(dl, VT, False, True, ARMcc, CCR, Cmp, DAG);
  if (CondCode2 != ARMCC::AL) {
    ARMcc = DAG.getConstant(CondCode2, dl, MVT::i32);
    Cmp = getVFPCmp(LHS, RHS, DAG, dl, IsSignaling);
    Result = getCMOV(dl, VT, Result, True, ARMcc, CCR, Cmp, DAG);
  }
  return DAG.getMergeValues({Result, Chain}, dl);
}

SDValue ARMTargetLowering::LowerSPONENTRY(SDValue Op, SelectionDAG &DAG) const {
  MachineFrameInfo &MFI = DAG.getMachineFunction().getFrameInfo();

  EVT VT = getPointerTy(DAG.getDataLayout());
  SDLoc DL(Op);
  int FI = MFI.CreateFixedObject(4, 0, false);
  return DAG.getFrameIndex(FI, VT);
}

SDValue ARMTargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  LLVM_DEBUG(dbgs() << "Lowering node: "; Op.dump());
  switch (Op.getOpcode()) {
  default: llvm_unreachable("Don't know how to custom lower this!");
  case ISD::WRITE_REGISTER: return LowerWRITE_REGISTER(Op, DAG);
  case ISD::ConstantPool: return LowerConstantPool(Op, DAG);
  case ISD::BlockAddress:  return LowerBlockAddress(Op, DAG);
  case ISD::GlobalAddress: return LowerGlobalAddress(Op, DAG);
  case ISD::GlobalTLSAddress: return LowerGlobalTLSAddress(Op, DAG);
  case ISD::SELECT:        return LowerSELECT(Op, DAG);
  case ISD::SELECT_CC:     return LowerSELECT_CC(Op, DAG);
  case ISD::BRCOND:        return LowerBRCOND(Op, DAG);
  case ISD::BR_CC:         return LowerBR_CC(Op, DAG);
  case ISD::BR_JT:         return LowerBR_JT(Op, DAG);
  case ISD::VASTART:       return LowerVASTART(Op, DAG);
  case ISD::ATOMIC_FENCE:  return LowerATOMIC_FENCE(Op, DAG, Subtarget);
  case ISD::PREFETCH:      return LowerPREFETCH(Op, DAG, Subtarget);
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:    return LowerINT_TO_FP(Op, DAG);
  case ISD::STRICT_FP_TO_SINT:
  case ISD::STRICT_FP_TO_UINT:
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:    return LowerFP_TO_INT(Op, DAG);
  case ISD::FP_TO_SINT_SAT:
  case ISD::FP_TO_UINT_SAT: return LowerFP_TO_INT_SAT(Op, DAG, Subtarget);
  case ISD::FCOPYSIGN:     return LowerFCOPYSIGN(Op, DAG);
  case ISD::RETURNADDR:    return LowerRETURNADDR(Op, DAG);
  case ISD::FRAMEADDR:     return LowerFRAMEADDR(Op, DAG);
  case ISD::EH_SJLJ_SETJMP: return LowerEH_SJLJ_SETJMP(Op, DAG);
  case ISD::EH_SJLJ_LONGJMP: return LowerEH_SJLJ_LONGJMP(Op, DAG);
  case ISD::EH_SJLJ_SETUP_DISPATCH: return LowerEH_SJLJ_SETUP_DISPATCH(Op, DAG);
  case ISD::INTRINSIC_VOID: return LowerINTRINSIC_VOID(Op, DAG, Subtarget);
  case ISD::INTRINSIC_WO_CHAIN: return LowerINTRINSIC_WO_CHAIN(Op, DAG,
                                                               Subtarget);
  case ISD::BITCAST:       return ExpandBITCAST(Op.getNode(), DAG, Subtarget);
  case ISD::SHL:
  case ISD::SRL:
  case ISD::SRA:           return LowerShift(Op.getNode(), DAG, Subtarget);
  case ISD::SREM:          return LowerREM(Op.getNode(), DAG);
  case ISD::UREM:          return LowerREM(Op.getNode(), DAG);
  case ISD::SHL_PARTS:     return LowerShiftLeftParts(Op, DAG);
  case ISD::SRL_PARTS:
  case ISD::SRA_PARTS:     return LowerShiftRightParts(Op, DAG);
  case ISD::CTTZ:
  case ISD::CTTZ_ZERO_UNDEF: return LowerCTTZ(Op.getNode(), DAG, Subtarget);
  case ISD::CTPOP:         return LowerCTPOP(Op.getNode(), DAG, Subtarget);
  case ISD::SETCC:         return LowerVSETCC(Op, DAG, Subtarget);
  case ISD::SETCCCARRY:    return LowerSETCCCARRY(Op, DAG);
  case ISD::ConstantFP:    return LowerConstantFP(Op, DAG, Subtarget);
  case ISD::BUILD_VECTOR:  return LowerBUILD_VECTOR(Op, DAG, Subtarget);
  case ISD::VECTOR_SHUFFLE: return LowerVECTOR_SHUFFLE(Op, DAG, Subtarget);
  case ISD::EXTRACT_SUBVECTOR: return LowerEXTRACT_SUBVECTOR(Op, DAG, Subtarget);
  case ISD::INSERT_VECTOR_ELT: return LowerINSERT_VECTOR_ELT(Op, DAG);
  case ISD::EXTRACT_VECTOR_ELT: return LowerEXTRACT_VECTOR_ELT(Op, DAG, Subtarget);
  case ISD::CONCAT_VECTORS: return LowerCONCAT_VECTORS(Op, DAG, Subtarget);
  case ISD::TRUNCATE:      return LowerTruncate(Op.getNode(), DAG, Subtarget);
  case ISD::SIGN_EXTEND:
  case ISD::ZERO_EXTEND:   return LowerVectorExtend(Op.getNode(), DAG, Subtarget);
  case ISD::GET_ROUNDING:  return LowerGET_ROUNDING(Op, DAG);
  case ISD::SET_ROUNDING:  return LowerSET_ROUNDING(Op, DAG);
  case ISD::SET_FPMODE:
    return LowerSET_FPMODE(Op, DAG);
  case ISD::RESET_FPMODE:
    return LowerRESET_FPMODE(Op, DAG);
  case ISD::MUL:           return LowerMUL(Op, DAG);
  case ISD::SDIV:
    if (Subtarget->isTargetWindows() && !Op.getValueType().isVector())
      return LowerDIV_Windows(Op, DAG, /* Signed */ true);
    return LowerSDIV(Op, DAG, Subtarget);
  case ISD::UDIV:
    if (Subtarget->isTargetWindows() && !Op.getValueType().isVector())
      return LowerDIV_Windows(Op, DAG, /* Signed */ false);
    return LowerUDIV(Op, DAG, Subtarget);
  case ISD::UADDO_CARRY:
  case ISD::USUBO_CARRY:
    return LowerUADDSUBO_CARRY(Op, DAG);
  case ISD::SADDO:
  case ISD::SSUBO:
    return LowerSignedALUO(Op, DAG);
  case ISD::UADDO:
  case ISD::USUBO:
    return LowerUnsignedALUO(Op, DAG);
  case ISD::SADDSAT:
  case ISD::SSUBSAT:
  case ISD::UADDSAT:
  case ISD::USUBSAT:
    return LowerADDSUBSAT(Op, DAG, Subtarget);
  case ISD::LOAD:
    return LowerPredicateLoad(Op, DAG);
  case ISD::STORE:
    return LowerSTORE(Op, DAG, Subtarget);
  case ISD::MLOAD:
    return LowerMLOAD(Op, DAG);
  case ISD::VECREDUCE_MUL:
  case ISD::VECREDUCE_AND:
  case ISD::VECREDUCE_OR:
  case ISD::VECREDUCE_XOR:
    return LowerVecReduce(Op, DAG, Subtarget);
  case ISD::VECREDUCE_FADD:
  case ISD::VECREDUCE_FMUL:
  case ISD::VECREDUCE_FMIN:
  case ISD::VECREDUCE_FMAX:
    return LowerVecReduceF(Op, DAG, Subtarget);
  case ISD::VECREDUCE_UMIN:
  case ISD::VECREDUCE_UMAX:
  case ISD::VECREDUCE_SMIN:
  case ISD::VECREDUCE_SMAX:
    return LowerVecReduceMinMax(Op, DAG, Subtarget);
  case ISD::ATOMIC_LOAD:
  case ISD::ATOMIC_STORE:  return LowerAtomicLoadStore(Op, DAG);
  case ISD::FSINCOS:       return LowerFSINCOS(Op, DAG);
  case ISD::SDIVREM:
  case ISD::UDIVREM:       return LowerDivRem(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC:
    if (Subtarget->isTargetWindows())
      return LowerDYNAMIC_STACKALLOC(Op, DAG);
    llvm_unreachable("Don't know how to custom lower this!");
  case ISD::STRICT_FP_ROUND:
  case ISD::FP_ROUND: return LowerFP_ROUND(Op, DAG);
  case ISD::STRICT_FP_EXTEND:
  case ISD::FP_EXTEND: return LowerFP_EXTEND(Op, DAG);
  case ISD::STRICT_FSETCC:
  case ISD::STRICT_FSETCCS: return LowerFSETCC(Op, DAG);
  case ISD::SPONENTRY:
    return LowerSPONENTRY(Op, DAG);
  case ARMISD::WIN__DBZCHK: return SDValue();
  }
}

static void ReplaceLongIntrinsic(SDNode *N, SmallVectorImpl<SDValue> &Results,
                                 SelectionDAG &DAG) {
  unsigned IntNo = N->getConstantOperandVal(0);
  unsigned Opc = 0;
  if (IntNo == Intrinsic::arm_smlald)
    Opc = ARMISD::SMLALD;
  else if (IntNo == Intrinsic::arm_smlaldx)
    Opc = ARMISD::SMLALDX;
  else if (IntNo == Intrinsic::arm_smlsld)
    Opc = ARMISD::SMLSLD;
  else if (IntNo == Intrinsic::arm_smlsldx)
    Opc = ARMISD::SMLSLDX;
  else
    return;

  SDLoc dl(N);
  SDValue Lo, Hi;
  std::tie(Lo, Hi) = DAG.SplitScalar(N->getOperand(3), dl, MVT::i32, MVT::i32);

  SDValue LongMul = DAG.getNode(Opc, dl,
                                DAG.getVTList(MVT::i32, MVT::i32),
                                N->getOperand(1), N->getOperand(2),
                                Lo, Hi);
  Results.push_back(DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64,
                                LongMul.getValue(0), LongMul.getValue(1)));
}

/// ReplaceNodeResults - Replace the results of node with an illegal result
/// type with new values built out of custom code.
void ARMTargetLowering::ReplaceNodeResults(SDNode *N,
                                           SmallVectorImpl<SDValue> &Results,
                                           SelectionDAG &DAG) const {
  SDValue Res;
  switch (N->getOpcode()) {
  default:
    llvm_unreachable("Don't know how to custom expand this!");
  case ISD::READ_REGISTER:
    ExpandREAD_REGISTER(N, Results, DAG);
    break;
  case ISD::BITCAST:
    Res = ExpandBITCAST(N, DAG, Subtarget);
    break;
  case ISD::SRL:
  case ISD::SRA:
  case ISD::SHL:
    Res = Expand64BitShift(N, DAG, Subtarget);
    break;
  case ISD::SREM:
  case ISD::UREM:
    Res = LowerREM(N, DAG);
    break;
  case ISD::SDIVREM:
  case ISD::UDIVREM:
    Res = LowerDivRem(SDValue(N, 0), DAG);
    assert(Res.getNumOperands() == 2 && "DivRem needs two values");
    Results.push_back(Res.getValue(0));
    Results.push_back(Res.getValue(1));
    return;
  case ISD::SADDSAT:
  case ISD::SSUBSAT:
  case ISD::UADDSAT:
  case ISD::USUBSAT:
    Res = LowerADDSUBSAT(SDValue(N, 0), DAG, Subtarget);
    break;
  case ISD::READCYCLECOUNTER:
    ReplaceREADCYCLECOUNTER(N, Results, DAG, Subtarget);
    return;
  case ISD::UDIV:
  case ISD::SDIV:
    assert(Subtarget->isTargetWindows() && "can only expand DIV on Windows");
    return ExpandDIV_Windows(SDValue(N, 0), DAG, N->getOpcode() == ISD::SDIV,
                             Results);
  case ISD::ATOMIC_CMP_SWAP:
    ReplaceCMP_SWAP_64Results(N, Results, DAG);
    return;
  case ISD::INTRINSIC_WO_CHAIN:
    return ReplaceLongIntrinsic(N, Results, DAG);
  case ISD::LOAD:
    LowerLOAD(N, Results, DAG);
    break;
  case ISD::TRUNCATE:
    Res = LowerTruncate(N, DAG, Subtarget);
    break;
  case ISD::SIGN_EXTEND:
  case ISD::ZERO_EXTEND:
    Res = LowerVectorExtend(N, DAG, Subtarget);
    break;
  case ISD::FP_TO_SINT_SAT:
  case ISD::FP_TO_UINT_SAT:
    Res = LowerFP_TO_INT_SAT(SDValue(N, 0), DAG, Subtarget);
    break;
  }
  if (Res.getNode())
    Results.push_back(Res);
}

//===----------------------------------------------------------------------===//
//                           ARM Scheduler Hooks
//===----------------------------------------------------------------------===//

/// SetupEntryBlockForSjLj - Insert code into the entry block that creates and
/// registers the function context.
void ARMTargetLowering::SetupEntryBlockForSjLj(MachineInstr &MI,
                                               MachineBasicBlock *MBB,
                                               MachineBasicBlock *DispatchBB,
                                               int FI) const {
  assert(!Subtarget->isROPI() && !Subtarget->isRWPI() &&
         "ROPI/RWPI not currently supported with SjLj");
  const TargetInstrInfo *TII = Subtarget->getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();
  MachineFunction *MF = MBB->getParent();
  MachineRegisterInfo *MRI = &MF->getRegInfo();
  MachineConstantPool *MCP = MF->getConstantPool();
  ARMFunctionInfo *AFI = MF->getInfo<ARMFunctionInfo>();
  const Function &F = MF->getFunction();

  bool isThumb = Subtarget->isThumb();
  bool isThumb2 = Subtarget->isThumb2();

  unsigned PCLabelId = AFI->createPICLabelUId();
  unsigned PCAdj = (isThumb || isThumb2) ? 4 : 8;
  ARMConstantPoolValue *CPV =
    ARMConstantPoolMBB::Create(F.getContext(), DispatchBB, PCLabelId, PCAdj);
  unsigned CPI = MCP->getConstantPoolIndex(CPV, Align(4));

  const TargetRegisterClass *TRC = isThumb ? &ARM::tGPRRegClass
                                           : &ARM::GPRRegClass;

  // Grab constant pool and fixed stack memory operands.
  MachineMemOperand *CPMMO =
      MF->getMachineMemOperand(MachinePointerInfo::getConstantPool(*MF),
                               MachineMemOperand::MOLoad, 4, Align(4));

  MachineMemOperand *FIMMOSt =
      MF->getMachineMemOperand(MachinePointerInfo::getFixedStack(*MF, FI),
                               MachineMemOperand::MOStore, 4, Align(4));

  // Load the address of the dispatch MBB into the jump buffer.
  if (isThumb2) {
    // Incoming value: jbuf
    //   ldr.n  r5, LCPI1_1
    //   orr    r5, r5, #1
    //   add    r5, pc
    //   str    r5, [$jbuf, #+4] ; &jbuf[1]
    Register NewVReg1 = MRI->createVirtualRegister(TRC);
    BuildMI(*MBB, MI, dl, TII->get(ARM::t2LDRpci), NewVReg1)
        .addConstantPoolIndex(CPI)
        .addMemOperand(CPMMO)
        .add(predOps(ARMCC::AL));
    // Set the low bit because of thumb mode.
    Register NewVReg2 = MRI->createVirtualRegister(TRC);
    BuildMI(*MBB, MI, dl, TII->get(ARM::t2ORRri), NewVReg2)
        .addReg(NewVReg1, RegState::Kill)
        .addImm(0x01)
        .add(predOps(ARMCC::AL))
        .add(condCodeOp());
    Register NewVReg3 = MRI->createVirtualRegister(TRC);
    BuildMI(*MBB, MI, dl, TII->get(ARM::tPICADD), NewVReg3)
      .addReg(NewVReg2, RegState::Kill)
      .addImm(PCLabelId);
    BuildMI(*MBB, MI, dl, TII->get(ARM::t2STRi12))
        .addReg(NewVReg3, RegState::Kill)
        .addFrameIndex(FI)
        .addImm(36) // &jbuf[1] :: pc
        .addMemOperand(FIMMOSt)
        .add(predOps(ARMCC::AL));
  } else if (isThumb) {
    // Incoming value: jbuf
    //   ldr.n  r1, LCPI1_4
    //   add    r1, pc
    //   mov    r2, #1
    //   orrs   r1, r2
    //   add    r2, $jbuf, #+4 ; &jbuf[1]
    //   str    r1, [r2]
    Register NewVReg1 = MRI->createVirtualRegister(TRC);
    BuildMI(*MBB, MI, dl, TII->get(ARM::tLDRpci), NewVReg1)
        .addConstantPoolIndex(CPI)
        .addMemOperand(CPMMO)
        .add(predOps(ARMCC::AL));
    Register NewVReg2 = MRI->createVirtualRegister(TRC);
    BuildMI(*MBB, MI, dl, TII->get(ARM::tPICADD), NewVReg2)
      .addReg(NewVReg1, RegState::Kill)
      .addImm(PCLabelId);
    // Set the low bit because of thumb mode.
    Register NewVReg3 = MRI->createVirtualRegister(TRC);
    BuildMI(*MBB, MI, dl, TII->get(ARM::tMOVi8), NewVReg3)
        .addReg(ARM::CPSR, RegState::Define)
        .addImm(1)
        .add(predOps(ARMCC::AL));
    Register NewVReg4 = MRI->createVirtualRegister(TRC);
    BuildMI(*MBB, MI, dl, TII->get(ARM::tORR), NewVReg4)
        .addReg(ARM::CPSR, RegState::Define)
        .addReg(NewVReg2, RegState::Kill)
        .addReg(NewVReg3, RegState::Kill)
        .add(predOps(ARMCC::AL));
    Register NewVReg5 = MRI->createVirtualRegister(TRC);
    BuildMI(*MBB, MI, dl, TII->get(ARM::tADDframe), NewVReg5)
            .addFrameIndex(FI)
            .addImm(36); // &jbuf[1] :: pc
    BuildMI(*MBB, MI, dl, TII->get(ARM::tSTRi))
        .addReg(NewVReg4, RegState::Kill)
        .addReg(NewVReg5, RegState::Kill)
        .addImm(0)
        .addMemOperand(FIMMOSt)
        .add(predOps(ARMCC::AL));
  } else {
    // Incoming value: jbuf
    //   ldr  r1, LCPI1_1
    //   add  r1, pc, r1
    //   str  r1, [$jbuf, #+4] ; &jbuf[1]
    Register NewVReg1 = MRI->createVirtualRegister(TRC);
    BuildMI(*MBB, MI, dl, TII->get(ARM::LDRi12), NewVReg1)
        .addConstantPoolIndex(CPI)
        .addImm(0)
        .addMemOperand(CPMMO)
        .add(predOps(ARMCC::AL));
    Register NewVReg2 = MRI->createVirtualRegister(TRC);
    BuildMI(*MBB, MI, dl, TII->get(ARM::PICADD), NewVReg2)
        .addReg(NewVReg1, RegState::Kill)
        .addImm(PCLabelId)
        .add(predOps(ARMCC::AL));
    BuildMI(*MBB, MI, dl, TII->get(ARM::STRi12))
        .addReg(NewVReg2, RegState::Kill)
        .addFrameIndex(FI)
        .addImm(36) // &jbuf[1] :: pc
        .addMemOperand(FIMMOSt)
        .add(predOps(ARMCC::AL));
  }
}

void ARMTargetLowering::EmitSjLjDispatchBlock(MachineInstr &MI,
                                              MachineBasicBlock *MBB) const {
  const TargetInstrInfo *TII = Subtarget->getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();
  MachineFunction *MF = MBB->getParent();
  MachineRegisterInfo *MRI = &MF->getRegInfo();
  MachineFrameInfo &MFI = MF->getFrameInfo();
  int FI = MFI.getFunctionContextIndex();

  const TargetRegisterClass *TRC = Subtarget->isThumb() ? &ARM::tGPRRegClass
                                                        : &ARM::GPRnopcRegClass;

  // Get a mapping of the call site numbers to all of the landing pads they're
  // associated with.
  DenseMap<unsigned, SmallVector<MachineBasicBlock*, 2>> CallSiteNumToLPad;
  unsigned MaxCSNum = 0;
  for (MachineBasicBlock &BB : *MF) {
    if (!BB.isEHPad())
      continue;

    // FIXME: We should assert that the EH_LABEL is the first MI in the landing
    // pad.
    for (MachineInstr &II : BB) {
      if (!II.isEHLabel())
        continue;

      MCSymbol *Sym = II.getOperand(0).getMCSymbol();
      if (!MF->hasCallSiteLandingPad(Sym)) continue;

      SmallVectorImpl<unsigned> &CallSiteIdxs = MF->getCallSiteLandingPad(Sym);
      for (unsigned Idx : CallSiteIdxs) {
        CallSiteNumToLPad[Idx].push_back(&BB);
        MaxCSNum = std::max(MaxCSNum, Idx);
      }
      break;
    }
  }

  // Get an ordered list of the machine basic blocks for the jump table.
  std::vector<MachineBasicBlock*> LPadList;
  SmallPtrSet<MachineBasicBlock*, 32> InvokeBBs;
  LPadList.reserve(CallSiteNumToLPad.size());
  for (unsigned I = 1; I <= MaxCSNum; ++I) {
    SmallVectorImpl<MachineBasicBlock*> &MBBList = CallSiteNumToLPad[I];
    for (MachineBasicBlock *MBB : MBBList) {
      LPadList.push_back(MBB);
      InvokeBBs.insert(MBB->pred_begin(), MBB->pred_end());
    }
  }

  assert(!LPadList.empty() &&
         "No landing pad destinations for the dispatch jump table!");

  // Create the jump table and associated information.
  MachineJumpTableInfo *JTI =
    MF->getOrCreateJumpTableInfo(MachineJumpTableInfo::EK_Inline);
  unsigned MJTI = JTI->createJumpTableIndex(LPadList);

  // Create the MBBs for the dispatch code.

  // Shove the dispatch's address into the return slot in the function context.
  MachineBasicBlock *DispatchBB = MF->CreateMachineBasicBlock();
  DispatchBB->setIsEHPad();

  MachineBasicBlock *TrapBB = MF->CreateMachineBasicBlock();
  unsigned trap_opcode;
  if (Subtarget->isThumb())
    trap_opcode = ARM::tTRAP;
  else
    trap_opcode = Subtarget->useNaClTrap() ? ARM::TRAPNaCl : ARM::TRAP;

  BuildMI(TrapBB, dl, TII->get(trap_opcode));
  DispatchBB->addSuccessor(TrapBB);

  MachineBasicBlock *DispContBB = MF->CreateMachineBasicBlock();
  DispatchBB->addSuccessor(DispContBB);

  // Insert and MBBs.
  MF->insert(MF->end(), DispatchBB);
  MF->insert(MF->end(), DispContBB);
  MF->insert(MF->end(), TrapBB);

  // Insert code into the entry block that creates and registers the function
  // context.
  SetupEntryBlockForSjLj(MI, MBB, DispatchBB, FI);

  MachineMemOperand *FIMMOLd = MF->getMachineMemOperand(
      MachinePointerInfo::getFixedStack(*MF, FI),
      MachineMemOperand::MOLoad | MachineMemOperand::MOVolatile, 4, Align(4));

  MachineInstrBuilder MIB;
  MIB = BuildMI(DispatchBB, dl, TII->get(ARM::Int_eh_sjlj_dispatchsetup));

  const ARMBaseInstrInfo *AII = static_cast<const ARMBaseInstrInfo*>(TII);
  const ARMBaseRegisterInfo &RI = AII->getRegisterInfo();

  // Add a register mask with no preserved registers.  This results in all
  // registers being marked as clobbered. This can't work if the dispatch block
  // is in a Thumb1 function and is linked with ARM code which uses the FP
  // registers, as there is no way to preserve the FP registers in Thumb1 mode.
  MIB.addRegMask(RI.getSjLjDispatchPreservedMask(*MF));

  bool IsPositionIndependent = isPositionIndependent();
  unsigned NumLPads = LPadList.size();
  if (Subtarget->isThumb2()) {
    Register NewVReg1 = MRI->createVirtualRegister(TRC);
    BuildMI(DispatchBB, dl, TII->get(ARM::t2LDRi12), NewVReg1)
        .addFrameIndex(FI)
        .addImm(4)
        .addMemOperand(FIMMOLd)
        .add(predOps(ARMCC::AL));

    if (NumLPads < 256) {
      BuildMI(DispatchBB, dl, TII->get(ARM::t2CMPri))
          .addReg(NewVReg1)
          .addImm(LPadList.size())
          .add(predOps(ARMCC::AL));
    } else {
      Register VReg1 = MRI->createVirtualRegister(TRC);
      BuildMI(DispatchBB, dl, TII->get(ARM::t2MOVi16), VReg1)
          .addImm(NumLPads & 0xFFFF)
          .add(predOps(ARMCC::AL));

      unsigned VReg2 = VReg1;
      if ((NumLPads & 0xFFFF0000) != 0) {
        VReg2 = MRI->createVirtualRegister(TRC);
        BuildMI(DispatchBB, dl, TII->get(ARM::t2MOVTi16), VReg2)
            .addReg(VReg1)
            .addImm(NumLPads >> 16)
            .add(predOps(ARMCC::AL));
      }

      BuildMI(DispatchBB, dl, TII->get(ARM::t2CMPrr))
          .addReg(NewVReg1)
          .addReg(VReg2)
          .add(predOps(ARMCC::AL));
    }

    BuildMI(DispatchBB, dl, TII->get(ARM::t2Bcc))
      .addMBB(TrapBB)
      .addImm(ARMCC::HI)
      .addReg(ARM::CPSR);

    Register NewVReg3 = MRI->createVirtualRegister(TRC);
    BuildMI(DispContBB, dl, TII->get(ARM::t2LEApcrelJT), NewVReg3)
        .addJumpTableIndex(MJTI)
        .add(predOps(ARMCC::AL));

    Register NewVReg4 = MRI->createVirtualRegister(TRC);
    BuildMI(DispContBB, dl, TII->get(ARM::t2ADDrs), NewVReg4)
        .addReg(NewVReg3, RegState::Kill)
        .addReg(NewVReg1)
        .addImm(ARM_AM::getSORegOpc(ARM_AM::lsl, 2))
        .add(predOps(ARMCC::AL))
        .add(condCodeOp());

    BuildMI(DispContBB, dl, TII->get(ARM::t2BR_JT))
      .addReg(NewVReg4, RegState::Kill)
      .addReg(NewVReg1)
      .addJumpTableIndex(MJTI);
  } else if (Subtarget->isThumb()) {
    Register NewVReg1 = MRI->createVirtualRegister(TRC);
    BuildMI(DispatchBB, dl, TII->get(ARM::tLDRspi), NewVReg1)
        .addFrameIndex(FI)
        .addImm(1)
        .addMemOperand(FIMMOLd)
        .add(predOps(ARMCC::AL));

    if (NumLPads < 256) {
      BuildMI(DispatchBB, dl, TII->get(ARM::tCMPi8))
          .addReg(NewVReg1)
          .addImm(NumLPads)
          .add(predOps(ARMCC::AL));
    } else {
      MachineConstantPool *ConstantPool = MF->getConstantPool();
      Type *Int32Ty = Type::getInt32Ty(MF->getFunction().getContext());
      const Constant *C = ConstantInt::get(Int32Ty, NumLPads);

      // MachineConstantPool wants an explicit alignment.
      Align Alignment = MF->getDataLayout().getPrefTypeAlign(Int32Ty);
      unsigned Idx = ConstantPool->getConstantPoolIndex(C, Alignment);

      Register VReg1 = MRI->createVirtualRegister(TRC);
      BuildMI(DispatchBB, dl, TII->get(ARM::tLDRpci))
          .addReg(VReg1, RegState::Define)
          .addConstantPoolIndex(Idx)
          .add(predOps(ARMCC::AL));
      BuildMI(DispatchBB, dl, TII->get(ARM::tCMPr))
          .addReg(NewVReg1)
          .addReg(VReg1)
          .add(predOps(ARMCC::AL));
    }

    BuildMI(DispatchBB, dl, TII->get(ARM::tBcc))
      .addMBB(TrapBB)
      .addImm(ARMCC::HI)
      .addReg(ARM::CPSR);

    Register NewVReg2 = MRI->createVirtualRegister(TRC);
    BuildMI(DispContBB, dl, TII->get(ARM::tLSLri), NewVReg2)
        .addReg(ARM::CPSR, RegState::Define)
        .addReg(NewVReg1)
        .addImm(2)
        .add(predOps(ARMCC::AL));

    Register NewVReg3 = MRI->createVirtualRegister(TRC);
    BuildMI(DispContBB, dl, TII->get(ARM::tLEApcrelJT), NewVReg3)
        .addJumpTableIndex(MJTI)
        .add(predOps(ARMCC::AL));

    Register NewVReg4 = MRI->createVirtualRegister(TRC);
    BuildMI(DispContBB, dl, TII->get(ARM::tADDrr), NewVReg4)
        .addReg(ARM::CPSR, RegState::Define)
        .addReg(NewVReg2, RegState::Kill)
        .addReg(NewVReg3)
        .add(predOps(ARMCC::AL));

    MachineMemOperand *JTMMOLd =
        MF->getMachineMemOperand(MachinePointerInfo::getJumpTable(*MF),
                                 MachineMemOperand::MOLoad, 4, Align(4));

    Register NewVReg5 = MRI->createVirtualRegister(TRC);
    BuildMI(DispContBB, dl, TII->get(ARM::tLDRi), NewVReg5)
        .addReg(NewVReg4, RegState::Kill)
        .addImm(0)
        .addMemOperand(JTMMOLd)
        .add(predOps(ARMCC::AL));

    unsigned NewVReg6 = NewVReg5;
    if (IsPositionIndependent) {
      NewVReg6 = MRI->createVirtualRegister(TRC);
      BuildMI(DispContBB, dl, TII->get(ARM::tADDrr), NewVReg6)
          .addReg(ARM::CPSR, RegState::Define)
          .addReg(NewVReg5, RegState::Kill)
          .addReg(NewVReg3)
          .add(predOps(ARMCC::AL));
    }

    BuildMI(DispContBB, dl, TII->get(ARM::tBR_JTr))
      .addReg(NewVReg6, RegState::Kill)
      .addJumpTableIndex(MJTI);
  } else {
    Register NewVReg1 = MRI->createVirtualRegister(TRC);
    BuildMI(DispatchBB, dl, TII->get(ARM::LDRi12), NewVReg1)
        .addFrameIndex(FI)
        .addImm(4)
        .addMemOperand(FIMMOLd)
        .add(predOps(ARMCC::AL));

    if (NumLPads < 256) {
      BuildMI(DispatchBB, dl, TII->get(ARM::CMPri))
          .addReg(NewVReg1)
          .addImm(NumLPads)
          .add(predOps(ARMCC::AL));
    } else if (Subtarget->hasV6T2Ops() && isUInt<16>(NumLPads)) {
      Register VReg1 = MRI->createVirtualRegister(TRC);
      BuildMI(DispatchBB, dl, TII->get(ARM::MOVi16), VReg1)
          .addImm(NumLPads & 0xFFFF)
          .add(predOps(ARMCC::AL));

      unsigned VReg2 = VReg1;
      if ((NumLPads & 0xFFFF0000) != 0) {
        VReg2 = MRI->createVirtualRegister(TRC);
        BuildMI(DispatchBB, dl, TII->get(ARM::MOVTi16), VReg2)
            .addReg(VReg1)
            .addImm(NumLPads >> 16)
            .add(predOps(ARMCC::AL));
      }

      BuildMI(DispatchBB, dl, TII->get(ARM::CMPrr))
          .addReg(NewVReg1)
          .addReg(VReg2)
          .add(predOps(ARMCC::AL));
    } else {
      MachineConstantPool *ConstantPool = MF->getConstantPool();
      Type *Int32Ty = Type::getInt32Ty(MF->getFunction().getContext());
      const Constant *C = ConstantInt::get(Int32Ty, NumLPads);

      // MachineConstantPool wants an explicit alignment.
      Align Alignment = MF->getDataLayout().getPrefTypeAlign(Int32Ty);
      unsigned Idx = ConstantPool->getConstantPoolIndex(C, Alignment);

      Register VReg1 = MRI->createVirtualRegister(TRC);
      BuildMI(DispatchBB, dl, TII->get(ARM::LDRcp))
          .addReg(VReg1, RegState::Define)
          .addConstantPoolIndex(Idx)
          .addImm(0)
          .add(predOps(ARMCC::AL));
      BuildMI(DispatchBB, dl, TII->get(ARM::CMPrr))
          .addReg(NewVReg1)
          .addReg(VReg1, RegState::Kill)
          .add(predOps(ARMCC::AL));
    }

    BuildMI(DispatchBB, dl, TII->get(ARM::Bcc))
      .addMBB(TrapBB)
      .addImm(ARMCC::HI)
      .addReg(ARM::CPSR);

    Register NewVReg3 = MRI->createVirtualRegister(TRC);
    BuildMI(DispContBB, dl, TII->get(ARM::MOVsi), NewVReg3)
        .addReg(NewVReg1)
        .addImm(ARM_AM::getSORegOpc(ARM_AM::lsl, 2))
        .add(predOps(ARMCC::AL))
        .add(condCodeOp());
    Register NewVReg4 = MRI->createVirtualRegister(TRC);
    BuildMI(DispContBB, dl, TII->get(ARM::LEApcrelJT), NewVReg4)
        .addJumpTableIndex(MJTI)
        .add(predOps(ARMCC::AL));

    MachineMemOperand *JTMMOLd =
        MF->getMachineMemOperand(MachinePointerInfo::getJumpTable(*MF),
                                 MachineMemOperand::MOLoad, 4, Align(4));
    Register NewVReg5 = MRI->createVirtualRegister(TRC);
    BuildMI(DispContBB, dl, TII->get(ARM::LDRrs), NewVReg5)
        .addReg(NewVReg3, RegState::Kill)
        .addReg(NewVReg4)
        .addImm(0)
        .addMemOperand(JTMMOLd)
        .add(predOps(ARMCC::AL));

    if (IsPositionIndependent) {
      BuildMI(DispContBB, dl, TII->get(ARM::BR_JTadd))
        .addReg(NewVReg5, RegState::Kill)
        .addReg(NewVReg4)
        .addJumpTableIndex(MJTI);
    } else {
      BuildMI(DispContBB, dl, TII->get(ARM::BR_JTr))
        .addReg(NewVReg5, RegState::Kill)
        .addJumpTableIndex(MJTI);
    }
  }

  // Add the jump table entries as successors to the MBB.
  SmallPtrSet<MachineBasicBlock*, 8> SeenMBBs;
  for (MachineBasicBlock *CurMBB : LPadList) {
    if (SeenMBBs.insert(CurMBB).second)
      DispContBB->addSuccessor(CurMBB);
  }

  // N.B. the order the invoke BBs are processed in doesn't matter here.
  const MCPhysReg *SavedRegs = RI.getCalleeSavedRegs(MF);
  SmallVector<MachineBasicBlock*, 64> MBBLPads;
  for (MachineBasicBlock *BB : InvokeBBs) {

    // Remove the landing pad successor from the invoke block and replace it
    // with the new dispatch block.
    SmallVector<MachineBasicBlock*, 4> Successors(BB->successors());
    while (!Successors.empty()) {
      MachineBasicBlock *SMBB = Successors.pop_back_val();
      if (SMBB->isEHPad()) {
        BB->removeSuccessor(SMBB);
        MBBLPads.push_back(SMBB);
      }
    }

    BB->addSuccessor(DispatchBB, BranchProbability::getZero());
    BB->normalizeSuccProbs();

    // Find the invoke call and mark all of the callee-saved registers as
    // 'implicit defined' so that they're spilled. This prevents code from
    // moving instructions to before the EH block, where they will never be
    // executed.
    for (MachineBasicBlock::reverse_iterator
           II = BB->rbegin(), IE = BB->rend(); II != IE; ++II) {
      if (!II->isCall()) continue;

      DenseMap<unsigned, bool> DefRegs;
      for (MachineInstr::mop_iterator
             OI = II->operands_begin(), OE = II->operands_end();
           OI != OE; ++OI) {
        if (!OI->isReg()) continue;
        DefRegs[OI->getReg()] = true;
      }

      MachineInstrBuilder MIB(*MF, &*II);

      for (unsigned i = 0; SavedRegs[i] != 0; ++i) {
        unsigned Reg = SavedRegs[i];
        if (Subtarget->isThumb2() &&
            !ARM::tGPRRegClass.contains(Reg) &&
            !ARM::hGPRRegClass.contains(Reg))
          continue;
        if (Subtarget->isThumb1Only() && !ARM::tGPRRegClass.contains(Reg))
          continue;
        if (!Subtarget->isThumb() && !ARM::GPRRegClass.contains(Reg))
          continue;
        if (!DefRegs[Reg])
          MIB.addReg(Reg, RegState::ImplicitDefine | RegState::Dead);
      }

      break;
    }
  }

  // Mark all former landing pads as non-landing pads. The dispatch is the only
  // landing pad now.
  for (MachineBasicBlock *MBBLPad : MBBLPads)
    MBBLPad->setIsEHPad(false);

  // The instruction is gone now.
  MI.eraseFromParent();
}

static
MachineBasicBlock *OtherSucc(MachineBasicBlock *MBB, MachineBasicBlock *Succ) {
  for (MachineBasicBlock *S : MBB->successors())
    if (S != Succ)
      return S;
  llvm_unreachable("Expecting a BB with two successors!");
}

/// Return the load opcode for a given load size. If load size >= 8,
/// neon opcode will be returned.
static unsigned getLdOpcode(unsigned LdSize, bool IsThumb1, bool IsThumb2) {
  if (LdSize >= 8)
    return LdSize == 16 ? ARM::VLD1q32wb_fixed
                        : LdSize == 8 ? ARM::VLD1d32wb_fixed : 0;
  if (IsThumb1)
    return LdSize == 4 ? ARM::tLDRi
                       : LdSize == 2 ? ARM::tLDRHi
                                     : LdSize == 1 ? ARM::tLDRBi : 0;
  if (IsThumb2)
    return LdSize == 4 ? ARM::t2LDR_POST
                       : LdSize == 2 ? ARM::t2LDRH_POST
                                     : LdSize == 1 ? ARM::t2LDRB_POST : 0;
  return LdSize == 4 ? ARM::LDR_POST_IMM
                     : LdSize == 2 ? ARM::LDRH_POST
                                   : LdSize == 1 ? ARM::LDRB_POST_IMM : 0;
}

/// Return the store opcode for a given store size. If store size >= 8,
/// neon opcode will be returned.
static unsigned getStOpcode(unsigned StSize, bool IsThumb1, bool IsThumb2) {
  if (StSize >= 8)
    return StSize == 16 ? ARM::VST1q32wb_fixed
                        : StSize == 8 ? ARM::VST1d32wb_fixed : 0;
  if (IsThumb1)
    return StSize == 4 ? ARM::tSTRi
                       : StSize == 2 ? ARM::tSTRHi
                                     : StSize == 1 ? ARM::tSTRBi : 0;
  if (IsThumb2)
    return StSize == 4 ? ARM::t2STR_POST
                       : StSize == 2 ? ARM::t2STRH_POST
                                     : StSize == 1 ? ARM::t2STRB_POST : 0;
  return StSize == 4 ? ARM::STR_POST_IMM
                     : StSize == 2 ? ARM::STRH_POST
                                   : StSize == 1 ? ARM::STRB_POST_IMM : 0;
}

/// Emit a post-increment load operation with given size. The instructions
/// will be added to BB at Pos.
static void emitPostLd(MachineBasicBlock *BB, MachineBasicBlock::iterator Pos,
                       const TargetInstrInfo *TII, const DebugLoc &dl,
                       unsigned LdSize, unsigned Data, unsigned AddrIn,
                       unsigned AddrOut, bool IsThumb1, bool IsThumb2) {
  unsigned LdOpc = getLdOpcode(LdSize, IsThumb1, IsThumb2);
  assert(LdOpc != 0 && "Should have a load opcode");
  if (LdSize >= 8) {
    BuildMI(*BB, Pos, dl, TII->get(LdOpc), Data)
        .addReg(AddrOut, RegState::Define)
        .addReg(AddrIn)
        .addImm(0)
        .add(predOps(ARMCC::AL));
  } else if (IsThumb1) {
    // load + update AddrIn
    BuildMI(*BB, Pos, dl, TII->get(LdOpc), Data)
        .addReg(AddrIn)
        .addImm(0)
        .add(predOps(ARMCC::AL));
    BuildMI(*BB, Pos, dl, TII->get(ARM::tADDi8), AddrOut)
        .add(t1CondCodeOp())
        .addReg(AddrIn)
        .addImm(LdSize)
        .add(predOps(ARMCC::AL));
  } else if (IsThumb2) {
    BuildMI(*BB, Pos, dl, TII->get(LdOpc), Data)
        .addReg(AddrOut, RegState::Define)
        .addReg(AddrIn)
        .addImm(LdSize)
        .add(predOps(ARMCC::AL));
  } else { // arm
    BuildMI(*BB, Pos, dl, TII->get(LdOpc), Data)
        .addReg(AddrOut, RegState::Define)
        .addReg(AddrIn)
        .addReg(0)
        .addImm(LdSize)
        .add(predOps(ARMCC::AL));
  }
}

/// Emit a post-increment store operation with given size. The instructions
/// will be added to BB at Pos.
static void emitPostSt(MachineBasicBlock *BB, MachineBasicBlock::iterator Pos,
                       const TargetInstrInfo *TII, const DebugLoc &dl,
                       unsigned StSize, unsigned Data, unsigned AddrIn,
                       unsigned AddrOut, bool IsThumb1, bool IsThumb2) {
  unsigned StOpc = getStOpcode(StSize, IsThumb1, IsThumb2);
  assert(StOpc != 0 && "Should have a store opcode");
  if (StSize >= 8) {
    BuildMI(*BB, Pos, dl, TII->get(StOpc), AddrOut)
        .addReg(AddrIn)
        .addImm(0)
        .addReg(Data)
        .add(predOps(ARMCC::AL));
  } else if (IsThumb1) {
    // store + update AddrIn
    BuildMI(*BB, Pos, dl, TII->get(StOpc))
        .addReg(Data)
        .addReg(AddrIn)
        .addImm(0)
        .add(predOps(ARMCC::AL));
    BuildMI(*BB, Pos, dl, TII->get(ARM::tADDi8), AddrOut)
        .add(t1CondCodeOp())
        .addReg(AddrIn)
        .addImm(StSize)
        .add(predOps(ARMCC::AL));
  } else if (IsThumb2) {
    BuildMI(*BB, Pos, dl, TII->get(StOpc), AddrOut)
        .addReg(Data)
        .addReg(AddrIn)
        .addImm(StSize)
        .add(predOps(ARMCC::AL));
  } else { // arm
    BuildMI(*BB, Pos, dl, TII->get(StOpc), AddrOut)
        .addReg(Data)
        .addReg(AddrIn)
        .addReg(0)
        .addImm(StSize)
        .add(predOps(ARMCC::AL));
  }
}

MachineBasicBlock *
ARMTargetLowering::EmitStructByval(MachineInstr &MI,
                                   MachineBasicBlock *BB) const {
  // This pseudo instruction has 3 operands: dst, src, size
  // We expand it to a loop if size > Subtarget->getMaxInlineSizeThreshold().
  // Otherwise, we will generate unrolled scalar copies.
  const TargetInstrInfo *TII = Subtarget->getInstrInfo();
  const BasicBlock *LLVM_BB = BB->getBasicBlock();
  MachineFunction::iterator It = ++BB->getIterator();

  Register dest = MI.getOperand(0).getReg();
  Register src = MI.getOperand(1).getReg();
  unsigned SizeVal = MI.getOperand(2).getImm();
  unsigned Alignment = MI.getOperand(3).getImm();
  DebugLoc dl = MI.getDebugLoc();

  MachineFunction *MF = BB->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  unsigned UnitSize = 0;
  const TargetRegisterClass *TRC = nullptr;
  const TargetRegisterClass *VecTRC = nullptr;

  bool IsThumb1 = Subtarget->isThumb1Only();
  bool IsThumb2 = Subtarget->isThumb2();
  bool IsThumb = Subtarget->isThumb();

  if (Alignment & 1) {
    UnitSize = 1;
  } else if (Alignment & 2) {
    UnitSize = 2;
  } else {
    // Check whether we can use NEON instructions.
    if (!MF->getFunction().hasFnAttribute(Attribute::NoImplicitFloat) &&
        Subtarget->hasNEON()) {
      if ((Alignment % 16 == 0) && SizeVal >= 16)
        UnitSize = 16;
      else if ((Alignment % 8 == 0) && SizeVal >= 8)
        UnitSize = 8;
    }
    // Can't use NEON instructions.
    if (UnitSize == 0)
      UnitSize = 4;
  }

  // Select the correct opcode and register class for unit size load/store
  bool IsNeon = UnitSize >= 8;
  TRC = IsThumb ? &ARM::tGPRRegClass : &ARM::GPRRegClass;
  if (IsNeon)
    VecTRC = UnitSize == 16 ? &ARM::DPairRegClass
                            : UnitSize == 8 ? &ARM::DPRRegClass
                                            : nullptr;

  unsigned BytesLeft = SizeVal % UnitSize;
  unsigned LoopSize = SizeVal - BytesLeft;

  if (SizeVal <= Subtarget->getMaxInlineSizeThreshold()) {
    // Use LDR and STR to copy.
    // [scratch, srcOut] = LDR_POST(srcIn, UnitSize)
    // [destOut] = STR_POST(scratch, destIn, UnitSize)
    unsigned srcIn = src;
    unsigned destIn = dest;
    for (unsigned i = 0; i < LoopSize; i+=UnitSize) {
      Register srcOut = MRI.createVirtualRegister(TRC);
      Register destOut = MRI.createVirtualRegister(TRC);
      Register scratch = MRI.createVirtualRegister(IsNeon ? VecTRC : TRC);
      emitPostLd(BB, MI, TII, dl, UnitSize, scratch, srcIn, srcOut,
                 IsThumb1, IsThumb2);
      emitPostSt(BB, MI, TII, dl, UnitSize, scratch, destIn, destOut,
                 IsThumb1, IsThumb2);
      srcIn = srcOut;
      destIn = destOut;
    }

    // Handle the leftover bytes with LDRB and STRB.
    // [scratch, srcOut] = LDRB_POST(srcIn, 1)
    // [destOut] = STRB_POST(scratch, destIn, 1)
    for (unsigned i = 0; i < BytesLeft; i++) {
      Register srcOut = MRI.createVirtualRegister(TRC);
      Register destOut = MRI.createVirtualRegister(TRC);
      Register scratch = MRI.createVirtualRegister(TRC);
      emitPostLd(BB, MI, TII, dl, 1, scratch, srcIn, srcOut,
                 IsThumb1, IsThumb2);
      emitPostSt(BB, MI, TII, dl, 1, scratch, destIn, destOut,
                 IsThumb1, IsThumb2);
      srcIn = srcOut;
      destIn = destOut;
    }
    MI.eraseFromParent(); // The instruction is gone now.
    return BB;
  }

  // Expand the pseudo op to a loop.
  // thisMBB:
  //   ...
  //   movw varEnd, # --> with thumb2
  //   movt varEnd, #
  //   ldrcp varEnd, idx --> without thumb2
  //   fallthrough --> loopMBB
  // loopMBB:
  //   PHI varPhi, varEnd, varLoop
  //   PHI srcPhi, src, srcLoop
  //   PHI destPhi, dst, destLoop
  //   [scratch, srcLoop] = LDR_POST(srcPhi, UnitSize)
  //   [destLoop] = STR_POST(scratch, destPhi, UnitSize)
  //   subs varLoop, varPhi, #UnitSize
  //   bne loopMBB
  //   fallthrough --> exitMBB
  // exitMBB:
  //   epilogue to handle left-over bytes
  //   [scratch, srcOut] = LDRB_POST(srcLoop, 1)
  //   [destOut] = STRB_POST(scratch, destLoop, 1)
  MachineBasicBlock *loopMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MachineBasicBlock *exitMBB = MF->CreateMachineBasicBlock(LLVM_BB);
  MF->insert(It, loopMBB);
  MF->insert(It, exitMBB);

  // Set the call frame size on entry to the new basic blocks.
  unsigned CallFrameSize = TII->getCallFrameSizeAt(MI);
  loopMBB->setCallFrameSize(CallFrameSize);
  exitMBB->setCallFrameSize(CallFrameSize);

  // Transfer the remainder of BB and its successor edges to exitMBB.
  exitMBB->splice(exitMBB->begin(), BB,
                  std::next(MachineBasicBlock::iterator(MI)), BB->end());
  exitMBB->transferSuccessorsAndUpdatePHIs(BB);

  // Load an immediate to varEnd.
  Register varEnd = MRI.createVirtualRegister(TRC);
  if (Subtarget->useMovt()) {
    BuildMI(BB, dl, TII->get(IsThumb ? ARM::t2MOVi32imm : ARM::MOVi32imm),
            varEnd)
        .addImm(LoopSize);
  } else if (Subtarget->genExecuteOnly()) {
    assert(IsThumb && "Non-thumb expected to have used movt");
    BuildMI(BB, dl, TII->get(ARM::tMOVi32imm), varEnd).addImm(LoopSize);
  } else {
    MachineConstantPool *ConstantPool = MF->getConstantPool();
    Type *Int32Ty = Type::getInt32Ty(MF->getFunction().getContext());
    const Constant *C = ConstantInt::get(Int32Ty, LoopSize);

    // MachineConstantPool wants an explicit alignment.
    Align Alignment = MF->getDataLayout().getPrefTypeAlign(Int32Ty);
    unsigned Idx = ConstantPool->getConstantPoolIndex(C, Alignment);
    MachineMemOperand *CPMMO =
        MF->getMachineMemOperand(MachinePointerInfo::getConstantPool(*MF),
                                 MachineMemOperand::MOLoad, 4, Align(4));

    if (IsThumb)
      BuildMI(*BB, MI, dl, TII->get(ARM::tLDRpci))
          .addReg(varEnd, RegState::Define)
          .addConstantPoolIndex(Idx)
          .add(predOps(ARMCC::AL))
          .addMemOperand(CPMMO);
    else
      BuildMI(*BB, MI, dl, TII->get(ARM::LDRcp))
          .addReg(varEnd, RegState::Define)
          .addConstantPoolIndex(Idx)
          .addImm(0)
          .add(predOps(ARMCC::AL))
          .addMemOperand(CPMMO);
  }
  BB->addSuccessor(loopMBB);

  // Generate the loop body:
  //   varPhi = PHI(varLoop, varEnd)
  //   srcPhi = PHI(srcLoop, src)
  //   destPhi = PHI(destLoop, dst)
  MachineBasicBlock *entryBB = BB;
  BB = loopMBB;
  Register varLoop = MRI.createVirtualRegister(TRC);
  Register varPhi = MRI.createVirtualRegister(TRC);
  Register srcLoop = MRI.createVirtualRegister(TRC);
  Register srcPhi = MRI.createVirtualRegister(TRC);
  Register destLoop = MRI.createVirtualRegister(TRC);
  Register destPhi = MRI.createVirtualRegister(TRC);

  BuildMI(*BB, BB->begin(), dl, TII->get(ARM::PHI), varPhi)
    .addReg(varLoop).addMBB(loopMBB)
    .addReg(varEnd).addMBB(entryBB);
  BuildMI(BB, dl, TII->get(ARM::PHI), srcPhi)
    .addReg(srcLoop).addMBB(loopMBB)
    .addReg(src).addMBB(entryBB);
  BuildMI(BB, dl, TII->get(ARM::PHI), destPhi)
    .addReg(destLoop).addMBB(loopMBB)
    .addReg(dest).addMBB(entryBB);

  //   [scratch, srcLoop] = LDR_POST(srcPhi, UnitSize)
  //   [destLoop] = STR_POST(scratch, destPhi, UnitSiz)
  Register scratch = MRI.createVirtualRegister(IsNeon ? VecTRC : TRC);
  emitPostLd(BB, BB->end(), TII, dl, UnitSize, scratch, srcPhi, srcLoop,
             IsThumb1, IsThumb2);
  emitPostSt(BB, BB->end(), TII, dl, UnitSize, scratch, destPhi, destLoop,
             IsThumb1, IsThumb2);

  // Decrement loop variable by UnitSize.
  if (IsThumb1) {
    BuildMI(*BB, BB->end(), dl, TII->get(ARM::tSUBi8), varLoop)
        .add(t1CondCodeOp())
        .addReg(varPhi)
        .addImm(UnitSize)
        .add(predOps(ARMCC::AL));
  } else {
    MachineInstrBuilder MIB =
        BuildMI(*BB, BB->end(), dl,
                TII->get(IsThumb2 ? ARM::t2SUBri : ARM::SUBri), varLoop);
    MIB.addReg(varPhi)
        .addImm(UnitSize)
        .add(predOps(ARMCC::AL))
        .add(condCodeOp());
    MIB->getOperand(5).setReg(ARM::CPSR);
    MIB->getOperand(5).setIsDef(true);
  }
  BuildMI(*BB, BB->end(), dl,
          TII->get(IsThumb1 ? ARM::tBcc : IsThumb2 ? ARM::t2Bcc : ARM::Bcc))
      .addMBB(loopMBB).addImm(ARMCC::NE).addReg(ARM::CPSR);

  // loopMBB can loop back to loopMBB or fall through to exitMBB.
  BB->addSuccessor(loopMBB);
  BB->addSuccessor(exitMBB);

  // Add epilogue to handle BytesLeft.
  BB = exitMBB;
  auto StartOfExit = exitMBB->begin();

  //   [scratch, srcOut] = LDRB_POST(srcLoop, 1)
  //   [destOut] = STRB_POST(scratch, destLoop, 1)
  unsigned srcIn = srcLoop;
  unsigned destIn = destLoop;
  for (unsigned i = 0; i < BytesLeft; i++) {
    Register srcOut = MRI.createVirtualRegister(TRC);
    Register destOut = MRI.createVirtualRegister(TRC);
    Register scratch = MRI.createVirtualRegister(TRC);
    emitPostLd(BB, StartOfExit, TII, dl, 1, scratch, srcIn, srcOut,
               IsThumb1, IsThumb2);
    emitPostSt(BB, StartOfExit, TII, dl, 1, scratch, destIn, destOut,
               IsThumb1, IsThumb2);
    srcIn = srcOut;
    destIn = destOut;
  }

  MI.eraseFromParent(); // The instruction is gone now.
  return BB;
}

MachineBasicBlock *
ARMTargetLowering::EmitLowered__chkstk(MachineInstr &MI,
                                       MachineBasicBlock *MBB) const {
  const TargetMachine &TM = getTargetMachine();
  const TargetInstrInfo &TII = *Subtarget->getInstrInfo();
  DebugLoc DL = MI.getDebugLoc();

  assert(Subtarget->isTargetWindows() &&
         "__chkstk is only supported on Windows");
  assert(Subtarget->isThumb2() && "Windows on ARM requires Thumb-2 mode");

  // __chkstk takes the number of words to allocate on the stack in R4, and
  // returns the stack adjustment in number of bytes in R4.  This will not
  // clober any other registers (other than the obvious lr).
  //
  // Although, technically, IP should be considered a register which may be
  // clobbered, the call itself will not touch it.  Windows on ARM is a pure
  // thumb-2 environment, so there is no interworking required.  As a result, we
  // do not expect a veneer to be emitted by the linker, clobbering IP.
  //
  // Each module receives its own copy of __chkstk, so no import thunk is
  // required, again, ensuring that IP is not clobbered.
  //
  // Finally, although some linkers may theoretically provide a trampoline for
  // out of range calls (which is quite common due to a 32M range limitation of
  // branches for Thumb), we can generate the long-call version via
  // -mcmodel=large, alleviating the need for the trampoline which may clobber
  // IP.

  switch (TM.getCodeModel()) {
  case CodeModel::Tiny:
    llvm_unreachable("Tiny code model not available on ARM.");
  case CodeModel::Small:
  case CodeModel::Medium:
  case CodeModel::Kernel:
    BuildMI(*MBB, MI, DL, TII.get(ARM::tBL))
        .add(predOps(ARMCC::AL))
        .addExternalSymbol("__chkstk")
        .addReg(ARM::R4, RegState::Implicit | RegState::Kill)
        .addReg(ARM::R4, RegState::Implicit | RegState::Define)
        .addReg(ARM::R12,
                RegState::Implicit | RegState::Define | RegState::Dead)
        .addReg(ARM::CPSR,
                RegState::Implicit | RegState::Define | RegState::Dead);
    break;
  case CodeModel::Large: {
    MachineRegisterInfo &MRI = MBB->getParent()->getRegInfo();
    Register Reg = MRI.createVirtualRegister(&ARM::rGPRRegClass);

    BuildMI(*MBB, MI, DL, TII.get(ARM::t2MOVi32imm), Reg)
      .addExternalSymbol("__chkstk");
    BuildMI(*MBB, MI, DL, TII.get(gettBLXrOpcode(*MBB->getParent())))
        .add(predOps(ARMCC::AL))
        .addReg(Reg, RegState::Kill)
        .addReg(ARM::R4, RegState::Implicit | RegState::Kill)
        .addReg(ARM::R4, RegState::Implicit | RegState::Define)
        .addReg(ARM::R12,
                RegState::Implicit | RegState::Define | RegState::Dead)
        .addReg(ARM::CPSR,
                RegState::Implicit | RegState::Define | RegState::Dead);
    break;
  }
  }

  BuildMI(*MBB, MI, DL, TII.get(ARM::t2SUBrr), ARM::SP)
      .addReg(ARM::SP, RegState::Kill)
      .addReg(ARM::R4, RegState::Kill)
      .setMIFlags(MachineInstr::FrameSetup)
      .add(predOps(ARMCC::AL))
      .add(condCodeOp());

  MI.eraseFromParent();
  return MBB;
}

MachineBasicBlock *
ARMTargetLowering::EmitLowered__dbzchk(MachineInstr &MI,
                                       MachineBasicBlock *MBB) const {
  DebugLoc DL = MI.getDebugLoc();
  MachineFunction *MF = MBB->getParent();
  const TargetInstrInfo *TII = Subtarget->getInstrInfo();

  MachineBasicBlock *ContBB = MF->CreateMachineBasicBlock();
  MF->insert(++MBB->getIterator(), ContBB);
  ContBB->splice(ContBB->begin(), MBB,
                 std::next(MachineBasicBlock::iterator(MI)), MBB->end());
  ContBB->transferSuccessorsAndUpdatePHIs(MBB);
  MBB->addSuccessor(ContBB);

  MachineBasicBlock *TrapBB = MF->CreateMachineBasicBlock();
  BuildMI(TrapBB, DL, TII->get(ARM::t__brkdiv0));
  MF->push_back(TrapBB);
  MBB->addSuccessor(TrapBB);

  BuildMI(*MBB, MI, DL, TII->get(ARM::tCMPi8))
      .addReg(MI.getOperand(0).getReg())
      .addImm(0)
      .add(predOps(ARMCC::AL));
  BuildMI(*MBB, MI, DL, TII->get(ARM::t2Bcc))
      .addMBB(TrapBB)
      .addImm(ARMCC::EQ)
      .addReg(ARM::CPSR);

  MI.eraseFromParent();
  return ContBB;
}

// The CPSR operand of SelectItr might be missing a kill marker
// because there were multiple uses of CPSR, and ISel didn't know
// which to mark. Figure out whether SelectItr should have had a
// kill marker, and set it if it should. Returns the correct kill
// marker value.
static bool checkAndUpdateCPSRKill(MachineBasicBlock::iterator SelectItr,
                                   MachineBasicBlock* BB,
                                   const TargetRegisterInfo* TRI) {
  // Scan forward through BB for a use/def of CPSR.
  MachineBasicBlock::iterator miI(std::next(SelectItr));
  for (MachineBasicBlock::iterator miE = BB->end(); miI != miE; ++miI) {
    const MachineInstr& mi = *miI;
    if (mi.readsRegister(ARM::CPSR, /*TRI=*/nullptr))
      return false;
    if (mi.definesRegister(ARM::CPSR, /*TRI=*/nullptr))
      break; // Should have kill-flag - update below.
  }

  // If we hit the end of the block, check whether CPSR is live into a
  // successor.
  if (miI == BB->end()) {
    for (MachineBasicBlock *Succ : BB->successors())
      if (Succ->isLiveIn(ARM::CPSR))
        return false;
  }

  // We found a def, or hit the end of the basic block and CPSR wasn't live
  // out. SelectMI should have a kill flag on CPSR.
  SelectItr->addRegisterKilled(ARM::CPSR, TRI);
  return true;
}

/// Adds logic in loop entry MBB to calculate loop iteration count and adds
/// t2WhileLoopSetup and t2WhileLoopStart to generate WLS loop
static Register genTPEntry(MachineBasicBlock *TpEntry,
                           MachineBasicBlock *TpLoopBody,
                           MachineBasicBlock *TpExit, Register OpSizeReg,
                           const TargetInstrInfo *TII, DebugLoc Dl,
                           MachineRegisterInfo &MRI) {
  // Calculates loop iteration count = ceil(n/16) = (n + 15) >> 4.
  Register AddDestReg = MRI.createVirtualRegister(&ARM::rGPRRegClass);
  BuildMI(TpEntry, Dl, TII->get(ARM::t2ADDri), AddDestReg)
      .addUse(OpSizeReg)
      .addImm(15)
      .add(predOps(ARMCC::AL))
      .addReg(0);

  Register LsrDestReg = MRI.createVirtualRegister(&ARM::rGPRRegClass);
  BuildMI(TpEntry, Dl, TII->get(ARM::t2LSRri), LsrDestReg)
      .addUse(AddDestReg, RegState::Kill)
      .addImm(4)
      .add(predOps(ARMCC::AL))
      .addReg(0);

  Register TotalIterationsReg = MRI.createVirtualRegister(&ARM::GPRlrRegClass);
  BuildMI(TpEntry, Dl, TII->get(ARM::t2WhileLoopSetup), TotalIterationsReg)
      .addUse(LsrDestReg, RegState::Kill);

  BuildMI(TpEntry, Dl, TII->get(ARM::t2WhileLoopStart))
      .addUse(TotalIterationsReg)
      .addMBB(TpExit);

  BuildMI(TpEntry, Dl, TII->get(ARM::t2B))
      .addMBB(TpLoopBody)
      .add(predOps(ARMCC::AL));

  return TotalIterationsReg;
}

/// Adds logic in the loopBody MBB to generate MVE_VCTP, t2DoLoopDec and
/// t2DoLoopEnd. These are used by later passes to generate tail predicated
/// loops.
static void genTPLoopBody(MachineBasicBlock *TpLoopBody,
                          MachineBasicBlock *TpEntry, MachineBasicBlock *TpExit,
                          const TargetInstrInfo *TII, DebugLoc Dl,
                          MachineRegisterInfo &MRI, Register OpSrcReg,
                          Register OpDestReg, Register ElementCountReg,
                          Register TotalIterationsReg, bool IsMemcpy) {
  // First insert 4 PHI nodes for: Current pointer to Src (if memcpy), Dest
  // array, loop iteration counter, predication counter.

  Register SrcPhiReg, CurrSrcReg;
  if (IsMemcpy) {
    //  Current position in the src array
    SrcPhiReg = MRI.createVirtualRegister(&ARM::rGPRRegClass);
    CurrSrcReg = MRI.createVirtualRegister(&ARM::rGPRRegClass);
    BuildMI(TpLoopBody, Dl, TII->get(ARM::PHI), SrcPhiReg)
        .addUse(OpSrcReg)
        .addMBB(TpEntry)
        .addUse(CurrSrcReg)
        .addMBB(TpLoopBody);
  }

  // Current position in the dest array
  Register DestPhiReg = MRI.createVirtualRegister(&ARM::rGPRRegClass);
  Register CurrDestReg = MRI.createVirtualRegister(&ARM::rGPRRegClass);
  BuildMI(TpLoopBody, Dl, TII->get(ARM::PHI), DestPhiReg)
      .addUse(OpDestReg)
      .addMBB(TpEntry)
      .addUse(CurrDestReg)
      .addMBB(TpLoopBody);

  // Current loop counter
  Register LoopCounterPhiReg = MRI.createVirtualRegister(&ARM::GPRlrRegClass);
  Register RemainingLoopIterationsReg =
      MRI.createVirtualRegister(&ARM::GPRlrRegClass);
  BuildMI(TpLoopBody, Dl, TII->get(ARM::PHI), LoopCounterPhiReg)
      .addUse(TotalIterationsReg)
      .addMBB(TpEntry)
      .addUse(RemainingLoopIterationsReg)
      .addMBB(TpLoopBody);

  // Predication counter
  Register PredCounterPhiReg = MRI.createVirtualRegister(&ARM::rGPRRegClass);
  Register RemainingElementsReg = MRI.createVirtualRegister(&ARM::rGPRRegClass);
  BuildMI(TpLoopBody, Dl, TII->get(ARM::PHI), PredCounterPhiReg)
      .addUse(ElementCountReg)
      .addMBB(TpEntry)
      .addUse(RemainingElementsReg)
      .addMBB(TpLoopBody);

  // Pass predication counter to VCTP
  Register VccrReg = MRI.createVirtualRegister(&ARM::VCCRRegClass);
  BuildMI(TpLoopBody, Dl, TII->get(ARM::MVE_VCTP8), VccrReg)
      .addUse(PredCounterPhiReg)
      .addImm(ARMVCC::None)
      .addReg(0)
      .addReg(0);

  BuildMI(TpLoopBody, Dl, TII->get(ARM::t2SUBri), RemainingElementsReg)
      .addUse(PredCounterPhiReg)
      .addImm(16)
      .add(predOps(ARMCC::AL))
      .addReg(0);

  // VLDRB (only if memcpy) and VSTRB instructions, predicated using VPR
  Register SrcValueReg;
  if (IsMemcpy) {
    SrcValueReg = MRI.createVirtualRegister(&ARM::MQPRRegClass);
    BuildMI(TpLoopBody, Dl, TII->get(ARM::MVE_VLDRBU8_post))
        .addDef(CurrSrcReg)
        .addDef(SrcValueReg)
        .addReg(SrcPhiReg)
        .addImm(16)
        .addImm(ARMVCC::Then)
        .addUse(VccrReg)
        .addReg(0);
  } else
    SrcValueReg = OpSrcReg;

  BuildMI(TpLoopBody, Dl, TII->get(ARM::MVE_VSTRBU8_post))
      .addDef(CurrDestReg)
      .addUse(SrcValueReg)
      .addReg(DestPhiReg)
      .addImm(16)
      .addImm(ARMVCC::Then)
      .addUse(VccrReg)
      .addReg(0);

  // Add the pseudoInstrs for decrementing the loop counter and marking the
  // end:t2DoLoopDec and t2DoLoopEnd
  BuildMI(TpLoopBody, Dl, TII->get(ARM::t2LoopDec), RemainingLoopIterationsReg)
      .addUse(LoopCounterPhiReg)
      .addImm(1);

  BuildMI(TpLoopBody, Dl, TII->get(ARM::t2LoopEnd))
      .addUse(RemainingLoopIterationsReg)
      .addMBB(TpLoopBody);

  BuildMI(TpLoopBody, Dl, TII->get(ARM::t2B))
      .addMBB(TpExit)
      .add(predOps(ARMCC::AL));
}

MachineBasicBlock *
ARMTargetLowering::EmitInstrWithCustomInserter(MachineInstr &MI,
                                               MachineBasicBlock *BB) const {
  const TargetInstrInfo *TII = Subtarget->getInstrInfo();
  DebugLoc dl = MI.getDebugLoc();
  bool isThumb2 = Subtarget->isThumb2();
  switch (MI.getOpcode()) {
  default: {
    MI.print(errs());
    llvm_unreachable("Unexpected instr type to insert");
  }

  // Thumb1 post-indexed loads are really just single-register LDMs.
  case ARM::tLDR_postidx: {
    MachineOperand Def(MI.getOperand(1));
    BuildMI(*BB, MI, dl, TII->get(ARM::tLDMIA_UPD))
        .add(Def)  // Rn_wb
        .add(MI.getOperand(2))  // Rn
        .add(MI.getOperand(3))  // PredImm
        .add(MI.getOperand(4))  // PredReg
        .add(MI.getOperand(0))  // Rt
        .cloneMemRefs(MI);
    MI.eraseFromParent();
    return BB;
  }

  case ARM::MVE_MEMCPYLOOPINST:
  case ARM::MVE_MEMSETLOOPINST: {

    // Transformation below expands MVE_MEMCPYLOOPINST/MVE_MEMSETLOOPINST Pseudo
    // into a Tail Predicated (TP) Loop. It adds the instructions to calculate
    // the iteration count =ceil(size_in_bytes/16)) in the TP entry block and
    // adds the relevant instructions in the TP loop Body for generation of a
    // WLSTP loop.

    // Below is relevant portion of the CFG after the transformation.
    // The Machine Basic Blocks are shown along with branch conditions (in
    // brackets). Note that TP entry/exit MBBs depict the entry/exit of this
    // portion of the CFG and may not necessarily be the entry/exit of the
    // function.

    //             (Relevant) CFG after transformation:
    //               TP entry MBB
    //                   |
    //          |-----------------|
    //       (n <= 0)          (n > 0)
    //          |                 |
    //          |         TP loop Body MBB<--|
    //          |                |           |
    //           \               |___________|
    //            \             /
    //              TP exit MBB

    MachineFunction *MF = BB->getParent();
    MachineFunctionProperties &Properties = MF->getProperties();
    MachineRegisterInfo &MRI = MF->getRegInfo();

    Register OpDestReg = MI.getOperand(0).getReg();
    Register OpSrcReg = MI.getOperand(1).getReg();
    Register OpSizeReg = MI.getOperand(2).getReg();

    // Allocate the required MBBs and add to parent function.
    MachineBasicBlock *TpEntry = BB;
    MachineBasicBlock *TpLoopBody = MF->CreateMachineBasicBlock();
    MachineBasicBlock *TpExit;

    MF->push_back(TpLoopBody);

    // If any instructions are present in the current block after
    // MVE_MEMCPYLOOPINST or MVE_MEMSETLOOPINST, split the current block and
    // move the instructions into the newly created exit block. If there are no
    // instructions add an explicit branch to the FallThrough block and then
    // split.
    //
    // The split is required for two reasons:
    // 1) A terminator(t2WhileLoopStart) will be placed at that site.
    // 2) Since a TPLoopBody will be added later, any phis in successive blocks
    //    need to be updated. splitAt() already handles this.
    TpExit = BB->splitAt(MI, false);
    if (TpExit == BB) {
      assert(BB->canFallThrough() && "Exit Block must be Fallthrough of the "
                                     "block containing memcpy/memset Pseudo");
      TpExit = BB->getFallThrough();
      BuildMI(BB, dl, TII->get(ARM::t2B))
          .addMBB(TpExit)
          .add(predOps(ARMCC::AL));
      TpExit = BB->splitAt(MI, false);
    }

    // Add logic for iteration count
    Register TotalIterationsReg =
        genTPEntry(TpEntry, TpLoopBody, TpExit, OpSizeReg, TII, dl, MRI);

    // Add the vectorized (and predicated) loads/store instructions
    bool IsMemcpy = MI.getOpcode() == ARM::MVE_MEMCPYLOOPINST;
    genTPLoopBody(TpLoopBody, TpEntry, TpExit, TII, dl, MRI, OpSrcReg,
                  OpDestReg, OpSizeReg, TotalIterationsReg, IsMemcpy);

    // Required to avoid conflict with the MachineVerifier during testing.
    Properties.reset(MachineFunctionProperties::Property::NoPHIs);

    // Connect the blocks
    TpEntry->addSuccessor(TpLoopBody);
    TpLoopBody->addSuccessor(TpLoopBody);
    TpLoopBody->addSuccessor(TpExit);

    // Reorder for a more natural layout
    TpLoopBody->moveAfter(TpEntry);
    TpExit->moveAfter(TpLoopBody);

    // Finally, remove the memcpy Pseudo Instruction
    MI.eraseFromParent();

    // Return the exit block as it may contain other instructions requiring a
    // custom inserter
    return TpExit;
  }

  // The Thumb2 pre-indexed stores have the same MI operands, they just
  // define them differently in the .td files from the isel patterns, so
  // they need pseudos.
  case ARM::t2STR_preidx:
    MI.setDesc(TII->get(ARM::t2STR_PRE));
    return BB;
  case ARM::t2STRB_preidx:
    MI.setDesc(TII->get(ARM::t2STRB_PRE));
    return BB;
  case ARM::t2STRH_preidx:
    MI.setDesc(TII->get(ARM::t2STRH_PRE));
    return BB;

  case ARM::STRi_preidx:
  case ARM::STRBi_preidx: {
    unsigned NewOpc = MI.getOpcode() == ARM::STRi_preidx ? ARM::STR_PRE_IMM
                                                         : ARM::STRB_PRE_IMM;
    // Decode the offset.
    unsigned Offset = MI.getOperand(4).getImm();
    bool isSub = ARM_AM::getAM2Op(Offset) == ARM_AM::sub;
    Offset = ARM_AM::getAM2Offset(Offset);
    if (isSub)
      Offset = -Offset;

    MachineMemOperand *MMO = *MI.memoperands_begin();
    BuildMI(*BB, MI, dl, TII->get(NewOpc))
        .add(MI.getOperand(0)) // Rn_wb
        .add(MI.getOperand(1)) // Rt
        .add(MI.getOperand(2)) // Rn
        .addImm(Offset)        // offset (skip GPR==zero_reg)
        .add(MI.getOperand(5)) // pred
        .add(MI.getOperand(6))
        .addMemOperand(MMO);
    MI.eraseFromParent();
    return BB;
  }
  case ARM::STRr_preidx:
  case ARM::STRBr_preidx:
  case ARM::STRH_preidx: {
    unsigned NewOpc;
    switch (MI.getOpcode()) {
    default: llvm_unreachable("unexpected opcode!");
    case ARM::STRr_preidx: NewOpc = ARM::STR_PRE_REG; break;
    case ARM::STRBr_preidx: NewOpc = ARM::STRB_PRE_REG; break;
    case ARM::STRH_preidx: NewOpc = ARM::STRH_PRE; break;
    }
    MachineInstrBuilder MIB = BuildMI(*BB, MI, dl, TII->get(NewOpc));
    for (const MachineOperand &MO : MI.operands())
      MIB.add(MO);
    MI.eraseFromParent();
    return BB;
  }

  case ARM::tMOVCCr_pseudo: {
    // To "insert" a SELECT_CC instruction, we actually have to insert the
    // diamond control-flow pattern.  The incoming instruction knows the
    // destination vreg to set, the condition code register to branch on, the
    // true/false values to select between, and a branch opcode to use.
    const BasicBlock *LLVM_BB = BB->getBasicBlock();
    MachineFunction::iterator It = ++BB->getIterator();

    //  thisMBB:
    //  ...
    //   TrueVal = ...
    //   cmpTY ccX, r1, r2
    //   bCC copy1MBB
    //   fallthrough --> copy0MBB
    MachineBasicBlock *thisMBB  = BB;
    MachineFunction *F = BB->getParent();
    MachineBasicBlock *copy0MBB = F->CreateMachineBasicBlock(LLVM_BB);
    MachineBasicBlock *sinkMBB  = F->CreateMachineBasicBlock(LLVM_BB);
    F->insert(It, copy0MBB);
    F->insert(It, sinkMBB);

    // Set the call frame size on entry to the new basic blocks.
    unsigned CallFrameSize = TII->getCallFrameSizeAt(MI);
    copy0MBB->setCallFrameSize(CallFrameSize);
    sinkMBB->setCallFrameSize(CallFrameSize);

    // Check whether CPSR is live past the tMOVCCr_pseudo.
    const TargetRegisterInfo *TRI = Subtarget->getRegisterInfo();
    if (!MI.killsRegister(ARM::CPSR, /*TRI=*/nullptr) &&
        !checkAndUpdateCPSRKill(MI, thisMBB, TRI)) {
      copy0MBB->addLiveIn(ARM::CPSR);
      sinkMBB->addLiveIn(ARM::CPSR);
    }

    // Transfer the remainder of BB and its successor edges to sinkMBB.
    sinkMBB->splice(sinkMBB->begin(), BB,
                    std::next(MachineBasicBlock::iterator(MI)), BB->end());
    sinkMBB->transferSuccessorsAndUpdatePHIs(BB);

    BB->addSuccessor(copy0MBB);
    BB->addSuccessor(sinkMBB);

    BuildMI(BB, dl, TII->get(ARM::tBcc))
        .addMBB(sinkMBB)
        .addImm(MI.getOperand(3).getImm())
        .addReg(MI.getOperand(4).getReg());

    //  copy0MBB:
    //   %FalseValue = ...
    //   # fallthrough to sinkMBB
    BB = copy0MBB;

    // Update machine-CFG edges
    BB->addSuccessor(sinkMBB);

    //  sinkMBB:
    //   %Result = phi [ %FalseValue, copy0MBB ], [ %TrueValue, thisMBB ]
    //  ...
    BB = sinkMBB;
    BuildMI(*BB, BB->begin(), dl, TII->get(ARM::PHI), MI.getOperand(0).getReg())
        .addReg(MI.getOperand(1).getReg())
        .addMBB(copy0MBB)
        .addReg(MI.getOperand(2).getReg())
        .addMBB(thisMBB);

    MI.eraseFromParent(); // The pseudo instruction is gone now.
    return BB;
  }

  case ARM::BCCi64:
  case ARM::BCCZi64: {
    // If there is an unconditional branch to the other successor, remove it.
    BB->erase(std::next(MachineBasicBlock::iterator(MI)), BB->end());

    // Compare both parts that make up the double comparison separately for
    // equality.
    bool RHSisZero = MI.getOpcode() == ARM::BCCZi64;

    Register LHS1 = MI.getOperand(1).getReg();
    Register LHS2 = MI.getOperand(2).getReg();
    if (RHSisZero) {
      BuildMI(BB, dl, TII->get(isThumb2 ? ARM::t2CMPri : ARM::CMPri))
          .addReg(LHS1)
          .addImm(0)
          .add(predOps(ARMCC::AL));
      BuildMI(BB, dl, TII->get(isThumb2 ? ARM::t2CMPri : ARM::CMPri))
        .addReg(LHS2).addImm(0)
        .addImm(ARMCC::EQ).addReg(ARM::CPSR);
    } else {
      Register RHS1 = MI.getOperand(3).getReg();
      Register RHS2 = MI.getOperand(4).getReg();
      BuildMI(BB, dl, TII->get(isThumb2 ? ARM::t2CMPrr : ARM::CMPrr))
          .addReg(LHS1)
          .addReg(RHS1)
          .add(predOps(ARMCC::AL));
      BuildMI(BB, dl, TII->get(isThumb2 ? ARM::t2CMPrr : ARM::CMPrr))
        .addReg(LHS2).addReg(RHS2)
        .addImm(ARMCC::EQ).addReg(ARM::CPSR);
    }

    MachineBasicBlock *destMBB = MI.getOperand(RHSisZero ? 3 : 5).getMBB();
    MachineBasicBlock *exitMBB = OtherSucc(BB, destMBB);
    if (MI.getOperand(0).getImm() == ARMCC::NE)
      std::swap(destMBB, exitMBB);

    BuildMI(BB, dl, TII->get(isThumb2 ? ARM::t2Bcc : ARM::Bcc))
      .addMBB(destMBB).addImm(ARMCC::EQ).addReg(ARM::CPSR);
    if (isThumb2)
      BuildMI(BB, dl, TII->get(ARM::t2B))
          .addMBB(exitMBB)
          .add(predOps(ARMCC::AL));
    else
      BuildMI(BB, dl, TII->get(ARM::B)) .addMBB(exitMBB);

    MI.eraseFromParent(); // The pseudo instruction is gone now.
    return BB;
  }

  case ARM::Int_eh_sjlj_setjmp:
  case ARM::Int_eh_sjlj_setjmp_nofp:
  case ARM::tInt_eh_sjlj_setjmp:
  case ARM::t2Int_eh_sjlj_setjmp:
  case ARM::t2Int_eh_sjlj_setjmp_nofp:
    return BB;

  case ARM::Int_eh_sjlj_setup_dispatch:
    EmitSjLjDispatchBlock(MI, BB);
    return BB;

  case ARM::ABS:
  case ARM::t2ABS: {
    // To insert an ABS instruction, we have to insert the
    // diamond control-flow pattern.  The incoming instruction knows the
    // source vreg to test against 0, the destination vreg to set,
    // the condition code register to branch on, the
    // true/false values to select between, and a branch opcode to use.
    // It transforms
    //     V1 = ABS V0
    // into
    //     V2 = MOVS V0
    //     BCC                      (branch to SinkBB if V0 >= 0)
    //     RSBBB: V3 = RSBri V2, 0  (compute ABS if V2 < 0)
    //     SinkBB: V1 = PHI(V2, V3)
    const BasicBlock *LLVM_BB = BB->getBasicBlock();
    MachineFunction::iterator BBI = ++BB->getIterator();
    MachineFunction *Fn = BB->getParent();
    MachineBasicBlock *RSBBB = Fn->CreateMachineBasicBlock(LLVM_BB);
    MachineBasicBlock *SinkBB  = Fn->CreateMachineBasicBlock(LLVM_BB);
    Fn->insert(BBI, RSBBB);
    Fn->insert(BBI, SinkBB);

    Register ABSSrcReg = MI.getOperand(1).getReg();
    Register ABSDstReg = MI.getOperand(0).getReg();
    bool ABSSrcKIll = MI.getOperand(1).isKill();
    bool isThumb2 = Subtarget->isThumb2();
    MachineRegisterInfo &MRI = Fn->getRegInfo();
    // In Thumb mode S must not be specified if source register is the SP or
    // PC and if destination register is the SP, so restrict register class
    Register NewRsbDstReg = MRI.createVirtualRegister(
        isThumb2 ? &ARM::rGPRRegClass : &ARM::GPRRegClass);

    // Transfer the remainder of BB and its successor edges to sinkMBB.
    SinkBB->splice(SinkBB->begin(), BB,
                   std::next(MachineBasicBlock::iterator(MI)), BB->end());
    SinkBB->transferSuccessorsAndUpdatePHIs(BB);

    BB->addSuccessor(RSBBB);
    BB->addSuccessor(SinkBB);

    // fall through to SinkMBB
    RSBBB->addSuccessor(SinkBB);

    // insert a cmp at the end of BB
    BuildMI(BB, dl, TII->get(isThumb2 ? ARM::t2CMPri : ARM::CMPri))
        .addReg(ABSSrcReg)
        .addImm(0)
        .add(predOps(ARMCC::AL));

    // insert a bcc with opposite CC to ARMCC::MI at the end of BB
    BuildMI(BB, dl,
      TII->get(isThumb2 ? ARM::t2Bcc : ARM::Bcc)).addMBB(SinkBB)
      .addImm(ARMCC::getOppositeCondition(ARMCC::MI)).addReg(ARM::CPSR);

    // insert rsbri in RSBBB
    // Note: BCC and rsbri will be converted into predicated rsbmi
    // by if-conversion pass
    BuildMI(*RSBBB, RSBBB->begin(), dl,
            TII->get(isThumb2 ? ARM::t2RSBri : ARM::RSBri), NewRsbDstReg)
        .addReg(ABSSrcReg, ABSSrcKIll ? RegState::Kill : 0)
        .addImm(0)
        .add(predOps(ARMCC::AL))
        .add(condCodeOp());

    // insert PHI in SinkBB,
    // reuse ABSDstReg to not change uses of ABS instruction
    BuildMI(*SinkBB, SinkBB->begin(), dl,
      TII->get(ARM::PHI), ABSDstReg)
      .addReg(NewRsbDstReg).addMBB(RSBBB)
      .addReg(ABSSrcReg).addMBB(BB);

    // remove ABS instruction
    MI.eraseFromParent();

    // return last added BB
    return SinkBB;
  }
  case ARM::COPY_STRUCT_BYVAL_I32:
    ++NumLoopByVals;
    return EmitStructByval(MI, BB);
  case ARM::WIN__CHKSTK:
    return EmitLowered__chkstk(MI, BB);
  case ARM::WIN__DBZCHK:
    return EmitLowered__dbzchk(MI, BB);
  }
}

/// Attaches vregs to MEMCPY that it will use as scratch registers
/// when it is expanded into LDM/STM. This is done as a post-isel lowering
/// instead of as a custom inserter because we need the use list from the SDNode.
static void attachMEMCPYScratchRegs(const ARMSubtarget *Subtarget,
                                    MachineInstr &MI, const SDNode *Node) {
  bool isThumb1 = Subtarget->isThumb1Only();

  DebugLoc DL = MI.getDebugLoc();
  MachineFunction *MF = MI.getParent()->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  MachineInstrBuilder MIB(*MF, MI);

  // If the new dst/src is unused mark it as dead.
  if (!Node->hasAnyUseOfValue(0)) {
    MI.getOperand(0).setIsDead(true);
  }
  if (!Node->hasAnyUseOfValue(1)) {
    MI.getOperand(1).setIsDead(true);
  }

  // The MEMCPY both defines and kills the scratch registers.
  for (unsigned I = 0; I != MI.getOperand(4).getImm(); ++I) {
    Register TmpReg = MRI.createVirtualRegister(isThumb1 ? &ARM::tGPRRegClass
                                                         : &ARM::GPRRegClass);
    MIB.addReg(TmpReg, RegState::Define|RegState::Dead);
  }
}

void ARMTargetLowering::AdjustInstrPostInstrSelection(MachineInstr &MI,
                                                      SDNode *Node) const {
  if (MI.getOpcode() == ARM::MEMCPY) {
    attachMEMCPYScratchRegs(Subtarget, MI, Node);
    return;
  }

  const MCInstrDesc *MCID = &MI.getDesc();
  // Adjust potentially 's' setting instructions after isel, i.e. ADC, SBC, RSB,
  // RSC. Coming out of isel, they have an implicit CPSR def, but the optional
  // operand is still set to noreg. If needed, set the optional operand's
  // register to CPSR, and remove the redundant implicit def.
  //
  // e.g. ADCS (..., implicit-def CPSR) -> ADC (... opt:def CPSR).

  // Rename pseudo opcodes.
  unsigned NewOpc = convertAddSubFlagsOpcode(MI.getOpcode());
  unsigned ccOutIdx;
  if (NewOpc) {
    const ARMBaseInstrInfo *TII = Subtarget->getInstrInfo();
    MCID = &TII->get(NewOpc);

    assert(MCID->getNumOperands() ==
           MI.getDesc().getNumOperands() + 5 - MI.getDesc().getSize()
        && "converted opcode should be the same except for cc_out"
           " (and, on Thumb1, pred)");

    MI.setDesc(*MCID);

    // Add the optional cc_out operand
    MI.addOperand(MachineOperand::CreateReg(0, /*isDef=*/true));

    // On Thumb1, move all input operands to the end, then add the predicate
    if (Subtarget->isThumb1Only()) {
      for (unsigned c = MCID->getNumOperands() - 4; c--;) {
        MI.addOperand(MI.getOperand(1));
        MI.removeOperand(1);
      }

      // Restore the ties
      for (unsigned i = MI.getNumOperands(); i--;) {
        const MachineOperand& op = MI.getOperand(i);
        if (op.isReg() && op.isUse()) {
          int DefIdx = MCID->getOperandConstraint(i, MCOI::TIED_TO);
          if (DefIdx != -1)
            MI.tieOperands(DefIdx, i);
        }
      }

      MI.addOperand(MachineOperand::CreateImm(ARMCC::AL));
      MI.addOperand(MachineOperand::CreateReg(0, /*isDef=*/false));
      ccOutIdx = 1;
    } else
      ccOutIdx = MCID->getNumOperands() - 1;
  } else
    ccOutIdx = MCID->getNumOperands() - 1;

  // Any ARM instruction that sets the 's' bit should specify an optional
  // "cc_out" operand in the last operand position.
  if (!MI.hasOptionalDef() || !MCID->operands()[ccOutIdx].isOptionalDef()) {
    assert(!NewOpc && "Optional cc_out operand required");
    return;
  }
  // Look for an implicit def of CPSR added by MachineInstr ctor. Remove it
  // since we already have an optional CPSR def.
  bool definesCPSR = false;
  bool deadCPSR = false;
  for (unsigned i = MCID->getNumOperands(), e = MI.getNumOperands(); i != e;
       ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if (MO.isReg() && MO.isDef() && MO.getReg() == ARM::CPSR) {
      definesCPSR = true;
      if (MO.isDead())
        deadCPSR = true;
      MI.removeOperand(i);
      break;
    }
  }
  if (!definesCPSR) {
    assert(!NewOpc && "Optional cc_out operand required");
    return;
  }
  assert(deadCPSR == !Node->hasAnyUseOfValue(1) && "inconsistent dead flag");
  if (deadCPSR) {
    assert(!MI.getOperand(ccOutIdx).getReg() &&
           "expect uninitialized optional cc_out operand");
    // Thumb1 instructions must have the S bit even if the CPSR is dead.
    if (!Subtarget->isThumb1Only())
      return;
  }

  // If this instruction was defined with an optional CPSR def and its dag node
  // had a live implicit CPSR def, then activate the optional CPSR def.
  MachineOperand &MO = MI.getOperand(ccOutIdx);
  MO.setReg(ARM::CPSR);
  MO.setIsDef(true);
}

//===----------------------------------------------------------------------===//
//                           ARM Optimization Hooks
//===----------------------------------------------------------------------===//

// Helper function that checks if N is a null or all ones constant.
static inline bool isZeroOrAllOnes(SDValue N, bool AllOnes) {
  return AllOnes ? isAllOnesConstant(N) : isNullConstant(N);
}

// Return true if N is conditionally 0 or all ones.
// Detects these expressions where cc is an i1 value:
//
//   (select cc 0, y)   [AllOnes=0]
//   (select cc y, 0)   [AllOnes=0]
//   (zext cc)          [AllOnes=0]
//   (sext cc)          [AllOnes=0/1]
//   (select cc -1, y)  [AllOnes=1]
//   (select cc y, -1)  [AllOnes=1]
//
// Invert is set when N is the null/all ones constant when CC is false.
// OtherOp is set to the alternative value of N.
static bool isConditionalZeroOrAllOnes(SDNode *N, bool AllOnes,
                                       SDValue &CC, bool &Invert,
                                       SDValue &OtherOp,
                                       SelectionDAG &DAG) {
  switch (N->getOpcode()) {
  default: return false;
  case ISD::SELECT: {
    CC = N->getOperand(0);
    SDValue N1 = N->getOperand(1);
    SDValue N2 = N->getOperand(2);
    if (isZeroOrAllOnes(N1, AllOnes)) {
      Invert = false;
      OtherOp = N2;
      return true;
    }
    if (isZeroOrAllOnes(N2, AllOnes)) {
      Invert = true;
      OtherOp = N1;
      return true;
    }
    return false;
  }
  case ISD::ZERO_EXTEND:
    // (zext cc) can never be the all ones value.
    if (AllOnes)
      return false;
    [[fallthrough]];
  case ISD::SIGN_EXTEND: {
    SDLoc dl(N);
    EVT VT = N->getValueType(0);
    CC = N->getOperand(0);
    if (CC.getValueType() != MVT::i1 || CC.getOpcode() != ISD::SETCC)
      return false;
    Invert = !AllOnes;
    if (AllOnes)
      // When looking for an AllOnes constant, N is an sext, and the 'other'
      // value is 0.
      OtherOp = DAG.getConstant(0, dl, VT);
    else if (N->getOpcode() == ISD::ZERO_EXTEND)
      // When looking for a 0 constant, N can be zext or sext.
      OtherOp = DAG.getConstant(1, dl, VT);
    else
      OtherOp = DAG.getAllOnesConstant(dl, VT);
    return true;
  }
  }
}

// Combine a constant select operand into its use:
//
//   (add (select cc, 0, c), x)  -> (select cc, x, (add, x, c))
//   (sub x, (select cc, 0, c))  -> (select cc, x, (sub, x, c))
//   (and (select cc, -1, c), x) -> (select cc, x, (and, x, c))  [AllOnes=1]
//   (or  (select cc, 0, c), x)  -> (select cc, x, (or, x, c))
//   (xor (select cc, 0, c), x)  -> (select cc, x, (xor, x, c))
//
// The transform is rejected if the select doesn't have a constant operand that
// is null, or all ones when AllOnes is set.
//
// Also recognize sext/zext from i1:
//
//   (add (zext cc), x) -> (select cc (add x, 1), x)
//   (add (sext cc), x) -> (select cc (add x, -1), x)
//
// These transformations eventually create predicated instructions.
//
// @param N       The node to transform.
// @param Slct    The N operand that is a select.
// @param OtherOp The other N operand (x above).
// @param DCI     Context.
// @param AllOnes Require the select constant to be all ones instead of null.
// @returns The new node, or SDValue() on failure.
static
SDValue combineSelectAndUse(SDNode *N, SDValue Slct, SDValue OtherOp,
                            TargetLowering::DAGCombinerInfo &DCI,
                            bool AllOnes = false) {
  SelectionDAG &DAG = DCI.DAG;
  EVT VT = N->getValueType(0);
  SDValue NonConstantVal;
  SDValue CCOp;
  bool SwapSelectOps;
  if (!isConditionalZeroOrAllOnes(Slct.getNode(), AllOnes, CCOp, SwapSelectOps,
                                  NonConstantVal, DAG))
    return SDValue();

  // Slct is now know to be the desired identity constant when CC is true.
  SDValue TrueVal = OtherOp;
  SDValue FalseVal = DAG.getNode(N->getOpcode(), SDLoc(N), VT,
                                 OtherOp, NonConstantVal);
  // Unless SwapSelectOps says CC should be false.
  if (SwapSelectOps)
    std::swap(TrueVal, FalseVal);

  return DAG.getNode(ISD::SELECT, SDLoc(N), VT,
                     CCOp, TrueVal, FalseVal);
}

// Attempt combineSelectAndUse on each operand of a commutative operator N.
static
SDValue combineSelectAndUseCommutative(SDNode *N, bool AllOnes,
                                       TargetLowering::DAGCombinerInfo &DCI) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  if (N0.getNode()->hasOneUse())
    if (SDValue Result = combineSelectAndUse(N, N0, N1, DCI, AllOnes))
      return Result;
  if (N1.getNode()->hasOneUse())
    if (SDValue Result = combineSelectAndUse(N, N1, N0, DCI, AllOnes))
      return Result;
  return SDValue();
}

static bool IsVUZPShuffleNode(SDNode *N) {
  // VUZP shuffle node.
  if (N->getOpcode() == ARMISD::VUZP)
    return true;

  // "VUZP" on i32 is an alias for VTRN.
  if (N->getOpcode() == ARMISD::VTRN && N->getValueType(0) == MVT::v2i32)
    return true;

  return false;
}

static SDValue AddCombineToVPADD(SDNode *N, SDValue N0, SDValue N1,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 const ARMSubtarget *Subtarget) {
  // Look for ADD(VUZP.0, VUZP.1).
  if (!IsVUZPShuffleNode(N0.getNode()) || N0.getNode() != N1.getNode() ||
      N0 == N1)
   return SDValue();

  // Make sure the ADD is a 64-bit add; there is no 128-bit VPADD.
  if (!N->getValueType(0).is64BitVector())
    return SDValue();

  // Generate vpadd.
  SelectionDAG &DAG = DCI.DAG;
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDLoc dl(N);
  SDNode *Unzip = N0.getNode();
  EVT VT = N->getValueType(0);

  SmallVector<SDValue, 8> Ops;
  Ops.push_back(DAG.getConstant(Intrinsic::arm_neon_vpadd, dl,
                                TLI.getPointerTy(DAG.getDataLayout())));
  Ops.push_back(Unzip->getOperand(0));
  Ops.push_back(Unzip->getOperand(1));

  return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, VT, Ops);
}

static SDValue AddCombineVUZPToVPADDL(SDNode *N, SDValue N0, SDValue N1,
                                      TargetLowering::DAGCombinerInfo &DCI,
                                      const ARMSubtarget *Subtarget) {
  // Check for two extended operands.
  if (!(N0.getOpcode() == ISD::SIGN_EXTEND &&
        N1.getOpcode() == ISD::SIGN_EXTEND) &&
      !(N0.getOpcode() == ISD::ZERO_EXTEND &&
        N1.getOpcode() == ISD::ZERO_EXTEND))
    return SDValue();

  SDValue N00 = N0.getOperand(0);
  SDValue N10 = N1.getOperand(0);

  // Look for ADD(SEXT(VUZP.0), SEXT(VUZP.1))
  if (!IsVUZPShuffleNode(N00.getNode()) || N00.getNode() != N10.getNode() ||
      N00 == N10)
    return SDValue();

  // We only recognize Q register paddl here; this can't be reached until
  // after type legalization.
  if (!N00.getValueType().is64BitVector() ||
      !N0.getValueType().is128BitVector())
    return SDValue();

  // Generate vpaddl.
  SelectionDAG &DAG = DCI.DAG;
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  SDLoc dl(N);
  EVT VT = N->getValueType(0);

  SmallVector<SDValue, 8> Ops;
  // Form vpaddl.sN or vpaddl.uN depending on the kind of extension.
  unsigned Opcode;
  if (N0.getOpcode() == ISD::SIGN_EXTEND)
    Opcode = Intrinsic::arm_neon_vpaddls;
  else
    Opcode = Intrinsic::arm_neon_vpaddlu;
  Ops.push_back(DAG.getConstant(Opcode, dl,
                                TLI.getPointerTy(DAG.getDataLayout())));
  EVT ElemTy = N00.getValueType().getVectorElementType();
  unsigned NumElts = VT.getVectorNumElements();
  EVT ConcatVT = EVT::getVectorVT(*DAG.getContext(), ElemTy, NumElts * 2);
  SDValue Concat = DAG.getNode(ISD::CONCAT_VECTORS, SDLoc(N), ConcatVT,
                               N00.getOperand(0), N00.getOperand(1));
  Ops.push_back(Concat);

  return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, VT, Ops);
}

// FIXME: This function shouldn't be necessary; if we lower BUILD_VECTOR in
// an appropriate manner, we end up with ADD(VUZP(ZEXT(N))), which is
// much easier to match.
static SDValue
AddCombineBUILD_VECTORToVPADDL(SDNode *N, SDValue N0, SDValue N1,
                               TargetLowering::DAGCombinerInfo &DCI,
                               const ARMSubtarget *Subtarget) {
  // Only perform optimization if after legalize, and if NEON is available. We
  // also expected both operands to be BUILD_VECTORs.
  if (DCI.isBeforeLegalize() || !Subtarget->hasNEON()
      || N0.getOpcode() != ISD::BUILD_VECTOR
      || N1.getOpcode() != ISD::BUILD_VECTOR)
    return SDValue();

  // Check output type since VPADDL operand elements can only be 8, 16, or 32.
  EVT VT = N->getValueType(0);
  if (!VT.isInteger() || VT.getVectorElementType() == MVT::i64)
    return SDValue();

  // Check that the vector operands are of the right form.
  // N0 and N1 are BUILD_VECTOR nodes with N number of EXTRACT_VECTOR
  // operands, where N is the size of the formed vector.
  // Each EXTRACT_VECTOR should have the same input vector and odd or even
  // index such that we have a pair wise add pattern.

  // Grab the vector that all EXTRACT_VECTOR nodes should be referencing.
  if (N0->getOperand(0)->getOpcode() != ISD::EXTRACT_VECTOR_ELT)
    return SDValue();
  SDValue Vec = N0->getOperand(0)->getOperand(0);
  SDNode *V = Vec.getNode();
  unsigned nextIndex = 0;

  // For each operands to the ADD which are BUILD_VECTORs,
  // check to see if each of their operands are an EXTRACT_VECTOR with
  // the same vector and appropriate index.
  for (unsigned i = 0, e = N0->getNumOperands(); i != e; ++i) {
    if (N0->getOperand(i)->getOpcode() == ISD::EXTRACT_VECTOR_ELT
        && N1->getOperand(i)->getOpcode() == ISD::EXTRACT_VECTOR_ELT) {

      SDValue ExtVec0 = N0->getOperand(i);
      SDValue ExtVec1 = N1->getOperand(i);

      // First operand is the vector, verify its the same.
      if (V != ExtVec0->getOperand(0).getNode() ||
          V != ExtVec1->getOperand(0).getNode())
        return SDValue();

      // Second is the constant, verify its correct.
      ConstantSDNode *C0 = dyn_cast<ConstantSDNode>(ExtVec0->getOperand(1));
      ConstantSDNode *C1 = dyn_cast<ConstantSDNode>(ExtVec1->getOperand(1));

      // For the constant, we want to see all the even or all the odd.
      if (!C0 || !C1 || C0->getZExtValue() != nextIndex
          || C1->getZExtValue() != nextIndex+1)
        return SDValue();

      // Increment index.
      nextIndex+=2;
    } else
      return SDValue();
  }

  // Don't generate vpaddl+vmovn; we'll match it to vpadd later. Also make sure
  // we're using the entire input vector, otherwise there's a size/legality
  // mismatch somewhere.
  if (nextIndex != Vec.getValueType().getVectorNumElements() ||
      Vec.getValueType().getVectorElementType() == VT.getVectorElementType())
    return SDValue();

  // Create VPADDL node.
  SelectionDAG &DAG = DCI.DAG;
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();

  SDLoc dl(N);

  // Build operand list.
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(DAG.getConstant(Intrinsic::arm_neon_vpaddls, dl,
                                TLI.getPointerTy(DAG.getDataLayout())));

  // Input is the vector.
  Ops.push_back(Vec);

  // Get widened type and narrowed type.
  MVT widenType;
  unsigned numElem = VT.getVectorNumElements();

  EVT inputLaneType = Vec.getValueType().getVectorElementType();
  switch (inputLaneType.getSimpleVT().SimpleTy) {
    case MVT::i8: widenType = MVT::getVectorVT(MVT::i16, numElem); break;
    case MVT::i16: widenType = MVT::getVectorVT(MVT::i32, numElem); break;
    case MVT::i32: widenType = MVT::getVectorVT(MVT::i64, numElem); break;
    default:
      llvm_unreachable("Invalid vector element type for padd optimization.");
  }

  SDValue tmp = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, dl, widenType, Ops);
  unsigned ExtOp = VT.bitsGT(tmp.getValueType()) ? ISD::ANY_EXTEND : ISD::TRUNCATE;
  return DAG.getNode(ExtOp, dl, VT, tmp);
}

static SDValue findMUL_LOHI(SDValue V) {
  if (V->getOpcode() == ISD::UMUL_LOHI ||
      V->getOpcode() == ISD::SMUL_LOHI)
    return V;
  return SDValue();
}

static SDValue AddCombineTo64BitSMLAL16(SDNode *AddcNode, SDNode *AddeNode,
                                        TargetLowering::DAGCombinerInfo &DCI,
                                        const ARMSubtarget *Subtarget) {
  if (!Subtarget->hasBaseDSP())
    return SDValue();

  // SMLALBB, SMLALBT, SMLALTB, SMLALTT multiply two 16-bit values and
  // accumulates the product into a 64-bit value. The 16-bit values will
  // be sign extended somehow or SRA'd into 32-bit values
  // (addc (adde (mul 16bit, 16bit), lo), hi)
  SDValue Mul = AddcNode->getOperand(0);
  SDValue Lo = AddcNode->getOperand(1);
  if (Mul.getOpcode() != ISD::MUL) {
    Lo = AddcNode->getOperand(0);
    Mul = AddcNode->getOperand(1);
    if (Mul.getOpcode() != ISD::MUL)
      return SDValue();
  }

  SDValue SRA = AddeNode->getOperand(0);
  SDValue Hi = AddeNode->getOperand(1);
  if (SRA.getOpcode() != ISD::SRA) {
    SRA = AddeNode->getOperand(1);
    Hi = AddeNode->getOperand(0);
    if (SRA.getOpcode() != ISD::SRA)
      return SDValue();
  }
  if (auto Const = dyn_cast<ConstantSDNode>(SRA.getOperand(1))) {
    if (Const->getZExtValue() != 31)
      return SDValue();
  } else
    return SDValue();

  if (SRA.getOperand(0) != Mul)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(AddcNode);
  unsigned Opcode = 0;
  SDValue Op0;
  SDValue Op1;

  if (isS16(Mul.getOperand(0), DAG) && isS16(Mul.getOperand(1), DAG)) {
    Opcode = ARMISD::SMLALBB;
    Op0 = Mul.getOperand(0);
    Op1 = Mul.getOperand(1);
  } else if (isS16(Mul.getOperand(0), DAG) && isSRA16(Mul.getOperand(1))) {
    Opcode = ARMISD::SMLALBT;
    Op0 = Mul.getOperand(0);
    Op1 = Mul.getOperand(1).getOperand(0);
  } else if (isSRA16(Mul.getOperand(0)) && isS16(Mul.getOperand(1), DAG)) {
    Opcode = ARMISD::SMLALTB;
    Op0 = Mul.getOperand(0).getOperand(0);
    Op1 = Mul.getOperand(1);
  } else if (isSRA16(Mul.getOperand(0)) && isSRA16(Mul.getOperand(1))) {
    Opcode = ARMISD::SMLALTT;
    Op0 = Mul->getOperand(0).getOperand(0);
    Op1 = Mul->getOperand(1).getOperand(0);
  }

  if (!Op0 || !Op1)
    return SDValue();

  SDValue SMLAL = DAG.getNode(Opcode, dl, DAG.getVTList(MVT::i32, MVT::i32),
                              Op0, Op1, Lo, Hi);
  // Replace the ADDs' nodes uses by the MLA node's values.
  SDValue HiMLALResult(SMLAL.getNode(), 1);
  SDValue LoMLALResult(SMLAL.getNode(), 0);

  DAG.ReplaceAllUsesOfValueWith(SDValue(AddcNode, 0), LoMLALResult);
  DAG.ReplaceAllUsesOfValueWith(SDValue(AddeNode, 0), HiMLALResult);

  // Return original node to notify the driver to stop replacing.
  SDValue resNode(AddcNode, 0);
  return resNode;
}

static SDValue AddCombineTo64bitMLAL(SDNode *AddeSubeNode,
                                     TargetLowering::DAGCombinerInfo &DCI,
                                     const ARMSubtarget *Subtarget) {
  // Look for multiply add opportunities.
  // The pattern is a ISD::UMUL_LOHI followed by two add nodes, where
  // each add nodes consumes a value from ISD::UMUL_LOHI and there is
  // a glue link from the first add to the second add.
  // If we find this pattern, we can replace the U/SMUL_LOHI, ADDC, and ADDE by
  // a S/UMLAL instruction.
  //                  UMUL_LOHI
  //                 / :lo    \ :hi
  //                V          \          [no multiline comment]
  //    loAdd ->  ADDC         |
  //                 \ :carry /
  //                  V      V
  //                    ADDE   <- hiAdd
  //
  // In the special case where only the higher part of a signed result is used
  // and the add to the low part of the result of ISD::UMUL_LOHI adds or subtracts
  // a constant with the exact value of 0x80000000, we recognize we are dealing
  // with a "rounded multiply and add" (or subtract) and transform it into
  // either a ARMISD::SMMLAR or ARMISD::SMMLSR respectively.

  assert((AddeSubeNode->getOpcode() == ARMISD::ADDE ||
          AddeSubeNode->getOpcode() == ARMISD::SUBE) &&
         "Expect an ADDE or SUBE");

  assert(AddeSubeNode->getNumOperands() == 3 &&
         AddeSubeNode->getOperand(2).getValueType() == MVT::i32 &&
         "ADDE node has the wrong inputs");

  // Check that we are chained to the right ADDC or SUBC node.
  SDNode *AddcSubcNode = AddeSubeNode->getOperand(2).getNode();
  if ((AddeSubeNode->getOpcode() == ARMISD::ADDE &&
       AddcSubcNode->getOpcode() != ARMISD::ADDC) ||
      (AddeSubeNode->getOpcode() == ARMISD::SUBE &&
       AddcSubcNode->getOpcode() != ARMISD::SUBC))
    return SDValue();

  SDValue AddcSubcOp0 = AddcSubcNode->getOperand(0);
  SDValue AddcSubcOp1 = AddcSubcNode->getOperand(1);

  // Check if the two operands are from the same mul_lohi node.
  if (AddcSubcOp0.getNode() == AddcSubcOp1.getNode())
    return SDValue();

  assert(AddcSubcNode->getNumValues() == 2 &&
         AddcSubcNode->getValueType(0) == MVT::i32 &&
         "Expect ADDC with two result values. First: i32");

  // Check that the ADDC adds the low result of the S/UMUL_LOHI. If not, it
  // maybe a SMLAL which multiplies two 16-bit values.
  if (AddeSubeNode->getOpcode() == ARMISD::ADDE &&
      AddcSubcOp0->getOpcode() != ISD::UMUL_LOHI &&
      AddcSubcOp0->getOpcode() != ISD::SMUL_LOHI &&
      AddcSubcOp1->getOpcode() != ISD::UMUL_LOHI &&
      AddcSubcOp1->getOpcode() != ISD::SMUL_LOHI)
    return AddCombineTo64BitSMLAL16(AddcSubcNode, AddeSubeNode, DCI, Subtarget);

  // Check for the triangle shape.
  SDValue AddeSubeOp0 = AddeSubeNode->getOperand(0);
  SDValue AddeSubeOp1 = AddeSubeNode->getOperand(1);

  // Make sure that the ADDE/SUBE operands are not coming from the same node.
  if (AddeSubeOp0.getNode() == AddeSubeOp1.getNode())
    return SDValue();

  // Find the MUL_LOHI node walking up ADDE/SUBE's operands.
  bool IsLeftOperandMUL = false;
  SDValue MULOp = findMUL_LOHI(AddeSubeOp0);
  if (MULOp == SDValue())
    MULOp = findMUL_LOHI(AddeSubeOp1);
  else
    IsLeftOperandMUL = true;
  if (MULOp == SDValue())
    return SDValue();

  // Figure out the right opcode.
  unsigned Opc = MULOp->getOpcode();
  unsigned FinalOpc = (Opc == ISD::SMUL_LOHI) ? ARMISD::SMLAL : ARMISD::UMLAL;

  // Figure out the high and low input values to the MLAL node.
  SDValue *HiAddSub = nullptr;
  SDValue *LoMul = nullptr;
  SDValue *LowAddSub = nullptr;

  // Ensure that ADDE/SUBE is from high result of ISD::xMUL_LOHI.
  if ((AddeSubeOp0 != MULOp.getValue(1)) && (AddeSubeOp1 != MULOp.getValue(1)))
    return SDValue();

  if (IsLeftOperandMUL)
    HiAddSub = &AddeSubeOp1;
  else
    HiAddSub = &AddeSubeOp0;

  // Ensure that LoMul and LowAddSub are taken from correct ISD::SMUL_LOHI node
  // whose low result is fed to the ADDC/SUBC we are checking.

  if (AddcSubcOp0 == MULOp.getValue(0)) {
    LoMul = &AddcSubcOp0;
    LowAddSub = &AddcSubcOp1;
  }
  if (AddcSubcOp1 == MULOp.getValue(0)) {
    LoMul = &AddcSubcOp1;
    LowAddSub = &AddcSubcOp0;
  }

  if (!LoMul)
    return SDValue();

  // If HiAddSub is the same node as ADDC/SUBC or is a predecessor of ADDC/SUBC
  // the replacement below will create a cycle.
  if (AddcSubcNode == HiAddSub->getNode() ||
      AddcSubcNode->isPredecessorOf(HiAddSub->getNode()))
    return SDValue();

  // Create the merged node.
  SelectionDAG &DAG = DCI.DAG;

  // Start building operand list.
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(LoMul->getOperand(0));
  Ops.push_back(LoMul->getOperand(1));

  // Check whether we can use SMMLAR, SMMLSR or SMMULR instead.  For this to be
  // the case, we must be doing signed multiplication and only use the higher
  // part of the result of the MLAL, furthermore the LowAddSub must be a constant
  // addition or subtraction with the value of 0x800000.
  if (Subtarget->hasV6Ops() && Subtarget->hasDSP() && Subtarget->useMulOps() &&
      FinalOpc == ARMISD::SMLAL && !AddeSubeNode->hasAnyUseOfValue(1) &&
      LowAddSub->getNode()->getOpcode() == ISD::Constant &&
      static_cast<ConstantSDNode *>(LowAddSub->getNode())->getZExtValue() ==
          0x80000000) {
    Ops.push_back(*HiAddSub);
    if (AddcSubcNode->getOpcode() == ARMISD::SUBC) {
      FinalOpc = ARMISD::SMMLSR;
    } else {
      FinalOpc = ARMISD::SMMLAR;
    }
    SDValue NewNode = DAG.getNode(FinalOpc, SDLoc(AddcSubcNode), MVT::i32, Ops);
    DAG.ReplaceAllUsesOfValueWith(SDValue(AddeSubeNode, 0), NewNode);

    return SDValue(AddeSubeNode, 0);
  } else if (AddcSubcNode->getOpcode() == ARMISD::SUBC)
    // SMMLS is generated during instruction selection and the rest of this
    // function can not handle the case where AddcSubcNode is a SUBC.
    return SDValue();

  // Finish building the operand list for {U/S}MLAL
  Ops.push_back(*LowAddSub);
  Ops.push_back(*HiAddSub);

  SDValue MLALNode = DAG.getNode(FinalOpc, SDLoc(AddcSubcNode),
                                 DAG.getVTList(MVT::i32, MVT::i32), Ops);

  // Replace the ADDs' nodes uses by the MLA node's values.
  SDValue HiMLALResult(MLALNode.getNode(), 1);
  DAG.ReplaceAllUsesOfValueWith(SDValue(AddeSubeNode, 0), HiMLALResult);

  SDValue LoMLALResult(MLALNode.getNode(), 0);
  DAG.ReplaceAllUsesOfValueWith(SDValue(AddcSubcNode, 0), LoMLALResult);

  // Return original node to notify the driver to stop replacing.
  return SDValue(AddeSubeNode, 0);
}

static SDValue AddCombineTo64bitUMAAL(SDNode *AddeNode,
                                      TargetLowering::DAGCombinerInfo &DCI,
                                      const ARMSubtarget *Subtarget) {
  // UMAAL is similar to UMLAL except that it adds two unsigned values.
  // While trying to combine for the other MLAL nodes, first search for the
  // chance to use UMAAL. Check if Addc uses a node which has already
  // been combined into a UMLAL. The other pattern is UMLAL using Addc/Adde
  // as the addend, and it's handled in PerformUMLALCombine.

  if (!Subtarget->hasV6Ops() || !Subtarget->hasDSP())
    return AddCombineTo64bitMLAL(AddeNode, DCI, Subtarget);

  // Check that we have a glued ADDC node.
  SDNode* AddcNode = AddeNode->getOperand(2).getNode();
  if (AddcNode->getOpcode() != ARMISD::ADDC)
    return SDValue();

  // Find the converted UMAAL or quit if it doesn't exist.
  SDNode *UmlalNode = nullptr;
  SDValue AddHi;
  if (AddcNode->getOperand(0).getOpcode() == ARMISD::UMLAL) {
    UmlalNode = AddcNode->getOperand(0).getNode();
    AddHi = AddcNode->getOperand(1);
  } else if (AddcNode->getOperand(1).getOpcode() == ARMISD::UMLAL) {
    UmlalNode = AddcNode->getOperand(1).getNode();
    AddHi = AddcNode->getOperand(0);
  } else {
    return AddCombineTo64bitMLAL(AddeNode, DCI, Subtarget);
  }

  // The ADDC should be glued to an ADDE node, which uses the same UMLAL as
  // the ADDC as well as Zero.
  if (!isNullConstant(UmlalNode->getOperand(3)))
    return SDValue();

  if ((isNullConstant(AddeNode->getOperand(0)) &&
       AddeNode->getOperand(1).getNode() == UmlalNode) ||
      (AddeNode->getOperand(0).getNode() == UmlalNode &&
       isNullConstant(AddeNode->getOperand(1)))) {
    SelectionDAG &DAG = DCI.DAG;
    SDValue Ops[] = { UmlalNode->getOperand(0), UmlalNode->getOperand(1),
                      UmlalNode->getOperand(2), AddHi };
    SDValue UMAAL =  DAG.getNode(ARMISD::UMAAL, SDLoc(AddcNode),
                                 DAG.getVTList(MVT::i32, MVT::i32), Ops);

    // Replace the ADDs' nodes uses by the UMAAL node's values.
    DAG.ReplaceAllUsesOfValueWith(SDValue(AddeNode, 0), SDValue(UMAAL.getNode(), 1));
    DAG.ReplaceAllUsesOfValueWith(SDValue(AddcNode, 0), SDValue(UMAAL.getNode(), 0));

    // Return original node to notify the driver to stop replacing.
    return SDValue(AddeNode, 0);
  }
  return SDValue();
}

static SDValue PerformUMLALCombine(SDNode *N, SelectionDAG &DAG,
                                   const ARMSubtarget *Subtarget) {
  if (!Subtarget->hasV6Ops() || !Subtarget->hasDSP())
    return SDValue();

  // Check that we have a pair of ADDC and ADDE as operands.
  // Both addends of the ADDE must be zero.
  SDNode* AddcNode = N->getOperand(2).getNode();
  SDNode* AddeNode = N->getOperand(3).getNode();
  if ((AddcNode->getOpcode() == ARMISD::ADDC) &&
      (AddeNode->getOpcode() == ARMISD::ADDE) &&
      isNullConstant(AddeNode->getOperand(0)) &&
      isNullConstant(AddeNode->getOperand(1)) &&
      (AddeNode->getOperand(2).getNode() == AddcNode))
    return DAG.getNode(ARMISD::UMAAL, SDLoc(N),
                       DAG.getVTList(MVT::i32, MVT::i32),
                       {N->getOperand(0), N->getOperand(1),
                        AddcNode->getOperand(0), AddcNode->getOperand(1)});
  else
    return SDValue();
}

static SDValue PerformAddcSubcCombine(SDNode *N,
                                      TargetLowering::DAGCombinerInfo &DCI,
                                      const ARMSubtarget *Subtarget) {
  SelectionDAG &DAG(DCI.DAG);

  if (N->getOpcode() == ARMISD::SUBC && N->hasAnyUseOfValue(1)) {
    // (SUBC (ADDE 0, 0, C), 1) -> C
    SDValue LHS = N->getOperand(0);
    SDValue RHS = N->getOperand(1);
    if (LHS->getOpcode() == ARMISD::ADDE &&
        isNullConstant(LHS->getOperand(0)) &&
        isNullConstant(LHS->getOperand(1)) && isOneConstant(RHS)) {
      return DCI.CombineTo(N, SDValue(N, 0), LHS->getOperand(2));
    }
  }

  if (Subtarget->isThumb1Only()) {
    SDValue RHS = N->getOperand(1);
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(RHS)) {
      int32_t imm = C->getSExtValue();
      if (imm < 0 && imm > std::numeric_limits<int>::min()) {
        SDLoc DL(N);
        RHS = DAG.getConstant(-imm, DL, MVT::i32);
        unsigned Opcode = (N->getOpcode() == ARMISD::ADDC) ? ARMISD::SUBC
                                                           : ARMISD::ADDC;
        return DAG.getNode(Opcode, DL, N->getVTList(), N->getOperand(0), RHS);
      }
    }
  }

  return SDValue();
}

static SDValue PerformAddeSubeCombine(SDNode *N,
                                      TargetLowering::DAGCombinerInfo &DCI,
                                      const ARMSubtarget *Subtarget) {
  if (Subtarget->isThumb1Only()) {
    SelectionDAG &DAG = DCI.DAG;
    SDValue RHS = N->getOperand(1);
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(RHS)) {
      int64_t imm = C->getSExtValue();
      if (imm < 0) {
        SDLoc DL(N);

        // The with-carry-in form matches bitwise not instead of the negation.
        // Effectively, the inverse interpretation of the carry flag already
        // accounts for part of the negation.
        RHS = DAG.getConstant(~imm, DL, MVT::i32);

        unsigned Opcode = (N->getOpcode() == ARMISD::ADDE) ? ARMISD::SUBE
                                                           : ARMISD::ADDE;
        return DAG.getNode(Opcode, DL, N->getVTList(),
                           N->getOperand(0), RHS, N->getOperand(2));
      }
    }
  } else if (N->getOperand(1)->getOpcode() == ISD::SMUL_LOHI) {
    return AddCombineTo64bitMLAL(N, DCI, Subtarget);
  }
  return SDValue();
}

static SDValue PerformSELECTCombine(SDNode *N,
                                    TargetLowering::DAGCombinerInfo &DCI,
                                    const ARMSubtarget *Subtarget) {
  if (!Subtarget->hasMVEIntegerOps())
    return SDValue();

  SDLoc dl(N);
  SDValue SetCC;
  SDValue LHS;
  SDValue RHS;
  ISD::CondCode CC;
  SDValue TrueVal;
  SDValue FalseVal;

  if (N->getOpcode() == ISD::SELECT &&
      N->getOperand(0)->getOpcode() == ISD::SETCC) {
    SetCC = N->getOperand(0);
    LHS = SetCC->getOperand(0);
    RHS = SetCC->getOperand(1);
    CC = cast<CondCodeSDNode>(SetCC->getOperand(2))->get();
    TrueVal = N->getOperand(1);
    FalseVal = N->getOperand(2);
  } else if (N->getOpcode() == ISD::SELECT_CC) {
    LHS = N->getOperand(0);
    RHS = N->getOperand(1);
    CC = cast<CondCodeSDNode>(N->getOperand(4))->get();
    TrueVal = N->getOperand(2);
    FalseVal = N->getOperand(3);
  } else {
    return SDValue();
  }

  unsigned int Opcode = 0;
  if ((TrueVal->getOpcode() == ISD::VECREDUCE_UMIN ||
       FalseVal->getOpcode() == ISD::VECREDUCE_UMIN) &&
      (CC == ISD::SETULT || CC == ISD::SETUGT)) {
    Opcode = ARMISD::VMINVu;
    if (CC == ISD::SETUGT)
      std::swap(TrueVal, FalseVal);
  } else if ((TrueVal->getOpcode() == ISD::VECREDUCE_SMIN ||
              FalseVal->getOpcode() == ISD::VECREDUCE_SMIN) &&
             (CC == ISD::SETLT || CC == ISD::SETGT)) {
    Opcode = ARMISD::VMINVs;
    if (CC == ISD::SETGT)
      std::swap(TrueVal, FalseVal);
  } else if ((TrueVal->getOpcode() == ISD::VECREDUCE_UMAX ||
              FalseVal->getOpcode() == ISD::VECREDUCE_UMAX) &&
             (CC == ISD::SETUGT || CC == ISD::SETULT)) {
    Opcode = ARMISD::VMAXVu;
    if (CC == ISD::SETULT)
      std::swap(TrueVal, FalseVal);
  } else if ((TrueVal->getOpcode() == ISD::VECREDUCE_SMAX ||
              FalseVal->getOpcode() == ISD::VECREDUCE_SMAX) &&
             (CC == ISD::SETGT || CC == ISD::SETLT)) {
    Opcode = ARMISD::VMAXVs;
    if (CC == ISD::SETLT)
      std::swap(TrueVal, FalseVal);
  } else
    return SDValue();

  // Normalise to the right hand side being the vector reduction
  switch (TrueVal->getOpcode()) {
  case ISD::VECREDUCE_UMIN:
  case ISD::VECREDUCE_SMIN:
  case ISD::VECREDUCE_UMAX:
  case ISD::VECREDUCE_SMAX:
    std::swap(LHS, RHS);
    std::swap(TrueVal, FalseVal);
    break;
  }

  EVT VectorType = FalseVal->getOperand(0).getValueType();

  if (VectorType != MVT::v16i8 && VectorType != MVT::v8i16 &&
      VectorType != MVT::v4i32)
    return SDValue();

  EVT VectorScalarType = VectorType.getVectorElementType();

  // The values being selected must also be the ones being compared
  if (TrueVal != LHS || FalseVal != RHS)
    return SDValue();

  EVT LeftType = LHS->getValueType(0);
  EVT RightType = RHS->getValueType(0);

  // The types must match the reduced type too
  if (LeftType != VectorScalarType || RightType != VectorScalarType)
    return SDValue();

  // Legalise the scalar to an i32
  if (VectorScalarType != MVT::i32)
    LHS = DCI.DAG.getNode(ISD::ANY_EXTEND, dl, MVT::i32, LHS);

  // Generate the reduction as an i32 for legalisation purposes
  auto Reduction =
      DCI.DAG.getNode(Opcode, dl, MVT::i32, LHS, RHS->getOperand(0));

  // The result isn't actually an i32 so truncate it back to its original type
  if (VectorScalarType != MVT::i32)
    Reduction = DCI.DAG.getNode(ISD::TRUNCATE, dl, VectorScalarType, Reduction);

  return Reduction;
}

// A special combine for the vqdmulh family of instructions. This is one of the
// potential set of patterns that could patch this instruction. The base pattern
// you would expect to be min(max(ashr(mul(mul(sext(x), 2), sext(y)), 16))).
// This matches the different min(max(ashr(mul(mul(sext(x), sext(y)), 2), 16))),
// which llvm will have optimized to min(ashr(mul(sext(x), sext(y)), 15))) as
// the max is unnecessary.
static SDValue PerformVQDMULHCombine(SDNode *N, SelectionDAG &DAG) {
  EVT VT = N->getValueType(0);
  SDValue Shft;
  ConstantSDNode *Clamp;

  if (!VT.isVector() || VT.getScalarSizeInBits() > 64)
    return SDValue();

  if (N->getOpcode() == ISD::SMIN) {
    Shft = N->getOperand(0);
    Clamp = isConstOrConstSplat(N->getOperand(1));
  } else if (N->getOpcode() == ISD::VSELECT) {
    // Detect a SMIN, which for an i64 node will be a vselect/setcc, not a smin.
    SDValue Cmp = N->getOperand(0);
    if (Cmp.getOpcode() != ISD::SETCC ||
        cast<CondCodeSDNode>(Cmp.getOperand(2))->get() != ISD::SETLT ||
        Cmp.getOperand(0) != N->getOperand(1) ||
        Cmp.getOperand(1) != N->getOperand(2))
      return SDValue();
    Shft = N->getOperand(1);
    Clamp = isConstOrConstSplat(N->getOperand(2));
  } else
    return SDValue();

  if (!Clamp)
    return SDValue();

  MVT ScalarType;
  int ShftAmt = 0;
  switch (Clamp->getSExtValue()) {
  case (1 << 7) - 1:
    ScalarType = MVT::i8;
    ShftAmt = 7;
    break;
  case (1 << 15) - 1:
    ScalarType = MVT::i16;
    ShftAmt = 15;
    break;
  case (1ULL << 31) - 1:
    ScalarType = MVT::i32;
    ShftAmt = 31;
    break;
  default:
    return SDValue();
  }

  if (Shft.getOpcode() != ISD::SRA)
    return SDValue();
  ConstantSDNode *N1 = isConstOrConstSplat(Shft.getOperand(1));
  if (!N1 || N1->getSExtValue() != ShftAmt)
    return SDValue();

  SDValue Mul = Shft.getOperand(0);
  if (Mul.getOpcode() != ISD::MUL)
    return SDValue();

  SDValue Ext0 = Mul.getOperand(0);
  SDValue Ext1 = Mul.getOperand(1);
  if (Ext0.getOpcode() != ISD::SIGN_EXTEND ||
      Ext1.getOpcode() != ISD::SIGN_EXTEND)
    return SDValue();
  EVT VecVT = Ext0.getOperand(0).getValueType();
  if (!VecVT.isPow2VectorType() || VecVT.getVectorNumElements() == 1)
    return SDValue();
  if (Ext1.getOperand(0).getValueType() != VecVT ||
      VecVT.getScalarType() != ScalarType ||
      VT.getScalarSizeInBits() < ScalarType.getScalarSizeInBits() * 2)
    return SDValue();

  SDLoc DL(Mul);
  unsigned LegalLanes = 128 / (ShftAmt + 1);
  EVT LegalVecVT = MVT::getVectorVT(ScalarType, LegalLanes);
  // For types smaller than legal vectors extend to be legal and only use needed
  // lanes.
  if (VecVT.getSizeInBits() < 128) {
    EVT ExtVecVT =
        MVT::getVectorVT(MVT::getIntegerVT(128 / VecVT.getVectorNumElements()),
                         VecVT.getVectorNumElements());
    SDValue Inp0 =
        DAG.getNode(ISD::ANY_EXTEND, DL, ExtVecVT, Ext0.getOperand(0));
    SDValue Inp1 =
        DAG.getNode(ISD::ANY_EXTEND, DL, ExtVecVT, Ext1.getOperand(0));
    Inp0 = DAG.getNode(ARMISD::VECTOR_REG_CAST, DL, LegalVecVT, Inp0);
    Inp1 = DAG.getNode(ARMISD::VECTOR_REG_CAST, DL, LegalVecVT, Inp1);
    SDValue VQDMULH = DAG.getNode(ARMISD::VQDMULH, DL, LegalVecVT, Inp0, Inp1);
    SDValue Trunc = DAG.getNode(ARMISD::VECTOR_REG_CAST, DL, ExtVecVT, VQDMULH);
    Trunc = DAG.getNode(ISD::TRUNCATE, DL, VecVT, Trunc);
    return DAG.getNode(ISD::SIGN_EXTEND, DL, VT, Trunc);
  }

  // For larger types, split into legal sized chunks.
  assert(VecVT.getSizeInBits() % 128 == 0 && "Expected a power2 type");
  unsigned NumParts = VecVT.getSizeInBits() / 128;
  SmallVector<SDValue> Parts;
  for (unsigned I = 0; I < NumParts; ++I) {
    SDValue Inp0 =
        DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, LegalVecVT, Ext0.getOperand(0),
                    DAG.getVectorIdxConstant(I * LegalLanes, DL));
    SDValue Inp1 =
        DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, LegalVecVT, Ext1.getOperand(0),
                    DAG.getVectorIdxConstant(I * LegalLanes, DL));
    SDValue VQDMULH = DAG.getNode(ARMISD::VQDMULH, DL, LegalVecVT, Inp0, Inp1);
    Parts.push_back(VQDMULH);
  }
  return DAG.getNode(ISD::SIGN_EXTEND, DL, VT,
                     DAG.getNode(ISD::CONCAT_VECTORS, DL, VecVT, Parts));
}

static SDValue PerformVSELECTCombine(SDNode *N,
                                     TargetLowering::DAGCombinerInfo &DCI,
                                     const ARMSubtarget *Subtarget) {
  if (!Subtarget->hasMVEIntegerOps())
    return SDValue();

  if (SDValue V = PerformVQDMULHCombine(N, DCI.DAG))
    return V;

  // Transforms vselect(not(cond), lhs, rhs) into vselect(cond, rhs, lhs).
  //
  // We need to re-implement this optimization here as the implementation in the
  // Target-Independent DAGCombiner does not handle the kind of constant we make
  // (it calls isConstOrConstSplat with AllowTruncation set to false - and for
  // good reason, allowing truncation there would break other targets).
  //
  // Currently, this is only done for MVE, as it's the only target that benefits
  // from this transformation (e.g. VPNOT+VPSEL becomes a single VPSEL).
  if (N->getOperand(0).getOpcode() != ISD::XOR)
    return SDValue();
  SDValue XOR = N->getOperand(0);

  // Check if the XOR's RHS is either a 1, or a BUILD_VECTOR of 1s.
  // It is important to check with truncation allowed as the BUILD_VECTORs we
  // generate in those situations will truncate their operands.
  ConstantSDNode *Const =
      isConstOrConstSplat(XOR->getOperand(1), /*AllowUndefs*/ false,
                          /*AllowTruncation*/ true);
  if (!Const || !Const->isOne())
    return SDValue();

  // Rewrite into vselect(cond, rhs, lhs).
  SDValue Cond = XOR->getOperand(0);
  SDValue LHS = N->getOperand(1);
  SDValue RHS = N->getOperand(2);
  EVT Type = N->getValueType(0);
  return DCI.DAG.getNode(ISD::VSELECT, SDLoc(N), Type, Cond, RHS, LHS);
}

// Convert vsetcc([0,1,2,..], splat(n), ult) -> vctp n
static SDValue PerformVSetCCToVCTPCombine(SDNode *N,
                                          TargetLowering::DAGCombinerInfo &DCI,
                                          const ARMSubtarget *Subtarget) {
  SDValue Op0 = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);
  ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(2))->get();
  EVT VT = N->getValueType(0);

  if (!Subtarget->hasMVEIntegerOps() ||
      !DCI.DAG.getTargetLoweringInfo().isTypeLegal(VT))
    return SDValue();

  if (CC == ISD::SETUGE) {
    std::swap(Op0, Op1);
    CC = ISD::SETULT;
  }

  if (CC != ISD::SETULT || VT.getScalarSizeInBits() != 1 ||
      Op0.getOpcode() != ISD::BUILD_VECTOR)
    return SDValue();

  // Check first operand is BuildVector of 0,1,2,...
  for (unsigned I = 0; I < VT.getVectorNumElements(); I++) {
    if (!Op0.getOperand(I).isUndef() &&
        !(isa<ConstantSDNode>(Op0.getOperand(I)) &&
          Op0.getConstantOperandVal(I) == I))
      return SDValue();
  }

  // The second is a Splat of Op1S
  SDValue Op1S = DCI.DAG.getSplatValue(Op1);
  if (!Op1S)
    return SDValue();

  unsigned Opc;
  switch (VT.getVectorNumElements()) {
  case 2:
    Opc = Intrinsic::arm_mve_vctp64;
    break;
  case 4:
    Opc = Intrinsic::arm_mve_vctp32;
    break;
  case 8:
    Opc = Intrinsic::arm_mve_vctp16;
    break;
  case 16:
    Opc = Intrinsic::arm_mve_vctp8;
    break;
  default:
    return SDValue();
  }

  SDLoc DL(N);
  return DCI.DAG.getNode(ISD::INTRINSIC_WO_CHAIN, DL, VT,
                         DCI.DAG.getConstant(Opc, DL, MVT::i32),
                         DCI.DAG.getZExtOrTrunc(Op1S, DL, MVT::i32));
}

/// PerformADDECombine - Target-specific dag combine transform from
/// ARMISD::ADDC, ARMISD::ADDE, and ISD::MUL_LOHI to MLAL or
/// ARMISD::ADDC, ARMISD::ADDE and ARMISD::UMLAL to ARMISD::UMAAL
static SDValue PerformADDECombine(SDNode *N,
                                  TargetLowering::DAGCombinerInfo &DCI,
                                  const ARMSubtarget *Subtarget) {
  // Only ARM and Thumb2 support UMLAL/SMLAL.
  if (Subtarget->isThumb1Only())
    return PerformAddeSubeCombine(N, DCI, Subtarget);

  // Only perform the checks after legalize when the pattern is available.
  if (DCI.isBeforeLegalize()) return SDValue();

  return AddCombineTo64bitUMAAL(N, DCI, Subtarget);
}

/// PerformADDCombineWithOperands - Try DAG combinations for an ADD with
/// operands N0 and N1.  This is a helper for PerformADDCombine that is
/// called with the default operands, and if that fails, with commuted
/// operands.
static SDValue PerformADDCombineWithOperands(SDNode *N, SDValue N0, SDValue N1,
                                          TargetLowering::DAGCombinerInfo &DCI,
                                          const ARMSubtarget *Subtarget){
  // Attempt to create vpadd for this add.
  if (SDValue Result = AddCombineToVPADD(N, N0, N1, DCI, Subtarget))
    return Result;

  // Attempt to create vpaddl for this add.
  if (SDValue Result = AddCombineVUZPToVPADDL(N, N0, N1, DCI, Subtarget))
    return Result;
  if (SDValue Result = AddCombineBUILD_VECTORToVPADDL(N, N0, N1, DCI,
                                                      Subtarget))
    return Result;

  // fold (add (select cc, 0, c), x) -> (select cc, x, (add, x, c))
  if (N0.getNode()->hasOneUse())
    if (SDValue Result = combineSelectAndUse(N, N0, N1, DCI))
      return Result;
  return SDValue();
}

static SDValue TryDistrubutionADDVecReduce(SDNode *N, SelectionDAG &DAG) {
  EVT VT = N->getValueType(0);
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDLoc dl(N);

  auto IsVecReduce = [](SDValue Op) {
    switch (Op.getOpcode()) {
    case ISD::VECREDUCE_ADD:
    case ARMISD::VADDVs:
    case ARMISD::VADDVu:
    case ARMISD::VMLAVs:
    case ARMISD::VMLAVu:
      return true;
    }
    return false;
  };

  auto DistrubuteAddAddVecReduce = [&](SDValue N0, SDValue N1) {
    // Distribute add(X, add(vecreduce(Y), vecreduce(Z))) ->
    //   add(add(X, vecreduce(Y)), vecreduce(Z))
    // to make better use of vaddva style instructions.
    if (VT == MVT::i32 && N1.getOpcode() == ISD::ADD && !IsVecReduce(N0) &&
        IsVecReduce(N1.getOperand(0)) && IsVecReduce(N1.getOperand(1)) &&
        !isa<ConstantSDNode>(N0) && N1->hasOneUse()) {
      SDValue Add0 = DAG.getNode(ISD::ADD, dl, VT, N0, N1.getOperand(0));
      return DAG.getNode(ISD::ADD, dl, VT, Add0, N1.getOperand(1));
    }
    // And turn add(add(A, reduce(B)), add(C, reduce(D))) ->
    //   add(add(add(A, C), reduce(B)), reduce(D))
    if (VT == MVT::i32 && N0.getOpcode() == ISD::ADD &&
        N1.getOpcode() == ISD::ADD && N0->hasOneUse() && N1->hasOneUse()) {
      unsigned N0RedOp = 0;
      if (!IsVecReduce(N0.getOperand(N0RedOp))) {
        N0RedOp = 1;
        if (!IsVecReduce(N0.getOperand(N0RedOp)))
          return SDValue();
      }

      unsigned N1RedOp = 0;
      if (!IsVecReduce(N1.getOperand(N1RedOp)))
        N1RedOp = 1;
      if (!IsVecReduce(N1.getOperand(N1RedOp)))
        return SDValue();

      SDValue Add0 = DAG.getNode(ISD::ADD, dl, VT, N0.getOperand(1 - N0RedOp),
                                 N1.getOperand(1 - N1RedOp));
      SDValue Add1 =
          DAG.getNode(ISD::ADD, dl, VT, Add0, N0.getOperand(N0RedOp));
      return DAG.getNode(ISD::ADD, dl, VT, Add1, N1.getOperand(N1RedOp));
    }
    return SDValue();
  };
  if (SDValue R = DistrubuteAddAddVecReduce(N0, N1))
    return R;
  if (SDValue R = DistrubuteAddAddVecReduce(N1, N0))
    return R;

  // Distribute add(vecreduce(load(Y)), vecreduce(load(Z)))
  // Or add(add(X, vecreduce(load(Y))), vecreduce(load(Z)))
  // by ascending load offsets. This can help cores prefetch if the order of
  // loads is more predictable.
  auto DistrubuteVecReduceLoad = [&](SDValue N0, SDValue N1, bool IsForward) {
    // Check if two reductions are known to load data where one is before/after
    // another. Return negative if N0 loads data before N1, positive if N1 is
    // before N0 and 0 otherwise if nothing is known.
    auto IsKnownOrderedLoad = [&](SDValue N0, SDValue N1) {
      // Look through to the first operand of a MUL, for the VMLA case.
      // Currently only looks at the first operand, in the hope they are equal.
      if (N0.getOpcode() == ISD::MUL)
        N0 = N0.getOperand(0);
      if (N1.getOpcode() == ISD::MUL)
        N1 = N1.getOperand(0);

      // Return true if the two operands are loads to the same object and the
      // offset of the first is known to be less than the offset of the second.
      LoadSDNode *Load0 = dyn_cast<LoadSDNode>(N0);
      LoadSDNode *Load1 = dyn_cast<LoadSDNode>(N1);
      if (!Load0 || !Load1 || Load0->getChain() != Load1->getChain() ||
          !Load0->isSimple() || !Load1->isSimple() || Load0->isIndexed() ||
          Load1->isIndexed())
        return 0;

      auto BaseLocDecomp0 = BaseIndexOffset::match(Load0, DAG);
      auto BaseLocDecomp1 = BaseIndexOffset::match(Load1, DAG);

      if (!BaseLocDecomp0.getBase() ||
          BaseLocDecomp0.getBase() != BaseLocDecomp1.getBase() ||
          !BaseLocDecomp0.hasValidOffset() || !BaseLocDecomp1.hasValidOffset())
        return 0;
      if (BaseLocDecomp0.getOffset() < BaseLocDecomp1.getOffset())
        return -1;
      if (BaseLocDecomp0.getOffset() > BaseLocDecomp1.getOffset())
        return 1;
      return 0;
    };

    SDValue X;
    if (N0.getOpcode() == ISD::ADD && N0->hasOneUse()) {
      if (IsVecReduce(N0.getOperand(0)) && IsVecReduce(N0.getOperand(1))) {
        int IsBefore = IsKnownOrderedLoad(N0.getOperand(0).getOperand(0),
                                         N0.getOperand(1).getOperand(0));
        if (IsBefore < 0) {
          X = N0.getOperand(0);
          N0 = N0.getOperand(1);
        } else if (IsBefore > 0) {
          X = N0.getOperand(1);
          N0 = N0.getOperand(0);
        } else
          return SDValue();
      } else if (IsVecReduce(N0.getOperand(0))) {
        X = N0.getOperand(1);
        N0 = N0.getOperand(0);
      } else if (IsVecReduce(N0.getOperand(1))) {
        X = N0.getOperand(0);
        N0 = N0.getOperand(1);
      } else
        return SDValue();
    } else if (IsForward && IsVecReduce(N0) && IsVecReduce(N1) &&
               IsKnownOrderedLoad(N0.getOperand(0), N1.getOperand(0)) < 0) {
      // Note this is backward to how you would expect. We create
      // add(reduce(load + 16), reduce(load + 0)) so that the
      // add(reduce(load+16), X) is combined into VADDVA(X, load+16)), leaving
      // the X as VADDV(load + 0)
      return DAG.getNode(ISD::ADD, dl, VT, N1, N0);
    } else
      return SDValue();

    if (!IsVecReduce(N0) || !IsVecReduce(N1))
      return SDValue();

    if (IsKnownOrderedLoad(N1.getOperand(0), N0.getOperand(0)) >= 0)
      return SDValue();

    // Switch from add(add(X, N0), N1) to add(add(X, N1), N0)
    SDValue Add0 = DAG.getNode(ISD::ADD, dl, VT, X, N1);
    return DAG.getNode(ISD::ADD, dl, VT, Add0, N0);
  };
  if (SDValue R = DistrubuteVecReduceLoad(N0, N1, true))
    return R;
  if (SDValue R = DistrubuteVecReduceLoad(N1, N0, false))
    return R;
  return SDValue();
}

static SDValue PerformADDVecReduce(SDNode *N, SelectionDAG &DAG,
                                   const ARMSubtarget *Subtarget) {
  if (!Subtarget->hasMVEIntegerOps())
    return SDValue();

  if (SDValue R = TryDistrubutionADDVecReduce(N, DAG))
    return R;

  EVT VT = N->getValueType(0);
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SDLoc dl(N);

  if (VT != MVT::i64)
    return SDValue();

  // We are looking for a i64 add of a VADDLVx. Due to these being i64's, this
  // will look like:
  //   t1: i32,i32 = ARMISD::VADDLVs x
  //   t2: i64 = build_pair t1, t1:1
  //   t3: i64 = add t2, y
  // Otherwise we try to push the add up above VADDLVAx, to potentially allow
  // the add to be simplified separately.
  // We also need to check for sext / zext and commutitive adds.
  auto MakeVecReduce = [&](unsigned Opcode, unsigned OpcodeA, SDValue NA,
                           SDValue NB) {
    if (NB->getOpcode() != ISD::BUILD_PAIR)
      return SDValue();
    SDValue VecRed = NB->getOperand(0);
    if ((VecRed->getOpcode() != Opcode && VecRed->getOpcode() != OpcodeA) ||
        VecRed.getResNo() != 0 ||
        NB->getOperand(1) != SDValue(VecRed.getNode(), 1))
      return SDValue();

    if (VecRed->getOpcode() == OpcodeA) {
      // add(NA, VADDLVA(Inp), Y) -> VADDLVA(add(NA, Inp), Y)
      SDValue Inp = DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64,
                                VecRed.getOperand(0), VecRed.getOperand(1));
      NA = DAG.getNode(ISD::ADD, dl, MVT::i64, Inp, NA);
    }

    SmallVector<SDValue, 4> Ops(2);
    std::tie(Ops[0], Ops[1]) = DAG.SplitScalar(NA, dl, MVT::i32, MVT::i32);

    unsigned S = VecRed->getOpcode() == OpcodeA ? 2 : 0;
    for (unsigned I = S, E = VecRed.getNumOperands(); I < E; I++)
      Ops.push_back(VecRed->getOperand(I));
    SDValue Red =
        DAG.getNode(OpcodeA, dl, DAG.getVTList({MVT::i32, MVT::i32}), Ops);
    return DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, Red,
                       SDValue(Red.getNode(), 1));
  };

  if (SDValue M = MakeVecReduce(ARMISD::VADDLVs, ARMISD::VADDLVAs, N0, N1))
    return M;
  if (SDValue M = MakeVecReduce(ARMISD::VADDLVu, ARMISD::VADDLVAu, N0, N1))
    return M;
  if (SDValue M = MakeVecReduce(ARMISD::VADDLVs, ARMISD::VADDLVAs, N1, N0))
    return M;
  if (SDValue M = MakeVecReduce(ARMISD::VADDLVu, ARMISD::VADDLVAu, N1, N0))
    return M;
  if (SDValue M = MakeVecReduce(ARMISD::VADDLVps, ARMISD::VADDLVAps, N0, N1))
    return M;
  if (SDValue M = MakeVecReduce(ARMISD::VADDLVpu, ARMISD::VADDLVApu, N0, N1))
    return M;
  if (SDValue M = MakeVecReduce(ARMISD::VADDLVps, ARMISD::VADDLVAps, N1, N0))
    return M;
  if (SDValue M = MakeVecReduce(ARMISD::VADDLVpu, ARMISD::VADDLVApu, N1, N0))
    return M;
  if (SDValue M = MakeVecReduce(ARMISD::VMLALVs, ARMISD::VMLALVAs, N0, N1))
    return M;
  if (SDValue M = MakeVecReduce(ARMISD::VMLALVu, ARMISD::VMLALVAu, N0, N1))
    return M;
  if (SDValue M = MakeVecReduce(ARMISD::VMLALVs, ARMISD::VMLALVAs, N1, N0))
    return M;
  if (SDValue M = MakeVecReduce(ARMISD::VMLALVu, ARMISD::VMLALVAu, N1, N0))
    return M;
  if (SDValue M = MakeVecReduce(ARMISD::VMLALVps, ARMISD::VMLALVAps, N0, N1))
    return M;
  if (SDValue M = MakeVecReduce(ARMISD::VMLALVpu, ARMISD::VMLALVApu, N0, N1))
    return M;
  if (SDValue M = MakeVecReduce(ARMISD::VMLALVps, ARMISD::VMLALVAps, N1, N0))
    return M;
  if (SDValue M = MakeVecReduce(ARMISD::VMLALVpu, ARMISD::VMLALVApu, N1, N0))
    return M;
  return SDValue();
}

bool
ARMTargetLowering::isDesirableToCommuteWithShift(const SDNode *N,
                                                 CombineLevel Level) const {
  assert((N->getOpcode() == ISD::SHL || N->getOpcode() == ISD::SRA ||
          N->getOpcode() == ISD::SRL) &&
         "Expected shift op");

  if (Level == BeforeLegalizeTypes)
    return true;

  if (N->getOpcode() != ISD::SHL)
    return true;

  if (Subtarget->isThumb1Only()) {
    // Avoid making expensive immediates by commuting shifts. (This logic
    // only applies to Thumb1 because ARM and Thumb2 immediates can be shifted
    // for free.)
    if (N->getOpcode() != ISD::SHL)
      return true;
    SDValue N1 = N->getOperand(0);
    if (N1->getOpcode() != ISD::ADD && N1->getOpcode() != ISD::AND &&
        N1->getOpcode() != ISD::OR && N1->getOpcode() != ISD::XOR)
      return true;
    if (auto *Const = dyn_cast<ConstantSDNode>(N1->getOperand(1))) {
      if (Const->getAPIntValue().ult(256))
        return false;
      if (N1->getOpcode() == ISD::ADD && Const->getAPIntValue().slt(0) &&
          Const->getAPIntValue().sgt(-256))
        return false;
    }
    return true;
  }

  // Turn off commute-with-shift transform after legalization, so it doesn't
  // conflict with PerformSHLSimplify.  (We could try to detect when
  // PerformSHLSimplify would trigger more precisely, but it isn't
  // really necessary.)
  return false;
}

bool ARMTargetLowering::isDesirableToCommuteXorWithShift(
    const SDNode *N) const {
  assert(N->getOpcode() == ISD::XOR &&
         (N->getOperand(0).getOpcode() == ISD::SHL ||
          N->getOperand(0).getOpcode() == ISD::SRL) &&
         "Expected XOR(SHIFT) pattern");

  // Only commute if the entire NOT mask is a hidden shifted mask.
  auto *XorC = dyn_cast<ConstantSDNode>(N->getOperand(1));
  auto *ShiftC = dyn_cast<ConstantSDNode>(N->getOperand(0).getOperand(1));
  if (XorC && ShiftC) {
    unsigned MaskIdx, MaskLen;
    if (XorC->getAPIntValue().isShiftedMask(MaskIdx, MaskLen)) {
      unsigned ShiftAmt = ShiftC->getZExtValue();
      unsigned BitWidth = N->getValueType(0).getScalarSizeInBits();
      if (N->getOperand(0).getOpcode() == ISD::SHL)
        return MaskIdx == ShiftAmt && MaskLen == (BitWidth - ShiftAmt);
      return MaskIdx == 0 && MaskLen == (BitWidth - ShiftAmt);
    }
  }

  return false;
}

bool ARMTargetLowering::shouldFoldConstantShiftPairToMask(
    const SDNode *N, CombineLevel Level) const {
  assert(((N->getOpcode() == ISD::SHL &&
           N->getOperand(0).getOpcode() == ISD::SRL) ||
          (N->getOpcode() == ISD::SRL &&
           N->getOperand(0).getOpcode() == ISD::SHL)) &&
         "Expected shift-shift mask");

  if (!Subtarget->isThumb1Only())
    return true;

  if (Level == BeforeLegalizeTypes)
    return true;

  return false;
}

bool ARMTargetLowering::shouldFoldSelectWithIdentityConstant(unsigned BinOpcode,
                                                             EVT VT) const {
  return Subtarget->hasMVEIntegerOps() && isTypeLegal(VT);
}

bool ARMTargetLowering::preferIncOfAddToSubOfNot(EVT VT) const {
  if (!Subtarget->hasNEON()) {
    if (Subtarget->isThumb1Only())
      return VT.getScalarSizeInBits() <= 32;
    return true;
  }
  return VT.isScalarInteger();
}

bool ARMTargetLowering::shouldConvertFpToSat(unsigned Op, EVT FPVT,
                                             EVT VT) const {
  if (!isOperationLegalOrCustom(Op, VT) || !FPVT.isSimple())
    return false;

  switch (FPVT.getSimpleVT().SimpleTy) {
  case MVT::f16:
    return Subtarget->hasVFP2Base();
  case MVT::f32:
    return Subtarget->hasVFP2Base();
  case MVT::f64:
    return Subtarget->hasFP64();
  case MVT::v4f32:
  case MVT::v8f16:
    return Subtarget->hasMVEFloatOps();
  default:
    return false;
  }
}

static SDValue PerformSHLSimplify(SDNode *N,
                                TargetLowering::DAGCombinerInfo &DCI,
                                const ARMSubtarget *ST) {
  // Allow the generic combiner to identify potential bswaps.
  if (DCI.isBeforeLegalize())
    return SDValue();

  // DAG combiner will fold:
  // (shl (add x, c1), c2) -> (add (shl x, c2), c1 << c2)
  // (shl (or x, c1), c2) -> (or (shl x, c2), c1 << c2
  // Other code patterns that can be also be modified have the following form:
  // b + ((a << 1) | 510)
  // b + ((a << 1) & 510)
  // b + ((a << 1) ^ 510)
  // b + ((a << 1) + 510)

  // Many instructions can  perform the shift for free, but it requires both
  // the operands to be registers. If c1 << c2 is too large, a mov immediate
  // instruction will needed. So, unfold back to the original pattern if:
  // - if c1 and c2 are small enough that they don't require mov imms.
  // - the user(s) of the node can perform an shl

  // No shifted operands for 16-bit instructions.
  if (ST->isThumb() && ST->isThumb1Only())
    return SDValue();

  // Check that all the users could perform the shl themselves.
  for (auto *U : N->uses()) {
    switch(U->getOpcode()) {
    default:
      return SDValue();
    case ISD::SUB:
    case ISD::ADD:
    case ISD::AND:
    case ISD::OR:
    case ISD::XOR:
    case ISD::SETCC:
    case ARMISD::CMP:
      // Check that the user isn't already using a constant because there
      // aren't any instructions that support an immediate operand and a
      // shifted operand.
      if (isa<ConstantSDNode>(U->getOperand(0)) ||
          isa<ConstantSDNode>(U->getOperand(1)))
        return SDValue();

      // Check that it's not already using a shift.
      if (U->getOperand(0).getOpcode() == ISD::SHL ||
          U->getOperand(1).getOpcode() == ISD::SHL)
        return SDValue();
      break;
    }
  }

  if (N->getOpcode() != ISD::ADD && N->getOpcode() != ISD::OR &&
      N->getOpcode() != ISD::XOR && N->getOpcode() != ISD::AND)
    return SDValue();

  if (N->getOperand(0).getOpcode() != ISD::SHL)
    return SDValue();

  SDValue SHL = N->getOperand(0);

  auto *C1ShlC2 = dyn_cast<ConstantSDNode>(N->getOperand(1));
  auto *C2 = dyn_cast<ConstantSDNode>(SHL.getOperand(1));
  if (!C1ShlC2 || !C2)
    return SDValue();

  APInt C2Int = C2->getAPIntValue();
  APInt C1Int = C1ShlC2->getAPIntValue();
  unsigned C2Width = C2Int.getBitWidth();
  if (C2Int.uge(C2Width))
    return SDValue();
  uint64_t C2Value = C2Int.getZExtValue();

  // Check that performing a lshr will not lose any information.
  APInt Mask = APInt::getHighBitsSet(C2Width, C2Width - C2Value);
  if ((C1Int & Mask) != C1Int)
    return SDValue();

  // Shift the first constant.
  C1Int.lshrInPlace(C2Int);

  // The immediates are encoded as an 8-bit value that can be rotated.
  auto LargeImm = [](const APInt &Imm) {
    unsigned Zeros = Imm.countl_zero() + Imm.countr_zero();
    return Imm.getBitWidth() - Zeros > 8;
  };

  if (LargeImm(C1Int) || LargeImm(C2Int))
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);
  SDValue X = SHL.getOperand(0);
  SDValue BinOp = DAG.getNode(N->getOpcode(), dl, MVT::i32, X,
                              DAG.getConstant(C1Int, dl, MVT::i32));
  // Shift left to compensate for the lshr of C1Int.
  SDValue Res = DAG.getNode(ISD::SHL, dl, MVT::i32, BinOp, SHL.getOperand(1));

  LLVM_DEBUG(dbgs() << "Simplify shl use:\n"; SHL.getOperand(0).dump();
             SHL.dump(); N->dump());
  LLVM_DEBUG(dbgs() << "Into:\n"; X.dump(); BinOp.dump(); Res.dump());
  return Res;
}


/// PerformADDCombine - Target-specific dag combine xforms for ISD::ADD.
///
static SDValue PerformADDCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 const ARMSubtarget *Subtarget) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  // Only works one way, because it needs an immediate operand.
  if (SDValue Result = PerformSHLSimplify(N, DCI, Subtarget))
    return Result;

  if (SDValue Result = PerformADDVecReduce(N, DCI.DAG, Subtarget))
    return Result;

  // First try with the default operand order.
  if (SDValue Result = PerformADDCombineWithOperands(N, N0, N1, DCI, Subtarget))
    return Result;

  // If that didn't work, try again with the operands commuted.
  return PerformADDCombineWithOperands(N, N1, N0, DCI, Subtarget);
}

// Combine (sub 0, (csinc X, Y, CC)) -> (csinv -X, Y, CC)
//   providing -X is as cheap as X (currently, just a constant).
static SDValue PerformSubCSINCCombine(SDNode *N, SelectionDAG &DAG) {
  if (N->getValueType(0) != MVT::i32 || !isNullConstant(N->getOperand(0)))
    return SDValue();
  SDValue CSINC = N->getOperand(1);
  if (CSINC.getOpcode() != ARMISD::CSINC || !CSINC.hasOneUse())
    return SDValue();

  ConstantSDNode *X = dyn_cast<ConstantSDNode>(CSINC.getOperand(0));
  if (!X)
    return SDValue();

  return DAG.getNode(ARMISD::CSINV, SDLoc(N), MVT::i32,
                     DAG.getNode(ISD::SUB, SDLoc(N), MVT::i32, N->getOperand(0),
                                 CSINC.getOperand(0)),
                     CSINC.getOperand(1), CSINC.getOperand(2),
                     CSINC.getOperand(3));
}

/// PerformSUBCombine - Target-specific dag combine xforms for ISD::SUB.
///
static SDValue PerformSUBCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 const ARMSubtarget *Subtarget) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  // fold (sub x, (select cc, 0, c)) -> (select cc, x, (sub, x, c))
  if (N1.getNode()->hasOneUse())
    if (SDValue Result = combineSelectAndUse(N, N1, N0, DCI))
      return Result;

  if (SDValue R = PerformSubCSINCCombine(N, DCI.DAG))
    return R;

  if (!Subtarget->hasMVEIntegerOps() || !N->getValueType(0).isVector())
    return SDValue();

  // Fold (sub (ARMvmovImm 0), (ARMvdup x)) -> (ARMvdup (sub 0, x))
  // so that we can readily pattern match more mve instructions which can use
  // a scalar operand.
  SDValue VDup = N->getOperand(1);
  if (VDup->getOpcode() != ARMISD::VDUP)
    return SDValue();

  SDValue VMov = N->getOperand(0);
  if (VMov->getOpcode() == ISD::BITCAST)
    VMov = VMov->getOperand(0);

  if (VMov->getOpcode() != ARMISD::VMOVIMM || !isZeroVector(VMov))
    return SDValue();

  SDLoc dl(N);
  SDValue Negate = DCI.DAG.getNode(ISD::SUB, dl, MVT::i32,
                                   DCI.DAG.getConstant(0, dl, MVT::i32),
                                   VDup->getOperand(0));
  return DCI.DAG.getNode(ARMISD::VDUP, dl, N->getValueType(0), Negate);
}

/// PerformVMULCombine
/// Distribute (A + B) * C to (A * C) + (B * C) to take advantage of the
/// special multiplier accumulator forwarding.
///   vmul d3, d0, d2
///   vmla d3, d1, d2
/// is faster than
///   vadd d3, d0, d1
///   vmul d3, d3, d2
//  However, for (A + B) * (A + B),
//    vadd d2, d0, d1
//    vmul d3, d0, d2
//    vmla d3, d1, d2
//  is slower than
//    vadd d2, d0, d1
//    vmul d3, d2, d2
static SDValue PerformVMULCombine(SDNode *N,
                                  TargetLowering::DAGCombinerInfo &DCI,
                                  const ARMSubtarget *Subtarget) {
  if (!Subtarget->hasVMLxForwarding())
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  unsigned Opcode = N0.getOpcode();
  if (Opcode != ISD::ADD && Opcode != ISD::SUB &&
      Opcode != ISD::FADD && Opcode != ISD::FSUB) {
    Opcode = N1.getOpcode();
    if (Opcode != ISD::ADD && Opcode != ISD::SUB &&
        Opcode != ISD::FADD && Opcode != ISD::FSUB)
      return SDValue();
    std::swap(N0, N1);
  }

  if (N0 == N1)
    return SDValue();

  EVT VT = N->getValueType(0);
  SDLoc DL(N);
  SDValue N00 = N0->getOperand(0);
  SDValue N01 = N0->getOperand(1);
  return DAG.getNode(Opcode, DL, VT,
                     DAG.getNode(ISD::MUL, DL, VT, N00, N1),
                     DAG.getNode(ISD::MUL, DL, VT, N01, N1));
}

static SDValue PerformMVEVMULLCombine(SDNode *N, SelectionDAG &DAG,
                                      const ARMSubtarget *Subtarget) {
  EVT VT = N->getValueType(0);
  if (VT != MVT::v2i64)
    return SDValue();

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  auto IsSignExt = [&](SDValue Op) {
    if (Op->getOpcode() != ISD::SIGN_EXTEND_INREG)
      return SDValue();
    EVT VT = cast<VTSDNode>(Op->getOperand(1))->getVT();
    if (VT.getScalarSizeInBits() == 32)
      return Op->getOperand(0);
    return SDValue();
  };
  auto IsZeroExt = [&](SDValue Op) {
    // Zero extends are a little more awkward. At the point we are matching
    // this, we are looking for an AND with a (-1, 0, -1, 0) buildvector mask.
    // That might be before of after a bitcast depending on how the and is
    // placed. Because this has to look through bitcasts, it is currently only
    // supported on LE.
    if (!Subtarget->isLittle())
      return SDValue();

    SDValue And = Op;
    if (And->getOpcode() == ISD::BITCAST)
      And = And->getOperand(0);
    if (And->getOpcode() != ISD::AND)
      return SDValue();
    SDValue Mask = And->getOperand(1);
    if (Mask->getOpcode() == ISD::BITCAST)
      Mask = Mask->getOperand(0);

    if (Mask->getOpcode() != ISD::BUILD_VECTOR ||
        Mask.getValueType() != MVT::v4i32)
      return SDValue();
    if (isAllOnesConstant(Mask->getOperand(0)) &&
        isNullConstant(Mask->getOperand(1)) &&
        isAllOnesConstant(Mask->getOperand(2)) &&
        isNullConstant(Mask->getOperand(3)))
      return And->getOperand(0);
    return SDValue();
  };

  SDLoc dl(N);
  if (SDValue Op0 = IsSignExt(N0)) {
    if (SDValue Op1 = IsSignExt(N1)) {
      SDValue New0a = DAG.getNode(ARMISD::VECTOR_REG_CAST, dl, MVT::v4i32, Op0);
      SDValue New1a = DAG.getNode(ARMISD::VECTOR_REG_CAST, dl, MVT::v4i32, Op1);
      return DAG.getNode(ARMISD::VMULLs, dl, VT, New0a, New1a);
    }
  }
  if (SDValue Op0 = IsZeroExt(N0)) {
    if (SDValue Op1 = IsZeroExt(N1)) {
      SDValue New0a = DAG.getNode(ARMISD::VECTOR_REG_CAST, dl, MVT::v4i32, Op0);
      SDValue New1a = DAG.getNode(ARMISD::VECTOR_REG_CAST, dl, MVT::v4i32, Op1);
      return DAG.getNode(ARMISD::VMULLu, dl, VT, New0a, New1a);
    }
  }

  return SDValue();
}

static SDValue PerformMULCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 const ARMSubtarget *Subtarget) {
  SelectionDAG &DAG = DCI.DAG;

  EVT VT = N->getValueType(0);
  if (Subtarget->hasMVEIntegerOps() && VT == MVT::v2i64)
    return PerformMVEVMULLCombine(N, DAG, Subtarget);

  if (Subtarget->isThumb1Only())
    return SDValue();

  if (DCI.isBeforeLegalize() || DCI.isCalledByLegalizer())
    return SDValue();

  if (VT.is64BitVector() || VT.is128BitVector())
    return PerformVMULCombine(N, DCI, Subtarget);
  if (VT != MVT::i32)
    return SDValue();

  ConstantSDNode *C = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!C)
    return SDValue();

  int64_t MulAmt = C->getSExtValue();
  unsigned ShiftAmt = llvm::countr_zero<uint64_t>(MulAmt);

  ShiftAmt = ShiftAmt & (32 - 1);
  SDValue V = N->getOperand(0);
  SDLoc DL(N);

  SDValue Res;
  MulAmt >>= ShiftAmt;

  if (MulAmt >= 0) {
    if (llvm::has_single_bit<uint32_t>(MulAmt - 1)) {
      // (mul x, 2^N + 1) => (add (shl x, N), x)
      Res = DAG.getNode(ISD::ADD, DL, VT,
                        V,
                        DAG.getNode(ISD::SHL, DL, VT,
                                    V,
                                    DAG.getConstant(Log2_32(MulAmt - 1), DL,
                                                    MVT::i32)));
    } else if (llvm::has_single_bit<uint32_t>(MulAmt + 1)) {
      // (mul x, 2^N - 1) => (sub (shl x, N), x)
      Res = DAG.getNode(ISD::SUB, DL, VT,
                        DAG.getNode(ISD::SHL, DL, VT,
                                    V,
                                    DAG.getConstant(Log2_32(MulAmt + 1), DL,
                                                    MVT::i32)),
                        V);
    } else
      return SDValue();
  } else {
    uint64_t MulAmtAbs = -MulAmt;
    if (llvm::has_single_bit<uint32_t>(MulAmtAbs + 1)) {
      // (mul x, -(2^N - 1)) => (sub x, (shl x, N))
      Res = DAG.getNode(ISD::SUB, DL, VT,
                        V,
                        DAG.getNode(ISD::SHL, DL, VT,
                                    V,
                                    DAG.getConstant(Log2_32(MulAmtAbs + 1), DL,
                                                    MVT::i32)));
    } else if (llvm::has_single_bit<uint32_t>(MulAmtAbs - 1)) {
      // (mul x, -(2^N + 1)) => - (add (shl x, N), x)
      Res = DAG.getNode(ISD::ADD, DL, VT,
                        V,
                        DAG.getNode(ISD::SHL, DL, VT,
                                    V,
                                    DAG.getConstant(Log2_32(MulAmtAbs - 1), DL,
                                                    MVT::i32)));
      Res = DAG.getNode(ISD::SUB, DL, VT,
                        DAG.getConstant(0, DL, MVT::i32), Res);
    } else
      return SDValue();
  }

  if (ShiftAmt != 0)
    Res = DAG.getNode(ISD::SHL, DL, VT,
                      Res, DAG.getConstant(ShiftAmt, DL, MVT::i32));

  // Do not add new nodes to DAG combiner worklist.
  DCI.CombineTo(N, Res, false);
  return SDValue();
}

static SDValue CombineANDShift(SDNode *N,
                               TargetLowering::DAGCombinerInfo &DCI,
                               const ARMSubtarget *Subtarget) {
  // Allow DAGCombine to pattern-match before we touch the canonical form.
  if (DCI.isBeforeLegalize() || DCI.isCalledByLegalizer())
    return SDValue();

  if (N->getValueType(0) != MVT::i32)
    return SDValue();

  ConstantSDNode *N1C = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!N1C)
    return SDValue();

  uint32_t C1 = (uint32_t)N1C->getZExtValue();
  // Don't transform uxtb/uxth.
  if (C1 == 255 || C1 == 65535)
    return SDValue();

  SDNode *N0 = N->getOperand(0).getNode();
  if (!N0->hasOneUse())
    return SDValue();

  if (N0->getOpcode() != ISD::SHL && N0->getOpcode() != ISD::SRL)
    return SDValue();

  bool LeftShift = N0->getOpcode() == ISD::SHL;

  ConstantSDNode *N01C = dyn_cast<ConstantSDNode>(N0->getOperand(1));
  if (!N01C)
    return SDValue();

  uint32_t C2 = (uint32_t)N01C->getZExtValue();
  if (!C2 || C2 >= 32)
    return SDValue();

  // Clear irrelevant bits in the mask.
  if (LeftShift)
    C1 &= (-1U << C2);
  else
    C1 &= (-1U >> C2);

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  // We have a pattern of the form "(and (shl x, c2) c1)" or
  // "(and (srl x, c2) c1)", where c1 is a shifted mask. Try to
  // transform to a pair of shifts, to save materializing c1.

  // First pattern: right shift, then mask off leading bits.
  // FIXME: Use demanded bits?
  if (!LeftShift && isMask_32(C1)) {
    uint32_t C3 = llvm::countl_zero(C1);
    if (C2 < C3) {
      SDValue SHL = DAG.getNode(ISD::SHL, DL, MVT::i32, N0->getOperand(0),
                                DAG.getConstant(C3 - C2, DL, MVT::i32));
      return DAG.getNode(ISD::SRL, DL, MVT::i32, SHL,
                         DAG.getConstant(C3, DL, MVT::i32));
    }
  }

  // First pattern, reversed: left shift, then mask off trailing bits.
  if (LeftShift && isMask_32(~C1)) {
    uint32_t C3 = llvm::countr_zero(C1);
    if (C2 < C3) {
      SDValue SHL = DAG.getNode(ISD::SRL, DL, MVT::i32, N0->getOperand(0),
                                DAG.getConstant(C3 - C2, DL, MVT::i32));
      return DAG.getNode(ISD::SHL, DL, MVT::i32, SHL,
                         DAG.getConstant(C3, DL, MVT::i32));
    }
  }

  // Second pattern: left shift, then mask off leading bits.
  // FIXME: Use demanded bits?
  if (LeftShift && isShiftedMask_32(C1)) {
    uint32_t Trailing = llvm::countr_zero(C1);
    uint32_t C3 = llvm::countl_zero(C1);
    if (Trailing == C2 && C2 + C3 < 32) {
      SDValue SHL = DAG.getNode(ISD::SHL, DL, MVT::i32, N0->getOperand(0),
                                DAG.getConstant(C2 + C3, DL, MVT::i32));
      return DAG.getNode(ISD::SRL, DL, MVT::i32, SHL,
                        DAG.getConstant(C3, DL, MVT::i32));
    }
  }

  // Second pattern, reversed: right shift, then mask off trailing bits.
  // FIXME: Handle other patterns of known/demanded bits.
  if (!LeftShift && isShiftedMask_32(C1)) {
    uint32_t Leading = llvm::countl_zero(C1);
    uint32_t C3 = llvm::countr_zero(C1);
    if (Leading == C2 && C2 + C3 < 32) {
      SDValue SHL = DAG.getNode(ISD::SRL, DL, MVT::i32, N0->getOperand(0),
                                DAG.getConstant(C2 + C3, DL, MVT::i32));
      return DAG.getNode(ISD::SHL, DL, MVT::i32, SHL,
                         DAG.getConstant(C3, DL, MVT::i32));
    }
  }

  // Transform "(and (shl x, c2) c1)" into "(shl (and x, c1>>c2), c2)"
  // if "c1 >> c2" is a cheaper immediate than "c1"
  if (LeftShift &&
      HasLowerConstantMaterializationCost(C1 >> C2, C1, Subtarget)) {

    SDValue And = DAG.getNode(ISD::AND, DL, MVT::i32, N0->getOperand(0),
                              DAG.getConstant(C1 >> C2, DL, MVT::i32));
    return DAG.getNode(ISD::SHL, DL, MVT::i32, And,
                       DAG.getConstant(C2, DL, MVT::i32));
  }

  return SDValue();
}

static SDValue PerformANDCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 const ARMSubtarget *Subtarget) {
  // Attempt to use immediate-form VBIC
  BuildVectorSDNode *BVN = dyn_cast<BuildVectorSDNode>(N->getOperand(1));
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  SelectionDAG &DAG = DCI.DAG;

  if (!DAG.getTargetLoweringInfo().isTypeLegal(VT) || VT == MVT::v2i1 ||
      VT == MVT::v4i1 || VT == MVT::v8i1 || VT == MVT::v16i1)
    return SDValue();

  APInt SplatBits, SplatUndef;
  unsigned SplatBitSize;
  bool HasAnyUndefs;
  if (BVN && (Subtarget->hasNEON() || Subtarget->hasMVEIntegerOps()) &&
      BVN->isConstantSplat(SplatBits, SplatUndef, SplatBitSize, HasAnyUndefs)) {
    if (SplatBitSize == 8 || SplatBitSize == 16 || SplatBitSize == 32 ||
        SplatBitSize == 64) {
      EVT VbicVT;
      SDValue Val = isVMOVModifiedImm((~SplatBits).getZExtValue(),
                                      SplatUndef.getZExtValue(), SplatBitSize,
                                      DAG, dl, VbicVT, VT, OtherModImm);
      if (Val.getNode()) {
        SDValue Input =
          DAG.getNode(ISD::BITCAST, dl, VbicVT, N->getOperand(0));
        SDValue Vbic = DAG.getNode(ARMISD::VBICIMM, dl, VbicVT, Input, Val);
        return DAG.getNode(ISD::BITCAST, dl, VT, Vbic);
      }
    }
  }

  if (!Subtarget->isThumb1Only()) {
    // fold (and (select cc, -1, c), x) -> (select cc, x, (and, x, c))
    if (SDValue Result = combineSelectAndUseCommutative(N, true, DCI))
      return Result;

    if (SDValue Result = PerformSHLSimplify(N, DCI, Subtarget))
      return Result;
  }

  if (Subtarget->isThumb1Only())
    if (SDValue Result = CombineANDShift(N, DCI, Subtarget))
      return Result;

  return SDValue();
}

// Try combining OR nodes to SMULWB, SMULWT.
static SDValue PerformORCombineToSMULWBT(SDNode *OR,
                                         TargetLowering::DAGCombinerInfo &DCI,
                                         const ARMSubtarget *Subtarget) {
  if (!Subtarget->hasV6Ops() ||
      (Subtarget->isThumb() &&
       (!Subtarget->hasThumb2() || !Subtarget->hasDSP())))
    return SDValue();

  SDValue SRL = OR->getOperand(0);
  SDValue SHL = OR->getOperand(1);

  if (SRL.getOpcode() != ISD::SRL || SHL.getOpcode() != ISD::SHL) {
    SRL = OR->getOperand(1);
    SHL = OR->getOperand(0);
  }
  if (!isSRL16(SRL) || !isSHL16(SHL))
    return SDValue();

  // The first operands to the shifts need to be the two results from the
  // same smul_lohi node.
  if ((SRL.getOperand(0).getNode() != SHL.getOperand(0).getNode()) ||
       SRL.getOperand(0).getOpcode() != ISD::SMUL_LOHI)
    return SDValue();

  SDNode *SMULLOHI = SRL.getOperand(0).getNode();
  if (SRL.getOperand(0) != SDValue(SMULLOHI, 0) ||
      SHL.getOperand(0) != SDValue(SMULLOHI, 1))
    return SDValue();

  // Now we have:
  // (or (srl (smul_lohi ?, ?), 16), (shl (smul_lohi ?, ?), 16)))
  // For SMUL[B|T] smul_lohi will take a 32-bit and a 16-bit arguments.
  // For SMUWB the 16-bit value will signed extended somehow.
  // For SMULWT only the SRA is required.
  // Check both sides of SMUL_LOHI
  SDValue OpS16 = SMULLOHI->getOperand(0);
  SDValue OpS32 = SMULLOHI->getOperand(1);

  SelectionDAG &DAG = DCI.DAG;
  if (!isS16(OpS16, DAG) && !isSRA16(OpS16)) {
    OpS16 = OpS32;
    OpS32 = SMULLOHI->getOperand(0);
  }

  SDLoc dl(OR);
  unsigned Opcode = 0;
  if (isS16(OpS16, DAG))
    Opcode = ARMISD::SMULWB;
  else if (isSRA16(OpS16)) {
    Opcode = ARMISD::SMULWT;
    OpS16 = OpS16->getOperand(0);
  }
  else
    return SDValue();

  SDValue Res = DAG.getNode(Opcode, dl, MVT::i32, OpS32, OpS16);
  DAG.ReplaceAllUsesOfValueWith(SDValue(OR, 0), Res);
  return SDValue(OR, 0);
}

static SDValue PerformORCombineToBFI(SDNode *N,
                                     TargetLowering::DAGCombinerInfo &DCI,
                                     const ARMSubtarget *Subtarget) {
  // BFI is only available on V6T2+
  if (Subtarget->isThumb1Only() || !Subtarget->hasV6T2Ops())
    return SDValue();

  EVT VT = N->getValueType(0);
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);
  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);
  // 1) or (and A, mask), val => ARMbfi A, val, mask
  //      iff (val & mask) == val
  //
  // 2) or (and A, mask), (and B, mask2) => ARMbfi A, (lsr B, amt), mask
  //  2a) iff isBitFieldInvertedMask(mask) && isBitFieldInvertedMask(~mask2)
  //          && mask == ~mask2
  //  2b) iff isBitFieldInvertedMask(~mask) && isBitFieldInvertedMask(mask2)
  //          && ~mask == mask2
  //  (i.e., copy a bitfield value into another bitfield of the same width)

  if (VT != MVT::i32)
    return SDValue();

  SDValue N00 = N0.getOperand(0);

  // The value and the mask need to be constants so we can verify this is
  // actually a bitfield set. If the mask is 0xffff, we can do better
  // via a movt instruction, so don't use BFI in that case.
  SDValue MaskOp = N0.getOperand(1);
  ConstantSDNode *MaskC = dyn_cast<ConstantSDNode>(MaskOp);
  if (!MaskC)
    return SDValue();
  unsigned Mask = MaskC->getZExtValue();
  if (Mask == 0xffff)
    return SDValue();
  SDValue Res;
  // Case (1): or (and A, mask), val => ARMbfi A, val, mask
  ConstantSDNode *N1C = dyn_cast<ConstantSDNode>(N1);
  if (N1C) {
    unsigned Val = N1C->getZExtValue();
    if ((Val & ~Mask) != Val)
      return SDValue();

    if (ARM::isBitFieldInvertedMask(Mask)) {
      Val >>= llvm::countr_zero(~Mask);

      Res = DAG.getNode(ARMISD::BFI, DL, VT, N00,
                        DAG.getConstant(Val, DL, MVT::i32),
                        DAG.getConstant(Mask, DL, MVT::i32));

      DCI.CombineTo(N, Res, false);
      // Return value from the original node to inform the combiner than N is
      // now dead.
      return SDValue(N, 0);
    }
  } else if (N1.getOpcode() == ISD::AND) {
    // case (2) or (and A, mask), (and B, mask2) => ARMbfi A, (lsr B, amt), mask
    ConstantSDNode *N11C = dyn_cast<ConstantSDNode>(N1.getOperand(1));
    if (!N11C)
      return SDValue();
    unsigned Mask2 = N11C->getZExtValue();

    // Mask and ~Mask2 (or reverse) must be equivalent for the BFI pattern
    // as is to match.
    if (ARM::isBitFieldInvertedMask(Mask) &&
        (Mask == ~Mask2)) {
      // The pack halfword instruction works better for masks that fit it,
      // so use that when it's available.
      if (Subtarget->hasDSP() &&
          (Mask == 0xffff || Mask == 0xffff0000))
        return SDValue();
      // 2a
      unsigned amt = llvm::countr_zero(Mask2);
      Res = DAG.getNode(ISD::SRL, DL, VT, N1.getOperand(0),
                        DAG.getConstant(amt, DL, MVT::i32));
      Res = DAG.getNode(ARMISD::BFI, DL, VT, N00, Res,
                        DAG.getConstant(Mask, DL, MVT::i32));
      DCI.CombineTo(N, Res, false);
      // Return value from the original node to inform the combiner than N is
      // now dead.
      return SDValue(N, 0);
    } else if (ARM::isBitFieldInvertedMask(~Mask) &&
               (~Mask == Mask2)) {
      // The pack halfword instruction works better for masks that fit it,
      // so use that when it's available.
      if (Subtarget->hasDSP() &&
          (Mask2 == 0xffff || Mask2 == 0xffff0000))
        return SDValue();
      // 2b
      unsigned lsb = llvm::countr_zero(Mask);
      Res = DAG.getNode(ISD::SRL, DL, VT, N00,
                        DAG.getConstant(lsb, DL, MVT::i32));
      Res = DAG.getNode(ARMISD::BFI, DL, VT, N1.getOperand(0), Res,
                        DAG.getConstant(Mask2, DL, MVT::i32));
      DCI.CombineTo(N, Res, false);
      // Return value from the original node to inform the combiner than N is
      // now dead.
      return SDValue(N, 0);
    }
  }

  if (DAG.MaskedValueIsZero(N1, MaskC->getAPIntValue()) &&
      N00.getOpcode() == ISD::SHL && isa<ConstantSDNode>(N00.getOperand(1)) &&
      ARM::isBitFieldInvertedMask(~Mask)) {
    // Case (3): or (and (shl A, #shamt), mask), B => ARMbfi B, A, ~mask
    // where lsb(mask) == #shamt and masked bits of B are known zero.
    SDValue ShAmt = N00.getOperand(1);
    unsigned ShAmtC = ShAmt->getAsZExtVal();
    unsigned LSB = llvm::countr_zero(Mask);
    if (ShAmtC != LSB)
      return SDValue();

    Res = DAG.getNode(ARMISD::BFI, DL, VT, N1, N00.getOperand(0),
                      DAG.getConstant(~Mask, DL, MVT::i32));

    DCI.CombineTo(N, Res, false);
    // Return value from the original node to inform the combiner than N is
    // now dead.
    return SDValue(N, 0);
  }

  return SDValue();
}

static bool isValidMVECond(unsigned CC, bool IsFloat) {
  switch (CC) {
  case ARMCC::EQ:
  case ARMCC::NE:
  case ARMCC::LE:
  case ARMCC::GT:
  case ARMCC::GE:
  case ARMCC::LT:
    return true;
  case ARMCC::HS:
  case ARMCC::HI:
    return !IsFloat;
  default:
    return false;
  };
}

static ARMCC::CondCodes getVCMPCondCode(SDValue N) {
  if (N->getOpcode() == ARMISD::VCMP)
    return (ARMCC::CondCodes)N->getConstantOperandVal(2);
  else if (N->getOpcode() == ARMISD::VCMPZ)
    return (ARMCC::CondCodes)N->getConstantOperandVal(1);
  else
    llvm_unreachable("Not a VCMP/VCMPZ!");
}

static bool CanInvertMVEVCMP(SDValue N) {
  ARMCC::CondCodes CC = ARMCC::getOppositeCondition(getVCMPCondCode(N));
  return isValidMVECond(CC, N->getOperand(0).getValueType().isFloatingPoint());
}

static SDValue PerformORCombine_i1(SDNode *N, SelectionDAG &DAG,
                                   const ARMSubtarget *Subtarget) {
  // Try to invert "or A, B" -> "and ~A, ~B", as the "and" is easier to chain
  // together with predicates
  EVT VT = N->getValueType(0);
  SDLoc DL(N);
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  auto IsFreelyInvertable = [&](SDValue V) {
    if (V->getOpcode() == ARMISD::VCMP || V->getOpcode() == ARMISD::VCMPZ)
      return CanInvertMVEVCMP(V);
    return false;
  };

  // At least one operand must be freely invertable.
  if (!(IsFreelyInvertable(N0) || IsFreelyInvertable(N1)))
    return SDValue();

  SDValue NewN0 = DAG.getLogicalNOT(DL, N0, VT);
  SDValue NewN1 = DAG.getLogicalNOT(DL, N1, VT);
  SDValue And = DAG.getNode(ISD::AND, DL, VT, NewN0, NewN1);
  return DAG.getLogicalNOT(DL, And, VT);
}

/// PerformORCombine - Target-specific dag combine xforms for ISD::OR
static SDValue PerformORCombine(SDNode *N,
                                TargetLowering::DAGCombinerInfo &DCI,
                                const ARMSubtarget *Subtarget) {
  // Attempt to use immediate-form VORR
  BuildVectorSDNode *BVN = dyn_cast<BuildVectorSDNode>(N->getOperand(1));
  SDLoc dl(N);
  EVT VT = N->getValueType(0);
  SelectionDAG &DAG = DCI.DAG;

  if(!DAG.getTargetLoweringInfo().isTypeLegal(VT))
    return SDValue();

  if (Subtarget->hasMVEIntegerOps() && (VT == MVT::v2i1 || VT == MVT::v4i1 ||
                                        VT == MVT::v8i1 || VT == MVT::v16i1))
    return PerformORCombine_i1(N, DAG, Subtarget);

  APInt SplatBits, SplatUndef;
  unsigned SplatBitSize;
  bool HasAnyUndefs;
  if (BVN && (Subtarget->hasNEON() || Subtarget->hasMVEIntegerOps()) &&
      BVN->isConstantSplat(SplatBits, SplatUndef, SplatBitSize, HasAnyUndefs)) {
    if (SplatBitSize == 8 || SplatBitSize == 16 || SplatBitSize == 32 ||
        SplatBitSize == 64) {
      EVT VorrVT;
      SDValue Val =
          isVMOVModifiedImm(SplatBits.getZExtValue(), SplatUndef.getZExtValue(),
                            SplatBitSize, DAG, dl, VorrVT, VT, OtherModImm);
      if (Val.getNode()) {
        SDValue Input =
          DAG.getNode(ISD::BITCAST, dl, VorrVT, N->getOperand(0));
        SDValue Vorr = DAG.getNode(ARMISD::VORRIMM, dl, VorrVT, Input, Val);
        return DAG.getNode(ISD::BITCAST, dl, VT, Vorr);
      }
    }
  }

  if (!Subtarget->isThumb1Only()) {
    // fold (or (select cc, 0, c), x) -> (select cc, x, (or, x, c))
    if (SDValue Result = combineSelectAndUseCommutative(N, false, DCI))
      return Result;
    if (SDValue Result = PerformORCombineToSMULWBT(N, DCI, Subtarget))
      return Result;
  }

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  // (or (and B, A), (and C, ~A)) => (VBSL A, B, C) when A is a constant.
  if (Subtarget->hasNEON() && N1.getOpcode() == ISD::AND && VT.isVector() &&
      DAG.getTargetLoweringInfo().isTypeLegal(VT)) {

    // The code below optimizes (or (and X, Y), Z).
    // The AND operand needs to have a single user to make these optimizations
    // profitable.
    if (N0.getOpcode() != ISD::AND || !N0.hasOneUse())
      return SDValue();

    APInt SplatUndef;
    unsigned SplatBitSize;
    bool HasAnyUndefs;

    APInt SplatBits0, SplatBits1;
    BuildVectorSDNode *BVN0 = dyn_cast<BuildVectorSDNode>(N0->getOperand(1));
    BuildVectorSDNode *BVN1 = dyn_cast<BuildVectorSDNode>(N1->getOperand(1));
    // Ensure that the second operand of both ands are constants
    if (BVN0 && BVN0->isConstantSplat(SplatBits0, SplatUndef, SplatBitSize,
                                      HasAnyUndefs) && !HasAnyUndefs) {
        if (BVN1 && BVN1->isConstantSplat(SplatBits1, SplatUndef, SplatBitSize,
                                          HasAnyUndefs) && !HasAnyUndefs) {
            // Ensure that the bit width of the constants are the same and that
            // the splat arguments are logical inverses as per the pattern we
            // are trying to simplify.
            if (SplatBits0.getBitWidth() == SplatBits1.getBitWidth() &&
                SplatBits0 == ~SplatBits1) {
                // Canonicalize the vector type to make instruction selection
                // simpler.
                EVT CanonicalVT = VT.is128BitVector() ? MVT::v4i32 : MVT::v2i32;
                SDValue Result = DAG.getNode(ARMISD::VBSP, dl, CanonicalVT,
                                             N0->getOperand(1),
                                             N0->getOperand(0),
                                             N1->getOperand(0));
                return DAG.getNode(ARMISD::VECTOR_REG_CAST, dl, VT, Result);
            }
        }
    }
  }

  // Try to use the ARM/Thumb2 BFI (bitfield insert) instruction when
  // reasonable.
  if (N0.getOpcode() == ISD::AND && N0.hasOneUse()) {
    if (SDValue Res = PerformORCombineToBFI(N, DCI, Subtarget))
      return Res;
  }

  if (SDValue Result = PerformSHLSimplify(N, DCI, Subtarget))
    return Result;

  return SDValue();
}

static SDValue PerformXORCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI,
                                 const ARMSubtarget *Subtarget) {
  EVT VT = N->getValueType(0);
  SelectionDAG &DAG = DCI.DAG;

  if(!DAG.getTargetLoweringInfo().isTypeLegal(VT))
    return SDValue();

  if (!Subtarget->isThumb1Only()) {
    // fold (xor (select cc, 0, c), x) -> (select cc, x, (xor, x, c))
    if (SDValue Result = combineSelectAndUseCommutative(N, false, DCI))
      return Result;

    if (SDValue Result = PerformSHLSimplify(N, DCI, Subtarget))
      return Result;
  }

  if (Subtarget->hasMVEIntegerOps()) {
    // fold (xor(vcmp/z, 1)) into a vcmp with the opposite condition.
    SDValue N0 = N->getOperand(0);
    SDValue N1 = N->getOperand(1);
    const TargetLowering *TLI = Subtarget->getTargetLowering();
    if (TLI->isConstTrueVal(N1) &&
        (N0->getOpcode() == ARMISD::VCMP || N0->getOpcode() == ARMISD::VCMPZ)) {
      if (CanInvertMVEVCMP(N0)) {
        SDLoc DL(N0);
        ARMCC::CondCodes CC = ARMCC::getOppositeCondition(getVCMPCondCode(N0));

        SmallVector<SDValue, 4> Ops;
        Ops.push_back(N0->getOperand(0));
        if (N0->getOpcode() == ARMISD::VCMP)
          Ops.push_back(N0->getOperand(1));
        Ops.push_back(DAG.getConstant(CC, DL, MVT::i32));
        return DAG.getNode(N0->getOpcode(), DL, N0->getValueType(0), Ops);
      }
    }
  }

  return SDValue();
}

// ParseBFI - given a BFI instruction in N, extract the "from" value (Rn) and return it,
// and fill in FromMask and ToMask with (consecutive) bits in "from" to be extracted and
// their position in "to" (Rd).
static SDValue ParseBFI(SDNode *N, APInt &ToMask, APInt &FromMask) {
  assert(N->getOpcode() == ARMISD::BFI);

  SDValue From = N->getOperand(1);
  ToMask = ~N->getConstantOperandAPInt(2);
  FromMask = APInt::getLowBitsSet(ToMask.getBitWidth(), ToMask.popcount());

  // If the Base came from a SHR #C, we can deduce that it is really testing bit
  // #C in the base of the SHR.
  if (From->getOpcode() == ISD::SRL &&
      isa<ConstantSDNode>(From->getOperand(1))) {
    APInt Shift = From->getConstantOperandAPInt(1);
    assert(Shift.getLimitedValue() < 32 && "Shift too large!");
    FromMask <<= Shift.getLimitedValue(31);
    From = From->getOperand(0);
  }

  return From;
}

// If A and B contain one contiguous set of bits, does A | B == A . B?
//
// Neither A nor B must be zero.
static bool BitsProperlyConcatenate(const APInt &A, const APInt &B) {
  unsigned LastActiveBitInA = A.countr_zero();
  unsigned FirstActiveBitInB = B.getBitWidth() - B.countl_zero() - 1;
  return LastActiveBitInA - 1 == FirstActiveBitInB;
}

static SDValue FindBFIToCombineWith(SDNode *N) {
  // We have a BFI in N. Find a BFI it can combine with, if one exists.
  APInt ToMask, FromMask;
  SDValue From = ParseBFI(N, ToMask, FromMask);
  SDValue To = N->getOperand(0);

  SDValue V = To;
  if (V.getOpcode() != ARMISD::BFI)
    return SDValue();

  APInt NewToMask, NewFromMask;
  SDValue NewFrom = ParseBFI(V.getNode(), NewToMask, NewFromMask);
  if (NewFrom != From)
    return SDValue();

  // Do the written bits conflict with any we've seen so far?
  if ((NewToMask & ToMask).getBoolValue())
    // Conflicting bits.
    return SDValue();

  // Are the new bits contiguous when combined with the old bits?
  if (BitsProperlyConcatenate(ToMask, NewToMask) &&
      BitsProperlyConcatenate(FromMask, NewFromMask))
    return V;
  if (BitsProperlyConcatenate(NewToMask, ToMask) &&
      BitsProperlyConcatenate(NewFromMask, FromMask))
    return V;

  return SDValue();
}

static SDValue PerformBFICombine(SDNode *N, SelectionDAG &DAG) {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  if (N1.getOpcode() == ISD::AND) {
    // (bfi A, (and B, Mask1), Mask2) -> (bfi A, B, Mask2) iff
    // the bits being cleared by the AND are not demanded by the BFI.
    ConstantSDNode *N11C = dyn_cast<ConstantSDNode>(N1.getOperand(1));
    if (!N11C)
      return SDValue();
    unsigned InvMask = N->getConstantOperandVal(2);
    unsigned LSB = llvm::countr_zero(~InvMask);
    unsigned Width = llvm::bit_width<unsigned>(~InvMask) - LSB;
    assert(Width <
               static_cast<unsigned>(std::numeric_limits<unsigned>::digits) &&
           "undefined behavior");
    unsigned Mask = (1u << Width) - 1;
    unsigned Mask2 = N11C->getZExtValue();
    if ((Mask & (~Mask2)) == 0)
      return DAG.getNode(ARMISD::BFI, SDLoc(N), N->getValueType(0),
                         N->getOperand(0), N1.getOperand(0), N->getOperand(2));
    return SDValue();
  }

  // Look for another BFI to combine with.
  if (SDValue CombineBFI = FindBFIToCombineWith(N)) {
    // We've found a BFI.
    APInt ToMask1, FromMask1;
    SDValue From1 = ParseBFI(N, ToMask1, FromMask1);

    APInt ToMask2, FromMask2;
    SDValue From2 = ParseBFI(CombineBFI.getNode(), ToMask2, FromMask2);
    assert(From1 == From2);
    (void)From2;

    // Create a new BFI, combining the two together.
    APInt NewFromMask = FromMask1 | FromMask2;
    APInt NewToMask = ToMask1 | ToMask2;

    EVT VT = N->getValueType(0);
    SDLoc dl(N);

    if (NewFromMask[0] == 0)
      From1 = DAG.getNode(ISD::SRL, dl, VT, From1,
                          DAG.getConstant(NewFromMask.countr_zero(), dl, VT));
    return DAG.getNode(ARMISD::BFI, dl, VT, CombineBFI.getOperand(0), From1,
                       DAG.getConstant(~NewToMask, dl, VT));
  }

  // Reassociate BFI(BFI (A, B, M1), C, M2) to BFI(BFI (A, C, M2), B, M1) so
  // that lower bit insertions are performed first, providing that M1 and M2
  // do no overlap. This can allow multiple BFI instructions to be combined
  // together by the other folds above.
  if (N->getOperand(0).getOpcode() == ARMISD::BFI) {
    APInt ToMask1 = ~N->getConstantOperandAPInt(2);
    APInt ToMask2 = ~N0.getConstantOperandAPInt(2);

    if (!N0.hasOneUse() || (ToMask1 & ToMask2) != 0 ||
        ToMask1.countl_zero() < ToMask2.countl_zero())
      return SDValue();

    EVT VT = N->getValueType(0);
    SDLoc dl(N);
    SDValue BFI1 = DAG.getNode(ARMISD::BFI, dl, VT, N0.getOperand(0),
                               N->getOperand(1), N->getOperand(2));
    return DAG.getNode(ARMISD::BFI, dl, VT, BFI1, N0.getOperand(1),
                       N0.getOperand(2));
  }

  return SDValue();
}

// Check that N is CMPZ(CSINC(0, 0, CC, X)),
//              or CMPZ(CMOV(1, 0, CC, $cpsr, X))
// return X if valid.
static SDValue IsCMPZCSINC(SDNode *Cmp, ARMCC::CondCodes &CC) {
  if (Cmp->getOpcode() != ARMISD::CMPZ || !isNullConstant(Cmp->getOperand(1)))
    return SDValue();
  SDValue CSInc = Cmp->getOperand(0);

  // Ignore any `And 1` nodes that may not yet have been removed. We are
  // looking for a value that produces 1/0, so these have no effect on the
  // code.
  while (CSInc.getOpcode() == ISD::AND &&
         isa<ConstantSDNode>(CSInc.getOperand(1)) &&
         CSInc.getConstantOperandVal(1) == 1 && CSInc->hasOneUse())
    CSInc = CSInc.getOperand(0);

  if (CSInc.getOpcode() == ARMISD::CSINC &&
      isNullConstant(CSInc.getOperand(0)) &&
      isNullConstant(CSInc.getOperand(1)) && CSInc->hasOneUse()) {
    CC = (ARMCC::CondCodes)CSInc.getConstantOperandVal(2);
    return CSInc.getOperand(3);
  }
  if (CSInc.getOpcode() == ARMISD::CMOV && isOneConstant(CSInc.getOperand(0)) &&
      isNullConstant(CSInc.getOperand(1)) && CSInc->hasOneUse()) {
    CC = (ARMCC::CondCodes)CSInc.getConstantOperandVal(2);
    return CSInc.getOperand(4);
  }
  if (CSInc.getOpcode() == ARMISD::CMOV && isOneConstant(CSInc.getOperand(1)) &&
      isNullConstant(CSInc.getOperand(0)) && CSInc->hasOneUse()) {
    CC = ARMCC::getOppositeCondition(
        (ARMCC::CondCodes)CSInc.getConstantOperandVal(2));
    return CSInc.getOperand(4);
  }
  return SDValue();
}

static SDValue PerformCMPZCombine(SDNode *N, SelectionDAG &DAG) {
  // Given CMPZ(CSINC(C, 0, 0, EQ), 0), we can just use C directly. As in
  //       t92: glue = ARMISD::CMPZ t74, 0
  //     t93: i32 = ARMISD::CSINC 0, 0, 1, t92
  //   t96: glue = ARMISD::CMPZ t93, 0
  // t114: i32 = ARMISD::CSINV 0, 0, 0, t96
  ARMCC::CondCodes Cond;
  if (SDValue C = IsCMPZCSINC(N, Cond))
    if (Cond == ARMCC::EQ)
      return C;
  return SDValue();
}

static SDValue PerformCSETCombine(SDNode *N, SelectionDAG &DAG) {
  // Fold away an unneccessary CMPZ/CSINC
  // CSXYZ A, B, C1 (CMPZ (CSINC 0, 0, C2, D), 0) ->
  // if C1==EQ -> CSXYZ A, B, C2, D
  // if C1==NE -> CSXYZ A, B, NOT(C2), D
  ARMCC::CondCodes Cond;
  if (SDValue C = IsCMPZCSINC(N->getOperand(3).getNode(), Cond)) {
    if (N->getConstantOperandVal(2) == ARMCC::EQ)
      return DAG.getNode(N->getOpcode(), SDLoc(N), MVT::i32, N->getOperand(0),
                         N->getOperand(1),
                         DAG.getConstant(Cond, SDLoc(N), MVT::i32), C);
    if (N->getConstantOperandVal(2) == ARMCC::NE)
      return DAG.getNode(
          N->getOpcode(), SDLoc(N), MVT::i32, N->getOperand(0),
          N->getOperand(1),
          DAG.getConstant(ARMCC::getOppositeCondition(Cond), SDLoc(N), MVT::i32), C);
  }
  return SDValue();
}

/// PerformVMOVRRDCombine - Target-specific dag combine xforms for
/// ARMISD::VMOVRRD.
static SDValue PerformVMOVRRDCombine(SDNode *N,
                                     TargetLowering::DAGCombinerInfo &DCI,
                                     const ARMSubtarget *Subtarget) {
  // vmovrrd(vmovdrr x, y) -> x,y
  SDValue InDouble = N->getOperand(0);
  if (InDouble.getOpcode() == ARMISD::VMOVDRR && Subtarget->hasFP64())
    return DCI.CombineTo(N, InDouble.getOperand(0), InDouble.getOperand(1));

  // vmovrrd(load f64) -> (load i32), (load i32)
  SDNode *InNode = InDouble.getNode();
  if (ISD::isNormalLoad(InNode) && InNode->hasOneUse() &&
      InNode->getValueType(0) == MVT::f64 &&
      InNode->getOperand(1).getOpcode() == ISD::FrameIndex &&
      !cast<LoadSDNode>(InNode)->isVolatile()) {
    // TODO: Should this be done for non-FrameIndex operands?
    LoadSDNode *LD = cast<LoadSDNode>(InNode);

    SelectionDAG &DAG = DCI.DAG;
    SDLoc DL(LD);
    SDValue BasePtr = LD->getBasePtr();
    SDValue NewLD1 =
        DAG.getLoad(MVT::i32, DL, LD->getChain(), BasePtr, LD->getPointerInfo(),
                    LD->getAlign(), LD->getMemOperand()->getFlags());

    SDValue OffsetPtr = DAG.getNode(ISD::ADD, DL, MVT::i32, BasePtr,
                                    DAG.getConstant(4, DL, MVT::i32));

    SDValue NewLD2 = DAG.getLoad(MVT::i32, DL, LD->getChain(), OffsetPtr,
                                 LD->getPointerInfo().getWithOffset(4),
                                 commonAlignment(LD->getAlign(), 4),
                                 LD->getMemOperand()->getFlags());

    DAG.ReplaceAllUsesOfValueWith(SDValue(LD, 1), NewLD2.getValue(1));
    if (DCI.DAG.getDataLayout().isBigEndian())
      std::swap (NewLD1, NewLD2);
    SDValue Result = DCI.CombineTo(N, NewLD1, NewLD2);
    return Result;
  }

  // VMOVRRD(extract(..(build_vector(a, b, c, d)))) -> a,b or c,d
  // VMOVRRD(extract(insert_vector(insert_vector(.., a, l1), b, l2))) -> a,b
  if (InDouble.getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
      isa<ConstantSDNode>(InDouble.getOperand(1))) {
    SDValue BV = InDouble.getOperand(0);
    // Look up through any nop bitcasts and vector_reg_casts. bitcasts may
    // change lane order under big endian.
    bool BVSwap = BV.getOpcode() == ISD::BITCAST;
    while (
        (BV.getOpcode() == ISD::BITCAST ||
         BV.getOpcode() == ARMISD::VECTOR_REG_CAST) &&
        (BV.getValueType() == MVT::v2f64 || BV.getValueType() == MVT::v2i64)) {
      BVSwap = BV.getOpcode() == ISD::BITCAST;
      BV = BV.getOperand(0);
    }
    if (BV.getValueType() != MVT::v4i32)
      return SDValue();

    // Handle buildvectors, pulling out the correct lane depending on
    // endianness.
    unsigned Offset = InDouble.getConstantOperandVal(1) == 1 ? 2 : 0;
    if (BV.getOpcode() == ISD::BUILD_VECTOR) {
      SDValue Op0 = BV.getOperand(Offset);
      SDValue Op1 = BV.getOperand(Offset + 1);
      if (!Subtarget->isLittle() && BVSwap)
        std::swap(Op0, Op1);

      return DCI.DAG.getMergeValues({Op0, Op1}, SDLoc(N));
    }

    // A chain of insert_vectors, grabbing the correct value of the chain of
    // inserts.
    SDValue Op0, Op1;
    while (BV.getOpcode() == ISD::INSERT_VECTOR_ELT) {
      if (isa<ConstantSDNode>(BV.getOperand(2))) {
        if (BV.getConstantOperandVal(2) == Offset)
          Op0 = BV.getOperand(1);
        if (BV.getConstantOperandVal(2) == Offset + 1)
          Op1 = BV.getOperand(1);
      }
      BV = BV.getOperand(0);
    }
    if (!Subtarget->isLittle() && BVSwap)
      std::swap(Op0, Op1);
    if (Op0 && Op1)
      return DCI.DAG.getMergeValues({Op0, Op1}, SDLoc(N));
  }

  return SDValue();
}

/// PerformVMOVDRRCombine - Target-specific dag combine xforms for
/// ARMISD::VMOVDRR.  This is also used for BUILD_VECTORs with 2 operands.
static SDValue PerformVMOVDRRCombine(SDNode *N, SelectionDAG &DAG) {
  // N=vmovrrd(X); vmovdrr(N:0, N:1) -> bit_convert(X)
  SDValue Op0 = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);
  if (Op0.getOpcode() == ISD::BITCAST)
    Op0 = Op0.getOperand(0);
  if (Op1.getOpcode() == ISD::BITCAST)
    Op1 = Op1.getOperand(0);
  if (Op0.getOpcode() == ARMISD::VMOVRRD &&
      Op0.getNode() == Op1.getNode() &&
      Op0.getResNo() == 0 && Op1.getResNo() == 1)
    return DAG.getNode(ISD::BITCAST, SDLoc(N),
                       N->getValueType(0), Op0.getOperand(0));
  return SDValue();
}

static SDValue PerformVMOVhrCombine(SDNode *N,
                                    TargetLowering::DAGCombinerInfo &DCI) {
  SDValue Op0 = N->getOperand(0);

  // VMOVhr (VMOVrh (X)) -> X
  if (Op0->getOpcode() == ARMISD::VMOVrh)
    return Op0->getOperand(0);

  // FullFP16: half values are passed in S-registers, and we don't
  // need any of the bitcast and moves:
  //
  //     t2: f32,ch1,gl1? = CopyFromReg ch, Register:f32 %0, gl?
  //   t5: i32 = bitcast t2
  // t18: f16 = ARMISD::VMOVhr t5
  // =>
  // tN: f16,ch2,gl2? = CopyFromReg ch, Register::f32 %0, gl?
  if (Op0->getOpcode() == ISD::BITCAST) {
    SDValue Copy = Op0->getOperand(0);
    if (Copy.getValueType() == MVT::f32 &&
        Copy->getOpcode() == ISD::CopyFromReg) {
      bool HasGlue = Copy->getNumOperands() == 3;
      SDValue Ops[] = {Copy->getOperand(0), Copy->getOperand(1),
                       HasGlue ? Copy->getOperand(2) : SDValue()};
      EVT OutTys[] = {N->getValueType(0), MVT::Other, MVT::Glue};
      SDValue NewCopy =
          DCI.DAG.getNode(ISD::CopyFromReg, SDLoc(N),
                          DCI.DAG.getVTList(ArrayRef(OutTys, HasGlue ? 3 : 2)),
                          ArrayRef(Ops, HasGlue ? 3 : 2));

      // Update Users, Chains, and Potential Glue.
      DCI.DAG.ReplaceAllUsesOfValueWith(SDValue(N, 0), NewCopy.getValue(0));
      DCI.DAG.ReplaceAllUsesOfValueWith(Copy.getValue(1), NewCopy.getValue(1));
      if (HasGlue)
        DCI.DAG.ReplaceAllUsesOfValueWith(Copy.getValue(2),
                                          NewCopy.getValue(2));

      return NewCopy;
    }
  }

  // fold (VMOVhr (load x)) -> (load (f16*)x)
  if (LoadSDNode *LN0 = dyn_cast<LoadSDNode>(Op0)) {
    if (LN0->hasOneUse() && LN0->isUnindexed() &&
        LN0->getMemoryVT() == MVT::i16) {
      SDValue Load =
          DCI.DAG.getLoad(N->getValueType(0), SDLoc(N), LN0->getChain(),
                          LN0->getBasePtr(), LN0->getMemOperand());
      DCI.DAG.ReplaceAllUsesOfValueWith(SDValue(N, 0), Load.getValue(0));
      DCI.DAG.ReplaceAllUsesOfValueWith(Op0.getValue(1), Load.getValue(1));
      return Load;
    }
  }

  // Only the bottom 16 bits of the source register are used.
  APInt DemandedMask = APInt::getLowBitsSet(32, 16);
  const TargetLowering &TLI = DCI.DAG.getTargetLoweringInfo();
  if (TLI.SimplifyDemandedBits(Op0, DemandedMask, DCI))
    return SDValue(N, 0);

  return SDValue();
}

static SDValue PerformVMOVrhCombine(SDNode *N, SelectionDAG &DAG) {
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // fold (VMOVrh (fpconst x)) -> const x
  if (ConstantFPSDNode *C = dyn_cast<ConstantFPSDNode>(N0)) {
    APFloat V = C->getValueAPF();
    return DAG.getConstant(V.bitcastToAPInt().getZExtValue(), SDLoc(N), VT);
  }

  // fold (VMOVrh (load x)) -> (zextload (i16*)x)
  if (ISD::isNormalLoad(N0.getNode()) && N0.hasOneUse()) {
    LoadSDNode *LN0 = cast<LoadSDNode>(N0);

    SDValue Load =
        DAG.getExtLoad(ISD::ZEXTLOAD, SDLoc(N), VT, LN0->getChain(),
                       LN0->getBasePtr(), MVT::i16, LN0->getMemOperand());
    DAG.ReplaceAllUsesOfValueWith(SDValue(N, 0), Load.getValue(0));
    DAG.ReplaceAllUsesOfValueWith(N0.getValue(1), Load.getValue(1));
    return Load;
  }

  // Fold VMOVrh(extract(x, n)) -> vgetlaneu(x, n)
  if (N0->getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
      isa<ConstantSDNode>(N0->getOperand(1)))
    return DAG.getNode(ARMISD::VGETLANEu, SDLoc(N), VT, N0->getOperand(0),
                       N0->getOperand(1));

  return SDValue();
}

/// hasNormalLoadOperand - Check if any of the operands of a BUILD_VECTOR node
/// are normal, non-volatile loads.  If so, it is profitable to bitcast an
/// i64 vector to have f64 elements, since the value can then be loaded
/// directly into a VFP register.
static bool hasNormalLoadOperand(SDNode *N) {
  unsigned NumElts = N->getValueType(0).getVectorNumElements();
  for (unsigned i = 0; i < NumElts; ++i) {
    SDNode *Elt = N->getOperand(i).getNode();
    if (ISD::isNormalLoad(Elt) && !cast<LoadSDNode>(Elt)->isVolatile())
      return true;
  }
  return false;
}

/// PerformBUILD_VECTORCombine - Target-specific dag combine xforms for
/// ISD::BUILD_VECTOR.
static SDValue PerformBUILD_VECTORCombine(SDNode *N,
                                          TargetLowering::DAGCombinerInfo &DCI,
                                          const ARMSubtarget *Subtarget) {
  // build_vector(N=ARMISD::VMOVRRD(X), N:1) -> bit_convert(X):
  // VMOVRRD is introduced when legalizing i64 types.  It forces the i64 value
  // into a pair of GPRs, which is fine when the value is used as a scalar,
  // but if the i64 value is converted to a vector, we need to undo the VMOVRRD.
  SelectionDAG &DAG = DCI.DAG;
  if (N->getNumOperands() == 2)
    if (SDValue RV = PerformVMOVDRRCombine(N, DAG))
      return RV;

  // Load i64 elements as f64 values so that type legalization does not split
  // them up into i32 values.
  EVT VT = N->getValueType(0);
  if (VT.getVectorElementType() != MVT::i64 || !hasNormalLoadOperand(N))
    return SDValue();
  SDLoc dl(N);
  SmallVector<SDValue, 8> Ops;
  unsigned NumElts = VT.getVectorNumElements();
  for (unsigned i = 0; i < NumElts; ++i) {
    SDValue V = DAG.getNode(ISD::BITCAST, dl, MVT::f64, N->getOperand(i));
    Ops.push_back(V);
    // Make the DAGCombiner fold the bitcast.
    DCI.AddToWorklist(V.getNode());
  }
  EVT FloatVT = EVT::getVectorVT(*DAG.getContext(), MVT::f64, NumElts);
  SDValue BV = DAG.getBuildVector(FloatVT, dl, Ops);
  return DAG.getNode(ISD::BITCAST, dl, VT, BV);
}

/// Target-specific dag combine xforms for ARMISD::BUILD_VECTOR.
static SDValue
PerformARMBUILD_VECTORCombine(SDNode *N, TargetLowering::DAGCombinerInfo &DCI) {
  // ARMISD::BUILD_VECTOR is introduced when legalizing ISD::BUILD_VECTOR.
  // At that time, we may have inserted bitcasts from integer to float.
  // If these bitcasts have survived DAGCombine, change the lowering of this
  // BUILD_VECTOR in something more vector friendly, i.e., that does not
  // force to use floating point types.

  // Make sure we can change the type of the vector.
  // This is possible iff:
  // 1. The vector is only used in a bitcast to a integer type. I.e.,
  //    1.1. Vector is used only once.
  //    1.2. Use is a bit convert to an integer type.
  // 2. The size of its operands are 32-bits (64-bits are not legal).
  EVT VT = N->getValueType(0);
  EVT EltVT = VT.getVectorElementType();

  // Check 1.1. and 2.
  if (EltVT.getSizeInBits() != 32 || !N->hasOneUse())
    return SDValue();

  // By construction, the input type must be float.
  assert(EltVT == MVT::f32 && "Unexpected type!");

  // Check 1.2.
  SDNode *Use = *N->use_begin();
  if (Use->getOpcode() != ISD::BITCAST ||
      Use->getValueType(0).isFloatingPoint())
    return SDValue();

  // Check profitability.
  // Model is, if more than half of the relevant operands are bitcast from
  // i32, turn the build_vector into a sequence of insert_vector_elt.
  // Relevant operands are everything that is not statically
  // (i.e., at compile time) bitcasted.
  unsigned NumOfBitCastedElts = 0;
  unsigned NumElts = VT.getVectorNumElements();
  unsigned NumOfRelevantElts = NumElts;
  for (unsigned Idx = 0; Idx < NumElts; ++Idx) {
    SDValue Elt = N->getOperand(Idx);
    if (Elt->getOpcode() == ISD::BITCAST) {
      // Assume only bit cast to i32 will go away.
      if (Elt->getOperand(0).getValueType() == MVT::i32)
        ++NumOfBitCastedElts;
    } else if (Elt.isUndef() || isa<ConstantSDNode>(Elt))
      // Constants are statically casted, thus do not count them as
      // relevant operands.
      --NumOfRelevantElts;
  }

  // Check if more than half of the elements require a non-free bitcast.
  if (NumOfBitCastedElts <= NumOfRelevantElts / 2)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  // Create the new vector type.
  EVT VecVT = EVT::getVectorVT(*DAG.getContext(), MVT::i32, NumElts);
  // Check if the type is legal.
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (!TLI.isTypeLegal(VecVT))
    return SDValue();

  // Combine:
  // ARMISD::BUILD_VECTOR E1, E2, ..., EN.
  // => BITCAST INSERT_VECTOR_ELT
  //                      (INSERT_VECTOR_ELT (...), (BITCAST EN-1), N-1),
  //                      (BITCAST EN), N.
  SDValue Vec = DAG.getUNDEF(VecVT);
  SDLoc dl(N);
  for (unsigned Idx = 0 ; Idx < NumElts; ++Idx) {
    SDValue V = N->getOperand(Idx);
    if (V.isUndef())
      continue;
    if (V.getOpcode() == ISD::BITCAST &&
        V->getOperand(0).getValueType() == MVT::i32)
      // Fold obvious case.
      V = V.getOperand(0);
    else {
      V = DAG.getNode(ISD::BITCAST, SDLoc(V), MVT::i32, V);
      // Make the DAGCombiner fold the bitcasts.
      DCI.AddToWorklist(V.getNode());
    }
    SDValue LaneIdx = DAG.getConstant(Idx, dl, MVT::i32);
    Vec = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, VecVT, Vec, V, LaneIdx);
  }
  Vec = DAG.getNode(ISD::BITCAST, dl, VT, Vec);
  // Make the DAGCombiner fold the bitcasts.
  DCI.AddToWorklist(Vec.getNode());
  return Vec;
}

static SDValue
PerformPREDICATE_CASTCombine(SDNode *N, TargetLowering::DAGCombinerInfo &DCI) {
  EVT VT = N->getValueType(0);
  SDValue Op = N->getOperand(0);
  SDLoc dl(N);

  // PREDICATE_CAST(PREDICATE_CAST(x)) == PREDICATE_CAST(x)
  if (Op->getOpcode() == ARMISD::PREDICATE_CAST) {
    // If the valuetypes are the same, we can remove the cast entirely.
    if (Op->getOperand(0).getValueType() == VT)
      return Op->getOperand(0);
    return DCI.DAG.getNode(ARMISD::PREDICATE_CAST, dl, VT, Op->getOperand(0));
  }

  // Turn pred_cast(xor x, -1) into xor(pred_cast x, -1), in order to produce
  // more VPNOT which might get folded as else predicates.
  if (Op.getValueType() == MVT::i32 && isBitwiseNot(Op)) {
    SDValue X =
        DCI.DAG.getNode(ARMISD::PREDICATE_CAST, dl, VT, Op->getOperand(0));
    SDValue C = DCI.DAG.getNode(ARMISD::PREDICATE_CAST, dl, VT,
                                DCI.DAG.getConstant(65535, dl, MVT::i32));
    return DCI.DAG.getNode(ISD::XOR, dl, VT, X, C);
  }

  // Only the bottom 16 bits of the source register are used.
  if (Op.getValueType() == MVT::i32) {
    APInt DemandedMask = APInt::getLowBitsSet(32, 16);
    const TargetLowering &TLI = DCI.DAG.getTargetLoweringInfo();
    if (TLI.SimplifyDemandedBits(Op, DemandedMask, DCI))
      return SDValue(N, 0);
  }
  return SDValue();
}

static SDValue PerformVECTOR_REG_CASTCombine(SDNode *N, SelectionDAG &DAG,
                                             const ARMSubtarget *ST) {
  EVT VT = N->getValueType(0);
  SDValue Op = N->getOperand(0);
  SDLoc dl(N);

  // Under Little endian, a VECTOR_REG_CAST is equivalent to a BITCAST
  if (ST->isLittle())
    return DAG.getNode(ISD::BITCAST, dl, VT, Op);

  // VECTOR_REG_CAST undef -> undef
  if (Op.isUndef())
    return DAG.getUNDEF(VT);

  // VECTOR_REG_CAST(VECTOR_REG_CAST(x)) == VECTOR_REG_CAST(x)
  if (Op->getOpcode() == ARMISD::VECTOR_REG_CAST) {
    // If the valuetypes are the same, we can remove the cast entirely.
    if (Op->getOperand(0).getValueType() == VT)
      return Op->getOperand(0);
    return DAG.getNode(ARMISD::VECTOR_REG_CAST, dl, VT, Op->getOperand(0));
  }

  return SDValue();
}

static SDValue PerformVCMPCombine(SDNode *N, SelectionDAG &DAG,
                                  const ARMSubtarget *Subtarget) {
  if (!Subtarget->hasMVEIntegerOps())
    return SDValue();

  EVT VT = N->getValueType(0);
  SDValue Op0 = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);
  ARMCC::CondCodes Cond = (ARMCC::CondCodes)N->getConstantOperandVal(2);
  SDLoc dl(N);

  // vcmp X, 0, cc -> vcmpz X, cc
  if (isZeroVector(Op1))
    return DAG.getNode(ARMISD::VCMPZ, dl, VT, Op0, N->getOperand(2));

  unsigned SwappedCond = getSwappedCondition(Cond);
  if (isValidMVECond(SwappedCond, VT.isFloatingPoint())) {
    // vcmp 0, X, cc -> vcmpz X, reversed(cc)
    if (isZeroVector(Op0))
      return DAG.getNode(ARMISD::VCMPZ, dl, VT, Op1,
                         DAG.getConstant(SwappedCond, dl, MVT::i32));
    // vcmp vdup(Y), X, cc -> vcmp X, vdup(Y), reversed(cc)
    if (Op0->getOpcode() == ARMISD::VDUP && Op1->getOpcode() != ARMISD::VDUP)
      return DAG.getNode(ARMISD::VCMP, dl, VT, Op1, Op0,
                         DAG.getConstant(SwappedCond, dl, MVT::i32));
  }

  return SDValue();
}

/// PerformInsertEltCombine - Target-specific dag combine xforms for
/// ISD::INSERT_VECTOR_ELT.
static SDValue PerformInsertEltCombine(SDNode *N,
                                       TargetLowering::DAGCombinerInfo &DCI) {
  // Bitcast an i64 load inserted into a vector to f64.
  // Otherwise, the i64 value will be legalized to a pair of i32 values.
  EVT VT = N->getValueType(0);
  SDNode *Elt = N->getOperand(1).getNode();
  if (VT.getVectorElementType() != MVT::i64 ||
      !ISD::isNormalLoad(Elt) || cast<LoadSDNode>(Elt)->isVolatile())
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc dl(N);
  EVT FloatVT = EVT::getVectorVT(*DAG.getContext(), MVT::f64,
                                 VT.getVectorNumElements());
  SDValue Vec = DAG.getNode(ISD::BITCAST, dl, FloatVT, N->getOperand(0));
  SDValue V = DAG.getNode(ISD::BITCAST, dl, MVT::f64, N->getOperand(1));
  // Make the DAGCombiner fold the bitcasts.
  DCI.AddToWorklist(Vec.getNode());
  DCI.AddToWorklist(V.getNode());
  SDValue InsElt = DAG.getNode(ISD::INSERT_VECTOR_ELT, dl, FloatVT,
                               Vec, V, N->getOperand(2));
  return DAG.getNode(ISD::BITCAST, dl, VT, InsElt);
}

// Convert a pair of extracts from the same base vector to a VMOVRRD. Either
// directly or bitcast to an integer if the original is a float vector.
// extract(x, n); extract(x, n+1)  ->  VMOVRRD(extract v2f64 x, n/2)
// bitcast(extract(x, n)); bitcast(extract(x, n+1))  ->  VMOVRRD(extract x, n/2)
static SDValue
PerformExtractEltToVMOVRRD(SDNode *N, TargetLowering::DAGCombinerInfo &DCI) {
  EVT VT = N->getValueType(0);
  SDLoc dl(N);

  if (!DCI.isAfterLegalizeDAG() || VT != MVT::i32 ||
      !DCI.DAG.getTargetLoweringInfo().isTypeLegal(MVT::f64))
    return SDValue();

  SDValue Ext = SDValue(N, 0);
  if (Ext.getOpcode() == ISD::BITCAST &&
      Ext.getOperand(0).getValueType() == MVT::f32)
    Ext = Ext.getOperand(0);
  if (Ext.getOpcode() != ISD::EXTRACT_VECTOR_ELT ||
      !isa<ConstantSDNode>(Ext.getOperand(1)) ||
      Ext.getConstantOperandVal(1) % 2 != 0)
    return SDValue();
  if (Ext->use_size() == 1 &&
      (Ext->use_begin()->getOpcode() == ISD::SINT_TO_FP ||
       Ext->use_begin()->getOpcode() == ISD::UINT_TO_FP))
    return SDValue();

  SDValue Op0 = Ext.getOperand(0);
  EVT VecVT = Op0.getValueType();
  unsigned ResNo = Op0.getResNo();
  unsigned Lane = Ext.getConstantOperandVal(1);
  if (VecVT.getVectorNumElements() != 4)
    return SDValue();

  // Find another extract, of Lane + 1
  auto OtherIt = find_if(Op0->uses(), [&](SDNode *V) {
    return V->getOpcode() == ISD::EXTRACT_VECTOR_ELT &&
           isa<ConstantSDNode>(V->getOperand(1)) &&
           V->getConstantOperandVal(1) == Lane + 1 &&
           V->getOperand(0).getResNo() == ResNo;
  });
  if (OtherIt == Op0->uses().end())
    return SDValue();

  // For float extracts, we need to be converting to a i32 for both vector
  // lanes.
  SDValue OtherExt(*OtherIt, 0);
  if (OtherExt.getValueType() != MVT::i32) {
    if (OtherExt->use_size() != 1 ||
        OtherExt->use_begin()->getOpcode() != ISD::BITCAST ||
        OtherExt->use_begin()->getValueType(0) != MVT::i32)
      return SDValue();
    OtherExt = SDValue(*OtherExt->use_begin(), 0);
  }

  // Convert the type to a f64 and extract with a VMOVRRD.
  SDValue F64 = DCI.DAG.getNode(
      ISD::EXTRACT_VECTOR_ELT, dl, MVT::f64,
      DCI.DAG.getNode(ARMISD::VECTOR_REG_CAST, dl, MVT::v2f64, Op0),
      DCI.DAG.getConstant(Ext.getConstantOperandVal(1) / 2, dl, MVT::i32));
  SDValue VMOVRRD =
      DCI.DAG.getNode(ARMISD::VMOVRRD, dl, {MVT::i32, MVT::i32}, F64);

  DCI.CombineTo(OtherExt.getNode(), SDValue(VMOVRRD.getNode(), 1));
  return VMOVRRD;
}

static SDValue PerformExtractEltCombine(SDNode *N,
                                        TargetLowering::DAGCombinerInfo &DCI,
                                        const ARMSubtarget *ST) {
  SDValue Op0 = N->getOperand(0);
  EVT VT = N->getValueType(0);
  SDLoc dl(N);

  // extract (vdup x) -> x
  if (Op0->getOpcode() == ARMISD::VDUP) {
    SDValue X = Op0->getOperand(0);
    if (VT == MVT::f16 && X.getValueType() == MVT::i32)
      return DCI.DAG.getNode(ARMISD::VMOVhr, dl, VT, X);
    if (VT == MVT::i32 && X.getValueType() == MVT::f16)
      return DCI.DAG.getNode(ARMISD::VMOVrh, dl, VT, X);
    if (VT == MVT::f32 && X.getValueType() == MVT::i32)
      return DCI.DAG.getNode(ISD::BITCAST, dl, VT, X);

    while (X.getValueType() != VT && X->getOpcode() == ISD::BITCAST)
      X = X->getOperand(0);
    if (X.getValueType() == VT)
      return X;
  }

  // extract ARM_BUILD_VECTOR -> x
  if (Op0->getOpcode() == ARMISD::BUILD_VECTOR &&
      isa<ConstantSDNode>(N->getOperand(1)) &&
      N->getConstantOperandVal(1) < Op0.getNumOperands()) {
    return Op0.getOperand(N->getConstantOperandVal(1));
  }

  // extract(bitcast(BUILD_VECTOR(VMOVDRR(a, b), ..))) -> a or b
  if (Op0.getValueType() == MVT::v4i32 &&
      isa<ConstantSDNode>(N->getOperand(1)) &&
      Op0.getOpcode() == ISD::BITCAST &&
      Op0.getOperand(0).getOpcode() == ISD::BUILD_VECTOR &&
      Op0.getOperand(0).getValueType() == MVT::v2f64) {
    SDValue BV = Op0.getOperand(0);
    unsigned Offset = N->getConstantOperandVal(1);
    SDValue MOV = BV.getOperand(Offset < 2 ? 0 : 1);
    if (MOV.getOpcode() == ARMISD::VMOVDRR)
      return MOV.getOperand(ST->isLittle() ? Offset % 2 : 1 - Offset % 2);
  }

  // extract x, n; extract x, n+1  ->  VMOVRRD x
  if (SDValue R = PerformExtractEltToVMOVRRD(N, DCI))
    return R;

  // extract (MVETrunc(x)) -> extract x
  if (Op0->getOpcode() == ARMISD::MVETRUNC) {
    unsigned Idx = N->getConstantOperandVal(1);
    unsigned Vec =
        Idx / Op0->getOperand(0).getValueType().getVectorNumElements();
    unsigned SubIdx =
        Idx % Op0->getOperand(0).getValueType().getVectorNumElements();
    return DCI.DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, VT, Op0.getOperand(Vec),
                           DCI.DAG.getConstant(SubIdx, dl, MVT::i32));
  }

  return SDValue();
}

static SDValue PerformSignExtendInregCombine(SDNode *N, SelectionDAG &DAG) {
  SDValue Op = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // sext_inreg(VGETLANEu) -> VGETLANEs
  if (Op.getOpcode() == ARMISD::VGETLANEu &&
      cast<VTSDNode>(N->getOperand(1))->getVT() ==
          Op.getOperand(0).getValueType().getScalarType())
    return DAG.getNode(ARMISD::VGETLANEs, SDLoc(N), VT, Op.getOperand(0),
                       Op.getOperand(1));

  return SDValue();
}

static SDValue
PerformInsertSubvectorCombine(SDNode *N, TargetLowering::DAGCombinerInfo &DCI) {
  SDValue Vec = N->getOperand(0);
  SDValue SubVec = N->getOperand(1);
  uint64_t IdxVal = N->getConstantOperandVal(2);
  EVT VecVT = Vec.getValueType();
  EVT SubVT = SubVec.getValueType();

  // Only do this for legal fixed vector types.
  if (!VecVT.isFixedLengthVector() ||
      !DCI.DAG.getTargetLoweringInfo().isTypeLegal(VecVT) ||
      !DCI.DAG.getTargetLoweringInfo().isTypeLegal(SubVT))
    return SDValue();

  // Ignore widening patterns.
  if (IdxVal == 0 && Vec.isUndef())
    return SDValue();

  // Subvector must be half the width and an "aligned" insertion.
  unsigned NumSubElts = SubVT.getVectorNumElements();
  if ((SubVT.getSizeInBits() * 2) != VecVT.getSizeInBits() ||
      (IdxVal != 0 && IdxVal != NumSubElts))
    return SDValue();

  // Fold insert_subvector -> concat_vectors
  // insert_subvector(Vec,Sub,lo) -> concat_vectors(Sub,extract(Vec,hi))
  // insert_subvector(Vec,Sub,hi) -> concat_vectors(extract(Vec,lo),Sub)
  SDLoc DL(N);
  SDValue Lo, Hi;
  if (IdxVal == 0) {
    Lo = SubVec;
    Hi = DCI.DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, SubVT, Vec,
                         DCI.DAG.getVectorIdxConstant(NumSubElts, DL));
  } else {
    Lo = DCI.DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, SubVT, Vec,
                         DCI.DAG.getVectorIdxConstant(0, DL));
    Hi = SubVec;
  }
  return DCI.DAG.getNode(ISD::CONCAT_VECTORS, DL, VecVT, Lo, Hi);
}

// shuffle(MVETrunc(x, y)) -> VMOVN(x, y)
static SDValue PerformShuffleVMOVNCombine(ShuffleVectorSDNode *N,
                                          SelectionDAG &DAG) {
  SDValue Trunc = N->getOperand(0);
  EVT VT = Trunc.getValueType();
  if (Trunc.getOpcode() != ARMISD::MVETRUNC || !N->getOperand(1).isUndef())
    return SDValue();

  SDLoc DL(Trunc);
  if (isVMOVNTruncMask(N->getMask(), VT, false))
    return DAG.getNode(
        ARMISD::VMOVN, DL, VT,
        DAG.getNode(ARMISD::VECTOR_REG_CAST, DL, VT, Trunc.getOperand(0)),
        DAG.getNode(ARMISD::VECTOR_REG_CAST, DL, VT, Trunc.getOperand(1)),
        DAG.getConstant(1, DL, MVT::i32));
  else if (isVMOVNTruncMask(N->getMask(), VT, true))
    return DAG.getNode(
        ARMISD::VMOVN, DL, VT,
        DAG.getNode(ARMISD::VECTOR_REG_CAST, DL, VT, Trunc.getOperand(1)),
        DAG.getNode(ARMISD::VECTOR_REG_CAST, DL, VT, Trunc.getOperand(0)),
        DAG.getConstant(1, DL, MVT::i32));
  return SDValue();
}

/// PerformVECTOR_SHUFFLECombine - Target-specific dag combine xforms for
/// ISD::VECTOR_SHUFFLE.
static SDValue PerformVECTOR_SHUFFLECombine(SDNode *N, SelectionDAG &DAG) {
  if (SDValue R = PerformShuffleVMOVNCombine(cast<ShuffleVectorSDNode>(N), DAG))
    return R;

  // The LLVM shufflevector instruction does not require the shuffle mask
  // length to match the operand vector length, but ISD::VECTOR_SHUFFLE does
  // have that requirement.  When translating to ISD::VECTOR_SHUFFLE, if the
  // operands do not match the mask length, they are extended by concatenating
  // them with undef vectors.  That is probably the right thing for other
  // targets, but for NEON it is better to concatenate two double-register
  // size vector operands into a single quad-register size vector.  Do that
  // transformation here:
  //   shuffle(concat(v1, undef), concat(v2, undef)) ->
  //   shuffle(concat(v1, v2), undef)
  SDValue Op0 = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);
  if (Op0.getOpcode() != ISD::CONCAT_VECTORS ||
      Op1.getOpcode() != ISD::CONCAT_VECTORS ||
      Op0.getNumOperands() != 2 ||
      Op1.getNumOperands() != 2)
    return SDValue();
  SDValue Concat0Op1 = Op0.getOperand(1);
  SDValue Concat1Op1 = Op1.getOperand(1);
  if (!Concat0Op1.isUndef() || !Concat1Op1.isUndef())
    return SDValue();
  // Skip the transformation if any of the types are illegal.
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT VT = N->getValueType(0);
  if (!TLI.isTypeLegal(VT) ||
      !TLI.isTypeLegal(Concat0Op1.getValueType()) ||
      !TLI.isTypeLegal(Concat1Op1.getValueType()))
    return SDValue();

  SDValue NewConcat = DAG.getNode(ISD::CONCAT_VECTORS, SDLoc(N), VT,
                                  Op0.getOperand(0), Op1.getOperand(0));
  // Translate the shuffle mask.
  SmallVector<int, 16> NewMask;
  unsigned NumElts = VT.getVectorNumElements();
  unsigned HalfElts = NumElts/2;
  ShuffleVectorSDNode *SVN = cast<ShuffleVectorSDNode>(N);
  for (unsigned n = 0; n < NumElts; ++n) {
    int MaskElt = SVN->getMaskElt(n);
    int NewElt = -1;
    if (MaskElt < (int)HalfElts)
      NewElt = MaskElt;
    else if (MaskElt >= (int)NumElts && MaskElt < (int)(NumElts + HalfElts))
      NewElt = HalfElts + MaskElt - NumElts;
    NewMask.push_back(NewElt);
  }
  return DAG.getVectorShuffle(VT, SDLoc(N), NewConcat,
                              DAG.getUNDEF(VT), NewMask);
}

/// Load/store instruction that can be merged with a base address
/// update
struct BaseUpdateTarget {
  SDNode *N;
  bool isIntrinsic;
  bool isStore;
  unsigned AddrOpIdx;
};

struct BaseUpdateUser {
  /// Instruction that updates a pointer
  SDNode *N;
  /// Pointer increment operand
  SDValue Inc;
  /// Pointer increment value if it is a constant, or 0 otherwise
  unsigned ConstInc;
};

static bool TryCombineBaseUpdate(struct BaseUpdateTarget &Target,
                                 struct BaseUpdateUser &User,
                                 bool SimpleConstIncOnly,
                                 TargetLowering::DAGCombinerInfo &DCI) {
  SelectionDAG &DAG = DCI.DAG;
  SDNode *N = Target.N;
  MemSDNode *MemN = cast<MemSDNode>(N);
  SDLoc dl(N);

  // Find the new opcode for the updating load/store.
  bool isLoadOp = true;
  bool isLaneOp = false;
  // Workaround for vst1x and vld1x intrinsics which do not have alignment
  // as an operand.
  bool hasAlignment = true;
  unsigned NewOpc = 0;
  unsigned NumVecs = 0;
  if (Target.isIntrinsic) {
    unsigned IntNo = N->getConstantOperandVal(1);
    switch (IntNo) {
    default:
      llvm_unreachable("unexpected intrinsic for Neon base update");
    case Intrinsic::arm_neon_vld1:
      NewOpc = ARMISD::VLD1_UPD;
      NumVecs = 1;
      break;
    case Intrinsic::arm_neon_vld2:
      NewOpc = ARMISD::VLD2_UPD;
      NumVecs = 2;
      break;
    case Intrinsic::arm_neon_vld3:
      NewOpc = ARMISD::VLD3_UPD;
      NumVecs = 3;
      break;
    case Intrinsic::arm_neon_vld4:
      NewOpc = ARMISD::VLD4_UPD;
      NumVecs = 4;
      break;
    case Intrinsic::arm_neon_vld1x2:
      NewOpc = ARMISD::VLD1x2_UPD;
      NumVecs = 2;
      hasAlignment = false;
      break;
    case Intrinsic::arm_neon_vld1x3:
      NewOpc = ARMISD::VLD1x3_UPD;
      NumVecs = 3;
      hasAlignment = false;
      break;
    case Intrinsic::arm_neon_vld1x4:
      NewOpc = ARMISD::VLD1x4_UPD;
      NumVecs = 4;
      hasAlignment = false;
      break;
    case Intrinsic::arm_neon_vld2dup:
      NewOpc = ARMISD::VLD2DUP_UPD;
      NumVecs = 2;
      break;
    case Intrinsic::arm_neon_vld3dup:
      NewOpc = ARMISD::VLD3DUP_UPD;
      NumVecs = 3;
      break;
    case Intrinsic::arm_neon_vld4dup:
      NewOpc = ARMISD::VLD4DUP_UPD;
      NumVecs = 4;
      break;
    case Intrinsic::arm_neon_vld2lane:
      NewOpc = ARMISD::VLD2LN_UPD;
      NumVecs = 2;
      isLaneOp = true;
      break;
    case Intrinsic::arm_neon_vld3lane:
      NewOpc = ARMISD::VLD3LN_UPD;
      NumVecs = 3;
      isLaneOp = true;
      break;
    case Intrinsic::arm_neon_vld4lane:
      NewOpc = ARMISD::VLD4LN_UPD;
      NumVecs = 4;
      isLaneOp = true;
      break;
    case Intrinsic::arm_neon_vst1:
      NewOpc = ARMISD::VST1_UPD;
      NumVecs = 1;
      isLoadOp = false;
      break;
    case Intrinsic::arm_neon_vst2:
      NewOpc = ARMISD::VST2_UPD;
      NumVecs = 2;
      isLoadOp = false;
      break;
    case Intrinsic::arm_neon_vst3:
      NewOpc = ARMISD::VST3_UPD;
      NumVecs = 3;
      isLoadOp = false;
      break;
    case Intrinsic::arm_neon_vst4:
      NewOpc = ARMISD::VST4_UPD;
      NumVecs = 4;
      isLoadOp = false;
      break;
    case Intrinsic::arm_neon_vst2lane:
      NewOpc = ARMISD::VST2LN_UPD;
      NumVecs = 2;
      isLoadOp = false;
      isLaneOp = true;
      break;
    case Intrinsic::arm_neon_vst3lane:
      NewOpc = ARMISD::VST3LN_UPD;
      NumVecs = 3;
      isLoadOp = false;
      isLaneOp = true;
      break;
    case Intrinsic::arm_neon_vst4lane:
      NewOpc = ARMISD::VST4LN_UPD;
      NumVecs = 4;
      isLoadOp = false;
      isLaneOp = true;
      break;
    case Intrinsic::arm_neon_vst1x2:
      NewOpc = ARMISD::VST1x2_UPD;
      NumVecs = 2;
      isLoadOp = false;
      hasAlignment = false;
      break;
    case Intrinsic::arm_neon_vst1x3:
      NewOpc = ARMISD::VST1x3_UPD;
      NumVecs = 3;
      isLoadOp = false;
      hasAlignment = false;
      break;
    case Intrinsic::arm_neon_vst1x4:
      NewOpc = ARMISD::VST1x4_UPD;
      NumVecs = 4;
      isLoadOp = false;
      hasAlignment = false;
      break;
    }
  } else {
    isLaneOp = true;
    switch (N->getOpcode()) {
    default:
      llvm_unreachable("unexpected opcode for Neon base update");
    case ARMISD::VLD1DUP:
      NewOpc = ARMISD::VLD1DUP_UPD;
      NumVecs = 1;
      break;
    case ARMISD::VLD2DUP:
      NewOpc = ARMISD::VLD2DUP_UPD;
      NumVecs = 2;
      break;
    case ARMISD::VLD3DUP:
      NewOpc = ARMISD::VLD3DUP_UPD;
      NumVecs = 3;
      break;
    case ARMISD::VLD4DUP:
      NewOpc = ARMISD::VLD4DUP_UPD;
      NumVecs = 4;
      break;
    case ISD::LOAD:
      NewOpc = ARMISD::VLD1_UPD;
      NumVecs = 1;
      isLaneOp = false;
      break;
    case ISD::STORE:
      NewOpc = ARMISD::VST1_UPD;
      NumVecs = 1;
      isLaneOp = false;
      isLoadOp = false;
      break;
    }
  }

  // Find the size of memory referenced by the load/store.
  EVT VecTy;
  if (isLoadOp) {
    VecTy = N->getValueType(0);
  } else if (Target.isIntrinsic) {
    VecTy = N->getOperand(Target.AddrOpIdx + 1).getValueType();
  } else {
    assert(Target.isStore &&
           "Node has to be a load, a store, or an intrinsic!");
    VecTy = N->getOperand(1).getValueType();
  }

  bool isVLDDUPOp =
      NewOpc == ARMISD::VLD1DUP_UPD || NewOpc == ARMISD::VLD2DUP_UPD ||
      NewOpc == ARMISD::VLD3DUP_UPD || NewOpc == ARMISD::VLD4DUP_UPD;

  unsigned NumBytes = NumVecs * VecTy.getSizeInBits() / 8;
  if (isLaneOp || isVLDDUPOp)
    NumBytes /= VecTy.getVectorNumElements();

  if (NumBytes >= 3 * 16 && User.ConstInc != NumBytes) {
    // VLD3/4 and VST3/4 for 128-bit vectors are implemented with two
    // separate instructions that make it harder to use a non-constant update.
    return false;
  }

  if (SimpleConstIncOnly && User.ConstInc != NumBytes)
    return false;

  // OK, we found an ADD we can fold into the base update.
  // Now, create a _UPD node, taking care of not breaking alignment.

  EVT AlignedVecTy = VecTy;
  Align Alignment = MemN->getAlign();

  // If this is a less-than-standard-aligned load/store, change the type to
  // match the standard alignment.
  // The alignment is overlooked when selecting _UPD variants; and it's
  // easier to introduce bitcasts here than fix that.
  // There are 3 ways to get to this base-update combine:
  // - intrinsics: they are assumed to be properly aligned (to the standard
  //   alignment of the memory type), so we don't need to do anything.
  // - ARMISD::VLDx nodes: they are only generated from the aforementioned
  //   intrinsics, so, likewise, there's nothing to do.
  // - generic load/store instructions: the alignment is specified as an
  //   explicit operand, rather than implicitly as the standard alignment
  //   of the memory type (like the intrisics).  We need to change the
  //   memory type to match the explicit alignment.  That way, we don't
  //   generate non-standard-aligned ARMISD::VLDx nodes.
  if (isa<LSBaseSDNode>(N)) {
    if (Alignment.value() < VecTy.getScalarSizeInBits() / 8) {
      MVT EltTy = MVT::getIntegerVT(Alignment.value() * 8);
      assert(NumVecs == 1 && "Unexpected multi-element generic load/store.");
      assert(!isLaneOp && "Unexpected generic load/store lane.");
      unsigned NumElts = NumBytes / (EltTy.getSizeInBits() / 8);
      AlignedVecTy = MVT::getVectorVT(EltTy, NumElts);
    }
    // Don't set an explicit alignment on regular load/stores that we want
    // to transform to VLD/VST 1_UPD nodes.
    // This matches the behavior of regular load/stores, which only get an
    // explicit alignment if the MMO alignment is larger than the standard
    // alignment of the memory type.
    // Intrinsics, however, always get an explicit alignment, set to the
    // alignment of the MMO.
    Alignment = Align(1);
  }

  // Create the new updating load/store node.
  // First, create an SDVTList for the new updating node's results.
  EVT Tys[6];
  unsigned NumResultVecs = (isLoadOp ? NumVecs : 0);
  unsigned n;
  for (n = 0; n < NumResultVecs; ++n)
    Tys[n] = AlignedVecTy;
  Tys[n++] = MVT::i32;
  Tys[n] = MVT::Other;
  SDVTList SDTys = DAG.getVTList(ArrayRef(Tys, NumResultVecs + 2));

  // Then, gather the new node's operands.
  SmallVector<SDValue, 8> Ops;
  Ops.push_back(N->getOperand(0)); // incoming chain
  Ops.push_back(N->getOperand(Target.AddrOpIdx));
  Ops.push_back(User.Inc);

  if (StoreSDNode *StN = dyn_cast<StoreSDNode>(N)) {
    // Try to match the intrinsic's signature
    Ops.push_back(StN->getValue());
  } else {
    // Loads (and of course intrinsics) match the intrinsics' signature,
    // so just add all but the alignment operand.
    unsigned LastOperand =
        hasAlignment ? N->getNumOperands() - 1 : N->getNumOperands();
    for (unsigned i = Target.AddrOpIdx + 1; i < LastOperand; ++i)
      Ops.push_back(N->getOperand(i));
  }

  // For all node types, the alignment operand is always the last one.
  Ops.push_back(DAG.getConstant(Alignment.value(), dl, MVT::i32));

  // If this is a non-standard-aligned STORE, the penultimate operand is the
  // stored value.  Bitcast it to the aligned type.
  if (AlignedVecTy != VecTy && N->getOpcode() == ISD::STORE) {
    SDValue &StVal = Ops[Ops.size() - 2];
    StVal = DAG.getNode(ISD::BITCAST, dl, AlignedVecTy, StVal);
  }

  EVT LoadVT = isLaneOp ? VecTy.getVectorElementType() : AlignedVecTy;
  SDValue UpdN = DAG.getMemIntrinsicNode(NewOpc, dl, SDTys, Ops, LoadVT,
                                         MemN->getMemOperand());

  // Update the uses.
  SmallVector<SDValue, 5> NewResults;
  for (unsigned i = 0; i < NumResultVecs; ++i)
    NewResults.push_back(SDValue(UpdN.getNode(), i));

  // If this is an non-standard-aligned LOAD, the first result is the loaded
  // value.  Bitcast it to the expected result type.
  if (AlignedVecTy != VecTy && N->getOpcode() == ISD::LOAD) {
    SDValue &LdVal = NewResults[0];
    LdVal = DAG.getNode(ISD::BITCAST, dl, VecTy, LdVal);
  }

  NewResults.push_back(SDValue(UpdN.getNode(), NumResultVecs + 1)); // chain
  DCI.CombineTo(N, NewResults);
  DCI.CombineTo(User.N, SDValue(UpdN.getNode(), NumResultVecs));

  return true;
}

// If (opcode ptr inc) is and ADD-like instruction, return the
// increment value. Otherwise return 0.
static unsigned getPointerConstIncrement(unsigned Opcode, SDValue Ptr,
                                         SDValue Inc, const SelectionDAG &DAG) {
  ConstantSDNode *CInc = dyn_cast<ConstantSDNode>(Inc.getNode());
  if (!CInc)
    return 0;

  switch (Opcode) {
  case ARMISD::VLD1_UPD:
  case ISD::ADD:
    return CInc->getZExtValue();
  case ISD::OR: {
    if (DAG.haveNoCommonBitsSet(Ptr, Inc)) {
      // (OR ptr inc) is the same as (ADD ptr inc)
      return CInc->getZExtValue();
    }
    return 0;
  }
  default:
    return 0;
  }
}

static bool findPointerConstIncrement(SDNode *N, SDValue *Ptr, SDValue *CInc) {
  switch (N->getOpcode()) {
  case ISD::ADD:
  case ISD::OR: {
    if (isa<ConstantSDNode>(N->getOperand(1))) {
      *Ptr = N->getOperand(0);
      *CInc = N->getOperand(1);
      return true;
    }
    return false;
  }
  case ARMISD::VLD1_UPD: {
    if (isa<ConstantSDNode>(N->getOperand(2))) {
      *Ptr = N->getOperand(1);
      *CInc = N->getOperand(2);
      return true;
    }
    return false;
  }
  default:
    return false;
  }
}

static bool isValidBaseUpdate(SDNode *N, SDNode *User) {
  // Check that the add is independent of the load/store.
  // Otherwise, folding it would create a cycle. Search through Addr
  // as well, since the User may not be a direct user of Addr and
  // only share a base pointer.
  SmallPtrSet<const SDNode *, 32> Visited;
  SmallVector<const SDNode *, 16> Worklist;
  Worklist.push_back(N);
  Worklist.push_back(User);
  if (SDNode::hasPredecessorHelper(N, Visited, Worklist) ||
      SDNode::hasPredecessorHelper(User, Visited, Worklist))
    return false;
  return true;
}

/// CombineBaseUpdate - Target-specific DAG combine function for VLDDUP,
/// NEON load/store intrinsics, and generic vector load/stores, to merge
/// base address updates.
/// For generic load/stores, the memory type is assumed to be a vector.
/// The caller is assumed to have checked legality.
static SDValue CombineBaseUpdate(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI) {
  const bool isIntrinsic = (N->getOpcode() == ISD::INTRINSIC_VOID ||
                            N->getOpcode() == ISD::INTRINSIC_W_CHAIN);
  const bool isStore = N->getOpcode() == ISD::STORE;
  const unsigned AddrOpIdx = ((isIntrinsic || isStore) ? 2 : 1);
  BaseUpdateTarget Target = {N, isIntrinsic, isStore, AddrOpIdx};

  SDValue Addr = N->getOperand(AddrOpIdx);

  SmallVector<BaseUpdateUser, 8> BaseUpdates;

  // Search for a use of the address operand that is an increment.
  for (SDNode::use_iterator UI = Addr.getNode()->use_begin(),
         UE = Addr.getNode()->use_end(); UI != UE; ++UI) {
    SDNode *User = *UI;
    if (UI.getUse().getResNo() != Addr.getResNo() ||
        User->getNumOperands() != 2)
      continue;

    SDValue Inc = User->getOperand(UI.getOperandNo() == 1 ? 0 : 1);
    unsigned ConstInc =
        getPointerConstIncrement(User->getOpcode(), Addr, Inc, DCI.DAG);

    if (ConstInc || User->getOpcode() == ISD::ADD)
      BaseUpdates.push_back({User, Inc, ConstInc});
  }

  // If the address is a constant pointer increment itself, find
  // another constant increment that has the same base operand
  SDValue Base;
  SDValue CInc;
  if (findPointerConstIncrement(Addr.getNode(), &Base, &CInc)) {
    unsigned Offset =
        getPointerConstIncrement(Addr->getOpcode(), Base, CInc, DCI.DAG);
    for (SDNode::use_iterator UI = Base->use_begin(), UE = Base->use_end();
         UI != UE; ++UI) {

      SDNode *User = *UI;
      if (UI.getUse().getResNo() != Base.getResNo() || User == Addr.getNode() ||
          User->getNumOperands() != 2)
        continue;

      SDValue UserInc = User->getOperand(UI.getOperandNo() == 0 ? 1 : 0);
      unsigned UserOffset =
          getPointerConstIncrement(User->getOpcode(), Base, UserInc, DCI.DAG);

      if (!UserOffset || UserOffset <= Offset)
        continue;

      unsigned NewConstInc = UserOffset - Offset;
      SDValue NewInc = DCI.DAG.getConstant(NewConstInc, SDLoc(N), MVT::i32);
      BaseUpdates.push_back({User, NewInc, NewConstInc});
    }
  }

  // Try to fold the load/store with an update that matches memory
  // access size. This should work well for sequential loads.
  //
  // Filter out invalid updates as well.
  unsigned NumValidUpd = BaseUpdates.size();
  for (unsigned I = 0; I < NumValidUpd;) {
    BaseUpdateUser &User = BaseUpdates[I];
    if (!isValidBaseUpdate(N, User.N)) {
      --NumValidUpd;
      std::swap(BaseUpdates[I], BaseUpdates[NumValidUpd]);
      continue;
    }

    if (TryCombineBaseUpdate(Target, User, /*SimpleConstIncOnly=*/true, DCI))
      return SDValue();
    ++I;
  }
  BaseUpdates.resize(NumValidUpd);

  // Try to fold with other users. Non-constant updates are considered
  // first, and constant updates are sorted to not break a sequence of
  // strided accesses (if there is any).
  std::stable_sort(BaseUpdates.begin(), BaseUpdates.end(),
                   [](const BaseUpdateUser &LHS, const BaseUpdateUser &RHS) {
                     return LHS.ConstInc < RHS.ConstInc;
                   });
  for (BaseUpdateUser &User : BaseUpdates) {
    if (TryCombineBaseUpdate(Target, User, /*SimpleConstIncOnly=*/false, DCI))
      return SDValue();
  }
  return SDValue();
}

static SDValue PerformVLDCombine(SDNode *N,
                                 TargetLowering::DAGCombinerInfo &DCI) {
  if (DCI.isBeforeLegalize() || DCI.isCalledByLegalizer())
    return SDValue();

  return CombineBaseUpdate(N, DCI);
}

static SDValue PerformMVEVLDCombine(SDNode *N,
                                    TargetLowering::DAGCombinerInfo &DCI) {
  if (DCI.isBeforeLegalize() || DCI.isCalledByLegalizer())
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDValue Addr = N->getOperand(2);
  MemSDNode *MemN = cast<MemSDNode>(N);
  SDLoc dl(N);

  // For the stores, where there are multiple intrinsics we only actually want
  // to post-inc the last of the them.
  unsigned IntNo = N->getConstantOperandVal(1);
  if (IntNo == Intrinsic::arm_mve_vst2q && N->getConstantOperandVal(5) != 1)
    return SDValue();
  if (IntNo == Intrinsic::arm_mve_vst4q && N->getConstantOperandVal(7) != 3)
    return SDValue();

  // Search for a use of the address operand that is an increment.
  for (SDNode::use_iterator UI = Addr.getNode()->use_begin(),
                            UE = Addr.getNode()->use_end();
       UI != UE; ++UI) {
    SDNode *User = *UI;
    if (User->getOpcode() != ISD::ADD ||
        UI.getUse().getResNo() != Addr.getResNo())
      continue;

    // Check that the add is independent of the load/store.  Otherwise, folding
    // it would create a cycle. We can avoid searching through Addr as it's a
    // predecessor to both.
    SmallPtrSet<const SDNode *, 32> Visited;
    SmallVector<const SDNode *, 16> Worklist;
    Visited.insert(Addr.getNode());
    Worklist.push_back(N);
    Worklist.push_back(User);
    if (SDNode::hasPredecessorHelper(N, Visited, Worklist) ||
        SDNode::hasPredecessorHelper(User, Visited, Worklist))
      continue;

    // Find the new opcode for the updating load/store.
    bool isLoadOp = true;
    unsigned NewOpc = 0;
    unsigned NumVecs = 0;
    switch (IntNo) {
    default:
      llvm_unreachable("unexpected intrinsic for MVE VLDn combine");
    case Intrinsic::arm_mve_vld2q:
      NewOpc = ARMISD::VLD2_UPD;
      NumVecs = 2;
      break;
    case Intrinsic::arm_mve_vld4q:
      NewOpc = ARMISD::VLD4_UPD;
      NumVecs = 4;
      break;
    case Intrinsic::arm_mve_vst2q:
      NewOpc = ARMISD::VST2_UPD;
      NumVecs = 2;
      isLoadOp = false;
      break;
    case Intrinsic::arm_mve_vst4q:
      NewOpc = ARMISD::VST4_UPD;
      NumVecs = 4;
      isLoadOp = false;
      break;
    }

    // Find the size of memory referenced by the load/store.
    EVT VecTy;
    if (isLoadOp) {
      VecTy = N->getValueType(0);
    } else {
      VecTy = N->getOperand(3).getValueType();
    }

    unsigned NumBytes = NumVecs * VecTy.getSizeInBits() / 8;

    // If the increment is a constant, it must match the memory ref size.
    SDValue Inc = User->getOperand(User->getOperand(0) == Addr ? 1 : 0);
    ConstantSDNode *CInc = dyn_cast<ConstantSDNode>(Inc.getNode());
    if (!CInc || CInc->getZExtValue() != NumBytes)
      continue;

    // Create the new updating load/store node.
    // First, create an SDVTList for the new updating node's results.
    EVT Tys[6];
    unsigned NumResultVecs = (isLoadOp ? NumVecs : 0);
    unsigned n;
    for (n = 0; n < NumResultVecs; ++n)
      Tys[n] = VecTy;
    Tys[n++] = MVT::i32;
    Tys[n] = MVT::Other;
    SDVTList SDTys = DAG.getVTList(ArrayRef(Tys, NumResultVecs + 2));

    // Then, gather the new node's operands.
    SmallVector<SDValue, 8> Ops;
    Ops.push_back(N->getOperand(0)); // incoming chain
    Ops.push_back(N->getOperand(2)); // ptr
    Ops.push_back(Inc);

    for (unsigned i = 3; i < N->getNumOperands(); ++i)
      Ops.push_back(N->getOperand(i));

    SDValue UpdN = DAG.getMemIntrinsicNode(NewOpc, dl, SDTys, Ops, VecTy,
                                           MemN->getMemOperand());

    // Update the uses.
    SmallVector<SDValue, 5> NewResults;
    for (unsigned i = 0; i < NumResultVecs; ++i)
      NewResults.push_back(SDValue(UpdN.getNode(), i));

    NewResults.push_back(SDValue(UpdN.getNode(), NumResultVecs + 1)); // chain
    DCI.CombineTo(N, NewResults);
    DCI.CombineTo(User, SDValue(UpdN.getNode(), NumResultVecs));

    break;
  }

  return SDValue();
}

/// CombineVLDDUP - For a VDUPLANE node N, check if its source operand is a
/// vldN-lane (N > 1) intrinsic, and if all the other uses of that intrinsic
/// are also VDUPLANEs.  If so, combine them to a vldN-dup operation and
/// return true.
static bool CombineVLDDUP(SDNode *N, TargetLowering::DAGCombinerInfo &DCI) {
  SelectionDAG &DAG = DCI.DAG;
  EVT VT = N->getValueType(0);
  // vldN-dup instructions only support 64-bit vectors for N > 1.
  if (!VT.is64BitVector())
    return false;

  // Check if the VDUPLANE operand is a vldN-dup intrinsic.
  SDNode *VLD = N->getOperand(0).getNode();
  if (VLD->getOpcode() != ISD::INTRINSIC_W_CHAIN)
    return false;
  unsigned NumVecs = 0;
  unsigned NewOpc = 0;
  unsigned IntNo = VLD->getConstantOperandVal(1);
  if (IntNo == Intrinsic::arm_neon_vld2lane) {
    NumVecs = 2;
    NewOpc = ARMISD::VLD2DUP;
  } else if (IntNo == Intrinsic::arm_neon_vld3lane) {
    NumVecs = 3;
    NewOpc = ARMISD::VLD3DUP;
  } else if (IntNo == Intrinsic::arm_neon_vld4lane) {
    NumVecs = 4;
    NewOpc = ARMISD::VLD4DUP;
  } else {
    return false;
  }

  // First check that all the vldN-lane uses are VDUPLANEs and that the lane
  // numbers match the load.
  unsigned VLDLaneNo = VLD->getConstantOperandVal(NumVecs + 3);
  for (SDNode::use_iterator UI = VLD->use_begin(), UE = VLD->use_end();
       UI != UE; ++UI) {
    // Ignore uses of the chain result.
    if (UI.getUse().getResNo() == NumVecs)
      continue;
    SDNode *User = *UI;
    if (User->getOpcode() != ARMISD::VDUPLANE ||
        VLDLaneNo != User->getConstantOperandVal(1))
      return false;
  }

  // Create the vldN-dup node.
  EVT Tys[5];
  unsigned n;
  for (n = 0; n < NumVecs; ++n)
    Tys[n] = VT;
  Tys[n] = MVT::Other;
  SDVTList SDTys = DAG.getVTList(ArrayRef(Tys, NumVecs + 1));
  SDValue Ops[] = { VLD->getOperand(0), VLD->getOperand(2) };
  MemIntrinsicSDNode *VLDMemInt = cast<MemIntrinsicSDNode>(VLD);
  SDValue VLDDup = DAG.getMemIntrinsicNode(NewOpc, SDLoc(VLD), SDTys,
                                           Ops, VLDMemInt->getMemoryVT(),
                                           VLDMemInt->getMemOperand());

  // Update the uses.
  for (SDNode::use_iterator UI = VLD->use_begin(), UE = VLD->use_end();
       UI != UE; ++UI) {
    unsigned ResNo = UI.getUse().getResNo();
    // Ignore uses of the chain result.
    if (ResNo == NumVecs)
      continue;
    SDNode *User = *UI;
    DCI.CombineTo(User, SDValue(VLDDup.getNode(), ResNo));
  }

  // Now the vldN-lane intrinsic is dead except for its chain result.
  // Update uses of the chain.
  std::vector<SDValue> VLDDupResults;
  for (unsigned n = 0; n < NumVecs; ++n)
    VLDDupResults.push_back(SDValue(VLDDup.getNode(), n));
  VLDDupResults.push_back(SDValue(VLDDup.getNode(), NumVecs));
  DCI.CombineTo(VLD, VLDDupResults);

  return true;
}

/// PerformVDUPLANECombine - Target-specific dag combine xforms for
/// ARMISD::VDUPLANE.
static SDValue PerformVDUPLANECombine(SDNode *N,
                                      TargetLowering::DAGCombinerInfo &DCI,
                                      const ARMSubtarget *Subtarget) {
  SDValue Op = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // On MVE, we just convert the VDUPLANE to a VDUP with an extract.
  if (Subtarget->hasMVEIntegerOps()) {
    EVT ExtractVT = VT.getVectorElementType();
    // We need to ensure we are creating a legal type.
    if (!DCI.DAG.getTargetLoweringInfo().isTypeLegal(ExtractVT))
      ExtractVT = MVT::i32;
    SDValue Extract = DCI.DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SDLoc(N), ExtractVT,
                              N->getOperand(0), N->getOperand(1));
    return DCI.DAG.getNode(ARMISD::VDUP, SDLoc(N), VT, Extract);
  }

  // If the source is a vldN-lane (N > 1) intrinsic, and all the other uses
  // of that intrinsic are also VDUPLANEs, combine them to a vldN-dup operation.
  if (CombineVLDDUP(N, DCI))
    return SDValue(N, 0);

  // If the source is already a VMOVIMM or VMVNIMM splat, the VDUPLANE is
  // redundant.  Ignore bit_converts for now; element sizes are checked below.
  while (Op.getOpcode() == ISD::BITCAST)
    Op = Op.getOperand(0);
  if (Op.getOpcode() != ARMISD::VMOVIMM && Op.getOpcode() != ARMISD::VMVNIMM)
    return SDValue();

  // Make sure the VMOV element size is not bigger than the VDUPLANE elements.
  unsigned EltSize = Op.getScalarValueSizeInBits();
  // The canonical VMOV for a zero vector uses a 32-bit element size.
  unsigned Imm = Op.getConstantOperandVal(0);
  unsigned EltBits;
  if (ARM_AM::decodeVMOVModImm(Imm, EltBits) == 0)
    EltSize = 8;
  if (EltSize > VT.getScalarSizeInBits())
    return SDValue();

  return DCI.DAG.getNode(ISD::BITCAST, SDLoc(N), VT, Op);
}

/// PerformVDUPCombine - Target-specific dag combine xforms for ARMISD::VDUP.
static SDValue PerformVDUPCombine(SDNode *N, SelectionDAG &DAG,
                                  const ARMSubtarget *Subtarget) {
  SDValue Op = N->getOperand(0);
  SDLoc dl(N);

  if (Subtarget->hasMVEIntegerOps()) {
    // Convert VDUP f32 -> VDUP BITCAST i32 under MVE, as we know the value will
    // need to come from a GPR.
    if (Op.getValueType() == MVT::f32)
      return DAG.getNode(ARMISD::VDUP, dl, N->getValueType(0),
                         DAG.getNode(ISD::BITCAST, dl, MVT::i32, Op));
    else if (Op.getValueType() == MVT::f16)
      return DAG.getNode(ARMISD::VDUP, dl, N->getValueType(0),
                         DAG.getNode(ARMISD::VMOVrh, dl, MVT::i32, Op));
  }

  if (!Subtarget->hasNEON())
    return SDValue();

  // Match VDUP(LOAD) -> VLD1DUP.
  // We match this pattern here rather than waiting for isel because the
  // transform is only legal for unindexed loads.
  LoadSDNode *LD = dyn_cast<LoadSDNode>(Op.getNode());
  if (LD && Op.hasOneUse() && LD->isUnindexed() &&
      LD->getMemoryVT() == N->getValueType(0).getVectorElementType()) {
    SDValue Ops[] = {LD->getOperand(0), LD->getOperand(1),
                     DAG.getConstant(LD->getAlign().value(), SDLoc(N), MVT::i32)};
    SDVTList SDTys = DAG.getVTList(N->getValueType(0), MVT::Other);
    SDValue VLDDup =
        DAG.getMemIntrinsicNode(ARMISD::VLD1DUP, SDLoc(N), SDTys, Ops,
                                LD->getMemoryVT(), LD->getMemOperand());
    DAG.ReplaceAllUsesOfValueWith(SDValue(LD, 1), VLDDup.getValue(1));
    return VLDDup;
  }

  return SDValue();
}

static SDValue PerformLOADCombine(SDNode *N,
                                  TargetLowering::DAGCombinerInfo &DCI,
                                  const ARMSubtarget *Subtarget) {
  EVT VT = N->getValueType(0);

  // If this is a legal vector load, try to combine it into a VLD1_UPD.
  if (Subtarget->hasNEON() && ISD::isNormalLoad(N) && VT.isVector() &&
      DCI.DAG.getTargetLoweringInfo().isTypeLegal(VT))
    return CombineBaseUpdate(N, DCI);

  return SDValue();
}

// Optimize trunc store (of multiple scalars) to shuffle and store.  First,
// pack all of the elements in one place.  Next, store to memory in fewer
// chunks.
static SDValue PerformTruncatingStoreCombine(StoreSDNode *St,
                                             SelectionDAG &DAG) {
  SDValue StVal = St->getValue();
  EVT VT = StVal.getValueType();
  if (!St->isTruncatingStore() || !VT.isVector())
    return SDValue();
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  EVT StVT = St->getMemoryVT();
  unsigned NumElems = VT.getVectorNumElements();
  assert(StVT != VT && "Cannot truncate to the same type");
  unsigned FromEltSz = VT.getScalarSizeInBits();
  unsigned ToEltSz = StVT.getScalarSizeInBits();

  // From, To sizes and ElemCount must be pow of two
  if (!isPowerOf2_32(NumElems * FromEltSz * ToEltSz))
    return SDValue();

  // We are going to use the original vector elt for storing.
  // Accumulated smaller vector elements must be a multiple of the store size.
  if (0 != (NumElems * FromEltSz) % ToEltSz)
    return SDValue();

  unsigned SizeRatio = FromEltSz / ToEltSz;
  assert(SizeRatio * NumElems * ToEltSz == VT.getSizeInBits());

  // Create a type on which we perform the shuffle.
  EVT WideVecVT = EVT::getVectorVT(*DAG.getContext(), StVT.getScalarType(),
                                   NumElems * SizeRatio);
  assert(WideVecVT.getSizeInBits() == VT.getSizeInBits());

  SDLoc DL(St);
  SDValue WideVec = DAG.getNode(ISD::BITCAST, DL, WideVecVT, StVal);
  SmallVector<int, 8> ShuffleVec(NumElems * SizeRatio, -1);
  for (unsigned i = 0; i < NumElems; ++i)
    ShuffleVec[i] = DAG.getDataLayout().isBigEndian() ? (i + 1) * SizeRatio - 1
                                                      : i * SizeRatio;

  // Can't shuffle using an illegal type.
  if (!TLI.isTypeLegal(WideVecVT))
    return SDValue();

  SDValue Shuff = DAG.getVectorShuffle(
      WideVecVT, DL, WideVec, DAG.getUNDEF(WideVec.getValueType()), ShuffleVec);
  // At this point all of the data is stored at the bottom of the
  // register. We now need to save it to mem.

  // Find the largest store unit
  MVT StoreType = MVT::i8;
  for (MVT Tp : MVT::integer_valuetypes()) {
    if (TLI.isTypeLegal(Tp) && Tp.getSizeInBits() <= NumElems * ToEltSz)
      StoreType = Tp;
  }
  // Didn't find a legal store type.
  if (!TLI.isTypeLegal(StoreType))
    return SDValue();

  // Bitcast the original vector into a vector of store-size units
  EVT StoreVecVT =
      EVT::getVectorVT(*DAG.getContext(), StoreType,
                       VT.getSizeInBits() / EVT(StoreType).getSizeInBits());
  assert(StoreVecVT.getSizeInBits() == VT.getSizeInBits());
  SDValue ShuffWide = DAG.getNode(ISD::BITCAST, DL, StoreVecVT, Shuff);
  SmallVector<SDValue, 8> Chains;
  SDValue Increment = DAG.getConstant(StoreType.getSizeInBits() / 8, DL,
                                      TLI.getPointerTy(DAG.getDataLayout()));
  SDValue BasePtr = St->getBasePtr();

  // Perform one or more big stores into memory.
  unsigned E = (ToEltSz * NumElems) / StoreType.getSizeInBits();
  for (unsigned I = 0; I < E; I++) {
    SDValue SubVec = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, StoreType,
                                 ShuffWide, DAG.getIntPtrConstant(I, DL));
    SDValue Ch =
        DAG.getStore(St->getChain(), DL, SubVec, BasePtr, St->getPointerInfo(),
                     St->getAlign(), St->getMemOperand()->getFlags());
    BasePtr =
        DAG.getNode(ISD::ADD, DL, BasePtr.getValueType(), BasePtr, Increment);
    Chains.push_back(Ch);
  }
  return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Chains);
}

// Try taking a single vector store from an fpround (which would otherwise turn
// into an expensive buildvector) and splitting it into a series of narrowing
// stores.
static SDValue PerformSplittingToNarrowingStores(StoreSDNode *St,
                                                 SelectionDAG &DAG) {
  if (!St->isSimple() || St->isTruncatingStore() || !St->isUnindexed())
    return SDValue();
  SDValue Trunc = St->getValue();
  if (Trunc->getOpcode() != ISD::FP_ROUND)
    return SDValue();
  EVT FromVT = Trunc->getOperand(0).getValueType();
  EVT ToVT = Trunc.getValueType();
  if (!ToVT.isVector())
    return SDValue();
  assert(FromVT.getVectorNumElements() == ToVT.getVectorNumElements());
  EVT ToEltVT = ToVT.getVectorElementType();
  EVT FromEltVT = FromVT.getVectorElementType();

  if (FromEltVT != MVT::f32 || ToEltVT != MVT::f16)
    return SDValue();

  unsigned NumElements = 4;
  if (FromVT.getVectorNumElements() % NumElements != 0)
    return SDValue();

  // Test if the Trunc will be convertable to a VMOVN with a shuffle, and if so
  // use the VMOVN over splitting the store. We are looking for patterns of:
  // !rev: 0 N 1 N+1 2 N+2 ...
  //  rev: N 0 N+1 1 N+2 2 ...
  // The shuffle may either be a single source (in which case N = NumElts/2) or
  // two inputs extended with concat to the same size (in which case N =
  // NumElts).
  auto isVMOVNShuffle = [&](ShuffleVectorSDNode *SVN, bool Rev) {
    ArrayRef<int> M = SVN->getMask();
    unsigned NumElts = ToVT.getVectorNumElements();
    if (SVN->getOperand(1).isUndef())
      NumElts /= 2;

    unsigned Off0 = Rev ? NumElts : 0;
    unsigned Off1 = Rev ? 0 : NumElts;

    for (unsigned I = 0; I < NumElts; I += 2) {
      if (M[I] >= 0 && M[I] != (int)(Off0 + I / 2))
        return false;
      if (M[I + 1] >= 0 && M[I + 1] != (int)(Off1 + I / 2))
        return false;
    }

    return true;
  };

  if (auto *Shuffle = dyn_cast<ShuffleVectorSDNode>(Trunc.getOperand(0)))
    if (isVMOVNShuffle(Shuffle, false) || isVMOVNShuffle(Shuffle, true))
      return SDValue();

  LLVMContext &C = *DAG.getContext();
  SDLoc DL(St);
  // Details about the old store
  SDValue Ch = St->getChain();
  SDValue BasePtr = St->getBasePtr();
  Align Alignment = St->getOriginalAlign();
  MachineMemOperand::Flags MMOFlags = St->getMemOperand()->getFlags();
  AAMDNodes AAInfo = St->getAAInfo();

  // We split the store into slices of NumElements. fp16 trunc stores are vcvt
  // and then stored as truncating integer stores.
  EVT NewFromVT = EVT::getVectorVT(C, FromEltVT, NumElements);
  EVT NewToVT = EVT::getVectorVT(
      C, EVT::getIntegerVT(C, ToEltVT.getSizeInBits()), NumElements);

  SmallVector<SDValue, 4> Stores;
  for (unsigned i = 0; i < FromVT.getVectorNumElements() / NumElements; i++) {
    unsigned NewOffset = i * NumElements * ToEltVT.getSizeInBits() / 8;
    SDValue NewPtr =
        DAG.getObjectPtrOffset(DL, BasePtr, TypeSize::getFixed(NewOffset));

    SDValue Extract =
        DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, NewFromVT, Trunc.getOperand(0),
                    DAG.getConstant(i * NumElements, DL, MVT::i32));

    SDValue FPTrunc =
        DAG.getNode(ARMISD::VCVTN, DL, MVT::v8f16, DAG.getUNDEF(MVT::v8f16),
                    Extract, DAG.getConstant(0, DL, MVT::i32));
    Extract = DAG.getNode(ARMISD::VECTOR_REG_CAST, DL, MVT::v4i32, FPTrunc);

    SDValue Store = DAG.getTruncStore(
        Ch, DL, Extract, NewPtr, St->getPointerInfo().getWithOffset(NewOffset),
        NewToVT, Alignment, MMOFlags, AAInfo);
    Stores.push_back(Store);
  }
  return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Stores);
}

// Try taking a single vector store from an MVETRUNC (which would otherwise turn
// into an expensive buildvector) and splitting it into a series of narrowing
// stores.
static SDValue PerformSplittingMVETruncToNarrowingStores(StoreSDNode *St,
                                                         SelectionDAG &DAG) {
  if (!St->isSimple() || St->isTruncatingStore() || !St->isUnindexed())
    return SDValue();
  SDValue Trunc = St->getValue();
  if (Trunc->getOpcode() != ARMISD::MVETRUNC)
    return SDValue();
  EVT FromVT = Trunc->getOperand(0).getValueType();
  EVT ToVT = Trunc.getValueType();

  LLVMContext &C = *DAG.getContext();
  SDLoc DL(St);
  // Details about the old store
  SDValue Ch = St->getChain();
  SDValue BasePtr = St->getBasePtr();
  Align Alignment = St->getOriginalAlign();
  MachineMemOperand::Flags MMOFlags = St->getMemOperand()->getFlags();
  AAMDNodes AAInfo = St->getAAInfo();

  EVT NewToVT = EVT::getVectorVT(C, ToVT.getVectorElementType(),
                                 FromVT.getVectorNumElements());

  SmallVector<SDValue, 4> Stores;
  for (unsigned i = 0; i < Trunc.getNumOperands(); i++) {
    unsigned NewOffset =
        i * FromVT.getVectorNumElements() * ToVT.getScalarSizeInBits() / 8;
    SDValue NewPtr =
        DAG.getObjectPtrOffset(DL, BasePtr, TypeSize::getFixed(NewOffset));

    SDValue Extract = Trunc.getOperand(i);
    SDValue Store = DAG.getTruncStore(
        Ch, DL, Extract, NewPtr, St->getPointerInfo().getWithOffset(NewOffset),
        NewToVT, Alignment, MMOFlags, AAInfo);
    Stores.push_back(Store);
  }
  return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Stores);
}

// Given a floating point store from an extracted vector, with an integer
// VGETLANE that already exists, store the existing VGETLANEu directly. This can
// help reduce fp register pressure, doesn't require the fp extract and allows
// use of more integer post-inc stores not available with vstr.
static SDValue PerformExtractFpToIntStores(StoreSDNode *St, SelectionDAG &DAG) {
  if (!St->isSimple() || St->isTruncatingStore() || !St->isUnindexed())
    return SDValue();
  SDValue Extract = St->getValue();
  EVT VT = Extract.getValueType();
  // For now only uses f16. This may be useful for f32 too, but that will
  // be bitcast(extract), not the VGETLANEu we currently check here.
  if (VT != MVT::f16 || Extract->getOpcode() != ISD::EXTRACT_VECTOR_ELT)
    return SDValue();

  SDNode *GetLane =
      DAG.getNodeIfExists(ARMISD::VGETLANEu, DAG.getVTList(MVT::i32),
                          {Extract.getOperand(0), Extract.getOperand(1)});
  if (!GetLane)
    return SDValue();

  LLVMContext &C = *DAG.getContext();
  SDLoc DL(St);
  // Create a new integer store to replace the existing floating point version.
  SDValue Ch = St->getChain();
  SDValue BasePtr = St->getBasePtr();
  Align Alignment = St->getOriginalAlign();
  MachineMemOperand::Flags MMOFlags = St->getMemOperand()->getFlags();
  AAMDNodes AAInfo = St->getAAInfo();
  EVT NewToVT = EVT::getIntegerVT(C, VT.getSizeInBits());
  SDValue Store = DAG.getTruncStore(Ch, DL, SDValue(GetLane, 0), BasePtr,
                                    St->getPointerInfo(), NewToVT, Alignment,
                                    MMOFlags, AAInfo);

  return Store;
}

/// PerformSTORECombine - Target-specific dag combine xforms for
/// ISD::STORE.
static SDValue PerformSTORECombine(SDNode *N,
                                   TargetLowering::DAGCombinerInfo &DCI,
                                   const ARMSubtarget *Subtarget) {
  StoreSDNode *St = cast<StoreSDNode>(N);
  if (St->isVolatile())
    return SDValue();
  SDValue StVal = St->getValue();
  EVT VT = StVal.getValueType();

  if (Subtarget->hasNEON())
    if (SDValue Store = PerformTruncatingStoreCombine(St, DCI.DAG))
      return Store;

  if (Subtarget->hasMVEFloatOps())
    if (SDValue NewToken = PerformSplittingToNarrowingStores(St, DCI.DAG))
      return NewToken;

  if (Subtarget->hasMVEIntegerOps()) {
    if (SDValue NewChain = PerformExtractFpToIntStores(St, DCI.DAG))
      return NewChain;
    if (SDValue NewToken =
            PerformSplittingMVETruncToNarrowingStores(St, DCI.DAG))
      return NewToken;
  }

  if (!ISD::isNormalStore(St))
    return SDValue();

  // Split a store of a VMOVDRR into two integer stores to avoid mixing NEON and
  // ARM stores of arguments in the same cache line.
  if (StVal.getNode()->getOpcode() == ARMISD::VMOVDRR &&
      StVal.getNode()->hasOneUse()) {
    SelectionDAG  &DAG = DCI.DAG;
    bool isBigEndian = DAG.getDataLayout().isBigEndian();
    SDLoc DL(St);
    SDValue BasePtr = St->getBasePtr();
    SDValue NewST1 = DAG.getStore(
        St->getChain(), DL, StVal.getNode()->getOperand(isBigEndian ? 1 : 0),
        BasePtr, St->getPointerInfo(), St->getOriginalAlign(),
        St->getMemOperand()->getFlags());

    SDValue OffsetPtr = DAG.getNode(ISD::ADD, DL, MVT::i32, BasePtr,
                                    DAG.getConstant(4, DL, MVT::i32));
    return DAG.getStore(NewST1.getValue(0), DL,
                        StVal.getNode()->getOperand(isBigEndian ? 0 : 1),
                        OffsetPtr, St->getPointerInfo().getWithOffset(4),
                        St->getOriginalAlign(),
                        St->getMemOperand()->getFlags());
  }

  if (StVal.getValueType() == MVT::i64 &&
      StVal.getNode()->getOpcode() == ISD::EXTRACT_VECTOR_ELT) {

    // Bitcast an i64 store extracted from a vector to f64.
    // Otherwise, the i64 value will be legalized to a pair of i32 values.
    SelectionDAG &DAG = DCI.DAG;
    SDLoc dl(StVal);
    SDValue IntVec = StVal.getOperand(0);
    EVT FloatVT = EVT::getVectorVT(*DAG.getContext(), MVT::f64,
                                   IntVec.getValueType().getVectorNumElements());
    SDValue Vec = DAG.getNode(ISD::BITCAST, dl, FloatVT, IntVec);
    SDValue ExtElt = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, dl, MVT::f64,
                                 Vec, StVal.getOperand(1));
    dl = SDLoc(N);
    SDValue V = DAG.getNode(ISD::BITCAST, dl, MVT::i64, ExtElt);
    // Make the DAGCombiner fold the bitcasts.
    DCI.AddToWorklist(Vec.getNode());
    DCI.AddToWorklist(ExtElt.getNode());
    DCI.AddToWorklist(V.getNode());
    return DAG.getStore(St->getChain(), dl, V, St->getBasePtr(),
                        St->getPointerInfo(), St->getAlign(),
                        St->getMemOperand()->getFlags(), St->getAAInfo());
  }

  // If this is a legal vector store, try to combine it into a VST1_UPD.
  if (Subtarget->hasNEON() && ISD::isNormalStore(N) && VT.isVector() &&
      DCI.DAG.getTargetLoweringInfo().isTypeLegal(VT))
    return CombineBaseUpdate(N, DCI);

  return SDValue();
}

/// PerformVCVTCombine - VCVT (floating-point to fixed-point, Advanced SIMD)
/// can replace combinations of VMUL and VCVT (floating-point to integer)
/// when the VMUL has a constant operand that is a power of 2.
///
/// Example (assume d17 = <float 8.000000e+00, float 8.000000e+00>):
///  vmul.f32        d16, d17, d16
///  vcvt.s32.f32    d16, d16
/// becomes:
///  vcvt.s32.f32    d16, d16, #3
static SDValue PerformVCVTCombine(SDNode *N, SelectionDAG &DAG,
                                  const ARMSubtarget *Subtarget) {
  if (!Subtarget->hasNEON())
    return SDValue();

  SDValue Op = N->getOperand(0);
  if (!Op.getValueType().isVector() || !Op.getValueType().isSimple() ||
      Op.getOpcode() != ISD::FMUL)
    return SDValue();

  SDValue ConstVec = Op->getOperand(1);
  if (!isa<BuildVectorSDNode>(ConstVec))
    return SDValue();

  MVT FloatTy = Op.getSimpleValueType().getVectorElementType();
  uint32_t FloatBits = FloatTy.getSizeInBits();
  MVT IntTy = N->getSimpleValueType(0).getVectorElementType();
  uint32_t IntBits = IntTy.getSizeInBits();
  unsigned NumLanes = Op.getValueType().getVectorNumElements();
  if (FloatBits != 32 || IntBits > 32 || (NumLanes != 4 && NumLanes != 2)) {
    // These instructions only exist converting from f32 to i32. We can handle
    // smaller integers by generating an extra truncate, but larger ones would
    // be lossy. We also can't handle anything other than 2 or 4 lanes, since
    // these intructions only support v2i32/v4i32 types.
    return SDValue();
  }

  BitVector UndefElements;
  BuildVectorSDNode *BV = cast<BuildVectorSDNode>(ConstVec);
  int32_t C = BV->getConstantFPSplatPow2ToLog2Int(&UndefElements, 33);
  if (C == -1 || C == 0 || C > 32)
    return SDValue();

  SDLoc dl(N);
  bool isSigned = N->getOpcode() == ISD::FP_TO_SINT;
  unsigned IntrinsicOpcode = isSigned ? Intrinsic::arm_neon_vcvtfp2fxs :
    Intrinsic::arm_neon_vcvtfp2fxu;
  SDValue FixConv = DAG.getNode(
      ISD::INTRINSIC_WO_CHAIN, dl, NumLanes == 2 ? MVT::v2i32 : MVT::v4i32,
      DAG.getConstant(IntrinsicOpcode, dl, MVT::i32), Op->getOperand(0),
      DAG.getConstant(C, dl, MVT::i32));

  if (IntBits < FloatBits)
    FixConv = DAG.getNode(ISD::TRUNCATE, dl, N->getValueType(0), FixConv);

  return FixConv;
}

static SDValue PerformFAddVSelectCombine(SDNode *N, SelectionDAG &DAG,
                                         const ARMSubtarget *Subtarget) {
  if (!Subtarget->hasMVEFloatOps())
    return SDValue();

  // Turn (fadd x, (vselect c, y, -0.0)) into (vselect c, (fadd x, y), x)
  // The second form can be more easily turned into a predicated vadd, and
  // possibly combined into a fma to become a predicated vfma.
  SDValue Op0 = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);
  EVT VT = N->getValueType(0);
  SDLoc DL(N);

  // The identity element for a fadd is -0.0 or +0.0 when the nsz flag is set,
  // which these VMOV's represent.
  auto isIdentitySplat = [&](SDValue Op, bool NSZ) {
    if (Op.getOpcode() != ISD::BITCAST ||
        Op.getOperand(0).getOpcode() != ARMISD::VMOVIMM)
      return false;
    uint64_t ImmVal = Op.getOperand(0).getConstantOperandVal(0);
    if (VT == MVT::v4f32 && (ImmVal == 1664 || (ImmVal == 0 && NSZ)))
      return true;
    if (VT == MVT::v8f16 && (ImmVal == 2688 || (ImmVal == 0 && NSZ)))
      return true;
    return false;
  };

  if (Op0.getOpcode() == ISD::VSELECT && Op1.getOpcode() != ISD::VSELECT)
    std::swap(Op0, Op1);

  if (Op1.getOpcode() != ISD::VSELECT)
    return SDValue();

  SDNodeFlags FaddFlags = N->getFlags();
  bool NSZ = FaddFlags.hasNoSignedZeros();
  if (!isIdentitySplat(Op1.getOperand(2), NSZ))
    return SDValue();

  SDValue FAdd =
      DAG.getNode(ISD::FADD, DL, VT, Op0, Op1.getOperand(1), FaddFlags);
  return DAG.getNode(ISD::VSELECT, DL, VT, Op1.getOperand(0), FAdd, Op0, FaddFlags);
}

static SDValue PerformFADDVCMLACombine(SDNode *N, SelectionDAG &DAG) {
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  EVT VT = N->getValueType(0);
  SDLoc DL(N);

  if (!N->getFlags().hasAllowReassociation())
    return SDValue();

  // Combine fadd(a, vcmla(b, c, d)) -> vcmla(fadd(a, b), b, c)
  auto ReassocComplex = [&](SDValue A, SDValue B) {
    if (A.getOpcode() != ISD::INTRINSIC_WO_CHAIN)
      return SDValue();
    unsigned Opc = A.getConstantOperandVal(0);
    if (Opc != Intrinsic::arm_mve_vcmlaq)
      return SDValue();
    SDValue VCMLA = DAG.getNode(
        ISD::INTRINSIC_WO_CHAIN, DL, VT, A.getOperand(0), A.getOperand(1),
        DAG.getNode(ISD::FADD, DL, VT, A.getOperand(2), B, N->getFlags()),
        A.getOperand(3), A.getOperand(4));
    VCMLA->setFlags(A->getFlags());
    return VCMLA;
  };
  if (SDValue R = ReassocComplex(LHS, RHS))
    return R;
  if (SDValue R = ReassocComplex(RHS, LHS))
    return R;

  return SDValue();
}

static SDValue PerformFADDCombine(SDNode *N, SelectionDAG &DAG,
                                  const ARMSubtarget *Subtarget) {
  if (SDValue S = PerformFAddVSelectCombine(N, DAG, Subtarget))
    return S;
  if (SDValue S = PerformFADDVCMLACombine(N, DAG))
    return S;
  return SDValue();
}

/// PerformVMulVCTPCombine - VCVT (fixed-point to floating-point, Advanced SIMD)
/// can replace combinations of VCVT (integer to floating-point) and VMUL
/// when the VMUL has a constant operand that is a power of 2.
///
/// Example (assume d17 = <float 0.125, float 0.125>):
///  vcvt.f32.s32    d16, d16
///  vmul.f32        d16, d16, d17
/// becomes:
///  vcvt.f32.s32    d16, d16, #3
static SDValue PerformVMulVCTPCombine(SDNode *N, SelectionDAG &DAG,
                                      const ARMSubtarget *Subtarget) {
  if (!Subtarget->hasNEON())
    return SDValue();

  SDValue Op = N->getOperand(0);
  unsigned OpOpcode = Op.getNode()->getOpcode();
  if (!N->getValueType(0).isVector() || !N->getValueType(0).isSimple() ||
      (OpOpcode != ISD::SINT_TO_FP && OpOpcode != ISD::UINT_TO_FP))
    return SDValue();

  SDValue ConstVec = N->getOperand(1);
  if (!isa<BuildVectorSDNode>(ConstVec))
    return SDValue();

  MVT FloatTy = N->getSimpleValueType(0).getVectorElementType();
  uint32_t FloatBits = FloatTy.getSizeInBits();
  MVT IntTy = Op.getOperand(0).getSimpleValueType().getVectorElementType();
  uint32_t IntBits = IntTy.getSizeInBits();
  unsigned NumLanes = Op.getValueType().getVectorNumElements();
  if (FloatBits != 32 || IntBits > 32 || (NumLanes != 4 && NumLanes != 2)) {
    // These instructions only exist converting from i32 to f32. We can handle
    // smaller integers by generating an extra extend, but larger ones would
    // be lossy. We also can't handle anything other than 2 or 4 lanes, since
    // these intructions only support v2i32/v4i32 types.
    return SDValue();
  }

  ConstantFPSDNode *CN = isConstOrConstSplatFP(ConstVec, true);
  APFloat Recip(0.0f);
  if (!CN || !CN->getValueAPF().getExactInverse(&Recip))
    return SDValue();

  bool IsExact;
  APSInt IntVal(33);
  if (Recip.convertToInteger(IntVal, APFloat::rmTowardZero, &IsExact) !=
          APFloat::opOK ||
      !IsExact)
    return SDValue();

  int32_t C = IntVal.exactLogBase2();
  if (C == -1 || C == 0 || C > 32)
    return SDValue();

  SDLoc DL(N);
  bool isSigned = OpOpcode == ISD::SINT_TO_FP;
  SDValue ConvInput = Op.getOperand(0);
  if (IntBits < FloatBits)
    ConvInput = DAG.getNode(isSigned ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND, DL,
                            NumLanes == 2 ? MVT::v2i32 : MVT::v4i32, ConvInput);

  unsigned IntrinsicOpcode = isSigned ? Intrinsic::arm_neon_vcvtfxs2fp
                                      : Intrinsic::arm_neon_vcvtfxu2fp;
  return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, DL, Op.getValueType(),
                     DAG.getConstant(IntrinsicOpcode, DL, MVT::i32), ConvInput,
                     DAG.getConstant(C, DL, MVT::i32));
}

static SDValue PerformVECREDUCE_ADDCombine(SDNode *N, SelectionDAG &DAG,
                                           const ARMSubtarget *ST) {
  if (!ST->hasMVEIntegerOps())
    return SDValue();

  assert(N->getOpcode() == ISD::VECREDUCE_ADD);
  EVT ResVT = N->getValueType(0);
  SDValue N0 = N->getOperand(0);
  SDLoc dl(N);

  // Try to turn vecreduce_add(add(x, y)) into vecreduce(x) + vecreduce(y)
  if (ResVT == MVT::i32 && N0.getOpcode() == ISD::ADD &&
      (N0.getValueType() == MVT::v4i32 || N0.getValueType() == MVT::v8i16 ||
       N0.getValueType() == MVT::v16i8)) {
    SDValue Red0 = DAG.getNode(ISD::VECREDUCE_ADD, dl, ResVT, N0.getOperand(0));
    SDValue Red1 = DAG.getNode(ISD::VECREDUCE_ADD, dl, ResVT, N0.getOperand(1));
    return DAG.getNode(ISD::ADD, dl, ResVT, Red0, Red1);
  }

  // We are looking for something that will have illegal types if left alone,
  // but that we can convert to a single instruction under MVE. For example
  // vecreduce_add(sext(A, v8i32)) => VADDV.s16 A
  // or
  // vecreduce_add(mul(zext(A, v16i32), zext(B, v16i32))) => VMLADAV.u8 A, B

  // The legal cases are:
  //   VADDV u/s 8/16/32
  //   VMLAV u/s 8/16/32
  //   VADDLV u/s 32
  //   VMLALV u/s 16/32

  // If the input vector is smaller than legal (v4i8/v4i16 for example) we can
  // extend it and use v4i32 instead.
  auto ExtTypeMatches = [](SDValue A, ArrayRef<MVT> ExtTypes) {
    EVT AVT = A.getValueType();
    return any_of(ExtTypes, [&](MVT Ty) {
      return AVT.getVectorNumElements() == Ty.getVectorNumElements() &&
             AVT.bitsLE(Ty);
    });
  };
  auto ExtendIfNeeded = [&](SDValue A, unsigned ExtendCode) {
    EVT AVT = A.getValueType();
    if (!AVT.is128BitVector())
      A = DAG.getNode(ExtendCode, dl,
                      AVT.changeVectorElementType(MVT::getIntegerVT(
                          128 / AVT.getVectorMinNumElements())),
                      A);
    return A;
  };
  auto IsVADDV = [&](MVT RetTy, unsigned ExtendCode, ArrayRef<MVT> ExtTypes) {
    if (ResVT != RetTy || N0->getOpcode() != ExtendCode)
      return SDValue();
    SDValue A = N0->getOperand(0);
    if (ExtTypeMatches(A, ExtTypes))
      return ExtendIfNeeded(A, ExtendCode);
    return SDValue();
  };
  auto IsPredVADDV = [&](MVT RetTy, unsigned ExtendCode,
                         ArrayRef<MVT> ExtTypes, SDValue &Mask) {
    if (ResVT != RetTy || N0->getOpcode() != ISD::VSELECT ||
        !ISD::isBuildVectorAllZeros(N0->getOperand(2).getNode()))
      return SDValue();
    Mask = N0->getOperand(0);
    SDValue Ext = N0->getOperand(1);
    if (Ext->getOpcode() != ExtendCode)
      return SDValue();
    SDValue A = Ext->getOperand(0);
    if (ExtTypeMatches(A, ExtTypes))
      return ExtendIfNeeded(A, ExtendCode);
    return SDValue();
  };
  auto IsVMLAV = [&](MVT RetTy, unsigned ExtendCode, ArrayRef<MVT> ExtTypes,
                     SDValue &A, SDValue &B) {
    // For a vmla we are trying to match a larger pattern:
    // ExtA = sext/zext A
    // ExtB = sext/zext B
    // Mul = mul ExtA, ExtB
    // vecreduce.add Mul
    // There might also be en extra extend between the mul and the addreduce, so
    // long as the bitwidth is high enough to make them equivalent (for example
    // original v8i16 might be mul at v8i32 and the reduce happens at v8i64).
    if (ResVT != RetTy)
      return false;
    SDValue Mul = N0;
    if (Mul->getOpcode() == ExtendCode &&
        Mul->getOperand(0).getScalarValueSizeInBits() * 2 >=
            ResVT.getScalarSizeInBits())
      Mul = Mul->getOperand(0);
    if (Mul->getOpcode() != ISD::MUL)
      return false;
    SDValue ExtA = Mul->getOperand(0);
    SDValue ExtB = Mul->getOperand(1);
    if (ExtA->getOpcode() != ExtendCode || ExtB->getOpcode() != ExtendCode)
      return false;
    A = ExtA->getOperand(0);
    B = ExtB->getOperand(0);
    if (ExtTypeMatches(A, ExtTypes) && ExtTypeMatches(B, ExtTypes)) {
      A = ExtendIfNeeded(A, ExtendCode);
      B = ExtendIfNeeded(B, ExtendCode);
      return true;
    }
    return false;
  };
  auto IsPredVMLAV = [&](MVT RetTy, unsigned ExtendCode, ArrayRef<MVT> ExtTypes,
                     SDValue &A, SDValue &B, SDValue &Mask) {
    // Same as the pattern above with a select for the zero predicated lanes
    // ExtA = sext/zext A
    // ExtB = sext/zext B
    // Mul = mul ExtA, ExtB
    // N0 = select Mask, Mul, 0
    // vecreduce.add N0
    if (ResVT != RetTy || N0->getOpcode() != ISD::VSELECT ||
        !ISD::isBuildVectorAllZeros(N0->getOperand(2).getNode()))
      return false;
    Mask = N0->getOperand(0);
    SDValue Mul = N0->getOperand(1);
    if (Mul->getOpcode() == ExtendCode &&
        Mul->getOperand(0).getScalarValueSizeInBits() * 2 >=
            ResVT.getScalarSizeInBits())
      Mul = Mul->getOperand(0);
    if (Mul->getOpcode() != ISD::MUL)
      return false;
    SDValue ExtA = Mul->getOperand(0);
    SDValue ExtB = Mul->getOperand(1);
    if (ExtA->getOpcode() != ExtendCode || ExtB->getOpcode() != ExtendCode)
      return false;
    A = ExtA->getOperand(0);
    B = ExtB->getOperand(0);
    if (ExtTypeMatches(A, ExtTypes) && ExtTypeMatches(B, ExtTypes)) {
      A = ExtendIfNeeded(A, ExtendCode);
      B = ExtendIfNeeded(B, ExtendCode);
      return true;
    }
    return false;
  };
  auto Create64bitNode = [&](unsigned Opcode, ArrayRef<SDValue> Ops) {
    // Split illegal MVT::v16i8->i64 vector reductions into two legal v8i16->i64
    // reductions. The operands are extended with MVEEXT, but as they are
    // reductions the lane orders do not matter. MVEEXT may be combined with
    // loads to produce two extending loads, or else they will be expanded to
    // VREV/VMOVL.
    EVT VT = Ops[0].getValueType();
    if (VT == MVT::v16i8) {
      assert((Opcode == ARMISD::VMLALVs || Opcode == ARMISD::VMLALVu) &&
             "Unexpected illegal long reduction opcode");
      bool IsUnsigned = Opcode == ARMISD::VMLALVu;

      SDValue Ext0 =
          DAG.getNode(IsUnsigned ? ARMISD::MVEZEXT : ARMISD::MVESEXT, dl,
                      DAG.getVTList(MVT::v8i16, MVT::v8i16), Ops[0]);
      SDValue Ext1 =
          DAG.getNode(IsUnsigned ? ARMISD::MVEZEXT : ARMISD::MVESEXT, dl,
                      DAG.getVTList(MVT::v8i16, MVT::v8i16), Ops[1]);

      SDValue MLA0 = DAG.getNode(Opcode, dl, DAG.getVTList(MVT::i32, MVT::i32),
                                 Ext0, Ext1);
      SDValue MLA1 =
          DAG.getNode(IsUnsigned ? ARMISD::VMLALVAu : ARMISD::VMLALVAs, dl,
                      DAG.getVTList(MVT::i32, MVT::i32), MLA0, MLA0.getValue(1),
                      Ext0.getValue(1), Ext1.getValue(1));
      return DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, MLA1, MLA1.getValue(1));
    }
    SDValue Node = DAG.getNode(Opcode, dl, {MVT::i32, MVT::i32}, Ops);
    return DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, Node,
                       SDValue(Node.getNode(), 1));
  };

  SDValue A, B;
  SDValue Mask;
  if (IsVMLAV(MVT::i32, ISD::SIGN_EXTEND, {MVT::v8i16, MVT::v16i8}, A, B))
    return DAG.getNode(ARMISD::VMLAVs, dl, ResVT, A, B);
  if (IsVMLAV(MVT::i32, ISD::ZERO_EXTEND, {MVT::v8i16, MVT::v16i8}, A, B))
    return DAG.getNode(ARMISD::VMLAVu, dl, ResVT, A, B);
  if (IsVMLAV(MVT::i64, ISD::SIGN_EXTEND, {MVT::v16i8, MVT::v8i16, MVT::v4i32},
              A, B))
    return Create64bitNode(ARMISD::VMLALVs, {A, B});
  if (IsVMLAV(MVT::i64, ISD::ZERO_EXTEND, {MVT::v16i8, MVT::v8i16, MVT::v4i32},
              A, B))
    return Create64bitNode(ARMISD::VMLALVu, {A, B});
  if (IsVMLAV(MVT::i16, ISD::SIGN_EXTEND, {MVT::v16i8}, A, B))
    return DAG.getNode(ISD::TRUNCATE, dl, ResVT,
                       DAG.getNode(ARMISD::VMLAVs, dl, MVT::i32, A, B));
  if (IsVMLAV(MVT::i16, ISD::ZERO_EXTEND, {MVT::v16i8}, A, B))
    return DAG.getNode(ISD::TRUNCATE, dl, ResVT,
                       DAG.getNode(ARMISD::VMLAVu, dl, MVT::i32, A, B));

  if (IsPredVMLAV(MVT::i32, ISD::SIGN_EXTEND, {MVT::v8i16, MVT::v16i8}, A, B,
                  Mask))
    return DAG.getNode(ARMISD::VMLAVps, dl, ResVT, A, B, Mask);
  if (IsPredVMLAV(MVT::i32, ISD::ZERO_EXTEND, {MVT::v8i16, MVT::v16i8}, A, B,
                  Mask))
    return DAG.getNode(ARMISD::VMLAVpu, dl, ResVT, A, B, Mask);
  if (IsPredVMLAV(MVT::i64, ISD::SIGN_EXTEND, {MVT::v8i16, MVT::v4i32}, A, B,
                  Mask))
    return Create64bitNode(ARMISD::VMLALVps, {A, B, Mask});
  if (IsPredVMLAV(MVT::i64, ISD::ZERO_EXTEND, {MVT::v8i16, MVT::v4i32}, A, B,
                  Mask))
    return Create64bitNode(ARMISD::VMLALVpu, {A, B, Mask});
  if (IsPredVMLAV(MVT::i16, ISD::SIGN_EXTEND, {MVT::v16i8}, A, B, Mask))
    return DAG.getNode(ISD::TRUNCATE, dl, ResVT,
                       DAG.getNode(ARMISD::VMLAVps, dl, MVT::i32, A, B, Mask));
  if (IsPredVMLAV(MVT::i16, ISD::ZERO_EXTEND, {MVT::v16i8}, A, B, Mask))
    return DAG.getNode(ISD::TRUNCATE, dl, ResVT,
                       DAG.getNode(ARMISD::VMLAVpu, dl, MVT::i32, A, B, Mask));

  if (SDValue A = IsVADDV(MVT::i32, ISD::SIGN_EXTEND, {MVT::v8i16, MVT::v16i8}))
    return DAG.getNode(ARMISD::VADDVs, dl, ResVT, A);
  if (SDValue A = IsVADDV(MVT::i32, ISD::ZERO_EXTEND, {MVT::v8i16, MVT::v16i8}))
    return DAG.getNode(ARMISD::VADDVu, dl, ResVT, A);
  if (SDValue A = IsVADDV(MVT::i64, ISD::SIGN_EXTEND, {MVT::v4i32}))
    return Create64bitNode(ARMISD::VADDLVs, {A});
  if (SDValue A = IsVADDV(MVT::i64, ISD::ZERO_EXTEND, {MVT::v4i32}))
    return Create64bitNode(ARMISD::VADDLVu, {A});
  if (SDValue A = IsVADDV(MVT::i16, ISD::SIGN_EXTEND, {MVT::v16i8}))
    return DAG.getNode(ISD::TRUNCATE, dl, ResVT,
                       DAG.getNode(ARMISD::VADDVs, dl, MVT::i32, A));
  if (SDValue A = IsVADDV(MVT::i16, ISD::ZERO_EXTEND, {MVT::v16i8}))
    return DAG.getNode(ISD::TRUNCATE, dl, ResVT,
                       DAG.getNode(ARMISD::VADDVu, dl, MVT::i32, A));

  if (SDValue A = IsPredVADDV(MVT::i32, ISD::SIGN_EXTEND, {MVT::v8i16, MVT::v16i8}, Mask))
    return DAG.getNode(ARMISD::VADDVps, dl, ResVT, A, Mask);
  if (SDValue A = IsPredVADDV(MVT::i32, ISD::ZERO_EXTEND, {MVT::v8i16, MVT::v16i8}, Mask))
    return DAG.getNode(ARMISD::VADDVpu, dl, ResVT, A, Mask);
  if (SDValue A = IsPredVADDV(MVT::i64, ISD::SIGN_EXTEND, {MVT::v4i32}, Mask))
    return Create64bitNode(ARMISD::VADDLVps, {A, Mask});
  if (SDValue A = IsPredVADDV(MVT::i64, ISD::ZERO_EXTEND, {MVT::v4i32}, Mask))
    return Create64bitNode(ARMISD::VADDLVpu, {A, Mask});
  if (SDValue A = IsPredVADDV(MVT::i16, ISD::SIGN_EXTEND, {MVT::v16i8}, Mask))
    return DAG.getNode(ISD::TRUNCATE, dl, ResVT,
                       DAG.getNode(ARMISD::VADDVps, dl, MVT::i32, A, Mask));
  if (SDValue A = IsPredVADDV(MVT::i16, ISD::ZERO_EXTEND, {MVT::v16i8}, Mask))
    return DAG.getNode(ISD::TRUNCATE, dl, ResVT,
                       DAG.getNode(ARMISD::VADDVpu, dl, MVT::i32, A, Mask));

  // Some complications. We can get a case where the two inputs of the mul are
  // the same, then the output sext will have been helpfully converted to a
  // zext. Turn it back.
  SDValue Op = N0;
  if (Op->getOpcode() == ISD::VSELECT)
    Op = Op->getOperand(1);
  if (Op->getOpcode() == ISD::ZERO_EXTEND &&
      Op->getOperand(0)->getOpcode() == ISD::MUL) {
    SDValue Mul = Op->getOperand(0);
    if (Mul->getOperand(0) == Mul->getOperand(1) &&
        Mul->getOperand(0)->getOpcode() == ISD::SIGN_EXTEND) {
      SDValue Ext = DAG.getNode(ISD::SIGN_EXTEND, dl, N0->getValueType(0), Mul);
      if (Op != N0)
        Ext = DAG.getNode(ISD::VSELECT, dl, N0->getValueType(0),
                          N0->getOperand(0), Ext, N0->getOperand(2));
      return DAG.getNode(ISD::VECREDUCE_ADD, dl, ResVT, Ext);
    }
  }

  return SDValue();
}

// Looks for vaddv(shuffle) or vmlav(shuffle, shuffle), with a shuffle where all
// the lanes are used. Due to the reduction being commutative the shuffle can be
// removed.
static SDValue PerformReduceShuffleCombine(SDNode *N, SelectionDAG &DAG) {
  unsigned VecOp = N->getOperand(0).getValueType().isVector() ? 0 : 2;
  auto *Shuf = dyn_cast<ShuffleVectorSDNode>(N->getOperand(VecOp));
  if (!Shuf || !Shuf->getOperand(1).isUndef())
    return SDValue();

  // Check all elements are used once in the mask.
  ArrayRef<int> Mask = Shuf->getMask();
  APInt SetElts(Mask.size(), 0);
  for (int E : Mask) {
    if (E < 0 || E >= (int)Mask.size())
      return SDValue();
    SetElts.setBit(E);
  }
  if (!SetElts.isAllOnes())
    return SDValue();

  if (N->getNumOperands() != VecOp + 1) {
    auto *Shuf2 = dyn_cast<ShuffleVectorSDNode>(N->getOperand(VecOp + 1));
    if (!Shuf2 || !Shuf2->getOperand(1).isUndef() || Shuf2->getMask() != Mask)
      return SDValue();
  }

  SmallVector<SDValue> Ops;
  for (SDValue Op : N->ops()) {
    if (Op.getValueType().isVector())
      Ops.push_back(Op.getOperand(0));
    else
      Ops.push_back(Op);
  }
  return DAG.getNode(N->getOpcode(), SDLoc(N), N->getVTList(), Ops);
}

static SDValue PerformVMOVNCombine(SDNode *N,
                                   TargetLowering::DAGCombinerInfo &DCI) {
  SDValue Op0 = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);
  unsigned IsTop = N->getConstantOperandVal(2);

  // VMOVNT a undef -> a
  // VMOVNB a undef -> a
  // VMOVNB undef a -> a
  if (Op1->isUndef())
    return Op0;
  if (Op0->isUndef() && !IsTop)
    return Op1;

  // VMOVNt(c, VQMOVNb(a, b)) => VQMOVNt(c, b)
  // VMOVNb(c, VQMOVNb(a, b)) => VQMOVNb(c, b)
  if ((Op1->getOpcode() == ARMISD::VQMOVNs ||
       Op1->getOpcode() == ARMISD::VQMOVNu) &&
      Op1->getConstantOperandVal(2) == 0)
    return DCI.DAG.getNode(Op1->getOpcode(), SDLoc(Op1), N->getValueType(0),
                           Op0, Op1->getOperand(1), N->getOperand(2));

  // Only the bottom lanes from Qm (Op1) and either the top or bottom lanes from
  // Qd (Op0) are demanded from a VMOVN, depending on whether we are inserting
  // into the top or bottom lanes.
  unsigned NumElts = N->getValueType(0).getVectorNumElements();
  APInt Op1DemandedElts = APInt::getSplat(NumElts, APInt::getLowBitsSet(2, 1));
  APInt Op0DemandedElts =
      IsTop ? Op1DemandedElts
            : APInt::getSplat(NumElts, APInt::getHighBitsSet(2, 1));

  const TargetLowering &TLI = DCI.DAG.getTargetLoweringInfo();
  if (TLI.SimplifyDemandedVectorElts(Op0, Op0DemandedElts, DCI))
    return SDValue(N, 0);
  if (TLI.SimplifyDemandedVectorElts(Op1, Op1DemandedElts, DCI))
    return SDValue(N, 0);

  return SDValue();
}

static SDValue PerformVQMOVNCombine(SDNode *N,
                                    TargetLowering::DAGCombinerInfo &DCI) {
  SDValue Op0 = N->getOperand(0);
  unsigned IsTop = N->getConstantOperandVal(2);

  unsigned NumElts = N->getValueType(0).getVectorNumElements();
  APInt Op0DemandedElts =
      APInt::getSplat(NumElts, IsTop ? APInt::getLowBitsSet(2, 1)
                                     : APInt::getHighBitsSet(2, 1));

  const TargetLowering &TLI = DCI.DAG.getTargetLoweringInfo();
  if (TLI.SimplifyDemandedVectorElts(Op0, Op0DemandedElts, DCI))
    return SDValue(N, 0);
  return SDValue();
}

static SDValue PerformVQDMULHCombine(SDNode *N,
                                     TargetLowering::DAGCombinerInfo &DCI) {
  EVT VT = N->getValueType(0);
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  auto *Shuf0 = dyn_cast<ShuffleVectorSDNode>(LHS);
  auto *Shuf1 = dyn_cast<ShuffleVectorSDNode>(RHS);
  // Turn VQDMULH(shuffle, shuffle) -> shuffle(VQDMULH)
  if (Shuf0 && Shuf1 && Shuf0->getMask().equals(Shuf1->getMask()) &&
      LHS.getOperand(1).isUndef() && RHS.getOperand(1).isUndef() &&
      (LHS.hasOneUse() || RHS.hasOneUse() || LHS == RHS)) {
    SDLoc DL(N);
    SDValue NewBinOp = DCI.DAG.getNode(N->getOpcode(), DL, VT,
                                       LHS.getOperand(0), RHS.getOperand(0));
    SDValue UndefV = LHS.getOperand(1);
    return DCI.DAG.getVectorShuffle(VT, DL, NewBinOp, UndefV, Shuf0->getMask());
  }
  return SDValue();
}

static SDValue PerformLongShiftCombine(SDNode *N, SelectionDAG &DAG) {
  SDLoc DL(N);
  SDValue Op0 = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);

  // Turn X << -C -> X >> C and viceversa. The negative shifts can come up from
  // uses of the intrinsics.
  if (auto C = dyn_cast<ConstantSDNode>(N->getOperand(2))) {
    int ShiftAmt = C->getSExtValue();
    if (ShiftAmt == 0) {
      SDValue Merge = DAG.getMergeValues({Op0, Op1}, DL);
      DAG.ReplaceAllUsesWith(N, Merge.getNode());
      return SDValue();
    }

    if (ShiftAmt >= -32 && ShiftAmt < 0) {
      unsigned NewOpcode =
          N->getOpcode() == ARMISD::LSLL ? ARMISD::LSRL : ARMISD::LSLL;
      SDValue NewShift = DAG.getNode(NewOpcode, DL, N->getVTList(), Op0, Op1,
                                     DAG.getConstant(-ShiftAmt, DL, MVT::i32));
      DAG.ReplaceAllUsesWith(N, NewShift.getNode());
      return NewShift;
    }
  }

  return SDValue();
}

/// PerformIntrinsicCombine - ARM-specific DAG combining for intrinsics.
SDValue ARMTargetLowering::PerformIntrinsicCombine(SDNode *N,
                                                   DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  unsigned IntNo = N->getConstantOperandVal(0);
  switch (IntNo) {
  default:
    // Don't do anything for most intrinsics.
    break;

  // Vector shifts: check for immediate versions and lower them.
  // Note: This is done during DAG combining instead of DAG legalizing because
  // the build_vectors for 64-bit vector element shift counts are generally
  // not legal, and it is hard to see their values after they get legalized to
  // loads from a constant pool.
  case Intrinsic::arm_neon_vshifts:
  case Intrinsic::arm_neon_vshiftu:
  case Intrinsic::arm_neon_vrshifts:
  case Intrinsic::arm_neon_vrshiftu:
  case Intrinsic::arm_neon_vrshiftn:
  case Intrinsic::arm_neon_vqshifts:
  case Intrinsic::arm_neon_vqshiftu:
  case Intrinsic::arm_neon_vqshiftsu:
  case Intrinsic::arm_neon_vqshiftns:
  case Intrinsic::arm_neon_vqshiftnu:
  case Intrinsic::arm_neon_vqshiftnsu:
  case Intrinsic::arm_neon_vqrshiftns:
  case Intrinsic::arm_neon_vqrshiftnu:
  case Intrinsic::arm_neon_vqrshiftnsu: {
    EVT VT = N->getOperand(1).getValueType();
    int64_t Cnt;
    unsigned VShiftOpc = 0;

    switch (IntNo) {
    case Intrinsic::arm_neon_vshifts:
    case Intrinsic::arm_neon_vshiftu:
      if (isVShiftLImm(N->getOperand(2), VT, false, Cnt)) {
        VShiftOpc = ARMISD::VSHLIMM;
        break;
      }
      if (isVShiftRImm(N->getOperand(2), VT, false, true, Cnt)) {
        VShiftOpc = (IntNo == Intrinsic::arm_neon_vshifts ? ARMISD::VSHRsIMM
                                                          : ARMISD::VSHRuIMM);
        break;
      }
      return SDValue();

    case Intrinsic::arm_neon_vrshifts:
    case Intrinsic::arm_neon_vrshiftu:
      if (isVShiftRImm(N->getOperand(2), VT, false, true, Cnt))
        break;
      return SDValue();

    case Intrinsic::arm_neon_vqshifts:
    case Intrinsic::arm_neon_vqshiftu:
      if (isVShiftLImm(N->getOperand(2), VT, false, Cnt))
        break;
      return SDValue();

    case Intrinsic::arm_neon_vqshiftsu:
      if (isVShiftLImm(N->getOperand(2), VT, false, Cnt))
        break;
      llvm_unreachable("invalid shift count for vqshlu intrinsic");

    case Intrinsic::arm_neon_vrshiftn:
    case Intrinsic::arm_neon_vqshiftns:
    case Intrinsic::arm_neon_vqshiftnu:
    case Intrinsic::arm_neon_vqshiftnsu:
    case Intrinsic::arm_neon_vqrshiftns:
    case Intrinsic::arm_neon_vqrshiftnu:
    case Intrinsic::arm_neon_vqrshiftnsu:
      // Narrowing shifts require an immediate right shift.
      if (isVShiftRImm(N->getOperand(2), VT, true, true, Cnt))
        break;
      llvm_unreachable("invalid shift count for narrowing vector shift "
                       "intrinsic");

    default:
      llvm_unreachable("unhandled vector shift");
    }

    switch (IntNo) {
    case Intrinsic::arm_neon_vshifts:
    case Intrinsic::arm_neon_vshiftu:
      // Opcode already set above.
      break;
    case Intrinsic::arm_neon_vrshifts:
      VShiftOpc = ARMISD::VRSHRsIMM;
      break;
    case Intrinsic::arm_neon_vrshiftu:
      VShiftOpc = ARMISD::VRSHRuIMM;
      break;
    case Intrinsic::arm_neon_vrshiftn:
      VShiftOpc = ARMISD::VRSHRNIMM;
      break;
    case Intrinsic::arm_neon_vqshifts:
      VShiftOpc = ARMISD::VQSHLsIMM;
      break;
    case Intrinsic::arm_neon_vqshiftu:
      VShiftOpc = ARMISD::VQSHLuIMM;
      break;
    case Intrinsic::arm_neon_vqshiftsu:
      VShiftOpc = ARMISD::VQSHLsuIMM;
      break;
    case Intrinsic::arm_neon_vqshiftns:
      VShiftOpc = ARMISD::VQSHRNsIMM;
      break;
    case Intrinsic::arm_neon_vqshiftnu:
      VShiftOpc = ARMISD::VQSHRNuIMM;
      break;
    case Intrinsic::arm_neon_vqshiftnsu:
      VShiftOpc = ARMISD::VQSHRNsuIMM;
      break;
    case Intrinsic::arm_neon_vqrshiftns:
      VShiftOpc = ARMISD::VQRSHRNsIMM;
      break;
    case Intrinsic::arm_neon_vqrshiftnu:
      VShiftOpc = ARMISD::VQRSHRNuIMM;
      break;
    case Intrinsic::arm_neon_vqrshiftnsu:
      VShiftOpc = ARMISD::VQRSHRNsuIMM;
      break;
    }

    SDLoc dl(N);
    return DAG.getNode(VShiftOpc, dl, N->getValueType(0),
                       N->getOperand(1), DAG.getConstant(Cnt, dl, MVT::i32));
  }

  case Intrinsic::arm_neon_vshiftins: {
    EVT VT = N->getOperand(1).getValueType();
    int64_t Cnt;
    unsigned VShiftOpc = 0;

    if (isVShiftLImm(N->getOperand(3), VT, false, Cnt))
      VShiftOpc = ARMISD::VSLIIMM;
    else if (isVShiftRImm(N->getOperand(3), VT, false, true, Cnt))
      VShiftOpc = ARMISD::VSRIIMM;
    else {
      llvm_unreachable("invalid shift count for vsli/vsri intrinsic");
    }

    SDLoc dl(N);
    return DAG.getNode(VShiftOpc, dl, N->getValueType(0),
                       N->getOperand(1), N->getOperand(2),
                       DAG.getConstant(Cnt, dl, MVT::i32));
  }

  case Intrinsic::arm_neon_vqrshifts:
  case Intrinsic::arm_neon_vqrshiftu:
    // No immediate versions of these to check for.
    break;

  case Intrinsic::arm_mve_vqdmlah:
  case Intrinsic::arm_mve_vqdmlash:
  case Intrinsic::arm_mve_vqrdmlah:
  case Intrinsic::arm_mve_vqrdmlash:
  case Intrinsic::arm_mve_vmla_n_predicated:
  case Intrinsic::arm_mve_vmlas_n_predicated:
  case Intrinsic::arm_mve_vqdmlah_predicated:
  case Intrinsic::arm_mve_vqdmlash_predicated:
  case Intrinsic::arm_mve_vqrdmlah_predicated:
  case Intrinsic::arm_mve_vqrdmlash_predicated: {
    // These intrinsics all take an i32 scalar operand which is narrowed to the
    // size of a single lane of the vector type they return. So we don't need
    // any bits of that operand above that point, which allows us to eliminate
    // uxth/sxth.
    unsigned BitWidth = N->getValueType(0).getScalarSizeInBits();
    APInt DemandedMask = APInt::getLowBitsSet(32, BitWidth);
    if (SimplifyDemandedBits(N->getOperand(3), DemandedMask, DCI))
      return SDValue();
    break;
  }

  case Intrinsic::arm_mve_minv:
  case Intrinsic::arm_mve_maxv:
  case Intrinsic::arm_mve_minav:
  case Intrinsic::arm_mve_maxav:
  case Intrinsic::arm_mve_minv_predicated:
  case Intrinsic::arm_mve_maxv_predicated:
  case Intrinsic::arm_mve_minav_predicated:
  case Intrinsic::arm_mve_maxav_predicated: {
    // These intrinsics all take an i32 scalar operand which is narrowed to the
    // size of a single lane of the vector type they take as the other input.
    unsigned BitWidth = N->getOperand(2)->getValueType(0).getScalarSizeInBits();
    APInt DemandedMask = APInt::getLowBitsSet(32, BitWidth);
    if (SimplifyDemandedBits(N->getOperand(1), DemandedMask, DCI))
      return SDValue();
    break;
  }

  case Intrinsic::arm_mve_addv: {
    // Turn this intrinsic straight into the appropriate ARMISD::VADDV node,
    // which allow PerformADDVecReduce to turn it into VADDLV when possible.
    bool Unsigned = N->getConstantOperandVal(2);
    unsigned Opc = Unsigned ? ARMISD::VADDVu : ARMISD::VADDVs;
    return DAG.getNode(Opc, SDLoc(N), N->getVTList(), N->getOperand(1));
  }

  case Intrinsic::arm_mve_addlv:
  case Intrinsic::arm_mve_addlv_predicated: {
    // Same for these, but ARMISD::VADDLV has to be followed by a BUILD_PAIR
    // which recombines the two outputs into an i64
    bool Unsigned = N->getConstantOperandVal(2);
    unsigned Opc = IntNo == Intrinsic::arm_mve_addlv ?
                    (Unsigned ? ARMISD::VADDLVu : ARMISD::VADDLVs) :
                    (Unsigned ? ARMISD::VADDLVpu : ARMISD::VADDLVps);

    SmallVector<SDValue, 4> Ops;
    for (unsigned i = 1, e = N->getNumOperands(); i < e; i++)
      if (i != 2)                      // skip the unsigned flag
        Ops.push_back(N->getOperand(i));

    SDLoc dl(N);
    SDValue val = DAG.getNode(Opc, dl, {MVT::i32, MVT::i32}, Ops);
    return DAG.getNode(ISD::BUILD_PAIR, dl, MVT::i64, val.getValue(0),
                       val.getValue(1));
  }
  }

  return SDValue();
}

/// PerformShiftCombine - Checks for immediate versions of vector shifts and
/// lowers them.  As with the vector shift intrinsics, this is done during DAG
/// combining instead of DAG legalizing because the build_vectors for 64-bit
/// vector element shift counts are generally not legal, and it is hard to see
/// their values after they get legalized to loads from a constant pool.
static SDValue PerformShiftCombine(SDNode *N,
                                   TargetLowering::DAGCombinerInfo &DCI,
                                   const ARMSubtarget *ST) {
  SelectionDAG &DAG = DCI.DAG;
  EVT VT = N->getValueType(0);

  if (ST->isThumb1Only() && N->getOpcode() == ISD::SHL && VT == MVT::i32 &&
      N->getOperand(0)->getOpcode() == ISD::AND &&
      N->getOperand(0)->hasOneUse()) {
    if (DCI.isBeforeLegalize() || DCI.isCalledByLegalizer())
      return SDValue();
    // Look for the pattern (shl (and x, AndMask), ShiftAmt). This doesn't
    // usually show up because instcombine prefers to canonicalize it to
    // (and (shl x, ShiftAmt) (shl AndMask, ShiftAmt)), but the shift can come
    // out of GEP lowering in some cases.
    SDValue N0 = N->getOperand(0);
    ConstantSDNode *ShiftAmtNode = dyn_cast<ConstantSDNode>(N->getOperand(1));
    if (!ShiftAmtNode)
      return SDValue();
    uint32_t ShiftAmt = static_cast<uint32_t>(ShiftAmtNode->getZExtValue());
    ConstantSDNode *AndMaskNode = dyn_cast<ConstantSDNode>(N0->getOperand(1));
    if (!AndMaskNode)
      return SDValue();
    uint32_t AndMask = static_cast<uint32_t>(AndMaskNode->getZExtValue());
    // Don't transform uxtb/uxth.
    if (AndMask == 255 || AndMask == 65535)
      return SDValue();
    if (isMask_32(AndMask)) {
      uint32_t MaskedBits = llvm::countl_zero(AndMask);
      if (MaskedBits > ShiftAmt) {
        SDLoc DL(N);
        SDValue SHL = DAG.getNode(ISD::SHL, DL, MVT::i32, N0->getOperand(0),
                                  DAG.getConstant(MaskedBits, DL, MVT::i32));
        return DAG.getNode(
            ISD::SRL, DL, MVT::i32, SHL,
            DAG.getConstant(MaskedBits - ShiftAmt, DL, MVT::i32));
      }
    }
  }

  // Nothing to be done for scalar shifts.
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  if (!VT.isVector() || !TLI.isTypeLegal(VT))
    return SDValue();
  if (ST->hasMVEIntegerOps())
    return SDValue();

  int64_t Cnt;

  switch (N->getOpcode()) {
  default: llvm_unreachable("unexpected shift opcode");

  case ISD::SHL:
    if (isVShiftLImm(N->getOperand(1), VT, false, Cnt)) {
      SDLoc dl(N);
      return DAG.getNode(ARMISD::VSHLIMM, dl, VT, N->getOperand(0),
                         DAG.getConstant(Cnt, dl, MVT::i32));
    }
    break;

  case ISD::SRA:
  case ISD::SRL:
    if (isVShiftRImm(N->getOperand(1), VT, false, false, Cnt)) {
      unsigned VShiftOpc =
          (N->getOpcode() == ISD::SRA ? ARMISD::VSHRsIMM : ARMISD::VSHRuIMM);
      SDLoc dl(N);
      return DAG.getNode(VShiftOpc, dl, VT, N->getOperand(0),
                         DAG.getConstant(Cnt, dl, MVT::i32));
    }
  }
  return SDValue();
}

// Look for a sign/zero/fpextend extend of a larger than legal load. This can be
// split into multiple extending loads, which are simpler to deal with than an
// arbitrary extend. For fp extends we use an integer extending load and a VCVTL
// to convert the type to an f32.
static SDValue PerformSplittingToWideningLoad(SDNode *N, SelectionDAG &DAG) {
  SDValue N0 = N->getOperand(0);
  if (N0.getOpcode() != ISD::LOAD)
    return SDValue();
  LoadSDNode *LD = cast<LoadSDNode>(N0.getNode());
  if (!LD->isSimple() || !N0.hasOneUse() || LD->isIndexed() ||
      LD->getExtensionType() != ISD::NON_EXTLOAD)
    return SDValue();
  EVT FromVT = LD->getValueType(0);
  EVT ToVT = N->getValueType(0);
  if (!ToVT.isVector())
    return SDValue();
  assert(FromVT.getVectorNumElements() == ToVT.getVectorNumElements());
  EVT ToEltVT = ToVT.getVectorElementType();
  EVT FromEltVT = FromVT.getVectorElementType();

  unsigned NumElements = 0;
  if (ToEltVT == MVT::i32 && FromEltVT == MVT::i8)
    NumElements = 4;
  if (ToEltVT == MVT::f32 && FromEltVT == MVT::f16)
    NumElements = 4;
  if (NumElements == 0 ||
      (FromEltVT != MVT::f16 && FromVT.getVectorNumElements() == NumElements) ||
      FromVT.getVectorNumElements() % NumElements != 0 ||
      !isPowerOf2_32(NumElements))
    return SDValue();

  LLVMContext &C = *DAG.getContext();
  SDLoc DL(LD);
  // Details about the old load
  SDValue Ch = LD->getChain();
  SDValue BasePtr = LD->getBasePtr();
  Align Alignment = LD->getOriginalAlign();
  MachineMemOperand::Flags MMOFlags = LD->getMemOperand()->getFlags();
  AAMDNodes AAInfo = LD->getAAInfo();

  ISD::LoadExtType NewExtType =
      N->getOpcode() == ISD::SIGN_EXTEND ? ISD::SEXTLOAD : ISD::ZEXTLOAD;
  SDValue Offset = DAG.getUNDEF(BasePtr.getValueType());
  EVT NewFromVT = EVT::getVectorVT(
      C, EVT::getIntegerVT(C, FromEltVT.getScalarSizeInBits()), NumElements);
  EVT NewToVT = EVT::getVectorVT(
      C, EVT::getIntegerVT(C, ToEltVT.getScalarSizeInBits()), NumElements);

  SmallVector<SDValue, 4> Loads;
  SmallVector<SDValue, 4> Chains;
  for (unsigned i = 0; i < FromVT.getVectorNumElements() / NumElements; i++) {
    unsigned NewOffset = (i * NewFromVT.getSizeInBits()) / 8;
    SDValue NewPtr =
        DAG.getObjectPtrOffset(DL, BasePtr, TypeSize::getFixed(NewOffset));

    SDValue NewLoad =
        DAG.getLoad(ISD::UNINDEXED, NewExtType, NewToVT, DL, Ch, NewPtr, Offset,
                    LD->getPointerInfo().getWithOffset(NewOffset), NewFromVT,
                    Alignment, MMOFlags, AAInfo);
    Loads.push_back(NewLoad);
    Chains.push_back(SDValue(NewLoad.getNode(), 1));
  }

  // Float truncs need to extended with VCVTB's into their floating point types.
  if (FromEltVT == MVT::f16) {
    SmallVector<SDValue, 4> Extends;

    for (unsigned i = 0; i < Loads.size(); i++) {
      SDValue LoadBC =
          DAG.getNode(ARMISD::VECTOR_REG_CAST, DL, MVT::v8f16, Loads[i]);
      SDValue FPExt = DAG.getNode(ARMISD::VCVTL, DL, MVT::v4f32, LoadBC,
                                  DAG.getConstant(0, DL, MVT::i32));
      Extends.push_back(FPExt);
    }

    Loads = Extends;
  }

  SDValue NewChain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Chains);
  DAG.ReplaceAllUsesOfValueWith(SDValue(LD, 1), NewChain);
  return DAG.getNode(ISD::CONCAT_VECTORS, DL, ToVT, Loads);
}

/// PerformExtendCombine - Target-specific DAG combining for ISD::SIGN_EXTEND,
/// ISD::ZERO_EXTEND, and ISD::ANY_EXTEND.
static SDValue PerformExtendCombine(SDNode *N, SelectionDAG &DAG,
                                    const ARMSubtarget *ST) {
  SDValue N0 = N->getOperand(0);

  // Check for sign- and zero-extensions of vector extract operations of 8- and
  // 16-bit vector elements. NEON and MVE support these directly. They are
  // handled during DAG combining because type legalization will promote them
  // to 32-bit types and it is messy to recognize the operations after that.
  if ((ST->hasNEON() || ST->hasMVEIntegerOps()) &&
      N0.getOpcode() == ISD::EXTRACT_VECTOR_ELT) {
    SDValue Vec = N0.getOperand(0);
    SDValue Lane = N0.getOperand(1);
    EVT VT = N->getValueType(0);
    EVT EltVT = N0.getValueType();
    const TargetLowering &TLI = DAG.getTargetLoweringInfo();

    if (VT == MVT::i32 &&
        (EltVT == MVT::i8 || EltVT == MVT::i16) &&
        TLI.isTypeLegal(Vec.getValueType()) &&
        isa<ConstantSDNode>(Lane)) {

      unsigned Opc = 0;
      switch (N->getOpcode()) {
      default: llvm_unreachable("unexpected opcode");
      case ISD::SIGN_EXTEND:
        Opc = ARMISD::VGETLANEs;
        break;
      case ISD::ZERO_EXTEND:
      case ISD::ANY_EXTEND:
        Opc = ARMISD::VGETLANEu;
        break;
      }
      return DAG.getNode(Opc, SDLoc(N), VT, Vec, Lane);
    }
  }

  if (ST->hasMVEIntegerOps())
    if (SDValue NewLoad = PerformSplittingToWideningLoad(N, DAG))
      return NewLoad;

  return SDValue();
}

static SDValue PerformFPExtendCombine(SDNode *N, SelectionDAG &DAG,
                                      const ARMSubtarget *ST) {
  if (ST->hasMVEFloatOps())
    if (SDValue NewLoad = PerformSplittingToWideningLoad(N, DAG))
      return NewLoad;

  return SDValue();
}

// Lower smin(smax(x, C1), C2) to ssat or usat, if they have saturating
// constant bounds.
static SDValue PerformMinMaxToSatCombine(SDValue Op, SelectionDAG &DAG,
                                         const ARMSubtarget *Subtarget) {
  if ((Subtarget->isThumb() || !Subtarget->hasV6Ops()) &&
      !Subtarget->isThumb2())
    return SDValue();

  EVT VT = Op.getValueType();
  SDValue Op0 = Op.getOperand(0);

  if (VT != MVT::i32 ||
      (Op0.getOpcode() != ISD::SMIN && Op0.getOpcode() != ISD::SMAX) ||
      !isa<ConstantSDNode>(Op.getOperand(1)) ||
      !isa<ConstantSDNode>(Op0.getOperand(1)))
    return SDValue();

  SDValue Min = Op;
  SDValue Max = Op0;
  SDValue Input = Op0.getOperand(0);
  if (Min.getOpcode() == ISD::SMAX)
    std::swap(Min, Max);

  APInt MinC = Min.getConstantOperandAPInt(1);
  APInt MaxC = Max.getConstantOperandAPInt(1);

  if (Min.getOpcode() != ISD::SMIN || Max.getOpcode() != ISD::SMAX ||
      !(MinC + 1).isPowerOf2())
    return SDValue();

  SDLoc DL(Op);
  if (MinC == ~MaxC)
    return DAG.getNode(ARMISD::SSAT, DL, VT, Input,
                       DAG.getConstant(MinC.countr_one(), DL, VT));
  if (MaxC == 0)
    return DAG.getNode(ARMISD::USAT, DL, VT, Input,
                       DAG.getConstant(MinC.countr_one(), DL, VT));

  return SDValue();
}

/// PerformMinMaxCombine - Target-specific DAG combining for creating truncating
/// saturates.
static SDValue PerformMinMaxCombine(SDNode *N, SelectionDAG &DAG,
                                    const ARMSubtarget *ST) {
  EVT VT = N->getValueType(0);
  SDValue N0 = N->getOperand(0);

  if (VT == MVT::i32)
    return PerformMinMaxToSatCombine(SDValue(N, 0), DAG, ST);

  if (!ST->hasMVEIntegerOps())
    return SDValue();

  if (SDValue V = PerformVQDMULHCombine(N, DAG))
    return V;

  if (VT != MVT::v4i32 && VT != MVT::v8i16)
    return SDValue();

  auto IsSignedSaturate = [&](SDNode *Min, SDNode *Max) {
    // Check one is a smin and the other is a smax
    if (Min->getOpcode() != ISD::SMIN)
      std::swap(Min, Max);
    if (Min->getOpcode() != ISD::SMIN || Max->getOpcode() != ISD::SMAX)
      return false;

    APInt SaturateC;
    if (VT == MVT::v4i32)
      SaturateC = APInt(32, (1 << 15) - 1, true);
    else //if (VT == MVT::v8i16)
      SaturateC = APInt(16, (1 << 7) - 1, true);

    APInt MinC, MaxC;
    if (!ISD::isConstantSplatVector(Min->getOperand(1).getNode(), MinC) ||
        MinC != SaturateC)
      return false;
    if (!ISD::isConstantSplatVector(Max->getOperand(1).getNode(), MaxC) ||
        MaxC != ~SaturateC)
      return false;
    return true;
  };

  if (IsSignedSaturate(N, N0.getNode())) {
    SDLoc DL(N);
    MVT ExtVT, HalfVT;
    if (VT == MVT::v4i32) {
      HalfVT = MVT::v8i16;
      ExtVT = MVT::v4i16;
    } else { // if (VT == MVT::v8i16)
      HalfVT = MVT::v16i8;
      ExtVT = MVT::v8i8;
    }

    // Create a VQMOVNB with undef top lanes, then signed extended into the top
    // half. That extend will hopefully be removed if only the bottom bits are
    // demanded (though a truncating store, for example).
    SDValue VQMOVN =
        DAG.getNode(ARMISD::VQMOVNs, DL, HalfVT, DAG.getUNDEF(HalfVT),
                    N0->getOperand(0), DAG.getConstant(0, DL, MVT::i32));
    SDValue Bitcast = DAG.getNode(ARMISD::VECTOR_REG_CAST, DL, VT, VQMOVN);
    return DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, VT, Bitcast,
                       DAG.getValueType(ExtVT));
  }

  auto IsUnsignedSaturate = [&](SDNode *Min) {
    // For unsigned, we just need to check for <= 0xffff
    if (Min->getOpcode() != ISD::UMIN)
      return false;

    APInt SaturateC;
    if (VT == MVT::v4i32)
      SaturateC = APInt(32, (1 << 16) - 1, true);
    else //if (VT == MVT::v8i16)
      SaturateC = APInt(16, (1 << 8) - 1, true);

    APInt MinC;
    if (!ISD::isConstantSplatVector(Min->getOperand(1).getNode(), MinC) ||
        MinC != SaturateC)
      return false;
    return true;
  };

  if (IsUnsignedSaturate(N)) {
    SDLoc DL(N);
    MVT HalfVT;
    unsigned ExtConst;
    if (VT == MVT::v4i32) {
      HalfVT = MVT::v8i16;
      ExtConst = 0x0000FFFF;
    } else { //if (VT == MVT::v8i16)
      HalfVT = MVT::v16i8;
      ExtConst = 0x00FF;
    }

    // Create a VQMOVNB with undef top lanes, then ZExt into the top half with
    // an AND. That extend will hopefully be removed if only the bottom bits are
    // demanded (though a truncating store, for example).
    SDValue VQMOVN =
        DAG.getNode(ARMISD::VQMOVNu, DL, HalfVT, DAG.getUNDEF(HalfVT), N0,
                    DAG.getConstant(0, DL, MVT::i32));
    SDValue Bitcast = DAG.getNode(ARMISD::VECTOR_REG_CAST, DL, VT, VQMOVN);
    return DAG.getNode(ISD::AND, DL, VT, Bitcast,
                       DAG.getConstant(ExtConst, DL, VT));
  }

  return SDValue();
}

static const APInt *isPowerOf2Constant(SDValue V) {
  ConstantSDNode *C = dyn_cast<ConstantSDNode>(V);
  if (!C)
    return nullptr;
  const APInt *CV = &C->getAPIntValue();
  return CV->isPowerOf2() ? CV : nullptr;
}

SDValue ARMTargetLowering::PerformCMOVToBFICombine(SDNode *CMOV, SelectionDAG &DAG) const {
  // If we have a CMOV, OR and AND combination such as:
  //   if (x & CN)
  //     y |= CM;
  //
  // And:
  //   * CN is a single bit;
  //   * All bits covered by CM are known zero in y
  //
  // Then we can convert this into a sequence of BFI instructions. This will
  // always be a win if CM is a single bit, will always be no worse than the
  // TST&OR sequence if CM is two bits, and for thumb will be no worse if CM is
  // three bits (due to the extra IT instruction).

  SDValue Op0 = CMOV->getOperand(0);
  SDValue Op1 = CMOV->getOperand(1);
  auto CC = CMOV->getConstantOperandAPInt(2).getLimitedValue();
  SDValue CmpZ = CMOV->getOperand(4);

  // The compare must be against zero.
  if (!isNullConstant(CmpZ->getOperand(1)))
    return SDValue();

  assert(CmpZ->getOpcode() == ARMISD::CMPZ);
  SDValue And = CmpZ->getOperand(0);
  if (And->getOpcode() != ISD::AND)
    return SDValue();
  const APInt *AndC = isPowerOf2Constant(And->getOperand(1));
  if (!AndC)
    return SDValue();
  SDValue X = And->getOperand(0);

  if (CC == ARMCC::EQ) {
    // We're performing an "equal to zero" compare. Swap the operands so we
    // canonicalize on a "not equal to zero" compare.
    std::swap(Op0, Op1);
  } else {
    assert(CC == ARMCC::NE && "How can a CMPZ node not be EQ or NE?");
  }

  if (Op1->getOpcode() != ISD::OR)
    return SDValue();

  ConstantSDNode *OrC = dyn_cast<ConstantSDNode>(Op1->getOperand(1));
  if (!OrC)
    return SDValue();
  SDValue Y = Op1->getOperand(0);

  if (Op0 != Y)
    return SDValue();

  // Now, is it profitable to continue?
  APInt OrCI = OrC->getAPIntValue();
  unsigned Heuristic = Subtarget->isThumb() ? 3 : 2;
  if (OrCI.popcount() > Heuristic)
    return SDValue();

  // Lastly, can we determine that the bits defined by OrCI
  // are zero in Y?
  KnownBits Known = DAG.computeKnownBits(Y);
  if ((OrCI & Known.Zero) != OrCI)
    return SDValue();

  // OK, we can do the combine.
  SDValue V = Y;
  SDLoc dl(X);
  EVT VT = X.getValueType();
  unsigned BitInX = AndC->logBase2();

  if (BitInX != 0) {
    // We must shift X first.
    X = DAG.getNode(ISD::SRL, dl, VT, X,
                    DAG.getConstant(BitInX, dl, VT));
  }

  for (unsigned BitInY = 0, NumActiveBits = OrCI.getActiveBits();
       BitInY < NumActiveBits; ++BitInY) {
    if (OrCI[BitInY] == 0)
      continue;
    APInt Mask(VT.getSizeInBits(), 0);
    Mask.setBit(BitInY);
    V = DAG.getNode(ARMISD::BFI, dl, VT, V, X,
                    // Confusingly, the operand is an *inverted* mask.
                    DAG.getConstant(~Mask, dl, VT));
  }

  return V;
}

// Given N, the value controlling the conditional branch, search for the loop
// intrinsic, returning it, along with how the value is used. We need to handle
// patterns such as the following:
// (brcond (xor (setcc (loop.decrement), 0, ne), 1), exit)
// (brcond (setcc (loop.decrement), 0, eq), exit)
// (brcond (setcc (loop.decrement), 0, ne), header)
static SDValue SearchLoopIntrinsic(SDValue N, ISD::CondCode &CC, int &Imm,
                                   bool &Negate) {
  switch (N->getOpcode()) {
  default:
    break;
  case ISD::XOR: {
    if (!isa<ConstantSDNode>(N.getOperand(1)))
      return SDValue();
    if (!cast<ConstantSDNode>(N.getOperand(1))->isOne())
      return SDValue();
    Negate = !Negate;
    return SearchLoopIntrinsic(N.getOperand(0), CC, Imm, Negate);
  }
  case ISD::SETCC: {
    auto *Const = dyn_cast<ConstantSDNode>(N.getOperand(1));
    if (!Const)
      return SDValue();
    if (Const->isZero())
      Imm = 0;
    else if (Const->isOne())
      Imm = 1;
    else
      return SDValue();
    CC = cast<CondCodeSDNode>(N.getOperand(2))->get();
    return SearchLoopIntrinsic(N->getOperand(0), CC, Imm, Negate);
  }
  case ISD::INTRINSIC_W_CHAIN: {
    unsigned IntOp = N.getConstantOperandVal(1);
    if (IntOp != Intrinsic::test_start_loop_iterations &&
        IntOp != Intrinsic::loop_decrement_reg)
      return SDValue();
    return N;
  }
  }
  return SDValue();
}

static SDValue PerformHWLoopCombine(SDNode *N,
                                    TargetLowering::DAGCombinerInfo &DCI,
                                    const ARMSubtarget *ST) {

  // The hwloop intrinsics that we're interested are used for control-flow,
  // either for entering or exiting the loop:
  // - test.start.loop.iterations will test whether its operand is zero. If it
  //   is zero, the proceeding branch should not enter the loop.
  // - loop.decrement.reg also tests whether its operand is zero. If it is
  //   zero, the proceeding branch should not branch back to the beginning of
  //   the loop.
  // So here, we need to check that how the brcond is using the result of each
  // of the intrinsics to ensure that we're branching to the right place at the
  // right time.

  ISD::CondCode CC;
  SDValue Cond;
  int Imm = 1;
  bool Negate = false;
  SDValue Chain = N->getOperand(0);
  SDValue Dest;

  if (N->getOpcode() == ISD::BRCOND) {
    CC = ISD::SETEQ;
    Cond = N->getOperand(1);
    Dest = N->getOperand(2);
  } else {
    assert(N->getOpcode() == ISD::BR_CC && "Expected BRCOND or BR_CC!");
    CC = cast<CondCodeSDNode>(N->getOperand(1))->get();
    Cond = N->getOperand(2);
    Dest = N->getOperand(4);
    if (auto *Const = dyn_cast<ConstantSDNode>(N->getOperand(3))) {
      if (!Const->isOne() && !Const->isZero())
        return SDValue();
      Imm = Const->getZExtValue();
    } else
      return SDValue();
  }

  SDValue Int = SearchLoopIntrinsic(Cond, CC, Imm, Negate);
  if (!Int)
    return SDValue();

  if (Negate)
    CC = ISD::getSetCCInverse(CC, /* Integer inverse */ MVT::i32);

  auto IsTrueIfZero = [](ISD::CondCode CC, int Imm) {
    return (CC == ISD::SETEQ && Imm == 0) ||
           (CC == ISD::SETNE && Imm == 1) ||
           (CC == ISD::SETLT && Imm == 1) ||
           (CC == ISD::SETULT && Imm == 1);
  };

  auto IsFalseIfZero = [](ISD::CondCode CC, int Imm) {
    return (CC == ISD::SETEQ && Imm == 1) ||
           (CC == ISD::SETNE && Imm == 0) ||
           (CC == ISD::SETGT && Imm == 0) ||
           (CC == ISD::SETUGT && Imm == 0) ||
           (CC == ISD::SETGE && Imm == 1) ||
           (CC == ISD::SETUGE && Imm == 1);
  };

  assert((IsTrueIfZero(CC, Imm) || IsFalseIfZero(CC, Imm)) &&
         "unsupported condition");

  SDLoc dl(Int);
  SelectionDAG &DAG = DCI.DAG;
  SDValue Elements = Int.getOperand(2);
  unsigned IntOp = Int->getConstantOperandVal(1);
  assert((N->hasOneUse() && N->use_begin()->getOpcode() == ISD::BR)
          && "expected single br user");
  SDNode *Br = *N->use_begin();
  SDValue OtherTarget = Br->getOperand(1);

  // Update the unconditional branch to branch to the given Dest.
  auto UpdateUncondBr = [](SDNode *Br, SDValue Dest, SelectionDAG &DAG) {
    SDValue NewBrOps[] = { Br->getOperand(0), Dest };
    SDValue NewBr = DAG.getNode(ISD::BR, SDLoc(Br), MVT::Other, NewBrOps);
    DAG.ReplaceAllUsesOfValueWith(SDValue(Br, 0), NewBr);
  };

  if (IntOp == Intrinsic::test_start_loop_iterations) {
    SDValue Res;
    SDValue Setup = DAG.getNode(ARMISD::WLSSETUP, dl, MVT::i32, Elements);
    // We expect this 'instruction' to branch when the counter is zero.
    if (IsTrueIfZero(CC, Imm)) {
      SDValue Ops[] = {Chain, Setup, Dest};
      Res = DAG.getNode(ARMISD::WLS, dl, MVT::Other, Ops);
    } else {
      // The logic is the reverse of what we need for WLS, so find the other
      // basic block target: the target of the proceeding br.
      UpdateUncondBr(Br, Dest, DAG);

      SDValue Ops[] = {Chain, Setup, OtherTarget};
      Res = DAG.getNode(ARMISD::WLS, dl, MVT::Other, Ops);
    }
    // Update LR count to the new value
    DAG.ReplaceAllUsesOfValueWith(Int.getValue(0), Setup);
    // Update chain
    DAG.ReplaceAllUsesOfValueWith(Int.getValue(2), Int.getOperand(0));
    return Res;
  } else {
    SDValue Size =
        DAG.getTargetConstant(Int.getConstantOperandVal(3), dl, MVT::i32);
    SDValue Args[] = { Int.getOperand(0), Elements, Size, };
    SDValue LoopDec = DAG.getNode(ARMISD::LOOP_DEC, dl,
                                  DAG.getVTList(MVT::i32, MVT::Other), Args);
    DAG.ReplaceAllUsesWith(Int.getNode(), LoopDec.getNode());

    // We expect this instruction to branch when the count is not zero.
    SDValue Target = IsFalseIfZero(CC, Imm) ? Dest : OtherTarget;

    // Update the unconditional branch to target the loop preheader if we've
    // found the condition has been reversed.
    if (Target == OtherTarget)
      UpdateUncondBr(Br, Dest, DAG);

    Chain = DAG.getNode(ISD::TokenFactor, dl, MVT::Other,
                        SDValue(LoopDec.getNode(), 1), Chain);

    SDValue EndArgs[] = { Chain, SDValue(LoopDec.getNode(), 0), Target };
    return DAG.getNode(ARMISD::LE, dl, MVT::Other, EndArgs);
  }
  return SDValue();
}

/// PerformBRCONDCombine - Target-specific DAG combining for ARMISD::BRCOND.
SDValue
ARMTargetLowering::PerformBRCONDCombine(SDNode *N, SelectionDAG &DAG) const {
  SDValue Cmp = N->getOperand(4);
  if (Cmp.getOpcode() != ARMISD::CMPZ)
    // Only looking at NE cases.
    return SDValue();

  EVT VT = N->getValueType(0);
  SDLoc dl(N);
  SDValue LHS = Cmp.getOperand(0);
  SDValue RHS = Cmp.getOperand(1);
  SDValue Chain = N->getOperand(0);
  SDValue BB = N->getOperand(1);
  SDValue ARMcc = N->getOperand(2);
  ARMCC::CondCodes CC = (ARMCC::CondCodes)ARMcc->getAsZExtVal();

  // (brcond Chain BB ne CPSR (cmpz (and (cmov 0 1 CC CPSR Cmp) 1) 0))
  // -> (brcond Chain BB CC CPSR Cmp)
  if (CC == ARMCC::NE && LHS.getOpcode() == ISD::AND && LHS->hasOneUse() &&
      LHS->getOperand(0)->getOpcode() == ARMISD::CMOV &&
      LHS->getOperand(0)->hasOneUse() &&
      isNullConstant(LHS->getOperand(0)->getOperand(0)) &&
      isOneConstant(LHS->getOperand(0)->getOperand(1)) &&
      isOneConstant(LHS->getOperand(1)) && isNullConstant(RHS)) {
    return DAG.getNode(
        ARMISD::BRCOND, dl, VT, Chain, BB, LHS->getOperand(0)->getOperand(2),
        LHS->getOperand(0)->getOperand(3), LHS->getOperand(0)->getOperand(4));
  }

  return SDValue();
}

/// PerformCMOVCombine - Target-specific DAG combining for ARMISD::CMOV.
SDValue
ARMTargetLowering::PerformCMOVCombine(SDNode *N, SelectionDAG &DAG) const {
  SDValue Cmp = N->getOperand(4);
  if (Cmp.getOpcode() != ARMISD::CMPZ)
    // Only looking at EQ and NE cases.
    return SDValue();

  EVT VT = N->getValueType(0);
  SDLoc dl(N);
  SDValue LHS = Cmp.getOperand(0);
  SDValue RHS = Cmp.getOperand(1);
  SDValue FalseVal = N->getOperand(0);
  SDValue TrueVal = N->getOperand(1);
  SDValue ARMcc = N->getOperand(2);
  ARMCC::CondCodes CC = (ARMCC::CondCodes)ARMcc->getAsZExtVal();

  // BFI is only available on V6T2+.
  if (!Subtarget->isThumb1Only() && Subtarget->hasV6T2Ops()) {
    SDValue R = PerformCMOVToBFICombine(N, DAG);
    if (R)
      return R;
  }

  // Simplify
  //   mov     r1, r0
  //   cmp     r1, x
  //   mov     r0, y
  //   moveq   r0, x
  // to
  //   cmp     r0, x
  //   movne   r0, y
  //
  //   mov     r1, r0
  //   cmp     r1, x
  //   mov     r0, x
  //   movne   r0, y
  // to
  //   cmp     r0, x
  //   movne   r0, y
  /// FIXME: Turn this into a target neutral optimization?
  SDValue Res;
  if (CC == ARMCC::NE && FalseVal == RHS && FalseVal != LHS) {
    Res = DAG.getNode(ARMISD::CMOV, dl, VT, LHS, TrueVal, ARMcc,
                      N->getOperand(3), Cmp);
  } else if (CC == ARMCC::EQ && TrueVal == RHS) {
    SDValue ARMcc;
    SDValue NewCmp = getARMCmp(LHS, RHS, ISD::SETNE, ARMcc, DAG, dl);
    Res = DAG.getNode(ARMISD::CMOV, dl, VT, LHS, FalseVal, ARMcc,
                      N->getOperand(3), NewCmp);
  }

  // (cmov F T ne CPSR (cmpz (cmov 0 1 CC CPSR Cmp) 0))
  // -> (cmov F T CC CPSR Cmp)
  if (CC == ARMCC::NE && LHS.getOpcode() == ARMISD::CMOV && LHS->hasOneUse() &&
      isNullConstant(LHS->getOperand(0)) && isOneConstant(LHS->getOperand(1)) &&
      isNullConstant(RHS)) {
    return DAG.getNode(ARMISD::CMOV, dl, VT, FalseVal, TrueVal,
                       LHS->getOperand(2), LHS->getOperand(3),
                       LHS->getOperand(4));
  }

  if (!VT.isInteger())
      return SDValue();

  // Fold away an unneccessary CMPZ/CMOV
  // CMOV A, B, C1, $cpsr, (CMPZ (CMOV 1, 0, C2, D), 0) ->
  // if C1==EQ -> CMOV A, B, C2, $cpsr, D
  // if C1==NE -> CMOV A, B, NOT(C2), $cpsr, D
  if (N->getConstantOperandVal(2) == ARMCC::EQ ||
      N->getConstantOperandVal(2) == ARMCC::NE) {
    ARMCC::CondCodes Cond;
    if (SDValue C = IsCMPZCSINC(N->getOperand(4).getNode(), Cond)) {
      if (N->getConstantOperandVal(2) == ARMCC::NE)
        Cond = ARMCC::getOppositeCondition(Cond);
      return DAG.getNode(N->getOpcode(), SDLoc(N), MVT::i32, N->getOperand(0),
                         N->getOperand(1),
                         DAG.getTargetConstant(Cond, SDLoc(N), MVT::i32),
                         N->getOperand(3), C);
    }
  }

  // Materialize a boolean comparison for integers so we can avoid branching.
  if (isNullConstant(FalseVal)) {
    if (CC == ARMCC::EQ && isOneConstant(TrueVal)) {
      if (!Subtarget->isThumb1Only() && Subtarget->hasV5TOps()) {
        // If x == y then x - y == 0 and ARM's CLZ will return 32, shifting it
        // right 5 bits will make that 32 be 1, otherwise it will be 0.
        // CMOV 0, 1, ==, (CMPZ x, y) -> SRL (CTLZ (SUB x, y)), 5
        SDValue Sub = DAG.getNode(ISD::SUB, dl, VT, LHS, RHS);
        Res = DAG.getNode(ISD::SRL, dl, VT, DAG.getNode(ISD::CTLZ, dl, VT, Sub),
                          DAG.getConstant(5, dl, MVT::i32));
      } else {
        // CMOV 0, 1, ==, (CMPZ x, y) ->
        //     (UADDO_CARRY (SUB x, y), t:0, t:1)
        // where t = (USUBO_CARRY 0, (SUB x, y), 0)
        //
        // The USUBO_CARRY computes 0 - (x - y) and this will give a borrow when
        // x != y. In other words, a carry C == 1 when x == y, C == 0
        // otherwise.
        // The final UADDO_CARRY computes
        //     x - y + (0 - (x - y)) + C == C
        SDValue Sub = DAG.getNode(ISD::SUB, dl, VT, LHS, RHS);
        SDVTList VTs = DAG.getVTList(VT, MVT::i32);
        SDValue Neg = DAG.getNode(ISD::USUBO, dl, VTs, FalseVal, Sub);
        // ISD::USUBO_CARRY returns a borrow but we want the carry here
        // actually.
        SDValue Carry =
            DAG.getNode(ISD::SUB, dl, MVT::i32,
                        DAG.getConstant(1, dl, MVT::i32), Neg.getValue(1));
        Res = DAG.getNode(ISD::UADDO_CARRY, dl, VTs, Sub, Neg, Carry);
      }
    } else if (CC == ARMCC::NE && !isNullConstant(RHS) &&
               (!Subtarget->isThumb1Only() || isPowerOf2Constant(TrueVal))) {
      // This seems pointless but will allow us to combine it further below.
      // CMOV 0, z, !=, (CMPZ x, y) -> CMOV (SUBC x, y), z, !=, (SUBC x, y):1
      SDValue Sub =
          DAG.getNode(ARMISD::SUBC, dl, DAG.getVTList(VT, MVT::i32), LHS, RHS);
      SDValue CPSRGlue = DAG.getCopyToReg(DAG.getEntryNode(), dl, ARM::CPSR,
                                          Sub.getValue(1), SDValue());
      Res = DAG.getNode(ARMISD::CMOV, dl, VT, Sub, TrueVal, ARMcc,
                        N->getOperand(3), CPSRGlue.getValue(1));
      FalseVal = Sub;
    }
  } else if (isNullConstant(TrueVal)) {
    if (CC == ARMCC::EQ && !isNullConstant(RHS) &&
        (!Subtarget->isThumb1Only() || isPowerOf2Constant(FalseVal))) {
      // This seems pointless but will allow us to combine it further below
      // Note that we change == for != as this is the dual for the case above.
      // CMOV z, 0, ==, (CMPZ x, y) -> CMOV (SUBC x, y), z, !=, (SUBC x, y):1
      SDValue Sub =
          DAG.getNode(ARMISD::SUBC, dl, DAG.getVTList(VT, MVT::i32), LHS, RHS);
      SDValue CPSRGlue = DAG.getCopyToReg(DAG.getEntryNode(), dl, ARM::CPSR,
                                          Sub.getValue(1), SDValue());
      Res = DAG.getNode(ARMISD::CMOV, dl, VT, Sub, FalseVal,
                        DAG.getConstant(ARMCC::NE, dl, MVT::i32),
                        N->getOperand(3), CPSRGlue.getValue(1));
      FalseVal = Sub;
    }
  }

  // On Thumb1, the DAG above may be further combined if z is a power of 2
  // (z == 2 ^ K).
  // CMOV (SUBC x, y), z, !=, (SUBC x, y):1 ->
  // t1 = (USUBO (SUB x, y), 1)
  // t2 = (USUBO_CARRY (SUB x, y), t1:0, t1:1)
  // Result = if K != 0 then (SHL t2:0, K) else t2:0
  //
  // This also handles the special case of comparing against zero; it's
  // essentially, the same pattern, except there's no SUBC:
  // CMOV x, z, !=, (CMPZ x, 0) ->
  // t1 = (USUBO x, 1)
  // t2 = (USUBO_CARRY x, t1:0, t1:1)
  // Result = if K != 0 then (SHL t2:0, K) else t2:0
  const APInt *TrueConst;
  if (Subtarget->isThumb1Only() && CC == ARMCC::NE &&
      ((FalseVal.getOpcode() == ARMISD::SUBC && FalseVal.getOperand(0) == LHS &&
        FalseVal.getOperand(1) == RHS) ||
       (FalseVal == LHS && isNullConstant(RHS))) &&
      (TrueConst = isPowerOf2Constant(TrueVal))) {
    SDVTList VTs = DAG.getVTList(VT, MVT::i32);
    unsigned ShiftAmount = TrueConst->logBase2();
    if (ShiftAmount)
      TrueVal = DAG.getConstant(1, dl, VT);
    SDValue Subc = DAG.getNode(ISD::USUBO, dl, VTs, FalseVal, TrueVal);
    Res = DAG.getNode(ISD::USUBO_CARRY, dl, VTs, FalseVal, Subc,
                      Subc.getValue(1));

    if (ShiftAmount)
      Res = DAG.getNode(ISD::SHL, dl, VT, Res,
                        DAG.getConstant(ShiftAmount, dl, MVT::i32));
  }

  if (Res.getNode()) {
    KnownBits Known = DAG.computeKnownBits(SDValue(N,0));
    // Capture demanded bits information that would be otherwise lost.
    if (Known.Zero == 0xfffffffe)
      Res = DAG.getNode(ISD::AssertZext, dl, MVT::i32, Res,
                        DAG.getValueType(MVT::i1));
    else if (Known.Zero == 0xffffff00)
      Res = DAG.getNode(ISD::AssertZext, dl, MVT::i32, Res,
                        DAG.getValueType(MVT::i8));
    else if (Known.Zero == 0xffff0000)
      Res = DAG.getNode(ISD::AssertZext, dl, MVT::i32, Res,
                        DAG.getValueType(MVT::i16));
  }

  return Res;
}

static SDValue PerformBITCASTCombine(SDNode *N,
                                     TargetLowering::DAGCombinerInfo &DCI,
                                     const ARMSubtarget *ST) {
  SelectionDAG &DAG = DCI.DAG;
  SDValue Src = N->getOperand(0);
  EVT DstVT = N->getValueType(0);

  // Convert v4f32 bitcast (v4i32 vdup (i32)) -> v4f32 vdup (i32) under MVE.
  if (ST->hasMVEIntegerOps() && Src.getOpcode() == ARMISD::VDUP) {
    EVT SrcVT = Src.getValueType();
    if (SrcVT.getScalarSizeInBits() == DstVT.getScalarSizeInBits())
      return DAG.getNode(ARMISD::VDUP, SDLoc(N), DstVT, Src.getOperand(0));
  }

  // We may have a bitcast of something that has already had this bitcast
  // combine performed on it, so skip past any VECTOR_REG_CASTs.
  while (Src.getOpcode() == ARMISD::VECTOR_REG_CAST)
    Src = Src.getOperand(0);

  // Bitcast from element-wise VMOV or VMVN doesn't need VREV if the VREV that
  // would be generated is at least the width of the element type.
  EVT SrcVT = Src.getValueType();
  if ((Src.getOpcode() == ARMISD::VMOVIMM ||
       Src.getOpcode() == ARMISD::VMVNIMM ||
       Src.getOpcode() == ARMISD::VMOVFPIMM) &&
      SrcVT.getScalarSizeInBits() <= DstVT.getScalarSizeInBits() &&
      DAG.getDataLayout().isBigEndian())
    return DAG.getNode(ARMISD::VECTOR_REG_CAST, SDLoc(N), DstVT, Src);

  // bitcast(extract(x, n)); bitcast(extract(x, n+1))  ->  VMOVRRD x
  if (SDValue R = PerformExtractEltToVMOVRRD(N, DCI))
    return R;

  return SDValue();
}

// Some combines for the MVETrunc truncations legalizer helper. Also lowers the
// node into stack operations after legalizeOps.
SDValue ARMTargetLowering::PerformMVETruncCombine(
    SDNode *N, TargetLowering::DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  EVT VT = N->getValueType(0);
  SDLoc DL(N);

  // MVETrunc(Undef, Undef) -> Undef
  if (all_of(N->ops(), [](SDValue Op) { return Op.isUndef(); }))
    return DAG.getUNDEF(VT);

  // MVETrunc(MVETrunc a b, MVETrunc c, d) -> MVETrunc
  if (N->getNumOperands() == 2 &&
      N->getOperand(0).getOpcode() == ARMISD::MVETRUNC &&
      N->getOperand(1).getOpcode() == ARMISD::MVETRUNC)
    return DAG.getNode(ARMISD::MVETRUNC, DL, VT, N->getOperand(0).getOperand(0),
                       N->getOperand(0).getOperand(1),
                       N->getOperand(1).getOperand(0),
                       N->getOperand(1).getOperand(1));

  // MVETrunc(shuffle, shuffle) -> VMOVN
  if (N->getNumOperands() == 2 &&
      N->getOperand(0).getOpcode() == ISD::VECTOR_SHUFFLE &&
      N->getOperand(1).getOpcode() == ISD::VECTOR_SHUFFLE) {
    auto *S0 = cast<ShuffleVectorSDNode>(N->getOperand(0).getNode());
    auto *S1 = cast<ShuffleVectorSDNode>(N->getOperand(1).getNode());

    if (S0->getOperand(0) == S1->getOperand(0) &&
        S0->getOperand(1) == S1->getOperand(1)) {
      // Construct complete shuffle mask
      SmallVector<int, 8> Mask(S0->getMask());
      Mask.append(S1->getMask().begin(), S1->getMask().end());

      if (isVMOVNTruncMask(Mask, VT, false))
        return DAG.getNode(
            ARMISD::VMOVN, DL, VT,
            DAG.getNode(ARMISD::VECTOR_REG_CAST, DL, VT, S0->getOperand(0)),
            DAG.getNode(ARMISD::VECTOR_REG_CAST, DL, VT, S0->getOperand(1)),
            DAG.getConstant(1, DL, MVT::i32));
      if (isVMOVNTruncMask(Mask, VT, true))
        return DAG.getNode(
            ARMISD::VMOVN, DL, VT,
            DAG.getNode(ARMISD::VECTOR_REG_CAST, DL, VT, S0->getOperand(1)),
            DAG.getNode(ARMISD::VECTOR_REG_CAST, DL, VT, S0->getOperand(0)),
            DAG.getConstant(1, DL, MVT::i32));
    }
  }

  // For MVETrunc of a buildvector or shuffle, it can be beneficial to lower the
  // truncate to a buildvector to allow the generic optimisations to kick in.
  if (all_of(N->ops(), [](SDValue Op) {
        return Op.getOpcode() == ISD::BUILD_VECTOR ||
               Op.getOpcode() == ISD::VECTOR_SHUFFLE ||
               (Op.getOpcode() == ISD::BITCAST &&
                Op.getOperand(0).getOpcode() == ISD::BUILD_VECTOR);
      })) {
    SmallVector<SDValue, 8> Extracts;
    for (unsigned Op = 0; Op < N->getNumOperands(); Op++) {
      SDValue O = N->getOperand(Op);
      for (unsigned i = 0; i < O.getValueType().getVectorNumElements(); i++) {
        SDValue Ext = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::i32, O,
                                  DAG.getConstant(i, DL, MVT::i32));
        Extracts.push_back(Ext);
      }
    }
    return DAG.getBuildVector(VT, DL, Extracts);
  }

  // If we are late in the legalization process and nothing has optimised
  // the trunc to anything better, lower it to a stack store and reload,
  // performing the truncation whilst keeping the lanes in the correct order:
  //   VSTRH.32 a, stack; VSTRH.32 b, stack+8; VLDRW.32 stack;
  if (!DCI.isAfterLegalizeDAG())
    return SDValue();

  SDValue StackPtr = DAG.CreateStackTemporary(TypeSize::getFixed(16), Align(4));
  int SPFI = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();
  int NumIns = N->getNumOperands();
  assert((NumIns == 2 || NumIns == 4) &&
         "Expected 2 or 4 inputs to an MVETrunc");
  EVT StoreVT = VT.getHalfNumVectorElementsVT(*DAG.getContext());
  if (N->getNumOperands() == 4)
    StoreVT = StoreVT.getHalfNumVectorElementsVT(*DAG.getContext());

  SmallVector<SDValue> Chains;
  for (int I = 0; I < NumIns; I++) {
    SDValue Ptr = DAG.getNode(
        ISD::ADD, DL, StackPtr.getValueType(), StackPtr,
        DAG.getConstant(I * 16 / NumIns, DL, StackPtr.getValueType()));
    MachinePointerInfo MPI = MachinePointerInfo::getFixedStack(
        DAG.getMachineFunction(), SPFI, I * 16 / NumIns);
    SDValue Ch = DAG.getTruncStore(DAG.getEntryNode(), DL, N->getOperand(I),
                                   Ptr, MPI, StoreVT, Align(4));
    Chains.push_back(Ch);
  }

  SDValue Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Chains);
  MachinePointerInfo MPI =
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), SPFI, 0);
  return DAG.getLoad(VT, DL, Chain, StackPtr, MPI, Align(4));
}

// Take a MVEEXT(load x) and split that into (extload x, extload x+8)
static SDValue PerformSplittingMVEEXTToWideningLoad(SDNode *N,
                                                    SelectionDAG &DAG) {
  SDValue N0 = N->getOperand(0);
  LoadSDNode *LD = dyn_cast<LoadSDNode>(N0.getNode());
  if (!LD || !LD->isSimple() || !N0.hasOneUse() || LD->isIndexed())
    return SDValue();

  EVT FromVT = LD->getMemoryVT();
  EVT ToVT = N->getValueType(0);
  if (!ToVT.isVector())
    return SDValue();
  assert(FromVT.getVectorNumElements() == ToVT.getVectorNumElements() * 2);
  EVT ToEltVT = ToVT.getVectorElementType();
  EVT FromEltVT = FromVT.getVectorElementType();

  unsigned NumElements = 0;
  if (ToEltVT == MVT::i32 && (FromEltVT == MVT::i16 || FromEltVT == MVT::i8))
    NumElements = 4;
  if (ToEltVT == MVT::i16 && FromEltVT == MVT::i8)
    NumElements = 8;
  assert(NumElements != 0);

  ISD::LoadExtType NewExtType =
      N->getOpcode() == ARMISD::MVESEXT ? ISD::SEXTLOAD : ISD::ZEXTLOAD;
  if (LD->getExtensionType() != ISD::NON_EXTLOAD &&
      LD->getExtensionType() != ISD::EXTLOAD &&
      LD->getExtensionType() != NewExtType)
    return SDValue();

  LLVMContext &C = *DAG.getContext();
  SDLoc DL(LD);
  // Details about the old load
  SDValue Ch = LD->getChain();
  SDValue BasePtr = LD->getBasePtr();
  Align Alignment = LD->getOriginalAlign();
  MachineMemOperand::Flags MMOFlags = LD->getMemOperand()->getFlags();
  AAMDNodes AAInfo = LD->getAAInfo();

  SDValue Offset = DAG.getUNDEF(BasePtr.getValueType());
  EVT NewFromVT = EVT::getVectorVT(
      C, EVT::getIntegerVT(C, FromEltVT.getScalarSizeInBits()), NumElements);
  EVT NewToVT = EVT::getVectorVT(
      C, EVT::getIntegerVT(C, ToEltVT.getScalarSizeInBits()), NumElements);

  SmallVector<SDValue, 4> Loads;
  SmallVector<SDValue, 4> Chains;
  for (unsigned i = 0; i < FromVT.getVectorNumElements() / NumElements; i++) {
    unsigned NewOffset = (i * NewFromVT.getSizeInBits()) / 8;
    SDValue NewPtr =
        DAG.getObjectPtrOffset(DL, BasePtr, TypeSize::getFixed(NewOffset));

    SDValue NewLoad =
        DAG.getLoad(ISD::UNINDEXED, NewExtType, NewToVT, DL, Ch, NewPtr, Offset,
                    LD->getPointerInfo().getWithOffset(NewOffset), NewFromVT,
                    Alignment, MMOFlags, AAInfo);
    Loads.push_back(NewLoad);
    Chains.push_back(SDValue(NewLoad.getNode(), 1));
  }

  SDValue NewChain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Chains);
  DAG.ReplaceAllUsesOfValueWith(SDValue(LD, 1), NewChain);
  return DAG.getMergeValues(Loads, DL);
}

// Perform combines for MVEEXT. If it has not be optimized to anything better
// before lowering, it gets converted to stack store and extloads performing the
// extend whilst still keeping the same lane ordering.
SDValue ARMTargetLowering::PerformMVEExtCombine(
    SDNode *N, TargetLowering::DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  EVT VT = N->getValueType(0);
  SDLoc DL(N);
  assert(N->getNumValues() == 2 && "Expected MVEEXT with 2 elements");
  assert((VT == MVT::v4i32 || VT == MVT::v8i16) && "Unexpected MVEEXT type");

  EVT ExtVT = N->getOperand(0).getValueType().getHalfNumVectorElementsVT(
      *DAG.getContext());
  auto Extend = [&](SDValue V) {
    SDValue VVT = DAG.getNode(ARMISD::VECTOR_REG_CAST, DL, VT, V);
    return N->getOpcode() == ARMISD::MVESEXT
               ? DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, VT, VVT,
                             DAG.getValueType(ExtVT))
               : DAG.getZeroExtendInReg(VVT, DL, ExtVT);
  };

  // MVEEXT(VDUP) -> SIGN_EXTEND_INREG(VDUP)
  if (N->getOperand(0).getOpcode() == ARMISD::VDUP) {
    SDValue Ext = Extend(N->getOperand(0));
    return DAG.getMergeValues({Ext, Ext}, DL);
  }

  // MVEEXT(shuffle) -> SIGN_EXTEND_INREG/ZERO_EXTEND_INREG
  if (auto *SVN = dyn_cast<ShuffleVectorSDNode>(N->getOperand(0))) {
    ArrayRef<int> Mask = SVN->getMask();
    assert(Mask.size() == 2 * VT.getVectorNumElements());
    assert(Mask.size() == SVN->getValueType(0).getVectorNumElements());
    unsigned Rev = VT == MVT::v4i32 ? ARMISD::VREV32 : ARMISD::VREV16;
    SDValue Op0 = SVN->getOperand(0);
    SDValue Op1 = SVN->getOperand(1);

    auto CheckInregMask = [&](int Start, int Offset) {
      for (int Idx = 0, E = VT.getVectorNumElements(); Idx < E; ++Idx)
        if (Mask[Start + Idx] >= 0 && Mask[Start + Idx] != Idx * 2 + Offset)
          return false;
      return true;
    };
    SDValue V0 = SDValue(N, 0);
    SDValue V1 = SDValue(N, 1);
    if (CheckInregMask(0, 0))
      V0 = Extend(Op0);
    else if (CheckInregMask(0, 1))
      V0 = Extend(DAG.getNode(Rev, DL, SVN->getValueType(0), Op0));
    else if (CheckInregMask(0, Mask.size()))
      V0 = Extend(Op1);
    else if (CheckInregMask(0, Mask.size() + 1))
      V0 = Extend(DAG.getNode(Rev, DL, SVN->getValueType(0), Op1));

    if (CheckInregMask(VT.getVectorNumElements(), Mask.size()))
      V1 = Extend(Op1);
    else if (CheckInregMask(VT.getVectorNumElements(), Mask.size() + 1))
      V1 = Extend(DAG.getNode(Rev, DL, SVN->getValueType(0), Op1));
    else if (CheckInregMask(VT.getVectorNumElements(), 0))
      V1 = Extend(Op0);
    else if (CheckInregMask(VT.getVectorNumElements(), 1))
      V1 = Extend(DAG.getNode(Rev, DL, SVN->getValueType(0), Op0));

    if (V0.getNode() != N || V1.getNode() != N)
      return DAG.getMergeValues({V0, V1}, DL);
  }

  // MVEEXT(load) -> extload, extload
  if (N->getOperand(0)->getOpcode() == ISD::LOAD)
    if (SDValue L = PerformSplittingMVEEXTToWideningLoad(N, DAG))
      return L;

  if (!DCI.isAfterLegalizeDAG())
    return SDValue();

  // Lower to a stack store and reload:
  //  VSTRW.32 a, stack; VLDRH.32 stack; VLDRH.32 stack+8;
  SDValue StackPtr = DAG.CreateStackTemporary(TypeSize::getFixed(16), Align(4));
  int SPFI = cast<FrameIndexSDNode>(StackPtr.getNode())->getIndex();
  int NumOuts = N->getNumValues();
  assert((NumOuts == 2 || NumOuts == 4) &&
         "Expected 2 or 4 outputs to an MVEEXT");
  EVT LoadVT = N->getOperand(0).getValueType().getHalfNumVectorElementsVT(
      *DAG.getContext());
  if (N->getNumOperands() == 4)
    LoadVT = LoadVT.getHalfNumVectorElementsVT(*DAG.getContext());

  MachinePointerInfo MPI =
      MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), SPFI, 0);
  SDValue Chain = DAG.getStore(DAG.getEntryNode(), DL, N->getOperand(0),
                               StackPtr, MPI, Align(4));

  SmallVector<SDValue> Loads;
  for (int I = 0; I < NumOuts; I++) {
    SDValue Ptr = DAG.getNode(
        ISD::ADD, DL, StackPtr.getValueType(), StackPtr,
        DAG.getConstant(I * 16 / NumOuts, DL, StackPtr.getValueType()));
    MachinePointerInfo MPI = MachinePointerInfo::getFixedStack(
        DAG.getMachineFunction(), SPFI, I * 16 / NumOuts);
    SDValue Load = DAG.getExtLoad(
        N->getOpcode() == ARMISD::MVESEXT ? ISD::SEXTLOAD : ISD::ZEXTLOAD, DL,
        VT, Chain, Ptr, MPI, LoadVT, Align(4));
    Loads.push_back(Load);
  }

  return DAG.getMergeValues(Loads, DL);
}

SDValue ARMTargetLowering::PerformDAGCombine(SDNode *N,
                                             DAGCombinerInfo &DCI) const {
  switch (N->getOpcode()) {
  default: break;
  case ISD::SELECT_CC:
  case ISD::SELECT:     return PerformSELECTCombine(N, DCI, Subtarget);
  case ISD::VSELECT:    return PerformVSELECTCombine(N, DCI, Subtarget);
  case ISD::SETCC:      return PerformVSetCCToVCTPCombine(N, DCI, Subtarget);
  case ARMISD::ADDE:    return PerformADDECombine(N, DCI, Subtarget);
  case ARMISD::UMLAL:   return PerformUMLALCombine(N, DCI.DAG, Subtarget);
  case ISD::ADD:        return PerformADDCombine(N, DCI, Subtarget);
  case ISD::SUB:        return PerformSUBCombine(N, DCI, Subtarget);
  case ISD::MUL:        return PerformMULCombine(N, DCI, Subtarget);
  case ISD::OR:         return PerformORCombine(N, DCI, Subtarget);
  case ISD::XOR:        return PerformXORCombine(N, DCI, Subtarget);
  case ISD::AND:        return PerformANDCombine(N, DCI, Subtarget);
  case ISD::BRCOND:
  case ISD::BR_CC:      return PerformHWLoopCombine(N, DCI, Subtarget);
  case ARMISD::ADDC:
  case ARMISD::SUBC:    return PerformAddcSubcCombine(N, DCI, Subtarget);
  case ARMISD::SUBE:    return PerformAddeSubeCombine(N, DCI, Subtarget);
  case ARMISD::BFI:     return PerformBFICombine(N, DCI.DAG);
  case ARMISD::VMOVRRD: return PerformVMOVRRDCombine(N, DCI, Subtarget);
  case ARMISD::VMOVDRR: return PerformVMOVDRRCombine(N, DCI.DAG);
  case ARMISD::VMOVhr:  return PerformVMOVhrCombine(N, DCI);
  case ARMISD::VMOVrh:  return PerformVMOVrhCombine(N, DCI.DAG);
  case ISD::STORE:      return PerformSTORECombine(N, DCI, Subtarget);
  case ISD::BUILD_VECTOR: return PerformBUILD_VECTORCombine(N, DCI, Subtarget);
  case ISD::INSERT_VECTOR_ELT: return PerformInsertEltCombine(N, DCI);
  case ISD::EXTRACT_VECTOR_ELT:
    return PerformExtractEltCombine(N, DCI, Subtarget);
  case ISD::SIGN_EXTEND_INREG: return PerformSignExtendInregCombine(N, DCI.DAG);
  case ISD::INSERT_SUBVECTOR: return PerformInsertSubvectorCombine(N, DCI);
  case ISD::VECTOR_SHUFFLE: return PerformVECTOR_SHUFFLECombine(N, DCI.DAG);
  case ARMISD::VDUPLANE: return PerformVDUPLANECombine(N, DCI, Subtarget);
  case ARMISD::VDUP: return PerformVDUPCombine(N, DCI.DAG, Subtarget);
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
    return PerformVCVTCombine(N, DCI.DAG, Subtarget);
  case ISD::FADD:
    return PerformFADDCombine(N, DCI.DAG, Subtarget);
  case ISD::FMUL:
    return PerformVMulVCTPCombine(N, DCI.DAG, Subtarget);
  case ISD::INTRINSIC_WO_CHAIN:
    return PerformIntrinsicCombine(N, DCI);
  case ISD::SHL:
  case ISD::SRA:
  case ISD::SRL:
    return PerformShiftCombine(N, DCI, Subtarget);
  case ISD::SIGN_EXTEND:
  case ISD::ZERO_EXTEND:
  case ISD::ANY_EXTEND:
    return PerformExtendCombine(N, DCI.DAG, Subtarget);
  case ISD::FP_EXTEND:
    return PerformFPExtendCombine(N, DCI.DAG, Subtarget);
  case ISD::SMIN:
  case ISD::UMIN:
  case ISD::SMAX:
  case ISD::UMAX:
    return PerformMinMaxCombine(N, DCI.DAG, Subtarget);
  case ARMISD::CMOV:
    return PerformCMOVCombine(N, DCI.DAG);
  case ARMISD::BRCOND:
    return PerformBRCONDCombine(N, DCI.DAG);
  case ARMISD::CMPZ:
    return PerformCMPZCombine(N, DCI.DAG);
  case ARMISD::CSINC:
  case ARMISD::CSINV:
  case ARMISD::CSNEG:
    return PerformCSETCombine(N, DCI.DAG);
  case ISD::LOAD:
    return PerformLOADCombine(N, DCI, Subtarget);
  case ARMISD::VLD1DUP:
  case ARMISD::VLD2DUP:
  case ARMISD::VLD3DUP:
  case ARMISD::VLD4DUP:
    return PerformVLDCombine(N, DCI);
  case ARMISD::BUILD_VECTOR:
    return PerformARMBUILD_VECTORCombine(N, DCI);
  case ISD::BITCAST:
    return PerformBITCASTCombine(N, DCI, Subtarget);
  case ARMISD::PREDICATE_CAST:
    return PerformPREDICATE_CASTCombine(N, DCI);
  case ARMISD::VECTOR_REG_CAST:
    return PerformVECTOR_REG_CASTCombine(N, DCI.DAG, Subtarget);
  case ARMISD::MVETRUNC:
    return PerformMVETruncCombine(N, DCI);
  case ARMISD::MVESEXT:
  case ARMISD::MVEZEXT:
    return PerformMVEExtCombine(N, DCI);
  case ARMISD::VCMP:
    return PerformVCMPCombine(N, DCI.DAG, Subtarget);
  case ISD::VECREDUCE_ADD:
    return PerformVECREDUCE_ADDCombine(N, DCI.DAG, Subtarget);
  case ARMISD::VADDVs:
  case ARMISD::VADDVu:
  case ARMISD::VADDLVs:
  case ARMISD::VADDLVu:
  case ARMISD::VADDLVAs:
  case ARMISD::VADDLVAu:
  case ARMISD::VMLAVs:
  case ARMISD::VMLAVu:
  case ARMISD::VMLALVs:
  case ARMISD::VMLALVu:
  case ARMISD::VMLALVAs:
  case ARMISD::VMLALVAu:
    return PerformReduceShuffleCombine(N, DCI.DAG);
  case ARMISD::VMOVN:
    return PerformVMOVNCombine(N, DCI);
  case ARMISD::VQMOVNs:
  case ARMISD::VQMOVNu:
    return PerformVQMOVNCombine(N, DCI);
  case ARMISD::VQDMULH:
    return PerformVQDMULHCombine(N, DCI);
  case ARMISD::ASRL:
  case ARMISD::LSRL:
  case ARMISD::LSLL:
    return PerformLongShiftCombine(N, DCI.DAG);
  case ARMISD::SMULWB: {
    unsigned BitWidth = N->getValueType(0).getSizeInBits();
    APInt DemandedMask = APInt::getLowBitsSet(BitWidth, 16);
    if (SimplifyDemandedBits(N->getOperand(1), DemandedMask, DCI))
      return SDValue();
    break;
  }
  case ARMISD::SMULWT: {
    unsigned BitWidth = N->getValueType(0).getSizeInBits();
    APInt DemandedMask = APInt::getHighBitsSet(BitWidth, 16);
    if (SimplifyDemandedBits(N->getOperand(1), DemandedMask, DCI))
      return SDValue();
    break;
  }
  case ARMISD::SMLALBB:
  case ARMISD::QADD16b:
  case ARMISD::QSUB16b:
  case ARMISD::UQADD16b:
  case ARMISD::UQSUB16b: {
    unsigned BitWidth = N->getValueType(0).getSizeInBits();
    APInt DemandedMask = APInt::getLowBitsSet(BitWidth, 16);
    if ((SimplifyDemandedBits(N->getOperand(0), DemandedMask, DCI)) ||
        (SimplifyDemandedBits(N->getOperand(1), DemandedMask, DCI)))
      return SDValue();
    break;
  }
  case ARMISD::SMLALBT: {
    unsigned LowWidth = N->getOperand(0).getValueType().getSizeInBits();
    APInt LowMask = APInt::getLowBitsSet(LowWidth, 16);
    unsigned HighWidth = N->getOperand(1).getValueType().getSizeInBits();
    APInt HighMask = APInt::getHighBitsSet(HighWidth, 16);
    if ((SimplifyDemandedBits(N->getOperand(0), LowMask, DCI)) ||
        (SimplifyDemandedBits(N->getOperand(1), HighMask, DCI)))
      return SDValue();
    break;
  }
  case ARMISD::SMLALTB: {
    unsigned HighWidth = N->getOperand(0).getValueType().getSizeInBits();
    APInt HighMask = APInt::getHighBitsSet(HighWidth, 16);
    unsigned LowWidth = N->getOperand(1).getValueType().getSizeInBits();
    APInt LowMask = APInt::getLowBitsSet(LowWidth, 16);
    if ((SimplifyDemandedBits(N->getOperand(0), HighMask, DCI)) ||
        (SimplifyDemandedBits(N->getOperand(1), LowMask, DCI)))
      return SDValue();
    break;
  }
  case ARMISD::SMLALTT: {
    unsigned BitWidth = N->getValueType(0).getSizeInBits();
    APInt DemandedMask = APInt::getHighBitsSet(BitWidth, 16);
    if ((SimplifyDemandedBits(N->getOperand(0), DemandedMask, DCI)) ||
        (SimplifyDemandedBits(N->getOperand(1), DemandedMask, DCI)))
      return SDValue();
    break;
  }
  case ARMISD::QADD8b:
  case ARMISD::QSUB8b:
  case ARMISD::UQADD8b:
  case ARMISD::UQSUB8b: {
    unsigned BitWidth = N->getValueType(0).getSizeInBits();
    APInt DemandedMask = APInt::getLowBitsSet(BitWidth, 8);
    if ((SimplifyDemandedBits(N->getOperand(0), DemandedMask, DCI)) ||
        (SimplifyDemandedBits(N->getOperand(1), DemandedMask, DCI)))
      return SDValue();
    break;
  }
  case ISD::INTRINSIC_VOID:
  case ISD::INTRINSIC_W_CHAIN:
    switch (N->getConstantOperandVal(1)) {
    case Intrinsic::arm_neon_vld1:
    case Intrinsic::arm_neon_vld1x2:
    case Intrinsic::arm_neon_vld1x3:
    case Intrinsic::arm_neon_vld1x4:
    case Intrinsic::arm_neon_vld2:
    case Intrinsic::arm_neon_vld3:
    case Intrinsic::arm_neon_vld4:
    case Intrinsic::arm_neon_vld2lane:
    case Intrinsic::arm_neon_vld3lane:
    case Intrinsic::arm_neon_vld4lane:
    case Intrinsic::arm_neon_vld2dup:
    case Intrinsic::arm_neon_vld3dup:
    case Intrinsic::arm_neon_vld4dup:
    case Intrinsic::arm_neon_vst1:
    case Intrinsic::arm_neon_vst1x2:
    case Intrinsic::arm_neon_vst1x3:
    case Intrinsic::arm_neon_vst1x4:
    case Intrinsic::arm_neon_vst2:
    case Intrinsic::arm_neon_vst3:
    case Intrinsic::arm_neon_vst4:
    case Intrinsic::arm_neon_vst2lane:
    case Intrinsic::arm_neon_vst3lane:
    case Intrinsic::arm_neon_vst4lane:
      return PerformVLDCombine(N, DCI);
    case Intrinsic::arm_mve_vld2q:
    case Intrinsic::arm_mve_vld4q:
    case Intrinsic::arm_mve_vst2q:
    case Intrinsic::arm_mve_vst4q:
      return PerformMVEVLDCombine(N, DCI);
    default: break;
    }
    break;
  }
  return SDValue();
}

bool ARMTargetLowering::isDesirableToTransformToIntegerOp(unsigned Opc,
                                                          EVT VT) const {
  return (VT == MVT::f32) && (Opc == ISD::LOAD || Opc == ISD::STORE);
}

bool ARMTargetLowering::allowsMisalignedMemoryAccesses(EVT VT, unsigned,
                                                       Align Alignment,
                                                       MachineMemOperand::Flags,
                                                       unsigned *Fast) const {
  // Depends what it gets converted into if the type is weird.
  if (!VT.isSimple())
    return false;

  // The AllowsUnaligned flag models the SCTLR.A setting in ARM cpus
  bool AllowsUnaligned = Subtarget->allowsUnalignedMem();
  auto Ty = VT.getSimpleVT().SimpleTy;

  if (Ty == MVT::i8 || Ty == MVT::i16 || Ty == MVT::i32) {
    // Unaligned access can use (for example) LRDB, LRDH, LDR
    if (AllowsUnaligned) {
      if (Fast)
        *Fast = Subtarget->hasV7Ops();
      return true;
    }
  }

  if (Ty == MVT::f64 || Ty == MVT::v2f64) {
    // For any little-endian targets with neon, we can support unaligned ld/st
    // of D and Q (e.g. {D0,D1}) registers by using vld1.i8/vst1.i8.
    // A big-endian target may also explicitly support unaligned accesses
    if (Subtarget->hasNEON() && (AllowsUnaligned || Subtarget->isLittle())) {
      if (Fast)
        *Fast = 1;
      return true;
    }
  }

  if (!Subtarget->hasMVEIntegerOps())
    return false;

  // These are for predicates
  if ((Ty == MVT::v16i1 || Ty == MVT::v8i1 || Ty == MVT::v4i1 ||
       Ty == MVT::v2i1)) {
    if (Fast)
      *Fast = 1;
    return true;
  }

  // These are for truncated stores/narrowing loads. They are fine so long as
  // the alignment is at least the size of the item being loaded
  if ((Ty == MVT::v4i8 || Ty == MVT::v8i8 || Ty == MVT::v4i16) &&
      Alignment >= VT.getScalarSizeInBits() / 8) {
    if (Fast)
      *Fast = true;
    return true;
  }

  // In little-endian MVE, the store instructions VSTRB.U8, VSTRH.U16 and
  // VSTRW.U32 all store the vector register in exactly the same format, and
  // differ only in the range of their immediate offset field and the required
  // alignment. So there is always a store that can be used, regardless of
  // actual type.
  //
  // For big endian, that is not the case. But can still emit a (VSTRB.U8;
  // VREV64.8) pair and get the same effect. This will likely be better than
  // aligning the vector through the stack.
  if (Ty == MVT::v16i8 || Ty == MVT::v8i16 || Ty == MVT::v8f16 ||
      Ty == MVT::v4i32 || Ty == MVT::v4f32 || Ty == MVT::v2i64 ||
      Ty == MVT::v2f64) {
    if (Fast)
      *Fast = 1;
    return true;
  }

  return false;
}


EVT ARMTargetLowering::getOptimalMemOpType(
    const MemOp &Op, const AttributeList &FuncAttributes) const {
  // See if we can use NEON instructions for this...
  if ((Op.isMemcpy() || Op.isZeroMemset()) && Subtarget->hasNEON() &&
      !FuncAttributes.hasFnAttr(Attribute::NoImplicitFloat)) {
    unsigned Fast;
    if (Op.size() >= 16 &&
        (Op.isAligned(Align(16)) ||
         (allowsMisalignedMemoryAccesses(MVT::v2f64, 0, Align(1),
                                         MachineMemOperand::MONone, &Fast) &&
          Fast))) {
      return MVT::v2f64;
    } else if (Op.size() >= 8 &&
               (Op.isAligned(Align(8)) ||
                (allowsMisalignedMemoryAccesses(
                     MVT::f64, 0, Align(1), MachineMemOperand::MONone, &Fast) &&
                 Fast))) {
      return MVT::f64;
    }
  }

  // Let the target-independent logic figure it out.
  return MVT::Other;
}

// 64-bit integers are split into their high and low parts and held in two
// different registers, so the trunc is free since the low register can just
// be used.
bool ARMTargetLowering::isTruncateFree(Type *SrcTy, Type *DstTy) const {
  if (!SrcTy->isIntegerTy() || !DstTy->isIntegerTy())
    return false;
  unsigned SrcBits = SrcTy->getPrimitiveSizeInBits();
  unsigned DestBits = DstTy->getPrimitiveSizeInBits();
  return (SrcBits == 64 && DestBits == 32);
}

bool ARMTargetLowering::isTruncateFree(EVT SrcVT, EVT DstVT) const {
  if (SrcVT.isVector() || DstVT.isVector() || !SrcVT.isInteger() ||
      !DstVT.isInteger())
    return false;
  unsigned SrcBits = SrcVT.getSizeInBits();
  unsigned DestBits = DstVT.getSizeInBits();
  return (SrcBits == 64 && DestBits == 32);
}

bool ARMTargetLowering::isZExtFree(SDValue Val, EVT VT2) const {
  if (Val.getOpcode() != ISD::LOAD)
    return false;

  EVT VT1 = Val.getValueType();
  if (!VT1.isSimple() || !VT1.isInteger() ||
      !VT2.isSimple() || !VT2.isInteger())
    return false;

  switch (VT1.getSimpleVT().SimpleTy) {
  default: break;
  case MVT::i1:
  case MVT::i8:
  case MVT::i16:
    // 8-bit and 16-bit loads implicitly zero-extend to 32-bits.
    return true;
  }

  return false;
}

bool ARMTargetLowering::isFNegFree(EVT VT) const {
  if (!VT.isSimple())
    return false;

  // There are quite a few FP16 instructions (e.g. VNMLA, VNMLS, etc.) that
  // negate values directly (fneg is free). So, we don't want to let the DAG
  // combiner rewrite fneg into xors and some other instructions.  For f16 and
  // FullFP16 argument passing, some bitcast nodes may be introduced,
  // triggering this DAG combine rewrite, so we are avoiding that with this.
  switch (VT.getSimpleVT().SimpleTy) {
  default: break;
  case MVT::f16:
    return Subtarget->hasFullFP16();
  }

  return false;
}

/// Check if Ext1 and Ext2 are extends of the same type, doubling the bitwidth
/// of the vector elements.
static bool areExtractExts(Value *Ext1, Value *Ext2) {
  auto areExtDoubled = [](Instruction *Ext) {
    return Ext->getType()->getScalarSizeInBits() ==
           2 * Ext->getOperand(0)->getType()->getScalarSizeInBits();
  };

  if (!match(Ext1, m_ZExtOrSExt(m_Value())) ||
      !match(Ext2, m_ZExtOrSExt(m_Value())) ||
      !areExtDoubled(cast<Instruction>(Ext1)) ||
      !areExtDoubled(cast<Instruction>(Ext2)))
    return false;

  return true;
}

/// Check if sinking \p I's operands to I's basic block is profitable, because
/// the operands can be folded into a target instruction, e.g.
/// sext/zext can be folded into vsubl.
bool ARMTargetLowering::shouldSinkOperands(Instruction *I,
                                           SmallVectorImpl<Use *> &Ops) const {
  if (!I->getType()->isVectorTy())
    return false;

  if (Subtarget->hasNEON()) {
    switch (I->getOpcode()) {
    case Instruction::Sub:
    case Instruction::Add: {
      if (!areExtractExts(I->getOperand(0), I->getOperand(1)))
        return false;
      Ops.push_back(&I->getOperandUse(0));
      Ops.push_back(&I->getOperandUse(1));
      return true;
    }
    default:
      return false;
    }
  }

  if (!Subtarget->hasMVEIntegerOps())
    return false;

  auto IsFMSMul = [&](Instruction *I) {
    if (!I->hasOneUse())
      return false;
    auto *Sub = cast<Instruction>(*I->users().begin());
    return Sub->getOpcode() == Instruction::FSub && Sub->getOperand(1) == I;
  };
  auto IsFMS = [&](Instruction *I) {
    if (match(I->getOperand(0), m_FNeg(m_Value())) ||
        match(I->getOperand(1), m_FNeg(m_Value())))
      return true;
    return false;
  };

  auto IsSinker = [&](Instruction *I, int Operand) {
    switch (I->getOpcode()) {
    case Instruction::Add:
    case Instruction::Mul:
    case Instruction::FAdd:
    case Instruction::ICmp:
    case Instruction::FCmp:
      return true;
    case Instruction::FMul:
      return !IsFMSMul(I);
    case Instruction::Sub:
    case Instruction::FSub:
    case Instruction::Shl:
    case Instruction::LShr:
    case Instruction::AShr:
      return Operand == 1;
    case Instruction::Call:
      if (auto *II = dyn_cast<IntrinsicInst>(I)) {
        switch (II->getIntrinsicID()) {
        case Intrinsic::fma:
          return !IsFMS(I);
        case Intrinsic::sadd_sat:
        case Intrinsic::uadd_sat:
        case Intrinsic::arm_mve_add_predicated:
        case Intrinsic::arm_mve_mul_predicated:
        case Intrinsic::arm_mve_qadd_predicated:
        case Intrinsic::arm_mve_vhadd:
        case Intrinsic::arm_mve_hadd_predicated:
        case Intrinsic::arm_mve_vqdmull:
        case Intrinsic::arm_mve_vqdmull_predicated:
        case Intrinsic::arm_mve_vqdmulh:
        case Intrinsic::arm_mve_qdmulh_predicated:
        case Intrinsic::arm_mve_vqrdmulh:
        case Intrinsic::arm_mve_qrdmulh_predicated:
        case Intrinsic::arm_mve_fma_predicated:
          return true;
        case Intrinsic::ssub_sat:
        case Intrinsic::usub_sat:
        case Intrinsic::arm_mve_sub_predicated:
        case Intrinsic::arm_mve_qsub_predicated:
        case Intrinsic::arm_mve_hsub_predicated:
        case Intrinsic::arm_mve_vhsub:
          return Operand == 1;
        default:
          return false;
        }
      }
      return false;
    default:
      return false;
    }
  };

  for (auto OpIdx : enumerate(I->operands())) {
    Instruction *Op = dyn_cast<Instruction>(OpIdx.value().get());
    // Make sure we are not already sinking this operand
    if (!Op || any_of(Ops, [&](Use *U) { return U->get() == Op; }))
      continue;

    Instruction *Shuffle = Op;
    if (Shuffle->getOpcode() == Instruction::BitCast)
      Shuffle = dyn_cast<Instruction>(Shuffle->getOperand(0));
    // We are looking for a splat that can be sunk.
    if (!Shuffle ||
        !match(Shuffle, m_Shuffle(
                            m_InsertElt(m_Undef(), m_Value(), m_ZeroInt()),
                            m_Undef(), m_ZeroMask())))
      continue;
    if (!IsSinker(I, OpIdx.index()))
      continue;

    // All uses of the shuffle should be sunk to avoid duplicating it across gpr
    // and vector registers
    for (Use &U : Op->uses()) {
      Instruction *Insn = cast<Instruction>(U.getUser());
      if (!IsSinker(Insn, U.getOperandNo()))
        return false;
    }

    Ops.push_back(&Shuffle->getOperandUse(0));
    if (Shuffle != Op)
      Ops.push_back(&Op->getOperandUse(0));
    Ops.push_back(&OpIdx.value());
  }
  return true;
}

Type *ARMTargetLowering::shouldConvertSplatType(ShuffleVectorInst *SVI) const {
  if (!Subtarget->hasMVEIntegerOps())
    return nullptr;
  Type *SVIType = SVI->getType();
  Type *ScalarType = SVIType->getScalarType();

  if (ScalarType->isFloatTy())
    return Type::getInt32Ty(SVIType->getContext());
  if (ScalarType->isHalfTy())
    return Type::getInt16Ty(SVIType->getContext());
  return nullptr;
}

bool ARMTargetLowering::isVectorLoadExtDesirable(SDValue ExtVal) const {
  EVT VT = ExtVal.getValueType();

  if (!isTypeLegal(VT))
    return false;

  if (auto *Ld = dyn_cast<MaskedLoadSDNode>(ExtVal.getOperand(0))) {
    if (Ld->isExpandingLoad())
      return false;
  }

  if (Subtarget->hasMVEIntegerOps())
    return true;

  // Don't create a loadext if we can fold the extension into a wide/long
  // instruction.
  // If there's more than one user instruction, the loadext is desirable no
  // matter what.  There can be two uses by the same instruction.
  if (ExtVal->use_empty() ||
      !ExtVal->use_begin()->isOnlyUserOf(ExtVal.getNode()))
    return true;

  SDNode *U = *ExtVal->use_begin();
  if ((U->getOpcode() == ISD::ADD || U->getOpcode() == ISD::SUB ||
       U->getOpcode() == ISD::SHL || U->getOpcode() == ARMISD::VSHLIMM))
    return false;

  return true;
}

bool ARMTargetLowering::allowTruncateForTailCall(Type *Ty1, Type *Ty2) const {
  if (!Ty1->isIntegerTy() || !Ty2->isIntegerTy())
    return false;

  if (!isTypeLegal(EVT::getEVT(Ty1)))
    return false;

  assert(Ty1->getPrimitiveSizeInBits() <= 64 && "i128 is probably not a noop");

  // Assuming the caller doesn't have a zeroext or signext return parameter,
  // truncation all the way down to i1 is valid.
  return true;
}

/// isFMAFasterThanFMulAndFAdd - Return true if an FMA operation is faster
/// than a pair of fmul and fadd instructions. fmuladd intrinsics will be
/// expanded to FMAs when this method returns true, otherwise fmuladd is
/// expanded to fmul + fadd.
///
/// ARM supports both fused and unfused multiply-add operations; we already
/// lower a pair of fmul and fadd to the latter so it's not clear that there
/// would be a gain or that the gain would be worthwhile enough to risk
/// correctness bugs.
///
/// For MVE, we set this to true as it helps simplify the need for some
/// patterns (and we don't have the non-fused floating point instruction).
bool ARMTargetLowering::isFMAFasterThanFMulAndFAdd(const MachineFunction &MF,
                                                   EVT VT) const {
  if (!VT.isSimple())
    return false;

  switch (VT.getSimpleVT().SimpleTy) {
  case MVT::v4f32:
  case MVT::v8f16:
    return Subtarget->hasMVEFloatOps();
  case MVT::f16:
    return Subtarget->useFPVFMx16();
  case MVT::f32:
    return Subtarget->useFPVFMx();
  case MVT::f64:
    return Subtarget->useFPVFMx64();
  default:
    break;
  }

  return false;
}

static bool isLegalT1AddressImmediate(int64_t V, EVT VT) {
  if (V < 0)
    return false;

  unsigned Scale = 1;
  switch (VT.getSimpleVT().SimpleTy) {
  case MVT::i1:
  case MVT::i8:
    // Scale == 1;
    break;
  case MVT::i16:
    // Scale == 2;
    Scale = 2;
    break;
  default:
    // On thumb1 we load most things (i32, i64, floats, etc) with a LDR
    // Scale == 4;
    Scale = 4;
    break;
  }

  if ((V & (Scale - 1)) != 0)
    return false;
  return isUInt<5>(V / Scale);
}

static bool isLegalT2AddressImmediate(int64_t V, EVT VT,
                                      const ARMSubtarget *Subtarget) {
  if (!VT.isInteger() && !VT.isFloatingPoint())
    return false;
  if (VT.isVector() && Subtarget->hasNEON())
    return false;
  if (VT.isVector() && VT.isFloatingPoint() && Subtarget->hasMVEIntegerOps() &&
      !Subtarget->hasMVEFloatOps())
    return false;

  bool IsNeg = false;
  if (V < 0) {
    IsNeg = true;
    V = -V;
  }

  unsigned NumBytes = std::max((unsigned)VT.getSizeInBits() / 8, 1U);

  // MVE: size * imm7
  if (VT.isVector() && Subtarget->hasMVEIntegerOps()) {
    switch (VT.getSimpleVT().getVectorElementType().SimpleTy) {
    case MVT::i32:
    case MVT::f32:
      return isShiftedUInt<7,2>(V);
    case MVT::i16:
    case MVT::f16:
      return isShiftedUInt<7,1>(V);
    case MVT::i8:
      return isUInt<7>(V);
    default:
      return false;
    }
  }

  // half VLDR: 2 * imm8
  if (VT.isFloatingPoint() && NumBytes == 2 && Subtarget->hasFPRegs16())
    return isShiftedUInt<8, 1>(V);
  // VLDR and LDRD: 4 * imm8
  if ((VT.isFloatingPoint() && Subtarget->hasVFP2Base()) || NumBytes == 8)
    return isShiftedUInt<8, 2>(V);

  if (NumBytes == 1 || NumBytes == 2 || NumBytes == 4) {
    // + imm12 or - imm8
    if (IsNeg)
      return isUInt<8>(V);
    return isUInt<12>(V);
  }

  return false;
}

/// isLegalAddressImmediate - Return true if the integer value can be used
/// as the offset of the target addressing mode for load / store of the
/// given type.
static bool isLegalAddressImmediate(int64_t V, EVT VT,
                                    const ARMSubtarget *Subtarget) {
  if (V == 0)
    return true;

  if (!VT.isSimple())
    return false;

  if (Subtarget->isThumb1Only())
    return isLegalT1AddressImmediate(V, VT);
  else if (Subtarget->isThumb2())
    return isLegalT2AddressImmediate(V, VT, Subtarget);

  // ARM mode.
  if (V < 0)
    V = - V;
  switch (VT.getSimpleVT().SimpleTy) {
  default: return false;
  case MVT::i1:
  case MVT::i8:
  case MVT::i32:
    // +- imm12
    return isUInt<12>(V);
  case MVT::i16:
    // +- imm8
    return isUInt<8>(V);
  case MVT::f32:
  case MVT::f64:
    if (!Subtarget->hasVFP2Base()) // FIXME: NEON?
      return false;
    return isShiftedUInt<8, 2>(V);
  }
}

bool ARMTargetLowering::isLegalT2ScaledAddressingMode(const AddrMode &AM,
                                                      EVT VT) const {
  int Scale = AM.Scale;
  if (Scale < 0)
    return false;

  switch (VT.getSimpleVT().SimpleTy) {
  default: return false;
  case MVT::i1:
  case MVT::i8:
  case MVT::i16:
  case MVT::i32:
    if (Scale == 1)
      return true;
    // r + r << imm
    Scale = Scale & ~1;
    return Scale == 2 || Scale == 4 || Scale == 8;
  case MVT::i64:
    // FIXME: What are we trying to model here? ldrd doesn't have an r + r
    // version in Thumb mode.
    // r + r
    if (Scale == 1)
      return true;
    // r * 2 (this can be lowered to r + r).
    if (!AM.HasBaseReg && Scale == 2)
      return true;
    return false;
  case MVT::isVoid:
    // Note, we allow "void" uses (basically, uses that aren't loads or
    // stores), because arm allows folding a scale into many arithmetic
    // operations.  This should be made more precise and revisited later.

    // Allow r << imm, but the imm has to be a multiple of two.
    if (Scale & 1) return false;
    return isPowerOf2_32(Scale);
  }
}

bool ARMTargetLowering::isLegalT1ScaledAddressingMode(const AddrMode &AM,
                                                      EVT VT) const {
  const int Scale = AM.Scale;

  // Negative scales are not supported in Thumb1.
  if (Scale < 0)
    return false;

  // Thumb1 addressing modes do not support register scaling excepting the
  // following cases:
  // 1. Scale == 1 means no scaling.
  // 2. Scale == 2 this can be lowered to r + r if there is no base register.
  return (Scale == 1) || (!AM.HasBaseReg && Scale == 2);
}

/// isLegalAddressingMode - Return true if the addressing mode represented
/// by AM is legal for this target, for a load/store of the specified type.
bool ARMTargetLowering::isLegalAddressingMode(const DataLayout &DL,
                                              const AddrMode &AM, Type *Ty,
                                              unsigned AS, Instruction *I) const {
  EVT VT = getValueType(DL, Ty, true);
  if (!isLegalAddressImmediate(AM.BaseOffs, VT, Subtarget))
    return false;

  // Can never fold addr of global into load/store.
  if (AM.BaseGV)
    return false;

  switch (AM.Scale) {
  case 0:  // no scale reg, must be "r+i" or "r", or "i".
    break;
  default:
    // ARM doesn't support any R+R*scale+imm addr modes.
    if (AM.BaseOffs)
      return false;

    if (!VT.isSimple())
      return false;

    if (Subtarget->isThumb1Only())
      return isLegalT1ScaledAddressingMode(AM, VT);

    if (Subtarget->isThumb2())
      return isLegalT2ScaledAddressingMode(AM, VT);

    int Scale = AM.Scale;
    switch (VT.getSimpleVT().SimpleTy) {
    default: return false;
    case MVT::i1:
    case MVT::i8:
    case MVT::i32:
      if (Scale < 0) Scale = -Scale;
      if (Scale == 1)
        return true;
      // r + r << imm
      return isPowerOf2_32(Scale & ~1);
    case MVT::i16:
    case MVT::i64:
      // r +/- r
      if (Scale == 1 || (AM.HasBaseReg && Scale == -1))
        return true;
      // r * 2 (this can be lowered to r + r).
      if (!AM.HasBaseReg && Scale == 2)
        return true;
      return false;

    case MVT::isVoid:
      // Note, we allow "void" uses (basically, uses that aren't loads or
      // stores), because arm allows folding a scale into many arithmetic
      // operations.  This should be made more precise and revisited later.

      // Allow r << imm, but the imm has to be a multiple of two.
      if (Scale & 1) return false;
      return isPowerOf2_32(Scale);
    }
  }
  return true;
}

/// isLegalICmpImmediate - Return true if the specified immediate is legal
/// icmp immediate, that is the target has icmp instructions which can compare
/// a register against the immediate without having to materialize the
/// immediate into a register.
bool ARMTargetLowering::isLegalICmpImmediate(int64_t Imm) const {
  // Thumb2 and ARM modes can use cmn for negative immediates.
  if (!Subtarget->isThumb())
    return ARM_AM::getSOImmVal((uint32_t)Imm) != -1 ||
           ARM_AM::getSOImmVal(-(uint32_t)Imm) != -1;
  if (Subtarget->isThumb2())
    return ARM_AM::getT2SOImmVal((uint32_t)Imm) != -1 ||
           ARM_AM::getT2SOImmVal(-(uint32_t)Imm) != -1;
  // Thumb1 doesn't have cmn, and only 8-bit immediates.
  return Imm >= 0 && Imm <= 255;
}

/// isLegalAddImmediate - Return true if the specified immediate is a legal add
/// *or sub* immediate, that is the target has add or sub instructions which can
/// add a register with the immediate without having to materialize the
/// immediate into a register.
bool ARMTargetLowering::isLegalAddImmediate(int64_t Imm) const {
  // Same encoding for add/sub, just flip the sign.
  int64_t AbsImm = std::abs(Imm);
  if (!Subtarget->isThumb())
    return ARM_AM::getSOImmVal(AbsImm) != -1;
  if (Subtarget->isThumb2())
    return ARM_AM::getT2SOImmVal(AbsImm) != -1;
  // Thumb1 only has 8-bit unsigned immediate.
  return AbsImm >= 0 && AbsImm <= 255;
}

// Return false to prevent folding
// (mul (add r, c0), c1) -> (add (mul r, c1), c0*c1) in DAGCombine,
// if the folding leads to worse code.
bool ARMTargetLowering::isMulAddWithConstProfitable(SDValue AddNode,
                                                    SDValue ConstNode) const {
  // Let the DAGCombiner decide for vector types and large types.
  const EVT VT = AddNode.getValueType();
  if (VT.isVector() || VT.getScalarSizeInBits() > 32)
    return true;

  // It is worse if c0 is legal add immediate, while c1*c0 is not
  // and has to be composed by at least two instructions.
  const ConstantSDNode *C0Node = cast<ConstantSDNode>(AddNode.getOperand(1));
  const ConstantSDNode *C1Node = cast<ConstantSDNode>(ConstNode);
  const int64_t C0 = C0Node->getSExtValue();
  APInt CA = C0Node->getAPIntValue() * C1Node->getAPIntValue();
  if (!isLegalAddImmediate(C0) || isLegalAddImmediate(CA.getSExtValue()))
    return true;
  if (ConstantMaterializationCost((unsigned)CA.getZExtValue(), Subtarget) > 1)
    return false;

  // Default to true and let the DAGCombiner decide.
  return true;
}

static bool getARMIndexedAddressParts(SDNode *Ptr, EVT VT,
                                      bool isSEXTLoad, SDValue &Base,
                                      SDValue &Offset, bool &isInc,
                                      SelectionDAG &DAG) {
  if (Ptr->getOpcode() != ISD::ADD && Ptr->getOpcode() != ISD::SUB)
    return false;

  if (VT == MVT::i16 || ((VT == MVT::i8 || VT == MVT::i1) && isSEXTLoad)) {
    // AddressingMode 3
    Base = Ptr->getOperand(0);
    if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(Ptr->getOperand(1))) {
      int RHSC = (int)RHS->getZExtValue();
      if (RHSC < 0 && RHSC > -256) {
        assert(Ptr->getOpcode() == ISD::ADD);
        isInc = false;
        Offset = DAG.getConstant(-RHSC, SDLoc(Ptr), RHS->getValueType(0));
        return true;
      }
    }
    isInc = (Ptr->getOpcode() == ISD::ADD);
    Offset = Ptr->getOperand(1);
    return true;
  } else if (VT == MVT::i32 || VT == MVT::i8 || VT == MVT::i1) {
    // AddressingMode 2
    if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(Ptr->getOperand(1))) {
      int RHSC = (int)RHS->getZExtValue();
      if (RHSC < 0 && RHSC > -0x1000) {
        assert(Ptr->getOpcode() == ISD::ADD);
        isInc = false;
        Offset = DAG.getConstant(-RHSC, SDLoc(Ptr), RHS->getValueType(0));
        Base = Ptr->getOperand(0);
        return true;
      }
    }

    if (Ptr->getOpcode() == ISD::ADD) {
      isInc = true;
      ARM_AM::ShiftOpc ShOpcVal=
        ARM_AM::getShiftOpcForNode(Ptr->getOperand(0).getOpcode());
      if (ShOpcVal != ARM_AM::no_shift) {
        Base = Ptr->getOperand(1);
        Offset = Ptr->getOperand(0);
      } else {
        Base = Ptr->getOperand(0);
        Offset = Ptr->getOperand(1);
      }
      return true;
    }

    isInc = (Ptr->getOpcode() == ISD::ADD);
    Base = Ptr->getOperand(0);
    Offset = Ptr->getOperand(1);
    return true;
  }

  // FIXME: Use VLDM / VSTM to emulate indexed FP load / store.
  return false;
}

static bool getT2IndexedAddressParts(SDNode *Ptr, EVT VT,
                                     bool isSEXTLoad, SDValue &Base,
                                     SDValue &Offset, bool &isInc,
                                     SelectionDAG &DAG) {
  if (Ptr->getOpcode() != ISD::ADD && Ptr->getOpcode() != ISD::SUB)
    return false;

  Base = Ptr->getOperand(0);
  if (ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(Ptr->getOperand(1))) {
    int RHSC = (int)RHS->getZExtValue();
    if (RHSC < 0 && RHSC > -0x100) { // 8 bits.
      assert(Ptr->getOpcode() == ISD::ADD);
      isInc = false;
      Offset = DAG.getConstant(-RHSC, SDLoc(Ptr), RHS->getValueType(0));
      return true;
    } else if (RHSC > 0 && RHSC < 0x100) { // 8 bit, no zero.
      isInc = Ptr->getOpcode() == ISD::ADD;
      Offset = DAG.getConstant(RHSC, SDLoc(Ptr), RHS->getValueType(0));
      return true;
    }
  }

  return false;
}

static bool getMVEIndexedAddressParts(SDNode *Ptr, EVT VT, Align Alignment,
                                      bool isSEXTLoad, bool IsMasked, bool isLE,
                                      SDValue &Base, SDValue &Offset,
                                      bool &isInc, SelectionDAG &DAG) {
  if (Ptr->getOpcode() != ISD::ADD && Ptr->getOpcode() != ISD::SUB)
    return false;
  if (!isa<ConstantSDNode>(Ptr->getOperand(1)))
    return false;

  // We allow LE non-masked loads to change the type (for example use a vldrb.8
  // as opposed to a vldrw.32). This can allow extra addressing modes or
  // alignments for what is otherwise an equivalent instruction.
  bool CanChangeType = isLE && !IsMasked;

  ConstantSDNode *RHS = cast<ConstantSDNode>(Ptr->getOperand(1));
  int RHSC = (int)RHS->getZExtValue();

  auto IsInRange = [&](int RHSC, int Limit, int Scale) {
    if (RHSC < 0 && RHSC > -Limit * Scale && RHSC % Scale == 0) {
      assert(Ptr->getOpcode() == ISD::ADD);
      isInc = false;
      Offset = DAG.getConstant(-RHSC, SDLoc(Ptr), RHS->getValueType(0));
      return true;
    } else if (RHSC > 0 && RHSC < Limit * Scale && RHSC % Scale == 0) {
      isInc = Ptr->getOpcode() == ISD::ADD;
      Offset = DAG.getConstant(RHSC, SDLoc(Ptr), RHS->getValueType(0));
      return true;
    }
    return false;
  };

  // Try to find a matching instruction based on s/zext, Alignment, Offset and
  // (in BE/masked) type.
  Base = Ptr->getOperand(0);
  if (VT == MVT::v4i16) {
    if (Alignment >= 2 && IsInRange(RHSC, 0x80, 2))
      return true;
  } else if (VT == MVT::v4i8 || VT == MVT::v8i8) {
    if (IsInRange(RHSC, 0x80, 1))
      return true;
  } else if (Alignment >= 4 &&
             (CanChangeType || VT == MVT::v4i32 || VT == MVT::v4f32) &&
             IsInRange(RHSC, 0x80, 4))
    return true;
  else if (Alignment >= 2 &&
           (CanChangeType || VT == MVT::v8i16 || VT == MVT::v8f16) &&
           IsInRange(RHSC, 0x80, 2))
    return true;
  else if ((CanChangeType || VT == MVT::v16i8) && IsInRange(RHSC, 0x80, 1))
    return true;
  return false;
}

/// getPreIndexedAddressParts - returns true by value, base pointer and
/// offset pointer and addressing mode by reference if the node's address
/// can be legally represented as pre-indexed load / store address.
bool
ARMTargetLowering::getPreIndexedAddressParts(SDNode *N, SDValue &Base,
                                             SDValue &Offset,
                                             ISD::MemIndexedMode &AM,
                                             SelectionDAG &DAG) const {
  if (Subtarget->isThumb1Only())
    return false;

  EVT VT;
  SDValue Ptr;
  Align Alignment;
  bool isSEXTLoad = false;
  bool IsMasked = false;
  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    Ptr = LD->getBasePtr();
    VT = LD->getMemoryVT();
    Alignment = LD->getAlign();
    isSEXTLoad = LD->getExtensionType() == ISD::SEXTLOAD;
  } else if (StoreSDNode *ST = dyn_cast<StoreSDNode>(N)) {
    Ptr = ST->getBasePtr();
    VT = ST->getMemoryVT();
    Alignment = ST->getAlign();
  } else if (MaskedLoadSDNode *LD = dyn_cast<MaskedLoadSDNode>(N)) {
    Ptr = LD->getBasePtr();
    VT = LD->getMemoryVT();
    Alignment = LD->getAlign();
    isSEXTLoad = LD->getExtensionType() == ISD::SEXTLOAD;
    IsMasked = true;
  } else if (MaskedStoreSDNode *ST = dyn_cast<MaskedStoreSDNode>(N)) {
    Ptr = ST->getBasePtr();
    VT = ST->getMemoryVT();
    Alignment = ST->getAlign();
    IsMasked = true;
  } else
    return false;

  bool isInc;
  bool isLegal = false;
  if (VT.isVector())
    isLegal = Subtarget->hasMVEIntegerOps() &&
              getMVEIndexedAddressParts(
                  Ptr.getNode(), VT, Alignment, isSEXTLoad, IsMasked,
                  Subtarget->isLittle(), Base, Offset, isInc, DAG);
  else {
    if (Subtarget->isThumb2())
      isLegal = getT2IndexedAddressParts(Ptr.getNode(), VT, isSEXTLoad, Base,
                                         Offset, isInc, DAG);
    else
      isLegal = getARMIndexedAddressParts(Ptr.getNode(), VT, isSEXTLoad, Base,
                                          Offset, isInc, DAG);
  }
  if (!isLegal)
    return false;

  AM = isInc ? ISD::PRE_INC : ISD::PRE_DEC;
  return true;
}

/// getPostIndexedAddressParts - returns true by value, base pointer and
/// offset pointer and addressing mode by reference if this node can be
/// combined with a load / store to form a post-indexed load / store.
bool ARMTargetLowering::getPostIndexedAddressParts(SDNode *N, SDNode *Op,
                                                   SDValue &Base,
                                                   SDValue &Offset,
                                                   ISD::MemIndexedMode &AM,
                                                   SelectionDAG &DAG) const {
  EVT VT;
  SDValue Ptr;
  Align Alignment;
  bool isSEXTLoad = false, isNonExt;
  bool IsMasked = false;
  if (LoadSDNode *LD = dyn_cast<LoadSDNode>(N)) {
    VT = LD->getMemoryVT();
    Ptr = LD->getBasePtr();
    Alignment = LD->getAlign();
    isSEXTLoad = LD->getExtensionType() == ISD::SEXTLOAD;
    isNonExt = LD->getExtensionType() == ISD::NON_EXTLOAD;
  } else if (StoreSDNode *ST = dyn_cast<StoreSDNode>(N)) {
    VT = ST->getMemoryVT();
    Ptr = ST->getBasePtr();
    Alignment = ST->getAlign();
    isNonExt = !ST->isTruncatingStore();
  } else if (MaskedLoadSDNode *LD = dyn_cast<MaskedLoadSDNode>(N)) {
    VT = LD->getMemoryVT();
    Ptr = LD->getBasePtr();
    Alignment = LD->getAlign();
    isSEXTLoad = LD->getExtensionType() == ISD::SEXTLOAD;
    isNonExt = LD->getExtensionType() == ISD::NON_EXTLOAD;
    IsMasked = true;
  } else if (MaskedStoreSDNode *ST = dyn_cast<MaskedStoreSDNode>(N)) {
    VT = ST->getMemoryVT();
    Ptr = ST->getBasePtr();
    Alignment = ST->getAlign();
    isNonExt = !ST->isTruncatingStore();
    IsMasked = true;
  } else
    return false;

  if (Subtarget->isThumb1Only()) {
    // Thumb-1 can do a limited post-inc load or store as an updating LDM. It
    // must be non-extending/truncating, i32, with an offset of 4.
    assert(Op->getValueType(0) == MVT::i32 && "Non-i32 post-inc op?!");
    if (Op->getOpcode() != ISD::ADD || !isNonExt)
      return false;
    auto *RHS = dyn_cast<ConstantSDNode>(Op->getOperand(1));
    if (!RHS || RHS->getZExtValue() != 4)
      return false;
    if (Alignment < Align(4))
      return false;

    Offset = Op->getOperand(1);
    Base = Op->getOperand(0);
    AM = ISD::POST_INC;
    return true;
  }

  bool isInc;
  bool isLegal = false;
  if (VT.isVector())
    isLegal = Subtarget->hasMVEIntegerOps() &&
              getMVEIndexedAddressParts(Op, VT, Alignment, isSEXTLoad, IsMasked,
                                        Subtarget->isLittle(), Base, Offset,
                                        isInc, DAG);
  else {
    if (Subtarget->isThumb2())
      isLegal = getT2IndexedAddressParts(Op, VT, isSEXTLoad, Base, Offset,
                                         isInc, DAG);
    else
      isLegal = getARMIndexedAddressParts(Op, VT, isSEXTLoad, Base, Offset,
                                          isInc, DAG);
  }
  if (!isLegal)
    return false;

  if (Ptr != Base) {
    // Swap base ptr and offset to catch more post-index load / store when
    // it's legal. In Thumb2 mode, offset must be an immediate.
    if (Ptr == Offset && Op->getOpcode() == ISD::ADD &&
        !Subtarget->isThumb2())
      std::swap(Base, Offset);

    // Post-indexed load / store update the base pointer.
    if (Ptr != Base)
      return false;
  }

  AM = isInc ? ISD::POST_INC : ISD::POST_DEC;
  return true;
}

void ARMTargetLowering::computeKnownBitsForTargetNode(const SDValue Op,
                                                      KnownBits &Known,
                                                      const APInt &DemandedElts,
                                                      const SelectionDAG &DAG,
                                                      unsigned Depth) const {
  unsigned BitWidth = Known.getBitWidth();
  Known.resetAll();
  switch (Op.getOpcode()) {
  default: break;
  case ARMISD::ADDC:
  case ARMISD::ADDE:
  case ARMISD::SUBC:
  case ARMISD::SUBE:
    // Special cases when we convert a carry to a boolean.
    if (Op.getResNo() == 0) {
      SDValue LHS = Op.getOperand(0);
      SDValue RHS = Op.getOperand(1);
      // (ADDE 0, 0, C) will give us a single bit.
      if (Op->getOpcode() == ARMISD::ADDE && isNullConstant(LHS) &&
          isNullConstant(RHS)) {
        Known.Zero |= APInt::getHighBitsSet(BitWidth, BitWidth - 1);
        return;
      }
    }
    break;
  case ARMISD::CMOV: {
    // Bits are known zero/one if known on the LHS and RHS.
    Known = DAG.computeKnownBits(Op.getOperand(0), Depth+1);
    if (Known.isUnknown())
      return;

    KnownBits KnownRHS = DAG.computeKnownBits(Op.getOperand(1), Depth+1);
    Known = Known.intersectWith(KnownRHS);
    return;
  }
  case ISD::INTRINSIC_W_CHAIN: {
    Intrinsic::ID IntID =
        static_cast<Intrinsic::ID>(Op->getConstantOperandVal(1));
    switch (IntID) {
    default: return;
    case Intrinsic::arm_ldaex:
    case Intrinsic::arm_ldrex: {
      EVT VT = cast<MemIntrinsicSDNode>(Op)->getMemoryVT();
      unsigned MemBits = VT.getScalarSizeInBits();
      Known.Zero |= APInt::getHighBitsSet(BitWidth, BitWidth - MemBits);
      return;
    }
    }
  }
  case ARMISD::BFI: {
    // Conservatively, we can recurse down the first operand
    // and just mask out all affected bits.
    Known = DAG.computeKnownBits(Op.getOperand(0), Depth + 1);

    // The operand to BFI is already a mask suitable for removing the bits it
    // sets.
    const APInt &Mask = Op.getConstantOperandAPInt(2);
    Known.Zero &= Mask;
    Known.One &= Mask;
    return;
  }
  case ARMISD::VGETLANEs:
  case ARMISD::VGETLANEu: {
    const SDValue &SrcSV = Op.getOperand(0);
    EVT VecVT = SrcSV.getValueType();
    assert(VecVT.isVector() && "VGETLANE expected a vector type");
    const unsigned NumSrcElts = VecVT.getVectorNumElements();
    ConstantSDNode *Pos = cast<ConstantSDNode>(Op.getOperand(1).getNode());
    assert(Pos->getAPIntValue().ult(NumSrcElts) &&
           "VGETLANE index out of bounds");
    unsigned Idx = Pos->getZExtValue();
    APInt DemandedElt = APInt::getOneBitSet(NumSrcElts, Idx);
    Known = DAG.computeKnownBits(SrcSV, DemandedElt, Depth + 1);

    EVT VT = Op.getValueType();
    const unsigned DstSz = VT.getScalarSizeInBits();
    const unsigned SrcSz = VecVT.getVectorElementType().getSizeInBits();
    (void)SrcSz;
    assert(SrcSz == Known.getBitWidth());
    assert(DstSz > SrcSz);
    if (Op.getOpcode() == ARMISD::VGETLANEs)
      Known = Known.sext(DstSz);
    else {
      Known = Known.zext(DstSz);
    }
    assert(DstSz == Known.getBitWidth());
    break;
  }
  case ARMISD::VMOVrh: {
    KnownBits KnownOp = DAG.computeKnownBits(Op->getOperand(0), Depth + 1);
    assert(KnownOp.getBitWidth() == 16);
    Known = KnownOp.zext(32);
    break;
  }
  case ARMISD::CSINC:
  case ARMISD::CSINV:
  case ARMISD::CSNEG: {
    KnownBits KnownOp0 = DAG.computeKnownBits(Op->getOperand(0), Depth + 1);
    KnownBits KnownOp1 = DAG.computeKnownBits(Op->getOperand(1), Depth + 1);

    // The result is either:
    // CSINC: KnownOp0 or KnownOp1 + 1
    // CSINV: KnownOp0 or ~KnownOp1
    // CSNEG: KnownOp0 or KnownOp1 * -1
    if (Op.getOpcode() == ARMISD::CSINC)
      KnownOp1 = KnownBits::computeForAddSub(
          /*Add=*/true, /*NSW=*/false, /*NUW=*/false, KnownOp1,
          KnownBits::makeConstant(APInt(32, 1)));
    else if (Op.getOpcode() == ARMISD::CSINV)
      std::swap(KnownOp1.Zero, KnownOp1.One);
    else if (Op.getOpcode() == ARMISD::CSNEG)
      KnownOp1 = KnownBits::mul(
          KnownOp1, KnownBits::makeConstant(APInt(32, -1)));

    Known = KnownOp0.intersectWith(KnownOp1);
    break;
  }
  }
}

bool ARMTargetLowering::targetShrinkDemandedConstant(
    SDValue Op, const APInt &DemandedBits, const APInt &DemandedElts,
    TargetLoweringOpt &TLO) const {
  // Delay optimization, so we don't have to deal with illegal types, or block
  // optimizations.
  if (!TLO.LegalOps)
    return false;

  // Only optimize AND for now.
  if (Op.getOpcode() != ISD::AND)
    return false;

  EVT VT = Op.getValueType();

  // Ignore vectors.
  if (VT.isVector())
    return false;

  assert(VT == MVT::i32 && "Unexpected integer type");

  // Make sure the RHS really is a constant.
  ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op.getOperand(1));
  if (!C)
    return false;

  unsigned Mask = C->getZExtValue();

  unsigned Demanded = DemandedBits.getZExtValue();
  unsigned ShrunkMask = Mask & Demanded;
  unsigned ExpandedMask = Mask | ~Demanded;

  // If the mask is all zeros, let the target-independent code replace the
  // result with zero.
  if (ShrunkMask == 0)
    return false;

  // If the mask is all ones, erase the AND. (Currently, the target-independent
  // code won't do this, so we have to do it explicitly to avoid an infinite
  // loop in obscure cases.)
  if (ExpandedMask == ~0U)
    return TLO.CombineTo(Op, Op.getOperand(0));

  auto IsLegalMask = [ShrunkMask, ExpandedMask](unsigned Mask) -> bool {
    return (ShrunkMask & Mask) == ShrunkMask && (~ExpandedMask & Mask) == 0;
  };
  auto UseMask = [Mask, Op, VT, &TLO](unsigned NewMask) -> bool {
    if (NewMask == Mask)
      return true;
    SDLoc DL(Op);
    SDValue NewC = TLO.DAG.getConstant(NewMask, DL, VT);
    SDValue NewOp = TLO.DAG.getNode(ISD::AND, DL, VT, Op.getOperand(0), NewC);
    return TLO.CombineTo(Op, NewOp);
  };

  // Prefer uxtb mask.
  if (IsLegalMask(0xFF))
    return UseMask(0xFF);

  // Prefer uxth mask.
  if (IsLegalMask(0xFFFF))
    return UseMask(0xFFFF);

  // [1, 255] is Thumb1 movs+ands, legal immediate for ARM/Thumb2.
  // FIXME: Prefer a contiguous sequence of bits for other optimizations.
  if (ShrunkMask < 256)
    return UseMask(ShrunkMask);

  // [-256, -2] is Thumb1 movs+bics, legal immediate for ARM/Thumb2.
  // FIXME: Prefer a contiguous sequence of bits for other optimizations.
  if ((int)ExpandedMask <= -2 && (int)ExpandedMask >= -256)
    return UseMask(ExpandedMask);

  // Potential improvements:
  //
  // We could try to recognize lsls+lsrs or lsrs+lsls pairs here.
  // We could try to prefer Thumb1 immediates which can be lowered to a
  // two-instruction sequence.
  // We could try to recognize more legal ARM/Thumb2 immediates here.

  return false;
}

bool ARMTargetLowering::SimplifyDemandedBitsForTargetNode(
    SDValue Op, const APInt &OriginalDemandedBits,
    const APInt &OriginalDemandedElts, KnownBits &Known, TargetLoweringOpt &TLO,
    unsigned Depth) const {
  unsigned Opc = Op.getOpcode();

  switch (Opc) {
  case ARMISD::ASRL:
  case ARMISD::LSRL: {
    // If this is result 0 and the other result is unused, see if the demand
    // bits allow us to shrink this long shift into a standard small shift in
    // the opposite direction.
    if (Op.getResNo() == 0 && !Op->hasAnyUseOfValue(1) &&
        isa<ConstantSDNode>(Op->getOperand(2))) {
      unsigned ShAmt = Op->getConstantOperandVal(2);
      if (ShAmt < 32 && OriginalDemandedBits.isSubsetOf(APInt::getAllOnes(32)
                                                        << (32 - ShAmt)))
        return TLO.CombineTo(
            Op, TLO.DAG.getNode(
                    ISD::SHL, SDLoc(Op), MVT::i32, Op.getOperand(1),
                    TLO.DAG.getConstant(32 - ShAmt, SDLoc(Op), MVT::i32)));
    }
    break;
  }
  case ARMISD::VBICIMM: {
    SDValue Op0 = Op.getOperand(0);
    unsigned ModImm = Op.getConstantOperandVal(1);
    unsigned EltBits = 0;
    uint64_t Mask = ARM_AM::decodeVMOVModImm(ModImm, EltBits);
    if ((OriginalDemandedBits & Mask) == 0)
      return TLO.CombineTo(Op, Op0);
  }
  }

  return TargetLowering::SimplifyDemandedBitsForTargetNode(
      Op, OriginalDemandedBits, OriginalDemandedElts, Known, TLO, Depth);
}

//===----------------------------------------------------------------------===//
//                           ARM Inline Assembly Support
//===----------------------------------------------------------------------===//

bool ARMTargetLowering::ExpandInlineAsm(CallInst *CI) const {
  // Looking for "rev" which is V6+.
  if (!Subtarget->hasV6Ops())
    return false;

  InlineAsm *IA = cast<InlineAsm>(CI->getCalledOperand());
  StringRef AsmStr = IA->getAsmString();
  SmallVector<StringRef, 4> AsmPieces;
  SplitString(AsmStr, AsmPieces, ";\n");

  switch (AsmPieces.size()) {
  default: return false;
  case 1:
    AsmStr = AsmPieces[0];
    AsmPieces.clear();
    SplitString(AsmStr, AsmPieces, " \t,");

    // rev $0, $1
    if (AsmPieces.size() == 3 &&
        AsmPieces[0] == "rev" && AsmPieces[1] == "$0" && AsmPieces[2] == "$1" &&
        IA->getConstraintString().compare(0, 4, "=l,l") == 0) {
      IntegerType *Ty = dyn_cast<IntegerType>(CI->getType());
      if (Ty && Ty->getBitWidth() == 32)
        return IntrinsicLowering::LowerToByteSwap(CI);
    }
    break;
  }

  return false;
}

const char *ARMTargetLowering::LowerXConstraint(EVT ConstraintVT) const {
  // At this point, we have to lower this constraint to something else, so we
  // lower it to an "r" or "w". However, by doing this we will force the result
  // to be in register, while the X constraint is much more permissive.
  //
  // Although we are correct (we are free to emit anything, without
  // constraints), we might break use cases that would expect us to be more
  // efficient and emit something else.
  if (!Subtarget->hasVFP2Base())
    return "r";
  if (ConstraintVT.isFloatingPoint())
    return "w";
  if (ConstraintVT.isVector() && Subtarget->hasNEON() &&
     (ConstraintVT.getSizeInBits() == 64 ||
      ConstraintVT.getSizeInBits() == 128))
    return "w";

  return "r";
}

/// getConstraintType - Given a constraint letter, return the type of
/// constraint it is for this target.
ARMTargetLowering::ConstraintType
ARMTargetLowering::getConstraintType(StringRef Constraint) const {
  unsigned S = Constraint.size();
  if (S == 1) {
    switch (Constraint[0]) {
    default:  break;
    case 'l': return C_RegisterClass;
    case 'w': return C_RegisterClass;
    case 'h': return C_RegisterClass;
    case 'x': return C_RegisterClass;
    case 't': return C_RegisterClass;
    case 'j': return C_Immediate; // Constant for movw.
    // An address with a single base register. Due to the way we
    // currently handle addresses it is the same as an 'r' memory constraint.
    case 'Q': return C_Memory;
    }
  } else if (S == 2) {
    switch (Constraint[0]) {
    default: break;
    case 'T': return C_RegisterClass;
    // All 'U+' constraints are addresses.
    case 'U': return C_Memory;
    }
  }
  return TargetLowering::getConstraintType(Constraint);
}

/// Examine constraint type and operand type and determine a weight value.
/// This object must already have been set up with the operand type
/// and the current alternative constraint selected.
TargetLowering::ConstraintWeight
ARMTargetLowering::getSingleConstraintMatchWeight(
    AsmOperandInfo &info, const char *constraint) const {
  ConstraintWeight weight = CW_Invalid;
  Value *CallOperandVal = info.CallOperandVal;
    // If we don't have a value, we can't do a match,
    // but allow it at the lowest weight.
  if (!CallOperandVal)
    return CW_Default;
  Type *type = CallOperandVal->getType();
  // Look at the constraint type.
  switch (*constraint) {
  default:
    weight = TargetLowering::getSingleConstraintMatchWeight(info, constraint);
    break;
  case 'l':
    if (type->isIntegerTy()) {
      if (Subtarget->isThumb())
        weight = CW_SpecificReg;
      else
        weight = CW_Register;
    }
    break;
  case 'w':
    if (type->isFloatingPointTy())
      weight = CW_Register;
    break;
  }
  return weight;
}

using RCPair = std::pair<unsigned, const TargetRegisterClass *>;

RCPair ARMTargetLowering::getRegForInlineAsmConstraint(
    const TargetRegisterInfo *TRI, StringRef Constraint, MVT VT) const {
  switch (Constraint.size()) {
  case 1:
    // GCC ARM Constraint Letters
    switch (Constraint[0]) {
    case 'l': // Low regs or general regs.
      if (Subtarget->isThumb())
        return RCPair(0U, &ARM::tGPRRegClass);
      return RCPair(0U, &ARM::GPRRegClass);
    case 'h': // High regs or no regs.
      if (Subtarget->isThumb())
        return RCPair(0U, &ARM::hGPRRegClass);
      break;
    case 'r':
      if (Subtarget->isThumb1Only())
        return RCPair(0U, &ARM::tGPRRegClass);
      return RCPair(0U, &ARM::GPRRegClass);
    case 'w':
      if (VT == MVT::Other)
        break;
      if (VT == MVT::f32 || VT == MVT::f16 || VT == MVT::bf16)
        return RCPair(0U, &ARM::SPRRegClass);
      if (VT.getSizeInBits() == 64)
        return RCPair(0U, &ARM::DPRRegClass);
      if (VT.getSizeInBits() == 128)
        return RCPair(0U, &ARM::QPRRegClass);
      break;
    case 'x':
      if (VT == MVT::Other)
        break;
      if (VT == MVT::f32 || VT == MVT::f16 || VT == MVT::bf16)
        return RCPair(0U, &ARM::SPR_8RegClass);
      if (VT.getSizeInBits() == 64)
        return RCPair(0U, &ARM::DPR_8RegClass);
      if (VT.getSizeInBits() == 128)
        return RCPair(0U, &ARM::QPR_8RegClass);
      break;
    case 't':
      if (VT == MVT::Other)
        break;
      if (VT == MVT::f32 || VT == MVT::i32 || VT == MVT::f16 || VT == MVT::bf16)
        return RCPair(0U, &ARM::SPRRegClass);
      if (VT.getSizeInBits() == 64)
        return RCPair(0U, &ARM::DPR_VFP2RegClass);
      if (VT.getSizeInBits() == 128)
        return RCPair(0U, &ARM::QPR_VFP2RegClass);
      break;
    }
    break;

  case 2:
    if (Constraint[0] == 'T') {
      switch (Constraint[1]) {
      default:
        break;
      case 'e':
        return RCPair(0U, &ARM::tGPREvenRegClass);
      case 'o':
        return RCPair(0U, &ARM::tGPROddRegClass);
      }
    }
    break;

  default:
    break;
  }

  if (StringRef("{cc}").equals_insensitive(Constraint))
    return std::make_pair(unsigned(ARM::CPSR), &ARM::CCRRegClass);

  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

/// LowerAsmOperandForConstraint - Lower the specified operand into the Ops
/// vector.  If it is invalid, don't add anything to Ops.
void ARMTargetLowering::LowerAsmOperandForConstraint(SDValue Op,
                                                     StringRef Constraint,
                                                     std::vector<SDValue> &Ops,
                                                     SelectionDAG &DAG) const {
  SDValue Result;

  // Currently only support length 1 constraints.
  if (Constraint.size() != 1)
    return;

  char ConstraintLetter = Constraint[0];
  switch (ConstraintLetter) {
  default: break;
  case 'j':
  case 'I': case 'J': case 'K': case 'L':
  case 'M': case 'N': case 'O':
    ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op);
    if (!C)
      return;

    int64_t CVal64 = C->getSExtValue();
    int CVal = (int) CVal64;
    // None of these constraints allow values larger than 32 bits.  Check
    // that the value fits in an int.
    if (CVal != CVal64)
      return;

    switch (ConstraintLetter) {
      case 'j':
        // Constant suitable for movw, must be between 0 and
        // 65535.
        if (Subtarget->hasV6T2Ops() || (Subtarget->hasV8MBaselineOps()))
          if (CVal >= 0 && CVal <= 65535)
            break;
        return;
      case 'I':
        if (Subtarget->isThumb1Only()) {
          // This must be a constant between 0 and 255, for ADD
          // immediates.
          if (CVal >= 0 && CVal <= 255)
            break;
        } else if (Subtarget->isThumb2()) {
          // A constant that can be used as an immediate value in a
          // data-processing instruction.
          if (ARM_AM::getT2SOImmVal(CVal) != -1)
            break;
        } else {
          // A constant that can be used as an immediate value in a
          // data-processing instruction.
          if (ARM_AM::getSOImmVal(CVal) != -1)
            break;
        }
        return;

      case 'J':
        if (Subtarget->isThumb1Only()) {
          // This must be a constant between -255 and -1, for negated ADD
          // immediates. This can be used in GCC with an "n" modifier that
          // prints the negated value, for use with SUB instructions. It is
          // not useful otherwise but is implemented for compatibility.
          if (CVal >= -255 && CVal <= -1)
            break;
        } else {
          // This must be a constant between -4095 and 4095. It is not clear
          // what this constraint is intended for. Implemented for
          // compatibility with GCC.
          if (CVal >= -4095 && CVal <= 4095)
            break;
        }
        return;

      case 'K':
        if (Subtarget->isThumb1Only()) {
          // A 32-bit value where only one byte has a nonzero value. Exclude
          // zero to match GCC. This constraint is used by GCC internally for
          // constants that can be loaded with a move/shift combination.
          // It is not useful otherwise but is implemented for compatibility.
          if (CVal != 0 && ARM_AM::isThumbImmShiftedVal(CVal))
            break;
        } else if (Subtarget->isThumb2()) {
          // A constant whose bitwise inverse can be used as an immediate
          // value in a data-processing instruction. This can be used in GCC
          // with a "B" modifier that prints the inverted value, for use with
          // BIC and MVN instructions. It is not useful otherwise but is
          // implemented for compatibility.
          if (ARM_AM::getT2SOImmVal(~CVal) != -1)
            break;
        } else {
          // A constant whose bitwise inverse can be used as an immediate
          // value in a data-processing instruction. This can be used in GCC
          // with a "B" modifier that prints the inverted value, for use with
          // BIC and MVN instructions. It is not useful otherwise but is
          // implemented for compatibility.
          if (ARM_AM::getSOImmVal(~CVal) != -1)
            break;
        }
        return;

      case 'L':
        if (Subtarget->isThumb1Only()) {
          // This must be a constant between -7 and 7,
          // for 3-operand ADD/SUB immediate instructions.
          if (CVal >= -7 && CVal < 7)
            break;
        } else if (Subtarget->isThumb2()) {
          // A constant whose negation can be used as an immediate value in a
          // data-processing instruction. This can be used in GCC with an "n"
          // modifier that prints the negated value, for use with SUB
          // instructions. It is not useful otherwise but is implemented for
          // compatibility.
          if (ARM_AM::getT2SOImmVal(-CVal) != -1)
            break;
        } else {
          // A constant whose negation can be used as an immediate value in a
          // data-processing instruction. This can be used in GCC with an "n"
          // modifier that prints the negated value, for use with SUB
          // instructions. It is not useful otherwise but is implemented for
          // compatibility.
          if (ARM_AM::getSOImmVal(-CVal) != -1)
            break;
        }
        return;

      case 'M':
        if (Subtarget->isThumb1Only()) {
          // This must be a multiple of 4 between 0 and 1020, for
          // ADD sp + immediate.
          if ((CVal >= 0 && CVal <= 1020) && ((CVal & 3) == 0))
            break;
        } else {
          // A power of two or a constant between 0 and 32.  This is used in
          // GCC for the shift amount on shifted register operands, but it is
          // useful in general for any shift amounts.
          if ((CVal >= 0 && CVal <= 32) || ((CVal & (CVal - 1)) == 0))
            break;
        }
        return;

      case 'N':
        if (Subtarget->isThumb1Only()) {
          // This must be a constant between 0 and 31, for shift amounts.
          if (CVal >= 0 && CVal <= 31)
            break;
        }
        return;

      case 'O':
        if (Subtarget->isThumb1Only()) {
          // This must be a multiple of 4 between -508 and 508, for
          // ADD/SUB sp = sp + immediate.
          if ((CVal >= -508 && CVal <= 508) && ((CVal & 3) == 0))
            break;
        }
        return;
    }
    Result = DAG.getTargetConstant(CVal, SDLoc(Op), Op.getValueType());
    break;
  }

  if (Result.getNode()) {
    Ops.push_back(Result);
    return;
  }
  return TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops, DAG);
}

static RTLIB::Libcall getDivRemLibcall(
    const SDNode *N, MVT::SimpleValueType SVT) {
  assert((N->getOpcode() == ISD::SDIVREM || N->getOpcode() == ISD::UDIVREM ||
          N->getOpcode() == ISD::SREM    || N->getOpcode() == ISD::UREM) &&
         "Unhandled Opcode in getDivRemLibcall");
  bool isSigned = N->getOpcode() == ISD::SDIVREM ||
                  N->getOpcode() == ISD::SREM;
  RTLIB::Libcall LC;
  switch (SVT) {
  default: llvm_unreachable("Unexpected request for libcall!");
  case MVT::i8:  LC = isSigned ? RTLIB::SDIVREM_I8  : RTLIB::UDIVREM_I8;  break;
  case MVT::i16: LC = isSigned ? RTLIB::SDIVREM_I16 : RTLIB::UDIVREM_I16; break;
  case MVT::i32: LC = isSigned ? RTLIB::SDIVREM_I32 : RTLIB::UDIVREM_I32; break;
  case MVT::i64: LC = isSigned ? RTLIB::SDIVREM_I64 : RTLIB::UDIVREM_I64; break;
  }
  return LC;
}

static TargetLowering::ArgListTy getDivRemArgList(
    const SDNode *N, LLVMContext *Context, const ARMSubtarget *Subtarget) {
  assert((N->getOpcode() == ISD::SDIVREM || N->getOpcode() == ISD::UDIVREM ||
          N->getOpcode() == ISD::SREM    || N->getOpcode() == ISD::UREM) &&
         "Unhandled Opcode in getDivRemArgList");
  bool isSigned = N->getOpcode() == ISD::SDIVREM ||
                  N->getOpcode() == ISD::SREM;
  TargetLowering::ArgListTy Args;
  TargetLowering::ArgListEntry Entry;
  for (unsigned i = 0, e = N->getNumOperands(); i != e; ++i) {
    EVT ArgVT = N->getOperand(i).getValueType();
    Type *ArgTy = ArgVT.getTypeForEVT(*Context);
    Entry.Node = N->getOperand(i);
    Entry.Ty = ArgTy;
    Entry.IsSExt = isSigned;
    Entry.IsZExt = !isSigned;
    Args.push_back(Entry);
  }
  if (Subtarget->isTargetWindows() && Args.size() >= 2)
    std::swap(Args[0], Args[1]);
  return Args;
}

SDValue ARMTargetLowering::LowerDivRem(SDValue Op, SelectionDAG &DAG) const {
  assert((Subtarget->isTargetAEABI() || Subtarget->isTargetAndroid() ||
          Subtarget->isTargetGNUAEABI() || Subtarget->isTargetMuslAEABI() ||
          Subtarget->isTargetWindows()) &&
         "Register-based DivRem lowering only");
  unsigned Opcode = Op->getOpcode();
  assert((Opcode == ISD::SDIVREM || Opcode == ISD::UDIVREM) &&
         "Invalid opcode for Div/Rem lowering");
  bool isSigned = (Opcode == ISD::SDIVREM);
  EVT VT = Op->getValueType(0);
  SDLoc dl(Op);

  if (VT == MVT::i64 && isa<ConstantSDNode>(Op.getOperand(1))) {
    SmallVector<SDValue> Result;
    if (expandDIVREMByConstant(Op.getNode(), Result, MVT::i32, DAG)) {
        SDValue Res0 =
            DAG.getNode(ISD::BUILD_PAIR, dl, VT, Result[0], Result[1]);
        SDValue Res1 =
            DAG.getNode(ISD::BUILD_PAIR, dl, VT, Result[2], Result[3]);
        return DAG.getNode(ISD::MERGE_VALUES, dl, Op->getVTList(),
                           {Res0, Res1});
    }
  }

  Type *Ty = VT.getTypeForEVT(*DAG.getContext());

  // If the target has hardware divide, use divide + multiply + subtract:
  //     div = a / b
  //     rem = a - b * div
  //     return {div, rem}
  // This should be lowered into UDIV/SDIV + MLS later on.
  bool hasDivide = Subtarget->isThumb() ? Subtarget->hasDivideInThumbMode()
                                        : Subtarget->hasDivideInARMMode();
  if (hasDivide && Op->getValueType(0).isSimple() &&
      Op->getSimpleValueType(0) == MVT::i32) {
    unsigned DivOpcode = isSigned ? ISD::SDIV : ISD::UDIV;
    const SDValue Dividend = Op->getOperand(0);
    const SDValue Divisor = Op->getOperand(1);
    SDValue Div = DAG.getNode(DivOpcode, dl, VT, Dividend, Divisor);
    SDValue Mul = DAG.getNode(ISD::MUL, dl, VT, Div, Divisor);
    SDValue Rem = DAG.getNode(ISD::SUB, dl, VT, Dividend, Mul);

    SDValue Values[2] = {Div, Rem};
    return DAG.getNode(ISD::MERGE_VALUES, dl, DAG.getVTList(VT, VT), Values);
  }

  RTLIB::Libcall LC = getDivRemLibcall(Op.getNode(),
                                       VT.getSimpleVT().SimpleTy);
  SDValue InChain = DAG.getEntryNode();

  TargetLowering::ArgListTy Args = getDivRemArgList(Op.getNode(),
                                                    DAG.getContext(),
                                                    Subtarget);

  SDValue Callee = DAG.getExternalSymbol(getLibcallName(LC),
                                         getPointerTy(DAG.getDataLayout()));

  Type *RetTy = StructType::get(Ty, Ty);

  if (Subtarget->isTargetWindows())
    InChain = WinDBZCheckDenominator(DAG, Op.getNode(), InChain);

  TargetLowering::CallLoweringInfo CLI(DAG);
  CLI.setDebugLoc(dl).setChain(InChain)
    .setCallee(getLibcallCallingConv(LC), RetTy, Callee, std::move(Args))
    .setInRegister().setSExtResult(isSigned).setZExtResult(!isSigned);

  std::pair<SDValue, SDValue> CallInfo = LowerCallTo(CLI);
  return CallInfo.first;
}

// Lowers REM using divmod helpers
// see RTABI section 4.2/4.3
SDValue ARMTargetLowering::LowerREM(SDNode *N, SelectionDAG &DAG) const {
  EVT VT = N->getValueType(0);

  if (VT == MVT::i64 && isa<ConstantSDNode>(N->getOperand(1))) {
    SmallVector<SDValue> Result;
    if (expandDIVREMByConstant(N, Result, MVT::i32, DAG))
        return DAG.getNode(ISD::BUILD_PAIR, SDLoc(N), N->getValueType(0),
                           Result[0], Result[1]);
  }

  // Build return types (div and rem)
  std::vector<Type*> RetTyParams;
  Type *RetTyElement;

  switch (VT.getSimpleVT().SimpleTy) {
  default: llvm_unreachable("Unexpected request for libcall!");
  case MVT::i8:   RetTyElement = Type::getInt8Ty(*DAG.getContext());  break;
  case MVT::i16:  RetTyElement = Type::getInt16Ty(*DAG.getContext()); break;
  case MVT::i32:  RetTyElement = Type::getInt32Ty(*DAG.getContext()); break;
  case MVT::i64:  RetTyElement = Type::getInt64Ty(*DAG.getContext()); break;
  }

  RetTyParams.push_back(RetTyElement);
  RetTyParams.push_back(RetTyElement);
  ArrayRef<Type*> ret = ArrayRef<Type*>(RetTyParams);
  Type *RetTy = StructType::get(*DAG.getContext(), ret);

  RTLIB::Libcall LC = getDivRemLibcall(N, N->getValueType(0).getSimpleVT().
                                                             SimpleTy);
  SDValue InChain = DAG.getEntryNode();
  TargetLowering::ArgListTy Args = getDivRemArgList(N, DAG.getContext(),
                                                    Subtarget);
  bool isSigned = N->getOpcode() == ISD::SREM;
  SDValue Callee = DAG.getExternalSymbol(getLibcallName(LC),
                                         getPointerTy(DAG.getDataLayout()));

  if (Subtarget->isTargetWindows())
    InChain = WinDBZCheckDenominator(DAG, N, InChain);

  // Lower call
  CallLoweringInfo CLI(DAG);
  CLI.setChain(InChain)
     .setCallee(CallingConv::ARM_AAPCS, RetTy, Callee, std::move(Args))
     .setSExtResult(isSigned).setZExtResult(!isSigned).setDebugLoc(SDLoc(N));
  std::pair<SDValue, SDValue> CallResult = LowerCallTo(CLI);

  // Return second (rem) result operand (first contains div)
  SDNode *ResNode = CallResult.first.getNode();
  assert(ResNode->getNumOperands() == 2 && "divmod should return two operands");
  return ResNode->getOperand(1);
}

SDValue
ARMTargetLowering::LowerDYNAMIC_STACKALLOC(SDValue Op, SelectionDAG &DAG) const {
  assert(Subtarget->isTargetWindows() && "unsupported target platform");
  SDLoc DL(Op);

  // Get the inputs.
  SDValue Chain = Op.getOperand(0);
  SDValue Size  = Op.getOperand(1);

  if (DAG.getMachineFunction().getFunction().hasFnAttribute(
          "no-stack-arg-probe")) {
    MaybeAlign Align =
        cast<ConstantSDNode>(Op.getOperand(2))->getMaybeAlignValue();
    SDValue SP = DAG.getCopyFromReg(Chain, DL, ARM::SP, MVT::i32);
    Chain = SP.getValue(1);
    SP = DAG.getNode(ISD::SUB, DL, MVT::i32, SP, Size);
    if (Align)
      SP =
          DAG.getNode(ISD::AND, DL, MVT::i32, SP.getValue(0),
                      DAG.getConstant(-(uint64_t)Align->value(), DL, MVT::i32));
    Chain = DAG.getCopyToReg(Chain, DL, ARM::SP, SP);
    SDValue Ops[2] = { SP, Chain };
    return DAG.getMergeValues(Ops, DL);
  }

  SDValue Words = DAG.getNode(ISD::SRL, DL, MVT::i32, Size,
                              DAG.getConstant(2, DL, MVT::i32));

  SDValue Glue;
  Chain = DAG.getCopyToReg(Chain, DL, ARM::R4, Words, Glue);
  Glue = Chain.getValue(1);

  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  Chain = DAG.getNode(ARMISD::WIN__CHKSTK, DL, NodeTys, Chain, Glue);

  SDValue NewSP = DAG.getCopyFromReg(Chain, DL, ARM::SP, MVT::i32);
  Chain = NewSP.getValue(1);

  SDValue Ops[2] = { NewSP, Chain };
  return DAG.getMergeValues(Ops, DL);
}

SDValue ARMTargetLowering::LowerFP_EXTEND(SDValue Op, SelectionDAG &DAG) const {
  bool IsStrict = Op->isStrictFPOpcode();
  SDValue SrcVal = Op.getOperand(IsStrict ? 1 : 0);
  const unsigned DstSz = Op.getValueType().getSizeInBits();
  const unsigned SrcSz = SrcVal.getValueType().getSizeInBits();
  assert(DstSz > SrcSz && DstSz <= 64 && SrcSz >= 16 &&
         "Unexpected type for custom-lowering FP_EXTEND");

  assert((!Subtarget->hasFP64() || !Subtarget->hasFPARMv8Base()) &&
         "With both FP DP and 16, any FP conversion is legal!");

  assert(!(DstSz == 32 && Subtarget->hasFP16()) &&
         "With FP16, 16 to 32 conversion is legal!");

  // Converting from 32 -> 64 is valid if we have FP64.
  if (SrcSz == 32 && DstSz == 64 && Subtarget->hasFP64()) {
    // FIXME: Remove this when we have strict fp instruction selection patterns
    if (IsStrict) {
      SDLoc Loc(Op);
      SDValue Result = DAG.getNode(ISD::FP_EXTEND,
                                   Loc, Op.getValueType(), SrcVal);
      return DAG.getMergeValues({Result, Op.getOperand(0)}, Loc);
    }
    return Op;
  }

  // Either we are converting from 16 -> 64, without FP16 and/or
  // FP.double-precision or without Armv8-fp. So we must do it in two
  // steps.
  // Or we are converting from 32 -> 64 without fp.double-precision or 16 -> 32
  // without FP16. So we must do a function call.
  SDLoc Loc(Op);
  RTLIB::Libcall LC;
  MakeLibCallOptions CallOptions;
  SDValue Chain = IsStrict ? Op.getOperand(0) : SDValue();
  for (unsigned Sz = SrcSz; Sz <= 32 && Sz < DstSz; Sz *= 2) {
    bool Supported = (Sz == 16 ? Subtarget->hasFP16() : Subtarget->hasFP64());
    MVT SrcVT = (Sz == 16 ? MVT::f16 : MVT::f32);
    MVT DstVT = (Sz == 16 ? MVT::f32 : MVT::f64);
    if (Supported) {
      if (IsStrict) {
        SrcVal = DAG.getNode(ISD::STRICT_FP_EXTEND, Loc,
                             {DstVT, MVT::Other}, {Chain, SrcVal});
        Chain = SrcVal.getValue(1);
      } else {
        SrcVal = DAG.getNode(ISD::FP_EXTEND, Loc, DstVT, SrcVal);
      }
    } else {
      LC = RTLIB::getFPEXT(SrcVT, DstVT);
      assert(LC != RTLIB::UNKNOWN_LIBCALL &&
             "Unexpected type for custom-lowering FP_EXTEND");
      std::tie(SrcVal, Chain) = makeLibCall(DAG, LC, DstVT, SrcVal, CallOptions,
                                            Loc, Chain);
    }
  }

  return IsStrict ? DAG.getMergeValues({SrcVal, Chain}, Loc) : SrcVal;
}

SDValue ARMTargetLowering::LowerFP_ROUND(SDValue Op, SelectionDAG &DAG) const {
  bool IsStrict = Op->isStrictFPOpcode();

  SDValue SrcVal = Op.getOperand(IsStrict ? 1 : 0);
  EVT SrcVT = SrcVal.getValueType();
  EVT DstVT = Op.getValueType();
  const unsigned DstSz = Op.getValueType().getSizeInBits();
  const unsigned SrcSz = SrcVT.getSizeInBits();
  (void)DstSz;
  assert(DstSz < SrcSz && SrcSz <= 64 && DstSz >= 16 &&
         "Unexpected type for custom-lowering FP_ROUND");

  assert((!Subtarget->hasFP64() || !Subtarget->hasFPARMv8Base()) &&
         "With both FP DP and 16, any FP conversion is legal!");

  SDLoc Loc(Op);

  // Instruction from 32 -> 16 if hasFP16 is valid
  if (SrcSz == 32 && Subtarget->hasFP16())
    return Op;

  // Lib call from 32 -> 16 / 64 -> [32, 16]
  RTLIB::Libcall LC = RTLIB::getFPROUND(SrcVT, DstVT);
  assert(LC != RTLIB::UNKNOWN_LIBCALL &&
         "Unexpected type for custom-lowering FP_ROUND");
  MakeLibCallOptions CallOptions;
  SDValue Chain = IsStrict ? Op.getOperand(0) : SDValue();
  SDValue Result;
  std::tie(Result, Chain) = makeLibCall(DAG, LC, DstVT, SrcVal, CallOptions,
                                        Loc, Chain);
  return IsStrict ? DAG.getMergeValues({Result, Chain}, Loc) : Result;
}

bool
ARMTargetLowering::isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const {
  // The ARM target isn't yet aware of offsets.
  return false;
}

bool ARM::isBitFieldInvertedMask(unsigned v) {
  if (v == 0xffffffff)
    return false;

  // there can be 1's on either or both "outsides", all the "inside"
  // bits must be 0's
  return isShiftedMask_32(~v);
}

/// isFPImmLegal - Returns true if the target can instruction select the
/// specified FP immediate natively. If false, the legalizer will
/// materialize the FP immediate as a load from a constant pool.
bool ARMTargetLowering::isFPImmLegal(const APFloat &Imm, EVT VT,
                                     bool ForCodeSize) const {
  if (!Subtarget->hasVFP3Base())
    return false;
  if (VT == MVT::f16 && Subtarget->hasFullFP16())
    return ARM_AM::getFP16Imm(Imm) != -1;
  if (VT == MVT::f32 && Subtarget->hasFullFP16() &&
      ARM_AM::getFP32FP16Imm(Imm) != -1)
    return true;
  if (VT == MVT::f32)
    return ARM_AM::getFP32Imm(Imm) != -1;
  if (VT == MVT::f64 && Subtarget->hasFP64())
    return ARM_AM::getFP64Imm(Imm) != -1;
  return false;
}

/// getTgtMemIntrinsic - Represent NEON load and store intrinsics as
/// MemIntrinsicNodes.  The associated MachineMemOperands record the alignment
/// specified in the intrinsic calls.
bool ARMTargetLowering::getTgtMemIntrinsic(IntrinsicInfo &Info,
                                           const CallInst &I,
                                           MachineFunction &MF,
                                           unsigned Intrinsic) const {
  switch (Intrinsic) {
  case Intrinsic::arm_neon_vld1:
  case Intrinsic::arm_neon_vld2:
  case Intrinsic::arm_neon_vld3:
  case Intrinsic::arm_neon_vld4:
  case Intrinsic::arm_neon_vld2lane:
  case Intrinsic::arm_neon_vld3lane:
  case Intrinsic::arm_neon_vld4lane:
  case Intrinsic::arm_neon_vld2dup:
  case Intrinsic::arm_neon_vld3dup:
  case Intrinsic::arm_neon_vld4dup: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    // Conservatively set memVT to the entire set of vectors loaded.
    auto &DL = I.getDataLayout();
    uint64_t NumElts = DL.getTypeSizeInBits(I.getType()) / 64;
    Info.memVT = EVT::getVectorVT(I.getType()->getContext(), MVT::i64, NumElts);
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Value *AlignArg = I.getArgOperand(I.arg_size() - 1);
    Info.align = cast<ConstantInt>(AlignArg)->getMaybeAlignValue();
    // volatile loads with NEON intrinsics not supported
    Info.flags = MachineMemOperand::MOLoad;
    return true;
  }
  case Intrinsic::arm_neon_vld1x2:
  case Intrinsic::arm_neon_vld1x3:
  case Intrinsic::arm_neon_vld1x4: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    // Conservatively set memVT to the entire set of vectors loaded.
    auto &DL = I.getDataLayout();
    uint64_t NumElts = DL.getTypeSizeInBits(I.getType()) / 64;
    Info.memVT = EVT::getVectorVT(I.getType()->getContext(), MVT::i64, NumElts);
    Info.ptrVal = I.getArgOperand(I.arg_size() - 1);
    Info.offset = 0;
    Info.align.reset();
    // volatile loads with NEON intrinsics not supported
    Info.flags = MachineMemOperand::MOLoad;
    return true;
  }
  case Intrinsic::arm_neon_vst1:
  case Intrinsic::arm_neon_vst2:
  case Intrinsic::arm_neon_vst3:
  case Intrinsic::arm_neon_vst4:
  case Intrinsic::arm_neon_vst2lane:
  case Intrinsic::arm_neon_vst3lane:
  case Intrinsic::arm_neon_vst4lane: {
    Info.opc = ISD::INTRINSIC_VOID;
    // Conservatively set memVT to the entire set of vectors stored.
    auto &DL = I.getDataLayout();
    unsigned NumElts = 0;
    for (unsigned ArgI = 1, ArgE = I.arg_size(); ArgI < ArgE; ++ArgI) {
      Type *ArgTy = I.getArgOperand(ArgI)->getType();
      if (!ArgTy->isVectorTy())
        break;
      NumElts += DL.getTypeSizeInBits(ArgTy) / 64;
    }
    Info.memVT = EVT::getVectorVT(I.getType()->getContext(), MVT::i64, NumElts);
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Value *AlignArg = I.getArgOperand(I.arg_size() - 1);
    Info.align = cast<ConstantInt>(AlignArg)->getMaybeAlignValue();
    // volatile stores with NEON intrinsics not supported
    Info.flags = MachineMemOperand::MOStore;
    return true;
  }
  case Intrinsic::arm_neon_vst1x2:
  case Intrinsic::arm_neon_vst1x3:
  case Intrinsic::arm_neon_vst1x4: {
    Info.opc = ISD::INTRINSIC_VOID;
    // Conservatively set memVT to the entire set of vectors stored.
    auto &DL = I.getDataLayout();
    unsigned NumElts = 0;
    for (unsigned ArgI = 1, ArgE = I.arg_size(); ArgI < ArgE; ++ArgI) {
      Type *ArgTy = I.getArgOperand(ArgI)->getType();
      if (!ArgTy->isVectorTy())
        break;
      NumElts += DL.getTypeSizeInBits(ArgTy) / 64;
    }
    Info.memVT = EVT::getVectorVT(I.getType()->getContext(), MVT::i64, NumElts);
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.align.reset();
    // volatile stores with NEON intrinsics not supported
    Info.flags = MachineMemOperand::MOStore;
    return true;
  }
  case Intrinsic::arm_mve_vld2q:
  case Intrinsic::arm_mve_vld4q: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    // Conservatively set memVT to the entire set of vectors loaded.
    Type *VecTy = cast<StructType>(I.getType())->getElementType(1);
    unsigned Factor = Intrinsic == Intrinsic::arm_mve_vld2q ? 2 : 4;
    Info.memVT = EVT::getVectorVT(VecTy->getContext(), MVT::i64, Factor * 2);
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.align = Align(VecTy->getScalarSizeInBits() / 8);
    // volatile loads with MVE intrinsics not supported
    Info.flags = MachineMemOperand::MOLoad;
    return true;
  }
  case Intrinsic::arm_mve_vst2q:
  case Intrinsic::arm_mve_vst4q: {
    Info.opc = ISD::INTRINSIC_VOID;
    // Conservatively set memVT to the entire set of vectors stored.
    Type *VecTy = I.getArgOperand(1)->getType();
    unsigned Factor = Intrinsic == Intrinsic::arm_mve_vst2q ? 2 : 4;
    Info.memVT = EVT::getVectorVT(VecTy->getContext(), MVT::i64, Factor * 2);
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.align = Align(VecTy->getScalarSizeInBits() / 8);
    // volatile stores with MVE intrinsics not supported
    Info.flags = MachineMemOperand::MOStore;
    return true;
  }
  case Intrinsic::arm_mve_vldr_gather_base:
  case Intrinsic::arm_mve_vldr_gather_base_predicated: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.ptrVal = nullptr;
    Info.memVT = MVT::getVT(I.getType());
    Info.align = Align(1);
    Info.flags |= MachineMemOperand::MOLoad;
    return true;
  }
  case Intrinsic::arm_mve_vldr_gather_base_wb:
  case Intrinsic::arm_mve_vldr_gather_base_wb_predicated: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.ptrVal = nullptr;
    Info.memVT = MVT::getVT(I.getType()->getContainedType(0));
    Info.align = Align(1);
    Info.flags |= MachineMemOperand::MOLoad;
    return true;
  }
  case Intrinsic::arm_mve_vldr_gather_offset:
  case Intrinsic::arm_mve_vldr_gather_offset_predicated: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.ptrVal = nullptr;
    MVT DataVT = MVT::getVT(I.getType());
    unsigned MemSize = cast<ConstantInt>(I.getArgOperand(2))->getZExtValue();
    Info.memVT = MVT::getVectorVT(MVT::getIntegerVT(MemSize),
                                  DataVT.getVectorNumElements());
    Info.align = Align(1);
    Info.flags |= MachineMemOperand::MOLoad;
    return true;
  }
  case Intrinsic::arm_mve_vstr_scatter_base:
  case Intrinsic::arm_mve_vstr_scatter_base_predicated: {
    Info.opc = ISD::INTRINSIC_VOID;
    Info.ptrVal = nullptr;
    Info.memVT = MVT::getVT(I.getArgOperand(2)->getType());
    Info.align = Align(1);
    Info.flags |= MachineMemOperand::MOStore;
    return true;
  }
  case Intrinsic::arm_mve_vstr_scatter_base_wb:
  case Intrinsic::arm_mve_vstr_scatter_base_wb_predicated: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.ptrVal = nullptr;
    Info.memVT = MVT::getVT(I.getArgOperand(2)->getType());
    Info.align = Align(1);
    Info.flags |= MachineMemOperand::MOStore;
    return true;
  }
  case Intrinsic::arm_mve_vstr_scatter_offset:
  case Intrinsic::arm_mve_vstr_scatter_offset_predicated: {
    Info.opc = ISD::INTRINSIC_VOID;
    Info.ptrVal = nullptr;
    MVT DataVT = MVT::getVT(I.getArgOperand(2)->getType());
    unsigned MemSize = cast<ConstantInt>(I.getArgOperand(3))->getZExtValue();
    Info.memVT = MVT::getVectorVT(MVT::getIntegerVT(MemSize),
                                  DataVT.getVectorNumElements());
    Info.align = Align(1);
    Info.flags |= MachineMemOperand::MOStore;
    return true;
  }
  case Intrinsic::arm_ldaex:
  case Intrinsic::arm_ldrex: {
    auto &DL = I.getDataLayout();
    Type *ValTy = I.getParamElementType(0);
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::getVT(ValTy);
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.align = DL.getABITypeAlign(ValTy);
    Info.flags = MachineMemOperand::MOLoad | MachineMemOperand::MOVolatile;
    return true;
  }
  case Intrinsic::arm_stlex:
  case Intrinsic::arm_strex: {
    auto &DL = I.getDataLayout();
    Type *ValTy = I.getParamElementType(1);
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::getVT(ValTy);
    Info.ptrVal = I.getArgOperand(1);
    Info.offset = 0;
    Info.align = DL.getABITypeAlign(ValTy);
    Info.flags = MachineMemOperand::MOStore | MachineMemOperand::MOVolatile;
    return true;
  }
  case Intrinsic::arm_stlexd:
  case Intrinsic::arm_strexd:
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::i64;
    Info.ptrVal = I.getArgOperand(2);
    Info.offset = 0;
    Info.align = Align(8);
    Info.flags = MachineMemOperand::MOStore | MachineMemOperand::MOVolatile;
    return true;

  case Intrinsic::arm_ldaexd:
  case Intrinsic::arm_ldrexd:
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::i64;
    Info.ptrVal = I.getArgOperand(0);
    Info.offset = 0;
    Info.align = Align(8);
    Info.flags = MachineMemOperand::MOLoad | MachineMemOperand::MOVolatile;
    return true;

  default:
    break;
  }

  return false;
}

/// Returns true if it is beneficial to convert a load of a constant
/// to just the constant itself.
bool ARMTargetLowering::shouldConvertConstantLoadToIntImm(const APInt &Imm,
                                                          Type *Ty) const {
  assert(Ty->isIntegerTy());

  unsigned Bits = Ty->getPrimitiveSizeInBits();
  if (Bits == 0 || Bits > 32)
    return false;
  return true;
}

bool ARMTargetLowering::isExtractSubvectorCheap(EVT ResVT, EVT SrcVT,
                                                unsigned Index) const {
  if (!isOperationLegalOrCustom(ISD::EXTRACT_SUBVECTOR, ResVT))
    return false;

  return (Index == 0 || Index == ResVT.getVectorNumElements());
}

Instruction *ARMTargetLowering::makeDMB(IRBuilderBase &Builder,
                                        ARM_MB::MemBOpt Domain) const {
  Module *M = Builder.GetInsertBlock()->getParent()->getParent();

  // First, if the target has no DMB, see what fallback we can use.
  if (!Subtarget->hasDataBarrier()) {
    // Some ARMv6 cpus can support data barriers with an mcr instruction.
    // Thumb1 and pre-v6 ARM mode use a libcall instead and should never get
    // here.
    if (Subtarget->hasV6Ops() && !Subtarget->isThumb()) {
      Function *MCR = Intrinsic::getDeclaration(M, Intrinsic::arm_mcr);
      Value* args[6] = {Builder.getInt32(15), Builder.getInt32(0),
                        Builder.getInt32(0), Builder.getInt32(7),
                        Builder.getInt32(10), Builder.getInt32(5)};
      return Builder.CreateCall(MCR, args);
    } else {
      // Instead of using barriers, atomic accesses on these subtargets use
      // libcalls.
      llvm_unreachable("makeDMB on a target so old that it has no barriers");
    }
  } else {
    Function *DMB = Intrinsic::getDeclaration(M, Intrinsic::arm_dmb);
    // Only a full system barrier exists in the M-class architectures.
    Domain = Subtarget->isMClass() ? ARM_MB::SY : Domain;
    Constant *CDomain = Builder.getInt32(Domain);
    return Builder.CreateCall(DMB, CDomain);
  }
}

// Based on http://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html
Instruction *ARMTargetLowering::emitLeadingFence(IRBuilderBase &Builder,
                                                 Instruction *Inst,
                                                 AtomicOrdering Ord) const {
  switch (Ord) {
  case AtomicOrdering::NotAtomic:
  case AtomicOrdering::Unordered:
    llvm_unreachable("Invalid fence: unordered/non-atomic");
  case AtomicOrdering::Monotonic:
  case AtomicOrdering::Acquire:
    return nullptr; // Nothing to do
  case AtomicOrdering::SequentiallyConsistent:
    if (!Inst->hasAtomicStore())
      return nullptr; // Nothing to do
    [[fallthrough]];
  case AtomicOrdering::Release:
  case AtomicOrdering::AcquireRelease:
    if (Subtarget->preferISHSTBarriers())
      return makeDMB(Builder, ARM_MB::ISHST);
    // FIXME: add a comment with a link to documentation justifying this.
    else
      return makeDMB(Builder, ARM_MB::ISH);
  }
  llvm_unreachable("Unknown fence ordering in emitLeadingFence");
}

Instruction *ARMTargetLowering::emitTrailingFence(IRBuilderBase &Builder,
                                                  Instruction *Inst,
                                                  AtomicOrdering Ord) const {
  switch (Ord) {
  case AtomicOrdering::NotAtomic:
  case AtomicOrdering::Unordered:
    llvm_unreachable("Invalid fence: unordered/not-atomic");
  case AtomicOrdering::Monotonic:
  case AtomicOrdering::Release:
    return nullptr; // Nothing to do
  case AtomicOrdering::Acquire:
  case AtomicOrdering::AcquireRelease:
  case AtomicOrdering::SequentiallyConsistent:
    return makeDMB(Builder, ARM_MB::ISH);
  }
  llvm_unreachable("Unknown fence ordering in emitTrailingFence");
}

// Loads and stores less than 64-bits are already atomic; ones above that
// are doomed anyway, so defer to the default libcall and blame the OS when
// things go wrong. Cortex M doesn't have ldrexd/strexd though, so don't emit
// anything for those.
TargetLoweringBase::AtomicExpansionKind
ARMTargetLowering::shouldExpandAtomicStoreInIR(StoreInst *SI) const {
  bool has64BitAtomicStore;
  if (Subtarget->isMClass())
    has64BitAtomicStore = false;
  else if (Subtarget->isThumb())
    has64BitAtomicStore = Subtarget->hasV7Ops();
  else
    has64BitAtomicStore = Subtarget->hasV6Ops();

  unsigned Size = SI->getValueOperand()->getType()->getPrimitiveSizeInBits();
  return Size == 64 && has64BitAtomicStore ? AtomicExpansionKind::Expand
                                           : AtomicExpansionKind::None;
}

// Loads and stores less than 64-bits are already atomic; ones above that
// are doomed anyway, so defer to the default libcall and blame the OS when
// things go wrong. Cortex M doesn't have ldrexd/strexd though, so don't emit
// anything for those.
// FIXME: ldrd and strd are atomic if the CPU has LPAE (e.g. A15 has that
// guarantee, see DDI0406C ARM architecture reference manual,
// sections A8.8.72-74 LDRD)
TargetLowering::AtomicExpansionKind
ARMTargetLowering::shouldExpandAtomicLoadInIR(LoadInst *LI) const {
  bool has64BitAtomicLoad;
  if (Subtarget->isMClass())
    has64BitAtomicLoad = false;
  else if (Subtarget->isThumb())
    has64BitAtomicLoad = Subtarget->hasV7Ops();
  else
    has64BitAtomicLoad = Subtarget->hasV6Ops();

  unsigned Size = LI->getType()->getPrimitiveSizeInBits();
  return (Size == 64 && has64BitAtomicLoad) ? AtomicExpansionKind::LLOnly
                                            : AtomicExpansionKind::None;
}

// For the real atomic operations, we have ldrex/strex up to 32 bits,
// and up to 64 bits on the non-M profiles
TargetLowering::AtomicExpansionKind
ARMTargetLowering::shouldExpandAtomicRMWInIR(AtomicRMWInst *AI) const {
  if (AI->isFloatingPointOperation())
    return AtomicExpansionKind::CmpXChg;

  unsigned Size = AI->getType()->getPrimitiveSizeInBits();
  bool hasAtomicRMW;
  if (Subtarget->isMClass())
    hasAtomicRMW = Subtarget->hasV8MBaselineOps();
  else if (Subtarget->isThumb())
    hasAtomicRMW = Subtarget->hasV7Ops();
  else
    hasAtomicRMW = Subtarget->hasV6Ops();
  if (Size <= (Subtarget->isMClass() ? 32U : 64U) && hasAtomicRMW) {
    // At -O0, fast-regalloc cannot cope with the live vregs necessary to
    // implement atomicrmw without spilling. If the target address is also on
    // the stack and close enough to the spill slot, this can lead to a
    // situation where the monitor always gets cleared and the atomic operation
    // can never succeed. So at -O0 lower this operation to a CAS loop.
    if (getTargetMachine().getOptLevel() == CodeGenOptLevel::None)
      return AtomicExpansionKind::CmpXChg;
    return AtomicExpansionKind::LLSC;
  }
  return AtomicExpansionKind::None;
}

// Similar to shouldExpandAtomicRMWInIR, ldrex/strex can be used  up to 32
// bits, and up to 64 bits on the non-M profiles.
TargetLowering::AtomicExpansionKind
ARMTargetLowering::shouldExpandAtomicCmpXchgInIR(AtomicCmpXchgInst *AI) const {
  // At -O0, fast-regalloc cannot cope with the live vregs necessary to
  // implement cmpxchg without spilling. If the address being exchanged is also
  // on the stack and close enough to the spill slot, this can lead to a
  // situation where the monitor always gets cleared and the atomic operation
  // can never succeed. So at -O0 we need a late-expanded pseudo-inst instead.
  unsigned Size = AI->getOperand(1)->getType()->getPrimitiveSizeInBits();
  bool HasAtomicCmpXchg;
  if (Subtarget->isMClass())
    HasAtomicCmpXchg = Subtarget->hasV8MBaselineOps();
  else if (Subtarget->isThumb())
    HasAtomicCmpXchg = Subtarget->hasV7Ops();
  else
    HasAtomicCmpXchg = Subtarget->hasV6Ops();
  if (getTargetMachine().getOptLevel() != CodeGenOptLevel::None &&
      HasAtomicCmpXchg && Size <= (Subtarget->isMClass() ? 32U : 64U))
    return AtomicExpansionKind::LLSC;
  return AtomicExpansionKind::None;
}

bool ARMTargetLowering::shouldInsertFencesForAtomic(
    const Instruction *I) const {
  return InsertFencesForAtomic;
}

bool ARMTargetLowering::useLoadStackGuardNode() const {
  if (Subtarget->getTargetTriple().isOSOpenBSD())
    return false;
  // ROPI/RWPI are not supported currently.
  return !Subtarget->isROPI() && !Subtarget->isRWPI();
}

void ARMTargetLowering::insertSSPDeclarations(Module &M) const {
  if (!Subtarget->getTargetTriple().isWindowsMSVCEnvironment())
    return TargetLowering::insertSSPDeclarations(M);

  // MSVC CRT has a global variable holding security cookie.
  M.getOrInsertGlobal("__security_cookie",
                      PointerType::getUnqual(M.getContext()));

  // MSVC CRT has a function to validate security cookie.
  FunctionCallee SecurityCheckCookie = M.getOrInsertFunction(
      "__security_check_cookie", Type::getVoidTy(M.getContext()),
      PointerType::getUnqual(M.getContext()));
  if (Function *F = dyn_cast<Function>(SecurityCheckCookie.getCallee()))
    F->addParamAttr(0, Attribute::AttrKind::InReg);
}

Value *ARMTargetLowering::getSDagStackGuard(const Module &M) const {
  // MSVC CRT has a global variable holding security cookie.
  if (Subtarget->getTargetTriple().isWindowsMSVCEnvironment())
    return M.getGlobalVariable("__security_cookie");
  return TargetLowering::getSDagStackGuard(M);
}

Function *ARMTargetLowering::getSSPStackGuardCheck(const Module &M) const {
  // MSVC CRT has a function to validate security cookie.
  if (Subtarget->getTargetTriple().isWindowsMSVCEnvironment())
    return M.getFunction("__security_check_cookie");
  return TargetLowering::getSSPStackGuardCheck(M);
}

bool ARMTargetLowering::canCombineStoreAndExtract(Type *VectorTy, Value *Idx,
                                                  unsigned &Cost) const {
  // If we do not have NEON, vector types are not natively supported.
  if (!Subtarget->hasNEON())
    return false;

  // Floating point values and vector values map to the same register file.
  // Therefore, although we could do a store extract of a vector type, this is
  // better to leave at float as we have more freedom in the addressing mode for
  // those.
  if (VectorTy->isFPOrFPVectorTy())
    return false;

  // If the index is unknown at compile time, this is very expensive to lower
  // and it is not possible to combine the store with the extract.
  if (!isa<ConstantInt>(Idx))
    return false;

  assert(VectorTy->isVectorTy() && "VectorTy is not a vector type");
  unsigned BitWidth = VectorTy->getPrimitiveSizeInBits().getFixedValue();
  // We can do a store + vector extract on any vector that fits perfectly in a D
  // or Q register.
  if (BitWidth == 64 || BitWidth == 128) {
    Cost = 0;
    return true;
  }
  return false;
}

bool ARMTargetLowering::isCheapToSpeculateCttz(Type *Ty) const {
  return Subtarget->hasV6T2Ops();
}

bool ARMTargetLowering::isCheapToSpeculateCtlz(Type *Ty) const {
  return Subtarget->hasV6T2Ops();
}

bool ARMTargetLowering::isMaskAndCmp0FoldingBeneficial(
    const Instruction &AndI) const {
  if (!Subtarget->hasV7Ops())
    return false;

  // Sink the `and` instruction only if the mask would fit into a modified
  // immediate operand.
  ConstantInt *Mask = dyn_cast<ConstantInt>(AndI.getOperand(1));
  if (!Mask || Mask->getValue().getBitWidth() > 32u)
    return false;
  auto MaskVal = unsigned(Mask->getValue().getZExtValue());
  return (Subtarget->isThumb2() ? ARM_AM::getT2SOImmVal(MaskVal)
                                : ARM_AM::getSOImmVal(MaskVal)) != -1;
}

TargetLowering::ShiftLegalizationStrategy
ARMTargetLowering::preferredShiftLegalizationStrategy(
    SelectionDAG &DAG, SDNode *N, unsigned ExpansionFactor) const {
  if (Subtarget->hasMinSize() && !Subtarget->isTargetWindows())
    return ShiftLegalizationStrategy::LowerToLibcall;
  return TargetLowering::preferredShiftLegalizationStrategy(DAG, N,
                                                            ExpansionFactor);
}

Value *ARMTargetLowering::emitLoadLinked(IRBuilderBase &Builder, Type *ValueTy,
                                         Value *Addr,
                                         AtomicOrdering Ord) const {
  Module *M = Builder.GetInsertBlock()->getParent()->getParent();
  bool IsAcquire = isAcquireOrStronger(Ord);

  // Since i64 isn't legal and intrinsics don't get type-lowered, the ldrexd
  // intrinsic must return {i32, i32} and we have to recombine them into a
  // single i64 here.
  if (ValueTy->getPrimitiveSizeInBits() == 64) {
    Intrinsic::ID Int =
        IsAcquire ? Intrinsic::arm_ldaexd : Intrinsic::arm_ldrexd;
    Function *Ldrex = Intrinsic::getDeclaration(M, Int);

    Value *LoHi = Builder.CreateCall(Ldrex, Addr, "lohi");

    Value *Lo = Builder.CreateExtractValue(LoHi, 0, "lo");
    Value *Hi = Builder.CreateExtractValue(LoHi, 1, "hi");
    if (!Subtarget->isLittle())
      std::swap (Lo, Hi);
    Lo = Builder.CreateZExt(Lo, ValueTy, "lo64");
    Hi = Builder.CreateZExt(Hi, ValueTy, "hi64");
    return Builder.CreateOr(
        Lo, Builder.CreateShl(Hi, ConstantInt::get(ValueTy, 32)), "val64");
  }

  Type *Tys[] = { Addr->getType() };
  Intrinsic::ID Int = IsAcquire ? Intrinsic::arm_ldaex : Intrinsic::arm_ldrex;
  Function *Ldrex = Intrinsic::getDeclaration(M, Int, Tys);
  CallInst *CI = Builder.CreateCall(Ldrex, Addr);

  CI->addParamAttr(
      0, Attribute::get(M->getContext(), Attribute::ElementType, ValueTy));
  return Builder.CreateTruncOrBitCast(CI, ValueTy);
}

void ARMTargetLowering::emitAtomicCmpXchgNoStoreLLBalance(
    IRBuilderBase &Builder) const {
  if (!Subtarget->hasV7Ops())
    return;
  Module *M = Builder.GetInsertBlock()->getParent()->getParent();
  Builder.CreateCall(Intrinsic::getDeclaration(M, Intrinsic::arm_clrex));
}

Value *ARMTargetLowering::emitStoreConditional(IRBuilderBase &Builder,
                                               Value *Val, Value *Addr,
                                               AtomicOrdering Ord) const {
  Module *M = Builder.GetInsertBlock()->getParent()->getParent();
  bool IsRelease = isReleaseOrStronger(Ord);

  // Since the intrinsics must have legal type, the i64 intrinsics take two
  // parameters: "i32, i32". We must marshal Val into the appropriate form
  // before the call.
  if (Val->getType()->getPrimitiveSizeInBits() == 64) {
    Intrinsic::ID Int =
        IsRelease ? Intrinsic::arm_stlexd : Intrinsic::arm_strexd;
    Function *Strex = Intrinsic::getDeclaration(M, Int);
    Type *Int32Ty = Type::getInt32Ty(M->getContext());

    Value *Lo = Builder.CreateTrunc(Val, Int32Ty, "lo");
    Value *Hi = Builder.CreateTrunc(Builder.CreateLShr(Val, 32), Int32Ty, "hi");
    if (!Subtarget->isLittle())
      std::swap(Lo, Hi);
    return Builder.CreateCall(Strex, {Lo, Hi, Addr});
  }

  Intrinsic::ID Int = IsRelease ? Intrinsic::arm_stlex : Intrinsic::arm_strex;
  Type *Tys[] = { Addr->getType() };
  Function *Strex = Intrinsic::getDeclaration(M, Int, Tys);

  CallInst *CI = Builder.CreateCall(
      Strex, {Builder.CreateZExtOrBitCast(
                  Val, Strex->getFunctionType()->getParamType(0)),
              Addr});
  CI->addParamAttr(1, Attribute::get(M->getContext(), Attribute::ElementType,
                                     Val->getType()));
  return CI;
}


bool ARMTargetLowering::alignLoopsWithOptSize() const {
  return Subtarget->isMClass();
}

/// A helper function for determining the number of interleaved accesses we
/// will generate when lowering accesses of the given type.
unsigned
ARMTargetLowering::getNumInterleavedAccesses(VectorType *VecTy,
                                             const DataLayout &DL) const {
  return (DL.getTypeSizeInBits(VecTy) + 127) / 128;
}

bool ARMTargetLowering::isLegalInterleavedAccessType(
    unsigned Factor, FixedVectorType *VecTy, Align Alignment,
    const DataLayout &DL) const {

  unsigned VecSize = DL.getTypeSizeInBits(VecTy);
  unsigned ElSize = DL.getTypeSizeInBits(VecTy->getElementType());

  if (!Subtarget->hasNEON() && !Subtarget->hasMVEIntegerOps())
    return false;

  // Ensure the vector doesn't have f16 elements. Even though we could do an
  // i16 vldN, we can't hold the f16 vectors and will end up converting via
  // f32.
  if (Subtarget->hasNEON() && VecTy->getElementType()->isHalfTy())
    return false;
  if (Subtarget->hasMVEIntegerOps() && Factor == 3)
    return false;

  // Ensure the number of vector elements is greater than 1.
  if (VecTy->getNumElements() < 2)
    return false;

  // Ensure the element type is legal.
  if (ElSize != 8 && ElSize != 16 && ElSize != 32)
    return false;
  // And the alignment if high enough under MVE.
  if (Subtarget->hasMVEIntegerOps() && Alignment < ElSize / 8)
    return false;

  // Ensure the total vector size is 64 or a multiple of 128. Types larger than
  // 128 will be split into multiple interleaved accesses.
  if (Subtarget->hasNEON() && VecSize == 64)
    return true;
  return VecSize % 128 == 0;
}

unsigned ARMTargetLowering::getMaxSupportedInterleaveFactor() const {
  if (Subtarget->hasNEON())
    return 4;
  if (Subtarget->hasMVEIntegerOps())
    return MVEMaxSupportedInterleaveFactor;
  return TargetLoweringBase::getMaxSupportedInterleaveFactor();
}

/// Lower an interleaved load into a vldN intrinsic.
///
/// E.g. Lower an interleaved load (Factor = 2):
///        %wide.vec = load <8 x i32>, <8 x i32>* %ptr, align 4
///        %v0 = shuffle %wide.vec, undef, <0, 2, 4, 6>  ; Extract even elements
///        %v1 = shuffle %wide.vec, undef, <1, 3, 5, 7>  ; Extract odd elements
///
///      Into:
///        %vld2 = { <4 x i32>, <4 x i32> } call llvm.arm.neon.vld2(%ptr, 4)
///        %vec0 = extractelement { <4 x i32>, <4 x i32> } %vld2, i32 0
///        %vec1 = extractelement { <4 x i32>, <4 x i32> } %vld2, i32 1
bool ARMTargetLowering::lowerInterleavedLoad(
    LoadInst *LI, ArrayRef<ShuffleVectorInst *> Shuffles,
    ArrayRef<unsigned> Indices, unsigned Factor) const {
  assert(Factor >= 2 && Factor <= getMaxSupportedInterleaveFactor() &&
         "Invalid interleave factor");
  assert(!Shuffles.empty() && "Empty shufflevector input");
  assert(Shuffles.size() == Indices.size() &&
         "Unmatched number of shufflevectors and indices");

  auto *VecTy = cast<FixedVectorType>(Shuffles[0]->getType());
  Type *EltTy = VecTy->getElementType();

  const DataLayout &DL = LI->getDataLayout();
  Align Alignment = LI->getAlign();

  // Skip if we do not have NEON and skip illegal vector types. We can
  // "legalize" wide vector types into multiple interleaved accesses as long as
  // the vector types are divisible by 128.
  if (!isLegalInterleavedAccessType(Factor, VecTy, Alignment, DL))
    return false;

  unsigned NumLoads = getNumInterleavedAccesses(VecTy, DL);

  // A pointer vector can not be the return type of the ldN intrinsics. Need to
  // load integer vectors first and then convert to pointer vectors.
  if (EltTy->isPointerTy())
    VecTy = FixedVectorType::get(DL.getIntPtrType(EltTy), VecTy);

  IRBuilder<> Builder(LI);

  // The base address of the load.
  Value *BaseAddr = LI->getPointerOperand();

  if (NumLoads > 1) {
    // If we're going to generate more than one load, reset the sub-vector type
    // to something legal.
    VecTy = FixedVectorType::get(VecTy->getElementType(),
                                 VecTy->getNumElements() / NumLoads);
  }

  assert(isTypeLegal(EVT::getEVT(VecTy)) && "Illegal vldN vector type!");

  auto createLoadIntrinsic = [&](Value *BaseAddr) {
    if (Subtarget->hasNEON()) {
      Type *PtrTy = Builder.getPtrTy(LI->getPointerAddressSpace());
      Type *Tys[] = {VecTy, PtrTy};
      static const Intrinsic::ID LoadInts[3] = {Intrinsic::arm_neon_vld2,
                                                Intrinsic::arm_neon_vld3,
                                                Intrinsic::arm_neon_vld4};
      Function *VldnFunc =
          Intrinsic::getDeclaration(LI->getModule(), LoadInts[Factor - 2], Tys);

      SmallVector<Value *, 2> Ops;
      Ops.push_back(BaseAddr);
      Ops.push_back(Builder.getInt32(LI->getAlign().value()));

      return Builder.CreateCall(VldnFunc, Ops, "vldN");
    } else {
      assert((Factor == 2 || Factor == 4) &&
             "expected interleave factor of 2 or 4 for MVE");
      Intrinsic::ID LoadInts =
          Factor == 2 ? Intrinsic::arm_mve_vld2q : Intrinsic::arm_mve_vld4q;
      Type *PtrTy = Builder.getPtrTy(LI->getPointerAddressSpace());
      Type *Tys[] = {VecTy, PtrTy};
      Function *VldnFunc =
          Intrinsic::getDeclaration(LI->getModule(), LoadInts, Tys);

      SmallVector<Value *, 2> Ops;
      Ops.push_back(BaseAddr);
      return Builder.CreateCall(VldnFunc, Ops, "vldN");
    }
  };

  // Holds sub-vectors extracted from the load intrinsic return values. The
  // sub-vectors are associated with the shufflevector instructions they will
  // replace.
  DenseMap<ShuffleVectorInst *, SmallVector<Value *, 4>> SubVecs;

  for (unsigned LoadCount = 0; LoadCount < NumLoads; ++LoadCount) {
    // If we're generating more than one load, compute the base address of
    // subsequent loads as an offset from the previous.
    if (LoadCount > 0)
      BaseAddr = Builder.CreateConstGEP1_32(VecTy->getElementType(), BaseAddr,
                                            VecTy->getNumElements() * Factor);

    CallInst *VldN = createLoadIntrinsic(BaseAddr);

    // Replace uses of each shufflevector with the corresponding vector loaded
    // by ldN.
    for (unsigned i = 0; i < Shuffles.size(); i++) {
      ShuffleVectorInst *SV = Shuffles[i];
      unsigned Index = Indices[i];

      Value *SubVec = Builder.CreateExtractValue(VldN, Index);

      // Convert the integer vector to pointer vector if the element is pointer.
      if (EltTy->isPointerTy())
        SubVec = Builder.CreateIntToPtr(
            SubVec,
            FixedVectorType::get(SV->getType()->getElementType(), VecTy));

      SubVecs[SV].push_back(SubVec);
    }
  }

  // Replace uses of the shufflevector instructions with the sub-vectors
  // returned by the load intrinsic. If a shufflevector instruction is
  // associated with more than one sub-vector, those sub-vectors will be
  // concatenated into a single wide vector.
  for (ShuffleVectorInst *SVI : Shuffles) {
    auto &SubVec = SubVecs[SVI];
    auto *WideVec =
        SubVec.size() > 1 ? concatenateVectors(Builder, SubVec) : SubVec[0];
    SVI->replaceAllUsesWith(WideVec);
  }

  return true;
}

/// Lower an interleaved store into a vstN intrinsic.
///
/// E.g. Lower an interleaved store (Factor = 3):
///        %i.vec = shuffle <8 x i32> %v0, <8 x i32> %v1,
///                                  <0, 4, 8, 1, 5, 9, 2, 6, 10, 3, 7, 11>
///        store <12 x i32> %i.vec, <12 x i32>* %ptr, align 4
///
///      Into:
///        %sub.v0 = shuffle <8 x i32> %v0, <8 x i32> v1, <0, 1, 2, 3>
///        %sub.v1 = shuffle <8 x i32> %v0, <8 x i32> v1, <4, 5, 6, 7>
///        %sub.v2 = shuffle <8 x i32> %v0, <8 x i32> v1, <8, 9, 10, 11>
///        call void llvm.arm.neon.vst3(%ptr, %sub.v0, %sub.v1, %sub.v2, 4)
///
/// Note that the new shufflevectors will be removed and we'll only generate one
/// vst3 instruction in CodeGen.
///
/// Example for a more general valid mask (Factor 3). Lower:
///        %i.vec = shuffle <32 x i32> %v0, <32 x i32> %v1,
///                 <4, 32, 16, 5, 33, 17, 6, 34, 18, 7, 35, 19>
///        store <12 x i32> %i.vec, <12 x i32>* %ptr
///
///      Into:
///        %sub.v0 = shuffle <32 x i32> %v0, <32 x i32> v1, <4, 5, 6, 7>
///        %sub.v1 = shuffle <32 x i32> %v0, <32 x i32> v1, <32, 33, 34, 35>
///        %sub.v2 = shuffle <32 x i32> %v0, <32 x i32> v1, <16, 17, 18, 19>
///        call void llvm.arm.neon.vst3(%ptr, %sub.v0, %sub.v1, %sub.v2, 4)
bool ARMTargetLowering::lowerInterleavedStore(StoreInst *SI,
                                              ShuffleVectorInst *SVI,
                                              unsigned Factor) const {
  assert(Factor >= 2 && Factor <= getMaxSupportedInterleaveFactor() &&
         "Invalid interleave factor");

  auto *VecTy = cast<FixedVectorType>(SVI->getType());
  assert(VecTy->getNumElements() % Factor == 0 && "Invalid interleaved store");

  unsigned LaneLen = VecTy->getNumElements() / Factor;
  Type *EltTy = VecTy->getElementType();
  auto *SubVecTy = FixedVectorType::get(EltTy, LaneLen);

  const DataLayout &DL = SI->getDataLayout();
  Align Alignment = SI->getAlign();

  // Skip if we do not have NEON and skip illegal vector types. We can
  // "legalize" wide vector types into multiple interleaved accesses as long as
  // the vector types are divisible by 128.
  if (!isLegalInterleavedAccessType(Factor, SubVecTy, Alignment, DL))
    return false;

  unsigned NumStores = getNumInterleavedAccesses(SubVecTy, DL);

  Value *Op0 = SVI->getOperand(0);
  Value *Op1 = SVI->getOperand(1);
  IRBuilder<> Builder(SI);

  // StN intrinsics don't support pointer vectors as arguments. Convert pointer
  // vectors to integer vectors.
  if (EltTy->isPointerTy()) {
    Type *IntTy = DL.getIntPtrType(EltTy);

    // Convert to the corresponding integer vector.
    auto *IntVecTy =
        FixedVectorType::get(IntTy, cast<FixedVectorType>(Op0->getType()));
    Op0 = Builder.CreatePtrToInt(Op0, IntVecTy);
    Op1 = Builder.CreatePtrToInt(Op1, IntVecTy);

    SubVecTy = FixedVectorType::get(IntTy, LaneLen);
  }

  // The base address of the store.
  Value *BaseAddr = SI->getPointerOperand();

  if (NumStores > 1) {
    // If we're going to generate more than one store, reset the lane length
    // and sub-vector type to something legal.
    LaneLen /= NumStores;
    SubVecTy = FixedVectorType::get(SubVecTy->getElementType(), LaneLen);
  }

  assert(isTypeLegal(EVT::getEVT(SubVecTy)) && "Illegal vstN vector type!");

  auto Mask = SVI->getShuffleMask();

  auto createStoreIntrinsic = [&](Value *BaseAddr,
                                  SmallVectorImpl<Value *> &Shuffles) {
    if (Subtarget->hasNEON()) {
      static const Intrinsic::ID StoreInts[3] = {Intrinsic::arm_neon_vst2,
                                                 Intrinsic::arm_neon_vst3,
                                                 Intrinsic::arm_neon_vst4};
      Type *PtrTy = Builder.getPtrTy(SI->getPointerAddressSpace());
      Type *Tys[] = {PtrTy, SubVecTy};

      Function *VstNFunc = Intrinsic::getDeclaration(
          SI->getModule(), StoreInts[Factor - 2], Tys);

      SmallVector<Value *, 6> Ops;
      Ops.push_back(BaseAddr);
      append_range(Ops, Shuffles);
      Ops.push_back(Builder.getInt32(SI->getAlign().value()));
      Builder.CreateCall(VstNFunc, Ops);
    } else {
      assert((Factor == 2 || Factor == 4) &&
             "expected interleave factor of 2 or 4 for MVE");
      Intrinsic::ID StoreInts =
          Factor == 2 ? Intrinsic::arm_mve_vst2q : Intrinsic::arm_mve_vst4q;
      Type *PtrTy = Builder.getPtrTy(SI->getPointerAddressSpace());
      Type *Tys[] = {PtrTy, SubVecTy};
      Function *VstNFunc =
          Intrinsic::getDeclaration(SI->getModule(), StoreInts, Tys);

      SmallVector<Value *, 6> Ops;
      Ops.push_back(BaseAddr);
      append_range(Ops, Shuffles);
      for (unsigned F = 0; F < Factor; F++) {
        Ops.push_back(Builder.getInt32(F));
        Builder.CreateCall(VstNFunc, Ops);
        Ops.pop_back();
      }
    }
  };

  for (unsigned StoreCount = 0; StoreCount < NumStores; ++StoreCount) {
    // If we generating more than one store, we compute the base address of
    // subsequent stores as an offset from the previous.
    if (StoreCount > 0)
      BaseAddr = Builder.CreateConstGEP1_32(SubVecTy->getElementType(),
                                            BaseAddr, LaneLen * Factor);

    SmallVector<Value *, 4> Shuffles;

    // Split the shufflevector operands into sub vectors for the new vstN call.
    for (unsigned i = 0; i < Factor; i++) {
      unsigned IdxI = StoreCount * LaneLen * Factor + i;
      if (Mask[IdxI] >= 0) {
        Shuffles.push_back(Builder.CreateShuffleVector(
            Op0, Op1, createSequentialMask(Mask[IdxI], LaneLen, 0)));
      } else {
        unsigned StartMask = 0;
        for (unsigned j = 1; j < LaneLen; j++) {
          unsigned IdxJ = StoreCount * LaneLen * Factor + j;
          if (Mask[IdxJ * Factor + IdxI] >= 0) {
            StartMask = Mask[IdxJ * Factor + IdxI] - IdxJ;
            break;
          }
        }
        // Note: If all elements in a chunk are undefs, StartMask=0!
        // Note: Filling undef gaps with random elements is ok, since
        // those elements were being written anyway (with undefs).
        // In the case of all undefs we're defaulting to using elems from 0
        // Note: StartMask cannot be negative, it's checked in
        // isReInterleaveMask
        Shuffles.push_back(Builder.CreateShuffleVector(
            Op0, Op1, createSequentialMask(StartMask, LaneLen, 0)));
      }
    }

    createStoreIntrinsic(BaseAddr, Shuffles);
  }
  return true;
}

enum HABaseType {
  HA_UNKNOWN = 0,
  HA_FLOAT,
  HA_DOUBLE,
  HA_VECT64,
  HA_VECT128
};

static bool isHomogeneousAggregate(Type *Ty, HABaseType &Base,
                                   uint64_t &Members) {
  if (auto *ST = dyn_cast<StructType>(Ty)) {
    for (unsigned i = 0; i < ST->getNumElements(); ++i) {
      uint64_t SubMembers = 0;
      if (!isHomogeneousAggregate(ST->getElementType(i), Base, SubMembers))
        return false;
      Members += SubMembers;
    }
  } else if (auto *AT = dyn_cast<ArrayType>(Ty)) {
    uint64_t SubMembers = 0;
    if (!isHomogeneousAggregate(AT->getElementType(), Base, SubMembers))
      return false;
    Members += SubMembers * AT->getNumElements();
  } else if (Ty->isFloatTy()) {
    if (Base != HA_UNKNOWN && Base != HA_FLOAT)
      return false;
    Members = 1;
    Base = HA_FLOAT;
  } else if (Ty->isDoubleTy()) {
    if (Base != HA_UNKNOWN && Base != HA_DOUBLE)
      return false;
    Members = 1;
    Base = HA_DOUBLE;
  } else if (auto *VT = dyn_cast<VectorType>(Ty)) {
    Members = 1;
    switch (Base) {
    case HA_FLOAT:
    case HA_DOUBLE:
      return false;
    case HA_VECT64:
      return VT->getPrimitiveSizeInBits().getFixedValue() == 64;
    case HA_VECT128:
      return VT->getPrimitiveSizeInBits().getFixedValue() == 128;
    case HA_UNKNOWN:
      switch (VT->getPrimitiveSizeInBits().getFixedValue()) {
      case 64:
        Base = HA_VECT64;
        return true;
      case 128:
        Base = HA_VECT128;
        return true;
      default:
        return false;
      }
    }
  }

  return (Members > 0 && Members <= 4);
}

/// Return the correct alignment for the current calling convention.
Align ARMTargetLowering::getABIAlignmentForCallingConv(
    Type *ArgTy, const DataLayout &DL) const {
  const Align ABITypeAlign = DL.getABITypeAlign(ArgTy);
  if (!ArgTy->isVectorTy())
    return ABITypeAlign;

  // Avoid over-aligning vector parameters. It would require realigning the
  // stack and waste space for no real benefit.
  return std::min(ABITypeAlign, DL.getStackAlignment());
}

/// Return true if a type is an AAPCS-VFP homogeneous aggregate or one of
/// [N x i32] or [N x i64]. This allows front-ends to skip emitting padding when
/// passing according to AAPCS rules.
bool ARMTargetLowering::functionArgumentNeedsConsecutiveRegisters(
    Type *Ty, CallingConv::ID CallConv, bool isVarArg,
    const DataLayout &DL) const {
  if (getEffectiveCallingConv(CallConv, isVarArg) !=
      CallingConv::ARM_AAPCS_VFP)
    return false;

  HABaseType Base = HA_UNKNOWN;
  uint64_t Members = 0;
  bool IsHA = isHomogeneousAggregate(Ty, Base, Members);
  LLVM_DEBUG(dbgs() << "isHA: " << IsHA << " "; Ty->dump());

  bool IsIntArray = Ty->isArrayTy() && Ty->getArrayElementType()->isIntegerTy();
  return IsHA || IsIntArray;
}

Register ARMTargetLowering::getExceptionPointerRegister(
    const Constant *PersonalityFn) const {
  // Platforms which do not use SjLj EH may return values in these registers
  // via the personality function.
  return Subtarget->useSjLjEH() ? Register() : ARM::R0;
}

Register ARMTargetLowering::getExceptionSelectorRegister(
    const Constant *PersonalityFn) const {
  // Platforms which do not use SjLj EH may return values in these registers
  // via the personality function.
  return Subtarget->useSjLjEH() ? Register() : ARM::R1;
}

void ARMTargetLowering::initializeSplitCSR(MachineBasicBlock *Entry) const {
  // Update IsSplitCSR in ARMFunctionInfo.
  ARMFunctionInfo *AFI = Entry->getParent()->getInfo<ARMFunctionInfo>();
  AFI->setIsSplitCSR(true);
}

void ARMTargetLowering::insertCopiesSplitCSR(
    MachineBasicBlock *Entry,
    const SmallVectorImpl<MachineBasicBlock *> &Exits) const {
  const ARMBaseRegisterInfo *TRI = Subtarget->getRegisterInfo();
  const MCPhysReg *IStart = TRI->getCalleeSavedRegsViaCopy(Entry->getParent());
  if (!IStart)
    return;

  const TargetInstrInfo *TII = Subtarget->getInstrInfo();
  MachineRegisterInfo *MRI = &Entry->getParent()->getRegInfo();
  MachineBasicBlock::iterator MBBI = Entry->begin();
  for (const MCPhysReg *I = IStart; *I; ++I) {
    const TargetRegisterClass *RC = nullptr;
    if (ARM::GPRRegClass.contains(*I))
      RC = &ARM::GPRRegClass;
    else if (ARM::DPRRegClass.contains(*I))
      RC = &ARM::DPRRegClass;
    else
      llvm_unreachable("Unexpected register class in CSRsViaCopy!");

    Register NewVR = MRI->createVirtualRegister(RC);
    // Create copy from CSR to a virtual register.
    // FIXME: this currently does not emit CFI pseudo-instructions, it works
    // fine for CXX_FAST_TLS since the C++-style TLS access functions should be
    // nounwind. If we want to generalize this later, we may need to emit
    // CFI pseudo-instructions.
    assert(Entry->getParent()->getFunction().hasFnAttribute(
               Attribute::NoUnwind) &&
           "Function should be nounwind in insertCopiesSplitCSR!");
    Entry->addLiveIn(*I);
    BuildMI(*Entry, MBBI, DebugLoc(), TII->get(TargetOpcode::COPY), NewVR)
        .addReg(*I);

    // Insert the copy-back instructions right before the terminator.
    for (auto *Exit : Exits)
      BuildMI(*Exit, Exit->getFirstTerminator(), DebugLoc(),
              TII->get(TargetOpcode::COPY), *I)
          .addReg(NewVR);
  }
}

void ARMTargetLowering::finalizeLowering(MachineFunction &MF) const {
  MF.getFrameInfo().computeMaxCallFrameSize(MF);
  TargetLoweringBase::finalizeLowering(MF);
}

bool ARMTargetLowering::isComplexDeinterleavingSupported() const {
  return Subtarget->hasMVEIntegerOps();
}

bool ARMTargetLowering::isComplexDeinterleavingOperationSupported(
    ComplexDeinterleavingOperation Operation, Type *Ty) const {
  auto *VTy = dyn_cast<FixedVectorType>(Ty);
  if (!VTy)
    return false;

  auto *ScalarTy = VTy->getScalarType();
  unsigned NumElements = VTy->getNumElements();

  unsigned VTyWidth = VTy->getScalarSizeInBits() * NumElements;
  if (VTyWidth < 128 || !llvm::isPowerOf2_32(VTyWidth))
    return false;

  // Both VCADD and VCMUL/VCMLA support the same types, F16 and F32
  if (ScalarTy->isHalfTy() || ScalarTy->isFloatTy())
    return Subtarget->hasMVEFloatOps();

  if (Operation != ComplexDeinterleavingOperation::CAdd)
    return false;

  return Subtarget->hasMVEIntegerOps() &&
         (ScalarTy->isIntegerTy(8) || ScalarTy->isIntegerTy(16) ||
          ScalarTy->isIntegerTy(32));
}

Value *ARMTargetLowering::createComplexDeinterleavingIR(
    IRBuilderBase &B, ComplexDeinterleavingOperation OperationType,
    ComplexDeinterleavingRotation Rotation, Value *InputA, Value *InputB,
    Value *Accumulator) const {

  FixedVectorType *Ty = cast<FixedVectorType>(InputA->getType());

  unsigned TyWidth = Ty->getScalarSizeInBits() * Ty->getNumElements();

  assert(TyWidth >= 128 && "Width of vector type must be at least 128 bits");

  if (TyWidth > 128) {
    int Stride = Ty->getNumElements() / 2;
    auto SplitSeq = llvm::seq<int>(0, Ty->getNumElements());
    auto SplitSeqVec = llvm::to_vector(SplitSeq);
    ArrayRef<int> LowerSplitMask(&SplitSeqVec[0], Stride);
    ArrayRef<int> UpperSplitMask(&SplitSeqVec[Stride], Stride);

    auto *LowerSplitA = B.CreateShuffleVector(InputA, LowerSplitMask);
    auto *LowerSplitB = B.CreateShuffleVector(InputB, LowerSplitMask);
    auto *UpperSplitA = B.CreateShuffleVector(InputA, UpperSplitMask);
    auto *UpperSplitB = B.CreateShuffleVector(InputB, UpperSplitMask);
    Value *LowerSplitAcc = nullptr;
    Value *UpperSplitAcc = nullptr;

    if (Accumulator) {
      LowerSplitAcc = B.CreateShuffleVector(Accumulator, LowerSplitMask);
      UpperSplitAcc = B.CreateShuffleVector(Accumulator, UpperSplitMask);
    }

    auto *LowerSplitInt = createComplexDeinterleavingIR(
        B, OperationType, Rotation, LowerSplitA, LowerSplitB, LowerSplitAcc);
    auto *UpperSplitInt = createComplexDeinterleavingIR(
        B, OperationType, Rotation, UpperSplitA, UpperSplitB, UpperSplitAcc);

    ArrayRef<int> JoinMask(&SplitSeqVec[0], Ty->getNumElements());
    return B.CreateShuffleVector(LowerSplitInt, UpperSplitInt, JoinMask);
  }

  auto *IntTy = Type::getInt32Ty(B.getContext());

  ConstantInt *ConstRotation = nullptr;
  if (OperationType == ComplexDeinterleavingOperation::CMulPartial) {
    ConstRotation = ConstantInt::get(IntTy, (int)Rotation);

    if (Accumulator)
      return B.CreateIntrinsic(Intrinsic::arm_mve_vcmlaq, Ty,
                               {ConstRotation, Accumulator, InputB, InputA});
    return B.CreateIntrinsic(Intrinsic::arm_mve_vcmulq, Ty,
                             {ConstRotation, InputB, InputA});
  }

  if (OperationType == ComplexDeinterleavingOperation::CAdd) {
    // 1 means the value is not halved.
    auto *ConstHalving = ConstantInt::get(IntTy, 1);

    if (Rotation == ComplexDeinterleavingRotation::Rotation_90)
      ConstRotation = ConstantInt::get(IntTy, 0);
    else if (Rotation == ComplexDeinterleavingRotation::Rotation_270)
      ConstRotation = ConstantInt::get(IntTy, 1);

    if (!ConstRotation)
      return nullptr; // Invalid rotation for arm_mve_vcaddq

    return B.CreateIntrinsic(Intrinsic::arm_mve_vcaddq, Ty,
                             {ConstHalving, ConstRotation, InputA, InputB});
  }

  return nullptr;
}
