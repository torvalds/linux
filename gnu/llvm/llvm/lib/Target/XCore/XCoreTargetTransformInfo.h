//===-- XCoreTargetTransformInfo.h - XCore specific TTI ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file a TargetTransformInfo::Concept conforming object specific to the
/// XCore target machine. It uses the target's detailed information to
/// provide more precise answers to certain TTI queries, while letting the
/// target independent and default TTI implementations handle the rest.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_XCORE_XCORETARGETTRANSFORMINFO_H
#define LLVM_LIB_TARGET_XCORE_XCORETARGETTRANSFORMINFO_H

#include "XCore.h"
#include "XCoreTargetMachine.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {

class XCoreTTIImpl : public BasicTTIImplBase<XCoreTTIImpl> {
  typedef BasicTTIImplBase<XCoreTTIImpl> BaseT;
  typedef TargetTransformInfo TTI;
  friend BaseT;

  const XCoreSubtarget *ST;
  const XCoreTargetLowering *TLI;

  const XCoreSubtarget *getST() const { return ST; }
  const XCoreTargetLowering *getTLI() const { return TLI; }

public:
  explicit XCoreTTIImpl(const XCoreTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl()),
        TLI(ST->getTargetLowering()) {}

  unsigned getNumberOfRegisters(unsigned ClassID) const {
    bool Vector = (ClassID == 1);
    if (Vector) {
      return 0;
    }
    return 12;
  }
};

} // end namespace llvm

#endif
