//===--- Block.cpp - Allocated blocks for the interpreter -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the classes describing allocated blocks.
//
//===----------------------------------------------------------------------===//

#include "InterpBlock.h"
#include "Pointer.h"

using namespace clang;
using namespace clang::interp;

void Block::addPointer(Pointer *P) {
  assert(P);
  if (IsStatic) {
    assert(!Pointers);
    return;
  }

#ifndef NDEBUG
  assert(!hasPointer(P));
#endif
  if (Pointers)
    Pointers->Prev = P;
  P->Next = Pointers;
  P->Prev = nullptr;
  Pointers = P;
}

void Block::removePointer(Pointer *P) {
  assert(P);
  if (IsStatic) {
    assert(!Pointers);
    return;
  }

#ifndef NDEBUG
  assert(hasPointer(P));
#endif

  if (Pointers == P)
    Pointers = P->Next;

  if (P->Prev)
    P->Prev->Next = P->Next;
  if (P->Next)
    P->Next->Prev = P->Prev;
}

void Block::cleanup() {
  if (Pointers == nullptr && IsDead)
    (reinterpret_cast<DeadBlock *>(this + 1) - 1)->free();
}

void Block::replacePointer(Pointer *Old, Pointer *New) {
  assert(Old);
  assert(New);
  if (IsStatic) {
    assert(!Pointers);
    return;
  }

#ifndef NDEBUG
  assert(hasPointer(Old));
#endif

  removePointer(Old);
  addPointer(New);

  Old->PointeeStorage.BS.Pointee = nullptr;

#ifndef NDEBUG
  assert(!hasPointer(Old));
  assert(hasPointer(New));
#endif
}

#ifndef NDEBUG
bool Block::hasPointer(const Pointer *P) const {
  for (const Pointer *C = Pointers; C; C = C->Next) {
    if (C == P)
      return true;
  }
  return false;
}
#endif

DeadBlock::DeadBlock(DeadBlock *&Root, Block *Blk)
    : Root(Root),
      B(~0u, Blk->Desc, Blk->IsStatic, Blk->IsExtern, /*isDead=*/true) {
  // Add the block to the chain of dead blocks.
  if (Root)
    Root->Prev = this;

  Next = Root;
  Prev = nullptr;
  Root = this;

  // Transfer pointers.
  B.Pointers = Blk->Pointers;
  for (Pointer *P = Blk->Pointers; P; P = P->Next)
    P->PointeeStorage.BS.Pointee = &B;
  Blk->Pointers = nullptr;
}

void DeadBlock::free() {
  if (B.IsInitialized)
    B.invokeDtor();

  if (Prev)
    Prev->Next = Next;
  if (Next)
    Next->Prev = Prev;
  if (Root == this)
    Root = Next;
  std::free(this);
}
