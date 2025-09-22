//===- Uniformity.h --------------------------------------*- C++ -*--------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_UNIFORMITY_H
#define LLVM_ADT_UNIFORMITY_H

namespace llvm {

/// Enum describing how instructions behave with respect to uniformity and
/// divergence, to answer the question: if the same instruction is executed by
/// two threads in a convergent set of threads, will its result value(s) be
/// uniform, i.e. the same on both threads?
enum class InstructionUniformity {
  /// The result values are uniform if and only if all operands are uniform.
  Default,

  /// The result values are always uniform.
  AlwaysUniform,

  /// The result values can never be assumed to be uniform.
  NeverUniform
};

} // namespace llvm
#endif // LLVM_ADT_UNIFORMITY_H
