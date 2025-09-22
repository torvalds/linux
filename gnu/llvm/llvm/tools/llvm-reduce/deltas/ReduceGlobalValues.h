//===- ReduceGlobalValues.h - Specialized Delta Pass ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass to reduce
// global value attributes/specifiers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_REDUCE_DELTAS_REDUCEGLOBALVALUES_H
#define LLVM_TOOLS_LLVM_REDUCE_DELTAS_REDUCEGLOBALVALUES_H

#include "Delta.h"

namespace llvm {
void reduceGlobalValuesDeltaPass(TestRunner &Test);
} // namespace llvm

#endif
