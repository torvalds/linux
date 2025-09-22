//===-- X86AsmBackend.cpp - X86 Assembler Backend -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/X86BaseInfo.h"
#include "MCTargetDesc/X86EncodingOptimization.h"
#include "MCTargetDesc/X86FixupKinds.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCELFStreamer.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixupKindInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCMachObjectWriter.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCValue.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
/// A wrapper for holding a mask of the values from X86::AlignBranchBoundaryKind
class X86AlignBranchKind {
private:
  uint8_t AlignBranchKind = 0;

public:
  void operator=(const std::string &Val) {
    if (Val.empty())
      return;
    SmallVector<StringRef, 6> BranchTypes;
    StringRef(Val).split(BranchTypes, '+', -1, false);
    for (auto BranchType : BranchTypes) {
      if (BranchType == "fused")
        addKind(X86::AlignBranchFused);
      else if (BranchType == "jcc")
        addKind(X86::AlignBranchJcc);
      else if (BranchType == "jmp")
        addKind(X86::AlignBranchJmp);
      else if (BranchType == "call")
        addKind(X86::AlignBranchCall);
      else if (BranchType == "ret")
        addKind(X86::AlignBranchRet);
      else if (BranchType == "indirect")
        addKind(X86::AlignBranchIndirect);
      else {
        errs() << "invalid argument " << BranchType.str()
               << " to -x86-align-branch=; each element must be one of: fused, "
                  "jcc, jmp, call, ret, indirect.(plus separated)\n";
      }
    }
  }

  operator uint8_t() const { return AlignBranchKind; }
  void addKind(X86::AlignBranchBoundaryKind Value) { AlignBranchKind |= Value; }
};

X86AlignBranchKind X86AlignBranchKindLoc;

cl::opt<unsigned> X86AlignBranchBoundary(
    "x86-align-branch-boundary", cl::init(0),
    cl::desc(
        "Control how the assembler should align branches with NOP. If the "
        "boundary's size is not 0, it should be a power of 2 and no less "
        "than 32. Branches will be aligned to prevent from being across or "
        "against the boundary of specified size. The default value 0 does not "
        "align branches."));

cl::opt<X86AlignBranchKind, true, cl::parser<std::string>> X86AlignBranch(
    "x86-align-branch",
    cl::desc(
        "Specify types of branches to align (plus separated list of types):"
             "\njcc      indicates conditional jumps"
             "\nfused    indicates fused conditional jumps"
             "\njmp      indicates direct unconditional jumps"
             "\ncall     indicates direct and indirect calls"
             "\nret      indicates rets"
             "\nindirect indicates indirect unconditional jumps"),
    cl::location(X86AlignBranchKindLoc));

cl::opt<bool> X86AlignBranchWithin32BBoundaries(
    "x86-branches-within-32B-boundaries", cl::init(false),
    cl::desc(
        "Align selected instructions to mitigate negative performance impact "
        "of Intel's micro code update for errata skx102.  May break "
        "assumptions about labels corresponding to particular instructions, "
        "and should be used with caution."));

cl::opt<unsigned> X86PadMaxPrefixSize(
    "x86-pad-max-prefix-size", cl::init(0),
    cl::desc("Maximum number of prefixes to use for padding"));

cl::opt<bool> X86PadForAlign(
    "x86-pad-for-align", cl::init(false), cl::Hidden,
    cl::desc("Pad previous instructions to implement align directives"));

cl::opt<bool> X86PadForBranchAlign(
    "x86-pad-for-branch-align", cl::init(true), cl::Hidden,
    cl::desc("Pad previous instructions to implement branch alignment"));

class X86AsmBackend : public MCAsmBackend {
  const MCSubtargetInfo &STI;
  std::unique_ptr<const MCInstrInfo> MCII;
  X86AlignBranchKind AlignBranchType;
  Align AlignBoundary;
  unsigned TargetPrefixMax = 0;

  MCInst PrevInst;
  unsigned PrevInstOpcode = 0;
  MCBoundaryAlignFragment *PendingBA = nullptr;
  std::pair<MCFragment *, size_t> PrevInstPosition;
  bool IsRightAfterData = false;

  uint8_t determinePaddingPrefix(const MCInst &Inst) const;
  bool isMacroFused(const MCInst &Cmp, const MCInst &Jcc) const;
  bool needAlign(const MCInst &Inst) const;
  bool canPadBranches(MCObjectStreamer &OS) const;
  bool canPadInst(const MCInst &Inst, MCObjectStreamer &OS) const;

public:
  X86AsmBackend(const Target &T, const MCSubtargetInfo &STI)
      : MCAsmBackend(llvm::endianness::little), STI(STI),
        MCII(T.createMCInstrInfo()) {
    if (X86AlignBranchWithin32BBoundaries) {
      // At the moment, this defaults to aligning fused branches, unconditional
      // jumps, and (unfused) conditional jumps with nops.  Both the
      // instructions aligned and the alignment method (nop vs prefix) may
      // change in the future.
      AlignBoundary = assumeAligned(32);
      AlignBranchType.addKind(X86::AlignBranchFused);
      AlignBranchType.addKind(X86::AlignBranchJcc);
      AlignBranchType.addKind(X86::AlignBranchJmp);
    }
    // Allow overriding defaults set by main flag
    if (X86AlignBranchBoundary.getNumOccurrences())
      AlignBoundary = assumeAligned(X86AlignBranchBoundary);
    if (X86AlignBranch.getNumOccurrences())
      AlignBranchType = X86AlignBranchKindLoc;
    if (X86PadMaxPrefixSize.getNumOccurrences())
      TargetPrefixMax = X86PadMaxPrefixSize;
  }

  bool allowAutoPadding() const override;
  bool allowEnhancedRelaxation() const override;
  void emitInstructionBegin(MCObjectStreamer &OS, const MCInst &Inst,
                            const MCSubtargetInfo &STI);
  void emitInstructionEnd(MCObjectStreamer &OS, const MCInst &Inst);

  unsigned getNumFixupKinds() const override {
    return X86::NumTargetFixupKinds;
  }

  std::optional<MCFixupKind> getFixupKind(StringRef Name) const override;

  const MCFixupKindInfo &getFixupKindInfo(MCFixupKind Kind) const override;

  bool shouldForceRelocation(const MCAssembler &Asm, const MCFixup &Fixup,
                             const MCValue &Target,
                             const MCSubtargetInfo *STI) override;

  void applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                  const MCValue &Target, MutableArrayRef<char> Data,
                  uint64_t Value, bool IsResolved,
                  const MCSubtargetInfo *STI) const override;

  bool mayNeedRelaxation(const MCInst &Inst,
                         const MCSubtargetInfo &STI) const override;

  bool fixupNeedsRelaxation(const MCFixup &Fixup,
                            uint64_t Value) const override;

  void relaxInstruction(MCInst &Inst,
                        const MCSubtargetInfo &STI) const override;

  bool padInstructionViaRelaxation(MCRelaxableFragment &RF,
                                   MCCodeEmitter &Emitter,
                                   unsigned &RemainingSize) const;

  bool padInstructionViaPrefix(MCRelaxableFragment &RF, MCCodeEmitter &Emitter,
                               unsigned &RemainingSize) const;

  bool padInstructionEncoding(MCRelaxableFragment &RF, MCCodeEmitter &Emitter,
                              unsigned &RemainingSize) const;

  void finishLayout(const MCAssembler &Asm) const override;

  unsigned getMaximumNopSize(const MCSubtargetInfo &STI) const override;

  bool writeNopData(raw_ostream &OS, uint64_t Count,
                    const MCSubtargetInfo *STI) const override;
};
} // end anonymous namespace

static bool isRelaxableBranch(unsigned Opcode) {
  return Opcode == X86::JCC_1 || Opcode == X86::JMP_1;
}

static unsigned getRelaxedOpcodeBranch(unsigned Opcode,
                                       bool Is16BitMode = false) {
  switch (Opcode) {
  default:
    llvm_unreachable("invalid opcode for branch");
  case X86::JCC_1:
    return (Is16BitMode) ? X86::JCC_2 : X86::JCC_4;
  case X86::JMP_1:
    return (Is16BitMode) ? X86::JMP_2 : X86::JMP_4;
  }
}

static unsigned getRelaxedOpcode(const MCInst &MI, bool Is16BitMode) {
  unsigned Opcode = MI.getOpcode();
  return isRelaxableBranch(Opcode) ? getRelaxedOpcodeBranch(Opcode, Is16BitMode)
                                   : X86::getOpcodeForLongImmediateForm(Opcode);
}

static X86::CondCode getCondFromBranch(const MCInst &MI,
                                       const MCInstrInfo &MCII) {
  unsigned Opcode = MI.getOpcode();
  switch (Opcode) {
  default:
    return X86::COND_INVALID;
  case X86::JCC_1: {
    const MCInstrDesc &Desc = MCII.get(Opcode);
    return static_cast<X86::CondCode>(
        MI.getOperand(Desc.getNumOperands() - 1).getImm());
  }
  }
}

static X86::SecondMacroFusionInstKind
classifySecondInstInMacroFusion(const MCInst &MI, const MCInstrInfo &MCII) {
  X86::CondCode CC = getCondFromBranch(MI, MCII);
  return classifySecondCondCodeInMacroFusion(CC);
}

/// Check if the instruction uses RIP relative addressing.
static bool isRIPRelative(const MCInst &MI, const MCInstrInfo &MCII) {
  unsigned Opcode = MI.getOpcode();
  const MCInstrDesc &Desc = MCII.get(Opcode);
  uint64_t TSFlags = Desc.TSFlags;
  unsigned CurOp = X86II::getOperandBias(Desc);
  int MemoryOperand = X86II::getMemoryOperandNo(TSFlags);
  if (MemoryOperand < 0)
    return false;
  unsigned BaseRegNum = MemoryOperand + CurOp + X86::AddrBaseReg;
  unsigned BaseReg = MI.getOperand(BaseRegNum).getReg();
  return (BaseReg == X86::RIP);
}

/// Check if the instruction is a prefix.
static bool isPrefix(unsigned Opcode, const MCInstrInfo &MCII) {
  return X86II::isPrefix(MCII.get(Opcode).TSFlags);
}

/// Check if the instruction is valid as the first instruction in macro fusion.
static bool isFirstMacroFusibleInst(const MCInst &Inst,
                                    const MCInstrInfo &MCII) {
  // An Intel instruction with RIP relative addressing is not macro fusible.
  if (isRIPRelative(Inst, MCII))
    return false;
  X86::FirstMacroFusionInstKind FIK =
      X86::classifyFirstOpcodeInMacroFusion(Inst.getOpcode());
  return FIK != X86::FirstMacroFusionInstKind::Invalid;
}

/// X86 can reduce the bytes of NOP by padding instructions with prefixes to
/// get a better peformance in some cases. Here, we determine which prefix is
/// the most suitable.
///
/// If the instruction has a segment override prefix, use the existing one.
/// If the target is 64-bit, use the CS.
/// If the target is 32-bit,
///   - If the instruction has a ESP/EBP base register, use SS.
///   - Otherwise use DS.
uint8_t X86AsmBackend::determinePaddingPrefix(const MCInst &Inst) const {
  assert((STI.hasFeature(X86::Is32Bit) || STI.hasFeature(X86::Is64Bit)) &&
         "Prefixes can be added only in 32-bit or 64-bit mode.");
  const MCInstrDesc &Desc = MCII->get(Inst.getOpcode());
  uint64_t TSFlags = Desc.TSFlags;

  // Determine where the memory operand starts, if present.
  int MemoryOperand = X86II::getMemoryOperandNo(TSFlags);
  if (MemoryOperand != -1)
    MemoryOperand += X86II::getOperandBias(Desc);

  unsigned SegmentReg = 0;
  if (MemoryOperand >= 0) {
    // Check for explicit segment override on memory operand.
    SegmentReg = Inst.getOperand(MemoryOperand + X86::AddrSegmentReg).getReg();
  }

  switch (TSFlags & X86II::FormMask) {
  default:
    break;
  case X86II::RawFrmDstSrc: {
    // Check segment override opcode prefix as needed (not for %ds).
    if (Inst.getOperand(2).getReg() != X86::DS)
      SegmentReg = Inst.getOperand(2).getReg();
    break;
  }
  case X86II::RawFrmSrc: {
    // Check segment override opcode prefix as needed (not for %ds).
    if (Inst.getOperand(1).getReg() != X86::DS)
      SegmentReg = Inst.getOperand(1).getReg();
    break;
  }
  case X86II::RawFrmMemOffs: {
    // Check segment override opcode prefix as needed.
    SegmentReg = Inst.getOperand(1).getReg();
    break;
  }
  }

  if (SegmentReg != 0)
    return X86::getSegmentOverridePrefixForReg(SegmentReg);

  if (STI.hasFeature(X86::Is64Bit))
    return X86::CS_Encoding;

  if (MemoryOperand >= 0) {
    unsigned BaseRegNum = MemoryOperand + X86::AddrBaseReg;
    unsigned BaseReg = Inst.getOperand(BaseRegNum).getReg();
    if (BaseReg == X86::ESP || BaseReg == X86::EBP)
      return X86::SS_Encoding;
  }
  return X86::DS_Encoding;
}

/// Check if the two instructions will be macro-fused on the target cpu.
bool X86AsmBackend::isMacroFused(const MCInst &Cmp, const MCInst &Jcc) const {
  const MCInstrDesc &InstDesc = MCII->get(Jcc.getOpcode());
  if (!InstDesc.isConditionalBranch())
    return false;
  if (!isFirstMacroFusibleInst(Cmp, *MCII))
    return false;
  const X86::FirstMacroFusionInstKind CmpKind =
      X86::classifyFirstOpcodeInMacroFusion(Cmp.getOpcode());
  const X86::SecondMacroFusionInstKind BranchKind =
      classifySecondInstInMacroFusion(Jcc, *MCII);
  return X86::isMacroFused(CmpKind, BranchKind);
}

/// Check if the instruction has a variant symbol operand.
static bool hasVariantSymbol(const MCInst &MI) {
  for (auto &Operand : MI) {
    if (!Operand.isExpr())
      continue;
    const MCExpr &Expr = *Operand.getExpr();
    if (Expr.getKind() == MCExpr::SymbolRef &&
        cast<MCSymbolRefExpr>(Expr).getKind() != MCSymbolRefExpr::VK_None)
      return true;
  }
  return false;
}

bool X86AsmBackend::allowAutoPadding() const {
  return (AlignBoundary != Align(1) && AlignBranchType != X86::AlignBranchNone);
}

bool X86AsmBackend::allowEnhancedRelaxation() const {
  return allowAutoPadding() && TargetPrefixMax != 0 && X86PadForBranchAlign;
}

/// X86 has certain instructions which enable interrupts exactly one
/// instruction *after* the instruction which stores to SS.  Return true if the
/// given instruction may have such an interrupt delay slot.
static bool mayHaveInterruptDelaySlot(unsigned InstOpcode) {
  switch (InstOpcode) {
  case X86::POPSS16:
  case X86::POPSS32:
  case X86::STI:
    return true;

  case X86::MOV16sr:
  case X86::MOV32sr:
  case X86::MOV64sr:
  case X86::MOV16sm:
    // In fact, this is only the case if the first operand is SS. However, as
    // segment moves occur extremely rarely, this is just a minor pessimization.
    return true;
  }
  return false;
}

/// Check if the instruction to be emitted is right after any data.
static bool
isRightAfterData(MCFragment *CurrentFragment,
                 const std::pair<MCFragment *, size_t> &PrevInstPosition) {
  MCFragment *F = CurrentFragment;
  // Since data is always emitted into a DataFragment, our check strategy is
  // simple here.
  //   - If the fragment is a DataFragment
  //     - If it's empty (section start or data after align), return false.
  //     - If it's not the fragment where the previous instruction is,
  //       returns true.
  //     - If it's the fragment holding the previous instruction but its
  //       size changed since the previous instruction was emitted into
  //       it, returns true.
  //     - Otherwise returns false.
  //   - If the fragment is not a DataFragment, returns false.
  if (auto *DF = dyn_cast_or_null<MCDataFragment>(F))
    return DF->getContents().size() &&
           (DF != PrevInstPosition.first ||
            DF->getContents().size() != PrevInstPosition.second);

  return false;
}

/// \returns the fragment size if it has instructions, otherwise returns 0.
static size_t getSizeForInstFragment(const MCFragment *F) {
  if (!F || !F->hasInstructions())
    return 0;
  // MCEncodedFragmentWithContents being templated makes this tricky.
  switch (F->getKind()) {
  default:
    llvm_unreachable("Unknown fragment with instructions!");
  case MCFragment::FT_Data:
    return cast<MCDataFragment>(*F).getContents().size();
  case MCFragment::FT_Relaxable:
    return cast<MCRelaxableFragment>(*F).getContents().size();
  case MCFragment::FT_CompactEncodedInst:
    return cast<MCCompactEncodedInstFragment>(*F).getContents().size();
  }
}

/// Return true if we can insert NOP or prefixes automatically before the
/// the instruction to be emitted.
bool X86AsmBackend::canPadInst(const MCInst &Inst, MCObjectStreamer &OS) const {
  if (hasVariantSymbol(Inst))
    // Linker may rewrite the instruction with variant symbol operand(e.g.
    // TLSCALL).
    return false;

  if (mayHaveInterruptDelaySlot(PrevInstOpcode))
    // If this instruction follows an interrupt enabling instruction with a one
    // instruction delay, inserting a nop would change behavior.
    return false;

  if (isPrefix(PrevInstOpcode, *MCII))
    // If this instruction follows a prefix, inserting a nop/prefix would change
    // semantic.
    return false;

  if (isPrefix(Inst.getOpcode(), *MCII))
    // If this instruction is a prefix, inserting a prefix would change
    // semantic.
    return false;

  if (IsRightAfterData)
    // If this instruction follows any data, there is no clear
    // instruction boundary, inserting a nop/prefix would change semantic.
    return false;

  return true;
}

bool X86AsmBackend::canPadBranches(MCObjectStreamer &OS) const {
  if (!OS.getAllowAutoPadding())
    return false;
  assert(allowAutoPadding() && "incorrect initialization!");

  // We only pad in text section.
  if (!OS.getCurrentSectionOnly()->isText())
    return false;

  // To be Done: Currently don't deal with Bundle cases.
  if (OS.getAssembler().isBundlingEnabled())
    return false;

  // Branches only need to be aligned in 32-bit or 64-bit mode.
  if (!(STI.hasFeature(X86::Is64Bit) || STI.hasFeature(X86::Is32Bit)))
    return false;

  return true;
}

/// Check if the instruction operand needs to be aligned.
bool X86AsmBackend::needAlign(const MCInst &Inst) const {
  const MCInstrDesc &Desc = MCII->get(Inst.getOpcode());
  return (Desc.isConditionalBranch() &&
          (AlignBranchType & X86::AlignBranchJcc)) ||
         (Desc.isUnconditionalBranch() &&
          (AlignBranchType & X86::AlignBranchJmp)) ||
         (Desc.isCall() && (AlignBranchType & X86::AlignBranchCall)) ||
         (Desc.isReturn() && (AlignBranchType & X86::AlignBranchRet)) ||
         (Desc.isIndirectBranch() &&
          (AlignBranchType & X86::AlignBranchIndirect));
}

/// Insert BoundaryAlignFragment before instructions to align branches.
void X86AsmBackend::emitInstructionBegin(MCObjectStreamer &OS,
                                         const MCInst &Inst, const MCSubtargetInfo &STI) {
  // Used by canPadInst. Done here, because in emitInstructionEnd, the current
  // fragment will have changed.
  IsRightAfterData =
      isRightAfterData(OS.getCurrentFragment(), PrevInstPosition);

  if (!canPadBranches(OS))
    return;

  // NB: PrevInst only valid if canPadBranches is true.
  if (!isMacroFused(PrevInst, Inst))
    // Macro fusion doesn't happen indeed, clear the pending.
    PendingBA = nullptr;

  // When branch padding is enabled (basically the skx102 erratum => unlikely),
  // we call canPadInst (not cheap) twice. However, in the common case, we can
  // avoid unnecessary calls to that, as this is otherwise only used for
  // relaxable fragments.
  if (!canPadInst(Inst, OS))
    return;

  if (PendingBA && PendingBA->getNext() == OS.getCurrentFragment()) {
    // Macro fusion actually happens and there is no other fragment inserted
    // after the previous instruction.
    //
    // Do nothing here since we already inserted a BoudaryAlign fragment when
    // we met the first instruction in the fused pair and we'll tie them
    // together in emitInstructionEnd.
    //
    // Note: When there is at least one fragment, such as MCAlignFragment,
    // inserted after the previous instruction, e.g.
    //
    // \code
    //   cmp %rax %rcx
    //   .align 16
    //   je .Label0
    // \ endcode
    //
    // We will treat the JCC as a unfused branch although it may be fused
    // with the CMP.
    return;
  }

  if (needAlign(Inst) || ((AlignBranchType & X86::AlignBranchFused) &&
                          isFirstMacroFusibleInst(Inst, *MCII))) {
    // If we meet a unfused branch or the first instuction in a fusiable pair,
    // insert a BoundaryAlign fragment.
    PendingBA = OS.getContext().allocFragment<MCBoundaryAlignFragment>(
        AlignBoundary, STI);
    OS.insert(PendingBA);
  }
}

/// Set the last fragment to be aligned for the BoundaryAlignFragment.
void X86AsmBackend::emitInstructionEnd(MCObjectStreamer &OS,
                                       const MCInst &Inst) {
  MCFragment *CF = OS.getCurrentFragment();
  if (auto *F = dyn_cast_or_null<MCRelaxableFragment>(CF))
    F->setAllowAutoPadding(canPadInst(Inst, OS));

  // Update PrevInstOpcode here, canPadInst() reads that.
  PrevInstOpcode = Inst.getOpcode();
  PrevInstPosition = std::make_pair(CF, getSizeForInstFragment(CF));

  if (!canPadBranches(OS))
    return;

  // PrevInst is only needed if canPadBranches. Copying an MCInst isn't cheap.
  PrevInst = Inst;

  if (!needAlign(Inst) || !PendingBA)
    return;

  // Tie the aligned instructions into a pending BoundaryAlign.
  PendingBA->setLastFragment(CF);
  PendingBA = nullptr;

  // We need to ensure that further data isn't added to the current
  // DataFragment, so that we can get the size of instructions later in
  // MCAssembler::relaxBoundaryAlign. The easiest way is to insert a new empty
  // DataFragment.
  if (isa_and_nonnull<MCDataFragment>(CF))
    OS.insert(OS.getContext().allocFragment<MCDataFragment>());

  // Update the maximum alignment on the current section if necessary.
  MCSection *Sec = OS.getCurrentSectionOnly();
  Sec->ensureMinAlignment(AlignBoundary);
}

std::optional<MCFixupKind> X86AsmBackend::getFixupKind(StringRef Name) const {
  if (STI.getTargetTriple().isOSBinFormatELF()) {
    unsigned Type;
    if (STI.getTargetTriple().getArch() == Triple::x86_64) {
      Type = llvm::StringSwitch<unsigned>(Name)
#define ELF_RELOC(X, Y) .Case(#X, Y)
#include "llvm/BinaryFormat/ELFRelocs/x86_64.def"
#undef ELF_RELOC
                 .Case("BFD_RELOC_NONE", ELF::R_X86_64_NONE)
                 .Case("BFD_RELOC_8", ELF::R_X86_64_8)
                 .Case("BFD_RELOC_16", ELF::R_X86_64_16)
                 .Case("BFD_RELOC_32", ELF::R_X86_64_32)
                 .Case("BFD_RELOC_64", ELF::R_X86_64_64)
                 .Default(-1u);
    } else {
      Type = llvm::StringSwitch<unsigned>(Name)
#define ELF_RELOC(X, Y) .Case(#X, Y)
#include "llvm/BinaryFormat/ELFRelocs/i386.def"
#undef ELF_RELOC
                 .Case("BFD_RELOC_NONE", ELF::R_386_NONE)
                 .Case("BFD_RELOC_8", ELF::R_386_8)
                 .Case("BFD_RELOC_16", ELF::R_386_16)
                 .Case("BFD_RELOC_32", ELF::R_386_32)
                 .Default(-1u);
    }
    if (Type == -1u)
      return std::nullopt;
    return static_cast<MCFixupKind>(FirstLiteralRelocationKind + Type);
  }
  return MCAsmBackend::getFixupKind(Name);
}

const MCFixupKindInfo &X86AsmBackend::getFixupKindInfo(MCFixupKind Kind) const {
  const static MCFixupKindInfo Infos[X86::NumTargetFixupKinds] = {
      {"reloc_riprel_4byte", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"reloc_riprel_4byte_movq_load", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"reloc_riprel_4byte_relax", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"reloc_riprel_4byte_relax_rex", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
      {"reloc_signed_4byte", 0, 32, 0},
      {"reloc_signed_4byte_relax", 0, 32, 0},
      {"reloc_global_offset_table", 0, 32, 0},
      {"reloc_global_offset_table8", 0, 64, 0},
      {"reloc_branch_4byte_pcrel", 0, 32, MCFixupKindInfo::FKF_IsPCRel},
  };

  // Fixup kinds from .reloc directive are like R_386_NONE/R_X86_64_NONE. They
  // do not require any extra processing.
  if (Kind >= FirstLiteralRelocationKind)
    return MCAsmBackend::getFixupKindInfo(FK_NONE);

  if (Kind < FirstTargetFixupKind)
    return MCAsmBackend::getFixupKindInfo(Kind);

  assert(unsigned(Kind - FirstTargetFixupKind) < getNumFixupKinds() &&
         "Invalid kind!");
  assert(Infos[Kind - FirstTargetFixupKind].Name && "Empty fixup name!");
  return Infos[Kind - FirstTargetFixupKind];
}

bool X86AsmBackend::shouldForceRelocation(const MCAssembler &,
                                          const MCFixup &Fixup, const MCValue &,
                                          const MCSubtargetInfo *STI) {
  return Fixup.getKind() >= FirstLiteralRelocationKind;
}

static unsigned getFixupKindSize(unsigned Kind) {
  switch (Kind) {
  default:
    llvm_unreachable("invalid fixup kind!");
  case FK_NONE:
    return 0;
  case FK_PCRel_1:
  case FK_SecRel_1:
  case FK_Data_1:
    return 1;
  case FK_PCRel_2:
  case FK_SecRel_2:
  case FK_Data_2:
    return 2;
  case FK_PCRel_4:
  case X86::reloc_riprel_4byte:
  case X86::reloc_riprel_4byte_relax:
  case X86::reloc_riprel_4byte_relax_rex:
  case X86::reloc_riprel_4byte_movq_load:
  case X86::reloc_signed_4byte:
  case X86::reloc_signed_4byte_relax:
  case X86::reloc_global_offset_table:
  case X86::reloc_branch_4byte_pcrel:
  case FK_SecRel_4:
  case FK_Data_4:
    return 4;
  case FK_PCRel_8:
  case FK_SecRel_8:
  case FK_Data_8:
  case X86::reloc_global_offset_table8:
    return 8;
  }
}

void X86AsmBackend::applyFixup(const MCAssembler &Asm, const MCFixup &Fixup,
                               const MCValue &Target,
                               MutableArrayRef<char> Data,
                               uint64_t Value, bool IsResolved,
                               const MCSubtargetInfo *STI) const {
  unsigned Kind = Fixup.getKind();
  if (Kind >= FirstLiteralRelocationKind)
    return;
  unsigned Size = getFixupKindSize(Kind);

  assert(Fixup.getOffset() + Size <= Data.size() && "Invalid fixup offset!");

  int64_t SignedValue = static_cast<int64_t>(Value);
  if ((Target.isAbsolute() || IsResolved) &&
      getFixupKindInfo(Fixup.getKind()).Flags &
      MCFixupKindInfo::FKF_IsPCRel) {
    // check that PC relative fixup fits into the fixup size.
    if (Size > 0 && !isIntN(Size * 8, SignedValue))
      Asm.getContext().reportError(
                                   Fixup.getLoc(), "value of " + Twine(SignedValue) +
                                   " is too large for field of " + Twine(Size) +
                                   ((Size == 1) ? " byte." : " bytes."));
  } else {
    // Check that uppper bits are either all zeros or all ones.
    // Specifically ignore overflow/underflow as long as the leakage is
    // limited to the lower bits. This is to remain compatible with
    // other assemblers.
    assert((Size == 0 || isIntN(Size * 8 + 1, SignedValue)) &&
           "Value does not fit in the Fixup field");
  }

  for (unsigned i = 0; i != Size; ++i)
    Data[Fixup.getOffset() + i] = uint8_t(Value >> (i * 8));
}

bool X86AsmBackend::mayNeedRelaxation(const MCInst &MI,
                                      const MCSubtargetInfo &STI) const {
  unsigned Opcode = MI.getOpcode();
  unsigned SkipOperands = X86::isCCMPCC(Opcode) ? 2 : 0;
  return isRelaxableBranch(Opcode) ||
         (X86::getOpcodeForLongImmediateForm(Opcode) != Opcode &&
          MI.getOperand(MI.getNumOperands() - 1 - SkipOperands).isExpr());
}

bool X86AsmBackend::fixupNeedsRelaxation(const MCFixup &Fixup,
                                         uint64_t Value) const {
  // Relax if the value is too big for a (signed) i8.
  return !isInt<8>(Value);
}

// FIXME: Can tblgen help at all here to verify there aren't other instructions
// we can relax?
void X86AsmBackend::relaxInstruction(MCInst &Inst,
                                     const MCSubtargetInfo &STI) const {
  // The only relaxations X86 does is from a 1byte pcrel to a 4byte pcrel.
  bool Is16BitMode = STI.hasFeature(X86::Is16Bit);
  unsigned RelaxedOp = getRelaxedOpcode(Inst, Is16BitMode);

  if (RelaxedOp == Inst.getOpcode()) {
    SmallString<256> Tmp;
    raw_svector_ostream OS(Tmp);
    Inst.dump_pretty(OS);
    OS << "\n";
    report_fatal_error("unexpected instruction to relax: " + OS.str());
  }

  Inst.setOpcode(RelaxedOp);
}

bool X86AsmBackend::padInstructionViaPrefix(MCRelaxableFragment &RF,
                                            MCCodeEmitter &Emitter,
                                            unsigned &RemainingSize) const {
  if (!RF.getAllowAutoPadding())
    return false;
  // If the instruction isn't fully relaxed, shifting it around might require a
  // larger value for one of the fixups then can be encoded.  The outer loop
  // will also catch this before moving to the next instruction, but we need to
  // prevent padding this single instruction as well.
  if (mayNeedRelaxation(RF.getInst(), *RF.getSubtargetInfo()))
    return false;

  const unsigned OldSize = RF.getContents().size();
  if (OldSize == 15)
    return false;

  const unsigned MaxPossiblePad = std::min(15 - OldSize, RemainingSize);
  const unsigned RemainingPrefixSize = [&]() -> unsigned {
    SmallString<15> Code;
    X86_MC::emitPrefix(Emitter, RF.getInst(), Code, STI);
    assert(Code.size() < 15 && "The number of prefixes must be less than 15.");

    // TODO: It turns out we need a decent amount of plumbing for the target
    // specific bits to determine number of prefixes its safe to add.  Various
    // targets (older chips mostly, but also Atom family) encounter decoder
    // stalls with too many prefixes.  For testing purposes, we set the value
    // externally for the moment.
    unsigned ExistingPrefixSize = Code.size();
    if (TargetPrefixMax <= ExistingPrefixSize)
      return 0;
    return TargetPrefixMax - ExistingPrefixSize;
  }();
  const unsigned PrefixBytesToAdd =
      std::min(MaxPossiblePad, RemainingPrefixSize);
  if (PrefixBytesToAdd == 0)
    return false;

  const uint8_t Prefix = determinePaddingPrefix(RF.getInst());

  SmallString<256> Code;
  Code.append(PrefixBytesToAdd, Prefix);
  Code.append(RF.getContents().begin(), RF.getContents().end());
  RF.getContents() = Code;

  // Adjust the fixups for the change in offsets
  for (auto &F : RF.getFixups()) {
    F.setOffset(F.getOffset() + PrefixBytesToAdd);
  }

  RemainingSize -= PrefixBytesToAdd;
  return true;
}

bool X86AsmBackend::padInstructionViaRelaxation(MCRelaxableFragment &RF,
                                                MCCodeEmitter &Emitter,
                                                unsigned &RemainingSize) const {
  if (!mayNeedRelaxation(RF.getInst(), *RF.getSubtargetInfo()))
    // TODO: There are lots of other tricks we could apply for increasing
    // encoding size without impacting performance.
    return false;

  MCInst Relaxed = RF.getInst();
  relaxInstruction(Relaxed, *RF.getSubtargetInfo());

  SmallVector<MCFixup, 4> Fixups;
  SmallString<15> Code;
  Emitter.encodeInstruction(Relaxed, Code, Fixups, *RF.getSubtargetInfo());
  const unsigned OldSize = RF.getContents().size();
  const unsigned NewSize = Code.size();
  assert(NewSize >= OldSize && "size decrease during relaxation?");
  unsigned Delta = NewSize - OldSize;
  if (Delta > RemainingSize)
    return false;
  RF.setInst(Relaxed);
  RF.getContents() = Code;
  RF.getFixups() = Fixups;
  RemainingSize -= Delta;
  return true;
}

bool X86AsmBackend::padInstructionEncoding(MCRelaxableFragment &RF,
                                           MCCodeEmitter &Emitter,
                                           unsigned &RemainingSize) const {
  bool Changed = false;
  if (RemainingSize != 0)
    Changed |= padInstructionViaRelaxation(RF, Emitter, RemainingSize);
  if (RemainingSize != 0)
    Changed |= padInstructionViaPrefix(RF, Emitter, RemainingSize);
  return Changed;
}

void X86AsmBackend::finishLayout(MCAssembler const &Asm) const {
  // See if we can further relax some instructions to cut down on the number of
  // nop bytes required for code alignment.  The actual win is in reducing
  // instruction count, not number of bytes.  Modern X86-64 can easily end up
  // decode limited.  It is often better to reduce the number of instructions
  // (i.e. eliminate nops) even at the cost of increasing the size and
  // complexity of others.
  if (!X86PadForAlign && !X86PadForBranchAlign)
    return;

  // The processed regions are delimitered by LabeledFragments. -g may have more
  // MCSymbols and therefore different relaxation results. X86PadForAlign is
  // disabled by default to eliminate the -g vs non -g difference.
  DenseSet<MCFragment *> LabeledFragments;
  for (const MCSymbol &S : Asm.symbols())
    LabeledFragments.insert(S.getFragment(false));

  for (MCSection &Sec : Asm) {
    if (!Sec.isText())
      continue;

    SmallVector<MCRelaxableFragment *, 4> Relaxable;
    for (MCSection::iterator I = Sec.begin(), IE = Sec.end(); I != IE; ++I) {
      MCFragment &F = *I;

      if (LabeledFragments.count(&F))
        Relaxable.clear();

      if (F.getKind() == MCFragment::FT_Data ||
          F.getKind() == MCFragment::FT_CompactEncodedInst)
        // Skip and ignore
        continue;

      if (F.getKind() == MCFragment::FT_Relaxable) {
        auto &RF = cast<MCRelaxableFragment>(*I);
        Relaxable.push_back(&RF);
        continue;
      }

      auto canHandle = [](MCFragment &F) -> bool {
        switch (F.getKind()) {
        default:
          return false;
        case MCFragment::FT_Align:
          return X86PadForAlign;
        case MCFragment::FT_BoundaryAlign:
          return X86PadForBranchAlign;
        }
      };
      // For any unhandled kind, assume we can't change layout.
      if (!canHandle(F)) {
        Relaxable.clear();
        continue;
      }

#ifndef NDEBUG
      const uint64_t OrigOffset = Asm.getFragmentOffset(F);
#endif
      const uint64_t OrigSize = Asm.computeFragmentSize(F);

      // To keep the effects local, prefer to relax instructions closest to
      // the align directive.  This is purely about human understandability
      // of the resulting code.  If we later find a reason to expand
      // particular instructions over others, we can adjust.
      unsigned RemainingSize = OrigSize;
      while (!Relaxable.empty() && RemainingSize != 0) {
        auto &RF = *Relaxable.pop_back_val();
        // Give the backend a chance to play any tricks it wishes to increase
        // the encoding size of the given instruction.  Target independent code
        // will try further relaxation, but target's may play further tricks.
        if (padInstructionEncoding(RF, Asm.getEmitter(), RemainingSize))
          Sec.setHasLayout(false);

        // If we have an instruction which hasn't been fully relaxed, we can't
        // skip past it and insert bytes before it.  Changing its starting
        // offset might require a larger negative offset than it can encode.
        // We don't need to worry about larger positive offsets as none of the
        // possible offsets between this and our align are visible, and the
        // ones afterwards aren't changing.
        if (mayNeedRelaxation(RF.getInst(), *RF.getSubtargetInfo()))
          break;
      }
      Relaxable.clear();

      // BoundaryAlign explicitly tracks it's size (unlike align)
      if (F.getKind() == MCFragment::FT_BoundaryAlign)
        cast<MCBoundaryAlignFragment>(F).setSize(RemainingSize);

#ifndef NDEBUG
      const uint64_t FinalOffset = Asm.getFragmentOffset(F);
      const uint64_t FinalSize = Asm.computeFragmentSize(F);
      assert(OrigOffset + OrigSize == FinalOffset + FinalSize &&
             "can't move start of next fragment!");
      assert(FinalSize == RemainingSize && "inconsistent size computation?");
#endif

      // If we're looking at a boundary align, make sure we don't try to pad
      // its target instructions for some following directive.  Doing so would
      // break the alignment of the current boundary align.
      if (auto *BF = dyn_cast<MCBoundaryAlignFragment>(&F)) {
        const MCFragment *LastFragment = BF->getLastFragment();
        if (!LastFragment)
          continue;
        while (&*I != LastFragment)
          ++I;
      }
    }
  }

  // The layout is done. Mark every fragment as valid.
  for (MCSection &Section : Asm) {
    Asm.getFragmentOffset(*Section.curFragList()->Tail);
    Asm.computeFragmentSize(*Section.curFragList()->Tail);
  }
}

unsigned X86AsmBackend::getMaximumNopSize(const MCSubtargetInfo &STI) const {
  if (STI.hasFeature(X86::Is16Bit))
    return 4;
  if (!STI.hasFeature(X86::FeatureNOPL) && !STI.hasFeature(X86::Is64Bit))
    return 1;
  if (STI.hasFeature(X86::TuningFast7ByteNOP))
    return 7;
  if (STI.hasFeature(X86::TuningFast15ByteNOP))
    return 15;
  if (STI.hasFeature(X86::TuningFast11ByteNOP))
    return 11;
  // FIXME: handle 32-bit mode
  // 15-bytes is the longest single NOP instruction, but 10-bytes is
  // commonly the longest that can be efficiently decoded.
  return 10;
}

/// Write a sequence of optimal nops to the output, covering \p Count
/// bytes.
/// \return - true on success, false on failure
bool X86AsmBackend::writeNopData(raw_ostream &OS, uint64_t Count,
                                 const MCSubtargetInfo *STI) const {
  // Write 1 or 2 byte NOP sequences, or a longer trapsled, until
  // we have written Count bytes
  do {
    const uint8_t ThisNopLength = (uint8_t) std::min(Count, (uint64_t)127);
    switch (ThisNopLength) {
      case 0: break;
      case 1: OS << '\x90';
              break;
      case 2: OS << '\x66';
              OS << '\x90';
              break;
      default: OS << '\xEB';
               OS << (uint8_t)(ThisNopLength - 2);
               for(uint8_t i = 2; i < ThisNopLength; ++i)
                 OS << '\xCC';
    }
    Count -= ThisNopLength;
  } while (Count != 0);

  return true;
}

/* *** */

namespace {

class ELFX86AsmBackend : public X86AsmBackend {
public:
  uint8_t OSABI;
  ELFX86AsmBackend(const Target &T, uint8_t OSABI, const MCSubtargetInfo &STI)
      : X86AsmBackend(T, STI), OSABI(OSABI) {}
};

class ELFX86_32AsmBackend : public ELFX86AsmBackend {
public:
  ELFX86_32AsmBackend(const Target &T, uint8_t OSABI,
                      const MCSubtargetInfo &STI)
    : ELFX86AsmBackend(T, OSABI, STI) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createX86ELFObjectWriter(/*IsELF64*/ false, OSABI, ELF::EM_386);
  }
};

class ELFX86_X32AsmBackend : public ELFX86AsmBackend {
public:
  ELFX86_X32AsmBackend(const Target &T, uint8_t OSABI,
                       const MCSubtargetInfo &STI)
      : ELFX86AsmBackend(T, OSABI, STI) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createX86ELFObjectWriter(/*IsELF64*/ false, OSABI,
                                    ELF::EM_X86_64);
  }
};

class ELFX86_IAMCUAsmBackend : public ELFX86AsmBackend {
public:
  ELFX86_IAMCUAsmBackend(const Target &T, uint8_t OSABI,
                         const MCSubtargetInfo &STI)
      : ELFX86AsmBackend(T, OSABI, STI) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createX86ELFObjectWriter(/*IsELF64*/ false, OSABI,
                                    ELF::EM_IAMCU);
  }
};

class ELFX86_64AsmBackend : public ELFX86AsmBackend {
public:
  ELFX86_64AsmBackend(const Target &T, uint8_t OSABI,
                      const MCSubtargetInfo &STI)
    : ELFX86AsmBackend(T, OSABI, STI) {}

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createX86ELFObjectWriter(/*IsELF64*/ true, OSABI, ELF::EM_X86_64);
  }
};

class WindowsX86AsmBackend : public X86AsmBackend {
  bool Is64Bit;

public:
  WindowsX86AsmBackend(const Target &T, bool is64Bit,
                       const MCSubtargetInfo &STI)
    : X86AsmBackend(T, STI)
    , Is64Bit(is64Bit) {
  }

  std::optional<MCFixupKind> getFixupKind(StringRef Name) const override {
    return StringSwitch<std::optional<MCFixupKind>>(Name)
        .Case("dir32", FK_Data_4)
        .Case("secrel32", FK_SecRel_4)
        .Case("secidx", FK_SecRel_2)
        .Default(MCAsmBackend::getFixupKind(Name));
  }

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    return createX86WinCOFFObjectWriter(Is64Bit);
  }
};

namespace CU {

  /// Compact unwind encoding values.
  enum CompactUnwindEncodings {
    /// [RE]BP based frame where [RE]BP is pused on the stack immediately after
    /// the return address, then [RE]SP is moved to [RE]BP.
    UNWIND_MODE_BP_FRAME                   = 0x01000000,

    /// A frameless function with a small constant stack size.
    UNWIND_MODE_STACK_IMMD                 = 0x02000000,

    /// A frameless function with a large constant stack size.
    UNWIND_MODE_STACK_IND                  = 0x03000000,

    /// No compact unwind encoding is available.
    UNWIND_MODE_DWARF                      = 0x04000000,

    /// Mask for encoding the frame registers.
    UNWIND_BP_FRAME_REGISTERS              = 0x00007FFF,

    /// Mask for encoding the frameless registers.
    UNWIND_FRAMELESS_STACK_REG_PERMUTATION = 0x000003FF
  };

} // namespace CU

class DarwinX86AsmBackend : public X86AsmBackend {
  const MCRegisterInfo &MRI;

  /// Number of registers that can be saved in a compact unwind encoding.
  enum { CU_NUM_SAVED_REGS = 6 };

  mutable unsigned SavedRegs[CU_NUM_SAVED_REGS];
  Triple TT;
  bool Is64Bit;

  unsigned OffsetSize;                   ///< Offset of a "push" instruction.
  unsigned MoveInstrSize;                ///< Size of a "move" instruction.
  unsigned StackDivide;                  ///< Amount to adjust stack size by.
protected:
  /// Size of a "push" instruction for the given register.
  unsigned PushInstrSize(unsigned Reg) const {
    switch (Reg) {
      case X86::EBX:
      case X86::ECX:
      case X86::EDX:
      case X86::EDI:
      case X86::ESI:
      case X86::EBP:
      case X86::RBX:
      case X86::RBP:
        return 1;
      case X86::R12:
      case X86::R13:
      case X86::R14:
      case X86::R15:
        return 2;
    }
    return 1;
  }

private:
  /// Get the compact unwind number for a given register. The number
  /// corresponds to the enum lists in compact_unwind_encoding.h.
  int getCompactUnwindRegNum(unsigned Reg) const {
    static const MCPhysReg CU32BitRegs[7] = {
      X86::EBX, X86::ECX, X86::EDX, X86::EDI, X86::ESI, X86::EBP, 0
    };
    static const MCPhysReg CU64BitRegs[] = {
      X86::RBX, X86::R12, X86::R13, X86::R14, X86::R15, X86::RBP, 0
    };
    const MCPhysReg *CURegs = Is64Bit ? CU64BitRegs : CU32BitRegs;
    for (int Idx = 1; *CURegs; ++CURegs, ++Idx)
      if (*CURegs == Reg)
        return Idx;

    return -1;
  }

  /// Return the registers encoded for a compact encoding with a frame
  /// pointer.
  uint32_t encodeCompactUnwindRegistersWithFrame() const {
    // Encode the registers in the order they were saved --- 3-bits per
    // register. The list of saved registers is assumed to be in reverse
    // order. The registers are numbered from 1 to CU_NUM_SAVED_REGS.
    uint32_t RegEnc = 0;
    for (int i = 0, Idx = 0; i != CU_NUM_SAVED_REGS; ++i) {
      unsigned Reg = SavedRegs[i];
      if (Reg == 0) break;

      int CURegNum = getCompactUnwindRegNum(Reg);
      if (CURegNum == -1) return ~0U;

      // Encode the 3-bit register number in order, skipping over 3-bits for
      // each register.
      RegEnc |= (CURegNum & 0x7) << (Idx++ * 3);
    }

    assert((RegEnc & 0x3FFFF) == RegEnc &&
           "Invalid compact register encoding!");
    return RegEnc;
  }

  /// Create the permutation encoding used with frameless stacks. It is
  /// passed the number of registers to be saved and an array of the registers
  /// saved.
  uint32_t encodeCompactUnwindRegistersWithoutFrame(unsigned RegCount) const {
    // The saved registers are numbered from 1 to 6. In order to encode the
    // order in which they were saved, we re-number them according to their
    // place in the register order. The re-numbering is relative to the last
    // re-numbered register. E.g., if we have registers {6, 2, 4, 5} saved in
    // that order:
    //
    //    Orig  Re-Num
    //    ----  ------
    //     6       6
    //     2       2
    //     4       3
    //     5       3
    //
    for (unsigned i = 0; i < RegCount; ++i) {
      int CUReg = getCompactUnwindRegNum(SavedRegs[i]);
      if (CUReg == -1) return ~0U;
      SavedRegs[i] = CUReg;
    }

    // Reverse the list.
    std::reverse(&SavedRegs[0], &SavedRegs[CU_NUM_SAVED_REGS]);

    uint32_t RenumRegs[CU_NUM_SAVED_REGS];
    for (unsigned i = CU_NUM_SAVED_REGS - RegCount; i < CU_NUM_SAVED_REGS; ++i){
      unsigned Countless = 0;
      for (unsigned j = CU_NUM_SAVED_REGS - RegCount; j < i; ++j)
        if (SavedRegs[j] < SavedRegs[i])
          ++Countless;

      RenumRegs[i] = SavedRegs[i] - Countless - 1;
    }

    // Take the renumbered values and encode them into a 10-bit number.
    uint32_t permutationEncoding = 0;
    switch (RegCount) {
    case 6:
      permutationEncoding |= 120 * RenumRegs[0] + 24 * RenumRegs[1]
                             + 6 * RenumRegs[2] +  2 * RenumRegs[3]
                             +     RenumRegs[4];
      break;
    case 5:
      permutationEncoding |= 120 * RenumRegs[1] + 24 * RenumRegs[2]
                             + 6 * RenumRegs[3] +  2 * RenumRegs[4]
                             +     RenumRegs[5];
      break;
    case 4:
      permutationEncoding |=  60 * RenumRegs[2] + 12 * RenumRegs[3]
                             + 3 * RenumRegs[4] +      RenumRegs[5];
      break;
    case 3:
      permutationEncoding |=  20 * RenumRegs[3] +  4 * RenumRegs[4]
                             +     RenumRegs[5];
      break;
    case 2:
      permutationEncoding |=   5 * RenumRegs[4] +      RenumRegs[5];
      break;
    case 1:
      permutationEncoding |=       RenumRegs[5];
      break;
    }

    assert((permutationEncoding & 0x3FF) == permutationEncoding &&
           "Invalid compact register encoding!");
    return permutationEncoding;
  }

public:
  DarwinX86AsmBackend(const Target &T, const MCRegisterInfo &MRI,
                      const MCSubtargetInfo &STI)
      : X86AsmBackend(T, STI), MRI(MRI), TT(STI.getTargetTriple()),
        Is64Bit(TT.isArch64Bit()) {
    memset(SavedRegs, 0, sizeof(SavedRegs));
    OffsetSize = Is64Bit ? 8 : 4;
    MoveInstrSize = Is64Bit ? 3 : 2;
    StackDivide = Is64Bit ? 8 : 4;
  }

  std::unique_ptr<MCObjectTargetWriter>
  createObjectTargetWriter() const override {
    uint32_t CPUType = cantFail(MachO::getCPUType(TT));
    uint32_t CPUSubType = cantFail(MachO::getCPUSubType(TT));
    return createX86MachObjectWriter(Is64Bit, CPUType, CPUSubType);
  }

  /// Implementation of algorithm to generate the compact unwind encoding
  /// for the CFI instructions.
  uint64_t generateCompactUnwindEncoding(const MCDwarfFrameInfo *FI,
                                         const MCContext *Ctxt) const override {
    ArrayRef<MCCFIInstruction> Instrs = FI->Instructions;
    if (Instrs.empty()) return 0;
    if (!isDarwinCanonicalPersonality(FI->Personality) &&
        !Ctxt->emitCompactUnwindNonCanonical())
      return CU::UNWIND_MODE_DWARF;

    // Reset the saved registers.
    unsigned SavedRegIdx = 0;
    memset(SavedRegs, 0, sizeof(SavedRegs));

    bool HasFP = false;

    // Encode that we are using EBP/RBP as the frame pointer.
    uint64_t CompactUnwindEncoding = 0;

    unsigned SubtractInstrIdx = Is64Bit ? 3 : 2;
    unsigned InstrOffset = 0;
    unsigned StackAdjust = 0;
    uint64_t StackSize = 0;
    int64_t MinAbsOffset = std::numeric_limits<int64_t>::max();

    for (const MCCFIInstruction &Inst : Instrs) {
      switch (Inst.getOperation()) {
      default:
        // Any other CFI directives indicate a frame that we aren't prepared
        // to represent via compact unwind, so just bail out.
        return CU::UNWIND_MODE_DWARF;
      case MCCFIInstruction::OpDefCfaRegister: {
        // Defines a frame pointer. E.g.
        //
        //     movq %rsp, %rbp
        //  L0:
        //     .cfi_def_cfa_register %rbp
        //
        HasFP = true;

        // If the frame pointer is other than esp/rsp, we do not have a way to
        // generate a compact unwinding representation, so bail out.
        if (*MRI.getLLVMRegNum(Inst.getRegister(), true) !=
            (Is64Bit ? X86::RBP : X86::EBP))
          return CU::UNWIND_MODE_DWARF;

        // Reset the counts.
        memset(SavedRegs, 0, sizeof(SavedRegs));
        StackAdjust = 0;
        SavedRegIdx = 0;
        MinAbsOffset = std::numeric_limits<int64_t>::max();
        InstrOffset += MoveInstrSize;
        break;
      }
      case MCCFIInstruction::OpDefCfaOffset: {
        // Defines a new offset for the CFA. E.g.
        //
        //  With frame:
        //
        //     pushq %rbp
        //  L0:
        //     .cfi_def_cfa_offset 16
        //
        //  Without frame:
        //
        //     subq $72, %rsp
        //  L0:
        //     .cfi_def_cfa_offset 80
        //
        StackSize = Inst.getOffset() / StackDivide;
        break;
      }
      case MCCFIInstruction::OpOffset: {
        // Defines a "push" of a callee-saved register. E.g.
        //
        //     pushq %r15
        //     pushq %r14
        //     pushq %rbx
        //  L0:
        //     subq $120, %rsp
        //  L1:
        //     .cfi_offset %rbx, -40
        //     .cfi_offset %r14, -32
        //     .cfi_offset %r15, -24
        //
        if (SavedRegIdx == CU_NUM_SAVED_REGS)
          // If there are too many saved registers, we cannot use a compact
          // unwind encoding.
          return CU::UNWIND_MODE_DWARF;

        unsigned Reg = *MRI.getLLVMRegNum(Inst.getRegister(), true);
        SavedRegs[SavedRegIdx++] = Reg;
        StackAdjust += OffsetSize;
        MinAbsOffset = std::min(MinAbsOffset, std::abs(Inst.getOffset()));
        InstrOffset += PushInstrSize(Reg);
        break;
      }
      }
    }

    StackAdjust /= StackDivide;

    if (HasFP) {
      if ((StackAdjust & 0xFF) != StackAdjust)
        // Offset was too big for a compact unwind encoding.
        return CU::UNWIND_MODE_DWARF;

      // We don't attempt to track a real StackAdjust, so if the saved registers
      // aren't adjacent to rbp we can't cope.
      if (SavedRegIdx != 0 && MinAbsOffset != 3 * (int)OffsetSize)
        return CU::UNWIND_MODE_DWARF;

      // Get the encoding of the saved registers when we have a frame pointer.
      uint32_t RegEnc = encodeCompactUnwindRegistersWithFrame();
      if (RegEnc == ~0U) return CU::UNWIND_MODE_DWARF;

      CompactUnwindEncoding |= CU::UNWIND_MODE_BP_FRAME;
      CompactUnwindEncoding |= (StackAdjust & 0xFF) << 16;
      CompactUnwindEncoding |= RegEnc & CU::UNWIND_BP_FRAME_REGISTERS;
    } else {
      SubtractInstrIdx += InstrOffset;
      ++StackAdjust;

      if ((StackSize & 0xFF) == StackSize) {
        // Frameless stack with a small stack size.
        CompactUnwindEncoding |= CU::UNWIND_MODE_STACK_IMMD;

        // Encode the stack size.
        CompactUnwindEncoding |= (StackSize & 0xFF) << 16;
      } else {
        if ((StackAdjust & 0x7) != StackAdjust)
          // The extra stack adjustments are too big for us to handle.
          return CU::UNWIND_MODE_DWARF;

        // Frameless stack with an offset too large for us to encode compactly.
        CompactUnwindEncoding |= CU::UNWIND_MODE_STACK_IND;

        // Encode the offset to the nnnnnn value in the 'subl $nnnnnn, ESP'
        // instruction.
        CompactUnwindEncoding |= (SubtractInstrIdx & 0xFF) << 16;

        // Encode any extra stack adjustments (done via push instructions).
        CompactUnwindEncoding |= (StackAdjust & 0x7) << 13;
      }

      // Encode the number of registers saved. (Reverse the list first.)
      std::reverse(&SavedRegs[0], &SavedRegs[SavedRegIdx]);
      CompactUnwindEncoding |= (SavedRegIdx & 0x7) << 10;

      // Get the encoding of the saved registers when we don't have a frame
      // pointer.
      uint32_t RegEnc = encodeCompactUnwindRegistersWithoutFrame(SavedRegIdx);
      if (RegEnc == ~0U) return CU::UNWIND_MODE_DWARF;

      // Encode the register encoding.
      CompactUnwindEncoding |=
        RegEnc & CU::UNWIND_FRAMELESS_STACK_REG_PERMUTATION;
    }

    return CompactUnwindEncoding;
  }
};

} // end anonymous namespace

MCAsmBackend *llvm::createX86_32AsmBackend(const Target &T,
                                           const MCSubtargetInfo &STI,
                                           const MCRegisterInfo &MRI,
                                           const MCTargetOptions &Options) {
  const Triple &TheTriple = STI.getTargetTriple();
  if (TheTriple.isOSBinFormatMachO())
    return new DarwinX86AsmBackend(T, MRI, STI);

  if (TheTriple.isOSWindows() && TheTriple.isOSBinFormatCOFF())
    return new WindowsX86AsmBackend(T, false, STI);

  uint8_t OSABI = MCELFObjectTargetWriter::getOSABI(TheTriple.getOS());

  if (TheTriple.isOSIAMCU())
    return new ELFX86_IAMCUAsmBackend(T, OSABI, STI);

  return new ELFX86_32AsmBackend(T, OSABI, STI);
}

MCAsmBackend *llvm::createX86_64AsmBackend(const Target &T,
                                           const MCSubtargetInfo &STI,
                                           const MCRegisterInfo &MRI,
                                           const MCTargetOptions &Options) {
  const Triple &TheTriple = STI.getTargetTriple();
  if (TheTriple.isOSBinFormatMachO())
    return new DarwinX86AsmBackend(T, MRI, STI);

  if (TheTriple.isOSWindows() && TheTriple.isOSBinFormatCOFF())
    return new WindowsX86AsmBackend(T, true, STI);

  if (TheTriple.isUEFI()) {
    assert(TheTriple.isOSBinFormatCOFF() &&
         "Only COFF format is supported in UEFI environment.");
    return new WindowsX86AsmBackend(T, true, STI);
  }

  uint8_t OSABI = MCELFObjectTargetWriter::getOSABI(TheTriple.getOS());

  if (TheTriple.isX32())
    return new ELFX86_X32AsmBackend(T, OSABI, STI);
  return new ELFX86_64AsmBackend(T, OSABI, STI);
}

namespace {
class X86ELFStreamer : public MCELFStreamer {
public:
  X86ELFStreamer(MCContext &Context, std::unique_ptr<MCAsmBackend> TAB,
                 std::unique_ptr<MCObjectWriter> OW,
                 std::unique_ptr<MCCodeEmitter> Emitter)
      : MCELFStreamer(Context, std::move(TAB), std::move(OW),
                      std::move(Emitter)) {}

  void emitInstruction(const MCInst &Inst, const MCSubtargetInfo &STI) override;
};
} // end anonymous namespace

void X86_MC::emitInstruction(MCObjectStreamer &S, const MCInst &Inst,
                             const MCSubtargetInfo &STI) {
  auto &Backend = static_cast<X86AsmBackend &>(S.getAssembler().getBackend());
  Backend.emitInstructionBegin(S, Inst, STI);
  S.MCObjectStreamer::emitInstruction(Inst, STI);
  Backend.emitInstructionEnd(S, Inst);
}

void X86ELFStreamer::emitInstruction(const MCInst &Inst,
                                     const MCSubtargetInfo &STI) {
  X86_MC::emitInstruction(*this, Inst, STI);
}

MCStreamer *llvm::createX86ELFStreamer(const Triple &T, MCContext &Context,
                                       std::unique_ptr<MCAsmBackend> &&MAB,
                                       std::unique_ptr<MCObjectWriter> &&MOW,
                                       std::unique_ptr<MCCodeEmitter> &&MCE) {
  return new X86ELFStreamer(Context, std::move(MAB), std::move(MOW),
                            std::move(MCE));
}
