//===-- XCOFFYAML.cpp - XCOFF YAMLIO implementation -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines classes for handling the YAML representation of XCOFF.
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjectYAML/XCOFFYAML.h"
#include "llvm/BinaryFormat/XCOFF.h"
#include <string.h>

namespace llvm {
namespace XCOFFYAML {

Object::Object() { memset(&Header, 0, sizeof(Header)); }

AuxSymbolEnt::~AuxSymbolEnt() = default;

} // namespace XCOFFYAML

namespace yaml {

void ScalarBitSetTraits<XCOFF::SectionTypeFlags>::bitset(
    IO &IO, XCOFF::SectionTypeFlags &Value) {
#define ECase(X) IO.bitSetCase(Value, #X, XCOFF::X)
  ECase(STYP_PAD);
  ECase(STYP_DWARF);
  ECase(STYP_TEXT);
  ECase(STYP_DATA);
  ECase(STYP_BSS);
  ECase(STYP_EXCEPT);
  ECase(STYP_INFO);
  ECase(STYP_TDATA);
  ECase(STYP_TBSS);
  ECase(STYP_LOADER);
  ECase(STYP_DEBUG);
  ECase(STYP_TYPCHK);
  ECase(STYP_OVRFLO);
#undef ECase
}

void ScalarEnumerationTraits<XCOFF::DwarfSectionSubtypeFlags>::enumeration(
    IO &IO, XCOFF::DwarfSectionSubtypeFlags &Value) {
#define ECase(X) IO.enumCase(Value, #X, XCOFF::X)
  ECase(SSUBTYP_DWINFO);
  ECase(SSUBTYP_DWLINE);
  ECase(SSUBTYP_DWPBNMS);
  ECase(SSUBTYP_DWPBTYP);
  ECase(SSUBTYP_DWARNGE);
  ECase(SSUBTYP_DWABREV);
  ECase(SSUBTYP_DWSTR);
  ECase(SSUBTYP_DWRNGES);
  ECase(SSUBTYP_DWLOC);
  ECase(SSUBTYP_DWFRAME);
  ECase(SSUBTYP_DWMAC);
#undef ECase
  IO.enumFallback<Hex32>(Value);
}

void ScalarEnumerationTraits<XCOFF::StorageClass>::enumeration(
    IO &IO, XCOFF::StorageClass &Value) {
#define ECase(X) IO.enumCase(Value, #X, XCOFF::X)
  ECase(C_NULL);
  ECase(C_AUTO);
  ECase(C_EXT);
  ECase(C_STAT);
  ECase(C_REG);
  ECase(C_EXTDEF);
  ECase(C_LABEL);
  ECase(C_ULABEL);
  ECase(C_MOS);
  ECase(C_ARG);
  ECase(C_STRTAG);
  ECase(C_MOU);
  ECase(C_UNTAG);
  ECase(C_TPDEF);
  ECase(C_USTATIC);
  ECase(C_ENTAG);
  ECase(C_MOE);
  ECase(C_REGPARM);
  ECase(C_FIELD);
  ECase(C_BLOCK);
  ECase(C_FCN);
  ECase(C_EOS);
  ECase(C_FILE);
  ECase(C_LINE);
  ECase(C_ALIAS);
  ECase(C_HIDDEN);
  ECase(C_HIDEXT);
  ECase(C_BINCL);
  ECase(C_EINCL);
  ECase(C_INFO);
  ECase(C_WEAKEXT);
  ECase(C_DWARF);
  ECase(C_GSYM);
  ECase(C_LSYM);
  ECase(C_PSYM);
  ECase(C_RSYM);
  ECase(C_RPSYM);
  ECase(C_STSYM);
  ECase(C_TCSYM);
  ECase(C_BCOMM);
  ECase(C_ECOML);
  ECase(C_ECOMM);
  ECase(C_DECL);
  ECase(C_ENTRY);
  ECase(C_FUN);
  ECase(C_BSTAT);
  ECase(C_ESTAT);
  ECase(C_GTLS);
  ECase(C_STTLS);
  ECase(C_EFCN);
#undef ECase
}

void ScalarEnumerationTraits<XCOFF::StorageMappingClass>::enumeration(
    IO &IO, XCOFF::StorageMappingClass &Value) {
#define ECase(X) IO.enumCase(Value, #X, XCOFF::X)
  ECase(XMC_PR);
  ECase(XMC_RO);
  ECase(XMC_DB);
  ECase(XMC_GL);
  ECase(XMC_XO);
  ECase(XMC_SV);
  ECase(XMC_SV64);
  ECase(XMC_SV3264);
  ECase(XMC_TI);
  ECase(XMC_TB);
  ECase(XMC_RW);
  ECase(XMC_TC0);
  ECase(XMC_TC);
  ECase(XMC_TD);
  ECase(XMC_DS);
  ECase(XMC_UA);
  ECase(XMC_BS);
  ECase(XMC_UC);
  ECase(XMC_TL);
  ECase(XMC_UL);
  ECase(XMC_TE);
#undef ECase
}

void ScalarEnumerationTraits<XCOFF::SymbolType>::enumeration(
    IO &IO, XCOFF::SymbolType &Value) {
#define ECase(X) IO.enumCase(Value, #X, XCOFF::X)
  ECase(XTY_ER);
  ECase(XTY_SD);
  ECase(XTY_LD);
  ECase(XTY_CM);
#undef ECase
  IO.enumFallback<Hex8>(Value);
}

void ScalarEnumerationTraits<XCOFFYAML::AuxSymbolType>::enumeration(
    IO &IO, XCOFFYAML::AuxSymbolType &Type) {
#define ECase(X) IO.enumCase(Type, #X, XCOFFYAML::X)
  ECase(AUX_EXCEPT);
  ECase(AUX_FCN);
  ECase(AUX_SYM);
  ECase(AUX_FILE);
  ECase(AUX_CSECT);
  ECase(AUX_SECT);
  ECase(AUX_STAT);
#undef ECase
}

void ScalarEnumerationTraits<XCOFF::CFileStringType>::enumeration(
    IO &IO, XCOFF::CFileStringType &Type) {
#define ECase(X) IO.enumCase(Type, #X, XCOFF::X)
  ECase(XFT_FN);
  ECase(XFT_CT);
  ECase(XFT_CV);
  ECase(XFT_CD);
#undef ECase
}

struct NSectionFlags {
  NSectionFlags(IO &) : Flags(XCOFF::SectionTypeFlags(0)) {}
  NSectionFlags(IO &, uint32_t C) : Flags(XCOFF::SectionTypeFlags(C)) {}

  uint32_t denormalize(IO &) { return Flags; }

  XCOFF::SectionTypeFlags Flags;
};

void MappingTraits<XCOFFYAML::FileHeader>::mapping(
    IO &IO, XCOFFYAML::FileHeader &FileHdr) {
  IO.mapOptional("MagicNumber", FileHdr.Magic);
  IO.mapOptional("NumberOfSections", FileHdr.NumberOfSections);
  IO.mapOptional("CreationTime", FileHdr.TimeStamp);
  IO.mapOptional("OffsetToSymbolTable", FileHdr.SymbolTableOffset);
  IO.mapOptional("EntriesInSymbolTable", FileHdr.NumberOfSymTableEntries);
  IO.mapOptional("AuxiliaryHeaderSize", FileHdr.AuxHeaderSize);
  IO.mapOptional("Flags", FileHdr.Flags);
}

void MappingTraits<XCOFFYAML::AuxiliaryHeader>::mapping(
    IO &IO, XCOFFYAML::AuxiliaryHeader &AuxHdr) {
  IO.mapOptional("Magic", AuxHdr.Magic);
  IO.mapOptional("Version", AuxHdr.Version);
  IO.mapOptional("TextStartAddr", AuxHdr.TextStartAddr);
  IO.mapOptional("DataStartAddr", AuxHdr.DataStartAddr);
  IO.mapOptional("TOCAnchorAddr", AuxHdr.TOCAnchorAddr);
  IO.mapOptional("TextSectionSize", AuxHdr.TextSize);
  IO.mapOptional("DataSectionSize", AuxHdr.InitDataSize);
  IO.mapOptional("BssSectionSize", AuxHdr.BssDataSize);
  IO.mapOptional("SecNumOfEntryPoint", AuxHdr.SecNumOfEntryPoint);
  IO.mapOptional("SecNumOfText", AuxHdr.SecNumOfText);
  IO.mapOptional("SecNumOfData", AuxHdr.SecNumOfData);
  IO.mapOptional("SecNumOfTOC", AuxHdr.SecNumOfTOC);
  IO.mapOptional("SecNumOfLoader", AuxHdr.SecNumOfLoader);
  IO.mapOptional("SecNumOfBSS", AuxHdr.SecNumOfBSS);
  IO.mapOptional("MaxAlignOfText", AuxHdr.MaxAlignOfText);
  IO.mapOptional("MaxAlignOfData", AuxHdr.MaxAlignOfData);
  IO.mapOptional("ModuleType", AuxHdr.CpuFlag);
  IO.mapOptional("TextPageSize", AuxHdr.TextPageSize);
  IO.mapOptional("DataPageSize", AuxHdr.DataPageSize);
  IO.mapOptional("StackPageSize", AuxHdr.StackPageSize);
  IO.mapOptional("FlagAndTDataAlignment", AuxHdr.FlagAndTDataAlignment);
  IO.mapOptional("EntryPointAddr", AuxHdr.EntryPointAddr);
  IO.mapOptional("MaxStackSize", AuxHdr.MaxStackSize);
  IO.mapOptional("MaxDataSize", AuxHdr.MaxDataSize);
  IO.mapOptional("SecNumOfTData", AuxHdr.SecNumOfTData);
  IO.mapOptional("SecNumOfTBSS", AuxHdr.SecNumOfTBSS);
  IO.mapOptional("Flag", AuxHdr.Flag);
}

void MappingTraits<XCOFFYAML::Relocation>::mapping(IO &IO,
                                                   XCOFFYAML::Relocation &R) {
  IO.mapOptional("Address", R.VirtualAddress);
  IO.mapOptional("Symbol", R.SymbolIndex);
  IO.mapOptional("Info", R.Info);
  IO.mapOptional("Type", R.Type);
}

void MappingTraits<XCOFFYAML::Section>::mapping(IO &IO,
                                                XCOFFYAML::Section &Sec) {
  MappingNormalization<NSectionFlags, uint32_t> NC(IO, Sec.Flags);
  IO.mapOptional("Name", Sec.SectionName);
  IO.mapOptional("Address", Sec.Address);
  IO.mapOptional("Size", Sec.Size);
  IO.mapOptional("FileOffsetToData", Sec.FileOffsetToData);
  IO.mapOptional("FileOffsetToRelocations", Sec.FileOffsetToRelocations);
  IO.mapOptional("FileOffsetToLineNumbers", Sec.FileOffsetToLineNumbers);
  IO.mapOptional("NumberOfRelocations", Sec.NumberOfRelocations);
  IO.mapOptional("NumberOfLineNumbers", Sec.NumberOfLineNumbers);
  IO.mapOptional("Flags", NC->Flags);
  IO.mapOptional("DWARFSectionSubtype", Sec.SectionSubtype);
  IO.mapOptional("SectionData", Sec.SectionData);
  IO.mapOptional("Relocations", Sec.Relocations);
}

static void auxSymMapping(IO &IO, XCOFFYAML::CsectAuxEnt &AuxSym, bool Is64) {
  IO.mapOptional("ParameterHashIndex", AuxSym.ParameterHashIndex);
  IO.mapOptional("TypeChkSectNum", AuxSym.TypeChkSectNum);
  IO.mapOptional("SymbolAlignmentAndType", AuxSym.SymbolAlignmentAndType);
  IO.mapOptional("SymbolType", AuxSym.SymbolType);
  IO.mapOptional("SymbolAlignment", AuxSym.SymbolAlignment);
  IO.mapOptional("StorageMappingClass", AuxSym.StorageMappingClass);
  if (Is64) {
    IO.mapOptional("SectionOrLengthLo", AuxSym.SectionOrLengthLo);
    IO.mapOptional("SectionOrLengthHi", AuxSym.SectionOrLengthHi);
  } else {
    IO.mapOptional("SectionOrLength", AuxSym.SectionOrLength);
    IO.mapOptional("StabInfoIndex", AuxSym.StabInfoIndex);
    IO.mapOptional("StabSectNum", AuxSym.StabSectNum);
  }
}

static void auxSymMapping(IO &IO, XCOFFYAML::FileAuxEnt &AuxSym) {
  IO.mapOptional("FileNameOrString", AuxSym.FileNameOrString);
  IO.mapOptional("FileStringType", AuxSym.FileStringType);
}

static void auxSymMapping(IO &IO, XCOFFYAML::BlockAuxEnt &AuxSym, bool Is64) {
  if (Is64) {
    IO.mapOptional("LineNum", AuxSym.LineNum);
  } else {
    IO.mapOptional("LineNumHi", AuxSym.LineNumHi);
    IO.mapOptional("LineNumLo", AuxSym.LineNumLo);
  }
}

static void auxSymMapping(IO &IO, XCOFFYAML::FunctionAuxEnt &AuxSym,
                          bool Is64) {
  if (!Is64)
    IO.mapOptional("OffsetToExceptionTbl", AuxSym.OffsetToExceptionTbl);
  IO.mapOptional("SizeOfFunction", AuxSym.SizeOfFunction);
  IO.mapOptional("SymIdxOfNextBeyond", AuxSym.SymIdxOfNextBeyond);
  IO.mapOptional("PtrToLineNum", AuxSym.PtrToLineNum);
}

static void auxSymMapping(IO &IO, XCOFFYAML::ExcpetionAuxEnt &AuxSym) {
  IO.mapOptional("OffsetToExceptionTbl", AuxSym.OffsetToExceptionTbl);
  IO.mapOptional("SizeOfFunction", AuxSym.SizeOfFunction);
  IO.mapOptional("SymIdxOfNextBeyond", AuxSym.SymIdxOfNextBeyond);
}

static void auxSymMapping(IO &IO, XCOFFYAML::SectAuxEntForDWARF &AuxSym) {
  IO.mapOptional("LengthOfSectionPortion", AuxSym.LengthOfSectionPortion);
  IO.mapOptional("NumberOfRelocEnt", AuxSym.NumberOfRelocEnt);
}

static void auxSymMapping(IO &IO, XCOFFYAML::SectAuxEntForStat &AuxSym) {
  IO.mapOptional("SectionLength", AuxSym.SectionLength);
  IO.mapOptional("NumberOfRelocEnt", AuxSym.NumberOfRelocEnt);
  IO.mapOptional("NumberOfLineNum", AuxSym.NumberOfLineNum);
}

template <typename AuxEntT>
static void ResetAuxSym(IO &IO,
                        std::unique_ptr<XCOFFYAML::AuxSymbolEnt> &AuxSym) {
  if (!IO.outputting())
    AuxSym.reset(new AuxEntT);
}

void MappingTraits<std::unique_ptr<XCOFFYAML::AuxSymbolEnt>>::mapping(
    IO &IO, std::unique_ptr<XCOFFYAML::AuxSymbolEnt> &AuxSym) {
  const bool Is64 =
      static_cast<XCOFFYAML::Object *>(IO.getContext())->Header.Magic ==
      (llvm::yaml::Hex16)XCOFF::XCOFF64;

  XCOFFYAML::AuxSymbolType AuxType;
  if (IO.outputting())
    AuxType = AuxSym->Type;
  IO.mapRequired("Type", AuxType);
  switch (AuxType) {
  case XCOFFYAML::AUX_EXCEPT:
    if (!Is64) {
      IO.setError("an auxiliary symbol of type AUX_EXCEPT cannot be defined in "
                  "XCOFF32");
      return;
    }
    ResetAuxSym<XCOFFYAML::ExcpetionAuxEnt>(IO, AuxSym);
    auxSymMapping(IO, *cast<XCOFFYAML::ExcpetionAuxEnt>(AuxSym.get()));
    break;
  case XCOFFYAML::AUX_FCN:
    ResetAuxSym<XCOFFYAML::FunctionAuxEnt>(IO, AuxSym);
    auxSymMapping(IO, *cast<XCOFFYAML::FunctionAuxEnt>(AuxSym.get()), Is64);
    break;
  case XCOFFYAML::AUX_SYM:
    ResetAuxSym<XCOFFYAML::BlockAuxEnt>(IO, AuxSym);
    auxSymMapping(IO, *cast<XCOFFYAML::BlockAuxEnt>(AuxSym.get()), Is64);
    break;
  case XCOFFYAML::AUX_FILE:
    ResetAuxSym<XCOFFYAML::FileAuxEnt>(IO, AuxSym);
    auxSymMapping(IO, *cast<XCOFFYAML::FileAuxEnt>(AuxSym.get()));
    break;
  case XCOFFYAML::AUX_CSECT:
    ResetAuxSym<XCOFFYAML::CsectAuxEnt>(IO, AuxSym);
    auxSymMapping(IO, *cast<XCOFFYAML::CsectAuxEnt>(AuxSym.get()), Is64);
    break;
  case XCOFFYAML::AUX_SECT:
    ResetAuxSym<XCOFFYAML::SectAuxEntForDWARF>(IO, AuxSym);
    auxSymMapping(IO, *cast<XCOFFYAML::SectAuxEntForDWARF>(AuxSym.get()));
    break;
  case XCOFFYAML::AUX_STAT:
    if (Is64) {
      IO.setError(
          "an auxiliary symbol of type AUX_STAT cannot be defined in XCOFF64");
      return;
    }
    ResetAuxSym<XCOFFYAML::SectAuxEntForStat>(IO, AuxSym);
    auxSymMapping(IO, *cast<XCOFFYAML::SectAuxEntForStat>(AuxSym.get()));
    break;
  }
}

void MappingTraits<XCOFFYAML::Symbol>::mapping(IO &IO, XCOFFYAML::Symbol &S) {
  IO.mapOptional("Name", S.SymbolName);
  IO.mapOptional("Value", S.Value);
  IO.mapOptional("Section", S.SectionName);
  IO.mapOptional("SectionIndex", S.SectionIndex);
  IO.mapOptional("Type", S.Type);
  IO.mapOptional("StorageClass", S.StorageClass);
  IO.mapOptional("NumberOfAuxEntries", S.NumberOfAuxEntries);
  IO.mapOptional("AuxEntries", S.AuxEntries);
}

void MappingTraits<XCOFFYAML::StringTable>::mapping(
    IO &IO, XCOFFYAML::StringTable &Str) {
  IO.mapOptional("ContentSize", Str.ContentSize);
  IO.mapOptional("Length", Str.Length);
  IO.mapOptional("Strings", Str.Strings);
  IO.mapOptional("RawContent", Str.RawContent);
}

void MappingTraits<XCOFFYAML::Object>::mapping(IO &IO, XCOFFYAML::Object &Obj) {
  IO.setContext(&Obj);
  IO.mapTag("!XCOFF", true);
  IO.mapRequired("FileHeader", Obj.Header);
  IO.mapOptional("AuxiliaryHeader", Obj.AuxHeader);
  IO.mapOptional("Sections", Obj.Sections);
  IO.mapOptional("Symbols", Obj.Symbols);
  IO.mapOptional("StringTable", Obj.StrTbl);
  IO.setContext(nullptr);
}

} // namespace yaml
} // namespace llvm
