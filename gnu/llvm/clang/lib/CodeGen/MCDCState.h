//===---- MCDCState.h - Per-Function MC/DC state ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Per-Function MC/DC state for PGO
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CODEGEN_MCDCSTATE_H
#define LLVM_CLANG_LIB_CODEGEN_MCDCSTATE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ProfileData/Coverage/MCDCTypes.h"

namespace clang {
class Stmt;
} // namespace clang

namespace clang::CodeGen::MCDC {

using namespace llvm::coverage::mcdc;

/// Per-Function MC/DC state
struct State {
  unsigned BitmapBits = 0;

  struct Decision {
    unsigned BitmapIdx;
    llvm::SmallVector<std::array<int, 2>> Indices;
  };

  llvm::DenseMap<const Stmt *, Decision> DecisionByStmt;

  struct Branch {
    ConditionID ID;
    const Stmt *DecisionStmt;
  };

  llvm::DenseMap<const Stmt *, Branch> BranchByStmt;
};

} // namespace clang::CodeGen::MCDC

#endif // LLVM_CLANG_LIB_CODEGEN_MCDCSTATE_H
