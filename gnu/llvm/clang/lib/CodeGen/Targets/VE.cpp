//===- VE.cpp -------------------------------------------------------------===//
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
// VE ABI Implementation.
//
namespace {
class VEABIInfo : public DefaultABIInfo {
public:
  VEABIInfo(CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

private:
  ABIArgInfo classifyReturnType(QualType RetTy) const;
  ABIArgInfo classifyArgumentType(QualType RetTy) const;
  void computeInfo(CGFunctionInfo &FI) const override;
};
} // end anonymous namespace

ABIArgInfo VEABIInfo::classifyReturnType(QualType Ty) const {
  if (Ty->isAnyComplexType())
    return ABIArgInfo::getDirect();
  uint64_t Size = getContext().getTypeSize(Ty);
  if (Size < 64 && Ty->isIntegerType())
    return ABIArgInfo::getExtend(Ty);
  return DefaultABIInfo::classifyReturnType(Ty);
}

ABIArgInfo VEABIInfo::classifyArgumentType(QualType Ty) const {
  if (Ty->isAnyComplexType())
    return ABIArgInfo::getDirect();
  uint64_t Size = getContext().getTypeSize(Ty);
  if (Size < 64 && Ty->isIntegerType())
    return ABIArgInfo::getExtend(Ty);
  return DefaultABIInfo::classifyArgumentType(Ty);
}

void VEABIInfo::computeInfo(CGFunctionInfo &FI) const {
  FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
  for (auto &Arg : FI.arguments())
    Arg.info = classifyArgumentType(Arg.type);
}

namespace {
class VETargetCodeGenInfo : public TargetCodeGenInfo {
public:
  VETargetCodeGenInfo(CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<VEABIInfo>(CGT)) {}
  // VE ABI requires the arguments of variadic and prototype-less functions
  // are passed in both registers and memory.
  bool isNoProtoCallVariadic(const CallArgList &args,
                             const FunctionNoProtoType *fnType) const override {
    return true;
  }
};
} // end anonymous namespace

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createVETargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<VETargetCodeGenInfo>(CGM.getTypes());
}
