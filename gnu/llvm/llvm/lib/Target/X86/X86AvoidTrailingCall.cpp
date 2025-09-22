//===----- X86AvoidTrailingCall.cpp - Insert int3 after trailing calls ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The Windows x64 unwinder decodes the instruction stream during unwinding.
// The unwinder decodes forward from the current PC to detect epilogue code
// patterns.
//
// First, this means that there must be an instruction after every
// call instruction for the unwinder to decode. LLVM must maintain the invariant
// that the last instruction of a function or funclet is not a call, or the
// unwinder may decode into the next function. Similarly, a call may not
// immediately precede an epilogue code pattern. As of this writing, the
// SEH_Epilogue pseudo instruction takes care of that.
//
// Second, all non-tail call jump targets must be within the *half-open*
// interval of the bounds of the function. The unwinder distinguishes between
// internal jump instructions and tail calls in an epilogue sequence by checking
// the jump target against the function bounds from the .pdata section. This
// means that the last regular MBB of an LLVM function must not be empty if
// there are regular jumps targeting it.
//
// This pass upholds these invariants by ensuring that blocks at the end of a
// function or funclet are a) not empty and b) do not end in a CALL instruction.
//
// Unwinder implementation for reference:
// https://github.com/dotnet/coreclr/blob/a9f3fc16483eecfc47fb79c362811d870be02249/src/unwinder/amd64/unwinder_amd64.cpp#L1015
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"

#define AVOIDCALL_DESC "X86 avoid trailing call pass"
#define AVOIDCALL_NAME "x86-avoid-trailing-call"

#define DEBUG_TYPE AVOIDCALL_NAME

using namespace llvm;

namespace {
class X86AvoidTrailingCallPass : public MachineFunctionPass {
public:
  X86AvoidTrailingCallPass() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  static char ID;

private:
  StringRef getPassName() const override { return AVOIDCALL_DESC; }
};
} // end anonymous namespace

char X86AvoidTrailingCallPass::ID = 0;

FunctionPass *llvm::createX86AvoidTrailingCallPass() {
  return new X86AvoidTrailingCallPass();
}

INITIALIZE_PASS(X86AvoidTrailingCallPass, AVOIDCALL_NAME, AVOIDCALL_DESC, false, false)

// A real instruction is a non-meta, non-pseudo instruction.  Some pseudos
// expand to nothing, and some expand to code. This logic conservatively assumes
// they might expand to nothing.
static bool isCallOrRealInstruction(MachineInstr &MI) {
  return MI.isCall() || (!MI.isPseudo() && !MI.isMetaInstruction());
}

// Return true if this is a call instruction, but not a tail call.
static bool isCallInstruction(const MachineInstr &MI) {
  return MI.isCall() && !MI.isReturn();
}

bool X86AvoidTrailingCallPass::runOnMachineFunction(MachineFunction &MF) {
  const X86Subtarget &STI = MF.getSubtarget<X86Subtarget>();
  const X86InstrInfo &TII = *STI.getInstrInfo();
  assert(STI.isTargetWin64() && "pass only runs on Win64");

  // We don't need to worry about any of the invariants described above if there
  // is no unwind info (CFI).
  if (!MF.hasWinCFI())
    return false;

  // FIXME: Perhaps this pass should also replace SEH_Epilogue by inserting nops
  // before epilogues.

  bool Changed = false;
  for (MachineBasicBlock &MBB : MF) {
    // Look for basic blocks that precede funclet entries or are at the end of
    // the function.
    MachineBasicBlock *NextMBB = MBB.getNextNode();
    if (NextMBB && !NextMBB->isEHFuncletEntry())
      continue;

    // Find the last real instruction in this block.
    auto LastRealInstr = llvm::find_if(reverse(MBB), isCallOrRealInstruction);

    // If the block is empty or the last real instruction is a call instruction,
    // insert an int3. If there is a call instruction, insert the int3 between
    // the call and any labels or other meta instructions. If the block is
    // empty, insert at block end.
    bool IsEmpty = LastRealInstr == MBB.rend();
    bool IsCall = !IsEmpty && isCallInstruction(*LastRealInstr);
    if (IsEmpty || IsCall) {
      LLVM_DEBUG({
        if (IsCall) {
          dbgs() << "inserting int3 after trailing call instruction:\n";
          LastRealInstr->dump();
          dbgs() << '\n';
        } else {
          dbgs() << "inserting int3 in trailing empty MBB:\n";
          MBB.dump();
        }
      });

      MachineBasicBlock::iterator MBBI = MBB.end();
      DebugLoc DL;
      if (IsCall) {
        MBBI = std::next(LastRealInstr.getReverse());
        DL = LastRealInstr->getDebugLoc();
      }
      BuildMI(MBB, MBBI, DL, TII.get(X86::INT3));
      Changed = true;
    }
  }

  return Changed;
}
