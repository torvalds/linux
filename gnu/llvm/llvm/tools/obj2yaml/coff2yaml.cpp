//===------ utils/obj2yaml.cpp - obj2yaml conversion tool -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "obj2yaml.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/DebugInfo/CodeView/DebugChecksumsSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugStringTableSubsection.h"
#include "llvm/DebugInfo/CodeView/StringsAndChecksums.h"
#include "llvm/Object/COFF.h"
#include "llvm/ObjectYAML/COFFYAML.h"
#include "llvm/ObjectYAML/CodeViewYAMLTypes.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/YAMLTraits.h"

using namespace llvm;

namespace {

class COFFDumper {
  const object::COFFObjectFile &Obj;
  COFFYAML::Object YAMLObj;
  template <typename T>
  void dumpOptionalHeader(T OptionalHeader);
  void dumpHeader();
  void dumpSections(unsigned numSections);
  void dumpSymbols(unsigned numSymbols);

public:
  COFFDumper(const object::COFFObjectFile &Obj);
  COFFYAML::Object &getYAMLObj();
};

}

COFFDumper::COFFDumper(const object::COFFObjectFile &Obj) : Obj(Obj) {
  if (const object::pe32_header *PE32Header = Obj.getPE32Header())
    dumpOptionalHeader(PE32Header);
  else if (const object::pe32plus_header *PE32PlusHeader =
               Obj.getPE32PlusHeader())
    dumpOptionalHeader(PE32PlusHeader);

  dumpHeader();
  dumpSections(Obj.getNumberOfSections());
  dumpSymbols(Obj.getNumberOfSymbols());
}

template <typename T> void COFFDumper::dumpOptionalHeader(T OptionalHeader) {
  YAMLObj.OptionalHeader = COFFYAML::PEHeader();
  YAMLObj.OptionalHeader->Header.AddressOfEntryPoint =
      OptionalHeader->AddressOfEntryPoint;
  YAMLObj.OptionalHeader->Header.ImageBase = OptionalHeader->ImageBase;
  YAMLObj.OptionalHeader->Header.SectionAlignment =
      OptionalHeader->SectionAlignment;
  YAMLObj.OptionalHeader->Header.FileAlignment = OptionalHeader->FileAlignment;
  YAMLObj.OptionalHeader->Header.MajorOperatingSystemVersion =
      OptionalHeader->MajorOperatingSystemVersion;
  YAMLObj.OptionalHeader->Header.MinorOperatingSystemVersion =
      OptionalHeader->MinorOperatingSystemVersion;
  YAMLObj.OptionalHeader->Header.MajorImageVersion =
      OptionalHeader->MajorImageVersion;
  YAMLObj.OptionalHeader->Header.MinorImageVersion =
      OptionalHeader->MinorImageVersion;
  YAMLObj.OptionalHeader->Header.MajorSubsystemVersion =
      OptionalHeader->MajorSubsystemVersion;
  YAMLObj.OptionalHeader->Header.MinorSubsystemVersion =
      OptionalHeader->MinorSubsystemVersion;
  YAMLObj.OptionalHeader->Header.Subsystem = OptionalHeader->Subsystem;
  YAMLObj.OptionalHeader->Header.DLLCharacteristics =
      OptionalHeader->DLLCharacteristics;
  YAMLObj.OptionalHeader->Header.SizeOfStackReserve =
      OptionalHeader->SizeOfStackReserve;
  YAMLObj.OptionalHeader->Header.SizeOfStackCommit =
      OptionalHeader->SizeOfStackCommit;
  YAMLObj.OptionalHeader->Header.SizeOfHeapReserve =
      OptionalHeader->SizeOfHeapReserve;
  YAMLObj.OptionalHeader->Header.SizeOfHeapCommit =
      OptionalHeader->SizeOfHeapCommit;
  YAMLObj.OptionalHeader->Header.NumberOfRvaAndSize =
      OptionalHeader->NumberOfRvaAndSize;
  unsigned I = 0;
  for (auto &DestDD : YAMLObj.OptionalHeader->DataDirectories) {
    const object::data_directory *DD = Obj.getDataDirectory(I++);
    if (!DD)
      continue;
    DestDD = COFF::DataDirectory();
    DestDD->RelativeVirtualAddress = DD->RelativeVirtualAddress;
    DestDD->Size = DD->Size;
  }
}

void COFFDumper::dumpHeader() {
  YAMLObj.Header.Machine = Obj.getMachine();
  YAMLObj.Header.Characteristics = Obj.getCharacteristics();
}

static void
initializeFileAndStringTable(const llvm::object::COFFObjectFile &Obj,
                             codeview::StringsAndChecksumsRef &SC) {

  ExitOnError Err("invalid .debug$S section");
  // Iterate all .debug$S sections looking for the checksums and string table.
  // Exit as soon as both sections are found.
  for (const auto &S : Obj.sections()) {
    if (SC.hasStrings() && SC.hasChecksums())
      break;

    Expected<StringRef> SectionNameOrErr = S.getName();
    if (!SectionNameOrErr) {
      consumeError(SectionNameOrErr.takeError());
      continue;
    }

    ArrayRef<uint8_t> sectionData;
    if ((*SectionNameOrErr) != ".debug$S")
      continue;

    const object::coff_section *COFFSection = Obj.getCOFFSection(S);

    cantFail(Obj.getSectionContents(COFFSection, sectionData));

    BinaryStreamReader Reader(sectionData, llvm::endianness::little);
    uint32_t Magic;

    Err(Reader.readInteger(Magic));
    assert(Magic == COFF::DEBUG_SECTION_MAGIC && "Invalid .debug$S section!");

    codeview::DebugSubsectionArray Subsections;
    Err(Reader.readArray(Subsections, Reader.bytesRemaining()));

    SC.initialize(Subsections);
  }
}

void COFFDumper::dumpSections(unsigned NumSections) {
  std::vector<COFFYAML::Section> &YAMLSections = YAMLObj.Sections;
  codeview::StringsAndChecksumsRef SC;
  initializeFileAndStringTable(Obj, SC);

  ExitOnError Err("invalid section table");
  StringMap<bool> SymbolUnique;
  for (const auto &S : Obj.symbols()) {
    StringRef Name = Err(Obj.getSymbolName(Obj.getCOFFSymbol(S)));
    StringMap<bool>::iterator It;
    bool Inserted;
    std::tie(It, Inserted) = SymbolUnique.insert(std::make_pair(Name, true));
    if (!Inserted)
      It->second = false;
  }

  for (const auto &ObjSection : Obj.sections()) {
    const object::coff_section *COFFSection = Obj.getCOFFSection(ObjSection);
    COFFYAML::Section NewYAMLSection;

    if (Expected<StringRef> NameOrErr = ObjSection.getName())
      NewYAMLSection.Name = *NameOrErr;
    else
      consumeError(NameOrErr.takeError());

    NewYAMLSection.Header.Characteristics = COFFSection->Characteristics;
    NewYAMLSection.Header.VirtualAddress = COFFSection->VirtualAddress;
    NewYAMLSection.Header.VirtualSize = COFFSection->VirtualSize;
    NewYAMLSection.Header.NumberOfLineNumbers =
        COFFSection->NumberOfLinenumbers;
    NewYAMLSection.Header.NumberOfRelocations =
        COFFSection->NumberOfRelocations;
    NewYAMLSection.Header.PointerToLineNumbers =
        COFFSection->PointerToLinenumbers;
    NewYAMLSection.Header.PointerToRawData = COFFSection->PointerToRawData;
    NewYAMLSection.Header.PointerToRelocations =
        COFFSection->PointerToRelocations;
    NewYAMLSection.Header.SizeOfRawData = COFFSection->SizeOfRawData;
    uint32_t Shift = (COFFSection->Characteristics >> 20) & 0xF;
    NewYAMLSection.Alignment = (1U << Shift) >> 1;
    assert(NewYAMLSection.Alignment <= 8192);

    ArrayRef<uint8_t> sectionData;
    if (!ObjSection.isBSS())
      cantFail(Obj.getSectionContents(COFFSection, sectionData));
    NewYAMLSection.SectionData = yaml::BinaryRef(sectionData);

    if (NewYAMLSection.Name == ".debug$S")
      NewYAMLSection.DebugS = CodeViewYAML::fromDebugS(sectionData, SC);
    else if (NewYAMLSection.Name == ".debug$T")
      NewYAMLSection.DebugT = CodeViewYAML::fromDebugT(sectionData,
                                                       NewYAMLSection.Name);
    else if (NewYAMLSection.Name == ".debug$P")
      NewYAMLSection.DebugP = CodeViewYAML::fromDebugT(sectionData,
                                                       NewYAMLSection.Name);
    else if (NewYAMLSection.Name == ".debug$H")
      NewYAMLSection.DebugH = CodeViewYAML::fromDebugH(sectionData);

    std::vector<COFFYAML::Relocation> Relocations;
    for (const auto &Reloc : ObjSection.relocations()) {
      const object::coff_relocation *reloc = Obj.getCOFFRelocation(Reloc);
      COFFYAML::Relocation Rel;
      object::symbol_iterator Sym = Reloc.getSymbol();
      Expected<StringRef> SymbolNameOrErr = Sym->getName();
      if (!SymbolNameOrErr) {
       std::string Buf;
       raw_string_ostream OS(Buf);
       logAllUnhandledErrors(SymbolNameOrErr.takeError(), OS);
       report_fatal_error(Twine(OS.str()));
      }
      if (SymbolUnique.lookup(*SymbolNameOrErr))
        Rel.SymbolName = *SymbolNameOrErr;
      else
        Rel.SymbolTableIndex = reloc->SymbolTableIndex;
      Rel.VirtualAddress = reloc->VirtualAddress;
      Rel.Type = reloc->Type;
      Relocations.push_back(Rel);
    }
    NewYAMLSection.Relocations = Relocations;
    YAMLSections.push_back(NewYAMLSection);
  }
}

static void
dumpFunctionDefinition(COFFYAML::Symbol *Sym,
                       const object::coff_aux_function_definition *ObjFD) {
  COFF::AuxiliaryFunctionDefinition YAMLFD;
  YAMLFD.TagIndex = ObjFD->TagIndex;
  YAMLFD.TotalSize = ObjFD->TotalSize;
  YAMLFD.PointerToLinenumber = ObjFD->PointerToLinenumber;
  YAMLFD.PointerToNextFunction = ObjFD->PointerToNextFunction;

  Sym->FunctionDefinition = YAMLFD;
}

static void
dumpbfAndEfLineInfo(COFFYAML::Symbol *Sym,
                    const object::coff_aux_bf_and_ef_symbol *ObjBES) {
  COFF::AuxiliarybfAndefSymbol YAMLAAS;
  YAMLAAS.Linenumber = ObjBES->Linenumber;
  YAMLAAS.PointerToNextFunction = ObjBES->PointerToNextFunction;

  Sym->bfAndefSymbol = YAMLAAS;
}

static void dumpWeakExternal(COFFYAML::Symbol *Sym,
                             const object::coff_aux_weak_external *ObjWE) {
  COFF::AuxiliaryWeakExternal YAMLWE;
  YAMLWE.TagIndex = ObjWE->TagIndex;
  YAMLWE.Characteristics = ObjWE->Characteristics;

  Sym->WeakExternal = YAMLWE;
}

static void
dumpSectionDefinition(COFFYAML::Symbol *Sym,
                      const object::coff_aux_section_definition *ObjSD,
                      bool IsBigObj) {
  COFF::AuxiliarySectionDefinition YAMLASD;
  int32_t AuxNumber = ObjSD->getNumber(IsBigObj);
  YAMLASD.Length = ObjSD->Length;
  YAMLASD.NumberOfRelocations = ObjSD->NumberOfRelocations;
  YAMLASD.NumberOfLinenumbers = ObjSD->NumberOfLinenumbers;
  YAMLASD.CheckSum = ObjSD->CheckSum;
  YAMLASD.Number = AuxNumber;
  YAMLASD.Selection = ObjSD->Selection;

  Sym->SectionDefinition = YAMLASD;
}

static void
dumpCLRTokenDefinition(COFFYAML::Symbol *Sym,
                       const object::coff_aux_clr_token *ObjCLRToken) {
  COFF::AuxiliaryCLRToken YAMLCLRToken;
  YAMLCLRToken.AuxType = ObjCLRToken->AuxType;
  YAMLCLRToken.SymbolTableIndex = ObjCLRToken->SymbolTableIndex;

  Sym->CLRToken = YAMLCLRToken;
}

void COFFDumper::dumpSymbols(unsigned NumSymbols) {
  ExitOnError Err("invalid symbol table");

  std::vector<COFFYAML::Symbol> &Symbols = YAMLObj.Symbols;
  for (const auto &S : Obj.symbols()) {
    object::COFFSymbolRef Symbol = Obj.getCOFFSymbol(S);
    COFFYAML::Symbol Sym;
    Sym.Name = Err(Obj.getSymbolName(Symbol));
    Sym.SimpleType = COFF::SymbolBaseType(Symbol.getBaseType());
    Sym.ComplexType = COFF::SymbolComplexType(Symbol.getComplexType());
    Sym.Header.StorageClass = Symbol.getStorageClass();
    Sym.Header.Value = Symbol.getValue();
    Sym.Header.SectionNumber = Symbol.getSectionNumber();
    Sym.Header.NumberOfAuxSymbols = Symbol.getNumberOfAuxSymbols();

    if (Symbol.getNumberOfAuxSymbols() > 0) {
      ArrayRef<uint8_t> AuxData = Obj.getSymbolAuxData(Symbol);
      if (Symbol.isFunctionDefinition()) {
        // This symbol represents a function definition.
        assert(Symbol.getNumberOfAuxSymbols() == 1 &&
               "Expected a single aux symbol to describe this function!");

        const object::coff_aux_function_definition *ObjFD =
            reinterpret_cast<const object::coff_aux_function_definition *>(
                AuxData.data());
        dumpFunctionDefinition(&Sym, ObjFD);
      } else if (Symbol.isFunctionLineInfo()) {
        // This symbol describes function line number information.
        assert(Symbol.getNumberOfAuxSymbols() == 1 &&
               "Expected a single aux symbol to describe this function!");

        const object::coff_aux_bf_and_ef_symbol *ObjBES =
            reinterpret_cast<const object::coff_aux_bf_and_ef_symbol *>(
                AuxData.data());
        dumpbfAndEfLineInfo(&Sym, ObjBES);
      } else if (Symbol.isAnyUndefined()) {
        // This symbol represents a weak external definition.
        assert(Symbol.getNumberOfAuxSymbols() == 1 &&
               "Expected a single aux symbol to describe this weak symbol!");

        const object::coff_aux_weak_external *ObjWE =
            reinterpret_cast<const object::coff_aux_weak_external *>(
                AuxData.data());
        dumpWeakExternal(&Sym, ObjWE);
      } else if (Symbol.isFileRecord()) {
        // This symbol represents a file record.
        Sym.File = StringRef(reinterpret_cast<const char *>(AuxData.data()),
                             Symbol.getNumberOfAuxSymbols() *
                                 Obj.getSymbolTableEntrySize())
                       .rtrim(StringRef("\0", /*length=*/1));
      } else if (Symbol.isSectionDefinition()) {
        // This symbol represents a section definition.
        assert(Symbol.getNumberOfAuxSymbols() == 1 &&
               "Expected a single aux symbol to describe this section!");

        const object::coff_aux_section_definition *ObjSD =
            reinterpret_cast<const object::coff_aux_section_definition *>(
                AuxData.data());
        dumpSectionDefinition(&Sym, ObjSD, Symbol.isBigObj());
      } else if (Symbol.isCLRToken()) {
        // This symbol represents a CLR token definition.
        assert(Symbol.getNumberOfAuxSymbols() == 1 &&
               "Expected a single aux symbol to describe this CLR Token!");

        const object::coff_aux_clr_token *ObjCLRToken =
            reinterpret_cast<const object::coff_aux_clr_token *>(
                AuxData.data());
        dumpCLRTokenDefinition(&Sym, ObjCLRToken);
      } else {
        llvm_unreachable("Unhandled auxiliary symbol!");
      }
    }
    Symbols.push_back(Sym);
  }
}

COFFYAML::Object &COFFDumper::getYAMLObj() {
  return YAMLObj;
}

std::error_code coff2yaml(raw_ostream &Out, const object::COFFObjectFile &Obj) {
  COFFDumper Dumper(Obj);

  yaml::Output Yout(Out);
  Yout << Dumper.getYAMLObj();

  return std::error_code();
}
