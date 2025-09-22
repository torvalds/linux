//===- MCSectionXCOFF.h - XCOFF Machine Code Sections -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MCSectionXCOFF class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSECTIONXCOFF_H
#define LLVM_MC_MCSECTIONXCOFF_H

#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSymbolXCOFF.h"

namespace llvm {

// This class represents an XCOFF `Control Section`, more commonly referred to
// as a csect. A csect represents the smallest possible unit of data/code which
// will be relocated as a single block. A csect can either be:
// 1) Initialized: The Type will be XTY_SD, and the symbols inside the csect
//    will have a label definition representing their offset within the csect.
// 2) Uninitialized: The Type will be XTY_CM, it will contain a single symbol,
//    and may not contain label definitions.
// 3) An external reference providing a symbol table entry for a symbol
//    contained in another XCOFF object file. External reference csects are not
//    implemented yet.
class MCSectionXCOFF final : public MCSection {
  friend class MCContext;

  std::optional<XCOFF::CsectProperties> CsectProp;
  MCSymbolXCOFF *const QualName;
  StringRef SymbolTableName;
  std::optional<XCOFF::DwarfSectionSubtypeFlags> DwarfSubtypeFlags;
  bool MultiSymbolsAllowed;
  SectionKind Kind;
  static constexpr unsigned DefaultAlignVal = 4;
  static constexpr unsigned DefaultTextAlignVal = 32;

  // XTY_CM sections are virtual except for toc-data symbols.
  MCSectionXCOFF(StringRef Name, XCOFF::StorageMappingClass SMC,
                 XCOFF::SymbolType ST, SectionKind K, MCSymbolXCOFF *QualName,
                 MCSymbol *Begin, StringRef SymbolTableName,
                 bool MultiSymbolsAllowed)
      : MCSection(SV_XCOFF, Name, K.isText(),
                  /*IsVirtual=*/ST == XCOFF::XTY_CM && SMC != XCOFF::XMC_TD,
                  Begin),
        CsectProp(XCOFF::CsectProperties(SMC, ST)), QualName(QualName),
        SymbolTableName(SymbolTableName), DwarfSubtypeFlags(std::nullopt),
        MultiSymbolsAllowed(MultiSymbolsAllowed), Kind(K) {
    assert(
        (ST == XCOFF::XTY_SD || ST == XCOFF::XTY_CM || ST == XCOFF::XTY_ER) &&
        "Invalid or unhandled type for csect.");
    assert(QualName != nullptr && "QualName is needed.");
    if (SMC == XCOFF::XMC_UL)
      assert((ST == XCOFF::XTY_CM || ST == XCOFF::XTY_ER) &&
             "Invalid csect type for storage mapping class XCOFF::XMC_UL");

    QualName->setRepresentedCsect(this);
    QualName->setStorageClass(XCOFF::C_HIDEXT);
    if (ST != XCOFF::XTY_ER) {
      // For a csect for program code, set the alignment to 32 bytes by default.
      // For other csects, set the alignment to 4 bytes by default.
      if (SMC == XCOFF::XMC_PR)
        setAlignment(Align(DefaultTextAlignVal));
      else
        setAlignment(Align(DefaultAlignVal));
    }
  }

  // DWARF sections are never virtual.
  MCSectionXCOFF(StringRef Name, SectionKind K, MCSymbolXCOFF *QualName,
                 XCOFF::DwarfSectionSubtypeFlags DwarfSubtypeFlags,
                 MCSymbol *Begin, StringRef SymbolTableName,
                 bool MultiSymbolsAllowed)
      : MCSection(SV_XCOFF, Name, K.isText(), /*IsVirtual=*/false, Begin),
        QualName(QualName), SymbolTableName(SymbolTableName),
        DwarfSubtypeFlags(DwarfSubtypeFlags),
        MultiSymbolsAllowed(MultiSymbolsAllowed), Kind(K) {
    assert(QualName != nullptr && "QualName is needed.");

    // FIXME: use a more meaningful name for non csect sections.
    QualName->setRepresentedCsect(this);

    // Use default text alignment as the alignment for DWARF sections.
    setAlignment(Align(DefaultTextAlignVal));
  }

  void printCsectDirective(raw_ostream &OS) const;

public:
  ~MCSectionXCOFF();

  static bool classof(const MCSection *S) {
    return S->getVariant() == SV_XCOFF;
  }

  XCOFF::StorageMappingClass getMappingClass() const {
    assert(isCsect() && "Only csect section has mapping class property!");
    return CsectProp->MappingClass;
  }
  XCOFF::StorageClass getStorageClass() const {
    return QualName->getStorageClass();
  }
  XCOFF::VisibilityType getVisibilityType() const {
    return QualName->getVisibilityType();
  }
  XCOFF::SymbolType getCSectType() const {
    assert(isCsect() && "Only csect section has symbol type property!");
    return CsectProp->Type;
  }
  MCSymbolXCOFF *getQualNameSymbol() const { return QualName; }

  void printSwitchToSection(const MCAsmInfo &MAI, const Triple &T,
                            raw_ostream &OS,
                            uint32_t Subsection) const override;
  bool useCodeAlign() const override;
  StringRef getSymbolTableName() const { return SymbolTableName; }
  void setSymbolTableName(StringRef STN) { SymbolTableName = STN; }
  bool isMultiSymbolsAllowed() const { return MultiSymbolsAllowed; }
  bool isCsect() const { return CsectProp.has_value(); }
  bool isDwarfSect() const { return DwarfSubtypeFlags.has_value(); }
  std::optional<XCOFF::DwarfSectionSubtypeFlags> getDwarfSubtypeFlags() const {
    return DwarfSubtypeFlags;
  }
  std::optional<XCOFF::CsectProperties> getCsectProp() const {
    return CsectProp;
  }
  SectionKind getKind() const { return Kind; }
};

} // end namespace llvm

#endif
