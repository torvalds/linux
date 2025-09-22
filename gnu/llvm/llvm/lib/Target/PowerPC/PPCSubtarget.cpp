//===-- PowerPCSubtarget.cpp - PPC Subtarget Information ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the PPC specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "PPCSubtarget.h"
#include "GISel/PPCCallLowering.h"
#include "GISel/PPCLegalizerInfo.h"
#include "GISel/PPCRegisterBankInfo.h"
#include "PPC.h"
#include "PPCRegisterInfo.h"
#include "PPCTargetMachine.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelect.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetMachine.h"
#include <cstdlib>

using namespace llvm;

#define DEBUG_TYPE "ppc-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "PPCGenSubtargetInfo.inc"

static cl::opt<bool>
    EnableMachinePipeliner("ppc-enable-pipeliner",
                           cl::desc("Enable Machine Pipeliner for PPC"),
                           cl::init(false), cl::Hidden);

PPCSubtarget &PPCSubtarget::initializeSubtargetDependencies(StringRef CPU,
                                                            StringRef TuneCPU,
                                                            StringRef FS) {
  initializeEnvironment();
  initSubtargetFeatures(CPU, TuneCPU, FS);
  return *this;
}

PPCSubtarget::PPCSubtarget(const Triple &TT, const std::string &CPU,
                           const std::string &TuneCPU, const std::string &FS,
                           const PPCTargetMachine &TM)
    : PPCGenSubtargetInfo(TT, CPU, TuneCPU, FS), TargetTriple(TT),
      IsPPC64(TargetTriple.getArch() == Triple::ppc64 ||
              TargetTriple.getArch() == Triple::ppc64le),
      TM(TM), FrameLowering(initializeSubtargetDependencies(CPU, TuneCPU, FS)),
      InstrInfo(*this), TLInfo(TM, *this) {
  CallLoweringInfo.reset(new PPCCallLowering(*getTargetLowering()));
  Legalizer.reset(new PPCLegalizerInfo(*this));
  auto *RBI = new PPCRegisterBankInfo(*getRegisterInfo());
  RegBankInfo.reset(RBI);

  InstSelector.reset(createPPCInstructionSelector(TM, *this, *RBI));
}

void PPCSubtarget::initializeEnvironment() {
  StackAlignment = Align(16);
  CPUDirective = PPC::DIR_NONE;
  HasPOPCNTD = POPCNTD_Unavailable;
}

void PPCSubtarget::initSubtargetFeatures(StringRef CPU, StringRef TuneCPU,
                                         StringRef FS) {
  // Determine default and user specified characteristics
  std::string CPUName = std::string(CPU);
  if (CPUName.empty() || CPU == "generic") {
    // If cross-compiling with -march=ppc64le without -mcpu
    if (TargetTriple.getArch() == Triple::ppc64le)
      CPUName = "ppc64le";
    else if (TargetTriple.getSubArch() == Triple::PPCSubArch_spe)
      CPUName = "e500";
    else
      CPUName = "generic";
  }

  // Determine the CPU to schedule for.
  if (TuneCPU.empty()) TuneCPU = CPUName;

  // Initialize scheduling itinerary for the specified CPU.
  InstrItins = getInstrItineraryForCPU(CPUName);

  // Parse features string.
  ParseSubtargetFeatures(CPUName, TuneCPU, FS);

  // If the user requested use of 64-bit regs, but the cpu selected doesn't
  // support it, ignore.
  if (IsPPC64 && has64BitSupport())
    Use64BitRegs = true;

  if (TargetTriple.isPPC32SecurePlt())
    IsSecurePlt = true;

  if (HasSPE && IsPPC64)
    report_fatal_error( "SPE is only supported for 32-bit targets.\n", false);
  if (HasSPE && (HasAltivec || HasVSX || HasFPU))
    report_fatal_error(
        "SPE and traditional floating point cannot both be enabled.\n", false);

  // If not SPE, set standard FPU
  if (!HasSPE)
    HasFPU = true;

  StackAlignment = getPlatformStackAlignment();

  // Determine endianness.
  IsLittleEndian = TM.isLittleEndian();

  if (HasAIXSmallLocalExecTLS || HasAIXSmallLocalDynamicTLS) {
    if (!TargetTriple.isOSAIX() || !IsPPC64)
      report_fatal_error("The aix-small-local-[exec|dynamic]-tls attribute is "
                         "only supported on AIX in "
                         "64-bit mode.\n",
                         false);
    // The aix-small-local-[exec|dynamic]-tls attribute should only be used with
    // -data-sections, as having data sections turned off with this option
    // is not ideal for performance. Moreover, the
    // small-local-[exec|dynamic]-tls region is a limited resource, and should
    // not be used for variables that may be replaced.
    if (!TM.getDataSections())
      report_fatal_error("The aix-small-local-[exec|dynamic]-tls attribute can "
                         "only be specified with "
                         "-data-sections.\n",
                         false);
  }

  if (HasAIXShLibTLSModelOpt && (!TargetTriple.isOSAIX() || !IsPPC64))
    report_fatal_error("The aix-shared-lib-tls-model-opt attribute "
                       "is only supported on AIX in 64-bit mode.\n",
                       false);
}

bool PPCSubtarget::enableMachineScheduler() const { return true; }

bool PPCSubtarget::enableMachinePipeliner() const {
  return getSchedModel().hasInstrSchedModel() && EnableMachinePipeliner;
}

bool PPCSubtarget::useDFAforSMS() const { return false; }

// This overrides the PostRAScheduler bit in the SchedModel for each CPU.
bool PPCSubtarget::enablePostRAScheduler() const { return true; }

PPCGenSubtargetInfo::AntiDepBreakMode PPCSubtarget::getAntiDepBreakMode() const {
  return TargetSubtargetInfo::ANTIDEP_ALL;
}

void PPCSubtarget::getCriticalPathRCs(RegClassVector &CriticalPathRCs) const {
  CriticalPathRCs.clear();
  CriticalPathRCs.push_back(isPPC64() ?
                            &PPC::G8RCRegClass : &PPC::GPRCRegClass);
}

void PPCSubtarget::overrideSchedPolicy(MachineSchedPolicy &Policy,
                                       unsigned NumRegionInstrs) const {
  // The GenericScheduler that we use defaults to scheduling bottom up only.
  // We want to schedule from both the top and the bottom and so we set
  // OnlyBottomUp to false.
  // We want to do bi-directional scheduling since it provides a more balanced
  // schedule leading to better performance.
  Policy.OnlyBottomUp = false;
  // Spilling is generally expensive on all PPC cores, so always enable
  // register-pressure tracking.
  Policy.ShouldTrackPressure = true;
}

bool PPCSubtarget::useAA() const {
  return true;
}

bool PPCSubtarget::enableSubRegLiveness() const { return true; }

bool PPCSubtarget::isGVIndirectSymbol(const GlobalValue *GV) const {
  if (isAIXABI()) {
    if (const GlobalVariable *GVar = dyn_cast<GlobalVariable>(GV))
      // On AIX the only symbols that aren't indirect are toc-data.
      return !GVar->hasAttribute("toc-data");

    return true;
  }

  // Large code model always uses the TOC even for local symbols.
  if (TM.getCodeModel() == CodeModel::Large)
    return true;

  if (TM.shouldAssumeDSOLocal(GV))
    return false;
  return true;
}

CodeModel::Model PPCSubtarget::getCodeModel(const TargetMachine &TM,
                                            const GlobalValue *GV) const {
  // If there isn't an attribute to override the module code model
  // this will be the effective code model.
  CodeModel::Model ModuleModel = TM.getCodeModel();

  // Initially support per global code model for AIX only.
  if (!isAIXABI())
    return ModuleModel;

  // Only GlobalVariables carry an attribute which can override the module code
  // model.
  assert(GV && "Unexpected NULL GlobalValue");
  const GlobalVariable *GlobalVar =
      [](const GlobalValue *GV) -> const GlobalVariable * {
    const GlobalVariable *Var = dyn_cast<GlobalVariable>(GV);
    if (Var)
      return Var;

    const GlobalAlias *Alias = dyn_cast<GlobalAlias>(GV);
    if (Alias)
      return dyn_cast<GlobalVariable>(Alias->getAliaseeObject());

    return nullptr;
  }(GV);

  if (!GlobalVar)
    return ModuleModel;

  std::optional<CodeModel::Model> MaybeCodeModel = GlobalVar->getCodeModel();
  if (MaybeCodeModel) {
    CodeModel::Model CM = *MaybeCodeModel;
    assert((CM == CodeModel::Small || CM == CodeModel::Large) &&
           "invalid code model for AIX");
    return CM;
  }

  return ModuleModel;
}

bool PPCSubtarget::isELFv2ABI() const { return TM.isELFv2ABI(); }
bool PPCSubtarget::isPPC64() const { return TM.isPPC64(); }

bool PPCSubtarget::isUsingPCRelativeCalls() const {
  return isPPC64() && hasPCRelativeMemops() && isELFv2ABI() &&
         CodeModel::Medium == getTargetMachine().getCodeModel();
}

// GlobalISEL
const CallLowering *PPCSubtarget::getCallLowering() const {
  return CallLoweringInfo.get();
}

const RegisterBankInfo *PPCSubtarget::getRegBankInfo() const {
  return RegBankInfo.get();
}

const LegalizerInfo *PPCSubtarget::getLegalizerInfo() const {
  return Legalizer.get();
}

InstructionSelector *PPCSubtarget::getInstructionSelector() const {
  return InstSelector.get();
}
