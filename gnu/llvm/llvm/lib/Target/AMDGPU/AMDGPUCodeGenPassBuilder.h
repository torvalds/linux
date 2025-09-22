//===- lib/Target/AMDGPU/AMDGPUCodeGenPassBuilder.h -----------*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUCODEGENPASSBUILDER_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUCODEGENPASSBUILDER_H

#include "llvm/MC/MCStreamer.h"
#include "llvm/Passes/CodeGenPassBuilder.h"

namespace llvm {

class AMDGPUTargetMachine;

class AMDGPUCodeGenPassBuilder
    : public CodeGenPassBuilder<AMDGPUCodeGenPassBuilder, AMDGPUTargetMachine> {
public:
  AMDGPUCodeGenPassBuilder(AMDGPUTargetMachine &TM,
                           const CGPassBuilderOption &Opts,
                           PassInstrumentationCallbacks *PIC);

  void addPreISel(AddIRPass &addPass) const;
  void addAsmPrinter(AddMachinePass &, CreateMCStreamer) const;
  Error addInstSelector(AddMachinePass &) const;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_AMDGPUCODEGENPASSBUILDER_H
