//===- PassInstrumentation.cpp - Pass Instrumentation interface -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file provides the implementation of PassInstrumentation class.
///
//===----------------------------------------------------------------------===//

#include "llvm/IR/PassInstrumentation.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

void PassInstrumentationCallbacks::addClassToPassName(StringRef ClassName,
                                                      StringRef PassName) {
  ClassToPassName.try_emplace(ClassName, PassName.str());
}

StringRef
PassInstrumentationCallbacks::getPassNameForClassName(StringRef ClassName) {
  if (!ClassToPassNameCallbacks.empty()) {
    for (auto &Fn : ClassToPassNameCallbacks)
      Fn();
    ClassToPassNameCallbacks.clear();
  }
  return ClassToPassName[ClassName];
}

AnalysisKey PassInstrumentationAnalysis::Key;

bool isSpecialPass(StringRef PassID, const std::vector<StringRef> &Specials) {
  size_t Pos = PassID.find('<');
  StringRef Prefix = PassID;
  if (Pos != StringRef::npos)
    Prefix = PassID.substr(0, Pos);
  return any_of(Specials,
                [Prefix](StringRef S) { return Prefix.ends_with(S); });
}

} // namespace llvm
