//===- SSAContext.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file declares a specialization of the GenericSSAContext<X>
/// class template for LLVM IR.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_SSACONTEXT_H
#define LLVM_IR_SSACONTEXT_H

#include "llvm/ADT/GenericSSAContext.h"
#include "llvm/IR/BasicBlock.h"

namespace llvm {
class BasicBlock;
class Function;
class Instruction;
class Value;

inline auto instrs(const BasicBlock &BB) {
  return llvm::make_range(BB.begin(), BB.end());
}

template <> struct GenericSSATraits<Function> {
  using BlockT = BasicBlock;
  using FunctionT = Function;
  using InstructionT = Instruction;
  using ValueRefT = Value *;
  using ConstValueRefT = const Value *;
  using UseT = Use;
};

using SSAContext = GenericSSAContext<Function>;

} // namespace llvm

#endif // LLVM_IR_SSACONTEXT_H
