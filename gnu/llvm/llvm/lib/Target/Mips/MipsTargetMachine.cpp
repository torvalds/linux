//===-- MipsTargetMachine.cpp - Define TargetMachine for Mips -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the info about Mips target spec.
//
//===----------------------------------------------------------------------===//

#include "MipsTargetMachine.h"
#include "MCTargetDesc/MipsABIInfo.h"
#include "MCTargetDesc/MipsMCTargetDesc.h"
#include "Mips.h"
#include "Mips16ISelDAGToDAG.h"
#include "MipsMachineFunction.h"
#include "MipsSEISelDAGToDAG.h"
#include "MipsSubtarget.h"
#include "MipsTargetObjectFile.h"
#include "MipsTargetTransformInfo.h"
#include "TargetInfo/MipsTargetInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/BasicTTIImpl.h"
#include "llvm/CodeGen/GlobalISel/CSEInfo.h"
#include "llvm/CodeGen/GlobalISel/IRTranslator.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelect.h"
#include "llvm/CodeGen/GlobalISel/Legalizer.h"
#include "llvm/CodeGen/GlobalISel/RegBankSelect.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetOptions.h"
#include <optional>
#include <string>

using namespace llvm;

#define DEBUG_TYPE "mips"

extern cl::opt<bool> FixLoongson2FBTB;
static cl::opt<bool>
    EnableMulMulFix("mfix4300", cl::init(false),
                    cl::desc("Enable the VR4300 mulmul bug fix."), cl::Hidden);

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeMipsTarget() {
  // Register the target.
  RegisterTargetMachine<MipsebTargetMachine> X(getTheMipsTarget());
  RegisterTargetMachine<MipselTargetMachine> Y(getTheMipselTarget());
  RegisterTargetMachine<MipsebTargetMachine> A(getTheMips64Target());
  RegisterTargetMachine<MipselTargetMachine> B(getTheMips64elTarget());

  PassRegistry *PR = PassRegistry::getPassRegistry();
  initializeGlobalISel(*PR);
  initializeMipsDelaySlotFillerPass(*PR);
  initializeMipsBranchExpansionPass(*PR);
  initializeMicroMipsSizeReducePass(*PR);
  initializeMipsPreLegalizerCombinerPass(*PR);
  initializeMipsPostLegalizerCombinerPass(*PR);
  initializeMipsMulMulBugFixPass(*PR);
  initializeMipsDAGToDAGISelLegacyPass(*PR);
}

static std::string computeDataLayout(const Triple &TT, StringRef CPU,
                                     const TargetOptions &Options,
                                     bool isLittle) {
  std::string Ret;
  MipsABIInfo ABI = MipsABIInfo::computeTargetABI(TT, CPU, Options.MCOptions);

  // There are both little and big endian mips.
  if (isLittle)
    Ret += "e";
  else
    Ret += "E";

  if (ABI.IsO32())
    Ret += "-m:m";
  else
    Ret += "-m:e";

  // Pointers are 32 bit on some ABIs.
  if (!ABI.IsN64())
    Ret += "-p:32:32";

  // 8 and 16 bit integers only need to have natural alignment, but try to
  // align them to 32 bits. 64 bit integers have natural alignment.
  Ret += "-i8:8:32-i16:16:32-i64:64";

  // 32 bit registers are always available and the stack is at least 64 bit
  // aligned. On N64 64 bit registers are also available and the stack is
  // 128 bit aligned.
  if (ABI.IsN64() || ABI.IsN32())
    Ret += "-n32:64-S128";
  else
    Ret += "-n32-S64";

  return Ret;
}

static Reloc::Model getEffectiveRelocModel(bool JIT,
                                           std::optional<Reloc::Model> RM) {
  if (!RM || JIT)
    return Reloc::Static;
  return *RM;
}

// On function prologue, the stack is created by decrementing
// its pointer. Once decremented, all references are done with positive
// offset from the stack/frame pointer, using StackGrowsUp enables
// an easier handling.
// Using CodeModel::Large enables different CALL behavior.
MipsTargetMachine::MipsTargetMachine(const Target &T, const Triple &TT,
                                     StringRef CPU, StringRef FS,
                                     const TargetOptions &Options,
                                     std::optional<Reloc::Model> RM,
                                     std::optional<CodeModel::Model> CM,
                                     CodeGenOptLevel OL, bool JIT,
                                     bool isLittle)
    : LLVMTargetMachine(T, computeDataLayout(TT, CPU, Options, isLittle), TT,
                        CPU, FS, Options, getEffectiveRelocModel(JIT, RM),
                        getEffectiveCodeModel(CM, CodeModel::Small), OL),
      isLittle(isLittle), TLOF(std::make_unique<MipsTargetObjectFile>()),
      ABI(MipsABIInfo::computeTargetABI(TT, CPU, Options.MCOptions)),
      Subtarget(nullptr),
      DefaultSubtarget(TT, CPU, FS, isLittle, *this, std::nullopt),
      NoMips16Subtarget(TT, CPU, FS.empty() ? "-mips16" : FS.str() + ",-mips16",
                        isLittle, *this, std::nullopt),
      Mips16Subtarget(TT, CPU, FS.empty() ? "+mips16" : FS.str() + ",+mips16",
                      isLittle, *this, std::nullopt) {
  Subtarget = &DefaultSubtarget;
  initAsmInfo();

  // Mips supports the debug entry values.
  setSupportsDebugEntryValues(true);
}

MipsTargetMachine::~MipsTargetMachine() = default;

void MipsebTargetMachine::anchor() {}

MipsebTargetMachine::MipsebTargetMachine(const Target &T, const Triple &TT,
                                         StringRef CPU, StringRef FS,
                                         const TargetOptions &Options,
                                         std::optional<Reloc::Model> RM,
                                         std::optional<CodeModel::Model> CM,
                                         CodeGenOptLevel OL, bool JIT)
    : MipsTargetMachine(T, TT, CPU, FS, Options, RM, CM, OL, JIT, false) {}

void MipselTargetMachine::anchor() {}

MipselTargetMachine::MipselTargetMachine(const Target &T, const Triple &TT,
                                         StringRef CPU, StringRef FS,
                                         const TargetOptions &Options,
                                         std::optional<Reloc::Model> RM,
                                         std::optional<CodeModel::Model> CM,
                                         CodeGenOptLevel OL, bool JIT)
    : MipsTargetMachine(T, TT, CPU, FS, Options, RM, CM, OL, JIT, true) {}

const MipsSubtarget *
MipsTargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

  std::string CPU =
      CPUAttr.isValid() ? CPUAttr.getValueAsString().str() : TargetCPU;
  std::string FS =
      FSAttr.isValid() ? FSAttr.getValueAsString().str() : TargetFS;
  bool hasMips16Attr = F.getFnAttribute("mips16").isValid();
  bool hasNoMips16Attr = F.getFnAttribute("nomips16").isValid();

  bool HasMicroMipsAttr = F.getFnAttribute("micromips").isValid();
  bool HasNoMicroMipsAttr = F.getFnAttribute("nomicromips").isValid();

  // FIXME: This is related to the code below to reset the target options,
  // we need to know whether or not the soft float flag is set on the
  // function, so we can enable it as a subtarget feature.
  bool softFloat = F.getFnAttribute("use-soft-float").getValueAsBool();

  if (hasMips16Attr)
    FS += FS.empty() ? "+mips16" : ",+mips16";
  else if (hasNoMips16Attr)
    FS += FS.empty() ? "-mips16" : ",-mips16";
  if (HasMicroMipsAttr)
    FS += FS.empty() ? "+micromips" : ",+micromips";
  else if (HasNoMicroMipsAttr)
    FS += FS.empty() ? "-micromips" : ",-micromips";
  if (softFloat)
    FS += FS.empty() ? "+soft-float" : ",+soft-float";

  auto &I = SubtargetMap[CPU + FS];
  if (!I) {
    // This needs to be done before we create a new subtarget since any
    // creation will depend on the TM and the code generation flags on the
    // function that reside in TargetOptions.
    resetTargetOptions(F);
    I = std::make_unique<MipsSubtarget>(
        TargetTriple, CPU, FS, isLittle, *this,
        MaybeAlign(F.getParent()->getOverrideStackAlignment()));
  }
  return I.get();
}

void MipsTargetMachine::resetSubtarget(MachineFunction *MF) {
  LLVM_DEBUG(dbgs() << "resetSubtarget\n");

  Subtarget = &MF->getSubtarget<MipsSubtarget>();
}

namespace {

/// Mips Code Generator Pass Configuration Options.
class MipsPassConfig : public TargetPassConfig {
public:
  MipsPassConfig(MipsTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {
    // The current implementation of long branch pass requires a scratch
    // register ($at) to be available before branch instructions. Tail merging
    // can break this requirement, so disable it when long branch pass is
    // enabled.
    EnableTailMerge = !getMipsSubtarget().enableLongBranchPass();
  }

  MipsTargetMachine &getMipsTargetMachine() const {
    return getTM<MipsTargetMachine>();
  }

  const MipsSubtarget &getMipsSubtarget() const {
    return *getMipsTargetMachine().getSubtargetImpl();
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  void addPreEmitPass() override;
  void addPreRegAlloc() override;
  bool addIRTranslator() override;
  void addPreLegalizeMachineIR() override;
  bool addLegalizeMachineIR() override;
  void addPreRegBankSelect() override;
  bool addRegBankSelect() override;
  bool addGlobalInstructionSelect() override;

  std::unique_ptr<CSEConfigBase> getCSEConfig() const override;
};

} // end anonymous namespace

TargetPassConfig *MipsTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new MipsPassConfig(*this, PM);
}

std::unique_ptr<CSEConfigBase> MipsPassConfig::getCSEConfig() const {
  return getStandardCSEConfigForOpt(TM->getOptLevel());
}

void MipsPassConfig::addIRPasses() {
  TargetPassConfig::addIRPasses();
  addPass(createAtomicExpandLegacyPass());
  if (getMipsSubtarget().os16())
    addPass(createMipsOs16Pass());
  if (getMipsSubtarget().inMips16HardFloat())
    addPass(createMips16HardFloatPass());
}
// Install an instruction selector pass using
// the ISelDag to gen Mips code.
bool MipsPassConfig::addInstSelector() {
  addPass(createMipsModuleISelDagPass());
  addPass(createMips16ISelDag(getMipsTargetMachine(), getOptLevel()));
  addPass(createMipsSEISelDag(getMipsTargetMachine(), getOptLevel()));
  return false;
}

void MipsPassConfig::addPreRegAlloc() {
  addPass(createMipsOptimizePICCallPass());

  if (FixLoongson2FBTB)
    addPass(createMipsLoongson2FBTBFix());
}

TargetTransformInfo
MipsTargetMachine::getTargetTransformInfo(const Function &F) const {
  if (Subtarget->allowMixed16_32()) {
    LLVM_DEBUG(errs() << "No Target Transform Info Pass Added\n");
    // FIXME: This is no longer necessary as the TTI returned is per-function.
    return TargetTransformInfo(F.getDataLayout());
  }

  LLVM_DEBUG(errs() << "Target Transform Info Pass Added\n");
  return TargetTransformInfo(MipsTTIImpl(this, F));
}

MachineFunctionInfo *MipsTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return MipsFunctionInfo::create<MipsFunctionInfo>(Allocator, F, STI);
}

// Implemented by targets that want to run passes immediately before
// machine code is emitted.
void MipsPassConfig::addPreEmitPass() {
  // Expand pseudo instructions that are sensitive to register allocation.
  addPass(createMipsExpandPseudoPass());

  // The microMIPS size reduction pass performs instruction reselection for
  // instructions which can be remapped to a 16 bit instruction.
  addPass(createMicroMipsSizeReducePass());

  // This pass inserts a nop instruction between two back-to-back multiplication
  // instructions when the "mfix4300" flag is passed.
  if (EnableMulMulFix)
    addPass(createMipsMulMulBugPass());

  // The delay slot filler pass can potientially create forbidden slot hazards
  // for MIPSR6 and therefore it should go before MipsBranchExpansion pass.
  addPass(createMipsDelaySlotFillerPass());

  // This pass expands branches and takes care about the forbidden slot hazards.
  // Expanding branches may potentially create forbidden slot hazards for
  // MIPSR6, and fixing such hazard may potentially break a branch by extending
  // its offset out of range. That's why this pass combine these two tasks, and
  // runs them alternately until one of them finishes without any changes. Only
  // then we can be sure that all branches are expanded properly and no hazards
  // exists.
  // Any new pass should go before this pass.
  addPass(createMipsBranchExpansion());

  addPass(createMipsConstantIslandPass());
}

bool MipsPassConfig::addIRTranslator() {
  addPass(new IRTranslator(getOptLevel()));
  return false;
}

void MipsPassConfig::addPreLegalizeMachineIR() {
  addPass(createMipsPreLegalizeCombiner());
}

bool MipsPassConfig::addLegalizeMachineIR() {
  addPass(new Legalizer());
  return false;
}

void MipsPassConfig::addPreRegBankSelect() {
  bool IsOptNone = getOptLevel() == CodeGenOptLevel::None;
  addPass(createMipsPostLegalizeCombiner(IsOptNone));
}

bool MipsPassConfig::addRegBankSelect() {
  addPass(new RegBankSelect());
  return false;
}

bool MipsPassConfig::addGlobalInstructionSelect() {
  addPass(new InstructionSelect(getOptLevel()));
  return false;
}
