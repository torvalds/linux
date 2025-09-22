//===- DWARFObject.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===-----------------------------------------------------------------------===/

#ifndef LLVM_DEBUGINFO_DWARF_DWARFOBJECT_H
#define LLVM_DEBUGINFO_DWARF_DWARFOBJECT_H

#include "llvm/DebugInfo/DWARF/DWARFRelocMap.h"
#include "llvm/DebugInfo/DWARF/DWARFSection.h"
#include "llvm/Object/ObjectFile.h"
#include <optional>

namespace llvm {
// This is responsible for low level access to the object file. It
// knows how to find the required sections and compute relocated
// values.
// The default implementations of the get<Section> methods return dummy values.
// This is to allow clients that only need some of those to implement just the
// ones they need. We can't use unreachable for as many cases because the parser
// implementation is eager and will call some of these methods even if the
// result is not used.
class DWARFObject {
  DWARFSection Dummy;

public:
  virtual ~DWARFObject() = default;
  virtual StringRef getFileName() const { llvm_unreachable("unimplemented"); }
  virtual const object::ObjectFile *getFile() const { return nullptr; }
  virtual ArrayRef<SectionName> getSectionNames() const { return {}; }
  virtual bool isLittleEndian() const = 0;
  virtual uint8_t getAddressSize() const { llvm_unreachable("unimplemented"); }
  virtual void
  forEachInfoSections(function_ref<void(const DWARFSection &)> F) const {}
  virtual void
  forEachTypesSections(function_ref<void(const DWARFSection &)> F) const {}
  virtual StringRef getAbbrevSection() const { return ""; }
  virtual const DWARFSection &getLocSection() const { return Dummy; }
  virtual const DWARFSection &getLoclistsSection() const { return Dummy; }
  virtual StringRef getArangesSection() const { return ""; }
  virtual const DWARFSection &getFrameSection() const { return Dummy; }
  virtual const DWARFSection &getEHFrameSection() const { return Dummy; }
  virtual const DWARFSection &getLineSection() const { return Dummy; }
  virtual StringRef getLineStrSection() const { return ""; }
  virtual StringRef getStrSection() const { return ""; }
  virtual const DWARFSection &getRangesSection() const { return Dummy; }
  virtual const DWARFSection &getRnglistsSection() const { return Dummy; }
  virtual const DWARFSection &getMacroSection() const { return Dummy; }
  virtual StringRef getMacroDWOSection() const { return ""; }
  virtual StringRef getMacinfoSection() const { return ""; }
  virtual StringRef getMacinfoDWOSection() const { return ""; }
  virtual const DWARFSection &getPubnamesSection() const { return Dummy; }
  virtual const DWARFSection &getPubtypesSection() const { return Dummy; }
  virtual const DWARFSection &getGnuPubnamesSection() const { return Dummy; }
  virtual const DWARFSection &getGnuPubtypesSection() const { return Dummy; }
  virtual const DWARFSection &getStrOffsetsSection() const { return Dummy; }
  virtual void
  forEachInfoDWOSections(function_ref<void(const DWARFSection &)> F) const {}
  virtual void
  forEachTypesDWOSections(function_ref<void(const DWARFSection &)> F) const {}
  virtual StringRef getAbbrevDWOSection() const { return ""; }
  virtual const DWARFSection &getLineDWOSection() const { return Dummy; }
  virtual const DWARFSection &getLocDWOSection() const { return Dummy; }
  virtual const DWARFSection &getLoclistsDWOSection() const { return Dummy; }
  virtual StringRef getStrDWOSection() const { return ""; }
  virtual const DWARFSection &getStrOffsetsDWOSection() const {
    return Dummy;
  }
  virtual const DWARFSection &getRangesDWOSection() const { return Dummy; }
  virtual const DWARFSection &getRnglistsDWOSection() const { return Dummy; }
  virtual const DWARFSection &getAddrSection() const { return Dummy; }
  virtual const DWARFSection &getAppleNamesSection() const { return Dummy; }
  virtual const DWARFSection &getAppleTypesSection() const { return Dummy; }
  virtual const DWARFSection &getAppleNamespacesSection() const {
    return Dummy;
  }
  virtual const DWARFSection &getNamesSection() const { return Dummy; }
  virtual const DWARFSection &getAppleObjCSection() const { return Dummy; }
  virtual StringRef getCUIndexSection() const { return ""; }
  virtual StringRef getGdbIndexSection() const { return ""; }
  virtual StringRef getTUIndexSection() const { return ""; }
  virtual std::optional<RelocAddrEntry> find(const DWARFSection &Sec,
                                             uint64_t Pos) const = 0;
};

} // namespace llvm
#endif
