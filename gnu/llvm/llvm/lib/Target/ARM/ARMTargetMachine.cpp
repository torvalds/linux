//===-- ARMTargetMachine.cpp - Define TargetMachine for ARM ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "ARMTargetMachine.h"
#include "ARM.h"
#include "ARMMachineFunctionInfo.h"
#include "ARMMacroFusion.h"
#include "ARMSubtarget.h"
#include "ARMTargetObjectFile.h"
#include "ARMTargetTransformInfo.h"
#include "MCTargetDesc/ARMMCTargetDesc.h"
#include "TargetInfo/ARMTargetInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/ExecutionDomainFix.h"
#include "llvm/CodeGen/GlobalISel/CSEInfo.h"
#include "llvm/CodeGen/GlobalISel/CallLowering.h"
#include "llvm/CodeGen/GlobalISel/IRTranslator.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelect.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/Legalizer.h"
#include "llvm/CodeGen/GlobalISel/LegalizerInfo.h"
#include "llvm/CodeGen/GlobalISel/RegBankSelect.h"
#include "llvm/CodeGen/MIRParser/MIParser.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegisterBankInfo.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Pass.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/ARMTargetParser.h"
#include "llvm/TargetParser/TargetParser.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/CFGuard.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Scalar.h"
#include <cassert>
#include <memory>
#include <optional>
#include <string>

using namespace llvm;

static cl::opt<bool>
DisableA15SDOptimization("disable-a15-sd-optimization", cl::Hidden,
                   cl::desc("Inhibit optimization of S->D register accesses on A15"),
                   cl::init(false));

static cl::opt<bool>
EnableAtomicTidy("arm-atomic-cfg-tidy", cl::Hidden,
                 cl::desc("Run SimplifyCFG after expanding atomic operations"
                          " to make use of cmpxchg flow-based information"),
                 cl::init(true));

static cl::opt<bool>
EnableARMLoadStoreOpt("arm-load-store-opt", cl::Hidden,
                      cl::desc("Enable ARM load/store optimization pass"),
                      cl::init(true));

// FIXME: Unify control over GlobalMerge.
static cl::opt<cl::boolOrDefault>
EnableGlobalMerge("arm-global-merge", cl::Hidden,
                  cl::desc("Enable the global merge pass"));

namespace llvm {
  void initializeARMExecutionDomainFixPass(PassRegistry&);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeARMTarget() {
  // Register the target.
  RegisterTargetMachine<ARMLETargetMachine> X(getTheARMLETarget());
  RegisterTargetMachine<ARMLETargetMachine> A(getTheThumbLETarget());
  RegisterTargetMachine<ARMBETargetMachine> Y(getTheARMBETarget());
  RegisterTargetMachine<ARMBETargetMachine> B(getTheThumbBETarget());

  PassRegistry &Registry = *PassRegistry::getPassRegistry();
  initializeGlobalISel(Registry);
  initializeARMLoadStoreOptPass(Registry);
  initializeARMPreAllocLoadStoreOptPass(Registry);
  initializeARMParallelDSPPass(Registry);
  initializeARMBranchTargetsPass(Registry);
  initializeARMConstantIslandsPass(Registry);
  initializeARMExecutionDomainFixPass(Registry);
  initializeARMExpandPseudoPass(Registry);
  initializeThumb2SizeReducePass(Registry);
  initializeMVEVPTBlockPass(Registry);
  initializeMVETPAndVPTOptimisationsPass(Registry);
  initializeMVETailPredicationPass(Registry);
  initializeARMLowOverheadLoopsPass(Registry);
  initializeARMBlockPlacementPass(Registry);
  initializeMVEGatherScatterLoweringPass(Registry);
  initializeARMSLSHardeningPass(Registry);
  initializeMVELaneInterleavingPass(Registry);
  initializeARMFixCortexA57AES1742098Pass(Registry);
  initializeARMDAGToDAGISelLegacyPass(Registry);
}

static std::unique_ptr<TargetLoweringObjectFile> createTLOF(const Triple &TT) {
  if (TT.isOSBinFormatMachO())
    return std::make_unique<TargetLoweringObjectFileMachO>();
  if (TT.isOSWindows())
    return std::make_unique<TargetLoweringObjectFileCOFF>();
  return std::make_unique<ARMElfTargetObjectFile>();
}

static ARMBaseTargetMachine::ARMABI
computeTargetABI(const Triple &TT, StringRef CPU,
                 const TargetOptions &Options) {
  StringRef ABIName = Options.MCOptions.getABIName();

  if (ABIName.empty())
    ABIName = ARM::computeDefaultTargetABI(TT, CPU);

  if (ABIName == "aapcs16")
    return ARMBaseTargetMachine::ARM_ABI_AAPCS16;
  else if (ABIName.starts_with("aapcs"))
    return ARMBaseTargetMachine::ARM_ABI_AAPCS;
  else if (ABIName.starts_with("apcs"))
    return ARMBaseTargetMachine::ARM_ABI_APCS;

  llvm_unreachable("Unhandled/unknown ABI Name!");
  return ARMBaseTargetMachine::ARM_ABI_UNKNOWN;
}

static std::string computeDataLayout(const Triple &TT, StringRef CPU,
                                     const TargetOptions &Options,
                                     bool isLittle) {
  auto ABI = computeTargetABI(TT, CPU, Options);
  std::string Ret;

  if (isLittle)
    // Little endian.
    Ret += "e";
  else
    // Big endian.
    Ret += "E";

  Ret += DataLayout::getManglingComponent(TT);

  // Pointers are 32 bits and aligned to 32 bits.
  Ret += "-p:32:32";

  // Function pointers are aligned to 8 bits (because the LSB stores the
  // ARM/Thumb state).
  Ret += "-Fi8";

  // ABIs other than APCS have 64 bit integers with natural alignment.
  if (ABI != ARMBaseTargetMachine::ARM_ABI_APCS)
    Ret += "-i64:64";

  // We have 64 bits floats. The APCS ABI requires them to be aligned to 32
  // bits, others to 64 bits. We always try to align to 64 bits.
  if (ABI == ARMBaseTargetMachine::ARM_ABI_APCS)
    Ret += "-f64:32:64";

  // We have 128 and 64 bit vectors. The APCS ABI aligns them to 32 bits, others
  // to 64. We always ty to give them natural alignment.
  if (ABI == ARMBaseTargetMachine::ARM_ABI_APCS)
    Ret += "-v64:32:64-v128:32:128";
  else if (ABI != ARMBaseTargetMachine::ARM_ABI_AAPCS16)
    Ret += "-v128:64:128";

  // Try to align aggregates to 32 bits (the default is 64 bits, which has no
  // particular hardware support on 32-bit ARM).
  Ret += "-a:0:32";

  // Integer registers are 32 bits.
  Ret += "-n32";

  // The stack is 128 bit aligned on NaCl, 64 bit aligned on AAPCS and 32 bit
  // aligned everywhere else.
  if (TT.isOSNaCl() || ABI == ARMBaseTargetMachine::ARM_ABI_AAPCS16)
    Ret += "-S128";
  else if (ABI == ARMBaseTargetMachine::ARM_ABI_AAPCS)
    Ret += "-S64";
  else
    Ret += "-S32";

  return Ret;
}

static Reloc::Model getEffectiveRelocModel(const Triple &TT,
                                           std::optional<Reloc::Model> RM) {
  if (!RM)
    // Default relocation model on Darwin is PIC.
    return TT.isOSBinFormatMachO() ? Reloc::PIC_ : Reloc::Static;

  if (*RM == Reloc::ROPI || *RM == Reloc::RWPI || *RM == Reloc::ROPI_RWPI)
    assert(TT.isOSBinFormatELF() &&
           "ROPI/RWPI currently only supported for ELF");

  // DynamicNoPIC is only used on darwin.
  if (*RM == Reloc::DynamicNoPIC && !TT.isOSDarwin())
    return Reloc::Static;

  return *RM;
}

/// Create an ARM architecture model.
///
ARMBaseTargetMachine::ARMBaseTargetMachine(const Target &T, const Triple &TT,
                                           StringRef CPU, StringRef FS,
                                           const TargetOptions &Options,
                                           std::optional<Reloc::Model> RM,
                                           std::optional<CodeModel::Model> CM,
                                           CodeGenOptLevel OL, bool isLittle)
    : LLVMTargetMachine(T, computeDataLayout(TT, CPU, Options, isLittle), TT,
                        CPU, FS, Options, getEffectiveRelocModel(TT, RM),
                        getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TargetABI(computeTargetABI(TT, CPU, Options)),
      TLOF(createTLOF(getTargetTriple())), isLittle(isLittle) {

  // Default to triple-appropriate float ABI
  if (Options.FloatABIType == FloatABI::Default) {
    if (isTargetHardFloat())
      this->Options.FloatABIType = FloatABI::Hard;
    else
      this->Options.FloatABIType = FloatABI::Soft;
  }

  // Default to triple-appropriate EABI
  if (Options.EABIVersion == EABI::Default ||
      Options.EABIVersion == EABI::Unknown) {
    // musl is compatible with glibc with regard to EABI version
    if ((TargetTriple.getEnvironment() == Triple::GNUEABI ||
         TargetTriple.getEnvironment() == Triple::GNUEABIT64 ||
         TargetTriple.getEnvironment() == Triple::GNUEABIHF ||
         TargetTriple.getEnvironment() == Triple::GNUEABIHFT64 ||
         TargetTriple.getEnvironment() == Triple::MuslEABI ||
         TargetTriple.getEnvironment() == Triple::MuslEABIHF ||
         TargetTriple.getEnvironment() == Triple::OpenHOS) &&
        !(TargetTriple.isOSWindows() || TargetTriple.isOSDarwin()))
      this->Options.EABIVersion = EABI::GNU;
    else
      this->Options.EABIVersion = EABI::EABI5;
  }

  if (TT.isOSBinFormatMachO()) {
    this->Options.TrapUnreachable = true;
    this->Options.NoTrapAfterNoreturn = true;
  }

  // ARM supports the debug entry values.
  setSupportsDebugEntryValues(true);

  initAsmInfo();

  // ARM supports the MachineOutliner.
  setMachineOutliner(true);
  setSupportsDefaultOutlining(true);
}

ARMBaseTargetMachine::~ARMBaseTargetMachine() = default;

MachineFunctionInfo *ARMBaseTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return ARMFunctionInfo::create<ARMFunctionInfo>(
      Allocator, F, static_cast<const ARMSubtarget *>(STI));
}

const ARMSubtarget *
ARMBaseTargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

  std::string CPU =
      CPUAttr.isValid() ? CPUAttr.getValueAsString().str() : TargetCPU;
  std::string FS =
      FSAttr.isValid() ? FSAttr.getValueAsString().str() : TargetFS;

  // FIXME: This is related to the code below to reset the target options,
  // we need to know whether or not the soft float flag is set on the
  // function before we can generate a subtarget. We also need to use
  // it as a key for the subtarget since that can be the only difference
  // between two functions.
  bool SoftFloat = F.getFnAttribute("use-soft-float").getValueAsBool();
  // If the soft float attribute is set on the function turn on the soft float
  // subtarget feature.
  if (SoftFloat)
    FS += FS.empty() ? "+soft-float" : ",+soft-float";

  // Use the optminsize to identify the subtarget, but don't use it in the
  // feature string.
  std::string Key = CPU + FS;
  if (F.hasMinSize())
    Key += "+minsize";

  auto &I = SubtargetMap[Key];
  if (!I) {
    // This needs to be done before we create a new subtarget since any
    // creation will depend on the TM and the code generation flags on the
    // function that reside in TargetOptions.
    resetTargetOptions(F);
    I = std::make_unique<ARMSubtarget>(TargetTriple, CPU, FS, *this, isLittle,
                                        F.hasMinSize());

    if (!I->isThumb() && !I->hasARMOps())
      F.getContext().emitError("Function '" + F.getName() + "' uses ARM "
          "instructions, but the target does not support ARM mode execution.");
  }

  return I.get();
}

TargetTransformInfo
ARMBaseTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(ARMTTIImpl(this, F));
}

ARMLETargetMachine::ARMLETargetMachine(const Target &T, const Triple &TT,
                                       StringRef CPU, StringRef FS,
                                       const TargetOptions &Options,
                                       std::optional<Reloc::Model> RM,
                                       std::optional<CodeModel::Model> CM,
                                       CodeGenOptLevel OL, bool JIT)
    : ARMBaseTargetMachine(T, TT, CPU, FS, Options, RM, CM, OL, true) {}

ARMBETargetMachine::ARMBETargetMachine(const Target &T, const Triple &TT,
                                       StringRef CPU, StringRef FS,
                                       const TargetOptions &Options,
                                       std::optional<Reloc::Model> RM,
                                       std::optional<CodeModel::Model> CM,
                                       CodeGenOptLevel OL, bool JIT)
    : ARMBaseTargetMachine(T, TT, CPU, FS, Options, RM, CM, OL, false) {}

namespace {

/// ARM Code Generator Pass Configuration Options.
class ARMPassConfig : public TargetPassConfig {
public:
  ARMPassConfig(ARMBaseTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  ARMBaseTargetMachine &getARMTargetMachine() const {
    return getTM<ARMBaseTargetMachine>();
  }

  ScheduleDAGInstrs *
  createMachineScheduler(MachineSchedContext *C) const override {
    ScheduleDAGMILive *DAG = createGenericSchedLive(C);
    // add DAG Mutations here.
    const ARMSubtarget &ST = C->MF->getSubtarget<ARMSubtarget>();
    if (ST.hasFusion())
      DAG->addMutation(createARMMacroFusionDAGMutation());
    return DAG;
  }

  ScheduleDAGInstrs *
  createPostMachineScheduler(MachineSchedContext *C) const override {
    ScheduleDAGMI *DAG = createGenericSchedPostRA(C);
    // add DAG Mutations here.
    const ARMSubtarget &ST = C->MF->getSubtarget<ARMSubtarget>();
    if (ST.hasFusion())
      DAG->addMutation(createARMMacroFusionDAGMutation());
    return DAG;
  }

  void addIRPasses() override;
  void addCodeGenPrepare() override;
  bool addPreISel() override;
  bool addInstSelector() override;
  bool addIRTranslator() override;
  bool addLegalizeMachineIR() override;
  bool addRegBankSelect() override;
  bool addGlobalInstructionSelect() override;
  void addPreRegAlloc() override;
  void addPreSched2() override;
  void addPreEmitPass() override;
  void addPreEmitPass2() override;

  std::unique_ptr<CSEConfigBase> getCSEConfig() const override;
};

class ARMExecutionDomainFix : public ExecutionDomainFix {
public:
  static char ID;
  ARMExecutionDomainFix() : ExecutionDomainFix(ID, ARM::DPRRegClass) {}
  StringRef getPassName() const override {
    return "ARM Execution Domain Fix";
  }
};
char ARMExecutionDomainFix::ID;

} // end anonymous namespace

INITIALIZE_PASS_BEGIN(ARMExecutionDomainFix, "arm-execution-domain-fix",
  "ARM Execution Domain Fix", false, false)
INITIALIZE_PASS_DEPENDENCY(ReachingDefAnalysis)
INITIALIZE_PASS_END(ARMExecutionDomainFix, "arm-execution-domain-fix",
  "ARM Execution Domain Fix", false, false)

TargetPassConfig *ARMBaseTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new ARMPassConfig(*this, PM);
}

std::unique_ptr<CSEConfigBase> ARMPassConfig::getCSEConfig() const {
  return getStandardCSEConfigForOpt(TM->getOptLevel());
}

void ARMPassConfig::addIRPasses() {
  if (TM->Options.ThreadModel == ThreadModel::Single)
    addPass(createLowerAtomicPass());
  else
    addPass(createAtomicExpandLegacyPass());

  // Cmpxchg instructions are often used with a subsequent comparison to
  // determine whether it succeeded. We can exploit existing control-flow in
  // ldrex/strex loops to simplify this, but it needs tidying up.
  if (TM->getOptLevel() != CodeGenOptLevel::None && EnableAtomicTidy)
    addPass(createCFGSimplificationPass(
        SimplifyCFGOptions().hoistCommonInsts(true).sinkCommonInsts(true),
        [this](const Function &F) {
          const auto &ST = this->TM->getSubtarget<ARMSubtarget>(F);
          return ST.hasAnyDataBarrier() && !ST.isThumb1Only();
        }));

  addPass(createMVEGatherScatterLoweringPass());
  addPass(createMVELaneInterleavingPass());

  TargetPassConfig::addIRPasses();

  // Run the parallel DSP pass.
  if (getOptLevel() == CodeGenOptLevel::Aggressive)
    addPass(createARMParallelDSPPass());

  // Match complex arithmetic patterns
  if (TM->getOptLevel() >= CodeGenOptLevel::Default)
    addPass(createComplexDeinterleavingPass(TM));

  // Match interleaved memory accesses to ldN/stN intrinsics.
  if (TM->getOptLevel() != CodeGenOptLevel::None)
    addPass(createInterleavedAccessPass());

  // Add Control Flow Guard checks.
  if (TM->getTargetTriple().isOSWindows())
    addPass(createCFGuardCheckPass());

  if (TM->Options.JMCInstrument)
    addPass(createJMCInstrumenterPass());
}

void ARMPassConfig::addCodeGenPrepare() {
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createTypePromotionLegacyPass());
  TargetPassConfig::addCodeGenPrepare();
}

bool ARMPassConfig::addPreISel() {
  if ((TM->getOptLevel() != CodeGenOptLevel::None &&
       EnableGlobalMerge == cl::BOU_UNSET) ||
      EnableGlobalMerge == cl::BOU_TRUE) {
    // FIXME: This is using the thumb1 only constant value for
    // maximal global offset for merging globals. We may want
    // to look into using the old value for non-thumb1 code of
    // 4095 based on the TargetMachine, but this starts to become
    // tricky when doing code gen per function.
    bool OnlyOptimizeForSize =
        (TM->getOptLevel() < CodeGenOptLevel::Aggressive) &&
        (EnableGlobalMerge == cl::BOU_UNSET);
    // Merging of extern globals is enabled by default on non-Mach-O as we
    // expect it to be generally either beneficial or harmless. On Mach-O it
    // is disabled as we emit the .subsections_via_symbols directive which
    // means that merging extern globals is not safe.
    bool MergeExternalByDefault = !TM->getTargetTriple().isOSBinFormatMachO();
    addPass(createGlobalMergePass(TM, 127, OnlyOptimizeForSize,
                                  MergeExternalByDefault));
  }

  if (TM->getOptLevel() != CodeGenOptLevel::None) {
    addPass(createHardwareLoopsLegacyPass());
    addPass(createMVETailPredicationPass());
    // FIXME: IR passes can delete address-taken basic blocks, deleting
    // corresponding blockaddresses. ARMConstantPoolConstant holds references to
    // address-taken basic blocks which can be invalidated if the function
    // containing the blockaddress has already been codegen'd and the basic
    // block is removed. Work around this by forcing all IR passes to run before
    // any ISel takes place. We should have a more principled way of handling
    // this. See D99707 for more details.
    addPass(createBarrierNoopPass());
  }

  return false;
}

bool ARMPassConfig::addInstSelector() {
  addPass(createARMISelDag(getARMTargetMachine(), getOptLevel()));
  return false;
}

bool ARMPassConfig::addIRTranslator() {
  addPass(new IRTranslator(getOptLevel()));
  return false;
}

bool ARMPassConfig::addLegalizeMachineIR() {
  addPass(new Legalizer());
  return false;
}

bool ARMPassConfig::addRegBankSelect() {
  addPass(new RegBankSelect());
  return false;
}

bool ARMPassConfig::addGlobalInstructionSelect() {
  addPass(new InstructionSelect(getOptLevel()));
  return false;
}

void ARMPassConfig::addPreRegAlloc() {
  if (getOptLevel() != CodeGenOptLevel::None) {
    if (getOptLevel() == CodeGenOptLevel::Aggressive)
      addPass(&MachinePipelinerID);

    addPass(createMVETPAndVPTOptimisationsPass());

    addPass(createMLxExpansionPass());

    if (EnableARMLoadStoreOpt)
      addPass(createARMLoadStoreOptimizationPass(/* pre-register alloc */ true));

    if (!DisableA15SDOptimization)
      addPass(createA15SDOptimizerPass());
  }
}

void ARMPassConfig::addPreSched2() {
  if (getOptLevel() != CodeGenOptLevel::None) {
    if (EnableARMLoadStoreOpt)
      addPass(createARMLoadStoreOptimizationPass());

    addPass(new ARMExecutionDomainFix());
    addPass(createBreakFalseDeps());
  }

  // Expand some pseudo instructions into multiple instructions to allow
  // proper scheduling.
  addPass(createARMExpandPseudoPass());

  if (getOptLevel() != CodeGenOptLevel::None) {
    // When optimising for size, always run the Thumb2SizeReduction pass before
    // IfConversion. Otherwise, check whether IT blocks are restricted
    // (e.g. in v8, IfConversion depends on Thumb instruction widths)
    addPass(createThumb2SizeReductionPass([this](const Function &F) {
      return this->TM->getSubtarget<ARMSubtarget>(F).hasMinSize() ||
             this->TM->getSubtarget<ARMSubtarget>(F).restrictIT();
    }));

    addPass(createIfConverter([](const MachineFunction &MF) {
      return !MF.getSubtarget<ARMSubtarget>().isThumb1Only();
    }));
  }
  addPass(createThumb2ITBlockPass());

  // Add both scheduling passes to give the subtarget an opportunity to pick
  // between them.
  if (getOptLevel() != CodeGenOptLevel::None) {
    addPass(&PostMachineSchedulerID);
    addPass(&PostRASchedulerID);
  }

  addPass(createMVEVPTBlockPass());
  addPass(createARMIndirectThunks());
  addPass(createARMSLSHardeningPass());
}

void ARMPassConfig::addPreEmitPass() {
  addPass(createThumb2SizeReductionPass());

  // Constant island pass work on unbundled instructions.
  addPass(createUnpackMachineBundles([](const MachineFunction &MF) {
    return MF.getSubtarget<ARMSubtarget>().isThumb2();
  }));

  // Don't optimize barriers or block placement at -O0.
  if (getOptLevel() != CodeGenOptLevel::None) {
    addPass(createARMBlockPlacementPass());
    addPass(createARMOptimizeBarriersPass());
  }
}

void ARMPassConfig::addPreEmitPass2() {
  // Inserts fixup instructions before unsafe AES operations. Instructions may
  // be inserted at the start of blocks and at within blocks so this pass has to
  // come before those below.
  addPass(createARMFixCortexA57AES1742098Pass());
  // Inserts BTIs at the start of functions and indirectly-called basic blocks,
  // so passes cannot add to the start of basic blocks once this has run.
  addPass(createARMBranchTargetsPass());
  // Inserts Constant Islands. Block sizes cannot be increased after this point,
  // as this may push the branch ranges and load offsets of accessing constant
  // pools out of range..
  addPass(createARMConstantIslandPass());
  // Finalises Low-Overhead Loops. This replaces pseudo instructions with real
  // instructions, but the pseudos all have conservative sizes so that block
  // sizes will only be decreased by this pass.
  addPass(createARMLowOverheadLoopsPass());

  if (TM->getTargetTriple().isOSWindows()) {
    // Identify valid longjmp targets for Windows Control Flow Guard.
    addPass(createCFGuardLongjmpPass());
    // Identify valid eh continuation targets for Windows EHCont Guard.
    addPass(createEHContGuardCatchretPass());
  }
}

yaml::MachineFunctionInfo *
ARMBaseTargetMachine::createDefaultFuncInfoYAML() const {
  return new yaml::ARMFunctionInfo();
}

yaml::MachineFunctionInfo *
ARMBaseTargetMachine::convertFuncInfoToYAML(const MachineFunction &MF) const {
  const auto *MFI = MF.getInfo<ARMFunctionInfo>();
  return new yaml::ARMFunctionInfo(*MFI);
}

bool ARMBaseTargetMachine::parseMachineFunctionInfo(
    const yaml::MachineFunctionInfo &MFI, PerFunctionMIParsingState &PFS,
    SMDiagnostic &Error, SMRange &SourceRange) const {
  const auto &YamlMFI = static_cast<const yaml::ARMFunctionInfo &>(MFI);
  MachineFunction &MF = PFS.MF;
  MF.getInfo<ARMFunctionInfo>()->initializeBaseYamlFields(YamlMFI);
  return false;
}
