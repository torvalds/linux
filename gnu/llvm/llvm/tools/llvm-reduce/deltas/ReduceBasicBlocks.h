//===- ReduceArguments.h - Specialized Delta Pass ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass in order
// to reduce uninteresting BasicBlocks from defined functions.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TOOLS_LLVM_REDUCE_DELTAS_REDUCEBASICBLOCKS_H
#define LLVM_TOOLS_LLVM_REDUCE_DELTAS_REDUCEBASICBLOCKS_H

#include "Delta.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

namespace llvm {
void reduceBasicBlocksDeltaPass(TestRunner &Test);
void reduceUnreachableBasicBlocksDeltaPass(TestRunner &Test);
} // namespace llvm

#endif
