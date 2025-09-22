//===- TFUtils.cpp - TFLite-based evaluation utilities --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements utilities for interfacing with TFLite.
//
//===----------------------------------------------------------------------===//
#include "llvm/Config/config.h"
#if defined(LLVM_HAVE_TFLITE)

#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/Utils/TFUtils.h"
#include "llvm/Support/Base64.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/kernels/register.h"
#include "tensorflow/lite/model.h"
#include "tensorflow/lite/model_builder.h"
#include "tensorflow/lite/op_resolver.h"
#include "tensorflow/lite/logger.h"

#include <cassert>
#include <numeric>
#include <optional>

using namespace llvm;

namespace llvm {
class EvaluationResultImpl {
public:
  EvaluationResultImpl(const std::vector<const TfLiteTensor *> &Outputs)
      : Outputs(Outputs){};

  const TfLiteTensor *getOutput(size_t I) { return Outputs[I]; }

  EvaluationResultImpl(const EvaluationResultImpl &) = delete;
  EvaluationResultImpl(EvaluationResultImpl &&Other) = delete;

private:
  const std::vector<const TfLiteTensor *> Outputs;
};

class TFModelEvaluatorImpl {
public:
  TFModelEvaluatorImpl(StringRef SavedModelPath,
                       const std::vector<TensorSpec> &InputSpecs,
                       const std::vector<TensorSpec> &OutputSpecs,
                       const char *Tags);

  bool isValid() const { return IsValid; }
  size_t outputSize() const { return Output.size(); }

  std::unique_ptr<EvaluationResultImpl> evaluate() {
    Interpreter->Invoke();
    return std::make_unique<EvaluationResultImpl>(Output);
  }

  const std::vector<TfLiteTensor *> &getInput() const { return Input; }

  ~TFModelEvaluatorImpl();

private:
  std::unique_ptr<tflite::FlatBufferModel> Model;

  /// The objects necessary for carrying out an evaluation of the SavedModel.
  /// They are expensive to set up, and we maintain them accross all the
  /// evaluations of the model.
  std::unique_ptr<tflite::Interpreter> Interpreter;

  /// The input tensors. We set up the tensors once and just mutate theirs
  /// scalars before each evaluation. The input tensors keep their value after
  /// an evaluation.
  std::vector<TfLiteTensor *> Input;

  /// The output nodes.
  std::vector<const TfLiteTensor *> Output;

  void invalidate() { IsValid = false; }

  bool IsValid = true;

  /// Reusable utility for ensuring we can bind the requested Name to a node in
  /// the SavedModel Graph.
  bool checkReportAndInvalidate(const TfLiteTensor *Tensor,
                                const TensorSpec &Spec);
};

} // namespace llvm

TFModelEvaluatorImpl::TFModelEvaluatorImpl(
    StringRef SavedModelPath, const std::vector<TensorSpec> &InputSpecs,
    const std::vector<TensorSpec> &OutputSpecs, const char *Tags = "serve")
    : Input(InputSpecs.size()), Output(OutputSpecs.size()) {
  // INFO and DEBUG messages could be numerous and not particularly interesting
  tflite::LoggerOptions::SetMinimumLogSeverity(tflite::TFLITE_LOG_WARNING);
  // FIXME: make ErrorReporter a member (may also need subclassing
  // StatefulErrorReporter) to easily get the latest error status, for
  // debugging.
  tflite::StderrReporter ErrorReporter;
  SmallVector<char, 128> TFLitePathBuff;
  llvm::sys::path::append(TFLitePathBuff, SavedModelPath, "model.tflite");
  StringRef TFLitePath(TFLitePathBuff.data(), TFLitePathBuff.size());
  Model = tflite::FlatBufferModel::BuildFromFile(TFLitePath.str().c_str(),
                                                 &ErrorReporter);
  if (!Model) {
    invalidate();
    return;
  }

  tflite::ops::builtin::BuiltinOpResolver Resolver;
  tflite::InterpreterBuilder Builder(*Model, Resolver);
  Builder(&Interpreter);

  if (!Interpreter) {
    invalidate();
    return;
  }

  // We assume the input buffers are valid for the lifetime of the interpreter.
  // By default, tflite allocates memory in an arena and will periodically take
  // away memory and reallocate it in a different location after evaluations in
  // order to improve utilization of the buffers owned in the arena. So, we
  // explicitly mark our input buffers as persistent to avoid this behavior.
  for (size_t I = 0; I < Interpreter->inputs().size(); ++I)
    Interpreter->tensor(I)->allocation_type =
        TfLiteAllocationType::kTfLiteArenaRwPersistent;

  if (Interpreter->AllocateTensors() != TfLiteStatus::kTfLiteOk) {
    invalidate();
    return;
  }
  // Known inputs and outputs
  StringMap<int> InputsMap;
  StringMap<int> OutputsMap;
  for (size_t I = 0; I < Interpreter->inputs().size(); ++I)
    InputsMap[Interpreter->GetInputName(I)] = I;
  for (size_t I = 0; I < Interpreter->outputs().size(); ++I)
    OutputsMap[Interpreter->GetOutputName(I)] = I;

  size_t NumberFeaturesPassed = 0;
  for (size_t I = 0; I < InputSpecs.size(); ++I) {
    auto &InputSpec = InputSpecs[I];
    auto MapI = InputsMap.find(InputSpec.name() + ":" +
                               std::to_string(InputSpec.port()));
    if (MapI == InputsMap.end()) {
      Input[I] = nullptr;
      continue;
    }
    Input[I] = Interpreter->tensor(MapI->second);
    if (!checkReportAndInvalidate(Input[I], InputSpec))
      return;
    std::memset(Input[I]->data.data, 0,
                InputSpecs[I].getTotalTensorBufferSize());
    ++NumberFeaturesPassed;
  }

  if (NumberFeaturesPassed < Interpreter->inputs().size()) {
    // we haven't passed all the required features to the model, throw an error.
    errs() << "Required feature(s) have not been passed to the ML model";
    invalidate();
    return;
  }

  for (size_t I = 0; I < OutputSpecs.size(); ++I) {
    const auto &OutputSpec = OutputSpecs[I];
    Output[I] = Interpreter->output_tensor(
        OutputsMap[OutputSpec.name() + ":" +
                   std::to_string(OutputSpec.port())]);
    if (!checkReportAndInvalidate(Output[I], OutputSpec))
      return;
  }
}

TFModelEvaluator::TFModelEvaluator(StringRef SavedModelPath,
                                   const std::vector<TensorSpec> &InputSpecs,
                                   const std::vector<TensorSpec> &OutputSpecs,
                                   const char *Tags)
    : Impl(new TFModelEvaluatorImpl(SavedModelPath, InputSpecs, OutputSpecs,
                                    Tags)) {
  if (!Impl->isValid())
    Impl.reset();
}

TFModelEvaluatorImpl::~TFModelEvaluatorImpl() {}

bool TFModelEvaluatorImpl::checkReportAndInvalidate(const TfLiteTensor *Tensor,
                                                    const TensorSpec &Spec) {
  if (!Tensor) {
    errs() << "Could not find TF_Output named: " + Spec.name();
    IsValid = false;
  }
  if (Spec.getTotalTensorBufferSize() != Tensor->bytes)
    IsValid = false;

  // If the total sizes match, there could still be a mismatch in the shape.
  // We ignore that for now.

  return IsValid;
}

std::optional<TFModelEvaluator::EvaluationResult> TFModelEvaluator::evaluate() {
  if (!isValid())
    return std::nullopt;
  return EvaluationResult(Impl->evaluate());
}

void *TFModelEvaluator::getUntypedInput(size_t Index) {
  TfLiteTensor *T = Impl->getInput()[Index];
  if (!T)
    return nullptr;
  return T->data.data;
}

TFModelEvaluator::EvaluationResult::EvaluationResult(
    std::unique_ptr<EvaluationResultImpl> Impl)
    : Impl(std::move(Impl)) {}

TFModelEvaluator::EvaluationResult::EvaluationResult(EvaluationResult &&Other)
    : Impl(std::move(Other.Impl)) {}

TFModelEvaluator::EvaluationResult &
TFModelEvaluator::EvaluationResult::operator=(EvaluationResult &&Other) {
  Impl = std::move(Other.Impl);
  return *this;
}

void *TFModelEvaluator::EvaluationResult::getUntypedTensorValue(size_t Index) {
  return Impl->getOutput(Index)->data.data;
}

const void *
TFModelEvaluator::EvaluationResult::getUntypedTensorValue(size_t Index) const {
  return Impl->getOutput(Index)->data.data;
}

TFModelEvaluator::EvaluationResult::~EvaluationResult() {}
TFModelEvaluator::~TFModelEvaluator() {}

#endif // defined(LLVM_HAVE_TFLITE)
