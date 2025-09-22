//===- MSP430.cpp ---------------------------------------------------------===//
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
// MSP430 ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class MSP430ABIInfo : public DefaultABIInfo {
  static ABIArgInfo complexArgInfo() {
    ABIArgInfo Info = ABIArgInfo::getDirect();
    Info.setCanBeFlattened(false);
    return Info;
  }

public:
  MSP430ABIInfo(CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

  ABIArgInfo classifyReturnType(QualType RetTy) const {
    if (RetTy->isAnyComplexType())
      return complexArgInfo();

    return DefaultABIInfo::classifyReturnType(RetTy);
  }

  ABIArgInfo classifyArgumentType(QualType RetTy) const {
    if (RetTy->isAnyComplexType())
      return complexArgInfo();

    return DefaultABIInfo::classifyArgumentType(RetTy);
  }

  // Just copy the original implementations because
  // DefaultABIInfo::classify{Return,Argument}Type() are not virtual
  void computeInfo(CGFunctionInfo &FI) const override {
    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
    for (auto &I : FI.arguments())
      I.info = classifyArgumentType(I.type);
  }

  RValue EmitVAArg(CodeGenFunction &CGF, Address VAListAddr, QualType Ty,
                   AggValueSlot Slot) const override {
    return CGF.EmitLoadOfAnyValue(
        CGF.MakeAddrLValue(
            EmitVAArgInstr(CGF, VAListAddr, Ty, classifyArgumentType(Ty)), Ty),
        Slot);
  }
};

class MSP430TargetCodeGenInfo : public TargetCodeGenInfo {
public:
  MSP430TargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<MSP430ABIInfo>(CGT)) {}
  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &M) const override;
};

}

void MSP430TargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGen::CodeGenModule &M) const {
  if (GV->isDeclaration())
    return;
  if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D)) {
    const auto *InterruptAttr = FD->getAttr<MSP430InterruptAttr>();
    if (!InterruptAttr)
      return;

    // Handle 'interrupt' attribute:
    llvm::Function *F = cast<llvm::Function>(GV);

    // Step 1: Set ISR calling convention.
    F->setCallingConv(llvm::CallingConv::MSP430_INTR);

    // Step 2: Add attributes goodness.
    F->addFnAttr(llvm::Attribute::NoInline);
    F->addFnAttr("interrupt", llvm::utostr(InterruptAttr->getNumber()));
  }
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createMSP430TargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<MSP430TargetCodeGenInfo>(CGM.getTypes());
}
