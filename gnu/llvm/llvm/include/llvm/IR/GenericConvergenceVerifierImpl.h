//===- GenericConvergenceVerifierImpl.h -----------------------*- C++ -*---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
///
/// A verifier for the static rules of convergence control tokens that works
/// with both LLVM IR and MIR.
///
/// This template implementation resides in a separate file so that it does not
/// get injected into every .cpp file that includes the generic header.
///
/// DO NOT INCLUDE THIS FILE WHEN MERELY USING CYCLEINFO.
///
/// This file should only be included by files that implement a
/// specialization of the relevant templates. Currently these are:
/// - llvm/lib/IR/Verifier.cpp
/// - llvm/lib/CodeGen/MachineVerifier.cpp
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_GENERICCONVERGENCEVERIFIERIMPL_H
#define LLVM_IR_GENERICCONVERGENCEVERIFIERIMPL_H

#include "llvm/ADT/GenericConvergenceVerifier.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/IntrinsicInst.h"

#define Check(C, ...)                                                          \
  do {                                                                         \
    if (!(C)) {                                                                \
      reportFailure(__VA_ARGS__);                                              \
      return;                                                                  \
    }                                                                          \
  } while (false)

#define CheckOrNull(C, ...)                                                    \
  do {                                                                         \
    if (!(C)) {                                                                \
      reportFailure(__VA_ARGS__);                                              \
      return {};                                                               \
    }                                                                          \
  } while (false)

namespace llvm {
template <class ContextT> void GenericConvergenceVerifier<ContextT>::clear() {
  Tokens.clear();
  CI.clear();
  ConvergenceKind = NoConvergence;
}

template <class ContextT>
void GenericConvergenceVerifier<ContextT>::visit(const BlockT &BB) {
  SeenFirstConvOp = false;
}

template <class ContextT>
void GenericConvergenceVerifier<ContextT>::visit(const InstructionT &I) {
  ConvOpKind ConvOp = getConvOp(I);

  auto *TokenDef = findAndCheckConvergenceTokenUsed(I);
  switch (ConvOp) {
  case CONV_ENTRY:
    Check(isInsideConvergentFunction(I),
          "Entry intrinsic can occur only in a convergent function.",
          {Context.print(&I)});
    Check(I.getParent()->isEntryBlock(),
          "Entry intrinsic can occur only in the entry block.",
          {Context.print(&I)});
    Check(!SeenFirstConvOp,
          "Entry intrinsic cannot be preceded by a convergent operation in the "
          "same basic block.",
          {Context.print(&I)});
    [[fallthrough]];
  case CONV_ANCHOR:
    Check(!TokenDef,
          "Entry or anchor intrinsic cannot have a convergencectrl token "
          "operand.",
          {Context.print(&I)});
    break;
  case CONV_LOOP:
    Check(TokenDef, "Loop intrinsic must have a convergencectrl token operand.",
          {Context.print(&I)});
    Check(!SeenFirstConvOp,
          "Loop intrinsic cannot be preceded by a convergent operation in the "
          "same basic block.",
          {Context.print(&I)});
    break;
  default:
    break;
  }

  if (ConvOp != CONV_NONE)
    checkConvergenceTokenProduced(I);

  if (isConvergent(I))
    SeenFirstConvOp = true;

  if (TokenDef || ConvOp != CONV_NONE) {
    Check(isConvergent(I),
          "Convergence control token can only be used in a convergent call.",
          {Context.print(&I)});
    Check(ConvergenceKind != UncontrolledConvergence,
          "Cannot mix controlled and uncontrolled convergence in the same "
          "function.",
          {Context.print(&I)});
    ConvergenceKind = ControlledConvergence;
  } else if (isConvergent(I)) {
    Check(ConvergenceKind != ControlledConvergence,
          "Cannot mix controlled and uncontrolled convergence in the same "
          "function.",
          {Context.print(&I)});
    ConvergenceKind = UncontrolledConvergence;
  }
}

template <class ContextT>
void GenericConvergenceVerifier<ContextT>::reportFailure(
    const Twine &Message, ArrayRef<Printable> DumpedValues) {
  FailureCB(Message);
  if (OS) {
    for (auto V : DumpedValues)
      *OS << V << '\n';
  }
}

template <class ContextT>
void GenericConvergenceVerifier<ContextT>::verify(const DominatorTreeT &DT) {
  assert(Context.getFunction());
  const auto &F = *Context.getFunction();

  DenseMap<const BlockT *, SmallVector<const InstructionT *, 8>> LiveTokenMap;
  DenseMap<const CycleT *, const InstructionT *> CycleHearts;

  // Just like the DominatorTree, compute the CycleInfo locally so that we
  // can run the verifier outside of a pass manager and we don't rely on
  // potentially out-dated analysis results.
  CI.compute(const_cast<FunctionT &>(F));

  auto checkToken = [&](const InstructionT *Token, const InstructionT *User,
                        SmallVectorImpl<const InstructionT *> &LiveTokens) {
    Check(DT.dominates(Token->getParent(), User->getParent()),
          "Convergence control token must dominate all its uses.",
          {Context.print(Token), Context.print(User)});

    Check(llvm::is_contained(LiveTokens, Token),
          "Convergence region is not well-nested.",
          {Context.print(Token), Context.print(User)});
    while (LiveTokens.back() != Token)
      LiveTokens.pop_back();

    // Check static rules about cycles.
    auto *BB = User->getParent();
    auto *BBCycle = CI.getCycle(BB);
    if (!BBCycle)
      return;

    auto *DefBB = Token->getParent();
    if (DefBB == BB || BBCycle->contains(DefBB)) {
      // degenerate occurrence of a loop intrinsic
      return;
    }

    Check(getConvOp(*User) == CONV_LOOP,
          "Convergence token used by an instruction other than "
          "llvm.experimental.convergence.loop in a cycle that does "
          "not contain the token's definition.",
          {Context.print(User), CI.print(BBCycle)});

    while (true) {
      auto *Parent = BBCycle->getParentCycle();
      if (!Parent || Parent->contains(DefBB))
        break;
      BBCycle = Parent;
    };

    Check(BBCycle->isReducible() && BB == BBCycle->getHeader(),
          "Cycle heart must dominate all blocks in the cycle.",
          {Context.print(User), Context.printAsOperand(BB), CI.print(BBCycle)});
    Check(!CycleHearts.count(BBCycle),
          "Two static convergence token uses in a cycle that does "
          "not contain either token's definition.",
          {Context.print(User), Context.print(CycleHearts[BBCycle]),
           CI.print(BBCycle)});
    CycleHearts[BBCycle] = User;
  };

  ReversePostOrderTraversal<const FunctionT *> RPOT(&F);
  SmallVector<const InstructionT *, 8> LiveTokens;
  for (auto *BB : RPOT) {
    LiveTokens.clear();
    auto LTIt = LiveTokenMap.find(BB);
    if (LTIt != LiveTokenMap.end()) {
      LiveTokens = std::move(LTIt->second);
      LiveTokenMap.erase(LTIt);
    }

    for (auto &I : *BB) {
      if (auto *Token = Tokens.lookup(&I))
        checkToken(Token, &I, LiveTokens);
      if (getConvOp(I) != CONV_NONE)
        LiveTokens.push_back(&I);
    }

    // Propagate token liveness
    for (auto *Succ : successors(BB)) {
      auto *SuccNode = DT.getNode(Succ);
      auto LTIt = LiveTokenMap.find(Succ);
      if (LTIt == LiveTokenMap.end()) {
        // We're the first predecessor: all tokens which dominate the
        // successor are live for now.
        LTIt = LiveTokenMap.try_emplace(Succ).first;
        for (auto LiveToken : LiveTokens) {
          if (!DT.dominates(DT.getNode(LiveToken->getParent()), SuccNode))
            break;
          LTIt->second.push_back(LiveToken);
        }
      } else {
        // Compute the intersection of live tokens.
        auto It = llvm::partition(
            LTIt->second, [&LiveTokens](const InstructionT *Token) {
              return llvm::is_contained(LiveTokens, Token);
            });
        LTIt->second.erase(It, LTIt->second.end());
      }
    }
  }
}

} // end namespace llvm

#endif // LLVM_IR_GENERICCONVERGENCEVERIFIERIMPL_H
