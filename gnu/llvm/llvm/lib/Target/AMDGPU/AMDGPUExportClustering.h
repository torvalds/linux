//===- AMDGPUExportClustering.h - AMDGPU Export Clustering ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUEXPORTCLUSTERING_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUEXPORTCLUSTERING_H

#include "llvm/CodeGen/ScheduleDAGMutation.h"
#include <memory>

namespace llvm {

std::unique_ptr<ScheduleDAGMutation> createAMDGPUExportClusteringDAGMutation();

} // namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_AMDGPUEXPORTCLUSTERING_H
