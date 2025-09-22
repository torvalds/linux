//===-- RISCVTargetObjectFile.cpp - RISC-V Object Info --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RISCVTargetObjectFile.h"
#include "MCTargetDesc/RISCVMCObjectFileInfo.h"
#include "RISCVTargetMachine.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCValue.h"

using namespace llvm;

unsigned RISCVELFTargetObjectFile::getTextSectionAlignment() const {
  return RISCVMCObjectFileInfo::getTextSectionAlignment(
      *getContext().getSubtargetInfo());
}

void RISCVELFTargetObjectFile::Initialize(MCContext &Ctx,
                                          const TargetMachine &TM) {
  TargetLoweringObjectFileELF::Initialize(Ctx, TM);

  PLTRelativeVariantKind = MCSymbolRefExpr::VK_PLT;
  SupportIndirectSymViaGOTPCRel = true;

  SmallDataSection = getContext().getELFSection(
      ".sdata", ELF::SHT_PROGBITS, ELF::SHF_WRITE | ELF::SHF_ALLOC);
  SmallBSSSection = getContext().getELFSection(".sbss", ELF::SHT_NOBITS,
                                               ELF::SHF_WRITE | ELF::SHF_ALLOC);
  SmallRODataSection =
      getContext().getELFSection(".srodata", ELF::SHT_PROGBITS, ELF::SHF_ALLOC);
  SmallROData4Section = getContext().getELFSection(
      ".srodata.cst4", ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_MERGE, 4);
  SmallROData8Section = getContext().getELFSection(
      ".srodata.cst8", ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_MERGE, 8);
  SmallROData16Section = getContext().getELFSection(
      ".srodata.cst16", ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_MERGE, 16);
  SmallROData32Section = getContext().getELFSection(
      ".srodata.cst32", ELF::SHT_PROGBITS, ELF::SHF_ALLOC | ELF::SHF_MERGE, 32);
}

const MCExpr *RISCVELFTargetObjectFile::getIndirectSymViaGOTPCRel(
    const GlobalValue *GV, const MCSymbol *Sym, const MCValue &MV,
    int64_t Offset, MachineModuleInfo *MMI, MCStreamer &Streamer) const {
  int64_t FinalOffset = Offset + MV.getConstant();
  const MCExpr *Res =
      MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_GOTPCREL, getContext());
  const MCExpr *Off = MCConstantExpr::create(FinalOffset, getContext());
  return MCBinaryExpr::createAdd(Res, Off, getContext());
}

// A address must be loaded from a small section if its size is less than the
// small section size threshold. Data in this section could be addressed by
// using gp_rel operator.
bool RISCVELFTargetObjectFile::isInSmallSection(uint64_t Size) const {
  // gcc has traditionally not treated zero-sized objects as small data, so this
  // is effectively part of the ABI.
  return Size > 0 && Size <= SSThreshold;
}

// Return true if this global address should be placed into small data/bss
// section.
bool RISCVELFTargetObjectFile::isGlobalInSmallSection(
    const GlobalObject *GO, const TargetMachine &TM) const {
  // Only global variables, not functions.
  const GlobalVariable *GVA = dyn_cast<GlobalVariable>(GO);
  if (!GVA)
    return false;

  // If the variable has an explicit section, it is placed in that section.
  if (GVA->hasSection()) {
    StringRef Section = GVA->getSection();

    // Explicitly placing any variable in the small data section overrides
    // the global -G value.
    if (Section == ".sdata" || Section == ".sbss")
      return true;

    // Otherwise reject putting the variable to small section if it has an
    // explicit section name.
    return false;
  }

  if (((GVA->hasExternalLinkage() && GVA->isDeclaration()) ||
       GVA->hasCommonLinkage()))
    return false;

  Type *Ty = GVA->getValueType();
  // It is possible that the type of the global is unsized, i.e. a declaration
  // of a extern struct. In this case don't presume it is in the small data
  // section. This happens e.g. when building the FreeBSD kernel.
  if (!Ty->isSized())
    return false;

  return isInSmallSection(
      GVA->getDataLayout().getTypeAllocSize(Ty));
}

MCSection *RISCVELFTargetObjectFile::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind Kind, const TargetMachine &TM) const {
  // Handle Small Section classification here.
  if (Kind.isBSS() && isGlobalInSmallSection(GO, TM))
    return SmallBSSSection;
  if (Kind.isData() && isGlobalInSmallSection(GO, TM))
    return SmallDataSection;

  // Otherwise, we work the same as ELF.
  return TargetLoweringObjectFileELF::SelectSectionForGlobal(GO, Kind, TM);
}

void RISCVELFTargetObjectFile::getModuleMetadata(Module &M) {
  TargetLoweringObjectFileELF::getModuleMetadata(M);
  SmallVector<Module::ModuleFlagEntry, 8> ModuleFlags;
  M.getModuleFlagsMetadata(ModuleFlags);

  for (const auto &MFE : ModuleFlags) {
    StringRef Key = MFE.Key->getString();
    if (Key == "SmallDataLimit") {
      SSThreshold = mdconst::extract<ConstantInt>(MFE.Val)->getZExtValue();
      break;
    }
  }
}

/// Return true if this constant should be placed into small data section.
bool RISCVELFTargetObjectFile::isConstantInSmallSection(
    const DataLayout &DL, const Constant *CN) const {
  return isInSmallSection(DL.getTypeAllocSize(CN->getType()));
}

MCSection *RISCVELFTargetObjectFile::getSectionForConstant(
    const DataLayout &DL, SectionKind Kind, const Constant *C,
    Align &Alignment) const {
  if (isConstantInSmallSection(DL, C)) {
    if (Kind.isMergeableConst4())
      return SmallROData4Section;
    if (Kind.isMergeableConst8())
      return SmallROData8Section;
    if (Kind.isMergeableConst16())
      return SmallROData16Section;
    if (Kind.isMergeableConst32())
      return SmallROData32Section;
    // LLVM only generate up to .rodata.cst32, and use .rodata section if more
    // than 32 bytes, so just use .srodata here.
    return SmallRODataSection;
  }

  // Otherwise, we work the same as ELF.
  return TargetLoweringObjectFileELF::getSectionForConstant(DL, Kind, C,
                                                            Alignment);
}
