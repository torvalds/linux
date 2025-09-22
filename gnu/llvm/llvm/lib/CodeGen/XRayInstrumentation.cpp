//===- XRayInstrumentation.cpp - Adds XRay instrumentation to functions. --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a MachineFunctionPass that inserts the appropriate
// XRay instrumentation instructions. We look for XRay-specific attributes
// on the function to determine whether we should insert the replacement
// operations.
//
//===---------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

namespace {

struct InstrumentationOptions {
  // Whether to emit PATCHABLE_TAIL_CALL.
  bool HandleTailcall;

  // Whether to emit PATCHABLE_RET/PATCHABLE_FUNCTION_EXIT for all forms of
  // return, e.g. conditional return.
  bool HandleAllReturns;
};

struct XRayInstrumentation : public MachineFunctionPass {
  static char ID;

  XRayInstrumentation() : MachineFunctionPass(ID) {
    initializeXRayInstrumentationPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addPreserved<MachineLoopInfoWrapperPass>();
    AU.addPreserved<MachineDominatorTreeWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

private:
  // Replace the original RET instruction with the exit sled code ("patchable
  //   ret" pseudo-instruction), so that at runtime XRay can replace the sled
  //   with a code jumping to XRay trampoline, which calls the tracing handler
  //   and, in the end, issues the RET instruction.
  // This is the approach to go on CPUs which have a single RET instruction,
  //   like x86/x86_64.
  void replaceRetWithPatchableRet(MachineFunction &MF,
                                  const TargetInstrInfo *TII,
                                  InstrumentationOptions);

  // Prepend the original return instruction with the exit sled code ("patchable
  //   function exit" pseudo-instruction), preserving the original return
  //   instruction just after the exit sled code.
  // This is the approach to go on CPUs which have multiple options for the
  //   return instruction, like ARM. For such CPUs we can't just jump into the
  //   XRay trampoline and issue a single return instruction there. We rather
  //   have to call the trampoline and return from it to the original return
  //   instruction of the function being instrumented.
  void prependRetWithPatchableExit(MachineFunction &MF,
                                   const TargetInstrInfo *TII,
                                   InstrumentationOptions);
};

} // end anonymous namespace

void XRayInstrumentation::replaceRetWithPatchableRet(
    MachineFunction &MF, const TargetInstrInfo *TII,
    InstrumentationOptions op) {
  // We look for *all* terminators and returns, then replace those with
  // PATCHABLE_RET instructions.
  SmallVector<MachineInstr *, 4> Terminators;
  for (auto &MBB : MF) {
    for (auto &T : MBB.terminators()) {
      unsigned Opc = 0;
      if (T.isReturn() &&
          (op.HandleAllReturns || T.getOpcode() == TII->getReturnOpcode())) {
        // Replace return instructions with:
        //   PATCHABLE_RET <Opcode>, <Operand>...
        Opc = TargetOpcode::PATCHABLE_RET;
      }
      if (TII->isTailCall(T) && op.HandleTailcall) {
        // Treat the tail call as a return instruction, which has a
        // different-looking sled than the normal return case.
        Opc = TargetOpcode::PATCHABLE_TAIL_CALL;
      }
      if (Opc != 0) {
        auto MIB = BuildMI(MBB, T, T.getDebugLoc(), TII->get(Opc))
                       .addImm(T.getOpcode());
        for (auto &MO : T.operands())
          MIB.add(MO);
        Terminators.push_back(&T);
        if (T.shouldUpdateCallSiteInfo())
          MF.eraseCallSiteInfo(&T);
      }
    }
  }

  for (auto &I : Terminators)
    I->eraseFromParent();
}

void XRayInstrumentation::prependRetWithPatchableExit(
    MachineFunction &MF, const TargetInstrInfo *TII,
    InstrumentationOptions op) {
  for (auto &MBB : MF)
    for (auto &T : MBB.terminators()) {
      unsigned Opc = 0;
      if (T.isReturn() &&
          (op.HandleAllReturns || T.getOpcode() == TII->getReturnOpcode())) {
        Opc = TargetOpcode::PATCHABLE_FUNCTION_EXIT;
      }
      if (TII->isTailCall(T) && op.HandleTailcall) {
        Opc = TargetOpcode::PATCHABLE_TAIL_CALL;
      }
      if (Opc != 0) {
        // Prepend the return instruction with PATCHABLE_FUNCTION_EXIT or
        //   PATCHABLE_TAIL_CALL .
        BuildMI(MBB, T, T.getDebugLoc(), TII->get(Opc));
      }
    }
}

bool XRayInstrumentation::runOnMachineFunction(MachineFunction &MF) {
  auto &F = MF.getFunction();
  auto InstrAttr = F.getFnAttribute("function-instrument");
  bool AlwaysInstrument = InstrAttr.isStringAttribute() &&
                          InstrAttr.getValueAsString() == "xray-always";
  bool NeverInstrument = InstrAttr.isStringAttribute() &&
                         InstrAttr.getValueAsString() == "xray-never";
  if (NeverInstrument && !AlwaysInstrument)
    return false;
  auto IgnoreLoopsAttr = F.getFnAttribute("xray-ignore-loops");

  uint64_t XRayThreshold = 0;
  if (!AlwaysInstrument) {
    bool IgnoreLoops = IgnoreLoopsAttr.isValid();
    XRayThreshold = F.getFnAttributeAsParsedInteger(
        "xray-instruction-threshold", std::numeric_limits<uint64_t>::max());
    if (XRayThreshold == std::numeric_limits<uint64_t>::max())
      return false;

    // Count the number of MachineInstr`s in MachineFunction
    uint64_t MICount = 0;
    for (const auto &MBB : MF)
      MICount += MBB.size();

    bool TooFewInstrs = MICount < XRayThreshold;

    if (!IgnoreLoops) {
      // Get MachineDominatorTree or compute it on the fly if it's unavailable
      auto *MDTWrapper =
          getAnalysisIfAvailable<MachineDominatorTreeWrapperPass>();
      auto *MDT = MDTWrapper ? &MDTWrapper->getDomTree() : nullptr;
      MachineDominatorTree ComputedMDT;
      if (!MDT) {
        ComputedMDT.getBase().recalculate(MF);
        MDT = &ComputedMDT;
      }

      // Get MachineLoopInfo or compute it on the fly if it's unavailable
      auto *MLIWrapper = getAnalysisIfAvailable<MachineLoopInfoWrapperPass>();
      auto *MLI = MLIWrapper ? &MLIWrapper->getLI() : nullptr;
      MachineLoopInfo ComputedMLI;
      if (!MLI) {
        ComputedMLI.analyze(MDT->getBase());
        MLI = &ComputedMLI;
      }

      // Check if we have a loop.
      // FIXME: Maybe make this smarter, and see whether the loops are dependent
      // on inputs or side-effects?
      if (MLI->empty() && TooFewInstrs)
        return false; // Function is too small and has no loops.
    } else if (TooFewInstrs) {
      // Function is too small
      return false;
    }
  }

  // We look for the first non-empty MachineBasicBlock, so that we can insert
  // the function instrumentation in the appropriate place.
  auto MBI = llvm::find_if(
      MF, [&](const MachineBasicBlock &MBB) { return !MBB.empty(); });
  if (MBI == MF.end())
    return false; // The function is empty.

  auto *TII = MF.getSubtarget().getInstrInfo();
  auto &FirstMBB = *MBI;
  auto &FirstMI = *FirstMBB.begin();

  if (!MF.getSubtarget().isXRaySupported()) {
    FirstMI.emitError("An attempt to perform XRay instrumentation for an"
                      " unsupported target.");
    return false;
  }

  if (!F.hasFnAttribute("xray-skip-entry")) {
    // First, insert an PATCHABLE_FUNCTION_ENTER as the first instruction of the
    // MachineFunction.
    BuildMI(FirstMBB, FirstMI, FirstMI.getDebugLoc(),
            TII->get(TargetOpcode::PATCHABLE_FUNCTION_ENTER));
  }

  if (!F.hasFnAttribute("xray-skip-exit")) {
    switch (MF.getTarget().getTargetTriple().getArch()) {
    case Triple::ArchType::arm:
    case Triple::ArchType::thumb:
    case Triple::ArchType::aarch64:
    case Triple::ArchType::hexagon:
    case Triple::ArchType::loongarch64:
    case Triple::ArchType::mips:
    case Triple::ArchType::mipsel:
    case Triple::ArchType::mips64:
    case Triple::ArchType::mips64el: {
      // For the architectures which don't have a single return instruction
      InstrumentationOptions op;
      op.HandleTailcall = false;
      op.HandleAllReturns = true;
      prependRetWithPatchableExit(MF, TII, op);
      break;
    }
    case Triple::ArchType::ppc64le: {
      // PPC has conditional returns. Turn them into branch and plain returns.
      InstrumentationOptions op;
      op.HandleTailcall = false;
      op.HandleAllReturns = true;
      replaceRetWithPatchableRet(MF, TII, op);
      break;
    }
    default: {
      // For the architectures that have a single return instruction (such as
      //   RETQ on x86_64).
      InstrumentationOptions op;
      op.HandleTailcall = true;
      op.HandleAllReturns = false;
      replaceRetWithPatchableRet(MF, TII, op);
      break;
    }
    }
  }
  return true;
}

char XRayInstrumentation::ID = 0;
char &llvm::XRayInstrumentationID = XRayInstrumentation::ID;
INITIALIZE_PASS_BEGIN(XRayInstrumentation, "xray-instrumentation",
                      "Insert XRay ops", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_END(XRayInstrumentation, "xray-instrumentation",
                    "Insert XRay ops", false, false)
