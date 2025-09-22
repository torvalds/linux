//===------ xcoff2yaml.cpp - XCOFF YAMLIO implementation --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "obj2yaml.h"
#include "llvm/Object/XCOFFObjectFile.h"
#include "llvm/ObjectYAML/XCOFFYAML.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/YAMLTraits.h"

using namespace llvm;
using namespace llvm::object;
namespace {

class XCOFFDumper {
  const object::XCOFFObjectFile &Obj;
  XCOFFYAML::Object YAMLObj;
  void dumpHeader();
  Error dumpSections();
  Error dumpSymbols();
  template <typename Shdr, typename Reloc>
  Error dumpSections(ArrayRef<Shdr> Sections);

  // Dump auxiliary symbols.
  Error dumpFileAuxSym(XCOFFYAML::Symbol &Sym,
                       const XCOFFSymbolRef &SymbolEntRef);
  Error dumpStatAuxSym(XCOFFYAML::Symbol &Sym,
                       const XCOFFSymbolRef &SymbolEntRef);
  Error dumpBlockAuxSym(XCOFFYAML::Symbol &Sym,
                        const XCOFFSymbolRef &SymbolEntRef);
  Error dumpDwarfAuxSym(XCOFFYAML::Symbol &Sym,
                        const XCOFFSymbolRef &SymbolEntRef);
  Error dumpAuxSyms(XCOFFYAML::Symbol &Sym, const XCOFFSymbolRef &SymbolEntRef);
  void dumpFuncAuxSym(XCOFFYAML::Symbol &Sym, const uintptr_t AuxAddress);
  void dumpExpAuxSym(XCOFFYAML::Symbol &Sym, const uintptr_t AuxAddress);
  void dumpCsectAuxSym(XCOFFYAML::Symbol &Sym,
                       const object::XCOFFCsectAuxRef &AuxEntPtr);

public:
  XCOFFDumper(const object::XCOFFObjectFile &obj) : Obj(obj) {}
  Error dump();
  XCOFFYAML::Object &getYAMLObj() { return YAMLObj; }

  template <typename T> const T *getAuxEntPtr(uintptr_t AuxAddress) {
    Obj.checkSymbolEntryPointer(AuxAddress);
    return reinterpret_cast<const T *>(AuxAddress);
  }
};
} // namespace

Error XCOFFDumper::dump() {
  dumpHeader();
  if (Error E = dumpSections())
    return E;
  return dumpSymbols();
}

void XCOFFDumper::dumpHeader() {
  YAMLObj.Header.Magic = Obj.getMagic();
  YAMLObj.Header.NumberOfSections = Obj.getNumberOfSections();
  YAMLObj.Header.TimeStamp = Obj.getTimeStamp();
  YAMLObj.Header.SymbolTableOffset = Obj.is64Bit()
                                         ? Obj.getSymbolTableOffset64()
                                         : Obj.getSymbolTableOffset32();
  YAMLObj.Header.NumberOfSymTableEntries =
      Obj.is64Bit() ? Obj.getNumberOfSymbolTableEntries64()
                    : Obj.getRawNumberOfSymbolTableEntries32();
  YAMLObj.Header.AuxHeaderSize = Obj.getOptionalHeaderSize();
  YAMLObj.Header.Flags = Obj.getFlags();
}

Error XCOFFDumper::dumpSections() {
  if (Obj.is64Bit())
    return dumpSections<XCOFFSectionHeader64, XCOFFRelocation64>(
        Obj.sections64());
  return dumpSections<XCOFFSectionHeader32, XCOFFRelocation32>(
      Obj.sections32());
}

template <typename Shdr, typename Reloc>
Error XCOFFDumper::dumpSections(ArrayRef<Shdr> Sections) {
  std::vector<XCOFFYAML::Section> &YamlSections = YAMLObj.Sections;
  for (const Shdr &S : Sections) {
    XCOFFYAML::Section YamlSec;
    YamlSec.SectionName = S.getName();
    YamlSec.Address = S.PhysicalAddress;
    YamlSec.Size = S.SectionSize;
    YamlSec.NumberOfRelocations = S.NumberOfRelocations;
    YamlSec.NumberOfLineNumbers = S.NumberOfLineNumbers;
    YamlSec.FileOffsetToData = S.FileOffsetToRawData;
    YamlSec.FileOffsetToRelocations = S.FileOffsetToRelocationInfo;
    YamlSec.FileOffsetToLineNumbers = S.FileOffsetToLineNumberInfo;
    YamlSec.Flags = S.Flags;
    if (YamlSec.Flags & XCOFF::STYP_DWARF) {
      unsigned Mask = Obj.is64Bit()
                          ? XCOFFSectionHeader64::SectionFlagsTypeMask
                          : XCOFFSectionHeader32::SectionFlagsTypeMask;
      YamlSec.SectionSubtype =
          static_cast<XCOFF::DwarfSectionSubtypeFlags>(S.Flags & ~Mask);
    }

    // Dump section data.
    if (S.FileOffsetToRawData) {
      DataRefImpl SectionDRI;
      SectionDRI.p = reinterpret_cast<uintptr_t>(&S);
      Expected<ArrayRef<uint8_t>> SecDataRefOrErr =
          Obj.getSectionContents(SectionDRI);
      if (!SecDataRefOrErr)
        return SecDataRefOrErr.takeError();
      YamlSec.SectionData = SecDataRefOrErr.get();
    }

    // Dump relocations.
    if (S.NumberOfRelocations) {
      auto RelRefOrErr = Obj.relocations<Shdr, Reloc>(S);
      if (!RelRefOrErr)
        return RelRefOrErr.takeError();
      for (const Reloc &R : RelRefOrErr.get()) {
        XCOFFYAML::Relocation YamlRel;
        YamlRel.Type = R.Type;
        YamlRel.Info = R.Info;
        YamlRel.SymbolIndex = R.SymbolIndex;
        YamlRel.VirtualAddress = R.VirtualAddress;
        YamlSec.Relocations.push_back(YamlRel);
      }
    }
    YamlSections.push_back(YamlSec);
  }
  return Error::success();
}

Error XCOFFDumper::dumpFileAuxSym(XCOFFYAML::Symbol &Sym,
                                  const XCOFFSymbolRef &SymbolEntRef) {
  for (uint8_t I = 1; I <= Sym.NumberOfAuxEntries; ++I) {
    uintptr_t AuxAddress = XCOFFObjectFile::getAdvancedSymbolEntryAddress(
        SymbolEntRef.getEntryAddress(), I);
    const XCOFFFileAuxEnt *FileAuxEntPtr =
        getAuxEntPtr<XCOFFFileAuxEnt>(AuxAddress);
    auto FileNameOrError = Obj.getCFileName(FileAuxEntPtr);
    if (!FileNameOrError)
      return FileNameOrError.takeError();

    XCOFFYAML::FileAuxEnt FileAuxSym;
    FileAuxSym.FileNameOrString = FileNameOrError.get();
    FileAuxSym.FileStringType = FileAuxEntPtr->Type;
    Sym.AuxEntries.push_back(
        std::make_unique<XCOFFYAML::FileAuxEnt>(FileAuxSym));
  }
  return Error::success();
}

Error XCOFFDumper::dumpStatAuxSym(XCOFFYAML::Symbol &Sym,
                                  const XCOFFSymbolRef &SymbolEntRef) {
  if (Sym.NumberOfAuxEntries != 1) {
    uint32_t SymbolIndex = Obj.getSymbolIndex(SymbolEntRef.getEntryAddress());
    return createError("failed to parse symbol \"" + Sym.SymbolName +
                       "\" with index of " + Twine(SymbolIndex) +
                       ": expected 1 aux symbol for C_STAT, while got " +
                       Twine(static_cast<uint32_t>(*Sym.NumberOfAuxEntries)));
  }

  const XCOFFSectAuxEntForStat *AuxEntPtr =
      getAuxEntPtr<XCOFFSectAuxEntForStat>(
          XCOFFObjectFile::getAdvancedSymbolEntryAddress(
              SymbolEntRef.getEntryAddress(), 1));
  XCOFFYAML::SectAuxEntForStat StatAuxSym;
  StatAuxSym.SectionLength = AuxEntPtr->SectionLength;
  StatAuxSym.NumberOfLineNum = AuxEntPtr->NumberOfLineNum;
  StatAuxSym.NumberOfRelocEnt = AuxEntPtr->NumberOfRelocEnt;
  Sym.AuxEntries.push_back(
      std::make_unique<XCOFFYAML::SectAuxEntForStat>(StatAuxSym));
  return Error::success();
}

void XCOFFDumper::dumpFuncAuxSym(XCOFFYAML::Symbol &Sym,
                                 const uintptr_t AuxAddress) {
  XCOFFYAML::FunctionAuxEnt FunAuxSym;

  if (Obj.is64Bit()) {
    const XCOFFFunctionAuxEnt64 *AuxEntPtr =
        getAuxEntPtr<XCOFFFunctionAuxEnt64>(AuxAddress);
    FunAuxSym.PtrToLineNum = AuxEntPtr->PtrToLineNum;
    FunAuxSym.SizeOfFunction = AuxEntPtr->SizeOfFunction;
    FunAuxSym.SymIdxOfNextBeyond = AuxEntPtr->SymIdxOfNextBeyond;
  } else {
    const XCOFFFunctionAuxEnt32 *AuxEntPtr =
        getAuxEntPtr<XCOFFFunctionAuxEnt32>(AuxAddress);
    FunAuxSym.OffsetToExceptionTbl = AuxEntPtr->OffsetToExceptionTbl;
    FunAuxSym.PtrToLineNum = AuxEntPtr->PtrToLineNum;
    FunAuxSym.SizeOfFunction = AuxEntPtr->SizeOfFunction;
    FunAuxSym.SymIdxOfNextBeyond = AuxEntPtr->SymIdxOfNextBeyond;
  }

  Sym.AuxEntries.push_back(
      std::make_unique<XCOFFYAML::FunctionAuxEnt>(FunAuxSym));
}

void XCOFFDumper::dumpExpAuxSym(XCOFFYAML::Symbol &Sym,
                                const uintptr_t AuxAddress) {
  const XCOFFExceptionAuxEnt *AuxEntPtr =
      getAuxEntPtr<XCOFFExceptionAuxEnt>(AuxAddress);
  XCOFFYAML::ExcpetionAuxEnt ExceptAuxSym;
  ExceptAuxSym.OffsetToExceptionTbl = AuxEntPtr->OffsetToExceptionTbl;
  ExceptAuxSym.SizeOfFunction = AuxEntPtr->SizeOfFunction;
  ExceptAuxSym.SymIdxOfNextBeyond = AuxEntPtr->SymIdxOfNextBeyond;
  Sym.AuxEntries.push_back(
      std::make_unique<XCOFFYAML::ExcpetionAuxEnt>(ExceptAuxSym));
}

void XCOFFDumper::dumpCsectAuxSym(XCOFFYAML::Symbol &Sym,
                                  const object::XCOFFCsectAuxRef &AuxEntPtr) {
  XCOFFYAML::CsectAuxEnt CsectAuxSym;
  CsectAuxSym.ParameterHashIndex = AuxEntPtr.getParameterHashIndex();
  CsectAuxSym.TypeChkSectNum = AuxEntPtr.getTypeChkSectNum();
  CsectAuxSym.SymbolAlignment = AuxEntPtr.getAlignmentLog2();
  CsectAuxSym.SymbolType =
      static_cast<XCOFF::SymbolType>(AuxEntPtr.getSymbolType());
  CsectAuxSym.StorageMappingClass = AuxEntPtr.getStorageMappingClass();

  if (Obj.is64Bit()) {
    CsectAuxSym.SectionOrLengthLo =
        static_cast<uint32_t>(AuxEntPtr.getSectionOrLength64());
    CsectAuxSym.SectionOrLengthHi =
        static_cast<uint32_t>(AuxEntPtr.getSectionOrLength64() >> 32);
  } else {
    CsectAuxSym.SectionOrLength = AuxEntPtr.getSectionOrLength32();
    CsectAuxSym.StabInfoIndex = AuxEntPtr.getStabInfoIndex32();
    CsectAuxSym.StabSectNum = AuxEntPtr.getStabSectNum32();
  }

  Sym.AuxEntries.push_back(
      std::make_unique<XCOFFYAML::CsectAuxEnt>(CsectAuxSym));
}

Error XCOFFDumper::dumpAuxSyms(XCOFFYAML::Symbol &Sym,
                               const XCOFFSymbolRef &SymbolEntRef) {
  auto ErrOrCsectAuxRef = SymbolEntRef.getXCOFFCsectAuxRef();
  if (!ErrOrCsectAuxRef)
    return ErrOrCsectAuxRef.takeError();
  XCOFFCsectAuxRef CsectAuxRef = ErrOrCsectAuxRef.get();

  for (uint8_t I = 1; I <= Sym.NumberOfAuxEntries; ++I) {

    if (I == Sym.NumberOfAuxEntries && !Obj.is64Bit()) {
      dumpCsectAuxSym(Sym, CsectAuxRef);
      return Error::success();
    }

    uintptr_t AuxAddress = XCOFFObjectFile::getAdvancedSymbolEntryAddress(
        SymbolEntRef.getEntryAddress(), I);

    if (Obj.is64Bit()) {
      XCOFF::SymbolAuxType Type = *Obj.getSymbolAuxType(AuxAddress);
      if (Type == XCOFF::SymbolAuxType::AUX_CSECT)
        dumpCsectAuxSym(Sym, CsectAuxRef);
      else if (Type == XCOFF::SymbolAuxType::AUX_FCN)
        dumpFuncAuxSym(Sym, AuxAddress);
      else if (Type == XCOFF::SymbolAuxType::AUX_EXCEPT)
        dumpExpAuxSym(Sym, AuxAddress);
      else {
        uint32_t SymbolIndex =
            Obj.getSymbolIndex(SymbolEntRef.getEntryAddress());
        return createError("failed to parse symbol \"" + Sym.SymbolName +
                           "\" with index of " + Twine(SymbolIndex) +
                           ": invalid auxiliary symbol type: " +
                           Twine(static_cast<uint32_t>(Type)));
      }

    } else
      dumpFuncAuxSym(Sym, AuxAddress);
  }

  return Error::success();
}

Error XCOFFDumper::dumpBlockAuxSym(XCOFFYAML::Symbol &Sym,
                                   const XCOFFSymbolRef &SymbolEntRef) {
  if (Sym.NumberOfAuxEntries != 1) {
    uint32_t SymbolIndex = Obj.getSymbolIndex(SymbolEntRef.getEntryAddress());
    return createError(
        "failed to parse symbol \"" + Sym.SymbolName + "\" with index of " +
        Twine(SymbolIndex) +
        ": expected 1 aux symbol for C_BLOCK or C_FCN, while got " +
        Twine(static_cast<uint32_t>(*Sym.NumberOfAuxEntries)));
  }

  uintptr_t AuxAddress = XCOFFObjectFile::getAdvancedSymbolEntryAddress(
      SymbolEntRef.getEntryAddress(), 1);
  XCOFFYAML::BlockAuxEnt BlockAuxSym;

  if (Obj.is64Bit()) {
    const XCOFFBlockAuxEnt64 *AuxEntPtr =
        getAuxEntPtr<XCOFFBlockAuxEnt64>(AuxAddress);
    BlockAuxSym.LineNum = AuxEntPtr->LineNum;
  } else {
    const XCOFFBlockAuxEnt32 *AuxEntPtr =
        getAuxEntPtr<XCOFFBlockAuxEnt32>(AuxAddress);
    BlockAuxSym.LineNumLo = AuxEntPtr->LineNumLo;
    BlockAuxSym.LineNumHi = AuxEntPtr->LineNumHi;
  }

  Sym.AuxEntries.push_back(
      std::make_unique<XCOFFYAML::BlockAuxEnt>(BlockAuxSym));
  return Error::success();
}

Error XCOFFDumper::dumpDwarfAuxSym(XCOFFYAML::Symbol &Sym,
                                   const XCOFFSymbolRef &SymbolEntRef) {
  if (Sym.NumberOfAuxEntries != 1) {
    uint32_t SymbolIndex = Obj.getSymbolIndex(SymbolEntRef.getEntryAddress());
    return createError("failed to parse symbol \"" + Sym.SymbolName +
                       "\" with index of " + Twine(SymbolIndex) +
                       ": expected 1 aux symbol for C_DWARF, while got " +
                       Twine(static_cast<uint32_t>(*Sym.NumberOfAuxEntries)));
  }

  uintptr_t AuxAddress = XCOFFObjectFile::getAdvancedSymbolEntryAddress(
      SymbolEntRef.getEntryAddress(), 1);
  XCOFFYAML::SectAuxEntForDWARF DwarfAuxSym;

  if (Obj.is64Bit()) {
    const XCOFFSectAuxEntForDWARF64 *AuxEntPtr =
        getAuxEntPtr<XCOFFSectAuxEntForDWARF64>(AuxAddress);
    DwarfAuxSym.LengthOfSectionPortion = AuxEntPtr->LengthOfSectionPortion;
    DwarfAuxSym.NumberOfRelocEnt = AuxEntPtr->NumberOfRelocEnt;
  } else {
    const XCOFFSectAuxEntForDWARF32 *AuxEntPtr =
        getAuxEntPtr<XCOFFSectAuxEntForDWARF32>(AuxAddress);
    DwarfAuxSym.LengthOfSectionPortion = AuxEntPtr->LengthOfSectionPortion;
    DwarfAuxSym.NumberOfRelocEnt = AuxEntPtr->NumberOfRelocEnt;
  }

  Sym.AuxEntries.push_back(
      std::make_unique<XCOFFYAML::SectAuxEntForDWARF>(DwarfAuxSym));
  return Error::success();
}

Error XCOFFDumper::dumpSymbols() {
  std::vector<XCOFFYAML::Symbol> &Symbols = YAMLObj.Symbols;

  for (const SymbolRef &S : Obj.symbols()) {
    DataRefImpl SymbolDRI = S.getRawDataRefImpl();
    const XCOFFSymbolRef SymbolEntRef = Obj.toSymbolRef(SymbolDRI);
    XCOFFYAML::Symbol Sym;

    Expected<StringRef> SymNameRefOrErr = Obj.getSymbolName(SymbolDRI);
    if (!SymNameRefOrErr) {
      return SymNameRefOrErr.takeError();
    }
    Sym.SymbolName = SymNameRefOrErr.get();

    Sym.Value = SymbolEntRef.getValue();

    Expected<StringRef> SectionNameRefOrErr =
        Obj.getSymbolSectionName(SymbolEntRef);
    if (!SectionNameRefOrErr)
      return SectionNameRefOrErr.takeError();

    Sym.SectionName = SectionNameRefOrErr.get();

    Sym.Type = SymbolEntRef.getSymbolType();
    Sym.StorageClass = SymbolEntRef.getStorageClass();
    Sym.NumberOfAuxEntries = SymbolEntRef.getNumberOfAuxEntries();

    if (Sym.NumberOfAuxEntries) {
      switch (Sym.StorageClass) {
      case XCOFF::C_FILE:
        if (Error E = dumpFileAuxSym(Sym, SymbolEntRef))
          return E;
        break;
      case XCOFF::C_STAT:
        if (Error E = dumpStatAuxSym(Sym, SymbolEntRef))
          return E;
        break;
      case XCOFF::C_EXT:
      case XCOFF::C_WEAKEXT:
      case XCOFF::C_HIDEXT:
        if (Error E = dumpAuxSyms(Sym, SymbolEntRef))
          return E;
        break;
      case XCOFF::C_BLOCK:
      case XCOFF::C_FCN:
        if (Error E = dumpBlockAuxSym(Sym, SymbolEntRef))
          return E;
        break;
      case XCOFF::C_DWARF:
        if (Error E = dumpDwarfAuxSym(Sym, SymbolEntRef))
          return E;
        break;
      default:
        break;
      }
    }

    Symbols.push_back(std::move(Sym));
  }

  return Error::success();
}

Error xcoff2yaml(raw_ostream &Out, const object::XCOFFObjectFile &Obj) {
  XCOFFDumper Dumper(Obj);

  if (Error E = Dumper.dump())
    return E;

  yaml::Output Yout(Out);
  Yout << Dumper.getYAMLObj();

  return Error::success();
}
