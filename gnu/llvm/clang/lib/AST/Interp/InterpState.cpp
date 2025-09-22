//===--- InterpState.cpp - Interpreter for the constexpr VM -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "InterpState.h"
#include "InterpFrame.h"
#include "InterpStack.h"
#include "Program.h"
#include "State.h"

using namespace clang;
using namespace clang::interp;

InterpState::InterpState(State &Parent, Program &P, InterpStack &Stk,
                         Context &Ctx, SourceMapper *M)
    : Parent(Parent), M(M), P(P), Stk(Stk), Ctx(Ctx), Current(nullptr) {}

InterpState::~InterpState() {
  while (Current) {
    InterpFrame *Next = Current->Caller;
    delete Current;
    Current = Next;
  }

  while (DeadBlocks) {
    DeadBlock *Next = DeadBlocks->Next;
    std::free(DeadBlocks);
    DeadBlocks = Next;
  }
}

void InterpState::cleanup() {
  // As a last resort, make sure all pointers still pointing to a dead block
  // don't point to it anymore.
  for (DeadBlock *DB = DeadBlocks; DB; DB = DB->Next) {
    for (Pointer *P = DB->B.Pointers; P; P = P->Next) {
      P->PointeeStorage.BS.Pointee = nullptr;
    }
  }

  Alloc.cleanup();
}

Frame *InterpState::getCurrentFrame() {
  if (Current && Current->Caller)
    return Current;
  return Parent.getCurrentFrame();
}

bool InterpState::reportOverflow(const Expr *E, const llvm::APSInt &Value) {
  QualType Type = E->getType();
  CCEDiag(E, diag::note_constexpr_overflow) << Value << Type;
  return noteUndefinedBehavior();
}

void InterpState::deallocate(Block *B) {
  assert(B);
  const Descriptor *Desc = B->getDescriptor();
  assert(Desc);

  if (B->hasPointers()) {
    size_t Size = B->getSize();

    // Allocate a new block, transferring over pointers.
    char *Memory =
        reinterpret_cast<char *>(std::malloc(sizeof(DeadBlock) + Size));
    auto *D = new (Memory) DeadBlock(DeadBlocks, B);
    std::memset(D->B.rawData(), 0, D->B.getSize());

    // Move data and metadata from the old block to the new (dead)block.
    if (B->IsInitialized && Desc->MoveFn) {
      Desc->MoveFn(B, B->data(), D->data(), Desc);
      if (Desc->getMetadataSize() > 0)
        std::memcpy(D->rawData(), B->rawData(), Desc->getMetadataSize());
    }
    D->B.IsInitialized = B->IsInitialized;

    // We moved the contents over to the DeadBlock.
    B->IsInitialized = false;
  } else if (B->IsInitialized) {
    B->invokeDtor();
  }
}

bool InterpState::maybeDiagnoseDanglingAllocations() {
  bool NoAllocationsLeft = (Alloc.getNumAllocations() == 0);

  if (!checkingPotentialConstantExpression()) {
    for (const auto &It : Alloc.allocation_sites()) {
      assert(It.second.size() > 0);

      const Expr *Source = It.first;
      CCEDiag(Source->getExprLoc(), diag::note_constexpr_memory_leak)
          << (It.second.size() - 1) << Source->getSourceRange();
    }
  }
  return NoAllocationsLeft;
}
