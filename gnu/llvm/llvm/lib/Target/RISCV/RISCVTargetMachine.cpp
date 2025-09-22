//===-- RISCVTargetMachine.cpp - Define TargetMachine for RISC-V ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the info about RISC-V target spec.
//
//===----------------------------------------------------------------------===//

#include "RISCVTargetMachine.h"
#include "MCTargetDesc/RISCVBaseInfo.h"
#include "RISCV.h"
#include "RISCVMachineFunctionInfo.h"
#include "RISCVTargetObjectFile.h"
#include "RISCVTargetTransformInfo.h"
#include "TargetInfo/RISCVTargetInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/GlobalISel/IRTranslator.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelect.h"
#include "llvm/CodeGen/GlobalISel/Legalizer.h"
#include "llvm/CodeGen/GlobalISel/RegBankSelect.h"
#include "llvm/CodeGen/MIRParser/MIParser.h"
#include "llvm/CodeGen/MIRYamlMapping.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/MacroFusion.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Vectorize/LoopIdiomVectorize.h"
#include <optional>
using namespace llvm;

static cl::opt<bool> EnableRedundantCopyElimination(
    "riscv-enable-copyelim",
    cl::desc("Enable the redundant copy elimination pass"), cl::init(true),
    cl::Hidden);

// FIXME: Unify control over GlobalMerge.
static cl::opt<cl::boolOrDefault>
    EnableGlobalMerge("riscv-enable-global-merge", cl::Hidden,
                      cl::desc("Enable the global merge pass"));

static cl::opt<bool>
    EnableMachineCombiner("riscv-enable-machine-combiner",
                          cl::desc("Enable the machine combiner pass"),
                          cl::init(true), cl::Hidden);

static cl::opt<unsigned> RVVVectorBitsMaxOpt(
    "riscv-v-vector-bits-max",
    cl::desc("Assume V extension vector registers are at most this big, "
             "with zero meaning no maximum size is assumed."),
    cl::init(0), cl::Hidden);

static cl::opt<int> RVVVectorBitsMinOpt(
    "riscv-v-vector-bits-min",
    cl::desc("Assume V extension vector registers are at least this big, "
             "with zero meaning no minimum size is assumed. A value of -1 "
             "means use Zvl*b extension. This is primarily used to enable "
             "autovectorization with fixed width vectors."),
    cl::init(-1), cl::Hidden);

static cl::opt<bool> EnableRISCVCopyPropagation(
    "riscv-enable-copy-propagation",
    cl::desc("Enable the copy propagation with RISC-V copy instr"),
    cl::init(true), cl::Hidden);

static cl::opt<bool> EnableRISCVDeadRegisterElimination(
    "riscv-enable-dead-defs", cl::Hidden,
    cl::desc("Enable the pass that removes dead"
             " definitons and replaces stores to"
             " them with stores to x0"),
    cl::init(true));

static cl::opt<bool>
    EnableSinkFold("riscv-enable-sink-fold",
                   cl::desc("Enable sinking and folding of instruction copies"),
                   cl::init(true), cl::Hidden);

static cl::opt<bool>
    EnableLoopDataPrefetch("riscv-enable-loop-data-prefetch", cl::Hidden,
                           cl::desc("Enable the loop data prefetch pass"),
                           cl::init(true));

static cl::opt<bool> EnableMISchedLoadClustering(
    "riscv-misched-load-clustering", cl::Hidden,
    cl::desc("Enable load clustering in the machine scheduler"),
    cl::init(false));

static cl::opt<bool> EnableVSETVLIAfterRVVRegAlloc(
    "riscv-vsetvl-after-rvv-regalloc", cl::Hidden,
    cl::desc("Insert vsetvls after vector register allocation"),
    cl::init(true));

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeRISCVTarget() {
  RegisterTargetMachine<RISCVTargetMachine> X(getTheRISCV32Target());
  RegisterTargetMachine<RISCVTargetMachine> Y(getTheRISCV64Target());
  auto *PR = PassRegistry::getPassRegistry();
  initializeGlobalISel(*PR);
  initializeRISCVO0PreLegalizerCombinerPass(*PR);
  initializeRISCVPreLegalizerCombinerPass(*PR);
  initializeRISCVPostLegalizerCombinerPass(*PR);
  initializeKCFIPass(*PR);
  initializeRISCVDeadRegisterDefinitionsPass(*PR);
  initializeRISCVMakeCompressibleOptPass(*PR);
  initializeRISCVGatherScatterLoweringPass(*PR);
  initializeRISCVCodeGenPreparePass(*PR);
  initializeRISCVPostRAExpandPseudoPass(*PR);
  initializeRISCVMergeBaseOffsetOptPass(*PR);
  initializeRISCVOptWInstrsPass(*PR);
  initializeRISCVPreRAExpandPseudoPass(*PR);
  initializeRISCVExpandPseudoPass(*PR);
  initializeRISCVVectorPeepholePass(*PR);
  initializeRISCVInsertVSETVLIPass(*PR);
  initializeRISCVInsertReadWriteCSRPass(*PR);
  initializeRISCVInsertWriteVXRMPass(*PR);
  initializeRISCVDAGToDAGISelLegacyPass(*PR);
  initializeRISCVMoveMergePass(*PR);
  initializeRISCVPushPopOptPass(*PR);
}

static StringRef computeDataLayout(const Triple &TT,
                                   const TargetOptions &Options) {
  StringRef ABIName = Options.MCOptions.getABIName();
  if (TT.isArch64Bit()) {
    if (ABIName == "lp64e")
      return "e-m:e-p:64:64-i64:64-i128:128-n32:64-S64";

    return "e-m:e-p:64:64-i64:64-i128:128-n32:64-S128";
  }
  assert(TT.isArch32Bit() && "only RV32 and RV64 are currently supported");

  if (ABIName == "ilp32e")
    return "e-m:e-p:32:32-i64:64-n32-S32";

  return "e-m:e-p:32:32-i64:64-n32-S128";
}

static Reloc::Model getEffectiveRelocModel(const Triple &TT,
                                           std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

RISCVTargetMachine::RISCVTargetMachine(const Target &T, const Triple &TT,
                                       StringRef CPU, StringRef FS,
                                       const TargetOptions &Options,
                                       std::optional<Reloc::Model> RM,
                                       std::optional<CodeModel::Model> CM,
                                       CodeGenOptLevel OL, bool JIT)
    : LLVMTargetMachine(T, computeDataLayout(TT, Options), TT, CPU, FS, Options,
                        getEffectiveRelocModel(TT, RM),
                        getEffectiveCodeModel(CM, CodeModel::Small), OL),
      TLOF(std::make_unique<RISCVELFTargetObjectFile>()) {
  initAsmInfo();

  // RISC-V supports the MachineOutliner.
  setMachineOutliner(true);
  setSupportsDefaultOutlining(true);

  if (TT.isOSFuchsia() && !TT.isArch64Bit())
    report_fatal_error("Fuchsia is only supported for 64-bit");
}

const RISCVSubtarget *
RISCVTargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute TuneAttr = F.getFnAttribute("tune-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

  std::string CPU =
      CPUAttr.isValid() ? CPUAttr.getValueAsString().str() : TargetCPU;
  std::string TuneCPU =
      TuneAttr.isValid() ? TuneAttr.getValueAsString().str() : CPU;
  std::string FS =
      FSAttr.isValid() ? FSAttr.getValueAsString().str() : TargetFS;

  unsigned RVVBitsMin = RVVVectorBitsMinOpt;
  unsigned RVVBitsMax = RVVVectorBitsMaxOpt;

  Attribute VScaleRangeAttr = F.getFnAttribute(Attribute::VScaleRange);
  if (VScaleRangeAttr.isValid()) {
    if (!RVVVectorBitsMinOpt.getNumOccurrences())
      RVVBitsMin = VScaleRangeAttr.getVScaleRangeMin() * RISCV::RVVBitsPerBlock;
    std::optional<unsigned> VScaleMax = VScaleRangeAttr.getVScaleRangeMax();
    if (VScaleMax.has_value() && !RVVVectorBitsMaxOpt.getNumOccurrences())
      RVVBitsMax = *VScaleMax * RISCV::RVVBitsPerBlock;
  }

  if (RVVBitsMin != -1U) {
    // FIXME: Change to >= 32 when VLEN = 32 is supported.
    assert((RVVBitsMin == 0 || (RVVBitsMin >= 64 && RVVBitsMin <= 65536 &&
                                isPowerOf2_32(RVVBitsMin))) &&
           "V or Zve* extension requires vector length to be in the range of "
           "64 to 65536 and a power 2!");
    assert((RVVBitsMax >= RVVBitsMin || RVVBitsMax == 0) &&
           "Minimum V extension vector length should not be larger than its "
           "maximum!");
  }
  assert((RVVBitsMax == 0 || (RVVBitsMax >= 64 && RVVBitsMax <= 65536 &&
                              isPowerOf2_32(RVVBitsMax))) &&
         "V or Zve* extension requires vector length to be in the range of "
         "64 to 65536 and a power 2!");

  if (RVVBitsMin != -1U) {
    if (RVVBitsMax != 0) {
      RVVBitsMin = std::min(RVVBitsMin, RVVBitsMax);
      RVVBitsMax = std::max(RVVBitsMin, RVVBitsMax);
    }

    RVVBitsMin = llvm::bit_floor(
        (RVVBitsMin < 64 || RVVBitsMin > 65536) ? 0 : RVVBitsMin);
  }
  RVVBitsMax =
      llvm::bit_floor((RVVBitsMax < 64 || RVVBitsMax > 65536) ? 0 : RVVBitsMax);

  SmallString<512> Key;
  raw_svector_ostream(Key) << "RVVMin" << RVVBitsMin << "RVVMax" << RVVBitsMax
                           << CPU << TuneCPU << FS;
  auto &I = SubtargetMap[Key];
  if (!I) {
    // This needs to be done before we create a new subtarget since any
    // creation will depend on the TM and the code generation flags on the
    // function that reside in TargetOptions.
    resetTargetOptions(F);
    auto ABIName = Options.MCOptions.getABIName();
    if (const MDString *ModuleTargetABI = dyn_cast_or_null<MDString>(
            F.getParent()->getModuleFlag("target-abi"))) {
      auto TargetABI = RISCVABI::getTargetABI(ABIName);
      if (TargetABI != RISCVABI::ABI_Unknown &&
          ModuleTargetABI->getString() != ABIName) {
        report_fatal_error("-target-abi option != target-abi module flag");
      }
      ABIName = ModuleTargetABI->getString();
    }
    I = std::make_unique<RISCVSubtarget>(
        TargetTriple, CPU, TuneCPU, FS, ABIName, RVVBitsMin, RVVBitsMax, *this);
  }
  return I.get();
}

MachineFunctionInfo *RISCVTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return RISCVMachineFunctionInfo::create<RISCVMachineFunctionInfo>(Allocator,
                                                                    F, STI);
}

TargetTransformInfo
RISCVTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(RISCVTTIImpl(this, F));
}

// A RISC-V hart has a single byte-addressable address space of 2^XLEN bytes
// for all memory accesses, so it is reasonable to assume that an
// implementation has no-op address space casts. If an implementation makes a
// change to this, they can override it here.
bool RISCVTargetMachine::isNoopAddrSpaceCast(unsigned SrcAS,
                                             unsigned DstAS) const {
  return true;
}

namespace {

class RVVRegisterRegAlloc : public RegisterRegAllocBase<RVVRegisterRegAlloc> {
public:
  RVVRegisterRegAlloc(const char *N, const char *D, FunctionPassCtor C)
      : RegisterRegAllocBase(N, D, C) {}
};

static bool onlyAllocateRVVReg(const TargetRegisterInfo &TRI,
                               const MachineRegisterInfo &MRI,
                               const Register Reg) {
  const TargetRegisterClass *RC = MRI.getRegClass(Reg);
  return RISCVRegisterInfo::isRVVRegClass(RC);
}

static FunctionPass *useDefaultRegisterAllocator() { return nullptr; }

static llvm::once_flag InitializeDefaultRVVRegisterAllocatorFlag;

/// -riscv-rvv-regalloc=<fast|basic|greedy> command line option.
/// This option could designate the rvv register allocator only.
/// For example: -riscv-rvv-regalloc=basic
static cl::opt<RVVRegisterRegAlloc::FunctionPassCtor, false,
               RegisterPassParser<RVVRegisterRegAlloc>>
    RVVRegAlloc("riscv-rvv-regalloc", cl::Hidden,
                cl::init(&useDefaultRegisterAllocator),
                cl::desc("Register allocator to use for RVV register."));

static void initializeDefaultRVVRegisterAllocatorOnce() {
  RegisterRegAlloc::FunctionPassCtor Ctor = RVVRegisterRegAlloc::getDefault();

  if (!Ctor) {
    Ctor = RVVRegAlloc;
    RVVRegisterRegAlloc::setDefault(RVVRegAlloc);
  }
}

static FunctionPass *createBasicRVVRegisterAllocator() {
  return createBasicRegisterAllocator(onlyAllocateRVVReg);
}

static FunctionPass *createGreedyRVVRegisterAllocator() {
  return createGreedyRegisterAllocator(onlyAllocateRVVReg);
}

static FunctionPass *createFastRVVRegisterAllocator() {
  return createFastRegisterAllocator(onlyAllocateRVVReg, false);
}

static RVVRegisterRegAlloc basicRegAllocRVVReg("basic",
                                               "basic register allocator",
                                               createBasicRVVRegisterAllocator);
static RVVRegisterRegAlloc
    greedyRegAllocRVVReg("greedy", "greedy register allocator",
                         createGreedyRVVRegisterAllocator);

static RVVRegisterRegAlloc fastRegAllocRVVReg("fast", "fast register allocator",
                                              createFastRVVRegisterAllocator);

class RISCVPassConfig : public TargetPassConfig {
public:
  RISCVPassConfig(RISCVTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {
    if (TM.getOptLevel() != CodeGenOptLevel::None)
      substitutePass(&PostRASchedulerID, &PostMachineSchedulerID);
    setEnableSinkAndFold(EnableSinkFold);
  }

  RISCVTargetMachine &getRISCVTargetMachine() const {
    return getTM<RISCVTargetMachine>();
  }

  ScheduleDAGInstrs *
  createMachineScheduler(MachineSchedContext *C) const override {
    ScheduleDAGMILive *DAG = nullptr;
    if (EnableMISchedLoadClustering) {
      DAG = createGenericSchedLive(C);
      DAG->addMutation(createLoadClusterDAGMutation(
          DAG->TII, DAG->TRI, /*ReorderWhileClustering=*/true));
    }
    return DAG;
  }

  void addIRPasses() override;
  bool addPreISel() override;
  void addCodeGenPrepare() override;
  bool addInstSelector() override;
  bool addIRTranslator() override;
  void addPreLegalizeMachineIR() override;
  bool addLegalizeMachineIR() override;
  void addPreRegBankSelect() override;
  bool addRegBankSelect() override;
  bool addGlobalInstructionSelect() override;
  void addPreEmitPass() override;
  void addPreEmitPass2() override;
  void addPreSched2() override;
  void addMachineSSAOptimization() override;
  FunctionPass *createRVVRegAllocPass(bool Optimized);
  bool addRegAssignAndRewriteFast() override;
  bool addRegAssignAndRewriteOptimized() override;
  void addPreRegAlloc() override;
  void addPostRegAlloc() override;
  void addFastRegAlloc() override;
};
} // namespace

TargetPassConfig *RISCVTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new RISCVPassConfig(*this, PM);
}

FunctionPass *RISCVPassConfig::createRVVRegAllocPass(bool Optimized) {
  // Initialize the global default.
  llvm::call_once(InitializeDefaultRVVRegisterAllocatorFlag,
                  initializeDefaultRVVRegisterAllocatorOnce);

  RegisterRegAlloc::FunctionPassCtor Ctor = RVVRegisterRegAlloc::getDefault();
  if (Ctor != useDefaultRegisterAllocator)
    return Ctor();

  if (Optimized)
    return createGreedyRVVRegisterAllocator();

  return createFastRVVRegisterAllocator();
}

bool RISCVPassConfig::addRegAssignAndRewriteFast() {
  addPass(createRVVRegAllocPass(false));
  if (EnableVSETVLIAfterRVVRegAlloc)
    addPass(createRISCVInsertVSETVLIPass());
  if (TM->getOptLevel() != CodeGenOptLevel::None &&
      EnableRISCVDeadRegisterElimination)
    addPass(createRISCVDeadRegisterDefinitionsPass());
  return TargetPassConfig::addRegAssignAndRewriteFast();
}

bool RISCVPassConfig::addRegAssignAndRewriteOptimized() {
  addPass(createRVVRegAllocPass(true));
  addPass(createVirtRegRewriter(false));
  if (EnableVSETVLIAfterRVVRegAlloc)
    addPass(createRISCVInsertVSETVLIPass());
  if (TM->getOptLevel() != CodeGenOptLevel::None &&
      EnableRISCVDeadRegisterElimination)
    addPass(createRISCVDeadRegisterDefinitionsPass());
  return TargetPassConfig::addRegAssignAndRewriteOptimized();
}

void RISCVPassConfig::addIRPasses() {
  addPass(createAtomicExpandLegacyPass());

  if (getOptLevel() != CodeGenOptLevel::None) {
    if (EnableLoopDataPrefetch)
      addPass(createLoopDataPrefetchPass());

    addPass(createRISCVGatherScatterLoweringPass());
    addPass(createInterleavedAccessPass());
    addPass(createRISCVCodeGenPreparePass());
  }

  TargetPassConfig::addIRPasses();
}

bool RISCVPassConfig::addPreISel() {
  if (TM->getOptLevel() != CodeGenOptLevel::None) {
    // Add a barrier before instruction selection so that we will not get
    // deleted block address after enabling default outlining. See D99707 for
    // more details.
    addPass(createBarrierNoopPass());
  }

  if (EnableGlobalMerge == cl::BOU_TRUE) {
    addPass(createGlobalMergePass(TM, /* MaxOffset */ 2047,
                                  /* OnlyOptimizeForSize */ false,
                                  /* MergeExternalByDefault */ true));
  }

  return false;
}

void RISCVPassConfig::addCodeGenPrepare() {
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createTypePromotionLegacyPass());
  TargetPassConfig::addCodeGenPrepare();
}

bool RISCVPassConfig::addInstSelector() {
  addPass(createRISCVISelDag(getRISCVTargetMachine(), getOptLevel()));

  return false;
}

bool RISCVPassConfig::addIRTranslator() {
  addPass(new IRTranslator(getOptLevel()));
  return false;
}

void RISCVPassConfig::addPreLegalizeMachineIR() {
  if (getOptLevel() == CodeGenOptLevel::None) {
    addPass(createRISCVO0PreLegalizerCombiner());
  } else {
    addPass(createRISCVPreLegalizerCombiner());
  }
}

bool RISCVPassConfig::addLegalizeMachineIR() {
  addPass(new Legalizer());
  return false;
}

void RISCVPassConfig::addPreRegBankSelect() {
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createRISCVPostLegalizerCombiner());
}

bool RISCVPassConfig::addRegBankSelect() {
  addPass(new RegBankSelect());
  return false;
}

bool RISCVPassConfig::addGlobalInstructionSelect() {
  addPass(new InstructionSelect(getOptLevel()));
  return false;
}

void RISCVPassConfig::addPreSched2() {
  addPass(createRISCVPostRAExpandPseudoPass());

  // Emit KCFI checks for indirect calls.
  addPass(createKCFIPass());
}

void RISCVPassConfig::addPreEmitPass() {
  // TODO: It would potentially be better to schedule copy propagation after
  // expanding pseudos (in addPreEmitPass2). However, performing copy
  // propagation after the machine outliner (which runs after addPreEmitPass)
  // currently leads to incorrect code-gen, where copies to registers within
  // outlined functions are removed erroneously.
  if (TM->getOptLevel() >= CodeGenOptLevel::Default &&
      EnableRISCVCopyPropagation)
    addPass(createMachineCopyPropagationPass(true));
  addPass(&BranchRelaxationPassID);
  addPass(createRISCVMakeCompressibleOptPass());
}

void RISCVPassConfig::addPreEmitPass2() {
  if (TM->getOptLevel() != CodeGenOptLevel::None) {
    addPass(createRISCVMoveMergePass());
    // Schedule PushPop Optimization before expansion of Pseudo instruction,
    // ensuring return instruction is detected correctly.
    addPass(createRISCVPushPopOptimizationPass());
  }
  addPass(createRISCVExpandPseudoPass());

  // Schedule the expansion of AMOs at the last possible moment, avoiding the
  // possibility for other passes to break the requirements for forward
  // progress in the LR/SC block.
  addPass(createRISCVExpandAtomicPseudoPass());

  // KCFI indirect call checks are lowered to a bundle.
  addPass(createUnpackMachineBundles([&](const MachineFunction &MF) {
    return MF.getFunction().getParent()->getModuleFlag("kcfi");
  }));
}

void RISCVPassConfig::addMachineSSAOptimization() {
  addPass(createRISCVVectorPeepholePass());

  TargetPassConfig::addMachineSSAOptimization();

  if (EnableMachineCombiner)
    addPass(&MachineCombinerID);

  if (TM->getTargetTriple().isRISCV64()) {
    addPass(createRISCVOptWInstrsPass());
  }
}

void RISCVPassConfig::addPreRegAlloc() {
  addPass(createRISCVPreRAExpandPseudoPass());
  if (TM->getOptLevel() != CodeGenOptLevel::None)
    addPass(createRISCVMergeBaseOffsetOptPass());

  addPass(createRISCVInsertReadWriteCSRPass());
  addPass(createRISCVInsertWriteVXRMPass());

  // Run RISCVInsertVSETVLI after PHI elimination. On O1 and above do it after
  // register coalescing so needVSETVLIPHI doesn't need to look through COPYs.
  if (!EnableVSETVLIAfterRVVRegAlloc) {
    if (TM->getOptLevel() == CodeGenOptLevel::None)
      insertPass(&PHIEliminationID, &RISCVInsertVSETVLIID);
    else
      insertPass(&RegisterCoalescerID, &RISCVInsertVSETVLIID);
  }
}

void RISCVPassConfig::addFastRegAlloc() {
  addPass(&InitUndefID);
  TargetPassConfig::addFastRegAlloc();
}


void RISCVPassConfig::addPostRegAlloc() {
  if (TM->getOptLevel() != CodeGenOptLevel::None &&
      EnableRedundantCopyElimination)
    addPass(createRISCVRedundantCopyEliminationPass());
}

void RISCVTargetMachine::registerPassBuilderCallbacks(PassBuilder &PB) {
  PB.registerLateLoopOptimizationsEPCallback([=](LoopPassManager &LPM,
                                                 OptimizationLevel Level) {
    LPM.addPass(LoopIdiomVectorizePass(LoopIdiomVectorizeStyle::Predicated));
  });
}

yaml::MachineFunctionInfo *
RISCVTargetMachine::createDefaultFuncInfoYAML() const {
  return new yaml::RISCVMachineFunctionInfo();
}

yaml::MachineFunctionInfo *
RISCVTargetMachine::convertFuncInfoToYAML(const MachineFunction &MF) const {
  const auto *MFI = MF.getInfo<RISCVMachineFunctionInfo>();
  return new yaml::RISCVMachineFunctionInfo(*MFI);
}

bool RISCVTargetMachine::parseMachineFunctionInfo(
    const yaml::MachineFunctionInfo &MFI, PerFunctionMIParsingState &PFS,
    SMDiagnostic &Error, SMRange &SourceRange) const {
  const auto &YamlMFI =
      static_cast<const yaml::RISCVMachineFunctionInfo &>(MFI);
  PFS.MF.getInfo<RISCVMachineFunctionInfo>()->initializeBaseYamlFields(YamlMFI);
  return false;
}
