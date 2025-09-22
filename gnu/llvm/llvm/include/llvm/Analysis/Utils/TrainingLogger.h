//===- TrainingLogger.h - mlgo feature/reward logging  ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The design goals of the logger are:
// - no dependencies that llvm doesn't already have.
// - support streaming, so that we don't need to buffer data during compilation
// - 0-decoding tensor values. Tensor values are potentially very large buffers
// of scalars. Because of their potentially large size, avoiding
// serialization/deserialization overhead is preferred.
//
// The simple logger produces an output of the form (each line item on its line)
// - header: a json object describing the data that will follow.
// - context: e.g. function name, for regalloc, or "default" for module-wide
// optimizations like the inliner. This is the context to which the subsequent
// data corresponds.
// - observation number.
// - tensor values - raw bytes of the tensors, in the order given in the header.
// The values are in succession, i.e. no separator is found between successive
// tensor values. At the end, there is a new line character.
// - [score] - this is optional, and is present if it was present in the header.
// Currently, for final rewards, we output "0" scores after each observation,
// except for the last one.
// <repeat>
// The file should be read as binary, but the reason we use newlines is mostly
// ease of debugging: the log can be opened in a text editor and, while tensor
// values are inscrutable, at least the sequence of data can be easily observed.
// Of course, the buffer of tensor values could contain '\n' bytes. A reader
// should use the header information to know how much data to read for the
// tensor values, and not use line information for that.
//
// An example reader, used for test, is available at
// Analysis/models/log_reader.py
//
// Example:
// {"features":[list of TensorSpecs], "score":<a tensor spec>}
// {"context": "aFunction"}
// {"observation": 0}
// <bytes>
// {"outcome": 0}
// <bytes for the tensor corresponding to the "score" spec in the header>
// {"observation": 1}
// ...
// {"context": "anotherFunction"}
// {"observation": 0}
// ...
//

#ifndef LLVM_ANALYSIS_UTILS_TRAININGLOGGER_H
#define LLVM_ANALYSIS_UTILS_TRAININGLOGGER_H

#include "llvm/Config/llvm-config.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/Analysis/TensorSpec.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/JSON.h"

#include <memory>
#include <optional>
#include <vector>

namespace llvm {

/// Logging utility - given an ordered specification of features, and assuming
/// a scalar reward, allow logging feature values and rewards.
/// The assumption is that, for an event to be logged (i.e. a set of feature
/// values and a reward), the user calls the log* API for each feature exactly
/// once, providing the index matching the position in the feature spec list
/// provided at construction. The example assumes the first feature's element
/// type is float, the second is int64, and the reward is float:
///
/// event 0:
///   logFloatValue(0, ...)
///   logInt64Value(1, ...)
///   ...
///   logFloatReward(...)
/// event 1:
///   logFloatValue(0, ...)
///   logInt64Value(1, ...)
///   ...
///   logFloatReward(...)
///
/// At the end, call print to generate the log.
/// Alternatively, don't call logReward at the end of each event, just
/// log{Float|Int32|Int64}FinalReward at the end.
class Logger final {
  std::unique_ptr<raw_ostream> OS;
  const std::vector<TensorSpec> FeatureSpecs;
  const TensorSpec RewardSpec;
  const bool IncludeReward;
  StringMap<size_t> ObservationIDs;
  std::string CurrentContext;

  void writeHeader(std::optional<TensorSpec> AdviceSpec);
  void writeTensor(const TensorSpec &Spec, const char *RawData) {
    OS->write(RawData, Spec.getTotalTensorBufferSize());
  }
  void logRewardImpl(const char *RawData);

public:
  /// Construct a Logger. If IncludeReward is false, then logReward or
  /// logFinalReward shouldn't be called, and the reward feature won't be
  /// printed out.
  /// NOTE: the FeatureSpecs are expected to be in the same order (i.e. have
  /// corresponding indices) with any MLModelRunner implementations
  /// corresponding to the model being trained/logged.
  Logger(std::unique_ptr<raw_ostream> OS,
         const std::vector<TensorSpec> &FeatureSpecs,
         const TensorSpec &RewardSpec, bool IncludeReward,
         std::optional<TensorSpec> AdviceSpec = std::nullopt);

  void switchContext(StringRef Name);
  void startObservation();
  void endObservation();
  void flush() { OS->flush(); }

  const std::string &currentContext() const { return CurrentContext; }

  /// Check if there is at least an observation for `currentContext()`.
  bool hasObservationInProgress() const {
    return hasAnyObservationForContext(CurrentContext);
  }

  /// Check if there is at least an observation for the context `Ctx`.
  bool hasAnyObservationForContext(StringRef Ctx) const {
    return ObservationIDs.contains(Ctx);
  }

  template <typename T> void logReward(T Value) {
    logRewardImpl(reinterpret_cast<const char *>(&Value));
  }

  void logTensorValue(size_t FeatureID, const char *RawData) {
    writeTensor(FeatureSpecs[FeatureID], RawData);
  }
};

} // namespace llvm
#endif // LLVM_ANALYSIS_UTILS_TRAININGLOGGER_H
