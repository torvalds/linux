//===--- Phases.cpp - Transformations on Driver Types ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/Phases.h"
#include "llvm/Support/ErrorHandling.h"
#include <cassert>

using namespace clang::driver;

const char *phases::getPhaseName(ID Id) {
  switch (Id) {
  case Preprocess: return "preprocessor";
  case Precompile: return "precompiler";
  case Compile: return "compiler";
  case Backend: return "backend";
  case Assemble: return "assembler";
  case Link: return "linker";
  case IfsMerge: return "ifsmerger";
  }

  llvm_unreachable("Invalid phase id.");
}
