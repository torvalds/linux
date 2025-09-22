//===-- M68kTargetMachine.cpp - M68k Target Machine -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains implementation for M68k target machine.
///
//===----------------------------------------------------------------------===//

#include "M68kTargetMachine.h"
#include "M68k.h"
#include "M68kMachineFunction.h"
#include "M68kSubtarget.h"
#include "M68kTargetObjectFile.h"
#include "TargetInfo/M68kTargetInfo.h"
#include "llvm/CodeGen/GlobalISel/IRTranslator.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelect.h"
#include "llvm/CodeGen/GlobalISel/Legalizer.h"
#include "llvm/CodeGen/GlobalISel/RegBankSelect.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/PassRegistry.h"
#include <memory>
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "m68k"

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeM68kTarget() {
  RegisterTargetMachine<M68kTargetMachine> X(getTheM68kTarget());
  auto *PR = PassRegistry::getPassRegistry();
  initializeGlobalISel(*PR);
  initializeM68kDAGToDAGISelLegacyPass(*PR);
  initializeM68kExpandPseudoPass(*PR);
  initializeM68kGlobalBaseRegPass(*PR);
  initializeM68kCollapseMOVEMPass(*PR);
}

namespace {

std::string computeDataLayout(const Triple &TT, StringRef CPU,
                              const TargetOptions &Options) {
  std::string Ret = "";
  // M68k is Big Endian
  Ret += "E";

  // FIXME how to wire it with the used object format?
  Ret += "-m:e";

  // M68k pointers are always 32 bit wide even for 16-bit CPUs.
  // The ABI only specifies 16-bit alignment.
  // On at least the 68020+ with a 32-bit bus, there is a performance benefit
  // to having 32-bit alignment.
  Ret += "-p:32:16:32";

  // Bytes do not require special alignment, words are word aligned and
  // long words are word aligned at minimum.
  Ret += "-i8:8:8-i16:16:16-i32:16:32";

  // FIXME no floats at the moment

  // The registers can hold 8, 16, 32 bits
  Ret += "-n8:16:32";

  Ret += "-a:0:16-S16";

  return Ret;
}

Reloc::Model getEffectiveRelocModel(const Triple &TT,
                                    std::optional<Reloc::Model> RM) {
  // If not defined we default to static
  if (!RM.has_value())
    return Reloc::Static;

  return *RM;
}

CodeModel::Model getEffectiveCodeModel(std::optional<CodeModel::Model> CM,
                                       bool JIT) {
  if (!CM) {
    return CodeModel::Small;
  } else if (CM == CodeModel::Large) {
    llvm_unreachable("Large code model is not supported");
  } else if (CM == CodeModel::Kernel) {
    llvm_unreachable("Kernel code model is not implemented yet");
  }
  return CM.value();
}
} // end anonymous namespace

M68kTargetMachine::M68kTargetMachine(const Target &T, const Triple &TT,
                                     StringRef CPU, StringRef FS,
                                     const TargetOptions &Options,
                                     std::optional<Reloc::Model> RM,
                                     std::optional<CodeModel::Model> CM,
                                     CodeGenOptLevel OL, bool JIT)
    : LLVMTargetMachine(T, computeDataLayout(TT, CPU, Options), TT, CPU, FS,
                        Options, getEffectiveRelocModel(TT, RM),
                        ::getEffectiveCodeModel(CM, JIT), OL),
      TLOF(std::make_unique<M68kELFTargetObjectFile>()),
      Subtarget(TT, CPU, FS, *this) {
  initAsmInfo();
}

M68kTargetMachine::~M68kTargetMachine() {}

const M68kSubtarget *
M68kTargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

  auto CPU = CPUAttr.isValid() ? CPUAttr.getValueAsString().str() : TargetCPU;
  auto FS = FSAttr.isValid() ? FSAttr.getValueAsString().str() : TargetFS;

  auto &I = SubtargetMap[CPU + FS];
  if (!I) {
    // This needs to be done before we create a new subtarget since any
    // creation will depend on the TM and the code generation flags on the
    // function that reside in TargetOptions.
    resetTargetOptions(F);
    I = std::make_unique<M68kSubtarget>(TargetTriple, CPU, FS, *this);
  }
  return I.get();
}

MachineFunctionInfo *M68kTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return M68kMachineFunctionInfo::create<M68kMachineFunctionInfo>(Allocator, F,
                                                                  STI);
}

//===----------------------------------------------------------------------===//
// Pass Pipeline Configuration
//===----------------------------------------------------------------------===//

namespace {
class M68kPassConfig : public TargetPassConfig {
public:
  M68kPassConfig(M68kTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  M68kTargetMachine &getM68kTargetMachine() const {
    return getTM<M68kTargetMachine>();
  }

  const M68kSubtarget &getM68kSubtarget() const {
    return *getM68kTargetMachine().getSubtargetImpl();
  }
  void addIRPasses() override;
  bool addIRTranslator() override;
  bool addLegalizeMachineIR() override;
  bool addRegBankSelect() override;
  bool addGlobalInstructionSelect() override;
  bool addInstSelector() override;
  void addPreSched2() override;
  void addPreEmitPass() override;
};
} // namespace

TargetPassConfig *M68kTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new M68kPassConfig(*this, PM);
}

void M68kPassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());
  TargetPassConfig::addIRPasses();
}

bool M68kPassConfig::addInstSelector() {
  // Install an instruction selector.
  addPass(createM68kISelDag(getM68kTargetMachine()));
  addPass(createM68kGlobalBaseRegPass());
  return false;
}

bool M68kPassConfig::addIRTranslator() {
  addPass(new IRTranslator());
  return false;
}

bool M68kPassConfig::addLegalizeMachineIR() {
  addPass(new Legalizer());
  return false;
}

bool M68kPassConfig::addRegBankSelect() {
  addPass(new RegBankSelect());
  return false;
}

bool M68kPassConfig::addGlobalInstructionSelect() {
  addPass(new InstructionSelect());
  return false;
}

void M68kPassConfig::addPreSched2() { addPass(createM68kExpandPseudoPass()); }

void M68kPassConfig::addPreEmitPass() {
  addPass(createM68kCollapseMOVEMPass());
}
