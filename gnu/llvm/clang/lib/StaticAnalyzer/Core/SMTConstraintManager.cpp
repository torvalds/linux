//== SMTConstraintManager.cpp -----------------------------------*- C++ -*--==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/StaticAnalyzer/Core/PathSensitive/SMTConstraintManager.h"

using namespace clang;
using namespace ento;

std::unique_ptr<ConstraintManager>
ento::CreateZ3ConstraintManager(ProgramStateManager &StMgr, ExprEngine *Eng) {
  return std::make_unique<SMTConstraintManager>(Eng, StMgr.getSValBuilder());
}
