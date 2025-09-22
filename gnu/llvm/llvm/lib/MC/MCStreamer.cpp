//===- lib/MC/MCStreamer.cpp - Streaming Machine Code Output --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCStreamer.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeView.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCPseudoProbe.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSectionCOFF.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCWin64EH.h"
#include "llvm/MC/MCWinEH.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/LEB128.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <utility>

using namespace llvm;

MCTargetStreamer::MCTargetStreamer(MCStreamer &S) : Streamer(S) {
  S.setTargetStreamer(this);
}

// Pin the vtables to this file.
MCTargetStreamer::~MCTargetStreamer() = default;

void MCTargetStreamer::emitLabel(MCSymbol *Symbol) {}

void MCTargetStreamer::finish() {}

void MCTargetStreamer::emitConstantPools() {}

void MCTargetStreamer::changeSection(const MCSection *CurSection,
                                     MCSection *Section, uint32_t Subsection,
                                     raw_ostream &OS) {
  Section->printSwitchToSection(*Streamer.getContext().getAsmInfo(),
                                Streamer.getContext().getTargetTriple(), OS,
                                Subsection);
}

void MCTargetStreamer::emitDwarfFileDirective(StringRef Directive) {
  Streamer.emitRawText(Directive);
}

void MCTargetStreamer::emitValue(const MCExpr *Value) {
  SmallString<128> Str;
  raw_svector_ostream OS(Str);

  Value->print(OS, Streamer.getContext().getAsmInfo());
  Streamer.emitRawText(OS.str());
}

void MCTargetStreamer::emitRawBytes(StringRef Data) {
  const MCAsmInfo *MAI = Streamer.getContext().getAsmInfo();
  const char *Directive = MAI->getData8bitsDirective();
  for (const unsigned char C : Data.bytes()) {
    SmallString<128> Str;
    raw_svector_ostream OS(Str);

    OS << Directive << (unsigned)C;
    Streamer.emitRawText(OS.str());
  }
}

void MCTargetStreamer::emitAssignment(MCSymbol *Symbol, const MCExpr *Value) {}

MCStreamer::MCStreamer(MCContext &Ctx)
    : Context(Ctx), CurrentWinFrameInfo(nullptr),
      CurrentProcWinFrameInfoStartIndex(0) {
  SectionStack.push_back(std::pair<MCSectionSubPair, MCSectionSubPair>());
}

MCStreamer::~MCStreamer() = default;

void MCStreamer::reset() {
  DwarfFrameInfos.clear();
  CurrentWinFrameInfo = nullptr;
  WinFrameInfos.clear();
  SectionStack.clear();
  SectionStack.push_back(std::pair<MCSectionSubPair, MCSectionSubPair>());
  CurFrag = nullptr;
}

raw_ostream &MCStreamer::getCommentOS() {
  // By default, discard comments.
  return nulls();
}

unsigned MCStreamer::getNumFrameInfos() { return DwarfFrameInfos.size(); }
ArrayRef<MCDwarfFrameInfo> MCStreamer::getDwarfFrameInfos() const {
  return DwarfFrameInfos;
}

void MCStreamer::emitRawComment(const Twine &T, bool TabPrefix) {}

void MCStreamer::addExplicitComment(const Twine &T) {}
void MCStreamer::emitExplicitComments() {}

void MCStreamer::generateCompactUnwindEncodings(MCAsmBackend *MAB) {
  for (auto &FI : DwarfFrameInfos)
    FI.CompactUnwindEncoding =
        (MAB ? MAB->generateCompactUnwindEncoding(&FI, &Context) : 0);
}

/// EmitIntValue - Special case of EmitValue that avoids the client having to
/// pass in a MCExpr for constant integers.
void MCStreamer::emitIntValue(uint64_t Value, unsigned Size) {
  assert(1 <= Size && Size <= 8 && "Invalid size");
  assert((isUIntN(8 * Size, Value) || isIntN(8 * Size, Value)) &&
         "Invalid size");
  const bool IsLittleEndian = Context.getAsmInfo()->isLittleEndian();
  uint64_t Swapped = support::endian::byte_swap(
      Value, IsLittleEndian ? llvm::endianness::little : llvm::endianness::big);
  unsigned Index = IsLittleEndian ? 0 : 8 - Size;
  emitBytes(StringRef(reinterpret_cast<char *>(&Swapped) + Index, Size));
}
void MCStreamer::emitIntValue(const APInt &Value) {
  if (Value.getNumWords() == 1) {
    emitIntValue(Value.getLimitedValue(), Value.getBitWidth() / 8);
    return;
  }

  const bool IsLittleEndianTarget = Context.getAsmInfo()->isLittleEndian();
  const bool ShouldSwap = sys::IsLittleEndianHost != IsLittleEndianTarget;
  const APInt Swapped = ShouldSwap ? Value.byteSwap() : Value;
  const unsigned Size = Value.getBitWidth() / 8;
  SmallString<10> Tmp;
  Tmp.resize(Size);
  StoreIntToMemory(Swapped, reinterpret_cast<uint8_t *>(Tmp.data()), Size);
  emitBytes(Tmp.str());
}

/// EmitULEB128IntValue - Special case of EmitULEB128Value that avoids the
/// client having to pass in a MCExpr for constant integers.
unsigned MCStreamer::emitULEB128IntValue(uint64_t Value, unsigned PadTo) {
  SmallString<128> Tmp;
  raw_svector_ostream OSE(Tmp);
  encodeULEB128(Value, OSE, PadTo);
  emitBytes(OSE.str());
  return Tmp.size();
}

/// EmitSLEB128IntValue - Special case of EmitSLEB128Value that avoids the
/// client having to pass in a MCExpr for constant integers.
unsigned MCStreamer::emitSLEB128IntValue(int64_t Value) {
  SmallString<128> Tmp;
  raw_svector_ostream OSE(Tmp);
  encodeSLEB128(Value, OSE);
  emitBytes(OSE.str());
  return Tmp.size();
}

void MCStreamer::emitValue(const MCExpr *Value, unsigned Size, SMLoc Loc) {
  emitValueImpl(Value, Size, Loc);
}

void MCStreamer::emitSymbolValue(const MCSymbol *Sym, unsigned Size,
                                 bool IsSectionRelative) {
  assert((!IsSectionRelative || Size == 4) &&
         "SectionRelative value requires 4-bytes");

  if (!IsSectionRelative)
    emitValueImpl(MCSymbolRefExpr::create(Sym, getContext()), Size);
  else
    emitCOFFSecRel32(Sym, /*Offset=*/0);
}

void MCStreamer::emitDTPRel64Value(const MCExpr *Value) {
  report_fatal_error("unsupported directive in streamer");
}

void MCStreamer::emitDTPRel32Value(const MCExpr *Value) {
  report_fatal_error("unsupported directive in streamer");
}

void MCStreamer::emitTPRel64Value(const MCExpr *Value) {
  report_fatal_error("unsupported directive in streamer");
}

void MCStreamer::emitTPRel32Value(const MCExpr *Value) {
  report_fatal_error("unsupported directive in streamer");
}

void MCStreamer::emitGPRel64Value(const MCExpr *Value) {
  report_fatal_error("unsupported directive in streamer");
}

void MCStreamer::emitGPRel32Value(const MCExpr *Value) {
  report_fatal_error("unsupported directive in streamer");
}

/// Emit NumBytes bytes worth of the value specified by FillValue.
/// This implements directives such as '.space'.
void MCStreamer::emitFill(uint64_t NumBytes, uint8_t FillValue) {
  if (NumBytes)
    emitFill(*MCConstantExpr::create(NumBytes, getContext()), FillValue);
}

void llvm::MCStreamer::emitNops(int64_t NumBytes, int64_t ControlledNopLen,
                                llvm::SMLoc, const MCSubtargetInfo& STI) {}

/// The implementation in this class just redirects to emitFill.
void MCStreamer::emitZeros(uint64_t NumBytes) { emitFill(NumBytes, 0); }

Expected<unsigned> MCStreamer::tryEmitDwarfFileDirective(
    unsigned FileNo, StringRef Directory, StringRef Filename,
    std::optional<MD5::MD5Result> Checksum, std::optional<StringRef> Source,
    unsigned CUID) {
  return getContext().getDwarfFile(Directory, Filename, FileNo, Checksum,
                                   Source, CUID);
}

void MCStreamer::emitDwarfFile0Directive(StringRef Directory,
                                         StringRef Filename,
                                         std::optional<MD5::MD5Result> Checksum,
                                         std::optional<StringRef> Source,
                                         unsigned CUID) {
  getContext().setMCLineTableRootFile(CUID, Directory, Filename, Checksum,
                                      Source);
}

void MCStreamer::emitCFIBKeyFrame() {
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->IsBKeyFrame = true;
}

void MCStreamer::emitCFIMTETaggedFrame() {
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->IsMTETaggedFrame = true;
}

void MCStreamer::emitDwarfLocDirective(unsigned FileNo, unsigned Line,
                                       unsigned Column, unsigned Flags,
                                       unsigned Isa, unsigned Discriminator,
                                       StringRef FileName) {
  getContext().setCurrentDwarfLoc(FileNo, Line, Column, Flags, Isa,
                                  Discriminator);
}

MCSymbol *MCStreamer::getDwarfLineTableSymbol(unsigned CUID) {
  MCDwarfLineTable &Table = getContext().getMCDwarfLineTable(CUID);
  if (!Table.getLabel()) {
    StringRef Prefix = Context.getAsmInfo()->getPrivateGlobalPrefix();
    Table.setLabel(
        Context.getOrCreateSymbol(Prefix + "line_table_start" + Twine(CUID)));
  }
  return Table.getLabel();
}

bool MCStreamer::hasUnfinishedDwarfFrameInfo() {
  return !FrameInfoStack.empty();
}

MCDwarfFrameInfo *MCStreamer::getCurrentDwarfFrameInfo() {
  if (!hasUnfinishedDwarfFrameInfo()) {
    getContext().reportError(getStartTokLoc(),
                             "this directive must appear between "
                             ".cfi_startproc and .cfi_endproc directives");
    return nullptr;
  }
  return &DwarfFrameInfos[FrameInfoStack.back().first];
}

bool MCStreamer::emitCVFileDirective(unsigned FileNo, StringRef Filename,
                                     ArrayRef<uint8_t> Checksum,
                                     unsigned ChecksumKind) {
  return getContext().getCVContext().addFile(*this, FileNo, Filename, Checksum,
                                             ChecksumKind);
}

bool MCStreamer::emitCVFuncIdDirective(unsigned FunctionId) {
  return getContext().getCVContext().recordFunctionId(FunctionId);
}

bool MCStreamer::emitCVInlineSiteIdDirective(unsigned FunctionId,
                                             unsigned IAFunc, unsigned IAFile,
                                             unsigned IALine, unsigned IACol,
                                             SMLoc Loc) {
  if (getContext().getCVContext().getCVFunctionInfo(IAFunc) == nullptr) {
    getContext().reportError(Loc, "parent function id not introduced by "
                                  ".cv_func_id or .cv_inline_site_id");
    return true;
  }

  return getContext().getCVContext().recordInlinedCallSiteId(
      FunctionId, IAFunc, IAFile, IALine, IACol);
}

void MCStreamer::emitCVLocDirective(unsigned FunctionId, unsigned FileNo,
                                    unsigned Line, unsigned Column,
                                    bool PrologueEnd, bool IsStmt,
                                    StringRef FileName, SMLoc Loc) {}

bool MCStreamer::checkCVLocSection(unsigned FuncId, unsigned FileNo,
                                   SMLoc Loc) {
  CodeViewContext &CVC = getContext().getCVContext();
  MCCVFunctionInfo *FI = CVC.getCVFunctionInfo(FuncId);
  if (!FI) {
    getContext().reportError(
        Loc, "function id not introduced by .cv_func_id or .cv_inline_site_id");
    return false;
  }

  // Track the section
  if (FI->Section == nullptr)
    FI->Section = getCurrentSectionOnly();
  else if (FI->Section != getCurrentSectionOnly()) {
    getContext().reportError(
        Loc,
        "all .cv_loc directives for a function must be in the same section");
    return false;
  }
  return true;
}

void MCStreamer::emitCVLinetableDirective(unsigned FunctionId,
                                          const MCSymbol *Begin,
                                          const MCSymbol *End) {}

void MCStreamer::emitCVInlineLinetableDirective(unsigned PrimaryFunctionId,
                                                unsigned SourceFileId,
                                                unsigned SourceLineNum,
                                                const MCSymbol *FnStartSym,
                                                const MCSymbol *FnEndSym) {}

/// Only call this on endian-specific types like ulittle16_t and little32_t, or
/// structs composed of them.
template <typename T>
static void copyBytesForDefRange(SmallString<20> &BytePrefix,
                                 codeview::SymbolKind SymKind,
                                 const T &DefRangeHeader) {
  BytePrefix.resize(2 + sizeof(T));
  codeview::ulittle16_t SymKindLE = codeview::ulittle16_t(SymKind);
  memcpy(&BytePrefix[0], &SymKindLE, 2);
  memcpy(&BytePrefix[2], &DefRangeHeader, sizeof(T));
}

void MCStreamer::emitCVDefRangeDirective(
    ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
    StringRef FixedSizePortion) {}

void MCStreamer::emitCVDefRangeDirective(
    ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
    codeview::DefRangeRegisterRelHeader DRHdr) {
  SmallString<20> BytePrefix;
  copyBytesForDefRange(BytePrefix, codeview::S_DEFRANGE_REGISTER_REL, DRHdr);
  emitCVDefRangeDirective(Ranges, BytePrefix);
}

void MCStreamer::emitCVDefRangeDirective(
    ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
    codeview::DefRangeSubfieldRegisterHeader DRHdr) {
  SmallString<20> BytePrefix;
  copyBytesForDefRange(BytePrefix, codeview::S_DEFRANGE_SUBFIELD_REGISTER,
                       DRHdr);
  emitCVDefRangeDirective(Ranges, BytePrefix);
}

void MCStreamer::emitCVDefRangeDirective(
    ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
    codeview::DefRangeRegisterHeader DRHdr) {
  SmallString<20> BytePrefix;
  copyBytesForDefRange(BytePrefix, codeview::S_DEFRANGE_REGISTER, DRHdr);
  emitCVDefRangeDirective(Ranges, BytePrefix);
}

void MCStreamer::emitCVDefRangeDirective(
    ArrayRef<std::pair<const MCSymbol *, const MCSymbol *>> Ranges,
    codeview::DefRangeFramePointerRelHeader DRHdr) {
  SmallString<20> BytePrefix;
  copyBytesForDefRange(BytePrefix, codeview::S_DEFRANGE_FRAMEPOINTER_REL,
                       DRHdr);
  emitCVDefRangeDirective(Ranges, BytePrefix);
}

void MCStreamer::emitEHSymAttributes(const MCSymbol *Symbol,
                                     MCSymbol *EHSymbol) {
}

void MCStreamer::initSections(bool NoExecStack, const MCSubtargetInfo &STI) {
  switchSection(getContext().getObjectFileInfo()->getTextSection());
}

void MCStreamer::emitLabel(MCSymbol *Symbol, SMLoc Loc) {
  Symbol->redefineIfPossible();

  if (!Symbol->isUndefined() || Symbol->isVariable())
    return getContext().reportError(Loc, "symbol '" + Twine(Symbol->getName()) +
                                             "' is already defined");

  assert(!Symbol->isVariable() && "Cannot emit a variable symbol!");
  assert(getCurrentSectionOnly() && "Cannot emit before setting section!");
  assert(!Symbol->getFragment() && "Unexpected fragment on symbol data!");
  assert(Symbol->isUndefined() && "Cannot define a symbol twice!");

  Symbol->setFragment(&getCurrentSectionOnly()->getDummyFragment());

  MCTargetStreamer *TS = getTargetStreamer();
  if (TS)
    TS->emitLabel(Symbol);
}

void MCStreamer::emitConditionalAssignment(MCSymbol *Symbol,
                                           const MCExpr *Value) {}

void MCStreamer::emitCFISections(bool EH, bool Debug) {}

void MCStreamer::emitCFIStartProc(bool IsSimple, SMLoc Loc) {
  if (!FrameInfoStack.empty() &&
      getCurrentSectionOnly() == FrameInfoStack.back().second)
    return getContext().reportError(
        Loc, "starting new .cfi frame before finishing the previous one");

  MCDwarfFrameInfo Frame;
  Frame.IsSimple = IsSimple;
  emitCFIStartProcImpl(Frame);

  const MCAsmInfo* MAI = Context.getAsmInfo();
  if (MAI) {
    for (const MCCFIInstruction& Inst : MAI->getInitialFrameState()) {
      if (Inst.getOperation() == MCCFIInstruction::OpDefCfa ||
          Inst.getOperation() == MCCFIInstruction::OpDefCfaRegister ||
          Inst.getOperation() == MCCFIInstruction::OpLLVMDefAspaceCfa) {
        Frame.CurrentCfaRegister = Inst.getRegister();
      }
    }
  }

  FrameInfoStack.emplace_back(DwarfFrameInfos.size(), getCurrentSectionOnly());
  DwarfFrameInfos.push_back(Frame);
}

void MCStreamer::emitCFIStartProcImpl(MCDwarfFrameInfo &Frame) {
}

void MCStreamer::emitCFIEndProc() {
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  emitCFIEndProcImpl(*CurFrame);
  FrameInfoStack.pop_back();
}

void MCStreamer::emitCFIEndProcImpl(MCDwarfFrameInfo &Frame) {
  // Put a dummy non-null value in Frame.End to mark that this frame has been
  // closed.
  Frame.End = (MCSymbol *)1;
}

MCSymbol *MCStreamer::emitCFILabel() {
  // Return a dummy non-null value so that label fields appear filled in when
  // generating textual assembly.
  return (MCSymbol *)1;
}

void MCStreamer::emitCFIDefCfa(int64_t Register, int64_t Offset, SMLoc Loc) {
  MCSymbol *Label = emitCFILabel();
  MCCFIInstruction Instruction =
      MCCFIInstruction::cfiDefCfa(Label, Register, Offset, Loc);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
  CurFrame->CurrentCfaRegister = static_cast<unsigned>(Register);
}

void MCStreamer::emitCFIDefCfaOffset(int64_t Offset, SMLoc Loc) {
  MCSymbol *Label = emitCFILabel();
  MCCFIInstruction Instruction =
      MCCFIInstruction::cfiDefCfaOffset(Label, Offset);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::emitCFIAdjustCfaOffset(int64_t Adjustment, SMLoc Loc) {
  MCSymbol *Label = emitCFILabel();
  MCCFIInstruction Instruction =
      MCCFIInstruction::createAdjustCfaOffset(Label, Adjustment, Loc);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::emitCFIDefCfaRegister(int64_t Register, SMLoc Loc) {
  MCSymbol *Label = emitCFILabel();
  MCCFIInstruction Instruction =
      MCCFIInstruction::createDefCfaRegister(Label, Register, Loc);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
  CurFrame->CurrentCfaRegister = static_cast<unsigned>(Register);
}

void MCStreamer::emitCFILLVMDefAspaceCfa(int64_t Register, int64_t Offset,
                                         int64_t AddressSpace, SMLoc Loc) {
  MCSymbol *Label = emitCFILabel();
  MCCFIInstruction Instruction = MCCFIInstruction::createLLVMDefAspaceCfa(
      Label, Register, Offset, AddressSpace, Loc);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
  CurFrame->CurrentCfaRegister = static_cast<unsigned>(Register);
}

void MCStreamer::emitCFIOffset(int64_t Register, int64_t Offset, SMLoc Loc) {
  MCSymbol *Label = emitCFILabel();
  MCCFIInstruction Instruction =
      MCCFIInstruction::createOffset(Label, Register, Offset, Loc);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::emitCFIRelOffset(int64_t Register, int64_t Offset, SMLoc Loc) {
  MCSymbol *Label = emitCFILabel();
  MCCFIInstruction Instruction =
      MCCFIInstruction::createRelOffset(Label, Register, Offset, Loc);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::emitCFIPersonality(const MCSymbol *Sym,
                                    unsigned Encoding) {
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Personality = Sym;
  CurFrame->PersonalityEncoding = Encoding;
}

void MCStreamer::emitCFILsda(const MCSymbol *Sym, unsigned Encoding) {
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Lsda = Sym;
  CurFrame->LsdaEncoding = Encoding;
}

void MCStreamer::emitCFIRememberState(SMLoc Loc) {
  MCSymbol *Label = emitCFILabel();
  MCCFIInstruction Instruction =
      MCCFIInstruction::createRememberState(Label, Loc);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::emitCFIRestoreState(SMLoc Loc) {
  // FIXME: Error if there is no matching cfi_remember_state.
  MCSymbol *Label = emitCFILabel();
  MCCFIInstruction Instruction =
      MCCFIInstruction::createRestoreState(Label, Loc);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::emitCFISameValue(int64_t Register, SMLoc Loc) {
  MCSymbol *Label = emitCFILabel();
  MCCFIInstruction Instruction =
      MCCFIInstruction::createSameValue(Label, Register, Loc);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::emitCFIRestore(int64_t Register, SMLoc Loc) {
  MCSymbol *Label = emitCFILabel();
  MCCFIInstruction Instruction =
      MCCFIInstruction::createRestore(Label, Register, Loc);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::emitCFIEscape(StringRef Values, SMLoc Loc) {
  MCSymbol *Label = emitCFILabel();
  MCCFIInstruction Instruction =
      MCCFIInstruction::createEscape(Label, Values, Loc, "");
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::emitCFIGnuArgsSize(int64_t Size, SMLoc Loc) {
  MCSymbol *Label = emitCFILabel();
  MCCFIInstruction Instruction =
      MCCFIInstruction::createGnuArgsSize(Label, Size, Loc);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::emitCFISignalFrame() {
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->IsSignalFrame = true;
}

void MCStreamer::emitCFIUndefined(int64_t Register, SMLoc Loc) {
  MCSymbol *Label = emitCFILabel();
  MCCFIInstruction Instruction =
      MCCFIInstruction::createUndefined(Label, Register, Loc);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::emitCFIRegister(int64_t Register1, int64_t Register2,
                                 SMLoc Loc) {
  MCSymbol *Label = emitCFILabel();
  MCCFIInstruction Instruction =
      MCCFIInstruction::createRegister(Label, Register1, Register2, Loc);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::emitCFIWindowSave(SMLoc Loc) {
  MCSymbol *Label = emitCFILabel();
  MCCFIInstruction Instruction = MCCFIInstruction::createWindowSave(Label, Loc);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::emitCFINegateRAState(SMLoc Loc) {
  MCSymbol *Label = emitCFILabel();
  MCCFIInstruction Instruction =
      MCCFIInstruction::createNegateRAState(Label, Loc);
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->Instructions.push_back(Instruction);
}

void MCStreamer::emitCFIReturnColumn(int64_t Register) {
  MCDwarfFrameInfo *CurFrame = getCurrentDwarfFrameInfo();
  if (!CurFrame)
    return;
  CurFrame->RAReg = Register;
}

void MCStreamer::emitCFILabelDirective(SMLoc Loc, StringRef Name) {
  MCSymbol *Label = emitCFILabel();
  MCSymbol *Sym = getContext().getOrCreateSymbol(Name);
  if (MCDwarfFrameInfo *F = getCurrentDwarfFrameInfo())
    F->Instructions.push_back(MCCFIInstruction::createLabel(Label, Sym, Loc));
}

WinEH::FrameInfo *MCStreamer::EnsureValidWinFrameInfo(SMLoc Loc) {
  const MCAsmInfo *MAI = Context.getAsmInfo();
  if (!MAI->usesWindowsCFI()) {
    getContext().reportError(
        Loc, ".seh_* directives are not supported on this target");
    return nullptr;
  }
  if (!CurrentWinFrameInfo || CurrentWinFrameInfo->End) {
    getContext().reportError(
        Loc, ".seh_ directive must appear within an active frame");
    return nullptr;
  }
  return CurrentWinFrameInfo;
}

void MCStreamer::emitWinCFIStartProc(const MCSymbol *Symbol, SMLoc Loc) {
  const MCAsmInfo *MAI = Context.getAsmInfo();
  if (!MAI->usesWindowsCFI())
    return getContext().reportError(
        Loc, ".seh_* directives are not supported on this target");
  if (CurrentWinFrameInfo && !CurrentWinFrameInfo->End)
    getContext().reportError(
        Loc, "Starting a function before ending the previous one!");

  MCSymbol *StartProc = emitCFILabel();

  CurrentProcWinFrameInfoStartIndex = WinFrameInfos.size();
  WinFrameInfos.emplace_back(
      std::make_unique<WinEH::FrameInfo>(Symbol, StartProc));
  CurrentWinFrameInfo = WinFrameInfos.back().get();
  CurrentWinFrameInfo->TextSection = getCurrentSectionOnly();
}

void MCStreamer::emitWinCFIEndProc(SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;
  if (CurFrame->ChainedParent)
    getContext().reportError(Loc, "Not all chained regions terminated!");

  MCSymbol *Label = emitCFILabel();
  CurFrame->End = Label;
  if (!CurFrame->FuncletOrFuncEnd)
    CurFrame->FuncletOrFuncEnd = CurFrame->End;

  for (size_t I = CurrentProcWinFrameInfoStartIndex, E = WinFrameInfos.size();
       I != E; ++I)
    emitWindowsUnwindTables(WinFrameInfos[I].get());
  switchSection(CurFrame->TextSection);
}

void MCStreamer::emitWinCFIFuncletOrFuncEnd(SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;
  if (CurFrame->ChainedParent)
    getContext().reportError(Loc, "Not all chained regions terminated!");

  MCSymbol *Label = emitCFILabel();
  CurFrame->FuncletOrFuncEnd = Label;
}

void MCStreamer::emitWinCFIStartChained(SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;

  MCSymbol *StartProc = emitCFILabel();

  WinFrameInfos.emplace_back(std::make_unique<WinEH::FrameInfo>(
      CurFrame->Function, StartProc, CurFrame));
  CurrentWinFrameInfo = WinFrameInfos.back().get();
  CurrentWinFrameInfo->TextSection = getCurrentSectionOnly();
}

void MCStreamer::emitWinCFIEndChained(SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;
  if (!CurFrame->ChainedParent)
    return getContext().reportError(
        Loc, "End of a chained region outside a chained region!");

  MCSymbol *Label = emitCFILabel();

  CurFrame->End = Label;
  CurrentWinFrameInfo = const_cast<WinEH::FrameInfo *>(CurFrame->ChainedParent);
}

void MCStreamer::emitWinEHHandler(const MCSymbol *Sym, bool Unwind, bool Except,
                                  SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;
  if (CurFrame->ChainedParent)
    return getContext().reportError(
        Loc, "Chained unwind areas can't have handlers!");
  CurFrame->ExceptionHandler = Sym;
  if (!Except && !Unwind)
    getContext().reportError(Loc, "Don't know what kind of handler this is!");
  if (Unwind)
    CurFrame->HandlesUnwind = true;
  if (Except)
    CurFrame->HandlesExceptions = true;
}

void MCStreamer::emitWinEHHandlerData(SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;
  if (CurFrame->ChainedParent)
    getContext().reportError(Loc, "Chained unwind areas can't have handlers!");
}

void MCStreamer::emitCGProfileEntry(const MCSymbolRefExpr *From,
                                    const MCSymbolRefExpr *To, uint64_t Count) {
}

static MCSection *getWinCFISection(MCContext &Context, unsigned *NextWinCFIID,
                                   MCSection *MainCFISec,
                                   const MCSection *TextSec) {
  // If this is the main .text section, use the main unwind info section.
  if (TextSec == Context.getObjectFileInfo()->getTextSection())
    return MainCFISec;

  const auto *TextSecCOFF = cast<MCSectionCOFF>(TextSec);
  auto *MainCFISecCOFF = cast<MCSectionCOFF>(MainCFISec);
  unsigned UniqueID = TextSecCOFF->getOrAssignWinCFISectionID(NextWinCFIID);

  // If this section is COMDAT, this unwind section should be COMDAT associative
  // with its group.
  const MCSymbol *KeySym = nullptr;
  if (TextSecCOFF->getCharacteristics() & COFF::IMAGE_SCN_LNK_COMDAT) {
    KeySym = TextSecCOFF->getCOMDATSymbol();

    // In a GNU environment, we can't use associative comdats. Instead, do what
    // GCC does, which is to make plain comdat selectany section named like
    // ".[px]data$_Z3foov".
    if (!Context.getAsmInfo()->hasCOFFAssociativeComdats()) {
      std::string SectionName = (MainCFISecCOFF->getName() + "$" +
                                 TextSecCOFF->getName().split('$').second)
                                    .str();
      return Context.getCOFFSection(SectionName,
                                    MainCFISecCOFF->getCharacteristics() |
                                        COFF::IMAGE_SCN_LNK_COMDAT,
                                    "", COFF::IMAGE_COMDAT_SELECT_ANY);
    }
  }

  return Context.getAssociativeCOFFSection(MainCFISecCOFF, KeySym, UniqueID);
}

MCSection *MCStreamer::getAssociatedPDataSection(const MCSection *TextSec) {
  return getWinCFISection(getContext(), &NextWinCFIID,
                          getContext().getObjectFileInfo()->getPDataSection(),
                          TextSec);
}

MCSection *MCStreamer::getAssociatedXDataSection(const MCSection *TextSec) {
  return getWinCFISection(getContext(), &NextWinCFIID,
                          getContext().getObjectFileInfo()->getXDataSection(),
                          TextSec);
}

void MCStreamer::emitSyntaxDirective() {}

static unsigned encodeSEHRegNum(MCContext &Ctx, MCRegister Reg) {
  return Ctx.getRegisterInfo()->getSEHRegNum(Reg);
}

void MCStreamer::emitWinCFIPushReg(MCRegister Register, SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;

  MCSymbol *Label = emitCFILabel();

  WinEH::Instruction Inst = Win64EH::Instruction::PushNonVol(
      Label, encodeSEHRegNum(Context, Register));
  CurFrame->Instructions.push_back(Inst);
}

void MCStreamer::emitWinCFISetFrame(MCRegister Register, unsigned Offset,
                                    SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;
  if (CurFrame->LastFrameInst >= 0)
    return getContext().reportError(
        Loc, "frame register and offset can be set at most once");
  if (Offset & 0x0F)
    return getContext().reportError(Loc, "offset is not a multiple of 16");
  if (Offset > 240)
    return getContext().reportError(
        Loc, "frame offset must be less than or equal to 240");

  MCSymbol *Label = emitCFILabel();

  WinEH::Instruction Inst = Win64EH::Instruction::SetFPReg(
      Label, encodeSEHRegNum(getContext(), Register), Offset);
  CurFrame->LastFrameInst = CurFrame->Instructions.size();
  CurFrame->Instructions.push_back(Inst);
}

void MCStreamer::emitWinCFIAllocStack(unsigned Size, SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;
  if (Size == 0)
    return getContext().reportError(Loc,
                                    "stack allocation size must be non-zero");
  if (Size & 7)
    return getContext().reportError(
        Loc, "stack allocation size is not a multiple of 8");

  MCSymbol *Label = emitCFILabel();

  WinEH::Instruction Inst = Win64EH::Instruction::Alloc(Label, Size);
  CurFrame->Instructions.push_back(Inst);
}

void MCStreamer::emitWinCFISaveReg(MCRegister Register, unsigned Offset,
                                   SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;

  if (Offset & 7)
    return getContext().reportError(
        Loc, "register save offset is not 8 byte aligned");

  MCSymbol *Label = emitCFILabel();

  WinEH::Instruction Inst = Win64EH::Instruction::SaveNonVol(
      Label, encodeSEHRegNum(Context, Register), Offset);
  CurFrame->Instructions.push_back(Inst);
}

void MCStreamer::emitWinCFISaveXMM(MCRegister Register, unsigned Offset,
                                   SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;
  if (Offset & 0x0F)
    return getContext().reportError(Loc, "offset is not a multiple of 16");

  MCSymbol *Label = emitCFILabel();

  WinEH::Instruction Inst = Win64EH::Instruction::SaveXMM(
      Label, encodeSEHRegNum(Context, Register), Offset);
  CurFrame->Instructions.push_back(Inst);
}

void MCStreamer::emitWinCFIPushFrame(bool Code, SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;
  if (!CurFrame->Instructions.empty())
    return getContext().reportError(
        Loc, "If present, PushMachFrame must be the first UOP");

  MCSymbol *Label = emitCFILabel();

  WinEH::Instruction Inst = Win64EH::Instruction::PushMachFrame(Label, Code);
  CurFrame->Instructions.push_back(Inst);
}

void MCStreamer::emitWinCFIEndProlog(SMLoc Loc) {
  WinEH::FrameInfo *CurFrame = EnsureValidWinFrameInfo(Loc);
  if (!CurFrame)
    return;

  MCSymbol *Label = emitCFILabel();

  CurFrame->PrologEnd = Label;
}

void MCStreamer::emitCOFFSafeSEH(MCSymbol const *Symbol) {}

void MCStreamer::emitCOFFSymbolIndex(MCSymbol const *Symbol) {}

void MCStreamer::emitCOFFSectionIndex(MCSymbol const *Symbol) {}

void MCStreamer::emitCOFFSecRel32(MCSymbol const *Symbol, uint64_t Offset) {}

void MCStreamer::emitCOFFImgRel32(MCSymbol const *Symbol, int64_t Offset) {}

/// EmitRawText - If this file is backed by an assembly streamer, this dumps
/// the specified string in the output .s file.  This capability is
/// indicated by the hasRawTextSupport() predicate.
void MCStreamer::emitRawTextImpl(StringRef String) {
  // This is not llvm_unreachable for the sake of out of tree backend
  // developers who may not have assembly streamers and should serve as a
  // reminder to not accidentally call EmitRawText in the absence of such.
  report_fatal_error("EmitRawText called on an MCStreamer that doesn't support "
                     "it (target backend is likely missing an AsmStreamer "
                     "implementation)");
}

void MCStreamer::emitRawText(const Twine &T) {
  SmallString<128> Str;
  emitRawTextImpl(T.toStringRef(Str));
}

void MCStreamer::emitWindowsUnwindTables() {}

void MCStreamer::emitWindowsUnwindTables(WinEH::FrameInfo *Frame) {}

void MCStreamer::finish(SMLoc EndLoc) {
  if ((!DwarfFrameInfos.empty() && !DwarfFrameInfos.back().End) ||
      (!WinFrameInfos.empty() && !WinFrameInfos.back()->End)) {
    getContext().reportError(EndLoc, "Unfinished frame!");
    return;
  }

  MCTargetStreamer *TS = getTargetStreamer();
  if (TS)
    TS->finish();

  finishImpl();
}

void MCStreamer::maybeEmitDwarf64Mark() {
  if (Context.getDwarfFormat() != dwarf::DWARF64)
    return;
  AddComment("DWARF64 Mark");
  emitInt32(dwarf::DW_LENGTH_DWARF64);
}

void MCStreamer::emitDwarfUnitLength(uint64_t Length, const Twine &Comment) {
  assert(Context.getDwarfFormat() == dwarf::DWARF64 ||
         Length <= dwarf::DW_LENGTH_lo_reserved);
  maybeEmitDwarf64Mark();
  AddComment(Comment);
  emitIntValue(Length, dwarf::getDwarfOffsetByteSize(Context.getDwarfFormat()));
}

MCSymbol *MCStreamer::emitDwarfUnitLength(const Twine &Prefix,
                                          const Twine &Comment) {
  maybeEmitDwarf64Mark();
  AddComment(Comment);
  MCSymbol *Lo = Context.createTempSymbol(Prefix + "_start");
  MCSymbol *Hi = Context.createTempSymbol(Prefix + "_end");

  emitAbsoluteSymbolDiff(
      Hi, Lo, dwarf::getDwarfOffsetByteSize(Context.getDwarfFormat()));
  // emit the begin symbol after we generate the length field.
  emitLabel(Lo);
  // Return the Hi symbol to the caller.
  return Hi;
}

void MCStreamer::emitDwarfLineStartLabel(MCSymbol *StartSym) {
  // Set the value of the symbol, as we are at the start of the line table.
  emitLabel(StartSym);
}

void MCStreamer::emitAssignment(MCSymbol *Symbol, const MCExpr *Value) {
  visitUsedExpr(*Value);
  Symbol->setVariableValue(Value);

  MCTargetStreamer *TS = getTargetStreamer();
  if (TS)
    TS->emitAssignment(Symbol, Value);
}

void MCTargetStreamer::prettyPrintAsm(MCInstPrinter &InstPrinter,
                                      uint64_t Address, const MCInst &Inst,
                                      const MCSubtargetInfo &STI,
                                      raw_ostream &OS) {
  InstPrinter.printInst(&Inst, Address, "", STI, OS);
}

void MCStreamer::visitUsedSymbol(const MCSymbol &Sym) {
}

void MCStreamer::visitUsedExpr(const MCExpr &Expr) {
  switch (Expr.getKind()) {
  case MCExpr::Target:
    cast<MCTargetExpr>(Expr).visitUsedExpr(*this);
    break;

  case MCExpr::Constant:
    break;

  case MCExpr::Binary: {
    const MCBinaryExpr &BE = cast<MCBinaryExpr>(Expr);
    visitUsedExpr(*BE.getLHS());
    visitUsedExpr(*BE.getRHS());
    break;
  }

  case MCExpr::SymbolRef:
    visitUsedSymbol(cast<MCSymbolRefExpr>(Expr).getSymbol());
    break;

  case MCExpr::Unary:
    visitUsedExpr(*cast<MCUnaryExpr>(Expr).getSubExpr());
    break;
  }
}

void MCStreamer::emitInstruction(const MCInst &Inst, const MCSubtargetInfo &) {
  // Scan for values.
  for (unsigned i = Inst.getNumOperands(); i--;)
    if (Inst.getOperand(i).isExpr())
      visitUsedExpr(*Inst.getOperand(i).getExpr());
}

void MCStreamer::emitPseudoProbe(uint64_t Guid, uint64_t Index, uint64_t Type,
                                 uint64_t Attr, uint64_t Discriminator,
                                 const MCPseudoProbeInlineStack &InlineStack,
                                 MCSymbol *FnSym) {
  auto &Context = getContext();

  // Create a symbol at in the current section for use in the probe.
  MCSymbol *ProbeSym = Context.createTempSymbol();

  // Set the value of the symbol to use for the MCPseudoProbe.
  emitLabel(ProbeSym);

  // Create a (local) probe entry with the symbol.
  MCPseudoProbe Probe(ProbeSym, Guid, Index, Type, Attr, Discriminator);

  // Add the probe entry to this section's entries.
  Context.getMCPseudoProbeTable().getProbeSections().addPseudoProbe(
      FnSym, Probe, InlineStack);
}

void MCStreamer::emitAbsoluteSymbolDiff(const MCSymbol *Hi, const MCSymbol *Lo,
                                        unsigned Size) {
  // Get the Hi-Lo expression.
  const MCExpr *Diff =
      MCBinaryExpr::createSub(MCSymbolRefExpr::create(Hi, Context),
                              MCSymbolRefExpr::create(Lo, Context), Context);

  const MCAsmInfo *MAI = Context.getAsmInfo();
  if (!MAI->doesSetDirectiveSuppressReloc()) {
    emitValue(Diff, Size);
    return;
  }

  // Otherwise, emit with .set (aka assignment).
  MCSymbol *SetLabel = Context.createTempSymbol("set");
  emitAssignment(SetLabel, Diff);
  emitSymbolValue(SetLabel, Size);
}

void MCStreamer::emitAbsoluteSymbolDiffAsULEB128(const MCSymbol *Hi,
                                                 const MCSymbol *Lo) {
  // Get the Hi-Lo expression.
  const MCExpr *Diff =
      MCBinaryExpr::createSub(MCSymbolRefExpr::create(Hi, Context),
                              MCSymbolRefExpr::create(Lo, Context), Context);

  emitULEB128Value(Diff);
}

void MCStreamer::emitAssemblerFlag(MCAssemblerFlag Flag) {}
void MCStreamer::emitThumbFunc(MCSymbol *Func) {}
void MCStreamer::emitSymbolDesc(MCSymbol *Symbol, unsigned DescValue) {}
void MCStreamer::beginCOFFSymbolDef(const MCSymbol *Symbol) {
  llvm_unreachable("this directive only supported on COFF targets");
}
void MCStreamer::endCOFFSymbolDef() {
  llvm_unreachable("this directive only supported on COFF targets");
}
void MCStreamer::emitFileDirective(StringRef Filename) {}
void MCStreamer::emitFileDirective(StringRef Filename,
                                   StringRef CompilerVersion,
                                   StringRef TimeStamp, StringRef Description) {
}
void MCStreamer::emitCOFFSymbolStorageClass(int StorageClass) {
  llvm_unreachable("this directive only supported on COFF targets");
}
void MCStreamer::emitCOFFSymbolType(int Type) {
  llvm_unreachable("this directive only supported on COFF targets");
}
void MCStreamer::emitXCOFFLocalCommonSymbol(MCSymbol *LabelSym, uint64_t Size,
                                            MCSymbol *CsectSym,
                                            Align Alignment) {
  llvm_unreachable("this directive only supported on XCOFF targets");
}

void MCStreamer::emitXCOFFSymbolLinkageWithVisibility(MCSymbol *Symbol,
                                                      MCSymbolAttr Linkage,
                                                      MCSymbolAttr Visibility) {
  llvm_unreachable("emitXCOFFSymbolLinkageWithVisibility is only supported on "
                   "XCOFF targets");
}

void MCStreamer::emitXCOFFRenameDirective(const MCSymbol *Name,
                                          StringRef Rename) {}

void MCStreamer::emitXCOFFRefDirective(const MCSymbol *Symbol) {
  llvm_unreachable("emitXCOFFRefDirective is only supported on XCOFF targets");
}

void MCStreamer::emitXCOFFExceptDirective(const MCSymbol *Symbol,
                                          const MCSymbol *Trap,
                                          unsigned Lang, unsigned Reason,
                                          unsigned FunctionSize,
                                          bool hasDebug) {
  report_fatal_error("emitXCOFFExceptDirective is only supported on "
                     "XCOFF targets");
}

void MCStreamer::emitXCOFFCInfoSym(StringRef Name, StringRef Metadata) {
  llvm_unreachable("emitXCOFFCInfoSym is only supported on"
                   "XCOFF targets");
}

void MCStreamer::emitELFSize(MCSymbol *Symbol, const MCExpr *Value) {}
void MCStreamer::emitELFSymverDirective(const MCSymbol *OriginalSym,
                                        StringRef Name, bool KeepOriginalSym) {}
void MCStreamer::emitLocalCommonSymbol(MCSymbol *Symbol, uint64_t Size,
                                       Align ByteAlignment) {}
void MCStreamer::emitTBSSSymbol(MCSection *Section, MCSymbol *Symbol,
                                uint64_t Size, Align ByteAlignment) {}
void MCStreamer::changeSection(MCSection *Section, uint32_t) {
  CurFrag = &Section->getDummyFragment();
}
void MCStreamer::emitWeakReference(MCSymbol *Alias, const MCSymbol *Symbol) {}
void MCStreamer::emitBytes(StringRef Data) {}
void MCStreamer::emitBinaryData(StringRef Data) { emitBytes(Data); }
void MCStreamer::emitValueImpl(const MCExpr *Value, unsigned Size, SMLoc Loc) {
  visitUsedExpr(*Value);
}
void MCStreamer::emitULEB128Value(const MCExpr *Value) {}
void MCStreamer::emitSLEB128Value(const MCExpr *Value) {}
void MCStreamer::emitFill(const MCExpr &NumBytes, uint64_t Value, SMLoc Loc) {}
void MCStreamer::emitFill(const MCExpr &NumValues, int64_t Size, int64_t Expr,
                          SMLoc Loc) {}
void MCStreamer::emitValueToAlignment(Align Alignment, int64_t Value,
                                      unsigned ValueSize,
                                      unsigned MaxBytesToEmit) {}
void MCStreamer::emitCodeAlignment(Align Alignment, const MCSubtargetInfo *STI,
                                   unsigned MaxBytesToEmit) {}
void MCStreamer::emitValueToOffset(const MCExpr *Offset, unsigned char Value,
                                   SMLoc Loc) {}
void MCStreamer::emitBundleAlignMode(Align Alignment) {}
void MCStreamer::emitBundleLock(bool AlignToEnd) {}
void MCStreamer::finishImpl() {}
void MCStreamer::emitBundleUnlock() {}

bool MCStreamer::popSection() {
  if (SectionStack.size() <= 1)
    return false;
  auto I = SectionStack.end();
  --I;
  MCSectionSubPair OldSec = I->first;
  --I;
  MCSectionSubPair NewSec = I->first;

  if (NewSec.first && OldSec != NewSec)
    changeSection(NewSec.first, NewSec.second);
  SectionStack.pop_back();
  return true;
}

void MCStreamer::switchSection(MCSection *Section, uint32_t Subsection) {
  assert(Section && "Cannot switch to a null section!");
  MCSectionSubPair curSection = SectionStack.back().first;
  SectionStack.back().second = curSection;
  if (MCSectionSubPair(Section, Subsection) != curSection) {
    changeSection(Section, Subsection);
    SectionStack.back().first = MCSectionSubPair(Section, Subsection);
    assert(!Section->hasEnded() && "Section already ended");
    MCSymbol *Sym = Section->getBeginSymbol();
    if (Sym && !Sym->isInSection())
      emitLabel(Sym);
  }
}

bool MCStreamer::switchSection(MCSection *Section, const MCExpr *SubsecExpr) {
  int64_t Subsec = 0;
  if (SubsecExpr) {
    if (!SubsecExpr->evaluateAsAbsolute(Subsec, getAssemblerPtr())) {
      getContext().reportError(SubsecExpr->getLoc(),
                               "cannot evaluate subsection number");
      return true;
    }
    if (!isUInt<31>(Subsec)) {
      getContext().reportError(SubsecExpr->getLoc(),
                               "subsection number " + Twine(Subsec) +
                                   " is not within [0,2147483647]");
      return true;
    }
  }
  switchSection(Section, Subsec);
  return false;
}

void MCStreamer::switchSectionNoPrint(MCSection *Section) {
  SectionStack.back().second = SectionStack.back().first;
  SectionStack.back().first = MCSectionSubPair(Section, 0);
  CurFrag = &Section->getDummyFragment();
}

MCSymbol *MCStreamer::endSection(MCSection *Section) {
  // TODO: keep track of the last subsection so that this symbol appears in the
  // correct place.
  MCSymbol *Sym = Section->getEndSymbol(Context);
  if (Sym->isInSection())
    return Sym;

  switchSection(Section);
  emitLabel(Sym);
  return Sym;
}

static VersionTuple
targetVersionOrMinimumSupportedOSVersion(const Triple &Target,
                                         VersionTuple TargetVersion) {
  VersionTuple Min = Target.getMinimumSupportedOSVersion();
  return !Min.empty() && Min > TargetVersion ? Min : TargetVersion;
}

static MCVersionMinType
getMachoVersionMinLoadCommandType(const Triple &Target) {
  assert(Target.isOSDarwin() && "expected a darwin OS");
  switch (Target.getOS()) {
  case Triple::MacOSX:
  case Triple::Darwin:
    return MCVM_OSXVersionMin;
  case Triple::IOS:
    assert(!Target.isMacCatalystEnvironment() &&
           "mac Catalyst should use LC_BUILD_VERSION");
    return MCVM_IOSVersionMin;
  case Triple::TvOS:
    return MCVM_TvOSVersionMin;
  case Triple::WatchOS:
    return MCVM_WatchOSVersionMin;
  default:
    break;
  }
  llvm_unreachable("unexpected OS type");
}

static VersionTuple getMachoBuildVersionSupportedOS(const Triple &Target) {
  assert(Target.isOSDarwin() && "expected a darwin OS");
  switch (Target.getOS()) {
  case Triple::MacOSX:
  case Triple::Darwin:
    return VersionTuple(10, 14);
  case Triple::IOS:
    // Mac Catalyst always uses the build version load command.
    if (Target.isMacCatalystEnvironment())
      return VersionTuple();
    [[fallthrough]];
  case Triple::TvOS:
    return VersionTuple(12);
  case Triple::WatchOS:
    return VersionTuple(5);
  case Triple::DriverKit:
    // DriverKit always uses the build version load command.
    return VersionTuple();
  case Triple::XROS:
    // XROS always uses the build version load command.
    return VersionTuple();
  default:
    break;
  }
  llvm_unreachable("unexpected OS type");
}

static MachO::PlatformType
getMachoBuildVersionPlatformType(const Triple &Target) {
  assert(Target.isOSDarwin() && "expected a darwin OS");
  switch (Target.getOS()) {
  case Triple::MacOSX:
  case Triple::Darwin:
    return MachO::PLATFORM_MACOS;
  case Triple::IOS:
    if (Target.isMacCatalystEnvironment())
      return MachO::PLATFORM_MACCATALYST;
    return Target.isSimulatorEnvironment() ? MachO::PLATFORM_IOSSIMULATOR
                                           : MachO::PLATFORM_IOS;
  case Triple::TvOS:
    return Target.isSimulatorEnvironment() ? MachO::PLATFORM_TVOSSIMULATOR
                                           : MachO::PLATFORM_TVOS;
  case Triple::WatchOS:
    return Target.isSimulatorEnvironment() ? MachO::PLATFORM_WATCHOSSIMULATOR
                                           : MachO::PLATFORM_WATCHOS;
  case Triple::DriverKit:
    return MachO::PLATFORM_DRIVERKIT;
  case Triple::XROS:
    return Target.isSimulatorEnvironment() ? MachO::PLATFORM_XROS_SIMULATOR
                                           : MachO::PLATFORM_XROS;
  default:
    break;
  }
  llvm_unreachable("unexpected OS type");
}

void MCStreamer::emitVersionForTarget(
    const Triple &Target, const VersionTuple &SDKVersion,
    const Triple *DarwinTargetVariantTriple,
    const VersionTuple &DarwinTargetVariantSDKVersion) {
  if (!Target.isOSBinFormatMachO() || !Target.isOSDarwin())
    return;
  // Do we even know the version?
  if (Target.getOSMajorVersion() == 0)
    return;

  VersionTuple Version;
  switch (Target.getOS()) {
  case Triple::MacOSX:
  case Triple::Darwin:
    Target.getMacOSXVersion(Version);
    break;
  case Triple::IOS:
  case Triple::TvOS:
    Version = Target.getiOSVersion();
    break;
  case Triple::WatchOS:
    Version = Target.getWatchOSVersion();
    break;
  case Triple::DriverKit:
    Version = Target.getDriverKitVersion();
    break;
  case Triple::XROS:
    Version = Target.getOSVersion();
    break;
  default:
    llvm_unreachable("unexpected OS type");
  }
  assert(Version.getMajor() != 0 && "A non-zero major version is expected");
  auto LinkedTargetVersion =
      targetVersionOrMinimumSupportedOSVersion(Target, Version);
  auto BuildVersionOSVersion = getMachoBuildVersionSupportedOS(Target);
  bool ShouldEmitBuildVersion = false;
  if (BuildVersionOSVersion.empty() ||
      LinkedTargetVersion >= BuildVersionOSVersion) {
    if (Target.isMacCatalystEnvironment() && DarwinTargetVariantTriple &&
        DarwinTargetVariantTriple->isMacOSX()) {
      emitVersionForTarget(*DarwinTargetVariantTriple,
                           DarwinTargetVariantSDKVersion,
                           /*DarwinTargetVariantTriple=*/nullptr,
                           /*DarwinTargetVariantSDKVersion=*/VersionTuple());
      emitDarwinTargetVariantBuildVersion(
          getMachoBuildVersionPlatformType(Target),
          LinkedTargetVersion.getMajor(),
          LinkedTargetVersion.getMinor().value_or(0),
          LinkedTargetVersion.getSubminor().value_or(0), SDKVersion);
      return;
    }
    emitBuildVersion(getMachoBuildVersionPlatformType(Target),
                     LinkedTargetVersion.getMajor(),
                     LinkedTargetVersion.getMinor().value_or(0),
                     LinkedTargetVersion.getSubminor().value_or(0), SDKVersion);
    ShouldEmitBuildVersion = true;
  }

  if (const Triple *TVT = DarwinTargetVariantTriple) {
    if (Target.isMacOSX() && TVT->isMacCatalystEnvironment()) {
      auto TVLinkedTargetVersion =
          targetVersionOrMinimumSupportedOSVersion(*TVT, TVT->getiOSVersion());
      emitDarwinTargetVariantBuildVersion(
          getMachoBuildVersionPlatformType(*TVT),
          TVLinkedTargetVersion.getMajor(),
          TVLinkedTargetVersion.getMinor().value_or(0),
          TVLinkedTargetVersion.getSubminor().value_or(0),
          DarwinTargetVariantSDKVersion);
    }
  }

  if (ShouldEmitBuildVersion)
    return;

  emitVersionMin(getMachoVersionMinLoadCommandType(Target),
                 LinkedTargetVersion.getMajor(),
                 LinkedTargetVersion.getMinor().value_or(0),
                 LinkedTargetVersion.getSubminor().value_or(0), SDKVersion);
}
