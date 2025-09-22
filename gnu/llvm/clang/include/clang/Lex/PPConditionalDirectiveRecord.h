//===--- PPConditionalDirectiveRecord.h - Preprocessing Directives-*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the PPConditionalDirectiveRecord class, which maintains
//  a record of conditional directive regions.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_LEX_PPCONDITIONALDIRECTIVERECORD_H
#define LLVM_CLANG_LEX_PPCONDITIONALDIRECTIVERECORD_H

#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/PPCallbacks.h"
#include "llvm/ADT/SmallVector.h"
#include <vector>

namespace clang {

/// Records preprocessor conditional directive regions and allows
/// querying in which region source locations belong to.
class PPConditionalDirectiveRecord : public PPCallbacks {
  SourceManager &SourceMgr;

  SmallVector<SourceLocation, 6> CondDirectiveStack;

  class CondDirectiveLoc {
    SourceLocation Loc;
    SourceLocation RegionLoc;

  public:
    CondDirectiveLoc(SourceLocation Loc, SourceLocation RegionLoc)
      : Loc(Loc), RegionLoc(RegionLoc) {}

    SourceLocation getLoc() const { return Loc; }
    SourceLocation getRegionLoc() const { return RegionLoc; }

    class Comp {
      SourceManager &SM;
    public:
      explicit Comp(SourceManager &SM) : SM(SM) {}
      bool operator()(const CondDirectiveLoc &LHS,
                      const CondDirectiveLoc &RHS) {
        return SM.isBeforeInTranslationUnit(LHS.getLoc(), RHS.getLoc());
      }
      bool operator()(const CondDirectiveLoc &LHS, SourceLocation RHS) {
        return SM.isBeforeInTranslationUnit(LHS.getLoc(), RHS);
      }
      bool operator()(SourceLocation LHS, const CondDirectiveLoc &RHS) {
        return SM.isBeforeInTranslationUnit(LHS, RHS.getLoc());
      }
    };
  };

  typedef std::vector<CondDirectiveLoc> CondDirectiveLocsTy;
  /// The locations of conditional directives in source order.
  CondDirectiveLocsTy CondDirectiveLocs;

  void addCondDirectiveLoc(CondDirectiveLoc DirLoc);

public:
  /// Construct a new preprocessing record.
  explicit PPConditionalDirectiveRecord(SourceManager &SM);

  size_t getTotalMemory() const;

  SourceManager &getSourceManager() const { return SourceMgr; }

  /// Returns true if the given range intersects with a conditional
  /// directive. if a \#if/\#endif block is fully contained within the range,
  /// this function will return false.
  bool rangeIntersectsConditionalDirective(SourceRange Range) const;

  /// Returns true if the given locations are in different regions,
  /// separated by conditional directive blocks.
  bool areInDifferentConditionalDirectiveRegion(SourceLocation LHS,
                                                SourceLocation RHS) const {
    return findConditionalDirectiveRegionLoc(LHS) !=
        findConditionalDirectiveRegionLoc(RHS);
  }

  SourceLocation findConditionalDirectiveRegionLoc(SourceLocation Loc) const;

private:
  void If(SourceLocation Loc, SourceRange ConditionRange,
          ConditionValueKind ConditionValue) override;
  void Elif(SourceLocation Loc, SourceRange ConditionRange,
            ConditionValueKind ConditionValue, SourceLocation IfLoc) override;
  void Ifdef(SourceLocation Loc, const Token &MacroNameTok,
             const MacroDefinition &MD) override;
  void Ifndef(SourceLocation Loc, const Token &MacroNameTok,
              const MacroDefinition &MD) override;
  void Elifdef(SourceLocation Loc, const Token &MacroNameTok,
               const MacroDefinition &MD) override;
  void Elifdef(SourceLocation Loc, SourceRange ConditionRange,
               SourceLocation IfLoc) override;
  void Elifndef(SourceLocation Loc, const Token &MacroNameTok,
                const MacroDefinition &MD) override;
  void Elifndef(SourceLocation Loc, SourceRange ConditionRange,
                SourceLocation IfLoc) override;
  void Else(SourceLocation Loc, SourceLocation IfLoc) override;
  void Endif(SourceLocation Loc, SourceLocation IfLoc) override;
};

} // end namespace clang

#endif // LLVM_CLANG_LEX_PPCONDITIONALDIRECTIVERECORD_H
