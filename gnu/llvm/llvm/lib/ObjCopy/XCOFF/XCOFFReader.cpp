//===- XCOFFReader.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "XCOFFReader.h"

namespace llvm {
namespace objcopy {
namespace xcoff {

using namespace object;

Error XCOFFReader::readSections(Object &Obj) const {
  ArrayRef<XCOFFSectionHeader32> Sections = XCOFFObj.sections32();
  for (const XCOFFSectionHeader32 &Sec : Sections) {
    Section ReadSec;
    // Section header.
    ReadSec.SectionHeader = Sec;
    DataRefImpl SectionDRI;
    SectionDRI.p = reinterpret_cast<uintptr_t>(&Sec);

    // Section data.
    if (Sec.SectionSize) {
      Expected<ArrayRef<uint8_t>> ContentsRef =
          XCOFFObj.getSectionContents(SectionDRI);
      if (!ContentsRef)
        return ContentsRef.takeError();
      ReadSec.Contents = ContentsRef.get();
    }

    // Relocations.
    if (Sec.NumberOfRelocations) {
      auto Relocations =
          XCOFFObj.relocations<XCOFFSectionHeader32, XCOFFRelocation32>(Sec);
      if (!Relocations)
        return Relocations.takeError();
      for (const XCOFFRelocation32 &Rel : Relocations.get())
        ReadSec.Relocations.push_back(Rel);
    }

    Obj.Sections.push_back(std::move(ReadSec));
  }
  return Error::success();
}

Error XCOFFReader::readSymbols(Object &Obj) const {
  std::vector<Symbol> Symbols;
  Symbols.reserve(XCOFFObj.getNumberOfSymbolTableEntries());
  for (SymbolRef Sym : XCOFFObj.symbols()) {
    Symbol ReadSym;
    DataRefImpl SymbolDRI = Sym.getRawDataRefImpl();
    XCOFFSymbolRef SymbolEntRef = XCOFFObj.toSymbolRef(SymbolDRI);
    ReadSym.Sym = *SymbolEntRef.getSymbol32();
    // Auxiliary entries.
    if (SymbolEntRef.getNumberOfAuxEntries()) {
      const char *Start = reinterpret_cast<const char *>(
          SymbolDRI.p + XCOFF::SymbolTableEntrySize);
      Expected<StringRef> RawAuxEntriesOrError = XCOFFObj.getRawData(
          Start,
          XCOFF::SymbolTableEntrySize * SymbolEntRef.getNumberOfAuxEntries(),
          StringRef("symbol"));
      if (!RawAuxEntriesOrError)
        return RawAuxEntriesOrError.takeError();
      ReadSym.AuxSymbolEntries = RawAuxEntriesOrError.get();
    }
    Obj.Symbols.push_back(std::move(ReadSym));
  }
  return Error::success();
}

Expected<std::unique_ptr<Object>> XCOFFReader::create() const {
  auto Obj = std::make_unique<Object>();
  // Only 32-bit supported now.
  if (XCOFFObj.is64Bit())
    return createStringError(object_error::invalid_file_type,
                             "64-bit XCOFF is not supported yet");
  // Read the file header.
  Obj->FileHeader = *XCOFFObj.fileHeader32();
  // Read the optional header.
  if (XCOFFObj.getOptionalHeaderSize())
    Obj->OptionalFileHeader = *XCOFFObj.auxiliaryHeader32();
  // Read each section.
  Obj->Sections.reserve(XCOFFObj.getNumberOfSections());
  if (Error E = readSections(*Obj))
    return std::move(E);
  // Read each symbol.
  Obj->Symbols.reserve(XCOFFObj.getRawNumberOfSymbolTableEntries32());
  if (Error E = readSymbols(*Obj))
    return std::move(E);
  // String table.
  Obj->StringTable = XCOFFObj.getStringTable();
  return std::move(Obj);
}

} // end namespace xcoff
} // end namespace objcopy
} // end namespace llvm
