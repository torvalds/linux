//===- ReduceGlobalVarInitializers.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass in order
// to reduce initializers of Global Variables in the provided Module.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_REDUCE_DELTAS_REDUCEGLOBALVARINITIALIZERS_H
#define LLVM_TOOLS_LLVM_REDUCE_DELTAS_REDUCEGLOBALVARINITIALIZERS_H

#include "Delta.h"
#include "llvm/IR/Value.h"
#include "llvm/Transforms/Utils/Cloning.h"

namespace llvm {
void reduceGlobalsInitializersDeltaPass(TestRunner &Test);
} // namespace llvm

#endif
