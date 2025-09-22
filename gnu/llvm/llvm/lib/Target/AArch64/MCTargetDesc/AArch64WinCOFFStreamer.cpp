//===-- AArch64WinCOFFStreamer.cpp - ARM Target WinCOFF Streamer ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AArch64WinCOFFStreamer.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAssembler.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCWin64EH.h"
#include "llvm/MC/MCWinCOFFStreamer.h"

using namespace llvm;

namespace {

class AArch64WinCOFFStreamer : public MCWinCOFFStreamer {
  Win64EH::ARM64UnwindEmitter EHStreamer;

public:
  AArch64WinCOFFStreamer(MCContext &C, std::unique_ptr<MCAsmBackend> AB,
                         std::unique_ptr<MCCodeEmitter> CE,
                         std::unique_ptr<MCObjectWriter> OW)
      : MCWinCOFFStreamer(C, std::move(AB), std::move(CE), std::move(OW)) {}

  void emitWinEHHandlerData(SMLoc Loc) override;
  void emitWindowsUnwindTables() override;
  void emitWindowsUnwindTables(WinEH::FrameInfo *Frame) override;
  void finishImpl() override;
};

void AArch64WinCOFFStreamer::emitWinEHHandlerData(SMLoc Loc) {
  MCStreamer::emitWinEHHandlerData(Loc);

  // We have to emit the unwind info now, because this directive
  // actually switches to the .xdata section!
  EHStreamer.EmitUnwindInfo(*this, getCurrentWinFrameInfo(),
                            /* HandlerData = */ true);
}

void AArch64WinCOFFStreamer::emitWindowsUnwindTables(WinEH::FrameInfo *Frame) {
  EHStreamer.EmitUnwindInfo(*this, Frame, /* HandlerData = */ false);
}

void AArch64WinCOFFStreamer::emitWindowsUnwindTables() {
  if (!getNumWinFrameInfos())
    return;
  EHStreamer.Emit(*this);
}

void AArch64WinCOFFStreamer::finishImpl() {
  emitFrames(nullptr);
  emitWindowsUnwindTables();

  MCWinCOFFStreamer::finishImpl();
}
} // end anonymous namespace

// Helper function to common out unwind code setup for those codes that can
// belong to both prolog and epilog.
// There are three types of Windows ARM64 SEH codes.  They can
// 1) take no operands: SEH_Nop, SEH_PrologEnd, SEH_EpilogStart, SEH_EpilogEnd
// 2) take an offset: SEH_StackAlloc, SEH_SaveFPLR, SEH_SaveFPLR_X
// 3) take a register and an offset/size: all others
void AArch64TargetWinCOFFStreamer::emitARM64WinUnwindCode(unsigned UnwindCode,
                                                          int Reg, int Offset) {
  auto &S = getStreamer();
  WinEH::FrameInfo *CurFrame = S.EnsureValidWinFrameInfo(SMLoc());
  if (!CurFrame)
    return;
  auto Inst = WinEH::Instruction(UnwindCode, /*Label=*/nullptr, Reg, Offset);
  if (InEpilogCFI)
    CurFrame->EpilogMap[CurrentEpilog].Instructions.push_back(Inst);
  else
    CurFrame->Instructions.push_back(Inst);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFIAllocStack(unsigned Size) {
  unsigned Op = Win64EH::UOP_AllocSmall;
  if (Size >= 16384)
    Op = Win64EH::UOP_AllocLarge;
  else if (Size >= 512)
    Op = Win64EH::UOP_AllocMedium;
  emitARM64WinUnwindCode(Op, -1, Size);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveR19R20X(int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveR19R20X, -1, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveFPLR(int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveFPLR, -1, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveFPLRX(int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveFPLRX, -1, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveReg(unsigned Reg,
                                                          int Offset) {
  assert(Offset >= 0 && Offset <= 504 &&
        "Offset for save reg should be >= 0 && <= 504");
  emitARM64WinUnwindCode(Win64EH::UOP_SaveReg, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveRegX(unsigned Reg,
                                                           int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveRegX, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveRegP(unsigned Reg,
                                                           int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveRegP, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveRegPX(unsigned Reg,
                                                            int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveRegPX, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveLRPair(unsigned Reg,
                                                             int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveLRPair, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveFReg(unsigned Reg,
                                                           int Offset) {
  assert(Offset >= 0 && Offset <= 504 &&
        "Offset for save reg should be >= 0 && <= 504");
  emitARM64WinUnwindCode(Win64EH::UOP_SaveFReg, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveFRegX(unsigned Reg,
                                                            int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveFRegX, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveFRegP(unsigned Reg,
                                                            int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveFRegP, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveFRegPX(unsigned Reg,
                                                             int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveFRegPX, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISetFP() {
  emitARM64WinUnwindCode(Win64EH::UOP_SetFP, -1, 0);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFIAddFP(unsigned Offset) {
  assert(Offset <= 2040 && "UOP_AddFP must have offset <= 2040");
  emitARM64WinUnwindCode(Win64EH::UOP_AddFP, -1, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFINop() {
  emitARM64WinUnwindCode(Win64EH::UOP_Nop, -1, 0);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveNext() {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveNext, -1, 0);
}

// The functions below handle opcodes that can end up in either a prolog or
// an epilog, but not both.
void AArch64TargetWinCOFFStreamer::emitARM64WinCFIPrologEnd() {
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
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFIEpilogStart() {
  auto &S = getStreamer();
  WinEH::FrameInfo *CurFrame = S.EnsureValidWinFrameInfo(SMLoc());
  if (!CurFrame)
    return;

  InEpilogCFI = true;
  CurrentEpilog = S.emitCFILabel();
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFIEpilogEnd() {
  auto &S = getStreamer();
  WinEH::FrameInfo *CurFrame = S.EnsureValidWinFrameInfo(SMLoc());
  if (!CurFrame)
    return;

  InEpilogCFI = false;
  WinEH::Instruction Inst =
      WinEH::Instruction(Win64EH::UOP_End, /*Label=*/nullptr, -1, 0);
  CurFrame->EpilogMap[CurrentEpilog].Instructions.push_back(Inst);
  MCSymbol *Label = S.emitCFILabel();
  CurFrame->EpilogMap[CurrentEpilog].End = Label;
  CurrentEpilog = nullptr;
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFITrapFrame() {
  emitARM64WinUnwindCode(Win64EH::UOP_TrapFrame, -1, 0);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFIMachineFrame() {
  emitARM64WinUnwindCode(Win64EH::UOP_PushMachFrame, -1, 0);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFIContext() {
  emitARM64WinUnwindCode(Win64EH::UOP_Context, -1, 0);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFIECContext() {
  emitARM64WinUnwindCode(Win64EH::UOP_ECContext, -1, 0);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFIClearUnwoundToCall() {
  emitARM64WinUnwindCode(Win64EH::UOP_ClearUnwoundToCall, -1, 0);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFIPACSignLR() {
  emitARM64WinUnwindCode(Win64EH::UOP_PACSignLR, -1, 0);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveAnyRegI(unsigned Reg,
                                                              int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveAnyRegI, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveAnyRegIP(unsigned Reg,
                                                               int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveAnyRegIP, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveAnyRegD(unsigned Reg,
                                                              int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveAnyRegD, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveAnyRegDP(unsigned Reg,
                                                               int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveAnyRegDP, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveAnyRegQ(unsigned Reg,
                                                              int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveAnyRegQ, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveAnyRegQP(unsigned Reg,
                                                               int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveAnyRegQP, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveAnyRegIX(unsigned Reg,
                                                               int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveAnyRegIX, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveAnyRegIPX(unsigned Reg,
                                                                int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveAnyRegIPX, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveAnyRegDX(unsigned Reg,
                                                               int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveAnyRegDX, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveAnyRegDPX(unsigned Reg,
                                                                int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveAnyRegDPX, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveAnyRegQX(unsigned Reg,
                                                               int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveAnyRegQX, Reg, Offset);
}

void AArch64TargetWinCOFFStreamer::emitARM64WinCFISaveAnyRegQPX(unsigned Reg,
                                                                int Offset) {
  emitARM64WinUnwindCode(Win64EH::UOP_SaveAnyRegQPX, Reg, Offset);
}

MCWinCOFFStreamer *
llvm::createAArch64WinCOFFStreamer(MCContext &Context,
                                   std::unique_ptr<MCAsmBackend> MAB,
                                   std::unique_ptr<MCObjectWriter> OW,
                                   std::unique_ptr<MCCodeEmitter> Emitter) {
  return new AArch64WinCOFFStreamer(Context, std::move(MAB), std::move(Emitter),
                                    std::move(OW));
}
