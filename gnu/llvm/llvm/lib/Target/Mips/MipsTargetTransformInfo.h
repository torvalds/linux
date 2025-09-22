//===-- MipsTargetTransformInfo.h - Mips specific TTI -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_MIPS_MIPSTARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_MIPS_MIPSTARGETTRANSFORMINFO_H

#include "MipsTargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"

namespace llvm {

class MipsTTIImpl : public BasicTTIImplBase<MipsTTIImpl> {
  using BaseT = BasicTTIImplBase<MipsTTIImpl>;
  using TTI = TargetTransformInfo;

  friend BaseT;

  const MipsSubtarget *ST;
  const MipsTargetLowering *TLI;

  const MipsSubtarget *getST() const { return ST; }
  const MipsTargetLowering *getTLI() const { return TLI; }

public:
  explicit MipsTTIImpl(const MipsTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}

  bool hasDivRemOp(Type *DataType, bool IsSigned);
};

} // end namespace llvm

#endif
