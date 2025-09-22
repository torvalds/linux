//===- DWARF.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The --gdb-index option instructs the linker to emit a .gdb_index section.
// The section contains information to make gdb startup faster.
// The format of the section is described at
// https://sourceware.org/gdb/onlinedocs/gdb/Index-Section-Format.html.
//
//===----------------------------------------------------------------------===//

#include "DWARF.h"
#include "InputSection.h"
#include "Symbols.h"
#include "lld/Common/Memory.h"
#include "llvm/DebugInfo/DWARF/DWARFDebugPubTable.h"
#include "llvm/Object/ELFObjectFile.h"

using namespace llvm;
using namespace llvm::object;
using namespace lld;
using namespace lld::elf;

template <class ELFT> LLDDwarfObj<ELFT>::LLDDwarfObj(ObjFile<ELFT> *obj) {
  // Get the ELF sections to retrieve sh_flags. See the SHF_GROUP comment below.
  ArrayRef<typename ELFT::Shdr> objSections = obj->template getELFShdrs<ELFT>();
  assert(objSections.size() == obj->getSections().size());
  for (auto [i, sec] : llvm::enumerate(obj->getSections())) {
    if (!sec)
      continue;

    if (LLDDWARFSection *m =
            StringSwitch<LLDDWARFSection *>(sec->name)
                .Case(".debug_addr", &addrSection)
                .Case(".debug_gnu_pubnames", &gnuPubnamesSection)
                .Case(".debug_gnu_pubtypes", &gnuPubtypesSection)
                .Case(".debug_line", &lineSection)
                .Case(".debug_loclists", &loclistsSection)
                .Case(".debug_names", &namesSection)
                .Case(".debug_ranges", &rangesSection)
                .Case(".debug_rnglists", &rnglistsSection)
                .Case(".debug_str_offsets", &strOffsetsSection)
                .Default(nullptr)) {
      m->Data = toStringRef(sec->contentMaybeDecompress());
      m->sec = sec;
      continue;
    }

    if (sec->name == ".debug_abbrev")
      abbrevSection = toStringRef(sec->contentMaybeDecompress());
    else if (sec->name == ".debug_str")
      strSection = toStringRef(sec->contentMaybeDecompress());
    else if (sec->name == ".debug_line_str")
      lineStrSection = toStringRef(sec->contentMaybeDecompress());
    else if (sec->name == ".debug_info" &&
             !(objSections[i].sh_flags & ELF::SHF_GROUP)) {
      // In DWARF v5, -fdebug-types-section places type units in .debug_info
      // sections in COMDAT groups. They are not compile units and thus should
      // be ignored for .gdb_index/diagnostics purposes.
      //
      // We use a simple heuristic: the compile unit does not have the SHF_GROUP
      // flag. If we place compile units in COMDAT groups in the future, we may
      // need to perform a lightweight parsing. We drop the SHF_GROUP flag when
      // the InputSection was created, so we need to retrieve sh_flags from the
      // associated ELF section header.
      infoSection.Data = toStringRef(sec->contentMaybeDecompress());
      infoSection.sec = sec;
    }
  }
}

namespace {
template <class RelTy> struct LLDRelocationResolver {
  // In the ELF ABIs, S sepresents the value of the symbol in the relocation
  // entry. For Rela, the addend is stored as part of the relocation entry and
  // is provided by the `findAux` method.
  // In resolve() methods, the `type` and `offset` arguments would always be 0,
  // because we don't set an owning object for the `RelocationRef` instance that
  // we create in `findAux()`.
  static uint64_t resolve(uint64_t /*type*/, uint64_t /*offset*/, uint64_t s,
                          uint64_t /*locData*/, int64_t addend) {
    return s + addend;
  }
};

template <class ELFT> struct LLDRelocationResolver<Elf_Rel_Impl<ELFT, false>> {
  // For Rel, the addend is extracted from the relocated location and is
  // supplied by the caller.
  static uint64_t resolve(uint64_t /*type*/, uint64_t /*offset*/, uint64_t s,
                          uint64_t locData, int64_t /*addend*/) {
    return s + locData;
  }
};
} // namespace

// Find if there is a relocation at Pos in Sec.  The code is a bit
// more complicated than usual because we need to pass a section index
// to llvm since it has no idea about InputSection.
template <class ELFT>
template <class RelTy>
std::optional<RelocAddrEntry>
LLDDwarfObj<ELFT>::findAux(const InputSectionBase &sec, uint64_t pos,
                           ArrayRef<RelTy> rels) const {
  auto it =
      partition_point(rels, [=](const RelTy &a) { return a.r_offset < pos; });
  if (it == rels.end() || it->r_offset != pos)
    return std::nullopt;
  const RelTy &rel = *it;

  const ObjFile<ELFT> *file = sec.getFile<ELFT>();
  uint32_t symIndex = rel.getSymbol(config->isMips64EL);
  const typename ELFT::Sym &sym = file->template getELFSyms<ELFT>()[symIndex];
  uint32_t secIndex = file->getSectionIndex(sym);

  // An undefined symbol may be a symbol defined in a discarded section. We
  // shall still resolve it. This is important for --gdb-index: the end address
  // offset of an entry in .debug_ranges is relocated. If it is not resolved,
  // its zero value will terminate the decoding of .debug_ranges prematurely.
  Symbol &s = file->getRelocTargetSym(rel);
  uint64_t val = 0;
  if (auto *dr = dyn_cast<Defined>(&s))
    val = dr->value;

  DataRefImpl d;
  d.p = getAddend<ELFT>(rel);
  return RelocAddrEntry{secIndex, RelocationRef(d, nullptr),
                        val,      std::optional<object::RelocationRef>(),
                        0,        LLDRelocationResolver<RelTy>::resolve};
}

template <class ELFT>
std::optional<RelocAddrEntry>
LLDDwarfObj<ELFT>::find(const llvm::DWARFSection &s, uint64_t pos) const {
  auto &sec = static_cast<const LLDDWARFSection &>(s);
  const RelsOrRelas<ELFT> rels =
      sec.sec->template relsOrRelas<ELFT>(/*supportsCrel=*/false);
  if (rels.areRelocsRel())
    return findAux(*sec.sec, pos, rels.rels);
  return findAux(*sec.sec, pos, rels.relas);
}

template class elf::LLDDwarfObj<ELF32LE>;
template class elf::LLDDwarfObj<ELF32BE>;
template class elf::LLDDwarfObj<ELF64LE>;
template class elf::LLDDwarfObj<ELF64BE>;
