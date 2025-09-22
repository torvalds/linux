//===-- PCHContainerOperations.h - PCH Containers ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SERIALIZATION_PCHCONTAINEROPERATIONS_H
#define LLVM_CLANG_SERIALIZATION_PCHCONTAINEROPERATIONS_H

#include "clang/Basic/Module.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <memory>

namespace llvm {
class raw_pwrite_stream;
}

namespace clang {

class ASTConsumer;
class CompilerInstance;

struct PCHBuffer {
  ASTFileSignature Signature;
  llvm::SmallVector<char, 0> Data;
  bool IsComplete;
};

/// This abstract interface provides operations for creating
/// containers for serialized ASTs (precompiled headers and clang
/// modules).
class PCHContainerWriter {
public:
  virtual ~PCHContainerWriter() = 0;
  virtual llvm::StringRef getFormat() const = 0;

  /// Return an ASTConsumer that can be chained with a
  /// PCHGenerator that produces a wrapper file format containing a
  /// serialized AST bitstream.
  virtual std::unique_ptr<ASTConsumer>
  CreatePCHContainerGenerator(CompilerInstance &CI,
                              const std::string &MainFileName,
                              const std::string &OutputFileName,
                              std::unique_ptr<llvm::raw_pwrite_stream> OS,
                              std::shared_ptr<PCHBuffer> Buffer) const = 0;
};

/// This abstract interface provides operations for unwrapping
/// containers for serialized ASTs (precompiled headers and clang
/// modules).
class PCHContainerReader {
public:
  virtual ~PCHContainerReader() = 0;
  /// Equivalent to the format passed to -fmodule-format=
  virtual llvm::ArrayRef<llvm::StringRef> getFormats() const = 0;

  /// Returns the serialized AST inside the PCH container Buffer.
  virtual llvm::StringRef ExtractPCH(llvm::MemoryBufferRef Buffer) const = 0;
};

/// Implements write operations for a raw pass-through PCH container.
class RawPCHContainerWriter : public PCHContainerWriter {
  llvm::StringRef getFormat() const override { return "raw"; }

  /// Return an ASTConsumer that can be chained with a
  /// PCHGenerator that writes the module to a flat file.
  std::unique_ptr<ASTConsumer>
  CreatePCHContainerGenerator(CompilerInstance &CI,
                              const std::string &MainFileName,
                              const std::string &OutputFileName,
                              std::unique_ptr<llvm::raw_pwrite_stream> OS,
                              std::shared_ptr<PCHBuffer> Buffer) const override;
};

/// Implements read operations for a raw pass-through PCH container.
class RawPCHContainerReader : public PCHContainerReader {
  llvm::ArrayRef<llvm::StringRef> getFormats() const override;
  /// Simply returns the buffer contained in Buffer.
  llvm::StringRef ExtractPCH(llvm::MemoryBufferRef Buffer) const override;
};

/// A registry of PCHContainerWriter and -Reader objects for different formats.
class PCHContainerOperations {
  llvm::StringMap<std::unique_ptr<PCHContainerWriter>> Writers;
  llvm::StringMap<PCHContainerReader *> Readers;
  llvm::SmallVector<std::unique_ptr<PCHContainerReader>> OwnedReaders;

public:
  /// Automatically registers a RawPCHContainerWriter and
  /// RawPCHContainerReader.
  PCHContainerOperations();
  void registerWriter(std::unique_ptr<PCHContainerWriter> Writer) {
    Writers[Writer->getFormat()] = std::move(Writer);
  }
  void registerReader(std::unique_ptr<PCHContainerReader> Reader) {
    assert(!Reader->getFormats().empty() &&
           "PCHContainerReader must handle >=1 format");
    for (llvm::StringRef Fmt : Reader->getFormats())
      Readers[Fmt] = Reader.get();
    OwnedReaders.push_back(std::move(Reader));
  }
  const PCHContainerWriter *getWriterOrNull(llvm::StringRef Format) {
    return Writers[Format].get();
  }
  const PCHContainerReader *getReaderOrNull(llvm::StringRef Format) {
    return Readers[Format];
  }
  const PCHContainerReader &getRawReader() {
    return *getReaderOrNull("raw");
  }
};

}

#endif
