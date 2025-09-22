//===- AMDGPUInsertDelayAlu.cpp - Insert s_delay_alu instructions ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Insert s_delay_alu instructions to avoid stalls on GFX11+.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIInstrInfo.h"
#include "llvm/ADT/SetVector.h"

using namespace llvm;

#define DEBUG_TYPE "amdgpu-insert-delay-alu"

namespace {

class AMDGPUInsertDelayAlu : public MachineFunctionPass {
public:
  static char ID;

  const SIInstrInfo *SII;
  const TargetRegisterInfo *TRI;

  TargetSchedModel SchedModel;

  AMDGPUInsertDelayAlu() : MachineFunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  // Return true if MI waits for all outstanding VALU instructions to complete.
  static bool instructionWaitsForVALU(const MachineInstr &MI) {
    // These instruction types wait for VA_VDST==0 before issuing.
    const uint64_t VA_VDST_0 = SIInstrFlags::DS | SIInstrFlags::EXP |
                               SIInstrFlags::FLAT | SIInstrFlags::MIMG |
                               SIInstrFlags::MTBUF | SIInstrFlags::MUBUF;
    if (MI.getDesc().TSFlags & VA_VDST_0)
      return true;
    if (MI.getOpcode() == AMDGPU::S_SENDMSG_RTN_B32 ||
        MI.getOpcode() == AMDGPU::S_SENDMSG_RTN_B64)
      return true;
    if (MI.getOpcode() == AMDGPU::S_WAITCNT_DEPCTR &&
        AMDGPU::DepCtr::decodeFieldVaVdst(MI.getOperand(0).getImm()) == 0)
      return true;
    return false;
  }

  // Types of delay that can be encoded in an s_delay_alu instruction.
  enum DelayType { VALU, TRANS, SALU, OTHER };

  // Get the delay type for an instruction with the specified TSFlags.
  static DelayType getDelayType(uint64_t TSFlags) {
    if (TSFlags & SIInstrFlags::TRANS)
      return TRANS;
    if (TSFlags & SIInstrFlags::VALU)
      return VALU;
    if (TSFlags & SIInstrFlags::SALU)
      return SALU;
    return OTHER;
  }

  // Information about the last instruction(s) that wrote to a particular
  // regunit. In straight-line code there will only be one such instruction, but
  // when control flow converges we merge the delay information from each path
  // to represent the union of the worst-case delays of each type.
  struct DelayInfo {
    // One larger than the maximum number of (non-TRANS) VALU instructions we
    // can encode in an s_delay_alu instruction.
    static constexpr unsigned VALU_MAX = 5;

    // One larger than the maximum number of TRANS instructions we can encode in
    // an s_delay_alu instruction.
    static constexpr unsigned TRANS_MAX = 4;

    // One larger than the maximum number of SALU cycles we can encode in an
    // s_delay_alu instruction.
    static constexpr unsigned SALU_CYCLES_MAX = 4;

    // If it was written by a (non-TRANS) VALU, remember how many clock cycles
    // are left until it completes, and how many other (non-TRANS) VALU we have
    // seen since it was issued.
    uint8_t VALUCycles = 0;
    uint8_t VALUNum = VALU_MAX;

    // If it was written by a TRANS, remember how many clock cycles are left
    // until it completes, and how many other TRANS we have seen since it was
    // issued.
    uint8_t TRANSCycles = 0;
    uint8_t TRANSNum = TRANS_MAX;
    // Also remember how many other (non-TRANS) VALU we have seen since it was
    // issued. When an instruction depends on both a prior TRANS and a prior
    // non-TRANS VALU, this is used to decide whether to encode a wait for just
    // one or both of them.
    uint8_t TRANSNumVALU = VALU_MAX;

    // If it was written by an SALU, remember how many clock cycles are left
    // until it completes.
    uint8_t SALUCycles = 0;

    DelayInfo() = default;

    DelayInfo(DelayType Type, unsigned Cycles) {
      switch (Type) {
      default:
        llvm_unreachable("unexpected type");
      case VALU:
        VALUCycles = Cycles;
        VALUNum = 0;
        break;
      case TRANS:
        TRANSCycles = Cycles;
        TRANSNum = 0;
        TRANSNumVALU = 0;
        break;
      case SALU:
        // Guard against pseudo-instructions like SI_CALL which are marked as
        // SALU but with a very high latency.
        SALUCycles = std::min(Cycles, SALU_CYCLES_MAX);
        break;
      }
    }

    bool operator==(const DelayInfo &RHS) const {
      return VALUCycles == RHS.VALUCycles && VALUNum == RHS.VALUNum &&
             TRANSCycles == RHS.TRANSCycles && TRANSNum == RHS.TRANSNum &&
             TRANSNumVALU == RHS.TRANSNumVALU && SALUCycles == RHS.SALUCycles;
    }

    bool operator!=(const DelayInfo &RHS) const { return !(*this == RHS); }

    // Merge another DelayInfo into this one, to represent the union of the
    // worst-case delays of each type.
    void merge(const DelayInfo &RHS) {
      VALUCycles = std::max(VALUCycles, RHS.VALUCycles);
      VALUNum = std::min(VALUNum, RHS.VALUNum);
      TRANSCycles = std::max(TRANSCycles, RHS.TRANSCycles);
      TRANSNum = std::min(TRANSNum, RHS.TRANSNum);
      TRANSNumVALU = std::min(TRANSNumVALU, RHS.TRANSNumVALU);
      SALUCycles = std::max(SALUCycles, RHS.SALUCycles);
    }

    // Update this DelayInfo after issuing an instruction. IsVALU should be 1
    // when issuing a (non-TRANS) VALU, else 0. IsTRANS should be 1 when issuing
    // a TRANS, else 0. Cycles is the number of cycles it takes to issue the
    // instruction.  Return true if there is no longer any useful delay info.
    bool advance(DelayType Type, unsigned Cycles) {
      bool Erase = true;

      VALUNum += (Type == VALU);
      if (VALUNum >= VALU_MAX || VALUCycles <= Cycles) {
        // Forget about the VALU instruction. It was too far back or has
        // definitely completed by now.
        VALUNum = VALU_MAX;
        VALUCycles = 0;
      } else {
        VALUCycles -= Cycles;
        Erase = false;
      }

      TRANSNum += (Type == TRANS);
      TRANSNumVALU += (Type == VALU);
      if (TRANSNum >= TRANS_MAX || TRANSCycles <= Cycles) {
        // Forget about any TRANS instruction. It was too far back or has
        // definitely completed by now.
        TRANSNum = TRANS_MAX;
        TRANSNumVALU = VALU_MAX;
        TRANSCycles = 0;
      } else {
        TRANSCycles -= Cycles;
        Erase = false;
      }

      if (SALUCycles <= Cycles) {
        // Forget about any SALU instruction. It has definitely completed by
        // now.
        SALUCycles = 0;
      } else {
        SALUCycles -= Cycles;
        Erase = false;
      }

      return Erase;
    }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
    void dump() const {
      if (VALUCycles)
        dbgs() << " VALUCycles=" << (int)VALUCycles;
      if (VALUNum < VALU_MAX)
        dbgs() << " VALUNum=" << (int)VALUNum;
      if (TRANSCycles)
        dbgs() << " TRANSCycles=" << (int)TRANSCycles;
      if (TRANSNum < TRANS_MAX)
        dbgs() << " TRANSNum=" << (int)TRANSNum;
      if (TRANSNumVALU < VALU_MAX)
        dbgs() << " TRANSNumVALU=" << (int)TRANSNumVALU;
      if (SALUCycles)
        dbgs() << " SALUCycles=" << (int)SALUCycles;
    }
#endif
  };

  // A map from regunits to the delay info for that regunit.
  struct DelayState : DenseMap<unsigned, DelayInfo> {
    // Merge another DelayState into this one by merging the delay info for each
    // regunit.
    void merge(const DelayState &RHS) {
      for (const auto &KV : RHS) {
        iterator It;
        bool Inserted;
        std::tie(It, Inserted) = insert(KV);
        if (!Inserted)
          It->second.merge(KV.second);
      }
    }

    // Advance the delay info for each regunit, erasing any that are no longer
    // useful.
    void advance(DelayType Type, unsigned Cycles) {
      iterator Next;
      for (auto I = begin(), E = end(); I != E; I = Next) {
        Next = std::next(I);
        if (I->second.advance(Type, Cycles))
          erase(I);
      }
    }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
    void dump(const TargetRegisterInfo *TRI) const {
      if (empty()) {
        dbgs() << "    empty\n";
        return;
      }

      // Dump DelayInfo for each RegUnit in numerical order.
      SmallVector<const_iterator, 8> Order;
      Order.reserve(size());
      for (const_iterator I = begin(), E = end(); I != E; ++I)
        Order.push_back(I);
      llvm::sort(Order, [](const const_iterator &A, const const_iterator &B) {
        return A->first < B->first;
      });
      for (const_iterator I : Order) {
        dbgs() << "    " << printRegUnit(I->first, TRI);
        I->second.dump();
        dbgs() << "\n";
      }
    }
#endif
  };

  // The saved delay state at the end of each basic block.
  DenseMap<MachineBasicBlock *, DelayState> BlockState;

  // Emit an s_delay_alu instruction if necessary before MI.
  MachineInstr *emitDelayAlu(MachineInstr &MI, DelayInfo Delay,
                             MachineInstr *LastDelayAlu) {
    unsigned Imm = 0;

    // Wait for a TRANS instruction.
    if (Delay.TRANSNum < DelayInfo::TRANS_MAX)
      Imm |= 4 + Delay.TRANSNum;

    // Wait for a VALU instruction (if it's more recent than any TRANS
    // instruction that we're also waiting for).
    if (Delay.VALUNum < DelayInfo::VALU_MAX &&
        Delay.VALUNum <= Delay.TRANSNumVALU) {
      if (Imm & 0xf)
        Imm |= Delay.VALUNum << 7;
      else
        Imm |= Delay.VALUNum;
    }

    // Wait for an SALU instruction.
    if (Delay.SALUCycles) {
      assert(Delay.SALUCycles < DelayInfo::SALU_CYCLES_MAX);
      if (Imm & 0x780) {
        // We have already encoded a VALU and a TRANS delay. There's no room in
        // the encoding for an SALU delay as well, so just drop it.
      } else if (Imm & 0xf) {
        Imm |= (Delay.SALUCycles + 8) << 7;
      } else {
        Imm |= Delay.SALUCycles + 8;
      }
    }

    // Don't emit the s_delay_alu instruction if there's nothing to wait for.
    if (!Imm)
      return LastDelayAlu;

    // If we only need to wait for one instruction, try encoding it in the last
    // s_delay_alu that we emitted.
    if (!(Imm & 0x780) && LastDelayAlu) {
      unsigned Skip = 0;
      for (auto I = MachineBasicBlock::instr_iterator(LastDelayAlu),
                E = MachineBasicBlock::instr_iterator(MI);
           ++I != E;) {
        if (!I->isBundle() && !I->isMetaInstruction())
          ++Skip;
      }
      if (Skip < 6) {
        MachineOperand &Op = LastDelayAlu->getOperand(0);
        unsigned LastImm = Op.getImm();
        assert((LastImm & ~0xf) == 0 &&
               "Remembered an s_delay_alu with no room for another delay!");
        LastImm |= Imm << 7 | Skip << 4;
        Op.setImm(LastImm);
        return nullptr;
      }
    }

    auto &MBB = *MI.getParent();
    MachineInstr *DelayAlu =
        BuildMI(MBB, MI, DebugLoc(), SII->get(AMDGPU::S_DELAY_ALU)).addImm(Imm);
    // Remember the s_delay_alu for next time if there is still room in it to
    // encode another delay.
    return (Imm & 0x780) ? nullptr : DelayAlu;
  }

  bool runOnMachineBasicBlock(MachineBasicBlock &MBB, bool Emit) {
    DelayState State;
    for (auto *Pred : MBB.predecessors())
      State.merge(BlockState[Pred]);

    LLVM_DEBUG(dbgs() << "  State at start of " << printMBBReference(MBB)
                      << "\n";
               State.dump(TRI););

    bool Changed = false;
    MachineInstr *LastDelayAlu = nullptr;

    // Iterate over the contents of bundles, but don't emit any instructions
    // inside a bundle.
    for (auto &MI : MBB.instrs()) {
      if (MI.isBundle() || MI.isMetaInstruction())
        continue;

      // Ignore some more instructions that do not generate any code.
      switch (MI.getOpcode()) {
      case AMDGPU::SI_RETURN_TO_EPILOG:
        continue;
      }

      DelayType Type = getDelayType(MI.getDesc().TSFlags);

      if (instructionWaitsForVALU(MI)) {
        // Forget about all outstanding VALU delays.
        // TODO: This is overkill since it also forgets about SALU delays.
        State = DelayState();
      } else if (Type != OTHER) {
        DelayInfo Delay;
        // TODO: Scan implicit uses too?
        for (const auto &Op : MI.explicit_uses()) {
          if (Op.isReg()) {
            // One of the operands of the writelane is also the output operand.
            // This creates the insertion of redundant delays. Hence, we have to
            // ignore this operand.
            if (MI.getOpcode() == AMDGPU::V_WRITELANE_B32 && Op.isTied())
              continue;
            for (MCRegUnit Unit : TRI->regunits(Op.getReg())) {
              auto It = State.find(Unit);
              if (It != State.end()) {
                Delay.merge(It->second);
                State.erase(Unit);
              }
            }
          }
        }
        if (Emit && !MI.isBundledWithPred()) {
          // TODO: For VALU->SALU delays should we use s_delay_alu or s_nop or
          // just ignore them?
          LastDelayAlu = emitDelayAlu(MI, Delay, LastDelayAlu);
        }
      }

      if (Type != OTHER) {
        // TODO: Scan implicit defs too?
        for (const auto &Op : MI.defs()) {
          unsigned Latency = SchedModel.computeOperandLatency(
              &MI, Op.getOperandNo(), nullptr, 0);
          for (MCRegUnit Unit : TRI->regunits(Op.getReg()))
            State[Unit] = DelayInfo(Type, Latency);
        }
      }

      // Advance by the number of cycles it takes to issue this instruction.
      // TODO: Use a more advanced model that accounts for instructions that
      // take multiple cycles to issue on a particular pipeline.
      unsigned Cycles = SIInstrInfo::getNumWaitStates(MI);
      // TODO: In wave64 mode, double the number of cycles for VALU and VMEM
      // instructions on the assumption that they will usually have to be issued
      // twice?
      State.advance(Type, Cycles);

      LLVM_DEBUG(dbgs() << "  State after " << MI; State.dump(TRI););
    }

    if (Emit) {
      assert(State == BlockState[&MBB] &&
             "Basic block state should not have changed on final pass!");
    } else if (State != BlockState[&MBB]) {
      BlockState[&MBB] = std::move(State);
      Changed = true;
    }
    return Changed;
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    if (skipFunction(MF.getFunction()))
      return false;

    LLVM_DEBUG(dbgs() << "AMDGPUInsertDelayAlu running on " << MF.getName()
                      << "\n");

    const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
    if (!ST.hasDelayAlu())
      return false;

    SII = ST.getInstrInfo();
    TRI = ST.getRegisterInfo();

    SchedModel.init(&ST);

    // Calculate the delay state for each basic block, iterating until we reach
    // a fixed point.
    SetVector<MachineBasicBlock *> WorkList;
    for (auto &MBB : reverse(MF))
      WorkList.insert(&MBB);
    while (!WorkList.empty()) {
      auto &MBB = *WorkList.pop_back_val();
      bool Changed = runOnMachineBasicBlock(MBB, false);
      if (Changed)
        WorkList.insert(MBB.succ_begin(), MBB.succ_end());
    }

    LLVM_DEBUG(dbgs() << "Final pass over all BBs\n");

    // Make one last pass over all basic blocks to emit s_delay_alu
    // instructions.
    bool Changed = false;
    for (auto &MBB : MF)
      Changed |= runOnMachineBasicBlock(MBB, true);
    return Changed;
  }
};

} // namespace

char AMDGPUInsertDelayAlu::ID = 0;

char &llvm::AMDGPUInsertDelayAluID = AMDGPUInsertDelayAlu::ID;

INITIALIZE_PASS(AMDGPUInsertDelayAlu, DEBUG_TYPE, "AMDGPU Insert Delay ALU",
                false, false)
