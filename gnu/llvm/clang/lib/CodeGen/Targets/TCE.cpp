//===- TCE.cpp ------------------------------------------------------------===//
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
// TCE ABI Implementation (see http://tce.cs.tut.fi). Uses mostly the defaults.
// Currently subclassed only to implement custom OpenCL C function attribute
// handling.
//===----------------------------------------------------------------------===//

namespace {

class TCETargetCodeGenInfo : public TargetCodeGenInfo {
public:
  TCETargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<DefaultABIInfo>(CGT)) {}

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &M) const override;
};

void TCETargetCodeGenInfo::setTargetAttributes(
    const Decl *D, llvm::GlobalValue *GV, CodeGen::CodeGenModule &M) const {
  if (GV->isDeclaration())
    return;
  const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D);
  if (!FD) return;

  llvm::Function *F = cast<llvm::Function>(GV);

  if (M.getLangOpts().OpenCL) {
    if (FD->hasAttr<OpenCLKernelAttr>()) {
      // OpenCL C Kernel functions are not subject to inlining
      F->addFnAttr(llvm::Attribute::NoInline);
      const ReqdWorkGroupSizeAttr *Attr = FD->getAttr<ReqdWorkGroupSizeAttr>();
      if (Attr) {
        // Convert the reqd_work_group_size() attributes to metadata.
        llvm::LLVMContext &Context = F->getContext();
        llvm::NamedMDNode *OpenCLMetadata =
            M.getModule().getOrInsertNamedMetadata(
                "opencl.kernel_wg_size_info");

        SmallVector<llvm::Metadata *, 5> Operands;
        Operands.push_back(llvm::ConstantAsMetadata::get(F));

        Operands.push_back(
            llvm::ConstantAsMetadata::get(llvm::Constant::getIntegerValue(
                M.Int32Ty, llvm::APInt(32, Attr->getXDim()))));
        Operands.push_back(
            llvm::ConstantAsMetadata::get(llvm::Constant::getIntegerValue(
                M.Int32Ty, llvm::APInt(32, Attr->getYDim()))));
        Operands.push_back(
            llvm::ConstantAsMetadata::get(llvm::Constant::getIntegerValue(
                M.Int32Ty, llvm::APInt(32, Attr->getZDim()))));

        // Add a boolean constant operand for "required" (true) or "hint"
        // (false) for implementing the work_group_size_hint attr later.
        // Currently always true as the hint is not yet implemented.
        Operands.push_back(
            llvm::ConstantAsMetadata::get(llvm::ConstantInt::getTrue(Context)));
        OpenCLMetadata->addOperand(llvm::MDNode::get(Context, Operands));
      }
    }
  }
}

}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createTCETargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<TCETargetCodeGenInfo>(CGM.getTypes());
}
