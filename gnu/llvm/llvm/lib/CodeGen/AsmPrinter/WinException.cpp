//===-- CodeGen/AsmPrinter/WinException.cpp - Dwarf Exception Impl ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains support for writing Win64 exception info into asm files.
//
//===----------------------------------------------------------------------===//

#include "WinException.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/CodeGen/WinEHFuncInfo.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/Target/TargetLoweringObjectFile.h"
#include "llvm/Target/TargetMachine.h"
using namespace llvm;

WinException::WinException(AsmPrinter *A) : EHStreamer(A) {
  // MSVC's EH tables are always composed of 32-bit words.  All known 64-bit
  // platforms use an imagerel32 relocation to refer to symbols.
  useImageRel32 = (A->getDataLayout().getPointerSizeInBits() == 64);
  isAArch64 = Asm->TM.getTargetTriple().isAArch64();
  isThumb = Asm->TM.getTargetTriple().isThumb();
}

WinException::~WinException() = default;

/// endModule - Emit all exception information that should come after the
/// content.
void WinException::endModule() {
  auto &OS = *Asm->OutStreamer;
  const Module *M = MMI->getModule();
  for (const Function &F : *M)
    if (F.hasFnAttribute("safeseh"))
      OS.emitCOFFSafeSEH(Asm->getSymbol(&F));

  if (M->getModuleFlag("ehcontguard") && !EHContTargets.empty()) {
    // Emit the symbol index of each ehcont target.
    OS.switchSection(Asm->OutContext.getObjectFileInfo()->getGEHContSection());
    for (const MCSymbol *S : EHContTargets) {
      OS.emitCOFFSymbolIndex(S);
    }
  }
}

void WinException::beginFunction(const MachineFunction *MF) {
  shouldEmitMoves = shouldEmitPersonality = shouldEmitLSDA = false;

  // If any landing pads survive, we need an EH table.
  bool hasLandingPads = !MF->getLandingPads().empty();
  bool hasEHFunclets = MF->hasEHFunclets();

  const Function &F = MF->getFunction();

  shouldEmitMoves = Asm->needsSEHMoves() && MF->hasWinCFI();

  const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();
  unsigned PerEncoding = TLOF.getPersonalityEncoding();

  EHPersonality Per = EHPersonality::Unknown;
  const Function *PerFn = nullptr;
  if (F.hasPersonalityFn()) {
    PerFn = dyn_cast<Function>(F.getPersonalityFn()->stripPointerCasts());
    Per = classifyEHPersonality(PerFn);
  }

  bool forceEmitPersonality = F.hasPersonalityFn() &&
                              !isNoOpWithoutInvoke(Per) &&
                              F.needsUnwindTableEntry();

  shouldEmitPersonality =
      forceEmitPersonality || ((hasLandingPads || hasEHFunclets) &&
                               PerEncoding != dwarf::DW_EH_PE_omit && PerFn);

  unsigned LSDAEncoding = TLOF.getLSDAEncoding();
  shouldEmitLSDA = shouldEmitPersonality &&
    LSDAEncoding != dwarf::DW_EH_PE_omit;

  // If we're not using CFI, we don't want the CFI or the personality, but we
  // might want EH tables if we had EH pads.
  if (!Asm->MAI->usesWindowsCFI()) {
    if (Per == EHPersonality::MSVC_X86SEH && !hasEHFunclets) {
      // If this is 32-bit SEH and we don't have any funclets (really invokes),
      // make sure we emit the parent offset label. Some unreferenced filter
      // functions may still refer to it.
      const WinEHFuncInfo &FuncInfo = *MF->getWinEHFuncInfo();
      StringRef FLinkageName =
          GlobalValue::dropLLVMManglingEscape(MF->getFunction().getName());
      emitEHRegistrationOffsetLabel(FuncInfo, FLinkageName);
    }
    shouldEmitLSDA = hasEHFunclets;
    shouldEmitPersonality = false;
    return;
  }

  beginFunclet(MF->front(), Asm->CurrentFnSym);
}

void WinException::markFunctionEnd() {
  if (isAArch64 && CurrentFuncletEntry &&
      (shouldEmitMoves || shouldEmitPersonality))
    Asm->OutStreamer->emitWinCFIFuncletOrFuncEnd();
}

/// endFunction - Gather and emit post-function exception information.
///
void WinException::endFunction(const MachineFunction *MF) {
  if (!shouldEmitPersonality && !shouldEmitMoves && !shouldEmitLSDA)
    return;

  const Function &F = MF->getFunction();
  EHPersonality Per = EHPersonality::Unknown;
  if (F.hasPersonalityFn())
    Per = classifyEHPersonality(F.getPersonalityFn()->stripPointerCasts());

  endFuncletImpl();

  // endFunclet will emit the necessary .xdata tables for table-based SEH.
  if (Per == EHPersonality::MSVC_TableSEH && MF->hasEHFunclets())
    return;

  if (shouldEmitPersonality || shouldEmitLSDA) {
    Asm->OutStreamer->pushSection();

    // Just switch sections to the right xdata section.
    MCSection *XData = Asm->OutStreamer->getAssociatedXDataSection(
        Asm->OutStreamer->getCurrentSectionOnly());
    Asm->OutStreamer->switchSection(XData);

    // Emit the tables appropriate to the personality function in use. If we
    // don't recognize the personality, assume it uses an Itanium-style LSDA.
    if (Per == EHPersonality::MSVC_TableSEH)
      emitCSpecificHandlerTable(MF);
    else if (Per == EHPersonality::MSVC_X86SEH)
      emitExceptHandlerTable(MF);
    else if (Per == EHPersonality::MSVC_CXX)
      emitCXXFrameHandler3Table(MF);
    else if (Per == EHPersonality::CoreCLR)
      emitCLRExceptionTable(MF);
    else
      emitExceptionTable();

    Asm->OutStreamer->popSection();
  }

  if (!MF->getCatchretTargets().empty()) {
    // Copy the function's catchret targets to a module-level list.
    EHContTargets.insert(EHContTargets.end(), MF->getCatchretTargets().begin(),
                         MF->getCatchretTargets().end());
  }
}

/// Retrieve the MCSymbol for a GlobalValue or MachineBasicBlock.
static MCSymbol *getMCSymbolForMBB(AsmPrinter *Asm,
                                   const MachineBasicBlock *MBB) {
  if (!MBB)
    return nullptr;

  assert(MBB->isEHFuncletEntry());

  // Give catches and cleanups a name based off of their parent function and
  // their funclet entry block's number.
  const MachineFunction *MF = MBB->getParent();
  const Function &F = MF->getFunction();
  StringRef FuncLinkageName = GlobalValue::dropLLVMManglingEscape(F.getName());
  MCContext &Ctx = MF->getContext();
  StringRef HandlerPrefix = MBB->isCleanupFuncletEntry() ? "dtor" : "catch";
  return Ctx.getOrCreateSymbol("?" + HandlerPrefix + "$" +
                               Twine(MBB->getNumber()) + "@?0?" +
                               FuncLinkageName + "@4HA");
}

void WinException::beginFunclet(const MachineBasicBlock &MBB,
                                MCSymbol *Sym) {
  CurrentFuncletEntry = &MBB;

  const Function &F = Asm->MF->getFunction();
  // If a symbol was not provided for the funclet, invent one.
  if (!Sym) {
    Sym = getMCSymbolForMBB(Asm, &MBB);

    // Describe our funclet symbol as a function with internal linkage.
    Asm->OutStreamer->beginCOFFSymbolDef(Sym);
    Asm->OutStreamer->emitCOFFSymbolStorageClass(COFF::IMAGE_SYM_CLASS_STATIC);
    Asm->OutStreamer->emitCOFFSymbolType(COFF::IMAGE_SYM_DTYPE_FUNCTION
                                         << COFF::SCT_COMPLEX_TYPE_SHIFT);
    Asm->OutStreamer->endCOFFSymbolDef();

    // We want our funclet's entry point to be aligned such that no nops will be
    // present after the label.
    Asm->emitAlignment(std::max(Asm->MF->getAlignment(), MBB.getAlignment()),
                       &F);

    // Now that we've emitted the alignment directive, point at our funclet.
    Asm->OutStreamer->emitLabel(Sym);
  }

  // Mark 'Sym' as starting our funclet.
  if (shouldEmitMoves || shouldEmitPersonality) {
    CurrentFuncletTextSection = Asm->OutStreamer->getCurrentSectionOnly();
    Asm->OutStreamer->emitWinCFIStartProc(Sym);
  }

  if (shouldEmitPersonality) {
    const TargetLoweringObjectFile &TLOF = Asm->getObjFileLowering();
    const Function *PerFn = nullptr;

    // Determine which personality routine we are using for this funclet.
    if (F.hasPersonalityFn())
      PerFn = dyn_cast<Function>(F.getPersonalityFn()->stripPointerCasts());
    const MCSymbol *PersHandlerSym =
        TLOF.getCFIPersonalitySymbol(PerFn, Asm->TM, MMI);

    // Do not emit a .seh_handler directives for cleanup funclets.
    // FIXME: This means cleanup funclets cannot handle exceptions. Given that
    // Clang doesn't produce EH constructs inside cleanup funclets and LLVM's
    // inliner doesn't allow inlining them, this isn't a major problem in
    // practice.
    if (!CurrentFuncletEntry->isCleanupFuncletEntry())
      Asm->OutStreamer->emitWinEHHandler(PersHandlerSym, true, true);
  }
}

void WinException::endFunclet() {
  if (isAArch64 && CurrentFuncletEntry &&
      (shouldEmitMoves || shouldEmitPersonality)) {
    Asm->OutStreamer->switchSection(CurrentFuncletTextSection);
    Asm->OutStreamer->emitWinCFIFuncletOrFuncEnd();
  }
  endFuncletImpl();
}

void WinException::endFuncletImpl() {
  // No funclet to process?  Great, we have nothing to do.
  if (!CurrentFuncletEntry)
    return;

  const MachineFunction *MF = Asm->MF;
  if (shouldEmitMoves || shouldEmitPersonality) {
    const Function &F = MF->getFunction();
    EHPersonality Per = EHPersonality::Unknown;
    if (F.hasPersonalityFn())
      Per = classifyEHPersonality(F.getPersonalityFn()->stripPointerCasts());

    if (Per == EHPersonality::MSVC_CXX && shouldEmitPersonality &&
        !CurrentFuncletEntry->isCleanupFuncletEntry()) {
      // Emit an UNWIND_INFO struct describing the prologue.
      Asm->OutStreamer->emitWinEHHandlerData();

      // If this is a C++ catch funclet (or the parent function),
      // emit a reference to the LSDA for the parent function.
      StringRef FuncLinkageName = GlobalValue::dropLLVMManglingEscape(F.getName());
      MCSymbol *FuncInfoXData = Asm->OutContext.getOrCreateSymbol(
          Twine("$cppxdata$", FuncLinkageName));
      Asm->OutStreamer->emitValue(create32bitRef(FuncInfoXData), 4);
    } else if (Per == EHPersonality::MSVC_TableSEH && MF->hasEHFunclets() &&
               !CurrentFuncletEntry->isEHFuncletEntry()) {
      // Emit an UNWIND_INFO struct describing the prologue.
      Asm->OutStreamer->emitWinEHHandlerData();

      // If this is the parent function in Win64 SEH, emit the LSDA immediately
      // following .seh_handlerdata.
      emitCSpecificHandlerTable(MF);
    } else if (shouldEmitPersonality || shouldEmitLSDA) {
      // Emit an UNWIND_INFO struct describing the prologue.
      Asm->OutStreamer->emitWinEHHandlerData();
      // In these cases, no further info is written to the .xdata section
      // right here, but is written by e.g. emitExceptionTable in endFunction()
      // above.
    } else {
      // No need to emit the EH handler data right here if nothing needs
      // writing to the .xdata section; it will be emitted for all
      // functions that need it in the end anyway.
    }

    // Switch back to the funclet start .text section now that we are done
    // writing to .xdata, and emit an .seh_endproc directive to mark the end of
    // the function.
    Asm->OutStreamer->switchSection(CurrentFuncletTextSection);
    Asm->OutStreamer->emitWinCFIEndProc();
  }

  // Let's make sure we don't try to end the same funclet twice.
  CurrentFuncletEntry = nullptr;
}

const MCExpr *WinException::create32bitRef(const MCSymbol *Value) {
  if (!Value)
    return MCConstantExpr::create(0, Asm->OutContext);
  return MCSymbolRefExpr::create(Value, useImageRel32
                                            ? MCSymbolRefExpr::VK_COFF_IMGREL32
                                            : MCSymbolRefExpr::VK_None,
                                 Asm->OutContext);
}

const MCExpr *WinException::create32bitRef(const GlobalValue *GV) {
  if (!GV)
    return MCConstantExpr::create(0, Asm->OutContext);
  return create32bitRef(Asm->getSymbol(GV));
}

const MCExpr *WinException::getLabel(const MCSymbol *Label) {
  return MCSymbolRefExpr::create(Label, MCSymbolRefExpr::VK_COFF_IMGREL32,
                                 Asm->OutContext);
}

const MCExpr *WinException::getLabelPlusOne(const MCSymbol *Label) {
  return MCBinaryExpr::createAdd(getLabel(Label),
                                 MCConstantExpr::create(1, Asm->OutContext),
                                 Asm->OutContext);
}

const MCExpr *WinException::getOffset(const MCSymbol *OffsetOf,
                                      const MCSymbol *OffsetFrom) {
  return MCBinaryExpr::createSub(
      MCSymbolRefExpr::create(OffsetOf, Asm->OutContext),
      MCSymbolRefExpr::create(OffsetFrom, Asm->OutContext), Asm->OutContext);
}

const MCExpr *WinException::getOffsetPlusOne(const MCSymbol *OffsetOf,
                                             const MCSymbol *OffsetFrom) {
  return MCBinaryExpr::createAdd(getOffset(OffsetOf, OffsetFrom),
                                 MCConstantExpr::create(1, Asm->OutContext),
                                 Asm->OutContext);
}

int WinException::getFrameIndexOffset(int FrameIndex,
                                      const WinEHFuncInfo &FuncInfo) {
  const TargetFrameLowering &TFI = *Asm->MF->getSubtarget().getFrameLowering();
  Register UnusedReg;
  if (Asm->MAI->usesWindowsCFI()) {
    StackOffset Offset =
        TFI.getFrameIndexReferencePreferSP(*Asm->MF, FrameIndex, UnusedReg,
                                           /*IgnoreSPUpdates*/ true);
    assert(UnusedReg ==
           Asm->MF->getSubtarget()
               .getTargetLowering()
               ->getStackPointerRegisterToSaveRestore());
    return Offset.getFixed();
  }

  // For 32-bit, offsets should be relative to the end of the EH registration
  // node. For 64-bit, it's relative to SP at the end of the prologue.
  assert(FuncInfo.EHRegNodeEndOffset != INT_MAX);
  StackOffset Offset = TFI.getFrameIndexReference(*Asm->MF, FrameIndex, UnusedReg);
  Offset += StackOffset::getFixed(FuncInfo.EHRegNodeEndOffset);
  assert(!Offset.getScalable() &&
         "Frame offsets with a scalable component are not supported");
  return Offset.getFixed();
}

namespace {

/// Top-level state used to represent unwind to caller
const int NullState = -1;

struct InvokeStateChange {
  /// EH Label immediately after the last invoke in the previous state, or
  /// nullptr if the previous state was the null state.
  const MCSymbol *PreviousEndLabel;

  /// EH label immediately before the first invoke in the new state, or nullptr
  /// if the new state is the null state.
  const MCSymbol *NewStartLabel;

  /// State of the invoke following NewStartLabel, or NullState to indicate
  /// the presence of calls which may unwind to caller.
  int NewState;
};

/// Iterator that reports all the invoke state changes in a range of machine
/// basic blocks.  Changes to the null state are reported whenever a call that
/// may unwind to caller is encountered.  The MBB range is expected to be an
/// entire function or funclet, and the start and end of the range are treated
/// as being in the NullState even if there's not an unwind-to-caller call
/// before the first invoke or after the last one (i.e., the first state change
/// reported is the first change to something other than NullState, and a
/// change back to NullState is always reported at the end of iteration).
class InvokeStateChangeIterator {
  InvokeStateChangeIterator(const WinEHFuncInfo &EHInfo,
                            MachineFunction::const_iterator MFI,
                            MachineFunction::const_iterator MFE,
                            MachineBasicBlock::const_iterator MBBI,
                            int BaseState)
      : EHInfo(EHInfo), MFI(MFI), MFE(MFE), MBBI(MBBI), BaseState(BaseState) {
    LastStateChange.PreviousEndLabel = nullptr;
    LastStateChange.NewStartLabel = nullptr;
    LastStateChange.NewState = BaseState;
    scan();
  }

public:
  static iterator_range<InvokeStateChangeIterator>
  range(const WinEHFuncInfo &EHInfo, MachineFunction::const_iterator Begin,
        MachineFunction::const_iterator End, int BaseState = NullState) {
    // Reject empty ranges to simplify bookkeeping by ensuring that we can get
    // the end of the last block.
    assert(Begin != End);
    auto BlockBegin = Begin->begin();
    auto BlockEnd = std::prev(End)->end();
    return make_range(
        InvokeStateChangeIterator(EHInfo, Begin, End, BlockBegin, BaseState),
        InvokeStateChangeIterator(EHInfo, End, End, BlockEnd, BaseState));
  }

  // Iterator methods.
  bool operator==(const InvokeStateChangeIterator &O) const {
    assert(BaseState == O.BaseState);
    // Must be visiting same block.
    if (MFI != O.MFI)
      return false;
    // Must be visiting same isntr.
    if (MBBI != O.MBBI)
      return false;
    // At end of block/instr iteration, we can still have two distinct states:
    // one to report the final EndLabel, and another indicating the end of the
    // state change iteration.  Check for CurrentEndLabel equality to
    // distinguish these.
    return CurrentEndLabel == O.CurrentEndLabel;
  }

  bool operator!=(const InvokeStateChangeIterator &O) const {
    return !operator==(O);
  }
  InvokeStateChange &operator*() { return LastStateChange; }
  InvokeStateChange *operator->() { return &LastStateChange; }
  InvokeStateChangeIterator &operator++() { return scan(); }

private:
  InvokeStateChangeIterator &scan();

  const WinEHFuncInfo &EHInfo;
  const MCSymbol *CurrentEndLabel = nullptr;
  MachineFunction::const_iterator MFI;
  MachineFunction::const_iterator MFE;
  MachineBasicBlock::const_iterator MBBI;
  InvokeStateChange LastStateChange;
  bool VisitingInvoke = false;
  int BaseState;
};

} // end anonymous namespace

InvokeStateChangeIterator &InvokeStateChangeIterator::scan() {
  bool IsNewBlock = false;
  for (; MFI != MFE; ++MFI, IsNewBlock = true) {
    if (IsNewBlock)
      MBBI = MFI->begin();
    for (auto MBBE = MFI->end(); MBBI != MBBE; ++MBBI) {
      const MachineInstr &MI = *MBBI;
      if (!VisitingInvoke && LastStateChange.NewState != BaseState &&
          MI.isCall() && !EHStreamer::callToNoUnwindFunction(&MI)) {
        // Indicate a change of state to the null state.  We don't have
        // start/end EH labels handy but the caller won't expect them for
        // null state regions.
        LastStateChange.PreviousEndLabel = CurrentEndLabel;
        LastStateChange.NewStartLabel = nullptr;
        LastStateChange.NewState = BaseState;
        CurrentEndLabel = nullptr;
        // Don't re-visit this instr on the next scan
        ++MBBI;
        return *this;
      }

      // All other state changes are at EH labels before/after invokes.
      if (!MI.isEHLabel())
        continue;
      MCSymbol *Label = MI.getOperand(0).getMCSymbol();
      if (Label == CurrentEndLabel) {
        VisitingInvoke = false;
        continue;
      }
      auto InvokeMapIter = EHInfo.LabelToStateMap.find(Label);
      // Ignore EH labels that aren't the ones inserted before an invoke
      if (InvokeMapIter == EHInfo.LabelToStateMap.end())
        continue;
      auto &StateAndEnd = InvokeMapIter->second;
      int NewState = StateAndEnd.first;
      // Keep track of the fact that we're between EH start/end labels so
      // we know not to treat the inoke we'll see as unwinding to caller.
      VisitingInvoke = true;
      if (NewState == LastStateChange.NewState) {
        // The state isn't actually changing here.  Record the new end and
        // keep going.
        CurrentEndLabel = StateAndEnd.second;
        continue;
      }
      // Found a state change to report
      LastStateChange.PreviousEndLabel = CurrentEndLabel;
      LastStateChange.NewStartLabel = Label;
      LastStateChange.NewState = NewState;
      // Start keeping track of the new current end
      CurrentEndLabel = StateAndEnd.second;
      // Don't re-visit this instr on the next scan
      ++MBBI;
      return *this;
    }
  }
  // Iteration hit the end of the block range.
  if (LastStateChange.NewState != BaseState) {
    // Report the end of the last new state
    LastStateChange.PreviousEndLabel = CurrentEndLabel;
    LastStateChange.NewStartLabel = nullptr;
    LastStateChange.NewState = BaseState;
    // Leave CurrentEndLabel non-null to distinguish this state from end.
    assert(CurrentEndLabel != nullptr);
    return *this;
  }
  // We've reported all state changes and hit the end state.
  CurrentEndLabel = nullptr;
  return *this;
}

/// Emit the language-specific data that __C_specific_handler expects.  This
/// handler lives in the x64 Microsoft C runtime and allows catching or cleaning
/// up after faults with __try, __except, and __finally.  The typeinfo values
/// are not really RTTI data, but pointers to filter functions that return an
/// integer (1, 0, or -1) indicating how to handle the exception. For __finally
/// blocks and other cleanups, the landing pad label is zero, and the filter
/// function is actually a cleanup handler with the same prototype.  A catch-all
/// entry is modeled with a null filter function field and a non-zero landing
/// pad label.
///
/// Possible filter function return values:
///   EXCEPTION_EXECUTE_HANDLER (1):
///     Jump to the landing pad label after cleanups.
///   EXCEPTION_CONTINUE_SEARCH (0):
///     Continue searching this table or continue unwinding.
///   EXCEPTION_CONTINUE_EXECUTION (-1):
///     Resume execution at the trapping PC.
///
/// Inferred table structure:
///   struct Table {
///     int NumEntries;
///     struct Entry {
///       imagerel32 LabelStart;       // Inclusive
///       imagerel32 LabelEnd;         // Exclusive
///       imagerel32 FilterOrFinally;  // One means catch-all.
///       imagerel32 LabelLPad;        // Zero means __finally.
///     } Entries[NumEntries];
///   };
void WinException::emitCSpecificHandlerTable(const MachineFunction *MF) {
  auto &OS = *Asm->OutStreamer;
  MCContext &Ctx = Asm->OutContext;
  const WinEHFuncInfo &FuncInfo = *MF->getWinEHFuncInfo();

  bool VerboseAsm = OS.isVerboseAsm();
  auto AddComment = [&](const Twine &Comment) {
    if (VerboseAsm)
      OS.AddComment(Comment);
  };

  if (!isAArch64) {
    // Emit a label assignment with the SEH frame offset so we can use it for
    // llvm.eh.recoverfp.
    StringRef FLinkageName =
        GlobalValue::dropLLVMManglingEscape(MF->getFunction().getName());
    MCSymbol *ParentFrameOffset =
        Ctx.getOrCreateParentFrameOffsetSymbol(FLinkageName);
    const MCExpr *MCOffset =
        MCConstantExpr::create(FuncInfo.SEHSetFrameOffset, Ctx);
    Asm->OutStreamer->emitAssignment(ParentFrameOffset, MCOffset);
  }

  // Use the assembler to compute the number of table entries through label
  // difference and division.
  MCSymbol *TableBegin =
      Ctx.createTempSymbol("lsda_begin", /*AlwaysAddSuffix=*/true);
  MCSymbol *TableEnd =
      Ctx.createTempSymbol("lsda_end", /*AlwaysAddSuffix=*/true);
  const MCExpr *LabelDiff = getOffset(TableEnd, TableBegin);
  const MCExpr *EntrySize = MCConstantExpr::create(16, Ctx);
  const MCExpr *EntryCount = MCBinaryExpr::createDiv(LabelDiff, EntrySize, Ctx);
  AddComment("Number of call sites");
  OS.emitValue(EntryCount, 4);

  OS.emitLabel(TableBegin);

  // Iterate over all the invoke try ranges. Unlike MSVC, LLVM currently only
  // models exceptions from invokes. LLVM also allows arbitrary reordering of
  // the code, so our tables end up looking a bit different. Rather than
  // trying to match MSVC's tables exactly, we emit a denormalized table.  For
  // each range of invokes in the same state, we emit table entries for all
  // the actions that would be taken in that state. This means our tables are
  // slightly bigger, which is OK.
  const MCSymbol *LastStartLabel = nullptr;
  int LastEHState = -1;
  // Break out before we enter into a finally funclet.
  // FIXME: We need to emit separate EH tables for cleanups.
  MachineFunction::const_iterator End = MF->end();
  MachineFunction::const_iterator Stop = std::next(MF->begin());
  while (Stop != End && !Stop->isEHFuncletEntry())
    ++Stop;
  for (const auto &StateChange :
       InvokeStateChangeIterator::range(FuncInfo, MF->begin(), Stop)) {
    // Emit all the actions for the state we just transitioned out of
    // if it was not the null state
    if (LastEHState != -1)
      emitSEHActionsForRange(FuncInfo, LastStartLabel,
                             StateChange.PreviousEndLabel, LastEHState);
    LastStartLabel = StateChange.NewStartLabel;
    LastEHState = StateChange.NewState;
  }

  OS.emitLabel(TableEnd);
}

void WinException::emitSEHActionsForRange(const WinEHFuncInfo &FuncInfo,
                                          const MCSymbol *BeginLabel,
                                          const MCSymbol *EndLabel, int State) {
  auto &OS = *Asm->OutStreamer;
  MCContext &Ctx = Asm->OutContext;
  bool VerboseAsm = OS.isVerboseAsm();
  auto AddComment = [&](const Twine &Comment) {
    if (VerboseAsm)
      OS.AddComment(Comment);
  };

  assert(BeginLabel && EndLabel);
  while (State != -1) {
    const SEHUnwindMapEntry &UME = FuncInfo.SEHUnwindMap[State];
    const MCExpr *FilterOrFinally;
    const MCExpr *ExceptOrNull;
    auto *Handler = cast<MachineBasicBlock *>(UME.Handler);
    if (UME.IsFinally) {
      FilterOrFinally = create32bitRef(getMCSymbolForMBB(Asm, Handler));
      ExceptOrNull = MCConstantExpr::create(0, Ctx);
    } else {
      // For an except, the filter can be 1 (catch-all) or a function
      // label.
      FilterOrFinally = UME.Filter ? create32bitRef(UME.Filter)
                                   : MCConstantExpr::create(1, Ctx);
      ExceptOrNull = create32bitRef(Handler->getSymbol());
    }

    AddComment("LabelStart");
    OS.emitValue(getLabel(BeginLabel), 4);
    AddComment("LabelEnd");
    OS.emitValue(getLabelPlusOne(EndLabel), 4);
    AddComment(UME.IsFinally ? "FinallyFunclet" : UME.Filter ? "FilterFunction"
                                                             : "CatchAll");
    OS.emitValue(FilterOrFinally, 4);
    AddComment(UME.IsFinally ? "Null" : "ExceptionHandler");
    OS.emitValue(ExceptOrNull, 4);

    assert(UME.ToState < State && "states should decrease");
    State = UME.ToState;
  }
}

void WinException::emitCXXFrameHandler3Table(const MachineFunction *MF) {
  const Function &F = MF->getFunction();
  auto &OS = *Asm->OutStreamer;
  const WinEHFuncInfo &FuncInfo = *MF->getWinEHFuncInfo();

  StringRef FuncLinkageName = GlobalValue::dropLLVMManglingEscape(F.getName());

  SmallVector<std::pair<const MCExpr *, int>, 4> IPToStateTable;
  MCSymbol *FuncInfoXData = nullptr;
  if (shouldEmitPersonality) {
    // If we're 64-bit, emit a pointer to the C++ EH data, and build a map from
    // IPs to state numbers.
    FuncInfoXData =
        Asm->OutContext.getOrCreateSymbol(Twine("$cppxdata$", FuncLinkageName));
    computeIP2StateTable(MF, FuncInfo, IPToStateTable);
  } else {
    FuncInfoXData = Asm->OutContext.getOrCreateLSDASymbol(FuncLinkageName);
  }

  int UnwindHelpOffset = 0;
  // TODO: The check for UnwindHelpFrameIdx against max() below (and the
  // second check further below) can be removed if MS C++ unwinding is
  // implemented for ARM, when test/CodeGen/ARM/Windows/wineh-basic.ll
  // passes without the check.
  if (Asm->MAI->usesWindowsCFI() &&
      FuncInfo.UnwindHelpFrameIdx != std::numeric_limits<int>::max())
    UnwindHelpOffset =
        getFrameIndexOffset(FuncInfo.UnwindHelpFrameIdx, FuncInfo);

  MCSymbol *UnwindMapXData = nullptr;
  MCSymbol *TryBlockMapXData = nullptr;
  MCSymbol *IPToStateXData = nullptr;
  if (!FuncInfo.CxxUnwindMap.empty())
    UnwindMapXData = Asm->OutContext.getOrCreateSymbol(
        Twine("$stateUnwindMap$", FuncLinkageName));
  if (!FuncInfo.TryBlockMap.empty())
    TryBlockMapXData =
        Asm->OutContext.getOrCreateSymbol(Twine("$tryMap$", FuncLinkageName));
  if (!IPToStateTable.empty())
    IPToStateXData =
        Asm->OutContext.getOrCreateSymbol(Twine("$ip2state$", FuncLinkageName));

  bool VerboseAsm = OS.isVerboseAsm();
  auto AddComment = [&](const Twine &Comment) {
    if (VerboseAsm)
      OS.AddComment(Comment);
  };

  // FuncInfo {
  //   uint32_t           MagicNumber
  //   int32_t            MaxState;
  //   UnwindMapEntry    *UnwindMap;
  //   uint32_t           NumTryBlocks;
  //   TryBlockMapEntry  *TryBlockMap;
  //   uint32_t           IPMapEntries; // always 0 for x86
  //   IPToStateMapEntry *IPToStateMap; // always 0 for x86
  //   uint32_t           UnwindHelp;   // non-x86 only
  //   ESTypeList        *ESTypeList;
  //   int32_t            EHFlags;
  // }
  // EHFlags & 1 -> Synchronous exceptions only, no async exceptions.
  // EHFlags & 2 -> ???
  // EHFlags & 4 -> The function is noexcept(true), unwinding can't continue.
  OS.emitValueToAlignment(Align(4));
  OS.emitLabel(FuncInfoXData);

  AddComment("MagicNumber");
  OS.emitInt32(0x19930522);

  AddComment("MaxState");
  OS.emitInt32(FuncInfo.CxxUnwindMap.size());

  AddComment("UnwindMap");
  OS.emitValue(create32bitRef(UnwindMapXData), 4);

  AddComment("NumTryBlocks");
  OS.emitInt32(FuncInfo.TryBlockMap.size());

  AddComment("TryBlockMap");
  OS.emitValue(create32bitRef(TryBlockMapXData), 4);

  AddComment("IPMapEntries");
  OS.emitInt32(IPToStateTable.size());

  AddComment("IPToStateXData");
  OS.emitValue(create32bitRef(IPToStateXData), 4);

  if (Asm->MAI->usesWindowsCFI() &&
      FuncInfo.UnwindHelpFrameIdx != std::numeric_limits<int>::max()) {
    AddComment("UnwindHelp");
    OS.emitInt32(UnwindHelpOffset);
  }

  AddComment("ESTypeList");
  OS.emitInt32(0);

  AddComment("EHFlags");
  if (MMI->getModule()->getModuleFlag("eh-asynch")) {
    OS.emitInt32(0);
  } else {
    OS.emitInt32(1);
  }

  // UnwindMapEntry {
  //   int32_t ToState;
  //   void  (*Action)();
  // };
  if (UnwindMapXData) {
    OS.emitLabel(UnwindMapXData);
    for (const CxxUnwindMapEntry &UME : FuncInfo.CxxUnwindMap) {
      MCSymbol *CleanupSym = getMCSymbolForMBB(
          Asm, dyn_cast_if_present<MachineBasicBlock *>(UME.Cleanup));
      AddComment("ToState");
      OS.emitInt32(UME.ToState);

      AddComment("Action");
      OS.emitValue(create32bitRef(CleanupSym), 4);
    }
  }

  // TryBlockMap {
  //   int32_t      TryLow;
  //   int32_t      TryHigh;
  //   int32_t      CatchHigh;
  //   int32_t      NumCatches;
  //   HandlerType *HandlerArray;
  // };
  if (TryBlockMapXData) {
    OS.emitLabel(TryBlockMapXData);
    SmallVector<MCSymbol *, 1> HandlerMaps;
    for (size_t I = 0, E = FuncInfo.TryBlockMap.size(); I != E; ++I) {
      const WinEHTryBlockMapEntry &TBME = FuncInfo.TryBlockMap[I];

      MCSymbol *HandlerMapXData = nullptr;
      if (!TBME.HandlerArray.empty())
        HandlerMapXData =
            Asm->OutContext.getOrCreateSymbol(Twine("$handlerMap$")
                                                  .concat(Twine(I))
                                                  .concat("$")
                                                  .concat(FuncLinkageName));
      HandlerMaps.push_back(HandlerMapXData);

      // TBMEs should form intervals.
      assert(0 <= TBME.TryLow && "bad trymap interval");
      assert(TBME.TryLow <= TBME.TryHigh && "bad trymap interval");
      assert(TBME.TryHigh < TBME.CatchHigh && "bad trymap interval");
      assert(TBME.CatchHigh < int(FuncInfo.CxxUnwindMap.size()) &&
             "bad trymap interval");

      AddComment("TryLow");
      OS.emitInt32(TBME.TryLow);

      AddComment("TryHigh");
      OS.emitInt32(TBME.TryHigh);

      AddComment("CatchHigh");
      OS.emitInt32(TBME.CatchHigh);

      AddComment("NumCatches");
      OS.emitInt32(TBME.HandlerArray.size());

      AddComment("HandlerArray");
      OS.emitValue(create32bitRef(HandlerMapXData), 4);
    }

    // All funclets use the same parent frame offset currently.
    unsigned ParentFrameOffset = 0;
    if (shouldEmitPersonality) {
      const TargetFrameLowering *TFI = MF->getSubtarget().getFrameLowering();
      ParentFrameOffset = TFI->getWinEHParentFrameOffset(*MF);
    }

    for (size_t I = 0, E = FuncInfo.TryBlockMap.size(); I != E; ++I) {
      const WinEHTryBlockMapEntry &TBME = FuncInfo.TryBlockMap[I];
      MCSymbol *HandlerMapXData = HandlerMaps[I];
      if (!HandlerMapXData)
        continue;
      // HandlerType {
      //   int32_t         Adjectives;
      //   TypeDescriptor *Type;
      //   int32_t         CatchObjOffset;
      //   void          (*Handler)();
      //   int32_t         ParentFrameOffset; // x64 and AArch64 only
      // };
      OS.emitLabel(HandlerMapXData);
      for (const WinEHHandlerType &HT : TBME.HandlerArray) {
        // Get the frame escape label with the offset of the catch object. If
        // the index is INT_MAX, then there is no catch object, and we should
        // emit an offset of zero, indicating that no copy will occur.
        const MCExpr *FrameAllocOffsetRef = nullptr;
        if (HT.CatchObj.FrameIndex != INT_MAX) {
          int Offset = getFrameIndexOffset(HT.CatchObj.FrameIndex, FuncInfo);
          assert(Offset != 0 && "Illegal offset for catch object!");
          FrameAllocOffsetRef = MCConstantExpr::create(Offset, Asm->OutContext);
        } else {
          FrameAllocOffsetRef = MCConstantExpr::create(0, Asm->OutContext);
        }

        MCSymbol *HandlerSym = getMCSymbolForMBB(
            Asm, dyn_cast_if_present<MachineBasicBlock *>(HT.Handler));

        AddComment("Adjectives");
        OS.emitInt32(HT.Adjectives);

        AddComment("Type");
        OS.emitValue(create32bitRef(HT.TypeDescriptor), 4);

        AddComment("CatchObjOffset");
        OS.emitValue(FrameAllocOffsetRef, 4);

        AddComment("Handler");
        OS.emitValue(create32bitRef(HandlerSym), 4);

        if (shouldEmitPersonality) {
          AddComment("ParentFrameOffset");
          OS.emitInt32(ParentFrameOffset);
        }
      }
    }
  }

  // IPToStateMapEntry {
  //   void   *IP;
  //   int32_t State;
  // };
  if (IPToStateXData) {
    OS.emitLabel(IPToStateXData);
    for (auto &IPStatePair : IPToStateTable) {
      AddComment("IP");
      OS.emitValue(IPStatePair.first, 4);
      AddComment("ToState");
      OS.emitInt32(IPStatePair.second);
    }
  }
}

void WinException::computeIP2StateTable(
    const MachineFunction *MF, const WinEHFuncInfo &FuncInfo,
    SmallVectorImpl<std::pair<const MCExpr *, int>> &IPToStateTable) {

  for (MachineFunction::const_iterator FuncletStart = MF->begin(),
                                       FuncletEnd = MF->begin(),
                                       End = MF->end();
       FuncletStart != End; FuncletStart = FuncletEnd) {
    // Find the end of the funclet
    while (++FuncletEnd != End) {
      if (FuncletEnd->isEHFuncletEntry()) {
        break;
      }
    }

    // Don't emit ip2state entries for cleanup funclets. Any interesting
    // exceptional actions in cleanups must be handled in a separate IR
    // function.
    if (FuncletStart->isCleanupFuncletEntry())
      continue;

    MCSymbol *StartLabel;
    int BaseState;
    if (FuncletStart == MF->begin()) {
      BaseState = NullState;
      StartLabel = Asm->getFunctionBegin();
    } else {
      auto *FuncletPad =
          cast<FuncletPadInst>(FuncletStart->getBasicBlock()->getFirstNonPHI());
      assert(FuncInfo.FuncletBaseStateMap.count(FuncletPad) != 0);
      BaseState = FuncInfo.FuncletBaseStateMap.find(FuncletPad)->second;
      StartLabel = getMCSymbolForMBB(Asm, &*FuncletStart);
    }
    assert(StartLabel && "need local function start label");
    IPToStateTable.push_back(
        std::make_pair(create32bitRef(StartLabel), BaseState));

    for (const auto &StateChange : InvokeStateChangeIterator::range(
             FuncInfo, FuncletStart, FuncletEnd, BaseState)) {
      // Compute the label to report as the start of this entry; use the EH
      // start label for the invoke if we have one, otherwise (this is a call
      // which may unwind to our caller and does not have an EH start label, so)
      // use the previous end label.
      const MCSymbol *ChangeLabel = StateChange.NewStartLabel;
      if (!ChangeLabel)
        ChangeLabel = StateChange.PreviousEndLabel;
      // Emit an entry indicating that PCs after 'Label' have this EH state.
      // NOTE: On ARM architectures, the StateFromIp automatically takes into
      // account that the return address is after the call instruction (whose EH
      // state we should be using), but on other platforms we need to +1 to the
      // label so that we are using the correct EH state.
      const MCExpr *LabelExpression = (isAArch64 || isThumb)
                                          ? getLabel(ChangeLabel)
                                          : getLabelPlusOne(ChangeLabel);
      IPToStateTable.push_back(
          std::make_pair(LabelExpression, StateChange.NewState));
      // FIXME: assert that NewState is between CatchLow and CatchHigh.
    }
  }
}

void WinException::emitEHRegistrationOffsetLabel(const WinEHFuncInfo &FuncInfo,
                                                 StringRef FLinkageName) {
  // Outlined helpers called by the EH runtime need to know the offset of the EH
  // registration in order to recover the parent frame pointer. Now that we know
  // we've code generated the parent, we can emit the label assignment that
  // those helpers use to get the offset of the registration node.

  // Compute the parent frame offset. The EHRegNodeFrameIndex will be invalid if
  // after optimization all the invokes were eliminated. We still need to emit
  // the parent frame offset label, but it should be garbage and should never be
  // used.
  int64_t Offset = 0;
  int FI = FuncInfo.EHRegNodeFrameIndex;
  if (FI != INT_MAX) {
    const TargetFrameLowering *TFI = Asm->MF->getSubtarget().getFrameLowering();
    Offset = TFI->getNonLocalFrameIndexReference(*Asm->MF, FI).getFixed();
  }

  MCContext &Ctx = Asm->OutContext;
  MCSymbol *ParentFrameOffset =
      Ctx.getOrCreateParentFrameOffsetSymbol(FLinkageName);
  Asm->OutStreamer->emitAssignment(ParentFrameOffset,
                                   MCConstantExpr::create(Offset, Ctx));
}

/// Emit the language-specific data that _except_handler3 and 4 expect. This is
/// functionally equivalent to the __C_specific_handler table, except it is
/// indexed by state number instead of IP.
void WinException::emitExceptHandlerTable(const MachineFunction *MF) {
  MCStreamer &OS = *Asm->OutStreamer;
  const Function &F = MF->getFunction();
  StringRef FLinkageName = GlobalValue::dropLLVMManglingEscape(F.getName());

  bool VerboseAsm = OS.isVerboseAsm();
  auto AddComment = [&](const Twine &Comment) {
    if (VerboseAsm)
      OS.AddComment(Comment);
  };

  const WinEHFuncInfo &FuncInfo = *MF->getWinEHFuncInfo();
  emitEHRegistrationOffsetLabel(FuncInfo, FLinkageName);

  // Emit the __ehtable label that we use for llvm.x86.seh.lsda.
  MCSymbol *LSDALabel = Asm->OutContext.getOrCreateLSDASymbol(FLinkageName);
  OS.emitValueToAlignment(Align(4));
  OS.emitLabel(LSDALabel);

  const auto *Per = cast<Function>(F.getPersonalityFn()->stripPointerCasts());
  StringRef PerName = Per->getName();
  int BaseState = -1;
  if (PerName == "_except_handler4") {
    // The LSDA for _except_handler4 starts with this struct, followed by the
    // scope table:
    //
    // struct EH4ScopeTable {
    //   int32_t GSCookieOffset;
    //   int32_t GSCookieXOROffset;
    //   int32_t EHCookieOffset;
    //   int32_t EHCookieXOROffset;
    //   ScopeTableEntry ScopeRecord[];
    // };
    //
    // Offsets are %ebp relative.
    //
    // The GS cookie is present only if the function needs stack protection.
    // GSCookieOffset = -2 means that GS cookie is not used.
    //
    // The EH cookie is always present.
    //
    // Check is done the following way:
    //    (ebp+CookieXOROffset) ^ [ebp+CookieOffset] == _security_cookie

    // Retrieve the Guard Stack slot.
    int GSCookieOffset = -2;
    const MachineFrameInfo &MFI = MF->getFrameInfo();
    if (MFI.hasStackProtectorIndex()) {
      Register UnusedReg;
      const TargetFrameLowering *TFI = MF->getSubtarget().getFrameLowering();
      int SSPIdx = MFI.getStackProtectorIndex();
      GSCookieOffset =
          TFI->getFrameIndexReference(*MF, SSPIdx, UnusedReg).getFixed();
    }

    // Retrieve the EH Guard slot.
    // TODO(etienneb): Get rid of this value and change it for and assertion.
    int EHCookieOffset = 9999;
    if (FuncInfo.EHGuardFrameIndex != INT_MAX) {
      Register UnusedReg;
      const TargetFrameLowering *TFI = MF->getSubtarget().getFrameLowering();
      int EHGuardIdx = FuncInfo.EHGuardFrameIndex;
      EHCookieOffset =
          TFI->getFrameIndexReference(*MF, EHGuardIdx, UnusedReg).getFixed();
    }

    AddComment("GSCookieOffset");
    OS.emitInt32(GSCookieOffset);
    AddComment("GSCookieXOROffset");
    OS.emitInt32(0);
    AddComment("EHCookieOffset");
    OS.emitInt32(EHCookieOffset);
    AddComment("EHCookieXOROffset");
    OS.emitInt32(0);
    BaseState = -2;
  }

  assert(!FuncInfo.SEHUnwindMap.empty());
  for (const SEHUnwindMapEntry &UME : FuncInfo.SEHUnwindMap) {
    auto *Handler = cast<MachineBasicBlock *>(UME.Handler);
    const MCSymbol *ExceptOrFinally =
        UME.IsFinally ? getMCSymbolForMBB(Asm, Handler) : Handler->getSymbol();
    // -1 is usually the base state for "unwind to caller", but for
    // _except_handler4 it's -2. Do that replacement here if necessary.
    int ToState = UME.ToState == -1 ? BaseState : UME.ToState;
    AddComment("ToState");
    OS.emitInt32(ToState);
    AddComment(UME.IsFinally ? "Null" : "FilterFunction");
    OS.emitValue(create32bitRef(UME.Filter), 4);
    AddComment(UME.IsFinally ? "FinallyFunclet" : "ExceptionHandler");
    OS.emitValue(create32bitRef(ExceptOrFinally), 4);
  }
}

static int getTryRank(const WinEHFuncInfo &FuncInfo, int State) {
  int Rank = 0;
  while (State != -1) {
    ++Rank;
    State = FuncInfo.ClrEHUnwindMap[State].TryParentState;
  }
  return Rank;
}

static int getTryAncestor(const WinEHFuncInfo &FuncInfo, int Left, int Right) {
  int LeftRank = getTryRank(FuncInfo, Left);
  int RightRank = getTryRank(FuncInfo, Right);

  while (LeftRank < RightRank) {
    Right = FuncInfo.ClrEHUnwindMap[Right].TryParentState;
    --RightRank;
  }

  while (RightRank < LeftRank) {
    Left = FuncInfo.ClrEHUnwindMap[Left].TryParentState;
    --LeftRank;
  }

  while (Left != Right) {
    Left = FuncInfo.ClrEHUnwindMap[Left].TryParentState;
    Right = FuncInfo.ClrEHUnwindMap[Right].TryParentState;
  }

  return Left;
}

void WinException::emitCLRExceptionTable(const MachineFunction *MF) {
  // CLR EH "states" are really just IDs that identify handlers/funclets;
  // states, handlers, and funclets all have 1:1 mappings between them, and a
  // handler/funclet's "state" is its index in the ClrEHUnwindMap.
  MCStreamer &OS = *Asm->OutStreamer;
  const WinEHFuncInfo &FuncInfo = *MF->getWinEHFuncInfo();
  MCSymbol *FuncBeginSym = Asm->getFunctionBegin();
  MCSymbol *FuncEndSym = Asm->getFunctionEnd();

  // A ClrClause describes a protected region.
  struct ClrClause {
    const MCSymbol *StartLabel; // Start of protected region
    const MCSymbol *EndLabel;   // End of protected region
    int State;          // Index of handler protecting the protected region
    int EnclosingState; // Index of funclet enclosing the protected region
  };
  SmallVector<ClrClause, 8> Clauses;

  // Build a map from handler MBBs to their corresponding states (i.e. their
  // indices in the ClrEHUnwindMap).
  int NumStates = FuncInfo.ClrEHUnwindMap.size();
  assert(NumStates > 0 && "Don't need exception table!");
  DenseMap<const MachineBasicBlock *, int> HandlerStates;
  for (int State = 0; State < NumStates; ++State) {
    MachineBasicBlock *HandlerBlock =
        cast<MachineBasicBlock *>(FuncInfo.ClrEHUnwindMap[State].Handler);
    HandlerStates[HandlerBlock] = State;
    // Use this loop through all handlers to verify our assumption (used in
    // the MinEnclosingState computation) that enclosing funclets have lower
    // state numbers than their enclosed funclets.
    assert(FuncInfo.ClrEHUnwindMap[State].HandlerParentState < State &&
           "ill-formed state numbering");
  }
  // Map the main function to the NullState.
  HandlerStates[&MF->front()] = NullState;

  // Write out a sentinel indicating the end of the standard (Windows) xdata
  // and the start of the additional (CLR) info.
  OS.emitInt32(0xffffffff);
  // Write out the number of funclets
  OS.emitInt32(NumStates);

  // Walk the machine blocks/instrs, computing and emitting a few things:
  // 1. Emit a list of the offsets to each handler entry, in lexical order.
  // 2. Compute a map (EndSymbolMap) from each funclet to the symbol at its end.
  // 3. Compute the list of ClrClauses, in the required order (inner before
  //    outer, earlier before later; the order by which a forward scan with
  //    early termination will find the innermost enclosing clause covering
  //    a given address).
  // 4. A map (MinClauseMap) from each handler index to the index of the
  //    outermost funclet/function which contains a try clause targeting the
  //    key handler.  This will be used to determine IsDuplicate-ness when
  //    emitting ClrClauses.  The NullState value is used to indicate that the
  //    top-level function contains a try clause targeting the key handler.
  // HandlerStack is a stack of (PendingStartLabel, PendingState) pairs for
  // try regions we entered before entering the PendingState try but which
  // we haven't yet exited.
  SmallVector<std::pair<const MCSymbol *, int>, 4> HandlerStack;
  // EndSymbolMap and MinClauseMap are maps described above.
  std::unique_ptr<MCSymbol *[]> EndSymbolMap(new MCSymbol *[NumStates]);
  SmallVector<int, 4> MinClauseMap((size_t)NumStates, NumStates);

  // Visit the root function and each funclet.
  for (MachineFunction::const_iterator FuncletStart = MF->begin(),
                                       FuncletEnd = MF->begin(),
                                       End = MF->end();
       FuncletStart != End; FuncletStart = FuncletEnd) {
    int FuncletState = HandlerStates[&*FuncletStart];
    // Find the end of the funclet
    MCSymbol *EndSymbol = FuncEndSym;
    while (++FuncletEnd != End) {
      if (FuncletEnd->isEHFuncletEntry()) {
        EndSymbol = getMCSymbolForMBB(Asm, &*FuncletEnd);
        break;
      }
    }
    // Emit the function/funclet end and, if this is a funclet (and not the
    // root function), record it in the EndSymbolMap.
    OS.emitValue(getOffset(EndSymbol, FuncBeginSym), 4);
    if (FuncletState != NullState) {
      // Record the end of the handler.
      EndSymbolMap[FuncletState] = EndSymbol;
    }

    // Walk the state changes in this function/funclet and compute its clauses.
    // Funclets always start in the null state.
    const MCSymbol *CurrentStartLabel = nullptr;
    int CurrentState = NullState;
    assert(HandlerStack.empty());
    for (const auto &StateChange :
         InvokeStateChangeIterator::range(FuncInfo, FuncletStart, FuncletEnd)) {
      // Close any try regions we're not still under
      int StillPendingState =
          getTryAncestor(FuncInfo, CurrentState, StateChange.NewState);
      while (CurrentState != StillPendingState) {
        assert(CurrentState != NullState &&
               "Failed to find still-pending state!");
        // Close the pending clause
        Clauses.push_back({CurrentStartLabel, StateChange.PreviousEndLabel,
                           CurrentState, FuncletState});
        // Now the next-outer try region is current
        CurrentState = FuncInfo.ClrEHUnwindMap[CurrentState].TryParentState;
        // Pop the new start label from the handler stack if we've exited all
        // inner try regions of the corresponding try region.
        if (HandlerStack.back().second == CurrentState)
          CurrentStartLabel = HandlerStack.pop_back_val().first;
      }

      if (StateChange.NewState != CurrentState) {
        // For each clause we're starting, update the MinClauseMap so we can
        // know which is the topmost funclet containing a clause targeting
        // it.
        for (int EnteredState = StateChange.NewState;
             EnteredState != CurrentState;
             EnteredState =
                 FuncInfo.ClrEHUnwindMap[EnteredState].TryParentState) {
          int &MinEnclosingState = MinClauseMap[EnteredState];
          if (FuncletState < MinEnclosingState)
            MinEnclosingState = FuncletState;
        }
        // Save the previous current start/label on the stack and update to
        // the newly-current start/state.
        HandlerStack.emplace_back(CurrentStartLabel, CurrentState);
        CurrentStartLabel = StateChange.NewStartLabel;
        CurrentState = StateChange.NewState;
      }
    }
    assert(HandlerStack.empty());
  }

  // Now emit the clause info, starting with the number of clauses.
  OS.emitInt32(Clauses.size());
  for (ClrClause &Clause : Clauses) {
    // Emit a CORINFO_EH_CLAUSE :
    /*
      struct CORINFO_EH_CLAUSE
      {
          CORINFO_EH_CLAUSE_FLAGS Flags;         // actually a CorExceptionFlag
          DWORD                   TryOffset;
          DWORD                   TryLength;     // actually TryEndOffset
          DWORD                   HandlerOffset;
          DWORD                   HandlerLength; // actually HandlerEndOffset
          union
          {
              DWORD               ClassToken;   // use for catch clauses
              DWORD               FilterOffset; // use for filter clauses
          };
      };

      enum CORINFO_EH_CLAUSE_FLAGS
      {
          CORINFO_EH_CLAUSE_NONE    = 0,
          CORINFO_EH_CLAUSE_FILTER  = 0x0001, // This clause is for a filter
          CORINFO_EH_CLAUSE_FINALLY = 0x0002, // This clause is a finally clause
          CORINFO_EH_CLAUSE_FAULT   = 0x0004, // This clause is a fault clause
      };
      typedef enum CorExceptionFlag
      {
          COR_ILEXCEPTION_CLAUSE_NONE,
          COR_ILEXCEPTION_CLAUSE_FILTER  = 0x0001, // This is a filter clause
          COR_ILEXCEPTION_CLAUSE_FINALLY = 0x0002, // This is a finally clause
          COR_ILEXCEPTION_CLAUSE_FAULT = 0x0004,   // This is a fault clause
          COR_ILEXCEPTION_CLAUSE_DUPLICATED = 0x0008, // duplicated clause. This
                                                      // clause was duplicated
                                                      // to a funclet which was
                                                      // pulled out of line
      } CorExceptionFlag;
    */
    // Add 1 to the start/end of the EH clause; the IP associated with a
    // call when the runtime does its scan is the IP of the next instruction
    // (the one to which control will return after the call), so we need
    // to add 1 to the end of the clause to cover that offset.  We also add
    // 1 to the start of the clause to make sure that the ranges reported
    // for all clauses are disjoint.  Note that we'll need some additional
    // logic when machine traps are supported, since in that case the IP
    // that the runtime uses is the offset of the faulting instruction
    // itself; if such an instruction immediately follows a call but the
    // two belong to different clauses, we'll need to insert a nop between
    // them so the runtime can distinguish the point to which the call will
    // return from the point at which the fault occurs.

    const MCExpr *ClauseBegin =
        getOffsetPlusOne(Clause.StartLabel, FuncBeginSym);
    const MCExpr *ClauseEnd = getOffsetPlusOne(Clause.EndLabel, FuncBeginSym);

    const ClrEHUnwindMapEntry &Entry = FuncInfo.ClrEHUnwindMap[Clause.State];
    MachineBasicBlock *HandlerBlock = cast<MachineBasicBlock *>(Entry.Handler);
    MCSymbol *BeginSym = getMCSymbolForMBB(Asm, HandlerBlock);
    const MCExpr *HandlerBegin = getOffset(BeginSym, FuncBeginSym);
    MCSymbol *EndSym = EndSymbolMap[Clause.State];
    const MCExpr *HandlerEnd = getOffset(EndSym, FuncBeginSym);

    uint32_t Flags = 0;
    switch (Entry.HandlerType) {
    case ClrHandlerType::Catch:
      // Leaving bits 0-2 clear indicates catch.
      break;
    case ClrHandlerType::Filter:
      Flags |= 1;
      break;
    case ClrHandlerType::Finally:
      Flags |= 2;
      break;
    case ClrHandlerType::Fault:
      Flags |= 4;
      break;
    }
    if (Clause.EnclosingState != MinClauseMap[Clause.State]) {
      // This is a "duplicate" clause; the handler needs to be entered from a
      // frame above the one holding the invoke.
      assert(Clause.EnclosingState > MinClauseMap[Clause.State]);
      Flags |= 8;
    }
    OS.emitInt32(Flags);

    // Write the clause start/end
    OS.emitValue(ClauseBegin, 4);
    OS.emitValue(ClauseEnd, 4);

    // Write out the handler start/end
    OS.emitValue(HandlerBegin, 4);
    OS.emitValue(HandlerEnd, 4);

    // Write out the type token or filter offset
    assert(Entry.HandlerType != ClrHandlerType::Filter && "NYI: filters");
    OS.emitInt32(Entry.TypeToken);
  }
}
