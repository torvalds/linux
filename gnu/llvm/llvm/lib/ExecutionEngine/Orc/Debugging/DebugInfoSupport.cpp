//===--- DebugInfoSupport.cpp -- Utils for debug info support ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utilities to preserve and parse debug info from LinkGraphs.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/Debugging/DebugInfoSupport.h"

#include "llvm/Support/SmallVectorMemoryBuffer.h"

#define DEBUG_TYPE "orc"

using namespace llvm;
using namespace llvm::orc;
using namespace llvm::jitlink;

namespace {
static DenseSet<StringRef> DWARFSectionNames = {
#define HANDLE_DWARF_SECTION(ENUM_NAME, ELF_NAME, CMDLINE_NAME, OPTION)        \
  StringRef(ELF_NAME),
#include "llvm/BinaryFormat/Dwarf.def"
#undef HANDLE_DWARF_SECTION
};

// We might be able to drop relocations to symbols that do end up
// being pruned by the linker, but for now we just preserve all
static void preserveDWARFSection(LinkGraph &G, Section &Sec) {
  DenseMap<Block *, Symbol *> Preserved;
  for (auto Sym : Sec.symbols()) {
    if (Sym->isLive())
      Preserved[&Sym->getBlock()] = Sym;
    else if (!Preserved.count(&Sym->getBlock()))
      Preserved[&Sym->getBlock()] = Sym;
  }
  for (auto Block : Sec.blocks()) {
    auto &PSym = Preserved[Block];
    if (!PSym)
      PSym = &G.addAnonymousSymbol(*Block, 0, 0, false, true);
    else if (!PSym->isLive())
      PSym->setLive(true);
  }
}

static SmallVector<char, 0> getSectionData(Section &Sec) {
  SmallVector<char, 0> SecData;
  SmallVector<Block *, 8> SecBlocks(Sec.blocks().begin(), Sec.blocks().end());
  std::sort(SecBlocks.begin(), SecBlocks.end(), [](Block *LHS, Block *RHS) {
    return LHS->getAddress() < RHS->getAddress();
  });
  // Convert back to what object file would have, one blob of section content
  // Assumes all zerofill
  // TODO handle alignment?
  // TODO handle alignment offset?
  for (auto *Block : SecBlocks) {
    if (Block->isZeroFill())
      SecData.resize(SecData.size() + Block->getSize(), 0);
    else
      SecData.append(Block->getContent().begin(), Block->getContent().end());
  }
  return SecData;
}

static void dumpDWARFContext(DWARFContext &DC) {
  auto options = llvm::DIDumpOptions();
  options.DumpType &= ~DIDT_UUID;
  options.DumpType &= ~(1 << DIDT_ID_DebugFrame);
  LLVM_DEBUG(DC.dump(dbgs(), options));
}

} // namespace

Error llvm::orc::preserveDebugSections(LinkGraph &G) {
  if (!G.getTargetTriple().isOSBinFormatELF()) {
    return make_error<StringError>(
        "preserveDebugSections only supports ELF LinkGraphs!",
        inconvertibleErrorCode());
  }
  for (auto &Sec : G.sections()) {
    if (DWARFSectionNames.count(Sec.getName())) {
      LLVM_DEBUG(dbgs() << "Preserving DWARF section " << Sec.getName()
                        << "\n");
      preserveDWARFSection(G, Sec);
    }
  }
  return Error::success();
}

Expected<std::pair<std::unique_ptr<DWARFContext>,
                   StringMap<std::unique_ptr<MemoryBuffer>>>>
llvm::orc::createDWARFContext(LinkGraph &G) {
  if (!G.getTargetTriple().isOSBinFormatELF()) {
    return make_error<StringError>(
        "createDWARFContext only supports ELF LinkGraphs!",
        inconvertibleErrorCode());
  }
  StringMap<std::unique_ptr<MemoryBuffer>> DWARFSectionData;
  for (auto &Sec : G.sections()) {
    if (DWARFSectionNames.count(Sec.getName())) {
      auto SecData = getSectionData(Sec);
      auto Name = Sec.getName();
      // DWARFContext expects the section name to not start with a dot
      Name.consume_front(".");
      LLVM_DEBUG(dbgs() << "Creating DWARFContext section " << Name
                        << " with size " << SecData.size() << "\n");
      DWARFSectionData[Name] =
          std::make_unique<SmallVectorMemoryBuffer>(std::move(SecData));
    }
  }
  auto Ctx =
      DWARFContext::create(DWARFSectionData, G.getPointerSize(),
                           G.getEndianness() == llvm::endianness::little);
  dumpDWARFContext(*Ctx);
  return std::make_pair(std::move(Ctx), std::move(DWARFSectionData));
}
