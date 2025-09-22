//== Checker.cpp - Registration mechanism for checkers -----------*- C++ -*--=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines Checker, used to create and register checkers.
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"
#include "clang/StaticAnalyzer/Core/Checker.h"

using namespace clang;
using namespace ento;

int ImplicitNullDerefEvent::Tag;

StringRef CheckerBase::getTagDescription() const {
  return getCheckerName().getName();
}

CheckerNameRef CheckerBase::getCheckerName() const { return Name; }

CheckerProgramPointTag::CheckerProgramPointTag(StringRef CheckerName,
                                               StringRef Msg)
  : SimpleProgramPointTag(CheckerName, Msg) {}

CheckerProgramPointTag::CheckerProgramPointTag(const CheckerBase *Checker,
                                               StringRef Msg)
    : SimpleProgramPointTag(Checker->getCheckerName().getName(), Msg) {}

raw_ostream& clang::ento::operator<<(raw_ostream &Out,
                                     const CheckerBase &Checker) {
  Out << Checker.getCheckerName().getName();
  return Out;
}
