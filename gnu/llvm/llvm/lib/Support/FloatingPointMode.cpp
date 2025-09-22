//===- FloatingPointMode.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/FloatingPointMode.h"
#include "llvm/ADT/StringExtras.h"

using namespace llvm;

FPClassTest llvm::fneg(FPClassTest Mask) {
  FPClassTest NewMask = Mask & fcNan;
  if (Mask & fcNegInf)
    NewMask |= fcPosInf;
  if (Mask & fcNegNormal)
    NewMask |= fcPosNormal;
  if (Mask & fcNegSubnormal)
    NewMask |= fcPosSubnormal;
  if (Mask & fcNegZero)
    NewMask |= fcPosZero;
  if (Mask & fcPosZero)
    NewMask |= fcNegZero;
  if (Mask & fcPosSubnormal)
    NewMask |= fcNegSubnormal;
  if (Mask & fcPosNormal)
    NewMask |= fcNegNormal;
  if (Mask & fcPosInf)
    NewMask |= fcNegInf;
  return NewMask;
}

FPClassTest llvm::inverse_fabs(FPClassTest Mask) {
  FPClassTest NewMask = Mask & fcNan;
  if (Mask & fcPosZero)
    NewMask |= fcZero;
  if (Mask & fcPosSubnormal)
    NewMask |= fcSubnormal;
  if (Mask & fcPosNormal)
    NewMask |= fcNormal;
  if (Mask & fcPosInf)
    NewMask |= fcInf;
  return NewMask;
}

FPClassTest llvm::unknown_sign(FPClassTest Mask) {
  FPClassTest NewMask = Mask & fcNan;
  if (Mask & fcZero)
    NewMask |= fcZero;
  if (Mask & fcSubnormal)
    NewMask |= fcSubnormal;
  if (Mask & fcNormal)
    NewMask |= fcNormal;
  if (Mask & fcInf)
    NewMask |= fcInf;
  return NewMask;
}

// Every bitfield has a unique name and one or more aliasing names that cover
// multiple bits. Names should be listed in order of preference, with higher
// popcounts listed first.
//
// Bits are consumed as printed. Each field should only be represented in one
// printed field.
static constexpr std::pair<FPClassTest, StringLiteral> NoFPClassName[] = {
  {fcAllFlags, "all"},
  {fcNan, "nan"},
  {fcSNan, "snan"},
  {fcQNan, "qnan"},
  {fcInf, "inf"},
  {fcNegInf, "ninf"},
  {fcPosInf, "pinf"},
  {fcZero, "zero"},
  {fcNegZero, "nzero"},
  {fcPosZero, "pzero"},
  {fcSubnormal, "sub"},
  {fcNegSubnormal, "nsub"},
  {fcPosSubnormal, "psub"},
  {fcNormal, "norm"},
  {fcNegNormal, "nnorm"},
  {fcPosNormal, "pnorm"}
};

raw_ostream &llvm::operator<<(raw_ostream &OS, FPClassTest Mask) {
  OS << '(';

  if (Mask == fcNone) {
    OS << "none)";
    return OS;
  }

  ListSeparator LS(" ");
  for (auto [BitTest, Name] : NoFPClassName) {
    if ((Mask & BitTest) == BitTest) {
      OS << LS << Name;

      // Clear the bits so we don't print any aliased names later.
      Mask &= ~BitTest;
    }
  }

  assert(Mask == 0 && "didn't print some mask bits");

  OS << ')';
  return OS;
}
