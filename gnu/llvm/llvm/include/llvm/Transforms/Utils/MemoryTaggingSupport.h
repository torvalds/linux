//===- MemoryTaggingSupport.h - helpers for memory tagging implementations ===//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares common infrastructure for HWAddressSanitizer and
// Aarch64StackTagging.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_UTILS_MEMORYTAGGINGSUPPORT_H
#define LLVM_TRANSFORMS_UTILS_MEMORYTAGGINGSUPPORT_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/StackSafetyAnalysis.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Support/Alignment.h"

namespace llvm {
class DominatorTree;
class DbgVariableIntrinsic;
class IntrinsicInst;
class PostDominatorTree;
class AllocaInst;
class Instruction;
namespace memtag {
// For an alloca valid between lifetime markers Start and Ends, call the
// Callback for all possible exits out of the lifetime in the containing
// function, which can return from the instructions in RetVec.
//
// Returns whether Ends covered all possible exits. If they did not,
// the caller should remove Ends to ensure that work done at the other
// exits does not happen outside of the lifetime.
bool forAllReachableExits(const DominatorTree &DT, const PostDominatorTree &PDT,
                          const LoopInfo &LI, const Instruction *Start,
                          const SmallVectorImpl<IntrinsicInst *> &Ends,
                          const SmallVectorImpl<Instruction *> &RetVec,
                          llvm::function_ref<void(Instruction *)> Callback);

bool isStandardLifetime(const SmallVectorImpl<IntrinsicInst *> &LifetimeStart,
                        const SmallVectorImpl<IntrinsicInst *> &LifetimeEnd,
                        const DominatorTree *DT, const LoopInfo *LI,
                        size_t MaxLifetimes);

Instruction *getUntagLocationIfFunctionExit(Instruction &Inst);

struct AllocaInfo {
  AllocaInst *AI;
  SmallVector<IntrinsicInst *, 2> LifetimeStart;
  SmallVector<IntrinsicInst *, 2> LifetimeEnd;
  SmallVector<DbgVariableIntrinsic *, 2> DbgVariableIntrinsics;
  // Non-intrinsic records of variable locations.
  SmallVector<DbgVariableRecord *, 2> DbgVariableRecords;
};

struct StackInfo {
  MapVector<AllocaInst *, AllocaInfo> AllocasToInstrument;
  SmallVector<Instruction *, 4> UnrecognizedLifetimes;
  SmallVector<Instruction *, 8> RetVec;
  bool CallsReturnTwice = false;
};

class StackInfoBuilder {
public:
  StackInfoBuilder(const StackSafetyGlobalInfo *SSI) : SSI(SSI) {}

  void visit(Instruction &Inst);
  bool isInterestingAlloca(const AllocaInst &AI);
  StackInfo &get() { return Info; };

private:
  StackInfo Info;
  const StackSafetyGlobalInfo *SSI;
};

uint64_t getAllocaSizeInBytes(const AllocaInst &AI);
void alignAndPadAlloca(memtag::AllocaInfo &Info, llvm::Align Align);
bool isLifetimeIntrinsic(Value *V);

Value *readRegister(IRBuilder<> &IRB, StringRef Name);
Value *getFP(IRBuilder<> &IRB);
Value *getPC(const Triple &TargetTriple, IRBuilder<> &IRB);
Value *getAndroidSlotPtr(IRBuilder<> &IRB, int Slot);

void annotateDebugRecords(AllocaInfo &Info, unsigned int Tag);

} // namespace memtag
} // namespace llvm

#endif
