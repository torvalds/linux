//===- Transform/Utils/CodeMoverUtils.h - CodeMover Utils -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This family of functions determine movements are safe on basic blocks, and
// instructions contained within a function.
//
// Please note that this is work in progress, and the functionality is not
// ready for broader production use.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_CODEMOVERUTILS_H
#define LLVM_TRANSFORMS_UTILS_CODEMOVERUTILS_H

namespace llvm {

class BasicBlock;
class DependenceInfo;
class DominatorTree;
class Instruction;
class PostDominatorTree;

/// Return true if \p I0 and \p I1 are control flow equivalent.
/// Two instructions are control flow equivalent if their basic blocks are
/// control flow equivalent.
bool isControlFlowEquivalent(const Instruction &I0, const Instruction &I1,
                             const DominatorTree &DT,
                             const PostDominatorTree &PDT);

/// Return true if \p BB0 and \p BB1 are control flow equivalent.
/// Two basic blocks are control flow equivalent if when one executes, the other
/// is guaranteed to execute.
bool isControlFlowEquivalent(const BasicBlock &BB0, const BasicBlock &BB1,
                             const DominatorTree &DT,
                             const PostDominatorTree &PDT);

/// Return true if \p I can be safely moved before \p InsertPoint.
bool isSafeToMoveBefore(Instruction &I, Instruction &InsertPoint,
                        DominatorTree &DT,
                        const PostDominatorTree *PDT = nullptr,
                        DependenceInfo *DI = nullptr,
                        bool CheckForEntireBlock = false);

/// Return true if all instructions (except the terminator) in \p BB can be
/// safely moved before \p InsertPoint.
bool isSafeToMoveBefore(BasicBlock &BB, Instruction &InsertPoint,
                        DominatorTree &DT,
                        const PostDominatorTree *PDT = nullptr,
                        DependenceInfo *DI = nullptr);

/// Move instructions, in an order-preserving manner, from \p FromBB to the
/// beginning of \p ToBB when proven safe.
void moveInstructionsToTheBeginning(BasicBlock &FromBB, BasicBlock &ToBB,
                                    DominatorTree &DT,
                                    const PostDominatorTree &PDT,
                                    DependenceInfo &DI);

/// Move instructions, in an order-preserving manner, from \p FromBB to the end
/// of \p ToBB when proven safe.
void moveInstructionsToTheEnd(BasicBlock &FromBB, BasicBlock &ToBB,
                              DominatorTree &DT, const PostDominatorTree &PDT,
                              DependenceInfo &DI);

/// In case that two BBs \p ThisBlock and \p OtherBlock are control flow
/// equivalent but they do not strictly dominate and post-dominate each
/// other, we determine if \p ThisBlock is reached after \p OtherBlock
/// in the control flow.
bool nonStrictlyPostDominate(const BasicBlock *ThisBlock,
                             const BasicBlock *OtherBlock,
                             const DominatorTree *DT,
                             const PostDominatorTree *PDT);

// Check if I0 is reached before I1 in the control flow.
bool isReachedBefore(const Instruction *I0, const Instruction *I1,
                     const DominatorTree *DT, const PostDominatorTree *PDT);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_CODEMOVERUTILS_H
