//===- PNaCl.cpp ----------------------------------------------------------===//
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
// le32/PNaCl bitcode ABI Implementation
//
// This is a simplified version of the x86_32 ABI.  Arguments and return values
// are always passed on the stack.
//===----------------------------------------------------------------------===//

class PNaClABIInfo : public ABIInfo {
 public:
  PNaClABIInfo(CodeGen::CodeGenTypes &CGT) : ABIInfo(CGT) {}

  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyArgumentType(QualType RetTy) const;

  void computeInfo(CGFunctionInfo &FI) const override;
  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override;
};

class PNaClTargetCodeGenInfo : public TargetCodeGenInfo {
 public:
   PNaClTargetCodeGenInfo(CodeGen::CodeGenTypes &CGT)
       : TargetCodeGenInfo(std::make_unique<PNaClABIInfo>(CGT)) {}
};

void PNaClABIInfo::computeInfo(CGFunctionInfo &FI) const {
  if (!getCXXABI().classifyReturnType(FI))
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType());

  for (auto &I : FI.arguments())
    I.info = classifyArgumentType(I.type);
}

RValue PNaClABIInfo::EmitVAArg(CodeGenFunction &CGF, Address VAListAddr,
                               QualType Ty, AggValueSlot Slot) const {
  // The PNaCL ABI is a bit odd, in that varargs don't use normal
  // function classification. Structs get passed directly for varargs
  // functions, through a rewriting transform in
  // pnacl-llvm/lib/Transforms/NaCl/ExpandVarArgs.cpp, which allows
  // this target to actually support a va_arg instructions with an
  // aggregate type, unlike other targets.
  return CGF.EmitLoadOfAnyValue(
      CGF.MakeAddrLValue(
          EmitVAArgInstr(CGF, VAListAddr, Ty, ABIArgInfo::getDirect()), Ty),
      Slot);
}

/// Classify argument of given type \p Ty.
ABIArgInfo PNaClABIInfo::classifyArgumentType(QualType Ty) const {
  if (isAggregateTypeForABI(Ty)) {
    if (CGCXXABI::RecordArgABI RAA = getRecordArgABI(Ty, getCXXABI()))
      return getNaturalAlignIndirect(Ty, RAA == CGCXXABI::RAA_DirectInMemory);
    return getNaturalAlignIndirect(Ty);
  } else if (const EnumType *EnumTy = Ty->getAs<EnumType>()) {
    // Treat an enum type as its underlying type.
    Ty = EnumTy->getDecl()->getIntegerType();
  } else if (Ty->isFloatingType()) {
    // Floating-point types don't go inreg.
    return ABIArgInfo::getDirect();
  } else if (const auto *EIT = Ty->getAs<BitIntType>()) {
    // Treat bit-precise integers as integers if <= 64, otherwise pass
    // indirectly.
    if (EIT->getNumBits() > 64)
      return getNaturalAlignIndirect(Ty);
    return ABIArgInfo::getDirect();
  }

  return (isPromotableIntegerTypeForABI(Ty) ? ABIArgInfo::getExtend(Ty)
                                            : ABIArgInfo::getDirect());
}

ABIArgInfo PNaClABIInfo::classifyReturnType(QualType RetTy) const {
  if (RetTy->isVoidType())
    return ABIArgInfo::getIgnore();

  // In the PNaCl ABI we always return records/structures on the stack.
  if (isAggregateTypeForABI(RetTy))
    return getNaturalAlignIndirect(RetTy);

  // Treat bit-precise integers as integers if <= 64, otherwise pass indirectly.
  if (const auto *EIT = RetTy->getAs<BitIntType>()) {
    if (EIT->getNumBits() > 64)
      return getNaturalAlignIndirect(RetTy);
    return ABIArgInfo::getDirect();
  }

  // Treat an enum type as its underlying type.
  if (const EnumType *EnumTy = RetTy->getAs<EnumType>())
    RetTy = EnumTy->getDecl()->getIntegerType();

  return (isPromotableIntegerTypeForABI(RetTy) ? ABIArgInfo::getExtend(RetTy)
                                               : ABIArgInfo::getDirect());
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createPNaClTargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<PNaClTargetCodeGenInfo>(CGM.getTypes());
}
