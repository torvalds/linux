//===- DirectXTargetTransformInfo.h - DirectX TTI ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_DIRECTX_DIRECTXTARGETTRANSFORMINFO_H
#define LLVM_DIRECTX_DIRECTXTARGETTRANSFORMINFO_H

#include "DirectXSubtarget.h"
#include "DirectXTargetMachine.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/IR/Function.h"

namespace llvm {
class DirectXTTIImpl : public BasicTTIImplBase<DirectXTTIImpl> {
  using BaseT = BasicTTIImplBase<DirectXTTIImpl>;
  using TTI = TargetTransformInfo;

  friend BaseT;

  const DirectXSubtarget *ST;
  const DirectXTargetLowering *TLI;

  const DirectXSubtarget *getST() const { return ST; }
  const DirectXTargetLowering *getTLI() const { return TLI; }

public:
  explicit DirectXTTIImpl(const DirectXTargetMachine *TM, const Function &F)
      : BaseT(TM, F.getDataLayout()), ST(TM->getSubtargetImpl(F)),
        TLI(ST->getTargetLowering()) {}
  unsigned getMinVectorRegisterBitWidth() const { return 32; }
};
} // namespace llvm

#endif // LLVM_DIRECTX_DIRECTXTARGETTRANSFORMINFO_H
