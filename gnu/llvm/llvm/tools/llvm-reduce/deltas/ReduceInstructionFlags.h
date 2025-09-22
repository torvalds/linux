//===- ReduceInstructionFlags.h - Specialized Delta Pass --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_REDUCE_DELTAS_REDUCEINSTRUCTIONFLAGS_H
#define LLVM_TOOLS_LLVM_REDUCE_DELTAS_REDUCEINSTRUCTIONFLAGS_H

#include "TestRunner.h"

namespace llvm {
void reduceInstructionFlagsDeltaPass(TestRunner &Test);
} // namespace llvm

#endif
