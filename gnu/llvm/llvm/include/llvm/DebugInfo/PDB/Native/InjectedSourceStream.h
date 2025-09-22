//===- InjectedSourceStream.h - PDB Headerblock Stream Access ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_INJECTEDSOURCESTREAM_H
#define LLVM_DEBUGINFO_PDB_NATIVE_INJECTEDSOURCESTREAM_H

#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/HashTable.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace pdb {
struct SrcHeaderBlockEntry;
struct SrcHeaderBlockHeader;
class PDBStringTable;

class InjectedSourceStream {
public:
  InjectedSourceStream(std::unique_ptr<msf::MappedBlockStream> Stream);
  Error reload(const PDBStringTable &Strings);

  using const_iterator = HashTable<SrcHeaderBlockEntry>::const_iterator;
  const_iterator begin() const { return InjectedSourceTable.begin(); }
  const_iterator end() const { return InjectedSourceTable.end(); }

  uint32_t size() const { return InjectedSourceTable.size(); }

private:
  std::unique_ptr<msf::MappedBlockStream> Stream;

  const SrcHeaderBlockHeader* Header;
  HashTable<SrcHeaderBlockEntry> InjectedSourceTable;
};
}
} // namespace llvm

#endif
