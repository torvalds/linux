//===- InlineAdvisor.h - Inlining decision making abstraction -*- C++ ---*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
#ifndef LLVM_ANALYSIS_INLINEADVISOR_H
#define LLVM_ANALYSIS_INLINEADVISOR_H

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/PassManager.h"
#include <memory>

namespace llvm {
class BasicBlock;
class CallBase;
class Function;
class Module;
class OptimizationRemark;
class ImportedFunctionsInliningStatistics;
class OptimizationRemarkEmitter;
struct ReplayInlinerSettings;

/// There are 4 scenarios we can use the InlineAdvisor:
/// - Default - use manual heuristics.
///
/// - Release mode, the expected mode for production, day to day deployments.
/// In this mode, when building the compiler, we also compile a pre-trained ML
/// model to native code, and link it as a static library. This mode has low
/// overhead and no additional dependencies for the compiler runtime.
///
/// - Development mode, for training new models.
/// In this mode, we trade off runtime performance for flexibility. This mode
/// requires the TFLite library, and evaluates models dynamically. This mode
/// also permits generating training logs, for offline training.
///
/// - Dynamically load an advisor via a plugin (PluginInlineAdvisorAnalysis)
enum class InliningAdvisorMode : int { Default, Release, Development };

// Each entry represents an inline driver.
enum class InlinePass : int {
  AlwaysInliner,
  CGSCCInliner,
  EarlyInliner,
  ModuleInliner,
  MLInliner,
  ReplayCGSCCInliner,
  ReplaySampleProfileInliner,
  SampleProfileInliner,
};

/// Provides context on when an inline advisor is constructed in the pipeline
/// (e.g., link phase, inline driver).
struct InlineContext {
  ThinOrFullLTOPhase LTOPhase;

  InlinePass Pass;
};

std::string AnnotateInlinePassName(InlineContext IC);

class InlineAdvisor;
/// Capture state between an inlining decision having had been made, and
/// its impact being observable. When collecting model training data, this
/// allows recording features/decisions/partial reward data sets.
///
/// Derivations of this type are expected to be tightly coupled with their
/// InliningAdvisors. The base type implements the minimal contractual
/// obligations.
class InlineAdvice {
public:
  InlineAdvice(InlineAdvisor *Advisor, CallBase &CB,
               OptimizationRemarkEmitter &ORE, bool IsInliningRecommended);

  InlineAdvice(InlineAdvice &&) = delete;
  InlineAdvice(const InlineAdvice &) = delete;
  virtual ~InlineAdvice() {
    assert(Recorded && "InlineAdvice should have been informed of the "
                       "inliner's decision in all cases");
  }

  /// Exactly one of the record* APIs must be called. Implementers may extend
  /// behavior by implementing the corresponding record*Impl.
  ///
  /// Call after inlining succeeded, and did not result in deleting the callee.
  void recordInlining();

  /// Call after inlining succeeded, and results in the callee being
  /// delete-able, meaning, it has no more users, and will be cleaned up
  /// subsequently.
  void recordInliningWithCalleeDeleted();

  /// Call after the decision for a call site was to not inline.
  void recordUnsuccessfulInlining(const InlineResult &Result) {
    markRecorded();
    recordUnsuccessfulInliningImpl(Result);
  }

  /// Call to indicate inlining was not attempted.
  void recordUnattemptedInlining() {
    markRecorded();
    recordUnattemptedInliningImpl();
  }

  /// Get the inlining recommendation.
  bool isInliningRecommended() const { return IsInliningRecommended; }
  const DebugLoc &getOriginalCallSiteDebugLoc() const { return DLoc; }
  const BasicBlock *getOriginalCallSiteBasicBlock() const { return Block; }

protected:
  virtual void recordInliningImpl() {}
  virtual void recordInliningWithCalleeDeletedImpl() {}
  virtual void recordUnsuccessfulInliningImpl(const InlineResult &Result) {}
  virtual void recordUnattemptedInliningImpl() {}

  InlineAdvisor *const Advisor;
  /// Caller and Callee are pre-inlining.
  Function *const Caller;
  Function *const Callee;

  // Capture the context of CB before inlining, as a successful inlining may
  // change that context, and we want to report success or failure in the
  // original context.
  const DebugLoc DLoc;
  const BasicBlock *const Block;
  OptimizationRemarkEmitter &ORE;
  const bool IsInliningRecommended;

private:
  void markRecorded() {
    assert(!Recorded && "Recording should happen exactly once");
    Recorded = true;
  }
  void recordInlineStatsIfNeeded();

  bool Recorded = false;
};

class DefaultInlineAdvice : public InlineAdvice {
public:
  DefaultInlineAdvice(InlineAdvisor *Advisor, CallBase &CB,
                      std::optional<InlineCost> OIC,
                      OptimizationRemarkEmitter &ORE, bool EmitRemarks = true)
      : InlineAdvice(Advisor, CB, ORE, OIC.has_value()), OriginalCB(&CB),
        OIC(OIC), EmitRemarks(EmitRemarks) {}

private:
  void recordUnsuccessfulInliningImpl(const InlineResult &Result) override;
  void recordInliningWithCalleeDeletedImpl() override;
  void recordInliningImpl() override;

private:
  CallBase *const OriginalCB;
  std::optional<InlineCost> OIC;
  bool EmitRemarks;
};

/// Interface for deciding whether to inline a call site or not.
class InlineAdvisor {
public:
  InlineAdvisor(InlineAdvisor &&) = delete;
  virtual ~InlineAdvisor();

  /// Get an InlineAdvice containing a recommendation on whether to
  /// inline or not. \p CB is assumed to be a direct call. \p FAM is assumed to
  /// be up-to-date wrt previous inlining decisions. \p MandatoryOnly indicates
  /// only mandatory (always-inline) call sites should be recommended - this
  /// allows the InlineAdvisor track such inlininings.
  /// Returns:
  /// - An InlineAdvice with the inlining recommendation.
  /// - Null when no recommendation is made (https://reviews.llvm.org/D110658).
  /// TODO: Consider removing the Null return scenario by incorporating the
  /// SampleProfile inliner into an InlineAdvisor
  std::unique_ptr<InlineAdvice> getAdvice(CallBase &CB,
                                          bool MandatoryOnly = false);

  /// This must be called when the Inliner pass is entered, to allow the
  /// InlineAdvisor update internal state, as result of function passes run
  /// between Inliner pass runs (for the same module).
  virtual void onPassEntry(LazyCallGraph::SCC *SCC = nullptr) {}

  /// This must be called when the Inliner pass is exited, as function passes
  /// may be run subsequently. This allows an implementation of InlineAdvisor
  /// to prepare for a partial update, based on the optional SCC.
  virtual void onPassExit(LazyCallGraph::SCC *SCC = nullptr) {}

  /// Support for printer pass
  virtual void print(raw_ostream &OS) const {
    OS << "Unimplemented InlineAdvisor print\n";
  }

  /// NOTE pass name is annotated only when inline advisor constructor provides InlineContext.
  const char *getAnnotatedInlinePassName() const {
    return AnnotatedInlinePassName.c_str();
  }

protected:
  InlineAdvisor(Module &M, FunctionAnalysisManager &FAM,
                std::optional<InlineContext> IC = std::nullopt);
  virtual std::unique_ptr<InlineAdvice> getAdviceImpl(CallBase &CB) = 0;
  virtual std::unique_ptr<InlineAdvice> getMandatoryAdvice(CallBase &CB,
                                                           bool Advice);

  Module &M;
  FunctionAnalysisManager &FAM;
  const std::optional<InlineContext> IC;
  const std::string AnnotatedInlinePassName;
  std::unique_ptr<ImportedFunctionsInliningStatistics> ImportedFunctionsStats;

  enum class MandatoryInliningKind { NotMandatory, Always, Never };

  static MandatoryInliningKind getMandatoryKind(CallBase &CB,
                                                FunctionAnalysisManager &FAM,
                                                OptimizationRemarkEmitter &ORE);

  OptimizationRemarkEmitter &getCallerORE(CallBase &CB);

private:
  friend class InlineAdvice;
};

/// The default (manual heuristics) implementation of the InlineAdvisor. This
/// implementation does not need to keep state between inliner pass runs, and is
/// reusable as-is for inliner pass test scenarios, as well as for regular use.
class DefaultInlineAdvisor : public InlineAdvisor {
public:
  DefaultInlineAdvisor(Module &M, FunctionAnalysisManager &FAM,
                       InlineParams Params, InlineContext IC)
      : InlineAdvisor(M, FAM, IC), Params(Params) {}

private:
  std::unique_ptr<InlineAdvice> getAdviceImpl(CallBase &CB) override;

  InlineParams Params;
};

/// Used for dynamically registering InlineAdvisors as plugins
///
/// An advisor plugin adds a new advisor at runtime by registering an instance
/// of PluginInlineAdvisorAnalysis in the current ModuleAnalysisManager.
/// For example, the following code dynamically registers a
/// DefaultInlineAdvisor:
///
/// namespace {
///
/// InlineAdvisor *defaultAdvisorFactory(Module &M, FunctionAnalysisManager
/// &FAM,
///                                      InlineParams Params, InlineContext IC)
///                                      {
///   return new DefaultInlineAdvisor(M, FAM, Params, IC);
/// }
///
/// struct DefaultDynamicAdvisor : PassInfoMixin<DefaultDynamicAdvisor> {
///   PreservedAnalyses run(Module &, ModuleAnalysisManager &MAM) {
///     PluginInlineAdvisorAnalysis PA(defaultAdvisorFactory);
///     MAM.registerPass([&] { return PA; });
///     return PreservedAnalyses::all();
///   }
/// };
///
/// } // namespace
///
/// extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
/// llvmGetPassPluginInfo() {
///   return {LLVM_PLUGIN_API_VERSION, "DynamicDefaultAdvisor",
///   LLVM_VERSION_STRING,
///           [](PassBuilder &PB) {
///             PB.registerPipelineStartEPCallback(
///                 [](ModulePassManager &MPM, OptimizationLevel Level) {
///                   MPM.addPass(DefaultDynamicAdvisor());
///                 });
///           }};
/// }
///
/// A plugin must implement an AdvisorFactory and register it with a
/// PluginInlineAdvisorAnlysis to the provided ModuleanAlysisManager.
///
/// If such a plugin has been registered
/// InlineAdvisorAnalysis::Result::tryCreate will return the dynamically loaded
/// advisor.
///
class PluginInlineAdvisorAnalysis
    : public AnalysisInfoMixin<PluginInlineAdvisorAnalysis> {
public:
  static AnalysisKey Key;
  static bool HasBeenRegistered;

  typedef InlineAdvisor *(*AdvisorFactory)(Module &M,
                                           FunctionAnalysisManager &FAM,
                                           InlineParams Params,
                                           InlineContext IC);

  PluginInlineAdvisorAnalysis(AdvisorFactory Factory) : Factory(Factory) {
    HasBeenRegistered = true;
    assert(Factory != nullptr &&
           "The plugin advisor factory should not be a null pointer.");
  }

  struct Result {
    AdvisorFactory Factory;
  };

  Result run(Module &M, ModuleAnalysisManager &MAM) { return {Factory}; }
  Result getResult() { return {Factory}; }

private:
  AdvisorFactory Factory;
};

/// The InlineAdvisorAnalysis is a module pass because the InlineAdvisor
/// needs to capture state right before inlining commences over a module.
class InlineAdvisorAnalysis : public AnalysisInfoMixin<InlineAdvisorAnalysis> {
public:
  static AnalysisKey Key;
  InlineAdvisorAnalysis() = default;
  struct Result {
    Result(Module &M, ModuleAnalysisManager &MAM) : M(M), MAM(MAM) {}
    bool invalidate(Module &, const PreservedAnalyses &PA,
                    ModuleAnalysisManager::Invalidator &) {
      // Check whether the analysis has been explicitly invalidated. Otherwise,
      // it's stateless and remains preserved.
      auto PAC = PA.getChecker<InlineAdvisorAnalysis>();
      return !PAC.preservedWhenStateless();
    }
    bool tryCreate(InlineParams Params, InliningAdvisorMode Mode,
                   const ReplayInlinerSettings &ReplaySettings,
                   InlineContext IC);
    InlineAdvisor *getAdvisor() const { return Advisor.get(); }

  private:
    Module &M;
    ModuleAnalysisManager &MAM;
    std::unique_ptr<InlineAdvisor> Advisor;
  };

  Result run(Module &M, ModuleAnalysisManager &MAM) { return Result(M, MAM); }
};

/// Printer pass for the InlineAdvisorAnalysis results.
class InlineAdvisorAnalysisPrinterPass
    : public PassInfoMixin<InlineAdvisorAnalysisPrinterPass> {
  raw_ostream &OS;

public:
  explicit InlineAdvisorAnalysisPrinterPass(raw_ostream &OS) : OS(OS) {}

  PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

  PreservedAnalyses run(LazyCallGraph::SCC &InitialC, CGSCCAnalysisManager &AM,
                        LazyCallGraph &CG, CGSCCUpdateResult &UR);
  static bool isRequired() { return true; }
};

std::unique_ptr<InlineAdvisor>
getReleaseModeAdvisor(Module &M, ModuleAnalysisManager &MAM,
                      std::function<bool(CallBase &)> GetDefaultAdvice);

std::unique_ptr<InlineAdvisor>
getDevelopmentModeAdvisor(Module &M, ModuleAnalysisManager &MAM,
                          std::function<bool(CallBase &)> GetDefaultAdvice);

// Default (manual policy) decision making helper APIs. Shared with the legacy
// pass manager inliner.

/// Return the cost only if the inliner should attempt to inline at the given
/// CallSite. If we return the cost, we will emit an optimisation remark later
/// using that cost, so we won't do so from this function. Return std::nullopt
/// if inlining should not be attempted.
std::optional<InlineCost>
shouldInline(CallBase &CB, function_ref<InlineCost(CallBase &CB)> GetInlineCost,
             OptimizationRemarkEmitter &ORE, bool EnableDeferral = true);

/// Emit ORE message.
void emitInlinedInto(OptimizationRemarkEmitter &ORE, DebugLoc DLoc,
                     const BasicBlock *Block, const Function &Callee,
                     const Function &Caller, bool IsMandatory,
                     function_ref<void(OptimizationRemark &)> ExtraContext = {},
                     const char *PassName = nullptr);

/// Emit ORE message based in cost (default heuristic).
void emitInlinedIntoBasedOnCost(OptimizationRemarkEmitter &ORE, DebugLoc DLoc,
                                const BasicBlock *Block, const Function &Callee,
                                const Function &Caller, const InlineCost &IC,
                                bool ForProfileContext = false,
                                const char *PassName = nullptr);

/// Add location info to ORE message.
void addLocationToRemarks(OptimizationRemark &Remark, DebugLoc DLoc);

/// Set the inline-remark attribute.
void setInlineRemark(CallBase &CB, StringRef Message);

/// Utility for extracting the inline cost message to a string.
std::string inlineCostStr(const InlineCost &IC);
} // namespace llvm
#endif // LLVM_ANALYSIS_INLINEADVISOR_H
