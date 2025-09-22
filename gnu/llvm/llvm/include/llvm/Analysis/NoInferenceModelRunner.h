//===- NoInferenceModelRunner.h ---- noop ML model runner  ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//

#ifndef LLVM_ANALYSIS_NOINFERENCEMODELRUNNER_H
#define LLVM_ANALYSIS_NOINFERENCEMODELRUNNER_H

#include "llvm/Analysis/MLModelRunner.h"
#include "llvm/Analysis/TensorSpec.h"
#include "llvm/Config/llvm-config.h"
namespace llvm {
/// A pseudo model runner. We use it to store feature values when collecting
/// logs for the default policy, in 'development' mode, but never ask it to
/// 'run'.
class NoInferenceModelRunner : public MLModelRunner {
public:
  NoInferenceModelRunner(LLVMContext &Ctx,
                         const std::vector<TensorSpec> &Inputs);

  static bool classof(const MLModelRunner *R) {
    return R->getKind() == MLModelRunner::Kind::NoOp;
  }

private:
  void *evaluateUntyped() override {
    llvm_unreachable("We shouldn't call run on this model runner.");
  }
};
} // namespace llvm
#endif // LLVM_ANALYSIS_NOINFERENCEMODELRUNNER_H
