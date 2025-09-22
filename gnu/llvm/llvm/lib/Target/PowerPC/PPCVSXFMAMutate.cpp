//===--------------- PPCVSXFMAMutate.cpp - VSX FMA Mutation ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass mutates the form of VSX FMA instructions to avoid unnecessary
// copies.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PPCPredicates.h"
#include "PPC.h"
#include "PPCInstrBuilder.h"
#include "PPCInstrInfo.h"
#include "PPCMachineFunctionInfo.h"
#include "PPCTargetMachine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/ScheduleDAG.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

// Temporarily disable FMA mutation by default, since it doesn't handle
// cross-basic-block intervals well.
// See: http://lists.llvm.org/pipermail/llvm-dev/2016-February/095669.html
//      http://reviews.llvm.org/D17087
static cl::opt<bool> DisableVSXFMAMutate(
    "disable-ppc-vsx-fma-mutation",
    cl::desc("Disable VSX FMA instruction mutation"), cl::init(true),
    cl::Hidden);

#define DEBUG_TYPE "ppc-vsx-fma-mutate"

namespace llvm { namespace PPC {
  int getAltVSXFMAOpcode(uint16_t Opcode);
} }

namespace {
  // PPCVSXFMAMutate pass - For copies between VSX registers and non-VSX registers
  // (Altivec and scalar floating-point registers), we need to transform the
  // copies into subregister copies with other restrictions.
  struct PPCVSXFMAMutate : public MachineFunctionPass {
    static char ID;
    PPCVSXFMAMutate() : MachineFunctionPass(ID) {
      initializePPCVSXFMAMutatePass(*PassRegistry::getPassRegistry());
    }

    LiveIntervals *LIS;
    const PPCInstrInfo *TII;

protected:
    bool processBlock(MachineBasicBlock &MBB) {
      bool Changed = false;

      MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
      const TargetRegisterInfo *TRI = &TII->getRegisterInfo();
      for (MachineBasicBlock::iterator I = MBB.begin(), IE = MBB.end();
           I != IE; ++I) {
        MachineInstr &MI = *I;

        // The default (A-type) VSX FMA form kills the addend (it is taken from
        // the target register, which is then updated to reflect the result of
        // the FMA). If the instruction, however, kills one of the registers
        // used for the product, then we can use the M-form instruction (which
        // will take that value from the to-be-defined register).

        int AltOpc = PPC::getAltVSXFMAOpcode(MI.getOpcode());
        if (AltOpc == -1)
          continue;

        // This pass is run after register coalescing, and so we're looking for
        // a situation like this:
        //   ...
        //   %5 = COPY %9; VSLRC:%5,%9
        //   %5<def,tied1> = XSMADDADP %5<tied0>, %17, %16,
        //                         implicit %rm; VSLRC:%5,%17,%16
        //   ...
        //   %9<def,tied1> = XSMADDADP %9<tied0>, %17, %19,
        //                         implicit %rm; VSLRC:%9,%17,%19
        //   ...
        // Where we can eliminate the copy by changing from the A-type to the
        // M-type instruction. Specifically, for this example, this means:
        //   %5<def,tied1> = XSMADDADP %5<tied0>, %17, %16,
        //                         implicit %rm; VSLRC:%5,%17,%16
        // is replaced by:
        //   %16<def,tied1> = XSMADDMDP %16<tied0>, %18, %9,
        //                         implicit %rm; VSLRC:%16,%18,%9
        // and we remove: %5 = COPY %9; VSLRC:%5,%9

        SlotIndex FMAIdx = LIS->getInstructionIndex(MI);

        VNInfo *AddendValNo =
            LIS->getInterval(MI.getOperand(1).getReg()).Query(FMAIdx).valueIn();

        // This can be null if the register is undef.
        if (!AddendValNo)
          continue;

        MachineInstr *AddendMI = LIS->getInstructionFromIndex(AddendValNo->def);

        // The addend and this instruction must be in the same block.

        if (!AddendMI || AddendMI->getParent() != MI.getParent())
          continue;

        // The addend must be a full copy within the same register class.

        if (!AddendMI->isFullCopy())
          continue;

        Register AddendSrcReg = AddendMI->getOperand(1).getReg();
        if (AddendSrcReg.isVirtual()) {
          if (MRI.getRegClass(AddendMI->getOperand(0).getReg()) !=
              MRI.getRegClass(AddendSrcReg))
            continue;
        } else {
          // If AddendSrcReg is a physical register, make sure the destination
          // register class contains it.
          if (!MRI.getRegClass(AddendMI->getOperand(0).getReg())
                ->contains(AddendSrcReg))
            continue;
        }

        // In theory, there could be other uses of the addend copy before this
        // fma.  We could deal with this, but that would require additional
        // logic below and I suspect it will not occur in any relevant
        // situations.  Additionally, check whether the copy source is killed
        // prior to the fma.  In order to replace the addend here with the
        // source of the copy, it must still be live here.  We can't use
        // interval testing for a physical register, so as long as we're
        // walking the MIs we may as well test liveness here.
        //
        // FIXME: There is a case that occurs in practice, like this:
        //   %9 = COPY %f1; VSSRC:%9
        //   ...
        //   %6 = COPY %9; VSSRC:%6,%9
        //   %7 = COPY %9; VSSRC:%7,%9
        //   %9<def,tied1> = XSMADDASP %9<tied0>, %1, %4; VSSRC:
        //   %6<def,tied1> = XSMADDASP %6<tied0>, %1, %2; VSSRC:
        //   %7<def,tied1> = XSMADDASP %7<tied0>, %1, %3; VSSRC:
        // which prevents an otherwise-profitable transformation.
        bool OtherUsers = false, KillsAddendSrc = false;
        for (auto J = std::prev(I), JE = MachineBasicBlock::iterator(AddendMI);
             J != JE; --J) {
          if (J->readsVirtualRegister(AddendMI->getOperand(0).getReg())) {
            OtherUsers = true;
            break;
          }
          if (J->modifiesRegister(AddendSrcReg, TRI) ||
              J->killsRegister(AddendSrcReg, TRI)) {
            KillsAddendSrc = true;
            break;
          }
        }

        if (OtherUsers || KillsAddendSrc)
          continue;


        // The transformation doesn't work well with things like:
        //    %5 = A-form-op %5, %11, %5;
        // unless %11 is also a kill, so skip when it is not,
        // and check operand 3 to see it is also a kill to handle the case:
        //   %5 = A-form-op %5, %5, %11;
        // where %5 and %11 are both kills. This case would be skipped
        // otherwise.
        Register OldFMAReg = MI.getOperand(0).getReg();

        // Find one of the product operands that is killed by this instruction.
        unsigned KilledProdOp = 0, OtherProdOp = 0;
        Register Reg2 = MI.getOperand(2).getReg();
        Register Reg3 = MI.getOperand(3).getReg();
        if (LIS->getInterval(Reg2).Query(FMAIdx).isKill()
            && Reg2 != OldFMAReg) {
          KilledProdOp = 2;
          OtherProdOp  = 3;
        } else if (LIS->getInterval(Reg3).Query(FMAIdx).isKill()
            && Reg3 != OldFMAReg) {
          KilledProdOp = 3;
          OtherProdOp  = 2;
        }

        // If there are no usable killed product operands, then this
        // transformation is likely not profitable.
        if (!KilledProdOp)
          continue;

        // If the addend copy is used only by this MI, then the addend source
        // register is likely not live here. This could be fixed (based on the
        // legality checks above, the live range for the addend source register
        // could be extended), but it seems likely that such a trivial copy can
        // be coalesced away later, and thus is not worth the effort.
        if (AddendSrcReg.isVirtual() &&
            !LIS->getInterval(AddendSrcReg).liveAt(FMAIdx))
          continue;

        // Transform: (O2 * O3) + O1 -> (O2 * O1) + O3.

        Register KilledProdReg = MI.getOperand(KilledProdOp).getReg();
        Register OtherProdReg = MI.getOperand(OtherProdOp).getReg();

        unsigned AddSubReg = AddendMI->getOperand(1).getSubReg();
        unsigned KilledProdSubReg = MI.getOperand(KilledProdOp).getSubReg();
        unsigned OtherProdSubReg = MI.getOperand(OtherProdOp).getSubReg();

        bool AddRegKill = AddendMI->getOperand(1).isKill();
        bool KilledProdRegKill = MI.getOperand(KilledProdOp).isKill();
        bool OtherProdRegKill = MI.getOperand(OtherProdOp).isKill();

        bool AddRegUndef = AddendMI->getOperand(1).isUndef();
        bool KilledProdRegUndef = MI.getOperand(KilledProdOp).isUndef();
        bool OtherProdRegUndef = MI.getOperand(OtherProdOp).isUndef();

        // If there isn't a class that fits, we can't perform the transform.
        // This is needed for correctness with a mixture of VSX and Altivec
        // instructions to make sure that a low VSX register is not assigned to
        // the Altivec instruction.
        if (!MRI.constrainRegClass(KilledProdReg,
                                   MRI.getRegClass(OldFMAReg)))
          continue;

        assert(OldFMAReg == AddendMI->getOperand(0).getReg() &&
               "Addend copy not tied to old FMA output!");

        LLVM_DEBUG(dbgs() << "VSX FMA Mutation:\n    " << MI);

        MI.getOperand(0).setReg(KilledProdReg);
        MI.getOperand(1).setReg(KilledProdReg);
        MI.getOperand(3).setReg(AddendSrcReg);

        MI.getOperand(0).setSubReg(KilledProdSubReg);
        MI.getOperand(1).setSubReg(KilledProdSubReg);
        MI.getOperand(3).setSubReg(AddSubReg);

        MI.getOperand(1).setIsKill(KilledProdRegKill);
        MI.getOperand(3).setIsKill(AddRegKill);

        MI.getOperand(1).setIsUndef(KilledProdRegUndef);
        MI.getOperand(3).setIsUndef(AddRegUndef);

        MI.setDesc(TII->get(AltOpc));

        // If the addend is also a multiplicand, replace it with the addend
        // source in both places.
        if (OtherProdReg == AddendMI->getOperand(0).getReg()) {
          MI.getOperand(2).setReg(AddendSrcReg);
          MI.getOperand(2).setSubReg(AddSubReg);
          MI.getOperand(2).setIsKill(AddRegKill);
          MI.getOperand(2).setIsUndef(AddRegUndef);
        } else {
          MI.getOperand(2).setReg(OtherProdReg);
          MI.getOperand(2).setSubReg(OtherProdSubReg);
          MI.getOperand(2).setIsKill(OtherProdRegKill);
          MI.getOperand(2).setIsUndef(OtherProdRegUndef);
        }

        LLVM_DEBUG(dbgs() << " -> " << MI);

        // The killed product operand was killed here, so we can reuse it now
        // for the result of the fma.

        LiveInterval &FMAInt = LIS->getInterval(OldFMAReg);
        VNInfo *FMAValNo = FMAInt.getVNInfoAt(FMAIdx.getRegSlot());
        for (auto UI = MRI.reg_nodbg_begin(OldFMAReg), UE = MRI.reg_nodbg_end();
             UI != UE;) {
          MachineOperand &UseMO = *UI;
          MachineInstr *UseMI = UseMO.getParent();
          ++UI;

          // Don't replace the result register of the copy we're about to erase.
          if (UseMI == AddendMI)
            continue;

          UseMO.substVirtReg(KilledProdReg, KilledProdSubReg, *TRI);
        }

        // Extend the live intervals of the killed product operand to hold the
        // fma result.

        LiveInterval &NewFMAInt = LIS->getInterval(KilledProdReg);
        for (auto &AI : FMAInt) {
          // Don't add the segment that corresponds to the original copy.
          if (AI.valno == AddendValNo)
            continue;

          VNInfo *NewFMAValNo =
              NewFMAInt.getNextValue(AI.start, LIS->getVNInfoAllocator());

          NewFMAInt.addSegment(
              LiveInterval::Segment(AI.start, AI.end, NewFMAValNo));
        }
        LLVM_DEBUG(dbgs() << "  extended: " << NewFMAInt << '\n');

        // Extend the live interval of the addend source (it might end at the
        // copy to be removed, or somewhere in between there and here). This
        // is necessary only if it is a physical register.
        if (!AddendSrcReg.isVirtual())
          for (MCRegUnit Unit : TRI->regunits(AddendSrcReg.asMCReg())) {
            LiveRange &AddendSrcRange = LIS->getRegUnit(Unit);
            AddendSrcRange.extendInBlock(LIS->getMBBStartIdx(&MBB),
                                         FMAIdx.getRegSlot());
            LLVM_DEBUG(dbgs() << "  extended: " << AddendSrcRange << '\n');
          }

        FMAInt.removeValNo(FMAValNo);
        LLVM_DEBUG(dbgs() << "  trimmed:  " << FMAInt << '\n');

        // Remove the (now unused) copy.

        LLVM_DEBUG(dbgs() << "  removing: " << *AddendMI << '\n');
        LIS->RemoveMachineInstrFromMaps(*AddendMI);
        AddendMI->eraseFromParent();

        Changed = true;
      }

      return Changed;
    }

public:
    bool runOnMachineFunction(MachineFunction &MF) override {
      if (skipFunction(MF.getFunction()))
        return false;

      // If we don't have VSX then go ahead and return without doing
      // anything.
      const PPCSubtarget &STI = MF.getSubtarget<PPCSubtarget>();
      if (!STI.hasVSX())
        return false;

      LIS = &getAnalysis<LiveIntervalsWrapperPass>().getLIS();

      TII = STI.getInstrInfo();

      bool Changed = false;

      if (DisableVSXFMAMutate)
        return Changed;

      for (MachineBasicBlock &B : llvm::make_early_inc_range(MF))
        if (processBlock(B))
          Changed = true;

      return Changed;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.addRequired<LiveIntervalsWrapperPass>();
      AU.addPreserved<LiveIntervalsWrapperPass>();
      AU.addRequired<SlotIndexesWrapperPass>();
      AU.addPreserved<SlotIndexesWrapperPass>();
      AU.addRequired<MachineDominatorTreeWrapperPass>();
      AU.addPreserved<MachineDominatorTreeWrapperPass>();
      MachineFunctionPass::getAnalysisUsage(AU);
    }
  };
}

INITIALIZE_PASS_BEGIN(PPCVSXFMAMutate, DEBUG_TYPE,
                      "PowerPC VSX FMA Mutation", false, false)
INITIALIZE_PASS_DEPENDENCY(LiveIntervalsWrapperPass)
INITIALIZE_PASS_DEPENDENCY(SlotIndexesWrapperPass)
INITIALIZE_PASS_DEPENDENCY(MachineDominatorTreeWrapperPass)
INITIALIZE_PASS_END(PPCVSXFMAMutate, DEBUG_TYPE,
                    "PowerPC VSX FMA Mutation", false, false)

char &llvm::PPCVSXFMAMutateID = PPCVSXFMAMutate::ID;

char PPCVSXFMAMutate::ID = 0;
FunctionPass *llvm::createPPCVSXFMAMutatePass() {
  return new PPCVSXFMAMutate();
}
