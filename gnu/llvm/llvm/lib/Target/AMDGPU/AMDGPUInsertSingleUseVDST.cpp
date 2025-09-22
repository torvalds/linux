//===- AMDGPUInsertSingleUseVDST.cpp - Insert s_singleuse_vdst instructions ==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Insert s_singleuse_vdst instructions on GFX11.5+ to mark regions of VALU
/// instructions that produce single-use VGPR values. If the value is forwarded
/// to the consumer instruction prior to VGPR writeback, the hardware can
/// then skip (kill) the VGPR write.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPUGenSearchableTables.inc"
#include "GCNSubtarget.h"
#include "SIInstrInfo.h"
#include "SIRegisterInfo.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Pass.h"
#include <array>

using namespace llvm;

#define DEBUG_TYPE "amdgpu-insert-single-use-vdst"

namespace {
class AMDGPUInsertSingleUseVDST : public MachineFunctionPass {
private:
  const SIInstrInfo *SII;
  class SingleUseInstruction {
  private:
    static const unsigned MaxSkipRange = 0b111;
    static const unsigned MaxNumberOfSkipRegions = 2;

    unsigned LastEncodedPositionEnd;
    MachineInstr *ProducerInstr;

    std::array<unsigned, MaxNumberOfSkipRegions + 1> SingleUseRegions;
    SmallVector<unsigned, MaxNumberOfSkipRegions> SkipRegions;

    // Adds a skip region into the instruction.
    void skip(const unsigned ProducerPosition) {
      while (LastEncodedPositionEnd + MaxSkipRange < ProducerPosition) {
        SkipRegions.push_back(MaxSkipRange);
        LastEncodedPositionEnd += MaxSkipRange;
      }
      SkipRegions.push_back(ProducerPosition - LastEncodedPositionEnd);
      LastEncodedPositionEnd = ProducerPosition;
    }

    bool currentRegionHasSpace() {
      const auto Region = SkipRegions.size();
      // The first region has an extra bit of encoding space.
      return SingleUseRegions[Region] <
             ((Region == MaxNumberOfSkipRegions) ? 0b1111U : 0b111U);
    }

    unsigned encodeImm() {
      // Handle the first Single Use Region separately as it has an extra bit
      // of encoding space.
      unsigned Imm = SingleUseRegions[SkipRegions.size()];
      unsigned ShiftAmount = 4;
      for (unsigned i = SkipRegions.size(); i > 0; i--) {
        Imm |= SkipRegions[i - 1] << ShiftAmount;
        ShiftAmount += 3;
        Imm |= SingleUseRegions[i - 1] << ShiftAmount;
        ShiftAmount += 3;
      }
      return Imm;
    }

  public:
    SingleUseInstruction(const unsigned ProducerPosition,
                         MachineInstr *Producer)
        : LastEncodedPositionEnd(ProducerPosition + 1), ProducerInstr(Producer),
          SingleUseRegions({1, 0, 0}) {}

    // Returns false if adding a new single use producer failed. This happens
    // because it could not be encoded, either because there is no room to
    // encode another single use producer region or that this single use
    // producer is too far away to encode the amount of instructions to skip.
    bool tryAddProducer(const unsigned ProducerPosition, MachineInstr *MI) {
      // Producer is too far away to encode into this instruction or another
      // skip region is needed and SkipRegions.size() = 2 so there's no room for
      // another skip region, therefore a new instruction is needed.
      if (LastEncodedPositionEnd +
              (MaxSkipRange * (MaxNumberOfSkipRegions - SkipRegions.size())) <
          ProducerPosition)
        return false;

      // If a skip region is needed.
      if (LastEncodedPositionEnd != ProducerPosition ||
          !currentRegionHasSpace()) {
        // If the current region is out of space therefore a skip region would
        // be needed, but there is no room for another skip region.
        if (SkipRegions.size() == MaxNumberOfSkipRegions)
          return false;
        skip(ProducerPosition);
      }

      SingleUseRegions[SkipRegions.size()]++;
      LastEncodedPositionEnd = ProducerPosition + 1;
      ProducerInstr = MI;
      return true;
    }

    auto emit(const SIInstrInfo *SII) {
      return BuildMI(*ProducerInstr->getParent(), ProducerInstr, DebugLoc(),
                     SII->get(AMDGPU::S_SINGLEUSE_VDST))
          .addImm(encodeImm());
    }
  };

public:
  static char ID;

  AMDGPUInsertSingleUseVDST() : MachineFunctionPass(ID) {}

  void insertSingleUseInstructions(
      ArrayRef<std::pair<unsigned, MachineInstr *>> SingleUseProducers) const {
    SmallVector<SingleUseInstruction> Instructions;

    for (auto &[Position, MI] : SingleUseProducers) {
      // Encode this position into the last single use instruction if possible.
      if (Instructions.empty() ||
          !Instructions.back().tryAddProducer(Position, MI)) {
        // If not, add a new instruction.
        Instructions.push_back(SingleUseInstruction(Position, MI));
      }
    }

    for (auto &Instruction : Instructions)
      Instruction.emit(SII);
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    const auto &ST = MF.getSubtarget<GCNSubtarget>();
    if (!ST.hasVGPRSingleUseHintInsts())
      return false;

    SII = ST.getInstrInfo();
    const auto *TRI = &SII->getRegisterInfo();
    bool InstructionEmitted = false;

    for (MachineBasicBlock &MBB : MF) {
      DenseMap<MCRegUnit, unsigned> RegisterUseCount;

      // Handle boundaries at the end of basic block separately to avoid
      // false positives. If they are live at the end of a basic block then
      // assume it has more uses later on.
      for (const auto &Liveout : MBB.liveouts()) {
        for (MCRegUnitMaskIterator Units(Liveout.PhysReg, TRI); Units.isValid();
             ++Units) {
          const auto [Unit, Mask] = *Units;
          if ((Mask & Liveout.LaneMask).any())
            RegisterUseCount[Unit] = 2;
        }
      }

      SmallVector<std::pair<unsigned, MachineInstr *>>
          SingleUseProducerPositions;

      unsigned VALUInstrCount = 0;
      for (MachineInstr &MI : reverse(MBB.instrs())) {
        // All registers in all operands need to be single use for an
        // instruction to be marked as a single use producer.
        bool AllProducerOperandsAreSingleUse = true;

        // Gather a list of Registers used before updating use counts to avoid
        // double counting registers that appear multiple times in a single
        // MachineInstr.
        SmallVector<MCRegUnit> RegistersUsed;

        for (const auto &Operand : MI.all_defs()) {
          const auto Reg = Operand.getReg();

          const auto RegUnits = TRI->regunits(Reg);
          if (any_of(RegUnits, [&RegisterUseCount](const MCRegUnit Unit) {
                return RegisterUseCount[Unit] > 1;
              }))
            AllProducerOperandsAreSingleUse = false;

          // Reset uses count when a register is no longer live.
          for (const MCRegUnit Unit : RegUnits)
            RegisterUseCount.erase(Unit);
        }

        for (const auto &Operand : MI.all_uses()) {
          const auto Reg = Operand.getReg();

          // Count the number of times each register is read.
          for (const MCRegUnit Unit : TRI->regunits(Reg)) {
            if (!is_contained(RegistersUsed, Unit))
              RegistersUsed.push_back(Unit);
          }
        }
        for (const MCRegUnit Unit : RegistersUsed)
          RegisterUseCount[Unit]++;

        // Do not attempt to optimise across exec mask changes.
        if (MI.modifiesRegister(AMDGPU::EXEC, TRI) ||
            AMDGPU::isInvalidSingleUseConsumerInst(MI.getOpcode())) {
          for (auto &UsedReg : RegisterUseCount)
            UsedReg.second = 2;
        }

        if (!SIInstrInfo::isVALU(MI) ||
            AMDGPU::isInvalidSingleUseProducerInst(MI.getOpcode()))
          continue;
        if (AllProducerOperandsAreSingleUse) {
          SingleUseProducerPositions.push_back({VALUInstrCount, &MI});
          InstructionEmitted = true;
        }
        VALUInstrCount++;
      }
      insertSingleUseInstructions(SingleUseProducerPositions);
    }
    return InstructionEmitted;
  }
};
} // namespace

char AMDGPUInsertSingleUseVDST::ID = 0;

char &llvm::AMDGPUInsertSingleUseVDSTID = AMDGPUInsertSingleUseVDST::ID;

INITIALIZE_PASS(AMDGPUInsertSingleUseVDST, DEBUG_TYPE,
                "AMDGPU Insert SingleUseVDST", false, false)
