//===- BPF.cpp ------------------------------------------------------------===//
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
// BPF ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class BPFABIInfo : public DefaultABIInfo {
public:
  BPFABIInfo(CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

  ABIArgInfo classifyArgumentType(QualType Ty) const {
    Ty = useFirstFieldIfTransparentUnion(Ty);

    if (isAggregateTypeForABI(Ty)) {
      uint64_t Bits = getContext().getTypeSize(Ty);
      if (Bits == 0)
        return ABIArgInfo::getIgnore();

      // If the aggregate needs 1 or 2 registers, do not use reference.
      if (Bits <= 128) {
        llvm::Type *CoerceTy;
        if (Bits <= 64) {
          CoerceTy =
              llvm::IntegerType::get(getVMContext(), llvm::alignTo(Bits, 8));
        } else {
          llvm::Type *RegTy = llvm::IntegerType::get(getVMContext(), 64);
          CoerceTy = llvm::ArrayType::get(RegTy, 2);
        }
        return ABIArgInfo::getDirect(CoerceTy);
      } else {
        return getNaturalAlignIndirect(Ty);
      }
    }

    if (const EnumType *EnumTy = Ty->getAs<EnumType>())
      Ty = EnumTy->getDecl()->getIntegerType();

    ASTContext &Context = getContext();
    if (const auto *EIT = Ty->getAs<BitIntType>())
      if (EIT->getNumBits() > Context.getTypeSize(Context.Int128Ty))
        return getNaturalAlignIndirect(Ty);

    return (isPromotableIntegerTypeForABI(Ty) ? ABIArgInfo::getExtend(Ty)
                                              : ABIArgInfo::getDirect());
  }

  ABIArgInfo classifyReturnType(QualType RetTy) const {
    if (RetTy->isVoidType())
      return ABIArgInfo::getIgnore();

    if (isAggregateTypeForABI(RetTy))
      return getNaturalAlignIndirect(RetTy);

    // Treat an enum type as its underlying type.
    if (const EnumType *EnumTy = RetTy->getAs<EnumType>())
      RetTy = EnumTy->getDecl()->getIntegerType();

    ASTContext &Context = getContext();
    if (const auto *EIT = RetTy->getAs<BitIntType>())
      if (EIT->getNumBits() > Context.getTypeSize(Context.Int128Ty))
        return getNaturalAlignIndirect(RetTy);

    // Caller will do necessary sign/zero extension.
    return ABIArgInfo::getDirect();
  }

  void computeInfo(CGFunctionInfo &FI) const override {
    FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
    for (auto &I : FI.arguments())
      I.info = classifyArgumentType(I.type);
  }

};

class BPFTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  BPFTargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<BPFABIInfo>(CGT)) {}
};

}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createBPFTargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<BPFTargetCodeGenInfo>(CGM.getTypes());
}
