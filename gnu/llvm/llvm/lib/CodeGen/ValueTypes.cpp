//===----------- ValueTypes.cpp - Implementation of EVT methods -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/ValueTypes.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TypeSize.h"
#include "llvm/Support/WithColor.h"
using namespace llvm;

EVT EVT::changeExtendedTypeToInteger() const {
  assert(isExtended() && "Type is not extended!");
  LLVMContext &Context = LLVMTy->getContext();
  return getIntegerVT(Context, getSizeInBits());
}

EVT EVT::changeExtendedVectorElementTypeToInteger() const {
  assert(isExtended() && "Type is not extended!");
  LLVMContext &Context = LLVMTy->getContext();
  EVT IntTy = getIntegerVT(Context, getScalarSizeInBits());
  return getVectorVT(Context, IntTy, getVectorElementCount());
}

EVT EVT::changeExtendedVectorElementType(EVT EltVT) const {
  assert(isExtended() && "Type is not extended!");
  LLVMContext &Context = LLVMTy->getContext();
  return getVectorVT(Context, EltVT, getVectorElementCount());
}

EVT EVT::getExtendedIntegerVT(LLVMContext &Context, unsigned BitWidth) {
  EVT VT;
  VT.LLVMTy = IntegerType::get(Context, BitWidth);
  assert(VT.isExtended() && "Type is not extended!");
  return VT;
}

EVT EVT::getExtendedVectorVT(LLVMContext &Context, EVT VT, unsigned NumElements,
                             bool IsScalable) {
  EVT ResultVT;
  ResultVT.LLVMTy =
      VectorType::get(VT.getTypeForEVT(Context), NumElements, IsScalable);
  assert(ResultVT.isExtended() && "Type is not extended!");
  return ResultVT;
}

EVT EVT::getExtendedVectorVT(LLVMContext &Context, EVT VT, ElementCount EC) {
  EVT ResultVT;
  ResultVT.LLVMTy = VectorType::get(VT.getTypeForEVT(Context), EC);
  assert(ResultVT.isExtended() && "Type is not extended!");
  return ResultVT;
}

bool EVT::isExtendedFloatingPoint() const {
  assert(isExtended() && "Type is not extended!");
  return LLVMTy->isFPOrFPVectorTy();
}

bool EVT::isExtendedInteger() const {
  assert(isExtended() && "Type is not extended!");
  return LLVMTy->isIntOrIntVectorTy();
}

bool EVT::isExtendedScalarInteger() const {
  assert(isExtended() && "Type is not extended!");
  return LLVMTy->isIntegerTy();
}

bool EVT::isExtendedVector() const {
  assert(isExtended() && "Type is not extended!");
  return LLVMTy->isVectorTy();
}

bool EVT::isExtended16BitVector() const {
  return isExtendedVector() &&
         getExtendedSizeInBits() == TypeSize::getFixed(16);
}

bool EVT::isExtended32BitVector() const {
  return isExtendedVector() &&
         getExtendedSizeInBits() == TypeSize::getFixed(32);
}

bool EVT::isExtended64BitVector() const {
  return isExtendedVector() &&
         getExtendedSizeInBits() == TypeSize::getFixed(64);
}

bool EVT::isExtended128BitVector() const {
  return isExtendedVector() &&
         getExtendedSizeInBits() == TypeSize::getFixed(128);
}

bool EVT::isExtended256BitVector() const {
  return isExtendedVector() &&
         getExtendedSizeInBits() == TypeSize::getFixed(256);
}

bool EVT::isExtended512BitVector() const {
  return isExtendedVector() &&
         getExtendedSizeInBits() == TypeSize::getFixed(512);
}

bool EVT::isExtended1024BitVector() const {
  return isExtendedVector() &&
         getExtendedSizeInBits() == TypeSize::getFixed(1024);
}

bool EVT::isExtended2048BitVector() const {
  return isExtendedVector() &&
         getExtendedSizeInBits() == TypeSize::getFixed(2048);
}

bool EVT::isExtendedFixedLengthVector() const {
  return isExtendedVector() && isa<FixedVectorType>(LLVMTy);
}

bool EVT::isExtendedScalableVector() const {
  return isExtendedVector() && isa<ScalableVectorType>(LLVMTy);
}

EVT EVT::getExtendedVectorElementType() const {
  assert(isExtended() && "Type is not extended!");
  return EVT::getEVT(cast<VectorType>(LLVMTy)->getElementType());
}

unsigned EVT::getExtendedVectorNumElements() const {
  assert(isExtended() && "Type is not extended!");
  ElementCount EC = cast<VectorType>(LLVMTy)->getElementCount();
  if (EC.isScalable()) {
    WithColor::warning()
        << "The code that requested the fixed number of elements has made the "
           "assumption that this vector is not scalable. This assumption was "
           "not correct, and this may lead to broken code\n";
  }
  return EC.getKnownMinValue();
}

ElementCount EVT::getExtendedVectorElementCount() const {
  assert(isExtended() && "Type is not extended!");
  return cast<VectorType>(LLVMTy)->getElementCount();
}

TypeSize EVT::getExtendedSizeInBits() const {
  assert(isExtended() && "Type is not extended!");
  if (IntegerType *ITy = dyn_cast<IntegerType>(LLVMTy))
    return TypeSize::getFixed(ITy->getBitWidth());
  if (VectorType *VTy = dyn_cast<VectorType>(LLVMTy))
    return VTy->getPrimitiveSizeInBits();
  llvm_unreachable("Unrecognized extended type!");
}

/// getEVTString - This function returns value type as a string, e.g. "i32".
std::string EVT::getEVTString() const {
  switch (V.SimpleTy) {
  default:
    if (isVector())
      return (isScalableVector() ? "nxv" : "v") +
             utostr(getVectorElementCount().getKnownMinValue()) +
             getVectorElementType().getEVTString();
    if (isInteger())
      return "i" + utostr(getSizeInBits());
    if (isFloatingPoint())
      return "f" + utostr(getSizeInBits());
    llvm_unreachable("Invalid EVT!");
  case MVT::bf16:      return "bf16";
  case MVT::ppcf128:   return "ppcf128";
  case MVT::isVoid:    return "isVoid";
  case MVT::Other:     return "ch";
  case MVT::Glue:      return "glue";
  case MVT::x86mmx:    return "x86mmx";
  case MVT::x86amx:    return "x86amx";
  case MVT::i64x8:     return "i64x8";
  case MVT::Metadata:  return "Metadata";
  case MVT::Untyped:   return "Untyped";
  case MVT::funcref:   return "funcref";
  case MVT::exnref:    return "exnref";
  case MVT::externref: return "externref";
  case MVT::aarch64svcount:
    return "aarch64svcount";
  case MVT::spirvbuiltin:
    return "spirvbuiltin";
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void EVT::dump() const {
  print(dbgs());
  dbgs() << "\n";
}
#endif

/// getTypeForEVT - This method returns an LLVM type corresponding to the
/// specified EVT.  For integer types, this returns an unsigned type.  Note
/// that this will abort for types that cannot be represented.
Type *EVT::getTypeForEVT(LLVMContext &Context) const {
  // clang-format off
  switch (V.SimpleTy) {
  default:
    assert(isExtended() && "Type is not extended!");
    return LLVMTy;
  case MVT::isVoid:  return Type::getVoidTy(Context);
  case MVT::x86mmx:  return Type::getX86_MMXTy(Context);
  case MVT::aarch64svcount:
    return TargetExtType::get(Context, "aarch64.svcount");
  case MVT::x86amx:  return Type::getX86_AMXTy(Context);
  case MVT::i64x8:   return IntegerType::get(Context, 512);
  case MVT::externref: return Type::getWasm_ExternrefTy(Context);
  case MVT::funcref: return Type::getWasm_FuncrefTy(Context);
  case MVT::Metadata: return Type::getMetadataTy(Context);
#define GET_VT_EVT(Ty, EVT) case MVT::Ty: return EVT;
#include "llvm/CodeGen/GenVT.inc"
#undef GET_VT_EVT
  }
  // clang-format on
}

/// Return the value type corresponding to the specified type.
/// If HandleUnknown is true, unknown types are returned as Other, otherwise
/// they are invalid.
/// NB: This includes pointer types, which require a DataLayout to convert
/// to a concrete value type.
MVT MVT::getVT(Type *Ty, bool HandleUnknown){
  assert(Ty != nullptr && "Invalid type");
  switch (Ty->getTypeID()) {
  default:
    if (HandleUnknown) return MVT(MVT::Other);
    llvm_unreachable("Unknown type!");
  case Type::VoidTyID:
    return MVT::isVoid;
  case Type::IntegerTyID:
    return getIntegerVT(cast<IntegerType>(Ty)->getBitWidth());
  case Type::HalfTyID:      return MVT(MVT::f16);
  case Type::BFloatTyID:    return MVT(MVT::bf16);
  case Type::FloatTyID:     return MVT(MVT::f32);
  case Type::DoubleTyID:    return MVT(MVT::f64);
  case Type::X86_FP80TyID:  return MVT(MVT::f80);
  case Type::X86_MMXTyID:   return MVT(MVT::x86mmx);
  case Type::TargetExtTyID: {
    TargetExtType *TargetExtTy = cast<TargetExtType>(Ty);
    if (TargetExtTy->getName() == "aarch64.svcount")
      return MVT(MVT::aarch64svcount);
    else if (TargetExtTy->getName().starts_with("spirv."))
      return MVT(MVT::spirvbuiltin);
    if (HandleUnknown)
      return MVT(MVT::Other);
    llvm_unreachable("Unknown target ext type!");
  }
  case Type::X86_AMXTyID:   return MVT(MVT::x86amx);
  case Type::FP128TyID:     return MVT(MVT::f128);
  case Type::PPC_FP128TyID: return MVT(MVT::ppcf128);
  case Type::FixedVectorTyID:
  case Type::ScalableVectorTyID: {
    VectorType *VTy = cast<VectorType>(Ty);
    return getVectorVT(
      getVT(VTy->getElementType(), /*HandleUnknown=*/ false),
            VTy->getElementCount());
  }
  }
}

/// getEVT - Return the value type corresponding to the specified type.
/// If HandleUnknown is true, unknown types are returned as Other, otherwise
/// they are invalid.
/// NB: This includes pointer types, which require a DataLayout to convert
/// to a concrete value type.
EVT EVT::getEVT(Type *Ty, bool HandleUnknown){
  switch (Ty->getTypeID()) {
  default:
    return MVT::getVT(Ty, HandleUnknown);
  case Type::TokenTyID:
    return MVT::Untyped;
  case Type::IntegerTyID:
    return getIntegerVT(Ty->getContext(), cast<IntegerType>(Ty)->getBitWidth());
  case Type::FixedVectorTyID:
  case Type::ScalableVectorTyID: {
    VectorType *VTy = cast<VectorType>(Ty);
    return getVectorVT(Ty->getContext(),
                       getEVT(VTy->getElementType(), /*HandleUnknown=*/ false),
                       VTy->getElementCount());
  }
  }
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void MVT::dump() const {
  print(dbgs());
  dbgs() << "\n";
}
#endif

void MVT::print(raw_ostream &OS) const {
  if (SimpleTy == INVALID_SIMPLE_VALUE_TYPE)
    OS << "invalid";
  else
    OS << EVT(*this).getEVTString();
}

