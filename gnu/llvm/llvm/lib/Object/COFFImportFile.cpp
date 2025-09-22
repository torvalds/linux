//===- COFFImportFile.cpp - COFF short import file implementation ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the writeImportLibrary function.
//
//===----------------------------------------------------------------------===//

#include "llvm/Object/COFFImportFile.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ArchiveWriter.h"
#include "llvm/Object/COFF.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Path.h"

#include <cstdint>
#include <string>
#include <vector>

using namespace llvm::COFF;
using namespace llvm::object;
using namespace llvm;

namespace llvm {
namespace object {

StringRef COFFImportFile::getFileFormatName() const {
  switch (getMachine()) {
  case COFF::IMAGE_FILE_MACHINE_I386:
    return "COFF-import-file-i386";
  case COFF::IMAGE_FILE_MACHINE_AMD64:
    return "COFF-import-file-x86-64";
  case COFF::IMAGE_FILE_MACHINE_ARMNT:
    return "COFF-import-file-ARM";
  case COFF::IMAGE_FILE_MACHINE_ARM64:
    return "COFF-import-file-ARM64";
  case COFF::IMAGE_FILE_MACHINE_ARM64EC:
    return "COFF-import-file-ARM64EC";
  case COFF::IMAGE_FILE_MACHINE_ARM64X:
    return "COFF-import-file-ARM64X";
  default:
    return "COFF-import-file-<unknown arch>";
  }
}

static StringRef applyNameType(ImportNameType Type, StringRef name) {
  auto ltrim1 = [](StringRef s, StringRef chars) {
    return !s.empty() && chars.contains(s[0]) ? s.substr(1) : s;
  };

  switch (Type) {
  case IMPORT_NAME_NOPREFIX:
    name = ltrim1(name, "?@_");
    break;
  case IMPORT_NAME_UNDECORATE:
    name = ltrim1(name, "?@_");
    name = name.substr(0, name.find('@'));
    break;
  default:
    break;
  }
  return name;
}

StringRef COFFImportFile::getExportName() const {
  const coff_import_header *hdr = getCOFFImportHeader();
  StringRef name = Data.getBuffer().substr(sizeof(*hdr)).split('\0').first;

  switch (hdr->getNameType()) {
  case IMPORT_ORDINAL:
    name = "";
    break;
  case IMPORT_NAME_NOPREFIX:
  case IMPORT_NAME_UNDECORATE:
    name = applyNameType(static_cast<ImportNameType>(hdr->getNameType()), name);
    break;
  case IMPORT_NAME_EXPORTAS: {
    // Skip DLL name
    name = Data.getBuffer().substr(sizeof(*hdr) + name.size() + 1);
    name = name.split('\0').second.split('\0').first;
    break;
  }
  default:
    break;
  }

  return name;
}

Error COFFImportFile::printSymbolName(raw_ostream &OS, DataRefImpl Symb) const {
  switch (Symb.p) {
  case ImpSymbol:
    OS << "__imp_";
    break;
  case ECAuxSymbol:
    OS << "__imp_aux_";
    break;
  }
  const char *Name = Data.getBufferStart() + sizeof(coff_import_header);
  if (Symb.p != ECThunkSymbol && COFF::isArm64EC(getMachine())) {
    if (std::optional<std::string> DemangledName =
            getArm64ECDemangledFunctionName(Name)) {
      OS << StringRef(*DemangledName);
      return Error::success();
    }
  }
  OS << StringRef(Name);
  return Error::success();
}

static uint16_t getImgRelRelocation(MachineTypes Machine) {
  switch (Machine) {
  default:
    llvm_unreachable("unsupported machine");
  case IMAGE_FILE_MACHINE_AMD64:
    return IMAGE_REL_AMD64_ADDR32NB;
  case IMAGE_FILE_MACHINE_ARMNT:
    return IMAGE_REL_ARM_ADDR32NB;
  case IMAGE_FILE_MACHINE_ARM64:
  case IMAGE_FILE_MACHINE_ARM64EC:
  case IMAGE_FILE_MACHINE_ARM64X:
    return IMAGE_REL_ARM64_ADDR32NB;
  case IMAGE_FILE_MACHINE_I386:
    return IMAGE_REL_I386_DIR32NB;
  }
}

template <class T> static void append(std::vector<uint8_t> &B, const T &Data) {
  size_t S = B.size();
  B.resize(S + sizeof(T));
  memcpy(&B[S], &Data, sizeof(T));
}

static void writeStringTable(std::vector<uint8_t> &B,
                             ArrayRef<const std::string_view> Strings) {
  // The COFF string table consists of a 4-byte value which is the size of the
  // table, including the length field itself.  This value is followed by the
  // string content itself, which is an array of null-terminated C-style
  // strings.  The termination is important as they are referenced to by offset
  // by the symbol entity in the file format.

  size_t Pos = B.size();
  size_t Offset = B.size();

  // Skip over the length field, we will fill it in later as we will have
  // computed the length while emitting the string content itself.
  Pos += sizeof(uint32_t);

  for (const auto &S : Strings) {
    B.resize(Pos + S.length() + 1);
    std::copy(S.begin(), S.end(), std::next(B.begin(), Pos));
    B[Pos + S.length()] = 0;
    Pos += S.length() + 1;
  }

  // Backfill the length of the table now that it has been computed.
  support::ulittle32_t Length(B.size() - Offset);
  support::endian::write32le(&B[Offset], Length);
}

static ImportNameType getNameType(StringRef Sym, StringRef ExtName,
                                  MachineTypes Machine, bool MinGW) {
  // A decorated stdcall function in MSVC is exported with the
  // type IMPORT_NAME, and the exported function name includes the
  // the leading underscore. In MinGW on the other hand, a decorated
  // stdcall function still omits the underscore (IMPORT_NAME_NOPREFIX).
  // See the comment in isDecorated in COFFModuleDefinition.cpp for more
  // details.
  if (ExtName.starts_with("_") && ExtName.contains('@') && !MinGW)
    return IMPORT_NAME;
  if (Sym != ExtName)
    return IMPORT_NAME_UNDECORATE;
  if (Machine == IMAGE_FILE_MACHINE_I386 && Sym.starts_with("_"))
    return IMPORT_NAME_NOPREFIX;
  return IMPORT_NAME;
}

static Expected<std::string> replace(StringRef S, StringRef From,
                                     StringRef To) {
  size_t Pos = S.find(From);

  // From and To may be mangled, but substrings in S may not.
  if (Pos == StringRef::npos && From.starts_with("_") && To.starts_with("_")) {
    From = From.substr(1);
    To = To.substr(1);
    Pos = S.find(From);
  }

  if (Pos == StringRef::npos) {
    return make_error<StringError>(
      StringRef(Twine(S + ": replacing '" + From +
        "' with '" + To + "' failed").str()), object_error::parse_failed);
  }

  return (Twine(S.substr(0, Pos)) + To + S.substr(Pos + From.size())).str();
}

namespace {
// This class constructs various small object files necessary to support linking
// symbols imported from a DLL.  The contents are pretty strictly defined and
// nearly entirely static.  The details of the structures files are defined in
// WINNT.h and the PE/COFF specification.
class ObjectFactory {
  using u16 = support::ulittle16_t;
  using u32 = support::ulittle32_t;
  MachineTypes NativeMachine;
  BumpPtrAllocator Alloc;
  StringRef ImportName;
  StringRef Library;
  std::string ImportDescriptorSymbolName;
  std::string NullThunkSymbolName;

public:
  ObjectFactory(StringRef S, MachineTypes M)
      : NativeMachine(M), ImportName(S), Library(llvm::sys::path::stem(S)),
        ImportDescriptorSymbolName((ImportDescriptorPrefix + Library).str()),
        NullThunkSymbolName(
            (NullThunkDataPrefix + Library + NullThunkDataSuffix).str()) {}

  // Creates an Import Descriptor.  This is a small object file which contains a
  // reference to the terminators and contains the library name (entry) for the
  // import name table.  It will force the linker to construct the necessary
  // structure to import symbols from the DLL.
  NewArchiveMember createImportDescriptor(std::vector<uint8_t> &Buffer);

  // Creates a NULL import descriptor.  This is a small object file whcih
  // contains a NULL import descriptor.  It is used to terminate the imports
  // from a specific DLL.
  NewArchiveMember createNullImportDescriptor(std::vector<uint8_t> &Buffer);

  // Create a NULL Thunk Entry.  This is a small object file which contains a
  // NULL Import Address Table entry and a NULL Import Lookup Table Entry.  It
  // is used to terminate the IAT and ILT.
  NewArchiveMember createNullThunk(std::vector<uint8_t> &Buffer);

  // Create a short import file which is described in PE/COFF spec 7. Import
  // Library Format.
  NewArchiveMember createShortImport(StringRef Sym, uint16_t Ordinal,
                                     ImportType Type, ImportNameType NameType,
                                     StringRef ExportName,
                                     MachineTypes Machine);

  // Create a weak external file which is described in PE/COFF Aux Format 3.
  NewArchiveMember createWeakExternal(StringRef Sym, StringRef Weak, bool Imp,
                                      MachineTypes Machine);

  bool is64Bit() const { return COFF::is64Bit(NativeMachine); }
};
} // namespace

NewArchiveMember
ObjectFactory::createImportDescriptor(std::vector<uint8_t> &Buffer) {
  const uint32_t NumberOfSections = 2;
  const uint32_t NumberOfSymbols = 7;
  const uint32_t NumberOfRelocations = 3;

  // COFF Header
  coff_file_header Header{
      u16(NativeMachine),
      u16(NumberOfSections),
      u32(0),
      u32(sizeof(Header) + (NumberOfSections * sizeof(coff_section)) +
          // .idata$2
          sizeof(coff_import_directory_table_entry) +
          NumberOfRelocations * sizeof(coff_relocation) +
          // .idata$4
          (ImportName.size() + 1)),
      u32(NumberOfSymbols),
      u16(0),
      u16(is64Bit() ? C_Invalid : IMAGE_FILE_32BIT_MACHINE),
  };
  append(Buffer, Header);

  // Section Header Table
  const coff_section SectionTable[NumberOfSections] = {
      {{'.', 'i', 'd', 'a', 't', 'a', '$', '2'},
       u32(0),
       u32(0),
       u32(sizeof(coff_import_directory_table_entry)),
       u32(sizeof(coff_file_header) + NumberOfSections * sizeof(coff_section)),
       u32(sizeof(coff_file_header) + NumberOfSections * sizeof(coff_section) +
           sizeof(coff_import_directory_table_entry)),
       u32(0),
       u16(NumberOfRelocations),
       u16(0),
       u32(IMAGE_SCN_ALIGN_4BYTES | IMAGE_SCN_CNT_INITIALIZED_DATA |
           IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE)},
      {{'.', 'i', 'd', 'a', 't', 'a', '$', '6'},
       u32(0),
       u32(0),
       u32(ImportName.size() + 1),
       u32(sizeof(coff_file_header) + NumberOfSections * sizeof(coff_section) +
           sizeof(coff_import_directory_table_entry) +
           NumberOfRelocations * sizeof(coff_relocation)),
       u32(0),
       u32(0),
       u16(0),
       u16(0),
       u32(IMAGE_SCN_ALIGN_2BYTES | IMAGE_SCN_CNT_INITIALIZED_DATA |
           IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE)},
  };
  append(Buffer, SectionTable);

  // .idata$2
  const coff_import_directory_table_entry ImportDescriptor{
      u32(0), u32(0), u32(0), u32(0), u32(0),
  };
  append(Buffer, ImportDescriptor);

  const coff_relocation RelocationTable[NumberOfRelocations] = {
      {u32(offsetof(coff_import_directory_table_entry, NameRVA)), u32(2),
       u16(getImgRelRelocation(NativeMachine))},
      {u32(offsetof(coff_import_directory_table_entry, ImportLookupTableRVA)),
       u32(3), u16(getImgRelRelocation(NativeMachine))},
      {u32(offsetof(coff_import_directory_table_entry, ImportAddressTableRVA)),
       u32(4), u16(getImgRelRelocation(NativeMachine))},
  };
  append(Buffer, RelocationTable);

  // .idata$6
  auto S = Buffer.size();
  Buffer.resize(S + ImportName.size() + 1);
  memcpy(&Buffer[S], ImportName.data(), ImportName.size());
  Buffer[S + ImportName.size()] = '\0';

  // Symbol Table
  coff_symbol16 SymbolTable[NumberOfSymbols] = {
      {{{0, 0, 0, 0, 0, 0, 0, 0}},
       u32(0),
       u16(1),
       u16(0),
       IMAGE_SYM_CLASS_EXTERNAL,
       0},
      {{{'.', 'i', 'd', 'a', 't', 'a', '$', '2'}},
       u32(0),
       u16(1),
       u16(0),
       IMAGE_SYM_CLASS_SECTION,
       0},
      {{{'.', 'i', 'd', 'a', 't', 'a', '$', '6'}},
       u32(0),
       u16(2),
       u16(0),
       IMAGE_SYM_CLASS_STATIC,
       0},
      {{{'.', 'i', 'd', 'a', 't', 'a', '$', '4'}},
       u32(0),
       u16(0),
       u16(0),
       IMAGE_SYM_CLASS_SECTION,
       0},
      {{{'.', 'i', 'd', 'a', 't', 'a', '$', '5'}},
       u32(0),
       u16(0),
       u16(0),
       IMAGE_SYM_CLASS_SECTION,
       0},
      {{{0, 0, 0, 0, 0, 0, 0, 0}},
       u32(0),
       u16(0),
       u16(0),
       IMAGE_SYM_CLASS_EXTERNAL,
       0},
      {{{0, 0, 0, 0, 0, 0, 0, 0}},
       u32(0),
       u16(0),
       u16(0),
       IMAGE_SYM_CLASS_EXTERNAL,
       0},
  };
  // TODO: Name.Offset.Offset here and in the all similar places below
  // suggests a names refactoring. Maybe StringTableOffset.Value?
  SymbolTable[0].Name.Offset.Offset =
      sizeof(uint32_t);
  SymbolTable[5].Name.Offset.Offset =
      sizeof(uint32_t) + ImportDescriptorSymbolName.length() + 1;
  SymbolTable[6].Name.Offset.Offset =
      sizeof(uint32_t) + ImportDescriptorSymbolName.length() + 1 +
      NullImportDescriptorSymbolName.length() + 1;
  append(Buffer, SymbolTable);

  // String Table
  writeStringTable(Buffer,
                   {ImportDescriptorSymbolName, NullImportDescriptorSymbolName,
                    NullThunkSymbolName});

  StringRef F{reinterpret_cast<const char *>(Buffer.data()), Buffer.size()};
  return {MemoryBufferRef(F, ImportName)};
}

NewArchiveMember
ObjectFactory::createNullImportDescriptor(std::vector<uint8_t> &Buffer) {
  const uint32_t NumberOfSections = 1;
  const uint32_t NumberOfSymbols = 1;

  // COFF Header
  coff_file_header Header{
      u16(NativeMachine),
      u16(NumberOfSections),
      u32(0),
      u32(sizeof(Header) + (NumberOfSections * sizeof(coff_section)) +
          // .idata$3
          sizeof(coff_import_directory_table_entry)),
      u32(NumberOfSymbols),
      u16(0),
      u16(is64Bit() ? C_Invalid : IMAGE_FILE_32BIT_MACHINE),
  };
  append(Buffer, Header);

  // Section Header Table
  const coff_section SectionTable[NumberOfSections] = {
      {{'.', 'i', 'd', 'a', 't', 'a', '$', '3'},
       u32(0),
       u32(0),
       u32(sizeof(coff_import_directory_table_entry)),
       u32(sizeof(coff_file_header) +
           (NumberOfSections * sizeof(coff_section))),
       u32(0),
       u32(0),
       u16(0),
       u16(0),
       u32(IMAGE_SCN_ALIGN_4BYTES | IMAGE_SCN_CNT_INITIALIZED_DATA |
           IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE)},
  };
  append(Buffer, SectionTable);

  // .idata$3
  const coff_import_directory_table_entry ImportDescriptor{
      u32(0), u32(0), u32(0), u32(0), u32(0),
  };
  append(Buffer, ImportDescriptor);

  // Symbol Table
  coff_symbol16 SymbolTable[NumberOfSymbols] = {
      {{{0, 0, 0, 0, 0, 0, 0, 0}},
       u32(0),
       u16(1),
       u16(0),
       IMAGE_SYM_CLASS_EXTERNAL,
       0},
  };
  SymbolTable[0].Name.Offset.Offset = sizeof(uint32_t);
  append(Buffer, SymbolTable);

  // String Table
  writeStringTable(Buffer, {NullImportDescriptorSymbolName});

  StringRef F{reinterpret_cast<const char *>(Buffer.data()), Buffer.size()};
  return {MemoryBufferRef(F, ImportName)};
}

NewArchiveMember ObjectFactory::createNullThunk(std::vector<uint8_t> &Buffer) {
  const uint32_t NumberOfSections = 2;
  const uint32_t NumberOfSymbols = 1;
  uint32_t VASize = is64Bit() ? 8 : 4;

  // COFF Header
  coff_file_header Header{
      u16(NativeMachine),
      u16(NumberOfSections),
      u32(0),
      u32(sizeof(Header) + (NumberOfSections * sizeof(coff_section)) +
          // .idata$5
          VASize +
          // .idata$4
          VASize),
      u32(NumberOfSymbols),
      u16(0),
      u16(is64Bit() ? C_Invalid : IMAGE_FILE_32BIT_MACHINE),
  };
  append(Buffer, Header);

  // Section Header Table
  const coff_section SectionTable[NumberOfSections] = {
      {{'.', 'i', 'd', 'a', 't', 'a', '$', '5'},
       u32(0),
       u32(0),
       u32(VASize),
       u32(sizeof(coff_file_header) + NumberOfSections * sizeof(coff_section)),
       u32(0),
       u32(0),
       u16(0),
       u16(0),
       u32((is64Bit() ? IMAGE_SCN_ALIGN_8BYTES : IMAGE_SCN_ALIGN_4BYTES) |
           IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ |
           IMAGE_SCN_MEM_WRITE)},
      {{'.', 'i', 'd', 'a', 't', 'a', '$', '4'},
       u32(0),
       u32(0),
       u32(VASize),
       u32(sizeof(coff_file_header) + NumberOfSections * sizeof(coff_section) +
           VASize),
       u32(0),
       u32(0),
       u16(0),
       u16(0),
       u32((is64Bit() ? IMAGE_SCN_ALIGN_8BYTES : IMAGE_SCN_ALIGN_4BYTES) |
           IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ |
           IMAGE_SCN_MEM_WRITE)},
  };
  append(Buffer, SectionTable);

  // .idata$5, ILT
  append(Buffer, u32(0));
  if (is64Bit())
    append(Buffer, u32(0));

  // .idata$4, IAT
  append(Buffer, u32(0));
  if (is64Bit())
    append(Buffer, u32(0));

  // Symbol Table
  coff_symbol16 SymbolTable[NumberOfSymbols] = {
      {{{0, 0, 0, 0, 0, 0, 0, 0}},
       u32(0),
       u16(1),
       u16(0),
       IMAGE_SYM_CLASS_EXTERNAL,
       0},
  };
  SymbolTable[0].Name.Offset.Offset = sizeof(uint32_t);
  append(Buffer, SymbolTable);

  // String Table
  writeStringTable(Buffer, {NullThunkSymbolName});

  StringRef F{reinterpret_cast<const char *>(Buffer.data()), Buffer.size()};
  return {MemoryBufferRef{F, ImportName}};
}

NewArchiveMember
ObjectFactory::createShortImport(StringRef Sym, uint16_t Ordinal,
                                 ImportType ImportType, ImportNameType NameType,
                                 StringRef ExportName, MachineTypes Machine) {
  size_t ImpSize = ImportName.size() + Sym.size() + 2; // +2 for NULs
  if (!ExportName.empty())
    ImpSize += ExportName.size() + 1;
  size_t Size = sizeof(coff_import_header) + ImpSize;
  char *Buf = Alloc.Allocate<char>(Size);
  memset(Buf, 0, Size);
  char *P = Buf;

  // Write short import library.
  auto *Imp = reinterpret_cast<coff_import_header *>(P);
  P += sizeof(*Imp);
  Imp->Sig2 = 0xFFFF;
  Imp->Machine = Machine;
  Imp->SizeOfData = ImpSize;
  if (Ordinal > 0)
    Imp->OrdinalHint = Ordinal;
  Imp->TypeInfo = (NameType << 2) | ImportType;

  // Write symbol name and DLL name.
  memcpy(P, Sym.data(), Sym.size());
  P += Sym.size() + 1;
  memcpy(P, ImportName.data(), ImportName.size());
  if (!ExportName.empty()) {
    P += ImportName.size() + 1;
    memcpy(P, ExportName.data(), ExportName.size());
  }

  return {MemoryBufferRef(StringRef(Buf, Size), ImportName)};
}

NewArchiveMember ObjectFactory::createWeakExternal(StringRef Sym,
                                                   StringRef Weak, bool Imp,
                                                   MachineTypes Machine) {
  std::vector<uint8_t> Buffer;
  const uint32_t NumberOfSections = 1;
  const uint32_t NumberOfSymbols = 5;

  // COFF Header
  coff_file_header Header{
      u16(Machine),
      u16(NumberOfSections),
      u32(0),
      u32(sizeof(Header) + (NumberOfSections * sizeof(coff_section))),
      u32(NumberOfSymbols),
      u16(0),
      u16(0),
  };
  append(Buffer, Header);

  // Section Header Table
  const coff_section SectionTable[NumberOfSections] = {
      {{'.', 'd', 'r', 'e', 'c', 't', 'v', 'e'},
       u32(0),
       u32(0),
       u32(0),
       u32(0),
       u32(0),
       u32(0),
       u16(0),
       u16(0),
       u32(IMAGE_SCN_LNK_INFO | IMAGE_SCN_LNK_REMOVE)}};
  append(Buffer, SectionTable);

  // Symbol Table
  coff_symbol16 SymbolTable[NumberOfSymbols] = {
      {{{'@', 'c', 'o', 'm', 'p', '.', 'i', 'd'}},
       u32(0),
       u16(0xFFFF),
       u16(0),
       IMAGE_SYM_CLASS_STATIC,
       0},
      {{{'@', 'f', 'e', 'a', 't', '.', '0', '0'}},
       u32(0),
       u16(0xFFFF),
       u16(0),
       IMAGE_SYM_CLASS_STATIC,
       0},
      {{{0, 0, 0, 0, 0, 0, 0, 0}},
       u32(0),
       u16(0),
       u16(0),
       IMAGE_SYM_CLASS_EXTERNAL,
       0},
      {{{0, 0, 0, 0, 0, 0, 0, 0}},
       u32(0),
       u16(0),
       u16(0),
       IMAGE_SYM_CLASS_WEAK_EXTERNAL,
       1},
      {{{2, 0, 0, 0, IMAGE_WEAK_EXTERN_SEARCH_ALIAS, 0, 0, 0}},
       u32(0),
       u16(0),
       u16(0),
       IMAGE_SYM_CLASS_NULL,
       0},
  };
  SymbolTable[2].Name.Offset.Offset = sizeof(uint32_t);

  //__imp_ String Table
  StringRef Prefix = Imp ? "__imp_" : "";
  SymbolTable[3].Name.Offset.Offset =
      sizeof(uint32_t) + Sym.size() + Prefix.size() + 1;
  append(Buffer, SymbolTable);
  writeStringTable(Buffer, {(Prefix + Sym).str(),
                            (Prefix + Weak).str()});

  // Copied here so we can still use writeStringTable
  char *Buf = Alloc.Allocate<char>(Buffer.size());
  memcpy(Buf, Buffer.data(), Buffer.size());
  return {MemoryBufferRef(StringRef(Buf, Buffer.size()), ImportName)};
}

Error writeImportLibrary(StringRef ImportName, StringRef Path,
                         ArrayRef<COFFShortExport> Exports,
                         MachineTypes Machine, bool MinGW,
                         ArrayRef<COFFShortExport> NativeExports) {

  MachineTypes NativeMachine = Machine;
  if (isArm64EC(Machine)) {
    NativeMachine = IMAGE_FILE_MACHINE_ARM64;
    Machine = IMAGE_FILE_MACHINE_ARM64EC;
  }

  std::vector<NewArchiveMember> Members;
  ObjectFactory OF(llvm::sys::path::filename(ImportName), NativeMachine);

  std::vector<uint8_t> ImportDescriptor;
  Members.push_back(OF.createImportDescriptor(ImportDescriptor));

  std::vector<uint8_t> NullImportDescriptor;
  Members.push_back(OF.createNullImportDescriptor(NullImportDescriptor));

  std::vector<uint8_t> NullThunk;
  Members.push_back(OF.createNullThunk(NullThunk));

  auto addExports = [&](ArrayRef<COFFShortExport> Exp,
                        MachineTypes M) -> Error {
    StringMap<std::string> RegularImports;
    struct Deferred {
      std::string Name;
      ImportType ImpType;
      const COFFShortExport *Export;
    };
    SmallVector<Deferred, 0> Renames;
    for (const COFFShortExport &E : Exp) {
      if (E.Private)
        continue;

      ImportType ImportType = IMPORT_CODE;
      if (E.Data)
        ImportType = IMPORT_DATA;
      if (E.Constant)
        ImportType = IMPORT_CONST;

      StringRef SymbolName = E.SymbolName.empty() ? E.Name : E.SymbolName;
      std::string Name;

      if (E.ExtName.empty()) {
        Name = std::string(SymbolName);
      } else {
        Expected<std::string> ReplacedName =
            replace(SymbolName, E.Name, E.ExtName);
        if (!ReplacedName)
          return ReplacedName.takeError();
        Name.swap(*ReplacedName);
      }

      ImportNameType NameType;
      std::string ExportName;
      if (E.Noname) {
        NameType = IMPORT_ORDINAL;
      } else if (!E.ExportAs.empty()) {
        NameType = IMPORT_NAME_EXPORTAS;
        ExportName = E.ExportAs;
      } else if (!E.ImportName.empty()) {
        // If we need to import from a specific ImportName, we may need to use
        // a weak alias (which needs another import to point at). But if we can
        // express ImportName based on the symbol name and a specific NameType,
        // prefer that over an alias.
        if (Machine == IMAGE_FILE_MACHINE_I386 &&
            applyNameType(IMPORT_NAME_UNDECORATE, Name) == E.ImportName)
          NameType = IMPORT_NAME_UNDECORATE;
        else if (Machine == IMAGE_FILE_MACHINE_I386 &&
                 applyNameType(IMPORT_NAME_NOPREFIX, Name) == E.ImportName)
          NameType = IMPORT_NAME_NOPREFIX;
        else if (isArm64EC(M)) {
          NameType = IMPORT_NAME_EXPORTAS;
          ExportName = E.ImportName;
        } else if (Name == E.ImportName)
          NameType = IMPORT_NAME;
        else {
          Deferred D;
          D.Name = Name;
          D.ImpType = ImportType;
          D.Export = &E;
          Renames.push_back(D);
          continue;
        }
      } else {
        NameType = getNameType(SymbolName, E.Name, M, MinGW);
      }

      // On ARM64EC, use EXPORTAS to import demangled name for mangled symbols.
      if (ImportType == IMPORT_CODE && isArm64EC(M)) {
        if (std::optional<std::string> MangledName =
                getArm64ECMangledFunctionName(Name)) {
          if (!E.Noname && ExportName.empty()) {
            NameType = IMPORT_NAME_EXPORTAS;
            ExportName.swap(Name);
          }
          Name = std::move(*MangledName);
        } else if (!E.Noname && ExportName.empty()) {
          NameType = IMPORT_NAME_EXPORTAS;
          ExportName = std::move(*getArm64ECDemangledFunctionName(Name));
        }
      }

      RegularImports[applyNameType(NameType, Name)] = Name;
      Members.push_back(OF.createShortImport(Name, E.Ordinal, ImportType,
                                             NameType, ExportName, M));
    }
    for (const auto &D : Renames) {
      auto It = RegularImports.find(D.Export->ImportName);
      if (It != RegularImports.end()) {
        // We have a regular import entry for a symbol with the name we
        // want to reference; produce an alias pointing at that.
        StringRef Symbol = It->second;
        if (D.ImpType == IMPORT_CODE)
          Members.push_back(OF.createWeakExternal(Symbol, D.Name, false, M));
        Members.push_back(OF.createWeakExternal(Symbol, D.Name, true, M));
      } else {
        Members.push_back(OF.createShortImport(D.Name, D.Export->Ordinal,
                                               D.ImpType, IMPORT_NAME_EXPORTAS,
                                               D.Export->ImportName, M));
      }
    }
    return Error::success();
  };

  if (Error e = addExports(Exports, Machine))
    return e;
  if (Error e = addExports(NativeExports, NativeMachine))
    return e;

  return writeArchive(Path, Members, SymtabWritingMode::NormalSymtab,
                      object::Archive::K_COFF,
                      /*Deterministic*/ true, /*Thin*/ false,
                      /*OldArchiveBuf*/ nullptr, isArm64EC(Machine));
}

} // namespace object
} // namespace llvm
