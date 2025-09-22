//===--- Refactoring.h - Framework for clang refactoring tools --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  Interfaces supporting refactorings that span multiple translation units.
//  While single translation unit refactorings are supported via the Rewriter,
//  when refactoring multiple translation units changes must be stored in a
//  SourceManager independent form, duplicate changes need to be removed, and
//  all changes must be applied at once at the end of the refactoring so that
//  the code is always parseable.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_REFACTORING_H
#define LLVM_CLANG_TOOLING_REFACTORING_H

#include "clang/Tooling/Core/Replacement.h"
#include "clang/Tooling/Tooling.h"
#include <map>
#include <string>

namespace clang {

class Rewriter;

namespace tooling {

/// A tool to run refactorings.
///
/// This is a refactoring specific version of \see ClangTool. FrontendActions
/// passed to run() and runAndSave() should add replacements to
/// getReplacements().
class RefactoringTool : public ClangTool {
public:
  /// \see ClangTool::ClangTool.
  RefactoringTool(const CompilationDatabase &Compilations,
                  ArrayRef<std::string> SourcePaths,
                  std::shared_ptr<PCHContainerOperations> PCHContainerOps =
                      std::make_shared<PCHContainerOperations>());

  /// Returns the file path to replacements map to which replacements
  /// should be added during the run of the tool.
  std::map<std::string, Replacements> &getReplacements();

  /// Call run(), apply all generated replacements, and immediately save
  /// the results to disk.
  ///
  /// \returns 0 upon success. Non-zero upon failure.
  int runAndSave(FrontendActionFactory *ActionFactory);

  /// Apply all stored replacements to the given Rewriter.
  ///
  /// FileToReplaces will be deduplicated with `groupReplacementsByFile` before
  /// application.
  ///
  /// Replacement applications happen independently of the success of other
  /// applications.
  ///
  /// \returns true if all replacements apply. false otherwise.
  bool applyAllReplacements(Rewriter &Rewrite);

private:
  /// Write all refactored files to disk.
  int saveRewrittenFiles(Rewriter &Rewrite);

private:
  std::map<std::string, Replacements> FileToReplaces;
};

/// Groups \p Replaces by the file path and applies each group of
/// Replacements on the related file in \p Rewriter. In addition to applying
/// given Replacements, this function also formats the changed code.
///
/// \pre Replacements must be conflict-free.
///
/// FileToReplaces will be deduplicated with `groupReplacementsByFile` before
/// application.
///
/// Replacement applications happen independently of the success of other
/// applications.
///
/// \param[in] FileToReplaces Replacements (grouped by files) to apply.
/// \param[in] Rewrite The `Rewritter` to apply replacements on.
/// \param[in] Style The style name used for reformatting. See ```getStyle``` in
/// "include/clang/Format/Format.h" for all possible style forms.
///
/// \returns true if all replacements applied and formatted. false otherwise.
bool formatAndApplyAllReplacements(
    const std::map<std::string, Replacements> &FileToReplaces,
    Rewriter &Rewrite, StringRef Style = "file");

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_REFACTORING_H
