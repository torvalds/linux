//===-- HexagonTargetMachine.cpp - Define TargetMachine for Hexagon -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the info about Hexagon target spec.
//
//===----------------------------------------------------------------------===//

#include "HexagonTargetMachine.h"
#include "Hexagon.h"
#include "HexagonISelLowering.h"
#include "HexagonLoopIdiomRecognition.h"
#include "HexagonMachineFunctionInfo.h"
#include "HexagonMachineScheduler.h"
#include "HexagonTargetObjectFile.h"
#include "HexagonTargetTransformInfo.h"
#include "HexagonVectorLoopCarriedReuse.h"
#include "TargetInfo/HexagonTargetInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/VLIWMachineScheduler.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Transforms/Scalar.h"
#include <optional>

using namespace llvm;

static cl::opt<bool>
    EnableCExtOpt("hexagon-cext", cl::Hidden, cl::init(true),
                  cl::desc("Enable Hexagon constant-extender optimization"));

static cl::opt<bool> EnableRDFOpt("rdf-opt", cl::Hidden, cl::init(true),
                                  cl::desc("Enable RDF-based optimizations"));

cl::opt<unsigned> RDFFuncBlockLimit(
    "rdf-bb-limit", cl::Hidden, cl::init(1000),
    cl::desc("Basic block limit for a function for RDF optimizations"));

static cl::opt<bool>
    DisableHardwareLoops("disable-hexagon-hwloops", cl::Hidden,
                         cl::desc("Disable Hardware Loops for Hexagon target"));

static cl::opt<bool>
    DisableAModeOpt("disable-hexagon-amodeopt", cl::Hidden,
                    cl::desc("Disable Hexagon Addressing Mode Optimization"));

static cl::opt<bool>
    DisableHexagonCFGOpt("disable-hexagon-cfgopt", cl::Hidden,
                         cl::desc("Disable Hexagon CFG Optimization"));

static cl::opt<bool>
    DisableHCP("disable-hcp", cl::Hidden,
               cl::desc("Disable Hexagon constant propagation"));

static cl::opt<bool> DisableStoreWidening("disable-store-widen", cl::Hidden,
                                          cl::init(false),
                                          cl::desc("Disable store widening"));

static cl::opt<bool> EnableExpandCondsets("hexagon-expand-condsets",
                                          cl::init(true), cl::Hidden,
                                          cl::desc("Early expansion of MUX"));

static cl::opt<bool> EnableTfrCleanup("hexagon-tfr-cleanup", cl::init(true),
                                      cl::Hidden,
                                      cl::desc("Cleanup of TFRs/COPYs"));

static cl::opt<bool> EnableEarlyIf("hexagon-eif", cl::init(true), cl::Hidden,
                                   cl::desc("Enable early if-conversion"));

static cl::opt<bool> EnableCopyHoist("hexagon-copy-hoist", cl::init(true),
                                     cl::Hidden, cl::ZeroOrMore,
                                     cl::desc("Enable Hexagon copy hoisting"));

static cl::opt<bool>
    EnableGenInsert("hexagon-insert", cl::init(true), cl::Hidden,
                    cl::desc("Generate \"insert\" instructions"));

static cl::opt<bool>
    EnableCommGEP("hexagon-commgep", cl::init(true), cl::Hidden,
                  cl::desc("Enable commoning of GEP instructions"));

static cl::opt<bool>
    EnableGenExtract("hexagon-extract", cl::init(true), cl::Hidden,
                     cl::desc("Generate \"extract\" instructions"));

static cl::opt<bool> EnableGenMux(
    "hexagon-mux", cl::init(true), cl::Hidden,
    cl::desc("Enable converting conditional transfers into MUX instructions"));

static cl::opt<bool>
    EnableGenPred("hexagon-gen-pred", cl::init(true), cl::Hidden,
                  cl::desc("Enable conversion of arithmetic operations to "
                           "predicate instructions"));

static cl::opt<bool>
    EnableLoopPrefetch("hexagon-loop-prefetch", cl::Hidden,
                       cl::desc("Enable loop data prefetch on Hexagon"));

static cl::opt<bool>
    DisableHSDR("disable-hsdr", cl::init(false), cl::Hidden,
                cl::desc("Disable splitting double registers"));

static cl::opt<bool>
    EnableGenMemAbs("hexagon-mem-abs", cl::init(true), cl::Hidden,
                    cl::desc("Generate absolute set instructions"));

static cl::opt<bool> EnableBitSimplify("hexagon-bit", cl::init(true),
                                       cl::Hidden,
                                       cl::desc("Bit simplification"));

static cl::opt<bool> EnableLoopResched("hexagon-loop-resched", cl::init(true),
                                       cl::Hidden,
                                       cl::desc("Loop rescheduling"));

static cl::opt<bool> HexagonNoOpt("hexagon-noopt", cl::init(false), cl::Hidden,
                                  cl::desc("Disable backend optimizations"));

static cl::opt<bool>
    EnableVectorPrint("enable-hexagon-vector-print", cl::Hidden,
                      cl::desc("Enable Hexagon Vector print instr pass"));

static cl::opt<bool>
    EnableVExtractOpt("hexagon-opt-vextract", cl::Hidden, cl::init(true),
                      cl::desc("Enable vextract optimization"));

static cl::opt<bool>
    EnableVectorCombine("hexagon-vector-combine", cl::Hidden, cl::init(true),
                        cl::desc("Enable HVX vector combining"));

static cl::opt<bool> EnableInitialCFGCleanup(
    "hexagon-initial-cfg-cleanup", cl::Hidden, cl::init(true),
    cl::desc("Simplify the CFG after atomic expansion pass"));

static cl::opt<bool> EnableInstSimplify("hexagon-instsimplify", cl::Hidden,
                                        cl::init(true),
                                        cl::desc("Enable instsimplify"));

/// HexagonTargetMachineModule - Note that this is used on hosts that
/// cannot link in a library unless there are references into the
/// library.  In particular, it seems that it is not possible to get
/// things to work on Win32 without this.  Though it is unused, do not
/// remove it.
extern "C" int HexagonTargetMachineModule;
int HexagonTargetMachineModule = 0;

static ScheduleDAGInstrs *createVLIWMachineSched(MachineSchedContext *C) {
  ScheduleDAGMILive *DAG = new VLIWMachineScheduler(
      C, std::make_unique<HexagonConvergingVLIWScheduler>());
  DAG->addMutation(std::make_unique<HexagonSubtarget::UsrOverflowMutation>());
  DAG->addMutation(std::make_unique<HexagonSubtarget::HVXMemLatencyMutation>());
  DAG->addMutation(std::make_unique<HexagonSubtarget::CallMutation>());
  DAG->addMutation(createCopyConstrainDAGMutation(DAG->TII, DAG->TRI));
  return DAG;
}

static MachineSchedRegistry
    SchedCustomRegistry("hexagon", "Run Hexagon's custom scheduler",
                        createVLIWMachineSched);

namespace llvm {
extern char &HexagonCopyHoistingID;
extern char &HexagonExpandCondsetsID;
extern char &HexagonTfrCleanupID;
void initializeHexagonBitSimplifyPass(PassRegistry &);
void initializeHexagonCopyHoistingPass(PassRegistry &);
void initializeHexagonConstExtendersPass(PassRegistry &);
void initializeHexagonConstPropagationPass(PassRegistry &);
void initializeHexagonCopyToCombinePass(PassRegistry &);
void initializeHexagonEarlyIfConversionPass(PassRegistry &);
void initializeHexagonExpandCondsetsPass(PassRegistry &);
void initializeHexagonGenMemAbsolutePass(PassRegistry &);
void initializeHexagonGenMuxPass(PassRegistry &);
void initializeHexagonHardwareLoopsPass(PassRegistry &);
void initializeHexagonLoopIdiomRecognizeLegacyPassPass(PassRegistry &);
void initializeHexagonLoopAlignPass(PassRegistry &);
void initializeHexagonNewValueJumpPass(PassRegistry &);
void initializeHexagonOptAddrModePass(PassRegistry &);
void initializeHexagonPacketizerPass(PassRegistry &);
void initializeHexagonRDFOptPass(PassRegistry &);
void initializeHexagonSplitDoubleRegsPass(PassRegistry &);
void initializeHexagonTfrCleanupPass(PassRegistry &);
void initializeHexagonVExtractPass(PassRegistry &);
void initializeHexagonVectorCombineLegacyPass(PassRegistry &);
void initializeHexagonVectorLoopCarriedReuseLegacyPassPass(PassRegistry &);
Pass *createHexagonLoopIdiomPass();
Pass *createHexagonVectorLoopCarriedReuseLegacyPass();

FunctionPass *createHexagonBitSimplify();
FunctionPass *createHexagonBranchRelaxation();
FunctionPass *createHexagonCallFrameInformation();
FunctionPass *createHexagonCFGOptimizer();
FunctionPass *createHexagonCommonGEP();
FunctionPass *createHexagonConstExtenders();
FunctionPass *createHexagonConstPropagationPass();
FunctionPass *createHexagonCopyHoisting();
FunctionPass *createHexagonCopyToCombine();
FunctionPass *createHexagonEarlyIfConversion();
FunctionPass *createHexagonFixupHwLoops();
FunctionPass *createHexagonGenExtract();
FunctionPass *createHexagonGenInsert();
FunctionPass *createHexagonGenMemAbsolute();
FunctionPass *createHexagonGenMux();
FunctionPass *createHexagonGenPredicate();
FunctionPass *createHexagonHardwareLoops();
FunctionPass *createHexagonISelDag(HexagonTargetMachine &TM,
                                   CodeGenOptLevel OptLevel);
FunctionPass *createHexagonLoopAlign();
FunctionPass *createHexagonLoopRescheduling();
FunctionPass *createHexagonNewValueJump();
FunctionPass *createHexagonOptAddrMode();
FunctionPass *createHexagonOptimizeSZextends();
FunctionPass *createHexagonPacketizer(bool Minimal);
FunctionPass *createHexagonPeephole();
FunctionPass *createHexagonRDFOpt();
FunctionPass *createHexagonSplitConst32AndConst64();
FunctionPass *createHexagonSplitDoubleRegs();
FunctionPass *createHexagonStoreWidening();
FunctionPass *createHexagonTfrCleanup();
FunctionPass *createHexagonVectorCombineLegacyPass();
FunctionPass *createHexagonVectorPrint();
FunctionPass *createHexagonVExtract();
} // namespace llvm

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM) {
  return RM.value_or(Reloc::Static);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeHexagonTarget() {
  // Register the target.
  RegisterTargetMachine<HexagonTargetMachine> X(getTheHexagonTarget());

  PassRegistry &PR = *PassRegistry::getPassRegistry();
  initializeHexagonBitSimplifyPass(PR);
  initializeHexagonConstExtendersPass(PR);
  initializeHexagonConstPropagationPass(PR);
  initializeHexagonCopyToCombinePass(PR);
  initializeHexagonEarlyIfConversionPass(PR);
  initializeHexagonGenMemAbsolutePass(PR);
  initializeHexagonGenMuxPass(PR);
  initializeHexagonHardwareLoopsPass(PR);
  initializeHexagonLoopIdiomRecognizeLegacyPassPass(PR);
  initializeHexagonNewValueJumpPass(PR);
  initializeHexagonOptAddrModePass(PR);
  initializeHexagonPacketizerPass(PR);
  initializeHexagonRDFOptPass(PR);
  initializeHexagonSplitDoubleRegsPass(PR);
  initializeHexagonVectorCombineLegacyPass(PR);
  initializeHexagonVectorLoopCarriedReuseLegacyPassPass(PR);
  initializeHexagonVExtractPass(PR);
  initializeHexagonDAGToDAGISelLegacyPass(PR);
}

HexagonTargetMachine::HexagonTargetMachine(const Target &T, const Triple &TT,
                                           StringRef CPU, StringRef FS,
                                           const TargetOptions &Options,
                                           std::optional<Reloc::Model> RM,
                                           std::optional<CodeModel::Model> CM,
                                           CodeGenOptLevel OL, bool JIT)
    // Specify the vector alignment explicitly. For v512x1, the calculated
    // alignment would be 512*alignment(i1), which is 512 bytes, instead of
    // the required minimum of 64 bytes.
    : LLVMTargetMachine(
          T,
          "e-m:e-p:32:32:32-a:0-n16:32-"
          "i64:64:64-i32:32:32-i16:16:16-i1:8:8-f32:32:32-f64:64:64-"
          "v32:32:32-v64:64:64-v512:512:512-v1024:1024:1024-v2048:2048:2048",
          TT, CPU, FS, Options, getEffectiveRelocModel(RM),
          getEffectiveCodeModel(CM, CodeModel::Small),
          (HexagonNoOpt ? CodeGenOptLevel::None : OL)),
      TLOF(std::make_unique<HexagonTargetObjectFile>()),
      Subtarget(Triple(TT), CPU, FS, *this) {
  initializeHexagonCopyHoistingPass(*PassRegistry::getPassRegistry());
  initializeHexagonExpandCondsetsPass(*PassRegistry::getPassRegistry());
  initializeHexagonLoopAlignPass(*PassRegistry::getPassRegistry());
  initializeHexagonTfrCleanupPass(*PassRegistry::getPassRegistry());
  initAsmInfo();
}

const HexagonSubtarget *
HexagonTargetMachine::getSubtargetImpl(const Function &F) const {
  AttributeList FnAttrs = F.getAttributes();
  Attribute CPUAttr = FnAttrs.getFnAttr("target-cpu");
  Attribute FSAttr = FnAttrs.getFnAttr("target-features");

  std::string CPU =
      CPUAttr.isValid() ? CPUAttr.getValueAsString().str() : TargetCPU;
  std::string FS =
      FSAttr.isValid() ? FSAttr.getValueAsString().str() : TargetFS;
  // Append the preexisting target features last, so that +mattr overrides
  // the "unsafe-fp-math" function attribute.
  // Creating a separate target feature is not strictly necessary, it only
  // exists to make "unsafe-fp-math" force creating a new subtarget.

  if (F.getFnAttribute("unsafe-fp-math").getValueAsBool())
    FS = FS.empty() ? "+unsafe-fp" : "+unsafe-fp," + FS;

  auto &I = SubtargetMap[CPU + FS];
  if (!I) {
    // This needs to be done before we create a new subtarget since any
    // creation will depend on the TM and the code generation flags on the
    // function that reside in TargetOptions.
    resetTargetOptions(F);
    I = std::make_unique<HexagonSubtarget>(TargetTriple, CPU, FS, *this);
  }
  return I.get();
}

void HexagonTargetMachine::registerPassBuilderCallbacks(PassBuilder &PB) {
#define GET_PASS_REGISTRY "HexagonPassRegistry.def"
#include "llvm/Passes/TargetPassRegistry.inc"

  PB.registerLateLoopOptimizationsEPCallback(
      [=](LoopPassManager &LPM, OptimizationLevel Level) {
        LPM.addPass(HexagonLoopIdiomRecognitionPass());
      });
  PB.registerLoopOptimizerEndEPCallback(
      [=](LoopPassManager &LPM, OptimizationLevel Level) {
        LPM.addPass(HexagonVectorLoopCarriedReusePass());
      });
}

TargetTransformInfo
HexagonTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(HexagonTTIImpl(this, F));
}

MachineFunctionInfo *HexagonTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return HexagonMachineFunctionInfo::create<HexagonMachineFunctionInfo>(
      Allocator, F, STI);
}

HexagonTargetMachine::~HexagonTargetMachine() = default;

namespace {
/// Hexagon Code Generator Pass Configuration Options.
class HexagonPassConfig : public TargetPassConfig {
public:
  HexagonPassConfig(HexagonTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  HexagonTargetMachine &getHexagonTargetMachine() const {
    return getTM<HexagonTargetMachine>();
  }

  ScheduleDAGInstrs *
  createMachineScheduler(MachineSchedContext *C) const override {
    return createVLIWMachineSched(C);
  }

  void addIRPasses() override;
  bool addInstSelector() override;
  void addPreRegAlloc() override;
  void addPostRegAlloc() override;
  void addPreSched2() override;
  void addPreEmitPass() override;
};
} // namespace

TargetPassConfig *HexagonTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new HexagonPassConfig(*this, PM);
}

void HexagonPassConfig::addIRPasses() {
  TargetPassConfig::addIRPasses();
  bool NoOpt = (getOptLevel() == CodeGenOptLevel::None);

  if (!NoOpt) {
    if (EnableInstSimplify)
      addPass(createInstSimplifyLegacyPass());
    addPass(createDeadCodeEliminationPass());
  }

  addPass(createAtomicExpandLegacyPass());

  if (!NoOpt) {
    if (EnableInitialCFGCleanup)
      addPass(createCFGSimplificationPass(SimplifyCFGOptions()
                                              .forwardSwitchCondToPhi(true)
                                              .convertSwitchRangeToICmp(true)
                                              .convertSwitchToLookupTable(true)
                                              .needCanonicalLoops(false)
                                              .hoistCommonInsts(true)
                                              .sinkCommonInsts(true)));
    if (EnableLoopPrefetch)
      addPass(createLoopDataPrefetchPass());
    if (EnableVectorCombine)
      addPass(createHexagonVectorCombineLegacyPass());
    if (EnableCommGEP)
      addPass(createHexagonCommonGEP());
    // Replace certain combinations of shifts and ands with extracts.
    if (EnableGenExtract)
      addPass(createHexagonGenExtract());
  }
}

bool HexagonPassConfig::addInstSelector() {
  HexagonTargetMachine &TM = getHexagonTargetMachine();
  bool NoOpt = (getOptLevel() == CodeGenOptLevel::None);

  if (!NoOpt)
    addPass(createHexagonOptimizeSZextends());

  addPass(createHexagonISelDag(TM, getOptLevel()));

  if (!NoOpt) {
    if (EnableVExtractOpt)
      addPass(createHexagonVExtract());
    // Create logical operations on predicate registers.
    if (EnableGenPred)
      addPass(createHexagonGenPredicate());
    // Rotate loops to expose bit-simplification opportunities.
    if (EnableLoopResched)
      addPass(createHexagonLoopRescheduling());
    // Split double registers.
    if (!DisableHSDR)
      addPass(createHexagonSplitDoubleRegs());
    // Bit simplification.
    if (EnableBitSimplify)
      addPass(createHexagonBitSimplify());
    addPass(createHexagonPeephole());
    // Constant propagation.
    if (!DisableHCP) {
      addPass(createHexagonConstPropagationPass());
      addPass(&UnreachableMachineBlockElimID);
    }
    if (EnableGenInsert)
      addPass(createHexagonGenInsert());
    if (EnableEarlyIf)
      addPass(createHexagonEarlyIfConversion());
  }

  return false;
}

void HexagonPassConfig::addPreRegAlloc() {
  if (getOptLevel() != CodeGenOptLevel::None) {
    if (EnableCExtOpt)
      addPass(createHexagonConstExtenders());
    if (EnableExpandCondsets)
      insertPass(&RegisterCoalescerID, &HexagonExpandCondsetsID);
    if (EnableCopyHoist)
      insertPass(&RegisterCoalescerID, &HexagonCopyHoistingID);
    if (EnableTfrCleanup)
      insertPass(&VirtRegRewriterID, &HexagonTfrCleanupID);
    if (!DisableStoreWidening)
      addPass(createHexagonStoreWidening());
    if (EnableGenMemAbs)
      addPass(createHexagonGenMemAbsolute());
    if (!DisableHardwareLoops)
      addPass(createHexagonHardwareLoops());
  }
  if (TM->getOptLevel() >= CodeGenOptLevel::Default)
    addPass(&MachinePipelinerID);
}

void HexagonPassConfig::addPostRegAlloc() {
  if (getOptLevel() != CodeGenOptLevel::None) {
    if (EnableRDFOpt)
      addPass(createHexagonRDFOpt());
    if (!DisableHexagonCFGOpt)
      addPass(createHexagonCFGOptimizer());
    if (!DisableAModeOpt)
      addPass(createHexagonOptAddrMode());
  }
}

void HexagonPassConfig::addPreSched2() {
  addPass(createHexagonCopyToCombine());
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(&IfConverterID);
  addPass(createHexagonSplitConst32AndConst64());
}

void HexagonPassConfig::addPreEmitPass() {
  bool NoOpt = (getOptLevel() == CodeGenOptLevel::None);

  if (!NoOpt)
    addPass(createHexagonNewValueJump());

  addPass(createHexagonBranchRelaxation());

  if (!NoOpt) {
    if (!DisableHardwareLoops)
      addPass(createHexagonFixupHwLoops());
    // Generate MUX from pairs of conditional transfers.
    if (EnableGenMux)
      addPass(createHexagonGenMux());
  }

  // Packetization is mandatory: it handles gather/scatter at all opt levels.
  addPass(createHexagonPacketizer(NoOpt));

  if (!NoOpt)
    addPass(createHexagonLoopAlign());

  if (EnableVectorPrint)
    addPass(createHexagonVectorPrint());

  // Add CFI instructions if necessary.
  addPass(createHexagonCallFrameInformation());
}
