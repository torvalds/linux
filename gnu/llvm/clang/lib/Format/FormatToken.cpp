//===--- FormatToken.cpp - Format C++ code --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements specific functions of \c FormatTokens and their
/// roles.
///
//===----------------------------------------------------------------------===//

#include "FormatToken.h"
#include "ContinuationIndenter.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include <climits>

namespace clang {
namespace format {

const char *getTokenTypeName(TokenType Type) {
  static const char *const TokNames[] = {
#define TYPE(X) #X,
      LIST_TOKEN_TYPES
#undef TYPE
      nullptr};

  if (Type < NUM_TOKEN_TYPES)
    return TokNames[Type];
  llvm_unreachable("unknown TokenType");
  return nullptr;
}

// Sorted common C++ non-keyword types.
static SmallVector<StringRef> CppNonKeywordTypes = {
    "clock_t",  "int16_t",   "int32_t", "int64_t",   "int8_t",
    "intptr_t", "ptrdiff_t", "size_t",  "time_t",    "uint16_t",
    "uint32_t", "uint64_t",  "uint8_t", "uintptr_t",
};

bool FormatToken::isTypeName(const LangOptions &LangOpts) const {
  const bool IsCpp = LangOpts.CXXOperatorNames;
  return is(TT_TypeName) || Tok.isSimpleTypeSpecifier(LangOpts) ||
         (IsCpp && is(tok::identifier) &&
          std::binary_search(CppNonKeywordTypes.begin(),
                             CppNonKeywordTypes.end(), TokenText));
}

bool FormatToken::isTypeOrIdentifier(const LangOptions &LangOpts) const {
  return isTypeName(LangOpts) || isOneOf(tok::kw_auto, tok::identifier);
}

bool FormatToken::isBlockIndentedInitRBrace(const FormatStyle &Style) const {
  assert(is(tok::r_brace));
  if (!Style.Cpp11BracedListStyle ||
      Style.AlignAfterOpenBracket != FormatStyle::BAS_BlockIndent) {
    return false;
  }
  const auto *LBrace = MatchingParen;
  assert(LBrace && LBrace->is(tok::l_brace));
  if (LBrace->is(BK_BracedInit))
    return true;
  if (LBrace->Previous && LBrace->Previous->is(tok::equal))
    return true;
  return false;
}

bool FormatToken::opensBlockOrBlockTypeList(const FormatStyle &Style) const {
  // C# Does not indent object initialisers as continuations.
  if (is(tok::l_brace) && getBlockKind() == BK_BracedInit && Style.isCSharp())
    return true;
  if (is(TT_TemplateString) && opensScope())
    return true;
  return is(TT_ArrayInitializerLSquare) || is(TT_ProtoExtensionLSquare) ||
         (is(tok::l_brace) &&
          (getBlockKind() == BK_Block || is(TT_DictLiteral) ||
           (!Style.Cpp11BracedListStyle && NestingLevel == 0))) ||
         (is(tok::less) && Style.isProto());
}

TokenRole::~TokenRole() {}

void TokenRole::precomputeFormattingInfos(const FormatToken *Token) {}

unsigned CommaSeparatedList::formatAfterToken(LineState &State,
                                              ContinuationIndenter *Indenter,
                                              bool DryRun) {
  if (!State.NextToken || !State.NextToken->Previous)
    return 0;

  if (Formats.size() <= 1)
    return 0; // Handled by formatFromToken (1) or avoid severe penalty (0).

  // Ensure that we start on the opening brace.
  const FormatToken *LBrace =
      State.NextToken->Previous->getPreviousNonComment();
  if (!LBrace || !LBrace->isOneOf(tok::l_brace, TT_ArrayInitializerLSquare) ||
      LBrace->is(BK_Block) || LBrace->is(TT_DictLiteral) ||
      LBrace->Next->is(TT_DesignatedInitializerPeriod)) {
    return 0;
  }

  // Calculate the number of code points we have to format this list. As the
  // first token is already placed, we have to subtract it.
  unsigned RemainingCodePoints =
      Style.ColumnLimit - State.Column + State.NextToken->Previous->ColumnWidth;

  // Find the best ColumnFormat, i.e. the best number of columns to use.
  const ColumnFormat *Format = getColumnFormat(RemainingCodePoints);

  // If no ColumnFormat can be used, the braced list would generally be
  // bin-packed. Add a severe penalty to this so that column layouts are
  // preferred if possible.
  if (!Format)
    return 10'000;

  // Format the entire list.
  unsigned Penalty = 0;
  unsigned Column = 0;
  unsigned Item = 0;
  while (State.NextToken != LBrace->MatchingParen) {
    bool NewLine = false;
    unsigned ExtraSpaces = 0;

    // If the previous token was one of our commas, we are now on the next item.
    if (Item < Commas.size() && State.NextToken->Previous == Commas[Item]) {
      if (!State.NextToken->isTrailingComment()) {
        ExtraSpaces += Format->ColumnSizes[Column] - ItemLengths[Item];
        ++Column;
      }
      ++Item;
    }

    if (Column == Format->Columns || State.NextToken->MustBreakBefore) {
      Column = 0;
      NewLine = true;
    }

    // Place token using the continuation indenter and store the penalty.
    Penalty += Indenter->addTokenToState(State, NewLine, DryRun, ExtraSpaces);
  }
  return Penalty;
}

unsigned CommaSeparatedList::formatFromToken(LineState &State,
                                             ContinuationIndenter *Indenter,
                                             bool DryRun) {
  // Formatting with 1 Column isn't really a column layout, so we don't need the
  // special logic here. We can just avoid bin packing any of the parameters.
  if (Formats.size() == 1 || HasNestedBracedList)
    State.Stack.back().AvoidBinPacking = true;
  return 0;
}

// Returns the lengths in code points between Begin and End (both included),
// assuming that the entire sequence is put on a single line.
static unsigned CodePointsBetween(const FormatToken *Begin,
                                  const FormatToken *End) {
  assert(End->TotalLength >= Begin->TotalLength);
  return End->TotalLength - Begin->TotalLength + Begin->ColumnWidth;
}

void CommaSeparatedList::precomputeFormattingInfos(const FormatToken *Token) {
  // FIXME: At some point we might want to do this for other lists, too.
  if (!Token->MatchingParen ||
      !Token->isOneOf(tok::l_brace, TT_ArrayInitializerLSquare)) {
    return;
  }

  // In C++11 braced list style, we should not format in columns unless they
  // have many items (20 or more) or we allow bin-packing of function call
  // arguments.
  if (Style.Cpp11BracedListStyle && !Style.BinPackArguments &&
      Commas.size() < 19) {
    return;
  }

  // Limit column layout for JavaScript array initializers to 20 or more items
  // for now to introduce it carefully. We can become more aggressive if this
  // necessary.
  if (Token->is(TT_ArrayInitializerLSquare) && Commas.size() < 19)
    return;

  // Column format doesn't really make sense if we don't align after brackets.
  if (Style.AlignAfterOpenBracket == FormatStyle::BAS_DontAlign)
    return;

  FormatToken *ItemBegin = Token->Next;
  while (ItemBegin->isTrailingComment())
    ItemBegin = ItemBegin->Next;
  SmallVector<bool, 8> MustBreakBeforeItem;

  // The lengths of an item if it is put at the end of the line. This includes
  // trailing comments which are otherwise ignored for column alignment.
  SmallVector<unsigned, 8> EndOfLineItemLength;
  MustBreakBeforeItem.reserve(Commas.size() + 1);
  EndOfLineItemLength.reserve(Commas.size() + 1);
  ItemLengths.reserve(Commas.size() + 1);

  bool HasSeparatingComment = false;
  for (unsigned i = 0, e = Commas.size() + 1; i != e; ++i) {
    assert(ItemBegin);
    // Skip comments on their own line.
    while (ItemBegin->HasUnescapedNewline && ItemBegin->isTrailingComment()) {
      ItemBegin = ItemBegin->Next;
      HasSeparatingComment = i > 0;
    }

    MustBreakBeforeItem.push_back(ItemBegin->MustBreakBefore);
    if (ItemBegin->is(tok::l_brace))
      HasNestedBracedList = true;
    const FormatToken *ItemEnd = nullptr;
    if (i == Commas.size()) {
      ItemEnd = Token->MatchingParen;
      const FormatToken *NonCommentEnd = ItemEnd->getPreviousNonComment();
      ItemLengths.push_back(CodePointsBetween(ItemBegin, NonCommentEnd));
      if (Style.Cpp11BracedListStyle &&
          !ItemEnd->Previous->isTrailingComment()) {
        // In Cpp11 braced list style, the } and possibly other subsequent
        // tokens will need to stay on a line with the last element.
        while (ItemEnd->Next && !ItemEnd->Next->CanBreakBefore)
          ItemEnd = ItemEnd->Next;
      } else {
        // In other braced lists styles, the "}" can be wrapped to the new line.
        ItemEnd = Token->MatchingParen->Previous;
      }
    } else {
      ItemEnd = Commas[i];
      // The comma is counted as part of the item when calculating the length.
      ItemLengths.push_back(CodePointsBetween(ItemBegin, ItemEnd));

      // Consume trailing comments so the are included in EndOfLineItemLength.
      if (ItemEnd->Next && !ItemEnd->Next->HasUnescapedNewline &&
          ItemEnd->Next->isTrailingComment()) {
        ItemEnd = ItemEnd->Next;
      }
    }
    EndOfLineItemLength.push_back(CodePointsBetween(ItemBegin, ItemEnd));
    // If there is a trailing comma in the list, the next item will start at the
    // closing brace. Don't create an extra item for this.
    if (ItemEnd->getNextNonComment() == Token->MatchingParen)
      break;
    ItemBegin = ItemEnd->Next;
  }

  // Don't use column layout for lists with few elements and in presence of
  // separating comments.
  if (Commas.size() < 5 || HasSeparatingComment)
    return;

  if (Token->NestingLevel != 0 && Token->is(tok::l_brace) && Commas.size() < 19)
    return;

  // We can never place more than ColumnLimit / 3 items in a row (because of the
  // spaces and the comma).
  unsigned MaxItems = Style.ColumnLimit / 3;
  SmallVector<unsigned> MinSizeInColumn;
  MinSizeInColumn.reserve(MaxItems);
  for (unsigned Columns = 1; Columns <= MaxItems; ++Columns) {
    ColumnFormat Format;
    Format.Columns = Columns;
    Format.ColumnSizes.resize(Columns);
    MinSizeInColumn.assign(Columns, UINT_MAX);
    Format.LineCount = 1;
    bool HasRowWithSufficientColumns = false;
    unsigned Column = 0;
    for (unsigned i = 0, e = ItemLengths.size(); i != e; ++i) {
      assert(i < MustBreakBeforeItem.size());
      if (MustBreakBeforeItem[i] || Column == Columns) {
        ++Format.LineCount;
        Column = 0;
      }
      if (Column == Columns - 1)
        HasRowWithSufficientColumns = true;
      unsigned Length =
          (Column == Columns - 1) ? EndOfLineItemLength[i] : ItemLengths[i];
      Format.ColumnSizes[Column] = std::max(Format.ColumnSizes[Column], Length);
      MinSizeInColumn[Column] = std::min(MinSizeInColumn[Column], Length);
      ++Column;
    }
    // If all rows are terminated early (e.g. by trailing comments), we don't
    // need to look further.
    if (!HasRowWithSufficientColumns)
      break;
    Format.TotalWidth = Columns - 1; // Width of the N-1 spaces.

    for (unsigned i = 0; i < Columns; ++i)
      Format.TotalWidth += Format.ColumnSizes[i];

    // Don't use this Format, if the difference between the longest and shortest
    // element in a column exceeds a threshold to avoid excessive spaces.
    if ([&] {
          for (unsigned i = 0; i < Columns - 1; ++i)
            if (Format.ColumnSizes[i] - MinSizeInColumn[i] > 10)
              return true;
          return false;
        }()) {
      continue;
    }

    // Ignore layouts that are bound to violate the column limit.
    if (Format.TotalWidth > Style.ColumnLimit && Columns > 1)
      continue;

    Formats.push_back(Format);
  }
}

const CommaSeparatedList::ColumnFormat *
CommaSeparatedList::getColumnFormat(unsigned RemainingCharacters) const {
  const ColumnFormat *BestFormat = nullptr;
  for (const ColumnFormat &Format : llvm::reverse(Formats)) {
    if (Format.TotalWidth <= RemainingCharacters || Format.Columns == 1) {
      if (BestFormat && Format.LineCount > BestFormat->LineCount)
        break;
      BestFormat = &Format;
    }
  }
  return BestFormat;
}

} // namespace format
} // namespace clang
