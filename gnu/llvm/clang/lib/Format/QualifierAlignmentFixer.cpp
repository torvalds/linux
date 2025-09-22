//===--- QualifierAlignmentFixer.cpp ----------------------------*- C++--*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements QualifierAlignmentFixer, a TokenAnalyzer that
/// enforces either left or right const depending on the style.
///
//===----------------------------------------------------------------------===//

#include "QualifierAlignmentFixer.h"
#include "FormatToken.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Regex.h"

#include <algorithm>
#include <optional>

#define DEBUG_TYPE "format-qualifier-alignment-fixer"

namespace clang {
namespace format {

void addQualifierAlignmentFixerPasses(const FormatStyle &Style,
                                      SmallVectorImpl<AnalyzerPass> &Passes) {
  std::vector<std::string> LeftOrder;
  std::vector<std::string> RightOrder;
  std::vector<tok::TokenKind> ConfiguredQualifierTokens;
  prepareLeftRightOrderingForQualifierAlignmentFixer(
      Style.QualifierOrder, LeftOrder, RightOrder, ConfiguredQualifierTokens);

  // Handle the left and right alignment separately.
  for (const auto &Qualifier : LeftOrder) {
    Passes.emplace_back(
        [&, Qualifier, ConfiguredQualifierTokens](const Environment &Env) {
          return LeftRightQualifierAlignmentFixer(Env, Style, Qualifier,
                                                  ConfiguredQualifierTokens,
                                                  /*RightAlign=*/false)
              .process();
        });
  }
  for (const auto &Qualifier : RightOrder) {
    Passes.emplace_back(
        [&, Qualifier, ConfiguredQualifierTokens](const Environment &Env) {
          return LeftRightQualifierAlignmentFixer(Env, Style, Qualifier,
                                                  ConfiguredQualifierTokens,
                                                  /*RightAlign=*/true)
              .process();
        });
  }
}

static void replaceToken(const SourceManager &SourceMgr,
                         tooling::Replacements &Fixes,
                         const CharSourceRange &Range, std::string NewText) {
  auto Replacement = tooling::Replacement(SourceMgr, Range, NewText);
  auto Err = Fixes.add(Replacement);

  if (Err) {
    llvm::errs() << "Error while rearranging Qualifier : "
                 << llvm::toString(std::move(Err)) << "\n";
  }
}

static void removeToken(const SourceManager &SourceMgr,
                        tooling::Replacements &Fixes,
                        const FormatToken *First) {
  auto Range = CharSourceRange::getCharRange(First->getStartOfNonWhitespace(),
                                             First->Tok.getEndLoc());
  replaceToken(SourceMgr, Fixes, Range, "");
}

static void insertQualifierAfter(const SourceManager &SourceMgr,
                                 tooling::Replacements &Fixes,
                                 const FormatToken *First,
                                 const std::string &Qualifier) {
  auto Range = CharSourceRange::getCharRange(First->Tok.getLocation(),
                                             First->Tok.getEndLoc());

  std::string NewText{};
  NewText += First->TokenText;
  NewText += " " + Qualifier;
  replaceToken(SourceMgr, Fixes, Range, NewText);
}

static void insertQualifierBefore(const SourceManager &SourceMgr,
                                  tooling::Replacements &Fixes,
                                  const FormatToken *First,
                                  const std::string &Qualifier) {
  auto Range = CharSourceRange::getCharRange(First->getStartOfNonWhitespace(),
                                             First->Tok.getEndLoc());

  std::string NewText = " " + Qualifier + " ";
  NewText += First->TokenText;

  replaceToken(SourceMgr, Fixes, Range, NewText);
}

static bool endsWithSpace(const std::string &s) {
  if (s.empty())
    return false;
  return isspace(s.back());
}

static bool startsWithSpace(const std::string &s) {
  if (s.empty())
    return false;
  return isspace(s.front());
}

static void rotateTokens(const SourceManager &SourceMgr,
                         tooling::Replacements &Fixes, const FormatToken *First,
                         const FormatToken *Last, bool Left) {
  auto *End = Last;
  auto *Begin = First;
  if (!Left) {
    End = Last->Next;
    Begin = First->Next;
  }

  std::string NewText;
  // If we are rotating to the left we move the Last token to the front.
  if (Left) {
    NewText += Last->TokenText;
    NewText += " ";
  }

  // Then move through the other tokens.
  auto *Tok = Begin;
  while (Tok != End) {
    if (!NewText.empty() && !endsWithSpace(NewText))
      NewText += " ";

    NewText += Tok->TokenText;
    Tok = Tok->Next;
  }

  // If we are rotating to the right we move the first token to the back.
  if (!Left) {
    if (!NewText.empty() && !startsWithSpace(NewText))
      NewText += " ";
    NewText += First->TokenText;
  }

  auto Range = CharSourceRange::getCharRange(First->getStartOfNonWhitespace(),
                                             Last->Tok.getEndLoc());

  replaceToken(SourceMgr, Fixes, Range, NewText);
}

static bool
isConfiguredQualifier(const FormatToken *const Tok,
                      const std::vector<tok::TokenKind> &Qualifiers) {
  return Tok && llvm::is_contained(Qualifiers, Tok->Tok.getKind());
}

static bool isQualifier(const FormatToken *const Tok) {
  if (!Tok)
    return false;

  switch (Tok->Tok.getKind()) {
  case tok::kw_const:
  case tok::kw_volatile:
  case tok::kw_static:
  case tok::kw_inline:
  case tok::kw_constexpr:
  case tok::kw_restrict:
  case tok::kw_friend:
    return true;
  default:
    return false;
  }
}

const FormatToken *LeftRightQualifierAlignmentFixer::analyzeRight(
    const SourceManager &SourceMgr, const AdditionalKeywords &Keywords,
    tooling::Replacements &Fixes, const FormatToken *const Tok,
    const std::string &Qualifier, tok::TokenKind QualifierType) {
  // We only need to think about streams that begin with a qualifier.
  if (Tok->isNot(QualifierType))
    return Tok;
  // Don't concern yourself if nothing follows the qualifier.
  if (!Tok->Next)
    return Tok;

  // Skip qualifiers to the left to find what preceeds the qualifiers.
  // Use isQualifier rather than isConfiguredQualifier to cover all qualifiers.
  const FormatToken *PreviousCheck = Tok->getPreviousNonComment();
  while (isQualifier(PreviousCheck))
    PreviousCheck = PreviousCheck->getPreviousNonComment();

  // Examples given in order of ['type', 'const', 'volatile']
  const bool IsRightQualifier = PreviousCheck && [PreviousCheck]() {
    // The cases:
    // `Foo() const` -> `Foo() const`
    // `Foo() const final` -> `Foo() const final`
    // `Foo() const override` -> `Foo() const final`
    // `Foo() const volatile override` -> `Foo() const volatile override`
    // `Foo() volatile const final` -> `Foo() const volatile final`
    if (PreviousCheck->is(tok::r_paren))
      return true;

    // The cases:
    // `struct {} volatile const a;` -> `struct {} const volatile a;`
    // `class {} volatile const a;` -> `class {} const volatile a;`
    if (PreviousCheck->is(tok::r_brace))
      return true;

    // The case:
    // `template <class T> const Bar Foo()` ->
    // `template <class T> Bar const Foo()`
    // The cases:
    // `Foo<int> const foo` -> `Foo<int> const foo`
    // `Foo<int> volatile const` -> `Foo<int> const volatile`
    // The case:
    // ```
    // template <class T>
    //   requires Concept1<T> && requires Concept2<T>
    // const Foo f();
    // ```
    // ->
    // ```
    // template <class T>
    //   requires Concept1<T> && requires Concept2<T>
    // Foo const f();
    // ```
    if (PreviousCheck->is(TT_TemplateCloser)) {
      // If the token closes a template<> or requires clause, then it is a left
      // qualifier and should be moved to the right.
      return !(PreviousCheck->ClosesTemplateDeclaration ||
               PreviousCheck->ClosesRequiresClause);
    }

    // The case  `Foo* const` -> `Foo* const`
    // The case  `Foo* volatile const` -> `Foo* const volatile`
    // The case  `int32_t const` -> `int32_t const`
    // The case  `auto volatile const` -> `auto const volatile`
    if (PreviousCheck->isOneOf(TT_PointerOrReference, tok::identifier,
                               tok::kw_auto)) {
      return true;
    }

    return false;
  }();

  // Find the last qualifier to the right.
  const FormatToken *LastQual = Tok;
  while (isQualifier(LastQual->getNextNonComment()))
    LastQual = LastQual->getNextNonComment();

  // If this qualifier is to the right of a type or pointer do a partial sort
  // and return.
  if (IsRightQualifier) {
    if (LastQual != Tok)
      rotateTokens(SourceMgr, Fixes, Tok, LastQual, /*Left=*/false);
    return Tok;
  }

  const FormatToken *TypeToken = LastQual->getNextNonComment();
  if (!TypeToken)
    return Tok;

  // Stay safe and don't move past macros, also don't bother with sorting.
  if (isPossibleMacro(TypeToken))
    return Tok;

  // The case `const long long int volatile` -> `long long int const volatile`
  // The case `long const long int volatile` -> `long long int const volatile`
  // The case `long long volatile int const` -> `long long int const volatile`
  // The case `const long long volatile int` -> `long long int const volatile`
  if (TypeToken->isTypeName(LangOpts)) {
    // The case `const decltype(foo)` -> `const decltype(foo)`
    // The case `const typeof(foo)` -> `const typeof(foo)`
    // The case `const _Atomic(foo)` -> `const _Atomic(foo)`
    if (TypeToken->isOneOf(tok::kw_decltype, tok::kw_typeof, tok::kw__Atomic))
      return Tok;

    const FormatToken *LastSimpleTypeSpecifier = TypeToken;
    while (isQualifierOrType(LastSimpleTypeSpecifier->getNextNonComment(),
                             LangOpts)) {
      LastSimpleTypeSpecifier = LastSimpleTypeSpecifier->getNextNonComment();
    }

    rotateTokens(SourceMgr, Fixes, Tok, LastSimpleTypeSpecifier,
                 /*Left=*/false);
    return LastSimpleTypeSpecifier;
  }

  // The case  `unsigned short const` -> `unsigned short const`
  // The case:
  // `unsigned short volatile const` -> `unsigned short const volatile`
  if (PreviousCheck && PreviousCheck->isTypeName(LangOpts)) {
    if (LastQual != Tok)
      rotateTokens(SourceMgr, Fixes, Tok, LastQual, /*Left=*/false);
    return Tok;
  }

  // Skip the typename keyword.
  // The case `const typename C::type` -> `typename C::type const`
  if (TypeToken->is(tok::kw_typename))
    TypeToken = TypeToken->getNextNonComment();

  // Skip the initial :: of a global-namespace type.
  // The case `const ::...` -> `::... const`
  if (TypeToken->is(tok::coloncolon)) {
    // The case `const ::template Foo...` -> `::template Foo... const`
    TypeToken = TypeToken->getNextNonComment();
    if (TypeToken && TypeToken->is(tok::kw_template))
      TypeToken = TypeToken->getNextNonComment();
  }

  // Don't change declarations such as
  // `foo(const struct Foo a);` -> `foo(const struct Foo a);`
  // as they would currently change code such as
  // `const struct my_struct_t {} my_struct;` -> `struct my_struct_t const {}
  // my_struct;`
  if (TypeToken->isOneOf(tok::kw_struct, tok::kw_class))
    return Tok;

  if (TypeToken->isOneOf(tok::kw_auto, tok::identifier)) {
    // The case  `const auto` -> `auto const`
    // The case  `const Foo` -> `Foo const`
    // The case  `const ::Foo` -> `::Foo const`
    // The case  `const Foo *` -> `Foo const *`
    // The case  `const Foo &` -> `Foo const &`
    // The case  `const Foo &&` -> `Foo const &&`
    // The case  `const std::Foo &&` -> `std::Foo const &&`
    // The case  `const std::Foo<T> &&` -> `std::Foo<T> const &&`
    // The case  `const ::template Foo` -> `::template Foo const`
    // The case  `const T::template Foo` -> `T::template Foo const`
    const FormatToken *Next = nullptr;
    while ((Next = TypeToken->getNextNonComment()) &&
           (Next->is(TT_TemplateOpener) ||
            Next->startsSequence(tok::coloncolon, tok::identifier) ||
            Next->startsSequence(tok::coloncolon, tok::kw_template,
                                 tok::identifier))) {
      if (Next->is(TT_TemplateOpener)) {
        assert(Next->MatchingParen && "Missing template closer");
        TypeToken = Next->MatchingParen;
      } else if (Next->startsSequence(tok::coloncolon, tok::identifier)) {
        TypeToken = Next->getNextNonComment();
      } else {
        TypeToken = Next->getNextNonComment()->getNextNonComment();
      }
    }

    if (Next->is(tok::kw_auto))
      TypeToken = Next;

    // Place the Qualifier at the end of the list of qualifiers.
    while (isQualifier(TypeToken->getNextNonComment())) {
      // The case `volatile Foo::iter const` -> `Foo::iter const volatile`
      TypeToken = TypeToken->getNextNonComment();
    }

    insertQualifierAfter(SourceMgr, Fixes, TypeToken, Qualifier);
    // Remove token and following whitespace.
    auto Range = CharSourceRange::getCharRange(
        Tok->getStartOfNonWhitespace(), Tok->Next->getStartOfNonWhitespace());
    replaceToken(SourceMgr, Fixes, Range, "");
  }

  return Tok;
}

const FormatToken *LeftRightQualifierAlignmentFixer::analyzeLeft(
    const SourceManager &SourceMgr, const AdditionalKeywords &Keywords,
    tooling::Replacements &Fixes, const FormatToken *const Tok,
    const std::string &Qualifier, tok::TokenKind QualifierType) {
  // We only need to think about streams that begin with a qualifier.
  if (Tok->isNot(QualifierType))
    return Tok;
  // Don't concern yourself if nothing preceeds the qualifier.
  if (!Tok->getPreviousNonComment())
    return Tok;

  // Skip qualifiers to the left to find what preceeds the qualifiers.
  const FormatToken *TypeToken = Tok->getPreviousNonComment();
  while (isQualifier(TypeToken))
    TypeToken = TypeToken->getPreviousNonComment();

  // For left qualifiers preceeded by nothing, a template declaration, or *,&,&&
  // we only perform sorting.
  if (!TypeToken || TypeToken->isPointerOrReference() ||
      TypeToken->ClosesRequiresClause || TypeToken->ClosesTemplateDeclaration) {

    // Don't sort past a non-configured qualifier token.
    const FormatToken *FirstQual = Tok;
    while (isConfiguredQualifier(FirstQual->getPreviousNonComment(),
                                 ConfiguredQualifierTokens)) {
      FirstQual = FirstQual->getPreviousNonComment();
    }

    if (FirstQual != Tok)
      rotateTokens(SourceMgr, Fixes, FirstQual, Tok, /*Left=*/true);
    return Tok;
  }

  // Stay safe and don't move past macros, also don't bother with sorting.
  if (isPossibleMacro(TypeToken))
    return Tok;

  // Examples given in order of ['const', 'volatile', 'type']

  // The case `volatile long long int const` -> `const volatile long long int`
  // The case `volatile long long const int` -> `const volatile long long int`
  // The case `const long long volatile int` -> `const volatile long long int`
  // The case `long volatile long int const` -> `const volatile long long int`
  if (TypeToken->isTypeName(LangOpts)) {
    const FormatToken *LastSimpleTypeSpecifier = TypeToken;
    while (isConfiguredQualifierOrType(
        LastSimpleTypeSpecifier->getPreviousNonComment(),
        ConfiguredQualifierTokens, LangOpts)) {
      LastSimpleTypeSpecifier =
          LastSimpleTypeSpecifier->getPreviousNonComment();
    }

    rotateTokens(SourceMgr, Fixes, LastSimpleTypeSpecifier, Tok,
                 /*Left=*/true);
    return Tok;
  }

  if (TypeToken->isOneOf(tok::kw_auto, tok::identifier, TT_TemplateCloser)) {
    const auto IsStartOfType = [](const FormatToken *const Tok) -> bool {
      if (!Tok)
        return true;

      // A template closer is not the start of a type.
      // The case `?<> const` -> `const ?<>`
      if (Tok->is(TT_TemplateCloser))
        return false;

      const FormatToken *const Previous = Tok->getPreviousNonComment();
      if (!Previous)
        return true;

      // An identifier preceeded by :: is not the start of a type.
      // The case `?::Foo const` -> `const ?::Foo`
      if (Tok->is(tok::identifier) && Previous->is(tok::coloncolon))
        return false;

      const FormatToken *const PrePrevious = Previous->getPreviousNonComment();
      // An identifier preceeded by ::template is not the start of a type.
      // The case `?::template Foo const` -> `const ?::template Foo`
      if (Tok->is(tok::identifier) && Previous->is(tok::kw_template) &&
          PrePrevious && PrePrevious->is(tok::coloncolon)) {
        return false;
      }

      if (Tok->endsSequence(tok::kw_auto, tok::identifier))
        return false;

      return true;
    };

    while (!IsStartOfType(TypeToken)) {
      // The case `?<>`
      if (TypeToken->is(TT_TemplateCloser)) {
        assert(TypeToken->MatchingParen && "Missing template opener");
        TypeToken = TypeToken->MatchingParen->getPreviousNonComment();
      } else {
        // The cases
        // `::Foo`
        // `?>::Foo`
        // `?Bar::Foo`
        // `::template Foo`
        // `?>::template Foo`
        // `?Bar::template Foo`
        if (TypeToken->getPreviousNonComment()->is(tok::kw_template))
          TypeToken = TypeToken->getPreviousNonComment();

        const FormatToken *const ColonColon =
            TypeToken->getPreviousNonComment();
        const FormatToken *const PreColonColon =
            ColonColon->getPreviousNonComment();
        if (PreColonColon &&
            PreColonColon->isOneOf(TT_TemplateCloser, tok::identifier)) {
          TypeToken = PreColonColon;
        } else {
          TypeToken = ColonColon;
        }
      }
    }

    assert(TypeToken && "Should be auto or identifier");

    // Place the Qualifier at the start of the list of qualifiers.
    const FormatToken *Previous = nullptr;
    while ((Previous = TypeToken->getPreviousNonComment()) &&
           (isConfiguredQualifier(Previous, ConfiguredQualifierTokens) ||
            Previous->is(tok::kw_typename))) {
      // The case `volatile Foo::iter const` -> `const volatile Foo::iter`
      // The case `typename C::type const` -> `const typename C::type`
      TypeToken = Previous;
    }

    // Don't change declarations such as
    // `foo(struct Foo const a);` -> `foo(struct Foo const a);`
    if (!Previous || !Previous->isOneOf(tok::kw_struct, tok::kw_class)) {
      insertQualifierBefore(SourceMgr, Fixes, TypeToken, Qualifier);
      removeToken(SourceMgr, Fixes, Tok);
    }
  }

  return Tok;
}

tok::TokenKind LeftRightQualifierAlignmentFixer::getTokenFromQualifier(
    const std::string &Qualifier) {
  // Don't let 'type' be an identifier, but steal typeof token.
  return llvm::StringSwitch<tok::TokenKind>(Qualifier)
      .Case("type", tok::kw_typeof)
      .Case("const", tok::kw_const)
      .Case("volatile", tok::kw_volatile)
      .Case("static", tok::kw_static)
      .Case("inline", tok::kw_inline)
      .Case("constexpr", tok::kw_constexpr)
      .Case("restrict", tok::kw_restrict)
      .Case("friend", tok::kw_friend)
      .Default(tok::identifier);
}

LeftRightQualifierAlignmentFixer::LeftRightQualifierAlignmentFixer(
    const Environment &Env, const FormatStyle &Style,
    const std::string &Qualifier,
    const std::vector<tok::TokenKind> &QualifierTokens, bool RightAlign)
    : TokenAnalyzer(Env, Style), Qualifier(Qualifier), RightAlign(RightAlign),
      ConfiguredQualifierTokens(QualifierTokens) {}

std::pair<tooling::Replacements, unsigned>
LeftRightQualifierAlignmentFixer::analyze(
    TokenAnnotator & /*Annotator*/,
    SmallVectorImpl<AnnotatedLine *> &AnnotatedLines,
    FormatTokenLexer &Tokens) {
  tooling::Replacements Fixes;
  AffectedRangeMgr.computeAffectedLines(AnnotatedLines);
  fixQualifierAlignment(AnnotatedLines, Tokens, Fixes);
  return {Fixes, 0};
}

void LeftRightQualifierAlignmentFixer::fixQualifierAlignment(
    SmallVectorImpl<AnnotatedLine *> &AnnotatedLines, FormatTokenLexer &Tokens,
    tooling::Replacements &Fixes) {
  const AdditionalKeywords &Keywords = Tokens.getKeywords();
  const SourceManager &SourceMgr = Env.getSourceManager();
  tok::TokenKind QualifierToken = getTokenFromQualifier(Qualifier);
  assert(QualifierToken != tok::identifier && "Unrecognised Qualifier");

  for (AnnotatedLine *Line : AnnotatedLines) {
    fixQualifierAlignment(Line->Children, Tokens, Fixes);
    if (!Line->Affected || Line->InPPDirective)
      continue;
    FormatToken *First = Line->First;
    assert(First);
    if (First->Finalized)
      continue;

    const auto *Last = Line->Last;

    for (const auto *Tok = First; Tok && Tok != Last && Tok->Next;
         Tok = Tok->Next) {
      if (Tok->MustBreakBefore)
        break;
      if (Tok->is(tok::comment))
        continue;
      if (RightAlign) {
        Tok = analyzeRight(SourceMgr, Keywords, Fixes, Tok, Qualifier,
                           QualifierToken);
      } else {
        Tok = analyzeLeft(SourceMgr, Keywords, Fixes, Tok, Qualifier,
                          QualifierToken);
      }
    }
  }
}

void prepareLeftRightOrderingForQualifierAlignmentFixer(
    const std::vector<std::string> &Order, std::vector<std::string> &LeftOrder,
    std::vector<std::string> &RightOrder,
    std::vector<tok::TokenKind> &Qualifiers) {

  // Depending on the position of type in the order you need
  // To iterate forward or backward through the order list as qualifier
  // can push through each other.
  // The Order list must define the position of "type" to signify
  assert(llvm::is_contained(Order, "type") &&
         "QualifierOrder must contain type");
  // Split the Order list by type and reverse the left side.

  bool left = true;
  for (const auto &s : Order) {
    if (s == "type") {
      left = false;
      continue;
    }

    tok::TokenKind QualifierToken =
        LeftRightQualifierAlignmentFixer::getTokenFromQualifier(s);
    if (QualifierToken != tok::kw_typeof && QualifierToken != tok::identifier)
      Qualifiers.push_back(QualifierToken);

    if (left) {
      // Reverse the order for left aligned items.
      LeftOrder.insert(LeftOrder.begin(), s);
    } else {
      RightOrder.push_back(s);
    }
  }
}

bool isQualifierOrType(const FormatToken *Tok, const LangOptions &LangOpts) {
  return Tok && (Tok->isTypeName(LangOpts) || Tok->is(tok::kw_auto) ||
                 isQualifier(Tok));
}

bool isConfiguredQualifierOrType(const FormatToken *Tok,
                                 const std::vector<tok::TokenKind> &Qualifiers,
                                 const LangOptions &LangOpts) {
  return Tok && (Tok->isTypeName(LangOpts) || Tok->is(tok::kw_auto) ||
                 isConfiguredQualifier(Tok, Qualifiers));
}

// If a token is an identifier and it's upper case, it could
// be a macro and hence we need to be able to ignore it.
bool isPossibleMacro(const FormatToken *Tok) {
  if (!Tok)
    return false;
  if (Tok->isNot(tok::identifier))
    return false;
  if (Tok->TokenText.upper() == Tok->TokenText.str()) {
    // T,K,U,V likely could be template arguments
    return Tok->TokenText.size() != 1;
  }
  return false;
}

} // namespace format
} // namespace clang
