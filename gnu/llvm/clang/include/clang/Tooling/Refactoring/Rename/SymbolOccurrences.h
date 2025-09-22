//===--- SymbolOccurrences.h - Clang refactoring library ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_REFACTORING_RENAME_SYMBOLOCCURRENCES_H
#define LLVM_CLANG_TOOLING_REFACTORING_RENAME_SYMBOLOCCURRENCES_H

#include "clang/Basic/LLVM.h"
#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include <vector>

namespace clang {
namespace tooling {

class SymbolName;

/// An occurrence of a symbol in the source.
///
/// Occurrences can have difference kinds, that describe whether this occurrence
/// is an exact semantic match, or whether this is a weaker textual match that's
/// not guaranteed to represent the exact declaration.
///
/// A single occurrence of a symbol can span more than one source range. For
/// example, Objective-C selectors can contain multiple argument labels:
///
/// \code
/// [object selectorPiece1: ... selectorPiece2: ...];
/// //      ^~~ range 0 ~~      ^~~ range 1 ~~
/// \endcode
///
/// We have to replace the text in both range 0 and range 1 when renaming the
/// Objective-C method 'selectorPiece1:selectorPiece2'.
class SymbolOccurrence {
public:
  enum OccurrenceKind {
    /// This occurrence is an exact match and can be renamed automatically.
    ///
    /// Note:
    /// Symbol occurrences in macro arguments that expand to different
    /// declarations get marked as exact matches, and thus the renaming engine
    /// will rename them e.g.:
    ///
    /// \code
    ///   #define MACRO(x) x + ns::x
    ///   int foo(int var) {
    ///     return MACRO(var); // var is renamed automatically here when
    ///                        // either var or ns::var is renamed.
    ///   };
    /// \endcode
    ///
    /// The user will have to fix their code manually after performing such a
    /// rename.
    /// FIXME: The rename verifier should notify user about this issue.
    MatchingSymbol
  };

  SymbolOccurrence(const SymbolName &Name, OccurrenceKind Kind,
                   ArrayRef<SourceLocation> Locations);

  SymbolOccurrence(SymbolOccurrence &&) = default;
  SymbolOccurrence &operator=(SymbolOccurrence &&) = default;

  OccurrenceKind getKind() const { return Kind; }

  ArrayRef<SourceRange> getNameRanges() const {
    if (MultipleRanges)
      return llvm::ArrayRef(MultipleRanges.get(), NumRanges);
    return SingleRange;
  }

private:
  OccurrenceKind Kind;
  std::unique_ptr<SourceRange[]> MultipleRanges;
  union {
    SourceRange SingleRange;
    unsigned NumRanges;
  };
};

using SymbolOccurrences = std::vector<SymbolOccurrence>;

} // end namespace tooling
} // end namespace clang

#endif // LLVM_CLANG_TOOLING_REFACTORING_RENAME_SYMBOLOCCURRENCES_H
