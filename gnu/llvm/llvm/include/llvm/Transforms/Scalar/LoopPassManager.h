//===- LoopPassManager.h - Loop pass management -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This header provides classes for managing a pipeline of passes over loops
/// in LLVM IR.
///
/// The primary loop pass pipeline is managed in a very particular way to
/// provide a set of core guarantees:
/// 1) Loops are, where possible, in simplified form.
/// 2) Loops are *always* in LCSSA form.
/// 3) A collection of Loop-specific analysis results are available:
///    - LoopInfo
///    - DominatorTree
///    - ScalarEvolution
///    - AAManager
/// 4) All loop passes preserve #1 (where possible), #2, and #3.
/// 5) Loop passes run over each loop in the loop nest from the innermost to
///    the outermost. Specifically, all inner loops are processed before
///    passes run over outer loops. When running the pipeline across an inner
///    loop creates new inner loops, those are added and processed in this
///    order as well.
///
/// This process is designed to facilitate transformations which simplify,
/// reduce, and remove loops. For passes which are more oriented towards
/// optimizing loops, especially optimizing loop *nests* instead of single
/// loops in isolation, this framework is less interesting.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPPASSMANAGER_H
#define LLVM_TRANSFORMS_SCALAR_LOOPPASSMANAGER_H

#include "llvm/ADT/PriorityWorklist.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopNestAnalysis.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/Transforms/Utils/LCSSA.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include <memory>

namespace llvm {

// Forward declarations of an update tracking API used in the pass manager.
class LPMUpdater;
class PassInstrumentation;

namespace {

template <typename PassT>
using HasRunOnLoopT = decltype(std::declval<PassT>().run(
    std::declval<Loop &>(), std::declval<LoopAnalysisManager &>(),
    std::declval<LoopStandardAnalysisResults &>(),
    std::declval<LPMUpdater &>()));

} // namespace

// Explicit specialization and instantiation declarations for the pass manager.
// See the comments on the definition of the specialization for details on how
// it differs from the primary template.
template <>
class PassManager<Loop, LoopAnalysisManager, LoopStandardAnalysisResults &,
                  LPMUpdater &>
    : public PassInfoMixin<
          PassManager<Loop, LoopAnalysisManager, LoopStandardAnalysisResults &,
                      LPMUpdater &>> {
public:
  explicit PassManager() = default;

  // FIXME: These are equivalent to the default move constructor/move
  // assignment. However, using = default triggers linker errors due to the
  // explicit instantiations below. Find a way to use the default and remove the
  // duplicated code here.
  PassManager(PassManager &&Arg)
      : IsLoopNestPass(std::move(Arg.IsLoopNestPass)),
        LoopPasses(std::move(Arg.LoopPasses)),
        LoopNestPasses(std::move(Arg.LoopNestPasses)) {}

  PassManager &operator=(PassManager &&RHS) {
    IsLoopNestPass = std::move(RHS.IsLoopNestPass);
    LoopPasses = std::move(RHS.LoopPasses);
    LoopNestPasses = std::move(RHS.LoopNestPasses);
    return *this;
  }

  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);

  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);
  /// Add either a loop pass or a loop-nest pass to the pass manager. Append \p
  /// Pass to the list of loop passes if it has a dedicated \fn run() method for
  /// loops and to the list of loop-nest passes if the \fn run() method is for
  /// loop-nests instead. Also append whether \p Pass is loop-nest pass or not
  /// to the end of \var IsLoopNestPass so we can easily identify the types of
  /// passes in the pass manager later.
  template <typename PassT>
  LLVM_ATTRIBUTE_MINSIZE
      std::enable_if_t<is_detected<HasRunOnLoopT, PassT>::value>
      addPass(PassT &&Pass) {
    using LoopPassModelT =
        detail::PassModel<Loop, PassT, LoopAnalysisManager,
                          LoopStandardAnalysisResults &, LPMUpdater &>;
    IsLoopNestPass.push_back(false);
    // Do not use make_unique or emplace_back, they cause too many template
    // instantiations, causing terrible compile times.
    LoopPasses.push_back(std::unique_ptr<LoopPassConceptT>(
        new LoopPassModelT(std::forward<PassT>(Pass))));
  }

  template <typename PassT>
  LLVM_ATTRIBUTE_MINSIZE
      std::enable_if_t<!is_detected<HasRunOnLoopT, PassT>::value>
      addPass(PassT &&Pass) {
    using LoopNestPassModelT =
        detail::PassModel<LoopNest, PassT, LoopAnalysisManager,
                          LoopStandardAnalysisResults &, LPMUpdater &>;
    IsLoopNestPass.push_back(true);
    // Do not use make_unique or emplace_back, they cause too many template
    // instantiations, causing terrible compile times.
    LoopNestPasses.push_back(std::unique_ptr<LoopNestPassConceptT>(
        new LoopNestPassModelT(std::forward<PassT>(Pass))));
  }

  bool isEmpty() const { return LoopPasses.empty() && LoopNestPasses.empty(); }

  static bool isRequired() { return true; }

  size_t getNumLoopPasses() const { return LoopPasses.size(); }
  size_t getNumLoopNestPasses() const { return LoopNestPasses.size(); }

protected:
  using LoopPassConceptT =
      detail::PassConcept<Loop, LoopAnalysisManager,
                          LoopStandardAnalysisResults &, LPMUpdater &>;
  using LoopNestPassConceptT =
      detail::PassConcept<LoopNest, LoopAnalysisManager,
                          LoopStandardAnalysisResults &, LPMUpdater &>;

  // BitVector that identifies whether the passes are loop passes or loop-nest
  // passes (true for loop-nest passes).
  BitVector IsLoopNestPass;
  std::vector<std::unique_ptr<LoopPassConceptT>> LoopPasses;
  std::vector<std::unique_ptr<LoopNestPassConceptT>> LoopNestPasses;

  /// Run either a loop pass or a loop-nest pass. Returns `std::nullopt` if
  /// PassInstrumentation's BeforePass returns false. Otherwise, returns the
  /// preserved analyses of the pass.
  template <typename IRUnitT, typename PassT>
  std::optional<PreservedAnalyses>
  runSinglePass(IRUnitT &IR, PassT &Pass, LoopAnalysisManager &AM,
                LoopStandardAnalysisResults &AR, LPMUpdater &U,
                PassInstrumentation &PI);

  PreservedAnalyses runWithLoopNestPasses(Loop &L, LoopAnalysisManager &AM,
                                          LoopStandardAnalysisResults &AR,
                                          LPMUpdater &U);
  PreservedAnalyses runWithoutLoopNestPasses(Loop &L, LoopAnalysisManager &AM,
                                             LoopStandardAnalysisResults &AR,
                                             LPMUpdater &U);

private:
  static const Loop &getLoopFromIR(Loop &L) { return L; }
  static const Loop &getLoopFromIR(LoopNest &LN) {
    return LN.getOutermostLoop();
  }
};

/// The Loop pass manager.
///
/// See the documentation for the PassManager template for details. It runs
/// a sequence of Loop passes over each Loop that the manager is run over. This
/// typedef serves as a convenient way to refer to this construct.
typedef PassManager<Loop, LoopAnalysisManager, LoopStandardAnalysisResults &,
                    LPMUpdater &>
    LoopPassManager;

/// A partial specialization of the require analysis template pass to forward
/// the extra parameters from a transformation's run method to the
/// AnalysisManager's getResult.
template <typename AnalysisT>
struct RequireAnalysisPass<AnalysisT, Loop, LoopAnalysisManager,
                           LoopStandardAnalysisResults &, LPMUpdater &>
    : PassInfoMixin<
          RequireAnalysisPass<AnalysisT, Loop, LoopAnalysisManager,
                              LoopStandardAnalysisResults &, LPMUpdater &>> {
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &) {
    (void)AM.template getResult<AnalysisT>(L, AR);
    return PreservedAnalyses::all();
  }
  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName) {
    auto ClassName = AnalysisT::name();
    auto PassName = MapClassName2PassName(ClassName);
    OS << "require<" << PassName << '>';
  }
};

/// An alias template to easily name a require analysis loop pass.
template <typename AnalysisT>
using RequireAnalysisLoopPass =
    RequireAnalysisPass<AnalysisT, Loop, LoopAnalysisManager,
                        LoopStandardAnalysisResults &, LPMUpdater &>;

class FunctionToLoopPassAdaptor;

/// This class provides an interface for updating the loop pass manager based
/// on mutations to the loop nest.
///
/// A reference to an instance of this class is passed as an argument to each
/// Loop pass, and Loop passes should use it to update LPM infrastructure if
/// they modify the loop nest structure.
///
/// \c LPMUpdater comes with two modes: the loop mode and the loop-nest mode. In
/// loop mode, all the loops in the function will be pushed into the worklist
/// and when new loops are added to the pipeline, their subloops are also
/// inserted recursively. On the other hand, in loop-nest mode, only top-level
/// loops are contained in the worklist and the addition of new (top-level)
/// loops will not trigger the addition of their subloops.
class LPMUpdater {
public:
  /// This can be queried by loop passes which run other loop passes (like pass
  /// managers) to know whether the loop needs to be skipped due to updates to
  /// the loop nest.
  ///
  /// If this returns true, the loop object may have been deleted, so passes
  /// should take care not to touch the object.
  bool skipCurrentLoop() const { return SkipCurrentLoop; }

  /// Loop passes should use this method to indicate they have deleted a loop
  /// from the nest.
  ///
  /// Note that this loop must either be the current loop or a subloop of the
  /// current loop. This routine must be called prior to removing the loop from
  /// the loop nest.
  ///
  /// If this is called for the current loop, in addition to clearing any
  /// state, this routine will mark that the current loop should be skipped by
  /// the rest of the pass management infrastructure.
  void markLoopAsDeleted(Loop &L, llvm::StringRef Name) {
    LAM.clear(L, Name);
    assert((&L == CurrentL || CurrentL->contains(&L)) &&
           "Cannot delete a loop outside of the "
           "subloop tree currently being processed.");
    if (&L == CurrentL)
      SkipCurrentLoop = true;
  }

  void setParentLoop(Loop *L) {
#ifdef LLVM_ENABLE_ABI_BREAKING_CHECKS
    ParentL = L;
#endif
  }

  /// Loop passes should use this method to indicate they have added new child
  /// loops of the current loop.
  ///
  /// \p NewChildLoops must contain only the immediate children. Any nested
  /// loops within them will be visited in postorder as usual for the loop pass
  /// manager.
  void addChildLoops(ArrayRef<Loop *> NewChildLoops) {
    assert(!LoopNestMode &&
           "Child loops should not be pushed in loop-nest mode.");
    // Insert ourselves back into the worklist first, as this loop should be
    // revisited after all the children have been processed.
    Worklist.insert(CurrentL);

#ifndef NDEBUG
    for (Loop *NewL : NewChildLoops)
      assert(NewL->getParentLoop() == CurrentL && "All of the new loops must "
                                                  "be immediate children of "
                                                  "the current loop!");
#endif

    appendLoopsToWorklist(NewChildLoops, Worklist);

    // Also skip further processing of the current loop--it will be revisited
    // after all of its newly added children are accounted for.
    SkipCurrentLoop = true;
  }

  /// Loop passes should use this method to indicate they have added new
  /// sibling loops to the current loop.
  ///
  /// \p NewSibLoops must only contain the immediate sibling loops. Any nested
  /// loops within them will be visited in postorder as usual for the loop pass
  /// manager.
  void addSiblingLoops(ArrayRef<Loop *> NewSibLoops) {
#if defined(LLVM_ENABLE_ABI_BREAKING_CHECKS) && !defined(NDEBUG)
    for (Loop *NewL : NewSibLoops)
      assert(NewL->getParentLoop() == ParentL &&
             "All of the new loops must be siblings of the current loop!");
#endif

    if (LoopNestMode)
      Worklist.insert(NewSibLoops);
    else
      appendLoopsToWorklist(NewSibLoops, Worklist);

    // No need to skip the current loop or revisit it, as sibling loops
    // shouldn't impact anything.
  }

  /// Restart the current loop.
  ///
  /// Loop passes should call this method to indicate the current loop has been
  /// sufficiently changed that it should be re-visited from the begining of
  /// the loop pass pipeline rather than continuing.
  void revisitCurrentLoop() {
    // Tell the currently in-flight pipeline to stop running.
    SkipCurrentLoop = true;

    // And insert ourselves back into the worklist.
    Worklist.insert(CurrentL);
  }

  bool isLoopNestChanged() const {
    return LoopNestChanged;
  }

  /// Loopnest passes should use this method to indicate if the
  /// loopnest has been modified.
  void markLoopNestChanged(bool Changed) {
    LoopNestChanged = Changed;
  }

private:
  friend class llvm::FunctionToLoopPassAdaptor;

  /// The \c FunctionToLoopPassAdaptor's worklist of loops to process.
  SmallPriorityWorklist<Loop *, 4> &Worklist;

  /// The analysis manager for use in the current loop nest.
  LoopAnalysisManager &LAM;

  Loop *CurrentL;
  bool SkipCurrentLoop;
  const bool LoopNestMode;
  bool LoopNestChanged;

#ifdef LLVM_ENABLE_ABI_BREAKING_CHECKS
  // In debug builds we also track the parent loop to implement asserts even in
  // the face of loop deletion.
  Loop *ParentL;
#endif

  LPMUpdater(SmallPriorityWorklist<Loop *, 4> &Worklist,
             LoopAnalysisManager &LAM, bool LoopNestMode = false,
             bool LoopNestChanged = false)
      : Worklist(Worklist), LAM(LAM), LoopNestMode(LoopNestMode),
        LoopNestChanged(LoopNestChanged) {}
};

template <typename IRUnitT, typename PassT>
std::optional<PreservedAnalyses> LoopPassManager::runSinglePass(
    IRUnitT &IR, PassT &Pass, LoopAnalysisManager &AM,
    LoopStandardAnalysisResults &AR, LPMUpdater &U, PassInstrumentation &PI) {
  // Get the loop in case of Loop pass and outermost loop in case of LoopNest
  // pass which is to be passed to BeforePass and AfterPass call backs.
  const Loop &L = getLoopFromIR(IR);
  // Check the PassInstrumentation's BeforePass callbacks before running the
  // pass, skip its execution completely if asked to (callback returns false).
  if (!PI.runBeforePass<Loop>(*Pass, L))
    return std::nullopt;

  PreservedAnalyses PA = Pass->run(IR, AM, AR, U);

  // do not pass deleted Loop into the instrumentation
  if (U.skipCurrentLoop())
    PI.runAfterPassInvalidated<IRUnitT>(*Pass, PA);
  else
    PI.runAfterPass<Loop>(*Pass, L, PA);
  return PA;
}

/// Adaptor that maps from a function to its loops.
///
/// Designed to allow composition of a LoopPass(Manager) and a
/// FunctionPassManager. Note that if this pass is constructed with a \c
/// FunctionAnalysisManager it will run the \c LoopAnalysisManagerFunctionProxy
/// analysis prior to running the loop passes over the function to enable a \c
/// LoopAnalysisManager to be used within this run safely.
///
/// The adaptor comes with two modes: the loop mode and the loop-nest mode, and
/// the worklist updater lived inside will be in the same mode as the adaptor
/// (refer to the documentation of \c LPMUpdater for more detailed explanation).
/// Specifically, in loop mode, all loops in the function will be pushed into
/// the worklist and processed by \p Pass, while only top-level loops are
/// processed in loop-nest mode. Please refer to the various specializations of
/// \fn createLoopFunctionToLoopPassAdaptor to see when loop mode and loop-nest
/// mode are used.
class FunctionToLoopPassAdaptor
    : public PassInfoMixin<FunctionToLoopPassAdaptor> {
public:
  using PassConceptT =
      detail::PassConcept<Loop, LoopAnalysisManager,
                          LoopStandardAnalysisResults &, LPMUpdater &>;

  explicit FunctionToLoopPassAdaptor(std::unique_ptr<PassConceptT> Pass,
                                     bool UseMemorySSA = false,
                                     bool UseBlockFrequencyInfo = false,
                                     bool UseBranchProbabilityInfo = false,
                                     bool LoopNestMode = false)
      : Pass(std::move(Pass)), UseMemorySSA(UseMemorySSA),
        UseBlockFrequencyInfo(UseBlockFrequencyInfo),
        UseBranchProbabilityInfo(UseBranchProbabilityInfo),
        LoopNestMode(LoopNestMode) {
    LoopCanonicalizationFPM.addPass(LoopSimplifyPass());
    LoopCanonicalizationFPM.addPass(LCSSAPass());
  }

  /// Runs the loop passes across every loop in the function.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);

  static bool isRequired() { return true; }

  bool isLoopNestMode() const { return LoopNestMode; }

private:
  std::unique_ptr<PassConceptT> Pass;

  FunctionPassManager LoopCanonicalizationFPM;

  bool UseMemorySSA = false;
  bool UseBlockFrequencyInfo = false;
  bool UseBranchProbabilityInfo = false;
  const bool LoopNestMode;
};

/// A function to deduce a loop pass type and wrap it in the templated
/// adaptor.
///
/// If \p Pass is a loop pass, the returned adaptor will be in loop mode.
template <typename LoopPassT>
inline std::enable_if_t<is_detected<HasRunOnLoopT, LoopPassT>::value,
                        FunctionToLoopPassAdaptor>
createFunctionToLoopPassAdaptor(LoopPassT &&Pass, bool UseMemorySSA = false,
                                bool UseBlockFrequencyInfo = false,
                                bool UseBranchProbabilityInfo = false) {
  using PassModelT =
      detail::PassModel<Loop, LoopPassT, LoopAnalysisManager,
                        LoopStandardAnalysisResults &, LPMUpdater &>;
  // Do not use make_unique, it causes too many template instantiations,
  // causing terrible compile times.
  return FunctionToLoopPassAdaptor(
      std::unique_ptr<FunctionToLoopPassAdaptor::PassConceptT>(
          new PassModelT(std::forward<LoopPassT>(Pass))),
      UseMemorySSA, UseBlockFrequencyInfo, UseBranchProbabilityInfo, false);
}

/// If \p Pass is a loop-nest pass, \p Pass will first be wrapped into a
/// \c LoopPassManager and the returned adaptor will be in loop-nest mode.
template <typename LoopNestPassT>
inline std::enable_if_t<!is_detected<HasRunOnLoopT, LoopNestPassT>::value,
                        FunctionToLoopPassAdaptor>
createFunctionToLoopPassAdaptor(LoopNestPassT &&Pass, bool UseMemorySSA = false,
                                bool UseBlockFrequencyInfo = false,
                                bool UseBranchProbabilityInfo = false) {
  LoopPassManager LPM;
  LPM.addPass(std::forward<LoopNestPassT>(Pass));
  using PassModelT =
      detail::PassModel<Loop, LoopPassManager, LoopAnalysisManager,
                        LoopStandardAnalysisResults &, LPMUpdater &>;
  // Do not use make_unique, it causes too many template instantiations,
  // causing terrible compile times.
  return FunctionToLoopPassAdaptor(
      std::unique_ptr<FunctionToLoopPassAdaptor::PassConceptT>(
          new PassModelT(std::move(LPM))),
      UseMemorySSA, UseBlockFrequencyInfo, UseBranchProbabilityInfo, true);
}

/// If \p Pass is an instance of \c LoopPassManager, the returned adaptor will
/// be in loop-nest mode if the pass manager contains only loop-nest passes.
template <>
inline FunctionToLoopPassAdaptor
createFunctionToLoopPassAdaptor<LoopPassManager>(
    LoopPassManager &&LPM, bool UseMemorySSA, bool UseBlockFrequencyInfo,
    bool UseBranchProbabilityInfo) {
  // Check if LPM contains any loop pass and if it does not, returns an adaptor
  // in loop-nest mode.
  using PassModelT =
      detail::PassModel<Loop, LoopPassManager, LoopAnalysisManager,
                        LoopStandardAnalysisResults &, LPMUpdater &>;
  bool LoopNestMode = (LPM.getNumLoopPasses() == 0);
  // Do not use make_unique, it causes too many template instantiations,
  // causing terrible compile times.
  return FunctionToLoopPassAdaptor(
      std::unique_ptr<FunctionToLoopPassAdaptor::PassConceptT>(
          new PassModelT(std::move(LPM))),
      UseMemorySSA, UseBlockFrequencyInfo, UseBranchProbabilityInfo,
      LoopNestMode);
}

/// Pass for printing a loop's contents as textual IR.
class PrintLoopPass : public PassInfoMixin<PrintLoopPass> {
  raw_ostream &OS;
  std::string Banner;

public:
  PrintLoopPass();
  PrintLoopPass(raw_ostream &OS, const std::string &Banner = "");

  PreservedAnalyses run(Loop &L, LoopAnalysisManager &,
                        LoopStandardAnalysisResults &, LPMUpdater &);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_LOOPPASSMANAGER_H
