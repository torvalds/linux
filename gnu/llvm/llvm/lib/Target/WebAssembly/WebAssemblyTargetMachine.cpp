//===- WebAssemblyTargetMachine.cpp - Define TargetMachine for WebAssembly -==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the WebAssembly-specific subclass of TargetMachine.
///
//===----------------------------------------------------------------------===//

#include "WebAssemblyTargetMachine.h"
#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "TargetInfo/WebAssemblyTargetInfo.h"
#include "WebAssembly.h"
#include "WebAssemblyISelLowering.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "WebAssemblyTargetObjectFile.h"
#include "WebAssemblyTargetTransformInfo.h"
#include "WebAssemblyUtilities.h"
#include "llvm/CodeGen/MIRParser/MIParser.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegAllocRegistry.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/Function.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/LowerAtomicPass.h"
#include "llvm/Transforms/Utils.h"
#include <optional>
using namespace llvm;

#define DEBUG_TYPE "wasm"

// A command-line option to keep implicit locals
// for the purpose of testing with lit/llc ONLY.
// This produces output which is not valid WebAssembly, and is not supported
// by assemblers/disassemblers and other MC based tools.
static cl::opt<bool> WasmDisableExplicitLocals(
    "wasm-disable-explicit-locals", cl::Hidden,
    cl::desc("WebAssembly: output implicit locals in"
             " instruction output for test purposes only."),
    cl::init(false));

static cl::opt<bool> WasmDisableFixIrreducibleControlFlowPass(
    "wasm-disable-fix-irreducible-control-flow-pass", cl::Hidden,
    cl::desc("webassembly: disables the fix "
             " irreducible control flow optimization pass"),
    cl::init(false));

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeWebAssemblyTarget() {
  // Register the target.
  RegisterTargetMachine<WebAssemblyTargetMachine> X(
      getTheWebAssemblyTarget32());
  RegisterTargetMachine<WebAssemblyTargetMachine> Y(
      getTheWebAssemblyTarget64());

  // Register backend passes
  auto &PR = *PassRegistry::getPassRegistry();
  initializeWebAssemblyAddMissingPrototypesPass(PR);
  initializeWebAssemblyLowerEmscriptenEHSjLjPass(PR);
  initializeLowerGlobalDtorsLegacyPassPass(PR);
  initializeFixFunctionBitcastsPass(PR);
  initializeOptimizeReturnedPass(PR);
  initializeWebAssemblyRefTypeMem2LocalPass(PR);
  initializeWebAssemblyArgumentMovePass(PR);
  initializeWebAssemblySetP2AlignOperandsPass(PR);
  initializeWebAssemblyReplacePhysRegsPass(PR);
  initializeWebAssemblyOptimizeLiveIntervalsPass(PR);
  initializeWebAssemblyMemIntrinsicResultsPass(PR);
  initializeWebAssemblyRegStackifyPass(PR);
  initializeWebAssemblyRegColoringPass(PR);
  initializeWebAssemblyNullifyDebugValueListsPass(PR);
  initializeWebAssemblyFixIrreducibleControlFlowPass(PR);
  initializeWebAssemblyLateEHPreparePass(PR);
  initializeWebAssemblyExceptionInfoPass(PR);
  initializeWebAssemblyCFGSortPass(PR);
  initializeWebAssemblyCFGStackifyPass(PR);
  initializeWebAssemblyExplicitLocalsPass(PR);
  initializeWebAssemblyLowerBrUnlessPass(PR);
  initializeWebAssemblyRegNumberingPass(PR);
  initializeWebAssemblyDebugFixupPass(PR);
  initializeWebAssemblyPeepholePass(PR);
  initializeWebAssemblyMCLowerPrePassPass(PR);
  initializeWebAssemblyLowerRefTypesIntPtrConvPass(PR);
  initializeWebAssemblyFixBrTableDefaultsPass(PR);
  initializeWebAssemblyDAGToDAGISelLegacyPass(PR);
}

//===----------------------------------------------------------------------===//
// WebAssembly Lowering public interface.
//===----------------------------------------------------------------------===//

static Reloc::Model getEffectiveRelocModel(std::optional<Reloc::Model> RM,
                                           const Triple &TT) {
  if (!RM) {
    // Default to static relocation model.  This should always be more optimial
    // than PIC since the static linker can determine all global addresses and
    // assume direct function calls.
    return Reloc::Static;
  }

  return *RM;
}

/// Create an WebAssembly architecture model.
///
WebAssemblyTargetMachine::WebAssemblyTargetMachine(
    const Target &T, const Triple &TT, StringRef CPU, StringRef FS,
    const TargetOptions &Options, std::optional<Reloc::Model> RM,
    std::optional<CodeModel::Model> CM, CodeGenOptLevel OL, bool JIT)
    : LLVMTargetMachine(
          T,
          TT.isArch64Bit()
              ? (TT.isOSEmscripten() ? "e-m:e-p:64:64-p10:8:8-p20:8:8-i64:64-"
                                       "f128:64-n32:64-S128-ni:1:10:20"
                                     : "e-m:e-p:64:64-p10:8:8-p20:8:8-i64:64-"
                                       "n32:64-S128-ni:1:10:20")
              : (TT.isOSEmscripten() ? "e-m:e-p:32:32-p10:8:8-p20:8:8-i64:64-"
                                       "f128:64-n32:64-S128-ni:1:10:20"
                                     : "e-m:e-p:32:32-p10:8:8-p20:8:8-i64:64-"
                                       "n32:64-S128-ni:1:10:20"),
          TT, CPU, FS, Options, getEffectiveRelocModel(RM, TT),
          getEffectiveCodeModel(CM, CodeModel::Large), OL),
      TLOF(new WebAssemblyTargetObjectFile()),
      UsesMultivalueABI(Options.MCOptions.getABIName() == "experimental-mv") {
  // WebAssembly type-checks instructions, but a noreturn function with a return
  // type that doesn't match the context will cause a check failure. So we lower
  // LLVM 'unreachable' to ISD::TRAP and then lower that to WebAssembly's
  // 'unreachable' instructions which is meant for that case.
  this->Options.TrapUnreachable = true;
  this->Options.NoTrapAfterNoreturn = false;

  // WebAssembly treats each function as an independent unit. Force
  // -ffunction-sections, effectively, so that we can emit them independently.
  this->Options.FunctionSections = true;
  this->Options.DataSections = true;
  this->Options.UniqueSectionNames = true;

  initAsmInfo();

  // Note that we don't use setRequiresStructuredCFG(true). It disables
  // optimizations than we're ok with, and want, such as critical edge
  // splitting and tail merging.
}

WebAssemblyTargetMachine::~WebAssemblyTargetMachine() = default; // anchor.

const WebAssemblySubtarget *WebAssemblyTargetMachine::getSubtargetImpl() const {
  return getSubtargetImpl(std::string(getTargetCPU()),
                          std::string(getTargetFeatureString()));
}

const WebAssemblySubtarget *
WebAssemblyTargetMachine::getSubtargetImpl(std::string CPU,
                                           std::string FS) const {
  auto &I = SubtargetMap[CPU + FS];
  if (!I) {
    I = std::make_unique<WebAssemblySubtarget>(TargetTriple, CPU, FS, *this);
  }
  return I.get();
}

const WebAssemblySubtarget *
WebAssemblyTargetMachine::getSubtargetImpl(const Function &F) const {
  Attribute CPUAttr = F.getFnAttribute("target-cpu");
  Attribute FSAttr = F.getFnAttribute("target-features");

  std::string CPU =
      CPUAttr.isValid() ? CPUAttr.getValueAsString().str() : TargetCPU;
  std::string FS =
      FSAttr.isValid() ? FSAttr.getValueAsString().str() : TargetFS;

  // This needs to be done before we create a new subtarget since any
  // creation will depend on the TM and the code generation flags on the
  // function that reside in TargetOptions.
  resetTargetOptions(F);

  return getSubtargetImpl(CPU, FS);
}

namespace {

class CoalesceFeaturesAndStripAtomics final : public ModulePass {
  // Take the union of all features used in the module and use it for each
  // function individually, since having multiple feature sets in one module
  // currently does not make sense for WebAssembly. If atomics are not enabled,
  // also strip atomic operations and thread local storage.
  static char ID;
  WebAssemblyTargetMachine *WasmTM;

public:
  CoalesceFeaturesAndStripAtomics(WebAssemblyTargetMachine *WasmTM)
      : ModulePass(ID), WasmTM(WasmTM) {}

  bool runOnModule(Module &M) override {
    FeatureBitset Features = coalesceFeatures(M);

    std::string FeatureStr = getFeatureString(Features);
    WasmTM->setTargetFeatureString(FeatureStr);
    for (auto &F : M)
      replaceFeatures(F, FeatureStr);

    bool StrippedAtomics = false;
    bool StrippedTLS = false;

    if (!Features[WebAssembly::FeatureAtomics]) {
      StrippedAtomics = stripAtomics(M);
      StrippedTLS = stripThreadLocals(M);
    } else if (!Features[WebAssembly::FeatureBulkMemory]) {
      StrippedTLS |= stripThreadLocals(M);
    }

    if (StrippedAtomics && !StrippedTLS)
      stripThreadLocals(M);
    else if (StrippedTLS && !StrippedAtomics)
      stripAtomics(M);

    recordFeatures(M, Features, StrippedAtomics || StrippedTLS);

    // Conservatively assume we have made some change
    return true;
  }

private:
  FeatureBitset coalesceFeatures(const Module &M) {
    FeatureBitset Features =
        WasmTM
            ->getSubtargetImpl(std::string(WasmTM->getTargetCPU()),
                               std::string(WasmTM->getTargetFeatureString()))
            ->getFeatureBits();
    for (auto &F : M)
      Features |= WasmTM->getSubtargetImpl(F)->getFeatureBits();
    return Features;
  }

  static std::string getFeatureString(const FeatureBitset &Features) {
    std::string Ret;
    for (const SubtargetFeatureKV &KV : WebAssemblyFeatureKV) {
      if (Features[KV.Value])
        Ret += (StringRef("+") + KV.Key + ",").str();
      else
        Ret += (StringRef("-") + KV.Key + ",").str();
    }
    return Ret;
  }

  void replaceFeatures(Function &F, const std::string &Features) {
    F.removeFnAttr("target-features");
    F.removeFnAttr("target-cpu");
    F.addFnAttr("target-features", Features);
  }

  bool stripAtomics(Module &M) {
    // Detect whether any atomics will be lowered, since there is no way to tell
    // whether the LowerAtomic pass lowers e.g. stores.
    bool Stripped = false;
    for (auto &F : M) {
      for (auto &B : F) {
        for (auto &I : B) {
          if (I.isAtomic()) {
            Stripped = true;
            goto done;
          }
        }
      }
    }

  done:
    if (!Stripped)
      return false;

    LowerAtomicPass Lowerer;
    FunctionAnalysisManager FAM;
    for (auto &F : M)
      Lowerer.run(F, FAM);

    return true;
  }

  bool stripThreadLocals(Module &M) {
    bool Stripped = false;
    for (auto &GV : M.globals()) {
      if (GV.isThreadLocal()) {
        // replace `@llvm.threadlocal.address.pX(GV)` with `GV`.
        for (Use &U : make_early_inc_range(GV.uses())) {
          if (IntrinsicInst *II = dyn_cast<IntrinsicInst>(U.getUser())) {
            if (II->getIntrinsicID() == Intrinsic::threadlocal_address &&
                II->getArgOperand(0) == &GV) {
              II->replaceAllUsesWith(&GV);
              II->eraseFromParent();
            }
          }
        }

        Stripped = true;
        GV.setThreadLocal(false);
      }
    }
    return Stripped;
  }

  void recordFeatures(Module &M, const FeatureBitset &Features, bool Stripped) {
    for (const SubtargetFeatureKV &KV : WebAssemblyFeatureKV) {
      if (Features[KV.Value]) {
        // Mark features as used
        std::string MDKey = (StringRef("wasm-feature-") + KV.Key).str();
        M.addModuleFlag(Module::ModFlagBehavior::Error, MDKey,
                        wasm::WASM_FEATURE_PREFIX_USED);
      }
    }
    // Code compiled without atomics or bulk-memory may have had its atomics or
    // thread-local data lowered to nonatomic operations or non-thread-local
    // data. In that case, we mark the pseudo-feature "shared-mem" as disallowed
    // to tell the linker that it would be unsafe to allow this code ot be used
    // in a module with shared memory.
    if (Stripped) {
      M.addModuleFlag(Module::ModFlagBehavior::Error, "wasm-feature-shared-mem",
                      wasm::WASM_FEATURE_PREFIX_DISALLOWED);
    }
  }
};
char CoalesceFeaturesAndStripAtomics::ID = 0;

/// WebAssembly Code Generator Pass Configuration Options.
class WebAssemblyPassConfig final : public TargetPassConfig {
public:
  WebAssemblyPassConfig(WebAssemblyTargetMachine &TM, PassManagerBase &PM)
      : TargetPassConfig(TM, PM) {}

  WebAssemblyTargetMachine &getWebAssemblyTargetMachine() const {
    return getTM<WebAssemblyTargetMachine>();
  }

  FunctionPass *createTargetRegisterAllocator(bool) override;

  void addIRPasses() override;
  void addISelPrepare() override;
  bool addInstSelector() override;
  void addOptimizedRegAlloc() override;
  void addPostRegAlloc() override;
  bool addGCPasses() override { return false; }
  void addPreEmitPass() override;
  bool addPreISel() override;

  // No reg alloc
  bool addRegAssignAndRewriteFast() override { return false; }

  // No reg alloc
  bool addRegAssignAndRewriteOptimized() override { return false; }
};
} // end anonymous namespace

MachineFunctionInfo *WebAssemblyTargetMachine::createMachineFunctionInfo(
    BumpPtrAllocator &Allocator, const Function &F,
    const TargetSubtargetInfo *STI) const {
  return WebAssemblyFunctionInfo::create<WebAssemblyFunctionInfo>(Allocator, F,
                                                                  STI);
}

TargetTransformInfo
WebAssemblyTargetMachine::getTargetTransformInfo(const Function &F) const {
  return TargetTransformInfo(WebAssemblyTTIImpl(this, F));
}

TargetPassConfig *
WebAssemblyTargetMachine::createPassConfig(PassManagerBase &PM) {
  return new WebAssemblyPassConfig(*this, PM);
}

FunctionPass *WebAssemblyPassConfig::createTargetRegisterAllocator(bool) {
  return nullptr; // No reg alloc
}

using WebAssembly::WasmEnableEH;
using WebAssembly::WasmEnableEmEH;
using WebAssembly::WasmEnableEmSjLj;
using WebAssembly::WasmEnableExnref;
using WebAssembly::WasmEnableSjLj;

static void basicCheckForEHAndSjLj(TargetMachine *TM) {

  // You can't enable two modes of EH at the same time
  if (WasmEnableEmEH && WasmEnableEH)
    report_fatal_error(
        "-enable-emscripten-cxx-exceptions not allowed with -wasm-enable-eh");
  // You can't enable two modes of SjLj at the same time
  if (WasmEnableEmSjLj && WasmEnableSjLj)
    report_fatal_error(
        "-enable-emscripten-sjlj not allowed with -wasm-enable-sjlj");
  // You can't mix Emscripten EH with Wasm SjLj.
  if (WasmEnableEmEH && WasmEnableSjLj)
    report_fatal_error(
        "-enable-emscripten-cxx-exceptions not allowed with -wasm-enable-sjlj");
  if (WasmEnableExnref && !WasmEnableEH)
    report_fatal_error(
        "-wasm-enable-exnref should be used with -wasm-enable-eh");

  // Here we make sure TargetOptions.ExceptionModel is the same as
  // MCAsmInfo.ExceptionsType. Normally these have to be the same, because clang
  // stores the exception model info in LangOptions, which is later transferred
  // to TargetOptions and MCAsmInfo. But when clang compiles bitcode directly,
  // clang's LangOptions is not used and thus the exception model info is not
  // correctly transferred to TargetOptions and MCAsmInfo, so we make sure we
  // have the correct exception model in WebAssemblyMCAsmInfo constructor. But
  // in this case TargetOptions is still not updated, so we make sure they are
  // the same.
  TM->Options.ExceptionModel = TM->getMCAsmInfo()->getExceptionHandlingType();

  // Basic Correctness checking related to -exception-model
  if (TM->Options.ExceptionModel != ExceptionHandling::None &&
      TM->Options.ExceptionModel != ExceptionHandling::Wasm)
    report_fatal_error("-exception-model should be either 'none' or 'wasm'");
  if (WasmEnableEmEH && TM->Options.ExceptionModel == ExceptionHandling::Wasm)
    report_fatal_error("-exception-model=wasm not allowed with "
                       "-enable-emscripten-cxx-exceptions");
  if (WasmEnableEH && TM->Options.ExceptionModel != ExceptionHandling::Wasm)
    report_fatal_error(
        "-wasm-enable-eh only allowed with -exception-model=wasm");
  if (WasmEnableSjLj && TM->Options.ExceptionModel != ExceptionHandling::Wasm)
    report_fatal_error(
        "-wasm-enable-sjlj only allowed with -exception-model=wasm");
  if ((!WasmEnableEH && !WasmEnableSjLj) &&
      TM->Options.ExceptionModel == ExceptionHandling::Wasm)
    report_fatal_error(
        "-exception-model=wasm only allowed with at least one of "
        "-wasm-enable-eh or -wasm-enable-sjlj");

  // Currently it is allowed to mix Wasm EH with Emscripten SjLj as an interim
  // measure, but some code will error out at compile time in this combination.
  // See WebAssemblyLowerEmscriptenEHSjLj pass for details.
}

//===----------------------------------------------------------------------===//
// The following functions are called from lib/CodeGen/Passes.cpp to modify
// the CodeGen pass sequence.
//===----------------------------------------------------------------------===//

void WebAssemblyPassConfig::addIRPasses() {
  // Add signatures to prototype-less function declarations
  addPass(createWebAssemblyAddMissingPrototypes());

  // Lower .llvm.global_dtors into .llvm.global_ctors with __cxa_atexit calls.
  addPass(createLowerGlobalDtorsLegacyPass());

  // Fix function bitcasts, as WebAssembly requires caller and callee signatures
  // to match.
  addPass(createWebAssemblyFixFunctionBitcasts());

  // Optimize "returned" function attributes.
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createWebAssemblyOptimizeReturned());

  basicCheckForEHAndSjLj(TM);

  // If exception handling is not enabled and setjmp/longjmp handling is
  // enabled, we lower invokes into calls and delete unreachable landingpad
  // blocks. Lowering invokes when there is no EH support is done in
  // TargetPassConfig::addPassesToHandleExceptions, but that runs after these IR
  // passes and Emscripten SjLj handling expects all invokes to be lowered
  // before.
  if (!WasmEnableEmEH && !WasmEnableEH) {
    addPass(createLowerInvokePass());
    // The lower invoke pass may create unreachable code. Remove it in order not
    // to process dead blocks in setjmp/longjmp handling.
    addPass(createUnreachableBlockEliminationPass());
  }

  // Handle exceptions and setjmp/longjmp if enabled. Unlike Wasm EH preparation
  // done in WasmEHPrepare pass, Wasm SjLj preparation shares libraries and
  // transformation algorithms with Emscripten SjLj, so we run
  // LowerEmscriptenEHSjLj pass also when Wasm SjLj is enabled.
  if (WasmEnableEmEH || WasmEnableEmSjLj || WasmEnableSjLj)
    addPass(createWebAssemblyLowerEmscriptenEHSjLj());

  // Expand indirectbr instructions to switches.
  addPass(createIndirectBrExpandPass());

  TargetPassConfig::addIRPasses();
}

void WebAssemblyPassConfig::addISelPrepare() {
  // We need to move reference type allocas to WASM_ADDRESS_SPACE_VAR so that
  // loads and stores are promoted to local.gets/local.sets.
  addPass(createWebAssemblyRefTypeMem2Local());
  // Lower atomics and TLS if necessary
  addPass(new CoalesceFeaturesAndStripAtomics(&getWebAssemblyTargetMachine()));

  // This is a no-op if atomics are not used in the module
  addPass(createAtomicExpandLegacyPass());

  TargetPassConfig::addISelPrepare();
}

bool WebAssemblyPassConfig::addInstSelector() {
  (void)TargetPassConfig::addInstSelector();
  addPass(
      createWebAssemblyISelDag(getWebAssemblyTargetMachine(), getOptLevel()));
  // Run the argument-move pass immediately after the ScheduleDAG scheduler
  // so that we can fix up the ARGUMENT instructions before anything else
  // sees them in the wrong place.
  addPass(createWebAssemblyArgumentMove());
  // Set the p2align operands. This information is present during ISel, however
  // it's inconvenient to collect. Collect it now, and update the immediate
  // operands.
  addPass(createWebAssemblySetP2AlignOperands());

  // Eliminate range checks and add default targets to br_table instructions.
  addPass(createWebAssemblyFixBrTableDefaults());

  // unreachable is terminator, non-terminator instruction after it is not
  // allowed.
  addPass(createWebAssemblyCleanCodeAfterTrap());

  return false;
}

void WebAssemblyPassConfig::addOptimizedRegAlloc() {
  // Currently RegisterCoalesce degrades wasm debug info quality by a
  // significant margin. As a quick fix, disable this for -O1, which is often
  // used for debugging large applications. Disabling this increases code size
  // of Emscripten core benchmarks by ~5%, which is acceptable for -O1, which is
  // usually not used for production builds.
  // TODO Investigate why RegisterCoalesce degrades debug info quality and fix
  // it properly
  if (getOptLevel() == CodeGenOptLevel::Less)
    disablePass(&RegisterCoalescerID);
  TargetPassConfig::addOptimizedRegAlloc();
}

void WebAssemblyPassConfig::addPostRegAlloc() {
  // TODO: The following CodeGen passes don't currently support code containing
  // virtual registers. Consider removing their restrictions and re-enabling
  // them.

  // These functions all require the NoVRegs property.
  disablePass(&MachineLateInstrsCleanupID);
  disablePass(&MachineCopyPropagationID);
  disablePass(&PostRAMachineSinkingID);
  disablePass(&PostRASchedulerID);
  disablePass(&FuncletLayoutID);
  disablePass(&StackMapLivenessID);
  disablePass(&PatchableFunctionID);
  disablePass(&ShrinkWrapID);

  // This pass hurts code size for wasm because it can generate irreducible
  // control flow.
  disablePass(&MachineBlockPlacementID);

  TargetPassConfig::addPostRegAlloc();
}

void WebAssemblyPassConfig::addPreEmitPass() {
  TargetPassConfig::addPreEmitPass();

  // Nullify DBG_VALUE_LISTs that we cannot handle.
  addPass(createWebAssemblyNullifyDebugValueLists());

  // Eliminate multiple-entry loops.
  if (!WasmDisableFixIrreducibleControlFlowPass)
    addPass(createWebAssemblyFixIrreducibleControlFlow());

  // Do various transformations for exception handling.
  // Every CFG-changing optimizations should come before this.
  if (TM->Options.ExceptionModel == ExceptionHandling::Wasm)
    addPass(createWebAssemblyLateEHPrepare());

  // Now that we have a prologue and epilogue and all frame indices are
  // rewritten, eliminate SP and FP. This allows them to be stackified,
  // colored, and numbered with the rest of the registers.
  addPass(createWebAssemblyReplacePhysRegs());

  // Preparations and optimizations related to register stackification.
  if (getOptLevel() != CodeGenOptLevel::None) {
    // Depend on LiveIntervals and perform some optimizations on it.
    addPass(createWebAssemblyOptimizeLiveIntervals());

    // Prepare memory intrinsic calls for register stackifying.
    addPass(createWebAssemblyMemIntrinsicResults());

    // Mark registers as representing wasm's value stack. This is a key
    // code-compression technique in WebAssembly. We run this pass (and
    // MemIntrinsicResults above) very late, so that it sees as much code as
    // possible, including code emitted by PEI and expanded by late tail
    // duplication.
    addPass(createWebAssemblyRegStackify());

    // Run the register coloring pass to reduce the total number of registers.
    // This runs after stackification so that it doesn't consider registers
    // that become stackified.
    addPass(createWebAssemblyRegColoring());
  }

  // Sort the blocks of the CFG into topological order, a prerequisite for
  // BLOCK and LOOP markers.
  addPass(createWebAssemblyCFGSort());

  // Insert BLOCK and LOOP markers.
  addPass(createWebAssemblyCFGStackify());

  // Insert explicit local.get and local.set operators.
  if (!WasmDisableExplicitLocals)
    addPass(createWebAssemblyExplicitLocals());

  // Lower br_unless into br_if.
  addPass(createWebAssemblyLowerBrUnless());

  // Perform the very last peephole optimizations on the code.
  if (getOptLevel() != CodeGenOptLevel::None)
    addPass(createWebAssemblyPeephole());

  // Create a mapping from LLVM CodeGen virtual registers to wasm registers.
  addPass(createWebAssemblyRegNumbering());

  // Fix debug_values whose defs have been stackified.
  if (!WasmDisableExplicitLocals)
    addPass(createWebAssemblyDebugFixup());

  // Collect information to prepare for MC lowering / asm printing.
  addPass(createWebAssemblyMCLowerPrePass());
}

bool WebAssemblyPassConfig::addPreISel() {
  TargetPassConfig::addPreISel();
  addPass(createWebAssemblyLowerRefTypesIntPtrConv());
  return false;
}

yaml::MachineFunctionInfo *
WebAssemblyTargetMachine::createDefaultFuncInfoYAML() const {
  return new yaml::WebAssemblyFunctionInfo();
}

yaml::MachineFunctionInfo *WebAssemblyTargetMachine::convertFuncInfoToYAML(
    const MachineFunction &MF) const {
  const auto *MFI = MF.getInfo<WebAssemblyFunctionInfo>();
  return new yaml::WebAssemblyFunctionInfo(MF, *MFI);
}

bool WebAssemblyTargetMachine::parseMachineFunctionInfo(
    const yaml::MachineFunctionInfo &MFI, PerFunctionMIParsingState &PFS,
    SMDiagnostic &Error, SMRange &SourceRange) const {
  const auto &YamlMFI = static_cast<const yaml::WebAssemblyFunctionInfo &>(MFI);
  MachineFunction &MF = PFS.MF;
  MF.getInfo<WebAssemblyFunctionInfo>()->initializeBaseYamlFields(MF, YamlMFI);
  return false;
}
