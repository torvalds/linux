//===- llvm/CodeGen/GlobalISel/RegisterBank.cpp - Register Bank --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the RegisterBank class.
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/RegisterBank.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/CodeGen/RegisterBankInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "registerbank"

using namespace llvm;

bool RegisterBank::verify(const RegisterBankInfo &RBI,
                          const TargetRegisterInfo &TRI) const {
  for (unsigned RCId = 0, End = TRI.getNumRegClasses(); RCId != End; ++RCId) {
    const TargetRegisterClass &RC = *TRI.getRegClass(RCId);

    if (!covers(RC))
      continue;
    // Verify that the register bank covers all the sub classes of the
    // classes it covers.

    // Use a different (slow in that case) method than
    // RegisterBankInfo to find the subclasses of RC, to make sure
    // both agree on the covers.
    for (unsigned SubRCId = 0; SubRCId != End; ++SubRCId) {
      const TargetRegisterClass &SubRC = *TRI.getRegClass(RCId);

      if (!RC.hasSubClassEq(&SubRC))
        continue;

      // Verify that the Size of the register bank is big enough to cover
      // all the register classes it covers.
      assert(RBI.getMaximumSize(getID()) >= TRI.getRegSizeInBits(SubRC) &&
             "Size is not big enough for all the subclasses!");
      assert(covers(SubRC) && "Not all subclasses are covered");
    }
  }
  return true;
}

bool RegisterBank::covers(const TargetRegisterClass &RC) const {
  return (CoveredClasses[RC.getID() / 32] & (1U << RC.getID() % 32)) != 0;
}

bool RegisterBank::operator==(const RegisterBank &OtherRB) const {
  // There must be only one instance of a given register bank alive
  // for the whole compilation.
  // The RegisterBankInfo is supposed to enforce that.
  assert((OtherRB.getID() != getID() || &OtherRB == this) &&
         "ID does not uniquely identify a RegisterBank");
  return &OtherRB == this;
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void RegisterBank::dump(const TargetRegisterInfo *TRI) const {
  print(dbgs(), /* IsForDebug */ true, TRI);
}
#endif

void RegisterBank::print(raw_ostream &OS, bool IsForDebug,
                         const TargetRegisterInfo *TRI) const {
  OS << getName();
  if (!IsForDebug)
    return;

  unsigned Count = 0;
  for (int i = 0, e = ((NumRegClasses + 31) / 32); i != e; ++i)
    Count += llvm::popcount(CoveredClasses[i]);

  OS << "(ID:" << getID() << ")\n"
     << "Number of Covered register classes: " << Count << '\n';
  // Print all the subclasses if we can.
  // This register classes may not be properly initialized yet.
  if (!TRI || NumRegClasses == 0)
    return;
  assert(NumRegClasses == TRI->getNumRegClasses() &&
         "TRI does not match the initialization process?");
  OS << "Covered register classes:\n";
  ListSeparator LS;
  for (unsigned RCId = 0, End = TRI->getNumRegClasses(); RCId != End; ++RCId) {
    const TargetRegisterClass &RC = *TRI->getRegClass(RCId);

    if (covers(RC))
      OS << LS << TRI->getRegClassName(&RC);
  }
}
