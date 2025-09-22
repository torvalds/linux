//===--------- PPCPreEmitPeephole.cpp - Late peephole optimizations -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A pre-emit peephole for catching opportunities introduced by late passes such
// as MachineBlockPlacement.
//
//===----------------------------------------------------------------------===//

#include "PPC.h"
#include "PPCInstrInfo.h"
#include "PPCSubtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/MC/MCContext.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "ppc-pre-emit-peephole"

STATISTIC(NumRRConvertedInPreEmit,
          "Number of r+r instructions converted to r+i in pre-emit peephole");
STATISTIC(NumRemovedInPreEmit,
          "Number of instructions deleted in pre-emit peephole");
STATISTIC(NumberOfSelfCopies,
          "Number of self copy instructions eliminated");
STATISTIC(NumFrameOffFoldInPreEmit,
          "Number of folding frame offset by using r+r in pre-emit peephole");
STATISTIC(NumCmpsInPreEmit,
          "Number of compares eliminated in pre-emit peephole");

static cl::opt<bool>
EnablePCRelLinkerOpt("ppc-pcrel-linker-opt", cl::Hidden, cl::init(true),
                     cl::desc("enable PC Relative linker optimization"));

static cl::opt<bool>
RunPreEmitPeephole("ppc-late-peephole", cl::Hidden, cl::init(true),
                   cl::desc("Run pre-emit peephole optimizations."));

static cl::opt<uint64_t>
DSCRValue("ppc-set-dscr", cl::Hidden,
          cl::desc("Set the Data Stream Control Register."));

namespace {

static bool hasPCRelativeForm(MachineInstr &Use) {
  switch (Use.getOpcode()) {
  default:
    return false;
  case PPC::LBZ:
  case PPC::LBZ8:
  case PPC::LHA:
  case PPC::LHA8:
  case PPC::LHZ:
  case PPC::LHZ8:
  case PPC::LWZ:
  case PPC::LWZ8:
  case PPC::STB:
  case PPC::STB8:
  case PPC::STH:
  case PPC::STH8:
  case PPC::STW:
  case PPC::STW8:
  case PPC::LD:
  case PPC::STD:
  case PPC::LWA:
  case PPC::LXSD:
  case PPC::LXSSP:
  case PPC::LXV:
  case PPC::STXSD:
  case PPC::STXSSP:
  case PPC::STXV:
  case PPC::LFD:
  case PPC::LFS:
  case PPC::STFD:
  case PPC::STFS:
  case PPC::DFLOADf32:
  case PPC::DFLOADf64:
  case PPC::DFSTOREf32:
  case PPC::DFSTOREf64:
    return true;
  }
}

  class PPCPreEmitPeephole : public MachineFunctionPass {
  public:
    static char ID;
    PPCPreEmitPeephole() : MachineFunctionPass(ID) {
      initializePPCPreEmitPeepholePass(*PassRegistry::getPassRegistry());
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      MachineFunctionPass::getAnalysisUsage(AU);
    }

    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }

    // This function removes any redundant load immediates. It has two level
    // loops - The outer loop finds the load immediates BBI that could be used
    // to replace following redundancy. The inner loop scans instructions that
    // after BBI to find redundancy and update kill/dead flags accordingly. If
    // AfterBBI is the same as BBI, it is redundant, otherwise any instructions
    // that modify the def register of BBI would break the scanning.
    // DeadOrKillToUnset is a pointer to the previous operand that had the
    // kill/dead flag set. It keeps track of the def register of BBI, the use
    // registers of AfterBBIs and the def registers of AfterBBIs.
    bool removeRedundantLIs(MachineBasicBlock &MBB,
                            const TargetRegisterInfo *TRI) {
      LLVM_DEBUG(dbgs() << "Remove redundant load immediates from MBB:\n";
                 MBB.dump(); dbgs() << "\n");

      DenseSet<MachineInstr *> InstrsToErase;
      for (auto BBI = MBB.instr_begin(); BBI != MBB.instr_end(); ++BBI) {
        // Skip load immediate that is marked to be erased later because it
        // cannot be used to replace any other instructions.
        if (InstrsToErase.contains(&*BBI))
          continue;
        // Skip non-load immediate.
        unsigned Opc = BBI->getOpcode();
        if (Opc != PPC::LI && Opc != PPC::LI8 && Opc != PPC::LIS &&
            Opc != PPC::LIS8)
          continue;
        // Skip load immediate, where the operand is a relocation (e.g., $r3 =
        // LI target-flags(ppc-lo) %const.0).
        if (!BBI->getOperand(1).isImm())
          continue;
        assert(BBI->getOperand(0).isReg() &&
               "Expected a register for the first operand");

        LLVM_DEBUG(dbgs() << "Scanning after load immediate: "; BBI->dump(););

        Register Reg = BBI->getOperand(0).getReg();
        int64_t Imm = BBI->getOperand(1).getImm();
        MachineOperand *DeadOrKillToUnset = nullptr;
        if (BBI->getOperand(0).isDead()) {
          DeadOrKillToUnset = &BBI->getOperand(0);
          LLVM_DEBUG(dbgs() << " Kill flag of " << *DeadOrKillToUnset
                            << " from load immediate " << *BBI
                            << " is a unsetting candidate\n");
        }
        // This loop scans instructions after BBI to see if there is any
        // redundant load immediate.
        for (auto AfterBBI = std::next(BBI); AfterBBI != MBB.instr_end();
             ++AfterBBI) {
          // Track the operand that kill Reg. We would unset the kill flag of
          // the operand if there is a following redundant load immediate.
          int KillIdx = AfterBBI->findRegisterUseOperandIdx(Reg, TRI, true);

          // We can't just clear implicit kills, so if we encounter one, stop
          // looking further.
          if (KillIdx != -1 && AfterBBI->getOperand(KillIdx).isImplicit()) {
            LLVM_DEBUG(dbgs()
                       << "Encountered an implicit kill, cannot proceed: ");
            LLVM_DEBUG(AfterBBI->dump());
            break;
          }

          if (KillIdx != -1) {
            assert(!DeadOrKillToUnset && "Shouldn't kill same register twice");
            DeadOrKillToUnset = &AfterBBI->getOperand(KillIdx);
            LLVM_DEBUG(dbgs()
                       << " Kill flag of " << *DeadOrKillToUnset << " from "
                       << *AfterBBI << " is a unsetting candidate\n");
          }

          if (!AfterBBI->modifiesRegister(Reg, TRI))
            continue;
          // Finish scanning because Reg is overwritten by a non-load
          // instruction.
          if (AfterBBI->getOpcode() != Opc)
            break;
          assert(AfterBBI->getOperand(0).isReg() &&
                 "Expected a register for the first operand");
          // Finish scanning because Reg is overwritten by a relocation or a
          // different value.
          if (!AfterBBI->getOperand(1).isImm() ||
              AfterBBI->getOperand(1).getImm() != Imm)
            break;

          // It loads same immediate value to the same Reg, which is redundant.
          // We would unset kill flag in previous Reg usage to extend live range
          // of Reg first, then remove the redundancy.
          if (DeadOrKillToUnset) {
            LLVM_DEBUG(dbgs()
                       << " Unset dead/kill flag of " << *DeadOrKillToUnset
                       << " from " << *DeadOrKillToUnset->getParent());
            if (DeadOrKillToUnset->isDef())
              DeadOrKillToUnset->setIsDead(false);
            else
              DeadOrKillToUnset->setIsKill(false);
          }
          DeadOrKillToUnset =
              AfterBBI->findRegisterDefOperand(Reg, TRI, true, true);
          if (DeadOrKillToUnset)
            LLVM_DEBUG(dbgs()
                       << " Dead flag of " << *DeadOrKillToUnset << " from "
                       << *AfterBBI << " is a unsetting candidate\n");
          InstrsToErase.insert(&*AfterBBI);
          LLVM_DEBUG(dbgs() << " Remove redundant load immediate: ";
                     AfterBBI->dump());
        }
      }

      for (MachineInstr *MI : InstrsToErase) {
        MI->eraseFromParent();
      }
      NumRemovedInPreEmit += InstrsToErase.size();
      return !InstrsToErase.empty();
    }

    // Check if this instruction is a PLDpc that is part of a GOT indirect
    // access.
    bool isGOTPLDpc(MachineInstr &Instr) {
      if (Instr.getOpcode() != PPC::PLDpc)
        return false;

      // The result must be a register.
      const MachineOperand &LoadedAddressReg = Instr.getOperand(0);
      if (!LoadedAddressReg.isReg())
        return false;

      // Make sure that this is a global symbol.
      const MachineOperand &SymbolOp = Instr.getOperand(1);
      if (!SymbolOp.isGlobal())
        return false;

      // Finally return true only if the GOT flag is present.
      return PPCInstrInfo::hasGOTFlag(SymbolOp.getTargetFlags());
    }

    bool addLinkerOpt(MachineBasicBlock &MBB, const TargetRegisterInfo *TRI) {
      MachineFunction *MF = MBB.getParent();
      // If the linker opt is disabled then just return.
      if (!EnablePCRelLinkerOpt)
        return false;

      // Add this linker opt only if we are using PC Relative memops.
      if (!MF->getSubtarget<PPCSubtarget>().isUsingPCRelativeCalls())
        return false;

      // Struct to keep track of one def/use pair for a GOT indirect access.
      struct GOTDefUsePair {
        MachineBasicBlock::iterator DefInst;
        MachineBasicBlock::iterator UseInst;
        Register DefReg;
        Register UseReg;
        bool StillValid;
      };
      // Vector of def/ues pairs in this basic block.
      SmallVector<GOTDefUsePair, 4> CandPairs;
      SmallVector<GOTDefUsePair, 4> ValidPairs;
      bool MadeChange = false;

      // Run through all of the instructions in the basic block and try to
      // collect potential pairs of GOT indirect access instructions.
      for (auto BBI = MBB.instr_begin(); BBI != MBB.instr_end(); ++BBI) {
        // Look for the initial GOT indirect load.
        if (isGOTPLDpc(*BBI)) {
          GOTDefUsePair CurrentPair{BBI, MachineBasicBlock::iterator(),
                                    BBI->getOperand(0).getReg(),
                                    PPC::NoRegister, true};
          CandPairs.push_back(CurrentPair);
          continue;
        }

        // We haven't encountered any new PLD instructions, nothing to check.
        if (CandPairs.empty())
          continue;

        // Run through the candidate pairs and see if any of the registers
        // defined in the PLD instructions are used by this instruction.
        // Note: the size of CandPairs can change in the loop.
        for (unsigned Idx = 0; Idx < CandPairs.size(); Idx++) {
          GOTDefUsePair &Pair = CandPairs[Idx];
          // The instruction does not use or modify this PLD's def reg,
          // ignore it.
          if (!BBI->readsRegister(Pair.DefReg, TRI) &&
              !BBI->modifiesRegister(Pair.DefReg, TRI))
            continue;

          // The use needs to be used in the address computation and not
          // as the register being stored for a store.
          const MachineOperand *UseOp =
              hasPCRelativeForm(*BBI) ? &BBI->getOperand(2) : nullptr;

          // Check for a valid use.
          if (UseOp && UseOp->isReg() && UseOp->getReg() == Pair.DefReg &&
              UseOp->isUse() && UseOp->isKill()) {
            Pair.UseInst = BBI;
            Pair.UseReg = BBI->getOperand(0).getReg();
            ValidPairs.push_back(Pair);
          }
          CandPairs.erase(CandPairs.begin() + Idx);
        }
      }

      // Go through all of the pairs and check for any more valid uses.
      for (auto Pair = ValidPairs.begin(); Pair != ValidPairs.end(); Pair++) {
        // We shouldn't be here if we don't have a valid pair.
        assert(Pair->UseInst.isValid() && Pair->StillValid &&
               "Kept an invalid def/use pair for GOT PCRel opt");
        // We have found a potential pair. Search through the instructions
        // between the def and the use to see if it is valid to mark this as a
        // linker opt.
        MachineBasicBlock::iterator BBI = Pair->DefInst;
        ++BBI;
        for (; BBI != Pair->UseInst; ++BBI) {
          if (BBI->readsRegister(Pair->UseReg, TRI) ||
              BBI->modifiesRegister(Pair->UseReg, TRI)) {
            Pair->StillValid = false;
            break;
          }
        }

        if (!Pair->StillValid)
          continue;

        // The load/store instruction that uses the address from the PLD will
        // either use a register (for a store) or define a register (for the
        // load). That register will be added as an implicit def to the PLD
        // and as an implicit use on the second memory op. This is a precaution
        // to prevent future passes from using that register between the two
        // instructions.
        MachineOperand ImplDef =
            MachineOperand::CreateReg(Pair->UseReg, true, true);
        MachineOperand ImplUse =
            MachineOperand::CreateReg(Pair->UseReg, false, true);
        Pair->DefInst->addOperand(ImplDef);
        Pair->UseInst->addOperand(ImplUse);

        // Create the symbol.
        MCContext &Context = MF->getContext();
        MCSymbol *Symbol = Context.createNamedTempSymbol("pcrel");
        MachineOperand PCRelLabel =
            MachineOperand::CreateMCSymbol(Symbol, PPCII::MO_PCREL_OPT_FLAG);
        Pair->DefInst->addOperand(*MF, PCRelLabel);
        Pair->UseInst->addOperand(*MF, PCRelLabel);
        MadeChange |= true;
      }
      return MadeChange;
    }

    // This function removes redundant pairs of accumulator prime/unprime
    // instructions. In some situations, it's possible the compiler inserts an
    // accumulator prime instruction followed by an unprime instruction (e.g.
    // when we store an accumulator after restoring it from a spill). If the
    // accumulator is not used between the two, they can be removed. This
    // function removes these redundant pairs from basic blocks.
    // The algorithm is quite straightforward - every time we encounter a prime
    // instruction, the primed register is added to a candidate set. Any use
    // other than a prime removes the candidate from the set and any de-prime
    // of a current candidate marks both the prime and de-prime for removal.
    // This way we ensure we only remove prime/de-prime *pairs* with no
    // intervening uses.
    bool removeAccPrimeUnprime(MachineBasicBlock &MBB) {
      DenseSet<MachineInstr *> InstrsToErase;
      // Initially, none of the acc registers are candidates.
      SmallVector<MachineInstr *, 8> Candidates(
          PPC::UACCRCRegClass.getNumRegs(), nullptr);

      for (MachineInstr &BBI : MBB.instrs()) {
        unsigned Opc = BBI.getOpcode();
        // If we are visiting a xxmtacc instruction, we add it and its operand
        // register to the candidate set.
        if (Opc == PPC::XXMTACC) {
          Register Acc = BBI.getOperand(0).getReg();
          assert(PPC::ACCRCRegClass.contains(Acc) &&
                 "Unexpected register for XXMTACC");
          Candidates[Acc - PPC::ACC0] = &BBI;
        }
        // If we are visiting a xxmfacc instruction and its operand register is
        // in the candidate set, we mark the two instructions for removal.
        else if (Opc == PPC::XXMFACC) {
          Register Acc = BBI.getOperand(0).getReg();
          assert(PPC::ACCRCRegClass.contains(Acc) &&
                 "Unexpected register for XXMFACC");
          if (!Candidates[Acc - PPC::ACC0])
            continue;
          InstrsToErase.insert(&BBI);
          InstrsToErase.insert(Candidates[Acc - PPC::ACC0]);
        }
        // If we are visiting an instruction using an accumulator register
        // as operand, we remove it from the candidate set.
        else {
          for (MachineOperand &Operand : BBI.operands()) {
            if (!Operand.isReg())
              continue;
            Register Reg = Operand.getReg();
            if (PPC::ACCRCRegClass.contains(Reg))
              Candidates[Reg - PPC::ACC0] = nullptr;
          }
        }
      }

      for (MachineInstr *MI : InstrsToErase)
        MI->eraseFromParent();
      NumRemovedInPreEmit += InstrsToErase.size();
      return !InstrsToErase.empty();
    }

    bool runOnMachineFunction(MachineFunction &MF) override {
      // If the user wants to set the DSCR using command-line options,
      // load in the specified value at the start of main.
      if (DSCRValue.getNumOccurrences() > 0 && MF.getName() == "main" &&
          MF.getFunction().hasExternalLinkage()) {
        DSCRValue = (uint32_t)(DSCRValue & 0x01FFFFFF); // 25-bit DSCR mask
        RegScavenger RS;
        MachineBasicBlock &MBB = MF.front();
        // Find an unused GPR according to register liveness
        RS.enterBasicBlock(MBB);
        unsigned InDSCR = RS.FindUnusedReg(&PPC::GPRCRegClass);
        if (InDSCR) {
          const PPCInstrInfo *TII =
              MF.getSubtarget<PPCSubtarget>().getInstrInfo();
          DebugLoc dl;
          MachineBasicBlock::iterator IP = MBB.begin(); // Insert Point
          // Copy the 32-bit DSCRValue integer into the GPR InDSCR using LIS and
          // ORI, then move to DSCR. If the requested DSCR value is contained
          // in a 16-bit signed number, we can emit a single `LI`, but the
          // impact of saving one instruction in one function does not warrant
          // any additional complexity in the logic here.
          BuildMI(MBB, IP, dl, TII->get(PPC::LIS), InDSCR)
              .addImm(DSCRValue >> 16);
          BuildMI(MBB, IP, dl, TII->get(PPC::ORI), InDSCR)
              .addReg(InDSCR)
              .addImm(DSCRValue & 0xFFFF);
          BuildMI(MBB, IP, dl, TII->get(PPC::MTUDSCR))
              .addReg(InDSCR, RegState::Kill);
        } else
          errs() << "Warning: Ran out of registers - Unable to set DSCR as "
                    "requested";
      }

      if (skipFunction(MF.getFunction()) || !RunPreEmitPeephole) {
        // Remove UNENCODED_NOP even when this pass is disabled.
        // This needs to be done unconditionally so we don't emit zeros
        // in the instruction stream.
        SmallVector<MachineInstr *, 4> InstrsToErase;
        for (MachineBasicBlock &MBB : MF)
          for (MachineInstr &MI : MBB)
            if (MI.getOpcode() == PPC::UNENCODED_NOP)
              InstrsToErase.push_back(&MI);
        for (MachineInstr *MI : InstrsToErase)
          MI->eraseFromParent();
        return false;
      }
      bool Changed = false;
      const PPCInstrInfo *TII = MF.getSubtarget<PPCSubtarget>().getInstrInfo();
      const TargetRegisterInfo *TRI = MF.getSubtarget().getRegisterInfo();
      SmallVector<MachineInstr *, 4> InstrsToErase;
      for (MachineBasicBlock &MBB : MF) {
        Changed |= removeRedundantLIs(MBB, TRI);
        Changed |= addLinkerOpt(MBB, TRI);
        Changed |= removeAccPrimeUnprime(MBB);
        for (MachineInstr &MI : MBB) {
          unsigned Opc = MI.getOpcode();
          if (Opc == PPC::UNENCODED_NOP) {
            InstrsToErase.push_back(&MI);
            continue;
          }
          // Detect self copies - these can result from running AADB.
          if (PPCInstrInfo::isSameClassPhysRegCopy(Opc)) {
            const MCInstrDesc &MCID = TII->get(Opc);
            if (MCID.getNumOperands() == 3 &&
                MI.getOperand(0).getReg() == MI.getOperand(1).getReg() &&
                MI.getOperand(0).getReg() == MI.getOperand(2).getReg()) {
              NumberOfSelfCopies++;
              LLVM_DEBUG(dbgs() << "Deleting self-copy instruction: ");
              LLVM_DEBUG(MI.dump());
              InstrsToErase.push_back(&MI);
              continue;
            }
            else if (MCID.getNumOperands() == 2 &&
                     MI.getOperand(0).getReg() == MI.getOperand(1).getReg()) {
              NumberOfSelfCopies++;
              LLVM_DEBUG(dbgs() << "Deleting self-copy instruction: ");
              LLVM_DEBUG(MI.dump());
              InstrsToErase.push_back(&MI);
              continue;
            }
          }
          MachineInstr *DefMIToErase = nullptr;
          SmallSet<Register, 4> UpdatedRegs;
          if (TII->convertToImmediateForm(MI, UpdatedRegs, &DefMIToErase)) {
            Changed = true;
            NumRRConvertedInPreEmit++;
            LLVM_DEBUG(dbgs() << "Converted instruction to imm form: ");
            LLVM_DEBUG(MI.dump());
            if (DefMIToErase) {
              InstrsToErase.push_back(DefMIToErase);
            }
          }
          if (TII->foldFrameOffset(MI)) {
            Changed = true;
            NumFrameOffFoldInPreEmit++;
            LLVM_DEBUG(dbgs() << "Frame offset folding by using index form: ");
            LLVM_DEBUG(MI.dump());
          }
          if (TII->optimizeCmpPostRA(MI)) {
            Changed = true;
            NumCmpsInPreEmit++;
            LLVM_DEBUG(dbgs() << "Optimize compare by using record form: ");
            LLVM_DEBUG(MI.dump());
            InstrsToErase.push_back(&MI);
          }
        }

        // Eliminate conditional branch based on a constant CR bit by
        // CRSET or CRUNSET. We eliminate the conditional branch or
        // convert it into an unconditional branch. Also, if the CR bit
        // is not used by other instructions, we eliminate CRSET as well.
        auto I = MBB.getFirstInstrTerminator();
        if (I == MBB.instr_end())
          continue;
        MachineInstr *Br = &*I;
        if (Br->getOpcode() != PPC::BC && Br->getOpcode() != PPC::BCn)
          continue;
        MachineInstr *CRSetMI = nullptr;
        Register CRBit = Br->getOperand(0).getReg();
        unsigned CRReg = getCRFromCRBit(CRBit);
        bool SeenUse = false;
        MachineBasicBlock::reverse_iterator It = Br, Er = MBB.rend();
        for (It++; It != Er; It++) {
          if (It->modifiesRegister(CRBit, TRI)) {
            if ((It->getOpcode() == PPC::CRUNSET ||
                 It->getOpcode() == PPC::CRSET) &&
                It->getOperand(0).getReg() == CRBit)
              CRSetMI = &*It;
            break;
          }
          if (It->readsRegister(CRBit, TRI))
            SeenUse = true;
        }
        if (!CRSetMI) continue;

        unsigned CRSetOp = CRSetMI->getOpcode();
        if ((Br->getOpcode() == PPC::BCn && CRSetOp == PPC::CRSET) ||
            (Br->getOpcode() == PPC::BC  && CRSetOp == PPC::CRUNSET)) {
          // Remove this branch since it cannot be taken.
          InstrsToErase.push_back(Br);
          MBB.removeSuccessor(Br->getOperand(1).getMBB());
        }
        else {
          // This conditional branch is always taken. So, remove all branches
          // and insert an unconditional branch to the destination of this.
          MachineBasicBlock::iterator It = Br, Er = MBB.end();
          for (; It != Er; It++) {
            if (It->isDebugInstr()) continue;
            assert(It->isTerminator() && "Non-terminator after a terminator");
            InstrsToErase.push_back(&*It);
          }
          if (!MBB.isLayoutSuccessor(Br->getOperand(1).getMBB())) {
            ArrayRef<MachineOperand> NoCond;
            TII->insertBranch(MBB, Br->getOperand(1).getMBB(), nullptr,
                              NoCond, Br->getDebugLoc());
          }
          for (auto &Succ : MBB.successors())
            if (Succ != Br->getOperand(1).getMBB()) {
              MBB.removeSuccessor(Succ);
              break;
            }
        }

        // If the CRBit is not used by another instruction, we can eliminate
        // CRSET/CRUNSET instruction.
        if (!SeenUse) {
          // We need to check use of the CRBit in successors.
          for (auto &SuccMBB : MBB.successors())
            if (SuccMBB->isLiveIn(CRBit) || SuccMBB->isLiveIn(CRReg)) {
              SeenUse = true;
              break;
            }
          if (!SeenUse)
            InstrsToErase.push_back(CRSetMI);
        }
      }
      for (MachineInstr *MI : InstrsToErase) {
        LLVM_DEBUG(dbgs() << "PPC pre-emit peephole: erasing instruction: ");
        LLVM_DEBUG(MI->dump());
        MI->eraseFromParent();
        NumRemovedInPreEmit++;
      }
      return Changed;
    }
  };
}

INITIALIZE_PASS(PPCPreEmitPeephole, DEBUG_TYPE, "PowerPC Pre-Emit Peephole",
                false, false)
char PPCPreEmitPeephole::ID = 0;

FunctionPass *llvm::createPPCPreEmitPeepholePass() {
  return new PPCPreEmitPeephole();
}
