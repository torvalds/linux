//===- SPIRVISelLowering.cpp - SPIR-V DAG Lowering Impl ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the SPIRVTargetLowering class.
//
//===----------------------------------------------------------------------===//

#include "SPIRVISelLowering.h"
#include "SPIRV.h"
#include "SPIRVInstrInfo.h"
#include "SPIRVRegisterBankInfo.h"
#include "SPIRVRegisterInfo.h"
#include "SPIRVSubtarget.h"
#include "SPIRVTargetMachine.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/IntrinsicsSPIRV.h"

#define DEBUG_TYPE "spirv-lower"

using namespace llvm;

unsigned SPIRVTargetLowering::getNumRegistersForCallingConv(
    LLVMContext &Context, CallingConv::ID CC, EVT VT) const {
  // This code avoids CallLowering fail inside getVectorTypeBreakdown
  // on v3i1 arguments. Maybe we need to return 1 for all types.
  // TODO: remove it once this case is supported by the default implementation.
  if (VT.isVector() && VT.getVectorNumElements() == 3 &&
      (VT.getVectorElementType() == MVT::i1 ||
       VT.getVectorElementType() == MVT::i8))
    return 1;
  if (!VT.isVector() && VT.isInteger() && VT.getSizeInBits() <= 64)
    return 1;
  return getNumRegisters(Context, VT);
}

MVT SPIRVTargetLowering::getRegisterTypeForCallingConv(LLVMContext &Context,
                                                       CallingConv::ID CC,
                                                       EVT VT) const {
  // This code avoids CallLowering fail inside getVectorTypeBreakdown
  // on v3i1 arguments. Maybe we need to return i32 for all types.
  // TODO: remove it once this case is supported by the default implementation.
  if (VT.isVector() && VT.getVectorNumElements() == 3) {
    if (VT.getVectorElementType() == MVT::i1)
      return MVT::v4i1;
    else if (VT.getVectorElementType() == MVT::i8)
      return MVT::v4i8;
  }
  return getRegisterType(Context, VT);
}

bool SPIRVTargetLowering::getTgtMemIntrinsic(IntrinsicInfo &Info,
                                             const CallInst &I,
                                             MachineFunction &MF,
                                             unsigned Intrinsic) const {
  unsigned AlignIdx = 3;
  switch (Intrinsic) {
  case Intrinsic::spv_load:
    AlignIdx = 2;
    [[fallthrough]];
  case Intrinsic::spv_store: {
    if (I.getNumOperands() >= AlignIdx + 1) {
      auto *AlignOp = cast<ConstantInt>(I.getOperand(AlignIdx));
      Info.align = Align(AlignOp->getZExtValue());
    }
    Info.flags = static_cast<MachineMemOperand::Flags>(
        cast<ConstantInt>(I.getOperand(AlignIdx - 1))->getZExtValue());
    Info.memVT = MVT::i64;
    // TODO: take into account opaque pointers (don't use getElementType).
    // MVT::getVT(PtrTy->getElementType());
    return true;
    break;
  }
  default:
    break;
  }
  return false;
}

std::pair<unsigned, const TargetRegisterClass *>
SPIRVTargetLowering::getRegForInlineAsmConstraint(const TargetRegisterInfo *TRI,
                                                  StringRef Constraint,
                                                  MVT VT) const {
  const TargetRegisterClass *RC = nullptr;
  if (Constraint.starts_with("{"))
    return std::make_pair(0u, RC);

  if (VT.isFloatingPoint())
    RC = VT.isVector() ? &SPIRV::vfIDRegClass
                       : (VT.getScalarSizeInBits() > 32 ? &SPIRV::fID64RegClass
                                                        : &SPIRV::fIDRegClass);
  else if (VT.isInteger())
    RC = VT.isVector() ? &SPIRV::vIDRegClass
                       : (VT.getScalarSizeInBits() > 32 ? &SPIRV::ID64RegClass
                                                        : &SPIRV::IDRegClass);
  else
    RC = &SPIRV::IDRegClass;

  return std::make_pair(0u, RC);
}

inline Register getTypeReg(MachineRegisterInfo *MRI, Register OpReg) {
  SPIRVType *TypeInst = MRI->getVRegDef(OpReg);
  return TypeInst && TypeInst->getOpcode() == SPIRV::OpFunctionParameter
             ? TypeInst->getOperand(1).getReg()
             : OpReg;
}

static void doInsertBitcast(const SPIRVSubtarget &STI, MachineRegisterInfo *MRI,
                            SPIRVGlobalRegistry &GR, MachineInstr &I,
                            Register OpReg, unsigned OpIdx,
                            SPIRVType *NewPtrType) {
  Register NewReg = MRI->createGenericVirtualRegister(LLT::scalar(32));
  MachineIRBuilder MIB(I);
  bool Res = MIB.buildInstr(SPIRV::OpBitcast)
                 .addDef(NewReg)
                 .addUse(GR.getSPIRVTypeID(NewPtrType))
                 .addUse(OpReg)
                 .constrainAllUses(*STI.getInstrInfo(), *STI.getRegisterInfo(),
                                   *STI.getRegBankInfo());
  if (!Res)
    report_fatal_error("insert validation bitcast: cannot constrain all uses");
  MRI->setRegClass(NewReg, &SPIRV::IDRegClass);
  GR.assignSPIRVTypeToVReg(NewPtrType, NewReg, MIB.getMF());
  I.getOperand(OpIdx).setReg(NewReg);
}

static SPIRVType *createNewPtrType(SPIRVGlobalRegistry &GR, MachineInstr &I,
                                   SPIRVType *OpType, bool ReuseType,
                                   bool EmitIR, SPIRVType *ResType,
                                   const Type *ResTy) {
  SPIRV::StorageClass::StorageClass SC =
      static_cast<SPIRV::StorageClass::StorageClass>(
          OpType->getOperand(1).getImm());
  MachineIRBuilder MIB(I);
  SPIRVType *NewBaseType =
      ReuseType ? ResType
                : GR.getOrCreateSPIRVType(
                      ResTy, MIB, SPIRV::AccessQualifier::ReadWrite, EmitIR);
  return GR.getOrCreateSPIRVPointerType(NewBaseType, MIB, SC);
}

// Insert a bitcast before the instruction to keep SPIR-V code valid
// when there is a type mismatch between results and operand types.
static void validatePtrTypes(const SPIRVSubtarget &STI,
                             MachineRegisterInfo *MRI, SPIRVGlobalRegistry &GR,
                             MachineInstr &I, unsigned OpIdx,
                             SPIRVType *ResType, const Type *ResTy = nullptr) {
  // Get operand type
  MachineFunction *MF = I.getParent()->getParent();
  Register OpReg = I.getOperand(OpIdx).getReg();
  Register OpTypeReg = getTypeReg(MRI, OpReg);
  SPIRVType *OpType = GR.getSPIRVTypeForVReg(OpTypeReg, MF);
  if (!ResType || !OpType || OpType->getOpcode() != SPIRV::OpTypePointer)
    return;
  // Get operand's pointee type
  Register ElemTypeReg = OpType->getOperand(2).getReg();
  SPIRVType *ElemType = GR.getSPIRVTypeForVReg(ElemTypeReg, MF);
  if (!ElemType)
    return;
  // Check if we need a bitcast to make a statement valid
  bool IsSameMF = MF == ResType->getParent()->getParent();
  bool IsEqualTypes = IsSameMF ? ElemType == ResType
                               : GR.getTypeForSPIRVType(ElemType) == ResTy;
  if (IsEqualTypes)
    return;
  // There is a type mismatch between results and operand types
  // and we insert a bitcast before the instruction to keep SPIR-V code valid
  SPIRVType *NewPtrType =
      createNewPtrType(GR, I, OpType, IsSameMF, false, ResType, ResTy);
  if (!GR.isBitcastCompatible(NewPtrType, OpType))
    report_fatal_error(
        "insert validation bitcast: incompatible result and operand types");
  doInsertBitcast(STI, MRI, GR, I, OpReg, OpIdx, NewPtrType);
}

// Insert a bitcast before OpGroupWaitEvents if the last argument is a pointer
// that doesn't point to OpTypeEvent.
static void validateGroupWaitEventsPtr(const SPIRVSubtarget &STI,
                                       MachineRegisterInfo *MRI,
                                       SPIRVGlobalRegistry &GR,
                                       MachineInstr &I) {
  constexpr unsigned OpIdx = 2;
  MachineFunction *MF = I.getParent()->getParent();
  Register OpReg = I.getOperand(OpIdx).getReg();
  Register OpTypeReg = getTypeReg(MRI, OpReg);
  SPIRVType *OpType = GR.getSPIRVTypeForVReg(OpTypeReg, MF);
  if (!OpType || OpType->getOpcode() != SPIRV::OpTypePointer)
    return;
  SPIRVType *ElemType = GR.getSPIRVTypeForVReg(OpType->getOperand(2).getReg());
  if (!ElemType || ElemType->getOpcode() == SPIRV::OpTypeEvent)
    return;
  // Insert a bitcast before the instruction to keep SPIR-V code valid.
  LLVMContext &Context = MF->getFunction().getContext();
  SPIRVType *NewPtrType =
      createNewPtrType(GR, I, OpType, false, true, nullptr,
                       TargetExtType::get(Context, "spirv.Event"));
  doInsertBitcast(STI, MRI, GR, I, OpReg, OpIdx, NewPtrType);
}

static void validateGroupAsyncCopyPtr(const SPIRVSubtarget &STI,
                                      MachineRegisterInfo *MRI,
                                      SPIRVGlobalRegistry &GR, MachineInstr &I,
                                      unsigned OpIdx) {
  MachineFunction *MF = I.getParent()->getParent();
  Register OpReg = I.getOperand(OpIdx).getReg();
  Register OpTypeReg = getTypeReg(MRI, OpReg);
  SPIRVType *OpType = GR.getSPIRVTypeForVReg(OpTypeReg, MF);
  if (!OpType || OpType->getOpcode() != SPIRV::OpTypePointer)
    return;
  SPIRVType *ElemType = GR.getSPIRVTypeForVReg(OpType->getOperand(2).getReg());
  if (!ElemType || ElemType->getOpcode() != SPIRV::OpTypeStruct ||
      ElemType->getNumOperands() != 2)
    return;
  // It's a structure-wrapper around another type with a single member field.
  SPIRVType *MemberType =
      GR.getSPIRVTypeForVReg(ElemType->getOperand(1).getReg());
  if (!MemberType)
    return;
  unsigned MemberTypeOp = MemberType->getOpcode();
  if (MemberTypeOp != SPIRV::OpTypeVector && MemberTypeOp != SPIRV::OpTypeInt &&
      MemberTypeOp != SPIRV::OpTypeFloat && MemberTypeOp != SPIRV::OpTypeBool)
    return;
  // It's a structure-wrapper around a valid type. Insert a bitcast before the
  // instruction to keep SPIR-V code valid.
  SPIRV::StorageClass::StorageClass SC =
      static_cast<SPIRV::StorageClass::StorageClass>(
          OpType->getOperand(1).getImm());
  MachineIRBuilder MIB(I);
  SPIRVType *NewPtrType = GR.getOrCreateSPIRVPointerType(MemberType, MIB, SC);
  doInsertBitcast(STI, MRI, GR, I, OpReg, OpIdx, NewPtrType);
}

// Insert a bitcast before the function call instruction to keep SPIR-V code
// valid when there is a type mismatch between actual and expected types of an
// argument:
// %formal = OpFunctionParameter %formal_type
// ...
// %res = OpFunctionCall %ty %fun %actual ...
// implies that %actual is of %formal_type, and in case of opaque pointers.
// We may need to insert a bitcast to ensure this.
void validateFunCallMachineDef(const SPIRVSubtarget &STI,
                               MachineRegisterInfo *DefMRI,
                               MachineRegisterInfo *CallMRI,
                               SPIRVGlobalRegistry &GR, MachineInstr &FunCall,
                               MachineInstr *FunDef) {
  if (FunDef->getOpcode() != SPIRV::OpFunction)
    return;
  unsigned OpIdx = 3;
  for (FunDef = FunDef->getNextNode();
       FunDef && FunDef->getOpcode() == SPIRV::OpFunctionParameter &&
       OpIdx < FunCall.getNumOperands();
       FunDef = FunDef->getNextNode(), OpIdx++) {
    SPIRVType *DefPtrType = DefMRI->getVRegDef(FunDef->getOperand(1).getReg());
    SPIRVType *DefElemType =
        DefPtrType && DefPtrType->getOpcode() == SPIRV::OpTypePointer
            ? GR.getSPIRVTypeForVReg(DefPtrType->getOperand(2).getReg(),
                                     DefPtrType->getParent()->getParent())
            : nullptr;
    if (DefElemType) {
      const Type *DefElemTy = GR.getTypeForSPIRVType(DefElemType);
      // validatePtrTypes() works in the context if the call site
      // When we process historical records about forward calls
      // we need to switch context to the (forward) call site and
      // then restore it back to the current machine function.
      MachineFunction *CurMF =
          GR.setCurrentFunc(*FunCall.getParent()->getParent());
      validatePtrTypes(STI, CallMRI, GR, FunCall, OpIdx, DefElemType,
                       DefElemTy);
      GR.setCurrentFunc(*CurMF);
    }
  }
}

// Ensure there is no mismatch between actual and expected arg types: calls
// with a processed definition. Return Function pointer if it's a forward
// call (ahead of definition), and nullptr otherwise.
const Function *validateFunCall(const SPIRVSubtarget &STI,
                                MachineRegisterInfo *CallMRI,
                                SPIRVGlobalRegistry &GR,
                                MachineInstr &FunCall) {
  const GlobalValue *GV = FunCall.getOperand(2).getGlobal();
  const Function *F = dyn_cast<Function>(GV);
  MachineInstr *FunDef =
      const_cast<MachineInstr *>(GR.getFunctionDefinition(F));
  if (!FunDef)
    return F;
  MachineRegisterInfo *DefMRI = &FunDef->getParent()->getParent()->getRegInfo();
  validateFunCallMachineDef(STI, DefMRI, CallMRI, GR, FunCall, FunDef);
  return nullptr;
}

// Ensure there is no mismatch between actual and expected arg types: calls
// ahead of a processed definition.
void validateForwardCalls(const SPIRVSubtarget &STI,
                          MachineRegisterInfo *DefMRI, SPIRVGlobalRegistry &GR,
                          MachineInstr &FunDef) {
  const Function *F = GR.getFunctionByDefinition(&FunDef);
  if (SmallPtrSet<MachineInstr *, 8> *FwdCalls = GR.getForwardCalls(F))
    for (MachineInstr *FunCall : *FwdCalls) {
      MachineRegisterInfo *CallMRI =
          &FunCall->getParent()->getParent()->getRegInfo();
      validateFunCallMachineDef(STI, DefMRI, CallMRI, GR, *FunCall, &FunDef);
    }
}

// Validation of an access chain.
void validateAccessChain(const SPIRVSubtarget &STI, MachineRegisterInfo *MRI,
                         SPIRVGlobalRegistry &GR, MachineInstr &I) {
  SPIRVType *BaseTypeInst = GR.getSPIRVTypeForVReg(I.getOperand(0).getReg());
  if (BaseTypeInst && BaseTypeInst->getOpcode() == SPIRV::OpTypePointer) {
    SPIRVType *BaseElemType =
        GR.getSPIRVTypeForVReg(BaseTypeInst->getOperand(2).getReg());
    validatePtrTypes(STI, MRI, GR, I, 2, BaseElemType);
  }
}

// TODO: the logic of inserting additional bitcast's is to be moved
// to pre-IRTranslation passes eventually
void SPIRVTargetLowering::finalizeLowering(MachineFunction &MF) const {
  // finalizeLowering() is called twice (see GlobalISel/InstructionSelect.cpp)
  // We'd like to avoid the needless second processing pass.
  if (ProcessedMF.find(&MF) != ProcessedMF.end())
    return;

  MachineRegisterInfo *MRI = &MF.getRegInfo();
  SPIRVGlobalRegistry &GR = *STI.getSPIRVGlobalRegistry();
  GR.setCurrentFunc(MF);
  for (MachineFunction::iterator I = MF.begin(), E = MF.end(); I != E; ++I) {
    MachineBasicBlock *MBB = &*I;
    for (MachineBasicBlock::iterator MBBI = MBB->begin(), MBBE = MBB->end();
         MBBI != MBBE;) {
      MachineInstr &MI = *MBBI++;
      switch (MI.getOpcode()) {
      case SPIRV::OpAtomicLoad:
      case SPIRV::OpAtomicExchange:
      case SPIRV::OpAtomicCompareExchange:
      case SPIRV::OpAtomicCompareExchangeWeak:
      case SPIRV::OpAtomicIIncrement:
      case SPIRV::OpAtomicIDecrement:
      case SPIRV::OpAtomicIAdd:
      case SPIRV::OpAtomicISub:
      case SPIRV::OpAtomicSMin:
      case SPIRV::OpAtomicUMin:
      case SPIRV::OpAtomicSMax:
      case SPIRV::OpAtomicUMax:
      case SPIRV::OpAtomicAnd:
      case SPIRV::OpAtomicOr:
      case SPIRV::OpAtomicXor:
        // for the above listed instructions
        // OpAtomicXXX <ResType>, ptr %Op, ...
        // implies that %Op is a pointer to <ResType>
      case SPIRV::OpLoad:
        // OpLoad <ResType>, ptr %Op implies that %Op is a pointer to <ResType>
        validatePtrTypes(STI, MRI, GR, MI, 2,
                         GR.getSPIRVTypeForVReg(MI.getOperand(0).getReg()));
        break;
      case SPIRV::OpAtomicStore:
        // OpAtomicStore ptr %Op, <Scope>, <Mem>, <Obj>
        // implies that %Op points to the <Obj>'s type
        validatePtrTypes(STI, MRI, GR, MI, 0,
                         GR.getSPIRVTypeForVReg(MI.getOperand(3).getReg()));
        break;
      case SPIRV::OpStore:
        // OpStore ptr %Op, <Obj> implies that %Op points to the <Obj>'s type
        validatePtrTypes(STI, MRI, GR, MI, 0,
                         GR.getSPIRVTypeForVReg(MI.getOperand(1).getReg()));
        break;
      case SPIRV::OpPtrCastToGeneric:
      case SPIRV::OpGenericCastToPtr:
        validateAccessChain(STI, MRI, GR, MI);
        break;
      case SPIRV::OpInBoundsPtrAccessChain:
        if (MI.getNumOperands() == 4)
          validateAccessChain(STI, MRI, GR, MI);
        break;

      case SPIRV::OpFunctionCall:
        // ensure there is no mismatch between actual and expected arg types:
        // calls with a processed definition
        if (MI.getNumOperands() > 3)
          if (const Function *F = validateFunCall(STI, MRI, GR, MI))
            GR.addForwardCall(F, &MI);
        break;
      case SPIRV::OpFunction:
        // ensure there is no mismatch between actual and expected arg types:
        // calls ahead of a processed definition
        validateForwardCalls(STI, MRI, GR, MI);
        break;

      // ensure that LLVM IR bitwise instructions result in logical SPIR-V
      // instructions when applied to bool type
      case SPIRV::OpBitwiseOrS:
      case SPIRV::OpBitwiseOrV:
        if (GR.isScalarOrVectorOfType(MI.getOperand(1).getReg(),
                                      SPIRV::OpTypeBool))
          MI.setDesc(STI.getInstrInfo()->get(SPIRV::OpLogicalOr));
        break;
      case SPIRV::OpBitwiseAndS:
      case SPIRV::OpBitwiseAndV:
        if (GR.isScalarOrVectorOfType(MI.getOperand(1).getReg(),
                                      SPIRV::OpTypeBool))
          MI.setDesc(STI.getInstrInfo()->get(SPIRV::OpLogicalAnd));
        break;
      case SPIRV::OpBitwiseXorS:
      case SPIRV::OpBitwiseXorV:
        if (GR.isScalarOrVectorOfType(MI.getOperand(1).getReg(),
                                      SPIRV::OpTypeBool))
          MI.setDesc(STI.getInstrInfo()->get(SPIRV::OpLogicalNotEqual));
        break;
      case SPIRV::OpGroupAsyncCopy:
        validateGroupAsyncCopyPtr(STI, MRI, GR, MI, 3);
        validateGroupAsyncCopyPtr(STI, MRI, GR, MI, 4);
        break;
      case SPIRV::OpGroupWaitEvents:
        // OpGroupWaitEvents ..., ..., <pointer to OpTypeEvent>
        validateGroupWaitEventsPtr(STI, MRI, GR, MI);
        break;
      case SPIRV::OpConstantI: {
        SPIRVType *Type = GR.getSPIRVTypeForVReg(MI.getOperand(1).getReg());
        if (Type->getOpcode() != SPIRV::OpTypeInt && MI.getOperand(2).isImm() &&
            MI.getOperand(2).getImm() == 0) {
          // Validate the null constant of a target extension type
          MI.setDesc(STI.getInstrInfo()->get(SPIRV::OpConstantNull));
          for (unsigned i = MI.getNumOperands() - 1; i > 1; --i)
            MI.removeOperand(i);
        }
      } break;
      }
    }
  }
  ProcessedMF.insert(&MF);
  TargetLowering::finalizeLowering(MF);
}
