//===--- RefactoringCallbacks.h - Structural query framework ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Provides callbacks to make common kinds of refactorings easy.
//
//  The general idea is to construct a matcher expression that describes a
//  subtree match on the AST and then replace the corresponding source code
//  either by some specific text or some other AST node.
//
//  Example:
//  int main(int argc, char **argv) {
//    ClangTool Tool(argc, argv);
//    MatchFinder Finder;
//    ReplaceStmtWithText Callback("integer", "42");
//    Finder.AddMatcher(id("integer", expression(integerLiteral())), Callback);
//    return Tool.run(newFrontendActionFactory(&Finder));
//  }
//
//  This will replace all integer literals with "42".
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_REFACTORINGCALLBACKS_H
#define LLVM_CLANG_TOOLING_REFACTORINGCALLBACKS_H

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Tooling/Refactoring.h"

namespace clang {
namespace tooling {

/// Base class for RefactoringCallbacks.
///
/// Collects \c tooling::Replacements while running.
class RefactoringCallback : public ast_matchers::MatchFinder::MatchCallback {
public:
  RefactoringCallback();
  Replacements &getReplacements();

protected:
  Replacements Replace;
};

/// Adaptor between \c ast_matchers::MatchFinder and \c
/// tooling::RefactoringTool.
///
/// Runs AST matchers and stores the \c tooling::Replacements in a map.
class ASTMatchRefactorer {
public:
  explicit ASTMatchRefactorer(
    std::map<std::string, Replacements> &FileToReplaces);

  template <typename T>
  void addMatcher(const T &Matcher, RefactoringCallback *Callback) {
    MatchFinder.addMatcher(Matcher, Callback);
    Callbacks.push_back(Callback);
  }

  void addDynamicMatcher(const ast_matchers::internal::DynTypedMatcher &Matcher,
                         RefactoringCallback *Callback);

  std::unique_ptr<ASTConsumer> newASTConsumer();

private:
  friend class RefactoringASTConsumer;
  std::vector<RefactoringCallback *> Callbacks;
  ast_matchers::MatchFinder MatchFinder;
  std::map<std::string, Replacements> &FileToReplaces;
};

/// Replace the text of the statement bound to \c FromId with the text in
/// \c ToText.
class ReplaceStmtWithText : public RefactoringCallback {
public:
  ReplaceStmtWithText(StringRef FromId, StringRef ToText);
  void run(const ast_matchers::MatchFinder::MatchResult &Result) override;

private:
  std::string FromId;
  std::string ToText;
};

/// Replace the text of an AST node bound to \c FromId with the result of
/// evaluating the template in \c ToTemplate.
///
/// Expressions of the form ${NodeName} in \c ToTemplate will be
/// replaced by the text of the node bound to ${NodeName}. The string
/// "$$" will be replaced by "$".
class ReplaceNodeWithTemplate : public RefactoringCallback {
public:
  static llvm::Expected<std::unique_ptr<ReplaceNodeWithTemplate>>
  create(StringRef FromId, StringRef ToTemplate);
  void run(const ast_matchers::MatchFinder::MatchResult &Result) override;

private:
  struct TemplateElement {
    enum { Literal, Identifier } Type;
    std::string Value;
  };
  ReplaceNodeWithTemplate(llvm::StringRef FromId,
                          std::vector<TemplateElement> Template);
  std::string FromId;
  std::vector<TemplateElement> Template;
};

/// Replace the text of the statement bound to \c FromId with the text of
/// the statement bound to \c ToId.
class ReplaceStmtWithStmt : public RefactoringCallback {
public:
  ReplaceStmtWithStmt(StringRef FromId, StringRef ToId);
  void run(const ast_matchers::MatchFinder::MatchResult &Result) override;

private:
  std::string FromId;
  std::string ToId;
};

/// Replace an if-statement bound to \c Id with the outdented text of its
/// body, choosing the consequent or the alternative based on whether
/// \c PickTrueBranch is true.
class ReplaceIfStmtWithItsBody : public RefactoringCallback {
public:
  ReplaceIfStmtWithItsBody(StringRef Id, bool PickTrueBranch);
  void run(const ast_matchers::MatchFinder::MatchResult &Result) override;

private:
  std::string Id;
  const bool PickTrueBranch;
};

} // end namespace tooling
} // end namespace clang

#endif
