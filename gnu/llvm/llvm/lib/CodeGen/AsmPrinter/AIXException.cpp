//===-- CodeGen/AsmPrinter/AIXException.cpp - AIX Exception Impl ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing AIX exception info into asm files.
//
//===----------------------------------------------------------------------===//

#include "DwarfException.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetLoweringObjectFileImpl.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCSectionXCOFF.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"

namespace llvm {

AIXException::AIXException(AsmPrinter *A) : EHStreamer(A) {}

void AIXException::emitExceptionInfoTable(const MCSymbol *LSDA,
                                          const MCSymbol *PerSym) {
  // Generate EH Info Table.
  // The EH Info Table, aka, 'compat unwind section' on AIX, have the following
  // format: struct eh_info_t {
  //   unsigned version;           /* EH info verion 0 */
  // #if defined(__64BIT__)
  //   char _pad[4];               /* padding */
  // #endif
  //   unsigned long lsda;         /* Pointer to LSDA */
  //   unsigned long personality;  /* Pointer to the personality routine */
  //   }

  auto *EHInfo =
      cast<MCSectionXCOFF>(Asm->getObjFileLowering().getCompactUnwindSection());
  if (Asm->TM.getFunctionSections()) {
    // If option -ffunction-sections is on, append the function name to the
    // name of EH Info Table csect so that each function has its own EH Info
    // Table csect. This helps the linker to garbage-collect EH info of unused
    // functions.
    SmallString<128> NameStr = EHInfo->getName();
    raw_svector_ostream(NameStr) << '.' << Asm->MF->getFunction().getName();
    EHInfo = Asm->OutContext.getXCOFFSection(NameStr, EHInfo->getKind(),
                                             EHInfo->getCsectProp());
  }
  Asm->OutStreamer->switchSection(EHInfo);
  MCSymbol *EHInfoLabel =
      TargetLoweringObjectFileXCOFF::getEHInfoTableSymbol(Asm->MF);
  Asm->OutStreamer->emitLabel(EHInfoLabel);

  // Version number.
  Asm->emitInt32(0);

  const DataLayout &DL = MMI->getModule()->getDataLayout();
  const unsigned PointerSize = DL.getPointerSize();

  // Add necessary paddings in 64 bit mode.
  Asm->OutStreamer->emitValueToAlignment(Align(PointerSize));

  // LSDA location.
  Asm->OutStreamer->emitValue(MCSymbolRefExpr::create(LSDA, Asm->OutContext),
                              PointerSize);

  // Personality routine.
  Asm->OutStreamer->emitValue(MCSymbolRefExpr::create(PerSym, Asm->OutContext),
                              PointerSize);
}

void AIXException::endFunction(const MachineFunction *MF) {
  // There is no easy way to access register information in `AIXException`
  // class. when ShouldEmitEHBlock is false and VRs are saved, A dumy eh info
  // table are emitted in PPCAIXAsmPrinter::emitFunctionBodyEnd.
  if (!TargetLoweringObjectFileXCOFF::ShouldEmitEHBlock(MF))
    return;

  const MCSymbol *LSDALabel = emitExceptionTable();

  const Function &F = MF->getFunction();
  assert(F.hasPersonalityFn() &&
         "Landingpads are presented, but no personality routine is found.");
  const auto *Per =
      cast<GlobalValue>(F.getPersonalityFn()->stripPointerCasts());
  const MCSymbol *PerSym = Asm->TM.getSymbol(Per);

  emitExceptionInfoTable(LSDALabel, PerSym);
}

} // End of namespace llvm
