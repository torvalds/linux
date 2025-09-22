//===-- TargetInfo/AMDGPUTargetInfo.h - TargetInfo for AMDGPU ---*- C++ -*-===//
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

#ifndef LLVM_LIB_TARGET_AMDGPU_TARGETINFO_AMDGPUTARGETINFO_H
#define LLVM_LIB_TARGET_AMDGPU_TARGETINFO_AMDGPUTARGETINFO_H

namespace llvm {

class Target;

/// The target for R600 GPUs.
Target &getTheR600Target();

/// The target for GCN GPUs.
Target &getTheGCNTarget();

}

#endif // LLVM_LIB_TARGET_AMDGPU_TARGETINFO_AMDGPUTARGETINFO_H
