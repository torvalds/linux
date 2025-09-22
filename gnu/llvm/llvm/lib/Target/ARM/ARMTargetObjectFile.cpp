//===-- llvm/Target/ARMTargetObjectFile.cpp - ARM Object Info Impl --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ARMTargetObjectFile.h"
#include "ARMSubtarget.h"
#include "ARMTargetMachine.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/SectionKind.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>

using namespace llvm;
using namespace dwarf;

//===----------------------------------------------------------------------===//
//                               ELF Target
//===----------------------------------------------------------------------===//

void ARMElfTargetObjectFile::Initialize(MCContext &Ctx,
                                        const TargetMachine &TM) {
  const ARMBaseTargetMachine &ARM_TM = static_cast<const ARMBaseTargetMachine &>(TM);
  bool isAAPCS_ABI = ARM_TM.TargetABI == ARMBaseTargetMachine::ARMABI::ARM_ABI_AAPCS;
  bool genExecuteOnly =
      ARM_TM.getMCSubtargetInfo()->hasFeature(ARM::FeatureExecuteOnly);

  TargetLoweringObjectFileELF::Initialize(Ctx, TM);
  InitializeELF(isAAPCS_ABI);

  if (isAAPCS_ABI) {
    LSDASection = nullptr;
  }

  // Make code section unreadable when in execute-only mode
  if (genExecuteOnly) {
    unsigned Type = ELF::SHT_PROGBITS;
    unsigned Flags =
        ELF::SHF_EXECINSTR | ELF::SHF_ALLOC | ELF::SHF_ARM_PURECODE;
    // Since we cannot modify flags for an existing section, we create a new
    // section with the right flags, and use 0 as the unique ID for
    // execute-only text
    TextSection =
        Ctx.getELFSection(".text", Type, Flags, 0, "", false, 0U, nullptr);
  }
}

MCRegister ARMElfTargetObjectFile::getStaticBase() const { return ARM::R9; }

const MCExpr *ARMElfTargetObjectFile::getIndirectSymViaGOTPCRel(
    const GlobalValue *GV, const MCSymbol *Sym, const MCValue &MV,
    int64_t Offset, MachineModuleInfo *MMI, MCStreamer &Streamer) const {
  int64_t FinalOffset = Offset + MV.getConstant();
  const MCExpr *Res = MCSymbolRefExpr::create(
      Sym, MCSymbolRefExpr::VK_ARM_GOT_PREL, getContext());
  const MCExpr *Off = MCConstantExpr::create(FinalOffset, getContext());
  return MCBinaryExpr::createAdd(Res, Off, getContext());
}

const MCExpr *ARMElfTargetObjectFile::
getIndirectSymViaRWPI(const MCSymbol *Sym) const {
  return MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_ARM_SBREL,
                                 getContext());
}

const MCExpr *ARMElfTargetObjectFile::getTTypeGlobalReference(
    const GlobalValue *GV, unsigned Encoding, const TargetMachine &TM,
    MachineModuleInfo *MMI, MCStreamer &Streamer) const {
  if (TM.getMCAsmInfo()->getExceptionHandlingType() != ExceptionHandling::ARM)
    return TargetLoweringObjectFileELF::getTTypeGlobalReference(
        GV, Encoding, TM, MMI, Streamer);

  assert(Encoding == DW_EH_PE_absptr && "Can handle absptr encoding only");

  return MCSymbolRefExpr::create(TM.getSymbol(GV),
                                 MCSymbolRefExpr::VK_ARM_TARGET2, getContext());
}

const MCExpr *ARMElfTargetObjectFile::
getDebugThreadLocalSymbol(const MCSymbol *Sym) const {
  return MCSymbolRefExpr::create(Sym, MCSymbolRefExpr::VK_ARM_TLSLDO,
                                 getContext());
}

static bool isExecuteOnlyFunction(const GlobalObject *GO, SectionKind SK,
                                  const TargetMachine &TM) {
  if (const Function *F = dyn_cast<Function>(GO))
    if (TM.getSubtarget<ARMSubtarget>(*F).genExecuteOnly() && SK.isText())
      return true;
  return false;
}

MCSection *ARMElfTargetObjectFile::getExplicitSectionGlobal(
    const GlobalObject *GO, SectionKind SK, const TargetMachine &TM) const {
  // Set execute-only access for the explicit section
  if (isExecuteOnlyFunction(GO, SK, TM))
    SK = SectionKind::getExecuteOnly();

  return TargetLoweringObjectFileELF::getExplicitSectionGlobal(GO, SK, TM);
}

MCSection *ARMElfTargetObjectFile::SelectSectionForGlobal(
    const GlobalObject *GO, SectionKind SK, const TargetMachine &TM) const {
  // Place the global in the execute-only text section
  if (isExecuteOnlyFunction(GO, SK, TM))
    SK = SectionKind::getExecuteOnly();

  return TargetLoweringObjectFileELF::SelectSectionForGlobal(GO, SK, TM);
}
