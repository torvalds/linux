//===-- ValueLatticeUtils.h - Utils for solving lattices --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares common functions useful for performing data-flow analyses
// that propagate values across function boundaries.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_VALUELATTICEUTILS_H
#define LLVM_ANALYSIS_VALUELATTICEUTILS_H

namespace llvm {

class Function;
class GlobalVariable;

/// Determine if the values of the given function's arguments can be tracked
/// interprocedurally. The value of an argument can be tracked if the function
/// has local linkage and its address is not taken.
bool canTrackArgumentsInterprocedurally(Function *F);

/// Determine if the values of the given function's returns can be tracked
/// interprocedurally. Return values can be tracked if the function has an
/// exact definition and it doesn't have the "naked" attribute. Naked functions
/// may contain assembly code that returns untrackable values.
bool canTrackReturnsInterprocedurally(Function *F);

/// Determine if the value maintained in the given global variable can be
/// tracked interprocedurally. A value can be tracked if the global variable
/// has local linkage and is only used by non-volatile loads and stores.
bool canTrackGlobalVariableInterprocedurally(GlobalVariable *GV);

} // end namespace llvm

#endif // LLVM_ANALYSIS_VALUELATTICEUTILS_H
