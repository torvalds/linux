//===- ASTSourceDescriptor.h -----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines the clang::ASTSourceDescriptor class, which abstracts clang modules
/// and precompiled header files
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_ASTSOURCEDESCRIPTOR_H
#define LLVM_CLANG_BASIC_ASTSOURCEDESCRIPTOR_H

#include "clang/Basic/Module.h"
#include "llvm/ADT/StringRef.h"
#include <string>
#include <utility>

namespace clang {

/// Abstracts clang modules and precompiled header files and holds
/// everything needed to generate debug info for an imported module
/// or PCH.
class ASTSourceDescriptor {
  StringRef PCHModuleName;
  StringRef Path;
  StringRef ASTFile;
  ASTFileSignature Signature;
  Module *ClangModule = nullptr;

public:
  ASTSourceDescriptor() = default;
  ASTSourceDescriptor(StringRef Name, StringRef Path, StringRef ASTFile,
                      ASTFileSignature Signature)
      : PCHModuleName(std::move(Name)), Path(std::move(Path)),
        ASTFile(std::move(ASTFile)), Signature(Signature) {}
  ASTSourceDescriptor(Module &M);

  std::string getModuleName() const;
  StringRef getPath() const { return Path; }
  StringRef getASTFile() const { return ASTFile; }
  ASTFileSignature getSignature() const { return Signature; }
  Module *getModuleOrNull() const { return ClangModule; }
};

} // namespace clang

#endif // LLVM_CLANG_BASIC_ASTSOURCEDESCRIPTOR_H
