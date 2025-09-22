//===- TokenBufferTokenManager.h  -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_SYNTAX_TOKEN_BUFFER_TOKEN_MANAGER_H
#define LLVM_CLANG_TOOLING_SYNTAX_TOKEN_BUFFER_TOKEN_MANAGER_H

#include "clang/Tooling/Syntax/TokenManager.h"
#include "clang/Tooling/Syntax/Tokens.h"

namespace clang {
namespace syntax {

/// A TokenBuffer-powered token manager.
/// It tracks the underlying token buffers, source manager, etc.
class TokenBufferTokenManager : public TokenManager {
public:
  TokenBufferTokenManager(const TokenBuffer &Tokens,
                          const LangOptions &LangOpts, SourceManager &SourceMgr)
      : Tokens(Tokens), LangOpts(LangOpts), SM(SourceMgr) {}

  static bool classof(const TokenManager *N) { return N->kind() == Kind; }
  llvm::StringLiteral kind() const override { return Kind; }

  llvm::StringRef getText(Key I) const override {
    const auto *Token = getToken(I);
    assert(Token);
    // Handle 'eof' separately, calling text() on it produces an empty string.
    // FIXME: this special logic is for syntax::Leaf dump, move it when we
    // have a direct way to retrive token kind in the syntax::Leaf.
    if (Token->kind() == tok::eof)
      return "<eof>";
    return Token->text(SM);
  }

  const syntax::Token *getToken(Key I) const {
    return reinterpret_cast<const syntax::Token *>(I);
  }
  SourceManager &sourceManager() { return SM; }
  const SourceManager &sourceManager() const { return SM; }
  const TokenBuffer &tokenBuffer() const { return Tokens; }

private:
  // This manager is powered by the TokenBuffer.
  static constexpr llvm::StringLiteral Kind = "TokenBuffer";

  /// Add \p Buffer to the underlying source manager, tokenize it and store the
  /// resulting tokens. Used exclusively in `FactoryImpl` to materialize tokens
  /// that were not written in user code.
  std::pair<FileID, ArrayRef<Token>>
  lexBuffer(std::unique_ptr<llvm::MemoryBuffer> Buffer);
  friend class FactoryImpl;

  const TokenBuffer &Tokens;
  const LangOptions &LangOpts;

  /// The underlying source manager for the ExtraTokens.
  SourceManager &SM;
  /// IDs and storage for additional tokenized files.
  llvm::DenseMap<FileID, std::vector<Token>> ExtraTokens;
};

} // namespace syntax
} // namespace clang

#endif // LLVM_CLANG_TOOLING_SYNTAX_TOKEN_BUFFER_TOKEN_MANAGER_H
