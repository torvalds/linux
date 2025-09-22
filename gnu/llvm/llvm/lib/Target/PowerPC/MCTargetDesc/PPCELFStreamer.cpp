//===-------- PPCELFStreamer.cpp - ELF Object Output ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a custom MCELFStreamer for PowerPC.
//
// The purpose of the custom ELF streamer is to allow us to intercept
// instructions as they are being emitted and align all 8 byte instructions
// to a 64 byte boundary if required (by adding a 4 byte nop). This is important
// because 8 byte instructions are not allowed to cross 64 byte boundaries
// and by aliging anything that is within 4 bytes of the boundary we can
// guarantee that the 8 byte instructions do not cross that boundary.
//
//===----------------------------------------------------------------------===//

#include "PPCELFStreamer.h"
#include "PPCFixupKinds.h"
#include "PPCMCCodeEmitter.h"
#include "PPCMCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCSymbolELF.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/SourceMgr.h"

using namespace llvm;

PPCELFStreamer::PPCELFStreamer(MCContext &Context,
                               std::unique_ptr<MCAsmBackend> MAB,
                               std::unique_ptr<MCObjectWriter> OW,
                               std::unique_ptr<MCCodeEmitter> Emitter)
    : MCELFStreamer(Context, std::move(MAB), std::move(OW), std::move(Emitter)),
      LastLabel(nullptr) {}

void PPCELFStreamer::emitPrefixedInstruction(const MCInst &Inst,
                                             const MCSubtargetInfo &STI) {
  // Prefixed instructions must not cross a 64-byte boundary (i.e. prefix is
  // before the boundary and the remaining 4-bytes are after the boundary). In
  // order to achieve this, a nop is added prior to any such boundary-crossing
  // prefixed instruction. Align to 64 bytes if possible but add a maximum of 4
  // bytes when trying to do that. If alignment requires adding more than 4
  // bytes then the instruction won't be aligned. When emitting a code alignment
  // a new fragment is created for this alignment. This fragment will contain
  // all of the nops required as part of the alignment operation. In the cases
  // when no nops are added then The fragment is still created but it remains
  // empty.
  emitCodeAlignment(Align(64), &STI, 4);

  // Emit the instruction.
  // Since the previous emit created a new fragment then adding this instruction
  // also forces the addition of a new fragment. Inst is now the first
  // instruction in that new fragment.
  MCELFStreamer::emitInstruction(Inst, STI);

  // The above instruction is forced to start a new fragment because it
  // comes after a code alignment fragment. Get that new fragment.
  MCFragment *InstructionFragment = getCurrentFragment();
  SMLoc InstLoc = Inst.getLoc();
  // Check if there was a last label emitted.
  if (LastLabel && !LastLabel->isUnset() && LastLabelLoc.isValid() &&
      InstLoc.isValid()) {
    const SourceMgr *SourceManager = getContext().getSourceManager();
    unsigned InstLine = SourceManager->FindLineNumber(InstLoc);
    unsigned LabelLine = SourceManager->FindLineNumber(LastLabelLoc);
    // If the Label and the Instruction are on the same line then move the
    // label to the top of the fragment containing the aligned instruction that
    // was just added.
    if (InstLine == LabelLine) {
      LastLabel->setFragment(InstructionFragment);
      LastLabel->setOffset(0);
    }
  }
}

void PPCELFStreamer::emitInstruction(const MCInst &Inst,
                                     const MCSubtargetInfo &STI) {
  PPCMCCodeEmitter *Emitter =
      static_cast<PPCMCCodeEmitter*>(getAssembler().getEmitterPtr());

  // If the instruction is a part of the GOT to PC-Rel link time optimization
  // instruction pair, return a value, otherwise return std::nullopt. A true
  // returned value means the instruction is the PLDpc and a false value means
  // it is the user instruction.
  std::optional<bool> IsPartOfGOTToPCRelPair =
      isPartOfGOTToPCRelPair(Inst, STI);

  // User of the GOT-indirect address.
  // For example, the load that will get the relocation as follows:
  // .reloc .Lpcrel1-8,R_PPC64_PCREL_OPT,.-(.Lpcrel1-8)
  //  lwa 3, 4(3)
  if (IsPartOfGOTToPCRelPair && !*IsPartOfGOTToPCRelPair)
    emitGOTToPCRelReloc(Inst);

  // Special handling is only for prefixed instructions.
  if (!Emitter->isPrefixedInstruction(Inst)) {
    MCELFStreamer::emitInstruction(Inst, STI);
    return;
  }
  emitPrefixedInstruction(Inst, STI);

  // Producer of the GOT-indirect address.
  // For example, the prefixed load from the got that will get the label as
  // follows:
  //  pld 3, vec@got@pcrel(0), 1
  // .Lpcrel1:
  if (IsPartOfGOTToPCRelPair && *IsPartOfGOTToPCRelPair)
    emitGOTToPCRelLabel(Inst);
}

void PPCELFStreamer::emitLabel(MCSymbol *Symbol, SMLoc Loc) {
  LastLabel = Symbol;
  LastLabelLoc = Loc;
  MCELFStreamer::emitLabel(Symbol);
}

// This linker time GOT PC Relative optimization relocation will look like this:
//   pld <reg> symbol@got@pcrel
// <Label###>:
//   .reloc Label###-8,R_PPC64_PCREL_OPT,.-(Label###-8)
//   load <loadedreg>, 0(<reg>)
// The reason we place the label after the PLDpc instruction is that there
// may be an alignment nop before it since prefixed instructions must not
// cross a 64-byte boundary (please see
// PPCELFStreamer::emitPrefixedInstruction()). When referring to the
// label, we subtract the width of a prefixed instruction (8 bytes) to ensure
// we refer to the PLDpc.
void PPCELFStreamer::emitGOTToPCRelReloc(const MCInst &Inst) {
  // Get the last operand which contains the symbol.
  const MCOperand &Operand = Inst.getOperand(Inst.getNumOperands() - 1);
  assert(Operand.isExpr() && "Expecting an MCExpr.");
  // Cast the last operand to MCSymbolRefExpr to get the symbol.
  const MCExpr *Expr = Operand.getExpr();
  const MCSymbolRefExpr *SymExpr = static_cast<const MCSymbolRefExpr *>(Expr);
  assert(SymExpr->getKind() == MCSymbolRefExpr::VK_PPC_PCREL_OPT &&
         "Expecting a symbol of type VK_PPC_PCREL_OPT");
  MCSymbol *LabelSym =
      getContext().getOrCreateSymbol(SymExpr->getSymbol().getName());
  const MCExpr *LabelExpr = MCSymbolRefExpr::create(LabelSym, getContext());
  const MCExpr *Eight = MCConstantExpr::create(8, getContext());
  // SubExpr is just Label###-8
  const MCExpr *SubExpr =
      MCBinaryExpr::createSub(LabelExpr, Eight, getContext());
  MCSymbol *CurrentLocation = getContext().createTempSymbol();
  const MCExpr *CurrentLocationExpr =
      MCSymbolRefExpr::create(CurrentLocation, getContext());
  // SubExpr2 is .-(Label###-8)
  const MCExpr *SubExpr2 =
      MCBinaryExpr::createSub(CurrentLocationExpr, SubExpr, getContext());

  MCDataFragment *DF = static_cast<MCDataFragment *>(LabelSym->getFragment());
  assert(DF && "Expecting a valid data fragment.");
  MCFixupKind FixupKind = static_cast<MCFixupKind>(FirstLiteralRelocationKind +
                                                   ELF::R_PPC64_PCREL_OPT);
  DF->getFixups().push_back(
      MCFixup::create(LabelSym->getOffset() - 8, SubExpr2,
                      FixupKind, Inst.getLoc()));
  emitLabel(CurrentLocation, Inst.getLoc());
}

// Emit the label that immediately follows the PLDpc for a link time GOT PC Rel
// optimization.
void PPCELFStreamer::emitGOTToPCRelLabel(const MCInst &Inst) {
  // Get the last operand which contains the symbol.
  const MCOperand &Operand = Inst.getOperand(Inst.getNumOperands() - 1);
  assert(Operand.isExpr() && "Expecting an MCExpr.");
  // Cast the last operand to MCSymbolRefExpr to get the symbol.
  const MCExpr *Expr = Operand.getExpr();
  const MCSymbolRefExpr *SymExpr = static_cast<const MCSymbolRefExpr *>(Expr);
  assert(SymExpr->getKind() == MCSymbolRefExpr::VK_PPC_PCREL_OPT &&
         "Expecting a symbol of type VK_PPC_PCREL_OPT");
  MCSymbol *LabelSym =
      getContext().getOrCreateSymbol(SymExpr->getSymbol().getName());
  emitLabel(LabelSym, Inst.getLoc());
}

// This function checks if the parameter Inst is part of the setup for a link
// time GOT PC Relative optimization. For example in this situation:
// <MCInst PLDpc <MCOperand Reg:282> <MCOperand Expr:(glob_double@got@pcrel)>
//   <MCOperand Imm:0> <MCOperand Expr:(.Lpcrel@<<invalid>>)>>
// <MCInst SOME_LOAD <MCOperand Reg:22> <MCOperand Imm:0> <MCOperand Reg:282>
//   <MCOperand Expr:(.Lpcrel@<<invalid>>)>>
// The above is a pair of such instructions and this function will not return
// std::nullopt for either one of them. In both cases we are looking for the
// last operand <MCOperand Expr:(.Lpcrel@<<invalid>>)> which needs to be an
// MCExpr and has the flag MCSymbolRefExpr::VK_PPC_PCREL_OPT. After that we just
// look at the opcode and in the case of PLDpc we will return true. For the load
// (or store) this function will return false indicating it has found the second
// instruciton in the pair.
std::optional<bool> llvm::isPartOfGOTToPCRelPair(const MCInst &Inst,
                                                 const MCSubtargetInfo &STI) {
  // Need at least two operands.
  if (Inst.getNumOperands() < 2)
    return std::nullopt;

  unsigned LastOp = Inst.getNumOperands() - 1;
  // The last operand needs to be an MCExpr and it needs to have a variant kind
  // of VK_PPC_PCREL_OPT. If it does not satisfy these conditions it is not a
  // link time GOT PC Rel opt instruction and we can ignore it and return
  // std::nullopt.
  const MCOperand &Operand = Inst.getOperand(LastOp);
  if (!Operand.isExpr())
    return std::nullopt;

  // Check for the variant kind VK_PPC_PCREL_OPT in this expression.
  const MCExpr *Expr = Operand.getExpr();
  const MCSymbolRefExpr *SymExpr = static_cast<const MCSymbolRefExpr *>(Expr);
  if (!SymExpr || SymExpr->getKind() != MCSymbolRefExpr::VK_PPC_PCREL_OPT)
    return std::nullopt;

  return (Inst.getOpcode() == PPC::PLDpc);
}

MCELFStreamer *llvm::createPPCELFStreamer(
    MCContext &Context, std::unique_ptr<MCAsmBackend> MAB,
    std::unique_ptr<MCObjectWriter> OW,
    std::unique_ptr<MCCodeEmitter> Emitter) {
  return new PPCELFStreamer(Context, std::move(MAB), std::move(OW),
                            std::move(Emitter));
}
