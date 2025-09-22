//===--- HeaderIncludes.cpp - Insert/Delete #includes --*- C++ -*----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Inclusions/HeaderIncludes.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/Path.h"
#include <optional>

namespace clang {
namespace tooling {
namespace {

LangOptions createLangOpts() {
  LangOptions LangOpts;
  LangOpts.CPlusPlus = 1;
  LangOpts.CPlusPlus11 = 1;
  LangOpts.CPlusPlus14 = 1;
  LangOpts.LineComment = 1;
  LangOpts.CXXOperatorNames = 1;
  LangOpts.Bool = 1;
  LangOpts.ObjC = 1;
  LangOpts.MicrosoftExt = 1;    // To get kw___try, kw___finally.
  LangOpts.DeclSpecKeyword = 1; // To get __declspec.
  LangOpts.WChar = 1;           // To get wchar_t
  return LangOpts;
}

// Returns the offset after skipping a sequence of tokens, matched by \p
// GetOffsetAfterSequence, from the start of the code.
// \p GetOffsetAfterSequence should be a function that matches a sequence of
// tokens and returns an offset after the sequence.
unsigned getOffsetAfterTokenSequence(
    StringRef FileName, StringRef Code, const IncludeStyle &Style,
    llvm::function_ref<unsigned(const SourceManager &, Lexer &, Token &)>
        GetOffsetAfterSequence) {
  SourceManagerForFile VirtualSM(FileName, Code);
  SourceManager &SM = VirtualSM.get();
  LangOptions LangOpts = createLangOpts();
  Lexer Lex(SM.getMainFileID(), SM.getBufferOrFake(SM.getMainFileID()), SM,
            LangOpts);
  Token Tok;
  // Get the first token.
  Lex.LexFromRawLexer(Tok);
  return GetOffsetAfterSequence(SM, Lex, Tok);
}

// Check if a sequence of tokens is like "#<Name> <raw_identifier>". If it is,
// \p Tok will be the token after this directive; otherwise, it can be any token
// after the given \p Tok (including \p Tok). If \p RawIDName is provided, the
// (second) raw_identifier name is checked.
bool checkAndConsumeDirectiveWithName(
    Lexer &Lex, StringRef Name, Token &Tok,
    std::optional<StringRef> RawIDName = std::nullopt) {
  bool Matched = Tok.is(tok::hash) && !Lex.LexFromRawLexer(Tok) &&
                 Tok.is(tok::raw_identifier) &&
                 Tok.getRawIdentifier() == Name && !Lex.LexFromRawLexer(Tok) &&
                 Tok.is(tok::raw_identifier) &&
                 (!RawIDName || Tok.getRawIdentifier() == *RawIDName);
  if (Matched)
    Lex.LexFromRawLexer(Tok);
  return Matched;
}

void skipComments(Lexer &Lex, Token &Tok) {
  while (Tok.is(tok::comment))
    if (Lex.LexFromRawLexer(Tok))
      return;
}

// Returns the offset after header guard directives and any comments
// before/after header guards (e.g. #ifndef/#define pair, #pragma once). If no
// header guard is present in the code, this will return the offset after
// skipping all comments from the start of the code.
unsigned getOffsetAfterHeaderGuardsAndComments(StringRef FileName,
                                               StringRef Code,
                                               const IncludeStyle &Style) {
  // \p Consume returns location after header guard or 0 if no header guard is
  // found.
  auto ConsumeHeaderGuardAndComment =
      [&](std::function<unsigned(const SourceManager &SM, Lexer &Lex,
                                 Token Tok)>
              Consume) {
        return getOffsetAfterTokenSequence(
            FileName, Code, Style,
            [&Consume](const SourceManager &SM, Lexer &Lex, Token Tok) {
              skipComments(Lex, Tok);
              unsigned InitialOffset = SM.getFileOffset(Tok.getLocation());
              return std::max(InitialOffset, Consume(SM, Lex, Tok));
            });
      };
  return std::max(
      // #ifndef/#define
      ConsumeHeaderGuardAndComment(
          [](const SourceManager &SM, Lexer &Lex, Token Tok) -> unsigned {
            if (checkAndConsumeDirectiveWithName(Lex, "ifndef", Tok)) {
              skipComments(Lex, Tok);
              if (checkAndConsumeDirectiveWithName(Lex, "define", Tok) &&
                  Tok.isAtStartOfLine())
                return SM.getFileOffset(Tok.getLocation());
            }
            return 0;
          }),
      // #pragma once
      ConsumeHeaderGuardAndComment(
          [](const SourceManager &SM, Lexer &Lex, Token Tok) -> unsigned {
            if (checkAndConsumeDirectiveWithName(Lex, "pragma", Tok,
                                                 StringRef("once")))
              return SM.getFileOffset(Tok.getLocation());
            return 0;
          }));
}

// Check if a sequence of tokens is like
//    "#include ("header.h" | <header.h>)".
// If it is, \p Tok will be the token after this directive; otherwise, it can be
// any token after the given \p Tok (including \p Tok).
bool checkAndConsumeInclusiveDirective(Lexer &Lex, Token &Tok) {
  auto Matched = [&]() {
    Lex.LexFromRawLexer(Tok);
    return true;
  };
  if (Tok.is(tok::hash) && !Lex.LexFromRawLexer(Tok) &&
      Tok.is(tok::raw_identifier) && Tok.getRawIdentifier() == "include") {
    if (Lex.LexFromRawLexer(Tok))
      return false;
    if (Tok.is(tok::string_literal))
      return Matched();
    if (Tok.is(tok::less)) {
      while (!Lex.LexFromRawLexer(Tok) && Tok.isNot(tok::greater)) {
      }
      if (Tok.is(tok::greater))
        return Matched();
    }
  }
  return false;
}

// Returns the offset of the last #include directive after which a new
// #include can be inserted. This ignores #include's after the #include block(s)
// in the beginning of a file to avoid inserting headers into code sections
// where new #include's should not be added by default.
// These code sections include:
//      - raw string literals (containing #include).
//      - #if blocks.
//      - Special #include's among declarations (e.g. functions).
//
// If no #include after which a new #include can be inserted, this returns the
// offset after skipping all comments from the start of the code.
// Inserting after an #include is not allowed if it comes after code that is not
// #include (e.g. pre-processing directive that is not #include, declarations).
unsigned getMaxHeaderInsertionOffset(StringRef FileName, StringRef Code,
                                     const IncludeStyle &Style) {
  return getOffsetAfterTokenSequence(
      FileName, Code, Style,
      [](const SourceManager &SM, Lexer &Lex, Token Tok) {
        skipComments(Lex, Tok);
        unsigned MaxOffset = SM.getFileOffset(Tok.getLocation());
        while (checkAndConsumeInclusiveDirective(Lex, Tok))
          MaxOffset = SM.getFileOffset(Tok.getLocation());
        return MaxOffset;
      });
}

inline StringRef trimInclude(StringRef IncludeName) {
  return IncludeName.trim("\"<>");
}

const char IncludeRegexPattern[] =
    R"(^[\t\ ]*#[\t\ ]*(import|include)[^"<]*(["<][^">]*[">]))";

// The filename of Path excluding extension.
// Used to match implementation with headers, this differs from sys::path::stem:
//  - in names with multiple dots (foo.cu.cc) it terminates at the *first*
//  - an empty stem is never returned: /foo/.bar.x => .bar
//  - we don't bother to handle . and .. specially
StringRef matchingStem(llvm::StringRef Path) {
  StringRef Name = llvm::sys::path::filename(Path);
  return Name.substr(0, Name.find('.', 1));
}

} // anonymous namespace

IncludeCategoryManager::IncludeCategoryManager(const IncludeStyle &Style,
                                               StringRef FileName)
    : Style(Style), FileName(FileName) {
  for (const auto &Category : Style.IncludeCategories) {
    CategoryRegexs.emplace_back(Category.Regex, Category.RegexIsCaseSensitive
                                                    ? llvm::Regex::NoFlags
                                                    : llvm::Regex::IgnoreCase);
  }
  IsMainFile = FileName.ends_with(".c") || FileName.ends_with(".cc") ||
               FileName.ends_with(".cpp") || FileName.ends_with(".c++") ||
               FileName.ends_with(".cxx") || FileName.ends_with(".m") ||
               FileName.ends_with(".mm");
  if (!Style.IncludeIsMainSourceRegex.empty()) {
    llvm::Regex MainFileRegex(Style.IncludeIsMainSourceRegex);
    IsMainFile |= MainFileRegex.match(FileName);
  }
}

int IncludeCategoryManager::getIncludePriority(StringRef IncludeName,
                                               bool CheckMainHeader) const {
  int Ret = INT_MAX;
  for (unsigned i = 0, e = CategoryRegexs.size(); i != e; ++i)
    if (CategoryRegexs[i].match(IncludeName)) {
      Ret = Style.IncludeCategories[i].Priority;
      break;
    }
  if (CheckMainHeader && IsMainFile && Ret > 0 && isMainHeader(IncludeName))
    Ret = 0;
  return Ret;
}

int IncludeCategoryManager::getSortIncludePriority(StringRef IncludeName,
                                                   bool CheckMainHeader) const {
  int Ret = INT_MAX;
  for (unsigned i = 0, e = CategoryRegexs.size(); i != e; ++i)
    if (CategoryRegexs[i].match(IncludeName)) {
      Ret = Style.IncludeCategories[i].SortPriority;
      if (Ret == 0)
        Ret = Style.IncludeCategories[i].Priority;
      break;
    }
  if (CheckMainHeader && IsMainFile && Ret > 0 && isMainHeader(IncludeName))
    Ret = 0;
  return Ret;
}
bool IncludeCategoryManager::isMainHeader(StringRef IncludeName) const {
  switch (Style.MainIncludeChar) {
  case IncludeStyle::MICD_Quote:
    if (!IncludeName.starts_with("\""))
      return false;
    break;
  case IncludeStyle::MICD_AngleBracket:
    if (!IncludeName.starts_with("<"))
      return false;
    break;
  case IncludeStyle::MICD_Any:
    break;
  }

  IncludeName =
      IncludeName.drop_front(1).drop_back(1); // remove the surrounding "" or <>
  // Not matchingStem: implementation files may have compound extensions but
  // headers may not.
  StringRef HeaderStem = llvm::sys::path::stem(IncludeName);
  StringRef FileStem = llvm::sys::path::stem(FileName); // foo.cu for foo.cu.cc
  StringRef MatchingFileStem = matchingStem(FileName);  // foo for foo.cu.cc
  // main-header examples:
  //  1) foo.h => foo.cc
  //  2) foo.h => foo.cu.cc
  //  3) foo.proto.h => foo.proto.cc
  //
  // non-main-header examples:
  //  1) foo.h => bar.cc
  //  2) foo.proto.h => foo.cc
  StringRef Matching;
  if (MatchingFileStem.starts_with_insensitive(HeaderStem))
    Matching = MatchingFileStem; // example 1), 2)
  else if (FileStem.equals_insensitive(HeaderStem))
    Matching = FileStem; // example 3)
  if (!Matching.empty()) {
    llvm::Regex MainIncludeRegex(HeaderStem.str() + Style.IncludeIsMainRegex,
                                 llvm::Regex::IgnoreCase);
    if (MainIncludeRegex.match(Matching))
      return true;
  }
  return false;
}

const llvm::Regex HeaderIncludes::IncludeRegex(IncludeRegexPattern);

HeaderIncludes::HeaderIncludes(StringRef FileName, StringRef Code,
                               const IncludeStyle &Style)
    : FileName(FileName), Code(Code), FirstIncludeOffset(-1),
      MinInsertOffset(
          getOffsetAfterHeaderGuardsAndComments(FileName, Code, Style)),
      MaxInsertOffset(MinInsertOffset +
                      getMaxHeaderInsertionOffset(
                          FileName, Code.drop_front(MinInsertOffset), Style)),
      MainIncludeFound(false),
      Categories(Style, FileName) {
  // Add 0 for main header and INT_MAX for headers that are not in any
  // category.
  Priorities = {0, INT_MAX};
  for (const auto &Category : Style.IncludeCategories)
    Priorities.insert(Category.Priority);
  SmallVector<StringRef, 32> Lines;
  Code.drop_front(MinInsertOffset).split(Lines, "\n");

  unsigned Offset = MinInsertOffset;
  unsigned NextLineOffset;
  SmallVector<StringRef, 4> Matches;
  for (auto Line : Lines) {
    NextLineOffset = std::min(Code.size(), Offset + Line.size() + 1);
    if (IncludeRegex.match(Line, &Matches)) {
      // If this is the last line without trailing newline, we need to make
      // sure we don't delete across the file boundary.
      addExistingInclude(
          Include(Matches[2],
                  tooling::Range(
                      Offset, std::min(Line.size() + 1, Code.size() - Offset)),
                  Matches[1] == "import" ? tooling::IncludeDirective::Import
                                         : tooling::IncludeDirective::Include),
          NextLineOffset);
    }
    Offset = NextLineOffset;
  }

  // Populate CategoryEndOfssets:
  // - Ensure that CategoryEndOffset[Highest] is always populated.
  // - If CategoryEndOffset[Priority] isn't set, use the next higher value
  // that is set, up to CategoryEndOffset[Highest].
  auto Highest = Priorities.begin();
  if (CategoryEndOffsets.find(*Highest) == CategoryEndOffsets.end()) {
    if (FirstIncludeOffset >= 0)
      CategoryEndOffsets[*Highest] = FirstIncludeOffset;
    else
      CategoryEndOffsets[*Highest] = MinInsertOffset;
  }
  // By this point, CategoryEndOffset[Highest] is always set appropriately:
  //  - to an appropriate location before/after existing #includes, or
  //  - to right after the header guard, or
  //  - to the beginning of the file.
  for (auto I = ++Priorities.begin(), E = Priorities.end(); I != E; ++I)
    if (CategoryEndOffsets.find(*I) == CategoryEndOffsets.end())
      CategoryEndOffsets[*I] = CategoryEndOffsets[*std::prev(I)];
}

// \p Offset: the start of the line following this include directive.
void HeaderIncludes::addExistingInclude(Include IncludeToAdd,
                                        unsigned NextLineOffset) {
  auto Iter =
      ExistingIncludes.try_emplace(trimInclude(IncludeToAdd.Name)).first;
  Iter->second.push_back(std::move(IncludeToAdd));
  auto &CurInclude = Iter->second.back();
  // The header name with quotes or angle brackets.
  // Only record the offset of current #include if we can insert after it.
  if (CurInclude.R.getOffset() <= MaxInsertOffset) {
    int Priority = Categories.getIncludePriority(
        CurInclude.Name, /*CheckMainHeader=*/!MainIncludeFound);
    if (Priority == 0)
      MainIncludeFound = true;
    CategoryEndOffsets[Priority] = NextLineOffset;
    IncludesByPriority[Priority].push_back(&CurInclude);
    if (FirstIncludeOffset < 0)
      FirstIncludeOffset = CurInclude.R.getOffset();
  }
}

std::optional<tooling::Replacement>
HeaderIncludes::insert(llvm::StringRef IncludeName, bool IsAngled,
                       IncludeDirective Directive) const {
  assert(IncludeName == trimInclude(IncludeName));
  // If a <header> ("header") already exists in code, "header" (<header>) with
  // different quotation and/or directive will still be inserted.
  // FIXME: figure out if this is the best behavior.
  auto It = ExistingIncludes.find(IncludeName);
  if (It != ExistingIncludes.end()) {
    for (const auto &Inc : It->second)
      if (Inc.Directive == Directive &&
          ((IsAngled && StringRef(Inc.Name).starts_with("<")) ||
           (!IsAngled && StringRef(Inc.Name).starts_with("\""))))
        return std::nullopt;
  }
  std::string Quoted =
      std::string(llvm::formatv(IsAngled ? "<{0}>" : "\"{0}\"", IncludeName));
  StringRef QuotedName = Quoted;
  int Priority = Categories.getIncludePriority(
      QuotedName, /*CheckMainHeader=*/!MainIncludeFound);
  auto CatOffset = CategoryEndOffsets.find(Priority);
  assert(CatOffset != CategoryEndOffsets.end());
  unsigned InsertOffset = CatOffset->second; // Fall back offset
  auto Iter = IncludesByPriority.find(Priority);
  if (Iter != IncludesByPriority.end()) {
    for (const auto *Inc : Iter->second) {
      if (QuotedName < Inc->Name) {
        InsertOffset = Inc->R.getOffset();
        break;
      }
    }
  }
  assert(InsertOffset <= Code.size());
  llvm::StringRef DirectiveSpelling =
      Directive == IncludeDirective::Include ? "include" : "import";
  std::string NewInclude =
      llvm::formatv("#{0} {1}\n", DirectiveSpelling, QuotedName);
  // When inserting headers at end of the code, also append '\n' to the code
  // if it does not end with '\n'.
  // FIXME: when inserting multiple #includes at the end of code, only one
  // newline should be added.
  if (InsertOffset == Code.size() && (!Code.empty() && Code.back() != '\n'))
    NewInclude = "\n" + NewInclude;
  return tooling::Replacement(FileName, InsertOffset, 0, NewInclude);
}

tooling::Replacements HeaderIncludes::remove(llvm::StringRef IncludeName,
                                             bool IsAngled) const {
  assert(IncludeName == trimInclude(IncludeName));
  tooling::Replacements Result;
  auto Iter = ExistingIncludes.find(IncludeName);
  if (Iter == ExistingIncludes.end())
    return Result;
  for (const auto &Inc : Iter->second) {
    if ((IsAngled && StringRef(Inc.Name).starts_with("\"")) ||
        (!IsAngled && StringRef(Inc.Name).starts_with("<")))
      continue;
    llvm::Error Err = Result.add(tooling::Replacement(
        FileName, Inc.R.getOffset(), Inc.R.getLength(), ""));
    if (Err) {
      auto ErrMsg = "Unexpected conflicts in #include deletions: " +
                    llvm::toString(std::move(Err));
      llvm_unreachable(ErrMsg.c_str());
    }
  }
  return Result;
}

} // namespace tooling
} // namespace clang
