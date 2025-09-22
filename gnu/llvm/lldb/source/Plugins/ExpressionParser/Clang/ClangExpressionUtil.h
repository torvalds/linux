//===-- ClangExpressionUtil.h -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGEXPRESSIONUTIL_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGEXPRESSIONUTIL_H

#include "lldb/lldb-private.h"

namespace lldb_private {
namespace ClangExpressionUtil {
/// Returns a ValueObject for the lambda class in the current frame
///
/// To represent a lambda, Clang generates an artificial class
/// whose members are the captures and whose operator() is the
/// lambda implementation. If we capture a 'this' pointer,
/// the artifical class will contain a member variable named 'this'.
///
/// This method returns the 'this' pointer to the artificial lambda
/// class if a real 'this' was captured. Otherwise, returns nullptr.
lldb::ValueObjectSP GetLambdaValueObject(StackFrame *frame);

} // namespace ClangExpressionUtil
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGEXPRESSIONHELPER_H
