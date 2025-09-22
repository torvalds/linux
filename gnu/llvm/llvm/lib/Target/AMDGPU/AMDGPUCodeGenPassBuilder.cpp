//===- lib/Target/AMDGPU/AMDGPUCodeGenPassBuilder.cpp ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AMDGPUCodeGenPassBuilder.h"
#include "AMDGPUISelDAGToDAG.h"
#include "AMDGPUTargetMachine.h"
#include "llvm/Analysis/UniformityAnalysis.h"

using namespace llvm;

AMDGPUCodeGenPassBuilder::AMDGPUCodeGenPassBuilder(
    AMDGPUTargetMachine &TM, const CGPassBuilderOption &Opts,
    PassInstrumentationCallbacks *PIC)
    : CodeGenPassBuilder(TM, Opts, PIC) {
  Opt.RequiresCodeGenSCCOrder = true;
  // Exceptions and StackMaps are not supported, so these passes will never do
  // anything.
  // Garbage collection is not supported.
  disablePass<StackMapLivenessPass, FuncletLayoutPass,
              ShadowStackGCLoweringPass>();
}

void AMDGPUCodeGenPassBuilder::addPreISel(AddIRPass &addPass) const {
  // TODO: Add passes pre instruction selection.
  // Test only, convert to real IR passes in future.
  addPass(RequireAnalysisPass<UniformityInfoAnalysis, Function>());
}

void AMDGPUCodeGenPassBuilder::addAsmPrinter(AddMachinePass &addPass,
                                             CreateMCStreamer) const {
  // TODO: Add AsmPrinter.
}

Error AMDGPUCodeGenPassBuilder::addInstSelector(AddMachinePass &addPass) const {
  addPass(AMDGPUISelDAGToDAGPass(TM));
  return Error::success();
}
