//===- TFUtils.h - utilities for TFLite -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
#ifndef LLVM_ANALYSIS_UTILS_TFUTILS_H
#define LLVM_ANALYSIS_UTILS_TFUTILS_H

#include "llvm/Config/llvm-config.h"

#ifdef LLVM_HAVE_TFLITE
#include "llvm/ADT/StringMap.h"
#include "llvm/Analysis/TensorSpec.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/JSON.h"

#include <memory>
#include <vector>

namespace llvm {

/// Load a SavedModel, find the given inputs and outputs, and setup storage
/// for input tensors. The user is responsible for correctly dimensioning the
/// input tensors and setting their values before calling evaluate().
/// To initialize:
/// - construct the object
/// - initialize the input tensors using initInput. Indices must correspond to
///   indices in the InputNames used at construction.
/// To use:
/// - set input values by using getInput to get each input tensor, and then
///   setting internal scalars, for all dimensions (tensors are row-major:
///   https://github.com/tensorflow/tensorflow/blob/r1.5/tensorflow/c/c_api.h#L205)
/// - call evaluate. The input tensors' values are not consumed after this, and
///   may still be read.
/// - use the outputs in the output vector
class TFModelEvaluatorImpl;
class EvaluationResultImpl;

class TFModelEvaluator final {
public:
  /// The result of a model evaluation. Handles the lifetime of the output
  /// tensors, which means that their values need to be used before
  /// the EvaluationResult's dtor is called.
  class EvaluationResult {
  public:
    EvaluationResult(const EvaluationResult &) = delete;
    EvaluationResult &operator=(const EvaluationResult &Other) = delete;

    EvaluationResult(EvaluationResult &&Other);
    EvaluationResult &operator=(EvaluationResult &&Other);

    ~EvaluationResult();

    /// Get a (const) pointer to the first element of the tensor at Index.
    template <typename T> T *getTensorValue(size_t Index) {
      return static_cast<T *>(getUntypedTensorValue(Index));
    }

    template <typename T> const T *getTensorValue(size_t Index) const {
      return static_cast<T *>(getUntypedTensorValue(Index));
    }

    /// Get a (const) pointer to the untyped data of the tensor.
    void *getUntypedTensorValue(size_t Index);
    const void *getUntypedTensorValue(size_t Index) const;

  private:
    friend class TFModelEvaluator;
    EvaluationResult(std::unique_ptr<EvaluationResultImpl> Impl);
    std::unique_ptr<EvaluationResultImpl> Impl;
  };

  TFModelEvaluator(StringRef SavedModelPath,
                   const std::vector<TensorSpec> &InputSpecs,
                   const std::vector<TensorSpec> &OutputSpecs,
                   const char *Tags = "serve");

  ~TFModelEvaluator();
  TFModelEvaluator(const TFModelEvaluator &) = delete;
  TFModelEvaluator(TFModelEvaluator &&) = delete;

  /// Evaluate the model, assuming it is valid. Returns std::nullopt if the
  /// evaluation fails or the model is invalid, or an EvaluationResult
  /// otherwise. The inputs are assumed to have been already provided via
  /// getInput(). When returning std::nullopt, it also invalidates this object.
  std::optional<EvaluationResult> evaluate();

  /// Provides access to the input vector.
  template <typename T> T *getInput(size_t Index) {
    return static_cast<T *>(getUntypedInput(Index));
  }

  /// Returns true if the model was loaded successfully, false
  /// otherwise.
  bool isValid() const { return !!Impl; }

  /// Untyped access to input.
  void *getUntypedInput(size_t Index);

private:
  std::unique_ptr<TFModelEvaluatorImpl> Impl;
};

} // namespace llvm

#endif // LLVM_HAVE_TFLITE
#endif // LLVM_ANALYSIS_UTILS_TFUTILS_H
