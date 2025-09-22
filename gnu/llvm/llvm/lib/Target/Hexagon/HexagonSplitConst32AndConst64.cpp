//=== HexagonSplitConst32AndConst64.cpp - split CONST32/Const64 into HI/LO ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// When the compiler is invoked with no small data, for instance, with the -G0
// command line option, then all CONST* opcodes should be broken down into
// appropriate LO and HI instructions. This splitting is done by this pass.
// The only reason this is not done in the DAG lowering itself is that there
// is no simple way of getting the register allocator to allot the same hard
// register to the result of LO and HI instructions. This pass is always
// scheduled after register allocation.
//
//===----------------------------------------------------------------------===//

#include "HexagonSubtarget.h"
#include "HexagonTargetMachine.h"
#include "HexagonTargetObjectFile.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

using namespace llvm;

#define DEBUG_TYPE "xfer"

namespace llvm {
  FunctionPass *createHexagonSplitConst32AndConst64();
  void initializeHexagonSplitConst32AndConst64Pass(PassRegistry&);
}

namespace {
  class HexagonSplitConst32AndConst64 : public MachineFunctionPass {
  public:
    static char ID;
    HexagonSplitConst32AndConst64() : MachineFunctionPass(ID) {
      PassRegistry &R = *PassRegistry::getPassRegistry();
      initializeHexagonSplitConst32AndConst64Pass(R);
    }
    StringRef getPassName() const override {
      return "Hexagon Split Const32s and Const64s";
    }
    bool runOnMachineFunction(MachineFunction &Fn) override;
    MachineFunctionProperties getRequiredProperties() const override {
      return MachineFunctionProperties().set(
          MachineFunctionProperties::Property::NoVRegs);
    }
  };
}

char HexagonSplitConst32AndConst64::ID = 0;

INITIALIZE_PASS(HexagonSplitConst32AndConst64, "split-const-for-sdata",
      "Hexagon Split Const32s and Const64s", false, false)

bool HexagonSplitConst32AndConst64::runOnMachineFunction(MachineFunction &Fn) {
  auto &HST = Fn.getSubtarget<HexagonSubtarget>();
  auto &HTM = static_cast<const HexagonTargetMachine&>(Fn.getTarget());
  auto &TLOF = *HTM.getObjFileLowering();
  if (HST.useSmallData() && TLOF.isSmallDataEnabled(HTM))
    return false;

  const TargetInstrInfo *TII = HST.getInstrInfo();
  const TargetRegisterInfo *TRI = HST.getRegisterInfo();

  // Loop over all of the basic blocks
  for (MachineBasicBlock &B : Fn) {
    for (MachineInstr &MI : llvm::make_early_inc_range(B)) {
      unsigned Opc = MI.getOpcode();

      if (Opc == Hexagon::CONST32) {
        Register DestReg = MI.getOperand(0).getReg();
        uint64_t ImmValue = MI.getOperand(1).getImm();
        const DebugLoc &DL = MI.getDebugLoc();
        BuildMI(B, MI, DL, TII->get(Hexagon::A2_tfrsi), DestReg)
            .addImm(ImmValue);
        B.erase(&MI);
      } else if (Opc == Hexagon::CONST64) {
        Register DestReg = MI.getOperand(0).getReg();
        int64_t ImmValue = MI.getOperand(1).getImm();
        const DebugLoc &DL = MI.getDebugLoc();
        Register DestLo = TRI->getSubReg(DestReg, Hexagon::isub_lo);
        Register DestHi = TRI->getSubReg(DestReg, Hexagon::isub_hi);

        int32_t LowWord = (ImmValue & 0xFFFFFFFF);
        int32_t HighWord = (ImmValue >> 32) & 0xFFFFFFFF;

        BuildMI(B, MI, DL, TII->get(Hexagon::A2_tfrsi), DestLo)
            .addImm(LowWord);
        BuildMI(B, MI, DL, TII->get(Hexagon::A2_tfrsi), DestHi)
            .addImm(HighWord);
        B.erase(&MI);
      }
    }
  }

  return true;
}


//===----------------------------------------------------------------------===//
//                         Public Constructor Functions
//===----------------------------------------------------------------------===//
FunctionPass *llvm::createHexagonSplitConst32AndConst64() {
  return new HexagonSplitConst32AndConst64();
}
