//===- llvm/IR/PassInstrumentation.h ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines the Pass Instrumentation classes that provide
/// instrumentation points into the pass execution by PassManager.
///
/// There are two main classes:
///   - PassInstrumentation provides a set of instrumentation points for
///     pass managers to call on.
///
///   - PassInstrumentationCallbacks registers callbacks and provides access
///     to them for PassInstrumentation.
///
/// PassInstrumentation object is being used as a result of
/// PassInstrumentationAnalysis (so it is intended to be easily copyable).
///
/// Intended scheme of use for Pass Instrumentation is as follows:
///    - register instrumentation callbacks in PassInstrumentationCallbacks
///      instance. PassBuilder provides helper for that.
///
///    - register PassInstrumentationAnalysis with all the PassManagers.
///      PassBuilder handles that automatically when registering analyses.
///
///    - Pass Manager requests PassInstrumentationAnalysis from analysis manager
///      and gets PassInstrumentation as its result.
///
///    - Pass Manager invokes PassInstrumentation entry points appropriately,
///      passing StringRef identification ("name") of the pass currently being
///      executed and IRUnit it works on. There can be different schemes of
///      providing names in future, currently it is just a name() of the pass.
///
///    - PassInstrumentation wraps address of IRUnit into llvm::Any and passes
///      control to all the registered callbacks. Note that we specifically wrap
///      'const IRUnitT*' so as to avoid any accidental changes to IR in
///      instrumenting callbacks.
///
///    - Some instrumentation points (BeforePass) allow to control execution
///      of a pass. For those callbacks returning false means pass will not be
///      executed.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_PASSINSTRUMENTATION_H
#define LLVM_IR_PASSINSTRUMENTATION_H

#include "llvm/ADT/Any.h"
#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/PassManager.h"
#include <type_traits>
#include <vector>

namespace llvm {

class PreservedAnalyses;
class StringRef;

/// This class manages callbacks registration, as well as provides a way for
/// PassInstrumentation to pass control to the registered callbacks.
class PassInstrumentationCallbacks {
public:
  // Before/After callbacks accept IRUnits whenever appropriate, so they need
  // to take them as constant pointers, wrapped with llvm::Any.
  // For the case when IRUnit has been invalidated there is a different
  // callback to use - AfterPassInvalidated.
  // We call all BeforePassFuncs to determine if a pass should run or not.
  // BeforeNonSkippedPassFuncs are called only if the pass should run.
  // TODO: currently AfterPassInvalidated does not accept IRUnit, since passing
  // already invalidated IRUnit is unsafe. There are ways to handle invalidated
  // IRUnits in a safe way, and we might pursue that as soon as there is a
  // useful instrumentation that needs it.
  using BeforePassFunc = bool(StringRef, Any);
  using BeforeSkippedPassFunc = void(StringRef, Any);
  using BeforeNonSkippedPassFunc = void(StringRef, Any);
  using AfterPassFunc = void(StringRef, Any, const PreservedAnalyses &);
  using AfterPassInvalidatedFunc = void(StringRef, const PreservedAnalyses &);
  using BeforeAnalysisFunc = void(StringRef, Any);
  using AfterAnalysisFunc = void(StringRef, Any);
  using AnalysisInvalidatedFunc = void(StringRef, Any);
  using AnalysesClearedFunc = void(StringRef);

public:
  PassInstrumentationCallbacks() = default;

  /// Copying PassInstrumentationCallbacks is not intended.
  PassInstrumentationCallbacks(const PassInstrumentationCallbacks &) = delete;
  void operator=(const PassInstrumentationCallbacks &) = delete;

  template <typename CallableT>
  void registerShouldRunOptionalPassCallback(CallableT C) {
    ShouldRunOptionalPassCallbacks.emplace_back(std::move(C));
  }

  template <typename CallableT>
  void registerBeforeSkippedPassCallback(CallableT C) {
    BeforeSkippedPassCallbacks.emplace_back(std::move(C));
  }

  template <typename CallableT>
  void registerBeforeNonSkippedPassCallback(CallableT C) {
    BeforeNonSkippedPassCallbacks.emplace_back(std::move(C));
  }

  template <typename CallableT>
  void registerAfterPassCallback(CallableT C, bool ToFront = false) {
    if (ToFront)
      AfterPassCallbacks.insert(AfterPassCallbacks.begin(), std::move(C));
    else
      AfterPassCallbacks.emplace_back(std::move(C));
  }

  template <typename CallableT>
  void registerAfterPassInvalidatedCallback(CallableT C, bool ToFront = false) {
    if (ToFront)
      AfterPassInvalidatedCallbacks.insert(
          AfterPassInvalidatedCallbacks.begin(), std::move(C));
    else
      AfterPassInvalidatedCallbacks.emplace_back(std::move(C));
  }

  template <typename CallableT>
  void registerBeforeAnalysisCallback(CallableT C) {
    BeforeAnalysisCallbacks.emplace_back(std::move(C));
  }

  template <typename CallableT>
  void registerAfterAnalysisCallback(CallableT C, bool ToFront = false) {
    if (ToFront)
      AfterAnalysisCallbacks.insert(AfterAnalysisCallbacks.begin(),
                                    std::move(C));
    else
      AfterAnalysisCallbacks.emplace_back(std::move(C));
  }

  template <typename CallableT>
  void registerAnalysisInvalidatedCallback(CallableT C) {
    AnalysisInvalidatedCallbacks.emplace_back(std::move(C));
  }

  template <typename CallableT>
  void registerAnalysesClearedCallback(CallableT C) {
    AnalysesClearedCallbacks.emplace_back(std::move(C));
  }

  template <typename CallableT>
  void registerClassToPassNameCallback(CallableT C) {
    ClassToPassNameCallbacks.emplace_back(std::move(C));
  }

  /// Add a class name to pass name mapping for use by pass instrumentation.
  void addClassToPassName(StringRef ClassName, StringRef PassName);
  /// Get the pass name for a given pass class name.
  StringRef getPassNameForClassName(StringRef ClassName);

private:
  friend class PassInstrumentation;

  /// These are only run on passes that are not required. They return false when
  /// an optional pass should be skipped.
  SmallVector<llvm::unique_function<BeforePassFunc>, 4>
      ShouldRunOptionalPassCallbacks;
  /// These are run on passes that are skipped.
  SmallVector<llvm::unique_function<BeforeSkippedPassFunc>, 4>
      BeforeSkippedPassCallbacks;
  /// These are run on passes that are about to be run.
  SmallVector<llvm::unique_function<BeforeNonSkippedPassFunc>, 4>
      BeforeNonSkippedPassCallbacks;
  /// These are run on passes that have just run.
  SmallVector<llvm::unique_function<AfterPassFunc>, 4> AfterPassCallbacks;
  /// These are run passes that have just run on invalidated IR.
  SmallVector<llvm::unique_function<AfterPassInvalidatedFunc>, 4>
      AfterPassInvalidatedCallbacks;
  /// These are run on analyses that are about to be run.
  SmallVector<llvm::unique_function<BeforeAnalysisFunc>, 4>
      BeforeAnalysisCallbacks;
  /// These are run on analyses that have been run.
  SmallVector<llvm::unique_function<AfterAnalysisFunc>, 4>
      AfterAnalysisCallbacks;
  /// These are run on analyses that have been invalidated.
  SmallVector<llvm::unique_function<AnalysisInvalidatedFunc>, 4>
      AnalysisInvalidatedCallbacks;
  /// These are run on analyses that have been cleared.
  SmallVector<llvm::unique_function<AnalysesClearedFunc>, 4>
      AnalysesClearedCallbacks;

  SmallVector<llvm::unique_function<void ()>, 4> ClassToPassNameCallbacks;
  DenseMap<StringRef, std::string> ClassToPassName;
};

/// This class provides instrumentation entry points for the Pass Manager,
/// doing calls to callbacks registered in PassInstrumentationCallbacks.
class PassInstrumentation {
  PassInstrumentationCallbacks *Callbacks;

  // Template argument PassT of PassInstrumentation::runBeforePass could be two
  // kinds: (1) a regular pass inherited from PassInfoMixin (happen when
  // creating a adaptor pass for a regular pass); (2) a type-erased PassConcept
  // created from (1). Here we want to make case (1) skippable unconditionally
  // since they are regular passes. We call PassConcept::isRequired to decide
  // for case (2).
  template <typename PassT>
  using has_required_t = decltype(std::declval<PassT &>().isRequired());

  template <typename PassT>
  static std::enable_if_t<is_detected<has_required_t, PassT>::value, bool>
  isRequired(const PassT &Pass) {
    return Pass.isRequired();
  }
  template <typename PassT>
  static std::enable_if_t<!is_detected<has_required_t, PassT>::value, bool>
  isRequired(const PassT &Pass) {
    return false;
  }

public:
  /// Callbacks object is not owned by PassInstrumentation, its life-time
  /// should at least match the life-time of corresponding
  /// PassInstrumentationAnalysis (which usually is till the end of current
  /// compilation).
  PassInstrumentation(PassInstrumentationCallbacks *CB = nullptr)
      : Callbacks(CB) {}

  /// BeforePass instrumentation point - takes \p Pass instance to be executed
  /// and constant reference to IR it operates on. \Returns true if pass is
  /// allowed to be executed. These are only run on optional pass since required
  /// passes must always be run. This allows these callbacks to print info when
  /// they want to skip a pass.
  template <typename IRUnitT, typename PassT>
  bool runBeforePass(const PassT &Pass, const IRUnitT &IR) const {
    if (!Callbacks)
      return true;

    bool ShouldRun = true;
    if (!isRequired(Pass)) {
      for (auto &C : Callbacks->ShouldRunOptionalPassCallbacks)
        ShouldRun &= C(Pass.name(), llvm::Any(&IR));
    }

    if (ShouldRun) {
      for (auto &C : Callbacks->BeforeNonSkippedPassCallbacks)
        C(Pass.name(), llvm::Any(&IR));
    } else {
      for (auto &C : Callbacks->BeforeSkippedPassCallbacks)
        C(Pass.name(), llvm::Any(&IR));
    }

    return ShouldRun;
  }

  /// AfterPass instrumentation point - takes \p Pass instance that has
  /// just been executed and constant reference to \p IR it operates on.
  /// \p IR is guaranteed to be valid at this point.
  template <typename IRUnitT, typename PassT>
  void runAfterPass(const PassT &Pass, const IRUnitT &IR,
                    const PreservedAnalyses &PA) const {
    if (Callbacks)
      for (auto &C : Callbacks->AfterPassCallbacks)
        C(Pass.name(), llvm::Any(&IR), PA);
  }

  /// AfterPassInvalidated instrumentation point - takes \p Pass instance
  /// that has just been executed. For use when IR has been invalidated
  /// by \p Pass execution.
  template <typename IRUnitT, typename PassT>
  void runAfterPassInvalidated(const PassT &Pass,
                               const PreservedAnalyses &PA) const {
    if (Callbacks)
      for (auto &C : Callbacks->AfterPassInvalidatedCallbacks)
        C(Pass.name(), PA);
  }

  /// BeforeAnalysis instrumentation point - takes \p Analysis instance
  /// to be executed and constant reference to IR it operates on.
  template <typename IRUnitT, typename PassT>
  void runBeforeAnalysis(const PassT &Analysis, const IRUnitT &IR) const {
    if (Callbacks)
      for (auto &C : Callbacks->BeforeAnalysisCallbacks)
        C(Analysis.name(), llvm::Any(&IR));
  }

  /// AfterAnalysis instrumentation point - takes \p Analysis instance
  /// that has just been executed and constant reference to IR it operated on.
  template <typename IRUnitT, typename PassT>
  void runAfterAnalysis(const PassT &Analysis, const IRUnitT &IR) const {
    if (Callbacks)
      for (auto &C : Callbacks->AfterAnalysisCallbacks)
        C(Analysis.name(), llvm::Any(&IR));
  }

  /// AnalysisInvalidated instrumentation point - takes \p Analysis instance
  /// that has just been invalidated and constant reference to IR it operated
  /// on.
  template <typename IRUnitT, typename PassT>
  void runAnalysisInvalidated(const PassT &Analysis, const IRUnitT &IR) const {
    if (Callbacks)
      for (auto &C : Callbacks->AnalysisInvalidatedCallbacks)
        C(Analysis.name(), llvm::Any(&IR));
  }

  /// AnalysesCleared instrumentation point - takes name of IR that analyses
  /// operated on.
  void runAnalysesCleared(StringRef Name) const {
    if (Callbacks)
      for (auto &C : Callbacks->AnalysesClearedCallbacks)
        C(Name);
  }

  /// Handle invalidation from the pass manager when PassInstrumentation
  /// is used as the result of PassInstrumentationAnalysis.
  ///
  /// On attempt to invalidate just return false. There is nothing to become
  /// invalid here.
  template <typename IRUnitT, typename... ExtraArgsT>
  bool invalidate(IRUnitT &, const class llvm::PreservedAnalyses &,
                  ExtraArgsT...) {
    return false;
  }

  template <typename CallableT>
  void pushBeforeNonSkippedPassCallback(CallableT C) {
    if (Callbacks)
      Callbacks->BeforeNonSkippedPassCallbacks.emplace_back(std::move(C));
  }
  void popBeforeNonSkippedPassCallback() {
    if (Callbacks)
      Callbacks->BeforeNonSkippedPassCallbacks.pop_back();
  }

  /// Get the pass name for a given pass class name.
  StringRef getPassNameForClassName(StringRef ClassName) const {
    if (Callbacks)
      return Callbacks->getPassNameForClassName(ClassName);
    return {};
  }
};

bool isSpecialPass(StringRef PassID, const std::vector<StringRef> &Specials);

/// Pseudo-analysis pass that exposes the \c PassInstrumentation to pass
/// managers.
class PassInstrumentationAnalysis
    : public AnalysisInfoMixin<PassInstrumentationAnalysis> {
  friend AnalysisInfoMixin<PassInstrumentationAnalysis>;
  static AnalysisKey Key;

  PassInstrumentationCallbacks *Callbacks;

public:
  /// PassInstrumentationCallbacks object is shared, owned by something else,
  /// not this analysis.
  PassInstrumentationAnalysis(PassInstrumentationCallbacks *Callbacks = nullptr)
      : Callbacks(Callbacks) {}

  using Result = PassInstrumentation;

  template <typename IRUnitT, typename AnalysisManagerT, typename... ExtraArgTs>
  Result run(IRUnitT &, AnalysisManagerT &, ExtraArgTs &&...) {
    return PassInstrumentation(Callbacks);
  }
};


} // namespace llvm

#endif
