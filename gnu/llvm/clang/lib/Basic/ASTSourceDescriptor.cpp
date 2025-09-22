//===- ASTSourceDescriptor.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// Defines the clang::ASTSourceDescriptor class, which abstracts clang modules
/// and precompiled header files
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/ASTSourceDescriptor.h"

namespace clang {

ASTSourceDescriptor::ASTSourceDescriptor(Module &M)
    : Signature(M.Signature), ClangModule(&M) {
  if (M.Directory)
    Path = M.Directory->getName();
  if (auto File = M.getASTFile())
    ASTFile = File->getName();
}

std::string ASTSourceDescriptor::getModuleName() const {
  if (ClangModule)
    return ClangModule->Name;
  else
    return std::string(PCHModuleName);
}

} // namespace clang
