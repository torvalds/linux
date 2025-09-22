//===-- XCOFFDumper.cpp - XCOFF dumping utility -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements an XCOFF specific dumper for llvm-readobj.
//
//===----------------------------------------------------------------------===//

#include "ObjDumper.h"
#include "llvm-readobj.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Object/XCOFFObjectFile.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/ScopedPrinter.h"

#include <ctime>

using namespace llvm;
using namespace object;

namespace {

class XCOFFDumper : public ObjDumper {

public:
  XCOFFDumper(const XCOFFObjectFile &Obj, ScopedPrinter &Writer)
      : ObjDumper(Writer, Obj.getFileName()), Obj(Obj) {}

  void printFileHeaders() override;
  void printAuxiliaryHeader() override;
  void printSectionHeaders() override;
  void printRelocations() override;
  void printSymbols(bool ExtraSymInfo) override;
  void printDynamicSymbols() override;
  void printUnwindInfo() override;
  void printStackMap() const override;
  void printNeededLibraries() override;
  void printStringTable() override;
  void printExceptionSection() override;
  void printLoaderSection(bool PrintHeader, bool PrintSymbols,
                          bool PrintRelocations) override;

  ScopedPrinter &getScopedPrinter() const { return W; }

private:
  template <typename T> void printSectionHeaders(ArrayRef<T> Sections);
  template <typename T> void printGenericSectionHeader(T &Sec) const;
  template <typename T> void printOverflowSectionHeader(T &Sec) const;
  template <typename T>
  void printExceptionSectionEntry(const T &ExceptionSectEnt) const;
  template <typename T> void printExceptionSectionEntries() const;
  template <typename T> const T *getAuxEntPtr(uintptr_t AuxAddress);
  void printFileAuxEnt(const XCOFFFileAuxEnt *AuxEntPtr);
  void printCsectAuxEnt(XCOFFCsectAuxRef AuxEntRef);
  void printSectAuxEntForStat(const XCOFFSectAuxEntForStat *AuxEntPtr);
  void printExceptionAuxEnt(const XCOFFExceptionAuxEnt *AuxEntPtr);
  void printFunctionAuxEnt(const XCOFFFunctionAuxEnt32 *AuxEntPtr);
  void printFunctionAuxEnt(const XCOFFFunctionAuxEnt64 *AuxEntPtr);
  void printBlockAuxEnt(const XCOFFBlockAuxEnt32 *AuxEntPtr);
  void printBlockAuxEnt(const XCOFFBlockAuxEnt64 *AuxEntPtr);
  template <typename T> void printSectAuxEntForDWARF(const T *AuxEntPtr);
  void printSymbol(const SymbolRef &);
  template <typename RelTy> void printRelocation(RelTy Reloc);
  template <typename Shdr, typename RelTy>
  void printRelocations(ArrayRef<Shdr> Sections);
  void printAuxiliaryHeader(const XCOFFAuxiliaryHeader32 *AuxHeader);
  void printAuxiliaryHeader(const XCOFFAuxiliaryHeader64 *AuxHeader);
  void printLoaderSectionHeader(uintptr_t LoaderSectAddr);
  void printLoaderSectionSymbols(uintptr_t LoaderSectAddr);
  template <typename LoaderSectionSymbolEntry, typename LoaderSectionHeader>
  void printLoaderSectionSymbolsHelper(uintptr_t LoaderSectAddr);
  template <typename LoadSectionRelocTy>
  void printLoaderSectionRelocationEntry(LoadSectionRelocTy *LoaderSecRelEntPtr,
                                         StringRef SymbolName);
  void printLoaderSectionRelocationEntries(uintptr_t LoaderSectAddr);
  template <typename LoaderSectionHeader, typename LoaderSectionSymbolEntry,
            typename LoaderSectionRelocationEntry>
  void printLoaderSectionRelocationEntriesHelper(uintptr_t LoaderSectAddr);

  const XCOFFObjectFile &Obj;
  const static int32_t FirstSymIdxOfLoaderSec = 3;
};
} // anonymous namespace

void XCOFFDumper::printFileHeaders() {
  DictScope DS(W, "FileHeader");
  W.printHex("Magic", Obj.getMagic());
  W.printNumber("NumberOfSections", Obj.getNumberOfSections());

  // Negative timestamp values are reserved for future use.
  int32_t TimeStamp = Obj.getTimeStamp();
  if (TimeStamp > 0) {
    // This handling of the time stamp assumes that the host system's time_t is
    // compatible with AIX time_t. If a platform is not compatible, the lit
    // tests will let us know.
    time_t TimeDate = TimeStamp;

    char FormattedTime[80] = {};

    size_t BytesFormatted =
      strftime(FormattedTime, sizeof(FormattedTime), "%F %T", gmtime(&TimeDate));
    if (BytesFormatted)
      W.printHex("TimeStamp", FormattedTime, TimeStamp);
    else
      W.printHex("Timestamp", TimeStamp);
  } else {
    W.printHex("TimeStamp", TimeStamp == 0 ? "None" : "Reserved Value",
               TimeStamp);
  }

  // The number of symbol table entries is an unsigned value in 64-bit objects
  // and a signed value (with negative values being 'reserved') in 32-bit
  // objects.
  if (Obj.is64Bit()) {
    W.printHex("SymbolTableOffset", Obj.getSymbolTableOffset64());
    W.printNumber("SymbolTableEntries", Obj.getNumberOfSymbolTableEntries64());
  } else {
    W.printHex("SymbolTableOffset", Obj.getSymbolTableOffset32());
    int32_t SymTabEntries = Obj.getRawNumberOfSymbolTableEntries32();
    if (SymTabEntries >= 0)
      W.printNumber("SymbolTableEntries", SymTabEntries);
    else
      W.printHex("SymbolTableEntries", "Reserved Value", SymTabEntries);
  }

  W.printHex("OptionalHeaderSize", Obj.getOptionalHeaderSize());
  W.printHex("Flags", Obj.getFlags());

  // TODO FIXME Add support for the auxiliary header (if any) once
  // XCOFFObjectFile has the necessary support.
}

void XCOFFDumper::printAuxiliaryHeader() {
  DictScope DS(W, "AuxiliaryHeader");

  if (Obj.is64Bit())
    printAuxiliaryHeader(Obj.auxiliaryHeader64());
  else
    printAuxiliaryHeader(Obj.auxiliaryHeader32());
}

void XCOFFDumper::printSectionHeaders() {
  if (Obj.is64Bit())
    printSectionHeaders(Obj.sections64());
  else
    printSectionHeaders(Obj.sections32());
}

void XCOFFDumper::printLoaderSection(bool PrintHeader, bool PrintSymbols,
                                     bool PrintRelocations) {
  DictScope DS(W, "Loader Section");
  Expected<uintptr_t> LoaderSectionAddrOrError =
      Obj.getSectionFileOffsetToRawData(XCOFF::STYP_LOADER);
  if (!LoaderSectionAddrOrError) {
    reportUniqueWarning(LoaderSectionAddrOrError.takeError());
    return;
  }
  uintptr_t LoaderSectionAddr = LoaderSectionAddrOrError.get();

  if (LoaderSectionAddr == 0)
    return;

  W.indent();
  if (PrintHeader)
    printLoaderSectionHeader(LoaderSectionAddr);

  if (PrintSymbols)
    printLoaderSectionSymbols(LoaderSectionAddr);

  if (PrintRelocations)
    printLoaderSectionRelocationEntries(LoaderSectionAddr);

  W.unindent();
}

void XCOFFDumper::printLoaderSectionHeader(uintptr_t LoaderSectionAddr) {
  DictScope DS(W, "Loader Section Header");

  auto PrintLoadSecHeaderCommon = [&](const auto *LDHeader) {
    W.printNumber("Version", LDHeader->Version);
    W.printNumber("NumberOfSymbolEntries", LDHeader->NumberOfSymTabEnt);
    W.printNumber("NumberOfRelocationEntries", LDHeader->NumberOfRelTabEnt);
    W.printNumber("LengthOfImportFileIDStringTable",
                  LDHeader->LengthOfImpidStrTbl);
    W.printNumber("NumberOfImportFileIDs", LDHeader->NumberOfImpid);
    W.printHex("OffsetToImportFileIDs", LDHeader->OffsetToImpid);
    W.printNumber("LengthOfStringTable", LDHeader->LengthOfStrTbl);
    W.printHex("OffsetToStringTable", LDHeader->OffsetToStrTbl);
  };

  if (Obj.is64Bit()) {
    const LoaderSectionHeader64 *LoaderSec64 =
        reinterpret_cast<const LoaderSectionHeader64 *>(LoaderSectionAddr);
    PrintLoadSecHeaderCommon(LoaderSec64);
    W.printHex("OffsetToSymbolTable", LoaderSec64->OffsetToSymTbl);
    W.printHex("OffsetToRelocationEntries", LoaderSec64->OffsetToRelEnt);
  } else {
    const LoaderSectionHeader32 *LoaderSec32 =
        reinterpret_cast<const LoaderSectionHeader32 *>(LoaderSectionAddr);
    PrintLoadSecHeaderCommon(LoaderSec32);
  }
}

const EnumEntry<XCOFF::StorageClass> SymStorageClass[] = {
#define ECase(X)                                                               \
  { #X, XCOFF::X }
    ECase(C_NULL),  ECase(C_AUTO),    ECase(C_EXT),     ECase(C_STAT),
    ECase(C_REG),   ECase(C_EXTDEF),  ECase(C_LABEL),   ECase(C_ULABEL),
    ECase(C_MOS),   ECase(C_ARG),     ECase(C_STRTAG),  ECase(C_MOU),
    ECase(C_UNTAG), ECase(C_TPDEF),   ECase(C_USTATIC), ECase(C_ENTAG),
    ECase(C_MOE),   ECase(C_REGPARM), ECase(C_FIELD),   ECase(C_BLOCK),
    ECase(C_FCN),   ECase(C_EOS),     ECase(C_FILE),    ECase(C_LINE),
    ECase(C_ALIAS), ECase(C_HIDDEN),  ECase(C_HIDEXT),  ECase(C_BINCL),
    ECase(C_EINCL), ECase(C_INFO),    ECase(C_WEAKEXT), ECase(C_DWARF),
    ECase(C_GSYM),  ECase(C_LSYM),    ECase(C_PSYM),    ECase(C_RSYM),
    ECase(C_RPSYM), ECase(C_STSYM),   ECase(C_TCSYM),   ECase(C_BCOMM),
    ECase(C_ECOML), ECase(C_ECOMM),   ECase(C_DECL),    ECase(C_ENTRY),
    ECase(C_FUN),   ECase(C_BSTAT),   ECase(C_ESTAT),   ECase(C_GTLS),
    ECase(C_STTLS), ECase(C_EFCN)
#undef ECase
};

template <typename LoaderSectionSymbolEntry, typename LoaderSectionHeader>
void XCOFFDumper::printLoaderSectionSymbolsHelper(uintptr_t LoaderSectionAddr) {
  const LoaderSectionHeader *LoadSecHeader =
      reinterpret_cast<const LoaderSectionHeader *>(LoaderSectionAddr);
  const LoaderSectionSymbolEntry *LoadSecSymEntPtr =
      reinterpret_cast<LoaderSectionSymbolEntry *>(
          LoaderSectionAddr + uintptr_t(LoadSecHeader->getOffsetToSymTbl()));

  for (uint32_t i = 0; i < LoadSecHeader->NumberOfSymTabEnt;
       ++i, ++LoadSecSymEntPtr) {
    if (Error E = Binary::checkOffset(
            Obj.getMemoryBufferRef(),
            LoaderSectionAddr + uintptr_t(LoadSecHeader->getOffsetToSymTbl()) +
                (i * sizeof(LoaderSectionSymbolEntry)),
            sizeof(LoaderSectionSymbolEntry))) {
      reportUniqueWarning(std::move(E));
      return;
    }

    Expected<StringRef> SymbolNameOrErr =
        LoadSecSymEntPtr->getSymbolName(LoadSecHeader);
    if (!SymbolNameOrErr) {
      reportUniqueWarning(SymbolNameOrErr.takeError());
      return;
    }

    DictScope DS(W, "Symbol");
    StringRef SymbolName = SymbolNameOrErr.get();
    W.printString("Name", opts::Demangle ? demangle(SymbolName) : SymbolName);
    W.printHex("Virtual Address", LoadSecSymEntPtr->Value);
    W.printNumber("SectionNum", LoadSecSymEntPtr->SectionNumber);
    W.printHex("SymbolType", LoadSecSymEntPtr->SymbolType);
    W.printEnum("StorageClass",
                static_cast<uint8_t>(LoadSecSymEntPtr->StorageClass),
                ArrayRef(SymStorageClass));
    W.printHex("ImportFileID", LoadSecSymEntPtr->ImportFileID);
    W.printNumber("ParameterTypeCheck", LoadSecSymEntPtr->ParameterTypeCheck);
  }
}

void XCOFFDumper::printLoaderSectionSymbols(uintptr_t LoaderSectionAddr) {
  DictScope DS(W, "Loader Section Symbols");
  if (Obj.is64Bit())
    printLoaderSectionSymbolsHelper<LoaderSectionSymbolEntry64,
                                    LoaderSectionHeader64>(LoaderSectionAddr);
  else
    printLoaderSectionSymbolsHelper<LoaderSectionSymbolEntry32,
                                    LoaderSectionHeader32>(LoaderSectionAddr);
}

const EnumEntry<XCOFF::RelocationType> RelocationTypeNameclass[] = {
#define ECase(X)                                                               \
  { #X, XCOFF::X }
    ECase(R_POS),    ECase(R_RL),     ECase(R_RLA),    ECase(R_NEG),
    ECase(R_REL),    ECase(R_TOC),    ECase(R_TRL),    ECase(R_TRLA),
    ECase(R_GL),     ECase(R_TCL),    ECase(R_REF),    ECase(R_BA),
    ECase(R_BR),     ECase(R_RBA),    ECase(R_RBR),    ECase(R_TLS),
    ECase(R_TLS_IE), ECase(R_TLS_LD), ECase(R_TLS_LE), ECase(R_TLSM),
    ECase(R_TLSML),  ECase(R_TOCU),   ECase(R_TOCL)
#undef ECase
};

// From the XCOFF specification: there are five implicit external symbols, one
// each for the .text, .data, .bss, .tdata, and .tbss sections. These symbols
// are referenced from the relocation table entries using symbol table index
// values 0, 1, 2, -1, and -2, respectively.
static const char *getImplicitLoaderSectionSymName(int SymIndx) {
  switch (SymIndx) {
  default:
    return "Unkown Symbol Name";
  case -2:
    return ".tbss";
  case -1:
    return ".tdata";
  case 0:
    return ".text";
  case 1:
    return ".data";
  case 2:
    return ".bss";
  }
}

template <typename LoadSectionRelocTy>
void XCOFFDumper::printLoaderSectionRelocationEntry(
    LoadSectionRelocTy *LoaderSecRelEntPtr, StringRef SymbolName) {
  uint16_t Type = LoaderSecRelEntPtr->Type;
  if (opts::ExpandRelocs) {
    DictScope DS(W, "Relocation");
    auto IsRelocationSigned = [](uint8_t Info) {
      return Info & XCOFF::XR_SIGN_INDICATOR_MASK;
    };
    auto IsFixupIndicated = [](uint8_t Info) {
      return Info & XCOFF::XR_FIXUP_INDICATOR_MASK;
    };
    auto GetRelocatedLength = [](uint8_t Info) {
      // The relocation encodes the bit length being relocated minus 1. Add
      // back
      //   the 1 to get the actual length being relocated.
      return (Info & XCOFF::XR_BIASED_LENGTH_MASK) + 1;
    };

    uint8_t Info = Type >> 8;
    W.printHex("Virtual Address", LoaderSecRelEntPtr->VirtualAddr);
    W.printNumber("Symbol", opts::Demangle ? demangle(SymbolName) : SymbolName,
                  LoaderSecRelEntPtr->SymbolIndex);
    W.printString("IsSigned", IsRelocationSigned(Info) ? "Yes" : "No");
    W.printNumber("FixupBitValue", IsFixupIndicated(Info) ? 1 : 0);
    W.printNumber("Length", GetRelocatedLength(Info));
    W.printEnum("Type", static_cast<uint8_t>(Type),
                ArrayRef(RelocationTypeNameclass));
    W.printNumber("SectionNumber", LoaderSecRelEntPtr->SectionNum);
  } else {
    W.startLine() << format_hex(LoaderSecRelEntPtr->VirtualAddr,
                                Obj.is64Bit() ? 18 : 10)
                  << " " << format_hex(Type, 6) << " ("
                  << XCOFF::getRelocationTypeString(
                         static_cast<XCOFF::RelocationType>(Type))
                  << ")" << format_decimal(LoaderSecRelEntPtr->SectionNum, 8)
                  << "    "
                  << (opts::Demangle ? demangle(SymbolName) : SymbolName)
                  << " (" << LoaderSecRelEntPtr->SymbolIndex << ")\n";
  }
}

template <typename LoaderSectionHeader, typename LoaderSectionSymbolEntry,
          typename LoaderSectionRelocationEntry>
void XCOFFDumper::printLoaderSectionRelocationEntriesHelper(
    uintptr_t LoaderSectionAddr) {
  const LoaderSectionHeader *LoaderSec =
      reinterpret_cast<const LoaderSectionHeader *>(LoaderSectionAddr);
  const LoaderSectionRelocationEntry *LoaderSecRelEntPtr =
      reinterpret_cast<const LoaderSectionRelocationEntry *>(
          LoaderSectionAddr + uintptr_t(LoaderSec->getOffsetToRelEnt()));

  if (!opts::ExpandRelocs)
    W.startLine() << center_justify("Vaddr", Obj.is64Bit() ? 18 : 10)
                  << center_justify("Type", 15) << right_justify("SecNum", 8)
                  << center_justify("SymbolName (Index) ", 24) << "\n";

  for (uint32_t i = 0; i < LoaderSec->NumberOfRelTabEnt;
       ++i, ++LoaderSecRelEntPtr) {
    StringRef SymbolName;
    if (LoaderSecRelEntPtr->SymbolIndex >= FirstSymIdxOfLoaderSec) {
      // Because there are implicit symbol index values (-2, -1, 0, 1, 2),
      // LoaderSecRelEnt.SymbolIndex - FirstSymIdxOfLoaderSec will get the
      // real symbol from the symbol table.
      const uint64_t SymOffset =
          (LoaderSecRelEntPtr->SymbolIndex - FirstSymIdxOfLoaderSec) *
          sizeof(LoaderSectionSymbolEntry);
      const LoaderSectionSymbolEntry *LoaderSecRelSymEntPtr =
          reinterpret_cast<LoaderSectionSymbolEntry *>(
              LoaderSectionAddr + uintptr_t(LoaderSec->getOffsetToSymTbl()) +
              SymOffset);

      Expected<StringRef> SymbolNameOrErr =
          LoaderSecRelSymEntPtr->getSymbolName(LoaderSec);
      if (!SymbolNameOrErr) {
        reportUniqueWarning(SymbolNameOrErr.takeError());
        return;
      }
      SymbolName = SymbolNameOrErr.get();
    } else
      SymbolName =
          getImplicitLoaderSectionSymName(LoaderSecRelEntPtr->SymbolIndex);

    printLoaderSectionRelocationEntry(LoaderSecRelEntPtr, SymbolName);
  }
}

void XCOFFDumper::printLoaderSectionRelocationEntries(
    uintptr_t LoaderSectionAddr) {
  DictScope DS(W, "Loader Section Relocations");

  if (Obj.is64Bit())
    printLoaderSectionRelocationEntriesHelper<LoaderSectionHeader64,
                                              LoaderSectionSymbolEntry64,
                                              LoaderSectionRelocationEntry64>(
        LoaderSectionAddr);
  else
    printLoaderSectionRelocationEntriesHelper<LoaderSectionHeader32,
                                              LoaderSectionSymbolEntry32,
                                              LoaderSectionRelocationEntry32>(
        LoaderSectionAddr);
}

template <typename T>
void XCOFFDumper::printExceptionSectionEntry(const T &ExceptionSectEnt) const {
  if (ExceptionSectEnt.getReason())
    W.printHex("Trap Instr Addr", ExceptionSectEnt.getTrapInstAddr());
  else {
    uint32_t SymIdx = ExceptionSectEnt.getSymbolIndex();
    Expected<StringRef> ErrOrSymbolName = Obj.getSymbolNameByIndex(SymIdx);
    if (Error E = ErrOrSymbolName.takeError()) {
      reportUniqueWarning(std::move(E));
      return;
    }
    StringRef SymName = *ErrOrSymbolName;

    W.printNumber("Symbol", SymName, SymIdx);
  }
  W.printNumber("LangID", ExceptionSectEnt.getLangID());
  W.printNumber("Reason", ExceptionSectEnt.getReason());
}

template <typename T> void XCOFFDumper::printExceptionSectionEntries() const {
  Expected<ArrayRef<T>> ExceptSectEntsOrErr = Obj.getExceptionEntries<T>();
  if (Error E = ExceptSectEntsOrErr.takeError()) {
    reportUniqueWarning(std::move(E));
    return;
  }
  ArrayRef<T> ExceptSectEnts = *ExceptSectEntsOrErr;

  DictScope DS(W, "Exception section");
  if (ExceptSectEnts.empty())
    return;
  for (auto &Ent : ExceptSectEnts)
    printExceptionSectionEntry(Ent);
}

void XCOFFDumper::printExceptionSection() {
  if (Obj.is64Bit())
    printExceptionSectionEntries<ExceptionSectionEntry64>();
  else
    printExceptionSectionEntries<ExceptionSectionEntry32>();
}

void XCOFFDumper::printRelocations() {
  if (Obj.is64Bit())
    printRelocations<XCOFFSectionHeader64, XCOFFRelocation64>(Obj.sections64());
  else
    printRelocations<XCOFFSectionHeader32, XCOFFRelocation32>(Obj.sections32());
}

template <typename RelTy> void XCOFFDumper::printRelocation(RelTy Reloc) {
  Expected<StringRef> ErrOrSymbolName =
      Obj.getSymbolNameByIndex(Reloc.SymbolIndex);
  if (Error E = ErrOrSymbolName.takeError()) {
    reportUniqueWarning(std::move(E));
    return;
  }
  StringRef SymbolName = *ErrOrSymbolName;
  StringRef RelocName = XCOFF::getRelocationTypeString(Reloc.Type);
  if (opts::ExpandRelocs) {
    DictScope Group(W, "Relocation");
    W.printHex("Virtual Address", Reloc.VirtualAddress);
    W.printNumber("Symbol", opts::Demangle ? demangle(SymbolName) : SymbolName,
                  Reloc.SymbolIndex);
    W.printString("IsSigned", Reloc.isRelocationSigned() ? "Yes" : "No");
    W.printNumber("FixupBitValue", Reloc.isFixupIndicated() ? 1 : 0);
    W.printNumber("Length", Reloc.getRelocatedLength());
    W.printEnum("Type", (uint8_t)Reloc.Type, ArrayRef(RelocationTypeNameclass));
  } else {
    raw_ostream &OS = W.startLine();
    OS << W.hex(Reloc.VirtualAddress) << " " << RelocName << " "
       << (opts::Demangle ? demangle(SymbolName) : SymbolName) << "("
       << Reloc.SymbolIndex << ") " << W.hex(Reloc.Info) << "\n";
  }
}

template <typename Shdr, typename RelTy>
void XCOFFDumper::printRelocations(ArrayRef<Shdr> Sections) {
  ListScope LS(W, "Relocations");
  uint16_t Index = 0;
  for (const Shdr &Sec : Sections) {
    ++Index;
    // Only the .text, .data, .tdata, and STYP_DWARF sections have relocation.
    if (Sec.Flags != XCOFF::STYP_TEXT && Sec.Flags != XCOFF::STYP_DATA &&
        Sec.Flags != XCOFF::STYP_TDATA && Sec.Flags != XCOFF::STYP_DWARF)
      continue;
    Expected<ArrayRef<RelTy>> ErrOrRelocations = Obj.relocations<Shdr, RelTy>(Sec);
    if (Error E = ErrOrRelocations.takeError()) {
      reportUniqueWarning(std::move(E));
      continue;
    }

    const ArrayRef<RelTy> Relocations = *ErrOrRelocations;
    if (Relocations.empty())
      continue;

    W.startLine() << "Section (index: " << Index << ") " << Sec.getName()
                  << " {\n";
    W.indent();

    for (const RelTy Reloc : Relocations)
      printRelocation(Reloc);

    W.unindent();
    W.startLine() << "}\n";
  }
}

const EnumEntry<XCOFF::CFileStringType> FileStringType[] = {
#define ECase(X)                                                               \
  { #X, XCOFF::X }
    ECase(XFT_FN), ECase(XFT_CT), ECase(XFT_CV), ECase(XFT_CD)
#undef ECase
};

const EnumEntry<XCOFF::SymbolAuxType> SymAuxType[] = {
#define ECase(X)                                                               \
  { #X, XCOFF::X }
    ECase(AUX_EXCEPT), ECase(AUX_FCN), ECase(AUX_SYM), ECase(AUX_FILE),
    ECase(AUX_CSECT),  ECase(AUX_SECT)
#undef ECase
};

void XCOFFDumper::printFileAuxEnt(const XCOFFFileAuxEnt *AuxEntPtr) {
  assert((!Obj.is64Bit() || AuxEntPtr->AuxType == XCOFF::AUX_FILE) &&
         "Mismatched auxiliary type!");
  StringRef FileName =
      unwrapOrError(Obj.getFileName(), Obj.getCFileName(AuxEntPtr));
  DictScope SymDs(W, "File Auxiliary Entry");
  W.printNumber("Index",
                Obj.getSymbolIndex(reinterpret_cast<uintptr_t>(AuxEntPtr)));
  W.printString("Name", FileName);
  W.printEnum("Type", static_cast<uint8_t>(AuxEntPtr->Type),
              ArrayRef(FileStringType));
  if (Obj.is64Bit()) {
    W.printEnum("Auxiliary Type", static_cast<uint8_t>(AuxEntPtr->AuxType),
                ArrayRef(SymAuxType));
  }
}

static const EnumEntry<XCOFF::StorageMappingClass> CsectStorageMappingClass[] =
    {
#define ECase(X)                                                               \
  { #X, XCOFF::X }
        ECase(XMC_PR), ECase(XMC_RO), ECase(XMC_DB),   ECase(XMC_GL),
        ECase(XMC_XO), ECase(XMC_SV), ECase(XMC_SV64), ECase(XMC_SV3264),
        ECase(XMC_TI), ECase(XMC_TB), ECase(XMC_RW),   ECase(XMC_TC0),
        ECase(XMC_TC), ECase(XMC_TD), ECase(XMC_DS),   ECase(XMC_UA),
        ECase(XMC_BS), ECase(XMC_UC), ECase(XMC_TL),   ECase(XMC_UL),
        ECase(XMC_TE)
#undef ECase
};

const EnumEntry<XCOFF::SymbolType> CsectSymbolTypeClass[] = {
#define ECase(X)                                                               \
  { #X, XCOFF::X }
    ECase(XTY_ER), ECase(XTY_SD), ECase(XTY_LD), ECase(XTY_CM)
#undef ECase
};

void XCOFFDumper::printCsectAuxEnt(XCOFFCsectAuxRef AuxEntRef) {
  assert((!Obj.is64Bit() || AuxEntRef.getAuxType64() == XCOFF::AUX_CSECT) &&
         "Mismatched auxiliary type!");

  DictScope SymDs(W, "CSECT Auxiliary Entry");
  W.printNumber("Index", Obj.getSymbolIndex(AuxEntRef.getEntryAddress()));
  W.printNumber(AuxEntRef.isLabel() ? "ContainingCsectSymbolIndex"
                                    : "SectionLen",
                AuxEntRef.getSectionOrLength());
  W.printHex("ParameterHashIndex", AuxEntRef.getParameterHashIndex());
  W.printHex("TypeChkSectNum", AuxEntRef.getTypeChkSectNum());
  // Print out symbol alignment and type.
  W.printNumber("SymbolAlignmentLog2", AuxEntRef.getAlignmentLog2());
  W.printEnum("SymbolType", AuxEntRef.getSymbolType(),
              ArrayRef(CsectSymbolTypeClass));
  W.printEnum("StorageMappingClass",
              static_cast<uint8_t>(AuxEntRef.getStorageMappingClass()),
              ArrayRef(CsectStorageMappingClass));

  if (Obj.is64Bit()) {
    W.printEnum("Auxiliary Type", static_cast<uint8_t>(XCOFF::AUX_CSECT),
                ArrayRef(SymAuxType));
  } else {
    W.printHex("StabInfoIndex", AuxEntRef.getStabInfoIndex32());
    W.printHex("StabSectNum", AuxEntRef.getStabSectNum32());
  }
}

void XCOFFDumper::printSectAuxEntForStat(
    const XCOFFSectAuxEntForStat *AuxEntPtr) {
  assert(!Obj.is64Bit() && "32-bit interface called on 64-bit object file.");

  DictScope SymDs(W, "Sect Auxiliary Entry For Stat");
  W.printNumber("Index",
                Obj.getSymbolIndex(reinterpret_cast<uintptr_t>(AuxEntPtr)));
  W.printNumber("SectionLength", AuxEntPtr->SectionLength);

  // Unlike the corresponding fields in the section header, NumberOfRelocEnt
  // and NumberOfLineNum do not handle values greater than 65535.
  W.printNumber("NumberOfRelocEnt", AuxEntPtr->NumberOfRelocEnt);
  W.printNumber("NumberOfLineNum", AuxEntPtr->NumberOfLineNum);
}

void XCOFFDumper::printExceptionAuxEnt(const XCOFFExceptionAuxEnt *AuxEntPtr) {
  assert(Obj.is64Bit() && "64-bit interface called on 32-bit object file.");

  DictScope SymDs(W, "Exception Auxiliary Entry");
  W.printNumber("Index",
                Obj.getSymbolIndex(reinterpret_cast<uintptr_t>(AuxEntPtr)));
  W.printHex("OffsetToExceptionTable", AuxEntPtr->OffsetToExceptionTbl);
  W.printHex("SizeOfFunction", AuxEntPtr->SizeOfFunction);
  W.printNumber("SymbolIndexOfNextBeyond", AuxEntPtr->SymIdxOfNextBeyond);
  W.printEnum("Auxiliary Type", static_cast<uint8_t>(AuxEntPtr->AuxType),
              ArrayRef(SymAuxType));
}

void XCOFFDumper::printFunctionAuxEnt(const XCOFFFunctionAuxEnt32 *AuxEntPtr) {
  assert(!Obj.is64Bit() && "32-bit interface called on 64-bit object file.");

  DictScope SymDs(W, "Function Auxiliary Entry");
  W.printNumber("Index",
                Obj.getSymbolIndex(reinterpret_cast<uintptr_t>(AuxEntPtr)));
  W.printHex("OffsetToExceptionTable", AuxEntPtr->OffsetToExceptionTbl);
  W.printHex("SizeOfFunction", AuxEntPtr->SizeOfFunction);
  W.printHex("PointerToLineNum", AuxEntPtr->PtrToLineNum);
  W.printNumber("SymbolIndexOfNextBeyond", AuxEntPtr->SymIdxOfNextBeyond);
}

void XCOFFDumper::printFunctionAuxEnt(const XCOFFFunctionAuxEnt64 *AuxEntPtr) {
  assert(Obj.is64Bit() && "64-bit interface called on 32-bit object file.");

  DictScope SymDs(W, "Function Auxiliary Entry");
  W.printNumber("Index",
                Obj.getSymbolIndex(reinterpret_cast<uintptr_t>(AuxEntPtr)));
  W.printHex("SizeOfFunction", AuxEntPtr->SizeOfFunction);
  W.printHex("PointerToLineNum", AuxEntPtr->PtrToLineNum);
  W.printNumber("SymbolIndexOfNextBeyond", AuxEntPtr->SymIdxOfNextBeyond);
  W.printEnum("Auxiliary Type", static_cast<uint8_t>(AuxEntPtr->AuxType),
              ArrayRef(SymAuxType));
}

void XCOFFDumper::printBlockAuxEnt(const XCOFFBlockAuxEnt32 *AuxEntPtr) {
  assert(!Obj.is64Bit() && "32-bit interface called on 64-bit object file.");

  DictScope SymDs(W, "Block Auxiliary Entry");
  W.printNumber("Index",
                Obj.getSymbolIndex(reinterpret_cast<uintptr_t>(AuxEntPtr)));
  W.printHex("LineNumber (High 2 Bytes)", AuxEntPtr->LineNumHi);
  W.printHex("LineNumber (Low 2 Bytes)", AuxEntPtr->LineNumLo);
}

void XCOFFDumper::printBlockAuxEnt(const XCOFFBlockAuxEnt64 *AuxEntPtr) {
  assert(Obj.is64Bit() && "64-bit interface called on 32-bit object file.");

  DictScope SymDs(W, "Block Auxiliary Entry");
  W.printNumber("Index",
                Obj.getSymbolIndex(reinterpret_cast<uintptr_t>(AuxEntPtr)));
  W.printHex("LineNumber", AuxEntPtr->LineNum);
  W.printEnum("Auxiliary Type", static_cast<uint8_t>(AuxEntPtr->AuxType),
              ArrayRef(SymAuxType));
}

template <typename T>
void XCOFFDumper::printSectAuxEntForDWARF(const T *AuxEntPtr) {
  DictScope SymDs(W, "Sect Auxiliary Entry For DWARF");
  W.printNumber("Index",
                Obj.getSymbolIndex(reinterpret_cast<uintptr_t>(AuxEntPtr)));
  W.printHex("LengthOfSectionPortion", AuxEntPtr->LengthOfSectionPortion);
  W.printNumber("NumberOfRelocEntries", AuxEntPtr->NumberOfRelocEnt);
  if (Obj.is64Bit())
    W.printEnum("Auxiliary Type", static_cast<uint8_t>(XCOFF::AUX_SECT),
                ArrayRef(SymAuxType));
}

static StringRef GetSymbolValueName(XCOFF::StorageClass SC) {
  switch (SC) {
  case XCOFF::C_EXT:
  case XCOFF::C_WEAKEXT:
  case XCOFF::C_HIDEXT:
  case XCOFF::C_STAT:
  case XCOFF::C_FCN:
  case XCOFF::C_BLOCK:
    return "Value (RelocatableAddress)";
  case XCOFF::C_FILE:
    return "Value (SymbolTableIndex)";
  case XCOFF::C_DWARF:
    return "Value (OffsetInDWARF)";
  case XCOFF::C_FUN:
  case XCOFF::C_STSYM:
  case XCOFF::C_BINCL:
  case XCOFF::C_EINCL:
  case XCOFF::C_INFO:
  case XCOFF::C_BSTAT:
  case XCOFF::C_LSYM:
  case XCOFF::C_PSYM:
  case XCOFF::C_RPSYM:
  case XCOFF::C_RSYM:
  case XCOFF::C_ECOML:
    assert(false && "This StorageClass for the symbol is not yet implemented.");
    return "";
  default:
    return "Value";
  }
}

const EnumEntry<XCOFF::CFileLangId> CFileLangIdClass[] = {
#define ECase(X)                                                               \
  { #X, XCOFF::X }
    ECase(TB_C), ECase(TB_Fortran), ECase(TB_CPLUSPLUS)
#undef ECase
};

const EnumEntry<XCOFF::CFileCpuId> CFileCpuIdClass[] = {
#define ECase(X)                                                               \
  { #X, XCOFF::X }
    ECase(TCPU_PPC64), ECase(TCPU_COM), ECase(TCPU_970)
#undef ECase
};

template <typename T> const T *XCOFFDumper::getAuxEntPtr(uintptr_t AuxAddress) {
  const T *AuxEntPtr = reinterpret_cast<const T *>(AuxAddress);
  Obj.checkSymbolEntryPointer(reinterpret_cast<uintptr_t>(AuxEntPtr));
  return AuxEntPtr;
}

static void printUnexpectedRawAuxEnt(ScopedPrinter &W, uintptr_t AuxAddress) {
  W.startLine() << "!Unexpected raw auxiliary entry data:\n";
  W.startLine() << format_bytes(
                       ArrayRef<uint8_t>(
                           reinterpret_cast<const uint8_t *>(AuxAddress),
                           XCOFF::SymbolTableEntrySize),
                       std::nullopt, XCOFF::SymbolTableEntrySize)
                << "\n";
}

void XCOFFDumper::printSymbol(const SymbolRef &S) {
  DataRefImpl SymbolDRI = S.getRawDataRefImpl();
  XCOFFSymbolRef SymbolEntRef = Obj.toSymbolRef(SymbolDRI);

  uint8_t NumberOfAuxEntries = SymbolEntRef.getNumberOfAuxEntries();

  DictScope SymDs(W, "Symbol");

  StringRef SymbolName =
      unwrapOrError(Obj.getFileName(), SymbolEntRef.getName());

  uint32_t SymbolIdx = Obj.getSymbolIndex(SymbolEntRef.getEntryAddress());
  XCOFF::StorageClass SymbolClass = SymbolEntRef.getStorageClass();

  W.printNumber("Index", SymbolIdx);
  W.printString("Name", opts::Demangle ? demangle(SymbolName) : SymbolName);
  W.printHex(GetSymbolValueName(SymbolClass), SymbolEntRef.getValue());

  StringRef SectionName =
      unwrapOrError(Obj.getFileName(), Obj.getSymbolSectionName(SymbolEntRef));

  W.printString("Section", SectionName);
  if (SymbolClass == XCOFF::C_FILE) {
    W.printEnum("Source Language ID", SymbolEntRef.getLanguageIdForCFile(),
                ArrayRef(CFileLangIdClass));
    W.printEnum("CPU Version ID", SymbolEntRef.getCPUTypeIddForCFile(),
                ArrayRef(CFileCpuIdClass));
  } else
    W.printHex("Type", SymbolEntRef.getSymbolType());

  W.printEnum("StorageClass", static_cast<uint8_t>(SymbolClass),
              ArrayRef(SymStorageClass));
  W.printNumber("NumberOfAuxEntries", NumberOfAuxEntries);

  if (NumberOfAuxEntries == 0)
    return;

  auto checkNumOfAux = [=] {
    if (NumberOfAuxEntries > 1)
      reportUniqueWarning("the " +
                          enumToString(static_cast<uint8_t>(SymbolClass),
                                       ArrayRef(SymStorageClass)) +
                          " symbol at index " + Twine(SymbolIdx) +
                          " should not have more than 1 "
                          "auxiliary entry");
  };

  switch (SymbolClass) {
  case XCOFF::C_FILE:
    // If the symbol is C_FILE and has auxiliary entries...
    for (int I = 1; I <= NumberOfAuxEntries; I++) {
      uintptr_t AuxAddress = XCOFFObjectFile::getAdvancedSymbolEntryAddress(
          SymbolEntRef.getEntryAddress(), I);

      if (Obj.is64Bit() &&
          *Obj.getSymbolAuxType(AuxAddress) != XCOFF::SymbolAuxType::AUX_FILE) {
        printUnexpectedRawAuxEnt(W, AuxAddress);
        continue;
      }

      const XCOFFFileAuxEnt *FileAuxEntPtr =
          getAuxEntPtr<XCOFFFileAuxEnt>(AuxAddress);
      printFileAuxEnt(FileAuxEntPtr);
    }
    break;
  case XCOFF::C_EXT:
  case XCOFF::C_WEAKEXT:
  case XCOFF::C_HIDEXT: {
    // For 32-bit objects, print the function auxiliary symbol table entry. The
    // last one must be a CSECT auxiliary entry.
    // For 64-bit objects, both a function auxiliary entry and an exception
    // auxiliary entry may appear, print them in the loop and skip printing the
    // CSECT auxiliary entry, which will be printed outside the loop.
    for (int I = 1; I <= NumberOfAuxEntries; I++) {
      if (I == NumberOfAuxEntries && !Obj.is64Bit())
        break;

      uintptr_t AuxAddress = XCOFFObjectFile::getAdvancedSymbolEntryAddress(
          SymbolEntRef.getEntryAddress(), I);

      if (Obj.is64Bit()) {
        XCOFF::SymbolAuxType Type = *Obj.getSymbolAuxType(AuxAddress);
        if (Type == XCOFF::SymbolAuxType::AUX_CSECT)
          continue;
        if (Type == XCOFF::SymbolAuxType::AUX_FCN) {
          const XCOFFFunctionAuxEnt64 *AuxEntPtr =
              getAuxEntPtr<XCOFFFunctionAuxEnt64>(AuxAddress);
          printFunctionAuxEnt(AuxEntPtr);
        } else if (Type == XCOFF::SymbolAuxType::AUX_EXCEPT) {
          const XCOFFExceptionAuxEnt *AuxEntPtr =
              getAuxEntPtr<XCOFFExceptionAuxEnt>(AuxAddress);
          printExceptionAuxEnt(AuxEntPtr);
        } else {
          printUnexpectedRawAuxEnt(W, AuxAddress);
        }
      } else {
        const XCOFFFunctionAuxEnt32 *AuxEntPtr =
            getAuxEntPtr<XCOFFFunctionAuxEnt32>(AuxAddress);
        printFunctionAuxEnt(AuxEntPtr);
      }
    }

    // Print the CSECT auxiliary entry.
    auto ErrOrCsectAuxRef = SymbolEntRef.getXCOFFCsectAuxRef();
    if (!ErrOrCsectAuxRef)
      reportUniqueWarning(ErrOrCsectAuxRef.takeError());
    else
      printCsectAuxEnt(*ErrOrCsectAuxRef);

    break;
  }
  case XCOFF::C_STAT: {
    checkNumOfAux();

    const XCOFFSectAuxEntForStat *StatAuxEntPtr =
        getAuxEntPtr<XCOFFSectAuxEntForStat>(
            XCOFFObjectFile::getAdvancedSymbolEntryAddress(
                SymbolEntRef.getEntryAddress(), 1));
    printSectAuxEntForStat(StatAuxEntPtr);
    break;
  }
  case XCOFF::C_DWARF: {
    checkNumOfAux();

    uintptr_t AuxAddress = XCOFFObjectFile::getAdvancedSymbolEntryAddress(
        SymbolEntRef.getEntryAddress(), 1);

    if (Obj.is64Bit()) {
      const XCOFFSectAuxEntForDWARF64 *AuxEntPtr =
          getAuxEntPtr<XCOFFSectAuxEntForDWARF64>(AuxAddress);
      printSectAuxEntForDWARF<XCOFFSectAuxEntForDWARF64>(AuxEntPtr);
    } else {
      const XCOFFSectAuxEntForDWARF32 *AuxEntPtr =
          getAuxEntPtr<XCOFFSectAuxEntForDWARF32>(AuxAddress);
      printSectAuxEntForDWARF<XCOFFSectAuxEntForDWARF32>(AuxEntPtr);
    }
    break;
  }
  case XCOFF::C_BLOCK:
  case XCOFF::C_FCN: {
    checkNumOfAux();

    uintptr_t AuxAddress = XCOFFObjectFile::getAdvancedSymbolEntryAddress(
        SymbolEntRef.getEntryAddress(), 1);

    if (Obj.is64Bit()) {
      const XCOFFBlockAuxEnt64 *AuxEntPtr =
          getAuxEntPtr<XCOFFBlockAuxEnt64>(AuxAddress);
      printBlockAuxEnt(AuxEntPtr);
    } else {
      const XCOFFBlockAuxEnt32 *AuxEntPtr =
          getAuxEntPtr<XCOFFBlockAuxEnt32>(AuxAddress);
      printBlockAuxEnt(AuxEntPtr);
    }
    break;
  }
  default:
    for (int i = 1; i <= NumberOfAuxEntries; i++) {
      printUnexpectedRawAuxEnt(W,
                               XCOFFObjectFile::getAdvancedSymbolEntryAddress(
                                   SymbolEntRef.getEntryAddress(), i));
    }
    break;
  }
}

void XCOFFDumper::printSymbols(bool /*ExtraSymInfo*/) {
  ListScope Group(W, "Symbols");
  for (const SymbolRef &S : Obj.symbols())
    printSymbol(S);
}

void XCOFFDumper::printStringTable() {
  DictScope DS(W, "StringTable");
  StringRef StrTable = Obj.getStringTable();
  uint32_t StrTabSize = StrTable.size();
  W.printNumber("Length", StrTabSize);
  // Print strings from the fifth byte, since the first four bytes contain the
  // length (in bytes) of the string table (including the length field).
  if (StrTabSize > 4)
    printAsStringList(StrTable, 4);
}

void XCOFFDumper::printDynamicSymbols() {
  llvm_unreachable("Unimplemented functionality for XCOFFDumper");
}

void XCOFFDumper::printUnwindInfo() {
  llvm_unreachable("Unimplemented functionality for XCOFFDumper");
}

void XCOFFDumper::printStackMap() const {
  llvm_unreachable("Unimplemented functionality for XCOFFDumper");
}

void XCOFFDumper::printNeededLibraries() {
  ListScope D(W, "NeededLibraries");
  auto ImportFilesOrError = Obj.getImportFileTable();
  if (!ImportFilesOrError) {
    reportUniqueWarning(ImportFilesOrError.takeError());
    return;
  }

  StringRef ImportFileTable = ImportFilesOrError.get();
  const char *CurrentStr = ImportFileTable.data();
  const char *TableEnd = ImportFileTable.end();
  // Default column width for names is 13 even if no names are that long.
  size_t BaseWidth = 13;

  // Get the max width of BASE columns.
  for (size_t StrIndex = 0; CurrentStr < TableEnd; ++StrIndex) {
    size_t CurrentLen = strlen(CurrentStr);
    CurrentStr += strlen(CurrentStr) + 1;
    if (StrIndex % 3 == 1)
      BaseWidth = std::max(BaseWidth, CurrentLen);
  }

  auto &OS = static_cast<formatted_raw_ostream &>(W.startLine());
  // Each entry consists of 3 strings: the path_name, base_name and
  // archive_member_name. The first entry is a default LIBPATH value and other
  // entries have no path_name. We just dump the base_name and
  // archive_member_name here.
  OS << left_justify("BASE", BaseWidth)  << " MEMBER\n";
  CurrentStr = ImportFileTable.data();
  for (size_t StrIndex = 0; CurrentStr < TableEnd;
       ++StrIndex, CurrentStr += strlen(CurrentStr) + 1) {
    if (StrIndex >= 3 && StrIndex % 3 != 0) {
      if (StrIndex % 3 == 1)
        OS << "  " << left_justify(CurrentStr, BaseWidth) << " ";
      else
        OS << CurrentStr << "\n";
    }
  }
}

const EnumEntry<XCOFF::SectionTypeFlags> SectionTypeFlagsNames[] = {
#define ECase(X)                                                               \
  { #X, XCOFF::X }
    ECase(STYP_PAD),    ECase(STYP_DWARF), ECase(STYP_TEXT),
    ECase(STYP_DATA),   ECase(STYP_BSS),   ECase(STYP_EXCEPT),
    ECase(STYP_INFO),   ECase(STYP_TDATA), ECase(STYP_TBSS),
    ECase(STYP_LOADER), ECase(STYP_DEBUG), ECase(STYP_TYPCHK),
    ECase(STYP_OVRFLO)
#undef ECase
};

const EnumEntry<XCOFF::DwarfSectionSubtypeFlags>
    DWARFSectionSubtypeFlagsNames[] = {
#define ECase(X)                                                               \
  { #X, XCOFF::X }
        ECase(SSUBTYP_DWINFO),  ECase(SSUBTYP_DWLINE),  ECase(SSUBTYP_DWPBNMS),
        ECase(SSUBTYP_DWPBTYP), ECase(SSUBTYP_DWARNGE), ECase(SSUBTYP_DWABREV),
        ECase(SSUBTYP_DWSTR),   ECase(SSUBTYP_DWRNGES), ECase(SSUBTYP_DWLOC),
        ECase(SSUBTYP_DWFRAME), ECase(SSUBTYP_DWMAC)
#undef ECase
};

template <typename T>
void XCOFFDumper::printOverflowSectionHeader(T &Sec) const {
  if (Obj.is64Bit()) {
    reportWarning(make_error<StringError>("An 64-bit XCOFF object file may not "
                                          "contain an overflow section header.",
                                          object_error::parse_failed),
                  Obj.getFileName());
  }

  W.printString("Name", Sec.getName());
  W.printNumber("NumberOfRelocations", Sec.PhysicalAddress);
  W.printNumber("NumberOfLineNumbers", Sec.VirtualAddress);
  W.printHex("Size", Sec.SectionSize);
  W.printHex("RawDataOffset", Sec.FileOffsetToRawData);
  W.printHex("RelocationPointer", Sec.FileOffsetToRelocationInfo);
  W.printHex("LineNumberPointer", Sec.FileOffsetToLineNumberInfo);
  W.printNumber("IndexOfSectionOverflowed", Sec.NumberOfRelocations);
  W.printNumber("IndexOfSectionOverflowed", Sec.NumberOfLineNumbers);
}

template <typename T>
void XCOFFDumper::printGenericSectionHeader(T &Sec) const {
  W.printString("Name", Sec.getName());
  W.printHex("PhysicalAddress", Sec.PhysicalAddress);
  W.printHex("VirtualAddress", Sec.VirtualAddress);
  W.printHex("Size", Sec.SectionSize);
  W.printHex("RawDataOffset", Sec.FileOffsetToRawData);
  W.printHex("RelocationPointer", Sec.FileOffsetToRelocationInfo);
  W.printHex("LineNumberPointer", Sec.FileOffsetToLineNumberInfo);
  W.printNumber("NumberOfRelocations", Sec.NumberOfRelocations);
  W.printNumber("NumberOfLineNumbers", Sec.NumberOfLineNumbers);
}

enum PrintStyle { Hex, Number };
template <typename T, typename V>
static void printAuxMemberHelper(PrintStyle Style, const char *MemberName,
                                 const T &Member, const V *AuxHeader,
                                 uint16_t AuxSize, uint16_t &PartialFieldOffset,
                                 const char *&PartialFieldName,
                                 ScopedPrinter &W) {
  ptrdiff_t Offset = reinterpret_cast<const char *>(&Member) -
                     reinterpret_cast<const char *>(AuxHeader);
  if (Offset + sizeof(Member) <= AuxSize)
    Style == Hex ? W.printHex(MemberName, Member)
                 : W.printNumber(MemberName, Member);
  else if (Offset < AuxSize) {
    PartialFieldOffset = Offset;
    PartialFieldName = MemberName;
  }
}

template <class T>
void checkAndPrintAuxHeaderParseError(const char *PartialFieldName,
                                      uint16_t PartialFieldOffset,
                                      uint16_t AuxSize, T &AuxHeader,
                                      XCOFFDumper *Dumper) {
  if (PartialFieldOffset < AuxSize) {
    Dumper->reportUniqueWarning(Twine("only partial field for ") +
                                PartialFieldName + " at offset (" +
                                Twine(PartialFieldOffset) + ")");
    Dumper->getScopedPrinter().printBinary(
        "Raw data", "",
        ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(&AuxHeader) +
                              PartialFieldOffset,
                          AuxSize - PartialFieldOffset));
  } else if (sizeof(AuxHeader) < AuxSize)
    Dumper->getScopedPrinter().printBinary(
        "Extra raw data", "",
        ArrayRef<uint8_t>(reinterpret_cast<const uint8_t *>(&AuxHeader) +
                              sizeof(AuxHeader),
                          AuxSize - sizeof(AuxHeader)));
}

void XCOFFDumper::printAuxiliaryHeader(
    const XCOFFAuxiliaryHeader32 *AuxHeader) {
  if (AuxHeader == nullptr)
    return;
  uint16_t AuxSize = Obj.getOptionalHeaderSize();
  uint16_t PartialFieldOffset = AuxSize;
  const char *PartialFieldName = nullptr;

  auto PrintAuxMember = [&](PrintStyle Style, const char *MemberName,
                            auto &Member) {
    printAuxMemberHelper(Style, MemberName, Member, AuxHeader, AuxSize,
                         PartialFieldOffset, PartialFieldName, W);
  };

  PrintAuxMember(Hex, "Magic", AuxHeader->AuxMagic);
  PrintAuxMember(Hex, "Version", AuxHeader->Version);
  PrintAuxMember(Hex, "Size of .text section", AuxHeader->TextSize);
  PrintAuxMember(Hex, "Size of .data section", AuxHeader->InitDataSize);
  PrintAuxMember(Hex, "Size of .bss section", AuxHeader->BssDataSize);
  PrintAuxMember(Hex, "Entry point address", AuxHeader->EntryPointAddr);
  PrintAuxMember(Hex, ".text section start address", AuxHeader->TextStartAddr);
  PrintAuxMember(Hex, ".data section start address", AuxHeader->DataStartAddr);
  PrintAuxMember(Hex, "TOC anchor address", AuxHeader->TOCAnchorAddr);
  PrintAuxMember(Number, "Section number of entryPoint",
                 AuxHeader->SecNumOfEntryPoint);
  PrintAuxMember(Number, "Section number of .text", AuxHeader->SecNumOfText);
  PrintAuxMember(Number, "Section number of .data", AuxHeader->SecNumOfData);
  PrintAuxMember(Number, "Section number of TOC", AuxHeader->SecNumOfTOC);
  PrintAuxMember(Number, "Section number of loader data",
                 AuxHeader->SecNumOfLoader);
  PrintAuxMember(Number, "Section number of .bss", AuxHeader->SecNumOfBSS);
  PrintAuxMember(Hex, "Maxium alignment of .text", AuxHeader->MaxAlignOfText);
  PrintAuxMember(Hex, "Maxium alignment of .data", AuxHeader->MaxAlignOfData);
  PrintAuxMember(Hex, "Module type", AuxHeader->ModuleType);
  PrintAuxMember(Hex, "CPU type of objects", AuxHeader->CpuFlag);
  PrintAuxMember(Hex, "(Reserved)", AuxHeader->CpuType);
  PrintAuxMember(Hex, "Maximum stack size", AuxHeader->MaxStackSize);
  PrintAuxMember(Hex, "Maximum data size", AuxHeader->MaxDataSize);
  PrintAuxMember(Hex, "Reserved for debugger", AuxHeader->ReservedForDebugger);
  PrintAuxMember(Hex, "Text page size", AuxHeader->TextPageSize);
  PrintAuxMember(Hex, "Data page size", AuxHeader->DataPageSize);
  PrintAuxMember(Hex, "Stack page size", AuxHeader->StackPageSize);
  if (offsetof(XCOFFAuxiliaryHeader32, FlagAndTDataAlignment) +
          sizeof(XCOFFAuxiliaryHeader32::FlagAndTDataAlignment) <=
      AuxSize) {
    W.printHex("Flag", AuxHeader->getFlag());
    W.printHex("Alignment of thread-local storage",
               AuxHeader->getTDataAlignment());
  }

  PrintAuxMember(Number, "Section number for .tdata", AuxHeader->SecNumOfTData);
  PrintAuxMember(Number, "Section number for .tbss", AuxHeader->SecNumOfTBSS);

  checkAndPrintAuxHeaderParseError(PartialFieldName, PartialFieldOffset,
                                   AuxSize, *AuxHeader, this);
}

void XCOFFDumper::printAuxiliaryHeader(
    const XCOFFAuxiliaryHeader64 *AuxHeader) {
  if (AuxHeader == nullptr)
    return;
  uint16_t AuxSize = Obj.getOptionalHeaderSize();
  uint16_t PartialFieldOffset = AuxSize;
  const char *PartialFieldName = nullptr;

  auto PrintAuxMember = [&](PrintStyle Style, const char *MemberName,
                            auto &Member) {
    printAuxMemberHelper(Style, MemberName, Member, AuxHeader, AuxSize,
                         PartialFieldOffset, PartialFieldName, W);
  };

  PrintAuxMember(Hex, "Magic", AuxHeader->AuxMagic);
  PrintAuxMember(Hex, "Version", AuxHeader->Version);
  PrintAuxMember(Hex, "Reserved for debugger", AuxHeader->ReservedForDebugger);
  PrintAuxMember(Hex, ".text section start address", AuxHeader->TextStartAddr);
  PrintAuxMember(Hex, ".data section start address", AuxHeader->DataStartAddr);
  PrintAuxMember(Hex, "TOC anchor address", AuxHeader->TOCAnchorAddr);
  PrintAuxMember(Number, "Section number of entryPoint",
                 AuxHeader->SecNumOfEntryPoint);
  PrintAuxMember(Number, "Section number of .text", AuxHeader->SecNumOfText);
  PrintAuxMember(Number, "Section number of .data", AuxHeader->SecNumOfData);
  PrintAuxMember(Number, "Section number of TOC", AuxHeader->SecNumOfTOC);
  PrintAuxMember(Number, "Section number of loader data",
                 AuxHeader->SecNumOfLoader);
  PrintAuxMember(Number, "Section number of .bss", AuxHeader->SecNumOfBSS);
  PrintAuxMember(Hex, "Maxium alignment of .text", AuxHeader->MaxAlignOfText);
  PrintAuxMember(Hex, "Maxium alignment of .data", AuxHeader->MaxAlignOfData);
  PrintAuxMember(Hex, "Module type", AuxHeader->ModuleType);
  PrintAuxMember(Hex, "CPU type of objects", AuxHeader->CpuFlag);
  PrintAuxMember(Hex, "(Reserved)", AuxHeader->CpuType);
  PrintAuxMember(Hex, "Text page size", AuxHeader->TextPageSize);
  PrintAuxMember(Hex, "Data page size", AuxHeader->DataPageSize);
  PrintAuxMember(Hex, "Stack page size", AuxHeader->StackPageSize);
  if (offsetof(XCOFFAuxiliaryHeader64, FlagAndTDataAlignment) +
          sizeof(XCOFFAuxiliaryHeader64::FlagAndTDataAlignment) <=
      AuxSize) {
    W.printHex("Flag", AuxHeader->getFlag());
    W.printHex("Alignment of thread-local storage",
               AuxHeader->getTDataAlignment());
  }
  PrintAuxMember(Hex, "Size of .text section", AuxHeader->TextSize);
  PrintAuxMember(Hex, "Size of .data section", AuxHeader->InitDataSize);
  PrintAuxMember(Hex, "Size of .bss section", AuxHeader->BssDataSize);
  PrintAuxMember(Hex, "Entry point address", AuxHeader->EntryPointAddr);
  PrintAuxMember(Hex, "Maximum stack size", AuxHeader->MaxStackSize);
  PrintAuxMember(Hex, "Maximum data size", AuxHeader->MaxDataSize);
  PrintAuxMember(Number, "Section number for .tdata", AuxHeader->SecNumOfTData);
  PrintAuxMember(Number, "Section number for .tbss", AuxHeader->SecNumOfTBSS);
  PrintAuxMember(Hex, "Additional flags 64-bit XCOFF", AuxHeader->XCOFF64Flag);

  checkAndPrintAuxHeaderParseError(PartialFieldName, PartialFieldOffset,
                                   AuxSize, *AuxHeader, this);
}

template <typename T>
void XCOFFDumper::printSectionHeaders(ArrayRef<T> Sections) {
  ListScope Group(W, "Sections");

  uint16_t Index = 1;
  for (const T &Sec : Sections) {
    DictScope SecDS(W, "Section");

    W.printNumber("Index", Index++);
    uint16_t SectionType = Sec.getSectionType();
    int32_t SectionSubtype = Sec.getSectionSubtype();
    switch (SectionType) {
    case XCOFF::STYP_OVRFLO:
      printOverflowSectionHeader(Sec);
      break;
    case XCOFF::STYP_LOADER:
    case XCOFF::STYP_EXCEPT:
    case XCOFF::STYP_TYPCHK:
      // TODO The interpretation of loader, exception and type check section
      // headers are different from that of generic section headers. We will
      // implement them later. We interpret them as generic section headers for
      // now.
    default:
      printGenericSectionHeader(Sec);
      break;
    }
    if (Sec.isReservedSectionType())
      W.printHex("Flags", "Reserved", SectionType);
    else {
      W.printEnum("Type", SectionType, ArrayRef(SectionTypeFlagsNames));
      if (SectionType == XCOFF::STYP_DWARF) {
        W.printEnum("DWARFSubType", SectionSubtype,
                    ArrayRef(DWARFSectionSubtypeFlagsNames));
      }
    }
  }

  if (opts::SectionRelocations)
    report_fatal_error("Dumping section relocations is unimplemented");

  if (opts::SectionSymbols)
    report_fatal_error("Dumping symbols is unimplemented");

  if (opts::SectionData)
    report_fatal_error("Dumping section data is unimplemented");
}

namespace llvm {
std::unique_ptr<ObjDumper>
createXCOFFDumper(const object::XCOFFObjectFile &XObj, ScopedPrinter &Writer) {
  return std::make_unique<XCOFFDumper>(XObj, Writer);
}
} // namespace llvm
