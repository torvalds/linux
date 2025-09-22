//===- lib/MC/MCObjectStreamer.cpp - Object File MCStreamer Interface -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCCodeView.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCValue.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/SourceMgr.h"
using namespace llvm;

MCObjectStreamer::MCObjectStreamer(MCContext &Context,
                                   std::unique_ptr<MCAsmBackend> TAB,
                                   std::unique_ptr<MCObjectWriter> OW,
                                   std::unique_ptr<MCCodeEmitter> Emitter)
    : MCStreamer(Context),
      Assembler(std::make_unique<MCAssembler>(
          Context, std::move(TAB), std::move(Emitter), std::move(OW))),
      EmitEHFrame(true), EmitDebugFrame(false) {
  assert(Assembler->getBackendPtr() && Assembler->getEmitterPtr());
  setAllowAutoPadding(Assembler->getBackend().allowAutoPadding());
  if (Context.getTargetOptions() && Context.getTargetOptions()->MCRelaxAll)
    Assembler->setRelaxAll(true);
}

MCObjectStreamer::~MCObjectStreamer() = default;

MCAssembler *MCObjectStreamer::getAssemblerPtr() {
  if (getUseAssemblerInfoForParsing())
    return Assembler.get();
  return nullptr;
}

// When fixup's offset is a forward declared label, e.g.:
//
//   .reloc 1f, R_MIPS_JALR, foo
// 1: nop
//
// postpone adding it to Fixups vector until the label is defined and its offset
// is known.
void MCObjectStreamer::resolvePendingFixups() {
  for (PendingMCFixup &PendingFixup : PendingFixups) {
    if (!PendingFixup.Sym || PendingFixup.Sym->isUndefined ()) {
      getContext().reportError(PendingFixup.Fixup.getLoc(),
                               "unresolved relocation offset");
      continue;
    }
    PendingFixup.Fixup.setOffset(PendingFixup.Sym->getOffset() +
                                 PendingFixup.Fixup.getOffset());

    // If the location symbol to relocate is in MCEncodedFragmentWithFixups,
    // put the Fixup into location symbol's fragment. Otherwise
    // put into PendingFixup.DF
    MCFragment *SymFragment = PendingFixup.Sym->getFragment();
    switch (SymFragment->getKind()) {
    case MCFragment::FT_Relaxable:
    case MCFragment::FT_Dwarf:
    case MCFragment::FT_PseudoProbe:
      cast<MCEncodedFragmentWithFixups<8, 1>>(SymFragment)
          ->getFixups()
          .push_back(PendingFixup.Fixup);
      break;
    case MCFragment::FT_Data:
    case MCFragment::FT_CVDefRange:
      cast<MCEncodedFragmentWithFixups<32, 4>>(SymFragment)
          ->getFixups()
          .push_back(PendingFixup.Fixup);
      break;
    default:
      PendingFixup.DF->getFixups().push_back(PendingFixup.Fixup);
      break;
    }
  }
  PendingFixups.clear();
}

// As a compile-time optimization, avoid allocating and evaluating an MCExpr
// tree for (Hi - Lo) when Hi and Lo are offsets into the same fragment.
static std::optional<uint64_t> absoluteSymbolDiff(const MCSymbol *Hi,
                                                  const MCSymbol *Lo) {
  assert(Hi && Lo);
  if (!Hi->getFragment() || Hi->getFragment() != Lo->getFragment() ||
      Hi->isVariable() || Lo->isVariable())
    return std::nullopt;

  return Hi->getOffset() - Lo->getOffset();
}

void MCObjectStreamer::emitAbsoluteSymbolDiff(const MCSymbol *Hi,
                                              const MCSymbol *Lo,
                                              unsigned Size) {
  if (!getAssembler().getContext().getTargetTriple().isRISCV())
    if (std::optional<uint64_t> Diff = absoluteSymbolDiff(Hi, Lo))
      return emitIntValue(*Diff, Size);
  MCStreamer::emitAbsoluteSymbolDiff(Hi, Lo, Size);
}

void MCObjectStreamer::emitAbsoluteSymbolDiffAsULEB128(const MCSymbol *Hi,
                                                       const MCSymbol *Lo) {
  if (!getAssembler().getContext().getTargetTriple().isRISCV())
    if (std::optional<uint64_t> Diff = absoluteSymbolDiff(Hi, Lo)) {
      emitULEB128IntValue(*Diff);
      return;
    }
  MCStreamer::emitAbsoluteSymbolDiffAsULEB128(Hi, Lo);
}

void MCObjectStreamer::reset() {
  if (Assembler) {
    Assembler->reset();
    if (getContext().getTargetOptions())
      Assembler->setRelaxAll(getContext().getTargetOptions()->MCRelaxAll);
  }
  EmitEHFrame = true;
  EmitDebugFrame = false;
  MCStreamer::reset();
}

void MCObjectStreamer::emitFrames(MCAsmBackend *MAB) {
  if (!getNumFrameInfos())
    return;

  if (EmitEHFrame)
    MCDwarfFrameEmitter::Emit(*this, MAB, true);

  if (EmitDebugFrame)
    MCDwarfFrameEmitter::Emit(*this, MAB, false);
}

static bool canReuseDataFragment(const MCDataFragment &F,
                                 const MCAssembler &Assembler,
                                 const MCSubtargetInfo *STI) {
  if (!F.hasInstructions())
    return true;
  // Do not add data after a linker-relaxable instruction. The difference
  // between a new label and a label at or before the linker-relaxable
  // instruction cannot be resolved at assemble-time.
  if (F.isLinkerRelaxable())
    return false;
  // When bundling is enabled, we don't want to add data to a fragment that
  // already has instructions (see MCELFStreamer::emitInstToData for details)
  if (Assembler.isBundlingEnabled())
    return false;
  // If the subtarget is changed mid fragment we start a new fragment to record
  // the new STI.
  return !STI || F.getSubtargetInfo() == STI;
}

MCDataFragment *
MCObjectStreamer::getOrCreateDataFragment(const MCSubtargetInfo *STI) {
  auto *F = dyn_cast<MCDataFragment>(getCurrentFragment());
  if (!F || !canReuseDataFragment(*F, *Assembler, STI)) {
    F = getContext().allocFragment<MCDataFragment>();
    insert(F);
  }
  return F;
}

void MCObjectStreamer::visitUsedSymbol(const MCSymbol &Sym) {
  Assembler->registerSymbol(Sym);
}

void MCObjectStreamer::emitCFISections(bool EH, bool Debug) {
  MCStreamer::emitCFISections(EH, Debug);
  EmitEHFrame = EH;
  EmitDebugFrame = Debug;
}

void MCObjectStreamer::emitValueImpl(const MCExpr *Value, unsigned Size,
                                     SMLoc Loc) {
  MCStreamer::emitValueImpl(Value, Size, Loc);
  MCDataFragment *DF = getOrCreateDataFragment();

  MCDwarfLineEntry::make(this, getCurrentSectionOnly());

  // Avoid fixups when possible.
  int64_t AbsValue;
  if (Value->evaluateAsAbsolute(AbsValue, getAssemblerPtr())) {
    if (!isUIntN(8 * Size, AbsValue) && !isIntN(8 * Size, AbsValue)) {
      getContext().reportError(
          Loc, "value evaluated as " + Twine(AbsValue) + " is out of range.");
      return;
    }
    emitIntValue(AbsValue, Size);
    return;
  }
  DF->getFixups().push_back(
      MCFixup::create(DF->getContents().size(), Value,
                      MCFixup::getKindForSize(Size, false), Loc));
  DF->getContents().resize(DF->getContents().size() + Size, 0);
}

MCSymbol *MCObjectStreamer::emitCFILabel() {
  MCSymbol *Label = getContext().createTempSymbol("cfi");
  emitLabel(Label);
  return Label;
}

void MCObjectStreamer::emitCFIStartProcImpl(MCDwarfFrameInfo &Frame) {
  // We need to create a local symbol to avoid relocations.
  Frame.Begin = getContext().createTempSymbol();
  emitLabel(Frame.Begin);
}

void MCObjectStreamer::emitCFIEndProcImpl(MCDwarfFrameInfo &Frame) {
  Frame.End = getContext().createTempSymbol();
  emitLabel(Frame.End);
}

void MCObjectStreamer::emitLabel(MCSymbol *Symbol, SMLoc Loc) {
  MCStreamer::emitLabel(Symbol, Loc);

  getAssembler().registerSymbol(*Symbol);

  // If there is a current fragment, mark the symbol as pointing into it.
  // Otherwise queue the label and set its fragment pointer when we emit the
  // next fragment.
  MCDataFragment *F = getOrCreateDataFragment();
  Symbol->setFragment(F);
  Symbol->setOffset(F->getContents().size());

  emitPendingAssignments(Symbol);
}

void MCObjectStreamer::emitPendingAssignments(MCSymbol *Symbol) {
  auto Assignments = pendingAssignments.find(Symbol);
  if (Assignments != pendingAssignments.end()) {
    for (const PendingAssignment &A : Assignments->second)
      emitAssignment(A.Symbol, A.Value);

    pendingAssignments.erase(Assignments);
  }
}

// Emit a label at a previously emitted fragment/offset position. This must be
// within the currently-active section.
void MCObjectStreamer::emitLabelAtPos(MCSymbol *Symbol, SMLoc Loc,
                                      MCDataFragment &F, uint64_t Offset) {
  assert(F.getParent() == getCurrentSectionOnly());
  MCStreamer::emitLabel(Symbol, Loc);
  getAssembler().registerSymbol(*Symbol);
  Symbol->setFragment(&F);
  Symbol->setOffset(Offset);
}

void MCObjectStreamer::emitULEB128Value(const MCExpr *Value) {
  int64_t IntValue;
  if (Value->evaluateAsAbsolute(IntValue, getAssemblerPtr())) {
    emitULEB128IntValue(IntValue);
    return;
  }
  insert(getContext().allocFragment<MCLEBFragment>(*Value, false));
}

void MCObjectStreamer::emitSLEB128Value(const MCExpr *Value) {
  int64_t IntValue;
  if (Value->evaluateAsAbsolute(IntValue, getAssemblerPtr())) {
    emitSLEB128IntValue(IntValue);
    return;
  }
  insert(getContext().allocFragment<MCLEBFragment>(*Value, true));
}

void MCObjectStreamer::emitWeakReference(MCSymbol *Alias,
                                         const MCSymbol *Symbol) {
  report_fatal_error("This file format doesn't support weak aliases.");
}

void MCObjectStreamer::changeSection(MCSection *Section, uint32_t Subsection) {
  changeSectionImpl(Section, Subsection);
}

bool MCObjectStreamer::changeSectionImpl(MCSection *Section,
                                         uint32_t Subsection) {
  assert(Section && "Cannot switch to a null section!");
  getContext().clearDwarfLocSeen();

  auto &Subsections = Section->Subsections;
  size_t I = 0, E = Subsections.size();
  while (I != E && Subsections[I].first < Subsection)
    ++I;
  // If the subsection number is not in the sorted Subsections list, create a
  // new fragment list.
  if (I == E || Subsections[I].first != Subsection) {
    auto *F = getContext().allocFragment<MCDataFragment>();
    F->setParent(Section);
    Subsections.insert(Subsections.begin() + I,
                       {Subsection, MCSection::FragList{F, F}});
  }
  Section->CurFragList = &Subsections[I].second;
  CurFrag = Section->CurFragList->Tail;

  return getAssembler().registerSection(*Section);
}

void MCObjectStreamer::switchSectionNoPrint(MCSection *Section) {
  MCStreamer::switchSectionNoPrint(Section);
  changeSection(Section, 0);
}

void MCObjectStreamer::emitAssignment(MCSymbol *Symbol, const MCExpr *Value) {
  getAssembler().registerSymbol(*Symbol);
  MCStreamer::emitAssignment(Symbol, Value);
  emitPendingAssignments(Symbol);
}

void MCObjectStreamer::emitConditionalAssignment(MCSymbol *Symbol,
                                                 const MCExpr *Value) {
  const MCSymbol *Target = &cast<MCSymbolRefExpr>(*Value).getSymbol();

  // If the symbol already exists, emit the assignment. Otherwise, emit it
  // later only if the symbol is also emitted.
  if (Target->isRegistered())
    emitAssignment(Symbol, Value);
  else
    pendingAssignments[Target].push_back({Symbol, Value});
}

bool MCObjectStreamer::mayHaveInstructions(MCSection &Sec) const {
  return Sec.hasInstructions();
}

void MCObjectStreamer::emitInstruction(const MCInst &Inst,
                                       const MCSubtargetInfo &STI) {
  const MCSection &Sec = *getCurrentSectionOnly();
  if (Sec.isVirtualSection()) {
    getContext().reportError(Inst.getLoc(), Twine(Sec.getVirtualSectionKind()) +
                                                " section '" + Sec.getName() +
                                                "' cannot have instructions");
    return;
  }
  emitInstructionImpl(Inst, STI);
}

void MCObjectStreamer::emitInstructionImpl(const MCInst &Inst,
                                           const MCSubtargetInfo &STI) {
  MCStreamer::emitInstruction(Inst, STI);

  MCSection *Sec = getCurrentSectionOnly();
  Sec->setHasInstructions(true);

  // Now that a machine instruction has been assembled into this section, make
  // a line entry for any .loc directive that has been seen.
  MCDwarfLineEntry::make(this, getCurrentSectionOnly());

  // If this instruction doesn't need relaxation, just emit it as data.
  MCAssembler &Assembler = getAssembler();
  MCAsmBackend &Backend = Assembler.getBackend();
  if (!(Backend.mayNeedRelaxation(Inst, STI) ||
        Backend.allowEnhancedRelaxation())) {
    emitInstToData(Inst, STI);
    return;
  }

  // Otherwise, relax and emit it as data if either:
  // - The RelaxAll flag was passed
  // - Bundling is enabled and this instruction is inside a bundle-locked
  //   group. We want to emit all such instructions into the same data
  //   fragment.
  if (Assembler.getRelaxAll() ||
      (Assembler.isBundlingEnabled() && Sec->isBundleLocked())) {
    MCInst Relaxed = Inst;
    while (Backend.mayNeedRelaxation(Relaxed, STI))
      Backend.relaxInstruction(Relaxed, STI);
    emitInstToData(Relaxed, STI);
    return;
  }

  // Otherwise emit to a separate fragment.
  emitInstToFragment(Inst, STI);
}

void MCObjectStreamer::emitInstToFragment(const MCInst &Inst,
                                          const MCSubtargetInfo &STI) {
  // Always create a new, separate fragment here, because its size can change
  // during relaxation.
  MCRelaxableFragment *IF =
      getContext().allocFragment<MCRelaxableFragment>(Inst, STI);
  insert(IF);

  getAssembler().getEmitter().encodeInstruction(Inst, IF->getContents(),
                                                IF->getFixups(), STI);
}

#ifndef NDEBUG
static const char *const BundlingNotImplementedMsg =
  "Aligned bundling is not implemented for this object format";
#endif

void MCObjectStreamer::emitBundleAlignMode(Align Alignment) {
  llvm_unreachable(BundlingNotImplementedMsg);
}

void MCObjectStreamer::emitBundleLock(bool AlignToEnd) {
  llvm_unreachable(BundlingNotImplementedMsg);
}

void MCObjectStreamer::emitBundleUnlock() {
  llvm_unreachable(BundlingNotImplementedMsg);
}

void MCObjectStreamer::emitDwarfLocDirective(unsigned FileNo, unsigned Line,
                                             unsigned Column, unsigned Flags,
                                             unsigned Isa,
                                             unsigned Discriminator,
                                             StringRef FileName) {
  // In case we see two .loc directives in a row, make sure the
  // first one gets a line entry.
  MCDwarfLineEntry::make(this, getCurrentSectionOnly());

  this->MCStreamer::emitDwarfLocDirective(FileNo, Line, Column, Flags, Isa,
                                          Discriminator, FileName);
}

static const MCExpr *buildSymbolDiff(MCObjectStreamer &OS, const MCSymbol *A,
                                     const MCSymbol *B, SMLoc Loc) {
  MCContext &Context = OS.getContext();
  MCSymbolRefExpr::VariantKind Variant = MCSymbolRefExpr::VK_None;
  const MCExpr *ARef = MCSymbolRefExpr::create(A, Variant, Context);
  const MCExpr *BRef = MCSymbolRefExpr::create(B, Variant, Context);
  const MCExpr *AddrDelta =
      MCBinaryExpr::create(MCBinaryExpr::Sub, ARef, BRef, Context, Loc);
  return AddrDelta;
}

static void emitDwarfSetLineAddr(MCObjectStreamer &OS,
                                 MCDwarfLineTableParams Params,
                                 int64_t LineDelta, const MCSymbol *Label,
                                 int PointerSize) {
  // emit the sequence to set the address
  OS.emitIntValue(dwarf::DW_LNS_extended_op, 1);
  OS.emitULEB128IntValue(PointerSize + 1);
  OS.emitIntValue(dwarf::DW_LNE_set_address, 1);
  OS.emitSymbolValue(Label, PointerSize);

  // emit the sequence for the LineDelta (from 1) and a zero address delta.
  MCDwarfLineAddr::Emit(&OS, Params, LineDelta, 0);
}

void MCObjectStreamer::emitDwarfAdvanceLineAddr(int64_t LineDelta,
                                                const MCSymbol *LastLabel,
                                                const MCSymbol *Label,
                                                unsigned PointerSize) {
  if (!LastLabel) {
    emitDwarfSetLineAddr(*this, Assembler->getDWARFLinetableParams(), LineDelta,
                         Label, PointerSize);
    return;
  }
  const MCExpr *AddrDelta = buildSymbolDiff(*this, Label, LastLabel, SMLoc());
  insert(getContext().allocFragment<MCDwarfLineAddrFragment>(LineDelta,
                                                             *AddrDelta));
}

void MCObjectStreamer::emitDwarfLineEndEntry(MCSection *Section,
                                             MCSymbol *LastLabel) {
  // Emit a DW_LNE_end_sequence for the end of the section.
  // Use the section end label to compute the address delta and use INT64_MAX
  // as the line delta which is the signal that this is actually a
  // DW_LNE_end_sequence.
  MCSymbol *SectionEnd = endSection(Section);

  // Switch back the dwarf line section, in case endSection had to switch the
  // section.
  MCContext &Ctx = getContext();
  switchSection(Ctx.getObjectFileInfo()->getDwarfLineSection());

  const MCAsmInfo *AsmInfo = Ctx.getAsmInfo();
  emitDwarfAdvanceLineAddr(INT64_MAX, LastLabel, SectionEnd,
                           AsmInfo->getCodePointerSize());
}

void MCObjectStreamer::emitDwarfAdvanceFrameAddr(const MCSymbol *LastLabel,
                                                 const MCSymbol *Label,
                                                 SMLoc Loc) {
  const MCExpr *AddrDelta = buildSymbolDiff(*this, Label, LastLabel, Loc);
  insert(getContext().allocFragment<MCDwarfCallFrameFragment>(*AddrDelta));
}

void MCObjectStreamer::emitCVLocDirective(unsigned FunctionId, unsigned FileNo,
                                          unsigned Line, unsigned Column,
                                          bool PrologueEnd, bool IsStmt,
                                          StringRef FileName, SMLoc Loc) {
  // Validate the directive.
  if (!checkCVLocSection(FunctionId, FileNo, Loc))
    return;

  // Emit a label at the current position and record it in the CodeViewContext.
  MCSymbol *LineSym = getContext().createTempSymbol();
  emitLabel(LineSym);
  getContext().getCVContext().recordCVLoc(getContext(), LineSym, FunctionId,
                                          FileNo, Line, Column, PrologueEnd,
                                          IsStmt);
}

void MCObjectStreamer::emitCVLinetableDirective(unsigned FunctionId,
                                                const MCSymbol *Begin,
                                                const MCSymbol *End) {
  getContext().getCVContext().emitLineTableForFunction(*this, FunctionId, Begin,
                                                       End);
  this->MCStreamer::emitCVLinetableDirective(FunctionId, Begin, End);
}

void MCObjectStreamer::emitCVInlineLinetableDirective(
    unsigned PrimaryFunctionId, unsigned SourceFileId, unsigned SourceLineNum,
    const MCSymbol *FnStartSym, const MCSymbol *FnEndSym) {
  getContext().getCVContext().emitInlineLineTableForFunction(
      *this, PrimaryFunctionId, SourceFileId, SourceLineNum, FnStartSym,
      FnEndSym);
  this->MCStreamer::emitCVInlineLinetableDirective(
      PrimaryFunctionId, SourceFileId, SourceLineNum, FnStartSym, FnEndSym);
}

void MCObjectStreamer::emitCVDefRangeDirective(
    ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
    StringRef FixedSizePortion) {
  getContext().getCVContext().emitDefRange(*this, Ranges, FixedSizePortion);
  // Attach labels that were pending before we created the defrange fragment to
  // the beginning of the new fragment.
  this->MCStreamer::emitCVDefRangeDirective(Ranges, FixedSizePortion);
}

void MCObjectStreamer::emitCVStringTableDirective() {
  getContext().getCVContext().emitStringTable(*this);
}
void MCObjectStreamer::emitCVFileChecksumsDirective() {
  getContext().getCVContext().emitFileChecksums(*this);
}

void MCObjectStreamer::emitCVFileChecksumOffsetDirective(unsigned FileNo) {
  getContext().getCVContext().emitFileChecksumOffset(*this, FileNo);
}

void MCObjectStreamer::emitBytes(StringRef Data) {
  MCDwarfLineEntry::make(this, getCurrentSectionOnly());
  MCDataFragment *DF = getOrCreateDataFragment();
  DF->getContents().append(Data.begin(), Data.end());
}

void MCObjectStreamer::emitValueToAlignment(Align Alignment, int64_t Value,
                                            unsigned ValueSize,
                                            unsigned MaxBytesToEmit) {
  if (MaxBytesToEmit == 0)
    MaxBytesToEmit = Alignment.value();
  insert(getContext().allocFragment<MCAlignFragment>(
      Alignment, Value, ValueSize, MaxBytesToEmit));

  // Update the maximum alignment on the current section if necessary.
  MCSection *CurSec = getCurrentSectionOnly();
  CurSec->ensureMinAlignment(Alignment);
}

void MCObjectStreamer::emitCodeAlignment(Align Alignment,
                                         const MCSubtargetInfo *STI,
                                         unsigned MaxBytesToEmit) {
  emitValueToAlignment(Alignment, 0, 1, MaxBytesToEmit);
  cast<MCAlignFragment>(getCurrentFragment())->setEmitNops(true, STI);
}

void MCObjectStreamer::emitValueToOffset(const MCExpr *Offset,
                                         unsigned char Value,
                                         SMLoc Loc) {
  insert(getContext().allocFragment<MCOrgFragment>(*Offset, Value, Loc));
}

// Associate DTPRel32 fixup with data and resize data area
void MCObjectStreamer::emitDTPRel32Value(const MCExpr *Value) {
  MCDataFragment *DF = getOrCreateDataFragment();
  DF->getFixups().push_back(MCFixup::create(DF->getContents().size(),
                                            Value, FK_DTPRel_4));
  DF->getContents().resize(DF->getContents().size() + 4, 0);
}

// Associate DTPRel64 fixup with data and resize data area
void MCObjectStreamer::emitDTPRel64Value(const MCExpr *Value) {
  MCDataFragment *DF = getOrCreateDataFragment();
  DF->getFixups().push_back(MCFixup::create(DF->getContents().size(),
                                            Value, FK_DTPRel_8));
  DF->getContents().resize(DF->getContents().size() + 8, 0);
}

// Associate TPRel32 fixup with data and resize data area
void MCObjectStreamer::emitTPRel32Value(const MCExpr *Value) {
  MCDataFragment *DF = getOrCreateDataFragment();
  DF->getFixups().push_back(MCFixup::create(DF->getContents().size(),
                                            Value, FK_TPRel_4));
  DF->getContents().resize(DF->getContents().size() + 4, 0);
}

// Associate TPRel64 fixup with data and resize data area
void MCObjectStreamer::emitTPRel64Value(const MCExpr *Value) {
  MCDataFragment *DF = getOrCreateDataFragment();
  DF->getFixups().push_back(MCFixup::create(DF->getContents().size(),
                                            Value, FK_TPRel_8));
  DF->getContents().resize(DF->getContents().size() + 8, 0);
}

// Associate GPRel32 fixup with data and resize data area
void MCObjectStreamer::emitGPRel32Value(const MCExpr *Value) {
  MCDataFragment *DF = getOrCreateDataFragment();
  DF->getFixups().push_back(
      MCFixup::create(DF->getContents().size(), Value, FK_GPRel_4));
  DF->getContents().resize(DF->getContents().size() + 4, 0);
}

// Associate GPRel64 fixup with data and resize data area
void MCObjectStreamer::emitGPRel64Value(const MCExpr *Value) {
  MCDataFragment *DF = getOrCreateDataFragment();
  DF->getFixups().push_back(
      MCFixup::create(DF->getContents().size(), Value, FK_GPRel_4));
  DF->getContents().resize(DF->getContents().size() + 8, 0);
}

static std::optional<std::pair<bool, std::string>>
getOffsetAndDataFragment(const MCSymbol &Symbol, uint32_t &RelocOffset,
                         MCDataFragment *&DF) {
  if (Symbol.isVariable()) {
    const MCExpr *SymbolExpr = Symbol.getVariableValue();
    MCValue OffsetVal;
    if(!SymbolExpr->evaluateAsRelocatable(OffsetVal, nullptr, nullptr))
      return std::make_pair(false,
                            std::string("symbol in .reloc offset is not "
                                        "relocatable"));
    if (OffsetVal.isAbsolute()) {
      RelocOffset = OffsetVal.getConstant();
      MCFragment *Fragment = Symbol.getFragment();
      // FIXME Support symbols with no DF. For example:
      // .reloc .data, ENUM_VALUE, <some expr>
      if (!Fragment || Fragment->getKind() != MCFragment::FT_Data)
        return std::make_pair(false,
                              std::string("symbol in offset has no data "
                                          "fragment"));
      DF = cast<MCDataFragment>(Fragment);
      return std::nullopt;
    }

    if (OffsetVal.getSymB())
      return std::make_pair(false,
                            std::string(".reloc symbol offset is not "
                                        "representable"));

    const MCSymbolRefExpr &SRE = cast<MCSymbolRefExpr>(*OffsetVal.getSymA());
    if (!SRE.getSymbol().isDefined())
      return std::make_pair(false,
                            std::string("symbol used in the .reloc offset is "
                                        "not defined"));

    if (SRE.getSymbol().isVariable())
      return std::make_pair(false,
                            std::string("symbol used in the .reloc offset is "
                                        "variable"));

    MCFragment *Fragment = SRE.getSymbol().getFragment();
    // FIXME Support symbols with no DF. For example:
    // .reloc .data, ENUM_VALUE, <some expr>
    if (!Fragment || Fragment->getKind() != MCFragment::FT_Data)
      return std::make_pair(false,
                            std::string("symbol in offset has no data "
                                        "fragment"));
    RelocOffset = SRE.getSymbol().getOffset() + OffsetVal.getConstant();
    DF = cast<MCDataFragment>(Fragment);
  } else {
    RelocOffset = Symbol.getOffset();
    MCFragment *Fragment = Symbol.getFragment();
    // FIXME Support symbols with no DF. For example:
    // .reloc .data, ENUM_VALUE, <some expr>
    if (!Fragment || Fragment->getKind() != MCFragment::FT_Data)
      return std::make_pair(false,
                            std::string("symbol in offset has no data "
                                        "fragment"));
    DF = cast<MCDataFragment>(Fragment);
  }
  return std::nullopt;
}

std::optional<std::pair<bool, std::string>>
MCObjectStreamer::emitRelocDirective(const MCExpr &Offset, StringRef Name,
                                     const MCExpr *Expr, SMLoc Loc,
                                     const MCSubtargetInfo &STI) {
  std::optional<MCFixupKind> MaybeKind =
      Assembler->getBackend().getFixupKind(Name);
  if (!MaybeKind)
    return std::make_pair(true, std::string("unknown relocation name"));

  MCFixupKind Kind = *MaybeKind;
  if (Expr)
    visitUsedExpr(*Expr);
  else
    Expr =
        MCSymbolRefExpr::create(getContext().createTempSymbol(), getContext());

  MCDataFragment *DF = getOrCreateDataFragment(&STI);
  MCValue OffsetVal;
  if (!Offset.evaluateAsRelocatable(OffsetVal, nullptr, nullptr))
    return std::make_pair(false,
                          std::string(".reloc offset is not relocatable"));
  if (OffsetVal.isAbsolute()) {
    if (OffsetVal.getConstant() < 0)
      return std::make_pair(false, std::string(".reloc offset is negative"));
    DF->getFixups().push_back(
        MCFixup::create(OffsetVal.getConstant(), Expr, Kind, Loc));
    return std::nullopt;
  }
  if (OffsetVal.getSymB())
    return std::make_pair(false,
                          std::string(".reloc offset is not representable"));

  const MCSymbolRefExpr &SRE = cast<MCSymbolRefExpr>(*OffsetVal.getSymA());
  const MCSymbol &Symbol = SRE.getSymbol();
  if (Symbol.isDefined()) {
    uint32_t SymbolOffset = 0;
    std::optional<std::pair<bool, std::string>> Error =
        getOffsetAndDataFragment(Symbol, SymbolOffset, DF);

    if (Error != std::nullopt)
      return Error;

    DF->getFixups().push_back(
        MCFixup::create(SymbolOffset + OffsetVal.getConstant(),
                        Expr, Kind, Loc));
    return std::nullopt;
  }

  PendingFixups.emplace_back(
      &SRE.getSymbol(), DF,
      MCFixup::create(OffsetVal.getConstant(), Expr, Kind, Loc));
  return std::nullopt;
}

void MCObjectStreamer::emitFill(const MCExpr &NumBytes, uint64_t FillValue,
                                SMLoc Loc) {
  assert(getCurrentSectionOnly() && "need a section");
  insert(
      getContext().allocFragment<MCFillFragment>(FillValue, 1, NumBytes, Loc));
}

void MCObjectStreamer::emitFill(const MCExpr &NumValues, int64_t Size,
                                int64_t Expr, SMLoc Loc) {
  int64_t IntNumValues;
  // Do additional checking now if we can resolve the value.
  if (NumValues.evaluateAsAbsolute(IntNumValues, getAssemblerPtr())) {
    if (IntNumValues < 0) {
      getContext().getSourceManager()->PrintMessage(
          Loc, SourceMgr::DK_Warning,
          "'.fill' directive with negative repeat count has no effect");
      return;
    }
    // Emit now if we can for better errors.
    int64_t NonZeroSize = Size > 4 ? 4 : Size;
    Expr &= ~0ULL >> (64 - NonZeroSize * 8);
    for (uint64_t i = 0, e = IntNumValues; i != e; ++i) {
      emitIntValue(Expr, NonZeroSize);
      if (NonZeroSize < Size)
        emitIntValue(0, Size - NonZeroSize);
    }
    return;
  }

  // Otherwise emit as fragment.
  assert(getCurrentSectionOnly() && "need a section");
  insert(
      getContext().allocFragment<MCFillFragment>(Expr, Size, NumValues, Loc));
}

void MCObjectStreamer::emitNops(int64_t NumBytes, int64_t ControlledNopLength,
                                SMLoc Loc, const MCSubtargetInfo &STI) {
  assert(getCurrentSectionOnly() && "need a section");
  insert(getContext().allocFragment<MCNopsFragment>(
      NumBytes, ControlledNopLength, Loc, STI));
}

void MCObjectStreamer::emitFileDirective(StringRef Filename) {
  MCAssembler &Asm = getAssembler();
  Asm.getWriter().addFileName(Asm, Filename);
}

void MCObjectStreamer::emitFileDirective(StringRef Filename,
                                         StringRef CompilerVersion,
                                         StringRef TimeStamp,
                                         StringRef Description) {
  MCObjectWriter &W = getAssembler().getWriter();
  W.addFileName(getAssembler(), Filename);
  if (CompilerVersion.size())
    W.setCompilerVersion(CompilerVersion);
  // TODO: add TimeStamp and Description to .file symbol table entry
  // with the integrated assembler.
}

void MCObjectStreamer::emitAddrsig() {
  getAssembler().getWriter().emitAddrsigSection();
}

void MCObjectStreamer::emitAddrsigSym(const MCSymbol *Sym) {
  getAssembler().getWriter().addAddrsigSymbol(Sym);
}

void MCObjectStreamer::finishImpl() {
  getContext().RemapDebugPaths();

  // If we are generating dwarf for assembly source files dump out the sections.
  if (getContext().getGenDwarfForAssembly())
    MCGenDwarfInfo::Emit(this);

  // Dump out the dwarf file & directory tables and line tables.
  MCDwarfLineTable::emit(this, getAssembler().getDWARFLinetableParams());

  // Emit pseudo probes for the current module.
  MCPseudoProbeTable::emit(this);

  resolvePendingFixups();
  getAssembler().Finish();
}
