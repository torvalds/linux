//===- M68k.cpp -----------------------------------------------------------===//
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
// M68k ABI Implementation
//===----------------------------------------------------------------------===//

namespace {

class M68kTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  M68kTargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<DefaultABIInfo>(CGT)) {}
  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &M) const override;
};

} // namespace

void M68kTargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGen::CodeGenModule &M) const {
  if (const auto *FD = dyn_cast_or_null<FunctionDecl>(D)) {
    if (const auto *attr = FD->getAttr<M68kInterruptAttr>()) {
      // Handle 'interrupt' attribute:
      llvm::Function *F = cast<llvm::Function>(GV);

      // Step 1: Set ISR calling convention.
      F->setCallingConv(llvm::CallingConv::M68k_INTR);

      // Step 2: Add attributes goodness.
      F->addFnAttr(llvm::Attribute::NoInline);

      // Step 3: Emit ISR vector alias.
      unsigned Num = attr->getNumber() / 2;
      llvm::GlobalAlias::create(llvm::Function::ExternalLinkage,
                                "__isr_" + Twine(Num), F);
    }
  }
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createM68kTargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<M68kTargetCodeGenInfo>(CGM.getTypes());
}
