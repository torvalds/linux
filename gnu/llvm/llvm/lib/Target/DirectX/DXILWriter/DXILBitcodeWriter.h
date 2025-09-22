//===- Bitcode/Writer/DXILBitcodeWriter.cpp - DXIL Bitcode Writer ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Bitcode writer implementation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DXILWRITER_DXILBITCODEWRITER_H
#define LLVM_DXILWRITER_DXILBITCODEWRITER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/MC/StringTableBuilder.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/MemoryBufferRef.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace llvm {

class BitstreamWriter;
class Module;
class raw_ostream;

namespace dxil {

class BitcodeWriter {
  SmallVectorImpl<char> &Buffer;
  std::unique_ptr<BitstreamWriter> Stream;

  StringTableBuilder StrtabBuilder{StringTableBuilder::RAW};

  // Owns any strings created by the irsymtab writer until we create the
  // string table.
  BumpPtrAllocator Alloc;

  void writeBlob(unsigned Block, unsigned Record, StringRef Blob);

  std::vector<Module *> Mods;

public:
  /// Create a BitcodeWriter that writes to Buffer.
  BitcodeWriter(SmallVectorImpl<char> &Buffer);

  ~BitcodeWriter();

  /// Write the specified module to the buffer specified at construction time.
  void writeModule(const Module &M);
};

/// Write the specified module to the specified raw output stream.
///
/// For streams where it matters, the given stream should be in "binary"
/// mode.
void WriteDXILToFile(const Module &M, raw_ostream &Out);

} // namespace dxil

} // namespace llvm

#endif // LLVM_DXILWRITER_DXILBITCODEWRITER_H
