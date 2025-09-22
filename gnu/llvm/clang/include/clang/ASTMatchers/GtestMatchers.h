//===- GtestMatchers.h - AST Matchers for GTest -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements matchers specific to structures in the Googletest
//  (gtest) framework.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ASTMATCHERS_GTESTMATCHERS_H
#define LLVM_CLANG_ASTMATCHERS_GTESTMATCHERS_H

#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "llvm/ADT/StringRef.h"

namespace clang {
namespace ast_matchers {

/// Gtest's comparison operations.
enum class GtestCmp {
  Eq,
  Ne,
  Ge,
  Gt,
  Le,
  Lt,
};

/// This enum indicates whether the mock method in the matched ON_CALL or
/// EXPECT_CALL macro has arguments. For example, `None` can be used to match
/// `ON_CALL(mock, TwoParamMethod)` whereas `Some` can be used to match
/// `ON_CALL(mock, TwoParamMethod(m1, m2))`.
enum class MockArgs {
  None,
  Some,
};

/// Matcher for gtest's ASSERT comparison macros including ASSERT_EQ, ASSERT_NE,
/// ASSERT_GE, ASSERT_GT, ASSERT_LE and ASSERT_LT.
internal::BindableMatcher<Stmt> gtestAssert(GtestCmp Cmp, StatementMatcher Left,
                                            StatementMatcher Right);

/// Matcher for gtest's ASSERT_THAT macro.
internal::BindableMatcher<Stmt> gtestAssertThat(StatementMatcher Actual,
                                                StatementMatcher Matcher);

/// Matcher for gtest's EXPECT comparison macros including EXPECT_EQ, EXPECT_NE,
/// EXPECT_GE, EXPECT_GT, EXPECT_LE and EXPECT_LT.
internal::BindableMatcher<Stmt> gtestExpect(GtestCmp Cmp, StatementMatcher Left,
                                            StatementMatcher Right);

/// Matcher for gtest's EXPECT_THAT macro.
internal::BindableMatcher<Stmt> gtestExpectThat(StatementMatcher Actual,
                                                StatementMatcher Matcher);

/// Matcher for gtest's EXPECT_CALL macro. `MockObject` matches the mock
/// object and `MockMethodName` is the name of the method invoked on the mock
/// object.
internal::BindableMatcher<Stmt> gtestExpectCall(StatementMatcher MockObject,
                                                llvm::StringRef MockMethodName,
                                                MockArgs Args);

/// Matcher for gtest's EXPECT_CALL macro. `MockCall` matches the whole mock
/// member method call. This API is more flexible but requires more knowledge of
/// the AST structure of EXPECT_CALL macros.
internal::BindableMatcher<Stmt> gtestExpectCall(StatementMatcher MockCall,
                                                MockArgs Args);

/// Like the first `gtestExpectCall` overload but for `ON_CALL`.
internal::BindableMatcher<Stmt> gtestOnCall(StatementMatcher MockObject,
                                            llvm::StringRef MockMethodName,
                                            MockArgs Args);

/// Like the second `gtestExpectCall` overload but for `ON_CALL`.
internal::BindableMatcher<Stmt> gtestOnCall(StatementMatcher MockCall,
                                            MockArgs Args);

} // namespace ast_matchers
} // namespace clang

#endif // LLVM_CLANG_ASTMATCHERS_GTESTMATCHERS_H

