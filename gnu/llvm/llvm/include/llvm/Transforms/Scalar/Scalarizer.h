//===- Scalarizer.h --- Scalarize vector operations -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This pass converts vector operations into scalar operations (or, optionally,
/// operations on smaller vector widths), in order to expose optimization
/// opportunities on the individual scalar operations.
/// It is mainly intended for targets that do not have vector units, but it
/// may also be useful for revectorizing code to different vector widths.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_SCALARIZER_H
#define LLVM_TRANSFORMS_SCALAR_SCALARIZER_H

#include "llvm/IR/PassManager.h"
#include <optional>

namespace llvm {

class Function;

struct ScalarizerPassOptions {
  // These options correspond 1:1 to cl::opt options defined in
  // Scalarizer.cpp. When the cl::opt are specified, they take precedence.
  // When the cl::opt are not specified, the present optional values allow to
  // override the cl::opt's default values.
  std::optional<bool> ScalarizeVariableInsertExtract;
  std::optional<bool> ScalarizeLoadStore;
  std::optional<unsigned> ScalarizeMinBits;
};

class ScalarizerPass : public PassInfoMixin<ScalarizerPass> {
  ScalarizerPassOptions Options;

public:
  ScalarizerPass() = default;
  ScalarizerPass(const ScalarizerPassOptions &Options) : Options(Options) {}

  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  void setScalarizeVariableInsertExtract(bool Value) {
    Options.ScalarizeVariableInsertExtract = Value;
  }
  void setScalarizeLoadStore(bool Value) { Options.ScalarizeLoadStore = Value; }
  void setScalarizeMinBits(unsigned Value) { Options.ScalarizeMinBits = Value; }
};
}

#endif /* LLVM_TRANSFORMS_SCALAR_SCALARIZER_H */
