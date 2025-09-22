//===- ReduceIRReferences.h  - Specialized Delta Pass -----------*- c++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a function which calls the Generic Delta pass in order
// to reduce uninteresting IR references from the MachineFunction.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_REDUCE_DELTAS_REDUCEIRREFERENCES_MIR_H
#define LLVM_TOOLS_LLVM_REDUCE_DELTAS_REDUCEIRREFERENCES_MIR_H

namespace llvm {
class TestRunner;

/// Remove IR references from instructions (i.e. from memory operands)
void reduceIRInstructionReferencesDeltaPass(TestRunner &Test);

/// Remove IR BasicBlock references (the block names)
void reduceIRBlockReferencesDeltaPass(TestRunner &Test);

/// Remove IR references from function level fields (e.g. frame object names)
void reduceIRFunctionReferencesDeltaPass(TestRunner &Test);

} // namespace llvm

#endif
