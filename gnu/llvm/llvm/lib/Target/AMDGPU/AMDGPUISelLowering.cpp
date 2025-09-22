//===-- AMDGPUISelLowering.cpp - AMDGPU Common DAG lowering functions -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This is the parent TargetLowering class for hardware code gen
/// targets.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUISelLowering.h"
#include "AMDGPU.h"
#include "AMDGPUInstrInfo.h"
#include "AMDGPUMachineFunction.h"
#include "SIMachineFunctionInfo.h"
#include "llvm/CodeGen/Analysis.h"
#include "llvm/CodeGen/GlobalISel/GISelKnownBits.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/KnownBits.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#include "AMDGPUGenCallingConv.inc"

static cl::opt<bool> AMDGPUBypassSlowDiv(
  "amdgpu-bypass-slow-div",
  cl::desc("Skip 64-bit divide for dynamic 32-bit values"),
  cl::init(true));

// Find a larger type to do a load / store of a vector with.
EVT AMDGPUTargetLowering::getEquivalentMemType(LLVMContext &Ctx, EVT VT) {
  unsigned StoreSize = VT.getStoreSizeInBits();
  if (StoreSize <= 32)
    return EVT::getIntegerVT(Ctx, StoreSize);

  if (StoreSize % 32 == 0)
    return EVT::getVectorVT(Ctx, MVT::i32, StoreSize / 32);

  return VT;
}

unsigned AMDGPUTargetLowering::numBitsUnsigned(SDValue Op, SelectionDAG &DAG) {
  return DAG.computeKnownBits(Op).countMaxActiveBits();
}

unsigned AMDGPUTargetLowering::numBitsSigned(SDValue Op, SelectionDAG &DAG) {
  // In order for this to be a signed 24-bit value, bit 23, must
  // be a sign bit.
  return DAG.ComputeMaxSignificantBits(Op);
}

AMDGPUTargetLowering::AMDGPUTargetLowering(const TargetMachine &TM,
                                           const AMDGPUSubtarget &STI)
    : TargetLowering(TM), Subtarget(&STI) {
  // Always lower memset, memcpy, and memmove intrinsics to load/store
  // instructions, rather then generating calls to memset, mempcy or memmove.
  MaxStoresPerMemset = MaxStoresPerMemsetOptSize = ~0U;
  MaxStoresPerMemcpy = MaxStoresPerMemcpyOptSize = ~0U;
  MaxStoresPerMemmove = MaxStoresPerMemmoveOptSize = ~0U;

  // Enable ganging up loads and stores in the memcpy DAG lowering.
  MaxGluedStoresPerMemcpy = 16;

  // Lower floating point store/load to integer store/load to reduce the number
  // of patterns in tablegen.
  setOperationAction(ISD::LOAD, MVT::f32, Promote);
  AddPromotedToType(ISD::LOAD, MVT::f32, MVT::i32);

  setOperationAction(ISD::LOAD, MVT::v2f32, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v2f32, MVT::v2i32);

  setOperationAction(ISD::LOAD, MVT::v3f32, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v3f32, MVT::v3i32);

  setOperationAction(ISD::LOAD, MVT::v4f32, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v4f32, MVT::v4i32);

  setOperationAction(ISD::LOAD, MVT::v5f32, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v5f32, MVT::v5i32);

  setOperationAction(ISD::LOAD, MVT::v6f32, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v6f32, MVT::v6i32);

  setOperationAction(ISD::LOAD, MVT::v7f32, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v7f32, MVT::v7i32);

  setOperationAction(ISD::LOAD, MVT::v8f32, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v8f32, MVT::v8i32);

  setOperationAction(ISD::LOAD, MVT::v9f32, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v9f32, MVT::v9i32);

  setOperationAction(ISD::LOAD, MVT::v10f32, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v10f32, MVT::v10i32);

  setOperationAction(ISD::LOAD, MVT::v11f32, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v11f32, MVT::v11i32);

  setOperationAction(ISD::LOAD, MVT::v12f32, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v12f32, MVT::v12i32);

  setOperationAction(ISD::LOAD, MVT::v16f32, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v16f32, MVT::v16i32);

  setOperationAction(ISD::LOAD, MVT::v32f32, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v32f32, MVT::v32i32);

  setOperationAction(ISD::LOAD, MVT::i64, Promote);
  AddPromotedToType(ISD::LOAD, MVT::i64, MVT::v2i32);

  setOperationAction(ISD::LOAD, MVT::v2i64, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v2i64, MVT::v4i32);

  setOperationAction(ISD::LOAD, MVT::f64, Promote);
  AddPromotedToType(ISD::LOAD, MVT::f64, MVT::v2i32);

  setOperationAction(ISD::LOAD, MVT::v2f64, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v2f64, MVT::v4i32);

  setOperationAction(ISD::LOAD, MVT::v3i64, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v3i64, MVT::v6i32);

  setOperationAction(ISD::LOAD, MVT::v4i64, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v4i64, MVT::v8i32);

  setOperationAction(ISD::LOAD, MVT::v3f64, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v3f64, MVT::v6i32);

  setOperationAction(ISD::LOAD, MVT::v4f64, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v4f64, MVT::v8i32);

  setOperationAction(ISD::LOAD, MVT::v8i64, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v8i64, MVT::v16i32);

  setOperationAction(ISD::LOAD, MVT::v8f64, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v8f64, MVT::v16i32);

  setOperationAction(ISD::LOAD, MVT::v16i64, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v16i64, MVT::v32i32);

  setOperationAction(ISD::LOAD, MVT::v16f64, Promote);
  AddPromotedToType(ISD::LOAD, MVT::v16f64, MVT::v32i32);

  setOperationAction(ISD::LOAD, MVT::i128, Promote);
  AddPromotedToType(ISD::LOAD, MVT::i128, MVT::v4i32);

  // TODO: Would be better to consume as directly legal
  setOperationAction(ISD::ATOMIC_LOAD, MVT::f32, Promote);
  AddPromotedToType(ISD::ATOMIC_LOAD, MVT::f32, MVT::i32);

  setOperationAction(ISD::ATOMIC_LOAD, MVT::f64, Promote);
  AddPromotedToType(ISD::ATOMIC_LOAD, MVT::f64, MVT::i64);

  setOperationAction(ISD::ATOMIC_LOAD, MVT::f16, Promote);
  AddPromotedToType(ISD::ATOMIC_LOAD, MVT::f16, MVT::i16);

  setOperationAction(ISD::ATOMIC_LOAD, MVT::bf16, Promote);
  AddPromotedToType(ISD::ATOMIC_LOAD, MVT::bf16, MVT::i16);

  setOperationAction(ISD::ATOMIC_STORE, MVT::f32, Promote);
  AddPromotedToType(ISD::ATOMIC_STORE, MVT::f32, MVT::i32);

  setOperationAction(ISD::ATOMIC_STORE, MVT::f64, Promote);
  AddPromotedToType(ISD::ATOMIC_STORE, MVT::f64, MVT::i64);

  setOperationAction(ISD::ATOMIC_STORE, MVT::f16, Promote);
  AddPromotedToType(ISD::ATOMIC_STORE, MVT::f16, MVT::i16);

  setOperationAction(ISD::ATOMIC_STORE, MVT::bf16, Promote);
  AddPromotedToType(ISD::ATOMIC_STORE, MVT::bf16, MVT::i16);

  // There are no 64-bit extloads. These should be done as a 32-bit extload and
  // an extension to 64-bit.
  for (MVT VT : MVT::integer_valuetypes())
    setLoadExtAction({ISD::EXTLOAD, ISD::SEXTLOAD, ISD::ZEXTLOAD}, MVT::i64, VT,
                     Expand);

  for (MVT VT : MVT::integer_valuetypes()) {
    if (VT == MVT::i64)
      continue;

    for (auto Op : {ISD::SEXTLOAD, ISD::ZEXTLOAD, ISD::EXTLOAD}) {
      setLoadExtAction(Op, VT, MVT::i1, Promote);
      setLoadExtAction(Op, VT, MVT::i8, Legal);
      setLoadExtAction(Op, VT, MVT::i16, Legal);
      setLoadExtAction(Op, VT, MVT::i32, Expand);
    }
  }

  for (MVT VT : MVT::integer_fixedlen_vector_valuetypes())
    for (auto MemVT :
         {MVT::v2i8, MVT::v4i8, MVT::v2i16, MVT::v3i16, MVT::v4i16})
      setLoadExtAction({ISD::SEXTLOAD, ISD::ZEXTLOAD, ISD::EXTLOAD}, VT, MemVT,
                       Expand);

  setLoadExtAction(ISD::EXTLOAD, MVT::f32, MVT::f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::f32, MVT::bf16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v2f32, MVT::v2f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v2f32, MVT::v2bf16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v3f32, MVT::v3f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v3f32, MVT::v3bf16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v4f32, MVT::v4f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v4f32, MVT::v4bf16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v8f32, MVT::v8f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v8f32, MVT::v8bf16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v16f32, MVT::v16f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v16f32, MVT::v16bf16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v32f32, MVT::v32f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v32f32, MVT::v32bf16, Expand);

  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f32, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v2f64, MVT::v2f32, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v3f64, MVT::v3f32, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v4f64, MVT::v4f32, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v8f64, MVT::v8f32, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v16f64, MVT::v16f32, Expand);

  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::f64, MVT::bf16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v2f64, MVT::v2f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v2f64, MVT::v2bf16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v3f64, MVT::v3f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v3f64, MVT::v3bf16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v4f64, MVT::v4f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v4f64, MVT::v4bf16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v8f64, MVT::v8f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v8f64, MVT::v8bf16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v16f64, MVT::v16f16, Expand);
  setLoadExtAction(ISD::EXTLOAD, MVT::v16f64, MVT::v16bf16, Expand);

  setOperationAction(ISD::STORE, MVT::f32, Promote);
  AddPromotedToType(ISD::STORE, MVT::f32, MVT::i32);

  setOperationAction(ISD::STORE, MVT::v2f32, Promote);
  AddPromotedToType(ISD::STORE, MVT::v2f32, MVT::v2i32);

  setOperationAction(ISD::STORE, MVT::v3f32, Promote);
  AddPromotedToType(ISD::STORE, MVT::v3f32, MVT::v3i32);

  setOperationAction(ISD::STORE, MVT::v4f32, Promote);
  AddPromotedToType(ISD::STORE, MVT::v4f32, MVT::v4i32);

  setOperationAction(ISD::STORE, MVT::v5f32, Promote);
  AddPromotedToType(ISD::STORE, MVT::v5f32, MVT::v5i32);

  setOperationAction(ISD::STORE, MVT::v6f32, Promote);
  AddPromotedToType(ISD::STORE, MVT::v6f32, MVT::v6i32);

  setOperationAction(ISD::STORE, MVT::v7f32, Promote);
  AddPromotedToType(ISD::STORE, MVT::v7f32, MVT::v7i32);

  setOperationAction(ISD::STORE, MVT::v8f32, Promote);
  AddPromotedToType(ISD::STORE, MVT::v8f32, MVT::v8i32);

  setOperationAction(ISD::STORE, MVT::v9f32, Promote);
  AddPromotedToType(ISD::STORE, MVT::v9f32, MVT::v9i32);

  setOperationAction(ISD::STORE, MVT::v10f32, Promote);
  AddPromotedToType(ISD::STORE, MVT::v10f32, MVT::v10i32);

  setOperationAction(ISD::STORE, MVT::v11f32, Promote);
  AddPromotedToType(ISD::STORE, MVT::v11f32, MVT::v11i32);

  setOperationAction(ISD::STORE, MVT::v12f32, Promote);
  AddPromotedToType(ISD::STORE, MVT::v12f32, MVT::v12i32);

  setOperationAction(ISD::STORE, MVT::v16f32, Promote);
  AddPromotedToType(ISD::STORE, MVT::v16f32, MVT::v16i32);

  setOperationAction(ISD::STORE, MVT::v32f32, Promote);
  AddPromotedToType(ISD::STORE, MVT::v32f32, MVT::v32i32);

  setOperationAction(ISD::STORE, MVT::i64, Promote);
  AddPromotedToType(ISD::STORE, MVT::i64, MVT::v2i32);

  setOperationAction(ISD::STORE, MVT::v2i64, Promote);
  AddPromotedToType(ISD::STORE, MVT::v2i64, MVT::v4i32);

  setOperationAction(ISD::STORE, MVT::f64, Promote);
  AddPromotedToType(ISD::STORE, MVT::f64, MVT::v2i32);

  setOperationAction(ISD::STORE, MVT::v2f64, Promote);
  AddPromotedToType(ISD::STORE, MVT::v2f64, MVT::v4i32);

  setOperationAction(ISD::STORE, MVT::v3i64, Promote);
  AddPromotedToType(ISD::STORE, MVT::v3i64, MVT::v6i32);

  setOperationAction(ISD::STORE, MVT::v3f64, Promote);
  AddPromotedToType(ISD::STORE, MVT::v3f64, MVT::v6i32);

  setOperationAction(ISD::STORE, MVT::v4i64, Promote);
  AddPromotedToType(ISD::STORE, MVT::v4i64, MVT::v8i32);

  setOperationAction(ISD::STORE, MVT::v4f64, Promote);
  AddPromotedToType(ISD::STORE, MVT::v4f64, MVT::v8i32);

  setOperationAction(ISD::STORE, MVT::v8i64, Promote);
  AddPromotedToType(ISD::STORE, MVT::v8i64, MVT::v16i32);

  setOperationAction(ISD::STORE, MVT::v8f64, Promote);
  AddPromotedToType(ISD::STORE, MVT::v8f64, MVT::v16i32);

  setOperationAction(ISD::STORE, MVT::v16i64, Promote);
  AddPromotedToType(ISD::STORE, MVT::v16i64, MVT::v32i32);

  setOperationAction(ISD::STORE, MVT::v16f64, Promote);
  AddPromotedToType(ISD::STORE, MVT::v16f64, MVT::v32i32);

  setOperationAction(ISD::STORE, MVT::i128, Promote);
  AddPromotedToType(ISD::STORE, MVT::i128, MVT::v4i32);

  setTruncStoreAction(MVT::i64, MVT::i1, Expand);
  setTruncStoreAction(MVT::i64, MVT::i8, Expand);
  setTruncStoreAction(MVT::i64, MVT::i16, Expand);
  setTruncStoreAction(MVT::i64, MVT::i32, Expand);

  setTruncStoreAction(MVT::v2i64, MVT::v2i1, Expand);
  setTruncStoreAction(MVT::v2i64, MVT::v2i8, Expand);
  setTruncStoreAction(MVT::v2i64, MVT::v2i16, Expand);
  setTruncStoreAction(MVT::v2i64, MVT::v2i32, Expand);

  setTruncStoreAction(MVT::f32, MVT::bf16, Expand);
  setTruncStoreAction(MVT::f32, MVT::f16, Expand);
  setTruncStoreAction(MVT::v2f32, MVT::v2bf16, Expand);
  setTruncStoreAction(MVT::v2f32, MVT::v2f16, Expand);
  setTruncStoreAction(MVT::v3f32, MVT::v3bf16, Expand);
  setTruncStoreAction(MVT::v3f32, MVT::v3f16, Expand);
  setTruncStoreAction(MVT::v4f32, MVT::v4bf16, Expand);
  setTruncStoreAction(MVT::v4f32, MVT::v4f16, Expand);
  setTruncStoreAction(MVT::v8f32, MVT::v8bf16, Expand);
  setTruncStoreAction(MVT::v8f32, MVT::v8f16, Expand);
  setTruncStoreAction(MVT::v16f32, MVT::v16bf16, Expand);
  setTruncStoreAction(MVT::v16f32, MVT::v16f16, Expand);
  setTruncStoreAction(MVT::v32f32, MVT::v32bf16, Expand);
  setTruncStoreAction(MVT::v32f32, MVT::v32f16, Expand);

  setTruncStoreAction(MVT::f64, MVT::bf16, Expand);
  setTruncStoreAction(MVT::f64, MVT::f16, Expand);
  setTruncStoreAction(MVT::f64, MVT::f32, Expand);

  setTruncStoreAction(MVT::v2f64, MVT::v2f32, Expand);
  setTruncStoreAction(MVT::v2f64, MVT::v2bf16, Expand);
  setTruncStoreAction(MVT::v2f64, MVT::v2f16, Expand);

  setTruncStoreAction(MVT::v3i32, MVT::v3i8, Expand);

  setTruncStoreAction(MVT::v3i64, MVT::v3i32, Expand);
  setTruncStoreAction(MVT::v3i64, MVT::v3i16, Expand);
  setTruncStoreAction(MVT::v3i64, MVT::v3i8, Expand);
  setTruncStoreAction(MVT::v3i64, MVT::v3i1, Expand);
  setTruncStoreAction(MVT::v3f64, MVT::v3f32, Expand);
  setTruncStoreAction(MVT::v3f64, MVT::v3bf16, Expand);
  setTruncStoreAction(MVT::v3f64, MVT::v3f16, Expand);

  setTruncStoreAction(MVT::v4i64, MVT::v4i32, Expand);
  setTruncStoreAction(MVT::v4i64, MVT::v4i16, Expand);
  setTruncStoreAction(MVT::v4f64, MVT::v4f32, Expand);
  setTruncStoreAction(MVT::v4f64, MVT::v4bf16, Expand);
  setTruncStoreAction(MVT::v4f64, MVT::v4f16, Expand);

  setTruncStoreAction(MVT::v8f64, MVT::v8f32, Expand);
  setTruncStoreAction(MVT::v8f64, MVT::v8bf16, Expand);
  setTruncStoreAction(MVT::v8f64, MVT::v8f16, Expand);

  setTruncStoreAction(MVT::v16f64, MVT::v16f32, Expand);
  setTruncStoreAction(MVT::v16f64, MVT::v16bf16, Expand);
  setTruncStoreAction(MVT::v16f64, MVT::v16f16, Expand);
  setTruncStoreAction(MVT::v16i64, MVT::v16i16, Expand);
  setTruncStoreAction(MVT::v16i64, MVT::v16i16, Expand);
  setTruncStoreAction(MVT::v16i64, MVT::v16i8, Expand);
  setTruncStoreAction(MVT::v16i64, MVT::v16i8, Expand);
  setTruncStoreAction(MVT::v16i64, MVT::v16i1, Expand);

  setOperationAction(ISD::Constant, {MVT::i32, MVT::i64}, Legal);
  setOperationAction(ISD::ConstantFP, {MVT::f32, MVT::f64}, Legal);

  setOperationAction({ISD::BR_JT, ISD::BRIND}, MVT::Other, Expand);

  // For R600, this is totally unsupported, just custom lower to produce an
  // error.
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32, Custom);

  // Library functions.  These default to Expand, but we have instructions
  // for them.
  setOperationAction({ISD::FCEIL, ISD::FPOW, ISD::FABS, ISD::FFLOOR,
                      ISD::FROUNDEVEN, ISD::FTRUNC, ISD::FMINNUM, ISD::FMAXNUM},
                     MVT::f32, Legal);

  setOperationAction(ISD::FLOG2, MVT::f32, Custom);
  setOperationAction(ISD::FROUND, {MVT::f32, MVT::f64}, Custom);

  setOperationAction(
      {ISD::FLOG, ISD::FLOG10, ISD::FEXP, ISD::FEXP2, ISD::FEXP10}, MVT::f32,
      Custom);

  setOperationAction(ISD::FNEARBYINT, {MVT::f16, MVT::f32, MVT::f64}, Custom);

  setOperationAction(ISD::FRINT, {MVT::f16, MVT::f32, MVT::f64}, Custom);

  setOperationAction(ISD::FREM, {MVT::f16, MVT::f32, MVT::f64}, Custom);

  if (Subtarget->has16BitInsts())
    setOperationAction(ISD::IS_FPCLASS, {MVT::f16, MVT::f32, MVT::f64}, Legal);
  else {
    setOperationAction(ISD::IS_FPCLASS, {MVT::f32, MVT::f64}, Legal);
    setOperationAction({ISD::FLOG2, ISD::FEXP2}, MVT::f16, Custom);
  }

  setOperationAction({ISD::FLOG10, ISD::FLOG, ISD::FEXP, ISD::FEXP10}, MVT::f16,
                     Custom);

  // FIXME: These IS_FPCLASS vector fp types are marked custom so it reaches
  // scalarization code. Can be removed when IS_FPCLASS expand isn't called by
  // default unless marked custom/legal.
  setOperationAction(
      ISD::IS_FPCLASS,
      {MVT::v2f16, MVT::v3f16, MVT::v4f16, MVT::v16f16, MVT::v2f32, MVT::v3f32,
       MVT::v4f32, MVT::v5f32, MVT::v6f32, MVT::v7f32, MVT::v8f32, MVT::v16f32,
       MVT::v2f64, MVT::v3f64, MVT::v4f64, MVT::v8f64, MVT::v16f64},
      Custom);

  // Expand to fneg + fadd.
  setOperationAction(ISD::FSUB, MVT::f64, Expand);

  setOperationAction(ISD::CONCAT_VECTORS,
                     {MVT::v3i32,  MVT::v3f32,  MVT::v4i32,  MVT::v4f32,
                      MVT::v5i32,  MVT::v5f32,  MVT::v6i32,  MVT::v6f32,
                      MVT::v7i32,  MVT::v7f32,  MVT::v8i32,  MVT::v8f32,
                      MVT::v9i32,  MVT::v9f32,  MVT::v10i32, MVT::v10f32,
                      MVT::v11i32, MVT::v11f32, MVT::v12i32, MVT::v12f32},
                     Custom);

  // FIXME: Why is v8f16/v8bf16 missing?
  setOperationAction(
      ISD::EXTRACT_SUBVECTOR,
      {MVT::v2f16,  MVT::v2bf16, MVT::v2i16,  MVT::v4f16,  MVT::v4bf16,
       MVT::v4i16,  MVT::v2f32,  MVT::v2i32,  MVT::v3f32,  MVT::v3i32,
       MVT::v4f32,  MVT::v4i32,  MVT::v5f32,  MVT::v5i32,  MVT::v6f32,
       MVT::v6i32,  MVT::v7f32,  MVT::v7i32,  MVT::v8f32,  MVT::v8i32,
       MVT::v9f32,  MVT::v9i32,  MVT::v10i32, MVT::v10f32, MVT::v11i32,
       MVT::v11f32, MVT::v12i32, MVT::v12f32, MVT::v16f16, MVT::v16bf16,
       MVT::v16i16, MVT::v16f32, MVT::v16i32, MVT::v32f32, MVT::v32i32,
       MVT::v2f64,  MVT::v2i64,  MVT::v3f64,  MVT::v3i64,  MVT::v4f64,
       MVT::v4i64,  MVT::v8f64,  MVT::v8i64,  MVT::v16f64, MVT::v16i64,
       MVT::v32i16, MVT::v32f16, MVT::v32bf16},
      Custom);

  setOperationAction(ISD::FP16_TO_FP, MVT::f64, Expand);
  setOperationAction(ISD::FP_TO_FP16, {MVT::f64, MVT::f32}, Custom);

  const MVT ScalarIntVTs[] = { MVT::i32, MVT::i64 };
  for (MVT VT : ScalarIntVTs) {
    // These should use [SU]DIVREM, so set them to expand
    setOperationAction({ISD::SDIV, ISD::UDIV, ISD::SREM, ISD::UREM}, VT,
                       Expand);

    // GPU does not have divrem function for signed or unsigned.
    setOperationAction({ISD::SDIVREM, ISD::UDIVREM}, VT, Custom);

    // GPU does not have [S|U]MUL_LOHI functions as a single instruction.
    setOperationAction({ISD::SMUL_LOHI, ISD::UMUL_LOHI}, VT, Expand);

    setOperationAction({ISD::BSWAP, ISD::CTTZ, ISD::CTLZ}, VT, Expand);

    // AMDGPU uses ADDC/SUBC/ADDE/SUBE
    setOperationAction({ISD::ADDC, ISD::SUBC, ISD::ADDE, ISD::SUBE}, VT, Legal);
  }

  // The hardware supports 32-bit FSHR, but not FSHL.
  setOperationAction(ISD::FSHR, MVT::i32, Legal);

  // The hardware supports 32-bit ROTR, but not ROTL.
  setOperationAction(ISD::ROTL, {MVT::i32, MVT::i64}, Expand);
  setOperationAction(ISD::ROTR, MVT::i64, Expand);

  setOperationAction({ISD::MULHU, ISD::MULHS}, MVT::i16, Expand);

  setOperationAction({ISD::MUL, ISD::MULHU, ISD::MULHS}, MVT::i64, Expand);
  setOperationAction(
      {ISD::UINT_TO_FP, ISD::SINT_TO_FP, ISD::FP_TO_SINT, ISD::FP_TO_UINT},
      MVT::i64, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::i64, Expand);

  setOperationAction({ISD::SMIN, ISD::UMIN, ISD::SMAX, ISD::UMAX}, MVT::i32,
                     Legal);

  setOperationAction(
      {ISD::CTTZ, ISD::CTTZ_ZERO_UNDEF, ISD::CTLZ, ISD::CTLZ_ZERO_UNDEF},
      MVT::i64, Custom);

  for (auto VT : {MVT::i8, MVT::i16})
    setOperationAction({ISD::CTLZ, ISD::CTLZ_ZERO_UNDEF}, VT, Custom);

  static const MVT::SimpleValueType VectorIntTypes[] = {
      MVT::v2i32, MVT::v3i32, MVT::v4i32, MVT::v5i32, MVT::v6i32, MVT::v7i32,
      MVT::v9i32, MVT::v10i32, MVT::v11i32, MVT::v12i32};

  for (MVT VT : VectorIntTypes) {
    // Expand the following operations for the current type by default.
    setOperationAction({ISD::ADD,        ISD::AND,     ISD::FP_TO_SINT,
                        ISD::FP_TO_UINT, ISD::MUL,     ISD::MULHU,
                        ISD::MULHS,      ISD::OR,      ISD::SHL,
                        ISD::SRA,        ISD::SRL,     ISD::ROTL,
                        ISD::ROTR,       ISD::SUB,     ISD::SINT_TO_FP,
                        ISD::UINT_TO_FP, ISD::SDIV,    ISD::UDIV,
                        ISD::SREM,       ISD::UREM,    ISD::SMUL_LOHI,
                        ISD::UMUL_LOHI,  ISD::SDIVREM, ISD::UDIVREM,
                        ISD::SELECT,     ISD::VSELECT, ISD::SELECT_CC,
                        ISD::XOR,        ISD::BSWAP,   ISD::CTPOP,
                        ISD::CTTZ,       ISD::CTLZ,    ISD::VECTOR_SHUFFLE,
                        ISD::SETCC},
                       VT, Expand);
  }

  static const MVT::SimpleValueType FloatVectorTypes[] = {
      MVT::v2f32, MVT::v3f32,  MVT::v4f32, MVT::v5f32, MVT::v6f32, MVT::v7f32,
      MVT::v9f32, MVT::v10f32, MVT::v11f32, MVT::v12f32};

  for (MVT VT : FloatVectorTypes) {
    setOperationAction(
        {ISD::FABS,          ISD::FMINNUM,        ISD::FMAXNUM,
         ISD::FADD,          ISD::FCEIL,          ISD::FCOS,
         ISD::FDIV,          ISD::FEXP2,          ISD::FEXP,
         ISD::FEXP10,        ISD::FLOG2,          ISD::FREM,
         ISD::FLOG,          ISD::FLOG10,         ISD::FPOW,
         ISD::FFLOOR,        ISD::FTRUNC,         ISD::FMUL,
         ISD::FMA,           ISD::FRINT,          ISD::FNEARBYINT,
         ISD::FSQRT,         ISD::FSIN,           ISD::FSUB,
         ISD::FNEG,          ISD::VSELECT,        ISD::SELECT_CC,
         ISD::FCOPYSIGN,     ISD::VECTOR_SHUFFLE, ISD::SETCC,
         ISD::FCANONICALIZE, ISD::FROUNDEVEN},
        VT, Expand);
  }

  // This causes using an unrolled select operation rather than expansion with
  // bit operations. This is in general better, but the alternative using BFI
  // instructions may be better if the select sources are SGPRs.
  setOperationAction(ISD::SELECT, MVT::v2f32, Promote);
  AddPromotedToType(ISD::SELECT, MVT::v2f32, MVT::v2i32);

  setOperationAction(ISD::SELECT, MVT::v3f32, Promote);
  AddPromotedToType(ISD::SELECT, MVT::v3f32, MVT::v3i32);

  setOperationAction(ISD::SELECT, MVT::v4f32, Promote);
  AddPromotedToType(ISD::SELECT, MVT::v4f32, MVT::v4i32);

  setOperationAction(ISD::SELECT, MVT::v5f32, Promote);
  AddPromotedToType(ISD::SELECT, MVT::v5f32, MVT::v5i32);

  setOperationAction(ISD::SELECT, MVT::v6f32, Promote);
  AddPromotedToType(ISD::SELECT, MVT::v6f32, MVT::v6i32);

  setOperationAction(ISD::SELECT, MVT::v7f32, Promote);
  AddPromotedToType(ISD::SELECT, MVT::v7f32, MVT::v7i32);

  setOperationAction(ISD::SELECT, MVT::v9f32, Promote);
  AddPromotedToType(ISD::SELECT, MVT::v9f32, MVT::v9i32);

  setOperationAction(ISD::SELECT, MVT::v10f32, Promote);
  AddPromotedToType(ISD::SELECT, MVT::v10f32, MVT::v10i32);

  setOperationAction(ISD::SELECT, MVT::v11f32, Promote);
  AddPromotedToType(ISD::SELECT, MVT::v11f32, MVT::v11i32);

  setOperationAction(ISD::SELECT, MVT::v12f32, Promote);
  AddPromotedToType(ISD::SELECT, MVT::v12f32, MVT::v12i32);

  setSchedulingPreference(Sched::RegPressure);
  setJumpIsExpensive(true);

  // FIXME: This is only partially true. If we have to do vector compares, any
  // SGPR pair can be a condition register. If we have a uniform condition, we
  // are better off doing SALU operations, where there is only one SCC. For now,
  // we don't have a way of knowing during instruction selection if a condition
  // will be uniform and we always use vector compares. Assume we are using
  // vector compares until that is fixed.
  setHasMultipleConditionRegisters(true);

  setMinCmpXchgSizeInBits(32);
  setSupportsUnalignedAtomics(false);

  PredictableSelectIsExpensive = false;

  // We want to find all load dependencies for long chains of stores to enable
  // merging into very wide vectors. The problem is with vectors with > 4
  // elements. MergeConsecutiveStores will attempt to merge these because x8/x16
  // vectors are a legal type, even though we have to split the loads
  // usually. When we can more precisely specify load legality per address
  // space, we should be able to make FindBetterChain/MergeConsecutiveStores
  // smarter so that they can figure out what to do in 2 iterations without all
  // N > 4 stores on the same chain.
  GatherAllAliasesMaxDepth = 16;

  // memcpy/memmove/memset are expanded in the IR, so we shouldn't need to worry
  // about these during lowering.
  MaxStoresPerMemcpy  = 0xffffffff;
  MaxStoresPerMemmove = 0xffffffff;
  MaxStoresPerMemset  = 0xffffffff;

  // The expansion for 64-bit division is enormous.
  if (AMDGPUBypassSlowDiv)
    addBypassSlowDiv(64, 32);

  setTargetDAGCombine({ISD::BITCAST,    ISD::SHL,
                       ISD::SRA,        ISD::SRL,
                       ISD::TRUNCATE,   ISD::MUL,
                       ISD::SMUL_LOHI,  ISD::UMUL_LOHI,
                       ISD::MULHU,      ISD::MULHS,
                       ISD::SELECT,     ISD::SELECT_CC,
                       ISD::STORE,      ISD::FADD,
                       ISD::FSUB,       ISD::FNEG,
                       ISD::FABS,       ISD::AssertZext,
                       ISD::AssertSext, ISD::INTRINSIC_WO_CHAIN});

  setMaxAtomicSizeInBitsSupported(64);
  setMaxDivRemBitWidthSupported(64);
  setMaxLargeFPConvertBitWidthSupported(64);
}

bool AMDGPUTargetLowering::mayIgnoreSignedZero(SDValue Op) const {
  if (getTargetMachine().Options.NoSignedZerosFPMath)
    return true;

  const auto Flags = Op.getNode()->getFlags();
  if (Flags.hasNoSignedZeros())
    return true;

  return false;
}

//===----------------------------------------------------------------------===//
// Target Information
//===----------------------------------------------------------------------===//

LLVM_READNONE
static bool fnegFoldsIntoOpcode(unsigned Opc) {
  switch (Opc) {
  case ISD::FADD:
  case ISD::FSUB:
  case ISD::FMUL:
  case ISD::FMA:
  case ISD::FMAD:
  case ISD::FMINNUM:
  case ISD::FMAXNUM:
  case ISD::FMINNUM_IEEE:
  case ISD::FMAXNUM_IEEE:
  case ISD::FMINIMUM:
  case ISD::FMAXIMUM:
  case ISD::SELECT:
  case ISD::FSIN:
  case ISD::FTRUNC:
  case ISD::FRINT:
  case ISD::FNEARBYINT:
  case ISD::FROUNDEVEN:
  case ISD::FCANONICALIZE:
  case AMDGPUISD::RCP:
  case AMDGPUISD::RCP_LEGACY:
  case AMDGPUISD::RCP_IFLAG:
  case AMDGPUISD::SIN_HW:
  case AMDGPUISD::FMUL_LEGACY:
  case AMDGPUISD::FMIN_LEGACY:
  case AMDGPUISD::FMAX_LEGACY:
  case AMDGPUISD::FMED3:
    // TODO: handle llvm.amdgcn.fma.legacy
    return true;
  case ISD::BITCAST:
    llvm_unreachable("bitcast is special cased");
  default:
    return false;
  }
}

static bool fnegFoldsIntoOp(const SDNode *N) {
  unsigned Opc = N->getOpcode();
  if (Opc == ISD::BITCAST) {
    // TODO: Is there a benefit to checking the conditions performFNegCombine
    // does? We don't for the other cases.
    SDValue BCSrc = N->getOperand(0);
    if (BCSrc.getOpcode() == ISD::BUILD_VECTOR) {
      return BCSrc.getNumOperands() == 2 &&
             BCSrc.getOperand(1).getValueSizeInBits() == 32;
    }

    return BCSrc.getOpcode() == ISD::SELECT && BCSrc.getValueType() == MVT::f32;
  }

  return fnegFoldsIntoOpcode(Opc);
}

/// \p returns true if the operation will definitely need to use a 64-bit
/// encoding, and thus will use a VOP3 encoding regardless of the source
/// modifiers.
LLVM_READONLY
static bool opMustUseVOP3Encoding(const SDNode *N, MVT VT) {
  return (N->getNumOperands() > 2 && N->getOpcode() != ISD::SELECT) ||
         VT == MVT::f64;
}

/// Return true if v_cndmask_b32 will support fabs/fneg source modifiers for the
/// type for ISD::SELECT.
LLVM_READONLY
static bool selectSupportsSourceMods(const SDNode *N) {
  // TODO: Only applies if select will be vector
  return N->getValueType(0) == MVT::f32;
}

// Most FP instructions support source modifiers, but this could be refined
// slightly.
LLVM_READONLY
static bool hasSourceMods(const SDNode *N) {
  if (isa<MemSDNode>(N))
    return false;

  switch (N->getOpcode()) {
  case ISD::CopyToReg:
  case ISD::FDIV:
  case ISD::FREM:
  case ISD::INLINEASM:
  case ISD::INLINEASM_BR:
  case AMDGPUISD::DIV_SCALE:
  case ISD::INTRINSIC_W_CHAIN:

  // TODO: Should really be looking at the users of the bitcast. These are
  // problematic because bitcasts are used to legalize all stores to integer
  // types.
  case ISD::BITCAST:
    return false;
  case ISD::INTRINSIC_WO_CHAIN: {
    switch (N->getConstantOperandVal(0)) {
    case Intrinsic::amdgcn_interp_p1:
    case Intrinsic::amdgcn_interp_p2:
    case Intrinsic::amdgcn_interp_mov:
    case Intrinsic::amdgcn_interp_p1_f16:
    case Intrinsic::amdgcn_interp_p2_f16:
      return false;
    default:
      return true;
    }
  }
  case ISD::SELECT:
    return selectSupportsSourceMods(N);
  default:
    return true;
  }
}

bool AMDGPUTargetLowering::allUsesHaveSourceMods(const SDNode *N,
                                                 unsigned CostThreshold) {
  // Some users (such as 3-operand FMA/MAD) must use a VOP3 encoding, and thus
  // it is truly free to use a source modifier in all cases. If there are
  // multiple users but for each one will necessitate using VOP3, there will be
  // a code size increase. Try to avoid increasing code size unless we know it
  // will save on the instruction count.
  unsigned NumMayIncreaseSize = 0;
  MVT VT = N->getValueType(0).getScalarType().getSimpleVT();

  assert(!N->use_empty());

  // XXX - Should this limit number of uses to check?
  for (const SDNode *U : N->uses()) {
    if (!hasSourceMods(U))
      return false;

    if (!opMustUseVOP3Encoding(U, VT)) {
      if (++NumMayIncreaseSize > CostThreshold)
        return false;
    }
  }

  return true;
}

EVT AMDGPUTargetLowering::getTypeForExtReturn(LLVMContext &Context, EVT VT,
                                              ISD::NodeType ExtendKind) const {
  assert(!VT.isVector() && "only scalar expected");

  // Round to the next multiple of 32-bits.
  unsigned Size = VT.getSizeInBits();
  if (Size <= 32)
    return MVT::i32;
  return EVT::getIntegerVT(Context, 32 * ((Size + 31) / 32));
}

MVT AMDGPUTargetLowering::getVectorIdxTy(const DataLayout &) const {
  return MVT::i32;
}

bool AMDGPUTargetLowering::isSelectSupported(SelectSupportKind SelType) const {
  return true;
}

// The backend supports 32 and 64 bit floating point immediates.
// FIXME: Why are we reporting vectors of FP immediates as legal?
bool AMDGPUTargetLowering::isFPImmLegal(const APFloat &Imm, EVT VT,
                                        bool ForCodeSize) const {
  EVT ScalarVT = VT.getScalarType();
  return (ScalarVT == MVT::f32 || ScalarVT == MVT::f64 ||
         (ScalarVT == MVT::f16 && Subtarget->has16BitInsts()));
}

// We don't want to shrink f64 / f32 constants.
bool AMDGPUTargetLowering::ShouldShrinkFPConstant(EVT VT) const {
  EVT ScalarVT = VT.getScalarType();
  return (ScalarVT != MVT::f32 && ScalarVT != MVT::f64);
}

bool AMDGPUTargetLowering::shouldReduceLoadWidth(SDNode *N,
                                                 ISD::LoadExtType ExtTy,
                                                 EVT NewVT) const {
  // TODO: This may be worth removing. Check regression tests for diffs.
  if (!TargetLoweringBase::shouldReduceLoadWidth(N, ExtTy, NewVT))
    return false;

  unsigned NewSize = NewVT.getStoreSizeInBits();

  // If we are reducing to a 32-bit load or a smaller multi-dword load,
  // this is always better.
  if (NewSize >= 32)
    return true;

  EVT OldVT = N->getValueType(0);
  unsigned OldSize = OldVT.getStoreSizeInBits();

  MemSDNode *MN = cast<MemSDNode>(N);
  unsigned AS = MN->getAddressSpace();
  // Do not shrink an aligned scalar load to sub-dword.
  // Scalar engine cannot do sub-dword loads.
  // TODO: Update this for GFX12 which does have scalar sub-dword loads.
  if (OldSize >= 32 && NewSize < 32 && MN->getAlign() >= Align(4) &&
      (AS == AMDGPUAS::CONSTANT_ADDRESS ||
       AS == AMDGPUAS::CONSTANT_ADDRESS_32BIT ||
       (isa<LoadSDNode>(N) && AS == AMDGPUAS::GLOBAL_ADDRESS &&
        MN->isInvariant())) &&
      AMDGPUInstrInfo::isUniformMMO(MN->getMemOperand()))
    return false;

  // Don't produce extloads from sub 32-bit types. SI doesn't have scalar
  // extloads, so doing one requires using a buffer_load. In cases where we
  // still couldn't use a scalar load, using the wider load shouldn't really
  // hurt anything.

  // If the old size already had to be an extload, there's no harm in continuing
  // to reduce the width.
  return (OldSize < 32);
}

bool AMDGPUTargetLowering::isLoadBitCastBeneficial(EVT LoadTy, EVT CastTy,
                                                   const SelectionDAG &DAG,
                                                   const MachineMemOperand &MMO) const {

  assert(LoadTy.getSizeInBits() == CastTy.getSizeInBits());

  if (LoadTy.getScalarType() == MVT::i32)
    return false;

  unsigned LScalarSize = LoadTy.getScalarSizeInBits();
  unsigned CastScalarSize = CastTy.getScalarSizeInBits();

  if ((LScalarSize >= CastScalarSize) && (CastScalarSize < 32))
    return false;

  unsigned Fast = 0;
  return allowsMemoryAccessForAlignment(*DAG.getContext(), DAG.getDataLayout(),
                                        CastTy, MMO, &Fast) &&
         Fast;
}

// SI+ has instructions for cttz / ctlz for 32-bit values. This is probably also
// profitable with the expansion for 64-bit since it's generally good to
// speculate things.
bool AMDGPUTargetLowering::isCheapToSpeculateCttz(Type *Ty) const {
  return true;
}

bool AMDGPUTargetLowering::isCheapToSpeculateCtlz(Type *Ty) const {
  return true;
}

bool AMDGPUTargetLowering::isSDNodeAlwaysUniform(const SDNode *N) const {
  switch (N->getOpcode()) {
  case ISD::EntryToken:
  case ISD::TokenFactor:
    return true;
  case ISD::INTRINSIC_WO_CHAIN: {
    unsigned IntrID = N->getConstantOperandVal(0);
    return AMDGPU::isIntrinsicAlwaysUniform(IntrID);
  }
  case ISD::LOAD:
    if (cast<LoadSDNode>(N)->getMemOperand()->getAddrSpace() ==
        AMDGPUAS::CONSTANT_ADDRESS_32BIT)
      return true;
    return false;
  case AMDGPUISD::SETCC: // ballot-style instruction
    return true;
  }
  return false;
}

SDValue AMDGPUTargetLowering::getNegatedExpression(
    SDValue Op, SelectionDAG &DAG, bool LegalOperations, bool ForCodeSize,
    NegatibleCost &Cost, unsigned Depth) const {

  switch (Op.getOpcode()) {
  case ISD::FMA:
  case ISD::FMAD: {
    // Negating a fma is not free if it has users without source mods.
    if (!allUsesHaveSourceMods(Op.getNode()))
      return SDValue();
    break;
  }
  case AMDGPUISD::RCP: {
    SDValue Src = Op.getOperand(0);
    EVT VT = Op.getValueType();
    SDLoc SL(Op);

    SDValue NegSrc = getNegatedExpression(Src, DAG, LegalOperations,
                                          ForCodeSize, Cost, Depth + 1);
    if (NegSrc)
      return DAG.getNode(AMDGPUISD::RCP, SL, VT, NegSrc, Op->getFlags());
    return SDValue();
  }
  default:
    break;
  }

  return TargetLowering::getNegatedExpression(Op, DAG, LegalOperations,
                                              ForCodeSize, Cost, Depth);
}

//===---------------------------------------------------------------------===//
// Target Properties
//===---------------------------------------------------------------------===//

bool AMDGPUTargetLowering::isFAbsFree(EVT VT) const {
  assert(VT.isFloatingPoint());

  // Packed operations do not have a fabs modifier.
  return VT == MVT::f32 || VT == MVT::f64 ||
         (Subtarget->has16BitInsts() && (VT == MVT::f16 || VT == MVT::bf16));
}

bool AMDGPUTargetLowering::isFNegFree(EVT VT) const {
  assert(VT.isFloatingPoint());
  // Report this based on the end legalized type.
  VT = VT.getScalarType();
  return VT == MVT::f32 || VT == MVT::f64 || VT == MVT::f16 || VT == MVT::bf16;
}

bool AMDGPUTargetLowering:: storeOfVectorConstantIsCheap(bool IsZero, EVT MemVT,
                                                         unsigned NumElem,
                                                         unsigned AS) const {
  return true;
}

bool AMDGPUTargetLowering::aggressivelyPreferBuildVectorSources(EVT VecVT) const {
  // There are few operations which truly have vector input operands. Any vector
  // operation is going to involve operations on each component, and a
  // build_vector will be a copy per element, so it always makes sense to use a
  // build_vector input in place of the extracted element to avoid a copy into a
  // super register.
  //
  // We should probably only do this if all users are extracts only, but this
  // should be the common case.
  return true;
}

bool AMDGPUTargetLowering::isTruncateFree(EVT Source, EVT Dest) const {
  // Truncate is just accessing a subregister.

  unsigned SrcSize = Source.getSizeInBits();
  unsigned DestSize = Dest.getSizeInBits();

  return DestSize < SrcSize && DestSize % 32 == 0 ;
}

bool AMDGPUTargetLowering::isTruncateFree(Type *Source, Type *Dest) const {
  // Truncate is just accessing a subregister.

  unsigned SrcSize = Source->getScalarSizeInBits();
  unsigned DestSize = Dest->getScalarSizeInBits();

  if (DestSize== 16 && Subtarget->has16BitInsts())
    return SrcSize >= 32;

  return DestSize < SrcSize && DestSize % 32 == 0;
}

bool AMDGPUTargetLowering::isZExtFree(Type *Src, Type *Dest) const {
  unsigned SrcSize = Src->getScalarSizeInBits();
  unsigned DestSize = Dest->getScalarSizeInBits();

  if (SrcSize == 16 && Subtarget->has16BitInsts())
    return DestSize >= 32;

  return SrcSize == 32 && DestSize == 64;
}

bool AMDGPUTargetLowering::isZExtFree(EVT Src, EVT Dest) const {
  // Any register load of a 64-bit value really requires 2 32-bit moves. For all
  // practical purposes, the extra mov 0 to load a 64-bit is free.  As used,
  // this will enable reducing 64-bit operations the 32-bit, which is always
  // good.

  if (Src == MVT::i16)
    return Dest == MVT::i32 ||Dest == MVT::i64 ;

  return Src == MVT::i32 && Dest == MVT::i64;
}

bool AMDGPUTargetLowering::isNarrowingProfitable(EVT SrcVT, EVT DestVT) const {
  // There aren't really 64-bit registers, but pairs of 32-bit ones and only a
  // limited number of native 64-bit operations. Shrinking an operation to fit
  // in a single 32-bit register should always be helpful. As currently used,
  // this is much less general than the name suggests, and is only used in
  // places trying to reduce the sizes of loads. Shrinking loads to < 32-bits is
  // not profitable, and may actually be harmful.
  return SrcVT.getSizeInBits() > 32 && DestVT.getSizeInBits() == 32;
}

bool AMDGPUTargetLowering::isDesirableToCommuteWithShift(
    const SDNode* N, CombineLevel Level) const {
  assert((N->getOpcode() == ISD::SHL || N->getOpcode() == ISD::SRA ||
          N->getOpcode() == ISD::SRL) &&
         "Expected shift op");
  // Always commute pre-type legalization and right shifts.
  // We're looking for shl(or(x,y),z) patterns.
  if (Level < CombineLevel::AfterLegalizeTypes ||
      N->getOpcode() != ISD::SHL || N->getOperand(0).getOpcode() != ISD::OR)
    return true;

  // If only user is a i32 right-shift, then don't destroy a BFE pattern.
  if (N->getValueType(0) == MVT::i32 && N->use_size() == 1 &&
      (N->use_begin()->getOpcode() == ISD::SRA ||
       N->use_begin()->getOpcode() == ISD::SRL))
    return false;

  // Don't destroy or(shl(load_zext(),c), load_zext()) patterns.
  auto IsShiftAndLoad = [](SDValue LHS, SDValue RHS) {
    if (LHS.getOpcode() != ISD::SHL)
      return false;
    auto *RHSLd = dyn_cast<LoadSDNode>(RHS);
    auto *LHS0 = dyn_cast<LoadSDNode>(LHS.getOperand(0));
    auto *LHS1 = dyn_cast<ConstantSDNode>(LHS.getOperand(1));
    return LHS0 && LHS1 && RHSLd && LHS0->getExtensionType() == ISD::ZEXTLOAD &&
           LHS1->getAPIntValue() == LHS0->getMemoryVT().getScalarSizeInBits() &&
           RHSLd->getExtensionType() == ISD::ZEXTLOAD;
  };
  SDValue LHS = N->getOperand(0).getOperand(0);
  SDValue RHS = N->getOperand(0).getOperand(1);
  return !(IsShiftAndLoad(LHS, RHS) || IsShiftAndLoad(RHS, LHS));
}

//===---------------------------------------------------------------------===//
// TargetLowering Callbacks
//===---------------------------------------------------------------------===//

CCAssignFn *AMDGPUCallLowering::CCAssignFnForCall(CallingConv::ID CC,
                                                  bool IsVarArg) {
  switch (CC) {
  case CallingConv::AMDGPU_VS:
  case CallingConv::AMDGPU_GS:
  case CallingConv::AMDGPU_PS:
  case CallingConv::AMDGPU_CS:
  case CallingConv::AMDGPU_HS:
  case CallingConv::AMDGPU_ES:
  case CallingConv::AMDGPU_LS:
    return CC_AMDGPU;
  case CallingConv::AMDGPU_CS_Chain:
  case CallingConv::AMDGPU_CS_ChainPreserve:
    return CC_AMDGPU_CS_CHAIN;
  case CallingConv::C:
  case CallingConv::Fast:
  case CallingConv::Cold:
    return CC_AMDGPU_Func;
  case CallingConv::AMDGPU_Gfx:
    return CC_SI_Gfx;
  case CallingConv::AMDGPU_KERNEL:
  case CallingConv::SPIR_KERNEL:
  default:
    report_fatal_error("Unsupported calling convention for call");
  }
}

CCAssignFn *AMDGPUCallLowering::CCAssignFnForReturn(CallingConv::ID CC,
                                                    bool IsVarArg) {
  switch (CC) {
  case CallingConv::AMDGPU_KERNEL:
  case CallingConv::SPIR_KERNEL:
    llvm_unreachable("kernels should not be handled here");
  case CallingConv::AMDGPU_VS:
  case CallingConv::AMDGPU_GS:
  case CallingConv::AMDGPU_PS:
  case CallingConv::AMDGPU_CS:
  case CallingConv::AMDGPU_CS_Chain:
  case CallingConv::AMDGPU_CS_ChainPreserve:
  case CallingConv::AMDGPU_HS:
  case CallingConv::AMDGPU_ES:
  case CallingConv::AMDGPU_LS:
    return RetCC_SI_Shader;
  case CallingConv::AMDGPU_Gfx:
    return RetCC_SI_Gfx;
  case CallingConv::C:
  case CallingConv::Fast:
  case CallingConv::Cold:
    return RetCC_AMDGPU_Func;
  default:
    report_fatal_error("Unsupported calling convention.");
  }
}

/// The SelectionDAGBuilder will automatically promote function arguments
/// with illegal types.  However, this does not work for the AMDGPU targets
/// since the function arguments are stored in memory as these illegal types.
/// In order to handle this properly we need to get the original types sizes
/// from the LLVM IR Function and fixup the ISD:InputArg values before
/// passing them to AnalyzeFormalArguments()

/// When the SelectionDAGBuilder computes the Ins, it takes care of splitting
/// input values across multiple registers.  Each item in the Ins array
/// represents a single value that will be stored in registers.  Ins[x].VT is
/// the value type of the value that will be stored in the register, so
/// whatever SDNode we lower the argument to needs to be this type.
///
/// In order to correctly lower the arguments we need to know the size of each
/// argument.  Since Ins[x].VT gives us the size of the register that will
/// hold the value, we need to look at Ins[x].ArgVT to see the 'real' type
/// for the original function argument so that we can deduce the correct memory
/// type to use for Ins[x].  In most cases the correct memory type will be
/// Ins[x].ArgVT.  However, this will not always be the case.  If, for example,
/// we have a kernel argument of type v8i8, this argument will be split into
/// 8 parts and each part will be represented by its own item in the Ins array.
/// For each part the Ins[x].ArgVT will be the v8i8, which is the full type of
/// the argument before it was split.  From this, we deduce that the memory type
/// for each individual part is i8.  We pass the memory type as LocVT to the
/// calling convention analysis function and the register type (Ins[x].VT) as
/// the ValVT.
void AMDGPUTargetLowering::analyzeFormalArgumentsCompute(
  CCState &State,
  const SmallVectorImpl<ISD::InputArg> &Ins) const {
  const MachineFunction &MF = State.getMachineFunction();
  const Function &Fn = MF.getFunction();
  LLVMContext &Ctx = Fn.getParent()->getContext();
  const AMDGPUSubtarget &ST = AMDGPUSubtarget::get(MF);
  const unsigned ExplicitOffset = ST.getExplicitKernelArgOffset();
  CallingConv::ID CC = Fn.getCallingConv();

  Align MaxAlign = Align(1);
  uint64_t ExplicitArgOffset = 0;
  const DataLayout &DL = Fn.getDataLayout();

  unsigned InIndex = 0;

  for (const Argument &Arg : Fn.args()) {
    const bool IsByRef = Arg.hasByRefAttr();
    Type *BaseArgTy = Arg.getType();
    Type *MemArgTy = IsByRef ? Arg.getParamByRefType() : BaseArgTy;
    Align Alignment = DL.getValueOrABITypeAlignment(
        IsByRef ? Arg.getParamAlign() : std::nullopt, MemArgTy);
    MaxAlign = std::max(Alignment, MaxAlign);
    uint64_t AllocSize = DL.getTypeAllocSize(MemArgTy);

    uint64_t ArgOffset = alignTo(ExplicitArgOffset, Alignment) + ExplicitOffset;
    ExplicitArgOffset = alignTo(ExplicitArgOffset, Alignment) + AllocSize;

    // We're basically throwing away everything passed into us and starting over
    // to get accurate in-memory offsets. The "PartOffset" is completely useless
    // to us as computed in Ins.
    //
    // We also need to figure out what type legalization is trying to do to get
    // the correct memory offsets.

    SmallVector<EVT, 16> ValueVTs;
    SmallVector<uint64_t, 16> Offsets;
    ComputeValueVTs(*this, DL, BaseArgTy, ValueVTs, &Offsets, ArgOffset);

    for (unsigned Value = 0, NumValues = ValueVTs.size();
         Value != NumValues; ++Value) {
      uint64_t BasePartOffset = Offsets[Value];

      EVT ArgVT = ValueVTs[Value];
      EVT MemVT = ArgVT;
      MVT RegisterVT = getRegisterTypeForCallingConv(Ctx, CC, ArgVT);
      unsigned NumRegs = getNumRegistersForCallingConv(Ctx, CC, ArgVT);

      if (NumRegs == 1) {
        // This argument is not split, so the IR type is the memory type.
        if (ArgVT.isExtended()) {
          // We have an extended type, like i24, so we should just use the
          // register type.
          MemVT = RegisterVT;
        } else {
          MemVT = ArgVT;
        }
      } else if (ArgVT.isVector() && RegisterVT.isVector() &&
                 ArgVT.getScalarType() == RegisterVT.getScalarType()) {
        assert(ArgVT.getVectorNumElements() > RegisterVT.getVectorNumElements());
        // We have a vector value which has been split into a vector with
        // the same scalar type, but fewer elements.  This should handle
        // all the floating-point vector types.
        MemVT = RegisterVT;
      } else if (ArgVT.isVector() &&
                 ArgVT.getVectorNumElements() == NumRegs) {
        // This arg has been split so that each element is stored in a separate
        // register.
        MemVT = ArgVT.getScalarType();
      } else if (ArgVT.isExtended()) {
        // We have an extended type, like i65.
        MemVT = RegisterVT;
      } else {
        unsigned MemoryBits = ArgVT.getStoreSizeInBits() / NumRegs;
        assert(ArgVT.getStoreSizeInBits() % NumRegs == 0);
        if (RegisterVT.isInteger()) {
          MemVT = EVT::getIntegerVT(State.getContext(), MemoryBits);
        } else if (RegisterVT.isVector()) {
          assert(!RegisterVT.getScalarType().isFloatingPoint());
          unsigned NumElements = RegisterVT.getVectorNumElements();
          assert(MemoryBits % NumElements == 0);
          // This vector type has been split into another vector type with
          // a different elements size.
          EVT ScalarVT = EVT::getIntegerVT(State.getContext(),
                                           MemoryBits / NumElements);
          MemVT = EVT::getVectorVT(State.getContext(), ScalarVT, NumElements);
        } else {
          llvm_unreachable("cannot deduce memory type.");
        }
      }

      // Convert one element vectors to scalar.
      if (MemVT.isVector() && MemVT.getVectorNumElements() == 1)
        MemVT = MemVT.getScalarType();

      // Round up vec3/vec5 argument.
      if (MemVT.isVector() && !MemVT.isPow2VectorType()) {
        assert(MemVT.getVectorNumElements() == 3 ||
               MemVT.getVectorNumElements() == 5 ||
               (MemVT.getVectorNumElements() >= 9 &&
                MemVT.getVectorNumElements() <= 12));
        MemVT = MemVT.getPow2VectorType(State.getContext());
      } else if (!MemVT.isSimple() && !MemVT.isVector()) {
        MemVT = MemVT.getRoundIntegerType(State.getContext());
      }

      unsigned PartOffset = 0;
      for (unsigned i = 0; i != NumRegs; ++i) {
        State.addLoc(CCValAssign::getCustomMem(InIndex++, RegisterVT,
                                               BasePartOffset + PartOffset,
                                               MemVT.getSimpleVT(),
                                               CCValAssign::Full));
        PartOffset += MemVT.getStoreSize();
      }
    }
  }
}

SDValue AMDGPUTargetLowering::LowerReturn(
  SDValue Chain, CallingConv::ID CallConv,
  bool isVarArg,
  const SmallVectorImpl<ISD::OutputArg> &Outs,
  const SmallVectorImpl<SDValue> &OutVals,
  const SDLoc &DL, SelectionDAG &DAG) const {
  // FIXME: Fails for r600 tests
  //assert(!isVarArg && Outs.empty() && OutVals.empty() &&
  // "wave terminate should not have return values");
  return DAG.getNode(AMDGPUISD::ENDPGM, DL, MVT::Other, Chain);
}

//===---------------------------------------------------------------------===//
// Target specific lowering
//===---------------------------------------------------------------------===//

/// Selects the correct CCAssignFn for a given CallingConvention value.
CCAssignFn *AMDGPUTargetLowering::CCAssignFnForCall(CallingConv::ID CC,
                                                    bool IsVarArg) {
  return AMDGPUCallLowering::CCAssignFnForCall(CC, IsVarArg);
}

CCAssignFn *AMDGPUTargetLowering::CCAssignFnForReturn(CallingConv::ID CC,
                                                      bool IsVarArg) {
  return AMDGPUCallLowering::CCAssignFnForReturn(CC, IsVarArg);
}

SDValue AMDGPUTargetLowering::addTokenForArgument(SDValue Chain,
                                                  SelectionDAG &DAG,
                                                  MachineFrameInfo &MFI,
                                                  int ClobberedFI) const {
  SmallVector<SDValue, 8> ArgChains;
  int64_t FirstByte = MFI.getObjectOffset(ClobberedFI);
  int64_t LastByte = FirstByte + MFI.getObjectSize(ClobberedFI) - 1;

  // Include the original chain at the beginning of the list. When this is
  // used by target LowerCall hooks, this helps legalize find the
  // CALLSEQ_BEGIN node.
  ArgChains.push_back(Chain);

  // Add a chain value for each stack argument corresponding
  for (SDNode *U : DAG.getEntryNode().getNode()->uses()) {
    if (LoadSDNode *L = dyn_cast<LoadSDNode>(U)) {
      if (FrameIndexSDNode *FI = dyn_cast<FrameIndexSDNode>(L->getBasePtr())) {
        if (FI->getIndex() < 0) {
          int64_t InFirstByte = MFI.getObjectOffset(FI->getIndex());
          int64_t InLastByte = InFirstByte;
          InLastByte += MFI.getObjectSize(FI->getIndex()) - 1;

          if ((InFirstByte <= FirstByte && FirstByte <= InLastByte) ||
              (FirstByte <= InFirstByte && InFirstByte <= LastByte))
            ArgChains.push_back(SDValue(L, 1));
        }
      }
    }
  }

  // Build a tokenfactor for all the chains.
  return DAG.getNode(ISD::TokenFactor, SDLoc(Chain), MVT::Other, ArgChains);
}

SDValue AMDGPUTargetLowering::lowerUnhandledCall(CallLoweringInfo &CLI,
                                                 SmallVectorImpl<SDValue> &InVals,
                                                 StringRef Reason) const {
  SDValue Callee = CLI.Callee;
  SelectionDAG &DAG = CLI.DAG;

  const Function &Fn = DAG.getMachineFunction().getFunction();

  StringRef FuncName("<unknown>");

  if (const ExternalSymbolSDNode *G = dyn_cast<ExternalSymbolSDNode>(Callee))
    FuncName = G->getSymbol();
  else if (const GlobalAddressSDNode *G = dyn_cast<GlobalAddressSDNode>(Callee))
    FuncName = G->getGlobal()->getName();

  DiagnosticInfoUnsupported NoCalls(
    Fn, Reason + FuncName, CLI.DL.getDebugLoc());
  DAG.getContext()->diagnose(NoCalls);

  if (!CLI.IsTailCall) {
    for (ISD::InputArg &Arg : CLI.Ins)
      InVals.push_back(DAG.getUNDEF(Arg.VT));
  }

  return DAG.getEntryNode();
}

SDValue AMDGPUTargetLowering::LowerCall(CallLoweringInfo &CLI,
                                        SmallVectorImpl<SDValue> &InVals) const {
  return lowerUnhandledCall(CLI, InVals, "unsupported call to function ");
}

SDValue AMDGPUTargetLowering::LowerDYNAMIC_STACKALLOC(SDValue Op,
                                                      SelectionDAG &DAG) const {
  const Function &Fn = DAG.getMachineFunction().getFunction();

  DiagnosticInfoUnsupported NoDynamicAlloca(Fn, "unsupported dynamic alloca",
                                            SDLoc(Op).getDebugLoc());
  DAG.getContext()->diagnose(NoDynamicAlloca);
  auto Ops = {DAG.getConstant(0, SDLoc(), Op.getValueType()), Op.getOperand(0)};
  return DAG.getMergeValues(Ops, SDLoc());
}

SDValue AMDGPUTargetLowering::LowerOperation(SDValue Op,
                                             SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  default:
    Op->print(errs(), &DAG);
    llvm_unreachable("Custom lowering code for this "
                     "instruction is not implemented yet!");
    break;
  case ISD::SIGN_EXTEND_INREG: return LowerSIGN_EXTEND_INREG(Op, DAG);
  case ISD::CONCAT_VECTORS: return LowerCONCAT_VECTORS(Op, DAG);
  case ISD::EXTRACT_SUBVECTOR: return LowerEXTRACT_SUBVECTOR(Op, DAG);
  case ISD::UDIVREM: return LowerUDIVREM(Op, DAG);
  case ISD::SDIVREM: return LowerSDIVREM(Op, DAG);
  case ISD::FREM: return LowerFREM(Op, DAG);
  case ISD::FCEIL: return LowerFCEIL(Op, DAG);
  case ISD::FTRUNC: return LowerFTRUNC(Op, DAG);
  case ISD::FRINT: return LowerFRINT(Op, DAG);
  case ISD::FNEARBYINT: return LowerFNEARBYINT(Op, DAG);
  case ISD::FROUNDEVEN:
    return LowerFROUNDEVEN(Op, DAG);
  case ISD::FROUND: return LowerFROUND(Op, DAG);
  case ISD::FFLOOR: return LowerFFLOOR(Op, DAG);
  case ISD::FLOG2:
    return LowerFLOG2(Op, DAG);
  case ISD::FLOG:
  case ISD::FLOG10:
    return LowerFLOGCommon(Op, DAG);
  case ISD::FEXP:
  case ISD::FEXP10:
    return lowerFEXP(Op, DAG);
  case ISD::FEXP2:
    return lowerFEXP2(Op, DAG);
  case ISD::SINT_TO_FP: return LowerSINT_TO_FP(Op, DAG);
  case ISD::UINT_TO_FP: return LowerUINT_TO_FP(Op, DAG);
  case ISD::FP_TO_FP16: return LowerFP_TO_FP16(Op, DAG);
  case ISD::FP_TO_SINT:
  case ISD::FP_TO_UINT:
    return LowerFP_TO_INT(Op, DAG);
  case ISD::CTTZ:
  case ISD::CTTZ_ZERO_UNDEF:
  case ISD::CTLZ:
  case ISD::CTLZ_ZERO_UNDEF:
    return LowerCTLZ_CTTZ(Op, DAG);
  case ISD::DYNAMIC_STACKALLOC: return LowerDYNAMIC_STACKALLOC(Op, DAG);
  }
  return Op;
}

void AMDGPUTargetLowering::ReplaceNodeResults(SDNode *N,
                                              SmallVectorImpl<SDValue> &Results,
                                              SelectionDAG &DAG) const {
  switch (N->getOpcode()) {
  case ISD::SIGN_EXTEND_INREG:
    // Different parts of legalization seem to interpret which type of
    // sign_extend_inreg is the one to check for custom lowering. The extended
    // from type is what really matters, but some places check for custom
    // lowering of the result type. This results in trying to use
    // ReplaceNodeResults to sext_in_reg to an illegal type, so we'll just do
    // nothing here and let the illegal result integer be handled normally.
    return;
  case ISD::FLOG2:
    if (SDValue Lowered = LowerFLOG2(SDValue(N, 0), DAG))
      Results.push_back(Lowered);
    return;
  case ISD::FLOG:
  case ISD::FLOG10:
    if (SDValue Lowered = LowerFLOGCommon(SDValue(N, 0), DAG))
      Results.push_back(Lowered);
    return;
  case ISD::FEXP2:
    if (SDValue Lowered = lowerFEXP2(SDValue(N, 0), DAG))
      Results.push_back(Lowered);
    return;
  case ISD::FEXP:
  case ISD::FEXP10:
    if (SDValue Lowered = lowerFEXP(SDValue(N, 0), DAG))
      Results.push_back(Lowered);
    return;
  case ISD::CTLZ:
  case ISD::CTLZ_ZERO_UNDEF:
    if (auto Lowered = lowerCTLZResults(SDValue(N, 0u), DAG))
      Results.push_back(Lowered);
    return;
  default:
    return;
  }
}

SDValue AMDGPUTargetLowering::LowerGlobalAddress(AMDGPUMachineFunction* MFI,
                                                 SDValue Op,
                                                 SelectionDAG &DAG) const {

  const DataLayout &DL = DAG.getDataLayout();
  GlobalAddressSDNode *G = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = G->getGlobal();

  if (!MFI->isModuleEntryFunction()) {
    if (std::optional<uint32_t> Address =
            AMDGPUMachineFunction::getLDSAbsoluteAddress(*GV)) {
      return DAG.getConstant(*Address, SDLoc(Op), Op.getValueType());
    }
  }

  if (G->getAddressSpace() == AMDGPUAS::LOCAL_ADDRESS ||
      G->getAddressSpace() == AMDGPUAS::REGION_ADDRESS) {
    if (!MFI->isModuleEntryFunction() &&
        GV->getName() != "llvm.amdgcn.module.lds") {
      SDLoc DL(Op);
      const Function &Fn = DAG.getMachineFunction().getFunction();
      DiagnosticInfoUnsupported BadLDSDecl(
        Fn, "local memory global used by non-kernel function",
        DL.getDebugLoc(), DS_Warning);
      DAG.getContext()->diagnose(BadLDSDecl);

      // We currently don't have a way to correctly allocate LDS objects that
      // aren't directly associated with a kernel. We do force inlining of
      // functions that use local objects. However, if these dead functions are
      // not eliminated, we don't want a compile time error. Just emit a warning
      // and a trap, since there should be no callable path here.
      SDValue Trap = DAG.getNode(ISD::TRAP, DL, MVT::Other, DAG.getEntryNode());
      SDValue OutputChain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other,
                                        Trap, DAG.getRoot());
      DAG.setRoot(OutputChain);
      return DAG.getUNDEF(Op.getValueType());
    }

    // XXX: What does the value of G->getOffset() mean?
    assert(G->getOffset() == 0 &&
         "Do not know what to do with an non-zero offset");

    // TODO: We could emit code to handle the initialization somewhere.
    // We ignore the initializer for now and legalize it to allow selection.
    // The initializer will anyway get errored out during assembly emission.
    unsigned Offset = MFI->allocateLDSGlobal(DL, *cast<GlobalVariable>(GV));
    return DAG.getConstant(Offset, SDLoc(Op), Op.getValueType());
  }
  return SDValue();
}

SDValue AMDGPUTargetLowering::LowerCONCAT_VECTORS(SDValue Op,
                                                  SelectionDAG &DAG) const {
  SmallVector<SDValue, 8> Args;
  SDLoc SL(Op);

  EVT VT = Op.getValueType();
  if (VT.getVectorElementType().getSizeInBits() < 32) {
    unsigned OpBitSize = Op.getOperand(0).getValueType().getSizeInBits();
    if (OpBitSize >= 32 && OpBitSize % 32 == 0) {
      unsigned NewNumElt = OpBitSize / 32;
      EVT NewEltVT = (NewNumElt == 1) ? MVT::i32
                                      : EVT::getVectorVT(*DAG.getContext(),
                                                         MVT::i32, NewNumElt);
      for (const SDUse &U : Op->ops()) {
        SDValue In = U.get();
        SDValue NewIn = DAG.getNode(ISD::BITCAST, SL, NewEltVT, In);
        if (NewNumElt > 1)
          DAG.ExtractVectorElements(NewIn, Args);
        else
          Args.push_back(NewIn);
      }

      EVT NewVT = EVT::getVectorVT(*DAG.getContext(), MVT::i32,
                                   NewNumElt * Op.getNumOperands());
      SDValue BV = DAG.getBuildVector(NewVT, SL, Args);
      return DAG.getNode(ISD::BITCAST, SL, VT, BV);
    }
  }

  for (const SDUse &U : Op->ops())
    DAG.ExtractVectorElements(U.get(), Args);

  return DAG.getBuildVector(Op.getValueType(), SL, Args);
}

SDValue AMDGPUTargetLowering::LowerEXTRACT_SUBVECTOR(SDValue Op,
                                                     SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SmallVector<SDValue, 8> Args;
  unsigned Start = Op.getConstantOperandVal(1);
  EVT VT = Op.getValueType();
  EVT SrcVT = Op.getOperand(0).getValueType();

  if (VT.getScalarSizeInBits() == 16 && Start % 2 == 0) {
    unsigned NumElt = VT.getVectorNumElements();
    unsigned NumSrcElt = SrcVT.getVectorNumElements();
    assert(NumElt % 2 == 0 && NumSrcElt % 2 == 0 && "expect legal types");

    // Extract 32-bit registers at a time.
    EVT NewSrcVT = EVT::getVectorVT(*DAG.getContext(), MVT::i32, NumSrcElt / 2);
    EVT NewVT = NumElt == 2
                    ? MVT::i32
                    : EVT::getVectorVT(*DAG.getContext(), MVT::i32, NumElt / 2);
    SDValue Tmp = DAG.getNode(ISD::BITCAST, SL, NewSrcVT, Op.getOperand(0));

    DAG.ExtractVectorElements(Tmp, Args, Start / 2, NumElt / 2);
    if (NumElt == 2)
      Tmp = Args[0];
    else
      Tmp = DAG.getBuildVector(NewVT, SL, Args);

    return DAG.getNode(ISD::BITCAST, SL, VT, Tmp);
  }

  DAG.ExtractVectorElements(Op.getOperand(0), Args, Start,
                            VT.getVectorNumElements());

  return DAG.getBuildVector(Op.getValueType(), SL, Args);
}

// TODO: Handle fabs too
static SDValue peekFNeg(SDValue Val) {
  if (Val.getOpcode() == ISD::FNEG)
    return Val.getOperand(0);

  return Val;
}

static SDValue peekFPSignOps(SDValue Val) {
  if (Val.getOpcode() == ISD::FNEG)
    Val = Val.getOperand(0);
  if (Val.getOpcode() == ISD::FABS)
    Val = Val.getOperand(0);
  if (Val.getOpcode() == ISD::FCOPYSIGN)
    Val = Val.getOperand(0);
  return Val;
}

SDValue AMDGPUTargetLowering::combineFMinMaxLegacyImpl(
    const SDLoc &DL, EVT VT, SDValue LHS, SDValue RHS, SDValue True,
    SDValue False, SDValue CC, DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  ISD::CondCode CCOpcode = cast<CondCodeSDNode>(CC)->get();
  switch (CCOpcode) {
  case ISD::SETOEQ:
  case ISD::SETONE:
  case ISD::SETUNE:
  case ISD::SETNE:
  case ISD::SETUEQ:
  case ISD::SETEQ:
  case ISD::SETFALSE:
  case ISD::SETFALSE2:
  case ISD::SETTRUE:
  case ISD::SETTRUE2:
  case ISD::SETUO:
  case ISD::SETO:
    break;
  case ISD::SETULE:
  case ISD::SETULT: {
    if (LHS == True)
      return DAG.getNode(AMDGPUISD::FMIN_LEGACY, DL, VT, RHS, LHS);
    return DAG.getNode(AMDGPUISD::FMAX_LEGACY, DL, VT, LHS, RHS);
  }
  case ISD::SETOLE:
  case ISD::SETOLT:
  case ISD::SETLE:
  case ISD::SETLT: {
    // Ordered. Assume ordered for undefined.

    // Only do this after legalization to avoid interfering with other combines
    // which might occur.
    if (DCI.getDAGCombineLevel() < AfterLegalizeDAG &&
        !DCI.isCalledByLegalizer())
      return SDValue();

    // We need to permute the operands to get the correct NaN behavior. The
    // selected operand is the second one based on the failing compare with NaN,
    // so permute it based on the compare type the hardware uses.
    if (LHS == True)
      return DAG.getNode(AMDGPUISD::FMIN_LEGACY, DL, VT, LHS, RHS);
    return DAG.getNode(AMDGPUISD::FMAX_LEGACY, DL, VT, RHS, LHS);
  }
  case ISD::SETUGE:
  case ISD::SETUGT: {
    if (LHS == True)
      return DAG.getNode(AMDGPUISD::FMAX_LEGACY, DL, VT, RHS, LHS);
    return DAG.getNode(AMDGPUISD::FMIN_LEGACY, DL, VT, LHS, RHS);
  }
  case ISD::SETGT:
  case ISD::SETGE:
  case ISD::SETOGE:
  case ISD::SETOGT: {
    if (DCI.getDAGCombineLevel() < AfterLegalizeDAG &&
        !DCI.isCalledByLegalizer())
      return SDValue();

    if (LHS == True)
      return DAG.getNode(AMDGPUISD::FMAX_LEGACY, DL, VT, LHS, RHS);
    return DAG.getNode(AMDGPUISD::FMIN_LEGACY, DL, VT, RHS, LHS);
  }
  case ISD::SETCC_INVALID:
    llvm_unreachable("Invalid setcc condcode!");
  }
  return SDValue();
}

/// Generate Min/Max node
SDValue AMDGPUTargetLowering::combineFMinMaxLegacy(const SDLoc &DL, EVT VT,
                                                   SDValue LHS, SDValue RHS,
                                                   SDValue True, SDValue False,
                                                   SDValue CC,
                                                   DAGCombinerInfo &DCI) const {
  if ((LHS == True && RHS == False) || (LHS == False && RHS == True))
    return combineFMinMaxLegacyImpl(DL, VT, LHS, RHS, True, False, CC, DCI);

  SelectionDAG &DAG = DCI.DAG;

  // If we can't directly match this, try to see if we can fold an fneg to
  // match.

  ConstantFPSDNode *CRHS = dyn_cast<ConstantFPSDNode>(RHS);
  ConstantFPSDNode *CFalse = dyn_cast<ConstantFPSDNode>(False);
  SDValue NegTrue = peekFNeg(True);

  // Undo the combine foldFreeOpFromSelect does if it helps us match the
  // fmin/fmax.
  //
  // select (fcmp olt (lhs, K)), (fneg lhs), -K
  // -> fneg (fmin_legacy lhs, K)
  //
  // TODO: Use getNegatedExpression
  if (LHS == NegTrue && CFalse && CRHS) {
    APFloat NegRHS = neg(CRHS->getValueAPF());
    if (NegRHS == CFalse->getValueAPF()) {
      SDValue Combined =
          combineFMinMaxLegacyImpl(DL, VT, LHS, RHS, NegTrue, False, CC, DCI);
      if (Combined)
        return DAG.getNode(ISD::FNEG, DL, VT, Combined);
      return SDValue();
    }
  }

  return SDValue();
}

std::pair<SDValue, SDValue>
AMDGPUTargetLowering::split64BitValue(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);

  SDValue Vec = DAG.getNode(ISD::BITCAST, SL, MVT::v2i32, Op);

  const SDValue Zero = DAG.getConstant(0, SL, MVT::i32);
  const SDValue One = DAG.getConstant(1, SL, MVT::i32);

  SDValue Lo = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, Vec, Zero);
  SDValue Hi = DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, Vec, One);

  return std::pair(Lo, Hi);
}

SDValue AMDGPUTargetLowering::getLoHalf64(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);

  SDValue Vec = DAG.getNode(ISD::BITCAST, SL, MVT::v2i32, Op);
  const SDValue Zero = DAG.getConstant(0, SL, MVT::i32);
  return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, Vec, Zero);
}

SDValue AMDGPUTargetLowering::getHiHalf64(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);

  SDValue Vec = DAG.getNode(ISD::BITCAST, SL, MVT::v2i32, Op);
  const SDValue One = DAG.getConstant(1, SL, MVT::i32);
  return DAG.getNode(ISD::EXTRACT_VECTOR_ELT, SL, MVT::i32, Vec, One);
}

// Split a vector type into two parts. The first part is a power of two vector.
// The second part is whatever is left over, and is a scalar if it would
// otherwise be a 1-vector.
std::pair<EVT, EVT>
AMDGPUTargetLowering::getSplitDestVTs(const EVT &VT, SelectionDAG &DAG) const {
  EVT LoVT, HiVT;
  EVT EltVT = VT.getVectorElementType();
  unsigned NumElts = VT.getVectorNumElements();
  unsigned LoNumElts = PowerOf2Ceil((NumElts + 1) / 2);
  LoVT = EVT::getVectorVT(*DAG.getContext(), EltVT, LoNumElts);
  HiVT = NumElts - LoNumElts == 1
             ? EltVT
             : EVT::getVectorVT(*DAG.getContext(), EltVT, NumElts - LoNumElts);
  return std::pair(LoVT, HiVT);
}

// Split a vector value into two parts of types LoVT and HiVT. HiVT could be
// scalar.
std::pair<SDValue, SDValue>
AMDGPUTargetLowering::splitVector(const SDValue &N, const SDLoc &DL,
                                  const EVT &LoVT, const EVT &HiVT,
                                  SelectionDAG &DAG) const {
  assert(LoVT.getVectorNumElements() +
                 (HiVT.isVector() ? HiVT.getVectorNumElements() : 1) <=
             N.getValueType().getVectorNumElements() &&
         "More vector elements requested than available!");
  SDValue Lo = DAG.getNode(ISD::EXTRACT_SUBVECTOR, DL, LoVT, N,
                           DAG.getVectorIdxConstant(0, DL));
  SDValue Hi = DAG.getNode(
      HiVT.isVector() ? ISD::EXTRACT_SUBVECTOR : ISD::EXTRACT_VECTOR_ELT, DL,
      HiVT, N, DAG.getVectorIdxConstant(LoVT.getVectorNumElements(), DL));
  return std::pair(Lo, Hi);
}

SDValue AMDGPUTargetLowering::SplitVectorLoad(const SDValue Op,
                                              SelectionDAG &DAG) const {
  LoadSDNode *Load = cast<LoadSDNode>(Op);
  EVT VT = Op.getValueType();
  SDLoc SL(Op);


  // If this is a 2 element vector, we really want to scalarize and not create
  // weird 1 element vectors.
  if (VT.getVectorNumElements() == 2) {
    SDValue Ops[2];
    std::tie(Ops[0], Ops[1]) = scalarizeVectorLoad(Load, DAG);
    return DAG.getMergeValues(Ops, SL);
  }

  SDValue BasePtr = Load->getBasePtr();
  EVT MemVT = Load->getMemoryVT();

  const MachinePointerInfo &SrcValue = Load->getMemOperand()->getPointerInfo();

  EVT LoVT, HiVT;
  EVT LoMemVT, HiMemVT;
  SDValue Lo, Hi;

  std::tie(LoVT, HiVT) = getSplitDestVTs(VT, DAG);
  std::tie(LoMemVT, HiMemVT) = getSplitDestVTs(MemVT, DAG);
  std::tie(Lo, Hi) = splitVector(Op, SL, LoVT, HiVT, DAG);

  unsigned Size = LoMemVT.getStoreSize();
  Align BaseAlign = Load->getAlign();
  Align HiAlign = commonAlignment(BaseAlign, Size);

  SDValue LoLoad = DAG.getExtLoad(Load->getExtensionType(), SL, LoVT,
                                  Load->getChain(), BasePtr, SrcValue, LoMemVT,
                                  BaseAlign, Load->getMemOperand()->getFlags());
  SDValue HiPtr = DAG.getObjectPtrOffset(SL, BasePtr, TypeSize::getFixed(Size));
  SDValue HiLoad =
      DAG.getExtLoad(Load->getExtensionType(), SL, HiVT, Load->getChain(),
                     HiPtr, SrcValue.getWithOffset(LoMemVT.getStoreSize()),
                     HiMemVT, HiAlign, Load->getMemOperand()->getFlags());

  SDValue Join;
  if (LoVT == HiVT) {
    // This is the case that the vector is power of two so was evenly split.
    Join = DAG.getNode(ISD::CONCAT_VECTORS, SL, VT, LoLoad, HiLoad);
  } else {
    Join = DAG.getNode(ISD::INSERT_SUBVECTOR, SL, VT, DAG.getUNDEF(VT), LoLoad,
                       DAG.getVectorIdxConstant(0, SL));
    Join = DAG.getNode(
        HiVT.isVector() ? ISD::INSERT_SUBVECTOR : ISD::INSERT_VECTOR_ELT, SL,
        VT, Join, HiLoad,
        DAG.getVectorIdxConstant(LoVT.getVectorNumElements(), SL));
  }

  SDValue Ops[] = {Join, DAG.getNode(ISD::TokenFactor, SL, MVT::Other,
                                     LoLoad.getValue(1), HiLoad.getValue(1))};

  return DAG.getMergeValues(Ops, SL);
}

SDValue AMDGPUTargetLowering::WidenOrSplitVectorLoad(SDValue Op,
                                                     SelectionDAG &DAG) const {
  LoadSDNode *Load = cast<LoadSDNode>(Op);
  EVT VT = Op.getValueType();
  SDValue BasePtr = Load->getBasePtr();
  EVT MemVT = Load->getMemoryVT();
  SDLoc SL(Op);
  const MachinePointerInfo &SrcValue = Load->getMemOperand()->getPointerInfo();
  Align BaseAlign = Load->getAlign();
  unsigned NumElements = MemVT.getVectorNumElements();

  // Widen from vec3 to vec4 when the load is at least 8-byte aligned
  // or 16-byte fully dereferenceable. Otherwise, split the vector load.
  if (NumElements != 3 ||
      (BaseAlign < Align(8) &&
       !SrcValue.isDereferenceable(16, *DAG.getContext(), DAG.getDataLayout())))
    return SplitVectorLoad(Op, DAG);

  assert(NumElements == 3);

  EVT WideVT =
      EVT::getVectorVT(*DAG.getContext(), VT.getVectorElementType(), 4);
  EVT WideMemVT =
      EVT::getVectorVT(*DAG.getContext(), MemVT.getVectorElementType(), 4);
  SDValue WideLoad = DAG.getExtLoad(
      Load->getExtensionType(), SL, WideVT, Load->getChain(), BasePtr, SrcValue,
      WideMemVT, BaseAlign, Load->getMemOperand()->getFlags());
  return DAG.getMergeValues(
      {DAG.getNode(ISD::EXTRACT_SUBVECTOR, SL, VT, WideLoad,
                   DAG.getVectorIdxConstant(0, SL)),
       WideLoad.getValue(1)},
      SL);
}

SDValue AMDGPUTargetLowering::SplitVectorStore(SDValue Op,
                                               SelectionDAG &DAG) const {
  StoreSDNode *Store = cast<StoreSDNode>(Op);
  SDValue Val = Store->getValue();
  EVT VT = Val.getValueType();

  // If this is a 2 element vector, we really want to scalarize and not create
  // weird 1 element vectors.
  if (VT.getVectorNumElements() == 2)
    return scalarizeVectorStore(Store, DAG);

  EVT MemVT = Store->getMemoryVT();
  SDValue Chain = Store->getChain();
  SDValue BasePtr = Store->getBasePtr();
  SDLoc SL(Op);

  EVT LoVT, HiVT;
  EVT LoMemVT, HiMemVT;
  SDValue Lo, Hi;

  std::tie(LoVT, HiVT) = getSplitDestVTs(VT, DAG);
  std::tie(LoMemVT, HiMemVT) = getSplitDestVTs(MemVT, DAG);
  std::tie(Lo, Hi) = splitVector(Val, SL, LoVT, HiVT, DAG);

  SDValue HiPtr = DAG.getObjectPtrOffset(SL, BasePtr, LoMemVT.getStoreSize());

  const MachinePointerInfo &SrcValue = Store->getMemOperand()->getPointerInfo();
  Align BaseAlign = Store->getAlign();
  unsigned Size = LoMemVT.getStoreSize();
  Align HiAlign = commonAlignment(BaseAlign, Size);

  SDValue LoStore =
      DAG.getTruncStore(Chain, SL, Lo, BasePtr, SrcValue, LoMemVT, BaseAlign,
                        Store->getMemOperand()->getFlags());
  SDValue HiStore =
      DAG.getTruncStore(Chain, SL, Hi, HiPtr, SrcValue.getWithOffset(Size),
                        HiMemVT, HiAlign, Store->getMemOperand()->getFlags());

  return DAG.getNode(ISD::TokenFactor, SL, MVT::Other, LoStore, HiStore);
}

// This is a shortcut for integer division because we have fast i32<->f32
// conversions, and fast f32 reciprocal instructions. The fractional part of a
// float is enough to accurately represent up to a 24-bit signed integer.
SDValue AMDGPUTargetLowering::LowerDIVREM24(SDValue Op, SelectionDAG &DAG,
                                            bool Sign) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  MVT IntVT = MVT::i32;
  MVT FltVT = MVT::f32;

  unsigned LHSSignBits = DAG.ComputeNumSignBits(LHS);
  if (LHSSignBits < 9)
    return SDValue();

  unsigned RHSSignBits = DAG.ComputeNumSignBits(RHS);
  if (RHSSignBits < 9)
    return SDValue();

  unsigned BitSize = VT.getSizeInBits();
  unsigned SignBits = std::min(LHSSignBits, RHSSignBits);
  unsigned DivBits = BitSize - SignBits;
  if (Sign)
    ++DivBits;

  ISD::NodeType ToFp = Sign ? ISD::SINT_TO_FP : ISD::UINT_TO_FP;
  ISD::NodeType ToInt = Sign ? ISD::FP_TO_SINT : ISD::FP_TO_UINT;

  SDValue jq = DAG.getConstant(1, DL, IntVT);

  if (Sign) {
    // char|short jq = ia ^ ib;
    jq = DAG.getNode(ISD::XOR, DL, VT, LHS, RHS);

    // jq = jq >> (bitsize - 2)
    jq = DAG.getNode(ISD::SRA, DL, VT, jq,
                     DAG.getConstant(BitSize - 2, DL, VT));

    // jq = jq | 0x1
    jq = DAG.getNode(ISD::OR, DL, VT, jq, DAG.getConstant(1, DL, VT));
  }

  // int ia = (int)LHS;
  SDValue ia = LHS;

  // int ib, (int)RHS;
  SDValue ib = RHS;

  // float fa = (float)ia;
  SDValue fa = DAG.getNode(ToFp, DL, FltVT, ia);

  // float fb = (float)ib;
  SDValue fb = DAG.getNode(ToFp, DL, FltVT, ib);

  SDValue fq = DAG.getNode(ISD::FMUL, DL, FltVT,
                           fa, DAG.getNode(AMDGPUISD::RCP, DL, FltVT, fb));

  // fq = trunc(fq);
  fq = DAG.getNode(ISD::FTRUNC, DL, FltVT, fq);

  // float fqneg = -fq;
  SDValue fqneg = DAG.getNode(ISD::FNEG, DL, FltVT, fq);

  MachineFunction &MF = DAG.getMachineFunction();

  bool UseFmadFtz = false;
  if (Subtarget->isGCN()) {
    const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
    UseFmadFtz =
        MFI->getMode().FP32Denormals != DenormalMode::getPreserveSign();
  }

  // float fr = mad(fqneg, fb, fa);
  unsigned OpCode = !Subtarget->hasMadMacF32Insts() ? (unsigned)ISD::FMA
                    : UseFmadFtz ? (unsigned)AMDGPUISD::FMAD_FTZ
                                 : (unsigned)ISD::FMAD;
  SDValue fr = DAG.getNode(OpCode, DL, FltVT, fqneg, fb, fa);

  // int iq = (int)fq;
  SDValue iq = DAG.getNode(ToInt, DL, IntVT, fq);

  // fr = fabs(fr);
  fr = DAG.getNode(ISD::FABS, DL, FltVT, fr);

  // fb = fabs(fb);
  fb = DAG.getNode(ISD::FABS, DL, FltVT, fb);

  EVT SetCCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);

  // int cv = fr >= fb;
  SDValue cv = DAG.getSetCC(DL, SetCCVT, fr, fb, ISD::SETOGE);

  // jq = (cv ? jq : 0);
  jq = DAG.getNode(ISD::SELECT, DL, VT, cv, jq, DAG.getConstant(0, DL, VT));

  // dst = iq + jq;
  SDValue Div = DAG.getNode(ISD::ADD, DL, VT, iq, jq);

  // Rem needs compensation, it's easier to recompute it
  SDValue Rem = DAG.getNode(ISD::MUL, DL, VT, Div, RHS);
  Rem = DAG.getNode(ISD::SUB, DL, VT, LHS, Rem);

  // Truncate to number of bits this divide really is.
  if (Sign) {
    SDValue InRegSize
      = DAG.getValueType(EVT::getIntegerVT(*DAG.getContext(), DivBits));
    Div = DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, VT, Div, InRegSize);
    Rem = DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, VT, Rem, InRegSize);
  } else {
    SDValue TruncMask = DAG.getConstant((UINT64_C(1) << DivBits) - 1, DL, VT);
    Div = DAG.getNode(ISD::AND, DL, VT, Div, TruncMask);
    Rem = DAG.getNode(ISD::AND, DL, VT, Rem, TruncMask);
  }

  return DAG.getMergeValues({ Div, Rem }, DL);
}

void AMDGPUTargetLowering::LowerUDIVREM64(SDValue Op,
                                      SelectionDAG &DAG,
                                      SmallVectorImpl<SDValue> &Results) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();

  assert(VT == MVT::i64 && "LowerUDIVREM64 expects an i64");

  EVT HalfVT = VT.getHalfSizedIntegerVT(*DAG.getContext());

  SDValue One = DAG.getConstant(1, DL, HalfVT);
  SDValue Zero = DAG.getConstant(0, DL, HalfVT);

  //HiLo split
  SDValue LHS_Lo, LHS_Hi;
  SDValue LHS = Op.getOperand(0);
  std::tie(LHS_Lo, LHS_Hi) = DAG.SplitScalar(LHS, DL, HalfVT, HalfVT);

  SDValue RHS_Lo, RHS_Hi;
  SDValue RHS = Op.getOperand(1);
  std::tie(RHS_Lo, RHS_Hi) = DAG.SplitScalar(RHS, DL, HalfVT, HalfVT);

  if (DAG.MaskedValueIsZero(RHS, APInt::getHighBitsSet(64, 32)) &&
      DAG.MaskedValueIsZero(LHS, APInt::getHighBitsSet(64, 32))) {

    SDValue Res = DAG.getNode(ISD::UDIVREM, DL, DAG.getVTList(HalfVT, HalfVT),
                              LHS_Lo, RHS_Lo);

    SDValue DIV = DAG.getBuildVector(MVT::v2i32, DL, {Res.getValue(0), Zero});
    SDValue REM = DAG.getBuildVector(MVT::v2i32, DL, {Res.getValue(1), Zero});

    Results.push_back(DAG.getNode(ISD::BITCAST, DL, MVT::i64, DIV));
    Results.push_back(DAG.getNode(ISD::BITCAST, DL, MVT::i64, REM));
    return;
  }

  if (isTypeLegal(MVT::i64)) {
    // The algorithm here is based on ideas from "Software Integer Division",
    // Tom Rodeheffer, August 2008.

    MachineFunction &MF = DAG.getMachineFunction();
    const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();

    // Compute denominator reciprocal.
    unsigned FMAD =
        !Subtarget->hasMadMacF32Insts() ? (unsigned)ISD::FMA
        : MFI->getMode().FP32Denormals == DenormalMode::getPreserveSign()
            ? (unsigned)ISD::FMAD
            : (unsigned)AMDGPUISD::FMAD_FTZ;

    SDValue Cvt_Lo = DAG.getNode(ISD::UINT_TO_FP, DL, MVT::f32, RHS_Lo);
    SDValue Cvt_Hi = DAG.getNode(ISD::UINT_TO_FP, DL, MVT::f32, RHS_Hi);
    SDValue Mad1 = DAG.getNode(FMAD, DL, MVT::f32, Cvt_Hi,
      DAG.getConstantFP(APInt(32, 0x4f800000).bitsToFloat(), DL, MVT::f32),
      Cvt_Lo);
    SDValue Rcp = DAG.getNode(AMDGPUISD::RCP, DL, MVT::f32, Mad1);
    SDValue Mul1 = DAG.getNode(ISD::FMUL, DL, MVT::f32, Rcp,
      DAG.getConstantFP(APInt(32, 0x5f7ffffc).bitsToFloat(), DL, MVT::f32));
    SDValue Mul2 = DAG.getNode(ISD::FMUL, DL, MVT::f32, Mul1,
      DAG.getConstantFP(APInt(32, 0x2f800000).bitsToFloat(), DL, MVT::f32));
    SDValue Trunc = DAG.getNode(ISD::FTRUNC, DL, MVT::f32, Mul2);
    SDValue Mad2 = DAG.getNode(FMAD, DL, MVT::f32, Trunc,
      DAG.getConstantFP(APInt(32, 0xcf800000).bitsToFloat(), DL, MVT::f32),
      Mul1);
    SDValue Rcp_Lo = DAG.getNode(ISD::FP_TO_UINT, DL, HalfVT, Mad2);
    SDValue Rcp_Hi = DAG.getNode(ISD::FP_TO_UINT, DL, HalfVT, Trunc);
    SDValue Rcp64 = DAG.getBitcast(VT,
                        DAG.getBuildVector(MVT::v2i32, DL, {Rcp_Lo, Rcp_Hi}));

    SDValue Zero64 = DAG.getConstant(0, DL, VT);
    SDValue One64  = DAG.getConstant(1, DL, VT);
    SDValue Zero1 = DAG.getConstant(0, DL, MVT::i1);
    SDVTList HalfCarryVT = DAG.getVTList(HalfVT, MVT::i1);

    // First round of UNR (Unsigned integer Newton-Raphson).
    SDValue Neg_RHS = DAG.getNode(ISD::SUB, DL, VT, Zero64, RHS);
    SDValue Mullo1 = DAG.getNode(ISD::MUL, DL, VT, Neg_RHS, Rcp64);
    SDValue Mulhi1 = DAG.getNode(ISD::MULHU, DL, VT, Rcp64, Mullo1);
    SDValue Mulhi1_Lo, Mulhi1_Hi;
    std::tie(Mulhi1_Lo, Mulhi1_Hi) =
        DAG.SplitScalar(Mulhi1, DL, HalfVT, HalfVT);
    SDValue Add1_Lo = DAG.getNode(ISD::UADDO_CARRY, DL, HalfCarryVT, Rcp_Lo,
                                  Mulhi1_Lo, Zero1);
    SDValue Add1_Hi = DAG.getNode(ISD::UADDO_CARRY, DL, HalfCarryVT, Rcp_Hi,
                                  Mulhi1_Hi, Add1_Lo.getValue(1));
    SDValue Add1 = DAG.getBitcast(VT,
                        DAG.getBuildVector(MVT::v2i32, DL, {Add1_Lo, Add1_Hi}));

    // Second round of UNR.
    SDValue Mullo2 = DAG.getNode(ISD::MUL, DL, VT, Neg_RHS, Add1);
    SDValue Mulhi2 = DAG.getNode(ISD::MULHU, DL, VT, Add1, Mullo2);
    SDValue Mulhi2_Lo, Mulhi2_Hi;
    std::tie(Mulhi2_Lo, Mulhi2_Hi) =
        DAG.SplitScalar(Mulhi2, DL, HalfVT, HalfVT);
    SDValue Add2_Lo = DAG.getNode(ISD::UADDO_CARRY, DL, HalfCarryVT, Add1_Lo,
                                  Mulhi2_Lo, Zero1);
    SDValue Add2_Hi = DAG.getNode(ISD::UADDO_CARRY, DL, HalfCarryVT, Add1_Hi,
                                  Mulhi2_Hi, Add2_Lo.getValue(1));
    SDValue Add2 = DAG.getBitcast(VT,
                        DAG.getBuildVector(MVT::v2i32, DL, {Add2_Lo, Add2_Hi}));

    SDValue Mulhi3 = DAG.getNode(ISD::MULHU, DL, VT, LHS, Add2);

    SDValue Mul3 = DAG.getNode(ISD::MUL, DL, VT, RHS, Mulhi3);

    SDValue Mul3_Lo, Mul3_Hi;
    std::tie(Mul3_Lo, Mul3_Hi) = DAG.SplitScalar(Mul3, DL, HalfVT, HalfVT);
    SDValue Sub1_Lo = DAG.getNode(ISD::USUBO_CARRY, DL, HalfCarryVT, LHS_Lo,
                                  Mul3_Lo, Zero1);
    SDValue Sub1_Hi = DAG.getNode(ISD::USUBO_CARRY, DL, HalfCarryVT, LHS_Hi,
                                  Mul3_Hi, Sub1_Lo.getValue(1));
    SDValue Sub1_Mi = DAG.getNode(ISD::SUB, DL, HalfVT, LHS_Hi, Mul3_Hi);
    SDValue Sub1 = DAG.getBitcast(VT,
                        DAG.getBuildVector(MVT::v2i32, DL, {Sub1_Lo, Sub1_Hi}));

    SDValue MinusOne = DAG.getConstant(0xffffffffu, DL, HalfVT);
    SDValue C1 = DAG.getSelectCC(DL, Sub1_Hi, RHS_Hi, MinusOne, Zero,
                                 ISD::SETUGE);
    SDValue C2 = DAG.getSelectCC(DL, Sub1_Lo, RHS_Lo, MinusOne, Zero,
                                 ISD::SETUGE);
    SDValue C3 = DAG.getSelectCC(DL, Sub1_Hi, RHS_Hi, C2, C1, ISD::SETEQ);

    // TODO: Here and below portions of the code can be enclosed into if/endif.
    // Currently control flow is unconditional and we have 4 selects after
    // potential endif to substitute PHIs.

    // if C3 != 0 ...
    SDValue Sub2_Lo = DAG.getNode(ISD::USUBO_CARRY, DL, HalfCarryVT, Sub1_Lo,
                                  RHS_Lo, Zero1);
    SDValue Sub2_Mi = DAG.getNode(ISD::USUBO_CARRY, DL, HalfCarryVT, Sub1_Mi,
                                  RHS_Hi, Sub1_Lo.getValue(1));
    SDValue Sub2_Hi = DAG.getNode(ISD::USUBO_CARRY, DL, HalfCarryVT, Sub2_Mi,
                                  Zero, Sub2_Lo.getValue(1));
    SDValue Sub2 = DAG.getBitcast(VT,
                        DAG.getBuildVector(MVT::v2i32, DL, {Sub2_Lo, Sub2_Hi}));

    SDValue Add3 = DAG.getNode(ISD::ADD, DL, VT, Mulhi3, One64);

    SDValue C4 = DAG.getSelectCC(DL, Sub2_Hi, RHS_Hi, MinusOne, Zero,
                                 ISD::SETUGE);
    SDValue C5 = DAG.getSelectCC(DL, Sub2_Lo, RHS_Lo, MinusOne, Zero,
                                 ISD::SETUGE);
    SDValue C6 = DAG.getSelectCC(DL, Sub2_Hi, RHS_Hi, C5, C4, ISD::SETEQ);

    // if (C6 != 0)
    SDValue Add4 = DAG.getNode(ISD::ADD, DL, VT, Add3, One64);

    SDValue Sub3_Lo = DAG.getNode(ISD::USUBO_CARRY, DL, HalfCarryVT, Sub2_Lo,
                                  RHS_Lo, Zero1);
    SDValue Sub3_Mi = DAG.getNode(ISD::USUBO_CARRY, DL, HalfCarryVT, Sub2_Mi,
                                  RHS_Hi, Sub2_Lo.getValue(1));
    SDValue Sub3_Hi = DAG.getNode(ISD::USUBO_CARRY, DL, HalfCarryVT, Sub3_Mi,
                                  Zero, Sub3_Lo.getValue(1));
    SDValue Sub3 = DAG.getBitcast(VT,
                        DAG.getBuildVector(MVT::v2i32, DL, {Sub3_Lo, Sub3_Hi}));

    // endif C6
    // endif C3

    SDValue Sel1 = DAG.getSelectCC(DL, C6, Zero, Add4, Add3, ISD::SETNE);
    SDValue Div  = DAG.getSelectCC(DL, C3, Zero, Sel1, Mulhi3, ISD::SETNE);

    SDValue Sel2 = DAG.getSelectCC(DL, C6, Zero, Sub3, Sub2, ISD::SETNE);
    SDValue Rem  = DAG.getSelectCC(DL, C3, Zero, Sel2, Sub1, ISD::SETNE);

    Results.push_back(Div);
    Results.push_back(Rem);

    return;
  }

  // r600 expandion.
  // Get Speculative values
  SDValue DIV_Part = DAG.getNode(ISD::UDIV, DL, HalfVT, LHS_Hi, RHS_Lo);
  SDValue REM_Part = DAG.getNode(ISD::UREM, DL, HalfVT, LHS_Hi, RHS_Lo);

  SDValue REM_Lo = DAG.getSelectCC(DL, RHS_Hi, Zero, REM_Part, LHS_Hi, ISD::SETEQ);
  SDValue REM = DAG.getBuildVector(MVT::v2i32, DL, {REM_Lo, Zero});
  REM = DAG.getNode(ISD::BITCAST, DL, MVT::i64, REM);

  SDValue DIV_Hi = DAG.getSelectCC(DL, RHS_Hi, Zero, DIV_Part, Zero, ISD::SETEQ);
  SDValue DIV_Lo = Zero;

  const unsigned halfBitWidth = HalfVT.getSizeInBits();

  for (unsigned i = 0; i < halfBitWidth; ++i) {
    const unsigned bitPos = halfBitWidth - i - 1;
    SDValue POS = DAG.getConstant(bitPos, DL, HalfVT);
    // Get value of high bit
    SDValue HBit = DAG.getNode(ISD::SRL, DL, HalfVT, LHS_Lo, POS);
    HBit = DAG.getNode(ISD::AND, DL, HalfVT, HBit, One);
    HBit = DAG.getNode(ISD::ZERO_EXTEND, DL, VT, HBit);

    // Shift
    REM = DAG.getNode(ISD::SHL, DL, VT, REM, DAG.getConstant(1, DL, VT));
    // Add LHS high bit
    REM = DAG.getNode(ISD::OR, DL, VT, REM, HBit);

    SDValue BIT = DAG.getConstant(1ULL << bitPos, DL, HalfVT);
    SDValue realBIT = DAG.getSelectCC(DL, REM, RHS, BIT, Zero, ISD::SETUGE);

    DIV_Lo = DAG.getNode(ISD::OR, DL, HalfVT, DIV_Lo, realBIT);

    // Update REM
    SDValue REM_sub = DAG.getNode(ISD::SUB, DL, VT, REM, RHS);
    REM = DAG.getSelectCC(DL, REM, RHS, REM_sub, REM, ISD::SETUGE);
  }

  SDValue DIV = DAG.getBuildVector(MVT::v2i32, DL, {DIV_Lo, DIV_Hi});
  DIV = DAG.getNode(ISD::BITCAST, DL, MVT::i64, DIV);
  Results.push_back(DIV);
  Results.push_back(REM);
}

SDValue AMDGPUTargetLowering::LowerUDIVREM(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();

  if (VT == MVT::i64) {
    SmallVector<SDValue, 2> Results;
    LowerUDIVREM64(Op, DAG, Results);
    return DAG.getMergeValues(Results, DL);
  }

  if (VT == MVT::i32) {
    if (SDValue Res = LowerDIVREM24(Op, DAG, false))
      return Res;
  }

  SDValue X = Op.getOperand(0);
  SDValue Y = Op.getOperand(1);

  // See AMDGPUCodeGenPrepare::expandDivRem32 for a description of the
  // algorithm used here.

  // Initial estimate of inv(y).
  SDValue Z = DAG.getNode(AMDGPUISD::URECIP, DL, VT, Y);

  // One round of UNR.
  SDValue NegY = DAG.getNode(ISD::SUB, DL, VT, DAG.getConstant(0, DL, VT), Y);
  SDValue NegYZ = DAG.getNode(ISD::MUL, DL, VT, NegY, Z);
  Z = DAG.getNode(ISD::ADD, DL, VT, Z,
                  DAG.getNode(ISD::MULHU, DL, VT, Z, NegYZ));

  // Quotient/remainder estimate.
  SDValue Q = DAG.getNode(ISD::MULHU, DL, VT, X, Z);
  SDValue R =
      DAG.getNode(ISD::SUB, DL, VT, X, DAG.getNode(ISD::MUL, DL, VT, Q, Y));

  // First quotient/remainder refinement.
  EVT CCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
  SDValue One = DAG.getConstant(1, DL, VT);
  SDValue Cond = DAG.getSetCC(DL, CCVT, R, Y, ISD::SETUGE);
  Q = DAG.getNode(ISD::SELECT, DL, VT, Cond,
                  DAG.getNode(ISD::ADD, DL, VT, Q, One), Q);
  R = DAG.getNode(ISD::SELECT, DL, VT, Cond,
                  DAG.getNode(ISD::SUB, DL, VT, R, Y), R);

  // Second quotient/remainder refinement.
  Cond = DAG.getSetCC(DL, CCVT, R, Y, ISD::SETUGE);
  Q = DAG.getNode(ISD::SELECT, DL, VT, Cond,
                  DAG.getNode(ISD::ADD, DL, VT, Q, One), Q);
  R = DAG.getNode(ISD::SELECT, DL, VT, Cond,
                  DAG.getNode(ISD::SUB, DL, VT, R, Y), R);

  return DAG.getMergeValues({Q, R}, DL);
}

SDValue AMDGPUTargetLowering::LowerSDIVREM(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();

  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);

  SDValue Zero = DAG.getConstant(0, DL, VT);
  SDValue NegOne = DAG.getConstant(-1, DL, VT);

  if (VT == MVT::i32) {
    if (SDValue Res = LowerDIVREM24(Op, DAG, true))
      return Res;
  }

  if (VT == MVT::i64 &&
      DAG.ComputeNumSignBits(LHS) > 32 &&
      DAG.ComputeNumSignBits(RHS) > 32) {
    EVT HalfVT = VT.getHalfSizedIntegerVT(*DAG.getContext());

    //HiLo split
    SDValue LHS_Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, HalfVT, LHS, Zero);
    SDValue RHS_Lo = DAG.getNode(ISD::EXTRACT_ELEMENT, DL, HalfVT, RHS, Zero);
    SDValue DIVREM = DAG.getNode(ISD::SDIVREM, DL, DAG.getVTList(HalfVT, HalfVT),
                                 LHS_Lo, RHS_Lo);
    SDValue Res[2] = {
      DAG.getNode(ISD::SIGN_EXTEND, DL, VT, DIVREM.getValue(0)),
      DAG.getNode(ISD::SIGN_EXTEND, DL, VT, DIVREM.getValue(1))
    };
    return DAG.getMergeValues(Res, DL);
  }

  SDValue LHSign = DAG.getSelectCC(DL, LHS, Zero, NegOne, Zero, ISD::SETLT);
  SDValue RHSign = DAG.getSelectCC(DL, RHS, Zero, NegOne, Zero, ISD::SETLT);
  SDValue DSign = DAG.getNode(ISD::XOR, DL, VT, LHSign, RHSign);
  SDValue RSign = LHSign; // Remainder sign is the same as LHS

  LHS = DAG.getNode(ISD::ADD, DL, VT, LHS, LHSign);
  RHS = DAG.getNode(ISD::ADD, DL, VT, RHS, RHSign);

  LHS = DAG.getNode(ISD::XOR, DL, VT, LHS, LHSign);
  RHS = DAG.getNode(ISD::XOR, DL, VT, RHS, RHSign);

  SDValue Div = DAG.getNode(ISD::UDIVREM, DL, DAG.getVTList(VT, VT), LHS, RHS);
  SDValue Rem = Div.getValue(1);

  Div = DAG.getNode(ISD::XOR, DL, VT, Div, DSign);
  Rem = DAG.getNode(ISD::XOR, DL, VT, Rem, RSign);

  Div = DAG.getNode(ISD::SUB, DL, VT, Div, DSign);
  Rem = DAG.getNode(ISD::SUB, DL, VT, Rem, RSign);

  SDValue Res[2] = {
    Div,
    Rem
  };
  return DAG.getMergeValues(Res, DL);
}

// (frem x, y) -> (fma (fneg (ftrunc (fdiv x, y))), y, x)
SDValue AMDGPUTargetLowering::LowerFREM(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  EVT VT = Op.getValueType();
  auto Flags = Op->getFlags();
  SDValue X = Op.getOperand(0);
  SDValue Y = Op.getOperand(1);

  SDValue Div = DAG.getNode(ISD::FDIV, SL, VT, X, Y, Flags);
  SDValue Trunc = DAG.getNode(ISD::FTRUNC, SL, VT, Div, Flags);
  SDValue Neg = DAG.getNode(ISD::FNEG, SL, VT, Trunc, Flags);
  // TODO: For f32 use FMAD instead if !hasFastFMA32?
  return DAG.getNode(ISD::FMA, SL, VT, Neg, Y, X, Flags);
}

SDValue AMDGPUTargetLowering::LowerFCEIL(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue Src = Op.getOperand(0);

  // result = trunc(src)
  // if (src > 0.0 && src != result)
  //   result += 1.0

  SDValue Trunc = DAG.getNode(ISD::FTRUNC, SL, MVT::f64, Src);

  const SDValue Zero = DAG.getConstantFP(0.0, SL, MVT::f64);
  const SDValue One = DAG.getConstantFP(1.0, SL, MVT::f64);

  EVT SetCCVT =
      getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), MVT::f64);

  SDValue Lt0 = DAG.getSetCC(SL, SetCCVT, Src, Zero, ISD::SETOGT);
  SDValue NeTrunc = DAG.getSetCC(SL, SetCCVT, Src, Trunc, ISD::SETONE);
  SDValue And = DAG.getNode(ISD::AND, SL, SetCCVT, Lt0, NeTrunc);

  SDValue Add = DAG.getNode(ISD::SELECT, SL, MVT::f64, And, One, Zero);
  // TODO: Should this propagate fast-math-flags?
  return DAG.getNode(ISD::FADD, SL, MVT::f64, Trunc, Add);
}

static SDValue extractF64Exponent(SDValue Hi, const SDLoc &SL,
                                  SelectionDAG &DAG) {
  const unsigned FractBits = 52;
  const unsigned ExpBits = 11;

  SDValue ExpPart = DAG.getNode(AMDGPUISD::BFE_U32, SL, MVT::i32,
                                Hi,
                                DAG.getConstant(FractBits - 32, SL, MVT::i32),
                                DAG.getConstant(ExpBits, SL, MVT::i32));
  SDValue Exp = DAG.getNode(ISD::SUB, SL, MVT::i32, ExpPart,
                            DAG.getConstant(1023, SL, MVT::i32));

  return Exp;
}

SDValue AMDGPUTargetLowering::LowerFTRUNC(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue Src = Op.getOperand(0);

  assert(Op.getValueType() == MVT::f64);

  const SDValue Zero = DAG.getConstant(0, SL, MVT::i32);

  // Extract the upper half, since this is where we will find the sign and
  // exponent.
  SDValue Hi = getHiHalf64(Src, DAG);

  SDValue Exp = extractF64Exponent(Hi, SL, DAG);

  const unsigned FractBits = 52;

  // Extract the sign bit.
  const SDValue SignBitMask = DAG.getConstant(UINT32_C(1) << 31, SL, MVT::i32);
  SDValue SignBit = DAG.getNode(ISD::AND, SL, MVT::i32, Hi, SignBitMask);

  // Extend back to 64-bits.
  SDValue SignBit64 = DAG.getBuildVector(MVT::v2i32, SL, {Zero, SignBit});
  SignBit64 = DAG.getNode(ISD::BITCAST, SL, MVT::i64, SignBit64);

  SDValue BcInt = DAG.getNode(ISD::BITCAST, SL, MVT::i64, Src);
  const SDValue FractMask
    = DAG.getConstant((UINT64_C(1) << FractBits) - 1, SL, MVT::i64);

  SDValue Shr = DAG.getNode(ISD::SRA, SL, MVT::i64, FractMask, Exp);
  SDValue Not = DAG.getNOT(SL, Shr, MVT::i64);
  SDValue Tmp0 = DAG.getNode(ISD::AND, SL, MVT::i64, BcInt, Not);

  EVT SetCCVT =
      getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), MVT::i32);

  const SDValue FiftyOne = DAG.getConstant(FractBits - 1, SL, MVT::i32);

  SDValue ExpLt0 = DAG.getSetCC(SL, SetCCVT, Exp, Zero, ISD::SETLT);
  SDValue ExpGt51 = DAG.getSetCC(SL, SetCCVT, Exp, FiftyOne, ISD::SETGT);

  SDValue Tmp1 = DAG.getNode(ISD::SELECT, SL, MVT::i64, ExpLt0, SignBit64, Tmp0);
  SDValue Tmp2 = DAG.getNode(ISD::SELECT, SL, MVT::i64, ExpGt51, BcInt, Tmp1);

  return DAG.getNode(ISD::BITCAST, SL, MVT::f64, Tmp2);
}

SDValue AMDGPUTargetLowering::LowerFROUNDEVEN(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue Src = Op.getOperand(0);

  assert(Op.getValueType() == MVT::f64);

  APFloat C1Val(APFloat::IEEEdouble(), "0x1.0p+52");
  SDValue C1 = DAG.getConstantFP(C1Val, SL, MVT::f64);
  SDValue CopySign = DAG.getNode(ISD::FCOPYSIGN, SL, MVT::f64, C1, Src);

  // TODO: Should this propagate fast-math-flags?

  SDValue Tmp1 = DAG.getNode(ISD::FADD, SL, MVT::f64, Src, CopySign);
  SDValue Tmp2 = DAG.getNode(ISD::FSUB, SL, MVT::f64, Tmp1, CopySign);

  SDValue Fabs = DAG.getNode(ISD::FABS, SL, MVT::f64, Src);

  APFloat C2Val(APFloat::IEEEdouble(), "0x1.fffffffffffffp+51");
  SDValue C2 = DAG.getConstantFP(C2Val, SL, MVT::f64);

  EVT SetCCVT =
      getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), MVT::f64);
  SDValue Cond = DAG.getSetCC(SL, SetCCVT, Fabs, C2, ISD::SETOGT);

  return DAG.getSelect(SL, MVT::f64, Cond, Src, Tmp2);
}

SDValue AMDGPUTargetLowering::LowerFNEARBYINT(SDValue Op,
                                              SelectionDAG &DAG) const {
  // FNEARBYINT and FRINT are the same, except in their handling of FP
  // exceptions. Those aren't really meaningful for us, and OpenCL only has
  // rint, so just treat them as equivalent.
  return DAG.getNode(ISD::FROUNDEVEN, SDLoc(Op), Op.getValueType(),
                     Op.getOperand(0));
}

SDValue AMDGPUTargetLowering::LowerFRINT(SDValue Op, SelectionDAG &DAG) const {
  auto VT = Op.getValueType();
  auto Arg = Op.getOperand(0u);
  return DAG.getNode(ISD::FROUNDEVEN, SDLoc(Op), VT, Arg);
}

// XXX - May require not supporting f32 denormals?

// Don't handle v2f16. The extra instructions to scalarize and repack around the
// compare and vselect end up producing worse code than scalarizing the whole
// operation.
SDValue AMDGPUTargetLowering::LowerFROUND(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue X = Op.getOperand(0);
  EVT VT = Op.getValueType();

  SDValue T = DAG.getNode(ISD::FTRUNC, SL, VT, X);

  // TODO: Should this propagate fast-math-flags?

  SDValue Diff = DAG.getNode(ISD::FSUB, SL, VT, X, T);

  SDValue AbsDiff = DAG.getNode(ISD::FABS, SL, VT, Diff);

  const SDValue Zero = DAG.getConstantFP(0.0, SL, VT);
  const SDValue One = DAG.getConstantFP(1.0, SL, VT);

  EVT SetCCVT =
      getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);

  const SDValue Half = DAG.getConstantFP(0.5, SL, VT);
  SDValue Cmp = DAG.getSetCC(SL, SetCCVT, AbsDiff, Half, ISD::SETOGE);
  SDValue OneOrZeroFP = DAG.getNode(ISD::SELECT, SL, VT, Cmp, One, Zero);

  SDValue SignedOffset = DAG.getNode(ISD::FCOPYSIGN, SL, VT, OneOrZeroFP, X);
  return DAG.getNode(ISD::FADD, SL, VT, T, SignedOffset);
}

SDValue AMDGPUTargetLowering::LowerFFLOOR(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue Src = Op.getOperand(0);

  // result = trunc(src);
  // if (src < 0.0 && src != result)
  //   result += -1.0.

  SDValue Trunc = DAG.getNode(ISD::FTRUNC, SL, MVT::f64, Src);

  const SDValue Zero = DAG.getConstantFP(0.0, SL, MVT::f64);
  const SDValue NegOne = DAG.getConstantFP(-1.0, SL, MVT::f64);

  EVT SetCCVT =
      getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), MVT::f64);

  SDValue Lt0 = DAG.getSetCC(SL, SetCCVT, Src, Zero, ISD::SETOLT);
  SDValue NeTrunc = DAG.getSetCC(SL, SetCCVT, Src, Trunc, ISD::SETONE);
  SDValue And = DAG.getNode(ISD::AND, SL, SetCCVT, Lt0, NeTrunc);

  SDValue Add = DAG.getNode(ISD::SELECT, SL, MVT::f64, And, NegOne, Zero);
  // TODO: Should this propagate fast-math-flags?
  return DAG.getNode(ISD::FADD, SL, MVT::f64, Trunc, Add);
}

/// Return true if it's known that \p Src can never be an f32 denormal value.
static bool valueIsKnownNeverF32Denorm(SDValue Src) {
  switch (Src.getOpcode()) {
  case ISD::FP_EXTEND:
    return Src.getOperand(0).getValueType() == MVT::f16;
  case ISD::FP16_TO_FP:
  case ISD::FFREXP:
    return true;
  case ISD::INTRINSIC_WO_CHAIN: {
    unsigned IntrinsicID = Src.getConstantOperandVal(0);
    switch (IntrinsicID) {
    case Intrinsic::amdgcn_frexp_mant:
      return true;
    default:
      return false;
    }
  }
  default:
    return false;
  }

  llvm_unreachable("covered opcode switch");
}

bool AMDGPUTargetLowering::allowApproxFunc(const SelectionDAG &DAG,
                                           SDNodeFlags Flags) {
  if (Flags.hasApproximateFuncs())
    return true;
  auto &Options = DAG.getTarget().Options;
  return Options.UnsafeFPMath || Options.ApproxFuncFPMath;
}

bool AMDGPUTargetLowering::needsDenormHandlingF32(const SelectionDAG &DAG,
                                                  SDValue Src,
                                                  SDNodeFlags Flags) {
  return !valueIsKnownNeverF32Denorm(Src) &&
         DAG.getMachineFunction()
                 .getDenormalMode(APFloat::IEEEsingle())
                 .Input != DenormalMode::PreserveSign;
}

SDValue AMDGPUTargetLowering::getIsLtSmallestNormal(SelectionDAG &DAG,
                                                    SDValue Src,
                                                    SDNodeFlags Flags) const {
  SDLoc SL(Src);
  EVT VT = Src.getValueType();
  const fltSemantics &Semantics = SelectionDAG::EVTToAPFloatSemantics(VT);
  SDValue SmallestNormal =
      DAG.getConstantFP(APFloat::getSmallestNormalized(Semantics), SL, VT);

  // Want to scale denormals up, but negatives and 0 work just as well on the
  // scaled path.
  SDValue IsLtSmallestNormal = DAG.getSetCC(
      SL, getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT), Src,
      SmallestNormal, ISD::SETOLT);

  return IsLtSmallestNormal;
}

SDValue AMDGPUTargetLowering::getIsFinite(SelectionDAG &DAG, SDValue Src,
                                          SDNodeFlags Flags) const {
  SDLoc SL(Src);
  EVT VT = Src.getValueType();
  const fltSemantics &Semantics = SelectionDAG::EVTToAPFloatSemantics(VT);
  SDValue Inf = DAG.getConstantFP(APFloat::getInf(Semantics), SL, VT);

  SDValue Fabs = DAG.getNode(ISD::FABS, SL, VT, Src, Flags);
  SDValue IsFinite = DAG.getSetCC(
      SL, getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT), Fabs,
      Inf, ISD::SETOLT);
  return IsFinite;
}

/// If denormal handling is required return the scaled input to FLOG2, and the
/// check for denormal range. Otherwise, return null values.
std::pair<SDValue, SDValue>
AMDGPUTargetLowering::getScaledLogInput(SelectionDAG &DAG, const SDLoc SL,
                                        SDValue Src, SDNodeFlags Flags) const {
  if (!needsDenormHandlingF32(DAG, Src, Flags))
    return {};

  MVT VT = MVT::f32;
  const fltSemantics &Semantics = APFloat::IEEEsingle();
  SDValue SmallestNormal =
      DAG.getConstantFP(APFloat::getSmallestNormalized(Semantics), SL, VT);

  SDValue IsLtSmallestNormal = DAG.getSetCC(
      SL, getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT), Src,
      SmallestNormal, ISD::SETOLT);

  SDValue Scale32 = DAG.getConstantFP(0x1.0p+32, SL, VT);
  SDValue One = DAG.getConstantFP(1.0, SL, VT);
  SDValue ScaleFactor =
      DAG.getNode(ISD::SELECT, SL, VT, IsLtSmallestNormal, Scale32, One, Flags);

  SDValue ScaledInput = DAG.getNode(ISD::FMUL, SL, VT, Src, ScaleFactor, Flags);
  return {ScaledInput, IsLtSmallestNormal};
}

SDValue AMDGPUTargetLowering::LowerFLOG2(SDValue Op, SelectionDAG &DAG) const {
  // v_log_f32 is good enough for OpenCL, except it doesn't handle denormals.
  // If we have to handle denormals, scale up the input and adjust the result.

  // scaled = x * (is_denormal ? 0x1.0p+32 : 1.0)
  // log2 = amdgpu_log2 - (is_denormal ? 32.0 : 0.0)

  SDLoc SL(Op);
  EVT VT = Op.getValueType();
  SDValue Src = Op.getOperand(0);
  SDNodeFlags Flags = Op->getFlags();

  if (VT == MVT::f16) {
    // Nothing in half is a denormal when promoted to f32.
    assert(!Subtarget->has16BitInsts());
    SDValue Ext = DAG.getNode(ISD::FP_EXTEND, SL, MVT::f32, Src, Flags);
    SDValue Log = DAG.getNode(AMDGPUISD::LOG, SL, MVT::f32, Ext, Flags);
    return DAG.getNode(ISD::FP_ROUND, SL, VT, Log,
                       DAG.getTargetConstant(0, SL, MVT::i32), Flags);
  }

  auto [ScaledInput, IsLtSmallestNormal] =
      getScaledLogInput(DAG, SL, Src, Flags);
  if (!ScaledInput)
    return DAG.getNode(AMDGPUISD::LOG, SL, VT, Src, Flags);

  SDValue Log2 = DAG.getNode(AMDGPUISD::LOG, SL, VT, ScaledInput, Flags);

  SDValue ThirtyTwo = DAG.getConstantFP(32.0, SL, VT);
  SDValue Zero = DAG.getConstantFP(0.0, SL, VT);
  SDValue ResultOffset =
      DAG.getNode(ISD::SELECT, SL, VT, IsLtSmallestNormal, ThirtyTwo, Zero);
  return DAG.getNode(ISD::FSUB, SL, VT, Log2, ResultOffset, Flags);
}

static SDValue getMad(SelectionDAG &DAG, const SDLoc &SL, EVT VT, SDValue X,
                      SDValue Y, SDValue C, SDNodeFlags Flags = SDNodeFlags()) {
  SDValue Mul = DAG.getNode(ISD::FMUL, SL, VT, X, Y, Flags);
  return DAG.getNode(ISD::FADD, SL, VT, Mul, C, Flags);
}

SDValue AMDGPUTargetLowering::LowerFLOGCommon(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDValue X = Op.getOperand(0);
  EVT VT = Op.getValueType();
  SDNodeFlags Flags = Op->getFlags();
  SDLoc DL(Op);

  const bool IsLog10 = Op.getOpcode() == ISD::FLOG10;
  assert(IsLog10 || Op.getOpcode() == ISD::FLOG);

  const auto &Options = getTargetMachine().Options;
  if (VT == MVT::f16 || Flags.hasApproximateFuncs() ||
      Options.ApproxFuncFPMath || Options.UnsafeFPMath) {

    if (VT == MVT::f16 && !Subtarget->has16BitInsts()) {
      // Log and multiply in f32 is good enough for f16.
      X = DAG.getNode(ISD::FP_EXTEND, DL, MVT::f32, X, Flags);
    }

    SDValue Lowered = LowerFLOGUnsafe(X, DL, DAG, IsLog10, Flags);
    if (VT == MVT::f16 && !Subtarget->has16BitInsts()) {
      return DAG.getNode(ISD::FP_ROUND, DL, VT, Lowered,
                         DAG.getTargetConstant(0, DL, MVT::i32), Flags);
    }

    return Lowered;
  }

  auto [ScaledInput, IsScaled] = getScaledLogInput(DAG, DL, X, Flags);
  if (ScaledInput)
    X = ScaledInput;

  SDValue Y = DAG.getNode(AMDGPUISD::LOG, DL, VT, X, Flags);

  SDValue R;
  if (Subtarget->hasFastFMAF32()) {
    // c+cc are ln(2)/ln(10) to more than 49 bits
    const float c_log10 = 0x1.344134p-2f;
    const float cc_log10 = 0x1.09f79ep-26f;

    // c + cc is ln(2) to more than 49 bits
    const float c_log = 0x1.62e42ep-1f;
    const float cc_log = 0x1.efa39ep-25f;

    SDValue C = DAG.getConstantFP(IsLog10 ? c_log10 : c_log, DL, VT);
    SDValue CC = DAG.getConstantFP(IsLog10 ? cc_log10 : cc_log, DL, VT);

    R = DAG.getNode(ISD::FMUL, DL, VT, Y, C, Flags);
    SDValue NegR = DAG.getNode(ISD::FNEG, DL, VT, R, Flags);
    SDValue FMA0 = DAG.getNode(ISD::FMA, DL, VT, Y, C, NegR, Flags);
    SDValue FMA1 = DAG.getNode(ISD::FMA, DL, VT, Y, CC, FMA0, Flags);
    R = DAG.getNode(ISD::FADD, DL, VT, R, FMA1, Flags);
  } else {
    // ch+ct is ln(2)/ln(10) to more than 36 bits
    const float ch_log10 = 0x1.344000p-2f;
    const float ct_log10 = 0x1.3509f6p-18f;

    // ch + ct is ln(2) to more than 36 bits
    const float ch_log = 0x1.62e000p-1f;
    const float ct_log = 0x1.0bfbe8p-15f;

    SDValue CH = DAG.getConstantFP(IsLog10 ? ch_log10 : ch_log, DL, VT);
    SDValue CT = DAG.getConstantFP(IsLog10 ? ct_log10 : ct_log, DL, VT);

    SDValue YAsInt = DAG.getNode(ISD::BITCAST, DL, MVT::i32, Y);
    SDValue MaskConst = DAG.getConstant(0xfffff000, DL, MVT::i32);
    SDValue YHInt = DAG.getNode(ISD::AND, DL, MVT::i32, YAsInt, MaskConst);
    SDValue YH = DAG.getNode(ISD::BITCAST, DL, MVT::f32, YHInt);
    SDValue YT = DAG.getNode(ISD::FSUB, DL, VT, Y, YH, Flags);

    SDValue YTCT = DAG.getNode(ISD::FMUL, DL, VT, YT, CT, Flags);
    SDValue Mad0 = getMad(DAG, DL, VT, YH, CT, YTCT, Flags);
    SDValue Mad1 = getMad(DAG, DL, VT, YT, CH, Mad0, Flags);
    R = getMad(DAG, DL, VT, YH, CH, Mad1);
  }

  const bool IsFiniteOnly = (Flags.hasNoNaNs() || Options.NoNaNsFPMath) &&
                            (Flags.hasNoInfs() || Options.NoInfsFPMath);

  // TODO: Check if known finite from source value.
  if (!IsFiniteOnly) {
    SDValue IsFinite = getIsFinite(DAG, Y, Flags);
    R = DAG.getNode(ISD::SELECT, DL, VT, IsFinite, R, Y, Flags);
  }

  if (IsScaled) {
    SDValue Zero = DAG.getConstantFP(0.0f, DL, VT);
    SDValue ShiftK =
        DAG.getConstantFP(IsLog10 ? 0x1.344136p+3f : 0x1.62e430p+4f, DL, VT);
    SDValue Shift =
        DAG.getNode(ISD::SELECT, DL, VT, IsScaled, ShiftK, Zero, Flags);
    R = DAG.getNode(ISD::FSUB, DL, VT, R, Shift, Flags);
  }

  return R;
}

SDValue AMDGPUTargetLowering::LowerFLOG10(SDValue Op, SelectionDAG &DAG) const {
  return LowerFLOGCommon(Op, DAG);
}

// Do f32 fast math expansion for flog2 or flog10. This is accurate enough for a
// promote f16 operation.
SDValue AMDGPUTargetLowering::LowerFLOGUnsafe(SDValue Src, const SDLoc &SL,
                                              SelectionDAG &DAG, bool IsLog10,
                                              SDNodeFlags Flags) const {
  EVT VT = Src.getValueType();
  unsigned LogOp =
      VT == MVT::f32 ? (unsigned)AMDGPUISD::LOG : (unsigned)ISD::FLOG2;

  double Log2BaseInverted =
      IsLog10 ? numbers::ln2 / numbers::ln10 : numbers::ln2;

  if (VT == MVT::f32) {
    auto [ScaledInput, IsScaled] = getScaledLogInput(DAG, SL, Src, Flags);
    if (ScaledInput) {
      SDValue LogSrc = DAG.getNode(AMDGPUISD::LOG, SL, VT, ScaledInput, Flags);
      SDValue ScaledResultOffset =
          DAG.getConstantFP(-32.0 * Log2BaseInverted, SL, VT);

      SDValue Zero = DAG.getConstantFP(0.0f, SL, VT);

      SDValue ResultOffset = DAG.getNode(ISD::SELECT, SL, VT, IsScaled,
                                         ScaledResultOffset, Zero, Flags);

      SDValue Log2Inv = DAG.getConstantFP(Log2BaseInverted, SL, VT);

      if (Subtarget->hasFastFMAF32())
        return DAG.getNode(ISD::FMA, SL, VT, LogSrc, Log2Inv, ResultOffset,
                           Flags);
      SDValue Mul = DAG.getNode(ISD::FMUL, SL, VT, LogSrc, Log2Inv, Flags);
      return DAG.getNode(ISD::FADD, SL, VT, Mul, ResultOffset);
    }
  }

  SDValue Log2Operand = DAG.getNode(LogOp, SL, VT, Src, Flags);
  SDValue Log2BaseInvertedOperand = DAG.getConstantFP(Log2BaseInverted, SL, VT);

  return DAG.getNode(ISD::FMUL, SL, VT, Log2Operand, Log2BaseInvertedOperand,
                     Flags);
}

SDValue AMDGPUTargetLowering::lowerFEXP2(SDValue Op, SelectionDAG &DAG) const {
  // v_exp_f32 is good enough for OpenCL, except it doesn't handle denormals.
  // If we have to handle denormals, scale up the input and adjust the result.

  SDLoc SL(Op);
  EVT VT = Op.getValueType();
  SDValue Src = Op.getOperand(0);
  SDNodeFlags Flags = Op->getFlags();

  if (VT == MVT::f16) {
    // Nothing in half is a denormal when promoted to f32.
    assert(!Subtarget->has16BitInsts());
    SDValue Ext = DAG.getNode(ISD::FP_EXTEND, SL, MVT::f32, Src, Flags);
    SDValue Log = DAG.getNode(AMDGPUISD::EXP, SL, MVT::f32, Ext, Flags);
    return DAG.getNode(ISD::FP_ROUND, SL, VT, Log,
                       DAG.getTargetConstant(0, SL, MVT::i32), Flags);
  }

  assert(VT == MVT::f32);

  if (!needsDenormHandlingF32(DAG, Src, Flags))
    return DAG.getNode(AMDGPUISD::EXP, SL, MVT::f32, Src, Flags);

  // bool needs_scaling = x < -0x1.f80000p+6f;
  // v_exp_f32(x + (s ? 0x1.0p+6f : 0.0f)) * (s ? 0x1.0p-64f : 1.0f);

  // -nextafter(128.0, -1)
  SDValue RangeCheckConst = DAG.getConstantFP(-0x1.f80000p+6f, SL, VT);

  EVT SetCCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);

  SDValue NeedsScaling =
      DAG.getSetCC(SL, SetCCVT, Src, RangeCheckConst, ISD::SETOLT);

  SDValue SixtyFour = DAG.getConstantFP(0x1.0p+6f, SL, VT);
  SDValue Zero = DAG.getConstantFP(0.0, SL, VT);

  SDValue AddOffset =
      DAG.getNode(ISD::SELECT, SL, VT, NeedsScaling, SixtyFour, Zero);

  SDValue AddInput = DAG.getNode(ISD::FADD, SL, VT, Src, AddOffset, Flags);
  SDValue Exp2 = DAG.getNode(AMDGPUISD::EXP, SL, VT, AddInput, Flags);

  SDValue TwoExpNeg64 = DAG.getConstantFP(0x1.0p-64f, SL, VT);
  SDValue One = DAG.getConstantFP(1.0, SL, VT);
  SDValue ResultScale =
      DAG.getNode(ISD::SELECT, SL, VT, NeedsScaling, TwoExpNeg64, One);

  return DAG.getNode(ISD::FMUL, SL, VT, Exp2, ResultScale, Flags);
}

SDValue AMDGPUTargetLowering::lowerFEXPUnsafe(SDValue X, const SDLoc &SL,
                                              SelectionDAG &DAG,
                                              SDNodeFlags Flags) const {
  EVT VT = X.getValueType();
  const SDValue Log2E = DAG.getConstantFP(numbers::log2e, SL, VT);

  if (VT != MVT::f32 || !needsDenormHandlingF32(DAG, X, Flags)) {
    // exp2(M_LOG2E_F * f);
    SDValue Mul = DAG.getNode(ISD::FMUL, SL, VT, X, Log2E, Flags);
    return DAG.getNode(VT == MVT::f32 ? (unsigned)AMDGPUISD::EXP
                                      : (unsigned)ISD::FEXP2,
                       SL, VT, Mul, Flags);
  }

  EVT SetCCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);

  SDValue Threshold = DAG.getConstantFP(-0x1.5d58a0p+6f, SL, VT);
  SDValue NeedsScaling = DAG.getSetCC(SL, SetCCVT, X, Threshold, ISD::SETOLT);

  SDValue ScaleOffset = DAG.getConstantFP(0x1.0p+6f, SL, VT);

  SDValue ScaledX = DAG.getNode(ISD::FADD, SL, VT, X, ScaleOffset, Flags);

  SDValue AdjustedX =
      DAG.getNode(ISD::SELECT, SL, VT, NeedsScaling, ScaledX, X);

  SDValue ExpInput = DAG.getNode(ISD::FMUL, SL, VT, AdjustedX, Log2E, Flags);

  SDValue Exp2 = DAG.getNode(AMDGPUISD::EXP, SL, VT, ExpInput, Flags);

  SDValue ResultScaleFactor = DAG.getConstantFP(0x1.969d48p-93f, SL, VT);
  SDValue AdjustedResult =
      DAG.getNode(ISD::FMUL, SL, VT, Exp2, ResultScaleFactor, Flags);

  return DAG.getNode(ISD::SELECT, SL, VT, NeedsScaling, AdjustedResult, Exp2,
                     Flags);
}

/// Emit approx-funcs appropriate lowering for exp10. inf/nan should still be
/// handled correctly.
SDValue AMDGPUTargetLowering::lowerFEXP10Unsafe(SDValue X, const SDLoc &SL,
                                                SelectionDAG &DAG,
                                                SDNodeFlags Flags) const {
  const EVT VT = X.getValueType();
  const unsigned Exp2Op = VT == MVT::f32 ? AMDGPUISD::EXP : ISD::FEXP2;

  if (VT != MVT::f32 || !needsDenormHandlingF32(DAG, X, Flags)) {
    // exp2(x * 0x1.a92000p+1f) * exp2(x * 0x1.4f0978p-11f);
    SDValue K0 = DAG.getConstantFP(0x1.a92000p+1f, SL, VT);
    SDValue K1 = DAG.getConstantFP(0x1.4f0978p-11f, SL, VT);

    SDValue Mul0 = DAG.getNode(ISD::FMUL, SL, VT, X, K0, Flags);
    SDValue Exp2_0 = DAG.getNode(Exp2Op, SL, VT, Mul0, Flags);
    SDValue Mul1 = DAG.getNode(ISD::FMUL, SL, VT, X, K1, Flags);
    SDValue Exp2_1 = DAG.getNode(Exp2Op, SL, VT, Mul1, Flags);
    return DAG.getNode(ISD::FMUL, SL, VT, Exp2_0, Exp2_1);
  }

  // bool s = x < -0x1.2f7030p+5f;
  // x += s ? 0x1.0p+5f : 0.0f;
  // exp10 = exp2(x * 0x1.a92000p+1f) *
  //        exp2(x * 0x1.4f0978p-11f) *
  //        (s ? 0x1.9f623ep-107f : 1.0f);

  EVT SetCCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);

  SDValue Threshold = DAG.getConstantFP(-0x1.2f7030p+5f, SL, VT);
  SDValue NeedsScaling = DAG.getSetCC(SL, SetCCVT, X, Threshold, ISD::SETOLT);

  SDValue ScaleOffset = DAG.getConstantFP(0x1.0p+5f, SL, VT);
  SDValue ScaledX = DAG.getNode(ISD::FADD, SL, VT, X, ScaleOffset, Flags);
  SDValue AdjustedX =
      DAG.getNode(ISD::SELECT, SL, VT, NeedsScaling, ScaledX, X);

  SDValue K0 = DAG.getConstantFP(0x1.a92000p+1f, SL, VT);
  SDValue K1 = DAG.getConstantFP(0x1.4f0978p-11f, SL, VT);

  SDValue Mul0 = DAG.getNode(ISD::FMUL, SL, VT, AdjustedX, K0, Flags);
  SDValue Exp2_0 = DAG.getNode(Exp2Op, SL, VT, Mul0, Flags);
  SDValue Mul1 = DAG.getNode(ISD::FMUL, SL, VT, AdjustedX, K1, Flags);
  SDValue Exp2_1 = DAG.getNode(Exp2Op, SL, VT, Mul1, Flags);

  SDValue MulExps = DAG.getNode(ISD::FMUL, SL, VT, Exp2_0, Exp2_1, Flags);

  SDValue ResultScaleFactor = DAG.getConstantFP(0x1.9f623ep-107f, SL, VT);
  SDValue AdjustedResult =
      DAG.getNode(ISD::FMUL, SL, VT, MulExps, ResultScaleFactor, Flags);

  return DAG.getNode(ISD::SELECT, SL, VT, NeedsScaling, AdjustedResult, MulExps,
                     Flags);
}

SDValue AMDGPUTargetLowering::lowerFEXP(SDValue Op, SelectionDAG &DAG) const {
  EVT VT = Op.getValueType();
  SDLoc SL(Op);
  SDValue X = Op.getOperand(0);
  SDNodeFlags Flags = Op->getFlags();
  const bool IsExp10 = Op.getOpcode() == ISD::FEXP10;

  if (VT.getScalarType() == MVT::f16) {
    // v_exp_f16 (fmul x, log2e)
    if (allowApproxFunc(DAG, Flags)) // TODO: Does this really require fast?
      return lowerFEXPUnsafe(X, SL, DAG, Flags);

    if (VT.isVector())
      return SDValue();

    // exp(f16 x) ->
    //   fptrunc (v_exp_f32 (fmul (fpext x), log2e))

    // Nothing in half is a denormal when promoted to f32.
    SDValue Ext = DAG.getNode(ISD::FP_EXTEND, SL, MVT::f32, X, Flags);
    SDValue Lowered = lowerFEXPUnsafe(Ext, SL, DAG, Flags);
    return DAG.getNode(ISD::FP_ROUND, SL, VT, Lowered,
                       DAG.getTargetConstant(0, SL, MVT::i32), Flags);
  }

  assert(VT == MVT::f32);

  // TODO: Interpret allowApproxFunc as ignoring DAZ. This is currently copying
  // library behavior. Also, is known-not-daz source sufficient?
  if (allowApproxFunc(DAG, Flags)) {
    return IsExp10 ? lowerFEXP10Unsafe(X, SL, DAG, Flags)
                   : lowerFEXPUnsafe(X, SL, DAG, Flags);
  }

  //    Algorithm:
  //
  //    e^x = 2^(x/ln(2)) = 2^(x*(64/ln(2))/64)
  //
  //    x*(64/ln(2)) = n + f, |f| <= 0.5, n is integer
  //    n = 64*m + j,   0 <= j < 64
  //
  //    e^x = 2^((64*m + j + f)/64)
  //        = (2^m) * (2^(j/64)) * 2^(f/64)
  //        = (2^m) * (2^(j/64)) * e^(f*(ln(2)/64))
  //
  //    f = x*(64/ln(2)) - n
  //    r = f*(ln(2)/64) = x - n*(ln(2)/64)
  //
  //    e^x = (2^m) * (2^(j/64)) * e^r
  //
  //    (2^(j/64)) is precomputed
  //
  //    e^r = 1 + r + (r^2)/2! + (r^3)/3! + (r^4)/4! + (r^5)/5!
  //    e^r = 1 + q
  //
  //    q = r + (r^2)/2! + (r^3)/3! + (r^4)/4! + (r^5)/5!
  //
  //    e^x = (2^m) * ( (2^(j/64)) + q*(2^(j/64)) )
  SDNodeFlags FlagsNoContract = Flags;
  FlagsNoContract.setAllowContract(false);

  SDValue PH, PL;
  if (Subtarget->hasFastFMAF32()) {
    const float c_exp = numbers::log2ef;
    const float cc_exp = 0x1.4ae0bep-26f; // c+cc are 49 bits
    const float c_exp10 = 0x1.a934f0p+1f;
    const float cc_exp10 = 0x1.2f346ep-24f;

    SDValue C = DAG.getConstantFP(IsExp10 ? c_exp10 : c_exp, SL, VT);
    SDValue CC = DAG.getConstantFP(IsExp10 ? cc_exp10 : cc_exp, SL, VT);

    PH = DAG.getNode(ISD::FMUL, SL, VT, X, C, Flags);
    SDValue NegPH = DAG.getNode(ISD::FNEG, SL, VT, PH, Flags);
    SDValue FMA0 = DAG.getNode(ISD::FMA, SL, VT, X, C, NegPH, Flags);
    PL = DAG.getNode(ISD::FMA, SL, VT, X, CC, FMA0, Flags);
  } else {
    const float ch_exp = 0x1.714000p+0f;
    const float cl_exp = 0x1.47652ap-12f; // ch + cl are 36 bits

    const float ch_exp10 = 0x1.a92000p+1f;
    const float cl_exp10 = 0x1.4f0978p-11f;

    SDValue CH = DAG.getConstantFP(IsExp10 ? ch_exp10 : ch_exp, SL, VT);
    SDValue CL = DAG.getConstantFP(IsExp10 ? cl_exp10 : cl_exp, SL, VT);

    SDValue XAsInt = DAG.getNode(ISD::BITCAST, SL, MVT::i32, X);
    SDValue MaskConst = DAG.getConstant(0xfffff000, SL, MVT::i32);
    SDValue XHAsInt = DAG.getNode(ISD::AND, SL, MVT::i32, XAsInt, MaskConst);
    SDValue XH = DAG.getNode(ISD::BITCAST, SL, VT, XHAsInt);
    SDValue XL = DAG.getNode(ISD::FSUB, SL, VT, X, XH, Flags);

    PH = DAG.getNode(ISD::FMUL, SL, VT, XH, CH, Flags);

    SDValue XLCL = DAG.getNode(ISD::FMUL, SL, VT, XL, CL, Flags);
    SDValue Mad0 = getMad(DAG, SL, VT, XL, CH, XLCL, Flags);
    PL = getMad(DAG, SL, VT, XH, CL, Mad0, Flags);
  }

  SDValue E = DAG.getNode(ISD::FROUNDEVEN, SL, VT, PH, Flags);

  // It is unsafe to contract this fsub into the PH multiply.
  SDValue PHSubE = DAG.getNode(ISD::FSUB, SL, VT, PH, E, FlagsNoContract);

  SDValue A = DAG.getNode(ISD::FADD, SL, VT, PHSubE, PL, Flags);
  SDValue IntE = DAG.getNode(ISD::FP_TO_SINT, SL, MVT::i32, E);
  SDValue Exp2 = DAG.getNode(AMDGPUISD::EXP, SL, VT, A, Flags);

  SDValue R = DAG.getNode(ISD::FLDEXP, SL, VT, Exp2, IntE, Flags);

  SDValue UnderflowCheckConst =
      DAG.getConstantFP(IsExp10 ? -0x1.66d3e8p+5f : -0x1.9d1da0p+6f, SL, VT);

  EVT SetCCVT = getSetCCResultType(DAG.getDataLayout(), *DAG.getContext(), VT);
  SDValue Zero = DAG.getConstantFP(0.0, SL, VT);
  SDValue Underflow =
      DAG.getSetCC(SL, SetCCVT, X, UnderflowCheckConst, ISD::SETOLT);

  R = DAG.getNode(ISD::SELECT, SL, VT, Underflow, Zero, R);
  const auto &Options = getTargetMachine().Options;

  if (!Flags.hasNoInfs() && !Options.NoInfsFPMath) {
    SDValue OverflowCheckConst =
        DAG.getConstantFP(IsExp10 ? 0x1.344136p+5f : 0x1.62e430p+6f, SL, VT);
    SDValue Overflow =
        DAG.getSetCC(SL, SetCCVT, X, OverflowCheckConst, ISD::SETOGT);
    SDValue Inf =
        DAG.getConstantFP(APFloat::getInf(APFloat::IEEEsingle()), SL, VT);
    R = DAG.getNode(ISD::SELECT, SL, VT, Overflow, Inf, R);
  }

  return R;
}

static bool isCtlzOpc(unsigned Opc) {
  return Opc == ISD::CTLZ || Opc == ISD::CTLZ_ZERO_UNDEF;
}

static bool isCttzOpc(unsigned Opc) {
  return Opc == ISD::CTTZ || Opc == ISD::CTTZ_ZERO_UNDEF;
}

SDValue AMDGPUTargetLowering::lowerCTLZResults(SDValue Op,
                                               SelectionDAG &DAG) const {
  auto SL = SDLoc(Op);
  auto Opc = Op.getOpcode();
  auto Arg = Op.getOperand(0u);
  auto ResultVT = Op.getValueType();

  if (ResultVT != MVT::i8 && ResultVT != MVT::i16)
    return {};

  assert(isCtlzOpc(Opc));
  assert(ResultVT == Arg.getValueType());

  const uint64_t NumBits = ResultVT.getFixedSizeInBits();
  SDValue NumExtBits = DAG.getConstant(32u - NumBits, SL, MVT::i32);
  SDValue NewOp;

  if (Opc == ISD::CTLZ_ZERO_UNDEF) {
    NewOp = DAG.getNode(ISD::ANY_EXTEND, SL, MVT::i32, Arg);
    NewOp = DAG.getNode(ISD::SHL, SL, MVT::i32, NewOp, NumExtBits);
    NewOp = DAG.getNode(Opc, SL, MVT::i32, NewOp);
  } else {
    NewOp = DAG.getNode(ISD::ZERO_EXTEND, SL, MVT::i32, Arg);
    NewOp = DAG.getNode(Opc, SL, MVT::i32, NewOp);
    NewOp = DAG.getNode(ISD::SUB, SL, MVT::i32, NewOp, NumExtBits);
  }

  return DAG.getNode(ISD::TRUNCATE, SL, ResultVT, NewOp);
}

SDValue AMDGPUTargetLowering::LowerCTLZ_CTTZ(SDValue Op, SelectionDAG &DAG) const {
  SDLoc SL(Op);
  SDValue Src = Op.getOperand(0);

  assert(isCtlzOpc(Op.getOpcode()) || isCttzOpc(Op.getOpcode()));
  bool Ctlz = isCtlzOpc(Op.getOpcode());
  unsigned NewOpc = Ctlz ? AMDGPUISD::FFBH_U32 : AMDGPUISD::FFBL_B32;

  bool ZeroUndef = Op.getOpcode() == ISD::CTLZ_ZERO_UNDEF ||
                   Op.getOpcode() == ISD::CTTZ_ZERO_UNDEF;
  bool Is64BitScalar = !Src->isDivergent() && Src.getValueType() == MVT::i64;

  if (Src.getValueType() == MVT::i32 || Is64BitScalar) {
    // (ctlz hi:lo) -> (umin (ffbh src), 32)
    // (cttz hi:lo) -> (umin (ffbl src), 32)
    // (ctlz_zero_undef src) -> (ffbh src)
    // (cttz_zero_undef src) -> (ffbl src)

    //  64-bit scalar version produce 32-bit result
    // (ctlz hi:lo) -> (umin (S_FLBIT_I32_B64 src), 64)
    // (cttz hi:lo) -> (umin (S_FF1_I32_B64 src), 64)
    // (ctlz_zero_undef src) -> (S_FLBIT_I32_B64 src)
    // (cttz_zero_undef src) -> (S_FF1_I32_B64 src)
    SDValue NewOpr = DAG.getNode(NewOpc, SL, MVT::i32, Src);
    if (!ZeroUndef) {
      const SDValue ConstVal = DAG.getConstant(
          Op.getValueType().getScalarSizeInBits(), SL, MVT::i32);
      NewOpr = DAG.getNode(ISD::UMIN, SL, MVT::i32, NewOpr, ConstVal);
    }
    return DAG.getNode(ISD::ZERO_EXTEND, SL, Src.getValueType(), NewOpr);
  }

  SDValue Lo, Hi;
  std::tie(Lo, Hi) = split64BitValue(Src, DAG);

  SDValue OprLo = DAG.getNode(NewOpc, SL, MVT::i32, Lo);
  SDValue OprHi = DAG.getNode(NewOpc, SL, MVT::i32, Hi);

  // (ctlz hi:lo) -> (umin3 (ffbh hi), (uaddsat (ffbh lo), 32), 64)
  // (cttz hi:lo) -> (umin3 (uaddsat (ffbl hi), 32), (ffbl lo), 64)
  // (ctlz_zero_undef hi:lo) -> (umin (ffbh hi), (add (ffbh lo), 32))
  // (cttz_zero_undef hi:lo) -> (umin (add (ffbl hi), 32), (ffbl lo))

  unsigned AddOpc = ZeroUndef ? ISD::ADD : ISD::UADDSAT;
  const SDValue Const32 = DAG.getConstant(32, SL, MVT::i32);
  if (Ctlz)
    OprLo = DAG.getNode(AddOpc, SL, MVT::i32, OprLo, Const32);
  else
    OprHi = DAG.getNode(AddOpc, SL, MVT::i32, OprHi, Const32);

  SDValue NewOpr;
  NewOpr = DAG.getNode(ISD::UMIN, SL, MVT::i32, OprLo, OprHi);
  if (!ZeroUndef) {
    const SDValue Const64 = DAG.getConstant(64, SL, MVT::i32);
    NewOpr = DAG.getNode(ISD::UMIN, SL, MVT::i32, NewOpr, Const64);
  }

  return DAG.getNode(ISD::ZERO_EXTEND, SL, MVT::i64, NewOpr);
}

SDValue AMDGPUTargetLowering::LowerINT_TO_FP32(SDValue Op, SelectionDAG &DAG,
                                               bool Signed) const {
  // The regular method converting a 64-bit integer to float roughly consists of
  // 2 steps: normalization and rounding. In fact, after normalization, the
  // conversion from a 64-bit integer to a float is essentially the same as the
  // one from a 32-bit integer. The only difference is that it has more
  // trailing bits to be rounded. To leverage the native 32-bit conversion, a
  // 64-bit integer could be preprocessed and fit into a 32-bit integer then
  // converted into the correct float number. The basic steps for the unsigned
  // conversion are illustrated in the following pseudo code:
  //
  // f32 uitofp(i64 u) {
  //   i32 hi, lo = split(u);
  //   // Only count the leading zeros in hi as we have native support of the
  //   // conversion from i32 to f32. If hi is all 0s, the conversion is
  //   // reduced to a 32-bit one automatically.
  //   i32 shamt = clz(hi); // Return 32 if hi is all 0s.
  //   u <<= shamt;
  //   hi, lo = split(u);
  //   hi |= (lo != 0) ? 1 : 0; // Adjust rounding bit in hi based on lo.
  //   // convert it as a 32-bit integer and scale the result back.
  //   return uitofp(hi) * 2^(32 - shamt);
  // }
  //
  // The signed one follows the same principle but uses 'ffbh_i32' to count its
  // sign bits instead. If 'ffbh_i32' is not available, its absolute value is
  // converted instead followed by negation based its sign bit.

  SDLoc SL(Op);
  SDValue Src = Op.getOperand(0);

  SDValue Lo, Hi;
  std::tie(Lo, Hi) = split64BitValue(Src, DAG);
  SDValue Sign;
  SDValue ShAmt;
  if (Signed && Subtarget->isGCN()) {
    // We also need to consider the sign bit in Lo if Hi has just sign bits,
    // i.e. Hi is 0 or -1. However, that only needs to take the MSB into
    // account. That is, the maximal shift is
    // - 32 if Lo and Hi have opposite signs;
    // - 33 if Lo and Hi have the same sign.
    //
    // Or, MaxShAmt = 33 + OppositeSign, where
    //
    // OppositeSign is defined as ((Lo ^ Hi) >> 31), which is
    // - -1 if Lo and Hi have opposite signs; and
    // -  0 otherwise.
    //
    // All in all, ShAmt is calculated as
    //
    //  umin(sffbh(Hi), 33 + (Lo^Hi)>>31) - 1.
    //
    // or
    //
    //  umin(sffbh(Hi) - 1, 32 + (Lo^Hi)>>31).
    //
    // to reduce the critical path.
    SDValue OppositeSign = DAG.getNode(
        ISD::SRA, SL, MVT::i32, DAG.getNode(ISD::XOR, SL, MVT::i32, Lo, Hi),
        DAG.getConstant(31, SL, MVT::i32));
    SDValue MaxShAmt =
        DAG.getNode(ISD::ADD, SL, MVT::i32, DAG.getConstant(32, SL, MVT::i32),
                    OppositeSign);
    // Count the leading sign bits.
    ShAmt = DAG.getNode(AMDGPUISD::FFBH_I32, SL, MVT::i32, Hi);
    // Different from unsigned conversion, the shift should be one bit less to
    // preserve the sign bit.
    ShAmt = DAG.getNode(ISD::SUB, SL, MVT::i32, ShAmt,
                        DAG.getConstant(1, SL, MVT::i32));
    ShAmt = DAG.getNode(ISD::UMIN, SL, MVT::i32, ShAmt, MaxShAmt);
  } else {
    if (Signed) {
      // Without 'ffbh_i32', only leading zeros could be counted. Take the
      // absolute value first.
      Sign = DAG.getNode(ISD::SRA, SL, MVT::i64, Src,
                         DAG.getConstant(63, SL, MVT::i64));
      SDValue Abs =
          DAG.getNode(ISD::XOR, SL, MVT::i64,
                      DAG.getNode(ISD::ADD, SL, MVT::i64, Src, Sign), Sign);
      std::tie(Lo, Hi) = split64BitValue(Abs, DAG);
    }
    // Count the leading zeros.
    ShAmt = DAG.getNode(ISD::CTLZ, SL, MVT::i32, Hi);
    // The shift amount for signed integers is [0, 32].
  }
  // Normalize the given 64-bit integer.
  SDValue Norm = DAG.getNode(ISD::SHL, SL, MVT::i64, Src, ShAmt);
  // Split it again.
  std::tie(Lo, Hi) = split64BitValue(Norm, DAG);
  // Calculate the adjust bit for rounding.
  // (lo != 0) ? 1 : 0 => (lo >= 1) ? 1 : 0 => umin(1, lo)
  SDValue Adjust = DAG.getNode(ISD::UMIN, SL, MVT::i32,
                               DAG.getConstant(1, SL, MVT::i32), Lo);
  // Get the 32-bit normalized integer.
  Norm = DAG.getNode(ISD::OR, SL, MVT::i32, Hi, Adjust);
  // Convert the normalized 32-bit integer into f32.
  unsigned Opc =
      (Signed && Subtarget->isGCN()) ? ISD::SINT_TO_FP : ISD::UINT_TO_FP;
  SDValue FVal = DAG.getNode(Opc, SL, MVT::f32, Norm);

  // Finally, need to scale back the converted floating number as the original
  // 64-bit integer is converted as a 32-bit one.
  ShAmt = DAG.getNode(ISD::SUB, SL, MVT::i32, DAG.getConstant(32, SL, MVT::i32),
                      ShAmt);
  // On GCN, use LDEXP directly.
  if (Subtarget->isGCN())
    return DAG.getNode(ISD::FLDEXP, SL, MVT::f32, FVal, ShAmt);

  // Otherwise, align 'ShAmt' to the exponent part and add it into the exponent
  // part directly to emulate the multiplication of 2^ShAmt. That 8-bit
  // exponent is enough to avoid overflowing into the sign bit.
  SDValue Exp = DAG.getNode(ISD::SHL, SL, MVT::i32, ShAmt,
                            DAG.getConstant(23, SL, MVT::i32));
  SDValue IVal =
      DAG.getNode(ISD::ADD, SL, MVT::i32,
                  DAG.getNode(ISD::BITCAST, SL, MVT::i32, FVal), Exp);
  if (Signed) {
    // Set the sign bit.
    Sign = DAG.getNode(ISD::SHL, SL, MVT::i32,
                       DAG.getNode(ISD::TRUNCATE, SL, MVT::i32, Sign),
                       DAG.getConstant(31, SL, MVT::i32));
    IVal = DAG.getNode(ISD::OR, SL, MVT::i32, IVal, Sign);
  }
  return DAG.getNode(ISD::BITCAST, SL, MVT::f32, IVal);
}

SDValue AMDGPUTargetLowering::LowerINT_TO_FP64(SDValue Op, SelectionDAG &DAG,
                                               bool Signed) const {
  SDLoc SL(Op);
  SDValue Src = Op.getOperand(0);

  SDValue Lo, Hi;
  std::tie(Lo, Hi) = split64BitValue(Src, DAG);

  SDValue CvtHi = DAG.getNode(Signed ? ISD::SINT_TO_FP : ISD::UINT_TO_FP,
                              SL, MVT::f64, Hi);

  SDValue CvtLo = DAG.getNode(ISD::UINT_TO_FP, SL, MVT::f64, Lo);

  SDValue LdExp = DAG.getNode(ISD::FLDEXP, SL, MVT::f64, CvtHi,
                              DAG.getConstant(32, SL, MVT::i32));
  // TODO: Should this propagate fast-math-flags?
  return DAG.getNode(ISD::FADD, SL, MVT::f64, LdExp, CvtLo);
}

SDValue AMDGPUTargetLowering::LowerUINT_TO_FP(SDValue Op,
                                               SelectionDAG &DAG) const {
  // TODO: Factor out code common with LowerSINT_TO_FP.
  EVT DestVT = Op.getValueType();
  SDValue Src = Op.getOperand(0);
  EVT SrcVT = Src.getValueType();

  if (SrcVT == MVT::i16) {
    if (DestVT == MVT::f16)
      return Op;
    SDLoc DL(Op);

    // Promote src to i32
    SDValue Ext = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i32, Src);
    return DAG.getNode(ISD::UINT_TO_FP, DL, DestVT, Ext);
  }

  if (DestVT == MVT::bf16) {
    SDLoc SL(Op);
    SDValue ToF32 = DAG.getNode(ISD::UINT_TO_FP, SL, MVT::f32, Src);
    SDValue FPRoundFlag = DAG.getIntPtrConstant(0, SL, /*isTarget=*/true);
    return DAG.getNode(ISD::FP_ROUND, SL, MVT::bf16, ToF32, FPRoundFlag);
  }

  if (SrcVT != MVT::i64)
    return Op;

  if (Subtarget->has16BitInsts() && DestVT == MVT::f16) {
    SDLoc DL(Op);

    SDValue IntToFp32 = DAG.getNode(Op.getOpcode(), DL, MVT::f32, Src);
    SDValue FPRoundFlag =
        DAG.getIntPtrConstant(0, SDLoc(Op), /*isTarget=*/true);
    SDValue FPRound =
        DAG.getNode(ISD::FP_ROUND, DL, MVT::f16, IntToFp32, FPRoundFlag);

    return FPRound;
  }

  if (DestVT == MVT::f32)
    return LowerINT_TO_FP32(Op, DAG, false);

  assert(DestVT == MVT::f64);
  return LowerINT_TO_FP64(Op, DAG, false);
}

SDValue AMDGPUTargetLowering::LowerSINT_TO_FP(SDValue Op,
                                              SelectionDAG &DAG) const {
  EVT DestVT = Op.getValueType();

  SDValue Src = Op.getOperand(0);
  EVT SrcVT = Src.getValueType();

  if (SrcVT == MVT::i16) {
    if (DestVT == MVT::f16)
      return Op;

    SDLoc DL(Op);
    // Promote src to i32
    SDValue Ext = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i32, Src);
    return DAG.getNode(ISD::SINT_TO_FP, DL, DestVT, Ext);
  }

  if (DestVT == MVT::bf16) {
    SDLoc SL(Op);
    SDValue ToF32 = DAG.getNode(ISD::SINT_TO_FP, SL, MVT::f32, Src);
    SDValue FPRoundFlag = DAG.getIntPtrConstant(0, SL, /*isTarget=*/true);
    return DAG.getNode(ISD::FP_ROUND, SL, MVT::bf16, ToF32, FPRoundFlag);
  }

  if (SrcVT != MVT::i64)
    return Op;

  // TODO: Factor out code common with LowerUINT_TO_FP.

  if (Subtarget->has16BitInsts() && DestVT == MVT::f16) {
    SDLoc DL(Op);
    SDValue Src = Op.getOperand(0);

    SDValue IntToFp32 = DAG.getNode(Op.getOpcode(), DL, MVT::f32, Src);
    SDValue FPRoundFlag =
        DAG.getIntPtrConstant(0, SDLoc(Op), /*isTarget=*/true);
    SDValue FPRound =
        DAG.getNode(ISD::FP_ROUND, DL, MVT::f16, IntToFp32, FPRoundFlag);

    return FPRound;
  }

  if (DestVT == MVT::f32)
    return LowerINT_TO_FP32(Op, DAG, true);

  assert(DestVT == MVT::f64);
  return LowerINT_TO_FP64(Op, DAG, true);
}

SDValue AMDGPUTargetLowering::LowerFP_TO_INT64(SDValue Op, SelectionDAG &DAG,
                                               bool Signed) const {
  SDLoc SL(Op);

  SDValue Src = Op.getOperand(0);
  EVT SrcVT = Src.getValueType();

  assert(SrcVT == MVT::f32 || SrcVT == MVT::f64);

  // The basic idea of converting a floating point number into a pair of 32-bit
  // integers is illustrated as follows:
  //
  //     tf := trunc(val);
  //    hif := floor(tf * 2^-32);
  //    lof := tf - hif * 2^32; // lof is always positive due to floor.
  //     hi := fptoi(hif);
  //     lo := fptoi(lof);
  //
  SDValue Trunc = DAG.getNode(ISD::FTRUNC, SL, SrcVT, Src);
  SDValue Sign;
  if (Signed && SrcVT == MVT::f32) {
    // However, a 32-bit floating point number has only 23 bits mantissa and
    // it's not enough to hold all the significant bits of `lof` if val is
    // negative. To avoid the loss of precision, We need to take the absolute
    // value after truncating and flip the result back based on the original
    // signedness.
    Sign = DAG.getNode(ISD::SRA, SL, MVT::i32,
                       DAG.getNode(ISD::BITCAST, SL, MVT::i32, Trunc),
                       DAG.getConstant(31, SL, MVT::i32));
    Trunc = DAG.getNode(ISD::FABS, SL, SrcVT, Trunc);
  }

  SDValue K0, K1;
  if (SrcVT == MVT::f64) {
    K0 = DAG.getConstantFP(
        llvm::bit_cast<double>(UINT64_C(/*2^-32*/ 0x3df0000000000000)), SL,
        SrcVT);
    K1 = DAG.getConstantFP(
        llvm::bit_cast<double>(UINT64_C(/*-2^32*/ 0xc1f0000000000000)), SL,
        SrcVT);
  } else {
    K0 = DAG.getConstantFP(
        llvm::bit_cast<float>(UINT32_C(/*2^-32*/ 0x2f800000)), SL, SrcVT);
    K1 = DAG.getConstantFP(
        llvm::bit_cast<float>(UINT32_C(/*-2^32*/ 0xcf800000)), SL, SrcVT);
  }
  // TODO: Should this propagate fast-math-flags?
  SDValue Mul = DAG.getNode(ISD::FMUL, SL, SrcVT, Trunc, K0);

  SDValue FloorMul = DAG.getNode(ISD::FFLOOR, SL, SrcVT, Mul);

  SDValue Fma = DAG.getNode(ISD::FMA, SL, SrcVT, FloorMul, K1, Trunc);

  SDValue Hi = DAG.getNode((Signed && SrcVT == MVT::f64) ? ISD::FP_TO_SINT
                                                         : ISD::FP_TO_UINT,
                           SL, MVT::i32, FloorMul);
  SDValue Lo = DAG.getNode(ISD::FP_TO_UINT, SL, MVT::i32, Fma);

  SDValue Result = DAG.getNode(ISD::BITCAST, SL, MVT::i64,
                               DAG.getBuildVector(MVT::v2i32, SL, {Lo, Hi}));

  if (Signed && SrcVT == MVT::f32) {
    assert(Sign);
    // Flip the result based on the signedness, which is either all 0s or 1s.
    Sign = DAG.getNode(ISD::BITCAST, SL, MVT::i64,
                       DAG.getBuildVector(MVT::v2i32, SL, {Sign, Sign}));
    // r := xor(r, sign) - sign;
    Result =
        DAG.getNode(ISD::SUB, SL, MVT::i64,
                    DAG.getNode(ISD::XOR, SL, MVT::i64, Result, Sign), Sign);
  }

  return Result;
}

SDValue AMDGPUTargetLowering::LowerFP_TO_FP16(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue N0 = Op.getOperand(0);

  // Convert to target node to get known bits
  if (N0.getValueType() == MVT::f32)
    return DAG.getNode(AMDGPUISD::FP_TO_FP16, DL, Op.getValueType(), N0);

  if (getTargetMachine().Options.UnsafeFPMath) {
    // There is a generic expand for FP_TO_FP16 with unsafe fast math.
    return SDValue();
  }

  assert(N0.getSimpleValueType() == MVT::f64);

  // f64 -> f16 conversion using round-to-nearest-even rounding mode.
  const unsigned ExpMask = 0x7ff;
  const unsigned ExpBiasf64 = 1023;
  const unsigned ExpBiasf16 = 15;
  SDValue Zero = DAG.getConstant(0, DL, MVT::i32);
  SDValue One = DAG.getConstant(1, DL, MVT::i32);
  SDValue U = DAG.getNode(ISD::BITCAST, DL, MVT::i64, N0);
  SDValue UH = DAG.getNode(ISD::SRL, DL, MVT::i64, U,
                           DAG.getConstant(32, DL, MVT::i64));
  UH = DAG.getZExtOrTrunc(UH, DL, MVT::i32);
  U = DAG.getZExtOrTrunc(U, DL, MVT::i32);
  SDValue E = DAG.getNode(ISD::SRL, DL, MVT::i32, UH,
                          DAG.getConstant(20, DL, MVT::i64));
  E = DAG.getNode(ISD::AND, DL, MVT::i32, E,
                  DAG.getConstant(ExpMask, DL, MVT::i32));
  // Subtract the fp64 exponent bias (1023) to get the real exponent and
  // add the f16 bias (15) to get the biased exponent for the f16 format.
  E = DAG.getNode(ISD::ADD, DL, MVT::i32, E,
                  DAG.getConstant(-ExpBiasf64 + ExpBiasf16, DL, MVT::i32));

  SDValue M = DAG.getNode(ISD::SRL, DL, MVT::i32, UH,
                          DAG.getConstant(8, DL, MVT::i32));
  M = DAG.getNode(ISD::AND, DL, MVT::i32, M,
                  DAG.getConstant(0xffe, DL, MVT::i32));

  SDValue MaskedSig = DAG.getNode(ISD::AND, DL, MVT::i32, UH,
                                  DAG.getConstant(0x1ff, DL, MVT::i32));
  MaskedSig = DAG.getNode(ISD::OR, DL, MVT::i32, MaskedSig, U);

  SDValue Lo40Set = DAG.getSelectCC(DL, MaskedSig, Zero, Zero, One, ISD::SETEQ);
  M = DAG.getNode(ISD::OR, DL, MVT::i32, M, Lo40Set);

  // (M != 0 ? 0x0200 : 0) | 0x7c00;
  SDValue I = DAG.getNode(ISD::OR, DL, MVT::i32,
      DAG.getSelectCC(DL, M, Zero, DAG.getConstant(0x0200, DL, MVT::i32),
                      Zero, ISD::SETNE), DAG.getConstant(0x7c00, DL, MVT::i32));

  // N = M | (E << 12);
  SDValue N = DAG.getNode(ISD::OR, DL, MVT::i32, M,
      DAG.getNode(ISD::SHL, DL, MVT::i32, E,
                  DAG.getConstant(12, DL, MVT::i32)));

  // B = clamp(1-E, 0, 13);
  SDValue OneSubExp = DAG.getNode(ISD::SUB, DL, MVT::i32,
                                  One, E);
  SDValue B = DAG.getNode(ISD::SMAX, DL, MVT::i32, OneSubExp, Zero);
  B = DAG.getNode(ISD::SMIN, DL, MVT::i32, B,
                  DAG.getConstant(13, DL, MVT::i32));

  SDValue SigSetHigh = DAG.getNode(ISD::OR, DL, MVT::i32, M,
                                   DAG.getConstant(0x1000, DL, MVT::i32));

  SDValue D = DAG.getNode(ISD::SRL, DL, MVT::i32, SigSetHigh, B);
  SDValue D0 = DAG.getNode(ISD::SHL, DL, MVT::i32, D, B);
  SDValue D1 = DAG.getSelectCC(DL, D0, SigSetHigh, One, Zero, ISD::SETNE);
  D = DAG.getNode(ISD::OR, DL, MVT::i32, D, D1);

  SDValue V = DAG.getSelectCC(DL, E, One, D, N, ISD::SETLT);
  SDValue VLow3 = DAG.getNode(ISD::AND, DL, MVT::i32, V,
                              DAG.getConstant(0x7, DL, MVT::i32));
  V = DAG.getNode(ISD::SRL, DL, MVT::i32, V,
                  DAG.getConstant(2, DL, MVT::i32));
  SDValue V0 = DAG.getSelectCC(DL, VLow3, DAG.getConstant(3, DL, MVT::i32),
                               One, Zero, ISD::SETEQ);
  SDValue V1 = DAG.getSelectCC(DL, VLow3, DAG.getConstant(5, DL, MVT::i32),
                               One, Zero, ISD::SETGT);
  V1 = DAG.getNode(ISD::OR, DL, MVT::i32, V0, V1);
  V = DAG.getNode(ISD::ADD, DL, MVT::i32, V, V1);

  V = DAG.getSelectCC(DL, E, DAG.getConstant(30, DL, MVT::i32),
                      DAG.getConstant(0x7c00, DL, MVT::i32), V, ISD::SETGT);
  V = DAG.getSelectCC(DL, E, DAG.getConstant(1039, DL, MVT::i32),
                      I, V, ISD::SETEQ);

  // Extract the sign bit.
  SDValue Sign = DAG.getNode(ISD::SRL, DL, MVT::i32, UH,
                            DAG.getConstant(16, DL, MVT::i32));
  Sign = DAG.getNode(ISD::AND, DL, MVT::i32, Sign,
                     DAG.getConstant(0x8000, DL, MVT::i32));

  V = DAG.getNode(ISD::OR, DL, MVT::i32, Sign, V);
  return DAG.getZExtOrTrunc(V, DL, Op.getValueType());
}

SDValue AMDGPUTargetLowering::LowerFP_TO_INT(const SDValue Op,
                                             SelectionDAG &DAG) const {
  SDValue Src = Op.getOperand(0);
  unsigned OpOpcode = Op.getOpcode();
  EVT SrcVT = Src.getValueType();
  EVT DestVT = Op.getValueType();

  // Will be selected natively
  if (SrcVT == MVT::f16 && DestVT == MVT::i16)
    return Op;

  if (SrcVT == MVT::bf16) {
    SDLoc DL(Op);
    SDValue PromotedSrc = DAG.getNode(ISD::FP_EXTEND, DL, MVT::f32, Src);
    return DAG.getNode(Op.getOpcode(), DL, DestVT, PromotedSrc);
  }

  // Promote i16 to i32
  if (DestVT == MVT::i16 && (SrcVT == MVT::f32 || SrcVT == MVT::f64)) {
    SDLoc DL(Op);

    SDValue FpToInt32 = DAG.getNode(OpOpcode, DL, MVT::i32, Src);
    return DAG.getNode(ISD::TRUNCATE, DL, MVT::i16, FpToInt32);
  }

  if (DestVT != MVT::i64)
    return Op;

  if (SrcVT == MVT::f16 ||
      (SrcVT == MVT::f32 && Src.getOpcode() == ISD::FP16_TO_FP)) {
    SDLoc DL(Op);

    SDValue FpToInt32 = DAG.getNode(OpOpcode, DL, MVT::i32, Src);
    unsigned Ext =
        OpOpcode == ISD::FP_TO_SINT ? ISD::SIGN_EXTEND : ISD::ZERO_EXTEND;
    return DAG.getNode(Ext, DL, MVT::i64, FpToInt32);
  }

  if (SrcVT == MVT::f32 || SrcVT == MVT::f64)
    return LowerFP_TO_INT64(Op, DAG, OpOpcode == ISD::FP_TO_SINT);

  return SDValue();
}

SDValue AMDGPUTargetLowering::LowerSIGN_EXTEND_INREG(SDValue Op,
                                                     SelectionDAG &DAG) const {
  EVT ExtraVT = cast<VTSDNode>(Op.getOperand(1))->getVT();
  MVT VT = Op.getSimpleValueType();
  MVT ScalarVT = VT.getScalarType();

  assert(VT.isVector());

  SDValue Src = Op.getOperand(0);
  SDLoc DL(Op);

  // TODO: Don't scalarize on Evergreen?
  unsigned NElts = VT.getVectorNumElements();
  SmallVector<SDValue, 8> Args;
  DAG.ExtractVectorElements(Src, Args, 0, NElts);

  SDValue VTOp = DAG.getValueType(ExtraVT.getScalarType());
  for (unsigned I = 0; I < NElts; ++I)
    Args[I] = DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, ScalarVT, Args[I], VTOp);

  return DAG.getBuildVector(VT, DL, Args);
}

//===----------------------------------------------------------------------===//
// Custom DAG optimizations
//===----------------------------------------------------------------------===//

static bool isU24(SDValue Op, SelectionDAG &DAG) {
  return AMDGPUTargetLowering::numBitsUnsigned(Op, DAG) <= 24;
}

static bool isI24(SDValue Op, SelectionDAG &DAG) {
  EVT VT = Op.getValueType();
  return VT.getSizeInBits() >= 24 && // Types less than 24-bit should be treated
                                     // as unsigned 24-bit values.
         AMDGPUTargetLowering::numBitsSigned(Op, DAG) <= 24;
}

static SDValue simplifyMul24(SDNode *Node24,
                             TargetLowering::DAGCombinerInfo &DCI) {
  SelectionDAG &DAG = DCI.DAG;
  const TargetLowering &TLI = DAG.getTargetLoweringInfo();
  bool IsIntrin = Node24->getOpcode() == ISD::INTRINSIC_WO_CHAIN;

  SDValue LHS = IsIntrin ? Node24->getOperand(1) : Node24->getOperand(0);
  SDValue RHS = IsIntrin ? Node24->getOperand(2) : Node24->getOperand(1);
  unsigned NewOpcode = Node24->getOpcode();
  if (IsIntrin) {
    unsigned IID = Node24->getConstantOperandVal(0);
    switch (IID) {
    case Intrinsic::amdgcn_mul_i24:
      NewOpcode = AMDGPUISD::MUL_I24;
      break;
    case Intrinsic::amdgcn_mul_u24:
      NewOpcode = AMDGPUISD::MUL_U24;
      break;
    case Intrinsic::amdgcn_mulhi_i24:
      NewOpcode = AMDGPUISD::MULHI_I24;
      break;
    case Intrinsic::amdgcn_mulhi_u24:
      NewOpcode = AMDGPUISD::MULHI_U24;
      break;
    default:
      llvm_unreachable("Expected 24-bit mul intrinsic");
    }
  }

  APInt Demanded = APInt::getLowBitsSet(LHS.getValueSizeInBits(), 24);

  // First try to simplify using SimplifyMultipleUseDemandedBits which allows
  // the operands to have other uses, but will only perform simplifications that
  // involve bypassing some nodes for this user.
  SDValue DemandedLHS = TLI.SimplifyMultipleUseDemandedBits(LHS, Demanded, DAG);
  SDValue DemandedRHS = TLI.SimplifyMultipleUseDemandedBits(RHS, Demanded, DAG);
  if (DemandedLHS || DemandedRHS)
    return DAG.getNode(NewOpcode, SDLoc(Node24), Node24->getVTList(),
                       DemandedLHS ? DemandedLHS : LHS,
                       DemandedRHS ? DemandedRHS : RHS);

  // Now try SimplifyDemandedBits which can simplify the nodes used by our
  // operands if this node is the only user.
  if (TLI.SimplifyDemandedBits(LHS, Demanded, DCI))
    return SDValue(Node24, 0);
  if (TLI.SimplifyDemandedBits(RHS, Demanded, DCI))
    return SDValue(Node24, 0);

  return SDValue();
}

template <typename IntTy>
static SDValue constantFoldBFE(SelectionDAG &DAG, IntTy Src0, uint32_t Offset,
                               uint32_t Width, const SDLoc &DL) {
  if (Width + Offset < 32) {
    uint32_t Shl = static_cast<uint32_t>(Src0) << (32 - Offset - Width);
    IntTy Result = static_cast<IntTy>(Shl) >> (32 - Width);
    return DAG.getConstant(Result, DL, MVT::i32);
  }

  return DAG.getConstant(Src0 >> Offset, DL, MVT::i32);
}

static bool hasVolatileUser(SDNode *Val) {
  for (SDNode *U : Val->uses()) {
    if (MemSDNode *M = dyn_cast<MemSDNode>(U)) {
      if (M->isVolatile())
        return true;
    }
  }

  return false;
}

bool AMDGPUTargetLowering::shouldCombineMemoryType(EVT VT) const {
  // i32 vectors are the canonical memory type.
  if (VT.getScalarType() == MVT::i32 || isTypeLegal(VT))
    return false;

  if (!VT.isByteSized())
    return false;

  unsigned Size = VT.getStoreSize();

  if ((Size == 1 || Size == 2 || Size == 4) && !VT.isVector())
    return false;

  if (Size == 3 || (Size > 4 && (Size % 4 != 0)))
    return false;

  return true;
}

// Replace load of an illegal type with a store of a bitcast to a friendlier
// type.
SDValue AMDGPUTargetLowering::performLoadCombine(SDNode *N,
                                                 DAGCombinerInfo &DCI) const {
  if (!DCI.isBeforeLegalize())
    return SDValue();

  LoadSDNode *LN = cast<LoadSDNode>(N);
  if (!LN->isSimple() || !ISD::isNormalLoad(LN) || hasVolatileUser(LN))
    return SDValue();

  SDLoc SL(N);
  SelectionDAG &DAG = DCI.DAG;
  EVT VT = LN->getMemoryVT();

  unsigned Size = VT.getStoreSize();
  Align Alignment = LN->getAlign();
  if (Alignment < Size && isTypeLegal(VT)) {
    unsigned IsFast;
    unsigned AS = LN->getAddressSpace();

    // Expand unaligned loads earlier than legalization. Due to visitation order
    // problems during legalization, the emitted instructions to pack and unpack
    // the bytes again are not eliminated in the case of an unaligned copy.
    if (!allowsMisalignedMemoryAccesses(
            VT, AS, Alignment, LN->getMemOperand()->getFlags(), &IsFast)) {
      if (VT.isVector())
        return SplitVectorLoad(SDValue(LN, 0), DAG);

      SDValue Ops[2];
      std::tie(Ops[0], Ops[1]) = expandUnalignedLoad(LN, DAG);

      return DAG.getMergeValues(Ops, SDLoc(N));
    }

    if (!IsFast)
      return SDValue();
  }

  if (!shouldCombineMemoryType(VT))
    return SDValue();

  EVT NewVT = getEquivalentMemType(*DAG.getContext(), VT);

  SDValue NewLoad
    = DAG.getLoad(NewVT, SL, LN->getChain(),
                  LN->getBasePtr(), LN->getMemOperand());

  SDValue BC = DAG.getNode(ISD::BITCAST, SL, VT, NewLoad);
  DCI.CombineTo(N, BC, NewLoad.getValue(1));
  return SDValue(N, 0);
}

// Replace store of an illegal type with a store of a bitcast to a friendlier
// type.
SDValue AMDGPUTargetLowering::performStoreCombine(SDNode *N,
                                                  DAGCombinerInfo &DCI) const {
  if (!DCI.isBeforeLegalize())
    return SDValue();

  StoreSDNode *SN = cast<StoreSDNode>(N);
  if (!SN->isSimple() || !ISD::isNormalStore(SN))
    return SDValue();

  EVT VT = SN->getMemoryVT();
  unsigned Size = VT.getStoreSize();

  SDLoc SL(N);
  SelectionDAG &DAG = DCI.DAG;
  Align Alignment = SN->getAlign();
  if (Alignment < Size && isTypeLegal(VT)) {
    unsigned IsFast;
    unsigned AS = SN->getAddressSpace();

    // Expand unaligned stores earlier than legalization. Due to visitation
    // order problems during legalization, the emitted instructions to pack and
    // unpack the bytes again are not eliminated in the case of an unaligned
    // copy.
    if (!allowsMisalignedMemoryAccesses(
            VT, AS, Alignment, SN->getMemOperand()->getFlags(), &IsFast)) {
      if (VT.isVector())
        return SplitVectorStore(SDValue(SN, 0), DAG);

      return expandUnalignedStore(SN, DAG);
    }

    if (!IsFast)
      return SDValue();
  }

  if (!shouldCombineMemoryType(VT))
    return SDValue();

  EVT NewVT = getEquivalentMemType(*DAG.getContext(), VT);
  SDValue Val = SN->getValue();

  //DCI.AddToWorklist(Val.getNode());

  bool OtherUses = !Val.hasOneUse();
  SDValue CastVal = DAG.getNode(ISD::BITCAST, SL, NewVT, Val);
  if (OtherUses) {
    SDValue CastBack = DAG.getNode(ISD::BITCAST, SL, VT, CastVal);
    DAG.ReplaceAllUsesOfValueWith(Val, CastBack);
  }

  return DAG.getStore(SN->getChain(), SL, CastVal,
                      SN->getBasePtr(), SN->getMemOperand());
}

// FIXME: This should go in generic DAG combiner with an isTruncateFree check,
// but isTruncateFree is inaccurate for i16 now because of SALU vs. VALU
// issues.
SDValue AMDGPUTargetLowering::performAssertSZExtCombine(SDNode *N,
                                                        DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDValue N0 = N->getOperand(0);

  // (vt2 (assertzext (truncate vt0:x), vt1)) ->
  //     (vt2 (truncate (assertzext vt0:x, vt1)))
  if (N0.getOpcode() == ISD::TRUNCATE) {
    SDValue N1 = N->getOperand(1);
    EVT ExtVT = cast<VTSDNode>(N1)->getVT();
    SDLoc SL(N);

    SDValue Src = N0.getOperand(0);
    EVT SrcVT = Src.getValueType();
    if (SrcVT.bitsGE(ExtVT)) {
      SDValue NewInReg = DAG.getNode(N->getOpcode(), SL, SrcVT, Src, N1);
      return DAG.getNode(ISD::TRUNCATE, SL, N->getValueType(0), NewInReg);
    }
  }

  return SDValue();
}

SDValue AMDGPUTargetLowering::performIntrinsicWOChainCombine(
  SDNode *N, DAGCombinerInfo &DCI) const {
  unsigned IID = N->getConstantOperandVal(0);
  switch (IID) {
  case Intrinsic::amdgcn_mul_i24:
  case Intrinsic::amdgcn_mul_u24:
  case Intrinsic::amdgcn_mulhi_i24:
  case Intrinsic::amdgcn_mulhi_u24:
    return simplifyMul24(N, DCI);
  case Intrinsic::amdgcn_fract:
  case Intrinsic::amdgcn_rsq:
  case Intrinsic::amdgcn_rcp_legacy:
  case Intrinsic::amdgcn_rsq_legacy:
  case Intrinsic::amdgcn_rsq_clamp: {
    // FIXME: This is probably wrong. If src is an sNaN, it won't be quieted
    SDValue Src = N->getOperand(1);
    return Src.isUndef() ? Src : SDValue();
  }
  case Intrinsic::amdgcn_frexp_exp: {
    // frexp_exp (fneg x) -> frexp_exp x
    // frexp_exp (fabs x) -> frexp_exp x
    // frexp_exp (fneg (fabs x)) -> frexp_exp x
    SDValue Src = N->getOperand(1);
    SDValue PeekSign = peekFPSignOps(Src);
    if (PeekSign == Src)
      return SDValue();
    return SDValue(DCI.DAG.UpdateNodeOperands(N, N->getOperand(0), PeekSign),
                   0);
  }
  default:
    return SDValue();
  }
}

/// Split the 64-bit value \p LHS into two 32-bit components, and perform the
/// binary operation \p Opc to it with the corresponding constant operands.
SDValue AMDGPUTargetLowering::splitBinaryBitConstantOpImpl(
  DAGCombinerInfo &DCI, const SDLoc &SL,
  unsigned Opc, SDValue LHS,
  uint32_t ValLo, uint32_t ValHi) const {
  SelectionDAG &DAG = DCI.DAG;
  SDValue Lo, Hi;
  std::tie(Lo, Hi) = split64BitValue(LHS, DAG);

  SDValue LoRHS = DAG.getConstant(ValLo, SL, MVT::i32);
  SDValue HiRHS = DAG.getConstant(ValHi, SL, MVT::i32);

  SDValue LoAnd = DAG.getNode(Opc, SL, MVT::i32, Lo, LoRHS);
  SDValue HiAnd = DAG.getNode(Opc, SL, MVT::i32, Hi, HiRHS);

  // Re-visit the ands. It's possible we eliminated one of them and it could
  // simplify the vector.
  DCI.AddToWorklist(Lo.getNode());
  DCI.AddToWorklist(Hi.getNode());

  SDValue Vec = DAG.getBuildVector(MVT::v2i32, SL, {LoAnd, HiAnd});
  return DAG.getNode(ISD::BITCAST, SL, MVT::i64, Vec);
}

SDValue AMDGPUTargetLowering::performShlCombine(SDNode *N,
                                                DAGCombinerInfo &DCI) const {
  EVT VT = N->getValueType(0);

  ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!RHS)
    return SDValue();

  SDValue LHS = N->getOperand(0);
  unsigned RHSVal = RHS->getZExtValue();
  if (!RHSVal)
    return LHS;

  SDLoc SL(N);
  SelectionDAG &DAG = DCI.DAG;

  switch (LHS->getOpcode()) {
  default:
    break;
  case ISD::ZERO_EXTEND:
  case ISD::SIGN_EXTEND:
  case ISD::ANY_EXTEND: {
    SDValue X = LHS->getOperand(0);

    if (VT == MVT::i32 && RHSVal == 16 && X.getValueType() == MVT::i16 &&
        isOperationLegal(ISD::BUILD_VECTOR, MVT::v2i16)) {
      // Prefer build_vector as the canonical form if packed types are legal.
      // (shl ([asz]ext i16:x), 16 -> build_vector 0, x
      SDValue Vec = DAG.getBuildVector(MVT::v2i16, SL,
       { DAG.getConstant(0, SL, MVT::i16), LHS->getOperand(0) });
      return DAG.getNode(ISD::BITCAST, SL, MVT::i32, Vec);
    }

    // shl (ext x) => zext (shl x), if shift does not overflow int
    if (VT != MVT::i64)
      break;
    KnownBits Known = DAG.computeKnownBits(X);
    unsigned LZ = Known.countMinLeadingZeros();
    if (LZ < RHSVal)
      break;
    EVT XVT = X.getValueType();
    SDValue Shl = DAG.getNode(ISD::SHL, SL, XVT, X, SDValue(RHS, 0));
    return DAG.getZExtOrTrunc(Shl, SL, VT);
  }
  }

  if (VT != MVT::i64)
    return SDValue();

  // i64 (shl x, C) -> (build_pair 0, (shl x, C -32))

  // On some subtargets, 64-bit shift is a quarter rate instruction. In the
  // common case, splitting this into a move and a 32-bit shift is faster and
  // the same code size.
  if (RHSVal < 32)
    return SDValue();

  SDValue ShiftAmt = DAG.getConstant(RHSVal - 32, SL, MVT::i32);

  SDValue Lo = DAG.getNode(ISD::TRUNCATE, SL, MVT::i32, LHS);
  SDValue NewShift = DAG.getNode(ISD::SHL, SL, MVT::i32, Lo, ShiftAmt);

  const SDValue Zero = DAG.getConstant(0, SL, MVT::i32);

  SDValue Vec = DAG.getBuildVector(MVT::v2i32, SL, {Zero, NewShift});
  return DAG.getNode(ISD::BITCAST, SL, MVT::i64, Vec);
}

SDValue AMDGPUTargetLowering::performSraCombine(SDNode *N,
                                                DAGCombinerInfo &DCI) const {
  if (N->getValueType(0) != MVT::i64)
    return SDValue();

  const ConstantSDNode *RHS = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!RHS)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc SL(N);
  unsigned RHSVal = RHS->getZExtValue();

  // (sra i64:x, 32) -> build_pair x, (sra hi_32(x), 31)
  if (RHSVal == 32) {
    SDValue Hi = getHiHalf64(N->getOperand(0), DAG);
    SDValue NewShift = DAG.getNode(ISD::SRA, SL, MVT::i32, Hi,
                                   DAG.getConstant(31, SL, MVT::i32));

    SDValue BuildVec = DAG.getBuildVector(MVT::v2i32, SL, {Hi, NewShift});
    return DAG.getNode(ISD::BITCAST, SL, MVT::i64, BuildVec);
  }

  // (sra i64:x, 63) -> build_pair (sra hi_32(x), 31), (sra hi_32(x), 31)
  if (RHSVal == 63) {
    SDValue Hi = getHiHalf64(N->getOperand(0), DAG);
    SDValue NewShift = DAG.getNode(ISD::SRA, SL, MVT::i32, Hi,
                                   DAG.getConstant(31, SL, MVT::i32));
    SDValue BuildVec = DAG.getBuildVector(MVT::v2i32, SL, {NewShift, NewShift});
    return DAG.getNode(ISD::BITCAST, SL, MVT::i64, BuildVec);
  }

  return SDValue();
}

SDValue AMDGPUTargetLowering::performSrlCombine(SDNode *N,
                                                DAGCombinerInfo &DCI) const {
  auto *RHS = dyn_cast<ConstantSDNode>(N->getOperand(1));
  if (!RHS)
    return SDValue();

  EVT VT = N->getValueType(0);
  SDValue LHS = N->getOperand(0);
  unsigned ShiftAmt = RHS->getZExtValue();
  SelectionDAG &DAG = DCI.DAG;
  SDLoc SL(N);

  // fold (srl (and x, c1 << c2), c2) -> (and (srl(x, c2), c1)
  // this improves the ability to match BFE patterns in isel.
  if (LHS.getOpcode() == ISD::AND) {
    if (auto *Mask = dyn_cast<ConstantSDNode>(LHS.getOperand(1))) {
      unsigned MaskIdx, MaskLen;
      if (Mask->getAPIntValue().isShiftedMask(MaskIdx, MaskLen) &&
          MaskIdx == ShiftAmt) {
        return DAG.getNode(
            ISD::AND, SL, VT,
            DAG.getNode(ISD::SRL, SL, VT, LHS.getOperand(0), N->getOperand(1)),
            DAG.getNode(ISD::SRL, SL, VT, LHS.getOperand(1), N->getOperand(1)));
      }
    }
  }

  if (VT != MVT::i64)
    return SDValue();

  if (ShiftAmt < 32)
    return SDValue();

  // srl i64:x, C for C >= 32
  // =>
  //   build_pair (srl hi_32(x), C - 32), 0
  SDValue Zero = DAG.getConstant(0, SL, MVT::i32);

  SDValue Hi = getHiHalf64(LHS, DAG);

  SDValue NewConst = DAG.getConstant(ShiftAmt - 32, SL, MVT::i32);
  SDValue NewShift = DAG.getNode(ISD::SRL, SL, MVT::i32, Hi, NewConst);

  SDValue BuildPair = DAG.getBuildVector(MVT::v2i32, SL, {NewShift, Zero});

  return DAG.getNode(ISD::BITCAST, SL, MVT::i64, BuildPair);
}

SDValue AMDGPUTargetLowering::performTruncateCombine(
  SDNode *N, DAGCombinerInfo &DCI) const {
  SDLoc SL(N);
  SelectionDAG &DAG = DCI.DAG;
  EVT VT = N->getValueType(0);
  SDValue Src = N->getOperand(0);

  // vt1 (truncate (bitcast (build_vector vt0:x, ...))) -> vt1 (bitcast vt0:x)
  if (Src.getOpcode() == ISD::BITCAST && !VT.isVector()) {
    SDValue Vec = Src.getOperand(0);
    if (Vec.getOpcode() == ISD::BUILD_VECTOR) {
      SDValue Elt0 = Vec.getOperand(0);
      EVT EltVT = Elt0.getValueType();
      if (VT.getFixedSizeInBits() <= EltVT.getFixedSizeInBits()) {
        if (EltVT.isFloatingPoint()) {
          Elt0 = DAG.getNode(ISD::BITCAST, SL,
                             EltVT.changeTypeToInteger(), Elt0);
        }

        return DAG.getNode(ISD::TRUNCATE, SL, VT, Elt0);
      }
    }
  }

  // Equivalent of above for accessing the high element of a vector as an
  // integer operation.
  // trunc (srl (bitcast (build_vector x, y))), 16 -> trunc (bitcast y)
  if (Src.getOpcode() == ISD::SRL && !VT.isVector()) {
    if (auto K = isConstOrConstSplat(Src.getOperand(1))) {
      if (2 * K->getZExtValue() == Src.getValueType().getScalarSizeInBits()) {
        SDValue BV = stripBitcast(Src.getOperand(0));
        if (BV.getOpcode() == ISD::BUILD_VECTOR &&
            BV.getValueType().getVectorNumElements() == 2) {
          SDValue SrcElt = BV.getOperand(1);
          EVT SrcEltVT = SrcElt.getValueType();
          if (SrcEltVT.isFloatingPoint()) {
            SrcElt = DAG.getNode(ISD::BITCAST, SL,
                                 SrcEltVT.changeTypeToInteger(), SrcElt);
          }

          return DAG.getNode(ISD::TRUNCATE, SL, VT, SrcElt);
        }
      }
    }
  }

  // Partially shrink 64-bit shifts to 32-bit if reduced to 16-bit.
  //
  // i16 (trunc (srl i64:x, K)), K <= 16 ->
  //     i16 (trunc (srl (i32 (trunc x), K)))
  if (VT.getScalarSizeInBits() < 32) {
    EVT SrcVT = Src.getValueType();
    if (SrcVT.getScalarSizeInBits() > 32 &&
        (Src.getOpcode() == ISD::SRL ||
         Src.getOpcode() == ISD::SRA ||
         Src.getOpcode() == ISD::SHL)) {
      SDValue Amt = Src.getOperand(1);
      KnownBits Known = DAG.computeKnownBits(Amt);

      // - For left shifts, do the transform as long as the shift
      //   amount is still legal for i32, so when ShiftAmt < 32 (<= 31)
      // - For right shift, do it if ShiftAmt <= (32 - Size) to avoid
      //   losing information stored in the high bits when truncating.
      const unsigned MaxCstSize =
          (Src.getOpcode() == ISD::SHL) ? 31 : (32 - VT.getScalarSizeInBits());
      if (Known.getMaxValue().ule(MaxCstSize)) {
        EVT MidVT = VT.isVector() ?
          EVT::getVectorVT(*DAG.getContext(), MVT::i32,
                           VT.getVectorNumElements()) : MVT::i32;

        EVT NewShiftVT = getShiftAmountTy(MidVT, DAG.getDataLayout());
        SDValue Trunc = DAG.getNode(ISD::TRUNCATE, SL, MidVT,
                                    Src.getOperand(0));
        DCI.AddToWorklist(Trunc.getNode());

        if (Amt.getValueType() != NewShiftVT) {
          Amt = DAG.getZExtOrTrunc(Amt, SL, NewShiftVT);
          DCI.AddToWorklist(Amt.getNode());
        }

        SDValue ShrunkShift = DAG.getNode(Src.getOpcode(), SL, MidVT,
                                          Trunc, Amt);
        return DAG.getNode(ISD::TRUNCATE, SL, VT, ShrunkShift);
      }
    }
  }

  return SDValue();
}

// We need to specifically handle i64 mul here to avoid unnecessary conversion
// instructions. If we only match on the legalized i64 mul expansion,
// SimplifyDemandedBits will be unable to remove them because there will be
// multiple uses due to the separate mul + mulh[su].
static SDValue getMul24(SelectionDAG &DAG, const SDLoc &SL,
                        SDValue N0, SDValue N1, unsigned Size, bool Signed) {
  if (Size <= 32) {
    unsigned MulOpc = Signed ? AMDGPUISD::MUL_I24 : AMDGPUISD::MUL_U24;
    return DAG.getNode(MulOpc, SL, MVT::i32, N0, N1);
  }

  unsigned MulLoOpc = Signed ? AMDGPUISD::MUL_I24 : AMDGPUISD::MUL_U24;
  unsigned MulHiOpc = Signed ? AMDGPUISD::MULHI_I24 : AMDGPUISD::MULHI_U24;

  SDValue MulLo = DAG.getNode(MulLoOpc, SL, MVT::i32, N0, N1);
  SDValue MulHi = DAG.getNode(MulHiOpc, SL, MVT::i32, N0, N1);

  return DAG.getNode(ISD::BUILD_PAIR, SL, MVT::i64, MulLo, MulHi);
}

/// If \p V is an add of a constant 1, returns the other operand. Otherwise
/// return SDValue().
static SDValue getAddOneOp(const SDNode *V) {
  if (V->getOpcode() != ISD::ADD)
    return SDValue();

  return isOneConstant(V->getOperand(1)) ? V->getOperand(0) : SDValue();
}

SDValue AMDGPUTargetLowering::performMulCombine(SDNode *N,
                                                DAGCombinerInfo &DCI) const {
  assert(N->getOpcode() == ISD::MUL);
  EVT VT = N->getValueType(0);

  // Don't generate 24-bit multiplies on values that are in SGPRs, since
  // we only have a 32-bit scalar multiply (avoid values being moved to VGPRs
  // unnecessarily). isDivergent() is used as an approximation of whether the
  // value is in an SGPR.
  if (!N->isDivergent())
    return SDValue();

  unsigned Size = VT.getSizeInBits();
  if (VT.isVector() || Size > 64)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  // Undo InstCombine canonicalize X * (Y + 1) -> X * Y + X to enable mad
  // matching.

  // mul x, (add y, 1) -> add (mul x, y), x
  auto IsFoldableAdd = [](SDValue V) -> SDValue {
    SDValue AddOp = getAddOneOp(V.getNode());
    if (!AddOp)
      return SDValue();

    if (V.hasOneUse() || all_of(V->uses(), [](const SDNode *U) -> bool {
          return U->getOpcode() == ISD::MUL;
        }))
      return AddOp;

    return SDValue();
  };

  // FIXME: The selection pattern is not properly checking for commuted
  // operands, so we have to place the mul in the LHS
  if (SDValue MulOper = IsFoldableAdd(N0)) {
    SDValue MulVal = DAG.getNode(N->getOpcode(), DL, VT, N1, MulOper);
    return DAG.getNode(ISD::ADD, DL, VT, MulVal, N1);
  }

  if (SDValue MulOper = IsFoldableAdd(N1)) {
    SDValue MulVal = DAG.getNode(N->getOpcode(), DL, VT, N0, MulOper);
    return DAG.getNode(ISD::ADD, DL, VT, MulVal, N0);
  }

  // There are i16 integer mul/mad.
  if (Subtarget->has16BitInsts() && VT.getScalarType().bitsLE(MVT::i16))
    return SDValue();

  // SimplifyDemandedBits has the annoying habit of turning useful zero_extends
  // in the source into any_extends if the result of the mul is truncated. Since
  // we can assume the high bits are whatever we want, use the underlying value
  // to avoid the unknown high bits from interfering.
  if (N0.getOpcode() == ISD::ANY_EXTEND)
    N0 = N0.getOperand(0);

  if (N1.getOpcode() == ISD::ANY_EXTEND)
    N1 = N1.getOperand(0);

  SDValue Mul;

  if (Subtarget->hasMulU24() && isU24(N0, DAG) && isU24(N1, DAG)) {
    N0 = DAG.getZExtOrTrunc(N0, DL, MVT::i32);
    N1 = DAG.getZExtOrTrunc(N1, DL, MVT::i32);
    Mul = getMul24(DAG, DL, N0, N1, Size, false);
  } else if (Subtarget->hasMulI24() && isI24(N0, DAG) && isI24(N1, DAG)) {
    N0 = DAG.getSExtOrTrunc(N0, DL, MVT::i32);
    N1 = DAG.getSExtOrTrunc(N1, DL, MVT::i32);
    Mul = getMul24(DAG, DL, N0, N1, Size, true);
  } else {
    return SDValue();
  }

  // We need to use sext even for MUL_U24, because MUL_U24 is used
  // for signed multiply of 8 and 16-bit types.
  return DAG.getSExtOrTrunc(Mul, DL, VT);
}

SDValue
AMDGPUTargetLowering::performMulLoHiCombine(SDNode *N,
                                            DAGCombinerInfo &DCI) const {
  if (N->getValueType(0) != MVT::i32)
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  bool Signed = N->getOpcode() == ISD::SMUL_LOHI;
  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  // SimplifyDemandedBits has the annoying habit of turning useful zero_extends
  // in the source into any_extends if the result of the mul is truncated. Since
  // we can assume the high bits are whatever we want, use the underlying value
  // to avoid the unknown high bits from interfering.
  if (N0.getOpcode() == ISD::ANY_EXTEND)
    N0 = N0.getOperand(0);
  if (N1.getOpcode() == ISD::ANY_EXTEND)
    N1 = N1.getOperand(0);

  // Try to use two fast 24-bit multiplies (one for each half of the result)
  // instead of one slow extending multiply.
  unsigned LoOpcode = 0;
  unsigned HiOpcode = 0;
  if (Signed) {
    if (Subtarget->hasMulI24() && isI24(N0, DAG) && isI24(N1, DAG)) {
      N0 = DAG.getSExtOrTrunc(N0, DL, MVT::i32);
      N1 = DAG.getSExtOrTrunc(N1, DL, MVT::i32);
      LoOpcode = AMDGPUISD::MUL_I24;
      HiOpcode = AMDGPUISD::MULHI_I24;
    }
  } else {
    if (Subtarget->hasMulU24() && isU24(N0, DAG) && isU24(N1, DAG)) {
      N0 = DAG.getZExtOrTrunc(N0, DL, MVT::i32);
      N1 = DAG.getZExtOrTrunc(N1, DL, MVT::i32);
      LoOpcode = AMDGPUISD::MUL_U24;
      HiOpcode = AMDGPUISD::MULHI_U24;
    }
  }
  if (!LoOpcode)
    return SDValue();

  SDValue Lo = DAG.getNode(LoOpcode, DL, MVT::i32, N0, N1);
  SDValue Hi = DAG.getNode(HiOpcode, DL, MVT::i32, N0, N1);
  DCI.CombineTo(N, Lo, Hi);
  return SDValue(N, 0);
}

SDValue AMDGPUTargetLowering::performMulhsCombine(SDNode *N,
                                                  DAGCombinerInfo &DCI) const {
  EVT VT = N->getValueType(0);

  if (!Subtarget->hasMulI24() || VT.isVector())
    return SDValue();

  // Don't generate 24-bit multiplies on values that are in SGPRs, since
  // we only have a 32-bit scalar multiply (avoid values being moved to VGPRs
  // unnecessarily). isDivergent() is used as an approximation of whether the
  // value is in an SGPR.
  // This doesn't apply if no s_mul_hi is available (since we'll end up with a
  // valu op anyway)
  if (Subtarget->hasSMulHi() && !N->isDivergent())
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  if (!isI24(N0, DAG) || !isI24(N1, DAG))
    return SDValue();

  N0 = DAG.getSExtOrTrunc(N0, DL, MVT::i32);
  N1 = DAG.getSExtOrTrunc(N1, DL, MVT::i32);

  SDValue Mulhi = DAG.getNode(AMDGPUISD::MULHI_I24, DL, MVT::i32, N0, N1);
  DCI.AddToWorklist(Mulhi.getNode());
  return DAG.getSExtOrTrunc(Mulhi, DL, VT);
}

SDValue AMDGPUTargetLowering::performMulhuCombine(SDNode *N,
                                                  DAGCombinerInfo &DCI) const {
  EVT VT = N->getValueType(0);

  if (!Subtarget->hasMulU24() || VT.isVector() || VT.getSizeInBits() > 32)
    return SDValue();

  // Don't generate 24-bit multiplies on values that are in SGPRs, since
  // we only have a 32-bit scalar multiply (avoid values being moved to VGPRs
  // unnecessarily). isDivergent() is used as an approximation of whether the
  // value is in an SGPR.
  // This doesn't apply if no s_mul_hi is available (since we'll end up with a
  // valu op anyway)
  if (Subtarget->hasSMulHi() && !N->isDivergent())
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  SDValue N0 = N->getOperand(0);
  SDValue N1 = N->getOperand(1);

  if (!isU24(N0, DAG) || !isU24(N1, DAG))
    return SDValue();

  N0 = DAG.getZExtOrTrunc(N0, DL, MVT::i32);
  N1 = DAG.getZExtOrTrunc(N1, DL, MVT::i32);

  SDValue Mulhi = DAG.getNode(AMDGPUISD::MULHI_U24, DL, MVT::i32, N0, N1);
  DCI.AddToWorklist(Mulhi.getNode());
  return DAG.getZExtOrTrunc(Mulhi, DL, VT);
}

SDValue AMDGPUTargetLowering::getFFBX_U32(SelectionDAG &DAG,
                                          SDValue Op,
                                          const SDLoc &DL,
                                          unsigned Opc) const {
  EVT VT = Op.getValueType();
  EVT LegalVT = getTypeToTransformTo(*DAG.getContext(), VT);
  if (LegalVT != MVT::i32 && (Subtarget->has16BitInsts() &&
                              LegalVT != MVT::i16))
    return SDValue();

  if (VT != MVT::i32)
    Op = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i32, Op);

  SDValue FFBX = DAG.getNode(Opc, DL, MVT::i32, Op);
  if (VT != MVT::i32)
    FFBX = DAG.getNode(ISD::TRUNCATE, DL, VT, FFBX);

  return FFBX;
}

// The native instructions return -1 on 0 input. Optimize out a select that
// produces -1 on 0.
//
// TODO: If zero is not undef, we could also do this if the output is compared
// against the bitwidth.
//
// TODO: Should probably combine against FFBH_U32 instead of ctlz directly.
SDValue AMDGPUTargetLowering::performCtlz_CttzCombine(const SDLoc &SL, SDValue Cond,
                                                 SDValue LHS, SDValue RHS,
                                                 DAGCombinerInfo &DCI) const {
  if (!isNullConstant(Cond.getOperand(1)))
    return SDValue();

  SelectionDAG &DAG = DCI.DAG;
  ISD::CondCode CCOpcode = cast<CondCodeSDNode>(Cond.getOperand(2))->get();
  SDValue CmpLHS = Cond.getOperand(0);

  // select (setcc x, 0, eq), -1, (ctlz_zero_undef x) -> ffbh_u32 x
  // select (setcc x, 0, eq), -1, (cttz_zero_undef x) -> ffbl_u32 x
  if (CCOpcode == ISD::SETEQ &&
      (isCtlzOpc(RHS.getOpcode()) || isCttzOpc(RHS.getOpcode())) &&
      RHS.getOperand(0) == CmpLHS && isAllOnesConstant(LHS)) {
    unsigned Opc =
        isCttzOpc(RHS.getOpcode()) ? AMDGPUISD::FFBL_B32 : AMDGPUISD::FFBH_U32;
    return getFFBX_U32(DAG, CmpLHS, SL, Opc);
  }

  // select (setcc x, 0, ne), (ctlz_zero_undef x), -1 -> ffbh_u32 x
  // select (setcc x, 0, ne), (cttz_zero_undef x), -1 -> ffbl_u32 x
  if (CCOpcode == ISD::SETNE &&
      (isCtlzOpc(LHS.getOpcode()) || isCttzOpc(LHS.getOpcode())) &&
      LHS.getOperand(0) == CmpLHS && isAllOnesConstant(RHS)) {
    unsigned Opc =
        isCttzOpc(LHS.getOpcode()) ? AMDGPUISD::FFBL_B32 : AMDGPUISD::FFBH_U32;

    return getFFBX_U32(DAG, CmpLHS, SL, Opc);
  }

  return SDValue();
}

static SDValue distributeOpThroughSelect(TargetLowering::DAGCombinerInfo &DCI,
                                         unsigned Op,
                                         const SDLoc &SL,
                                         SDValue Cond,
                                         SDValue N1,
                                         SDValue N2) {
  SelectionDAG &DAG = DCI.DAG;
  EVT VT = N1.getValueType();

  SDValue NewSelect = DAG.getNode(ISD::SELECT, SL, VT, Cond,
                                  N1.getOperand(0), N2.getOperand(0));
  DCI.AddToWorklist(NewSelect.getNode());
  return DAG.getNode(Op, SL, VT, NewSelect);
}

// Pull a free FP operation out of a select so it may fold into uses.
//
// select c, (fneg x), (fneg y) -> fneg (select c, x, y)
// select c, (fneg x), k -> fneg (select c, x, (fneg k))
//
// select c, (fabs x), (fabs y) -> fabs (select c, x, y)
// select c, (fabs x), +k -> fabs (select c, x, k)
SDValue
AMDGPUTargetLowering::foldFreeOpFromSelect(TargetLowering::DAGCombinerInfo &DCI,
                                           SDValue N) const {
  SelectionDAG &DAG = DCI.DAG;
  SDValue Cond = N.getOperand(0);
  SDValue LHS = N.getOperand(1);
  SDValue RHS = N.getOperand(2);

  EVT VT = N.getValueType();
  if ((LHS.getOpcode() == ISD::FABS && RHS.getOpcode() == ISD::FABS) ||
      (LHS.getOpcode() == ISD::FNEG && RHS.getOpcode() == ISD::FNEG)) {
    if (!AMDGPUTargetLowering::allUsesHaveSourceMods(N.getNode()))
      return SDValue();

    return distributeOpThroughSelect(DCI, LHS.getOpcode(),
                                     SDLoc(N), Cond, LHS, RHS);
  }

  bool Inv = false;
  if (RHS.getOpcode() == ISD::FABS || RHS.getOpcode() == ISD::FNEG) {
    std::swap(LHS, RHS);
    Inv = true;
  }

  // TODO: Support vector constants.
  ConstantFPSDNode *CRHS = dyn_cast<ConstantFPSDNode>(RHS);
  if ((LHS.getOpcode() == ISD::FNEG || LHS.getOpcode() == ISD::FABS) && CRHS &&
      !selectSupportsSourceMods(N.getNode())) {
    SDLoc SL(N);
    // If one side is an fneg/fabs and the other is a constant, we can push the
    // fneg/fabs down. If it's an fabs, the constant needs to be non-negative.
    SDValue NewLHS = LHS.getOperand(0);
    SDValue NewRHS = RHS;

    // Careful: if the neg can be folded up, don't try to pull it back down.
    bool ShouldFoldNeg = true;

    if (NewLHS.hasOneUse()) {
      unsigned Opc = NewLHS.getOpcode();
      if (LHS.getOpcode() == ISD::FNEG && fnegFoldsIntoOp(NewLHS.getNode()))
        ShouldFoldNeg = false;
      if (LHS.getOpcode() == ISD::FABS && Opc == ISD::FMUL)
        ShouldFoldNeg = false;
    }

    if (ShouldFoldNeg) {
      if (LHS.getOpcode() == ISD::FABS && CRHS->isNegative())
        return SDValue();

      // We're going to be forced to use a source modifier anyway, there's no
      // point to pulling the negate out unless we can get a size reduction by
      // negating the constant.
      //
      // TODO: Generalize to use getCheaperNegatedExpression which doesn't know
      // about cheaper constants.
      if (NewLHS.getOpcode() == ISD::FABS &&
          getConstantNegateCost(CRHS) != NegatibleCost::Cheaper)
        return SDValue();

      if (!AMDGPUTargetLowering::allUsesHaveSourceMods(N.getNode()))
        return SDValue();

      if (LHS.getOpcode() == ISD::FNEG)
        NewRHS = DAG.getNode(ISD::FNEG, SL, VT, RHS);

      if (Inv)
        std::swap(NewLHS, NewRHS);

      SDValue NewSelect = DAG.getNode(ISD::SELECT, SL, VT,
                                      Cond, NewLHS, NewRHS);
      DCI.AddToWorklist(NewSelect.getNode());
      return DAG.getNode(LHS.getOpcode(), SL, VT, NewSelect);
    }
  }

  return SDValue();
}

SDValue AMDGPUTargetLowering::performSelectCombine(SDNode *N,
                                                   DAGCombinerInfo &DCI) const {
  if (SDValue Folded = foldFreeOpFromSelect(DCI, SDValue(N, 0)))
    return Folded;

  SDValue Cond = N->getOperand(0);
  if (Cond.getOpcode() != ISD::SETCC)
    return SDValue();

  EVT VT = N->getValueType(0);
  SDValue LHS = Cond.getOperand(0);
  SDValue RHS = Cond.getOperand(1);
  SDValue CC = Cond.getOperand(2);

  SDValue True = N->getOperand(1);
  SDValue False = N->getOperand(2);

  if (Cond.hasOneUse()) { // TODO: Look for multiple select uses.
    SelectionDAG &DAG = DCI.DAG;
    if (DAG.isConstantValueOfAnyType(True) &&
        !DAG.isConstantValueOfAnyType(False)) {
      // Swap cmp + select pair to move constant to false input.
      // This will allow using VOPC cndmasks more often.
      // select (setcc x, y), k, x -> select (setccinv x, y), x, k

      SDLoc SL(N);
      ISD::CondCode NewCC =
          getSetCCInverse(cast<CondCodeSDNode>(CC)->get(), LHS.getValueType());

      SDValue NewCond = DAG.getSetCC(SL, Cond.getValueType(), LHS, RHS, NewCC);
      return DAG.getNode(ISD::SELECT, SL, VT, NewCond, False, True);
    }

    if (VT == MVT::f32 && Subtarget->hasFminFmaxLegacy()) {
      SDValue MinMax
        = combineFMinMaxLegacy(SDLoc(N), VT, LHS, RHS, True, False, CC, DCI);
      // Revisit this node so we can catch min3/max3/med3 patterns.
      //DCI.AddToWorklist(MinMax.getNode());
      return MinMax;
    }
  }

  // There's no reason to not do this if the condition has other uses.
  return performCtlz_CttzCombine(SDLoc(N), Cond, True, False, DCI);
}

static bool isInv2Pi(const APFloat &APF) {
  static const APFloat KF16(APFloat::IEEEhalf(), APInt(16, 0x3118));
  static const APFloat KF32(APFloat::IEEEsingle(), APInt(32, 0x3e22f983));
  static const APFloat KF64(APFloat::IEEEdouble(), APInt(64, 0x3fc45f306dc9c882));

  return APF.bitwiseIsEqual(KF16) ||
         APF.bitwiseIsEqual(KF32) ||
         APF.bitwiseIsEqual(KF64);
}

// 0 and 1.0 / (0.5 * pi) do not have inline immmediates, so there is an
// additional cost to negate them.
TargetLowering::NegatibleCost
AMDGPUTargetLowering::getConstantNegateCost(const ConstantFPSDNode *C) const {
  if (C->isZero())
    return C->isNegative() ? NegatibleCost::Cheaper : NegatibleCost::Expensive;

  if (Subtarget->hasInv2PiInlineImm() && isInv2Pi(C->getValueAPF()))
    return C->isNegative() ? NegatibleCost::Cheaper : NegatibleCost::Expensive;

  return NegatibleCost::Neutral;
}

bool AMDGPUTargetLowering::isConstantCostlierToNegate(SDValue N) const {
  if (const ConstantFPSDNode *C = isConstOrConstSplatFP(N))
    return getConstantNegateCost(C) == NegatibleCost::Expensive;
  return false;
}

bool AMDGPUTargetLowering::isConstantCheaperToNegate(SDValue N) const {
  if (const ConstantFPSDNode *C = isConstOrConstSplatFP(N))
    return getConstantNegateCost(C) == NegatibleCost::Cheaper;
  return false;
}

static unsigned inverseMinMax(unsigned Opc) {
  switch (Opc) {
  case ISD::FMAXNUM:
    return ISD::FMINNUM;
  case ISD::FMINNUM:
    return ISD::FMAXNUM;
  case ISD::FMAXNUM_IEEE:
    return ISD::FMINNUM_IEEE;
  case ISD::FMINNUM_IEEE:
    return ISD::FMAXNUM_IEEE;
  case ISD::FMAXIMUM:
    return ISD::FMINIMUM;
  case ISD::FMINIMUM:
    return ISD::FMAXIMUM;
  case AMDGPUISD::FMAX_LEGACY:
    return AMDGPUISD::FMIN_LEGACY;
  case AMDGPUISD::FMIN_LEGACY:
    return  AMDGPUISD::FMAX_LEGACY;
  default:
    llvm_unreachable("invalid min/max opcode");
  }
}

/// \return true if it's profitable to try to push an fneg into its source
/// instruction.
bool AMDGPUTargetLowering::shouldFoldFNegIntoSrc(SDNode *N, SDValue N0) {
  // If the input has multiple uses and we can either fold the negate down, or
  // the other uses cannot, give up. This both prevents unprofitable
  // transformations and infinite loops: we won't repeatedly try to fold around
  // a negate that has no 'good' form.
  if (N0.hasOneUse()) {
    // This may be able to fold into the source, but at a code size cost. Don't
    // fold if the fold into the user is free.
    if (allUsesHaveSourceMods(N, 0))
      return false;
  } else {
    if (fnegFoldsIntoOp(N0.getNode()) &&
        (allUsesHaveSourceMods(N) || !allUsesHaveSourceMods(N0.getNode())))
      return false;
  }

  return true;
}

SDValue AMDGPUTargetLowering::performFNegCombine(SDNode *N,
                                                 DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDValue N0 = N->getOperand(0);
  EVT VT = N->getValueType(0);

  unsigned Opc = N0.getOpcode();

  if (!shouldFoldFNegIntoSrc(N, N0))
    return SDValue();

  SDLoc SL(N);
  switch (Opc) {
  case ISD::FADD: {
    if (!mayIgnoreSignedZero(N0))
      return SDValue();

    // (fneg (fadd x, y)) -> (fadd (fneg x), (fneg y))
    SDValue LHS = N0.getOperand(0);
    SDValue RHS = N0.getOperand(1);

    if (LHS.getOpcode() != ISD::FNEG)
      LHS = DAG.getNode(ISD::FNEG, SL, VT, LHS);
    else
      LHS = LHS.getOperand(0);

    if (RHS.getOpcode() != ISD::FNEG)
      RHS = DAG.getNode(ISD::FNEG, SL, VT, RHS);
    else
      RHS = RHS.getOperand(0);

    SDValue Res = DAG.getNode(ISD::FADD, SL, VT, LHS, RHS, N0->getFlags());
    if (Res.getOpcode() != ISD::FADD)
      return SDValue(); // Op got folded away.
    if (!N0.hasOneUse())
      DAG.ReplaceAllUsesWith(N0, DAG.getNode(ISD::FNEG, SL, VT, Res));
    return Res;
  }
  case ISD::FMUL:
  case AMDGPUISD::FMUL_LEGACY: {
    // (fneg (fmul x, y)) -> (fmul x, (fneg y))
    // (fneg (fmul_legacy x, y)) -> (fmul_legacy x, (fneg y))
    SDValue LHS = N0.getOperand(0);
    SDValue RHS = N0.getOperand(1);

    if (LHS.getOpcode() == ISD::FNEG)
      LHS = LHS.getOperand(0);
    else if (RHS.getOpcode() == ISD::FNEG)
      RHS = RHS.getOperand(0);
    else
      RHS = DAG.getNode(ISD::FNEG, SL, VT, RHS);

    SDValue Res = DAG.getNode(Opc, SL, VT, LHS, RHS, N0->getFlags());
    if (Res.getOpcode() != Opc)
      return SDValue(); // Op got folded away.
    if (!N0.hasOneUse())
      DAG.ReplaceAllUsesWith(N0, DAG.getNode(ISD::FNEG, SL, VT, Res));
    return Res;
  }
  case ISD::FMA:
  case ISD::FMAD: {
    // TODO: handle llvm.amdgcn.fma.legacy
    if (!mayIgnoreSignedZero(N0))
      return SDValue();

    // (fneg (fma x, y, z)) -> (fma x, (fneg y), (fneg z))
    SDValue LHS = N0.getOperand(0);
    SDValue MHS = N0.getOperand(1);
    SDValue RHS = N0.getOperand(2);

    if (LHS.getOpcode() == ISD::FNEG)
      LHS = LHS.getOperand(0);
    else if (MHS.getOpcode() == ISD::FNEG)
      MHS = MHS.getOperand(0);
    else
      MHS = DAG.getNode(ISD::FNEG, SL, VT, MHS);

    if (RHS.getOpcode() != ISD::FNEG)
      RHS = DAG.getNode(ISD::FNEG, SL, VT, RHS);
    else
      RHS = RHS.getOperand(0);

    SDValue Res = DAG.getNode(Opc, SL, VT, LHS, MHS, RHS);
    if (Res.getOpcode() != Opc)
      return SDValue(); // Op got folded away.
    if (!N0.hasOneUse())
      DAG.ReplaceAllUsesWith(N0, DAG.getNode(ISD::FNEG, SL, VT, Res));
    return Res;
  }
  case ISD::FMAXNUM:
  case ISD::FMINNUM:
  case ISD::FMAXNUM_IEEE:
  case ISD::FMINNUM_IEEE:
  case ISD::FMINIMUM:
  case ISD::FMAXIMUM:
  case AMDGPUISD::FMAX_LEGACY:
  case AMDGPUISD::FMIN_LEGACY: {
    // fneg (fmaxnum x, y) -> fminnum (fneg x), (fneg y)
    // fneg (fminnum x, y) -> fmaxnum (fneg x), (fneg y)
    // fneg (fmax_legacy x, y) -> fmin_legacy (fneg x), (fneg y)
    // fneg (fmin_legacy x, y) -> fmax_legacy (fneg x), (fneg y)

    SDValue LHS = N0.getOperand(0);
    SDValue RHS = N0.getOperand(1);

    // 0 doesn't have a negated inline immediate.
    // TODO: This constant check should be generalized to other operations.
    if (isConstantCostlierToNegate(RHS))
      return SDValue();

    SDValue NegLHS = DAG.getNode(ISD::FNEG, SL, VT, LHS);
    SDValue NegRHS = DAG.getNode(ISD::FNEG, SL, VT, RHS);
    unsigned Opposite = inverseMinMax(Opc);

    SDValue Res = DAG.getNode(Opposite, SL, VT, NegLHS, NegRHS, N0->getFlags());
    if (Res.getOpcode() != Opposite)
      return SDValue(); // Op got folded away.
    if (!N0.hasOneUse())
      DAG.ReplaceAllUsesWith(N0, DAG.getNode(ISD::FNEG, SL, VT, Res));
    return Res;
  }
  case AMDGPUISD::FMED3: {
    SDValue Ops[3];
    for (unsigned I = 0; I < 3; ++I)
      Ops[I] = DAG.getNode(ISD::FNEG, SL, VT, N0->getOperand(I), N0->getFlags());

    SDValue Res = DAG.getNode(AMDGPUISD::FMED3, SL, VT, Ops, N0->getFlags());
    if (Res.getOpcode() != AMDGPUISD::FMED3)
      return SDValue(); // Op got folded away.

    if (!N0.hasOneUse()) {
      SDValue Neg = DAG.getNode(ISD::FNEG, SL, VT, Res);
      DAG.ReplaceAllUsesWith(N0, Neg);

      for (SDNode *U : Neg->uses())
        DCI.AddToWorklist(U);
    }

    return Res;
  }
  case ISD::FP_EXTEND:
  case ISD::FTRUNC:
  case ISD::FRINT:
  case ISD::FNEARBYINT: // XXX - Should fround be handled?
  case ISD::FROUNDEVEN:
  case ISD::FSIN:
  case ISD::FCANONICALIZE:
  case AMDGPUISD::RCP:
  case AMDGPUISD::RCP_LEGACY:
  case AMDGPUISD::RCP_IFLAG:
  case AMDGPUISD::SIN_HW: {
    SDValue CvtSrc = N0.getOperand(0);
    if (CvtSrc.getOpcode() == ISD::FNEG) {
      // (fneg (fp_extend (fneg x))) -> (fp_extend x)
      // (fneg (rcp (fneg x))) -> (rcp x)
      return DAG.getNode(Opc, SL, VT, CvtSrc.getOperand(0));
    }

    if (!N0.hasOneUse())
      return SDValue();

    // (fneg (fp_extend x)) -> (fp_extend (fneg x))
    // (fneg (rcp x)) -> (rcp (fneg x))
    SDValue Neg = DAG.getNode(ISD::FNEG, SL, CvtSrc.getValueType(), CvtSrc);
    return DAG.getNode(Opc, SL, VT, Neg, N0->getFlags());
  }
  case ISD::FP_ROUND: {
    SDValue CvtSrc = N0.getOperand(0);

    if (CvtSrc.getOpcode() == ISD::FNEG) {
      // (fneg (fp_round (fneg x))) -> (fp_round x)
      return DAG.getNode(ISD::FP_ROUND, SL, VT,
                         CvtSrc.getOperand(0), N0.getOperand(1));
    }

    if (!N0.hasOneUse())
      return SDValue();

    // (fneg (fp_round x)) -> (fp_round (fneg x))
    SDValue Neg = DAG.getNode(ISD::FNEG, SL, CvtSrc.getValueType(), CvtSrc);
    return DAG.getNode(ISD::FP_ROUND, SL, VT, Neg, N0.getOperand(1));
  }
  case ISD::FP16_TO_FP: {
    // v_cvt_f32_f16 supports source modifiers on pre-VI targets without legal
    // f16, but legalization of f16 fneg ends up pulling it out of the source.
    // Put the fneg back as a legal source operation that can be matched later.
    SDLoc SL(N);

    SDValue Src = N0.getOperand(0);
    EVT SrcVT = Src.getValueType();

    // fneg (fp16_to_fp x) -> fp16_to_fp (xor x, 0x8000)
    SDValue IntFNeg = DAG.getNode(ISD::XOR, SL, SrcVT, Src,
                                  DAG.getConstant(0x8000, SL, SrcVT));
    return DAG.getNode(ISD::FP16_TO_FP, SL, N->getValueType(0), IntFNeg);
  }
  case ISD::SELECT: {
    // fneg (select c, a, b) -> select c, (fneg a), (fneg b)
    // TODO: Invert conditions of foldFreeOpFromSelect
    return SDValue();
  }
  case ISD::BITCAST: {
    SDLoc SL(N);
    SDValue BCSrc = N0.getOperand(0);
    if (BCSrc.getOpcode() == ISD::BUILD_VECTOR) {
      SDValue HighBits = BCSrc.getOperand(BCSrc.getNumOperands() - 1);
      if (HighBits.getValueType().getSizeInBits() != 32 ||
          !fnegFoldsIntoOp(HighBits.getNode()))
        return SDValue();

      // f64 fneg only really needs to operate on the high half of of the
      // register, so try to force it to an f32 operation to help make use of
      // source modifiers.
      //
      //
      // fneg (f64 (bitcast (build_vector x, y))) ->
      // f64 (bitcast (build_vector (bitcast i32:x to f32),
      //                            (fneg (bitcast i32:y to f32)))

      SDValue CastHi = DAG.getNode(ISD::BITCAST, SL, MVT::f32, HighBits);
      SDValue NegHi = DAG.getNode(ISD::FNEG, SL, MVT::f32, CastHi);
      SDValue CastBack =
          DAG.getNode(ISD::BITCAST, SL, HighBits.getValueType(), NegHi);

      SmallVector<SDValue, 8> Ops(BCSrc->op_begin(), BCSrc->op_end());
      Ops.back() = CastBack;
      DCI.AddToWorklist(NegHi.getNode());
      SDValue Build =
          DAG.getNode(ISD::BUILD_VECTOR, SL, BCSrc.getValueType(), Ops);
      SDValue Result = DAG.getNode(ISD::BITCAST, SL, VT, Build);

      if (!N0.hasOneUse())
        DAG.ReplaceAllUsesWith(N0, DAG.getNode(ISD::FNEG, SL, VT, Result));
      return Result;
    }

    if (BCSrc.getOpcode() == ISD::SELECT && VT == MVT::f32 &&
        BCSrc.hasOneUse()) {
      // fneg (bitcast (f32 (select cond, i32:lhs, i32:rhs))) ->
      //   select cond, (bitcast i32:lhs to f32), (bitcast i32:rhs to f32)

      // TODO: Cast back result for multiple uses is beneficial in some cases.

      SDValue LHS =
          DAG.getNode(ISD::BITCAST, SL, MVT::f32, BCSrc.getOperand(1));
      SDValue RHS =
          DAG.getNode(ISD::BITCAST, SL, MVT::f32, BCSrc.getOperand(2));

      SDValue NegLHS = DAG.getNode(ISD::FNEG, SL, MVT::f32, LHS);
      SDValue NegRHS = DAG.getNode(ISD::FNEG, SL, MVT::f32, RHS);

      return DAG.getNode(ISD::SELECT, SL, MVT::f32, BCSrc.getOperand(0), NegLHS,
                         NegRHS);
    }

    return SDValue();
  }
  default:
    return SDValue();
  }
}

SDValue AMDGPUTargetLowering::performFAbsCombine(SDNode *N,
                                                 DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDValue N0 = N->getOperand(0);

  if (!N0.hasOneUse())
    return SDValue();

  switch (N0.getOpcode()) {
  case ISD::FP16_TO_FP: {
    assert(!Subtarget->has16BitInsts() && "should only see if f16 is illegal");
    SDLoc SL(N);
    SDValue Src = N0.getOperand(0);
    EVT SrcVT = Src.getValueType();

    // fabs (fp16_to_fp x) -> fp16_to_fp (and x, 0x7fff)
    SDValue IntFAbs = DAG.getNode(ISD::AND, SL, SrcVT, Src,
                                  DAG.getConstant(0x7fff, SL, SrcVT));
    return DAG.getNode(ISD::FP16_TO_FP, SL, N->getValueType(0), IntFAbs);
  }
  default:
    return SDValue();
  }
}

SDValue AMDGPUTargetLowering::performRcpCombine(SDNode *N,
                                                DAGCombinerInfo &DCI) const {
  const auto *CFP = dyn_cast<ConstantFPSDNode>(N->getOperand(0));
  if (!CFP)
    return SDValue();

  // XXX - Should this flush denormals?
  const APFloat &Val = CFP->getValueAPF();
  APFloat One(Val.getSemantics(), "1.0");
  return DCI.DAG.getConstantFP(One / Val, SDLoc(N), N->getValueType(0));
}

SDValue AMDGPUTargetLowering::PerformDAGCombine(SDNode *N,
                                                DAGCombinerInfo &DCI) const {
  SelectionDAG &DAG = DCI.DAG;
  SDLoc DL(N);

  switch(N->getOpcode()) {
  default:
    break;
  case ISD::BITCAST: {
    EVT DestVT = N->getValueType(0);

    // Push casts through vector builds. This helps avoid emitting a large
    // number of copies when materializing floating point vector constants.
    //
    // vNt1 bitcast (vNt0 (build_vector t0:x, t0:y)) =>
    //   vnt1 = build_vector (t1 (bitcast t0:x)), (t1 (bitcast t0:y))
    if (DestVT.isVector()) {
      SDValue Src = N->getOperand(0);
      if (Src.getOpcode() == ISD::BUILD_VECTOR &&
          (DCI.getDAGCombineLevel() < AfterLegalizeDAG ||
           isOperationLegal(ISD::BUILD_VECTOR, DestVT))) {
        EVT SrcVT = Src.getValueType();
        unsigned NElts = DestVT.getVectorNumElements();

        if (SrcVT.getVectorNumElements() == NElts) {
          EVT DestEltVT = DestVT.getVectorElementType();

          SmallVector<SDValue, 8> CastedElts;
          SDLoc SL(N);
          for (unsigned I = 0, E = SrcVT.getVectorNumElements(); I != E; ++I) {
            SDValue Elt = Src.getOperand(I);
            CastedElts.push_back(DAG.getNode(ISD::BITCAST, DL, DestEltVT, Elt));
          }

          return DAG.getBuildVector(DestVT, SL, CastedElts);
        }
      }
    }

    if (DestVT.getSizeInBits() != 64 || !DestVT.isVector())
      break;

    // Fold bitcasts of constants.
    //
    // v2i32 (bitcast i64:k) -> build_vector lo_32(k), hi_32(k)
    // TODO: Generalize and move to DAGCombiner
    SDValue Src = N->getOperand(0);
    if (ConstantSDNode *C = dyn_cast<ConstantSDNode>(Src)) {
      SDLoc SL(N);
      uint64_t CVal = C->getZExtValue();
      SDValue BV = DAG.getNode(ISD::BUILD_VECTOR, SL, MVT::v2i32,
                               DAG.getConstant(Lo_32(CVal), SL, MVT::i32),
                               DAG.getConstant(Hi_32(CVal), SL, MVT::i32));
      return DAG.getNode(ISD::BITCAST, SL, DestVT, BV);
    }

    if (ConstantFPSDNode *C = dyn_cast<ConstantFPSDNode>(Src)) {
      const APInt &Val = C->getValueAPF().bitcastToAPInt();
      SDLoc SL(N);
      uint64_t CVal = Val.getZExtValue();
      SDValue Vec = DAG.getNode(ISD::BUILD_VECTOR, SL, MVT::v2i32,
                                DAG.getConstant(Lo_32(CVal), SL, MVT::i32),
                                DAG.getConstant(Hi_32(CVal), SL, MVT::i32));

      return DAG.getNode(ISD::BITCAST, SL, DestVT, Vec);
    }

    break;
  }
  case ISD::SHL: {
    if (DCI.getDAGCombineLevel() < AfterLegalizeDAG)
      break;

    return performShlCombine(N, DCI);
  }
  case ISD::SRL: {
    if (DCI.getDAGCombineLevel() < AfterLegalizeDAG)
      break;

    return performSrlCombine(N, DCI);
  }
  case ISD::SRA: {
    if (DCI.getDAGCombineLevel() < AfterLegalizeDAG)
      break;

    return performSraCombine(N, DCI);
  }
  case ISD::TRUNCATE:
    return performTruncateCombine(N, DCI);
  case ISD::MUL:
    return performMulCombine(N, DCI);
  case AMDGPUISD::MUL_U24:
  case AMDGPUISD::MUL_I24: {
    if (SDValue Simplified = simplifyMul24(N, DCI))
      return Simplified;
    break;
  }
  case AMDGPUISD::MULHI_I24:
  case AMDGPUISD::MULHI_U24:
    return simplifyMul24(N, DCI);
  case ISD::SMUL_LOHI:
  case ISD::UMUL_LOHI:
    return performMulLoHiCombine(N, DCI);
  case ISD::MULHS:
    return performMulhsCombine(N, DCI);
  case ISD::MULHU:
    return performMulhuCombine(N, DCI);
  case ISD::SELECT:
    return performSelectCombine(N, DCI);
  case ISD::FNEG:
    return performFNegCombine(N, DCI);
  case ISD::FABS:
    return performFAbsCombine(N, DCI);
  case AMDGPUISD::BFE_I32:
  case AMDGPUISD::BFE_U32: {
    assert(!N->getValueType(0).isVector() &&
           "Vector handling of BFE not implemented");
    ConstantSDNode *Width = dyn_cast<ConstantSDNode>(N->getOperand(2));
    if (!Width)
      break;

    uint32_t WidthVal = Width->getZExtValue() & 0x1f;
    if (WidthVal == 0)
      return DAG.getConstant(0, DL, MVT::i32);

    ConstantSDNode *Offset = dyn_cast<ConstantSDNode>(N->getOperand(1));
    if (!Offset)
      break;

    SDValue BitsFrom = N->getOperand(0);
    uint32_t OffsetVal = Offset->getZExtValue() & 0x1f;

    bool Signed = N->getOpcode() == AMDGPUISD::BFE_I32;

    if (OffsetVal == 0) {
      // This is already sign / zero extended, so try to fold away extra BFEs.
      unsigned SignBits =  Signed ? (32 - WidthVal + 1) : (32 - WidthVal);

      unsigned OpSignBits = DAG.ComputeNumSignBits(BitsFrom);
      if (OpSignBits >= SignBits)
        return BitsFrom;

      EVT SmallVT = EVT::getIntegerVT(*DAG.getContext(), WidthVal);
      if (Signed) {
        // This is a sign_extend_inreg. Replace it to take advantage of existing
        // DAG Combines. If not eliminated, we will match back to BFE during
        // selection.

        // TODO: The sext_inreg of extended types ends, although we can could
        // handle them in a single BFE.
        return DAG.getNode(ISD::SIGN_EXTEND_INREG, DL, MVT::i32, BitsFrom,
                           DAG.getValueType(SmallVT));
      }

      return DAG.getZeroExtendInReg(BitsFrom, DL, SmallVT);
    }

    if (ConstantSDNode *CVal = dyn_cast<ConstantSDNode>(BitsFrom)) {
      if (Signed) {
        return constantFoldBFE<int32_t>(DAG,
                                        CVal->getSExtValue(),
                                        OffsetVal,
                                        WidthVal,
                                        DL);
      }

      return constantFoldBFE<uint32_t>(DAG,
                                       CVal->getZExtValue(),
                                       OffsetVal,
                                       WidthVal,
                                       DL);
    }

    if ((OffsetVal + WidthVal) >= 32 &&
        !(Subtarget->hasSDWA() && OffsetVal == 16 && WidthVal == 16)) {
      SDValue ShiftVal = DAG.getConstant(OffsetVal, DL, MVT::i32);
      return DAG.getNode(Signed ? ISD::SRA : ISD::SRL, DL, MVT::i32,
                         BitsFrom, ShiftVal);
    }

    if (BitsFrom.hasOneUse()) {
      APInt Demanded = APInt::getBitsSet(32,
                                         OffsetVal,
                                         OffsetVal + WidthVal);

      KnownBits Known;
      TargetLowering::TargetLoweringOpt TLO(DAG, !DCI.isBeforeLegalize(),
                                            !DCI.isBeforeLegalizeOps());
      const TargetLowering &TLI = DAG.getTargetLoweringInfo();
      if (TLI.ShrinkDemandedConstant(BitsFrom, Demanded, TLO) ||
          TLI.SimplifyDemandedBits(BitsFrom, Demanded, Known, TLO)) {
        DCI.CommitTargetLoweringOpt(TLO);
      }
    }

    break;
  }
  case ISD::LOAD:
    return performLoadCombine(N, DCI);
  case ISD::STORE:
    return performStoreCombine(N, DCI);
  case AMDGPUISD::RCP:
  case AMDGPUISD::RCP_IFLAG:
    return performRcpCombine(N, DCI);
  case ISD::AssertZext:
  case ISD::AssertSext:
    return performAssertSZExtCombine(N, DCI);
  case ISD::INTRINSIC_WO_CHAIN:
    return performIntrinsicWOChainCombine(N, DCI);
  case AMDGPUISD::FMAD_FTZ: {
    SDValue N0 = N->getOperand(0);
    SDValue N1 = N->getOperand(1);
    SDValue N2 = N->getOperand(2);
    EVT VT = N->getValueType(0);

    // FMAD_FTZ is a FMAD + flush denormals to zero.
    // We flush the inputs, the intermediate step, and the output.
    ConstantFPSDNode *N0CFP = dyn_cast<ConstantFPSDNode>(N0);
    ConstantFPSDNode *N1CFP = dyn_cast<ConstantFPSDNode>(N1);
    ConstantFPSDNode *N2CFP = dyn_cast<ConstantFPSDNode>(N2);
    if (N0CFP && N1CFP && N2CFP) {
      const auto FTZ = [](const APFloat &V) {
        if (V.isDenormal()) {
          APFloat Zero(V.getSemantics(), 0);
          return V.isNegative() ? -Zero : Zero;
        }
        return V;
      };

      APFloat V0 = FTZ(N0CFP->getValueAPF());
      APFloat V1 = FTZ(N1CFP->getValueAPF());
      APFloat V2 = FTZ(N2CFP->getValueAPF());
      V0.multiply(V1, APFloat::rmNearestTiesToEven);
      V0 = FTZ(V0);
      V0.add(V2, APFloat::rmNearestTiesToEven);
      return DAG.getConstantFP(FTZ(V0), DL, VT);
    }
    break;
  }
  }
  return SDValue();
}

//===----------------------------------------------------------------------===//
// Helper functions
//===----------------------------------------------------------------------===//

SDValue AMDGPUTargetLowering::CreateLiveInRegister(SelectionDAG &DAG,
                                                   const TargetRegisterClass *RC,
                                                   Register Reg, EVT VT,
                                                   const SDLoc &SL,
                                                   bool RawReg) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  Register VReg;

  if (!MRI.isLiveIn(Reg)) {
    VReg = MRI.createVirtualRegister(RC);
    MRI.addLiveIn(Reg, VReg);
  } else {
    VReg = MRI.getLiveInVirtReg(Reg);
  }

  if (RawReg)
    return DAG.getRegister(VReg, VT);

  return DAG.getCopyFromReg(DAG.getEntryNode(), SL, VReg, VT);
}

// This may be called multiple times, and nothing prevents creating multiple
// objects at the same offset. See if we already defined this object.
static int getOrCreateFixedStackObject(MachineFrameInfo &MFI, unsigned Size,
                                       int64_t Offset) {
  for (int I = MFI.getObjectIndexBegin(); I < 0; ++I) {
    if (MFI.getObjectOffset(I) == Offset) {
      assert(MFI.getObjectSize(I) == Size);
      return I;
    }
  }

  return MFI.CreateFixedObject(Size, Offset, true);
}

SDValue AMDGPUTargetLowering::loadStackInputValue(SelectionDAG &DAG,
                                                  EVT VT,
                                                  const SDLoc &SL,
                                                  int64_t Offset) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  int FI = getOrCreateFixedStackObject(MFI, VT.getStoreSize(), Offset);

  auto SrcPtrInfo = MachinePointerInfo::getStack(MF, Offset);
  SDValue Ptr = DAG.getFrameIndex(FI, MVT::i32);

  return DAG.getLoad(VT, SL, DAG.getEntryNode(), Ptr, SrcPtrInfo, Align(4),
                     MachineMemOperand::MODereferenceable |
                         MachineMemOperand::MOInvariant);
}

SDValue AMDGPUTargetLowering::storeStackInputValue(SelectionDAG &DAG,
                                                   const SDLoc &SL,
                                                   SDValue Chain,
                                                   SDValue ArgVal,
                                                   int64_t Offset) const {
  MachineFunction &MF = DAG.getMachineFunction();
  MachinePointerInfo DstInfo = MachinePointerInfo::getStack(MF, Offset);
  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();

  SDValue Ptr = DAG.getConstant(Offset, SL, MVT::i32);
  // Stores to the argument stack area are relative to the stack pointer.
  SDValue SP =
      DAG.getCopyFromReg(Chain, SL, Info->getStackPtrOffsetReg(), MVT::i32);
  Ptr = DAG.getNode(ISD::ADD, SL, MVT::i32, SP, Ptr);
  SDValue Store = DAG.getStore(Chain, SL, ArgVal, Ptr, DstInfo, Align(4),
                               MachineMemOperand::MODereferenceable);
  return Store;
}

SDValue AMDGPUTargetLowering::loadInputValue(SelectionDAG &DAG,
                                             const TargetRegisterClass *RC,
                                             EVT VT, const SDLoc &SL,
                                             const ArgDescriptor &Arg) const {
  assert(Arg && "Attempting to load missing argument");

  SDValue V = Arg.isRegister() ?
    CreateLiveInRegister(DAG, RC, Arg.getRegister(), VT, SL) :
    loadStackInputValue(DAG, VT, SL, Arg.getStackOffset());

  if (!Arg.isMasked())
    return V;

  unsigned Mask = Arg.getMask();
  unsigned Shift = llvm::countr_zero<unsigned>(Mask);
  V = DAG.getNode(ISD::SRL, SL, VT, V,
                  DAG.getShiftAmountConstant(Shift, VT, SL));
  return DAG.getNode(ISD::AND, SL, VT, V,
                     DAG.getConstant(Mask >> Shift, SL, VT));
}

uint32_t AMDGPUTargetLowering::getImplicitParameterOffset(
    uint64_t ExplicitKernArgSize, const ImplicitParameter Param) const {
  unsigned ExplicitArgOffset = Subtarget->getExplicitKernelArgOffset();
  const Align Alignment = Subtarget->getAlignmentForImplicitArgPtr();
  uint64_t ArgOffset =
      alignTo(ExplicitKernArgSize, Alignment) + ExplicitArgOffset;
  switch (Param) {
  case FIRST_IMPLICIT:
    return ArgOffset;
  case PRIVATE_BASE:
    return ArgOffset + AMDGPU::ImplicitArg::PRIVATE_BASE_OFFSET;
  case SHARED_BASE:
    return ArgOffset + AMDGPU::ImplicitArg::SHARED_BASE_OFFSET;
  case QUEUE_PTR:
    return ArgOffset + AMDGPU::ImplicitArg::QUEUE_PTR_OFFSET;
  }
  llvm_unreachable("unexpected implicit parameter type");
}

uint32_t AMDGPUTargetLowering::getImplicitParameterOffset(
    const MachineFunction &MF, const ImplicitParameter Param) const {
  const AMDGPUMachineFunction *MFI = MF.getInfo<AMDGPUMachineFunction>();
  return getImplicitParameterOffset(MFI->getExplicitKernArgSize(), Param);
}

#define NODE_NAME_CASE(node) case AMDGPUISD::node: return #node;

const char* AMDGPUTargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch ((AMDGPUISD::NodeType)Opcode) {
  case AMDGPUISD::FIRST_NUMBER: break;
  // AMDIL DAG nodes
  NODE_NAME_CASE(UMUL);
  NODE_NAME_CASE(BRANCH_COND);

  // AMDGPU DAG nodes
  NODE_NAME_CASE(IF)
  NODE_NAME_CASE(ELSE)
  NODE_NAME_CASE(LOOP)
  NODE_NAME_CASE(CALL)
  NODE_NAME_CASE(TC_RETURN)
  NODE_NAME_CASE(TC_RETURN_GFX)
  NODE_NAME_CASE(TC_RETURN_CHAIN)
  NODE_NAME_CASE(TRAP)
  NODE_NAME_CASE(RET_GLUE)
  NODE_NAME_CASE(WAVE_ADDRESS)
  NODE_NAME_CASE(RETURN_TO_EPILOG)
  NODE_NAME_CASE(ENDPGM)
  NODE_NAME_CASE(ENDPGM_TRAP)
  NODE_NAME_CASE(SIMULATED_TRAP)
  NODE_NAME_CASE(DWORDADDR)
  NODE_NAME_CASE(FRACT)
  NODE_NAME_CASE(SETCC)
  NODE_NAME_CASE(SETREG)
  NODE_NAME_CASE(DENORM_MODE)
  NODE_NAME_CASE(FMA_W_CHAIN)
  NODE_NAME_CASE(FMUL_W_CHAIN)
  NODE_NAME_CASE(CLAMP)
  NODE_NAME_CASE(COS_HW)
  NODE_NAME_CASE(SIN_HW)
  NODE_NAME_CASE(FMAX_LEGACY)
  NODE_NAME_CASE(FMIN_LEGACY)
  NODE_NAME_CASE(FMAX3)
  NODE_NAME_CASE(SMAX3)
  NODE_NAME_CASE(UMAX3)
  NODE_NAME_CASE(FMIN3)
  NODE_NAME_CASE(SMIN3)
  NODE_NAME_CASE(UMIN3)
  NODE_NAME_CASE(FMED3)
  NODE_NAME_CASE(SMED3)
  NODE_NAME_CASE(UMED3)
  NODE_NAME_CASE(FMAXIMUM3)
  NODE_NAME_CASE(FMINIMUM3)
  NODE_NAME_CASE(FDOT2)
  NODE_NAME_CASE(URECIP)
  NODE_NAME_CASE(DIV_SCALE)
  NODE_NAME_CASE(DIV_FMAS)
  NODE_NAME_CASE(DIV_FIXUP)
  NODE_NAME_CASE(FMAD_FTZ)
  NODE_NAME_CASE(RCP)
  NODE_NAME_CASE(RSQ)
  NODE_NAME_CASE(RCP_LEGACY)
  NODE_NAME_CASE(RCP_IFLAG)
  NODE_NAME_CASE(LOG)
  NODE_NAME_CASE(EXP)
  NODE_NAME_CASE(FMUL_LEGACY)
  NODE_NAME_CASE(RSQ_CLAMP)
  NODE_NAME_CASE(FP_CLASS)
  NODE_NAME_CASE(DOT4)
  NODE_NAME_CASE(CARRY)
  NODE_NAME_CASE(BORROW)
  NODE_NAME_CASE(BFE_U32)
  NODE_NAME_CASE(BFE_I32)
  NODE_NAME_CASE(BFI)
  NODE_NAME_CASE(BFM)
  NODE_NAME_CASE(FFBH_U32)
  NODE_NAME_CASE(FFBH_I32)
  NODE_NAME_CASE(FFBL_B32)
  NODE_NAME_CASE(MUL_U24)
  NODE_NAME_CASE(MUL_I24)
  NODE_NAME_CASE(MULHI_U24)
  NODE_NAME_CASE(MULHI_I24)
  NODE_NAME_CASE(MAD_U24)
  NODE_NAME_CASE(MAD_I24)
  NODE_NAME_CASE(MAD_I64_I32)
  NODE_NAME_CASE(MAD_U64_U32)
  NODE_NAME_CASE(PERM)
  NODE_NAME_CASE(TEXTURE_FETCH)
  NODE_NAME_CASE(R600_EXPORT)
  NODE_NAME_CASE(CONST_ADDRESS)
  NODE_NAME_CASE(REGISTER_LOAD)
  NODE_NAME_CASE(REGISTER_STORE)
  NODE_NAME_CASE(SAMPLE)
  NODE_NAME_CASE(SAMPLEB)
  NODE_NAME_CASE(SAMPLED)
  NODE_NAME_CASE(SAMPLEL)
  NODE_NAME_CASE(CVT_F32_UBYTE0)
  NODE_NAME_CASE(CVT_F32_UBYTE1)
  NODE_NAME_CASE(CVT_F32_UBYTE2)
  NODE_NAME_CASE(CVT_F32_UBYTE3)
  NODE_NAME_CASE(CVT_PKRTZ_F16_F32)
  NODE_NAME_CASE(CVT_PKNORM_I16_F32)
  NODE_NAME_CASE(CVT_PKNORM_U16_F32)
  NODE_NAME_CASE(CVT_PK_I16_I32)
  NODE_NAME_CASE(CVT_PK_U16_U32)
  NODE_NAME_CASE(FP_TO_FP16)
  NODE_NAME_CASE(BUILD_VERTICAL_VECTOR)
  NODE_NAME_CASE(CONST_DATA_PTR)
  NODE_NAME_CASE(PC_ADD_REL_OFFSET)
  NODE_NAME_CASE(LDS)
  NODE_NAME_CASE(FPTRUNC_ROUND_UPWARD)
  NODE_NAME_CASE(FPTRUNC_ROUND_DOWNWARD)
  NODE_NAME_CASE(DUMMY_CHAIN)
  case AMDGPUISD::FIRST_MEM_OPCODE_NUMBER: break;
  NODE_NAME_CASE(LOAD_D16_HI)
  NODE_NAME_CASE(LOAD_D16_LO)
  NODE_NAME_CASE(LOAD_D16_HI_I8)
  NODE_NAME_CASE(LOAD_D16_HI_U8)
  NODE_NAME_CASE(LOAD_D16_LO_I8)
  NODE_NAME_CASE(LOAD_D16_LO_U8)
  NODE_NAME_CASE(STORE_MSKOR)
  NODE_NAME_CASE(LOAD_CONSTANT)
  NODE_NAME_CASE(TBUFFER_STORE_FORMAT)
  NODE_NAME_CASE(TBUFFER_STORE_FORMAT_D16)
  NODE_NAME_CASE(TBUFFER_LOAD_FORMAT)
  NODE_NAME_CASE(TBUFFER_LOAD_FORMAT_D16)
  NODE_NAME_CASE(DS_ORDERED_COUNT)
  NODE_NAME_CASE(ATOMIC_CMP_SWAP)
  NODE_NAME_CASE(BUFFER_LOAD)
  NODE_NAME_CASE(BUFFER_LOAD_UBYTE)
  NODE_NAME_CASE(BUFFER_LOAD_USHORT)
  NODE_NAME_CASE(BUFFER_LOAD_BYTE)
  NODE_NAME_CASE(BUFFER_LOAD_SHORT)
  NODE_NAME_CASE(BUFFER_LOAD_TFE)
  NODE_NAME_CASE(BUFFER_LOAD_UBYTE_TFE)
  NODE_NAME_CASE(BUFFER_LOAD_USHORT_TFE)
  NODE_NAME_CASE(BUFFER_LOAD_BYTE_TFE)
  NODE_NAME_CASE(BUFFER_LOAD_SHORT_TFE)
  NODE_NAME_CASE(BUFFER_LOAD_FORMAT)
  NODE_NAME_CASE(BUFFER_LOAD_FORMAT_TFE)
  NODE_NAME_CASE(BUFFER_LOAD_FORMAT_D16)
  NODE_NAME_CASE(SBUFFER_LOAD)
  NODE_NAME_CASE(SBUFFER_LOAD_BYTE)
  NODE_NAME_CASE(SBUFFER_LOAD_UBYTE)
  NODE_NAME_CASE(SBUFFER_LOAD_SHORT)
  NODE_NAME_CASE(SBUFFER_LOAD_USHORT)
  NODE_NAME_CASE(BUFFER_STORE)
  NODE_NAME_CASE(BUFFER_STORE_BYTE)
  NODE_NAME_CASE(BUFFER_STORE_SHORT)
  NODE_NAME_CASE(BUFFER_STORE_FORMAT)
  NODE_NAME_CASE(BUFFER_STORE_FORMAT_D16)
  NODE_NAME_CASE(BUFFER_ATOMIC_SWAP)
  NODE_NAME_CASE(BUFFER_ATOMIC_ADD)
  NODE_NAME_CASE(BUFFER_ATOMIC_SUB)
  NODE_NAME_CASE(BUFFER_ATOMIC_SMIN)
  NODE_NAME_CASE(BUFFER_ATOMIC_UMIN)
  NODE_NAME_CASE(BUFFER_ATOMIC_SMAX)
  NODE_NAME_CASE(BUFFER_ATOMIC_UMAX)
  NODE_NAME_CASE(BUFFER_ATOMIC_AND)
  NODE_NAME_CASE(BUFFER_ATOMIC_OR)
  NODE_NAME_CASE(BUFFER_ATOMIC_XOR)
  NODE_NAME_CASE(BUFFER_ATOMIC_INC)
  NODE_NAME_CASE(BUFFER_ATOMIC_DEC)
  NODE_NAME_CASE(BUFFER_ATOMIC_CMPSWAP)
  NODE_NAME_CASE(BUFFER_ATOMIC_CSUB)
  NODE_NAME_CASE(BUFFER_ATOMIC_FADD)
  NODE_NAME_CASE(BUFFER_ATOMIC_FMIN)
  NODE_NAME_CASE(BUFFER_ATOMIC_FMAX)
  NODE_NAME_CASE(BUFFER_ATOMIC_COND_SUB_U32)

  case AMDGPUISD::LAST_AMDGPU_ISD_NUMBER: break;
  }
  return nullptr;
}

SDValue AMDGPUTargetLowering::getSqrtEstimate(SDValue Operand,
                                              SelectionDAG &DAG, int Enabled,
                                              int &RefinementSteps,
                                              bool &UseOneConstNR,
                                              bool Reciprocal) const {
  EVT VT = Operand.getValueType();

  if (VT == MVT::f32) {
    RefinementSteps = 0;
    return DAG.getNode(AMDGPUISD::RSQ, SDLoc(Operand), VT, Operand);
  }

  // TODO: There is also f64 rsq instruction, but the documentation is less
  // clear on its precision.

  return SDValue();
}

SDValue AMDGPUTargetLowering::getRecipEstimate(SDValue Operand,
                                               SelectionDAG &DAG, int Enabled,
                                               int &RefinementSteps) const {
  EVT VT = Operand.getValueType();

  if (VT == MVT::f32) {
    // Reciprocal, < 1 ulp error.
    //
    // This reciprocal approximation converges to < 0.5 ulp error with one
    // newton rhapson performed with two fused multiple adds (FMAs).

    RefinementSteps = 0;
    return DAG.getNode(AMDGPUISD::RCP, SDLoc(Operand), VT, Operand);
  }

  // TODO: There is also f64 rcp instruction, but the documentation is less
  // clear on its precision.

  return SDValue();
}

static unsigned workitemIntrinsicDim(unsigned ID) {
  switch (ID) {
  case Intrinsic::amdgcn_workitem_id_x:
    return 0;
  case Intrinsic::amdgcn_workitem_id_y:
    return 1;
  case Intrinsic::amdgcn_workitem_id_z:
    return 2;
  default:
    llvm_unreachable("not a workitem intrinsic");
  }
}

void AMDGPUTargetLowering::computeKnownBitsForTargetNode(
    const SDValue Op, KnownBits &Known,
    const APInt &DemandedElts, const SelectionDAG &DAG, unsigned Depth) const {

  Known.resetAll(); // Don't know anything.

  unsigned Opc = Op.getOpcode();

  switch (Opc) {
  default:
    break;
  case AMDGPUISD::CARRY:
  case AMDGPUISD::BORROW: {
    Known.Zero = APInt::getHighBitsSet(32, 31);
    break;
  }

  case AMDGPUISD::BFE_I32:
  case AMDGPUISD::BFE_U32: {
    ConstantSDNode *CWidth = dyn_cast<ConstantSDNode>(Op.getOperand(2));
    if (!CWidth)
      return;

    uint32_t Width = CWidth->getZExtValue() & 0x1f;

    if (Opc == AMDGPUISD::BFE_U32)
      Known.Zero = APInt::getHighBitsSet(32, 32 - Width);

    break;
  }
  case AMDGPUISD::FP_TO_FP16: {
    unsigned BitWidth = Known.getBitWidth();

    // High bits are zero.
    Known.Zero = APInt::getHighBitsSet(BitWidth, BitWidth - 16);
    break;
  }
  case AMDGPUISD::MUL_U24:
  case AMDGPUISD::MUL_I24: {
    KnownBits LHSKnown = DAG.computeKnownBits(Op.getOperand(0), Depth + 1);
    KnownBits RHSKnown = DAG.computeKnownBits(Op.getOperand(1), Depth + 1);
    unsigned TrailZ = LHSKnown.countMinTrailingZeros() +
                      RHSKnown.countMinTrailingZeros();
    Known.Zero.setLowBits(std::min(TrailZ, 32u));
    // Skip extra check if all bits are known zeros.
    if (TrailZ >= 32)
      break;

    // Truncate to 24 bits.
    LHSKnown = LHSKnown.trunc(24);
    RHSKnown = RHSKnown.trunc(24);

    if (Opc == AMDGPUISD::MUL_I24) {
      unsigned LHSValBits = LHSKnown.countMaxSignificantBits();
      unsigned RHSValBits = RHSKnown.countMaxSignificantBits();
      unsigned MaxValBits = LHSValBits + RHSValBits;
      if (MaxValBits > 32)
        break;
      unsigned SignBits = 32 - MaxValBits + 1;
      bool LHSNegative = LHSKnown.isNegative();
      bool LHSNonNegative = LHSKnown.isNonNegative();
      bool LHSPositive = LHSKnown.isStrictlyPositive();
      bool RHSNegative = RHSKnown.isNegative();
      bool RHSNonNegative = RHSKnown.isNonNegative();
      bool RHSPositive = RHSKnown.isStrictlyPositive();

      if ((LHSNonNegative && RHSNonNegative) || (LHSNegative && RHSNegative))
        Known.Zero.setHighBits(SignBits);
      else if ((LHSNegative && RHSPositive) || (LHSPositive && RHSNegative))
        Known.One.setHighBits(SignBits);
    } else {
      unsigned LHSValBits = LHSKnown.countMaxActiveBits();
      unsigned RHSValBits = RHSKnown.countMaxActiveBits();
      unsigned MaxValBits = LHSValBits + RHSValBits;
      if (MaxValBits >= 32)
        break;
      Known.Zero.setBitsFrom(MaxValBits);
    }
    break;
  }
  case AMDGPUISD::PERM: {
    ConstantSDNode *CMask = dyn_cast<ConstantSDNode>(Op.getOperand(2));
    if (!CMask)
      return;

    KnownBits LHSKnown = DAG.computeKnownBits(Op.getOperand(0), Depth + 1);
    KnownBits RHSKnown = DAG.computeKnownBits(Op.getOperand(1), Depth + 1);
    unsigned Sel = CMask->getZExtValue();

    for (unsigned I = 0; I < 32; I += 8) {
      unsigned SelBits = Sel & 0xff;
      if (SelBits < 4) {
        SelBits *= 8;
        Known.One |= ((RHSKnown.One.getZExtValue() >> SelBits) & 0xff) << I;
        Known.Zero |= ((RHSKnown.Zero.getZExtValue() >> SelBits) & 0xff) << I;
      } else if (SelBits < 7) {
        SelBits = (SelBits & 3) * 8;
        Known.One |= ((LHSKnown.One.getZExtValue() >> SelBits) & 0xff) << I;
        Known.Zero |= ((LHSKnown.Zero.getZExtValue() >> SelBits) & 0xff) << I;
      } else if (SelBits == 0x0c) {
        Known.Zero |= 0xFFull << I;
      } else if (SelBits > 0x0c) {
        Known.One |= 0xFFull << I;
      }
      Sel >>= 8;
    }
    break;
  }
  case AMDGPUISD::BUFFER_LOAD_UBYTE:  {
    Known.Zero.setHighBits(24);
    break;
  }
  case AMDGPUISD::BUFFER_LOAD_USHORT: {
    Known.Zero.setHighBits(16);
    break;
  }
  case AMDGPUISD::LDS: {
    auto GA = cast<GlobalAddressSDNode>(Op.getOperand(0).getNode());
    Align Alignment = GA->getGlobal()->getPointerAlignment(DAG.getDataLayout());

    Known.Zero.setHighBits(16);
    Known.Zero.setLowBits(Log2(Alignment));
    break;
  }
  case AMDGPUISD::SMIN3:
  case AMDGPUISD::SMAX3:
  case AMDGPUISD::SMED3:
  case AMDGPUISD::UMIN3:
  case AMDGPUISD::UMAX3:
  case AMDGPUISD::UMED3: {
    KnownBits Known2 = DAG.computeKnownBits(Op.getOperand(2), Depth + 1);
    if (Known2.isUnknown())
      break;

    KnownBits Known1 = DAG.computeKnownBits(Op.getOperand(1), Depth + 1);
    if (Known1.isUnknown())
      break;

    KnownBits Known0 = DAG.computeKnownBits(Op.getOperand(0), Depth + 1);
    if (Known0.isUnknown())
      break;

    // TODO: Handle LeadZero/LeadOne from UMIN/UMAX handling.
    Known.Zero = Known0.Zero & Known1.Zero & Known2.Zero;
    Known.One = Known0.One & Known1.One & Known2.One;
    break;
  }
  case ISD::INTRINSIC_WO_CHAIN: {
    unsigned IID = Op.getConstantOperandVal(0);
    switch (IID) {
    case Intrinsic::amdgcn_workitem_id_x:
    case Intrinsic::amdgcn_workitem_id_y:
    case Intrinsic::amdgcn_workitem_id_z: {
      unsigned MaxValue = Subtarget->getMaxWorkitemID(
          DAG.getMachineFunction().getFunction(), workitemIntrinsicDim(IID));
      Known.Zero.setHighBits(llvm::countl_zero(MaxValue));
      break;
    }
    default:
      break;
    }
  }
  }
}

unsigned AMDGPUTargetLowering::ComputeNumSignBitsForTargetNode(
    SDValue Op, const APInt &DemandedElts, const SelectionDAG &DAG,
    unsigned Depth) const {
  switch (Op.getOpcode()) {
  case AMDGPUISD::BFE_I32: {
    ConstantSDNode *Width = dyn_cast<ConstantSDNode>(Op.getOperand(2));
    if (!Width)
      return 1;

    unsigned SignBits = 32 - Width->getZExtValue() + 1;
    if (!isNullConstant(Op.getOperand(1)))
      return SignBits;

    // TODO: Could probably figure something out with non-0 offsets.
    unsigned Op0SignBits = DAG.ComputeNumSignBits(Op.getOperand(0), Depth + 1);
    return std::max(SignBits, Op0SignBits);
  }

  case AMDGPUISD::BFE_U32: {
    ConstantSDNode *Width = dyn_cast<ConstantSDNode>(Op.getOperand(2));
    return Width ? 32 - (Width->getZExtValue() & 0x1f) : 1;
  }

  case AMDGPUISD::CARRY:
  case AMDGPUISD::BORROW:
    return 31;
  case AMDGPUISD::BUFFER_LOAD_BYTE:
    return 25;
  case AMDGPUISD::BUFFER_LOAD_SHORT:
    return 17;
  case AMDGPUISD::BUFFER_LOAD_UBYTE:
    return 24;
  case AMDGPUISD::BUFFER_LOAD_USHORT:
    return 16;
  case AMDGPUISD::FP_TO_FP16:
    return 16;
  case AMDGPUISD::SMIN3:
  case AMDGPUISD::SMAX3:
  case AMDGPUISD::SMED3:
  case AMDGPUISD::UMIN3:
  case AMDGPUISD::UMAX3:
  case AMDGPUISD::UMED3: {
    unsigned Tmp2 = DAG.ComputeNumSignBits(Op.getOperand(2), Depth + 1);
    if (Tmp2 == 1)
      return 1; // Early out.

    unsigned Tmp1 = DAG.ComputeNumSignBits(Op.getOperand(1), Depth + 1);
    if (Tmp1 == 1)
      return 1; // Early out.

    unsigned Tmp0 = DAG.ComputeNumSignBits(Op.getOperand(0), Depth + 1);
    if (Tmp0 == 1)
      return 1; // Early out.

    return std::min({Tmp0, Tmp1, Tmp2});
  }
  default:
    return 1;
  }
}

unsigned AMDGPUTargetLowering::computeNumSignBitsForTargetInstr(
  GISelKnownBits &Analysis, Register R,
  const APInt &DemandedElts, const MachineRegisterInfo &MRI,
  unsigned Depth) const {
  const MachineInstr *MI = MRI.getVRegDef(R);
  if (!MI)
    return 1;

  // TODO: Check range metadata on MMO.
  switch (MI->getOpcode()) {
  case AMDGPU::G_AMDGPU_BUFFER_LOAD_SBYTE:
    return 25;
  case AMDGPU::G_AMDGPU_BUFFER_LOAD_SSHORT:
    return 17;
  case AMDGPU::G_AMDGPU_BUFFER_LOAD_UBYTE:
    return 24;
  case AMDGPU::G_AMDGPU_BUFFER_LOAD_USHORT:
    return 16;
  case AMDGPU::G_AMDGPU_SMED3:
  case AMDGPU::G_AMDGPU_UMED3: {
    auto [Dst, Src0, Src1, Src2] = MI->getFirst4Regs();
    unsigned Tmp2 = Analysis.computeNumSignBits(Src2, DemandedElts, Depth + 1);
    if (Tmp2 == 1)
      return 1;
    unsigned Tmp1 = Analysis.computeNumSignBits(Src1, DemandedElts, Depth + 1);
    if (Tmp1 == 1)
      return 1;
    unsigned Tmp0 = Analysis.computeNumSignBits(Src0, DemandedElts, Depth + 1);
    if (Tmp0 == 1)
      return 1;
    return std::min({Tmp0, Tmp1, Tmp2});
  }
  default:
    return 1;
  }
}

bool AMDGPUTargetLowering::isKnownNeverNaNForTargetNode(SDValue Op,
                                                        const SelectionDAG &DAG,
                                                        bool SNaN,
                                                        unsigned Depth) const {
  unsigned Opcode = Op.getOpcode();
  switch (Opcode) {
  case AMDGPUISD::FMIN_LEGACY:
  case AMDGPUISD::FMAX_LEGACY: {
    if (SNaN)
      return true;

    // TODO: Can check no nans on one of the operands for each one, but which
    // one?
    return false;
  }
  case AMDGPUISD::FMUL_LEGACY:
  case AMDGPUISD::CVT_PKRTZ_F16_F32: {
    if (SNaN)
      return true;
    return DAG.isKnownNeverNaN(Op.getOperand(0), SNaN, Depth + 1) &&
           DAG.isKnownNeverNaN(Op.getOperand(1), SNaN, Depth + 1);
  }
  case AMDGPUISD::FMED3:
  case AMDGPUISD::FMIN3:
  case AMDGPUISD::FMAX3:
  case AMDGPUISD::FMINIMUM3:
  case AMDGPUISD::FMAXIMUM3:
  case AMDGPUISD::FMAD_FTZ: {
    if (SNaN)
      return true;
    return DAG.isKnownNeverNaN(Op.getOperand(0), SNaN, Depth + 1) &&
           DAG.isKnownNeverNaN(Op.getOperand(1), SNaN, Depth + 1) &&
           DAG.isKnownNeverNaN(Op.getOperand(2), SNaN, Depth + 1);
  }
  case AMDGPUISD::CVT_F32_UBYTE0:
  case AMDGPUISD::CVT_F32_UBYTE1:
  case AMDGPUISD::CVT_F32_UBYTE2:
  case AMDGPUISD::CVT_F32_UBYTE3:
    return true;

  case AMDGPUISD::RCP:
  case AMDGPUISD::RSQ:
  case AMDGPUISD::RCP_LEGACY:
  case AMDGPUISD::RSQ_CLAMP: {
    if (SNaN)
      return true;

    // TODO: Need is known positive check.
    return false;
  }
  case ISD::FLDEXP:
  case AMDGPUISD::FRACT: {
    if (SNaN)
      return true;
    return DAG.isKnownNeverNaN(Op.getOperand(0), SNaN, Depth + 1);
  }
  case AMDGPUISD::DIV_SCALE:
  case AMDGPUISD::DIV_FMAS:
  case AMDGPUISD::DIV_FIXUP:
    // TODO: Refine on operands.
    return SNaN;
  case AMDGPUISD::SIN_HW:
  case AMDGPUISD::COS_HW: {
    // TODO: Need check for infinity
    return SNaN;
  }
  case ISD::INTRINSIC_WO_CHAIN: {
    unsigned IntrinsicID = Op.getConstantOperandVal(0);
    // TODO: Handle more intrinsics
    switch (IntrinsicID) {
    case Intrinsic::amdgcn_cubeid:
      return true;

    case Intrinsic::amdgcn_frexp_mant: {
      if (SNaN)
        return true;
      return DAG.isKnownNeverNaN(Op.getOperand(1), SNaN, Depth + 1);
    }
    case Intrinsic::amdgcn_cvt_pkrtz: {
      if (SNaN)
        return true;
      return DAG.isKnownNeverNaN(Op.getOperand(1), SNaN, Depth + 1) &&
             DAG.isKnownNeverNaN(Op.getOperand(2), SNaN, Depth + 1);
    }
    case Intrinsic::amdgcn_rcp:
    case Intrinsic::amdgcn_rsq:
    case Intrinsic::amdgcn_rcp_legacy:
    case Intrinsic::amdgcn_rsq_legacy:
    case Intrinsic::amdgcn_rsq_clamp: {
      if (SNaN)
        return true;

      // TODO: Need is known positive check.
      return false;
    }
    case Intrinsic::amdgcn_trig_preop:
    case Intrinsic::amdgcn_fdot2:
      // TODO: Refine on operand
      return SNaN;
    case Intrinsic::amdgcn_fma_legacy:
      if (SNaN)
        return true;
      return DAG.isKnownNeverNaN(Op.getOperand(1), SNaN, Depth + 1) &&
             DAG.isKnownNeverNaN(Op.getOperand(2), SNaN, Depth + 1) &&
             DAG.isKnownNeverNaN(Op.getOperand(3), SNaN, Depth + 1);
    default:
      return false;
    }
  }
  default:
    return false;
  }
}

bool AMDGPUTargetLowering::isReassocProfitable(MachineRegisterInfo &MRI,
                                               Register N0, Register N1) const {
  return MRI.hasOneNonDBGUse(N0); // FIXME: handle regbanks
}

TargetLowering::AtomicExpansionKind
AMDGPUTargetLowering::shouldExpandAtomicRMWInIR(AtomicRMWInst *RMW) const {
  switch (RMW->getOperation()) {
  case AtomicRMWInst::Nand:
  case AtomicRMWInst::FAdd:
  case AtomicRMWInst::FSub:
  case AtomicRMWInst::FMax:
  case AtomicRMWInst::FMin:
    return AtomicExpansionKind::CmpXChg;
  case AtomicRMWInst::Xchg: {
    const DataLayout &DL = RMW->getFunction()->getDataLayout();
    unsigned ValSize = DL.getTypeSizeInBits(RMW->getType());
    if (ValSize == 32 || ValSize == 64)
      return AtomicExpansionKind::None;
    return AtomicExpansionKind::CmpXChg;
  }
  default: {
    if (auto *IntTy = dyn_cast<IntegerType>(RMW->getType())) {
      unsigned Size = IntTy->getBitWidth();
      if (Size == 32 || Size == 64)
        return AtomicExpansionKind::None;
    }

    return AtomicExpansionKind::CmpXChg;
  }
  }
}

/// Whether it is profitable to sink the operands of an
/// Instruction I to the basic block of I.
/// This helps using several modifiers (like abs and neg) more often.
bool AMDGPUTargetLowering::shouldSinkOperands(
    Instruction *I, SmallVectorImpl<Use *> &Ops) const {
  using namespace PatternMatch;

  for (auto &Op : I->operands()) {
    // Ensure we are not already sinking this operand.
    if (any_of(Ops, [&](Use *U) { return U->get() == Op.get(); }))
      continue;

    if (match(&Op, m_FAbs(m_Value())) || match(&Op, m_FNeg(m_Value())))
      Ops.push_back(&Op);
  }

  return !Ops.empty();
}
