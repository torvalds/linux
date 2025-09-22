//===-- SPIRVGlobalRegistry.cpp - SPIR-V Global Registry --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the implementation of the SPIRVGlobalRegistry class,
// which is used to maintain rich type information required for SPIR-V even
// after lowering from LLVM IR to GMIR. It can convert an llvm::Type into
// an OpTypeXXX instruction, and map it to a virtual register. Also it builds
// and supports consistency of constants and global variables.
//
//===----------------------------------------------------------------------===//

#include "SPIRVGlobalRegistry.h"
#include "SPIRV.h"
#include "SPIRVBuiltins.h"
#include "SPIRVSubtarget.h"
#include "SPIRVTargetMachine.h"
#include "SPIRVUtils.h"
#include "llvm/ADT/APInt.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Casting.h"
#include <cassert>

using namespace llvm;
SPIRVGlobalRegistry::SPIRVGlobalRegistry(unsigned PointerSize)
    : PointerSize(PointerSize), Bound(0) {}

SPIRVType *SPIRVGlobalRegistry::assignIntTypeToVReg(unsigned BitWidth,
                                                    Register VReg,
                                                    MachineInstr &I,
                                                    const SPIRVInstrInfo &TII) {
  SPIRVType *SpirvType = getOrCreateSPIRVIntegerType(BitWidth, I, TII);
  assignSPIRVTypeToVReg(SpirvType, VReg, *CurMF);
  return SpirvType;
}

SPIRVType *
SPIRVGlobalRegistry::assignFloatTypeToVReg(unsigned BitWidth, Register VReg,
                                           MachineInstr &I,
                                           const SPIRVInstrInfo &TII) {
  SPIRVType *SpirvType = getOrCreateSPIRVFloatType(BitWidth, I, TII);
  assignSPIRVTypeToVReg(SpirvType, VReg, *CurMF);
  return SpirvType;
}

SPIRVType *SPIRVGlobalRegistry::assignVectTypeToVReg(
    SPIRVType *BaseType, unsigned NumElements, Register VReg, MachineInstr &I,
    const SPIRVInstrInfo &TII) {
  SPIRVType *SpirvType =
      getOrCreateSPIRVVectorType(BaseType, NumElements, I, TII);
  assignSPIRVTypeToVReg(SpirvType, VReg, *CurMF);
  return SpirvType;
}

SPIRVType *SPIRVGlobalRegistry::assignTypeToVReg(
    const Type *Type, Register VReg, MachineIRBuilder &MIRBuilder,
    SPIRV::AccessQualifier::AccessQualifier AccessQual, bool EmitIR) {
  SPIRVType *SpirvType =
      getOrCreateSPIRVType(Type, MIRBuilder, AccessQual, EmitIR);
  assignSPIRVTypeToVReg(SpirvType, VReg, MIRBuilder.getMF());
  return SpirvType;
}

void SPIRVGlobalRegistry::assignSPIRVTypeToVReg(SPIRVType *SpirvType,
                                                Register VReg,
                                                MachineFunction &MF) {
  VRegToTypeMap[&MF][VReg] = SpirvType;
}

static Register createTypeVReg(MachineIRBuilder &MIRBuilder) {
  auto &MRI = MIRBuilder.getMF().getRegInfo();
  auto Res = MRI.createGenericVirtualRegister(LLT::scalar(32));
  MRI.setRegClass(Res, &SPIRV::TYPERegClass);
  return Res;
}

static Register createTypeVReg(MachineRegisterInfo &MRI) {
  auto Res = MRI.createGenericVirtualRegister(LLT::scalar(32));
  MRI.setRegClass(Res, &SPIRV::TYPERegClass);
  return Res;
}

SPIRVType *SPIRVGlobalRegistry::getOpTypeBool(MachineIRBuilder &MIRBuilder) {
  return MIRBuilder.buildInstr(SPIRV::OpTypeBool)
      .addDef(createTypeVReg(MIRBuilder));
}

unsigned SPIRVGlobalRegistry::adjustOpTypeIntWidth(unsigned Width) const {
  if (Width > 64)
    report_fatal_error("Unsupported integer width!");
  const SPIRVSubtarget &ST = cast<SPIRVSubtarget>(CurMF->getSubtarget());
  if (ST.canUseExtension(
          SPIRV::Extension::SPV_INTEL_arbitrary_precision_integers))
    return Width;
  if (Width <= 8)
    Width = 8;
  else if (Width <= 16)
    Width = 16;
  else if (Width <= 32)
    Width = 32;
  else
    Width = 64;
  return Width;
}

SPIRVType *SPIRVGlobalRegistry::getOpTypeInt(unsigned Width,
                                             MachineIRBuilder &MIRBuilder,
                                             bool IsSigned) {
  Width = adjustOpTypeIntWidth(Width);
  const SPIRVSubtarget &ST =
      cast<SPIRVSubtarget>(MIRBuilder.getMF().getSubtarget());
  if (ST.canUseExtension(
          SPIRV::Extension::SPV_INTEL_arbitrary_precision_integers)) {
    MIRBuilder.buildInstr(SPIRV::OpExtension)
        .addImm(SPIRV::Extension::SPV_INTEL_arbitrary_precision_integers);
    MIRBuilder.buildInstr(SPIRV::OpCapability)
        .addImm(SPIRV::Capability::ArbitraryPrecisionIntegersINTEL);
  }
  auto MIB = MIRBuilder.buildInstr(SPIRV::OpTypeInt)
                 .addDef(createTypeVReg(MIRBuilder))
                 .addImm(Width)
                 .addImm(IsSigned ? 1 : 0);
  return MIB;
}

SPIRVType *SPIRVGlobalRegistry::getOpTypeFloat(uint32_t Width,
                                               MachineIRBuilder &MIRBuilder) {
  auto MIB = MIRBuilder.buildInstr(SPIRV::OpTypeFloat)
                 .addDef(createTypeVReg(MIRBuilder))
                 .addImm(Width);
  return MIB;
}

SPIRVType *SPIRVGlobalRegistry::getOpTypeVoid(MachineIRBuilder &MIRBuilder) {
  return MIRBuilder.buildInstr(SPIRV::OpTypeVoid)
      .addDef(createTypeVReg(MIRBuilder));
}

SPIRVType *SPIRVGlobalRegistry::getOpTypeVector(uint32_t NumElems,
                                                SPIRVType *ElemType,
                                                MachineIRBuilder &MIRBuilder) {
  auto EleOpc = ElemType->getOpcode();
  (void)EleOpc;
  assert((EleOpc == SPIRV::OpTypeInt || EleOpc == SPIRV::OpTypeFloat ||
          EleOpc == SPIRV::OpTypeBool) &&
         "Invalid vector element type");

  auto MIB = MIRBuilder.buildInstr(SPIRV::OpTypeVector)
                 .addDef(createTypeVReg(MIRBuilder))
                 .addUse(getSPIRVTypeID(ElemType))
                 .addImm(NumElems);
  return MIB;
}

std::tuple<Register, ConstantInt *, bool>
SPIRVGlobalRegistry::getOrCreateConstIntReg(uint64_t Val, SPIRVType *SpvType,
                                            MachineIRBuilder *MIRBuilder,
                                            MachineInstr *I,
                                            const SPIRVInstrInfo *TII) {
  const IntegerType *LLVMIntTy;
  if (SpvType)
    LLVMIntTy = cast<IntegerType>(getTypeForSPIRVType(SpvType));
  else
    LLVMIntTy = IntegerType::getInt32Ty(CurMF->getFunction().getContext());
  bool NewInstr = false;
  // Find a constant in DT or build a new one.
  ConstantInt *CI = ConstantInt::get(const_cast<IntegerType *>(LLVMIntTy), Val);
  Register Res = DT.find(CI, CurMF);
  if (!Res.isValid()) {
    unsigned BitWidth = SpvType ? getScalarOrVectorBitWidth(SpvType) : 32;
    // TODO: handle cases where the type is not 32bit wide
    // TODO: https://github.com/llvm/llvm-project/issues/88129
    LLT LLTy = LLT::scalar(32);
    Res = CurMF->getRegInfo().createGenericVirtualRegister(LLTy);
    CurMF->getRegInfo().setRegClass(Res, &SPIRV::IDRegClass);
    if (MIRBuilder)
      assignTypeToVReg(LLVMIntTy, Res, *MIRBuilder);
    else
      assignIntTypeToVReg(BitWidth, Res, *I, *TII);
    DT.add(CI, CurMF, Res);
    NewInstr = true;
  }
  return std::make_tuple(Res, CI, NewInstr);
}

std::tuple<Register, ConstantFP *, bool, unsigned>
SPIRVGlobalRegistry::getOrCreateConstFloatReg(APFloat Val, SPIRVType *SpvType,
                                              MachineIRBuilder *MIRBuilder,
                                              MachineInstr *I,
                                              const SPIRVInstrInfo *TII) {
  const Type *LLVMFloatTy;
  LLVMContext &Ctx = CurMF->getFunction().getContext();
  unsigned BitWidth = 32;
  if (SpvType)
    LLVMFloatTy = getTypeForSPIRVType(SpvType);
  else {
    LLVMFloatTy = Type::getFloatTy(Ctx);
    if (MIRBuilder)
      SpvType = getOrCreateSPIRVType(LLVMFloatTy, *MIRBuilder);
  }
  bool NewInstr = false;
  // Find a constant in DT or build a new one.
  auto *const CI = ConstantFP::get(Ctx, Val);
  Register Res = DT.find(CI, CurMF);
  if (!Res.isValid()) {
    if (SpvType)
      BitWidth = getScalarOrVectorBitWidth(SpvType);
    // TODO: handle cases where the type is not 32bit wide
    // TODO: https://github.com/llvm/llvm-project/issues/88129
    LLT LLTy = LLT::scalar(32);
    Res = CurMF->getRegInfo().createGenericVirtualRegister(LLTy);
    CurMF->getRegInfo().setRegClass(Res, &SPIRV::IDRegClass);
    if (MIRBuilder)
      assignTypeToVReg(LLVMFloatTy, Res, *MIRBuilder);
    else
      assignFloatTypeToVReg(BitWidth, Res, *I, *TII);
    DT.add(CI, CurMF, Res);
    NewInstr = true;
  }
  return std::make_tuple(Res, CI, NewInstr, BitWidth);
}

Register SPIRVGlobalRegistry::getOrCreateConstFP(APFloat Val, MachineInstr &I,
                                                 SPIRVType *SpvType,
                                                 const SPIRVInstrInfo &TII,
                                                 bool ZeroAsNull) {
  assert(SpvType);
  ConstantFP *CI;
  Register Res;
  bool New;
  unsigned BitWidth;
  std::tie(Res, CI, New, BitWidth) =
      getOrCreateConstFloatReg(Val, SpvType, nullptr, &I, &TII);
  // If we have found Res register which is defined by the passed G_CONSTANT
  // machine instruction, a new constant instruction should be created.
  if (!New && (!I.getOperand(0).isReg() || Res != I.getOperand(0).getReg()))
    return Res;
  MachineInstrBuilder MIB;
  MachineBasicBlock &BB = *I.getParent();
  // In OpenCL OpConstantNull - Scalar floating point: +0.0 (all bits 0)
  if (Val.isPosZero() && ZeroAsNull) {
    MIB = BuildMI(BB, I, I.getDebugLoc(), TII.get(SPIRV::OpConstantNull))
              .addDef(Res)
              .addUse(getSPIRVTypeID(SpvType));
  } else {
    MIB = BuildMI(BB, I, I.getDebugLoc(), TII.get(SPIRV::OpConstantF))
              .addDef(Res)
              .addUse(getSPIRVTypeID(SpvType));
    addNumImm(
        APInt(BitWidth, CI->getValueAPF().bitcastToAPInt().getZExtValue()),
        MIB);
  }
  const auto &ST = CurMF->getSubtarget();
  constrainSelectedInstRegOperands(*MIB, *ST.getInstrInfo(),
                                   *ST.getRegisterInfo(), *ST.getRegBankInfo());
  return Res;
}

Register SPIRVGlobalRegistry::getOrCreateConstInt(uint64_t Val, MachineInstr &I,
                                                  SPIRVType *SpvType,
                                                  const SPIRVInstrInfo &TII,
                                                  bool ZeroAsNull) {
  assert(SpvType);
  ConstantInt *CI;
  Register Res;
  bool New;
  std::tie(Res, CI, New) =
      getOrCreateConstIntReg(Val, SpvType, nullptr, &I, &TII);
  // If we have found Res register which is defined by the passed G_CONSTANT
  // machine instruction, a new constant instruction should be created.
  if (!New && (!I.getOperand(0).isReg() || Res != I.getOperand(0).getReg()))
    return Res;
  MachineInstrBuilder MIB;
  MachineBasicBlock &BB = *I.getParent();
  if (Val || !ZeroAsNull) {
    MIB = BuildMI(BB, I, I.getDebugLoc(), TII.get(SPIRV::OpConstantI))
              .addDef(Res)
              .addUse(getSPIRVTypeID(SpvType));
    addNumImm(APInt(getScalarOrVectorBitWidth(SpvType), Val), MIB);
  } else {
    MIB = BuildMI(BB, I, I.getDebugLoc(), TII.get(SPIRV::OpConstantNull))
              .addDef(Res)
              .addUse(getSPIRVTypeID(SpvType));
  }
  const auto &ST = CurMF->getSubtarget();
  constrainSelectedInstRegOperands(*MIB, *ST.getInstrInfo(),
                                   *ST.getRegisterInfo(), *ST.getRegBankInfo());
  return Res;
}

Register SPIRVGlobalRegistry::buildConstantInt(uint64_t Val,
                                               MachineIRBuilder &MIRBuilder,
                                               SPIRVType *SpvType,
                                               bool EmitIR) {
  auto &MF = MIRBuilder.getMF();
  const IntegerType *LLVMIntTy;
  if (SpvType)
    LLVMIntTy = cast<IntegerType>(getTypeForSPIRVType(SpvType));
  else
    LLVMIntTy = IntegerType::getInt32Ty(MF.getFunction().getContext());
  // Find a constant in DT or build a new one.
  const auto ConstInt =
      ConstantInt::get(const_cast<IntegerType *>(LLVMIntTy), Val);
  Register Res = DT.find(ConstInt, &MF);
  if (!Res.isValid()) {
    unsigned BitWidth = SpvType ? getScalarOrVectorBitWidth(SpvType) : 32;
    LLT LLTy = LLT::scalar(EmitIR ? BitWidth : 32);
    Res = MF.getRegInfo().createGenericVirtualRegister(LLTy);
    MF.getRegInfo().setRegClass(Res, &SPIRV::IDRegClass);
    assignTypeToVReg(LLVMIntTy, Res, MIRBuilder,
                     SPIRV::AccessQualifier::ReadWrite, EmitIR);
    DT.add(ConstInt, &MIRBuilder.getMF(), Res);
    if (EmitIR) {
      MIRBuilder.buildConstant(Res, *ConstInt);
    } else {
      if (!SpvType)
        SpvType = getOrCreateSPIRVIntegerType(BitWidth, MIRBuilder);
      MachineInstrBuilder MIB;
      if (Val) {
        MIB = MIRBuilder.buildInstr(SPIRV::OpConstantI)
                  .addDef(Res)
                  .addUse(getSPIRVTypeID(SpvType));
        addNumImm(APInt(BitWidth, Val), MIB);
      } else {
        MIB = MIRBuilder.buildInstr(SPIRV::OpConstantNull)
                  .addDef(Res)
                  .addUse(getSPIRVTypeID(SpvType));
      }
      const auto &Subtarget = CurMF->getSubtarget();
      constrainSelectedInstRegOperands(*MIB, *Subtarget.getInstrInfo(),
                                       *Subtarget.getRegisterInfo(),
                                       *Subtarget.getRegBankInfo());
    }
  }
  return Res;
}

Register SPIRVGlobalRegistry::buildConstantFP(APFloat Val,
                                              MachineIRBuilder &MIRBuilder,
                                              SPIRVType *SpvType) {
  auto &MF = MIRBuilder.getMF();
  auto &Ctx = MF.getFunction().getContext();
  if (!SpvType) {
    const Type *LLVMFPTy = Type::getFloatTy(Ctx);
    SpvType = getOrCreateSPIRVType(LLVMFPTy, MIRBuilder);
  }
  // Find a constant in DT or build a new one.
  const auto ConstFP = ConstantFP::get(Ctx, Val);
  Register Res = DT.find(ConstFP, &MF);
  if (!Res.isValid()) {
    Res = MF.getRegInfo().createGenericVirtualRegister(LLT::scalar(32));
    MF.getRegInfo().setRegClass(Res, &SPIRV::IDRegClass);
    assignSPIRVTypeToVReg(SpvType, Res, MF);
    DT.add(ConstFP, &MF, Res);

    MachineInstrBuilder MIB;
    MIB = MIRBuilder.buildInstr(SPIRV::OpConstantF)
              .addDef(Res)
              .addUse(getSPIRVTypeID(SpvType));
    addNumImm(ConstFP->getValueAPF().bitcastToAPInt(), MIB);
  }

  return Res;
}

Register SPIRVGlobalRegistry::getOrCreateBaseRegister(Constant *Val,
                                                      MachineInstr &I,
                                                      SPIRVType *SpvType,
                                                      const SPIRVInstrInfo &TII,
                                                      unsigned BitWidth) {
  SPIRVType *Type = SpvType;
  if (SpvType->getOpcode() == SPIRV::OpTypeVector ||
      SpvType->getOpcode() == SPIRV::OpTypeArray) {
    auto EleTypeReg = SpvType->getOperand(1).getReg();
    Type = getSPIRVTypeForVReg(EleTypeReg);
  }
  if (Type->getOpcode() == SPIRV::OpTypeFloat) {
    SPIRVType *SpvBaseType = getOrCreateSPIRVFloatType(BitWidth, I, TII);
    return getOrCreateConstFP(dyn_cast<ConstantFP>(Val)->getValue(), I,
                              SpvBaseType, TII);
  }
  assert(Type->getOpcode() == SPIRV::OpTypeInt);
  SPIRVType *SpvBaseType = getOrCreateSPIRVIntegerType(BitWidth, I, TII);
  return getOrCreateConstInt(Val->getUniqueInteger().getSExtValue(), I,
                             SpvBaseType, TII);
}

Register SPIRVGlobalRegistry::getOrCreateCompositeOrNull(
    Constant *Val, MachineInstr &I, SPIRVType *SpvType,
    const SPIRVInstrInfo &TII, Constant *CA, unsigned BitWidth,
    unsigned ElemCnt, bool ZeroAsNull) {
  // Find a constant vector or array in DT or build a new one.
  Register Res = DT.find(CA, CurMF);
  // If no values are attached, the composite is null constant.
  bool IsNull = Val->isNullValue() && ZeroAsNull;
  if (!Res.isValid()) {
    // SpvScalConst should be created before SpvVecConst to avoid undefined ID
    // error on validation.
    // TODO: can moved below once sorting of types/consts/defs is implemented.
    Register SpvScalConst;
    if (!IsNull)
      SpvScalConst = getOrCreateBaseRegister(Val, I, SpvType, TII, BitWidth);

    // TODO: handle cases where the type is not 32bit wide
    // TODO: https://github.com/llvm/llvm-project/issues/88129
    LLT LLTy = LLT::scalar(32);
    Register SpvVecConst =
        CurMF->getRegInfo().createGenericVirtualRegister(LLTy);
    CurMF->getRegInfo().setRegClass(SpvVecConst, &SPIRV::IDRegClass);
    assignSPIRVTypeToVReg(SpvType, SpvVecConst, *CurMF);
    DT.add(CA, CurMF, SpvVecConst);
    MachineInstrBuilder MIB;
    MachineBasicBlock &BB = *I.getParent();
    if (!IsNull) {
      MIB = BuildMI(BB, I, I.getDebugLoc(), TII.get(SPIRV::OpConstantComposite))
                .addDef(SpvVecConst)
                .addUse(getSPIRVTypeID(SpvType));
      for (unsigned i = 0; i < ElemCnt; ++i)
        MIB.addUse(SpvScalConst);
    } else {
      MIB = BuildMI(BB, I, I.getDebugLoc(), TII.get(SPIRV::OpConstantNull))
                .addDef(SpvVecConst)
                .addUse(getSPIRVTypeID(SpvType));
    }
    const auto &Subtarget = CurMF->getSubtarget();
    constrainSelectedInstRegOperands(*MIB, *Subtarget.getInstrInfo(),
                                     *Subtarget.getRegisterInfo(),
                                     *Subtarget.getRegBankInfo());
    return SpvVecConst;
  }
  return Res;
}

Register SPIRVGlobalRegistry::getOrCreateConstVector(uint64_t Val,
                                                     MachineInstr &I,
                                                     SPIRVType *SpvType,
                                                     const SPIRVInstrInfo &TII,
                                                     bool ZeroAsNull) {
  const Type *LLVMTy = getTypeForSPIRVType(SpvType);
  assert(LLVMTy->isVectorTy());
  const FixedVectorType *LLVMVecTy = cast<FixedVectorType>(LLVMTy);
  Type *LLVMBaseTy = LLVMVecTy->getElementType();
  assert(LLVMBaseTy->isIntegerTy());
  auto *ConstVal = ConstantInt::get(LLVMBaseTy, Val);
  auto *ConstVec =
      ConstantVector::getSplat(LLVMVecTy->getElementCount(), ConstVal);
  unsigned BW = getScalarOrVectorBitWidth(SpvType);
  return getOrCreateCompositeOrNull(ConstVal, I, SpvType, TII, ConstVec, BW,
                                    SpvType->getOperand(2).getImm(),
                                    ZeroAsNull);
}

Register SPIRVGlobalRegistry::getOrCreateConstVector(APFloat Val,
                                                     MachineInstr &I,
                                                     SPIRVType *SpvType,
                                                     const SPIRVInstrInfo &TII,
                                                     bool ZeroAsNull) {
  const Type *LLVMTy = getTypeForSPIRVType(SpvType);
  assert(LLVMTy->isVectorTy());
  const FixedVectorType *LLVMVecTy = cast<FixedVectorType>(LLVMTy);
  Type *LLVMBaseTy = LLVMVecTy->getElementType();
  assert(LLVMBaseTy->isFloatingPointTy());
  auto *ConstVal = ConstantFP::get(LLVMBaseTy, Val);
  auto *ConstVec =
      ConstantVector::getSplat(LLVMVecTy->getElementCount(), ConstVal);
  unsigned BW = getScalarOrVectorBitWidth(SpvType);
  return getOrCreateCompositeOrNull(ConstVal, I, SpvType, TII, ConstVec, BW,
                                    SpvType->getOperand(2).getImm(),
                                    ZeroAsNull);
}

Register SPIRVGlobalRegistry::getOrCreateConstIntArray(
    uint64_t Val, size_t Num, MachineInstr &I, SPIRVType *SpvType,
    const SPIRVInstrInfo &TII) {
  const Type *LLVMTy = getTypeForSPIRVType(SpvType);
  assert(LLVMTy->isArrayTy());
  const ArrayType *LLVMArrTy = cast<ArrayType>(LLVMTy);
  Type *LLVMBaseTy = LLVMArrTy->getElementType();
  Constant *CI = ConstantInt::get(LLVMBaseTy, Val);
  SPIRVType *SpvBaseTy = getSPIRVTypeForVReg(SpvType->getOperand(1).getReg());
  unsigned BW = getScalarOrVectorBitWidth(SpvBaseTy);
  // The following is reasonably unique key that is better that [Val]. The naive
  // alternative would be something along the lines of:
  //   SmallVector<Constant *> NumCI(Num, CI);
  //   Constant *UniqueKey =
  //     ConstantArray::get(const_cast<ArrayType*>(LLVMArrTy), NumCI);
  // that would be a truly unique but dangerous key, because it could lead to
  // the creation of constants of arbitrary length (that is, the parameter of
  // memset) which were missing in the original module.
  Constant *UniqueKey = ConstantStruct::getAnon(
      {PoisonValue::get(const_cast<ArrayType *>(LLVMArrTy)),
       ConstantInt::get(LLVMBaseTy, Val), ConstantInt::get(LLVMBaseTy, Num)});
  return getOrCreateCompositeOrNull(CI, I, SpvType, TII, UniqueKey, BW,
                                    LLVMArrTy->getNumElements());
}

Register SPIRVGlobalRegistry::getOrCreateIntCompositeOrNull(
    uint64_t Val, MachineIRBuilder &MIRBuilder, SPIRVType *SpvType, bool EmitIR,
    Constant *CA, unsigned BitWidth, unsigned ElemCnt) {
  Register Res = DT.find(CA, CurMF);
  if (!Res.isValid()) {
    Register SpvScalConst;
    if (Val || EmitIR) {
      SPIRVType *SpvBaseType =
          getOrCreateSPIRVIntegerType(BitWidth, MIRBuilder);
      SpvScalConst = buildConstantInt(Val, MIRBuilder, SpvBaseType, EmitIR);
    }
    LLT LLTy = EmitIR ? LLT::fixed_vector(ElemCnt, BitWidth) : LLT::scalar(32);
    Register SpvVecConst =
        CurMF->getRegInfo().createGenericVirtualRegister(LLTy);
    CurMF->getRegInfo().setRegClass(SpvVecConst, &SPIRV::IDRegClass);
    assignSPIRVTypeToVReg(SpvType, SpvVecConst, *CurMF);
    DT.add(CA, CurMF, SpvVecConst);
    if (EmitIR) {
      MIRBuilder.buildSplatVector(SpvVecConst, SpvScalConst);
    } else {
      if (Val) {
        auto MIB = MIRBuilder.buildInstr(SPIRV::OpConstantComposite)
                       .addDef(SpvVecConst)
                       .addUse(getSPIRVTypeID(SpvType));
        for (unsigned i = 0; i < ElemCnt; ++i)
          MIB.addUse(SpvScalConst);
      } else {
        MIRBuilder.buildInstr(SPIRV::OpConstantNull)
            .addDef(SpvVecConst)
            .addUse(getSPIRVTypeID(SpvType));
      }
    }
    return SpvVecConst;
  }
  return Res;
}

Register
SPIRVGlobalRegistry::getOrCreateConsIntVector(uint64_t Val,
                                              MachineIRBuilder &MIRBuilder,
                                              SPIRVType *SpvType, bool EmitIR) {
  const Type *LLVMTy = getTypeForSPIRVType(SpvType);
  assert(LLVMTy->isVectorTy());
  const FixedVectorType *LLVMVecTy = cast<FixedVectorType>(LLVMTy);
  Type *LLVMBaseTy = LLVMVecTy->getElementType();
  const auto ConstInt = ConstantInt::get(LLVMBaseTy, Val);
  auto ConstVec =
      ConstantVector::getSplat(LLVMVecTy->getElementCount(), ConstInt);
  unsigned BW = getScalarOrVectorBitWidth(SpvType);
  return getOrCreateIntCompositeOrNull(Val, MIRBuilder, SpvType, EmitIR,
                                       ConstVec, BW,
                                       SpvType->getOperand(2).getImm());
}

Register
SPIRVGlobalRegistry::getOrCreateConstNullPtr(MachineIRBuilder &MIRBuilder,
                                             SPIRVType *SpvType) {
  const Type *LLVMTy = getTypeForSPIRVType(SpvType);
  const TypedPointerType *LLVMPtrTy = cast<TypedPointerType>(LLVMTy);
  // Find a constant in DT or build a new one.
  Constant *CP = ConstantPointerNull::get(PointerType::get(
      LLVMPtrTy->getElementType(), LLVMPtrTy->getAddressSpace()));
  Register Res = DT.find(CP, CurMF);
  if (!Res.isValid()) {
    LLT LLTy = LLT::pointer(LLVMPtrTy->getAddressSpace(), PointerSize);
    Res = CurMF->getRegInfo().createGenericVirtualRegister(LLTy);
    CurMF->getRegInfo().setRegClass(Res, &SPIRV::IDRegClass);
    assignSPIRVTypeToVReg(SpvType, Res, *CurMF);
    MIRBuilder.buildInstr(SPIRV::OpConstantNull)
        .addDef(Res)
        .addUse(getSPIRVTypeID(SpvType));
    DT.add(CP, CurMF, Res);
  }
  return Res;
}

Register SPIRVGlobalRegistry::buildConstantSampler(
    Register ResReg, unsigned AddrMode, unsigned Param, unsigned FilerMode,
    MachineIRBuilder &MIRBuilder, SPIRVType *SpvType) {
  SPIRVType *SampTy;
  if (SpvType)
    SampTy = getOrCreateSPIRVType(getTypeForSPIRVType(SpvType), MIRBuilder);
  else if ((SampTy = getOrCreateSPIRVTypeByName("opencl.sampler_t",
                                                MIRBuilder)) == nullptr)
    report_fatal_error("Unable to recognize SPIRV type name: opencl.sampler_t");

  auto Sampler =
      ResReg.isValid()
          ? ResReg
          : MIRBuilder.getMRI()->createVirtualRegister(&SPIRV::IDRegClass);
  auto Res = MIRBuilder.buildInstr(SPIRV::OpConstantSampler)
                 .addDef(Sampler)
                 .addUse(getSPIRVTypeID(SampTy))
                 .addImm(AddrMode)
                 .addImm(Param)
                 .addImm(FilerMode);
  assert(Res->getOperand(0).isReg());
  return Res->getOperand(0).getReg();
}

Register SPIRVGlobalRegistry::buildGlobalVariable(
    Register ResVReg, SPIRVType *BaseType, StringRef Name,
    const GlobalValue *GV, SPIRV::StorageClass::StorageClass Storage,
    const MachineInstr *Init, bool IsConst, bool HasLinkageTy,
    SPIRV::LinkageType::LinkageType LinkageType, MachineIRBuilder &MIRBuilder,
    bool IsInstSelector) {
  const GlobalVariable *GVar = nullptr;
  if (GV)
    GVar = cast<const GlobalVariable>(GV);
  else {
    // If GV is not passed explicitly, use the name to find or construct
    // the global variable.
    Module *M = MIRBuilder.getMF().getFunction().getParent();
    GVar = M->getGlobalVariable(Name);
    if (GVar == nullptr) {
      const Type *Ty = getTypeForSPIRVType(BaseType); // TODO: check type.
      // Module takes ownership of the global var.
      GVar = new GlobalVariable(*M, const_cast<Type *>(Ty), false,
                                GlobalValue::ExternalLinkage, nullptr,
                                Twine(Name));
    }
    GV = GVar;
  }
  Register Reg = DT.find(GVar, &MIRBuilder.getMF());
  if (Reg.isValid()) {
    if (Reg != ResVReg)
      MIRBuilder.buildCopy(ResVReg, Reg);
    return ResVReg;
  }

  auto MIB = MIRBuilder.buildInstr(SPIRV::OpVariable)
                 .addDef(ResVReg)
                 .addUse(getSPIRVTypeID(BaseType))
                 .addImm(static_cast<uint32_t>(Storage));

  if (Init != 0) {
    MIB.addUse(Init->getOperand(0).getReg());
  }

  // ISel may introduce a new register on this step, so we need to add it to
  // DT and correct its type avoiding fails on the next stage.
  if (IsInstSelector) {
    const auto &Subtarget = CurMF->getSubtarget();
    constrainSelectedInstRegOperands(*MIB, *Subtarget.getInstrInfo(),
                                     *Subtarget.getRegisterInfo(),
                                     *Subtarget.getRegBankInfo());
  }
  Reg = MIB->getOperand(0).getReg();
  DT.add(GVar, &MIRBuilder.getMF(), Reg);

  // Set to Reg the same type as ResVReg has.
  auto MRI = MIRBuilder.getMRI();
  assert(MRI->getType(ResVReg).isPointer() && "Pointer type is expected");
  if (Reg != ResVReg) {
    LLT RegLLTy =
        LLT::pointer(MRI->getType(ResVReg).getAddressSpace(), getPointerSize());
    MRI->setType(Reg, RegLLTy);
    assignSPIRVTypeToVReg(BaseType, Reg, MIRBuilder.getMF());
  } else {
    // Our knowledge about the type may be updated.
    // If that's the case, we need to update a type
    // associated with the register.
    SPIRVType *DefType = getSPIRVTypeForVReg(ResVReg);
    if (!DefType || DefType != BaseType)
      assignSPIRVTypeToVReg(BaseType, Reg, MIRBuilder.getMF());
  }

  // If it's a global variable with name, output OpName for it.
  if (GVar && GVar->hasName())
    buildOpName(Reg, GVar->getName(), MIRBuilder);

  // Output decorations for the GV.
  // TODO: maybe move to GenerateDecorations pass.
  const SPIRVSubtarget &ST =
      cast<SPIRVSubtarget>(MIRBuilder.getMF().getSubtarget());
  if (IsConst && ST.isOpenCLEnv())
    buildOpDecorate(Reg, MIRBuilder, SPIRV::Decoration::Constant, {});

  if (GVar && GVar->getAlign().valueOrOne().value() != 1) {
    unsigned Alignment = (unsigned)GVar->getAlign().valueOrOne().value();
    buildOpDecorate(Reg, MIRBuilder, SPIRV::Decoration::Alignment, {Alignment});
  }

  if (HasLinkageTy)
    buildOpDecorate(Reg, MIRBuilder, SPIRV::Decoration::LinkageAttributes,
                    {static_cast<uint32_t>(LinkageType)}, Name);

  SPIRV::BuiltIn::BuiltIn BuiltInId;
  if (getSpirvBuiltInIdByName(Name, BuiltInId))
    buildOpDecorate(Reg, MIRBuilder, SPIRV::Decoration::BuiltIn,
                    {static_cast<uint32_t>(BuiltInId)});

  // If it's a global variable with "spirv.Decorations" metadata node
  // recognize it as a SPIR-V friendly LLVM IR and parse "spirv.Decorations"
  // arguments.
  MDNode *GVarMD = nullptr;
  if (GVar && (GVarMD = GVar->getMetadata("spirv.Decorations")) != nullptr)
    buildOpSpirvDecorations(Reg, MIRBuilder, GVarMD);

  return Reg;
}

SPIRVType *SPIRVGlobalRegistry::getOpTypeArray(uint32_t NumElems,
                                               SPIRVType *ElemType,
                                               MachineIRBuilder &MIRBuilder,
                                               bool EmitIR) {
  assert((ElemType->getOpcode() != SPIRV::OpTypeVoid) &&
         "Invalid array element type");
  Register NumElementsVReg =
      buildConstantInt(NumElems, MIRBuilder, nullptr, EmitIR);
  auto MIB = MIRBuilder.buildInstr(SPIRV::OpTypeArray)
                 .addDef(createTypeVReg(MIRBuilder))
                 .addUse(getSPIRVTypeID(ElemType))
                 .addUse(NumElementsVReg);
  return MIB;
}

SPIRVType *SPIRVGlobalRegistry::getOpTypeOpaque(const StructType *Ty,
                                                MachineIRBuilder &MIRBuilder) {
  assert(Ty->hasName());
  const StringRef Name = Ty->hasName() ? Ty->getName() : "";
  Register ResVReg = createTypeVReg(MIRBuilder);
  auto MIB = MIRBuilder.buildInstr(SPIRV::OpTypeOpaque).addDef(ResVReg);
  addStringImm(Name, MIB);
  buildOpName(ResVReg, Name, MIRBuilder);
  return MIB;
}

SPIRVType *SPIRVGlobalRegistry::getOpTypeStruct(const StructType *Ty,
                                                MachineIRBuilder &MIRBuilder,
                                                bool EmitIR) {
  SmallVector<Register, 4> FieldTypes;
  for (const auto &Elem : Ty->elements()) {
    SPIRVType *ElemTy = findSPIRVType(toTypedPointer(Elem), MIRBuilder);
    assert(ElemTy && ElemTy->getOpcode() != SPIRV::OpTypeVoid &&
           "Invalid struct element type");
    FieldTypes.push_back(getSPIRVTypeID(ElemTy));
  }
  Register ResVReg = createTypeVReg(MIRBuilder);
  auto MIB = MIRBuilder.buildInstr(SPIRV::OpTypeStruct).addDef(ResVReg);
  for (const auto &Ty : FieldTypes)
    MIB.addUse(Ty);
  if (Ty->hasName())
    buildOpName(ResVReg, Ty->getName(), MIRBuilder);
  if (Ty->isPacked())
    buildOpDecorate(ResVReg, MIRBuilder, SPIRV::Decoration::CPacked, {});
  return MIB;
}

SPIRVType *SPIRVGlobalRegistry::getOrCreateSpecialType(
    const Type *Ty, MachineIRBuilder &MIRBuilder,
    SPIRV::AccessQualifier::AccessQualifier AccQual) {
  assert(isSpecialOpaqueType(Ty) && "Not a special opaque builtin type");
  return SPIRV::lowerBuiltinType(Ty, AccQual, MIRBuilder, this);
}

SPIRVType *SPIRVGlobalRegistry::getOpTypePointer(
    SPIRV::StorageClass::StorageClass SC, SPIRVType *ElemType,
    MachineIRBuilder &MIRBuilder, Register Reg) {
  if (!Reg.isValid())
    Reg = createTypeVReg(MIRBuilder);
  return MIRBuilder.buildInstr(SPIRV::OpTypePointer)
      .addDef(Reg)
      .addImm(static_cast<uint32_t>(SC))
      .addUse(getSPIRVTypeID(ElemType));
}

SPIRVType *SPIRVGlobalRegistry::getOpTypeForwardPointer(
    SPIRV::StorageClass::StorageClass SC, MachineIRBuilder &MIRBuilder) {
  return MIRBuilder.buildInstr(SPIRV::OpTypeForwardPointer)
      .addUse(createTypeVReg(MIRBuilder))
      .addImm(static_cast<uint32_t>(SC));
}

SPIRVType *SPIRVGlobalRegistry::getOpTypeFunction(
    SPIRVType *RetType, const SmallVectorImpl<SPIRVType *> &ArgTypes,
    MachineIRBuilder &MIRBuilder) {
  auto MIB = MIRBuilder.buildInstr(SPIRV::OpTypeFunction)
                 .addDef(createTypeVReg(MIRBuilder))
                 .addUse(getSPIRVTypeID(RetType));
  for (const SPIRVType *ArgType : ArgTypes)
    MIB.addUse(getSPIRVTypeID(ArgType));
  return MIB;
}

SPIRVType *SPIRVGlobalRegistry::getOrCreateOpTypeFunctionWithArgs(
    const Type *Ty, SPIRVType *RetType,
    const SmallVectorImpl<SPIRVType *> &ArgTypes,
    MachineIRBuilder &MIRBuilder) {
  Register Reg = DT.find(Ty, &MIRBuilder.getMF());
  if (Reg.isValid())
    return getSPIRVTypeForVReg(Reg);
  SPIRVType *SpirvType = getOpTypeFunction(RetType, ArgTypes, MIRBuilder);
  DT.add(Ty, CurMF, getSPIRVTypeID(SpirvType));
  return finishCreatingSPIRVType(Ty, SpirvType);
}

SPIRVType *SPIRVGlobalRegistry::findSPIRVType(
    const Type *Ty, MachineIRBuilder &MIRBuilder,
    SPIRV::AccessQualifier::AccessQualifier AccQual, bool EmitIR) {
  Ty = adjustIntTypeByWidth(Ty);
  Register Reg = DT.find(Ty, &MIRBuilder.getMF());
  if (Reg.isValid())
    return getSPIRVTypeForVReg(Reg);
  if (ForwardPointerTypes.contains(Ty))
    return ForwardPointerTypes[Ty];
  return restOfCreateSPIRVType(Ty, MIRBuilder, AccQual, EmitIR);
}

Register SPIRVGlobalRegistry::getSPIRVTypeID(const SPIRVType *SpirvType) const {
  assert(SpirvType && "Attempting to get type id for nullptr type.");
  if (SpirvType->getOpcode() == SPIRV::OpTypeForwardPointer)
    return SpirvType->uses().begin()->getReg();
  return SpirvType->defs().begin()->getReg();
}

// We need to use a new LLVM integer type if there is a mismatch between
// number of bits in LLVM and SPIRV integer types to let DuplicateTracker
// ensure uniqueness of a SPIRV type by the corresponding LLVM type. Without
// such an adjustment SPIRVGlobalRegistry::getOpTypeInt() could create the
// same "OpTypeInt 8" type for a series of LLVM integer types with number of
// bits less than 8. This would lead to duplicate type definitions
// eventually due to the method that DuplicateTracker utilizes to reason
// about uniqueness of type records.
const Type *SPIRVGlobalRegistry::adjustIntTypeByWidth(const Type *Ty) const {
  if (auto IType = dyn_cast<IntegerType>(Ty)) {
    unsigned SrcBitWidth = IType->getBitWidth();
    if (SrcBitWidth > 1) {
      unsigned BitWidth = adjustOpTypeIntWidth(SrcBitWidth);
      // Maybe change source LLVM type to keep DuplicateTracker consistent.
      if (SrcBitWidth != BitWidth)
        Ty = IntegerType::get(Ty->getContext(), BitWidth);
    }
  }
  return Ty;
}

SPIRVType *SPIRVGlobalRegistry::createSPIRVType(
    const Type *Ty, MachineIRBuilder &MIRBuilder,
    SPIRV::AccessQualifier::AccessQualifier AccQual, bool EmitIR) {
  if (isSpecialOpaqueType(Ty))
    return getOrCreateSpecialType(Ty, MIRBuilder, AccQual);
  auto &TypeToSPIRVTypeMap = DT.getTypes()->getAllUses();
  auto t = TypeToSPIRVTypeMap.find(Ty);
  if (t != TypeToSPIRVTypeMap.end()) {
    auto tt = t->second.find(&MIRBuilder.getMF());
    if (tt != t->second.end())
      return getSPIRVTypeForVReg(tt->second);
  }

  if (auto IType = dyn_cast<IntegerType>(Ty)) {
    const unsigned Width = IType->getBitWidth();
    return Width == 1 ? getOpTypeBool(MIRBuilder)
                      : getOpTypeInt(Width, MIRBuilder, false);
  }
  if (Ty->isFloatingPointTy())
    return getOpTypeFloat(Ty->getPrimitiveSizeInBits(), MIRBuilder);
  if (Ty->isVoidTy())
    return getOpTypeVoid(MIRBuilder);
  if (Ty->isVectorTy()) {
    SPIRVType *El =
        findSPIRVType(cast<FixedVectorType>(Ty)->getElementType(), MIRBuilder);
    return getOpTypeVector(cast<FixedVectorType>(Ty)->getNumElements(), El,
                           MIRBuilder);
  }
  if (Ty->isArrayTy()) {
    SPIRVType *El = findSPIRVType(Ty->getArrayElementType(), MIRBuilder);
    return getOpTypeArray(Ty->getArrayNumElements(), El, MIRBuilder, EmitIR);
  }
  if (auto SType = dyn_cast<StructType>(Ty)) {
    if (SType->isOpaque())
      return getOpTypeOpaque(SType, MIRBuilder);
    return getOpTypeStruct(SType, MIRBuilder, EmitIR);
  }
  if (auto FType = dyn_cast<FunctionType>(Ty)) {
    SPIRVType *RetTy = findSPIRVType(FType->getReturnType(), MIRBuilder);
    SmallVector<SPIRVType *, 4> ParamTypes;
    for (const auto &t : FType->params()) {
      ParamTypes.push_back(findSPIRVType(t, MIRBuilder));
    }
    return getOpTypeFunction(RetTy, ParamTypes, MIRBuilder);
  }
  unsigned AddrSpace = 0xFFFF;
  if (auto PType = dyn_cast<TypedPointerType>(Ty))
    AddrSpace = PType->getAddressSpace();
  else if (auto PType = dyn_cast<PointerType>(Ty))
    AddrSpace = PType->getAddressSpace();
  else
    report_fatal_error("Unable to convert LLVM type to SPIRVType", true);

  SPIRVType *SpvElementType = nullptr;
  if (auto PType = dyn_cast<TypedPointerType>(Ty))
    SpvElementType = getOrCreateSPIRVType(PType->getElementType(), MIRBuilder,
                                          AccQual, EmitIR);
  else
    SpvElementType = getOrCreateSPIRVIntegerType(8, MIRBuilder);

  // Get access to information about available extensions
  const SPIRVSubtarget *ST =
      static_cast<const SPIRVSubtarget *>(&MIRBuilder.getMF().getSubtarget());
  auto SC = addressSpaceToStorageClass(AddrSpace, *ST);
  // Null pointer means we have a loop in type definitions, make and
  // return corresponding OpTypeForwardPointer.
  if (SpvElementType == nullptr) {
    if (!ForwardPointerTypes.contains(Ty))
      ForwardPointerTypes[Ty] = getOpTypeForwardPointer(SC, MIRBuilder);
    return ForwardPointerTypes[Ty];
  }
  // If we have forward pointer associated with this type, use its register
  // operand to create OpTypePointer.
  if (ForwardPointerTypes.contains(Ty)) {
    Register Reg = getSPIRVTypeID(ForwardPointerTypes[Ty]);
    return getOpTypePointer(SC, SpvElementType, MIRBuilder, Reg);
  }

  return getOrCreateSPIRVPointerType(SpvElementType, MIRBuilder, SC);
}

SPIRVType *SPIRVGlobalRegistry::restOfCreateSPIRVType(
    const Type *Ty, MachineIRBuilder &MIRBuilder,
    SPIRV::AccessQualifier::AccessQualifier AccessQual, bool EmitIR) {
  if (TypesInProcessing.count(Ty) && !isPointerTy(Ty))
    return nullptr;
  TypesInProcessing.insert(Ty);
  SPIRVType *SpirvType = createSPIRVType(Ty, MIRBuilder, AccessQual, EmitIR);
  TypesInProcessing.erase(Ty);
  VRegToTypeMap[&MIRBuilder.getMF()][getSPIRVTypeID(SpirvType)] = SpirvType;
  SPIRVToLLVMType[SpirvType] = unifyPtrType(Ty);
  Register Reg = DT.find(Ty, &MIRBuilder.getMF());
  // Do not add OpTypeForwardPointer to DT, a corresponding normal pointer type
  // will be added later. For special types it is already added to DT.
  if (SpirvType->getOpcode() != SPIRV::OpTypeForwardPointer && !Reg.isValid() &&
      !isSpecialOpaqueType(Ty)) {
    if (!isPointerTy(Ty))
      DT.add(Ty, &MIRBuilder.getMF(), getSPIRVTypeID(SpirvType));
    else if (isTypedPointerTy(Ty))
      DT.add(cast<TypedPointerType>(Ty)->getElementType(),
             getPointerAddressSpace(Ty), &MIRBuilder.getMF(),
             getSPIRVTypeID(SpirvType));
    else
      DT.add(Type::getInt8Ty(MIRBuilder.getMF().getFunction().getContext()),
             getPointerAddressSpace(Ty), &MIRBuilder.getMF(),
             getSPIRVTypeID(SpirvType));
  }

  return SpirvType;
}

SPIRVType *
SPIRVGlobalRegistry::getSPIRVTypeForVReg(Register VReg,
                                         const MachineFunction *MF) const {
  auto t = VRegToTypeMap.find(MF ? MF : CurMF);
  if (t != VRegToTypeMap.end()) {
    auto tt = t->second.find(VReg);
    if (tt != t->second.end())
      return tt->second;
  }
  return nullptr;
}

SPIRVType *SPIRVGlobalRegistry::getOrCreateSPIRVType(
    const Type *Ty, MachineIRBuilder &MIRBuilder,
    SPIRV::AccessQualifier::AccessQualifier AccessQual, bool EmitIR) {
  Register Reg;
  if (!isPointerTy(Ty)) {
    Ty = adjustIntTypeByWidth(Ty);
    Reg = DT.find(Ty, &MIRBuilder.getMF());
  } else if (isTypedPointerTy(Ty)) {
    Reg = DT.find(cast<TypedPointerType>(Ty)->getElementType(),
                  getPointerAddressSpace(Ty), &MIRBuilder.getMF());
  } else {
    Reg =
        DT.find(Type::getInt8Ty(MIRBuilder.getMF().getFunction().getContext()),
                getPointerAddressSpace(Ty), &MIRBuilder.getMF());
  }

  if (Reg.isValid() && !isSpecialOpaqueType(Ty))
    return getSPIRVTypeForVReg(Reg);
  TypesInProcessing.clear();
  SPIRVType *STy = restOfCreateSPIRVType(Ty, MIRBuilder, AccessQual, EmitIR);
  // Create normal pointer types for the corresponding OpTypeForwardPointers.
  for (auto &CU : ForwardPointerTypes) {
    const Type *Ty2 = CU.first;
    SPIRVType *STy2 = CU.second;
    if ((Reg = DT.find(Ty2, &MIRBuilder.getMF())).isValid())
      STy2 = getSPIRVTypeForVReg(Reg);
    else
      STy2 = restOfCreateSPIRVType(Ty2, MIRBuilder, AccessQual, EmitIR);
    if (Ty == Ty2)
      STy = STy2;
  }
  ForwardPointerTypes.clear();
  return STy;
}

bool SPIRVGlobalRegistry::isScalarOfType(Register VReg,
                                         unsigned TypeOpcode) const {
  SPIRVType *Type = getSPIRVTypeForVReg(VReg);
  assert(Type && "isScalarOfType VReg has no type assigned");
  return Type->getOpcode() == TypeOpcode;
}

bool SPIRVGlobalRegistry::isScalarOrVectorOfType(Register VReg,
                                                 unsigned TypeOpcode) const {
  SPIRVType *Type = getSPIRVTypeForVReg(VReg);
  assert(Type && "isScalarOrVectorOfType VReg has no type assigned");
  if (Type->getOpcode() == TypeOpcode)
    return true;
  if (Type->getOpcode() == SPIRV::OpTypeVector) {
    Register ScalarTypeVReg = Type->getOperand(1).getReg();
    SPIRVType *ScalarType = getSPIRVTypeForVReg(ScalarTypeVReg);
    return ScalarType->getOpcode() == TypeOpcode;
  }
  return false;
}

unsigned
SPIRVGlobalRegistry::getScalarOrVectorComponentCount(Register VReg) const {
  return getScalarOrVectorComponentCount(getSPIRVTypeForVReg(VReg));
}

unsigned
SPIRVGlobalRegistry::getScalarOrVectorComponentCount(SPIRVType *Type) const {
  if (!Type)
    return 0;
  return Type->getOpcode() == SPIRV::OpTypeVector
             ? static_cast<unsigned>(Type->getOperand(2).getImm())
             : 1;
}

unsigned
SPIRVGlobalRegistry::getScalarOrVectorBitWidth(const SPIRVType *Type) const {
  assert(Type && "Invalid Type pointer");
  if (Type->getOpcode() == SPIRV::OpTypeVector) {
    auto EleTypeReg = Type->getOperand(1).getReg();
    Type = getSPIRVTypeForVReg(EleTypeReg);
  }
  if (Type->getOpcode() == SPIRV::OpTypeInt ||
      Type->getOpcode() == SPIRV::OpTypeFloat)
    return Type->getOperand(1).getImm();
  if (Type->getOpcode() == SPIRV::OpTypeBool)
    return 1;
  llvm_unreachable("Attempting to get bit width of non-integer/float type.");
}

unsigned SPIRVGlobalRegistry::getNumScalarOrVectorTotalBitWidth(
    const SPIRVType *Type) const {
  assert(Type && "Invalid Type pointer");
  unsigned NumElements = 1;
  if (Type->getOpcode() == SPIRV::OpTypeVector) {
    NumElements = static_cast<unsigned>(Type->getOperand(2).getImm());
    Type = getSPIRVTypeForVReg(Type->getOperand(1).getReg());
  }
  return Type->getOpcode() == SPIRV::OpTypeInt ||
                 Type->getOpcode() == SPIRV::OpTypeFloat
             ? NumElements * Type->getOperand(1).getImm()
             : 0;
}

const SPIRVType *SPIRVGlobalRegistry::retrieveScalarOrVectorIntType(
    const SPIRVType *Type) const {
  if (Type && Type->getOpcode() == SPIRV::OpTypeVector)
    Type = getSPIRVTypeForVReg(Type->getOperand(1).getReg());
  return Type && Type->getOpcode() == SPIRV::OpTypeInt ? Type : nullptr;
}

bool SPIRVGlobalRegistry::isScalarOrVectorSigned(const SPIRVType *Type) const {
  const SPIRVType *IntType = retrieveScalarOrVectorIntType(Type);
  return IntType && IntType->getOperand(2).getImm() != 0;
}

SPIRVType *SPIRVGlobalRegistry::getPointeeType(SPIRVType *PtrType) {
  return PtrType && PtrType->getOpcode() == SPIRV::OpTypePointer
             ? getSPIRVTypeForVReg(PtrType->getOperand(2).getReg())
             : nullptr;
}

unsigned SPIRVGlobalRegistry::getPointeeTypeOp(Register PtrReg) {
  SPIRVType *ElemType = getPointeeType(getSPIRVTypeForVReg(PtrReg));
  return ElemType ? ElemType->getOpcode() : 0;
}

bool SPIRVGlobalRegistry::isBitcastCompatible(const SPIRVType *Type1,
                                              const SPIRVType *Type2) const {
  if (!Type1 || !Type2)
    return false;
  auto Op1 = Type1->getOpcode(), Op2 = Type2->getOpcode();
  // Ignore difference between <1.5 and >=1.5 protocol versions:
  // it's valid if either Result Type or Operand is a pointer, and the other
  // is a pointer, an integer scalar, or an integer vector.
  if (Op1 == SPIRV::OpTypePointer &&
      (Op2 == SPIRV::OpTypePointer || retrieveScalarOrVectorIntType(Type2)))
    return true;
  if (Op2 == SPIRV::OpTypePointer &&
      (Op1 == SPIRV::OpTypePointer || retrieveScalarOrVectorIntType(Type1)))
    return true;
  unsigned Bits1 = getNumScalarOrVectorTotalBitWidth(Type1),
           Bits2 = getNumScalarOrVectorTotalBitWidth(Type2);
  return Bits1 > 0 && Bits1 == Bits2;
}

SPIRV::StorageClass::StorageClass
SPIRVGlobalRegistry::getPointerStorageClass(Register VReg) const {
  SPIRVType *Type = getSPIRVTypeForVReg(VReg);
  assert(Type && Type->getOpcode() == SPIRV::OpTypePointer &&
         Type->getOperand(1).isImm() && "Pointer type is expected");
  return static_cast<SPIRV::StorageClass::StorageClass>(
      Type->getOperand(1).getImm());
}

SPIRVType *SPIRVGlobalRegistry::getOrCreateOpTypeImage(
    MachineIRBuilder &MIRBuilder, SPIRVType *SampledType, SPIRV::Dim::Dim Dim,
    uint32_t Depth, uint32_t Arrayed, uint32_t Multisampled, uint32_t Sampled,
    SPIRV::ImageFormat::ImageFormat ImageFormat,
    SPIRV::AccessQualifier::AccessQualifier AccessQual) {
  auto TD = SPIRV::make_descr_image(SPIRVToLLVMType.lookup(SampledType), Dim,
                                    Depth, Arrayed, Multisampled, Sampled,
                                    ImageFormat, AccessQual);
  if (auto *Res = checkSpecialInstr(TD, MIRBuilder))
    return Res;
  Register ResVReg = createTypeVReg(MIRBuilder);
  DT.add(TD, &MIRBuilder.getMF(), ResVReg);
  return MIRBuilder.buildInstr(SPIRV::OpTypeImage)
      .addDef(ResVReg)
      .addUse(getSPIRVTypeID(SampledType))
      .addImm(Dim)
      .addImm(Depth)        // Depth (whether or not it is a Depth image).
      .addImm(Arrayed)      // Arrayed.
      .addImm(Multisampled) // Multisampled (0 = only single-sample).
      .addImm(Sampled)      // Sampled (0 = usage known at runtime).
      .addImm(ImageFormat)
      .addImm(AccessQual);
}

SPIRVType *
SPIRVGlobalRegistry::getOrCreateOpTypeSampler(MachineIRBuilder &MIRBuilder) {
  auto TD = SPIRV::make_descr_sampler();
  if (auto *Res = checkSpecialInstr(TD, MIRBuilder))
    return Res;
  Register ResVReg = createTypeVReg(MIRBuilder);
  DT.add(TD, &MIRBuilder.getMF(), ResVReg);
  return MIRBuilder.buildInstr(SPIRV::OpTypeSampler).addDef(ResVReg);
}

SPIRVType *SPIRVGlobalRegistry::getOrCreateOpTypePipe(
    MachineIRBuilder &MIRBuilder,
    SPIRV::AccessQualifier::AccessQualifier AccessQual) {
  auto TD = SPIRV::make_descr_pipe(AccessQual);
  if (auto *Res = checkSpecialInstr(TD, MIRBuilder))
    return Res;
  Register ResVReg = createTypeVReg(MIRBuilder);
  DT.add(TD, &MIRBuilder.getMF(), ResVReg);
  return MIRBuilder.buildInstr(SPIRV::OpTypePipe)
      .addDef(ResVReg)
      .addImm(AccessQual);
}

SPIRVType *SPIRVGlobalRegistry::getOrCreateOpTypeDeviceEvent(
    MachineIRBuilder &MIRBuilder) {
  auto TD = SPIRV::make_descr_event();
  if (auto *Res = checkSpecialInstr(TD, MIRBuilder))
    return Res;
  Register ResVReg = createTypeVReg(MIRBuilder);
  DT.add(TD, &MIRBuilder.getMF(), ResVReg);
  return MIRBuilder.buildInstr(SPIRV::OpTypeDeviceEvent).addDef(ResVReg);
}

SPIRVType *SPIRVGlobalRegistry::getOrCreateOpTypeSampledImage(
    SPIRVType *ImageType, MachineIRBuilder &MIRBuilder) {
  auto TD = SPIRV::make_descr_sampled_image(
      SPIRVToLLVMType.lookup(MIRBuilder.getMF().getRegInfo().getVRegDef(
          ImageType->getOperand(1).getReg())),
      ImageType);
  if (auto *Res = checkSpecialInstr(TD, MIRBuilder))
    return Res;
  Register ResVReg = createTypeVReg(MIRBuilder);
  DT.add(TD, &MIRBuilder.getMF(), ResVReg);
  return MIRBuilder.buildInstr(SPIRV::OpTypeSampledImage)
      .addDef(ResVReg)
      .addUse(getSPIRVTypeID(ImageType));
}

SPIRVType *SPIRVGlobalRegistry::getOrCreateOpTypeCoopMatr(
    MachineIRBuilder &MIRBuilder, const TargetExtType *ExtensionType,
    const SPIRVType *ElemType, uint32_t Scope, uint32_t Rows, uint32_t Columns,
    uint32_t Use) {
  Register ResVReg = DT.find(ExtensionType, &MIRBuilder.getMF());
  if (ResVReg.isValid())
    return MIRBuilder.getMF().getRegInfo().getUniqueVRegDef(ResVReg);
  ResVReg = createTypeVReg(MIRBuilder);
  SPIRVType *SpirvTy =
      MIRBuilder.buildInstr(SPIRV::OpTypeCooperativeMatrixKHR)
          .addDef(ResVReg)
          .addUse(getSPIRVTypeID(ElemType))
          .addUse(buildConstantInt(Scope, MIRBuilder, nullptr, true))
          .addUse(buildConstantInt(Rows, MIRBuilder, nullptr, true))
          .addUse(buildConstantInt(Columns, MIRBuilder, nullptr, true))
          .addUse(buildConstantInt(Use, MIRBuilder, nullptr, true));
  DT.add(ExtensionType, &MIRBuilder.getMF(), ResVReg);
  return SpirvTy;
}

SPIRVType *SPIRVGlobalRegistry::getOrCreateOpTypeByOpcode(
    const Type *Ty, MachineIRBuilder &MIRBuilder, unsigned Opcode) {
  Register ResVReg = DT.find(Ty, &MIRBuilder.getMF());
  if (ResVReg.isValid())
    return MIRBuilder.getMF().getRegInfo().getUniqueVRegDef(ResVReg);
  ResVReg = createTypeVReg(MIRBuilder);
  SPIRVType *SpirvTy = MIRBuilder.buildInstr(Opcode).addDef(ResVReg);
  DT.add(Ty, &MIRBuilder.getMF(), ResVReg);
  return SpirvTy;
}

const MachineInstr *
SPIRVGlobalRegistry::checkSpecialInstr(const SPIRV::SpecialTypeDescriptor &TD,
                                       MachineIRBuilder &MIRBuilder) {
  Register Reg = DT.find(TD, &MIRBuilder.getMF());
  if (Reg.isValid())
    return MIRBuilder.getMF().getRegInfo().getUniqueVRegDef(Reg);
  return nullptr;
}

// Returns nullptr if unable to recognize SPIRV type name
SPIRVType *SPIRVGlobalRegistry::getOrCreateSPIRVTypeByName(
    StringRef TypeStr, MachineIRBuilder &MIRBuilder,
    SPIRV::StorageClass::StorageClass SC,
    SPIRV::AccessQualifier::AccessQualifier AQ) {
  unsigned VecElts = 0;
  auto &Ctx = MIRBuilder.getMF().getFunction().getContext();

  // Parse strings representing either a SPIR-V or OpenCL builtin type.
  if (hasBuiltinTypePrefix(TypeStr))
    return getOrCreateSPIRVType(SPIRV::parseBuiltinTypeNameToTargetExtType(
                                    TypeStr.str(), MIRBuilder.getContext()),
                                MIRBuilder, AQ);

  // Parse type name in either "typeN" or "type vector[N]" format, where
  // N is the number of elements of the vector.
  Type *Ty;

  Ty = parseBasicTypeName(TypeStr, Ctx);
  if (!Ty)
    // Unable to recognize SPIRV type name
    return nullptr;

  auto SpirvTy = getOrCreateSPIRVType(Ty, MIRBuilder, AQ);

  // Handle "type*" or  "type* vector[N]".
  if (TypeStr.starts_with("*")) {
    SpirvTy = getOrCreateSPIRVPointerType(SpirvTy, MIRBuilder, SC);
    TypeStr = TypeStr.substr(strlen("*"));
  }

  // Handle "typeN*" or  "type vector[N]*".
  bool IsPtrToVec = TypeStr.consume_back("*");

  if (TypeStr.consume_front(" vector[")) {
    TypeStr = TypeStr.substr(0, TypeStr.find(']'));
  }
  TypeStr.getAsInteger(10, VecElts);
  if (VecElts > 0)
    SpirvTy = getOrCreateSPIRVVectorType(SpirvTy, VecElts, MIRBuilder);

  if (IsPtrToVec)
    SpirvTy = getOrCreateSPIRVPointerType(SpirvTy, MIRBuilder, SC);

  return SpirvTy;
}

SPIRVType *
SPIRVGlobalRegistry::getOrCreateSPIRVIntegerType(unsigned BitWidth,
                                                 MachineIRBuilder &MIRBuilder) {
  return getOrCreateSPIRVType(
      IntegerType::get(MIRBuilder.getMF().getFunction().getContext(), BitWidth),
      MIRBuilder);
}

SPIRVType *SPIRVGlobalRegistry::finishCreatingSPIRVType(const Type *LLVMTy,
                                                        SPIRVType *SpirvType) {
  assert(CurMF == SpirvType->getMF());
  VRegToTypeMap[CurMF][getSPIRVTypeID(SpirvType)] = SpirvType;
  SPIRVToLLVMType[SpirvType] = unifyPtrType(LLVMTy);
  return SpirvType;
}

SPIRVType *SPIRVGlobalRegistry::getOrCreateSPIRVType(unsigned BitWidth,
                                                     MachineInstr &I,
                                                     const SPIRVInstrInfo &TII,
                                                     unsigned SPIRVOPcode,
                                                     Type *LLVMTy) {
  Register Reg = DT.find(LLVMTy, CurMF);
  if (Reg.isValid())
    return getSPIRVTypeForVReg(Reg);
  MachineBasicBlock &BB = *I.getParent();
  auto MIB = BuildMI(BB, I, I.getDebugLoc(), TII.get(SPIRVOPcode))
                 .addDef(createTypeVReg(CurMF->getRegInfo()))
                 .addImm(BitWidth)
                 .addImm(0);
  DT.add(LLVMTy, CurMF, getSPIRVTypeID(MIB));
  return finishCreatingSPIRVType(LLVMTy, MIB);
}

SPIRVType *SPIRVGlobalRegistry::getOrCreateSPIRVIntegerType(
    unsigned BitWidth, MachineInstr &I, const SPIRVInstrInfo &TII) {
  // Maybe adjust bit width to keep DuplicateTracker consistent. Without
  // such an adjustment SPIRVGlobalRegistry::getOpTypeInt() could create, for
  // example, the same "OpTypeInt 8" type for a series of LLVM integer types
  // with number of bits less than 8, causing duplicate type definitions.
  BitWidth = adjustOpTypeIntWidth(BitWidth);
  Type *LLVMTy = IntegerType::get(CurMF->getFunction().getContext(), BitWidth);
  return getOrCreateSPIRVType(BitWidth, I, TII, SPIRV::OpTypeInt, LLVMTy);
}

SPIRVType *SPIRVGlobalRegistry::getOrCreateSPIRVFloatType(
    unsigned BitWidth, MachineInstr &I, const SPIRVInstrInfo &TII) {
  LLVMContext &Ctx = CurMF->getFunction().getContext();
  Type *LLVMTy;
  switch (BitWidth) {
  case 16:
    LLVMTy = Type::getHalfTy(Ctx);
    break;
  case 32:
    LLVMTy = Type::getFloatTy(Ctx);
    break;
  case 64:
    LLVMTy = Type::getDoubleTy(Ctx);
    break;
  default:
    llvm_unreachable("Bit width is of unexpected size.");
  }
  return getOrCreateSPIRVType(BitWidth, I, TII, SPIRV::OpTypeFloat, LLVMTy);
}

SPIRVType *
SPIRVGlobalRegistry::getOrCreateSPIRVBoolType(MachineIRBuilder &MIRBuilder) {
  return getOrCreateSPIRVType(
      IntegerType::get(MIRBuilder.getMF().getFunction().getContext(), 1),
      MIRBuilder);
}

SPIRVType *
SPIRVGlobalRegistry::getOrCreateSPIRVBoolType(MachineInstr &I,
                                              const SPIRVInstrInfo &TII) {
  Type *LLVMTy = IntegerType::get(CurMF->getFunction().getContext(), 1);
  Register Reg = DT.find(LLVMTy, CurMF);
  if (Reg.isValid())
    return getSPIRVTypeForVReg(Reg);
  MachineBasicBlock &BB = *I.getParent();
  auto MIB = BuildMI(BB, I, I.getDebugLoc(), TII.get(SPIRV::OpTypeBool))
                 .addDef(createTypeVReg(CurMF->getRegInfo()));
  DT.add(LLVMTy, CurMF, getSPIRVTypeID(MIB));
  return finishCreatingSPIRVType(LLVMTy, MIB);
}

SPIRVType *SPIRVGlobalRegistry::getOrCreateSPIRVVectorType(
    SPIRVType *BaseType, unsigned NumElements, MachineIRBuilder &MIRBuilder) {
  return getOrCreateSPIRVType(
      FixedVectorType::get(const_cast<Type *>(getTypeForSPIRVType(BaseType)),
                           NumElements),
      MIRBuilder);
}

SPIRVType *SPIRVGlobalRegistry::getOrCreateSPIRVVectorType(
    SPIRVType *BaseType, unsigned NumElements, MachineInstr &I,
    const SPIRVInstrInfo &TII) {
  Type *LLVMTy = FixedVectorType::get(
      const_cast<Type *>(getTypeForSPIRVType(BaseType)), NumElements);
  Register Reg = DT.find(LLVMTy, CurMF);
  if (Reg.isValid())
    return getSPIRVTypeForVReg(Reg);
  MachineBasicBlock &BB = *I.getParent();
  auto MIB = BuildMI(BB, I, I.getDebugLoc(), TII.get(SPIRV::OpTypeVector))
                 .addDef(createTypeVReg(CurMF->getRegInfo()))
                 .addUse(getSPIRVTypeID(BaseType))
                 .addImm(NumElements);
  DT.add(LLVMTy, CurMF, getSPIRVTypeID(MIB));
  return finishCreatingSPIRVType(LLVMTy, MIB);
}

SPIRVType *SPIRVGlobalRegistry::getOrCreateSPIRVArrayType(
    SPIRVType *BaseType, unsigned NumElements, MachineInstr &I,
    const SPIRVInstrInfo &TII) {
  Type *LLVMTy = ArrayType::get(
      const_cast<Type *>(getTypeForSPIRVType(BaseType)), NumElements);
  Register Reg = DT.find(LLVMTy, CurMF);
  if (Reg.isValid())
    return getSPIRVTypeForVReg(Reg);
  MachineBasicBlock &BB = *I.getParent();
  SPIRVType *SpirvType = getOrCreateSPIRVIntegerType(32, I, TII);
  Register Len = getOrCreateConstInt(NumElements, I, SpirvType, TII);
  auto MIB = BuildMI(BB, I, I.getDebugLoc(), TII.get(SPIRV::OpTypeArray))
                 .addDef(createTypeVReg(CurMF->getRegInfo()))
                 .addUse(getSPIRVTypeID(BaseType))
                 .addUse(Len);
  DT.add(LLVMTy, CurMF, getSPIRVTypeID(MIB));
  return finishCreatingSPIRVType(LLVMTy, MIB);
}

SPIRVType *SPIRVGlobalRegistry::getOrCreateSPIRVPointerType(
    SPIRVType *BaseType, MachineIRBuilder &MIRBuilder,
    SPIRV::StorageClass::StorageClass SC) {
  const Type *PointerElementType = getTypeForSPIRVType(BaseType);
  unsigned AddressSpace = storageClassToAddressSpace(SC);
  Type *LLVMTy = TypedPointerType::get(const_cast<Type *>(PointerElementType),
                                       AddressSpace);
  // check if this type is already available
  Register Reg = DT.find(PointerElementType, AddressSpace, CurMF);
  if (Reg.isValid())
    return getSPIRVTypeForVReg(Reg);
  // create a new type
  auto MIB = BuildMI(MIRBuilder.getMBB(), MIRBuilder.getInsertPt(),
                     MIRBuilder.getDebugLoc(),
                     MIRBuilder.getTII().get(SPIRV::OpTypePointer))
                 .addDef(createTypeVReg(CurMF->getRegInfo()))
                 .addImm(static_cast<uint32_t>(SC))
                 .addUse(getSPIRVTypeID(BaseType));
  DT.add(PointerElementType, AddressSpace, CurMF, getSPIRVTypeID(MIB));
  return finishCreatingSPIRVType(LLVMTy, MIB);
}

SPIRVType *SPIRVGlobalRegistry::getOrCreateSPIRVPointerType(
    SPIRVType *BaseType, MachineInstr &I, const SPIRVInstrInfo &,
    SPIRV::StorageClass::StorageClass SC) {
  MachineIRBuilder MIRBuilder(I);
  return getOrCreateSPIRVPointerType(BaseType, MIRBuilder, SC);
}

Register SPIRVGlobalRegistry::getOrCreateUndef(MachineInstr &I,
                                               SPIRVType *SpvType,
                                               const SPIRVInstrInfo &TII) {
  assert(SpvType);
  const Type *LLVMTy = getTypeForSPIRVType(SpvType);
  assert(LLVMTy);
  // Find a constant in DT or build a new one.
  UndefValue *UV = UndefValue::get(const_cast<Type *>(LLVMTy));
  Register Res = DT.find(UV, CurMF);
  if (Res.isValid())
    return Res;
  LLT LLTy = LLT::scalar(32);
  Res = CurMF->getRegInfo().createGenericVirtualRegister(LLTy);
  CurMF->getRegInfo().setRegClass(Res, &SPIRV::IDRegClass);
  assignSPIRVTypeToVReg(SpvType, Res, *CurMF);
  DT.add(UV, CurMF, Res);

  MachineInstrBuilder MIB;
  MIB = BuildMI(*I.getParent(), I, I.getDebugLoc(), TII.get(SPIRV::OpUndef))
            .addDef(Res)
            .addUse(getSPIRVTypeID(SpvType));
  const auto &ST = CurMF->getSubtarget();
  constrainSelectedInstRegOperands(*MIB, *ST.getInstrInfo(),
                                   *ST.getRegisterInfo(), *ST.getRegBankInfo());
  return Res;
}
