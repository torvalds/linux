//===--- LoopWidening.h - Widen loops ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This header contains the declarations of functions which are used to widen
/// loops which do not otherwise exit. The widening is done by invalidating
/// anything which might be modified by the body of the loop.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_LOOPWIDENING_H
#define LLVM_CLANG_STATICANALYZER_CORE_PATHSENSITIVE_LOOPWIDENING_H

#include "clang/Analysis/CFG.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"

namespace clang {
namespace ento {

/// Get the states that result from widening the loop.
///
/// Widen the loop by invalidating anything that might be modified
/// by the loop body in any iteration.
ProgramStateRef getWidenedLoopState(ProgramStateRef PrevState,
                                    const LocationContext *LCtx,
                                    unsigned BlockCount, const Stmt *LoopStmt);

} // end namespace ento
} // end namespace clang

#endif
