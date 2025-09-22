//===-- AArch64TargetMachine.cpp - Define TargetMachine for AArch64 -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#include "AArch64TargetMachine.h"
#include "AArch64.h"
#include "AArch64MachineFunctionInfo.h"
#include "AArch64MachineScheduler.h"
#include "AArch64MacroFusion.h"
#include "AArch64Subtarget.h"
#include "AArch64TargetObjectFile.h"
#include "AArch64TargetTransformInfo.h"
#include "MCTargetDesc/AArch64MCTargetDesc.h"
#include "TargetInfo/AArch64TargetInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/CodeGen/CFIFixup.h"
#include "llvm/CodeGen/CSEConfigBase.h"
#include "llvm/CodeGen/GlobalISel/CSEInfo.h"
#include "llvm/CodeGen/GlobalISel/IRTranslator.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelect.h"
#include "llvm/CodeGen/GlobalISel/Legalizer.h"
#include "llvm/CodeGen/GlobalISel/LoadStoreOpt.h"
#include "llvm/CodeGen/GlobalISel/Localizer.h"
#include "llvm/CodeGen/GlobalISel/RegBankSelect.h"
#include "llvm/CodeGen/MIRParser/MIParser.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/Transforms/CFGuard.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Vectorize/LoopIdiomVectorize.h"
#include <memory>
#include <optional>
#include <string>

using namespace llvm;

static cl::opt<bool> EnableCCMP("aarch64-enable-ccmp",
                                cl::desc("Enable the CCMP formation pass"),
                                cl::init(true), cl::Hidden);

static cl::opt<bool>
    EnableCondBrTuning("aarch64-enable-cond-br-tune",
                       cl::desc("Enable the conditional branch tuning pass"),
                       cl::init(true), cl::Hidden);

static cl::opt<bool> EnableAArch64CopyPropagation(
    "aarch64-enable-copy-propagation",
    cl::desc("Enable the copy propagation with AArch64 copy instr"),
    cl::init(true), cl::Hidden);

static cl::opt<bool> EnableMCR("aarch64-enable-mcr",
                               cl::desc("Enable the machine combiner pass"),
                               cl::init(true), cl::Hidden);

static cl::opt<bool> EnableStPairSuppress("aarch64-enable-stp-suppress",
                                          cl::desc("Suppress STP for AArch64"),
                                          cl::init(true), cl::Hidden);

static cl::opt<bool> EnableAdvSIMDScalar(
    "aarch64-enable-simd-scalar",
    cl::desc("Enable use of AdvSIMD scalar integer instructions"),
    cl::init(false), cl::Hidden);

static cl::opt<bool>
    EnablePromoteConstant("aarch64-enable-promote-const",
                          cl::desc("Enable the promote constant pass"),
                          cl::init(true), cl::Hidden);

static cl::opt<bool> EnableCollectLOH(
    "aarch64-enable-collect-loh",
    cl::desc("Enable the pass that emits the linker optimization hints (LOH)"),
    cl::init(true), cl::Hidden);

static cl::opt<bool>
    EnableDeadRegisterElimination("aarch64-enable-dead-defs", cl::Hidden,
                                  cl::desc("Enable the pass that removes dead"
                                           " definitons and replaces stores to"
                                           " them with stores to the zero"
                                           " register"),
                                  cl::init(true));

static cl::opt<bool> EnableRedundantCopyElimination(
    "aarch64-enable-copyelim",
    cl::desc("Enable the redundant copy elimination pass"), cl::init(true),
    cl::Hidden);

static cl::opt<bool> EnableLoadStoreOpt("aarch64-enable-ldst-opt",
                                        cl::desc("Enable the load/store pair"
                                                 " optimization pass"),
                                        cl::init(true), cl::Hidden);

static cl::opt<bool> EnableAtomicTidy(
    "aarch64-enable-atomic-cfg-tidy", cl::Hidden,
    cl::desc("Run SimplifyCFG after expanding atomic operations"
             " to make use of cmpxchg flow-based information"),
    cl::init(true));

static cl::opt<bool>
EnableEarlyIfConversion("aarch64-enable-early-ifcvt", cl::Hidden,
                        cl::desc("Run early if-conversion"),
                        cl::init(true));

static cl::opt<bool>
    EnableCondOpt("aarch64-enable-condopt",
                  cl::desc("Enable the condition optimizer pass"),
                  cl::init(true), cl::Hidden);

static cl::opt<bool>
    EnableGEPOpt("aarch64-enable-gep-opt", cl::Hidden,
                 cl::desc("Enable optimizations on complex GEPs"),
                 cl::init(false));

static cl::opt<bool>
    EnableSelectOpt("aarch64-select-opt", cl::Hidden,
                    cl::desc("Enable select to branch optimizations"),
                    cl::init(true));

static cl::opt<bool>
    BranchRelaxation("aarch64-enable-branch-relax", cl::Hidden, cl::init(true),
                     cl::desc("Relax out of range conditional branches"));

static cl::opt<bool> EnableCompressJumpTables(
    "aarch64-enable-compress-jump-tables", cl::Hidden, cl::init(true),
    cl::desc("Use smallest entry possible for jump tables"));

// FIXME: Unify control over GlobalMerge.
static cl::opt<cl::boolOrDefault>
    EnableGlobalMerge("aarch64-enable-global-merge", cl::Hidden,
                      cl::desc("Enable the global merge pass"));

static cl::opt<bool>
    EnableLoopDataPrefetch("aarch64-enable-loop-data-prefetch", cl::Hidden,
                           cl::desc("Enable the loop data prefetch pass"),
                           cl::init(true));

static cl::opt<int> EnableGlobalISelAtO(
    "aarch64-enable-global-isel-at-O", cl::Hidden,
    cl::desc("Enable GlobalISel at or below an opt level (-1 to disable)"),
    cl::init(0));

static cl::opt<bool>
    EnableSVEIntrinsicOpts("aarch64-enable-sve-intrinsic-opts", cl::Hidden,
                           cl::desc("Enable SVE intrinsic opts"),
                           cl::init(true));

static cl::opt<bool> EnableFalkorHWPFFix("aarch64-enable-falkor-hwpf-fix",
                                         cl::init(true), cl::Hidden);

static cl::opt<bool>
    EnableBranchTargets("aarch64-enable-branch-targets", cl::Hidden,
                        cl::desc("Enable the AArch64 branch target pass"),
                        cl::init(true));

static cl::opt<unsigned> SVEVectorBitsMaxOpt(
    "aarch64-sve-vector-bits-max",
    cl::desc("Assume SVE vector registers are at most this big, "
             "with zero meaning no maximum size is assumed."),
    cl::init(0), cl::Hidden);

static cl::opt<unsigned> SVEVectorBitsMinOpt(
    "aarch64-sve-vector-bits-min",
    cl::desc("Assume SVE vector registers are at least this big, "
             "with zero meaning no minimum size is assumed."),
    cl::init(0), cl::Hidden);

static cl::opt<bool> ForceStreaming(
    "force-streaming",
    cl::desc("Force the use of streaming code for all functions"),
    cl::init(false), cl::Hidden);

static cl::opt<bool> ForceStreamingCompatible(
    "force-streaming-compatible",
    cl::desc("Force the use of streaming-compatible code for all functions"),
    cl::init(false), cl::Hidden);

extern cl::opt<bool> EnableHomogeneousPrologEpilog;

static cl::opt<bool> EnableGISelLoadStoreOptPreLegal(
    "aarch64-enable-gisel-ldst-prelegal",
    cl::desc("Enable GlobalISel's pre-legalizer load/store optimization pass"),
    cl::init(true), cl::Hidden);

static cl::opt<bool> EnableGISelLoadStoreOptPostLegal(
    "aarch64-enable-gisel-ldst-postlegal",
    cl::desc("Enable GlobalISel's post-legalizer load/store optimization pass"),
    cl::init(false), cl::Hidden);

static cl::opt<bool>
    EnableSinkFold("aarch64-enable-sink-fold",
                   cl::desc("Enable sinking and folding of instruction copies"),
                   cl::init(true), cl::Hidden);

static cl::opt<bool>
    EnableMachinePipeliner("aarch64-enable-pipeliner",
                           cl::desc("Enable Machine Pipeliner for AArch64"),
                           cl::init(false), cl::Hidden);

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeAArch64Target() {
  // Register the target.
  RegisterTargetMachine<AArch64leTargetMachine> X(getTheAArch64leTarget());
  RegisterTargetMachine<AArch64beTargetMachine> Y(getTheAArch64beTarget());
  RegisterTargetMachine<AArch64leTargetMachine> Z(getTheARM64Target());
  RegisterTargetMachine<AArch64leTargetMachine> W(getTheARM64_32Target());
  RegisterTargetMachine<AArch64leTargetMachine> V(getTheAArch64_32Target());
  auto PR = PassRegistry::getPassRegistry();
  initializeGlobalISel(*PR);
  initializeAArch64A53Fix835769Pass(*PR);
  initializeAArch64A57FPLoadBalancingPass(*PR);
  initializeAArch64AdvSIMDScalarPass(*PR);
  initializeAArch64BranchTargetsPass(*PR);
  initializeAArch64CollectLOHPass(*PR);
  initializeAArch64CompressJumpTablesPass(*PR);
  initializeAArch64ConditionalComparesPass(*PR);
  initializeAArch64ConditionOptimizerPass(*PR);
  initializeAArch64DeadRegisterDefinitionsPass(*PR);
  initializeAArch64ExpandPseudoPass(*PR);
  initializeAArch64LoadStoreOptPass(*PR);
  initializeAArch64MIPeepholeOptPass(*PR);
  initializeAArch64SIMDInstrOptPass(*PR);
  initializeAArch64O0PreLegalizerCombinerPass(*PR);
  initializeAArch64PreLegalizerCombinerPass(*PR);
  initializeAArch64PointerAuthPass(*PR);
  initializeAArch64PostCoalescerPass(*PR);
  initializeAArch64PostLegalizerCombinerPass(*PR);
  initializeAArch64PostLegalizerLoweringPass(*PR);
  initializeAArch64PostSelectOptimizePass(*PR);
  initializeAArch64PromoteConstantPass(*PR);
  initializeAArch64RedundantCopyEliminationPass(*PR);
  initializeAArch64StorePairSuppressPass(*PR);
  initializeFalkorHWPFFixPass(*PR);
  initializeFalkorMarkStridedAccessesLegacyPass(*PR);
  initializeLDTLSCleanupPass(*PR);
  initializeKCFIPass(*PR);
  initializeSMEABIPass(*PR);
  initializeSVEIntrinsicOptsPass(*PR);
  initializeAArch64SpeculationHardeningPass(*PR);
  initializeAArch64SLSHardeningPass(*PR);
  initializeAArch64StackTaggingPass(*PR);
  initializeAArch64StackTaggingPreRAPass(*PR);
  initializeAArch64LowerHomogeneousPrologEpilogPass(*PR);
  initializeAArch64DAGToDAGISelLegacyPass(*PR);
  initializeAArch64GlobalsTaggingPass(*PR);
}

//===----------------------------------------------------------------------===//
// AArch64 Lowering public interface.
//===----------------------------------------------------------------------===//
static std::unique_ptr<TargetLoweringObjectFile> createTLOF(const Triple &TT) {
  if (TT.isOSBinFormatMachO())
    return std::make_unique<AArch64_MachoTargetObjectFile>();
  if (TT.isOSBinFormatCOFF())
    return std::make_unique<AArch64_COFFTargetObjectFile>();

  return std::make_unique<AArch64_ELFTargetObjectFile>();
}

// Helper function to build a DataLayout string
static std::string computeDataLayout(const Triple &TT,
                                     const MCTargetOptions &Options,
                                     bool LittleEndian) {
  if (TT.isOSBinFormatMachO()) {
    if (TT.getArch() == Triple::aarch64_32)
      return "e-m:o-p:32:32-i64:64-i128:128-n32:64-S128-Fn32";
    return "e-m:o-i64:64-i128:128-n32:64-S128-Fn32";
  }
  if (TT.isOSBinFormatCOFF())
    return "e-m:w-p:64:64-i32:32-i64:64-i128:128-n32:64-S128-Fn32";
  std::string Endian = LittleEndian ? "e" : "E";
  std::string Ptr32 = TT.getEnvironment() == Triple::GNUILP32 ? "-p:32:32" : "";
  return Endian + "-m:e" + Ptr32 +
         "-i8:8:32-i16:16:32-i64:64-i128:128-n32:64-S128-Fn32";
}

static StringRef computeDefaultCPU(const Triple &TT, StringRef CPU) {
  if (CPU.empty() && TT.isArm64e())
    return "apple-a12";
  return CPU;
}

static Reloc::Model getEffectiveRelocModel(const Triple &TT,
                                           std::optional<Reloc::Model> RM) {
  // AArch64 Darwin and Windows are always PIC.
  if (TT.isOSDarwin() || TT.isOSWindows())
    return Reloc::PIC_;
  // On ELF platforms the default static relocation model has a smart enough
  // linker to cope with referencing external symbols defined in a shared
  // library. Hence DynamicNoPIC doesn't need to be promoted to PIC.
  if (!RM || *RM == Reloc::DynamicNoPIC)
    return Reloc::Static;
  return *RM;
}

static CodeModel::Model
getEffectiveAArch64CodeModel(const Triple &TT,
                             std::optional<CodeModel::Model> CM, bool JIT) {
  if (CM) {
    if (*CM != CodeModel::Small && *CM != CodeModel::Tiny &&
        *CM != CodeModel::Large) {
      report_fatal_error(
          "Only small, tiny and large code models are allowed on AArch64");
    } else if (*CM == CodeModel::Tiny && !TT.isOSBinFormatELF())
      report_fatal_error("tiny code model is only supported on ELF");
    return *CM;
  }
  // The default MCJIT memory managers make no guarantees about where they can
  // find an executable page; JITed code needs to be able to refer to globals
  // no matter how far away they are.
  // We should set the CodeModel::Small for Windows ARM64 in JIT mode,
  // since with large code model LLVM generating 4 MOV instructions, and
  // Windows doesn't support relocating these long branch (4 MOVs).
  if (JIT && !TT.isOSWindows())
    return CodeModel::Large;
  return CodeModel::Small;
}

/// Create an AArch64 architecture model.
///
AArch64TargetMachine::AArch64TargetMachine(const Target &T, const Triple &TT,
                                           StringRef CPU, StringRef FS,
                                           const TargetOptions &Options,
                                           std::optional<Reloc::Model> RM,
                                           std::optional<CodeModel::Model> CM,
                                           CodeGenOptLevel OL, bool JIT,
                                           bool LittleEndian)
    : LLVMTargetMachine(T,
                        computeDataLayout(TT, Options.MCOptions, LittleEndian),
                        TT, computeDefaultCPU(TT, CPU), FS, Options,
                        getEffectiveRelocModel(TT, RM),
                        getEffectiveAArch64CodeModel(TT, CM, JIT), OL),
      TLOF(createTLOF(getTargetTriple())), isLittle(LittleEndian) {
  initAsmInfo();

  if (TT.isOSBinFormatMachO()) {
    this->Options.TrapUnreachable = true;
    this->Options.NoTrapAfterNoreturn = true;
  }

  if (getMCAsmInfo()->usesWindowsCFI()) {
    // Unwinding can get confused if the last instruction in an
    // exception-handling region (function, funclet, try block, etc.)
    // is a call.
    //
    // FIXME: We could elide the trap if the next instruction would be in
    // the same region anyway.
    this->Options.TrapUnreachable = true;
  }

  if (this->Options.TLSSize == 0) // default
    this->Options.TLSSize = 24;
  if ((getCodeModel() == CodeModel::Small ||
       getCodeModel() == CodeModel::Kernel) &&
      this->Options.TLSSize > 32)
    // for the small (and kernel) code model, the maximum TLS size is 4GiB
    this->Options.TLSSize = 32;
  else if (getCodeModel() == CodeModel::Tiny && this->Options.TLSSize > 24)
    // for the tiny code model, the maximum TLS size is 1MiB (< 16MiB)
    this->Options.TLSSize = 24;

  // Enable GlobalISel at or below EnableGlobalISelAt0, unless this is
  // MachO/CodeModel::Large, which GlobalISel does not support.
  if (static_cast<int>(getOptLevel()) <= EnableGlobalISelAtO &&
      !getTargetTriple().isOSOpenBSD() &&
      TT.getArch() != Triple::aarch64_32 &&
      TT.getEnvironment() != Triple::GNUILP32 &&
      !(getCodeModel() == CodeModel::Large && TT.isOSBinFormatMachO())) {
    setGlobalISel(true);
    setGlobalISelAbort(GlobalISelAbortMode::Disable);
  }

  // AArch64 supports the MachineOutliner.
  setMachineOutliner(true);

  // AArch64 supports default outlining behaviour.
  setSupportsDefaultOutlining(true);

  // AArch64 supports the debug entry values.
  setSupportsDebugEntryValues(true);

  // AArch64 supports fixing up the DWARF unwind information.
  if (!getMCAsmInfo()->usesWindowsCFI())
    setCFIFixup(true);
}

AArch64TargetMachine::~AArch64TargetMachine() = default;

const AArch64Subtarget *
AArch64TargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute TuneAttr = F.getFnAttribute("tune-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

  StringRef CPU = CPUAttr.isValid() ? CPUAttr.getValueAsString() : TargetCPU;
  StringRef TuneCPU = TuneAttr.isValid() ? TuneAttr.getValueAsString() : CPU;
  StringRef FS = FSAttr.isValid() ? FSAttr.getValueAsString() : TargetFS;
  bool HasMinSize = F.hasMinSize();

  bool IsStreaming = ForceStreaming ||
                     F.hasFnAttribute("aarch64_pstate_sm_enabled") ||
                     F.hasFnAttribute("aarch64_pstate_sm_body");
  bool IsStreamingCompatible = ForceStreamingCompatible ||
                               F.hasFnAttribute("aarch64_pstate_sm_compatible");

  unsigned MinSVEVectorSize = 0;
  unsigned MaxSVEVectorSize = 0;
  if (F.hasFnAttribute(Attribute::VScaleRange)) {
    ConstantRange CR = getVScaleRange(&F, 64);
    MinSVEVectorSize = CR.getUnsignedMin().getZExtValue() * 128;
    MaxSVEVectorSize = CR.getUnsignedMax().getZExtValue() * 128;
  } else {
    MinSVEVectorSize = SVEVectorBitsMinOpt;
    MaxSVEVectorSize = SVEVectorBitsMaxOpt;
  }

  assert(MinSVEVectorSize % 128 == 0 &&
         "SVE requires vector length in multiples of 128!");
  assert(MaxSVEVectorSize % 128 == 0 &&
         "SVE requires vector length in multiples of 128!");
  assert((MaxSVEVectorSize >= MinSVEVectorSize || MaxSVEVectorSize == 0) &&
         "Minimum SVE vector size should not be larger than its maximum!");

  // Sanitize user input in case of no asserts
  if (MaxSVEVectorSize != 0) {
    MinSVEVectorSize = std::min(MinSVEVectorSize, MaxSVEVectorSize);
    MaxSVEVectorSize = std::max(MinSVEVectorSize, MaxSVEVectorSize);
  }

  SmallString<512> Key;
  raw_svector_ostream(Key) << "SVEMin" << MinSVEVectorSize << "SVEMax"
                           << MaxSVEVectorSize << "IsStreaming=" << IsStreaming
                           << "IsStreamingCompatible=" << IsStreamingCompatible
                           << CPU << TuneCPU << FS
                           << "HasMinSize=" << HasMinSize;

  auto &I = SubtargetMap[Key];
  if (!I) {
    // This needs to be done before we create a new subtarget since any
    // creation will depend on the TM and the code generation flags on the
    // function that reside in TargetOptions.
    resetTargetOptions(F);
    I = std::make_unique<AArch64Subtarget>(
        TargetTriple, CPU, TuneCPU, FS, *this, isLittle, MinSVEVectorSize,
        MaxSVEVectorSize, IsStreaming, IsStreamingCompatible, HasMinSize);
  }

  assert((!IsStreaming || I->hasSME()) && "Expected SME to be available");

  return I.get();
}

void AArch64leTargetMachine::anchor() { }

AArch64leTargetMachine::AArch64leTargetMachine(
    const Target &T, const Triple &TT, StringRef CPU, StringRef FS,
    const TargetOptions &Options, std::optional<Reloc::Model> RM,
    std::optional<CodeModel::Model> CM, CodeGenOptLevel OL, bool JIT)
    : AArch64TargetMachine(T, TT, CPU, FS, Options, RM, CM, OL, JIT, true) {}

void AArch64beTargetMachine::anchor() { }

AArch64beTargetMachine::AArch64beTargetMachine(
    const Target &T, const Triple &TT, StringRef CPU, StringRef FS,
    const TargetOptions &Options, std::optional<Reloc::Model> RM,
    std::optional<CodeModel::Model> CM, CodeGenOptLevel OL, bool JIT)
    : AArch64TargetMachine(T, TT, CPU, FS, Options, RM, CM, OL, JIT, false) {}

namespace {

/// AArch64 Code Generator Pass Configuration Options.
class AArch64PassConfig : public TargetPassConfig {
public:
  AArch64PassConfig(AArch64TargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {
    if (TM.getOptLevel() != CodeGenOptLevel::None)
      substitutePass(&PostRASchedulerID, &PostMachineSchedulerID);
    setEnableSinkAndFold(EnableSinkFold);
  }

  AArch64TargetMachine &getAArch64TargetMachine() const {
    return getTM<AArch64TargetMachine>();
  }

  ScheduleDAGInstrs *
  createMachineScheduler(MachineSchedContext *C) const override {
    const AArch64Subtarget &ST = C->MF->getSubtarget<AArch64Subtarget>();
    ScheduleDAGMILive *DAG = createGenericSchedLive(C);
    DAG->addMutation(createLoadClusterDAGMutation(DAG->TII, DAG->TRI));
    DAG->addMutation(createStoreClusterDAGMutation(DAG->TII, DAG->TRI));
    if (ST.hasFusion())
      DAG->addMutation(createAArch64MacroFusionDAGMutation());
    return DAG;
  }

  ScheduleDAGInstrs *
  createPostMachineScheduler(MachineSchedContext *C) const override {
    const AArch64Subtarget &ST = C->MF->getSubtarget<AArch64Subtarget>();
    ScheduleDAGMI *DAG =
        new ScheduleDAGMI(C, std::make_unique<AArch64PostRASchedStrategy>(C),
                          /* RemoveKillFlags=*/true);
    if (ST.hasFusion()) {
      // Run the Macro Fusion after RA again since literals are expanded from
      // pseudos then (v. addPreSched2()).
      DAG->addMutation(createAArch64MacroFusionDAGMutation());
      return DAG;
    }

    return DAG;
  }

  void addIRPasses()  override;
  bool addPreISel() override;
  void addCodeGenPrepare() override;
  bool addInstSelector() override;
  bool addIRTranslator() override;
  void addPreLegalizeMachineIR() override;
  bool addLegalizeMachineIR() override;
  void addPreRegBankSelect() override;
  bool addRegBankSelect() override;
  bool addGlobalInstructionSelect() override;
  void addMachineSSAOptimization() override;
  bool addILPOpts() override;
  void addPreRegAlloc() override;
  void addPostRegAlloc() override;
  void addPreSched2() override;
  void addPreEmitPass() override;
  void addPostBBSections() override;
  void addPreEmitPass2() override;
  bool addRegAssignAndRewriteOptimized() override;

  std::unique_ptr<CSEConfigBase> getCSEConfig() const override;
};

} // end anonymous namespace

void AArch64TargetMachine::registerPassBuilderCallbacks(PassBuilder &PB) {

  PB.registerLateLoopOptimizationsEPCallback(
      [=](LoopPassManager &LPM, OptimizationLevel Level) {
        LPM.addPass(LoopIdiomVectorizePass());
      });
}

TargetTransformInfo
AArch64TargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(AArch64TTIImpl(this, F));
}

TargetPassConfig *AArch64TargetMachine::createPassConfig(PassManagerBase &PM) {
  return new AArch64PassConfig(*this, PM);
}

std::unique_ptr<CSEConfigBase> AArch64PassConfig::getCSEConfig() const {
  return getStandardCSEConfigForOpt(TM->getOptLevel());
}

void AArch64PassConfig::addIRPasses() {
  // Always expand atomic operations, we don't deal with atomicrmw or cmpxchg
  // ourselves.
  addPass(createAtomicExpandLegacyPass());

  // Expand any SVE vector library calls that we can't code generate directly.
  if (EnableSVEIntrinsicOpts &&
      TM->getOptLevel() == CodeGenOptLevel::Aggressive)
    addPass(createSVEIntrinsicOptsPass());

  // Cmpxchg instructions are often used with a subsequent comparison to
  // determine whether it succeeded. We can exploit existing control-flow in
  // ldrex/strex loops to simplify this, but it needs tidying up.
  if (TM->getOptLevel() != CodeGenOptLevel::None && EnableAtomicTidy)
    addPass(createCFGSimplificationPass(SimplifyCFGOptions()
                                            .forwardSwitchCondToPhi(true)
                                            .convertSwitchRangeToICmp(true)
                                            .convertSwitchToLookupTable(true)
                                            .needCanonicalLoops(false)
                                            .hoistCommonInsts(true)
                                            .sinkCommonInsts(true)));

  // Run LoopDataPrefetch
  //
  // Run this before LSR to remove the multiplies involved in computing the
  // pointer values N iterations ahead.
  if (TM->getOptLevel() != CodeGenOptLevel::None) {
    if (EnableLoopDataPrefetch)
      addPass(createLoopDataPrefetchPass());
    if (EnableFalkorHWPFFix)
      addPass(createFalkorMarkStridedAccessesPass());
  }

  if (EnableGEPOpt) {
    // Call SeparateConstOffsetFromGEP pass to extract constants within indices
    // and lower a GEP with multiple indices to either arithmetic operations or
    // multiple GEPs with single index.
    addPass(createSeparateConstOffsetFromGEPPass(true));
    // Call EarlyCSE pass to find and remove subexpressions in the lowered
    // result.
    addPass(createEarlyCSEPass());
    // Do loop invariant code motion in case part of the lowered result is
    // invariant.
    addPass(createLICMPass());
  }

  TargetPassConfig::addIRPasses();

  if (getOptLevel() == CodeGenOptLevel::Aggressive && EnableSelectOpt)
    addPass(createSelectOptimizePass());

  addPass(createAArch64GlobalsTaggingPass());
  addPass(createAArch64StackTaggingPass(
      /*IsOptNone=*/TM->getOptLevel() == CodeGenOptLevel::None));

  // Match complex arithmetic patterns
  if (TM->getOptLevel() >= CodeGenOptLevel::Default)
    addPass(createComplexDeinterleavingPass(TM));

  // Match interleaved memory accesses to ldN/stN intrinsics.
  if (TM->getOptLevel() != CodeGenOptLevel::None) {
    addPass(createInterleavedLoadCombinePass());
    addPass(createInterleavedAccessPass());
  }

  // Expand any functions marked with SME attributes which require special
  // changes for the calling convention or that require the lazy-saving
  // mechanism specified in the SME ABI.
  addPass(createSMEABIPass());

  // Add Control Flow Guard checks.
  if (TM->getTargetTriple().isOSWindows()) {
    if (TM->getTargetTriple().isWindowsArm64EC())
      addPass(createAArch64Arm64ECCallLoweringPass());
    else
      addPass(createCFGuardCheckPass());
  }

  if (TM->Options.JMCInstrument)
    addPass(createJMCInstrumenterPass());
}

// Pass Pipeline Configuration
bool AArch64PassConfig::addPreISel() {
  // Run promote constant before global merge, so that the promoted constants
  // get a chance to be merged
  if (TM->getOptLevel() != CodeGenOptLevel::None && EnablePromoteConstant)
    addPass(createAArch64PromoteConstantPass());
  // FIXME: On AArch64, this depends on the type.
  // Basically, the addressable offsets are up to 4095 * Ty.getSizeInBytes().
  // and the offset has to be a multiple of the related size in bytes.
  if ((TM->getOptLevel() != CodeGenOptLevel::None &&
       EnableGlobalMerge == cl::BOU_UNSET) ||
      EnableGlobalMerge == cl::BOU_TRUE) {
    bool OnlyOptimizeForSize =
        (TM->getOptLevel() < CodeGenOptLevel::Aggressive) &&
        (EnableGlobalMerge == cl::BOU_UNSET);

    // Merging of extern globals is enabled by default on non-Mach-O as we
    // expect it to be generally either beneficial or harmless. On Mach-O it
    // is disabled as we emit the .subsections_via_symbols directive which
    // means that merging extern globals is not safe.
    bool MergeExternalByDefault = !TM->getTargetTriple().isOSBinFormatMachO();

    // FIXME: extern global merging is only enabled when we optimise for size
    // because there are some regressions with it also enabled for performance.
    if (!OnlyOptimizeForSize)
      MergeExternalByDefault = false;

    addPass(createGlobalMergePass(TM, 4095, OnlyOptimizeForSize,
                                  MergeExternalByDefault));
  }

  return false;
}

void AArch64PassConfig::addCodeGenPrepare() {
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createTypePromotionLegacyPass());
  TargetPassConfig::addCodeGenPrepare();
}

bool AArch64PassConfig::addInstSelector() {
  addPass(createAArch64ISelDag(getAArch64TargetMachine(), getOptLevel()));

  // For ELF, cleanup any local-dynamic TLS accesses (i.e. combine as many
  // references to _TLS_MODULE_BASE_ as possible.
  if (TM->getTargetTriple().isOSBinFormatELF() &&
      getOptLevel() != CodeGenOptLevel::None)
    addPass(createAArch64CleanupLocalDynamicTLSPass());

  return false;
}

bool AArch64PassConfig::addIRTranslator() {
  addPass(new IRTranslator(getOptLevel()));
  return false;
}

void AArch64PassConfig::addPreLegalizeMachineIR() {
  if (getOptLevel() == CodeGenOptLevel::None) {
    addPass(createAArch64O0PreLegalizerCombiner());
    addPass(new Localizer());
  } else {
    addPass(createAArch64PreLegalizerCombiner());
    addPass(new Localizer());
    if (EnableGISelLoadStoreOptPreLegal)
      addPass(new LoadStoreOpt());
  }
}

bool AArch64PassConfig::addLegalizeMachineIR() {
  addPass(new Legalizer());
  return false;
}

void AArch64PassConfig::addPreRegBankSelect() {
  bool IsOptNone = getOptLevel() == CodeGenOptLevel::None;
  if (!IsOptNone) {
    addPass(createAArch64PostLegalizerCombiner(IsOptNone));
    if (EnableGISelLoadStoreOptPostLegal)
      addPass(new LoadStoreOpt());
  }
  addPass(createAArch64PostLegalizerLowering());
}

bool AArch64PassConfig::addRegBankSelect() {
  addPass(new RegBankSelect());
  return false;
}

bool AArch64PassConfig::addGlobalInstructionSelect() {
  addPass(new InstructionSelect(getOptLevel()));
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createAArch64PostSelectOptimize());
  return false;
}

void AArch64PassConfig::addMachineSSAOptimization() {
  // Run default MachineSSAOptimization first.
  TargetPassConfig::addMachineSSAOptimization();

  if (TM->getOptLevel() != CodeGenOptLevel::None)
    addPass(createAArch64MIPeepholeOptPass());
}

bool AArch64PassConfig::addILPOpts() {
  if (EnableCondOpt)
    addPass(createAArch64ConditionOptimizerPass());
  if (EnableCCMP)
    addPass(createAArch64ConditionalCompares());
  if (EnableMCR)
    addPass(&MachineCombinerID);
  if (EnableCondBrTuning)
    addPass(createAArch64CondBrTuning());
  if (EnableEarlyIfConversion)
    addPass(&EarlyIfConverterID);
  if (EnableStPairSuppress)
    addPass(createAArch64StorePairSuppressPass());
  addPass(createAArch64SIMDInstrOptPass());
  if (TM->getOptLevel() != CodeGenOptLevel::None)
    addPass(createAArch64StackTaggingPreRAPass());
  return true;
}

void AArch64PassConfig::addPreRegAlloc() {
  // Change dead register definitions to refer to the zero register.
  if (TM->getOptLevel() != CodeGenOptLevel::None &&
      EnableDeadRegisterElimination)
    addPass(createAArch64DeadRegisterDefinitions());

  // Use AdvSIMD scalar instructions whenever profitable.
  if (TM->getOptLevel() != CodeGenOptLevel::None && EnableAdvSIMDScalar) {
    addPass(createAArch64AdvSIMDScalar());
    // The AdvSIMD pass may produce copies that can be rewritten to
    // be register coalescer friendly.
    addPass(&PeepholeOptimizerID);
  }
  if (TM->getOptLevel() != CodeGenOptLevel::None && EnableMachinePipeliner)
    addPass(&MachinePipelinerID);
}

void AArch64PassConfig::addPostRegAlloc() {
  // Remove redundant copy instructions.
  if (TM->getOptLevel() != CodeGenOptLevel::None &&
      EnableRedundantCopyElimination)
    addPass(createAArch64RedundantCopyEliminationPass());

  if (TM->getOptLevel() != CodeGenOptLevel::None && usingDefaultRegAlloc())
    // Improve performance for some FP/SIMD code for A57.
    addPass(createAArch64A57FPLoadBalancing());
}

void AArch64PassConfig::addPreSched2() {
  // Lower homogeneous frame instructions
  if (EnableHomogeneousPrologEpilog)
    addPass(createAArch64LowerHomogeneousPrologEpilogPass());
  // Expand some pseudo instructions to allow proper scheduling.
  addPass(createAArch64ExpandPseudoPass());
  // Use load/store pair instructions when possible.
  if (TM->getOptLevel() != CodeGenOptLevel::None) {
    if (EnableLoadStoreOpt)
      addPass(createAArch64LoadStoreOptimizationPass());
  }
  // Emit KCFI checks for indirect calls.
  addPass(createKCFIPass());

  // The AArch64SpeculationHardeningPass destroys dominator tree and natural
  // loop info, which is needed for the FalkorHWPFFixPass and also later on.
  // Therefore, run the AArch64SpeculationHardeningPass before the
  // FalkorHWPFFixPass to avoid recomputing dominator tree and natural loop
  // info.
  addPass(createAArch64SpeculationHardeningPass());

  if (TM->getOptLevel() != CodeGenOptLevel::None) {
    if (EnableFalkorHWPFFix)
      addPass(createFalkorHWPFFixPass());
  }
}

void AArch64PassConfig::addPreEmitPass() {
  // Machine Block Placement might have created new opportunities when run
  // at O3, where the Tail Duplication Threshold is set to 4 instructions.
  // Run the load/store optimizer once more.
  if (TM->getOptLevel() >= CodeGenOptLevel::Aggressive && EnableLoadStoreOpt)
    addPass(createAArch64LoadStoreOptimizationPass());

  if (TM->getOptLevel() >= CodeGenOptLevel::Aggressive &&
      EnableAArch64CopyPropagation)
    addPass(createMachineCopyPropagationPass(true));

  addPass(createAArch64A53Fix835769());

  if (TM->getTargetTriple().isOSWindows()) {
    // Identify valid longjmp targets for Windows Control Flow Guard.
    addPass(createCFGuardLongjmpPass());
    // Identify valid eh continuation targets for Windows EHCont Guard.
    addPass(createEHContGuardCatchretPass());
  }

  if (TM->getOptLevel() != CodeGenOptLevel::None && EnableCollectLOH &&
      TM->getTargetTriple().isOSBinFormatMachO())
    addPass(createAArch64CollectLOHPass());
}

void AArch64PassConfig::addPostBBSections() {
  addPass(createAArch64SLSHardeningPass());
  addPass(createAArch64PointerAuthPass());
  if (EnableBranchTargets)
    addPass(createAArch64BranchTargetsPass());
  // Relax conditional branch instructions if they're otherwise out of
  // range of their destination.
  if (BranchRelaxation)
    addPass(&BranchRelaxationPassID);

  if (TM->getOptLevel() != CodeGenOptLevel::None && EnableCompressJumpTables)
    addPass(createAArch64CompressJumpTablesPass());
}

void AArch64PassConfig::addPreEmitPass2() {
  // SVE bundles move prefixes with destructive operations. BLR_RVMARKER pseudo
  // instructions are lowered to bundles as well.
  addPass(createUnpackMachineBundles(nullptr));
}

bool AArch64PassConfig::addRegAssignAndRewriteOptimized() {
  addPass(createAArch64PostCoalescerPass());
  return TargetPassConfig::addRegAssignAndRewriteOptimized();
}

MachineFunctionInfo *AArch64TargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return AArch64FunctionInfo::create<AArch64FunctionInfo>(
      Allocator, F, static_cast<const AArch64Subtarget *>(STI));
}

yaml::MachineFunctionInfo *
AArch64TargetMachine::createDefaultFuncInfoYAML() const {
  return new yaml::AArch64FunctionInfo();
}

yaml::MachineFunctionInfo *
AArch64TargetMachine::convertFuncInfoToYAML(const MachineFunction &MF) const {
  const auto *MFI = MF.getInfo<AArch64FunctionInfo>();
  return new yaml::AArch64FunctionInfo(*MFI);
}

bool AArch64TargetMachine::parseMachineFunctionInfo(
    const yaml::MachineFunctionInfo &MFI, PerFunctionMIParsingState &PFS,
    SMDiagnostic &Error, SMRange &SourceRange) const {
  const auto &YamlMFI = static_cast<const yaml::AArch64FunctionInfo &>(MFI);
  MachineFunction &MF = PFS.MF;
  MF.getInfo<AArch64FunctionInfo>()->initializeBaseYamlFields(YamlMFI);
  return false;
}
