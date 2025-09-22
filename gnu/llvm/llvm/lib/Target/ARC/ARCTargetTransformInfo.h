//===- ARCTargetTransformInfo.h - ARC specific TTI --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// \file
// This file contains a TargetTransformInfo::Concept conforming object specific
// to the ARC target machine. It uses the target's detailed information to
// provide more precise answers to certain TTI queries, while letting the
// target independent and default TTI implementations handle the rest.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARC_ARCTARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_ARC_ARCTARGETTRANSFORMINFO_H

#include "ARC.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"

namespace llvm {

class ARCSubtarget;
class ARCTargetLowering;
class ARCTargetMachine;

class ARCTTIImpl : public BasicTTIImplBase<ARCTTIImpl> {
  using BaseT = BasicTTIImplBase<ARCTTIImpl>;
  friend BaseT;

  const ARCSubtarget *ST;
  const ARCTargetLowering *TLI;

  const ARCSubtarget *getST() const { return ST; }
  const ARCTargetLowering *getTLI() const { return TLI; }

public:
  explicit ARCTTIImpl(const ARCTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl()),
        TLI(ST->getTargetLowering()) {}

  // Provide value semantics. MSVC requires that we spell all of these out.
  ARCTTIImpl(const ARCTTIImpl &Arg)
      : BaseT(static_cast<const BaseT &>(Arg)), ST(Arg.ST), TLI(Arg.TLI) {}
  ARCTTIImpl(ARCTTIImpl &&Arg)
      : BaseT(std::move(static_cast<BaseT &>(Arg))), ST(std::move(Arg.ST)),
        TLI(std::move(Arg.TLI)) {}
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARC_ARCTARGETTRANSFORMINFO_H
