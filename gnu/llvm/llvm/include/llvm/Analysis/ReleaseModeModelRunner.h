//===- ReleaseModeModelRunner.h - Fast, precompiled model runner  ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a model runner wrapping an AOT compiled ML model.
// Only inference is supported.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_RELEASEMODEMODELRUNNER_H
#define LLVM_ANALYSIS_RELEASEMODEMODELRUNNER_H

#include "llvm/ADT/StringExtras.h"
#include "llvm/Analysis/MLModelRunner.h"
#include "llvm/Analysis/TensorSpec.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MD5.h"

#include <memory>

namespace llvm {

/// ReleaseModeModelRunner - production mode implementation of the
/// MLModelRunner. It uses an AOT-compiled SavedModel for efficient execution.
struct EmbeddedModelRunnerOptions {
  /// Feed and Fetch feature prefixes - i.e. a feature named "foo" will be
  /// looked up as {FeedPrefix}_foo; and the output named "bar" will be looked
  /// up as {FetchPrefix}_bar
  StringRef FeedPrefix = "feed_";
  StringRef FetchPrefix = "fetch_";

  /// ModelSelector is the name (recognized by the AOT-ed model) of a sub-model
  /// to use. "" is allowed if the model doesn't support sub-models.
  StringRef ModelSelector = "";

  EmbeddedModelRunnerOptions &setFeedPrefix(StringRef Value) {
    FeedPrefix = Value;
    return *this;
  }
  EmbeddedModelRunnerOptions &setFetchPrefix(StringRef Value) {
    FetchPrefix = Value;
    return *this;
  }
  EmbeddedModelRunnerOptions &setModelSelector(StringRef Value) {
    ModelSelector = Value;
    return *this;
  }
};

template <class TGen>
class ReleaseModeModelRunner final : public MLModelRunner {
public:
  /// FeatureNames' type should be an indexed collection of std::string, like
  /// std::array or std::vector, that has a size() method.
  template <class FType>
  ReleaseModeModelRunner(LLVMContext &Ctx, const FType &InputSpec,
                         StringRef DecisionName,
                         const EmbeddedModelRunnerOptions &Options = {})
      : MLModelRunner(Ctx, MLModelRunner::Kind::Release, InputSpec.size() + 1),
        CompiledModel(std::make_unique<TGen>()) {
    assert(CompiledModel && "The CompiledModel should be valid");
    // Set up the model_selector past all the InputSpecs in all cases.
    //   - if the model doesn't have such a feature, but the user requested it,
    //   we report error. Same if the model supports it but the user didn't
    //   specify it
    //   - finally, we compute the MD5 hash of the user input and set the value
    //   of the model selector to {high, low}
    bool InputIsPresent = true;
    populateTensor(InputSpec.size(),
                   TensorSpec::createSpec<uint64_t>("model_selector", {2}),
                   Options.FeedPrefix, InputIsPresent);

    // If we hit the "report an error" cases outlined above, continue with the
    // set up in case there's some custom diagnostics handler installed and it
    // doesn't promptly exit.
    if (Options.ModelSelector.empty() && InputIsPresent)
      Ctx.emitError(
          "A model selector was not specified but the underlying model "
          "requires selecting one because it exposes a model_selector input");
    uint64_t High = 0;
    uint64_t Low = 0;
    if (!Options.ModelSelector.empty()) {
      if (!InputIsPresent)
        Ctx.emitError("A model selector was specified but the underlying model "
                      "does not expose a model_selector input");
      const auto Hash = MD5::hash(arrayRefFromStringRef(Options.ModelSelector));
      High = Hash.high();
      Low = Hash.low();
    }
    getTensor<uint64_t>(InputSpec.size())[0] = High;
    getTensor<uint64_t>(InputSpec.size())[1] = Low;
    // At this point, the model selector is set up. If the user didn't provide
    // one, but the model has a model_selector, it'll be set to (0, 0) which
    // the composite model should treat as error as part of its implementation
    // (but that should only matter if there is a custom handler that doesn't
    // exit on error)
    for (size_t I = 0; I < InputSpec.size(); ++I)
      populateTensor(I, InputSpec[I], Options.FeedPrefix, InputIsPresent);

    ResultIndex = CompiledModel->LookupResultIndex(Options.FetchPrefix.str() +
                                                   DecisionName.str());
    assert(ResultIndex >= 0 && "Cannot find DecisionName in inlining model");
  }

  virtual ~ReleaseModeModelRunner() = default;

  static bool classof(const MLModelRunner *R) {
    return R->getKind() == MLModelRunner::Kind::Release;
  }

private:
  // fetch the model-provided buffer for the given Spec, or let MLModelRunner
  // create a scratch buffer. Indicate back to the caller if the model had that
  // input in the first place.
  void populateTensor(size_t Pos, const TensorSpec &Spec, StringRef Prefix,
                      bool &InputIsPresent) {
    const int Index =
        CompiledModel->LookupArgIndex((Prefix + Spec.name()).str());
    void *Buffer = nullptr;
    InputIsPresent = Index >= 0;
    if (InputIsPresent)
      Buffer = CompiledModel->arg_data(Index);
    setUpBufferForTensor(Pos, Spec, Buffer);
  }

  void *evaluateUntyped() override {
    CompiledModel->Run();
    return CompiledModel->result_data(ResultIndex);
  }

  int32_t ResultIndex = -1;
  std::unique_ptr<TGen> CompiledModel;
};

/// A mock class satisfying the interface expected by ReleaseModeModelRunner for
/// its `TGen` parameter. Useful to avoid conditional compilation complexity, as
/// a compile-time replacement for a real AOT-ed model.
class NoopSavedModelImpl final {
#define NOOP_MODEL_ERRMSG                                                      \
  "The mock AOT-ed saved model is a compile-time stub and should not be "      \
  "called."

public:
  NoopSavedModelImpl() = default;
  int LookupArgIndex(const std::string &) { llvm_unreachable(NOOP_MODEL_ERRMSG); }
  int LookupResultIndex(const std::string &) { llvm_unreachable(NOOP_MODEL_ERRMSG); }
  void Run() { llvm_unreachable(NOOP_MODEL_ERRMSG); }
  void *result_data(int) { llvm_unreachable(NOOP_MODEL_ERRMSG); }
  void *arg_data(int) { llvm_unreachable(NOOP_MODEL_ERRMSG); }
#undef NOOP_MODEL_ERRMSG
};

template <class T> bool isEmbeddedModelEvaluatorValid() { return true; }

template <> inline bool isEmbeddedModelEvaluatorValid<NoopSavedModelImpl>() {
  return false;
}
} // namespace llvm

#endif // LLVM_ANALYSIS_RELEASEMODEMODELRUNNER_H
