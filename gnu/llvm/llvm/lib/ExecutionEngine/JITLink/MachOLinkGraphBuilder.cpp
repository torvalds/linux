//=--------- MachOLinkGraphBuilder.cpp - MachO LinkGraph builder ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic MachO LinkGraph building code.
//
//===----------------------------------------------------------------------===//

#include "MachOLinkGraphBuilder.h"
#include <optional>

#define DEBUG_TYPE "jitlink"

static const char *CommonSectionName = "__common";

namespace llvm {
namespace jitlink {

MachOLinkGraphBuilder::~MachOLinkGraphBuilder() = default;

Expected<std::unique_ptr<LinkGraph>> MachOLinkGraphBuilder::buildGraph() {

  // We only operate on relocatable objects.
  if (!Obj.isRelocatableObject())
    return make_error<JITLinkError>("Object is not a relocatable MachO");

  if (auto Err = createNormalizedSections())
    return std::move(Err);

  if (auto Err = createNormalizedSymbols())
    return std::move(Err);

  if (auto Err = graphifyRegularSymbols())
    return std::move(Err);

  if (auto Err = graphifySectionsWithCustomParsers())
    return std::move(Err);

  if (auto Err = addRelocations())
    return std::move(Err);

  return std::move(G);
}

MachOLinkGraphBuilder::MachOLinkGraphBuilder(
    const object::MachOObjectFile &Obj, Triple TT, SubtargetFeatures Features,
    LinkGraph::GetEdgeKindNameFunction GetEdgeKindName)
    : Obj(Obj),
      G(std::make_unique<LinkGraph>(std::string(Obj.getFileName()),
                                    std::move(TT), std::move(Features),
                                    getPointerSize(Obj), getEndianness(Obj),
                                    std::move(GetEdgeKindName))) {
  auto &MachHeader = Obj.getHeader64();
  SubsectionsViaSymbols = MachHeader.flags & MachO::MH_SUBSECTIONS_VIA_SYMBOLS;
}

void MachOLinkGraphBuilder::addCustomSectionParser(
    StringRef SectionName, SectionParserFunction Parser) {
  assert(!CustomSectionParserFunctions.count(SectionName) &&
         "Custom parser for this section already exists");
  CustomSectionParserFunctions[SectionName] = std::move(Parser);
}

Linkage MachOLinkGraphBuilder::getLinkage(uint16_t Desc) {
  if ((Desc & MachO::N_WEAK_DEF) || (Desc & MachO::N_WEAK_REF))
    return Linkage::Weak;
  return Linkage::Strong;
}

Scope MachOLinkGraphBuilder::getScope(StringRef Name, uint8_t Type) {
  if (Type & MachO::N_EXT) {
    if ((Type & MachO::N_PEXT) || Name.starts_with("l"))
      return Scope::Hidden;
    else
      return Scope::Default;
  }
  return Scope::Local;
}

bool MachOLinkGraphBuilder::isAltEntry(const NormalizedSymbol &NSym) {
  return NSym.Desc & MachO::N_ALT_ENTRY;
}

bool MachOLinkGraphBuilder::isDebugSection(const NormalizedSection &NSec) {
  return (NSec.Flags & MachO::S_ATTR_DEBUG &&
          strcmp(NSec.SegName, "__DWARF") == 0);
}

bool MachOLinkGraphBuilder::isZeroFillSection(const NormalizedSection &NSec) {
  switch (NSec.Flags & MachO::SECTION_TYPE) {
  case MachO::S_ZEROFILL:
  case MachO::S_GB_ZEROFILL:
  case MachO::S_THREAD_LOCAL_ZEROFILL:
    return true;
  default:
    return false;
  }
}

unsigned
MachOLinkGraphBuilder::getPointerSize(const object::MachOObjectFile &Obj) {
  return Obj.is64Bit() ? 8 : 4;
}

llvm::endianness
MachOLinkGraphBuilder::getEndianness(const object::MachOObjectFile &Obj) {
  return Obj.isLittleEndian() ? llvm::endianness::little
                              : llvm::endianness::big;
}

Section &MachOLinkGraphBuilder::getCommonSection() {
  if (!CommonSection)
    CommonSection = &G->createSection(CommonSectionName,
                                      orc::MemProt::Read | orc::MemProt::Write);
  return *CommonSection;
}

Error MachOLinkGraphBuilder::createNormalizedSections() {
  // Build normalized sections. Verifies that section data is in-range (for
  // sections with content) and that address ranges are non-overlapping.

  LLVM_DEBUG(dbgs() << "Creating normalized sections...\n");

  for (auto &SecRef : Obj.sections()) {
    NormalizedSection NSec;
    uint32_t DataOffset = 0;

    auto SecIndex = Obj.getSectionIndex(SecRef.getRawDataRefImpl());

    if (Obj.is64Bit()) {
      const MachO::section_64 &Sec64 =
          Obj.getSection64(SecRef.getRawDataRefImpl());

      memcpy(&NSec.SectName, &Sec64.sectname, 16);
      NSec.SectName[16] = '\0';
      memcpy(&NSec.SegName, Sec64.segname, 16);
      NSec.SegName[16] = '\0';

      NSec.Address = orc::ExecutorAddr(Sec64.addr);
      NSec.Size = Sec64.size;
      NSec.Alignment = 1ULL << Sec64.align;
      NSec.Flags = Sec64.flags;
      DataOffset = Sec64.offset;
    } else {
      const MachO::section &Sec32 = Obj.getSection(SecRef.getRawDataRefImpl());

      memcpy(&NSec.SectName, &Sec32.sectname, 16);
      NSec.SectName[16] = '\0';
      memcpy(&NSec.SegName, Sec32.segname, 16);
      NSec.SegName[16] = '\0';

      NSec.Address = orc::ExecutorAddr(Sec32.addr);
      NSec.Size = Sec32.size;
      NSec.Alignment = 1ULL << Sec32.align;
      NSec.Flags = Sec32.flags;
      DataOffset = Sec32.offset;
    }

    LLVM_DEBUG({
      dbgs() << "  " << NSec.SegName << "," << NSec.SectName << ": "
             << formatv("{0:x16}", NSec.Address) << " -- "
             << formatv("{0:x16}", NSec.Address + NSec.Size)
             << ", align: " << NSec.Alignment << ", index: " << SecIndex
             << "\n";
    });

    // Get the section data if any.
    if (!isZeroFillSection(NSec)) {
      if (DataOffset + NSec.Size > Obj.getData().size())
        return make_error<JITLinkError>(
            "Section data extends past end of file");

      NSec.Data = Obj.getData().data() + DataOffset;
    }

    // Get prot flags.
    // FIXME: Make sure this test is correct (it's probably missing cases
    // as-is).
    orc::MemProt Prot;
    if (NSec.Flags & MachO::S_ATTR_PURE_INSTRUCTIONS)
      Prot = orc::MemProt::Read | orc::MemProt::Exec;
    else
      Prot = orc::MemProt::Read | orc::MemProt::Write;

    auto FullyQualifiedName =
        G->allocateContent(StringRef(NSec.SegName) + "," + NSec.SectName);
    NSec.GraphSection = &G->createSection(
        StringRef(FullyQualifiedName.data(), FullyQualifiedName.size()), Prot);

    // TODO: Are there any other criteria for NoAlloc lifetime?
    if (NSec.Flags & MachO::S_ATTR_DEBUG)
      NSec.GraphSection->setMemLifetime(orc::MemLifetime::NoAlloc);

    IndexToSection.insert(std::make_pair(SecIndex, std::move(NSec)));
  }

  std::vector<NormalizedSection *> Sections;
  Sections.reserve(IndexToSection.size());
  for (auto &KV : IndexToSection)
    Sections.push_back(&KV.second);

  // If we didn't end up creating any sections then bail out. The code below
  // assumes that we have at least one section.
  if (Sections.empty())
    return Error::success();

  llvm::sort(Sections,
             [](const NormalizedSection *LHS, const NormalizedSection *RHS) {
               assert(LHS && RHS && "Null section?");
               if (LHS->Address != RHS->Address)
                 return LHS->Address < RHS->Address;
               return LHS->Size < RHS->Size;
             });

  for (unsigned I = 0, E = Sections.size() - 1; I != E; ++I) {
    auto &Cur = *Sections[I];
    auto &Next = *Sections[I + 1];
    if (Next.Address < Cur.Address + Cur.Size)
      return make_error<JITLinkError>(
          "Address range for section " +
          formatv("\"{0}/{1}\" [ {2:x16} -- {3:x16} ] ", Cur.SegName,
                  Cur.SectName, Cur.Address, Cur.Address + Cur.Size) +
          "overlaps section \"" + Next.SegName + "/" + Next.SectName + "\"" +
          formatv("\"{0}/{1}\" [ {2:x16} -- {3:x16} ] ", Next.SegName,
                  Next.SectName, Next.Address, Next.Address + Next.Size));
  }

  return Error::success();
}

Error MachOLinkGraphBuilder::createNormalizedSymbols() {
  LLVM_DEBUG(dbgs() << "Creating normalized symbols...\n");

  for (auto &SymRef : Obj.symbols()) {

    unsigned SymbolIndex = Obj.getSymbolIndex(SymRef.getRawDataRefImpl());
    uint64_t Value;
    uint32_t NStrX;
    uint8_t Type;
    uint8_t Sect;
    uint16_t Desc;

    if (Obj.is64Bit()) {
      const MachO::nlist_64 &NL64 =
          Obj.getSymbol64TableEntry(SymRef.getRawDataRefImpl());
      Value = NL64.n_value;
      NStrX = NL64.n_strx;
      Type = NL64.n_type;
      Sect = NL64.n_sect;
      Desc = NL64.n_desc;
    } else {
      const MachO::nlist &NL32 =
          Obj.getSymbolTableEntry(SymRef.getRawDataRefImpl());
      Value = NL32.n_value;
      NStrX = NL32.n_strx;
      Type = NL32.n_type;
      Sect = NL32.n_sect;
      Desc = NL32.n_desc;
    }

    // Skip stabs.
    // FIXME: Are there other symbols we should be skipping?
    if (Type & MachO::N_STAB)
      continue;

    std::optional<StringRef> Name;
    if (NStrX) {
      if (auto NameOrErr = SymRef.getName())
        Name = *NameOrErr;
      else
        return NameOrErr.takeError();
    } else if (Type & MachO::N_EXT)
      return make_error<JITLinkError>("Symbol at index " +
                                      formatv("{0}", SymbolIndex) +
                                      " has no name (string table index 0), "
                                      "but N_EXT bit is set");

    LLVM_DEBUG({
      dbgs() << "  ";
      if (!Name)
        dbgs() << "<anonymous symbol>";
      else
        dbgs() << *Name;
      dbgs() << ": value = " << formatv("{0:x16}", Value)
             << ", type = " << formatv("{0:x2}", Type)
             << ", desc = " << formatv("{0:x4}", Desc) << ", sect = ";
      if (Sect)
        dbgs() << static_cast<unsigned>(Sect - 1);
      else
        dbgs() << "none";
      dbgs() << "\n";
    });

    // If this symbol has a section, verify that the addresses line up.
    if (Sect != 0) {
      auto NSec = findSectionByIndex(Sect - 1);
      if (!NSec)
        return NSec.takeError();

      if (orc::ExecutorAddr(Value) < NSec->Address ||
          orc::ExecutorAddr(Value) > NSec->Address + NSec->Size)
        return make_error<JITLinkError>("Address " + formatv("{0:x}", Value) +
                                        " for symbol " + *Name +
                                        " does not fall within section");

      if (!NSec->GraphSection) {
        LLVM_DEBUG({
          dbgs() << "  Skipping: Symbol is in section " << NSec->SegName << "/"
                 << NSec->SectName
                 << " which has no associated graph section.\n";
        });
        continue;
      }
    }

    IndexToSymbol[SymbolIndex] =
        &createNormalizedSymbol(*Name, Value, Type, Sect, Desc,
                                getLinkage(Desc), getScope(*Name, Type));
  }

  return Error::success();
}

void MachOLinkGraphBuilder::addSectionStartSymAndBlock(
    unsigned SecIndex, Section &GraphSec, orc::ExecutorAddr Address,
    const char *Data, orc::ExecutorAddrDiff Size, uint32_t Alignment,
    bool IsLive) {
  Block &B =
      Data ? G->createContentBlock(GraphSec, ArrayRef<char>(Data, Size),
                                   Address, Alignment, 0)
           : G->createZeroFillBlock(GraphSec, Size, Address, Alignment, 0);
  auto &Sym = G->addAnonymousSymbol(B, 0, Size, false, IsLive);
  auto SecI = IndexToSection.find(SecIndex);
  assert(SecI != IndexToSection.end() && "SecIndex invalid");
  auto &NSec = SecI->second;
  assert(!NSec.CanonicalSymbols.count(Sym.getAddress()) &&
         "Anonymous block start symbol clashes with existing symbol address");
  NSec.CanonicalSymbols[Sym.getAddress()] = &Sym;
}

Error MachOLinkGraphBuilder::graphifyRegularSymbols() {

  LLVM_DEBUG(dbgs() << "Creating graph symbols...\n");

  /// We only have 256 section indexes: Use a vector rather than a map.
  std::vector<std::vector<NormalizedSymbol *>> SecIndexToSymbols;
  SecIndexToSymbols.resize(256);

  // Create commons, externs, and absolutes, and partition all other symbols by
  // section.
  for (auto &KV : IndexToSymbol) {
    auto &NSym = *KV.second;

    switch (NSym.Type & MachO::N_TYPE) {
    case MachO::N_UNDF:
      if (NSym.Value) {
        if (!NSym.Name)
          return make_error<JITLinkError>("Anonymous common symbol at index " +
                                          Twine(KV.first));
        NSym.GraphSymbol = &G->addDefinedSymbol(
            G->createZeroFillBlock(getCommonSection(),
                                   orc::ExecutorAddrDiff(NSym.Value),
                                   orc::ExecutorAddr(),
                                   1ull << MachO::GET_COMM_ALIGN(NSym.Desc), 0),
            0, *NSym.Name, orc::ExecutorAddrDiff(NSym.Value), Linkage::Strong,
            NSym.S, false, NSym.Desc & MachO::N_NO_DEAD_STRIP);
      } else {
        if (!NSym.Name)
          return make_error<JITLinkError>("Anonymous external symbol at "
                                          "index " +
                                          Twine(KV.first));
        NSym.GraphSymbol = &G->addExternalSymbol(
            *NSym.Name, 0, (NSym.Desc & MachO::N_WEAK_REF) != 0);
      }
      break;
    case MachO::N_ABS:
      if (!NSym.Name)
        return make_error<JITLinkError>("Anonymous absolute symbol at index " +
                                        Twine(KV.first));
      NSym.GraphSymbol = &G->addAbsoluteSymbol(
          *NSym.Name, orc::ExecutorAddr(NSym.Value), 0, Linkage::Strong,
          getScope(*NSym.Name, NSym.Type), NSym.Desc & MachO::N_NO_DEAD_STRIP);
      break;
    case MachO::N_SECT:
      SecIndexToSymbols[NSym.Sect - 1].push_back(&NSym);
      break;
    case MachO::N_PBUD:
      return make_error<JITLinkError>(
          "Unupported N_PBUD symbol " +
          (NSym.Name ? ("\"" + *NSym.Name + "\"") : Twine("<anon>")) +
          " at index " + Twine(KV.first));
    case MachO::N_INDR:
      return make_error<JITLinkError>(
          "Unupported N_INDR symbol " +
          (NSym.Name ? ("\"" + *NSym.Name + "\"") : Twine("<anon>")) +
          " at index " + Twine(KV.first));
    default:
      return make_error<JITLinkError>(
          "Unrecognized symbol type " + Twine(NSym.Type & MachO::N_TYPE) +
          " for symbol " +
          (NSym.Name ? ("\"" + *NSym.Name + "\"") : Twine("<anon>")) +
          " at index " + Twine(KV.first));
    }
  }

  // Loop over sections performing regular graphification for those that
  // don't have custom parsers.
  for (auto &KV : IndexToSection) {
    auto SecIndex = KV.first;
    auto &NSec = KV.second;

    if (!NSec.GraphSection) {
      LLVM_DEBUG({
        dbgs() << "  " << NSec.SegName << "/" << NSec.SectName
               << " has no graph section. Skipping.\n";
      });
      continue;
    }

    // Skip sections with custom parsers.
    if (CustomSectionParserFunctions.count(NSec.GraphSection->getName())) {
      LLVM_DEBUG({
        dbgs() << "  Skipping section " << NSec.GraphSection->getName()
               << " as it has a custom parser.\n";
      });
      continue;
    } else if ((NSec.Flags & MachO::SECTION_TYPE) ==
               MachO::S_CSTRING_LITERALS) {
      if (auto Err = graphifyCStringSection(
              NSec, std::move(SecIndexToSymbols[SecIndex])))
        return Err;
      continue;
    } else
      LLVM_DEBUG({
        dbgs() << "  Graphifying regular section "
               << NSec.GraphSection->getName() << "...\n";
      });

    bool SectionIsNoDeadStrip = NSec.Flags & MachO::S_ATTR_NO_DEAD_STRIP;
    bool SectionIsText = NSec.Flags & MachO::S_ATTR_PURE_INSTRUCTIONS;

    auto &SecNSymStack = SecIndexToSymbols[SecIndex];

    // If this section is non-empty but there are no symbols covering it then
    // create one block and anonymous symbol to cover the entire section.
    if (SecNSymStack.empty()) {
      if (NSec.Size > 0) {
        LLVM_DEBUG({
          dbgs() << "    Section non-empty, but contains no symbols. "
                    "Creating anonymous block to cover "
                 << formatv("{0:x16}", NSec.Address) << " -- "
                 << formatv("{0:x16}", NSec.Address + NSec.Size) << "\n";
        });
        addSectionStartSymAndBlock(SecIndex, *NSec.GraphSection, NSec.Address,
                                   NSec.Data, NSec.Size, NSec.Alignment,
                                   SectionIsNoDeadStrip);
      } else
        LLVM_DEBUG({
          dbgs() << "    Section empty and contains no symbols. Skipping.\n";
        });
      continue;
    }

    // Sort the symbol stack in by address, alt-entry status, scope, and name.
    // We sort in reverse order so that symbols will be visited in the right
    // order when we pop off the stack below.
    llvm::sort(SecNSymStack, [](const NormalizedSymbol *LHS,
                                const NormalizedSymbol *RHS) {
      if (LHS->Value != RHS->Value)
        return LHS->Value > RHS->Value;
      if (isAltEntry(*LHS) != isAltEntry(*RHS))
        return isAltEntry(*RHS);
      if (LHS->S != RHS->S)
        return static_cast<uint8_t>(LHS->S) < static_cast<uint8_t>(RHS->S);
      return LHS->Name < RHS->Name;
    });

    // The first symbol in a section can not be an alt-entry symbol.
    if (!SecNSymStack.empty() && isAltEntry(*SecNSymStack.back()))
      return make_error<JITLinkError>(
          "First symbol in " + NSec.GraphSection->getName() + " is alt-entry");

    // If the section is non-empty but there is no symbol covering the start
    // address then add an anonymous one.
    if (orc::ExecutorAddr(SecNSymStack.back()->Value) != NSec.Address) {
      auto AnonBlockSize =
          orc::ExecutorAddr(SecNSymStack.back()->Value) - NSec.Address;
      LLVM_DEBUG({
        dbgs() << "    Section start not covered by symbol. "
               << "Creating anonymous block to cover [ " << NSec.Address
               << " -- " << (NSec.Address + AnonBlockSize) << " ]\n";
      });
      addSectionStartSymAndBlock(SecIndex, *NSec.GraphSection, NSec.Address,
                                 NSec.Data, AnonBlockSize, NSec.Alignment,
                                 SectionIsNoDeadStrip);
    }

    // Visit section symbols in order by popping off the reverse-sorted stack,
    // building graph symbols as we go.
    //
    // If MH_SUBSECTIONS_VIA_SYMBOLS is set we'll build a block for each
    // alt-entry chain.
    //
    // If MH_SUBSECTIONS_VIA_SYMBOLS is not set then we'll just build one block
    // for the whole section.
    while (!SecNSymStack.empty()) {
      SmallVector<NormalizedSymbol *, 8> BlockSyms;

      // Get the symbols in this alt-entry chain, or the whole section (if
      // !SubsectionsViaSymbols).
      BlockSyms.push_back(SecNSymStack.back());
      SecNSymStack.pop_back();
      while (!SecNSymStack.empty() &&
             (isAltEntry(*SecNSymStack.back()) ||
              SecNSymStack.back()->Value == BlockSyms.back()->Value ||
             !SubsectionsViaSymbols)) {
        BlockSyms.push_back(SecNSymStack.back());
        SecNSymStack.pop_back();
      }

      // BlockNSyms now contains the block symbols in reverse canonical order.
      auto BlockStart = orc::ExecutorAddr(BlockSyms.front()->Value);
      orc::ExecutorAddr BlockEnd =
          SecNSymStack.empty() ? NSec.Address + NSec.Size
                               : orc::ExecutorAddr(SecNSymStack.back()->Value);
      orc::ExecutorAddrDiff BlockOffset = BlockStart - NSec.Address;
      orc::ExecutorAddrDiff BlockSize = BlockEnd - BlockStart;

      LLVM_DEBUG({
        dbgs() << "    Creating block for " << formatv("{0:x16}", BlockStart)
               << " -- " << formatv("{0:x16}", BlockEnd) << ": "
               << NSec.GraphSection->getName() << " + "
               << formatv("{0:x16}", BlockOffset) << " with "
               << BlockSyms.size() << " symbol(s)...\n";
      });

      Block &B =
          NSec.Data
              ? G->createContentBlock(
                    *NSec.GraphSection,
                    ArrayRef<char>(NSec.Data + BlockOffset, BlockSize),
                    BlockStart, NSec.Alignment, BlockStart % NSec.Alignment)
              : G->createZeroFillBlock(*NSec.GraphSection, BlockSize,
                                       BlockStart, NSec.Alignment,
                                       BlockStart % NSec.Alignment);

      std::optional<orc::ExecutorAddr> LastCanonicalAddr;
      auto SymEnd = BlockEnd;
      while (!BlockSyms.empty()) {
        auto &NSym = *BlockSyms.back();
        BlockSyms.pop_back();

        bool SymLive =
            (NSym.Desc & MachO::N_NO_DEAD_STRIP) || SectionIsNoDeadStrip;

        auto &Sym = createStandardGraphSymbol(
            NSym, B, SymEnd - orc::ExecutorAddr(NSym.Value), SectionIsText,
            SymLive, LastCanonicalAddr != orc::ExecutorAddr(NSym.Value));

        if (LastCanonicalAddr != Sym.getAddress()) {
          if (LastCanonicalAddr)
            SymEnd = *LastCanonicalAddr;
          LastCanonicalAddr = Sym.getAddress();
        }
      }
    }
  }

  return Error::success();
}

Symbol &MachOLinkGraphBuilder::createStandardGraphSymbol(NormalizedSymbol &NSym,
                                                         Block &B, size_t Size,
                                                         bool IsText,
                                                         bool IsNoDeadStrip,
                                                         bool IsCanonical) {

  LLVM_DEBUG({
    dbgs() << "      " << formatv("{0:x16}", NSym.Value) << " -- "
           << formatv("{0:x16}", NSym.Value + Size) << ": ";
    if (!NSym.Name)
      dbgs() << "<anonymous symbol>";
    else
      dbgs() << NSym.Name;
    if (IsText)
      dbgs() << " [text]";
    if (IsNoDeadStrip)
      dbgs() << " [no-dead-strip]";
    if (!IsCanonical)
      dbgs() << " [non-canonical]";
    dbgs() << "\n";
  });

  auto SymOffset = orc::ExecutorAddr(NSym.Value) - B.getAddress();
  auto &Sym =
      NSym.Name
          ? G->addDefinedSymbol(B, SymOffset, *NSym.Name, Size, NSym.L, NSym.S,
                                IsText, IsNoDeadStrip)
          : G->addAnonymousSymbol(B, SymOffset, Size, IsText, IsNoDeadStrip);
  NSym.GraphSymbol = &Sym;

  if (IsCanonical)
    setCanonicalSymbol(getSectionByIndex(NSym.Sect - 1), Sym);

  return Sym;
}

Error MachOLinkGraphBuilder::graphifySectionsWithCustomParsers() {
  // Graphify special sections.
  for (auto &KV : IndexToSection) {
    auto &NSec = KV.second;

    // Skip non-graph sections.
    if (!NSec.GraphSection)
      continue;

    auto HI = CustomSectionParserFunctions.find(NSec.GraphSection->getName());
    if (HI != CustomSectionParserFunctions.end()) {
      auto &Parse = HI->second;
      if (auto Err = Parse(NSec))
        return Err;
    }
  }

  return Error::success();
}

Error MachOLinkGraphBuilder::graphifyCStringSection(
    NormalizedSection &NSec, std::vector<NormalizedSymbol *> NSyms) {
  assert(NSec.GraphSection && "C string literal section missing graph section");
  assert(NSec.Data && "C string literal section has no data");

  LLVM_DEBUG({
    dbgs() << "  Graphifying C-string literal section "
           << NSec.GraphSection->getName() << "\n";
  });

  if (NSec.Data[NSec.Size - 1] != '\0')
    return make_error<JITLinkError>("C string literal section " +
                                    NSec.GraphSection->getName() +
                                    " does not end with null terminator");

  /// Sort into reverse order to use as a stack.
  llvm::sort(NSyms,
             [](const NormalizedSymbol *LHS, const NormalizedSymbol *RHS) {
               if (LHS->Value != RHS->Value)
                 return LHS->Value > RHS->Value;
               if (LHS->L != RHS->L)
                 return LHS->L > RHS->L;
               if (LHS->S != RHS->S)
                 return LHS->S > RHS->S;
               if (RHS->Name) {
                 if (!LHS->Name)
                   return true;
                 return *LHS->Name > *RHS->Name;
               }
               return false;
             });

  bool SectionIsNoDeadStrip = NSec.Flags & MachO::S_ATTR_NO_DEAD_STRIP;
  bool SectionIsText = NSec.Flags & MachO::S_ATTR_PURE_INSTRUCTIONS;
  orc::ExecutorAddrDiff BlockStart = 0;

  // Scan section for null characters.
  for (size_t I = 0; I != NSec.Size; ++I) {
    if (NSec.Data[I] == '\0') {
      size_t BlockSize = I + 1 - BlockStart;
      // Create a block for this null terminated string.
      auto &B = G->createContentBlock(*NSec.GraphSection,
                                      {NSec.Data + BlockStart, BlockSize},
                                      NSec.Address + BlockStart, NSec.Alignment,
                                      BlockStart % NSec.Alignment);

      LLVM_DEBUG({
        dbgs() << "    Created block " << B.getRange()
               << ", align = " << B.getAlignment()
               << ", align-ofs = " << B.getAlignmentOffset() << " for \"";
        for (size_t J = 0; J != std::min(B.getSize(), size_t(16)); ++J)
          switch (B.getContent()[J]) {
          case '\0': break;
          case '\n': dbgs() << "\\n"; break;
          case '\t': dbgs() << "\\t"; break;
          default:   dbgs() << B.getContent()[J]; break;
          }
        if (B.getSize() > 16)
          dbgs() << "...";
        dbgs() << "\"\n";
      });

      // If there's no symbol at the start of this block then create one.
      if (NSyms.empty() ||
          orc::ExecutorAddr(NSyms.back()->Value) != B.getAddress()) {
        auto &S = G->addAnonymousSymbol(B, 0, BlockSize, false, false);
        setCanonicalSymbol(NSec, S);
        LLVM_DEBUG({
          dbgs() << "      Adding symbol for c-string block " << B.getRange()
                 << ": <anonymous symbol> at offset 0\n";
        });
      }

      // Process any remaining symbols that point into this block.
      auto LastCanonicalAddr = B.getAddress() + BlockSize;
      while (!NSyms.empty() && orc::ExecutorAddr(NSyms.back()->Value) <
                                   B.getAddress() + BlockSize) {
        auto &NSym = *NSyms.back();
        size_t SymSize = (B.getAddress() + BlockSize) -
                         orc::ExecutorAddr(NSyms.back()->Value);
        bool SymLive =
            (NSym.Desc & MachO::N_NO_DEAD_STRIP) || SectionIsNoDeadStrip;

        bool IsCanonical = false;
        if (LastCanonicalAddr != orc::ExecutorAddr(NSym.Value)) {
          IsCanonical = true;
          LastCanonicalAddr = orc::ExecutorAddr(NSym.Value);
        }

        auto &Sym = createStandardGraphSymbol(NSym, B, SymSize, SectionIsText,
                                              SymLive, IsCanonical);
        (void)Sym;
        LLVM_DEBUG({
          dbgs() << "      Adding symbol for c-string block " << B.getRange()
                 << ": "
                 << (Sym.hasName() ? Sym.getName() : "<anonymous symbol>")
                 << " at offset " << formatv("{0:x}", Sym.getOffset()) << "\n";
        });

        NSyms.pop_back();
      }

      BlockStart += BlockSize;
    }
  }

  assert(llvm::all_of(NSec.GraphSection->blocks(),
                      [](Block *B) { return isCStringBlock(*B); }) &&
         "All blocks in section should hold single c-strings");

  return Error::success();
}

Error CompactUnwindSplitter::operator()(LinkGraph &G) {
  auto *CUSec = G.findSectionByName(CompactUnwindSectionName);
  if (!CUSec)
    return Error::success();

  if (!G.getTargetTriple().isOSBinFormatMachO())
    return make_error<JITLinkError>(
        "Error linking " + G.getName() +
        ": compact unwind splitting not supported on non-macho target " +
        G.getTargetTriple().str());

  unsigned CURecordSize = 0;
  unsigned PersonalityEdgeOffset = 0;
  unsigned LSDAEdgeOffset = 0;
  switch (G.getTargetTriple().getArch()) {
  case Triple::aarch64:
  case Triple::x86_64:
    // 64-bit compact-unwind record format:
    // Range start: 8 bytes.
    // Range size:  4 bytes.
    // CU encoding: 4 bytes.
    // Personality: 8 bytes.
    // LSDA:        8 bytes.
    CURecordSize = 32;
    PersonalityEdgeOffset = 16;
    LSDAEdgeOffset = 24;
    break;
  default:
    return make_error<JITLinkError>(
        "Error linking " + G.getName() +
        ": compact unwind splitting not supported on " +
        G.getTargetTriple().getArchName());
  }

  std::vector<Block *> OriginalBlocks(CUSec->blocks().begin(),
                                      CUSec->blocks().end());
  LLVM_DEBUG({
    dbgs() << "In " << G.getName() << " splitting compact unwind section "
           << CompactUnwindSectionName << " containing "
           << OriginalBlocks.size() << " initial blocks...\n";
  });

  while (!OriginalBlocks.empty()) {
    auto *B = OriginalBlocks.back();
    OriginalBlocks.pop_back();

    if (B->getSize() == 0) {
      LLVM_DEBUG({
        dbgs() << "  Skipping empty block at "
               << formatv("{0:x16}", B->getAddress()) << "\n";
      });
      continue;
    }

    LLVM_DEBUG({
      dbgs() << "  Splitting block at " << formatv("{0:x16}", B->getAddress())
             << " into " << (B->getSize() / CURecordSize)
             << " compact unwind record(s)\n";
    });

    if (B->getSize() % CURecordSize)
      return make_error<JITLinkError>(
          "Error splitting compact unwind record in " + G.getName() +
          ": block at " + formatv("{0:x}", B->getAddress()) + " has size " +
          formatv("{0:x}", B->getSize()) +
          " (not a multiple of CU record size of " +
          formatv("{0:x}", CURecordSize) + ")");

    unsigned NumBlocks = B->getSize() / CURecordSize;
    LinkGraph::SplitBlockCache C;

    for (unsigned I = 0; I != NumBlocks; ++I) {
      auto &CURec = G.splitBlock(*B, CURecordSize, &C);
      bool AddedKeepAlive = false;

      for (auto &E : CURec.edges()) {
        if (E.getOffset() == 0) {
          LLVM_DEBUG({
            dbgs() << "    Updating compact unwind record at "
                   << formatv("{0:x16}", CURec.getAddress()) << " to point to "
                   << (E.getTarget().hasName() ? E.getTarget().getName()
                                               : StringRef())
                   << " (at " << formatv("{0:x16}", E.getTarget().getAddress())
                   << ")\n";
          });

          if (E.getTarget().isExternal())
            return make_error<JITLinkError>(
                "Error adding keep-alive edge for compact unwind record at " +
                formatv("{0:x}", CURec.getAddress()) + ": target " +
                E.getTarget().getName() + " is an external symbol");
          auto &TgtBlock = E.getTarget().getBlock();
          auto &CURecSym =
              G.addAnonymousSymbol(CURec, 0, CURecordSize, false, false);
          TgtBlock.addEdge(Edge::KeepAlive, 0, CURecSym, 0);
          AddedKeepAlive = true;
        } else if (E.getOffset() != PersonalityEdgeOffset &&
                   E.getOffset() != LSDAEdgeOffset)
          return make_error<JITLinkError>("Unexpected edge at offset " +
                                          formatv("{0:x}", E.getOffset()) +
                                          " in compact unwind record at " +
                                          formatv("{0:x}", CURec.getAddress()));
      }

      if (!AddedKeepAlive)
        return make_error<JITLinkError>(
            "Error adding keep-alive edge for compact unwind record at " +
            formatv("{0:x}", CURec.getAddress()) +
            ": no outgoing target edge at offset 0");
    }
  }
  return Error::success();
}

} // end namespace jitlink
} // end namespace llvm
