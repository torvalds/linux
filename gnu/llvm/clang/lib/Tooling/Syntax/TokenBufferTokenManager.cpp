//===- TokenBufferTokenManager.cpp ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/Syntax/TokenBufferTokenManager.h"

namespace clang {
namespace syntax {
constexpr llvm::StringLiteral syntax::TokenBufferTokenManager::Kind;

std::pair<FileID, ArrayRef<syntax::Token>>
syntax::TokenBufferTokenManager::lexBuffer(
    std::unique_ptr<llvm::MemoryBuffer> Input) {
  auto FID = SM.createFileID(std::move(Input));
  auto It = ExtraTokens.try_emplace(FID, tokenize(FID, SM, LangOpts));
  assert(It.second && "duplicate FileID");
  return {FID, It.first->second};
}

} // namespace syntax
} // namespace clang
