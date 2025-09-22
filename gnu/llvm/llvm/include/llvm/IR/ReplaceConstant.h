//===- ReplaceConstant.h - Replacing LLVM constant expressions --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the utility function for replacing LLVM constant
// expressions by instructions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_REPLACECONSTANT_H
#define LLVM_IR_REPLACECONSTANT_H

namespace llvm {

template <typename T> class ArrayRef;
class Constant;
class Function;

/// Replace constant expressions users of the given constants with
/// instructions. Return whether anything was changed.
///
/// Passing RestrictToFunc will restrict the constant replacement
/// to the passed in functions scope, as opposed to the replacements
/// occurring at module scope.
///
/// RemoveDeadConstants by default will remove all dead constants as
/// the final step of the function after replacement, when passed
/// false it will skip this final step.
///
/// If \p IncludeSelf is enabled, also convert the passed constants themselves
/// to instructions, rather than only their users.
bool convertUsersOfConstantsToInstructions(ArrayRef<Constant *> Consts,
                                           Function *RestrictToFunc = nullptr,
                                           bool RemoveDeadConstants = true,
                                           bool IncludeSelf = false);

} // end namespace llvm

#endif // LLVM_IR_REPLACECONSTANT_H
