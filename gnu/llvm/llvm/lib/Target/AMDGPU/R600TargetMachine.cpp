//===-- R600TargetMachine.cpp - TargetMachine for hw codegen targets-------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// The AMDGPU-R600 target machine contains all of the hardware specific
/// information  needed to emit code for R600 GPUs.
//
//===----------------------------------------------------------------------===//

#include "R600TargetMachine.h"
#include "AMDGPUTargetMachine.h"
#include "R600.h"
#include "R600CodeGenPassBuilder.h"
#include "R600MachineScheduler.h"
#include "R600TargetTransformInfo.h"
#include "llvm/Transforms/Scalar.h"
#include <optional>

using namespace llvm;

static cl::opt<bool>
    EnableR600StructurizeCFG("r600-ir-structurize",
                             cl::desc("Use StructurizeCFG IR pass"),
                             cl::init(true));

static cl::opt<bool> EnableR600IfConvert("r600-if-convert",
                                         cl::desc("Use if conversion pass"),
                                         cl::ReallyHidden, cl::init(true));

static cl::opt<bool, true> EnableAMDGPUFunctionCallsOpt(
    "amdgpu-function-calls", cl::desc("Enable AMDGPU function call support"),
    cl::location(AMDGPUTargetMachine::EnableFunctionCalls), cl::init(true),
    cl::Hidden);

static ScheduleDAGInstrs *createR600MachineScheduler(MachineSchedContext *C) {
  return new ScheduleDAGMILive(C, std::make_unique<R600SchedStrategy>());
}

static MachineSchedRegistry R600SchedRegistry("r600",
                                              "Run R600's custom scheduler",
                                              createR600MachineScheduler);

//===----------------------------------------------------------------------===//
// R600 Target Machine (R600 -> Cayman)
//===----------------------------------------------------------------------===//

R600TargetMachine::R600TargetMachine(const Target &T, const Triple &TT,
                                     StringRef CPU, StringRef FS,
                                     const TargetOptions &Options,
                                     std::optional<Reloc::Model> RM,
                                     std::optional<CodeModel::Model> CM,
                                     CodeGenOptLevel OL, bool JIT)
    : AMDGPUTargetMachine(T, TT, CPU, FS, Options, RM, CM, OL) {
  setRequiresStructuredCFG(true);

  // Override the default since calls aren't supported for r600.
  if (EnableFunctionCalls &&
      EnableAMDGPUFunctionCallsOpt.getNumOccurrences() == 0)
    EnableFunctionCalls = false;
}

const TargetSubtargetInfo *
R600TargetMachine::getSubtargetImpl(const Function &F) const {
  StringRef GPU = getGPUName(F);
  StringRef FS = getFeatureString(F);

  SmallString<128> SubtargetKey(GPU);
  SubtargetKey.append(FS);

  auto &I = SubtargetMap[SubtargetKey];
  if (!I) {
    // This needs to be done before we create a new subtarget since any
    // creation will depend on the TM and the code generation flags on the
    // function that reside in TargetOptions.
    resetTargetOptions(F);
    I = std::make_unique<R600Subtarget>(TargetTriple, GPU, FS, *this);
  }

  return I.get();
}

TargetTransformInfo
R600TargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(R600TTIImpl(this, F));
}

namespace {
class R600PassConfig final : public AMDGPUPassConfig {
public:
  R600PassConfig(LLVMTargetMachine &TM, PassManagerBase &PM)
      : AMDGPUPassConfig(TM, PM) {}

  ScheduleDAGInstrs *
  createMachineScheduler(MachineSchedContext *C) const override {
    return createR600MachineScheduler(C);
  }

  bool addPreISel() override;
  bool addInstSelector() override;
  void addPreRegAlloc() override;
  void addPreSched2() override;
  void addPreEmitPass() override;
};
} // namespace

//===----------------------------------------------------------------------===//
// R600 Pass Setup
//===----------------------------------------------------------------------===//

bool R600PassConfig::addPreISel() {
  AMDGPUPassConfig::addPreISel();

  if (EnableR600StructurizeCFG)
    addPass(createStructurizeCFGPass());
  return false;
}

bool R600PassConfig::addInstSelector() {
  addPass(createR600ISelDag(getAMDGPUTargetMachine(), getOptLevel()));
  return false;
}

void R600PassConfig::addPreRegAlloc() { addPass(createR600VectorRegMerger()); }

void R600PassConfig::addPreSched2() {
  addPass(createR600EmitClauseMarkers());
  if (EnableR600IfConvert)
    addPass(&IfConverterID);
  addPass(createR600ClauseMergePass());
}

void R600PassConfig::addPreEmitPass() {
  addPass(createR600MachineCFGStructurizerPass());
  addPass(createR600ExpandSpecialInstrsPass());
  addPass(&FinalizeMachineBundlesID);
  addPass(createR600Packetizer());
  addPass(createR600ControlFlowFinalizer());
}

TargetPassConfig *R600TargetMachine::createPassConfig(PassManagerBase &PM) {
  return new R600PassConfig(*this, PM);
}

Error R600TargetMachine::buildCodeGenPipeline(
    ModulePassManager &MPM, raw_pwrite_stream &Out, raw_pwrite_stream *DwoOut,
    CodeGenFileType FileType, const CGPassBuilderOption &Opts,
    PassInstrumentationCallbacks *PIC) {
  R600CodeGenPassBuilder CGPB(*this, Opts, PIC);
  return CGPB.buildPipeline(MPM, Out, DwoOut, FileType);
}
