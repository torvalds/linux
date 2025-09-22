//===-- Use.cpp - Implement the Use class ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"

namespace llvm {

void Use::swap(Use &RHS) {
  if (Val == RHS.Val)
    return;

  std::swap(Val, RHS.Val);
  std::swap(Next, RHS.Next);
  std::swap(Prev, RHS.Prev);

  *Prev = this;
  if (Next)
    Next->Prev = &Next;

  *RHS.Prev = &RHS;
  if (RHS.Next)
    RHS.Next->Prev = &RHS.Next;
}

unsigned Use::getOperandNo() const {
  return this - getUser()->op_begin();
}

void Use::zap(Use *Start, const Use *Stop, bool del) {
  while (Start != Stop)
    (--Stop)->~Use();
  if (del)
    ::operator delete(Start);
}

} // namespace llvm
