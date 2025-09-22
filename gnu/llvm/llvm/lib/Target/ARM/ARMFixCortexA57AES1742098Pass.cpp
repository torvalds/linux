//===-- ARMFixCortexA57AES1742098Pass.cpp ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This pass works around a Cortex Core Fused AES erratum:
// - Cortex-A57 Erratum 1742098
// - Cortex-A72 Erratum 1655431
//
// The erratum may be triggered if an input vector register to AESE or AESD was
// last written by an instruction that only updated 32 bits of it. This can
// occur for either of the input registers.
//
// The workaround chosen is to update the input register using `r = VORRq r, r`,
// as this updates all 128 bits of the register unconditionally, but does not
// change the values observed in `r`, making the input safe.
//
// This pass has to be conservative in a few cases:
// - an input vector register to the AES instruction is defined outside the
//   current function, where we have to assume the register was updated in an
//   unsafe way; and
// - an input vector register to the AES instruction is updated along multiple
//   different control-flow paths, where we have to ensure all the register
//   updating instructions are safe.
//
// Both of these cases may apply to a input vector register. In either case, we
// need to ensure that, when the pass is finished, there exists a safe
// instruction between every unsafe register updating instruction and the AES
// instruction.
//
//===----------------------------------------------------------------------===//

#include "ARM.h"
#include "ARMBaseInstrInfo.h"
#include "ARMBaseRegisterInfo.h"
#include "ARMSubtarget.h"
#include "Utils/ARMBaseInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineInstrBundleIterator.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/ReachingDefAnalysis.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <assert.h>
#include <stdint.h>

using namespace llvm;

#define DEBUG_TYPE "arm-fix-cortex-a57-aes-1742098"

//===----------------------------------------------------------------------===//

namespace {
class ARMFixCortexA57AES1742098 : public MachineFunctionPass {
public:
  static char ID;
  explicit ARMFixCortexA57AES1742098() : MachineFunctionPass(ID) {
    initializeARMFixCortexA57AES1742098Pass(*PassRegistry::getPassRegistry());
  }

  bool runOnMachineFunction(MachineFunction &F) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

  StringRef getPassName() const override {
    return "ARM fix for Cortex-A57 AES Erratum 1742098";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<ReachingDefAnalysis>();
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

private:
  // This is the information needed to insert the fixup in the right place.
  struct AESFixupLocation {
    MachineBasicBlock *Block;
    // The fixup instruction will be inserted *before* InsertionPt.
    MachineInstr *InsertionPt;
    MachineOperand *MOp;
  };

  void analyzeMF(MachineFunction &MF, ReachingDefAnalysis &RDA,
                 const ARMBaseRegisterInfo *TRI,
                 SmallVectorImpl<AESFixupLocation> &FixupLocsForFn) const;

  void insertAESFixup(AESFixupLocation &FixupLoc, const ARMBaseInstrInfo *TII,
                      const ARMBaseRegisterInfo *TRI) const;

  static bool isFirstAESPairInstr(MachineInstr &MI);
  static bool isSafeAESInput(MachineInstr &MI);
};
char ARMFixCortexA57AES1742098::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS_BEGIN(ARMFixCortexA57AES1742098, DEBUG_TYPE,
                      "ARM fix for Cortex-A57 AES Erratum 1742098", false,
                      false)
INITIALIZE_PASS_DEPENDENCY(ReachingDefAnalysis);
INITIALIZE_PASS_END(ARMFixCortexA57AES1742098, DEBUG_TYPE,
                    "ARM fix for Cortex-A57 AES Erratum 1742098", false, false)

//===----------------------------------------------------------------------===//

bool ARMFixCortexA57AES1742098::isFirstAESPairInstr(MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  return Opc == ARM::AESD || Opc == ARM::AESE;
}

bool ARMFixCortexA57AES1742098::isSafeAESInput(MachineInstr &MI) {
  auto CondCodeIsAL = [](MachineInstr &MI) -> bool {
    int CCIdx = MI.findFirstPredOperandIdx();
    if (CCIdx == -1)
      return false;
    return MI.getOperand(CCIdx).getImm() == (int64_t)ARMCC::AL;
  };

  switch (MI.getOpcode()) {
  // Unknown: Assume not safe.
  default:
    return false;
  // 128-bit wide AES instructions
  case ARM::AESD:
  case ARM::AESE:
  case ARM::AESMC:
  case ARM::AESIMC:
    // No CondCode.
    return true;
  // 128-bit and 64-bit wide bitwise ops (when condition = al)
  case ARM::VANDd:
  case ARM::VANDq:
  case ARM::VORRd:
  case ARM::VORRq:
  case ARM::VEORd:
  case ARM::VEORq:
  case ARM::VMVNd:
  case ARM::VMVNq:
  // VMOV of 64-bit value between D registers (when condition = al)
  case ARM::VMOVD:
  // VMOV of 64 bit value from GPRs (when condition = al)
  case ARM::VMOVDRR:
  // VMOV of immediate into D or Q registers (when condition = al)
  case ARM::VMOVv2i64:
  case ARM::VMOVv1i64:
  case ARM::VMOVv2f32:
  case ARM::VMOVv4f32:
  case ARM::VMOVv2i32:
  case ARM::VMOVv4i32:
  case ARM::VMOVv4i16:
  case ARM::VMOVv8i16:
  case ARM::VMOVv8i8:
  case ARM::VMOVv16i8:
  // Loads (when condition = al)
  // VLD Dn, [Rn, #imm]
  case ARM::VLDRD:
  // VLDM
  case ARM::VLDMDDB_UPD:
  case ARM::VLDMDIA_UPD:
  case ARM::VLDMDIA:
  // VLDn to all lanes.
  case ARM::VLD1d64:
  case ARM::VLD1q64:
  case ARM::VLD1d32:
  case ARM::VLD1q32:
  case ARM::VLD2b32:
  case ARM::VLD2d32:
  case ARM::VLD2q32:
  case ARM::VLD1d16:
  case ARM::VLD1q16:
  case ARM::VLD2d16:
  case ARM::VLD2q16:
  case ARM::VLD1d8:
  case ARM::VLD1q8:
  case ARM::VLD2b8:
  case ARM::VLD2d8:
  case ARM::VLD2q8:
  case ARM::VLD3d32:
  case ARM::VLD3q32:
  case ARM::VLD3d16:
  case ARM::VLD3q16:
  case ARM::VLD3d8:
  case ARM::VLD3q8:
  case ARM::VLD4d32:
  case ARM::VLD4q32:
  case ARM::VLD4d16:
  case ARM::VLD4q16:
  case ARM::VLD4d8:
  case ARM::VLD4q8:
  // VLD1 (single element to one lane)
  case ARM::VLD1LNd32:
  case ARM::VLD1LNd32_UPD:
  case ARM::VLD1LNd8:
  case ARM::VLD1LNd8_UPD:
  case ARM::VLD1LNd16:
  case ARM::VLD1LNd16_UPD:
  // VLD1 (single element to all lanes)
  case ARM::VLD1DUPd32:
  case ARM::VLD1DUPd32wb_fixed:
  case ARM::VLD1DUPd32wb_register:
  case ARM::VLD1DUPd16:
  case ARM::VLD1DUPd16wb_fixed:
  case ARM::VLD1DUPd16wb_register:
  case ARM::VLD1DUPd8:
  case ARM::VLD1DUPd8wb_fixed:
  case ARM::VLD1DUPd8wb_register:
  case ARM::VLD1DUPq32:
  case ARM::VLD1DUPq32wb_fixed:
  case ARM::VLD1DUPq32wb_register:
  case ARM::VLD1DUPq16:
  case ARM::VLD1DUPq16wb_fixed:
  case ARM::VLD1DUPq16wb_register:
  case ARM::VLD1DUPq8:
  case ARM::VLD1DUPq8wb_fixed:
  case ARM::VLD1DUPq8wb_register:
  // VMOV
  case ARM::VSETLNi32:
  case ARM::VSETLNi16:
  case ARM::VSETLNi8:
    return CondCodeIsAL(MI);
  };

  return false;
}

bool ARMFixCortexA57AES1742098::runOnMachineFunction(MachineFunction &F) {
  LLVM_DEBUG(dbgs() << "***** ARMFixCortexA57AES1742098 *****\n");
  auto &STI = F.getSubtarget<ARMSubtarget>();

  // Fix not requested or AES instructions not present: skip pass.
  if (!STI.hasAES() || !STI.fixCortexA57AES1742098())
    return false;

  const ARMBaseRegisterInfo *TRI = STI.getRegisterInfo();
  const ARMBaseInstrInfo *TII = STI.getInstrInfo();

  auto &RDA = getAnalysis<ReachingDefAnalysis>();

  // Analyze whole function to find instructions which need fixing up...
  SmallVector<AESFixupLocation> FixupLocsForFn{};
  analyzeMF(F, RDA, TRI, FixupLocsForFn);

  // ... and fix the instructions up all at the same time.
  bool Changed = false;
  LLVM_DEBUG(dbgs() << "Inserting " << FixupLocsForFn.size() << " fixup(s)\n");
  for (AESFixupLocation &FixupLoc : FixupLocsForFn) {
    insertAESFixup(FixupLoc, TII, TRI);
    Changed |= true;
  }

  return Changed;
}

void ARMFixCortexA57AES1742098::analyzeMF(
    MachineFunction &MF, ReachingDefAnalysis &RDA,
    const ARMBaseRegisterInfo *TRI,
    SmallVectorImpl<AESFixupLocation> &FixupLocsForFn) const {
  unsigned MaxAllowedFixups = 0;

  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      if (!isFirstAESPairInstr(MI))
        continue;

      // Found an instruction to check the operands of.
      LLVM_DEBUG(dbgs() << "Found AES Pair starting: " << MI);
      assert(MI.getNumExplicitOperands() == 3 && MI.getNumExplicitDefs() == 1 &&
             "Unknown AES Instruction Format. Expected 1 def, 2 uses.");

      // A maximum of two fixups should be inserted for each AES pair (one per
      // register use).
      MaxAllowedFixups += 2;

      // Inspect all operands, choosing whether to insert a fixup.
      for (MachineOperand &MOp : MI.uses()) {
        SmallPtrSet<MachineInstr *, 1> AllDefs{};
        RDA.getGlobalReachingDefs(&MI, MOp.getReg(), AllDefs);

        // Planned Fixup: This should be added to FixupLocsForFn at most once.
        AESFixupLocation NewLoc{&MBB, &MI, &MOp};

        // In small functions with loops, this operand may be both a live-in and
        // have definitions within the function itself. These will need a fixup.
        bool IsLiveIn = MF.front().isLiveIn(MOp.getReg());

        // If the register doesn't have defining instructions, and is not a
        // live-in, then something is wrong and the fixup must always be
        // inserted to be safe.
        if (!IsLiveIn && AllDefs.size() == 0) {
          LLVM_DEBUG(dbgs()
                     << "Fixup Planned: No Defining Instrs found, not live-in: "
                     << printReg(MOp.getReg(), TRI) << "\n");
          FixupLocsForFn.emplace_back(NewLoc);
          continue;
        }

        auto IsUnsafe = [](MachineInstr *MI) -> bool {
          return !isSafeAESInput(*MI);
        };
        size_t UnsafeCount = llvm::count_if(AllDefs, IsUnsafe);

        // If there are no unsafe definitions...
        if (UnsafeCount == 0) {
          // ... and the register is not live-in ...
          if (!IsLiveIn) {
            // ... then skip the fixup.
            LLVM_DEBUG(dbgs() << "No Fixup: Defining instrs are all safe: "
                              << printReg(MOp.getReg(), TRI) << "\n");
            continue;
          }

          // Otherwise, the only unsafe "definition" is a live-in, so insert the
          // fixup at the start of the function.
          LLVM_DEBUG(dbgs()
                     << "Fixup Planned: Live-In (with safe defining instrs): "
                     << printReg(MOp.getReg(), TRI) << "\n");
          NewLoc.Block = &MF.front();
          NewLoc.InsertionPt = &*NewLoc.Block->begin();
          LLVM_DEBUG(dbgs() << "Moving Fixup for Live-In to immediately before "
                            << *NewLoc.InsertionPt);
          FixupLocsForFn.emplace_back(NewLoc);
          continue;
        }

        // If a fixup is needed in more than one place, then the best place to
        // insert it is adjacent to the use rather than introducing a fixup
        // adjacent to each def.
        //
        // FIXME: It might be better to hoist this to the start of the BB, if
        // possible.
        if (IsLiveIn || UnsafeCount > 1) {
          LLVM_DEBUG(dbgs() << "Fixup Planned: Multiple unsafe defining instrs "
                               "(including live-ins): "
                            << printReg(MOp.getReg(), TRI) << "\n");
          FixupLocsForFn.emplace_back(NewLoc);
          continue;
        }

        assert(UnsafeCount == 1 && !IsLiveIn &&
               "At this point, there should be one unsafe defining instrs "
               "and the defined register should not be a live-in.");
        SmallPtrSetIterator<MachineInstr *> It =
            llvm::find_if(AllDefs, IsUnsafe);
        assert(It != AllDefs.end() &&
               "UnsafeCount == 1 but No Unsafe MachineInstr found.");
        MachineInstr *DefMI = *It;

        LLVM_DEBUG(
            dbgs() << "Fixup Planned: Found single unsafe defining instrs for "
                   << printReg(MOp.getReg(), TRI) << ": " << *DefMI);

        // There is one unsafe defining instruction, which needs a fixup. It is
        // generally good to hoist the fixup to be adjacent to the defining
        // instruction rather than the using instruction, as the using
        // instruction may be inside a loop when the defining instruction is
        // not.
        MachineBasicBlock::iterator DefIt = DefMI;
        ++DefIt;
        if (DefIt != DefMI->getParent()->end()) {
          LLVM_DEBUG(dbgs() << "Moving Fixup to immediately after " << *DefMI
                            << "And immediately before " << *DefIt);
          NewLoc.Block = DefIt->getParent();
          NewLoc.InsertionPt = &*DefIt;
        }

        FixupLocsForFn.emplace_back(NewLoc);
      }
    }
  }

  assert(FixupLocsForFn.size() <= MaxAllowedFixups &&
         "Inserted too many fixups for this function.");
  (void)MaxAllowedFixups;
}

void ARMFixCortexA57AES1742098::insertAESFixup(
    AESFixupLocation &FixupLoc, const ARMBaseInstrInfo *TII,
    const ARMBaseRegisterInfo *TRI) const {
  MachineOperand *OperandToFixup = FixupLoc.MOp;

  assert(OperandToFixup->isReg() && "OperandToFixup must be a register");
  Register RegToFixup = OperandToFixup->getReg();

  LLVM_DEBUG(dbgs() << "Inserting VORRq of " << printReg(RegToFixup, TRI)
                    << " before: " << *FixupLoc.InsertionPt);

  // Insert the new `VORRq qN, qN, qN`. There are a few details here:
  //
  // The uses are marked as killed, even if the original use of OperandToFixup
  // is not killed, as the new instruction is clobbering the register. This is
  // safe even if there are other uses of `qN`, as the VORRq value-wise a no-op
  // (it is inserted for microarchitectural reasons).
  //
  // The def and the uses are still marked as Renamable if the original register
  // was, to avoid having to rummage through all the other uses and defs and
  // unset their renamable bits.
  unsigned Renamable = OperandToFixup->isRenamable() ? RegState::Renamable : 0;
  BuildMI(*FixupLoc.Block, FixupLoc.InsertionPt, DebugLoc(),
          TII->get(ARM::VORRq))
      .addReg(RegToFixup, RegState::Define | Renamable)
      .addReg(RegToFixup, RegState::Kill | Renamable)
      .addReg(RegToFixup, RegState::Kill | Renamable)
      .addImm((uint64_t)ARMCC::AL)
      .addReg(ARM::NoRegister);
}

// Factory function used by AArch64TargetMachine to add the pass to
// the passmanager.
FunctionPass *llvm::createARMFixCortexA57AES1742098Pass() {
  return new ARMFixCortexA57AES1742098();
}
