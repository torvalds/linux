//===-- TargetInfo/AMDGPUTargetInfo.cpp - TargetInfo for AMDGPU -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
//
//===----------------------------------------------------------------------===//

#include "TargetInfo/AMDGPUTargetInfo.h"
#include "llvm/MC/TargetRegistry.h"

using namespace llvm;

/// The target for R600 GPUs.
Target &llvm::getTheR600Target() {
  static Target TheAMDGPUTarget;
  return TheAMDGPUTarget;
}

/// The target for GCN GPUs.
Target &llvm::getTheGCNTarget() {
  static Target TheGCNTarget;
  return TheGCNTarget;
}

/// Extern function to initialize the targets for the AMDGPU backend
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeAMDGPUTargetInfo() {
  RegisterTarget<Triple::r600, false> R600(getTheR600Target(), "r600",
                                           "AMD GPUs HD2XXX-HD6XXX", "AMDGPU");
  RegisterTarget<Triple::amdgcn, false> GCN(getTheGCNTarget(), "amdgcn",
                                            "AMD GCN GPUs", "AMDGPU");
}
