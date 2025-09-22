//===- MCSectionCOFF.h - COFF Machine Code Sections -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MCSectionCOFF class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSECTIONCOFF_H
#define LLVM_MC_MCSECTIONCOFF_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/SectionKind.h"
#include <cassert>

namespace llvm {

class MCSymbol;

/// This represents a section on Windows
class MCSectionCOFF final : public MCSection {
  // FIXME: The following fields should not be mutable, but are for now so the
  // asm parser can honor the .linkonce directive.

  /// This is the Characteristics field of a section, drawn from the enums
  /// below.
  mutable unsigned Characteristics;

  /// The unique IDs used with the .pdata and .xdata sections created internally
  /// by the assembler. This ID is used to ensure that for every .text section,
  /// there is exactly one .pdata and one .xdata section, which is required by
  /// the Microsoft incremental linker. This data is mutable because this ID is
  /// not notionally part of the section.
  mutable unsigned WinCFISectionID = ~0U;

  /// The COMDAT symbol of this section. Only valid if this is a COMDAT section.
  /// Two COMDAT sections are merged if they have the same COMDAT symbol.
  MCSymbol *COMDATSymbol;

  /// This is the Selection field for the section symbol, if it is a COMDAT
  /// section (Characteristics & IMAGE_SCN_LNK_COMDAT) != 0
  mutable int Selection;

private:
  friend class MCContext;
  // The storage of Name is owned by MCContext's COFFUniquingMap.
  MCSectionCOFF(StringRef Name, unsigned Characteristics,
                MCSymbol *COMDATSymbol, int Selection, MCSymbol *Begin)
      : MCSection(SV_COFF, Name, Characteristics & COFF::IMAGE_SCN_CNT_CODE,
                  Characteristics & COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA,
                  Begin),
        Characteristics(Characteristics), COMDATSymbol(COMDATSymbol),
        Selection(Selection) {
    assert((Characteristics & 0x00F00000) == 0 &&
           "alignment must not be set upon section creation");
  }

public:
  /// Decides whether a '.section' directive should be printed before the
  /// section name
  bool shouldOmitSectionDirective(StringRef Name, const MCAsmInfo &MAI) const;

  unsigned getCharacteristics() const { return Characteristics; }
  MCSymbol *getCOMDATSymbol() const { return COMDATSymbol; }
  int getSelection() const { return Selection; }

  void setSelection(int Selection) const;

  void printSwitchToSection(const MCAsmInfo &MAI, const Triple &T,
                            raw_ostream &OS,
                            uint32_t Subsection) const override;
  bool useCodeAlign() const override;
  StringRef getVirtualSectionKind() const override;

  unsigned getOrAssignWinCFISectionID(unsigned *NextID) const {
    if (WinCFISectionID == ~0U)
      WinCFISectionID = (*NextID)++;
    return WinCFISectionID;
  }

  static bool isImplicitlyDiscardable(StringRef Name) {
    return Name.starts_with(".debug");
  }

  static bool classof(const MCSection *S) { return S->getVariant() == SV_COFF; }
};

} // end namespace llvm

#endif // LLVM_MC_MCSECTIONCOFF_H
