//===- NoInferenceModelRunner.cpp - noop ML model runner   ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A pseudo model runner. We use it to store feature values when collecting
// logs for the default policy, in 'development' mode, but never ask it to
// 'run'.
//===----------------------------------------------------------------------===//
#include "llvm/Analysis/NoInferenceModelRunner.h"

using namespace llvm;

NoInferenceModelRunner::NoInferenceModelRunner(
    LLVMContext &Ctx, const std::vector<TensorSpec> &Inputs)
    : MLModelRunner(Ctx, MLModelRunner::Kind::NoOp, Inputs.size()) {
  size_t Index = 0;
  for (const auto &TS : Inputs)
    setUpBufferForTensor(Index++, TS, nullptr);
}
