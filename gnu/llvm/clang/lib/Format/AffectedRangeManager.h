//===--- AffectedRangeManager.h - Format C++ code ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// AffectedRangeManager class manages affected ranges in the code.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_FORMAT_AFFECTEDRANGEMANAGER_H
#define LLVM_CLANG_LIB_FORMAT_AFFECTEDRANGEMANAGER_H

#include "clang/Basic/SourceManager.h"

namespace clang {
namespace format {

struct FormatToken;
class AnnotatedLine;

class AffectedRangeManager {
public:
  AffectedRangeManager(const SourceManager &SourceMgr,
                       const ArrayRef<CharSourceRange> Ranges)
      : SourceMgr(SourceMgr), Ranges(Ranges.begin(), Ranges.end()) {}

  // Determines which lines are affected by the SourceRanges given as input.
  // Returns \c true if at least one line in \p Lines or one of their
  // children is affected.
  bool computeAffectedLines(SmallVectorImpl<AnnotatedLine *> &Lines);

  // Returns true if 'Range' intersects with one of the input ranges.
  bool affectsCharSourceRange(const CharSourceRange &Range);

private:
  // Returns true if the range from 'First' to 'Last' intersects with one of the
  // input ranges.
  bool affectsTokenRange(const FormatToken &First, const FormatToken &Last,
                         bool IncludeLeadingNewlines);

  // Returns true if one of the input ranges intersect the leading empty lines
  // before 'Tok'.
  bool affectsLeadingEmptyLines(const FormatToken &Tok);

  // Marks all lines between I and E as well as all their children as affected.
  void markAllAsAffected(SmallVectorImpl<AnnotatedLine *>::iterator I,
                         SmallVectorImpl<AnnotatedLine *>::iterator E);

  // Determines whether 'Line' is affected by the SourceRanges given as input.
  // Returns \c true if line or one if its children is affected.
  bool nonPPLineAffected(AnnotatedLine *Line, const AnnotatedLine *PreviousLine,
                         SmallVectorImpl<AnnotatedLine *> &Lines);

  const SourceManager &SourceMgr;
  const SmallVector<CharSourceRange, 8> Ranges;
};

} // namespace format
} // namespace clang

#endif // LLVM_CLANG_LIB_FORMAT_AFFECTEDRANGEMANAGER_H
