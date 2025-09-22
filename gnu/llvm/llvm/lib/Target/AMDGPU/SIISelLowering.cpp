//===-- SIISelLowering.cpp - SI DAG Lowering Implementation ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Custom DAG lowering for SI
//
//===----------------------------------------------------------------------===//

#include "SIISelLowering.h"
#include "AMDGPU.h"
#include "AMDGPUInstrInfo.h"
#include "AMDGPUTargetMachine.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIMachineFunctionInfo.h"
#include "SIRegisterInfo.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/Analysis/UniformityAnalysis.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/ByteProvider.h"
#include "llvm/CodeGen/FunctionLoweringInfo.h"
#include "llvm/CodeGen/GlobalISel/GISelKnownBits.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/GlobalISel/MIPatternMatch.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/IntrinsicsR600.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Support/ModRef.h"
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "si-lower"

STATISTIC(NumTailCalls, "Number of tail calls");

static cl::opt<bool> DisableLoopAlignment(
  "amdgpu-disable-loop-alignment",
  cl::desc("Do not align and prefetch loops"),
  cl::init(false));

static cl::opt<bool> UseDivergentRegisterIndexing(
  "amdgpu-use-divergent-register-indexing",
  cl::Hidden,
  cl::desc("Use indirect register addressing for divergent indexes"),
  cl::init(false));

static bool denormalModeIsFlushAllF32(const MachineFunction &MF) {
  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();
  return Info->getMode().FP32Denormals == DenormalMode::getPreserveSign();
}

static bool denormalModeIsFlushAllF64F16(const MachineFunction &MF) {
  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();
  return Info->getMode().FP64FP16Denormals == DenormalMode::getPreserveSign();
}

static unsigned findFirstFreeSGPR(CCState &CCInfo) {
  unsigned NumSGPRs = AMDGPU::SGPR_32RegClass.getNumRegs();
  for (unsigned Reg = 0; Reg < NumSGPRs; ++Reg) {
    if (!CCInfo.isAllocated(AMDGPU::SGPR0 + Reg)) {
      return AMDGPU::SGPR0 + Reg;
    }
  }
  llvm_unreachable("Cannot allocate sgpr");
}

SITargetLowering::SITargetLowering(const TargetMachine &TM,
                                   const GCNSubtarget &STI)
    : AMDGPUTargetLowering(TM, STI),
      Subtarget(&STI) {
  addRegisterClass(MVT::i1, &AMDGPU::VReg_1RegClass);
  addRegisterClass(MVT::i64, &AMDGPU::SReg_64RegClass);

  addRegisterClass(MVT::i32, &AMDGPU::SReg_32RegClass);
  addRegisterClass(MVT::f32, &AMDGPU::VGPR_32RegClass);

  addRegisterClass(MVT::v2i32, &AMDGPU::SReg_64RegClass);

  const SIRegisterInfo *TRI = STI.getRegisterInfo();
  const TargetRegisterClass *V64RegClass = TRI->getVGPR64Class();

  addRegisterClass(MVT::f64, V64RegClass);
  addRegisterClass(MVT::v2f32, V64RegClass);
  addRegisterClass(MVT::Untyped, V64RegClass);

  addRegisterClass(MVT::v3i32, &AMDGPU::SGPR_96RegClass);
  addRegisterClass(MVT::v3f32, TRI->getVGPRClassForBitWidth(96));

  addRegisterClass(MVT::v2i64, &AMDGPU::SGPR_128RegClass);
  addRegisterClass(MVT::v2f64, &AMDGPU::SGPR_128RegClass);

  addRegisterClass(MVT::v4i32, &AMDGPU::SGPR_128RegClass);
  addRegisterClass(MVT::v4f32, TRI->getVGPRClassForBitWidth(128));

  addRegisterClass(MVT::v5i32, &AMDGPU::SGPR_160RegClass);
  addRegisterClass(MVT::v5f32, TRI->getVGPRClassForBitWidth(160));

  addRegisterClass(MVT::v6i32, &AMDGPU::SGPR_192RegClass);
  addRegisterClass(MVT::v6f32, TRI->getVGPRClassForBitWidth(192));

  addRegisterClass(MVT::v3i64, &AMDGPU::SGPR_192RegClass);
  addRegisterClass(MVT::v3f64, TRI->getVGPRClassForBitWidth(192));

  addRegisterClass(MVT::v7i32, &AMDGPU::SGPR_224RegClass);
  addRegisterClass(MVT::v7f32, TRI->getVGPRClassForBitWidth(224));

  addRegisterClass(MVT::v8i32, &AMDGPU::SGPR_256RegClass);
  addRegisterClass(MVT::v8f32, TRI->getVGPRClassForBitWidth(256));

  addRegisterClass(MVT::v4i64, &AMDGPU::SGPR_256RegClass);
  addRegisterClass(MVT::v4f64, TRI->getVGPRClassForBitWidth(256));

  addRegisterClass(MVT::v9i32, &AMDGPU::SGPR_288RegClass);
  addRegisterClass(MVT::v9f32, TRI->getVGPRClassForBitWidth(288));

  addRegisterClass(MVT::v10i32, &AMDGPU::SGPR_320RegClass);
  addRegisterClass(MVT::v10f32, TRI->getVGPRClassForBitWidth(320));

  addRegisterClass(MVT::v11i32, &AMDGPU::SGPR_352RegClass);
  addRegisterClass(MVT::v11f32, TRI->getVGPRClassForBitWidth(352));

  addRegisterClass(MVT::v12i32, &AMDGPU::SGPR_384RegClass);
  addRegisterClass(MVT::v12f32, TRI->getVGPRClassForBitWidth(384));

  addRegisterClass(MVT::v16i32, &AMDGPU::SGPR_512RegClass);
  addRegisterClass(MVT::v16f32, TRI->getVGPRClassForBitWidth(512));

  addRegisterClass(MVT::v8i64, &AMDGPU::SGPR_512RegClass);
  addRegisterClass(MVT::v8f64, TRI->getVGPRClassForBitWidth(512));

  addRegisterClass(MVT::v16i64, &AMDGPU::SGPR_1024RegClass);
  addRegisterClass(MVT::v16f64, TRI->getVGPRClassForBitWidth(1024));

  if (Subtarget->has16BitInsts()) {
    if (Subtarget->useRealTrue16Insts()) {
      addRegisterClass(MVT::i16, &AMDGPU::VGPR_16RegClass);
      addRegisterClass(MVT::f16, &AMDGPU::VGPR_16RegClass);
      addRegisterClass(MVT::bf16, &AMDGPU::VGPR_16RegClass);
    } else {
      addRegisterClass(MVT::i16, &AMDGPU::SReg_32RegClass);
      addRegisterClass(MVT::f16, &AMDGPU::SReg_32RegClass);
      addRegisterClass(MVT::bf16, &AMDGPU::SReg_32RegClass);
    }

    // Unless there are also VOP3P operations, not operations are really legal.
    addRegisterClass(MVT::v2i16, &AMDGPU::SReg_32RegClass);
    addRegisterClass(MVT::v2f16, &AMDGPU::SReg_32RegClass);
    addRegisterClass(MVT::v2bf16, &AMDGPU::SReg_32RegClass);
    addRegisterClass(MVT::v4i16, &AMDGPU::SReg_64RegClass);
    addRegisterClass(MVT::v4f16, &AMDGPU::SReg_64RegClass);
    addRegisterClass(MVT::v4bf16, &AMDGPU::SReg_64RegClass);
    addRegisterClass(MVT::v8i16, &AMDGPU::SGPR_128RegClass);
    addRegisterClass(MVT::v8f16, &AMDGPU::SGPR_128RegClass);
    addRegisterClass(MVT::v8bf16, &AMDGPU::SGPR_128RegClass);
    addRegisterClass(MVT::v16i16, &AMDGPU::SGPR_256RegClass);
    addRegisterClass(MVT::v16f16, &AMDGPU::SGPR_256RegClass);
    addRegisterClass(MVT::v16bf16, &AMDGPU::SGPR_256RegClass);
    addRegisterClass(MVT::v32i16, &AMDGPU::SGPR_512RegClass);
    addRegisterClass(MVT::v32f16, &AMDGPU::SGPR_512RegClass);
    addRegisterClass(MVT::v32bf16, &AMDGPU::SGPR_512RegClass);
  }

  addRegisterClass(MVT::v32i32, &AMDGPU::VReg_1024RegClass);
  addRegisterClass(MVT::v32f32, TRI->getVGPRClassForBitWidth(1024));

  computeRegisterProperties(Subtarget->getRegisterInfo());

  // The boolean content concept here is too inflexible. Compares only ever
  // really produce a 1-bit result. Any copy/extend from these will turn into a
  // select, and zext/1 or sext/-1 are equally cheap. Arbitrarily choose 0/1, as
  // it's what most targets use.
  setBooleanContents(ZeroOrOneBooleanContent);
  setBooleanVectorContents(ZeroOrOneBooleanContent);

  // We need to custom lower vector stores from local memory
  setOperationAction(ISD::LOAD,
                     {MVT::v2i32,  MVT::v3i32,  MVT::v4i32,  MVT::v5i32,
                      MVT::v6i32,  MVT::v7i32,  MVT::v8i32,  MVT::v9i32,
                      MVT::v10i32, MVT::v11i32, MVT::v12i32, MVT::v16i32,
                      MVT::i1,     MVT::v32i32},
                     Custom);

  setOperationAction(ISD::STORE,
                     {MVT::v2i32,  MVT::v3i32,  MVT::v4i32,  MVT::v5i32,
                      MVT::v6i32,  MVT::v7i32,  MVT::v8i32,  MVT::v9i32,
                      MVT::v10i32, MVT::v11i32, MVT::v12i32, MVT::v16i32,
                      MVT::i1,     MVT::v32i32},
                     Custom);

  if (isTypeLegal(MVT::bf16)) {
    for (unsigned Opc :
         {ISD::FADD,     ISD::FSUB,       ISD::FMUL,    ISD::FDIV,
          ISD::FREM,     ISD::FMA,        ISD::FMINNUM, ISD::FMAXNUM,
          ISD::FMINIMUM, ISD::FMAXIMUM,   ISD::FSQRT,   ISD::FCBRT,
          ISD::FSIN,     ISD::FCOS,       ISD::FPOW,    ISD::FPOWI,
          ISD::FLDEXP,   ISD::FFREXP,     ISD::FLOG,    ISD::FLOG2,
          ISD::FLOG10,   ISD::FEXP,       ISD::FEXP2,   ISD::FEXP10,
          ISD::FCEIL,    ISD::FTRUNC,     ISD::FRINT,   ISD::FNEARBYINT,
          ISD::FROUND,   ISD::FROUNDEVEN, ISD::FFLOOR,  ISD::FCANONICALIZE,
          ISD::SETCC}) {
      // FIXME: The promoted to type shouldn't need to be explicit
      setOperationAction(Opc, MVT::bf16, Promote);
      AddPromotedToType(Opc, MVT::bf16, MVT::f32);
    }

    setOperationAction(ISD::FP_ROUND, MVT::bf16, Expand);

    setOperationAction(ISD::SELECT, MVT::bf16, Promote);
    AddPromotedToType(ISD::SELECT, MVT::bf16, MVT::i16);

    setOperationAction(ISD::FABS, MVT::bf16, Legal);
    setOperationAction(ISD::FNEG, MVT::bf16, Legal);
    setOperationAction(ISD::FCOPYSIGN, MVT::bf16, Legal);

    // We only need to custom lower because we can't specify an action for bf16
    // sources.
    setOperationAction(ISD::FP_TO_SINT, MVT::i32, Custom);
    setOperationAction(ISD::FP_TO_UINT, MVT::i32, Custom);
  }

  setTruncStoreAction(MVT::v2i32, MVT::v2i16, Expand);
  setTruncStoreAction(MVT::v3i32, MVT::v3i16, Expand);
  setTruncStoreAction(MVT::v4i32, MVT::v4i16, Expand);
  setTruncStoreAction(MVT::v8i32, MVT::v8i16, Expand);
  setTruncStoreAction(MVT::v16i32, MVT::v16i16, Expand);
  setTruncStoreAction(MVT::v32i32, MVT::v32i16, Expand);
  setTruncStoreAction(MVT::v2i32, MVT::v2i8, Expand);
  setTruncStoreAction(MVT::v4i32, MVT::v4i8, Expand);
  setTruncStoreAction(MVT::v8i32, MVT::v8i8, Expand);
  setTruncStoreAction(MVT::v16i32, MVT::v16i8, Expand);
  setTruncStoreAction(MVT::v32i32, MVT::v32i8, Expand);
  setTruncStoreAction(MVT::v2i16, MVT::v2i8, Expand);
  setTruncStoreAction(MVT::v4i16, MVT::v4i8, Expand);
  setTruncStoreAction(MVT::v8i16, MVT::v8i8, Expand);
  setTruncStoreAction(MVT::v16i16, MVT::v16i8, Expand);
  setTruncStoreAction(MVT::v32i16, MVT::v32i8, Expand);

  setTruncStoreAction(MVT::v3i64, MVT::v3i16, Expand);
  setTruncStoreAction(MVT::v3i64, MVT::v3i32, Expand);
  setTruncStoreAction(MVT::v4i64, MVT::v4i8, Expand);
  setTruncStoreAction(MVT::v8i64, MVT::v8i8, Expand);
  setTruncStoreAction(MVT::v8i64, MVT::v8i16, Expand);
  setTruncStoreAction(MVT::v8i64, MVT::v8i32, Expand);
  setTruncStoreAction(MVT::v16i64, MVT::v16i32, Expand);

  setOperationAction(ISD::GlobalAddress, {MVT::i32, MVT::i64}, Custom);

  setOperationAction(ISD::SELECT, MVT::i1, Promote);
  setOperationAction(ISD::SELECT, MVT::i64, Custom);
  setOperationAction(ISD::SELECT, MVT::f64, Promote);
  AddPromotedToType(ISD::SELECT, MVT::f64, MVT::i64);

  setOperationAction(ISD::FSQRT, {MVT::f32, MVT::f64}, Custom);

  setOperationAction(ISD::SELECT_CC,
                     {MVT::f32, MVT::i32, MVT::i64, MVT::f64, MVT::i1}, Expand);

  setOperationAction(ISD::SETCC, MVT::i1, Promote);
  setOperationAction(ISD::SETCC, {MVT::v2i1, MVT::v4i1}, Expand);
  AddPromotedToType(ISD::SETCC, MVT::i1, MVT::i32);

  setOperationAction(ISD::TRUNCATE,
                     {MVT::v2i32,  MVT::v3i32,  MVT::v4i32,  MVT::v5i32,
                      MVT::v6i32,  MVT::v7i32,  MVT::v8i32,  MVT::v9i32,
                      MVT::v10i32, MVT::v11i32, MVT::v12i32, MVT::v16i32},
                     Expand);
  setOperationAction(ISD::FP_ROUND,
                     {MVT::v2f32,  MVT::v3f32,  MVT::v4f32,  MVT::v5f32,
                      MVT::v6f32,  MVT::v7f32,  MVT::v8f32,  MVT::v9f32,
                      MVT::v10f32, MVT::v11f32, MVT::v12f32, MVT::v16f32},
                     Expand);

  setOperationAction(ISD::SIGN_EXTEND_INREG,
                     {MVT::v2i1, MVT::v4i1, MVT::v2i8, MVT::v4i8, MVT::v2i16,
                      MVT::v3i16, MVT::v4i16, MVT::Other},
                     Custom);

  setOperationAction(ISD::BRCOND, MVT::Other, Custom);
  setOperationAction(ISD::BR_CC,
                     {MVT::i1, MVT::i32, MVT::i64, MVT::f32, MVT::f64}, Expand);

  setOperationAction({ISD::UADDO, ISD::USUBO}, MVT::i32, Legal);

  setOperationAction({ISD::UADDO_CARRY, ISD::USUBO_CARRY}, MVT::i32, Legal);

  setOperationAction({ISD::SHL_PARTS, ISD::SRA_PARTS, ISD::SRL_PARTS}, MVT::i64,
                     Expand);

#if 0
  setOperationAction({ISD::UADDO_CARRY, ISD::USUBO_CARRY}, MVT::i64, Legal);
#endif

  // We only support LOAD/STORE and vector manipulation ops for vectors
  // with > 4 elements.
  for (MVT VT :
       {MVT::v8i32,   MVT::v8f32,  MVT::v9i32,  MVT::v9f32,  MVT::v10i32,
        MVT::v10f32,  MVT::v11i32, MVT::v11f32, MVT::v12i32, MVT::v12f32,
        MVT::v16i32,  MVT::v16f32, MVT::v2i64,  MVT::v2f64,  MVT::v4i16,
        MVT::v4f16,   MVT::v4bf16, MVT::v3i64,  MVT::v3f64,  MVT::v6i32,
        MVT::v6f32,   MVT::v4i64,  MVT::v4f64,  MVT::v8i64,  MVT::v8f64,
        MVT::v8i16,   MVT::v8f16,  MVT::v8bf16, MVT::v16i16, MVT::v16f16,
        MVT::v16bf16, MVT::v16i64, MVT::v16f64, MVT::v32i32, MVT::v32f32,
        MVT::v32i16,  MVT::v32f16, MVT::v32bf16}) {
    for (unsigned Op = 0; Op < ISD::BUILTIN_OP_END; ++Op) {
      switch (Op) {
      case ISD::LOAD:
      case ISD::STORE:
      case ISD::BUILD_VECTOR:
      case ISD::BITCAST:
      case ISD::UNDEF:
      case ISD::EXTRACT_VECTOR_ELT:
      case ISD::INSERT_VECTOR_ELT:
      case ISD::SCALAR_TO_VECTOR:
      case ISD::IS_FPCLASS:
        break;
      case ISD::EXTRACT_SUBVECTOR:
      case ISD::INSERT_SUBVECTOR:
      case ISD::CONCAT_VECTORS:
        setOperationAction(Op, VT, Custom);
        break;
      default:
        setOperationAction(Op, VT, Expand);
        break;
      }
    }
  }

  setOperationAction(ISD::FP_EXTEND, MVT::v4f32, Expand);

  // TODO: For dynamic 64-bit vector inserts/extracts, should emit a pseudo that
  // is expanded to avoid having two separate loops in case the index is a VGPR.

  // Most operations are naturally 32-bit vector operations. We only support
  // load and store of i64 vectors, so promote v2i64 vector operations to v4i32.
  for (MVT Vec64 : { MVT::v2i64, MVT::v2f64 }) {
    setOperationAction(ISD::BUILD_VECTOR, Vec64, Promote);
    AddPromotedToType(ISD::BUILD_VECTOR, Vec64, MVT::v4i32);

    setOperationAction(ISD::EXTRACT_VECTOR_ELT, Vec64, Promote);
    AddPromotedToType(ISD::EXTRACT_VECTOR_ELT, Vec64, MVT::v4i32);

    setOperationAction(ISD::INSERT_VECTOR_ELT, Vec64, Promote);
    AddPromotedToType(ISD::INSERT_VECTOR_ELT, Vec64, MVT::v4i32);

    setOperationAction(ISD::SCALAR_TO_VECTOR, Vec64, Promote);
    AddPromotedToType(ISD::SCALAR_TO_VECTOR, Vec64, MVT::v4i32);
  }

  for (MVT Vec64 : { MVT::v3i64, MVT::v3f64 }) {
    setOperationAction(ISD::BUILD_VECTOR, Vec64, Promote);
    AddPromotedToType(ISD::BUILD_VECTOR, Vec64, MVT::v6i32);

    setOperationAction(ISD::EXTRACT_VECTOR_ELT, Vec64, Promote);
    AddPromotedToType(ISD::EXTRACT_VECTOR_ELT, Vec64, MVT::v6i32);

    setOperationAction(ISD::INSERT_VECTOR_ELT, Vec64, Promote);
    AddPromotedToType(ISD::INSERT_VECTOR_ELT, Vec64, MVT::v6i32);

    setOperationAction(ISD::SCALAR_TO_VECTOR, Vec64, Promote);
    AddPromotedToType(ISD::SCALAR_TO_VECTOR, Vec64, MVT::v6i32);
  }

  for (MVT Vec64 : { MVT::v4i64, MVT::v4f64 }) {
    setOperationAction(ISD::BUILD_VECTOR, Vec64, Promote);
    AddPromotedToType(ISD::BUILD_VECTOR, Vec64, MVT::v8i32);

    setOperationAction(ISD::EXTRACT_VECTOR_ELT, Vec64, Promote);
    AddPromotedToType(ISD::EXTRACT_VECTOR_ELT, Vec64, MVT::v8i32);

    setOperationAction(ISD::INSERT_VECTOR_ELT, Vec64, Promote);
    AddPromotedToType(ISD::INSERT_VECTOR_ELT, Vec64, MVT::v8i32);

    setOperationAction(ISD::SCALAR_TO_VECTOR, Vec64, Promote);
    AddPromotedToType(ISD::SCALAR_TO_VECTOR, Vec64, MVT::v8i32);
  }

  for (MVT Vec64 : { MVT::v8i64, MVT::v8f64 }) {
    setOperationAction(ISD::BUILD_VECTOR, Vec64, Promote);
    AddPromotedToType(ISD::BUILD_VECTOR, Vec64, MVT::v16i32);

    setOperationAction(ISD::EXTRACT_VECTOR_ELT, Vec64, Promote);
    AddPromotedToType(ISD::EXTRACT_VECTOR_ELT, Vec64, MVT::v16i32);

    setOperationAction(ISD::INSERT_VECTOR_ELT, Vec64, Promote);
    AddPromotedToType(ISD::INSERT_VECTOR_ELT, Vec64, MVT::v16i32);

    setOperationAction(ISD::SCALAR_TO_VECTOR, Vec64, Promote);
    AddPromotedToType(ISD::SCALAR_TO_VECTOR, Vec64, MVT::v16i32);
  }

  for (MVT Vec64 : { MVT::v16i64, MVT::v16f64 }) {
    setOperationAction(ISD::BUILD_VECTOR, Vec64, Promote);
    AddPromotedToType(ISD::BUILD_VECTOR, Vec64, MVT::v32i32);

    setOperationAction(ISD::EXTRACT_VECTOR_ELT, Vec64, Promote);
    AddPromotedToType(ISD::EXTRACT_VECTOR_ELT, Vec64, MVT::v32i32);

    setOperationAction(ISD::INSERT_VECTOR_ELT, Vec64, Promote);
    AddPromotedToType(ISD::INSERT_VECTOR_ELT, Vec64, MVT::v32i32);

    setOperationAction(ISD::SCALAR_TO_VECTOR, Vec64, Promote);
    AddPromotedToType(ISD::SCALAR_TO_VECTOR, Vec64, MVT::v32i32);
  }

  setOperationAction(ISD::VECTOR_SHUFFLE,
                     {MVT::v8i32, MVT::v8f32, MVT::v16i32, MVT::v16f32},
                     Expand);

  setOperationAction(ISD::BUILD_VECTOR, {MVT::v4f16, MVT::v4i16, MVT::v4bf16},
                     Custom);

  // Avoid stack access for these.
  // TODO: Generalize to more vector types.
  setOperationAction({ISD::EXTRACT_VECTOR_ELT, ISD::INSERT_VECTOR_ELT},
                     {MVT::v2i16, MVT::v2f16, MVT::v2bf16, MVT::v2i8, MVT::v4i8,
                      MVT::v8i8, MVT::v4i16, MVT::v4f16, MVT::v4bf16},
                     Custom);

  // Deal with vec3 vector operations when widened to vec4.
  setOperationAction(ISD::INSERT_SUBVECTOR,
                     {MVT::v3i32, MVT::v3f32, MVT::v4i32, MVT::v4f32}, Custom);

  // Deal with vec5/6/7 vector operations when widened to vec8.
  setOperationAction(ISD::INSERT_SUBVECTOR,
                     {MVT::v5i32,  MVT::v5f32,  MVT::v6i32,  MVT::v6f32,
                      MVT::v7i32,  MVT::v7f32,  MVT::v8i32,  MVT::v8f32,
                      MVT::v9i32,  MVT::v9f32,  MVT::v10i32, MVT::v10f32,
                      MVT::v11i32, MVT::v11f32, MVT::v12i32, MVT::v12f32},
                     Custom);

  // BUFFER/FLAT_ATOMIC_CMP_SWAP on GCN GPUs needs input marshalling,
  // and output demarshalling
  setOperationAction(ISD::ATOMIC_CMP_SWAP, {MVT::i32, MVT::i64}, Custom);

  // We can't return success/failure, only the old value,
  // let LLVM add the comparison
  setOperationAction(ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS, {MVT::i32, MVT::i64},
                     Expand);

  setOperationAction(ISD::ADDRSPACECAST, {MVT::i32, MVT::i64}, Custom);

  setOperationAction(ISD::BITREVERSE, {MVT::i32, MVT::i64}, Legal);

  // FIXME: This should be narrowed to i32, but that only happens if i64 is
  // illegal.
  // FIXME: Should lower sub-i32 bswaps to bit-ops without v_perm_b32.
  setOperationAction(ISD::BSWAP, {MVT::i64, MVT::i32}, Legal);

  // On SI this is s_memtime and s_memrealtime on VI.
  setOperationAction(ISD::READCYCLECOUNTER, MVT::i64, Legal);

  if (Subtarget->hasSMemRealTime() ||
      Subtarget->getGeneration() >= AMDGPUSubtarget::GFX11)
    setOperationAction(ISD::READSTEADYCOUNTER, MVT::i64, Legal);
  setOperationAction({ISD::TRAP, ISD::DEBUGTRAP}, MVT::Other, Custom);

  if (Subtarget->has16BitInsts()) {
    setOperationAction({ISD::FPOW, ISD::FPOWI}, MVT::f16, Promote);
    setOperationAction({ISD::FLOG, ISD::FEXP, ISD::FLOG10}, MVT::f16, Custom);
  } else {
    setOperationAction(ISD::FSQRT, MVT::f16, Custom);
  }

  if (Subtarget->hasMadMacF32Insts())
    setOperationAction(ISD::FMAD, MVT::f32, Legal);

  if (!Subtarget->hasBFI())
    // fcopysign can be done in a single instruction with BFI.
    setOperationAction(ISD::FCOPYSIGN, {MVT::f32, MVT::f64}, Expand);

  if (!Subtarget->hasBCNT(32))
    setOperationAction(ISD::CTPOP, MVT::i32, Expand);

  if (!Subtarget->hasBCNT(64))
    setOperationAction(ISD::CTPOP, MVT::i64, Expand);

  if (Subtarget->hasFFBH())
    setOperationAction({ISD::CTLZ, ISD::CTLZ_ZERO_UNDEF}, MVT::i32, Custom);

  if (Subtarget->hasFFBL())
    setOperationAction({ISD::CTTZ, ISD::CTTZ_ZERO_UNDEF}, MVT::i32, Custom);

  // We only really have 32-bit BFE instructions (and 16-bit on VI).
  //
  // On SI+ there are 64-bit BFEs, but they are scalar only and there isn't any
  // effort to match them now. We want this to be false for i64 cases when the
  // extraction isn't restricted to the upper or lower half. Ideally we would
  // have some pass reduce 64-bit extracts to 32-bit if possible. Extracts that
  // span the midpoint are probably relatively rare, so don't worry about them
  // for now.
  if (Subtarget->hasBFE())
    setHasExtractBitsInsn(true);

  // Clamp modifier on add/sub
  if (Subtarget->hasIntClamp())
    setOperationAction({ISD::UADDSAT, ISD::USUBSAT}, MVT::i32, Legal);

  if (Subtarget->hasAddNoCarry())
    setOperationAction({ISD::SADDSAT, ISD::SSUBSAT}, {MVT::i16, MVT::i32},
                       Legal);

  setOperationAction({ISD::FMINNUM, ISD::FMAXNUM}, {MVT::f32, MVT::f64},
                     Custom);

  // These are really only legal for ieee_mode functions. We should be avoiding
  // them for functions that don't have ieee_mode enabled, so just say they are
  // legal.
  setOperationAction({ISD::FMINNUM_IEEE, ISD::FMAXNUM_IEEE},
                     {MVT::f32, MVT::f64}, Legal);

  if (Subtarget->haveRoundOpsF64())
    setOperationAction({ISD::FTRUNC, ISD::FCEIL, ISD::FROUNDEVEN}, MVT::f64,
                       Legal);
  else
    setOperationAction({ISD::FCEIL, ISD::FTRUNC, ISD::FROUNDEVEN, ISD::FFLOOR},
                       MVT::f64, Custom);

  setOperationAction(ISD::FFLOOR, MVT::f64, Legal);
  setOperationAction({ISD::FLDEXP, ISD::STRICT_FLDEXP}, {MVT::f32, MVT::f64},
                     Legal);
  setOperationAction(ISD::FFREXP, {MVT::f32, MVT::f64}, Custom);

  setOperationAction({ISD::FSIN, ISD::FCOS, ISD::FDIV}, MVT::f32, Custom);
  setOperationAction(ISD::FDIV, MVT::f64, Custom);

  setOperationAction(ISD::BF16_TO_FP, {MVT::i16, MVT::f32, MVT::f64}, Expand);
  setOperationAction(ISD::FP_TO_BF16, {MVT::i16, MVT::f32, MVT::f64}, Expand);

  // Custom lower these because we can't specify a rule based on an illegal
  // source bf16.
  setOperationAction({ISD::FP_EXTEND, ISD::STRICT_FP_EXTEND}, MVT::f32, Custom);
  setOperationAction({ISD::FP_EXTEND, ISD::STRICT_FP_EXTEND}, MVT::f64, Custom);

  if (Subtarget->has16BitInsts()) {
    setOperationAction({ISD::Constant, ISD::SMIN, ISD::SMAX, ISD::UMIN,
                        ISD::UMAX, ISD::UADDSAT, ISD::USUBSAT},
                       MVT::i16, Legal);

    AddPromotedToType(ISD::SIGN_EXTEND, MVT::i16, MVT::i32);

    setOperationAction({ISD::ROTR, ISD::ROTL, ISD::SELECT_CC, ISD::BR_CC},
                       MVT::i16, Expand);

    setOperationAction({ISD::SIGN_EXTEND, ISD::SDIV, ISD::UDIV, ISD::SREM,
                        ISD::UREM, ISD::BITREVERSE, ISD::CTTZ,
                        ISD::CTTZ_ZERO_UNDEF, ISD::CTLZ, ISD::CTLZ_ZERO_UNDEF,
                        ISD::CTPOP},
                       MVT::i16, Promote);

    setOperationAction(ISD::LOAD, MVT::i16, Custom);

    setTruncStoreAction(MVT::i64, MVT::i16, Expand);

    setOperationAction(ISD::FP16_TO_FP, MVT::i16, Promote);
    AddPromotedToType(ISD::FP16_TO_FP, MVT::i16, MVT::i32);
    setOperationAction(ISD::FP_TO_FP16, MVT::i16, Promote);
    AddPromotedToType(ISD::FP_TO_FP16, MVT::i16, MVT::i32);

    setOperationAction({ISD::FP_TO_SINT, ISD::FP_TO_UINT}, MVT::i16, Custom);
    setOperationAction({ISD::SINT_TO_FP, ISD::UINT_TO_FP}, MVT::i16, Custom);
    setOperationAction({ISD::SINT_TO_FP, ISD::UINT_TO_FP}, MVT::i16, Custom);

    setOperationAction({ISD::SINT_TO_FP, ISD::UINT_TO_FP}, MVT::i32, Custom);

    // F16 - Constant Actions.
    setOperationAction(ISD::ConstantFP, MVT::f16, Legal);
    setOperationAction(ISD::ConstantFP, MVT::bf16, Legal);

    // F16 - Load/Store Actions.
    setOperationAction(ISD::LOAD, MVT::f16, Promote);
    AddPromotedToType(ISD::LOAD, MVT::f16, MVT::i16);
    setOperationAction(ISD::STORE, MVT::f16, Promote);
    AddPromotedToType(ISD::STORE, MVT::f16, MVT::i16);

    // BF16 - Load/Store Actions.
    setOperationAction(ISD::LOAD, MVT::bf16, Promote);
    AddPromotedToType(ISD::LOAD, MVT::bf16, MVT::i16);
    setOperationAction(ISD::STORE, MVT::bf16, Promote);
    AddPromotedToType(ISD::STORE, MVT::bf16, MVT::i16);

    // F16 - VOP1 Actions.
    setOperationAction({ISD::FP_ROUND, ISD::STRICT_FP_ROUND, ISD::FCOS,
                        ISD::FSIN, ISD::FROUND, ISD::FPTRUNC_ROUND},
                       MVT::f16, Custom);

    setOperationAction({ISD::FP_TO_SINT, ISD::FP_TO_UINT}, MVT::f16, Promote);
    setOperationAction({ISD::FP_TO_SINT, ISD::FP_TO_UINT}, MVT::bf16, Promote);

    // F16 - VOP2 Actions.
    setOperationAction({ISD::BR_CC, ISD::SELECT_CC}, {MVT::f16, MVT::bf16},
                       Expand);
    setOperationAction({ISD::FLDEXP, ISD::STRICT_FLDEXP}, MVT::f16, Custom);
    setOperationAction(ISD::FFREXP, MVT::f16, Custom);
    setOperationAction(ISD::FDIV, MVT::f16, Custom);

    // F16 - VOP3 Actions.
    setOperationAction(ISD::FMA, MVT::f16, Legal);
    if (STI.hasMadF16())
      setOperationAction(ISD::FMAD, MVT::f16, Legal);

    for (MVT VT :
         {MVT::v2i16, MVT::v2f16, MVT::v2bf16, MVT::v4i16, MVT::v4f16,
          MVT::v4bf16, MVT::v8i16, MVT::v8f16, MVT::v8bf16, MVT::v16i16,
          MVT::v16f16, MVT::v16bf16, MVT::v32i16, MVT::v32f16}) {
      for (unsigned Op = 0; Op < ISD::BUILTIN_OP_END; ++Op) {
        switch (Op) {
        case ISD::LOAD:
        case ISD::STORE:
        case ISD::BUILD_VECTOR:
        case ISD::BITCAST:
        case ISD::UNDEF:
        case ISD::EXTRACT_VECTOR_ELT:
        case ISD::INSERT_VECTOR_ELT:
        case ISD::INSERT_SUBVECTOR:
        case ISD::EXTRACT_SUBVECTOR:
        case ISD::SCALAR_TO_VECTOR:
        case ISD::IS_FPCLASS:
          break;
        case ISD::CONCAT_VECTORS:
          setOperationAction(Op, VT, Custom);
          break;
        default:
          setOperationAction(Op, VT, Expand);
          break;
        }
      }
    }

    // v_perm_b32 can handle either of these.
    setOperationAction(ISD::BSWAP, {MVT::i16, MVT::v2i16}, Legal);
    setOperationAction(ISD::BSWAP, MVT::v4i16, Custom);

    // XXX - Do these do anything? Vector constants turn into build_vector.
    setOperationAction(ISD::Constant, {MVT::v2i16, MVT::v2f16}, Legal);

    setOperationAction(ISD::UNDEF, {MVT::v2i16, MVT::v2f16, MVT::v2bf16},
                       Legal);

    setOperationAction(ISD::STORE, MVT::v2i16, Promote);
    AddPromotedToType(ISD::STORE, MVT::v2i16, MVT::i32);
    setOperationAction(ISD::STORE, MVT::v2f16, Promote);
    AddPromotedToType(ISD::STORE, MVT::v2f16, MVT::i32);

    setOperationAction(ISD::LOAD, MVT::v2i16, Promote);
    AddPromotedToType(ISD::LOAD, MVT::v2i16, MVT::i32);
    setOperationAction(ISD::LOAD, MVT::v2f16, Promote);
    AddPromotedToType(ISD::LOAD, MVT::v2f16, MVT::i32);

    setOperationAction(ISD::AND, MVT::v2i16, Promote);
    AddPromotedToType(ISD::AND, MVT::v2i16, MVT::i32);
    setOperationAction(ISD::OR, MVT::v2i16, Promote);
    AddPromotedToType(ISD::OR, MVT::v2i16, MVT::i32);
    setOperationAction(ISD::XOR, MVT::v2i16, Promote);
    AddPromotedToType(ISD::XOR, MVT::v2i16, MVT::i32);

    setOperationAction(ISD::LOAD, MVT::v4i16, Promote);
    AddPromotedToType(ISD::LOAD, MVT::v4i16, MVT::v2i32);
    setOperationAction(ISD::LOAD, MVT::v4f16, Promote);
    AddPromotedToType(ISD::LOAD, MVT::v4f16, MVT::v2i32);
    setOperationAction(ISD::LOAD, MVT::v4bf16, Promote);
    AddPromotedToType(ISD::LOAD, MVT::v4bf16, MVT::v2i32);

    setOperationAction(ISD::STORE, MVT::v4i16, Promote);
    AddPromotedToType(ISD::STORE, MVT::v4i16, MVT::v2i32);
    setOperationAction(ISD::STORE, MVT::v4f16, Promote);
    AddPromotedToType(ISD::STORE, MVT::v4f16, MVT::v2i32);
    setOperationAction(ISD::STORE, MVT::v4bf16, Promote);
    AddPromotedToType(ISD::STORE, MVT::v4bf16, MVT::v2i32);

    setOperationAction(ISD::LOAD, MVT::v8i16, Promote);
    AddPromotedToType(ISD::LOAD, MVT::v8i16, MVT::v4i32);
    setOperationAction(ISD::LOAD, MVT::v8f16, Promote);
    AddPromotedToType(ISD::LOAD, MVT::v8f16, MVT::v4i32);
    setOperationAction(ISD::LOAD, MVT::v8bf16, Promote);
    AddPromotedToType(ISD::LOAD, MVT::v8bf16, MVT::v4i32);

    setOperationAction(ISD::STORE, MVT::v4i16, Promote);
    AddPromotedToType(ISD::STORE, MVT::v4i16, MVT::v2i32);
    setOperationAction(ISD::STORE, MVT::v4f16, Promote);
    AddPromotedToType(ISD::STORE, MVT::v4f16, MVT::v2i32);

    setOperationAction(ISD::STORE, MVT::v8i16, Promote);
    AddPromotedToType(ISD::STORE, MVT::v8i16, MVT::v4i32);
    setOperationAction(ISD::STORE, MVT::v8f16, Promote);
    AddPromotedToType(ISD::STORE, MVT::v8f16, MVT::v4i32);
    setOperationAction(ISD::STORE, MVT::v8bf16, Promote);
    AddPromotedToType(ISD::STORE, MVT::v8bf16, MVT::v4i32);

    setOperationAction(ISD::LOAD, MVT::v16i16, Promote);
    AddPromotedToType(ISD::LOAD, MVT::v16i16, MVT::v8i32);
    setOperationAction(ISD::LOAD, MVT::v16f16, Promote);
    AddPromotedToType(ISD::LOAD, MVT::v16f16, MVT::v8i32);
    setOperationAction(ISD::LOAD, MVT::v16bf16, Promote);
    AddPromotedToType(ISD::LOAD, MVT::v16bf16, MVT::v8i32);

    setOperationAction(ISD::STORE, MVT::v16i16, Promote);
    AddPromotedToType(ISD::STORE, MVT::v16i16, MVT::v8i32);
    setOperationAction(ISD::STORE, MVT::v16f16, Promote);
    AddPromotedToType(ISD::STORE, MVT::v16f16, MVT::v8i32);
    setOperationAction(ISD::STORE, MVT::v16bf16, Promote);
    AddPromotedToType(ISD::STORE, MVT::v16bf16, MVT::v8i32);

    setOperationAction(ISD::LOAD, MVT::v32i16, Promote);
    AddPromotedToType(ISD::LOAD, MVT::v32i16, MVT::v16i32);
    setOperationAction(ISD::LOAD, MVT::v32f16, Promote);
    AddPromotedToType(ISD::LOAD, MVT::v32f16, MVT::v16i32);
    setOperationAction(ISD::LOAD, MVT::v32bf16, Promote);
    AddPromotedToType(ISD::LOAD, MVT::v32bf16, MVT::v16i32);

    setOperationAction(ISD::STORE, MVT::v32i16, Promote);
    AddPromotedToType(ISD::STORE, MVT::v32i16, MVT::v16i32);
    setOperationAction(ISD::STORE, MVT::v32f16, Promote);
    AddPromotedToType(ISD::STORE, MVT::v32f16, MVT::v16i32);
    setOperationAction(ISD::STORE, MVT::v32bf16, Promote);
    AddPromotedToType(ISD::STORE, MVT::v32bf16, MVT::v16i32);

    setOperationAction({ISD::ANY_EXTEND, ISD::ZERO_EXTEND, ISD::SIGN_EXTEND},
                       MVT::v2i32, Expand);
    setOperationAction(ISD::FP_EXTEND, MVT::v2f32, Expand);

    setOperationAction({ISD::ANY_EXTEND, ISD::ZERO_EXTEND, ISD::SIGN_EXTEND},
                       MVT::v4i32, Expand);

    setOperationAction({ISD::ANY_EXTEND, ISD::ZERO_EXTEND, ISD::SIGN_EXTEND},
                       MVT::v8i32, Expand);

    setOperationAction(ISD::BUILD_VECTOR, {MVT::v2i16, MVT::v2f16, MVT::v2bf16},
                       Subtarget->hasVOP3PInsts() ? Legal : Custom);

    setOperationAction(ISD::FNEG, MVT::v2f16, Legal);
    // This isn't really legal, but this avoids the legalizer unrolling it (and
    // allows matching fneg (fabs x) patterns)
    setOperationAction(ISD::FABS, MVT::v2f16, Legal);

    setOperationAction({ISD::FMAXNUM, ISD::FMINNUM}, MVT::f16, Custom);
    setOperationAction({ISD::FMAXNUM_IEEE, ISD::FMINNUM_IEEE}, MVT::f16, Legal);

    setOperationAction({ISD::FMINNUM_IEEE, ISD::FMAXNUM_IEEE},
                       {MVT::v4f16, MVT::v8f16, MVT::v16f16, MVT::v32f16},
                       Custom);

    setOperationAction({ISD::FMINNUM, ISD::FMAXNUM},
                       {MVT::v4f16, MVT::v8f16, MVT::v16f16, MVT::v32f16},
                       Expand);

    for (MVT Vec16 :
         {MVT::v8i16, MVT::v8f16, MVT::v8bf16, MVT::v16i16, MVT::v16f16,
          MVT::v16bf16, MVT::v32i16, MVT::v32f16, MVT::v32bf16}) {
      setOperationAction(
          {ISD::BUILD_VECTOR, ISD::EXTRACT_VECTOR_ELT, ISD::SCALAR_TO_VECTOR},
          Vec16, Custom);
      setOperationAction(ISD::INSERT_VECTOR_ELT, Vec16, Expand);
    }
  }

  if (Subtarget->hasVOP3PInsts()) {
    setOperationAction({ISD::ADD, ISD::SUB, ISD::MUL, ISD::SHL, ISD::SRL,
                        ISD::SRA, ISD::SMIN, ISD::UMIN, ISD::SMAX, ISD::UMAX,
                        ISD::UADDSAT, ISD::USUBSAT, ISD::SADDSAT, ISD::SSUBSAT},
                       MVT::v2i16, Legal);

    setOperationAction({ISD::FADD, ISD::FMUL, ISD::FMA, ISD::FMINNUM_IEEE,
                        ISD::FMAXNUM_IEEE, ISD::FCANONICALIZE},
                       MVT::v2f16, Legal);

    setOperationAction(ISD::EXTRACT_VECTOR_ELT, {MVT::v2i16, MVT::v2f16, MVT::v2bf16},
                       Custom);

    setOperationAction(ISD::VECTOR_SHUFFLE,
                       {MVT::v4f16, MVT::v4i16, MVT::v8f16, MVT::v8i16,
                        MVT::v16f16, MVT::v16i16, MVT::v32f16, MVT::v32i16},
                       Custom);

    for (MVT VT : {MVT::v4i16, MVT::v8i16, MVT::v16i16, MVT::v32i16})
      // Split vector operations.
      setOperationAction({ISD::SHL, ISD::SRA, ISD::SRL, ISD::ADD, ISD::SUB,
                          ISD::MUL, ISD::ABS, ISD::SMIN, ISD::SMAX, ISD::UMIN,
                          ISD::UMAX, ISD::UADDSAT, ISD::SADDSAT, ISD::USUBSAT,
                          ISD::SSUBSAT},
                         VT, Custom);

    for (MVT VT : {MVT::v4f16, MVT::v8f16, MVT::v16f16, MVT::v32f16})
      // Split vector operations.
      setOperationAction({ISD::FADD, ISD::FMUL, ISD::FMA, ISD::FCANONICALIZE},
                         VT, Custom);

    setOperationAction({ISD::FMAXNUM, ISD::FMINNUM}, {MVT::v2f16, MVT::v4f16},
                       Custom);

    setOperationAction(ISD::FEXP, MVT::v2f16, Custom);
    setOperationAction(ISD::SELECT, {MVT::v4i16, MVT::v4f16, MVT::v4bf16},
                       Custom);

    if (Subtarget->hasPackedFP32Ops()) {
      setOperationAction({ISD::FADD, ISD::FMUL, ISD::FMA, ISD::FNEG},
                         MVT::v2f32, Legal);
      setOperationAction({ISD::FADD, ISD::FMUL, ISD::FMA},
                         {MVT::v4f32, MVT::v8f32, MVT::v16f32, MVT::v32f32},
                         Custom);
    }
  }

  setOperationAction({ISD::FNEG, ISD::FABS}, MVT::v4f16, Custom);

  if (Subtarget->has16BitInsts()) {
    setOperationAction(ISD::SELECT, MVT::v2i16, Promote);
    AddPromotedToType(ISD::SELECT, MVT::v2i16, MVT::i32);
    setOperationAction(ISD::SELECT, MVT::v2f16, Promote);
    AddPromotedToType(ISD::SELECT, MVT::v2f16, MVT::i32);
  } else {
    // Legalization hack.
    setOperationAction(ISD::SELECT, {MVT::v2i16, MVT::v2f16}, Custom);

    setOperationAction({ISD::FNEG, ISD::FABS}, MVT::v2f16, Custom);
  }

  setOperationAction(ISD::SELECT,
                     {MVT::v4i16, MVT::v4f16, MVT::v4bf16, MVT::v2i8, MVT::v4i8,
                      MVT::v8i8, MVT::v8i16, MVT::v8f16, MVT::v8bf16,
                      MVT::v16i16, MVT::v16f16, MVT::v16bf16, MVT::v32i16,
                      MVT::v32f16, MVT::v32bf16},
                     Custom);

  setOperationAction({ISD::SMULO, ISD::UMULO}, MVT::i64, Custom);

  if (Subtarget->hasScalarSMulU64())
    setOperationAction(ISD::MUL, MVT::i64, Custom);

  if (Subtarget->hasMad64_32())
    setOperationAction({ISD::SMUL_LOHI, ISD::UMUL_LOHI}, MVT::i32, Custom);

  if (Subtarget->hasPrefetch())
    setOperationAction(ISD::PREFETCH, MVT::Other, Custom);

  if (Subtarget->hasIEEEMinMax()) {
    setOperationAction({ISD::FMAXIMUM, ISD::FMINIMUM},
                       {MVT::f16, MVT::f32, MVT::f64, MVT::v2f16}, Legal);
    setOperationAction({ISD::FMINIMUM, ISD::FMAXIMUM},
                       {MVT::v4f16, MVT::v8f16, MVT::v16f16, MVT::v32f16},
                       Custom);
  }

  setOperationAction(ISD::INTRINSIC_WO_CHAIN,
                     {MVT::Other, MVT::f32, MVT::v4f32, MVT::i16, MVT::f16,
                      MVT::bf16, MVT::v2i16, MVT::v2f16, MVT::v2bf16, MVT::i128,
                      MVT::i8},
                     Custom);

  setOperationAction(ISD::INTRINSIC_W_CHAIN,
                     {MVT::v2f16, MVT::v2i16, MVT::v2bf16, MVT::v3f16,
                      MVT::v3i16, MVT::v4f16, MVT::v4i16, MVT::v4bf16,
                      MVT::v8i16, MVT::v8f16, MVT::v8bf16, MVT::Other, MVT::f16,
                      MVT::i16, MVT::bf16, MVT::i8, MVT::i128},
                     Custom);

  setOperationAction(ISD::INTRINSIC_VOID,
                     {MVT::Other, MVT::v2i16, MVT::v2f16, MVT::v2bf16,
                      MVT::v3i16, MVT::v3f16, MVT::v4f16, MVT::v4i16,
                      MVT::v4bf16, MVT::v8i16, MVT::v8f16, MVT::v8bf16,
                      MVT::f16, MVT::i16, MVT::bf16, MVT::i8, MVT::i128},
                     Custom);

  setOperationAction(ISD::STACKSAVE, MVT::Other, Custom);
  setOperationAction(ISD::GET_ROUNDING, MVT::i32, Custom);
  setOperationAction(ISD::SET_ROUNDING, MVT::Other, Custom);
  setOperationAction(ISD::GET_FPENV, MVT::i64, Custom);
  setOperationAction(ISD::SET_FPENV, MVT::i64, Custom);

  // TODO: Could move this to custom lowering, could benefit from combines on
  // extract of relevant bits.
  setOperationAction(ISD::GET_FPMODE, MVT::i32, Legal);

  setOperationAction(ISD::MUL, MVT::i1, Promote);

  setTargetDAGCombine({ISD::ADD,
                       ISD::UADDO_CARRY,
                       ISD::SUB,
                       ISD::USUBO_CARRY,
                       ISD::FADD,
                       ISD::FSUB,
                       ISD::FDIV,
                       ISD::FMINNUM,
                       ISD::FMAXNUM,
                       ISD::FMINNUM_IEEE,
                       ISD::FMAXNUM_IEEE,
                       ISD::FMINIMUM,
                       ISD::FMAXIMUM,
                       ISD::FMA,
                       ISD::SMIN,
                       ISD::SMAX,
                       ISD::UMIN,
                       ISD::UMAX,
                       ISD::SETCC,
                       ISD::AND,
                       ISD::OR,
                       ISD::XOR,
                       ISD::FSHR,
                       ISD::SINT_TO_FP,
                       ISD::UINT_TO_FP,
                       ISD::FCANONICALIZE,
                       ISD::SCALAR_TO_VECTOR,
                       ISD::ZERO_EXTEND,
                       ISD::SIGN_EXTEND_INREG,
                       ISD::EXTRACT_VECTOR_ELT,
                       ISD::INSERT_VECTOR_ELT,
                       ISD::FCOPYSIGN});

  if (Subtarget->has16BitInsts() && !Subtarget->hasMed3_16())
    setTargetDAGCombine(ISD::FP_ROUND);

  // All memory operations. Some folding on the pointer operand is done to help
  // matching the constant offsets in the addressing modes.
  setTargetDAGCombine({ISD::LOAD,
                       ISD::STORE,
                       ISD::ATOMIC_LOAD,
                       ISD::ATOMIC_STORE,
                       ISD::ATOMIC_CMP_SWAP,
                       ISD::ATOMIC_CMP_SWAP_WITH_SUCCESS,
                       ISD::ATOMIC_SWAP,
                       ISD::ATOMIC_LOAD_ADD,
                       ISD::ATOMIC_LOAD_SUB,
                       ISD::ATOMIC_LOAD_AND,
                       ISD::ATOMIC_LOAD_OR,
                       ISD::ATOMIC_LOAD_XOR,
                       ISD::ATOMIC_LOAD_NAND,
                       ISD::ATOMIC_LOAD_MIN,
                       ISD::ATOMIC_LOAD_MAX,
                       ISD::ATOMIC_LOAD_UMIN,
                       ISD::ATOMIC_LOAD_UMAX,
                       ISD::ATOMIC_LOAD_FADD,
                       ISD::ATOMIC_LOAD_FMIN,
                       ISD::ATOMIC_LOAD_FMAX,
                       ISD::ATOMIC_LOAD_UINC_WRAP,
                       ISD::ATOMIC_LOAD_UDEC_WRAP,
                       ISD::INTRINSIC_VOID,
                       ISD::INTRINSIC_W_CHAIN});

  // FIXME: In other contexts we pretend this is a per-function property.
  setStackPointerRegisterToSaveRestore(AMDGPU::SGPR32);

  setSchedulingPreference(Sched::RegPressure);
}

const GCNSubtarget *SITargetLowering::getSubtarget() const {
  return Subtarget;
}

ArrayRef<MCPhysReg> SITargetLowering::getRoundingControlRegisters() const {
  static const MCPhysReg RCRegs[] = {AMDGPU::MODE};
  return RCRegs;
}

//===----------------------------------------------------------------------===//
// TargetLowering queries
//===----------------------------------------------------------------------===//

// v_mad_mix* support a conversion from f16 to f32.
//
// There is only one special case when denormals are enabled we don't currently,
// where this is OK to use.
bool SITargetLowering::isFPExtFoldable(const SelectionDAG &DAG, unsigned Opcode,
                                       EVT DestVT, EVT SrcVT) const {
  return ((Opcode == ISD::FMAD && Subtarget->hasMadMixInsts()) ||
          (Opcode == ISD::FMA && Subtarget->hasFmaMixInsts())) &&
         DestVT.getScalarType() == MVT::f32 &&
         SrcVT.getScalarType() == MVT::f16 &&
         // TODO: This probably only requires no input flushing?
         denormalModeIsFlushAllF32(DAG.getMachineFunction());
}

bool SITargetLowering::isFPExtFoldable(const MachineInstr &MI, unsigned Opcode,
                                       LLT DestTy, LLT SrcTy) const {
  return ((Opcode == TargetOpcode::G_FMAD && Subtarget->hasMadMixInsts()) ||
          (Opcode == TargetOpcode::G_FMA && Subtarget->hasFmaMixInsts())) &&
         DestTy.getScalarSizeInBits() == 32 &&
         SrcTy.getScalarSizeInBits() == 16 &&
         // TODO: This probably only requires no input flushing?
         denormalModeIsFlushAllF32(*MI.getMF());
}

bool SITargetLowering::isShuffleMaskLegal(ArrayRef<int>, EVT) const {
  // SI has some legal vector types, but no legal vector operations. Say no
  // shuffles are legal in order to prefer scalarizing some vector operations.
  return false;
}

MVT SITargetLowering::getRegisterTypeForCallingConv(LLVMContext &Context,
                                                    CallingConv::ID CC,
                                                    EVT VT) const {
  if (CC == CallingConv::AMDGPU_KERNEL)
    return TargetLowering::getRegisterTypeForCallingConv(Context, CC, VT);

  if (VT.isVector()) {
    EVT ScalarVT = VT.getScalarType();
    unsigned Size = ScalarVT.getSizeInBits();
    if (Size == 16) {
      if (Subtarget->has16BitInsts()) {
        if (VT.isInteger())
          return MVT::v2i16;
        return (ScalarVT == MVT::bf16 ? MVT::i32 : MVT::v2f16);
      }
      return VT.isInteger() ? MVT::i32 : MVT::f32;
    }

    if (Size < 16)
      return Subtarget->has16BitInsts() ? MVT::i16 : MVT::i32;
    return Size == 32 ? ScalarVT.getSimpleVT() : MVT::i32;
  }

  if (VT.getSizeInBits() > 32)
    return MVT::i32;

  return TargetLowering::getRegisterTypeForCallingConv(Context, CC, VT);
}

unsigned SITargetLowering::getNumRegistersForCallingConv(LLVMContext &Context,
                                                         CallingConv::ID CC,
                                                         EVT VT) const {
  if (CC == CallingConv::AMDGPU_KERNEL)
    return TargetLowering::getNumRegistersForCallingConv(Context, CC, VT);

  if (VT.isVector()) {
    unsigned NumElts = VT.getVectorNumElements();
    EVT ScalarVT = VT.getScalarType();
    unsigned Size = ScalarVT.getSizeInBits();

    // FIXME: Should probably promote 8-bit vectors to i16.
    if (Size == 16 && Subtarget->has16BitInsts())
      return (NumElts + 1) / 2;

    if (Size <= 32)
      return NumElts;

    if (Size > 32)
      return NumElts * ((Size + 31) / 32);
  } else if (VT.getSizeInBits() > 32)
    return (VT.getSizeInBits() + 31) / 32;

  return TargetLowering::getNumRegistersForCallingConv(Context, CC, VT);
}

unsigned SITargetLowering::getVectorTypeBreakdownForCallingConv(
  LLVMContext &Context, CallingConv::ID CC,
  EVT VT, EVT &IntermediateVT,
  unsigned &NumIntermediates, MVT &RegisterVT) const {
  if (CC != CallingConv::AMDGPU_KERNEL && VT.isVector()) {
    unsigned NumElts = VT.getVectorNumElements();
    EVT ScalarVT = VT.getScalarType();
    unsigned Size = ScalarVT.getSizeInBits();
    // FIXME: We should fix the ABI to be the same on targets without 16-bit
    // support, but unless we can properly handle 3-vectors, it will be still be
    // inconsistent.
    if (Size == 16 && Subtarget->has16BitInsts()) {
      if (ScalarVT == MVT::bf16) {
        RegisterVT = MVT::i32;
        IntermediateVT = MVT::v2bf16;
      } else {
        RegisterVT = VT.isInteger() ? MVT::v2i16 : MVT::v2f16;
        IntermediateVT = RegisterVT;
      }
      NumIntermediates = (NumElts + 1) / 2;
      return NumIntermediates;
    }

    if (Size == 32) {
      RegisterVT = ScalarVT.getSimpleVT();
      IntermediateVT = RegisterVT;
      NumIntermediates = NumElts;
      return NumIntermediates;
    }

    if (Size < 16 && Subtarget->has16BitInsts()) {
      // FIXME: Should probably form v2i16 pieces
      RegisterVT = MVT::i16;
      IntermediateVT = ScalarVT;
      NumIntermediates = NumElts;
      return NumIntermediates;
    }


    if (Size != 16 && Size <= 32) {
      RegisterVT = MVT::i32;
      IntermediateVT = ScalarVT;
      NumIntermediates = NumElts;
      return NumIntermediates;
    }

    if (Size > 32) {
      RegisterVT = MVT::i32;
      IntermediateVT = RegisterVT;
      NumIntermediates = NumElts * ((Size + 31) / 32);
      return NumIntermediates;
    }
  }

  return TargetLowering::getVectorTypeBreakdownForCallingConv(
    Context, CC, VT, IntermediateVT, NumIntermediates, RegisterVT);
}

static EVT memVTFromLoadIntrData(const SITargetLowering &TLI,
                                 const DataLayout &DL, Type *Ty,
                                 unsigned MaxNumLanes) {
  assert(MaxNumLanes != 0);

  LLVMContext &Ctx = Ty->getContext();
  if (auto *VT = dyn_cast<FixedVectorType>(Ty)) {
    unsigned NumElts = std::min(MaxNumLanes, VT->getNumElements());
    return EVT::getVectorVT(Ctx, TLI.getValueType(DL, VT->getElementType()),
                            NumElts);
  }

  return TLI.getValueType(DL, Ty);
}

// Peek through TFE struct returns to only use the data size.
static EVT memVTFromLoadIntrReturn(const SITargetLowering &TLI,
                                   const DataLayout &DL, Type *Ty,
                                   unsigned MaxNumLanes) {
  auto *ST = dyn_cast<StructType>(Ty);
  if (!ST)
    return memVTFromLoadIntrData(TLI, DL, Ty, MaxNumLanes);

  // TFE intrinsics return an aggregate type.
  assert(ST->getNumContainedTypes() == 2 &&
         ST->getContainedType(1)->isIntegerTy(32));
  return memVTFromLoadIntrData(TLI, DL, ST->getContainedType(0), MaxNumLanes);
}

/// Map address space 7 to MVT::v5i32 because that's its in-memory
/// representation. This return value is vector-typed because there is no
/// MVT::i160 and it is not clear if one can be added. While this could
/// cause issues during codegen, these address space 7 pointers will be
/// rewritten away by then. Therefore, we can return MVT::v5i32 in order
/// to allow pre-codegen passes that query TargetTransformInfo, often for cost
/// modeling, to work.
MVT SITargetLowering::getPointerTy(const DataLayout &DL, unsigned AS) const {
  if (AMDGPUAS::BUFFER_FAT_POINTER == AS && DL.getPointerSizeInBits(AS) == 160)
    return MVT::v5i32;
  if (AMDGPUAS::BUFFER_STRIDED_POINTER == AS &&
      DL.getPointerSizeInBits(AS) == 192)
    return MVT::v6i32;
  return AMDGPUTargetLowering::getPointerTy(DL, AS);
}
/// Similarly, the in-memory representation of a p7 is {p8, i32}, aka
/// v8i32 when padding is added.
/// The in-memory representation of a p9 is {p8, i32, i32}, which is
/// also v8i32 with padding.
MVT SITargetLowering::getPointerMemTy(const DataLayout &DL, unsigned AS) const {
  if ((AMDGPUAS::BUFFER_FAT_POINTER == AS &&
       DL.getPointerSizeInBits(AS) == 160) ||
      (AMDGPUAS::BUFFER_STRIDED_POINTER == AS &&
       DL.getPointerSizeInBits(AS) == 192))
    return MVT::v8i32;
  return AMDGPUTargetLowering::getPointerMemTy(DL, AS);
}

bool SITargetLowering::getTgtMemIntrinsic(IntrinsicInfo &Info,
                                          const CallInst &CI,
                                          MachineFunction &MF,
                                          unsigned IntrID) const {
  Info.flags = MachineMemOperand::MONone;
  if (CI.hasMetadata(LLVMContext::MD_invariant_load))
    Info.flags |= MachineMemOperand::MOInvariant;

  if (const AMDGPU::RsrcIntrinsic *RsrcIntr =
          AMDGPU::lookupRsrcIntrinsic(IntrID)) {
    AttributeList Attr = Intrinsic::getAttributes(CI.getContext(),
                                                  (Intrinsic::ID)IntrID);
    MemoryEffects ME = Attr.getMemoryEffects();
    if (ME.doesNotAccessMemory())
      return false;

    // TODO: Should images get their own address space?
    Info.fallbackAddressSpace = AMDGPUAS::BUFFER_RESOURCE;

    const AMDGPU::MIMGBaseOpcodeInfo *BaseOpcode = nullptr;
    if (RsrcIntr->IsImage) {
      const AMDGPU::ImageDimIntrinsicInfo *Intr =
          AMDGPU::getImageDimIntrinsicInfo(IntrID);
      BaseOpcode = AMDGPU::getMIMGBaseOpcodeInfo(Intr->BaseOpcode);
      Info.align.reset();
    }

    Value *RsrcArg = CI.getArgOperand(RsrcIntr->RsrcArg);
    if (auto *RsrcPtrTy = dyn_cast<PointerType>(RsrcArg->getType())) {
      if (RsrcPtrTy->getAddressSpace() == AMDGPUAS::BUFFER_RESOURCE)
        // We conservatively set the memory operand of a buffer intrinsic to the
        // base resource pointer, so that we can access alias information about
        // those pointers. Cases like "this points at the same value
        // but with a different offset" are handled in
        // areMemAccessesTriviallyDisjoint.
        Info.ptrVal = RsrcArg;
    }

    auto *Aux = cast<ConstantInt>(CI.getArgOperand(CI.arg_size() - 1));
    if (Aux->getZExtValue() & AMDGPU::CPol::VOLATILE)
      Info.flags |= MachineMemOperand::MOVolatile;
    Info.flags |= MachineMemOperand::MODereferenceable;
    if (ME.onlyReadsMemory()) {
      if (RsrcIntr->IsImage) {
        unsigned MaxNumLanes = 4;

        if (!BaseOpcode->Gather4) {
          // If this isn't a gather, we may have excess loaded elements in the
          // IR type. Check the dmask for the real number of elements loaded.
          unsigned DMask
            = cast<ConstantInt>(CI.getArgOperand(0))->getZExtValue();
          MaxNumLanes = DMask == 0 ? 1 : llvm::popcount(DMask);
        }

        Info.memVT = memVTFromLoadIntrReturn(*this, MF.getDataLayout(),
                                             CI.getType(), MaxNumLanes);
      } else {
        Info.memVT =
            memVTFromLoadIntrReturn(*this, MF.getDataLayout(), CI.getType(),
                                    std::numeric_limits<unsigned>::max());
      }

      // FIXME: What does alignment mean for an image?
      Info.opc = ISD::INTRINSIC_W_CHAIN;
      Info.flags |= MachineMemOperand::MOLoad;
    } else if (ME.onlyWritesMemory()) {
      Info.opc = ISD::INTRINSIC_VOID;

      Type *DataTy = CI.getArgOperand(0)->getType();
      if (RsrcIntr->IsImage) {
        unsigned DMask = cast<ConstantInt>(CI.getArgOperand(1))->getZExtValue();
        unsigned DMaskLanes = DMask == 0 ? 1 : llvm::popcount(DMask);
        Info.memVT = memVTFromLoadIntrData(*this, MF.getDataLayout(), DataTy,
                                           DMaskLanes);
      } else
        Info.memVT = getValueType(MF.getDataLayout(), DataTy);

      Info.flags |= MachineMemOperand::MOStore;
    } else {
      // Atomic or NoReturn Sampler
      Info.opc = CI.getType()->isVoidTy() ? ISD::INTRINSIC_VOID :
                                            ISD::INTRINSIC_W_CHAIN;
      Info.flags |= MachineMemOperand::MOLoad |
                    MachineMemOperand::MOStore |
                    MachineMemOperand::MODereferenceable;

      switch (IntrID) {
      default:
        if (RsrcIntr->IsImage && BaseOpcode->NoReturn) {
          // Fake memory access type for no return sampler intrinsics
          Info.memVT = MVT::i32;
        } else {
          // XXX - Should this be volatile without known ordering?
          Info.flags |= MachineMemOperand::MOVolatile;
          Info.memVT = MVT::getVT(CI.getArgOperand(0)->getType());
        }
        break;
      case Intrinsic::amdgcn_raw_buffer_load_lds:
      case Intrinsic::amdgcn_raw_ptr_buffer_load_lds:
      case Intrinsic::amdgcn_struct_buffer_load_lds:
      case Intrinsic::amdgcn_struct_ptr_buffer_load_lds: {
        unsigned Width = cast<ConstantInt>(CI.getArgOperand(2))->getZExtValue();
        Info.memVT = EVT::getIntegerVT(CI.getContext(), Width * 8);
        Info.ptrVal = CI.getArgOperand(1);
        return true;
      }
      case Intrinsic::amdgcn_raw_atomic_buffer_load:
      case Intrinsic::amdgcn_raw_ptr_atomic_buffer_load: {
        Info.memVT =
            memVTFromLoadIntrReturn(*this, MF.getDataLayout(), CI.getType(),
                                    std::numeric_limits<unsigned>::max());
        Info.flags &= ~MachineMemOperand::MOStore;
        return true;
      }
      }
    }
    return true;
  }

  switch (IntrID) {
  case Intrinsic::amdgcn_ds_ordered_add:
  case Intrinsic::amdgcn_ds_ordered_swap: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::getVT(CI.getType());
    Info.ptrVal = CI.getOperand(0);
    Info.align.reset();
    Info.flags |= MachineMemOperand::MOLoad | MachineMemOperand::MOStore;

    const ConstantInt *Vol = cast<ConstantInt>(CI.getOperand(4));
    if (!Vol->isZero())
      Info.flags |= MachineMemOperand::MOVolatile;

    return true;
  }
  case Intrinsic::amdgcn_ds_add_gs_reg_rtn:
  case Intrinsic::amdgcn_ds_sub_gs_reg_rtn: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::getVT(CI.getOperand(0)->getType());
    Info.ptrVal = nullptr;
    Info.fallbackAddressSpace = AMDGPUAS::STREAMOUT_REGISTER;
    Info.flags = MachineMemOperand::MOLoad | MachineMemOperand::MOStore;
    return true;
  }
  case Intrinsic::amdgcn_ds_append:
  case Intrinsic::amdgcn_ds_consume: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::getVT(CI.getType());
    Info.ptrVal = CI.getOperand(0);
    Info.align.reset();
    Info.flags |= MachineMemOperand::MOLoad | MachineMemOperand::MOStore;

    const ConstantInt *Vol = cast<ConstantInt>(CI.getOperand(1));
    if (!Vol->isZero())
      Info.flags |= MachineMemOperand::MOVolatile;

    return true;
  }
  case Intrinsic::amdgcn_global_atomic_csub: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::getVT(CI.getType());
    Info.ptrVal = CI.getOperand(0);
    Info.align.reset();
    Info.flags |= MachineMemOperand::MOLoad |
                  MachineMemOperand::MOStore |
                  MachineMemOperand::MOVolatile;
    return true;
  }
  case Intrinsic::amdgcn_image_bvh_intersect_ray: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::getVT(CI.getType()); // XXX: what is correct VT?

    Info.fallbackAddressSpace = AMDGPUAS::BUFFER_RESOURCE;
    Info.align.reset();
    Info.flags |= MachineMemOperand::MOLoad |
                  MachineMemOperand::MODereferenceable;
    return true;
  }
  case Intrinsic::amdgcn_global_atomic_fadd:
  case Intrinsic::amdgcn_global_atomic_fmin:
  case Intrinsic::amdgcn_global_atomic_fmax:
  case Intrinsic::amdgcn_global_atomic_fmin_num:
  case Intrinsic::amdgcn_global_atomic_fmax_num:
  case Intrinsic::amdgcn_global_atomic_ordered_add_b64:
  case Intrinsic::amdgcn_flat_atomic_fadd:
  case Intrinsic::amdgcn_flat_atomic_fmin:
  case Intrinsic::amdgcn_flat_atomic_fmax:
  case Intrinsic::amdgcn_flat_atomic_fmin_num:
  case Intrinsic::amdgcn_flat_atomic_fmax_num:
  case Intrinsic::amdgcn_global_atomic_fadd_v2bf16:
  case Intrinsic::amdgcn_atomic_cond_sub_u32:
  case Intrinsic::amdgcn_flat_atomic_fadd_v2bf16: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::getVT(CI.getType());
    Info.ptrVal = CI.getOperand(0);
    Info.align.reset();
    Info.flags |= MachineMemOperand::MOLoad |
                  MachineMemOperand::MOStore |
                  MachineMemOperand::MODereferenceable |
                  MachineMemOperand::MOVolatile;
    return true;
  }
  case Intrinsic::amdgcn_global_load_tr_b64:
  case Intrinsic::amdgcn_global_load_tr_b128: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;
    Info.memVT = MVT::getVT(CI.getType());
    Info.ptrVal = CI.getOperand(0);
    Info.align.reset();
    Info.flags |= MachineMemOperand::MOLoad;
    return true;
  }
  case Intrinsic::amdgcn_ds_gws_init:
  case Intrinsic::amdgcn_ds_gws_barrier:
  case Intrinsic::amdgcn_ds_gws_sema_v:
  case Intrinsic::amdgcn_ds_gws_sema_br:
  case Intrinsic::amdgcn_ds_gws_sema_p:
  case Intrinsic::amdgcn_ds_gws_sema_release_all: {
    Info.opc = ISD::INTRINSIC_VOID;

    const GCNTargetMachine &TM =
        static_cast<const GCNTargetMachine &>(getTargetMachine());

    SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
    Info.ptrVal = MFI->getGWSPSV(TM);

    // This is an abstract access, but we need to specify a type and size.
    Info.memVT = MVT::i32;
    Info.size = 4;
    Info.align = Align(4);

    if (IntrID == Intrinsic::amdgcn_ds_gws_barrier)
      Info.flags |= MachineMemOperand::MOLoad;
    else
      Info.flags |= MachineMemOperand::MOStore;
    return true;
  }
  case Intrinsic::amdgcn_global_load_lds: {
    Info.opc = ISD::INTRINSIC_VOID;
    unsigned Width = cast<ConstantInt>(CI.getArgOperand(2))->getZExtValue();
    Info.memVT = EVT::getIntegerVT(CI.getContext(), Width * 8);
    Info.ptrVal = CI.getArgOperand(1);
    Info.flags |= MachineMemOperand::MOLoad | MachineMemOperand::MOStore;
    return true;
  }
  case Intrinsic::amdgcn_ds_bvh_stack_rtn: {
    Info.opc = ISD::INTRINSIC_W_CHAIN;

    const GCNTargetMachine &TM =
        static_cast<const GCNTargetMachine &>(getTargetMachine());

    SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
    Info.ptrVal = MFI->getGWSPSV(TM);

    // This is an abstract access, but we need to specify a type and size.
    Info.memVT = MVT::i32;
    Info.size = 4;
    Info.align = Align(4);

    Info.flags = MachineMemOperand::MOLoad | MachineMemOperand::MOStore;
    return true;
  }
  default:
    return false;
  }
}

void SITargetLowering::CollectTargetIntrinsicOperands(
    const CallInst &I, SmallVectorImpl<SDValue> &Ops, SelectionDAG &DAG) const {
  switch (cast<IntrinsicInst>(I).getIntrinsicID()) {
  case Intrinsic::amdgcn_addrspacecast_nonnull: {
    // The DAG's ValueType loses the addrspaces.
    // Add them as 2 extra Constant operands "from" and "to".
    unsigned SrcAS = I.getOperand(0)->getType()->getPointerAddressSpace();
    unsigned DstAS = I.getType()->getPointerAddressSpace();
    Ops.push_back(DAG.getTargetConstant(SrcAS, SDLoc(), MVT::i32));
    Ops.push_back(DAG.getTargetConstant(DstAS, SDLoc(), MVT::i32));
    break;
  }
  default:
    break;
  }
}

bool SITargetLowering::getAddrModeArguments(IntrinsicInst *II,
                                            SmallVectorImpl<Value*> &Ops,
                                            Type *&AccessTy) const {
  Value *Ptr = nullptr;
  switch (II->getIntrinsicID()) {
  case Intrinsic::amdgcn_atomic_cond_sub_u32:
  case Intrinsic::amdgcn_ds_append:
  case Intrinsic::amdgcn_ds_consume:
  case Intrinsic::amdgcn_ds_ordered_add:
  case Intrinsic::amdgcn_ds_ordered_swap:
  case Intrinsic::amdgcn_flat_atomic_fadd:
  case Intrinsic::amdgcn_flat_atomic_fadd_v2bf16:
  case Intrinsic::amdgcn_flat_atomic_fmax:
  case Intrinsic::amdgcn_flat_atomic_fmax_num:
  case Intrinsic::amdgcn_flat_atomic_fmin:
  case Intrinsic::amdgcn_flat_atomic_fmin_num:
  case Intrinsic::amdgcn_global_atomic_csub:
  case Intrinsic::amdgcn_global_atomic_fadd:
  case Intrinsic::amdgcn_global_atomic_fadd_v2bf16:
  case Intrinsic::amdgcn_global_atomic_fmax:
  case Intrinsic::amdgcn_global_atomic_fmax_num:
  case Intrinsic::amdgcn_global_atomic_fmin:
  case Intrinsic::amdgcn_global_atomic_fmin_num:
  case Intrinsic::amdgcn_global_atomic_ordered_add_b64:
  case Intrinsic::amdgcn_global_load_tr_b64:
  case Intrinsic::amdgcn_global_load_tr_b128:
    Ptr = II->getArgOperand(0);
    break;
  case Intrinsic::amdgcn_global_load_lds:
    Ptr = II->getArgOperand(1);
    break;
  default:
    return false;
  }
  AccessTy = II->getType();
  Ops.push_back(Ptr);
  return true;
}

bool SITargetLowering::isLegalFlatAddressingMode(const AddrMode &AM,
                                                 unsigned AddrSpace) const {
  if (!Subtarget->hasFlatInstOffsets()) {
    // Flat instructions do not have offsets, and only have the register
    // address.
    return AM.BaseOffs == 0 && AM.Scale == 0;
  }

  decltype(SIInstrFlags::FLAT) FlatVariant =
      AddrSpace == AMDGPUAS::GLOBAL_ADDRESS    ? SIInstrFlags::FlatGlobal
      : AddrSpace == AMDGPUAS::PRIVATE_ADDRESS ? SIInstrFlags::FlatScratch
                                               : SIInstrFlags::FLAT;

  return AM.Scale == 0 &&
         (AM.BaseOffs == 0 || Subtarget->getInstrInfo()->isLegalFLATOffset(
                                  AM.BaseOffs, AddrSpace, FlatVariant));
}

bool SITargetLowering::isLegalGlobalAddressingMode(const AddrMode &AM) const {
  if (Subtarget->hasFlatGlobalInsts())
    return isLegalFlatAddressingMode(AM, AMDGPUAS::GLOBAL_ADDRESS);

  if (!Subtarget->hasAddr64() || Subtarget->useFlatForGlobal()) {
    // Assume the we will use FLAT for all global memory accesses
    // on VI.
    // FIXME: This assumption is currently wrong.  On VI we still use
    // MUBUF instructions for the r + i addressing mode.  As currently
    // implemented, the MUBUF instructions only work on buffer < 4GB.
    // It may be possible to support > 4GB buffers with MUBUF instructions,
    // by setting the stride value in the resource descriptor which would
    // increase the size limit to (stride * 4GB).  However, this is risky,
    // because it has never been validated.
    return isLegalFlatAddressingMode(AM, AMDGPUAS::FLAT_ADDRESS);
  }

  return isLegalMUBUFAddressingMode(AM);
}

bool SITargetLowering::isLegalMUBUFAddressingMode(const AddrMode &AM) const {
  // MUBUF / MTBUF instructions have a 12-bit unsigned byte offset, and
  // additionally can do r + r + i with addr64. 32-bit has more addressing
  // mode options. Depending on the resource constant, it can also do
  // (i64 r0) + (i32 r1) * (i14 i).
  //
  // Private arrays end up using a scratch buffer most of the time, so also
  // assume those use MUBUF instructions. Scratch loads / stores are currently
  // implemented as mubuf instructions with offen bit set, so slightly
  // different than the normal addr64.
  const SIInstrInfo *TII = Subtarget->getInstrInfo();
  if (!TII->isLegalMUBUFImmOffset(AM.BaseOffs))
    return false;

  // FIXME: Since we can split immediate into soffset and immediate offset,
  // would it make sense to allow any immediate?

  switch (AM.Scale) {
  case 0: // r + i or just i, depending on HasBaseReg.
    return true;
  case 1:
    return true; // We have r + r or r + i.
  case 2:
    if (AM.HasBaseReg) {
      // Reject 2 * r + r.
      return false;
    }

    // Allow 2 * r as r + r
    // Or  2 * r + i is allowed as r + r + i.
    return true;
  default: // Don't allow n * r
    return false;
  }
}

bool SITargetLowering::isLegalAddressingMode(const DataLayout &DL,
                                             const AddrMode &AM, Type *Ty,
                                             unsigned AS, Instruction *I) const {
  // No global is ever allowed as a base.
  if (AM.BaseGV)
    return false;

  if (AS == AMDGPUAS::GLOBAL_ADDRESS)
    return isLegalGlobalAddressingMode(AM);

  if (AS == AMDGPUAS::CONSTANT_ADDRESS ||
      AS == AMDGPUAS::CONSTANT_ADDRESS_32BIT ||
      AS == AMDGPUAS::BUFFER_FAT_POINTER || AS == AMDGPUAS::BUFFER_RESOURCE ||
      AS == AMDGPUAS::BUFFER_STRIDED_POINTER) {
    // If the offset isn't a multiple of 4, it probably isn't going to be
    // correctly aligned.
    // FIXME: Can we get the real alignment here?
    if (AM.BaseOffs % 4 != 0)
      return isLegalMUBUFAddressingMode(AM);

    if (!Subtarget->hasScalarSubwordLoads()) {
      // There are no SMRD extloads, so if we have to do a small type access we
      // will use a MUBUF load.
      // FIXME?: We also need to do this if unaligned, but we don't know the
      // alignment here.
      if (Ty->isSized() && DL.getTypeStoreSize(Ty) < 4)
        return isLegalGlobalAddressingMode(AM);
    }

    if (Subtarget->getGeneration() == AMDGPUSubtarget::SOUTHERN_ISLANDS) {
      // SMRD instructions have an 8-bit, dword offset on SI.
      if (!isUInt<8>(AM.BaseOffs / 4))
        return false;
    } else if (Subtarget->getGeneration() == AMDGPUSubtarget::SEA_ISLANDS) {
      // On CI+, this can also be a 32-bit literal constant offset. If it fits
      // in 8-bits, it can use a smaller encoding.
      if (!isUInt<32>(AM.BaseOffs / 4))
        return false;
    } else if (Subtarget->getGeneration() < AMDGPUSubtarget::GFX9) {
      // On VI, these use the SMEM format and the offset is 20-bit in bytes.
      if (!isUInt<20>(AM.BaseOffs))
        return false;
    } else if (Subtarget->getGeneration() < AMDGPUSubtarget::GFX12) {
      // On GFX9 the offset is signed 21-bit in bytes (but must not be negative
      // for S_BUFFER_* instructions).
      if (!isInt<21>(AM.BaseOffs))
        return false;
    } else {
      // On GFX12, all offsets are signed 24-bit in bytes.
      if (!isInt<24>(AM.BaseOffs))
        return false;
    }

    if ((AS == AMDGPUAS::CONSTANT_ADDRESS ||
         AS == AMDGPUAS::CONSTANT_ADDRESS_32BIT) &&
        AM.BaseOffs < 0) {
      // Scalar (non-buffer) loads can only use a negative offset if
      // soffset+offset is non-negative. Since the compiler can only prove that
      // in a few special cases, it is safer to claim that negative offsets are
      // not supported.
      return false;
    }

    if (AM.Scale == 0) // r + i or just i, depending on HasBaseReg.
      return true;

    if (AM.Scale == 1 && AM.HasBaseReg)
      return true;

    return false;
  }

  if (AS == AMDGPUAS::PRIVATE_ADDRESS)
    return Subtarget->enableFlatScratch()
               ? isLegalFlatAddressingMode(AM, AMDGPUAS::PRIVATE_ADDRESS)
               : isLegalMUBUFAddressingMode(AM);

  if (AS == AMDGPUAS::LOCAL_ADDRESS ||
      (AS == AMDGPUAS::REGION_ADDRESS && Subtarget->hasGDS())) {
    // Basic, single offset DS instructions allow a 16-bit unsigned immediate
    // field.
    // XXX - If doing a 4-byte aligned 8-byte type access, we effectively have
    // an 8-bit dword offset but we don't know the alignment here.
    if (!isUInt<16>(AM.BaseOffs))
      return false;

    if (AM.Scale == 0) // r + i or just i, depending on HasBaseReg.
      return true;

    if (AM.Scale == 1 && AM.HasBaseReg)
      return true;

    return false;
  }

  if (AS == AMDGPUAS::FLAT_ADDRESS || AS == AMDGPUAS::UNKNOWN_ADDRESS_SPACE) {
    // For an unknown address space, this usually means that this is for some
    // reason being used for pure arithmetic, and not based on some addressing
    // computation. We don't have instructions that compute pointers with any
    // addressing modes, so treat them as having no offset like flat
    // instructions.
    return isLegalFlatAddressingMode(AM, AMDGPUAS::FLAT_ADDRESS);
  }

  // Assume a user alias of global for unknown address spaces.
  return isLegalGlobalAddressingMode(AM);
}

bool SITargetLowering::canMergeStoresTo(unsigned AS, EVT MemVT,
                                        const MachineFunction &MF) const {
  if (AS == AMDGPUAS::GLOBAL_ADDRESS || AS == AMDGPUAS::FLAT_ADDRESS)
    return (MemVT.getSizeInBits() <= 4 * 32);
  if (AS == AMDGPUAS::PRIVATE_ADDRESS) {
    unsigned MaxPrivateBits = 8 * getSubtarget()->getMaxPrivateElementSize();
    return (MemVT.getSizeInBits() <= MaxPrivateBits);
  }
  if (AS == AMDGPUAS::LOCAL_ADDRESS || AS == AMDGPUAS::REGION_ADDRESS)
    return (MemVT.getSizeInBits() <= 2 * 32);
  return true;
}

bool SITargetLowering::allowsMisalignedMemoryAccessesImpl(
    unsigned Size, unsigned AddrSpace, Align Alignment,
    MachineMemOperand::Flags Flags, unsigned *IsFast) const {
  if (IsFast)
    *IsFast = 0;

  if (AddrSpace == AMDGPUAS::LOCAL_ADDRESS ||
      AddrSpace == AMDGPUAS::REGION_ADDRESS) {
    // Check if alignment requirements for ds_read/write instructions are
    // disabled.
    if (!Subtarget->hasUnalignedDSAccessEnabled() && Alignment < Align(4))
      return false;

    Align RequiredAlignment(PowerOf2Ceil(Size/8)); // Natural alignment.
    if (Subtarget->hasLDSMisalignedBug() && Size > 32 &&
        Alignment < RequiredAlignment)
      return false;

    // Either, the alignment requirements are "enabled", or there is an
    // unaligned LDS access related hardware bug though alignment requirements
    // are "disabled". In either case, we need to check for proper alignment
    // requirements.
    //
    switch (Size) {
    case 64:
      // SI has a hardware bug in the LDS / GDS bounds checking: if the base
      // address is negative, then the instruction is incorrectly treated as
      // out-of-bounds even if base + offsets is in bounds. Split vectorized
      // loads here to avoid emitting ds_read2_b32. We may re-combine the
      // load later in the SILoadStoreOptimizer.
      if (!Subtarget->hasUsableDSOffset() && Alignment < Align(8))
        return false;

      // 8 byte accessing via ds_read/write_b64 require 8-byte alignment, but we
      // can do a 4 byte aligned, 8 byte access in a single operation using
      // ds_read2/write2_b32 with adjacent offsets.
      RequiredAlignment = Align(4);

      if (Subtarget->hasUnalignedDSAccessEnabled()) {
        // We will either select ds_read_b64/ds_write_b64 or ds_read2_b32/
        // ds_write2_b32 depending on the alignment. In either case with either
        // alignment there is no faster way of doing this.

        // The numbers returned here and below are not additive, it is a 'speed
        // rank'. They are just meant to be compared to decide if a certain way
        // of lowering an operation is faster than another. For that purpose
        // naturally aligned operation gets it bitsize to indicate that "it
        // operates with a speed comparable to N-bit wide load". With the full
        // alignment ds128 is slower than ds96 for example. If underaligned it
        // is comparable to a speed of a single dword access, which would then
        // mean 32 < 128 and it is faster to issue a wide load regardless.
        // 1 is simply "slow, don't do it". I.e. comparing an aligned load to a
        // wider load which will not be aligned anymore the latter is slower.
        if (IsFast)
          *IsFast = (Alignment >= RequiredAlignment) ? 64
                    : (Alignment < Align(4))         ? 32
                                                     : 1;
        return true;
      }

      break;
    case 96:
      if (!Subtarget->hasDS96AndDS128())
        return false;

      // 12 byte accessing via ds_read/write_b96 require 16-byte alignment on
      // gfx8 and older.

      if (Subtarget->hasUnalignedDSAccessEnabled()) {
        // Naturally aligned access is fastest. However, also report it is Fast
        // if memory is aligned less than DWORD. A narrow load or store will be
        // be equally slow as a single ds_read_b96/ds_write_b96, but there will
        // be more of them, so overall we will pay less penalty issuing a single
        // instruction.

        // See comment on the values above.
        if (IsFast)
          *IsFast = (Alignment >= RequiredAlignment) ? 96
                    : (Alignment < Align(4))         ? 32
                                                     : 1;
        return true;
      }

      break;
    case 128:
      if (!Subtarget->hasDS96AndDS128() || !Subtarget->useDS128())
        return false;

      // 16 byte accessing via ds_read/write_b128 require 16-byte alignment on
      // gfx8 and older, but  we can do a 8 byte aligned, 16 byte access in a
      // single operation using ds_read2/write2_b64.
      RequiredAlignment = Align(8);

      if (Subtarget->hasUnalignedDSAccessEnabled()) {
        // Naturally aligned access is fastest. However, also report it is Fast
        // if memory is aligned less than DWORD. A narrow load or store will be
        // be equally slow as a single ds_read_b128/ds_write_b128, but there
        // will be more of them, so overall we will pay less penalty issuing a
        // single instruction.

        // See comment on the values above.
        if (IsFast)
          *IsFast = (Alignment >= RequiredAlignment) ? 128
                    : (Alignment < Align(4))         ? 32
                                                     : 1;
        return true;
      }

      break;
    default:
      if (Size > 32)
        return false;

      break;
    }

    // See comment on the values above.
    // Note that we have a single-dword or sub-dword here, so if underaligned
    // it is a slowest possible access, hence returned value is 0.
    if (IsFast)
      *IsFast = (Alignment >= RequiredAlignment) ? Size : 0;

    return Alignment >= RequiredAlignment ||
           Subtarget->hasUnalignedDSAccessEnabled();
  }

  if (AddrSpace == AMDGPUAS::PRIVATE_ADDRESS) {
    bool AlignedBy4 = Alignment >= Align(4);
    if (IsFast)
      *IsFast = AlignedBy4;

    return AlignedBy4 ||
           Subtarget->enableFlatScratch() ||
           Subtarget->hasUnalignedScratchAccess();
  }

  // FIXME: We have to be conservative here and assume that flat operations
  // will access scratch.  If we had access to the IR function, then we
  // could determine if any private memory was used in the function.
  if (AddrSpace == AMDGPUAS::FLAT_ADDRESS &&
      !Subtarget->hasUnalignedScratchAccess()) {
    bool AlignedBy4 = Alignment >= Align(4);
    if (IsFast)
      *IsFast = AlignedBy4;

    return AlignedBy4;
  }

  // So long as they are correct, wide global memory operations perform better
  // than multiple smaller memory ops -- even when misaligned
  if (AMDGPU::isExtendedGlobalAddrSpace(AddrSpace)) {
    if (IsFast)
      *IsFast = Size;

    return Alignment >= Align(4) ||
           Subtarget->hasUnalignedBufferAccessEnabled();
  }

  // Smaller than dword value must be aligned.
  if (Size < 32)
    return false;

  // 8.1.6 - For Dword or larger reads or writes, the two LSBs of the
  // byte-address are ignored, thus forcing Dword alignment.
  // This applies to private, global, and constant memory.
  if (IsFast)
    *IsFast = 1;

  return Size >= 32 && Alignment >= Align(4);
}

bool SITargetLowering::allowsMisalignedMemoryAccesses(
    EVT VT, unsigned AddrSpace, Align Alignment, MachineMemOperand::Flags Flags,
    unsigned *IsFast) const {
  return allowsMisalignedMemoryAccessesImpl(VT.getSizeInBits(), AddrSpace,
                                            Alignment, Flags, IsFast);
}

EVT SITargetLowering::getOptimalMemOpType(
    const MemOp &Op, const AttributeList &FuncAttributes) const {
  // FIXME: Should account for address space here.

  // The default fallback uses the private pointer size as a guess for a type to
  // use. Make sure we switch these to 64-bit accesses.

  if (Op.size() >= 16 &&
      Op.isDstAligned(Align(4))) // XXX: Should only do for global
    return MVT::v4i32;

  if (Op.size() >= 8 && Op.isDstAligned(Align(4)))
    return MVT::v2i32;

  // Use the default.
  return MVT::Other;
}

bool SITargetLowering::isMemOpHasNoClobberedMemOperand(const SDNode *N) const {
  const MemSDNode *MemNode = cast<MemSDNode>(N);
  return MemNode->getMemOperand()->getFlags() & MONoClobber;
}

bool SITargetLowering::isNonGlobalAddrSpace(unsigned AS) {
  return AS == AMDGPUAS::LOCAL_ADDRESS || AS == AMDGPUAS::REGION_ADDRESS ||
         AS == AMDGPUAS::PRIVATE_ADDRESS;
}

bool SITargetLowering::isFreeAddrSpaceCast(unsigned SrcAS,
                                           unsigned DestAS) const {
  // Flat -> private/local is a simple truncate.
  // Flat -> global is no-op
  if (SrcAS == AMDGPUAS::FLAT_ADDRESS)
    return true;

  const GCNTargetMachine &TM =
      static_cast<const GCNTargetMachine &>(getTargetMachine());
  return TM.isNoopAddrSpaceCast(SrcAS, DestAS);
}

bool SITargetLowering::isMemOpUniform(const SDNode *N) const {
  const MemSDNode *MemNode = cast<MemSDNode>(N);

  return AMDGPUInstrInfo::isUniformMMO(MemNode->getMemOperand());
}

TargetLoweringBase::LegalizeTypeAction
SITargetLowering::getPreferredVectorAction(MVT VT) const {
  if (!VT.isScalableVector() && VT.getVectorNumElements() != 1 &&
      VT.getScalarType().bitsLE(MVT::i16))
    return VT.isPow2VectorType() ? TypeSplitVector : TypeWidenVector;
  return TargetLoweringBase::getPreferredVectorAction(VT);
}

bool SITargetLowering::shouldConvertConstantLoadToIntImm(const APInt &Imm,
                                                         Type *Ty) const {
  // FIXME: Could be smarter if called for vector constants.
  return true;
}

bool SITargetLowering::isExtractSubvectorCheap(EVT ResVT, EVT SrcVT,
                                               unsigned Index) const {
  if (!isOperationLegalOrCustom(ISD::EXTRACT_SUBVECTOR, ResVT))
    return false;

  // TODO: Add more cases that are cheap.
  return Index == 0;
}

bool SITargetLowering::isTypeDesirableForOp(unsigned Op, EVT VT) const {
  if (Subtarget->has16BitInsts() && VT == MVT::i16) {
    switch (Op) {
    case ISD::LOAD:
    case ISD::STORE:

    // These operations are done with 32-bit instructions anyway.
    case ISD::AND:
    case ISD::OR:
    case ISD::XOR:
    case ISD::SELECT:
      // TODO: Extensions?
      return true;
    default:
      return false;
    }
  }

  // SimplifySetCC uses this function to determine whether or not it should
  // create setcc with i1 operands.  We don't have instructions for i1 setcc.
  if (VT == MVT::i1 && Op == ISD::SETCC)
    return false;

  return TargetLowering::isTypeDesirableForOp(Op, VT);
}

SDValue SITargetLowering::lowerKernArgParameterPtr(SelectionDAG &DAG,
                                                   const SDLoc &SL,
                                                   SDValue Chain,
                                                   uint64_t Offset) const {
  const DataLayout &DL = DAG.getDataLayout();
  MachineFunction &MF = DAG.getMachineFunction();
  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();

  const ArgDescriptor *InputPtrReg;
  const TargetRegisterClass *RC;
  LLT ArgTy;
  MVT PtrVT = getPointerTy(DL, AMDGPUAS::CONSTANT_ADDRESS);

  std::tie(InputPtrReg, RC, ArgTy) =
      Info->getPreloadedValue(AMDGPUFunctionArgInfo::KERNARG_SEGMENT_PTR);

  // We may not have the kernarg segment argument if we have no kernel
  // arguments.
  if (!InputPtrReg)
    return DAG.getConstant(Offset, SL, PtrVT);

  MachineRegisterInfo &MRI = DAG.getMachineFunction().getRegInfo();
  SDValue BasePtr = DAG.getCopyFromReg(Chain, SL,
    MRI.getLiveInVirtReg(InputPtrReg->getRegister()), PtrVT);

  return DAG.getObjectPtrOffset(SL, BasePtr, TypeSize::getFixed(Offset));
}

SDValue SITargetLowering::getImplicitArgPtr(SelectionDAG &DAG,
                                            const SDLoc &SL) const {
  uint64_t Offset = getImplicitParameterOffset(DAG.getMachineFunction(),
                                               FIRST_IMPLICIT);
  return lowerKernArgParameterPtr(DAG, SL, DAG.getEntryNode(), Offset);
}

SDValue SITargetLowering::getLDSKernelId(SelectionDAG &DAG,
                                         const SDLoc &SL) const {

  Function &F = DAG.getMachineFunction().getFunction();
  std::optional<uint32_t> KnownSize =
      AMDGPUMachineFunction::getLDSKernelIdMetadata(F);
  if (KnownSize.has_value())
    return DAG.getConstant(*KnownSize, SL, MVT::i32);
  return SDValue();
}

SDValue SITargetLowering::convertArgType(SelectionDAG &DAG, EVT VT, EVT MemVT,
                                         const SDLoc &SL, SDValue Val,
                                         bool Signed,
                                         const ISD::InputArg *Arg) const {
  // First, if it is a widened vector, narrow it.
  if (VT.isVector() &&
      VT.getVectorNumElements() != MemVT.getVectorNumElements()) {
    EVT NarrowedVT =
        EVT::getVectorVT(*DAG.getContext(), MemVT.getVectorElementType(),
                         VT.getVectorNumElements());
    Val = DAG.getNode(ISD::EXTRACT_SUBVECTOR, SL, NarrowedVT, Val,
                      DAG.getConstant(0, SL, MVT::i32));
  }

  // Then convert the vector elements or scalar value.
  if (Arg && (Arg->Flags.isSExt() || Arg->Flags.isZExt()) &&
      VT.bitsLT(MemVT)) {
    unsigned Opc = Arg->Flags.isZExt() ? ISD::AssertZext : ISD::AssertSext;
    Val = DAG.getNode(Opc, SL, MemVT, Val, DAG.getValueType(VT));
  }

  if (MemVT.isFloatingPoint())
    Val = getFPExtOrFPRound(DAG, Val, SL, VT);
  else if (Signed)
    Val = DAG.getSExtOrTrunc(Val, SL, VT);
  else
    Val = DAG.getZExtOrTrunc(Val, SL, VT);

  return Val;
}

SDValue SITargetLowering::lowerKernargMemParameter(
    SelectionDAG &DAG, EVT VT, EVT MemVT, const SDLoc &SL, SDValue Chain,
    uint64_t Offset, Align Alignment, bool Signed,
    const ISD::InputArg *Arg) const {
  MachinePointerInfo PtrInfo(AMDGPUAS::CONSTANT_ADDRESS);

  // Try to avoid using an extload by loading earlier than the argument address,
  // and extracting the relevant bits. The load should hopefully be merged with
  // the previous argument.
  if (MemVT.getStoreSize() < 4 && Alignment < 4) {
    // TODO: Handle align < 4 and size >= 4 (can happen with packed structs).
    int64_t AlignDownOffset = alignDown(Offset, 4);
    int64_t OffsetDiff = Offset - AlignDownOffset;

    EVT IntVT = MemVT.changeTypeToInteger();

    // TODO: If we passed in the base kernel offset we could have a better
    // alignment than 4, but we don't really need it.
    SDValue Ptr = lowerKernArgParameterPtr(DAG, SL, Chain, AlignDownOffset);
    SDValue Load = DAG.getLoad(MVT::i32, SL, Chain, Ptr, PtrInfo, Align(4),
                               MachineMemOperand::MODereferenceable |
                                   MachineMemOperand::MOInvariant);

    SDValue ShiftAmt = DAG.getConstant(OffsetDiff * 8, SL, MVT::i32);
    SDValue Extract = DAG.getNode(ISD::SRL, SL, MVT::i32, Load, ShiftAmt);

    SDValue ArgVal = DAG.getNode(ISD::TRUNCATE, SL, IntVT, Extract);
    ArgVal = DAG.getNode(ISD::BITCAST, SL, MemVT, ArgVal);
    ArgVal = convertArgType(DAG, VT, MemVT, SL, ArgVal, Signed, Arg);


    return DAG.getMergeValues({ ArgVal, Load.getValue(1) }, SL);
  }

  SDValue Ptr = lowerKernArgParameterPtr(DAG, SL, Chain, Offset);
  SDValue Load = DAG.getLoad(MemVT, SL, Chain, Ptr, PtrInfo, Alignment,
                             MachineMemOperand::MODereferenceable |
                                 MachineMemOperand::MOInvariant);

  SDValue Val = convertArgType(DAG, VT, MemVT, SL, Load, Signed, Arg);
  return DAG.getMergeValues({ Val, Load.getValue(1) }, SL);
}

SDValue SITargetLowering::lowerStackParameter(SelectionDAG &DAG, CCValAssign &VA,
                                              const SDLoc &SL, SDValue Chain,
                                              const ISD::InputArg &Arg) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  if (Arg.Flags.isByVal()) {
    unsigned Size = Arg.Flags.getByValSize();
    int FrameIdx = MFI.CreateFixedObject(Size, VA.getLocMemOffset(), false);
    return DAG.getFrameIndex(FrameIdx, MVT::i32);
  }

  unsigned ArgOffset = VA.getLocMemOffset();
  unsigned ArgSize = VA.getValVT().getStoreSize();

  int FI = MFI.CreateFixedObject(ArgSize, ArgOffset, true);

  // Create load nodes to retrieve arguments from the stack.
  SDValue FIN = DAG.getFrameIndex(FI, MVT::i32);
  SDValue ArgValue;

  // For NON_EXTLOAD, generic code in getLoad assert(ValVT == MemVT)
  ISD::LoadExtType ExtType = ISD::NON_EXTLOAD;
  MVT MemVT = VA.getValVT();

  switch (VA.getLocInfo()) {
  default:
    break;
  case CCValAssign::BCvt:
    MemVT = VA.getLocVT();
    break;
  case CCValAssign::SExt:
    ExtType = ISD::SEXTLOAD;
    break;
  case CCValAssign::ZExt:
    ExtType = ISD::ZEXTLOAD;
    break;
  case CCValAssign::AExt:
    ExtType = ISD::EXTLOAD;
    break;
  }

  ArgValue = DAG.getExtLoad(
    ExtType, SL, VA.getLocVT(), Chain, FIN,
    MachinePointerInfo::getFixedStack(DAG.getMachineFunction(), FI),
    MemVT);
  return ArgValue;
}

SDValue SITargetLowering::getPreloadedValue(SelectionDAG &DAG,
  const SIMachineFunctionInfo &MFI,
  EVT VT,
  AMDGPUFunctionArgInfo::PreloadedValue PVID) const {
  const ArgDescriptor *Reg = nullptr;
  const TargetRegisterClass *RC;
  LLT Ty;

  CallingConv::ID CC = DAG.getMachineFunction().getFunction().getCallingConv();
  const ArgDescriptor WorkGroupIDX =
      ArgDescriptor::createRegister(AMDGPU::TTMP9);
  // If GridZ is not programmed in an entry function then the hardware will set
  // it to all zeros, so there is no need to mask the GridY value in the low
  // order bits.
  const ArgDescriptor WorkGroupIDY = ArgDescriptor::createRegister(
      AMDGPU::TTMP7,
      AMDGPU::isEntryFunctionCC(CC) && !MFI.hasWorkGroupIDZ() ? ~0u : 0xFFFFu);
  const ArgDescriptor WorkGroupIDZ =
      ArgDescriptor::createRegister(AMDGPU::TTMP7, 0xFFFF0000u);
  if (Subtarget->hasArchitectedSGPRs() &&
      (AMDGPU::isCompute(CC) || CC == CallingConv::AMDGPU_Gfx)) {
    switch (PVID) {
    case AMDGPUFunctionArgInfo::WORKGROUP_ID_X:
      Reg = &WorkGroupIDX;
      RC = &AMDGPU::SReg_32RegClass;
      Ty = LLT::scalar(32);
      break;
    case AMDGPUFunctionArgInfo::WORKGROUP_ID_Y:
      Reg = &WorkGroupIDY;
      RC = &AMDGPU::SReg_32RegClass;
      Ty = LLT::scalar(32);
      break;
    case AMDGPUFunctionArgInfo::WORKGROUP_ID_Z:
      Reg = &WorkGroupIDZ;
      RC = &AMDGPU::SReg_32RegClass;
      Ty = LLT::scalar(32);
      break;
    default:
      break;
    }
  }

  if (!Reg)
    std::tie(Reg, RC, Ty) = MFI.getPreloadedValue(PVID);
  if (!Reg) {
    if (PVID == AMDGPUFunctionArgInfo::PreloadedValue::KERNARG_SEGMENT_PTR) {
      // It's possible for a kernarg intrinsic call to appear in a kernel with
      // no allocated segment, in which case we do not add the user sgpr
      // argument, so just return null.
      return DAG.getConstant(0, SDLoc(), VT);
    }

    // It's undefined behavior if a function marked with the amdgpu-no-*
    // attributes uses the corresponding intrinsic.
    return DAG.getUNDEF(VT);
  }

  return loadInputValue(DAG, RC, VT, SDLoc(DAG.getEntryNode()), *Reg);
}

static void processPSInputArgs(SmallVectorImpl<ISD::InputArg> &Splits,
                               CallingConv::ID CallConv,
                               ArrayRef<ISD::InputArg> Ins, BitVector &Skipped,
                               FunctionType *FType,
                               SIMachineFunctionInfo *Info) {
  for (unsigned I = 0, E = Ins.size(), PSInputNum = 0; I != E; ++I) {
    const ISD::InputArg *Arg = &Ins[I];

    assert((!Arg->VT.isVector() || Arg->VT.getScalarSizeInBits() == 16) &&
           "vector type argument should have been split");

    // First check if it's a PS input addr.
    if (CallConv == CallingConv::AMDGPU_PS &&
        !Arg->Flags.isInReg() && PSInputNum <= 15) {
      bool SkipArg = !Arg->Used && !Info->isPSInputAllocated(PSInputNum);

      // Inconveniently only the first part of the split is marked as isSplit,
      // so skip to the end. We only want to increment PSInputNum once for the
      // entire split argument.
      if (Arg->Flags.isSplit()) {
        while (!Arg->Flags.isSplitEnd()) {
          assert((!Arg->VT.isVector() ||
                  Arg->VT.getScalarSizeInBits() == 16) &&
                 "unexpected vector split in ps argument type");
          if (!SkipArg)
            Splits.push_back(*Arg);
          Arg = &Ins[++I];
        }
      }

      if (SkipArg) {
        // We can safely skip PS inputs.
        Skipped.set(Arg->getOrigArgIndex());
        ++PSInputNum;
        continue;
      }

      Info->markPSInputAllocated(PSInputNum);
      if (Arg->Used)
        Info->markPSInputEnabled(PSInputNum);

      ++PSInputNum;
    }

    Splits.push_back(*Arg);
  }
}

// Allocate special inputs passed in VGPRs.
void SITargetLowering::allocateSpecialEntryInputVGPRs(CCState &CCInfo,
                                                      MachineFunction &MF,
                                                      const SIRegisterInfo &TRI,
                                                      SIMachineFunctionInfo &Info) const {
  const LLT S32 = LLT::scalar(32);
  MachineRegisterInfo &MRI = MF.getRegInfo();

  if (Info.hasWorkItemIDX()) {
    Register Reg = AMDGPU::VGPR0;
    MRI.setType(MF.addLiveIn(Reg, &AMDGPU::VGPR_32RegClass), S32);

    CCInfo.AllocateReg(Reg);
    unsigned Mask = (Subtarget->hasPackedTID() &&
                     Info.hasWorkItemIDY()) ? 0x3ff : ~0u;
    Info.setWorkItemIDX(ArgDescriptor::createRegister(Reg, Mask));
  }

  if (Info.hasWorkItemIDY()) {
    assert(Info.hasWorkItemIDX());
    if (Subtarget->hasPackedTID()) {
      Info.setWorkItemIDY(ArgDescriptor::createRegister(AMDGPU::VGPR0,
                                                        0x3ff << 10));
    } else {
      unsigned Reg = AMDGPU::VGPR1;
      MRI.setType(MF.addLiveIn(Reg, &AMDGPU::VGPR_32RegClass), S32);

      CCInfo.AllocateReg(Reg);
      Info.setWorkItemIDY(ArgDescriptor::createRegister(Reg));
    }
  }

  if (Info.hasWorkItemIDZ()) {
    assert(Info.hasWorkItemIDX() && Info.hasWorkItemIDY());
    if (Subtarget->hasPackedTID()) {
      Info.setWorkItemIDZ(ArgDescriptor::createRegister(AMDGPU::VGPR0,
                                                        0x3ff << 20));
    } else {
      unsigned Reg = AMDGPU::VGPR2;
      MRI.setType(MF.addLiveIn(Reg, &AMDGPU::VGPR_32RegClass), S32);

      CCInfo.AllocateReg(Reg);
      Info.setWorkItemIDZ(ArgDescriptor::createRegister(Reg));
    }
  }
}

// Try to allocate a VGPR at the end of the argument list, or if no argument
// VGPRs are left allocating a stack slot.
// If \p Mask is is given it indicates bitfield position in the register.
// If \p Arg is given use it with new ]p Mask instead of allocating new.
static ArgDescriptor allocateVGPR32Input(CCState &CCInfo, unsigned Mask = ~0u,
                                         ArgDescriptor Arg = ArgDescriptor()) {
  if (Arg.isSet())
    return ArgDescriptor::createArg(Arg, Mask);

  ArrayRef<MCPhysReg> ArgVGPRs = ArrayRef(AMDGPU::VGPR_32RegClass.begin(), 32);
  unsigned RegIdx = CCInfo.getFirstUnallocated(ArgVGPRs);
  if (RegIdx == ArgVGPRs.size()) {
    // Spill to stack required.
    int64_t Offset = CCInfo.AllocateStack(4, Align(4));

    return ArgDescriptor::createStack(Offset, Mask);
  }

  unsigned Reg = ArgVGPRs[RegIdx];
  Reg = CCInfo.AllocateReg(Reg);
  assert(Reg != AMDGPU::NoRegister);

  MachineFunction &MF = CCInfo.getMachineFunction();
  Register LiveInVReg = MF.addLiveIn(Reg, &AMDGPU::VGPR_32RegClass);
  MF.getRegInfo().setType(LiveInVReg, LLT::scalar(32));
  return ArgDescriptor::createRegister(Reg, Mask);
}

static ArgDescriptor allocateSGPR32InputImpl(CCState &CCInfo,
                                             const TargetRegisterClass *RC,
                                             unsigned NumArgRegs) {
  ArrayRef<MCPhysReg> ArgSGPRs = ArrayRef(RC->begin(), 32);
  unsigned RegIdx = CCInfo.getFirstUnallocated(ArgSGPRs);
  if (RegIdx == ArgSGPRs.size())
    report_fatal_error("ran out of SGPRs for arguments");

  unsigned Reg = ArgSGPRs[RegIdx];
  Reg = CCInfo.AllocateReg(Reg);
  assert(Reg != AMDGPU::NoRegister);

  MachineFunction &MF = CCInfo.getMachineFunction();
  MF.addLiveIn(Reg, RC);
  return ArgDescriptor::createRegister(Reg);
}

// If this has a fixed position, we still should allocate the register in the
// CCInfo state. Technically we could get away with this for values passed
// outside of the normal argument range.
static void allocateFixedSGPRInputImpl(CCState &CCInfo,
                                       const TargetRegisterClass *RC,
                                       MCRegister Reg) {
  Reg = CCInfo.AllocateReg(Reg);
  assert(Reg != AMDGPU::NoRegister);
  MachineFunction &MF = CCInfo.getMachineFunction();
  MF.addLiveIn(Reg, RC);
}

static void allocateSGPR32Input(CCState &CCInfo, ArgDescriptor &Arg) {
  if (Arg) {
    allocateFixedSGPRInputImpl(CCInfo, &AMDGPU::SGPR_32RegClass,
                               Arg.getRegister());
  } else
    Arg = allocateSGPR32InputImpl(CCInfo, &AMDGPU::SGPR_32RegClass, 32);
}

static void allocateSGPR64Input(CCState &CCInfo, ArgDescriptor &Arg) {
  if (Arg) {
    allocateFixedSGPRInputImpl(CCInfo, &AMDGPU::SGPR_64RegClass,
                               Arg.getRegister());
  } else
    Arg = allocateSGPR32InputImpl(CCInfo, &AMDGPU::SGPR_64RegClass, 16);
}

/// Allocate implicit function VGPR arguments at the end of allocated user
/// arguments.
void SITargetLowering::allocateSpecialInputVGPRs(
  CCState &CCInfo, MachineFunction &MF,
  const SIRegisterInfo &TRI, SIMachineFunctionInfo &Info) const {
  const unsigned Mask = 0x3ff;
  ArgDescriptor Arg;

  if (Info.hasWorkItemIDX()) {
    Arg = allocateVGPR32Input(CCInfo, Mask);
    Info.setWorkItemIDX(Arg);
  }

  if (Info.hasWorkItemIDY()) {
    Arg = allocateVGPR32Input(CCInfo, Mask << 10, Arg);
    Info.setWorkItemIDY(Arg);
  }

  if (Info.hasWorkItemIDZ())
    Info.setWorkItemIDZ(allocateVGPR32Input(CCInfo, Mask << 20, Arg));
}

/// Allocate implicit function VGPR arguments in fixed registers.
void SITargetLowering::allocateSpecialInputVGPRsFixed(
  CCState &CCInfo, MachineFunction &MF,
  const SIRegisterInfo &TRI, SIMachineFunctionInfo &Info) const {
  Register Reg = CCInfo.AllocateReg(AMDGPU::VGPR31);
  if (!Reg)
    report_fatal_error("failed to allocated VGPR for implicit arguments");

  const unsigned Mask = 0x3ff;
  Info.setWorkItemIDX(ArgDescriptor::createRegister(Reg, Mask));
  Info.setWorkItemIDY(ArgDescriptor::createRegister(Reg, Mask << 10));
  Info.setWorkItemIDZ(ArgDescriptor::createRegister(Reg, Mask << 20));
}

void SITargetLowering::allocateSpecialInputSGPRs(
  CCState &CCInfo,
  MachineFunction &MF,
  const SIRegisterInfo &TRI,
  SIMachineFunctionInfo &Info) const {
  auto &ArgInfo = Info.getArgInfo();
  const GCNUserSGPRUsageInfo &UserSGPRInfo = Info.getUserSGPRInfo();

  // TODO: Unify handling with private memory pointers.
  if (UserSGPRInfo.hasDispatchPtr())
    allocateSGPR64Input(CCInfo, ArgInfo.DispatchPtr);

  const Module *M = MF.getFunction().getParent();
  if (UserSGPRInfo.hasQueuePtr() &&
      AMDGPU::getAMDHSACodeObjectVersion(*M) < AMDGPU::AMDHSA_COV5)
    allocateSGPR64Input(CCInfo, ArgInfo.QueuePtr);

  // Implicit arg ptr takes the place of the kernarg segment pointer. This is a
  // constant offset from the kernarg segment.
  if (Info.hasImplicitArgPtr())
    allocateSGPR64Input(CCInfo, ArgInfo.ImplicitArgPtr);

  if (UserSGPRInfo.hasDispatchID())
    allocateSGPR64Input(CCInfo, ArgInfo.DispatchID);

  // flat_scratch_init is not applicable for non-kernel functions.

  if (Info.hasWorkGroupIDX())
    allocateSGPR32Input(CCInfo, ArgInfo.WorkGroupIDX);

  if (Info.hasWorkGroupIDY())
    allocateSGPR32Input(CCInfo, ArgInfo.WorkGroupIDY);

  if (Info.hasWorkGroupIDZ())
    allocateSGPR32Input(CCInfo, ArgInfo.WorkGroupIDZ);

  if (Info.hasLDSKernelId())
    allocateSGPR32Input(CCInfo, ArgInfo.LDSKernelId);
}

// Allocate special inputs passed in user SGPRs.
void SITargetLowering::allocateHSAUserSGPRs(CCState &CCInfo,
                                            MachineFunction &MF,
                                            const SIRegisterInfo &TRI,
                                            SIMachineFunctionInfo &Info) const {
  const GCNUserSGPRUsageInfo &UserSGPRInfo = Info.getUserSGPRInfo();
  if (UserSGPRInfo.hasImplicitBufferPtr()) {
    Register ImplicitBufferPtrReg = Info.addImplicitBufferPtr(TRI);
    MF.addLiveIn(ImplicitBufferPtrReg, &AMDGPU::SGPR_64RegClass);
    CCInfo.AllocateReg(ImplicitBufferPtrReg);
  }

  // FIXME: How should these inputs interact with inreg / custom SGPR inputs?
  if (UserSGPRInfo.hasPrivateSegmentBuffer()) {
    Register PrivateSegmentBufferReg = Info.addPrivateSegmentBuffer(TRI);
    MF.addLiveIn(PrivateSegmentBufferReg, &AMDGPU::SGPR_128RegClass);
    CCInfo.AllocateReg(PrivateSegmentBufferReg);
  }

  if (UserSGPRInfo.hasDispatchPtr()) {
    Register DispatchPtrReg = Info.addDispatchPtr(TRI);
    MF.addLiveIn(DispatchPtrReg, &AMDGPU::SGPR_64RegClass);
    CCInfo.AllocateReg(DispatchPtrReg);
  }

  const Module *M = MF.getFunction().getParent();
  if (UserSGPRInfo.hasQueuePtr() &&
      AMDGPU::getAMDHSACodeObjectVersion(*M) < AMDGPU::AMDHSA_COV5) {
    Register QueuePtrReg = Info.addQueuePtr(TRI);
    MF.addLiveIn(QueuePtrReg, &AMDGPU::SGPR_64RegClass);
    CCInfo.AllocateReg(QueuePtrReg);
  }

  if (UserSGPRInfo.hasKernargSegmentPtr()) {
    MachineRegisterInfo &MRI = MF.getRegInfo();
    Register InputPtrReg = Info.addKernargSegmentPtr(TRI);
    CCInfo.AllocateReg(InputPtrReg);

    Register VReg = MF.addLiveIn(InputPtrReg, &AMDGPU::SGPR_64RegClass);
    MRI.setType(VReg, LLT::pointer(AMDGPUAS::CONSTANT_ADDRESS, 64));
  }

  if (UserSGPRInfo.hasDispatchID()) {
    Register DispatchIDReg = Info.addDispatchID(TRI);
    MF.addLiveIn(DispatchIDReg, &AMDGPU::SGPR_64RegClass);
    CCInfo.AllocateReg(DispatchIDReg);
  }

  if (UserSGPRInfo.hasFlatScratchInit() && !getSubtarget()->isAmdPalOS()) {
    Register FlatScratchInitReg = Info.addFlatScratchInit(TRI);
    MF.addLiveIn(FlatScratchInitReg, &AMDGPU::SGPR_64RegClass);
    CCInfo.AllocateReg(FlatScratchInitReg);
  }

  if (UserSGPRInfo.hasPrivateSegmentSize()) {
    Register PrivateSegmentSizeReg = Info.addPrivateSegmentSize(TRI);
    MF.addLiveIn(PrivateSegmentSizeReg, &AMDGPU::SGPR_32RegClass);
    CCInfo.AllocateReg(PrivateSegmentSizeReg);
  }

  // TODO: Add GridWorkGroupCount user SGPRs when used. For now with HSA we read
  // these from the dispatch pointer.
}

// Allocate pre-loaded kernel arguemtns. Arguments to be preloading must be
// sequential starting from the first argument.
void SITargetLowering::allocatePreloadKernArgSGPRs(
    CCState &CCInfo, SmallVectorImpl<CCValAssign> &ArgLocs,
    const SmallVectorImpl<ISD::InputArg> &Ins, MachineFunction &MF,
    const SIRegisterInfo &TRI, SIMachineFunctionInfo &Info) const {
  Function &F = MF.getFunction();
  unsigned LastExplicitArgOffset =
      MF.getSubtarget<GCNSubtarget>().getExplicitKernelArgOffset();
  GCNUserSGPRUsageInfo &SGPRInfo = Info.getUserSGPRInfo();
  bool InPreloadSequence = true;
  unsigned InIdx = 0;
  for (auto &Arg : F.args()) {
    if (!InPreloadSequence || !Arg.hasInRegAttr())
      break;

    int ArgIdx = Arg.getArgNo();
    // Don't preload non-original args or parts not in the current preload
    // sequence.
    if (InIdx < Ins.size() && (!Ins[InIdx].isOrigArg() ||
                               (int)Ins[InIdx].getOrigArgIndex() != ArgIdx))
      break;

    for (; InIdx < Ins.size() && Ins[InIdx].isOrigArg() &&
           (int)Ins[InIdx].getOrigArgIndex() == ArgIdx;
         InIdx++) {
      assert(ArgLocs[ArgIdx].isMemLoc());
      auto &ArgLoc = ArgLocs[InIdx];
      const Align KernelArgBaseAlign = Align(16);
      unsigned ArgOffset = ArgLoc.getLocMemOffset();
      Align Alignment = commonAlignment(KernelArgBaseAlign, ArgOffset);
      unsigned NumAllocSGPRs =
          alignTo(ArgLoc.getLocVT().getFixedSizeInBits(), 32) / 32;

      // Arg is preloaded into the previous SGPR.
      if (ArgLoc.getLocVT().getStoreSize() < 4 && Alignment < 4) {
        Info.getArgInfo().PreloadKernArgs[InIdx].Regs.push_back(
            Info.getArgInfo().PreloadKernArgs[InIdx - 1].Regs[0]);
        continue;
      }

      unsigned Padding = ArgOffset - LastExplicitArgOffset;
      unsigned PaddingSGPRs = alignTo(Padding, 4) / 4;
      // Check for free user SGPRs for preloading.
      if (PaddingSGPRs + NumAllocSGPRs + 1 /*Synthetic SGPRs*/ >
          SGPRInfo.getNumFreeUserSGPRs()) {
        InPreloadSequence = false;
        break;
      }

      // Preload this argument.
      const TargetRegisterClass *RC =
          TRI.getSGPRClassForBitWidth(NumAllocSGPRs * 32);
      SmallVectorImpl<MCRegister> *PreloadRegs =
          Info.addPreloadedKernArg(TRI, RC, NumAllocSGPRs, InIdx, PaddingSGPRs);

      if (PreloadRegs->size() > 1)
        RC = &AMDGPU::SGPR_32RegClass;
      for (auto &Reg : *PreloadRegs) {
        assert(Reg);
        MF.addLiveIn(Reg, RC);
        CCInfo.AllocateReg(Reg);
      }

      LastExplicitArgOffset = NumAllocSGPRs * 4 + ArgOffset;
    }
  }
}

void SITargetLowering::allocateLDSKernelId(CCState &CCInfo, MachineFunction &MF,
                                           const SIRegisterInfo &TRI,
                                           SIMachineFunctionInfo &Info) const {
  // Always allocate this last since it is a synthetic preload.
  if (Info.hasLDSKernelId()) {
    Register Reg = Info.addLDSKernelId();
    MF.addLiveIn(Reg, &AMDGPU::SGPR_32RegClass);
    CCInfo.AllocateReg(Reg);
  }
}

// Allocate special input registers that are initialized per-wave.
void SITargetLowering::allocateSystemSGPRs(CCState &CCInfo,
                                           MachineFunction &MF,
                                           SIMachineFunctionInfo &Info,
                                           CallingConv::ID CallConv,
                                           bool IsShader) const {
  bool HasArchitectedSGPRs = Subtarget->hasArchitectedSGPRs();
  if (Subtarget->hasUserSGPRInit16Bug() && !IsShader) {
    // Note: user SGPRs are handled by the front-end for graphics shaders
    // Pad up the used user SGPRs with dead inputs.

    // TODO: NumRequiredSystemSGPRs computation should be adjusted appropriately
    // before enabling architected SGPRs for workgroup IDs.
    assert(!HasArchitectedSGPRs && "Unhandled feature for the subtarget");

    unsigned CurrentUserSGPRs = Info.getNumUserSGPRs();
    // Note we do not count the PrivateSegmentWaveByteOffset. We do not want to
    // rely on it to reach 16 since if we end up having no stack usage, it will
    // not really be added.
    unsigned NumRequiredSystemSGPRs = Info.hasWorkGroupIDX() +
                                      Info.hasWorkGroupIDY() +
                                      Info.hasWorkGroupIDZ() +
                                      Info.hasWorkGroupInfo();
    for (unsigned i = NumRequiredSystemSGPRs + CurrentUserSGPRs; i < 16; ++i) {
      Register Reg = Info.addReservedUserSGPR();
      MF.addLiveIn(Reg, &AMDGPU::SGPR_32RegClass);
      CCInfo.AllocateReg(Reg);
    }
  }

  if (!HasArchitectedSGPRs) {
    if (Info.hasWorkGroupIDX()) {
      Register Reg = Info.addWorkGroupIDX();
      MF.addLiveIn(Reg, &AMDGPU::SGPR_32RegClass);
      CCInfo.AllocateReg(Reg);
    }

    if (Info.hasWorkGroupIDY()) {
      Register Reg = Info.addWorkGroupIDY();
      MF.addLiveIn(Reg, &AMDGPU::SGPR_32RegClass);
      CCInfo.AllocateReg(Reg);
    }

    if (Info.hasWorkGroupIDZ()) {
      Register Reg = Info.addWorkGroupIDZ();
      MF.addLiveIn(Reg, &AMDGPU::SGPR_32RegClass);
      CCInfo.AllocateReg(Reg);
    }
  }

  if (Info.hasWorkGroupInfo()) {
    Register Reg = Info.addWorkGroupInfo();
    MF.addLiveIn(Reg, &AMDGPU::SGPR_32RegClass);
    CCInfo.AllocateReg(Reg);
  }

  if (Info.hasPrivateSegmentWaveByteOffset()) {
    // Scratch wave offset passed in system SGPR.
    unsigned PrivateSegmentWaveByteOffsetReg;

    if (IsShader) {
      PrivateSegmentWaveByteOffsetReg =
        Info.getPrivateSegmentWaveByteOffsetSystemSGPR();

      // This is true if the scratch wave byte offset doesn't have a fixed
      // location.
      if (PrivateSegmentWaveByteOffsetReg == AMDGPU::NoRegister) {
        PrivateSegmentWaveByteOffsetReg = findFirstFreeSGPR(CCInfo);
        Info.setPrivateSegmentWaveByteOffset(PrivateSegmentWaveByteOffsetReg);
      }
    } else
      PrivateSegmentWaveByteOffsetReg = Info.addPrivateSegmentWaveByteOffset();

    MF.addLiveIn(PrivateSegmentWaveByteOffsetReg, &AMDGPU::SGPR_32RegClass);
    CCInfo.AllocateReg(PrivateSegmentWaveByteOffsetReg);
  }

  assert(!Subtarget->hasUserSGPRInit16Bug() || IsShader ||
         Info.getNumPreloadedSGPRs() >= 16);
}

static void reservePrivateMemoryRegs(const TargetMachine &TM,
                                     MachineFunction &MF,
                                     const SIRegisterInfo &TRI,
                                     SIMachineFunctionInfo &Info) {
  // Now that we've figured out where the scratch register inputs are, see if
  // should reserve the arguments and use them directly.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  bool HasStackObjects = MFI.hasStackObjects();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();

  // Record that we know we have non-spill stack objects so we don't need to
  // check all stack objects later.
  if (HasStackObjects)
    Info.setHasNonSpillStackObjects(true);

  // Everything live out of a block is spilled with fast regalloc, so it's
  // almost certain that spilling will be required.
  if (TM.getOptLevel() == CodeGenOptLevel::None)
    HasStackObjects = true;

  // For now assume stack access is needed in any callee functions, so we need
  // the scratch registers to pass in.
  bool RequiresStackAccess = HasStackObjects || MFI.hasCalls();

  if (!ST.enableFlatScratch()) {
    if (RequiresStackAccess && ST.isAmdHsaOrMesa(MF.getFunction())) {
      // If we have stack objects, we unquestionably need the private buffer
      // resource. For the Code Object V2 ABI, this will be the first 4 user
      // SGPR inputs. We can reserve those and use them directly.

      Register PrivateSegmentBufferReg =
          Info.getPreloadedReg(AMDGPUFunctionArgInfo::PRIVATE_SEGMENT_BUFFER);
      Info.setScratchRSrcReg(PrivateSegmentBufferReg);
    } else {
      unsigned ReservedBufferReg = TRI.reservedPrivateSegmentBufferReg(MF);
      // We tentatively reserve the last registers (skipping the last registers
      // which may contain VCC, FLAT_SCR, and XNACK). After register allocation,
      // we'll replace these with the ones immediately after those which were
      // really allocated. In the prologue copies will be inserted from the
      // argument to these reserved registers.

      // Without HSA, relocations are used for the scratch pointer and the
      // buffer resource setup is always inserted in the prologue. Scratch wave
      // offset is still in an input SGPR.
      Info.setScratchRSrcReg(ReservedBufferReg);
    }
  }

  MachineRegisterInfo &MRI = MF.getRegInfo();

  // For entry functions we have to set up the stack pointer if we use it,
  // whereas non-entry functions get this "for free". This means there is no
  // intrinsic advantage to using S32 over S34 in cases where we do not have
  // calls but do need a frame pointer (i.e. if we are requested to have one
  // because frame pointer elimination is disabled). To keep things simple we
  // only ever use S32 as the call ABI stack pointer, and so using it does not
  // imply we need a separate frame pointer.
  //
  // Try to use s32 as the SP, but move it if it would interfere with input
  // arguments. This won't work with calls though.
  //
  // FIXME: Move SP to avoid any possible inputs, or find a way to spill input
  // registers.
  if (!MRI.isLiveIn(AMDGPU::SGPR32)) {
    Info.setStackPtrOffsetReg(AMDGPU::SGPR32);
  } else {
    assert(AMDGPU::isShader(MF.getFunction().getCallingConv()));

    if (MFI.hasCalls())
      report_fatal_error("call in graphics shader with too many input SGPRs");

    for (unsigned Reg : AMDGPU::SGPR_32RegClass) {
      if (!MRI.isLiveIn(Reg)) {
        Info.setStackPtrOffsetReg(Reg);
        break;
      }
    }

    if (Info.getStackPtrOffsetReg() == AMDGPU::SP_REG)
      report_fatal_error("failed to find register for SP");
  }

  // hasFP should be accurate for entry functions even before the frame is
  // finalized, because it does not rely on the known stack size, only
  // properties like whether variable sized objects are present.
  if (ST.getFrameLowering()->hasFP(MF)) {
    Info.setFrameOffsetReg(AMDGPU::SGPR33);
  }
}

bool SITargetLowering::supportSplitCSR(MachineFunction *MF) const {
  const SIMachineFunctionInfo *Info = MF->getInfo<SIMachineFunctionInfo>();
  return !Info->isEntryFunction();
}

void SITargetLowering::initializeSplitCSR(MachineBasicBlock *Entry) const {

}

void SITargetLowering::insertCopiesSplitCSR(
  MachineBasicBlock *Entry,
  const SmallVectorImpl<MachineBasicBlock *> &Exits) const {
  const SIRegisterInfo *TRI = getSubtarget()->getRegisterInfo();

  const MCPhysReg *IStart = TRI->getCalleeSavedRegsViaCopy(Entry->getParent());
  if (!IStart)
    return;

  const TargetInstrInfo *TII = Subtarget->getInstrInfo();
  MachineRegisterInfo *MRI = &Entry->getParent()->getRegInfo();
  MachineBasicBlock::iterator MBBI = Entry->begin();
  for (const MCPhysReg *I = IStart; *I; ++I) {
    const TargetRegisterClass *RC = nullptr;
    if (AMDGPU::SReg_64RegClass.contains(*I))
      RC = &AMDGPU::SGPR_64RegClass;
    else if (AMDGPU::SReg_32RegClass.contains(*I))
      RC = &AMDGPU::SGPR_32RegClass;
    else
      llvm_unreachable("Unexpected register class in CSRsViaCopy!");

    Register NewVR = MRI->createVirtualRegister(RC);
    // Create copy from CSR to a virtual register.
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

SDValue SITargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool isVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  const SIRegisterInfo *TRI = getSubtarget()->getRegisterInfo();

  MachineFunction &MF = DAG.getMachineFunction();
  const Function &Fn = MF.getFunction();
  FunctionType *FType = MF.getFunction().getFunctionType();
  SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();

  if (Subtarget->isAmdHsaOS() && AMDGPU::isGraphics(CallConv)) {
    DiagnosticInfoUnsupported NoGraphicsHSA(
        Fn, "unsupported non-compute shaders with HSA", DL.getDebugLoc());
    DAG.getContext()->diagnose(NoGraphicsHSA);
    return DAG.getEntryNode();
  }

  SmallVector<ISD::InputArg, 16> Splits;
  SmallVector<CCValAssign, 16> ArgLocs;
  BitVector Skipped(Ins.size());
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());

  bool IsGraphics = AMDGPU::isGraphics(CallConv);
  bool IsKernel = AMDGPU::isKernel(CallConv);
  bool IsEntryFunc = AMDGPU::isEntryFunctionCC(CallConv);

  if (IsGraphics) {
    const GCNUserSGPRUsageInfo &UserSGPRInfo = Info->getUserSGPRInfo();
    assert(!UserSGPRInfo.hasDispatchPtr() &&
           !UserSGPRInfo.hasKernargSegmentPtr() && !Info->hasWorkGroupInfo() &&
           !Info->hasLDSKernelId() && !Info->hasWorkItemIDX() &&
           !Info->hasWorkItemIDY() && !Info->hasWorkItemIDZ());
    (void)UserSGPRInfo;
    if (!Subtarget->enableFlatScratch())
      assert(!UserSGPRInfo.hasFlatScratchInit());
    if ((CallConv != CallingConv::AMDGPU_CS &&
         CallConv != CallingConv::AMDGPU_Gfx) ||
        !Subtarget->hasArchitectedSGPRs())
      assert(!Info->hasWorkGroupIDX() && !Info->hasWorkGroupIDY() &&
             !Info->hasWorkGroupIDZ());
  }

  if (CallConv == CallingConv::AMDGPU_PS) {
    processPSInputArgs(Splits, CallConv, Ins, Skipped, FType, Info);

    // At least one interpolation mode must be enabled or else the GPU will
    // hang.
    //
    // Check PSInputAddr instead of PSInputEnable. The idea is that if the user
    // set PSInputAddr, the user wants to enable some bits after the compilation
    // based on run-time states. Since we can't know what the final PSInputEna
    // will look like, so we shouldn't do anything here and the user should take
    // responsibility for the correct programming.
    //
    // Otherwise, the following restrictions apply:
    // - At least one of PERSP_* (0xF) or LINEAR_* (0x70) must be enabled.
    // - If POS_W_FLOAT (11) is enabled, at least one of PERSP_* must be
    //   enabled too.
    if ((Info->getPSInputAddr() & 0x7F) == 0 ||
        ((Info->getPSInputAddr() & 0xF) == 0 && Info->isPSInputAllocated(11))) {
      CCInfo.AllocateReg(AMDGPU::VGPR0);
      CCInfo.AllocateReg(AMDGPU::VGPR1);
      Info->markPSInputAllocated(0);
      Info->markPSInputEnabled(0);
    }
    if (Subtarget->isAmdPalOS()) {
      // For isAmdPalOS, the user does not enable some bits after compilation
      // based on run-time states; the register values being generated here are
      // the final ones set in hardware. Therefore we need to apply the
      // workaround to PSInputAddr and PSInputEnable together.  (The case where
      // a bit is set in PSInputAddr but not PSInputEnable is where the
      // frontend set up an input arg for a particular interpolation mode, but
      // nothing uses that input arg. Really we should have an earlier pass
      // that removes such an arg.)
      unsigned PsInputBits = Info->getPSInputAddr() & Info->getPSInputEnable();
      if ((PsInputBits & 0x7F) == 0 ||
          ((PsInputBits & 0xF) == 0 && (PsInputBits >> 11 & 1)))
        Info->markPSInputEnabled(llvm::countr_zero(Info->getPSInputAddr()));
    }
  } else if (IsKernel) {
    assert(Info->hasWorkGroupIDX() && Info->hasWorkItemIDX());
  } else {
    Splits.append(Ins.begin(), Ins.end());
  }

  if (IsKernel)
    analyzeFormalArgumentsCompute(CCInfo, Ins);

  if (IsEntryFunc) {
    allocateSpecialEntryInputVGPRs(CCInfo, MF, *TRI, *Info);
    allocateHSAUserSGPRs(CCInfo, MF, *TRI, *Info);
    if (IsKernel && Subtarget->hasKernargPreload())
      allocatePreloadKernArgSGPRs(CCInfo, ArgLocs, Ins, MF, *TRI, *Info);

    allocateLDSKernelId(CCInfo, MF, *TRI, *Info);
  } else if (!IsGraphics) {
    // For the fixed ABI, pass workitem IDs in the last argument register.
    allocateSpecialInputVGPRsFixed(CCInfo, MF, *TRI, *Info);

    // FIXME: Sink this into allocateSpecialInputSGPRs
    if (!Subtarget->enableFlatScratch())
      CCInfo.AllocateReg(Info->getScratchRSrcReg());

    allocateSpecialInputSGPRs(CCInfo, MF, *TRI, *Info);
  }

  if (!IsKernel) {
    CCAssignFn *AssignFn = CCAssignFnForCall(CallConv, isVarArg);
    CCInfo.AnalyzeFormalArguments(Splits, AssignFn);
  }

  SmallVector<SDValue, 16> Chains;

  // FIXME: This is the minimum kernel argument alignment. We should improve
  // this to the maximum alignment of the arguments.
  //
  // FIXME: Alignment of explicit arguments totally broken with non-0 explicit
  // kern arg offset.
  const Align KernelArgBaseAlign = Align(16);

  for (unsigned i = 0, e = Ins.size(), ArgIdx = 0; i != e; ++i) {
    const ISD::InputArg &Arg = Ins[i];
    if (Arg.isOrigArg() && Skipped[Arg.getOrigArgIndex()]) {
      InVals.push_back(DAG.getUNDEF(Arg.VT));
      continue;
    }

    CCValAssign &VA = ArgLocs[ArgIdx++];
    MVT VT = VA.getLocVT();

    if (IsEntryFunc && VA.isMemLoc()) {
      VT = Ins[i].VT;
      EVT MemVT = VA.getLocVT();

      const uint64_t Offset = VA.getLocMemOffset();
      Align Alignment = commonAlignment(KernelArgBaseAlign, Offset);

      if (Arg.Flags.isByRef()) {
        SDValue Ptr = lowerKernArgParameterPtr(DAG, DL, Chain, Offset);

        const GCNTargetMachine &TM =
            static_cast<const GCNTargetMachine &>(getTargetMachine());
        if (!TM.isNoopAddrSpaceCast(AMDGPUAS::CONSTANT_ADDRESS,
                                    Arg.Flags.getPointerAddrSpace())) {
          Ptr = DAG.getAddrSpaceCast(DL, VT, Ptr, AMDGPUAS::CONSTANT_ADDRESS,
                                     Arg.Flags.getPointerAddrSpace());
        }

        InVals.push_back(Ptr);
        continue;
      }

      SDValue NewArg;
      if (Arg.isOrigArg() && Info->getArgInfo().PreloadKernArgs.count(i)) {
        if (MemVT.getStoreSize() < 4 && Alignment < 4) {
          // In this case the argument is packed into the previous preload SGPR.
          int64_t AlignDownOffset = alignDown(Offset, 4);
          int64_t OffsetDiff = Offset - AlignDownOffset;
          EVT IntVT = MemVT.changeTypeToInteger();

          const SIMachineFunctionInfo *Info =
              MF.getInfo<SIMachineFunctionInfo>();
          MachineRegisterInfo &MRI = DAG.getMachineFunction().getRegInfo();
          Register Reg =
              Info->getArgInfo().PreloadKernArgs.find(i)->getSecond().Regs[0];

          assert(Reg);
          Register VReg = MRI.getLiveInVirtReg(Reg);
          SDValue Copy = DAG.getCopyFromReg(Chain, DL, VReg, MVT::i32);

          SDValue ShiftAmt = DAG.getConstant(OffsetDiff * 8, DL, MVT::i32);
          SDValue Extract = DAG.getNode(ISD::SRL, DL, MVT::i32, Copy, ShiftAmt);

          SDValue ArgVal = DAG.getNode(ISD::TRUNCATE, DL, IntVT, Extract);
          ArgVal = DAG.getNode(ISD::BITCAST, DL, MemVT, ArgVal);
          NewArg = convertArgType(DAG, VT, MemVT, DL, ArgVal,
                                  Ins[i].Flags.isSExt(), &Ins[i]);

          NewArg = DAG.getMergeValues({NewArg, Copy.getValue(1)}, DL);
        } else {
          const SIMachineFunctionInfo *Info =
              MF.getInfo<SIMachineFunctionInfo>();
          MachineRegisterInfo &MRI = DAG.getMachineFunction().getRegInfo();
          const SmallVectorImpl<MCRegister> &PreloadRegs =
              Info->getArgInfo().PreloadKernArgs.find(i)->getSecond().Regs;

          SDValue Copy;
          if (PreloadRegs.size() == 1) {
            Register VReg = MRI.getLiveInVirtReg(PreloadRegs[0]);
            const TargetRegisterClass *RC = MRI.getRegClass(VReg);
            NewArg = DAG.getCopyFromReg(
                Chain, DL, VReg,
                EVT::getIntegerVT(*DAG.getContext(),
                                  TRI->getRegSizeInBits(*RC)));

          } else {
            // If the kernarg alignment does not match the alignment of the SGPR
            // tuple RC that can accommodate this argument, it will be built up
            // via copies from from the individual SGPRs that the argument was
            // preloaded to.
            SmallVector<SDValue, 4> Elts;
            for (auto Reg : PreloadRegs) {
              Register VReg = MRI.getLiveInVirtReg(Reg);
              Copy = DAG.getCopyFromReg(Chain, DL, VReg, MVT::i32);
              Elts.push_back(Copy);
            }
            NewArg =
                DAG.getBuildVector(EVT::getVectorVT(*DAG.getContext(), MVT::i32,
                                                    PreloadRegs.size()),
                                   DL, Elts);
          }

          // If the argument was preloaded to multiple consecutive 32-bit
          // registers because of misalignment between addressable SGPR tuples
          // and the argument size, we can still assume that because of kernarg
          // segment alignment restrictions that NewArg's size is the same as
          // MemVT and just do a bitcast. If MemVT is less than 32-bits we add a
          // truncate since we cannot preload to less than a single SGPR and the
          // MemVT may be smaller.
          EVT MemVTInt =
              EVT::getIntegerVT(*DAG.getContext(), MemVT.getSizeInBits());
          if (MemVT.bitsLT(NewArg.getSimpleValueType()))
            NewArg = DAG.getNode(ISD::TRUNCATE, DL, MemVTInt, NewArg);

          NewArg = DAG.getBitcast(MemVT, NewArg);
          NewArg = convertArgType(DAG, VT, MemVT, DL, NewArg,
                                  Ins[i].Flags.isSExt(), &Ins[i]);
          NewArg = DAG.getMergeValues({NewArg, Chain}, DL);
        }
      } else {
        NewArg =
            lowerKernargMemParameter(DAG, VT, MemVT, DL, Chain, Offset,
                                     Alignment, Ins[i].Flags.isSExt(), &Ins[i]);
      }
      Chains.push_back(NewArg.getValue(1));

      auto *ParamTy =
        dyn_cast<PointerType>(FType->getParamType(Ins[i].getOrigArgIndex()));
      if (Subtarget->getGeneration() == AMDGPUSubtarget::SOUTHERN_ISLANDS &&
          ParamTy && (ParamTy->getAddressSpace() == AMDGPUAS::LOCAL_ADDRESS ||
                      ParamTy->getAddressSpace() == AMDGPUAS::REGION_ADDRESS)) {
        // On SI local pointers are just offsets into LDS, so they are always
        // less than 16-bits.  On CI and newer they could potentially be
        // real pointers, so we can't guarantee their size.
        NewArg = DAG.getNode(ISD::AssertZext, DL, NewArg.getValueType(), NewArg,
                             DAG.getValueType(MVT::i16));
      }

      InVals.push_back(NewArg);
      continue;
    }
    if (!IsEntryFunc && VA.isMemLoc()) {
      SDValue Val = lowerStackParameter(DAG, VA, DL, Chain, Arg);
      InVals.push_back(Val);
      if (!Arg.Flags.isByVal())
        Chains.push_back(Val.getValue(1));
      continue;
    }

    assert(VA.isRegLoc() && "Parameter must be in a register!");

    Register Reg = VA.getLocReg();
    const TargetRegisterClass *RC = nullptr;
    if (AMDGPU::VGPR_32RegClass.contains(Reg))
      RC = &AMDGPU::VGPR_32RegClass;
    else if (AMDGPU::SGPR_32RegClass.contains(Reg))
      RC = &AMDGPU::SGPR_32RegClass;
    else
      llvm_unreachable("Unexpected register class in LowerFormalArguments!");
    EVT ValVT = VA.getValVT();

    Reg = MF.addLiveIn(Reg, RC);
    SDValue Val = DAG.getCopyFromReg(Chain, DL, Reg, VT);

    if (Arg.Flags.isSRet()) {
      // The return object should be reasonably addressable.

      // FIXME: This helps when the return is a real sret. If it is a
      // automatically inserted sret (i.e. CanLowerReturn returns false), an
      // extra copy is inserted in SelectionDAGBuilder which obscures this.
      unsigned NumBits
        = 32 - getSubtarget()->getKnownHighZeroBitsForFrameIndex();
      Val = DAG.getNode(ISD::AssertZext, DL, VT, Val,
        DAG.getValueType(EVT::getIntegerVT(*DAG.getContext(), NumBits)));
    }

    // If this is an 8 or 16-bit value, it is really passed promoted
    // to 32 bits. Insert an assert[sz]ext to capture this, then
    // truncate to the right size.
    switch (VA.getLocInfo()) {
    case CCValAssign::Full:
      break;
    case CCValAssign::BCvt:
      Val = DAG.getNode(ISD::BITCAST, DL, ValVT, Val);
      break;
    case CCValAssign::SExt:
      Val = DAG.getNode(ISD::AssertSext, DL, VT, Val,
                        DAG.getValueType(ValVT));
      Val = DAG.getNode(ISD::TRUNCATE, DL, ValVT, Val);
      break;
    case CCValAssign::ZExt:
      Val = DAG.getNode(ISD::AssertZext, DL, VT, Val,
                        DAG.getValueType(ValVT));
      Val = DAG.getNode(ISD::TRUNCATE, DL, ValVT, Val);
      break;
    case CCValAssign::AExt:
      Val = DAG.getNode(ISD::TRUNCATE, DL, ValVT, Val);
      break;
    default:
      llvm_unreachable("Unknown loc info!");
    }

    InVals.push_back(Val);
  }

  // Start adding system SGPRs.
  if (IsEntryFunc)
    allocateSystemSGPRs(CCInfo, MF, *Info, CallConv, IsGraphics);

  // DAG.getPass() returns nullptr when using new pass manager.
  // TODO: Use DAG.getMFAM() to access analysis result.
  if (DAG.getPass()) {
    auto &ArgUsageInfo = DAG.getPass()->getAnalysis<AMDGPUArgumentUsageInfo>();
    ArgUsageInfo.setFuncArgInfo(Fn, Info->getArgInfo());
  }

  unsigned StackArgSize = CCInfo.getStackSize();
  Info->setBytesInStackArgArea(StackArgSize);

  return Chains.empty() ? Chain :
    DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Chains);
}

// TODO: If return values can't fit in registers, we should return as many as
// possible in registers before passing on stack.
bool SITargetLowering::CanLowerReturn(
  CallingConv::ID CallConv,
  MachineFunction &MF, bool IsVarArg,
  const SmallVectorImpl<ISD::OutputArg> &Outs,
  LLVMContext &Context) const {
  // Replacing returns with sret/stack usage doesn't make sense for shaders.
  // FIXME: Also sort of a workaround for custom vector splitting in LowerReturn
  // for shaders. Vector types should be explicitly handled by CC.
  if (AMDGPU::isEntryFunctionCC(CallConv))
    return true;

  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, RVLocs, Context);
  if (!CCInfo.CheckReturn(Outs, CCAssignFnForReturn(CallConv, IsVarArg)))
    return false;

  // We must use the stack if return would require unavailable registers.
  unsigned MaxNumVGPRs = Subtarget->getMaxNumVGPRs(MF);
  unsigned TotalNumVGPRs = AMDGPU::VGPR_32RegClass.getNumRegs();
  for (unsigned i = MaxNumVGPRs; i < TotalNumVGPRs; ++i)
    if (CCInfo.isAllocated(AMDGPU::VGPR_32RegClass.getRegister(i)))
      return false;

  return true;
}

SDValue
SITargetLowering::LowerReturn(SDValue Chain, CallingConv::ID CallConv,
                              bool isVarArg,
                              const SmallVectorImpl<ISD::OutputArg> &Outs,
                              const SmallVectorImpl<SDValue> &OutVals,
                              const SDLoc &DL, SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();

  if (AMDGPU::isKernel(CallConv)) {
    return AMDGPUTargetLowering::LowerReturn(Chain, CallConv, isVarArg, Outs,
                                             OutVals, DL, DAG);
  }

  bool IsShader = AMDGPU::isShader(CallConv);

  Info->setIfReturnsVoid(Outs.empty());
  bool IsWaveEnd = Info->returnsVoid() && IsShader;

  // CCValAssign - represent the assignment of the return value to a location.
  SmallVector<CCValAssign, 48> RVLocs;
  SmallVector<ISD::OutputArg, 48> Splits;

  // CCState - Info about the registers and stack slots.
  CCState CCInfo(CallConv, isVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  // Analyze outgoing return values.
  CCInfo.AnalyzeReturn(Outs, CCAssignFnForReturn(CallConv, isVarArg));

  SDValue Glue;
  SmallVector<SDValue, 48> RetOps;
  RetOps.push_back(Chain); // Operand #0 = Chain (updated below)

  // Copy the result values into the output registers.
  for (unsigned I = 0, RealRVLocIdx = 0, E = RVLocs.size(); I != E;
       ++I, ++RealRVLocIdx) {
    CCValAssign &VA = RVLocs[I];
    assert(VA.isRegLoc() && "Can only return in registers!");
    // TODO: Partially return in registers if return values don't fit.
    SDValue Arg = OutVals[RealRVLocIdx];

    // Copied from other backends.
    switch (VA.getLocInfo()) {
    case CCValAssign::Full:
      break;
    case CCValAssign::BCvt:
      Arg = DAG.getNode(ISD::BITCAST, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    default:
      llvm_unreachable("Unknown loc info!");
    }

    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), Arg, Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  // FIXME: Does sret work properly?
  if (!Info->isEntryFunction()) {
    const SIRegisterInfo *TRI = Subtarget->getRegisterInfo();
    const MCPhysReg *I =
      TRI->getCalleeSavedRegsViaCopy(&DAG.getMachineFunction());
    if (I) {
      for (; *I; ++I) {
        if (AMDGPU::SReg_64RegClass.contains(*I))
          RetOps.push_back(DAG.getRegister(*I, MVT::i64));
        else if (AMDGPU::SReg_32RegClass.contains(*I))
          RetOps.push_back(DAG.getRegister(*I, MVT::i32));
        else
          llvm_unreachable("Unexpected register class in CSRsViaCopy!");
      }
    }
  }

  // Update chain and glue.
  RetOps[0] = Chain;
  if (Glue.getNode())
    RetOps.push_back(Glue);

  unsigned Opc = AMDGPUISD::ENDPGM;
  if (!IsWaveEnd)
    Opc = IsShader ? AMDGPUISD::RETURN_TO_EPILOG : AMDGPUISD::RET_GLUE;
  return DAG.getNode(Opc, DL, MVT::Other, RetOps);
}

SDValue SITargetLowering::LowerCallResult(
    SDValue Chain, SDValue InGlue, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals, bool IsThisReturn,
    SDValue ThisVal) const {
  CCAssignFn *RetCC = CCAssignFnForReturn(CallConv, IsVarArg);

  // Assign locations to each value returned by this call.
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  CCInfo.AnalyzeCallResult(Ins, RetCC);

  // Copy all of the result registers out of their specified physreg.
  for (CCValAssign VA : RVLocs) {
    SDValue Val;

    if (VA.isRegLoc()) {
      Val = DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), VA.getLocVT(), InGlue);
      Chain = Val.getValue(1);
      InGlue = Val.getValue(2);
    } else if (VA.isMemLoc()) {
      report_fatal_error("TODO: return values in memory");
    } else
      llvm_unreachable("unknown argument location type");

    switch (VA.getLocInfo()) {
    case CCValAssign::Full:
      break;
    case CCValAssign::BCvt:
      Val = DAG.getNode(ISD::BITCAST, DL, VA.getValVT(), Val);
      break;
    case CCValAssign::ZExt:
      Val = DAG.getNode(ISD::AssertZext, DL, VA.getLocVT(), Val,
                        DAG.getValueType(VA.getValVT()));
      Val = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Val);
      break;
    case CCValAssign::SExt:
      Val = DAG.getNode(ISD::AssertSext, DL, VA.getLocVT(), Val,
                        DAG.getValueType(VA.getValVT()));
      Val = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Val);
      break;
    case CCValAssign::AExt:
      Val = DAG.getNode(ISD::TRUNCATE, DL, VA.getValVT(), Val);
      break;
    default:
      llvm_unreachable("Unknown loc info!");
    }

    InVals.push_back(Val);
  }

  return Chain;
}

// Add code to pass special inputs required depending on used features separate
// from the explicit user arguments present in the IR.
void SITargetLowering::passSpecialInputs(
    CallLoweringInfo &CLI,
    CCState &CCInfo,
    const SIMachineFunctionInfo &Info,
    SmallVectorImpl<std::pair<unsigned, SDValue>> &RegsToPass,
    SmallVectorImpl<SDValue> &MemOpChains,
    SDValue Chain) const {
  // If we don't have a call site, this was a call inserted by
  // legalization. These can never use special inputs.
  if (!CLI.CB)
    return;

  SelectionDAG &DAG = CLI.DAG;
  const SDLoc &DL = CLI.DL;
  const Function &F = DAG.getMachineFunction().getFunction();

  const SIRegisterInfo *TRI = Subtarget->getRegisterInfo();
  const AMDGPUFunctionArgInfo &CallerArgInfo = Info.getArgInfo();

  const AMDGPUFunctionArgInfo *CalleeArgInfo
    = &AMDGPUArgumentUsageInfo::FixedABIFunctionInfo;
  if (const Function *CalleeFunc = CLI.CB->getCalledFunction()) {
    // DAG.getPass() returns nullptr when using new pass manager.
    // TODO: Use DAG.getMFAM() to access analysis result.
    if (DAG.getPass()) {
      auto &ArgUsageInfo =
          DAG.getPass()->getAnalysis<AMDGPUArgumentUsageInfo>();
      CalleeArgInfo = &ArgUsageInfo.lookupFuncArgInfo(*CalleeFunc);
    }
  }

  // TODO: Unify with private memory register handling. This is complicated by
  // the fact that at least in kernels, the input argument is not necessarily
  // in the same location as the input.
  static constexpr std::pair<AMDGPUFunctionArgInfo::PreloadedValue,
                             StringLiteral> ImplicitAttrs[] = {
    {AMDGPUFunctionArgInfo::DISPATCH_PTR, "amdgpu-no-dispatch-ptr"},
    {AMDGPUFunctionArgInfo::QUEUE_PTR, "amdgpu-no-queue-ptr" },
    {AMDGPUFunctionArgInfo::IMPLICIT_ARG_PTR, "amdgpu-no-implicitarg-ptr"},
    {AMDGPUFunctionArgInfo::DISPATCH_ID, "amdgpu-no-dispatch-id"},
    {AMDGPUFunctionArgInfo::WORKGROUP_ID_X, "amdgpu-no-workgroup-id-x"},
    {AMDGPUFunctionArgInfo::WORKGROUP_ID_Y,"amdgpu-no-workgroup-id-y"},
    {AMDGPUFunctionArgInfo::WORKGROUP_ID_Z,"amdgpu-no-workgroup-id-z"},
    {AMDGPUFunctionArgInfo::LDS_KERNEL_ID,"amdgpu-no-lds-kernel-id"},
  };

  for (auto Attr : ImplicitAttrs) {
    const ArgDescriptor *OutgoingArg;
    const TargetRegisterClass *ArgRC;
    LLT ArgTy;

    AMDGPUFunctionArgInfo::PreloadedValue InputID = Attr.first;

    // If the callee does not use the attribute value, skip copying the value.
    if (CLI.CB->hasFnAttr(Attr.second))
      continue;

    std::tie(OutgoingArg, ArgRC, ArgTy) =
        CalleeArgInfo->getPreloadedValue(InputID);
    if (!OutgoingArg)
      continue;

    const ArgDescriptor *IncomingArg;
    const TargetRegisterClass *IncomingArgRC;
    LLT Ty;
    std::tie(IncomingArg, IncomingArgRC, Ty) =
        CallerArgInfo.getPreloadedValue(InputID);
    assert(IncomingArgRC == ArgRC);

    // All special arguments are ints for now.
    EVT ArgVT = TRI->getSpillSize(*ArgRC) == 8 ? MVT::i64 : MVT::i32;
    SDValue InputReg;

    if (IncomingArg) {
      InputReg = loadInputValue(DAG, ArgRC, ArgVT, DL, *IncomingArg);
    } else if (InputID == AMDGPUFunctionArgInfo::IMPLICIT_ARG_PTR) {
      // The implicit arg ptr is special because it doesn't have a corresponding
      // input for kernels, and is computed from the kernarg segment pointer.
      InputReg = getImplicitArgPtr(DAG, DL);
    } else if (InputID == AMDGPUFunctionArgInfo::LDS_KERNEL_ID) {
      std::optional<uint32_t> Id =
          AMDGPUMachineFunction::getLDSKernelIdMetadata(F);
      if (Id.has_value()) {
        InputReg = DAG.getConstant(*Id, DL, ArgVT);
      } else {
        InputReg = DAG.getUNDEF(ArgVT);
      }
    } else {
      // We may have proven the input wasn't needed, although the ABI is
      // requiring it. We just need to allocate the register appropriately.
      InputReg = DAG.getUNDEF(ArgVT);
    }

    if (OutgoingArg->isRegister()) {
      RegsToPass.emplace_back(OutgoingArg->getRegister(), InputReg);
      if (!CCInfo.AllocateReg(OutgoingArg->getRegister()))
        report_fatal_error("failed to allocate implicit input argument");
    } else {
      unsigned SpecialArgOffset =
          CCInfo.AllocateStack(ArgVT.getStoreSize(), Align(4));
      SDValue ArgStore = storeStackInputValue(DAG, DL, Chain, InputReg,
                                              SpecialArgOffset);
      MemOpChains.push_back(ArgStore);
    }
  }

  // Pack workitem IDs into a single register or pass it as is if already
  // packed.
  const ArgDescriptor *OutgoingArg;
  const TargetRegisterClass *ArgRC;
  LLT Ty;

  std::tie(OutgoingArg, ArgRC, Ty) =
      CalleeArgInfo->getPreloadedValue(AMDGPUFunctionArgInfo::WORKITEM_ID_X);
  if (!OutgoingArg)
    std::tie(OutgoingArg, ArgRC, Ty) =
        CalleeArgInfo->getPreloadedValue(AMDGPUFunctionArgInfo::WORKITEM_ID_Y);
  if (!OutgoingArg)
    std::tie(OutgoingArg, ArgRC, Ty) =
        CalleeArgInfo->getPreloadedValue(AMDGPUFunctionArgInfo::WORKITEM_ID_Z);
  if (!OutgoingArg)
    return;

  const ArgDescriptor *IncomingArgX = std::get<0>(
      CallerArgInfo.getPreloadedValue(AMDGPUFunctionArgInfo::WORKITEM_ID_X));
  const ArgDescriptor *IncomingArgY = std::get<0>(
      CallerArgInfo.getPreloadedValue(AMDGPUFunctionArgInfo::WORKITEM_ID_Y));
  const ArgDescriptor *IncomingArgZ = std::get<0>(
      CallerArgInfo.getPreloadedValue(AMDGPUFunctionArgInfo::WORKITEM_ID_Z));

  SDValue InputReg;
  SDLoc SL;

  const bool NeedWorkItemIDX = !CLI.CB->hasFnAttr("amdgpu-no-workitem-id-x");
  const bool NeedWorkItemIDY = !CLI.CB->hasFnAttr("amdgpu-no-workitem-id-y");
  const bool NeedWorkItemIDZ = !CLI.CB->hasFnAttr("amdgpu-no-workitem-id-z");

  // If incoming ids are not packed we need to pack them.
  if (IncomingArgX && !IncomingArgX->isMasked() && CalleeArgInfo->WorkItemIDX &&
      NeedWorkItemIDX) {
    if (Subtarget->getMaxWorkitemID(F, 0) != 0) {
      InputReg = loadInputValue(DAG, ArgRC, MVT::i32, DL, *IncomingArgX);
    } else {
      InputReg = DAG.getConstant(0, DL, MVT::i32);
    }
  }

  if (IncomingArgY && !IncomingArgY->isMasked() && CalleeArgInfo->WorkItemIDY &&
      NeedWorkItemIDY && Subtarget->getMaxWorkitemID(F, 1) != 0) {
    SDValue Y = loadInputValue(DAG, ArgRC, MVT::i32, DL, *IncomingArgY);
    Y = DAG.getNode(ISD::SHL, SL, MVT::i32, Y,
                    DAG.getShiftAmountConstant(10, MVT::i32, SL));
    InputReg = InputReg.getNode() ?
                 DAG.getNode(ISD::OR, SL, MVT::i32, InputReg, Y) : Y;
  }

  if (IncomingArgZ && !IncomingArgZ->isMasked() && CalleeArgInfo->WorkItemIDZ &&
      NeedWorkItemIDZ && Subtarget->getMaxWorkitemID(F, 2) != 0) {
    SDValue Z = loadInputValue(DAG, ArgRC, MVT::i32, DL, *IncomingArgZ);
    Z = DAG.getNode(ISD::SHL, SL, MVT::i32, Z,
                    DAG.getShiftAmountConstant(20, MVT::i32, SL));
    InputReg = InputReg.getNode() ?
                 DAG.getNode(ISD::OR, SL, MVT::i32, InputReg, Z) : Z;
  }

  if (!InputReg && (NeedWorkItemIDX || NeedWorkItemIDY || NeedWorkItemIDZ)) {
    if (!IncomingArgX && !IncomingArgY && !IncomingArgZ) {
      // We're in a situation where the outgoing function requires the workitem
      // ID, but the calling function does not have it (e.g a graphics function
      // calling a C calling convention function). This is illegal, but we need
      // to produce something.
      InputReg = DAG.getUNDEF(MVT::i32);
    } else {
      // Workitem ids are already packed, any of present incoming arguments
      // will carry all required fields.
      ArgDescriptor IncomingArg = ArgDescriptor::createArg(
        IncomingArgX ? *IncomingArgX :
        IncomingArgY ? *IncomingArgY :
        *IncomingArgZ, ~0u);
      InputReg = loadInputValue(DAG, ArgRC, MVT::i32, DL, IncomingArg);
    }
  }

  if (OutgoingArg->isRegister()) {
    if (InputReg)
      RegsToPass.emplace_back(OutgoingArg->getRegister(), InputReg);

    CCInfo.AllocateReg(OutgoingArg->getRegister());
  } else {
    unsigned SpecialArgOffset = CCInfo.AllocateStack(4, Align(4));
    if (InputReg) {
      SDValue ArgStore = storeStackInputValue(DAG, DL, Chain, InputReg,
                                              SpecialArgOffset);
      MemOpChains.push_back(ArgStore);
    }
  }
}

static bool canGuaranteeTCO(CallingConv::ID CC) {
  return CC == CallingConv::Fast;
}

/// Return true if we might ever do TCO for calls with this calling convention.
static bool mayTailCallThisCC(CallingConv::ID CC) {
  switch (CC) {
  case CallingConv::C:
  case CallingConv::AMDGPU_Gfx:
    return true;
  default:
    return canGuaranteeTCO(CC);
  }
}

bool SITargetLowering::isEligibleForTailCallOptimization(
    SDValue Callee, CallingConv::ID CalleeCC, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals,
    const SmallVectorImpl<ISD::InputArg> &Ins, SelectionDAG &DAG) const {
  if (AMDGPU::isChainCC(CalleeCC))
    return true;

  if (!mayTailCallThisCC(CalleeCC))
    return false;

  // For a divergent call target, we need to do a waterfall loop over the
  // possible callees which precludes us from using a simple jump.
  if (Callee->isDivergent())
    return false;

  MachineFunction &MF = DAG.getMachineFunction();
  const Function &CallerF = MF.getFunction();
  CallingConv::ID CallerCC = CallerF.getCallingConv();
  const SIRegisterInfo *TRI = getSubtarget()->getRegisterInfo();
  const uint32_t *CallerPreserved = TRI->getCallPreservedMask(MF, CallerCC);

  // Kernels aren't callable, and don't have a live in return address so it
  // doesn't make sense to do a tail call with entry functions.
  if (!CallerPreserved)
    return false;

  bool CCMatch = CallerCC == CalleeCC;

  if (DAG.getTarget().Options.GuaranteedTailCallOpt) {
    if (canGuaranteeTCO(CalleeCC) && CCMatch)
      return true;
    return false;
  }

  // TODO: Can we handle var args?
  if (IsVarArg)
    return false;

  for (const Argument &Arg : CallerF.args()) {
    if (Arg.hasByValAttr())
      return false;
  }

  LLVMContext &Ctx = *DAG.getContext();

  // Check that the call results are passed in the same way.
  if (!CCState::resultsCompatible(CalleeCC, CallerCC, MF, Ctx, Ins,
                                  CCAssignFnForCall(CalleeCC, IsVarArg),
                                  CCAssignFnForCall(CallerCC, IsVarArg)))
    return false;

  // The callee has to preserve all registers the caller needs to preserve.
  if (!CCMatch) {
    const uint32_t *CalleePreserved = TRI->getCallPreservedMask(MF, CalleeCC);
    if (!TRI->regmaskSubsetEqual(CallerPreserved, CalleePreserved))
      return false;
  }

  // Nothing more to check if the callee is taking no arguments.
  if (Outs.empty())
    return true;

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CalleeCC, IsVarArg, MF, ArgLocs, Ctx);

  CCInfo.AnalyzeCallOperands(Outs, CCAssignFnForCall(CalleeCC, IsVarArg));

  const SIMachineFunctionInfo *FuncInfo = MF.getInfo<SIMachineFunctionInfo>();
  // If the stack arguments for this call do not fit into our own save area then
  // the call cannot be made tail.
  // TODO: Is this really necessary?
  if (CCInfo.getStackSize() > FuncInfo->getBytesInStackArgArea())
    return false;

  const MachineRegisterInfo &MRI = MF.getRegInfo();
  return parametersInCSRMatch(MRI, CallerPreserved, ArgLocs, OutVals);
}

bool SITargetLowering::mayBeEmittedAsTailCall(const CallInst *CI) const {
  if (!CI->isTailCall())
    return false;

  const Function *ParentFn = CI->getParent()->getParent();
  if (AMDGPU::isEntryFunctionCC(ParentFn->getCallingConv()))
    return false;
  return true;
}

// The wave scratch offset register is used as the global base pointer.
SDValue SITargetLowering::LowerCall(CallLoweringInfo &CLI,
                                    SmallVectorImpl<SDValue> &InVals) const {
  CallingConv::ID CallConv = CLI.CallConv;
  bool IsChainCallConv = AMDGPU::isChainCC(CallConv);

  SelectionDAG &DAG = CLI.DAG;

  TargetLowering::ArgListEntry RequestedExec;
  if (IsChainCallConv) {
    // The last argument should be the value that we need to put in EXEC.
    // Pop it out of CLI.Outs and CLI.OutVals before we do any processing so we
    // don't treat it like the rest of the arguments.
    RequestedExec = CLI.Args.back();
    assert(RequestedExec.Node && "No node for EXEC");

    if (!RequestedExec.Ty->isIntegerTy(Subtarget->getWavefrontSize()))
      return lowerUnhandledCall(CLI, InVals, "Invalid value for EXEC");

    assert(CLI.Outs.back().OrigArgIndex == 2 && "Unexpected last arg");
    CLI.Outs.pop_back();
    CLI.OutVals.pop_back();

    if (RequestedExec.Ty->isIntegerTy(64)) {
      assert(CLI.Outs.back().OrigArgIndex == 2 && "Exec wasn't split up");
      CLI.Outs.pop_back();
      CLI.OutVals.pop_back();
    }

    assert(CLI.Outs.back().OrigArgIndex != 2 &&
           "Haven't popped all the pieces of the EXEC mask");
  }

  const SDLoc &DL = CLI.DL;
  SmallVector<ISD::OutputArg, 32> &Outs = CLI.Outs;
  SmallVector<SDValue, 32> &OutVals = CLI.OutVals;
  SmallVector<ISD::InputArg, 32> &Ins = CLI.Ins;
  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  bool &IsTailCall = CLI.IsTailCall;
  bool IsVarArg = CLI.IsVarArg;
  bool IsSibCall = false;
  MachineFunction &MF = DAG.getMachineFunction();

  if (Callee.isUndef() || isNullConstant(Callee)) {
    if (!CLI.IsTailCall) {
      for (ISD::InputArg &Arg : CLI.Ins)
        InVals.push_back(DAG.getUNDEF(Arg.VT));
    }

    return Chain;
  }

  if (IsVarArg) {
    return lowerUnhandledCall(CLI, InVals,
                              "unsupported call to variadic function ");
  }

  if (!CLI.CB)
    report_fatal_error("unsupported libcall legalization");

  if (IsTailCall && MF.getTarget().Options.GuaranteedTailCallOpt) {
    return lowerUnhandledCall(CLI, InVals,
                              "unsupported required tail call to function ");
  }

  if (IsTailCall) {
    IsTailCall = isEligibleForTailCallOptimization(
      Callee, CallConv, IsVarArg, Outs, OutVals, Ins, DAG);
    if (!IsTailCall &&
        ((CLI.CB && CLI.CB->isMustTailCall()) || IsChainCallConv)) {
      report_fatal_error("failed to perform tail call elimination on a call "
                         "site marked musttail or on llvm.amdgcn.cs.chain");
    }

    bool TailCallOpt = MF.getTarget().Options.GuaranteedTailCallOpt;

    // A sibling call is one where we're under the usual C ABI and not planning
    // to change that but can still do a tail call:
    if (!TailCallOpt && IsTailCall)
      IsSibCall = true;

    if (IsTailCall)
      ++NumTailCalls;
  }

  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();
  SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPass;
  SmallVector<SDValue, 8> MemOpChains;

  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  CCAssignFn *AssignFn = CCAssignFnForCall(CallConv, IsVarArg);

  if (CallConv != CallingConv::AMDGPU_Gfx && !AMDGPU::isChainCC(CallConv)) {
    // With a fixed ABI, allocate fixed registers before user arguments.
    passSpecialInputs(CLI, CCInfo, *Info, RegsToPass, MemOpChains, Chain);
  }

  CCInfo.AnalyzeCallOperands(Outs, AssignFn);

  // Get a count of how many bytes are to be pushed on the stack.
  unsigned NumBytes = CCInfo.getStackSize();

  if (IsSibCall) {
    // Since we're not changing the ABI to make this a tail call, the memory
    // operands are already available in the caller's incoming argument space.
    NumBytes = 0;
  }

  // FPDiff is the byte offset of the call's argument area from the callee's.
  // Stores to callee stack arguments will be placed in FixedStackSlots offset
  // by this amount for a tail call. In a sibling call it must be 0 because the
  // caller will deallocate the entire stack and the callee still expects its
  // arguments to begin at SP+0. Completely unused for non-tail calls.
  int32_t FPDiff = 0;
  MachineFrameInfo &MFI = MF.getFrameInfo();

  // Adjust the stack pointer for the new arguments...
  // These operations are automatically eliminated by the prolog/epilog pass
  if (!IsSibCall)
    Chain = DAG.getCALLSEQ_START(Chain, 0, 0, DL);

  if (!IsSibCall || IsChainCallConv) {
    if (!Subtarget->enableFlatScratch()) {
      SmallVector<SDValue, 4> CopyFromChains;

      // In the HSA case, this should be an identity copy.
      SDValue ScratchRSrcReg
        = DAG.getCopyFromReg(Chain, DL, Info->getScratchRSrcReg(), MVT::v4i32);
      RegsToPass.emplace_back(IsChainCallConv
                                  ? AMDGPU::SGPR48_SGPR49_SGPR50_SGPR51
                                  : AMDGPU::SGPR0_SGPR1_SGPR2_SGPR3,
                              ScratchRSrcReg);
      CopyFromChains.push_back(ScratchRSrcReg.getValue(1));
      Chain = DAG.getTokenFactor(DL, CopyFromChains);
    }
  }

  MVT PtrVT = MVT::i32;

  // Walk the register/memloc assignments, inserting copies/loads.
  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    CCValAssign &VA = ArgLocs[i];
    SDValue Arg = OutVals[i];

    // Promote the value if needed.
    switch (VA.getLocInfo()) {
    case CCValAssign::Full:
      break;
    case CCValAssign::BCvt:
      Arg = DAG.getNode(ISD::BITCAST, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::FPExt:
      Arg = DAG.getNode(ISD::FP_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    default:
      llvm_unreachable("Unknown loc info!");
    }

    if (VA.isRegLoc()) {
      RegsToPass.push_back(std::pair(VA.getLocReg(), Arg));
    } else {
      assert(VA.isMemLoc());

      SDValue DstAddr;
      MachinePointerInfo DstInfo;

      unsigned LocMemOffset = VA.getLocMemOffset();
      int32_t Offset = LocMemOffset;

      SDValue PtrOff = DAG.getConstant(Offset, DL, PtrVT);
      MaybeAlign Alignment;

      if (IsTailCall) {
        ISD::ArgFlagsTy Flags = Outs[i].Flags;
        unsigned OpSize = Flags.isByVal() ?
          Flags.getByValSize() : VA.getValVT().getStoreSize();

        // FIXME: We can have better than the minimum byval required alignment.
        Alignment =
            Flags.isByVal()
                ? Flags.getNonZeroByValAlign()
                : commonAlignment(Subtarget->getStackAlignment(), Offset);

        Offset = Offset + FPDiff;
        int FI = MFI.CreateFixedObject(OpSize, Offset, true);

        DstAddr = DAG.getFrameIndex(FI, PtrVT);
        DstInfo = MachinePointerInfo::getFixedStack(MF, FI);

        // Make sure any stack arguments overlapping with where we're storing
        // are loaded before this eventual operation. Otherwise they'll be
        // clobbered.

        // FIXME: Why is this really necessary? This seems to just result in a
        // lot of code to copy the stack and write them back to the same
        // locations, which are supposed to be immutable?
        Chain = addTokenForArgument(Chain, DAG, MFI, FI);
      } else {
        // Stores to the argument stack area are relative to the stack pointer.
        SDValue SP = DAG.getCopyFromReg(Chain, DL, Info->getStackPtrOffsetReg(),
                                        MVT::i32);
        DstAddr = DAG.getNode(ISD::ADD, DL, MVT::i32, SP, PtrOff);
        DstInfo = MachinePointerInfo::getStack(MF, LocMemOffset);
        Alignment =
            commonAlignment(Subtarget->getStackAlignment(), LocMemOffset);
      }

      if (Outs[i].Flags.isByVal()) {
        SDValue SizeNode =
            DAG.getConstant(Outs[i].Flags.getByValSize(), DL, MVT::i32);
        SDValue Cpy =
            DAG.getMemcpy(Chain, DL, DstAddr, Arg, SizeNode,
                          Outs[i].Flags.getNonZeroByValAlign(),
                          /*isVol = */ false, /*AlwaysInline = */ true,
                          /*CI=*/nullptr, std::nullopt, DstInfo,
                          MachinePointerInfo(AMDGPUAS::PRIVATE_ADDRESS));

        MemOpChains.push_back(Cpy);
      } else {
        SDValue Store =
            DAG.getStore(Chain, DL, Arg, DstAddr, DstInfo, Alignment);
        MemOpChains.push_back(Store);
      }
    }
  }

  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOpChains);

  // Build a sequence of copy-to-reg nodes chained together with token chain
  // and flag operands which copy the outgoing args into the appropriate regs.
  SDValue InGlue;
  for (auto &RegToPass : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, DL, RegToPass.first,
                             RegToPass.second, InGlue);
    InGlue = Chain.getValue(1);
  }


  // We don't usually want to end the call-sequence here because we would tidy
  // the frame up *after* the call, however in the ABI-changing tail-call case
  // we've carefully laid out the parameters so that when sp is reset they'll be
  // in the correct location.
  if (IsTailCall && !IsSibCall) {
    Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, InGlue, DL);
    InGlue = Chain.getValue(1);
  }

  std::vector<SDValue> Ops;
  Ops.push_back(Chain);
  Ops.push_back(Callee);
  // Add a redundant copy of the callee global which will not be legalized, as
  // we need direct access to the callee later.
  if (GlobalAddressSDNode *GSD = dyn_cast<GlobalAddressSDNode>(Callee)) {
    const GlobalValue *GV = GSD->getGlobal();
    Ops.push_back(DAG.getTargetGlobalAddress(GV, DL, MVT::i64));
  } else {
    Ops.push_back(DAG.getTargetConstant(0, DL, MVT::i64));
  }

  if (IsTailCall) {
    // Each tail call may have to adjust the stack by a different amount, so
    // this information must travel along with the operation for eventual
    // consumption by emitEpilogue.
    Ops.push_back(DAG.getTargetConstant(FPDiff, DL, MVT::i32));
  }

  if (IsChainCallConv)
    Ops.push_back(RequestedExec.Node);

  // Add argument registers to the end of the list so that they are known live
  // into the call.
  for (auto &RegToPass : RegsToPass) {
    Ops.push_back(DAG.getRegister(RegToPass.first,
                                  RegToPass.second.getValueType()));
  }

  // Add a register mask operand representing the call-preserved registers.
  auto *TRI = static_cast<const SIRegisterInfo *>(Subtarget->getRegisterInfo());
  const uint32_t *Mask = TRI->getCallPreservedMask(MF, CallConv);
  assert(Mask && "Missing call preserved mask for calling convention");
  Ops.push_back(DAG.getRegisterMask(Mask));

  if (SDValue Token = CLI.ConvergenceControlToken) {
    SmallVector<SDValue, 2> GlueOps;
    GlueOps.push_back(Token);
    if (InGlue)
      GlueOps.push_back(InGlue);

    InGlue = SDValue(DAG.getMachineNode(TargetOpcode::CONVERGENCECTRL_GLUE, DL,
                                        MVT::Glue, GlueOps),
                     0);
  }

  if (InGlue)
    Ops.push_back(InGlue);

  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);

  // If we're doing a tall call, use a TC_RETURN here rather than an
  // actual call instruction.
  if (IsTailCall) {
    MFI.setHasTailCall();
    unsigned OPC = AMDGPUISD::TC_RETURN;
    switch (CallConv) {
    case CallingConv::AMDGPU_Gfx:
      OPC = AMDGPUISD::TC_RETURN_GFX;
      break;
    case CallingConv::AMDGPU_CS_Chain:
    case CallingConv::AMDGPU_CS_ChainPreserve:
      OPC = AMDGPUISD::TC_RETURN_CHAIN;
      break;
    }

    return DAG.getNode(OPC, DL, NodeTys, Ops);
  }

  // Returns a chain and a flag for retval copy to use.
  SDValue Call = DAG.getNode(AMDGPUISD::CALL, DL, NodeTys, Ops);
  Chain = Call.getValue(0);
  InGlue = Call.getValue(1);

  uint64_t CalleePopBytes = NumBytes;
  Chain = DAG.getCALLSEQ_END(Chain, 0, CalleePopBytes, InGlue, DL);
  if (!Ins.empty())
    InGlue = Chain.getValue(1);

  // Handle result values, copying them out of physregs into vregs that we
  // return.
  return LowerCallResult(Chain, InGlue, CallConv, IsVarArg, Ins, DL, DAG,
                         InVals, /*IsThisReturn=*/false, SDValue());
}

// This is identical to the default implementation in ExpandDYNAMIC_STACKALLOC,
// except for applying the wave size scale to the increment amount.
SDValue SITargetLowering::lowerDYNAMIC_STACKALLOCImpl(
    SDValue Op, SelectionDAG &DAG) const {
  const MachineFunction &MF = DAG.getMachineFunction();
  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();

  SDLoc dl(Op);
  EVT VT = Op.getValueType();
  SDValue Tmp1 = Op;
  SDValue Tmp2 = Op.getValue(1);
  SDValue Tmp3 = Op.getOperand(2);
  SDValue Chain = Tmp1.getOperand(0);

  Register SPReg = Info->getStackPtrOffsetReg();

  // Chain the dynamic stack allocation so that it doesn't modify the stack
  // pointer when other instructions are using the stack.
  Chain = DAG.getCALLSEQ_START(Chain, 0, 0, dl);

  SDValue Size  = Tmp2.getOperand(1);
  SDValue SP = DAG.getCopyFromReg(Chain, dl, SPReg, VT);
  Chain = SP.getValue(1);
  MaybeAlign Alignment = cast<ConstantSDNode>(Tmp3)->getMaybeAlignValue();
  const TargetFrameLowering *TFL = Subtarget->getFrameLowering();
  unsigned Opc =
    TFL->getStackGrowthDirection() == TargetFrameLowering::StackGrowsUp ?
    ISD::ADD : ISD::SUB;

  SDValue ScaledSize = DAG.getNode(
      ISD::SHL, dl, VT, Size,
      DAG.getConstant(Subtarget->getWavefrontSizeLog2(), dl, MVT::i32));

  Align StackAlign = TFL->getStackAlign();
  Tmp1 = DAG.getNode(Opc, dl, VT, SP, ScaledSize); // Value
  if (Alignment && *Alignment > StackAlign) {
    Tmp1 = DAG.getNode(ISD::AND, dl, VT, Tmp1,
                       DAG.getConstant(-(uint64_t)Alignment->value()
                                           << Subtarget->getWavefrontSizeLog2(),
                                       dl, VT));
  }

  Chain = DAG.getCopyToReg(Chain, dl, SPReg, Tmp1);    // Output chain
  Tmp2 = DAG.getCALLSEQ_END(Chain, 0, 0, SDValue(), dl);

  return DAG.getMergeValues({Tmp1, Tmp2}, dl);
}

SDValue SITargetLowering::LowerDYNAMIC_STACKALLOC(SDValue Op,
                                                  SelectionDAG &DAG) const {
  // We only handle constant sizes here to allow non-entry block, static sized
  // allocas. A truly dynamic value is more difficult to support because we
  // don't know if the size value is uniform or not. If the size isn't uniform,
  // we would need to do a wave reduction to get the maximum size to know how
  // much to increment the uniform stack pointer.
  SDValue Size = Op.getOperand(1);
  if (isa<ConstantSDNode>(Size))
      return lowerDYNAMIC_STACKALLOCImpl(Op, DAG); // Use "generic" expansion.

  return AMDGPUTargetLowering::LowerDYNAMIC_STACKALLOC(Op, DAG);
}

SDValue SITargetLowering::LowerSTACKSAVE(SDValue Op, SelectionDAG &DAG) const {
  if (Op.getValueType() != MVT::i32)
    return Op; // Defer to cannot select error.

  Register SP = getStackPointerRegisterToSaveRestore();
  SDLoc SL(Op);

  SDValue CopyFromSP = DAG.getCopyFromReg(Op->getOperand(0), SL, SP, MVT::i32);

  // Convert from wave uniform to swizzled vector address. This should protect
  // from any edge cases where the stacksave result isn't directly used with
  // stackrestore.
  SDValue VectorAddress =
      DAG.getNode(AMDGPUISD::WAVE_ADDRESS, SL, MVT::i32, CopyFromSP);
  return DAG.getMergeValues({VectorAddress, CopyFromSP.getValue(1)}, SL);
}

SDValue SITargetLowering::lowerGET_ROUNDING(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc SL(Op);
  assert(Op.getValueType() == MVT::i32);

  uint32_t BothRoundHwReg =
      AMDGPU::Hwreg::HwregEncoding::encode(AMDGPU::Hwreg::ID_MODE, 0, 4);
  SDValue GetRoundBothImm = DAG.getTargetConstant(BothRoundHwReg, SL, MVT::i32);

  SDValue IntrinID =
      DAG.getTargetConstant(Intrinsic::amdgcn_s_getreg, SL, MVT::i32);
  SDValue GetReg = DAG.getNode(ISD::INTRINSIC_W_CHAIN, SL, Op->getVTList(),
                               Op.getOperand(0), IntrinID, GetRoundBothImm);

  // There are two rounding modes, one for f32 and one for f64/f16. We only
  // report in the standard value range if both are the same.
  //
  // The raw values also differ from the expected FLT_ROUNDS values. Nearest
  // ties away from zero is not supported, and the other values are rotated by
  // 1.
  //
  // If the two rounding modes are not the same, report a target defined value.

  // Mode register rounding mode fields:
  //
  // [1:0] Single-precision round mode.
  // [3:2] Double/Half-precision round mode.
  //
  // 0=nearest even; 1= +infinity; 2= -infinity, 3= toward zero.
  //
  //             Hardware   Spec
  // Toward-0        3        0
  // Nearest Even    0        1
  // +Inf            1        2
  // -Inf            2        3
  //  NearestAway0  N/A       4
  //
  // We have to handle 16 permutations of a 4-bit value, so we create a 64-bit
  // table we can index by the raw hardware mode.
  //
  // (trunc (FltRoundConversionTable >> MODE.fp_round)) & 0xf

  SDValue BitTable =
      DAG.getConstant(AMDGPU::FltRoundConversionTable, SL, MVT::i64);

  SDValue Two = DAG.getConstant(2, SL, MVT::i32);
  SDValue RoundModeTimesNumBits =
      DAG.getNode(ISD::SHL, SL, MVT::i32, GetReg, Two);

  // TODO: We could possibly avoid a 64-bit shift and use a simpler table if we
  // knew only one mode was demanded.
  SDValue TableValue =
      DAG.getNode(ISD::SRL, SL, MVT::i64, BitTable, RoundModeTimesNumBits);
  SDValue TruncTable = DAG.getNode(ISD::TRUNCATE, SL, MVT::i32, TableValue);

  SDValue EntryMask = DAG.getConstant(0xf, SL, MVT::i32);
  SDValue TableEntry =
      DAG.getNode(ISD::AND, SL, MVT::i32, TruncTable, EntryMask);

  // There's a gap in the 4-bit encoded table and actual enum values, so offset
  // if it's an extended value.
  SDValue Four = DAG.getConstant(4, SL, MVT::i32);
  SDValue IsStandardValue =
      DAG.getSetCC(SL, MVT::i1, TableEntry, Four, ISD::SETULT);
  SDValue EnumOffset = DAG.getNode(ISD::ADD, SL, MVT::i32, TableEntry, Four);
  SDValue Result = DAG.getNode(ISD::SELECT, SL, MVT::i32, IsStandardValue,
                               TableEntry, EnumOffset);

  return DAG.getMergeValues({Result, GetReg.getValue(1)}, SL);
}

SDValue SITargetLowering::lowerSET_ROUNDING(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc SL(Op);

  SDValue NewMode = Op.getOperand(1);
  assert(NewMode.getValueType() == MVT::i32);

  // Index a table of 4-bit entries mapping from the C FLT_ROUNDS values to the
  // hardware MODE.fp_round values.
  if (auto *ConstMode = dyn_cast<ConstantSDNode>(NewMode)) {
    uint32_t ClampedVal = std::min(
        static_cast<uint32_t>(ConstMode->getZExtValue()),
        static_cast<uint32_t>(AMDGPU::TowardZeroF32_TowardNegativeF64));
    NewMode = DAG.getConstant(
        AMDGPU::decodeFltRoundToHWConversionTable(ClampedVal), SL, MVT::i32);
  } else {
    // If we know the input can only be one of the supported standard modes in
    // the range 0-3, we can use a simplified mapping to hardware values.
    KnownBits KB = DAG.computeKnownBits(NewMode);
    const bool UseReducedTable = KB.countMinLeadingZeros() >= 30;
    // The supported standard values are 0-3. The extended values start at 8. We
    // need to offset by 4 if the value is in the extended range.

    if (UseReducedTable) {
      // Truncate to the low 32-bits.
      SDValue BitTable = DAG.getConstant(
          AMDGPU::FltRoundToHWConversionTable & 0xffff, SL, MVT::i32);

      SDValue Two = DAG.getConstant(2, SL, MVT::i32);
      SDValue RoundModeTimesNumBits =
          DAG.getNode(ISD::SHL, SL, MVT::i32, NewMode, Two);

      NewMode =
          DAG.getNode(ISD::SRL, SL, MVT::i32, BitTable, RoundModeTimesNumBits);

      // TODO: SimplifyDemandedBits on the setreg source here can likely reduce
      // the table extracted bits into inline immediates.
    } else {
      // table_index = umin(value, value - 4)
      // MODE.fp_round = (bit_table >> (table_index << 2)) & 0xf
      SDValue BitTable =
          DAG.getConstant(AMDGPU::FltRoundToHWConversionTable, SL, MVT::i64);

      SDValue Four = DAG.getConstant(4, SL, MVT::i32);
      SDValue OffsetEnum = DAG.getNode(ISD::SUB, SL, MVT::i32, NewMode, Four);
      SDValue IndexVal =
          DAG.getNode(ISD::UMIN, SL, MVT::i32, NewMode, OffsetEnum);

      SDValue Two = DAG.getConstant(2, SL, MVT::i32);
      SDValue RoundModeTimesNumBits =
          DAG.getNode(ISD::SHL, SL, MVT::i32, IndexVal, Two);

      SDValue TableValue =
          DAG.getNode(ISD::SRL, SL, MVT::i64, BitTable, RoundModeTimesNumBits);
      SDValue TruncTable = DAG.getNode(ISD::TRUNCATE, SL, MVT::i32, TableValue);

      // No need to mask out the high bits since the setreg will ignore them
      // anyway.
      NewMode = TruncTable;
    }

    // Insert a readfirstlane in case the value is a VGPR. We could do this
    // earlier and keep more operations scalar, but that interferes with
    // combining the source.
    SDValue ReadFirstLaneID =
        DAG.getTargetConstant(Intrinsic::amdgcn_readfirstlane, SL, MVT::i32);
    NewMode = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, SL, MVT::i32,
                          ReadFirstLaneID, NewMode);
  }

  // N.B. The setreg will be later folded into s_round_mode on supported
  // targets.
  SDValue IntrinID =
      DAG.getTargetConstant(Intrinsic::amdgcn_s_setreg, SL, MVT::i32);
  uint32_t BothRoundHwReg =
      AMDGPU::Hwreg::HwregEncoding::encode(AMDGPU::Hwreg::ID_MODE, 0, 4);
  SDValue RoundBothImm = DAG.getTargetConstant(BothRoundHwReg, SL, MVT::i32);

  SDValue SetReg =
      DAG.getNode(ISD::INTRINSIC_VOID, SL, Op->getVTList(), Op.getOperand(0),
                  IntrinID, RoundBothImm, NewMode);

  return SetReg;
}

SDValue SITargetLowering::lowerPREFETCH(SDValue Op, SelectionDAG &DAG) const {
  if (Op->isDivergent())
    return SDValue();

  switch (cast<MemSDNode>(Op)->getAddressSpace()) {
  case AMDGPUAS::FLAT_ADDRESS:
  case AMDGPUAS::GLOBAL_ADDRESS:
  case AMDGPUAS::CONSTANT_ADDRESS:
  case AMDGPUAS::CONSTANT_ADDRESS_32BIT:
    break;
  default:
    return SDValue();
  }

  return Op;
}

// Work around DAG legality rules only based on the result type.
SDValue SITargetLowering::lowerFP_EXTEND(SDValue Op, SelectionDAG &DAG) const {
  bool IsStrict = Op.getOpcode() == ISD::STRICT_FP_EXTEND;
  SDValue Src = Op.getOperand(IsStrict ? 1 : 0);
  EVT SrcVT = Src.getValueType();

  if (SrcVT.getScalarType() != MVT::bf16)
    return Op;

  SDLoc SL(Op);
  SDValue BitCast =
      DAG.getNode(ISD::BITCAST, SL, SrcVT.changeTypeToInteger(), Src);

  EVT DstVT = Op.getValueType();
  if (IsStrict)
    llvm_unreachable("Need STRICT_BF16_TO_FP");

  return DAG.getNode(ISD::BF16_TO_FP, SL, DstVT, BitCast);
}

SDValue SITargetLowering::lowerGET_FPENV(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  if (Op.getValueType() != MVT::i64)
    return Op;

  uint32_t ModeHwReg =
      AMDGPU::Hwreg::HwregEncoding::encode(AMDGPU::Hwreg::ID_MODE, 0, 23);
  SDValue ModeHwRegImm = DAG.getTargetConstant(ModeHwReg, SL, MVT::i32);
  uint32_t TrapHwReg =
      AMDGPU::Hwreg::HwregEncoding::encode(AMDGPU::Hwreg::ID_TRAPSTS, 0, 5);
  SDValue TrapHwRegImm = DAG.getTargetConstant(TrapHwReg, SL, MVT::i32);

  SDVTList VTList = DAG.getVTList(MVT::i32, MVT::Other);
  SDValue IntrinID =
      DAG.getTargetConstant(Intrinsic::amdgcn_s_getreg, SL, MVT::i32);
  SDValue GetModeReg = DAG.getNode(ISD::INTRINSIC_W_CHAIN, SL, VTList,
                                   Op.getOperand(0), IntrinID, ModeHwRegImm);
  SDValue GetTrapReg = DAG.getNode(ISD::INTRINSIC_W_CHAIN, SL, VTList,
                                   Op.getOperand(0), IntrinID, TrapHwRegImm);
  SDValue TokenReg =
      DAG.getNode(ISD::TokenFactor, SL, MVT::Other, GetModeReg.getValue(1),
                  GetTrapReg.getValue(1));

  SDValue CvtPtr =
      DAG.getNode(ISD::BUILD_VECTOR, SL, MVT::v2i32, GetModeReg, GetTrapReg);
  SDValue Result = DAG.getNode(ISD::BITCAST, SL, MVT::i64, CvtPtr);

  return DAG.getMergeValues({Result, TokenReg}, SL);
}

SDValue SITargetLowering::lowerSET_FPENV(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  if (Op.getOperand(1).getValueType() != MVT::i64)
    return Op;

  SDValue Input = DAG.getNode(ISD::BITCAST, SL, MVT::v2i32, Op.getOperand(1));
  SDValue NewModeReg = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, Input,
                                   DAG.getConstant(0, SL, MVT::i32));
  SDValue NewTrapReg = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, Input,
                                   DAG.getConstant(1, SL, MVT::i32));

  SDValue ReadFirstLaneID =
      DAG.getTargetConstant(Intrinsic::amdgcn_readfirstlane, SL, MVT::i32);
  NewModeReg = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, SL, MVT::i32,
                           ReadFirstLaneID, NewModeReg);
  NewTrapReg = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, SL, MVT::i32,
                           ReadFirstLaneID, NewTrapReg);

  unsigned ModeHwReg =
      AMDGPU::Hwreg::HwregEncoding::encode(AMDGPU::Hwreg::ID_MODE, 0, 23);
  SDValue ModeHwRegImm = DAG.getTargetConstant(ModeHwReg, SL, MVT::i32);
  unsigned TrapHwReg =
      AMDGPU::Hwreg::HwregEncoding::encode(AMDGPU::Hwreg::ID_TRAPSTS, 0, 5);
  SDValue TrapHwRegImm = DAG.getTargetConstant(TrapHwReg, SL, MVT::i32);

  SDValue IntrinID =
      DAG.getTargetConstant(Intrinsic::amdgcn_s_setreg, SL, MVT::i32);
  SDValue SetModeReg =
      DAG.getNode(ISD::INTRINSIC_VOID, SL, MVT::Other, Op.getOperand(0),
                  IntrinID, ModeHwRegImm, NewModeReg);
  SDValue SetTrapReg =
      DAG.getNode(ISD::INTRINSIC_VOID, SL, MVT::Other, Op.getOperand(0),
                  IntrinID, TrapHwRegImm, NewTrapReg);
  return DAG.getNode(ISD::TokenFactor, SL, MVT::Other, SetTrapReg, SetModeReg);
}

Register SITargetLowering::getRegisterByName(const char* RegName, LLT VT,
                                             const MachineFunction &MF) const {
  Register Reg = StringSwitch<Register>(RegName)
    .Case("m0", AMDGPU::M0)
    .Case("exec", AMDGPU::EXEC)
    .Case("exec_lo", AMDGPU::EXEC_LO)
    .Case("exec_hi", AMDGPU::EXEC_HI)
    .Case("flat_scratch", AMDGPU::FLAT_SCR)
    .Case("flat_scratch_lo", AMDGPU::FLAT_SCR_LO)
    .Case("flat_scratch_hi", AMDGPU::FLAT_SCR_HI)
    .Default(Register());

  if (Reg == AMDGPU::NoRegister) {
    report_fatal_error(Twine("invalid register name \""
                             + StringRef(RegName)  + "\"."));

  }

  if (!Subtarget->hasFlatScrRegister() &&
       Subtarget->getRegisterInfo()->regsOverlap(Reg, AMDGPU::FLAT_SCR)) {
    report_fatal_error(Twine("invalid register \""
                             + StringRef(RegName)  + "\" for subtarget."));
  }

  switch (Reg) {
  case AMDGPU::M0:
  case AMDGPU::EXEC_LO:
  case AMDGPU::EXEC_HI:
  case AMDGPU::FLAT_SCR_LO:
  case AMDGPU::FLAT_SCR_HI:
    if (VT.getSizeInBits() == 32)
      return Reg;
    break;
  case AMDGPU::EXEC:
  case AMDGPU::FLAT_SCR:
    if (VT.getSizeInBits() == 64)
      return Reg;
    break;
  default:
    llvm_unreachable("missing register type checking");
  }

  report_fatal_error(Twine("invalid type for register \""
                           + StringRef(RegName) + "\"."));
}

// If kill is not the last instruction, split the block so kill is always a
// proper terminator.
MachineBasicBlock *
SITargetLowering::splitKillBlock(MachineInstr &MI,
                                 MachineBasicBlock *BB) const {
  MachineBasicBlock *SplitBB = BB->splitAt(MI, false /*UpdateLiveIns*/);
  const SIInstrInfo *TII = getSubtarget()->getInstrInfo();
  MI.setDesc(TII->getKillTerminatorFromPseudo(MI.getOpcode()));
  return SplitBB;
}

// Split block \p MBB at \p MI, as to insert a loop. If \p InstInLoop is true,
// \p MI will be the only instruction in the loop body block. Otherwise, it will
// be the first instruction in the remainder block.
//
/// \returns { LoopBody, Remainder }
static std::pair<MachineBasicBlock *, MachineBasicBlock *>
splitBlockForLoop(MachineInstr &MI, MachineBasicBlock &MBB, bool InstInLoop) {
  MachineFunction *MF = MBB.getParent();
  MachineBasicBlock::iterator I(&MI);

  // To insert the loop we need to split the block. Move everything after this
  // point to a new block, and insert a new empty block between the two.
  MachineBasicBlock *LoopBB = MF->CreateMachineBasicBlock();
  MachineBasicBlock *RemainderBB = MF->CreateMachineBasicBlock();
  MachineFunction::iterator MBBI(MBB);
  ++MBBI;

  MF->insert(MBBI, LoopBB);
  MF->insert(MBBI, RemainderBB);

  LoopBB->addSuccessor(LoopBB);
  LoopBB->addSuccessor(RemainderBB);

  // Move the rest of the block into a new block.
  RemainderBB->transferSuccessorsAndUpdatePHIs(&MBB);

  if (InstInLoop) {
    auto Next = std::next(I);

    // Move instruction to loop body.
    LoopBB->splice(LoopBB->begin(), &MBB, I, Next);

    // Move the rest of the block.
    RemainderBB->splice(RemainderBB->begin(), &MBB, Next, MBB.end());
  } else {
    RemainderBB->splice(RemainderBB->begin(), &MBB, I, MBB.end());
  }

  MBB.addSuccessor(LoopBB);

  return std::pair(LoopBB, RemainderBB);
}

/// Insert \p MI into a BUNDLE with an S_WAITCNT 0 immediately following it.
void SITargetLowering::bundleInstWithWaitcnt(MachineInstr &MI) const {
  MachineBasicBlock *MBB = MI.getParent();
  const SIInstrInfo *TII = getSubtarget()->getInstrInfo();
  auto I = MI.getIterator();
  auto E = std::next(I);

  BuildMI(*MBB, E, MI.getDebugLoc(), TII->get(AMDGPU::S_WAITCNT))
    .addImm(0);

  MIBundleBuilder Bundler(*MBB, I, E);
  finalizeBundle(*MBB, Bundler.begin());
}

MachineBasicBlock *
SITargetLowering::emitGWSMemViolTestLoop(MachineInstr &MI,
                                         MachineBasicBlock *BB) const {
  const DebugLoc &DL = MI.getDebugLoc();

  MachineRegisterInfo &MRI = BB->getParent()->getRegInfo();

  MachineBasicBlock *LoopBB;
  MachineBasicBlock *RemainderBB;
  const SIInstrInfo *TII = getSubtarget()->getInstrInfo();

  // Apparently kill flags are only valid if the def is in the same block?
  if (MachineOperand *Src = TII->getNamedOperand(MI, AMDGPU::OpName::data0))
    Src->setIsKill(false);

  std::tie(LoopBB, RemainderBB) = splitBlockForLoop(MI, *BB, true);

  MachineBasicBlock::iterator I = LoopBB->end();

  const unsigned EncodedReg = AMDGPU::Hwreg::HwregEncoding::encode(
      AMDGPU::Hwreg::ID_TRAPSTS, AMDGPU::Hwreg::OFFSET_MEM_VIOL, 1);

  // Clear TRAP_STS.MEM_VIOL
  BuildMI(*LoopBB, LoopBB->begin(), DL, TII->get(AMDGPU::S_SETREG_IMM32_B32))
    .addImm(0)
    .addImm(EncodedReg);

  bundleInstWithWaitcnt(MI);

  Register Reg = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);

  // Load and check TRAP_STS.MEM_VIOL
  BuildMI(*LoopBB, I, DL, TII->get(AMDGPU::S_GETREG_B32), Reg)
    .addImm(EncodedReg);

  // FIXME: Do we need to use an isel pseudo that may clobber scc?
  BuildMI(*LoopBB, I, DL, TII->get(AMDGPU::S_CMP_LG_U32))
    .addReg(Reg, RegState::Kill)
    .addImm(0);
  BuildMI(*LoopBB, I, DL, TII->get(AMDGPU::S_CBRANCH_SCC1))
    .addMBB(LoopBB);

  return RemainderBB;
}

// Do a v_movrels_b32 or v_movreld_b32 for each unique value of \p IdxReg in the
// wavefront. If the value is uniform and just happens to be in a VGPR, this
// will only do one iteration. In the worst case, this will loop 64 times.
//
// TODO: Just use v_readlane_b32 if we know the VGPR has a uniform value.
static MachineBasicBlock::iterator
emitLoadM0FromVGPRLoop(const SIInstrInfo *TII, MachineRegisterInfo &MRI,
                       MachineBasicBlock &OrigBB, MachineBasicBlock &LoopBB,
                       const DebugLoc &DL, const MachineOperand &Idx,
                       unsigned InitReg, unsigned ResultReg, unsigned PhiReg,
                       unsigned InitSaveExecReg, int Offset, bool UseGPRIdxMode,
                       Register &SGPRIdxReg) {

  MachineFunction *MF = OrigBB.getParent();
  const GCNSubtarget &ST = MF->getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  MachineBasicBlock::iterator I = LoopBB.begin();

  const TargetRegisterClass *BoolRC = TRI->getBoolRC();
  Register PhiExec = MRI.createVirtualRegister(BoolRC);
  Register NewExec = MRI.createVirtualRegister(BoolRC);
  Register CurrentIdxReg = MRI.createVirtualRegister(&AMDGPU::SGPR_32RegClass);
  Register CondReg = MRI.createVirtualRegister(BoolRC);

  BuildMI(LoopBB, I, DL, TII->get(TargetOpcode::PHI), PhiReg)
    .addReg(InitReg)
    .addMBB(&OrigBB)
    .addReg(ResultReg)
    .addMBB(&LoopBB);

  BuildMI(LoopBB, I, DL, TII->get(TargetOpcode::PHI), PhiExec)
    .addReg(InitSaveExecReg)
    .addMBB(&OrigBB)
    .addReg(NewExec)
    .addMBB(&LoopBB);

  // Read the next variant <- also loop target.
  BuildMI(LoopBB, I, DL, TII->get(AMDGPU::V_READFIRSTLANE_B32), CurrentIdxReg)
      .addReg(Idx.getReg(), getUndefRegState(Idx.isUndef()));

  // Compare the just read M0 value to all possible Idx values.
  BuildMI(LoopBB, I, DL, TII->get(AMDGPU::V_CMP_EQ_U32_e64), CondReg)
      .addReg(CurrentIdxReg)
      .addReg(Idx.getReg(), 0, Idx.getSubReg());

  // Update EXEC, save the original EXEC value to VCC.
  BuildMI(LoopBB, I, DL, TII->get(ST.isWave32() ? AMDGPU::S_AND_SAVEEXEC_B32
                                                : AMDGPU::S_AND_SAVEEXEC_B64),
          NewExec)
    .addReg(CondReg, RegState::Kill);

  MRI.setSimpleHint(NewExec, CondReg);

  if (UseGPRIdxMode) {
    if (Offset == 0) {
      SGPRIdxReg = CurrentIdxReg;
    } else {
      SGPRIdxReg = MRI.createVirtualRegister(&AMDGPU::SGPR_32RegClass);
      BuildMI(LoopBB, I, DL, TII->get(AMDGPU::S_ADD_I32), SGPRIdxReg)
          .addReg(CurrentIdxReg, RegState::Kill)
          .addImm(Offset);
    }
  } else {
    // Move index from VCC into M0
    if (Offset == 0) {
      BuildMI(LoopBB, I, DL, TII->get(AMDGPU::S_MOV_B32), AMDGPU::M0)
        .addReg(CurrentIdxReg, RegState::Kill);
    } else {
      BuildMI(LoopBB, I, DL, TII->get(AMDGPU::S_ADD_I32), AMDGPU::M0)
        .addReg(CurrentIdxReg, RegState::Kill)
        .addImm(Offset);
    }
  }

  // Update EXEC, switch all done bits to 0 and all todo bits to 1.
  unsigned Exec = ST.isWave32() ? AMDGPU::EXEC_LO : AMDGPU::EXEC;
  MachineInstr *InsertPt =
    BuildMI(LoopBB, I, DL, TII->get(ST.isWave32() ? AMDGPU::S_XOR_B32_term
                                                  : AMDGPU::S_XOR_B64_term), Exec)
      .addReg(Exec)
      .addReg(NewExec);

  // XXX - s_xor_b64 sets scc to 1 if the result is nonzero, so can we use
  // s_cbranch_scc0?

  // Loop back to V_READFIRSTLANE_B32 if there are still variants to cover.
  BuildMI(LoopBB, I, DL, TII->get(AMDGPU::S_CBRANCH_EXECNZ))
    .addMBB(&LoopBB);

  return InsertPt->getIterator();
}

// This has slightly sub-optimal regalloc when the source vector is killed by
// the read. The register allocator does not understand that the kill is
// per-workitem, so is kept alive for the whole loop so we end up not re-using a
// subregister from it, using 1 more VGPR than necessary. This was saved when
// this was expanded after register allocation.
static MachineBasicBlock::iterator
loadM0FromVGPR(const SIInstrInfo *TII, MachineBasicBlock &MBB, MachineInstr &MI,
               unsigned InitResultReg, unsigned PhiReg, int Offset,
               bool UseGPRIdxMode, Register &SGPRIdxReg) {
  MachineFunction *MF = MBB.getParent();
  const GCNSubtarget &ST = MF->getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  const DebugLoc &DL = MI.getDebugLoc();
  MachineBasicBlock::iterator I(&MI);

  const auto *BoolXExecRC = TRI->getRegClass(AMDGPU::SReg_1_XEXECRegClassID);
  Register DstReg = MI.getOperand(0).getReg();
  Register SaveExec = MRI.createVirtualRegister(BoolXExecRC);
  Register TmpExec = MRI.createVirtualRegister(BoolXExecRC);
  unsigned Exec = ST.isWave32() ? AMDGPU::EXEC_LO : AMDGPU::EXEC;
  unsigned MovExecOpc = ST.isWave32() ? AMDGPU::S_MOV_B32 : AMDGPU::S_MOV_B64;

  BuildMI(MBB, I, DL, TII->get(TargetOpcode::IMPLICIT_DEF), TmpExec);

  // Save the EXEC mask
  BuildMI(MBB, I, DL, TII->get(MovExecOpc), SaveExec)
    .addReg(Exec);

  MachineBasicBlock *LoopBB;
  MachineBasicBlock *RemainderBB;
  std::tie(LoopBB, RemainderBB) = splitBlockForLoop(MI, MBB, false);

  const MachineOperand *Idx = TII->getNamedOperand(MI, AMDGPU::OpName::idx);

  auto InsPt = emitLoadM0FromVGPRLoop(TII, MRI, MBB, *LoopBB, DL, *Idx,
                                      InitResultReg, DstReg, PhiReg, TmpExec,
                                      Offset, UseGPRIdxMode, SGPRIdxReg);

  MachineBasicBlock* LandingPad = MF->CreateMachineBasicBlock();
  MachineFunction::iterator MBBI(LoopBB);
  ++MBBI;
  MF->insert(MBBI, LandingPad);
  LoopBB->removeSuccessor(RemainderBB);
  LandingPad->addSuccessor(RemainderBB);
  LoopBB->addSuccessor(LandingPad);
  MachineBasicBlock::iterator First = LandingPad->begin();
  BuildMI(*LandingPad, First, DL, TII->get(MovExecOpc), Exec)
    .addReg(SaveExec);

  return InsPt;
}

// Returns subreg index, offset
static std::pair<unsigned, int>
computeIndirectRegAndOffset(const SIRegisterInfo &TRI,
                            const TargetRegisterClass *SuperRC,
                            unsigned VecReg,
                            int Offset) {
  int NumElts = TRI.getRegSizeInBits(*SuperRC) / 32;

  // Skip out of bounds offsets, or else we would end up using an undefined
  // register.
  if (Offset >= NumElts || Offset < 0)
    return std::pair(AMDGPU::sub0, Offset);

  return std::pair(SIRegisterInfo::getSubRegFromChannel(Offset), 0);
}

static void setM0ToIndexFromSGPR(const SIInstrInfo *TII,
                                 MachineRegisterInfo &MRI, MachineInstr &MI,
                                 int Offset) {
  MachineBasicBlock *MBB = MI.getParent();
  const DebugLoc &DL = MI.getDebugLoc();
  MachineBasicBlock::iterator I(&MI);

  const MachineOperand *Idx = TII->getNamedOperand(MI, AMDGPU::OpName::idx);

  assert(Idx->getReg() != AMDGPU::NoRegister);

  if (Offset == 0) {
    BuildMI(*MBB, I, DL, TII->get(AMDGPU::S_MOV_B32), AMDGPU::M0).add(*Idx);
  } else {
    BuildMI(*MBB, I, DL, TII->get(AMDGPU::S_ADD_I32), AMDGPU::M0)
        .add(*Idx)
        .addImm(Offset);
  }
}

static Register getIndirectSGPRIdx(const SIInstrInfo *TII,
                                   MachineRegisterInfo &MRI, MachineInstr &MI,
                                   int Offset) {
  MachineBasicBlock *MBB = MI.getParent();
  const DebugLoc &DL = MI.getDebugLoc();
  MachineBasicBlock::iterator I(&MI);

  const MachineOperand *Idx = TII->getNamedOperand(MI, AMDGPU::OpName::idx);

  if (Offset == 0)
    return Idx->getReg();

  Register Tmp = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);
  BuildMI(*MBB, I, DL, TII->get(AMDGPU::S_ADD_I32), Tmp)
      .add(*Idx)
      .addImm(Offset);
  return Tmp;
}

static MachineBasicBlock *emitIndirectSrc(MachineInstr &MI,
                                          MachineBasicBlock &MBB,
                                          const GCNSubtarget &ST) {
  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo &TRI = TII->getRegisterInfo();
  MachineFunction *MF = MBB.getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();

  Register Dst = MI.getOperand(0).getReg();
  const MachineOperand *Idx = TII->getNamedOperand(MI, AMDGPU::OpName::idx);
  Register SrcReg = TII->getNamedOperand(MI, AMDGPU::OpName::src)->getReg();
  int Offset = TII->getNamedOperand(MI, AMDGPU::OpName::offset)->getImm();

  const TargetRegisterClass *VecRC = MRI.getRegClass(SrcReg);
  const TargetRegisterClass *IdxRC = MRI.getRegClass(Idx->getReg());

  unsigned SubReg;
  std::tie(SubReg, Offset)
    = computeIndirectRegAndOffset(TRI, VecRC, SrcReg, Offset);

  const bool UseGPRIdxMode = ST.useVGPRIndexMode();

  // Check for a SGPR index.
  if (TII->getRegisterInfo().isSGPRClass(IdxRC)) {
    MachineBasicBlock::iterator I(&MI);
    const DebugLoc &DL = MI.getDebugLoc();

    if (UseGPRIdxMode) {
      // TODO: Look at the uses to avoid the copy. This may require rescheduling
      // to avoid interfering with other uses, so probably requires a new
      // optimization pass.
      Register Idx = getIndirectSGPRIdx(TII, MRI, MI, Offset);

      const MCInstrDesc &GPRIDXDesc =
          TII->getIndirectGPRIDXPseudo(TRI.getRegSizeInBits(*VecRC), true);
      BuildMI(MBB, I, DL, GPRIDXDesc, Dst)
          .addReg(SrcReg)
          .addReg(Idx)
          .addImm(SubReg);
    } else {
      setM0ToIndexFromSGPR(TII, MRI, MI, Offset);

      BuildMI(MBB, I, DL, TII->get(AMDGPU::V_MOVRELS_B32_e32), Dst)
        .addReg(SrcReg, 0, SubReg)
        .addReg(SrcReg, RegState::Implicit);
    }

    MI.eraseFromParent();

    return &MBB;
  }

  // Control flow needs to be inserted if indexing with a VGPR.
  const DebugLoc &DL = MI.getDebugLoc();
  MachineBasicBlock::iterator I(&MI);

  Register PhiReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
  Register InitReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);

  BuildMI(MBB, I, DL, TII->get(TargetOpcode::IMPLICIT_DEF), InitReg);

  Register SGPRIdxReg;
  auto InsPt = loadM0FromVGPR(TII, MBB, MI, InitReg, PhiReg, Offset,
                              UseGPRIdxMode, SGPRIdxReg);

  MachineBasicBlock *LoopBB = InsPt->getParent();

  if (UseGPRIdxMode) {
    const MCInstrDesc &GPRIDXDesc =
        TII->getIndirectGPRIDXPseudo(TRI.getRegSizeInBits(*VecRC), true);

    BuildMI(*LoopBB, InsPt, DL, GPRIDXDesc, Dst)
        .addReg(SrcReg)
        .addReg(SGPRIdxReg)
        .addImm(SubReg);
  } else {
    BuildMI(*LoopBB, InsPt, DL, TII->get(AMDGPU::V_MOVRELS_B32_e32), Dst)
      .addReg(SrcReg, 0, SubReg)
      .addReg(SrcReg, RegState::Implicit);
  }

  MI.eraseFromParent();

  return LoopBB;
}

static MachineBasicBlock *emitIndirectDst(MachineInstr &MI,
                                          MachineBasicBlock &MBB,
                                          const GCNSubtarget &ST) {
  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo &TRI = TII->getRegisterInfo();
  MachineFunction *MF = MBB.getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();

  Register Dst = MI.getOperand(0).getReg();
  const MachineOperand *SrcVec = TII->getNamedOperand(MI, AMDGPU::OpName::src);
  const MachineOperand *Idx = TII->getNamedOperand(MI, AMDGPU::OpName::idx);
  const MachineOperand *Val = TII->getNamedOperand(MI, AMDGPU::OpName::val);
  int Offset = TII->getNamedOperand(MI, AMDGPU::OpName::offset)->getImm();
  const TargetRegisterClass *VecRC = MRI.getRegClass(SrcVec->getReg());
  const TargetRegisterClass *IdxRC = MRI.getRegClass(Idx->getReg());

  // This can be an immediate, but will be folded later.
  assert(Val->getReg());

  unsigned SubReg;
  std::tie(SubReg, Offset) = computeIndirectRegAndOffset(TRI, VecRC,
                                                         SrcVec->getReg(),
                                                         Offset);
  const bool UseGPRIdxMode = ST.useVGPRIndexMode();

  if (Idx->getReg() == AMDGPU::NoRegister) {
    MachineBasicBlock::iterator I(&MI);
    const DebugLoc &DL = MI.getDebugLoc();

    assert(Offset == 0);

    BuildMI(MBB, I, DL, TII->get(TargetOpcode::INSERT_SUBREG), Dst)
        .add(*SrcVec)
        .add(*Val)
        .addImm(SubReg);

    MI.eraseFromParent();
    return &MBB;
  }

  // Check for a SGPR index.
  if (TII->getRegisterInfo().isSGPRClass(IdxRC)) {
    MachineBasicBlock::iterator I(&MI);
    const DebugLoc &DL = MI.getDebugLoc();

    if (UseGPRIdxMode) {
      Register Idx = getIndirectSGPRIdx(TII, MRI, MI, Offset);

      const MCInstrDesc &GPRIDXDesc =
          TII->getIndirectGPRIDXPseudo(TRI.getRegSizeInBits(*VecRC), false);
      BuildMI(MBB, I, DL, GPRIDXDesc, Dst)
          .addReg(SrcVec->getReg())
          .add(*Val)
          .addReg(Idx)
          .addImm(SubReg);
    } else {
      setM0ToIndexFromSGPR(TII, MRI, MI, Offset);

      const MCInstrDesc &MovRelDesc = TII->getIndirectRegWriteMovRelPseudo(
          TRI.getRegSizeInBits(*VecRC), 32, false);
      BuildMI(MBB, I, DL, MovRelDesc, Dst)
          .addReg(SrcVec->getReg())
          .add(*Val)
          .addImm(SubReg);
    }
    MI.eraseFromParent();
    return &MBB;
  }

  // Control flow needs to be inserted if indexing with a VGPR.
  if (Val->isReg())
    MRI.clearKillFlags(Val->getReg());

  const DebugLoc &DL = MI.getDebugLoc();

  Register PhiReg = MRI.createVirtualRegister(VecRC);

  Register SGPRIdxReg;
  auto InsPt = loadM0FromVGPR(TII, MBB, MI, SrcVec->getReg(), PhiReg, Offset,
                              UseGPRIdxMode, SGPRIdxReg);
  MachineBasicBlock *LoopBB = InsPt->getParent();

  if (UseGPRIdxMode) {
    const MCInstrDesc &GPRIDXDesc =
        TII->getIndirectGPRIDXPseudo(TRI.getRegSizeInBits(*VecRC), false);

    BuildMI(*LoopBB, InsPt, DL, GPRIDXDesc, Dst)
        .addReg(PhiReg)
        .add(*Val)
        .addReg(SGPRIdxReg)
        .addImm(SubReg);
  } else {
    const MCInstrDesc &MovRelDesc = TII->getIndirectRegWriteMovRelPseudo(
        TRI.getRegSizeInBits(*VecRC), 32, false);
    BuildMI(*LoopBB, InsPt, DL, MovRelDesc, Dst)
        .addReg(PhiReg)
        .add(*Val)
        .addImm(SubReg);
  }

  MI.eraseFromParent();
  return LoopBB;
}

static MachineBasicBlock *lowerWaveReduce(MachineInstr &MI,
                                          MachineBasicBlock &BB,
                                          const GCNSubtarget &ST,
                                          unsigned Opc) {
  MachineRegisterInfo &MRI = BB.getParent()->getRegInfo();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  const DebugLoc &DL = MI.getDebugLoc();
  const SIInstrInfo *TII = ST.getInstrInfo();

  // Reduction operations depend on whether the input operand is SGPR or VGPR.
  Register SrcReg = MI.getOperand(1).getReg();
  bool isSGPR = TRI->isSGPRClass(MRI.getRegClass(SrcReg));
  Register DstReg = MI.getOperand(0).getReg();
  MachineBasicBlock *RetBB = nullptr;
  if (isSGPR) {
    // These operations with a uniform value i.e. SGPR are idempotent.
    // Reduced value will be same as given sgpr.
    BuildMI(BB, MI, DL, TII->get(AMDGPU::S_MOV_B32), DstReg).addReg(SrcReg);
    RetBB = &BB;
  } else {
    // TODO: Implement DPP Strategy and switch based on immediate strategy
    // operand. For now, for all the cases (default, Iterative and DPP we use
    // iterative approach by default.)

    // To reduce the VGPR using iterative approach, we need to iterate
    // over all the active lanes. Lowering consists of ComputeLoop,
    // which iterate over only active lanes. We use copy of EXEC register
    // as induction variable and every active lane modifies it using bitset0
    // so that we will get the next active lane for next iteration.
    MachineBasicBlock::iterator I = BB.end();
    Register SrcReg = MI.getOperand(1).getReg();

    // Create Control flow for loop
    // Split MI's Machine Basic block into For loop
    auto [ComputeLoop, ComputeEnd] = splitBlockForLoop(MI, BB, true);

    // Create virtual registers required for lowering.
    const TargetRegisterClass *WaveMaskRegClass = TRI->getWaveMaskRegClass();
    const TargetRegisterClass *DstRegClass = MRI.getRegClass(DstReg);
    Register LoopIterator = MRI.createVirtualRegister(WaveMaskRegClass);
    Register InitalValReg = MRI.createVirtualRegister(DstRegClass);

    Register AccumulatorReg = MRI.createVirtualRegister(DstRegClass);
    Register ActiveBitsReg = MRI.createVirtualRegister(WaveMaskRegClass);
    Register NewActiveBitsReg = MRI.createVirtualRegister(WaveMaskRegClass);

    Register FF1Reg = MRI.createVirtualRegister(DstRegClass);
    Register LaneValueReg = MRI.createVirtualRegister(DstRegClass);

    bool IsWave32 = ST.isWave32();
    unsigned MovOpc = IsWave32 ? AMDGPU::S_MOV_B32 : AMDGPU::S_MOV_B64;
    unsigned ExecReg = IsWave32 ? AMDGPU::EXEC_LO : AMDGPU::EXEC;

    // Create initail values of induction variable from Exec, Accumulator and
    // insert branch instr to newly created ComputeBlockk
    uint32_t InitalValue =
        (Opc == AMDGPU::S_MIN_U32) ? std::numeric_limits<uint32_t>::max() : 0;
    auto TmpSReg =
        BuildMI(BB, I, DL, TII->get(MovOpc), LoopIterator).addReg(ExecReg);
    BuildMI(BB, I, DL, TII->get(AMDGPU::S_MOV_B32), InitalValReg)
        .addImm(InitalValue);
    BuildMI(BB, I, DL, TII->get(AMDGPU::S_BRANCH)).addMBB(ComputeLoop);

    // Start constructing ComputeLoop
    I = ComputeLoop->end();
    auto Accumulator =
        BuildMI(*ComputeLoop, I, DL, TII->get(AMDGPU::PHI), AccumulatorReg)
            .addReg(InitalValReg)
            .addMBB(&BB);
    auto ActiveBits =
        BuildMI(*ComputeLoop, I, DL, TII->get(AMDGPU::PHI), ActiveBitsReg)
            .addReg(TmpSReg->getOperand(0).getReg())
            .addMBB(&BB);

    // Perform the computations
    unsigned SFFOpc = IsWave32 ? AMDGPU::S_FF1_I32_B32 : AMDGPU::S_FF1_I32_B64;
    auto FF1 = BuildMI(*ComputeLoop, I, DL, TII->get(SFFOpc), FF1Reg)
                   .addReg(ActiveBits->getOperand(0).getReg());
    auto LaneValue = BuildMI(*ComputeLoop, I, DL,
                             TII->get(AMDGPU::V_READLANE_B32), LaneValueReg)
                         .addReg(SrcReg)
                         .addReg(FF1->getOperand(0).getReg());
    auto NewAccumulator = BuildMI(*ComputeLoop, I, DL, TII->get(Opc), DstReg)
                              .addReg(Accumulator->getOperand(0).getReg())
                              .addReg(LaneValue->getOperand(0).getReg());

    // Manipulate the iterator to get the next active lane
    unsigned BITSETOpc =
        IsWave32 ? AMDGPU::S_BITSET0_B32 : AMDGPU::S_BITSET0_B64;
    auto NewActiveBits =
        BuildMI(*ComputeLoop, I, DL, TII->get(BITSETOpc), NewActiveBitsReg)
            .addReg(FF1->getOperand(0).getReg())
            .addReg(ActiveBits->getOperand(0).getReg());

    // Add phi nodes
    Accumulator.addReg(NewAccumulator->getOperand(0).getReg())
        .addMBB(ComputeLoop);
    ActiveBits.addReg(NewActiveBits->getOperand(0).getReg())
        .addMBB(ComputeLoop);

    // Creating branching
    unsigned CMPOpc = IsWave32 ? AMDGPU::S_CMP_LG_U32 : AMDGPU::S_CMP_LG_U64;
    BuildMI(*ComputeLoop, I, DL, TII->get(CMPOpc))
        .addReg(NewActiveBits->getOperand(0).getReg())
        .addImm(0);
    BuildMI(*ComputeLoop, I, DL, TII->get(AMDGPU::S_CBRANCH_SCC1))
        .addMBB(ComputeLoop);

    RetBB = ComputeEnd;
  }
  MI.eraseFromParent();
  return RetBB;
}

MachineBasicBlock *SITargetLowering::EmitInstrWithCustomInserter(
  MachineInstr &MI, MachineBasicBlock *BB) const {

  const SIInstrInfo *TII = getSubtarget()->getInstrInfo();
  MachineFunction *MF = BB->getParent();
  SIMachineFunctionInfo *MFI = MF->getInfo<SIMachineFunctionInfo>();

  switch (MI.getOpcode()) {
  case AMDGPU::WAVE_REDUCE_UMIN_PSEUDO_U32:
    return lowerWaveReduce(MI, *BB, *getSubtarget(), AMDGPU::S_MIN_U32);
  case AMDGPU::WAVE_REDUCE_UMAX_PSEUDO_U32:
    return lowerWaveReduce(MI, *BB, *getSubtarget(), AMDGPU::S_MAX_U32);
  case AMDGPU::S_UADDO_PSEUDO:
  case AMDGPU::S_USUBO_PSEUDO: {
    const DebugLoc &DL = MI.getDebugLoc();
    MachineOperand &Dest0 = MI.getOperand(0);
    MachineOperand &Dest1 = MI.getOperand(1);
    MachineOperand &Src0 = MI.getOperand(2);
    MachineOperand &Src1 = MI.getOperand(3);

    unsigned Opc = (MI.getOpcode() == AMDGPU::S_UADDO_PSEUDO)
                       ? AMDGPU::S_ADD_I32
                       : AMDGPU::S_SUB_I32;
    BuildMI(*BB, MI, DL, TII->get(Opc), Dest0.getReg()).add(Src0).add(Src1);

    BuildMI(*BB, MI, DL, TII->get(AMDGPU::S_CSELECT_B64), Dest1.getReg())
        .addImm(1)
        .addImm(0);

    MI.eraseFromParent();
    return BB;
  }
  case AMDGPU::S_ADD_U64_PSEUDO:
  case AMDGPU::S_SUB_U64_PSEUDO: {
    // For targets older than GFX12, we emit a sequence of 32-bit operations.
    // For GFX12, we emit s_add_u64 and s_sub_u64.
    const GCNSubtarget &ST = MF->getSubtarget<GCNSubtarget>();
    MachineRegisterInfo &MRI = BB->getParent()->getRegInfo();
    const DebugLoc &DL = MI.getDebugLoc();
    MachineOperand &Dest = MI.getOperand(0);
    MachineOperand &Src0 = MI.getOperand(1);
    MachineOperand &Src1 = MI.getOperand(2);
    bool IsAdd = (MI.getOpcode() == AMDGPU::S_ADD_U64_PSEUDO);
    if (Subtarget->hasScalarAddSub64()) {
      unsigned Opc = IsAdd ? AMDGPU::S_ADD_U64 : AMDGPU::S_SUB_U64;
      BuildMI(*BB, MI, DL, TII->get(Opc), Dest.getReg())
        .add(Src0)
        .add(Src1);
    } else {
      const SIRegisterInfo *TRI = ST.getRegisterInfo();
      const TargetRegisterClass *BoolRC = TRI->getBoolRC();

      Register DestSub0 = MRI.createVirtualRegister(&AMDGPU::SReg_32RegClass);
      Register DestSub1 = MRI.createVirtualRegister(&AMDGPU::SReg_32RegClass);

      MachineOperand Src0Sub0 = TII->buildExtractSubRegOrImm(
          MI, MRI, Src0, BoolRC, AMDGPU::sub0, &AMDGPU::SReg_32RegClass);
      MachineOperand Src0Sub1 = TII->buildExtractSubRegOrImm(
          MI, MRI, Src0, BoolRC, AMDGPU::sub1, &AMDGPU::SReg_32RegClass);

      MachineOperand Src1Sub0 = TII->buildExtractSubRegOrImm(
          MI, MRI, Src1, BoolRC, AMDGPU::sub0, &AMDGPU::SReg_32RegClass);
      MachineOperand Src1Sub1 = TII->buildExtractSubRegOrImm(
          MI, MRI, Src1, BoolRC, AMDGPU::sub1, &AMDGPU::SReg_32RegClass);

      unsigned LoOpc = IsAdd ? AMDGPU::S_ADD_U32 : AMDGPU::S_SUB_U32;
      unsigned HiOpc = IsAdd ? AMDGPU::S_ADDC_U32 : AMDGPU::S_SUBB_U32;
      BuildMI(*BB, MI, DL, TII->get(LoOpc), DestSub0)
          .add(Src0Sub0)
          .add(Src1Sub0);
      BuildMI(*BB, MI, DL, TII->get(HiOpc), DestSub1)
          .add(Src0Sub1)
          .add(Src1Sub1);
      BuildMI(*BB, MI, DL, TII->get(TargetOpcode::REG_SEQUENCE), Dest.getReg())
          .addReg(DestSub0)
          .addImm(AMDGPU::sub0)
          .addReg(DestSub1)
          .addImm(AMDGPU::sub1);
    }
    MI.eraseFromParent();
    return BB;
  }
  case AMDGPU::V_ADD_U64_PSEUDO:
  case AMDGPU::V_SUB_U64_PSEUDO: {
    MachineRegisterInfo &MRI = BB->getParent()->getRegInfo();
    const GCNSubtarget &ST = MF->getSubtarget<GCNSubtarget>();
    const SIRegisterInfo *TRI = ST.getRegisterInfo();
    const DebugLoc &DL = MI.getDebugLoc();

    bool IsAdd = (MI.getOpcode() == AMDGPU::V_ADD_U64_PSEUDO);

    MachineOperand &Dest = MI.getOperand(0);
    MachineOperand &Src0 = MI.getOperand(1);
    MachineOperand &Src1 = MI.getOperand(2);

    if (IsAdd && ST.hasLshlAddB64()) {
      auto Add = BuildMI(*BB, MI, DL, TII->get(AMDGPU::V_LSHL_ADD_U64_e64),
                         Dest.getReg())
                     .add(Src0)
                     .addImm(0)
                     .add(Src1);
      TII->legalizeOperands(*Add);
      MI.eraseFromParent();
      return BB;
    }

    const auto *CarryRC = TRI->getRegClass(AMDGPU::SReg_1_XEXECRegClassID);

    Register DestSub0 = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    Register DestSub1 = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);

    Register CarryReg = MRI.createVirtualRegister(CarryRC);
    Register DeadCarryReg = MRI.createVirtualRegister(CarryRC);

    const TargetRegisterClass *Src0RC = Src0.isReg()
                                            ? MRI.getRegClass(Src0.getReg())
                                            : &AMDGPU::VReg_64RegClass;
    const TargetRegisterClass *Src1RC = Src1.isReg()
                                            ? MRI.getRegClass(Src1.getReg())
                                            : &AMDGPU::VReg_64RegClass;

    const TargetRegisterClass *Src0SubRC =
        TRI->getSubRegisterClass(Src0RC, AMDGPU::sub0);
    const TargetRegisterClass *Src1SubRC =
        TRI->getSubRegisterClass(Src1RC, AMDGPU::sub1);

    MachineOperand SrcReg0Sub0 = TII->buildExtractSubRegOrImm(
        MI, MRI, Src0, Src0RC, AMDGPU::sub0, Src0SubRC);
    MachineOperand SrcReg1Sub0 = TII->buildExtractSubRegOrImm(
        MI, MRI, Src1, Src1RC, AMDGPU::sub0, Src1SubRC);

    MachineOperand SrcReg0Sub1 = TII->buildExtractSubRegOrImm(
        MI, MRI, Src0, Src0RC, AMDGPU::sub1, Src0SubRC);
    MachineOperand SrcReg1Sub1 = TII->buildExtractSubRegOrImm(
        MI, MRI, Src1, Src1RC, AMDGPU::sub1, Src1SubRC);

    unsigned LoOpc = IsAdd ? AMDGPU::V_ADD_CO_U32_e64 : AMDGPU::V_SUB_CO_U32_e64;
    MachineInstr *LoHalf = BuildMI(*BB, MI, DL, TII->get(LoOpc), DestSub0)
                               .addReg(CarryReg, RegState::Define)
                               .add(SrcReg0Sub0)
                               .add(SrcReg1Sub0)
                               .addImm(0); // clamp bit

    unsigned HiOpc = IsAdd ? AMDGPU::V_ADDC_U32_e64 : AMDGPU::V_SUBB_U32_e64;
    MachineInstr *HiHalf =
        BuildMI(*BB, MI, DL, TII->get(HiOpc), DestSub1)
            .addReg(DeadCarryReg, RegState::Define | RegState::Dead)
            .add(SrcReg0Sub1)
            .add(SrcReg1Sub1)
            .addReg(CarryReg, RegState::Kill)
            .addImm(0); // clamp bit

    BuildMI(*BB, MI, DL, TII->get(TargetOpcode::REG_SEQUENCE), Dest.getReg())
        .addReg(DestSub0)
        .addImm(AMDGPU::sub0)
        .addReg(DestSub1)
        .addImm(AMDGPU::sub1);
    TII->legalizeOperands(*LoHalf);
    TII->legalizeOperands(*HiHalf);
    MI.eraseFromParent();
    return BB;
  }
  case AMDGPU::S_ADD_CO_PSEUDO:
  case AMDGPU::S_SUB_CO_PSEUDO: {
    // This pseudo has a chance to be selected
    // only from uniform add/subcarry node. All the VGPR operands
    // therefore assumed to be splat vectors.
    MachineRegisterInfo &MRI = BB->getParent()->getRegInfo();
    const GCNSubtarget &ST = MF->getSubtarget<GCNSubtarget>();
    const SIRegisterInfo *TRI = ST.getRegisterInfo();
    MachineBasicBlock::iterator MII = MI;
    const DebugLoc &DL = MI.getDebugLoc();
    MachineOperand &Dest = MI.getOperand(0);
    MachineOperand &CarryDest = MI.getOperand(1);
    MachineOperand &Src0 = MI.getOperand(2);
    MachineOperand &Src1 = MI.getOperand(3);
    MachineOperand &Src2 = MI.getOperand(4);
    unsigned Opc = (MI.getOpcode() == AMDGPU::S_ADD_CO_PSEUDO)
                       ? AMDGPU::S_ADDC_U32
                       : AMDGPU::S_SUBB_U32;
    if (Src0.isReg() && TRI->isVectorRegister(MRI, Src0.getReg())) {
      Register RegOp0 = MRI.createVirtualRegister(&AMDGPU::SReg_32RegClass);
      BuildMI(*BB, MII, DL, TII->get(AMDGPU::V_READFIRSTLANE_B32), RegOp0)
          .addReg(Src0.getReg());
      Src0.setReg(RegOp0);
    }
    if (Src1.isReg() && TRI->isVectorRegister(MRI, Src1.getReg())) {
      Register RegOp1 = MRI.createVirtualRegister(&AMDGPU::SReg_32RegClass);
      BuildMI(*BB, MII, DL, TII->get(AMDGPU::V_READFIRSTLANE_B32), RegOp1)
          .addReg(Src1.getReg());
      Src1.setReg(RegOp1);
    }
    Register RegOp2 = MRI.createVirtualRegister(&AMDGPU::SReg_32RegClass);
    if (TRI->isVectorRegister(MRI, Src2.getReg())) {
      BuildMI(*BB, MII, DL, TII->get(AMDGPU::V_READFIRSTLANE_B32), RegOp2)
          .addReg(Src2.getReg());
      Src2.setReg(RegOp2);
    }

    const TargetRegisterClass *Src2RC = MRI.getRegClass(Src2.getReg());
    unsigned WaveSize = TRI->getRegSizeInBits(*Src2RC);
    assert(WaveSize == 64 || WaveSize == 32);

    if (WaveSize == 64) {
      if (ST.hasScalarCompareEq64()) {
        BuildMI(*BB, MII, DL, TII->get(AMDGPU::S_CMP_LG_U64))
            .addReg(Src2.getReg())
            .addImm(0);
      } else {
        const TargetRegisterClass *SubRC =
            TRI->getSubRegisterClass(Src2RC, AMDGPU::sub0);
        MachineOperand Src2Sub0 = TII->buildExtractSubRegOrImm(
            MII, MRI, Src2, Src2RC, AMDGPU::sub0, SubRC);
        MachineOperand Src2Sub1 = TII->buildExtractSubRegOrImm(
            MII, MRI, Src2, Src2RC, AMDGPU::sub1, SubRC);
        Register Src2_32 = MRI.createVirtualRegister(&AMDGPU::SReg_32RegClass);

        BuildMI(*BB, MII, DL, TII->get(AMDGPU::S_OR_B32), Src2_32)
            .add(Src2Sub0)
            .add(Src2Sub1);

        BuildMI(*BB, MII, DL, TII->get(AMDGPU::S_CMP_LG_U32))
            .addReg(Src2_32, RegState::Kill)
            .addImm(0);
      }
    } else {
      BuildMI(*BB, MII, DL, TII->get(AMDGPU::S_CMP_LG_U32))
          .addReg(Src2.getReg())
          .addImm(0);
    }

    BuildMI(*BB, MII, DL, TII->get(Opc), Dest.getReg()).add(Src0).add(Src1);

    unsigned SelOpc =
        (WaveSize == 64) ? AMDGPU::S_CSELECT_B64 : AMDGPU::S_CSELECT_B32;

    BuildMI(*BB, MII, DL, TII->get(SelOpc), CarryDest.getReg())
        .addImm(-1)
        .addImm(0);

    MI.eraseFromParent();
    return BB;
  }
  case AMDGPU::SI_INIT_M0: {
    BuildMI(*BB, MI.getIterator(), MI.getDebugLoc(),
            TII->get(AMDGPU::S_MOV_B32), AMDGPU::M0)
        .add(MI.getOperand(0));
    MI.eraseFromParent();
    return BB;
  }
  case AMDGPU::GET_GROUPSTATICSIZE: {
    assert(getTargetMachine().getTargetTriple().getOS() == Triple::AMDHSA ||
           getTargetMachine().getTargetTriple().getOS() == Triple::AMDPAL);
    DebugLoc DL = MI.getDebugLoc();
    BuildMI(*BB, MI, DL, TII->get(AMDGPU::S_MOV_B32))
        .add(MI.getOperand(0))
        .addImm(MFI->getLDSSize());
    MI.eraseFromParent();
    return BB;
  }
  case AMDGPU::GET_SHADERCYCLESHILO: {
    assert(MF->getSubtarget<GCNSubtarget>().hasShaderCyclesHiLoRegisters());
    MachineRegisterInfo &MRI = MF->getRegInfo();
    const DebugLoc &DL = MI.getDebugLoc();
    // The algorithm is:
    //
    // hi1 = getreg(SHADER_CYCLES_HI)
    // lo1 = getreg(SHADER_CYCLES_LO)
    // hi2 = getreg(SHADER_CYCLES_HI)
    //
    // If hi1 == hi2 then there was no overflow and the result is hi2:lo1.
    // Otherwise there was overflow and the result is hi2:0. In both cases the
    // result should represent the actual time at some point during the sequence
    // of three getregs.
    using namespace AMDGPU::Hwreg;
    Register RegHi1 = MRI.createVirtualRegister(&AMDGPU::SReg_32RegClass);
    BuildMI(*BB, MI, DL, TII->get(AMDGPU::S_GETREG_B32), RegHi1)
        .addImm(HwregEncoding::encode(ID_SHADER_CYCLES_HI, 0, 32));
    Register RegLo1 = MRI.createVirtualRegister(&AMDGPU::SReg_32RegClass);
    BuildMI(*BB, MI, DL, TII->get(AMDGPU::S_GETREG_B32), RegLo1)
        .addImm(HwregEncoding::encode(ID_SHADER_CYCLES, 0, 32));
    Register RegHi2 = MRI.createVirtualRegister(&AMDGPU::SReg_32RegClass);
    BuildMI(*BB, MI, DL, TII->get(AMDGPU::S_GETREG_B32), RegHi2)
        .addImm(HwregEncoding::encode(ID_SHADER_CYCLES_HI, 0, 32));
    BuildMI(*BB, MI, DL, TII->get(AMDGPU::S_CMP_EQ_U32))
        .addReg(RegHi1)
        .addReg(RegHi2);
    Register RegLo = MRI.createVirtualRegister(&AMDGPU::SReg_32RegClass);
    BuildMI(*BB, MI, DL, TII->get(AMDGPU::S_CSELECT_B32), RegLo)
        .addReg(RegLo1)
        .addImm(0);
    BuildMI(*BB, MI, DL, TII->get(AMDGPU::REG_SEQUENCE))
        .add(MI.getOperand(0))
        .addReg(RegLo)
        .addImm(AMDGPU::sub0)
        .addReg(RegHi2)
        .addImm(AMDGPU::sub1);
    MI.eraseFromParent();
    return BB;
  }
  case AMDGPU::SI_INDIRECT_SRC_V1:
  case AMDGPU::SI_INDIRECT_SRC_V2:
  case AMDGPU::SI_INDIRECT_SRC_V4:
  case AMDGPU::SI_INDIRECT_SRC_V8:
  case AMDGPU::SI_INDIRECT_SRC_V9:
  case AMDGPU::SI_INDIRECT_SRC_V10:
  case AMDGPU::SI_INDIRECT_SRC_V11:
  case AMDGPU::SI_INDIRECT_SRC_V12:
  case AMDGPU::SI_INDIRECT_SRC_V16:
  case AMDGPU::SI_INDIRECT_SRC_V32:
    return emitIndirectSrc(MI, *BB, *getSubtarget());
  case AMDGPU::SI_INDIRECT_DST_V1:
  case AMDGPU::SI_INDIRECT_DST_V2:
  case AMDGPU::SI_INDIRECT_DST_V4:
  case AMDGPU::SI_INDIRECT_DST_V8:
  case AMDGPU::SI_INDIRECT_DST_V9:
  case AMDGPU::SI_INDIRECT_DST_V10:
  case AMDGPU::SI_INDIRECT_DST_V11:
  case AMDGPU::SI_INDIRECT_DST_V12:
  case AMDGPU::SI_INDIRECT_DST_V16:
  case AMDGPU::SI_INDIRECT_DST_V32:
    return emitIndirectDst(MI, *BB, *getSubtarget());
  case AMDGPU::SI_KILL_F32_COND_IMM_PSEUDO:
  case AMDGPU::SI_KILL_I1_PSEUDO:
    return splitKillBlock(MI, BB);
  case AMDGPU::V_CNDMASK_B64_PSEUDO: {
    MachineRegisterInfo &MRI = BB->getParent()->getRegInfo();
    const GCNSubtarget &ST = MF->getSubtarget<GCNSubtarget>();
    const SIRegisterInfo *TRI = ST.getRegisterInfo();

    Register Dst = MI.getOperand(0).getReg();
    const MachineOperand &Src0 = MI.getOperand(1);
    const MachineOperand &Src1 = MI.getOperand(2);
    const DebugLoc &DL = MI.getDebugLoc();
    Register SrcCond = MI.getOperand(3).getReg();

    Register DstLo = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    Register DstHi = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    const auto *CondRC = TRI->getRegClass(AMDGPU::SReg_1_XEXECRegClassID);
    Register SrcCondCopy = MRI.createVirtualRegister(CondRC);

    const TargetRegisterClass *Src0RC = Src0.isReg()
                                            ? MRI.getRegClass(Src0.getReg())
                                            : &AMDGPU::VReg_64RegClass;
    const TargetRegisterClass *Src1RC = Src1.isReg()
                                            ? MRI.getRegClass(Src1.getReg())
                                            : &AMDGPU::VReg_64RegClass;

    const TargetRegisterClass *Src0SubRC =
        TRI->getSubRegisterClass(Src0RC, AMDGPU::sub0);
    const TargetRegisterClass *Src1SubRC =
        TRI->getSubRegisterClass(Src1RC, AMDGPU::sub1);

    MachineOperand Src0Sub0 = TII->buildExtractSubRegOrImm(
        MI, MRI, Src0, Src0RC, AMDGPU::sub0, Src0SubRC);
    MachineOperand Src1Sub0 = TII->buildExtractSubRegOrImm(
        MI, MRI, Src1, Src1RC, AMDGPU::sub0, Src1SubRC);

    MachineOperand Src0Sub1 = TII->buildExtractSubRegOrImm(
        MI, MRI, Src0, Src0RC, AMDGPU::sub1, Src0SubRC);
    MachineOperand Src1Sub1 = TII->buildExtractSubRegOrImm(
        MI, MRI, Src1, Src1RC, AMDGPU::sub1, Src1SubRC);

    BuildMI(*BB, MI, DL, TII->get(AMDGPU::COPY), SrcCondCopy)
      .addReg(SrcCond);
    BuildMI(*BB, MI, DL, TII->get(AMDGPU::V_CNDMASK_B32_e64), DstLo)
        .addImm(0)
        .add(Src0Sub0)
        .addImm(0)
        .add(Src1Sub0)
        .addReg(SrcCondCopy);
    BuildMI(*BB, MI, DL, TII->get(AMDGPU::V_CNDMASK_B32_e64), DstHi)
        .addImm(0)
        .add(Src0Sub1)
        .addImm(0)
        .add(Src1Sub1)
        .addReg(SrcCondCopy);

    BuildMI(*BB, MI, DL, TII->get(AMDGPU::REG_SEQUENCE), Dst)
      .addReg(DstLo)
      .addImm(AMDGPU::sub0)
      .addReg(DstHi)
      .addImm(AMDGPU::sub1);
    MI.eraseFromParent();
    return BB;
  }
  case AMDGPU::SI_BR_UNDEF: {
    const SIInstrInfo *TII = getSubtarget()->getInstrInfo();
    const DebugLoc &DL = MI.getDebugLoc();
    MachineInstr *Br = BuildMI(*BB, MI, DL, TII->get(AMDGPU::S_CBRANCH_SCC1))
                           .add(MI.getOperand(0));
    Br->getOperand(1).setIsUndef(); // read undef SCC
    MI.eraseFromParent();
    return BB;
  }
  case AMDGPU::ADJCALLSTACKUP:
  case AMDGPU::ADJCALLSTACKDOWN: {
    const SIMachineFunctionInfo *Info = MF->getInfo<SIMachineFunctionInfo>();
    MachineInstrBuilder MIB(*MF, &MI);
    MIB.addReg(Info->getStackPtrOffsetReg(), RegState::ImplicitDefine)
       .addReg(Info->getStackPtrOffsetReg(), RegState::Implicit);
    return BB;
  }
  case AMDGPU::SI_CALL_ISEL: {
    const SIInstrInfo *TII = getSubtarget()->getInstrInfo();
    const DebugLoc &DL = MI.getDebugLoc();

    unsigned ReturnAddrReg = TII->getRegisterInfo().getReturnAddressReg(*MF);

    MachineInstrBuilder MIB;
    MIB = BuildMI(*BB, MI, DL, TII->get(AMDGPU::SI_CALL), ReturnAddrReg);

    for (const MachineOperand &MO : MI.operands())
      MIB.add(MO);

    MIB.cloneMemRefs(MI);
    MI.eraseFromParent();
    return BB;
  }
  case AMDGPU::V_ADD_CO_U32_e32:
  case AMDGPU::V_SUB_CO_U32_e32:
  case AMDGPU::V_SUBREV_CO_U32_e32: {
    // TODO: Define distinct V_*_I32_Pseudo instructions instead.
    const DebugLoc &DL = MI.getDebugLoc();
    unsigned Opc = MI.getOpcode();

    bool NeedClampOperand = false;
    if (TII->pseudoToMCOpcode(Opc) == -1) {
      Opc = AMDGPU::getVOPe64(Opc);
      NeedClampOperand = true;
    }

    auto I = BuildMI(*BB, MI, DL, TII->get(Opc), MI.getOperand(0).getReg());
    if (TII->isVOP3(*I)) {
      const GCNSubtarget &ST = MF->getSubtarget<GCNSubtarget>();
      const SIRegisterInfo *TRI = ST.getRegisterInfo();
      I.addReg(TRI->getVCC(), RegState::Define);
    }
    I.add(MI.getOperand(1))
     .add(MI.getOperand(2));
    if (NeedClampOperand)
      I.addImm(0); // clamp bit for e64 encoding

    TII->legalizeOperands(*I);

    MI.eraseFromParent();
    return BB;
  }
  case AMDGPU::V_ADDC_U32_e32:
  case AMDGPU::V_SUBB_U32_e32:
  case AMDGPU::V_SUBBREV_U32_e32:
    // These instructions have an implicit use of vcc which counts towards the
    // constant bus limit.
    TII->legalizeOperands(MI);
    return BB;
  case AMDGPU::DS_GWS_INIT:
  case AMDGPU::DS_GWS_SEMA_BR:
  case AMDGPU::DS_GWS_BARRIER:
    TII->enforceOperandRCAlignment(MI, AMDGPU::OpName::data0);
    [[fallthrough]];
  case AMDGPU::DS_GWS_SEMA_V:
  case AMDGPU::DS_GWS_SEMA_P:
  case AMDGPU::DS_GWS_SEMA_RELEASE_ALL:
    // A s_waitcnt 0 is required to be the instruction immediately following.
    if (getSubtarget()->hasGWSAutoReplay()) {
      bundleInstWithWaitcnt(MI);
      return BB;
    }

    return emitGWSMemViolTestLoop(MI, BB);
  case AMDGPU::S_SETREG_B32: {
    // Try to optimize cases that only set the denormal mode or rounding mode.
    //
    // If the s_setreg_b32 fully sets all of the bits in the rounding mode or
    // denormal mode to a constant, we can use s_round_mode or s_denorm_mode
    // instead.
    //
    // FIXME: This could be predicates on the immediate, but tablegen doesn't
    // allow you to have a no side effect instruction in the output of a
    // sideeffecting pattern.
    auto [ID, Offset, Width] =
        AMDGPU::Hwreg::HwregEncoding::decode(MI.getOperand(1).getImm());
    if (ID != AMDGPU::Hwreg::ID_MODE)
      return BB;

    const unsigned WidthMask = maskTrailingOnes<unsigned>(Width);
    const unsigned SetMask = WidthMask << Offset;

    if (getSubtarget()->hasDenormModeInst()) {
      unsigned SetDenormOp = 0;
      unsigned SetRoundOp = 0;

      // The dedicated instructions can only set the whole denorm or round mode
      // at once, not a subset of bits in either.
      if (SetMask ==
          (AMDGPU::Hwreg::FP_ROUND_MASK | AMDGPU::Hwreg::FP_DENORM_MASK)) {
        // If this fully sets both the round and denorm mode, emit the two
        // dedicated instructions for these.
        SetRoundOp = AMDGPU::S_ROUND_MODE;
        SetDenormOp = AMDGPU::S_DENORM_MODE;
      } else if (SetMask == AMDGPU::Hwreg::FP_ROUND_MASK) {
        SetRoundOp = AMDGPU::S_ROUND_MODE;
      } else if (SetMask == AMDGPU::Hwreg::FP_DENORM_MASK) {
        SetDenormOp = AMDGPU::S_DENORM_MODE;
      }

      if (SetRoundOp || SetDenormOp) {
        MachineRegisterInfo &MRI = BB->getParent()->getRegInfo();
        MachineInstr *Def = MRI.getVRegDef(MI.getOperand(0).getReg());
        if (Def && Def->isMoveImmediate() && Def->getOperand(1).isImm()) {
          unsigned ImmVal = Def->getOperand(1).getImm();
          if (SetRoundOp) {
            BuildMI(*BB, MI, MI.getDebugLoc(), TII->get(SetRoundOp))
                .addImm(ImmVal & 0xf);

            // If we also have the denorm mode, get just the denorm mode bits.
            ImmVal >>= 4;
          }

          if (SetDenormOp) {
            BuildMI(*BB, MI, MI.getDebugLoc(), TII->get(SetDenormOp))
                .addImm(ImmVal & 0xf);
          }

          MI.eraseFromParent();
          return BB;
        }
      }
    }

    // If only FP bits are touched, used the no side effects pseudo.
    if ((SetMask & (AMDGPU::Hwreg::FP_ROUND_MASK |
                    AMDGPU::Hwreg::FP_DENORM_MASK)) == SetMask)
      MI.setDesc(TII->get(AMDGPU::S_SETREG_B32_mode));

    return BB;
  }
  case AMDGPU::S_INVERSE_BALLOT_U32:
  case AMDGPU::S_INVERSE_BALLOT_U64:
    // These opcodes only exist to let SIFixSGPRCopies insert a readfirstlane if
    // necessary. After that they are equivalent to a COPY.
    MI.setDesc(TII->get(AMDGPU::COPY));
    return BB;
  case AMDGPU::ENDPGM_TRAP: {
    const DebugLoc &DL = MI.getDebugLoc();
    if (BB->succ_empty() && std::next(MI.getIterator()) == BB->end()) {
      MI.setDesc(TII->get(AMDGPU::S_ENDPGM));
      MI.addOperand(MachineOperand::CreateImm(0));
      return BB;
    }

    // We need a block split to make the real endpgm a terminator. We also don't
    // want to break phis in successor blocks, so we can't just delete to the
    // end of the block.

    MachineBasicBlock *SplitBB = BB->splitAt(MI, false /*UpdateLiveIns*/);
    MachineBasicBlock *TrapBB = MF->CreateMachineBasicBlock();
    MF->push_back(TrapBB);
    BuildMI(*TrapBB, TrapBB->end(), DL, TII->get(AMDGPU::S_ENDPGM))
      .addImm(0);
    BuildMI(*BB, &MI, DL, TII->get(AMDGPU::S_CBRANCH_EXECNZ))
      .addMBB(TrapBB);

    BB->addSuccessor(TrapBB);
    MI.eraseFromParent();
    return SplitBB;
  }
  case AMDGPU::SIMULATED_TRAP: {
    assert(Subtarget->hasPrivEnabledTrap2NopBug());
    MachineRegisterInfo &MRI = BB->getParent()->getRegInfo();
    MachineBasicBlock *SplitBB =
        TII->insertSimulatedTrap(MRI, *BB, MI, MI.getDebugLoc());
    MI.eraseFromParent();
    return SplitBB;
  }
  default:
    if (TII->isImage(MI) || TII->isMUBUF(MI)) {
      if (!MI.mayStore())
        AddMemOpInit(MI);
      return BB;
    }
    return AMDGPUTargetLowering::EmitInstrWithCustomInserter(MI, BB);
  }
}

bool SITargetLowering::enableAggressiveFMAFusion(EVT VT) const {
  // This currently forces unfolding various combinations of fsub into fma with
  // free fneg'd operands. As long as we have fast FMA (controlled by
  // isFMAFasterThanFMulAndFAdd), we should perform these.

  // When fma is quarter rate, for f64 where add / sub are at best half rate,
  // most of these combines appear to be cycle neutral but save on instruction
  // count / code size.
  return true;
}

bool SITargetLowering::enableAggressiveFMAFusion(LLT Ty) const { return true; }

EVT SITargetLowering::getSetCCResultType(const DataLayout &DL, LLVMContext &Ctx,
                                         EVT VT) const {
  if (!VT.isVector()) {
    return MVT::i1;
  }
  return EVT::getVectorVT(Ctx, MVT::i1, VT.getVectorNumElements());
}

MVT SITargetLowering::getScalarShiftAmountTy(const DataLayout &, EVT VT) const {
  // TODO: Should i16 be used always if legal? For now it would force VALU
  // shifts.
  return (VT == MVT::i16) ? MVT::i16 : MVT::i32;
}

LLT SITargetLowering::getPreferredShiftAmountTy(LLT Ty) const {
  return (Ty.getScalarSizeInBits() <= 16 && Subtarget->has16BitInsts())
             ? Ty.changeElementSize(16)
             : Ty.changeElementSize(32);
}

// Answering this is somewhat tricky and depends on the specific device which
// have different rates for fma or all f64 operations.
//
// v_fma_f64 and v_mul_f64 always take the same number of cycles as each other
// regardless of which device (although the number of cycles differs between
// devices), so it is always profitable for f64.
//
// v_fma_f32 takes 4 or 16 cycles depending on the device, so it is profitable
// only on full rate devices. Normally, we should prefer selecting v_mad_f32
// which we can always do even without fused FP ops since it returns the same
// result as the separate operations and since it is always full
// rate. Therefore, we lie and report that it is not faster for f32. v_mad_f32
// however does not support denormals, so we do report fma as faster if we have
// a fast fma device and require denormals.
//
bool SITargetLowering::isFMAFasterThanFMulAndFAdd(const MachineFunction &MF,
                                                  EVT VT) const {
  VT = VT.getScalarType();

  switch (VT.getSimpleVT().SimpleTy) {
  case MVT::f32: {
    // If mad is not available this depends only on if f32 fma is full rate.
    if (!Subtarget->hasMadMacF32Insts())
      return Subtarget->hasFastFMAF32();

    // Otherwise f32 mad is always full rate and returns the same result as
    // the separate operations so should be preferred over fma.
    // However does not support denormals.
    if (!denormalModeIsFlushAllF32(MF))
      return Subtarget->hasFastFMAF32() || Subtarget->hasDLInsts();

    // If the subtarget has v_fmac_f32, that's just as good as v_mac_f32.
    return Subtarget->hasFastFMAF32() && Subtarget->hasDLInsts();
  }
  case MVT::f64:
    return true;
  case MVT::f16:
    return Subtarget->has16BitInsts() && !denormalModeIsFlushAllF64F16(MF);
  default:
    break;
  }

  return false;
}

bool SITargetLowering::isFMAFasterThanFMulAndFAdd(const MachineFunction &MF,
                                                  LLT Ty) const {
  switch (Ty.getScalarSizeInBits()) {
  case 16:
    return isFMAFasterThanFMulAndFAdd(MF, MVT::f16);
  case 32:
    return isFMAFasterThanFMulAndFAdd(MF, MVT::f32);
  case 64:
    return isFMAFasterThanFMulAndFAdd(MF, MVT::f64);
  default:
    break;
  }

  return false;
}

bool SITargetLowering::isFMADLegal(const MachineInstr &MI, LLT Ty) const {
  if (!Ty.isScalar())
    return false;

  if (Ty.getScalarSizeInBits() == 16)
    return Subtarget->hasMadF16() && denormalModeIsFlushAllF64F16(*MI.getMF());
  if (Ty.getScalarSizeInBits() == 32)
    return Subtarget->hasMadMacF32Insts() &&
           denormalModeIsFlushAllF32(*MI.getMF());

  return false;
}

bool SITargetLowering::isFMADLegal(const SelectionDAG &DAG,
                                   const SDNode *N) const {
  // TODO: Check future ftz flag
  // v_mad_f32/v_mac_f32 do not support denormals.
  EVT VT = N->getValueType(0);
  if (VT == MVT::f32)
    return Subtarget->hasMadMacF32Insts() &&
           denormalModeIsFlushAllF32(DAG.getMachineFunction());
  if (VT == MVT::f16) {
    return Subtarget->hasMadF16() &&
           denormalModeIsFlushAllF64F16(DAG.getMachineFunction());
  }

  return false;
}

//===----------------------------------------------------------------------===//
// Custom DAG Lowering Operations
//===----------------------------------------------------------------------===//

// Work around LegalizeDAG doing the wrong thing and fully scalarizing if the
// wider vector type is legal.
SDValue SITargetLowering::splitUnaryVectorOp(SDValue Op,
                                             SelectionDAG &DAG) const {
  unsigned Opc = Op.getOpcode();
  EVT VT = Op.getValueType();
  assert(VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4f32 ||
         VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v16i16 ||
         VT == MVT::v16f16 || VT == MVT::v8f32 || VT == MVT::v16f32 ||
         VT == MVT::v32f32 || VT == MVT::v32i16 || VT == MVT::v32f16);

  SDValue Lo, Hi;
  std::tie(Lo, Hi) = DAG.SplitVectorOperand(Op.getNode(), 0);

  SDLoc SL(Op);
  SDValue OpLo = DAG.getNode(Opc, SL, Lo.getValueType(), Lo,
                             Op->getFlags());
  SDValue OpHi = DAG.getNode(Opc, SL, Hi.getValueType(), Hi,
                             Op->getFlags());

  return DAG.getNode(ISD::CONCAT_VECTORS, SDLoc(Op), VT, OpLo, OpHi);
}

// Work around LegalizeDAG doing the wrong thing and fully scalarizing if the
// wider vector type is legal.
SDValue SITargetLowering::splitBinaryVectorOp(SDValue Op,
                                              SelectionDAG &DAG) const {
  unsigned Opc = Op.getOpcode();
  EVT VT = Op.getValueType();
  assert(VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v4f32 ||
         VT == MVT::v8i16 || VT == MVT::v8f16 || VT == MVT::v16i16 ||
         VT == MVT::v16f16 || VT == MVT::v8f32 || VT == MVT::v16f32 ||
         VT == MVT::v32f32 || VT == MVT::v32i16 || VT == MVT::v32f16);

  SDValue Lo0, Hi0;
  std::tie(Lo0, Hi0) = DAG.SplitVectorOperand(Op.getNode(), 0);
  SDValue Lo1, Hi1;
  std::tie(Lo1, Hi1) = DAG.SplitVectorOperand(Op.getNode(), 1);

  SDLoc SL(Op);

  SDValue OpLo = DAG.getNode(Opc, SL, Lo0.getValueType(), Lo0, Lo1,
                             Op->getFlags());
  SDValue OpHi = DAG.getNode(Opc, SL, Hi0.getValueType(), Hi0, Hi1,
                             Op->getFlags());

  return DAG.getNode(ISD::CONCAT_VECTORS, SDLoc(Op), VT, OpLo, OpHi);
}

SDValue SITargetLowering::splitTernaryVectorOp(SDValue Op,
                                              SelectionDAG &DAG) const {
  unsigned Opc = Op.getOpcode();
  EVT VT = Op.getValueType();
  assert(VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v8i16 ||
         VT == MVT::v8f16 || VT == MVT::v4f32 || VT == MVT::v16i16 ||
         VT == MVT::v16f16 || VT == MVT::v8f32 || VT == MVT::v16f32 ||
         VT == MVT::v32f32 || VT == MVT::v32f16 || VT == MVT::v32i16 ||
         VT == MVT::v4bf16 || VT == MVT::v8bf16 || VT == MVT::v16bf16 ||
         VT == MVT::v32bf16);

  SDValue Lo0, Hi0;
  SDValue Op0 = Op.getOperand(0);
  std::tie(Lo0, Hi0) = Op0.getValueType().isVector()
                           ? DAG.SplitVectorOperand(Op.getNode(), 0)
                           : std::pair(Op0, Op0);
  SDValue Lo1, Hi1;
  std::tie(Lo1, Hi1) = DAG.SplitVectorOperand(Op.getNode(), 1);
  SDValue Lo2, Hi2;
  std::tie(Lo2, Hi2) = DAG.SplitVectorOperand(Op.getNode(), 2);

  SDLoc SL(Op);
  auto ResVT = DAG.GetSplitDestVTs(VT);

  SDValue OpLo = DAG.getNode(Opc, SL, ResVT.first, Lo0, Lo1, Lo2,
                             Op->getFlags());
  SDValue OpHi = DAG.getNode(Opc, SL, ResVT.second, Hi0, Hi1, Hi2,
                             Op->getFlags());

  return DAG.getNode(ISD::CONCAT_VECTORS, SDLoc(Op), VT, OpLo, OpHi);
}


SDValue SITargetLowering::LowerOperation(SDValue Op, SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  default: return AMDGPUTargetLowering::LowerOperation(Op, DAG);
  case ISD::BRCOND: return LowerBRCOND(Op, DAG);
  case ISD::RETURNADDR: return LowerRETURNADDR(Op, DAG);
  case ISD::LOAD: {
    SDValue Result = LowerLOAD(Op, DAG);
    assert((!Result.getNode() ||
            Result.getNode()->getNumValues() == 2) &&
           "Load should return a value and a chain");
    return Result;
  }
  case ISD::FSQRT: {
    EVT VT = Op.getValueType();
    if (VT == MVT::f32)
      return lowerFSQRTF32(Op, DAG);
    if (VT == MVT::f64)
      return lowerFSQRTF64(Op, DAG);
    return SDValue();
  }
  case ISD::FSIN:
  case ISD::FCOS:
    return LowerTrig(Op, DAG);
  case ISD::SELECT: return LowerSELECT(Op, DAG);
  case ISD::FDIV: return LowerFDIV(Op, DAG);
  case ISD::FFREXP: return LowerFFREXP(Op, DAG);
  case ISD::ATOMIC_CMP_SWAP: return LowerATOMIC_CMP_SWAP(Op, DAG);
  case ISD::STORE: return LowerSTORE(Op, DAG);
  case ISD::GlobalAddress: {
    MachineFunction &MF = DAG.getMachineFunction();
    SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
    return LowerGlobalAddress(MFI, Op, DAG);
  }
  case ISD::INTRINSIC_WO_CHAIN: return LowerINTRINSIC_WO_CHAIN(Op, DAG);
  case ISD::INTRINSIC_W_CHAIN: return LowerINTRINSIC_W_CHAIN(Op, DAG);
  case ISD::INTRINSIC_VOID: return LowerINTRINSIC_VOID(Op, DAG);
  case ISD::ADDRSPACECAST: return lowerADDRSPACECAST(Op, DAG);
  case ISD::INSERT_SUBVECTOR:
    return lowerINSERT_SUBVECTOR(Op, DAG);
  case ISD::INSERT_VECTOR_ELT:
    return lowerINSERT_VECTOR_ELT(Op, DAG);
  case ISD::EXTRACT_VECTOR_ELT:
    return lowerEXTRACT_VECTOR_ELT(Op, DAG);
  case ISD::VECTOR_SHUFFLE:
    return lowerVECTOR_SHUFFLE(Op, DAG);
  case ISD::SCALAR_TO_VECTOR:
    return lowerSCALAR_TO_VECTOR(Op, DAG);
  case ISD::BUILD_VECTOR:
    return lowerBUILD_VECTOR(Op, DAG);
  case ISD::FP_ROUND:
  case ISD::STRICT_FP_ROUND:
    return lowerFP_ROUND(Op, DAG);
  case ISD::FPTRUNC_ROUND: {
    unsigned Opc;
    SDLoc DL(Op);

    if (Op.getOperand(0)->getValueType(0) != MVT::f32)
      return SDValue();

    // Get the rounding mode from the last operand
    int RoundMode = Op.getConstantOperandVal(1);
    if (RoundMode == (int)RoundingMode::TowardPositive)
      Opc = AMDGPUISD::FPTRUNC_ROUND_UPWARD;
    else if (RoundMode == (int)RoundingMode::TowardNegative)
      Opc = AMDGPUISD::FPTRUNC_ROUND_DOWNWARD;
    else
      return SDValue();

    return DAG.getNode(Opc, DL, Op.getNode()->getVTList(), Op->getOperand(0));
  }
  case ISD::TRAP:
    return lowerTRAP(Op, DAG);
  case ISD::DEBUGTRAP:
    return lowerDEBUGTRAP(Op, DAG);
  case ISD::ABS:
  case ISD::FABS:
  case ISD::FNEG:
  case ISD::FCANONICALIZE:
  case ISD::BSWAP:
    return splitUnaryVectorOp(Op, DAG);
  case ISD::FMINNUM:
  case ISD::FMAXNUM:
    return lowerFMINNUM_FMAXNUM(Op, DAG);
  case ISD::FLDEXP:
  case ISD::STRICT_FLDEXP:
    return lowerFLDEXP(Op, DAG);
  case ISD::FMA:
    return splitTernaryVectorOp(Op, DAG);
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
    return LowerFP_TO_INT(Op, DAG);
  case ISD::SHL:
  case ISD::SRA:
  case ISD::SRL:
  case ISD::ADD:
  case ISD::SUB:
  case ISD::SMIN:
  case ISD::SMAX:
  case ISD::UMIN:
  case ISD::UMAX:
  case ISD::FADD:
  case ISD::FMUL:
  case ISD::FMINNUM_IEEE:
  case ISD::FMAXNUM_IEEE:
  case ISD::FMINIMUM:
  case ISD::FMAXIMUM:
  case ISD::UADDSAT:
  case ISD::USUBSAT:
  case ISD::SADDSAT:
  case ISD::SSUBSAT:
    return splitBinaryVectorOp(Op, DAG);
  case ISD::MUL:
    return lowerMUL(Op, DAG);
  case ISD::SMULO:
  case ISD::UMULO:
    return lowerXMULO(Op, DAG);
  case ISD::SMUL_LOHI:
  case ISD::UMUL_LOHI:
    return lowerXMUL_LOHI(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC:
    return LowerDYNAMIC_STACKALLOC(Op, DAG);
  case ISD::STACKSAVE:
    return LowerSTACKSAVE(Op, DAG);
  case ISD::GET_ROUNDING:
    return lowerGET_ROUNDING(Op, DAG);
  case ISD::SET_ROUNDING:
    return lowerSET_ROUNDING(Op, DAG);
  case ISD::PREFETCH:
    return lowerPREFETCH(Op, DAG);
  case ISD::FP_EXTEND:
  case ISD::STRICT_FP_EXTEND:
    return lowerFP_EXTEND(Op, DAG);
  case ISD::GET_FPENV:
    return lowerGET_FPENV(Op, DAG);
  case ISD::SET_FPENV:
    return lowerSET_FPENV(Op, DAG);
  }
  return SDValue();
}

// Used for D16: Casts the result of an instruction into the right vector,
// packs values if loads return unpacked values.
static SDValue adjustLoadValueTypeImpl(SDValue Result, EVT LoadVT,
                                       const SDLoc &DL,
                                       SelectionDAG &DAG, bool Unpacked) {
  if (!LoadVT.isVector())
    return Result;

  // Cast back to the original packed type or to a larger type that is a
  // multiple of 32 bit for D16. Widening the return type is a required for
  // legalization.
  EVT FittingLoadVT = LoadVT;
  if ((LoadVT.getVectorNumElements() % 2) == 1) {
    FittingLoadVT =
        EVT::getVectorVT(*DAG.getContext(), LoadVT.getVectorElementType(),
                         LoadVT.getVectorNumElements() + 1);
  }

  if (Unpacked) { // From v2i32/v4i32 back to v2f16/v4f16.
    // Truncate to v2i16/v4i16.
    EVT IntLoadVT = FittingLoadVT.changeTypeToInteger();

    // Workaround legalizer not scalarizing truncate after vector op
    // legalization but not creating intermediate vector trunc.
    SmallVector<SDValue, 4> Elts;
    DAG.ExtractVectorElements(Result, Elts);
    for (SDValue &Elt : Elts)
      Elt = DAG.getNode(ISD::TRUNCATE, DL, MVT::i16, Elt);

    // Pad illegal v1i16/v3fi6 to v4i16
    if ((LoadVT.getVectorNumElements() % 2) == 1)
      Elts.push_back(DAG.getUNDEF(MVT::i16));

    Result = DAG.getBuildVector(IntLoadVT, DL, Elts);

    // Bitcast to original type (v2f16/v4f16).
    return DAG.getNode(ISD::BITCAST, DL, FittingLoadVT, Result);
  }

  // Cast back to the original packed type.
  return DAG.getNode(ISD::BITCAST, DL, FittingLoadVT, Result);
}

SDValue SITargetLowering::adjustLoadValueType(unsigned Opcode,
                                              MemSDNode *M,
                                              SelectionDAG &DAG,
                                              ArrayRef<SDValue> Ops,
                                              bool IsIntrinsic) const {
  SDLoc DL(M);

  bool Unpacked = Subtarget->hasUnpackedD16VMem();
  EVT LoadVT = M->getValueType(0);

  EVT EquivLoadVT = LoadVT;
  if (LoadVT.isVector()) {
    if (Unpacked) {
      EquivLoadVT = EVT::getVectorVT(*DAG.getContext(), MVT::i32,
                                     LoadVT.getVectorNumElements());
    } else if ((LoadVT.getVectorNumElements() % 2) == 1) {
      // Widen v3f16 to legal type
      EquivLoadVT =
          EVT::getVectorVT(*DAG.getContext(), LoadVT.getVectorElementType(),
                           LoadVT.getVectorNumElements() + 1);
    }
  }

  // Change from v4f16/v2f16 to EquivLoadVT.
  SDVTList VTList = DAG.getVTList(EquivLoadVT, MVT::Other);

  SDValue Load
    = DAG.getMemIntrinsicNode(
      IsIntrinsic ? (unsigned)ISD::INTRINSIC_W_CHAIN : Opcode, DL,
      VTList, Ops, M->getMemoryVT(),
      M->getMemOperand());

  SDValue Adjusted = adjustLoadValueTypeImpl(Load, LoadVT, DL, DAG, Unpacked);

  return DAG.getMergeValues({ Adjusted, Load.getValue(1) }, DL);
}

SDValue SITargetLowering::lowerIntrinsicLoad(MemSDNode *M, bool IsFormat,
                                             SelectionDAG &DAG,
                                             ArrayRef<SDValue> Ops) const {
  SDLoc DL(M);
  EVT LoadVT = M->getValueType(0);
  EVT EltType = LoadVT.getScalarType();
  EVT IntVT = LoadVT.changeTypeToInteger();

  bool IsD16 = IsFormat && (EltType.getSizeInBits() == 16);

  assert(M->getNumValues() == 2 || M->getNumValues() == 3);
  bool IsTFE = M->getNumValues() == 3;

  unsigned Opc = IsFormat ? (IsTFE ? AMDGPUISD::BUFFER_LOAD_FORMAT_TFE
                                   : AMDGPUISD::BUFFER_LOAD_FORMAT)
                 : IsTFE  ? AMDGPUISD::BUFFER_LOAD_TFE
                          : AMDGPUISD::BUFFER_LOAD;

  if (IsD16) {
    return adjustLoadValueType(AMDGPUISD::BUFFER_LOAD_FORMAT_D16, M, DAG, Ops);
  }

  // Handle BUFFER_LOAD_BYTE/UBYTE/SHORT/USHORT overloaded intrinsics
  if (!IsD16 && !LoadVT.isVector() && EltType.getSizeInBits() < 32)
    return handleByteShortBufferLoads(DAG, LoadVT, DL, Ops, M->getMemOperand(),
                                      IsTFE);

  if (isTypeLegal(LoadVT)) {
    return getMemIntrinsicNode(Opc, DL, M->getVTList(), Ops, IntVT,
                               M->getMemOperand(), DAG);
  }

  EVT CastVT = getEquivalentMemType(*DAG.getContext(), LoadVT);
  SDVTList VTList = DAG.getVTList(CastVT, MVT::Other);
  SDValue MemNode = getMemIntrinsicNode(Opc, DL, VTList, Ops, CastVT,
                                        M->getMemOperand(), DAG);
  return DAG.getMergeValues(
      {DAG.getNode(ISD::BITCAST, DL, LoadVT, MemNode), MemNode.getValue(1)},
      DL);
}

static SDValue lowerICMPIntrinsic(const SITargetLowering &TLI,
                                  SDNode *N, SelectionDAG &DAG) {
  EVT VT = N->getValueType(0);
  unsigned CondCode = N->getConstantOperandVal(3);
  if (!ICmpInst::isIntPredicate(static_cast<ICmpInst::Predicate>(CondCode)))
    return DAG.getUNDEF(VT);

  ICmpInst::Predicate IcInput = static_cast<ICmpInst::Predicate>(CondCode);

  SDValue LHS = N->getOperand(1);
  SDValue RHS = N->getOperand(2);

  SDLoc DL(N);

  EVT CmpVT = LHS.getValueType();
  if (CmpVT == MVT::i16 && !TLI.isTypeLegal(MVT::i16)) {
    unsigned PromoteOp = ICmpInst::isSigned(IcInput) ?
      ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
    LHS = DAG.getNode(PromoteOp, DL, MVT::i32, LHS);
    RHS = DAG.getNode(PromoteOp, DL, MVT::i32, RHS);
  }

  ISD::CondCode CCOpcode = getICmpCondCode(IcInput);

  unsigned WavefrontSize = TLI.getSubtarget()->getWavefrontSize();
  EVT CCVT = EVT::getIntegerVT(*DAG.getContext(), WavefrontSize);

  SDValue SetCC = DAG.getNode(AMDGPUISD::SETCC, DL, CCVT, LHS, RHS,
                              DAG.getCondCode(CCOpcode));
  if (VT.bitsEq(CCVT))
    return SetCC;
  return DAG.getZExtOrTrunc(SetCC, DL, VT);
}

static SDValue lowerFCMPIntrinsic(const SITargetLowering &TLI,
                                  SDNode *N, SelectionDAG &DAG) {
  EVT VT = N->getValueType(0);

  unsigned CondCode = N->getConstantOperandVal(3);
  if (!FCmpInst::isFPPredicate(static_cast<FCmpInst::Predicate>(CondCode)))
    return DAG.getUNDEF(VT);

  SDValue Src0 = N->getOperand(1);
  SDValue Src1 = N->getOperand(2);
  EVT CmpVT = Src0.getValueType();
  SDLoc SL(N);

  if (CmpVT == MVT::f16 && !TLI.isTypeLegal(CmpVT)) {
    Src0 = DAG.getNode(ISD::FP_EXTEND, SL, MVT::f32, Src0);
    Src1 = DAG.getNode(ISD::FP_EXTEND, SL, MVT::f32, Src1);
  }

  FCmpInst::Predicate IcInput = static_cast<FCmpInst::Predicate>(CondCode);
  ISD::CondCode CCOpcode = getFCmpCondCode(IcInput);
  unsigned WavefrontSize = TLI.getSubtarget()->getWavefrontSize();
  EVT CCVT = EVT::getIntegerVT(*DAG.getContext(), WavefrontSize);
  SDValue SetCC = DAG.getNode(AMDGPUISD::SETCC, SL, CCVT, Src0,
                              Src1, DAG.getCondCode(CCOpcode));
  if (VT.bitsEq(CCVT))
    return SetCC;
  return DAG.getZExtOrTrunc(SetCC, SL, VT);
}

static SDValue lowerBALLOTIntrinsic(const SITargetLowering &TLI, SDNode *N,
                                    SelectionDAG &DAG) {
  EVT VT = N->getValueType(0);
  SDValue Src = N->getOperand(1);
  SDLoc SL(N);

  if (Src.getOpcode() == ISD::SETCC) {
    // (ballot (ISD::SETCC ...)) -> (AMDGPUISD::SETCC ...)
    return DAG.getNode(AMDGPUISD::SETCC, SL, VT, Src.getOperand(0),
                       Src.getOperand(1), Src.getOperand(2));
  }
  if (const ConstantSDNode *Arg = dyn_cast<ConstantSDNode>(Src)) {
    // (ballot 0) -> 0
    if (Arg->isZero())
      return DAG.getConstant(0, SL, VT);

    // (ballot 1) -> EXEC/EXEC_LO
    if (Arg->isOne()) {
      Register Exec;
      if (VT.getScalarSizeInBits() == 32)
        Exec = AMDGPU::EXEC_LO;
      else if (VT.getScalarSizeInBits() == 64)
        Exec = AMDGPU::EXEC;
      else
        return SDValue();

      return DAG.getCopyFromReg(DAG.getEntryNode(), SL, Exec, VT);
    }
  }

  // (ballot (i1 $src)) -> (AMDGPUISD::SETCC (i32 (zext $src)) (i32 0)
  // ISD::SETNE)
  return DAG.getNode(
      AMDGPUISD::SETCC, SL, VT, DAG.getZExtOrTrunc(Src, SL, MVT::i32),
      DAG.getConstant(0, SL, MVT::i32), DAG.getCondCode(ISD::SETNE));
}

static SDValue lowerLaneOp(const SITargetLowering &TLI, SDNode *N,
                           SelectionDAG &DAG) {
  EVT VT = N->getValueType(0);
  unsigned ValSize = VT.getSizeInBits();
  unsigned IID = N->getConstantOperandVal(0);
  bool IsPermLane16 = IID == Intrinsic::amdgcn_permlane16 ||
                      IID == Intrinsic::amdgcn_permlanex16;
  SDLoc SL(N);
  MVT IntVT = MVT::getIntegerVT(ValSize);

  auto createLaneOp = [&DAG, &SL, N, IID](SDValue Src0, SDValue Src1,
                                          SDValue Src2, MVT ValT) -> SDValue {
    SmallVector<SDValue, 8> Operands;
    switch (IID) {
    case Intrinsic::amdgcn_permlane16:
    case Intrinsic::amdgcn_permlanex16:
      Operands.push_back(N->getOperand(6));
      Operands.push_back(N->getOperand(5));
      Operands.push_back(N->getOperand(4));
      [[fallthrough]];
    case Intrinsic::amdgcn_writelane:
      Operands.push_back(Src2);
      [[fallthrough]];
    case Intrinsic::amdgcn_readlane:
      Operands.push_back(Src1);
      [[fallthrough]];
    case Intrinsic::amdgcn_readfirstlane:
    case Intrinsic::amdgcn_permlane64:
      Operands.push_back(Src0);
      break;
    default:
      llvm_unreachable("unhandled lane op");
    }

    Operands.push_back(DAG.getTargetConstant(IID, SL, MVT::i32));
    std::reverse(Operands.begin(), Operands.end());

    if (SDNode *GL = N->getGluedNode()) {
      assert(GL->getOpcode() == ISD::CONVERGENCECTRL_GLUE);
      GL = GL->getOperand(0).getNode();
      Operands.push_back(DAG.getNode(ISD::CONVERGENCECTRL_GLUE, SL, MVT::Glue,
                                     SDValue(GL, 0)));
    }

    return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, SL, ValT, Operands);
  };

  SDValue Src0 = N->getOperand(1);
  SDValue Src1, Src2;
  if (IID == Intrinsic::amdgcn_readlane || IID == Intrinsic::amdgcn_writelane ||
      IsPermLane16) {
    Src1 = N->getOperand(2);
    if (IID == Intrinsic::amdgcn_writelane || IsPermLane16)
      Src2 = N->getOperand(3);
  }

  if (ValSize == 32) {
    // Already legal
    return SDValue();
  }

  if (ValSize < 32) {
    bool IsFloat = VT.isFloatingPoint();
    Src0 = DAG.getAnyExtOrTrunc(IsFloat ? DAG.getBitcast(IntVT, Src0) : Src0,
                                SL, MVT::i32);

    if (IsPermLane16) {
      Src1 = DAG.getAnyExtOrTrunc(IsFloat ? DAG.getBitcast(IntVT, Src1) : Src1,
                                  SL, MVT::i32);
    }

    if (IID == Intrinsic::amdgcn_writelane) {
      Src2 = DAG.getAnyExtOrTrunc(IsFloat ? DAG.getBitcast(IntVT, Src2) : Src2,
                                  SL, MVT::i32);
    }

    SDValue LaneOp = createLaneOp(Src0, Src1, Src2, MVT::i32);
    SDValue Trunc = DAG.getAnyExtOrTrunc(LaneOp, SL, IntVT);
    return IsFloat ? DAG.getBitcast(VT, Trunc) : Trunc;
  }

  if (ValSize % 32 != 0)
    return SDValue();

  auto unrollLaneOp = [&DAG, &SL](SDNode *N) -> SDValue {
    EVT VT = N->getValueType(0);
    unsigned NE = VT.getVectorNumElements();
    EVT EltVT = VT.getVectorElementType();
    SmallVector<SDValue, 8> Scalars;
    unsigned NumOperands = N->getNumOperands();
    SmallVector<SDValue, 4> Operands(NumOperands);
    SDNode *GL = N->getGluedNode();

    // only handle convergencectrl_glue
    assert(!GL || GL->getOpcode() == ISD::CONVERGENCECTRL_GLUE);

    for (unsigned i = 0; i != NE; ++i) {
      for (unsigned j = 0, e = GL ? NumOperands - 1 : NumOperands; j != e;
           ++j) {
        SDValue Operand = N->getOperand(j);
        EVT OperandVT = Operand.getValueType();
        if (OperandVT.isVector()) {
          // A vector operand; extract a single element.
          EVT OperandEltVT = OperandVT.getVectorElementType();
          Operands[j] = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, OperandEltVT,
                                    Operand, DAG.getVectorIdxConstant(i, SL));
        } else {
          // A scalar operand; just use it as is.
          Operands[j] = Operand;
        }
      }

      if (GL)
        Operands[NumOperands - 1] =
            DAG.getNode(ISD::CONVERGENCECTRL_GLUE, SL, MVT::Glue,
                        SDValue(GL->getOperand(0).getNode(), 0));

      Scalars.push_back(DAG.getNode(N->getOpcode(), SL, EltVT, Operands));
    }

    EVT VecVT = EVT::getVectorVT(*DAG.getContext(), EltVT, NE);
    return DAG.getBuildVector(VecVT, SL, Scalars);
  };

  if (VT.isVector()) {
    switch (MVT::SimpleValueType EltTy =
                VT.getVectorElementType().getSimpleVT().SimpleTy) {
    case MVT::i32:
    case MVT::f32: {
      SDValue LaneOp = createLaneOp(Src0, Src1, Src2, VT.getSimpleVT());
      return unrollLaneOp(LaneOp.getNode());
    }
    case MVT::i16:
    case MVT::f16:
    case MVT::bf16: {
      MVT SubVecVT = MVT::getVectorVT(EltTy, 2);
      SmallVector<SDValue, 4> Pieces;
      SDValue Src0SubVec, Src1SubVec, Src2SubVec;
      for (unsigned i = 0, EltIdx = 0; i < ValSize / 32; i++) {
        Src0SubVec = DAG.getNode(ISD::EXTRACT_SUBVECTOR, SL, SubVecVT, Src0,
                                 DAG.getConstant(EltIdx, SL, MVT::i32));

        if (IsPermLane16)
          Src1SubVec = DAG.getNode(ISD::EXTRACT_SUBVECTOR, SL, SubVecVT, Src1,
                                   DAG.getConstant(EltIdx, SL, MVT::i32));

        if (IID == Intrinsic::amdgcn_writelane)
          Src2SubVec = DAG.getNode(ISD::EXTRACT_SUBVECTOR, SL, SubVecVT, Src2,
                                   DAG.getConstant(EltIdx, SL, MVT::i32));

        Pieces.push_back(
            IsPermLane16
                ? createLaneOp(Src0SubVec, Src1SubVec, Src2, SubVecVT)
                : createLaneOp(Src0SubVec, Src1, Src2SubVec, SubVecVT));
        EltIdx += 2;
      }
      return DAG.getNode(ISD::CONCAT_VECTORS, SL, VT, Pieces);
    }
    default:
      // Handle all other cases by bitcasting to i32 vectors
      break;
    }
  }

  MVT VecVT = MVT::getVectorVT(MVT::i32, ValSize / 32);
  Src0 = DAG.getBitcast(VecVT, Src0);

  if (IsPermLane16)
    Src1 = DAG.getBitcast(VecVT, Src1);

  if (IID == Intrinsic::amdgcn_writelane)
    Src2 = DAG.getBitcast(VecVT, Src2);

  SDValue LaneOp = createLaneOp(Src0, Src1, Src2, VecVT);
  SDValue UnrolledLaneOp = unrollLaneOp(LaneOp.getNode());
  return DAG.getBitcast(VT, UnrolledLaneOp);
}

void SITargetLowering::ReplaceNodeResults(SDNode *N,
                                          SmallVectorImpl<SDValue> &Results,
                                          SelectionDAG &DAG) const {
  switch (N->getOpcode()) {
  case ISD::INSERT_VECTOR_ELT: {
    if (SDValue Res = lowerINSERT_VECTOR_ELT(SDValue(N, 0), DAG))
      Results.push_back(Res);
    return;
  }
  case ISD::EXTRACT_VECTOR_ELT: {
    if (SDValue Res = lowerEXTRACT_VECTOR_ELT(SDValue(N, 0), DAG))
      Results.push_back(Res);
    return;
  }
  case ISD::INTRINSIC_WO_CHAIN: {
    unsigned IID = N->getConstantOperandVal(0);
    switch (IID) {
    case Intrinsic::amdgcn_make_buffer_rsrc:
      Results.push_back(lowerPointerAsRsrcIntrin(N, DAG));
      return;
    case Intrinsic::amdgcn_cvt_pkrtz: {
      SDValue Src0 = N->getOperand(1);
      SDValue Src1 = N->getOperand(2);
      SDLoc SL(N);
      SDValue Cvt = DAG.getNode(AMDGPUISD::CVT_PKRTZ_F16_F32, SL, MVT::i32,
                                Src0, Src1);
      Results.push_back(DAG.getNode(ISD::BITCAST, SL, MVT::v2f16, Cvt));
      return;
    }
    case Intrinsic::amdgcn_cvt_pknorm_i16:
    case Intrinsic::amdgcn_cvt_pknorm_u16:
    case Intrinsic::amdgcn_cvt_pk_i16:
    case Intrinsic::amdgcn_cvt_pk_u16: {
      SDValue Src0 = N->getOperand(1);
      SDValue Src1 = N->getOperand(2);
      SDLoc SL(N);
      unsigned Opcode;

      if (IID == Intrinsic::amdgcn_cvt_pknorm_i16)
        Opcode = AMDGPUISD::CVT_PKNORM_I16_F32;
      else if (IID == Intrinsic::amdgcn_cvt_pknorm_u16)
        Opcode = AMDGPUISD::CVT_PKNORM_U16_F32;
      else if (IID == Intrinsic::amdgcn_cvt_pk_i16)
        Opcode = AMDGPUISD::CVT_PK_I16_I32;
      else
        Opcode = AMDGPUISD::CVT_PK_U16_U32;

      EVT VT = N->getValueType(0);
      if (isTypeLegal(VT))
        Results.push_back(DAG.getNode(Opcode, SL, VT, Src0, Src1));
      else {
        SDValue Cvt = DAG.getNode(Opcode, SL, MVT::i32, Src0, Src1);
        Results.push_back(DAG.getNode(ISD::BITCAST, SL, MVT::v2i16, Cvt));
      }
      return;
    }
    case Intrinsic::amdgcn_s_buffer_load: {
      // Lower llvm.amdgcn.s.buffer.load.(i8, u8) intrinsics. First, we generate
      // s_buffer_load_u8 for signed and unsigned load instructions. Next, DAG
      // combiner tries to merge the s_buffer_load_u8 with a sext instruction
      // (performSignExtendInRegCombine()) and it replaces s_buffer_load_u8 with
      // s_buffer_load_i8.
      if (!Subtarget->hasScalarSubwordLoads())
        return;
      SDValue Op = SDValue(N, 0);
      SDValue Rsrc = Op.getOperand(1);
      SDValue Offset = Op.getOperand(2);
      SDValue CachePolicy = Op.getOperand(3);
      EVT VT = Op.getValueType();
      assert(VT == MVT::i8 && "Expected 8-bit s_buffer_load intrinsics.\n");
      SDLoc DL(Op);
      MachineFunction &MF = DAG.getMachineFunction();
      const DataLayout &DataLayout = DAG.getDataLayout();
      Align Alignment =
          DataLayout.getABITypeAlign(VT.getTypeForEVT(*DAG.getContext()));
      MachineMemOperand *MMO = MF.getMachineMemOperand(
          MachinePointerInfo(),
          MachineMemOperand::MOLoad | MachineMemOperand::MODereferenceable |
              MachineMemOperand::MOInvariant,
          VT.getStoreSize(), Alignment);
      SDValue LoadVal;
      if (!Offset->isDivergent()) {
        SDValue Ops[] = {Rsrc, // source register
                         Offset, CachePolicy};
        SDValue BufferLoad =
            DAG.getMemIntrinsicNode(AMDGPUISD::SBUFFER_LOAD_UBYTE, DL,
                                    DAG.getVTList(MVT::i32), Ops, VT, MMO);
        LoadVal = DAG.getNode(ISD::TRUNCATE, DL, VT, BufferLoad);
      } else {
        SDValue Ops[] = {
            DAG.getEntryNode(),                    // Chain
            Rsrc,                                  // rsrc
            DAG.getConstant(0, DL, MVT::i32),      // vindex
            {},                                    // voffset
            {},                                    // soffset
            {},                                    // offset
            CachePolicy,                           // cachepolicy
            DAG.getTargetConstant(0, DL, MVT::i1), // idxen
        };
        setBufferOffsets(Offset, DAG, &Ops[3], Align(4));
        LoadVal = handleByteShortBufferLoads(DAG, VT, DL, Ops, MMO);
      }
      Results.push_back(LoadVal);
      return;
    }
    }
    break;
  }
  case ISD::INTRINSIC_W_CHAIN: {
    if (SDValue Res = LowerINTRINSIC_W_CHAIN(SDValue(N, 0), DAG)) {
      if (Res.getOpcode() == ISD::MERGE_VALUES) {
        // FIXME: Hacky
        for (unsigned I = 0; I < Res.getNumOperands(); I++) {
          Results.push_back(Res.getOperand(I));
        }
      } else {
        Results.push_back(Res);
        Results.push_back(Res.getValue(1));
      }
      return;
    }

    break;
  }
  case ISD::SELECT: {
    SDLoc SL(N);
    EVT VT = N->getValueType(0);
    EVT NewVT = getEquivalentMemType(*DAG.getContext(), VT);
    SDValue LHS = DAG.getNode(ISD::BITCAST, SL, NewVT, N->getOperand(1));
    SDValue RHS = DAG.getNode(ISD::BITCAST, SL, NewVT, N->getOperand(2));

    EVT SelectVT = NewVT;
    if (NewVT.bitsLT(MVT::i32)) {
      LHS = DAG.getNode(ISD::ANY_EXTEND, SL, MVT::i32, LHS);
      RHS = DAG.getNode(ISD::ANY_EXTEND, SL, MVT::i32, RHS);
      SelectVT = MVT::i32;
    }

    SDValue NewSelect = DAG.getNode(ISD::SELECT, SL, SelectVT,
                                    N->getOperand(0), LHS, RHS);

    if (NewVT != SelectVT)
      NewSelect = DAG.getNode(ISD::TRUNCATE, SL, NewVT, NewSelect);
    Results.push_back(DAG.getNode(ISD::BITCAST, SL, VT, NewSelect));
    return;
  }
  case ISD::FNEG: {
    if (N->getValueType(0) != MVT::v2f16)
      break;

    SDLoc SL(N);
    SDValue BC = DAG.getNode(ISD::BITCAST, SL, MVT::i32, N->getOperand(0));

    SDValue Op = DAG.getNode(ISD::XOR, SL, MVT::i32,
                             BC,
                             DAG.getConstant(0x80008000, SL, MVT::i32));
    Results.push_back(DAG.getNode(ISD::BITCAST, SL, MVT::v2f16, Op));
    return;
  }
  case ISD::FABS: {
    if (N->getValueType(0) != MVT::v2f16)
      break;

    SDLoc SL(N);
    SDValue BC = DAG.getNode(ISD::BITCAST, SL, MVT::i32, N->getOperand(0));

    SDValue Op = DAG.getNode(ISD::AND, SL, MVT::i32,
                             BC,
                             DAG.getConstant(0x7fff7fff, SL, MVT::i32));
    Results.push_back(DAG.getNode(ISD::BITCAST, SL, MVT::v2f16, Op));
    return;
  }
  case ISD::FSQRT: {
    if (N->getValueType(0) != MVT::f16)
      break;
    Results.push_back(lowerFSQRTF16(SDValue(N, 0), DAG));
    break;
  }
  default:
    AMDGPUTargetLowering::ReplaceNodeResults(N, Results, DAG);
    break;
  }
}

/// Helper function for LowerBRCOND
static SDNode *findUser(SDValue Value, unsigned Opcode) {

  SDNode *Parent = Value.getNode();
  for (SDNode::use_iterator I = Parent->use_begin(), E = Parent->use_end();
       I != E; ++I) {

    if (I.getUse().get() != Value)
      continue;

    if (I->getOpcode() == Opcode)
      return *I;
  }
  return nullptr;
}

unsigned SITargetLowering::isCFIntrinsic(const SDNode *Intr) const {
  if (Intr->getOpcode() == ISD::INTRINSIC_W_CHAIN) {
    switch (Intr->getConstantOperandVal(1)) {
    case Intrinsic::amdgcn_if:
      return AMDGPUISD::IF;
    case Intrinsic::amdgcn_else:
      return AMDGPUISD::ELSE;
    case Intrinsic::amdgcn_loop:
      return AMDGPUISD::LOOP;
    case Intrinsic::amdgcn_end_cf:
      llvm_unreachable("should not occur");
    default:
      return 0;
    }
  }

  // break, if_break, else_break are all only used as inputs to loop, not
  // directly as branch conditions.
  return 0;
}

bool SITargetLowering::shouldEmitFixup(const GlobalValue *GV) const {
  const Triple &TT = getTargetMachine().getTargetTriple();
  return (GV->getAddressSpace() == AMDGPUAS::CONSTANT_ADDRESS ||
          GV->getAddressSpace() == AMDGPUAS::CONSTANT_ADDRESS_32BIT) &&
         AMDGPU::shouldEmitConstantsToTextSection(TT);
}

bool SITargetLowering::shouldEmitGOTReloc(const GlobalValue *GV) const {
  if (Subtarget->isAmdPalOS() || Subtarget->isMesa3DOS())
    return false;

  // FIXME: Either avoid relying on address space here or change the default
  // address space for functions to avoid the explicit check.
  return (GV->getValueType()->isFunctionTy() ||
          !isNonGlobalAddrSpace(GV->getAddressSpace())) &&
         !shouldEmitFixup(GV) && !getTargetMachine().shouldAssumeDSOLocal(GV);
}

bool SITargetLowering::shouldEmitPCReloc(const GlobalValue *GV) const {
  return !shouldEmitFixup(GV) && !shouldEmitGOTReloc(GV);
}

bool SITargetLowering::shouldUseLDSConstAddress(const GlobalValue *GV) const {
  if (!GV->hasExternalLinkage())
    return true;

  const auto OS = getTargetMachine().getTargetTriple().getOS();
  return OS == Triple::AMDHSA || OS == Triple::AMDPAL;
}

/// This transforms the control flow intrinsics to get the branch destination as
/// last parameter, also switches branch target with BR if the need arise
SDValue SITargetLowering::LowerBRCOND(SDValue BRCOND,
                                      SelectionDAG &DAG) const {
  SDLoc DL(BRCOND);

  SDNode *Intr = BRCOND.getOperand(1).getNode();
  SDValue Target = BRCOND.getOperand(2);
  SDNode *BR = nullptr;
  SDNode *SetCC = nullptr;

  if (Intr->getOpcode() == ISD::SETCC) {
    // As long as we negate the condition everything is fine
    SetCC = Intr;
    Intr = SetCC->getOperand(0).getNode();

  } else {
    // Get the target from BR if we don't negate the condition
    BR = findUser(BRCOND, ISD::BR);
    assert(BR && "brcond missing unconditional branch user");
    Target = BR->getOperand(1);
  }

  unsigned CFNode = isCFIntrinsic(Intr);
  if (CFNode == 0) {
    // This is a uniform branch so we don't need to legalize.
    return BRCOND;
  }

  bool HaveChain = Intr->getOpcode() == ISD::INTRINSIC_VOID ||
                   Intr->getOpcode() == ISD::INTRINSIC_W_CHAIN;

  assert(!SetCC ||
        (SetCC->getConstantOperandVal(1) == 1 &&
         cast<CondCodeSDNode>(SetCC->getOperand(2).getNode())->get() ==
                                                             ISD::SETNE));

  // operands of the new intrinsic call
  SmallVector<SDValue, 4> Ops;
  if (HaveChain)
    Ops.push_back(BRCOND.getOperand(0));

  Ops.append(Intr->op_begin() + (HaveChain ?  2 : 1), Intr->op_end());
  Ops.push_back(Target);

  ArrayRef<EVT> Res(Intr->value_begin() + 1, Intr->value_end());

  // build the new intrinsic call
  SDNode *Result = DAG.getNode(CFNode, DL, DAG.getVTList(Res), Ops).getNode();

  if (!HaveChain) {
    SDValue Ops[] =  {
      SDValue(Result, 0),
      BRCOND.getOperand(0)
    };

    Result = DAG.getMergeValues(Ops, DL).getNode();
  }

  if (BR) {
    // Give the branch instruction our target
    SDValue Ops[] = {
      BR->getOperand(0),
      BRCOND.getOperand(2)
    };
    SDValue NewBR = DAG.getNode(ISD::BR, DL, BR->getVTList(), Ops);
    DAG.ReplaceAllUsesWith(BR, NewBR.getNode());
  }

  SDValue Chain = SDValue(Result, Result->getNumValues() - 1);

  // Copy the intrinsic results to registers
  for (unsigned i = 1, e = Intr->getNumValues() - 1; i != e; ++i) {
    SDNode *CopyToReg = findUser(SDValue(Intr, i), ISD::CopyToReg);
    if (!CopyToReg)
      continue;

    Chain = DAG.getCopyToReg(
      Chain, DL,
      CopyToReg->getOperand(1),
      SDValue(Result, i - 1),
      SDValue());

    DAG.ReplaceAllUsesWith(SDValue(CopyToReg, 0), CopyToReg->getOperand(0));
  }

  // Remove the old intrinsic from the chain
  DAG.ReplaceAllUsesOfValueWith(
    SDValue(Intr, Intr->getNumValues() - 1),
    Intr->getOperand(0));

  return Chain;
}

SDValue SITargetLowering::LowerRETURNADDR(SDValue Op,
                                          SelectionDAG &DAG) const {
  MVT VT = Op.getSimpleValueType();
  SDLoc DL(Op);
  // Checking the depth
  if (Op.getConstantOperandVal(0) != 0)
    return DAG.getConstant(0, DL, VT);

  MachineFunction &MF = DAG.getMachineFunction();
  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();
  // Check for kernel and shader functions
  if (Info->isEntryFunction())
    return DAG.getConstant(0, DL, VT);

  MachineFrameInfo &MFI = MF.getFrameInfo();
  // There is a call to @llvm.returnaddress in this function
  MFI.setReturnAddressIsTaken(true);

  const SIRegisterInfo *TRI = getSubtarget()->getRegisterInfo();
  // Get the return address reg and mark it as an implicit live-in
  Register Reg = MF.addLiveIn(TRI->getReturnAddressReg(MF), getRegClassFor(VT, Op.getNode()->isDivergent()));

  return DAG.getCopyFromReg(DAG.getEntryNode(), DL, Reg, VT);
}

SDValue SITargetLowering::getFPExtOrFPRound(SelectionDAG &DAG,
                                            SDValue Op,
                                            const SDLoc &DL,
                                            EVT VT) const {
  return Op.getValueType().bitsLE(VT) ?
      DAG.getNode(ISD::FP_EXTEND, DL, VT, Op) :
    DAG.getNode(ISD::FP_ROUND, DL, VT, Op,
                DAG.getTargetConstant(0, DL, MVT::i32));
}

SDValue SITargetLowering::lowerFP_ROUND(SDValue Op, SelectionDAG &DAG) const {
  assert(Op.getValueType() == MVT::f16 &&
         "Do not know how to custom lower FP_ROUND for non-f16 type");

  SDValue Src = Op.getOperand(0);
  EVT SrcVT = Src.getValueType();
  if (SrcVT != MVT::f64)
    return Op;

  // TODO: Handle strictfp
  if (Op.getOpcode() != ISD::FP_ROUND)
    return Op;

  SDLoc DL(Op);

  SDValue FpToFp16 = DAG.getNode(ISD::FP_TO_FP16, DL, MVT::i32, Src);
  SDValue Trunc = DAG.getNode(ISD::TRUNCATE, DL, MVT::i16, FpToFp16);
  return DAG.getNode(ISD::BITCAST, DL, MVT::f16, Trunc);
}

SDValue SITargetLowering::lowerFMINNUM_FMAXNUM(SDValue Op,
                                               SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  const MachineFunction &MF = DAG.getMachineFunction();
  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();
  bool IsIEEEMode = Info->getMode().IEEE;

  // FIXME: Assert during selection that this is only selected for
  // ieee_mode. Currently a combine can produce the ieee version for non-ieee
  // mode functions, but this happens to be OK since it's only done in cases
  // where there is known no sNaN.
  if (IsIEEEMode)
    return expandFMINNUM_FMAXNUM(Op.getNode(), DAG);

  if (VT == MVT::v4f16 || VT == MVT::v8f16 || VT == MVT::v16f16 ||
      VT == MVT::v16bf16)
    return splitBinaryVectorOp(Op, DAG);
  return Op;
}

SDValue SITargetLowering::lowerFLDEXP(SDValue Op, SelectionDAG &DAG) const {
  bool IsStrict = Op.getOpcode() == ISD::STRICT_FLDEXP;
  EVT VT = Op.getValueType();
  assert(VT == MVT::f16);

  SDValue Exp = Op.getOperand(IsStrict ? 2 : 1);
  EVT ExpVT = Exp.getValueType();
  if (ExpVT == MVT::i16)
    return Op;

  SDLoc DL(Op);

  // Correct the exponent type for f16 to i16.
  // Clamp the range of the exponent to the instruction's range.

  // TODO: This should be a generic narrowing legalization, and can easily be
  // for GlobalISel.

  SDValue MinExp = DAG.getConstant(minIntN(16), DL, ExpVT);
  SDValue ClampMin = DAG.getNode(ISD::SMAX, DL, ExpVT, Exp, MinExp);

  SDValue MaxExp = DAG.getConstant(maxIntN(16), DL, ExpVT);
  SDValue Clamp = DAG.getNode(ISD::SMIN, DL, ExpVT, ClampMin, MaxExp);

  SDValue TruncExp = DAG.getNode(ISD::TRUNCATE, DL, MVT::i16, Clamp);

  if (IsStrict) {
    return DAG.getNode(ISD::STRICT_FLDEXP, DL, {VT, MVT::Other},
                       {Op.getOperand(0), Op.getOperand(1), TruncExp});
  }

  return DAG.getNode(ISD::FLDEXP, DL, VT, Op.getOperand(0), TruncExp);
}

// Custom lowering for vector multiplications and s_mul_u64.
SDValue SITargetLowering::lowerMUL(SDValue Op, SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();

  // Split vector operands.
  if (VT.isVector())
    return splitBinaryVectorOp(Op, DAG);

  assert(VT == MVT::i64 && "The following code is a special for s_mul_u64");

  // There are four ways to lower s_mul_u64:
  //
  // 1. If all the operands are uniform, then we lower it as it is.
  //
  // 2. If the operands are divergent, then we have to split s_mul_u64 in 32-bit
  //    multiplications because there is not a vector equivalent of s_mul_u64.
  //
  // 3. If the cost model decides that it is more efficient to use vector
  //    registers, then we have to split s_mul_u64 in 32-bit multiplications.
  //    This happens in splitScalarSMULU64() in SIInstrInfo.cpp .
  //
  // 4. If the cost model decides to use vector registers and both of the
  //    operands are zero-extended/sign-extended from 32-bits, then we split the
  //    s_mul_u64 in two 32-bit multiplications. The problem is that it is not
  //    possible to check if the operands are zero-extended or sign-extended in
  //    SIInstrInfo.cpp. For this reason, here, we replace s_mul_u64 with
  //    s_mul_u64_u32_pseudo if both operands are zero-extended and we replace
  //    s_mul_u64 with s_mul_i64_i32_pseudo if both operands are sign-extended.
  //    If the cost model decides that we have to use vector registers, then
  //    splitScalarSMulPseudo() (in SIInstrInfo.cpp) split s_mul_u64_u32/
  //    s_mul_i64_i32_pseudo in two vector multiplications. If the cost model
  //    decides that we should use scalar registers, then s_mul_u64_u32_pseudo/
  //    s_mul_i64_i32_pseudo is lowered as s_mul_u64 in expandPostRAPseudo() in
  //    SIInstrInfo.cpp .

  if (Op->isDivergent())
    return SDValue();

  SDValue Op0 = Op.getOperand(0);
  SDValue Op1 = Op.getOperand(1);
  // If all the operands are zero-enteted to 32-bits, then we replace s_mul_u64
  // with s_mul_u64_u32_pseudo. If all the operands are sign-extended to
  // 32-bits, then we replace s_mul_u64 with s_mul_i64_i32_pseudo.
  KnownBits Op0KnownBits = DAG.computeKnownBits(Op0);
  unsigned Op0LeadingZeros = Op0KnownBits.countMinLeadingZeros();
  KnownBits Op1KnownBits = DAG.computeKnownBits(Op1);
  unsigned Op1LeadingZeros = Op1KnownBits.countMinLeadingZeros();
  SDLoc SL(Op);
  if (Op0LeadingZeros >= 32 && Op1LeadingZeros >= 32)
    return SDValue(
        DAG.getMachineNode(AMDGPU::S_MUL_U64_U32_PSEUDO, SL, VT, Op0, Op1), 0);
  unsigned Op0SignBits = DAG.ComputeNumSignBits(Op0);
  unsigned Op1SignBits = DAG.ComputeNumSignBits(Op1);
  if (Op0SignBits >= 33 && Op1SignBits >= 33)
    return SDValue(
        DAG.getMachineNode(AMDGPU::S_MUL_I64_I32_PSEUDO, SL, VT, Op0, Op1), 0);
  // If all the operands are uniform, then we lower s_mul_u64 as it is.
  return Op;
}

SDValue SITargetLowering::lowerXMULO(SDValue Op, SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  SDLoc SL(Op);
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  bool isSigned = Op.getOpcode() == ISD::SMULO;

  if (ConstantSDNode *RHSC = isConstOrConstSplat(RHS)) {
    const APInt &C = RHSC->getAPIntValue();
    // mulo(X, 1 << S) -> { X << S, (X << S) >> S != X }
    if (C.isPowerOf2()) {
      // smulo(x, signed_min) is same as umulo(x, signed_min).
      bool UseArithShift = isSigned && !C.isMinSignedValue();
      SDValue ShiftAmt = DAG.getConstant(C.logBase2(), SL, MVT::i32);
      SDValue Result = DAG.getNode(ISD::SHL, SL, VT, LHS, ShiftAmt);
      SDValue Overflow = DAG.getSetCC(SL, MVT::i1,
          DAG.getNode(UseArithShift ? ISD::SRA : ISD::SRL,
                      SL, VT, Result, ShiftAmt),
          LHS, ISD::SETNE);
      return DAG.getMergeValues({ Result, Overflow }, SL);
    }
  }

  SDValue Result = DAG.getNode(ISD::MUL, SL, VT, LHS, RHS);
  SDValue Top = DAG.getNode(isSigned ? ISD::MULHS : ISD::MULHU,
                            SL, VT, LHS, RHS);

  SDValue Sign = isSigned
    ? DAG.getNode(ISD::SRA, SL, VT, Result,
                  DAG.getConstant(VT.getScalarSizeInBits() - 1, SL, MVT::i32))
    : DAG.getConstant(0, SL, VT);
  SDValue Overflow = DAG.getSetCC(SL, MVT::i1, Top, Sign, ISD::SETNE);

  return DAG.getMergeValues({ Result, Overflow }, SL);
}

SDValue SITargetLowering::lowerXMUL_LOHI(SDValue Op, SelectionDAG &DAG) const {
  if (Op->isDivergent()) {
    // Select to V_MAD_[IU]64_[IU]32.
    return Op;
  }
  if (Subtarget->hasSMulHi()) {
    // Expand to S_MUL_I32 + S_MUL_HI_[IU]32.
    return SDValue();
  }
  // The multiply is uniform but we would have to use V_MUL_HI_[IU]32 to
  // calculate the high part, so we might as well do the whole thing with
  // V_MAD_[IU]64_[IU]32.
  return Op;
}

SDValue SITargetLowering::lowerTRAP(SDValue Op, SelectionDAG &DAG) const {
  if (!Subtarget->isTrapHandlerEnabled() ||
      Subtarget->getTrapHandlerAbi() != GCNSubtarget::TrapHandlerAbi::AMDHSA)
    return lowerTrapEndpgm(Op, DAG);

  return Subtarget->supportsGetDoorbellID() ? lowerTrapHsa(Op, DAG) :
         lowerTrapHsaQueuePtr(Op, DAG);
}

SDValue SITargetLowering::lowerTrapEndpgm(
    SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue Chain = Op.getOperand(0);
  return DAG.getNode(AMDGPUISD::ENDPGM_TRAP, SL, MVT::Other, Chain);
}

SDValue SITargetLowering::loadImplicitKernelArgument(SelectionDAG &DAG, MVT VT,
    const SDLoc &DL, Align Alignment, ImplicitParameter Param) const {
  MachineFunction &MF = DAG.getMachineFunction();
  uint64_t Offset = getImplicitParameterOffset(MF, Param);
  SDValue Ptr = lowerKernArgParameterPtr(DAG, DL, DAG.getEntryNode(), Offset);
  MachinePointerInfo PtrInfo(AMDGPUAS::CONSTANT_ADDRESS);
  return DAG.getLoad(VT, DL, DAG.getEntryNode(), Ptr, PtrInfo, Alignment,
                     MachineMemOperand::MODereferenceable |
                         MachineMemOperand::MOInvariant);
}

SDValue SITargetLowering::lowerTrapHsaQueuePtr(
    SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue Chain = Op.getOperand(0);

  SDValue QueuePtr;
  // For code object version 5, QueuePtr is passed through implicit kernarg.
  const Module *M = DAG.getMachineFunction().getFunction().getParent();
  if (AMDGPU::getAMDHSACodeObjectVersion(*M) >= AMDGPU::AMDHSA_COV5) {
    QueuePtr =
        loadImplicitKernelArgument(DAG, MVT::i64, SL, Align(8), QUEUE_PTR);
  } else {
    MachineFunction &MF = DAG.getMachineFunction();
    SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();
    Register UserSGPR = Info->getQueuePtrUserSGPR();

    if (UserSGPR == AMDGPU::NoRegister) {
      // We probably are in a function incorrectly marked with
      // amdgpu-no-queue-ptr. This is undefined. We don't want to delete the
      // trap, so just use a null pointer.
      QueuePtr = DAG.getConstant(0, SL, MVT::i64);
    } else {
      QueuePtr = CreateLiveInRegister(DAG, &AMDGPU::SReg_64RegClass, UserSGPR,
                                      MVT::i64);
    }
  }

  SDValue SGPR01 = DAG.getRegister(AMDGPU::SGPR0_SGPR1, MVT::i64);
  SDValue ToReg = DAG.getCopyToReg(Chain, SL, SGPR01,
                                   QueuePtr, SDValue());

  uint64_t TrapID = static_cast<uint64_t>(GCNSubtarget::TrapID::LLVMAMDHSATrap);
  SDValue Ops[] = {
    ToReg,
    DAG.getTargetConstant(TrapID, SL, MVT::i16),
    SGPR01,
    ToReg.getValue(1)
  };
  return DAG.getNode(AMDGPUISD::TRAP, SL, MVT::Other, Ops);
}

SDValue SITargetLowering::lowerTrapHsa(
    SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue Chain = Op.getOperand(0);

  // We need to simulate the 's_trap 2' instruction on targets that run in
  // PRIV=1 (where it is treated as a nop).
  if (Subtarget->hasPrivEnabledTrap2NopBug())
    return DAG.getNode(AMDGPUISD::SIMULATED_TRAP, SL, MVT::Other, Chain);

  uint64_t TrapID = static_cast<uint64_t>(GCNSubtarget::TrapID::LLVMAMDHSATrap);
  SDValue Ops[] = {
    Chain,
    DAG.getTargetConstant(TrapID, SL, MVT::i16)
  };
  return DAG.getNode(AMDGPUISD::TRAP, SL, MVT::Other, Ops);
}

SDValue SITargetLowering::lowerDEBUGTRAP(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue Chain = Op.getOperand(0);
  MachineFunction &MF = DAG.getMachineFunction();

  if (!Subtarget->isTrapHandlerEnabled() ||
      Subtarget->getTrapHandlerAbi() != GCNSubtarget::TrapHandlerAbi::AMDHSA) {
    DiagnosticInfoUnsupported NoTrap(MF.getFunction(),
                                     "debugtrap handler not supported",
                                     Op.getDebugLoc(),
                                     DS_Warning);
    LLVMContext &Ctx = MF.getFunction().getContext();
    Ctx.diagnose(NoTrap);
    return Chain;
  }

  uint64_t TrapID = static_cast<uint64_t>(GCNSubtarget::TrapID::LLVMAMDHSADebugTrap);
  SDValue Ops[] = {
    Chain,
    DAG.getTargetConstant(TrapID, SL, MVT::i16)
  };
  return DAG.getNode(AMDGPUISD::TRAP, SL, MVT::Other, Ops);
}

SDValue SITargetLowering::getSegmentAperture(unsigned AS, const SDLoc &DL,
                                             SelectionDAG &DAG) const {
  if (Subtarget->hasApertureRegs()) {
    const unsigned ApertureRegNo = (AS == AMDGPUAS::LOCAL_ADDRESS)
                                       ? AMDGPU::SRC_SHARED_BASE
                                       : AMDGPU::SRC_PRIVATE_BASE;
    // Note: this feature (register) is broken. When used as a 32-bit operand,
    // it returns a wrong value (all zeroes?). The real value is in the upper 32
    // bits.
    //
    // To work around the issue, directly emit a 64 bit mov from this register
    // then extract the high bits. Note that this shouldn't even result in a
    // shift being emitted and simply become a pair of registers (e.g.):
    //    s_mov_b64 s[6:7], src_shared_base
    //    v_mov_b32_e32 v1, s7
    //
    // FIXME: It would be more natural to emit a CopyFromReg here, but then copy
    // coalescing would kick in and it would think it's okay to use the "HI"
    // subregister directly (instead of extracting the HI 32 bits) which is an
    // artificial (unusable) register.
    //  Register TableGen definitions would need an overhaul to get rid of the
    //  artificial "HI" aperture registers and prevent this kind of issue from
    //  happening.
    SDNode *Mov = DAG.getMachineNode(AMDGPU::S_MOV_B64, DL, MVT::i64,
                                     DAG.getRegister(ApertureRegNo, MVT::i64));
    return DAG.getNode(
        ISD::TRUNCATE, DL, MVT::i32,
        DAG.getNode(ISD::SRL, DL, MVT::i64,
                    {SDValue(Mov, 0), DAG.getConstant(32, DL, MVT::i64)}));
  }

  // For code object version 5, private_base and shared_base are passed through
  // implicit kernargs.
  const Module *M = DAG.getMachineFunction().getFunction().getParent();
  if (AMDGPU::getAMDHSACodeObjectVersion(*M) >= AMDGPU::AMDHSA_COV5) {
    ImplicitParameter Param =
        (AS == AMDGPUAS::LOCAL_ADDRESS) ? SHARED_BASE : PRIVATE_BASE;
    return loadImplicitKernelArgument(DAG, MVT::i32, DL, Align(4), Param);
  }

  MachineFunction &MF = DAG.getMachineFunction();
  SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();
  Register UserSGPR = Info->getQueuePtrUserSGPR();
  if (UserSGPR == AMDGPU::NoRegister) {
    // We probably are in a function incorrectly marked with
    // amdgpu-no-queue-ptr. This is undefined.
    return DAG.getUNDEF(MVT::i32);
  }

  SDValue QueuePtr = CreateLiveInRegister(
    DAG, &AMDGPU::SReg_64RegClass, UserSGPR, MVT::i64);

  // Offset into amd_queue_t for group_segment_aperture_base_hi /
  // private_segment_aperture_base_hi.
  uint32_t StructOffset = (AS == AMDGPUAS::LOCAL_ADDRESS) ? 0x40 : 0x44;

  SDValue Ptr =
      DAG.getObjectPtrOffset(DL, QueuePtr, TypeSize::getFixed(StructOffset));

  // TODO: Use custom target PseudoSourceValue.
  // TODO: We should use the value from the IR intrinsic call, but it might not
  // be available and how do we get it?
  MachinePointerInfo PtrInfo(AMDGPUAS::CONSTANT_ADDRESS);
  return DAG.getLoad(MVT::i32, DL, QueuePtr.getValue(1), Ptr, PtrInfo,
                     commonAlignment(Align(64), StructOffset),
                     MachineMemOperand::MODereferenceable |
                         MachineMemOperand::MOInvariant);
}

/// Return true if the value is a known valid address, such that a null check is
/// not necessary.
static bool isKnownNonNull(SDValue Val, SelectionDAG &DAG,
                           const AMDGPUTargetMachine &TM, unsigned AddrSpace) {
  if (isa<FrameIndexSDNode>(Val) || isa<GlobalAddressSDNode>(Val) ||
      isa<BasicBlockSDNode>(Val))
    return true;

  if (auto *ConstVal = dyn_cast<ConstantSDNode>(Val))
    return ConstVal->getSExtValue() != TM.getNullPointerValue(AddrSpace);

  // TODO: Search through arithmetic, handle arguments and loads
  // marked nonnull.
  return false;
}

SDValue SITargetLowering::lowerADDRSPACECAST(SDValue Op,
                                             SelectionDAG &DAG) const {
  SDLoc SL(Op);

  const AMDGPUTargetMachine &TM =
    static_cast<const AMDGPUTargetMachine &>(getTargetMachine());

  unsigned DestAS, SrcAS;
  SDValue Src;
  bool IsNonNull = false;
  if (const auto *ASC = dyn_cast<AddrSpaceCastSDNode>(Op)) {
    SrcAS = ASC->getSrcAddressSpace();
    Src = ASC->getOperand(0);
    DestAS = ASC->getDestAddressSpace();
  } else {
    assert(Op.getOpcode() == ISD::INTRINSIC_WO_CHAIN &&
           Op.getConstantOperandVal(0) ==
               Intrinsic::amdgcn_addrspacecast_nonnull);
    Src = Op->getOperand(1);
    SrcAS = Op->getConstantOperandVal(2);
    DestAS = Op->getConstantOperandVal(3);
    IsNonNull = true;
  }

  SDValue FlatNullPtr = DAG.getConstant(0, SL, MVT::i64);

  // flat -> local/private
  if (SrcAS == AMDGPUAS::FLAT_ADDRESS) {
    if (DestAS == AMDGPUAS::LOCAL_ADDRESS ||
        DestAS == AMDGPUAS::PRIVATE_ADDRESS) {
      SDValue Ptr = DAG.getNode(ISD::TRUNCATE, SL, MVT::i32, Src);

      if (IsNonNull || isKnownNonNull(Op, DAG, TM, SrcAS))
        return Ptr;

      unsigned NullVal = TM.getNullPointerValue(DestAS);
      SDValue SegmentNullPtr = DAG.getConstant(NullVal, SL, MVT::i32);
      SDValue NonNull = DAG.getSetCC(SL, MVT::i1, Src, FlatNullPtr, ISD::SETNE);

      return DAG.getNode(ISD::SELECT, SL, MVT::i32, NonNull, Ptr,
                         SegmentNullPtr);
    }
  }

  // local/private -> flat
  if (DestAS == AMDGPUAS::FLAT_ADDRESS) {
    if (SrcAS == AMDGPUAS::LOCAL_ADDRESS ||
        SrcAS == AMDGPUAS::PRIVATE_ADDRESS) {

      SDValue Aperture = getSegmentAperture(SrcAS, SL, DAG);
      SDValue CvtPtr =
          DAG.getNode(ISD::BUILD_VECTOR, SL, MVT::v2i32, Src, Aperture);
      CvtPtr = DAG.getNode(ISD::BITCAST, SL, MVT::i64, CvtPtr);

      if (IsNonNull || isKnownNonNull(Op, DAG, TM, SrcAS))
        return CvtPtr;

      unsigned NullVal = TM.getNullPointerValue(SrcAS);
      SDValue SegmentNullPtr = DAG.getConstant(NullVal, SL, MVT::i32);

      SDValue NonNull
        = DAG.getSetCC(SL, MVT::i1, Src, SegmentNullPtr, ISD::SETNE);

      return DAG.getNode(ISD::SELECT, SL, MVT::i64, NonNull, CvtPtr,
                         FlatNullPtr);
    }
  }

  if (SrcAS == AMDGPUAS::CONSTANT_ADDRESS_32BIT &&
      Op.getValueType() == MVT::i64) {
    const SIMachineFunctionInfo *Info =
        DAG.getMachineFunction().getInfo<SIMachineFunctionInfo>();
    SDValue Hi = DAG.getConstant(Info->get32BitAddressHighBits(), SL, MVT::i32);
    SDValue Vec = DAG.getNode(ISD::BUILD_VECTOR, SL, MVT::v2i32, Src, Hi);
    return DAG.getNode(ISD::BITCAST, SL, MVT::i64, Vec);
  }

  if (DestAS == AMDGPUAS::CONSTANT_ADDRESS_32BIT &&
      Src.getValueType() == MVT::i64)
    return DAG.getNode(ISD::TRUNCATE, SL, MVT::i32, Src);

  // global <-> flat are no-ops and never emitted.

  const MachineFunction &MF = DAG.getMachineFunction();
  DiagnosticInfoUnsupported InvalidAddrSpaceCast(
    MF.getFunction(), "invalid addrspacecast", SL.getDebugLoc());
  DAG.getContext()->diagnose(InvalidAddrSpaceCast);

  return DAG.getUNDEF(Op->getValueType(0));
}

// This lowers an INSERT_SUBVECTOR by extracting the individual elements from
// the small vector and inserting them into the big vector. That is better than
// the default expansion of doing it via a stack slot. Even though the use of
// the stack slot would be optimized away afterwards, the stack slot itself
// remains.
SDValue SITargetLowering::lowerINSERT_SUBVECTOR(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDValue Vec = Op.getOperand(0);
  SDValue Ins = Op.getOperand(1);
  SDValue Idx = Op.getOperand(2);
  EVT VecVT = Vec.getValueType();
  EVT InsVT = Ins.getValueType();
  EVT EltVT = VecVT.getVectorElementType();
  unsigned InsNumElts = InsVT.getVectorNumElements();
  unsigned IdxVal = Idx->getAsZExtVal();
  SDLoc SL(Op);

  if (EltVT.getScalarSizeInBits() == 16 && IdxVal % 2 == 0) {
    // Insert 32-bit registers at a time.
    assert(InsNumElts % 2 == 0 && "expect legal vector types");

    unsigned VecNumElts = VecVT.getVectorNumElements();
    EVT NewVecVT =
        EVT::getVectorVT(*DAG.getContext(), MVT::i32, VecNumElts / 2);
    EVT NewInsVT = InsNumElts == 2 ? MVT::i32
                                   : EVT::getVectorVT(*DAG.getContext(),
                                                      MVT::i32, InsNumElts / 2);

    Vec = DAG.getNode(ISD::BITCAST, SL, NewVecVT, Vec);
    Ins = DAG.getNode(ISD::BITCAST, SL, NewInsVT, Ins);

    for (unsigned I = 0; I != InsNumElts / 2; ++I) {
      SDValue Elt;
      if (InsNumElts == 2) {
        Elt = Ins;
      } else {
        Elt = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, Ins,
                          DAG.getConstant(I, SL, MVT::i32));
      }
      Vec = DAG.getNode(ISD::INSERT_VECTOR_ELT, SL, NewVecVT, Vec, Elt,
                        DAG.getConstant(IdxVal / 2 + I, SL, MVT::i32));
    }

    return DAG.getNode(ISD::BITCAST, SL, VecVT, Vec);
  }

  for (unsigned I = 0; I != InsNumElts; ++I) {
    SDValue Elt = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, EltVT, Ins,
                              DAG.getConstant(I, SL, MVT::i32));
    Vec = DAG.getNode(ISD::INSERT_VECTOR_ELT, SL, VecVT, Vec, Elt,
                      DAG.getConstant(IdxVal + I, SL, MVT::i32));
  }
  return Vec;
}

SDValue SITargetLowering::lowerINSERT_VECTOR_ELT(SDValue Op,
                                                 SelectionDAG &DAG) const {
  SDValue Vec = Op.getOperand(0);
  SDValue InsVal = Op.getOperand(1);
  SDValue Idx = Op.getOperand(2);
  EVT VecVT = Vec.getValueType();
  EVT EltVT = VecVT.getVectorElementType();
  unsigned VecSize = VecVT.getSizeInBits();
  unsigned EltSize = EltVT.getSizeInBits();
  SDLoc SL(Op);

  // Specially handle the case of v4i16 with static indexing.
  unsigned NumElts = VecVT.getVectorNumElements();
  auto KIdx = dyn_cast<ConstantSDNode>(Idx);
  if (NumElts == 4 && EltSize == 16 && KIdx) {
    SDValue BCVec = DAG.getNode(ISD::BITCAST, SL, MVT::v2i32, Vec);

    SDValue LoHalf = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, BCVec,
                                 DAG.getConstant(0, SL, MVT::i32));
    SDValue HiHalf = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, BCVec,
                                 DAG.getConstant(1, SL, MVT::i32));

    SDValue LoVec = DAG.getNode(ISD::BITCAST, SL, MVT::v2i16, LoHalf);
    SDValue HiVec = DAG.getNode(ISD::BITCAST, SL, MVT::v2i16, HiHalf);

    unsigned Idx = KIdx->getZExtValue();
    bool InsertLo = Idx < 2;
    SDValue InsHalf = DAG.getNode(ISD::INSERT_VECTOR_ELT, SL, MVT::v2i16,
      InsertLo ? LoVec : HiVec,
      DAG.getNode(ISD::BITCAST, SL, MVT::i16, InsVal),
      DAG.getConstant(InsertLo ? Idx : (Idx - 2), SL, MVT::i32));

    InsHalf = DAG.getNode(ISD::BITCAST, SL, MVT::i32, InsHalf);

    SDValue Concat = InsertLo ?
      DAG.getBuildVector(MVT::v2i32, SL, { InsHalf, HiHalf }) :
      DAG.getBuildVector(MVT::v2i32, SL, { LoHalf, InsHalf });

    return DAG.getNode(ISD::BITCAST, SL, VecVT, Concat);
  }

  // Static indexing does not lower to stack access, and hence there is no need
  // for special custom lowering to avoid stack access.
  if (isa<ConstantSDNode>(Idx))
    return SDValue();

  // Avoid stack access for dynamic indexing by custom lowering to
  // v_bfi_b32 (v_bfm_b32 16, (shl idx, 16)), val, vec

  assert(VecSize <= 64 && "Expected target vector size to be <= 64 bits");

  MVT IntVT = MVT::getIntegerVT(VecSize);

  // Convert vector index to bit-index and get the required bit mask.
  assert(isPowerOf2_32(EltSize));
  const auto EltMask = maskTrailingOnes<uint64_t>(EltSize);
  SDValue ScaleFactor = DAG.getConstant(Log2_32(EltSize), SL, MVT::i32);
  SDValue ScaledIdx = DAG.getNode(ISD::SHL, SL, MVT::i32, Idx, ScaleFactor);
  SDValue BFM = DAG.getNode(ISD::SHL, SL, IntVT,
                            DAG.getConstant(EltMask, SL, IntVT), ScaledIdx);

  // 1. Create a congruent vector with the target value in each element.
  SDValue ExtVal = DAG.getNode(ISD::BITCAST, SL, IntVT,
                               DAG.getSplatBuildVector(VecVT, SL, InsVal));

  // 2. Mask off all other indices except the required index within (1).
  SDValue LHS = DAG.getNode(ISD::AND, SL, IntVT, BFM, ExtVal);

  // 3. Mask off the required index within the target vector.
  SDValue BCVec = DAG.getNode(ISD::BITCAST, SL, IntVT, Vec);
  SDValue RHS = DAG.getNode(ISD::AND, SL, IntVT,
                            DAG.getNOT(SL, BFM, IntVT), BCVec);

  // 4. Get (2) and (3) ORed into the target vector.
  SDValue BFI = DAG.getNode(ISD::OR, SL, IntVT, LHS, RHS);

  return DAG.getNode(ISD::BITCAST, SL, VecVT, BFI);
}

SDValue SITargetLowering::lowerEXTRACT_VECTOR_ELT(SDValue Op,
                                                  SelectionDAG &DAG) const {
  SDLoc SL(Op);

  EVT ResultVT = Op.getValueType();
  SDValue Vec = Op.getOperand(0);
  SDValue Idx = Op.getOperand(1);
  EVT VecVT = Vec.getValueType();
  unsigned VecSize = VecVT.getSizeInBits();
  EVT EltVT = VecVT.getVectorElementType();

  DAGCombinerInfo DCI(DAG, AfterLegalizeVectorOps, true, nullptr);

  // Make sure we do any optimizations that will make it easier to fold
  // source modifiers before obscuring it with bit operations.

  // XXX - Why doesn't this get called when vector_shuffle is expanded?
  if (SDValue Combined = performExtractVectorEltCombine(Op.getNode(), DCI))
    return Combined;

  if (VecSize == 128 || VecSize == 256 || VecSize == 512) {
    SDValue Lo, Hi;
    EVT LoVT, HiVT;
    std::tie(LoVT, HiVT) = DAG.GetSplitDestVTs(VecVT);

    if (VecSize == 128) {
      SDValue V2 = DAG.getBitcast(MVT::v2i64, Vec);
      Lo = DAG.getBitcast(LoVT,
                          DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i64, V2,
                                      DAG.getConstant(0, SL, MVT::i32)));
      Hi = DAG.getBitcast(HiVT,
                          DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i64, V2,
                                      DAG.getConstant(1, SL, MVT::i32)));
    } else if (VecSize == 256) {
      SDValue V2 = DAG.getBitcast(MVT::v4i64, Vec);
      SDValue Parts[4];
      for (unsigned P = 0; P < 4; ++P) {
        Parts[P] = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i64, V2,
                               DAG.getConstant(P, SL, MVT::i32));
      }

      Lo = DAG.getBitcast(LoVT, DAG.getNode(ISD::BUILD_VECTOR, SL, MVT::v2i64,
                                            Parts[0], Parts[1]));
      Hi = DAG.getBitcast(HiVT, DAG.getNode(ISD::BUILD_VECTOR, SL, MVT::v2i64,
                                            Parts[2], Parts[3]));
    } else {
      assert(VecSize == 512);

      SDValue V2 = DAG.getBitcast(MVT::v8i64, Vec);
      SDValue Parts[8];
      for (unsigned P = 0; P < 8; ++P) {
        Parts[P] = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i64, V2,
                               DAG.getConstant(P, SL, MVT::i32));
      }

      Lo = DAG.getBitcast(LoVT,
                          DAG.getNode(ISD::BUILD_VECTOR, SL, MVT::v4i64,
                                      Parts[0], Parts[1], Parts[2], Parts[3]));
      Hi = DAG.getBitcast(HiVT,
                          DAG.getNode(ISD::BUILD_VECTOR, SL, MVT::v4i64,
                                      Parts[4], Parts[5],Parts[6], Parts[7]));
    }

    EVT IdxVT = Idx.getValueType();
    unsigned NElem = VecVT.getVectorNumElements();
    assert(isPowerOf2_32(NElem));
    SDValue IdxMask = DAG.getConstant(NElem / 2 - 1, SL, IdxVT);
    SDValue NewIdx = DAG.getNode(ISD::AND, SL, IdxVT, Idx, IdxMask);
    SDValue Half = DAG.getSelectCC(SL, Idx, IdxMask, Hi, Lo, ISD::SETUGT);
    return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, EltVT, Half, NewIdx);
  }

  assert(VecSize <= 64);

  MVT IntVT = MVT::getIntegerVT(VecSize);

  // If Vec is just a SCALAR_TO_VECTOR, then use the scalar integer directly.
  SDValue VecBC = peekThroughBitcasts(Vec);
  if (VecBC.getOpcode() == ISD::SCALAR_TO_VECTOR) {
    SDValue Src = VecBC.getOperand(0);
    Src = DAG.getBitcast(Src.getValueType().changeTypeToInteger(), Src);
    Vec = DAG.getAnyExtOrTrunc(Src, SL, IntVT);
  }

  unsigned EltSize = EltVT.getSizeInBits();
  assert(isPowerOf2_32(EltSize));

  SDValue ScaleFactor = DAG.getConstant(Log2_32(EltSize), SL, MVT::i32);

  // Convert vector index to bit-index (* EltSize)
  SDValue ScaledIdx = DAG.getNode(ISD::SHL, SL, MVT::i32, Idx, ScaleFactor);

  SDValue BC = DAG.getNode(ISD::BITCAST, SL, IntVT, Vec);
  SDValue Elt = DAG.getNode(ISD::SRL, SL, IntVT, BC, ScaledIdx);

  if (ResultVT == MVT::f16 || ResultVT == MVT::bf16) {
    SDValue Result = DAG.getNode(ISD::TRUNCATE, SL, MVT::i16, Elt);
    return DAG.getNode(ISD::BITCAST, SL, ResultVT, Result);
  }

  return DAG.getAnyExtOrTrunc(Elt, SL, ResultVT);
}

static bool elementPairIsContiguous(ArrayRef<int> Mask, int Elt) {
  assert(Elt % 2 == 0);
  return Mask[Elt + 1] == Mask[Elt] + 1 && (Mask[Elt] % 2 == 0);
}

SDValue SITargetLowering::lowerVECTOR_SHUFFLE(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc SL(Op);
  EVT ResultVT = Op.getValueType();
  ShuffleVectorSDNode *SVN = cast<ShuffleVectorSDNode>(Op);

  EVT PackVT = ResultVT.isInteger() ? MVT::v2i16 : MVT::v2f16;
  EVT EltVT = PackVT.getVectorElementType();
  int SrcNumElts = Op.getOperand(0).getValueType().getVectorNumElements();

  // vector_shuffle <0,1,6,7> lhs, rhs
  // -> concat_vectors (extract_subvector lhs, 0), (extract_subvector rhs, 2)
  //
  // vector_shuffle <6,7,2,3> lhs, rhs
  // -> concat_vectors (extract_subvector rhs, 2), (extract_subvector lhs, 2)
  //
  // vector_shuffle <6,7,0,1> lhs, rhs
  // -> concat_vectors (extract_subvector rhs, 2), (extract_subvector lhs, 0)

  // Avoid scalarizing when both halves are reading from consecutive elements.
  SmallVector<SDValue, 4> Pieces;
  for (int I = 0, N = ResultVT.getVectorNumElements(); I != N; I += 2) {
    if (elementPairIsContiguous(SVN->getMask(), I)) {
      const int Idx = SVN->getMaskElt(I);
      int VecIdx = Idx < SrcNumElts ? 0 : 1;
      int EltIdx = Idx < SrcNumElts ? Idx : Idx - SrcNumElts;
      SDValue SubVec = DAG.getNode(ISD::EXTRACT_SUBVECTOR, SL,
                                    PackVT, SVN->getOperand(VecIdx),
                                    DAG.getConstant(EltIdx, SL, MVT::i32));
      Pieces.push_back(SubVec);
    } else {
      const int Idx0 = SVN->getMaskElt(I);
      const int Idx1 = SVN->getMaskElt(I + 1);
      int VecIdx0 = Idx0 < SrcNumElts ? 0 : 1;
      int VecIdx1 = Idx1 < SrcNumElts ? 0 : 1;
      int EltIdx0 = Idx0 < SrcNumElts ? Idx0 : Idx0 - SrcNumElts;
      int EltIdx1 = Idx1 < SrcNumElts ? Idx1 : Idx1 - SrcNumElts;

      SDValue Vec0 = SVN->getOperand(VecIdx0);
      SDValue Elt0 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, EltVT,
                                 Vec0, DAG.getConstant(EltIdx0, SL, MVT::i32));

      SDValue Vec1 = SVN->getOperand(VecIdx1);
      SDValue Elt1 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, EltVT,
                                 Vec1, DAG.getConstant(EltIdx1, SL, MVT::i32));
      Pieces.push_back(DAG.getBuildVector(PackVT, SL, { Elt0, Elt1 }));
    }
  }

  return DAG.getNode(ISD::CONCAT_VECTORS, SL, ResultVT, Pieces);
}

SDValue SITargetLowering::lowerSCALAR_TO_VECTOR(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDValue SVal = Op.getOperand(0);
  EVT ResultVT = Op.getValueType();
  EVT SValVT = SVal.getValueType();
  SDValue UndefVal = DAG.getUNDEF(SValVT);
  SDLoc SL(Op);

  SmallVector<SDValue, 8> VElts;
  VElts.push_back(SVal);
  for (int I = 1, E = ResultVT.getVectorNumElements(); I < E; ++I)
    VElts.push_back(UndefVal);

  return DAG.getBuildVector(ResultVT, SL, VElts);
}

SDValue SITargetLowering::lowerBUILD_VECTOR(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc SL(Op);
  EVT VT = Op.getValueType();

  if (VT == MVT::v4i16 || VT == MVT::v4f16 || VT == MVT::v8i16 ||
      VT == MVT::v8f16 || VT == MVT::v4bf16 || VT == MVT::v8bf16) {
    EVT HalfVT = MVT::getVectorVT(VT.getVectorElementType().getSimpleVT(),
                                  VT.getVectorNumElements() / 2);
    MVT HalfIntVT = MVT::getIntegerVT(HalfVT.getSizeInBits());

    // Turn into pair of packed build_vectors.
    // TODO: Special case for constants that can be materialized with s_mov_b64.
    SmallVector<SDValue, 4> LoOps, HiOps;
    for (unsigned I = 0, E = VT.getVectorNumElements() / 2; I != E; ++I) {
      LoOps.push_back(Op.getOperand(I));
      HiOps.push_back(Op.getOperand(I + E));
    }
    SDValue Lo = DAG.getBuildVector(HalfVT, SL, LoOps);
    SDValue Hi = DAG.getBuildVector(HalfVT, SL, HiOps);

    SDValue CastLo = DAG.getNode(ISD::BITCAST, SL, HalfIntVT, Lo);
    SDValue CastHi = DAG.getNode(ISD::BITCAST, SL, HalfIntVT, Hi);

    SDValue Blend = DAG.getBuildVector(MVT::getVectorVT(HalfIntVT, 2), SL,
                                       { CastLo, CastHi });
    return DAG.getNode(ISD::BITCAST, SL, VT, Blend);
  }

  if (VT == MVT::v16i16 || VT == MVT::v16f16 || VT == MVT::v16bf16) {
    EVT QuarterVT = MVT::getVectorVT(VT.getVectorElementType().getSimpleVT(),
                                     VT.getVectorNumElements() / 4);
    MVT QuarterIntVT = MVT::getIntegerVT(QuarterVT.getSizeInBits());

    SmallVector<SDValue, 4> Parts[4];
    for (unsigned I = 0, E = VT.getVectorNumElements() / 4; I != E; ++I) {
      for (unsigned P = 0; P < 4; ++P)
        Parts[P].push_back(Op.getOperand(I + P * E));
    }
    SDValue Casts[4];
    for (unsigned P = 0; P < 4; ++P) {
      SDValue Vec = DAG.getBuildVector(QuarterVT, SL, Parts[P]);
      Casts[P] = DAG.getNode(ISD::BITCAST, SL, QuarterIntVT, Vec);
    }

    SDValue Blend =
        DAG.getBuildVector(MVT::getVectorVT(QuarterIntVT, 4), SL, Casts);
    return DAG.getNode(ISD::BITCAST, SL, VT, Blend);
  }

  if (VT == MVT::v32i16 || VT == MVT::v32f16 || VT == MVT::v32bf16) {
    EVT QuarterVT = MVT::getVectorVT(VT.getVectorElementType().getSimpleVT(),
                                     VT.getVectorNumElements() / 8);
    MVT QuarterIntVT = MVT::getIntegerVT(QuarterVT.getSizeInBits());

    SmallVector<SDValue, 8> Parts[8];
    for (unsigned I = 0, E = VT.getVectorNumElements() / 8; I != E; ++I) {
      for (unsigned P = 0; P < 8; ++P)
        Parts[P].push_back(Op.getOperand(I + P * E));
    }
    SDValue Casts[8];
    for (unsigned P = 0; P < 8; ++P) {
      SDValue Vec = DAG.getBuildVector(QuarterVT, SL, Parts[P]);
      Casts[P] = DAG.getNode(ISD::BITCAST, SL, QuarterIntVT, Vec);
    }

    SDValue Blend =
        DAG.getBuildVector(MVT::getVectorVT(QuarterIntVT, 8), SL, Casts);
    return DAG.getNode(ISD::BITCAST, SL, VT, Blend);
  }

  assert(VT == MVT::v2f16 || VT == MVT::v2i16 || VT == MVT::v2bf16);
  assert(!Subtarget->hasVOP3PInsts() && "this should be legal");

  SDValue Lo = Op.getOperand(0);
  SDValue Hi = Op.getOperand(1);

  // Avoid adding defined bits with the zero_extend.
  if (Hi.isUndef()) {
    Lo = DAG.getNode(ISD::BITCAST, SL, MVT::i16, Lo);
    SDValue ExtLo = DAG.getNode(ISD::ANY_EXTEND, SL, MVT::i32, Lo);
    return DAG.getNode(ISD::BITCAST, SL, VT, ExtLo);
  }

  Hi = DAG.getNode(ISD::BITCAST, SL, MVT::i16, Hi);
  Hi = DAG.getNode(ISD::ZERO_EXTEND, SL, MVT::i32, Hi);

  SDValue ShlHi = DAG.getNode(ISD::SHL, SL, MVT::i32, Hi,
                              DAG.getConstant(16, SL, MVT::i32));
  if (Lo.isUndef())
    return DAG.getNode(ISD::BITCAST, SL, VT, ShlHi);

  Lo = DAG.getNode(ISD::BITCAST, SL, MVT::i16, Lo);
  Lo = DAG.getNode(ISD::ZERO_EXTEND, SL, MVT::i32, Lo);

  SDValue Or = DAG.getNode(ISD::OR, SL, MVT::i32, Lo, ShlHi);
  return DAG.getNode(ISD::BITCAST, SL, VT, Or);
}

bool
SITargetLowering::isOffsetFoldingLegal(const GlobalAddressSDNode *GA) const {
  // OSes that use ELF REL relocations (instead of RELA) can only store a
  // 32-bit addend in the instruction, so it is not safe to allow offset folding
  // which can create arbitrary 64-bit addends. (This is only a problem for
  // R_AMDGPU_*32_HI relocations since other relocation types are unaffected by
  // the high 32 bits of the addend.)
  //
  // This should be kept in sync with how HasRelocationAddend is initialized in
  // the constructor of ELFAMDGPUAsmBackend.
  if (!Subtarget->isAmdHsaOS())
    return false;

  // We can fold offsets for anything that doesn't require a GOT relocation.
  return (GA->getAddressSpace() == AMDGPUAS::GLOBAL_ADDRESS ||
          GA->getAddressSpace() == AMDGPUAS::CONSTANT_ADDRESS ||
          GA->getAddressSpace() == AMDGPUAS::CONSTANT_ADDRESS_32BIT) &&
         !shouldEmitGOTReloc(GA->getGlobal());
}

static SDValue
buildPCRelGlobalAddress(SelectionDAG &DAG, const GlobalValue *GV,
                        const SDLoc &DL, int64_t Offset, EVT PtrVT,
                        unsigned GAFlags = SIInstrInfo::MO_NONE) {
  assert(isInt<32>(Offset + 4) && "32-bit offset is expected!");
  // In order to support pc-relative addressing, the PC_ADD_REL_OFFSET SDNode is
  // lowered to the following code sequence:
  //
  // For constant address space:
  //   s_getpc_b64 s[0:1]
  //   s_add_u32 s0, s0, $symbol
  //   s_addc_u32 s1, s1, 0
  //
  //   s_getpc_b64 returns the address of the s_add_u32 instruction and then
  //   a fixup or relocation is emitted to replace $symbol with a literal
  //   constant, which is a pc-relative offset from the encoding of the $symbol
  //   operand to the global variable.
  //
  // For global address space:
  //   s_getpc_b64 s[0:1]
  //   s_add_u32 s0, s0, $symbol@{gotpc}rel32@lo
  //   s_addc_u32 s1, s1, $symbol@{gotpc}rel32@hi
  //
  //   s_getpc_b64 returns the address of the s_add_u32 instruction and then
  //   fixups or relocations are emitted to replace $symbol@*@lo and
  //   $symbol@*@hi with lower 32 bits and higher 32 bits of a literal constant,
  //   which is a 64-bit pc-relative offset from the encoding of the $symbol
  //   operand to the global variable.
  SDValue PtrLo = DAG.getTargetGlobalAddress(GV, DL, MVT::i32, Offset, GAFlags);
  SDValue PtrHi;
  if (GAFlags == SIInstrInfo::MO_NONE)
    PtrHi = DAG.getTargetConstant(0, DL, MVT::i32);
  else
    PtrHi = DAG.getTargetGlobalAddress(GV, DL, MVT::i32, Offset, GAFlags + 1);
  return DAG.getNode(AMDGPUISD::PC_ADD_REL_OFFSET, DL, PtrVT, PtrLo, PtrHi);
}

SDValue SITargetLowering::LowerGlobalAddress(AMDGPUMachineFunction *MFI,
                                             SDValue Op,
                                             SelectionDAG &DAG) const {
  GlobalAddressSDNode *GSD = cast<GlobalAddressSDNode>(Op);
  SDLoc DL(GSD);
  EVT PtrVT = Op.getValueType();

  const GlobalValue *GV = GSD->getGlobal();
  if ((GSD->getAddressSpace() == AMDGPUAS::LOCAL_ADDRESS &&
       shouldUseLDSConstAddress(GV)) ||
      GSD->getAddressSpace() == AMDGPUAS::REGION_ADDRESS ||
      GSD->getAddressSpace() == AMDGPUAS::PRIVATE_ADDRESS) {
    if (GSD->getAddressSpace() == AMDGPUAS::LOCAL_ADDRESS &&
        GV->hasExternalLinkage()) {
      Type *Ty = GV->getValueType();
      // HIP uses an unsized array `extern __shared__ T s[]` or similar
      // zero-sized type in other languages to declare the dynamic shared
      // memory which size is not known at the compile time. They will be
      // allocated by the runtime and placed directly after the static
      // allocated ones. They all share the same offset.
      if (DAG.getDataLayout().getTypeAllocSize(Ty).isZero()) {
        assert(PtrVT == MVT::i32 && "32-bit pointer is expected.");
        // Adjust alignment for that dynamic shared memory array.
        Function &F = DAG.getMachineFunction().getFunction();
        MFI->setDynLDSAlign(F, *cast<GlobalVariable>(GV));
        MFI->setUsesDynamicLDS(true);
        return SDValue(
            DAG.getMachineNode(AMDGPU::GET_GROUPSTATICSIZE, DL, PtrVT), 0);
      }
    }
    return AMDGPUTargetLowering::LowerGlobalAddress(MFI, Op, DAG);
  }

  if (GSD->getAddressSpace() == AMDGPUAS::LOCAL_ADDRESS) {
    SDValue GA = DAG.getTargetGlobalAddress(GV, DL, MVT::i32, GSD->getOffset(),
                                            SIInstrInfo::MO_ABS32_LO);
    return DAG.getNode(AMDGPUISD::LDS, DL, MVT::i32, GA);
  }

  if (Subtarget->isAmdPalOS() || Subtarget->isMesa3DOS()) {
    SDValue AddrLo = DAG.getTargetGlobalAddress(
        GV, DL, MVT::i32, GSD->getOffset(), SIInstrInfo::MO_ABS32_LO);
    AddrLo = {DAG.getMachineNode(AMDGPU::S_MOV_B32, DL, MVT::i32, AddrLo), 0};

    SDValue AddrHi = DAG.getTargetGlobalAddress(
        GV, DL, MVT::i32, GSD->getOffset(), SIInstrInfo::MO_ABS32_HI);
    AddrHi = {DAG.getMachineNode(AMDGPU::S_MOV_B32, DL, MVT::i32, AddrHi), 0};

    return DAG.getNode(ISD::BUILD_PAIR, DL, MVT::i64, AddrLo, AddrHi);
  }

  if (shouldEmitFixup(GV))
    return buildPCRelGlobalAddress(DAG, GV, DL, GSD->getOffset(), PtrVT);

  if (shouldEmitPCReloc(GV))
    return buildPCRelGlobalAddress(DAG, GV, DL, GSD->getOffset(), PtrVT,
                                   SIInstrInfo::MO_REL32);

  SDValue GOTAddr = buildPCRelGlobalAddress(DAG, GV, DL, 0, PtrVT,
                                            SIInstrInfo::MO_GOTPCREL32);

  Type *Ty = PtrVT.getTypeForEVT(*DAG.getContext());
  PointerType *PtrTy = PointerType::get(Ty, AMDGPUAS::CONSTANT_ADDRESS);
  const DataLayout &DataLayout = DAG.getDataLayout();
  Align Alignment = DataLayout.getABITypeAlign(PtrTy);
  MachinePointerInfo PtrInfo
    = MachinePointerInfo::getGOT(DAG.getMachineFunction());

  return DAG.getLoad(PtrVT, DL, DAG.getEntryNode(), GOTAddr, PtrInfo, Alignment,
                     MachineMemOperand::MODereferenceable |
                         MachineMemOperand::MOInvariant);
}

SDValue SITargetLowering::copyToM0(SelectionDAG &DAG, SDValue Chain,
                                   const SDLoc &DL, SDValue V) const {
  // We can't use S_MOV_B32 directly, because there is no way to specify m0 as
  // the destination register.
  //
  // We can't use CopyToReg, because MachineCSE won't combine COPY instructions,
  // so we will end up with redundant moves to m0.
  //
  // We use a pseudo to ensure we emit s_mov_b32 with m0 as the direct result.

  // A Null SDValue creates a glue result.
  SDNode *M0 = DAG.getMachineNode(AMDGPU::SI_INIT_M0, DL, MVT::Other, MVT::Glue,
                                  V, Chain);
  return SDValue(M0, 0);
}

SDValue SITargetLowering::lowerImplicitZextParam(SelectionDAG &DAG,
                                                 SDValue Op,
                                                 MVT VT,
                                                 unsigned Offset) const {
  SDLoc SL(Op);
  SDValue Param = lowerKernargMemParameter(
      DAG, MVT::i32, MVT::i32, SL, DAG.getEntryNode(), Offset, Align(4), false);
  // The local size values will have the hi 16-bits as zero.
  return DAG.getNode(ISD::AssertZext, SL, MVT::i32, Param,
                     DAG.getValueType(VT));
}

static SDValue emitNonHSAIntrinsicError(SelectionDAG &DAG, const SDLoc &DL,
                                        EVT VT) {
  DiagnosticInfoUnsupported BadIntrin(DAG.getMachineFunction().getFunction(),
                                      "non-hsa intrinsic with hsa target",
                                      DL.getDebugLoc());
  DAG.getContext()->diagnose(BadIntrin);
  return DAG.getUNDEF(VT);
}

static SDValue emitRemovedIntrinsicError(SelectionDAG &DAG, const SDLoc &DL,
                                         EVT VT) {
  DiagnosticInfoUnsupported BadIntrin(DAG.getMachineFunction().getFunction(),
                                      "intrinsic not supported on subtarget",
                                      DL.getDebugLoc());
  DAG.getContext()->diagnose(BadIntrin);
  return DAG.getUNDEF(VT);
}

static SDValue getBuildDwordsVector(SelectionDAG &DAG, SDLoc DL,
                                    ArrayRef<SDValue> Elts) {
  assert(!Elts.empty());
  MVT Type;
  unsigned NumElts = Elts.size();

  if (NumElts <= 12) {
    Type = MVT::getVectorVT(MVT::f32, NumElts);
  } else {
    assert(Elts.size() <= 16);
    Type = MVT::v16f32;
    NumElts = 16;
  }

  SmallVector<SDValue, 16> VecElts(NumElts);
  for (unsigned i = 0; i < Elts.size(); ++i) {
    SDValue Elt = Elts[i];
    if (Elt.getValueType() != MVT::f32)
      Elt = DAG.getBitcast(MVT::f32, Elt);
    VecElts[i] = Elt;
  }
  for (unsigned i = Elts.size(); i < NumElts; ++i)
    VecElts[i] = DAG.getUNDEF(MVT::f32);

  if (NumElts == 1)
    return VecElts[0];
  return DAG.getBuildVector(Type, DL, VecElts);
}

static SDValue padEltsToUndef(SelectionDAG &DAG, const SDLoc &DL, EVT CastVT,
                              SDValue Src, int ExtraElts) {
  EVT SrcVT = Src.getValueType();

  SmallVector<SDValue, 8> Elts;

  if (SrcVT.isVector())
    DAG.ExtractVectorElements(Src, Elts);
  else
    Elts.push_back(Src);

  SDValue Undef = DAG.getUNDEF(SrcVT.getScalarType());
  while (ExtraElts--)
    Elts.push_back(Undef);

  return DAG.getBuildVector(CastVT, DL, Elts);
}

// Re-construct the required return value for a image load intrinsic.
// This is more complicated due to the optional use TexFailCtrl which means the required
// return type is an aggregate
static SDValue constructRetValue(SelectionDAG &DAG, MachineSDNode *Result,
                                 ArrayRef<EVT> ResultTypes, bool IsTexFail,
                                 bool Unpacked, bool IsD16, int DMaskPop,
                                 int NumVDataDwords, bool IsAtomicPacked16Bit,
                                 const SDLoc &DL) {
  // Determine the required return type. This is the same regardless of IsTexFail flag
  EVT ReqRetVT = ResultTypes[0];
  int ReqRetNumElts = ReqRetVT.isVector() ? ReqRetVT.getVectorNumElements() : 1;
  int NumDataDwords = ((IsD16 && !Unpacked) || IsAtomicPacked16Bit)
                          ? (ReqRetNumElts + 1) / 2
                          : ReqRetNumElts;

  int MaskPopDwords = (!IsD16 || Unpacked) ? DMaskPop : (DMaskPop + 1) / 2;

  MVT DataDwordVT = NumDataDwords == 1 ?
    MVT::i32 : MVT::getVectorVT(MVT::i32, NumDataDwords);

  MVT MaskPopVT = MaskPopDwords == 1 ?
    MVT::i32 : MVT::getVectorVT(MVT::i32, MaskPopDwords);

  SDValue Data(Result, 0);
  SDValue TexFail;

  if (DMaskPop > 0 && Data.getValueType() != MaskPopVT) {
    SDValue ZeroIdx = DAG.getConstant(0, DL, MVT::i32);
    if (MaskPopVT.isVector()) {
      Data = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, MaskPopVT,
                         SDValue(Result, 0), ZeroIdx);
    } else {
      Data = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MaskPopVT,
                         SDValue(Result, 0), ZeroIdx);
    }
  }

  if (DataDwordVT.isVector() && !IsAtomicPacked16Bit)
    Data = padEltsToUndef(DAG, DL, DataDwordVT, Data,
                          NumDataDwords - MaskPopDwords);

  if (IsD16)
    Data = adjustLoadValueTypeImpl(Data, ReqRetVT, DL, DAG, Unpacked);

  EVT LegalReqRetVT = ReqRetVT;
  if (!ReqRetVT.isVector()) {
    if (!Data.getValueType().isInteger())
      Data = DAG.getNode(ISD::BITCAST, DL,
                         Data.getValueType().changeTypeToInteger(), Data);
    Data = DAG.getNode(ISD::TRUNCATE, DL, ReqRetVT.changeTypeToInteger(), Data);
  } else {
    // We need to widen the return vector to a legal type
    if ((ReqRetVT.getVectorNumElements() % 2) == 1 &&
        ReqRetVT.getVectorElementType().getSizeInBits() == 16) {
      LegalReqRetVT =
          EVT::getVectorVT(*DAG.getContext(), ReqRetVT.getVectorElementType(),
                           ReqRetVT.getVectorNumElements() + 1);
    }
  }
  Data = DAG.getNode(ISD::BITCAST, DL, LegalReqRetVT, Data);

  if (IsTexFail) {
    TexFail =
        DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::i32, SDValue(Result, 0),
                    DAG.getConstant(MaskPopDwords, DL, MVT::i32));

    return DAG.getMergeValues({Data, TexFail, SDValue(Result, 1)}, DL);
  }

  if (Result->getNumValues() == 1)
    return Data;

  return DAG.getMergeValues({Data, SDValue(Result, 1)}, DL);
}

static bool parseTexFail(SDValue TexFailCtrl, SelectionDAG &DAG, SDValue *TFE,
                         SDValue *LWE, bool &IsTexFail) {
  auto TexFailCtrlConst = cast<ConstantSDNode>(TexFailCtrl.getNode());

  uint64_t Value = TexFailCtrlConst->getZExtValue();
  if (Value) {
    IsTexFail = true;
  }

  SDLoc DL(TexFailCtrlConst);
  *TFE = DAG.getTargetConstant((Value & 0x1) ? 1 : 0, DL, MVT::i32);
  Value &= ~(uint64_t)0x1;
  *LWE = DAG.getTargetConstant((Value & 0x2) ? 1 : 0, DL, MVT::i32);
  Value &= ~(uint64_t)0x2;

  return Value == 0;
}

static void packImage16bitOpsToDwords(SelectionDAG &DAG, SDValue Op,
                                      MVT PackVectorVT,
                                      SmallVectorImpl<SDValue> &PackedAddrs,
                                      unsigned DimIdx, unsigned EndIdx,
                                      unsigned NumGradients) {
  SDLoc DL(Op);
  for (unsigned I = DimIdx; I < EndIdx; I++) {
    SDValue Addr = Op.getOperand(I);

    // Gradients are packed with undef for each coordinate.
    // In <hi 16 bit>,<lo 16 bit> notation, the registers look like this:
    // 1D: undef,dx/dh; undef,dx/dv
    // 2D: dy/dh,dx/dh; dy/dv,dx/dv
    // 3D: dy/dh,dx/dh; undef,dz/dh; dy/dv,dx/dv; undef,dz/dv
    if (((I + 1) >= EndIdx) ||
        ((NumGradients / 2) % 2 == 1 && (I == DimIdx + (NumGradients / 2) - 1 ||
                                         I == DimIdx + NumGradients - 1))) {
      if (Addr.getValueType() != MVT::i16)
        Addr = DAG.getBitcast(MVT::i16, Addr);
      Addr = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i32, Addr);
    } else {
      Addr = DAG.getBuildVector(PackVectorVT, DL, {Addr, Op.getOperand(I + 1)});
      I++;
    }
    Addr = DAG.getBitcast(MVT::f32, Addr);
    PackedAddrs.push_back(Addr);
  }
}

SDValue SITargetLowering::lowerImage(SDValue Op,
                                     const AMDGPU::ImageDimIntrinsicInfo *Intr,
                                     SelectionDAG &DAG, bool WithChain) const {
  SDLoc DL(Op);
  MachineFunction &MF = DAG.getMachineFunction();
  const GCNSubtarget* ST = &MF.getSubtarget<GCNSubtarget>();
  const AMDGPU::MIMGBaseOpcodeInfo *BaseOpcode =
      AMDGPU::getMIMGBaseOpcodeInfo(Intr->BaseOpcode);
  const AMDGPU::MIMGDimInfo *DimInfo = AMDGPU::getMIMGDimInfo(Intr->Dim);
  unsigned IntrOpcode = Intr->BaseOpcode;
  bool IsGFX10Plus = AMDGPU::isGFX10Plus(*Subtarget);
  bool IsGFX11Plus = AMDGPU::isGFX11Plus(*Subtarget);
  bool IsGFX12Plus = AMDGPU::isGFX12Plus(*Subtarget);

  SmallVector<EVT, 3> ResultTypes(Op->values());
  SmallVector<EVT, 3> OrigResultTypes(Op->values());
  bool IsD16 = false;
  bool IsG16 = false;
  bool IsA16 = false;
  SDValue VData;
  int NumVDataDwords = 0;
  bool AdjustRetType = false;
  bool IsAtomicPacked16Bit = false;

  // Offset of intrinsic arguments
  const unsigned ArgOffset = WithChain ? 2 : 1;

  unsigned DMask;
  unsigned DMaskLanes = 0;

  if (BaseOpcode->Atomic) {
    VData = Op.getOperand(2);

    IsAtomicPacked16Bit =
        (Intr->BaseOpcode == AMDGPU::IMAGE_ATOMIC_PK_ADD_F16 ||
         Intr->BaseOpcode == AMDGPU::IMAGE_ATOMIC_PK_ADD_BF16);

    bool Is64Bit = VData.getValueSizeInBits() == 64;
    if (BaseOpcode->AtomicX2) {
      SDValue VData2 = Op.getOperand(3);
      VData = DAG.getBuildVector(Is64Bit ? MVT::v2i64 : MVT::v2i32, DL,
                                 {VData, VData2});
      if (Is64Bit)
        VData = DAG.getBitcast(MVT::v4i32, VData);

      ResultTypes[0] = Is64Bit ? MVT::v2i64 : MVT::v2i32;
      DMask = Is64Bit ? 0xf : 0x3;
      NumVDataDwords = Is64Bit ? 4 : 2;
    } else {
      DMask = Is64Bit ? 0x3 : 0x1;
      NumVDataDwords = Is64Bit ? 2 : 1;
    }
  } else {
    DMask = Op->getConstantOperandVal(ArgOffset + Intr->DMaskIndex);
    DMaskLanes = BaseOpcode->Gather4 ? 4 : llvm::popcount(DMask);

    if (BaseOpcode->Store) {
      VData = Op.getOperand(2);

      MVT StoreVT = VData.getSimpleValueType();
      if (StoreVT.getScalarType() == MVT::f16) {
        if (!Subtarget->hasD16Images() || !BaseOpcode->HasD16)
          return Op; // D16 is unsupported for this instruction

        IsD16 = true;
        VData = handleD16VData(VData, DAG, true);
      }

      NumVDataDwords = (VData.getValueType().getSizeInBits() + 31) / 32;
    } else if (!BaseOpcode->NoReturn) {
      // Work out the num dwords based on the dmask popcount and underlying type
      // and whether packing is supported.
      MVT LoadVT = ResultTypes[0].getSimpleVT();
      if (LoadVT.getScalarType() == MVT::f16) {
        if (!Subtarget->hasD16Images() || !BaseOpcode->HasD16)
          return Op; // D16 is unsupported for this instruction

        IsD16 = true;
      }

      // Confirm that the return type is large enough for the dmask specified
      if ((LoadVT.isVector() && LoadVT.getVectorNumElements() < DMaskLanes) ||
          (!LoadVT.isVector() && DMaskLanes > 1))
          return Op;

      // The sq block of gfx8 and gfx9 do not estimate register use correctly
      // for d16 image_gather4, image_gather4_l, and image_gather4_lz
      // instructions.
      if (IsD16 && !Subtarget->hasUnpackedD16VMem() &&
          !(BaseOpcode->Gather4 && Subtarget->hasImageGather4D16Bug()))
        NumVDataDwords = (DMaskLanes + 1) / 2;
      else
        NumVDataDwords = DMaskLanes;

      AdjustRetType = true;
    }
  }

  unsigned VAddrEnd = ArgOffset + Intr->VAddrEnd;
  SmallVector<SDValue, 4> VAddrs;

  // Check for 16 bit addresses or derivatives and pack if true.
  MVT VAddrVT =
      Op.getOperand(ArgOffset + Intr->GradientStart).getSimpleValueType();
  MVT VAddrScalarVT = VAddrVT.getScalarType();
  MVT GradPackVectorVT = VAddrScalarVT == MVT::f16 ? MVT::v2f16 : MVT::v2i16;
  IsG16 = VAddrScalarVT == MVT::f16 || VAddrScalarVT == MVT::i16;

  VAddrVT = Op.getOperand(ArgOffset + Intr->CoordStart).getSimpleValueType();
  VAddrScalarVT = VAddrVT.getScalarType();
  MVT AddrPackVectorVT = VAddrScalarVT == MVT::f16 ? MVT::v2f16 : MVT::v2i16;
  IsA16 = VAddrScalarVT == MVT::f16 || VAddrScalarVT == MVT::i16;

  // Push back extra arguments.
  for (unsigned I = Intr->VAddrStart; I < Intr->GradientStart; I++) {
    if (IsA16 && (Op.getOperand(ArgOffset + I).getValueType() == MVT::f16)) {
      assert(I == Intr->BiasIndex && "Got unexpected 16-bit extra argument");
      // Special handling of bias when A16 is on. Bias is of type half but
      // occupies full 32-bit.
      SDValue Bias = DAG.getBuildVector(
          MVT::v2f16, DL,
          {Op.getOperand(ArgOffset + I), DAG.getUNDEF(MVT::f16)});
      VAddrs.push_back(Bias);
    } else {
      assert((!IsA16 || Intr->NumBiasArgs == 0 || I != Intr->BiasIndex) &&
             "Bias needs to be converted to 16 bit in A16 mode");
      VAddrs.push_back(Op.getOperand(ArgOffset + I));
    }
  }

  if (BaseOpcode->Gradients && !ST->hasG16() && (IsA16 != IsG16)) {
    // 16 bit gradients are supported, but are tied to the A16 control
    // so both gradients and addresses must be 16 bit
    LLVM_DEBUG(
        dbgs() << "Failed to lower image intrinsic: 16 bit addresses "
                  "require 16 bit args for both gradients and addresses");
    return Op;
  }

  if (IsA16) {
    if (!ST->hasA16()) {
      LLVM_DEBUG(dbgs() << "Failed to lower image intrinsic: Target does not "
                           "support 16 bit addresses\n");
      return Op;
    }
  }

  // We've dealt with incorrect input so we know that if IsA16, IsG16
  // are set then we have to compress/pack operands (either address,
  // gradient or both)
  // In the case where a16 and gradients are tied (no G16 support) then we
  // have already verified that both IsA16 and IsG16 are true
  if (BaseOpcode->Gradients && IsG16 && ST->hasG16()) {
    // Activate g16
    const AMDGPU::MIMGG16MappingInfo *G16MappingInfo =
        AMDGPU::getMIMGG16MappingInfo(Intr->BaseOpcode);
    IntrOpcode = G16MappingInfo->G16; // set new opcode to variant with _g16
  }

  // Add gradients (packed or unpacked)
  if (IsG16) {
    // Pack the gradients
    // const int PackEndIdx = IsA16 ? VAddrEnd : (ArgOffset + Intr->CoordStart);
    packImage16bitOpsToDwords(DAG, Op, GradPackVectorVT, VAddrs,
                              ArgOffset + Intr->GradientStart,
                              ArgOffset + Intr->CoordStart, Intr->NumGradients);
  } else {
    for (unsigned I = ArgOffset + Intr->GradientStart;
         I < ArgOffset + Intr->CoordStart; I++)
      VAddrs.push_back(Op.getOperand(I));
  }

  // Add addresses (packed or unpacked)
  if (IsA16) {
    packImage16bitOpsToDwords(DAG, Op, AddrPackVectorVT, VAddrs,
                              ArgOffset + Intr->CoordStart, VAddrEnd,
                              0 /* No gradients */);
  } else {
    // Add uncompressed address
    for (unsigned I = ArgOffset + Intr->CoordStart; I < VAddrEnd; I++)
      VAddrs.push_back(Op.getOperand(I));
  }

  // If the register allocator cannot place the address registers contiguously
  // without introducing moves, then using the non-sequential address encoding
  // is always preferable, since it saves VALU instructions and is usually a
  // wash in terms of code size or even better.
  //
  // However, we currently have no way of hinting to the register allocator that
  // MIMG addresses should be placed contiguously when it is possible to do so,
  // so force non-NSA for the common 2-address case as a heuristic.
  //
  // SIShrinkInstructions will convert NSA encodings to non-NSA after register
  // allocation when possible.
  //
  // Partial NSA is allowed on GFX11+ where the final register is a contiguous
  // set of the remaining addresses.
  const unsigned NSAMaxSize = ST->getNSAMaxSize(BaseOpcode->Sampler);
  const bool HasPartialNSAEncoding = ST->hasPartialNSAEncoding();
  const bool UseNSA = ST->hasNSAEncoding() &&
                      VAddrs.size() >= ST->getNSAThreshold(MF) &&
                      (VAddrs.size() <= NSAMaxSize || HasPartialNSAEncoding);
  const bool UsePartialNSA =
      UseNSA && HasPartialNSAEncoding && VAddrs.size() > NSAMaxSize;

  SDValue VAddr;
  if (UsePartialNSA) {
    VAddr = getBuildDwordsVector(DAG, DL,
                                 ArrayRef(VAddrs).drop_front(NSAMaxSize - 1));
  }
  else if (!UseNSA) {
    VAddr = getBuildDwordsVector(DAG, DL, VAddrs);
  }

  SDValue True = DAG.getTargetConstant(1, DL, MVT::i1);
  SDValue False = DAG.getTargetConstant(0, DL, MVT::i1);
  SDValue Unorm;
  if (!BaseOpcode->Sampler) {
    Unorm = True;
  } else {
    uint64_t UnormConst =
        Op.getConstantOperandVal(ArgOffset + Intr->UnormIndex);

    Unorm = UnormConst ? True : False;
  }

  SDValue TFE;
  SDValue LWE;
  SDValue TexFail = Op.getOperand(ArgOffset + Intr->TexFailCtrlIndex);
  bool IsTexFail = false;
  if (!parseTexFail(TexFail, DAG, &TFE, &LWE, IsTexFail))
    return Op;

  if (IsTexFail) {
    if (!DMaskLanes) {
      // Expecting to get an error flag since TFC is on - and dmask is 0
      // Force dmask to be at least 1 otherwise the instruction will fail
      DMask = 0x1;
      DMaskLanes = 1;
      NumVDataDwords = 1;
    }
    NumVDataDwords += 1;
    AdjustRetType = true;
  }

  // Has something earlier tagged that the return type needs adjusting
  // This happens if the instruction is a load or has set TexFailCtrl flags
  if (AdjustRetType) {
    // NumVDataDwords reflects the true number of dwords required in the return type
    if (DMaskLanes == 0 && !BaseOpcode->Store) {
      // This is a no-op load. This can be eliminated
      SDValue Undef = DAG.getUNDEF(Op.getValueType());
      if (isa<MemSDNode>(Op))
        return DAG.getMergeValues({Undef, Op.getOperand(0)}, DL);
      return Undef;
    }

    EVT NewVT = NumVDataDwords > 1 ?
                  EVT::getVectorVT(*DAG.getContext(), MVT::i32, NumVDataDwords)
                : MVT::i32;

    ResultTypes[0] = NewVT;
    if (ResultTypes.size() == 3) {
      // Original result was aggregate type used for TexFailCtrl results
      // The actual instruction returns as a vector type which has now been
      // created. Remove the aggregate result.
      ResultTypes.erase(&ResultTypes[1]);
    }
  }

  unsigned CPol = Op.getConstantOperandVal(ArgOffset + Intr->CachePolicyIndex);
  if (BaseOpcode->Atomic)
    CPol |= AMDGPU::CPol::GLC; // TODO no-return optimization
  if (CPol & ~((IsGFX12Plus ? AMDGPU::CPol::ALL : AMDGPU::CPol::ALL_pregfx12) |
               AMDGPU::CPol::VOLATILE))
    return Op;

  SmallVector<SDValue, 26> Ops;
  if (BaseOpcode->Store || BaseOpcode->Atomic)
    Ops.push_back(VData); // vdata
  if (UsePartialNSA) {
    append_range(Ops, ArrayRef(VAddrs).take_front(NSAMaxSize - 1));
    Ops.push_back(VAddr);
  }
  else if (UseNSA)
    append_range(Ops, VAddrs);
  else
    Ops.push_back(VAddr);
  Ops.push_back(Op.getOperand(ArgOffset + Intr->RsrcIndex));
  if (BaseOpcode->Sampler)
    Ops.push_back(Op.getOperand(ArgOffset + Intr->SampIndex));
  Ops.push_back(DAG.getTargetConstant(DMask, DL, MVT::i32));
  if (IsGFX10Plus)
    Ops.push_back(DAG.getTargetConstant(DimInfo->Encoding, DL, MVT::i32));
  if (!IsGFX12Plus || BaseOpcode->Sampler || BaseOpcode->MSAA)
    Ops.push_back(Unorm);
  Ops.push_back(DAG.getTargetConstant(CPol, DL, MVT::i32));
  Ops.push_back(IsA16 &&  // r128, a16 for gfx9
                ST->hasFeature(AMDGPU::FeatureR128A16) ? True : False);
  if (IsGFX10Plus)
    Ops.push_back(IsA16 ? True : False);
  if (!Subtarget->hasGFX90AInsts()) {
    Ops.push_back(TFE); //tfe
  } else if (TFE->getAsZExtVal()) {
    report_fatal_error("TFE is not supported on this GPU");
  }
  if (!IsGFX12Plus || BaseOpcode->Sampler || BaseOpcode->MSAA)
    Ops.push_back(LWE); // lwe
  if (!IsGFX10Plus)
    Ops.push_back(DimInfo->DA ? True : False);
  if (BaseOpcode->HasD16)
    Ops.push_back(IsD16 ? True : False);
  if (isa<MemSDNode>(Op))
    Ops.push_back(Op.getOperand(0)); // chain

  int NumVAddrDwords =
      UseNSA ? VAddrs.size() : VAddr.getValueType().getSizeInBits() / 32;
  int Opcode = -1;

  if (IsGFX12Plus) {
    Opcode = AMDGPU::getMIMGOpcode(IntrOpcode, AMDGPU::MIMGEncGfx12,
                                   NumVDataDwords, NumVAddrDwords);
  } else if (IsGFX11Plus) {
    Opcode = AMDGPU::getMIMGOpcode(IntrOpcode,
                                   UseNSA ? AMDGPU::MIMGEncGfx11NSA
                                          : AMDGPU::MIMGEncGfx11Default,
                                   NumVDataDwords, NumVAddrDwords);
  } else if (IsGFX10Plus) {
    Opcode = AMDGPU::getMIMGOpcode(IntrOpcode,
                                   UseNSA ? AMDGPU::MIMGEncGfx10NSA
                                          : AMDGPU::MIMGEncGfx10Default,
                                   NumVDataDwords, NumVAddrDwords);
  } else {
    if (Subtarget->hasGFX90AInsts()) {
      Opcode = AMDGPU::getMIMGOpcode(IntrOpcode, AMDGPU::MIMGEncGfx90a,
                                     NumVDataDwords, NumVAddrDwords);
      if (Opcode == -1)
        report_fatal_error(
            "requested image instruction is not supported on this GPU");
    }
    if (Opcode == -1 &&
        Subtarget->getGeneration() >= AMDGPUSubtarget::VOLCANIC_ISLANDS)
      Opcode = AMDGPU::getMIMGOpcode(IntrOpcode, AMDGPU::MIMGEncGfx8,
                                     NumVDataDwords, NumVAddrDwords);
    if (Opcode == -1)
      Opcode = AMDGPU::getMIMGOpcode(IntrOpcode, AMDGPU::MIMGEncGfx6,
                                     NumVDataDwords, NumVAddrDwords);
  }
  if (Opcode == -1)
    return Op;

  MachineSDNode *NewNode = DAG.getMachineNode(Opcode, DL, ResultTypes, Ops);
  if (auto MemOp = dyn_cast<MemSDNode>(Op)) {
    MachineMemOperand *MemRef = MemOp->getMemOperand();
    DAG.setNodeMemRefs(NewNode, {MemRef});
  }

  if (BaseOpcode->AtomicX2) {
    SmallVector<SDValue, 1> Elt;
    DAG.ExtractVectorElements(SDValue(NewNode, 0), Elt, 0, 1);
    return DAG.getMergeValues({Elt[0], SDValue(NewNode, 1)}, DL);
  }
  if (BaseOpcode->NoReturn)
    return SDValue(NewNode, 0);
  return constructRetValue(DAG, NewNode, OrigResultTypes, IsTexFail,
                           Subtarget->hasUnpackedD16VMem(), IsD16, DMaskLanes,
                           NumVDataDwords, IsAtomicPacked16Bit, DL);
}

SDValue SITargetLowering::lowerSBuffer(EVT VT, SDLoc DL, SDValue Rsrc,
                                       SDValue Offset, SDValue CachePolicy,
                                       SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();

  const DataLayout &DataLayout = DAG.getDataLayout();
  Align Alignment =
      DataLayout.getABITypeAlign(VT.getTypeForEVT(*DAG.getContext()));

  MachineMemOperand *MMO = MF.getMachineMemOperand(
      MachinePointerInfo(),
      MachineMemOperand::MOLoad | MachineMemOperand::MODereferenceable |
          MachineMemOperand::MOInvariant,
      VT.getStoreSize(), Alignment);

  if (!Offset->isDivergent()) {
    SDValue Ops[] = {Rsrc, Offset, CachePolicy};

    // Lower llvm.amdgcn.s.buffer.load.{i16, u16} intrinsics. Initially, the
    // s_buffer_load_u16 instruction is emitted for both signed and unsigned
    // loads. Later, DAG combiner tries to combine s_buffer_load_u16 with sext
    // and generates s_buffer_load_i16 (performSignExtendInRegCombine).
    if (VT == MVT::i16 && Subtarget->hasScalarSubwordLoads()) {
      SDValue BufferLoad =
          DAG.getMemIntrinsicNode(AMDGPUISD::SBUFFER_LOAD_USHORT, DL,
                                  DAG.getVTList(MVT::i32), Ops, VT, MMO);
      return DAG.getNode(ISD::TRUNCATE, DL, VT, BufferLoad);
    }

    // Widen vec3 load to vec4.
    if (VT.isVector() && VT.getVectorNumElements() == 3 &&
        !Subtarget->hasScalarDwordx3Loads()) {
      EVT WidenedVT =
          EVT::getVectorVT(*DAG.getContext(), VT.getVectorElementType(), 4);
      auto WidenedOp = DAG.getMemIntrinsicNode(
          AMDGPUISD::SBUFFER_LOAD, DL, DAG.getVTList(WidenedVT), Ops, WidenedVT,
          MF.getMachineMemOperand(MMO, 0, WidenedVT.getStoreSize()));
      auto Subvector = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, VT, WidenedOp,
                                   DAG.getVectorIdxConstant(0, DL));
      return Subvector;
    }

    return DAG.getMemIntrinsicNode(AMDGPUISD::SBUFFER_LOAD, DL,
                                   DAG.getVTList(VT), Ops, VT, MMO);
  }

  // We have a divergent offset. Emit a MUBUF buffer load instead. We can
  // assume that the buffer is unswizzled.
  SDValue Ops[] = {
      DAG.getEntryNode(),                    // Chain
      Rsrc,                                  // rsrc
      DAG.getConstant(0, DL, MVT::i32),      // vindex
      {},                                    // voffset
      {},                                    // soffset
      {},                                    // offset
      CachePolicy,                           // cachepolicy
      DAG.getTargetConstant(0, DL, MVT::i1), // idxen
  };
  if (VT == MVT::i16 && Subtarget->hasScalarSubwordLoads()) {
    setBufferOffsets(Offset, DAG, &Ops[3], Align(4));
    return handleByteShortBufferLoads(DAG, VT, DL, Ops, MMO);
  }

  SmallVector<SDValue, 4> Loads;
  unsigned NumLoads = 1;
  MVT LoadVT = VT.getSimpleVT();
  unsigned NumElts = LoadVT.isVector() ? LoadVT.getVectorNumElements() : 1;
  assert((LoadVT.getScalarType() == MVT::i32 ||
          LoadVT.getScalarType() == MVT::f32));

  if (NumElts == 8 || NumElts == 16) {
    NumLoads = NumElts / 4;
    LoadVT = MVT::getVectorVT(LoadVT.getScalarType(), 4);
  }

  SDVTList VTList = DAG.getVTList({LoadVT, MVT::Glue});

  // Use the alignment to ensure that the required offsets will fit into the
  // immediate offsets.
  setBufferOffsets(Offset, DAG, &Ops[3],
                   NumLoads > 1 ? Align(16 * NumLoads) : Align(4));

  uint64_t InstOffset = Ops[5]->getAsZExtVal();
  for (unsigned i = 0; i < NumLoads; ++i) {
    Ops[5] = DAG.getTargetConstant(InstOffset + 16 * i, DL, MVT::i32);
    Loads.push_back(getMemIntrinsicNode(AMDGPUISD::BUFFER_LOAD, DL, VTList, Ops,
                                        LoadVT, MMO, DAG));
  }

  if (NumElts == 8 || NumElts == 16)
    return DAG.getNode(ISD::CONCAT_VECTORS, DL, VT, Loads);

  return Loads[0];
}

SDValue SITargetLowering::lowerWaveID(SelectionDAG &DAG, SDValue Op) const {
  // With architected SGPRs, waveIDinGroup is in TTMP8[29:25].
  if (!Subtarget->hasArchitectedSGPRs())
    return {};
  SDLoc SL(Op);
  MVT VT = MVT::i32;
  SDValue TTMP8 = DAG.getCopyFromReg(DAG.getEntryNode(), SL, AMDGPU::TTMP8, VT);
  return DAG.getNode(AMDGPUISD::BFE_U32, SL, VT, TTMP8,
                     DAG.getConstant(25, SL, VT), DAG.getConstant(5, SL, VT));
}

SDValue SITargetLowering::lowerWorkitemID(SelectionDAG &DAG, SDValue Op,
                                          unsigned Dim,
                                          const ArgDescriptor &Arg) const {
  SDLoc SL(Op);
  MachineFunction &MF = DAG.getMachineFunction();
  unsigned MaxID = Subtarget->getMaxWorkitemID(MF.getFunction(), Dim);
  if (MaxID == 0)
    return DAG.getConstant(0, SL, MVT::i32);

  SDValue Val = loadInputValue(DAG, &AMDGPU::VGPR_32RegClass, MVT::i32,
                               SDLoc(DAG.getEntryNode()), Arg);

  // Don't bother inserting AssertZext for packed IDs since we're emitting the
  // masking operations anyway.
  //
  // TODO: We could assert the top bit is 0 for the source copy.
  if (Arg.isMasked())
    return Val;

  // Preserve the known bits after expansion to a copy.
  EVT SmallVT = EVT::getIntegerVT(*DAG.getContext(), llvm::bit_width(MaxID));
  return DAG.getNode(ISD::AssertZext, SL, MVT::i32, Val,
                     DAG.getValueType(SmallVT));
}

SDValue SITargetLowering::LowerINTRINSIC_WO_CHAIN(SDValue Op,
                                                  SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  auto MFI = MF.getInfo<SIMachineFunctionInfo>();

  EVT VT = Op.getValueType();
  SDLoc DL(Op);
  unsigned IntrinsicID = Op.getConstantOperandVal(0);

  // TODO: Should this propagate fast-math-flags?

  switch (IntrinsicID) {
  case Intrinsic::amdgcn_implicit_buffer_ptr: {
    if (getSubtarget()->isAmdHsaOrMesa(MF.getFunction()))
      return emitNonHSAIntrinsicError(DAG, DL, VT);
    return getPreloadedValue(DAG, *MFI, VT,
                             AMDGPUFunctionArgInfo::IMPLICIT_BUFFER_PTR);
  }
  case Intrinsic::amdgcn_dispatch_ptr:
  case Intrinsic::amdgcn_queue_ptr: {
    if (!Subtarget->isAmdHsaOrMesa(MF.getFunction())) {
      DiagnosticInfoUnsupported BadIntrin(
          MF.getFunction(), "unsupported hsa intrinsic without hsa target",
          DL.getDebugLoc());
      DAG.getContext()->diagnose(BadIntrin);
      return DAG.getUNDEF(VT);
    }

    auto RegID = IntrinsicID == Intrinsic::amdgcn_dispatch_ptr ?
      AMDGPUFunctionArgInfo::DISPATCH_PTR : AMDGPUFunctionArgInfo::QUEUE_PTR;
    return getPreloadedValue(DAG, *MFI, VT, RegID);
  }
  case Intrinsic::amdgcn_implicitarg_ptr: {
    if (MFI->isEntryFunction())
      return getImplicitArgPtr(DAG, DL);
    return getPreloadedValue(DAG, *MFI, VT,
                             AMDGPUFunctionArgInfo::IMPLICIT_ARG_PTR);
  }
  case Intrinsic::amdgcn_kernarg_segment_ptr: {
    if (!AMDGPU::isKernel(MF.getFunction().getCallingConv())) {
      // This only makes sense to call in a kernel, so just lower to null.
      return DAG.getConstant(0, DL, VT);
    }

    return getPreloadedValue(DAG, *MFI, VT,
                             AMDGPUFunctionArgInfo::KERNARG_SEGMENT_PTR);
  }
  case Intrinsic::amdgcn_dispatch_id: {
    return getPreloadedValue(DAG, *MFI, VT, AMDGPUFunctionArgInfo::DISPATCH_ID);
  }
  case Intrinsic::amdgcn_rcp:
    return DAG.getNode(AMDGPUISD::RCP, DL, VT, Op.getOperand(1));
  case Intrinsic::amdgcn_rsq:
    return DAG.getNode(AMDGPUISD::RSQ, DL, VT, Op.getOperand(1));
  case Intrinsic::amdgcn_rsq_legacy:
    if (Subtarget->getGeneration() >= AMDGPUSubtarget::VOLCANIC_ISLANDS)
      return emitRemovedIntrinsicError(DAG, DL, VT);
    return SDValue();
  case Intrinsic::amdgcn_rcp_legacy:
    if (Subtarget->getGeneration() >= AMDGPUSubtarget::VOLCANIC_ISLANDS)
      return emitRemovedIntrinsicError(DAG, DL, VT);
    return DAG.getNode(AMDGPUISD::RCP_LEGACY, DL, VT, Op.getOperand(1));
  case Intrinsic::amdgcn_rsq_clamp: {
    if (Subtarget->getGeneration() < AMDGPUSubtarget::VOLCANIC_ISLANDS)
      return DAG.getNode(AMDGPUISD::RSQ_CLAMP, DL, VT, Op.getOperand(1));

    Type *Type = VT.getTypeForEVT(*DAG.getContext());
    APFloat Max = APFloat::getLargest(Type->getFltSemantics());
    APFloat Min = APFloat::getLargest(Type->getFltSemantics(), true);

    SDValue Rsq = DAG.getNode(AMDGPUISD::RSQ, DL, VT, Op.getOperand(1));
    SDValue Tmp = DAG.getNode(ISD::FMINNUM, DL, VT, Rsq,
                              DAG.getConstantFP(Max, DL, VT));
    return DAG.getNode(ISD::FMAXNUM, DL, VT, Tmp,
                       DAG.getConstantFP(Min, DL, VT));
  }
  case Intrinsic::r600_read_ngroups_x:
    if (Subtarget->isAmdHsaOS())
      return emitNonHSAIntrinsicError(DAG, DL, VT);

    return lowerKernargMemParameter(DAG, VT, VT, DL, DAG.getEntryNode(),
                                    SI::KernelInputOffsets::NGROUPS_X, Align(4),
                                    false);
  case Intrinsic::r600_read_ngroups_y:
    if (Subtarget->isAmdHsaOS())
      return emitNonHSAIntrinsicError(DAG, DL, VT);

    return lowerKernargMemParameter(DAG, VT, VT, DL, DAG.getEntryNode(),
                                    SI::KernelInputOffsets::NGROUPS_Y, Align(4),
                                    false);
  case Intrinsic::r600_read_ngroups_z:
    if (Subtarget->isAmdHsaOS())
      return emitNonHSAIntrinsicError(DAG, DL, VT);

    return lowerKernargMemParameter(DAG, VT, VT, DL, DAG.getEntryNode(),
                                    SI::KernelInputOffsets::NGROUPS_Z, Align(4),
                                    false);
  case Intrinsic::r600_read_global_size_x:
    if (Subtarget->isAmdHsaOS())
      return emitNonHSAIntrinsicError(DAG, DL, VT);

    return lowerKernargMemParameter(DAG, VT, VT, DL, DAG.getEntryNode(),
                                    SI::KernelInputOffsets::GLOBAL_SIZE_X,
                                    Align(4), false);
  case Intrinsic::r600_read_global_size_y:
    if (Subtarget->isAmdHsaOS())
      return emitNonHSAIntrinsicError(DAG, DL, VT);

    return lowerKernargMemParameter(DAG, VT, VT, DL, DAG.getEntryNode(),
                                    SI::KernelInputOffsets::GLOBAL_SIZE_Y,
                                    Align(4), false);
  case Intrinsic::r600_read_global_size_z:
    if (Subtarget->isAmdHsaOS())
      return emitNonHSAIntrinsicError(DAG, DL, VT);

    return lowerKernargMemParameter(DAG, VT, VT, DL, DAG.getEntryNode(),
                                    SI::KernelInputOffsets::GLOBAL_SIZE_Z,
                                    Align(4), false);
  case Intrinsic::r600_read_local_size_x:
    if (Subtarget->isAmdHsaOS())
      return emitNonHSAIntrinsicError(DAG, DL, VT);

    return lowerImplicitZextParam(DAG, Op, MVT::i16,
                                  SI::KernelInputOffsets::LOCAL_SIZE_X);
  case Intrinsic::r600_read_local_size_y:
    if (Subtarget->isAmdHsaOS())
      return emitNonHSAIntrinsicError(DAG, DL, VT);

    return lowerImplicitZextParam(DAG, Op, MVT::i16,
                                  SI::KernelInputOffsets::LOCAL_SIZE_Y);
  case Intrinsic::r600_read_local_size_z:
    if (Subtarget->isAmdHsaOS())
      return emitNonHSAIntrinsicError(DAG, DL, VT);

    return lowerImplicitZextParam(DAG, Op, MVT::i16,
                                  SI::KernelInputOffsets::LOCAL_SIZE_Z);
  case Intrinsic::amdgcn_workgroup_id_x:
    return getPreloadedValue(DAG, *MFI, VT,
                             AMDGPUFunctionArgInfo::WORKGROUP_ID_X);
  case Intrinsic::amdgcn_workgroup_id_y:
    return getPreloadedValue(DAG, *MFI, VT,
                             AMDGPUFunctionArgInfo::WORKGROUP_ID_Y);
  case Intrinsic::amdgcn_workgroup_id_z:
    return getPreloadedValue(DAG, *MFI, VT,
                             AMDGPUFunctionArgInfo::WORKGROUP_ID_Z);
  case Intrinsic::amdgcn_wave_id:
    return lowerWaveID(DAG, Op);
  case Intrinsic::amdgcn_lds_kernel_id: {
    if (MFI->isEntryFunction())
      return getLDSKernelId(DAG, DL);
    return getPreloadedValue(DAG, *MFI, VT,
                             AMDGPUFunctionArgInfo::LDS_KERNEL_ID);
  }
  case Intrinsic::amdgcn_workitem_id_x:
    return lowerWorkitemID(DAG, Op, 0, MFI->getArgInfo().WorkItemIDX);
  case Intrinsic::amdgcn_workitem_id_y:
    return lowerWorkitemID(DAG, Op, 1, MFI->getArgInfo().WorkItemIDY);
  case Intrinsic::amdgcn_workitem_id_z:
    return lowerWorkitemID(DAG, Op, 2, MFI->getArgInfo().WorkItemIDZ);
  case Intrinsic::amdgcn_wavefrontsize:
    return DAG.getConstant(MF.getSubtarget<GCNSubtarget>().getWavefrontSize(),
                           SDLoc(Op), MVT::i32);
  case Intrinsic::amdgcn_s_buffer_load: {
    unsigned CPol = Op.getConstantOperandVal(3);
    // s_buffer_load, because of how it's optimized, can't be volatile
    // so reject ones with the volatile bit set.
    if (CPol & ~((Subtarget->getGeneration() >= AMDGPUSubtarget::GFX12)
                     ? AMDGPU::CPol::ALL
                     : AMDGPU::CPol::ALL_pregfx12))
      return Op;
    return lowerSBuffer(VT, DL, Op.getOperand(1), Op.getOperand(2), Op.getOperand(3),
                        DAG);
  }
  case Intrinsic::amdgcn_fdiv_fast:
    return lowerFDIV_FAST(Op, DAG);
  case Intrinsic::amdgcn_sin:
    return DAG.getNode(AMDGPUISD::SIN_HW, DL, VT, Op.getOperand(1));

  case Intrinsic::amdgcn_cos:
    return DAG.getNode(AMDGPUISD::COS_HW, DL, VT, Op.getOperand(1));

  case Intrinsic::amdgcn_mul_u24:
    return DAG.getNode(AMDGPUISD::MUL_U24, DL, VT, Op.getOperand(1), Op.getOperand(2));
  case Intrinsic::amdgcn_mul_i24:
    return DAG.getNode(AMDGPUISD::MUL_I24, DL, VT, Op.getOperand(1), Op.getOperand(2));

  case Intrinsic::amdgcn_log_clamp: {
    if (Subtarget->getGeneration() < AMDGPUSubtarget::VOLCANIC_ISLANDS)
      return SDValue();

    return emitRemovedIntrinsicError(DAG, DL, VT);
  }
  case Intrinsic::amdgcn_fract:
    return DAG.getNode(AMDGPUISD::FRACT, DL, VT, Op.getOperand(1));

  case Intrinsic::amdgcn_class:
    return DAG.getNode(AMDGPUISD::FP_CLASS, DL, VT,
                       Op.getOperand(1), Op.getOperand(2));
  case Intrinsic::amdgcn_div_fmas:
    return DAG.getNode(AMDGPUISD::DIV_FMAS, DL, VT,
                       Op.getOperand(1), Op.getOperand(2), Op.getOperand(3),
                       Op.getOperand(4));

  case Intrinsic::amdgcn_div_fixup:
    return DAG.getNode(AMDGPUISD::DIV_FIXUP, DL, VT,
                       Op.getOperand(1), Op.getOperand(2), Op.getOperand(3));

  case Intrinsic::amdgcn_div_scale: {
    const ConstantSDNode *Param = cast<ConstantSDNode>(Op.getOperand(3));

    // Translate to the operands expected by the machine instruction. The
    // first parameter must be the same as the first instruction.
    SDValue Numerator = Op.getOperand(1);
    SDValue Denominator = Op.getOperand(2);

    // Note this order is opposite of the machine instruction's operations,
    // which is s0.f = Quotient, s1.f = Denominator, s2.f = Numerator. The
    // intrinsic has the numerator as the first operand to match a normal
    // division operation.

    SDValue Src0 = Param->isAllOnes() ? Numerator : Denominator;

    return DAG.getNode(AMDGPUISD::DIV_SCALE, DL, Op->getVTList(), Src0,
                       Denominator, Numerator);
  }
  case Intrinsic::amdgcn_icmp: {
    // There is a Pat that handles this variant, so return it as-is.
    if (Op.getOperand(1).getValueType() == MVT::i1 &&
        Op.getConstantOperandVal(2) == 0 &&
        Op.getConstantOperandVal(3) == ICmpInst::Predicate::ICMP_NE)
      return Op;
    return lowerICMPIntrinsic(*this, Op.getNode(), DAG);
  }
  case Intrinsic::amdgcn_fcmp: {
    return lowerFCMPIntrinsic(*this, Op.getNode(), DAG);
  }
  case Intrinsic::amdgcn_ballot:
    return lowerBALLOTIntrinsic(*this, Op.getNode(), DAG);
  case Intrinsic::amdgcn_fmed3:
    return DAG.getNode(AMDGPUISD::FMED3, DL, VT,
                       Op.getOperand(1), Op.getOperand(2), Op.getOperand(3));
  case Intrinsic::amdgcn_fdot2:
    return DAG.getNode(AMDGPUISD::FDOT2, DL, VT,
                       Op.getOperand(1), Op.getOperand(2), Op.getOperand(3),
                       Op.getOperand(4));
  case Intrinsic::amdgcn_fmul_legacy:
    return DAG.getNode(AMDGPUISD::FMUL_LEGACY, DL, VT,
                       Op.getOperand(1), Op.getOperand(2));
  case Intrinsic::amdgcn_sffbh:
    return DAG.getNode(AMDGPUISD::FFBH_I32, DL, VT, Op.getOperand(1));
  case Intrinsic::amdgcn_sbfe:
    return DAG.getNode(AMDGPUISD::BFE_I32, DL, VT,
                       Op.getOperand(1), Op.getOperand(2), Op.getOperand(3));
  case Intrinsic::amdgcn_ubfe:
    return DAG.getNode(AMDGPUISD::BFE_U32, DL, VT,
                       Op.getOperand(1), Op.getOperand(2), Op.getOperand(3));
  case Intrinsic::amdgcn_cvt_pkrtz:
  case Intrinsic::amdgcn_cvt_pknorm_i16:
  case Intrinsic::amdgcn_cvt_pknorm_u16:
  case Intrinsic::amdgcn_cvt_pk_i16:
  case Intrinsic::amdgcn_cvt_pk_u16: {
    // FIXME: Stop adding cast if v2f16/v2i16 are legal.
    EVT VT = Op.getValueType();
    unsigned Opcode;

    if (IntrinsicID == Intrinsic::amdgcn_cvt_pkrtz)
      Opcode = AMDGPUISD::CVT_PKRTZ_F16_F32;
    else if (IntrinsicID == Intrinsic::amdgcn_cvt_pknorm_i16)
      Opcode = AMDGPUISD::CVT_PKNORM_I16_F32;
    else if (IntrinsicID == Intrinsic::amdgcn_cvt_pknorm_u16)
      Opcode = AMDGPUISD::CVT_PKNORM_U16_F32;
    else if (IntrinsicID == Intrinsic::amdgcn_cvt_pk_i16)
      Opcode = AMDGPUISD::CVT_PK_I16_I32;
    else
      Opcode = AMDGPUISD::CVT_PK_U16_U32;

    if (isTypeLegal(VT))
      return DAG.getNode(Opcode, DL, VT, Op.getOperand(1), Op.getOperand(2));

    SDValue Node = DAG.getNode(Opcode, DL, MVT::i32,
                               Op.getOperand(1), Op.getOperand(2));
    return DAG.getNode(ISD::BITCAST, DL, VT, Node);
  }
  case Intrinsic::amdgcn_fmad_ftz:
    return DAG.getNode(AMDGPUISD::FMAD_FTZ, DL, VT, Op.getOperand(1),
                       Op.getOperand(2), Op.getOperand(3));

  case Intrinsic::amdgcn_if_break:
    return SDValue(DAG.getMachineNode(AMDGPU::SI_IF_BREAK, DL, VT,
                                      Op->getOperand(1), Op->getOperand(2)), 0);

  case Intrinsic::amdgcn_groupstaticsize: {
    Triple::OSType OS = getTargetMachine().getTargetTriple().getOS();
    if (OS == Triple::AMDHSA || OS == Triple::AMDPAL)
      return Op;

    const Module *M = MF.getFunction().getParent();
    const GlobalValue *GV =
        M->getNamedValue(Intrinsic::getName(Intrinsic::amdgcn_groupstaticsize));
    SDValue GA = DAG.getTargetGlobalAddress(GV, DL, MVT::i32, 0,
                                            SIInstrInfo::MO_ABS32_LO);
    return {DAG.getMachineNode(AMDGPU::S_MOV_B32, DL, MVT::i32, GA), 0};
  }
  case Intrinsic::amdgcn_is_shared:
  case Intrinsic::amdgcn_is_private: {
    SDLoc SL(Op);
    unsigned AS = (IntrinsicID == Intrinsic::amdgcn_is_shared) ?
      AMDGPUAS::LOCAL_ADDRESS : AMDGPUAS::PRIVATE_ADDRESS;
    SDValue Aperture = getSegmentAperture(AS, SL, DAG);
    SDValue SrcVec = DAG.getNode(ISD::BITCAST, DL, MVT::v2i32,
                                 Op.getOperand(1));

    SDValue SrcHi = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, SrcVec,
                                DAG.getConstant(1, SL, MVT::i32));
    return DAG.getSetCC(SL, MVT::i1, SrcHi, Aperture, ISD::SETEQ);
  }
  case Intrinsic::amdgcn_perm:
    return DAG.getNode(AMDGPUISD::PERM, DL, MVT::i32, Op.getOperand(1),
                       Op.getOperand(2), Op.getOperand(3));
  case Intrinsic::amdgcn_reloc_constant: {
    Module *M = const_cast<Module *>(MF.getFunction().getParent());
    const MDNode *Metadata = cast<MDNodeSDNode>(Op.getOperand(1))->getMD();
    auto SymbolName = cast<MDString>(Metadata->getOperand(0))->getString();
    auto RelocSymbol = cast<GlobalVariable>(
        M->getOrInsertGlobal(SymbolName, Type::getInt32Ty(M->getContext())));
    SDValue GA = DAG.getTargetGlobalAddress(RelocSymbol, DL, MVT::i32, 0,
                                            SIInstrInfo::MO_ABS32_LO);
    return {DAG.getMachineNode(AMDGPU::S_MOV_B32, DL, MVT::i32, GA), 0};
  }
  case Intrinsic::amdgcn_swmmac_f16_16x16x32_f16:
  case Intrinsic::amdgcn_swmmac_bf16_16x16x32_bf16:
  case Intrinsic::amdgcn_swmmac_f32_16x16x32_bf16:
  case Intrinsic::amdgcn_swmmac_f32_16x16x32_f16:
  case Intrinsic::amdgcn_swmmac_f32_16x16x32_fp8_fp8:
  case Intrinsic::amdgcn_swmmac_f32_16x16x32_fp8_bf8:
  case Intrinsic::amdgcn_swmmac_f32_16x16x32_bf8_fp8:
  case Intrinsic::amdgcn_swmmac_f32_16x16x32_bf8_bf8: {
    if (Op.getOperand(4).getValueType() == MVT::i32)
      return SDValue();

    SDLoc SL(Op);
    auto IndexKeyi32 = DAG.getAnyExtOrTrunc(Op.getOperand(4), SL, MVT::i32);
    return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, SL, Op.getValueType(),
                       Op.getOperand(0), Op.getOperand(1), Op.getOperand(2),
                       Op.getOperand(3), IndexKeyi32);
  }
  case Intrinsic::amdgcn_swmmac_i32_16x16x32_iu4:
  case Intrinsic::amdgcn_swmmac_i32_16x16x32_iu8:
  case Intrinsic::amdgcn_swmmac_i32_16x16x64_iu4: {
    if (Op.getOperand(6).getValueType() == MVT::i32)
      return SDValue();

    SDLoc SL(Op);
    auto IndexKeyi32 = DAG.getAnyExtOrTrunc(Op.getOperand(6), SL, MVT::i32);
    return DAG.getNode(ISD::INTRINSIC_WO_CHAIN, SL, Op.getValueType(),
                       {Op.getOperand(0), Op.getOperand(1), Op.getOperand(2),
                        Op.getOperand(3), Op.getOperand(4), Op.getOperand(5),
                        IndexKeyi32, Op.getOperand(7)});
  }
  case Intrinsic::amdgcn_addrspacecast_nonnull:
    return lowerADDRSPACECAST(Op, DAG);
  case Intrinsic::amdgcn_readlane:
  case Intrinsic::amdgcn_readfirstlane:
  case Intrinsic::amdgcn_writelane:
  case Intrinsic::amdgcn_permlane16:
  case Intrinsic::amdgcn_permlanex16:
  case Intrinsic::amdgcn_permlane64:
    return lowerLaneOp(*this, Op.getNode(), DAG);
  default:
    if (const AMDGPU::ImageDimIntrinsicInfo *ImageDimIntr =
            AMDGPU::getImageDimIntrinsicInfo(IntrinsicID))
      return lowerImage(Op, ImageDimIntr, DAG, false);

    return Op;
  }
}

// On targets not supporting constant in soffset field, turn zero to
// SGPR_NULL to avoid generating an extra s_mov with zero.
static SDValue selectSOffset(SDValue SOffset, SelectionDAG &DAG,
                             const GCNSubtarget *Subtarget) {
  if (Subtarget->hasRestrictedSOffset() && isNullConstant(SOffset))
    return DAG.getRegister(AMDGPU::SGPR_NULL, MVT::i32);
  return SOffset;
}

SDValue SITargetLowering::lowerRawBufferAtomicIntrin(SDValue Op,
                                                     SelectionDAG &DAG,
                                                     unsigned NewOpcode) const {
  SDLoc DL(Op);

  SDValue VData = Op.getOperand(2);
  SDValue Rsrc = bufferRsrcPtrToVector(Op.getOperand(3), DAG);
  auto Offsets = splitBufferOffsets(Op.getOperand(4), DAG);
  auto SOffset = selectSOffset(Op.getOperand(5), DAG, Subtarget);
  SDValue Ops[] = {
      Op.getOperand(0),                      // Chain
      VData,                                 // vdata
      Rsrc,                                  // rsrc
      DAG.getConstant(0, DL, MVT::i32),      // vindex
      Offsets.first,                         // voffset
      SOffset,                               // soffset
      Offsets.second,                        // offset
      Op.getOperand(6),                      // cachepolicy
      DAG.getTargetConstant(0, DL, MVT::i1), // idxen
  };

  auto *M = cast<MemSDNode>(Op);

  EVT MemVT = VData.getValueType();
  return DAG.getMemIntrinsicNode(NewOpcode, DL, Op->getVTList(), Ops, MemVT,
                                 M->getMemOperand());
}

SDValue
SITargetLowering::lowerStructBufferAtomicIntrin(SDValue Op, SelectionDAG &DAG,
                                                unsigned NewOpcode) const {
  SDLoc DL(Op);

  SDValue VData = Op.getOperand(2);
  SDValue Rsrc = bufferRsrcPtrToVector(Op.getOperand(3), DAG);
  auto Offsets = splitBufferOffsets(Op.getOperand(5), DAG);
  auto SOffset = selectSOffset(Op.getOperand(6), DAG, Subtarget);
  SDValue Ops[] = {
      Op.getOperand(0),                      // Chain
      VData,                                 // vdata
      Rsrc,                                  // rsrc
      Op.getOperand(4),                      // vindex
      Offsets.first,                         // voffset
      SOffset,                               // soffset
      Offsets.second,                        // offset
      Op.getOperand(7),                      // cachepolicy
      DAG.getTargetConstant(1, DL, MVT::i1), // idxen
  };

  auto *M = cast<MemSDNode>(Op);

  EVT MemVT = VData.getValueType();
  return DAG.getMemIntrinsicNode(NewOpcode, DL, Op->getVTList(), Ops, MemVT,
                                 M->getMemOperand());
}

SDValue SITargetLowering::LowerINTRINSIC_W_CHAIN(SDValue Op,
                                                 SelectionDAG &DAG) const {
  unsigned IntrID = Op.getConstantOperandVal(1);
  SDLoc DL(Op);

  switch (IntrID) {
  case Intrinsic::amdgcn_ds_ordered_add:
  case Intrinsic::amdgcn_ds_ordered_swap: {
    MemSDNode *M = cast<MemSDNode>(Op);
    SDValue Chain = M->getOperand(0);
    SDValue M0 = M->getOperand(2);
    SDValue Value = M->getOperand(3);
    unsigned IndexOperand = M->getConstantOperandVal(7);
    unsigned WaveRelease = M->getConstantOperandVal(8);
    unsigned WaveDone = M->getConstantOperandVal(9);

    unsigned OrderedCountIndex = IndexOperand & 0x3f;
    IndexOperand &= ~0x3f;
    unsigned CountDw = 0;

    if (Subtarget->getGeneration() >= AMDGPUSubtarget::GFX10) {
      CountDw = (IndexOperand >> 24) & 0xf;
      IndexOperand &= ~(0xf << 24);

      if (CountDw < 1 || CountDw > 4) {
        report_fatal_error(
            "ds_ordered_count: dword count must be between 1 and 4");
      }
    }

    if (IndexOperand)
      report_fatal_error("ds_ordered_count: bad index operand");

    if (WaveDone && !WaveRelease)
      report_fatal_error("ds_ordered_count: wave_done requires wave_release");

    unsigned Instruction = IntrID == Intrinsic::amdgcn_ds_ordered_add ? 0 : 1;
    unsigned ShaderType =
        SIInstrInfo::getDSShaderTypeValue(DAG.getMachineFunction());
    unsigned Offset0 = OrderedCountIndex << 2;
    unsigned Offset1 = WaveRelease | (WaveDone << 1) | (Instruction << 4);

    if (Subtarget->getGeneration() >= AMDGPUSubtarget::GFX10)
      Offset1 |= (CountDw - 1) << 6;

    if (Subtarget->getGeneration() < AMDGPUSubtarget::GFX11)
      Offset1 |= ShaderType << 2;

    unsigned Offset = Offset0 | (Offset1 << 8);

    SDValue Ops[] = {
      Chain,
      Value,
      DAG.getTargetConstant(Offset, DL, MVT::i16),
      copyToM0(DAG, Chain, DL, M0).getValue(1), // Glue
    };
    return DAG.getMemIntrinsicNode(AMDGPUISD::DS_ORDERED_COUNT, DL,
                                   M->getVTList(), Ops, M->getMemoryVT(),
                                   M->getMemOperand());
  }
  case Intrinsic::amdgcn_raw_buffer_load:
  case Intrinsic::amdgcn_raw_ptr_buffer_load:
  case Intrinsic::amdgcn_raw_atomic_buffer_load:
  case Intrinsic::amdgcn_raw_ptr_atomic_buffer_load:
  case Intrinsic::amdgcn_raw_buffer_load_format:
  case Intrinsic::amdgcn_raw_ptr_buffer_load_format: {
    const bool IsFormat =
        IntrID == Intrinsic::amdgcn_raw_buffer_load_format ||
        IntrID == Intrinsic::amdgcn_raw_ptr_buffer_load_format;

    SDValue Rsrc = bufferRsrcPtrToVector(Op.getOperand(2), DAG);
    auto Offsets = splitBufferOffsets(Op.getOperand(3), DAG);
    auto SOffset = selectSOffset(Op.getOperand(4), DAG, Subtarget);
    SDValue Ops[] = {
        Op.getOperand(0),                      // Chain
        Rsrc,                                  // rsrc
        DAG.getConstant(0, DL, MVT::i32),      // vindex
        Offsets.first,                         // voffset
        SOffset,                               // soffset
        Offsets.second,                        // offset
        Op.getOperand(5),                      // cachepolicy, swizzled buffer
        DAG.getTargetConstant(0, DL, MVT::i1), // idxen
    };

    auto *M = cast<MemSDNode>(Op);
    return lowerIntrinsicLoad(M, IsFormat, DAG, Ops);
  }
  case Intrinsic::amdgcn_struct_buffer_load:
  case Intrinsic::amdgcn_struct_ptr_buffer_load:
  case Intrinsic::amdgcn_struct_buffer_load_format:
  case Intrinsic::amdgcn_struct_ptr_buffer_load_format: {
    const bool IsFormat =
        IntrID == Intrinsic::amdgcn_struct_buffer_load_format ||
        IntrID == Intrinsic::amdgcn_struct_ptr_buffer_load_format;

    SDValue Rsrc = bufferRsrcPtrToVector(Op.getOperand(2), DAG);
    auto Offsets = splitBufferOffsets(Op.getOperand(4), DAG);
    auto SOffset = selectSOffset(Op.getOperand(5), DAG, Subtarget);
    SDValue Ops[] = {
        Op.getOperand(0),                      // Chain
        Rsrc,                                  // rsrc
        Op.getOperand(3),                      // vindex
        Offsets.first,                         // voffset
        SOffset,                               // soffset
        Offsets.second,                        // offset
        Op.getOperand(6),                      // cachepolicy, swizzled buffer
        DAG.getTargetConstant(1, DL, MVT::i1), // idxen
    };

    return lowerIntrinsicLoad(cast<MemSDNode>(Op), IsFormat, DAG, Ops);
  }
  case Intrinsic::amdgcn_raw_tbuffer_load:
  case Intrinsic::amdgcn_raw_ptr_tbuffer_load: {
    MemSDNode *M = cast<MemSDNode>(Op);
    EVT LoadVT = Op.getValueType();
    SDValue Rsrc = bufferRsrcPtrToVector(Op.getOperand(2), DAG);
    auto Offsets = splitBufferOffsets(Op.getOperand(3), DAG);
    auto SOffset = selectSOffset(Op.getOperand(4), DAG, Subtarget);

    SDValue Ops[] = {
        Op.getOperand(0),                      // Chain
        Rsrc,                                  // rsrc
        DAG.getConstant(0, DL, MVT::i32),      // vindex
        Offsets.first,                         // voffset
        SOffset,                               // soffset
        Offsets.second,                        // offset
        Op.getOperand(5),                      // format
        Op.getOperand(6),                      // cachepolicy, swizzled buffer
        DAG.getTargetConstant(0, DL, MVT::i1), // idxen
    };

    if (LoadVT.getScalarType() == MVT::f16)
      return adjustLoadValueType(AMDGPUISD::TBUFFER_LOAD_FORMAT_D16,
                                 M, DAG, Ops);
    return getMemIntrinsicNode(AMDGPUISD::TBUFFER_LOAD_FORMAT, DL,
                               Op->getVTList(), Ops, LoadVT, M->getMemOperand(),
                               DAG);
  }
  case Intrinsic::amdgcn_struct_tbuffer_load:
  case Intrinsic::amdgcn_struct_ptr_tbuffer_load: {
    MemSDNode *M = cast<MemSDNode>(Op);
    EVT LoadVT = Op.getValueType();
    SDValue Rsrc = bufferRsrcPtrToVector(Op.getOperand(2), DAG);
    auto Offsets = splitBufferOffsets(Op.getOperand(4), DAG);
    auto SOffset = selectSOffset(Op.getOperand(5), DAG, Subtarget);

    SDValue Ops[] = {
        Op.getOperand(0),                      // Chain
        Rsrc,                                  // rsrc
        Op.getOperand(3),                      // vindex
        Offsets.first,                         // voffset
        SOffset,                               // soffset
        Offsets.second,                        // offset
        Op.getOperand(6),                      // format
        Op.getOperand(7),                      // cachepolicy, swizzled buffer
        DAG.getTargetConstant(1, DL, MVT::i1), // idxen
    };

    if (LoadVT.getScalarType() == MVT::f16)
      return adjustLoadValueType(AMDGPUISD::TBUFFER_LOAD_FORMAT_D16,
                                 M, DAG, Ops);
    return getMemIntrinsicNode(AMDGPUISD::TBUFFER_LOAD_FORMAT, DL,
                               Op->getVTList(), Ops, LoadVT, M->getMemOperand(),
                               DAG);
  }
  case Intrinsic::amdgcn_raw_buffer_atomic_fadd:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_fadd:
    return lowerRawBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_FADD);
  case Intrinsic::amdgcn_struct_buffer_atomic_fadd:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_fadd:
    return lowerStructBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_FADD);
  case Intrinsic::amdgcn_raw_buffer_atomic_fmin:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_fmin:
    return lowerRawBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_FMIN);
  case Intrinsic::amdgcn_struct_buffer_atomic_fmin:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_fmin:
    return lowerStructBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_FMIN);
  case Intrinsic::amdgcn_raw_buffer_atomic_fmax:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_fmax:
    return lowerRawBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_FMAX);
  case Intrinsic::amdgcn_struct_buffer_atomic_fmax:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_fmax:
    return lowerStructBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_FMAX);
  case Intrinsic::amdgcn_raw_buffer_atomic_swap:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_swap:
    return lowerRawBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_SWAP);
  case Intrinsic::amdgcn_raw_buffer_atomic_add:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_add:
    return lowerRawBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_ADD);
  case Intrinsic::amdgcn_raw_buffer_atomic_sub:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_sub:
    return lowerRawBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_SUB);
  case Intrinsic::amdgcn_raw_buffer_atomic_smin:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_smin:
    return lowerRawBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_SMIN);
  case Intrinsic::amdgcn_raw_buffer_atomic_umin:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_umin:
    return lowerRawBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_UMIN);
  case Intrinsic::amdgcn_raw_buffer_atomic_smax:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_smax:
    return lowerRawBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_SMAX);
  case Intrinsic::amdgcn_raw_buffer_atomic_umax:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_umax:
    return lowerRawBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_UMAX);
  case Intrinsic::amdgcn_raw_buffer_atomic_and:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_and:
    return lowerRawBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_AND);
  case Intrinsic::amdgcn_raw_buffer_atomic_or:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_or:
    return lowerRawBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_OR);
  case Intrinsic::amdgcn_raw_buffer_atomic_xor:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_xor:
    return lowerRawBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_XOR);
  case Intrinsic::amdgcn_raw_buffer_atomic_inc:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_inc:
    return lowerRawBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_INC);
  case Intrinsic::amdgcn_raw_buffer_atomic_dec:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_dec:
    return lowerRawBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_DEC);
  case Intrinsic::amdgcn_raw_buffer_atomic_cond_sub_u32:
    return lowerRawBufferAtomicIntrin(Op, DAG,
                                      AMDGPUISD::BUFFER_ATOMIC_COND_SUB_U32);
  case Intrinsic::amdgcn_struct_buffer_atomic_swap:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_swap:
    return lowerStructBufferAtomicIntrin(Op, DAG,
                                         AMDGPUISD::BUFFER_ATOMIC_SWAP);
  case Intrinsic::amdgcn_struct_buffer_atomic_add:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_add:
    return lowerStructBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_ADD);
  case Intrinsic::amdgcn_struct_buffer_atomic_sub:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_sub:
    return lowerStructBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_SUB);
  case Intrinsic::amdgcn_struct_buffer_atomic_smin:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_smin:
    return lowerStructBufferAtomicIntrin(Op, DAG,
                                         AMDGPUISD::BUFFER_ATOMIC_SMIN);
  case Intrinsic::amdgcn_struct_buffer_atomic_umin:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_umin:
    return lowerStructBufferAtomicIntrin(Op, DAG,
                                         AMDGPUISD::BUFFER_ATOMIC_UMIN);
  case Intrinsic::amdgcn_struct_buffer_atomic_smax:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_smax:
    return lowerStructBufferAtomicIntrin(Op, DAG,
                                         AMDGPUISD::BUFFER_ATOMIC_SMAX);
  case Intrinsic::amdgcn_struct_buffer_atomic_umax:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_umax:
    return lowerStructBufferAtomicIntrin(Op, DAG,
                                         AMDGPUISD::BUFFER_ATOMIC_UMAX);
  case Intrinsic::amdgcn_struct_buffer_atomic_and:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_and:
    return lowerStructBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_AND);
  case Intrinsic::amdgcn_struct_buffer_atomic_or:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_or:
    return lowerStructBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_OR);
  case Intrinsic::amdgcn_struct_buffer_atomic_xor:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_xor:
    return lowerStructBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_XOR);
  case Intrinsic::amdgcn_struct_buffer_atomic_inc:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_inc:
    return lowerStructBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_INC);
  case Intrinsic::amdgcn_struct_buffer_atomic_dec:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_dec:
    return lowerStructBufferAtomicIntrin(Op, DAG, AMDGPUISD::BUFFER_ATOMIC_DEC);
  case Intrinsic::amdgcn_struct_buffer_atomic_cond_sub_u32:
    return lowerStructBufferAtomicIntrin(Op, DAG,
                                         AMDGPUISD::BUFFER_ATOMIC_COND_SUB_U32);

  case Intrinsic::amdgcn_raw_buffer_atomic_cmpswap:
  case Intrinsic::amdgcn_raw_ptr_buffer_atomic_cmpswap: {
    SDValue Rsrc = bufferRsrcPtrToVector(Op.getOperand(4), DAG);
    auto Offsets = splitBufferOffsets(Op.getOperand(5), DAG);
    auto SOffset = selectSOffset(Op.getOperand(6), DAG, Subtarget);
    SDValue Ops[] = {
        Op.getOperand(0),                      // Chain
        Op.getOperand(2),                      // src
        Op.getOperand(3),                      // cmp
        Rsrc,                                  // rsrc
        DAG.getConstant(0, DL, MVT::i32),      // vindex
        Offsets.first,                         // voffset
        SOffset,                               // soffset
        Offsets.second,                        // offset
        Op.getOperand(7),                      // cachepolicy
        DAG.getTargetConstant(0, DL, MVT::i1), // idxen
    };
    EVT VT = Op.getValueType();
    auto *M = cast<MemSDNode>(Op);

    return DAG.getMemIntrinsicNode(AMDGPUISD::BUFFER_ATOMIC_CMPSWAP, DL,
                                   Op->getVTList(), Ops, VT, M->getMemOperand());
  }
  case Intrinsic::amdgcn_struct_buffer_atomic_cmpswap:
  case Intrinsic::amdgcn_struct_ptr_buffer_atomic_cmpswap: {
    SDValue Rsrc = bufferRsrcPtrToVector(Op->getOperand(4), DAG);
    auto Offsets = splitBufferOffsets(Op.getOperand(6), DAG);
    auto SOffset = selectSOffset(Op.getOperand(7), DAG, Subtarget);
    SDValue Ops[] = {
        Op.getOperand(0),                      // Chain
        Op.getOperand(2),                      // src
        Op.getOperand(3),                      // cmp
        Rsrc,                                  // rsrc
        Op.getOperand(5),                      // vindex
        Offsets.first,                         // voffset
        SOffset,                               // soffset
        Offsets.second,                        // offset
        Op.getOperand(8),                      // cachepolicy
        DAG.getTargetConstant(1, DL, MVT::i1), // idxen
    };
    EVT VT = Op.getValueType();
    auto *M = cast<MemSDNode>(Op);

    return DAG.getMemIntrinsicNode(AMDGPUISD::BUFFER_ATOMIC_CMPSWAP, DL,
                                   Op->getVTList(), Ops, VT, M->getMemOperand());
  }
  case Intrinsic::amdgcn_image_bvh_intersect_ray: {
    MemSDNode *M = cast<MemSDNode>(Op);
    SDValue NodePtr = M->getOperand(2);
    SDValue RayExtent = M->getOperand(3);
    SDValue RayOrigin = M->getOperand(4);
    SDValue RayDir = M->getOperand(5);
    SDValue RayInvDir = M->getOperand(6);
    SDValue TDescr = M->getOperand(7);

    assert(NodePtr.getValueType() == MVT::i32 ||
           NodePtr.getValueType() == MVT::i64);
    assert(RayDir.getValueType() == MVT::v3f16 ||
           RayDir.getValueType() == MVT::v3f32);

    if (!Subtarget->hasGFX10_AEncoding()) {
      emitRemovedIntrinsicError(DAG, DL, Op.getValueType());
      return SDValue();
    }

    const bool IsGFX11 = AMDGPU::isGFX11(*Subtarget);
    const bool IsGFX11Plus = AMDGPU::isGFX11Plus(*Subtarget);
    const bool IsGFX12Plus = AMDGPU::isGFX12Plus(*Subtarget);
    const bool IsA16 = RayDir.getValueType().getVectorElementType() == MVT::f16;
    const bool Is64 = NodePtr.getValueType() == MVT::i64;
    const unsigned NumVDataDwords = 4;
    const unsigned NumVAddrDwords = IsA16 ? (Is64 ? 9 : 8) : (Is64 ? 12 : 11);
    const unsigned NumVAddrs = IsGFX11Plus ? (IsA16 ? 4 : 5) : NumVAddrDwords;
    const bool UseNSA = (Subtarget->hasNSAEncoding() &&
                         NumVAddrs <= Subtarget->getNSAMaxSize()) ||
                        IsGFX12Plus;
    const unsigned BaseOpcodes[2][2] = {
        {AMDGPU::IMAGE_BVH_INTERSECT_RAY, AMDGPU::IMAGE_BVH_INTERSECT_RAY_a16},
        {AMDGPU::IMAGE_BVH64_INTERSECT_RAY,
         AMDGPU::IMAGE_BVH64_INTERSECT_RAY_a16}};
    int Opcode;
    if (UseNSA) {
      Opcode = AMDGPU::getMIMGOpcode(BaseOpcodes[Is64][IsA16],
                                     IsGFX12Plus ? AMDGPU::MIMGEncGfx12
                                     : IsGFX11   ? AMDGPU::MIMGEncGfx11NSA
                                                 : AMDGPU::MIMGEncGfx10NSA,
                                     NumVDataDwords, NumVAddrDwords);
    } else {
      assert(!IsGFX12Plus);
      Opcode = AMDGPU::getMIMGOpcode(BaseOpcodes[Is64][IsA16],
                                     IsGFX11 ? AMDGPU::MIMGEncGfx11Default
                                             : AMDGPU::MIMGEncGfx10Default,
                                     NumVDataDwords, NumVAddrDwords);
    }
    assert(Opcode != -1);

    SmallVector<SDValue, 16> Ops;

    auto packLanes = [&DAG, &Ops, &DL] (SDValue Op, bool IsAligned) {
      SmallVector<SDValue, 3> Lanes;
      DAG.ExtractVectorElements(Op, Lanes, 0, 3);
      if (Lanes[0].getValueSizeInBits() == 32) {
        for (unsigned I = 0; I < 3; ++I)
          Ops.push_back(DAG.getBitcast(MVT::i32, Lanes[I]));
      } else {
        if (IsAligned) {
          Ops.push_back(
            DAG.getBitcast(MVT::i32,
                           DAG.getBuildVector(MVT::v2f16, DL,
                                              { Lanes[0], Lanes[1] })));
          Ops.push_back(Lanes[2]);
        } else {
          SDValue Elt0 = Ops.pop_back_val();
          Ops.push_back(
            DAG.getBitcast(MVT::i32,
                           DAG.getBuildVector(MVT::v2f16, DL,
                                              { Elt0, Lanes[0] })));
          Ops.push_back(
            DAG.getBitcast(MVT::i32,
                           DAG.getBuildVector(MVT::v2f16, DL,
                                              { Lanes[1], Lanes[2] })));
        }
      }
    };

    if (UseNSA && IsGFX11Plus) {
      Ops.push_back(NodePtr);
      Ops.push_back(DAG.getBitcast(MVT::i32, RayExtent));
      Ops.push_back(RayOrigin);
      if (IsA16) {
        SmallVector<SDValue, 3> DirLanes, InvDirLanes, MergedLanes;
        DAG.ExtractVectorElements(RayDir, DirLanes, 0, 3);
        DAG.ExtractVectorElements(RayInvDir, InvDirLanes, 0, 3);
        for (unsigned I = 0; I < 3; ++I) {
          MergedLanes.push_back(DAG.getBitcast(
              MVT::i32, DAG.getBuildVector(MVT::v2f16, DL,
                                           {DirLanes[I], InvDirLanes[I]})));
        }
        Ops.push_back(DAG.getBuildVector(MVT::v3i32, DL, MergedLanes));
      } else {
        Ops.push_back(RayDir);
        Ops.push_back(RayInvDir);
      }
    } else {
      if (Is64)
        DAG.ExtractVectorElements(DAG.getBitcast(MVT::v2i32, NodePtr), Ops, 0,
                                  2);
      else
        Ops.push_back(NodePtr);

      Ops.push_back(DAG.getBitcast(MVT::i32, RayExtent));
      packLanes(RayOrigin, true);
      packLanes(RayDir, true);
      packLanes(RayInvDir, false);
    }

    if (!UseNSA) {
      // Build a single vector containing all the operands so far prepared.
      if (NumVAddrDwords > 12) {
        SDValue Undef = DAG.getUNDEF(MVT::i32);
        Ops.append(16 - Ops.size(), Undef);
      }
      assert(Ops.size() >= 8 && Ops.size() <= 12);
      SDValue MergedOps = DAG.getBuildVector(
          MVT::getVectorVT(MVT::i32, Ops.size()), DL, Ops);
      Ops.clear();
      Ops.push_back(MergedOps);
    }

    Ops.push_back(TDescr);
    Ops.push_back(DAG.getTargetConstant(IsA16, DL, MVT::i1));
    Ops.push_back(M->getChain());

    auto *NewNode = DAG.getMachineNode(Opcode, DL, M->getVTList(), Ops);
    MachineMemOperand *MemRef = M->getMemOperand();
    DAG.setNodeMemRefs(NewNode, {MemRef});
    return SDValue(NewNode, 0);
  }
  case Intrinsic::amdgcn_global_atomic_fmin:
  case Intrinsic::amdgcn_global_atomic_fmax:
  case Intrinsic::amdgcn_global_atomic_fmin_num:
  case Intrinsic::amdgcn_global_atomic_fmax_num:
  case Intrinsic::amdgcn_flat_atomic_fmin:
  case Intrinsic::amdgcn_flat_atomic_fmax:
  case Intrinsic::amdgcn_flat_atomic_fmin_num:
  case Intrinsic::amdgcn_flat_atomic_fmax_num: {
    MemSDNode *M = cast<MemSDNode>(Op);
    SDValue Ops[] = {
      M->getOperand(0), // Chain
      M->getOperand(2), // Ptr
      M->getOperand(3)  // Value
    };
    unsigned Opcode = 0;
    switch (IntrID) {
    case Intrinsic::amdgcn_global_atomic_fmin:
    case Intrinsic::amdgcn_global_atomic_fmin_num:
    case Intrinsic::amdgcn_flat_atomic_fmin:
    case Intrinsic::amdgcn_flat_atomic_fmin_num: {
      Opcode = ISD::ATOMIC_LOAD_FMIN;
      break;
    }
    case Intrinsic::amdgcn_global_atomic_fmax:
    case Intrinsic::amdgcn_global_atomic_fmax_num:
    case Intrinsic::amdgcn_flat_atomic_fmax:
    case Intrinsic::amdgcn_flat_atomic_fmax_num: {
      Opcode = ISD::ATOMIC_LOAD_FMAX;
      break;
    }
    default:
      llvm_unreachable("unhandled atomic opcode");
    }
    return DAG.getAtomic(Opcode, SDLoc(Op), M->getMemoryVT(), M->getVTList(),
                         Ops, M->getMemOperand());
  }
  case Intrinsic::amdgcn_s_get_barrier_state: {
    SDValue Chain = Op->getOperand(0);
    SmallVector<SDValue, 2> Ops;
    unsigned Opc;
    bool IsInlinableBarID = false;
    int64_t BarID;

    if (isa<ConstantSDNode>(Op->getOperand(2))) {
      BarID = cast<ConstantSDNode>(Op->getOperand(2))->getSExtValue();
      IsInlinableBarID = AMDGPU::isInlinableIntLiteral(BarID);
    }

    if (IsInlinableBarID) {
      Opc = AMDGPU::S_GET_BARRIER_STATE_IMM;
      SDValue K = DAG.getTargetConstant(BarID, DL, MVT::i32);
      Ops.push_back(K);
    } else {
      Opc = AMDGPU::S_GET_BARRIER_STATE_M0;
      SDValue M0Val = copyToM0(DAG, Chain, DL, Op.getOperand(2));
      Ops.push_back(M0Val.getValue(0));
    }

    auto NewMI = DAG.getMachineNode(Opc, DL, Op->getVTList(), Ops);
    return SDValue(NewMI, 0);
  }
  default:

    if (const AMDGPU::ImageDimIntrinsicInfo *ImageDimIntr =
            AMDGPU::getImageDimIntrinsicInfo(IntrID))
      return lowerImage(Op, ImageDimIntr, DAG, true);

    return SDValue();
  }
}

// Call DAG.getMemIntrinsicNode for a load, but first widen a dwordx3 type to
// dwordx4 if on SI and handle TFE loads.
SDValue SITargetLowering::getMemIntrinsicNode(unsigned Opcode, const SDLoc &DL,
                                              SDVTList VTList,
                                              ArrayRef<SDValue> Ops, EVT MemVT,
                                              MachineMemOperand *MMO,
                                              SelectionDAG &DAG) const {
  LLVMContext &C = *DAG.getContext();
  MachineFunction &MF = DAG.getMachineFunction();
  EVT VT = VTList.VTs[0];

  assert(VTList.NumVTs == 2 || VTList.NumVTs == 3);
  bool IsTFE = VTList.NumVTs == 3;
  if (IsTFE) {
    unsigned NumValueDWords = divideCeil(VT.getSizeInBits(), 32);
    unsigned NumOpDWords = NumValueDWords + 1;
    EVT OpDWordsVT = EVT::getVectorVT(C, MVT::i32, NumOpDWords);
    SDVTList OpDWordsVTList = DAG.getVTList(OpDWordsVT, VTList.VTs[2]);
    MachineMemOperand *OpDWordsMMO =
        MF.getMachineMemOperand(MMO, 0, NumOpDWords * 4);
    SDValue Op = getMemIntrinsicNode(Opcode, DL, OpDWordsVTList, Ops,
                                     OpDWordsVT, OpDWordsMMO, DAG);
    SDValue Status = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::i32, Op,
                                 DAG.getVectorIdxConstant(NumValueDWords, DL));
    SDValue ZeroIdx = DAG.getVectorIdxConstant(0, DL);
    SDValue ValueDWords =
        NumValueDWords == 1
            ? DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::i32, Op, ZeroIdx)
            : DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL,
                          EVT::getVectorVT(C, MVT::i32, NumValueDWords), Op,
                          ZeroIdx);
    SDValue Value = DAG.getNode(ISD::BITCAST, DL, VT, ValueDWords);
    return DAG.getMergeValues({Value, Status, SDValue(Op.getNode(), 1)}, DL);
  }

  if (!Subtarget->hasDwordx3LoadStores() &&
      (VT == MVT::v3i32 || VT == MVT::v3f32)) {
    EVT WidenedVT = EVT::getVectorVT(C, VT.getVectorElementType(), 4);
    EVT WidenedMemVT = EVT::getVectorVT(C, MemVT.getVectorElementType(), 4);
    MachineMemOperand *WidenedMMO = MF.getMachineMemOperand(MMO, 0, 16);
    SDVTList WidenedVTList = DAG.getVTList(WidenedVT, VTList.VTs[1]);
    SDValue Op = DAG.getMemIntrinsicNode(Opcode, DL, WidenedVTList, Ops,
                                         WidenedMemVT, WidenedMMO);
    SDValue Value = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, VT, Op,
                                DAG.getVectorIdxConstant(0, DL));
    return DAG.getMergeValues({Value, SDValue(Op.getNode(), 1)}, DL);
  }

  return DAG.getMemIntrinsicNode(Opcode, DL, VTList, Ops, MemVT, MMO);
}

SDValue SITargetLowering::handleD16VData(SDValue VData, SelectionDAG &DAG,
                                         bool ImageStore) const {
  EVT StoreVT = VData.getValueType();

  // No change for f16 and legal vector D16 types.
  if (!StoreVT.isVector())
    return VData;

  SDLoc DL(VData);
  unsigned NumElements = StoreVT.getVectorNumElements();

  if (Subtarget->hasUnpackedD16VMem()) {
    // We need to unpack the packed data to store.
    EVT IntStoreVT = StoreVT.changeTypeToInteger();
    SDValue IntVData = DAG.getNode(ISD::BITCAST, DL, IntStoreVT, VData);

    EVT EquivStoreVT =
        EVT::getVectorVT(*DAG.getContext(), MVT::i32, NumElements);
    SDValue ZExt = DAG.getNode(ISD::ZERO_EXTEND, DL, EquivStoreVT, IntVData);
    return DAG.UnrollVectorOp(ZExt.getNode());
  }

  // The sq block of gfx8.1 does not estimate register use correctly for d16
  // image store instructions. The data operand is computed as if it were not a
  // d16 image instruction.
  if (ImageStore && Subtarget->hasImageStoreD16Bug()) {
    // Bitcast to i16
    EVT IntStoreVT = StoreVT.changeTypeToInteger();
    SDValue IntVData = DAG.getNode(ISD::BITCAST, DL, IntStoreVT, VData);

    // Decompose into scalars
    SmallVector<SDValue, 4> Elts;
    DAG.ExtractVectorElements(IntVData, Elts);

    // Group pairs of i16 into v2i16 and bitcast to i32
    SmallVector<SDValue, 4> PackedElts;
    for (unsigned I = 0; I < Elts.size() / 2; I += 1) {
      SDValue Pair =
          DAG.getBuildVector(MVT::v2i16, DL, {Elts[I * 2], Elts[I * 2 + 1]});
      SDValue IntPair = DAG.getNode(ISD::BITCAST, DL, MVT::i32, Pair);
      PackedElts.push_back(IntPair);
    }
    if ((NumElements % 2) == 1) {
      // Handle v3i16
      unsigned I = Elts.size() / 2;
      SDValue Pair = DAG.getBuildVector(MVT::v2i16, DL,
                                        {Elts[I * 2], DAG.getUNDEF(MVT::i16)});
      SDValue IntPair = DAG.getNode(ISD::BITCAST, DL, MVT::i32, Pair);
      PackedElts.push_back(IntPair);
    }

    // Pad using UNDEF
    PackedElts.resize(Elts.size(), DAG.getUNDEF(MVT::i32));

    // Build final vector
    EVT VecVT =
        EVT::getVectorVT(*DAG.getContext(), MVT::i32, PackedElts.size());
    return DAG.getBuildVector(VecVT, DL, PackedElts);
  }

  if (NumElements == 3) {
    EVT IntStoreVT =
        EVT::getIntegerVT(*DAG.getContext(), StoreVT.getStoreSizeInBits());
    SDValue IntVData = DAG.getNode(ISD::BITCAST, DL, IntStoreVT, VData);

    EVT WidenedStoreVT = EVT::getVectorVT(
        *DAG.getContext(), StoreVT.getVectorElementType(), NumElements + 1);
    EVT WidenedIntVT = EVT::getIntegerVT(*DAG.getContext(),
                                         WidenedStoreVT.getStoreSizeInBits());
    SDValue ZExt = DAG.getNode(ISD::ZERO_EXTEND, DL, WidenedIntVT, IntVData);
    return DAG.getNode(ISD::BITCAST, DL, WidenedStoreVT, ZExt);
  }

  assert(isTypeLegal(StoreVT));
  return VData;
}

SDValue SITargetLowering::LowerINTRINSIC_VOID(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Chain = Op.getOperand(0);
  unsigned IntrinsicID = Op.getConstantOperandVal(1);
  MachineFunction &MF = DAG.getMachineFunction();

  switch (IntrinsicID) {
  case Intrinsic::amdgcn_exp_compr: {
    if (!Subtarget->hasCompressedExport()) {
      DiagnosticInfoUnsupported BadIntrin(
          DAG.getMachineFunction().getFunction(),
          "intrinsic not supported on subtarget", DL.getDebugLoc());
      DAG.getContext()->diagnose(BadIntrin);
    }
    SDValue Src0 = Op.getOperand(4);
    SDValue Src1 = Op.getOperand(5);
    // Hack around illegal type on SI by directly selecting it.
    if (isTypeLegal(Src0.getValueType()))
      return SDValue();

    const ConstantSDNode *Done = cast<ConstantSDNode>(Op.getOperand(6));
    SDValue Undef = DAG.getUNDEF(MVT::f32);
    const SDValue Ops[] = {
      Op.getOperand(2), // tgt
      DAG.getNode(ISD::BITCAST, DL, MVT::f32, Src0), // src0
      DAG.getNode(ISD::BITCAST, DL, MVT::f32, Src1), // src1
      Undef, // src2
      Undef, // src3
      Op.getOperand(7), // vm
      DAG.getTargetConstant(1, DL, MVT::i1), // compr
      Op.getOperand(3), // en
      Op.getOperand(0) // Chain
    };

    unsigned Opc = Done->isZero() ? AMDGPU::EXP : AMDGPU::EXP_DONE;
    return SDValue(DAG.getMachineNode(Opc, DL, Op->getVTList(), Ops), 0);
  }
  case Intrinsic::amdgcn_s_barrier: {
    const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
    if (getTargetMachine().getOptLevel() > CodeGenOptLevel::None) {
      unsigned WGSize = ST.getFlatWorkGroupSizes(MF.getFunction()).second;
      if (WGSize <= ST.getWavefrontSize())
        return SDValue(DAG.getMachineNode(AMDGPU::WAVE_BARRIER, DL, MVT::Other,
                                          Op.getOperand(0)), 0);
    }

    // On GFX12 lower s_barrier into s_barrier_signal_imm and s_barrier_wait
    if (ST.hasSplitBarriers()) {
      SDValue K =
          DAG.getTargetConstant(AMDGPU::Barrier::WORKGROUP, DL, MVT::i32);
      SDValue BarSignal =
          SDValue(DAG.getMachineNode(AMDGPU::S_BARRIER_SIGNAL_IMM, DL,
                                     MVT::Other, K, Op.getOperand(0)),
                  0);
      SDValue BarWait =
          SDValue(DAG.getMachineNode(AMDGPU::S_BARRIER_WAIT, DL, MVT::Other, K,
                                     BarSignal.getValue(0)),
                  0);
      return BarWait;
    }

    return SDValue();
  };

  case Intrinsic::amdgcn_struct_tbuffer_store:
  case Intrinsic::amdgcn_struct_ptr_tbuffer_store: {
    SDValue VData = Op.getOperand(2);
    bool IsD16 = (VData.getValueType().getScalarType() == MVT::f16);
    if (IsD16)
      VData = handleD16VData(VData, DAG);
    SDValue Rsrc = bufferRsrcPtrToVector(Op.getOperand(3), DAG);
    auto Offsets = splitBufferOffsets(Op.getOperand(5), DAG);
    auto SOffset = selectSOffset(Op.getOperand(6), DAG, Subtarget);
    SDValue Ops[] = {
        Chain,
        VData,                                 // vdata
        Rsrc,                                  // rsrc
        Op.getOperand(4),                      // vindex
        Offsets.first,                         // voffset
        SOffset,                               // soffset
        Offsets.second,                        // offset
        Op.getOperand(7),                      // format
        Op.getOperand(8),                      // cachepolicy, swizzled buffer
        DAG.getTargetConstant(1, DL, MVT::i1), // idxen
    };
    unsigned Opc = IsD16 ? AMDGPUISD::TBUFFER_STORE_FORMAT_D16 :
                           AMDGPUISD::TBUFFER_STORE_FORMAT;
    MemSDNode *M = cast<MemSDNode>(Op);
    return DAG.getMemIntrinsicNode(Opc, DL, Op->getVTList(), Ops,
                                   M->getMemoryVT(), M->getMemOperand());
  }

  case Intrinsic::amdgcn_raw_tbuffer_store:
  case Intrinsic::amdgcn_raw_ptr_tbuffer_store: {
    SDValue VData = Op.getOperand(2);
    bool IsD16 = (VData.getValueType().getScalarType() == MVT::f16);
    if (IsD16)
      VData = handleD16VData(VData, DAG);
    SDValue Rsrc = bufferRsrcPtrToVector(Op.getOperand(3), DAG);
    auto Offsets = splitBufferOffsets(Op.getOperand(4), DAG);
    auto SOffset = selectSOffset(Op.getOperand(5), DAG, Subtarget);
    SDValue Ops[] = {
        Chain,
        VData,                                 // vdata
        Rsrc,                                  // rsrc
        DAG.getConstant(0, DL, MVT::i32),      // vindex
        Offsets.first,                         // voffset
        SOffset,                               // soffset
        Offsets.second,                        // offset
        Op.getOperand(6),                      // format
        Op.getOperand(7),                      // cachepolicy, swizzled buffer
        DAG.getTargetConstant(0, DL, MVT::i1), // idxen
    };
    unsigned Opc = IsD16 ? AMDGPUISD::TBUFFER_STORE_FORMAT_D16 :
                           AMDGPUISD::TBUFFER_STORE_FORMAT;
    MemSDNode *M = cast<MemSDNode>(Op);
    return DAG.getMemIntrinsicNode(Opc, DL, Op->getVTList(), Ops,
                                   M->getMemoryVT(), M->getMemOperand());
  }

  case Intrinsic::amdgcn_raw_buffer_store:
  case Intrinsic::amdgcn_raw_ptr_buffer_store:
  case Intrinsic::amdgcn_raw_buffer_store_format:
  case Intrinsic::amdgcn_raw_ptr_buffer_store_format: {
    const bool IsFormat =
        IntrinsicID == Intrinsic::amdgcn_raw_buffer_store_format ||
        IntrinsicID == Intrinsic::amdgcn_raw_ptr_buffer_store_format;

    SDValue VData = Op.getOperand(2);
    EVT VDataVT = VData.getValueType();
    EVT EltType = VDataVT.getScalarType();
    bool IsD16 = IsFormat && (EltType.getSizeInBits() == 16);
    if (IsD16) {
      VData = handleD16VData(VData, DAG);
      VDataVT = VData.getValueType();
    }

    if (!isTypeLegal(VDataVT)) {
      VData =
          DAG.getNode(ISD::BITCAST, DL,
                      getEquivalentMemType(*DAG.getContext(), VDataVT), VData);
    }

    SDValue Rsrc = bufferRsrcPtrToVector(Op.getOperand(3), DAG);
    auto Offsets = splitBufferOffsets(Op.getOperand(4), DAG);
    auto SOffset = selectSOffset(Op.getOperand(5), DAG, Subtarget);
    SDValue Ops[] = {
        Chain,
        VData,
        Rsrc,
        DAG.getConstant(0, DL, MVT::i32),      // vindex
        Offsets.first,                         // voffset
        SOffset,                               // soffset
        Offsets.second,                        // offset
        Op.getOperand(6),                      // cachepolicy, swizzled buffer
        DAG.getTargetConstant(0, DL, MVT::i1), // idxen
    };
    unsigned Opc =
        IsFormat ? AMDGPUISD::BUFFER_STORE_FORMAT : AMDGPUISD::BUFFER_STORE;
    Opc = IsD16 ? AMDGPUISD::BUFFER_STORE_FORMAT_D16 : Opc;
    MemSDNode *M = cast<MemSDNode>(Op);

    // Handle BUFFER_STORE_BYTE/SHORT overloaded intrinsics
    if (!IsD16 && !VDataVT.isVector() && EltType.getSizeInBits() < 32)
      return handleByteShortBufferStores(DAG, VDataVT, DL, Ops, M);

    return DAG.getMemIntrinsicNode(Opc, DL, Op->getVTList(), Ops,
                                   M->getMemoryVT(), M->getMemOperand());
  }

  case Intrinsic::amdgcn_struct_buffer_store:
  case Intrinsic::amdgcn_struct_ptr_buffer_store:
  case Intrinsic::amdgcn_struct_buffer_store_format:
  case Intrinsic::amdgcn_struct_ptr_buffer_store_format: {
    const bool IsFormat =
        IntrinsicID == Intrinsic::amdgcn_struct_buffer_store_format ||
        IntrinsicID == Intrinsic::amdgcn_struct_ptr_buffer_store_format;

    SDValue VData = Op.getOperand(2);
    EVT VDataVT = VData.getValueType();
    EVT EltType = VDataVT.getScalarType();
    bool IsD16 = IsFormat && (EltType.getSizeInBits() == 16);

    if (IsD16) {
      VData = handleD16VData(VData, DAG);
      VDataVT = VData.getValueType();
    }

    if (!isTypeLegal(VDataVT)) {
      VData =
          DAG.getNode(ISD::BITCAST, DL,
                      getEquivalentMemType(*DAG.getContext(), VDataVT), VData);
    }

    auto Rsrc = bufferRsrcPtrToVector(Op.getOperand(3), DAG);
    auto Offsets = splitBufferOffsets(Op.getOperand(5), DAG);
    auto SOffset = selectSOffset(Op.getOperand(6), DAG, Subtarget);
    SDValue Ops[] = {
        Chain,
        VData,
        Rsrc,
        Op.getOperand(4),                      // vindex
        Offsets.first,                         // voffset
        SOffset,                               // soffset
        Offsets.second,                        // offset
        Op.getOperand(7),                      // cachepolicy, swizzled buffer
        DAG.getTargetConstant(1, DL, MVT::i1), // idxen
    };
    unsigned Opc =
        !IsFormat ? AMDGPUISD::BUFFER_STORE : AMDGPUISD::BUFFER_STORE_FORMAT;
    Opc = IsD16 ? AMDGPUISD::BUFFER_STORE_FORMAT_D16 : Opc;
    MemSDNode *M = cast<MemSDNode>(Op);

    // Handle BUFFER_STORE_BYTE/SHORT overloaded intrinsics
    EVT VDataType = VData.getValueType().getScalarType();
    if (!IsD16 && !VDataVT.isVector() && EltType.getSizeInBits() < 32)
      return handleByteShortBufferStores(DAG, VDataType, DL, Ops, M);

    return DAG.getMemIntrinsicNode(Opc, DL, Op->getVTList(), Ops,
                                   M->getMemoryVT(), M->getMemOperand());
  }
  case Intrinsic::amdgcn_raw_buffer_load_lds:
  case Intrinsic::amdgcn_raw_ptr_buffer_load_lds:
  case Intrinsic::amdgcn_struct_buffer_load_lds:
  case Intrinsic::amdgcn_struct_ptr_buffer_load_lds: {
    assert(!AMDGPU::isGFX12Plus(*Subtarget));
    unsigned Opc;
    bool HasVIndex =
        IntrinsicID == Intrinsic::amdgcn_struct_buffer_load_lds ||
        IntrinsicID == Intrinsic::amdgcn_struct_ptr_buffer_load_lds;
    unsigned OpOffset = HasVIndex ? 1 : 0;
    SDValue VOffset = Op.getOperand(5 + OpOffset);
    bool HasVOffset = !isNullConstant(VOffset);
    unsigned Size = Op->getConstantOperandVal(4);

    switch (Size) {
    default:
      return SDValue();
    case 1:
      Opc = HasVIndex ? HasVOffset ? AMDGPU::BUFFER_LOAD_UBYTE_LDS_BOTHEN
                                   : AMDGPU::BUFFER_LOAD_UBYTE_LDS_IDXEN
                      : HasVOffset ? AMDGPU::BUFFER_LOAD_UBYTE_LDS_OFFEN
                                   : AMDGPU::BUFFER_LOAD_UBYTE_LDS_OFFSET;
      break;
    case 2:
      Opc = HasVIndex ? HasVOffset ? AMDGPU::BUFFER_LOAD_USHORT_LDS_BOTHEN
                                   : AMDGPU::BUFFER_LOAD_USHORT_LDS_IDXEN
                      : HasVOffset ? AMDGPU::BUFFER_LOAD_USHORT_LDS_OFFEN
                                   : AMDGPU::BUFFER_LOAD_USHORT_LDS_OFFSET;
      break;
    case 4:
      Opc = HasVIndex ? HasVOffset ? AMDGPU::BUFFER_LOAD_DWORD_LDS_BOTHEN
                                   : AMDGPU::BUFFER_LOAD_DWORD_LDS_IDXEN
                      : HasVOffset ? AMDGPU::BUFFER_LOAD_DWORD_LDS_OFFEN
                                   : AMDGPU::BUFFER_LOAD_DWORD_LDS_OFFSET;
      break;
    }

    SDValue M0Val = copyToM0(DAG, Chain, DL, Op.getOperand(3));

    SmallVector<SDValue, 8> Ops;

    if (HasVIndex && HasVOffset)
      Ops.push_back(DAG.getBuildVector(MVT::v2i32, DL,
                                       { Op.getOperand(5), // VIndex
                                         VOffset }));
    else if (HasVIndex)
      Ops.push_back(Op.getOperand(5));
    else if (HasVOffset)
      Ops.push_back(VOffset);

    SDValue Rsrc = bufferRsrcPtrToVector(Op.getOperand(2), DAG);
    Ops.push_back(Rsrc);
    Ops.push_back(Op.getOperand(6 + OpOffset)); // soffset
    Ops.push_back(Op.getOperand(7 + OpOffset)); // imm offset
    unsigned Aux = Op.getConstantOperandVal(8 + OpOffset);
    Ops.push_back(
      DAG.getTargetConstant(Aux & AMDGPU::CPol::ALL, DL, MVT::i8)); // cpol
    Ops.push_back(DAG.getTargetConstant(
        Aux & AMDGPU::CPol::SWZ_pregfx12 ? 1 : 0, DL, MVT::i8)); // swz
    Ops.push_back(M0Val.getValue(0)); // Chain
    Ops.push_back(M0Val.getValue(1)); // Glue

    auto *M = cast<MemSDNode>(Op);
    MachineMemOperand *LoadMMO = M->getMemOperand();
    // Don't set the offset value here because the pointer points to the base of
    // the buffer.
    MachinePointerInfo LoadPtrI = LoadMMO->getPointerInfo();

    MachinePointerInfo StorePtrI = LoadPtrI;
    LoadPtrI.V = PoisonValue::get(
        PointerType::get(*DAG.getContext(), AMDGPUAS::GLOBAL_ADDRESS));
    LoadPtrI.AddrSpace = AMDGPUAS::GLOBAL_ADDRESS;
    StorePtrI.AddrSpace = AMDGPUAS::LOCAL_ADDRESS;

    auto F = LoadMMO->getFlags() &
             ~(MachineMemOperand::MOStore | MachineMemOperand::MOLoad);
    LoadMMO =
        MF.getMachineMemOperand(LoadPtrI, F | MachineMemOperand::MOLoad, Size,
                                LoadMMO->getBaseAlign(), LoadMMO->getAAInfo());

    MachineMemOperand *StoreMMO = MF.getMachineMemOperand(
        StorePtrI, F | MachineMemOperand::MOStore, sizeof(int32_t),
        LoadMMO->getBaseAlign(), LoadMMO->getAAInfo());

    auto Load = DAG.getMachineNode(Opc, DL, M->getVTList(), Ops);
    DAG.setNodeMemRefs(Load, {LoadMMO, StoreMMO});

    return SDValue(Load, 0);
  }
  case Intrinsic::amdgcn_global_load_lds: {
    unsigned Opc;
    unsigned Size = Op->getConstantOperandVal(4);
    switch (Size) {
    default:
      return SDValue();
    case 1:
      Opc = AMDGPU::GLOBAL_LOAD_LDS_UBYTE;
      break;
    case 2:
      Opc = AMDGPU::GLOBAL_LOAD_LDS_USHORT;
      break;
    case 4:
      Opc = AMDGPU::GLOBAL_LOAD_LDS_DWORD;
      break;
    }

    auto *M = cast<MemSDNode>(Op);
    SDValue M0Val = copyToM0(DAG, Chain, DL, Op.getOperand(3));

    SmallVector<SDValue, 6> Ops;

    SDValue Addr = Op.getOperand(2); // Global ptr
    SDValue VOffset;
    // Try to split SAddr and VOffset. Global and LDS pointers share the same
    // immediate offset, so we cannot use a regular SelectGlobalSAddr().
    if (Addr->isDivergent() && Addr.getOpcode() == ISD::ADD) {
      SDValue LHS = Addr.getOperand(0);
      SDValue RHS = Addr.getOperand(1);

      if (LHS->isDivergent())
        std::swap(LHS, RHS);

      if (!LHS->isDivergent() && RHS.getOpcode() == ISD::ZERO_EXTEND &&
          RHS.getOperand(0).getValueType() == MVT::i32) {
        // add (i64 sgpr), (zero_extend (i32 vgpr))
        Addr = LHS;
        VOffset = RHS.getOperand(0);
      }
    }

    Ops.push_back(Addr);
    if (!Addr->isDivergent()) {
      Opc = AMDGPU::getGlobalSaddrOp(Opc);
      if (!VOffset)
        VOffset = SDValue(
            DAG.getMachineNode(AMDGPU::V_MOV_B32_e32, DL, MVT::i32,
                               DAG.getTargetConstant(0, DL, MVT::i32)), 0);
      Ops.push_back(VOffset);
    }

    Ops.push_back(Op.getOperand(5));  // Offset
    Ops.push_back(Op.getOperand(6));  // CPol
    Ops.push_back(M0Val.getValue(0)); // Chain
    Ops.push_back(M0Val.getValue(1)); // Glue

    MachineMemOperand *LoadMMO = M->getMemOperand();
    MachinePointerInfo LoadPtrI = LoadMMO->getPointerInfo();
    LoadPtrI.Offset = Op->getConstantOperandVal(5);
    MachinePointerInfo StorePtrI = LoadPtrI;
    LoadPtrI.V = PoisonValue::get(
        PointerType::get(*DAG.getContext(), AMDGPUAS::GLOBAL_ADDRESS));
    LoadPtrI.AddrSpace = AMDGPUAS::GLOBAL_ADDRESS;
    StorePtrI.AddrSpace = AMDGPUAS::LOCAL_ADDRESS;
    auto F = LoadMMO->getFlags() &
             ~(MachineMemOperand::MOStore | MachineMemOperand::MOLoad);
    LoadMMO =
        MF.getMachineMemOperand(LoadPtrI, F | MachineMemOperand::MOLoad, Size,
                                LoadMMO->getBaseAlign(), LoadMMO->getAAInfo());
    MachineMemOperand *StoreMMO = MF.getMachineMemOperand(
        StorePtrI, F | MachineMemOperand::MOStore, sizeof(int32_t), Align(4),
        LoadMMO->getAAInfo());

    auto Load = DAG.getMachineNode(Opc, DL, Op->getVTList(), Ops);
    DAG.setNodeMemRefs(Load, {LoadMMO, StoreMMO});

    return SDValue(Load, 0);
  }
  case Intrinsic::amdgcn_end_cf:
    return SDValue(DAG.getMachineNode(AMDGPU::SI_END_CF, DL, MVT::Other,
                                      Op->getOperand(2), Chain), 0);
  case Intrinsic::amdgcn_s_barrier_init:
  case Intrinsic::amdgcn_s_barrier_join:
  case Intrinsic::amdgcn_s_wakeup_barrier: {
    SDValue Chain = Op->getOperand(0);
    SmallVector<SDValue, 2> Ops;
    SDValue BarOp = Op->getOperand(2);
    unsigned Opc;
    bool IsInlinableBarID = false;
    int64_t BarVal;

    if (isa<ConstantSDNode>(BarOp)) {
      BarVal = cast<ConstantSDNode>(BarOp)->getSExtValue();
      IsInlinableBarID = AMDGPU::isInlinableIntLiteral(BarVal);
    }

    if (IsInlinableBarID) {
      switch (IntrinsicID) {
      default:
        return SDValue();
      case Intrinsic::amdgcn_s_barrier_init:
        Opc = AMDGPU::S_BARRIER_INIT_IMM;
        break;
      case Intrinsic::amdgcn_s_barrier_join:
        Opc = AMDGPU::S_BARRIER_JOIN_IMM;
        break;
      case Intrinsic::amdgcn_s_wakeup_barrier:
        Opc = AMDGPU::S_WAKEUP_BARRIER_IMM;
        break;
      }

      SDValue K = DAG.getTargetConstant(BarVal, DL, MVT::i32);
      Ops.push_back(K);
    } else {
      switch (IntrinsicID) {
      default:
        return SDValue();
      case Intrinsic::amdgcn_s_barrier_init:
        Opc = AMDGPU::S_BARRIER_INIT_M0;
        break;
      case Intrinsic::amdgcn_s_barrier_join:
        Opc = AMDGPU::S_BARRIER_JOIN_M0;
        break;
      case Intrinsic::amdgcn_s_wakeup_barrier:
        Opc = AMDGPU::S_WAKEUP_BARRIER_M0;
        break;
      }
    }

    if (IntrinsicID == Intrinsic::amdgcn_s_barrier_init) {
      SDValue M0Val;
      // Member count will be read from M0[16:22]
      M0Val = DAG.getNode(ISD::SHL, DL, MVT::i32, Op.getOperand(3),
                          DAG.getShiftAmountConstant(16, MVT::i32, DL));

      if (!IsInlinableBarID) {
        // If reference to barrier id is not an inline constant then it must be
        // referenced with M0[4:0]. Perform an OR with the member count to
        // include it in M0.
        M0Val = SDValue(DAG.getMachineNode(AMDGPU::S_OR_B32, DL, MVT::i32,
                                           Op.getOperand(2), M0Val),
                        0);
      }
      Ops.push_back(copyToM0(DAG, Chain, DL, M0Val).getValue(0));
    } else if (!IsInlinableBarID) {
      Ops.push_back(copyToM0(DAG, Chain, DL, BarOp).getValue(0));
    }

    auto NewMI = DAG.getMachineNode(Opc, DL, Op->getVTList(), Ops);
    return SDValue(NewMI, 0);
  }
  default: {
    if (const AMDGPU::ImageDimIntrinsicInfo *ImageDimIntr =
            AMDGPU::getImageDimIntrinsicInfo(IntrinsicID))
      return lowerImage(Op, ImageDimIntr, DAG, true);

    return Op;
  }
  }
}

// The raw.(t)buffer and struct.(t)buffer intrinsics have two offset args:
// offset (the offset that is included in bounds checking and swizzling, to be
// split between the instruction's voffset and immoffset fields) and soffset
// (the offset that is excluded from bounds checking and swizzling, to go in
// the instruction's soffset field).  This function takes the first kind of
// offset and figures out how to split it between voffset and immoffset.
std::pair<SDValue, SDValue> SITargetLowering::splitBufferOffsets(
    SDValue Offset, SelectionDAG &DAG) const {
  SDLoc DL(Offset);
  const unsigned MaxImm = SIInstrInfo::getMaxMUBUFImmOffset(*Subtarget);
  SDValue N0 = Offset;
  ConstantSDNode *C1 = nullptr;

  if ((C1 = dyn_cast<ConstantSDNode>(N0)))
    N0 = SDValue();
  else if (DAG.isBaseWithConstantOffset(N0)) {
    C1 = cast<ConstantSDNode>(N0.getOperand(1));
    N0 = N0.getOperand(0);
  }

  if (C1) {
    unsigned ImmOffset = C1->getZExtValue();
    // If the immediate value is too big for the immoffset field, put only bits
    // that would normally fit in the immoffset field. The remaining value that
    // is copied/added for the voffset field is a large power of 2, and it
    // stands more chance of being CSEd with the copy/add for another similar
    // load/store.
    // However, do not do that rounding down if that is a negative
    // number, as it appears to be illegal to have a negative offset in the
    // vgpr, even if adding the immediate offset makes it positive.
    unsigned Overflow = ImmOffset & ~MaxImm;
    ImmOffset -= Overflow;
    if ((int32_t)Overflow < 0) {
      Overflow += ImmOffset;
      ImmOffset = 0;
    }
    C1 = cast<ConstantSDNode>(DAG.getTargetConstant(ImmOffset, DL, MVT::i32));
    if (Overflow) {
      auto OverflowVal = DAG.getConstant(Overflow, DL, MVT::i32);
      if (!N0)
        N0 = OverflowVal;
      else {
        SDValue Ops[] = { N0, OverflowVal };
        N0 = DAG.getNode(ISD::ADD, DL, MVT::i32, Ops);
      }
    }
  }
  if (!N0)
    N0 = DAG.getConstant(0, DL, MVT::i32);
  if (!C1)
    C1 = cast<ConstantSDNode>(DAG.getTargetConstant(0, DL, MVT::i32));
  return {N0, SDValue(C1, 0)};
}

// Analyze a combined offset from an amdgcn_s_buffer_load intrinsic and store
// the three offsets (voffset, soffset and instoffset) into the SDValue[3] array
// pointed to by Offsets.
void SITargetLowering::setBufferOffsets(SDValue CombinedOffset,
                                        SelectionDAG &DAG, SDValue *Offsets,
                                        Align Alignment) const {
  const SIInstrInfo *TII = getSubtarget()->getInstrInfo();
  SDLoc DL(CombinedOffset);
  if (auto *C = dyn_cast<ConstantSDNode>(CombinedOffset)) {
    uint32_t Imm = C->getZExtValue();
    uint32_t SOffset, ImmOffset;
    if (TII->splitMUBUFOffset(Imm, SOffset, ImmOffset, Alignment)) {
      Offsets[0] = DAG.getConstant(0, DL, MVT::i32);
      Offsets[1] = DAG.getConstant(SOffset, DL, MVT::i32);
      Offsets[2] = DAG.getTargetConstant(ImmOffset, DL, MVT::i32);
      return;
    }
  }
  if (DAG.isBaseWithConstantOffset(CombinedOffset)) {
    SDValue N0 = CombinedOffset.getOperand(0);
    SDValue N1 = CombinedOffset.getOperand(1);
    uint32_t SOffset, ImmOffset;
    int Offset = cast<ConstantSDNode>(N1)->getSExtValue();
    if (Offset >= 0 &&
        TII->splitMUBUFOffset(Offset, SOffset, ImmOffset, Alignment)) {
      Offsets[0] = N0;
      Offsets[1] = DAG.getConstant(SOffset, DL, MVT::i32);
      Offsets[2] = DAG.getTargetConstant(ImmOffset, DL, MVT::i32);
      return;
    }
  }

  SDValue SOffsetZero = Subtarget->hasRestrictedSOffset()
                            ? DAG.getRegister(AMDGPU::SGPR_NULL, MVT::i32)
                            : DAG.getConstant(0, DL, MVT::i32);

  Offsets[0] = CombinedOffset;
  Offsets[1] = SOffsetZero;
  Offsets[2] = DAG.getTargetConstant(0, DL, MVT::i32);
}

SDValue SITargetLowering::bufferRsrcPtrToVector(SDValue MaybePointer,
                                                SelectionDAG &DAG) const {
  if (!MaybePointer.getValueType().isScalarInteger())
    return MaybePointer;

  SDLoc DL(MaybePointer);

  SDValue Rsrc = DAG.getBitcast(MVT::v4i32, MaybePointer);
  return Rsrc;
}

// Wrap a global or flat pointer into a buffer intrinsic using the flags
// specified in the intrinsic.
SDValue SITargetLowering::lowerPointerAsRsrcIntrin(SDNode *Op,
                                                   SelectionDAG &DAG) const {
  SDLoc Loc(Op);

  SDValue Pointer = Op->getOperand(1);
  SDValue Stride = Op->getOperand(2);
  SDValue NumRecords = Op->getOperand(3);
  SDValue Flags = Op->getOperand(4);

  auto [LowHalf, HighHalf] = DAG.SplitScalar(Pointer, Loc, MVT::i32, MVT::i32);
  SDValue Mask = DAG.getConstant(0x0000ffff, Loc, MVT::i32);
  SDValue Masked = DAG.getNode(ISD::AND, Loc, MVT::i32, HighHalf, Mask);
  std::optional<uint32_t> ConstStride = std::nullopt;
  if (auto *ConstNode = dyn_cast<ConstantSDNode>(Stride))
    ConstStride = ConstNode->getZExtValue();

  SDValue NewHighHalf = Masked;
  if (!ConstStride || *ConstStride != 0) {
    SDValue ShiftedStride;
    if (ConstStride) {
      ShiftedStride = DAG.getConstant(*ConstStride << 16, Loc, MVT::i32);
    } else {
      SDValue ExtStride = DAG.getAnyExtOrTrunc(Stride, Loc, MVT::i32);
      ShiftedStride =
          DAG.getNode(ISD::SHL, Loc, MVT::i32, ExtStride,
                      DAG.getShiftAmountConstant(16, MVT::i32, Loc));
    }
    NewHighHalf = DAG.getNode(ISD::OR, Loc, MVT::i32, Masked, ShiftedStride);
  }

  SDValue Rsrc = DAG.getNode(ISD::BUILD_VECTOR, Loc, MVT::v4i32, LowHalf,
                             NewHighHalf, NumRecords, Flags);
  SDValue RsrcPtr = DAG.getNode(ISD::BITCAST, Loc, MVT::i128, Rsrc);
  return RsrcPtr;
}

// Handle 8 bit and 16 bit buffer loads
SDValue SITargetLowering::handleByteShortBufferLoads(SelectionDAG &DAG,
                                                     EVT LoadVT, SDLoc DL,
                                                     ArrayRef<SDValue> Ops,
                                                     MachineMemOperand *MMO,
                                                     bool IsTFE) const {
  EVT IntVT = LoadVT.changeTypeToInteger();

  if (IsTFE) {
    unsigned Opc = (LoadVT.getScalarType() == MVT::i8)
                       ? AMDGPUISD::BUFFER_LOAD_UBYTE_TFE
                       : AMDGPUISD::BUFFER_LOAD_USHORT_TFE;
    MachineFunction &MF = DAG.getMachineFunction();
    MachineMemOperand *OpMMO = MF.getMachineMemOperand(MMO, 0, 8);
    SDVTList VTs = DAG.getVTList(MVT::v2i32, MVT::Other);
    SDValue Op = getMemIntrinsicNode(Opc, DL, VTs, Ops, MVT::v2i32, OpMMO, DAG);
    SDValue Status = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::i32, Op,
                                 DAG.getConstant(1, DL, MVT::i32));
    SDValue Data = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::i32, Op,
                               DAG.getConstant(0, DL, MVT::i32));
    SDValue Trunc = DAG.getNode(ISD::TRUNCATE, DL, IntVT, Data);
    SDValue Value = DAG.getNode(ISD::BITCAST, DL, LoadVT, Trunc);
    return DAG.getMergeValues({Value, Status, SDValue(Op.getNode(), 1)}, DL);
  }

  unsigned Opc = (LoadVT.getScalarType() == MVT::i8) ?
         AMDGPUISD::BUFFER_LOAD_UBYTE : AMDGPUISD::BUFFER_LOAD_USHORT;

  SDVTList ResList = DAG.getVTList(MVT::i32, MVT::Other);
  SDValue BufferLoad =
      DAG.getMemIntrinsicNode(Opc, DL, ResList, Ops, IntVT, MMO);
  SDValue LoadVal = DAG.getNode(ISD::TRUNCATE, DL, IntVT, BufferLoad);
  LoadVal = DAG.getNode(ISD::BITCAST, DL, LoadVT, LoadVal);

  return DAG.getMergeValues({LoadVal, BufferLoad.getValue(1)}, DL);
}

// Handle 8 bit and 16 bit buffer stores
SDValue SITargetLowering::handleByteShortBufferStores(SelectionDAG &DAG,
                                                      EVT VDataType, SDLoc DL,
                                                      SDValue Ops[],
                                                      MemSDNode *M) const {
  if (VDataType == MVT::f16 || VDataType == MVT::bf16)
    Ops[1] = DAG.getNode(ISD::BITCAST, DL, MVT::i16, Ops[1]);

  SDValue BufferStoreExt = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i32, Ops[1]);
  Ops[1] = BufferStoreExt;
  unsigned Opc = (VDataType == MVT::i8) ? AMDGPUISD::BUFFER_STORE_BYTE :
                                 AMDGPUISD::BUFFER_STORE_SHORT;
  ArrayRef<SDValue> OpsRef = ArrayRef(&Ops[0], 9);
  return DAG.getMemIntrinsicNode(Opc, DL, M->getVTList(), OpsRef, VDataType,
                                     M->getMemOperand());
}

static SDValue getLoadExtOrTrunc(SelectionDAG &DAG,
                                 ISD::LoadExtType ExtType, SDValue Op,
                                 const SDLoc &SL, EVT VT) {
  if (VT.bitsLT(Op.getValueType()))
    return DAG.getNode(ISD::TRUNCATE, SL, VT, Op);

  switch (ExtType) {
  case ISD::SEXTLOAD:
    return DAG.getNode(ISD::SIGN_EXTEND, SL, VT, Op);
  case ISD::ZEXTLOAD:
    return DAG.getNode(ISD::ZERO_EXTEND, SL, VT, Op);
  case ISD::EXTLOAD:
    return DAG.getNode(ISD::ANY_EXTEND, SL, VT, Op);
  case ISD::NON_EXTLOAD:
    return Op;
  }

  llvm_unreachable("invalid ext type");
}

// Try to turn 8 and 16-bit scalar loads into SMEM eligible 32-bit loads.
// TODO: Skip this on GFX12 which does have scalar sub-dword loads.
SDValue SITargetLowering::widenLoad(LoadSDNode *Ld, DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  if (Ld->getAlign() < Align(4) || Ld->isDivergent())
    return SDValue();

  // FIXME: Constant loads should all be marked invariant.
  unsigned AS = Ld->getAddressSpace();
  if (AS != AMDGPUAS::CONSTANT_ADDRESS &&
      AS != AMDGPUAS::CONSTANT_ADDRESS_32BIT &&
      (AS != AMDGPUAS::GLOBAL_ADDRESS || !Ld->isInvariant()))
    return SDValue();

  // Don't do this early, since it may interfere with adjacent load merging for
  // illegal types. We can avoid losing alignment information for exotic types
  // pre-legalize.
  EVT MemVT = Ld->getMemoryVT();
  if ((MemVT.isSimple() && !DCI.isAfterLegalizeDAG()) ||
      MemVT.getSizeInBits() >= 32)
    return SDValue();

  SDLoc SL(Ld);

  assert((!MemVT.isVector() || Ld->getExtensionType() == ISD::NON_EXTLOAD) &&
         "unexpected vector extload");

  // TODO: Drop only high part of range.
  SDValue Ptr = Ld->getBasePtr();
  SDValue NewLoad = DAG.getLoad(
      ISD::UNINDEXED, ISD::NON_EXTLOAD, MVT::i32, SL, Ld->getChain(), Ptr,
      Ld->getOffset(), Ld->getPointerInfo(), MVT::i32, Ld->getAlign(),
      Ld->getMemOperand()->getFlags(), Ld->getAAInfo(),
      nullptr); // Drop ranges

  EVT TruncVT = EVT::getIntegerVT(*DAG.getContext(), MemVT.getSizeInBits());
  if (MemVT.isFloatingPoint()) {
    assert(Ld->getExtensionType() == ISD::NON_EXTLOAD &&
           "unexpected fp extload");
    TruncVT = MemVT.changeTypeToInteger();
  }

  SDValue Cvt = NewLoad;
  if (Ld->getExtensionType() == ISD::SEXTLOAD) {
    Cvt = DAG.getNode(ISD::SIGN_EXTEND_INREG, SL, MVT::i32, NewLoad,
                      DAG.getValueType(TruncVT));
  } else if (Ld->getExtensionType() == ISD::ZEXTLOAD ||
             Ld->getExtensionType() == ISD::NON_EXTLOAD) {
    Cvt = DAG.getZeroExtendInReg(NewLoad, SL, TruncVT);
  } else {
    assert(Ld->getExtensionType() == ISD::EXTLOAD);
  }

  EVT VT = Ld->getValueType(0);
  EVT IntVT = EVT::getIntegerVT(*DAG.getContext(), VT.getSizeInBits());

  DCI.AddToWorklist(Cvt.getNode());

  // We may need to handle exotic cases, such as i16->i64 extloads, so insert
  // the appropriate extension from the 32-bit load.
  Cvt = getLoadExtOrTrunc(DAG, Ld->getExtensionType(), Cvt, SL, IntVT);
  DCI.AddToWorklist(Cvt.getNode());

  // Handle conversion back to floating point if necessary.
  Cvt = DAG.getNode(ISD::BITCAST, SL, VT, Cvt);

  return DAG.getMergeValues({ Cvt, NewLoad.getValue(1) }, SL);
}

static bool addressMayBeAccessedAsPrivate(const MachineMemOperand *MMO,
                                          const SIMachineFunctionInfo &Info) {
  // TODO: Should check if the address can definitely not access stack.
  if (Info.isEntryFunction())
    return Info.getUserSGPRInfo().hasFlatScratchInit();
  return true;
}

SDValue SITargetLowering::LowerLOAD(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  LoadSDNode *Load = cast<LoadSDNode>(Op);
  ISD::LoadExtType ExtType = Load->getExtensionType();
  EVT MemVT = Load->getMemoryVT();

  if (ExtType == ISD::NON_EXTLOAD && MemVT.getSizeInBits() < 32) {
    if (MemVT == MVT::i16 && isTypeLegal(MVT::i16))
      return SDValue();

    // FIXME: Copied from PPC
    // First, load into 32 bits, then truncate to 1 bit.

    SDValue Chain = Load->getChain();
    SDValue BasePtr = Load->getBasePtr();
    MachineMemOperand *MMO = Load->getMemOperand();

    EVT RealMemVT = (MemVT == MVT::i1) ? MVT::i8 : MVT::i16;

    SDValue NewLD = DAG.getExtLoad(ISD::EXTLOAD, DL, MVT::i32, Chain,
                                   BasePtr, RealMemVT, MMO);

    if (!MemVT.isVector()) {
      SDValue Ops[] = {
        DAG.getNode(ISD::TRUNCATE, DL, MemVT, NewLD),
        NewLD.getValue(1)
      };

      return DAG.getMergeValues(Ops, DL);
    }

    SmallVector<SDValue, 3> Elts;
    for (unsigned I = 0, N = MemVT.getVectorNumElements(); I != N; ++I) {
      SDValue Elt = DAG.getNode(ISD::SRL, DL, MVT::i32, NewLD,
                                DAG.getConstant(I, DL, MVT::i32));

      Elts.push_back(DAG.getNode(ISD::TRUNCATE, DL, MVT::i1, Elt));
    }

    SDValue Ops[] = {
      DAG.getBuildVector(MemVT, DL, Elts),
      NewLD.getValue(1)
    };

    return DAG.getMergeValues(Ops, DL);
  }

  if (!MemVT.isVector())
    return SDValue();

  assert(Op.getValueType().getVectorElementType() == MVT::i32 &&
         "Custom lowering for non-i32 vectors hasn't been implemented.");

  Align Alignment = Load->getAlign();
  unsigned AS = Load->getAddressSpace();
  if (Subtarget->hasLDSMisalignedBug() && AS == AMDGPUAS::FLAT_ADDRESS &&
      Alignment.value() < MemVT.getStoreSize() && MemVT.getSizeInBits() > 32) {
    return SplitVectorLoad(Op, DAG);
  }

  MachineFunction &MF = DAG.getMachineFunction();
  SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  // If there is a possibility that flat instruction access scratch memory
  // then we need to use the same legalization rules we use for private.
  if (AS == AMDGPUAS::FLAT_ADDRESS &&
      !Subtarget->hasMultiDwordFlatScratchAddressing())
    AS = addressMayBeAccessedAsPrivate(Load->getMemOperand(), *MFI) ?
         AMDGPUAS::PRIVATE_ADDRESS : AMDGPUAS::GLOBAL_ADDRESS;

  unsigned NumElements = MemVT.getVectorNumElements();

  if (AS == AMDGPUAS::CONSTANT_ADDRESS ||
      AS == AMDGPUAS::CONSTANT_ADDRESS_32BIT) {
    if (!Op->isDivergent() && Alignment >= Align(4) && NumElements < 32) {
      if (MemVT.isPow2VectorType() ||
          (Subtarget->hasScalarDwordx3Loads() && NumElements == 3))
        return SDValue();
      return WidenOrSplitVectorLoad(Op, DAG);
    }
    // Non-uniform loads will be selected to MUBUF instructions, so they
    // have the same legalization requirements as global and private
    // loads.
    //
  }

  if (AS == AMDGPUAS::CONSTANT_ADDRESS ||
      AS == AMDGPUAS::CONSTANT_ADDRESS_32BIT ||
      AS == AMDGPUAS::GLOBAL_ADDRESS) {
    if (Subtarget->getScalarizeGlobalBehavior() && !Op->isDivergent() &&
        Load->isSimple() && isMemOpHasNoClobberedMemOperand(Load) &&
        Alignment >= Align(4) && NumElements < 32) {
      if (MemVT.isPow2VectorType() ||
          (Subtarget->hasScalarDwordx3Loads() && NumElements == 3))
        return SDValue();
      return WidenOrSplitVectorLoad(Op, DAG);
    }
    // Non-uniform loads will be selected to MUBUF instructions, so they
    // have the same legalization requirements as global and private
    // loads.
    //
  }
  if (AS == AMDGPUAS::CONSTANT_ADDRESS ||
      AS == AMDGPUAS::CONSTANT_ADDRESS_32BIT ||
      AS == AMDGPUAS::GLOBAL_ADDRESS ||
      AS == AMDGPUAS::FLAT_ADDRESS) {
    if (NumElements > 4)
      return SplitVectorLoad(Op, DAG);
    // v3 loads not supported on SI.
    if (NumElements == 3 && !Subtarget->hasDwordx3LoadStores())
      return WidenOrSplitVectorLoad(Op, DAG);

    // v3 and v4 loads are supported for private and global memory.
    return SDValue();
  }
  if (AS == AMDGPUAS::PRIVATE_ADDRESS) {
    // Depending on the setting of the private_element_size field in the
    // resource descriptor, we can only make private accesses up to a certain
    // size.
    switch (Subtarget->getMaxPrivateElementSize()) {
    case 4: {
      SDValue Ops[2];
      std::tie(Ops[0], Ops[1]) = scalarizeVectorLoad(Load, DAG);
      return DAG.getMergeValues(Ops, DL);
    }
    case 8:
      if (NumElements > 2)
        return SplitVectorLoad(Op, DAG);
      return SDValue();
    case 16:
      // Same as global/flat
      if (NumElements > 4)
        return SplitVectorLoad(Op, DAG);
      // v3 loads not supported on SI.
      if (NumElements == 3 && !Subtarget->hasDwordx3LoadStores())
        return WidenOrSplitVectorLoad(Op, DAG);

      return SDValue();
    default:
      llvm_unreachable("unsupported private_element_size");
    }
  } else if (AS == AMDGPUAS::LOCAL_ADDRESS || AS == AMDGPUAS::REGION_ADDRESS) {
    unsigned Fast = 0;
    auto Flags = Load->getMemOperand()->getFlags();
    if (allowsMisalignedMemoryAccessesImpl(MemVT.getSizeInBits(), AS,
                                           Load->getAlign(), Flags, &Fast) &&
        Fast > 1)
      return SDValue();

    if (MemVT.isVector())
      return SplitVectorLoad(Op, DAG);
  }

  if (!allowsMemoryAccessForAlignment(*DAG.getContext(), DAG.getDataLayout(),
                                      MemVT, *Load->getMemOperand())) {
    SDValue Ops[2];
    std::tie(Ops[0], Ops[1]) = expandUnalignedLoad(Load, DAG);
    return DAG.getMergeValues(Ops, DL);
  }

  return SDValue();
}

SDValue SITargetLowering::LowerSELECT(SDValue Op, SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  if (VT.getSizeInBits() == 128 || VT.getSizeInBits() == 256 ||
      VT.getSizeInBits() == 512)
    return splitTernaryVectorOp(Op, DAG);

  assert(VT.getSizeInBits() == 64);

  SDLoc DL(Op);
  SDValue Cond = Op.getOperand(0);

  SDValue Zero = DAG.getConstant(0, DL, MVT::i32);
  SDValue One = DAG.getConstant(1, DL, MVT::i32);

  SDValue LHS = DAG.getNode(ISD::BITCAST, DL, MVT::v2i32, Op.getOperand(1));
  SDValue RHS = DAG.getNode(ISD::BITCAST, DL, MVT::v2i32, Op.getOperand(2));

  SDValue Lo0 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::i32, LHS, Zero);
  SDValue Lo1 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::i32, RHS, Zero);

  SDValue Lo = DAG.getSelect(DL, MVT::i32, Cond, Lo0, Lo1);

  SDValue Hi0 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::i32, LHS, One);
  SDValue Hi1 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::i32, RHS, One);

  SDValue Hi = DAG.getSelect(DL, MVT::i32, Cond, Hi0, Hi1);

  SDValue Res = DAG.getBuildVector(MVT::v2i32, DL, {Lo, Hi});
  return DAG.getNode(ISD::BITCAST, DL, VT, Res);
}

// Catch division cases where we can use shortcuts with rcp and rsq
// instructions.
SDValue SITargetLowering::lowerFastUnsafeFDIV(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  EVT VT = Op.getValueType();
  const SDNodeFlags Flags = Op->getFlags();

  bool AllowInaccurateRcp = Flags.hasApproximateFuncs() ||
                            DAG.getTarget().Options.UnsafeFPMath;

  if (const ConstantFPSDNode *CLHS = dyn_cast<ConstantFPSDNode>(LHS)) {
    // Without !fpmath accuracy information, we can't do more because we don't
    // know exactly whether rcp is accurate enough to meet !fpmath requirement.
    // f16 is always accurate enough
    if (!AllowInaccurateRcp && VT != MVT::f16)
      return SDValue();

    if (CLHS->isExactlyValue(1.0)) {
      // v_rcp_f32 and v_rsq_f32 do not support denormals, and according to
      // the CI documentation has a worst case error of 1 ulp.
      // OpenCL requires <= 2.5 ulp for 1.0 / x, so it should always be OK to
      // use it as long as we aren't trying to use denormals.
      //
      // v_rcp_f16 and v_rsq_f16 DO support denormals and 0.51ulp.

      // 1.0 / sqrt(x) -> rsq(x)

      // XXX - Is UnsafeFPMath sufficient to do this for f64? The maximum ULP
      // error seems really high at 2^29 ULP.
      // 1.0 / x -> rcp(x)
      return DAG.getNode(AMDGPUISD::RCP, SL, VT, RHS);
    }

    // Same as for 1.0, but expand the sign out of the constant.
    if (CLHS->isExactlyValue(-1.0)) {
      // -1.0 / x -> rcp (fneg x)
      SDValue FNegRHS = DAG.getNode(ISD::FNEG, SL, VT, RHS);
      return DAG.getNode(AMDGPUISD::RCP, SL, VT, FNegRHS);
    }
  }

  // For f16 require afn or arcp.
  // For f32 require afn.
  if (!AllowInaccurateRcp && (VT != MVT::f16 || !Flags.hasAllowReciprocal()))
    return SDValue();

  // Turn into multiply by the reciprocal.
  // x / y -> x * (1.0 / y)
  SDValue Recip = DAG.getNode(AMDGPUISD::RCP, SL, VT, RHS);
  return DAG.getNode(ISD::FMUL, SL, VT, LHS, Recip, Flags);
}

SDValue SITargetLowering::lowerFastUnsafeFDIV64(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue X = Op.getOperand(0);
  SDValue Y = Op.getOperand(1);
  EVT VT = Op.getValueType();
  const SDNodeFlags Flags = Op->getFlags();

  bool AllowInaccurateDiv = Flags.hasApproximateFuncs() ||
                            DAG.getTarget().Options.UnsafeFPMath;
  if (!AllowInaccurateDiv)
    return SDValue();

  SDValue NegY = DAG.getNode(ISD::FNEG, SL, VT, Y);
  SDValue One = DAG.getConstantFP(1.0, SL, VT);

  SDValue R = DAG.getNode(AMDGPUISD::RCP, SL, VT, Y);
  SDValue Tmp0 = DAG.getNode(ISD::FMA, SL, VT, NegY, R, One);

  R = DAG.getNode(ISD::FMA, SL, VT, Tmp0, R, R);
  SDValue Tmp1 = DAG.getNode(ISD::FMA, SL, VT, NegY, R, One);
  R = DAG.getNode(ISD::FMA, SL, VT, Tmp1, R, R);
  SDValue Ret = DAG.getNode(ISD::FMUL, SL, VT, X, R);
  SDValue Tmp2 = DAG.getNode(ISD::FMA, SL, VT, NegY, Ret, X);
  return DAG.getNode(ISD::FMA, SL, VT, Tmp2, R, Ret);
}

static SDValue getFPBinOp(SelectionDAG &DAG, unsigned Opcode, const SDLoc &SL,
                          EVT VT, SDValue A, SDValue B, SDValue GlueChain,
                          SDNodeFlags Flags) {
  if (GlueChain->getNumValues() <= 1) {
    return DAG.getNode(Opcode, SL, VT, A, B, Flags);
  }

  assert(GlueChain->getNumValues() == 3);

  SDVTList VTList = DAG.getVTList(VT, MVT::Other, MVT::Glue);
  switch (Opcode) {
  default: llvm_unreachable("no chain equivalent for opcode");
  case ISD::FMUL:
    Opcode = AMDGPUISD::FMUL_W_CHAIN;
    break;
  }

  return DAG.getNode(Opcode, SL, VTList,
                     {GlueChain.getValue(1), A, B, GlueChain.getValue(2)},
                     Flags);
}

static SDValue getFPTernOp(SelectionDAG &DAG, unsigned Opcode, const SDLoc &SL,
                           EVT VT, SDValue A, SDValue B, SDValue C,
                           SDValue GlueChain, SDNodeFlags Flags) {
  if (GlueChain->getNumValues() <= 1) {
    return DAG.getNode(Opcode, SL, VT, {A, B, C}, Flags);
  }

  assert(GlueChain->getNumValues() == 3);

  SDVTList VTList = DAG.getVTList(VT, MVT::Other, MVT::Glue);
  switch (Opcode) {
  default: llvm_unreachable("no chain equivalent for opcode");
  case ISD::FMA:
    Opcode = AMDGPUISD::FMA_W_CHAIN;
    break;
  }

  return DAG.getNode(Opcode, SL, VTList,
                     {GlueChain.getValue(1), A, B, C, GlueChain.getValue(2)},
                     Flags);
}

SDValue SITargetLowering::LowerFDIV16(SDValue Op, SelectionDAG &DAG) const {
  if (SDValue FastLowered = lowerFastUnsafeFDIV(Op, DAG))
    return FastLowered;

  SDLoc SL(Op);
  SDValue Src0 = Op.getOperand(0);
  SDValue Src1 = Op.getOperand(1);

  SDValue CvtSrc0 = DAG.getNode(ISD::FP_EXTEND, SL, MVT::f32, Src0);
  SDValue CvtSrc1 = DAG.getNode(ISD::FP_EXTEND, SL, MVT::f32, Src1);

  SDValue RcpSrc1 = DAG.getNode(AMDGPUISD::RCP, SL, MVT::f32, CvtSrc1);
  SDValue Quot = DAG.getNode(ISD::FMUL, SL, MVT::f32, CvtSrc0, RcpSrc1);

  SDValue FPRoundFlag = DAG.getTargetConstant(0, SL, MVT::i32);
  SDValue BestQuot = DAG.getNode(ISD::FP_ROUND, SL, MVT::f16, Quot, FPRoundFlag);

  return DAG.getNode(AMDGPUISD::DIV_FIXUP, SL, MVT::f16, BestQuot, Src1, Src0);
}

// Faster 2.5 ULP division that does not support denormals.
SDValue SITargetLowering::lowerFDIV_FAST(SDValue Op, SelectionDAG &DAG) const {
  SDNodeFlags Flags = Op->getFlags();
  SDLoc SL(Op);
  SDValue LHS = Op.getOperand(1);
  SDValue RHS = Op.getOperand(2);

  SDValue r1 = DAG.getNode(ISD::FABS, SL, MVT::f32, RHS, Flags);

  const APFloat K0Val(0x1p+96f);
  const SDValue K0 = DAG.getConstantFP(K0Val, SL, MVT::f32);

  const APFloat K1Val(0x1p-32f);
  const SDValue K1 = DAG.getConstantFP(K1Val, SL, MVT::f32);

  const SDValue One = DAG.getConstantFP(1.0, SL, MVT::f32);

  EVT SetCCVT =
    getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), MVT::f32);

  SDValue r2 = DAG.getSetCC(SL, SetCCVT, r1, K0, ISD::SETOGT);

  SDValue r3 = DAG.getNode(ISD::SELECT, SL, MVT::f32, r2, K1, One, Flags);

  r1 = DAG.getNode(ISD::FMUL, SL, MVT::f32, RHS, r3, Flags);

  // rcp does not support denormals.
  SDValue r0 = DAG.getNode(AMDGPUISD::RCP, SL, MVT::f32, r1, Flags);

  SDValue Mul = DAG.getNode(ISD::FMUL, SL, MVT::f32, LHS, r0, Flags);

  return DAG.getNode(ISD::FMUL, SL, MVT::f32, r3, Mul, Flags);
}

// Returns immediate value for setting the F32 denorm mode when using the
// S_DENORM_MODE instruction.
static SDValue getSPDenormModeValue(uint32_t SPDenormMode, SelectionDAG &DAG,
                                    const SIMachineFunctionInfo *Info,
                                    const GCNSubtarget *ST) {
  assert(ST->hasDenormModeInst() && "Requires S_DENORM_MODE");
  uint32_t DPDenormModeDefault = Info->getMode().fpDenormModeDPValue();
  uint32_t Mode = SPDenormMode | (DPDenormModeDefault << 2);
  return DAG.getTargetConstant(Mode, SDLoc(), MVT::i32);
}

SDValue SITargetLowering::LowerFDIV32(SDValue Op, SelectionDAG &DAG) const {
  if (SDValue FastLowered = lowerFastUnsafeFDIV(Op, DAG))
    return FastLowered;

  // The selection matcher assumes anything with a chain selecting to a
  // mayRaiseFPException machine instruction. Since we're introducing a chain
  // here, we need to explicitly report nofpexcept for the regular fdiv
  // lowering.
  SDNodeFlags Flags = Op->getFlags();
  Flags.setNoFPExcept(true);

  SDLoc SL(Op);
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);

  const SDValue One = DAG.getConstantFP(1.0, SL, MVT::f32);

  SDVTList ScaleVT = DAG.getVTList(MVT::f32, MVT::i1);

  SDValue DenominatorScaled = DAG.getNode(AMDGPUISD::DIV_SCALE, SL, ScaleVT,
                                          {RHS, RHS, LHS}, Flags);
  SDValue NumeratorScaled = DAG.getNode(AMDGPUISD::DIV_SCALE, SL, ScaleVT,
                                        {LHS, RHS, LHS}, Flags);

  // Denominator is scaled to not be denormal, so using rcp is ok.
  SDValue ApproxRcp = DAG.getNode(AMDGPUISD::RCP, SL, MVT::f32,
                                  DenominatorScaled, Flags);
  SDValue NegDivScale0 = DAG.getNode(ISD::FNEG, SL, MVT::f32,
                                     DenominatorScaled, Flags);

  using namespace AMDGPU::Hwreg;
  const unsigned Denorm32Reg = HwregEncoding::encode(ID_MODE, 4, 2);
  const SDValue BitField = DAG.getTargetConstant(Denorm32Reg, SL, MVT::i32);

  const MachineFunction &MF = DAG.getMachineFunction();
  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();
  const DenormalMode DenormMode = Info->getMode().FP32Denormals;

  const bool PreservesDenormals = DenormMode == DenormalMode::getIEEE();
  const bool HasDynamicDenormals =
      (DenormMode.Input == DenormalMode::Dynamic) ||
      (DenormMode.Output == DenormalMode::Dynamic);

  SDValue SavedDenormMode;

  if (!PreservesDenormals) {
    // Note we can't use the STRICT_FMA/STRICT_FMUL for the non-strict FDIV
    // lowering. The chain dependence is insufficient, and we need glue. We do
    // not need the glue variants in a strictfp function.

    SDVTList BindParamVTs = DAG.getVTList(MVT::Other, MVT::Glue);

    SDValue Glue = DAG.getEntryNode();
    if (HasDynamicDenormals) {
      SDNode *GetReg = DAG.getMachineNode(AMDGPU::S_GETREG_B32, SL,
                                          DAG.getVTList(MVT::i32, MVT::Glue),
                                          {BitField, Glue});
      SavedDenormMode = SDValue(GetReg, 0);

      Glue = DAG.getMergeValues(
          {DAG.getEntryNode(), SDValue(GetReg, 0), SDValue(GetReg, 1)}, SL);
    }

    SDNode *EnableDenorm;
    if (Subtarget->hasDenormModeInst()) {
      const SDValue EnableDenormValue =
          getSPDenormModeValue(FP_DENORM_FLUSH_NONE, DAG, Info, Subtarget);

      EnableDenorm = DAG.getNode(AMDGPUISD::DENORM_MODE, SL, BindParamVTs, Glue,
                                 EnableDenormValue)
                         .getNode();
    } else {
      const SDValue EnableDenormValue = DAG.getConstant(FP_DENORM_FLUSH_NONE,
                                                        SL, MVT::i32);
      EnableDenorm = DAG.getMachineNode(AMDGPU::S_SETREG_B32, SL, BindParamVTs,
                                        {EnableDenormValue, BitField, Glue});
    }

    SDValue Ops[3] = {
      NegDivScale0,
      SDValue(EnableDenorm, 0),
      SDValue(EnableDenorm, 1)
    };

    NegDivScale0 = DAG.getMergeValues(Ops, SL);
  }

  SDValue Fma0 = getFPTernOp(DAG, ISD::FMA, SL, MVT::f32, NegDivScale0,
                             ApproxRcp, One, NegDivScale0, Flags);

  SDValue Fma1 = getFPTernOp(DAG, ISD::FMA, SL, MVT::f32, Fma0, ApproxRcp,
                             ApproxRcp, Fma0, Flags);

  SDValue Mul = getFPBinOp(DAG, ISD::FMUL, SL, MVT::f32, NumeratorScaled,
                           Fma1, Fma1, Flags);

  SDValue Fma2 = getFPTernOp(DAG, ISD::FMA, SL, MVT::f32, NegDivScale0, Mul,
                             NumeratorScaled, Mul, Flags);

  SDValue Fma3 = getFPTernOp(DAG, ISD::FMA, SL, MVT::f32,
                             Fma2, Fma1, Mul, Fma2, Flags);

  SDValue Fma4 = getFPTernOp(DAG, ISD::FMA, SL, MVT::f32, NegDivScale0, Fma3,
                             NumeratorScaled, Fma3, Flags);

  if (!PreservesDenormals) {
    SDNode *DisableDenorm;
    if (!HasDynamicDenormals && Subtarget->hasDenormModeInst()) {
      const SDValue DisableDenormValue = getSPDenormModeValue(
          FP_DENORM_FLUSH_IN_FLUSH_OUT, DAG, Info, Subtarget);

      DisableDenorm = DAG.getNode(AMDGPUISD::DENORM_MODE, SL, MVT::Other,
                                  Fma4.getValue(1), DisableDenormValue,
                                  Fma4.getValue(2)).getNode();
    } else {
      assert(HasDynamicDenormals == (bool)SavedDenormMode);
      const SDValue DisableDenormValue =
          HasDynamicDenormals
              ? SavedDenormMode
              : DAG.getConstant(FP_DENORM_FLUSH_IN_FLUSH_OUT, SL, MVT::i32);

      DisableDenorm = DAG.getMachineNode(
          AMDGPU::S_SETREG_B32, SL, MVT::Other,
          {DisableDenormValue, BitField, Fma4.getValue(1), Fma4.getValue(2)});
    }

    SDValue OutputChain = DAG.getNode(ISD::TokenFactor, SL, MVT::Other,
                                      SDValue(DisableDenorm, 0), DAG.getRoot());
    DAG.setRoot(OutputChain);
  }

  SDValue Scale = NumeratorScaled.getValue(1);
  SDValue Fmas = DAG.getNode(AMDGPUISD::DIV_FMAS, SL, MVT::f32,
                             {Fma4, Fma1, Fma3, Scale}, Flags);

  return DAG.getNode(AMDGPUISD::DIV_FIXUP, SL, MVT::f32, Fmas, RHS, LHS, Flags);
}

SDValue SITargetLowering::LowerFDIV64(SDValue Op, SelectionDAG &DAG) const {
  if (SDValue FastLowered = lowerFastUnsafeFDIV64(Op, DAG))
    return FastLowered;

  SDLoc SL(Op);
  SDValue X = Op.getOperand(0);
  SDValue Y = Op.getOperand(1);

  const SDValue One = DAG.getConstantFP(1.0, SL, MVT::f64);

  SDVTList ScaleVT = DAG.getVTList(MVT::f64, MVT::i1);

  SDValue DivScale0 = DAG.getNode(AMDGPUISD::DIV_SCALE, SL, ScaleVT, Y, Y, X);

  SDValue NegDivScale0 = DAG.getNode(ISD::FNEG, SL, MVT::f64, DivScale0);

  SDValue Rcp = DAG.getNode(AMDGPUISD::RCP, SL, MVT::f64, DivScale0);

  SDValue Fma0 = DAG.getNode(ISD::FMA, SL, MVT::f64, NegDivScale0, Rcp, One);

  SDValue Fma1 = DAG.getNode(ISD::FMA, SL, MVT::f64, Rcp, Fma0, Rcp);

  SDValue Fma2 = DAG.getNode(ISD::FMA, SL, MVT::f64, NegDivScale0, Fma1, One);

  SDValue DivScale1 = DAG.getNode(AMDGPUISD::DIV_SCALE, SL, ScaleVT, X, Y, X);

  SDValue Fma3 = DAG.getNode(ISD::FMA, SL, MVT::f64, Fma1, Fma2, Fma1);
  SDValue Mul = DAG.getNode(ISD::FMUL, SL, MVT::f64, DivScale1, Fma3);

  SDValue Fma4 = DAG.getNode(ISD::FMA, SL, MVT::f64,
                             NegDivScale0, Mul, DivScale1);

  SDValue Scale;

  if (!Subtarget->hasUsableDivScaleConditionOutput()) {
    // Workaround a hardware bug on SI where the condition output from div_scale
    // is not usable.

    const SDValue Hi = DAG.getConstant(1, SL, MVT::i32);

    // Figure out if the scale to use for div_fmas.
    SDValue NumBC = DAG.getNode(ISD::BITCAST, SL, MVT::v2i32, X);
    SDValue DenBC = DAG.getNode(ISD::BITCAST, SL, MVT::v2i32, Y);
    SDValue Scale0BC = DAG.getNode(ISD::BITCAST, SL, MVT::v2i32, DivScale0);
    SDValue Scale1BC = DAG.getNode(ISD::BITCAST, SL, MVT::v2i32, DivScale1);

    SDValue NumHi = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, NumBC, Hi);
    SDValue DenHi = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, DenBC, Hi);

    SDValue Scale0Hi
      = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, Scale0BC, Hi);
    SDValue Scale1Hi
      = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, Scale1BC, Hi);

    SDValue CmpDen = DAG.getSetCC(SL, MVT::i1, DenHi, Scale0Hi, ISD::SETEQ);
    SDValue CmpNum = DAG.getSetCC(SL, MVT::i1, NumHi, Scale1Hi, ISD::SETEQ);
    Scale = DAG.getNode(ISD::XOR, SL, MVT::i1, CmpNum, CmpDen);
  } else {
    Scale = DivScale1.getValue(1);
  }

  SDValue Fmas = DAG.getNode(AMDGPUISD::DIV_FMAS, SL, MVT::f64,
                             Fma4, Fma3, Mul, Scale);

  return DAG.getNode(AMDGPUISD::DIV_FIXUP, SL, MVT::f64, Fmas, Y, X);
}

SDValue SITargetLowering::LowerFDIV(SDValue Op, SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();

  if (VT == MVT::f32)
    return LowerFDIV32(Op, DAG);

  if (VT == MVT::f64)
    return LowerFDIV64(Op, DAG);

  if (VT == MVT::f16)
    return LowerFDIV16(Op, DAG);

  llvm_unreachable("Unexpected type for fdiv");
}

SDValue SITargetLowering::LowerFFREXP(SDValue Op, SelectionDAG &DAG) const {
  SDLoc dl(Op);
  SDValue Val = Op.getOperand(0);
  EVT VT = Val.getValueType();
  EVT ResultExpVT = Op->getValueType(1);
  EVT InstrExpVT = VT == MVT::f16 ? MVT::i16 : MVT::i32;

  SDValue Mant = DAG.getNode(
      ISD::INTRINSIC_WO_CHAIN, dl, VT,
      DAG.getTargetConstant(Intrinsic::amdgcn_frexp_mant, dl, MVT::i32), Val);

  SDValue Exp = DAG.getNode(
      ISD::INTRINSIC_WO_CHAIN, dl, InstrExpVT,
      DAG.getTargetConstant(Intrinsic::amdgcn_frexp_exp, dl, MVT::i32), Val);

  if (Subtarget->hasFractBug()) {
    SDValue Fabs = DAG.getNode(ISD::FABS, dl, VT, Val);
    SDValue Inf = DAG.getConstantFP(
        APFloat::getInf(SelectionDAG::EVTToAPFloatSemantics(VT)), dl, VT);

    SDValue IsFinite = DAG.getSetCC(dl, MVT::i1, Fabs, Inf, ISD::SETOLT);
    SDValue Zero = DAG.getConstant(0, dl, InstrExpVT);
    Exp = DAG.getNode(ISD::SELECT, dl, InstrExpVT, IsFinite, Exp, Zero);
    Mant = DAG.getNode(ISD::SELECT, dl, VT, IsFinite, Mant, Val);
  }

  SDValue CastExp = DAG.getSExtOrTrunc(Exp, dl, ResultExpVT);
  return DAG.getMergeValues({Mant, CastExp}, dl);
}

SDValue SITargetLowering::LowerSTORE(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  StoreSDNode *Store = cast<StoreSDNode>(Op);
  EVT VT = Store->getMemoryVT();

  if (VT == MVT::i1) {
    return DAG.getTruncStore(Store->getChain(), DL,
       DAG.getSExtOrTrunc(Store->getValue(), DL, MVT::i32),
       Store->getBasePtr(), MVT::i1, Store->getMemOperand());
  }

  assert(VT.isVector() &&
         Store->getValue().getValueType().getScalarType() == MVT::i32);

  unsigned AS = Store->getAddressSpace();
  if (Subtarget->hasLDSMisalignedBug() &&
      AS == AMDGPUAS::FLAT_ADDRESS &&
      Store->getAlign().value() < VT.getStoreSize() && VT.getSizeInBits() > 32) {
    return SplitVectorStore(Op, DAG);
  }

  MachineFunction &MF = DAG.getMachineFunction();
  SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  // If there is a possibility that flat instruction access scratch memory
  // then we need to use the same legalization rules we use for private.
  if (AS == AMDGPUAS::FLAT_ADDRESS &&
      !Subtarget->hasMultiDwordFlatScratchAddressing())
    AS = addressMayBeAccessedAsPrivate(Store->getMemOperand(), *MFI) ?
         AMDGPUAS::PRIVATE_ADDRESS : AMDGPUAS::GLOBAL_ADDRESS;

  unsigned NumElements = VT.getVectorNumElements();
  if (AS == AMDGPUAS::GLOBAL_ADDRESS ||
      AS == AMDGPUAS::FLAT_ADDRESS) {
    if (NumElements > 4)
      return SplitVectorStore(Op, DAG);
    // v3 stores not supported on SI.
    if (NumElements == 3 && !Subtarget->hasDwordx3LoadStores())
      return SplitVectorStore(Op, DAG);

    if (!allowsMemoryAccessForAlignment(*DAG.getContext(), DAG.getDataLayout(),
                                        VT, *Store->getMemOperand()))
      return expandUnalignedStore(Store, DAG);

    return SDValue();
  }
  if (AS == AMDGPUAS::PRIVATE_ADDRESS) {
    switch (Subtarget->getMaxPrivateElementSize()) {
    case 4:
      return scalarizeVectorStore(Store, DAG);
    case 8:
      if (NumElements > 2)
        return SplitVectorStore(Op, DAG);
      return SDValue();
    case 16:
      if (NumElements > 4 ||
          (NumElements == 3 && !Subtarget->enableFlatScratch()))
        return SplitVectorStore(Op, DAG);
      return SDValue();
    default:
      llvm_unreachable("unsupported private_element_size");
    }
  } else if (AS == AMDGPUAS::LOCAL_ADDRESS || AS == AMDGPUAS::REGION_ADDRESS) {
    unsigned Fast = 0;
    auto Flags = Store->getMemOperand()->getFlags();
    if (allowsMisalignedMemoryAccessesImpl(VT.getSizeInBits(), AS,
                                           Store->getAlign(), Flags, &Fast) &&
        Fast > 1)
      return SDValue();

    if (VT.isVector())
      return SplitVectorStore(Op, DAG);

    return expandUnalignedStore(Store, DAG);
  }

  // Probably an invalid store. If so we'll end up emitting a selection error.
  return SDValue();
}

// Avoid the full correct expansion for f32 sqrt when promoting from f16.
SDValue SITargetLowering::lowerFSQRTF16(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  assert(!Subtarget->has16BitInsts());
  SDNodeFlags Flags = Op->getFlags();
  SDValue Ext =
      DAG.getNode(ISD::FP_EXTEND, SL, MVT::f32, Op.getOperand(0), Flags);

  SDValue SqrtID = DAG.getTargetConstant(Intrinsic::amdgcn_sqrt, SL, MVT::i32);
  SDValue Sqrt =
      DAG.getNode(ISD::INTRINSIC_WO_CHAIN, SL, MVT::f32, SqrtID, Ext, Flags);

  return DAG.getNode(ISD::FP_ROUND, SL, MVT::f16, Sqrt,
                     DAG.getTargetConstant(0, SL, MVT::i32), Flags);
}

SDValue SITargetLowering::lowerFSQRTF32(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDNodeFlags Flags = Op->getFlags();
  MVT VT = Op.getValueType().getSimpleVT();
  const SDValue X = Op.getOperand(0);

  if (allowApproxFunc(DAG, Flags)) {
    // Instruction is 1ulp but ignores denormals.
    return DAG.getNode(
        ISD::INTRINSIC_WO_CHAIN, DL, VT,
        DAG.getTargetConstant(Intrinsic::amdgcn_sqrt, DL, MVT::i32), X, Flags);
  }

  SDValue ScaleThreshold = DAG.getConstantFP(0x1.0p-96f, DL, VT);
  SDValue NeedScale = DAG.getSetCC(DL, MVT::i1, X, ScaleThreshold, ISD::SETOLT);

  SDValue ScaleUpFactor = DAG.getConstantFP(0x1.0p+32f, DL, VT);

  SDValue ScaledX = DAG.getNode(ISD::FMUL, DL, VT, X, ScaleUpFactor, Flags);

  SDValue SqrtX =
      DAG.getNode(ISD::SELECT, DL, VT, NeedScale, ScaledX, X, Flags);

  SDValue SqrtS;
  if (needsDenormHandlingF32(DAG, X, Flags)) {
    SDValue SqrtID =
        DAG.getTargetConstant(Intrinsic::amdgcn_sqrt, DL, MVT::i32);
    SqrtS = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, DL, VT, SqrtID, SqrtX, Flags);

    SDValue SqrtSAsInt = DAG.getNode(ISD::BITCAST, DL, MVT::i32, SqrtS);
    SDValue SqrtSNextDownInt = DAG.getNode(ISD::ADD, DL, MVT::i32, SqrtSAsInt,
                                           DAG.getConstant(-1, DL, MVT::i32));
    SDValue SqrtSNextDown = DAG.getNode(ISD::BITCAST, DL, VT, SqrtSNextDownInt);

    SDValue NegSqrtSNextDown =
        DAG.getNode(ISD::FNEG, DL, VT, SqrtSNextDown, Flags);

    SDValue SqrtVP =
        DAG.getNode(ISD::FMA, DL, VT, NegSqrtSNextDown, SqrtS, SqrtX, Flags);

    SDValue SqrtSNextUpInt = DAG.getNode(ISD::ADD, DL, MVT::i32, SqrtSAsInt,
                                         DAG.getConstant(1, DL, MVT::i32));
    SDValue SqrtSNextUp = DAG.getNode(ISD::BITCAST, DL, VT, SqrtSNextUpInt);

    SDValue NegSqrtSNextUp = DAG.getNode(ISD::FNEG, DL, VT, SqrtSNextUp, Flags);
    SDValue SqrtVS =
        DAG.getNode(ISD::FMA, DL, VT, NegSqrtSNextUp, SqrtS, SqrtX, Flags);

    SDValue Zero = DAG.getConstantFP(0.0f, DL, VT);
    SDValue SqrtVPLE0 = DAG.getSetCC(DL, MVT::i1, SqrtVP, Zero, ISD::SETOLE);

    SqrtS = DAG.getNode(ISD::SELECT, DL, VT, SqrtVPLE0, SqrtSNextDown, SqrtS,
                        Flags);

    SDValue SqrtVPVSGT0 = DAG.getSetCC(DL, MVT::i1, SqrtVS, Zero, ISD::SETOGT);
    SqrtS = DAG.getNode(ISD::SELECT, DL, VT, SqrtVPVSGT0, SqrtSNextUp, SqrtS,
                        Flags);
  } else {
    SDValue SqrtR = DAG.getNode(AMDGPUISD::RSQ, DL, VT, SqrtX, Flags);

    SqrtS = DAG.getNode(ISD::FMUL, DL, VT, SqrtX, SqrtR, Flags);

    SDValue Half = DAG.getConstantFP(0.5f, DL, VT);
    SDValue SqrtH = DAG.getNode(ISD::FMUL, DL, VT, SqrtR, Half, Flags);
    SDValue NegSqrtH = DAG.getNode(ISD::FNEG, DL, VT, SqrtH, Flags);

    SDValue SqrtE = DAG.getNode(ISD::FMA, DL, VT, NegSqrtH, SqrtS, Half, Flags);
    SqrtH = DAG.getNode(ISD::FMA, DL, VT, SqrtH, SqrtE, SqrtH, Flags);
    SqrtS = DAG.getNode(ISD::FMA, DL, VT, SqrtS, SqrtE, SqrtS, Flags);

    SDValue NegSqrtS = DAG.getNode(ISD::FNEG, DL, VT, SqrtS, Flags);
    SDValue SqrtD =
        DAG.getNode(ISD::FMA, DL, VT, NegSqrtS, SqrtS, SqrtX, Flags);
    SqrtS = DAG.getNode(ISD::FMA, DL, VT, SqrtD, SqrtH, SqrtS, Flags);
  }

  SDValue ScaleDownFactor = DAG.getConstantFP(0x1.0p-16f, DL, VT);

  SDValue ScaledDown =
      DAG.getNode(ISD::FMUL, DL, VT, SqrtS, ScaleDownFactor, Flags);

  SqrtS = DAG.getNode(ISD::SELECT, DL, VT, NeedScale, ScaledDown, SqrtS, Flags);
  SDValue IsZeroOrInf =
      DAG.getNode(ISD::IS_FPCLASS, DL, MVT::i1, SqrtX,
                  DAG.getTargetConstant(fcZero | fcPosInf, DL, MVT::i32));

  return DAG.getNode(ISD::SELECT, DL, VT, IsZeroOrInf, SqrtX, SqrtS, Flags);
}

SDValue SITargetLowering::lowerFSQRTF64(SDValue Op, SelectionDAG &DAG) const {
  // For double type, the SQRT and RSQ instructions don't have required
  // precision, we apply Goldschmidt's algorithm to improve the result:
  //
  //   y0 = rsq(x)
  //   g0 = x * y0
  //   h0 = 0.5 * y0
  //
  //   r0 = 0.5 - h0 * g0
  //   g1 = g0 * r0 + g0
  //   h1 = h0 * r0 + h0
  //
  //   r1 = 0.5 - h1 * g1 => d0 = x - g1 * g1
  //   g2 = g1 * r1 + g1     g2 = d0 * h1 + g1
  //   h2 = h1 * r1 + h1
  //
  //   r2 = 0.5 - h2 * g2 => d1 = x - g2 * g2
  //   g3 = g2 * r2 + g2     g3 = d1 * h1 + g2
  //
  //   sqrt(x) = g3

  SDNodeFlags Flags = Op->getFlags();

  SDLoc DL(Op);

  SDValue X = Op.getOperand(0);
  SDValue ScaleConstant = DAG.getConstantFP(0x1.0p-767, DL, MVT::f64);

  SDValue Scaling = DAG.getSetCC(DL, MVT::i1, X, ScaleConstant, ISD::SETOLT);

  SDValue ZeroInt = DAG.getConstant(0, DL, MVT::i32);

  // Scale up input if it is too small.
  SDValue ScaleUpFactor = DAG.getConstant(256, DL, MVT::i32);
  SDValue ScaleUp =
      DAG.getNode(ISD::SELECT, DL, MVT::i32, Scaling, ScaleUpFactor, ZeroInt);
  SDValue SqrtX = DAG.getNode(ISD::FLDEXP, DL, MVT::f64, X, ScaleUp, Flags);

  SDValue SqrtY = DAG.getNode(AMDGPUISD::RSQ, DL, MVT::f64, SqrtX);

  SDValue SqrtS0 = DAG.getNode(ISD::FMUL, DL, MVT::f64, SqrtX, SqrtY);

  SDValue Half = DAG.getConstantFP(0.5, DL, MVT::f64);
  SDValue SqrtH0 = DAG.getNode(ISD::FMUL, DL, MVT::f64, SqrtY, Half);

  SDValue NegSqrtH0 = DAG.getNode(ISD::FNEG, DL, MVT::f64, SqrtH0);
  SDValue SqrtR0 = DAG.getNode(ISD::FMA, DL, MVT::f64, NegSqrtH0, SqrtS0, Half);

  SDValue SqrtH1 = DAG.getNode(ISD::FMA, DL, MVT::f64, SqrtH0, SqrtR0, SqrtH0);

  SDValue SqrtS1 = DAG.getNode(ISD::FMA, DL, MVT::f64, SqrtS0, SqrtR0, SqrtS0);

  SDValue NegSqrtS1 = DAG.getNode(ISD::FNEG, DL, MVT::f64, SqrtS1);
  SDValue SqrtD0 = DAG.getNode(ISD::FMA, DL, MVT::f64, NegSqrtS1, SqrtS1, SqrtX);

  SDValue SqrtS2 = DAG.getNode(ISD::FMA, DL, MVT::f64, SqrtD0, SqrtH1, SqrtS1);

  SDValue NegSqrtS2 = DAG.getNode(ISD::FNEG, DL, MVT::f64, SqrtS2);
  SDValue SqrtD1 =
      DAG.getNode(ISD::FMA, DL, MVT::f64, NegSqrtS2, SqrtS2, SqrtX);

  SDValue SqrtRet = DAG.getNode(ISD::FMA, DL, MVT::f64, SqrtD1, SqrtH1, SqrtS2);

  SDValue ScaleDownFactor = DAG.getConstant(-128, DL, MVT::i32);
  SDValue ScaleDown =
      DAG.getNode(ISD::SELECT, DL, MVT::i32, Scaling, ScaleDownFactor, ZeroInt);
  SqrtRet = DAG.getNode(ISD::FLDEXP, DL, MVT::f64, SqrtRet, ScaleDown, Flags);

  // TODO: Switch to fcmp oeq 0 for finite only. Can't fully remove this check
  // with finite only or nsz because rsq(+/-0) = +/-inf

  // TODO: Check for DAZ and expand to subnormals
  SDValue IsZeroOrInf =
      DAG.getNode(ISD::IS_FPCLASS, DL, MVT::i1, SqrtX,
                  DAG.getTargetConstant(fcZero | fcPosInf, DL, MVT::i32));

  // If x is +INF, +0, or -0, use its original value
  return DAG.getNode(ISD::SELECT, DL, MVT::f64, IsZeroOrInf, SqrtX, SqrtRet,
                     Flags);
}

SDValue SITargetLowering::LowerTrig(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  SDValue Arg = Op.getOperand(0);
  SDValue TrigVal;

  // Propagate fast-math flags so that the multiply we introduce can be folded
  // if Arg is already the result of a multiply by constant.
  auto Flags = Op->getFlags();

  SDValue OneOver2Pi = DAG.getConstantFP(0.5 * numbers::inv_pi, DL, VT);

  if (Subtarget->hasTrigReducedRange()) {
    SDValue MulVal = DAG.getNode(ISD::FMUL, DL, VT, Arg, OneOver2Pi, Flags);
    TrigVal = DAG.getNode(AMDGPUISD::FRACT, DL, VT, MulVal, Flags);
  } else {
    TrigVal = DAG.getNode(ISD::FMUL, DL, VT, Arg, OneOver2Pi, Flags);
  }

  switch (Op.getOpcode()) {
  case ISD::FCOS:
    return DAG.getNode(AMDGPUISD::COS_HW, SDLoc(Op), VT, TrigVal, Flags);
  case ISD::FSIN:
    return DAG.getNode(AMDGPUISD::SIN_HW, SDLoc(Op), VT, TrigVal, Flags);
  default:
    llvm_unreachable("Wrong trig opcode");
  }
}

SDValue SITargetLowering::LowerATOMIC_CMP_SWAP(SDValue Op, SelectionDAG &DAG) const {
  AtomicSDNode *AtomicNode = cast<AtomicSDNode>(Op);
  assert(AtomicNode->isCompareAndSwap());
  unsigned AS = AtomicNode->getAddressSpace();

  // No custom lowering required for local address space
  if (!AMDGPU::isFlatGlobalAddrSpace(AS))
    return Op;

  // Non-local address space requires custom lowering for atomic compare
  // and swap; cmp and swap should be in a v2i32 or v2i64 in case of _X2
  SDLoc DL(Op);
  SDValue ChainIn = Op.getOperand(0);
  SDValue Addr = Op.getOperand(1);
  SDValue Old = Op.getOperand(2);
  SDValue New = Op.getOperand(3);
  EVT VT = Op.getValueType();
  MVT SimpleVT = VT.getSimpleVT();
  MVT VecType = MVT::getVectorVT(SimpleVT, 2);

  SDValue NewOld = DAG.getBuildVector(VecType, DL, {New, Old});
  SDValue Ops[] = { ChainIn, Addr, NewOld };

  return DAG.getMemIntrinsicNode(AMDGPUISD::ATOMIC_CMP_SWAP, DL, Op->getVTList(),
                                 Ops, VT, AtomicNode->getMemOperand());
}

//===----------------------------------------------------------------------===//
// Custom DAG optimizations
//===----------------------------------------------------------------------===//

SDValue SITargetLowering::performUCharToFloatCombine(SDNode *N,
                                                     DAGCombinerInfo &DCI) const {
  EVT VT = N->getValueType(0);
  EVT ScalarVT = VT.getScalarType();
  if (ScalarVT != MVT::f32 && ScalarVT != MVT::f16)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  SDValue Src = N->getOperand(0);
  EVT SrcVT = Src.getValueType();

  // TODO: We could try to match extracting the higher bytes, which would be
  // easier if i8 vectors weren't promoted to i32 vectors, particularly after
  // types are legalized. v4i8 -> v4f32 is probably the only case to worry
  // about in practice.
  if (DCI.isAfterLegalizeDAG() && SrcVT == MVT::i32) {
    if (DAG.MaskedValueIsZero(Src, APInt::getHighBitsSet(32, 24))) {
      SDValue Cvt = DAG.getNode(AMDGPUISD::CVT_F32_UBYTE0, DL, MVT::f32, Src);
      DCI.AddToWorklist(Cvt.getNode());

      // For the f16 case, fold to a cast to f32 and then cast back to f16.
      if (ScalarVT != MVT::f32) {
        Cvt = DAG.getNode(ISD::FP_ROUND, DL, VT, Cvt,
                          DAG.getTargetConstant(0, DL, MVT::i32));
      }
      return Cvt;
    }
  }

  return SDValue();
}

SDValue SITargetLowering::performFCopySignCombine(SDNode *N,
                                                  DAGCombinerInfo &DCI) const {
  SDValue MagnitudeOp = N->getOperand(0);
  SDValue SignOp = N->getOperand(1);
  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  // f64 fcopysign is really an f32 copysign on the high bits, so replace the
  // lower half with a copy.
  // fcopysign f64:x, _:y -> x.lo32, (fcopysign (f32 x.hi32), _:y)
  if (MagnitudeOp.getValueType() == MVT::f64) {
    SDValue MagAsVector = DAG.getNode(ISD::BITCAST, DL, MVT::v2f32, MagnitudeOp);
    SDValue MagLo =
      DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::f32, MagAsVector,
                  DAG.getConstant(0, DL, MVT::i32));
    SDValue MagHi =
      DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::f32, MagAsVector,
                  DAG.getConstant(1, DL, MVT::i32));

    SDValue HiOp =
      DAG.getNode(ISD::FCOPYSIGN, DL, MVT::f32, MagHi, SignOp);

    SDValue Vector = DAG.getNode(ISD::BUILD_VECTOR, DL, MVT::v2f32, MagLo, HiOp);

    return DAG.getNode(ISD::BITCAST, DL, MVT::f64, Vector);
  }

  if (SignOp.getValueType() != MVT::f64)
    return SDValue();

  // Reduce width of sign operand, we only need the highest bit.
  //
  // fcopysign f64:x, f64:y ->
  //   fcopysign f64:x, (extract_vector_elt (bitcast f64:y to v2f32), 1)
  // TODO: In some cases it might make sense to go all the way to f16.
  SDValue SignAsVector = DAG.getNode(ISD::BITCAST, DL, MVT::v2f32, SignOp);
  SDValue SignAsF32 =
      DAG.getNode(ISD::EXTRACT_VECTOR_ELT, DL, MVT::f32, SignAsVector,
                  DAG.getConstant(1, DL, MVT::i32));

  return DAG.getNode(ISD::FCOPYSIGN, DL, N->getValueType(0), N->getOperand(0),
                     SignAsF32);
}

// (shl (add x, c1), c2) -> add (shl x, c2), (shl c1, c2)
// (shl (or x, c1), c2) -> add (shl x, c2), (shl c1, c2) iff x and c1 share no
// bits

// This is a variant of
// (mul (add x, c1), c2) -> add (mul x, c2), (mul c1, c2),
//
// The normal DAG combiner will do this, but only if the add has one use since
// that would increase the number of instructions.
//
// This prevents us from seeing a constant offset that can be folded into a
// memory instruction's addressing mode. If we know the resulting add offset of
// a pointer can be folded into an addressing offset, we can replace the pointer
// operand with the add of new constant offset. This eliminates one of the uses,
// and may allow the remaining use to also be simplified.
//
SDValue SITargetLowering::performSHLPtrCombine(SDNode *N,
                                               unsigned AddrSpace,
                                               EVT MemVT,
                                               DAGCombinerInfo &DCI) const {
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  // We only do this to handle cases where it's profitable when there are
  // multiple uses of the add, so defer to the standard combine.
  if ((N0.getOpcode() != ISD::ADD && N0.getOpcode() != ISD::OR) ||
      N0->hasOneUse())
    return SDValue();

  const ConstantSDNode *CN1 = dyn_cast<ConstantSDNode>(N1);
  if (!CN1)
    return SDValue();

  const ConstantSDNode *CAdd = dyn_cast<ConstantSDNode>(N0.getOperand(1));
  if (!CAdd)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;

  if (N0->getOpcode() == ISD::OR &&
      !DAG.haveNoCommonBitsSet(N0.getOperand(0), N0.getOperand(1)))
    return SDValue();

  // If the resulting offset is too large, we can't fold it into the
  // addressing mode offset.
  APInt Offset = CAdd->getAPIntValue() << CN1->getAPIntValue();
  Type *Ty = MemVT.getTypeForEVT(*DCI.DAG.getContext());

  AddrMode AM;
  AM.HasBaseReg = true;
  AM.BaseOffs = Offset.getSExtValue();
  if (!isLegalAddressingMode(DCI.DAG.getDataLayout(), AM, Ty, AddrSpace))
    return SDValue();

  SDLoc SL(N);
  EVT VT = N->getValueType(0);

  SDValue ShlX = DAG.getNode(ISD::SHL, SL, VT, N0.getOperand(0), N1);
  SDValue COffset = DAG.getConstant(Offset, SL, VT);

  SDNodeFlags Flags;
  Flags.setNoUnsignedWrap(N->getFlags().hasNoUnsignedWrap() &&
                          (N0.getOpcode() == ISD::OR ||
                           N0->getFlags().hasNoUnsignedWrap()));

  return DAG.getNode(ISD::ADD, SL, VT, ShlX, COffset, Flags);
}

/// MemSDNode::getBasePtr() does not work for intrinsics, which needs to offset
/// by the chain and intrinsic ID. Theoretically we would also need to check the
/// specific intrinsic, but they all place the pointer operand first.
static unsigned getBasePtrIndex(const MemSDNode *N) {
  switch (N->getOpcode()) {
  case ISD::STORE:
  case ISD::INTRINSIC_W_CHAIN:
  case ISD::INTRINSIC_VOID:
    return 2;
  default:
    return 1;
  }
}

SDValue SITargetLowering::performMemSDNodeCombine(MemSDNode *N,
                                                  DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDLoc SL(N);

  unsigned PtrIdx = getBasePtrIndex(N);
  SDValue Ptr = N->getOperand(PtrIdx);

  // TODO: We could also do this for multiplies.
  if (Ptr.getOpcode() == ISD::SHL) {
    SDValue NewPtr = performSHLPtrCombine(Ptr.getNode(),  N->getAddressSpace(),
                                          N->getMemoryVT(), DCI);
    if (NewPtr) {
      SmallVector<SDValue, 8> NewOps(N->op_begin(), N->op_end());

      NewOps[PtrIdx] = NewPtr;
      return SDValue(DAG.UpdateNodeOperands(N, NewOps), 0);
    }
  }

  return SDValue();
}

static bool bitOpWithConstantIsReducible(unsigned Opc, uint32_t Val) {
  return (Opc == ISD::AND && (Val == 0 || Val == 0xffffffff)) ||
         (Opc == ISD::OR && (Val == 0xffffffff || Val == 0)) ||
         (Opc == ISD::XOR && Val == 0);
}

// Break up 64-bit bit operation of a constant into two 32-bit and/or/xor. This
// will typically happen anyway for a VALU 64-bit and. This exposes other 32-bit
// integer combine opportunities since most 64-bit operations are decomposed
// this way.  TODO: We won't want this for SALU especially if it is an inline
// immediate.
SDValue SITargetLowering::splitBinaryBitConstantOp(
  DAGCombinerInfo &DCI,
  const SDLoc &SL,
  unsigned Opc, SDValue LHS,
  const ConstantSDNode *CRHS) const {
  uint64_t Val = CRHS->getZExtValue();
  uint32_t ValLo = Lo_32(Val);
  uint32_t ValHi = Hi_32(Val);
  const SIInstrInfo *TII = getSubtarget()->getInstrInfo();

    if ((bitOpWithConstantIsReducible(Opc, ValLo) ||
         bitOpWithConstantIsReducible(Opc, ValHi)) ||
        (CRHS->hasOneUse() && !TII->isInlineConstant(CRHS->getAPIntValue()))) {
    // If we need to materialize a 64-bit immediate, it will be split up later
    // anyway. Avoid creating the harder to understand 64-bit immediate
    // materialization.
    return splitBinaryBitConstantOpImpl(DCI, SL, Opc, LHS, ValLo, ValHi);
  }

  return SDValue();
}

bool llvm::isBoolSGPR(SDValue V) {
  if (V.getValueType() != MVT::i1)
    return false;
  switch (V.getOpcode()) {
  default:
    break;
  case ISD::SETCC:
  case AMDGPUISD::FP_CLASS:
    return true;
  case ISD::AND:
  case ISD::OR:
  case ISD::XOR:
    return isBoolSGPR(V.getOperand(0)) && isBoolSGPR(V.getOperand(1));
  }
  return false;
}

// If a constant has all zeroes or all ones within each byte return it.
// Otherwise return 0.
static uint32_t getConstantPermuteMask(uint32_t C) {
  // 0xff for any zero byte in the mask
  uint32_t ZeroByteMask = 0;
  if (!(C & 0x000000ff)) ZeroByteMask |= 0x000000ff;
  if (!(C & 0x0000ff00)) ZeroByteMask |= 0x0000ff00;
  if (!(C & 0x00ff0000)) ZeroByteMask |= 0x00ff0000;
  if (!(C & 0xff000000)) ZeroByteMask |= 0xff000000;
  uint32_t NonZeroByteMask = ~ZeroByteMask; // 0xff for any non-zero byte
  if ((NonZeroByteMask & C) != NonZeroByteMask)
    return 0; // Partial bytes selected.
  return C;
}

// Check if a node selects whole bytes from its operand 0 starting at a byte
// boundary while masking the rest. Returns select mask as in the v_perm_b32
// or -1 if not succeeded.
// Note byte select encoding:
// value 0-3 selects corresponding source byte;
// value 0xc selects zero;
// value 0xff selects 0xff.
static uint32_t getPermuteMask(SDValue V) {
  assert(V.getValueSizeInBits() == 32);

  if (V.getNumOperands() != 2)
    return ~0;

  ConstantSDNode *N1 = dyn_cast<ConstantSDNode>(V.getOperand(1));
  if (!N1)
    return ~0;

  uint32_t C = N1->getZExtValue();

  switch (V.getOpcode()) {
  default:
    break;
  case ISD::AND:
    if (uint32_t ConstMask = getConstantPermuteMask(C))
      return (0x03020100 & ConstMask) | (0x0c0c0c0c & ~ConstMask);
    break;

  case ISD::OR:
    if (uint32_t ConstMask = getConstantPermuteMask(C))
      return (0x03020100 & ~ConstMask) | ConstMask;
    break;

  case ISD::SHL:
    if (C % 8)
      return ~0;

    return uint32_t((0x030201000c0c0c0cull << C) >> 32);

  case ISD::SRL:
    if (C % 8)
      return ~0;

    return uint32_t(0x0c0c0c0c03020100ull >> C);
  }

  return ~0;
}

SDValue SITargetLowering::performAndCombine(SDNode *N,
                                            DAGCombinerInfo &DCI) const {
  if (DCI.isBeforeLegalize())
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  EVT VT = N->getValueType(0);
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);


  const ConstantSDNode *CRHS = dyn_cast<ConstantSDNode>(RHS);
  if (VT == MVT::i64 && CRHS) {
    if (SDValue Split
        = splitBinaryBitConstantOp(DCI, SDLoc(N), ISD::AND, LHS, CRHS))
      return Split;
  }

  if (CRHS && VT == MVT::i32) {
    // and (srl x, c), mask => shl (bfe x, nb + c, mask >> nb), nb
    // nb = number of trailing zeroes in mask
    // It can be optimized out using SDWA for GFX8+ in the SDWA peephole pass,
    // given that we are selecting 8 or 16 bit fields starting at byte boundary.
    uint64_t Mask = CRHS->getZExtValue();
    unsigned Bits = llvm::popcount(Mask);
    if (getSubtarget()->hasSDWA() && LHS->getOpcode() == ISD::SRL &&
        (Bits == 8 || Bits == 16) && isShiftedMask_64(Mask) && !(Mask & 1)) {
      if (auto *CShift = dyn_cast<ConstantSDNode>(LHS->getOperand(1))) {
        unsigned Shift = CShift->getZExtValue();
        unsigned NB = CRHS->getAPIntValue().countr_zero();
        unsigned Offset = NB + Shift;
        if ((Offset & (Bits - 1)) == 0) { // Starts at a byte or word boundary.
          SDLoc SL(N);
          SDValue BFE = DAG.getNode(AMDGPUISD::BFE_U32, SL, MVT::i32,
                                    LHS->getOperand(0),
                                    DAG.getConstant(Offset, SL, MVT::i32),
                                    DAG.getConstant(Bits, SL, MVT::i32));
          EVT NarrowVT = EVT::getIntegerVT(*DAG.getContext(), Bits);
          SDValue Ext = DAG.getNode(ISD::AssertZext, SL, VT, BFE,
                                    DAG.getValueType(NarrowVT));
          SDValue Shl = DAG.getNode(ISD::SHL, SDLoc(LHS), VT, Ext,
                                    DAG.getConstant(NB, SDLoc(CRHS), MVT::i32));
          return Shl;
        }
      }
    }

    // and (perm x, y, c1), c2 -> perm x, y, permute_mask(c1, c2)
    if (LHS.hasOneUse() && LHS.getOpcode() == AMDGPUISD::PERM &&
        isa<ConstantSDNode>(LHS.getOperand(2))) {
      uint32_t Sel = getConstantPermuteMask(Mask);
      if (!Sel)
        return SDValue();

      // Select 0xc for all zero bytes
      Sel = (LHS.getConstantOperandVal(2) & Sel) | (~Sel & 0x0c0c0c0c);
      SDLoc DL(N);
      return DAG.getNode(AMDGPUISD::PERM, DL, MVT::i32, LHS.getOperand(0),
                         LHS.getOperand(1), DAG.getConstant(Sel, DL, MVT::i32));
    }
  }

  // (and (fcmp ord x, x), (fcmp une (fabs x), inf)) ->
  // fp_class x, ~(s_nan | q_nan | n_infinity | p_infinity)
  if (LHS.getOpcode() == ISD::SETCC && RHS.getOpcode() == ISD::SETCC) {
    ISD::CondCode LCC = cast<CondCodeSDNode>(LHS.getOperand(2))->get();
    ISD::CondCode RCC = cast<CondCodeSDNode>(RHS.getOperand(2))->get();

    SDValue X = LHS.getOperand(0);
    SDValue Y = RHS.getOperand(0);
    if (Y.getOpcode() != ISD::FABS || Y.getOperand(0) != X ||
        !isTypeLegal(X.getValueType()))
      return SDValue();

    if (LCC == ISD::SETO) {
      if (X != LHS.getOperand(1))
        return SDValue();

      if (RCC == ISD::SETUNE) {
        const ConstantFPSDNode *C1 = dyn_cast<ConstantFPSDNode>(RHS.getOperand(1));
        if (!C1 || !C1->isInfinity() || C1->isNegative())
          return SDValue();

        const uint32_t Mask = SIInstrFlags::N_NORMAL |
                              SIInstrFlags::N_SUBNORMAL |
                              SIInstrFlags::N_ZERO |
                              SIInstrFlags::P_ZERO |
                              SIInstrFlags::P_SUBNORMAL |
                              SIInstrFlags::P_NORMAL;

        static_assert(((~(SIInstrFlags::S_NAN |
                          SIInstrFlags::Q_NAN |
                          SIInstrFlags::N_INFINITY |
                          SIInstrFlags::P_INFINITY)) & 0x3ff) == Mask,
                      "mask not equal");

        SDLoc DL(N);
        return DAG.getNode(AMDGPUISD::FP_CLASS, DL, MVT::i1,
                           X, DAG.getConstant(Mask, DL, MVT::i32));
      }
    }
  }

  if (RHS.getOpcode() == ISD::SETCC && LHS.getOpcode() == AMDGPUISD::FP_CLASS)
    std::swap(LHS, RHS);

  if (LHS.getOpcode() == ISD::SETCC && RHS.getOpcode() == AMDGPUISD::FP_CLASS &&
      RHS.hasOneUse()) {
    ISD::CondCode LCC = cast<CondCodeSDNode>(LHS.getOperand(2))->get();
    // and (fcmp seto), (fp_class x, mask) -> fp_class x, mask & ~(p_nan | n_nan)
    // and (fcmp setuo), (fp_class x, mask) -> fp_class x, mask & (p_nan | n_nan)
    const ConstantSDNode *Mask = dyn_cast<ConstantSDNode>(RHS.getOperand(1));
    if ((LCC == ISD::SETO || LCC == ISD::SETUO) && Mask &&
        (RHS.getOperand(0) == LHS.getOperand(0) &&
         LHS.getOperand(0) == LHS.getOperand(1))) {
      const unsigned OrdMask = SIInstrFlags::S_NAN | SIInstrFlags::Q_NAN;
      unsigned NewMask = LCC == ISD::SETO ?
        Mask->getZExtValue() & ~OrdMask :
        Mask->getZExtValue() & OrdMask;

      SDLoc DL(N);
      return DAG.getNode(AMDGPUISD::FP_CLASS, DL, MVT::i1, RHS.getOperand(0),
                         DAG.getConstant(NewMask, DL, MVT::i32));
    }
  }

  if (VT == MVT::i32 &&
      (RHS.getOpcode() == ISD::SIGN_EXTEND || LHS.getOpcode() == ISD::SIGN_EXTEND)) {
    // and x, (sext cc from i1) => select cc, x, 0
    if (RHS.getOpcode() != ISD::SIGN_EXTEND)
      std::swap(LHS, RHS);
    if (isBoolSGPR(RHS.getOperand(0)))
      return DAG.getSelect(SDLoc(N), MVT::i32, RHS.getOperand(0),
                           LHS, DAG.getConstant(0, SDLoc(N), MVT::i32));
  }

  // and (op x, c1), (op y, c2) -> perm x, y, permute_mask(c1, c2)
  const SIInstrInfo *TII = getSubtarget()->getInstrInfo();
  if (VT == MVT::i32 && LHS.hasOneUse() && RHS.hasOneUse() &&
      N->isDivergent() && TII->pseudoToMCOpcode(AMDGPU::V_PERM_B32_e64) != -1) {
    uint32_t LHSMask = getPermuteMask(LHS);
    uint32_t RHSMask = getPermuteMask(RHS);
    if (LHSMask != ~0u && RHSMask != ~0u) {
      // Canonicalize the expression in an attempt to have fewer unique masks
      // and therefore fewer registers used to hold the masks.
      if (LHSMask > RHSMask) {
        std::swap(LHSMask, RHSMask);
        std::swap(LHS, RHS);
      }

      // Select 0xc for each lane used from source operand. Zero has 0xc mask
      // set, 0xff have 0xff in the mask, actual lanes are in the 0-3 range.
      uint32_t LHSUsedLanes = ~(LHSMask & 0x0c0c0c0c) & 0x0c0c0c0c;
      uint32_t RHSUsedLanes = ~(RHSMask & 0x0c0c0c0c) & 0x0c0c0c0c;

      // Check of we need to combine values from two sources within a byte.
      if (!(LHSUsedLanes & RHSUsedLanes) &&
          // If we select high and lower word keep it for SDWA.
          // TODO: teach SDWA to work with v_perm_b32 and remove the check.
          !(LHSUsedLanes == 0x0c0c0000 && RHSUsedLanes == 0x00000c0c)) {
        // Each byte in each mask is either selector mask 0-3, or has higher
        // bits set in either of masks, which can be 0xff for 0xff or 0x0c for
        // zero. If 0x0c is in either mask it shall always be 0x0c. Otherwise
        // mask which is not 0xff wins. By anding both masks we have a correct
        // result except that 0x0c shall be corrected to give 0x0c only.
        uint32_t Mask = LHSMask & RHSMask;
        for (unsigned I = 0; I < 32; I += 8) {
          uint32_t ByteSel = 0xff << I;
          if ((LHSMask & ByteSel) == 0x0c || (RHSMask & ByteSel) == 0x0c)
            Mask &= (0x0c << I) & 0xffffffff;
        }

        // Add 4 to each active LHS lane. It will not affect any existing 0xff
        // or 0x0c.
        uint32_t Sel = Mask | (LHSUsedLanes & 0x04040404);
        SDLoc DL(N);

        return DAG.getNode(AMDGPUISD::PERM, DL, MVT::i32,
                           LHS.getOperand(0), RHS.getOperand(0),
                           DAG.getConstant(Sel, DL, MVT::i32));
      }
    }
  }

  return SDValue();
}

// A key component of v_perm is a mapping between byte position of the src
// operands, and the byte position of the dest. To provide such, we need: 1. the
// node that provides x byte of the dest of the OR, and 2. the byte of the node
// used to provide that x byte. calculateByteProvider finds which node provides
// a certain byte of the dest of the OR, and calculateSrcByte takes that node,
// and finds an ultimate src and byte position For example: The supported
// LoadCombine pattern for vector loads is as follows
//                                t1
//                                or
//                      /                  \
//                      t2                 t3
//                     zext                shl
//                      |                   |     \
//                     t4                  t5     16
//                     or                 anyext
//                 /        \               |
//                t6        t7             t8
//               srl        shl             or
//            /    |      /     \         /     \
//           t9   t10    t11   t12      t13    t14
//         trunc*  8    trunc*  8      and     and
//           |            |          /    |     |    \
//          t15          t16        t17  t18   t19   t20
//                                trunc*  255   srl   -256
//                                   |         /   \
//                                  t15       t15  16
//
// *In this example, the truncs are from i32->i16
//
// calculateByteProvider would find t6, t7, t13, and t14 for bytes 0-3
// respectively. calculateSrcByte would find (given node) -> ultimate src &
// byteposition: t6 -> t15 & 1, t7 -> t16 & 0, t13 -> t15 & 0, t14 -> t15 & 3.
// After finding the mapping, we can combine the tree into vperm t15, t16,
// 0x05000407

// Find the source and byte position from a node.
// \p DestByte is the byte position of the dest of the or that the src
// ultimately provides. \p SrcIndex is the byte of the src that maps to this
// dest of the or byte. \p Depth tracks how many recursive iterations we have
// performed.
static const std::optional<ByteProvider<SDValue>>
calculateSrcByte(const SDValue Op, uint64_t DestByte, uint64_t SrcIndex = 0,
                 unsigned Depth = 0) {
  // We may need to recursively traverse a series of SRLs
  if (Depth >= 6)
    return std::nullopt;

  if (Op.getValueSizeInBits() < 8)
    return std::nullopt;

  if (Op.getValueType().isVector())
    return ByteProvider<SDValue>::getSrc(Op, DestByte, SrcIndex);

  switch (Op->getOpcode()) {
  case ISD::TRUNCATE: {
    return calculateSrcByte(Op->getOperand(0), DestByte, SrcIndex, Depth + 1);
  }

  case ISD::SIGN_EXTEND:
  case ISD::ZERO_EXTEND:
  case ISD::SIGN_EXTEND_INREG: {
    SDValue NarrowOp = Op->getOperand(0);
    auto NarrowVT = NarrowOp.getValueType();
    if (Op->getOpcode() == ISD::SIGN_EXTEND_INREG) {
      auto *VTSign = cast<VTSDNode>(Op->getOperand(1));
      NarrowVT = VTSign->getVT();
    }
    if (!NarrowVT.isByteSized())
      return std::nullopt;
    uint64_t NarrowByteWidth = NarrowVT.getStoreSize();

    if (SrcIndex >= NarrowByteWidth)
      return std::nullopt;
    return calculateSrcByte(Op->getOperand(0), DestByte, SrcIndex, Depth + 1);
  }

  case ISD::SRA:
  case ISD::SRL: {
    auto ShiftOp = dyn_cast<ConstantSDNode>(Op->getOperand(1));
    if (!ShiftOp)
      return std::nullopt;

    uint64_t BitShift = ShiftOp->getZExtValue();

    if (BitShift % 8 != 0)
      return std::nullopt;

    SrcIndex += BitShift / 8;

    return calculateSrcByte(Op->getOperand(0), DestByte, SrcIndex, Depth + 1);
  }

  default: {
    return ByteProvider<SDValue>::getSrc(Op, DestByte, SrcIndex);
  }
  }
  llvm_unreachable("fully handled switch");
}

// For a byte position in the result of an Or, traverse the tree and find the
// node (and the byte of the node) which ultimately provides this {Or,
// BytePosition}. \p Op is the operand we are currently examining. \p Index is
// the byte position of the Op that corresponds with the originally requested
// byte of the Or \p Depth tracks how many recursive iterations we have
// performed. \p StartingIndex is the originally requested byte of the Or
static const std::optional<ByteProvider<SDValue>>
calculateByteProvider(const SDValue &Op, unsigned Index, unsigned Depth,
                      unsigned StartingIndex = 0) {
  // Finding Src tree of RHS of or typically requires at least 1 additional
  // depth
  if (Depth > 6)
    return std::nullopt;

  unsigned BitWidth = Op.getScalarValueSizeInBits();
  if (BitWidth % 8 != 0)
    return std::nullopt;
  if (Index > BitWidth / 8 - 1)
    return std::nullopt;

  bool IsVec = Op.getValueType().isVector();
  switch (Op.getOpcode()) {
  case ISD::OR: {
    if (IsVec)
      return std::nullopt;

    auto RHS = calculateByteProvider(Op.getOperand(1), Index, Depth + 1,
                                     StartingIndex);
    if (!RHS)
      return std::nullopt;
    auto LHS = calculateByteProvider(Op.getOperand(0), Index, Depth + 1,
                                     StartingIndex);
    if (!LHS)
      return std::nullopt;
    // A well formed Or will have two ByteProviders for each byte, one of which
    // is constant zero
    if (!LHS->isConstantZero() && !RHS->isConstantZero())
      return std::nullopt;
    if (!LHS || LHS->isConstantZero())
      return RHS;
    if (!RHS || RHS->isConstantZero())
      return LHS;
    return std::nullopt;
  }

  case ISD::AND: {
    if (IsVec)
      return std::nullopt;

    auto BitMaskOp = dyn_cast<ConstantSDNode>(Op->getOperand(1));
    if (!BitMaskOp)
      return std::nullopt;

    uint32_t BitMask = BitMaskOp->getZExtValue();
    // Bits we expect for our StartingIndex
    uint32_t IndexMask = 0xFF << (Index * 8);

    if ((IndexMask & BitMask) != IndexMask) {
      // If the result of the and partially provides the byte, then it
      // is not well formatted
      if (IndexMask & BitMask)
        return std::nullopt;
      return ByteProvider<SDValue>::getConstantZero();
    }

    return calculateSrcByte(Op->getOperand(0), StartingIndex, Index);
  }

  case ISD::FSHR: {
    if (IsVec)
      return std::nullopt;

    // fshr(X,Y,Z): (X << (BW - (Z % BW))) | (Y >> (Z % BW))
    auto ShiftOp = dyn_cast<ConstantSDNode>(Op->getOperand(2));
    if (!ShiftOp || Op.getValueType().isVector())
      return std::nullopt;

    uint64_t BitsProvided = Op.getValueSizeInBits();
    if (BitsProvided % 8 != 0)
      return std::nullopt;

    uint64_t BitShift = ShiftOp->getAPIntValue().urem(BitsProvided);
    if (BitShift % 8)
      return std::nullopt;

    uint64_t ConcatSizeInBytes = BitsProvided / 4;
    uint64_t ByteShift = BitShift / 8;

    uint64_t NewIndex = (Index + ByteShift) % ConcatSizeInBytes;
    uint64_t BytesProvided = BitsProvided / 8;
    SDValue NextOp = Op.getOperand(NewIndex >= BytesProvided ? 0 : 1);
    NewIndex %= BytesProvided;
    return calculateByteProvider(NextOp, NewIndex, Depth + 1, StartingIndex);
  }

  case ISD::SRA:
  case ISD::SRL: {
    if (IsVec)
      return std::nullopt;

    auto ShiftOp = dyn_cast<ConstantSDNode>(Op->getOperand(1));
    if (!ShiftOp)
      return std::nullopt;

    uint64_t BitShift = ShiftOp->getZExtValue();
    if (BitShift % 8)
      return std::nullopt;

    auto BitsProvided = Op.getScalarValueSizeInBits();
    if (BitsProvided % 8 != 0)
      return std::nullopt;

    uint64_t BytesProvided = BitsProvided / 8;
    uint64_t ByteShift = BitShift / 8;
    // The dest of shift will have good [0 : (BytesProvided - ByteShift)] bytes.
    // If the byte we are trying to provide (as tracked by index) falls in this
    // range, then the SRL provides the byte. The byte of interest of the src of
    // the SRL is Index + ByteShift
    return BytesProvided - ByteShift > Index
               ? calculateSrcByte(Op->getOperand(0), StartingIndex,
                                  Index + ByteShift)
               : ByteProvider<SDValue>::getConstantZero();
  }

  case ISD::SHL: {
    if (IsVec)
      return std::nullopt;

    auto ShiftOp = dyn_cast<ConstantSDNode>(Op->getOperand(1));
    if (!ShiftOp)
      return std::nullopt;

    uint64_t BitShift = ShiftOp->getZExtValue();
    if (BitShift % 8 != 0)
      return std::nullopt;
    uint64_t ByteShift = BitShift / 8;

    // If we are shifting by an amount greater than (or equal to)
    // the index we are trying to provide, then it provides 0s. If not,
    // then this bytes are not definitively 0s, and the corresponding byte
    // of interest is Index - ByteShift of the src
    return Index < ByteShift
               ? ByteProvider<SDValue>::getConstantZero()
               : calculateByteProvider(Op.getOperand(0), Index - ByteShift,
                                       Depth + 1, StartingIndex);
  }
  case ISD::ANY_EXTEND:
  case ISD::SIGN_EXTEND:
  case ISD::ZERO_EXTEND:
  case ISD::SIGN_EXTEND_INREG:
  case ISD::AssertZext:
  case ISD::AssertSext: {
    if (IsVec)
      return std::nullopt;

    SDValue NarrowOp = Op->getOperand(0);
    unsigned NarrowBitWidth = NarrowOp.getValueSizeInBits();
    if (Op->getOpcode() == ISD::SIGN_EXTEND_INREG ||
        Op->getOpcode() == ISD::AssertZext ||
        Op->getOpcode() == ISD::AssertSext) {
      auto *VTSign = cast<VTSDNode>(Op->getOperand(1));
      NarrowBitWidth = VTSign->getVT().getSizeInBits();
    }
    if (NarrowBitWidth % 8 != 0)
      return std::nullopt;
    uint64_t NarrowByteWidth = NarrowBitWidth / 8;

    if (Index >= NarrowByteWidth)
      return Op.getOpcode() == ISD::ZERO_EXTEND
                 ? std::optional<ByteProvider<SDValue>>(
                       ByteProvider<SDValue>::getConstantZero())
                 : std::nullopt;
    return calculateByteProvider(NarrowOp, Index, Depth + 1, StartingIndex);
  }

  case ISD::TRUNCATE: {
    if (IsVec)
      return std::nullopt;

    uint64_t NarrowByteWidth = BitWidth / 8;

    if (NarrowByteWidth >= Index) {
      return calculateByteProvider(Op.getOperand(0), Index, Depth + 1,
                                   StartingIndex);
    }

    return std::nullopt;
  }

  case ISD::CopyFromReg: {
    if (BitWidth / 8 > Index)
      return calculateSrcByte(Op, StartingIndex, Index);

    return std::nullopt;
  }

  case ISD::LOAD: {
    auto L = cast<LoadSDNode>(Op.getNode());

    unsigned NarrowBitWidth = L->getMemoryVT().getSizeInBits();
    if (NarrowBitWidth % 8 != 0)
      return std::nullopt;
    uint64_t NarrowByteWidth = NarrowBitWidth / 8;

    // If the width of the load does not reach byte we are trying to provide for
    // and it is not a ZEXTLOAD, then the load does not provide for the byte in
    // question
    if (Index >= NarrowByteWidth) {
      return L->getExtensionType() == ISD::ZEXTLOAD
                 ? std::optional<ByteProvider<SDValue>>(
                       ByteProvider<SDValue>::getConstantZero())
                 : std::nullopt;
    }

    if (NarrowByteWidth > Index) {
      return calculateSrcByte(Op, StartingIndex, Index);
    }

    return std::nullopt;
  }

  case ISD::BSWAP: {
    if (IsVec)
      return std::nullopt;

    return calculateByteProvider(Op->getOperand(0), BitWidth / 8 - Index - 1,
                                 Depth + 1, StartingIndex);
  }

  case ISD::EXTRACT_VECTOR_ELT: {
    auto IdxOp = dyn_cast<ConstantSDNode>(Op->getOperand(1));
    if (!IdxOp)
      return std::nullopt;
    auto VecIdx = IdxOp->getZExtValue();
    auto ScalarSize = Op.getScalarValueSizeInBits();
    if (ScalarSize < 32)
      Index = ScalarSize == 8 ? VecIdx : VecIdx * 2 + Index;
    return calculateSrcByte(ScalarSize >= 32 ? Op : Op.getOperand(0),
                            StartingIndex, Index);
  }

  case AMDGPUISD::PERM: {
    if (IsVec)
      return std::nullopt;

    auto PermMask = dyn_cast<ConstantSDNode>(Op->getOperand(2));
    if (!PermMask)
      return std::nullopt;

    auto IdxMask =
        (PermMask->getZExtValue() & (0xFF << (Index * 8))) >> (Index * 8);
    if (IdxMask > 0x07 && IdxMask != 0x0c)
      return std::nullopt;

    auto NextOp = Op.getOperand(IdxMask > 0x03 ? 0 : 1);
    auto NextIndex = IdxMask > 0x03 ? IdxMask % 4 : IdxMask;

    return IdxMask != 0x0c ? calculateSrcByte(NextOp, StartingIndex, NextIndex)
                           : ByteProvider<SDValue>(
                                 ByteProvider<SDValue>::getConstantZero());
  }

  default: {
    return std::nullopt;
  }
  }

  llvm_unreachable("fully handled switch");
}

// Returns true if the Operand is a scalar and is 16 bits
static bool isExtendedFrom16Bits(SDValue &Operand) {

  switch (Operand.getOpcode()) {
  case ISD::ANY_EXTEND:
  case ISD::SIGN_EXTEND:
  case ISD::ZERO_EXTEND: {
    auto OpVT = Operand.getOperand(0).getValueType();
    return !OpVT.isVector() && OpVT.getSizeInBits() == 16;
  }
  case ISD::LOAD: {
    LoadSDNode *L = cast<LoadSDNode>(Operand.getNode());
    auto ExtType = cast<LoadSDNode>(L)->getExtensionType();
    if (ExtType == ISD::ZEXTLOAD || ExtType == ISD::SEXTLOAD ||
        ExtType == ISD::EXTLOAD) {
      auto MemVT = L->getMemoryVT();
      return !MemVT.isVector() && MemVT.getSizeInBits() == 16;
    }
    return L->getMemoryVT().getSizeInBits() == 16;
  }
  default:
    return false;
  }
}

// Returns true if the mask matches consecutive bytes, and the first byte
// begins at a power of 2 byte offset from 0th byte
static bool addresses16Bits(int Mask) {
  int Low8 = Mask & 0xff;
  int Hi8 = (Mask & 0xff00) >> 8;

  assert(Low8 < 8 && Hi8 < 8);
  // Are the bytes contiguous in the order of increasing addresses.
  bool IsConsecutive = (Hi8 - Low8 == 1);
  // Is the first byte at location that is aligned for 16 bit instructions.
  // A counter example is taking 2 consecutive bytes starting at the 8th bit.
  // In this case, we still need code to extract the 16 bit operand, so it
  // is better to use i8 v_perm
  bool Is16Aligned = !(Low8 % 2);

  return IsConsecutive && Is16Aligned;
}

// Do not lower into v_perm if the operands are actually 16 bit
// and the selected bits (based on PermMask) correspond with two
// easily addressable 16 bit operands.
static bool hasNon16BitAccesses(uint64_t PermMask, SDValue &Op,
                                SDValue &OtherOp) {
  int Low16 = PermMask & 0xffff;
  int Hi16 = (PermMask & 0xffff0000) >> 16;

  auto TempOp = peekThroughBitcasts(Op);
  auto TempOtherOp = peekThroughBitcasts(OtherOp);

  auto OpIs16Bit =
      TempOtherOp.getValueSizeInBits() == 16 || isExtendedFrom16Bits(TempOp);
  if (!OpIs16Bit)
    return true;

  auto OtherOpIs16Bit = TempOtherOp.getValueSizeInBits() == 16 ||
                        isExtendedFrom16Bits(TempOtherOp);
  if (!OtherOpIs16Bit)
    return true;

  // Do we cleanly address both
  return !addresses16Bits(Low16) || !addresses16Bits(Hi16);
}

static SDValue getDWordFromOffset(SelectionDAG &DAG, SDLoc SL, SDValue Src,
                                  unsigned DWordOffset) {
  SDValue Ret;

  auto TypeSize = Src.getValueSizeInBits().getFixedValue();
  // ByteProvider must be at least 8 bits
  assert(Src.getValueSizeInBits().isKnownMultipleOf(8));

  if (TypeSize <= 32)
    return DAG.getBitcastedAnyExtOrTrunc(Src, SL, MVT::i32);

  if (Src.getValueType().isVector()) {
    auto ScalarTySize = Src.getScalarValueSizeInBits();
    auto ScalarTy = Src.getValueType().getScalarType();
    if (ScalarTySize == 32) {
      return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, Src,
                         DAG.getConstant(DWordOffset, SL, MVT::i32));
    }
    if (ScalarTySize > 32) {
      Ret = DAG.getNode(
          ISD::EXTRACT_VECTOR_ELT, SL, ScalarTy, Src,
          DAG.getConstant(DWordOffset / (ScalarTySize / 32), SL, MVT::i32));
      auto ShiftVal = 32 * (DWordOffset % (ScalarTySize / 32));
      if (ShiftVal)
        Ret = DAG.getNode(ISD::SRL, SL, Ret.getValueType(), Ret,
                          DAG.getConstant(ShiftVal, SL, MVT::i32));
      return DAG.getBitcastedAnyExtOrTrunc(Ret, SL, MVT::i32);
    }

    assert(ScalarTySize < 32);
    auto NumElements = TypeSize / ScalarTySize;
    auto Trunc32Elements = (ScalarTySize * NumElements) / 32;
    auto NormalizedTrunc = Trunc32Elements * 32 / ScalarTySize;
    auto NumElementsIn32 = 32 / ScalarTySize;
    auto NumAvailElements = DWordOffset < Trunc32Elements
                                ? NumElementsIn32
                                : NumElements - NormalizedTrunc;

    SmallVector<SDValue, 4> VecSrcs;
    DAG.ExtractVectorElements(Src, VecSrcs, DWordOffset * NumElementsIn32,
                              NumAvailElements);

    Ret = DAG.getBuildVector(
        MVT::getVectorVT(MVT::getIntegerVT(ScalarTySize), NumAvailElements), SL,
        VecSrcs);
    return Ret = DAG.getBitcastedAnyExtOrTrunc(Ret, SL, MVT::i32);
  }

  /// Scalar Type
  auto ShiftVal = 32 * DWordOffset;
  Ret = DAG.getNode(ISD::SRL, SL, Src.getValueType(), Src,
                    DAG.getConstant(ShiftVal, SL, MVT::i32));
  return DAG.getBitcastedAnyExtOrTrunc(Ret, SL, MVT::i32);
}

static SDValue matchPERM(SDNode *N, TargetLowering::DAGCombinerInfo &DCI) {
  SelectionDAG &DAG = DCI.DAG;
  [[maybe_unused]] EVT VT = N->getValueType(0);
  SmallVector<ByteProvider<SDValue>, 8> PermNodes;

  // VT is known to be MVT::i32, so we need to provide 4 bytes.
  assert(VT == MVT::i32);
  for (int i = 0; i < 4; i++) {
    // Find the ByteProvider that provides the ith byte of the result of OR
    std::optional<ByteProvider<SDValue>> P =
        calculateByteProvider(SDValue(N, 0), i, 0, /*StartingIndex = */ i);
    // TODO support constantZero
    if (!P || P->isConstantZero())
      return SDValue();

    PermNodes.push_back(*P);
  }
  if (PermNodes.size() != 4)
    return SDValue();

  std::pair<unsigned, unsigned> FirstSrc(0, PermNodes[0].SrcOffset / 4);
  std::optional<std::pair<unsigned, unsigned>> SecondSrc;
  uint64_t PermMask = 0x00000000;
  for (size_t i = 0; i < PermNodes.size(); i++) {
    auto PermOp = PermNodes[i];
    // Since the mask is applied to Src1:Src2, Src1 bytes must be offset
    // by sizeof(Src2) = 4
    int SrcByteAdjust = 4;

    // If the Src uses a byte from a different DWORD, then it corresponds
    // with a difference source
    if (!PermOp.hasSameSrc(PermNodes[FirstSrc.first]) ||
        ((PermOp.SrcOffset / 4) != FirstSrc.second)) {
      if (SecondSrc)
        if (!PermOp.hasSameSrc(PermNodes[SecondSrc->first]) ||
            ((PermOp.SrcOffset / 4) != SecondSrc->second))
          return SDValue();

      // Set the index of the second distinct Src node
      SecondSrc = {i, PermNodes[i].SrcOffset / 4};
      assert(!(PermNodes[SecondSrc->first].Src->getValueSizeInBits() % 8));
      SrcByteAdjust = 0;
    }
    assert((PermOp.SrcOffset % 4) + SrcByteAdjust < 8);
    assert(!DAG.getDataLayout().isBigEndian());
    PermMask |= ((PermOp.SrcOffset % 4) + SrcByteAdjust) << (i * 8);
  }
  SDLoc DL(N);
  SDValue Op = *PermNodes[FirstSrc.first].Src;
  Op = getDWordFromOffset(DAG, DL, Op, FirstSrc.second);
  assert(Op.getValueSizeInBits() == 32);

  // Check that we are not just extracting the bytes in order from an op
  if (!SecondSrc) {
    int Low16 = PermMask & 0xffff;
    int Hi16 = (PermMask & 0xffff0000) >> 16;

    bool WellFormedLow = (Low16 == 0x0504) || (Low16 == 0x0100);
    bool WellFormedHi = (Hi16 == 0x0706) || (Hi16 == 0x0302);

    // The perm op would really just produce Op. So combine into Op
    if (WellFormedLow && WellFormedHi)
      return DAG.getBitcast(MVT::getIntegerVT(32), Op);
  }

  SDValue OtherOp = SecondSrc ? *PermNodes[SecondSrc->first].Src : Op;

  if (SecondSrc) {
    OtherOp = getDWordFromOffset(DAG, DL, OtherOp, SecondSrc->second);
    assert(OtherOp.getValueSizeInBits() == 32);
  }

  if (hasNon16BitAccesses(PermMask, Op, OtherOp)) {

    assert(Op.getValueType().isByteSized() &&
           OtherOp.getValueType().isByteSized());

    // If the ultimate src is less than 32 bits, then we will only be
    // using bytes 0: Op.getValueSizeInBytes() - 1 in the or.
    // CalculateByteProvider would not have returned Op as source if we
    // used a byte that is outside its ValueType. Thus, we are free to
    // ANY_EXTEND as the extended bits are dont-cares.
    Op = DAG.getBitcastedAnyExtOrTrunc(Op, DL, MVT::i32);
    OtherOp = DAG.getBitcastedAnyExtOrTrunc(OtherOp, DL, MVT::i32);

    return DAG.getNode(AMDGPUISD::PERM, DL, MVT::i32, Op, OtherOp,
                       DAG.getConstant(PermMask, DL, MVT::i32));
  }
  return SDValue();
}

SDValue SITargetLowering::performOrCombine(SDNode *N,
                                           DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  EVT VT = N->getValueType(0);
  if (VT == MVT::i1) {
    // or (fp_class x, c1), (fp_class x, c2) -> fp_class x, (c1 | c2)
    if (LHS.getOpcode() == AMDGPUISD::FP_CLASS &&
        RHS.getOpcode() == AMDGPUISD::FP_CLASS) {
      SDValue Src = LHS.getOperand(0);
      if (Src != RHS.getOperand(0))
        return SDValue();

      const ConstantSDNode *CLHS = dyn_cast<ConstantSDNode>(LHS.getOperand(1));
      const ConstantSDNode *CRHS = dyn_cast<ConstantSDNode>(RHS.getOperand(1));
      if (!CLHS || !CRHS)
        return SDValue();

      // Only 10 bits are used.
      static const uint32_t MaxMask = 0x3ff;

      uint32_t NewMask = (CLHS->getZExtValue() | CRHS->getZExtValue()) & MaxMask;
      SDLoc DL(N);
      return DAG.getNode(AMDGPUISD::FP_CLASS, DL, MVT::i1,
                         Src, DAG.getConstant(NewMask, DL, MVT::i32));
    }

    return SDValue();
  }

  // or (perm x, y, c1), c2 -> perm x, y, permute_mask(c1, c2)
  if (isa<ConstantSDNode>(RHS) && LHS.hasOneUse() &&
      LHS.getOpcode() == AMDGPUISD::PERM &&
      isa<ConstantSDNode>(LHS.getOperand(2))) {
    uint32_t Sel = getConstantPermuteMask(N->getConstantOperandVal(1));
    if (!Sel)
      return SDValue();

    Sel |= LHS.getConstantOperandVal(2);
    SDLoc DL(N);
    return DAG.getNode(AMDGPUISD::PERM, DL, MVT::i32, LHS.getOperand(0),
                       LHS.getOperand(1), DAG.getConstant(Sel, DL, MVT::i32));
  }

  // or (op x, c1), (op y, c2) -> perm x, y, permute_mask(c1, c2)
  const SIInstrInfo *TII = getSubtarget()->getInstrInfo();
  if (VT == MVT::i32 && LHS.hasOneUse() && RHS.hasOneUse() &&
      N->isDivergent() && TII->pseudoToMCOpcode(AMDGPU::V_PERM_B32_e64) != -1) {

    // If all the uses of an or need to extract the individual elements, do not
    // attempt to lower into v_perm
    auto usesCombinedOperand = [](SDNode *OrUse) {
      // If we have any non-vectorized use, then it is a candidate for v_perm
      if (OrUse->getOpcode() != ISD::BITCAST ||
          !OrUse->getValueType(0).isVector())
        return true;

      // If we have any non-vectorized use, then it is a candidate for v_perm
      for (auto VUse : OrUse->uses()) {
        if (!VUse->getValueType(0).isVector())
          return true;

        // If the use of a vector is a store, then combining via a v_perm
        // is beneficial.
        // TODO -- whitelist more uses
        for (auto VectorwiseOp : {ISD::STORE, ISD::CopyToReg, ISD::CopyFromReg})
          if (VUse->getOpcode() == VectorwiseOp)
            return true;
      }
      return false;
    };

    if (!any_of(N->uses(), usesCombinedOperand))
      return SDValue();

    uint32_t LHSMask = getPermuteMask(LHS);
    uint32_t RHSMask = getPermuteMask(RHS);

    if (LHSMask != ~0u && RHSMask != ~0u) {
      // Canonicalize the expression in an attempt to have fewer unique masks
      // and therefore fewer registers used to hold the masks.
      if (LHSMask > RHSMask) {
        std::swap(LHSMask, RHSMask);
        std::swap(LHS, RHS);
      }

      // Select 0xc for each lane used from source operand. Zero has 0xc mask
      // set, 0xff have 0xff in the mask, actual lanes are in the 0-3 range.
      uint32_t LHSUsedLanes = ~(LHSMask & 0x0c0c0c0c) & 0x0c0c0c0c;
      uint32_t RHSUsedLanes = ~(RHSMask & 0x0c0c0c0c) & 0x0c0c0c0c;

      // Check of we need to combine values from two sources within a byte.
      if (!(LHSUsedLanes & RHSUsedLanes) &&
          // If we select high and lower word keep it for SDWA.
          // TODO: teach SDWA to work with v_perm_b32 and remove the check.
          !(LHSUsedLanes == 0x0c0c0000 && RHSUsedLanes == 0x00000c0c)) {
        // Kill zero bytes selected by other mask. Zero value is 0xc.
        LHSMask &= ~RHSUsedLanes;
        RHSMask &= ~LHSUsedLanes;
        // Add 4 to each active LHS lane
        LHSMask |= LHSUsedLanes & 0x04040404;
        // Combine masks
        uint32_t Sel = LHSMask | RHSMask;
        SDLoc DL(N);

        return DAG.getNode(AMDGPUISD::PERM, DL, MVT::i32,
                           LHS.getOperand(0), RHS.getOperand(0),
                           DAG.getConstant(Sel, DL, MVT::i32));
      }
    }
    if (LHSMask == ~0u || RHSMask == ~0u) {
      if (SDValue Perm = matchPERM(N, DCI))
        return Perm;
    }
  }

  if (VT != MVT::i64 || DCI.isBeforeLegalizeOps())
    return SDValue();

  // TODO: This could be a generic combine with a predicate for extracting the
  // high half of an integer being free.

  // (or i64:x, (zero_extend i32:y)) ->
  //   i64 (bitcast (v2i32 build_vector (or i32:y, lo_32(x)), hi_32(x)))
  if (LHS.getOpcode() == ISD::ZERO_EXTEND &&
      RHS.getOpcode() != ISD::ZERO_EXTEND)
    std::swap(LHS, RHS);

  if (RHS.getOpcode() == ISD::ZERO_EXTEND) {
    SDValue ExtSrc = RHS.getOperand(0);
    EVT SrcVT = ExtSrc.getValueType();
    if (SrcVT == MVT::i32) {
      SDLoc SL(N);
      SDValue LowLHS, HiBits;
      std::tie(LowLHS, HiBits) = split64BitValue(LHS, DAG);
      SDValue LowOr = DAG.getNode(ISD::OR, SL, MVT::i32, LowLHS, ExtSrc);

      DCI.AddToWorklist(LowOr.getNode());
      DCI.AddToWorklist(HiBits.getNode());

      SDValue Vec = DAG.getNode(ISD::BUILD_VECTOR, SL, MVT::v2i32,
                                LowOr, HiBits);
      return DAG.getNode(ISD::BITCAST, SL, MVT::i64, Vec);
    }
  }

  const ConstantSDNode *CRHS = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (CRHS) {
    if (SDValue Split
          = splitBinaryBitConstantOp(DCI, SDLoc(N), ISD::OR,
                                     N->getOperand(0), CRHS))
      return Split;
  }

  return SDValue();
}

SDValue SITargetLowering::performXorCombine(SDNode *N,
                                            DAGCombinerInfo &DCI) const {
  if (SDValue RV = reassociateScalarOps(N, DCI.DAG))
    return RV;

  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  const ConstantSDNode *CRHS = dyn_cast<ConstantSDNode>(RHS);
  SelectionDAG &DAG = DCI.DAG;

  EVT VT = N->getValueType(0);
  if (CRHS && VT == MVT::i64) {
    if (SDValue Split
          = splitBinaryBitConstantOp(DCI, SDLoc(N), ISD::XOR, LHS, CRHS))
      return Split;
  }

  // Make sure to apply the 64-bit constant splitting fold before trying to fold
  // fneg-like xors into 64-bit select.
  if (LHS.getOpcode() == ISD::SELECT && VT == MVT::i32) {
    // This looks like an fneg, try to fold as a source modifier.
    if (CRHS && CRHS->getAPIntValue().isSignMask() &&
        shouldFoldFNegIntoSrc(N, LHS)) {
      // xor (select c, a, b), 0x80000000 ->
      //   bitcast (select c, (fneg (bitcast a)), (fneg (bitcast b)))
      SDLoc DL(N);
      SDValue CastLHS =
          DAG.getNode(ISD::BITCAST, DL, MVT::f32, LHS->getOperand(1));
      SDValue CastRHS =
          DAG.getNode(ISD::BITCAST, DL, MVT::f32, LHS->getOperand(2));
      SDValue FNegLHS = DAG.getNode(ISD::FNEG, DL, MVT::f32, CastLHS);
      SDValue FNegRHS = DAG.getNode(ISD::FNEG, DL, MVT::f32, CastRHS);
      SDValue NewSelect = DAG.getNode(ISD::SELECT, DL, MVT::f32,
                                      LHS->getOperand(0), FNegLHS, FNegRHS);
      return DAG.getNode(ISD::BITCAST, DL, VT, NewSelect);
    }
  }

  return SDValue();
}

SDValue SITargetLowering::performZeroExtendCombine(SDNode *N,
                                                   DAGCombinerInfo &DCI) const {
  if (!Subtarget->has16BitInsts() ||
      DCI.getDAGCombineLevel() < AfterLegalizeDAG)
    return SDValue();

  EVT VT = N->getValueType(0);
  if (VT != MVT::i32)
    return SDValue();

  SDValue Src = N->getOperand(0);
  if (Src.getValueType() != MVT::i16)
    return SDValue();

  return SDValue();
}

SDValue
SITargetLowering::performSignExtendInRegCombine(SDNode *N,
                                                DAGCombinerInfo &DCI) const {
  SDValue Src = N->getOperand(0);
  auto *VTSign = cast<VTSDNode>(N->getOperand(1));

  // Combine s_buffer_load_u8 or s_buffer_load_u16 with sext and replace them
  // with s_buffer_load_i8 and s_buffer_load_i16 respectively.
  if (((Src.getOpcode() == AMDGPUISD::SBUFFER_LOAD_UBYTE &&
        VTSign->getVT() == MVT::i8) ||
       (Src.getOpcode() == AMDGPUISD::SBUFFER_LOAD_USHORT &&
        VTSign->getVT() == MVT::i16))) {
    assert(Subtarget->hasScalarSubwordLoads() &&
           "s_buffer_load_{u8, i8} are supported "
           "in GFX12 (or newer) architectures.");
    EVT VT = Src.getValueType();
    unsigned Opc = (Src.getOpcode() == AMDGPUISD::SBUFFER_LOAD_UBYTE)
                       ? AMDGPUISD::SBUFFER_LOAD_BYTE
                       : AMDGPUISD::SBUFFER_LOAD_SHORT;
    SDLoc DL(N);
    SDVTList ResList = DCI.DAG.getVTList(MVT::i32);
    SDValue Ops[] = {
        Src.getOperand(0), // source register
        Src.getOperand(1), // offset
        Src.getOperand(2)  // cachePolicy
    };
    auto *M = cast<MemSDNode>(Src);
    SDValue BufferLoad = DCI.DAG.getMemIntrinsicNode(
        Opc, DL, ResList, Ops, M->getMemoryVT(), M->getMemOperand());
    SDValue LoadVal = DCI.DAG.getNode(ISD::TRUNCATE, DL, VT, BufferLoad);
    return LoadVal;
  }
  if (((Src.getOpcode() == AMDGPUISD::BUFFER_LOAD_UBYTE &&
        VTSign->getVT() == MVT::i8) ||
       (Src.getOpcode() == AMDGPUISD::BUFFER_LOAD_USHORT &&
        VTSign->getVT() == MVT::i16)) &&
      Src.hasOneUse()) {
    auto *M = cast<MemSDNode>(Src);
    SDValue Ops[] = {
      Src.getOperand(0), // Chain
      Src.getOperand(1), // rsrc
      Src.getOperand(2), // vindex
      Src.getOperand(3), // voffset
      Src.getOperand(4), // soffset
      Src.getOperand(5), // offset
      Src.getOperand(6),
      Src.getOperand(7)
    };
    // replace with BUFFER_LOAD_BYTE/SHORT
    SDVTList ResList = DCI.DAG.getVTList(MVT::i32,
                                         Src.getOperand(0).getValueType());
    unsigned Opc = (Src.getOpcode() == AMDGPUISD::BUFFER_LOAD_UBYTE) ?
                   AMDGPUISD::BUFFER_LOAD_BYTE : AMDGPUISD::BUFFER_LOAD_SHORT;
    SDValue BufferLoadSignExt = DCI.DAG.getMemIntrinsicNode(Opc, SDLoc(N),
                                                          ResList,
                                                          Ops, M->getMemoryVT(),
                                                          M->getMemOperand());
    return DCI.DAG.getMergeValues({BufferLoadSignExt,
                                  BufferLoadSignExt.getValue(1)}, SDLoc(N));
  }
  return SDValue();
}

SDValue SITargetLowering::performClassCombine(SDNode *N,
                                              DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDValue Mask = N->getOperand(1);

  // fp_class x, 0 -> false
  if (isNullConstant(Mask))
    return DAG.getConstant(0, SDLoc(N), MVT::i1);

  if (N->getOperand(0).isUndef())
    return DAG.getUNDEF(MVT::i1);

  return SDValue();
}

SDValue SITargetLowering::performRcpCombine(SDNode *N,
                                            DAGCombinerInfo &DCI) const {
  EVT VT = N->getValueType(0);
  SDValue N0 = N->getOperand(0);

  if (N0.isUndef()) {
    return DCI.DAG.getConstantFP(
        APFloat::getQNaN(SelectionDAG::EVTToAPFloatSemantics(VT)), SDLoc(N),
        VT);
  }

  if (VT == MVT::f32 && (N0.getOpcode() == ISD::UINT_TO_FP ||
                         N0.getOpcode() == ISD::SINT_TO_FP)) {
    return DCI.DAG.getNode(AMDGPUISD::RCP_IFLAG, SDLoc(N), VT, N0,
                           N->getFlags());
  }

  // TODO: Could handle f32 + amdgcn.sqrt but probably never reaches here.
  if ((VT == MVT::f16 && N0.getOpcode() == ISD::FSQRT) &&
      N->getFlags().hasAllowContract() && N0->getFlags().hasAllowContract()) {
    return DCI.DAG.getNode(AMDGPUISD::RSQ, SDLoc(N), VT,
                           N0.getOperand(0), N->getFlags());
  }

  return AMDGPUTargetLowering::performRcpCombine(N, DCI);
}

bool SITargetLowering::isCanonicalized(SelectionDAG &DAG, SDValue Op,
                                       unsigned MaxDepth) const {
  unsigned Opcode = Op.getOpcode();
  if (Opcode == ISD::FCANONICALIZE)
    return true;

  if (auto *CFP = dyn_cast<ConstantFPSDNode>(Op)) {
    const auto &F = CFP->getValueAPF();
    if (F.isNaN() && F.isSignaling())
      return false;
    if (!F.isDenormal())
      return true;

    DenormalMode Mode =
        DAG.getMachineFunction().getDenormalMode(F.getSemantics());
    return Mode == DenormalMode::getIEEE();
  }

  // If source is a result of another standard FP operation it is already in
  // canonical form.
  if (MaxDepth == 0)
    return false;

  switch (Opcode) {
  // These will flush denorms if required.
  case ISD::FADD:
  case ISD::FSUB:
  case ISD::FMUL:
  case ISD::FCEIL:
  case ISD::FFLOOR:
  case ISD::FMA:
  case ISD::FMAD:
  case ISD::FSQRT:
  case ISD::FDIV:
  case ISD::FREM:
  case ISD::FP_ROUND:
  case ISD::FP_EXTEND:
  case ISD::FP16_TO_FP:
  case ISD::FP_TO_FP16:
  case ISD::BF16_TO_FP:
  case ISD::FP_TO_BF16:
  case ISD::FLDEXP:
  case AMDGPUISD::FMUL_LEGACY:
  case AMDGPUISD::FMAD_FTZ:
  case AMDGPUISD::RCP:
  case AMDGPUISD::RSQ:
  case AMDGPUISD::RSQ_CLAMP:
  case AMDGPUISD::RCP_LEGACY:
  case AMDGPUISD::RCP_IFLAG:
  case AMDGPUISD::LOG:
  case AMDGPUISD::EXP:
  case AMDGPUISD::DIV_SCALE:
  case AMDGPUISD::DIV_FMAS:
  case AMDGPUISD::DIV_FIXUP:
  case AMDGPUISD::FRACT:
  case AMDGPUISD::CVT_PKRTZ_F16_F32:
  case AMDGPUISD::CVT_F32_UBYTE0:
  case AMDGPUISD::CVT_F32_UBYTE1:
  case AMDGPUISD::CVT_F32_UBYTE2:
  case AMDGPUISD::CVT_F32_UBYTE3:
  case AMDGPUISD::FP_TO_FP16:
  case AMDGPUISD::SIN_HW:
  case AMDGPUISD::COS_HW:
    return true;

  // It can/will be lowered or combined as a bit operation.
  // Need to check their input recursively to handle.
  case ISD::FNEG:
  case ISD::FABS:
  case ISD::FCOPYSIGN:
    return isCanonicalized(DAG, Op.getOperand(0), MaxDepth - 1);

  case ISD::AND:
    if (Op.getValueType() == MVT::i32) {
      // Be careful as we only know it is a bitcast floating point type. It
      // could be f32, v2f16, we have no way of knowing. Luckily the constant
      // value that we optimize for, which comes up in fp32 to bf16 conversions,
      // is valid to optimize for all types.
      if (auto *RHS = dyn_cast<ConstantSDNode>(Op.getOperand(1))) {
        if (RHS->getZExtValue() == 0xffff0000) {
          return isCanonicalized(DAG, Op.getOperand(0), MaxDepth - 1);
        }
      }
    }
    break;

  case ISD::FSIN:
  case ISD::FCOS:
  case ISD::FSINCOS:
    return Op.getValueType().getScalarType() != MVT::f16;

  case ISD::FMINNUM:
  case ISD::FMAXNUM:
  case ISD::FMINNUM_IEEE:
  case ISD::FMAXNUM_IEEE:
  case ISD::FMINIMUM:
  case ISD::FMAXIMUM:
  case AMDGPUISD::CLAMP:
  case AMDGPUISD::FMED3:
  case AMDGPUISD::FMAX3:
  case AMDGPUISD::FMIN3:
  case AMDGPUISD::FMAXIMUM3:
  case AMDGPUISD::FMINIMUM3: {
    // FIXME: Shouldn't treat the generic operations different based these.
    // However, we aren't really required to flush the result from
    // minnum/maxnum..

    // snans will be quieted, so we only need to worry about denormals.
    if (Subtarget->supportsMinMaxDenormModes() ||
        // FIXME: denormalsEnabledForType is broken for dynamic
        denormalsEnabledForType(DAG, Op.getValueType()))
      return true;

    // Flushing may be required.
    // In pre-GFX9 targets V_MIN_F32 and others do not flush denorms. For such
    // targets need to check their input recursively.

    // FIXME: Does this apply with clamp? It's implemented with max.
    for (unsigned I = 0, E = Op.getNumOperands(); I != E; ++I) {
      if (!isCanonicalized(DAG, Op.getOperand(I), MaxDepth - 1))
        return false;
    }

    return true;
  }
  case ISD::SELECT: {
    return isCanonicalized(DAG, Op.getOperand(1), MaxDepth - 1) &&
           isCanonicalized(DAG, Op.getOperand(2), MaxDepth - 1);
  }
  case ISD::BUILD_VECTOR: {
    for (unsigned i = 0, e = Op.getNumOperands(); i != e; ++i) {
      SDValue SrcOp = Op.getOperand(i);
      if (!isCanonicalized(DAG, SrcOp, MaxDepth - 1))
        return false;
    }

    return true;
  }
  case ISD::EXTRACT_VECTOR_ELT:
  case ISD::EXTRACT_SUBVECTOR: {
    return isCanonicalized(DAG, Op.getOperand(0), MaxDepth - 1);
  }
  case ISD::INSERT_VECTOR_ELT: {
    return isCanonicalized(DAG, Op.getOperand(0), MaxDepth - 1) &&
           isCanonicalized(DAG, Op.getOperand(1), MaxDepth - 1);
  }
  case ISD::UNDEF:
    // Could be anything.
    return false;

  case ISD::BITCAST:
    // TODO: This is incorrect as it loses track of the operand's type. We may
    // end up effectively bitcasting from f32 to v2f16 or vice versa, and the
    // same bits that are canonicalized in one type need not be in the other.
    return isCanonicalized(DAG, Op.getOperand(0), MaxDepth - 1);
  case ISD::TRUNCATE: {
    // Hack round the mess we make when legalizing extract_vector_elt
    if (Op.getValueType() == MVT::i16) {
      SDValue TruncSrc = Op.getOperand(0);
      if (TruncSrc.getValueType() == MVT::i32 &&
          TruncSrc.getOpcode() == ISD::BITCAST &&
          TruncSrc.getOperand(0).getValueType() == MVT::v2f16) {
        return isCanonicalized(DAG, TruncSrc.getOperand(0), MaxDepth - 1);
      }
    }
    return false;
  }
  case ISD::INTRINSIC_WO_CHAIN: {
    unsigned IntrinsicID = Op.getConstantOperandVal(0);
    // TODO: Handle more intrinsics
    switch (IntrinsicID) {
    case Intrinsic::amdgcn_cvt_pkrtz:
    case Intrinsic::amdgcn_cubeid:
    case Intrinsic::amdgcn_frexp_mant:
    case Intrinsic::amdgcn_fdot2:
    case Intrinsic::amdgcn_rcp:
    case Intrinsic::amdgcn_rsq:
    case Intrinsic::amdgcn_rsq_clamp:
    case Intrinsic::amdgcn_rcp_legacy:
    case Intrinsic::amdgcn_rsq_legacy:
    case Intrinsic::amdgcn_trig_preop:
    case Intrinsic::amdgcn_log:
    case Intrinsic::amdgcn_exp2:
    case Intrinsic::amdgcn_sqrt:
      return true;
    default:
      break;
    }

    break;
  }
  default:
    break;
  }

  // FIXME: denormalsEnabledForType is broken for dynamic
  return denormalsEnabledForType(DAG, Op.getValueType()) &&
         DAG.isKnownNeverSNaN(Op);
}

bool SITargetLowering::isCanonicalized(Register Reg, const MachineFunction &MF,
                                       unsigned MaxDepth) const {
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  MachineInstr *MI = MRI.getVRegDef(Reg);
  unsigned Opcode = MI->getOpcode();

  if (Opcode == AMDGPU::G_FCANONICALIZE)
    return true;

  std::optional<FPValueAndVReg> FCR;
  // Constant splat (can be padded with undef) or scalar constant.
  if (mi_match(Reg, MRI, MIPatternMatch::m_GFCstOrSplat(FCR))) {
    if (FCR->Value.isSignaling())
      return false;
    if (!FCR->Value.isDenormal())
      return true;

    DenormalMode Mode = MF.getDenormalMode(FCR->Value.getSemantics());
    return Mode == DenormalMode::getIEEE();
  }

  if (MaxDepth == 0)
    return false;

  switch (Opcode) {
  case AMDGPU::G_FADD:
  case AMDGPU::G_FSUB:
  case AMDGPU::G_FMUL:
  case AMDGPU::G_FCEIL:
  case AMDGPU::G_FFLOOR:
  case AMDGPU::G_FRINT:
  case AMDGPU::G_FNEARBYINT:
  case AMDGPU::G_INTRINSIC_FPTRUNC_ROUND:
  case AMDGPU::G_INTRINSIC_TRUNC:
  case AMDGPU::G_INTRINSIC_ROUNDEVEN:
  case AMDGPU::G_FMA:
  case AMDGPU::G_FMAD:
  case AMDGPU::G_FSQRT:
  case AMDGPU::G_FDIV:
  case AMDGPU::G_FREM:
  case AMDGPU::G_FPOW:
  case AMDGPU::G_FPEXT:
  case AMDGPU::G_FLOG:
  case AMDGPU::G_FLOG2:
  case AMDGPU::G_FLOG10:
  case AMDGPU::G_FPTRUNC:
  case AMDGPU::G_AMDGPU_RCP_IFLAG:
  case AMDGPU::G_AMDGPU_CVT_F32_UBYTE0:
  case AMDGPU::G_AMDGPU_CVT_F32_UBYTE1:
  case AMDGPU::G_AMDGPU_CVT_F32_UBYTE2:
  case AMDGPU::G_AMDGPU_CVT_F32_UBYTE3:
    return true;
  case AMDGPU::G_FNEG:
  case AMDGPU::G_FABS:
  case AMDGPU::G_FCOPYSIGN:
    return isCanonicalized(MI->getOperand(1).getReg(), MF, MaxDepth - 1);
  case AMDGPU::G_FMINNUM:
  case AMDGPU::G_FMAXNUM:
  case AMDGPU::G_FMINNUM_IEEE:
  case AMDGPU::G_FMAXNUM_IEEE:
  case AMDGPU::G_FMINIMUM:
  case AMDGPU::G_FMAXIMUM: {
    if (Subtarget->supportsMinMaxDenormModes() ||
        // FIXME: denormalsEnabledForType is broken for dynamic
        denormalsEnabledForType(MRI.getType(Reg), MF))
      return true;

    [[fallthrough]];
  }
  case AMDGPU::G_BUILD_VECTOR:
    for (const MachineOperand &MO : llvm::drop_begin(MI->operands()))
      if (!isCanonicalized(MO.getReg(), MF, MaxDepth - 1))
        return false;
    return true;
  case AMDGPU::G_INTRINSIC:
  case AMDGPU::G_INTRINSIC_CONVERGENT:
    switch (cast<GIntrinsic>(MI)->getIntrinsicID()) {
    case Intrinsic::amdgcn_fmul_legacy:
    case Intrinsic::amdgcn_fmad_ftz:
    case Intrinsic::amdgcn_sqrt:
    case Intrinsic::amdgcn_fmed3:
    case Intrinsic::amdgcn_sin:
    case Intrinsic::amdgcn_cos:
    case Intrinsic::amdgcn_log:
    case Intrinsic::amdgcn_exp2:
    case Intrinsic::amdgcn_log_clamp:
    case Intrinsic::amdgcn_rcp:
    case Intrinsic::amdgcn_rcp_legacy:
    case Intrinsic::amdgcn_rsq:
    case Intrinsic::amdgcn_rsq_clamp:
    case Intrinsic::amdgcn_rsq_legacy:
    case Intrinsic::amdgcn_div_scale:
    case Intrinsic::amdgcn_div_fmas:
    case Intrinsic::amdgcn_div_fixup:
    case Intrinsic::amdgcn_fract:
    case Intrinsic::amdgcn_cvt_pkrtz:
    case Intrinsic::amdgcn_cubeid:
    case Intrinsic::amdgcn_cubema:
    case Intrinsic::amdgcn_cubesc:
    case Intrinsic::amdgcn_cubetc:
    case Intrinsic::amdgcn_frexp_mant:
    case Intrinsic::amdgcn_fdot2:
    case Intrinsic::amdgcn_trig_preop:
      return true;
    default:
      break;
    }

    [[fallthrough]];
  default:
    return false;
  }

  llvm_unreachable("invalid operation");
}

// Constant fold canonicalize.
SDValue SITargetLowering::getCanonicalConstantFP(
  SelectionDAG &DAG, const SDLoc &SL, EVT VT, const APFloat &C) const {
  // Flush denormals to 0 if not enabled.
  if (C.isDenormal()) {
    DenormalMode Mode =
        DAG.getMachineFunction().getDenormalMode(C.getSemantics());
    if (Mode == DenormalMode::getPreserveSign()) {
      return DAG.getConstantFP(
          APFloat::getZero(C.getSemantics(), C.isNegative()), SL, VT);
    }

    if (Mode != DenormalMode::getIEEE())
      return SDValue();
  }

  if (C.isNaN()) {
    APFloat CanonicalQNaN = APFloat::getQNaN(C.getSemantics());
    if (C.isSignaling()) {
      // Quiet a signaling NaN.
      // FIXME: Is this supposed to preserve payload bits?
      return DAG.getConstantFP(CanonicalQNaN, SL, VT);
    }

    // Make sure it is the canonical NaN bitpattern.
    //
    // TODO: Can we use -1 as the canonical NaN value since it's an inline
    // immediate?
    if (C.bitcastToAPInt() != CanonicalQNaN.bitcastToAPInt())
      return DAG.getConstantFP(CanonicalQNaN, SL, VT);
  }

  // Already canonical.
  return DAG.getConstantFP(C, SL, VT);
}

static bool vectorEltWillFoldAway(SDValue Op) {
  return Op.isUndef() || isa<ConstantFPSDNode>(Op);
}

SDValue SITargetLowering::performFCanonicalizeCombine(
  SDNode *N,
  DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  // fcanonicalize undef -> qnan
  if (N0.isUndef()) {
    APFloat QNaN = APFloat::getQNaN(SelectionDAG::EVTToAPFloatSemantics(VT));
    return DAG.getConstantFP(QNaN, SDLoc(N), VT);
  }

  if (ConstantFPSDNode *CFP = isConstOrConstSplatFP(N0)) {
    EVT VT = N->getValueType(0);
    return getCanonicalConstantFP(DAG, SDLoc(N), VT, CFP->getValueAPF());
  }

  // fcanonicalize (build_vector x, k) -> build_vector (fcanonicalize x),
  //                                                   (fcanonicalize k)
  //
  // fcanonicalize (build_vector x, undef) -> build_vector (fcanonicalize x), 0

  // TODO: This could be better with wider vectors that will be split to v2f16,
  // and to consider uses since there aren't that many packed operations.
  if (N0.getOpcode() == ISD::BUILD_VECTOR && VT == MVT::v2f16 &&
      isTypeLegal(MVT::v2f16)) {
    SDLoc SL(N);
    SDValue NewElts[2];
    SDValue Lo = N0.getOperand(0);
    SDValue Hi = N0.getOperand(1);
    EVT EltVT = Lo.getValueType();

    if (vectorEltWillFoldAway(Lo) || vectorEltWillFoldAway(Hi)) {
      for (unsigned I = 0; I != 2; ++I) {
        SDValue Op = N0.getOperand(I);
        if (ConstantFPSDNode *CFP = dyn_cast<ConstantFPSDNode>(Op)) {
          NewElts[I] = getCanonicalConstantFP(DAG, SL, EltVT,
                                              CFP->getValueAPF());
        } else if (Op.isUndef()) {
          // Handled below based on what the other operand is.
          NewElts[I] = Op;
        } else {
          NewElts[I] = DAG.getNode(ISD::FCANONICALIZE, SL, EltVT, Op);
        }
      }

      // If one half is undef, and one is constant, prefer a splat vector rather
      // than the normal qNaN. If it's a register, prefer 0.0 since that's
      // cheaper to use and may be free with a packed operation.
      if (NewElts[0].isUndef()) {
        if (isa<ConstantFPSDNode>(NewElts[1]))
          NewElts[0] = isa<ConstantFPSDNode>(NewElts[1]) ?
            NewElts[1]: DAG.getConstantFP(0.0f, SL, EltVT);
      }

      if (NewElts[1].isUndef()) {
        NewElts[1] = isa<ConstantFPSDNode>(NewElts[0]) ?
          NewElts[0] : DAG.getConstantFP(0.0f, SL, EltVT);
      }

      return DAG.getBuildVector(VT, SL, NewElts);
    }
  }

  return SDValue();
}

static unsigned minMaxOpcToMin3Max3Opc(unsigned Opc) {
  switch (Opc) {
  case ISD::FMAXNUM:
  case ISD::FMAXNUM_IEEE:
    return AMDGPUISD::FMAX3;
  case ISD::FMAXIMUM:
    return AMDGPUISD::FMAXIMUM3;
  case ISD::SMAX:
    return AMDGPUISD::SMAX3;
  case ISD::UMAX:
    return AMDGPUISD::UMAX3;
  case ISD::FMINNUM:
  case ISD::FMINNUM_IEEE:
    return AMDGPUISD::FMIN3;
  case ISD::FMINIMUM:
    return AMDGPUISD::FMINIMUM3;
  case ISD::SMIN:
    return AMDGPUISD::SMIN3;
  case ISD::UMIN:
    return AMDGPUISD::UMIN3;
  default:
    llvm_unreachable("Not a min/max opcode");
  }
}

SDValue SITargetLowering::performIntMed3ImmCombine(SelectionDAG &DAG,
                                                   const SDLoc &SL, SDValue Src,
                                                   SDValue MinVal,
                                                   SDValue MaxVal,
                                                   bool Signed) const {

  // med3 comes from
  //    min(max(x, K0), K1), K0 < K1
  //    max(min(x, K0), K1), K1 < K0
  //
  // "MinVal" and "MaxVal" respectively refer to the rhs of the
  // min/max op.
  ConstantSDNode *MinK = dyn_cast<ConstantSDNode>(MinVal);
  ConstantSDNode *MaxK = dyn_cast<ConstantSDNode>(MaxVal);

  if (!MinK || !MaxK)
    return SDValue();

  if (Signed) {
    if (MaxK->getAPIntValue().sge(MinK->getAPIntValue()))
      return SDValue();
  } else {
    if (MaxK->getAPIntValue().uge(MinK->getAPIntValue()))
      return SDValue();
  }

  EVT VT = MinK->getValueType(0);
  unsigned Med3Opc = Signed ? AMDGPUISD::SMED3 : AMDGPUISD::UMED3;
  if (VT == MVT::i32 || (VT == MVT::i16 && Subtarget->hasMed3_16()))
    return DAG.getNode(Med3Opc, SL, VT, Src, MaxVal, MinVal);

  // Note: we could also extend to i32 and use i32 med3 if i16 med3 is
  // not available, but this is unlikely to be profitable as constants
  // will often need to be materialized & extended, especially on
  // pre-GFX10 where VOP3 instructions couldn't take literal operands.
  return SDValue();
}

static ConstantFPSDNode *getSplatConstantFP(SDValue Op) {
  if (ConstantFPSDNode *C = dyn_cast<ConstantFPSDNode>(Op))
    return C;

  if (BuildVectorSDNode *BV = dyn_cast<BuildVectorSDNode>(Op)) {
    if (ConstantFPSDNode *C = BV->getConstantFPSplatNode())
      return C;
  }

  return nullptr;
}

SDValue SITargetLowering::performFPMed3ImmCombine(SelectionDAG &DAG,
                                                  const SDLoc &SL,
                                                  SDValue Op0,
                                                  SDValue Op1) const {
  ConstantFPSDNode *K1 = getSplatConstantFP(Op1);
  if (!K1)
    return SDValue();

  ConstantFPSDNode *K0 = getSplatConstantFP(Op0.getOperand(1));
  if (!K0)
    return SDValue();

  // Ordered >= (although NaN inputs should have folded away by now).
  if (K0->getValueAPF() > K1->getValueAPF())
    return SDValue();

  const MachineFunction &MF = DAG.getMachineFunction();
  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();

  // TODO: Check IEEE bit enabled?
  EVT VT = Op0.getValueType();
  if (Info->getMode().DX10Clamp) {
    // If dx10_clamp is enabled, NaNs clamp to 0.0. This is the same as the
    // hardware fmed3 behavior converting to a min.
    // FIXME: Should this be allowing -0.0?
    if (K1->isExactlyValue(1.0) && K0->isExactlyValue(0.0))
      return DAG.getNode(AMDGPUISD::CLAMP, SL, VT, Op0.getOperand(0));
  }

  // med3 for f16 is only available on gfx9+, and not available for v2f16.
  if (VT == MVT::f32 || (VT == MVT::f16 && Subtarget->hasMed3_16())) {
    // This isn't safe with signaling NaNs because in IEEE mode, min/max on a
    // signaling NaN gives a quiet NaN. The quiet NaN input to the min would
    // then give the other result, which is different from med3 with a NaN
    // input.
    SDValue Var = Op0.getOperand(0);
    if (!DAG.isKnownNeverSNaN(Var))
      return SDValue();

    const SIInstrInfo *TII = getSubtarget()->getInstrInfo();

    if ((!K0->hasOneUse() || TII->isInlineConstant(K0->getValueAPF())) &&
        (!K1->hasOneUse() || TII->isInlineConstant(K1->getValueAPF()))) {
      return DAG.getNode(AMDGPUISD::FMED3, SL, K0->getValueType(0),
                         Var, SDValue(K0, 0), SDValue(K1, 0));
    }
  }

  return SDValue();
}

/// \return true if the subtarget supports minimum3 and maximum3 with the given
/// base min/max opcode \p Opc for type \p VT.
static bool supportsMin3Max3(const GCNSubtarget &Subtarget, unsigned Opc,
                             EVT VT) {
  switch (Opc) {
  case ISD::FMINNUM:
  case ISD::FMAXNUM:
  case ISD::FMINNUM_IEEE:
  case ISD::FMAXNUM_IEEE:
  case AMDGPUISD::FMIN_LEGACY:
  case AMDGPUISD::FMAX_LEGACY:
    return (VT == MVT::f32) || (VT == MVT::f16 && Subtarget.hasMin3Max3_16());
  case ISD::FMINIMUM:
  case ISD::FMAXIMUM:
    return (VT == MVT::f32 || VT == MVT::f16) && Subtarget.hasIEEEMinMax3();
  case ISD::SMAX:
  case ISD::SMIN:
  case ISD::UMAX:
  case ISD::UMIN:
    return (VT == MVT::i32) || (VT == MVT::i16 && Subtarget.hasMin3Max3_16());
  default:
    return false;
  }

  llvm_unreachable("not a min/max opcode");
}

SDValue SITargetLowering::performMinMaxCombine(SDNode *N,
                                               DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;

  EVT VT = N->getValueType(0);
  unsigned Opc = N->getOpcode();
  SDValue Op0 = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);

  // Only do this if the inner op has one use since this will just increases
  // register pressure for no benefit.

  if (supportsMin3Max3(*Subtarget, Opc, VT)) {
    // max(max(a, b), c) -> max3(a, b, c)
    // min(min(a, b), c) -> min3(a, b, c)
    if (Op0.getOpcode() == Opc && Op0.hasOneUse()) {
      SDLoc DL(N);
      return DAG.getNode(minMaxOpcToMin3Max3Opc(Opc),
                         DL,
                         N->getValueType(0),
                         Op0.getOperand(0),
                         Op0.getOperand(1),
                         Op1);
    }

    // Try commuted.
    // max(a, max(b, c)) -> max3(a, b, c)
    // min(a, min(b, c)) -> min3(a, b, c)
    if (Op1.getOpcode() == Opc && Op1.hasOneUse()) {
      SDLoc DL(N);
      return DAG.getNode(minMaxOpcToMin3Max3Opc(Opc),
                         DL,
                         N->getValueType(0),
                         Op0,
                         Op1.getOperand(0),
                         Op1.getOperand(1));
    }
  }

  // min(max(x, K0), K1), K0 < K1 -> med3(x, K0, K1)
  // max(min(x, K0), K1), K1 < K0 -> med3(x, K1, K0)
  if (Opc == ISD::SMIN && Op0.getOpcode() == ISD::SMAX && Op0.hasOneUse()) {
    if (SDValue Med3 = performIntMed3ImmCombine(
            DAG, SDLoc(N), Op0->getOperand(0), Op1, Op0->getOperand(1), true))
      return Med3;
  }
  if (Opc == ISD::SMAX && Op0.getOpcode() == ISD::SMIN && Op0.hasOneUse()) {
    if (SDValue Med3 = performIntMed3ImmCombine(
            DAG, SDLoc(N), Op0->getOperand(0), Op0->getOperand(1), Op1, true))
      return Med3;
  }

  if (Opc == ISD::UMIN && Op0.getOpcode() == ISD::UMAX && Op0.hasOneUse()) {
    if (SDValue Med3 = performIntMed3ImmCombine(
            DAG, SDLoc(N), Op0->getOperand(0), Op1, Op0->getOperand(1), false))
      return Med3;
  }
  if (Opc == ISD::UMAX && Op0.getOpcode() == ISD::UMIN && Op0.hasOneUse()) {
    if (SDValue Med3 = performIntMed3ImmCombine(
            DAG, SDLoc(N), Op0->getOperand(0), Op0->getOperand(1), Op1, false))
      return Med3;
  }

  // fminnum(fmaxnum(x, K0), K1), K0 < K1 && !is_snan(x) -> fmed3(x, K0, K1)
  if (((Opc == ISD::FMINNUM && Op0.getOpcode() == ISD::FMAXNUM) ||
       (Opc == ISD::FMINNUM_IEEE && Op0.getOpcode() == ISD::FMAXNUM_IEEE) ||
       (Opc == AMDGPUISD::FMIN_LEGACY &&
        Op0.getOpcode() == AMDGPUISD::FMAX_LEGACY)) &&
      (VT == MVT::f32 || VT == MVT::f64 ||
       (VT == MVT::f16 && Subtarget->has16BitInsts()) ||
       (VT == MVT::v2f16 && Subtarget->hasVOP3PInsts())) &&
      Op0.hasOneUse()) {
    if (SDValue Res = performFPMed3ImmCombine(DAG, SDLoc(N), Op0, Op1))
      return Res;
  }

  return SDValue();
}

static bool isClampZeroToOne(SDValue A, SDValue B) {
  if (ConstantFPSDNode *CA = dyn_cast<ConstantFPSDNode>(A)) {
    if (ConstantFPSDNode *CB = dyn_cast<ConstantFPSDNode>(B)) {
      // FIXME: Should this be allowing -0.0?
      return (CA->isExactlyValue(0.0) && CB->isExactlyValue(1.0)) ||
             (CA->isExactlyValue(1.0) && CB->isExactlyValue(0.0));
    }
  }

  return false;
}

// FIXME: Should only worry about snans for version with chain.
SDValue SITargetLowering::performFMed3Combine(SDNode *N,
                                              DAGCombinerInfo &DCI) const {
  EVT VT = N->getValueType(0);
  // v_med3_f32 and v_max_f32 behave identically wrt denorms, exceptions and
  // NaNs. With a NaN input, the order of the operands may change the result.

  SelectionDAG &DAG = DCI.DAG;
  SDLoc SL(N);

  SDValue Src0 = N->getOperand(0);
  SDValue Src1 = N->getOperand(1);
  SDValue Src2 = N->getOperand(2);

  if (isClampZeroToOne(Src0, Src1)) {
    // const_a, const_b, x -> clamp is safe in all cases including signaling
    // nans.
    // FIXME: Should this be allowing -0.0?
    return DAG.getNode(AMDGPUISD::CLAMP, SL, VT, Src2);
  }

  const MachineFunction &MF = DAG.getMachineFunction();
  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();

  // FIXME: dx10_clamp behavior assumed in instcombine. Should we really bother
  // handling no dx10-clamp?
  if (Info->getMode().DX10Clamp) {
    // If NaNs is clamped to 0, we are free to reorder the inputs.

    if (isa<ConstantFPSDNode>(Src0) && !isa<ConstantFPSDNode>(Src1))
      std::swap(Src0, Src1);

    if (isa<ConstantFPSDNode>(Src1) && !isa<ConstantFPSDNode>(Src2))
      std::swap(Src1, Src2);

    if (isa<ConstantFPSDNode>(Src0) && !isa<ConstantFPSDNode>(Src1))
      std::swap(Src0, Src1);

    if (isClampZeroToOne(Src1, Src2))
      return DAG.getNode(AMDGPUISD::CLAMP, SL, VT, Src0);
  }

  return SDValue();
}

SDValue SITargetLowering::performCvtPkRTZCombine(SDNode *N,
                                                 DAGCombinerInfo &DCI) const {
  SDValue Src0 = N->getOperand(0);
  SDValue Src1 = N->getOperand(1);
  if (Src0.isUndef() && Src1.isUndef())
    return DCI.DAG.getUNDEF(N->getValueType(0));
  return SDValue();
}

// Check if EXTRACT_VECTOR_ELT/INSERT_VECTOR_ELT (<n x e>, var-idx) should be
// expanded into a set of cmp/select instructions.
bool SITargetLowering::shouldExpandVectorDynExt(unsigned EltSize,
                                                unsigned NumElem,
                                                bool IsDivergentIdx,
                                                const GCNSubtarget *Subtarget) {
  if (UseDivergentRegisterIndexing)
    return false;

  unsigned VecSize = EltSize * NumElem;

  // Sub-dword vectors of size 2 dword or less have better implementation.
  if (VecSize <= 64 && EltSize < 32)
    return false;

  // Always expand the rest of sub-dword instructions, otherwise it will be
  // lowered via memory.
  if (EltSize < 32)
    return true;

  // Always do this if var-idx is divergent, otherwise it will become a loop.
  if (IsDivergentIdx)
    return true;

  // Large vectors would yield too many compares and v_cndmask_b32 instructions.
  unsigned NumInsts = NumElem /* Number of compares */ +
                      ((EltSize + 31) / 32) * NumElem /* Number of cndmasks */;

  // On some architectures (GFX9) movrel is not available and it's better
  // to expand.
  if (!Subtarget->hasMovrel())
    return NumInsts <= 16;

  // If movrel is available, use it instead of expanding for vector of 8
  // elements.
  return NumInsts <= 15;
}

bool SITargetLowering::shouldExpandVectorDynExt(SDNode *N) const {
  SDValue Idx = N->getOperand(N->getNumOperands() - 1);
  if (isa<ConstantSDNode>(Idx))
    return false;

  SDValue Vec = N->getOperand(0);
  EVT VecVT = Vec.getValueType();
  EVT EltVT = VecVT.getVectorElementType();
  unsigned EltSize = EltVT.getSizeInBits();
  unsigned NumElem = VecVT.getVectorNumElements();

  return SITargetLowering::shouldExpandVectorDynExt(
      EltSize, NumElem, Idx->isDivergent(), getSubtarget());
}

SDValue SITargetLowering::performExtractVectorEltCombine(
  SDNode *N, DAGCombinerInfo &DCI) const {
  SDValue Vec = N->getOperand(0);
  SelectionDAG &DAG = DCI.DAG;

  EVT VecVT = Vec.getValueType();
  EVT VecEltVT = VecVT.getVectorElementType();
  EVT ResVT = N->getValueType(0);

  unsigned VecSize = VecVT.getSizeInBits();
  unsigned VecEltSize = VecEltVT.getSizeInBits();

  if ((Vec.getOpcode() == ISD::FNEG ||
       Vec.getOpcode() == ISD::FABS) && allUsesHaveSourceMods(N)) {
    SDLoc SL(N);
    SDValue Idx = N->getOperand(1);
    SDValue Elt =
        DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, ResVT, Vec.getOperand(0), Idx);
    return DAG.getNode(Vec.getOpcode(), SL, ResVT, Elt);
  }

  // ScalarRes = EXTRACT_VECTOR_ELT ((vector-BINOP Vec1, Vec2), Idx)
  //    =>
  // Vec1Elt = EXTRACT_VECTOR_ELT(Vec1, Idx)
  // Vec2Elt = EXTRACT_VECTOR_ELT(Vec2, Idx)
  // ScalarRes = scalar-BINOP Vec1Elt, Vec2Elt
  if (Vec.hasOneUse() && DCI.isBeforeLegalize() && VecEltVT == ResVT) {
    SDLoc SL(N);
    SDValue Idx = N->getOperand(1);
    unsigned Opc = Vec.getOpcode();

    switch(Opc) {
    default:
      break;
      // TODO: Support other binary operations.
    case ISD::FADD:
    case ISD::FSUB:
    case ISD::FMUL:
    case ISD::ADD:
    case ISD::UMIN:
    case ISD::UMAX:
    case ISD::SMIN:
    case ISD::SMAX:
    case ISD::FMAXNUM:
    case ISD::FMINNUM:
    case ISD::FMAXNUM_IEEE:
    case ISD::FMINNUM_IEEE:
    case ISD::FMAXIMUM:
    case ISD::FMINIMUM: {
      SDValue Elt0 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, ResVT,
                                 Vec.getOperand(0), Idx);
      SDValue Elt1 = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, ResVT,
                                 Vec.getOperand(1), Idx);

      DCI.AddToWorklist(Elt0.getNode());
      DCI.AddToWorklist(Elt1.getNode());
      return DAG.getNode(Opc, SL, ResVT, Elt0, Elt1, Vec->getFlags());
    }
    }
  }

  // EXTRACT_VECTOR_ELT (<n x e>, var-idx) => n x select (e, const-idx)
  if (shouldExpandVectorDynExt(N)) {
    SDLoc SL(N);
    SDValue Idx = N->getOperand(1);
    SDValue V;
    for (unsigned I = 0, E = VecVT.getVectorNumElements(); I < E; ++I) {
      SDValue IC = DAG.getVectorIdxConstant(I, SL);
      SDValue Elt = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, ResVT, Vec, IC);
      if (I == 0)
        V = Elt;
      else
        V = DAG.getSelectCC(SL, Idx, IC, Elt, V, ISD::SETEQ);
    }
    return V;
  }

  if (!DCI.isBeforeLegalize())
    return SDValue();

  // Try to turn sub-dword accesses of vectors into accesses of the same 32-bit
  // elements. This exposes more load reduction opportunities by replacing
  // multiple small extract_vector_elements with a single 32-bit extract.
  auto *Idx = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (isa<MemSDNode>(Vec) && VecEltSize <= 16 && VecEltVT.isByteSized() &&
      VecSize > 32 && VecSize % 32 == 0 && Idx) {
    EVT NewVT = getEquivalentMemType(*DAG.getContext(), VecVT);

    unsigned BitIndex = Idx->getZExtValue() * VecEltSize;
    unsigned EltIdx = BitIndex / 32;
    unsigned LeftoverBitIdx = BitIndex % 32;
    SDLoc SL(N);

    SDValue Cast = DAG.getNode(ISD::BITCAST, SL, NewVT, Vec);
    DCI.AddToWorklist(Cast.getNode());

    SDValue Elt = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, Cast,
                              DAG.getConstant(EltIdx, SL, MVT::i32));
    DCI.AddToWorklist(Elt.getNode());
    SDValue Srl = DAG.getNode(ISD::SRL, SL, MVT::i32, Elt,
                              DAG.getConstant(LeftoverBitIdx, SL, MVT::i32));
    DCI.AddToWorklist(Srl.getNode());

    EVT VecEltAsIntVT = VecEltVT.changeTypeToInteger();
    SDValue Trunc = DAG.getNode(ISD::TRUNCATE, SL, VecEltAsIntVT, Srl);
    DCI.AddToWorklist(Trunc.getNode());

    if (VecEltVT == ResVT) {
      return DAG.getNode(ISD::BITCAST, SL, VecEltVT, Trunc);
    }

    assert(ResVT.isScalarInteger());
    return DAG.getAnyExtOrTrunc(Trunc, SL, ResVT);
  }

  return SDValue();
}

SDValue
SITargetLowering::performInsertVectorEltCombine(SDNode *N,
                                                DAGCombinerInfo &DCI) const {
  SDValue Vec = N->getOperand(0);
  SDValue Idx = N->getOperand(2);
  EVT VecVT = Vec.getValueType();
  EVT EltVT = VecVT.getVectorElementType();

  // INSERT_VECTOR_ELT (<n x e>, var-idx)
  // => BUILD_VECTOR n x select (e, const-idx)
  if (!shouldExpandVectorDynExt(N))
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc SL(N);
  SDValue Ins = N->getOperand(1);
  EVT IdxVT = Idx.getValueType();

  SmallVector<SDValue, 16> Ops;
  for (unsigned I = 0, E = VecVT.getVectorNumElements(); I < E; ++I) {
    SDValue IC = DAG.getConstant(I, SL, IdxVT);
    SDValue Elt = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, EltVT, Vec, IC);
    SDValue V = DAG.getSelectCC(SL, Idx, IC, Ins, Elt, ISD::SETEQ);
    Ops.push_back(V);
  }

  return DAG.getBuildVector(VecVT, SL, Ops);
}

/// Return the source of an fp_extend from f16 to f32, or a converted FP
/// constant.
static SDValue strictFPExtFromF16(SelectionDAG &DAG, SDValue Src) {
  if (Src.getOpcode() == ISD::FP_EXTEND &&
      Src.getOperand(0).getValueType() == MVT::f16) {
    return Src.getOperand(0);
  }

  if (auto *CFP = dyn_cast<ConstantFPSDNode>(Src)) {
    APFloat Val = CFP->getValueAPF();
    bool LosesInfo = true;
    Val.convert(APFloat::IEEEhalf(), APFloat::rmNearestTiesToEven, &LosesInfo);
    if (!LosesInfo)
      return DAG.getConstantFP(Val, SDLoc(Src), MVT::f16);
  }

  return SDValue();
}

SDValue SITargetLowering::performFPRoundCombine(SDNode *N,
                                                DAGCombinerInfo &DCI) const {
  assert(Subtarget->has16BitInsts() && !Subtarget->hasMed3_16() &&
         "combine only useful on gfx8");

  SDValue TruncSrc = N->getOperand(0);
  EVT VT = N->getValueType(0);
  if (VT != MVT::f16)
    return SDValue();

  if (TruncSrc.getOpcode() != AMDGPUISD::FMED3 ||
      TruncSrc.getValueType() != MVT::f32 || !TruncSrc.hasOneUse())
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc SL(N);

  // Optimize f16 fmed3 pattern performed on f32. On gfx8 there is no f16 fmed3,
  // and expanding it with min/max saves 1 instruction vs. casting to f32 and
  // casting back.

  // fptrunc (f32 (fmed3 (fpext f16:a, fpext f16:b, fpext f16:c))) =>
  // fmin(fmax(a, b), fmax(fmin(a, b), c))
  SDValue A = strictFPExtFromF16(DAG, TruncSrc.getOperand(0));
  if (!A)
    return SDValue();

  SDValue B = strictFPExtFromF16(DAG, TruncSrc.getOperand(1));
  if (!B)
    return SDValue();

  SDValue C = strictFPExtFromF16(DAG, TruncSrc.getOperand(2));
  if (!C)
    return SDValue();

  // This changes signaling nan behavior. If an input is a signaling nan, it
  // would have been quieted by the fpext originally. We don't care because
  // these are unconstrained ops. If we needed to insert quieting canonicalizes
  // we would be worse off than just doing the promotion.
  SDValue A1 = DAG.getNode(ISD::FMINNUM_IEEE, SL, VT, A, B);
  SDValue B1 = DAG.getNode(ISD::FMAXNUM_IEEE, SL, VT, A, B);
  SDValue C1 = DAG.getNode(ISD::FMAXNUM_IEEE, SL, VT, A1, C);
  return DAG.getNode(ISD::FMINNUM_IEEE, SL, VT, B1, C1);
}

unsigned SITargetLowering::getFusedOpcode(const SelectionDAG &DAG,
                                          const SDNode *N0,
                                          const SDNode *N1) const {
  EVT VT = N0->getValueType(0);

  // Only do this if we are not trying to support denormals. v_mad_f32 does not
  // support denormals ever.
  if (((VT == MVT::f32 &&
        denormalModeIsFlushAllF32(DAG.getMachineFunction())) ||
       (VT == MVT::f16 && Subtarget->hasMadF16() &&
        denormalModeIsFlushAllF64F16(DAG.getMachineFunction()))) &&
      isOperationLegal(ISD::FMAD, VT))
    return ISD::FMAD;

  const TargetOptions &Options = DAG.getTarget().Options;
  if ((Options.AllowFPOpFusion == FPOpFusion::Fast || Options.UnsafeFPMath ||
       (N0->getFlags().hasAllowContract() &&
        N1->getFlags().hasAllowContract())) &&
      isFMAFasterThanFMulAndFAdd(DAG.getMachineFunction(), VT)) {
    return ISD::FMA;
  }

  return 0;
}

// For a reassociatable opcode perform:
// op x, (op y, z) -> op (op x, z), y, if x and z are uniform
SDValue SITargetLowering::reassociateScalarOps(SDNode *N,
                                               SelectionDAG &DAG) const {
  EVT VT = N->getValueType(0);
  if (VT != MVT::i32 && VT != MVT::i64)
    return SDValue();

  if (DAG.isBaseWithConstantOffset(SDValue(N, 0)))
    return SDValue();

  unsigned Opc = N->getOpcode();
  SDValue Op0 = N->getOperand(0);
  SDValue Op1 = N->getOperand(1);

  if (!(Op0->isDivergent() ^ Op1->isDivergent()))
    return SDValue();

  if (Op0->isDivergent())
    std::swap(Op0, Op1);

  if (Op1.getOpcode() != Opc || !Op1.hasOneUse())
    return SDValue();

  SDValue Op2 = Op1.getOperand(1);
  Op1 = Op1.getOperand(0);
  if (!(Op1->isDivergent() ^ Op2->isDivergent()))
    return SDValue();

  if (Op1->isDivergent())
    std::swap(Op1, Op2);

  SDLoc SL(N);
  SDValue Add1 = DAG.getNode(Opc, SL, VT, Op0, Op1);
  return DAG.getNode(Opc, SL, VT, Add1, Op2);
}

static SDValue getMad64_32(SelectionDAG &DAG, const SDLoc &SL,
                           EVT VT,
                           SDValue N0, SDValue N1, SDValue N2,
                           bool Signed) {
  unsigned MadOpc = Signed ? AMDGPUISD::MAD_I64_I32 : AMDGPUISD::MAD_U64_U32;
  SDVTList VTs = DAG.getVTList(MVT::i64, MVT::i1);
  SDValue Mad = DAG.getNode(MadOpc, SL, VTs, N0, N1, N2);
  return DAG.getNode(ISD::TRUNCATE, SL, VT, Mad);
}

// Fold (add (mul x, y), z) --> (mad_[iu]64_[iu]32 x, y, z) plus high
// multiplies, if any.
//
// Full 64-bit multiplies that feed into an addition are lowered here instead
// of using the generic expansion. The generic expansion ends up with
// a tree of ADD nodes that prevents us from using the "add" part of the
// MAD instruction. The expansion produced here results in a chain of ADDs
// instead of a tree.
SDValue SITargetLowering::tryFoldToMad64_32(SDNode *N,
                                            DAGCombinerInfo &DCI) const {
  assert(N->getOpcode() == ISD::ADD);

  SelectionDAG &DAG = DCI.DAG;
  EVT VT = N->getValueType(0);
  SDLoc SL(N);
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  if (VT.isVector())
    return SDValue();

  // S_MUL_HI_[IU]32 was added in gfx9, which allows us to keep the overall
  // result in scalar registers for uniform values.
  if (!N->isDivergent() && Subtarget->hasSMulHi())
    return SDValue();

  unsigned NumBits = VT.getScalarSizeInBits();
  if (NumBits <= 32 || NumBits > 64)
    return SDValue();

  if (LHS.getOpcode() != ISD::MUL) {
    assert(RHS.getOpcode() == ISD::MUL);
    std::swap(LHS, RHS);
  }

  // Avoid the fold if it would unduly increase the number of multiplies due to
  // multiple uses, except on hardware with full-rate multiply-add (which is
  // part of full-rate 64-bit ops).
  if (!Subtarget->hasFullRate64Ops()) {
    unsigned NumUsers = 0;
    for (SDNode *Use : LHS->uses()) {
      // There is a use that does not feed into addition, so the multiply can't
      // be removed. We prefer MUL + ADD + ADDC over MAD + MUL.
      if (Use->getOpcode() != ISD::ADD)
        return SDValue();

      // We prefer 2xMAD over MUL + 2xADD + 2xADDC (code density), and prefer
      // MUL + 3xADD + 3xADDC over 3xMAD.
      ++NumUsers;
      if (NumUsers >= 3)
        return SDValue();
    }
  }

  SDValue MulLHS = LHS.getOperand(0);
  SDValue MulRHS = LHS.getOperand(1);
  SDValue AddRHS = RHS;

  // Always check whether operands are small unsigned values, since that
  // knowledge is useful in more cases. Check for small signed values only if
  // doing so can unlock a shorter code sequence.
  bool MulLHSUnsigned32 = numBitsUnsigned(MulLHS, DAG) <= 32;
  bool MulRHSUnsigned32 = numBitsUnsigned(MulRHS, DAG) <= 32;

  bool MulSignedLo = false;
  if (!MulLHSUnsigned32 || !MulRHSUnsigned32) {
    MulSignedLo = numBitsSigned(MulLHS, DAG) <= 32 &&
                  numBitsSigned(MulRHS, DAG) <= 32;
  }

  // The operands and final result all have the same number of bits. If
  // operands need to be extended, they can be extended with garbage. The
  // resulting garbage in the high bits of the mad_[iu]64_[iu]32 result is
  // truncated away in the end.
  if (VT != MVT::i64) {
    MulLHS = DAG.getNode(ISD::ANY_EXTEND, SL, MVT::i64, MulLHS);
    MulRHS = DAG.getNode(ISD::ANY_EXTEND, SL, MVT::i64, MulRHS);
    AddRHS = DAG.getNode(ISD::ANY_EXTEND, SL, MVT::i64, AddRHS);
  }

  // The basic code generated is conceptually straightforward. Pseudo code:
  //
  //   accum = mad_64_32 lhs.lo, rhs.lo, accum
  //   accum.hi = add (mul lhs.hi, rhs.lo), accum.hi
  //   accum.hi = add (mul lhs.lo, rhs.hi), accum.hi
  //
  // The second and third lines are optional, depending on whether the factors
  // are {sign,zero}-extended or not.
  //
  // The actual DAG is noisier than the pseudo code, but only due to
  // instructions that disassemble values into low and high parts, and
  // assemble the final result.
  SDValue One = DAG.getConstant(1, SL, MVT::i32);

  auto MulLHSLo = DAG.getNode(ISD::TRUNCATE, SL, MVT::i32, MulLHS);
  auto MulRHSLo = DAG.getNode(ISD::TRUNCATE, SL, MVT::i32, MulRHS);
  SDValue Accum =
      getMad64_32(DAG, SL, MVT::i64, MulLHSLo, MulRHSLo, AddRHS, MulSignedLo);

  if (!MulSignedLo && (!MulLHSUnsigned32 || !MulRHSUnsigned32)) {
    SDValue AccumLo, AccumHi;
    std::tie(AccumLo, AccumHi) = DAG.SplitScalar(Accum, SL, MVT::i32, MVT::i32);

    if (!MulLHSUnsigned32) {
      auto MulLHSHi =
          DAG.getNode(ISD::EXTRACT_ELEMENT, SL, MVT::i32, MulLHS, One);
      SDValue MulHi = DAG.getNode(ISD::MUL, SL, MVT::i32, MulLHSHi, MulRHSLo);
      AccumHi = DAG.getNode(ISD::ADD, SL, MVT::i32, MulHi, AccumHi);
    }

    if (!MulRHSUnsigned32) {
      auto MulRHSHi =
          DAG.getNode(ISD::EXTRACT_ELEMENT, SL, MVT::i32, MulRHS, One);
      SDValue MulHi = DAG.getNode(ISD::MUL, SL, MVT::i32, MulLHSLo, MulRHSHi);
      AccumHi = DAG.getNode(ISD::ADD, SL, MVT::i32, MulHi, AccumHi);
    }

    Accum = DAG.getBuildVector(MVT::v2i32, SL, {AccumLo, AccumHi});
    Accum = DAG.getBitcast(MVT::i64, Accum);
  }

  if (VT != MVT::i64)
    Accum = DAG.getNode(ISD::TRUNCATE, SL, VT, Accum);
  return Accum;
}

// Collect the ultimate src of each of the mul node's operands, and confirm
// each operand is 8 bytes.
static std::optional<ByteProvider<SDValue>>
handleMulOperand(const SDValue &MulOperand) {
  auto Byte0 = calculateByteProvider(MulOperand, 0, 0);
  if (!Byte0 || Byte0->isConstantZero()) {
    return std::nullopt;
  }
  auto Byte1 = calculateByteProvider(MulOperand, 1, 0);
  if (Byte1 && !Byte1->isConstantZero()) {
    return std::nullopt;
  }
  return Byte0;
}

static unsigned addPermMasks(unsigned First, unsigned Second) {
  unsigned FirstCs = First & 0x0c0c0c0c;
  unsigned SecondCs = Second & 0x0c0c0c0c;
  unsigned FirstNoCs = First & ~0x0c0c0c0c;
  unsigned SecondNoCs = Second & ~0x0c0c0c0c;

  assert((FirstCs & 0xFF) | (SecondCs & 0xFF));
  assert((FirstCs & 0xFF00) | (SecondCs & 0xFF00));
  assert((FirstCs & 0xFF0000) | (SecondCs & 0xFF0000));
  assert((FirstCs & 0xFF000000) | (SecondCs & 0xFF000000));

  return (FirstNoCs | SecondNoCs) | (FirstCs & SecondCs);
}

struct DotSrc {
  SDValue SrcOp;
  int64_t PermMask;
  int64_t DWordOffset;
};

static void placeSources(ByteProvider<SDValue> &Src0,
                         ByteProvider<SDValue> &Src1,
                         SmallVectorImpl<DotSrc> &Src0s,
                         SmallVectorImpl<DotSrc> &Src1s, int Step) {

  assert(Src0.Src.has_value() && Src1.Src.has_value());
  // Src0s and Src1s are empty, just place arbitrarily.
  if (Step == 0) {
    Src0s.push_back({*Src0.Src, ((Src0.SrcOffset % 4) << 24) + 0x0c0c0c,
                     Src0.SrcOffset / 4});
    Src1s.push_back({*Src1.Src, ((Src1.SrcOffset % 4) << 24) + 0x0c0c0c,
                     Src1.SrcOffset / 4});
    return;
  }

  for (int BPI = 0; BPI < 2; BPI++) {
    std::pair<ByteProvider<SDValue>, ByteProvider<SDValue>> BPP = {Src0, Src1};
    if (BPI == 1) {
      BPP = {Src1, Src0};
    }
    unsigned ZeroMask = 0x0c0c0c0c;
    unsigned FMask = 0xFF << (8 * (3 - Step));

    unsigned FirstMask =
        (BPP.first.SrcOffset % 4) << (8 * (3 - Step)) | (ZeroMask & ~FMask);
    unsigned SecondMask =
        (BPP.second.SrcOffset % 4) << (8 * (3 - Step)) | (ZeroMask & ~FMask);
    // Attempt to find Src vector which contains our SDValue, if so, add our
    // perm mask to the existing one. If we are unable to find a match for the
    // first SDValue, attempt to find match for the second.
    int FirstGroup = -1;
    for (int I = 0; I < 2; I++) {
      SmallVectorImpl<DotSrc> &Srcs = I == 0 ? Src0s : Src1s;
      auto MatchesFirst = [&BPP](DotSrc &IterElt) {
        return IterElt.SrcOp == *BPP.first.Src &&
               (IterElt.DWordOffset == (BPP.first.SrcOffset / 4));
      };

      auto Match = llvm::find_if(Srcs, MatchesFirst);
      if (Match != Srcs.end()) {
        Match->PermMask = addPermMasks(FirstMask, Match->PermMask);
        FirstGroup = I;
        break;
      }
    }
    if (FirstGroup != -1) {
      SmallVectorImpl<DotSrc> &Srcs = FirstGroup == 1 ? Src0s : Src1s;
      auto MatchesSecond = [&BPP](DotSrc &IterElt) {
        return IterElt.SrcOp == *BPP.second.Src &&
               (IterElt.DWordOffset == (BPP.second.SrcOffset / 4));
      };
      auto Match = llvm::find_if(Srcs, MatchesSecond);
      if (Match != Srcs.end()) {
        Match->PermMask = addPermMasks(SecondMask, Match->PermMask);
      } else
        Srcs.push_back({*BPP.second.Src, SecondMask, BPP.second.SrcOffset / 4});
      return;
    }
  }

  // If we have made it here, then we could not find a match in Src0s or Src1s
  // for either Src0 or Src1, so just place them arbitrarily.

  unsigned ZeroMask = 0x0c0c0c0c;
  unsigned FMask = 0xFF << (8 * (3 - Step));

  Src0s.push_back(
      {*Src0.Src,
       ((Src0.SrcOffset % 4) << (8 * (3 - Step)) | (ZeroMask & ~FMask)),
       Src1.SrcOffset / 4});
  Src1s.push_back(
      {*Src1.Src,
       ((Src1.SrcOffset % 4) << (8 * (3 - Step)) | (ZeroMask & ~FMask)),
       Src1.SrcOffset / 4});

  return;
}

static SDValue resolveSources(SelectionDAG &DAG, SDLoc SL,
                              SmallVectorImpl<DotSrc> &Srcs, bool IsSigned,
                              bool IsAny) {

  // If we just have one source, just permute it accordingly.
  if (Srcs.size() == 1) {
    auto Elt = Srcs.begin();
    auto EltOp = getDWordFromOffset(DAG, SL, Elt->SrcOp, Elt->DWordOffset);

    // v_perm will produce the original value
    if (Elt->PermMask == 0x3020100)
      return EltOp;

    return DAG.getNode(AMDGPUISD::PERM, SL, MVT::i32, EltOp, EltOp,
                       DAG.getConstant(Elt->PermMask, SL, MVT::i32));
  }

  auto FirstElt = Srcs.begin();
  auto SecondElt = std::next(FirstElt);

  SmallVector<SDValue, 2> Perms;

  // If we have multiple sources in the chain, combine them via perms (using
  // calculated perm mask) and Ors.
  while (true) {
    auto FirstMask = FirstElt->PermMask;
    auto SecondMask = SecondElt->PermMask;

    unsigned FirstCs = FirstMask & 0x0c0c0c0c;
    unsigned FirstPlusFour = FirstMask | 0x04040404;
    // 0x0c + 0x04 = 0x10, so anding with 0x0F will produced 0x00 for any
    // original 0x0C.
    FirstMask = (FirstPlusFour & 0x0F0F0F0F) | FirstCs;

    auto PermMask = addPermMasks(FirstMask, SecondMask);
    auto FirstVal =
        getDWordFromOffset(DAG, SL, FirstElt->SrcOp, FirstElt->DWordOffset);
    auto SecondVal =
        getDWordFromOffset(DAG, SL, SecondElt->SrcOp, SecondElt->DWordOffset);

    Perms.push_back(DAG.getNode(AMDGPUISD::PERM, SL, MVT::i32, FirstVal,
                                SecondVal,
                                DAG.getConstant(PermMask, SL, MVT::i32)));

    FirstElt = std::next(SecondElt);
    if (FirstElt == Srcs.end())
      break;

    SecondElt = std::next(FirstElt);
    // If we only have a FirstElt, then just combine that into the cumulative
    // source node.
    if (SecondElt == Srcs.end()) {
      auto EltOp =
          getDWordFromOffset(DAG, SL, FirstElt->SrcOp, FirstElt->DWordOffset);

      Perms.push_back(
          DAG.getNode(AMDGPUISD::PERM, SL, MVT::i32, EltOp, EltOp,
                      DAG.getConstant(FirstElt->PermMask, SL, MVT::i32)));
      break;
    }
  }

  assert(Perms.size() == 1 || Perms.size() == 2);
  return Perms.size() == 2
             ? DAG.getNode(ISD::OR, SL, MVT::i32, Perms[0], Perms[1])
             : Perms[0];
}

static void fixMasks(SmallVectorImpl<DotSrc> &Srcs, unsigned ChainLength) {
  for (auto &[EntryVal, EntryMask, EntryOffset] : Srcs) {
    EntryMask = EntryMask >> ((4 - ChainLength) * 8);
    auto ZeroMask = ChainLength == 2 ? 0x0c0c0000 : 0x0c000000;
    EntryMask += ZeroMask;
  }
}

static bool isMul(const SDValue Op) {
  auto Opcode = Op.getOpcode();

  return (Opcode == ISD::MUL || Opcode == AMDGPUISD::MUL_U24 ||
          Opcode == AMDGPUISD::MUL_I24);
}

static std::optional<bool>
checkDot4MulSignedness(const SDValue &N, ByteProvider<SDValue> &Src0,
                       ByteProvider<SDValue> &Src1, const SDValue &S0Op,
                       const SDValue &S1Op, const SelectionDAG &DAG) {
  // If we both ops are i8s (pre legalize-dag), then the signedness semantics
  // of the dot4 is irrelevant.
  if (S0Op.getValueSizeInBits() == 8 && S1Op.getValueSizeInBits() == 8)
    return false;

  auto Known0 = DAG.computeKnownBits(S0Op, 0);
  bool S0IsUnsigned = Known0.countMinLeadingZeros() > 0;
  bool S0IsSigned = Known0.countMinLeadingOnes() > 0;
  auto Known1 = DAG.computeKnownBits(S1Op, 0);
  bool S1IsUnsigned = Known1.countMinLeadingZeros() > 0;
  bool S1IsSigned = Known1.countMinLeadingOnes() > 0;

  assert(!(S0IsUnsigned && S0IsSigned));
  assert(!(S1IsUnsigned && S1IsSigned));

  // There are 9 possible permutations of
  // {S0IsUnsigned, S0IsSigned, S1IsUnsigned, S1IsSigned}

  // In two permutations, the sign bits are known to be the same for both Ops,
  // so simply return Signed / Unsigned corresponding to the MSB

  if ((S0IsUnsigned && S1IsUnsigned) || (S0IsSigned && S1IsSigned))
    return S0IsSigned;

  // In another two permutations, the sign bits are known to be opposite. In
  // this case return std::nullopt to indicate a bad match.

  if ((S0IsUnsigned && S1IsSigned) || (S0IsSigned && S1IsUnsigned))
    return std::nullopt;

  // In the remaining five permutations, we don't know the value of the sign
  // bit for at least one Op. Since we have a valid ByteProvider, we know that
  // the upper bits must be extension bits. Thus, the only ways for the sign
  // bit to be unknown is if it was sign extended from unknown value, or if it
  // was any extended. In either case, it is correct to use the signed
  // version of the signedness semantics of dot4

  // In two of such permutations, we known the sign bit is set for
  // one op, and the other is unknown. It is okay to used signed version of
  // dot4.
  if ((S0IsSigned && !(S1IsSigned || S1IsUnsigned)) ||
      ((S1IsSigned && !(S0IsSigned || S0IsUnsigned))))
    return true;

  // In one such permutation, we don't know either of the sign bits. It is okay
  // to used the signed version of dot4.
  if ((!(S1IsSigned || S1IsUnsigned) && !(S0IsSigned || S0IsUnsigned)))
    return true;

  // In two of such permutations, we known the sign bit is unset for
  // one op, and the other is unknown. Return std::nullopt to indicate a
  // bad match.
  if ((S0IsUnsigned && !(S1IsSigned || S1IsUnsigned)) ||
      ((S1IsUnsigned && !(S0IsSigned || S0IsUnsigned))))
    return std::nullopt;

  llvm_unreachable("Fully covered condition");
}

SDValue SITargetLowering::performAddCombine(SDNode *N,
                                            DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  EVT VT = N->getValueType(0);
  SDLoc SL(N);
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  if (LHS.getOpcode() == ISD::MUL || RHS.getOpcode() == ISD::MUL) {
    if (Subtarget->hasMad64_32()) {
      if (SDValue Folded = tryFoldToMad64_32(N, DCI))
        return Folded;
    }
  }

  if (SDValue V = reassociateScalarOps(N, DAG)) {
    return V;
  }

  if ((isMul(LHS) || isMul(RHS)) && Subtarget->hasDot7Insts() &&
      (Subtarget->hasDot1Insts() || Subtarget->hasDot8Insts())) {
    SDValue TempNode(N, 0);
    std::optional<bool> IsSigned;
    SmallVector<DotSrc, 4> Src0s;
    SmallVector<DotSrc, 4> Src1s;
    SmallVector<SDValue, 4> Src2s;

    // Match the v_dot4 tree, while collecting src nodes.
    int ChainLength = 0;
    for (int I = 0; I < 4; I++) {
      auto MulIdx = isMul(LHS) ? 0 : isMul(RHS) ? 1 : -1;
      if (MulIdx == -1)
        break;
      auto Src0 = handleMulOperand(TempNode->getOperand(MulIdx)->getOperand(0));
      if (!Src0)
        break;
      auto Src1 = handleMulOperand(TempNode->getOperand(MulIdx)->getOperand(1));
      if (!Src1)
        break;

      auto IterIsSigned = checkDot4MulSignedness(
          TempNode->getOperand(MulIdx), *Src0, *Src1,
          TempNode->getOperand(MulIdx)->getOperand(0),
          TempNode->getOperand(MulIdx)->getOperand(1), DAG);
      if (!IterIsSigned)
        break;
      if (!IsSigned)
        IsSigned = *IterIsSigned;
      if (*IterIsSigned != *IsSigned)
        break;
      placeSources(*Src0, *Src1, Src0s, Src1s, I);
      auto AddIdx = 1 - MulIdx;
      // Allow the special case where add (add (mul24, 0), mul24) became ->
      // add (mul24, mul24).
      if (I == 2 && isMul(TempNode->getOperand(AddIdx))) {
        Src2s.push_back(TempNode->getOperand(AddIdx));
        auto Src0 =
            handleMulOperand(TempNode->getOperand(AddIdx)->getOperand(0));
        if (!Src0)
          break;
        auto Src1 =
            handleMulOperand(TempNode->getOperand(AddIdx)->getOperand(1));
        if (!Src1)
          break;
        auto IterIsSigned = checkDot4MulSignedness(
            TempNode->getOperand(AddIdx), *Src0, *Src1,
            TempNode->getOperand(AddIdx)->getOperand(0),
            TempNode->getOperand(AddIdx)->getOperand(1), DAG);
        if (!IterIsSigned)
          break;
        assert(IsSigned);
        if (*IterIsSigned != *IsSigned)
          break;
        placeSources(*Src0, *Src1, Src0s, Src1s, I + 1);
        Src2s.push_back(DAG.getConstant(0, SL, MVT::i32));
        ChainLength = I + 2;
        break;
      }

      TempNode = TempNode->getOperand(AddIdx);
      Src2s.push_back(TempNode);
      ChainLength = I + 1;
      if (TempNode->getNumOperands() < 2)
        break;
      LHS = TempNode->getOperand(0);
      RHS = TempNode->getOperand(1);
    }

    if (ChainLength < 2)
      return SDValue();

    // Masks were constructed with assumption that we would find a chain of
    // length 4. If not, then we need to 0 out the MSB bits (via perm mask of
    // 0x0c) so they do not affect dot calculation.
    if (ChainLength < 4) {
      fixMasks(Src0s, ChainLength);
      fixMasks(Src1s, ChainLength);
    }

    SDValue Src0, Src1;

    // If we are just using a single source for both, and have permuted the
    // bytes consistently, we can just use the sources without permuting
    // (commutation).
    bool UseOriginalSrc = false;
    if (ChainLength == 4 && Src0s.size() == 1 && Src1s.size() == 1 &&
        Src0s.begin()->PermMask == Src1s.begin()->PermMask &&
        Src0s.begin()->SrcOp.getValueSizeInBits() >= 32 &&
        Src1s.begin()->SrcOp.getValueSizeInBits() >= 32) {
      SmallVector<unsigned, 4> SrcBytes;
      auto Src0Mask = Src0s.begin()->PermMask;
      SrcBytes.push_back(Src0Mask & 0xFF000000);
      bool UniqueEntries = true;
      for (auto I = 1; I < 4; I++) {
        auto NextByte = Src0Mask & (0xFF << ((3 - I) * 8));

        if (is_contained(SrcBytes, NextByte)) {
          UniqueEntries = false;
          break;
        }
        SrcBytes.push_back(NextByte);
      }

      if (UniqueEntries) {
        UseOriginalSrc = true;

        auto FirstElt = Src0s.begin();
        auto FirstEltOp =
            getDWordFromOffset(DAG, SL, FirstElt->SrcOp, FirstElt->DWordOffset);

        auto SecondElt = Src1s.begin();
        auto SecondEltOp = getDWordFromOffset(DAG, SL, SecondElt->SrcOp,
                                              SecondElt->DWordOffset);

        Src0 = DAG.getBitcastedAnyExtOrTrunc(FirstEltOp, SL,
                                             MVT::getIntegerVT(32));
        Src1 = DAG.getBitcastedAnyExtOrTrunc(SecondEltOp, SL,
                                             MVT::getIntegerVT(32));
      }
    }

    if (!UseOriginalSrc) {
      Src0 = resolveSources(DAG, SL, Src0s, false, true);
      Src1 = resolveSources(DAG, SL, Src1s, false, true);
    }

    assert(IsSigned);
    SDValue Src2 =
        DAG.getExtOrTrunc(*IsSigned, Src2s[ChainLength - 1], SL, MVT::i32);

    SDValue IID = DAG.getTargetConstant(*IsSigned ? Intrinsic::amdgcn_sdot4
                                                  : Intrinsic::amdgcn_udot4,
                                        SL, MVT::i64);

    assert(!VT.isVector());
    auto Dot = DAG.getNode(ISD::INTRINSIC_WO_CHAIN, SL, MVT::i32, IID, Src0,
                           Src1, Src2, DAG.getTargetConstant(0, SL, MVT::i1));

    return DAG.getExtOrTrunc(*IsSigned, Dot, SL, VT);
  }

  if (VT != MVT::i32 || !DCI.isAfterLegalizeDAG())
    return SDValue();

  // add x, zext (setcc) => uaddo_carry x, 0, setcc
  // add x, sext (setcc) => usubo_carry x, 0, setcc
  unsigned Opc = LHS.getOpcode();
  if (Opc == ISD::ZERO_EXTEND || Opc == ISD::SIGN_EXTEND ||
      Opc == ISD::ANY_EXTEND || Opc == ISD::UADDO_CARRY)
    std::swap(RHS, LHS);

  Opc = RHS.getOpcode();
  switch (Opc) {
  default: break;
  case ISD::ZERO_EXTEND:
  case ISD::SIGN_EXTEND:
  case ISD::ANY_EXTEND: {
    auto Cond = RHS.getOperand(0);
    // If this won't be a real VOPC output, we would still need to insert an
    // extra instruction anyway.
    if (!isBoolSGPR(Cond))
      break;
    SDVTList VTList = DAG.getVTList(MVT::i32, MVT::i1);
    SDValue Args[] = { LHS, DAG.getConstant(0, SL, MVT::i32), Cond };
    Opc = (Opc == ISD::SIGN_EXTEND) ? ISD::USUBO_CARRY : ISD::UADDO_CARRY;
    return DAG.getNode(Opc, SL, VTList, Args);
  }
  case ISD::UADDO_CARRY: {
    // add x, (uaddo_carry y, 0, cc) => uaddo_carry x, y, cc
    if (!isNullConstant(RHS.getOperand(1)))
      break;
    SDValue Args[] = { LHS, RHS.getOperand(0), RHS.getOperand(2) };
    return DAG.getNode(ISD::UADDO_CARRY, SDLoc(N), RHS->getVTList(), Args);
  }
  }
  return SDValue();
}

SDValue SITargetLowering::performSubCombine(SDNode *N,
                                            DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  EVT VT = N->getValueType(0);

  if (VT != MVT::i32)
    return SDValue();

  SDLoc SL(N);
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  // sub x, zext (setcc) => usubo_carry x, 0, setcc
  // sub x, sext (setcc) => uaddo_carry x, 0, setcc
  unsigned Opc = RHS.getOpcode();
  switch (Opc) {
  default: break;
  case ISD::ZERO_EXTEND:
  case ISD::SIGN_EXTEND:
  case ISD::ANY_EXTEND: {
    auto Cond = RHS.getOperand(0);
    // If this won't be a real VOPC output, we would still need to insert an
    // extra instruction anyway.
    if (!isBoolSGPR(Cond))
      break;
    SDVTList VTList = DAG.getVTList(MVT::i32, MVT::i1);
    SDValue Args[] = { LHS, DAG.getConstant(0, SL, MVT::i32), Cond };
    Opc = (Opc == ISD::SIGN_EXTEND) ? ISD::UADDO_CARRY : ISD::USUBO_CARRY;
    return DAG.getNode(Opc, SL, VTList, Args);
  }
  }

  if (LHS.getOpcode() == ISD::USUBO_CARRY) {
    // sub (usubo_carry x, 0, cc), y => usubo_carry x, y, cc
    if (!isNullConstant(LHS.getOperand(1)))
      return SDValue();
    SDValue Args[] = { LHS.getOperand(0), RHS, LHS.getOperand(2) };
    return DAG.getNode(ISD::USUBO_CARRY, SDLoc(N), LHS->getVTList(), Args);
  }
  return SDValue();
}

SDValue SITargetLowering::performAddCarrySubCarryCombine(SDNode *N,
  DAGCombinerInfo &DCI) const {

  if (N->getValueType(0) != MVT::i32)
    return SDValue();

  if (!isNullConstant(N->getOperand(1)))
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDValue LHS = N->getOperand(0);

  // uaddo_carry (add x, y), 0, cc => uaddo_carry x, y, cc
  // usubo_carry (sub x, y), 0, cc => usubo_carry x, y, cc
  unsigned LHSOpc = LHS.getOpcode();
  unsigned Opc = N->getOpcode();
  if ((LHSOpc == ISD::ADD && Opc == ISD::UADDO_CARRY) ||
      (LHSOpc == ISD::SUB && Opc == ISD::USUBO_CARRY)) {
    SDValue Args[] = { LHS.getOperand(0), LHS.getOperand(1), N->getOperand(2) };
    return DAG.getNode(Opc, SDLoc(N), N->getVTList(), Args);
  }
  return SDValue();
}

SDValue SITargetLowering::performFAddCombine(SDNode *N,
                                             DAGCombinerInfo &DCI) const {
  if (DCI.getDAGCombineLevel() < AfterLegalizeDAG)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  EVT VT = N->getValueType(0);

  SDLoc SL(N);
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  // These should really be instruction patterns, but writing patterns with
  // source modifiers is a pain.

  // fadd (fadd (a, a), b) -> mad 2.0, a, b
  if (LHS.getOpcode() == ISD::FADD) {
    SDValue A = LHS.getOperand(0);
    if (A == LHS.getOperand(1)) {
      unsigned FusedOp = getFusedOpcode(DAG, N, LHS.getNode());
      if (FusedOp != 0) {
        const SDValue Two = DAG.getConstantFP(2.0, SL, VT);
        return DAG.getNode(FusedOp, SL, VT, A, Two, RHS);
      }
    }
  }

  // fadd (b, fadd (a, a)) -> mad 2.0, a, b
  if (RHS.getOpcode() == ISD::FADD) {
    SDValue A = RHS.getOperand(0);
    if (A == RHS.getOperand(1)) {
      unsigned FusedOp = getFusedOpcode(DAG, N, RHS.getNode());
      if (FusedOp != 0) {
        const SDValue Two = DAG.getConstantFP(2.0, SL, VT);
        return DAG.getNode(FusedOp, SL, VT, A, Two, LHS);
      }
    }
  }

  return SDValue();
}

SDValue SITargetLowering::performFSubCombine(SDNode *N,
                                             DAGCombinerInfo &DCI) const {
  if (DCI.getDAGCombineLevel() < AfterLegalizeDAG)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc SL(N);
  EVT VT = N->getValueType(0);
  assert(!VT.isVector());

  // Try to get the fneg to fold into the source modifier. This undoes generic
  // DAG combines and folds them into the mad.
  //
  // Only do this if we are not trying to support denormals. v_mad_f32 does
  // not support denormals ever.
  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  if (LHS.getOpcode() == ISD::FADD) {
    // (fsub (fadd a, a), c) -> mad 2.0, a, (fneg c)
    SDValue A = LHS.getOperand(0);
    if (A == LHS.getOperand(1)) {
      unsigned FusedOp = getFusedOpcode(DAG, N, LHS.getNode());
      if (FusedOp != 0){
        const SDValue Two = DAG.getConstantFP(2.0, SL, VT);
        SDValue NegRHS = DAG.getNode(ISD::FNEG, SL, VT, RHS);

        return DAG.getNode(FusedOp, SL, VT, A, Two, NegRHS);
      }
    }
  }

  if (RHS.getOpcode() == ISD::FADD) {
    // (fsub c, (fadd a, a)) -> mad -2.0, a, c

    SDValue A = RHS.getOperand(0);
    if (A == RHS.getOperand(1)) {
      unsigned FusedOp = getFusedOpcode(DAG, N, RHS.getNode());
      if (FusedOp != 0){
        const SDValue NegTwo = DAG.getConstantFP(-2.0, SL, VT);
        return DAG.getNode(FusedOp, SL, VT, A, NegTwo, LHS);
      }
    }
  }

  return SDValue();
}

SDValue SITargetLowering::performFDivCombine(SDNode *N,
                                             DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDLoc SL(N);
  EVT VT = N->getValueType(0);
  if (VT != MVT::f16 || !Subtarget->has16BitInsts())
    return SDValue();

  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);

  SDNodeFlags Flags = N->getFlags();
  SDNodeFlags RHSFlags = RHS->getFlags();
  if (!Flags.hasAllowContract() || !RHSFlags.hasAllowContract() ||
      !RHS->hasOneUse())
    return SDValue();

  if (const ConstantFPSDNode *CLHS = dyn_cast<ConstantFPSDNode>(LHS)) {
    bool IsNegative = false;
    if (CLHS->isExactlyValue(1.0) ||
        (IsNegative = CLHS->isExactlyValue(-1.0))) {
      // fdiv contract 1.0, (sqrt contract x) -> rsq for f16
      // fdiv contract -1.0, (sqrt contract x) -> fneg(rsq) for f16
      if (RHS.getOpcode() == ISD::FSQRT) {
        // TODO: Or in RHS flags, somehow missing from SDNodeFlags
        SDValue Rsq =
            DAG.getNode(AMDGPUISD::RSQ, SL, VT, RHS.getOperand(0), Flags);
        return IsNegative ? DAG.getNode(ISD::FNEG, SL, VT, Rsq, Flags) : Rsq;
      }
    }
  }

  return SDValue();
}

SDValue SITargetLowering::performFMACombine(SDNode *N,
                                            DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  EVT VT = N->getValueType(0);
  SDLoc SL(N);

  if (!Subtarget->hasDot7Insts() || VT != MVT::f32)
    return SDValue();

  // FMA((F32)S0.x, (F32)S1. x, FMA((F32)S0.y, (F32)S1.y, (F32)z)) ->
  //   FDOT2((V2F16)S0, (V2F16)S1, (F32)z))
  SDValue Op1 = N->getOperand(0);
  SDValue Op2 = N->getOperand(1);
  SDValue FMA = N->getOperand(2);

  if (FMA.getOpcode() != ISD::FMA ||
      Op1.getOpcode() != ISD::FP_EXTEND ||
      Op2.getOpcode() != ISD::FP_EXTEND)
    return SDValue();

  // fdot2_f32_f16 always flushes fp32 denormal operand and output to zero,
  // regardless of the denorm mode setting. Therefore,
  // unsafe-fp-math/fp-contract is sufficient to allow generating fdot2.
  const TargetOptions &Options = DAG.getTarget().Options;
  if (Options.AllowFPOpFusion == FPOpFusion::Fast || Options.UnsafeFPMath ||
      (N->getFlags().hasAllowContract() &&
       FMA->getFlags().hasAllowContract())) {
    Op1 = Op1.getOperand(0);
    Op2 = Op2.getOperand(0);
    if (Op1.getOpcode() != ISD::EXTRACT_VECTOR_ELT ||
        Op2.getOpcode() != ISD::EXTRACT_VECTOR_ELT)
      return SDValue();

    SDValue Vec1 = Op1.getOperand(0);
    SDValue Idx1 = Op1.getOperand(1);
    SDValue Vec2 = Op2.getOperand(0);

    SDValue FMAOp1 = FMA.getOperand(0);
    SDValue FMAOp2 = FMA.getOperand(1);
    SDValue FMAAcc = FMA.getOperand(2);

    if (FMAOp1.getOpcode() != ISD::FP_EXTEND ||
        FMAOp2.getOpcode() != ISD::FP_EXTEND)
      return SDValue();

    FMAOp1 = FMAOp1.getOperand(0);
    FMAOp2 = FMAOp2.getOperand(0);
    if (FMAOp1.getOpcode() != ISD::EXTRACT_VECTOR_ELT ||
        FMAOp2.getOpcode() != ISD::EXTRACT_VECTOR_ELT)
      return SDValue();

    SDValue Vec3 = FMAOp1.getOperand(0);
    SDValue Vec4 = FMAOp2.getOperand(0);
    SDValue Idx2 = FMAOp1.getOperand(1);

    if (Idx1 != Op2.getOperand(1) || Idx2 != FMAOp2.getOperand(1) ||
        // Idx1 and Idx2 cannot be the same.
        Idx1 == Idx2)
      return SDValue();

    if (Vec1 == Vec2 || Vec3 == Vec4)
      return SDValue();

    if (Vec1.getValueType() != MVT::v2f16 || Vec2.getValueType() != MVT::v2f16)
      return SDValue();

    if ((Vec1 == Vec3 && Vec2 == Vec4) ||
        (Vec1 == Vec4 && Vec2 == Vec3)) {
      return DAG.getNode(AMDGPUISD::FDOT2, SL, MVT::f32, Vec1, Vec2, FMAAcc,
                         DAG.getTargetConstant(0, SL, MVT::i1));
    }
  }
  return SDValue();
}

SDValue SITargetLowering::performSetCCCombine(SDNode *N,
                                              DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDLoc SL(N);

  SDValue LHS = N->getOperand(0);
  SDValue RHS = N->getOperand(1);
  EVT VT = LHS.getValueType();
  ISD::CondCode CC = cast<CondCodeSDNode>(N->getOperand(2))->get();

  auto CRHS = dyn_cast<ConstantSDNode>(RHS);
  if (!CRHS) {
    CRHS = dyn_cast<ConstantSDNode>(LHS);
    if (CRHS) {
      std::swap(LHS, RHS);
      CC = getSetCCSwappedOperands(CC);
    }
  }

  if (CRHS) {
    if (VT == MVT::i32 && LHS.getOpcode() == ISD::SIGN_EXTEND &&
        isBoolSGPR(LHS.getOperand(0))) {
      // setcc (sext from i1 cc), -1, ne|sgt|ult) => not cc => xor cc, -1
      // setcc (sext from i1 cc), -1, eq|sle|uge) => cc
      // setcc (sext from i1 cc),  0, eq|sge|ule) => not cc => xor cc, -1
      // setcc (sext from i1 cc),  0, ne|ugt|slt) => cc
      if ((CRHS->isAllOnes() &&
           (CC == ISD::SETNE || CC == ISD::SETGT || CC == ISD::SETULT)) ||
          (CRHS->isZero() &&
           (CC == ISD::SETEQ || CC == ISD::SETGE || CC == ISD::SETULE)))
        return DAG.getNode(ISD::XOR, SL, MVT::i1, LHS.getOperand(0),
                           DAG.getConstant(-1, SL, MVT::i1));
      if ((CRHS->isAllOnes() &&
           (CC == ISD::SETEQ || CC == ISD::SETLE || CC == ISD::SETUGE)) ||
          (CRHS->isZero() &&
           (CC == ISD::SETNE || CC == ISD::SETUGT || CC == ISD::SETLT)))
        return LHS.getOperand(0);
    }

    const APInt &CRHSVal = CRHS->getAPIntValue();
    if ((CC == ISD::SETEQ || CC == ISD::SETNE) &&
        LHS.getOpcode() == ISD::SELECT &&
        isa<ConstantSDNode>(LHS.getOperand(1)) &&
        isa<ConstantSDNode>(LHS.getOperand(2)) &&
        LHS.getConstantOperandVal(1) != LHS.getConstantOperandVal(2) &&
        isBoolSGPR(LHS.getOperand(0))) {
      // Given CT != FT:
      // setcc (select cc, CT, CF), CF, eq => xor cc, -1
      // setcc (select cc, CT, CF), CF, ne => cc
      // setcc (select cc, CT, CF), CT, ne => xor cc, -1
      // setcc (select cc, CT, CF), CT, eq => cc
      const APInt &CT = LHS.getConstantOperandAPInt(1);
      const APInt &CF = LHS.getConstantOperandAPInt(2);

      if ((CF == CRHSVal && CC == ISD::SETEQ) ||
          (CT == CRHSVal && CC == ISD::SETNE))
        return DAG.getNode(ISD::XOR, SL, MVT::i1, LHS.getOperand(0),
                           DAG.getConstant(-1, SL, MVT::i1));
      if ((CF == CRHSVal && CC == ISD::SETNE) ||
          (CT == CRHSVal && CC == ISD::SETEQ))
        return LHS.getOperand(0);
    }
  }

  if (VT != MVT::f32 && VT != MVT::f64 &&
      (!Subtarget->has16BitInsts() || VT != MVT::f16))
    return SDValue();

  // Match isinf/isfinite pattern
  // (fcmp oeq (fabs x), inf) -> (fp_class x, (p_infinity | n_infinity))
  // (fcmp one (fabs x), inf) -> (fp_class x,
  // (p_normal | n_normal | p_subnormal | n_subnormal | p_zero | n_zero)
  if ((CC == ISD::SETOEQ || CC == ISD::SETONE) && LHS.getOpcode() == ISD::FABS) {
    const ConstantFPSDNode *CRHS = dyn_cast<ConstantFPSDNode>(RHS);
    if (!CRHS)
      return SDValue();

    const APFloat &APF = CRHS->getValueAPF();
    if (APF.isInfinity() && !APF.isNegative()) {
      const unsigned IsInfMask = SIInstrFlags::P_INFINITY |
                                 SIInstrFlags::N_INFINITY;
      const unsigned IsFiniteMask = SIInstrFlags::N_ZERO |
                                    SIInstrFlags::P_ZERO |
                                    SIInstrFlags::N_NORMAL |
                                    SIInstrFlags::P_NORMAL |
                                    SIInstrFlags::N_SUBNORMAL |
                                    SIInstrFlags::P_SUBNORMAL;
      unsigned Mask = CC == ISD::SETOEQ ? IsInfMask : IsFiniteMask;
      return DAG.getNode(AMDGPUISD::FP_CLASS, SL, MVT::i1, LHS.getOperand(0),
                         DAG.getConstant(Mask, SL, MVT::i32));
    }
  }

  return SDValue();
}

SDValue SITargetLowering::performCvtF32UByteNCombine(SDNode *N,
                                                     DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDLoc SL(N);
  unsigned Offset = N->getOpcode() - AMDGPUISD::CVT_F32_UBYTE0;

  SDValue Src = N->getOperand(0);
  SDValue Shift = N->getOperand(0);

  // TODO: Extend type shouldn't matter (assuming legal types).
  if (Shift.getOpcode() == ISD::ZERO_EXTEND)
    Shift = Shift.getOperand(0);

  if (Shift.getOpcode() == ISD::SRL || Shift.getOpcode() == ISD::SHL) {
    // cvt_f32_ubyte1 (shl x,  8) -> cvt_f32_ubyte0 x
    // cvt_f32_ubyte3 (shl x, 16) -> cvt_f32_ubyte1 x
    // cvt_f32_ubyte0 (srl x, 16) -> cvt_f32_ubyte2 x
    // cvt_f32_ubyte1 (srl x, 16) -> cvt_f32_ubyte3 x
    // cvt_f32_ubyte0 (srl x,  8) -> cvt_f32_ubyte1 x
    if (auto *C = dyn_cast<ConstantSDNode>(Shift.getOperand(1))) {
      SDValue Shifted = DAG.getZExtOrTrunc(Shift.getOperand(0),
                                 SDLoc(Shift.getOperand(0)), MVT::i32);

      unsigned ShiftOffset = 8 * Offset;
      if (Shift.getOpcode() == ISD::SHL)
        ShiftOffset -= C->getZExtValue();
      else
        ShiftOffset += C->getZExtValue();

      if (ShiftOffset < 32 && (ShiftOffset % 8) == 0) {
        return DAG.getNode(AMDGPUISD::CVT_F32_UBYTE0 + ShiftOffset / 8, SL,
                           MVT::f32, Shifted);
      }
    }
  }

  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  APInt DemandedBits = APInt::getBitsSet(32, 8 * Offset, 8 * Offset + 8);
  if (TLI.SimplifyDemandedBits(Src, DemandedBits, DCI)) {
    // We simplified Src. If this node is not dead, visit it again so it is
    // folded properly.
    if (N->getOpcode() != ISD::DELETED_NODE)
      DCI.AddToWorklist(N);
    return SDValue(N, 0);
  }

  // Handle (or x, (srl y, 8)) pattern when known bits are zero.
  if (SDValue DemandedSrc =
          TLI.SimplifyMultipleUseDemandedBits(Src, DemandedBits, DAG))
    return DAG.getNode(N->getOpcode(), SL, MVT::f32, DemandedSrc);

  return SDValue();
}

SDValue SITargetLowering::performClampCombine(SDNode *N,
                                              DAGCombinerInfo &DCI) const {
  ConstantFPSDNode *CSrc = dyn_cast<ConstantFPSDNode>(N->getOperand(0));
  if (!CSrc)
    return SDValue();

  const MachineFunction &MF = DCI.DAG.getMachineFunction();
  const APFloat &F = CSrc->getValueAPF();
  APFloat Zero = APFloat::getZero(F.getSemantics());
  if (F < Zero ||
      (F.isNaN() && MF.getInfo<SIMachineFunctionInfo>()->getMode().DX10Clamp)) {
    return DCI.DAG.getConstantFP(Zero, SDLoc(N), N->getValueType(0));
  }

  APFloat One(F.getSemantics(), "1.0");
  if (F > One)
    return DCI.DAG.getConstantFP(One, SDLoc(N), N->getValueType(0));

  return SDValue(CSrc, 0);
}


SDValue SITargetLowering::PerformDAGCombine(SDNode *N,
                                            DAGCombinerInfo &DCI) const {
  if (getTargetMachine().getOptLevel() == CodeGenOptLevel::None)
    return SDValue();
  switch (N->getOpcode()) {
  case ISD::ADD:
    return performAddCombine(N, DCI);
  case ISD::SUB:
    return performSubCombine(N, DCI);
  case ISD::UADDO_CARRY:
  case ISD::USUBO_CARRY:
    return performAddCarrySubCarryCombine(N, DCI);
  case ISD::FADD:
    return performFAddCombine(N, DCI);
  case ISD::FSUB:
    return performFSubCombine(N, DCI);
  case ISD::FDIV:
    return performFDivCombine(N, DCI);
  case ISD::SETCC:
    return performSetCCCombine(N, DCI);
  case ISD::FMAXNUM:
  case ISD::FMINNUM:
  case ISD::FMAXNUM_IEEE:
  case ISD::FMINNUM_IEEE:
  case ISD::FMAXIMUM:
  case ISD::FMINIMUM:
  case ISD::SMAX:
  case ISD::SMIN:
  case ISD::UMAX:
  case ISD::UMIN:
  case AMDGPUISD::FMIN_LEGACY:
  case AMDGPUISD::FMAX_LEGACY:
    return performMinMaxCombine(N, DCI);
  case ISD::FMA:
    return performFMACombine(N, DCI);
  case ISD::AND:
    return performAndCombine(N, DCI);
  case ISD::OR:
    return performOrCombine(N, DCI);
  case ISD::FSHR: {
    const SIInstrInfo *TII = getSubtarget()->getInstrInfo();
    if (N->getValueType(0) == MVT::i32 && N->isDivergent() &&
        TII->pseudoToMCOpcode(AMDGPU::V_PERM_B32_e64) != -1) {
      return matchPERM(N, DCI);
    }
    break;
  }
  case ISD::XOR:
    return performXorCombine(N, DCI);
  case ISD::ZERO_EXTEND:
    return performZeroExtendCombine(N, DCI);
  case ISD::SIGN_EXTEND_INREG:
    return performSignExtendInRegCombine(N , DCI);
  case AMDGPUISD::FP_CLASS:
    return performClassCombine(N, DCI);
  case ISD::FCANONICALIZE:
    return performFCanonicalizeCombine(N, DCI);
  case AMDGPUISD::RCP:
    return performRcpCombine(N, DCI);
  case ISD::FLDEXP:
  case AMDGPUISD::FRACT:
  case AMDGPUISD::RSQ:
  case AMDGPUISD::RCP_LEGACY:
  case AMDGPUISD::RCP_IFLAG:
  case AMDGPUISD::RSQ_CLAMP: {
    // FIXME: This is probably wrong. If src is an sNaN, it won't be quieted
    SDValue Src = N->getOperand(0);
    if (Src.isUndef())
      return Src;
    break;
  }
  case ISD::SINT_TO_FP:
  case ISD::UINT_TO_FP:
    return performUCharToFloatCombine(N, DCI);
  case ISD::FCOPYSIGN:
    return performFCopySignCombine(N, DCI);
  case AMDGPUISD::CVT_F32_UBYTE0:
  case AMDGPUISD::CVT_F32_UBYTE1:
  case AMDGPUISD::CVT_F32_UBYTE2:
  case AMDGPUISD::CVT_F32_UBYTE3:
    return performCvtF32UByteNCombine(N, DCI);
  case AMDGPUISD::FMED3:
    return performFMed3Combine(N, DCI);
  case AMDGPUISD::CVT_PKRTZ_F16_F32:
    return performCvtPkRTZCombine(N, DCI);
  case AMDGPUISD::CLAMP:
    return performClampCombine(N, DCI);
  case ISD::SCALAR_TO_VECTOR: {
    SelectionDAG &DAG = DCI.DAG;
    EVT VT = N->getValueType(0);

    // v2i16 (scalar_to_vector i16:x) -> v2i16 (bitcast (any_extend i16:x))
    if (VT == MVT::v2i16 || VT == MVT::v2f16 || VT == MVT::v2bf16) {
      SDLoc SL(N);
      SDValue Src = N->getOperand(0);
      EVT EltVT = Src.getValueType();
      if (EltVT != MVT::i16)
        Src = DAG.getNode(ISD::BITCAST, SL, MVT::i16, Src);

      SDValue Ext = DAG.getNode(ISD::ANY_EXTEND, SL, MVT::i32, Src);
      return DAG.getNode(ISD::BITCAST, SL, VT, Ext);
    }

    break;
  }
  case ISD::EXTRACT_VECTOR_ELT:
    return performExtractVectorEltCombine(N, DCI);
  case ISD::INSERT_VECTOR_ELT:
    return performInsertVectorEltCombine(N, DCI);
  case ISD::FP_ROUND:
    return performFPRoundCombine(N, DCI);
  case ISD::LOAD: {
    if (SDValue Widened = widenLoad(cast<LoadSDNode>(N), DCI))
      return Widened;
    [[fallthrough]];
  }
  default: {
    if (!DCI.isBeforeLegalize()) {
      if (MemSDNode *MemNode = dyn_cast<MemSDNode>(N))
        return performMemSDNodeCombine(MemNode, DCI);
    }

    break;
  }
  }

  return AMDGPUTargetLowering::PerformDAGCombine(N, DCI);
}

/// Helper function for adjustWritemask
static unsigned SubIdx2Lane(unsigned Idx) {
  switch (Idx) {
  default: return ~0u;
  case AMDGPU::sub0: return 0;
  case AMDGPU::sub1: return 1;
  case AMDGPU::sub2: return 2;
  case AMDGPU::sub3: return 3;
  case AMDGPU::sub4: return 4; // Possible with TFE/LWE
  }
}

/// Adjust the writemask of MIMG, VIMAGE or VSAMPLE instructions
SDNode *SITargetLowering::adjustWritemask(MachineSDNode *&Node,
                                          SelectionDAG &DAG) const {
  unsigned Opcode = Node->getMachineOpcode();

  // Subtract 1 because the vdata output is not a MachineSDNode operand.
  int D16Idx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::d16) - 1;
  if (D16Idx >= 0 && Node->getConstantOperandVal(D16Idx))
    return Node; // not implemented for D16

  SDNode *Users[5] = { nullptr };
  unsigned Lane = 0;
  unsigned DmaskIdx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::dmask) - 1;
  unsigned OldDmask = Node->getConstantOperandVal(DmaskIdx);
  unsigned NewDmask = 0;
  unsigned TFEIdx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::tfe) - 1;
  unsigned LWEIdx = AMDGPU::getNamedOperandIdx(Opcode, AMDGPU::OpName::lwe) - 1;
  bool UsesTFC = ((int(TFEIdx) >= 0 && Node->getConstantOperandVal(TFEIdx)) ||
                  (int(LWEIdx) >= 0 && Node->getConstantOperandVal(LWEIdx)))
                     ? true
                     : false;
  unsigned TFCLane = 0;
  bool HasChain = Node->getNumValues() > 1;

  if (OldDmask == 0) {
    // These are folded out, but on the chance it happens don't assert.
    return Node;
  }

  unsigned OldBitsSet = llvm::popcount(OldDmask);
  // Work out which is the TFE/LWE lane if that is enabled.
  if (UsesTFC) {
    TFCLane = OldBitsSet;
  }

  // Try to figure out the used register components
  for (SDNode::use_iterator I = Node->use_begin(), E = Node->use_end();
       I != E; ++I) {

    // Don't look at users of the chain.
    if (I.getUse().getResNo() != 0)
      continue;

    // Abort if we can't understand the usage
    if (!I->isMachineOpcode() ||
        I->getMachineOpcode() != TargetOpcode::EXTRACT_SUBREG)
      return Node;

    // Lane means which subreg of %vgpra_vgprb_vgprc_vgprd is used.
    // Note that subregs are packed, i.e. Lane==0 is the first bit set
    // in OldDmask, so it can be any of X,Y,Z,W; Lane==1 is the second bit
    // set, etc.
    Lane = SubIdx2Lane(I->getConstantOperandVal(1));
    if (Lane == ~0u)
      return Node;

    // Check if the use is for the TFE/LWE generated result at VGPRn+1.
    if (UsesTFC && Lane == TFCLane) {
      Users[Lane] = *I;
    } else {
      // Set which texture component corresponds to the lane.
      unsigned Comp;
      for (unsigned i = 0, Dmask = OldDmask; (i <= Lane) && (Dmask != 0); i++) {
        Comp = llvm::countr_zero(Dmask);
        Dmask &= ~(1 << Comp);
      }

      // Abort if we have more than one user per component.
      if (Users[Lane])
        return Node;

      Users[Lane] = *I;
      NewDmask |= 1 << Comp;
    }
  }

  // Don't allow 0 dmask, as hardware assumes one channel enabled.
  bool NoChannels = !NewDmask;
  if (NoChannels) {
    if (!UsesTFC) {
      // No uses of the result and not using TFC. Then do nothing.
      return Node;
    }
    // If the original dmask has one channel - then nothing to do
    if (OldBitsSet == 1)
      return Node;
    // Use an arbitrary dmask - required for the instruction to work
    NewDmask = 1;
  }
  // Abort if there's no change
  if (NewDmask == OldDmask)
    return Node;

  unsigned BitsSet = llvm::popcount(NewDmask);

  // Check for TFE or LWE - increase the number of channels by one to account
  // for the extra return value
  // This will need adjustment for D16 if this is also included in
  // adjustWriteMask (this function) but at present D16 are excluded.
  unsigned NewChannels = BitsSet + UsesTFC;

  int NewOpcode =
      AMDGPU::getMaskedMIMGOp(Node->getMachineOpcode(), NewChannels);
  assert(NewOpcode != -1 &&
         NewOpcode != static_cast<int>(Node->getMachineOpcode()) &&
         "failed to find equivalent MIMG op");

  // Adjust the writemask in the node
  SmallVector<SDValue, 12> Ops;
  Ops.insert(Ops.end(), Node->op_begin(), Node->op_begin() + DmaskIdx);
  Ops.push_back(DAG.getTargetConstant(NewDmask, SDLoc(Node), MVT::i32));
  Ops.insert(Ops.end(), Node->op_begin() + DmaskIdx + 1, Node->op_end());

  MVT SVT = Node->getValueType(0).getVectorElementType().getSimpleVT();

  MVT ResultVT = NewChannels == 1 ?
    SVT : MVT::getVectorVT(SVT, NewChannels == 3 ? 4 :
                           NewChannels == 5 ? 8 : NewChannels);
  SDVTList NewVTList = HasChain ?
    DAG.getVTList(ResultVT, MVT::Other) : DAG.getVTList(ResultVT);


  MachineSDNode *NewNode = DAG.getMachineNode(NewOpcode, SDLoc(Node),
                                              NewVTList, Ops);

  if (HasChain) {
    // Update chain.
    DAG.setNodeMemRefs(NewNode, Node->memoperands());
    DAG.ReplaceAllUsesOfValueWith(SDValue(Node, 1), SDValue(NewNode, 1));
  }

  if (NewChannels == 1) {
    assert(Node->hasNUsesOfValue(1, 0));
    SDNode *Copy = DAG.getMachineNode(TargetOpcode::COPY,
                                      SDLoc(Node), Users[Lane]->getValueType(0),
                                      SDValue(NewNode, 0));
    DAG.ReplaceAllUsesWith(Users[Lane], Copy);
    return nullptr;
  }

  // Update the users of the node with the new indices
  for (unsigned i = 0, Idx = AMDGPU::sub0; i < 5; ++i) {
    SDNode *User = Users[i];
    if (!User) {
      // Handle the special case of NoChannels. We set NewDmask to 1 above, but
      // Users[0] is still nullptr because channel 0 doesn't really have a use.
      if (i || !NoChannels)
        continue;
    } else {
      SDValue Op = DAG.getTargetConstant(Idx, SDLoc(User), MVT::i32);
      SDNode *NewUser = DAG.UpdateNodeOperands(User, SDValue(NewNode, 0), Op);
      if (NewUser != User) {
        DAG.ReplaceAllUsesWith(SDValue(User, 0), SDValue(NewUser, 0));
        DAG.RemoveDeadNode(User);
      }
    }

    switch (Idx) {
    default: break;
    case AMDGPU::sub0: Idx = AMDGPU::sub1; break;
    case AMDGPU::sub1: Idx = AMDGPU::sub2; break;
    case AMDGPU::sub2: Idx = AMDGPU::sub3; break;
    case AMDGPU::sub3: Idx = AMDGPU::sub4; break;
    }
  }

  DAG.RemoveDeadNode(Node);
  return nullptr;
}

static bool isFrameIndexOp(SDValue Op) {
  if (Op.getOpcode() == ISD::AssertZext)
    Op = Op.getOperand(0);

  return isa<FrameIndexSDNode>(Op);
}

/// Legalize target independent instructions (e.g. INSERT_SUBREG)
/// with frame index operands.
/// LLVM assumes that inputs are to these instructions are registers.
SDNode *SITargetLowering::legalizeTargetIndependentNode(SDNode *Node,
                                                        SelectionDAG &DAG) const {
  if (Node->getOpcode() == ISD::CopyToReg) {
    RegisterSDNode *DestReg = cast<RegisterSDNode>(Node->getOperand(1));
    SDValue SrcVal = Node->getOperand(2);

    // Insert a copy to a VReg_1 virtual register so LowerI1Copies doesn't have
    // to try understanding copies to physical registers.
    if (SrcVal.getValueType() == MVT::i1 && DestReg->getReg().isPhysical()) {
      SDLoc SL(Node);
      MachineRegisterInfo &MRI = DAG.getMachineFunction().getRegInfo();
      SDValue VReg = DAG.getRegister(
        MRI.createVirtualRegister(&AMDGPU::VReg_1RegClass), MVT::i1);

      SDNode *Glued = Node->getGluedNode();
      SDValue ToVReg
        = DAG.getCopyToReg(Node->getOperand(0), SL, VReg, SrcVal,
                         SDValue(Glued, Glued ? Glued->getNumValues() - 1 : 0));
      SDValue ToResultReg
        = DAG.getCopyToReg(ToVReg, SL, SDValue(DestReg, 0),
                           VReg, ToVReg.getValue(1));
      DAG.ReplaceAllUsesWith(Node, ToResultReg.getNode());
      DAG.RemoveDeadNode(Node);
      return ToResultReg.getNode();
    }
  }

  SmallVector<SDValue, 8> Ops;
  for (unsigned i = 0; i < Node->getNumOperands(); ++i) {
    if (!isFrameIndexOp(Node->getOperand(i))) {
      Ops.push_back(Node->getOperand(i));
      continue;
    }

    SDLoc DL(Node);
    Ops.push_back(SDValue(DAG.getMachineNode(AMDGPU::S_MOV_B32, DL,
                                     Node->getOperand(i).getValueType(),
                                     Node->getOperand(i)), 0));
  }

  return DAG.UpdateNodeOperands(Node, Ops);
}

/// Fold the instructions after selecting them.
/// Returns null if users were already updated.
SDNode *SITargetLowering::PostISelFolding(MachineSDNode *Node,
                                          SelectionDAG &DAG) const {
  const SIInstrInfo *TII = getSubtarget()->getInstrInfo();
  unsigned Opcode = Node->getMachineOpcode();

  if (TII->isImage(Opcode) && !TII->get(Opcode).mayStore() &&
      !TII->isGather4(Opcode) &&
      AMDGPU::hasNamedOperand(Opcode, AMDGPU::OpName::dmask)) {
    return adjustWritemask(Node, DAG);
  }

  if (Opcode == AMDGPU::INSERT_SUBREG ||
      Opcode == AMDGPU::REG_SEQUENCE) {
    legalizeTargetIndependentNode(Node, DAG);
    return Node;
  }

  switch (Opcode) {
  case AMDGPU::V_DIV_SCALE_F32_e64:
  case AMDGPU::V_DIV_SCALE_F64_e64: {
    // Satisfy the operand register constraint when one of the inputs is
    // undefined. Ordinarily each undef value will have its own implicit_def of
    // a vreg, so force these to use a single register.
    SDValue Src0 = Node->getOperand(1);
    SDValue Src1 = Node->getOperand(3);
    SDValue Src2 = Node->getOperand(5);

    if ((Src0.isMachineOpcode() &&
         Src0.getMachineOpcode() != AMDGPU::IMPLICIT_DEF) &&
        (Src0 == Src1 || Src0 == Src2))
      break;

    MVT VT = Src0.getValueType().getSimpleVT();
    const TargetRegisterClass *RC =
        getRegClassFor(VT, Src0.getNode()->isDivergent());

    MachineRegisterInfo &MRI = DAG.getMachineFunction().getRegInfo();
    SDValue UndefReg = DAG.getRegister(MRI.createVirtualRegister(RC), VT);

    SDValue ImpDef = DAG.getCopyToReg(DAG.getEntryNode(), SDLoc(Node),
                                      UndefReg, Src0, SDValue());

    // src0 must be the same register as src1 or src2, even if the value is
    // undefined, so make sure we don't violate this constraint.
    if (Src0.isMachineOpcode() &&
        Src0.getMachineOpcode() == AMDGPU::IMPLICIT_DEF) {
      if (Src1.isMachineOpcode() &&
          Src1.getMachineOpcode() != AMDGPU::IMPLICIT_DEF)
        Src0 = Src1;
      else if (Src2.isMachineOpcode() &&
               Src2.getMachineOpcode() != AMDGPU::IMPLICIT_DEF)
        Src0 = Src2;
      else {
        assert(Src1.getMachineOpcode() == AMDGPU::IMPLICIT_DEF);
        Src0 = UndefReg;
        Src1 = UndefReg;
      }
    } else
      break;

    SmallVector<SDValue, 9> Ops(Node->op_begin(), Node->op_end());
    Ops[1] = Src0;
    Ops[3] = Src1;
    Ops[5] = Src2;
    Ops.push_back(ImpDef.getValue(1));
    return DAG.getMachineNode(Opcode, SDLoc(Node), Node->getVTList(), Ops);
  }
  default:
    break;
  }

  return Node;
}

// Any MIMG instructions that use tfe or lwe require an initialization of the
// result register that will be written in the case of a memory access failure.
// The required code is also added to tie this init code to the result of the
// img instruction.
void SITargetLowering::AddMemOpInit(MachineInstr &MI) const {
  const SIInstrInfo *TII = getSubtarget()->getInstrInfo();
  const SIRegisterInfo &TRI = TII->getRegisterInfo();
  MachineRegisterInfo &MRI = MI.getMF()->getRegInfo();
  MachineBasicBlock &MBB = *MI.getParent();

  int DstIdx =
      AMDGPU::getNamedOperandIdx(MI.getOpcode(), AMDGPU::OpName::vdata);
  unsigned InitIdx = 0;

  if (TII->isImage(MI)) {
    MachineOperand *TFE = TII->getNamedOperand(MI, AMDGPU::OpName::tfe);
    MachineOperand *LWE = TII->getNamedOperand(MI, AMDGPU::OpName::lwe);
    MachineOperand *D16 = TII->getNamedOperand(MI, AMDGPU::OpName::d16);

    if (!TFE && !LWE) // intersect_ray
      return;

    unsigned TFEVal = TFE ? TFE->getImm() : 0;
    unsigned LWEVal = LWE ? LWE->getImm() : 0;
    unsigned D16Val = D16 ? D16->getImm() : 0;

    if (!TFEVal && !LWEVal)
      return;

    // At least one of TFE or LWE are non-zero
    // We have to insert a suitable initialization of the result value and
    // tie this to the dest of the image instruction.

    // Calculate which dword we have to initialize to 0.
    MachineOperand *MO_Dmask = TII->getNamedOperand(MI, AMDGPU::OpName::dmask);

    // check that dmask operand is found.
    assert(MO_Dmask && "Expected dmask operand in instruction");

    unsigned dmask = MO_Dmask->getImm();
    // Determine the number of active lanes taking into account the
    // Gather4 special case
    unsigned ActiveLanes = TII->isGather4(MI) ? 4 : llvm::popcount(dmask);

    bool Packed = !Subtarget->hasUnpackedD16VMem();

    InitIdx = D16Val && Packed ? ((ActiveLanes + 1) >> 1) + 1 : ActiveLanes + 1;

    // Abandon attempt if the dst size isn't large enough
    // - this is in fact an error but this is picked up elsewhere and
    // reported correctly.
    uint32_t DstSize =
        TRI.getRegSizeInBits(*TII->getOpRegClass(MI, DstIdx)) / 32;
    if (DstSize < InitIdx)
      return;
  } else if (TII->isMUBUF(MI) && AMDGPU::getMUBUFTfe(MI.getOpcode())) {
    InitIdx = TRI.getRegSizeInBits(*TII->getOpRegClass(MI, DstIdx)) / 32;
  } else {
    return;
  }

  const DebugLoc &DL = MI.getDebugLoc();

  // Create a register for the initialization value.
  Register PrevDst = MRI.cloneVirtualRegister(MI.getOperand(DstIdx).getReg());
  unsigned NewDst = 0; // Final initialized value will be in here

  // If PRTStrictNull feature is enabled (the default) then initialize
  // all the result registers to 0, otherwise just the error indication
  // register (VGPRn+1)
  unsigned SizeLeft = Subtarget->usePRTStrictNull() ? InitIdx : 1;
  unsigned CurrIdx = Subtarget->usePRTStrictNull() ? 0 : (InitIdx - 1);

  BuildMI(MBB, MI, DL, TII->get(AMDGPU::IMPLICIT_DEF), PrevDst);
  for (; SizeLeft; SizeLeft--, CurrIdx++) {
    NewDst = MRI.createVirtualRegister(TII->getOpRegClass(MI, DstIdx));
    // Initialize dword
    Register SubReg = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    BuildMI(MBB, MI, DL, TII->get(AMDGPU::V_MOV_B32_e32), SubReg)
      .addImm(0);
    // Insert into the super-reg
    BuildMI(MBB, MI, DL, TII->get(TargetOpcode::INSERT_SUBREG), NewDst)
      .addReg(PrevDst)
      .addReg(SubReg)
      .addImm(SIRegisterInfo::getSubRegFromChannel(CurrIdx));

    PrevDst = NewDst;
  }

  // Add as an implicit operand
  MI.addOperand(MachineOperand::CreateReg(NewDst, false, true));

  // Tie the just added implicit operand to the dst
  MI.tieOperands(DstIdx, MI.getNumOperands() - 1);
}

/// Assign the register class depending on the number of
/// bits set in the writemask
void SITargetLowering::AdjustInstrPostInstrSelection(MachineInstr &MI,
                                                     SDNode *Node) const {
  const SIInstrInfo *TII = getSubtarget()->getInstrInfo();

  MachineFunction *MF = MI.getParent()->getParent();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  SIMachineFunctionInfo *Info = MF->getInfo<SIMachineFunctionInfo>();

  if (TII->isVOP3(MI.getOpcode())) {
    // Make sure constant bus requirements are respected.
    TII->legalizeOperandsVOP3(MRI, MI);

    // Prefer VGPRs over AGPRs in mAI instructions where possible.
    // This saves a chain-copy of registers and better balance register
    // use between vgpr and agpr as agpr tuples tend to be big.
    if (!MI.getDesc().operands().empty()) {
      unsigned Opc = MI.getOpcode();
      bool HasAGPRs = Info->mayNeedAGPRs();
      const SIRegisterInfo *TRI = Subtarget->getRegisterInfo();
      int16_t Src2Idx = AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src2);
      for (auto I :
           {AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src0),
            AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::src1), Src2Idx}) {
        if (I == -1)
          break;
        if ((I == Src2Idx) && (HasAGPRs))
          break;
        MachineOperand &Op = MI.getOperand(I);
        if (!Op.isReg() || !Op.getReg().isVirtual())
          continue;
        auto *RC = TRI->getRegClassForReg(MRI, Op.getReg());
        if (!TRI->hasAGPRs(RC))
          continue;
        auto *Src = MRI.getUniqueVRegDef(Op.getReg());
        if (!Src || !Src->isCopy() ||
            !TRI->isSGPRReg(MRI, Src->getOperand(1).getReg()))
          continue;
        auto *NewRC = TRI->getEquivalentVGPRClass(RC);
        // All uses of agpr64 and agpr32 can also accept vgpr except for
        // v_accvgpr_read, but we do not produce agpr reads during selection,
        // so no use checks are needed.
        MRI.setRegClass(Op.getReg(), NewRC);
      }

      if (!HasAGPRs)
        return;

      // Resolve the rest of AV operands to AGPRs.
      if (auto *Src2 = TII->getNamedOperand(MI, AMDGPU::OpName::src2)) {
        if (Src2->isReg() && Src2->getReg().isVirtual()) {
          auto *RC = TRI->getRegClassForReg(MRI, Src2->getReg());
          if (TRI->isVectorSuperClass(RC)) {
            auto *NewRC = TRI->getEquivalentAGPRClass(RC);
            MRI.setRegClass(Src2->getReg(), NewRC);
            if (Src2->isTied())
              MRI.setRegClass(MI.getOperand(0).getReg(), NewRC);
          }
        }
      }
    }

    return;
  }

  if (TII->isImage(MI))
    TII->enforceOperandRCAlignment(MI, AMDGPU::OpName::vaddr);
}

static SDValue buildSMovImm32(SelectionDAG &DAG, const SDLoc &DL,
                              uint64_t Val) {
  SDValue K = DAG.getTargetConstant(Val, DL, MVT::i32);
  return SDValue(DAG.getMachineNode(AMDGPU::S_MOV_B32, DL, MVT::i32, K), 0);
}

MachineSDNode *SITargetLowering::wrapAddr64Rsrc(SelectionDAG &DAG,
                                                const SDLoc &DL,
                                                SDValue Ptr) const {
  const SIInstrInfo *TII = getSubtarget()->getInstrInfo();

  // Build the half of the subregister with the constants before building the
  // full 128-bit register. If we are building multiple resource descriptors,
  // this will allow CSEing of the 2-component register.
  const SDValue Ops0[] = {
    DAG.getTargetConstant(AMDGPU::SGPR_64RegClassID, DL, MVT::i32),
    buildSMovImm32(DAG, DL, 0),
    DAG.getTargetConstant(AMDGPU::sub0, DL, MVT::i32),
    buildSMovImm32(DAG, DL, TII->getDefaultRsrcDataFormat() >> 32),
    DAG.getTargetConstant(AMDGPU::sub1, DL, MVT::i32)
  };

  SDValue SubRegHi = SDValue(DAG.getMachineNode(AMDGPU::REG_SEQUENCE, DL,
                                                MVT::v2i32, Ops0), 0);

  // Combine the constants and the pointer.
  const SDValue Ops1[] = {
    DAG.getTargetConstant(AMDGPU::SGPR_128RegClassID, DL, MVT::i32),
    Ptr,
    DAG.getTargetConstant(AMDGPU::sub0_sub1, DL, MVT::i32),
    SubRegHi,
    DAG.getTargetConstant(AMDGPU::sub2_sub3, DL, MVT::i32)
  };

  return DAG.getMachineNode(AMDGPU::REG_SEQUENCE, DL, MVT::v4i32, Ops1);
}

/// Return a resource descriptor with the 'Add TID' bit enabled
///        The TID (Thread ID) is multiplied by the stride value (bits [61:48]
///        of the resource descriptor) to create an offset, which is added to
///        the resource pointer.
MachineSDNode *SITargetLowering::buildRSRC(SelectionDAG &DAG, const SDLoc &DL,
                                           SDValue Ptr, uint32_t RsrcDword1,
                                           uint64_t RsrcDword2And3) const {
  SDValue PtrLo = DAG.getTargetExtractSubreg(AMDGPU::sub0, DL, MVT::i32, Ptr);
  SDValue PtrHi = DAG.getTargetExtractSubreg(AMDGPU::sub1, DL, MVT::i32, Ptr);
  if (RsrcDword1) {
    PtrHi = SDValue(DAG.getMachineNode(AMDGPU::S_OR_B32, DL, MVT::i32, PtrHi,
                                     DAG.getConstant(RsrcDword1, DL, MVT::i32)),
                    0);
  }

  SDValue DataLo = buildSMovImm32(DAG, DL,
                                  RsrcDword2And3 & UINT64_C(0xFFFFFFFF));
  SDValue DataHi = buildSMovImm32(DAG, DL, RsrcDword2And3 >> 32);

  const SDValue Ops[] = {
    DAG.getTargetConstant(AMDGPU::SGPR_128RegClassID, DL, MVT::i32),
    PtrLo,
    DAG.getTargetConstant(AMDGPU::sub0, DL, MVT::i32),
    PtrHi,
    DAG.getTargetConstant(AMDGPU::sub1, DL, MVT::i32),
    DataLo,
    DAG.getTargetConstant(AMDGPU::sub2, DL, MVT::i32),
    DataHi,
    DAG.getTargetConstant(AMDGPU::sub3, DL, MVT::i32)
  };

  return DAG.getMachineNode(AMDGPU::REG_SEQUENCE, DL, MVT::v4i32, Ops);
}

//===----------------------------------------------------------------------===//
//                         SI Inline Assembly Support
//===----------------------------------------------------------------------===//

std::pair<unsigned, const TargetRegisterClass *>
SITargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI_,
                                               StringRef Constraint,
                                               MVT VT) const {
  const SIRegisterInfo *TRI = static_cast<const SIRegisterInfo *>(TRI_);

  const TargetRegisterClass *RC = nullptr;
  if (Constraint.size() == 1) {
    const unsigned BitWidth = VT.getSizeInBits();
    switch (Constraint[0]) {
    default:
      return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
    case 's':
    case 'r':
      switch (BitWidth) {
      case 16:
        RC = &AMDGPU::SReg_32RegClass;
        break;
      case 64:
        RC = &AMDGPU::SGPR_64RegClass;
        break;
      default:
        RC = SIRegisterInfo::getSGPRClassForBitWidth(BitWidth);
        if (!RC)
          return std::pair(0U, nullptr);
        break;
      }
      break;
    case 'v':
      switch (BitWidth) {
      case 16:
        RC = &AMDGPU::VGPR_32RegClass;
        break;
      default:
        RC = TRI->getVGPRClassForBitWidth(BitWidth);
        if (!RC)
          return std::pair(0U, nullptr);
        break;
      }
      break;
    case 'a':
      if (!Subtarget->hasMAIInsts())
        break;
      switch (BitWidth) {
      case 16:
        RC = &AMDGPU::AGPR_32RegClass;
        break;
      default:
        RC = TRI->getAGPRClassForBitWidth(BitWidth);
        if (!RC)
          return std::pair(0U, nullptr);
        break;
      }
      break;
    }
    // We actually support i128, i16 and f16 as inline parameters
    // even if they are not reported as legal
    if (RC && (isTypeLegal(VT) || VT.SimpleTy == MVT::i128 ||
               VT.SimpleTy == MVT::i16 || VT.SimpleTy == MVT::f16))
      return std::pair(0U, RC);
  }

  if (Constraint.starts_with("{") && Constraint.ends_with("}")) {
    StringRef RegName(Constraint.data() + 1, Constraint.size() - 2);
    if (RegName.consume_front("v")) {
      RC = &AMDGPU::VGPR_32RegClass;
    } else if (RegName.consume_front("s")) {
      RC = &AMDGPU::SGPR_32RegClass;
    } else if (RegName.consume_front("a")) {
      RC = &AMDGPU::AGPR_32RegClass;
    }

    if (RC) {
      uint32_t Idx;
      if (RegName.consume_front("[")) {
        uint32_t End;
        bool Failed = RegName.consumeInteger(10, Idx);
        Failed |= !RegName.consume_front(":");
        Failed |= RegName.consumeInteger(10, End);
        Failed |= !RegName.consume_back("]");
        if (!Failed) {
          uint32_t Width = (End - Idx + 1) * 32;
          MCRegister Reg = RC->getRegister(Idx);
          if (SIRegisterInfo::isVGPRClass(RC))
            RC = TRI->getVGPRClassForBitWidth(Width);
          else if (SIRegisterInfo::isSGPRClass(RC))
            RC = TRI->getSGPRClassForBitWidth(Width);
          else if (SIRegisterInfo::isAGPRClass(RC))
            RC = TRI->getAGPRClassForBitWidth(Width);
          if (RC) {
            Reg = TRI->getMatchingSuperReg(Reg, AMDGPU::sub0, RC);
            return std::pair(Reg, RC);
          }
        }
      } else {
        bool Failed = RegName.getAsInteger(10, Idx);
        if (!Failed && Idx < RC->getNumRegs())
          return std::pair(RC->getRegister(Idx), RC);
      }
    }
  }

  auto Ret = TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
  if (Ret.first)
    Ret.second = TRI->getPhysRegBaseClass(Ret.first);

  return Ret;
}

static bool isImmConstraint(StringRef Constraint) {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default: break;
    case 'I':
    case 'J':
    case 'A':
    case 'B':
    case 'C':
      return true;
    }
  } else if (Constraint == "DA" ||
             Constraint == "DB") {
    return true;
  }
  return false;
}

SITargetLowering::ConstraintType
SITargetLowering::getConstraintType(StringRef Constraint) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    default: break;
    case 's':
    case 'v':
    case 'a':
      return C_RegisterClass;
    }
  }
  if (isImmConstraint(Constraint)) {
    return C_Other;
  }
  return TargetLowering::getConstraintType(Constraint);
}

static uint64_t clearUnusedBits(uint64_t Val, unsigned Size) {
  if (!AMDGPU::isInlinableIntLiteral(Val)) {
    Val = Val & maskTrailingOnes<uint64_t>(Size);
  }
  return Val;
}

void SITargetLowering::LowerAsmOperandForConstraint(SDValue Op,
                                                    StringRef Constraint,
                                                    std::vector<SDValue> &Ops,
                                                    SelectionDAG &DAG) const {
  if (isImmConstraint(Constraint)) {
    uint64_t Val;
    if (getAsmOperandConstVal(Op, Val) &&
        checkAsmConstraintVal(Op, Constraint, Val)) {
      Val = clearUnusedBits(Val, Op.getScalarValueSizeInBits());
      Ops.push_back(DAG.getTargetConstant(Val, SDLoc(Op), MVT::i64));
    }
  } else {
    TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops, DAG);
  }
}

bool SITargetLowering::getAsmOperandConstVal(SDValue Op, uint64_t &Val) const {
  unsigned Size = Op.getScalarValueSizeInBits();
  if (Size > 64)
    return false;

  if (Size == 16 && !Subtarget->has16BitInsts())
    return false;

  if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Op)) {
    Val = C->getSExtValue();
    return true;
  }
  if (ConstantFPSDNode *C = dyn_cast<ConstantFPSDNode>(Op)) {
    Val = C->getValueAPF().bitcastToAPInt().getSExtValue();
    return true;
  }
  if (BuildVectorSDNode *V = dyn_cast<BuildVectorSDNode>(Op)) {
    if (Size != 16 || Op.getNumOperands() != 2)
      return false;
    if (Op.getOperand(0).isUndef() || Op.getOperand(1).isUndef())
      return false;
    if (ConstantSDNode *C = V->getConstantSplatNode()) {
      Val = C->getSExtValue();
      return true;
    }
    if (ConstantFPSDNode *C = V->getConstantFPSplatNode()) {
      Val = C->getValueAPF().bitcastToAPInt().getSExtValue();
      return true;
    }
  }

  return false;
}

bool SITargetLowering::checkAsmConstraintVal(SDValue Op, StringRef Constraint,
                                             uint64_t Val) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'I':
      return AMDGPU::isInlinableIntLiteral(Val);
    case 'J':
      return isInt<16>(Val);
    case 'A':
      return checkAsmConstraintValA(Op, Val);
    case 'B':
      return isInt<32>(Val);
    case 'C':
      return isUInt<32>(clearUnusedBits(Val, Op.getScalarValueSizeInBits())) ||
             AMDGPU::isInlinableIntLiteral(Val);
    default:
      break;
    }
  } else if (Constraint.size() == 2) {
    if (Constraint == "DA") {
      int64_t HiBits = static_cast<int32_t>(Val >> 32);
      int64_t LoBits = static_cast<int32_t>(Val);
      return checkAsmConstraintValA(Op, HiBits, 32) &&
             checkAsmConstraintValA(Op, LoBits, 32);
    }
    if (Constraint == "DB") {
      return true;
    }
  }
  llvm_unreachable("Invalid asm constraint");
}

bool SITargetLowering::checkAsmConstraintValA(SDValue Op, uint64_t Val,
                                              unsigned MaxSize) const {
  unsigned Size = std::min<unsigned>(Op.getScalarValueSizeInBits(), MaxSize);
  bool HasInv2Pi = Subtarget->hasInv2PiInlineImm();
  if (Size == 16) {
    MVT VT = Op.getSimpleValueType();
    switch (VT.SimpleTy) {
    default:
      return false;
    case MVT::i16:
      return AMDGPU::isInlinableLiteralI16(Val, HasInv2Pi);
    case MVT::f16:
      return AMDGPU::isInlinableLiteralFP16(Val, HasInv2Pi);
    case MVT::bf16:
      return AMDGPU::isInlinableLiteralBF16(Val, HasInv2Pi);
    case MVT::v2i16:
      return AMDGPU::getInlineEncodingV2I16(Val).has_value();
    case MVT::v2f16:
      return AMDGPU::getInlineEncodingV2F16(Val).has_value();
    case MVT::v2bf16:
      return AMDGPU::getInlineEncodingV2BF16(Val).has_value();
    }
  }
  if ((Size == 32 && AMDGPU::isInlinableLiteral32(Val, HasInv2Pi)) ||
      (Size == 64 && AMDGPU::isInlinableLiteral64(Val, HasInv2Pi)))
    return true;
  return false;
}

static int getAlignedAGPRClassID(unsigned UnalignedClassID) {
  switch (UnalignedClassID) {
  case AMDGPU::VReg_64RegClassID:
    return AMDGPU::VReg_64_Align2RegClassID;
  case AMDGPU::VReg_96RegClassID:
    return AMDGPU::VReg_96_Align2RegClassID;
  case AMDGPU::VReg_128RegClassID:
    return AMDGPU::VReg_128_Align2RegClassID;
  case AMDGPU::VReg_160RegClassID:
    return AMDGPU::VReg_160_Align2RegClassID;
  case AMDGPU::VReg_192RegClassID:
    return AMDGPU::VReg_192_Align2RegClassID;
  case AMDGPU::VReg_224RegClassID:
    return AMDGPU::VReg_224_Align2RegClassID;
  case AMDGPU::VReg_256RegClassID:
    return AMDGPU::VReg_256_Align2RegClassID;
  case AMDGPU::VReg_288RegClassID:
    return AMDGPU::VReg_288_Align2RegClassID;
  case AMDGPU::VReg_320RegClassID:
    return AMDGPU::VReg_320_Align2RegClassID;
  case AMDGPU::VReg_352RegClassID:
    return AMDGPU::VReg_352_Align2RegClassID;
  case AMDGPU::VReg_384RegClassID:
    return AMDGPU::VReg_384_Align2RegClassID;
  case AMDGPU::VReg_512RegClassID:
    return AMDGPU::VReg_512_Align2RegClassID;
  case AMDGPU::VReg_1024RegClassID:
    return AMDGPU::VReg_1024_Align2RegClassID;
  case AMDGPU::AReg_64RegClassID:
    return AMDGPU::AReg_64_Align2RegClassID;
  case AMDGPU::AReg_96RegClassID:
    return AMDGPU::AReg_96_Align2RegClassID;
  case AMDGPU::AReg_128RegClassID:
    return AMDGPU::AReg_128_Align2RegClassID;
  case AMDGPU::AReg_160RegClassID:
    return AMDGPU::AReg_160_Align2RegClassID;
  case AMDGPU::AReg_192RegClassID:
    return AMDGPU::AReg_192_Align2RegClassID;
  case AMDGPU::AReg_256RegClassID:
    return AMDGPU::AReg_256_Align2RegClassID;
  case AMDGPU::AReg_512RegClassID:
    return AMDGPU::AReg_512_Align2RegClassID;
  case AMDGPU::AReg_1024RegClassID:
    return AMDGPU::AReg_1024_Align2RegClassID;
  default:
    return -1;
  }
}

// Figure out which registers should be reserved for stack access. Only after
// the function is legalized do we know all of the non-spill stack objects or if
// calls are present.
void SITargetLowering::finalizeLowering(MachineFunction &MF) const {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *TRI = Subtarget->getRegisterInfo();
  const SIInstrInfo *TII = ST.getInstrInfo();

  if (Info->isEntryFunction()) {
    // Callable functions have fixed registers used for stack access.
    reservePrivateMemoryRegs(getTargetMachine(), MF, *TRI, *Info);
  }

  // TODO: Move this logic to getReservedRegs()
  // Reserve the SGPR(s) to save/restore EXEC for WWM spill/copy handling.
  unsigned MaxNumSGPRs = ST.getMaxNumSGPRs(MF);
  Register SReg = ST.isWave32()
                      ? AMDGPU::SGPR_32RegClass.getRegister(MaxNumSGPRs - 1)
                      : TRI->getAlignedHighSGPRForRC(MF, /*Align=*/2,
                                                     &AMDGPU::SGPR_64RegClass);
  Info->setSGPRForEXECCopy(SReg);

  assert(!TRI->isSubRegister(Info->getScratchRSrcReg(),
                             Info->getStackPtrOffsetReg()));
  if (Info->getStackPtrOffsetReg() != AMDGPU::SP_REG)
    MRI.replaceRegWith(AMDGPU::SP_REG, Info->getStackPtrOffsetReg());

  // We need to worry about replacing the default register with itself in case
  // of MIR testcases missing the MFI.
  if (Info->getScratchRSrcReg() != AMDGPU::PRIVATE_RSRC_REG)
    MRI.replaceRegWith(AMDGPU::PRIVATE_RSRC_REG, Info->getScratchRSrcReg());

  if (Info->getFrameOffsetReg() != AMDGPU::FP_REG)
    MRI.replaceRegWith(AMDGPU::FP_REG, Info->getFrameOffsetReg());

  Info->limitOccupancy(MF);

  if (ST.isWave32() && !MF.empty()) {
    for (auto &MBB : MF) {
      for (auto &MI : MBB) {
        TII->fixImplicitOperands(MI);
      }
    }
  }

  // FIXME: This is a hack to fixup AGPR classes to use the properly aligned
  // classes if required. Ideally the register class constraints would differ
  // per-subtarget, but there's no easy way to achieve that right now. This is
  // not a problem for VGPRs because the correctly aligned VGPR class is implied
  // from using them as the register class for legal types.
  if (ST.needsAlignedVGPRs()) {
    for (unsigned I = 0, E = MRI.getNumVirtRegs(); I != E; ++I) {
      const Register Reg = Register::index2VirtReg(I);
      const TargetRegisterClass *RC = MRI.getRegClassOrNull(Reg);
      if (!RC)
        continue;
      int NewClassID = getAlignedAGPRClassID(RC->getID());
      if (NewClassID != -1)
        MRI.setRegClass(Reg, TRI->getRegClass(NewClassID));
    }
  }

  TargetLoweringBase::finalizeLowering(MF);
}

void SITargetLowering::computeKnownBitsForTargetNode(const SDValue Op,
                                                     KnownBits &Known,
                                                     const APInt &DemandedElts,
                                                     const SelectionDAG &DAG,
                                                     unsigned Depth) const {
  Known.resetAll();
  unsigned Opc = Op.getOpcode();
  switch (Opc) {
  case ISD::INTRINSIC_WO_CHAIN: {
    unsigned IID = Op.getConstantOperandVal(0);
    switch (IID) {
    case Intrinsic::amdgcn_mbcnt_lo:
    case Intrinsic::amdgcn_mbcnt_hi: {
      const GCNSubtarget &ST =
          DAG.getMachineFunction().getSubtarget<GCNSubtarget>();
      // These return at most the (wavefront size - 1) + src1
      // As long as src1 is an immediate we can calc known bits
      KnownBits Src1Known = DAG.computeKnownBits(Op.getOperand(2), Depth + 1);
      unsigned Src1ValBits = Src1Known.countMaxActiveBits();
      unsigned MaxActiveBits = std::max(Src1ValBits, ST.getWavefrontSizeLog2());
      // Cater for potential carry
      MaxActiveBits += Src1ValBits ? 1 : 0;
      unsigned Size = Op.getValueType().getSizeInBits();
      if (MaxActiveBits < Size)
        Known.Zero.setHighBits(Size - MaxActiveBits);
      return;
    }
    }
    break;
  }
  }
  return AMDGPUTargetLowering::computeKnownBitsForTargetNode(
      Op, Known, DemandedElts, DAG, Depth);
}

void SITargetLowering::computeKnownBitsForFrameIndex(
  const int FI, KnownBits &Known, const MachineFunction &MF) const {
  TargetLowering::computeKnownBitsForFrameIndex(FI, Known, MF);

  // Set the high bits to zero based on the maximum allowed scratch size per
  // wave. We can't use vaddr in MUBUF instructions if we don't know the address
  // calculation won't overflow, so assume the sign bit is never set.
  Known.Zero.setHighBits(getSubtarget()->getKnownHighZeroBitsForFrameIndex());
}

static void knownBitsForWorkitemID(const GCNSubtarget &ST, GISelKnownBits &KB,
                                   KnownBits &Known, unsigned Dim) {
  unsigned MaxValue =
      ST.getMaxWorkitemID(KB.getMachineFunction().getFunction(), Dim);
  Known.Zero.setHighBits(llvm::countl_zero(MaxValue));
}

void SITargetLowering::computeKnownBitsForTargetInstr(
    GISelKnownBits &KB, Register R, KnownBits &Known, const APInt &DemandedElts,
    const MachineRegisterInfo &MRI, unsigned Depth) const {
  const MachineInstr *MI = MRI.getVRegDef(R);
  switch (MI->getOpcode()) {
  case AMDGPU::G_INTRINSIC:
  case AMDGPU::G_INTRINSIC_CONVERGENT: {
    switch (cast<GIntrinsic>(MI)->getIntrinsicID()) {
    case Intrinsic::amdgcn_workitem_id_x:
      knownBitsForWorkitemID(*getSubtarget(), KB, Known, 0);
      break;
    case Intrinsic::amdgcn_workitem_id_y:
      knownBitsForWorkitemID(*getSubtarget(), KB, Known, 1);
      break;
    case Intrinsic::amdgcn_workitem_id_z:
      knownBitsForWorkitemID(*getSubtarget(), KB, Known, 2);
      break;
    case Intrinsic::amdgcn_mbcnt_lo:
    case Intrinsic::amdgcn_mbcnt_hi: {
      // These return at most the wavefront size - 1.
      unsigned Size = MRI.getType(R).getSizeInBits();
      Known.Zero.setHighBits(Size - getSubtarget()->getWavefrontSizeLog2());
      break;
    }
    case Intrinsic::amdgcn_groupstaticsize: {
      // We can report everything over the maximum size as 0. We can't report
      // based on the actual size because we don't know if it's accurate or not
      // at any given point.
      Known.Zero.setHighBits(
          llvm::countl_zero(getSubtarget()->getAddressableLocalMemorySize()));
      break;
    }
    }
    break;
  }
  case AMDGPU::G_AMDGPU_BUFFER_LOAD_UBYTE:
    Known.Zero.setHighBits(24);
    break;
  case AMDGPU::G_AMDGPU_BUFFER_LOAD_USHORT:
    Known.Zero.setHighBits(16);
    break;
  case AMDGPU::G_AMDGPU_SMED3:
  case AMDGPU::G_AMDGPU_UMED3: {
    auto [Dst, Src0, Src1, Src2] = MI->getFirst4Regs();

    KnownBits Known2;
    KB.computeKnownBitsImpl(Src2, Known2, DemandedElts, Depth + 1);
    if (Known2.isUnknown())
      break;

    KnownBits Known1;
    KB.computeKnownBitsImpl(Src1, Known1, DemandedElts, Depth + 1);
    if (Known1.isUnknown())
      break;

    KnownBits Known0;
    KB.computeKnownBitsImpl(Src0, Known0, DemandedElts, Depth + 1);
    if (Known0.isUnknown())
      break;

    // TODO: Handle LeadZero/LeadOne from UMIN/UMAX handling.
    Known.Zero = Known0.Zero & Known1.Zero & Known2.Zero;
    Known.One = Known0.One & Known1.One & Known2.One;
    break;
  }
  }
}

Align SITargetLowering::computeKnownAlignForTargetInstr(
  GISelKnownBits &KB, Register R, const MachineRegisterInfo &MRI,
  unsigned Depth) const {
  const MachineInstr *MI = MRI.getVRegDef(R);
  if (auto *GI = dyn_cast<GIntrinsic>(MI)) {
    // FIXME: Can this move to generic code? What about the case where the call
    // site specifies a lower alignment?
    Intrinsic::ID IID = GI->getIntrinsicID();
    LLVMContext &Ctx = KB.getMachineFunction().getFunction().getContext();
    AttributeList Attrs = Intrinsic::getAttributes(Ctx, IID);
    if (MaybeAlign RetAlign = Attrs.getRetAlignment())
      return *RetAlign;
  }
  return Align(1);
}

Align SITargetLowering::getPrefLoopAlignment(MachineLoop *ML) const {
  const Align PrefAlign = TargetLowering::getPrefLoopAlignment(ML);
  const Align CacheLineAlign = Align(64);

  // Pre-GFX10 target did not benefit from loop alignment
  if (!ML || DisableLoopAlignment || !getSubtarget()->hasInstPrefetch() ||
      getSubtarget()->hasInstFwdPrefetchBug())
    return PrefAlign;

  // On GFX10 I$ is 4 x 64 bytes cache lines.
  // By default prefetcher keeps one cache line behind and reads two ahead.
  // We can modify it with S_INST_PREFETCH for larger loops to have two lines
  // behind and one ahead.
  // Therefor we can benefit from aligning loop headers if loop fits 192 bytes.
  // If loop fits 64 bytes it always spans no more than two cache lines and
  // does not need an alignment.
  // Else if loop is less or equal 128 bytes we do not need to modify prefetch,
  // Else if loop is less or equal 192 bytes we need two lines behind.

  const SIInstrInfo *TII = getSubtarget()->getInstrInfo();
  const MachineBasicBlock *Header = ML->getHeader();
  if (Header->getAlignment() != PrefAlign)
    return Header->getAlignment(); // Already processed.

  unsigned LoopSize = 0;
  for (const MachineBasicBlock *MBB : ML->blocks()) {
    // If inner loop block is aligned assume in average half of the alignment
    // size to be added as nops.
    if (MBB != Header)
      LoopSize += MBB->getAlignment().value() / 2;

    for (const MachineInstr &MI : *MBB) {
      LoopSize += TII->getInstSizeInBytes(MI);
      if (LoopSize > 192)
        return PrefAlign;
    }
  }

  if (LoopSize <= 64)
    return PrefAlign;

  if (LoopSize <= 128)
    return CacheLineAlign;

  // If any of parent loops is surrounded by prefetch instructions do not
  // insert new for inner loop, which would reset parent's settings.
  for (MachineLoop *P = ML->getParentLoop(); P; P = P->getParentLoop()) {
    if (MachineBasicBlock *Exit = P->getExitBlock()) {
      auto I = Exit->getFirstNonDebugInstr();
      if (I != Exit->end() && I->getOpcode() == AMDGPU::S_INST_PREFETCH)
        return CacheLineAlign;
    }
  }

  MachineBasicBlock *Pre = ML->getLoopPreheader();
  MachineBasicBlock *Exit = ML->getExitBlock();

  if (Pre && Exit) {
    auto PreTerm = Pre->getFirstTerminator();
    if (PreTerm == Pre->begin() ||
        std::prev(PreTerm)->getOpcode() != AMDGPU::S_INST_PREFETCH)
      BuildMI(*Pre, PreTerm, DebugLoc(), TII->get(AMDGPU::S_INST_PREFETCH))
          .addImm(1); // prefetch 2 lines behind PC

    auto ExitHead = Exit->getFirstNonDebugInstr();
    if (ExitHead == Exit->end() ||
        ExitHead->getOpcode() != AMDGPU::S_INST_PREFETCH)
      BuildMI(*Exit, ExitHead, DebugLoc(), TII->get(AMDGPU::S_INST_PREFETCH))
          .addImm(2); // prefetch 1 line behind PC
  }

  return CacheLineAlign;
}

LLVM_ATTRIBUTE_UNUSED
static bool isCopyFromRegOfInlineAsm(const SDNode *N) {
  assert(N->getOpcode() == ISD::CopyFromReg);
  do {
    // Follow the chain until we find an INLINEASM node.
    N = N->getOperand(0).getNode();
    if (N->getOpcode() == ISD::INLINEASM ||
        N->getOpcode() == ISD::INLINEASM_BR)
      return true;
  } while (N->getOpcode() == ISD::CopyFromReg);
  return false;
}

bool SITargetLowering::isSDNodeSourceOfDivergence(const SDNode *N,
                                                  FunctionLoweringInfo *FLI,
                                                  UniformityInfo *UA) const {
  switch (N->getOpcode()) {
  case ISD::CopyFromReg: {
    const RegisterSDNode *R = cast<RegisterSDNode>(N->getOperand(1));
    const MachineRegisterInfo &MRI = FLI->MF->getRegInfo();
    const SIRegisterInfo *TRI = Subtarget->getRegisterInfo();
    Register Reg = R->getReg();

    // FIXME: Why does this need to consider isLiveIn?
    if (Reg.isPhysical() || MRI.isLiveIn(Reg))
      return !TRI->isSGPRReg(MRI, Reg);

    if (const Value *V = FLI->getValueFromVirtualReg(R->getReg()))
      return UA->isDivergent(V);

    assert(Reg == FLI->DemoteRegister || isCopyFromRegOfInlineAsm(N));
    return !TRI->isSGPRReg(MRI, Reg);
  }
  case ISD::LOAD: {
    const LoadSDNode *L = cast<LoadSDNode>(N);
    unsigned AS = L->getAddressSpace();
    // A flat load may access private memory.
    return AS == AMDGPUAS::PRIVATE_ADDRESS || AS == AMDGPUAS::FLAT_ADDRESS;
  }
  case ISD::CALLSEQ_END:
    return true;
  case ISD::INTRINSIC_WO_CHAIN:
    return AMDGPU::isIntrinsicSourceOfDivergence(N->getConstantOperandVal(0));
  case ISD::INTRINSIC_W_CHAIN:
    return AMDGPU::isIntrinsicSourceOfDivergence(N->getConstantOperandVal(1));
  case AMDGPUISD::ATOMIC_CMP_SWAP:
  case AMDGPUISD::BUFFER_ATOMIC_SWAP:
  case AMDGPUISD::BUFFER_ATOMIC_ADD:
  case AMDGPUISD::BUFFER_ATOMIC_SUB:
  case AMDGPUISD::BUFFER_ATOMIC_SMIN:
  case AMDGPUISD::BUFFER_ATOMIC_UMIN:
  case AMDGPUISD::BUFFER_ATOMIC_SMAX:
  case AMDGPUISD::BUFFER_ATOMIC_UMAX:
  case AMDGPUISD::BUFFER_ATOMIC_AND:
  case AMDGPUISD::BUFFER_ATOMIC_OR:
  case AMDGPUISD::BUFFER_ATOMIC_XOR:
  case AMDGPUISD::BUFFER_ATOMIC_INC:
  case AMDGPUISD::BUFFER_ATOMIC_DEC:
  case AMDGPUISD::BUFFER_ATOMIC_CMPSWAP:
  case AMDGPUISD::BUFFER_ATOMIC_CSUB:
  case AMDGPUISD::BUFFER_ATOMIC_FADD:
  case AMDGPUISD::BUFFER_ATOMIC_FMIN:
  case AMDGPUISD::BUFFER_ATOMIC_FMAX:
    // Target-specific read-modify-write atomics are sources of divergence.
    return true;
  default:
    if (auto *A = dyn_cast<AtomicSDNode>(N)) {
      // Generic read-modify-write atomics are sources of divergence.
      return A->readMem() && A->writeMem();
    }
    return false;
  }
}

bool SITargetLowering::denormalsEnabledForType(const SelectionDAG &DAG,
                                               EVT VT) const {
  switch (VT.getScalarType().getSimpleVT().SimpleTy) {
  case MVT::f32:
    return !denormalModeIsFlushAllF32(DAG.getMachineFunction());
  case MVT::f64:
  case MVT::f16:
    return !denormalModeIsFlushAllF64F16(DAG.getMachineFunction());
  default:
    return false;
  }
}

bool SITargetLowering::denormalsEnabledForType(
    LLT Ty, const MachineFunction &MF) const {
  switch (Ty.getScalarSizeInBits()) {
  case 32:
    return !denormalModeIsFlushAllF32(MF);
  case 64:
  case 16:
    return !denormalModeIsFlushAllF64F16(MF);
  default:
    return false;
  }
}

bool SITargetLowering::isKnownNeverNaNForTargetNode(SDValue Op,
                                                    const SelectionDAG &DAG,
                                                    bool SNaN,
                                                    unsigned Depth) const {
  if (Op.getOpcode() == AMDGPUISD::CLAMP) {
    const MachineFunction &MF = DAG.getMachineFunction();
    const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();

    if (Info->getMode().DX10Clamp)
      return true; // Clamped to 0.
    return DAG.isKnownNeverNaN(Op.getOperand(0), SNaN, Depth + 1);
  }

  return AMDGPUTargetLowering::isKnownNeverNaNForTargetNode(Op, DAG,
                                                            SNaN, Depth);
}

#if 0
// FIXME: This should be checked before unsafe fp atomics are enabled
// Global FP atomic instructions have a hardcoded FP mode and do not support
// FP32 denormals, and only support v2f16 denormals.
static bool fpModeMatchesGlobalFPAtomicMode(const AtomicRMWInst *RMW) {
  const fltSemantics &Flt = RMW->getType()->getScalarType()->getFltSemantics();
  auto DenormMode = RMW->getParent()->getParent()->getDenormalMode(Flt);
  if (&Flt == &APFloat::IEEEsingle())
    return DenormMode == DenormalMode::getPreserveSign();
  return DenormMode == DenormalMode::getIEEE();
}
#endif

// The amdgpu-unsafe-fp-atomics attribute enables generation of unsafe
// floating point atomic instructions. May generate more efficient code,
// but may not respect rounding and denormal modes, and may give incorrect
// results for certain memory destinations.
bool unsafeFPAtomicsDisabled(Function *F) {
  return F->getFnAttribute("amdgpu-unsafe-fp-atomics").getValueAsString() !=
         "true";
}

static OptimizationRemark emitAtomicRMWLegalRemark(const AtomicRMWInst *RMW) {
  LLVMContext &Ctx = RMW->getContext();
  SmallVector<StringRef> SSNs;
  Ctx.getSyncScopeNames(SSNs);
  StringRef MemScope = SSNs[RMW->getSyncScopeID()].empty()
                           ? "system"
                           : SSNs[RMW->getSyncScopeID()];

  return OptimizationRemark(DEBUG_TYPE, "Passed", RMW)
         << "Hardware instruction generated for atomic "
         << RMW->getOperationName(RMW->getOperation())
         << " operation at memory scope " << MemScope;
}

static bool isHalf2OrBFloat2(Type *Ty) {
  if (auto *VT = dyn_cast<FixedVectorType>(Ty)) {
    Type *EltTy = VT->getElementType();
    return VT->getNumElements() == 2 &&
           (EltTy->isHalfTy() || EltTy->isBFloatTy());
  }

  return false;
}

static bool isHalf2(Type *Ty) {
  FixedVectorType *VT = dyn_cast<FixedVectorType>(Ty);
  return VT && VT->getNumElements() == 2 && VT->getElementType()->isHalfTy();
}

static bool isBFloat2(Type *Ty) {
  FixedVectorType *VT = dyn_cast<FixedVectorType>(Ty);
  return VT && VT->getNumElements() == 2 && VT->getElementType()->isBFloatTy();
}

TargetLowering::AtomicExpansionKind
SITargetLowering::shouldExpandAtomicRMWInIR(AtomicRMWInst *RMW) const {
  unsigned AS = RMW->getPointerAddressSpace();
  if (AS == AMDGPUAS::PRIVATE_ADDRESS)
    return AtomicExpansionKind::NotAtomic;

  auto ReportUnsafeHWInst = [=](TargetLowering::AtomicExpansionKind Kind) {
    OptimizationRemarkEmitter ORE(RMW->getFunction());
    ORE.emit([=]() {
      return emitAtomicRMWLegalRemark(RMW) << " due to an unsafe request.";
    });
    return Kind;
  };

  auto SSID = RMW->getSyncScopeID();
  bool HasSystemScope =
      SSID == SyncScope::System ||
      SSID == RMW->getContext().getOrInsertSyncScopeID("one-as");

  switch (RMW->getOperation()) {
  case AtomicRMWInst::Sub:
  case AtomicRMWInst::Or:
  case AtomicRMWInst::Xor: {
    // Atomic sub/or/xor do not work over PCI express, but atomic add
    // does. InstCombine transforms these with 0 to or, so undo that.
    if (HasSystemScope && AMDGPU::isFlatGlobalAddrSpace(AS)) {
      if (Constant *ConstVal = dyn_cast<Constant>(RMW->getValOperand());
          ConstVal && ConstVal->isNullValue())
        return AtomicExpansionKind::Expand;
    }

    break;
  }
  case AtomicRMWInst::FAdd: {
    Type *Ty = RMW->getType();

    // TODO: Handle REGION_ADDRESS
    if (AS == AMDGPUAS::LOCAL_ADDRESS) {
      // DS F32 FP atomics do respect the denormal mode, but the rounding mode
      // is fixed to round-to-nearest-even.
      //
      // F64 / PK_F16 / PK_BF16 never flush and are also fixed to
      // round-to-nearest-even.
      //
      // We ignore the rounding mode problem, even in strictfp. The C++ standard
      // suggests it is OK if the floating-point mode may not match the calling
      // thread.
      if (Ty->isFloatTy()) {
        return Subtarget->hasLDSFPAtomicAddF32() ? AtomicExpansionKind::None
                                                 : AtomicExpansionKind::CmpXChg;
      }

      if (Ty->isDoubleTy()) {
        // Ignores denormal mode, but we don't consider flushing mandatory.
        return Subtarget->hasLDSFPAtomicAddF64() ? AtomicExpansionKind::None
                                                 : AtomicExpansionKind::CmpXChg;
      }

      if (Subtarget->hasAtomicDsPkAdd16Insts() && isHalf2OrBFloat2(Ty))
        return AtomicExpansionKind::None;

      return AtomicExpansionKind::CmpXChg;
    }

    if (!AMDGPU::isFlatGlobalAddrSpace(AS) &&
        AS != AMDGPUAS::BUFFER_FAT_POINTER)
      return AtomicExpansionKind::CmpXChg;

    if (Subtarget->hasGFX940Insts() && (Ty->isFloatTy() || Ty->isDoubleTy()))
      return AtomicExpansionKind::None;

    if (AS == AMDGPUAS::FLAT_ADDRESS) {
      // gfx940, gfx12
      // FIXME: Needs to account for no fine-grained memory
      if (Subtarget->hasAtomicFlatPkAdd16Insts() && isHalf2OrBFloat2(Ty))
        return AtomicExpansionKind::None;
    } else if (AMDGPU::isExtendedGlobalAddrSpace(AS)) {
      // gfx90a, gfx940, gfx12
      // FIXME: Needs to account for no fine-grained memory
      if (Subtarget->hasAtomicBufferGlobalPkAddF16Insts() && isHalf2(Ty))
        return AtomicExpansionKind::None;

      // gfx940, gfx12
      // FIXME: Needs to account for no fine-grained memory
      if (Subtarget->hasAtomicGlobalPkAddBF16Inst() && isBFloat2(Ty))
        return AtomicExpansionKind::None;
    } else if (AS == AMDGPUAS::BUFFER_FAT_POINTER) {
      // gfx90a, gfx940, gfx12
      // FIXME: Needs to account for no fine-grained memory
      if (Subtarget->hasAtomicBufferGlobalPkAddF16Insts() && isHalf2(Ty))
        return AtomicExpansionKind::None;

      // While gfx90a/gfx940 supports v2bf16 for global/flat, it does not for
      // buffer. gfx12 does have the buffer version.
      if (Subtarget->hasAtomicBufferPkAddBF16Inst() && isBFloat2(Ty))
        return AtomicExpansionKind::None;
    }

    if (unsafeFPAtomicsDisabled(RMW->getFunction()))
      return AtomicExpansionKind::CmpXChg;

    // Always expand system scope fp atomics.
    if (HasSystemScope)
      return AtomicExpansionKind::CmpXChg;

    // global and flat atomic fadd f64: gfx90a, gfx940.
    if (Subtarget->hasFlatBufferGlobalAtomicFaddF64Inst() && Ty->isDoubleTy())
      return ReportUnsafeHWInst(AtomicExpansionKind::None);

    if (AS != AMDGPUAS::FLAT_ADDRESS) {
      if (Ty->isFloatTy()) {
        // global/buffer atomic fadd f32 no-rtn: gfx908, gfx90a, gfx940, gfx11+.
        if (RMW->use_empty() && Subtarget->hasAtomicFaddNoRtnInsts())
          return ReportUnsafeHWInst(AtomicExpansionKind::None);
        // global/buffer atomic fadd f32 rtn: gfx90a, gfx940, gfx11+.
        if (!RMW->use_empty() && Subtarget->hasAtomicFaddRtnInsts())
          return ReportUnsafeHWInst(AtomicExpansionKind::None);
      } else {
        // gfx908
        if (RMW->use_empty() &&
            Subtarget->hasAtomicBufferGlobalPkAddF16NoRtnInsts() && isHalf2(Ty))
          return ReportUnsafeHWInst(AtomicExpansionKind::None);
      }
    }

    // flat atomic fadd f32: gfx940, gfx11+.
    if (AS == AMDGPUAS::FLAT_ADDRESS && Ty->isFloatTy()) {
      if (Subtarget->hasFlatAtomicFaddF32Inst())
        return ReportUnsafeHWInst(AtomicExpansionKind::None);

      // If it is in flat address space, and the type is float, we will try to
      // expand it, if the target supports global and lds atomic fadd. The
      // reason we need that is, in the expansion, we emit the check of address
      // space. If it is in global address space, we emit the global atomic
      // fadd; if it is in shared address space, we emit the LDS atomic fadd.
      if (Subtarget->hasLDSFPAtomicAddF32()) {
        if (RMW->use_empty() && Subtarget->hasAtomicFaddNoRtnInsts())
          return AtomicExpansionKind::Expand;
        if (!RMW->use_empty() && Subtarget->hasAtomicFaddRtnInsts())
          return AtomicExpansionKind::Expand;
      }
    }

    return AtomicExpansionKind::CmpXChg;
  }
  case AtomicRMWInst::FMin:
  case AtomicRMWInst::FMax: {
    Type *Ty = RMW->getType();

    // LDS float and double fmin/fmax were always supported.
    if (AS == AMDGPUAS::LOCAL_ADDRESS && (Ty->isFloatTy() || Ty->isDoubleTy()))
      return AtomicExpansionKind::None;

    if (unsafeFPAtomicsDisabled(RMW->getFunction()))
      return AtomicExpansionKind::CmpXChg;

    // Always expand system scope fp atomics.
    if (HasSystemScope)
      return AtomicExpansionKind::CmpXChg;

    // For flat and global cases:
    // float, double in gfx7. Manual claims denormal support.
    // Removed in gfx8.
    // float, double restored in gfx10.
    // double removed again in gfx11, so only f32 for gfx11/gfx12.
    //
    // For gfx9, gfx90a and gfx940 support f64 for global (same as fadd), but no
    // f32.
    //
    // FIXME: Check scope and fine grained memory
    if (AS == AMDGPUAS::FLAT_ADDRESS) {
      if (Subtarget->hasAtomicFMinFMaxF32FlatInsts() && Ty->isFloatTy())
        return ReportUnsafeHWInst(AtomicExpansionKind::None);
      if (Subtarget->hasAtomicFMinFMaxF64FlatInsts() && Ty->isDoubleTy())
        return ReportUnsafeHWInst(AtomicExpansionKind::None);
    } else if (AMDGPU::isExtendedGlobalAddrSpace(AS) ||
               AS == AMDGPUAS::BUFFER_FAT_POINTER) {
      if (Subtarget->hasAtomicFMinFMaxF32GlobalInsts() && Ty->isFloatTy())
        return ReportUnsafeHWInst(AtomicExpansionKind::None);
      if (Subtarget->hasAtomicFMinFMaxF64GlobalInsts() && Ty->isDoubleTy())
        return ReportUnsafeHWInst(AtomicExpansionKind::None);
    }

    return AtomicExpansionKind::CmpXChg;
  }
  case AtomicRMWInst::Min:
  case AtomicRMWInst::Max:
  case AtomicRMWInst::UMin:
  case AtomicRMWInst::UMax: {
    if (AMDGPU::isFlatGlobalAddrSpace(AS) ||
        AS == AMDGPUAS::BUFFER_FAT_POINTER) {
      // Always expand system scope min/max atomics.
      if (HasSystemScope)
        return AtomicExpansionKind::CmpXChg;
    }
    break;
  }
  default:
    break;
  }

  return AMDGPUTargetLowering::shouldExpandAtomicRMWInIR(RMW);
}

TargetLowering::AtomicExpansionKind
SITargetLowering::shouldExpandAtomicLoadInIR(LoadInst *LI) const {
  return LI->getPointerAddressSpace() == AMDGPUAS::PRIVATE_ADDRESS
             ? AtomicExpansionKind::NotAtomic
             : AtomicExpansionKind::None;
}

TargetLowering::AtomicExpansionKind
SITargetLowering::shouldExpandAtomicStoreInIR(StoreInst *SI) const {
  return SI->getPointerAddressSpace() == AMDGPUAS::PRIVATE_ADDRESS
             ? AtomicExpansionKind::NotAtomic
             : AtomicExpansionKind::None;
}

TargetLowering::AtomicExpansionKind
SITargetLowering::shouldExpandAtomicCmpXchgInIR(AtomicCmpXchgInst *CmpX) const {
  return CmpX->getPointerAddressSpace() == AMDGPUAS::PRIVATE_ADDRESS
             ? AtomicExpansionKind::NotAtomic
             : AtomicExpansionKind::None;
}

const TargetRegisterClass *
SITargetLowering::getRegClassFor(MVT VT, bool isDivergent) const {
  const TargetRegisterClass *RC = TargetLoweringBase::getRegClassFor(VT, false);
  const SIRegisterInfo *TRI = Subtarget->getRegisterInfo();
  if (RC == &AMDGPU::VReg_1RegClass && !isDivergent)
    return Subtarget->getWavefrontSize() == 64 ? &AMDGPU::SReg_64RegClass
                                               : &AMDGPU::SReg_32RegClass;
  if (!TRI->isSGPRClass(RC) && !isDivergent)
    return TRI->getEquivalentSGPRClass(RC);
  if (TRI->isSGPRClass(RC) && isDivergent)
    return TRI->getEquivalentVGPRClass(RC);

  return RC;
}

// FIXME: This is a workaround for DivergenceAnalysis not understanding always
// uniform values (as produced by the mask results of control flow intrinsics)
// used outside of divergent blocks. The phi users need to also be treated as
// always uniform.
//
// FIXME: DA is no longer in-use. Does this still apply to UniformityAnalysis?
static bool hasCFUser(const Value *V, SmallPtrSet<const Value *, 16> &Visited,
                      unsigned WaveSize) {
  // FIXME: We assume we never cast the mask results of a control flow
  // intrinsic.
  // Early exit if the type won't be consistent as a compile time hack.
  IntegerType *IT = dyn_cast<IntegerType>(V->getType());
  if (!IT || IT->getBitWidth() != WaveSize)
    return false;

  if (!isa<Instruction>(V))
    return false;
  if (!Visited.insert(V).second)
    return false;
  bool Result = false;
  for (const auto *U : V->users()) {
    if (const IntrinsicInst *Intrinsic = dyn_cast<IntrinsicInst>(U)) {
      if (V == U->getOperand(1)) {
        switch (Intrinsic->getIntrinsicID()) {
        default:
          Result = false;
          break;
        case Intrinsic::amdgcn_if_break:
        case Intrinsic::amdgcn_if:
        case Intrinsic::amdgcn_else:
          Result = true;
          break;
        }
      }
      if (V == U->getOperand(0)) {
        switch (Intrinsic->getIntrinsicID()) {
        default:
          Result = false;
          break;
        case Intrinsic::amdgcn_end_cf:
        case Intrinsic::amdgcn_loop:
          Result = true;
          break;
        }
      }
    } else {
      Result = hasCFUser(U, Visited, WaveSize);
    }
    if (Result)
      break;
  }
  return Result;
}

bool SITargetLowering::requiresUniformRegister(MachineFunction &MF,
                                               const Value *V) const {
  if (const CallInst *CI = dyn_cast<CallInst>(V)) {
    if (CI->isInlineAsm()) {
      // FIXME: This cannot give a correct answer. This should only trigger in
      // the case where inline asm returns mixed SGPR and VGPR results, used
      // outside the defining block. We don't have a specific result to
      // consider, so this assumes if any value is SGPR, the overall register
      // also needs to be SGPR.
      const SIRegisterInfo *SIRI = Subtarget->getRegisterInfo();
      TargetLowering::AsmOperandInfoVector TargetConstraints = ParseConstraints(
          MF.getDataLayout(), Subtarget->getRegisterInfo(), *CI);
      for (auto &TC : TargetConstraints) {
        if (TC.Type == InlineAsm::isOutput) {
          ComputeConstraintToUse(TC, SDValue());
          const TargetRegisterClass *RC = getRegForInlineAsmConstraint(
              SIRI, TC.ConstraintCode, TC.ConstraintVT).second;
          if (RC && SIRI->isSGPRClass(RC))
            return true;
        }
      }
    }
  }
  SmallPtrSet<const Value *, 16> Visited;
  return hasCFUser(V, Visited, Subtarget->getWavefrontSize());
}

bool SITargetLowering::hasMemSDNodeUser(SDNode *N) const {
  SDNode::use_iterator I = N->use_begin(), E = N->use_end();
  for (; I != E; ++I) {
    if (MemSDNode *M = dyn_cast<MemSDNode>(*I)) {
      if (getBasePtrIndex(M) == I.getOperandNo())
        return true;
    }
  }
  return false;
}

bool SITargetLowering::isReassocProfitable(SelectionDAG &DAG, SDValue N0,
                                           SDValue N1) const {
  if (!N0.hasOneUse())
    return false;
  // Take care of the opportunity to keep N0 uniform
  if (N0->isDivergent() || !N1->isDivergent())
    return true;
  // Check if we have a good chance to form the memory access pattern with the
  // base and offset
  return (DAG.isBaseWithConstantOffset(N0) &&
          hasMemSDNodeUser(*N0->use_begin()));
}

bool SITargetLowering::isReassocProfitable(MachineRegisterInfo &MRI,
                                           Register N0, Register N1) const {
  return MRI.hasOneNonDBGUse(N0); // FIXME: handle regbanks
}

MachineMemOperand::Flags
SITargetLowering::getTargetMMOFlags(const Instruction &I) const {
  // Propagate metadata set by AMDGPUAnnotateUniformValues to the MMO of a load.
  MachineMemOperand::Flags Flags = MachineMemOperand::MONone;
  if (I.getMetadata("amdgpu.noclobber"))
    Flags |= MONoClobber;
  if (I.getMetadata("amdgpu.last.use"))
    Flags |= MOLastUse;
  return Flags;
}

bool SITargetLowering::checkForPhysRegDependency(
    SDNode *Def, SDNode *User, unsigned Op, const TargetRegisterInfo *TRI,
    const TargetInstrInfo *TII, unsigned &PhysReg, int &Cost) const {
  if (User->getOpcode() != ISD::CopyToReg)
    return false;
  if (!Def->isMachineOpcode())
    return false;
  MachineSDNode *MDef = dyn_cast<MachineSDNode>(Def);
  if (!MDef)
    return false;

  unsigned ResNo = User->getOperand(Op).getResNo();
  if (User->getOperand(Op)->getValueType(ResNo) != MVT::i1)
    return false;
  const MCInstrDesc &II = TII->get(MDef->getMachineOpcode());
  if (II.isCompare() && II.hasImplicitDefOfPhysReg(AMDGPU::SCC)) {
    PhysReg = AMDGPU::SCC;
    const TargetRegisterClass *RC =
        TRI->getMinimalPhysRegClass(PhysReg, Def->getSimpleValueType(ResNo));
    Cost = RC->getCopyCost();
    return true;
  }
  return false;
}

void SITargetLowering::emitExpandAtomicRMW(AtomicRMWInst *AI) const {
  AtomicRMWInst::BinOp Op = AI->getOperation();

  if (Op == AtomicRMWInst::Sub || Op == AtomicRMWInst::Or ||
      Op == AtomicRMWInst::Xor) {
    // atomicrmw or %ptr, 0 -> atomicrmw add %ptr, 0
    assert(cast<Constant>(AI->getValOperand())->isNullValue() &&
           "this cannot be replaced with add");
    AI->setOperation(AtomicRMWInst::Add);
    return;
  }

  assert(Subtarget->hasAtomicFaddInsts() &&
         "target should have atomic fadd instructions");
  assert(AI->getType()->isFloatTy() &&
         AI->getPointerAddressSpace() == AMDGPUAS::FLAT_ADDRESS &&
         "generic atomicrmw expansion only supports FP32 operand in flat "
         "address space");
  assert(Op == AtomicRMWInst::FAdd && "only fadd is supported for now");

  // Given: atomicrmw fadd ptr %addr, float %val ordering
  //
  // With this expansion we produce the following code:
  //   [...]
  //   br label %atomicrmw.check.shared
  //
  // atomicrmw.check.shared:
  //   %is.shared = call i1 @llvm.amdgcn.is.shared(ptr %addr)
  //   br i1 %is.shared, label %atomicrmw.shared, label %atomicrmw.check.private
  //
  // atomicrmw.shared:
  //   %cast.shared = addrspacecast ptr %addr to ptr addrspace(3)
  //   %loaded.shared = atomicrmw fadd ptr addrspace(3) %cast.shared,
  //                                   float %val ordering
  //   br label %atomicrmw.phi
  //
  // atomicrmw.check.private:
  //   %is.private = call i1 @llvm.amdgcn.is.private(ptr %int8ptr)
  //   br i1 %is.private, label %atomicrmw.private, label %atomicrmw.global
  //
  // atomicrmw.private:
  //   %cast.private = addrspacecast ptr %addr to ptr addrspace(5)
  //   %loaded.private = load float, ptr addrspace(5) %cast.private
  //   %val.new = fadd float %loaded.private, %val
  //   store float %val.new, ptr addrspace(5) %cast.private
  //   br label %atomicrmw.phi
  //
  // atomicrmw.global:
  //   %cast.global = addrspacecast ptr %addr to ptr addrspace(1)
  //   %loaded.global = atomicrmw fadd ptr addrspace(1) %cast.global,
  //                                   float %val ordering
  //   br label %atomicrmw.phi
  //
  // atomicrmw.phi:
  //   %loaded.phi = phi float [ %loaded.shared, %atomicrmw.shared ],
  //                           [ %loaded.private, %atomicrmw.private ],
  //                           [ %loaded.global, %atomicrmw.global ]
  //   br label %atomicrmw.end
  //
  // atomicrmw.end:
  //    [...]

  IRBuilder<> Builder(AI);
  LLVMContext &Ctx = Builder.getContext();

  BasicBlock *BB = Builder.GetInsertBlock();
  Function *F = BB->getParent();
  BasicBlock *ExitBB =
      BB->splitBasicBlock(Builder.GetInsertPoint(), "atomicrmw.end");
  BasicBlock *CheckSharedBB =
      BasicBlock::Create(Ctx, "atomicrmw.check.shared", F, ExitBB);
  BasicBlock *SharedBB = BasicBlock::Create(Ctx, "atomicrmw.shared", F, ExitBB);
  BasicBlock *CheckPrivateBB =
      BasicBlock::Create(Ctx, "atomicrmw.check.private", F, ExitBB);
  BasicBlock *PrivateBB =
      BasicBlock::Create(Ctx, "atomicrmw.private", F, ExitBB);
  BasicBlock *GlobalBB = BasicBlock::Create(Ctx, "atomicrmw.global", F, ExitBB);
  BasicBlock *PhiBB = BasicBlock::Create(Ctx, "atomicrmw.phi", F, ExitBB);

  Value *Val = AI->getValOperand();
  Type *ValTy = Val->getType();
  Value *Addr = AI->getPointerOperand();

  auto CreateNewAtomicRMW = [AI](IRBuilder<> &Builder, Value *Addr,
                                 Value *Val) -> Value * {
    AtomicRMWInst *OldVal =
        Builder.CreateAtomicRMW(AI->getOperation(), Addr, Val, AI->getAlign(),
                                AI->getOrdering(), AI->getSyncScopeID());
    SmallVector<std::pair<unsigned, MDNode *>> MDs;
    AI->getAllMetadata(MDs);
    for (auto &P : MDs)
      OldVal->setMetadata(P.first, P.second);
    return OldVal;
  };

  std::prev(BB->end())->eraseFromParent();
  Builder.SetInsertPoint(BB);
  Builder.CreateBr(CheckSharedBB);

  Builder.SetInsertPoint(CheckSharedBB);
  CallInst *IsShared = Builder.CreateIntrinsic(Intrinsic::amdgcn_is_shared, {},
                                               {Addr}, nullptr, "is.shared");
  Builder.CreateCondBr(IsShared, SharedBB, CheckPrivateBB);

  Builder.SetInsertPoint(SharedBB);
  Value *CastToLocal = Builder.CreateAddrSpaceCast(
      Addr, PointerType::get(Ctx, AMDGPUAS::LOCAL_ADDRESS));
  Value *LoadedShared = CreateNewAtomicRMW(Builder, CastToLocal, Val);
  Builder.CreateBr(PhiBB);

  Builder.SetInsertPoint(CheckPrivateBB);
  CallInst *IsPrivate = Builder.CreateIntrinsic(
      Intrinsic::amdgcn_is_private, {}, {Addr}, nullptr, "is.private");
  Builder.CreateCondBr(IsPrivate, PrivateBB, GlobalBB);

  Builder.SetInsertPoint(PrivateBB);
  Value *CastToPrivate = Builder.CreateAddrSpaceCast(
      Addr, PointerType::get(Ctx, AMDGPUAS::PRIVATE_ADDRESS));
  Value *LoadedPrivate =
      Builder.CreateLoad(ValTy, CastToPrivate, "loaded.private");
  Value *NewVal = Builder.CreateFAdd(LoadedPrivate, Val, "val.new");
  Builder.CreateStore(NewVal, CastToPrivate);
  Builder.CreateBr(PhiBB);

  Builder.SetInsertPoint(GlobalBB);
  Value *CastToGlobal = Builder.CreateAddrSpaceCast(
      Addr, PointerType::get(Ctx, AMDGPUAS::GLOBAL_ADDRESS));
  Value *LoadedGlobal = CreateNewAtomicRMW(Builder, CastToGlobal, Val);
  Builder.CreateBr(PhiBB);

  Builder.SetInsertPoint(PhiBB);
  PHINode *Loaded = Builder.CreatePHI(ValTy, 3, "loaded.phi");
  Loaded->addIncoming(LoadedShared, SharedBB);
  Loaded->addIncoming(LoadedPrivate, PrivateBB);
  Loaded->addIncoming(LoadedGlobal, GlobalBB);
  Builder.CreateBr(ExitBB);

  AI->replaceAllUsesWith(Loaded);
  AI->eraseFromParent();
}

LoadInst *
SITargetLowering::lowerIdempotentRMWIntoFencedLoad(AtomicRMWInst *AI) const {
  IRBuilder<> Builder(AI);
  auto Order = AI->getOrdering();

  // The optimization removes store aspect of the atomicrmw. Therefore, cache
  // must be flushed if the atomic ordering had a release semantics. This is
  // not necessary a fence, a release fence just coincides to do that flush.
  // Avoid replacing of an atomicrmw with a release semantics.
  if (isReleaseOrStronger(Order))
    return nullptr;

  LoadInst *LI = Builder.CreateAlignedLoad(
      AI->getType(), AI->getPointerOperand(), AI->getAlign());
  LI->setAtomic(Order, AI->getSyncScopeID());
  LI->copyMetadata(*AI);
  LI->takeName(AI);
  AI->replaceAllUsesWith(LI);
  AI->eraseFromParent();
  return LI;
}
