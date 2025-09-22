//===- ComputeReplacements.cpp --------------------------------*- C++ -*-=====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "clang/Tooling/Core/Replacement.h"
#include "clang/Tooling/Syntax/Mutations.h"
#include "clang/Tooling/Syntax/TokenBufferTokenManager.h"
#include "clang/Tooling/Syntax/Tokens.h"
#include "clang/Tooling/Syntax/Tree.h"
#include "llvm/Support/Error.h"

using namespace clang;

namespace {
using ProcessTokensFn = llvm::function_ref<void(llvm::ArrayRef<syntax::Token>,
                                                bool /*IsOriginal*/)>;
/// Enumerates spans of tokens from the tree consecutively laid out in memory.
void enumerateTokenSpans(const syntax::Tree *Root,
                         const syntax::TokenBufferTokenManager &STM,
                         ProcessTokensFn Callback) {
  struct Enumerator {
    Enumerator(const syntax::TokenBufferTokenManager &STM,
               ProcessTokensFn Callback)
        : STM(STM), SpanBegin(nullptr), SpanEnd(nullptr), SpanIsOriginal(false),
          Callback(Callback) {}

    void run(const syntax::Tree *Root) {
      process(Root);
      // Report the last span to the user.
      if (SpanBegin)
        Callback(llvm::ArrayRef(SpanBegin, SpanEnd), SpanIsOriginal);
    }

  private:
    void process(const syntax::Node *N) {
      if (auto *T = dyn_cast<syntax::Tree>(N)) {
        for (const auto *C = T->getFirstChild(); C != nullptr;
             C = C->getNextSibling())
          process(C);
        return;
      }

      auto *L = cast<syntax::Leaf>(N);
      if (SpanEnd == STM.getToken(L->getTokenKey()) &&
          SpanIsOriginal == L->isOriginal()) {
        // Extend the current span.
        ++SpanEnd;
        return;
      }
      // Report the current span to the user.
      if (SpanBegin)
        Callback(llvm::ArrayRef(SpanBegin, SpanEnd), SpanIsOriginal);
      // Start recording a new span.
      SpanBegin = STM.getToken(L->getTokenKey());
      SpanEnd = SpanBegin + 1;
      SpanIsOriginal = L->isOriginal();
    }

    const syntax::TokenBufferTokenManager &STM;
    const syntax::Token *SpanBegin;
    const syntax::Token *SpanEnd;
    bool SpanIsOriginal;
    ProcessTokensFn Callback;
  };

  return Enumerator(STM, Callback).run(Root);
}

syntax::FileRange rangeOfExpanded(const syntax::TokenBufferTokenManager &STM,
                                  llvm::ArrayRef<syntax::Token> Expanded) {
  const auto &Buffer = STM.tokenBuffer();
  const auto &SM = STM.sourceManager();

  // Check that \p Expanded actually points into expanded tokens.
  assert(Buffer.expandedTokens().begin() <= Expanded.begin());
  assert(Expanded.end() < Buffer.expandedTokens().end());

  if (Expanded.empty())
    // (!) empty tokens must always point before end().
    return syntax::FileRange(
        SM, SM.getExpansionLoc(Expanded.begin()->location()), /*Length=*/0);

  auto Spelled = Buffer.spelledForExpanded(Expanded);
  assert(Spelled && "could not find spelled tokens for expanded");
  return syntax::Token::range(SM, Spelled->front(), Spelled->back());
}
} // namespace

tooling::Replacements
syntax::computeReplacements(const TokenBufferTokenManager &TBTM,
                            const syntax::TranslationUnit &TU) {
  const auto &Buffer = TBTM.tokenBuffer();
  const auto &SM = TBTM.sourceManager();

  tooling::Replacements Replacements;
  // Text inserted by the replacement we are building now.
  std::string Replacement;
  auto emitReplacement = [&](llvm::ArrayRef<syntax::Token> ReplacedRange) {
    if (ReplacedRange.empty() && Replacement.empty())
      return;
    llvm::cantFail(Replacements.add(tooling::Replacement(
        SM, rangeOfExpanded(TBTM, ReplacedRange).toCharRange(SM),
        Replacement)));
    Replacement = "";
  };
  const syntax::Token *NextOriginal = Buffer.expandedTokens().begin();
  enumerateTokenSpans(
      &TU, TBTM, [&](llvm::ArrayRef<syntax::Token> Tokens, bool IsOriginal) {
        if (!IsOriginal) {
          Replacement +=
              syntax::Token::range(SM, Tokens.front(), Tokens.back()).text(SM);
          return;
        }
        assert(NextOriginal <= Tokens.begin());
        // We are looking at a span of original tokens.
        if (NextOriginal != Tokens.begin()) {
          // There is a gap, record a replacement or deletion.
          emitReplacement(llvm::ArrayRef(NextOriginal, Tokens.begin()));
        } else {
          // No gap, but we may have pending insertions. Emit them now.
          emitReplacement(llvm::ArrayRef(NextOriginal, /*Length=*/(size_t)0));
        }
        NextOriginal = Tokens.end();
      });

  // We might have pending replacements at the end of file. If so, emit them.
  emitReplacement(
      llvm::ArrayRef(NextOriginal, Buffer.expandedTokens().drop_back().end()));

  return Replacements;
}
