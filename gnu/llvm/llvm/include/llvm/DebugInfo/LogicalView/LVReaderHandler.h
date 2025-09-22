//===-- LVReaderHandler.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class implements the Reader handler.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_LOGICALVIEW_READERS_LVREADERHANDLER_H
#define LLVM_DEBUGINFO_LOGICALVIEW_READERS_LVREADERHANDLER_H

#include "llvm/ADT/PointerUnion.h"
#include "llvm/DebugInfo/LogicalView/Core/LVReader.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/MachOUniversal.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/ScopedPrinter.h"
#include <string>
#include <vector>

namespace llvm {
namespace logicalview {

using LVReaders = std::vector<std::unique_ptr<LVReader>>;
using ArgVector = std::vector<std::string>;
using PdbOrObj = PointerUnion<object::ObjectFile *, pdb::PDBFile *>;

// This class performs the following tasks:
// - Creates a logical reader for every binary file in the command line,
//   that parses the debug information and creates a high level logical
//   view representation containing scopes, symbols, types and lines.
// - Prints and compares the logical views.
//
// The supported binary formats are: ELF, Mach-O and CodeView.
class LVReaderHandler {
  ArgVector &Objects;
  ScopedPrinter &W;
  raw_ostream &OS;
  LVReaders TheReaders;

  Error createReaders();
  Error printReaders();
  Error compareReaders();

  Error handleArchive(LVReaders &Readers, StringRef Filename,
                      object::Archive &Arch);
  Error handleBuffer(LVReaders &Readers, StringRef Filename,
                     MemoryBufferRef Buffer, StringRef ExePath = {});
  Error handleFile(LVReaders &Readers, StringRef Filename,
                   StringRef ExePath = {});
  Error handleMach(LVReaders &Readers, StringRef Filename,
                   object::MachOUniversalBinary &Mach);
  Error handleObject(LVReaders &Readers, StringRef Filename,
                     object::Binary &Binary);
  Error handleObject(LVReaders &Readers, StringRef Filename, StringRef Buffer,
                     StringRef ExePath);

  Error createReader(StringRef Filename, LVReaders &Readers, PdbOrObj &Input,
                     StringRef FileFormatName, StringRef ExePath = {});

public:
  LVReaderHandler() = delete;
  LVReaderHandler(ArgVector &Objects, ScopedPrinter &W,
                  LVOptions &ReaderOptions)
      : Objects(Objects), W(W), OS(W.getOStream()) {
    setOptions(&ReaderOptions);
  }
  LVReaderHandler(const LVReaderHandler &) = delete;
  LVReaderHandler &operator=(const LVReaderHandler &) = delete;

  Error createReader(StringRef Filename, LVReaders &Readers) {
    return handleFile(Readers, Filename);
  }
  Error process();

  Expected<std::unique_ptr<LVReader>> createReader(StringRef Pathname) {
    LVReaders Readers;
    if (Error Err = createReader(Pathname, Readers))
      return std::move(Err);
    return std::move(Readers[0]);
  }

  void print(raw_ostream &OS) const;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void dump() const { print(dbgs()); }
#endif
};

} // end namespace logicalview
} // namespace llvm

#endif // LLVM_DEBUGINFO_LOGICALVIEW_READERS_LVREADERHANDLER_H
