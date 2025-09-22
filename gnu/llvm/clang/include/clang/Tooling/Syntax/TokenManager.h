//===- TokenManager.h - Manage Tokens for syntax-tree ------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines Token interfaces for the clang syntax-tree. This is the level of
// abstraction that the syntax-tree uses to operate on Token.
//
// TokenManager decouples the syntax-tree from a particular token
// implementation. For example, a TokenBuffer captured from a clang parser may
// track macro expansions and associate tokens with clang's SourceManager, while
// a clang pseudoparser would use a flat array of raw-lexed tokens in memory.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLING_SYNTAX_TOKEN_MANAGER_H
#define LLVM_CLANG_TOOLING_SYNTAX_TOKEN_MANAGER_H

#include "llvm/ADT/StringRef.h"
#include <cstdint>

namespace clang {
namespace syntax {

/// Defines interfaces for operating "Token" in the clang syntax-tree.
class TokenManager {
public:
  virtual ~TokenManager() = default;

  /// Describes what the exact class kind of the TokenManager is.
  virtual llvm::StringLiteral kind() const = 0;

  /// A key to identify a specific token. The token concept depends on the
  /// underlying implementation -- it can be a spelled token from the original
  /// source file or an expanded token.
  /// The syntax-tree Leaf node holds a Key.
  using Key = uintptr_t;
  virtual llvm::StringRef getText(Key K) const = 0;
};

} // namespace syntax
} // namespace clang

#endif // LLVM_CLANG_TOOLING_SYNTAX_TOKEN_MANAGER_H
