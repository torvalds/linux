//===--- EnterExpressionEvaluationContext.h ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_ENTEREXPRESSIONEVALUATIONCONTEXT_H
#define LLVM_CLANG_SEMA_ENTEREXPRESSIONEVALUATIONCONTEXT_H

#include "clang/Sema/Sema.h"

namespace clang {

class Decl;

/// RAII object that enters a new expression evaluation context.
class EnterExpressionEvaluationContext {
  Sema &Actions;
  bool Entered = true;

public:
  EnterExpressionEvaluationContext(
      Sema &Actions, Sema::ExpressionEvaluationContext NewContext,
      Decl *LambdaContextDecl = nullptr,
      Sema::ExpressionEvaluationContextRecord::ExpressionKind ExprContext =
          Sema::ExpressionEvaluationContextRecord::EK_Other,
      bool ShouldEnter = true)
      : Actions(Actions), Entered(ShouldEnter) {
    if (Entered)
      Actions.PushExpressionEvaluationContext(NewContext, LambdaContextDecl,
                                              ExprContext);
  }
  EnterExpressionEvaluationContext(
      Sema &Actions, Sema::ExpressionEvaluationContext NewContext,
      Sema::ReuseLambdaContextDecl_t,
      Sema::ExpressionEvaluationContextRecord::ExpressionKind ExprContext =
          Sema::ExpressionEvaluationContextRecord::EK_Other)
      : Actions(Actions) {
    Actions.PushExpressionEvaluationContext(
        NewContext, Sema::ReuseLambdaContextDecl, ExprContext);
  }

  enum InitListTag { InitList };
  EnterExpressionEvaluationContext(Sema &Actions, InitListTag,
                                   bool ShouldEnter = true)
      : Actions(Actions), Entered(false) {
    // In C++11 onwards, narrowing checks are performed on the contents of
    // braced-init-lists, even when they occur within unevaluated operands.
    // Therefore we still need to instantiate constexpr functions used in such
    // a context.
    if (ShouldEnter && Actions.isUnevaluatedContext() &&
        Actions.getLangOpts().CPlusPlus11) {
      Actions.PushExpressionEvaluationContext(
          Sema::ExpressionEvaluationContext::UnevaluatedList);
      Entered = true;
    }
  }

  ~EnterExpressionEvaluationContext() {
    if (Entered)
      Actions.PopExpressionEvaluationContext();
  }
};

} // namespace clang

#endif
