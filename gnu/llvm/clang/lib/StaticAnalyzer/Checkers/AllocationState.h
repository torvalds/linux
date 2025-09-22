//===--- AllocationState.h ------------------------------------- *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_ALLOCATIONSTATE_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_ALLOCATIONSTATE_H

#include "clang/StaticAnalyzer/Core/BugReporter/BugReporterVisitors.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/ProgramState.h"

namespace clang {
namespace ento {

namespace allocation_state {

ProgramStateRef markReleased(ProgramStateRef State, SymbolRef Sym,
                             const Expr *Origin);

/// This function provides an additional visitor that augments the bug report
/// with information relevant to memory errors caused by the misuse of
/// AF_InnerBuffer symbols.
std::unique_ptr<BugReporterVisitor> getInnerPointerBRVisitor(SymbolRef Sym);

/// 'Sym' represents a pointer to the inner buffer of a container object.
/// This function looks up the memory region of that object in
/// DanglingInternalBufferChecker's program state map.
const MemRegion *getContainerObjRegion(ProgramStateRef State, SymbolRef Sym);

} // end namespace allocation_state

} // end namespace ento
} // end namespace clang

#endif
