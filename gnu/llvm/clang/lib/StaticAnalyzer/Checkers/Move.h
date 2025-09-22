//=== Move.h - Tracking moved-from objects. ------------------------*- C++ -*-//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines inter-checker API for the use-after-move checker. It allows
// dependent checkers to figure out if an object is in a moved-from state.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_MOVE_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_MOVE_H

#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"

namespace clang {
namespace ento {
namespace move {

/// Returns true if the object is known to have been recently std::moved.
bool isMovedFrom(ProgramStateRef State, const MemRegion *Region);

} // namespace move
} // namespace ento
} // namespace clang

#endif // LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_MOVE_H
