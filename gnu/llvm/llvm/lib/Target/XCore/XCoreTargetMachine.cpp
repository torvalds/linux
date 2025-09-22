//===-- XCoreTargetMachine.cpp - Define TargetMachine for XCore -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "XCoreTargetMachine.h"
#include "MCTargetDesc/XCoreMCTargetDesc.h"
#include "TargetInfo/XCoreTargetInfo.h"
#include "XCore.h"
#include "XCoreMachineFunctionInfo.h"
#include "XCoreTargetObjectFile.h"
#include "XCoreTargetTransformInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"
#include <optional>

using namespace llvm;

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

static CodeModel::Model
getEffectiveXCoreCodeModel(std::optional<CodeModel::Model> CM) {
  if (CM) {
    if (*CM != CodeModel::Small && *CM != CodeModel::Large)
      report_fatal_error("Target only supports CodeModel Small or Large");
    return *CM;
  }
  return CodeModel::Small;
}

/// Create an ILP32 architecture model
///
XCoreTargetMachine::XCoreTargetMachine(const Target &T, const Triple &TT,
                                       StringRef CPU, StringRef FS,
                                       const TargetOptions &Options,
                                       std::optional<Reloc::Model> RM,
                                       std::optional<CodeModel::Model> CM,
                                       CodeGenOptLevel OL, bool JIT)
    : LLVMTargetMachine(
          T, "e-m:e-p:32:32-i1:8:32-i8:8:32-i16:16:32-i64:32-f64:32-a:0:32-n32",
          TT, CPU, FS, Options, getEffectiveRelocModel(RM),
          getEffectiveXCoreCodeModel(CM), OL),
      TLOF(std::make_unique<XCoreTargetObjectFile>()),
      Subtarget(TT, std::string(CPU), std::string(FS), *this) {
  initAsmInfo();
}

XCoreTargetMachine::~XCoreTargetMachine() = default;

namespace {

/// XCore Code Generator Pass Configuration Options.
class XCorePassConfig : public TargetPassConfig {
public:
  XCorePassConfig(XCoreTargetMachine &TM, PassManagerBase &PM)
    : TargetPassConfig(TM, PM) {}

  XCoreTargetMachine &getXCoreTargetMachine() const {
    return getTM<XCoreTargetMachine>();
  }

  void addIRPasses() override;
  bool addPreISel() override;
  bool addInstSelector() override;
  void addPreEmitPass() override;
};

} // end anonymous namespace

TargetPassConfig *XCoreTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new XCorePassConfig(*this, PM);
}

void XCorePassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());

  TargetPassConfig::addIRPasses();
}

bool XCorePassConfig::addPreISel() {
  addPass(createXCoreLowerThreadLocalPass());
  return false;
}

bool XCorePassConfig::addInstSelector() {
  addPass(createXCoreISelDag(getXCoreTargetMachine(), getOptLevel()));
  return false;
}

void XCorePassConfig::addPreEmitPass() {
  addPass(createXCoreFrameToArgsOffsetEliminationPass());
}

// Force static initialization.
extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeXCoreTarget() {
  RegisterTargetMachine<XCoreTargetMachine> X(getTheXCoreTarget());
  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeXCoreDAGToDAGISelLegacyPass(PR);
}

TargetTransformInfo
XCoreTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(XCoreTTIImpl(this, F));
}

MachineFunctionInfo *XCoreTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return XCoreFunctionInfo::create<XCoreFunctionInfo>(Allocator, F, STI);
}
