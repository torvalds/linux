//===- llvm/Transforms/Utils.h - Utility Transformations --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header file defines prototypes for accessor functions that expose passes
// in the Utils transformations library.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_H
#define LLVM_TRANSFORMS_UTILS_H

namespace llvm {

class ModulePass;
class FunctionPass;
class Pass;

//===----------------------------------------------------------------------===//
//
// LowerInvoke - This pass removes invoke instructions, converting them to call
// instructions.
//
FunctionPass *createLowerInvokePass();
extern char &LowerInvokePassID;

//===----------------------------------------------------------------------===//
//
// LowerSwitch - This pass converts SwitchInst instructions into a sequence of
// chained binary branch instructions.
//
FunctionPass *createLowerSwitchPass();
extern char &LowerSwitchID;

//===----------------------------------------------------------------------===//
//
// EntryExitInstrumenter pass - Instrument function entry/exit with calls to
// mcount(), @__cyg_profile_func_{enter,exit} and the like. There are two
// variants, intended to run pre- and post-inlining, respectively. Only the
// post-inlining variant is used with the legacy pass manager.
//
FunctionPass *createPostInlineEntryExitInstrumenterPass();

//===----------------------------------------------------------------------===//
//
// BreakCriticalEdges - Break all of the critical edges in the CFG by inserting
// a dummy basic block. This pass may be "required" by passes that cannot deal
// with critical edges. For this usage, a pass must call:
//
//   AU.addRequiredID(BreakCriticalEdgesID);
//
// This pass obviously invalidates the CFG, but can update forward dominator
// (set, immediate dominators, tree, and frontier) information.
//
FunctionPass *createBreakCriticalEdgesPass();
extern char &BreakCriticalEdgesID;

//===----------------------------------------------------------------------===//
//
// LCSSA - This pass inserts phi nodes at loop boundaries to simplify other loop
// optimizations.
//
Pass *createLCSSAPass();
extern char &LCSSAID;

//===----------------------------------------------------------------------===//
//
// PromoteMemoryToRegister - This pass is used to promote memory references to
// be register references. A simple example of the transformation performed by
// this pass is:
//
//        FROM CODE                           TO CODE
//   %X = alloca i32, i32 1                 ret i32 42
//   store i32 42, i32 *%X
//   %Y = load i32* %X
//   ret i32 %Y
//
FunctionPass *createPromoteMemoryToRegisterPass();

//===----------------------------------------------------------------------===//
//
// LoopSimplify - Insert Pre-header blocks into the CFG for every function in
// the module.  This pass updates dominator information, loop information, and
// does not add critical edges to the CFG.
//
//   AU.addRequiredID(LoopSimplifyID);
//
Pass *createLoopSimplifyPass();
extern char &LoopSimplifyID;

//===----------------------------------------------------------------------===//
//
// UnifyLoopExits - For each loop, creates a new block N such that all exiting
// blocks branch to N, and then N distributes control flow to all the original
// exit blocks.
//
FunctionPass *createUnifyLoopExitsPass();

//===----------------------------------------------------------------------===//
//
// FixIrreducible - Convert each SCC with irreducible control-flow
// into a natural loop.
//
FunctionPass *createFixIrreduciblePass();

//===----------------------------------------------------------------------===//
//
// CanonicalizeFreezeInLoops - Canonicalize freeze instructions in loops so they
// don't block SCEV.
//
Pass *createCanonicalizeFreezeInLoopsPass();

//===----------------------------------------------------------------------===//
// LowerGlobalDtorsLegacy - Lower @llvm.global_dtors by creating wrapper
// functions that are registered in @llvm.global_ctors and which contain a call
// to `__cxa_atexit` to register their destructor functions.
ModulePass *createLowerGlobalDtorsLegacyPass();
} // namespace llvm

#endif
