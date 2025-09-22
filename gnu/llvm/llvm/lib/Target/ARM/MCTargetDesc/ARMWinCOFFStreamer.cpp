//===-- ARMWinCOFFStreamer.cpp - ARM Target WinCOFF Streamer ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ARMMCTargetDesc.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCWin64EH.h"
#include "llvm/MC/MCWinCOFFStreamer.h"

using namespace llvm;

namespace {
class ARMWinCOFFStreamer : public MCWinCOFFStreamer {
  Win64EH::ARMUnwindEmitter EHStreamer;

public:
  ARMWinCOFFStreamer(MCContext &C, std::unique_ptr<MCAsmBackend> AB,
                     std::unique_ptr<MCCodeEmitter> CE,
                     std::unique_ptr<MCObjectWriter> OW)
      : MCWinCOFFStreamer(C, std::move(AB), std::move(CE), std::move(OW)) {}

  void emitWinEHHandlerData(SMLoc Loc) override;
  void emitWindowsUnwindTables() override;
  void emitWindowsUnwindTables(WinEH::FrameInfo *Frame) override;

  void emitThumbFunc(MCSymbol *Symbol) override;
  void finishImpl() override;
};

void ARMWinCOFFStreamer::emitWinEHHandlerData(SMLoc Loc) {
  MCStreamer::emitWinEHHandlerData(Loc);

  // We have to emit the unwind info now, because this directive
  // actually switches to the .xdata section!
  EHStreamer.EmitUnwindInfo(*this, getCurrentWinFrameInfo(),
                            /* HandlerData = */ true);
}

void ARMWinCOFFStreamer::emitWindowsUnwindTables(WinEH::FrameInfo *Frame) {
  EHStreamer.EmitUnwindInfo(*this, Frame, /* HandlerData = */ false);
}

void ARMWinCOFFStreamer::emitWindowsUnwindTables() {
  if (!getNumWinFrameInfos())
    return;
  EHStreamer.Emit(*this);
}

void ARMWinCOFFStreamer::emitThumbFunc(MCSymbol *Symbol) {
  getAssembler().setIsThumbFunc(Symbol);
}

void ARMWinCOFFStreamer::finishImpl() {
  emitFrames(nullptr);
  emitWindowsUnwindTables();

  MCWinCOFFStreamer::finishImpl();
}
}

MCStreamer *
llvm::createARMWinCOFFStreamer(MCContext &Context,
                               std::unique_ptr<MCAsmBackend> &&MAB,
                               std::unique_ptr<MCObjectWriter> &&OW,
                               std::unique_ptr<MCCodeEmitter> &&Emitter) {
  return new ARMWinCOFFStreamer(Context, std::move(MAB), std::move(Emitter),
                                std::move(OW));
}

namespace {
class ARMTargetWinCOFFStreamer : public llvm::ARMTargetStreamer {
private:
  // True if we are processing SEH directives in an epilogue.
  bool InEpilogCFI = false;

  // Symbol of the current epilog for which we are processing SEH directives.
  MCSymbol *CurrentEpilog = nullptr;

public:
  ARMTargetWinCOFFStreamer(llvm::MCStreamer &S) : ARMTargetStreamer(S) {}

  // The unwind codes on ARM Windows are documented at
  // https://docs.microsoft.com/en-us/cpp/build/arm-exception-handling
  void emitARMWinCFIAllocStack(unsigned Size, bool Wide) override;
  void emitARMWinCFISaveRegMask(unsigned Mask, bool Wide) override;
  void emitARMWinCFISaveSP(unsigned Reg) override;
  void emitARMWinCFISaveFRegs(unsigned First, unsigned Last) override;
  void emitARMWinCFISaveLR(unsigned Offset) override;
  void emitARMWinCFIPrologEnd(bool Fragment) override;
  void emitARMWinCFINop(bool Wide) override;
  void emitARMWinCFIEpilogStart(unsigned Condition) override;
  void emitARMWinCFIEpilogEnd() override;
  void emitARMWinCFICustom(unsigned Opcode) override;

private:
  void emitARMWinUnwindCode(unsigned UnwindCode, int Reg, int Offset);
};

// Helper function to common out unwind code setup for those codes that can
// belong to both prolog and epilog.
void ARMTargetWinCOFFStreamer::emitARMWinUnwindCode(unsigned UnwindCode,
                                                    int Reg, int Offset) {
  auto &S = getStreamer();
  WinEH::FrameInfo *CurFrame = S.EnsureValidWinFrameInfo(SMLoc());
  if (!CurFrame)
    return;
  MCSymbol *Label = S.emitCFILabel();
  auto Inst = WinEH::Instruction(UnwindCode, Label, Reg, Offset);
  if (InEpilogCFI)
    CurFrame->EpilogMap[CurrentEpilog].Instructions.push_back(Inst);
  else
    CurFrame->Instructions.push_back(Inst);
}

void ARMTargetWinCOFFStreamer::emitARMWinCFIAllocStack(unsigned Size,
                                                       bool Wide) {
  unsigned Op = Win64EH::UOP_AllocSmall;
  if (!Wide) {
    if (Size / 4 > 0xffff)
      Op = Win64EH::UOP_AllocHuge;
    else if (Size / 4 > 0x7f)
      Op = Win64EH::UOP_AllocLarge;
  } else {
    Op = Win64EH::UOP_WideAllocMedium;
    if (Size / 4 > 0xffff)
      Op = Win64EH::UOP_WideAllocHuge;
    else if (Size / 4 > 0x3ff)
      Op = Win64EH::UOP_WideAllocLarge;
  }
  emitARMWinUnwindCode(Op, -1, Size);
}

void ARMTargetWinCOFFStreamer::emitARMWinCFISaveRegMask(unsigned Mask,
                                                        bool Wide) {
  assert(Mask != 0);
  int Lr = (Mask & 0x4000) ? 1 : 0;
  Mask &= ~0x4000;
  if (Wide)
    assert((Mask & ~0x1fff) == 0);
  else
    assert((Mask & ~0x00ff) == 0);
  if (Mask && ((Mask + (1 << 4)) & Mask) == 0) {
    if (Wide && (Mask & 0x1000) == 0 && (Mask & 0xff) == 0xf0) {
      // One continuous range from r4 to r8-r11
      for (int I = 11; I >= 8; I--) {
        if (Mask & (1 << I)) {
          emitARMWinUnwindCode(Win64EH::UOP_WideSaveRegsR4R11LR, I, Lr);
          return;
        }
      }
      // If it actually was from r4 to r4-r7, continue below.
    } else if (!Wide) {
      // One continuous range from r4 to r4-r7
      for (int I = 7; I >= 4; I--) {
        if (Mask & (1 << I)) {
          emitARMWinUnwindCode(Win64EH::UOP_SaveRegsR4R7LR, I, Lr);
          return;
        }
      }
      llvm_unreachable("logic error");
    }
  }
  Mask |= Lr << 14;
  if (Wide)
    emitARMWinUnwindCode(Win64EH::UOP_WideSaveRegMask, Mask, 0);
  else
    emitARMWinUnwindCode(Win64EH::UOP_SaveRegMask, Mask, 0);
}

void ARMTargetWinCOFFStreamer::emitARMWinCFISaveSP(unsigned Reg) {
  emitARMWinUnwindCode(Win64EH::UOP_SaveSP, Reg, 0);
}

void ARMTargetWinCOFFStreamer::emitARMWinCFISaveFRegs(unsigned First,
                                                      unsigned Last) {
  assert(First <= Last);
  assert(First >= 16 || Last < 16);
  assert(First <= 31 && Last <= 31);
  if (First == 8)
    emitARMWinUnwindCode(Win64EH::UOP_SaveFRegD8D15, Last, 0);
  else if (First <= 15)
    emitARMWinUnwindCode(Win64EH::UOP_SaveFRegD0D15, First, Last);
  else
    emitARMWinUnwindCode(Win64EH::UOP_SaveFRegD16D31, First, Last);
}

void ARMTargetWinCOFFStreamer::emitARMWinCFISaveLR(unsigned Offset) {
  emitARMWinUnwindCode(Win64EH::UOP_SaveLR, 0, Offset);
}

void ARMTargetWinCOFFStreamer::emitARMWinCFINop(bool Wide) {
  if (Wide)
    emitARMWinUnwindCode(Win64EH::UOP_WideNop, -1, 0);
  else
    emitARMWinUnwindCode(Win64EH::UOP_Nop, -1, 0);
}

void ARMTargetWinCOFFStreamer::emitARMWinCFIPrologEnd(bool Fragment) {
  auto &S = getStreamer();
  WinEH::FrameInfo *CurFrame = S.EnsureValidWinFrameInfo(SMLoc());
  if (!CurFrame)
    return;

  MCSymbol *Label = S.emitCFILabel();
  CurFrame->PrologEnd = Label;
  WinEH::Instruction Inst =
      WinEH::Instruction(Win64EH::UOP_End, /*Label=*/nullptr, -1, 0);
  auto it = CurFrame->Instructions.begin();
  CurFrame->Instructions.insert(it, Inst);
  CurFrame->Fragment = Fragment;
}

void ARMTargetWinCOFFStreamer::emitARMWinCFIEpilogStart(unsigned Condition) {
  auto &S = getStreamer();
  WinEH::FrameInfo *CurFrame = S.EnsureValidWinFrameInfo(SMLoc());
  if (!CurFrame)
    return;

  InEpilogCFI = true;
  CurrentEpilog = S.emitCFILabel();
  CurFrame->EpilogMap[CurrentEpilog].Condition = Condition;
}

void ARMTargetWinCOFFStreamer::emitARMWinCFIEpilogEnd() {
  auto &S = getStreamer();
  WinEH::FrameInfo *CurFrame = S.EnsureValidWinFrameInfo(SMLoc());
  if (!CurFrame)
    return;

  if (!CurrentEpilog) {
    S.getContext().reportError(SMLoc(), "Stray .seh_endepilogue in " +
                                            CurFrame->Function->getName());
    return;
  }

  std::vector<WinEH::Instruction> &Epilog =
      CurFrame->EpilogMap[CurrentEpilog].Instructions;

  unsigned UnwindCode = Win64EH::UOP_End;
  if (!Epilog.empty()) {
    WinEH::Instruction EndInstr = Epilog.back();
    if (EndInstr.Operation == Win64EH::UOP_Nop) {
      UnwindCode = Win64EH::UOP_EndNop;
      Epilog.pop_back();
    } else if (EndInstr.Operation == Win64EH::UOP_WideNop) {
      UnwindCode = Win64EH::UOP_WideEndNop;
      Epilog.pop_back();
    }
  }

  InEpilogCFI = false;
  WinEH::Instruction Inst = WinEH::Instruction(UnwindCode, nullptr, -1, 0);
  CurFrame->EpilogMap[CurrentEpilog].Instructions.push_back(Inst);
  MCSymbol *Label = S.emitCFILabel();
  CurFrame->EpilogMap[CurrentEpilog].End = Label;
  CurrentEpilog = nullptr;
}

void ARMTargetWinCOFFStreamer::emitARMWinCFICustom(unsigned Opcode) {
  emitARMWinUnwindCode(Win64EH::UOP_Custom, 0, Opcode);
}

} // end anonymous namespace

MCTargetStreamer *llvm::createARMObjectTargetWinCOFFStreamer(MCStreamer &S) {
  return new ARMTargetWinCOFFStreamer(S);
}
