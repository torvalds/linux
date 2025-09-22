//===-- GuardUtils.h - Utils for work with guards ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// Utils that are used to perform analyzes related to guards and their
// conditions.
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_GUARDUTILS_H
#define LLVM_ANALYSIS_GUARDUTILS_H

namespace llvm {

class BasicBlock;
class Use;
class User;
class Value;
template <typename T> class SmallVectorImpl;

/// Returns true iff \p U has semantics of a guard expressed in a form of call
/// of llvm.experimental.guard intrinsic.
bool isGuard(const User *U);

/// Returns true iff \p V has semantics of llvm.experimental.widenable.condition
/// call
bool isWidenableCondition(const Value *V);

/// Returns true iff \p U is a widenable branch (that is,
/// extractWidenableCondition returns widenable condition).
bool isWidenableBranch(const User *U);

/// Returns true iff \p U has semantics of a guard expressed in a form of a
/// widenable conditional branch to deopt block.
bool isGuardAsWidenableBranch(const User *U);

/// If U is widenable branch looking like:
///   %cond = ...
///   %wc = call i1 @llvm.experimental.widenable.condition()
///   %branch_cond = and i1 %cond, %wc
///   br i1 %branch_cond, label %if_true_bb, label %if_false_bb ; <--- U
/// The function returns true, and the values %cond and %wc and blocks
/// %if_true_bb, if_false_bb are returned in
/// the parameters (Condition, WidenableCondition, IfTrueBB and IfFalseFF)
/// respectively. If \p U does not match this pattern, return false.
bool parseWidenableBranch(const User *U, Value *&Condition,
                          Value *&WidenableCondition, BasicBlock *&IfTrueBB,
                          BasicBlock *&IfFalseBB);

/// Analogous to the above, but return the Uses so that they can be
/// modified. Unlike previous version, Condition is optional and may be null.
bool parseWidenableBranch(User *U, Use *&Cond, Use *&WC, BasicBlock *&IfTrueBB,
                          BasicBlock *&IfFalseBB);

// The guard condition is expected to be in form of:
//   cond1 && cond2 && cond3 ...
// or in case of widenable branch:
//   cond1 && cond2 && cond3 && widenable_contidion ...
// Method collects the list of checks, but skips widenable_condition.
void parseWidenableGuard(const User *U, llvm::SmallVectorImpl<Value *> &Checks);

// Returns widenable_condition if it exists in the expression tree rooting from
// \p U and has only one use.
Value *extractWidenableCondition(const User *U);
} // llvm

#endif // LLVM_ANALYSIS_GUARDUTILS_H
