//===- SPIRVLegalizerInfo.cpp --- SPIR-V Legalization Rules ------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the targeting of the Machinelegalizer class for SPIR-V.
//
//===----------------------------------------------------------------------===//

#include "SPIRVLegalizerInfo.h"
#include "SPIRV.h"
#include "SPIRVGlobalRegistry.h"
#include "SPIRVSubtarget.h"
#include "llvm/CodeGen/GlobalISel/LegalizerHelper.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"

using namespace llvm;
using namespace llvm::LegalizeActions;
using namespace llvm::LegalityPredicates;

static const std::set<unsigned> TypeFoldingSupportingOpcs = {
    TargetOpcode::G_ADD,
    TargetOpcode::G_FADD,
    TargetOpcode::G_SUB,
    TargetOpcode::G_FSUB,
    TargetOpcode::G_MUL,
    TargetOpcode::G_FMUL,
    TargetOpcode::G_SDIV,
    TargetOpcode::G_UDIV,
    TargetOpcode::G_FDIV,
    TargetOpcode::G_SREM,
    TargetOpcode::G_UREM,
    TargetOpcode::G_FREM,
    TargetOpcode::G_FNEG,
    TargetOpcode::G_CONSTANT,
    TargetOpcode::G_FCONSTANT,
    TargetOpcode::G_AND,
    TargetOpcode::G_OR,
    TargetOpcode::G_XOR,
    TargetOpcode::G_SHL,
    TargetOpcode::G_ASHR,
    TargetOpcode::G_LSHR,
    TargetOpcode::G_SELECT,
    TargetOpcode::G_EXTRACT_VECTOR_ELT,
};

bool isTypeFoldingSupported(unsigned Opcode) {
  return TypeFoldingSupportingOpcs.count(Opcode) > 0;
}

SPIRVLegalizerInfo::SPIRVLegalizerInfo(const SPIRVSubtarget &ST) {
  using namespace TargetOpcode;

  this->ST = &ST;
  GR = ST.getSPIRVGlobalRegistry();

  const LLT s1 = LLT::scalar(1);
  const LLT s8 = LLT::scalar(8);
  const LLT s16 = LLT::scalar(16);
  const LLT s32 = LLT::scalar(32);
  const LLT s64 = LLT::scalar(64);

  const LLT v16s64 = LLT::fixed_vector(16, 64);
  const LLT v16s32 = LLT::fixed_vector(16, 32);
  const LLT v16s16 = LLT::fixed_vector(16, 16);
  const LLT v16s8 = LLT::fixed_vector(16, 8);
  const LLT v16s1 = LLT::fixed_vector(16, 1);

  const LLT v8s64 = LLT::fixed_vector(8, 64);
  const LLT v8s32 = LLT::fixed_vector(8, 32);
  const LLT v8s16 = LLT::fixed_vector(8, 16);
  const LLT v8s8 = LLT::fixed_vector(8, 8);
  const LLT v8s1 = LLT::fixed_vector(8, 1);

  const LLT v4s64 = LLT::fixed_vector(4, 64);
  const LLT v4s32 = LLT::fixed_vector(4, 32);
  const LLT v4s16 = LLT::fixed_vector(4, 16);
  const LLT v4s8 = LLT::fixed_vector(4, 8);
  const LLT v4s1 = LLT::fixed_vector(4, 1);

  const LLT v3s64 = LLT::fixed_vector(3, 64);
  const LLT v3s32 = LLT::fixed_vector(3, 32);
  const LLT v3s16 = LLT::fixed_vector(3, 16);
  const LLT v3s8 = LLT::fixed_vector(3, 8);
  const LLT v3s1 = LLT::fixed_vector(3, 1);

  const LLT v2s64 = LLT::fixed_vector(2, 64);
  const LLT v2s32 = LLT::fixed_vector(2, 32);
  const LLT v2s16 = LLT::fixed_vector(2, 16);
  const LLT v2s8 = LLT::fixed_vector(2, 8);
  const LLT v2s1 = LLT::fixed_vector(2, 1);

  const unsigned PSize = ST.getPointerSize();
  const LLT p0 = LLT::pointer(0, PSize); // Function
  const LLT p1 = LLT::pointer(1, PSize); // CrossWorkgroup
  const LLT p2 = LLT::pointer(2, PSize); // UniformConstant
  const LLT p3 = LLT::pointer(3, PSize); // Workgroup
  const LLT p4 = LLT::pointer(4, PSize); // Generic
  const LLT p5 =
      LLT::pointer(5, PSize); // Input, SPV_INTEL_usm_storage_classes (Device)
  const LLT p6 = LLT::pointer(6, PSize); // SPV_INTEL_usm_storage_classes (Host)

  // TODO: remove copy-pasting here by using concatenation in some way.
  auto allPtrsScalarsAndVectors = {
      p0,    p1,    p2,    p3,    p4,     p5,     p6,    s1,   s8,   s16,
      s32,   s64,   v2s1,  v2s8,  v2s16,  v2s32,  v2s64, v3s1, v3s8, v3s16,
      v3s32, v3s64, v4s1,  v4s8,  v4s16,  v4s32,  v4s64, v8s1, v8s8, v8s16,
      v8s32, v8s64, v16s1, v16s8, v16s16, v16s32, v16s64};

  auto allVectors = {v2s1,  v2s8,   v2s16,  v2s32, v2s64, v3s1,  v3s8,
                     v3s16, v3s32,  v3s64,  v4s1,  v4s8,  v4s16, v4s32,
                     v4s64, v8s1,   v8s8,   v8s16, v8s32, v8s64, v16s1,
                     v16s8, v16s16, v16s32, v16s64};

  auto allScalarsAndVectors = {
      s1,   s8,   s16,   s32,   s64,   v2s1,  v2s8,  v2s16,  v2s32,  v2s64,
      v3s1, v3s8, v3s16, v3s32, v3s64, v4s1,  v4s8,  v4s16,  v4s32,  v4s64,
      v8s1, v8s8, v8s16, v8s32, v8s64, v16s1, v16s8, v16s16, v16s32, v16s64};

  auto allIntScalarsAndVectors = {s8,    s16,   s32,   s64,    v2s8,   v2s16,
                                  v2s32, v2s64, v3s8,  v3s16,  v3s32,  v3s64,
                                  v4s8,  v4s16, v4s32, v4s64,  v8s8,   v8s16,
                                  v8s32, v8s64, v16s8, v16s16, v16s32, v16s64};

  auto allBoolScalarsAndVectors = {s1, v2s1, v3s1, v4s1, v8s1, v16s1};

  auto allIntScalars = {s8, s16, s32, s64};

  auto allFloatScalars = {s16, s32, s64};

  auto allFloatScalarsAndVectors = {
      s16,   s32,   s64,   v2s16, v2s32, v2s64, v3s16,  v3s32,  v3s64,
      v4s16, v4s32, v4s64, v8s16, v8s32, v8s64, v16s16, v16s32, v16s64};

  auto allFloatAndIntScalarsAndPtrs = {s8, s16, s32, s64, p0, p1,
                                       p2, p3,  p4,  p5,  p6};

  auto allPtrs = {p0, p1, p2, p3, p4, p5, p6};
  auto allWritablePtrs = {p0, p1, p3, p4, p5, p6};

  for (auto Opc : TypeFoldingSupportingOpcs)
    getActionDefinitionsBuilder(Opc).custom();

  getActionDefinitionsBuilder(G_GLOBAL_VALUE).alwaysLegal();

  // TODO: add proper rules for vectors legalization.
  getActionDefinitionsBuilder(
      {G_BUILD_VECTOR, G_SHUFFLE_VECTOR, G_SPLAT_VECTOR})
      .alwaysLegal();

  // Vector Reduction Operations
  getActionDefinitionsBuilder(
      {G_VECREDUCE_SMIN, G_VECREDUCE_SMAX, G_VECREDUCE_UMIN, G_VECREDUCE_UMAX,
       G_VECREDUCE_ADD, G_VECREDUCE_MUL, G_VECREDUCE_FMUL, G_VECREDUCE_FMIN,
       G_VECREDUCE_FMAX, G_VECREDUCE_FMINIMUM, G_VECREDUCE_FMAXIMUM,
       G_VECREDUCE_OR, G_VECREDUCE_AND, G_VECREDUCE_XOR})
      .legalFor(allVectors)
      .scalarize(1)
      .lower();

  getActionDefinitionsBuilder({G_VECREDUCE_SEQ_FADD, G_VECREDUCE_SEQ_FMUL})
      .scalarize(2)
      .lower();

  // Merge/Unmerge
  // TODO: add proper legalization rules.
  getActionDefinitionsBuilder(G_UNMERGE_VALUES).alwaysLegal();

  getActionDefinitionsBuilder({G_MEMCPY, G_MEMMOVE})
      .legalIf(all(typeInSet(0, allWritablePtrs), typeInSet(1, allPtrs)));

  getActionDefinitionsBuilder(G_MEMSET).legalIf(
      all(typeInSet(0, allWritablePtrs), typeInSet(1, allIntScalars)));

  getActionDefinitionsBuilder(G_ADDRSPACE_CAST)
      .legalForCartesianProduct(allPtrs, allPtrs);

  getActionDefinitionsBuilder({G_LOAD, G_STORE}).legalIf(typeInSet(1, allPtrs));

  getActionDefinitionsBuilder(G_BITREVERSE).legalFor(allIntScalarsAndVectors);

  getActionDefinitionsBuilder(G_FMA).legalFor(allFloatScalarsAndVectors);

  getActionDefinitionsBuilder({G_FPTOSI, G_FPTOUI})
      .legalForCartesianProduct(allIntScalarsAndVectors,
                                allFloatScalarsAndVectors);

  getActionDefinitionsBuilder({G_SITOFP, G_UITOFP})
      .legalForCartesianProduct(allFloatScalarsAndVectors,
                                allScalarsAndVectors);

  getActionDefinitionsBuilder({G_SMIN, G_SMAX, G_UMIN, G_UMAX, G_ABS})
      .legalFor(allIntScalarsAndVectors);

  getActionDefinitionsBuilder(G_CTPOP).legalForCartesianProduct(
      allIntScalarsAndVectors, allIntScalarsAndVectors);

  getActionDefinitionsBuilder(G_PHI).legalFor(allPtrsScalarsAndVectors);

  getActionDefinitionsBuilder(G_BITCAST).legalIf(
      all(typeInSet(0, allPtrsScalarsAndVectors),
          typeInSet(1, allPtrsScalarsAndVectors)));

  getActionDefinitionsBuilder({G_IMPLICIT_DEF, G_FREEZE}).alwaysLegal();

  getActionDefinitionsBuilder({G_STACKSAVE, G_STACKRESTORE}).alwaysLegal();

  getActionDefinitionsBuilder(G_INTTOPTR)
      .legalForCartesianProduct(allPtrs, allIntScalars);
  getActionDefinitionsBuilder(G_PTRTOINT)
      .legalForCartesianProduct(allIntScalars, allPtrs);
  getActionDefinitionsBuilder(G_PTR_ADD).legalForCartesianProduct(
      allPtrs, allIntScalars);

  // ST.canDirectlyComparePointers() for pointer args is supported in
  // legalizeCustom().
  getActionDefinitionsBuilder(G_ICMP).customIf(
      all(typeInSet(0, allBoolScalarsAndVectors),
          typeInSet(1, allPtrsScalarsAndVectors)));

  getActionDefinitionsBuilder(G_FCMP).legalIf(
      all(typeInSet(0, allBoolScalarsAndVectors),
          typeInSet(1, allFloatScalarsAndVectors)));

  getActionDefinitionsBuilder({G_ATOMICRMW_OR, G_ATOMICRMW_ADD, G_ATOMICRMW_AND,
                               G_ATOMICRMW_MAX, G_ATOMICRMW_MIN,
                               G_ATOMICRMW_SUB, G_ATOMICRMW_XOR,
                               G_ATOMICRMW_UMAX, G_ATOMICRMW_UMIN})
      .legalForCartesianProduct(allIntScalars, allWritablePtrs);

  getActionDefinitionsBuilder(
      {G_ATOMICRMW_FADD, G_ATOMICRMW_FSUB, G_ATOMICRMW_FMIN, G_ATOMICRMW_FMAX})
      .legalForCartesianProduct(allFloatScalars, allWritablePtrs);

  getActionDefinitionsBuilder(G_ATOMICRMW_XCHG)
      .legalForCartesianProduct(allFloatAndIntScalarsAndPtrs, allWritablePtrs);

  getActionDefinitionsBuilder(G_ATOMIC_CMPXCHG_WITH_SUCCESS).lower();
  // TODO: add proper legalization rules.
  getActionDefinitionsBuilder(G_ATOMIC_CMPXCHG).alwaysLegal();

  getActionDefinitionsBuilder({G_UADDO, G_USUBO, G_SMULO, G_UMULO})
      .alwaysLegal();

  // Extensions.
  getActionDefinitionsBuilder({G_TRUNC, G_ZEXT, G_SEXT, G_ANYEXT})
      .legalForCartesianProduct(allScalarsAndVectors);

  // FP conversions.
  getActionDefinitionsBuilder({G_FPTRUNC, G_FPEXT})
      .legalForCartesianProduct(allFloatScalarsAndVectors);

  // Pointer-handling.
  getActionDefinitionsBuilder(G_FRAME_INDEX).legalFor({p0});

  // Control-flow. In some cases (e.g. constants) s1 may be promoted to s32.
  getActionDefinitionsBuilder(G_BRCOND).legalFor({s1, s32});

  // TODO: Review the target OpenCL and GLSL Extended Instruction Set specs to
  // tighten these requirements. Many of these math functions are only legal on
  // specific bitwidths, so they are not selectable for
  // allFloatScalarsAndVectors.
  getActionDefinitionsBuilder({G_FPOW,
                               G_FEXP,
                               G_FEXP2,
                               G_FLOG,
                               G_FLOG2,
                               G_FLOG10,
                               G_FABS,
                               G_FMINNUM,
                               G_FMAXNUM,
                               G_FCEIL,
                               G_FCOS,
                               G_FSIN,
                               G_FTAN,
                               G_FACOS,
                               G_FASIN,
                               G_FATAN,
                               G_FCOSH,
                               G_FSINH,
                               G_FTANH,
                               G_FSQRT,
                               G_FFLOOR,
                               G_FRINT,
                               G_FNEARBYINT,
                               G_INTRINSIC_ROUND,
                               G_INTRINSIC_TRUNC,
                               G_FMINIMUM,
                               G_FMAXIMUM,
                               G_INTRINSIC_ROUNDEVEN})
      .legalFor(allFloatScalarsAndVectors);

  getActionDefinitionsBuilder(G_FCOPYSIGN)
      .legalForCartesianProduct(allFloatScalarsAndVectors,
                                allFloatScalarsAndVectors);

  getActionDefinitionsBuilder(G_FPOWI).legalForCartesianProduct(
      allFloatScalarsAndVectors, allIntScalarsAndVectors);

  if (ST.canUseExtInstSet(SPIRV::InstructionSet::OpenCL_std)) {
    getActionDefinitionsBuilder(
        {G_CTTZ, G_CTTZ_ZERO_UNDEF, G_CTLZ, G_CTLZ_ZERO_UNDEF})
        .legalForCartesianProduct(allIntScalarsAndVectors,
                                  allIntScalarsAndVectors);

    // Struct return types become a single scalar, so cannot easily legalize.
    getActionDefinitionsBuilder({G_SMULH, G_UMULH}).alwaysLegal();

    // supported saturation arithmetic
    getActionDefinitionsBuilder({G_SADDSAT, G_UADDSAT, G_SSUBSAT, G_USUBSAT})
        .legalFor(allIntScalarsAndVectors);
  }

  getLegacyLegalizerInfo().computeTables();
  verify(*ST.getInstrInfo());
}

static Register convertPtrToInt(Register Reg, LLT ConvTy, SPIRVType *SpirvType,
                                LegalizerHelper &Helper,
                                MachineRegisterInfo &MRI,
                                SPIRVGlobalRegistry *GR) {
  Register ConvReg = MRI.createGenericVirtualRegister(ConvTy);
  GR->assignSPIRVTypeToVReg(SpirvType, ConvReg, Helper.MIRBuilder.getMF());
  Helper.MIRBuilder.buildInstr(TargetOpcode::G_PTRTOINT)
      .addDef(ConvReg)
      .addUse(Reg);
  return ConvReg;
}

bool SPIRVLegalizerInfo::legalizeCustom(
    LegalizerHelper &Helper, MachineInstr &MI,
    LostDebugLocObserver &LocObserver) const {
  auto Opc = MI.getOpcode();
  MachineRegisterInfo &MRI = MI.getMF()->getRegInfo();
  if (!isTypeFoldingSupported(Opc)) {
    assert(Opc == TargetOpcode::G_ICMP);
    assert(GR->getSPIRVTypeForVReg(MI.getOperand(0).getReg()));
    auto &Op0 = MI.getOperand(2);
    auto &Op1 = MI.getOperand(3);
    Register Reg0 = Op0.getReg();
    Register Reg1 = Op1.getReg();
    CmpInst::Predicate Cond =
        static_cast<CmpInst::Predicate>(MI.getOperand(1).getPredicate());
    if ((!ST->canDirectlyComparePointers() ||
         (Cond != CmpInst::ICMP_EQ && Cond != CmpInst::ICMP_NE)) &&
        MRI.getType(Reg0).isPointer() && MRI.getType(Reg1).isPointer()) {
      LLT ConvT = LLT::scalar(ST->getPointerSize());
      Type *LLVMTy = IntegerType::get(MI.getMF()->getFunction().getContext(),
                                      ST->getPointerSize());
      SPIRVType *SpirvTy = GR->getOrCreateSPIRVType(LLVMTy, Helper.MIRBuilder);
      Op0.setReg(convertPtrToInt(Reg0, ConvT, SpirvTy, Helper, MRI, GR));
      Op1.setReg(convertPtrToInt(Reg1, ConvT, SpirvTy, Helper, MRI, GR));
    }
    return true;
  }
  // TODO: implement legalization for other opcodes.
  return true;
}
