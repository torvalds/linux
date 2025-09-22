//===-- CodeGen/AsmPrinter/ARMException.cpp - ARM EHABI Exception Impl ----===//
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
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/IR/Function.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCStreamer.h"
using namespace llvm;

ARMException::ARMException(AsmPrinter *A) : EHStreamer(A) {}

ARMException::~ARMException() = default;

ARMTargetStreamer &ARMException::getTargetStreamer() {
  MCTargetStreamer &TS = *Asm->OutStreamer->getTargetStreamer();
  return static_cast<ARMTargetStreamer &>(TS);
}

void ARMException::beginFunction(const MachineFunction *MF) {
  if (Asm->MAI->getExceptionHandlingType() == ExceptionHandling::ARM)
    getTargetStreamer().emitFnStart();
  // See if we need call frame info.
  AsmPrinter::CFISection CFISecType = Asm->getFunctionCFISectionType(*MF);
  assert(CFISecType != AsmPrinter::CFISection::EH &&
         "non-EH CFI not yet supported in prologue with EHABI lowering");

  if (CFISecType == AsmPrinter::CFISection::Debug) {
    if (!hasEmittedCFISections) {
      if (Asm->getModuleCFISectionType() == AsmPrinter::CFISection::Debug)
        Asm->OutStreamer->emitCFISections(false, true);
      hasEmittedCFISections = true;
    }

    shouldEmitCFI = true;
    Asm->OutStreamer->emitCFIStartProc(false);
  }
}

void ARMException::markFunctionEnd() {
  if (shouldEmitCFI)
    Asm->OutStreamer->emitCFIEndProc();
}

/// endFunction - Gather and emit post-function exception information.
///
void ARMException::endFunction(const MachineFunction *MF) {
  ARMTargetStreamer &ATS = getTargetStreamer();
  const Function &F = MF->getFunction();
  const Function *Per = nullptr;
  if (F.hasPersonalityFn())
    Per = dyn_cast<Function>(F.getPersonalityFn()->stripPointerCasts());
  bool forceEmitPersonality =
    F.hasPersonalityFn() && !isNoOpWithoutInvoke(classifyEHPersonality(Per)) &&
    F.needsUnwindTableEntry();
  bool shouldEmitPersonality = forceEmitPersonality ||
    !MF->getLandingPads().empty();
  if (!Asm->MF->getFunction().needsUnwindTableEntry() &&
      !shouldEmitPersonality)
    ATS.emitCantUnwind();
  else if (shouldEmitPersonality) {
    // Emit references to personality.
    if (Per) {
      MCSymbol *PerSym = Asm->getSymbol(Per);
      ATS.emitPersonality(PerSym);
    }

    // Emit .handlerdata directive.
    ATS.emitHandlerData();

    // Emit actual exception table
    emitExceptionTable();
  }

  if (Asm->MAI->getExceptionHandlingType() == ExceptionHandling::ARM)
    ATS.emitFnEnd();
}

void ARMException::emitTypeInfos(unsigned TTypeEncoding,
                                 MCSymbol *TTBaseLabel) {
  const MachineFunction *MF = Asm->MF;
  const std::vector<const GlobalValue *> &TypeInfos = MF->getTypeInfos();
  const std::vector<unsigned> &FilterIds = MF->getFilterIds();

  bool VerboseAsm = Asm->OutStreamer->isVerboseAsm();

  int Entry = 0;
  // Emit the Catch TypeInfos.
  if (VerboseAsm && !TypeInfos.empty()) {
    Asm->OutStreamer->AddComment(">> Catch TypeInfos <<");
    Asm->OutStreamer->addBlankLine();
    Entry = TypeInfos.size();
  }

  for (const GlobalValue *GV : reverse(TypeInfos)) {
    if (VerboseAsm)
      Asm->OutStreamer->AddComment("TypeInfo " + Twine(Entry--));
    Asm->emitTTypeReference(GV, TTypeEncoding);
  }

  Asm->OutStreamer->emitLabel(TTBaseLabel);

  // Emit the Exception Specifications.
  if (VerboseAsm && !FilterIds.empty()) {
    Asm->OutStreamer->AddComment(">> Filter TypeInfos <<");
    Asm->OutStreamer->addBlankLine();
    Entry = 0;
  }
  for (std::vector<unsigned>::const_iterator
         I = FilterIds.begin(), E = FilterIds.end(); I < E; ++I) {
    unsigned TypeID = *I;
    if (VerboseAsm) {
      --Entry;
      if (TypeID != 0)
        Asm->OutStreamer->AddComment("FilterInfo " + Twine(Entry));
    }

    Asm->emitTTypeReference((TypeID == 0 ? nullptr : TypeInfos[TypeID - 1]),
                            TTypeEncoding);
  }
}
