//===--- SemaLambda.h - Lambda Helper Functions --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides some common utility functions for processing
/// Lambdas.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMALAMBDA_H
#define LLVM_CLANG_SEMA_SEMALAMBDA_H

#include "clang/AST/ASTLambda.h"
#include <optional>

namespace clang {
namespace sema {
class FunctionScopeInfo;
}
class Sema;

/// Examines the FunctionScopeInfo stack to determine the nearest
/// enclosing lambda (to the current lambda) that is 'capture-capable' for
/// the variable referenced in the current lambda (i.e. \p VarToCapture).
/// If successful, returns the index into Sema's FunctionScopeInfo stack
/// of the capture-capable lambda's LambdaScopeInfo.
/// See Implementation for more detailed comments.

std::optional<unsigned> getStackIndexOfNearestEnclosingCaptureCapableLambda(
    ArrayRef<const sema::FunctionScopeInfo *> FunctionScopes,
    ValueDecl *VarToCapture, Sema &S);

} // clang

#endif
