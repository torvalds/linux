//===- DWARF.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===-------------------------------------------------------------------===//

#ifndef LLD_MACHO_DWARF_H
#define LLD_MACHO_DWARF_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/DWARF/DWARFObject.h"

namespace lld::macho {

class ObjFile;

// Implements the interface between LLVM's DWARF-parsing utilities and LLD's
// InputSection structures.
class DwarfObject final : public llvm::DWARFObject {
public:
  bool isLittleEndian() const override { return true; }

  std::optional<llvm::RelocAddrEntry> find(const llvm::DWARFSection &sec,
                                           uint64_t pos) const override {
    // TODO: implement this
    return std::nullopt;
  }

  void forEachInfoSections(
      llvm::function_ref<void(const llvm::DWARFSection &)> f) const override {
    f(infoSection);
  }

  llvm::StringRef getAbbrevSection() const override { return abbrevSection; }
  llvm::StringRef getStrSection() const override { return strSection; }

  llvm::DWARFSection const &getLineSection() const override {
    return lineSection;
  }

  llvm::DWARFSection const &getStrOffsetsSection() const override {
    return strOffsSection;
  }

  // Returns an instance of DwarfObject if the given object file has the
  // relevant DWARF debug sections.
  static std::unique_ptr<DwarfObject> create(ObjFile *);

private:
  llvm::DWARFSection infoSection;
  llvm::DWARFSection lineSection;
  llvm::DWARFSection strOffsSection;
  llvm::StringRef abbrevSection;
  llvm::StringRef strSection;
};

} // namespace lld::macho

#endif
