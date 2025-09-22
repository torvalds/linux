//===- SymbolizableObjectFile.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of SymbolizableObjectFile class.
//
//===----------------------------------------------------------------------===//

#include "llvm/DebugInfo/Symbolize/SymbolizableObjectFile.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolSize.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/DataExtractor.h"
#include "llvm/TargetParser/Triple.h"
#include <algorithm>

using namespace llvm;
using namespace object;
using namespace symbolize;

Expected<std::unique_ptr<SymbolizableObjectFile>>
SymbolizableObjectFile::create(const object::ObjectFile *Obj,
                               std::unique_ptr<DIContext> DICtx,
                               bool UntagAddresses) {
  assert(DICtx);
  std::unique_ptr<SymbolizableObjectFile> res(
      new SymbolizableObjectFile(Obj, std::move(DICtx), UntagAddresses));
  std::unique_ptr<DataExtractor> OpdExtractor;
  uint64_t OpdAddress = 0;
  // Find the .opd (function descriptor) section if any, for big-endian
  // PowerPC64 ELF.
  if (Obj->getArch() == Triple::ppc64) {
    for (section_iterator Section : Obj->sections()) {
      Expected<StringRef> NameOrErr = Section->getName();
      if (!NameOrErr)
        return NameOrErr.takeError();

      if (*NameOrErr == ".opd") {
        Expected<StringRef> E = Section->getContents();
        if (!E)
          return E.takeError();
        OpdExtractor.reset(new DataExtractor(*E, Obj->isLittleEndian(),
                                             Obj->getBytesInAddress()));
        OpdAddress = Section->getAddress();
        break;
      }
    }
  }
  std::vector<std::pair<SymbolRef, uint64_t>> Symbols =
      computeSymbolSizes(*Obj);
  for (auto &P : Symbols)
    if (Error E =
            res->addSymbol(P.first, P.second, OpdExtractor.get(), OpdAddress))
      return std::move(E);

  // If this is a COFF object and we didn't find any symbols, try the export
  // table.
  if (Symbols.empty()) {
    if (auto *CoffObj = dyn_cast<COFFObjectFile>(Obj))
      if (Error E = res->addCoffExportSymbols(CoffObj))
        return std::move(E);
  }

  std::vector<SymbolDesc> &SS = res->Symbols;
  // Sort by (Addr,Size,Name). If several SymbolDescs share the same Addr,
  // pick the one with the largest Size. This helps us avoid symbols with no
  // size information (Size=0).
  llvm::stable_sort(SS);
  auto I = SS.begin(), E = SS.end(), J = SS.begin();
  while (I != E) {
    auto OI = I;
    while (++I != E && OI->Addr == I->Addr) {
    }
    *J++ = I[-1];
  }
  SS.erase(J, SS.end());

  return std::move(res);
}

SymbolizableObjectFile::SymbolizableObjectFile(const ObjectFile *Obj,
                                               std::unique_ptr<DIContext> DICtx,
                                               bool UntagAddresses)
    : Module(Obj), DebugInfoContext(std::move(DICtx)),
      UntagAddresses(UntagAddresses) {}

namespace {

struct OffsetNamePair {
  uint32_t Offset;
  StringRef Name;

  bool operator<(const OffsetNamePair &R) const {
    return Offset < R.Offset;
  }
};

} // end anonymous namespace

Error SymbolizableObjectFile::addCoffExportSymbols(
    const COFFObjectFile *CoffObj) {
  // Get all export names and offsets.
  std::vector<OffsetNamePair> ExportSyms;
  for (const ExportDirectoryEntryRef &Ref : CoffObj->export_directories()) {
    StringRef Name;
    uint32_t Offset;
    if (auto EC = Ref.getSymbolName(Name))
      return EC;
    if (auto EC = Ref.getExportRVA(Offset))
      return EC;
    ExportSyms.push_back(OffsetNamePair{Offset, Name});
  }
  if (ExportSyms.empty())
    return Error::success();

  // Sort by ascending offset.
  array_pod_sort(ExportSyms.begin(), ExportSyms.end());

  // Approximate the symbol sizes by assuming they run to the next symbol.
  // FIXME: This assumes all exports are functions.
  uint64_t ImageBase = CoffObj->getImageBase();
  for (auto I = ExportSyms.begin(), E = ExportSyms.end(); I != E; ++I) {
    OffsetNamePair &Export = *I;
    // FIXME: The last export has a one byte size now.
    uint32_t NextOffset = I != E ? I->Offset : Export.Offset + 1;
    uint64_t SymbolStart = ImageBase + Export.Offset;
    uint64_t SymbolSize = NextOffset - Export.Offset;
    Symbols.push_back({SymbolStart, SymbolSize, Export.Name, 0});
  }
  return Error::success();
}

Error SymbolizableObjectFile::addSymbol(const SymbolRef &Symbol,
                                        uint64_t SymbolSize,
                                        DataExtractor *OpdExtractor,
                                        uint64_t OpdAddress) {
  // Avoid adding symbols from an unknown/undefined section.
  const ObjectFile &Obj = *Symbol.getObject();
  Expected<StringRef> SymbolNameOrErr = Symbol.getName();
  if (!SymbolNameOrErr)
    return SymbolNameOrErr.takeError();
  StringRef SymbolName = *SymbolNameOrErr;

  uint32_t ELFSymIdx =
      Obj.isELF() ? ELFSymbolRef(Symbol).getRawDataRefImpl().d.b : 0;
  Expected<section_iterator> Sec = Symbol.getSection();
  if (!Sec || Obj.section_end() == *Sec) {
    if (Obj.isELF()) {
      // Store the (index, filename) pair for a file symbol.
      ELFSymbolRef ESym(Symbol);
      if (ESym.getELFType() == ELF::STT_FILE)
        FileSymbols.emplace_back(ELFSymIdx, SymbolName);
    }
    return Error::success();
  }

  Expected<SymbolRef::Type> SymbolTypeOrErr = Symbol.getType();
  if (!SymbolTypeOrErr)
    return SymbolTypeOrErr.takeError();
  SymbolRef::Type SymbolType = *SymbolTypeOrErr;
  if (Obj.isELF()) {
    // Ignore any symbols coming from sections that don't have runtime
    // allocated memory.
    if ((elf_section_iterator(*Sec)->getFlags() & ELF::SHF_ALLOC) == 0)
      return Error::success();

    // Allow function and data symbols. Additionally allow STT_NONE, which are
    // common for functions defined in assembly.
    uint8_t Type = ELFSymbolRef(Symbol).getELFType();
    if (Type != ELF::STT_NOTYPE && Type != ELF::STT_FUNC &&
        Type != ELF::STT_OBJECT && Type != ELF::STT_GNU_IFUNC)
      return Error::success();
    // Some STT_NOTYPE symbols are not desired. This excludes STT_SECTION and
    // ARM mapping symbols.
    uint32_t Flags = cantFail(Symbol.getFlags());
    if (Flags & SymbolRef::SF_FormatSpecific)
      return Error::success();
  } else if (SymbolType != SymbolRef::ST_Function &&
             SymbolType != SymbolRef::ST_Data) {
    return Error::success();
  }

  Expected<uint64_t> SymbolAddressOrErr = Symbol.getAddress();
  if (!SymbolAddressOrErr)
    return SymbolAddressOrErr.takeError();
  uint64_t SymbolAddress = *SymbolAddressOrErr;
  if (UntagAddresses) {
    // For kernel addresses, bits 56-63 need to be set, so we sign extend bit 55
    // into bits 56-63 instead of masking them out.
    SymbolAddress &= (1ull << 56) - 1;
    SymbolAddress = (int64_t(SymbolAddress) << 8) >> 8;
  }
  if (OpdExtractor) {
    // For big-endian PowerPC64 ELF, symbols in the .opd section refer to
    // function descriptors. The first word of the descriptor is a pointer to
    // the function's code.
    // For the purposes of symbolization, pretend the symbol's address is that
    // of the function's code, not the descriptor.
    uint64_t OpdOffset = SymbolAddress - OpdAddress;
    if (OpdExtractor->isValidOffsetForAddress(OpdOffset))
      SymbolAddress = OpdExtractor->getAddress(&OpdOffset);
  }
  // Mach-O symbol table names have leading underscore, skip it.
  if (Module->isMachO())
    SymbolName.consume_front("_");

  if (Obj.isELF() && ELFSymbolRef(Symbol).getBinding() != ELF::STB_LOCAL)
    ELFSymIdx = 0;
  Symbols.push_back({SymbolAddress, SymbolSize, SymbolName, ELFSymIdx});
  return Error::success();
}

// Return true if this is a 32-bit x86 PE COFF module.
bool SymbolizableObjectFile::isWin32Module() const {
  auto *CoffObject = dyn_cast<COFFObjectFile>(Module);
  return CoffObject && CoffObject->getMachine() == COFF::IMAGE_FILE_MACHINE_I386;
}

uint64_t SymbolizableObjectFile::getModulePreferredBase() const {
  if (auto *CoffObject = dyn_cast<COFFObjectFile>(Module))
    return CoffObject->getImageBase();
  return 0;
}

bool SymbolizableObjectFile::getNameFromSymbolTable(
    uint64_t Address, std::string &Name, uint64_t &Addr, uint64_t &Size,
    std::string &FileName) const {
  SymbolDesc SD{Address, UINT64_C(-1), StringRef(), 0};
  auto SymbolIterator = llvm::upper_bound(Symbols, SD);
  if (SymbolIterator == Symbols.begin())
    return false;
  --SymbolIterator;
  if (SymbolIterator->Size != 0 &&
      SymbolIterator->Addr + SymbolIterator->Size <= Address)
    return false;
  Name = SymbolIterator->Name.str();
  Addr = SymbolIterator->Addr;
  Size = SymbolIterator->Size;

  if (SymbolIterator->ELFLocalSymIdx != 0) {
    // If this is an ELF local symbol, find the STT_FILE symbol preceding
    // SymbolIterator to get the filename. The ELF spec requires the STT_FILE
    // symbol (if present) precedes the other STB_LOCAL symbols for the file.
    assert(Module->isELF());
    auto It = llvm::upper_bound(
        FileSymbols,
        std::make_pair(SymbolIterator->ELFLocalSymIdx, StringRef()));
    if (It != FileSymbols.begin())
      FileName = It[-1].second.str();
  }
  return true;
}

bool SymbolizableObjectFile::shouldOverrideWithSymbolTable(
    FunctionNameKind FNKind, bool UseSymbolTable) const {
  // When DWARF is used with -gline-tables-only / -gmlt, the symbol table gives
  // better answers for linkage names than the DIContext. Otherwise, we are
  // probably using PEs and PDBs, and we shouldn't do the override. PE files
  // generally only contain the names of exported symbols.
  return FNKind == FunctionNameKind::LinkageName && UseSymbolTable &&
         isa<DWARFContext>(DebugInfoContext.get());
}

DILineInfo
SymbolizableObjectFile::symbolizeCode(object::SectionedAddress ModuleOffset,
                                      DILineInfoSpecifier LineInfoSpecifier,
                                      bool UseSymbolTable) const {
  if (ModuleOffset.SectionIndex == object::SectionedAddress::UndefSection)
    ModuleOffset.SectionIndex =
        getModuleSectionIndexForAddress(ModuleOffset.Address);
  DILineInfo LineInfo =
      DebugInfoContext->getLineInfoForAddress(ModuleOffset, LineInfoSpecifier);

  // Override function name from symbol table if necessary.
  if (shouldOverrideWithSymbolTable(LineInfoSpecifier.FNKind, UseSymbolTable)) {
    std::string FunctionName, FileName;
    uint64_t Start, Size;
    if (getNameFromSymbolTable(ModuleOffset.Address, FunctionName, Start, Size,
                               FileName)) {
      LineInfo.FunctionName = FunctionName;
      LineInfo.StartAddress = Start;
      if (LineInfo.FileName == DILineInfo::BadString && !FileName.empty())
        LineInfo.FileName = FileName;
    }
  }
  return LineInfo;
}

DIInliningInfo SymbolizableObjectFile::symbolizeInlinedCode(
    object::SectionedAddress ModuleOffset,
    DILineInfoSpecifier LineInfoSpecifier, bool UseSymbolTable) const {
  if (ModuleOffset.SectionIndex == object::SectionedAddress::UndefSection)
    ModuleOffset.SectionIndex =
        getModuleSectionIndexForAddress(ModuleOffset.Address);
  DIInliningInfo InlinedContext = DebugInfoContext->getInliningInfoForAddress(
      ModuleOffset, LineInfoSpecifier);

  // Make sure there is at least one frame in context.
  if (InlinedContext.getNumberOfFrames() == 0)
    InlinedContext.addFrame(DILineInfo());

  // Override the function name in lower frame with name from symbol table.
  if (shouldOverrideWithSymbolTable(LineInfoSpecifier.FNKind, UseSymbolTable)) {
    std::string FunctionName, FileName;
    uint64_t Start, Size;
    if (getNameFromSymbolTable(ModuleOffset.Address, FunctionName, Start, Size,
                               FileName)) {
      DILineInfo *LI = InlinedContext.getMutableFrame(
          InlinedContext.getNumberOfFrames() - 1);
      LI->FunctionName = FunctionName;
      LI->StartAddress = Start;
      if (LI->FileName == DILineInfo::BadString && !FileName.empty())
        LI->FileName = FileName;
    }
  }

  return InlinedContext;
}

DIGlobal SymbolizableObjectFile::symbolizeData(
    object::SectionedAddress ModuleOffset) const {
  DIGlobal Res;
  std::string FileName;
  getNameFromSymbolTable(ModuleOffset.Address, Res.Name, Res.Start, Res.Size,
                         FileName);
  Res.DeclFile = FileName;

  // Try and get a better filename:lineno pair from the debuginfo, if present.
  DILineInfo DL = DebugInfoContext->getLineInfoForDataAddress(ModuleOffset);
  if (DL.Line != 0) {
    Res.DeclFile = DL.FileName;
    Res.DeclLine = DL.Line;
  }
  return Res;
}

std::vector<DILocal> SymbolizableObjectFile::symbolizeFrame(
    object::SectionedAddress ModuleOffset) const {
  if (ModuleOffset.SectionIndex == object::SectionedAddress::UndefSection)
    ModuleOffset.SectionIndex =
        getModuleSectionIndexForAddress(ModuleOffset.Address);
  return DebugInfoContext->getLocalsForAddress(ModuleOffset);
}

std::vector<object::SectionedAddress>
SymbolizableObjectFile::findSymbol(StringRef Symbol, uint64_t Offset) const {
  std::vector<object::SectionedAddress> Result;
  for (const SymbolDesc &Sym : Symbols) {
    if (Sym.Name == Symbol) {
      uint64_t Addr = Sym.Addr;
      if (Offset < Sym.Size)
        Addr += Offset;
      object::SectionedAddress A{Addr, getModuleSectionIndexForAddress(Addr)};
      Result.push_back(A);
    }
  }
  return Result;
}

/// Search for the first occurence of specified Address in ObjectFile.
uint64_t SymbolizableObjectFile::getModuleSectionIndexForAddress(
    uint64_t Address) const {

  for (SectionRef Sec : Module->sections()) {
    if (!Sec.isText() || Sec.isVirtual())
      continue;

    if (Address >= Sec.getAddress() &&
        Address < Sec.getAddress() + Sec.getSize())
      return Sec.getIndex();
  }

  return object::SectionedAddress::UndefSection;
}
