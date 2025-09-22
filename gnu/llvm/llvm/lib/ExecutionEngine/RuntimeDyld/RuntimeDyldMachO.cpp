//===-- RuntimeDyldMachO.cpp - Run-time dynamic linker for MC-JIT -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implementation of the MC-JIT runtime dynamic linker.
//
//===----------------------------------------------------------------------===//

#include "RuntimeDyldMachO.h"
#include "Targets/RuntimeDyldMachOAArch64.h"
#include "Targets/RuntimeDyldMachOARM.h"
#include "Targets/RuntimeDyldMachOI386.h"
#include "Targets/RuntimeDyldMachOX86_64.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"

using namespace llvm;
using namespace llvm::object;

#define DEBUG_TYPE "dyld"

namespace {

class LoadedMachOObjectInfo final
    : public LoadedObjectInfoHelper<LoadedMachOObjectInfo,
                                    RuntimeDyld::LoadedObjectInfo> {
public:
  LoadedMachOObjectInfo(RuntimeDyldImpl &RTDyld,
                        ObjSectionToIDMap ObjSecToIDMap)
      : LoadedObjectInfoHelper(RTDyld, std::move(ObjSecToIDMap)) {}

  OwningBinary<ObjectFile>
  getObjectForDebug(const ObjectFile &Obj) const override {
    return OwningBinary<ObjectFile>();
  }
};

}

namespace llvm {

int64_t RuntimeDyldMachO::memcpyAddend(const RelocationEntry &RE) const {
  unsigned NumBytes = 1 << RE.Size;
  uint8_t *Src = Sections[RE.SectionID].getAddress() + RE.Offset;

  return static_cast<int64_t>(readBytesUnaligned(Src, NumBytes));
}

Expected<relocation_iterator>
RuntimeDyldMachO::processScatteredVANILLA(
                          unsigned SectionID, relocation_iterator RelI,
                          const ObjectFile &BaseObjT,
                          RuntimeDyldMachO::ObjSectionToIDMap &ObjSectionToID,
                          bool TargetIsLocalThumbFunc) {
  const MachOObjectFile &Obj =
    static_cast<const MachOObjectFile&>(BaseObjT);
  MachO::any_relocation_info RE =
    Obj.getRelocation(RelI->getRawDataRefImpl());

  SectionEntry &Section = Sections[SectionID];
  uint32_t RelocType = Obj.getAnyRelocationType(RE);
  bool IsPCRel = Obj.getAnyRelocationPCRel(RE);
  unsigned Size = Obj.getAnyRelocationLength(RE);
  uint64_t Offset = RelI->getOffset();
  uint8_t *LocalAddress = Section.getAddressWithOffset(Offset);
  unsigned NumBytes = 1 << Size;
  int64_t Addend = readBytesUnaligned(LocalAddress, NumBytes);

  unsigned SymbolBaseAddr = Obj.getScatteredRelocationValue(RE);
  section_iterator TargetSI = getSectionByAddress(Obj, SymbolBaseAddr);
  assert(TargetSI != Obj.section_end() && "Can't find section for symbol");
  uint64_t SectionBaseAddr = TargetSI->getAddress();
  SectionRef TargetSection = *TargetSI;
  bool IsCode = TargetSection.isText();
  uint32_t TargetSectionID = ~0U;
  if (auto TargetSectionIDOrErr =
        findOrEmitSection(Obj, TargetSection, IsCode, ObjSectionToID))
    TargetSectionID = *TargetSectionIDOrErr;
  else
    return TargetSectionIDOrErr.takeError();

  Addend -= SectionBaseAddr;
  RelocationEntry R(SectionID, Offset, RelocType, Addend, IsPCRel, Size);
  R.IsTargetThumbFunc = TargetIsLocalThumbFunc;

  addRelocationForSection(R, TargetSectionID);

  return ++RelI;
}


Expected<RelocationValueRef>
RuntimeDyldMachO::getRelocationValueRef(
    const ObjectFile &BaseTObj, const relocation_iterator &RI,
    const RelocationEntry &RE, ObjSectionToIDMap &ObjSectionToID) {

  const MachOObjectFile &Obj =
      static_cast<const MachOObjectFile &>(BaseTObj);
  MachO::any_relocation_info RelInfo =
      Obj.getRelocation(RI->getRawDataRefImpl());
  RelocationValueRef Value;

  bool IsExternal = Obj.getPlainRelocationExternal(RelInfo);
  if (IsExternal) {
    symbol_iterator Symbol = RI->getSymbol();
    StringRef TargetName;
    if (auto TargetNameOrErr = Symbol->getName())
      TargetName = *TargetNameOrErr;
    else
      return TargetNameOrErr.takeError();
    RTDyldSymbolTable::const_iterator SI =
      GlobalSymbolTable.find(TargetName.data());
    if (SI != GlobalSymbolTable.end()) {
      const auto &SymInfo = SI->second;
      Value.SectionID = SymInfo.getSectionID();
      Value.Offset = SymInfo.getOffset() + RE.Addend;
    } else {
      Value.SymbolName = TargetName.data();
      Value.Offset = RE.Addend;
    }
  } else {
    SectionRef Sec = Obj.getAnyRelocationSection(RelInfo);
    bool IsCode = Sec.isText();
    if (auto SectionIDOrErr = findOrEmitSection(Obj, Sec, IsCode,
                                                ObjSectionToID))
      Value.SectionID = *SectionIDOrErr;
    else
      return SectionIDOrErr.takeError();
    uint64_t Addr = Sec.getAddress();
    Value.Offset = RE.Addend - Addr;
  }

  return Value;
}

void RuntimeDyldMachO::makeValueAddendPCRel(RelocationValueRef &Value,
                                            const relocation_iterator &RI,
                                            unsigned OffsetToNextPC) {
  auto &O = *cast<MachOObjectFile>(RI->getObject());
  section_iterator SecI = O.getRelocationRelocatedSection(RI);
  Value.Offset += RI->getOffset() + OffsetToNextPC + SecI->getAddress();
}

void RuntimeDyldMachO::dumpRelocationToResolve(const RelocationEntry &RE,
                                               uint64_t Value) const {
  const SectionEntry &Section = Sections[RE.SectionID];
  uint8_t *LocalAddress = Section.getAddress() + RE.Offset;
  uint64_t FinalAddress = Section.getLoadAddress() + RE.Offset;

  dbgs() << "resolveRelocation Section: " << RE.SectionID
         << " LocalAddress: " << format("%p", LocalAddress)
         << " FinalAddress: " << format("0x%016" PRIx64, FinalAddress)
         << " Value: " << format("0x%016" PRIx64, Value) << " Addend: " << RE.Addend
         << " isPCRel: " << RE.IsPCRel << " MachoType: " << RE.RelType
         << " Size: " << (1 << RE.Size) << "\n";
}

section_iterator
RuntimeDyldMachO::getSectionByAddress(const MachOObjectFile &Obj,
                                      uint64_t Addr) {
  section_iterator SI = Obj.section_begin();
  section_iterator SE = Obj.section_end();

  for (; SI != SE; ++SI) {
    uint64_t SAddr = SI->getAddress();
    uint64_t SSize = SI->getSize();
    if ((Addr >= SAddr) && (Addr < SAddr + SSize))
      return SI;
  }

  return SE;
}


// Populate __pointers section.
Error RuntimeDyldMachO::populateIndirectSymbolPointersSection(
                                                    const MachOObjectFile &Obj,
                                                    const SectionRef &PTSection,
                                                    unsigned PTSectionID) {
  assert(!Obj.is64Bit() &&
         "Pointer table section not supported in 64-bit MachO.");

  MachO::dysymtab_command DySymTabCmd = Obj.getDysymtabLoadCommand();
  MachO::section Sec32 = Obj.getSection(PTSection.getRawDataRefImpl());
  uint32_t PTSectionSize = Sec32.size;
  unsigned FirstIndirectSymbol = Sec32.reserved1;
  const unsigned PTEntrySize = 4;
  unsigned NumPTEntries = PTSectionSize / PTEntrySize;
  unsigned PTEntryOffset = 0;

  assert((PTSectionSize % PTEntrySize) == 0 &&
         "Pointers section does not contain a whole number of stubs?");

  LLVM_DEBUG(dbgs() << "Populating pointer table section "
                    << Sections[PTSectionID].getName() << ", Section ID "
                    << PTSectionID << ", " << NumPTEntries << " entries, "
                    << PTEntrySize << " bytes each:\n");

  for (unsigned i = 0; i < NumPTEntries; ++i) {
    unsigned SymbolIndex =
      Obj.getIndirectSymbolTableEntry(DySymTabCmd, FirstIndirectSymbol + i);
    symbol_iterator SI = Obj.getSymbolByIndex(SymbolIndex);
    StringRef IndirectSymbolName;
    if (auto IndirectSymbolNameOrErr = SI->getName())
      IndirectSymbolName = *IndirectSymbolNameOrErr;
    else
      return IndirectSymbolNameOrErr.takeError();
    LLVM_DEBUG(dbgs() << "  " << IndirectSymbolName << ": index " << SymbolIndex
                      << ", PT offset: " << PTEntryOffset << "\n");
    RelocationEntry RE(PTSectionID, PTEntryOffset,
                       MachO::GENERIC_RELOC_VANILLA, 0, false, 2);
    addRelocationForSymbol(RE, IndirectSymbolName);
    PTEntryOffset += PTEntrySize;
  }
  return Error::success();
}

bool RuntimeDyldMachO::isCompatibleFile(const object::ObjectFile &Obj) const {
  return Obj.isMachO();
}

template <typename Impl>
Error
RuntimeDyldMachOCRTPBase<Impl>::finalizeLoad(const ObjectFile &Obj,
                                             ObjSectionToIDMap &SectionMap) {
  unsigned EHFrameSID = RTDYLD_INVALID_SECTION_ID;
  unsigned TextSID = RTDYLD_INVALID_SECTION_ID;
  unsigned ExceptTabSID = RTDYLD_INVALID_SECTION_ID;

  for (const auto &Section : Obj.sections()) {
    StringRef Name;
    if (Expected<StringRef> NameOrErr = Section.getName())
      Name = *NameOrErr;
    else
      consumeError(NameOrErr.takeError());

    // Force emission of the __text, __eh_frame, and __gcc_except_tab sections
    // if they're present. Otherwise call down to the impl to handle other
    // sections that have already been emitted.
    if (Name == "__text") {
      if (auto TextSIDOrErr = findOrEmitSection(Obj, Section, true, SectionMap))
        TextSID = *TextSIDOrErr;
      else
        return TextSIDOrErr.takeError();
    } else if (Name == "__eh_frame") {
      if (auto EHFrameSIDOrErr = findOrEmitSection(Obj, Section, false,
                                                   SectionMap))
        EHFrameSID = *EHFrameSIDOrErr;
      else
        return EHFrameSIDOrErr.takeError();
    } else if (Name == "__gcc_except_tab") {
      if (auto ExceptTabSIDOrErr = findOrEmitSection(Obj, Section, true,
                                                     SectionMap))
        ExceptTabSID = *ExceptTabSIDOrErr;
      else
        return ExceptTabSIDOrErr.takeError();
    } else {
      auto I = SectionMap.find(Section);
      if (I != SectionMap.end())
        if (auto Err = impl().finalizeSection(Obj, I->second, Section))
          return Err;
    }
  }
  UnregisteredEHFrameSections.push_back(
    EHFrameRelatedSections(EHFrameSID, TextSID, ExceptTabSID));

  return Error::success();
}

template <typename Impl>
unsigned char *RuntimeDyldMachOCRTPBase<Impl>::processFDE(uint8_t *P,
                                                          int64_t DeltaForText,
                                                          int64_t DeltaForEH) {
  typedef typename Impl::TargetPtrT TargetPtrT;

  LLVM_DEBUG(dbgs() << "Processing FDE: Delta for text: " << DeltaForText
                    << ", Delta for EH: " << DeltaForEH << "\n");
  uint32_t Length = readBytesUnaligned(P, 4);
  P += 4;
  uint8_t *Ret = P + Length;
  uint32_t Offset = readBytesUnaligned(P, 4);
  if (Offset == 0) // is a CIE
    return Ret;

  P += 4;
  TargetPtrT FDELocation = readBytesUnaligned(P, sizeof(TargetPtrT));
  TargetPtrT NewLocation = FDELocation - DeltaForText;
  writeBytesUnaligned(NewLocation, P, sizeof(TargetPtrT));

  P += sizeof(TargetPtrT);

  // Skip the FDE address range
  P += sizeof(TargetPtrT);

  uint8_t Augmentationsize = *P;
  P += 1;
  if (Augmentationsize != 0) {
    TargetPtrT LSDA = readBytesUnaligned(P, sizeof(TargetPtrT));
    TargetPtrT NewLSDA = LSDA - DeltaForEH;
    writeBytesUnaligned(NewLSDA, P, sizeof(TargetPtrT));
  }

  return Ret;
}

static int64_t computeDelta(SectionEntry *A, SectionEntry *B) {
  int64_t ObjDistance = static_cast<int64_t>(A->getObjAddress()) -
                        static_cast<int64_t>(B->getObjAddress());
  int64_t MemDistance = A->getLoadAddress() - B->getLoadAddress();
  return ObjDistance - MemDistance;
}

template <typename Impl>
void RuntimeDyldMachOCRTPBase<Impl>::registerEHFrames() {

  for (int i = 0, e = UnregisteredEHFrameSections.size(); i != e; ++i) {
    EHFrameRelatedSections &SectionInfo = UnregisteredEHFrameSections[i];
    if (SectionInfo.EHFrameSID == RTDYLD_INVALID_SECTION_ID ||
        SectionInfo.TextSID == RTDYLD_INVALID_SECTION_ID)
      continue;
    SectionEntry *Text = &Sections[SectionInfo.TextSID];
    SectionEntry *EHFrame = &Sections[SectionInfo.EHFrameSID];
    SectionEntry *ExceptTab = nullptr;
    if (SectionInfo.ExceptTabSID != RTDYLD_INVALID_SECTION_ID)
      ExceptTab = &Sections[SectionInfo.ExceptTabSID];

    int64_t DeltaForText = computeDelta(Text, EHFrame);
    int64_t DeltaForEH = 0;
    if (ExceptTab)
      DeltaForEH = computeDelta(ExceptTab, EHFrame);

    uint8_t *P = EHFrame->getAddress();
    uint8_t *End = P + EHFrame->getSize();
    while (P != End) {
      P = processFDE(P, DeltaForText, DeltaForEH);
    }

    MemMgr.registerEHFrames(EHFrame->getAddress(), EHFrame->getLoadAddress(),
                            EHFrame->getSize());
  }
  UnregisteredEHFrameSections.clear();
}

std::unique_ptr<RuntimeDyldMachO>
RuntimeDyldMachO::create(Triple::ArchType Arch,
                         RuntimeDyld::MemoryManager &MemMgr,
                         JITSymbolResolver &Resolver) {
  switch (Arch) {
  default:
    llvm_unreachable("Unsupported target for RuntimeDyldMachO.");
    break;
  case Triple::arm:
    return std::make_unique<RuntimeDyldMachOARM>(MemMgr, Resolver);
  case Triple::aarch64:
    return std::make_unique<RuntimeDyldMachOAArch64>(MemMgr, Resolver);
  case Triple::aarch64_32:
    return std::make_unique<RuntimeDyldMachOAArch64>(MemMgr, Resolver);
  case Triple::x86:
    return std::make_unique<RuntimeDyldMachOI386>(MemMgr, Resolver);
  case Triple::x86_64:
    return std::make_unique<RuntimeDyldMachOX86_64>(MemMgr, Resolver);
  }
}

std::unique_ptr<RuntimeDyld::LoadedObjectInfo>
RuntimeDyldMachO::loadObject(const object::ObjectFile &O) {
  if (auto ObjSectionToIDOrErr = loadObjectImpl(O))
    return std::make_unique<LoadedMachOObjectInfo>(*this,
                                                    *ObjSectionToIDOrErr);
  else {
    HasError = true;
    raw_string_ostream ErrStream(ErrorStr);
    logAllUnhandledErrors(ObjSectionToIDOrErr.takeError(), ErrStream);
    return nullptr;
  }
}

} // end namespace llvm
