//===- ModelUnderTrainingRunner.cpp - 'development' mode runner -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of a MLModelRunner for 'development' mode, i.e. evaluation
// happens off a model that's provided from the command line and is interpreted.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/Config/config.h"
#if defined(LLVM_HAVE_TFLITE)
#include "llvm/Analysis/ModelUnderTrainingRunner.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include <optional>

using namespace llvm;
namespace {
struct LoggedFeatureSpec {
  TensorSpec Spec;
  std::optional<std::string> LoggingName;
};

std::optional<std::vector<LoggedFeatureSpec>>
loadOutputSpecs(LLVMContext &Ctx, StringRef ExpectedDecisionName,
                StringRef ModelPath, StringRef SpecFileOverride) {
  SmallVector<char, 128> OutputSpecsPath;
  StringRef FileName = SpecFileOverride;
  if (FileName.empty()) {
    llvm::sys::path::append(OutputSpecsPath, ModelPath, "output_spec.json");
    FileName = {OutputSpecsPath.data(), OutputSpecsPath.size()};
  }

  auto BufferOrError = MemoryBuffer::getFileOrSTDIN(FileName);
  if (!BufferOrError) {
    Ctx.emitError("Error opening output specs file: " + FileName + " : " +
                  BufferOrError.getError().message());
    return std::nullopt;
  }
  auto ParsedJSONValues = json::parse(BufferOrError.get()->getBuffer());
  if (!ParsedJSONValues) {
    Ctx.emitError("Could not parse specs file: " + FileName);
    return std::nullopt;
  }
  auto ValuesArray = ParsedJSONValues->getAsArray();
  if (!ValuesArray) {
    Ctx.emitError("Expected an array of {tensor_spec:<TensorSpec>, "
                  "logging_name:<name>} dictionaries");
    return std::nullopt;
  }
  std::vector<LoggedFeatureSpec> Ret;
  for (const auto &Value : *ValuesArray)
    if (const auto *Obj = Value.getAsObject())
      if (const auto *SpecPart = Obj->get("tensor_spec"))
        if (auto TensorSpec = getTensorSpecFromJSON(Ctx, *SpecPart))
          if (auto LoggingName = Obj->getString("logging_name")) {
            if (!TensorSpec->isElementType<int64_t>() &&
                !TensorSpec->isElementType<int32_t>() &&
                !TensorSpec->isElementType<float>()) {
              Ctx.emitError(
                  "Only int64, int32, and float tensors are supported. "
                  "Found unsupported type for tensor named " +
                  TensorSpec->name());
              return std::nullopt;
            }
            Ret.push_back({*TensorSpec, LoggingName->str()});
          }

  if (ValuesArray->size() != Ret.size()) {
    Ctx.emitError(
        "Unable to parse output spec. It should be a json file containing an "
        "array of dictionaries. Each dictionary must have a 'tensor_spec' key, "
        "with a json object describing a TensorSpec; and a 'logging_name' key, "
        "which is a string to use as name when logging this tensor in the "
        "training log.");
    return std::nullopt;
  }
  if (Ret.empty() || *Ret[0].LoggingName != ExpectedDecisionName) {
    Ctx.emitError("The first output spec must describe the decision tensor, "
                  "and must have the logging_name " +
                  StringRef(ExpectedDecisionName));
    return std::nullopt;
  }
  return Ret;
}
} // namespace

ModelUnderTrainingRunner::ModelUnderTrainingRunner(
    LLVMContext &Ctx, const std::string &ModelPath,
    const std::vector<TensorSpec> &InputSpecs,
    const std::vector<TensorSpec> &OutputSpecs,
    const std::vector<TensorSpec> &ExtraOutputsForLogging)
    : MLModelRunner(Ctx, MLModelRunner::Kind::Development, InputSpecs.size()),
      OutputSpecs(OutputSpecs), ExtraOutputsForLogging(ExtraOutputsForLogging) {
  Evaluator =
      std::make_unique<TFModelEvaluator>(ModelPath, InputSpecs, OutputSpecs);
  if (!Evaluator || !Evaluator->isValid()) {
    Ctx.emitError("Failed to create saved model evaluator");
    Evaluator.reset();
    return;
  }

  for (size_t I = 0, E = InputSpecs.size(); I < E; ++I) {
    setUpBufferForTensor(I, InputSpecs[I], Evaluator->getUntypedInput(I));
  }
}

void *ModelUnderTrainingRunner::evaluateUntyped() {
  LastEvaluationResult = Evaluator->evaluate();
  if (!LastEvaluationResult.has_value()) {
    Ctx.emitError("Error evaluating model.");
    return nullptr;
  }
  return LastEvaluationResult->getUntypedTensorValue(0);
}

std::unique_ptr<ModelUnderTrainingRunner>
ModelUnderTrainingRunner::createAndEnsureValid(
    LLVMContext &Ctx, const std::string &ModelPath, StringRef DecisionName,
    const std::vector<TensorSpec> &InputSpecs,
    StringRef OutputSpecsPathOverride) {
  if (auto MaybeOutputSpecs = loadOutputSpecs(Ctx, DecisionName, ModelPath,
                                              OutputSpecsPathOverride)) {
    std::unique_ptr<ModelUnderTrainingRunner> MUTR;
    std::vector<TensorSpec> OutputSpecs;
    std::vector<TensorSpec> ExtraOutputsForLogging;
    append_range(OutputSpecs,
                 map_range(*MaybeOutputSpecs, [](const LoggedFeatureSpec &LFS) {
                   return LFS.Spec;
                 }));
    append_range(ExtraOutputsForLogging,
                 map_range(drop_begin(*MaybeOutputSpecs),
                           [](const LoggedFeatureSpec &LFS) {
                             return TensorSpec(LFS.LoggingName
                                                   ? *LFS.LoggingName
                                                   : LFS.Spec.name(),
                                               LFS.Spec);
                           }));

    MUTR.reset(new ModelUnderTrainingRunner(
        Ctx, ModelPath, InputSpecs, OutputSpecs, ExtraOutputsForLogging));
    if (MUTR && MUTR->isValid())
      return MUTR;

    Ctx.emitError("Could not load or create model evaluator.");
    return nullptr;
  }
  Ctx.emitError("Could not load the policy model from the provided path");
  return nullptr;
}

#endif // defined(LLVM_HAVE_TFLITE)
