//===- COFFWriter.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "COFFWriter.h"
#include "COFFObject.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorHandling.h"
#include <cstddef>
#include <cstdint>

namespace llvm {
namespace objcopy {
namespace coff {

using namespace object;
using namespace COFF;

Error COFFWriter::finalizeRelocTargets() {
  for (Section &Sec : Obj.getMutableSections()) {
    for (Relocation &R : Sec.Relocs) {
      const Symbol *Sym = Obj.findSymbol(R.Target);
      if (Sym == nullptr)
        return createStringError(object_error::invalid_symbol_index,
                                 "relocation target '%s' (%zu) not found",
                                 R.TargetName.str().c_str(), R.Target);
      R.Reloc.SymbolTableIndex = Sym->RawIndex;
    }
  }
  return Error::success();
}

Error COFFWriter::finalizeSymbolContents() {
  for (Symbol &Sym : Obj.getMutableSymbols()) {
    if (Sym.TargetSectionId <= 0) {
      // Undefined, or a special kind of symbol. These negative values
      // are stored in the SectionNumber field which is unsigned.
      Sym.Sym.SectionNumber = static_cast<uint32_t>(Sym.TargetSectionId);
    } else {
      const Section *Sec = Obj.findSection(Sym.TargetSectionId);
      if (Sec == nullptr)
        return createStringError(object_error::invalid_symbol_index,
                                 "symbol '%s' points to a removed section",
                                 Sym.Name.str().c_str());
      Sym.Sym.SectionNumber = Sec->Index;

      if (Sym.Sym.NumberOfAuxSymbols == 1 &&
          Sym.Sym.StorageClass == IMAGE_SYM_CLASS_STATIC) {
        coff_aux_section_definition *SD =
            reinterpret_cast<coff_aux_section_definition *>(
                Sym.AuxData[0].Opaque);
        uint32_t SDSectionNumber;
        if (Sym.AssociativeComdatTargetSectionId == 0) {
          // Not a comdat associative section; just set the Number field to
          // the number of the section itself.
          SDSectionNumber = Sec->Index;
        } else {
          Sec = Obj.findSection(Sym.AssociativeComdatTargetSectionId);
          if (Sec == nullptr)
            return createStringError(
                object_error::invalid_symbol_index,
                "symbol '%s' is associative to a removed section",
                Sym.Name.str().c_str());
          SDSectionNumber = Sec->Index;
        }
        // Update the section definition with the new section number.
        SD->NumberLowPart = static_cast<uint16_t>(SDSectionNumber);
        SD->NumberHighPart = static_cast<uint16_t>(SDSectionNumber >> 16);
      }
    }
    // Check that we actually have got AuxData to match the weak symbol target
    // we want to set. Only >= 1 would be required, but only == 1 makes sense.
    if (Sym.WeakTargetSymbolId && Sym.Sym.NumberOfAuxSymbols == 1) {
      coff_aux_weak_external *WE =
          reinterpret_cast<coff_aux_weak_external *>(Sym.AuxData[0].Opaque);
      const Symbol *Target = Obj.findSymbol(*Sym.WeakTargetSymbolId);
      if (Target == nullptr)
        return createStringError(object_error::invalid_symbol_index,
                                 "symbol '%s' is missing its weak target",
                                 Sym.Name.str().c_str());
      WE->TagIndex = Target->RawIndex;
    }
  }
  return Error::success();
}

void COFFWriter::layoutSections() {
  for (auto &S : Obj.getMutableSections()) {
    if (S.Header.SizeOfRawData > 0)
      S.Header.PointerToRawData = FileSize;
    else
      S.Header.PointerToRawData = 0;
    FileSize += S.Header.SizeOfRawData; // For executables, this is already
                                        // aligned to FileAlignment.
    if (S.Relocs.size() >= 0xffff) {
      S.Header.Characteristics |= COFF::IMAGE_SCN_LNK_NRELOC_OVFL;
      S.Header.NumberOfRelocations = 0xffff;
      S.Header.PointerToRelocations = FileSize;
      FileSize += sizeof(coff_relocation);
    } else {
      S.Header.NumberOfRelocations = S.Relocs.size();
      S.Header.PointerToRelocations = S.Relocs.size() ? FileSize : 0;
    }

    FileSize += S.Relocs.size() * sizeof(coff_relocation);
    FileSize = alignTo(FileSize, FileAlignment);

    if (S.Header.Characteristics & IMAGE_SCN_CNT_INITIALIZED_DATA)
      SizeOfInitializedData += S.Header.SizeOfRawData;
  }
}

Expected<size_t> COFFWriter::finalizeStringTable() {
  for (const auto &S : Obj.getSections())
    if (S.Name.size() > COFF::NameSize)
      StrTabBuilder.add(S.Name);

  for (const auto &S : Obj.getSymbols())
    if (S.Name.size() > COFF::NameSize)
      StrTabBuilder.add(S.Name);

  StrTabBuilder.finalize();

  for (auto &S : Obj.getMutableSections()) {
    memset(S.Header.Name, 0, sizeof(S.Header.Name));
    if (S.Name.size() <= COFF::NameSize) {
      // Short names can go in the field directly.
      memcpy(S.Header.Name, S.Name.data(), S.Name.size());
    } else {
      // Offset of the section name in the string table.
      size_t Offset = StrTabBuilder.getOffset(S.Name);
      if (!COFF::encodeSectionName(S.Header.Name, Offset))
        return createStringError(object_error::invalid_section_index,
                                 "COFF string table is greater than 64GB, "
                                 "unable to encode section name offset");
    }
  }
  for (auto &S : Obj.getMutableSymbols()) {
    if (S.Name.size() > COFF::NameSize) {
      S.Sym.Name.Offset.Zeroes = 0;
      S.Sym.Name.Offset.Offset = StrTabBuilder.getOffset(S.Name);
    } else {
      strncpy(S.Sym.Name.ShortName, S.Name.data(), COFF::NameSize);
    }
  }
  return StrTabBuilder.getSize();
}

template <class SymbolTy>
std::pair<size_t, size_t> COFFWriter::finalizeSymbolTable() {
  size_t RawSymIndex = 0;
  for (auto &S : Obj.getMutableSymbols()) {
    // Symbols normally have NumberOfAuxSymbols set correctly all the time.
    // For file symbols, we need to know the output file's symbol size to be
    // able to calculate the number of slots it occupies.
    if (!S.AuxFile.empty())
      S.Sym.NumberOfAuxSymbols =
          alignTo(S.AuxFile.size(), sizeof(SymbolTy)) / sizeof(SymbolTy);
    S.RawIndex = RawSymIndex;
    RawSymIndex += 1 + S.Sym.NumberOfAuxSymbols;
  }
  return std::make_pair(RawSymIndex * sizeof(SymbolTy), sizeof(SymbolTy));
}

Error COFFWriter::finalize(bool IsBigObj) {
  size_t SymTabSize, SymbolSize;
  std::tie(SymTabSize, SymbolSize) = IsBigObj
                                         ? finalizeSymbolTable<coff_symbol32>()
                                         : finalizeSymbolTable<coff_symbol16>();

  if (Error E = finalizeRelocTargets())
    return E;
  if (Error E = finalizeSymbolContents())
    return E;

  size_t SizeOfHeaders = 0;
  FileAlignment = 1;
  size_t PeHeaderSize = 0;
  if (Obj.IsPE) {
    Obj.DosHeader.AddressOfNewExeHeader =
        sizeof(Obj.DosHeader) + Obj.DosStub.size();
    SizeOfHeaders += Obj.DosHeader.AddressOfNewExeHeader + sizeof(PEMagic);

    FileAlignment = Obj.PeHeader.FileAlignment;
    Obj.PeHeader.NumberOfRvaAndSize = Obj.DataDirectories.size();

    PeHeaderSize = Obj.Is64 ? sizeof(pe32plus_header) : sizeof(pe32_header);
    SizeOfHeaders +=
        PeHeaderSize + sizeof(data_directory) * Obj.DataDirectories.size();
  }
  Obj.CoffFileHeader.NumberOfSections = Obj.getSections().size();
  SizeOfHeaders +=
      IsBigObj ? sizeof(coff_bigobj_file_header) : sizeof(coff_file_header);
  SizeOfHeaders += sizeof(coff_section) * Obj.getSections().size();
  SizeOfHeaders = alignTo(SizeOfHeaders, FileAlignment);

  Obj.CoffFileHeader.SizeOfOptionalHeader =
      PeHeaderSize + sizeof(data_directory) * Obj.DataDirectories.size();

  FileSize = SizeOfHeaders;
  SizeOfInitializedData = 0;

  layoutSections();

  if (Obj.IsPE) {
    Obj.PeHeader.SizeOfHeaders = SizeOfHeaders;
    Obj.PeHeader.SizeOfInitializedData = SizeOfInitializedData;

    if (!Obj.getSections().empty()) {
      const Section &S = Obj.getSections().back();
      Obj.PeHeader.SizeOfImage =
          alignTo(S.Header.VirtualAddress + S.Header.VirtualSize,
                  Obj.PeHeader.SectionAlignment);
    }

    // If the PE header had a checksum, clear it, since it isn't valid
    // any longer. (We don't calculate a new one.)
    Obj.PeHeader.CheckSum = 0;
  }

  Expected<size_t> StrTabSizeOrErr = finalizeStringTable();
  if (!StrTabSizeOrErr)
    return StrTabSizeOrErr.takeError();

  size_t StrTabSize = *StrTabSizeOrErr;

  size_t PointerToSymbolTable = FileSize;
  // StrTabSize <= 4 is the size of an empty string table, only consisting
  // of the length field.
  if (SymTabSize == 0 && StrTabSize <= 4 && Obj.IsPE) {
    // For executables, don't point to the symbol table and skip writing
    // the length field, if both the symbol and string tables are empty.
    PointerToSymbolTable = 0;
    StrTabSize = 0;
  }

  size_t NumRawSymbols = SymTabSize / SymbolSize;
  Obj.CoffFileHeader.PointerToSymbolTable = PointerToSymbolTable;
  Obj.CoffFileHeader.NumberOfSymbols = NumRawSymbols;
  FileSize += SymTabSize + StrTabSize;
  FileSize = alignTo(FileSize, FileAlignment);

  return Error::success();
}

void COFFWriter::writeHeaders(bool IsBigObj) {
  uint8_t *Ptr = reinterpret_cast<uint8_t *>(Buf->getBufferStart());
  if (Obj.IsPE) {
    memcpy(Ptr, &Obj.DosHeader, sizeof(Obj.DosHeader));
    Ptr += sizeof(Obj.DosHeader);
    memcpy(Ptr, Obj.DosStub.data(), Obj.DosStub.size());
    Ptr += Obj.DosStub.size();
    memcpy(Ptr, PEMagic, sizeof(PEMagic));
    Ptr += sizeof(PEMagic);
  }
  if (!IsBigObj) {
    memcpy(Ptr, &Obj.CoffFileHeader, sizeof(Obj.CoffFileHeader));
    Ptr += sizeof(Obj.CoffFileHeader);
  } else {
    // Generate a coff_bigobj_file_header, filling it in with the values
    // from Obj.CoffFileHeader. All extra fields that don't exist in
    // coff_file_header can be set to hardcoded values.
    coff_bigobj_file_header BigObjHeader;
    BigObjHeader.Sig1 = IMAGE_FILE_MACHINE_UNKNOWN;
    BigObjHeader.Sig2 = 0xffff;
    BigObjHeader.Version = BigObjHeader::MinBigObjectVersion;
    BigObjHeader.Machine = Obj.CoffFileHeader.Machine;
    BigObjHeader.TimeDateStamp = Obj.CoffFileHeader.TimeDateStamp;
    memcpy(BigObjHeader.UUID, BigObjMagic, sizeof(BigObjMagic));
    BigObjHeader.unused1 = 0;
    BigObjHeader.unused2 = 0;
    BigObjHeader.unused3 = 0;
    BigObjHeader.unused4 = 0;
    // The value in Obj.CoffFileHeader.NumberOfSections is truncated, thus
    // get the original one instead.
    BigObjHeader.NumberOfSections = Obj.getSections().size();
    BigObjHeader.PointerToSymbolTable = Obj.CoffFileHeader.PointerToSymbolTable;
    BigObjHeader.NumberOfSymbols = Obj.CoffFileHeader.NumberOfSymbols;

    memcpy(Ptr, &BigObjHeader, sizeof(BigObjHeader));
    Ptr += sizeof(BigObjHeader);
  }
  if (Obj.IsPE) {
    if (Obj.Is64) {
      memcpy(Ptr, &Obj.PeHeader, sizeof(Obj.PeHeader));
      Ptr += sizeof(Obj.PeHeader);
    } else {
      pe32_header PeHeader;
      copyPeHeader(PeHeader, Obj.PeHeader);
      // The pe32plus_header (stored in Object) lacks the BaseOfData field.
      PeHeader.BaseOfData = Obj.BaseOfData;

      memcpy(Ptr, &PeHeader, sizeof(PeHeader));
      Ptr += sizeof(PeHeader);
    }
    for (const auto &DD : Obj.DataDirectories) {
      memcpy(Ptr, &DD, sizeof(DD));
      Ptr += sizeof(DD);
    }
  }
  for (const auto &S : Obj.getSections()) {
    memcpy(Ptr, &S.Header, sizeof(S.Header));
    Ptr += sizeof(S.Header);
  }
}

void COFFWriter::writeSections() {
  for (const auto &S : Obj.getSections()) {
    uint8_t *Ptr = reinterpret_cast<uint8_t *>(Buf->getBufferStart()) +
                   S.Header.PointerToRawData;
    ArrayRef<uint8_t> Contents = S.getContents();
    std::copy(Contents.begin(), Contents.end(), Ptr);

    // For executable sections, pad the remainder of the raw data size with
    // 0xcc, which is int3 on x86.
    if ((S.Header.Characteristics & IMAGE_SCN_CNT_CODE) &&
        S.Header.SizeOfRawData > Contents.size())
      memset(Ptr + Contents.size(), 0xcc,
             S.Header.SizeOfRawData - Contents.size());

    Ptr += S.Header.SizeOfRawData;

    if (S.Relocs.size() >= 0xffff) {
      object::coff_relocation R;
      R.VirtualAddress = S.Relocs.size() + 1;
      R.SymbolTableIndex = 0;
      R.Type = 0;
      memcpy(Ptr, &R, sizeof(R));
      Ptr += sizeof(R);
    }
    for (const auto &R : S.Relocs) {
      memcpy(Ptr, &R.Reloc, sizeof(R.Reloc));
      Ptr += sizeof(R.Reloc);
    }
  }
}

template <class SymbolTy> void COFFWriter::writeSymbolStringTables() {
  uint8_t *Ptr = reinterpret_cast<uint8_t *>(Buf->getBufferStart()) +
                 Obj.CoffFileHeader.PointerToSymbolTable;
  for (const auto &S : Obj.getSymbols()) {
    // Convert symbols back to the right size, from coff_symbol32.
    copySymbol<SymbolTy, coff_symbol32>(*reinterpret_cast<SymbolTy *>(Ptr),
                                        S.Sym);
    Ptr += sizeof(SymbolTy);
    if (!S.AuxFile.empty()) {
      // For file symbols, just write the string into the aux symbol slots,
      // assuming that the unwritten parts are initialized to zero in the memory
      // mapped file.
      std::copy(S.AuxFile.begin(), S.AuxFile.end(), Ptr);
      Ptr += S.Sym.NumberOfAuxSymbols * sizeof(SymbolTy);
    } else {
      // For other auxillary symbols, write their opaque payload into one symbol
      // table slot each. For big object files, the symbols are larger than the
      // opaque auxillary symbol struct and we leave padding at the end of each
      // entry.
      for (const AuxSymbol &AuxSym : S.AuxData) {
        ArrayRef<uint8_t> Ref = AuxSym.getRef();
        std::copy(Ref.begin(), Ref.end(), Ptr);
        Ptr += sizeof(SymbolTy);
      }
    }
  }
  if (StrTabBuilder.getSize() > 4 || !Obj.IsPE) {
    // Always write a string table in object files, even an empty one.
    StrTabBuilder.write(Ptr);
    Ptr += StrTabBuilder.getSize();
  }
}

Error COFFWriter::write(bool IsBigObj) {
  if (Error E = finalize(IsBigObj))
    return E;

  Buf = WritableMemoryBuffer::getNewMemBuffer(FileSize);
  if (!Buf)
    return createStringError(llvm::errc::not_enough_memory,
                             "failed to allocate memory buffer of " +
                                 Twine::utohexstr(FileSize) + " bytes.");

  writeHeaders(IsBigObj);
  writeSections();
  if (IsBigObj)
    writeSymbolStringTables<coff_symbol32>();
  else
    writeSymbolStringTables<coff_symbol16>();

  if (Obj.IsPE)
    if (Error E = patchDebugDirectory())
      return E;

  // TODO: Implement direct writing to the output stream (without intermediate
  // memory buffer Buf).
  Out.write(Buf->getBufferStart(), Buf->getBufferSize());
  return Error::success();
}

Expected<uint32_t> COFFWriter::virtualAddressToFileAddress(uint32_t RVA) {
  for (const auto &S : Obj.getSections()) {
    if (RVA >= S.Header.VirtualAddress &&
        RVA < S.Header.VirtualAddress + S.Header.SizeOfRawData)
      return S.Header.PointerToRawData + RVA - S.Header.VirtualAddress;
  }
  return createStringError(object_error::parse_failed,
                           "debug directory payload not found");
}

// Locate which sections contain the debug directories, iterate over all
// the debug_directory structs in there, and set the PointerToRawData field
// in all of them, according to their new physical location in the file.
Error COFFWriter::patchDebugDirectory() {
  if (Obj.DataDirectories.size() <= DEBUG_DIRECTORY)
    return Error::success();
  const data_directory *Dir = &Obj.DataDirectories[DEBUG_DIRECTORY];
  if (Dir->Size <= 0)
    return Error::success();
  for (const auto &S : Obj.getSections()) {
    if (Dir->RelativeVirtualAddress >= S.Header.VirtualAddress &&
        Dir->RelativeVirtualAddress <
            S.Header.VirtualAddress + S.Header.SizeOfRawData) {
      if (Dir->RelativeVirtualAddress + Dir->Size >
          S.Header.VirtualAddress + S.Header.SizeOfRawData)
        return createStringError(object_error::parse_failed,
                                 "debug directory extends past end of section");

      size_t Offset = Dir->RelativeVirtualAddress - S.Header.VirtualAddress;
      uint8_t *Ptr = reinterpret_cast<uint8_t *>(Buf->getBufferStart()) +
                     S.Header.PointerToRawData + Offset;
      uint8_t *End = Ptr + Dir->Size;
      while (Ptr < End) {
        debug_directory *Debug = reinterpret_cast<debug_directory *>(Ptr);
        if (Debug->PointerToRawData) {
          if (Expected<uint32_t> FilePosOrErr =
                  virtualAddressToFileAddress(Debug->AddressOfRawData))
            Debug->PointerToRawData = *FilePosOrErr;
          else
            return FilePosOrErr.takeError();
        }
        Ptr += sizeof(debug_directory);
        Offset += sizeof(debug_directory);
      }
      // Debug directory found and patched, all done.
      return Error::success();
    }
  }
  return createStringError(object_error::parse_failed,
                           "debug directory not found");
}

Error COFFWriter::write() {
  bool IsBigObj = Obj.getSections().size() > MaxNumberOfSections16;
  if (IsBigObj && Obj.IsPE)
    return createStringError(object_error::parse_failed,
                             "too many sections for executable");
  return write(IsBigObj);
}

} // end namespace coff
} // end namespace objcopy
} // end namespace llvm
