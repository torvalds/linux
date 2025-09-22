//===--- TestSupport.h - Clang-based refactoring tool -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Declares datatypes and routines that are used by test-specific code
/// in clang-refactor.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_CLANG_REFACTOR_TEST_SUPPORT_H
#define LLVM_CLANG_TOOLS_CLANG_REFACTOR_TEST_SUPPORT_H

#include "ToolRefactoringResultConsumer.h"
#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Error.h"
#include <map>
#include <optional>
#include <string>

namespace clang {

class SourceManager;

namespace refactor {

/// A source selection range that's specified in a test file using an inline
/// command in the comment. These commands can take the following forms:
///
/// - /*range=*/ will create an empty selection range in the default group
///   right after the comment.
/// - /*range a=*/ will create an empty selection range in the 'a' group right
///   after the comment.
/// - /*range = +1*/ will create an empty selection range at a location that's
///   right after the comment with one offset to the column.
/// - /*range= -> +2:3*/ will create a selection range that starts at the
///   location right after the comment, and ends at column 3 of the 2nd line
///   after the line of the starting location.
///
/// Clang-refactor will expected all ranges in one test group to produce
/// identical results.
struct TestSelectionRange {
  unsigned Begin, End;
};

/// A set of test selection ranges specified in one file.
struct TestSelectionRangesInFile {
  std::string Filename;
  struct RangeGroup {
    std::string Name;
    SmallVector<TestSelectionRange, 8> Ranges;
  };
  std::vector<RangeGroup> GroupedRanges;

  bool foreachRange(const SourceManager &SM,
                    llvm::function_ref<void(SourceRange)> Callback) const;

  std::unique_ptr<ClangRefactorToolConsumerInterface> createConsumer() const;

  void dump(llvm::raw_ostream &OS) const;
};

/// Extracts the grouped selection ranges from the file that's specified in
/// the -selection=test:<filename> option.
///
/// The grouped ranges are specified in comments using the following syntax:
/// "range" [ group-name ] "=" [ "+" starting-column-offset ] [ "->"
///                              "+" ending-line-offset ":"
///                                  ending-column-position ]
///
/// The selection range is then computed from this command by taking the ending
/// location of the comment, and adding 'starting-column-offset' to the column
/// for that location. That location in turns becomes the whole selection range,
/// unless 'ending-line-offset' and 'ending-column-position' are specified. If
/// they are specified, then the ending location of the selection range is
/// the starting location's line + 'ending-line-offset' and the
/// 'ending-column-position' column.
///
/// All selection ranges in one group are expected to produce the same
/// refactoring result.
///
/// When testing, zero is returned from clang-refactor even when a group
/// produces an initiation error, which is different from normal invocation
/// that returns a non-zero value. This is done on purpose, to ensure that group
/// consistency checks can return non-zero, but still print the output of
/// the group. So even if a test matches the output of group, it will still fail
/// because clang-refactor should return zero on exit when the group results are
/// consistent.
///
/// \returns std::nullopt on failure (errors are emitted to stderr), or a set of
/// grouped source ranges in the given file otherwise.
std::optional<TestSelectionRangesInFile>
findTestSelectionRanges(StringRef Filename);

} // end namespace refactor
} // end namespace clang

#endif // LLVM_CLANG_TOOLS_CLANG_REFACTOR_TEST_SUPPORT_H
