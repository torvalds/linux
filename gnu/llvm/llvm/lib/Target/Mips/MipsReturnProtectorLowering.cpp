//===-- MipsReturnProtectorLowering.cpp --------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Mips implementation of ReturnProtectorLowering
// class.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/MipsBaseInfo.h"
#include "MipsInstrInfo.h"
#include "MipsMachineFunction.h"
#include "MipsReturnProtectorLowering.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetOptions.h"
#include <cstdlib>

using namespace llvm;

void MipsReturnProtectorLowering::insertReturnProtectorPrologue(
    MachineFunction &MF, MachineBasicBlock &MBB, GlobalVariable *cookie) const {

  MachineBasicBlock::instr_iterator MI = MBB.instr_begin();
  DebugLoc MBBDL = MBB.findDebugLoc(MI);
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  const TargetMachine &TM = MF.getTarget();
  unsigned REG = MF.getFrameInfo().getReturnProtectorRegister();

  const GlobalValue *FName = &MF.getFunction();

  // Select some scratch registers
  unsigned TempReg1 = Mips::AT_64;
  unsigned TempReg2 = Mips::V0_64;
  if (!MBB.isLiveIn(TempReg1))
    MBB.addLiveIn(TempReg1);
  if (!MBB.isLiveIn(TempReg2))
    MBB.addLiveIn(TempReg2);

  if (TM.isPositionIndependent()) {

    if (!MBB.isLiveIn(Mips::T9_64))
      MBB.addLiveIn(Mips::T9_64);

    // TempReg1 loads the GOT pointer
    // TempReg2 load the offset from GOT to random cookie pointer
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::LUi64), TempReg1)
      .addGlobalAddress(FName, 0, MipsII::MO_GPOFF_HI);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::LUi64), TempReg2)
      .addGlobalAddress(cookie, 0, MipsII::MO_GOT_HI16);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::DADDu), TempReg1)
      .addReg(TempReg1)
      .addReg(Mips::T9_64);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::DADDiu), TempReg2)
      .addReg(TempReg2)
      .addGlobalAddress(cookie, 0, MipsII::MO_GOT_LO16);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::DADDu), TempReg1)
      .addReg(TempReg1)
      .addReg(TempReg2);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::LD), REG)
      .addReg(TempReg1)
      .addGlobalAddress(FName, 0, MipsII::MO_GPOFF_LO);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::LD), REG)
      .addReg(REG)
      .addImm(0);
  } else {
    // TempReg1 loads the high 32 bits
    // TempReg2 loads the low 32 bits
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::LUi64), TempReg1)
      .addGlobalAddress(cookie, 0, MipsII::MO_HIGHEST);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::LUi64), TempReg2)
      .addGlobalAddress(cookie, 0, MipsII::MO_ABS_HI);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::DADDiu), TempReg1)
      .addReg(TempReg1)
      .addGlobalAddress(cookie, 0, MipsII::MO_HIGHER);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::DSLL), TempReg1)
      .addReg(TempReg1)
      .addImm(32);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::DADDu), TempReg1)
      .addReg(TempReg1)
      .addReg(TempReg2);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::LD), REG)
      .addReg(TempReg1)
      .addGlobalAddress(cookie, 0, MipsII::MO_ABS_LO);
  }

  BuildMI(MBB, MI, MBBDL, TII->get(Mips::XOR64), REG)
    .addReg(REG)
    .addReg(Mips::RA_64);
}

void MipsReturnProtectorLowering::insertReturnProtectorEpilogue(
    MachineFunction &MF, MachineInstr &MI, GlobalVariable *cookie) const {


  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc MBBDL = MI.getDebugLoc();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  const TargetMachine &TM = MF.getTarget();
  unsigned REG = MF.getFrameInfo().getReturnProtectorRegister();

  const GlobalValue *FName = &MF.getFunction();
  const unsigned TRAPCODE = 0x52;

  // Select some scratch registers
  unsigned TempReg1 = Mips::T7_64;
  unsigned TempReg2 = Mips::T8_64;
  if (REG == Mips::T7_64 || REG == Mips::T8_64) {
    TempReg1 = Mips::T5_64;
    TempReg2 = Mips::T6_64;
  }
  if (!MBB.isLiveIn(TempReg1))
    MBB.addLiveIn(TempReg1);
  if (!MBB.isLiveIn(TempReg2))
    MBB.addLiveIn(TempReg2);

  // Undo the XOR to retrieve the random cookie
  BuildMI(MBB, MI, MBBDL, TII->get(Mips::XOR64), REG)
    .addReg(REG)
    .addReg(Mips::RA_64);

  // Load the random cookie
  if (TM.isPositionIndependent()) {

    if (!MBB.isLiveIn(Mips::T9_64))
      MBB.addLiveIn(Mips::T9_64);

    // T9 is trashed by this point, and we cannot trust saving
    // the value from function entry on the stack, so calculate
    // the address of the function entry using a pseudo
    MCSymbol *BALTarget = MF.getContext().createTempSymbol();
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::RETGUARD_GET_FUNCTION_ADDR), Mips::T9_64)
      .addReg(TempReg1)
      .addReg(TempReg2)
      .addSym(BALTarget);

    // TempReg1 loads the GOT pointer
    // TempReg2 load the offset from GOT to random cookie pointer
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::LUi64), TempReg1)
      .addGlobalAddress(FName, 0, MipsII::MO_GPOFF_HI);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::LUi64), TempReg2)
      .addGlobalAddress(cookie, 0, MipsII::MO_GOT_HI16);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::DADDu), TempReg1)
      .addReg(TempReg1)
      .addReg(Mips::T9_64);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::DADDiu), TempReg2)
      .addReg(TempReg2)
      .addGlobalAddress(cookie, 0, MipsII::MO_GOT_LO16);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::DADDu), TempReg1)
      .addReg(TempReg1)
      .addReg(TempReg2);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::LD), TempReg1)
      .addReg(TempReg1)
      .addGlobalAddress(FName, 0, MipsII::MO_GPOFF_LO);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::LD), TempReg1)
      .addReg(TempReg1)
      .addImm(0);
    // Verify the random cookie
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::TNE))
      .addReg(TempReg1)
      .addReg(REG)
      .addImm(TRAPCODE);
    // Emit the BAL target symbol from above
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::RETGUARD_EMIT_SYMBOL))
      .addSym(BALTarget);
  } else {
    // TempReg1 loads the high 32 bits
    // TempReg2 loads the low 32 bits
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::LUi64), TempReg1)
      .addGlobalAddress(cookie, 0, MipsII::MO_HIGHEST);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::LUi64), TempReg2)
      .addGlobalAddress(cookie, 0, MipsII::MO_ABS_HI);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::DADDiu), TempReg1)
      .addReg(TempReg1)
      .addGlobalAddress(cookie, 0, MipsII::MO_HIGHER);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::DSLL), TempReg1)
      .addReg(TempReg1)
      .addImm(32);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::DADDu), TempReg1)
      .addReg(TempReg1)
      .addReg(TempReg2);
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::LD), TempReg1)
      .addReg(TempReg1)
      .addGlobalAddress(cookie, 0, MipsII::MO_ABS_LO);
    // Verify the random cookie
    BuildMI(MBB, MI, MBBDL, TII->get(Mips::TNE))
      .addReg(TempReg1)
      .addReg(REG)
      .addImm(TRAPCODE);
  }
}

bool MipsReturnProtectorLowering::opcodeIsReturn(unsigned opcode) const {
  switch (opcode) {
    case Mips::RetRA:
      return true;
    default:
      return false;
  }
}

void MipsReturnProtectorLowering::fillTempRegisters(
    MachineFunction &MF, std::vector<unsigned> &TempRegs) const {

  const Function &F = MF.getFunction();

  // long double arguments (f128) occupy two arg registers, so shift
  // subsequent arguments down by one register
  size_t shift_reg = 0;
  for (const auto &arg : F.args()) {
    if (arg.getType()->isFP128Ty())
      shift_reg += 1;
  }

  if (!F.isVarArg()) {
    // We can use any of the caller saved unused arg registers
    switch (F.arg_size() + shift_reg) {
      case 0:
        // A0 is used to return f128 values in soft float
      case 1:
        TempRegs.push_back(Mips::A1_64);
        LLVM_FALLTHROUGH;
      case 2:
        TempRegs.push_back(Mips::A2_64);
        LLVM_FALLTHROUGH;
      case 3:
        TempRegs.push_back(Mips::A3_64);
        LLVM_FALLTHROUGH;
      case 4:
        TempRegs.push_back(Mips::T0_64);
        LLVM_FALLTHROUGH;
      case 5:
        TempRegs.push_back(Mips::T1_64);
        LLVM_FALLTHROUGH;
      case 6:
        TempRegs.push_back(Mips::T2_64);
        LLVM_FALLTHROUGH;
      case 7:
        TempRegs.push_back(Mips::T3_64);
        LLVM_FALLTHROUGH;
      case 8:
        TempRegs.push_back(Mips::T4_64);
        LLVM_FALLTHROUGH;
      case 9:
        TempRegs.push_back(Mips::T5_64);
        LLVM_FALLTHROUGH;
      case 10:
        TempRegs.push_back(Mips::T6_64);
        LLVM_FALLTHROUGH;
      case 11:
        TempRegs.push_back(Mips::T7_64);
        LLVM_FALLTHROUGH;
      case 12:
        TempRegs.push_back(Mips::T8_64);
        LLVM_FALLTHROUGH;
      default:
        break;
    }
  }
  // For FastCC this is the only scratch reg that isn't V0 or T9
  TempRegs.push_back(Mips::AT_64);
}
