//===- MCSectionELF.h - ELF Machine Code Sections ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MCSectionELF class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSECTIONELF_H
#define LLVM_MC_MCSECTIONELF_H

#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/MC/SectionKind.h"

namespace llvm {

/// This represents a section on linux, lots of unix variants and some bare
/// metal systems.
class MCSectionELF final : public MCSection {
  /// This is the sh_type field of a section, drawn from the enums below.
  unsigned Type;

  /// This is the sh_flags field of a section, drawn from the enums below.
  unsigned Flags;

  unsigned UniqueID;

  /// The size of each entry in this section. This size only makes sense for
  /// sections that contain fixed-sized entries. If a section does not contain
  /// fixed-sized entries 'EntrySize' will be 0.
  unsigned EntrySize;

  /// The section group signature symbol (if not null) and a bool indicating
  /// whether this is a GRP_COMDAT group.
  const PointerIntPair<const MCSymbolELF *, 1, bool> Group;

  /// Used by SHF_LINK_ORDER. If non-null, the sh_link field will be set to the
  /// section header index of the section where LinkedToSym is defined.
  const MCSymbol *LinkedToSym;

  /// Start/end offset in file, used by ELFWriter.
  uint64_t StartOffset;
  uint64_t EndOffset;

private:
  friend class MCContext;

  // The storage of Name is owned by MCContext's ELFUniquingMap.
  MCSectionELF(StringRef Name, unsigned type, unsigned flags,
               unsigned entrySize, const MCSymbolELF *group, bool IsComdat,
               unsigned UniqueID, MCSymbol *Begin,
               const MCSymbolELF *LinkedToSym)
      : MCSection(SV_ELF, Name, flags & ELF::SHF_EXECINSTR,
                  type == ELF::SHT_NOBITS, Begin),
        Type(type), Flags(flags), UniqueID(UniqueID), EntrySize(entrySize),
        Group(group, IsComdat), LinkedToSym(LinkedToSym) {
    if (Group.getPointer())
      Group.getPointer()->setIsSignature();
  }

  // TODO Delete after we stop supporting generation of GNU-style .zdebug_*
  // sections.
  void setSectionName(StringRef Name) { this->Name = Name; }

public:
  /// Decides whether a '.section' directive should be printed before the
  /// section name
  bool shouldOmitSectionDirective(StringRef Name, const MCAsmInfo &MAI) const;

  unsigned getType() const { return Type; }
  unsigned getFlags() const { return Flags; }
  unsigned getEntrySize() const { return EntrySize; }
  void setFlags(unsigned F) { Flags = F; }
  const MCSymbolELF *getGroup() const { return Group.getPointer(); }
  bool isComdat() const { return Group.getInt(); }

  void printSwitchToSection(const MCAsmInfo &MAI, const Triple &T,
                            raw_ostream &OS,
                            uint32_t Subsection) const override;
  bool useCodeAlign() const override;
  StringRef getVirtualSectionKind() const override;

  bool isUnique() const { return UniqueID != NonUniqueID; }
  unsigned getUniqueID() const { return UniqueID; }

  const MCSection *getLinkedToSection() const {
    return &LinkedToSym->getSection();
  }
  const MCSymbol *getLinkedToSymbol() const { return LinkedToSym; }

  void setOffsets(uint64_t Start, uint64_t End) {
    StartOffset = Start;
    EndOffset = End;
  }
  std::pair<uint64_t, uint64_t> getOffsets() const {
    return std::make_pair(StartOffset, EndOffset);
  }

  static bool classof(const MCSection *S) {
    return S->getVariant() == SV_ELF;
  }
};

} // end namespace llvm

#endif // LLVM_MC_MCSECTIONELF_H
