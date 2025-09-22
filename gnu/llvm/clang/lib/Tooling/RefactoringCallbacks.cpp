//===--- RefactoringCallbacks.cpp - Structural query framework ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//
#include "clang/Tooling/RefactoringCallbacks.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/Lexer.h"

using llvm::StringError;
using llvm::make_error;

namespace clang {
namespace tooling {

RefactoringCallback::RefactoringCallback() {}
tooling::Replacements &RefactoringCallback::getReplacements() {
  return Replace;
}

ASTMatchRefactorer::ASTMatchRefactorer(
    std::map<std::string, Replacements> &FileToReplaces)
    : FileToReplaces(FileToReplaces) {}

void ASTMatchRefactorer::addDynamicMatcher(
    const ast_matchers::internal::DynTypedMatcher &Matcher,
    RefactoringCallback *Callback) {
  MatchFinder.addDynamicMatcher(Matcher, Callback);
  Callbacks.push_back(Callback);
}

class RefactoringASTConsumer : public ASTConsumer {
public:
  explicit RefactoringASTConsumer(ASTMatchRefactorer &Refactoring)
      : Refactoring(Refactoring) {}

  void HandleTranslationUnit(ASTContext &Context) override {
    // The ASTMatchRefactorer is re-used between translation units.
    // Clear the matchers so that each Replacement is only emitted once.
    for (const auto &Callback : Refactoring.Callbacks) {
      Callback->getReplacements().clear();
    }
    Refactoring.MatchFinder.matchAST(Context);
    for (const auto &Callback : Refactoring.Callbacks) {
      for (const auto &Replacement : Callback->getReplacements()) {
        llvm::Error Err =
            Refactoring.FileToReplaces[std::string(Replacement.getFilePath())]
                .add(Replacement);
        if (Err) {
          llvm::errs() << "Skipping replacement " << Replacement.toString()
                       << " due to this error:\n"
                       << toString(std::move(Err)) << "\n";
        }
      }
    }
  }

private:
  ASTMatchRefactorer &Refactoring;
};

std::unique_ptr<ASTConsumer> ASTMatchRefactorer::newASTConsumer() {
  return std::make_unique<RefactoringASTConsumer>(*this);
}

static Replacement replaceStmtWithText(SourceManager &Sources, const Stmt &From,
                                       StringRef Text) {
  return tooling::Replacement(
      Sources, CharSourceRange::getTokenRange(From.getSourceRange()), Text);
}
static Replacement replaceStmtWithStmt(SourceManager &Sources, const Stmt &From,
                                       const Stmt &To) {
  return replaceStmtWithText(
      Sources, From,
      Lexer::getSourceText(CharSourceRange::getTokenRange(To.getSourceRange()),
                           Sources, LangOptions()));
}

ReplaceStmtWithText::ReplaceStmtWithText(StringRef FromId, StringRef ToText)
    : FromId(std::string(FromId)), ToText(std::string(ToText)) {}

void ReplaceStmtWithText::run(
    const ast_matchers::MatchFinder::MatchResult &Result) {
  if (const Stmt *FromMatch = Result.Nodes.getNodeAs<Stmt>(FromId)) {
    auto Err = Replace.add(tooling::Replacement(
        *Result.SourceManager,
        CharSourceRange::getTokenRange(FromMatch->getSourceRange()), ToText));
    // FIXME: better error handling. For now, just print error message in the
    // release version.
    if (Err) {
      llvm::errs() << llvm::toString(std::move(Err)) << "\n";
      assert(false);
    }
  }
}

ReplaceStmtWithStmt::ReplaceStmtWithStmt(StringRef FromId, StringRef ToId)
    : FromId(std::string(FromId)), ToId(std::string(ToId)) {}

void ReplaceStmtWithStmt::run(
    const ast_matchers::MatchFinder::MatchResult &Result) {
  const Stmt *FromMatch = Result.Nodes.getNodeAs<Stmt>(FromId);
  const Stmt *ToMatch = Result.Nodes.getNodeAs<Stmt>(ToId);
  if (FromMatch && ToMatch) {
    auto Err = Replace.add(
        replaceStmtWithStmt(*Result.SourceManager, *FromMatch, *ToMatch));
    // FIXME: better error handling. For now, just print error message in the
    // release version.
    if (Err) {
      llvm::errs() << llvm::toString(std::move(Err)) << "\n";
      assert(false);
    }
  }
}

ReplaceIfStmtWithItsBody::ReplaceIfStmtWithItsBody(StringRef Id,
                                                   bool PickTrueBranch)
    : Id(std::string(Id)), PickTrueBranch(PickTrueBranch) {}

void ReplaceIfStmtWithItsBody::run(
    const ast_matchers::MatchFinder::MatchResult &Result) {
  if (const IfStmt *Node = Result.Nodes.getNodeAs<IfStmt>(Id)) {
    const Stmt *Body = PickTrueBranch ? Node->getThen() : Node->getElse();
    if (Body) {
      auto Err =
          Replace.add(replaceStmtWithStmt(*Result.SourceManager, *Node, *Body));
      // FIXME: better error handling. For now, just print error message in the
      // release version.
      if (Err) {
        llvm::errs() << llvm::toString(std::move(Err)) << "\n";
        assert(false);
      }
    } else if (!PickTrueBranch) {
      // If we want to use the 'else'-branch, but it doesn't exist, delete
      // the whole 'if'.
      auto Err =
          Replace.add(replaceStmtWithText(*Result.SourceManager, *Node, ""));
      // FIXME: better error handling. For now, just print error message in the
      // release version.
      if (Err) {
        llvm::errs() << llvm::toString(std::move(Err)) << "\n";
        assert(false);
      }
    }
  }
}

ReplaceNodeWithTemplate::ReplaceNodeWithTemplate(
    llvm::StringRef FromId, std::vector<TemplateElement> Template)
    : FromId(std::string(FromId)), Template(std::move(Template)) {}

llvm::Expected<std::unique_ptr<ReplaceNodeWithTemplate>>
ReplaceNodeWithTemplate::create(StringRef FromId, StringRef ToTemplate) {
  std::vector<TemplateElement> ParsedTemplate;
  for (size_t Index = 0; Index < ToTemplate.size();) {
    if (ToTemplate[Index] == '$') {
      if (ToTemplate.substr(Index, 2) == "$$") {
        Index += 2;
        ParsedTemplate.push_back(
            TemplateElement{TemplateElement::Literal, "$"});
      } else if (ToTemplate.substr(Index, 2) == "${") {
        size_t EndOfIdentifier = ToTemplate.find("}", Index);
        if (EndOfIdentifier == std::string::npos) {
          return make_error<StringError>(
              "Unterminated ${...} in replacement template near " +
                  ToTemplate.substr(Index),
              llvm::inconvertibleErrorCode());
        }
        std::string SourceNodeName = std::string(
            ToTemplate.substr(Index + 2, EndOfIdentifier - Index - 2));
        ParsedTemplate.push_back(
            TemplateElement{TemplateElement::Identifier, SourceNodeName});
        Index = EndOfIdentifier + 1;
      } else {
        return make_error<StringError>(
            "Invalid $ in replacement template near " +
                ToTemplate.substr(Index),
            llvm::inconvertibleErrorCode());
      }
    } else {
      size_t NextIndex = ToTemplate.find('$', Index + 1);
      ParsedTemplate.push_back(TemplateElement{
          TemplateElement::Literal,
          std::string(ToTemplate.substr(Index, NextIndex - Index))});
      Index = NextIndex;
    }
  }
  return std::unique_ptr<ReplaceNodeWithTemplate>(
      new ReplaceNodeWithTemplate(FromId, std::move(ParsedTemplate)));
}

void ReplaceNodeWithTemplate::run(
    const ast_matchers::MatchFinder::MatchResult &Result) {
  const auto &NodeMap = Result.Nodes.getMap();

  std::string ToText;
  for (const auto &Element : Template) {
    switch (Element.Type) {
    case TemplateElement::Literal:
      ToText += Element.Value;
      break;
    case TemplateElement::Identifier: {
      auto NodeIter = NodeMap.find(Element.Value);
      if (NodeIter == NodeMap.end()) {
        llvm::errs() << "Node " << Element.Value
                     << " used in replacement template not bound in Matcher \n";
        llvm::report_fatal_error("Unbound node in replacement template.");
      }
      CharSourceRange Source =
          CharSourceRange::getTokenRange(NodeIter->second.getSourceRange());
      ToText += Lexer::getSourceText(Source, *Result.SourceManager,
                                     Result.Context->getLangOpts());
      break;
    }
    }
  }
  if (NodeMap.count(FromId) == 0) {
    llvm::errs() << "Node to be replaced " << FromId
                 << " not bound in query.\n";
    llvm::report_fatal_error("FromId node not bound in MatchResult");
  }
  auto Replacement =
      tooling::Replacement(*Result.SourceManager, &NodeMap.at(FromId), ToText,
                           Result.Context->getLangOpts());
  llvm::Error Err = Replace.add(Replacement);
  if (Err) {
    llvm::errs() << "Query and replace failed in " << Replacement.getFilePath()
                 << "! " << llvm::toString(std::move(Err)) << "\n";
    llvm::report_fatal_error("Replacement failed");
  }
}

} // end namespace tooling
} // end namespace clang
