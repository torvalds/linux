//===--- SPIRVUtils.h ---- SPIR-V Utility Functions -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains miscellaneous utility functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPIRV_SPIRVUTILS_H
#define LLVM_LIB_TARGET_SPIRV_SPIRVUTILS_H

#include "MCTargetDesc/SPIRVBaseInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/TypedPointerType.h"
#include <string>

namespace llvm {
class MCInst;
class MachineFunction;
class MachineInstr;
class MachineInstrBuilder;
class MachineIRBuilder;
class MachineRegisterInfo;
class Register;
class StringRef;
class SPIRVInstrInfo;
class SPIRVSubtarget;

// Add the given string as a series of integer operand, inserting null
// terminators and padding to make sure the operands all have 32-bit
// little-endian words.
void addStringImm(const StringRef &Str, MCInst &Inst);
void addStringImm(const StringRef &Str, MachineInstrBuilder &MIB);
void addStringImm(const StringRef &Str, IRBuilder<> &B,
                  std::vector<Value *> &Args);

// Read the series of integer operands back as a null-terminated string using
// the reverse of the logic in addStringImm.
std::string getStringImm(const MachineInstr &MI, unsigned StartIndex);

// Add the given numerical immediate to MIB.
void addNumImm(const APInt &Imm, MachineInstrBuilder &MIB);

// Add an OpName instruction for the given target register.
void buildOpName(Register Target, const StringRef &Name,
                 MachineIRBuilder &MIRBuilder);

// Add an OpDecorate instruction for the given Reg.
void buildOpDecorate(Register Reg, MachineIRBuilder &MIRBuilder,
                     SPIRV::Decoration::Decoration Dec,
                     const std::vector<uint32_t> &DecArgs,
                     StringRef StrImm = "");
void buildOpDecorate(Register Reg, MachineInstr &I, const SPIRVInstrInfo &TII,
                     SPIRV::Decoration::Decoration Dec,
                     const std::vector<uint32_t> &DecArgs,
                     StringRef StrImm = "");

// Add an OpDecorate instruction by "spirv.Decorations" metadata node.
void buildOpSpirvDecorations(Register Reg, MachineIRBuilder &MIRBuilder,
                             const MDNode *GVarMD);

// Convert a SPIR-V storage class to the corresponding LLVM IR address space.
unsigned storageClassToAddressSpace(SPIRV::StorageClass::StorageClass SC);

// Convert an LLVM IR address space to a SPIR-V storage class.
SPIRV::StorageClass::StorageClass
addressSpaceToStorageClass(unsigned AddrSpace, const SPIRVSubtarget &STI);

SPIRV::MemorySemantics::MemorySemantics
getMemSemanticsForStorageClass(SPIRV::StorageClass::StorageClass SC);

SPIRV::MemorySemantics::MemorySemantics getMemSemantics(AtomicOrdering Ord);

// Find def instruction for the given ConstReg, walking through
// spv_track_constant and ASSIGN_TYPE instructions. Updates ConstReg by def
// of OpConstant instruction.
MachineInstr *getDefInstrMaybeConstant(Register &ConstReg,
                                       const MachineRegisterInfo *MRI);

// Get constant integer value of the given ConstReg.
uint64_t getIConstVal(Register ConstReg, const MachineRegisterInfo *MRI);

// Check if MI is a SPIR-V specific intrinsic call.
bool isSpvIntrinsic(const MachineInstr &MI, Intrinsic::ID IntrinsicID);

// Get type of i-th operand of the metadata node.
Type *getMDOperandAsType(const MDNode *N, unsigned I);

// If OpenCL or SPIR-V builtin function name is recognized, return a demangled
// name, otherwise return an empty string.
std::string getOclOrSpirvBuiltinDemangledName(StringRef Name);

// Check if a string contains a builtin prefix.
bool hasBuiltinTypePrefix(StringRef Name);

// Check if given LLVM type is a special opaque builtin type.
bool isSpecialOpaqueType(const Type *Ty);

// Check if the function is an SPIR-V entry point
bool isEntryPoint(const Function &F);

// Parse basic scalar type name, substring TypeName, and return LLVM type.
Type *parseBasicTypeName(StringRef &TypeName, LLVMContext &Ctx);

// True if this is an instance of TypedPointerType.
inline bool isTypedPointerTy(const Type *T) {
  return T && T->getTypeID() == Type::TypedPointerTyID;
}

// True if this is an instance of PointerType.
inline bool isUntypedPointerTy(const Type *T) {
  return T && T->getTypeID() == Type::PointerTyID;
}

// True if this is an instance of PointerType or TypedPointerType.
inline bool isPointerTy(const Type *T) {
  return isUntypedPointerTy(T) || isTypedPointerTy(T);
}

// Get the address space of this pointer or pointer vector type for instances of
// PointerType or TypedPointerType.
inline unsigned getPointerAddressSpace(const Type *T) {
  Type *SubT = T->getScalarType();
  return SubT->getTypeID() == Type::PointerTyID
             ? cast<PointerType>(SubT)->getAddressSpace()
             : cast<TypedPointerType>(SubT)->getAddressSpace();
}

// Return true if the Argument is decorated with a pointee type
inline bool hasPointeeTypeAttr(Argument *Arg) {
  return Arg->hasByValAttr() || Arg->hasByRefAttr() || Arg->hasStructRetAttr();
}

// Return the pointee type of the argument or nullptr otherwise
inline Type *getPointeeTypeByAttr(Argument *Arg) {
  if (Arg->hasByValAttr())
    return Arg->getParamByValType();
  if (Arg->hasStructRetAttr())
    return Arg->getParamStructRetType();
  if (Arg->hasByRefAttr())
    return Arg->getParamByRefType();
  return nullptr;
}

inline Type *reconstructFunctionType(Function *F) {
  SmallVector<Type *> ArgTys;
  for (unsigned i = 0; i < F->arg_size(); ++i)
    ArgTys.push_back(F->getArg(i)->getType());
  return FunctionType::get(F->getReturnType(), ArgTys, F->isVarArg());
}

#define TYPED_PTR_TARGET_EXT_NAME "spirv.$TypedPointerType"
inline Type *getTypedPointerWrapper(Type *ElemTy, unsigned AS) {
  return TargetExtType::get(ElemTy->getContext(), TYPED_PTR_TARGET_EXT_NAME,
                            {ElemTy}, {AS});
}

inline bool isTypedPointerWrapper(TargetExtType *ExtTy) {
  return ExtTy->getName() == TYPED_PTR_TARGET_EXT_NAME &&
         ExtTy->getNumIntParameters() == 1 &&
         ExtTy->getNumTypeParameters() == 1;
}

inline Type *applyWrappers(Type *Ty) {
  if (auto *ExtTy = dyn_cast<TargetExtType>(Ty)) {
    if (isTypedPointerWrapper(ExtTy))
      return TypedPointerType::get(applyWrappers(ExtTy->getTypeParameter(0)),
                                   ExtTy->getIntParameter(0));
  } else if (auto *VecTy = dyn_cast<VectorType>(Ty)) {
    Type *ElemTy = VecTy->getElementType();
    Type *NewElemTy = ElemTy->isTargetExtTy() ? applyWrappers(ElemTy) : ElemTy;
    if (NewElemTy != ElemTy)
      return VectorType::get(NewElemTy, VecTy->getElementCount());
  }
  return Ty;
}

inline Type *getPointeeType(Type *Ty) {
  if (auto PType = dyn_cast<TypedPointerType>(Ty))
    return PType->getElementType();
  else if (auto *ExtTy = dyn_cast<TargetExtType>(Ty))
    if (isTypedPointerWrapper(ExtTy))
      return applyWrappers(ExtTy->getTypeParameter(0));
  return nullptr;
}

inline bool isUntypedEquivalentToTyExt(Type *Ty1, Type *Ty2) {
  if (!isUntypedPointerTy(Ty1) || !Ty2)
    return false;
  if (auto *ExtTy = dyn_cast<TargetExtType>(Ty2))
    if (isTypedPointerWrapper(ExtTy) &&
        ExtTy->getTypeParameter(0) ==
            IntegerType::getInt8Ty(Ty1->getContext()) &&
        ExtTy->getIntParameter(0) == cast<PointerType>(Ty1)->getAddressSpace())
      return true;
  return false;
}

inline bool isEquivalentTypes(Type *Ty1, Type *Ty2) {
  return isUntypedEquivalentToTyExt(Ty1, Ty2) ||
         isUntypedEquivalentToTyExt(Ty2, Ty1);
}

inline Type *toTypedPointer(Type *Ty) {
  if (Type *NewTy = applyWrappers(Ty); NewTy != Ty)
    return NewTy;
  return isUntypedPointerTy(Ty)
             ? TypedPointerType::get(IntegerType::getInt8Ty(Ty->getContext()),
                                     getPointerAddressSpace(Ty))
             : Ty;
}

inline Type *toTypedFunPointer(FunctionType *FTy) {
  Type *OrigRetTy = FTy->getReturnType();
  Type *RetTy = toTypedPointer(OrigRetTy);
  bool IsUntypedPtr = false;
  for (Type *PTy : FTy->params()) {
    if (isUntypedPointerTy(PTy)) {
      IsUntypedPtr = true;
      break;
    }
  }
  if (!IsUntypedPtr && RetTy == OrigRetTy)
    return FTy;
  SmallVector<Type *> ParamTys;
  for (Type *PTy : FTy->params())
    ParamTys.push_back(toTypedPointer(PTy));
  return FunctionType::get(RetTy, ParamTys, FTy->isVarArg());
}

inline const Type *unifyPtrType(const Type *Ty) {
  if (auto FTy = dyn_cast<FunctionType>(Ty))
    return toTypedFunPointer(const_cast<FunctionType *>(FTy));
  return toTypedPointer(const_cast<Type *>(Ty));
}

} // namespace llvm
#endif // LLVM_LIB_TARGET_SPIRV_SPIRVUTILS_H
