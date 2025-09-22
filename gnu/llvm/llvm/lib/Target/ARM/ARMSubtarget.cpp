//===-- ARMSubtarget.cpp - ARM Subtarget Information ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the ARM specific subclass of TargetSubtargetInfo.
//
//===----------------------------------------------------------------------===//

#include "ARM.h"

#include "ARMCallLowering.h"
#include "ARMFrameLowering.h"
#include "ARMInstrInfo.h"
#include "ARMLegalizerInfo.h"
#include "ARMRegisterBankInfo.h"
#include "ARMSubtarget.h"
#include "ARMTargetMachine.h"
#include "MCTargetDesc/ARMMCTargetDesc.h"
#include "Thumb1FrameLowering.h"
#include "Thumb1InstrInfo.h"
#include "Thumb2InstrInfo.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelect.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/ARMTargetParser.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

#define DEBUG_TYPE "arm-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "ARMGenSubtargetInfo.inc"

static cl::opt<bool>
UseFusedMulOps("arm-use-mulops",
               cl::init(true), cl::Hidden);

enum ITMode {
  DefaultIT,
  RestrictedIT
};

static cl::opt<ITMode>
    IT(cl::desc("IT block support"), cl::Hidden, cl::init(DefaultIT),
       cl::values(clEnumValN(DefaultIT, "arm-default-it",
                             "Generate any type of IT block"),
                  clEnumValN(RestrictedIT, "arm-restrict-it",
                             "Disallow complex IT blocks")));

/// ForceFastISel - Use the fast-isel, even for subtargets where it is not
/// currently supported (for testing only).
static cl::opt<bool>
ForceFastISel("arm-force-fast-isel",
               cl::init(false), cl::Hidden);

/// initializeSubtargetDependencies - Initializes using a CPU and feature string
/// so that we can use initializer lists for subtarget initialization.
ARMSubtarget &ARMSubtarget::initializeSubtargetDependencies(StringRef CPU,
                                                            StringRef FS) {
  initializeEnvironment();
  initSubtargetFeatures(CPU, FS);
  return *this;
}

ARMFrameLowering *ARMSubtarget::initializeFrameLowering(StringRef CPU,
                                                        StringRef FS) {
  ARMSubtarget &STI = initializeSubtargetDependencies(CPU, FS);
  if (STI.isThumb1Only())
    return (ARMFrameLowering *)new Thumb1FrameLowering(STI);

  return new ARMFrameLowering(STI);
}

ARMSubtarget::ARMSubtarget(const Triple &TT, const std::string &CPU,
                           const std::string &FS,
                           const ARMBaseTargetMachine &TM, bool IsLittle,
                           bool MinSize)
    : ARMGenSubtargetInfo(TT, CPU, /*TuneCPU*/ CPU, FS),
      UseMulOps(UseFusedMulOps), CPUString(CPU), OptMinSize(MinSize),
      IsLittle(IsLittle), TargetTriple(TT), Options(TM.Options), TM(TM),
      FrameLowering(initializeFrameLowering(CPU, FS)),
      // At this point initializeSubtargetDependencies has been called so
      // we can query directly.
      InstrInfo(isThumb1Only()
                    ? (ARMBaseInstrInfo *)new Thumb1InstrInfo(*this)
                    : !isThumb()
                          ? (ARMBaseInstrInfo *)new ARMInstrInfo(*this)
                          : (ARMBaseInstrInfo *)new Thumb2InstrInfo(*this)),
      TLInfo(TM, *this) {

  CallLoweringInfo.reset(new ARMCallLowering(*getTargetLowering()));
  Legalizer.reset(new ARMLegalizerInfo(*this));

  auto *RBI = new ARMRegisterBankInfo(*getRegisterInfo());

  // FIXME: At this point, we can't rely on Subtarget having RBI.
  // It's awkward to mix passing RBI and the Subtarget; should we pass
  // TII/TRI as well?
  InstSelector.reset(createARMInstructionSelector(TM, *this, *RBI));

  RegBankInfo.reset(RBI);
}

const CallLowering *ARMSubtarget::getCallLowering() const {
  return CallLoweringInfo.get();
}

InstructionSelector *ARMSubtarget::getInstructionSelector() const {
  return InstSelector.get();
}

const LegalizerInfo *ARMSubtarget::getLegalizerInfo() const {
  return Legalizer.get();
}

const RegisterBankInfo *ARMSubtarget::getRegBankInfo() const {
  return RegBankInfo.get();
}

bool ARMSubtarget::isXRaySupported() const {
  // We don't currently suppport Thumb, but Windows requires Thumb.
  return hasV6Ops() && hasARMOps() && !isTargetWindows();
}

void ARMSubtarget::initializeEnvironment() {
  // MCAsmInfo isn't always present (e.g. in opt) so we can't initialize this
  // directly from it, but we can try to make sure they're consistent when both
  // available.
  UseSjLjEH = (isTargetDarwin() && !isTargetWatchABI() &&
               Options.ExceptionModel == ExceptionHandling::None) ||
              Options.ExceptionModel == ExceptionHandling::SjLj;
  assert((!TM.getMCAsmInfo() ||
          (TM.getMCAsmInfo()->getExceptionHandlingType() ==
           ExceptionHandling::SjLj) == UseSjLjEH) &&
         "inconsistent sjlj choice between CodeGen and MC");
}

void ARMSubtarget::initSubtargetFeatures(StringRef CPU, StringRef FS) {
  if (CPUString.empty()) {
    CPUString = "generic";

    if (isTargetDarwin()) {
      StringRef ArchName = TargetTriple.getArchName();
      ARM::ArchKind AK = ARM::parseArch(ArchName);
      if (AK == ARM::ArchKind::ARMV7S)
        // Default to the Swift CPU when targeting armv7s/thumbv7s.
        CPUString = "swift";
      else if (AK == ARM::ArchKind::ARMV7K)
        // Default to the Cortex-a7 CPU when targeting armv7k/thumbv7k.
        // ARMv7k does not use SjLj exception handling.
        CPUString = "cortex-a7";
    }
  }

  // Insert the architecture feature derived from the target triple into the
  // feature string. This is important for setting features that are implied
  // based on the architecture version.
  std::string ArchFS = ARM_MC::ParseARMTriple(TargetTriple, CPUString);
  if (!FS.empty()) {
    if (!ArchFS.empty())
      ArchFS = (Twine(ArchFS) + "," + FS).str();
    else
      ArchFS = std::string(FS);
  }
  ParseSubtargetFeatures(CPUString, /*TuneCPU*/ CPUString, ArchFS);

  // FIXME: This used enable V6T2 support implicitly for Thumb2 mode.
  // Assert this for now to make the change obvious.
  assert(hasV6T2Ops() || !hasThumb2());

  if (genExecuteOnly()) {
    // Execute only support for >= v8-M Baseline requires movt support
    if (hasV8MBaselineOps())
      NoMovt = false;
    if (!hasV6MOps())
      report_fatal_error("Cannot generate execute-only code for this target");
  }

  // Keep a pointer to static instruction cost data for the specified CPU.
  SchedModel = getSchedModelForCPU(CPUString);

  // Initialize scheduling itinerary for the specified CPU.
  InstrItins = getInstrItineraryForCPU(CPUString);

  // FIXME: this is invalid for WindowsCE
  if (isTargetWindows())
    NoARM = true;

  if (isAAPCS_ABI())
    stackAlignment = Align(8);
  if (isTargetNaCl() || isAAPCS16_ABI())
    stackAlignment = Align(16);

  // FIXME: Completely disable sibcall for Thumb1 since ThumbRegisterInfo::
  // emitEpilogue is not ready for them. Thumb tail calls also use t2B, as
  // the Thumb1 16-bit unconditional branch doesn't have sufficient relocation
  // support in the assembler and linker to be used. This would need to be
  // fixed to fully support tail calls in Thumb1.
  //
  // For ARMv8-M, we /do/ implement tail calls.  Doing this is tricky for v8-M
  // baseline, since the LDM/POP instruction on Thumb doesn't take LR.  This
  // means if we need to reload LR, it takes extra instructions, which outweighs
  // the value of the tail call; but here we don't know yet whether LR is going
  // to be used. We take the optimistic approach of generating the tail call and
  // perhaps taking a hit if we need to restore the LR.

  // Thumb1 PIC calls to external symbols use BX, so they can be tail calls,
  // but we need to make sure there are enough registers; the only valid
  // registers are the 4 used for parameters.  We don't currently do this
  // case.

  SupportsTailCall = !isThumb1Only() || hasV8MBaselineOps();

  if (isTargetMachO() && isTargetIOS() && getTargetTriple().isOSVersionLT(5, 0))
    SupportsTailCall = false;

  switch (IT) {
  case DefaultIT:
    RestrictIT = false;
    break;
  case RestrictedIT:
    RestrictIT = true;
    break;
  }

  // NEON f32 ops are non-IEEE 754 compliant. Darwin is ok with it by default.
  const FeatureBitset &Bits = getFeatureBits();
  if ((Bits[ARM::ProcA5] || Bits[ARM::ProcA8]) && // Where this matters
      (Options.UnsafeFPMath || isTargetDarwin()))
    HasNEONForFP = true;

  if (isRWPI())
    ReserveR9 = true;

  // If MVEVectorCostFactor is still 0 (has not been set to anything else), default it to 2
  if (MVEVectorCostFactor == 0)
    MVEVectorCostFactor = 2;

  // FIXME: Teach TableGen to deal with these instead of doing it manually here.
  switch (ARMProcFamily) {
  case Others:
  case CortexA5:
    break;
  case CortexA7:
    LdStMultipleTiming = DoubleIssue;
    break;
  case CortexA8:
    LdStMultipleTiming = DoubleIssue;
    break;
  case CortexA9:
    LdStMultipleTiming = DoubleIssueCheckUnalignedAccess;
    PreISelOperandLatencyAdjustment = 1;
    break;
  case CortexA12:
    break;
  case CortexA15:
    MaxInterleaveFactor = 2;
    PreISelOperandLatencyAdjustment = 1;
    PartialUpdateClearance = 12;
    break;
  case CortexA17:
  case CortexA32:
  case CortexA35:
  case CortexA53:
  case CortexA55:
  case CortexA57:
  case CortexA72:
  case CortexA73:
  case CortexA75:
  case CortexA76:
  case CortexA77:
  case CortexA78:
  case CortexA78AE:
  case CortexA78C:
  case CortexA710:
  case CortexR4:
  case CortexR5:
  case CortexR7:
  case CortexM3:
  case CortexM7:
  case CortexR52:
  case CortexR52plus:
  case CortexX1:
  case CortexX1C:
    break;
  case Exynos:
    LdStMultipleTiming = SingleIssuePlusExtras;
    MaxInterleaveFactor = 4;
    if (!isThumb())
      PrefLoopLogAlignment = 3;
    break;
  case Kryo:
    break;
  case Krait:
    PreISelOperandLatencyAdjustment = 1;
    break;
  case NeoverseV1:
    break;
  case Swift:
    MaxInterleaveFactor = 2;
    LdStMultipleTiming = SingleIssuePlusExtras;
    PreISelOperandLatencyAdjustment = 1;
    PartialUpdateClearance = 12;
    break;
  }
}

bool ARMSubtarget::isTargetHardFloat() const { return TM.isTargetHardFloat(); }

bool ARMSubtarget::isAPCS_ABI() const {
  assert(TM.TargetABI != ARMBaseTargetMachine::ARM_ABI_UNKNOWN);
  return TM.TargetABI == ARMBaseTargetMachine::ARM_ABI_APCS;
}
bool ARMSubtarget::isAAPCS_ABI() const {
  assert(TM.TargetABI != ARMBaseTargetMachine::ARM_ABI_UNKNOWN);
  return TM.TargetABI == ARMBaseTargetMachine::ARM_ABI_AAPCS ||
         TM.TargetABI == ARMBaseTargetMachine::ARM_ABI_AAPCS16;
}
bool ARMSubtarget::isAAPCS16_ABI() const {
  assert(TM.TargetABI != ARMBaseTargetMachine::ARM_ABI_UNKNOWN);
  return TM.TargetABI == ARMBaseTargetMachine::ARM_ABI_AAPCS16;
}

bool ARMSubtarget::isROPI() const {
  return TM.getRelocationModel() == Reloc::ROPI ||
         TM.getRelocationModel() == Reloc::ROPI_RWPI;
}
bool ARMSubtarget::isRWPI() const {
  return TM.getRelocationModel() == Reloc::RWPI ||
         TM.getRelocationModel() == Reloc::ROPI_RWPI;
}

bool ARMSubtarget::isGVIndirectSymbol(const GlobalValue *GV) const {
  if (!TM.shouldAssumeDSOLocal(GV))
    return true;

  // 32 bit macho has no relocation for a-b if a is undefined, even if b is in
  // the section that is being relocated. This means we have to use o load even
  // for GVs that are known to be local to the dso.
  if (isTargetMachO() && TM.isPositionIndependent() &&
      (GV->isDeclarationForLinker() || GV->hasCommonLinkage()))
    return true;

  return false;
}

bool ARMSubtarget::isGVInGOT(const GlobalValue *GV) const {
  return isTargetELF() && TM.isPositionIndependent() && !GV->isDSOLocal();
}

unsigned ARMSubtarget::getMispredictionPenalty() const {
  return SchedModel.MispredictPenalty;
}

bool ARMSubtarget::enableMachineScheduler() const {
  // The MachineScheduler can increase register usage, so we use more high
  // registers and end up with more T2 instructions that cannot be converted to
  // T1 instructions. At least until we do better at converting to thumb1
  // instructions, on cortex-m at Oz where we are size-paranoid, don't use the
  // Machine scheduler, relying on the DAG register pressure scheduler instead.
  if (isMClass() && hasMinSize())
    return false;
  // Enable the MachineScheduler before register allocation for subtargets
  // with the use-misched feature.
  return useMachineScheduler();
}

bool ARMSubtarget::enableSubRegLiveness() const {
  // Enable SubRegLiveness for MVE to better optimize s subregs for mqpr regs
  // and q subregs for qqqqpr regs.
  return hasMVEIntegerOps();
}

bool ARMSubtarget::enableMachinePipeliner() const {
  // Enable the MachinePipeliner before register allocation for subtargets
  // with the use-mipipeliner feature.
  return getSchedModel().hasInstrSchedModel() && useMachinePipeliner();
}

bool ARMSubtarget::useDFAforSMS() const { return false; }

// This overrides the PostRAScheduler bit in the SchedModel for any CPU.
bool ARMSubtarget::enablePostRAScheduler() const {
  if (enableMachineScheduler())
    return false;
  if (disablePostRAScheduler())
    return false;
  // Thumb1 cores will generally not benefit from post-ra scheduling
  return !isThumb1Only();
}

bool ARMSubtarget::enablePostRAMachineScheduler() const {
  if (!enableMachineScheduler())
    return false;
  if (disablePostRAScheduler())
    return false;
  return !isThumb1Only();
}

bool ARMSubtarget::useStride4VFPs() const {
  // For general targets, the prologue can grow when VFPs are allocated with
  // stride 4 (more vpush instructions). But WatchOS uses a compact unwind
  // format which it's more important to get right.
  return isTargetWatchABI() ||
         (useWideStrideVFP() && !OptMinSize);
}

bool ARMSubtarget::useMovt() const {
  // NOTE Windows on ARM needs to use mov.w/mov.t pairs to materialise 32-bit
  // immediates as it is inherently position independent, and may be out of
  // range otherwise.
  return !NoMovt && hasV8MBaselineOps() &&
         (isTargetWindows() || !OptMinSize || genExecuteOnly());
}

bool ARMSubtarget::useFastISel() const {
  // Enable fast-isel for any target, for testing only.
  if (ForceFastISel)
    return true;

  // Limit fast-isel to the targets that are or have been tested.
  if (!hasV6Ops())
    return false;

  // Thumb2 support on iOS; ARM support on iOS, Linux and NaCl.
  return TM.Options.EnableFastISel &&
         ((isTargetMachO() && !isThumb1Only()) ||
          (isTargetLinux() && !isThumb()) || (isTargetNaCl() && !isThumb()));
}

unsigned ARMSubtarget::getGPRAllocationOrder(const MachineFunction &MF) const {
  // The GPR register class has multiple possible allocation orders, with
  // tradeoffs preferred by different sub-architectures and optimisation goals.
  // The allocation orders are:
  // 0: (the default tablegen order, not used)
  // 1: r14, r0-r13
  // 2: r0-r7
  // 3: r0-r7, r12, lr, r8-r11
  // Note that the register allocator will change this order so that
  // callee-saved registers are used later, as they require extra work in the
  // prologue/epilogue (though we sometimes override that).

  // For thumb1-only targets, only the low registers are allocatable.
  if (isThumb1Only())
    return 2;

  // Allocate low registers first, so we can select more 16-bit instructions.
  // We also (in ignoreCSRForAllocationOrder) override  the default behaviour
  // with regards to callee-saved registers, because pushing extra registers is
  // much cheaper (in terms of code size) than using high registers. After
  // that, we allocate r12 (doesn't need to be saved), lr (saving it means we
  // can return with the pop, don't need an extra "bx lr") and then the rest of
  // the high registers.
  if (isThumb2() && MF.getFunction().hasMinSize())
    return 3;

  // Otherwise, allocate in the default order, using LR first because saving it
  // allows a shorter epilogue sequence.
  return 1;
}

bool ARMSubtarget::ignoreCSRForAllocationOrder(const MachineFunction &MF,
                                               unsigned PhysReg) const {
  // To minimize code size in Thumb2, we prefer the usage of low regs (lower
  // cost per use) so we can  use narrow encoding. By default, caller-saved
  // registers (e.g. lr, r12) are always  allocated first, regardless of
  // their cost per use. When optForMinSize, we prefer the low regs even if
  // they are CSR because usually push/pop can be folded into existing ones.
  return isThumb2() && MF.getFunction().hasMinSize() &&
         ARM::GPRRegClass.contains(PhysReg);
}

bool ARMSubtarget::splitFramePointerPush(const MachineFunction &MF) const {
  const Function &F = MF.getFunction();
  if (!MF.getTarget().getMCAsmInfo()->usesWindowsCFI() ||
      !F.needsUnwindTableEntry())
    return false;
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MFI.hasVarSizedObjects() || getRegisterInfo()->hasStackRealignment(MF);
}
