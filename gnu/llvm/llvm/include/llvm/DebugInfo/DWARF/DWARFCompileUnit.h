//===- DWARFCompileUnit.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFCOMPILEUNIT_H
#define LLVM_DEBUGINFO_DWARF_DWARFCOMPILEUNIT_H

#include "llvm/DebugInfo/DWARF/DWARFUnit.h"

namespace llvm {

class DWARFContext;
class DWARFDebugAbbrev;
class raw_ostream;
struct DIDumpOptions;
struct DWARFSection;

class DWARFCompileUnit : public DWARFUnit {
public:
  DWARFCompileUnit(DWARFContext &Context, const DWARFSection &Section,
                   const DWARFUnitHeader &Header, const DWARFDebugAbbrev *DA,
                   const DWARFSection *RS, const DWARFSection *LocSection,
                   StringRef SS, const DWARFSection &SOS,
                   const DWARFSection *AOS, const DWARFSection &LS, bool LE,
                   bool IsDWO, const DWARFUnitVector &UnitVector)
      : DWARFUnit(Context, Section, Header, DA, RS, LocSection, SS, SOS, AOS,
                  LS, LE, IsDWO, UnitVector) {}

  /// VTable anchor.
  ~DWARFCompileUnit() override;
  /// Dump this compile unit to \p OS.
  void dump(raw_ostream &OS, DIDumpOptions DumpOpts) override;
  /// Enable LLVM-style RTTI.
  static bool classof(const DWARFUnit *U) { return !U->isTypeUnit(); }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFCOMPILEUNIT_H
