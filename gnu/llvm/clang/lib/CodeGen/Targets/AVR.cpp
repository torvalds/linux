//===- AVR.cpp ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABIInfoImpl.h"
#include "TargetInfo.h"
#include "clang/Basic/DiagnosticFrontend.h"

using namespace clang;
using namespace clang::CodeGen;

//===----------------------------------------------------------------------===//
// AVR ABI Implementation. Documented at
// https://gcc.gnu.org/wiki/avr-gcc#Calling_Convention
// https://gcc.gnu.org/wiki/avr-gcc#Reduced_Tiny
//===----------------------------------------------------------------------===//

namespace {
class AVRABIInfo : public DefaultABIInfo {
private:
  // The total amount of registers can be used to pass parameters. It is 18 on
  // AVR, or 6 on AVRTiny.
  const unsigned ParamRegs;
  // The total amount of registers can be used to pass return value. It is 8 on
  // AVR, or 4 on AVRTiny.
  const unsigned RetRegs;

public:
  AVRABIInfo(CodeGenTypes &CGT, unsigned NPR, unsigned NRR)
      : DefaultABIInfo(CGT), ParamRegs(NPR), RetRegs(NRR) {}

  ABIArgInfo classifyReturnType(QualType Ty, bool &LargeRet) const {
    // On AVR, a return struct with size less than or equals to 8 bytes is
    // returned directly via registers R18-R25. On AVRTiny, a return struct
    // with size less than or equals to 4 bytes is returned directly via
    // registers R22-R25.
    if (isAggregateTypeForABI(Ty) &&
        getContext().getTypeSize(Ty) <= RetRegs * 8)
      return ABIArgInfo::getDirect();
    // A return value (struct or scalar) with larger size is returned via a
    // stack slot, along with a pointer as the function's implicit argument.
    if (getContext().getTypeSize(Ty) > RetRegs * 8) {
      LargeRet = true;
      return getNaturalAlignIndirect(Ty);
    }
    // An i8 return value should not be extended to i16, since AVR has 8-bit
    // registers.
    if (Ty->isIntegralOrEnumerationType() && getContext().getTypeSize(Ty) <= 8)
      return ABIArgInfo::getDirect();
    // Otherwise we follow the default way which is compatible.
    return DefaultABIInfo::classifyReturnType(Ty);
  }

  ABIArgInfo classifyArgumentType(QualType Ty, unsigned &NumRegs) const {
    unsigned TySize = getContext().getTypeSize(Ty);

    // An int8 type argument always costs two registers like an int16.
    if (TySize == 8 && NumRegs >= 2) {
      NumRegs -= 2;
      return ABIArgInfo::getExtend(Ty);
    }

    // If the argument size is an odd number of bytes, round up the size
    // to the next even number.
    TySize = llvm::alignTo(TySize, 16);

    // Any type including an array/struct type can be passed in rgisters,
    // if there are enough registers left.
    if (TySize <= NumRegs * 8) {
      NumRegs -= TySize / 8;
      return ABIArgInfo::getDirect();
    }

    // An argument is passed either completely in registers or completely in
    // memory. Since there are not enough registers left, current argument
    // and all other unprocessed arguments should be passed in memory.
    // However we still need to return `ABIArgInfo::getDirect()` other than
    // `ABIInfo::getNaturalAlignIndirect(Ty)`, otherwise an extra stack slot
    // will be allocated, so the stack frame layout will be incompatible with
    // avr-gcc.
    NumRegs = 0;
    return ABIArgInfo::getDirect();
  }

  void computeInfo(CGFunctionInfo &FI) const override {
    // Decide the return type.
    bool LargeRet = false;
    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType(), LargeRet);

    // Decide each argument type. The total number of registers can be used for
    // arguments depends on several factors:
    // 1. Arguments of varargs functions are passed on the stack. This applies
    //    even to the named arguments. So no register can be used.
    // 2. Total 18 registers can be used on avr and 6 ones on avrtiny.
    // 3. If the return type is a struct with too large size, two registers
    //    (out of 18/6) will be cost as an implicit pointer argument.
    unsigned NumRegs = ParamRegs;
    if (FI.isVariadic())
      NumRegs = 0;
    else if (LargeRet)
      NumRegs -= 2;
    for (auto &I : FI.arguments())
      I.info = classifyArgumentType(I.type, NumRegs);
  }
};

class AVRTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  AVRTargetCodeGenInfo(CodeGenTypes &CGT, unsigned NPR, unsigned NRR)
      : TargetCodeGenInfo(std::make_unique<AVRABIInfo>(CGT, NPR, NRR)) {}

  LangAS getGlobalVarAddressSpace(CodeGenModule &CGM,
                                  const VarDecl *D) const override {
    // Check if global/static variable is defined in address space
    // 1~6 (__flash, __flash1, __flash2, __flash3, __flash4, __flash5)
    // but not constant.
    if (D) {
      LangAS AS = D->getType().getAddressSpace();
      if (isTargetAddressSpace(AS) && 1 <= toTargetAddressSpace(AS) &&
          toTargetAddressSpace(AS) <= 6 && !D->getType().isConstQualified())
        CGM.getDiags().Report(D->getLocation(),
                              diag::err_verify_nonconst_addrspace)
            << "__flash*";
    }
    return TargetCodeGenInfo::getGlobalVarAddressSpace(CGM, D);
  }

  void setTargetAttributes(const Decl *D, llvm::GlobalValue *GV,
                           CodeGen::CodeGenModule &CGM) const override {
    if (GV->isDeclaration())
      return;
    const auto *FD = dyn_cast_or_null<FunctionDecl>(D);
    if (!FD) return;
    auto *Fn = cast<llvm::Function>(GV);

    if (FD->getAttr<AVRInterruptAttr>())
      Fn->addFnAttr("interrupt");

    if (FD->getAttr<AVRSignalAttr>())
      Fn->addFnAttr("signal");
  }
};
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createAVRTargetCodeGenInfo(CodeGenModule &CGM, unsigned NPR,
                                    unsigned NRR) {
  return std::make_unique<AVRTargetCodeGenInfo>(CGM.getTypes(), NPR, NRR);
}
