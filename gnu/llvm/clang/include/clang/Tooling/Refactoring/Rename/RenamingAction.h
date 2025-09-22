//===--- RenamingAction.h - Clang refactoring library ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Provides an action to rename every symbol at a point.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_REFACTORING_RENAME_RENAMINGACTION_H
#define LLVM_CLANG_TOOLING_REFACTORING_RENAME_RENAMINGACTION_H

#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Refactoring/AtomicChange.h"
#include "clang/Tooling/Refactoring/RefactoringActionRules.h"
#include "clang/Tooling/Refactoring/RefactoringOptions.h"
#include "clang/Tooling/Refactoring/Rename/SymbolOccurrences.h"
#include "llvm/Support/Error.h"

namespace clang {
class ASTConsumer;

namespace tooling {

class RenamingAction {
public:
  RenamingAction(const std::vector<std::string> &NewNames,
                 const std::vector<std::string> &PrevNames,
                 const std::vector<std::vector<std::string>> &USRList,
                 std::map<std::string, tooling::Replacements> &FileToReplaces,
                 bool PrintLocations = false)
      : NewNames(NewNames), PrevNames(PrevNames), USRList(USRList),
        FileToReplaces(FileToReplaces), PrintLocations(PrintLocations) {}

  std::unique_ptr<ASTConsumer> newASTConsumer();

private:
  const std::vector<std::string> &NewNames, &PrevNames;
  const std::vector<std::vector<std::string>> &USRList;
  std::map<std::string, tooling::Replacements> &FileToReplaces;
  bool PrintLocations;
};

class RenameOccurrences final : public SourceChangeRefactoringRule {
public:
  static Expected<RenameOccurrences> initiate(RefactoringRuleContext &Context,
                                              SourceRange SelectionRange,
                                              std::string NewName);

  static const RefactoringDescriptor &describe();

  const NamedDecl *getRenameDecl() const;

private:
  RenameOccurrences(const NamedDecl *ND, std::string NewName)
      : ND(ND), NewName(std::move(NewName)) {}

  Expected<AtomicChanges>
  createSourceReplacements(RefactoringRuleContext &Context) override;

  const NamedDecl *ND;
  std::string NewName;
};

class QualifiedRenameRule final : public SourceChangeRefactoringRule {
public:
  static Expected<QualifiedRenameRule> initiate(RefactoringRuleContext &Context,
                                                std::string OldQualifiedName,
                                                std::string NewQualifiedName);

  static const RefactoringDescriptor &describe();

private:
  QualifiedRenameRule(const NamedDecl *ND,
                      std::string NewQualifiedName)
      : ND(ND), NewQualifiedName(std::move(NewQualifiedName)) {}

  Expected<AtomicChanges>
  createSourceReplacements(RefactoringRuleContext &Context) override;

  // A NamedDecl which identifies the symbol being renamed.
  const NamedDecl *ND;
  // The new qualified name to change the symbol to.
  std::string NewQualifiedName;
};

/// Returns source replacements that correspond to the rename of the given
/// symbol occurrences.
llvm::Expected<std::vector<AtomicChange>>
createRenameReplacements(const SymbolOccurrences &Occurrences,
                         const SourceManager &SM, const SymbolName &NewName);

/// Rename all symbols identified by the given USRs.
class QualifiedRenamingAction {
public:
  QualifiedRenamingAction(
      const std::vector<std::string> &NewNames,
      const std::vector<std::vector<std::string>> &USRList,
      std::map<std::string, tooling::Replacements> &FileToReplaces)
      : NewNames(NewNames), USRList(USRList), FileToReplaces(FileToReplaces) {}

  std::unique_ptr<ASTConsumer> newASTConsumer();

private:
  /// New symbol names.
  const std::vector<std::string> &NewNames;

  /// A list of USRs. Each element represents USRs of a symbol being renamed.
  const std::vector<std::vector<std::string>> &USRList;

  /// A file path to replacements map.
  std::map<std::string, tooling::Replacements> &FileToReplaces;
};

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_REFACTORING_RENAME_RENAMINGACTION_H
