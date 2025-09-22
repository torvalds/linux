//===- lib/MC/MCWin64EH.cpp - MCWin64EH implementation --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/MC/MCWin64EH.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Win64EH.h"
namespace llvm {
class MCSection;
}

using namespace llvm;

// NOTE: All relocations generated here are 4-byte image-relative.

static uint8_t CountOfUnwindCodes(std::vector<WinEH::Instruction> &Insns) {
  uint8_t Count = 0;
  for (const auto &I : Insns) {
    switch (static_cast<Win64EH::UnwindOpcodes>(I.Operation)) {
    default:
      llvm_unreachable("Unsupported unwind code");
    case Win64EH::UOP_PushNonVol:
    case Win64EH::UOP_AllocSmall:
    case Win64EH::UOP_SetFPReg:
    case Win64EH::UOP_PushMachFrame:
      Count += 1;
      break;
    case Win64EH::UOP_SaveNonVol:
    case Win64EH::UOP_SaveXMM128:
      Count += 2;
      break;
    case Win64EH::UOP_SaveNonVolBig:
    case Win64EH::UOP_SaveXMM128Big:
      Count += 3;
      break;
    case Win64EH::UOP_AllocLarge:
      Count += (I.Offset > 512 * 1024 - 8) ? 3 : 2;
      break;
    }
  }
  return Count;
}

static void EmitAbsDifference(MCStreamer &Streamer, const MCSymbol *LHS,
                              const MCSymbol *RHS) {
  MCContext &Context = Streamer.getContext();
  const MCExpr *Diff =
      MCBinaryExpr::createSub(MCSymbolRefExpr::create(LHS, Context),
                              MCSymbolRefExpr::create(RHS, Context), Context);
  Streamer.emitValue(Diff, 1);
}

static void EmitUnwindCode(MCStreamer &streamer, const MCSymbol *begin,
                           WinEH::Instruction &inst) {
  uint8_t b2;
  uint16_t w;
  b2 = (inst.Operation & 0x0F);
  switch (static_cast<Win64EH::UnwindOpcodes>(inst.Operation)) {
  default:
    llvm_unreachable("Unsupported unwind code");
  case Win64EH::UOP_PushNonVol:
    EmitAbsDifference(streamer, inst.Label, begin);
    b2 |= (inst.Register & 0x0F) << 4;
    streamer.emitInt8(b2);
    break;
  case Win64EH::UOP_AllocLarge:
    EmitAbsDifference(streamer, inst.Label, begin);
    if (inst.Offset > 512 * 1024 - 8) {
      b2 |= 0x10;
      streamer.emitInt8(b2);
      w = inst.Offset & 0xFFF8;
      streamer.emitInt16(w);
      w = inst.Offset >> 16;
    } else {
      streamer.emitInt8(b2);
      w = inst.Offset >> 3;
    }
    streamer.emitInt16(w);
    break;
  case Win64EH::UOP_AllocSmall:
    b2 |= (((inst.Offset - 8) >> 3) & 0x0F) << 4;
    EmitAbsDifference(streamer, inst.Label, begin);
    streamer.emitInt8(b2);
    break;
  case Win64EH::UOP_SetFPReg:
    EmitAbsDifference(streamer, inst.Label, begin);
    streamer.emitInt8(b2);
    break;
  case Win64EH::UOP_SaveNonVol:
  case Win64EH::UOP_SaveXMM128:
    b2 |= (inst.Register & 0x0F) << 4;
    EmitAbsDifference(streamer, inst.Label, begin);
    streamer.emitInt8(b2);
    w = inst.Offset >> 3;
    if (inst.Operation == Win64EH::UOP_SaveXMM128)
      w >>= 1;
    streamer.emitInt16(w);
    break;
  case Win64EH::UOP_SaveNonVolBig:
  case Win64EH::UOP_SaveXMM128Big:
    b2 |= (inst.Register & 0x0F) << 4;
    EmitAbsDifference(streamer, inst.Label, begin);
    streamer.emitInt8(b2);
    if (inst.Operation == Win64EH::UOP_SaveXMM128Big)
      w = inst.Offset & 0xFFF0;
    else
      w = inst.Offset & 0xFFF8;
    streamer.emitInt16(w);
    w = inst.Offset >> 16;
    streamer.emitInt16(w);
    break;
  case Win64EH::UOP_PushMachFrame:
    if (inst.Offset == 1)
      b2 |= 0x10;
    EmitAbsDifference(streamer, inst.Label, begin);
    streamer.emitInt8(b2);
    break;
  }
}

static void EmitSymbolRefWithOfs(MCStreamer &streamer,
                                 const MCSymbol *Base,
                                 int64_t Offset) {
  MCContext &Context = streamer.getContext();
  const MCConstantExpr *OffExpr = MCConstantExpr::create(Offset, Context);
  const MCSymbolRefExpr *BaseRefRel = MCSymbolRefExpr::create(Base,
                                              MCSymbolRefExpr::VK_COFF_IMGREL32,
                                              Context);
  streamer.emitValue(MCBinaryExpr::createAdd(BaseRefRel, OffExpr, Context), 4);
}

static void EmitSymbolRefWithOfs(MCStreamer &streamer,
                                 const MCSymbol *Base,
                                 const MCSymbol *Other) {
  MCContext &Context = streamer.getContext();
  const MCSymbolRefExpr *BaseRef = MCSymbolRefExpr::create(Base, Context);
  const MCSymbolRefExpr *OtherRef = MCSymbolRefExpr::create(Other, Context);
  const MCExpr *Ofs = MCBinaryExpr::createSub(OtherRef, BaseRef, Context);
  const MCSymbolRefExpr *BaseRefRel = MCSymbolRefExpr::create(Base,
                                              MCSymbolRefExpr::VK_COFF_IMGREL32,
                                              Context);
  streamer.emitValue(MCBinaryExpr::createAdd(BaseRefRel, Ofs, Context), 4);
}

static void EmitRuntimeFunction(MCStreamer &streamer,
                                const WinEH::FrameInfo *info) {
  MCContext &context = streamer.getContext();

  streamer.emitValueToAlignment(Align(4));
  EmitSymbolRefWithOfs(streamer, info->Begin, info->Begin);
  EmitSymbolRefWithOfs(streamer, info->Begin, info->End);
  streamer.emitValue(MCSymbolRefExpr::create(info->Symbol,
                                             MCSymbolRefExpr::VK_COFF_IMGREL32,
                                             context), 4);
}

static void EmitUnwindInfo(MCStreamer &streamer, WinEH::FrameInfo *info) {
  // If this UNWIND_INFO already has a symbol, it's already been emitted.
  if (info->Symbol)
    return;

  MCContext &context = streamer.getContext();
  MCSymbol *Label = context.createTempSymbol();

  streamer.emitValueToAlignment(Align(4));
  streamer.emitLabel(Label);
  info->Symbol = Label;

  // Upper 3 bits are the version number (currently 1).
  uint8_t flags = 0x01;
  if (info->ChainedParent)
    flags |= Win64EH::UNW_ChainInfo << 3;
  else {
    if (info->HandlesUnwind)
      flags |= Win64EH::UNW_TerminateHandler << 3;
    if (info->HandlesExceptions)
      flags |= Win64EH::UNW_ExceptionHandler << 3;
  }
  streamer.emitInt8(flags);

  if (info->PrologEnd)
    EmitAbsDifference(streamer, info->PrologEnd, info->Begin);
  else
    streamer.emitInt8(0);

  uint8_t numCodes = CountOfUnwindCodes(info->Instructions);
  streamer.emitInt8(numCodes);

  uint8_t frame = 0;
  if (info->LastFrameInst >= 0) {
    WinEH::Instruction &frameInst = info->Instructions[info->LastFrameInst];
    assert(frameInst.Operation == Win64EH::UOP_SetFPReg);
    frame = (frameInst.Register & 0x0F) | (frameInst.Offset & 0xF0);
  }
  streamer.emitInt8(frame);

  // Emit unwind instructions (in reverse order).
  uint8_t numInst = info->Instructions.size();
  for (uint8_t c = 0; c < numInst; ++c) {
    WinEH::Instruction inst = info->Instructions.back();
    info->Instructions.pop_back();
    EmitUnwindCode(streamer, info->Begin, inst);
  }

  // For alignment purposes, the instruction array will always have an even
  // number of entries, with the final entry potentially unused (in which case
  // the array will be one longer than indicated by the count of unwind codes
  // field).
  if (numCodes & 1) {
    streamer.emitInt16(0);
  }

  if (flags & (Win64EH::UNW_ChainInfo << 3))
    EmitRuntimeFunction(streamer, info->ChainedParent);
  else if (flags &
           ((Win64EH::UNW_TerminateHandler|Win64EH::UNW_ExceptionHandler) << 3))
    streamer.emitValue(MCSymbolRefExpr::create(info->ExceptionHandler,
                                              MCSymbolRefExpr::VK_COFF_IMGREL32,
                                              context), 4);
  else if (numCodes == 0) {
    // The minimum size of an UNWIND_INFO struct is 8 bytes. If we're not
    // a chained unwind info, if there is no handler, and if there are fewer
    // than 2 slots used in the unwind code array, we have to pad to 8 bytes.
    streamer.emitInt32(0);
  }
}

void llvm::Win64EH::UnwindEmitter::Emit(MCStreamer &Streamer) const {
  // Emit the unwind info structs first.
  for (const auto &CFI : Streamer.getWinFrameInfos()) {
    MCSection *XData = Streamer.getAssociatedXDataSection(CFI->TextSection);
    Streamer.switchSection(XData);
    ::EmitUnwindInfo(Streamer, CFI.get());
  }

  // Now emit RUNTIME_FUNCTION entries.
  for (const auto &CFI : Streamer.getWinFrameInfos()) {
    MCSection *PData = Streamer.getAssociatedPDataSection(CFI->TextSection);
    Streamer.switchSection(PData);
    EmitRuntimeFunction(Streamer, CFI.get());
  }
}

void llvm::Win64EH::UnwindEmitter::EmitUnwindInfo(MCStreamer &Streamer,
                                                  WinEH::FrameInfo *info,
                                                  bool HandlerData) const {
  // Switch sections (the static function above is meant to be called from
  // here and from Emit().
  MCSection *XData = Streamer.getAssociatedXDataSection(info->TextSection);
  Streamer.switchSection(XData);

  ::EmitUnwindInfo(Streamer, info);
}

static const MCExpr *GetSubDivExpr(MCStreamer &Streamer, const MCSymbol *LHS,
                                   const MCSymbol *RHS, int Div) {
  MCContext &Context = Streamer.getContext();
  const MCExpr *Expr =
      MCBinaryExpr::createSub(MCSymbolRefExpr::create(LHS, Context),
                              MCSymbolRefExpr::create(RHS, Context), Context);
  if (Div != 1)
    Expr = MCBinaryExpr::createDiv(Expr, MCConstantExpr::create(Div, Context),
                                   Context);
  return Expr;
}

static std::optional<int64_t> GetOptionalAbsDifference(MCStreamer &Streamer,
                                                       const MCSymbol *LHS,
                                                       const MCSymbol *RHS) {
  MCContext &Context = Streamer.getContext();
  const MCExpr *Diff =
      MCBinaryExpr::createSub(MCSymbolRefExpr::create(LHS, Context),
                              MCSymbolRefExpr::create(RHS, Context), Context);
  MCObjectStreamer *OS = (MCObjectStreamer *)(&Streamer);
  // It should normally be possible to calculate the length of a function
  // at this point, but it might not be possible in the presence of certain
  // unusual constructs, like an inline asm with an alignment directive.
  int64_t value;
  if (!Diff->evaluateAsAbsolute(value, OS->getAssembler()))
    return std::nullopt;
  return value;
}

static int64_t GetAbsDifference(MCStreamer &Streamer, const MCSymbol *LHS,
                                const MCSymbol *RHS) {
  std::optional<int64_t> MaybeDiff =
      GetOptionalAbsDifference(Streamer, LHS, RHS);
  if (!MaybeDiff)
    report_fatal_error("Failed to evaluate function length in SEH unwind info");
  return *MaybeDiff;
}

static void checkARM64Instructions(MCStreamer &Streamer,
                                   ArrayRef<WinEH::Instruction> Insns,
                                   const MCSymbol *Begin, const MCSymbol *End,
                                   StringRef Name, StringRef Type) {
  if (!End)
    return;
  std::optional<int64_t> MaybeDistance =
      GetOptionalAbsDifference(Streamer, End, Begin);
  if (!MaybeDistance)
    return;
  uint32_t Distance = (uint32_t)*MaybeDistance;

  for (const auto &I : Insns) {
    switch (static_cast<Win64EH::UnwindOpcodes>(I.Operation)) {
    default:
      break;
    case Win64EH::UOP_TrapFrame:
    case Win64EH::UOP_PushMachFrame:
    case Win64EH::UOP_Context:
    case Win64EH::UOP_ECContext:
    case Win64EH::UOP_ClearUnwoundToCall:
      // Can't reason about these opcodes and how they map to actual
      // instructions.
      return;
    }
  }
  // Exclude the end opcode which doesn't map to an instruction.
  uint32_t InstructionBytes = 4 * (Insns.size() - 1);
  if (Distance != InstructionBytes) {
    Streamer.getContext().reportError(
        SMLoc(), "Incorrect size for " + Name + " " + Type + ": " +
                     Twine(Distance) +
                     " bytes of instructions in range, but .seh directives "
                     "corresponding to " +
                     Twine(InstructionBytes) + " bytes\n");
  }
}

static uint32_t ARM64CountOfUnwindCodes(ArrayRef<WinEH::Instruction> Insns) {
  uint32_t Count = 0;
  for (const auto &I : Insns) {
    switch (static_cast<Win64EH::UnwindOpcodes>(I.Operation)) {
    default:
      llvm_unreachable("Unsupported ARM64 unwind code");
    case Win64EH::UOP_AllocSmall:
      Count += 1;
      break;
    case Win64EH::UOP_AllocMedium:
      Count += 2;
      break;
    case Win64EH::UOP_AllocLarge:
      Count += 4;
      break;
    case Win64EH::UOP_SaveR19R20X:
      Count += 1;
      break;
    case Win64EH::UOP_SaveFPLRX:
      Count += 1;
      break;
    case Win64EH::UOP_SaveFPLR:
      Count += 1;
      break;
    case Win64EH::UOP_SaveReg:
      Count += 2;
      break;
    case Win64EH::UOP_SaveRegP:
      Count += 2;
      break;
    case Win64EH::UOP_SaveRegPX:
      Count += 2;
      break;
    case Win64EH::UOP_SaveRegX:
      Count += 2;
      break;
    case Win64EH::UOP_SaveLRPair:
      Count += 2;
      break;
    case Win64EH::UOP_SaveFReg:
      Count += 2;
      break;
    case Win64EH::UOP_SaveFRegP:
      Count += 2;
      break;
    case Win64EH::UOP_SaveFRegX:
      Count += 2;
      break;
    case Win64EH::UOP_SaveFRegPX:
      Count += 2;
      break;
    case Win64EH::UOP_SetFP:
      Count += 1;
      break;
    case Win64EH::UOP_AddFP:
      Count += 2;
      break;
    case Win64EH::UOP_Nop:
      Count += 1;
      break;
    case Win64EH::UOP_End:
      Count += 1;
      break;
    case Win64EH::UOP_SaveNext:
      Count += 1;
      break;
    case Win64EH::UOP_TrapFrame:
      Count += 1;
      break;
    case Win64EH::UOP_PushMachFrame:
      Count += 1;
      break;
    case Win64EH::UOP_Context:
      Count += 1;
      break;
    case Win64EH::UOP_ECContext:
      Count += 1;
      break;
    case Win64EH::UOP_ClearUnwoundToCall:
      Count += 1;
      break;
    case Win64EH::UOP_PACSignLR:
      Count += 1;
      break;
    case Win64EH::UOP_SaveAnyRegI:
    case Win64EH::UOP_SaveAnyRegIP:
    case Win64EH::UOP_SaveAnyRegD:
    case Win64EH::UOP_SaveAnyRegDP:
    case Win64EH::UOP_SaveAnyRegQ:
    case Win64EH::UOP_SaveAnyRegQP:
    case Win64EH::UOP_SaveAnyRegIX:
    case Win64EH::UOP_SaveAnyRegIPX:
    case Win64EH::UOP_SaveAnyRegDX:
    case Win64EH::UOP_SaveAnyRegDPX:
    case Win64EH::UOP_SaveAnyRegQX:
    case Win64EH::UOP_SaveAnyRegQPX:
      Count += 3;
      break;
    }
  }
  return Count;
}

// Unwind opcode encodings and restrictions are documented at
// https://docs.microsoft.com/en-us/cpp/build/arm64-exception-handling
static void ARM64EmitUnwindCode(MCStreamer &streamer,
                                const WinEH::Instruction &inst) {
  uint8_t b, reg;
  switch (static_cast<Win64EH::UnwindOpcodes>(inst.Operation)) {
  default:
    llvm_unreachable("Unsupported ARM64 unwind code");
  case Win64EH::UOP_AllocSmall:
    b = (inst.Offset >> 4) & 0x1F;
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_AllocMedium: {
    uint16_t hw = (inst.Offset >> 4) & 0x7FF;
    b = 0xC0;
    b |= (hw >> 8);
    streamer.emitInt8(b);
    b = hw & 0xFF;
    streamer.emitInt8(b);
    break;
  }
  case Win64EH::UOP_AllocLarge: {
    uint32_t w;
    b = 0xE0;
    streamer.emitInt8(b);
    w = inst.Offset >> 4;
    b = (w & 0x00FF0000) >> 16;
    streamer.emitInt8(b);
    b = (w & 0x0000FF00) >> 8;
    streamer.emitInt8(b);
    b = w & 0x000000FF;
    streamer.emitInt8(b);
    break;
  }
  case Win64EH::UOP_SetFP:
    b = 0xE1;
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_AddFP:
    b = 0xE2;
    streamer.emitInt8(b);
    b = (inst.Offset >> 3);
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_Nop:
    b = 0xE3;
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_SaveR19R20X:
    b = 0x20;
    b |= (inst.Offset >> 3) & 0x1F;
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_SaveFPLRX:
    b = 0x80;
    b |= ((inst.Offset - 1) >> 3) & 0x3F;
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_SaveFPLR:
    b = 0x40;
    b |= (inst.Offset >> 3) & 0x3F;
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_SaveReg:
    assert(inst.Register >= 19 && "Saved reg must be >= 19");
    reg = inst.Register - 19;
    b = 0xD0 | ((reg & 0xC) >> 2);
    streamer.emitInt8(b);
    b = ((reg & 0x3) << 6) | (inst.Offset >> 3);
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_SaveRegX:
    assert(inst.Register >= 19 && "Saved reg must be >= 19");
    reg = inst.Register - 19;
    b = 0xD4 | ((reg & 0x8) >> 3);
    streamer.emitInt8(b);
    b = ((reg & 0x7) << 5) | ((inst.Offset >> 3) - 1);
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_SaveRegP:
    assert(inst.Register >= 19 && "Saved registers must be >= 19");
    reg = inst.Register - 19;
    b = 0xC8 | ((reg & 0xC) >> 2);
    streamer.emitInt8(b);
    b = ((reg & 0x3) << 6) | (inst.Offset >> 3);
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_SaveRegPX:
    assert(inst.Register >= 19 && "Saved registers must be >= 19");
    reg = inst.Register - 19;
    b = 0xCC | ((reg & 0xC) >> 2);
    streamer.emitInt8(b);
    b = ((reg & 0x3) << 6) | ((inst.Offset >> 3) - 1);
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_SaveLRPair:
    assert(inst.Register >= 19 && "Saved reg must be >= 19");
    reg = inst.Register - 19;
    assert((reg % 2) == 0 && "Saved reg must be 19+2*X");
    reg /= 2;
    b = 0xD6 | ((reg & 0x7) >> 2);
    streamer.emitInt8(b);
    b = ((reg & 0x3) << 6) | (inst.Offset >> 3);
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_SaveFReg:
    assert(inst.Register >= 8 && "Saved dreg must be >= 8");
    reg = inst.Register - 8;
    b = 0xDC | ((reg & 0x4) >> 2);
    streamer.emitInt8(b);
    b = ((reg & 0x3) << 6) | (inst.Offset >> 3);
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_SaveFRegX:
    assert(inst.Register >= 8 && "Saved dreg must be >= 8");
    reg = inst.Register - 8;
    b = 0xDE;
    streamer.emitInt8(b);
    b = ((reg & 0x7) << 5) | ((inst.Offset >> 3) - 1);
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_SaveFRegP:
    assert(inst.Register >= 8 && "Saved dregs must be >= 8");
    reg = inst.Register - 8;
    b = 0xD8 | ((reg & 0x4) >> 2);
    streamer.emitInt8(b);
    b = ((reg & 0x3) << 6) | (inst.Offset >> 3);
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_SaveFRegPX:
    assert(inst.Register >= 8 && "Saved dregs must be >= 8");
    reg = inst.Register - 8;
    b = 0xDA | ((reg & 0x4) >> 2);
    streamer.emitInt8(b);
    b = ((reg & 0x3) << 6) | ((inst.Offset >> 3) - 1);
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_End:
    b = 0xE4;
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_SaveNext:
    b = 0xE6;
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_TrapFrame:
    b = 0xE8;
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_PushMachFrame:
    b = 0xE9;
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_Context:
    b = 0xEA;
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_ECContext:
    b = 0xEB;
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_ClearUnwoundToCall:
    b = 0xEC;
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_PACSignLR:
    b = 0xFC;
    streamer.emitInt8(b);
    break;
  case Win64EH::UOP_SaveAnyRegI:
  case Win64EH::UOP_SaveAnyRegIP:
  case Win64EH::UOP_SaveAnyRegD:
  case Win64EH::UOP_SaveAnyRegDP:
  case Win64EH::UOP_SaveAnyRegQ:
  case Win64EH::UOP_SaveAnyRegQP:
  case Win64EH::UOP_SaveAnyRegIX:
  case Win64EH::UOP_SaveAnyRegIPX:
  case Win64EH::UOP_SaveAnyRegDX:
  case Win64EH::UOP_SaveAnyRegDPX:
  case Win64EH::UOP_SaveAnyRegQX:
  case Win64EH::UOP_SaveAnyRegQPX: {
    // This assumes the opcodes are listed in the enum in a particular order.
    int Op = inst.Operation - Win64EH::UOP_SaveAnyRegI;
    int Writeback = Op / 6;
    int Paired = Op % 2;
    int Mode = (Op / 2) % 3;
    int Offset = inst.Offset >> 3;
    if (Writeback || Paired || Mode == 2)
      Offset >>= 1;
    if (Writeback)
      --Offset;
    b = 0xE7;
    streamer.emitInt8(b);
    assert(inst.Register < 32);
    b = inst.Register | (Writeback << 5) | (Paired << 6);
    streamer.emitInt8(b);
    b = Offset | (Mode << 6);
    streamer.emitInt8(b);
    break;
  }
  }
}

// Returns the epilog symbol of an epilog with the exact same unwind code
// sequence, if it exists.  Otherwise, returns nullptr.
// EpilogInstrs - Unwind codes for the current epilog.
// Epilogs - Epilogs that potentialy match the current epilog.
static MCSymbol*
FindMatchingEpilog(const std::vector<WinEH::Instruction>& EpilogInstrs,
                   const std::vector<MCSymbol *>& Epilogs,
                   const WinEH::FrameInfo *info) {
  for (auto *EpilogStart : Epilogs) {
    auto InstrsIter = info->EpilogMap.find(EpilogStart);
    assert(InstrsIter != info->EpilogMap.end() &&
           "Epilog not found in EpilogMap");
    const auto &Instrs = InstrsIter->second.Instructions;

    if (Instrs.size() != EpilogInstrs.size())
      continue;

    bool Match = true;
    for (unsigned i = 0; i < Instrs.size(); ++i)
      if (Instrs[i] != EpilogInstrs[i]) {
        Match = false;
        break;
      }

    if (Match)
      return EpilogStart;
  }
  return nullptr;
}

static void simplifyARM64Opcodes(std::vector<WinEH::Instruction> &Instructions,
                                 bool Reverse) {
  unsigned PrevOffset = -1;
  unsigned PrevRegister = -1;

  auto VisitInstruction = [&](WinEH::Instruction &Inst) {
    // Convert 2-byte opcodes into equivalent 1-byte ones.
    if (Inst.Operation == Win64EH::UOP_SaveRegP && Inst.Register == 29) {
      Inst.Operation = Win64EH::UOP_SaveFPLR;
      Inst.Register = -1;
    } else if (Inst.Operation == Win64EH::UOP_SaveRegPX &&
               Inst.Register == 29) {
      Inst.Operation = Win64EH::UOP_SaveFPLRX;
      Inst.Register = -1;
    } else if (Inst.Operation == Win64EH::UOP_SaveRegPX &&
               Inst.Register == 19 && Inst.Offset <= 248) {
      Inst.Operation = Win64EH::UOP_SaveR19R20X;
      Inst.Register = -1;
    } else if (Inst.Operation == Win64EH::UOP_AddFP && Inst.Offset == 0) {
      Inst.Operation = Win64EH::UOP_SetFP;
    } else if (Inst.Operation == Win64EH::UOP_SaveRegP &&
               Inst.Register == PrevRegister + 2 &&
               Inst.Offset == PrevOffset + 16) {
      Inst.Operation = Win64EH::UOP_SaveNext;
      Inst.Register = -1;
      Inst.Offset = 0;
      // Intentionally not creating UOP_SaveNext for float register pairs,
      // as current versions of Windows (up to at least 20.04) is buggy
      // regarding SaveNext for float pairs.
    }
    // Update info about the previous instruction, for detecting if
    // the next one can be made a UOP_SaveNext
    if (Inst.Operation == Win64EH::UOP_SaveR19R20X) {
      PrevOffset = 0;
      PrevRegister = 19;
    } else if (Inst.Operation == Win64EH::UOP_SaveRegPX) {
      PrevOffset = 0;
      PrevRegister = Inst.Register;
    } else if (Inst.Operation == Win64EH::UOP_SaveRegP) {
      PrevOffset = Inst.Offset;
      PrevRegister = Inst.Register;
    } else if (Inst.Operation == Win64EH::UOP_SaveNext) {
      PrevRegister += 2;
      PrevOffset += 16;
    } else {
      PrevRegister = -1;
      PrevOffset = -1;
    }
  };

  // Iterate over instructions in a forward order (for prologues),
  // backwards for epilogues (i.e. always reverse compared to how the
  // opcodes are stored).
  if (Reverse) {
    for (auto It = Instructions.rbegin(); It != Instructions.rend(); It++)
      VisitInstruction(*It);
  } else {
    for (WinEH::Instruction &Inst : Instructions)
      VisitInstruction(Inst);
  }
}

// Check if an epilog exists as a subset of the end of a prolog (backwards).
static int
getARM64OffsetInProlog(const std::vector<WinEH::Instruction> &Prolog,
                       const std::vector<WinEH::Instruction> &Epilog) {
  // Can't find an epilog as a subset if it is longer than the prolog.
  if (Epilog.size() > Prolog.size())
    return -1;

  // Check that the epilog actually is a perfect match for the end (backwrds)
  // of the prolog.
  for (int I = Epilog.size() - 1; I >= 0; I--) {
    if (Prolog[I] != Epilog[Epilog.size() - 1 - I])
      return -1;
  }

  if (Epilog.size() == Prolog.size())
    return 0;

  // If the epilog was a subset of the prolog, find its offset.
  return ARM64CountOfUnwindCodes(ArrayRef<WinEH::Instruction>(
      &Prolog[Epilog.size()], Prolog.size() - Epilog.size()));
}

static int checkARM64PackedEpilog(MCStreamer &streamer, WinEH::FrameInfo *info,
                                  WinEH::FrameInfo::Segment *Seg,
                                  int PrologCodeBytes) {
  // Can only pack if there's one single epilog
  if (Seg->Epilogs.size() != 1)
    return -1;

  MCSymbol *Sym = Seg->Epilogs.begin()->first;
  const std::vector<WinEH::Instruction> &Epilog =
      info->EpilogMap[Sym].Instructions;

  // Check that the epilog actually is at the very end of the function,
  // otherwise it can't be packed.
  uint32_t DistanceFromEnd =
      (uint32_t)(Seg->Offset + Seg->Length - Seg->Epilogs.begin()->second);
  if (DistanceFromEnd / 4 != Epilog.size())
    return -1;

  int RetVal = -1;
  // Even if we don't end up sharing opcodes with the prolog, we can still
  // write the offset as a packed offset, if the single epilog is located at
  // the end of the function and the offset (pointing after the prolog) fits
  // as a packed offset.
  if (PrologCodeBytes <= 31 &&
      PrologCodeBytes + ARM64CountOfUnwindCodes(Epilog) <= 124)
    RetVal = PrologCodeBytes;

  int Offset = getARM64OffsetInProlog(info->Instructions, Epilog);
  if (Offset < 0)
    return RetVal;

  // Check that the offset and prolog size fits in the first word; it's
  // unclear whether the epilog count in the extension word can be taken
  // as packed epilog offset.
  if (Offset > 31 || PrologCodeBytes > 124)
    return RetVal;

  // As we choose to express the epilog as part of the prolog, remove the
  // epilog from the map, so we don't try to emit its opcodes.
  info->EpilogMap.erase(Sym);
  return Offset;
}

static bool tryARM64PackedUnwind(WinEH::FrameInfo *info, uint32_t FuncLength,
                                 int PackedEpilogOffset) {
  if (PackedEpilogOffset == 0) {
    // Fully symmetric prolog and epilog, should be ok for packed format.
    // For CR=3, the corresponding synthesized epilog actually lacks the
    // SetFP opcode, but unwinding should work just fine despite that
    // (if at the SetFP opcode, the unwinder considers it as part of the
    // function body and just unwinds the full prolog instead).
  } else if (PackedEpilogOffset == 1) {
    // One single case of differences between prolog and epilog is allowed:
    // The epilog can lack a single SetFP that is the last opcode in the
    // prolog, for the CR=3 case.
    if (info->Instructions.back().Operation != Win64EH::UOP_SetFP)
      return false;
  } else {
    // Too much difference between prolog and epilog.
    return false;
  }
  unsigned RegI = 0, RegF = 0;
  int Predecrement = 0;
  enum {
    Start,
    Start2,
    Start3,
    IntRegs,
    FloatRegs,
    InputArgs,
    StackAdjust,
    FrameRecord,
    End
  } Location = Start;
  bool StandaloneLR = false, FPLRPair = false;
  bool PAC = false;
  int StackOffset = 0;
  int Nops = 0;
  // Iterate over the prolog and check that all opcodes exactly match
  // the canonical order and form. A more lax check could verify that
  // all saved registers are in the expected locations, but not enforce
  // the order - that would work fine when unwinding from within
  // functions, but not be exactly right if unwinding happens within
  // prologs/epilogs.
  for (const WinEH::Instruction &Inst : info->Instructions) {
    switch (Inst.Operation) {
    case Win64EH::UOP_End:
      if (Location != Start)
        return false;
      Location = Start2;
      break;
    case Win64EH::UOP_PACSignLR:
      if (Location != Start2)
        return false;
      PAC = true;
      Location = Start3;
      break;
    case Win64EH::UOP_SaveR19R20X:
      if (Location != Start2 && Location != Start3)
        return false;
      Predecrement = Inst.Offset;
      RegI = 2;
      Location = IntRegs;
      break;
    case Win64EH::UOP_SaveRegX:
      if (Location != Start2 && Location != Start3)
        return false;
      Predecrement = Inst.Offset;
      if (Inst.Register == 19)
        RegI += 1;
      else if (Inst.Register == 30)
        StandaloneLR = true;
      else
        return false;
      // Odd register; can't be any further int registers.
      Location = FloatRegs;
      break;
    case Win64EH::UOP_SaveRegPX:
      // Can't have this in a canonical prologue. Either this has been
      // canonicalized into SaveR19R20X or SaveFPLRX, or it's an unsupported
      // register pair.
      // It can't be canonicalized into SaveR19R20X if the offset is
      // larger than 248 bytes, but even with the maximum case with
      // RegI=10/RegF=8/CR=1/H=1, we end up with SavSZ = 216, which should
      // fit into SaveR19R20X.
      // The unwinding opcodes can't describe the otherwise seemingly valid
      // case for RegI=1 CR=1, that would start with a
      // "stp x19, lr, [sp, #-...]!" as that fits neither SaveRegPX nor
      // SaveLRPair.
      return false;
    case Win64EH::UOP_SaveRegP:
      if (Location != IntRegs || Inst.Offset != 8 * RegI ||
          Inst.Register != 19 + RegI)
        return false;
      RegI += 2;
      break;
    case Win64EH::UOP_SaveReg:
      if (Location != IntRegs || Inst.Offset != 8 * RegI)
        return false;
      if (Inst.Register == 19 + RegI)
        RegI += 1;
      else if (Inst.Register == 30)
        StandaloneLR = true;
      else
        return false;
      // Odd register; can't be any further int registers.
      Location = FloatRegs;
      break;
    case Win64EH::UOP_SaveLRPair:
      if (Location != IntRegs || Inst.Offset != 8 * RegI ||
          Inst.Register != 19 + RegI)
        return false;
      RegI += 1;
      StandaloneLR = true;
      Location = FloatRegs;
      break;
    case Win64EH::UOP_SaveFRegX:
      // Packed unwind can't handle prologs that only save one single
      // float register.
      return false;
    case Win64EH::UOP_SaveFReg:
      if (Location != FloatRegs || RegF == 0 || Inst.Register != 8 + RegF ||
          Inst.Offset != 8 * (RegI + (StandaloneLR ? 1 : 0) + RegF))
        return false;
      RegF += 1;
      Location = InputArgs;
      break;
    case Win64EH::UOP_SaveFRegPX:
      if ((Location != Start2 && Location != Start3) || Inst.Register != 8)
        return false;
      Predecrement = Inst.Offset;
      RegF = 2;
      Location = FloatRegs;
      break;
    case Win64EH::UOP_SaveFRegP:
      if ((Location != IntRegs && Location != FloatRegs) ||
          Inst.Register != 8 + RegF ||
          Inst.Offset != 8 * (RegI + (StandaloneLR ? 1 : 0) + RegF))
        return false;
      RegF += 2;
      Location = FloatRegs;
      break;
    case Win64EH::UOP_SaveNext:
      if (Location == IntRegs)
        RegI += 2;
      else if (Location == FloatRegs)
        RegF += 2;
      else
        return false;
      break;
    case Win64EH::UOP_Nop:
      if (Location != IntRegs && Location != FloatRegs && Location != InputArgs)
        return false;
      Location = InputArgs;
      Nops++;
      break;
    case Win64EH::UOP_AllocSmall:
    case Win64EH::UOP_AllocMedium:
      if (Location != Start2 && Location != Start3 && Location != IntRegs &&
          Location != FloatRegs && Location != InputArgs &&
          Location != StackAdjust)
        return false;
      // Can have either a single decrement, or a pair of decrements with
      // 4080 and another decrement.
      if (StackOffset == 0)
        StackOffset = Inst.Offset;
      else if (StackOffset != 4080)
        return false;
      else
        StackOffset += Inst.Offset;
      Location = StackAdjust;
      break;
    case Win64EH::UOP_SaveFPLRX:
      // Not allowing FPLRX after StackAdjust; if a StackAdjust is used, it
      // should be followed by a FPLR instead.
      if (Location != Start2 && Location != Start3 && Location != IntRegs &&
          Location != FloatRegs && Location != InputArgs)
        return false;
      StackOffset = Inst.Offset;
      Location = FrameRecord;
      FPLRPair = true;
      break;
    case Win64EH::UOP_SaveFPLR:
      // This can only follow after a StackAdjust
      if (Location != StackAdjust || Inst.Offset != 0)
        return false;
      Location = FrameRecord;
      FPLRPair = true;
      break;
    case Win64EH::UOP_SetFP:
      if (Location != FrameRecord)
        return false;
      Location = End;
      break;
    case Win64EH::UOP_SaveAnyRegI:
    case Win64EH::UOP_SaveAnyRegIP:
    case Win64EH::UOP_SaveAnyRegD:
    case Win64EH::UOP_SaveAnyRegDP:
    case Win64EH::UOP_SaveAnyRegQ:
    case Win64EH::UOP_SaveAnyRegQP:
    case Win64EH::UOP_SaveAnyRegIX:
    case Win64EH::UOP_SaveAnyRegIPX:
    case Win64EH::UOP_SaveAnyRegDX:
    case Win64EH::UOP_SaveAnyRegDPX:
    case Win64EH::UOP_SaveAnyRegQX:
    case Win64EH::UOP_SaveAnyRegQPX:
      // These are never canonical; they don't show up with the usual Arm64
      // calling convention.
      return false;
    case Win64EH::UOP_AllocLarge:
      // Allocations this large can't be represented in packed unwind (and
      // usually don't fit the canonical form anyway because we need to use
      // __chkstk to allocate the stack space).
      return false;
    case Win64EH::UOP_AddFP:
      // "add x29, sp, #N" doesn't show up in the canonical pattern (except for
      // N=0, which is UOP_SetFP).
      return false;
    case Win64EH::UOP_TrapFrame:
    case Win64EH::UOP_Context:
    case Win64EH::UOP_ECContext:
    case Win64EH::UOP_ClearUnwoundToCall:
    case Win64EH::UOP_PushMachFrame:
      // These are special opcodes that aren't normally generated.
      return false;
    default:
      report_fatal_error("Unknown Arm64 unwind opcode");
    }
  }
  if (RegI > 10 || RegF > 8)
    return false;
  if (StandaloneLR && FPLRPair)
    return false;
  if (FPLRPair && Location != End)
    return false;
  if (Nops != 0 && Nops != 4)
    return false;
  if (PAC && !FPLRPair)
    return false;
  int H = Nops == 4;
  // There's an inconsistency regarding packed unwind info with homed
  // parameters; according to the documentation, the epilog shouldn't have
  // the same corresponding nops (and thus, to set the H bit, we should
  // require an epilog which isn't exactly symmetrical - we shouldn't accept
  // an exact mirrored epilog for those cases), but in practice,
  // RtlVirtualUnwind behaves as if it does expect the epilogue to contain
  // the same nops. See https://github.com/llvm/llvm-project/issues/54879.
  // To play it safe, don't produce packed unwind info with homed parameters.
  if (H)
    return false;
  int IntSZ = 8 * RegI;
  if (StandaloneLR)
    IntSZ += 8;
  int FpSZ = 8 * RegF; // RegF not yet decremented
  int SavSZ = (IntSZ + FpSZ + 8 * 8 * H + 0xF) & ~0xF;
  if (Predecrement != SavSZ)
    return false;
  if (FPLRPair && StackOffset < 16)
    return false;
  if (StackOffset % 16)
    return false;
  uint32_t FrameSize = (StackOffset + SavSZ) / 16;
  if (FrameSize > 0x1FF)
    return false;
  assert(RegF != 1 && "One single float reg not allowed");
  if (RegF > 0)
    RegF--; // Convert from actual number of registers, to value stored
  assert(FuncLength <= 0x7FF && "FuncLength should have been checked earlier");
  int Flag = 0x01; // Function segments not supported yet
  int CR = PAC ? 2 : FPLRPair ? 3 : StandaloneLR ? 1 : 0;
  info->PackedInfo |= Flag << 0;
  info->PackedInfo |= (FuncLength & 0x7FF) << 2;
  info->PackedInfo |= (RegF & 0x7) << 13;
  info->PackedInfo |= (RegI & 0xF) << 16;
  info->PackedInfo |= (H & 0x1) << 20;
  info->PackedInfo |= (CR & 0x3) << 21;
  info->PackedInfo |= (FrameSize & 0x1FF) << 23;
  return true;
}

static void ARM64ProcessEpilogs(WinEH::FrameInfo *info,
                                WinEH::FrameInfo::Segment *Seg,
                                uint32_t &TotalCodeBytes,
                                MapVector<MCSymbol *, uint32_t> &EpilogInfo) {

  std::vector<MCSymbol *> EpilogStarts;
  for (auto &I : Seg->Epilogs)
    EpilogStarts.push_back(I.first);

  // Epilogs processed so far.
  std::vector<MCSymbol *> AddedEpilogs;
  for (auto *S : EpilogStarts) {
    MCSymbol *EpilogStart = S;
    auto &EpilogInstrs = info->EpilogMap[S].Instructions;
    uint32_t CodeBytes = ARM64CountOfUnwindCodes(EpilogInstrs);

    MCSymbol* MatchingEpilog =
      FindMatchingEpilog(EpilogInstrs, AddedEpilogs, info);
    int PrologOffset;
    if (MatchingEpilog) {
      assert(EpilogInfo.contains(MatchingEpilog) &&
             "Duplicate epilog not found");
      EpilogInfo[EpilogStart] = EpilogInfo.lookup(MatchingEpilog);
      // Clear the unwind codes in the EpilogMap, so that they don't get output
      // in ARM64EmitUnwindInfoForSegment().
      EpilogInstrs.clear();
    } else if ((PrologOffset = getARM64OffsetInProlog(info->Instructions,
                                                      EpilogInstrs)) >= 0) {
      EpilogInfo[EpilogStart] = PrologOffset;
      // If the segment doesn't have a prolog, an end_c will be emitted before
      // prolog opcodes. So epilog start index in opcodes array is moved by 1.
      if (!Seg->HasProlog)
        EpilogInfo[EpilogStart] += 1;
      // Clear the unwind codes in the EpilogMap, so that they don't get output
      // in ARM64EmitUnwindInfoForSegment().
      EpilogInstrs.clear();
    } else {
      EpilogInfo[EpilogStart] = TotalCodeBytes;
      TotalCodeBytes += CodeBytes;
      AddedEpilogs.push_back(EpilogStart);
    }
  }
}

static void ARM64FindSegmentsInFunction(MCStreamer &streamer,
                                        WinEH::FrameInfo *info,
                                        int64_t RawFuncLength) {
  if (info->PrologEnd)
    checkARM64Instructions(streamer, info->Instructions, info->Begin,
                           info->PrologEnd, info->Function->getName(),
                           "prologue");
  struct EpilogStartEnd {
    MCSymbol *Start;
    int64_t Offset;
    int64_t End;
  };
  // Record Start and End of each epilog.
  SmallVector<struct EpilogStartEnd, 4> Epilogs;
  for (auto &I : info->EpilogMap) {
    MCSymbol *Start = I.first;
    auto &Instrs = I.second.Instructions;
    int64_t Offset = GetAbsDifference(streamer, Start, info->Begin);
    checkARM64Instructions(streamer, Instrs, Start, I.second.End,
                           info->Function->getName(), "epilogue");
    assert((Epilogs.size() == 0 || Offset >= Epilogs.back().End) &&
           "Epilogs should be monotonically ordered");
    // Exclue the end opcode from Instrs.size() when calculating the end of the
    // epilog.
    Epilogs.push_back({Start, Offset, Offset + (int64_t)(Instrs.size() - 1) * 4});
  }

  unsigned E = 0;
  int64_t SegLimit = 0xFFFFC;
  int64_t SegOffset = 0;

  if (RawFuncLength > SegLimit) {

    int64_t RemainingLength = RawFuncLength;

    while (RemainingLength > SegLimit) {
      // Try divide the function into segments, requirements:
      // 1. Segment length <= 0xFFFFC;
      // 2. Each Prologue or Epilogue must be fully within a segment.
      int64_t SegLength = SegLimit;
      int64_t SegEnd = SegOffset + SegLength;
      // Keep record on symbols and offsets of epilogs in this segment.
      MapVector<MCSymbol *, int64_t> EpilogsInSegment;

      while (E < Epilogs.size() && Epilogs[E].End < SegEnd) {
        // Epilogs within current segment.
        EpilogsInSegment[Epilogs[E].Start] = Epilogs[E].Offset;
        ++E;
      }

      // At this point, we have:
      // 1. Put all epilogs in segments already. No action needed here; or
      // 2. Found an epilog that will cross segments boundry. We need to
      //    move back current segment's end boundry, so the epilog is entirely
      //    in the next segment; or
      // 3. Left at least one epilog that is entirely after this segment.
      //    It'll be handled by the next iteration, or the last segment.
      if (E < Epilogs.size() && Epilogs[E].Offset <= SegEnd)
        // Move back current Segment's end boundry.
        SegLength = Epilogs[E].Offset - SegOffset;

      auto Seg = WinEH::FrameInfo::Segment(
          SegOffset, SegLength, /* HasProlog */!SegOffset);
      Seg.Epilogs = std::move(EpilogsInSegment);
      info->Segments.push_back(Seg);

      SegOffset += SegLength;
      RemainingLength -= SegLength;
    }
  }

  // Add the last segment when RawFuncLength > 0xFFFFC,
  // or the only segment otherwise.
  auto LastSeg =
      WinEH::FrameInfo::Segment(SegOffset, RawFuncLength - SegOffset,
                                /* HasProlog */!SegOffset);
  for (; E < Epilogs.size(); ++E)
    LastSeg.Epilogs[Epilogs[E].Start] = Epilogs[E].Offset;
  info->Segments.push_back(LastSeg);
}

static void ARM64EmitUnwindInfoForSegment(MCStreamer &streamer,
                                          WinEH::FrameInfo *info,
                                          WinEH::FrameInfo::Segment &Seg,
                                          bool TryPacked = true) {
  MCContext &context = streamer.getContext();
  MCSymbol *Label = context.createTempSymbol();

  streamer.emitValueToAlignment(Align(4));
  streamer.emitLabel(Label);
  Seg.Symbol = Label;
  // Use the 1st segemnt's label as function's.
  if (Seg.Offset == 0)
    info->Symbol = Label;

  bool HasProlog = Seg.HasProlog;
  bool HasEpilogs = (Seg.Epilogs.size() != 0);

  uint32_t SegLength = (uint32_t)Seg.Length / 4;
  uint32_t PrologCodeBytes = info->PrologCodeBytes;

  int PackedEpilogOffset = HasEpilogs ?
      checkARM64PackedEpilog(streamer, info, &Seg, PrologCodeBytes) : -1;

  // TODO:
  // 1. Enable packed unwind info (.pdata only) for multi-segment functions.
  // 2. Emit packed unwind info (.pdata only) for segments that have neithor
  //    prolog nor epilog.
  if (info->Segments.size() == 1 && PackedEpilogOffset >= 0 &&
      uint32_t(PackedEpilogOffset) < PrologCodeBytes &&
      !info->HandlesExceptions && SegLength <= 0x7ff && TryPacked) {
    // Matching prolog/epilog and no exception handlers; check if the
    // prolog matches the patterns that can be described by the packed
    // format.

    // info->Symbol was already set even if we didn't actually write any
    // unwind info there. Keep using that as indicator that this unwind
    // info has been generated already.
    if (tryARM64PackedUnwind(info, SegLength, PackedEpilogOffset))
      return;
  }

  // If the prolog is not in this segment, we need to emit an end_c, which takes
  // 1 byte, before prolog unwind ops.
  if (!HasProlog) {
    PrologCodeBytes += 1;
    if (PackedEpilogOffset >= 0)
      PackedEpilogOffset += 1;
    // If a segment has neither prolog nor epilog, "With full .xdata record,
    // Epilog Count = 1. Epilog Start Index points to end_c."
    // https://docs.microsoft.com/en-us/cpp/build/arm64-exception-handling#function-fragments
    // TODO: We can remove this if testing shows zero epilog scope is ok with
    //       MS unwinder.
    if (!HasEpilogs)
      // Pack the fake epilog into phantom prolog.
      PackedEpilogOffset = 0;
  }

  uint32_t TotalCodeBytes = PrologCodeBytes;

  // Process epilogs.
  MapVector<MCSymbol *, uint32_t> EpilogInfo;
  ARM64ProcessEpilogs(info, &Seg, TotalCodeBytes, EpilogInfo);

  // Code Words, Epilog count, E, X, Vers, Function Length
  uint32_t row1 = 0x0;
  uint32_t CodeWords = TotalCodeBytes / 4;
  uint32_t CodeWordsMod = TotalCodeBytes % 4;
  if (CodeWordsMod)
    CodeWords++;
  uint32_t EpilogCount =
      PackedEpilogOffset >= 0 ? PackedEpilogOffset : Seg.Epilogs.size();
  bool ExtensionWord = EpilogCount > 31 || TotalCodeBytes > 124;
  if (!ExtensionWord) {
    row1 |= (EpilogCount & 0x1F) << 22;
    row1 |= (CodeWords & 0x1F) << 27;
  }
  if (info->HandlesExceptions) // X
    row1 |= 1 << 20;
  if (PackedEpilogOffset >= 0) // E
    row1 |= 1 << 21;
  row1 |= SegLength & 0x3FFFF;
  streamer.emitInt32(row1);

  // Extended Code Words, Extended Epilog Count
  if (ExtensionWord) {
    // FIXME: We should be able to split unwind info into multiple sections.
    if (CodeWords > 0xFF || EpilogCount > 0xFFFF)
      report_fatal_error(
          "SEH unwind data splitting is only implemented for large functions, "
          "cases of too many code words or too many epilogs will be done "
          "later");
    uint32_t row2 = 0x0;
    row2 |= (CodeWords & 0xFF) << 16;
    row2 |= (EpilogCount & 0xFFFF);
    streamer.emitInt32(row2);
  }

  if (PackedEpilogOffset < 0) {
    // Epilog Start Index, Epilog Start Offset
    for (auto &I : EpilogInfo) {
      MCSymbol *EpilogStart = I.first;
      uint32_t EpilogIndex = I.second;
      // Epilog offset within the Segment.
      uint32_t EpilogOffset = (uint32_t)(Seg.Epilogs[EpilogStart] - Seg.Offset);
      if (EpilogOffset)
        EpilogOffset /= 4;
      uint32_t row3 = EpilogOffset;
      row3 |= (EpilogIndex & 0x3FF) << 22;
      streamer.emitInt32(row3);
    }
  }

  // Note that even for segments that have no prolog, we still need to emit
  // prolog unwinding opcodes so that the unwinder knows how to unwind from
  // such a segment.
  // The end_c opcode at the start indicates to the unwinder that the actual
  // prolog is outside of the current segment, and the unwinder shouldn't try
  // to check for unwinding from a partial prolog.
  if (!HasProlog)
    // Emit an end_c.
    streamer.emitInt8((uint8_t)0xE5);

  // Emit prolog unwind instructions (in reverse order).
  for (auto Inst : llvm::reverse(info->Instructions))
    ARM64EmitUnwindCode(streamer, Inst);

  // Emit epilog unwind instructions
  for (auto &I : Seg.Epilogs) {
    auto &EpilogInstrs = info->EpilogMap[I.first].Instructions;
    for (const WinEH::Instruction &inst : EpilogInstrs)
      ARM64EmitUnwindCode(streamer, inst);
  }

  int32_t BytesMod = CodeWords * 4 - TotalCodeBytes;
  assert(BytesMod >= 0);
  for (int i = 0; i < BytesMod; i++)
    streamer.emitInt8(0xE3);

  if (info->HandlesExceptions)
    streamer.emitValue(
        MCSymbolRefExpr::create(info->ExceptionHandler,
                                MCSymbolRefExpr::VK_COFF_IMGREL32, context),
        4);
}

// Populate the .xdata section.  The format of .xdata on ARM64 is documented at
// https://docs.microsoft.com/en-us/cpp/build/arm64-exception-handling
static void ARM64EmitUnwindInfo(MCStreamer &streamer, WinEH::FrameInfo *info,
                                bool TryPacked = true) {
  // If this UNWIND_INFO already has a symbol, it's already been emitted.
  if (info->Symbol)
    return;
  // If there's no unwind info here (not even a terminating UOP_End), the
  // unwind info is considered bogus and skipped. If this was done in
  // response to an explicit .seh_handlerdata, the associated trailing
  // handler data is left orphaned in the xdata section.
  if (info->empty()) {
    info->EmitAttempted = true;
    return;
  }
  if (info->EmitAttempted) {
    // If we tried to emit unwind info before (due to an explicit
    // .seh_handlerdata directive), but skipped it (because there was no
    // valid information to emit at the time), and it later got valid unwind
    // opcodes, we can't emit it here, because the trailing handler data
    // was already emitted elsewhere in the xdata section.
    streamer.getContext().reportError(
        SMLoc(), "Earlier .seh_handlerdata for " + info->Function->getName() +
                     " skipped due to no unwind info at the time "
                     "(.seh_handlerdata too early?), but the function later "
                     "did get unwind info that can't be emitted");
    return;
  }

  simplifyARM64Opcodes(info->Instructions, false);
  for (auto &I : info->EpilogMap)
    simplifyARM64Opcodes(I.second.Instructions, true);

  int64_t RawFuncLength;
  if (!info->FuncletOrFuncEnd) {
    report_fatal_error("FuncletOrFuncEnd not set");
  } else {
    // FIXME: GetAbsDifference tries to compute the length of the function
    // immediately, before the whole file is emitted, but in general
    // that's impossible: the size in bytes of certain assembler directives
    // like .align and .fill is not known until the whole file is parsed and
    // relaxations are applied. Currently, GetAbsDifference fails with a fatal
    // error in that case. (We mostly don't hit this because inline assembly
    // specifying those directives is rare, and we don't normally try to
    // align loops on AArch64.)
    //
    // There are two potential approaches to delaying the computation. One,
    // we could emit something like ".word (endfunc-beginfunc)/4+0x10800000",
    // as long as we have some conservative estimate we could use to prove
    // that we don't need to split the unwind data. Emitting the constant
    // is straightforward, but there's no existing code for estimating the
    // size of the function.
    //
    // The other approach would be to use a dedicated, relaxable fragment,
    // which could grow to accommodate splitting the unwind data if
    // necessary. This is more straightforward, since it automatically works
    // without any new infrastructure, and it's consistent with how we handle
    // relaxation in other contexts.  But it would require some refactoring
    // to move parts of the pdata/xdata emission into the implementation of
    // a fragment. We could probably continue to encode the unwind codes
    // here, but we'd have to emit the pdata, the xdata header, and the
    // epilogue scopes later, since they depend on whether the we need to
    // split the unwind data.
    //
    // If this is fixed, remove code in AArch64ISelLowering.cpp that
    // disables loop alignment on Windows.
    RawFuncLength = GetAbsDifference(streamer, info->FuncletOrFuncEnd,
                                     info->Begin);
  }

  ARM64FindSegmentsInFunction(streamer, info, RawFuncLength);

  info->PrologCodeBytes = ARM64CountOfUnwindCodes(info->Instructions);
  for (auto &S : info->Segments)
    ARM64EmitUnwindInfoForSegment(streamer, info, S, TryPacked);

  // Clear prolog instructions after unwind info is emitted for all segments.
  info->Instructions.clear();
}

static uint32_t ARMCountOfUnwindCodes(ArrayRef<WinEH::Instruction> Insns) {
  uint32_t Count = 0;
  for (const auto &I : Insns) {
    switch (static_cast<Win64EH::UnwindOpcodes>(I.Operation)) {
    default:
      llvm_unreachable("Unsupported ARM unwind code");
    case Win64EH::UOP_AllocSmall:
      Count += 1;
      break;
    case Win64EH::UOP_AllocLarge:
      Count += 3;
      break;
    case Win64EH::UOP_AllocHuge:
      Count += 4;
      break;
    case Win64EH::UOP_WideAllocMedium:
      Count += 2;
      break;
    case Win64EH::UOP_WideAllocLarge:
      Count += 3;
      break;
    case Win64EH::UOP_WideAllocHuge:
      Count += 4;
      break;
    case Win64EH::UOP_WideSaveRegMask:
      Count += 2;
      break;
    case Win64EH::UOP_SaveSP:
      Count += 1;
      break;
    case Win64EH::UOP_SaveRegsR4R7LR:
      Count += 1;
      break;
    case Win64EH::UOP_WideSaveRegsR4R11LR:
      Count += 1;
      break;
    case Win64EH::UOP_SaveFRegD8D15:
      Count += 1;
      break;
    case Win64EH::UOP_SaveRegMask:
      Count += 2;
      break;
    case Win64EH::UOP_SaveLR:
      Count += 2;
      break;
    case Win64EH::UOP_SaveFRegD0D15:
      Count += 2;
      break;
    case Win64EH::UOP_SaveFRegD16D31:
      Count += 2;
      break;
    case Win64EH::UOP_Nop:
    case Win64EH::UOP_WideNop:
    case Win64EH::UOP_End:
    case Win64EH::UOP_EndNop:
    case Win64EH::UOP_WideEndNop:
      Count += 1;
      break;
    case Win64EH::UOP_Custom: {
      int J;
      for (J = 3; J > 0; J--)
        if (I.Offset & (0xffu << (8 * J)))
          break;
      Count += J + 1;
      break;
    }
    }
  }
  return Count;
}

static uint32_t ARMCountOfInstructionBytes(ArrayRef<WinEH::Instruction> Insns,
                                           bool *HasCustom = nullptr) {
  uint32_t Count = 0;
  for (const auto &I : Insns) {
    switch (static_cast<Win64EH::UnwindOpcodes>(I.Operation)) {
    default:
      llvm_unreachable("Unsupported ARM unwind code");
    case Win64EH::UOP_AllocSmall:
    case Win64EH::UOP_AllocLarge:
    case Win64EH::UOP_AllocHuge:
      Count += 2;
      break;
    case Win64EH::UOP_WideAllocMedium:
    case Win64EH::UOP_WideAllocLarge:
    case Win64EH::UOP_WideAllocHuge:
      Count += 4;
      break;
    case Win64EH::UOP_WideSaveRegMask:
    case Win64EH::UOP_WideSaveRegsR4R11LR:
      Count += 4;
      break;
    case Win64EH::UOP_SaveSP:
      Count += 2;
      break;
    case Win64EH::UOP_SaveRegMask:
    case Win64EH::UOP_SaveRegsR4R7LR:
      Count += 2;
      break;
    case Win64EH::UOP_SaveFRegD8D15:
    case Win64EH::UOP_SaveFRegD0D15:
    case Win64EH::UOP_SaveFRegD16D31:
      Count += 4;
      break;
    case Win64EH::UOP_SaveLR:
      Count += 4;
      break;
    case Win64EH::UOP_Nop:
    case Win64EH::UOP_EndNop:
      Count += 2;
      break;
    case Win64EH::UOP_WideNop:
    case Win64EH::UOP_WideEndNop:
      Count += 4;
      break;
    case Win64EH::UOP_End:
      // This doesn't map to any instruction
      break;
    case Win64EH::UOP_Custom:
      // We can't reason about what instructions this maps to; return a
      // phony number to make sure we don't accidentally do epilog packing.
      Count += 1000;
      if (HasCustom)
        *HasCustom = true;
      break;
    }
  }
  return Count;
}

static void checkARMInstructions(MCStreamer &Streamer,
                                 ArrayRef<WinEH::Instruction> Insns,
                                 const MCSymbol *Begin, const MCSymbol *End,
                                 StringRef Name, StringRef Type) {
  if (!End)
    return;
  std::optional<int64_t> MaybeDistance =
      GetOptionalAbsDifference(Streamer, End, Begin);
  if (!MaybeDistance)
    return;
  uint32_t Distance = (uint32_t)*MaybeDistance;
  bool HasCustom = false;
  uint32_t InstructionBytes = ARMCountOfInstructionBytes(Insns, &HasCustom);
  if (HasCustom)
    return;
  if (Distance != InstructionBytes) {
    Streamer.getContext().reportError(
        SMLoc(), "Incorrect size for " + Name + " " + Type + ": " +
                     Twine(Distance) +
                     " bytes of instructions in range, but .seh directives "
                     "corresponding to " +
                     Twine(InstructionBytes) + " bytes\n");
  }
}

static bool isARMTerminator(const WinEH::Instruction &inst) {
  switch (static_cast<Win64EH::UnwindOpcodes>(inst.Operation)) {
  case Win64EH::UOP_End:
  case Win64EH::UOP_EndNop:
  case Win64EH::UOP_WideEndNop:
    return true;
  default:
    return false;
  }
}

// Unwind opcode encodings and restrictions are documented at
// https://docs.microsoft.com/en-us/cpp/build/arm-exception-handling
static void ARMEmitUnwindCode(MCStreamer &streamer,
                              const WinEH::Instruction &inst) {
  uint32_t w, lr;
  int i;
  switch (static_cast<Win64EH::UnwindOpcodes>(inst.Operation)) {
  default:
    llvm_unreachable("Unsupported ARM unwind code");
  case Win64EH::UOP_AllocSmall:
    assert((inst.Offset & 3) == 0);
    assert(inst.Offset / 4 <= 0x7f);
    streamer.emitInt8(inst.Offset / 4);
    break;
  case Win64EH::UOP_WideSaveRegMask:
    assert((inst.Register & ~0x5fff) == 0);
    lr = (inst.Register >> 14) & 1;
    w = 0x8000 | (inst.Register & 0x1fff) | (lr << 13);
    streamer.emitInt8((w >> 8) & 0xff);
    streamer.emitInt8((w >> 0) & 0xff);
    break;
  case Win64EH::UOP_SaveSP:
    assert(inst.Register <= 0x0f);
    streamer.emitInt8(0xc0 | inst.Register);
    break;
  case Win64EH::UOP_SaveRegsR4R7LR:
    assert(inst.Register >= 4 && inst.Register <= 7);
    assert(inst.Offset <= 1);
    streamer.emitInt8(0xd0 | (inst.Register - 4) | (inst.Offset << 2));
    break;
  case Win64EH::UOP_WideSaveRegsR4R11LR:
    assert(inst.Register >= 8 && inst.Register <= 11);
    assert(inst.Offset <= 1);
    streamer.emitInt8(0xd8 | (inst.Register - 8) | (inst.Offset << 2));
    break;
  case Win64EH::UOP_SaveFRegD8D15:
    assert(inst.Register >= 8 && inst.Register <= 15);
    streamer.emitInt8(0xe0 | (inst.Register - 8));
    break;
  case Win64EH::UOP_WideAllocMedium:
    assert((inst.Offset & 3) == 0);
    assert(inst.Offset / 4 <= 0x3ff);
    w = 0xe800 | (inst.Offset / 4);
    streamer.emitInt8((w >> 8) & 0xff);
    streamer.emitInt8((w >> 0) & 0xff);
    break;
  case Win64EH::UOP_SaveRegMask:
    assert((inst.Register & ~0x40ff) == 0);
    lr = (inst.Register >> 14) & 1;
    w = 0xec00 | (inst.Register & 0x0ff) | (lr << 8);
    streamer.emitInt8((w >> 8) & 0xff);
    streamer.emitInt8((w >> 0) & 0xff);
    break;
  case Win64EH::UOP_SaveLR:
    assert((inst.Offset & 3) == 0);
    assert(inst.Offset / 4 <= 0x0f);
    streamer.emitInt8(0xef);
    streamer.emitInt8(inst.Offset / 4);
    break;
  case Win64EH::UOP_SaveFRegD0D15:
    assert(inst.Register <= 15);
    assert(inst.Offset <= 15);
    assert(inst.Register <= inst.Offset);
    streamer.emitInt8(0xf5);
    streamer.emitInt8((inst.Register << 4) | inst.Offset);
    break;
  case Win64EH::UOP_SaveFRegD16D31:
    assert(inst.Register >= 16 && inst.Register <= 31);
    assert(inst.Offset >= 16 && inst.Offset <= 31);
    assert(inst.Register <= inst.Offset);
    streamer.emitInt8(0xf6);
    streamer.emitInt8(((inst.Register - 16) << 4) | (inst.Offset - 16));
    break;
  case Win64EH::UOP_AllocLarge:
    assert((inst.Offset & 3) == 0);
    assert(inst.Offset / 4 <= 0xffff);
    w = inst.Offset / 4;
    streamer.emitInt8(0xf7);
    streamer.emitInt8((w >> 8) & 0xff);
    streamer.emitInt8((w >> 0) & 0xff);
    break;
  case Win64EH::UOP_AllocHuge:
    assert((inst.Offset & 3) == 0);
    assert(inst.Offset / 4 <= 0xffffff);
    w = inst.Offset / 4;
    streamer.emitInt8(0xf8);
    streamer.emitInt8((w >> 16) & 0xff);
    streamer.emitInt8((w >> 8) & 0xff);
    streamer.emitInt8((w >> 0) & 0xff);
    break;
  case Win64EH::UOP_WideAllocLarge:
    assert((inst.Offset & 3) == 0);
    assert(inst.Offset / 4 <= 0xffff);
    w = inst.Offset / 4;
    streamer.emitInt8(0xf9);
    streamer.emitInt8((w >> 8) & 0xff);
    streamer.emitInt8((w >> 0) & 0xff);
    break;
  case Win64EH::UOP_WideAllocHuge:
    assert((inst.Offset & 3) == 0);
    assert(inst.Offset / 4 <= 0xffffff);
    w = inst.Offset / 4;
    streamer.emitInt8(0xfa);
    streamer.emitInt8((w >> 16) & 0xff);
    streamer.emitInt8((w >> 8) & 0xff);
    streamer.emitInt8((w >> 0) & 0xff);
    break;
  case Win64EH::UOP_Nop:
    streamer.emitInt8(0xfb);
    break;
  case Win64EH::UOP_WideNop:
    streamer.emitInt8(0xfc);
    break;
  case Win64EH::UOP_EndNop:
    streamer.emitInt8(0xfd);
    break;
  case Win64EH::UOP_WideEndNop:
    streamer.emitInt8(0xfe);
    break;
  case Win64EH::UOP_End:
    streamer.emitInt8(0xff);
    break;
  case Win64EH::UOP_Custom:
    for (i = 3; i > 0; i--)
      if (inst.Offset & (0xffu << (8 * i)))
        break;
    for (; i >= 0; i--)
      streamer.emitInt8((inst.Offset >> (8 * i)) & 0xff);
    break;
  }
}

// Check if an epilog exists as a subset of the end of a prolog (backwards).
// An epilog may end with one out of three different end opcodes; if this
// is the first epilog that shares opcodes with the prolog, we can tolerate
// that this opcode differs (and the caller will update the prolog to use
// the same end opcode as the epilog). If another epilog already shares
// opcodes with the prolog, the ending opcode must be a strict match.
static int getARMOffsetInProlog(const std::vector<WinEH::Instruction> &Prolog,
                                const std::vector<WinEH::Instruction> &Epilog,
                                bool CanTweakProlog) {
  // Can't find an epilog as a subset if it is longer than the prolog.
  if (Epilog.size() > Prolog.size())
    return -1;

  // Check that the epilog actually is a perfect match for the end (backwrds)
  // of the prolog.
  // If we can adjust the prolog afterwards, don't check that the end opcodes
  // match.
  int EndIdx = CanTweakProlog ? 1 : 0;
  for (int I = Epilog.size() - 1; I >= EndIdx; I--) {
    // TODO: Could also allow minor mismatches, e.g. "add sp, #16" vs
    // "push {r0-r3}".
    if (Prolog[I] != Epilog[Epilog.size() - 1 - I])
      return -1;
  }

  if (CanTweakProlog) {
    // Check that both prolog and epilog end with an expected end opcode.
    if (Prolog.front().Operation != Win64EH::UOP_End)
      return -1;
    if (Epilog.back().Operation != Win64EH::UOP_End &&
        Epilog.back().Operation != Win64EH::UOP_EndNop &&
        Epilog.back().Operation != Win64EH::UOP_WideEndNop)
      return -1;
  }

  // If the epilog was a subset of the prolog, find its offset.
  if (Epilog.size() == Prolog.size())
    return 0;
  return ARMCountOfUnwindCodes(ArrayRef<WinEH::Instruction>(
      &Prolog[Epilog.size()], Prolog.size() - Epilog.size()));
}

static int checkARMPackedEpilog(MCStreamer &streamer, WinEH::FrameInfo *info,
                                int PrologCodeBytes) {
  // Can only pack if there's one single epilog
  if (info->EpilogMap.size() != 1)
    return -1;

  const WinEH::FrameInfo::Epilog &EpilogInfo = info->EpilogMap.begin()->second;
  // Can only pack if the epilog is unconditional
  if (EpilogInfo.Condition != 0xe) // ARMCC::AL
    return -1;

  const std::vector<WinEH::Instruction> &Epilog = EpilogInfo.Instructions;
  // Make sure we have at least the trailing end opcode
  if (info->Instructions.empty() || Epilog.empty())
    return -1;

  // Check that the epilog actually is at the very end of the function,
  // otherwise it can't be packed.
  std::optional<int64_t> MaybeDistance = GetOptionalAbsDifference(
      streamer, info->FuncletOrFuncEnd, info->EpilogMap.begin()->first);
  if (!MaybeDistance)
    return -1;
  uint32_t DistanceFromEnd = (uint32_t)*MaybeDistance;
  uint32_t InstructionBytes = ARMCountOfInstructionBytes(Epilog);
  if (DistanceFromEnd != InstructionBytes)
    return -1;

  int RetVal = -1;
  // Even if we don't end up sharing opcodes with the prolog, we can still
  // write the offset as a packed offset, if the single epilog is located at
  // the end of the function and the offset (pointing after the prolog) fits
  // as a packed offset.
  if (PrologCodeBytes <= 31 &&
      PrologCodeBytes + ARMCountOfUnwindCodes(Epilog) <= 63)
    RetVal = PrologCodeBytes;

  int Offset =
      getARMOffsetInProlog(info->Instructions, Epilog, /*CanTweakProlog=*/true);
  if (Offset < 0)
    return RetVal;

  // Check that the offset and prolog size fits in the first word; it's
  // unclear whether the epilog count in the extension word can be taken
  // as packed epilog offset.
  if (Offset > 31 || PrologCodeBytes > 63)
    return RetVal;

  // Replace the regular end opcode of the prolog with the one from the
  // epilog.
  info->Instructions.front() = Epilog.back();

  // As we choose to express the epilog as part of the prolog, remove the
  // epilog from the map, so we don't try to emit its opcodes.
  info->EpilogMap.clear();
  return Offset;
}

static bool parseRegMask(unsigned Mask, bool &HasLR, bool &HasR11,
                         unsigned &Folded, int &IntRegs) {
  if (Mask & (1 << 14)) {
    HasLR = true;
    Mask &= ~(1 << 14);
  }
  if (Mask & (1 << 11)) {
    HasR11 = true;
    Mask &= ~(1 << 11);
  }
  Folded = 0;
  IntRegs = -1;
  if (!Mask)
    return true;
  int First = 0;
  // Shift right until we have the bits at the bottom
  while ((Mask & 1) == 0) {
    First++;
    Mask >>= 1;
  }
  if ((Mask & (Mask + 1)) != 0)
    return false; // Not a consecutive series of bits? Can't be packed.
  // Count the bits
  int N = 0;
  while (Mask & (1 << N))
    N++;
  if (First < 4) {
    if (First + N < 4)
      return false;
    Folded = 4 - First;
    N -= Folded;
    First = 4;
  }
  if (First > 4)
    return false; // Can't be packed
  if (N >= 1)
    IntRegs = N - 1;
  return true;
}

static bool tryARMPackedUnwind(MCStreamer &streamer, WinEH::FrameInfo *info,
                               uint32_t FuncLength) {
  int Step = 0;
  bool Homing = false;
  bool HasR11 = false;
  bool HasChain = false;
  bool HasLR = false;
  int IntRegs = -1;   // r4 - r(4+N)
  int FloatRegs = -1; // d8 - d(8+N)
  unsigned PF = 0;    // Number of extra pushed registers
  unsigned StackAdjust = 0;
  // Iterate over the prolog and check that all opcodes exactly match
  // the canonical order and form.
  for (const WinEH::Instruction &Inst : info->Instructions) {
    switch (Inst.Operation) {
    default:
      llvm_unreachable("Unsupported ARM unwind code");
    case Win64EH::UOP_Custom:
    case Win64EH::UOP_AllocLarge:
    case Win64EH::UOP_AllocHuge:
    case Win64EH::UOP_WideAllocLarge:
    case Win64EH::UOP_WideAllocHuge:
    case Win64EH::UOP_SaveFRegD0D15:
    case Win64EH::UOP_SaveFRegD16D31:
      // Can't be packed
      return false;
    case Win64EH::UOP_SaveSP:
      // Can't be packed; we can't rely on restoring sp from r11 when
      // unwinding a packed prologue.
      return false;
    case Win64EH::UOP_SaveLR:
      // Can't be present in a packed prologue
      return false;

    case Win64EH::UOP_End:
    case Win64EH::UOP_EndNop:
    case Win64EH::UOP_WideEndNop:
      if (Step != 0)
        return false;
      Step = 1;
      break;

    case Win64EH::UOP_SaveRegsR4R7LR:
    case Win64EH::UOP_WideSaveRegsR4R11LR:
      // push {r4-r11,lr}
      if (Step != 1 && Step != 2)
        return false;
      assert(Inst.Register >= 4 && Inst.Register <= 11); // r4-rX
      assert(Inst.Offset <= 1);                          // Lr
      IntRegs = Inst.Register - 4;
      if (Inst.Register == 11) {
        HasR11 = true;
        IntRegs--;
      }
      if (Inst.Offset)
        HasLR = true;
      Step = 3;
      break;

    case Win64EH::UOP_SaveRegMask:
      if (Step == 1 && Inst.Register == 0x0f) {
        // push {r0-r3}
        Homing = true;
        Step = 2;
        break;
      }
      [[fallthrough]];
    case Win64EH::UOP_WideSaveRegMask:
      if (Step != 1 && Step != 2)
        return false;
      // push {r4-r9,r11,lr}
      // push {r11,lr}
      // push {r1-r5}
      if (!parseRegMask(Inst.Register, HasLR, HasR11, PF, IntRegs))
        return false;
      Step = 3;
      break;

    case Win64EH::UOP_Nop:
      // mov r11, sp
      if (Step != 3 || !HasR11 || IntRegs >= 0 || PF > 0)
        return false;
      HasChain = true;
      Step = 4;
      break;
    case Win64EH::UOP_WideNop:
      // add.w r11, sp, #xx
      if (Step != 3 || !HasR11 || (IntRegs < 0 && PF == 0))
        return false;
      HasChain = true;
      Step = 4;
      break;

    case Win64EH::UOP_SaveFRegD8D15:
      if (Step != 1 && Step != 2 && Step != 3 && Step != 4)
        return false;
      assert(Inst.Register >= 8 && Inst.Register <= 15);
      if (Inst.Register == 15)
        return false; // Can't pack this case, R==7 means no IntRegs
      if (IntRegs >= 0)
        return false;
      FloatRegs = Inst.Register - 8;
      Step = 5;
      break;

    case Win64EH::UOP_AllocSmall:
    case Win64EH::UOP_WideAllocMedium:
      if (Step != 1 && Step != 2 && Step != 3 && Step != 4 && Step != 5)
        return false;
      if (PF > 0) // Can't have both folded and explicit stack allocation
        return false;
      if (Inst.Offset / 4 >= 0x3f4)
        return false;
      StackAdjust = Inst.Offset / 4;
      Step = 6;
      break;
    }
  }
  if (HasR11 && !HasChain) {
    if (IntRegs + 4 == 10) {
      // r11 stored, but not chaining; can be packed if already saving r4-r10
      // and we can fit r11 into this range.
      IntRegs++;
      HasR11 = false;
    } else
      return false;
  }
  if (HasChain && !HasLR)
    return false;

  // Packed uneind info can't express multiple epilogues.
  if (info->EpilogMap.size() > 1)
    return false;

  unsigned EF = 0;
  int Ret = 0;
  if (info->EpilogMap.size() == 0) {
    Ret = 3; // No epilogue
  } else {
    // As the prologue and epilogue aren't exact mirrors of each other,
    // we have to check the epilogue too and see if it matches what we've
    // concluded from the prologue.
    const WinEH::FrameInfo::Epilog &EpilogInfo =
        info->EpilogMap.begin()->second;
    if (EpilogInfo.Condition != 0xe) // ARMCC::AL
      return false;
    const std::vector<WinEH::Instruction> &Epilog = EpilogInfo.Instructions;
    std::optional<int64_t> MaybeDistance = GetOptionalAbsDifference(
        streamer, info->FuncletOrFuncEnd, info->EpilogMap.begin()->first);
    if (!MaybeDistance)
      return false;
    uint32_t DistanceFromEnd = (uint32_t)*MaybeDistance;
    uint32_t InstructionBytes = ARMCountOfInstructionBytes(Epilog);
    if (DistanceFromEnd != InstructionBytes)
      return false;

    bool GotStackAdjust = false;
    bool GotFloatRegs = false;
    bool GotIntRegs = false;
    bool GotHomingRestore = false;
    bool GotLRRestore = false;
    bool NeedsReturn = false;
    bool GotReturn = false;

    Step = 6;
    for (const WinEH::Instruction &Inst : Epilog) {
      switch (Inst.Operation) {
      default:
        llvm_unreachable("Unsupported ARM unwind code");
      case Win64EH::UOP_Custom:
      case Win64EH::UOP_AllocLarge:
      case Win64EH::UOP_AllocHuge:
      case Win64EH::UOP_WideAllocLarge:
      case Win64EH::UOP_WideAllocHuge:
      case Win64EH::UOP_SaveFRegD0D15:
      case Win64EH::UOP_SaveFRegD16D31:
      case Win64EH::UOP_SaveSP:
      case Win64EH::UOP_Nop:
      case Win64EH::UOP_WideNop:
        // Can't be packed in an epilogue
        return false;

      case Win64EH::UOP_AllocSmall:
      case Win64EH::UOP_WideAllocMedium:
        if (Inst.Offset / 4 >= 0x3f4)
          return false;
        if (Step == 6) {
          if (Homing && FloatRegs < 0 && IntRegs < 0 && StackAdjust == 0 &&
              PF == 0 && Inst.Offset == 16) {
            GotHomingRestore = true;
            Step = 10;
          } else {
            if (StackAdjust > 0) {
              // Got stack adjust in prologue too; must match.
              if (StackAdjust != Inst.Offset / 4)
                return false;
              GotStackAdjust = true;
            } else if (PF == Inst.Offset / 4) {
              // Folded prologue, non-folded epilogue
              StackAdjust = Inst.Offset / 4;
              GotStackAdjust = true;
            } else {
              // StackAdjust == 0 in prologue, mismatch
              return false;
            }
            Step = 7;
          }
        } else if (Step == 7 || Step == 8 || Step == 9) {
          if (!Homing || Inst.Offset != 16)
            return false;
          GotHomingRestore = true;
          Step = 10;
        } else
          return false;
        break;

      case Win64EH::UOP_SaveFRegD8D15:
        if (Step != 6 && Step != 7)
          return false;
        assert(Inst.Register >= 8 && Inst.Register <= 15);
        if (FloatRegs != (int)(Inst.Register - 8))
          return false;
        GotFloatRegs = true;
        Step = 8;
        break;

      case Win64EH::UOP_SaveRegsR4R7LR:
      case Win64EH::UOP_WideSaveRegsR4R11LR: {
        // push {r4-r11,lr}
        if (Step != 6 && Step != 7 && Step != 8)
          return false;
        assert(Inst.Register >= 4 && Inst.Register <= 11); // r4-rX
        assert(Inst.Offset <= 1);                          // Lr
        if (Homing && HasLR) {
          // If homing and LR is backed up, we can either restore LR here
          // and return with Ret == 1 or 2, or return with SaveLR below
          if (Inst.Offset) {
            GotLRRestore = true;
            NeedsReturn = true;
          } else {
            // Expecting a separate SaveLR below
          }
        } else {
          if (HasLR != (Inst.Offset == 1))
            return false;
        }
        GotLRRestore = Inst.Offset == 1;
        if (IntRegs < 0) // This opcode must include r4
          return false;
        int Expected = IntRegs;
        if (HasChain) {
          // Can't express r11 here unless IntRegs describe r4-r10
          if (IntRegs != 6)
            return false;
          Expected++;
        }
        if (Expected != (int)(Inst.Register - 4))
          return false;
        GotIntRegs = true;
        Step = 9;
        break;
      }

      case Win64EH::UOP_SaveRegMask:
      case Win64EH::UOP_WideSaveRegMask: {
        if (Step != 6 && Step != 7 && Step != 8)
          return false;
        // push {r4-r9,r11,lr}
        // push {r11,lr}
        // push {r1-r5}
        bool CurHasLR = false, CurHasR11 = false;
        int Regs;
        if (!parseRegMask(Inst.Register, CurHasLR, CurHasR11, EF, Regs))
          return false;
        if (EF > 0) {
          if (EF != PF && EF != StackAdjust)
            return false;
        }
        if (Homing && HasLR) {
          // If homing and LR is backed up, we can either restore LR here
          // and return with Ret == 1 or 2, or return with SaveLR below
          if (CurHasLR) {
            GotLRRestore = true;
            NeedsReturn = true;
          } else {
            // Expecting a separate SaveLR below
          }
        } else {
          if (CurHasLR != HasLR)
            return false;
          GotLRRestore = CurHasLR;
        }
        int Expected = IntRegs;
        if (HasChain) {
          // If we have chaining, the mask must have included r11.
          if (!CurHasR11)
            return false;
        } else if (Expected == 7) {
          // If we don't have chaining, the mask could still include r11,
          // expressed as part of IntRegs Instead.
          Expected--;
          if (!CurHasR11)
            return false;
        } else {
          // Neither HasChain nor r11 included in IntRegs, must not have r11
          // here either.
          if (CurHasR11)
            return false;
        }
        if (Expected != Regs)
          return false;
        GotIntRegs = true;
        Step = 9;
        break;
      }

      case Win64EH::UOP_SaveLR:
        if (Step != 6 && Step != 7 && Step != 8 && Step != 9)
          return false;
        if (!Homing || Inst.Offset != 20 || GotLRRestore)
          return false;
        GotLRRestore = true;
        GotHomingRestore = true;
        Step = 10;
        break;

      case Win64EH::UOP_EndNop:
      case Win64EH::UOP_WideEndNop:
        GotReturn = true;
        Ret = (Inst.Operation == Win64EH::UOP_EndNop) ? 1 : 2;
        [[fallthrough]];
      case Win64EH::UOP_End:
        if (Step != 6 && Step != 7 && Step != 8 && Step != 9 && Step != 10)
          return false;
        Step = 11;
        break;
      }
    }

    if (Step != 11)
      return false;
    if (StackAdjust > 0 && !GotStackAdjust && EF == 0)
      return false;
    if (FloatRegs >= 0 && !GotFloatRegs)
      return false;
    if (IntRegs >= 0 && !GotIntRegs)
      return false;
    if (Homing && !GotHomingRestore)
      return false;
    if (HasLR && !GotLRRestore)
      return false;
    if (NeedsReturn && !GotReturn)
      return false;
  }

  assert(PF == 0 || EF == 0 ||
         StackAdjust == 0); // Can't have adjust in all three
  if (PF > 0 || EF > 0) {
    StackAdjust = PF > 0 ? (PF - 1) : (EF - 1);
    assert(StackAdjust <= 3);
    StackAdjust |= 0x3f0;
    if (PF > 0)
      StackAdjust |= 1 << 2;
    if (EF > 0)
      StackAdjust |= 1 << 3;
  }

  assert(FuncLength <= 0x7FF && "FuncLength should have been checked earlier");
  int Flag = info->Fragment ? 0x02 : 0x01;
  int H = Homing ? 1 : 0;
  int L = HasLR ? 1 : 0;
  int C = HasChain ? 1 : 0;
  assert(IntRegs < 0 || FloatRegs < 0);
  unsigned Reg, R;
  if (IntRegs >= 0) {
    Reg = IntRegs;
    assert(Reg <= 7);
    R = 0;
  } else if (FloatRegs >= 0) {
    Reg = FloatRegs;
    assert(Reg < 7);
    R = 1;
  } else {
    // No int or float regs stored (except possibly R11,LR)
    Reg = 7;
    R = 1;
  }
  info->PackedInfo |= Flag << 0;
  info->PackedInfo |= (FuncLength & 0x7FF) << 2;
  info->PackedInfo |= (Ret & 0x3) << 13;
  info->PackedInfo |= H << 15;
  info->PackedInfo |= Reg << 16;
  info->PackedInfo |= R << 19;
  info->PackedInfo |= L << 20;
  info->PackedInfo |= C << 21;
  assert(StackAdjust <= 0x3ff);
  info->PackedInfo |= StackAdjust << 22;
  return true;
}

// Populate the .xdata section.  The format of .xdata on ARM is documented at
// https://docs.microsoft.com/en-us/cpp/build/arm-exception-handling
static void ARMEmitUnwindInfo(MCStreamer &streamer, WinEH::FrameInfo *info,
                              bool TryPacked = true) {
  // If this UNWIND_INFO already has a symbol, it's already been emitted.
  if (info->Symbol)
    return;
  // If there's no unwind info here (not even a terminating UOP_End), the
  // unwind info is considered bogus and skipped. If this was done in
  // response to an explicit .seh_handlerdata, the associated trailing
  // handler data is left orphaned in the xdata section.
  if (info->empty()) {
    info->EmitAttempted = true;
    return;
  }
  if (info->EmitAttempted) {
    // If we tried to emit unwind info before (due to an explicit
    // .seh_handlerdata directive), but skipped it (because there was no
    // valid information to emit at the time), and it later got valid unwind
    // opcodes, we can't emit it here, because the trailing handler data
    // was already emitted elsewhere in the xdata section.
    streamer.getContext().reportError(
        SMLoc(), "Earlier .seh_handlerdata for " + info->Function->getName() +
                     " skipped due to no unwind info at the time "
                     "(.seh_handlerdata too early?), but the function later "
                     "did get unwind info that can't be emitted");
    return;
  }

  MCContext &context = streamer.getContext();
  MCSymbol *Label = context.createTempSymbol();

  streamer.emitValueToAlignment(Align(4));
  streamer.emitLabel(Label);
  info->Symbol = Label;

  if (!info->PrologEnd)
    streamer.getContext().reportError(SMLoc(), "Prologue in " +
                                                   info->Function->getName() +
                                                   " not correctly terminated");

  if (info->PrologEnd && !info->Fragment)
    checkARMInstructions(streamer, info->Instructions, info->Begin,
                         info->PrologEnd, info->Function->getName(),
                         "prologue");
  for (auto &I : info->EpilogMap) {
    MCSymbol *EpilogStart = I.first;
    auto &Epilog = I.second;
    checkARMInstructions(streamer, Epilog.Instructions, EpilogStart, Epilog.End,
                         info->Function->getName(), "epilogue");
    if (Epilog.Instructions.empty() ||
        !isARMTerminator(Epilog.Instructions.back()))
      streamer.getContext().reportError(
          SMLoc(), "Epilogue in " + info->Function->getName() +
                       " not correctly terminated");
  }

  std::optional<int64_t> RawFuncLength;
  const MCExpr *FuncLengthExpr = nullptr;
  if (!info->FuncletOrFuncEnd) {
    report_fatal_error("FuncletOrFuncEnd not set");
  } else {
    // As the size of many thumb2 instructions isn't known until later,
    // we can't always rely on being able to calculate the absolute
    // length of the function here. If we can't calculate it, defer it
    // to a relocation.
    //
    // In such a case, we won't know if the function is too long so that
    // the unwind info would need to be split (but this isn't implemented
    // anyway).
    RawFuncLength =
        GetOptionalAbsDifference(streamer, info->FuncletOrFuncEnd, info->Begin);
    if (!RawFuncLength)
      FuncLengthExpr =
          GetSubDivExpr(streamer, info->FuncletOrFuncEnd, info->Begin, 2);
  }
  uint32_t FuncLength = 0;
  if (RawFuncLength)
    FuncLength = (uint32_t)*RawFuncLength / 2;
  if (FuncLength > 0x3FFFF)
    report_fatal_error("SEH unwind data splitting not yet implemented");
  uint32_t PrologCodeBytes = ARMCountOfUnwindCodes(info->Instructions);
  uint32_t TotalCodeBytes = PrologCodeBytes;

  if (!info->HandlesExceptions && RawFuncLength && FuncLength <= 0x7ff &&
      TryPacked) {
    // No exception handlers; check if the prolog and epilog matches the
    // patterns that can be described by the packed format. If we don't
    // know the exact function length yet, we can't do this.

    // info->Symbol was already set even if we didn't actually write any
    // unwind info there. Keep using that as indicator that this unwind
    // info has been generated already.

    if (tryARMPackedUnwind(streamer, info, FuncLength))
      return;
  }

  int PackedEpilogOffset =
      checkARMPackedEpilog(streamer, info, PrologCodeBytes);

  // Process epilogs.
  MapVector<MCSymbol *, uint32_t> EpilogInfo;
  // Epilogs processed so far.
  std::vector<MCSymbol *> AddedEpilogs;

  bool CanTweakProlog = true;
  for (auto &I : info->EpilogMap) {
    MCSymbol *EpilogStart = I.first;
    auto &EpilogInstrs = I.second.Instructions;
    uint32_t CodeBytes = ARMCountOfUnwindCodes(EpilogInstrs);

    MCSymbol *MatchingEpilog =
        FindMatchingEpilog(EpilogInstrs, AddedEpilogs, info);
    int PrologOffset;
    if (MatchingEpilog) {
      assert(EpilogInfo.contains(MatchingEpilog) &&
             "Duplicate epilog not found");
      EpilogInfo[EpilogStart] = EpilogInfo.lookup(MatchingEpilog);
      // Clear the unwind codes in the EpilogMap, so that they don't get output
      // in the logic below.
      EpilogInstrs.clear();
    } else if ((PrologOffset = getARMOffsetInProlog(
                    info->Instructions, EpilogInstrs, CanTweakProlog)) >= 0) {
      if (CanTweakProlog) {
        // Replace the regular end opcode of the prolog with the one from the
        // epilog.
        info->Instructions.front() = EpilogInstrs.back();
        // Later epilogs need a strict match for the end opcode.
        CanTweakProlog = false;
      }
      EpilogInfo[EpilogStart] = PrologOffset;
      // Clear the unwind codes in the EpilogMap, so that they don't get output
      // in the logic below.
      EpilogInstrs.clear();
    } else {
      EpilogInfo[EpilogStart] = TotalCodeBytes;
      TotalCodeBytes += CodeBytes;
      AddedEpilogs.push_back(EpilogStart);
    }
  }

  // Code Words, Epilog count, F, E, X, Vers, Function Length
  uint32_t row1 = 0x0;
  uint32_t CodeWords = TotalCodeBytes / 4;
  uint32_t CodeWordsMod = TotalCodeBytes % 4;
  if (CodeWordsMod)
    CodeWords++;
  uint32_t EpilogCount =
      PackedEpilogOffset >= 0 ? PackedEpilogOffset : info->EpilogMap.size();
  bool ExtensionWord = EpilogCount > 31 || CodeWords > 15;
  if (!ExtensionWord) {
    row1 |= (EpilogCount & 0x1F) << 23;
    row1 |= (CodeWords & 0x0F) << 28;
  }
  if (info->HandlesExceptions) // X
    row1 |= 1 << 20;
  if (PackedEpilogOffset >= 0) // E
    row1 |= 1 << 21;
  if (info->Fragment) // F
    row1 |= 1 << 22;
  row1 |= FuncLength & 0x3FFFF;
  if (RawFuncLength)
    streamer.emitInt32(row1);
  else
    streamer.emitValue(
        MCBinaryExpr::createOr(FuncLengthExpr,
                               MCConstantExpr::create(row1, context), context),
        4);

  // Extended Code Words, Extended Epilog Count
  if (ExtensionWord) {
    // FIXME: We should be able to split unwind info into multiple sections.
    if (CodeWords > 0xFF || EpilogCount > 0xFFFF)
      report_fatal_error("SEH unwind data splitting not yet implemented");
    uint32_t row2 = 0x0;
    row2 |= (CodeWords & 0xFF) << 16;
    row2 |= (EpilogCount & 0xFFFF);
    streamer.emitInt32(row2);
  }

  if (PackedEpilogOffset < 0) {
    // Epilog Start Index, Epilog Start Offset
    for (auto &I : EpilogInfo) {
      MCSymbol *EpilogStart = I.first;
      uint32_t EpilogIndex = I.second;

      std::optional<int64_t> MaybeEpilogOffset =
          GetOptionalAbsDifference(streamer, EpilogStart, info->Begin);
      const MCExpr *OffsetExpr = nullptr;
      uint32_t EpilogOffset = 0;
      if (MaybeEpilogOffset)
        EpilogOffset = *MaybeEpilogOffset / 2;
      else
        OffsetExpr = GetSubDivExpr(streamer, EpilogStart, info->Begin, 2);

      assert(info->EpilogMap.contains(EpilogStart));
      unsigned Condition = info->EpilogMap[EpilogStart].Condition;
      assert(Condition <= 0xf);

      uint32_t row3 = EpilogOffset;
      row3 |= Condition << 20;
      row3 |= (EpilogIndex & 0x3FF) << 24;
      if (MaybeEpilogOffset)
        streamer.emitInt32(row3);
      else
        streamer.emitValue(
            MCBinaryExpr::createOr(
                OffsetExpr, MCConstantExpr::create(row3, context), context),
            4);
    }
  }

  // Emit prolog unwind instructions (in reverse order).
  uint8_t numInst = info->Instructions.size();
  for (uint8_t c = 0; c < numInst; ++c) {
    WinEH::Instruction inst = info->Instructions.back();
    info->Instructions.pop_back();
    ARMEmitUnwindCode(streamer, inst);
  }

  // Emit epilog unwind instructions
  for (auto &I : info->EpilogMap) {
    auto &EpilogInstrs = I.second.Instructions;
    for (const WinEH::Instruction &inst : EpilogInstrs)
      ARMEmitUnwindCode(streamer, inst);
  }

  int32_t BytesMod = CodeWords * 4 - TotalCodeBytes;
  assert(BytesMod >= 0);
  for (int i = 0; i < BytesMod; i++)
    streamer.emitInt8(0xFB);

  if (info->HandlesExceptions)
    streamer.emitValue(
        MCSymbolRefExpr::create(info->ExceptionHandler,
                                MCSymbolRefExpr::VK_COFF_IMGREL32, context),
        4);
}

static void ARM64EmitRuntimeFunction(MCStreamer &streamer,
                                     const WinEH::FrameInfo *info) {
  MCContext &context = streamer.getContext();

  streamer.emitValueToAlignment(Align(4));
  for (const auto &S : info->Segments) {
    EmitSymbolRefWithOfs(streamer, info->Begin, S.Offset);
    if (info->PackedInfo)
      streamer.emitInt32(info->PackedInfo);
    else
      streamer.emitValue(
          MCSymbolRefExpr::create(S.Symbol, MCSymbolRefExpr::VK_COFF_IMGREL32,
                                  context),
          4);
  }
}


static void ARMEmitRuntimeFunction(MCStreamer &streamer,
                                   const WinEH::FrameInfo *info) {
  MCContext &context = streamer.getContext();

  streamer.emitValueToAlignment(Align(4));
  EmitSymbolRefWithOfs(streamer, info->Begin, info->Begin);
  if (info->PackedInfo)
    streamer.emitInt32(info->PackedInfo);
  else
    streamer.emitValue(
        MCSymbolRefExpr::create(info->Symbol, MCSymbolRefExpr::VK_COFF_IMGREL32,
                                context),
        4);
}

void llvm::Win64EH::ARM64UnwindEmitter::Emit(MCStreamer &Streamer) const {
  // Emit the unwind info structs first.
  for (const auto &CFI : Streamer.getWinFrameInfos()) {
    WinEH::FrameInfo *Info = CFI.get();
    if (Info->empty())
      continue;
    MCSection *XData = Streamer.getAssociatedXDataSection(CFI->TextSection);
    Streamer.switchSection(XData);
    ARM64EmitUnwindInfo(Streamer, Info);
  }

  // Now emit RUNTIME_FUNCTION entries.
  for (const auto &CFI : Streamer.getWinFrameInfos()) {
    WinEH::FrameInfo *Info = CFI.get();
    // ARM64EmitUnwindInfo above clears the info struct, so we can't check
    // empty here. But if a Symbol is set, we should create the corresponding
    // pdata entry.
    if (!Info->Symbol)
      continue;
    MCSection *PData = Streamer.getAssociatedPDataSection(CFI->TextSection);
    Streamer.switchSection(PData);
    ARM64EmitRuntimeFunction(Streamer, Info);
  }
}

void llvm::Win64EH::ARM64UnwindEmitter::EmitUnwindInfo(MCStreamer &Streamer,
                                                       WinEH::FrameInfo *info,
                                                       bool HandlerData) const {
  // Called if there's an .seh_handlerdata directive before the end of the
  // function. This forces writing the xdata record already here - and
  // in this case, the function isn't actually ended already, but the xdata
  // record needs to know the function length. In these cases, if the funclet
  // end hasn't been marked yet, the xdata function length won't cover the
  // whole function, only up to this point.
  if (!info->FuncletOrFuncEnd) {
    Streamer.switchSection(info->TextSection);
    info->FuncletOrFuncEnd = Streamer.emitCFILabel();
  }
  // Switch sections (the static function above is meant to be called from
  // here and from Emit().
  MCSection *XData = Streamer.getAssociatedXDataSection(info->TextSection);
  Streamer.switchSection(XData);
  ARM64EmitUnwindInfo(Streamer, info, /* TryPacked = */ !HandlerData);
}

void llvm::Win64EH::ARMUnwindEmitter::Emit(MCStreamer &Streamer) const {
  // Emit the unwind info structs first.
  for (const auto &CFI : Streamer.getWinFrameInfos()) {
    WinEH::FrameInfo *Info = CFI.get();
    if (Info->empty())
      continue;
    MCSection *XData = Streamer.getAssociatedXDataSection(CFI->TextSection);
    Streamer.switchSection(XData);
    ARMEmitUnwindInfo(Streamer, Info);
  }

  // Now emit RUNTIME_FUNCTION entries.
  for (const auto &CFI : Streamer.getWinFrameInfos()) {
    WinEH::FrameInfo *Info = CFI.get();
    // ARMEmitUnwindInfo above clears the info struct, so we can't check
    // empty here. But if a Symbol is set, we should create the corresponding
    // pdata entry.
    if (!Info->Symbol)
      continue;
    MCSection *PData = Streamer.getAssociatedPDataSection(CFI->TextSection);
    Streamer.switchSection(PData);
    ARMEmitRuntimeFunction(Streamer, Info);
  }
}

void llvm::Win64EH::ARMUnwindEmitter::EmitUnwindInfo(MCStreamer &Streamer,
                                                     WinEH::FrameInfo *info,
                                                     bool HandlerData) const {
  // Called if there's an .seh_handlerdata directive before the end of the
  // function. This forces writing the xdata record already here - and
  // in this case, the function isn't actually ended already, but the xdata
  // record needs to know the function length. In these cases, if the funclet
  // end hasn't been marked yet, the xdata function length won't cover the
  // whole function, only up to this point.
  if (!info->FuncletOrFuncEnd) {
    Streamer.switchSection(info->TextSection);
    info->FuncletOrFuncEnd = Streamer.emitCFILabel();
  }
  // Switch sections (the static function above is meant to be called from
  // here and from Emit().
  MCSection *XData = Streamer.getAssociatedXDataSection(info->TextSection);
  Streamer.switchSection(XData);
  ARMEmitUnwindInfo(Streamer, info, /* TryPacked = */ !HandlerData);
}
