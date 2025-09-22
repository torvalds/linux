//===- MacroExpansionContext.cpp - Macro expansion information --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/MacroExpansionContext.h"
#include "llvm/Support/Debug.h"
#include <optional>

#define DEBUG_TYPE "macro-expansion-context"

static void dumpTokenInto(const clang::Preprocessor &PP, llvm::raw_ostream &OS,
                          clang::Token Tok);

namespace clang {
namespace detail {
class MacroExpansionRangeRecorder : public PPCallbacks {
  const Preprocessor &PP;
  SourceManager &SM;
  MacroExpansionContext::ExpansionRangeMap &ExpansionRanges;

public:
  explicit MacroExpansionRangeRecorder(
      const Preprocessor &PP, SourceManager &SM,
      MacroExpansionContext::ExpansionRangeMap &ExpansionRanges)
      : PP(PP), SM(SM), ExpansionRanges(ExpansionRanges) {}

  void MacroExpands(const Token &MacroName, const MacroDefinition &MD,
                    SourceRange Range, const MacroArgs *Args) override {
    // Ignore annotation tokens like: _Pragma("pack(push, 1)")
    if (MacroName.getIdentifierInfo()->getName() == "_Pragma")
      return;

    SourceLocation MacroNameBegin = SM.getExpansionLoc(MacroName.getLocation());
    assert(MacroNameBegin == SM.getExpansionLoc(Range.getBegin()));

    const SourceLocation ExpansionEnd = [Range, &SM = SM, &MacroName] {
      // If the range is empty, use the length of the macro.
      if (Range.getBegin() == Range.getEnd())
        return SM.getExpansionLoc(
            MacroName.getLocation().getLocWithOffset(MacroName.getLength()));

      // Include the last character.
      return SM.getExpansionLoc(Range.getEnd()).getLocWithOffset(1);
    }();

    (void)PP;
    LLVM_DEBUG(llvm::dbgs() << "MacroExpands event: '";
               dumpTokenInto(PP, llvm::dbgs(), MacroName);
               llvm::dbgs()
               << "' with length " << MacroName.getLength() << " at ";
               MacroNameBegin.print(llvm::dbgs(), SM);
               llvm::dbgs() << ", expansion end at ";
               ExpansionEnd.print(llvm::dbgs(), SM); llvm::dbgs() << '\n';);

    // If the expansion range is empty, use the identifier of the macro as a
    // range.
    MacroExpansionContext::ExpansionRangeMap::iterator It;
    bool Inserted;
    std::tie(It, Inserted) =
        ExpansionRanges.try_emplace(MacroNameBegin, ExpansionEnd);
    if (Inserted) {
      LLVM_DEBUG(llvm::dbgs() << "maps ";
                 It->getFirst().print(llvm::dbgs(), SM); llvm::dbgs() << " to ";
                 It->getSecond().print(llvm::dbgs(), SM);
                 llvm::dbgs() << '\n';);
    } else {
      if (SM.isBeforeInTranslationUnit(It->getSecond(), ExpansionEnd)) {
        It->getSecond() = ExpansionEnd;
        LLVM_DEBUG(
            llvm::dbgs() << "remaps "; It->getFirst().print(llvm::dbgs(), SM);
            llvm::dbgs() << " to "; It->getSecond().print(llvm::dbgs(), SM);
            llvm::dbgs() << '\n';);
      }
    }
  }
};
} // namespace detail
} // namespace clang

using namespace clang;

MacroExpansionContext::MacroExpansionContext(const LangOptions &LangOpts)
    : LangOpts(LangOpts) {}

void MacroExpansionContext::registerForPreprocessor(Preprocessor &NewPP) {
  PP = &NewPP;
  SM = &NewPP.getSourceManager();

  // Make sure that the Preprocessor does not outlive the MacroExpansionContext.
  PP->addPPCallbacks(std::make_unique<detail::MacroExpansionRangeRecorder>(
      *PP, *SM, ExpansionRanges));
  // Same applies here.
  PP->setTokenWatcher([this](const Token &Tok) { onTokenLexed(Tok); });
}

std::optional<StringRef>
MacroExpansionContext::getExpandedText(SourceLocation MacroExpansionLoc) const {
  if (MacroExpansionLoc.isMacroID())
    return std::nullopt;

  // If there was no macro expansion at that location, return std::nullopt.
  if (ExpansionRanges.find_as(MacroExpansionLoc) == ExpansionRanges.end())
    return std::nullopt;

  // There was macro expansion, but resulted in no tokens, return empty string.
  const auto It = ExpandedTokens.find_as(MacroExpansionLoc);
  if (It == ExpandedTokens.end())
    return StringRef{""};

  // Otherwise we have the actual token sequence as string.
  return It->getSecond().str();
}

std::optional<StringRef>
MacroExpansionContext::getOriginalText(SourceLocation MacroExpansionLoc) const {
  if (MacroExpansionLoc.isMacroID())
    return std::nullopt;

  const auto It = ExpansionRanges.find_as(MacroExpansionLoc);
  if (It == ExpansionRanges.end())
    return std::nullopt;

  assert(It->getFirst() != It->getSecond() &&
         "Every macro expansion must cover a non-empty range.");

  return Lexer::getSourceText(
      CharSourceRange::getCharRange(It->getFirst(), It->getSecond()), *SM,
      LangOpts);
}

void MacroExpansionContext::dumpExpansionRanges() const {
  dumpExpansionRangesToStream(llvm::dbgs());
}
void MacroExpansionContext::dumpExpandedTexts() const {
  dumpExpandedTextsToStream(llvm::dbgs());
}

void MacroExpansionContext::dumpExpansionRangesToStream(raw_ostream &OS) const {
  std::vector<std::pair<SourceLocation, SourceLocation>> LocalExpansionRanges;
  LocalExpansionRanges.reserve(ExpansionRanges.size());
  for (const auto &Record : ExpansionRanges)
    LocalExpansionRanges.emplace_back(
        std::make_pair(Record.getFirst(), Record.getSecond()));
  llvm::sort(LocalExpansionRanges);

  OS << "\n=============== ExpansionRanges ===============\n";
  for (const auto &Record : LocalExpansionRanges) {
    OS << "> ";
    Record.first.print(OS, *SM);
    OS << ", ";
    Record.second.print(OS, *SM);
    OS << '\n';
  }
}

void MacroExpansionContext::dumpExpandedTextsToStream(raw_ostream &OS) const {
  std::vector<std::pair<SourceLocation, MacroExpansionText>>
      LocalExpandedTokens;
  LocalExpandedTokens.reserve(ExpandedTokens.size());
  for (const auto &Record : ExpandedTokens)
    LocalExpandedTokens.emplace_back(
        std::make_pair(Record.getFirst(), Record.getSecond()));
  llvm::sort(LocalExpandedTokens);

  OS << "\n=============== ExpandedTokens ===============\n";
  for (const auto &Record : LocalExpandedTokens) {
    OS << "> ";
    Record.first.print(OS, *SM);
    OS << " -> '" << Record.second << "'\n";
  }
}

static void dumpTokenInto(const Preprocessor &PP, raw_ostream &OS, Token Tok) {
  assert(Tok.isNot(tok::raw_identifier));

  // Ignore annotation tokens like: _Pragma("pack(push, 1)")
  if (Tok.isAnnotation())
    return;

  if (IdentifierInfo *II = Tok.getIdentifierInfo()) {
    // FIXME: For now, we don't respect whitespaces between macro expanded
    // tokens. We just emit a space after every identifier to produce a valid
    // code for `int a ;` like expansions.
    //              ^-^-- Space after the 'int' and 'a' identifiers.
    OS << II->getName() << ' ';
  } else if (Tok.isLiteral() && !Tok.needsCleaning() && Tok.getLiteralData()) {
    OS << StringRef(Tok.getLiteralData(), Tok.getLength());
  } else {
    char Tmp[256];
    if (Tok.getLength() < sizeof(Tmp)) {
      const char *TokPtr = Tmp;
      // FIXME: Might use a different overload for cleaner callsite.
      unsigned Len = PP.getSpelling(Tok, TokPtr);
      OS.write(TokPtr, Len);
    } else {
      OS << "<too long token>";
    }
  }
}

void MacroExpansionContext::onTokenLexed(const Token &Tok) {
  SourceLocation SLoc = Tok.getLocation();
  if (SLoc.isFileID())
    return;

  LLVM_DEBUG(llvm::dbgs() << "lexed macro expansion token '";
             dumpTokenInto(*PP, llvm::dbgs(), Tok); llvm::dbgs() << "' at ";
             SLoc.print(llvm::dbgs(), *SM); llvm::dbgs() << '\n';);

  // Remove spelling location.
  SourceLocation CurrExpansionLoc = SM->getExpansionLoc(SLoc);

  MacroExpansionText TokenAsString;
  llvm::raw_svector_ostream OS(TokenAsString);

  // FIXME: Prepend newlines and space to produce the exact same output as the
  // preprocessor would for this token.

  dumpTokenInto(*PP, OS, Tok);

  ExpansionMap::iterator It;
  bool Inserted;
  std::tie(It, Inserted) =
      ExpandedTokens.try_emplace(CurrExpansionLoc, std::move(TokenAsString));
  if (!Inserted)
    It->getSecond().append(TokenAsString);
}

