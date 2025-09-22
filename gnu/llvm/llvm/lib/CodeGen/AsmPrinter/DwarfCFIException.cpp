//===-- CodeGen/AsmPrinter/DwarfException.cpp - Dwarf Exception Impl ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing DWARF exception info into asm files.
//
//===----------------------------------------------------------------------===//

#include "DwarfException.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
using namespace llvm;

DwarfCFIException::DwarfCFIException(AsmPrinter *A) : EHStreamer(A) {}

DwarfCFIException::~DwarfCFIException() = default;

void DwarfCFIException::addPersonality(const GlobalValue *Personality) {
  if (!llvm::is_contained(Personalities, Personality))
    Personalities.push_back(Personality);
}

/// endModule - Emit all exception information that should come after the
/// content.
void DwarfCFIException::endModule() {
  // SjLj uses this pass and it doesn't need this info.
  if (!Asm->MAI->usesCFIForEH())
    return;

  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();

  unsigned PerEncoding = TLOF.getPersonalityEncoding();

  if ((PerEncoding & 0x80) != dwarf::DW_EH_PE_indirect)
    return;

  // Emit indirect reference table for all used personality functions
  for (const GlobalValue *Personality : Personalities) {
    MCSymbol *Sym = Asm->getSymbol(Personality);
    TLOF.emitPersonalityValue(*Asm->OutStreamer, Asm->getDataLayout(), Sym);
  }
  Personalities.clear();
}

void DwarfCFIException::beginFunction(const MachineFunction *MF) {
  shouldEmitPersonality = shouldEmitLSDA = false;
  const Function &F = MF->getFunction();

  // If any landing pads survive, we need an EH table.
  bool hasLandingPads = !MF->getLandingPads().empty();

  // See if we need frame move info.
  bool shouldEmitMoves =
      Asm->getFunctionCFISectionType(*MF) != AsmPrinter::CFISection::None;

  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();
  unsigned PerEncoding = TLOF.getPersonalityEncoding();
  const GlobalValue *Per = nullptr;
  if (F.hasPersonalityFn())
    Per = dyn_cast<GlobalValue>(F.getPersonalityFn()->stripPointerCasts());

  // Emit a personality function even when there are no landing pads
  forceEmitPersonality =
      // ...if a personality function is explicitly specified
      F.hasPersonalityFn() &&
      // ... and it's not known to be a noop in the absence of invokes
      !isNoOpWithoutInvoke(classifyEHPersonality(Per)) &&
      // ... and we're not explicitly asked not to emit it
      F.needsUnwindTableEntry();

  shouldEmitPersonality =
      (forceEmitPersonality ||
       (hasLandingPads && PerEncoding != dwarf::DW_EH_PE_omit)) &&
      Per;

  unsigned LSDAEncoding = TLOF.getLSDAEncoding();
  shouldEmitLSDA = shouldEmitPersonality &&
    LSDAEncoding != dwarf::DW_EH_PE_omit;

  const MCAsmInfo &MAI = *MF->getContext().getAsmInfo();
  if (MAI.getExceptionHandlingType() != ExceptionHandling::None)
    shouldEmitCFI =
        MAI.usesCFIForEH() && (shouldEmitPersonality || shouldEmitMoves);
  else
    shouldEmitCFI = Asm->usesCFIWithoutEH() && shouldEmitMoves;
}

void DwarfCFIException::beginBasicBlockSection(const MachineBasicBlock &MBB) {
  if (!shouldEmitCFI)
    return;

  if (!hasEmittedCFISections) {
    AsmPrinter::CFISection CFISecType = Asm->getModuleCFISectionType();
    // If we don't say anything it implies `.cfi_sections .eh_frame`, so we
    // chose not to be verbose in that case. And with `ForceDwarfFrameSection`,
    // we should always emit .debug_frame.
    if (CFISecType == AsmPrinter::CFISection::Debug ||
        Asm->TM.Options.ForceDwarfFrameSection)
      Asm->OutStreamer->emitCFISections(
          CFISecType == AsmPrinter::CFISection::EH, true);
    hasEmittedCFISections = true;
  }

  Asm->OutStreamer->emitCFIStartProc(/*IsSimple=*/false);

  // Indicate personality routine, if any.
  if (!shouldEmitPersonality)
    return;

  auto &F = MBB.getParent()->getFunction();
  auto *P = dyn_cast<GlobalValue>(F.getPersonalityFn()->stripPointerCasts());
  assert(P && "Expected personality function");
  // Record the personality function.
  addPersonality(P);

  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();
  unsigned PerEncoding = TLOF.getPersonalityEncoding();
  const MCSymbol *Sym = TLOF.getCFIPersonalitySymbol(P, Asm->TM, MMI);
  Asm->OutStreamer->emitCFIPersonality(Sym, PerEncoding);

  // Provide LSDA information.
  if (shouldEmitLSDA)
    Asm->OutStreamer->emitCFILsda(Asm->getMBBExceptionSym(MBB),
                                  TLOF.getLSDAEncoding());
}

void DwarfCFIException::endBasicBlockSection(const MachineBasicBlock &MBB) {
  if (shouldEmitCFI)
    Asm->OutStreamer->emitCFIEndProc();
}

/// endFunction - Gather and emit post-function exception information.
///
void DwarfCFIException::endFunction(const MachineFunction *MF) {
  if (!shouldEmitPersonality)
    return;

  emitExceptionTable();
}
