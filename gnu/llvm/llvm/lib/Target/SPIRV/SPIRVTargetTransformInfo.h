//===- SPIRVTargetTransformInfo.h - SPIR-V specific TTI ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// This file contains a TargetTransformInfo::Concept conforming object specific
// to the SPIRV target machine. It uses the target's detailed information to
// provide more precise answers to certain TTI queries, while letting the
// target independent and default TTI implementations handle the rest.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SPIRV_SPIRVTARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_SPIRV_SPIRVTARGETTRANSFORMINFO_H

#include "SPIRV.h"
#include "SPIRVTargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"

namespace llvm {
class SPIRVTTIImpl : public BasicTTIImplBase<SPIRVTTIImpl> {
  using BaseT = BasicTTIImplBase<SPIRVTTIImpl>;

  friend BaseT;

  const SPIRVSubtarget *ST;
  const SPIRVTargetLowering *TLI;

  const TargetSubtargetInfo *getST() const { return ST; }
  const SPIRVTargetLowering *getTLI() const { return TLI; }

public:
  explicit SPIRVTTIImpl(const SPIRVTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_SPIRV_SPIRVTARGETTRANSFORMINFO_H
