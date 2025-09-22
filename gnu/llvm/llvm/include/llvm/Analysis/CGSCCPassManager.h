//===- CGSCCPassManager.h - Call graph pass management ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This header provides classes for managing passes over SCCs of the call
/// graph. These passes form an important component of LLVM's interprocedural
/// optimizations. Because they operate on the SCCs of the call graph, and they
/// traverse the graph in post-order, they can effectively do pair-wise
/// interprocedural optimizations for all call edges in the program while
/// incrementally refining it and improving the context of these pair-wise
/// optimizations. At each call site edge, the callee has already been
/// optimized as much as is possible. This in turn allows very accurate
/// analysis of it for IPO.
///
/// A secondary more general goal is to be able to isolate optimization on
/// unrelated parts of the IR module. This is useful to ensure our
/// optimizations are principled and don't miss oportunities where refinement
/// of one part of the module influences transformations in another part of the
/// module. But this is also useful if we want to parallelize the optimizations
/// across common large module graph shapes which tend to be very wide and have
/// large regions of unrelated cliques.
///
/// To satisfy these goals, we use the LazyCallGraph which provides two graphs
/// nested inside each other (and built lazily from the bottom-up): the call
/// graph proper, and a reference graph. The reference graph is super set of
/// the call graph and is a conservative approximation of what could through
/// scalar or CGSCC transforms *become* the call graph. Using this allows us to
/// ensure we optimize functions prior to them being introduced into the call
/// graph by devirtualization or other technique, and thus ensures that
/// subsequent pair-wise interprocedural optimizations observe the optimized
/// form of these functions. The (potentially transitive) reference
/// reachability used by the reference graph is a conservative approximation
/// that still allows us to have independent regions of the graph.
///
/// FIXME: There is one major drawback of the reference graph: in its naive
/// form it is quadratic because it contains a distinct edge for each
/// (potentially indirect) reference, even if are all through some common
/// global table of function pointers. This can be fixed in a number of ways
/// that essentially preserve enough of the normalization. While it isn't
/// expected to completely preclude the usability of this, it will need to be
/// addressed.
///
///
/// All of these issues are made substantially more complex in the face of
/// mutations to the call graph while optimization passes are being run. When
/// mutations to the call graph occur we want to achieve two different things:
///
/// - We need to update the call graph in-flight and invalidate analyses
///   cached on entities in the graph. Because of the cache-based analysis
///   design of the pass manager, it is essential to have stable identities for
///   the elements of the IR that passes traverse, and to invalidate any
///   analyses cached on these elements as the mutations take place.
///
/// - We want to preserve the incremental and post-order traversal of the
///   graph even as it is refined and mutated. This means we want optimization
///   to observe the most refined form of the call graph and to do so in
///   post-order.
///
/// To address this, the CGSCC manager uses both worklists that can be expanded
/// by passes which transform the IR, and provides invalidation tests to skip
/// entries that become dead. This extra data is provided to every SCC pass so
/// that it can carefully update the manager's traversal as the call graph
/// mutates.
///
/// We also provide support for running function passes within the CGSCC walk,
/// and there we provide automatic update of the call graph including of the
/// pass manager to reflect call graph changes that fall out naturally as part
/// of scalar transformations.
///
/// The patterns used to ensure the goals of post-order visitation of the fully
/// refined graph:
///
/// 1) Sink toward the "bottom" as the graph is refined. This means that any
///    iteration continues in some valid post-order sequence after the mutation
///    has altered the structure.
///
/// 2) Enqueue in post-order, including the current entity. If the current
///    entity's shape changes, it and everything after it in post-order needs
///    to be visited to observe that shape.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CGSCCPASSMANAGER_H
#define LLVM_ANALYSIS_CGSCCPASSMANAGER_H

#include "llvm/ADT/MapVector.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <utility>

namespace llvm {

class Function;
class Value;
template <typename T, unsigned int N> class SmallPriorityWorklist;
struct CGSCCUpdateResult;

class Module;

// Allow debug logging in this inline function.
#define DEBUG_TYPE "cgscc"

/// Extern template declaration for the analysis set for this IR unit.
extern template class AllAnalysesOn<LazyCallGraph::SCC>;

extern template class AnalysisManager<LazyCallGraph::SCC, LazyCallGraph &>;

/// The CGSCC analysis manager.
///
/// See the documentation for the AnalysisManager template for detail
/// documentation. This type serves as a convenient way to refer to this
/// construct in the adaptors and proxies used to integrate this into the larger
/// pass manager infrastructure.
using CGSCCAnalysisManager =
    AnalysisManager<LazyCallGraph::SCC, LazyCallGraph &>;

// Explicit specialization and instantiation declarations for the pass manager.
// See the comments on the definition of the specialization for details on how
// it differs from the primary template.
template <>
PreservedAnalyses
PassManager<LazyCallGraph::SCC, CGSCCAnalysisManager, LazyCallGraph &,
            CGSCCUpdateResult &>::run(LazyCallGraph::SCC &InitialC,
                                      CGSCCAnalysisManager &AM,
                                      LazyCallGraph &G, CGSCCUpdateResult &UR);
extern template class PassManager<LazyCallGraph::SCC, CGSCCAnalysisManager,
                                  LazyCallGraph &, CGSCCUpdateResult &>;

/// The CGSCC pass manager.
///
/// See the documentation for the PassManager template for details. It runs
/// a sequence of SCC passes over each SCC that the manager is run over. This
/// type serves as a convenient way to refer to this construct.
using CGSCCPassManager =
    PassManager<LazyCallGraph::SCC, CGSCCAnalysisManager, LazyCallGraph &,
                CGSCCUpdateResult &>;

/// An explicit specialization of the require analysis template pass.
template <typename AnalysisT>
struct RequireAnalysisPass<AnalysisT, LazyCallGraph::SCC, CGSCCAnalysisManager,
                           LazyCallGraph &, CGSCCUpdateResult &>
    : PassInfoMixin<RequireAnalysisPass<AnalysisT, LazyCallGraph::SCC,
                                        CGSCCAnalysisManager, LazyCallGraph &,
                                        CGSCCUpdateResult &>> {
  PreservedAnalyses run(LazyCallGraph::SCC &C, CGSCCAnalysisManager &AM,
                        LazyCallGraph &CG, CGSCCUpdateResult &) {
    (void)AM.template getResult<AnalysisT>(C, CG);
    return PreservedAnalyses::all();
  }
  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName) {
    auto ClassName = AnalysisT::name();
    auto PassName = MapClassName2PassName(ClassName);
    OS << "require<" << PassName << '>';
  }
};

/// A proxy from a \c CGSCCAnalysisManager to a \c Module.
using CGSCCAnalysisManagerModuleProxy =
    InnerAnalysisManagerProxy<CGSCCAnalysisManager, Module>;

/// We need a specialized result for the \c CGSCCAnalysisManagerModuleProxy so
/// it can have access to the call graph in order to walk all the SCCs when
/// invalidating things.
template <> class CGSCCAnalysisManagerModuleProxy::Result {
public:
  explicit Result(CGSCCAnalysisManager &InnerAM, LazyCallGraph &G)
      : InnerAM(&InnerAM), G(&G) {}

  /// Accessor for the analysis manager.
  CGSCCAnalysisManager &getManager() { return *InnerAM; }

  /// Handler for invalidation of the Module.
  ///
  /// If the proxy analysis itself is preserved, then we assume that the set of
  /// SCCs in the Module hasn't changed. Thus any pointers to SCCs in the
  /// CGSCCAnalysisManager are still valid, and we don't need to call \c clear
  /// on the CGSCCAnalysisManager.
  ///
  /// Regardless of whether this analysis is marked as preserved, all of the
  /// analyses in the \c CGSCCAnalysisManager are potentially invalidated based
  /// on the set of preserved analyses.
  bool invalidate(Module &M, const PreservedAnalyses &PA,
                  ModuleAnalysisManager::Invalidator &Inv);

private:
  CGSCCAnalysisManager *InnerAM;
  LazyCallGraph *G;
};

/// Provide a specialized run method for the \c CGSCCAnalysisManagerModuleProxy
/// so it can pass the lazy call graph to the result.
template <>
CGSCCAnalysisManagerModuleProxy::Result
CGSCCAnalysisManagerModuleProxy::run(Module &M, ModuleAnalysisManager &AM);

// Ensure the \c CGSCCAnalysisManagerModuleProxy is provided as an extern
// template.
extern template class InnerAnalysisManagerProxy<CGSCCAnalysisManager, Module>;

extern template class OuterAnalysisManagerProxy<
    ModuleAnalysisManager, LazyCallGraph::SCC, LazyCallGraph &>;

/// A proxy from a \c ModuleAnalysisManager to an \c SCC.
using ModuleAnalysisManagerCGSCCProxy =
    OuterAnalysisManagerProxy<ModuleAnalysisManager, LazyCallGraph::SCC,
                              LazyCallGraph &>;

/// Support structure for SCC passes to communicate updates the call graph back
/// to the CGSCC pass manager infrastructure.
///
/// The CGSCC pass manager runs SCC passes which are allowed to update the call
/// graph and SCC structures. This means the structure the pass manager works
/// on is mutating underneath it. In order to support that, there needs to be
/// careful communication about the precise nature and ramifications of these
/// updates to the pass management infrastructure.
///
/// All SCC passes will have to accept a reference to the management layer's
/// update result struct and use it to reflect the results of any CG updates
/// performed.
///
/// Passes which do not change the call graph structure in any way can just
/// ignore this argument to their run method.
struct CGSCCUpdateResult {
  /// Worklist of the SCCs queued for processing.
  ///
  /// When a pass refines the graph and creates new SCCs or causes them to have
  /// a different shape or set of component functions it should add the SCCs to
  /// this worklist so that we visit them in the refined form.
  ///
  /// Note that if the SCCs are part of a RefSCC that is added to the \c
  /// RCWorklist, they don't need to be added here as visiting the RefSCC will
  /// be sufficient to re-visit the SCCs within it.
  ///
  /// This worklist is in reverse post-order, as we pop off the back in order
  /// to observe SCCs in post-order. When adding SCCs, clients should add them
  /// in reverse post-order.
  SmallPriorityWorklist<LazyCallGraph::SCC *, 1> &CWorklist;

  /// The set of invalidated SCCs which should be skipped if they are found
  /// in \c CWorklist.
  ///
  /// This is used to quickly prune out SCCs when they get deleted and happen
  /// to already be on the worklist. We use this primarily to avoid scanning
  /// the list and removing entries from it.
  SmallPtrSetImpl<LazyCallGraph::SCC *> &InvalidatedSCCs;

  /// If non-null, the updated current \c SCC being processed.
  ///
  /// This is set when a graph refinement takes place and the "current" point
  /// in the graph moves "down" or earlier in the post-order walk. This will
  /// often cause the "current" SCC to be a newly created SCC object and the
  /// old one to be added to the above worklist. When that happens, this
  /// pointer is non-null and can be used to continue processing the "top" of
  /// the post-order walk.
  LazyCallGraph::SCC *UpdatedC;

  /// Preserved analyses across SCCs.
  ///
  /// We specifically want to allow CGSCC passes to mutate ancestor IR
  /// (changing both the CG structure and the function IR itself). However,
  /// this means we need to take special care to correctly mark what analyses
  /// are preserved *across* SCCs. We have to track this out-of-band here
  /// because within the main `PassManager` infrastructure we need to mark
  /// everything within an SCC as preserved in order to avoid repeatedly
  /// invalidating the same analyses as we unnest pass managers and adaptors.
  /// So we track the cross-SCC version of the preserved analyses here from any
  /// code that does direct invalidation of SCC analyses, and then use it
  /// whenever we move forward in the post-order walk of SCCs before running
  /// passes over the new SCC.
  PreservedAnalyses CrossSCCPA;

  /// A hacky area where the inliner can retain history about inlining
  /// decisions that mutated the call graph's SCC structure in order to avoid
  /// infinite inlining. See the comments in the inliner's CG update logic.
  ///
  /// FIXME: Keeping this here seems like a big layering issue, we should look
  /// for a better technique.
  SmallDenseSet<std::pair<LazyCallGraph::Node *, LazyCallGraph::SCC *>, 4>
      &InlinedInternalEdges;

  /// Functions that a pass has considered to be dead to be removed at the end
  /// of the call graph walk in batch.
  SmallVector<Function *, 4> &DeadFunctions;

  /// Weak VHs to keep track of indirect calls for the purposes of detecting
  /// devirtualization.
  ///
  /// This is a map to avoid having duplicate entries. If a Value is
  /// deallocated, its corresponding WeakTrackingVH will be nulled out. When
  /// checking if a Value is in the map or not, also check if the corresponding
  /// WeakTrackingVH is null to avoid issues with a new Value sharing the same
  /// address as a deallocated one.
  SmallMapVector<Value *, WeakTrackingVH, 16> IndirectVHs;
};

/// The core module pass which does a post-order walk of the SCCs and
/// runs a CGSCC pass over each one.
///
/// Designed to allow composition of a CGSCCPass(Manager) and
/// a ModulePassManager. Note that this pass must be run with a module analysis
/// manager as it uses the LazyCallGraph analysis. It will also run the
/// \c CGSCCAnalysisManagerModuleProxy analysis prior to running the CGSCC
/// pass over the module to enable a \c FunctionAnalysisManager to be used
/// within this run safely.
class ModuleToPostOrderCGSCCPassAdaptor
    : public PassInfoMixin<ModuleToPostOrderCGSCCPassAdaptor> {
public:
  using PassConceptT =
      detail::PassConcept<LazyCallGraph::SCC, CGSCCAnalysisManager,
                          LazyCallGraph &, CGSCCUpdateResult &>;

  explicit ModuleToPostOrderCGSCCPassAdaptor(std::unique_ptr<PassConceptT> Pass)
      : Pass(std::move(Pass)) {}

  ModuleToPostOrderCGSCCPassAdaptor(ModuleToPostOrderCGSCCPassAdaptor &&Arg)
      : Pass(std::move(Arg.Pass)) {}

  friend void swap(ModuleToPostOrderCGSCCPassAdaptor &LHS,
                   ModuleToPostOrderCGSCCPassAdaptor &RHS) {
    std::swap(LHS.Pass, RHS.Pass);
  }

  ModuleToPostOrderCGSCCPassAdaptor &
  operator=(ModuleToPostOrderCGSCCPassAdaptor RHS) {
    swap(*this, RHS);
    return *this;
  }

  /// Runs the CGSCC pass across every SCC in the module.
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName) {
    OS << "cgscc(";
    Pass->printPipeline(OS, MapClassName2PassName);
    OS << ')';
  }

  static bool isRequired() { return true; }

private:
  std::unique_ptr<PassConceptT> Pass;
};

/// A function to deduce a function pass type and wrap it in the
/// templated adaptor.
template <typename CGSCCPassT>
ModuleToPostOrderCGSCCPassAdaptor
createModuleToPostOrderCGSCCPassAdaptor(CGSCCPassT &&Pass) {
  using PassModelT =
      detail::PassModel<LazyCallGraph::SCC, CGSCCPassT, CGSCCAnalysisManager,
                        LazyCallGraph &, CGSCCUpdateResult &>;
  // Do not use make_unique, it causes too many template instantiations,
  // causing terrible compile times.
  return ModuleToPostOrderCGSCCPassAdaptor(
      std::unique_ptr<ModuleToPostOrderCGSCCPassAdaptor::PassConceptT>(
          new PassModelT(std::forward<CGSCCPassT>(Pass))));
}

/// A proxy from a \c FunctionAnalysisManager to an \c SCC.
///
/// When a module pass runs and triggers invalidation, both the CGSCC and
/// Function analysis manager proxies on the module get an invalidation event.
/// We don't want to fully duplicate responsibility for most of the
/// invalidation logic. Instead, this layer is only responsible for SCC-local
/// invalidation events. We work with the module's FunctionAnalysisManager to
/// invalidate function analyses.
class FunctionAnalysisManagerCGSCCProxy
    : public AnalysisInfoMixin<FunctionAnalysisManagerCGSCCProxy> {
public:
  class Result {
  public:
    explicit Result() : FAM(nullptr) {}
    explicit Result(FunctionAnalysisManager &FAM) : FAM(&FAM) {}

    void updateFAM(FunctionAnalysisManager &FAM) { this->FAM = &FAM; }
    /// Accessor for the analysis manager.
    FunctionAnalysisManager &getManager() {
      assert(FAM);
      return *FAM;
    }

    bool invalidate(LazyCallGraph::SCC &C, const PreservedAnalyses &PA,
                    CGSCCAnalysisManager::Invalidator &Inv);

  private:
    FunctionAnalysisManager *FAM;
  };

  /// Computes the \c FunctionAnalysisManager and stores it in the result proxy.
  Result run(LazyCallGraph::SCC &C, CGSCCAnalysisManager &AM, LazyCallGraph &);

private:
  friend AnalysisInfoMixin<FunctionAnalysisManagerCGSCCProxy>;

  static AnalysisKey Key;
};

extern template class OuterAnalysisManagerProxy<CGSCCAnalysisManager, Function>;

/// A proxy from a \c CGSCCAnalysisManager to a \c Function.
using CGSCCAnalysisManagerFunctionProxy =
    OuterAnalysisManagerProxy<CGSCCAnalysisManager, Function>;

/// Helper to update the call graph after running a function pass.
///
/// Function passes can only mutate the call graph in specific ways. This
/// routine provides a helper that updates the call graph in those ways
/// including returning whether any changes were made and populating a CG
/// update result struct for the overall CGSCC walk.
LazyCallGraph::SCC &updateCGAndAnalysisManagerForFunctionPass(
    LazyCallGraph &G, LazyCallGraph::SCC &C, LazyCallGraph::Node &N,
    CGSCCAnalysisManager &AM, CGSCCUpdateResult &UR,
    FunctionAnalysisManager &FAM);

/// Helper to update the call graph after running a CGSCC pass.
///
/// CGSCC passes can only mutate the call graph in specific ways. This
/// routine provides a helper that updates the call graph in those ways
/// including returning whether any changes were made and populating a CG
/// update result struct for the overall CGSCC walk.
LazyCallGraph::SCC &updateCGAndAnalysisManagerForCGSCCPass(
    LazyCallGraph &G, LazyCallGraph::SCC &C, LazyCallGraph::Node &N,
    CGSCCAnalysisManager &AM, CGSCCUpdateResult &UR,
    FunctionAnalysisManager &FAM);

/// Adaptor that maps from a SCC to its functions.
///
/// Designed to allow composition of a FunctionPass(Manager) and
/// a CGSCCPassManager. Note that if this pass is constructed with a pointer
/// to a \c CGSCCAnalysisManager it will run the
/// \c FunctionAnalysisManagerCGSCCProxy analysis prior to running the function
/// pass over the SCC to enable a \c FunctionAnalysisManager to be used
/// within this run safely.
class CGSCCToFunctionPassAdaptor
    : public PassInfoMixin<CGSCCToFunctionPassAdaptor> {
public:
  using PassConceptT = detail::PassConcept<Function, FunctionAnalysisManager>;

  explicit CGSCCToFunctionPassAdaptor(std::unique_ptr<PassConceptT> Pass,
                                      bool EagerlyInvalidate, bool NoRerun)
      : Pass(std::move(Pass)), EagerlyInvalidate(EagerlyInvalidate),
        NoRerun(NoRerun) {}

  CGSCCToFunctionPassAdaptor(CGSCCToFunctionPassAdaptor &&Arg)
      : Pass(std::move(Arg.Pass)), EagerlyInvalidate(Arg.EagerlyInvalidate),
        NoRerun(Arg.NoRerun) {}

  friend void swap(CGSCCToFunctionPassAdaptor &LHS,
                   CGSCCToFunctionPassAdaptor &RHS) {
    std::swap(LHS.Pass, RHS.Pass);
  }

  CGSCCToFunctionPassAdaptor &operator=(CGSCCToFunctionPassAdaptor RHS) {
    swap(*this, RHS);
    return *this;
  }

  /// Runs the function pass across every function in the module.
  PreservedAnalyses run(LazyCallGraph::SCC &C, CGSCCAnalysisManager &AM,
                        LazyCallGraph &CG, CGSCCUpdateResult &UR);

  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName) {
    OS << "function";
    if (EagerlyInvalidate || NoRerun) {
      OS << "<";
      if (EagerlyInvalidate)
        OS << "eager-inv";
      if (EagerlyInvalidate && NoRerun)
        OS << ";";
      if (NoRerun)
        OS << "no-rerun";
      OS << ">";
    }
    OS << '(';
    Pass->printPipeline(OS, MapClassName2PassName);
    OS << ')';
  }

  static bool isRequired() { return true; }

private:
  std::unique_ptr<PassConceptT> Pass;
  bool EagerlyInvalidate;
  bool NoRerun;
};

/// A function to deduce a function pass type and wrap it in the
/// templated adaptor.
template <typename FunctionPassT>
CGSCCToFunctionPassAdaptor
createCGSCCToFunctionPassAdaptor(FunctionPassT &&Pass,
                                 bool EagerlyInvalidate = false,
                                 bool NoRerun = false) {
  using PassModelT =
      detail::PassModel<Function, FunctionPassT, FunctionAnalysisManager>;
  // Do not use make_unique, it causes too many template instantiations,
  // causing terrible compile times.
  return CGSCCToFunctionPassAdaptor(
      std::unique_ptr<CGSCCToFunctionPassAdaptor::PassConceptT>(
          new PassModelT(std::forward<FunctionPassT>(Pass))),
      EagerlyInvalidate, NoRerun);
}

// A marker to determine if function passes should be run on a function within a
// CGSCCToFunctionPassAdaptor. This is used to prevent running an expensive
// function pass (manager) on a function multiple times if SCC mutations cause a
// function to be visited multiple times and the function is not modified by
// other SCC passes.
class ShouldNotRunFunctionPassesAnalysis
    : public AnalysisInfoMixin<ShouldNotRunFunctionPassesAnalysis> {
public:
  static AnalysisKey Key;
  struct Result {};

  Result run(Function &F, FunctionAnalysisManager &FAM) { return Result(); }
};

/// A helper that repeats an SCC pass each time an indirect call is refined to
/// a direct call by that pass.
///
/// While the CGSCC pass manager works to re-visit SCCs and RefSCCs as they
/// change shape, we may also want to repeat an SCC pass if it simply refines
/// an indirect call to a direct call, even if doing so does not alter the
/// shape of the graph. Note that this only pertains to direct calls to
/// functions where IPO across the SCC may be able to compute more precise
/// results. For intrinsics, we assume scalar optimizations already can fully
/// reason about them.
///
/// This repetition has the potential to be very large however, as each one
/// might refine a single call site. As a consequence, in practice we use an
/// upper bound on the number of repetitions to limit things.
class DevirtSCCRepeatedPass : public PassInfoMixin<DevirtSCCRepeatedPass> {
public:
  using PassConceptT =
      detail::PassConcept<LazyCallGraph::SCC, CGSCCAnalysisManager,
                          LazyCallGraph &, CGSCCUpdateResult &>;

  explicit DevirtSCCRepeatedPass(std::unique_ptr<PassConceptT> Pass,
                                 int MaxIterations)
      : Pass(std::move(Pass)), MaxIterations(MaxIterations) {}

  /// Runs the wrapped pass up to \c MaxIterations on the SCC, iterating
  /// whenever an indirect call is refined.
  PreservedAnalyses run(LazyCallGraph::SCC &InitialC, CGSCCAnalysisManager &AM,
                        LazyCallGraph &CG, CGSCCUpdateResult &UR);

  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName) {
    OS << "devirt<" << MaxIterations << ">(";
    Pass->printPipeline(OS, MapClassName2PassName);
    OS << ')';
  }

private:
  std::unique_ptr<PassConceptT> Pass;
  int MaxIterations;
};

/// A function to deduce a function pass type and wrap it in the
/// templated adaptor.
template <typename CGSCCPassT>
DevirtSCCRepeatedPass createDevirtSCCRepeatedPass(CGSCCPassT &&Pass,
                                                  int MaxIterations) {
  using PassModelT =
      detail::PassModel<LazyCallGraph::SCC, CGSCCPassT, CGSCCAnalysisManager,
                        LazyCallGraph &, CGSCCUpdateResult &>;
  // Do not use make_unique, it causes too many template instantiations,
  // causing terrible compile times.
  return DevirtSCCRepeatedPass(
      std::unique_ptr<DevirtSCCRepeatedPass::PassConceptT>(
          new PassModelT(std::forward<CGSCCPassT>(Pass))),
      MaxIterations);
}

// Clear out the debug logging macro.
#undef DEBUG_TYPE

} // end namespace llvm

#endif // LLVM_ANALYSIS_CGSCCPASSMANAGER_H
