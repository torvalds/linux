//===- TargetPassConfig.h - Code Generation pass options --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Target-Independent Code Generator Pass Configuration Options pass.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TARGETPASSCONFIG_H
#define LLVM_CODEGEN_TARGETPASSCONFIG_H

#include "llvm/Pass.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Error.h"
#include <cassert>
#include <string>

namespace llvm {

class LLVMTargetMachine;
struct MachineSchedContext;
class PassConfigImpl;
class ScheduleDAGInstrs;
class CSEConfigBase;
class PassInstrumentationCallbacks;

// The old pass manager infrastructure is hidden in a legacy namespace now.
namespace legacy {

class PassManagerBase;

} // end namespace legacy

using legacy::PassManagerBase;

/// Discriminated union of Pass ID types.
///
/// The PassConfig API prefers dealing with IDs because they are safer and more
/// efficient. IDs decouple configuration from instantiation. This way, when a
/// pass is overriden, it isn't unnecessarily instantiated. It is also unsafe to
/// refer to a Pass pointer after adding it to a pass manager, which deletes
/// redundant pass instances.
///
/// However, it is convient to directly instantiate target passes with
/// non-default ctors. These often don't have a registered PassInfo. Rather than
/// force all target passes to implement the pass registry boilerplate, allow
/// the PassConfig API to handle either type.
///
/// AnalysisID is sadly char*, so PointerIntPair won't work.
class IdentifyingPassPtr {
  union {
    AnalysisID ID;
    Pass *P;
  };
  bool IsInstance = false;

public:
  IdentifyingPassPtr() : P(nullptr) {}
  IdentifyingPassPtr(AnalysisID IDPtr) : ID(IDPtr) {}
  IdentifyingPassPtr(Pass *InstancePtr) : P(InstancePtr), IsInstance(true) {}

  bool isValid() const { return P; }
  bool isInstance() const { return IsInstance; }

  AnalysisID getID() const {
    assert(!IsInstance && "Not a Pass ID");
    return ID;
  }

  Pass *getInstance() const {
    assert(IsInstance && "Not a Pass Instance");
    return P;
  }
};


/// Target-Independent Code Generator Pass Configuration Options.
///
/// This is an ImmutablePass solely for the purpose of exposing CodeGen options
/// to the internals of other CodeGen passes.
class TargetPassConfig : public ImmutablePass {
private:
  PassManagerBase *PM = nullptr;
  AnalysisID StartBefore = nullptr;
  AnalysisID StartAfter = nullptr;
  AnalysisID StopBefore = nullptr;
  AnalysisID StopAfter = nullptr;

  unsigned StartBeforeInstanceNum = 0;
  unsigned StartBeforeCount = 0;

  unsigned StartAfterInstanceNum = 0;
  unsigned StartAfterCount = 0;

  unsigned StopBeforeInstanceNum = 0;
  unsigned StopBeforeCount = 0;

  unsigned StopAfterInstanceNum = 0;
  unsigned StopAfterCount = 0;

  bool Started = true;
  bool Stopped = false;
  bool AddingMachinePasses = false;
  bool DebugifyIsSafe = true;

  /// Set the StartAfter, StartBefore and StopAfter passes to allow running only
  /// a portion of the normal code-gen pass sequence.
  ///
  /// If the StartAfter and StartBefore pass ID is zero, then compilation will
  /// begin at the normal point; otherwise, clear the Started flag to indicate
  /// that passes should not be added until the starting pass is seen.  If the
  /// Stop pass ID is zero, then compilation will continue to the end.
  ///
  /// This function expects that at least one of the StartAfter or the
  /// StartBefore pass IDs is null.
  void setStartStopPasses();

protected:
  LLVMTargetMachine *TM;
  PassConfigImpl *Impl = nullptr; // Internal data structures
  bool Initialized = false; // Flagged after all passes are configured.

  // Target Pass Options
  // Targets provide a default setting, user flags override.
  bool DisableVerify = false;

  /// Default setting for -enable-tail-merge on this target.
  bool EnableTailMerge = true;

  /// Enable sinking of instructions in MachineSink where a computation can be
  /// folded into the addressing mode of a memory load/store instruction or
  /// replace a copy.
  bool EnableSinkAndFold = false;

  /// Require processing of functions such that callees are generated before
  /// callers.
  bool RequireCodeGenSCCOrder = false;

  /// Add the actual instruction selection passes. This does not include
  /// preparation passes on IR.
  bool addCoreISelPasses();

public:
  TargetPassConfig(LLVMTargetMachine &TM, PassManagerBase &pm);
  // Dummy constructor.
  TargetPassConfig();

  ~TargetPassConfig() override;

  static char ID;

  /// Get the right type of TargetMachine for this target.
  template<typename TMC> TMC &getTM() const {
    return *static_cast<TMC*>(TM);
  }

  //
  void setInitialized() { Initialized = true; }

  CodeGenOptLevel getOptLevel() const;

  /// Returns true if one of the `-start-after`, `-start-before`, `-stop-after`
  /// or `-stop-before` options is set.
  static bool hasLimitedCodeGenPipeline();

  /// Returns true if none of the `-stop-before` and `-stop-after` options is
  /// set.
  static bool willCompleteCodeGenPipeline();

  /// If hasLimitedCodeGenPipeline is true, this method returns
  /// a string with the name of the options that caused this
  /// pipeline to be limited.
  static std::string getLimitedCodeGenPipelineReason();

  struct StartStopInfo {
    bool StartAfter;
    bool StopAfter;
    unsigned StartInstanceNum;
    unsigned StopInstanceNum;
    StringRef StartPass;
    StringRef StopPass;
  };

  /// Returns pass name in `-stop-before` or `-stop-after`
  /// NOTE: New pass manager migration only
  static Expected<StartStopInfo>
  getStartStopInfo(PassInstrumentationCallbacks &PIC);

  void setDisableVerify(bool Disable) { setOpt(DisableVerify, Disable); }

  bool getEnableTailMerge() const { return EnableTailMerge; }
  void setEnableTailMerge(bool Enable) { setOpt(EnableTailMerge, Enable); }

  bool getEnableSinkAndFold() const { return EnableSinkAndFold; }
  void setEnableSinkAndFold(bool Enable) { setOpt(EnableSinkAndFold, Enable); }

  bool requiresCodeGenSCCOrder() const { return RequireCodeGenSCCOrder; }
  void setRequiresCodeGenSCCOrder(bool Enable = true) {
    setOpt(RequireCodeGenSCCOrder, Enable);
  }

  /// Allow the target to override a specific pass without overriding the pass
  /// pipeline. When passes are added to the standard pipeline at the
  /// point where StandardID is expected, add TargetID in its place.
  void substitutePass(AnalysisID StandardID, IdentifyingPassPtr TargetID);

  /// Insert InsertedPassID pass after TargetPassID pass.
  void insertPass(AnalysisID TargetPassID, IdentifyingPassPtr InsertedPassID);

  /// Allow the target to enable a specific standard pass by default.
  void enablePass(AnalysisID PassID) { substitutePass(PassID, PassID); }

  /// Allow the target to disable a specific standard pass by default.
  void disablePass(AnalysisID PassID) {
    substitutePass(PassID, IdentifyingPassPtr());
  }

  /// Return the pass substituted for StandardID by the target.
  /// If no substitution exists, return StandardID.
  IdentifyingPassPtr getPassSubstitution(AnalysisID StandardID) const;

  /// Return true if the pass has been substituted by the target or
  /// overridden on the command line.
  bool isPassSubstitutedOrOverridden(AnalysisID ID) const;

  /// Return true if the optimized regalloc pipeline is enabled.
  bool getOptimizeRegAlloc() const;

  /// Return true if the default global register allocator is in use and
  /// has not be overriden on the command line with '-regalloc=...'
  bool usingDefaultRegAlloc() const;

  /// High level function that adds all passes necessary to go from llvm IR
  /// representation to the MI representation.
  /// Adds IR based lowering and target specific optimization passes and finally
  /// the core instruction selection passes.
  /// \returns true if an error occurred, false otherwise.
  bool addISelPasses();

  /// Add common target configurable passes that perform LLVM IR to IR
  /// transforms following machine independent optimization.
  virtual void addIRPasses();

  /// Add passes to lower exception handling for the code generator.
  void addPassesToHandleExceptions();

  /// Add pass to prepare the LLVM IR for code generation. This should be done
  /// before exception handling preparation passes.
  virtual void addCodeGenPrepare();

  /// Add common passes that perform LLVM IR to IR transforms in preparation for
  /// instruction selection.
  virtual void addISelPrepare();

  /// addInstSelector - This method should install an instruction selector pass,
  /// which converts from LLVM code to machine instructions.
  virtual bool addInstSelector() {
    return true;
  }

  /// This method should install an IR translator pass, which converts from
  /// LLVM code to machine instructions with possibly generic opcodes.
  virtual bool addIRTranslator() { return true; }

  /// This method may be implemented by targets that want to run passes
  /// immediately before legalization.
  virtual void addPreLegalizeMachineIR() {}

  /// This method should install a legalize pass, which converts the instruction
  /// sequence into one that can be selected by the target.
  virtual bool addLegalizeMachineIR() { return true; }

  /// This method may be implemented by targets that want to run passes
  /// immediately before the register bank selection.
  virtual void addPreRegBankSelect() {}

  /// This method should install a register bank selector pass, which
  /// assigns register banks to virtual registers without a register
  /// class or register banks.
  virtual bool addRegBankSelect() { return true; }

  /// This method may be implemented by targets that want to run passes
  /// immediately before the (global) instruction selection.
  virtual void addPreGlobalInstructionSelect() {}

  /// This method should install a (global) instruction selector pass, which
  /// converts possibly generic instructions to fully target-specific
  /// instructions, thereby constraining all generic virtual registers to
  /// register classes.
  virtual bool addGlobalInstructionSelect() { return true; }

  /// Add the complete, standard set of LLVM CodeGen passes.
  /// Fully developed targets will not generally override this.
  virtual void addMachinePasses();

  /// Create an instance of ScheduleDAGInstrs to be run within the standard
  /// MachineScheduler pass for this function and target at the current
  /// optimization level.
  ///
  /// This can also be used to plug a new MachineSchedStrategy into an instance
  /// of the standard ScheduleDAGMI:
  ///   return new ScheduleDAGMI(C, std::make_unique<MyStrategy>(C), /*RemoveKillFlags=*/false)
  ///
  /// Return NULL to select the default (generic) machine scheduler.
  virtual ScheduleDAGInstrs *
  createMachineScheduler(MachineSchedContext *C) const {
    return nullptr;
  }

  /// Similar to createMachineScheduler but used when postRA machine scheduling
  /// is enabled.
  virtual ScheduleDAGInstrs *
  createPostMachineScheduler(MachineSchedContext *C) const {
    return nullptr;
  }

  /// printAndVerify - Add a pass to dump then verify the machine function, if
  /// those steps are enabled.
  void printAndVerify(const std::string &Banner);

  /// Add a pass to print the machine function if printing is enabled.
  void addPrintPass(const std::string &Banner);

  /// Add a pass to perform basic verification of the machine function if
  /// verification is enabled.
  void addVerifyPass(const std::string &Banner);

  /// Add a pass to add synthesized debug info to the MIR.
  void addDebugifyPass();

  /// Add a pass to remove debug info from the MIR.
  void addStripDebugPass();

  /// Add a pass to check synthesized debug info for MIR.
  void addCheckDebugPass();

  /// Add standard passes before a pass that's about to be added. For example,
  /// the DebugifyMachineModulePass if it is enabled.
  void addMachinePrePasses(bool AllowDebugify = true);

  /// Add standard passes after a pass that has just been added. For example,
  /// the MachineVerifier if it is enabled.
  void addMachinePostPasses(const std::string &Banner);

  /// Check whether or not GlobalISel should abort on error.
  /// When this is disabled, GlobalISel will fall back on SDISel instead of
  /// erroring out.
  bool isGlobalISelAbortEnabled() const;

  /// Check whether or not a diagnostic should be emitted when GlobalISel
  /// uses the fallback path. In other words, it will emit a diagnostic
  /// when GlobalISel failed and isGlobalISelAbortEnabled is false.
  virtual bool reportDiagnosticWhenGlobalISelFallback() const;

  /// Check whether continuous CSE should be enabled in GISel passes.
  /// By default, it's enabled for non O0 levels.
  virtual bool isGISelCSEEnabled() const;

  /// Returns the CSEConfig object to use for the current optimization level.
  virtual std::unique_ptr<CSEConfigBase> getCSEConfig() const;

protected:
  // Helper to verify the analysis is really immutable.
  void setOpt(bool &Opt, bool Val);

  /// Return true if register allocator is specified by -regalloc=override.
  bool isCustomizedRegAlloc();

  /// Methods with trivial inline returns are convenient points in the common
  /// codegen pass pipeline where targets may insert passes. Methods with
  /// out-of-line standard implementations are major CodeGen stages called by
  /// addMachinePasses. Some targets may override major stages when inserting
  /// passes is insufficient, but maintaining overriden stages is more work.
  ///

  /// addPreISelPasses - This method should add any "last minute" LLVM->LLVM
  /// passes (which are run just before instruction selector).
  virtual bool addPreISel() {
    return true;
  }

  /// addMachineSSAOptimization - Add standard passes that optimize machine
  /// instructions in SSA form.
  virtual void addMachineSSAOptimization();

  /// Add passes that optimize instruction level parallelism for out-of-order
  /// targets. These passes are run while the machine code is still in SSA
  /// form, so they can use MachineTraceMetrics to control their heuristics.
  ///
  /// All passes added here should preserve the MachineDominatorTree,
  /// MachineLoopInfo, and MachineTraceMetrics analyses.
  virtual bool addILPOpts() {
    return false;
  }

  /// This method may be implemented by targets that want to run passes
  /// immediately before register allocation.
  virtual void addPreRegAlloc() { }

  /// createTargetRegisterAllocator - Create the register allocator pass for
  /// this target at the current optimization level.
  virtual FunctionPass *createTargetRegisterAllocator(bool Optimized);

  /// addFastRegAlloc - Add the minimum set of target-independent passes that
  /// are required for fast register allocation.
  virtual void addFastRegAlloc();

  /// addOptimizedRegAlloc - Add passes related to register allocation.
  /// LLVMTargetMachine provides standard regalloc passes for most targets.
  virtual void addOptimizedRegAlloc();

  /// addPreRewrite - Add passes to the optimized register allocation pipeline
  /// after register allocation is complete, but before virtual registers are
  /// rewritten to physical registers.
  ///
  /// These passes must preserve VirtRegMap and LiveIntervals, and when running
  /// after RABasic or RAGreedy, they should take advantage of LiveRegMatrix.
  /// When these passes run, VirtRegMap contains legal physreg assignments for
  /// all virtual registers.
  ///
  /// Note if the target overloads addRegAssignAndRewriteOptimized, this may not
  /// be honored. This is also not generally used for the fast variant,
  /// where the allocation and rewriting are done in one pass.
  virtual bool addPreRewrite() {
    return false;
  }

  /// addPostFastRegAllocRewrite - Add passes to the optimized register
  /// allocation pipeline after fast register allocation is complete.
  virtual bool addPostFastRegAllocRewrite() { return false; }

  /// Add passes to be run immediately after virtual registers are rewritten
  /// to physical registers.
  virtual void addPostRewrite() { }

  /// This method may be implemented by targets that want to run passes after
  /// register allocation pass pipeline but before prolog-epilog insertion.
  virtual void addPostRegAlloc() { }

  /// Add passes that optimize machine instructions after register allocation.
  virtual void addMachineLateOptimization();

  /// This method may be implemented by targets that want to run passes after
  /// prolog-epilog insertion and before the second instruction scheduling pass.
  virtual void addPreSched2() { }

  /// addGCPasses - Add late codegen passes that analyze code for garbage
  /// collection. This should return true if GC info should be printed after
  /// these passes.
  virtual bool addGCPasses();

  /// Add standard basic block placement passes.
  virtual void addBlockPlacement();

  /// This pass may be implemented by targets that want to run passes
  /// immediately before machine code is emitted.
  virtual void addPreEmitPass() { }

  /// This pass may be implemented by targets that want to run passes
  /// immediately after basic block sections are assigned.
  virtual void addPostBBSections() {}

  /// Targets may add passes immediately before machine code is emitted in this
  /// callback. This is called even later than `addPreEmitPass`.
  // FIXME: Rename `addPreEmitPass` to something more sensible given its actual
  // position and remove the `2` suffix here as this callback is what
  // `addPreEmitPass` *should* be but in reality isn't.
  virtual void addPreEmitPass2() {}

  /// Utilities for targets to add passes to the pass manager.
  ///

  /// Add a CodeGen pass at this point in the pipeline after checking overrides.
  /// Return the pass that was added, or zero if no pass was added.
  AnalysisID addPass(AnalysisID PassID);

  /// Add a pass to the PassManager if that pass is supposed to be run, as
  /// determined by the StartAfter and StopAfter options. Takes ownership of the
  /// pass.
  void addPass(Pass *P);

  /// addMachinePasses helper to create the target-selected or overriden
  /// regalloc pass.
  virtual FunctionPass *createRegAllocPass(bool Optimized);

  /// Add core register allocator passes which do the actual register assignment
  /// and rewriting. \returns true if any passes were added.
  virtual bool addRegAssignAndRewriteFast();
  virtual bool addRegAssignAndRewriteOptimized();
};

void registerCodeGenCallback(PassInstrumentationCallbacks &PIC,
                             LLVMTargetMachine &);

} // end namespace llvm

#endif // LLVM_CODEGEN_TARGETPASSCONFIG_H
