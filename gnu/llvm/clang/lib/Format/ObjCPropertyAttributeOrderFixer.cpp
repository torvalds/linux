//===--- ObjCPropertyAttributeOrderFixer.cpp -------------------*- C++--*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements ObjCPropertyAttributeOrderFixer, a TokenAnalyzer that
/// adjusts the order of attributes in an ObjC `@property(...)` declaration,
/// depending on the style.
///
//===----------------------------------------------------------------------===//

#include "ObjCPropertyAttributeOrderFixer.h"

#include <algorithm>

namespace clang {
namespace format {

ObjCPropertyAttributeOrderFixer::ObjCPropertyAttributeOrderFixer(
    const Environment &Env, const FormatStyle &Style)
    : TokenAnalyzer(Env, Style) {
  // Create an "order priority" map to use to sort properties.
  unsigned Index = 0;
  for (const auto &Property : Style.ObjCPropertyAttributeOrder)
    SortOrderMap[Property] = Index++;
}

struct ObjCPropertyEntry {
  StringRef Attribute; // eg, `readwrite`
  StringRef Value;     // eg, the `foo` of the attribute `getter=foo`
};

void ObjCPropertyAttributeOrderFixer::sortPropertyAttributes(
    const SourceManager &SourceMgr, tooling::Replacements &Fixes,
    const FormatToken *BeginTok, const FormatToken *EndTok) {
  assert(BeginTok);
  assert(EndTok);
  assert(EndTok->Previous);

  // If there are zero or one tokens, nothing to do.
  if (BeginTok == EndTok || BeginTok->Next == EndTok)
    return;

  // Use a set to sort attributes and remove duplicates.
  std::set<unsigned> Ordinals;

  // Create a "remapping index" on how to reorder the attributes.
  SmallVector<int> Indices;

  // Collect the attributes.
  SmallVector<ObjCPropertyEntry> PropertyAttributes;
  bool HasDuplicates = false;
  int Index = 0;
  for (auto Tok = BeginTok; Tok != EndTok; Tok = Tok->Next) {
    assert(Tok);
    if (Tok->is(tok::comma)) {
      // Ignore the comma separators.
      continue;
    }

    // Most attributes look like identifiers, but `class` is a keyword.
    if (!Tok->isOneOf(tok::identifier, tok::kw_class)) {
      // If we hit any other kind of token, just bail.
      return;
    }

    const StringRef Attribute{Tok->TokenText};
    StringRef Value;

    // Also handle `getter=getFoo` attributes.
    // (Note: no check needed against `EndTok`, since its type is not
    // BinaryOperator or Identifier)
    assert(Tok->Next);
    if (Tok->Next->is(tok::equal)) {
      Tok = Tok->Next;
      assert(Tok->Next);
      if (Tok->Next->isNot(tok::identifier)) {
        // If we hit any other kind of token, just bail. It's unusual/illegal.
        return;
      }
      Tok = Tok->Next;
      Value = Tok->TokenText;
    }

    auto It = SortOrderMap.find(Attribute);
    if (It == SortOrderMap.end())
      It = SortOrderMap.insert({Attribute, SortOrderMap.size()}).first;

    // Sort the indices based on the priority stored in `SortOrderMap`.
    const auto Ordinal = It->second;
    if (!Ordinals.insert(Ordinal).second) {
      HasDuplicates = true;
      continue;
    }

    if (Ordinal >= Indices.size())
      Indices.resize(Ordinal + 1);
    Indices[Ordinal] = Index++;

    // Memoize the attribute.
    PropertyAttributes.push_back({Attribute, Value});
  }

  if (!HasDuplicates) {
    // There's nothing to do unless there's more than one attribute.
    if (PropertyAttributes.size() < 2)
      return;

    int PrevIndex = -1;
    bool IsSorted = true;
    for (const auto Ordinal : Ordinals) {
      const auto Index = Indices[Ordinal];
      if (Index < PrevIndex) {
        IsSorted = false;
        break;
      }
      assert(Index > PrevIndex);
      PrevIndex = Index;
    }

    // If the property order is already correct, then no fix-up is needed.
    if (IsSorted)
      return;
  }

  // Generate the replacement text.
  std::string NewText;
  bool IsFirst = true;
  for (const auto Ordinal : Ordinals) {
    if (IsFirst)
      IsFirst = false;
    else
      NewText += ", ";

    const auto &PropertyEntry = PropertyAttributes[Indices[Ordinal]];
    NewText += PropertyEntry.Attribute;

    if (const auto Value = PropertyEntry.Value; !Value.empty()) {
      NewText += '=';
      NewText += Value;
    }
  }

  auto Range = CharSourceRange::getCharRange(
      BeginTok->getStartOfNonWhitespace(), EndTok->Previous->Tok.getEndLoc());
  auto Replacement = tooling::Replacement(SourceMgr, Range, NewText);
  auto Err = Fixes.add(Replacement);
  if (Err) {
    llvm::errs() << "Error while reodering ObjC property attributes : "
                 << llvm::toString(std::move(Err)) << "\n";
  }
}

void ObjCPropertyAttributeOrderFixer::analyzeObjCPropertyDecl(
    const SourceManager &SourceMgr, const AdditionalKeywords &Keywords,
    tooling::Replacements &Fixes, const FormatToken *Tok) {
  assert(Tok);

  // Expect `property` to be the very next token or else just bail early.
  const FormatToken *const PropertyTok = Tok->Next;
  if (!PropertyTok || PropertyTok->isNot(Keywords.kw_property))
    return;

  // Expect the opening paren to be the next token or else just bail early.
  const FormatToken *const LParenTok = PropertyTok->getNextNonComment();
  if (!LParenTok || LParenTok->isNot(tok::l_paren))
    return;

  // Get the matching right-paren, the bounds for property attributes.
  const FormatToken *const RParenTok = LParenTok->MatchingParen;
  if (!RParenTok)
    return;

  sortPropertyAttributes(SourceMgr, Fixes, LParenTok->Next, RParenTok);
}

std::pair<tooling::Replacements, unsigned>
ObjCPropertyAttributeOrderFixer::analyze(
    TokenAnnotator & /*Annotator*/,
    SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
    FormatTokenLexer &Tokens) {
  tooling::Replacements Fixes;
  const AdditionalKeywords &Keywords = Tokens.getKeywords();
  const SourceManager &SourceMgr = Env.getSourceManager();
  AffectedRangeMgr.computeAffectedLines(AnnotatedLines);

  for (AnnotatedLine *Line : AnnotatedLines) {
    assert(Line);
    if (!Line->Affected || Line->Type != LT_ObjCProperty)
      continue;
    FormatToken *First = Line->First;
    assert(First);
    if (First->Finalized)
      continue;

    const auto *Last = Line->Last;

    for (const auto *Tok = First; Tok != Last; Tok = Tok->Next) {
      assert(Tok);

      // Skip until the `@` of a `@property` declaration.
      if (Tok->isNot(TT_ObjCProperty))
        continue;

      analyzeObjCPropertyDecl(SourceMgr, Keywords, Fixes, Tok);

      // There are never two `@property` in a line (they are split
      // by other passes), so this pass can break after just one.
      break;
    }
  }
  return {Fixes, 0};
}

} // namespace format
} // namespace clang
