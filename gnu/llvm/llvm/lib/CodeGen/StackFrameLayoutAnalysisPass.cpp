//===-- StackFrameLayoutAnalysisPass.cpp
//------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// StackFrameLayoutAnalysisPass implementation. Outputs information about the
// layout of the stack frame, using the remarks interface. On the CLI it prints
// a textual representation of the stack frame. When possible it prints the
// values that occupy a stack slot using any available debug information. Since
// output is remarks based, it is also available in a machine readable file
// format, such as YAML.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SetVector.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/StackProtector.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/PrintPasses.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

#include <sstream>

using namespace llvm;

#define DEBUG_TYPE "stack-frame-layout"

namespace {

/// StackFrameLayoutAnalysisPass - This is a pass to dump the stack frame of a
/// MachineFunction.
///
struct StackFrameLayoutAnalysisPass : public MachineFunctionPass {
  using SlotDbgMap = SmallDenseMap<int, SetVector<const DILocalVariable *>>;
  static char ID;

  enum SlotType {
    Spill,          // a Spill slot
    Fixed,          // a Fixed slot (e.g. arguments passed on the stack)
    VariableSized,  // a variable sized object
    StackProtector, // Stack Protector slot
    Variable,       // a slot used to store a local data (could be a tmp)
    Invalid         // It's an error for a slot to have this type
  };

  struct SlotData {
    int Slot;
    int Size;
    int Align;
    StackOffset Offset;
    SlotType SlotTy;
    bool Scalable;

    SlotData(const MachineFrameInfo &MFI, const StackOffset Offset,
             const int Idx)
        : Slot(Idx), Size(MFI.getObjectSize(Idx)),
          Align(MFI.getObjectAlign(Idx).value()), Offset(Offset),
          SlotTy(Invalid), Scalable(false) {
      Scalable = MFI.getStackID(Idx) == TargetStackID::ScalableVector;
      if (MFI.isSpillSlotObjectIndex(Idx))
        SlotTy = SlotType::Spill;
      else if (MFI.isFixedObjectIndex(Idx))
        SlotTy = SlotType::Fixed;
      else if (MFI.isVariableSizedObjectIndex(Idx))
        SlotTy = SlotType::VariableSized;
      else if (MFI.hasStackProtectorIndex() &&
               Idx == MFI.getStackProtectorIndex())
        SlotTy = SlotType::StackProtector;
      else
        SlotTy = SlotType::Variable;
    }

    bool isVarSize() const { return SlotTy == SlotType::VariableSized; }

    // We use this to sort in reverse order, so that the layout is displayed
    // correctly. Variable sized slots are sorted to the end of the list, as
    // offsets are currently incorrect for these but they reside at the end of
    // the stack frame. The Slot index is used to ensure deterministic order
    // when offsets are equal.
    bool operator<(const SlotData &Rhs) const {
      return std::make_tuple(!isVarSize(),
                             Offset.getFixed() + Offset.getScalable(), Slot) >
             std::make_tuple(!Rhs.isVarSize(),
                             Rhs.Offset.getFixed() + Rhs.Offset.getScalable(),
                             Rhs.Slot);
    }
  };

  StackFrameLayoutAnalysisPass() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "Stack Frame Layout Analysis";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
    AU.addRequired<MachineOptimizationRemarkEmitterPass>();
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    // TODO: We should implement a similar filter for remarks:
    //   -Rpass-func-filter=<regex>
    if (!isFunctionInPrintList(MF.getName()))
      return false;

    LLVMContext &Ctx = MF.getFunction().getContext();
    if (!Ctx.getDiagHandlerPtr()->isAnalysisRemarkEnabled(DEBUG_TYPE))
      return false;

    MachineOptimizationRemarkAnalysis Rem(DEBUG_TYPE, "StackLayout",
                                          MF.getFunction().getSubprogram(),
                                          &MF.front());
    Rem << ("\nFunction: " + MF.getName()).str();
    emitStackFrameLayoutRemarks(MF, Rem);
    getAnalysis<MachineOptimizationRemarkEmitterPass>().getORE().emit(Rem);
    return false;
  }

  std::string getTypeString(SlotType Ty) {
    switch (Ty) {
    case SlotType::Spill:
      return "Spill";
    case SlotType::Fixed:
      return "Fixed";
    case SlotType::VariableSized:
      return "VariableSized";
    case SlotType::StackProtector:
      return "Protector";
    case SlotType::Variable:
      return "Variable";
    default:
      llvm_unreachable("bad slot type for stack layout");
    }
  }

  void emitStackSlotRemark(const MachineFunction &MF, const SlotData &D,
                           MachineOptimizationRemarkAnalysis &Rem) {
    // To make it easy to understand the stack layout from the CLI, we want to
    // print each slot like the following:
    //
    //   Offset: [SP+8], Type: Spill, Align: 8, Size: 16
    //       foo @ /path/to/file.c:25
    //       bar @ /path/to/file.c:35
    //
    // Which prints the size, alignment, and offset from the SP at function
    // entry.
    //
    // But we also want the machine readable remarks data to be nicely
    // organized. So we print some additional data as strings for the CLI
    // output, but maintain more structured data for the YAML.
    //
    // For example we store the Offset in YAML as:
    //    ...
    //    - Offset: -8
    //    - ScalableOffset: -16
    // Note: the ScalableOffset entries are added only for slots with non-zero
    // scalable offsets.
    //
    // But we print it to the CLI as:
    //   Offset: [SP-8]
    //
    // Or with non-zero scalable offset:
    //   Offset: [SP-8-16 x vscale]

    // Negative offsets will print a leading `-`, so only add `+`
    std::string Prefix =
        formatv("\nOffset: [SP{0}", (D.Offset.getFixed() < 0) ? "" : "+").str();
    Rem << Prefix << ore::NV("Offset", D.Offset.getFixed());

    if (D.Offset.getScalable()) {
      Rem << ((D.Offset.getScalable() < 0) ? "" : "+")
          << ore::NV("ScalableOffset", D.Offset.getScalable()) << " x vscale";
    }

    Rem << "], Type: " << ore::NV("Type", getTypeString(D.SlotTy))
        << ", Align: " << ore::NV("Align", D.Align)
        << ", Size: " << ore::NV("Size", ElementCount::get(D.Size, D.Scalable));
  }

  void emitSourceLocRemark(const MachineFunction &MF, const DILocalVariable *N,
                           MachineOptimizationRemarkAnalysis &Rem) {
    std::string Loc =
        formatv("{0} @ {1}:{2}", N->getName(), N->getFilename(), N->getLine())
            .str();
    Rem << "\n    " << ore::NV("DataLoc", Loc);
  }

  StackOffset getStackOffset(const MachineFunction &MF,
                             const MachineFrameInfo &MFI,
                             const TargetFrameLowering *FI, int FrameIdx) {
    if (!FI)
      return StackOffset::getFixed(MFI.getObjectOffset(FrameIdx));

    return FI->getFrameIndexReferenceFromSP(MF, FrameIdx);
  }

  void emitStackFrameLayoutRemarks(MachineFunction &MF,
                                   MachineOptimizationRemarkAnalysis &Rem) {
    const MachineFrameInfo &MFI = MF.getFrameInfo();
    if (!MFI.hasStackObjects())
      return;

    const TargetFrameLowering *FI = MF.getSubtarget().getFrameLowering();

    LLVM_DEBUG(dbgs() << "getStackProtectorIndex =="
                      << MFI.getStackProtectorIndex() << "\n");

    std::vector<SlotData> SlotInfo;

    const unsigned int NumObj = MFI.getNumObjects();
    SlotInfo.reserve(NumObj);
    // initialize slot info
    for (int Idx = MFI.getObjectIndexBegin(), EndIdx = MFI.getObjectIndexEnd();
         Idx != EndIdx; ++Idx) {
      if (MFI.isDeadObjectIndex(Idx))
        continue;
      SlotInfo.emplace_back(MFI, getStackOffset(MF, MFI, FI, Idx), Idx);
    }

    // sort the ordering, to match the actual layout in memory
    llvm::sort(SlotInfo);

    SlotDbgMap SlotMap = genSlotDbgMapping(MF);

    for (const SlotData &Info : SlotInfo) {
      emitStackSlotRemark(MF, Info, Rem);
      for (const DILocalVariable *N : SlotMap[Info.Slot])
        emitSourceLocRemark(MF, N, Rem);
    }
  }

  // We need to generate a mapping of slots to the values that are stored to
  // them. This information is lost by the time we need to print out the frame,
  // so we reconstruct it here by walking the CFG, and generating the mapping.
  SlotDbgMap genSlotDbgMapping(MachineFunction &MF) {
    SlotDbgMap SlotDebugMap;

    // add variables to the map
    for (MachineFunction::VariableDbgInfo &DI :
         MF.getInStackSlotVariableDbgInfo())
      SlotDebugMap[DI.getStackSlot()].insert(DI.Var);

    // Then add all the spills that have debug data
    for (MachineBasicBlock &MBB : MF) {
      for (MachineInstr &MI : MBB) {
        for (MachineMemOperand *MO : MI.memoperands()) {
          if (!MO->isStore())
            continue;
          auto *FI = dyn_cast_or_null<FixedStackPseudoSourceValue>(
              MO->getPseudoValue());
          if (!FI)
            continue;
          int FrameIdx = FI->getFrameIndex();
          SmallVector<MachineInstr *> Dbg;
          MI.collectDebugValues(Dbg);

          for (MachineInstr *MI : Dbg)
            SlotDebugMap[FrameIdx].insert(MI->getDebugVariable());
        }
      }
    }

    return SlotDebugMap;
  }
};

char StackFrameLayoutAnalysisPass::ID = 0;
} // namespace

char &llvm::StackFrameLayoutAnalysisPassID = StackFrameLayoutAnalysisPass::ID;
INITIALIZE_PASS(StackFrameLayoutAnalysisPass, "stack-frame-layout",
                "Stack Frame Layout", false, false)

namespace llvm {
/// Returns a newly-created StackFrameLayout pass.
MachineFunctionPass *createStackFrameLayoutAnalysisPass() {
  return new StackFrameLayoutAnalysisPass();
}

} // namespace llvm
