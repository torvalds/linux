//===- COFFReader.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "COFFReader.h"
#include "COFFObject.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstddef>
#include <cstdint>

namespace llvm {
namespace objcopy {
namespace coff {

using namespace object;
using namespace COFF;

Error COFFReader::readExecutableHeaders(Object &Obj) const {
  const dos_header *DH = COFFObj.getDOSHeader();
  Obj.Is64 = COFFObj.is64();
  if (!DH)
    return Error::success();

  Obj.IsPE = true;
  Obj.DosHeader = *DH;
  if (DH->AddressOfNewExeHeader > sizeof(*DH))
    Obj.DosStub = ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(&DH[1]),
                                    DH->AddressOfNewExeHeader - sizeof(*DH));

  if (COFFObj.is64()) {
    Obj.PeHeader = *COFFObj.getPE32PlusHeader();
  } else {
    const pe32_header *PE32 = COFFObj.getPE32Header();
    copyPeHeader(Obj.PeHeader, *PE32);
    // The pe32plus_header (stored in Object) lacks the BaseOfData field.
    Obj.BaseOfData = PE32->BaseOfData;
  }

  for (size_t I = 0; I < Obj.PeHeader.NumberOfRvaAndSize; I++) {
    const data_directory *Dir = COFFObj.getDataDirectory(I);
    if (!Dir)
      return errorCodeToError(object_error::parse_failed);
    Obj.DataDirectories.emplace_back(*Dir);
  }
  return Error::success();
}

Error COFFReader::readSections(Object &Obj) const {
  std::vector<Section> Sections;
  // Section indexing starts from 1.
  for (size_t I = 1, E = COFFObj.getNumberOfSections(); I <= E; I++) {
    Expected<const coff_section *> SecOrErr = COFFObj.getSection(I);
    if (!SecOrErr)
      return SecOrErr.takeError();
    const coff_section *Sec = *SecOrErr;
    Sections.push_back(Section());
    Section &S = Sections.back();
    S.Header = *Sec;
    S.Header.Characteristics &= ~COFF::IMAGE_SCN_LNK_NRELOC_OVFL;
    ArrayRef<uint8_t> Contents;
    if (Error E = COFFObj.getSectionContents(Sec, Contents))
      return E;
    S.setContentsRef(Contents);
    ArrayRef<coff_relocation> Relocs = COFFObj.getRelocations(Sec);
    for (const coff_relocation &R : Relocs)
      S.Relocs.push_back(R);
    if (Expected<StringRef> NameOrErr = COFFObj.getSectionName(Sec))
      S.Name = *NameOrErr;
    else
      return NameOrErr.takeError();
  }
  Obj.addSections(Sections);
  return Error::success();
}

Error COFFReader::readSymbols(Object &Obj, bool IsBigObj) const {
  std::vector<Symbol> Symbols;
  Symbols.reserve(COFFObj.getNumberOfSymbols());
  ArrayRef<Section> Sections = Obj.getSections();
  for (uint32_t I = 0, E = COFFObj.getNumberOfSymbols(); I < E;) {
    Expected<COFFSymbolRef> SymOrErr = COFFObj.getSymbol(I);
    if (!SymOrErr)
      return SymOrErr.takeError();
    COFFSymbolRef SymRef = *SymOrErr;

    Symbols.push_back(Symbol());
    Symbol &Sym = Symbols.back();
    // Copy symbols from the original form into an intermediate coff_symbol32.
    if (IsBigObj)
      copySymbol(Sym.Sym,
                 *reinterpret_cast<const coff_symbol32 *>(SymRef.getRawPtr()));
    else
      copySymbol(Sym.Sym,
                 *reinterpret_cast<const coff_symbol16 *>(SymRef.getRawPtr()));
    auto NameOrErr = COFFObj.getSymbolName(SymRef);
    if (!NameOrErr)
      return NameOrErr.takeError();
    Sym.Name = *NameOrErr;

    ArrayRef<uint8_t> AuxData = COFFObj.getSymbolAuxData(SymRef);
    size_t SymSize = IsBigObj ? sizeof(coff_symbol32) : sizeof(coff_symbol16);
    assert(AuxData.size() == SymSize * SymRef.getNumberOfAuxSymbols());
    // The auxillary symbols are structs of sizeof(coff_symbol16) each.
    // In the big object format (where symbols are coff_symbol32), each
    // auxillary symbol is padded with 2 bytes at the end. Copy each
    // auxillary symbol to the Sym.AuxData vector. For file symbols,
    // the whole range of aux symbols are interpreted as one null padded
    // string instead.
    if (SymRef.isFileRecord())
      Sym.AuxFile = StringRef(reinterpret_cast<const char *>(AuxData.data()),
                              AuxData.size())
                        .rtrim('\0');
    else
      for (size_t I = 0; I < SymRef.getNumberOfAuxSymbols(); I++)
        Sym.AuxData.push_back(AuxData.slice(I * SymSize, sizeof(AuxSymbol)));

    // Find the unique id of the section
    if (SymRef.getSectionNumber() <=
        0) // Special symbol (undefined/absolute/debug)
      Sym.TargetSectionId = SymRef.getSectionNumber();
    else if (static_cast<uint32_t>(SymRef.getSectionNumber() - 1) <
             Sections.size())
      Sym.TargetSectionId = Sections[SymRef.getSectionNumber() - 1].UniqueId;
    else
      return createStringError(object_error::parse_failed,
                               "section number out of range");
    // For section definitions, check if it is comdat associative, and if
    // it is, find the target section unique id.
    const coff_aux_section_definition *SD = SymRef.getSectionDefinition();
    const coff_aux_weak_external *WE = SymRef.getWeakExternal();
    if (SD && SD->Selection == IMAGE_COMDAT_SELECT_ASSOCIATIVE) {
      int32_t Index = SD->getNumber(IsBigObj);
      if (Index <= 0 || static_cast<uint32_t>(Index - 1) >= Sections.size())
        return createStringError(object_error::parse_failed,
                                 "unexpected associative section index");
      Sym.AssociativeComdatTargetSectionId = Sections[Index - 1].UniqueId;
    } else if (WE) {
      // This is a raw symbol index for now, but store it in the Symbol
      // until we've added them to the Object, which assigns the final
      // unique ids.
      Sym.WeakTargetSymbolId = WE->TagIndex;
    }
    I += 1 + SymRef.getNumberOfAuxSymbols();
  }
  Obj.addSymbols(Symbols);
  return Error::success();
}

Error COFFReader::setSymbolTargets(Object &Obj) const {
  std::vector<const Symbol *> RawSymbolTable;
  for (const Symbol &Sym : Obj.getSymbols()) {
    RawSymbolTable.push_back(&Sym);
    for (size_t I = 0; I < Sym.Sym.NumberOfAuxSymbols; I++)
      RawSymbolTable.push_back(nullptr);
  }
  for (Symbol &Sym : Obj.getMutableSymbols()) {
    // Convert WeakTargetSymbolId from the original raw symbol index to
    // a proper unique id.
    if (Sym.WeakTargetSymbolId) {
      if (*Sym.WeakTargetSymbolId >= RawSymbolTable.size())
        return createStringError(object_error::parse_failed,
                                 "weak external reference out of range");
      const Symbol *Target = RawSymbolTable[*Sym.WeakTargetSymbolId];
      if (Target == nullptr)
        return createStringError(object_error::parse_failed,
                                 "invalid SymbolTableIndex");
      Sym.WeakTargetSymbolId = Target->UniqueId;
    }
  }
  for (Section &Sec : Obj.getMutableSections()) {
    for (Relocation &R : Sec.Relocs) {
      if (R.Reloc.SymbolTableIndex >= RawSymbolTable.size())
        return createStringError(object_error::parse_failed,
                                 "SymbolTableIndex out of range");
      const Symbol *Sym = RawSymbolTable[R.Reloc.SymbolTableIndex];
      if (Sym == nullptr)
        return createStringError(object_error::parse_failed,
                                 "invalid SymbolTableIndex");
      R.Target = Sym->UniqueId;
      R.TargetName = Sym->Name;
    }
  }
  return Error::success();
}

Expected<std::unique_ptr<Object>> COFFReader::create() const {
  auto Obj = std::make_unique<Object>();

  bool IsBigObj = false;
  if (const coff_file_header *CFH = COFFObj.getCOFFHeader()) {
    Obj->CoffFileHeader = *CFH;
  } else {
    const coff_bigobj_file_header *CBFH = COFFObj.getCOFFBigObjHeader();
    if (!CBFH)
      return createStringError(object_error::parse_failed,
                               "no COFF file header returned");
    // Only copying the few fields from the bigobj header that we need
    // and won't recreate in the end.
    Obj->CoffFileHeader.Machine = CBFH->Machine;
    Obj->CoffFileHeader.TimeDateStamp = CBFH->TimeDateStamp;
    IsBigObj = true;
  }

  if (Error E = readExecutableHeaders(*Obj))
    return std::move(E);
  if (Error E = readSections(*Obj))
    return std::move(E);
  if (Error E = readSymbols(*Obj, IsBigObj))
    return std::move(E);
  if (Error E = setSymbolTargets(*Obj))
    return std::move(E);

  return std::move(Obj);
}

} // end namespace coff
} // end namespace objcopy
} // end namespace llvm
