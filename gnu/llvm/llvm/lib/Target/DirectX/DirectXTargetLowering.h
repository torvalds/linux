//===-- DirectXTargetLowering.h - Define DX TargetLowering  -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the DirectX specific subclass of TargetLowering.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DIRECTX_DIRECTXTARGETLOWERING_H
#define LLVM_DIRECTX_DIRECTXTARGETLOWERING_H

#include "llvm/CodeGen/TargetLowering.h"

namespace llvm {

class DirectXSubtarget;
class DirectXTargetMachine;

class DirectXTargetLowering : public TargetLowering {
public:
  explicit DirectXTargetLowering(const DirectXTargetMachine &TM,
                                 const DirectXSubtarget &STI);
};

} // end namespace llvm

#endif // LLVM_DIRECTX_DIRECTXTARGETLOWERING_H
