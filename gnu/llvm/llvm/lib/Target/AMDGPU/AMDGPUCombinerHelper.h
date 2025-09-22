//=== lib/CodeGen/GlobalISel/AMDGPUCombinerHelper.h -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This contains common combine transformations that may be used in a combine
/// pass.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUCOMBINERHELPER_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUCOMBINERHELPER_H

#include "llvm/CodeGen/GlobalISel/Combiner.h"
#include "llvm/CodeGen/GlobalISel/CombinerHelper.h"

using namespace llvm;

class AMDGPUCombinerHelper : public CombinerHelper {
public:
  using CombinerHelper::CombinerHelper;

  bool matchFoldableFneg(MachineInstr &MI, MachineInstr *&MatchInfo);
  void applyFoldableFneg(MachineInstr &MI, MachineInstr *&MatchInfo);

  bool matchExpandPromotedF16FMed3(MachineInstr &MI, Register Src0,
                                   Register Src1, Register Src2);
  void applyExpandPromotedF16FMed3(MachineInstr &MI, Register Src0,
                                   Register Src1, Register Src2);
};

#endif // LLVM_LIB_TARGET_AMDGPU_AMDGPUCOMBINERHELPER_H
