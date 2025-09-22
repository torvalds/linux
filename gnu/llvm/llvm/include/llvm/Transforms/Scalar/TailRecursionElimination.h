//===---- TailRecursionElimination.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file transforms calls of the current function (self recursion) followed
// by a return instruction with a branch to the entry of the function, creating
// a loop.  This pass also implements the following extensions to the basic
// algorithm:
//
//  1. Trivial instructions between the call and return do not prevent the
//     transformation from taking place, though currently the analysis cannot
//     support moving any really useful instructions (only dead ones).
//  2. This pass transforms functions that are prevented from being tail
//     recursive by an associative and commutative expression to use an
//     accumulator variable, thus compiling the typical naive factorial or
//     'fib' implementation into efficient code.
//  3. TRE is performed if the function returns void, if the return
//     returns the result returned by the call, or if the function returns a
//     run-time constant on all exits from the function.  It is possible, though
//     unlikely, that the return returns something else (like constant 0), and
//     can still be TRE'd.  It can be TRE'd if ALL OTHER return instructions in
//     the function return the exact same value.
//  4. If it can prove that callees do not access their caller stack frame,
//     they are marked as eligible for tail call elimination (by the code
//     generator).
//
// There are several improvements that could be made:
//
//  1. If the function has any alloca instructions, these instructions will be
//     moved out of the entry block of the function, causing them to be
//     evaluated each time through the tail recursion.  Safely keeping allocas
//     in the entry block requires analysis to proves that the tail-called
//     function does not read or write the stack object.
//  2. Tail recursion is only performed if the call immediately precedes the
//     return instruction.  It's possible that there could be a jump between
//     the call and the return.
//  3. There can be intervening operations between the call and the return that
//     prevent the TRE from occurring.  For example, there could be GEP's and
//     stores to memory that will not be read or written by the call.  This
//     requires some substantial analysis (such as with DSA) to prove safe to
//     move ahead of the call, but doing so could allow many more TREs to be
//     performed, for example in TreeAdd/TreeAlloc from the treeadd benchmark.
//  4. The algorithm we use to detect if callees access their caller stack
//     frames is very primitive.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_TAILRECURSIONELIMINATION_H
#define LLVM_TRANSFORMS_SCALAR_TAILRECURSIONELIMINATION_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

struct TailCallElimPass : PassInfoMixin<TailCallElimPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_TAILRECURSIONELIMINATION_H
