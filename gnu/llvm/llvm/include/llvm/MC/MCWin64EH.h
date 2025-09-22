//===- MCWin64EH.h - Machine Code Win64 EH support --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains declarations to support the Win64 Exception Handling
// scheme in MC.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCWIN64EH_H
#define LLVM_MC_MCWIN64EH_H

#include "llvm/MC/MCWinEH.h"
#include "llvm/Support/Win64EH.h"

namespace llvm {
class MCStreamer;
class MCSymbol;

namespace Win64EH {
struct Instruction {
  static WinEH::Instruction PushNonVol(MCSymbol *L, unsigned Reg) {
    return WinEH::Instruction(Win64EH::UOP_PushNonVol, L, Reg, -1);
  }
  static WinEH::Instruction Alloc(MCSymbol *L, unsigned Size) {
    return WinEH::Instruction(Size > 128 ? UOP_AllocLarge : UOP_AllocSmall, L,
                              -1, Size);
  }
  static WinEH::Instruction PushMachFrame(MCSymbol *L, bool Code) {
    return WinEH::Instruction(UOP_PushMachFrame, L, -1, Code ? 1 : 0);
  }
  static WinEH::Instruction SaveNonVol(MCSymbol *L, unsigned Reg,
                                       unsigned Offset) {
    return WinEH::Instruction(Offset > 512 * 1024 - 8 ? UOP_SaveNonVolBig
                                                      : UOP_SaveNonVol,
                              L, Reg, Offset);
  }
  static WinEH::Instruction SaveXMM(MCSymbol *L, unsigned Reg,
                                    unsigned Offset) {
    return WinEH::Instruction(Offset > 512 * 1024 - 8 ? UOP_SaveXMM128Big
                                                      : UOP_SaveXMM128,
                              L, Reg, Offset);
  }
  static WinEH::Instruction SetFPReg(MCSymbol *L, unsigned Reg, unsigned Off) {
    return WinEH::Instruction(UOP_SetFPReg, L, Reg, Off);
  }
};

class UnwindEmitter : public WinEH::UnwindEmitter {
public:
  void Emit(MCStreamer &Streamer) const override;
  void EmitUnwindInfo(MCStreamer &Streamer, WinEH::FrameInfo *FI,
                      bool HandlerData) const override;
};

class ARMUnwindEmitter : public WinEH::UnwindEmitter {
public:
  void Emit(MCStreamer &Streamer) const override;
  void EmitUnwindInfo(MCStreamer &Streamer, WinEH::FrameInfo *FI,
                      bool HandlerData) const override;
};

class ARM64UnwindEmitter : public WinEH::UnwindEmitter {
public:
  void Emit(MCStreamer &Streamer) const override;
  void EmitUnwindInfo(MCStreamer &Streamer, WinEH::FrameInfo *FI,
                      bool HandlerData) const override;
};
}
} // end namespace llvm

#endif
