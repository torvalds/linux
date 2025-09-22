//===- PassManager.h - Pass management infrastructure -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This header defines various interfaces for pass management in LLVM. There
/// is no "pass" interface in LLVM per se. Instead, an instance of any class
/// which supports a method to 'run' it over a unit of IR can be used as
/// a pass. A pass manager is generally a tool to collect a sequence of passes
/// which run over a particular IR construct, and run each of them in sequence
/// over each such construct in the containing IR construct. As there is no
/// containing IR construct for a Module, a manager for passes over modules
/// forms the base case which runs its managed passes in sequence over the
/// single module provided.
///
/// The core IR library provides managers for running passes over
/// modules and functions.
///
/// * FunctionPassManager can run over a Module, runs each pass over
///   a Function.
/// * ModulePassManager must be directly run, runs each pass over the Module.
///
/// Note that the implementations of the pass managers use concept-based
/// polymorphism as outlined in the "Value Semantics and Concept-based
/// Polymorphism" talk (or its abbreviated sibling "Inheritance Is The Base
/// Class of Evil") by Sean Parent:
/// * http://github.com/sean-parent/sean-parent.github.com/wiki/Papers-and-Presentations
/// * http://www.youtube.com/watch?v=_BpMYeUFXv8
/// * http://channel9.msdn.com/Events/GoingNative/2013/Inheritance-Is-The-Base-Class-of-Evil
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_PASSMANAGER_H
#define LLVM_IR_PASSMANAGER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/IR/Analysis.h"
#include "llvm/IR/PassManagerInternal.h"
#include "llvm/Support/TypeName.h"
#include <cassert>
#include <cstring>
#include <iterator>
#include <list>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace llvm {

class Function;
class Module;

// Forward declare the analysis manager template.
template <typename IRUnitT, typename... ExtraArgTs> class AnalysisManager;

/// A CRTP mix-in to automatically provide informational APIs needed for
/// passes.
///
/// This provides some boilerplate for types that are passes.
template <typename DerivedT> struct PassInfoMixin {
  /// Gets the name of the pass we are mixed into.
  static StringRef name() {
    static_assert(std::is_base_of<PassInfoMixin, DerivedT>::value,
                  "Must pass the derived type as the template argument!");
    StringRef Name = getTypeName<DerivedT>();
    Name.consume_front("llvm::");
    return Name;
  }

  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName) {
    StringRef ClassName = DerivedT::name();
    auto PassName = MapClassName2PassName(ClassName);
    OS << PassName;
  }
};

/// A CRTP mix-in that provides informational APIs needed for analysis passes.
///
/// This provides some boilerplate for types that are analysis passes. It
/// automatically mixes in \c PassInfoMixin.
template <typename DerivedT>
struct AnalysisInfoMixin : PassInfoMixin<DerivedT> {
  /// Returns an opaque, unique ID for this analysis type.
  ///
  /// This ID is a pointer type that is guaranteed to be 8-byte aligned and thus
  /// suitable for use in sets, maps, and other data structures that use the low
  /// bits of pointers.
  ///
  /// Note that this requires the derived type provide a static \c AnalysisKey
  /// member called \c Key.
  ///
  /// FIXME: The only reason the mixin type itself can't declare the Key value
  /// is that some compilers cannot correctly unique a templated static variable
  /// so it has the same addresses in each instantiation. The only currently
  /// known platform with this limitation is Windows DLL builds, specifically
  /// building each part of LLVM as a DLL. If we ever remove that build
  /// configuration, this mixin can provide the static key as well.
  static AnalysisKey *ID() {
    static_assert(std::is_base_of<AnalysisInfoMixin, DerivedT>::value,
                  "Must pass the derived type as the template argument!");
    return &DerivedT::Key;
  }
};

namespace detail {

/// Actual unpacker of extra arguments in getAnalysisResult,
/// passes only those tuple arguments that are mentioned in index_sequence.
template <typename PassT, typename IRUnitT, typename AnalysisManagerT,
          typename... ArgTs, size_t... Ns>
typename PassT::Result
getAnalysisResultUnpackTuple(AnalysisManagerT &AM, IRUnitT &IR,
                             std::tuple<ArgTs...> Args,
                             std::index_sequence<Ns...>) {
  (void)Args;
  return AM.template getResult<PassT>(IR, std::get<Ns>(Args)...);
}

/// Helper for *partial* unpacking of extra arguments in getAnalysisResult.
///
/// Arguments passed in tuple come from PassManager, so they might have extra
/// arguments after those AnalysisManager's ExtraArgTs ones that we need to
/// pass to getResult.
template <typename PassT, typename IRUnitT, typename... AnalysisArgTs,
          typename... MainArgTs>
typename PassT::Result
getAnalysisResult(AnalysisManager<IRUnitT, AnalysisArgTs...> &AM, IRUnitT &IR,
                  std::tuple<MainArgTs...> Args) {
  return (getAnalysisResultUnpackTuple<
          PassT, IRUnitT>)(AM, IR, Args,
                           std::index_sequence_for<AnalysisArgTs...>{});
}

} // namespace detail

/// Manages a sequence of passes over a particular unit of IR.
///
/// A pass manager contains a sequence of passes to run over a particular unit
/// of IR (e.g. Functions, Modules). It is itself a valid pass over that unit of
/// IR, and when run over some given IR will run each of its contained passes in
/// sequence. Pass managers are the primary and most basic building block of a
/// pass pipeline.
///
/// When you run a pass manager, you provide an \c AnalysisManager<IRUnitT>
/// argument. The pass manager will propagate that analysis manager to each
/// pass it runs, and will call the analysis manager's invalidation routine with
/// the PreservedAnalyses of each pass it runs.
template <typename IRUnitT,
          typename AnalysisManagerT = AnalysisManager<IRUnitT>,
          typename... ExtraArgTs>
class PassManager : public PassInfoMixin<
                        PassManager<IRUnitT, AnalysisManagerT, ExtraArgTs...>> {
public:
  /// Construct a pass manager.
  explicit PassManager() = default;

  // FIXME: These are equivalent to the default move constructor/move
  // assignment. However, using = default triggers linker errors due to the
  // explicit instantiations below. Find away to use the default and remove the
  // duplicated code here.
  PassManager(PassManager &&Arg) : Passes(std::move(Arg.Passes)) {}

  PassManager &operator=(PassManager &&RHS) {
    Passes = std::move(RHS.Passes);
    return *this;
  }

  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName) {
    for (unsigned Idx = 0, Size = Passes.size(); Idx != Size; ++Idx) {
      auto *P = Passes[Idx].get();
      P->printPipeline(OS, MapClassName2PassName);
      if (Idx + 1 < Size)
        OS << ',';
    }
  }

  /// Run all of the passes in this manager over the given unit of IR.
  /// ExtraArgs are passed to each pass.
  PreservedAnalyses run(IRUnitT &IR, AnalysisManagerT &AM,
                        ExtraArgTs... ExtraArgs);

  template <typename PassT>
  LLVM_ATTRIBUTE_MINSIZE std::enable_if_t<!std::is_same_v<PassT, PassManager>>
  addPass(PassT &&Pass) {
    using PassModelT =
        detail::PassModel<IRUnitT, PassT, AnalysisManagerT, ExtraArgTs...>;
    // Do not use make_unique or emplace_back, they cause too many template
    // instantiations, causing terrible compile times.
    Passes.push_back(std::unique_ptr<PassConceptT>(
        new PassModelT(std::forward<PassT>(Pass))));
  }

  /// When adding a pass manager pass that has the same type as this pass
  /// manager, simply move the passes over. This is because we don't have
  /// use cases rely on executing nested pass managers. Doing this could
  /// reduce implementation complexity and avoid potential invalidation
  /// issues that may happen with nested pass managers of the same type.
  template <typename PassT>
  LLVM_ATTRIBUTE_MINSIZE std::enable_if_t<std::is_same_v<PassT, PassManager>>
  addPass(PassT &&Pass) {
    for (auto &P : Pass.Passes)
      Passes.push_back(std::move(P));
  }

  /// Returns if the pass manager contains any passes.
  bool isEmpty() const { return Passes.empty(); }

  static bool isRequired() { return true; }

protected:
  using PassConceptT =
      detail::PassConcept<IRUnitT, AnalysisManagerT, ExtraArgTs...>;

  std::vector<std::unique_ptr<PassConceptT>> Passes;
};

template <typename IRUnitT>
void printIRUnitNameForStackTrace(raw_ostream &OS, const IRUnitT &IR);

template <>
void printIRUnitNameForStackTrace<Module>(raw_ostream &OS, const Module &IR);

extern template class PassManager<Module>;

/// Convenience typedef for a pass manager over modules.
using ModulePassManager = PassManager<Module>;

template <>
void printIRUnitNameForStackTrace<Function>(raw_ostream &OS,
                                            const Function &IR);

extern template class PassManager<Function>;

/// Convenience typedef for a pass manager over functions.
using FunctionPassManager = PassManager<Function>;

/// A container for analyses that lazily runs them and caches their
/// results.
///
/// This class can manage analyses for any IR unit where the address of the IR
/// unit sufficies as its identity.
template <typename IRUnitT, typename... ExtraArgTs> class AnalysisManager {
public:
  class Invalidator;

private:
  // Now that we've defined our invalidator, we can define the concept types.
  using ResultConceptT = detail::AnalysisResultConcept<IRUnitT, Invalidator>;
  using PassConceptT =
      detail::AnalysisPassConcept<IRUnitT, Invalidator, ExtraArgTs...>;

  /// List of analysis pass IDs and associated concept pointers.
  ///
  /// Requires iterators to be valid across appending new entries and arbitrary
  /// erases. Provides the analysis ID to enable finding iterators to a given
  /// entry in maps below, and provides the storage for the actual result
  /// concept.
  using AnalysisResultListT =
      std::list<std::pair<AnalysisKey *, std::unique_ptr<ResultConceptT>>>;

  /// Map type from IRUnitT pointer to our custom list type.
  using AnalysisResultListMapT = DenseMap<IRUnitT *, AnalysisResultListT>;

  /// Map type from a pair of analysis ID and IRUnitT pointer to an
  /// iterator into a particular result list (which is where the actual analysis
  /// result is stored).
  using AnalysisResultMapT =
      DenseMap<std::pair<AnalysisKey *, IRUnitT *>,
               typename AnalysisResultListT::iterator>;

public:
  /// API to communicate dependencies between analyses during invalidation.
  ///
  /// When an analysis result embeds handles to other analysis results, it
  /// needs to be invalidated both when its own information isn't preserved and
  /// when any of its embedded analysis results end up invalidated. We pass an
  /// \c Invalidator object as an argument to \c invalidate() in order to let
  /// the analysis results themselves define the dependency graph on the fly.
  /// This lets us avoid building an explicit representation of the
  /// dependencies between analysis results.
  class Invalidator {
  public:
    /// Trigger the invalidation of some other analysis pass if not already
    /// handled and return whether it was in fact invalidated.
    ///
    /// This is expected to be called from within a given analysis result's \c
    /// invalidate method to trigger a depth-first walk of all inter-analysis
    /// dependencies. The same \p IR unit and \p PA passed to that result's \c
    /// invalidate method should in turn be provided to this routine.
    ///
    /// The first time this is called for a given analysis pass, it will call
    /// the corresponding result's \c invalidate method.  Subsequent calls will
    /// use a cache of the results of that initial call.  It is an error to form
    /// cyclic dependencies between analysis results.
    ///
    /// This returns true if the given analysis's result is invalid. Any
    /// dependecies on it will become invalid as a result.
    template <typename PassT>
    bool invalidate(IRUnitT &IR, const PreservedAnalyses &PA) {
      using ResultModelT =
          detail::AnalysisResultModel<IRUnitT, PassT, typename PassT::Result,
                                      Invalidator>;

      return invalidateImpl<ResultModelT>(PassT::ID(), IR, PA);
    }

    /// A type-erased variant of the above invalidate method with the same core
    /// API other than passing an analysis ID rather than an analysis type
    /// parameter.
    ///
    /// This is sadly less efficient than the above routine, which leverages
    /// the type parameter to avoid the type erasure overhead.
    bool invalidate(AnalysisKey *ID, IRUnitT &IR, const PreservedAnalyses &PA) {
      return invalidateImpl<>(ID, IR, PA);
    }

  private:
    friend class AnalysisManager;

    template <typename ResultT = ResultConceptT>
    bool invalidateImpl(AnalysisKey *ID, IRUnitT &IR,
                        const PreservedAnalyses &PA) {
      // If we've already visited this pass, return true if it was invalidated
      // and false otherwise.
      auto IMapI = IsResultInvalidated.find(ID);
      if (IMapI != IsResultInvalidated.end())
        return IMapI->second;

      // Otherwise look up the result object.
      auto RI = Results.find({ID, &IR});
      assert(RI != Results.end() &&
             "Trying to invalidate a dependent result that isn't in the "
             "manager's cache is always an error, likely due to a stale result "
             "handle!");

      auto &Result = static_cast<ResultT &>(*RI->second->second);

      // Insert into the map whether the result should be invalidated and return
      // that. Note that we cannot reuse IMapI and must do a fresh insert here,
      // as calling invalidate could (recursively) insert things into the map,
      // making any iterator or reference invalid.
      bool Inserted;
      std::tie(IMapI, Inserted) =
          IsResultInvalidated.insert({ID, Result.invalidate(IR, PA, *this)});
      (void)Inserted;
      assert(Inserted && "Should not have already inserted this ID, likely "
                         "indicates a dependency cycle!");
      return IMapI->second;
    }

    Invalidator(SmallDenseMap<AnalysisKey *, bool, 8> &IsResultInvalidated,
                const AnalysisResultMapT &Results)
        : IsResultInvalidated(IsResultInvalidated), Results(Results) {}

    SmallDenseMap<AnalysisKey *, bool, 8> &IsResultInvalidated;
    const AnalysisResultMapT &Results;
  };

  /// Construct an empty analysis manager.
  AnalysisManager();
  AnalysisManager(AnalysisManager &&);
  AnalysisManager &operator=(AnalysisManager &&);

  /// Returns true if the analysis manager has an empty results cache.
  bool empty() const {
    assert(AnalysisResults.empty() == AnalysisResultLists.empty() &&
           "The storage and index of analysis results disagree on how many "
           "there are!");
    return AnalysisResults.empty();
  }

  /// Clear any cached analysis results for a single unit of IR.
  ///
  /// This doesn't invalidate, but instead simply deletes, the relevant results.
  /// It is useful when the IR is being removed and we want to clear out all the
  /// memory pinned for it.
  void clear(IRUnitT &IR, llvm::StringRef Name);

  /// Clear all analysis results cached by this AnalysisManager.
  ///
  /// Like \c clear(IRUnitT&), this doesn't invalidate the results; it simply
  /// deletes them.  This lets you clean up the AnalysisManager when the set of
  /// IR units itself has potentially changed, and thus we can't even look up a
  /// a result and invalidate/clear it directly.
  void clear() {
    AnalysisResults.clear();
    AnalysisResultLists.clear();
  }

  /// Get the result of an analysis pass for a given IR unit.
  ///
  /// Runs the analysis if a cached result is not available.
  template <typename PassT>
  typename PassT::Result &getResult(IRUnitT &IR, ExtraArgTs... ExtraArgs) {
    assert(AnalysisPasses.count(PassT::ID()) &&
           "This analysis pass was not registered prior to being queried");
    ResultConceptT &ResultConcept =
        getResultImpl(PassT::ID(), IR, ExtraArgs...);

    using ResultModelT =
        detail::AnalysisResultModel<IRUnitT, PassT, typename PassT::Result,
                                    Invalidator>;

    return static_cast<ResultModelT &>(ResultConcept).Result;
  }

  /// Get the cached result of an analysis pass for a given IR unit.
  ///
  /// This method never runs the analysis.
  ///
  /// \returns null if there is no cached result.
  template <typename PassT>
  typename PassT::Result *getCachedResult(IRUnitT &IR) const {
    assert(AnalysisPasses.count(PassT::ID()) &&
           "This analysis pass was not registered prior to being queried");

    ResultConceptT *ResultConcept = getCachedResultImpl(PassT::ID(), IR);
    if (!ResultConcept)
      return nullptr;

    using ResultModelT =
        detail::AnalysisResultModel<IRUnitT, PassT, typename PassT::Result,
                                    Invalidator>;

    return &static_cast<ResultModelT *>(ResultConcept)->Result;
  }

  /// Verify that the given Result cannot be invalidated, assert otherwise.
  template <typename PassT>
  void verifyNotInvalidated(IRUnitT &IR, typename PassT::Result *Result) const {
    PreservedAnalyses PA = PreservedAnalyses::none();
    SmallDenseMap<AnalysisKey *, bool, 8> IsResultInvalidated;
    Invalidator Inv(IsResultInvalidated, AnalysisResults);
    assert(!Result->invalidate(IR, PA, Inv) &&
           "Cached result cannot be invalidated");
  }

  /// Register an analysis pass with the manager.
  ///
  /// The parameter is a callable whose result is an analysis pass. This allows
  /// passing in a lambda to construct the analysis.
  ///
  /// The analysis type to register is the type returned by calling the \c
  /// PassBuilder argument. If that type has already been registered, then the
  /// argument will not be called and this function will return false.
  /// Otherwise, we register the analysis returned by calling \c PassBuilder(),
  /// and this function returns true.
  ///
  /// (Note: Although the return value of this function indicates whether or not
  /// an analysis was previously registered, there intentionally isn't a way to
  /// query this directly.  Instead, you should just register all the analyses
  /// you might want and let this class run them lazily.  This idiom lets us
  /// minimize the number of times we have to look up analyses in our
  /// hashtable.)
  template <typename PassBuilderT>
  bool registerPass(PassBuilderT &&PassBuilder) {
    using PassT = decltype(PassBuilder());
    using PassModelT =
        detail::AnalysisPassModel<IRUnitT, PassT, Invalidator, ExtraArgTs...>;

    auto &PassPtr = AnalysisPasses[PassT::ID()];
    if (PassPtr)
      // Already registered this pass type!
      return false;

    // Construct a new model around the instance returned by the builder.
    PassPtr.reset(new PassModelT(PassBuilder()));
    return true;
  }

  /// Invalidate cached analyses for an IR unit.
  ///
  /// Walk through all of the analyses pertaining to this unit of IR and
  /// invalidate them, unless they are preserved by the PreservedAnalyses set.
  void invalidate(IRUnitT &IR, const PreservedAnalyses &PA);

private:
  /// Look up a registered analysis pass.
  PassConceptT &lookUpPass(AnalysisKey *ID) {
    typename AnalysisPassMapT::iterator PI = AnalysisPasses.find(ID);
    assert(PI != AnalysisPasses.end() &&
           "Analysis passes must be registered prior to being queried!");
    return *PI->second;
  }

  /// Look up a registered analysis pass.
  const PassConceptT &lookUpPass(AnalysisKey *ID) const {
    typename AnalysisPassMapT::const_iterator PI = AnalysisPasses.find(ID);
    assert(PI != AnalysisPasses.end() &&
           "Analysis passes must be registered prior to being queried!");
    return *PI->second;
  }

  /// Get an analysis result, running the pass if necessary.
  ResultConceptT &getResultImpl(AnalysisKey *ID, IRUnitT &IR,
                                ExtraArgTs... ExtraArgs);

  /// Get a cached analysis result or return null.
  ResultConceptT *getCachedResultImpl(AnalysisKey *ID, IRUnitT &IR) const {
    typename AnalysisResultMapT::const_iterator RI =
        AnalysisResults.find({ID, &IR});
    return RI == AnalysisResults.end() ? nullptr : &*RI->second->second;
  }

  /// Map type from analysis pass ID to pass concept pointer.
  using AnalysisPassMapT =
      DenseMap<AnalysisKey *, std::unique_ptr<PassConceptT>>;

  /// Collection of analysis passes, indexed by ID.
  AnalysisPassMapT AnalysisPasses;

  /// Map from IR unit to a list of analysis results.
  ///
  /// Provides linear time removal of all analysis results for a IR unit and
  /// the ultimate storage for a particular cached analysis result.
  AnalysisResultListMapT AnalysisResultLists;

  /// Map from an analysis ID and IR unit to a particular cached
  /// analysis result.
  AnalysisResultMapT AnalysisResults;
};

extern template class AnalysisManager<Module>;

/// Convenience typedef for the Module analysis manager.
using ModuleAnalysisManager = AnalysisManager<Module>;

extern template class AnalysisManager<Function>;

/// Convenience typedef for the Function analysis manager.
using FunctionAnalysisManager = AnalysisManager<Function>;

/// An analysis over an "outer" IR unit that provides access to an
/// analysis manager over an "inner" IR unit.  The inner unit must be contained
/// in the outer unit.
///
/// For example, InnerAnalysisManagerProxy<FunctionAnalysisManager, Module> is
/// an analysis over Modules (the "outer" unit) that provides access to a
/// Function analysis manager.  The FunctionAnalysisManager is the "inner"
/// manager being proxied, and Functions are the "inner" unit.  The inner/outer
/// relationship is valid because each Function is contained in one Module.
///
/// If you're (transitively) within a pass manager for an IR unit U that
/// contains IR unit V, you should never use an analysis manager over V, except
/// via one of these proxies.
///
/// Note that the proxy's result is a move-only RAII object.  The validity of
/// the analyses in the inner analysis manager is tied to its lifetime.
template <typename AnalysisManagerT, typename IRUnitT, typename... ExtraArgTs>
class InnerAnalysisManagerProxy
    : public AnalysisInfoMixin<
          InnerAnalysisManagerProxy<AnalysisManagerT, IRUnitT>> {
public:
  class Result {
  public:
    explicit Result(AnalysisManagerT &InnerAM) : InnerAM(&InnerAM) {}

    Result(Result &&Arg) : InnerAM(std::move(Arg.InnerAM)) {
      // We have to null out the analysis manager in the moved-from state
      // because we are taking ownership of the responsibilty to clear the
      // analysis state.
      Arg.InnerAM = nullptr;
    }

    ~Result() {
      // InnerAM is cleared in a moved from state where there is nothing to do.
      if (!InnerAM)
        return;

      // Clear out the analysis manager if we're being destroyed -- it means we
      // didn't even see an invalidate call when we got invalidated.
      InnerAM->clear();
    }

    Result &operator=(Result &&RHS) {
      InnerAM = RHS.InnerAM;
      // We have to null out the analysis manager in the moved-from state
      // because we are taking ownership of the responsibilty to clear the
      // analysis state.
      RHS.InnerAM = nullptr;
      return *this;
    }

    /// Accessor for the analysis manager.
    AnalysisManagerT &getManager() { return *InnerAM; }

    /// Handler for invalidation of the outer IR unit, \c IRUnitT.
    ///
    /// If the proxy analysis itself is not preserved, we assume that the set of
    /// inner IR objects contained in IRUnit may have changed.  In this case,
    /// we have to call \c clear() on the inner analysis manager, as it may now
    /// have stale pointers to its inner IR objects.
    ///
    /// Regardless of whether the proxy analysis is marked as preserved, all of
    /// the analyses in the inner analysis manager are potentially invalidated
    /// based on the set of preserved analyses.
    bool invalidate(
        IRUnitT &IR, const PreservedAnalyses &PA,
        typename AnalysisManager<IRUnitT, ExtraArgTs...>::Invalidator &Inv);

  private:
    AnalysisManagerT *InnerAM;
  };

  explicit InnerAnalysisManagerProxy(AnalysisManagerT &InnerAM)
      : InnerAM(&InnerAM) {}

  /// Run the analysis pass and create our proxy result object.
  ///
  /// This doesn't do any interesting work; it is primarily used to insert our
  /// proxy result object into the outer analysis cache so that we can proxy
  /// invalidation to the inner analysis manager.
  Result run(IRUnitT &IR, AnalysisManager<IRUnitT, ExtraArgTs...> &AM,
             ExtraArgTs...) {
    return Result(*InnerAM);
  }

private:
  friend AnalysisInfoMixin<
      InnerAnalysisManagerProxy<AnalysisManagerT, IRUnitT>>;

  static AnalysisKey Key;

  AnalysisManagerT *InnerAM;
};

template <typename AnalysisManagerT, typename IRUnitT, typename... ExtraArgTs>
AnalysisKey
    InnerAnalysisManagerProxy<AnalysisManagerT, IRUnitT, ExtraArgTs...>::Key;

/// Provide the \c FunctionAnalysisManager to \c Module proxy.
using FunctionAnalysisManagerModuleProxy =
    InnerAnalysisManagerProxy<FunctionAnalysisManager, Module>;

/// Specialization of the invalidate method for the \c
/// FunctionAnalysisManagerModuleProxy's result.
template <>
bool FunctionAnalysisManagerModuleProxy::Result::invalidate(
    Module &M, const PreservedAnalyses &PA,
    ModuleAnalysisManager::Invalidator &Inv);

// Ensure the \c FunctionAnalysisManagerModuleProxy is provided as an extern
// template.
extern template class InnerAnalysisManagerProxy<FunctionAnalysisManager,
                                                Module>;

/// An analysis over an "inner" IR unit that provides access to an
/// analysis manager over a "outer" IR unit.  The inner unit must be contained
/// in the outer unit.
///
/// For example OuterAnalysisManagerProxy<ModuleAnalysisManager, Function> is an
/// analysis over Functions (the "inner" unit) which provides access to a Module
/// analysis manager.  The ModuleAnalysisManager is the "outer" manager being
/// proxied, and Modules are the "outer" IR unit.  The inner/outer relationship
/// is valid because each Function is contained in one Module.
///
/// This proxy only exposes the const interface of the outer analysis manager,
/// to indicate that you cannot cause an outer analysis to run from within an
/// inner pass.  Instead, you must rely on the \c getCachedResult API.  This is
/// due to keeping potential future concurrency in mind. To give an example,
/// running a module analysis before any function passes may give a different
/// result than running it in a function pass. Both may be valid, but it would
/// produce non-deterministic results. GlobalsAA is a good analysis example,
/// because the cached information has the mod/ref info for all memory for each
/// function at the time the analysis was computed. The information is still
/// valid after a function transformation, but it may be *different* if
/// recomputed after that transform. GlobalsAA is never invalidated.

///
/// This proxy doesn't manage invalidation in any way -- that is handled by the
/// recursive return path of each layer of the pass manager.  A consequence of
/// this is the outer analyses may be stale.  We invalidate the outer analyses
/// only when we're done running passes over the inner IR units.
template <typename AnalysisManagerT, typename IRUnitT, typename... ExtraArgTs>
class OuterAnalysisManagerProxy
    : public AnalysisInfoMixin<
          OuterAnalysisManagerProxy<AnalysisManagerT, IRUnitT, ExtraArgTs...>> {
public:
  /// Result proxy object for \c OuterAnalysisManagerProxy.
  class Result {
  public:
    explicit Result(const AnalysisManagerT &OuterAM) : OuterAM(&OuterAM) {}

    /// Get a cached analysis. If the analysis can be invalidated, this will
    /// assert.
    template <typename PassT, typename IRUnitTParam>
    typename PassT::Result *getCachedResult(IRUnitTParam &IR) const {
      typename PassT::Result *Res =
          OuterAM->template getCachedResult<PassT>(IR);
      if (Res)
        OuterAM->template verifyNotInvalidated<PassT>(IR, Res);
      return Res;
    }

    /// Method provided for unit testing, not intended for general use.
    template <typename PassT, typename IRUnitTParam>
    bool cachedResultExists(IRUnitTParam &IR) const {
      typename PassT::Result *Res =
          OuterAM->template getCachedResult<PassT>(IR);
      return Res != nullptr;
    }

    /// When invalidation occurs, remove any registered invalidation events.
    bool invalidate(
        IRUnitT &IRUnit, const PreservedAnalyses &PA,
        typename AnalysisManager<IRUnitT, ExtraArgTs...>::Invalidator &Inv) {
      // Loop over the set of registered outer invalidation mappings and if any
      // of them map to an analysis that is now invalid, clear it out.
      SmallVector<AnalysisKey *, 4> DeadKeys;
      for (auto &KeyValuePair : OuterAnalysisInvalidationMap) {
        AnalysisKey *OuterID = KeyValuePair.first;
        auto &InnerIDs = KeyValuePair.second;
        llvm::erase_if(InnerIDs, [&](AnalysisKey *InnerID) {
          return Inv.invalidate(InnerID, IRUnit, PA);
        });
        if (InnerIDs.empty())
          DeadKeys.push_back(OuterID);
      }

      for (auto *OuterID : DeadKeys)
        OuterAnalysisInvalidationMap.erase(OuterID);

      // The proxy itself remains valid regardless of anything else.
      return false;
    }

    /// Register a deferred invalidation event for when the outer analysis
    /// manager processes its invalidations.
    template <typename OuterAnalysisT, typename InvalidatedAnalysisT>
    void registerOuterAnalysisInvalidation() {
      AnalysisKey *OuterID = OuterAnalysisT::ID();
      AnalysisKey *InvalidatedID = InvalidatedAnalysisT::ID();

      auto &InvalidatedIDList = OuterAnalysisInvalidationMap[OuterID];
      // Note, this is a linear scan. If we end up with large numbers of
      // analyses that all trigger invalidation on the same outer analysis,
      // this entire system should be changed to some other deterministic
      // data structure such as a `SetVector` of a pair of pointers.
      if (!llvm::is_contained(InvalidatedIDList, InvalidatedID))
        InvalidatedIDList.push_back(InvalidatedID);
    }

    /// Access the map from outer analyses to deferred invalidation requiring
    /// analyses.
    const SmallDenseMap<AnalysisKey *, TinyPtrVector<AnalysisKey *>, 2> &
    getOuterInvalidations() const {
      return OuterAnalysisInvalidationMap;
    }

  private:
    const AnalysisManagerT *OuterAM;

    /// A map from an outer analysis ID to the set of this IR-unit's analyses
    /// which need to be invalidated.
    SmallDenseMap<AnalysisKey *, TinyPtrVector<AnalysisKey *>, 2>
        OuterAnalysisInvalidationMap;
  };

  OuterAnalysisManagerProxy(const AnalysisManagerT &OuterAM)
      : OuterAM(&OuterAM) {}

  /// Run the analysis pass and create our proxy result object.
  /// Nothing to see here, it just forwards the \c OuterAM reference into the
  /// result.
  Result run(IRUnitT &, AnalysisManager<IRUnitT, ExtraArgTs...> &,
             ExtraArgTs...) {
    return Result(*OuterAM);
  }

private:
  friend AnalysisInfoMixin<
      OuterAnalysisManagerProxy<AnalysisManagerT, IRUnitT, ExtraArgTs...>>;

  static AnalysisKey Key;

  const AnalysisManagerT *OuterAM;
};

template <typename AnalysisManagerT, typename IRUnitT, typename... ExtraArgTs>
AnalysisKey
    OuterAnalysisManagerProxy<AnalysisManagerT, IRUnitT, ExtraArgTs...>::Key;

extern template class OuterAnalysisManagerProxy<ModuleAnalysisManager,
                                                Function>;
/// Provide the \c ModuleAnalysisManager to \c Function proxy.
using ModuleAnalysisManagerFunctionProxy =
    OuterAnalysisManagerProxy<ModuleAnalysisManager, Function>;

/// Trivial adaptor that maps from a module to its functions.
///
/// Designed to allow composition of a FunctionPass(Manager) and
/// a ModulePassManager, by running the FunctionPass(Manager) over every
/// function in the module.
///
/// Function passes run within this adaptor can rely on having exclusive access
/// to the function they are run over. They should not read or modify any other
/// functions! Other threads or systems may be manipulating other functions in
/// the module, and so their state should never be relied on.
/// FIXME: Make the above true for all of LLVM's actual passes, some still
/// violate this principle.
///
/// Function passes can also read the module containing the function, but they
/// should not modify that module outside of the use lists of various globals.
/// For example, a function pass is not permitted to add functions to the
/// module.
/// FIXME: Make the above true for all of LLVM's actual passes, some still
/// violate this principle.
///
/// Note that although function passes can access module analyses, module
/// analyses are not invalidated while the function passes are running, so they
/// may be stale.  Function analyses will not be stale.
class ModuleToFunctionPassAdaptor
    : public PassInfoMixin<ModuleToFunctionPassAdaptor> {
public:
  using PassConceptT = detail::PassConcept<Function, FunctionAnalysisManager>;

  explicit ModuleToFunctionPassAdaptor(std::unique_ptr<PassConceptT> Pass,
                                       bool EagerlyInvalidate)
      : Pass(std::move(Pass)), EagerlyInvalidate(EagerlyInvalidate) {}

  /// Runs the function pass across every function in the module.
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName);

  static bool isRequired() { return true; }

private:
  std::unique_ptr<PassConceptT> Pass;
  bool EagerlyInvalidate;
};

/// A function to deduce a function pass type and wrap it in the
/// templated adaptor.
template <typename FunctionPassT>
ModuleToFunctionPassAdaptor
createModuleToFunctionPassAdaptor(FunctionPassT &&Pass,
                                  bool EagerlyInvalidate = false) {
  using PassModelT =
      detail::PassModel<Function, FunctionPassT, FunctionAnalysisManager>;
  // Do not use make_unique, it causes too many template instantiations,
  // causing terrible compile times.
  return ModuleToFunctionPassAdaptor(
      std::unique_ptr<ModuleToFunctionPassAdaptor::PassConceptT>(
          new PassModelT(std::forward<FunctionPassT>(Pass))),
      EagerlyInvalidate);
}

/// A utility pass template to force an analysis result to be available.
///
/// If there are extra arguments at the pass's run level there may also be
/// extra arguments to the analysis manager's \c getResult routine. We can't
/// guess how to effectively map the arguments from one to the other, and so
/// this specialization just ignores them.
///
/// Specific patterns of run-method extra arguments and analysis manager extra
/// arguments will have to be defined as appropriate specializations.
template <typename AnalysisT, typename IRUnitT,
          typename AnalysisManagerT = AnalysisManager<IRUnitT>,
          typename... ExtraArgTs>
struct RequireAnalysisPass
    : PassInfoMixin<RequireAnalysisPass<AnalysisT, IRUnitT, AnalysisManagerT,
                                        ExtraArgTs...>> {
  /// Run this pass over some unit of IR.
  ///
  /// This pass can be run over any unit of IR and use any analysis manager
  /// provided they satisfy the basic API requirements. When this pass is
  /// created, these methods can be instantiated to satisfy whatever the
  /// context requires.
  PreservedAnalyses run(IRUnitT &Arg, AnalysisManagerT &AM,
                        ExtraArgTs &&... Args) {
    (void)AM.template getResult<AnalysisT>(Arg,
                                           std::forward<ExtraArgTs>(Args)...);

    return PreservedAnalyses::all();
  }
  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName) {
    auto ClassName = AnalysisT::name();
    auto PassName = MapClassName2PassName(ClassName);
    OS << "require<" << PassName << '>';
  }
  static bool isRequired() { return true; }
};

/// A no-op pass template which simply forces a specific analysis result
/// to be invalidated.
template <typename AnalysisT>
struct InvalidateAnalysisPass
    : PassInfoMixin<InvalidateAnalysisPass<AnalysisT>> {
  /// Run this pass over some unit of IR.
  ///
  /// This pass can be run over any unit of IR and use any analysis manager,
  /// provided they satisfy the basic API requirements. When this pass is
  /// created, these methods can be instantiated to satisfy whatever the
  /// context requires.
  template <typename IRUnitT, typename AnalysisManagerT, typename... ExtraArgTs>
  PreservedAnalyses run(IRUnitT &Arg, AnalysisManagerT &AM, ExtraArgTs &&...) {
    auto PA = PreservedAnalyses::all();
    PA.abandon<AnalysisT>();
    return PA;
  }
  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName) {
    auto ClassName = AnalysisT::name();
    auto PassName = MapClassName2PassName(ClassName);
    OS << "invalidate<" << PassName << '>';
  }
};

/// A utility pass that does nothing, but preserves no analyses.
///
/// Because this preserves no analyses, any analysis passes queried after this
/// pass runs will recompute fresh results.
struct InvalidateAllAnalysesPass : PassInfoMixin<InvalidateAllAnalysesPass> {
  /// Run this pass over some unit of IR.
  template <typename IRUnitT, typename AnalysisManagerT, typename... ExtraArgTs>
  PreservedAnalyses run(IRUnitT &, AnalysisManagerT &, ExtraArgTs &&...) {
    return PreservedAnalyses::none();
  }
};

} // end namespace llvm

#endif // LLVM_IR_PASSMANAGER_H
