//===- InjectedSourceStream.cpp - PDB Headerblock Stream Access -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/PDB/Native/InjectedSourceStream.h"

#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/Native/HashTable.h"
#include "llvm/DebugInfo/PDB/Native/PDBStringTable.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/Native/RawTypes.h"
#include "llvm/Support/BinaryStreamReader.h"

using namespace llvm;
using namespace llvm::msf;
using namespace llvm::support;
using namespace llvm::pdb;

InjectedSourceStream::InjectedSourceStream(
    std::unique_ptr<MappedBlockStream> Stream)
    : Stream(std::move(Stream)) {}

Error InjectedSourceStream::reload(const PDBStringTable &Strings) {
  BinaryStreamReader Reader(*Stream);

  if (auto EC = Reader.readObject(Header))
    return EC;

  if (Header->Version !=
      static_cast<uint32_t>(PdbRaw_SrcHeaderBlockVer::SrcVerOne))
    return make_error<RawError>(raw_error_code::corrupt_file,
                                "Invalid headerblock header version");

  if (auto EC = InjectedSourceTable.load(Reader))
    return EC;

  for (const auto& Entry : *this) {
    if (Entry.second.Size != sizeof(SrcHeaderBlockEntry))
      return make_error<RawError>(raw_error_code::corrupt_file,
                                  "Invalid headerbock entry size");
    if (Entry.second.Version !=
        static_cast<uint32_t>(PdbRaw_SrcHeaderBlockVer::SrcVerOne))
      return make_error<RawError>(raw_error_code::corrupt_file,
                                  "Invalid headerbock entry version");

    // Check that all name references are valid.
    auto Name = Strings.getStringForID(Entry.second.FileNI);
    if (!Name)
      return Name.takeError();
    auto ObjName = Strings.getStringForID(Entry.second.ObjNI);
    if (!ObjName)
      return ObjName.takeError();
    auto VName = Strings.getStringForID(Entry.second.VFileNI);
    if (!VName)
      return VName.takeError();
  }

  assert(Reader.bytesRemaining() == 0);
  return Error::success();
}
