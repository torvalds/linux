//===- ReduceInstructionsMIR.h  - Specialized Delta Pass --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass in order
// to reduce uninteresting MachineInstr from the MachineFunction.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_REDUCE_DELTAS_REDUCEINSTRUCTIONS_MIR_H
#define LLVM_TOOLS_LLVM_REDUCE_DELTAS_REDUCEINSTRUCTIONS_MIR_H

namespace llvm {
class TestRunner;

void reduceInstructionsMIRDeltaPass(TestRunner &Test);
} // namespace llvm

#endif
