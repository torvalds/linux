//===-- R600MachineFunctionInfo.h - R600 Machine Function Info ----*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_R600MACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_AMDGPU_R600MACHINEFUNCTIONINFO_H

#include "AMDGPUMachineFunction.h"

namespace llvm {

class R600Subtarget;

class R600MachineFunctionInfo final : public AMDGPUMachineFunction {
public:
  R600MachineFunctionInfo(const Function &F, const R600Subtarget *STI);
  unsigned CFStackSize;
};

} // End llvm namespace

#endif
