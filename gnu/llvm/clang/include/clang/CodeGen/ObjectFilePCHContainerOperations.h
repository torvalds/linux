//===-- CodeGen/ObjectFilePCHContainerOperations.h - ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_CODEGEN_OBJECTFILEPCHCONTAINEROPERATIONS_H
#define LLVM_CLANG_CODEGEN_OBJECTFILEPCHCONTAINEROPERATIONS_H

#include "clang/Frontend/PCHContainerOperations.h"

namespace clang {

/// A PCHContainerWriter implementation that uses LLVM to
/// wraps Clang modules inside a COFF, ELF, or Mach-O container.
class ObjectFilePCHContainerWriter : public PCHContainerWriter {
  StringRef getFormat() const override { return "obj"; }

  /// Return an ASTConsumer that can be chained with a
  /// PCHGenerator that produces a wrapper file format
  /// that also contains full debug info for the module.
  std::unique_ptr<ASTConsumer>
  CreatePCHContainerGenerator(CompilerInstance &CI,
                              const std::string &MainFileName,
                              const std::string &OutputFileName,
                              std::unique_ptr<llvm::raw_pwrite_stream> OS,
                              std::shared_ptr<PCHBuffer> Buffer) const override;
};

/// A PCHContainerReader implementation that uses LLVM to
/// wraps Clang modules inside a COFF, ELF, or Mach-O container.
class ObjectFilePCHContainerReader : public PCHContainerReader {
  ArrayRef<StringRef> getFormats() const override;

  /// Returns the serialized AST inside the PCH container Buffer.
  StringRef ExtractPCH(llvm::MemoryBufferRef Buffer) const override;
};
}

#endif
