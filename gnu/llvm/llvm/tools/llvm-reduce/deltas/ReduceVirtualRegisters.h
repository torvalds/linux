//===- ReduceVirtualRegisters.h  - Specialized Delta Pass -------*- c++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass in order
// to simplify virtual register information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_REDUCE_DELTAS_REDUCEVIRTUALREGISTERS_H
#define LLVM_TOOLS_LLVM_REDUCE_DELTAS_REDUCEVIRTUALREGISTERS_H

namespace llvm {
class TestRunner;

/// Remove register allocation hints from virtual registes.
void reduceVirtualRegisterHintsDeltaPass(TestRunner &Test);

} // namespace llvm

#endif
