//===- lib/MC/MCContext.cpp - Machine Code Context ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCContext.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/Wasm.h"
#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeView.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFragment.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCLabel.h"
#include "llvm/MC/MCSectionCOFF.h"
#include "llvm/MC/MCSectionDXContainer.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCSectionGOFF.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCSectionSPIRV.h"
#include "llvm/MC/MCSectionWasm.h"
#include "llvm/MC/MCSectionXCOFF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolCOFF.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/MCSymbolGOFF.h"
#include "llvm/MC/MCSymbolMachO.h"
#include "llvm/MC/MCSymbolWasm.h"
#include "llvm/MC/MCSymbolXCOFF.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdlib>
#include <optional>
#include <tuple>
#include <utility>

using namespace llvm;

static void defaultDiagHandler(const SMDiagnostic &SMD, bool, const SourceMgr &,
                               std::vector<const MDNode *> &) {
  SMD.print(nullptr, errs());
}

MCContext::MCContext(const Triple &TheTriple, const MCAsmInfo *mai,
                     const MCRegisterInfo *mri, const MCSubtargetInfo *msti,
                     const SourceMgr *mgr, MCTargetOptions const *TargetOpts,
                     bool DoAutoReset, StringRef Swift5ReflSegmentName)
    : Swift5ReflectionSegmentName(Swift5ReflSegmentName), TT(TheTriple),
      SrcMgr(mgr), InlineSrcMgr(nullptr), DiagHandler(defaultDiagHandler),
      MAI(mai), MRI(mri), MSTI(msti), Symbols(Allocator),
      InlineAsmUsedLabelNames(Allocator),
      CurrentDwarfLoc(0, 0, 0, DWARF2_FLAG_IS_STMT, 0, 0),
      AutoReset(DoAutoReset), TargetOptions(TargetOpts) {
  SaveTempLabels = TargetOptions && TargetOptions->MCSaveTempLabels;
  SecureLogFile = TargetOptions ? TargetOptions->AsSecureLogFile : "";

  if (SrcMgr && SrcMgr->getNumBuffers())
    MainFileName = std::string(SrcMgr->getMemoryBuffer(SrcMgr->getMainFileID())
                                   ->getBufferIdentifier());

  switch (TheTriple.getObjectFormat()) {
  case Triple::MachO:
    Env = IsMachO;
    break;
  case Triple::COFF:
    if (!TheTriple.isOSWindows() && !TheTriple.isUEFI())
      report_fatal_error(
          "Cannot initialize MC for non-Windows COFF object files.");

    Env = IsCOFF;
    break;
  case Triple::ELF:
    Env = IsELF;
    break;
  case Triple::Wasm:
    Env = IsWasm;
    break;
  case Triple::XCOFF:
    Env = IsXCOFF;
    break;
  case Triple::GOFF:
    Env = IsGOFF;
    break;
  case Triple::DXContainer:
    Env = IsDXContainer;
    break;
  case Triple::SPIRV:
    Env = IsSPIRV;
    break;
  case Triple::UnknownObjectFormat:
    report_fatal_error("Cannot initialize MC for unknown object file format.");
    break;
  }
}

MCContext::~MCContext() {
  if (AutoReset)
    reset();

  // NOTE: The symbols are all allocated out of a bump pointer allocator,
  // we don't need to free them here.
}

void MCContext::initInlineSourceManager() {
  if (!InlineSrcMgr)
    InlineSrcMgr.reset(new SourceMgr());
}

//===----------------------------------------------------------------------===//
// Module Lifetime Management
//===----------------------------------------------------------------------===//

void MCContext::reset() {
  SrcMgr = nullptr;
  InlineSrcMgr.reset();
  LocInfos.clear();
  DiagHandler = defaultDiagHandler;

  // Call the destructors so the fragments are freed
  COFFAllocator.DestroyAll();
  DXCAllocator.DestroyAll();
  ELFAllocator.DestroyAll();
  GOFFAllocator.DestroyAll();
  MachOAllocator.DestroyAll();
  WasmAllocator.DestroyAll();
  XCOFFAllocator.DestroyAll();
  MCInstAllocator.DestroyAll();
  SPIRVAllocator.DestroyAll();
  WasmSignatureAllocator.DestroyAll();

  // ~CodeViewContext may destroy a MCFragment outside of sections and need to
  // be reset before FragmentAllocator.
  CVContext.reset();

  MCSubtargetAllocator.DestroyAll();
  InlineAsmUsedLabelNames.clear();
  Symbols.clear();
  Allocator.Reset();
  FragmentAllocator.Reset();
  Instances.clear();
  CompilationDir.clear();
  MainFileName.clear();
  MCDwarfLineTablesCUMap.clear();
  SectionsForRanges.clear();
  MCGenDwarfLabelEntries.clear();
  DwarfDebugFlags = StringRef();
  DwarfCompileUnitID = 0;
  CurrentDwarfLoc = MCDwarfLoc(0, 0, 0, DWARF2_FLAG_IS_STMT, 0, 0);

  MachOUniquingMap.clear();
  ELFUniquingMap.clear();
  GOFFUniquingMap.clear();
  COFFUniquingMap.clear();
  WasmUniquingMap.clear();
  XCOFFUniquingMap.clear();
  DXCUniquingMap.clear();

  ELFEntrySizeMap.clear();
  ELFSeenGenericMergeableSections.clear();

  DwarfLocSeen = false;
  GenDwarfForAssembly = false;
  GenDwarfFileNumber = 0;

  HadError = false;
}

//===----------------------------------------------------------------------===//
// MCInst Management
//===----------------------------------------------------------------------===//

MCInst *MCContext::createMCInst() {
  return new (MCInstAllocator.Allocate()) MCInst;
}

// Allocate the initial MCDataFragment for the begin symbol.
MCDataFragment *MCContext::allocInitialFragment(MCSection &Sec) {
  assert(!Sec.curFragList()->Head);
  auto *F = allocFragment<MCDataFragment>();
  F->setParent(&Sec);
  Sec.curFragList()->Head = F;
  Sec.curFragList()->Tail = F;
  return F;
}

//===----------------------------------------------------------------------===//
// Symbol Manipulation
//===----------------------------------------------------------------------===//

MCSymbol *MCContext::getOrCreateSymbol(const Twine &Name) {
  SmallString<128> NameSV;
  StringRef NameRef = Name.toStringRef(NameSV);

  assert(!NameRef.empty() && "Normal symbols cannot be unnamed!");

  MCSymbolTableEntry &Entry = getSymbolTableEntry(NameRef);
  if (!Entry.second.Symbol) {
    bool IsRenamable = NameRef.starts_with(MAI->getPrivateGlobalPrefix());
    bool IsTemporary = IsRenamable && !SaveTempLabels;
    if (!Entry.second.Used) {
      Entry.second.Used = true;
      Entry.second.Symbol = createSymbolImpl(&Entry, IsTemporary);
    } else {
      assert(IsRenamable && "cannot rename non-private symbol");
      // Slow path: we need to rename a temp symbol from the user.
      Entry.second.Symbol = createRenamableSymbol(NameRef, false, IsTemporary);
    }
  }

  return Entry.second.Symbol;
}

MCSymbol *MCContext::getOrCreateFrameAllocSymbol(const Twine &FuncName,
                                                 unsigned Idx) {
  return getOrCreateSymbol(MAI->getPrivateGlobalPrefix() + FuncName +
                           "$frame_escape_" + Twine(Idx));
}

MCSymbol *MCContext::getOrCreateParentFrameOffsetSymbol(const Twine &FuncName) {
  return getOrCreateSymbol(MAI->getPrivateGlobalPrefix() + FuncName +
                           "$parent_frame_offset");
}

MCSymbol *MCContext::getOrCreateLSDASymbol(const Twine &FuncName) {
  return getOrCreateSymbol(MAI->getPrivateGlobalPrefix() + "__ehtable$" +
                           FuncName);
}

MCSymbolTableEntry &MCContext::getSymbolTableEntry(StringRef Name) {
  return *Symbols.try_emplace(Name, MCSymbolTableValue{}).first;
}

MCSymbol *MCContext::createSymbolImpl(const MCSymbolTableEntry *Name,
                                      bool IsTemporary) {
  static_assert(std::is_trivially_destructible<MCSymbolCOFF>(),
                "MCSymbol classes must be trivially destructible");
  static_assert(std::is_trivially_destructible<MCSymbolELF>(),
                "MCSymbol classes must be trivially destructible");
  static_assert(std::is_trivially_destructible<MCSymbolMachO>(),
                "MCSymbol classes must be trivially destructible");
  static_assert(std::is_trivially_destructible<MCSymbolWasm>(),
                "MCSymbol classes must be trivially destructible");
  static_assert(std::is_trivially_destructible<MCSymbolXCOFF>(),
                "MCSymbol classes must be trivially destructible");

  switch (getObjectFileType()) {
  case MCContext::IsCOFF:
    return new (Name, *this) MCSymbolCOFF(Name, IsTemporary);
  case MCContext::IsELF:
    return new (Name, *this) MCSymbolELF(Name, IsTemporary);
  case MCContext::IsGOFF:
    return new (Name, *this) MCSymbolGOFF(Name, IsTemporary);
  case MCContext::IsMachO:
    return new (Name, *this) MCSymbolMachO(Name, IsTemporary);
  case MCContext::IsWasm:
    return new (Name, *this) MCSymbolWasm(Name, IsTemporary);
  case MCContext::IsXCOFF:
    return createXCOFFSymbolImpl(Name, IsTemporary);
  case MCContext::IsDXContainer:
    break;
  case MCContext::IsSPIRV:
    return new (Name, *this)
        MCSymbol(MCSymbol::SymbolKindUnset, Name, IsTemporary);
  }
  return new (Name, *this)
      MCSymbol(MCSymbol::SymbolKindUnset, Name, IsTemporary);
}

MCSymbol *MCContext::createRenamableSymbol(const Twine &Name,
                                           bool AlwaysAddSuffix,
                                           bool IsTemporary) {
  SmallString<128> NewName;
  Name.toVector(NewName);
  size_t NameLen = NewName.size();

  MCSymbolTableEntry &NameEntry = getSymbolTableEntry(NewName.str());
  MCSymbolTableEntry *EntryPtr = &NameEntry;
  while (AlwaysAddSuffix || EntryPtr->second.Used) {
    AlwaysAddSuffix = false;

    NewName.resize(NameLen);
    raw_svector_ostream(NewName) << NameEntry.second.NextUniqueID++;
    EntryPtr = &getSymbolTableEntry(NewName.str());
  }

  EntryPtr->second.Used = true;
  return createSymbolImpl(EntryPtr, IsTemporary);
}

MCSymbol *MCContext::createTempSymbol(const Twine &Name, bool AlwaysAddSuffix) {
  if (!UseNamesOnTempLabels)
    return createSymbolImpl(nullptr, /*IsTemporary=*/true);
  return createRenamableSymbol(MAI->getPrivateGlobalPrefix() + Name,
                               AlwaysAddSuffix, /*IsTemporary=*/true);
}

MCSymbol *MCContext::createNamedTempSymbol(const Twine &Name) {
  return createRenamableSymbol(MAI->getPrivateGlobalPrefix() + Name, true,
                               /*IsTemporary=*/!SaveTempLabels);
}

MCSymbol *MCContext::createBlockSymbol(const Twine &Name, bool AlwaysEmit) {
  if (AlwaysEmit)
    return getOrCreateSymbol(MAI->getPrivateLabelPrefix() + Name);

  bool IsTemporary = !SaveTempLabels;
  if (IsTemporary && !UseNamesOnTempLabels)
    return createSymbolImpl(nullptr, IsTemporary);
  return createRenamableSymbol(MAI->getPrivateLabelPrefix() + Name,
                               /*AlwaysAddSuffix=*/false, IsTemporary);
}

MCSymbol *MCContext::createLinkerPrivateTempSymbol() {
  return createLinkerPrivateSymbol("tmp");
}

MCSymbol *MCContext::createLinkerPrivateSymbol(const Twine &Name) {
  return createRenamableSymbol(MAI->getLinkerPrivateGlobalPrefix() + Name,
                               /*AlwaysAddSuffix=*/true,
                               /*IsTemporary=*/false);
}

MCSymbol *MCContext::createTempSymbol() { return createTempSymbol("tmp"); }

MCSymbol *MCContext::createNamedTempSymbol() {
  return createNamedTempSymbol("tmp");
}

MCSymbol *MCContext::createLocalSymbol(StringRef Name) {
  MCSymbolTableEntry &NameEntry = getSymbolTableEntry(Name);
  return createSymbolImpl(&NameEntry, /*IsTemporary=*/false);
}

unsigned MCContext::NextInstance(unsigned LocalLabelVal) {
  MCLabel *&Label = Instances[LocalLabelVal];
  if (!Label)
    Label = new (*this) MCLabel(0);
  return Label->incInstance();
}

unsigned MCContext::GetInstance(unsigned LocalLabelVal) {
  MCLabel *&Label = Instances[LocalLabelVal];
  if (!Label)
    Label = new (*this) MCLabel(0);
  return Label->getInstance();
}

MCSymbol *MCContext::getOrCreateDirectionalLocalSymbol(unsigned LocalLabelVal,
                                                       unsigned Instance) {
  MCSymbol *&Sym = LocalSymbols[std::make_pair(LocalLabelVal, Instance)];
  if (!Sym)
    Sym = createNamedTempSymbol();
  return Sym;
}

MCSymbol *MCContext::createDirectionalLocalSymbol(unsigned LocalLabelVal) {
  unsigned Instance = NextInstance(LocalLabelVal);
  return getOrCreateDirectionalLocalSymbol(LocalLabelVal, Instance);
}

MCSymbol *MCContext::getDirectionalLocalSymbol(unsigned LocalLabelVal,
                                               bool Before) {
  unsigned Instance = GetInstance(LocalLabelVal);
  if (!Before)
    ++Instance;
  return getOrCreateDirectionalLocalSymbol(LocalLabelVal, Instance);
}

template <typename Symbol>
Symbol *MCContext::getOrCreateSectionSymbol(StringRef Section) {
  Symbol *R;
  auto &SymEntry = getSymbolTableEntry(Section);
  MCSymbol *Sym = SymEntry.second.Symbol;
  // A section symbol can not redefine regular symbols. There may be multiple
  // sections with the same name, in which case the first such section wins.
  if (Sym && Sym->isDefined() &&
      (!Sym->isInSection() || Sym->getSection().getBeginSymbol() != Sym))
    reportError(SMLoc(), "invalid symbol redefinition");
  if (Sym && Sym->isUndefined()) {
    R = cast<Symbol>(Sym);
  } else {
    SymEntry.second.Used = true;
    R = new (&SymEntry, *this) Symbol(&SymEntry, /*isTemporary=*/false);
    if (!Sym)
      SymEntry.second.Symbol = R;
  }
  return R;
}

MCSymbol *MCContext::lookupSymbol(const Twine &Name) const {
  SmallString<128> NameSV;
  StringRef NameRef = Name.toStringRef(NameSV);
  return Symbols.lookup(NameRef).Symbol;
}

void MCContext::setSymbolValue(MCStreamer &Streamer, const Twine &Sym,
                               uint64_t Val) {
  auto Symbol = getOrCreateSymbol(Sym);
  Streamer.emitAssignment(Symbol, MCConstantExpr::create(Val, *this));
}

void MCContext::registerInlineAsmLabel(MCSymbol *Sym) {
  InlineAsmUsedLabelNames[Sym->getName()] = Sym;
}

wasm::WasmSignature *MCContext::createWasmSignature() {
  return new (WasmSignatureAllocator.Allocate()) wasm::WasmSignature;
}

MCSymbolXCOFF *MCContext::createXCOFFSymbolImpl(const MCSymbolTableEntry *Name,
                                                bool IsTemporary) {
  if (!Name)
    return new (nullptr, *this) MCSymbolXCOFF(nullptr, IsTemporary);

  StringRef OriginalName = Name->first();
  if (OriginalName.starts_with("._Renamed..") ||
      OriginalName.starts_with("_Renamed.."))
    reportError(SMLoc(), "invalid symbol name from source");

  if (MAI->isValidUnquotedName(OriginalName))
    return new (Name, *this) MCSymbolXCOFF(Name, IsTemporary);

  // Now we have a name that contains invalid character(s) for XCOFF symbol.
  // Let's replace with something valid, but save the original name so that
  // we could still use the original name in the symbol table.
  SmallString<128> InvalidName(OriginalName);

  // If it's an entry point symbol, we will keep the '.'
  // in front for the convention purpose. Otherwise, add "_Renamed.."
  // as prefix to signal this is an renamed symbol.
  const bool IsEntryPoint = InvalidName.starts_with(".");
  SmallString<128> ValidName =
      StringRef(IsEntryPoint ? "._Renamed.." : "_Renamed..");

  // Append the hex values of '_' and invalid characters with "_Renamed..";
  // at the same time replace invalid characters with '_'.
  for (size_t I = 0; I < InvalidName.size(); ++I) {
    if (!MAI->isAcceptableChar(InvalidName[I]) || InvalidName[I] == '_') {
      raw_svector_ostream(ValidName).write_hex(InvalidName[I]);
      InvalidName[I] = '_';
    }
  }

  // Skip entry point symbol's '.' as we already have a '.' in front of
  // "_Renamed".
  if (IsEntryPoint)
    ValidName.append(InvalidName.substr(1, InvalidName.size() - 1));
  else
    ValidName.append(InvalidName);

  MCSymbolTableEntry &NameEntry = getSymbolTableEntry(ValidName.str());
  assert(!NameEntry.second.Used && "This name is used somewhere else.");
  NameEntry.second.Used = true;
  // Have the MCSymbol object itself refer to the copy of the string
  // that is embedded in the symbol table entry.
  MCSymbolXCOFF *XSym =
      new (&NameEntry, *this) MCSymbolXCOFF(&NameEntry, IsTemporary);
  XSym->setSymbolTableName(MCSymbolXCOFF::getUnqualifiedName(OriginalName));
  return XSym;
}

//===----------------------------------------------------------------------===//
// Section Management
//===----------------------------------------------------------------------===//

MCSectionMachO *MCContext::getMachOSection(StringRef Segment, StringRef Section,
                                           unsigned TypeAndAttributes,
                                           unsigned Reserved2, SectionKind Kind,
                                           const char *BeginSymName) {
  // We unique sections by their segment/section pair.  The returned section
  // may not have the same flags as the requested section, if so this should be
  // diagnosed by the client as an error.

  // Form the name to look up.
  assert(Section.size() <= 16 && "section name is too long");
  assert(!memchr(Section.data(), '\0', Section.size()) &&
         "section name cannot contain NUL");

  // Do the lookup, if we have a hit, return it.
  auto R = MachOUniquingMap.try_emplace((Segment + Twine(',') + Section).str());
  if (!R.second)
    return R.first->second;

  MCSymbol *Begin = nullptr;
  if (BeginSymName)
    Begin = createTempSymbol(BeginSymName, false);

  // Otherwise, return a new section.
  StringRef Name = R.first->first();
  auto *Ret = new (MachOAllocator.Allocate())
      MCSectionMachO(Segment, Name.substr(Name.size() - Section.size()),
                     TypeAndAttributes, Reserved2, Kind, Begin);
  R.first->second = Ret;
  allocInitialFragment(*Ret);
  return Ret;
}

MCSectionELF *MCContext::createELFSectionImpl(StringRef Section, unsigned Type,
                                              unsigned Flags,
                                              unsigned EntrySize,
                                              const MCSymbolELF *Group,
                                              bool Comdat, unsigned UniqueID,
                                              const MCSymbolELF *LinkedToSym) {
  auto *R = getOrCreateSectionSymbol<MCSymbolELF>(Section);
  R->setBinding(ELF::STB_LOCAL);
  R->setType(ELF::STT_SECTION);

  auto *Ret = new (ELFAllocator.Allocate()) MCSectionELF(
      Section, Type, Flags, EntrySize, Group, Comdat, UniqueID, R, LinkedToSym);

  auto *F = allocInitialFragment(*Ret);
  R->setFragment(F);
  return Ret;
}

MCSectionELF *
MCContext::createELFRelSection(const Twine &Name, unsigned Type, unsigned Flags,
                               unsigned EntrySize, const MCSymbolELF *Group,
                               const MCSectionELF *RelInfoSection) {
  StringMap<bool>::iterator I;
  bool Inserted;
  std::tie(I, Inserted) = RelSecNames.insert(std::make_pair(Name.str(), true));

  return createELFSectionImpl(
      I->getKey(), Type, Flags, EntrySize, Group, true, true,
      cast<MCSymbolELF>(RelInfoSection->getBeginSymbol()));
}

MCSectionELF *MCContext::getELFNamedSection(const Twine &Prefix,
                                            const Twine &Suffix, unsigned Type,
                                            unsigned Flags,
                                            unsigned EntrySize) {
  return getELFSection(Prefix + "." + Suffix, Type, Flags, EntrySize, Suffix,
                       /*IsComdat=*/true);
}

MCSectionELF *MCContext::getELFSection(const Twine &Section, unsigned Type,
                                       unsigned Flags, unsigned EntrySize,
                                       const Twine &Group, bool IsComdat,
                                       unsigned UniqueID,
                                       const MCSymbolELF *LinkedToSym) {
  MCSymbolELF *GroupSym = nullptr;
  if (!Group.isTriviallyEmpty() && !Group.str().empty())
    GroupSym = cast<MCSymbolELF>(getOrCreateSymbol(Group));

  return getELFSection(Section, Type, Flags, EntrySize, GroupSym, IsComdat,
                       UniqueID, LinkedToSym);
}

MCSectionELF *MCContext::getELFSection(const Twine &Section, unsigned Type,
                                       unsigned Flags, unsigned EntrySize,
                                       const MCSymbolELF *GroupSym,
                                       bool IsComdat, unsigned UniqueID,
                                       const MCSymbolELF *LinkedToSym) {
  StringRef Group = "";
  if (GroupSym)
    Group = GroupSym->getName();
  assert(!(LinkedToSym && LinkedToSym->getName().empty()));

  // Sections are differentiated by the quadruple (section_name, group_name,
  // unique_id, link_to_symbol_name). Sections sharing the same quadruple are
  // combined into one section. As an optimization, non-unique sections without
  // group or linked-to symbol have a shorter unique-ing key.
  std::pair<StringMap<MCSectionELF *>::iterator, bool> EntryNewPair;
  // Length of the section name, which are the first SectionLen bytes of the key
  unsigned SectionLen;
  if (GroupSym || LinkedToSym || UniqueID != MCSection::NonUniqueID) {
    SmallString<128> Buffer;
    Section.toVector(Buffer);
    SectionLen = Buffer.size();
    Buffer.push_back(0); // separator which cannot occur in the name
    if (GroupSym)
      Buffer.append(GroupSym->getName());
    Buffer.push_back(0); // separator which cannot occur in the name
    if (LinkedToSym)
      Buffer.append(LinkedToSym->getName());
    support::endian::write(Buffer, UniqueID, endianness::native);
    StringRef UniqueMapKey = StringRef(Buffer);
    EntryNewPair = ELFUniquingMap.insert(std::make_pair(UniqueMapKey, nullptr));
  } else if (!Section.isSingleStringRef()) {
    SmallString<128> Buffer;
    StringRef UniqueMapKey = Section.toStringRef(Buffer);
    SectionLen = UniqueMapKey.size();
    EntryNewPair = ELFUniquingMap.insert(std::make_pair(UniqueMapKey, nullptr));
  } else {
    StringRef UniqueMapKey = Section.getSingleStringRef();
    SectionLen = UniqueMapKey.size();
    EntryNewPair = ELFUniquingMap.insert(std::make_pair(UniqueMapKey, nullptr));
  }

  if (!EntryNewPair.second)
    return EntryNewPair.first->second;

  StringRef CachedName = EntryNewPair.first->getKey().take_front(SectionLen);

  MCSectionELF *Result =
      createELFSectionImpl(CachedName, Type, Flags, EntrySize, GroupSym,
                           IsComdat, UniqueID, LinkedToSym);
  EntryNewPair.first->second = Result;

  recordELFMergeableSectionInfo(Result->getName(), Result->getFlags(),
                                Result->getUniqueID(), Result->getEntrySize());

  return Result;
}

MCSectionELF *MCContext::createELFGroupSection(const MCSymbolELF *Group,
                                               bool IsComdat) {
  return createELFSectionImpl(".group", ELF::SHT_GROUP, 0, 4, Group, IsComdat,
                              MCSection::NonUniqueID, nullptr);
}

void MCContext::recordELFMergeableSectionInfo(StringRef SectionName,
                                              unsigned Flags, unsigned UniqueID,
                                              unsigned EntrySize) {
  bool IsMergeable = Flags & ELF::SHF_MERGE;
  if (UniqueID == GenericSectionID) {
    ELFSeenGenericMergeableSections.insert(SectionName);
    // Minor performance optimization: avoid hash map lookup in
    // isELFGenericMergeableSection, which will return true for SectionName.
    IsMergeable = true;
  }

  // For mergeable sections or non-mergeable sections with a generic mergeable
  // section name we enter their Unique ID into the ELFEntrySizeMap so that
  // compatible globals can be assigned to the same section.

  if (IsMergeable || isELFGenericMergeableSection(SectionName)) {
    ELFEntrySizeMap.insert(std::make_pair(
        std::make_tuple(SectionName, Flags, EntrySize), UniqueID));
  }
}

bool MCContext::isELFImplicitMergeableSectionNamePrefix(StringRef SectionName) {
  return SectionName.starts_with(".rodata.str") ||
         SectionName.starts_with(".rodata.cst");
}

bool MCContext::isELFGenericMergeableSection(StringRef SectionName) {
  return isELFImplicitMergeableSectionNamePrefix(SectionName) ||
         ELFSeenGenericMergeableSections.count(SectionName);
}

std::optional<unsigned>
MCContext::getELFUniqueIDForEntsize(StringRef SectionName, unsigned Flags,
                                    unsigned EntrySize) {
  auto I = ELFEntrySizeMap.find(std::make_tuple(SectionName, Flags, EntrySize));
  return (I != ELFEntrySizeMap.end()) ? std::optional<unsigned>(I->second)
                                      : std::nullopt;
}

MCSectionGOFF *MCContext::getGOFFSection(StringRef Section, SectionKind Kind,
                                         MCSection *Parent,
                                         uint32_t Subsection) {
  // Do the lookup. If we don't have a hit, return a new section.
  auto IterBool =
      GOFFUniquingMap.insert(std::make_pair(Section.str(), nullptr));
  auto Iter = IterBool.first;
  if (!IterBool.second)
    return Iter->second;

  StringRef CachedName = Iter->first;
  MCSectionGOFF *GOFFSection = new (GOFFAllocator.Allocate())
      MCSectionGOFF(CachedName, Kind, Parent, Subsection);
  Iter->second = GOFFSection;
  allocInitialFragment(*GOFFSection);
  return GOFFSection;
}

MCSectionCOFF *MCContext::getCOFFSection(StringRef Section,
                                         unsigned Characteristics,
                                         StringRef COMDATSymName, int Selection,
                                         unsigned UniqueID) {
  MCSymbol *COMDATSymbol = nullptr;
  if (!COMDATSymName.empty()) {
    COMDATSymbol = getOrCreateSymbol(COMDATSymName);
    COMDATSymName = COMDATSymbol->getName();
    // A non-associative COMDAT is considered to define the COMDAT symbol. Check
    // the redefinition error.
    if (Selection != COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE && COMDATSymbol &&
        COMDATSymbol->isDefined() &&
        (!COMDATSymbol->isInSection() ||
         cast<MCSectionCOFF>(COMDATSymbol->getSection()).getCOMDATSymbol() !=
             COMDATSymbol))
      reportError(SMLoc(), "invalid symbol redefinition");
  }

  // Do the lookup, if we have a hit, return it.
  COFFSectionKey T{Section, COMDATSymName, Selection, UniqueID};
  auto IterBool = COFFUniquingMap.insert(std::make_pair(T, nullptr));
  auto Iter = IterBool.first;
  if (!IterBool.second)
    return Iter->second;

  StringRef CachedName = Iter->first.SectionName;
  MCSymbol *Begin = getOrCreateSectionSymbol<MCSymbolCOFF>(Section);
  MCSectionCOFF *Result = new (COFFAllocator.Allocate()) MCSectionCOFF(
      CachedName, Characteristics, COMDATSymbol, Selection, Begin);
  Iter->second = Result;
  auto *F = allocInitialFragment(*Result);
  Begin->setFragment(F);
  return Result;
}

MCSectionCOFF *MCContext::getCOFFSection(StringRef Section,
                                         unsigned Characteristics) {
  return getCOFFSection(Section, Characteristics, "", 0, GenericSectionID);
}

MCSectionCOFF *MCContext::getAssociativeCOFFSection(MCSectionCOFF *Sec,
                                                    const MCSymbol *KeySym,
                                                    unsigned UniqueID) {
  // Return the normal section if we don't have to be associative or unique.
  if (!KeySym && UniqueID == GenericSectionID)
    return Sec;

  // If we have a key symbol, make an associative section with the same name and
  // kind as the normal section.
  unsigned Characteristics = Sec->getCharacteristics();
  if (KeySym) {
    Characteristics |= COFF::IMAGE_SCN_LNK_COMDAT;
    return getCOFFSection(Sec->getName(), Characteristics, KeySym->getName(),
                          COFF::IMAGE_COMDAT_SELECT_ASSOCIATIVE, UniqueID);
  }

  return getCOFFSection(Sec->getName(), Characteristics, "", 0, UniqueID);
}

MCSectionWasm *MCContext::getWasmSection(const Twine &Section, SectionKind K,
                                         unsigned Flags, const Twine &Group,
                                         unsigned UniqueID) {
  MCSymbolWasm *GroupSym = nullptr;
  if (!Group.isTriviallyEmpty() && !Group.str().empty()) {
    GroupSym = cast<MCSymbolWasm>(getOrCreateSymbol(Group));
    GroupSym->setComdat(true);
  }

  return getWasmSection(Section, K, Flags, GroupSym, UniqueID);
}

MCSectionWasm *MCContext::getWasmSection(const Twine &Section, SectionKind Kind,
                                         unsigned Flags,
                                         const MCSymbolWasm *GroupSym,
                                         unsigned UniqueID) {
  StringRef Group = "";
  if (GroupSym)
    Group = GroupSym->getName();
  // Do the lookup, if we have a hit, return it.
  auto IterBool = WasmUniquingMap.insert(
      std::make_pair(WasmSectionKey{Section.str(), Group, UniqueID}, nullptr));
  auto &Entry = *IterBool.first;
  if (!IterBool.second)
    return Entry.second;

  StringRef CachedName = Entry.first.SectionName;

  MCSymbol *Begin = createRenamableSymbol(CachedName, true, false);
  // Begin always has a different name than CachedName... see #48596.
  getSymbolTableEntry(Begin->getName()).second.Symbol = Begin;
  cast<MCSymbolWasm>(Begin)->setType(wasm::WASM_SYMBOL_TYPE_SECTION);

  MCSectionWasm *Result = new (WasmAllocator.Allocate())
      MCSectionWasm(CachedName, Kind, Flags, GroupSym, UniqueID, Begin);
  Entry.second = Result;

  auto *F = allocInitialFragment(*Result);
  Begin->setFragment(F);
  return Result;
}

bool MCContext::hasXCOFFSection(StringRef Section,
                                XCOFF::CsectProperties CsectProp) const {
  return XCOFFUniquingMap.count(
             XCOFFSectionKey(Section.str(), CsectProp.MappingClass)) != 0;
}

MCSectionXCOFF *MCContext::getXCOFFSection(
    StringRef Section, SectionKind Kind,
    std::optional<XCOFF::CsectProperties> CsectProp, bool MultiSymbolsAllowed,
    std::optional<XCOFF::DwarfSectionSubtypeFlags> DwarfSectionSubtypeFlags) {
  bool IsDwarfSec = DwarfSectionSubtypeFlags.has_value();
  assert((IsDwarfSec != CsectProp.has_value()) && "Invalid XCOFF section!");

  // Do the lookup. If we have a hit, return it.
  auto IterBool = XCOFFUniquingMap.insert(std::make_pair(
      IsDwarfSec ? XCOFFSectionKey(Section.str(), *DwarfSectionSubtypeFlags)
                 : XCOFFSectionKey(Section.str(), CsectProp->MappingClass),
      nullptr));
  auto &Entry = *IterBool.first;
  if (!IterBool.second) {
    MCSectionXCOFF *ExistedEntry = Entry.second;
    if (ExistedEntry->isMultiSymbolsAllowed() != MultiSymbolsAllowed)
      report_fatal_error("section's multiply symbols policy does not match");

    return ExistedEntry;
  }

  // Otherwise, return a new section.
  StringRef CachedName = Entry.first.SectionName;
  MCSymbolXCOFF *QualName = nullptr;
  // Debug section don't have storage class attribute.
  if (IsDwarfSec)
    QualName = cast<MCSymbolXCOFF>(getOrCreateSymbol(CachedName));
  else
    QualName = cast<MCSymbolXCOFF>(getOrCreateSymbol(
        CachedName + "[" +
        XCOFF::getMappingClassString(CsectProp->MappingClass) + "]"));

  // QualName->getUnqualifiedName() and CachedName are the same except when
  // CachedName contains invalid character(s) such as '$' for an XCOFF symbol.
  MCSectionXCOFF *Result = nullptr;
  if (IsDwarfSec)
    Result = new (XCOFFAllocator.Allocate()) MCSectionXCOFF(
        QualName->getUnqualifiedName(), Kind, QualName,
        *DwarfSectionSubtypeFlags, QualName, CachedName, MultiSymbolsAllowed);
  else
    Result = new (XCOFFAllocator.Allocate())
        MCSectionXCOFF(QualName->getUnqualifiedName(), CsectProp->MappingClass,
                       CsectProp->Type, Kind, QualName, nullptr, CachedName,
                       MultiSymbolsAllowed);

  Entry.second = Result;

  auto *F = allocInitialFragment(*Result);

  // We might miss calculating the symbols difference as absolute value before
  // adding fixups when symbol_A without the fragment set is the csect itself
  // and symbol_B is in it.
  // TODO: Currently we only set the fragment for XMC_PR csects and DWARF
  // sections because we don't have other cases that hit this problem yet.
  if (IsDwarfSec || CsectProp->MappingClass == XCOFF::XMC_PR)
    QualName->setFragment(F);

  return Result;
}

MCSectionSPIRV *MCContext::getSPIRVSection() {
  MCSectionSPIRV *Result = new (SPIRVAllocator.Allocate()) MCSectionSPIRV();

  allocInitialFragment(*Result);
  return Result;
}

MCSectionDXContainer *MCContext::getDXContainerSection(StringRef Section,
                                                       SectionKind K) {
  // Do the lookup, if we have a hit, return it.
  auto ItInsertedPair = DXCUniquingMap.try_emplace(Section);
  if (!ItInsertedPair.second)
    return ItInsertedPair.first->second;

  auto MapIt = ItInsertedPair.first;
  // Grab the name from the StringMap. Since the Section is going to keep a
  // copy of this StringRef we need to make sure the underlying string stays
  // alive as long as we need it.
  StringRef Name = MapIt->first();
  MapIt->second =
      new (DXCAllocator.Allocate()) MCSectionDXContainer(Name, K, nullptr);

  // The first fragment will store the header
  allocInitialFragment(*MapIt->second);
  return MapIt->second;
}

MCSubtargetInfo &MCContext::getSubtargetCopy(const MCSubtargetInfo &STI) {
  return *new (MCSubtargetAllocator.Allocate()) MCSubtargetInfo(STI);
}

void MCContext::addDebugPrefixMapEntry(const std::string &From,
                                       const std::string &To) {
  DebugPrefixMap.emplace_back(From, To);
}

void MCContext::remapDebugPath(SmallVectorImpl<char> &Path) {
  for (const auto &[From, To] : llvm::reverse(DebugPrefixMap))
    if (llvm::sys::path::replace_path_prefix(Path, From, To))
      break;
}

void MCContext::RemapDebugPaths() {
  const auto &DebugPrefixMap = this->DebugPrefixMap;
  if (DebugPrefixMap.empty())
    return;

  // Remap compilation directory.
  remapDebugPath(CompilationDir);

  // Remap MCDwarfDirs and RootFile.Name in all compilation units.
  SmallString<256> P;
  for (auto &CUIDTablePair : MCDwarfLineTablesCUMap) {
    for (auto &Dir : CUIDTablePair.second.getMCDwarfDirs()) {
      P = Dir;
      remapDebugPath(P);
      Dir = std::string(P);
    }

    // Used by DW_TAG_compile_unit's DT_AT_name and DW_TAG_label's
    // DW_AT_decl_file for DWARF v5 generated for assembly source.
    P = CUIDTablePair.second.getRootFile().Name;
    remapDebugPath(P);
    CUIDTablePair.second.getRootFile().Name = std::string(P);
  }
}

//===----------------------------------------------------------------------===//
// Dwarf Management
//===----------------------------------------------------------------------===//

EmitDwarfUnwindType MCContext::emitDwarfUnwindInfo() const {
  if (!TargetOptions)
    return EmitDwarfUnwindType::Default;
  return TargetOptions->EmitDwarfUnwind;
}

bool MCContext::emitCompactUnwindNonCanonical() const {
  if (TargetOptions)
    return TargetOptions->EmitCompactUnwindNonCanonical;
  return false;
}

void MCContext::setGenDwarfRootFile(StringRef InputFileName, StringRef Buffer) {
  // MCDwarf needs the root file as well as the compilation directory.
  // If we find a '.file 0' directive that will supersede these values.
  std::optional<MD5::MD5Result> Cksum;
  if (getDwarfVersion() >= 5) {
    MD5 Hash;
    MD5::MD5Result Sum;
    Hash.update(Buffer);
    Hash.final(Sum);
    Cksum = Sum;
  }
  // Canonicalize the root filename. It cannot be empty, and should not
  // repeat the compilation dir.
  // The MCContext ctor initializes MainFileName to the name associated with
  // the SrcMgr's main file ID, which might be the same as InputFileName (and
  // possibly include directory components).
  // Or, MainFileName might have been overridden by a -main-file-name option,
  // which is supposed to be just a base filename with no directory component.
  // So, if the InputFileName and MainFileName are not equal, assume
  // MainFileName is a substitute basename and replace the last component.
  SmallString<1024> FileNameBuf = InputFileName;
  if (FileNameBuf.empty() || FileNameBuf == "-")
    FileNameBuf = "<stdin>";
  if (!getMainFileName().empty() && FileNameBuf != getMainFileName()) {
    llvm::sys::path::remove_filename(FileNameBuf);
    llvm::sys::path::append(FileNameBuf, getMainFileName());
  }
  StringRef FileName = FileNameBuf;
  if (FileName.consume_front(getCompilationDir()))
    if (llvm::sys::path::is_separator(FileName.front()))
      FileName = FileName.drop_front();
  assert(!FileName.empty());
  setMCLineTableRootFile(
      /*CUID=*/0, getCompilationDir(), FileName, Cksum, std::nullopt);
}

/// getDwarfFile - takes a file name and number to place in the dwarf file and
/// directory tables.  If the file number has already been allocated it is an
/// error and zero is returned and the client reports the error, else the
/// allocated file number is returned.  The file numbers may be in any order.
Expected<unsigned>
MCContext::getDwarfFile(StringRef Directory, StringRef FileName,
                        unsigned FileNumber,
                        std::optional<MD5::MD5Result> Checksum,
                        std::optional<StringRef> Source, unsigned CUID) {
  MCDwarfLineTable &Table = MCDwarfLineTablesCUMap[CUID];
  return Table.tryGetFile(Directory, FileName, Checksum, Source, DwarfVersion,
                          FileNumber);
}

/// isValidDwarfFileNumber - takes a dwarf file number and returns true if it
/// currently is assigned and false otherwise.
bool MCContext::isValidDwarfFileNumber(unsigned FileNumber, unsigned CUID) {
  const MCDwarfLineTable &LineTable = getMCDwarfLineTable(CUID);
  if (FileNumber == 0)
    return getDwarfVersion() >= 5;
  if (FileNumber >= LineTable.getMCDwarfFiles().size())
    return false;

  return !LineTable.getMCDwarfFiles()[FileNumber].Name.empty();
}

/// Remove empty sections from SectionsForRanges, to avoid generating
/// useless debug info for them.
void MCContext::finalizeDwarfSections(MCStreamer &MCOS) {
  SectionsForRanges.remove_if(
      [&](MCSection *Sec) { return !MCOS.mayHaveInstructions(*Sec); });
}

CodeViewContext &MCContext::getCVContext() {
  if (!CVContext)
    CVContext.reset(new CodeViewContext(this));
  return *CVContext;
}

//===----------------------------------------------------------------------===//
// Error Reporting
//===----------------------------------------------------------------------===//

void MCContext::diagnose(const SMDiagnostic &SMD) {
  assert(DiagHandler && "MCContext::DiagHandler is not set");
  bool UseInlineSrcMgr = false;
  const SourceMgr *SMP = nullptr;
  if (SrcMgr) {
    SMP = SrcMgr;
  } else if (InlineSrcMgr) {
    SMP = InlineSrcMgr.get();
    UseInlineSrcMgr = true;
  } else
    llvm_unreachable("Either SourceMgr should be available");
  DiagHandler(SMD, UseInlineSrcMgr, *SMP, LocInfos);
}

void MCContext::reportCommon(
    SMLoc Loc,
    std::function<void(SMDiagnostic &, const SourceMgr *)> GetMessage) {
  // * MCContext::SrcMgr is null when the MC layer emits machine code for input
  //   other than assembly file, say, for .c/.cpp/.ll/.bc.
  // * MCContext::InlineSrcMgr is null when the inline asm is not used.
  // * A default SourceMgr is needed for diagnosing when both MCContext::SrcMgr
  //   and MCContext::InlineSrcMgr are null.
  SourceMgr SM;
  const SourceMgr *SMP = &SM;
  bool UseInlineSrcMgr = false;

  // FIXME: Simplify these by combining InlineSrcMgr & SrcMgr.
  //        For MC-only execution, only SrcMgr is used;
  //        For non MC-only execution, InlineSrcMgr is only ctor'd if there is
  //        inline asm in the IR.
  if (Loc.isValid()) {
    if (SrcMgr) {
      SMP = SrcMgr;
    } else if (InlineSrcMgr) {
      SMP = InlineSrcMgr.get();
      UseInlineSrcMgr = true;
    } else
      llvm_unreachable("Either SourceMgr should be available");
  }

  SMDiagnostic D;
  GetMessage(D, SMP);
  DiagHandler(D, UseInlineSrcMgr, *SMP, LocInfos);
}

void MCContext::reportError(SMLoc Loc, const Twine &Msg) {
  HadError = true;
  reportCommon(Loc, [&](SMDiagnostic &D, const SourceMgr *SMP) {
    D = SMP->GetMessage(Loc, SourceMgr::DK_Error, Msg);
  });
}

void MCContext::reportWarning(SMLoc Loc, const Twine &Msg) {
  if (TargetOptions && TargetOptions->MCNoWarn)
    return;
  if (TargetOptions && TargetOptions->MCFatalWarnings) {
    reportError(Loc, Msg);
  } else {
    reportCommon(Loc, [&](SMDiagnostic &D, const SourceMgr *SMP) {
      D = SMP->GetMessage(Loc, SourceMgr::DK_Warning, Msg);
    });
  }
}
