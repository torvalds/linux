//===- InlineOrder.h - Inlining order abstraction -*- C++ ---*-------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
#ifndef LLVM_ANALYSIS_INLINEORDER_H
#define LLVM_ANALYSIS_INLINEORDER_H

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Analysis/InlineCost.h"
#include <utility>

namespace llvm {
class CallBase;

template <typename T> class InlineOrder {
public:
  virtual ~InlineOrder() = default;

  virtual size_t size() = 0;

  virtual void push(const T &Elt) = 0;

  virtual T pop() = 0;

  virtual void erase_if(function_ref<bool(T)> Pred) = 0;

  bool empty() { return !size(); }
};

std::unique_ptr<InlineOrder<std::pair<CallBase *, int>>>
getDefaultInlineOrder(FunctionAnalysisManager &FAM, const InlineParams &Params,
                      ModuleAnalysisManager &MAM, Module &M);

std::unique_ptr<InlineOrder<std::pair<CallBase *, int>>>
getInlineOrder(FunctionAnalysisManager &FAM, const InlineParams &Params,
               ModuleAnalysisManager &MAM, Module &M);

/// Used for dynamically loading instances of InlineOrder as plugins
///
/// Plugins must implement an InlineOrderFactory, for an example refer to:
/// llvm/unittests/Analysis/InlineOrderPlugin/InlineOrderPlugin.cpp
///
/// If a PluginInlineOrderAnalysis has been registered with the
/// current ModuleAnalysisManager, llvm::getInlineOrder returns an
/// InlineOrder created by the PluginInlineOrderAnalysis' Factory.
///
class PluginInlineOrderAnalysis
    : public AnalysisInfoMixin<PluginInlineOrderAnalysis> {
public:
  static AnalysisKey Key;

  typedef std::unique_ptr<InlineOrder<std::pair<CallBase *, int>>> (
      *InlineOrderFactory)(FunctionAnalysisManager &FAM,
                           const InlineParams &Params,
                           ModuleAnalysisManager &MAM, Module &M);

  PluginInlineOrderAnalysis(InlineOrderFactory Factory) : Factory(Factory) {
    HasBeenRegistered = true;
    assert(Factory != nullptr &&
           "The plugin inline order factory should not be a null pointer.");
  }

  struct Result {
    InlineOrderFactory Factory;
  };

  Result run(Module &, ModuleAnalysisManager &) { return {Factory}; }
  Result getResult() { return {Factory}; }

  static bool isRegistered() { return HasBeenRegistered; }
  static void unregister() { HasBeenRegistered = false; }

private:
  static bool HasBeenRegistered;
  InlineOrderFactory Factory;
};

} // namespace llvm
#endif // LLVM_ANALYSIS_INLINEORDER_H
