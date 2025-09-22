//===--- UnwrappedLineParser.cpp - Format C++ code ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the implementation of the UnwrappedLineParser,
/// which turns a stream of tokens into UnwrappedLines.
///
//===----------------------------------------------------------------------===//

#include "UnwrappedLineParser.h"
#include "FormatToken.h"
#include "FormatTokenLexer.h"
#include "FormatTokenSource.h"
#include "Macros.h"
#include "TokenAnnotator.h"
#include "clang/Basic/TokenKinds.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <utility>

#define DEBUG_TYPE "format-parser"

namespace clang {
namespace format {

namespace {

void printLine(llvm::raw_ostream &OS, const UnwrappedLine &Line,
               StringRef Prefix = "", bool PrintText = false) {
  OS << Prefix << "Line(" << Line.Level << ", FSC=" << Line.FirstStartColumn
     << ")" << (Line.InPPDirective ? " MACRO" : "") << ": ";
  bool NewLine = false;
  for (std::list<UnwrappedLineNode>::const_iterator I = Line.Tokens.begin(),
                                                    E = Line.Tokens.end();
       I != E; ++I) {
    if (NewLine) {
      OS << Prefix;
      NewLine = false;
    }
    OS << I->Tok->Tok.getName() << "["
       << "T=" << (unsigned)I->Tok->getType()
       << ", OC=" << I->Tok->OriginalColumn << ", \"" << I->Tok->TokenText
       << "\"] ";
    for (SmallVectorImpl<UnwrappedLine>::const_iterator
             CI = I->Children.begin(),
             CE = I->Children.end();
         CI != CE; ++CI) {
      OS << "\n";
      printLine(OS, *CI, (Prefix + "  ").str());
      NewLine = true;
    }
  }
  if (!NewLine)
    OS << "\n";
}

LLVM_ATTRIBUTE_UNUSED static void printDebugInfo(const UnwrappedLine &Line) {
  printLine(llvm::dbgs(), Line);
}

class ScopedDeclarationState {
public:
  ScopedDeclarationState(UnwrappedLine &Line, llvm::BitVector &Stack,
                         bool MustBeDeclaration)
      : Line(Line), Stack(Stack) {
    Line.MustBeDeclaration = MustBeDeclaration;
    Stack.push_back(MustBeDeclaration);
  }
  ~ScopedDeclarationState() {
    Stack.pop_back();
    if (!Stack.empty())
      Line.MustBeDeclaration = Stack.back();
    else
      Line.MustBeDeclaration = true;
  }

private:
  UnwrappedLine &Line;
  llvm::BitVector &Stack;
};

} // end anonymous namespace

std::ostream &operator<<(std::ostream &Stream, const UnwrappedLine &Line) {
  llvm::raw_os_ostream OS(Stream);
  printLine(OS, Line);
  return Stream;
}

class ScopedLineState {
public:
  ScopedLineState(UnwrappedLineParser &Parser,
                  bool SwitchToPreprocessorLines = false)
      : Parser(Parser), OriginalLines(Parser.CurrentLines) {
    if (SwitchToPreprocessorLines)
      Parser.CurrentLines = &Parser.PreprocessorDirectives;
    else if (!Parser.Line->Tokens.empty())
      Parser.CurrentLines = &Parser.Line->Tokens.back().Children;
    PreBlockLine = std::move(Parser.Line);
    Parser.Line = std::make_unique<UnwrappedLine>();
    Parser.Line->Level = PreBlockLine->Level;
    Parser.Line->PPLevel = PreBlockLine->PPLevel;
    Parser.Line->InPPDirective = PreBlockLine->InPPDirective;
    Parser.Line->InMacroBody = PreBlockLine->InMacroBody;
    Parser.Line->UnbracedBodyLevel = PreBlockLine->UnbracedBodyLevel;
  }

  ~ScopedLineState() {
    if (!Parser.Line->Tokens.empty())
      Parser.addUnwrappedLine();
    assert(Parser.Line->Tokens.empty());
    Parser.Line = std::move(PreBlockLine);
    if (Parser.CurrentLines == &Parser.PreprocessorDirectives)
      Parser.MustBreakBeforeNextToken = true;
    Parser.CurrentLines = OriginalLines;
  }

private:
  UnwrappedLineParser &Parser;

  std::unique_ptr<UnwrappedLine> PreBlockLine;
  SmallVectorImpl<UnwrappedLine> *OriginalLines;
};

class CompoundStatementIndenter {
public:
  CompoundStatementIndenter(UnwrappedLineParser *Parser,
                            const FormatStyle &Style, unsigned &LineLevel)
      : CompoundStatementIndenter(Parser, LineLevel,
                                  Style.BraceWrapping.AfterControlStatement,
                                  Style.BraceWrapping.IndentBraces) {}
  CompoundStatementIndenter(UnwrappedLineParser *Parser, unsigned &LineLevel,
                            bool WrapBrace, bool IndentBrace)
      : LineLevel(LineLevel), OldLineLevel(LineLevel) {
    if (WrapBrace)
      Parser->addUnwrappedLine();
    if (IndentBrace)
      ++LineLevel;
  }
  ~CompoundStatementIndenter() { LineLevel = OldLineLevel; }

private:
  unsigned &LineLevel;
  unsigned OldLineLevel;
};

UnwrappedLineParser::UnwrappedLineParser(
    SourceManager &SourceMgr, const FormatStyle &Style,
    const AdditionalKeywords &Keywords, unsigned FirstStartColumn,
    ArrayRef<FormatToken *> Tokens, UnwrappedLineConsumer &Callback,
    llvm::SpecificBumpPtrAllocator<FormatToken> &Allocator,
    IdentifierTable &IdentTable)
    : Line(new UnwrappedLine), MustBreakBeforeNextToken(false),
      CurrentLines(&Lines), Style(Style), IsCpp(Style.isCpp()),
      LangOpts(getFormattingLangOpts(Style)), Keywords(Keywords),
      CommentPragmasRegex(Style.CommentPragmas), Tokens(nullptr),
      Callback(Callback), AllTokens(Tokens), PPBranchLevel(-1),
      IncludeGuard(Style.IndentPPDirectives == FormatStyle::PPDIS_None
                       ? IG_Rejected
                       : IG_Inited),
      IncludeGuardToken(nullptr), FirstStartColumn(FirstStartColumn),
      Macros(Style.Macros, SourceMgr, Style, Allocator, IdentTable) {
  assert(IsCpp == LangOpts.CXXOperatorNames);
}

void UnwrappedLineParser::reset() {
  PPBranchLevel = -1;
  IncludeGuard = Style.IndentPPDirectives == FormatStyle::PPDIS_None
                     ? IG_Rejected
                     : IG_Inited;
  IncludeGuardToken = nullptr;
  Line.reset(new UnwrappedLine);
  CommentsBeforeNextToken.clear();
  FormatTok = nullptr;
  MustBreakBeforeNextToken = false;
  IsDecltypeAutoFunction = false;
  PreprocessorDirectives.clear();
  CurrentLines = &Lines;
  DeclarationScopeStack.clear();
  NestedTooDeep.clear();
  NestedLambdas.clear();
  PPStack.clear();
  Line->FirstStartColumn = FirstStartColumn;

  if (!Unexpanded.empty())
    for (FormatToken *Token : AllTokens)
      Token->MacroCtx.reset();
  CurrentExpandedLines.clear();
  ExpandedLines.clear();
  Unexpanded.clear();
  InExpansion = false;
  Reconstruct.reset();
}

void UnwrappedLineParser::parse() {
  IndexedTokenSource TokenSource(AllTokens);
  Line->FirstStartColumn = FirstStartColumn;
  do {
    LLVM_DEBUG(llvm::dbgs() << "----\n");
    reset();
    Tokens = &TokenSource;
    TokenSource.reset();

    readToken();
    parseFile();

    // If we found an include guard then all preprocessor directives (other than
    // the guard) are over-indented by one.
    if (IncludeGuard == IG_Found) {
      for (auto &Line : Lines)
        if (Line.InPPDirective && Line.Level > 0)
          --Line.Level;
    }

    // Create line with eof token.
    assert(eof());
    pushToken(FormatTok);
    addUnwrappedLine();

    // In a first run, format everything with the lines containing macro calls
    // replaced by the expansion.
    if (!ExpandedLines.empty()) {
      LLVM_DEBUG(llvm::dbgs() << "Expanded lines:\n");
      for (const auto &Line : Lines) {
        if (!Line.Tokens.empty()) {
          auto it = ExpandedLines.find(Line.Tokens.begin()->Tok);
          if (it != ExpandedLines.end()) {
            for (const auto &Expanded : it->second) {
              LLVM_DEBUG(printDebugInfo(Expanded));
              Callback.consumeUnwrappedLine(Expanded);
            }
            continue;
          }
        }
        LLVM_DEBUG(printDebugInfo(Line));
        Callback.consumeUnwrappedLine(Line);
      }
      Callback.finishRun();
    }

    LLVM_DEBUG(llvm::dbgs() << "Unwrapped lines:\n");
    for (const UnwrappedLine &Line : Lines) {
      LLVM_DEBUG(printDebugInfo(Line));
      Callback.consumeUnwrappedLine(Line);
    }
    Callback.finishRun();
    Lines.clear();
    while (!PPLevelBranchIndex.empty() &&
           PPLevelBranchIndex.back() + 1 >= PPLevelBranchCount.back()) {
      PPLevelBranchIndex.resize(PPLevelBranchIndex.size() - 1);
      PPLevelBranchCount.resize(PPLevelBranchCount.size() - 1);
    }
    if (!PPLevelBranchIndex.empty()) {
      ++PPLevelBranchIndex.back();
      assert(PPLevelBranchIndex.size() == PPLevelBranchCount.size());
      assert(PPLevelBranchIndex.back() <= PPLevelBranchCount.back());
    }
  } while (!PPLevelBranchIndex.empty());
}

void UnwrappedLineParser::parseFile() {
  // The top-level context in a file always has declarations, except for pre-
  // processor directives and JavaScript files.
  bool MustBeDeclaration = !Line->InPPDirective && !Style.isJavaScript();
  ScopedDeclarationState DeclarationState(*Line, DeclarationScopeStack,
                                          MustBeDeclaration);
  if (Style.Language == FormatStyle::LK_TextProto)
    parseBracedList();
  else
    parseLevel();
  // Make sure to format the remaining tokens.
  //
  // LK_TextProto is special since its top-level is parsed as the body of a
  // braced list, which does not necessarily have natural line separators such
  // as a semicolon. Comments after the last entry that have been determined to
  // not belong to that line, as in:
  //   key: value
  //   // endfile comment
  // do not have a chance to be put on a line of their own until this point.
  // Here we add this newline before end-of-file comments.
  if (Style.Language == FormatStyle::LK_TextProto &&
      !CommentsBeforeNextToken.empty()) {
    addUnwrappedLine();
  }
  flushComments(true);
  addUnwrappedLine();
}

void UnwrappedLineParser::parseCSharpGenericTypeConstraint() {
  do {
    switch (FormatTok->Tok.getKind()) {
    case tok::l_brace:
      return;
    default:
      if (FormatTok->is(Keywords.kw_where)) {
        addUnwrappedLine();
        nextToken();
        parseCSharpGenericTypeConstraint();
        break;
      }
      nextToken();
      break;
    }
  } while (!eof());
}

void UnwrappedLineParser::parseCSharpAttribute() {
  int UnpairedSquareBrackets = 1;
  do {
    switch (FormatTok->Tok.getKind()) {
    case tok::r_square:
      nextToken();
      --UnpairedSquareBrackets;
      if (UnpairedSquareBrackets == 0) {
        addUnwrappedLine();
        return;
      }
      break;
    case tok::l_square:
      ++UnpairedSquareBrackets;
      nextToken();
      break;
    default:
      nextToken();
      break;
    }
  } while (!eof());
}

bool UnwrappedLineParser::precededByCommentOrPPDirective() const {
  if (!Lines.empty() && Lines.back().InPPDirective)
    return true;

  const FormatToken *Previous = Tokens->getPreviousToken();
  return Previous && Previous->is(tok::comment) &&
         (Previous->IsMultiline || Previous->NewlinesBefore > 0);
}

/// \brief Parses a level, that is ???.
/// \param OpeningBrace Opening brace (\p nullptr if absent) of that level.
/// \param IfKind The \p if statement kind in the level.
/// \param IfLeftBrace The left brace of the \p if block in the level.
/// \returns true if a simple block of if/else/for/while, or false otherwise.
/// (A simple block has a single statement.)
bool UnwrappedLineParser::parseLevel(const FormatToken *OpeningBrace,
                                     IfStmtKind *IfKind,
                                     FormatToken **IfLeftBrace) {
  const bool InRequiresExpression =
      OpeningBrace && OpeningBrace->is(TT_RequiresExpressionLBrace);
  const bool IsPrecededByCommentOrPPDirective =
      !Style.RemoveBracesLLVM || precededByCommentOrPPDirective();
  FormatToken *IfLBrace = nullptr;
  bool HasDoWhile = false;
  bool HasLabel = false;
  unsigned StatementCount = 0;
  bool SwitchLabelEncountered = false;

  do {
    if (FormatTok->isAttribute()) {
      nextToken();
      if (FormatTok->is(tok::l_paren))
        parseParens();
      continue;
    }
    tok::TokenKind Kind = FormatTok->Tok.getKind();
    if (FormatTok->is(TT_MacroBlockBegin))
      Kind = tok::l_brace;
    else if (FormatTok->is(TT_MacroBlockEnd))
      Kind = tok::r_brace;

    auto ParseDefault = [this, OpeningBrace, IfKind, &IfLBrace, &HasDoWhile,
                         &HasLabel, &StatementCount] {
      parseStructuralElement(OpeningBrace, IfKind, &IfLBrace,
                             HasDoWhile ? nullptr : &HasDoWhile,
                             HasLabel ? nullptr : &HasLabel);
      ++StatementCount;
      assert(StatementCount > 0 && "StatementCount overflow!");
    };

    switch (Kind) {
    case tok::comment:
      nextToken();
      addUnwrappedLine();
      break;
    case tok::l_brace:
      if (InRequiresExpression) {
        FormatTok->setFinalizedType(TT_RequiresExpressionLBrace);
      } else if (FormatTok->Previous &&
                 FormatTok->Previous->ClosesRequiresClause) {
        // We need the 'default' case here to correctly parse a function
        // l_brace.
        ParseDefault();
        continue;
      }
      if (!InRequiresExpression && FormatTok->isNot(TT_MacroBlockBegin)) {
        if (tryToParseBracedList())
          continue;
        FormatTok->setFinalizedType(TT_BlockLBrace);
      }
      parseBlock();
      ++StatementCount;
      assert(StatementCount > 0 && "StatementCount overflow!");
      addUnwrappedLine();
      break;
    case tok::r_brace:
      if (OpeningBrace) {
        if (!Style.RemoveBracesLLVM || Line->InPPDirective ||
            !OpeningBrace->isOneOf(TT_ControlStatementLBrace, TT_ElseLBrace)) {
          return false;
        }
        if (FormatTok->isNot(tok::r_brace) || StatementCount != 1 || HasLabel ||
            HasDoWhile || IsPrecededByCommentOrPPDirective ||
            precededByCommentOrPPDirective()) {
          return false;
        }
        const FormatToken *Next = Tokens->peekNextToken();
        if (Next->is(tok::comment) && Next->NewlinesBefore == 0)
          return false;
        if (IfLeftBrace)
          *IfLeftBrace = IfLBrace;
        return true;
      }
      nextToken();
      addUnwrappedLine();
      break;
    case tok::kw_default: {
      unsigned StoredPosition = Tokens->getPosition();
      auto *Next = Tokens->getNextNonComment();
      FormatTok = Tokens->setPosition(StoredPosition);
      if (!Next->isOneOf(tok::colon, tok::arrow)) {
        // default not followed by `:` or `->` is not a case label; treat it
        // like an identifier.
        parseStructuralElement();
        break;
      }
      // Else, if it is 'default:', fall through to the case handling.
      [[fallthrough]];
    }
    case tok::kw_case:
      if (Style.Language == FormatStyle::LK_Proto || Style.isVerilog() ||
          (Style.isJavaScript() && Line->MustBeDeclaration)) {
        // Proto: there are no switch/case statements
        // Verilog: Case labels don't have this word. We handle case
        // labels including default in TokenAnnotator.
        // JavaScript: A 'case: string' style field declaration.
        ParseDefault();
        break;
      }
      if (!SwitchLabelEncountered &&
          (Style.IndentCaseLabels ||
           (OpeningBrace && OpeningBrace->is(TT_SwitchExpressionLBrace)) ||
           (Line->InPPDirective && Line->Level == 1))) {
        ++Line->Level;
      }
      SwitchLabelEncountered = true;
      parseStructuralElement();
      break;
    case tok::l_square:
      if (Style.isCSharp()) {
        nextToken();
        parseCSharpAttribute();
        break;
      }
      if (handleCppAttributes())
        break;
      [[fallthrough]];
    default:
      ParseDefault();
      break;
    }
  } while (!eof());

  return false;
}

void UnwrappedLineParser::calculateBraceTypes(bool ExpectClassBody) {
  // We'll parse forward through the tokens until we hit
  // a closing brace or eof - note that getNextToken() will
  // parse macros, so this will magically work inside macro
  // definitions, too.
  unsigned StoredPosition = Tokens->getPosition();
  FormatToken *Tok = FormatTok;
  const FormatToken *PrevTok = Tok->Previous;
  // Keep a stack of positions of lbrace tokens. We will
  // update information about whether an lbrace starts a
  // braced init list or a different block during the loop.
  struct StackEntry {
    FormatToken *Tok;
    const FormatToken *PrevTok;
  };
  SmallVector<StackEntry, 8> LBraceStack;
  assert(Tok->is(tok::l_brace));

  do {
    auto *NextTok = Tokens->getNextNonComment();

    if (!Line->InMacroBody && !Style.isTableGen()) {
      // Skip PPDirective lines and comments.
      while (NextTok->is(tok::hash)) {
        NextTok = Tokens->getNextToken();
        if (NextTok->is(tok::pp_not_keyword))
          break;
        do {
          NextTok = Tokens->getNextToken();
        } while (!NextTok->HasUnescapedNewline && NextTok->isNot(tok::eof));

        while (NextTok->is(tok::comment))
          NextTok = Tokens->getNextToken();
      }
    }

    switch (Tok->Tok.getKind()) {
    case tok::l_brace:
      if (Style.isJavaScript() && PrevTok) {
        if (PrevTok->isOneOf(tok::colon, tok::less)) {
          // A ':' indicates this code is in a type, or a braced list
          // following a label in an object literal ({a: {b: 1}}).
          // A '<' could be an object used in a comparison, but that is nonsense
          // code (can never return true), so more likely it is a generic type
          // argument (`X<{a: string; b: number}>`).
          // The code below could be confused by semicolons between the
          // individual members in a type member list, which would normally
          // trigger BK_Block. In both cases, this must be parsed as an inline
          // braced init.
          Tok->setBlockKind(BK_BracedInit);
        } else if (PrevTok->is(tok::r_paren)) {
          // `) { }` can only occur in function or method declarations in JS.
          Tok->setBlockKind(BK_Block);
        }
      } else {
        Tok->setBlockKind(BK_Unknown);
      }
      LBraceStack.push_back({Tok, PrevTok});
      break;
    case tok::r_brace:
      if (LBraceStack.empty())
        break;
      if (auto *LBrace = LBraceStack.back().Tok; LBrace->is(BK_Unknown)) {
        bool ProbablyBracedList = false;
        if (Style.Language == FormatStyle::LK_Proto) {
          ProbablyBracedList = NextTok->isOneOf(tok::comma, tok::r_square);
        } else if (LBrace->isNot(TT_EnumLBrace)) {
          // Using OriginalColumn to distinguish between ObjC methods and
          // binary operators is a bit hacky.
          bool NextIsObjCMethod = NextTok->isOneOf(tok::plus, tok::minus) &&
                                  NextTok->OriginalColumn == 0;

          // Try to detect a braced list. Note that regardless how we mark inner
          // braces here, we will overwrite the BlockKind later if we parse a
          // braced list (where all blocks inside are by default braced lists),
          // or when we explicitly detect blocks (for example while parsing
          // lambdas).

          // If we already marked the opening brace as braced list, the closing
          // must also be part of it.
          ProbablyBracedList = LBrace->is(TT_BracedListLBrace);

          ProbablyBracedList = ProbablyBracedList ||
                               (Style.isJavaScript() &&
                                NextTok->isOneOf(Keywords.kw_of, Keywords.kw_in,
                                                 Keywords.kw_as));
          ProbablyBracedList =
              ProbablyBracedList || (IsCpp && (PrevTok->Tok.isLiteral() ||
                                               NextTok->is(tok::l_paren)));

          // If there is a comma, semicolon or right paren after the closing
          // brace, we assume this is a braced initializer list.
          // FIXME: Some of these do not apply to JS, e.g. "} {" can never be a
          // braced list in JS.
          ProbablyBracedList =
              ProbablyBracedList ||
              NextTok->isOneOf(tok::comma, tok::period, tok::colon,
                               tok::r_paren, tok::r_square, tok::ellipsis);

          // Distinguish between braced list in a constructor initializer list
          // followed by constructor body, or just adjacent blocks.
          ProbablyBracedList =
              ProbablyBracedList ||
              (NextTok->is(tok::l_brace) && LBraceStack.back().PrevTok &&
               LBraceStack.back().PrevTok->isOneOf(tok::identifier,
                                                   tok::greater));

          ProbablyBracedList =
              ProbablyBracedList ||
              (NextTok->is(tok::identifier) &&
               !PrevTok->isOneOf(tok::semi, tok::r_brace, tok::l_brace));

          ProbablyBracedList = ProbablyBracedList ||
                               (NextTok->is(tok::semi) &&
                                (!ExpectClassBody || LBraceStack.size() != 1));

          ProbablyBracedList =
              ProbablyBracedList ||
              (NextTok->isBinaryOperator() && !NextIsObjCMethod);

          if (!Style.isCSharp() && NextTok->is(tok::l_square)) {
            // We can have an array subscript after a braced init
            // list, but C++11 attributes are expected after blocks.
            NextTok = Tokens->getNextToken();
            ProbablyBracedList = NextTok->isNot(tok::l_square);
          }

          // Cpp macro definition body that is a nonempty braced list or block:
          if (IsCpp && Line->InMacroBody && PrevTok != FormatTok &&
              !FormatTok->Previous && NextTok->is(tok::eof) &&
              // A statement can end with only `;` (simple statement), a block
              // closing brace (compound statement), or `:` (label statement).
              // If PrevTok is a block opening brace, Tok ends an empty block.
              !PrevTok->isOneOf(tok::semi, BK_Block, tok::colon)) {
            ProbablyBracedList = true;
          }
        }
        const auto BlockKind = ProbablyBracedList ? BK_BracedInit : BK_Block;
        Tok->setBlockKind(BlockKind);
        LBrace->setBlockKind(BlockKind);
      }
      LBraceStack.pop_back();
      break;
    case tok::identifier:
      if (Tok->isNot(TT_StatementMacro))
        break;
      [[fallthrough]];
    case tok::at:
    case tok::semi:
    case tok::kw_if:
    case tok::kw_while:
    case tok::kw_for:
    case tok::kw_switch:
    case tok::kw_try:
    case tok::kw___try:
      if (!LBraceStack.empty() && LBraceStack.back().Tok->is(BK_Unknown))
        LBraceStack.back().Tok->setBlockKind(BK_Block);
      break;
    default:
      break;
    }

    PrevTok = Tok;
    Tok = NextTok;
  } while (Tok->isNot(tok::eof) && !LBraceStack.empty());

  // Assume other blocks for all unclosed opening braces.
  for (const auto &Entry : LBraceStack)
    if (Entry.Tok->is(BK_Unknown))
      Entry.Tok->setBlockKind(BK_Block);

  FormatTok = Tokens->setPosition(StoredPosition);
}

// Sets the token type of the directly previous right brace.
void UnwrappedLineParser::setPreviousRBraceType(TokenType Type) {
  if (auto Prev = FormatTok->getPreviousNonComment();
      Prev && Prev->is(tok::r_brace)) {
    Prev->setFinalizedType(Type);
  }
}

template <class T>
static inline void hash_combine(std::size_t &seed, const T &v) {
  std::hash<T> hasher;
  seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

size_t UnwrappedLineParser::computePPHash() const {
  size_t h = 0;
  for (const auto &i : PPStack) {
    hash_combine(h, size_t(i.Kind));
    hash_combine(h, i.Line);
  }
  return h;
}

// Checks whether \p ParsedLine might fit on a single line. If \p OpeningBrace
// is not null, subtracts its length (plus the preceding space) when computing
// the length of \p ParsedLine. We must clone the tokens of \p ParsedLine before
// running the token annotator on it so that we can restore them afterward.
bool UnwrappedLineParser::mightFitOnOneLine(
    UnwrappedLine &ParsedLine, const FormatToken *OpeningBrace) const {
  const auto ColumnLimit = Style.ColumnLimit;
  if (ColumnLimit == 0)
    return true;

  auto &Tokens = ParsedLine.Tokens;
  assert(!Tokens.empty());

  const auto *LastToken = Tokens.back().Tok;
  assert(LastToken);

  SmallVector<UnwrappedLineNode> SavedTokens(Tokens.size());

  int Index = 0;
  for (const auto &Token : Tokens) {
    assert(Token.Tok);
    auto &SavedToken = SavedTokens[Index++];
    SavedToken.Tok = new FormatToken;
    SavedToken.Tok->copyFrom(*Token.Tok);
    SavedToken.Children = std::move(Token.Children);
  }

  AnnotatedLine Line(ParsedLine);
  assert(Line.Last == LastToken);

  TokenAnnotator Annotator(Style, Keywords);
  Annotator.annotate(Line);
  Annotator.calculateFormattingInformation(Line);

  auto Length = LastToken->TotalLength;
  if (OpeningBrace) {
    assert(OpeningBrace != Tokens.front().Tok);
    if (auto Prev = OpeningBrace->Previous;
        Prev && Prev->TotalLength + ColumnLimit == OpeningBrace->TotalLength) {
      Length -= ColumnLimit;
    }
    Length -= OpeningBrace->TokenText.size() + 1;
  }

  if (const auto *FirstToken = Line.First; FirstToken->is(tok::r_brace)) {
    assert(!OpeningBrace || OpeningBrace->is(TT_ControlStatementLBrace));
    Length -= FirstToken->TokenText.size() + 1;
  }

  Index = 0;
  for (auto &Token : Tokens) {
    const auto &SavedToken = SavedTokens[Index++];
    Token.Tok->copyFrom(*SavedToken.Tok);
    Token.Children = std::move(SavedToken.Children);
    delete SavedToken.Tok;
  }

  // If these change PPLevel needs to be used for get correct indentation.
  assert(!Line.InMacroBody);
  assert(!Line.InPPDirective);
  return Line.Level * Style.IndentWidth + Length <= ColumnLimit;
}

FormatToken *UnwrappedLineParser::parseBlock(bool MustBeDeclaration,
                                             unsigned AddLevels, bool MunchSemi,
                                             bool KeepBraces,
                                             IfStmtKind *IfKind,
                                             bool UnindentWhitesmithsBraces) {
  auto HandleVerilogBlockLabel = [this]() {
    // ":" name
    if (Style.isVerilog() && FormatTok->is(tok::colon)) {
      nextToken();
      if (Keywords.isVerilogIdentifier(*FormatTok))
        nextToken();
    }
  };

  // Whether this is a Verilog-specific block that has a special header like a
  // module.
  const bool VerilogHierarchy =
      Style.isVerilog() && Keywords.isVerilogHierarchy(*FormatTok);
  assert((FormatTok->isOneOf(tok::l_brace, TT_MacroBlockBegin) ||
          (Style.isVerilog() &&
           (Keywords.isVerilogBegin(*FormatTok) || VerilogHierarchy))) &&
         "'{' or macro block token expected");
  FormatToken *Tok = FormatTok;
  const bool FollowedByComment = Tokens->peekNextToken()->is(tok::comment);
  auto Index = CurrentLines->size();
  const bool MacroBlock = FormatTok->is(TT_MacroBlockBegin);
  FormatTok->setBlockKind(BK_Block);

  // For Whitesmiths mode, jump to the next level prior to skipping over the
  // braces.
  if (!VerilogHierarchy && AddLevels > 0 &&
      Style.BreakBeforeBraces == FormatStyle::BS_Whitesmiths) {
    ++Line->Level;
  }

  size_t PPStartHash = computePPHash();

  const unsigned InitialLevel = Line->Level;
  if (VerilogHierarchy) {
    AddLevels += parseVerilogHierarchyHeader();
  } else {
    nextToken(/*LevelDifference=*/AddLevels);
    HandleVerilogBlockLabel();
  }

  // Bail out if there are too many levels. Otherwise, the stack might overflow.
  if (Line->Level > 300)
    return nullptr;

  if (MacroBlock && FormatTok->is(tok::l_paren))
    parseParens();

  size_t NbPreprocessorDirectives =
      !parsingPPDirective() ? PreprocessorDirectives.size() : 0;
  addUnwrappedLine();
  size_t OpeningLineIndex =
      CurrentLines->empty()
          ? (UnwrappedLine::kInvalidIndex)
          : (CurrentLines->size() - 1 - NbPreprocessorDirectives);

  // Whitesmiths is weird here. The brace needs to be indented for the namespace
  // block, but the block itself may not be indented depending on the style
  // settings. This allows the format to back up one level in those cases.
  if (UnindentWhitesmithsBraces)
    --Line->Level;

  ScopedDeclarationState DeclarationState(*Line, DeclarationScopeStack,
                                          MustBeDeclaration);
  if (AddLevels > 0u && Style.BreakBeforeBraces != FormatStyle::BS_Whitesmiths)
    Line->Level += AddLevels;

  FormatToken *IfLBrace = nullptr;
  const bool SimpleBlock = parseLevel(Tok, IfKind, &IfLBrace);

  if (eof())
    return IfLBrace;

  if (MacroBlock ? FormatTok->isNot(TT_MacroBlockEnd)
                 : FormatTok->isNot(tok::r_brace)) {
    Line->Level = InitialLevel;
    FormatTok->setBlockKind(BK_Block);
    return IfLBrace;
  }

  if (FormatTok->is(tok::r_brace)) {
    FormatTok->setBlockKind(BK_Block);
    if (Tok->is(TT_NamespaceLBrace))
      FormatTok->setFinalizedType(TT_NamespaceRBrace);
  }

  const bool IsFunctionRBrace =
      FormatTok->is(tok::r_brace) && Tok->is(TT_FunctionLBrace);

  auto RemoveBraces = [=]() mutable {
    if (!SimpleBlock)
      return false;
    assert(Tok->isOneOf(TT_ControlStatementLBrace, TT_ElseLBrace));
    assert(FormatTok->is(tok::r_brace));
    const bool WrappedOpeningBrace = !Tok->Previous;
    if (WrappedOpeningBrace && FollowedByComment)
      return false;
    const bool HasRequiredIfBraces = IfLBrace && !IfLBrace->Optional;
    if (KeepBraces && !HasRequiredIfBraces)
      return false;
    if (Tok->isNot(TT_ElseLBrace) || !HasRequiredIfBraces) {
      const FormatToken *Previous = Tokens->getPreviousToken();
      assert(Previous);
      if (Previous->is(tok::r_brace) && !Previous->Optional)
        return false;
    }
    assert(!CurrentLines->empty());
    auto &LastLine = CurrentLines->back();
    if (LastLine.Level == InitialLevel + 1 && !mightFitOnOneLine(LastLine))
      return false;
    if (Tok->is(TT_ElseLBrace))
      return true;
    if (WrappedOpeningBrace) {
      assert(Index > 0);
      --Index; // The line above the wrapped l_brace.
      Tok = nullptr;
    }
    return mightFitOnOneLine((*CurrentLines)[Index], Tok);
  };
  if (RemoveBraces()) {
    Tok->MatchingParen = FormatTok;
    FormatTok->MatchingParen = Tok;
  }

  size_t PPEndHash = computePPHash();

  // Munch the closing brace.
  nextToken(/*LevelDifference=*/-AddLevels);

  // When this is a function block and there is an unnecessary semicolon
  // afterwards then mark it as optional (so the RemoveSemi pass can get rid of
  // it later).
  if (Style.RemoveSemicolon && IsFunctionRBrace) {
    while (FormatTok->is(tok::semi)) {
      FormatTok->Optional = true;
      nextToken();
    }
  }

  HandleVerilogBlockLabel();

  if (MacroBlock && FormatTok->is(tok::l_paren))
    parseParens();

  Line->Level = InitialLevel;

  if (FormatTok->is(tok::kw_noexcept)) {
    // A noexcept in a requires expression.
    nextToken();
  }

  if (FormatTok->is(tok::arrow)) {
    // Following the } or noexcept we can find a trailing return type arrow
    // as part of an implicit conversion constraint.
    nextToken();
    parseStructuralElement();
  }

  if (MunchSemi && FormatTok->is(tok::semi))
    nextToken();

  if (PPStartHash == PPEndHash) {
    Line->MatchingOpeningBlockLineIndex = OpeningLineIndex;
    if (OpeningLineIndex != UnwrappedLine::kInvalidIndex) {
      // Update the opening line to add the forward reference as well
      (*CurrentLines)[OpeningLineIndex].MatchingClosingBlockLineIndex =
          CurrentLines->size() - 1;
    }
  }

  return IfLBrace;
}

static bool isGoogScope(const UnwrappedLine &Line) {
  // FIXME: Closure-library specific stuff should not be hard-coded but be
  // configurable.
  if (Line.Tokens.size() < 4)
    return false;
  auto I = Line.Tokens.begin();
  if (I->Tok->TokenText != "goog")
    return false;
  ++I;
  if (I->Tok->isNot(tok::period))
    return false;
  ++I;
  if (I->Tok->TokenText != "scope")
    return false;
  ++I;
  return I->Tok->is(tok::l_paren);
}

static bool isIIFE(const UnwrappedLine &Line,
                   const AdditionalKeywords &Keywords) {
  // Look for the start of an immediately invoked anonymous function.
  // https://en.wikipedia.org/wiki/Immediately-invoked_function_expression
  // This is commonly done in JavaScript to create a new, anonymous scope.
  // Example: (function() { ... })()
  if (Line.Tokens.size() < 3)
    return false;
  auto I = Line.Tokens.begin();
  if (I->Tok->isNot(tok::l_paren))
    return false;
  ++I;
  if (I->Tok->isNot(Keywords.kw_function))
    return false;
  ++I;
  return I->Tok->is(tok::l_paren);
}

static bool ShouldBreakBeforeBrace(const FormatStyle &Style,
                                   const FormatToken &InitialToken) {
  tok::TokenKind Kind = InitialToken.Tok.getKind();
  if (InitialToken.is(TT_NamespaceMacro))
    Kind = tok::kw_namespace;

  switch (Kind) {
  case tok::kw_namespace:
    return Style.BraceWrapping.AfterNamespace;
  case tok::kw_class:
    return Style.BraceWrapping.AfterClass;
  case tok::kw_union:
    return Style.BraceWrapping.AfterUnion;
  case tok::kw_struct:
    return Style.BraceWrapping.AfterStruct;
  case tok::kw_enum:
    return Style.BraceWrapping.AfterEnum;
  default:
    return false;
  }
}

void UnwrappedLineParser::parseChildBlock() {
  assert(FormatTok->is(tok::l_brace));
  FormatTok->setBlockKind(BK_Block);
  const FormatToken *OpeningBrace = FormatTok;
  nextToken();
  {
    bool SkipIndent = (Style.isJavaScript() &&
                       (isGoogScope(*Line) || isIIFE(*Line, Keywords)));
    ScopedLineState LineState(*this);
    ScopedDeclarationState DeclarationState(*Line, DeclarationScopeStack,
                                            /*MustBeDeclaration=*/false);
    Line->Level += SkipIndent ? 0 : 1;
    parseLevel(OpeningBrace);
    flushComments(isOnNewLine(*FormatTok));
    Line->Level -= SkipIndent ? 0 : 1;
  }
  nextToken();
}

void UnwrappedLineParser::parsePPDirective() {
  assert(FormatTok->is(tok::hash) && "'#' expected");
  ScopedMacroState MacroState(*Line, Tokens, FormatTok);

  nextToken();

  if (!FormatTok->Tok.getIdentifierInfo()) {
    parsePPUnknown();
    return;
  }

  switch (FormatTok->Tok.getIdentifierInfo()->getPPKeywordID()) {
  case tok::pp_define:
    parsePPDefine();
    return;
  case tok::pp_if:
    parsePPIf(/*IfDef=*/false);
    break;
  case tok::pp_ifdef:
  case tok::pp_ifndef:
    parsePPIf(/*IfDef=*/true);
    break;
  case tok::pp_else:
  case tok::pp_elifdef:
  case tok::pp_elifndef:
  case tok::pp_elif:
    parsePPElse();
    break;
  case tok::pp_endif:
    parsePPEndIf();
    break;
  case tok::pp_pragma:
    parsePPPragma();
    break;
  default:
    parsePPUnknown();
    break;
  }
}

void UnwrappedLineParser::conditionalCompilationCondition(bool Unreachable) {
  size_t Line = CurrentLines->size();
  if (CurrentLines == &PreprocessorDirectives)
    Line += Lines.size();

  if (Unreachable ||
      (!PPStack.empty() && PPStack.back().Kind == PP_Unreachable)) {
    PPStack.push_back({PP_Unreachable, Line});
  } else {
    PPStack.push_back({PP_Conditional, Line});
  }
}

void UnwrappedLineParser::conditionalCompilationStart(bool Unreachable) {
  ++PPBranchLevel;
  assert(PPBranchLevel >= 0 && PPBranchLevel <= (int)PPLevelBranchIndex.size());
  if (PPBranchLevel == (int)PPLevelBranchIndex.size()) {
    PPLevelBranchIndex.push_back(0);
    PPLevelBranchCount.push_back(0);
  }
  PPChainBranchIndex.push(Unreachable ? -1 : 0);
  bool Skip = PPLevelBranchIndex[PPBranchLevel] > 0;
  conditionalCompilationCondition(Unreachable || Skip);
}

void UnwrappedLineParser::conditionalCompilationAlternative() {
  if (!PPStack.empty())
    PPStack.pop_back();
  assert(PPBranchLevel < (int)PPLevelBranchIndex.size());
  if (!PPChainBranchIndex.empty())
    ++PPChainBranchIndex.top();
  conditionalCompilationCondition(
      PPBranchLevel >= 0 && !PPChainBranchIndex.empty() &&
      PPLevelBranchIndex[PPBranchLevel] != PPChainBranchIndex.top());
}

void UnwrappedLineParser::conditionalCompilationEnd() {
  assert(PPBranchLevel < (int)PPLevelBranchIndex.size());
  if (PPBranchLevel >= 0 && !PPChainBranchIndex.empty()) {
    if (PPChainBranchIndex.top() + 1 > PPLevelBranchCount[PPBranchLevel])
      PPLevelBranchCount[PPBranchLevel] = PPChainBranchIndex.top() + 1;
  }
  // Guard against #endif's without #if.
  if (PPBranchLevel > -1)
    --PPBranchLevel;
  if (!PPChainBranchIndex.empty())
    PPChainBranchIndex.pop();
  if (!PPStack.empty())
    PPStack.pop_back();
}

void UnwrappedLineParser::parsePPIf(bool IfDef) {
  bool IfNDef = FormatTok->is(tok::pp_ifndef);
  nextToken();
  bool Unreachable = false;
  if (!IfDef && (FormatTok->is(tok::kw_false) || FormatTok->TokenText == "0"))
    Unreachable = true;
  if (IfDef && !IfNDef && FormatTok->TokenText == "SWIG")
    Unreachable = true;
  conditionalCompilationStart(Unreachable);
  FormatToken *IfCondition = FormatTok;
  // If there's a #ifndef on the first line, and the only lines before it are
  // comments, it could be an include guard.
  bool MaybeIncludeGuard = IfNDef;
  if (IncludeGuard == IG_Inited && MaybeIncludeGuard) {
    for (auto &Line : Lines) {
      if (Line.Tokens.front().Tok->isNot(tok::comment)) {
        MaybeIncludeGuard = false;
        IncludeGuard = IG_Rejected;
        break;
      }
    }
  }
  --PPBranchLevel;
  parsePPUnknown();
  ++PPBranchLevel;
  if (IncludeGuard == IG_Inited && MaybeIncludeGuard) {
    IncludeGuard = IG_IfNdefed;
    IncludeGuardToken = IfCondition;
  }
}

void UnwrappedLineParser::parsePPElse() {
  // If a potential include guard has an #else, it's not an include guard.
  if (IncludeGuard == IG_Defined && PPBranchLevel == 0)
    IncludeGuard = IG_Rejected;
  // Don't crash when there is an #else without an #if.
  assert(PPBranchLevel >= -1);
  if (PPBranchLevel == -1)
    conditionalCompilationStart(/*Unreachable=*/true);
  conditionalCompilationAlternative();
  --PPBranchLevel;
  parsePPUnknown();
  ++PPBranchLevel;
}

void UnwrappedLineParser::parsePPEndIf() {
  conditionalCompilationEnd();
  parsePPUnknown();
  // If the #endif of a potential include guard is the last thing in the file,
  // then we found an include guard.
  if (IncludeGuard == IG_Defined && PPBranchLevel == -1 && Tokens->isEOF() &&
      Style.IndentPPDirectives != FormatStyle::PPDIS_None) {
    IncludeGuard = IG_Found;
  }
}

void UnwrappedLineParser::parsePPDefine() {
  nextToken();

  if (!FormatTok->Tok.getIdentifierInfo()) {
    IncludeGuard = IG_Rejected;
    IncludeGuardToken = nullptr;
    parsePPUnknown();
    return;
  }

  if (IncludeGuard == IG_IfNdefed &&
      IncludeGuardToken->TokenText == FormatTok->TokenText) {
    IncludeGuard = IG_Defined;
    IncludeGuardToken = nullptr;
    for (auto &Line : Lines) {
      if (!Line.Tokens.front().Tok->isOneOf(tok::comment, tok::hash)) {
        IncludeGuard = IG_Rejected;
        break;
      }
    }
  }

  // In the context of a define, even keywords should be treated as normal
  // identifiers. Setting the kind to identifier is not enough, because we need
  // to treat additional keywords like __except as well, which are already
  // identifiers. Setting the identifier info to null interferes with include
  // guard processing above, and changes preprocessing nesting.
  FormatTok->Tok.setKind(tok::identifier);
  FormatTok->Tok.setIdentifierInfo(Keywords.kw_internal_ident_after_define);
  nextToken();
  if (FormatTok->Tok.getKind() == tok::l_paren &&
      !FormatTok->hasWhitespaceBefore()) {
    parseParens();
  }
  if (Style.IndentPPDirectives != FormatStyle::PPDIS_None)
    Line->Level += PPBranchLevel + 1;
  addUnwrappedLine();
  ++Line->Level;

  Line->PPLevel = PPBranchLevel + (IncludeGuard == IG_Defined ? 0 : 1);
  assert((int)Line->PPLevel >= 0);
  Line->InMacroBody = true;

  if (Style.SkipMacroDefinitionBody) {
    while (!eof()) {
      FormatTok->Finalized = true;
      FormatTok = Tokens->getNextToken();
    }
    addUnwrappedLine();
    return;
  }

  // Errors during a preprocessor directive can only affect the layout of the
  // preprocessor directive, and thus we ignore them. An alternative approach
  // would be to use the same approach we use on the file level (no
  // re-indentation if there was a structural error) within the macro
  // definition.
  parseFile();
}

void UnwrappedLineParser::parsePPPragma() {
  Line->InPragmaDirective = true;
  parsePPUnknown();
}

void UnwrappedLineParser::parsePPUnknown() {
  do {
    nextToken();
  } while (!eof());
  if (Style.IndentPPDirectives != FormatStyle::PPDIS_None)
    Line->Level += PPBranchLevel + 1;
  addUnwrappedLine();
}

// Here we exclude certain tokens that are not usually the first token in an
// unwrapped line. This is used in attempt to distinguish macro calls without
// trailing semicolons from other constructs split to several lines.
static bool tokenCanStartNewLine(const FormatToken &Tok) {
  // Semicolon can be a null-statement, l_square can be a start of a macro or
  // a C++11 attribute, but this doesn't seem to be common.
  return !Tok.isOneOf(tok::semi, tok::l_brace,
                      // Tokens that can only be used as binary operators and a
                      // part of overloaded operator names.
                      tok::period, tok::periodstar, tok::arrow, tok::arrowstar,
                      tok::less, tok::greater, tok::slash, tok::percent,
                      tok::lessless, tok::greatergreater, tok::equal,
                      tok::plusequal, tok::minusequal, tok::starequal,
                      tok::slashequal, tok::percentequal, tok::ampequal,
                      tok::pipeequal, tok::caretequal, tok::greatergreaterequal,
                      tok::lesslessequal,
                      // Colon is used in labels, base class lists, initializer
                      // lists, range-based for loops, ternary operator, but
                      // should never be the first token in an unwrapped line.
                      tok::colon,
                      // 'noexcept' is a trailing annotation.
                      tok::kw_noexcept);
}

static bool mustBeJSIdent(const AdditionalKeywords &Keywords,
                          const FormatToken *FormatTok) {
  // FIXME: This returns true for C/C++ keywords like 'struct'.
  return FormatTok->is(tok::identifier) &&
         (!FormatTok->Tok.getIdentifierInfo() ||
          !FormatTok->isOneOf(
              Keywords.kw_in, Keywords.kw_of, Keywords.kw_as, Keywords.kw_async,
              Keywords.kw_await, Keywords.kw_yield, Keywords.kw_finally,
              Keywords.kw_function, Keywords.kw_import, Keywords.kw_is,
              Keywords.kw_let, Keywords.kw_var, tok::kw_const,
              Keywords.kw_abstract, Keywords.kw_extends, Keywords.kw_implements,
              Keywords.kw_instanceof, Keywords.kw_interface,
              Keywords.kw_override, Keywords.kw_throws, Keywords.kw_from));
}

static bool mustBeJSIdentOrValue(const AdditionalKeywords &Keywords,
                                 const FormatToken *FormatTok) {
  return FormatTok->Tok.isLiteral() ||
         FormatTok->isOneOf(tok::kw_true, tok::kw_false) ||
         mustBeJSIdent(Keywords, FormatTok);
}

// isJSDeclOrStmt returns true if |FormatTok| starts a declaration or statement
// when encountered after a value (see mustBeJSIdentOrValue).
static bool isJSDeclOrStmt(const AdditionalKeywords &Keywords,
                           const FormatToken *FormatTok) {
  return FormatTok->isOneOf(
      tok::kw_return, Keywords.kw_yield,
      // conditionals
      tok::kw_if, tok::kw_else,
      // loops
      tok::kw_for, tok::kw_while, tok::kw_do, tok::kw_continue, tok::kw_break,
      // switch/case
      tok::kw_switch, tok::kw_case,
      // exceptions
      tok::kw_throw, tok::kw_try, tok::kw_catch, Keywords.kw_finally,
      // declaration
      tok::kw_const, tok::kw_class, Keywords.kw_var, Keywords.kw_let,
      Keywords.kw_async, Keywords.kw_function,
      // import/export
      Keywords.kw_import, tok::kw_export);
}

// Checks whether a token is a type in K&R C (aka C78).
static bool isC78Type(const FormatToken &Tok) {
  return Tok.isOneOf(tok::kw_char, tok::kw_short, tok::kw_int, tok::kw_long,
                     tok::kw_unsigned, tok::kw_float, tok::kw_double,
                     tok::identifier);
}

// This function checks whether a token starts the first parameter declaration
// in a K&R C (aka C78) function definition, e.g.:
//   int f(a, b)
//   short a, b;
//   {
//      return a + b;
//   }
static bool isC78ParameterDecl(const FormatToken *Tok, const FormatToken *Next,
                               const FormatToken *FuncName) {
  assert(Tok);
  assert(Next);
  assert(FuncName);

  if (FuncName->isNot(tok::identifier))
    return false;

  const FormatToken *Prev = FuncName->Previous;
  if (!Prev || (Prev->isNot(tok::star) && !isC78Type(*Prev)))
    return false;

  if (!isC78Type(*Tok) &&
      !Tok->isOneOf(tok::kw_register, tok::kw_struct, tok::kw_union)) {
    return false;
  }

  if (Next->isNot(tok::star) && !Next->Tok.getIdentifierInfo())
    return false;

  Tok = Tok->Previous;
  if (!Tok || Tok->isNot(tok::r_paren))
    return false;

  Tok = Tok->Previous;
  if (!Tok || Tok->isNot(tok::identifier))
    return false;

  return Tok->Previous && Tok->Previous->isOneOf(tok::l_paren, tok::comma);
}

bool UnwrappedLineParser::parseModuleImport() {
  assert(FormatTok->is(Keywords.kw_import) && "'import' expected");

  if (auto Token = Tokens->peekNextToken(/*SkipComment=*/true);
      !Token->Tok.getIdentifierInfo() &&
      !Token->isOneOf(tok::colon, tok::less, tok::string_literal)) {
    return false;
  }

  nextToken();
  while (!eof()) {
    if (FormatTok->is(tok::colon)) {
      FormatTok->setFinalizedType(TT_ModulePartitionColon);
    }
    // Handle import <foo/bar.h> as we would an include statement.
    else if (FormatTok->is(tok::less)) {
      nextToken();
      while (!FormatTok->isOneOf(tok::semi, tok::greater, tok::eof)) {
        // Mark tokens up to the trailing line comments as implicit string
        // literals.
        if (FormatTok->isNot(tok::comment) &&
            !FormatTok->TokenText.starts_with("//")) {
          FormatTok->setFinalizedType(TT_ImplicitStringLiteral);
        }
        nextToken();
      }
    }
    if (FormatTok->is(tok::semi)) {
      nextToken();
      break;
    }
    nextToken();
  }

  addUnwrappedLine();
  return true;
}

// readTokenWithJavaScriptASI reads the next token and terminates the current
// line if JavaScript Automatic Semicolon Insertion must
// happen between the current token and the next token.
//
// This method is conservative - it cannot cover all edge cases of JavaScript,
// but only aims to correctly handle certain well known cases. It *must not*
// return true in speculative cases.
void UnwrappedLineParser::readTokenWithJavaScriptASI() {
  FormatToken *Previous = FormatTok;
  readToken();
  FormatToken *Next = FormatTok;

  bool IsOnSameLine =
      CommentsBeforeNextToken.empty()
          ? Next->NewlinesBefore == 0
          : CommentsBeforeNextToken.front()->NewlinesBefore == 0;
  if (IsOnSameLine)
    return;

  bool PreviousMustBeValue = mustBeJSIdentOrValue(Keywords, Previous);
  bool PreviousStartsTemplateExpr =
      Previous->is(TT_TemplateString) && Previous->TokenText.ends_with("${");
  if (PreviousMustBeValue || Previous->is(tok::r_paren)) {
    // If the line contains an '@' sign, the previous token might be an
    // annotation, which can precede another identifier/value.
    bool HasAt = llvm::any_of(Line->Tokens, [](UnwrappedLineNode &LineNode) {
      return LineNode.Tok->is(tok::at);
    });
    if (HasAt)
      return;
  }
  if (Next->is(tok::exclaim) && PreviousMustBeValue)
    return addUnwrappedLine();
  bool NextMustBeValue = mustBeJSIdentOrValue(Keywords, Next);
  bool NextEndsTemplateExpr =
      Next->is(TT_TemplateString) && Next->TokenText.starts_with("}");
  if (NextMustBeValue && !NextEndsTemplateExpr && !PreviousStartsTemplateExpr &&
      (PreviousMustBeValue ||
       Previous->isOneOf(tok::r_square, tok::r_paren, tok::plusplus,
                         tok::minusminus))) {
    return addUnwrappedLine();
  }
  if ((PreviousMustBeValue || Previous->is(tok::r_paren)) &&
      isJSDeclOrStmt(Keywords, Next)) {
    return addUnwrappedLine();
  }
}

void UnwrappedLineParser::parseStructuralElement(
    const FormatToken *OpeningBrace, IfStmtKind *IfKind,
    FormatToken **IfLeftBrace, bool *HasDoWhile, bool *HasLabel) {
  if (Style.Language == FormatStyle::LK_TableGen &&
      FormatTok->is(tok::pp_include)) {
    nextToken();
    if (FormatTok->is(tok::string_literal))
      nextToken();
    addUnwrappedLine();
    return;
  }

  if (IsCpp) {
    while (FormatTok->is(tok::l_square) && handleCppAttributes()) {
    }
  } else if (Style.isVerilog()) {
    if (Keywords.isVerilogStructuredProcedure(*FormatTok)) {
      parseForOrWhileLoop(/*HasParens=*/false);
      return;
    }
    if (FormatTok->isOneOf(Keywords.kw_foreach, Keywords.kw_repeat)) {
      parseForOrWhileLoop();
      return;
    }
    if (FormatTok->isOneOf(tok::kw_restrict, Keywords.kw_assert,
                           Keywords.kw_assume, Keywords.kw_cover)) {
      parseIfThenElse(IfKind, /*KeepBraces=*/false, /*IsVerilogAssert=*/true);
      return;
    }

    // Skip things that can exist before keywords like 'if' and 'case'.
    while (true) {
      if (FormatTok->isOneOf(Keywords.kw_priority, Keywords.kw_unique,
                             Keywords.kw_unique0)) {
        nextToken();
      } else if (FormatTok->is(tok::l_paren) &&
                 Tokens->peekNextToken()->is(tok::star)) {
        parseParens();
      } else {
        break;
      }
    }
  }

  // Tokens that only make sense at the beginning of a line.
  if (FormatTok->isAccessSpecifierKeyword()) {
    if (Style.Language == FormatStyle::LK_Java || Style.isJavaScript() ||
        Style.isCSharp()) {
      nextToken();
    } else {
      parseAccessSpecifier();
    }
    return;
  }
  switch (FormatTok->Tok.getKind()) {
  case tok::kw_asm:
    nextToken();
    if (FormatTok->is(tok::l_brace)) {
      FormatTok->setFinalizedType(TT_InlineASMBrace);
      nextToken();
      while (FormatTok && !eof()) {
        if (FormatTok->is(tok::r_brace)) {
          FormatTok->setFinalizedType(TT_InlineASMBrace);
          nextToken();
          addUnwrappedLine();
          break;
        }
        FormatTok->Finalized = true;
        nextToken();
      }
    }
    break;
  case tok::kw_namespace:
    parseNamespace();
    return;
  case tok::kw_if: {
    if (Style.isJavaScript() && Line->MustBeDeclaration) {
      // field/method declaration.
      break;
    }
    FormatToken *Tok = parseIfThenElse(IfKind);
    if (IfLeftBrace)
      *IfLeftBrace = Tok;
    return;
  }
  case tok::kw_for:
  case tok::kw_while:
    if (Style.isJavaScript() && Line->MustBeDeclaration) {
      // field/method declaration.
      break;
    }
    parseForOrWhileLoop();
    return;
  case tok::kw_do:
    if (Style.isJavaScript() && Line->MustBeDeclaration) {
      // field/method declaration.
      break;
    }
    parseDoWhile();
    if (HasDoWhile)
      *HasDoWhile = true;
    return;
  case tok::kw_switch:
    if (Style.isJavaScript() && Line->MustBeDeclaration) {
      // 'switch: string' field declaration.
      break;
    }
    parseSwitch(/*IsExpr=*/false);
    return;
  case tok::kw_default: {
    // In Verilog default along with other labels are handled in the next loop.
    if (Style.isVerilog())
      break;
    if (Style.isJavaScript() && Line->MustBeDeclaration) {
      // 'default: string' field declaration.
      break;
    }
    auto *Default = FormatTok;
    nextToken();
    if (FormatTok->is(tok::colon)) {
      FormatTok->setFinalizedType(TT_CaseLabelColon);
      parseLabel();
      return;
    }
    if (FormatTok->is(tok::arrow)) {
      FormatTok->setFinalizedType(TT_CaseLabelArrow);
      Default->setFinalizedType(TT_SwitchExpressionLabel);
      parseLabel();
      return;
    }
    // e.g. "default void f() {}" in a Java interface.
    break;
  }
  case tok::kw_case:
    // Proto: there are no switch/case statements.
    if (Style.Language == FormatStyle::LK_Proto) {
      nextToken();
      return;
    }
    if (Style.isVerilog()) {
      parseBlock();
      addUnwrappedLine();
      return;
    }
    if (Style.isJavaScript() && Line->MustBeDeclaration) {
      // 'case: string' field declaration.
      nextToken();
      break;
    }
    parseCaseLabel();
    return;
  case tok::kw_try:
  case tok::kw___try:
    if (Style.isJavaScript() && Line->MustBeDeclaration) {
      // field/method declaration.
      break;
    }
    parseTryCatch();
    return;
  case tok::kw_extern:
    nextToken();
    if (Style.isVerilog()) {
      // In Verilog and extern module declaration looks like a start of module.
      // But there is no body and endmodule. So we handle it separately.
      if (Keywords.isVerilogHierarchy(*FormatTok)) {
        parseVerilogHierarchyHeader();
        return;
      }
    } else if (FormatTok->is(tok::string_literal)) {
      nextToken();
      if (FormatTok->is(tok::l_brace)) {
        if (Style.BraceWrapping.AfterExternBlock)
          addUnwrappedLine();
        // Either we indent or for backwards compatibility we follow the
        // AfterExternBlock style.
        unsigned AddLevels =
            (Style.IndentExternBlock == FormatStyle::IEBS_Indent) ||
                    (Style.BraceWrapping.AfterExternBlock &&
                     Style.IndentExternBlock ==
                         FormatStyle::IEBS_AfterExternBlock)
                ? 1u
                : 0u;
        parseBlock(/*MustBeDeclaration=*/true, AddLevels);
        addUnwrappedLine();
        return;
      }
    }
    break;
  case tok::kw_export:
    if (Style.isJavaScript()) {
      parseJavaScriptEs6ImportExport();
      return;
    }
    if (IsCpp) {
      nextToken();
      if (FormatTok->is(tok::kw_namespace)) {
        parseNamespace();
        return;
      }
      if (FormatTok->is(Keywords.kw_import) && parseModuleImport())
        return;
    }
    break;
  case tok::kw_inline:
    nextToken();
    if (FormatTok->is(tok::kw_namespace)) {
      parseNamespace();
      return;
    }
    break;
  case tok::identifier:
    if (FormatTok->is(TT_ForEachMacro)) {
      parseForOrWhileLoop();
      return;
    }
    if (FormatTok->is(TT_MacroBlockBegin)) {
      parseBlock(/*MustBeDeclaration=*/false, /*AddLevels=*/1u,
                 /*MunchSemi=*/false);
      return;
    }
    if (FormatTok->is(Keywords.kw_import)) {
      if (Style.isJavaScript()) {
        parseJavaScriptEs6ImportExport();
        return;
      }
      if (Style.Language == FormatStyle::LK_Proto) {
        nextToken();
        if (FormatTok->is(tok::kw_public))
          nextToken();
        if (FormatTok->isNot(tok::string_literal))
          return;
        nextToken();
        if (FormatTok->is(tok::semi))
          nextToken();
        addUnwrappedLine();
        return;
      }
      if (IsCpp && parseModuleImport())
        return;
    }
    if (IsCpp && FormatTok->isOneOf(Keywords.kw_signals, Keywords.kw_qsignals,
                                    Keywords.kw_slots, Keywords.kw_qslots)) {
      nextToken();
      if (FormatTok->is(tok::colon)) {
        nextToken();
        addUnwrappedLine();
        return;
      }
    }
    if (IsCpp && FormatTok->is(TT_StatementMacro)) {
      parseStatementMacro();
      return;
    }
    if (IsCpp && FormatTok->is(TT_NamespaceMacro)) {
      parseNamespace();
      return;
    }
    // In Verilog labels can be any expression, so we don't do them here.
    // JS doesn't have macros, and within classes colons indicate fields, not
    // labels.
    // TableGen doesn't have labels.
    if (!Style.isJavaScript() && !Style.isVerilog() && !Style.isTableGen() &&
        Tokens->peekNextToken()->is(tok::colon) && !Line->MustBeDeclaration) {
      nextToken();
      if (!Line->InMacroBody || CurrentLines->size() > 1)
        Line->Tokens.begin()->Tok->MustBreakBefore = true;
      FormatTok->setFinalizedType(TT_GotoLabelColon);
      parseLabel(!Style.IndentGotoLabels);
      if (HasLabel)
        *HasLabel = true;
      return;
    }
    // In all other cases, parse the declaration.
    break;
  default:
    break;
  }

  for (const bool InRequiresExpression =
           OpeningBrace && OpeningBrace->is(TT_RequiresExpressionLBrace);
       !eof();) {
    if (IsCpp && FormatTok->isCppAlternativeOperatorKeyword()) {
      if (auto *Next = Tokens->peekNextToken(/*SkipComment=*/true);
          Next && Next->isBinaryOperator()) {
        FormatTok->Tok.setKind(tok::identifier);
      }
    }
    const FormatToken *Previous = FormatTok->Previous;
    switch (FormatTok->Tok.getKind()) {
    case tok::at:
      nextToken();
      if (FormatTok->is(tok::l_brace)) {
        nextToken();
        parseBracedList();
        break;
      } else if (Style.Language == FormatStyle::LK_Java &&
                 FormatTok->is(Keywords.kw_interface)) {
        nextToken();
        break;
      }
      switch (FormatTok->Tok.getObjCKeywordID()) {
      case tok::objc_public:
      case tok::objc_protected:
      case tok::objc_package:
      case tok::objc_private:
        return parseAccessSpecifier();
      case tok::objc_interface:
      case tok::objc_implementation:
        return parseObjCInterfaceOrImplementation();
      case tok::objc_protocol:
        if (parseObjCProtocol())
          return;
        break;
      case tok::objc_end:
        return; // Handled by the caller.
      case tok::objc_optional:
      case tok::objc_required:
        nextToken();
        addUnwrappedLine();
        return;
      case tok::objc_autoreleasepool:
        nextToken();
        if (FormatTok->is(tok::l_brace)) {
          if (Style.BraceWrapping.AfterControlStatement ==
              FormatStyle::BWACS_Always) {
            addUnwrappedLine();
          }
          parseBlock();
        }
        addUnwrappedLine();
        return;
      case tok::objc_synchronized:
        nextToken();
        if (FormatTok->is(tok::l_paren)) {
          // Skip synchronization object
          parseParens();
        }
        if (FormatTok->is(tok::l_brace)) {
          if (Style.BraceWrapping.AfterControlStatement ==
              FormatStyle::BWACS_Always) {
            addUnwrappedLine();
          }
          parseBlock();
        }
        addUnwrappedLine();
        return;
      case tok::objc_try:
        // This branch isn't strictly necessary (the kw_try case below would
        // do this too after the tok::at is parsed above).  But be explicit.
        parseTryCatch();
        return;
      default:
        break;
      }
      break;
    case tok::kw_requires: {
      if (IsCpp) {
        bool ParsedClause = parseRequires();
        if (ParsedClause)
          return;
      } else {
        nextToken();
      }
      break;
    }
    case tok::kw_enum:
      // Ignore if this is part of "template <enum ..." or "... -> enum" or
      // "template <..., enum ...>".
      if (Previous && Previous->isOneOf(tok::less, tok::arrow, tok::comma)) {
        nextToken();
        break;
      }

      // parseEnum falls through and does not yet add an unwrapped line as an
      // enum definition can start a structural element.
      if (!parseEnum())
        break;
      // This only applies to C++ and Verilog.
      if (!IsCpp && !Style.isVerilog()) {
        addUnwrappedLine();
        return;
      }
      break;
    case tok::kw_typedef:
      nextToken();
      if (FormatTok->isOneOf(Keywords.kw_NS_ENUM, Keywords.kw_NS_OPTIONS,
                             Keywords.kw_CF_ENUM, Keywords.kw_CF_OPTIONS,
                             Keywords.kw_CF_CLOSED_ENUM,
                             Keywords.kw_NS_CLOSED_ENUM)) {
        parseEnum();
      }
      break;
    case tok::kw_class:
      if (Style.isVerilog()) {
        parseBlock();
        addUnwrappedLine();
        return;
      }
      if (Style.isTableGen()) {
        // Do nothing special. In this case the l_brace becomes FunctionLBrace.
        // This is same as def and so on.
        nextToken();
        break;
      }
      [[fallthrough]];
    case tok::kw_struct:
    case tok::kw_union:
      if (parseStructLike())
        return;
      break;
    case tok::kw_decltype:
      nextToken();
      if (FormatTok->is(tok::l_paren)) {
        parseParens();
        assert(FormatTok->Previous);
        if (FormatTok->Previous->endsSequence(tok::r_paren, tok::kw_auto,
                                              tok::l_paren)) {
          Line->SeenDecltypeAuto = true;
        }
      }
      break;
    case tok::period:
      nextToken();
      // In Java, classes have an implicit static member "class".
      if (Style.Language == FormatStyle::LK_Java && FormatTok &&
          FormatTok->is(tok::kw_class)) {
        nextToken();
      }
      if (Style.isJavaScript() && FormatTok &&
          FormatTok->Tok.getIdentifierInfo()) {
        // JavaScript only has pseudo keywords, all keywords are allowed to
        // appear in "IdentifierName" positions. See http://es5.github.io/#x7.6
        nextToken();
      }
      break;
    case tok::semi:
      nextToken();
      addUnwrappedLine();
      return;
    case tok::r_brace:
      addUnwrappedLine();
      return;
    case tok::l_paren: {
      parseParens();
      // Break the unwrapped line if a K&R C function definition has a parameter
      // declaration.
      if (OpeningBrace || !IsCpp || !Previous || eof())
        break;
      if (isC78ParameterDecl(FormatTok,
                             Tokens->peekNextToken(/*SkipComment=*/true),
                             Previous)) {
        addUnwrappedLine();
        return;
      }
      break;
    }
    case tok::kw_operator:
      nextToken();
      if (FormatTok->isBinaryOperator())
        nextToken();
      break;
    case tok::caret:
      nextToken();
      // Block return type.
      if (FormatTok->Tok.isAnyIdentifier() || FormatTok->isTypeName(LangOpts)) {
        nextToken();
        // Return types: pointers are ok too.
        while (FormatTok->is(tok::star))
          nextToken();
      }
      // Block argument list.
      if (FormatTok->is(tok::l_paren))
        parseParens();
      // Block body.
      if (FormatTok->is(tok::l_brace))
        parseChildBlock();
      break;
    case tok::l_brace:
      if (InRequiresExpression)
        FormatTok->setFinalizedType(TT_BracedListLBrace);
      if (!tryToParsePropertyAccessor() && !tryToParseBracedList()) {
        IsDecltypeAutoFunction = Line->SeenDecltypeAuto;
        // A block outside of parentheses must be the last part of a
        // structural element.
        // FIXME: Figure out cases where this is not true, and add projections
        // for them (the one we know is missing are lambdas).
        if (Style.Language == FormatStyle::LK_Java &&
            Line->Tokens.front().Tok->is(Keywords.kw_synchronized)) {
          // If necessary, we could set the type to something different than
          // TT_FunctionLBrace.
          if (Style.BraceWrapping.AfterControlStatement ==
              FormatStyle::BWACS_Always) {
            addUnwrappedLine();
          }
        } else if (Style.BraceWrapping.AfterFunction) {
          addUnwrappedLine();
        }
        if (!Previous || Previous->isNot(TT_TypeDeclarationParen))
          FormatTok->setFinalizedType(TT_FunctionLBrace);
        parseBlock();
        IsDecltypeAutoFunction = false;
        addUnwrappedLine();
        return;
      }
      // Otherwise this was a braced init list, and the structural
      // element continues.
      break;
    case tok::kw_try:
      if (Style.isJavaScript() && Line->MustBeDeclaration) {
        // field/method declaration.
        nextToken();
        break;
      }
      // We arrive here when parsing function-try blocks.
      if (Style.BraceWrapping.AfterFunction)
        addUnwrappedLine();
      parseTryCatch();
      return;
    case tok::identifier: {
      if (Style.isCSharp() && FormatTok->is(Keywords.kw_where) &&
          Line->MustBeDeclaration) {
        addUnwrappedLine();
        parseCSharpGenericTypeConstraint();
        break;
      }
      if (FormatTok->is(TT_MacroBlockEnd)) {
        addUnwrappedLine();
        return;
      }

      // Function declarations (as opposed to function expressions) are parsed
      // on their own unwrapped line by continuing this loop. Function
      // expressions (functions that are not on their own line) must not create
      // a new unwrapped line, so they are special cased below.
      size_t TokenCount = Line->Tokens.size();
      if (Style.isJavaScript() && FormatTok->is(Keywords.kw_function) &&
          (TokenCount > 1 ||
           (TokenCount == 1 &&
            Line->Tokens.front().Tok->isNot(Keywords.kw_async)))) {
        tryToParseJSFunction();
        break;
      }
      if ((Style.isJavaScript() || Style.Language == FormatStyle::LK_Java) &&
          FormatTok->is(Keywords.kw_interface)) {
        if (Style.isJavaScript()) {
          // In JavaScript/TypeScript, "interface" can be used as a standalone
          // identifier, e.g. in `var interface = 1;`. If "interface" is
          // followed by another identifier, it is very like to be an actual
          // interface declaration.
          unsigned StoredPosition = Tokens->getPosition();
          FormatToken *Next = Tokens->getNextToken();
          FormatTok = Tokens->setPosition(StoredPosition);
          if (!mustBeJSIdent(Keywords, Next)) {
            nextToken();
            break;
          }
        }
        parseRecord();
        addUnwrappedLine();
        return;
      }

      if (Style.isVerilog()) {
        if (FormatTok->is(Keywords.kw_table)) {
          parseVerilogTable();
          return;
        }
        if (Keywords.isVerilogBegin(*FormatTok) ||
            Keywords.isVerilogHierarchy(*FormatTok)) {
          parseBlock();
          addUnwrappedLine();
          return;
        }
      }

      if (!IsCpp && FormatTok->is(Keywords.kw_interface)) {
        if (parseStructLike())
          return;
        break;
      }

      if (IsCpp && FormatTok->is(TT_StatementMacro)) {
        parseStatementMacro();
        return;
      }

      // See if the following token should start a new unwrapped line.
      StringRef Text = FormatTok->TokenText;

      FormatToken *PreviousToken = FormatTok;
      nextToken();

      // JS doesn't have macros, and within classes colons indicate fields, not
      // labels.
      if (Style.isJavaScript())
        break;

      auto OneTokenSoFar = [&]() {
        auto I = Line->Tokens.begin(), E = Line->Tokens.end();
        while (I != E && I->Tok->is(tok::comment))
          ++I;
        if (Style.isVerilog())
          while (I != E && I->Tok->is(tok::hash))
            ++I;
        return I != E && (++I == E);
      };
      if (OneTokenSoFar()) {
        // Recognize function-like macro usages without trailing semicolon as
        // well as free-standing macros like Q_OBJECT.
        bool FunctionLike = FormatTok->is(tok::l_paren);
        if (FunctionLike)
          parseParens();

        bool FollowedByNewline =
            CommentsBeforeNextToken.empty()
                ? FormatTok->NewlinesBefore > 0
                : CommentsBeforeNextToken.front()->NewlinesBefore > 0;

        if (FollowedByNewline && (Text.size() >= 5 || FunctionLike) &&
            tokenCanStartNewLine(*FormatTok) && Text == Text.upper()) {
          if (PreviousToken->isNot(TT_UntouchableMacroFunc))
            PreviousToken->setFinalizedType(TT_FunctionLikeOrFreestandingMacro);
          addUnwrappedLine();
          return;
        }
      }
      break;
    }
    case tok::equal:
      if ((Style.isJavaScript() || Style.isCSharp()) &&
          FormatTok->is(TT_FatArrow)) {
        tryToParseChildBlock();
        break;
      }

      nextToken();
      if (FormatTok->is(tok::l_brace)) {
        // Block kind should probably be set to BK_BracedInit for any language.
        // C# needs this change to ensure that array initialisers and object
        // initialisers are indented the same way.
        if (Style.isCSharp())
          FormatTok->setBlockKind(BK_BracedInit);
        // TableGen's defset statement has syntax of the form,
        // `defset <type> <name> = { <statement>... }`
        if (Style.isTableGen() &&
            Line->Tokens.begin()->Tok->is(Keywords.kw_defset)) {
          FormatTok->setFinalizedType(TT_FunctionLBrace);
          parseBlock(/*MustBeDeclaration=*/false, /*AddLevels=*/1u,
                     /*MunchSemi=*/false);
          addUnwrappedLine();
          break;
        }
        nextToken();
        parseBracedList();
      } else if (Style.Language == FormatStyle::LK_Proto &&
                 FormatTok->is(tok::less)) {
        nextToken();
        parseBracedList(/*IsAngleBracket=*/true);
      }
      break;
    case tok::l_square:
      parseSquare();
      break;
    case tok::kw_new:
      parseNew();
      break;
    case tok::kw_switch:
      if (Style.Language == FormatStyle::LK_Java)
        parseSwitch(/*IsExpr=*/true);
      else
        nextToken();
      break;
    case tok::kw_case:
      // Proto: there are no switch/case statements.
      if (Style.Language == FormatStyle::LK_Proto) {
        nextToken();
        return;
      }
      // In Verilog switch is called case.
      if (Style.isVerilog()) {
        parseBlock();
        addUnwrappedLine();
        return;
      }
      if (Style.isJavaScript() && Line->MustBeDeclaration) {
        // 'case: string' field declaration.
        nextToken();
        break;
      }
      parseCaseLabel();
      break;
    case tok::kw_default:
      nextToken();
      if (Style.isVerilog()) {
        if (FormatTok->is(tok::colon)) {
          // The label will be handled in the next iteration.
          break;
        }
        if (FormatTok->is(Keywords.kw_clocking)) {
          // A default clocking block.
          parseBlock();
          addUnwrappedLine();
          return;
        }
        parseVerilogCaseLabel();
        return;
      }
      break;
    case tok::colon:
      nextToken();
      if (Style.isVerilog()) {
        parseVerilogCaseLabel();
        return;
      }
      break;
    case tok::greater:
      nextToken();
      if (FormatTok->is(tok::l_brace))
        FormatTok->Previous->setFinalizedType(TT_TemplateCloser);
      break;
    default:
      nextToken();
      break;
    }
  }
}

bool UnwrappedLineParser::tryToParsePropertyAccessor() {
  assert(FormatTok->is(tok::l_brace));
  if (!Style.isCSharp())
    return false;
  // See if it's a property accessor.
  if (FormatTok->Previous->isNot(tok::identifier))
    return false;

  // See if we are inside a property accessor.
  //
  // Record the current tokenPosition so that we can advance and
  // reset the current token. `Next` is not set yet so we need
  // another way to advance along the token stream.
  unsigned int StoredPosition = Tokens->getPosition();
  FormatToken *Tok = Tokens->getNextToken();

  // A trivial property accessor is of the form:
  // { [ACCESS_SPECIFIER] [get]; [ACCESS_SPECIFIER] [set|init] }
  // Track these as they do not require line breaks to be introduced.
  bool HasSpecialAccessor = false;
  bool IsTrivialPropertyAccessor = true;
  while (!eof()) {
    if (Tok->isAccessSpecifierKeyword() ||
        Tok->isOneOf(tok::semi, Keywords.kw_internal, Keywords.kw_get,
                     Keywords.kw_init, Keywords.kw_set)) {
      if (Tok->isOneOf(Keywords.kw_get, Keywords.kw_init, Keywords.kw_set))
        HasSpecialAccessor = true;
      Tok = Tokens->getNextToken();
      continue;
    }
    if (Tok->isNot(tok::r_brace))
      IsTrivialPropertyAccessor = false;
    break;
  }

  if (!HasSpecialAccessor) {
    Tokens->setPosition(StoredPosition);
    return false;
  }

  // Try to parse the property accessor:
  // https://docs.microsoft.com/en-us/dotnet/csharp/programming-guide/classes-and-structs/properties
  Tokens->setPosition(StoredPosition);
  if (!IsTrivialPropertyAccessor && Style.BraceWrapping.AfterFunction)
    addUnwrappedLine();
  nextToken();
  do {
    switch (FormatTok->Tok.getKind()) {
    case tok::r_brace:
      nextToken();
      if (FormatTok->is(tok::equal)) {
        while (!eof() && FormatTok->isNot(tok::semi))
          nextToken();
        nextToken();
      }
      addUnwrappedLine();
      return true;
    case tok::l_brace:
      ++Line->Level;
      parseBlock(/*MustBeDeclaration=*/true);
      addUnwrappedLine();
      --Line->Level;
      break;
    case tok::equal:
      if (FormatTok->is(TT_FatArrow)) {
        ++Line->Level;
        do {
          nextToken();
        } while (!eof() && FormatTok->isNot(tok::semi));
        nextToken();
        addUnwrappedLine();
        --Line->Level;
        break;
      }
      nextToken();
      break;
    default:
      if (FormatTok->isOneOf(Keywords.kw_get, Keywords.kw_init,
                             Keywords.kw_set) &&
          !IsTrivialPropertyAccessor) {
        // Non-trivial get/set needs to be on its own line.
        addUnwrappedLine();
      }
      nextToken();
    }
  } while (!eof());

  // Unreachable for well-formed code (paired '{' and '}').
  return true;
}

bool UnwrappedLineParser::tryToParseLambda() {
  assert(FormatTok->is(tok::l_square));
  if (!IsCpp) {
    nextToken();
    return false;
  }
  FormatToken &LSquare = *FormatTok;
  if (!tryToParseLambdaIntroducer())
    return false;

  bool SeenArrow = false;
  bool InTemplateParameterList = false;

  while (FormatTok->isNot(tok::l_brace)) {
    if (FormatTok->isTypeName(LangOpts) || FormatTok->isAttribute()) {
      nextToken();
      continue;
    }
    switch (FormatTok->Tok.getKind()) {
    case tok::l_brace:
      break;
    case tok::l_paren:
      parseParens(/*AmpAmpTokenType=*/TT_PointerOrReference);
      break;
    case tok::l_square:
      parseSquare();
      break;
    case tok::less:
      assert(FormatTok->Previous);
      if (FormatTok->Previous->is(tok::r_square))
        InTemplateParameterList = true;
      nextToken();
      break;
    case tok::kw_auto:
    case tok::kw_class:
    case tok::kw_struct:
    case tok::kw_union:
    case tok::kw_template:
    case tok::kw_typename:
    case tok::amp:
    case tok::star:
    case tok::kw_const:
    case tok::kw_constexpr:
    case tok::kw_consteval:
    case tok::comma:
    case tok::greater:
    case tok::identifier:
    case tok::numeric_constant:
    case tok::coloncolon:
    case tok::kw_mutable:
    case tok::kw_noexcept:
    case tok::kw_static:
      nextToken();
      break;
    // Specialization of a template with an integer parameter can contain
    // arithmetic, logical, comparison and ternary operators.
    //
    // FIXME: This also accepts sequences of operators that are not in the scope
    // of a template argument list.
    //
    // In a C++ lambda a template type can only occur after an arrow. We use
    // this as an heuristic to distinguish between Objective-C expressions
    // followed by an `a->b` expression, such as:
    // ([obj func:arg] + a->b)
    // Otherwise the code below would parse as a lambda.
    case tok::plus:
    case tok::minus:
    case tok::exclaim:
    case tok::tilde:
    case tok::slash:
    case tok::percent:
    case tok::lessless:
    case tok::pipe:
    case tok::pipepipe:
    case tok::ampamp:
    case tok::caret:
    case tok::equalequal:
    case tok::exclaimequal:
    case tok::greaterequal:
    case tok::lessequal:
    case tok::question:
    case tok::colon:
    case tok::ellipsis:
    case tok::kw_true:
    case tok::kw_false:
      if (SeenArrow || InTemplateParameterList) {
        nextToken();
        break;
      }
      return true;
    case tok::arrow:
      // This might or might not actually be a lambda arrow (this could be an
      // ObjC method invocation followed by a dereferencing arrow). We might
      // reset this back to TT_Unknown in TokenAnnotator.
      FormatTok->setFinalizedType(TT_LambdaArrow);
      SeenArrow = true;
      nextToken();
      break;
    case tok::kw_requires: {
      auto *RequiresToken = FormatTok;
      nextToken();
      parseRequiresClause(RequiresToken);
      break;
    }
    case tok::equal:
      if (!InTemplateParameterList)
        return true;
      nextToken();
      break;
    default:
      return true;
    }
  }

  FormatTok->setFinalizedType(TT_LambdaLBrace);
  LSquare.setFinalizedType(TT_LambdaLSquare);

  NestedLambdas.push_back(Line->SeenDecltypeAuto);
  parseChildBlock();
  assert(!NestedLambdas.empty());
  NestedLambdas.pop_back();

  return true;
}

bool UnwrappedLineParser::tryToParseLambdaIntroducer() {
  const FormatToken *Previous = FormatTok->Previous;
  const FormatToken *LeftSquare = FormatTok;
  nextToken();
  if ((Previous && ((Previous->Tok.getIdentifierInfo() &&
                     !Previous->isOneOf(tok::kw_return, tok::kw_co_await,
                                        tok::kw_co_yield, tok::kw_co_return)) ||
                    Previous->closesScope())) ||
      LeftSquare->isCppStructuredBinding(IsCpp)) {
    return false;
  }
  if (FormatTok->is(tok::l_square) || tok::isLiteral(FormatTok->Tok.getKind()))
    return false;
  if (FormatTok->is(tok::r_square)) {
    const FormatToken *Next = Tokens->peekNextToken(/*SkipComment=*/true);
    if (Next->is(tok::greater))
      return false;
  }
  parseSquare(/*LambdaIntroducer=*/true);
  return true;
}

void UnwrappedLineParser::tryToParseJSFunction() {
  assert(FormatTok->is(Keywords.kw_function));
  if (FormatTok->is(Keywords.kw_async))
    nextToken();
  // Consume "function".
  nextToken();

  // Consume * (generator function). Treat it like C++'s overloaded operators.
  if (FormatTok->is(tok::star)) {
    FormatTok->setFinalizedType(TT_OverloadedOperator);
    nextToken();
  }

  // Consume function name.
  if (FormatTok->is(tok::identifier))
    nextToken();

  if (FormatTok->isNot(tok::l_paren))
    return;

  // Parse formal parameter list.
  parseParens();

  if (FormatTok->is(tok::colon)) {
    // Parse a type definition.
    nextToken();

    // Eat the type declaration. For braced inline object types, balance braces,
    // otherwise just parse until finding an l_brace for the function body.
    if (FormatTok->is(tok::l_brace))
      tryToParseBracedList();
    else
      while (!FormatTok->isOneOf(tok::l_brace, tok::semi) && !eof())
        nextToken();
  }

  if (FormatTok->is(tok::semi))
    return;

  parseChildBlock();
}

bool UnwrappedLineParser::tryToParseBracedList() {
  if (FormatTok->is(BK_Unknown))
    calculateBraceTypes();
  assert(FormatTok->isNot(BK_Unknown));
  if (FormatTok->is(BK_Block))
    return false;
  nextToken();
  parseBracedList();
  return true;
}

bool UnwrappedLineParser::tryToParseChildBlock() {
  assert(Style.isJavaScript() || Style.isCSharp());
  assert(FormatTok->is(TT_FatArrow));
  // Fat arrows (=>) have tok::TokenKind tok::equal but TokenType TT_FatArrow.
  // They always start an expression or a child block if followed by a curly
  // brace.
  nextToken();
  if (FormatTok->isNot(tok::l_brace))
    return false;
  parseChildBlock();
  return true;
}

bool UnwrappedLineParser::parseBracedList(bool IsAngleBracket, bool IsEnum) {
  assert(!IsAngleBracket || !IsEnum);
  bool HasError = false;

  // FIXME: Once we have an expression parser in the UnwrappedLineParser,
  // replace this by using parseAssignmentExpression() inside.
  do {
    if (Style.isCSharp() && FormatTok->is(TT_FatArrow) &&
        tryToParseChildBlock()) {
      continue;
    }
    if (Style.isJavaScript()) {
      if (FormatTok->is(Keywords.kw_function)) {
        tryToParseJSFunction();
        continue;
      }
      if (FormatTok->is(tok::l_brace)) {
        // Could be a method inside of a braced list `{a() { return 1; }}`.
        if (tryToParseBracedList())
          continue;
        parseChildBlock();
      }
    }
    if (FormatTok->is(IsAngleBracket ? tok::greater : tok::r_brace)) {
      if (IsEnum) {
        FormatTok->setBlockKind(BK_Block);
        if (!Style.AllowShortEnumsOnASingleLine)
          addUnwrappedLine();
      }
      nextToken();
      return !HasError;
    }
    switch (FormatTok->Tok.getKind()) {
    case tok::l_square:
      if (Style.isCSharp())
        parseSquare();
      else
        tryToParseLambda();
      break;
    case tok::l_paren:
      parseParens();
      // JavaScript can just have free standing methods and getters/setters in
      // object literals. Detect them by a "{" following ")".
      if (Style.isJavaScript()) {
        if (FormatTok->is(tok::l_brace))
          parseChildBlock();
        break;
      }
      break;
    case tok::l_brace:
      // Assume there are no blocks inside a braced init list apart
      // from the ones we explicitly parse out (like lambdas).
      FormatTok->setBlockKind(BK_BracedInit);
      if (!IsAngleBracket) {
        auto *Prev = FormatTok->Previous;
        if (Prev && Prev->is(tok::greater))
          Prev->setFinalizedType(TT_TemplateCloser);
      }
      nextToken();
      parseBracedList();
      break;
    case tok::less:
      nextToken();
      if (IsAngleBracket)
        parseBracedList(/*IsAngleBracket=*/true);
      break;
    case tok::semi:
      // JavaScript (or more precisely TypeScript) can have semicolons in braced
      // lists (in so-called TypeMemberLists). Thus, the semicolon cannot be
      // used for error recovery if we have otherwise determined that this is
      // a braced list.
      if (Style.isJavaScript()) {
        nextToken();
        break;
      }
      HasError = true;
      if (!IsEnum)
        return false;
      nextToken();
      break;
    case tok::comma:
      nextToken();
      if (IsEnum && !Style.AllowShortEnumsOnASingleLine)
        addUnwrappedLine();
      break;
    default:
      nextToken();
      break;
    }
  } while (!eof());
  return false;
}

/// \brief Parses a pair of parentheses (and everything between them).
/// \param AmpAmpTokenType If different than TT_Unknown sets this type for all
/// double ampersands. This applies for all nested scopes as well.
///
/// Returns whether there is a `=` token between the parentheses.
bool UnwrappedLineParser::parseParens(TokenType AmpAmpTokenType) {
  assert(FormatTok->is(tok::l_paren) && "'(' expected.");
  auto *LeftParen = FormatTok;
  bool SeenEqual = false;
  bool MightBeFoldExpr = false;
  const bool MightBeStmtExpr = Tokens->peekNextToken()->is(tok::l_brace);
  nextToken();
  do {
    switch (FormatTok->Tok.getKind()) {
    case tok::l_paren:
      if (parseParens(AmpAmpTokenType))
        SeenEqual = true;
      if (Style.Language == FormatStyle::LK_Java && FormatTok->is(tok::l_brace))
        parseChildBlock();
      break;
    case tok::r_paren: {
      auto *Prev = LeftParen->Previous;
      if (!MightBeStmtExpr && !MightBeFoldExpr && !Line->InMacroBody &&
          Style.RemoveParentheses > FormatStyle::RPS_Leave) {
        const auto *Next = Tokens->peekNextToken();
        const bool DoubleParens =
            Prev && Prev->is(tok::l_paren) && Next && Next->is(tok::r_paren);
        const auto *PrevPrev = Prev ? Prev->getPreviousNonComment() : nullptr;
        const bool Blacklisted =
            PrevPrev &&
            (PrevPrev->isOneOf(tok::kw___attribute, tok::kw_decltype) ||
             (SeenEqual &&
              (PrevPrev->isOneOf(tok::kw_if, tok::kw_while) ||
               PrevPrev->endsSequence(tok::kw_constexpr, tok::kw_if))));
        const bool ReturnParens =
            Style.RemoveParentheses == FormatStyle::RPS_ReturnStatement &&
            ((NestedLambdas.empty() && !IsDecltypeAutoFunction) ||
             (!NestedLambdas.empty() && !NestedLambdas.back())) &&
            Prev && Prev->isOneOf(tok::kw_return, tok::kw_co_return) && Next &&
            Next->is(tok::semi);
        if ((DoubleParens && !Blacklisted) || ReturnParens) {
          LeftParen->Optional = true;
          FormatTok->Optional = true;
        }
      }
      if (Prev) {
        if (Prev->is(TT_TypenameMacro)) {
          LeftParen->setFinalizedType(TT_TypeDeclarationParen);
          FormatTok->setFinalizedType(TT_TypeDeclarationParen);
        } else if (Prev->is(tok::greater) && FormatTok->Previous == LeftParen) {
          Prev->setFinalizedType(TT_TemplateCloser);
        }
      }
      nextToken();
      return SeenEqual;
    }
    case tok::r_brace:
      // A "}" inside parenthesis is an error if there wasn't a matching "{".
      return SeenEqual;
    case tok::l_square:
      tryToParseLambda();
      break;
    case tok::l_brace:
      if (!tryToParseBracedList())
        parseChildBlock();
      break;
    case tok::at:
      nextToken();
      if (FormatTok->is(tok::l_brace)) {
        nextToken();
        parseBracedList();
      }
      break;
    case tok::ellipsis:
      MightBeFoldExpr = true;
      nextToken();
      break;
    case tok::equal:
      SeenEqual = true;
      if (Style.isCSharp() && FormatTok->is(TT_FatArrow))
        tryToParseChildBlock();
      else
        nextToken();
      break;
    case tok::kw_class:
      if (Style.isJavaScript())
        parseRecord(/*ParseAsExpr=*/true);
      else
        nextToken();
      break;
    case tok::identifier:
      if (Style.isJavaScript() && (FormatTok->is(Keywords.kw_function)))
        tryToParseJSFunction();
      else
        nextToken();
      break;
    case tok::kw_switch:
      if (Style.Language == FormatStyle::LK_Java)
        parseSwitch(/*IsExpr=*/true);
      else
        nextToken();
      break;
    case tok::kw_requires: {
      auto RequiresToken = FormatTok;
      nextToken();
      parseRequiresExpression(RequiresToken);
      break;
    }
    case tok::ampamp:
      if (AmpAmpTokenType != TT_Unknown)
        FormatTok->setFinalizedType(AmpAmpTokenType);
      [[fallthrough]];
    default:
      nextToken();
      break;
    }
  } while (!eof());
  return SeenEqual;
}

void UnwrappedLineParser::parseSquare(bool LambdaIntroducer) {
  if (!LambdaIntroducer) {
    assert(FormatTok->is(tok::l_square) && "'[' expected.");
    if (tryToParseLambda())
      return;
  }
  do {
    switch (FormatTok->Tok.getKind()) {
    case tok::l_paren:
      parseParens();
      break;
    case tok::r_square:
      nextToken();
      return;
    case tok::r_brace:
      // A "}" inside parenthesis is an error if there wasn't a matching "{".
      return;
    case tok::l_square:
      parseSquare();
      break;
    case tok::l_brace: {
      if (!tryToParseBracedList())
        parseChildBlock();
      break;
    }
    case tok::at:
    case tok::colon:
      nextToken();
      if (FormatTok->is(tok::l_brace)) {
        nextToken();
        parseBracedList();
      }
      break;
    default:
      nextToken();
      break;
    }
  } while (!eof());
}

void UnwrappedLineParser::keepAncestorBraces() {
  if (!Style.RemoveBracesLLVM)
    return;

  const int MaxNestingLevels = 2;
  const int Size = NestedTooDeep.size();
  if (Size >= MaxNestingLevels)
    NestedTooDeep[Size - MaxNestingLevels] = true;
  NestedTooDeep.push_back(false);
}

static FormatToken *getLastNonComment(const UnwrappedLine &Line) {
  for (const auto &Token : llvm::reverse(Line.Tokens))
    if (Token.Tok->isNot(tok::comment))
      return Token.Tok;

  return nullptr;
}

void UnwrappedLineParser::parseUnbracedBody(bool CheckEOF) {
  FormatToken *Tok = nullptr;

  if (Style.InsertBraces && !Line->InPPDirective && !Line->Tokens.empty() &&
      PreprocessorDirectives.empty() && FormatTok->isNot(tok::semi)) {
    Tok = Style.BraceWrapping.AfterControlStatement == FormatStyle::BWACS_Never
              ? getLastNonComment(*Line)
              : Line->Tokens.back().Tok;
    assert(Tok);
    if (Tok->BraceCount < 0) {
      assert(Tok->BraceCount == -1);
      Tok = nullptr;
    } else {
      Tok->BraceCount = -1;
    }
  }

  addUnwrappedLine();
  ++Line->Level;
  ++Line->UnbracedBodyLevel;
  parseStructuralElement();
  --Line->UnbracedBodyLevel;

  if (Tok) {
    assert(!Line->InPPDirective);
    Tok = nullptr;
    for (const auto &L : llvm::reverse(*CurrentLines)) {
      if (!L.InPPDirective && getLastNonComment(L)) {
        Tok = L.Tokens.back().Tok;
        break;
      }
    }
    assert(Tok);
    ++Tok->BraceCount;
  }

  if (CheckEOF && eof())
    addUnwrappedLine();

  --Line->Level;
}

static void markOptionalBraces(FormatToken *LeftBrace) {
  if (!LeftBrace)
    return;

  assert(LeftBrace->is(tok::l_brace));

  FormatToken *RightBrace = LeftBrace->MatchingParen;
  if (!RightBrace) {
    assert(!LeftBrace->Optional);
    return;
  }

  assert(RightBrace->is(tok::r_brace));
  assert(RightBrace->MatchingParen == LeftBrace);
  assert(LeftBrace->Optional == RightBrace->Optional);

  LeftBrace->Optional = true;
  RightBrace->Optional = true;
}

void UnwrappedLineParser::handleAttributes() {
  // Handle AttributeMacro, e.g. `if (x) UNLIKELY`.
  if (FormatTok->isAttribute())
    nextToken();
  else if (FormatTok->is(tok::l_square))
    handleCppAttributes();
}

bool UnwrappedLineParser::handleCppAttributes() {
  // Handle [[likely]] / [[unlikely]] attributes.
  assert(FormatTok->is(tok::l_square));
  if (!tryToParseSimpleAttribute())
    return false;
  parseSquare();
  return true;
}

/// Returns whether \c Tok begins a block.
bool UnwrappedLineParser::isBlockBegin(const FormatToken &Tok) const {
  // FIXME: rename the function or make
  // Tok.isOneOf(tok::l_brace, TT_MacroBlockBegin) work.
  return Style.isVerilog() ? Keywords.isVerilogBegin(Tok)
                           : Tok.is(tok::l_brace);
}

FormatToken *UnwrappedLineParser::parseIfThenElse(IfStmtKind *IfKind,
                                                  bool KeepBraces,
                                                  bool IsVerilogAssert) {
  assert((FormatTok->is(tok::kw_if) ||
          (Style.isVerilog() &&
           FormatTok->isOneOf(tok::kw_restrict, Keywords.kw_assert,
                              Keywords.kw_assume, Keywords.kw_cover))) &&
         "'if' expected");
  nextToken();

  if (IsVerilogAssert) {
    // Handle `assert #0` and `assert final`.
    if (FormatTok->is(Keywords.kw_verilogHash)) {
      nextToken();
      if (FormatTok->is(tok::numeric_constant))
        nextToken();
    } else if (FormatTok->isOneOf(Keywords.kw_final, Keywords.kw_property,
                                  Keywords.kw_sequence)) {
      nextToken();
    }
  }

  // TableGen's if statement has the form of `if <cond> then { ... }`.
  if (Style.isTableGen()) {
    while (!eof() && FormatTok->isNot(Keywords.kw_then)) {
      // Simply skip until then. This range only contains a value.
      nextToken();
    }
  }

  // Handle `if !consteval`.
  if (FormatTok->is(tok::exclaim))
    nextToken();

  bool KeepIfBraces = true;
  if (FormatTok->is(tok::kw_consteval)) {
    nextToken();
  } else {
    KeepIfBraces = !Style.RemoveBracesLLVM || KeepBraces;
    if (FormatTok->isOneOf(tok::kw_constexpr, tok::identifier))
      nextToken();
    if (FormatTok->is(tok::l_paren)) {
      FormatTok->setFinalizedType(TT_ConditionLParen);
      parseParens();
    }
  }
  handleAttributes();
  // The then action is optional in Verilog assert statements.
  if (IsVerilogAssert && FormatTok->is(tok::semi)) {
    nextToken();
    addUnwrappedLine();
    return nullptr;
  }

  bool NeedsUnwrappedLine = false;
  keepAncestorBraces();

  FormatToken *IfLeftBrace = nullptr;
  IfStmtKind IfBlockKind = IfStmtKind::NotIf;

  if (isBlockBegin(*FormatTok)) {
    FormatTok->setFinalizedType(TT_ControlStatementLBrace);
    IfLeftBrace = FormatTok;
    CompoundStatementIndenter Indenter(this, Style, Line->Level);
    parseBlock(/*MustBeDeclaration=*/false, /*AddLevels=*/1u,
               /*MunchSemi=*/true, KeepIfBraces, &IfBlockKind);
    setPreviousRBraceType(TT_ControlStatementRBrace);
    if (Style.BraceWrapping.BeforeElse)
      addUnwrappedLine();
    else
      NeedsUnwrappedLine = true;
  } else if (IsVerilogAssert && FormatTok->is(tok::kw_else)) {
    addUnwrappedLine();
  } else {
    parseUnbracedBody();
  }

  if (Style.RemoveBracesLLVM) {
    assert(!NestedTooDeep.empty());
    KeepIfBraces = KeepIfBraces ||
                   (IfLeftBrace && !IfLeftBrace->MatchingParen) ||
                   NestedTooDeep.back() || IfBlockKind == IfStmtKind::IfOnly ||
                   IfBlockKind == IfStmtKind::IfElseIf;
  }

  bool KeepElseBraces = KeepIfBraces;
  FormatToken *ElseLeftBrace = nullptr;
  IfStmtKind Kind = IfStmtKind::IfOnly;

  if (FormatTok->is(tok::kw_else)) {
    if (Style.RemoveBracesLLVM) {
      NestedTooDeep.back() = false;
      Kind = IfStmtKind::IfElse;
    }
    nextToken();
    handleAttributes();
    if (isBlockBegin(*FormatTok)) {
      const bool FollowedByIf = Tokens->peekNextToken()->is(tok::kw_if);
      FormatTok->setFinalizedType(TT_ElseLBrace);
      ElseLeftBrace = FormatTok;
      CompoundStatementIndenter Indenter(this, Style, Line->Level);
      IfStmtKind ElseBlockKind = IfStmtKind::NotIf;
      FormatToken *IfLBrace =
          parseBlock(/*MustBeDeclaration=*/false, /*AddLevels=*/1u,
                     /*MunchSemi=*/true, KeepElseBraces, &ElseBlockKind);
      setPreviousRBraceType(TT_ElseRBrace);
      if (FormatTok->is(tok::kw_else)) {
        KeepElseBraces = KeepElseBraces ||
                         ElseBlockKind == IfStmtKind::IfOnly ||
                         ElseBlockKind == IfStmtKind::IfElseIf;
      } else if (FollowedByIf && IfLBrace && !IfLBrace->Optional) {
        KeepElseBraces = true;
        assert(ElseLeftBrace->MatchingParen);
        markOptionalBraces(ElseLeftBrace);
      }
      addUnwrappedLine();
    } else if (!IsVerilogAssert && FormatTok->is(tok::kw_if)) {
      const FormatToken *Previous = Tokens->getPreviousToken();
      assert(Previous);
      const bool IsPrecededByComment = Previous->is(tok::comment);
      if (IsPrecededByComment) {
        addUnwrappedLine();
        ++Line->Level;
      }
      bool TooDeep = true;
      if (Style.RemoveBracesLLVM) {
        Kind = IfStmtKind::IfElseIf;
        TooDeep = NestedTooDeep.pop_back_val();
      }
      ElseLeftBrace = parseIfThenElse(/*IfKind=*/nullptr, KeepIfBraces);
      if (Style.RemoveBracesLLVM)
        NestedTooDeep.push_back(TooDeep);
      if (IsPrecededByComment)
        --Line->Level;
    } else {
      parseUnbracedBody(/*CheckEOF=*/true);
    }
  } else {
    KeepIfBraces = KeepIfBraces || IfBlockKind == IfStmtKind::IfElse;
    if (NeedsUnwrappedLine)
      addUnwrappedLine();
  }

  if (!Style.RemoveBracesLLVM)
    return nullptr;

  assert(!NestedTooDeep.empty());
  KeepElseBraces = KeepElseBraces ||
                   (ElseLeftBrace && !ElseLeftBrace->MatchingParen) ||
                   NestedTooDeep.back();

  NestedTooDeep.pop_back();

  if (!KeepIfBraces && !KeepElseBraces) {
    markOptionalBraces(IfLeftBrace);
    markOptionalBraces(ElseLeftBrace);
  } else if (IfLeftBrace) {
    FormatToken *IfRightBrace = IfLeftBrace->MatchingParen;
    if (IfRightBrace) {
      assert(IfRightBrace->MatchingParen == IfLeftBrace);
      assert(!IfLeftBrace->Optional);
      assert(!IfRightBrace->Optional);
      IfLeftBrace->MatchingParen = nullptr;
      IfRightBrace->MatchingParen = nullptr;
    }
  }

  if (IfKind)
    *IfKind = Kind;

  return IfLeftBrace;
}

void UnwrappedLineParser::parseTryCatch() {
  assert(FormatTok->isOneOf(tok::kw_try, tok::kw___try) && "'try' expected");
  nextToken();
  bool NeedsUnwrappedLine = false;
  bool HasCtorInitializer = false;
  if (FormatTok->is(tok::colon)) {
    auto *Colon = FormatTok;
    // We are in a function try block, what comes is an initializer list.
    nextToken();
    if (FormatTok->is(tok::identifier)) {
      HasCtorInitializer = true;
      Colon->setFinalizedType(TT_CtorInitializerColon);
    }

    // In case identifiers were removed by clang-tidy, what might follow is
    // multiple commas in sequence - before the first identifier.
    while (FormatTok->is(tok::comma))
      nextToken();

    while (FormatTok->is(tok::identifier)) {
      nextToken();
      if (FormatTok->is(tok::l_paren)) {
        parseParens();
      } else if (FormatTok->is(tok::l_brace)) {
        nextToken();
        parseBracedList();
      }

      // In case identifiers were removed by clang-tidy, what might follow is
      // multiple commas in sequence - after the first identifier.
      while (FormatTok->is(tok::comma))
        nextToken();
    }
  }
  // Parse try with resource.
  if (Style.Language == FormatStyle::LK_Java && FormatTok->is(tok::l_paren))
    parseParens();

  keepAncestorBraces();

  if (FormatTok->is(tok::l_brace)) {
    if (HasCtorInitializer)
      FormatTok->setFinalizedType(TT_FunctionLBrace);
    CompoundStatementIndenter Indenter(this, Style, Line->Level);
    parseBlock();
    if (Style.BraceWrapping.BeforeCatch)
      addUnwrappedLine();
    else
      NeedsUnwrappedLine = true;
  } else if (FormatTok->isNot(tok::kw_catch)) {
    // The C++ standard requires a compound-statement after a try.
    // If there's none, we try to assume there's a structuralElement
    // and try to continue.
    addUnwrappedLine();
    ++Line->Level;
    parseStructuralElement();
    --Line->Level;
  }
  while (true) {
    if (FormatTok->is(tok::at))
      nextToken();
    if (!(FormatTok->isOneOf(tok::kw_catch, Keywords.kw___except,
                             tok::kw___finally) ||
          ((Style.Language == FormatStyle::LK_Java || Style.isJavaScript()) &&
           FormatTok->is(Keywords.kw_finally)) ||
          (FormatTok->isObjCAtKeyword(tok::objc_catch) ||
           FormatTok->isObjCAtKeyword(tok::objc_finally)))) {
      break;
    }
    nextToken();
    while (FormatTok->isNot(tok::l_brace)) {
      if (FormatTok->is(tok::l_paren)) {
        parseParens();
        continue;
      }
      if (FormatTok->isOneOf(tok::semi, tok::r_brace, tok::eof)) {
        if (Style.RemoveBracesLLVM)
          NestedTooDeep.pop_back();
        return;
      }
      nextToken();
    }
    NeedsUnwrappedLine = false;
    Line->MustBeDeclaration = false;
    CompoundStatementIndenter Indenter(this, Style, Line->Level);
    parseBlock();
    if (Style.BraceWrapping.BeforeCatch)
      addUnwrappedLine();
    else
      NeedsUnwrappedLine = true;
  }

  if (Style.RemoveBracesLLVM)
    NestedTooDeep.pop_back();

  if (NeedsUnwrappedLine)
    addUnwrappedLine();
}

void UnwrappedLineParser::parseNamespace() {
  assert(FormatTok->isOneOf(tok::kw_namespace, TT_NamespaceMacro) &&
         "'namespace' expected");

  const FormatToken &InitialToken = *FormatTok;
  nextToken();
  if (InitialToken.is(TT_NamespaceMacro)) {
    parseParens();
  } else {
    while (FormatTok->isOneOf(tok::identifier, tok::coloncolon, tok::kw_inline,
                              tok::l_square, tok::period, tok::l_paren) ||
           (Style.isCSharp() && FormatTok->is(tok::kw_union))) {
      if (FormatTok->is(tok::l_square))
        parseSquare();
      else if (FormatTok->is(tok::l_paren))
        parseParens();
      else
        nextToken();
    }
  }
  if (FormatTok->is(tok::l_brace)) {
    FormatTok->setFinalizedType(TT_NamespaceLBrace);

    if (ShouldBreakBeforeBrace(Style, InitialToken))
      addUnwrappedLine();

    unsigned AddLevels =
        Style.NamespaceIndentation == FormatStyle::NI_All ||
                (Style.NamespaceIndentation == FormatStyle::NI_Inner &&
                 DeclarationScopeStack.size() > 1)
            ? 1u
            : 0u;
    bool ManageWhitesmithsBraces =
        AddLevels == 0u &&
        Style.BreakBeforeBraces == FormatStyle::BS_Whitesmiths;

    // If we're in Whitesmiths mode, indent the brace if we're not indenting
    // the whole block.
    if (ManageWhitesmithsBraces)
      ++Line->Level;

    // Munch the semicolon after a namespace. This is more common than one would
    // think. Putting the semicolon into its own line is very ugly.
    parseBlock(/*MustBeDeclaration=*/true, AddLevels, /*MunchSemi=*/true,
               /*KeepBraces=*/true, /*IfKind=*/nullptr,
               ManageWhitesmithsBraces);

    addUnwrappedLine(AddLevels > 0 ? LineLevel::Remove : LineLevel::Keep);

    if (ManageWhitesmithsBraces)
      --Line->Level;
  }
  // FIXME: Add error handling.
}

void UnwrappedLineParser::parseNew() {
  assert(FormatTok->is(tok::kw_new) && "'new' expected");
  nextToken();

  if (Style.isCSharp()) {
    do {
      // Handle constructor invocation, e.g. `new(field: value)`.
      if (FormatTok->is(tok::l_paren))
        parseParens();

      // Handle array initialization syntax, e.g. `new[] {10, 20, 30}`.
      if (FormatTok->is(tok::l_brace))
        parseBracedList();

      if (FormatTok->isOneOf(tok::semi, tok::comma))
        return;

      nextToken();
    } while (!eof());
  }

  if (Style.Language != FormatStyle::LK_Java)
    return;

  // In Java, we can parse everything up to the parens, which aren't optional.
  do {
    // There should not be a ;, { or } before the new's open paren.
    if (FormatTok->isOneOf(tok::semi, tok::l_brace, tok::r_brace))
      return;

    // Consume the parens.
    if (FormatTok->is(tok::l_paren)) {
      parseParens();

      // If there is a class body of an anonymous class, consume that as child.
      if (FormatTok->is(tok::l_brace))
        parseChildBlock();
      return;
    }
    nextToken();
  } while (!eof());
}

void UnwrappedLineParser::parseLoopBody(bool KeepBraces, bool WrapRightBrace) {
  keepAncestorBraces();

  if (isBlockBegin(*FormatTok)) {
    FormatTok->setFinalizedType(TT_ControlStatementLBrace);
    FormatToken *LeftBrace = FormatTok;
    CompoundStatementIndenter Indenter(this, Style, Line->Level);
    parseBlock(/*MustBeDeclaration=*/false, /*AddLevels=*/1u,
               /*MunchSemi=*/true, KeepBraces);
    setPreviousRBraceType(TT_ControlStatementRBrace);
    if (!KeepBraces) {
      assert(!NestedTooDeep.empty());
      if (!NestedTooDeep.back())
        markOptionalBraces(LeftBrace);
    }
    if (WrapRightBrace)
      addUnwrappedLine();
  } else {
    parseUnbracedBody();
  }

  if (!KeepBraces)
    NestedTooDeep.pop_back();
}

void UnwrappedLineParser::parseForOrWhileLoop(bool HasParens) {
  assert((FormatTok->isOneOf(tok::kw_for, tok::kw_while, TT_ForEachMacro) ||
          (Style.isVerilog() &&
           FormatTok->isOneOf(Keywords.kw_always, Keywords.kw_always_comb,
                              Keywords.kw_always_ff, Keywords.kw_always_latch,
                              Keywords.kw_final, Keywords.kw_initial,
                              Keywords.kw_foreach, Keywords.kw_forever,
                              Keywords.kw_repeat))) &&
         "'for', 'while' or foreach macro expected");
  const bool KeepBraces = !Style.RemoveBracesLLVM ||
                          !FormatTok->isOneOf(tok::kw_for, tok::kw_while);

  nextToken();
  // JS' for await ( ...
  if (Style.isJavaScript() && FormatTok->is(Keywords.kw_await))
    nextToken();
  if (IsCpp && FormatTok->is(tok::kw_co_await))
    nextToken();
  if (HasParens && FormatTok->is(tok::l_paren)) {
    // The type is only set for Verilog basically because we were afraid to
    // change the existing behavior for loops. See the discussion on D121756 for
    // details.
    if (Style.isVerilog())
      FormatTok->setFinalizedType(TT_ConditionLParen);
    parseParens();
  }

  if (Style.isVerilog()) {
    // Event control.
    parseVerilogSensitivityList();
  } else if (Style.AllowShortLoopsOnASingleLine && FormatTok->is(tok::semi) &&
             Tokens->getPreviousToken()->is(tok::r_paren)) {
    nextToken();
    addUnwrappedLine();
    return;
  }

  handleAttributes();
  parseLoopBody(KeepBraces, /*WrapRightBrace=*/true);
}

void UnwrappedLineParser::parseDoWhile() {
  assert(FormatTok->is(tok::kw_do) && "'do' expected");
  nextToken();

  parseLoopBody(/*KeepBraces=*/true, Style.BraceWrapping.BeforeWhile);

  // FIXME: Add error handling.
  if (FormatTok->isNot(tok::kw_while)) {
    addUnwrappedLine();
    return;
  }

  FormatTok->setFinalizedType(TT_DoWhile);

  // If in Whitesmiths mode, the line with the while() needs to be indented
  // to the same level as the block.
  if (Style.BreakBeforeBraces == FormatStyle::BS_Whitesmiths)
    ++Line->Level;

  nextToken();
  parseStructuralElement();
}

void UnwrappedLineParser::parseLabel(bool LeftAlignLabel) {
  nextToken();
  unsigned OldLineLevel = Line->Level;

  if (LeftAlignLabel)
    Line->Level = 0;
  else if (Line->Level > 1 || (!Line->InPPDirective && Line->Level > 0))
    --Line->Level;

  if (!Style.IndentCaseBlocks && CommentsBeforeNextToken.empty() &&
      FormatTok->is(tok::l_brace)) {

    CompoundStatementIndenter Indenter(this, Line->Level,
                                       Style.BraceWrapping.AfterCaseLabel,
                                       Style.BraceWrapping.IndentBraces);
    parseBlock();
    if (FormatTok->is(tok::kw_break)) {
      if (Style.BraceWrapping.AfterControlStatement ==
          FormatStyle::BWACS_Always) {
        addUnwrappedLine();
        if (!Style.IndentCaseBlocks &&
            Style.BreakBeforeBraces == FormatStyle::BS_Whitesmiths) {
          ++Line->Level;
        }
      }
      parseStructuralElement();
    }
    addUnwrappedLine();
  } else {
    if (FormatTok->is(tok::semi))
      nextToken();
    addUnwrappedLine();
  }
  Line->Level = OldLineLevel;
  if (FormatTok->isNot(tok::l_brace)) {
    parseStructuralElement();
    addUnwrappedLine();
  }
}

void UnwrappedLineParser::parseCaseLabel() {
  assert(FormatTok->is(tok::kw_case) && "'case' expected");
  auto *Case = FormatTok;

  // FIXME: fix handling of complex expressions here.
  do {
    nextToken();
    if (FormatTok->is(tok::colon)) {
      FormatTok->setFinalizedType(TT_CaseLabelColon);
      break;
    }
    if (Style.Language == FormatStyle::LK_Java && FormatTok->is(tok::arrow)) {
      FormatTok->setFinalizedType(TT_CaseLabelArrow);
      Case->setFinalizedType(TT_SwitchExpressionLabel);
      break;
    }
  } while (!eof());
  parseLabel();
}

void UnwrappedLineParser::parseSwitch(bool IsExpr) {
  assert(FormatTok->is(tok::kw_switch) && "'switch' expected");
  nextToken();
  if (FormatTok->is(tok::l_paren))
    parseParens();

  keepAncestorBraces();

  if (FormatTok->is(tok::l_brace)) {
    CompoundStatementIndenter Indenter(this, Style, Line->Level);
    FormatTok->setFinalizedType(IsExpr ? TT_SwitchExpressionLBrace
                                       : TT_ControlStatementLBrace);
    if (IsExpr)
      parseChildBlock();
    else
      parseBlock();
    setPreviousRBraceType(TT_ControlStatementRBrace);
    if (!IsExpr)
      addUnwrappedLine();
  } else {
    addUnwrappedLine();
    ++Line->Level;
    parseStructuralElement();
    --Line->Level;
  }

  if (Style.RemoveBracesLLVM)
    NestedTooDeep.pop_back();
}

// Operators that can follow a C variable.
static bool isCOperatorFollowingVar(tok::TokenKind Kind) {
  switch (Kind) {
  case tok::ampamp:
  case tok::ampequal:
  case tok::arrow:
  case tok::caret:
  case tok::caretequal:
  case tok::comma:
  case tok::ellipsis:
  case tok::equal:
  case tok::equalequal:
  case tok::exclaim:
  case tok::exclaimequal:
  case tok::greater:
  case tok::greaterequal:
  case tok::greatergreater:
  case tok::greatergreaterequal:
  case tok::l_paren:
  case tok::l_square:
  case tok::less:
  case tok::lessequal:
  case tok::lessless:
  case tok::lesslessequal:
  case tok::minus:
  case tok::minusequal:
  case tok::minusminus:
  case tok::percent:
  case tok::percentequal:
  case tok::period:
  case tok::pipe:
  case tok::pipeequal:
  case tok::pipepipe:
  case tok::plus:
  case tok::plusequal:
  case tok::plusplus:
  case tok::question:
  case tok::r_brace:
  case tok::r_paren:
  case tok::r_square:
  case tok::semi:
  case tok::slash:
  case tok::slashequal:
  case tok::star:
  case tok::starequal:
    return true;
  default:
    return false;
  }
}

void UnwrappedLineParser::parseAccessSpecifier() {
  FormatToken *AccessSpecifierCandidate = FormatTok;
  nextToken();
  // Understand Qt's slots.
  if (FormatTok->isOneOf(Keywords.kw_slots, Keywords.kw_qslots))
    nextToken();
  // Otherwise, we don't know what it is, and we'd better keep the next token.
  if (FormatTok->is(tok::colon)) {
    nextToken();
    addUnwrappedLine();
  } else if (FormatTok->isNot(tok::coloncolon) &&
             !isCOperatorFollowingVar(FormatTok->Tok.getKind())) {
    // Not a variable name nor namespace name.
    addUnwrappedLine();
  } else if (AccessSpecifierCandidate) {
    // Consider the access specifier to be a C identifier.
    AccessSpecifierCandidate->Tok.setKind(tok::identifier);
  }
}

/// \brief Parses a requires, decides if it is a clause or an expression.
/// \pre The current token has to be the requires keyword.
/// \returns true if it parsed a clause.
bool UnwrappedLineParser::parseRequires() {
  assert(FormatTok->is(tok::kw_requires) && "'requires' expected");
  auto RequiresToken = FormatTok;

  // We try to guess if it is a requires clause, or a requires expression. For
  // that we first consume the keyword and check the next token.
  nextToken();

  switch (FormatTok->Tok.getKind()) {
  case tok::l_brace:
    // This can only be an expression, never a clause.
    parseRequiresExpression(RequiresToken);
    return false;
  case tok::l_paren:
    // Clauses and expression can start with a paren, it's unclear what we have.
    break;
  default:
    // All other tokens can only be a clause.
    parseRequiresClause(RequiresToken);
    return true;
  }

  // Looking forward we would have to decide if there are function declaration
  // like arguments to the requires expression:
  // requires (T t) {
  // Or there is a constraint expression for the requires clause:
  // requires (C<T> && ...

  // But first let's look behind.
  auto *PreviousNonComment = RequiresToken->getPreviousNonComment();

  if (!PreviousNonComment ||
      PreviousNonComment->is(TT_RequiresExpressionLBrace)) {
    // If there is no token, or an expression left brace, we are a requires
    // clause within a requires expression.
    parseRequiresClause(RequiresToken);
    return true;
  }

  switch (PreviousNonComment->Tok.getKind()) {
  case tok::greater:
  case tok::r_paren:
  case tok::kw_noexcept:
  case tok::kw_const:
    // This is a requires clause.
    parseRequiresClause(RequiresToken);
    return true;
  case tok::amp:
  case tok::ampamp: {
    // This can be either:
    // if (... && requires (T t) ...)
    // Or
    // void member(...) && requires (C<T> ...
    // We check the one token before that for a const:
    // void member(...) const && requires (C<T> ...
    auto PrevPrev = PreviousNonComment->getPreviousNonComment();
    if (PrevPrev && PrevPrev->is(tok::kw_const)) {
      parseRequiresClause(RequiresToken);
      return true;
    }
    break;
  }
  default:
    if (PreviousNonComment->isTypeOrIdentifier(LangOpts)) {
      // This is a requires clause.
      parseRequiresClause(RequiresToken);
      return true;
    }
    // It's an expression.
    parseRequiresExpression(RequiresToken);
    return false;
  }

  // Now we look forward and try to check if the paren content is a parameter
  // list. The parameters can be cv-qualified and contain references or
  // pointers.
  // So we want basically to check for TYPE NAME, but TYPE can contain all kinds
  // of stuff: typename, const, *, &, &&, ::, identifiers.

  unsigned StoredPosition = Tokens->getPosition();
  FormatToken *NextToken = Tokens->getNextToken();
  int Lookahead = 0;
  auto PeekNext = [&Lookahead, &NextToken, this] {
    ++Lookahead;
    NextToken = Tokens->getNextToken();
  };

  bool FoundType = false;
  bool LastWasColonColon = false;
  int OpenAngles = 0;

  for (; Lookahead < 50; PeekNext()) {
    switch (NextToken->Tok.getKind()) {
    case tok::kw_volatile:
    case tok::kw_const:
    case tok::comma:
      if (OpenAngles == 0) {
        FormatTok = Tokens->setPosition(StoredPosition);
        parseRequiresExpression(RequiresToken);
        return false;
      }
      break;
    case tok::eof:
      // Break out of the loop.
      Lookahead = 50;
      break;
    case tok::coloncolon:
      LastWasColonColon = true;
      break;
    case tok::kw_decltype:
    case tok::identifier:
      if (FoundType && !LastWasColonColon && OpenAngles == 0) {
        FormatTok = Tokens->setPosition(StoredPosition);
        parseRequiresExpression(RequiresToken);
        return false;
      }
      FoundType = true;
      LastWasColonColon = false;
      break;
    case tok::less:
      ++OpenAngles;
      break;
    case tok::greater:
      --OpenAngles;
      break;
    default:
      if (NextToken->isTypeName(LangOpts)) {
        FormatTok = Tokens->setPosition(StoredPosition);
        parseRequiresExpression(RequiresToken);
        return false;
      }
      break;
    }
  }
  // This seems to be a complicated expression, just assume it's a clause.
  FormatTok = Tokens->setPosition(StoredPosition);
  parseRequiresClause(RequiresToken);
  return true;
}

/// \brief Parses a requires clause.
/// \param RequiresToken The requires keyword token, which starts this clause.
/// \pre We need to be on the next token after the requires keyword.
/// \sa parseRequiresExpression
///
/// Returns if it either has finished parsing the clause, or it detects, that
/// the clause is incorrect.
void UnwrappedLineParser::parseRequiresClause(FormatToken *RequiresToken) {
  assert(FormatTok->getPreviousNonComment() == RequiresToken);
  assert(RequiresToken->is(tok::kw_requires) && "'requires' expected");

  // If there is no previous token, we are within a requires expression,
  // otherwise we will always have the template or function declaration in front
  // of it.
  bool InRequiresExpression =
      !RequiresToken->Previous ||
      RequiresToken->Previous->is(TT_RequiresExpressionLBrace);

  RequiresToken->setFinalizedType(InRequiresExpression
                                      ? TT_RequiresClauseInARequiresExpression
                                      : TT_RequiresClause);

  // NOTE: parseConstraintExpression is only ever called from this function.
  // It could be inlined into here.
  parseConstraintExpression();

  if (!InRequiresExpression)
    FormatTok->Previous->ClosesRequiresClause = true;
}

/// \brief Parses a requires expression.
/// \param RequiresToken The requires keyword token, which starts this clause.
/// \pre We need to be on the next token after the requires keyword.
/// \sa parseRequiresClause
///
/// Returns if it either has finished parsing the expression, or it detects,
/// that the expression is incorrect.
void UnwrappedLineParser::parseRequiresExpression(FormatToken *RequiresToken) {
  assert(FormatTok->getPreviousNonComment() == RequiresToken);
  assert(RequiresToken->is(tok::kw_requires) && "'requires' expected");

  RequiresToken->setFinalizedType(TT_RequiresExpression);

  if (FormatTok->is(tok::l_paren)) {
    FormatTok->setFinalizedType(TT_RequiresExpressionLParen);
    parseParens();
  }

  if (FormatTok->is(tok::l_brace)) {
    FormatTok->setFinalizedType(TT_RequiresExpressionLBrace);
    parseChildBlock();
  }
}

/// \brief Parses a constraint expression.
///
/// This is the body of a requires clause. It returns, when the parsing is
/// complete, or the expression is incorrect.
void UnwrappedLineParser::parseConstraintExpression() {
  // The special handling for lambdas is needed since tryToParseLambda() eats a
  // token and if a requires expression is the last part of a requires clause
  // and followed by an attribute like [[nodiscard]] the ClosesRequiresClause is
  // not set on the correct token. Thus we need to be aware if we even expect a
  // lambda to be possible.
  // template <typename T> requires requires { ... } [[nodiscard]] ...;
  bool LambdaNextTimeAllowed = true;

  // Within lambda declarations, it is permitted to put a requires clause after
  // its template parameter list, which would place the requires clause right
  // before the parentheses of the parameters of the lambda declaration. Thus,
  // we track if we expect to see grouping parentheses at all.
  // Without this check, `requires foo<T> (T t)` in the below example would be
  // seen as the whole requires clause, accidentally eating the parameters of
  // the lambda.
  // [&]<typename T> requires foo<T> (T t) { ... };
  bool TopLevelParensAllowed = true;

  do {
    bool LambdaThisTimeAllowed = std::exchange(LambdaNextTimeAllowed, false);

    switch (FormatTok->Tok.getKind()) {
    case tok::kw_requires: {
      auto RequiresToken = FormatTok;
      nextToken();
      parseRequiresExpression(RequiresToken);
      break;
    }

    case tok::l_paren:
      if (!TopLevelParensAllowed)
        return;
      parseParens(/*AmpAmpTokenType=*/TT_BinaryOperator);
      TopLevelParensAllowed = false;
      break;

    case tok::l_square:
      if (!LambdaThisTimeAllowed || !tryToParseLambda())
        return;
      break;

    case tok::kw_const:
    case tok::semi:
    case tok::kw_class:
    case tok::kw_struct:
    case tok::kw_union:
      return;

    case tok::l_brace:
      // Potential function body.
      return;

    case tok::ampamp:
    case tok::pipepipe:
      FormatTok->setFinalizedType(TT_BinaryOperator);
      nextToken();
      LambdaNextTimeAllowed = true;
      TopLevelParensAllowed = true;
      break;

    case tok::comma:
    case tok::comment:
      LambdaNextTimeAllowed = LambdaThisTimeAllowed;
      nextToken();
      break;

    case tok::kw_sizeof:
    case tok::greater:
    case tok::greaterequal:
    case tok::greatergreater:
    case tok::less:
    case tok::lessequal:
    case tok::lessless:
    case tok::equalequal:
    case tok::exclaim:
    case tok::exclaimequal:
    case tok::plus:
    case tok::minus:
    case tok::star:
    case tok::slash:
      LambdaNextTimeAllowed = true;
      TopLevelParensAllowed = true;
      // Just eat them.
      nextToken();
      break;

    case tok::numeric_constant:
    case tok::coloncolon:
    case tok::kw_true:
    case tok::kw_false:
      TopLevelParensAllowed = false;
      // Just eat them.
      nextToken();
      break;

    case tok::kw_static_cast:
    case tok::kw_const_cast:
    case tok::kw_reinterpret_cast:
    case tok::kw_dynamic_cast:
      nextToken();
      if (FormatTok->isNot(tok::less))
        return;

      nextToken();
      parseBracedList(/*IsAngleBracket=*/true);
      break;

    default:
      if (!FormatTok->Tok.getIdentifierInfo()) {
        // Identifiers are part of the default case, we check for more then
        // tok::identifier to handle builtin type traits.
        return;
      }

      // We need to differentiate identifiers for a template deduction guide,
      // variables, or function return types (the constraint expression has
      // ended before that), and basically all other cases. But it's easier to
      // check the other way around.
      assert(FormatTok->Previous);
      switch (FormatTok->Previous->Tok.getKind()) {
      case tok::coloncolon:  // Nested identifier.
      case tok::ampamp:      // Start of a function or variable for the
      case tok::pipepipe:    // constraint expression. (binary)
      case tok::exclaim:     // The same as above, but unary.
      case tok::kw_requires: // Initial identifier of a requires clause.
      case tok::equal:       // Initial identifier of a concept declaration.
        break;
      default:
        return;
      }

      // Read identifier with optional template declaration.
      nextToken();
      if (FormatTok->is(tok::less)) {
        nextToken();
        parseBracedList(/*IsAngleBracket=*/true);
      }
      TopLevelParensAllowed = false;
      break;
    }
  } while (!eof());
}

bool UnwrappedLineParser::parseEnum() {
  const FormatToken &InitialToken = *FormatTok;

  // Won't be 'enum' for NS_ENUMs.
  if (FormatTok->is(tok::kw_enum))
    nextToken();

  // In TypeScript, "enum" can also be used as property name, e.g. in interface
  // declarations. An "enum" keyword followed by a colon would be a syntax
  // error and thus assume it is just an identifier.
  if (Style.isJavaScript() && FormatTok->isOneOf(tok::colon, tok::question))
    return false;

  // In protobuf, "enum" can be used as a field name.
  if (Style.Language == FormatStyle::LK_Proto && FormatTok->is(tok::equal))
    return false;

  if (IsCpp) {
    // Eat up enum class ...
    if (FormatTok->isOneOf(tok::kw_class, tok::kw_struct))
      nextToken();
    while (FormatTok->is(tok::l_square))
      if (!handleCppAttributes())
        return false;
  }

  while (FormatTok->Tok.getIdentifierInfo() ||
         FormatTok->isOneOf(tok::colon, tok::coloncolon, tok::less,
                            tok::greater, tok::comma, tok::question,
                            tok::l_square)) {
    if (Style.isVerilog()) {
      FormatTok->setFinalizedType(TT_VerilogDimensionedTypeName);
      nextToken();
      // In Verilog the base type can have dimensions.
      while (FormatTok->is(tok::l_square))
        parseSquare();
    } else {
      nextToken();
    }
    // We can have macros or attributes in between 'enum' and the enum name.
    if (FormatTok->is(tok::l_paren))
      parseParens();
    if (FormatTok->is(tok::identifier)) {
      nextToken();
      // If there are two identifiers in a row, this is likely an elaborate
      // return type. In Java, this can be "implements", etc.
      if (IsCpp && FormatTok->is(tok::identifier))
        return false;
    }
  }

  // Just a declaration or something is wrong.
  if (FormatTok->isNot(tok::l_brace))
    return true;
  FormatTok->setFinalizedType(TT_EnumLBrace);
  FormatTok->setBlockKind(BK_Block);

  if (Style.Language == FormatStyle::LK_Java) {
    // Java enums are different.
    parseJavaEnumBody();
    return true;
  }
  if (Style.Language == FormatStyle::LK_Proto) {
    parseBlock(/*MustBeDeclaration=*/true);
    return true;
  }

  if (!Style.AllowShortEnumsOnASingleLine &&
      ShouldBreakBeforeBrace(Style, InitialToken)) {
    addUnwrappedLine();
  }
  // Parse enum body.
  nextToken();
  if (!Style.AllowShortEnumsOnASingleLine) {
    addUnwrappedLine();
    Line->Level += 1;
  }
  bool HasError = !parseBracedList(/*IsAngleBracket=*/false, /*IsEnum=*/true);
  if (!Style.AllowShortEnumsOnASingleLine)
    Line->Level -= 1;
  if (HasError) {
    if (FormatTok->is(tok::semi))
      nextToken();
    addUnwrappedLine();
  }
  setPreviousRBraceType(TT_EnumRBrace);
  return true;

  // There is no addUnwrappedLine() here so that we fall through to parsing a
  // structural element afterwards. Thus, in "enum A {} n, m;",
  // "} n, m;" will end up in one unwrapped line.
}

bool UnwrappedLineParser::parseStructLike() {
  // parseRecord falls through and does not yet add an unwrapped line as a
  // record declaration or definition can start a structural element.
  parseRecord();
  // This does not apply to Java, JavaScript and C#.
  if (Style.Language == FormatStyle::LK_Java || Style.isJavaScript() ||
      Style.isCSharp()) {
    if (FormatTok->is(tok::semi))
      nextToken();
    addUnwrappedLine();
    return true;
  }
  return false;
}

namespace {
// A class used to set and restore the Token position when peeking
// ahead in the token source.
class ScopedTokenPosition {
  unsigned StoredPosition;
  FormatTokenSource *Tokens;

public:
  ScopedTokenPosition(FormatTokenSource *Tokens) : Tokens(Tokens) {
    assert(Tokens && "Tokens expected to not be null");
    StoredPosition = Tokens->getPosition();
  }

  ~ScopedTokenPosition() { Tokens->setPosition(StoredPosition); }
};
} // namespace

// Look to see if we have [[ by looking ahead, if
// its not then rewind to the original position.
bool UnwrappedLineParser::tryToParseSimpleAttribute() {
  ScopedTokenPosition AutoPosition(Tokens);
  FormatToken *Tok = Tokens->getNextToken();
  // We already read the first [ check for the second.
  if (Tok->isNot(tok::l_square))
    return false;
  // Double check that the attribute is just something
  // fairly simple.
  while (Tok->isNot(tok::eof)) {
    if (Tok->is(tok::r_square))
      break;
    Tok = Tokens->getNextToken();
  }
  if (Tok->is(tok::eof))
    return false;
  Tok = Tokens->getNextToken();
  if (Tok->isNot(tok::r_square))
    return false;
  Tok = Tokens->getNextToken();
  if (Tok->is(tok::semi))
    return false;
  return true;
}

void UnwrappedLineParser::parseJavaEnumBody() {
  assert(FormatTok->is(tok::l_brace));
  const FormatToken *OpeningBrace = FormatTok;

  // Determine whether the enum is simple, i.e. does not have a semicolon or
  // constants with class bodies. Simple enums can be formatted like braced
  // lists, contracted to a single line, etc.
  unsigned StoredPosition = Tokens->getPosition();
  bool IsSimple = true;
  FormatToken *Tok = Tokens->getNextToken();
  while (Tok->isNot(tok::eof)) {
    if (Tok->is(tok::r_brace))
      break;
    if (Tok->isOneOf(tok::l_brace, tok::semi)) {
      IsSimple = false;
      break;
    }
    // FIXME: This will also mark enums with braces in the arguments to enum
    // constants as "not simple". This is probably fine in practice, though.
    Tok = Tokens->getNextToken();
  }
  FormatTok = Tokens->setPosition(StoredPosition);

  if (IsSimple) {
    nextToken();
    parseBracedList();
    addUnwrappedLine();
    return;
  }

  // Parse the body of a more complex enum.
  // First add a line for everything up to the "{".
  nextToken();
  addUnwrappedLine();
  ++Line->Level;

  // Parse the enum constants.
  while (!eof()) {
    if (FormatTok->is(tok::l_brace)) {
      // Parse the constant's class body.
      parseBlock(/*MustBeDeclaration=*/true, /*AddLevels=*/1u,
                 /*MunchSemi=*/false);
    } else if (FormatTok->is(tok::l_paren)) {
      parseParens();
    } else if (FormatTok->is(tok::comma)) {
      nextToken();
      addUnwrappedLine();
    } else if (FormatTok->is(tok::semi)) {
      nextToken();
      addUnwrappedLine();
      break;
    } else if (FormatTok->is(tok::r_brace)) {
      addUnwrappedLine();
      break;
    } else {
      nextToken();
    }
  }

  // Parse the class body after the enum's ";" if any.
  parseLevel(OpeningBrace);
  nextToken();
  --Line->Level;
  addUnwrappedLine();
}

void UnwrappedLineParser::parseRecord(bool ParseAsExpr) {
  const FormatToken &InitialToken = *FormatTok;
  nextToken();

  const FormatToken *ClassName = nullptr;
  bool IsDerived = false;
  auto IsNonMacroIdentifier = [](const FormatToken *Tok) {
    return Tok->is(tok::identifier) && Tok->TokenText != Tok->TokenText.upper();
  };
  // JavaScript/TypeScript supports anonymous classes like:
  // a = class extends foo { }
  bool JSPastExtendsOrImplements = false;
  // The actual identifier can be a nested name specifier, and in macros
  // it is often token-pasted.
  // An [[attribute]] can be before the identifier.
  while (FormatTok->isOneOf(tok::identifier, tok::coloncolon, tok::hashhash,
                            tok::kw_alignas, tok::l_square) ||
         FormatTok->isAttribute() ||
         ((Style.Language == FormatStyle::LK_Java || Style.isJavaScript()) &&
          FormatTok->isOneOf(tok::period, tok::comma))) {
    if (Style.isJavaScript() &&
        FormatTok->isOneOf(Keywords.kw_extends, Keywords.kw_implements)) {
      JSPastExtendsOrImplements = true;
      // JavaScript/TypeScript supports inline object types in
      // extends/implements positions:
      //     class Foo implements {bar: number} { }
      nextToken();
      if (FormatTok->is(tok::l_brace)) {
        tryToParseBracedList();
        continue;
      }
    }
    if (FormatTok->is(tok::l_square) && handleCppAttributes())
      continue;
    const auto *Previous = FormatTok;
    nextToken();
    switch (FormatTok->Tok.getKind()) {
    case tok::l_paren:
      // We can have macros in between 'class' and the class name.
      if (!IsNonMacroIdentifier(Previous) ||
          // e.g. `struct macro(a) S { int i; };`
          Previous->Previous == &InitialToken) {
        parseParens();
      }
      break;
    case tok::coloncolon:
    case tok::hashhash:
      break;
    default:
      if (!JSPastExtendsOrImplements && !ClassName &&
          Previous->is(tok::identifier) && Previous->isNot(TT_AttributeMacro)) {
        ClassName = Previous;
      }
    }
  }

  auto IsListInitialization = [&] {
    if (!ClassName || IsDerived)
      return false;
    assert(FormatTok->is(tok::l_brace));
    const auto *Prev = FormatTok->getPreviousNonComment();
    assert(Prev);
    return Prev != ClassName && Prev->is(tok::identifier) &&
           Prev->isNot(Keywords.kw_final) && tryToParseBracedList();
  };

  if (FormatTok->isOneOf(tok::colon, tok::less)) {
    int AngleNestingLevel = 0;
    do {
      if (FormatTok->is(tok::less))
        ++AngleNestingLevel;
      else if (FormatTok->is(tok::greater))
        --AngleNestingLevel;

      if (AngleNestingLevel == 0) {
        if (FormatTok->is(tok::colon)) {
          IsDerived = true;
        } else if (FormatTok->is(tok::identifier) &&
                   FormatTok->Previous->is(tok::coloncolon)) {
          ClassName = FormatTok;
        } else if (FormatTok->is(tok::l_paren) &&
                   IsNonMacroIdentifier(FormatTok->Previous)) {
          break;
        }
      }
      if (FormatTok->is(tok::l_brace)) {
        if (AngleNestingLevel == 0 && IsListInitialization())
          return;
        calculateBraceTypes(/*ExpectClassBody=*/true);
        if (!tryToParseBracedList())
          break;
      }
      if (FormatTok->is(tok::l_square)) {
        FormatToken *Previous = FormatTok->Previous;
        if (!Previous || (Previous->isNot(tok::r_paren) &&
                          !Previous->isTypeOrIdentifier(LangOpts))) {
          // Don't try parsing a lambda if we had a closing parenthesis before,
          // it was probably a pointer to an array: int (*)[].
          if (!tryToParseLambda())
            continue;
        } else {
          parseSquare();
          continue;
        }
      }
      if (FormatTok->is(tok::semi))
        return;
      if (Style.isCSharp() && FormatTok->is(Keywords.kw_where)) {
        addUnwrappedLine();
        nextToken();
        parseCSharpGenericTypeConstraint();
        break;
      }
      nextToken();
    } while (!eof());
  }

  auto GetBraceTypes =
      [](const FormatToken &RecordTok) -> std::pair<TokenType, TokenType> {
    switch (RecordTok.Tok.getKind()) {
    case tok::kw_class:
      return {TT_ClassLBrace, TT_ClassRBrace};
    case tok::kw_struct:
      return {TT_StructLBrace, TT_StructRBrace};
    case tok::kw_union:
      return {TT_UnionLBrace, TT_UnionRBrace};
    default:
      // Useful for e.g. interface.
      return {TT_RecordLBrace, TT_RecordRBrace};
    }
  };
  if (FormatTok->is(tok::l_brace)) {
    if (IsListInitialization())
      return;
    auto [OpenBraceType, ClosingBraceType] = GetBraceTypes(InitialToken);
    FormatTok->setFinalizedType(OpenBraceType);
    if (ParseAsExpr) {
      parseChildBlock();
    } else {
      if (ShouldBreakBeforeBrace(Style, InitialToken))
        addUnwrappedLine();

      unsigned AddLevels = Style.IndentAccessModifiers ? 2u : 1u;
      parseBlock(/*MustBeDeclaration=*/true, AddLevels, /*MunchSemi=*/false);
    }
    setPreviousRBraceType(ClosingBraceType);
  }
  // There is no addUnwrappedLine() here so that we fall through to parsing a
  // structural element afterwards. Thus, in "class A {} n, m;",
  // "} n, m;" will end up in one unwrapped line.
}

void UnwrappedLineParser::parseObjCMethod() {
  assert(FormatTok->isOneOf(tok::l_paren, tok::identifier) &&
         "'(' or identifier expected.");
  do {
    if (FormatTok->is(tok::semi)) {
      nextToken();
      addUnwrappedLine();
      return;
    } else if (FormatTok->is(tok::l_brace)) {
      if (Style.BraceWrapping.AfterFunction)
        addUnwrappedLine();
      parseBlock();
      addUnwrappedLine();
      return;
    } else {
      nextToken();
    }
  } while (!eof());
}

void UnwrappedLineParser::parseObjCProtocolList() {
  assert(FormatTok->is(tok::less) && "'<' expected.");
  do {
    nextToken();
    // Early exit in case someone forgot a close angle.
    if (FormatTok->isOneOf(tok::semi, tok::l_brace) ||
        FormatTok->isObjCAtKeyword(tok::objc_end)) {
      return;
    }
  } while (!eof() && FormatTok->isNot(tok::greater));
  nextToken(); // Skip '>'.
}

void UnwrappedLineParser::parseObjCUntilAtEnd() {
  do {
    if (FormatTok->isObjCAtKeyword(tok::objc_end)) {
      nextToken();
      addUnwrappedLine();
      break;
    }
    if (FormatTok->is(tok::l_brace)) {
      parseBlock();
      // In ObjC interfaces, nothing should be following the "}".
      addUnwrappedLine();
    } else if (FormatTok->is(tok::r_brace)) {
      // Ignore stray "}". parseStructuralElement doesn't consume them.
      nextToken();
      addUnwrappedLine();
    } else if (FormatTok->isOneOf(tok::minus, tok::plus)) {
      nextToken();
      parseObjCMethod();
    } else {
      parseStructuralElement();
    }
  } while (!eof());
}

void UnwrappedLineParser::parseObjCInterfaceOrImplementation() {
  assert(FormatTok->Tok.getObjCKeywordID() == tok::objc_interface ||
         FormatTok->Tok.getObjCKeywordID() == tok::objc_implementation);
  nextToken();
  nextToken(); // interface name

  // @interface can be followed by a lightweight generic
  // specialization list, then either a base class or a category.
  if (FormatTok->is(tok::less))
    parseObjCLightweightGenerics();
  if (FormatTok->is(tok::colon)) {
    nextToken();
    nextToken(); // base class name
    // The base class can also have lightweight generics applied to it.
    if (FormatTok->is(tok::less))
      parseObjCLightweightGenerics();
  } else if (FormatTok->is(tok::l_paren)) {
    // Skip category, if present.
    parseParens();
  }

  if (FormatTok->is(tok::less))
    parseObjCProtocolList();

  if (FormatTok->is(tok::l_brace)) {
    if (Style.BraceWrapping.AfterObjCDeclaration)
      addUnwrappedLine();
    parseBlock(/*MustBeDeclaration=*/true);
  }

  // With instance variables, this puts '}' on its own line.  Without instance
  // variables, this ends the @interface line.
  addUnwrappedLine();

  parseObjCUntilAtEnd();
}

void UnwrappedLineParser::parseObjCLightweightGenerics() {
  assert(FormatTok->is(tok::less));
  // Unlike protocol lists, generic parameterizations support
  // nested angles:
  //
  // @interface Foo<ValueType : id <NSCopying, NSSecureCoding>> :
  //     NSObject <NSCopying, NSSecureCoding>
  //
  // so we need to count how many open angles we have left.
  unsigned NumOpenAngles = 1;
  do {
    nextToken();
    // Early exit in case someone forgot a close angle.
    if (FormatTok->isOneOf(tok::semi, tok::l_brace) ||
        FormatTok->isObjCAtKeyword(tok::objc_end)) {
      break;
    }
    if (FormatTok->is(tok::less)) {
      ++NumOpenAngles;
    } else if (FormatTok->is(tok::greater)) {
      assert(NumOpenAngles > 0 && "'>' makes NumOpenAngles negative");
      --NumOpenAngles;
    }
  } while (!eof() && NumOpenAngles != 0);
  nextToken(); // Skip '>'.
}

// Returns true for the declaration/definition form of @protocol,
// false for the expression form.
bool UnwrappedLineParser::parseObjCProtocol() {
  assert(FormatTok->Tok.getObjCKeywordID() == tok::objc_protocol);
  nextToken();

  if (FormatTok->is(tok::l_paren)) {
    // The expression form of @protocol, e.g. "Protocol* p = @protocol(foo);".
    return false;
  }

  // The definition/declaration form,
  // @protocol Foo
  // - (int)someMethod;
  // @end

  nextToken(); // protocol name

  if (FormatTok->is(tok::less))
    parseObjCProtocolList();

  // Check for protocol declaration.
  if (FormatTok->is(tok::semi)) {
    nextToken();
    addUnwrappedLine();
    return true;
  }

  addUnwrappedLine();
  parseObjCUntilAtEnd();
  return true;
}

void UnwrappedLineParser::parseJavaScriptEs6ImportExport() {
  bool IsImport = FormatTok->is(Keywords.kw_import);
  assert(IsImport || FormatTok->is(tok::kw_export));
  nextToken();

  // Consume the "default" in "export default class/function".
  if (FormatTok->is(tok::kw_default))
    nextToken();

  // Consume "async function", "function" and "default function", so that these
  // get parsed as free-standing JS functions, i.e. do not require a trailing
  // semicolon.
  if (FormatTok->is(Keywords.kw_async))
    nextToken();
  if (FormatTok->is(Keywords.kw_function)) {
    nextToken();
    return;
  }

  // For imports, `export *`, `export {...}`, consume the rest of the line up
  // to the terminating `;`. For everything else, just return and continue
  // parsing the structural element, i.e. the declaration or expression for
  // `export default`.
  if (!IsImport && !FormatTok->isOneOf(tok::l_brace, tok::star) &&
      !FormatTok->isStringLiteral() &&
      !(FormatTok->is(Keywords.kw_type) &&
        Tokens->peekNextToken()->isOneOf(tok::l_brace, tok::star))) {
    return;
  }

  while (!eof()) {
    if (FormatTok->is(tok::semi))
      return;
    if (Line->Tokens.empty()) {
      // Common issue: Automatic Semicolon Insertion wrapped the line, so the
      // import statement should terminate.
      return;
    }
    if (FormatTok->is(tok::l_brace)) {
      FormatTok->setBlockKind(BK_Block);
      nextToken();
      parseBracedList();
    } else {
      nextToken();
    }
  }
}

void UnwrappedLineParser::parseStatementMacro() {
  nextToken();
  if (FormatTok->is(tok::l_paren))
    parseParens();
  if (FormatTok->is(tok::semi))
    nextToken();
  addUnwrappedLine();
}

void UnwrappedLineParser::parseVerilogHierarchyIdentifier() {
  // consume things like a::`b.c[d:e] or a::*
  while (true) {
    if (FormatTok->isOneOf(tok::star, tok::period, tok::periodstar,
                           tok::coloncolon, tok::hash) ||
        Keywords.isVerilogIdentifier(*FormatTok)) {
      nextToken();
    } else if (FormatTok->is(tok::l_square)) {
      parseSquare();
    } else {
      break;
    }
  }
}

void UnwrappedLineParser::parseVerilogSensitivityList() {
  if (FormatTok->isNot(tok::at))
    return;
  nextToken();
  // A block event expression has 2 at signs.
  if (FormatTok->is(tok::at))
    nextToken();
  switch (FormatTok->Tok.getKind()) {
  case tok::star:
    nextToken();
    break;
  case tok::l_paren:
    parseParens();
    break;
  default:
    parseVerilogHierarchyIdentifier();
    break;
  }
}

unsigned UnwrappedLineParser::parseVerilogHierarchyHeader() {
  unsigned AddLevels = 0;

  if (FormatTok->is(Keywords.kw_clocking)) {
    nextToken();
    if (Keywords.isVerilogIdentifier(*FormatTok))
      nextToken();
    parseVerilogSensitivityList();
    if (FormatTok->is(tok::semi))
      nextToken();
  } else if (FormatTok->isOneOf(tok::kw_case, Keywords.kw_casex,
                                Keywords.kw_casez, Keywords.kw_randcase,
                                Keywords.kw_randsequence)) {
    if (Style.IndentCaseLabels)
      AddLevels++;
    nextToken();
    if (FormatTok->is(tok::l_paren)) {
      FormatTok->setFinalizedType(TT_ConditionLParen);
      parseParens();
    }
    if (FormatTok->isOneOf(Keywords.kw_inside, Keywords.kw_matches))
      nextToken();
    // The case header has no semicolon.
  } else {
    // "module" etc.
    nextToken();
    // all the words like the name of the module and specifiers like
    // "automatic" and the width of function return type
    while (true) {
      if (FormatTok->is(tok::l_square)) {
        auto Prev = FormatTok->getPreviousNonComment();
        if (Prev && Keywords.isVerilogIdentifier(*Prev))
          Prev->setFinalizedType(TT_VerilogDimensionedTypeName);
        parseSquare();
      } else if (Keywords.isVerilogIdentifier(*FormatTok) ||
                 FormatTok->isOneOf(Keywords.kw_automatic, tok::kw_static)) {
        nextToken();
      } else {
        break;
      }
    }

    auto NewLine = [this]() {
      addUnwrappedLine();
      Line->IsContinuation = true;
    };

    // package imports
    while (FormatTok->is(Keywords.kw_import)) {
      NewLine();
      nextToken();
      parseVerilogHierarchyIdentifier();
      if (FormatTok->is(tok::semi))
        nextToken();
    }

    // parameters and ports
    if (FormatTok->is(Keywords.kw_verilogHash)) {
      NewLine();
      nextToken();
      if (FormatTok->is(tok::l_paren)) {
        FormatTok->setFinalizedType(TT_VerilogMultiLineListLParen);
        parseParens();
      }
    }
    if (FormatTok->is(tok::l_paren)) {
      NewLine();
      FormatTok->setFinalizedType(TT_VerilogMultiLineListLParen);
      parseParens();
    }

    // extends and implements
    if (FormatTok->is(Keywords.kw_extends)) {
      NewLine();
      nextToken();
      parseVerilogHierarchyIdentifier();
      if (FormatTok->is(tok::l_paren))
        parseParens();
    }
    if (FormatTok->is(Keywords.kw_implements)) {
      NewLine();
      do {
        nextToken();
        parseVerilogHierarchyIdentifier();
      } while (FormatTok->is(tok::comma));
    }

    // Coverage event for cover groups.
    if (FormatTok->is(tok::at)) {
      NewLine();
      parseVerilogSensitivityList();
    }

    if (FormatTok->is(tok::semi))
      nextToken(/*LevelDifference=*/1);
    addUnwrappedLine();
  }

  return AddLevels;
}

void UnwrappedLineParser::parseVerilogTable() {
  assert(FormatTok->is(Keywords.kw_table));
  nextToken(/*LevelDifference=*/1);
  addUnwrappedLine();

  auto InitialLevel = Line->Level++;
  while (!eof() && !Keywords.isVerilogEnd(*FormatTok)) {
    FormatToken *Tok = FormatTok;
    nextToken();
    if (Tok->is(tok::semi))
      addUnwrappedLine();
    else if (Tok->isOneOf(tok::star, tok::colon, tok::question, tok::minus))
      Tok->setFinalizedType(TT_VerilogTableItem);
  }
  Line->Level = InitialLevel;
  nextToken(/*LevelDifference=*/-1);
  addUnwrappedLine();
}

void UnwrappedLineParser::parseVerilogCaseLabel() {
  // The label will get unindented in AnnotatingParser. If there are no leading
  // spaces, indent the rest here so that things inside the block will be
  // indented relative to things outside. We don't use parseLabel because we
  // don't know whether this colon is a label or a ternary expression at this
  // point.
  auto OrigLevel = Line->Level;
  auto FirstLine = CurrentLines->size();
  if (Line->Level == 0 || (Line->InPPDirective && Line->Level <= 1))
    ++Line->Level;
  else if (!Style.IndentCaseBlocks && Keywords.isVerilogBegin(*FormatTok))
    --Line->Level;
  parseStructuralElement();
  // Restore the indentation in both the new line and the line that has the
  // label.
  if (CurrentLines->size() > FirstLine)
    (*CurrentLines)[FirstLine].Level = OrigLevel;
  Line->Level = OrigLevel;
}

bool UnwrappedLineParser::containsExpansion(const UnwrappedLine &Line) const {
  for (const auto &N : Line.Tokens) {
    if (N.Tok->MacroCtx)
      return true;
    for (const UnwrappedLine &Child : N.Children)
      if (containsExpansion(Child))
        return true;
  }
  return false;
}

void UnwrappedLineParser::addUnwrappedLine(LineLevel AdjustLevel) {
  if (Line->Tokens.empty())
    return;
  LLVM_DEBUG({
    if (!parsingPPDirective()) {
      llvm::dbgs() << "Adding unwrapped line:\n";
      printDebugInfo(*Line);
    }
  });

  // If this line closes a block when in Whitesmiths mode, remember that
  // information so that the level can be decreased after the line is added.
  // This has to happen after the addition of the line since the line itself
  // needs to be indented.
  bool ClosesWhitesmithsBlock =
      Line->MatchingOpeningBlockLineIndex != UnwrappedLine::kInvalidIndex &&
      Style.BreakBeforeBraces == FormatStyle::BS_Whitesmiths;

  // If the current line was expanded from a macro call, we use it to
  // reconstruct an unwrapped line from the structure of the expanded unwrapped
  // line and the unexpanded token stream.
  if (!parsingPPDirective() && !InExpansion && containsExpansion(*Line)) {
    if (!Reconstruct)
      Reconstruct.emplace(Line->Level, Unexpanded);
    Reconstruct->addLine(*Line);

    // While the reconstructed unexpanded lines are stored in the normal
    // flow of lines, the expanded lines are stored on the side to be analyzed
    // in an extra step.
    CurrentExpandedLines.push_back(std::move(*Line));

    if (Reconstruct->finished()) {
      UnwrappedLine Reconstructed = std::move(*Reconstruct).takeResult();
      assert(!Reconstructed.Tokens.empty() &&
             "Reconstructed must at least contain the macro identifier.");
      assert(!parsingPPDirective());
      LLVM_DEBUG({
        llvm::dbgs() << "Adding unexpanded line:\n";
        printDebugInfo(Reconstructed);
      });
      ExpandedLines[Reconstructed.Tokens.begin()->Tok] = CurrentExpandedLines;
      Lines.push_back(std::move(Reconstructed));
      CurrentExpandedLines.clear();
      Reconstruct.reset();
    }
  } else {
    // At the top level we only get here when no unexpansion is going on, or
    // when conditional formatting led to unfinished macro reconstructions.
    assert(!Reconstruct || (CurrentLines != &Lines) || PPStack.size() > 0);
    CurrentLines->push_back(std::move(*Line));
  }
  Line->Tokens.clear();
  Line->MatchingOpeningBlockLineIndex = UnwrappedLine::kInvalidIndex;
  Line->FirstStartColumn = 0;
  Line->IsContinuation = false;
  Line->SeenDecltypeAuto = false;

  if (ClosesWhitesmithsBlock && AdjustLevel == LineLevel::Remove)
    --Line->Level;
  if (!parsingPPDirective() && !PreprocessorDirectives.empty()) {
    CurrentLines->append(
        std::make_move_iterator(PreprocessorDirectives.begin()),
        std::make_move_iterator(PreprocessorDirectives.end()));
    PreprocessorDirectives.clear();
  }
  // Disconnect the current token from the last token on the previous line.
  FormatTok->Previous = nullptr;
}

bool UnwrappedLineParser::eof() const { return FormatTok->is(tok::eof); }

bool UnwrappedLineParser::isOnNewLine(const FormatToken &FormatTok) {
  return (Line->InPPDirective || FormatTok.HasUnescapedNewline) &&
         FormatTok.NewlinesBefore > 0;
}

// Checks if \p FormatTok is a line comment that continues the line comment
// section on \p Line.
static bool
continuesLineCommentSection(const FormatToken &FormatTok,
                            const UnwrappedLine &Line,
                            const llvm::Regex &CommentPragmasRegex) {
  if (Line.Tokens.empty())
    return false;

  StringRef IndentContent = FormatTok.TokenText;
  if (FormatTok.TokenText.starts_with("//") ||
      FormatTok.TokenText.starts_with("/*")) {
    IndentContent = FormatTok.TokenText.substr(2);
  }
  if (CommentPragmasRegex.match(IndentContent))
    return false;

  // If Line starts with a line comment, then FormatTok continues the comment
  // section if its original column is greater or equal to the original start
  // column of the line.
  //
  // Define the min column token of a line as follows: if a line ends in '{' or
  // contains a '{' followed by a line comment, then the min column token is
  // that '{'. Otherwise, the min column token of the line is the first token of
  // the line.
  //
  // If Line starts with a token other than a line comment, then FormatTok
  // continues the comment section if its original column is greater than the
  // original start column of the min column token of the line.
  //
  // For example, the second line comment continues the first in these cases:
  //
  // // first line
  // // second line
  //
  // and:
  //
  // // first line
  //  // second line
  //
  // and:
  //
  // int i; // first line
  //  // second line
  //
  // and:
  //
  // do { // first line
  //      // second line
  //   int i;
  // } while (true);
  //
  // and:
  //
  // enum {
  //   a, // first line
  //    // second line
  //   b
  // };
  //
  // The second line comment doesn't continue the first in these cases:
  //
  //   // first line
  //  // second line
  //
  // and:
  //
  // int i; // first line
  // // second line
  //
  // and:
  //
  // do { // first line
  //   // second line
  //   int i;
  // } while (true);
  //
  // and:
  //
  // enum {
  //   a, // first line
  //   // second line
  // };
  const FormatToken *MinColumnToken = Line.Tokens.front().Tok;

  // Scan for '{//'. If found, use the column of '{' as a min column for line
  // comment section continuation.
  const FormatToken *PreviousToken = nullptr;
  for (const UnwrappedLineNode &Node : Line.Tokens) {
    if (PreviousToken && PreviousToken->is(tok::l_brace) &&
        isLineComment(*Node.Tok)) {
      MinColumnToken = PreviousToken;
      break;
    }
    PreviousToken = Node.Tok;

    // Grab the last newline preceding a token in this unwrapped line.
    if (Node.Tok->NewlinesBefore > 0)
      MinColumnToken = Node.Tok;
  }
  if (PreviousToken && PreviousToken->is(tok::l_brace))
    MinColumnToken = PreviousToken;

  return continuesLineComment(FormatTok, /*Previous=*/Line.Tokens.back().Tok,
                              MinColumnToken);
}

void UnwrappedLineParser::flushComments(bool NewlineBeforeNext) {
  bool JustComments = Line->Tokens.empty();
  for (FormatToken *Tok : CommentsBeforeNextToken) {
    // Line comments that belong to the same line comment section are put on the
    // same line since later we might want to reflow content between them.
    // Additional fine-grained breaking of line comment sections is controlled
    // by the class BreakableLineCommentSection in case it is desirable to keep
    // several line comment sections in the same unwrapped line.
    //
    // FIXME: Consider putting separate line comment sections as children to the
    // unwrapped line instead.
    Tok->ContinuesLineCommentSection =
        continuesLineCommentSection(*Tok, *Line, CommentPragmasRegex);
    if (isOnNewLine(*Tok) && JustComments && !Tok->ContinuesLineCommentSection)
      addUnwrappedLine();
    pushToken(Tok);
  }
  if (NewlineBeforeNext && JustComments)
    addUnwrappedLine();
  CommentsBeforeNextToken.clear();
}

void UnwrappedLineParser::nextToken(int LevelDifference) {
  if (eof())
    return;
  flushComments(isOnNewLine(*FormatTok));
  pushToken(FormatTok);
  FormatToken *Previous = FormatTok;
  if (!Style.isJavaScript())
    readToken(LevelDifference);
  else
    readTokenWithJavaScriptASI();
  FormatTok->Previous = Previous;
  if (Style.isVerilog()) {
    // Blocks in Verilog can have `begin` and `end` instead of braces.  For
    // keywords like `begin`, we can't treat them the same as left braces
    // because some contexts require one of them.  For example structs use
    // braces and if blocks use keywords, and a left brace can occur in an if
    // statement, but it is not a block.  For keywords like `end`, we simply
    // treat them the same as right braces.
    if (Keywords.isVerilogEnd(*FormatTok))
      FormatTok->Tok.setKind(tok::r_brace);
  }
}

void UnwrappedLineParser::distributeComments(
    const SmallVectorImpl<FormatToken *> &Comments,
    const FormatToken *NextTok) {
  // Whether or not a line comment token continues a line is controlled by
  // the method continuesLineCommentSection, with the following caveat:
  //
  // Define a trail of Comments to be a nonempty proper postfix of Comments such
  // that each comment line from the trail is aligned with the next token, if
  // the next token exists. If a trail exists, the beginning of the maximal
  // trail is marked as a start of a new comment section.
  //
  // For example in this code:
  //
  // int a; // line about a
  //   // line 1 about b
  //   // line 2 about b
  //   int b;
  //
  // the two lines about b form a maximal trail, so there are two sections, the
  // first one consisting of the single comment "// line about a" and the
  // second one consisting of the next two comments.
  if (Comments.empty())
    return;
  bool ShouldPushCommentsInCurrentLine = true;
  bool HasTrailAlignedWithNextToken = false;
  unsigned StartOfTrailAlignedWithNextToken = 0;
  if (NextTok) {
    // We are skipping the first element intentionally.
    for (unsigned i = Comments.size() - 1; i > 0; --i) {
      if (Comments[i]->OriginalColumn == NextTok->OriginalColumn) {
        HasTrailAlignedWithNextToken = true;
        StartOfTrailAlignedWithNextToken = i;
      }
    }
  }
  for (unsigned i = 0, e = Comments.size(); i < e; ++i) {
    FormatToken *FormatTok = Comments[i];
    if (HasTrailAlignedWithNextToken && i == StartOfTrailAlignedWithNextToken) {
      FormatTok->ContinuesLineCommentSection = false;
    } else {
      FormatTok->ContinuesLineCommentSection =
          continuesLineCommentSection(*FormatTok, *Line, CommentPragmasRegex);
    }
    if (!FormatTok->ContinuesLineCommentSection &&
        (isOnNewLine(*FormatTok) || FormatTok->IsFirst)) {
      ShouldPushCommentsInCurrentLine = false;
    }
    if (ShouldPushCommentsInCurrentLine)
      pushToken(FormatTok);
    else
      CommentsBeforeNextToken.push_back(FormatTok);
  }
}

void UnwrappedLineParser::readToken(int LevelDifference) {
  SmallVector<FormatToken *, 1> Comments;
  bool PreviousWasComment = false;
  bool FirstNonCommentOnLine = false;
  do {
    FormatTok = Tokens->getNextToken();
    assert(FormatTok);
    while (FormatTok->isOneOf(TT_ConflictStart, TT_ConflictEnd,
                              TT_ConflictAlternative)) {
      if (FormatTok->is(TT_ConflictStart))
        conditionalCompilationStart(/*Unreachable=*/false);
      else if (FormatTok->is(TT_ConflictAlternative))
        conditionalCompilationAlternative();
      else if (FormatTok->is(TT_ConflictEnd))
        conditionalCompilationEnd();
      FormatTok = Tokens->getNextToken();
      FormatTok->MustBreakBefore = true;
      FormatTok->MustBreakBeforeFinalized = true;
    }

    auto IsFirstNonCommentOnLine = [](bool FirstNonCommentOnLine,
                                      const FormatToken &Tok,
                                      bool PreviousWasComment) {
      auto IsFirstOnLine = [](const FormatToken &Tok) {
        return Tok.HasUnescapedNewline || Tok.IsFirst;
      };

      // Consider preprocessor directives preceded by block comments as first
      // on line.
      if (PreviousWasComment)
        return FirstNonCommentOnLine || IsFirstOnLine(Tok);
      return IsFirstOnLine(Tok);
    };

    FirstNonCommentOnLine = IsFirstNonCommentOnLine(
        FirstNonCommentOnLine, *FormatTok, PreviousWasComment);
    PreviousWasComment = FormatTok->is(tok::comment);

    while (!Line->InPPDirective && FormatTok->is(tok::hash) &&
           (!Style.isVerilog() ||
            Keywords.isVerilogPPDirective(*Tokens->peekNextToken())) &&
           FirstNonCommentOnLine) {
      distributeComments(Comments, FormatTok);
      Comments.clear();
      // If there is an unfinished unwrapped line, we flush the preprocessor
      // directives only after that unwrapped line was finished later.
      bool SwitchToPreprocessorLines = !Line->Tokens.empty();
      ScopedLineState BlockState(*this, SwitchToPreprocessorLines);
      assert((LevelDifference >= 0 ||
              static_cast<unsigned>(-LevelDifference) <= Line->Level) &&
             "LevelDifference makes Line->Level negative");
      Line->Level += LevelDifference;
      // Comments stored before the preprocessor directive need to be output
      // before the preprocessor directive, at the same level as the
      // preprocessor directive, as we consider them to apply to the directive.
      if (Style.IndentPPDirectives == FormatStyle::PPDIS_BeforeHash &&
          PPBranchLevel > 0) {
        Line->Level += PPBranchLevel;
      }
      assert(Line->Level >= Line->UnbracedBodyLevel);
      Line->Level -= Line->UnbracedBodyLevel;
      flushComments(isOnNewLine(*FormatTok));
      parsePPDirective();
      PreviousWasComment = FormatTok->is(tok::comment);
      FirstNonCommentOnLine = IsFirstNonCommentOnLine(
          FirstNonCommentOnLine, *FormatTok, PreviousWasComment);
    }

    if (!PPStack.empty() && (PPStack.back().Kind == PP_Unreachable) &&
        !Line->InPPDirective) {
      continue;
    }

    if (FormatTok->is(tok::identifier) &&
        Macros.defined(FormatTok->TokenText) &&
        // FIXME: Allow expanding macros in preprocessor directives.
        !Line->InPPDirective) {
      FormatToken *ID = FormatTok;
      unsigned Position = Tokens->getPosition();

      // To correctly parse the code, we need to replace the tokens of the macro
      // call with its expansion.
      auto PreCall = std::move(Line);
      Line.reset(new UnwrappedLine);
      bool OldInExpansion = InExpansion;
      InExpansion = true;
      // We parse the macro call into a new line.
      auto Args = parseMacroCall();
      InExpansion = OldInExpansion;
      assert(Line->Tokens.front().Tok == ID);
      // And remember the unexpanded macro call tokens.
      auto UnexpandedLine = std::move(Line);
      // Reset to the old line.
      Line = std::move(PreCall);

      LLVM_DEBUG({
        llvm::dbgs() << "Macro call: " << ID->TokenText << "(";
        if (Args) {
          llvm::dbgs() << "(";
          for (const auto &Arg : Args.value())
            for (const auto &T : Arg)
              llvm::dbgs() << T->TokenText << " ";
          llvm::dbgs() << ")";
        }
        llvm::dbgs() << "\n";
      });
      if (Macros.objectLike(ID->TokenText) && Args &&
          !Macros.hasArity(ID->TokenText, Args->size())) {
        // The macro is either
        // - object-like, but we got argumnets, or
        // - overloaded to be both object-like and function-like, but none of
        //   the function-like arities match the number of arguments.
        // Thus, expand as object-like macro.
        LLVM_DEBUG(llvm::dbgs()
                   << "Macro \"" << ID->TokenText
                   << "\" not overloaded for arity " << Args->size()
                   << "or not function-like, using object-like overload.");
        Args.reset();
        UnexpandedLine->Tokens.resize(1);
        Tokens->setPosition(Position);
        nextToken();
        assert(!Args && Macros.objectLike(ID->TokenText));
      }
      if ((!Args && Macros.objectLike(ID->TokenText)) ||
          (Args && Macros.hasArity(ID->TokenText, Args->size()))) {
        // Next, we insert the expanded tokens in the token stream at the
        // current position, and continue parsing.
        Unexpanded[ID] = std::move(UnexpandedLine);
        SmallVector<FormatToken *, 8> Expansion =
            Macros.expand(ID, std::move(Args));
        if (!Expansion.empty())
          FormatTok = Tokens->insertTokens(Expansion);

        LLVM_DEBUG({
          llvm::dbgs() << "Expanded: ";
          for (const auto &T : Expansion)
            llvm::dbgs() << T->TokenText << " ";
          llvm::dbgs() << "\n";
        });
      } else {
        LLVM_DEBUG({
          llvm::dbgs() << "Did not expand macro \"" << ID->TokenText
                       << "\", because it was used ";
          if (Args)
            llvm::dbgs() << "with " << Args->size();
          else
            llvm::dbgs() << "without";
          llvm::dbgs() << " arguments, which doesn't match any definition.\n";
        });
        Tokens->setPosition(Position);
        FormatTok = ID;
      }
    }

    if (FormatTok->isNot(tok::comment)) {
      distributeComments(Comments, FormatTok);
      Comments.clear();
      return;
    }

    Comments.push_back(FormatTok);
  } while (!eof());

  distributeComments(Comments, nullptr);
  Comments.clear();
}

namespace {
template <typename Iterator>
void pushTokens(Iterator Begin, Iterator End,
                llvm::SmallVectorImpl<FormatToken *> &Into) {
  for (auto I = Begin; I != End; ++I) {
    Into.push_back(I->Tok);
    for (const auto &Child : I->Children)
      pushTokens(Child.Tokens.begin(), Child.Tokens.end(), Into);
  }
}
} // namespace

std::optional<llvm::SmallVector<llvm::SmallVector<FormatToken *, 8>, 1>>
UnwrappedLineParser::parseMacroCall() {
  std::optional<llvm::SmallVector<llvm::SmallVector<FormatToken *, 8>, 1>> Args;
  assert(Line->Tokens.empty());
  nextToken();
  if (FormatTok->isNot(tok::l_paren))
    return Args;
  unsigned Position = Tokens->getPosition();
  FormatToken *Tok = FormatTok;
  nextToken();
  Args.emplace();
  auto ArgStart = std::prev(Line->Tokens.end());

  int Parens = 0;
  do {
    switch (FormatTok->Tok.getKind()) {
    case tok::l_paren:
      ++Parens;
      nextToken();
      break;
    case tok::r_paren: {
      if (Parens > 0) {
        --Parens;
        nextToken();
        break;
      }
      Args->push_back({});
      pushTokens(std::next(ArgStart), Line->Tokens.end(), Args->back());
      nextToken();
      return Args;
    }
    case tok::comma: {
      if (Parens > 0) {
        nextToken();
        break;
      }
      Args->push_back({});
      pushTokens(std::next(ArgStart), Line->Tokens.end(), Args->back());
      nextToken();
      ArgStart = std::prev(Line->Tokens.end());
      break;
    }
    default:
      nextToken();
      break;
    }
  } while (!eof());
  Line->Tokens.resize(1);
  Tokens->setPosition(Position);
  FormatTok = Tok;
  return {};
}

void UnwrappedLineParser::pushToken(FormatToken *Tok) {
  Line->Tokens.push_back(UnwrappedLineNode(Tok));
  if (MustBreakBeforeNextToken) {
    Line->Tokens.back().Tok->MustBreakBefore = true;
    Line->Tokens.back().Tok->MustBreakBeforeFinalized = true;
    MustBreakBeforeNextToken = false;
  }
}

} // end namespace format
} // end namespace clang
