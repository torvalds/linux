//===-------------- PPCVSXCopy.cpp - VSX Copy Legalization ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A pass which deals with the complexity of generating legal VSX register
// copies to/from register classes which partially overlap with the VSX
// register file.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/PPCPredicates.h"
#include "PPC.h"
#include "PPCHazardRecognizers.h"
#include "PPCInstrBuilder.h"
#include "PPCInstrInfo.h"
#include "PPCMachineFunctionInfo.h"
#include "PPCTargetMachine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "ppc-vsx-copy"

namespace {
  // PPCVSXCopy pass - For copies between VSX registers and non-VSX registers
  // (Altivec and scalar floating-point registers), we need to transform the
  // copies into subregister copies with other restrictions.
  struct PPCVSXCopy : public MachineFunctionPass {
    static char ID;
    PPCVSXCopy() : MachineFunctionPass(ID) {
      initializePPCVSXCopyPass(*PassRegistry::getPassRegistry());
    }

    const TargetInstrInfo *TII;

    bool IsRegInClass(unsigned Reg, const TargetRegisterClass *RC,
                      MachineRegisterInfo &MRI) {
      if (Register::isVirtualRegister(Reg)) {
        return RC->hasSubClassEq(MRI.getRegClass(Reg));
      } else if (RC->contains(Reg)) {
        return true;
      }

      return false;
    }

    bool IsVSReg(unsigned Reg, MachineRegisterInfo &MRI) {
      return IsRegInClass(Reg, &PPC::VSRCRegClass, MRI);
    }

    bool IsVRReg(unsigned Reg, MachineRegisterInfo &MRI) {
      return IsRegInClass(Reg, &PPC::VRRCRegClass, MRI);
    }

    bool IsF8Reg(unsigned Reg, MachineRegisterInfo &MRI) {
      return IsRegInClass(Reg, &PPC::F8RCRegClass, MRI);
    }

    bool IsVSFReg(unsigned Reg, MachineRegisterInfo &MRI) {
      return IsRegInClass(Reg, &PPC::VSFRCRegClass, MRI);
    }

    bool IsVSSReg(unsigned Reg, MachineRegisterInfo &MRI) {
      return IsRegInClass(Reg, &PPC::VSSRCRegClass, MRI);
    }

protected:
    bool processBlock(MachineBasicBlock &MBB) {
      bool Changed = false;

      MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
      for (MachineInstr &MI : MBB) {
        if (!MI.isFullCopy())
          continue;

        MachineOperand &DstMO = MI.getOperand(0);
        MachineOperand &SrcMO = MI.getOperand(1);

        if ( IsVSReg(DstMO.getReg(), MRI) &&
            !IsVSReg(SrcMO.getReg(), MRI)) {
          // This is a copy *to* a VSX register from a non-VSX register.
          Changed = true;

          const TargetRegisterClass *SrcRC = &PPC::VSLRCRegClass;
          assert((IsF8Reg(SrcMO.getReg(), MRI) ||
                  IsVSSReg(SrcMO.getReg(), MRI) ||
                  IsVSFReg(SrcMO.getReg(), MRI)) &&
                 "Unknown source for a VSX copy");

          Register NewVReg = MRI.createVirtualRegister(SrcRC);
          BuildMI(MBB, MI, MI.getDebugLoc(),
                  TII->get(TargetOpcode::SUBREG_TO_REG), NewVReg)
              .addImm(1) // add 1, not 0, because there is no implicit clearing
                         // of the high bits.
              .add(SrcMO)
              .addImm(PPC::sub_64);

          // The source of the original copy is now the new virtual register.
          SrcMO.setReg(NewVReg);
        } else if (!IsVSReg(DstMO.getReg(), MRI) &&
                    IsVSReg(SrcMO.getReg(), MRI)) {
          // This is a copy *from* a VSX register to a non-VSX register.
          Changed = true;

          const TargetRegisterClass *DstRC = &PPC::VSLRCRegClass;
          assert((IsF8Reg(DstMO.getReg(), MRI) ||
                  IsVSFReg(DstMO.getReg(), MRI) ||
                  IsVSSReg(DstMO.getReg(), MRI)) &&
                 "Unknown destination for a VSX copy");

          // Copy the VSX value into a new VSX register of the correct subclass.
          Register NewVReg = MRI.createVirtualRegister(DstRC);
          BuildMI(MBB, MI, MI.getDebugLoc(), TII->get(TargetOpcode::COPY),
                  NewVReg)
              .add(SrcMO);

          // Transform the original copy into a subregister extraction copy.
          SrcMO.setReg(NewVReg);
          SrcMO.setSubReg(PPC::sub_64);
        }
      }

      return Changed;
    }

public:
    bool runOnMachineFunction(MachineFunction &MF) override {
      // If we don't have VSX on the subtarget, don't do anything.
      const PPCSubtarget &STI = MF.getSubtarget<PPCSubtarget>();
      if (!STI.hasVSX())
        return false;
      TII = STI.getInstrInfo();

      bool Changed = false;

      for (MachineBasicBlock &B : llvm::make_early_inc_range(MF))
        if (processBlock(B))
          Changed = true;

      return Changed;
    }

    void getAnalysisUsage(AnalysisUsage &AU) const override {
      MachineFunctionPass::getAnalysisUsage(AU);
    }
  };
}

INITIALIZE_PASS(PPCVSXCopy, DEBUG_TYPE,
                "PowerPC VSX Copy Legalization", false, false)

char PPCVSXCopy::ID = 0;
FunctionPass*
llvm::createPPCVSXCopyPass() { return new PPCVSXCopy(); }
