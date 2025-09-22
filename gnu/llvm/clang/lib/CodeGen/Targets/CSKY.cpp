//===- CSKY.cpp -----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"

using namespace clang;
using namespace clang::CodeGen;

//===----------------------------------------------------------------------===//
// CSKY ABI Implementation
//===----------------------------------------------------------------------===//
namespace {
class CSKYABIInfo : public DefaultABIInfo {
  static const int NumArgGPRs = 4;
  static const int NumArgFPRs = 4;

  static const unsigned XLen = 32;
  unsigned FLen;

public:
  CSKYABIInfo(CodeGen::CodeGenTypes &CGT, unsigned FLen)
      : DefaultABIInfo(CGT), FLen(FLen) {}

  void computeInfo(CGFunctionInfo &FI) const override;
  ABIArgInfo classifyArgumentType(QualType Ty, int &ArgGPRsLeft,
                                  int &ArgFPRsLeft,
                                  bool isReturnType = false) const;
  ABIArgInfo classifyReturnType(QualType RetTy) const;

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;
};

} // end anonymous namespace

void CSKYABIInfo::computeInfo(CGFunctionInfo &FI) const {
  QualType RetTy = FI.getReturnType();
  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classifyReturnType(RetTy);

  bool IsRetIndirect = FI.getReturnInfo().getKind() == ABIArgInfo::Indirect;

  // We must track the number of GPRs used in order to conform to the CSKY
  // ABI, as integer scalars passed in registers should have signext/zeroext
  // when promoted.
  int ArgGPRsLeft = IsRetIndirect ? NumArgGPRs - 1 : NumArgGPRs;
  int ArgFPRsLeft = FLen ? NumArgFPRs : 0;

  for (auto &ArgInfo : FI.arguments()) {
    ArgInfo.info = classifyArgumentType(ArgInfo.type, ArgGPRsLeft, ArgFPRsLeft);
  }
}

RValue CSKYABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                              QualType Ty, AggValueSlot Slot) const {
  CharUnits SlotSize = CharUnits::fromQuantity(XLen / 8);

  // Empty records are ignored for parameter passing purposes.
  if (isEmptyRecord(getContext(), Ty, true))
    return Slot.asRValue();

  auto TInfo = getContext().getTypeInfoInChars(Ty);

  return emitVoidPtrVAArg(CGF, VAListAddr, Ty, false, TInfo, SlotSize,
                          /*AllowHigherAlign=*/true, Slot);
}

ABIArgInfo CSKYABIInfo::classifyArgumentType(QualType Ty, int &ArgGPRsLeft,
                                             int &ArgFPRsLeft,
                                             bool isReturnType) const {
  assert(ArgGPRsLeft <= NumArgGPRs && "Arg GPR tracking underflow");
  Ty = useFirstFieldIfTransparentUnion(Ty);

  // Structures with either a non-trivial destructor or a non-trivial
  // copy constructor are always passed indirectly.
  if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI())) {
    if (ArgGPRsLeft)
      ArgGPRsLeft -= 1;
    return getNaturalAlignIndirect(Ty, /*ByVal=*/RAA ==
                                           CGCXXABI::RAA_DirectInMemory);
  }

  // Ignore empty structs/unions.
  if (isEmptyRecord(getContext(), Ty, true))
    return ABIArgInfo::getIgnore();

  if (!Ty->getAsUnionType())
    if (const Type *SeltTy = isSingleElementStruct(Ty, getContext()))
      return ABIArgInfo::getDirect(CGT.ConvertType(QualType(SeltTy, 0)));

  uint64_t Size = getContext().getTypeSize(Ty);
  // Pass floating point values via FPRs if possible.
  if (Ty->isFloatingType() && !Ty->isComplexType() && FLen >= Size &&
      ArgFPRsLeft) {
    ArgFPRsLeft--;
    return ABIArgInfo::getDirect();
  }

  // Complex types for the hard float ABI must be passed direct rather than
  // using CoerceAndExpand.
  if (Ty->isComplexType() && FLen && !isReturnType) {
    QualType EltTy = Ty->castAs<ComplexType>()->getElementType();
    if (getContext().getTypeSize(EltTy) <= FLen) {
      ArgFPRsLeft -= 2;
      return ABIArgInfo::getDirect();
    }
  }

  if (!isAggregateTypeForABI(Ty)) {
    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = Ty->getAs<EnumType>())
      Ty = EnumTy->getDecl()->getIntegerType();

    // All integral types are promoted to XLen width, unless passed on the
    // stack.
    if (Size < XLen && Ty->isIntegralOrEnumerationType())
      return ABIArgInfo::getExtend(Ty);

    if (const auto *EIT = Ty->getAs<BitIntType>()) {
      if (EIT->getNumBits() < XLen)
        return ABIArgInfo::getExtend(Ty);
    }

    return ABIArgInfo::getDirect();
  }

  // For argument type, the first 4*XLen parts of aggregate will be passed
  // in registers, and the rest will be passed in stack.
  // So we can coerce to integers directly and let backend handle it correctly.
  // For return type, aggregate which <= 2*XLen will be returned in registers.
  // Otherwise, aggregate will be returned indirectly.
  if (!isReturnType || (isReturnType && Size <= 2 * XLen)) {
    if (Size <= XLen) {
      return ABIArgInfo::getDirect(
          llvm::IntegerType::get(getVMContext(), XLen));
    } else {
      return ABIArgInfo::getDirect(llvm::ArrayType::get(
          llvm::IntegerType::get(getVMContext(), XLen), (Size + 31) / XLen));
    }
  }
  return getNaturalAlignIndirect(Ty, /*ByVal=*/false);
}

ABIArgInfo CSKYABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  int ArgGPRsLeft = 2;
  int ArgFPRsLeft = FLen ? 1 : 0;

  // The rules for return and argument types are the same, so defer to
  // classifyArgumentType.
  return classifyArgumentType(RetTy, ArgGPRsLeft, ArgFPRsLeft, true);
}

namespace {
class CSKYTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  CSKYTargetCodeGenInfo(CodeGen::CodeGenTypes &CGT, unsigned FLen)
      : TargetCodeGenInfo(std::make_unique<CSKYABIInfo>(CGT, FLen)) {}
};
} // end anonymous namespace

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createCSKYTargetCodeGenInfo(CodeGenModule &CGM, unsigned FLen) {
  return std::make_unique<CSKYTargetCodeGenInfo>(CGM.getTypes(), FLen);
}
