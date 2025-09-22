//===-- Operations.h - ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementations of common fuzzer operation descriptors for building an IR
// mutator.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_FUZZMUTATE_OPERATIONS_H
#define LLVM_FUZZMUTATE_OPERATIONS_H

#include "llvm/FuzzMutate/OpDescriptor.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"

namespace llvm {

/// Getters for the default sets of operations, per general category.
/// @{
void describeFuzzerIntOps(std::vector<fuzzerop::OpDescriptor> &Ops);
void describeFuzzerFloatOps(std::vector<fuzzerop::OpDescriptor> &Ops);
void describeFuzzerControlFlowOps(std::vector<fuzzerop::OpDescriptor> &Ops);
void describeFuzzerPointerOps(std::vector<fuzzerop::OpDescriptor> &Ops);
void describeFuzzerAggregateOps(std::vector<fuzzerop::OpDescriptor> &Ops);
void describeFuzzerVectorOps(std::vector<fuzzerop::OpDescriptor> &Ops);
void describeFuzzerUnaryOperations(std::vector<fuzzerop::OpDescriptor> &Ops);
void describeFuzzerOtherOps(std::vector<fuzzerop::OpDescriptor> &Ops);
/// @}

namespace fuzzerop {

/// Descriptors for individual operations.
/// @{
OpDescriptor selectDescriptor(unsigned Weight);
OpDescriptor fnegDescriptor(unsigned Weight);
OpDescriptor binOpDescriptor(unsigned Weight, Instruction::BinaryOps Op);
OpDescriptor cmpOpDescriptor(unsigned Weight, Instruction::OtherOps CmpOp,
                             CmpInst::Predicate Pred);
OpDescriptor splitBlockDescriptor(unsigned Weight);
OpDescriptor gepDescriptor(unsigned Weight);
OpDescriptor extractValueDescriptor(unsigned Weight);
OpDescriptor insertValueDescriptor(unsigned Weight);
OpDescriptor extractElementDescriptor(unsigned Weight);
OpDescriptor insertElementDescriptor(unsigned Weight);
OpDescriptor shuffleVectorDescriptor(unsigned Weight);

/// @}

} // namespace fuzzerop

} // namespace llvm

#endif // LLVM_FUZZMUTATE_OPERATIONS_H
