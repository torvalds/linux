//===--- PPConditionalDirectiveRecord.h - Preprocessing Directives-*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the PPConditionalDirectiveRecord class, which maintains
//  a record of conditional directive regions.
//
//===----------------------------------------------------------------------===//
#include "clang/Lex/PPConditionalDirectiveRecord.h"
#include "llvm/Support/Capacity.h"

using namespace clang;

PPConditionalDirectiveRecord::PPConditionalDirectiveRecord(SourceManager &SM)
  : SourceMgr(SM) {
  CondDirectiveStack.push_back(SourceLocation());
}

bool PPConditionalDirectiveRecord::rangeIntersectsConditionalDirective(
                                                      SourceRange Range) const {
  if (Range.isInvalid())
    return false;

  CondDirectiveLocsTy::const_iterator low = llvm::lower_bound(
      CondDirectiveLocs, Range.getBegin(), CondDirectiveLoc::Comp(SourceMgr));
  if (low == CondDirectiveLocs.end())
    return false;

  if (SourceMgr.isBeforeInTranslationUnit(Range.getEnd(), low->getLoc()))
    return false;

  CondDirectiveLocsTy::const_iterator
    upp = std::upper_bound(low, CondDirectiveLocs.end(),
                           Range.getEnd(), CondDirectiveLoc::Comp(SourceMgr));
  SourceLocation uppRegion;
  if (upp != CondDirectiveLocs.end())
    uppRegion = upp->getRegionLoc();

  return low->getRegionLoc() != uppRegion;
}

SourceLocation PPConditionalDirectiveRecord::findConditionalDirectiveRegionLoc(
                                                     SourceLocation Loc) const {
  if (Loc.isInvalid())
    return SourceLocation();
  if (CondDirectiveLocs.empty())
    return SourceLocation();

  if (SourceMgr.isBeforeInTranslationUnit(CondDirectiveLocs.back().getLoc(),
                                          Loc))
    return CondDirectiveStack.back();

  CondDirectiveLocsTy::const_iterator low = llvm::lower_bound(
      CondDirectiveLocs, Loc, CondDirectiveLoc::Comp(SourceMgr));
  assert(low != CondDirectiveLocs.end());
  return low->getRegionLoc();
}

void PPConditionalDirectiveRecord::addCondDirectiveLoc(
                                                      CondDirectiveLoc DirLoc) {
  // Ignore directives in system headers.
  if (SourceMgr.isInSystemHeader(DirLoc.getLoc()))
    return;

  assert(CondDirectiveLocs.empty() ||
         SourceMgr.isBeforeInTranslationUnit(CondDirectiveLocs.back().getLoc(),
                                             DirLoc.getLoc()));
  CondDirectiveLocs.push_back(DirLoc);
}

void PPConditionalDirectiveRecord::If(SourceLocation Loc,
                                      SourceRange ConditionRange,
                                      ConditionValueKind ConditionValue) {
  addCondDirectiveLoc(CondDirectiveLoc(Loc, CondDirectiveStack.back()));
  CondDirectiveStack.push_back(Loc);
}

void PPConditionalDirectiveRecord::Ifdef(SourceLocation Loc,
                                         const Token &MacroNameTok,
                                         const MacroDefinition &MD) {
  addCondDirectiveLoc(CondDirectiveLoc(Loc, CondDirectiveStack.back()));
  CondDirectiveStack.push_back(Loc);
}

void PPConditionalDirectiveRecord::Ifndef(SourceLocation Loc,
                                          const Token &MacroNameTok,
                                          const MacroDefinition &MD) {
  addCondDirectiveLoc(CondDirectiveLoc(Loc, CondDirectiveStack.back()));
  CondDirectiveStack.push_back(Loc);
}

void PPConditionalDirectiveRecord::Elif(SourceLocation Loc,
                                        SourceRange ConditionRange,
                                        ConditionValueKind ConditionValue,
                                        SourceLocation IfLoc) {
  addCondDirectiveLoc(CondDirectiveLoc(Loc, CondDirectiveStack.back()));
  CondDirectiveStack.back() = Loc;
}

void PPConditionalDirectiveRecord::Elifdef(SourceLocation Loc, const Token &,
                                           const MacroDefinition &) {
  addCondDirectiveLoc(CondDirectiveLoc(Loc, CondDirectiveStack.back()));
  CondDirectiveStack.back() = Loc;
}
void PPConditionalDirectiveRecord::Elifdef(SourceLocation Loc, SourceRange,
                                           SourceLocation) {
  addCondDirectiveLoc(CondDirectiveLoc(Loc, CondDirectiveStack.back()));
  CondDirectiveStack.back() = Loc;
}

void PPConditionalDirectiveRecord::Elifndef(SourceLocation Loc, const Token &,
                                            const MacroDefinition &) {
  addCondDirectiveLoc(CondDirectiveLoc(Loc, CondDirectiveStack.back()));
  CondDirectiveStack.back() = Loc;
}
void PPConditionalDirectiveRecord::Elifndef(SourceLocation Loc, SourceRange,
                                            SourceLocation) {
  addCondDirectiveLoc(CondDirectiveLoc(Loc, CondDirectiveStack.back()));
  CondDirectiveStack.back() = Loc;
}

void PPConditionalDirectiveRecord::Else(SourceLocation Loc,
                                        SourceLocation IfLoc) {
  addCondDirectiveLoc(CondDirectiveLoc(Loc, CondDirectiveStack.back()));
  CondDirectiveStack.back() = Loc;
}

void PPConditionalDirectiveRecord::Endif(SourceLocation Loc,
                                         SourceLocation IfLoc) {
  addCondDirectiveLoc(CondDirectiveLoc(Loc, CondDirectiveStack.back()));
  assert(!CondDirectiveStack.empty());
  CondDirectiveStack.pop_back();
}

size_t PPConditionalDirectiveRecord::getTotalMemory() const {
  return llvm::capacity_in_bytes(CondDirectiveLocs);
}
