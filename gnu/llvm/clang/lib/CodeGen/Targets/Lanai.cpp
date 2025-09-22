//===- Lanai.cpp ----------------------------------------------------------===//
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
// Lanai ABI Implementation
//===----------------------------------------------------------------------===//

namespace {
class LanaiABIInfo : public DefaultABIInfo {
  struct CCState {
    unsigned FreeRegs;
  };

public:
  LanaiABIInfo(CodeGen::CodeGenTypes &CGT) : DefaultABIInfo(CGT) {}

  bool shouldUseInReg(QualType Ty, CCState &State) const;

  void computeInfo(CGFunctionInfo &FI) const override {
    CCState State;
    // Lanai uses 4 registers to pass arguments unless the function has the
    // regparm attribute set.
    if (FI.getHasRegParm()) {
      State.FreeRegs = FI.getRegParm();
    } else {
      State.FreeRegs = 4;
    }

    if (!getCXXABI().classifyReturnType(FI))
      FI.getReturnInfo() = classifyReturnType(FI.getReturnType());
    for (auto &I : FI.arguments())
      I.info = classifyArgumentType(I.type, State);
  }

  ABIArgInfo getIndirectResult(QualType Ty, bool ByVal, CCState &State) const;
  ABIArgInfo classifyArgumentType(QualType RetTy, CCState &State) const;
};
} // end anonymous namespace

bool LanaiABIInfo::shouldUseInReg(QualType Ty, CCState &State) const {
  unsigned Size = getContext().getTypeSize(Ty);
  unsigned SizeInRegs = llvm::alignTo(Size, 32U) / 32U;

  if (SizeInRegs == 0)
    return false;

  if (SizeInRegs > State.FreeRegs) {
    State.FreeRegs = 0;
    return false;
  }

  State.FreeRegs -= SizeInRegs;

  return true;
}

ABIArgInfo LanaiABIInfo::getIndirectResult(QualType Ty, bool ByVal,
                                           CCState &State) const {
  if (!ByVal) {
    if (State.FreeRegs) {
      --State.FreeRegs; // Non-byval indirects just use one pointer.
      return getNaturalAlignIndirectInReg(Ty);
    }
    return getNaturalAlignIndirect(Ty, false);
  }

  // Compute the byval alignment.
  const unsigned MinABIStackAlignInBytes = 4;
  unsigned TypeAlign = getContext().getTypeAlign(Ty) / 8;
  return ABIArgInfo::getIndirect(CharUnits::fromQuantity(4), /*ByVal=*/true,
                                 /*Realign=*/TypeAlign >
                                     MinABIStackAlignInBytes);
}

ABIArgInfo LanaiABIInfo::classifyArgumentType(QualType Ty,
                                              CCState &State) const {
  // Check with the C++ ABI first.
  const RecordType *RT = Ty->getAs<RecordType>();
  if (RT) {
    CGCXXABI::RecordArgABI RAA = getRecordArgABI(RT, getCXXABI());
    if (RAA == CGCXXABI::RAA_Indirect) {
      return getIndirectResult(Ty, /*ByVal=*/false, State);
    } else if (RAA == CGCXXABI::RAA_DirectInMemory) {
      return getNaturalAlignIndirect(Ty, /*ByVal=*/true);
    }
  }

  if (isAggregateTypeForABI(Ty)) {
    // Structures with flexible arrays are always indirect.
    if (RT && RT->getDecl()->hasFlexibleArrayMember())
      return getIndirectResult(Ty, /*ByVal=*/true, State);

    // Ignore empty structs/unions.
    if (isEmptyRecord(getContext(), Ty, true))
      return ABIArgInfo::getIgnore();

    llvm::LLVMContext &LLVMContext = getVMContext();
    unsigned SizeInRegs = (getContext().getTypeSize(Ty) + 31) / 32;
    if (SizeInRegs <= State.FreeRegs) {
      llvm::IntegerType *Int32 = llvm::Type::getInt32Ty(LLVMContext);
      SmallVector<llvm::Type *, 3> Elements(SizeInRegs, Int32);
      llvm::Type *Result = llvm::StructType::get(LLVMContext, Elements);
      State.FreeRegs -= SizeInRegs;
      return ABIArgInfo::getDirectInReg(Result);
    } else {
      State.FreeRegs = 0;
    }
    return getIndirectResult(Ty, true, State);
  }

  // Treat an enum type as its underlying type.
  if (const auto *EnumTy = Ty->getAs<EnumType>())
    Ty = EnumTy->getDecl()->getIntegerType();

  bool InReg = shouldUseInReg(Ty, State);

  // Don't pass >64 bit integers in registers.
  if (const auto *EIT = Ty->getAs<BitIntType>())
    if (EIT->getNumBits() > 64)
      return getIndirectResult(Ty, /*ByVal=*/true, State);

  if (isPromotableIntegerTypeForABI(Ty)) {
    if (InReg)
      return ABIArgInfo::getDirectInReg();
    return ABIArgInfo::getExtend(Ty);
  }
  if (InReg)
    return ABIArgInfo::getDirectInReg();
  return ABIArgInfo::getDirect();
}

namespace {
class LanaiTargetCodeGenInfo : public TargetCodeGenInfo {
public:
  LanaiTargetCodeGenInfo(CodeGen::CodeGenTypes &CGT)
      : TargetCodeGenInfo(std::make_unique<LanaiABIInfo>(CGT)) {}
};
}

std::unique_ptr<TargetCodeGenInfo>
CodeGen::createLanaiTargetCodeGenInfo(CodeGenModule &CGM) {
  return std::make_unique<LanaiTargetCodeGenInfo>(CGM.getTypes());
}
