//===---------- ExprMutationAnalyzer.h ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_EXPRMUTATIONANALYZER_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_EXPRMUTATIONANALYZER_H

#include "clang/ASTMatchers/ASTMatchers.h"
#include "llvm/ADT/DenseMap.h"
#include <memory>

namespace clang {

class FunctionParmMutationAnalyzer;

/// Analyzes whether any mutative operations are applied to an expression within
/// a given statement.
class ExprMutationAnalyzer {
  friend class FunctionParmMutationAnalyzer;

public:
  struct Memoized {
    using ResultMap = llvm::DenseMap<const Expr *, const Stmt *>;
    using FunctionParaAnalyzerMap =
        llvm::SmallDenseMap<const FunctionDecl *,
                            std::unique_ptr<FunctionParmMutationAnalyzer>>;

    ResultMap Results;
    ResultMap PointeeResults;
    FunctionParaAnalyzerMap FuncParmAnalyzer;

    void clear() {
      Results.clear();
      PointeeResults.clear();
      FuncParmAnalyzer.clear();
    }
  };
  struct Analyzer {
    Analyzer(const Stmt &Stm, ASTContext &Context, Memoized &Memorized)
        : Stm(Stm), Context(Context), Memorized(Memorized) {}

    const Stmt *findMutation(const Expr *Exp);
    const Stmt *findMutation(const Decl *Dec);

    const Stmt *findPointeeMutation(const Expr *Exp);
    const Stmt *findPointeeMutation(const Decl *Dec);
    static bool isUnevaluated(const Stmt *Smt, const Stmt &Stm,
                              ASTContext &Context);

  private:
    using MutationFinder = const Stmt *(Analyzer::*)(const Expr *);

    const Stmt *findMutationMemoized(const Expr *Exp,
                                     llvm::ArrayRef<MutationFinder> Finders,
                                     Memoized::ResultMap &MemoizedResults);
    const Stmt *tryEachDeclRef(const Decl *Dec, MutationFinder Finder);

    bool isUnevaluated(const Expr *Exp);

    const Stmt *findExprMutation(ArrayRef<ast_matchers::BoundNodes> Matches);
    const Stmt *findDeclMutation(ArrayRef<ast_matchers::BoundNodes> Matches);
    const Stmt *
    findExprPointeeMutation(ArrayRef<ast_matchers::BoundNodes> Matches);
    const Stmt *
    findDeclPointeeMutation(ArrayRef<ast_matchers::BoundNodes> Matches);

    const Stmt *findDirectMutation(const Expr *Exp);
    const Stmt *findMemberMutation(const Expr *Exp);
    const Stmt *findArrayElementMutation(const Expr *Exp);
    const Stmt *findCastMutation(const Expr *Exp);
    const Stmt *findRangeLoopMutation(const Expr *Exp);
    const Stmt *findReferenceMutation(const Expr *Exp);
    const Stmt *findFunctionArgMutation(const Expr *Exp);

    const Stmt &Stm;
    ASTContext &Context;
    Memoized &Memorized;
  };

  ExprMutationAnalyzer(const Stmt &Stm, ASTContext &Context)
      : Memorized(), A(Stm, Context, Memorized) {}

  bool isMutated(const Expr *Exp) { return findMutation(Exp) != nullptr; }
  bool isMutated(const Decl *Dec) { return findMutation(Dec) != nullptr; }
  const Stmt *findMutation(const Expr *Exp) { return A.findMutation(Exp); }
  const Stmt *findMutation(const Decl *Dec) { return A.findMutation(Dec); }

  bool isPointeeMutated(const Expr *Exp) {
    return findPointeeMutation(Exp) != nullptr;
  }
  bool isPointeeMutated(const Decl *Dec) {
    return findPointeeMutation(Dec) != nullptr;
  }
  const Stmt *findPointeeMutation(const Expr *Exp) {
    return A.findPointeeMutation(Exp);
  }
  const Stmt *findPointeeMutation(const Decl *Dec) {
    return A.findPointeeMutation(Dec);
  }

  static bool isUnevaluated(const Stmt *Smt, const Stmt &Stm,
                            ASTContext &Context) {
    return Analyzer::isUnevaluated(Smt, Stm, Context);
  }

private:
  Memoized Memorized;
  Analyzer A;
};

// A convenient wrapper around ExprMutationAnalyzer for analyzing function
// params.
class FunctionParmMutationAnalyzer {
public:
  static FunctionParmMutationAnalyzer *
  getFunctionParmMutationAnalyzer(const FunctionDecl &Func, ASTContext &Context,
                                  ExprMutationAnalyzer::Memoized &Memorized) {
    auto it = Memorized.FuncParmAnalyzer.find(&Func);
    if (it == Memorized.FuncParmAnalyzer.end())
      it =
          Memorized.FuncParmAnalyzer
              .try_emplace(&Func, std::unique_ptr<FunctionParmMutationAnalyzer>(
                                      new FunctionParmMutationAnalyzer(
                                          Func, Context, Memorized)))
              .first;
    return it->getSecond().get();
  }

  bool isMutated(const ParmVarDecl *Parm) {
    return findMutation(Parm) != nullptr;
  }
  const Stmt *findMutation(const ParmVarDecl *Parm);

private:
  ExprMutationAnalyzer::Analyzer BodyAnalyzer;
  llvm::DenseMap<const ParmVarDecl *, const Stmt *> Results;

  FunctionParmMutationAnalyzer(const FunctionDecl &Func, ASTContext &Context,
                               ExprMutationAnalyzer::Memoized &Memorized);
};

} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_ANALYSES_EXPRMUTATIONANALYZER_H
