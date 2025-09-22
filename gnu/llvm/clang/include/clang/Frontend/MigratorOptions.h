//===--- MigratorOptions.h - MigratorOptions Options ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header contains the structures necessary for a front-end to specify
// various migration analysis.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_MIGRATOROPTIONS_H
#define LLVM_CLANG_FRONTEND_MIGRATOROPTIONS_H

#include "llvm/Support/Compiler.h"

namespace clang {

class MigratorOptions {
public:
  LLVM_PREFERRED_TYPE(bool)
  unsigned NoNSAllocReallocError : 1;
  LLVM_PREFERRED_TYPE(bool)
  unsigned NoFinalizeRemoval : 1;
  MigratorOptions() {
    NoNSAllocReallocError = 0;
    NoFinalizeRemoval = 0;
  }
};

}
#endif
