//===-- COFFDump.cpp - COFF-specific dumper ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements the COFF-specific dumper for llvm-objdump.
/// It outputs the Win64 EH data structures as plain text.
/// The encoding of the unwind codes is described in MSDN:
/// https://docs.microsoft.com/en-us/cpp/build/exception-handling-x64
///
//===----------------------------------------------------------------------===//

#include "COFFDump.h"

#include "llvm-objdump.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/COFFImportFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Win64EH.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::objdump;
using namespace llvm::object;
using namespace llvm::Win64EH;

namespace {
template <typename T> struct EnumEntry {
  T Value;
  StringRef Name;
};

class COFFDumper : public Dumper {
public:
  explicit COFFDumper(const llvm::object::COFFObjectFile &O)
      : Dumper(O), Obj(O) {
    Is64 = !Obj.getPE32Header();
  }

  template <class PEHeader> void printPEHeader(const PEHeader &Hdr) const;
  void printPrivateHeaders() override;

private:
  template <typename T> FormattedNumber formatAddr(T V) const {
    return format_hex_no_prefix(V, Is64 ? 16 : 8);
  }

  uint32_t getBaseOfData(const void *Hdr) const {
    return Is64 ? 0 : static_cast<const pe32_header *>(Hdr)->BaseOfData;
  }

  const llvm::object::COFFObjectFile &Obj;
  bool Is64;
};
} // namespace

std::unique_ptr<Dumper>
objdump::createCOFFDumper(const object::COFFObjectFile &Obj) {
  return std::make_unique<COFFDumper>(Obj);
}

constexpr EnumEntry<uint16_t> PEHeaderMagic[] = {
    {uint16_t(COFF::PE32Header::PE32), "PE32"},
    {uint16_t(COFF::PE32Header::PE32_PLUS), "PE32+"},
};

constexpr EnumEntry<COFF::WindowsSubsystem> PEWindowsSubsystem[] = {
    {COFF::IMAGE_SUBSYSTEM_UNKNOWN, "unspecified"},
    {COFF::IMAGE_SUBSYSTEM_NATIVE, "NT native"},
    {COFF::IMAGE_SUBSYSTEM_WINDOWS_GUI, "Windows GUI"},
    {COFF::IMAGE_SUBSYSTEM_WINDOWS_CUI, "Windows CUI"},
    {COFF::IMAGE_SUBSYSTEM_POSIX_CUI, "POSIX CUI"},
    {COFF::IMAGE_SUBSYSTEM_WINDOWS_CE_GUI, "Wince CUI"},
    {COFF::IMAGE_SUBSYSTEM_EFI_APPLICATION, "EFI application"},
    {COFF::IMAGE_SUBSYSTEM_EFI_BOOT_SERVICE_DRIVER, "EFI boot service driver"},
    {COFF::IMAGE_SUBSYSTEM_EFI_RUNTIME_DRIVER, "EFI runtime driver"},
    {COFF::IMAGE_SUBSYSTEM_EFI_ROM, "SAL runtime driver"},
    {COFF::IMAGE_SUBSYSTEM_XBOX, "XBOX"},
};

template <typename T, typename TEnum>
static void printOptionalEnumName(T Value,
                                  ArrayRef<EnumEntry<TEnum>> EnumValues) {
  for (const EnumEntry<TEnum> &I : EnumValues)
    if (I.Value == Value) {
      outs() << "\t(" << I.Name << ')';
      return;
    }
}

template <class PEHeader>
void COFFDumper::printPEHeader(const PEHeader &Hdr) const {
  auto print = [](const char *K, auto V, const char *Fmt = "%d\n") {
    outs() << format("%-23s ", K) << format(Fmt, V);
  };
  auto printU16 = [&](const char *K, support::ulittle16_t V,
                      const char *Fmt = "%d\n") { print(K, uint16_t(V), Fmt); };
  auto printU32 = [&](const char *K, support::ulittle32_t V,
                      const char *Fmt = "%d\n") { print(K, uint32_t(V), Fmt); };
  auto printAddr = [=](const char *K, uint64_t V) {
    outs() << format("%-23s ", K) << formatAddr(V) << '\n';
  };

  printU16("Magic", Hdr.Magic, "%04x");
  printOptionalEnumName(Hdr.Magic, ArrayRef(PEHeaderMagic));
  outs() << '\n';
  print("MajorLinkerVersion", Hdr.MajorLinkerVersion);
  print("MinorLinkerVersion", Hdr.MinorLinkerVersion);
  printAddr("SizeOfCode", Hdr.SizeOfCode);
  printAddr("SizeOfInitializedData", Hdr.SizeOfInitializedData);
  printAddr("SizeOfUninitializedData", Hdr.SizeOfUninitializedData);
  printAddr("AddressOfEntryPoint", Hdr.AddressOfEntryPoint);
  printAddr("BaseOfCode", Hdr.BaseOfCode);
  if (!Is64)
    printAddr("BaseOfData", getBaseOfData(&Hdr));
  printAddr("ImageBase", Hdr.ImageBase);
  printU32("SectionAlignment", Hdr.SectionAlignment, "%08x\n");
  printU32("FileAlignment", Hdr.FileAlignment, "%08x\n");
  printU16("MajorOSystemVersion", Hdr.MajorOperatingSystemVersion);
  printU16("MinorOSystemVersion", Hdr.MinorOperatingSystemVersion);
  printU16("MajorImageVersion", Hdr.MajorImageVersion);
  printU16("MinorImageVersion", Hdr.MinorImageVersion);
  printU16("MajorSubsystemVersion", Hdr.MajorSubsystemVersion);
  printU16("MinorSubsystemVersion", Hdr.MinorSubsystemVersion);
  printU32("Win32Version", Hdr.Win32VersionValue, "%08x\n");
  printU32("SizeOfImage", Hdr.SizeOfImage, "%08x\n");
  printU32("SizeOfHeaders", Hdr.SizeOfHeaders, "%08x\n");
  printU32("CheckSum", Hdr.CheckSum, "%08x\n");
  printU16("Subsystem", Hdr.Subsystem, "%08x");
  printOptionalEnumName(Hdr.Subsystem, ArrayRef(PEWindowsSubsystem));
  outs() << '\n';

  printU16("DllCharacteristics", Hdr.DLLCharacteristics, "%08x\n");
#define FLAG(Name)                                                             \
  if (Hdr.DLLCharacteristics & COFF::IMAGE_DLL_CHARACTERISTICS_##Name)         \
    outs() << "\t\t\t\t\t" << #Name << '\n';
  FLAG(HIGH_ENTROPY_VA);
  FLAG(DYNAMIC_BASE);
  FLAG(FORCE_INTEGRITY);
  FLAG(NX_COMPAT);
  FLAG(NO_ISOLATION);
  FLAG(NO_SEH);
  FLAG(NO_BIND);
  FLAG(APPCONTAINER);
  FLAG(WDM_DRIVER);
  FLAG(GUARD_CF);
  FLAG(TERMINAL_SERVER_AWARE);
#undef FLAG

  printAddr("SizeOfStackReserve", Hdr.SizeOfStackReserve);
  printAddr("SizeOfStackCommit", Hdr.SizeOfStackCommit);
  printAddr("SizeOfHeapReserve", Hdr.SizeOfHeapReserve);
  printAddr("SizeOfHeapCommit", Hdr.SizeOfHeapCommit);
  printU32("LoaderFlags", Hdr.LoaderFlags, "%08x\n");
  printU32("NumberOfRvaAndSizes", Hdr.NumberOfRvaAndSize, "%08x\n");

  static const char *DirName[COFF::NUM_DATA_DIRECTORIES + 1] = {
      "Export Directory [.edata (or where ever we found it)]",
      "Import Directory [parts of .idata]",
      "Resource Directory [.rsrc]",
      "Exception Directory [.pdata]",
      "Security Directory",
      "Base Relocation Directory [.reloc]",
      "Debug Directory",
      "Description Directory",
      "Special Directory",
      "Thread Storage Directory [.tls]",
      "Load Configuration Directory",
      "Bound Import Directory",
      "Import Address Table Directory",
      "Delay Import Directory",
      "CLR Runtime Header",
      "Reserved",
  };
  outs() << "\nThe Data Directory\n";
  for (uint32_t I = 0; I != std::size(DirName); ++I) {
    uint32_t Addr = 0, Size = 0;
    if (const data_directory *Data = Obj.getDataDirectory(I)) {
      Addr = Data->RelativeVirtualAddress;
      Size = Data->Size;
    }
    outs() << format("Entry %x ", I) << formatAddr(Addr)
           << format(" %08x %s\n", uint32_t(Size), DirName[I]);
  }
}

// Returns the name of the unwind code.
static StringRef getUnwindCodeTypeName(uint8_t Code) {
  switch(Code) {
  default: llvm_unreachable("Invalid unwind code");
  case UOP_PushNonVol: return "UOP_PushNonVol";
  case UOP_AllocLarge: return "UOP_AllocLarge";
  case UOP_AllocSmall: return "UOP_AllocSmall";
  case UOP_SetFPReg: return "UOP_SetFPReg";
  case UOP_SaveNonVol: return "UOP_SaveNonVol";
  case UOP_SaveNonVolBig: return "UOP_SaveNonVolBig";
  case UOP_Epilog: return "UOP_Epilog";
  case UOP_SpareCode: return "UOP_SpareCode";
  case UOP_SaveXMM128: return "UOP_SaveXMM128";
  case UOP_SaveXMM128Big: return "UOP_SaveXMM128Big";
  case UOP_PushMachFrame: return "UOP_PushMachFrame";
  }
}

// Returns the name of a referenced register.
static StringRef getUnwindRegisterName(uint8_t Reg) {
  switch(Reg) {
  default: llvm_unreachable("Invalid register");
  case 0: return "RAX";
  case 1: return "RCX";
  case 2: return "RDX";
  case 3: return "RBX";
  case 4: return "RSP";
  case 5: return "RBP";
  case 6: return "RSI";
  case 7: return "RDI";
  case 8: return "R8";
  case 9: return "R9";
  case 10: return "R10";
  case 11: return "R11";
  case 12: return "R12";
  case 13: return "R13";
  case 14: return "R14";
  case 15: return "R15";
  }
}

// Calculates the number of array slots required for the unwind code.
static unsigned getNumUsedSlots(const UnwindCode &UnwindCode) {
  switch (UnwindCode.getUnwindOp()) {
  default: llvm_unreachable("Invalid unwind code");
  case UOP_PushNonVol:
  case UOP_AllocSmall:
  case UOP_SetFPReg:
  case UOP_PushMachFrame:
    return 1;
  case UOP_SaveNonVol:
  case UOP_SaveXMM128:
  case UOP_Epilog:
    return 2;
  case UOP_SaveNonVolBig:
  case UOP_SaveXMM128Big:
  case UOP_SpareCode:
    return 3;
  case UOP_AllocLarge:
    return (UnwindCode.getOpInfo() == 0) ? 2 : 3;
  }
}

// Prints one unwind code. Because an unwind code can occupy up to 3 slots in
// the unwind codes array, this function requires that the correct number of
// slots is provided.
static void printUnwindCode(ArrayRef<UnwindCode> UCs) {
  assert(UCs.size() >= getNumUsedSlots(UCs[0]));
  outs() <<  format("      0x%02x: ", unsigned(UCs[0].u.CodeOffset))
         << getUnwindCodeTypeName(UCs[0].getUnwindOp());
  switch (UCs[0].getUnwindOp()) {
  case UOP_PushNonVol:
    outs() << " " << getUnwindRegisterName(UCs[0].getOpInfo());
    break;
  case UOP_AllocLarge:
    if (UCs[0].getOpInfo() == 0) {
      outs() << " " << UCs[1].FrameOffset;
    } else {
      outs() << " " << UCs[1].FrameOffset
                       + (static_cast<uint32_t>(UCs[2].FrameOffset) << 16);
    }
    break;
  case UOP_AllocSmall:
    outs() << " " << ((UCs[0].getOpInfo() + 1) * 8);
    break;
  case UOP_SetFPReg:
    outs() << " ";
    break;
  case UOP_SaveNonVol:
    outs() << " " << getUnwindRegisterName(UCs[0].getOpInfo())
           << format(" [0x%04x]", 8 * UCs[1].FrameOffset);
    break;
  case UOP_SaveNonVolBig:
    outs() << " " << getUnwindRegisterName(UCs[0].getOpInfo())
           << format(" [0x%08x]", UCs[1].FrameOffset
                    + (static_cast<uint32_t>(UCs[2].FrameOffset) << 16));
    break;
  case UOP_SaveXMM128:
    outs() << " XMM" << static_cast<uint32_t>(UCs[0].getOpInfo())
           << format(" [0x%04x]", 16 * UCs[1].FrameOffset);
    break;
  case UOP_SaveXMM128Big:
    outs() << " XMM" << UCs[0].getOpInfo()
           << format(" [0x%08x]", UCs[1].FrameOffset
                           + (static_cast<uint32_t>(UCs[2].FrameOffset) << 16));
    break;
  case UOP_PushMachFrame:
    outs() << " " << (UCs[0].getOpInfo() ? "w/o" : "w")
           << " error code";
    break;
  }
  outs() << "\n";
}

static void printAllUnwindCodes(ArrayRef<UnwindCode> UCs) {
  for (const UnwindCode *I = UCs.begin(), *E = UCs.end(); I < E; ) {
    unsigned UsedSlots = getNumUsedSlots(*I);
    if (UsedSlots > UCs.size()) {
      outs() << "Unwind data corrupted: Encountered unwind op "
             << getUnwindCodeTypeName((*I).getUnwindOp())
             << " which requires " << UsedSlots
             << " slots, but only " << UCs.size()
             << " remaining in buffer";
      return ;
    }
    printUnwindCode(ArrayRef(I, E));
    I += UsedSlots;
  }
}

// Given a symbol sym this functions returns the address and section of it.
static Error resolveSectionAndAddress(const COFFObjectFile *Obj,
                                      const SymbolRef &Sym,
                                      const coff_section *&ResolvedSection,
                                      uint64_t &ResolvedAddr) {
  Expected<uint64_t> ResolvedAddrOrErr = Sym.getAddress();
  if (!ResolvedAddrOrErr)
    return ResolvedAddrOrErr.takeError();
  ResolvedAddr = *ResolvedAddrOrErr;
  Expected<section_iterator> Iter = Sym.getSection();
  if (!Iter)
    return Iter.takeError();
  ResolvedSection = Obj->getCOFFSection(**Iter);
  return Error::success();
}

// Given a vector of relocations for a section and an offset into this section
// the function returns the symbol used for the relocation at the offset.
static Error resolveSymbol(const std::vector<RelocationRef> &Rels,
                                     uint64_t Offset, SymbolRef &Sym) {
  for (auto &R : Rels) {
    uint64_t Ofs = R.getOffset();
    if (Ofs == Offset) {
      Sym = *R.getSymbol();
      return Error::success();
    }
  }
  return make_error<BinaryError>();
}

// Given a vector of relocations for a section and an offset into this section
// the function resolves the symbol used for the relocation at the offset and
// returns the section content and the address inside the content pointed to
// by the symbol.
static Error
getSectionContents(const COFFObjectFile *Obj,
                   const std::vector<RelocationRef> &Rels, uint64_t Offset,
                   ArrayRef<uint8_t> &Contents, uint64_t &Addr) {
  SymbolRef Sym;
  if (Error E = resolveSymbol(Rels, Offset, Sym))
    return E;
  const coff_section *Section;
  if (Error E = resolveSectionAndAddress(Obj, Sym, Section, Addr))
    return E;
  return Obj->getSectionContents(Section, Contents);
}

// Given a vector of relocations for a section and an offset into this section
// the function returns the name of the symbol used for the relocation at the
// offset.
static Error resolveSymbolName(const std::vector<RelocationRef> &Rels,
                               uint64_t Offset, StringRef &Name) {
  SymbolRef Sym;
  if (Error EC = resolveSymbol(Rels, Offset, Sym))
    return EC;
  Expected<StringRef> NameOrErr = Sym.getName();
  if (!NameOrErr)
    return NameOrErr.takeError();
  Name = *NameOrErr;
  return Error::success();
}

static void printCOFFSymbolAddress(raw_ostream &Out,
                                   const std::vector<RelocationRef> &Rels,
                                   uint64_t Offset, uint32_t Disp) {
  StringRef Sym;
  if (!resolveSymbolName(Rels, Offset, Sym)) {
    Out << Sym;
    if (Disp > 0)
      Out << format(" + 0x%04x", Disp);
  } else {
    Out << format("0x%04x", Disp);
  }
}

static void
printSEHTable(const COFFObjectFile *Obj, uint32_t TableVA, int Count) {
  if (Count == 0)
    return;

  uintptr_t IntPtr = 0;
  if (Error E = Obj->getVaPtr(TableVA, IntPtr))
    reportError(std::move(E), Obj->getFileName());

  const support::ulittle32_t *P = (const support::ulittle32_t *)IntPtr;
  outs() << "SEH Table:";
  for (int I = 0; I < Count; ++I)
    outs() << format(" 0x%x", P[I] + Obj->getPE32Header()->ImageBase);
  outs() << "\n\n";
}

template <typename T>
static void printTLSDirectoryT(const coff_tls_directory<T> *TLSDir) {
  size_t FormatWidth = sizeof(T) * 2;
  outs() << "TLS directory:"
         << "\n  StartAddressOfRawData: "
         << format_hex(TLSDir->StartAddressOfRawData, FormatWidth)
         << "\n  EndAddressOfRawData: "
         << format_hex(TLSDir->EndAddressOfRawData, FormatWidth)
         << "\n  AddressOfIndex: "
         << format_hex(TLSDir->AddressOfIndex, FormatWidth)
         << "\n  AddressOfCallBacks: "
         << format_hex(TLSDir->AddressOfCallBacks, FormatWidth)
         << "\n  SizeOfZeroFill: "
         << TLSDir->SizeOfZeroFill
         << "\n  Characteristics: "
         << TLSDir->Characteristics
         << "\n  Alignment: "
         << TLSDir->getAlignment()
         << "\n\n";
}

static void printTLSDirectory(const COFFObjectFile *Obj) {
  const pe32_header *PE32Header = Obj->getPE32Header();
  const pe32plus_header *PE32PlusHeader = Obj->getPE32PlusHeader();

  // Skip if it's not executable.
  if (!PE32Header && !PE32PlusHeader)
    return;

  if (PE32Header) {
    if (auto *TLSDir = Obj->getTLSDirectory32())
      printTLSDirectoryT(TLSDir);
  } else {
    if (auto *TLSDir = Obj->getTLSDirectory64())
      printTLSDirectoryT(TLSDir);
  }

  outs() << "\n";
}

static void printLoadConfiguration(const COFFObjectFile *Obj) {
  // Skip if it's not executable.
  if (!Obj->getPE32Header())
    return;

  // Currently only x86 is supported
  if (Obj->getMachine() != COFF::IMAGE_FILE_MACHINE_I386)
    return;

  auto *LoadConf = Obj->getLoadConfig32();
  if (!LoadConf)
    return;

  outs() << "Load configuration:"
         << "\n  Timestamp: " << LoadConf->TimeDateStamp
         << "\n  Major Version: " << LoadConf->MajorVersion
         << "\n  Minor Version: " << LoadConf->MinorVersion
         << "\n  GlobalFlags Clear: " << LoadConf->GlobalFlagsClear
         << "\n  GlobalFlags Set: " << LoadConf->GlobalFlagsSet
         << "\n  Critical Section Default Timeout: " << LoadConf->CriticalSectionDefaultTimeout
         << "\n  Decommit Free Block Threshold: " << LoadConf->DeCommitFreeBlockThreshold
         << "\n  Decommit Total Free Threshold: " << LoadConf->DeCommitTotalFreeThreshold
         << "\n  Lock Prefix Table: " << LoadConf->LockPrefixTable
         << "\n  Maximum Allocation Size: " << LoadConf->MaximumAllocationSize
         << "\n  Virtual Memory Threshold: " << LoadConf->VirtualMemoryThreshold
         << "\n  Process Affinity Mask: " << LoadConf->ProcessAffinityMask
         << "\n  Process Heap Flags: " << LoadConf->ProcessHeapFlags
         << "\n  CSD Version: " << LoadConf->CSDVersion
         << "\n  Security Cookie: " << LoadConf->SecurityCookie
         << "\n  SEH Table: " << LoadConf->SEHandlerTable
         << "\n  SEH Count: " << LoadConf->SEHandlerCount
         << "\n\n";
  printSEHTable(Obj, LoadConf->SEHandlerTable, LoadConf->SEHandlerCount);
  outs() << "\n";
}

// Prints import tables. The import table is a table containing the list of
// DLL name and symbol names which will be linked by the loader.
static void printImportTables(const COFFObjectFile *Obj) {
  import_directory_iterator I = Obj->import_directory_begin();
  import_directory_iterator E = Obj->import_directory_end();
  if (I == E)
    return;
  outs() << "The Import Tables:\n";
  for (const ImportDirectoryEntryRef &DirRef : Obj->import_directories()) {
    const coff_import_directory_table_entry *Dir;
    StringRef Name;
    if (DirRef.getImportTableEntry(Dir)) return;
    if (DirRef.getName(Name)) return;

    outs() << format("  lookup %08x time %08x fwd %08x name %08x addr %08x\n\n",
                     static_cast<uint32_t>(Dir->ImportLookupTableRVA),
                     static_cast<uint32_t>(Dir->TimeDateStamp),
                     static_cast<uint32_t>(Dir->ForwarderChain),
                     static_cast<uint32_t>(Dir->NameRVA),
                     static_cast<uint32_t>(Dir->ImportAddressTableRVA));
    outs() << "    DLL Name: " << Name << "\n";
    outs() << "    Hint/Ord  Name\n";
    for (const ImportedSymbolRef &Entry : DirRef.imported_symbols()) {
      bool IsOrdinal;
      if (Entry.isOrdinal(IsOrdinal))
        return;
      if (IsOrdinal) {
        uint16_t Ordinal;
        if (Entry.getOrdinal(Ordinal))
          return;
        outs() << format("      % 6d\n", Ordinal);
        continue;
      }
      uint32_t HintNameRVA;
      if (Entry.getHintNameRVA(HintNameRVA))
        return;
      uint16_t Hint;
      StringRef Name;
      if (Obj->getHintName(HintNameRVA, Hint, Name))
        return;
      outs() << format("      % 6d  ", Hint) << Name << "\n";
    }
    outs() << "\n";
  }
}

// Prints export tables. The export table is a table containing the list of
// exported symbol from the DLL.
static void printExportTable(const COFFObjectFile *Obj) {
  export_directory_iterator I = Obj->export_directory_begin();
  export_directory_iterator E = Obj->export_directory_end();
  if (I == E)
    return;
  outs() << "Export Table:\n";
  StringRef DllName;
  uint32_t OrdinalBase;
  if (I->getDllName(DllName))
    return;
  if (I->getOrdinalBase(OrdinalBase))
    return;
  outs() << " DLL name: " << DllName << "\n";
  outs() << " Ordinal base: " << OrdinalBase << "\n";
  outs() << " Ordinal      RVA  Name\n";
  for (; I != E; I = ++I) {
    uint32_t RVA;
    if (I->getExportRVA(RVA))
      return;
    StringRef Name;
    if (I->getSymbolName(Name))
      continue;
    if (!RVA && Name.empty())
      continue;

    uint32_t Ordinal;
    if (I->getOrdinal(Ordinal))
      return;
    bool IsForwarder;
    if (I->isForwarder(IsForwarder))
      return;

    if (IsForwarder) {
      // Export table entries can be used to re-export symbols that
      // this COFF file is imported from some DLLs. This is rare.
      // In most cases IsForwarder is false.
      outs() << format("   %5d         ", Ordinal);
    } else {
      outs() << format("   %5d %# 8x", Ordinal, RVA);
    }

    if (!Name.empty())
      outs() << "  " << Name;
    if (IsForwarder) {
      StringRef S;
      if (I->getForwardTo(S))
        return;
      outs() << " (forwarded to " << S << ")";
    }
    outs() << "\n";
  }
}

// Given the COFF object file, this function returns the relocations for .pdata
// and the pointer to "runtime function" structs.
static bool getPDataSection(const COFFObjectFile *Obj,
                            std::vector<RelocationRef> &Rels,
                            const RuntimeFunction *&RFStart, int &NumRFs) {
  for (const SectionRef &Section : Obj->sections()) {
    StringRef Name = unwrapOrError(Section.getName(), Obj->getFileName());
    if (Name != ".pdata")
      continue;

    const coff_section *Pdata = Obj->getCOFFSection(Section);
    append_range(Rels, Section.relocations());

    // Sort relocations by address.
    llvm::sort(Rels, isRelocAddressLess);

    ArrayRef<uint8_t> Contents;
    if (Error E = Obj->getSectionContents(Pdata, Contents))
      reportError(std::move(E), Obj->getFileName());

    if (Contents.empty())
      continue;

    RFStart = reinterpret_cast<const RuntimeFunction *>(Contents.data());
    NumRFs = Contents.size() / sizeof(RuntimeFunction);
    return true;
  }
  return false;
}

Error objdump::getCOFFRelocationValueString(const COFFObjectFile *Obj,
                                            const RelocationRef &Rel,
                                            SmallVectorImpl<char> &Result) {
  symbol_iterator SymI = Rel.getSymbol();
  Expected<StringRef> SymNameOrErr = SymI->getName();
  if (!SymNameOrErr)
    return SymNameOrErr.takeError();
  StringRef SymName = *SymNameOrErr;
  Result.append(SymName.begin(), SymName.end());
  return Error::success();
}

static void printWin64EHUnwindInfo(const Win64EH::UnwindInfo *UI) {
  // The casts to int are required in order to output the value as number.
  // Without the casts the value would be interpreted as char data (which
  // results in garbage output).
  outs() << "    Version: " << static_cast<int>(UI->getVersion()) << "\n";
  outs() << "    Flags: " << static_cast<int>(UI->getFlags());
  if (UI->getFlags()) {
    if (UI->getFlags() & UNW_ExceptionHandler)
      outs() << " UNW_ExceptionHandler";
    if (UI->getFlags() & UNW_TerminateHandler)
      outs() << " UNW_TerminateHandler";
    if (UI->getFlags() & UNW_ChainInfo)
      outs() << " UNW_ChainInfo";
  }
  outs() << "\n";
  outs() << "    Size of prolog: " << static_cast<int>(UI->PrologSize) << "\n";
  outs() << "    Number of Codes: " << static_cast<int>(UI->NumCodes) << "\n";
  // Maybe this should move to output of UOP_SetFPReg?
  if (UI->getFrameRegister()) {
    outs() << "    Frame register: "
           << getUnwindRegisterName(UI->getFrameRegister()) << "\n";
    outs() << "    Frame offset: " << 16 * UI->getFrameOffset() << "\n";
  } else {
    outs() << "    No frame pointer used\n";
  }
  if (UI->getFlags() & (UNW_ExceptionHandler | UNW_TerminateHandler)) {
    // FIXME: Output exception handler data
  } else if (UI->getFlags() & UNW_ChainInfo) {
    // FIXME: Output chained unwind info
  }

  if (UI->NumCodes)
    outs() << "    Unwind Codes:\n";

  printAllUnwindCodes(ArrayRef(&UI->UnwindCodes[0], UI->NumCodes));

  outs() << "\n";
  outs().flush();
}

/// Prints out the given RuntimeFunction struct for x64, assuming that Obj is
/// pointing to an executable file.
static void printRuntimeFunction(const COFFObjectFile *Obj,
                                 const RuntimeFunction &RF) {
  if (!RF.StartAddress)
    return;
  outs() << "Function Table:\n"
         << format("  Start Address: 0x%04x\n",
                   static_cast<uint32_t>(RF.StartAddress))
         << format("  End Address: 0x%04x\n",
                   static_cast<uint32_t>(RF.EndAddress))
         << format("  Unwind Info Address: 0x%04x\n",
                   static_cast<uint32_t>(RF.UnwindInfoOffset));
  uintptr_t addr;
  if (Obj->getRvaPtr(RF.UnwindInfoOffset, addr))
    return;
  printWin64EHUnwindInfo(reinterpret_cast<const Win64EH::UnwindInfo *>(addr));
}

/// Prints out the given RuntimeFunction struct for x64, assuming that Obj is
/// pointing to an object file. Unlike executable, fields in RuntimeFunction
/// struct are filled with zeros, but instead there are relocations pointing to
/// them so that the linker will fill targets' RVAs to the fields at link
/// time. This function interprets the relocations to find the data to be used
/// in the resulting executable.
static void printRuntimeFunctionRels(const COFFObjectFile *Obj,
                                     const RuntimeFunction &RF,
                                     uint64_t SectionOffset,
                                     const std::vector<RelocationRef> &Rels) {
  outs() << "Function Table:\n";
  outs() << "  Start Address: ";
  printCOFFSymbolAddress(outs(), Rels,
                         SectionOffset +
                             /*offsetof(RuntimeFunction, StartAddress)*/ 0,
                         RF.StartAddress);
  outs() << "\n";

  outs() << "  End Address: ";
  printCOFFSymbolAddress(outs(), Rels,
                         SectionOffset +
                             /*offsetof(RuntimeFunction, EndAddress)*/ 4,
                         RF.EndAddress);
  outs() << "\n";

  outs() << "  Unwind Info Address: ";
  printCOFFSymbolAddress(outs(), Rels,
                         SectionOffset +
                             /*offsetof(RuntimeFunction, UnwindInfoOffset)*/ 8,
                         RF.UnwindInfoOffset);
  outs() << "\n";

  ArrayRef<uint8_t> XContents;
  uint64_t UnwindInfoOffset = 0;
  if (Error E = getSectionContents(
          Obj, Rels,
          SectionOffset +
              /*offsetof(RuntimeFunction, UnwindInfoOffset)*/ 8,
          XContents, UnwindInfoOffset))
    reportError(std::move(E), Obj->getFileName());
  if (XContents.empty())
    return;

  UnwindInfoOffset += RF.UnwindInfoOffset;
  if (UnwindInfoOffset > XContents.size())
    return;

  auto *UI = reinterpret_cast<const Win64EH::UnwindInfo *>(XContents.data() +
                                                           UnwindInfoOffset);
  printWin64EHUnwindInfo(UI);
}

void objdump::printCOFFUnwindInfo(const COFFObjectFile *Obj) {
  if (Obj->getMachine() != COFF::IMAGE_FILE_MACHINE_AMD64) {
    WithColor::error(errs(), "llvm-objdump")
        << "unsupported image machine type "
           "(currently only AMD64 is supported).\n";
    return;
  }

  std::vector<RelocationRef> Rels;
  const RuntimeFunction *RFStart;
  int NumRFs;
  if (!getPDataSection(Obj, Rels, RFStart, NumRFs))
    return;
  ArrayRef<RuntimeFunction> RFs(RFStart, NumRFs);

  bool IsExecutable = Rels.empty();
  if (IsExecutable) {
    for (const RuntimeFunction &RF : RFs)
      printRuntimeFunction(Obj, RF);
    return;
  }

  for (const RuntimeFunction &RF : RFs) {
    uint64_t SectionOffset =
        std::distance(RFs.begin(), &RF) * sizeof(RuntimeFunction);
    printRuntimeFunctionRels(Obj, RF, SectionOffset, Rels);
  }
}

void COFFDumper::printPrivateHeaders() {
  COFFDumper CD(Obj);
  const uint16_t Cha = Obj.getCharacteristics();
  outs() << "Characteristics 0x" << Twine::utohexstr(Cha) << '\n';
#define FLAG(F, Name)                                                          \
  if (Cha & F)                                                                 \
    outs() << '\t' << Name << '\n';
  FLAG(COFF::IMAGE_FILE_RELOCS_STRIPPED, "relocations stripped");
  FLAG(COFF::IMAGE_FILE_EXECUTABLE_IMAGE, "executable");
  FLAG(COFF::IMAGE_FILE_LINE_NUMS_STRIPPED, "line numbers stripped");
  FLAG(COFF::IMAGE_FILE_LOCAL_SYMS_STRIPPED, "symbols stripped");
  FLAG(COFF::IMAGE_FILE_LARGE_ADDRESS_AWARE, "large address aware");
  FLAG(COFF::IMAGE_FILE_BYTES_REVERSED_LO, "little endian");
  FLAG(COFF::IMAGE_FILE_32BIT_MACHINE, "32 bit words");
  FLAG(COFF::IMAGE_FILE_DEBUG_STRIPPED, "debugging information removed");
  FLAG(COFF::IMAGE_FILE_REMOVABLE_RUN_FROM_SWAP,
       "copy to swap file if on removable media");
  FLAG(COFF::IMAGE_FILE_NET_RUN_FROM_SWAP,
       "copy to swap file if on network media");
  FLAG(COFF::IMAGE_FILE_SYSTEM, "system file");
  FLAG(COFF::IMAGE_FILE_DLL, "DLL");
  FLAG(COFF::IMAGE_FILE_UP_SYSTEM_ONLY, "run only on uniprocessor machine");
  FLAG(COFF::IMAGE_FILE_BYTES_REVERSED_HI, "big endian");
#undef FLAG

  // TODO Support PE_IMAGE_DEBUG_TYPE_REPRO.
  // Since ctime(3) returns a 26 character string of the form:
  // "Sun Sep 16 01:03:52 1973\n\0"
  // just print 24 characters.
  const time_t Timestamp = Obj.getTimeDateStamp();
  outs() << format("\nTime/Date               %.24s\n", ctime(&Timestamp));

  if (const pe32_header *Hdr = Obj.getPE32Header())
    CD.printPEHeader<pe32_header>(*Hdr);
  else if (const pe32plus_header *Hdr = Obj.getPE32PlusHeader())
    CD.printPEHeader<pe32plus_header>(*Hdr);

  printTLSDirectory(&Obj);
  printLoadConfiguration(&Obj);
  printImportTables(&Obj);
  printExportTable(&Obj);
}

void objdump::printCOFFSymbolTable(const object::COFFImportFile &i) {
  unsigned Index = 0;
  bool IsCode = i.getCOFFImportHeader()->getType() == COFF::IMPORT_CODE;

  for (const object::BasicSymbolRef &Sym : i.symbols()) {
    std::string Name;
    raw_string_ostream NS(Name);

    cantFail(Sym.printName(NS));
    NS.flush();

    outs() << "[" << format("%2d", Index) << "]"
           << "(sec " << format("%2d", 0) << ")"
           << "(fl 0x00)" // Flag bits, which COFF doesn't have.
           << "(ty " << format("%3x", (IsCode && Index) ? 32 : 0) << ")"
           << "(scl " << format("%3x", 0) << ") "
           << "(nx " << 0 << ") "
           << "0x" << format("%08x", 0) << " " << Name << '\n';

    ++Index;
  }
}

void objdump::printCOFFSymbolTable(const COFFObjectFile &coff) {
  for (unsigned SI = 0, SE = coff.getNumberOfSymbols(); SI != SE; ++SI) {
    Expected<COFFSymbolRef> Symbol = coff.getSymbol(SI);
    if (!Symbol)
      reportError(Symbol.takeError(), coff.getFileName());

    Expected<StringRef> NameOrErr = coff.getSymbolName(*Symbol);
    if (!NameOrErr)
      reportError(NameOrErr.takeError(), coff.getFileName());
    StringRef Name = *NameOrErr;

    outs() << "[" << format("%2d", SI) << "]"
           << "(sec " << format("%2d", int(Symbol->getSectionNumber())) << ")"
           << "(fl 0x00)" // Flag bits, which COFF doesn't have.
           << "(ty " << format("%3x", unsigned(Symbol->getType())) << ")"
           << "(scl " << format("%3x", unsigned(Symbol->getStorageClass()))
           << ") "
           << "(nx " << unsigned(Symbol->getNumberOfAuxSymbols()) << ") "
           << "0x" << format("%08x", unsigned(Symbol->getValue())) << " "
           << Name;
    if (Demangle && Name.starts_with("?")) {
      int Status = -1;
      char *DemangledSymbol = microsoftDemangle(Name, nullptr, &Status);

      if (Status == 0 && DemangledSymbol) {
        outs() << " (" << StringRef(DemangledSymbol) << ")";
        std::free(DemangledSymbol);
      } else {
        outs() << " (invalid mangled name)";
      }
    }
    outs() << "\n";

    for (unsigned AI = 0, AE = Symbol->getNumberOfAuxSymbols(); AI < AE; ++AI, ++SI) {
      if (Symbol->isSectionDefinition()) {
        const coff_aux_section_definition *asd;
        if (Error E =
                coff.getAuxSymbol<coff_aux_section_definition>(SI + 1, asd))
          reportError(std::move(E), coff.getFileName());

        int32_t AuxNumber = asd->getNumber(Symbol->isBigObj());

        outs() << "AUX "
               << format("scnlen 0x%x nreloc %d nlnno %d checksum 0x%x "
                         , unsigned(asd->Length)
                         , unsigned(asd->NumberOfRelocations)
                         , unsigned(asd->NumberOfLinenumbers)
                         , unsigned(asd->CheckSum))
               << format("assoc %d comdat %d\n"
                         , unsigned(AuxNumber)
                         , unsigned(asd->Selection));
      } else if (Symbol->isFileRecord()) {
        const char *FileName;
        if (Error E = coff.getAuxSymbol<char>(SI + 1, FileName))
          reportError(std::move(E), coff.getFileName());

        StringRef Name(FileName, Symbol->getNumberOfAuxSymbols() *
                                     coff.getSymbolTableEntrySize());
        outs() << "AUX " << Name.rtrim(StringRef("\0", 1))  << '\n';

        SI = SI + Symbol->getNumberOfAuxSymbols();
        break;
      } else if (Symbol->isWeakExternal()) {
        const coff_aux_weak_external *awe;
        if (Error E = coff.getAuxSymbol<coff_aux_weak_external>(SI + 1, awe))
          reportError(std::move(E), coff.getFileName());

        outs() << "AUX " << format("indx %d srch %d\n",
                                   static_cast<uint32_t>(awe->TagIndex),
                                   static_cast<uint32_t>(awe->Characteristics));
      } else {
        outs() << "AUX Unknown\n";
      }
    }
  }
}
