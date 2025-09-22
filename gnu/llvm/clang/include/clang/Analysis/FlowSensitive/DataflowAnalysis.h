//===- DataflowAnalysis.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines base types and functions for building dataflow analyses
//  that run over Control-Flow Graphs (CFGs).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_DATAFLOWANALYSIS_H
#define LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_DATAFLOWANALYSIS_H

#include <iterator>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "clang/AST/ASTContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/FlowSensitive/AdornedCFG.h"
#include "clang/Analysis/FlowSensitive/DataflowEnvironment.h"
#include "clang/Analysis/FlowSensitive/DataflowLattice.h"
#include "clang/Analysis/FlowSensitive/MatchSwitch.h"
#include "clang/Analysis/FlowSensitive/TypeErasedDataflowAnalysis.h"
#include "clang/Analysis/FlowSensitive/WatchedLiteralsSolver.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"

namespace clang {
namespace dataflow {

/// Base class template for dataflow analyses built on a single lattice type.
///
/// Requirements:
///
///  `Derived` must be derived from a specialization of this class template and
///  must provide the following public members:
///   * `LatticeT initialElement()` - returns a lattice element that models the
///     initial state of a basic block;
///   * `void transfer(const CFGElement &, LatticeT &, Environment &)` - applies
///     the analysis transfer function for a given CFG element and lattice
///     element.
///
///  `Derived` can optionally provide the following members:
///  * `void transferBranch(bool Branch, const Stmt *Stmt, TypeErasedLattice &E,
///                         Environment &Env)` - applies the analysis transfer
///    function for a given edge from a CFG block of a conditional statement.
///
///  `Derived` can optionally override the virtual functions in the
///  `Environment::ValueModel` interface (which is an indirect base class of
///  this class).
///
///  `LatticeT` is a bounded join-semilattice that is used by `Derived` and must
///  provide the following public members:
///   * `LatticeJoinEffect join(const LatticeT &)` - joins the object and the
///     argument by computing their least upper bound, modifies the object if
///     necessary, and returns an effect indicating whether any changes were
///     made to it;
///     FIXME: make it `static LatticeT join(const LatticeT&, const LatticeT&)`
///   * `bool operator==(const LatticeT &) const` - returns true if and only if
///     the object is equal to the argument.
///
/// `LatticeT` can optionally provide the following members:
///  * `LatticeJoinEffect widen(const LatticeT &Previous)` - replaces the
///    lattice element with an  approximation that can reach a fixed point more
///    quickly than iterated application of the transfer function alone. The
///    previous value is provided to inform the choice of widened value. The
///    function must also serve as a comparison operation, by indicating whether
///    the widened value is equivalent to the previous value with the returned
///    `LatticeJoinEffect`.
template <typename Derived, typename LatticeT>
class DataflowAnalysis : public TypeErasedDataflowAnalysis {
public:
  /// Bounded join-semilattice that is used in the analysis.
  using Lattice = LatticeT;

  explicit DataflowAnalysis(ASTContext &Context) : Context(Context) {}

  explicit DataflowAnalysis(ASTContext &Context,
                            DataflowAnalysisOptions Options)
      : TypeErasedDataflowAnalysis(Options), Context(Context) {}

  ASTContext &getASTContext() final { return Context; }

  TypeErasedLattice typeErasedInitialElement() final {
    return {static_cast<Derived *>(this)->initialElement()};
  }

  TypeErasedLattice joinTypeErased(const TypeErasedLattice &E1,
                                   const TypeErasedLattice &E2) final {
    // FIXME: change the signature of join() to avoid copying here.
    Lattice L1 = llvm::any_cast<const Lattice &>(E1.Value);
    const Lattice &L2 = llvm::any_cast<const Lattice &>(E2.Value);
    L1.join(L2);
    return {std::move(L1)};
  }

  LatticeJoinEffect widenTypeErased(TypeErasedLattice &Current,
                                    const TypeErasedLattice &Previous) final {
    Lattice &C = llvm::any_cast<Lattice &>(Current.Value);
    const Lattice &P = llvm::any_cast<const Lattice &>(Previous.Value);
    return widenInternal(Rank0{}, C, P);
  }

  bool isEqualTypeErased(const TypeErasedLattice &E1,
                         const TypeErasedLattice &E2) final {
    const Lattice &L1 = llvm::any_cast<const Lattice &>(E1.Value);
    const Lattice &L2 = llvm::any_cast<const Lattice &>(E2.Value);
    return L1 == L2;
  }

  void transferTypeErased(const CFGElement &Element, TypeErasedLattice &E,
                          Environment &Env) final {
    Lattice &L = llvm::any_cast<Lattice &>(E.Value);
    static_cast<Derived *>(this)->transfer(Element, L, Env);
  }

  void transferBranchTypeErased(bool Branch, const Stmt *Stmt,
                                TypeErasedLattice &E, Environment &Env) final {
    transferBranchInternal(Rank0{}, *static_cast<Derived *>(this), Branch, Stmt,
                           E, Env);
  }

private:
  // These `Rank` structs are used for template metaprogramming to choose
  // between overloads.
  struct Rank1 {};
  struct Rank0 : Rank1 {};

  // The first-choice implementation: use `widen` when it is available.
  template <typename T>
  static auto widenInternal(Rank0, T &Current, const T &Prev)
      -> decltype(Current.widen(Prev)) {
    return Current.widen(Prev);
  }

  // The second-choice implementation: `widen` is unavailable. Widening is
  // merged with equality checking, so when widening is unimplemented, we
  // default to equality checking.
  static LatticeJoinEffect widenInternal(Rank1, const Lattice &Current,
                                         const Lattice &Prev) {
    return Prev == Current ? LatticeJoinEffect::Unchanged
                           : LatticeJoinEffect::Changed;
  }

  // The first-choice implementation: `transferBranch` is implemented.
  template <typename Analysis>
  static auto transferBranchInternal(Rank0, Analysis &A, bool Branch,
                                     const Stmt *Stmt, TypeErasedLattice &L,
                                     Environment &Env)
      -> std::void_t<decltype(A.transferBranch(
          Branch, Stmt, std::declval<LatticeT &>(), Env))> {
    A.transferBranch(Branch, Stmt, llvm::any_cast<Lattice &>(L.Value), Env);
  }

  // The second-choice implementation: `transferBranch` is unimplemented. No-op.
  template <typename Analysis>
  static void transferBranchInternal(Rank1, Analysis &A, bool, const Stmt *,
                                     TypeErasedLattice &, Environment &) {}

  ASTContext &Context;
};

// Model of the program at a given program point.
template <typename LatticeT> struct DataflowAnalysisState {
  // Model of a program property.
  LatticeT Lattice;

  // Model of the state of the program (store and heap).
  Environment Env;
};

/// A callback to be called with the state before or after visiting a CFG
/// element.
template <typename AnalysisT>
using CFGEltCallback = std::function<void(
    const CFGElement &,
    const DataflowAnalysisState<typename AnalysisT::Lattice> &)>;

/// A pair of callbacks to be called with the state before and after visiting a
/// CFG element.
/// Either or both of the callbacks may be null.
template <typename AnalysisT> struct CFGEltCallbacks {
  CFGEltCallback<AnalysisT> Before;
  CFGEltCallback<AnalysisT> After;
};

/// A callback for performing diagnosis on a CFG element, called with the state
/// before or after visiting that CFG element. Returns a list of diagnostics
/// to emit (if any).
template <typename AnalysisT, typename Diagnostic>
using DiagnosisCallback = llvm::function_ref<llvm::SmallVector<Diagnostic>(
    const CFGElement &, ASTContext &,
    const TransferStateForDiagnostics<typename AnalysisT::Lattice> &)>;

/// A pair of callbacks for performing diagnosis on a CFG element, called with
/// the state before and after visiting that CFG element.
/// Either or both of the callbacks may be null.
template <typename AnalysisT, typename Diagnostic> struct DiagnosisCallbacks {
  DiagnosisCallback<AnalysisT, Diagnostic> Before;
  DiagnosisCallback<AnalysisT, Diagnostic> After;
};

/// Default for the maximum number of SAT solver iterations during analysis.
inline constexpr std::int64_t kDefaultMaxSATIterations = 1'000'000'000;

/// Default for the maximum number of block visits during analysis.
inline constexpr std::int32_t kDefaultMaxBlockVisits = 20'000;

/// Performs dataflow analysis and returns a mapping from basic block IDs to
/// dataflow analysis states that model the respective basic blocks. The
/// returned vector, if any, will have the same size as the number of CFG
/// blocks, with indices corresponding to basic block IDs. Returns an error if
/// the dataflow analysis cannot be performed successfully. Otherwise, calls
/// `PostAnalysisCallbacks` on each CFG element with the final analysis results
/// before and after that program point.
///
/// `MaxBlockVisits` caps the number of block visits during analysis. See
/// `runTypeErasedDataflowAnalysis` for a full description. The default value is
/// essentially arbitrary -- large enough to accommodate what seems like any
/// reasonable CFG, but still small enough to limit the cost of hitting the
/// limit.
template <typename AnalysisT>
llvm::Expected<std::vector<
    std::optional<DataflowAnalysisState<typename AnalysisT::Lattice>>>>
runDataflowAnalysis(const AdornedCFG &ACFG, AnalysisT &Analysis,
                    const Environment &InitEnv,
                    CFGEltCallbacks<AnalysisT> PostAnalysisCallbacks,
                    std::int32_t MaxBlockVisits = kDefaultMaxBlockVisits) {
  CFGEltCallbacksTypeErased TypeErasedCallbacks;
  if (PostAnalysisCallbacks.Before) {
    TypeErasedCallbacks.Before =
        [&PostAnalysisCallbacks](const CFGElement &Element,
                                 const TypeErasedDataflowAnalysisState &State) {
          auto *Lattice =
              llvm::any_cast<typename AnalysisT::Lattice>(&State.Lattice.Value);
          // FIXME: we should not be copying the environment here!
          // Ultimately the `CFGEltCallback` only gets a const reference anyway.
          PostAnalysisCallbacks.Before(
              Element, DataflowAnalysisState<typename AnalysisT::Lattice>{
                           *Lattice, State.Env.fork()});
        };
  }
  if (PostAnalysisCallbacks.After) {
    TypeErasedCallbacks.After =
        [&PostAnalysisCallbacks](const CFGElement &Element,
                                 const TypeErasedDataflowAnalysisState &State) {
          auto *Lattice =
              llvm::any_cast<typename AnalysisT::Lattice>(&State.Lattice.Value);
          // FIXME: we should not be copying the environment here!
          // Ultimately the `CFGEltCallback` only gets a const reference anyway.
          PostAnalysisCallbacks.After(
              Element, DataflowAnalysisState<typename AnalysisT::Lattice>{
                           *Lattice, State.Env.fork()});
        };
  }

  auto TypeErasedBlockStates = runTypeErasedDataflowAnalysis(
      ACFG, Analysis, InitEnv, TypeErasedCallbacks, MaxBlockVisits);
  if (!TypeErasedBlockStates)
    return TypeErasedBlockStates.takeError();

  std::vector<std::optional<DataflowAnalysisState<typename AnalysisT::Lattice>>>
      BlockStates;
  BlockStates.reserve(TypeErasedBlockStates->size());

  llvm::transform(
      std::move(*TypeErasedBlockStates), std::back_inserter(BlockStates),
      [](auto &OptState) {
        return llvm::transformOptional(
            std::move(OptState), [](TypeErasedDataflowAnalysisState &&State) {
              return DataflowAnalysisState<typename AnalysisT::Lattice>{
                  llvm::any_cast<typename AnalysisT::Lattice>(
                      std::move(State.Lattice.Value)),
                  std::move(State.Env)};
            });
      });
  return std::move(BlockStates);
}

/// Overload that takes only one post-analysis callback, which is run on the
/// state after visiting the `CFGElement`. This is provided for backwards
/// compatibility; new callers should call the overload taking `CFGEltCallbacks`
/// instead.
template <typename AnalysisT>
llvm::Expected<std::vector<
    std::optional<DataflowAnalysisState<typename AnalysisT::Lattice>>>>
runDataflowAnalysis(
    const AdornedCFG &ACFG, AnalysisT &Analysis, const Environment &InitEnv,
    CFGEltCallback<AnalysisT> PostAnalysisCallbackAfterElt = nullptr,
    std::int32_t MaxBlockVisits = kDefaultMaxBlockVisits) {
  return runDataflowAnalysis(ACFG, Analysis, InitEnv,
                             {nullptr, PostAnalysisCallbackAfterElt},
                             MaxBlockVisits);
}

// Create an analysis class that is derived from `DataflowAnalysis`. This is an
// SFINAE adapter that allows us to call two different variants of constructor
// (either with or without the optional `Environment` parameter).
// FIXME: Make all classes derived from `DataflowAnalysis` take an `Environment`
// parameter in their constructor so that we can get rid of this abomination.
template <typename AnalysisT>
auto createAnalysis(ASTContext &ASTCtx, Environment &Env)
    -> decltype(AnalysisT(ASTCtx, Env)) {
  return AnalysisT(ASTCtx, Env);
}
template <typename AnalysisT>
auto createAnalysis(ASTContext &ASTCtx, Environment &Env)
    -> decltype(AnalysisT(ASTCtx)) {
  return AnalysisT(ASTCtx);
}

/// Runs a dataflow analysis over the given function and then runs `Diagnoser`
/// over the results. Returns a list of diagnostics for `FuncDecl` or an
/// error. Currently, errors can occur (at least) because the analysis requires
/// too many iterations over the CFG or the SAT solver times out.
///
/// The default value of `MaxSATIterations` was chosen based on the following
/// observations:
/// - Non-pathological calls to the solver typically require only a few hundred
///   iterations.
/// - This limit is still low enough to keep runtimes acceptable (on typical
///   machines) in cases where we hit the limit.
///
/// `MaxBlockVisits` caps the number of block visits during analysis. See
/// `runDataflowAnalysis` for a full description and explanation of the default
/// value.
template <typename AnalysisT, typename Diagnostic>
llvm::Expected<llvm::SmallVector<Diagnostic>>
diagnoseFunction(const FunctionDecl &FuncDecl, ASTContext &ASTCtx,
                 DiagnosisCallbacks<AnalysisT, Diagnostic> Diagnoser,
                 std::int64_t MaxSATIterations = kDefaultMaxSATIterations,
                 std::int32_t MaxBlockVisits = kDefaultMaxBlockVisits) {
  llvm::Expected<AdornedCFG> Context = AdornedCFG::build(FuncDecl);
  if (!Context)
    return Context.takeError();

  auto Solver = std::make_unique<WatchedLiteralsSolver>(MaxSATIterations);
  DataflowAnalysisContext AnalysisContext(*Solver);
  Environment Env(AnalysisContext, FuncDecl);
  AnalysisT Analysis = createAnalysis<AnalysisT>(ASTCtx, Env);
  llvm::SmallVector<Diagnostic> Diagnostics;
  CFGEltCallbacksTypeErased PostAnalysisCallbacks;
  if (Diagnoser.Before) {
    PostAnalysisCallbacks.Before =
        [&ASTCtx, &Diagnoser,
         &Diagnostics](const CFGElement &Elt,
                       const TypeErasedDataflowAnalysisState &State) mutable {
          auto EltDiagnostics = Diagnoser.Before(
              Elt, ASTCtx,
              TransferStateForDiagnostics<typename AnalysisT::Lattice>(
                  llvm::any_cast<const typename AnalysisT::Lattice &>(
                      State.Lattice.Value),
                  State.Env));
          llvm::move(EltDiagnostics, std::back_inserter(Diagnostics));
        };
  }
  if (Diagnoser.After) {
    PostAnalysisCallbacks.After =
        [&ASTCtx, &Diagnoser,
         &Diagnostics](const CFGElement &Elt,
                       const TypeErasedDataflowAnalysisState &State) mutable {
          auto EltDiagnostics = Diagnoser.After(
              Elt, ASTCtx,
              TransferStateForDiagnostics<typename AnalysisT::Lattice>(
                  llvm::any_cast<const typename AnalysisT::Lattice &>(
                      State.Lattice.Value),
                  State.Env));
          llvm::move(EltDiagnostics, std::back_inserter(Diagnostics));
        };
  }
  if (llvm::Error Err =
          runTypeErasedDataflowAnalysis(*Context, Analysis, Env,
                                        PostAnalysisCallbacks, MaxBlockVisits)
              .takeError())
    return std::move(Err);

  if (Solver->reachedLimit())
    return llvm::createStringError(llvm::errc::interrupted,
                                   "SAT solver timed out");

  return Diagnostics;
}

/// Overload that takes only one diagnosis callback, which is run on the state
/// after visiting the `CFGElement`. This is provided for backwards
/// compatibility; new callers should call the overload taking
/// `DiagnosisCallbacks` instead.
template <typename AnalysisT, typename Diagnostic>
llvm::Expected<llvm::SmallVector<Diagnostic>>
diagnoseFunction(const FunctionDecl &FuncDecl, ASTContext &ASTCtx,
                 DiagnosisCallback<AnalysisT, Diagnostic> Diagnoser,
                 std::int64_t MaxSATIterations = kDefaultMaxSATIterations,
                 std::int32_t MaxBlockVisits = kDefaultMaxBlockVisits) {
  DiagnosisCallbacks<AnalysisT, Diagnostic> Callbacks = {nullptr, Diagnoser};
  return diagnoseFunction(FuncDecl, ASTCtx, Callbacks, MaxSATIterations,
                          MaxBlockVisits);
}

/// Abstract base class for dataflow "models": reusable analysis components that
/// model a particular aspect of program semantics in the `Environment`. For
/// example, a model may capture a type and its related functions.
class DataflowModel : public Environment::ValueModel {
public:
  /// Return value indicates whether the model processed the `Element`.
  virtual bool transfer(const CFGElement &Element, Environment &Env) = 0;
};

} // namespace dataflow
} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_FLOWSENSITIVE_DATAFLOWANALYSIS_H
