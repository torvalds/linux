//===- XCOFFWriter.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Errc.h"
#include "XCOFFWriter.h"

namespace llvm {
namespace objcopy {
namespace xcoff {

using namespace object;

void XCOFFWriter::finalizeHeaders() {
  // File header.
  FileSize += sizeof(XCOFFFileHeader32);
  // Optional file header.
  FileSize += Obj.FileHeader.AuxHeaderSize;
  // Section headers.
  FileSize += sizeof(XCOFFSectionHeader32) * Obj.Sections.size();
}

void XCOFFWriter::finalizeSections() {
  for (const Section &Sec : Obj.Sections) {
    // Section data.
    FileSize += Sec.Contents.size();
    // Relocations.
    FileSize +=
        Sec.SectionHeader.NumberOfRelocations * sizeof(XCOFFRelocation32);
  }
}

void XCOFFWriter::finalizeSymbolStringTable() {
  assert(Obj.FileHeader.SymbolTableOffset >= FileSize);
  FileSize = Obj.FileHeader.SymbolTableOffset;
  // Symbols and auxiliary entries.
  FileSize +=
      Obj.FileHeader.NumberOfSymTableEntries * XCOFF::SymbolTableEntrySize;
  // String table.
  FileSize += Obj.StringTable.size();
}

void XCOFFWriter::finalize() {
  FileSize = 0;
  finalizeHeaders();
  finalizeSections();
  finalizeSymbolStringTable();
}

void XCOFFWriter::writeHeaders() {
  // Write the file header.
  uint8_t *Ptr = reinterpret_cast<uint8_t *>(Buf->getBufferStart());
  memcpy(Ptr, &Obj.FileHeader, sizeof(XCOFFFileHeader32));
  Ptr += sizeof(XCOFFFileHeader32);

  // Write the optional header.
  if (Obj.FileHeader.AuxHeaderSize) {
    memcpy(Ptr, &Obj.OptionalFileHeader, Obj.FileHeader.AuxHeaderSize);
    Ptr += Obj.FileHeader.AuxHeaderSize;
  }

  // Write section headers.
  for (const Section &Sec : Obj.Sections) {
    memcpy(Ptr, &Sec.SectionHeader, sizeof(XCOFFSectionHeader32));
    Ptr += sizeof(XCOFFSectionHeader32);
  }
}

void XCOFFWriter::writeSections() {
  // Write section data.
  for (const Section &Sec : Obj.Sections) {
    uint8_t *Ptr = reinterpret_cast<uint8_t *>(Buf->getBufferStart()) +
                   Sec.SectionHeader.FileOffsetToRawData;
    Ptr = std::copy(Sec.Contents.begin(), Sec.Contents.end(), Ptr);
  }

  // Write relocations.
  for (const Section &Sec : Obj.Sections) {
    uint8_t *Ptr = reinterpret_cast<uint8_t *>(Buf->getBufferStart()) +
                   Sec.SectionHeader.FileOffsetToRelocationInfo;
    for (const XCOFFRelocation32 &Rel : Sec.Relocations) {
      memcpy(Ptr, &Rel, sizeof(XCOFFRelocation32));
      Ptr += sizeof(XCOFFRelocation32);
    }
  }
}

void XCOFFWriter::writeSymbolStringTable() {
  // Write symbols.
  uint8_t *Ptr = reinterpret_cast<uint8_t *>(Buf->getBufferStart()) +
                 Obj.FileHeader.SymbolTableOffset;
  for (const Symbol &Sym : Obj.Symbols) {
    memcpy(Ptr, &Sym.Sym, XCOFF::SymbolTableEntrySize);
    Ptr += XCOFF::SymbolTableEntrySize;
    // Auxiliary symbols.
    memcpy(Ptr, Sym.AuxSymbolEntries.data(), Sym.AuxSymbolEntries.size());
    Ptr += Sym.AuxSymbolEntries.size();
  }
  // Write the string table.
  memcpy(Ptr, Obj.StringTable.data(), Obj.StringTable.size());
  Ptr += Obj.StringTable.size();
}

Error XCOFFWriter::write() {
  finalize();
  Buf = WritableMemoryBuffer::getNewMemBuffer(FileSize);
  if (!Buf)
    return createStringError(errc::not_enough_memory,
                             "failed to allocate memory buffer of " +
                                 Twine::utohexstr(FileSize) + " bytes");

  writeHeaders();
  writeSections();
  writeSymbolStringTable();
  Out.write(Buf->getBufferStart(), Buf->getBufferSize());
  return Error::success();
}

} // end namespace xcoff
} // end namespace objcopy
} // end namespace llvm
