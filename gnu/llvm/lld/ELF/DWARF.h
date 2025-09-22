//===- DWARF.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===-------------------------------------------------------------------===//

#ifndef LLD_ELF_DWARF_H
#define LLD_ELF_DWARF_H

#include "InputFiles.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/ELF.h"
#include <optional>

namespace lld::elf {

class InputSection;

struct LLDDWARFSection final : public llvm::DWARFSection {
  InputSectionBase *sec = nullptr;
};

template <class ELFT> class LLDDwarfObj final : public llvm::DWARFObject {
public:
  explicit LLDDwarfObj(ObjFile<ELFT> *obj);

  void forEachInfoSections(
      llvm::function_ref<void(const llvm::DWARFSection &)> f) const override {
    f(infoSection);
  }

  InputSection *getInfoSection() const {
    return cast<InputSection>(infoSection.sec);
  }

  const llvm::DWARFSection &getAddrSection() const override {
    return addrSection;
  }
  const llvm::DWARFSection &getLineSection() const override {
    return lineSection;
  }
  const llvm::DWARFSection &getLoclistsSection() const override {
    return loclistsSection;
  }
  const llvm::DWARFSection &getRangesSection() const override {
    return rangesSection;
  }
  const llvm::DWARFSection &getRnglistsSection() const override {
    return rnglistsSection;
  }
  const llvm::DWARFSection &getStrOffsetsSection() const override {
    return strOffsetsSection;
  }

  const LLDDWARFSection &getGnuPubnamesSection() const override {
    return gnuPubnamesSection;
  }
  const LLDDWARFSection &getGnuPubtypesSection() const override {
    return gnuPubtypesSection;
  }
  const LLDDWARFSection &getNamesSection() const override {
    return namesSection;
  }

  StringRef getFileName() const override { return ""; }
  StringRef getAbbrevSection() const override { return abbrevSection; }
  StringRef getStrSection() const override { return strSection; }
  StringRef getLineStrSection() const override { return lineStrSection; }

  bool isLittleEndian() const override {
    return ELFT::Endianness == llvm::endianness::little;
  }

  std::optional<llvm::RelocAddrEntry> find(const llvm::DWARFSection &sec,
                                           uint64_t pos) const override;

private:
  template <class RelTy>
  std::optional<llvm::RelocAddrEntry> findAux(const InputSectionBase &sec,
                                              uint64_t pos,
                                              ArrayRef<RelTy> rels) const;

  LLDDWARFSection addrSection;
  LLDDWARFSection gnuPubnamesSection;
  LLDDWARFSection gnuPubtypesSection;
  LLDDWARFSection infoSection;
  LLDDWARFSection lineSection;
  LLDDWARFSection loclistsSection;
  LLDDWARFSection namesSection;
  LLDDWARFSection rangesSection;
  LLDDWARFSection rnglistsSection;
  LLDDWARFSection strOffsetsSection;
  StringRef abbrevSection;
  StringRef lineStrSection;
  StringRef strSection;
};

} // namespace lld::elf

#endif
