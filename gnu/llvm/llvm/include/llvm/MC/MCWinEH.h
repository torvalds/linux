//===- MCWinEH.h - Windows Unwinding Support --------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCWINEH_H
#define LLVM_MC_MCWINEH_H

#include "llvm/ADT/MapVector.h"
#include <vector>

namespace llvm {
class MCSection;
class MCStreamer;
class MCSymbol;

namespace WinEH {
struct Instruction {
  const MCSymbol *Label;
  unsigned Offset;
  unsigned Register;
  unsigned Operation;

  Instruction(unsigned Op, MCSymbol *L, unsigned Reg, unsigned Off)
    : Label(L), Offset(Off), Register(Reg), Operation(Op) {}

  bool operator==(const Instruction &I) const {
    // Check whether two instructions refer to the same operation
    // applied at a different spot (i.e. pointing at a different label).
    return Offset == I.Offset && Register == I.Register &&
           Operation == I.Operation;
  }
  bool operator!=(const Instruction &I) const { return !(*this == I); }
};

struct FrameInfo {
  const MCSymbol *Begin = nullptr;
  const MCSymbol *End = nullptr;
  const MCSymbol *FuncletOrFuncEnd = nullptr;
  const MCSymbol *ExceptionHandler = nullptr;
  const MCSymbol *Function = nullptr;
  const MCSymbol *PrologEnd = nullptr;
  const MCSymbol *Symbol = nullptr;
  MCSection *TextSection = nullptr;
  uint32_t PackedInfo = 0;
  uint32_t PrologCodeBytes = 0;

  bool HandlesUnwind = false;
  bool HandlesExceptions = false;
  bool EmitAttempted = false;
  bool Fragment = false;

  int LastFrameInst = -1;
  const FrameInfo *ChainedParent = nullptr;
  std::vector<Instruction> Instructions;
  struct Epilog {
    std::vector<Instruction> Instructions;
    unsigned Condition;
    MCSymbol *End;
  };
  MapVector<MCSymbol *, Epilog> EpilogMap;

  // For splitting unwind info of large functions
  struct Segment {
    int64_t Offset;
    int64_t Length;
    bool HasProlog;
    MCSymbol *Symbol = nullptr;
    // Map an Epilog's symbol to its offset within the function.
    MapVector<MCSymbol *, int64_t> Epilogs;

    Segment(int64_t Offset, int64_t Length, bool HasProlog = false)
        : Offset(Offset), Length(Length), HasProlog(HasProlog) {}
  };

  std::vector<Segment> Segments;

  FrameInfo() = default;
  FrameInfo(const MCSymbol *Function, const MCSymbol *BeginFuncEHLabel)
      : Begin(BeginFuncEHLabel), Function(Function) {}
  FrameInfo(const MCSymbol *Function, const MCSymbol *BeginFuncEHLabel,
            const FrameInfo *ChainedParent)
      : Begin(BeginFuncEHLabel), Function(Function),
        ChainedParent(ChainedParent) {}

  bool empty() const {
    if (!Instructions.empty())
      return false;
    for (const auto &E : EpilogMap)
      if (!E.second.Instructions.empty())
        return false;
    return true;
  }
};

class UnwindEmitter {
public:
  virtual ~UnwindEmitter();

  /// This emits the unwind info sections (.pdata and .xdata in PE/COFF).
  virtual void Emit(MCStreamer &Streamer) const = 0;
  virtual void EmitUnwindInfo(MCStreamer &Streamer, FrameInfo *FI,
                              bool HandlerData) const = 0;
};
}
}

#endif
