//===-- PPCReturnProtectorLowering.cpp --------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the PPC implementation of ReturnProtectorLowering
// class.
//
//===----------------------------------------------------------------------===//

#include "PPC.h"
#include "PPCInstrInfo.h"
#include "PPCMachineFunctionInfo.h"
#include "PPCReturnProtectorLowering.h"
#include "PPCTargetMachine.h"
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

void PPCReturnProtectorLowering::insertReturnProtectorPrologue(
    MachineFunction &MF, MachineBasicBlock &MBB, GlobalVariable *cookie) const {

  MachineBasicBlock::instr_iterator MI = MBB.instr_begin();
  DebugLoc MBBDL = MBB.findDebugLoc(MI);
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  const TargetMachine &TM = MF.getTarget();
  unsigned REG = MF.getFrameInfo().getReturnProtectorRegister();
  bool is64bit = MF.getSubtarget<PPCSubtarget>().isPPC64();
  bool isELFv2 = MF.getSubtarget<PPCSubtarget>().isELFv2ABI();

  unsigned LRReg = PPC::R0;
  unsigned TOCReg = PPC::R2;
  unsigned XOR = PPC::XOR;
  unsigned LWZ = PPC::LWZ;
  unsigned MFLR = PPC::MFLR;
  if (is64bit) {
    LRReg = PPC::X0;
    TOCReg = PPC::X2;
    XOR = PPC::XOR8;
    LWZ = PPC::LWZ8;
    MFLR = PPC::MFLR8;
  }

  if (!MBB.isLiveIn(LRReg))
    MBB.addLiveIn(LRReg);

  if (is64bit) {
    // PIC and non-PIC is the same
    if (!isELFv2)
      llvm_unreachable("ppc64 retguard requires ELFv2");
    // Get the return address into LRReg
    BuildMI(MBB, MI, MBBDL, TII->get(MFLR), LRReg);
    // Load the random cookie value into REG
    BuildMI(MBB, MI, MBBDL, TII->get(PPC::ADDIStocHA8), REG)
      .addReg(TOCReg)
      .addGlobalAddress(cookie);
    BuildMI(MBB, MI, MBBDL, TII->get(PPC::LD), REG)
      .addGlobalAddress(cookie, 0, PPCII::MO_TOC_LO)
      .addReg(REG);
    // XOR cookie ^ random = retguard cookie
    BuildMI(MBB, MI, MBBDL, TII->get(XOR), REG)
      .addReg(REG)
      .addReg(LRReg);
  } else {
    // 32 bit
    if (TM.isPositionIndependent()) {
      MCSymbol *HereSym = MF.getContext().createTempSymbol();
      // Get LR into a register, and get PC into another register
      BuildMI(MBB, MI, MBBDL, TII->get(PPC::RETGUARD_LOAD_PC), REG)
        .addReg(LRReg, RegState::Define)
        .addSym(HereSym);
      // Get the random cookie address into REG
      BuildMI(MBB, MI, MBBDL, TII->get(PPC::RETGUARD_LOAD_GOT), REG)
        .addReg(REG)
        .addSym(HereSym);
      BuildMI(MBB, MI, MBBDL, TII->get(PPC::LWZtoc), REG)
        .addGlobalAddress(cookie, 0, 0)
        .addReg(REG);
      // Now load the random cookie value
      BuildMI(MBB, MI, MBBDL, TII->get(PPC::LWZ), REG)
        .addImm(0)
        .addReg(REG);
      // XOR cookie ^ random = retguard cookie
      BuildMI(MBB, MI, MBBDL, TII->get(XOR), REG)
        .addReg(REG)
        .addReg(LRReg);
    } else {
      // Non-PIC prologue
      // Load LR into a register
      BuildMI(MBB, MI, MBBDL, TII->get(MFLR), LRReg);
      // Load random cookie into another register
      BuildMI(MBB, MI, MBBDL, TII->get(PPC::RETGUARD_LOAD_COOKIE), REG)
        .addGlobalAddress(cookie);
      // XOR cookie ^ random = retguard cookie
      BuildMI(MBB, MI, MBBDL, TII->get(XOR), REG)
        .addReg(REG)
        .addReg(LRReg);
    }
  }
}

void PPCReturnProtectorLowering::insertReturnProtectorEpilogue(
    MachineFunction &MF, MachineInstr &MI, GlobalVariable *cookie) const {

  MachineBasicBlock &MBB = *MI.getParent();
  DebugLoc MBBDL = MI.getDebugLoc();
  const TargetInstrInfo *TII = MF.getSubtarget().getInstrInfo();
  const TargetMachine &TM = MF.getTarget();
  unsigned REG = MF.getFrameInfo().getReturnProtectorRegister();
  bool is64bit = MF.getSubtarget<PPCSubtarget>().isPPC64();
  bool isELFv2 = MF.getSubtarget<PPCSubtarget>().isELFv2ABI();

  unsigned LRReg = PPC::R0;
  unsigned TOCReg = PPC::R2;
  unsigned RGReg = PPC::R12;
  unsigned TRAP = PPC::TW;
  unsigned XOR = PPC::XOR;
  unsigned LWZ = PPC::LWZ;
  unsigned MFLR = PPC::MFLR;
  if (is64bit) {
    LRReg = PPC::X0;
    TOCReg = PPC::X2;
    RGReg = PPC::X12;
    TRAP = PPC::TD;
    XOR = PPC::XOR8;
    LWZ = PPC::LWZ8;
    MFLR = PPC::MFLR8;
  }

  if (!MBB.isLiveIn(LRReg))
    MBB.addLiveIn(LRReg);
  if (!MBB.isLiveIn(RGReg))
    MBB.addLiveIn(RGReg);

  if (is64bit) {
    // PIC and non-PIC is the same
    if (!isELFv2)
      llvm_unreachable("ppc64 retguard requires ELFv2");
    // Get the return address into LRReg
    BuildMI(MBB, MI, MBBDL, TII->get(MFLR), LRReg);
    // XOR the LRReg with the retguard cookie value
    BuildMI(MBB, MI, MBBDL, TII->get(XOR), REG)
      .addReg(REG)
      .addReg(LRReg);
    // Load the random cookie value into RGReg
    BuildMI(MBB, MI, MBBDL, TII->get(PPC::ADDIStocHA8), RGReg)
      .addReg(TOCReg)
      .addGlobalAddress(cookie);
    BuildMI(MBB, MI, MBBDL, TII->get(PPC::LD), RGReg)
      .addGlobalAddress(cookie, 0, PPCII::MO_TOC_LO)
      .addReg(RGReg);
    // Trap if they don't compare
    BuildMI(MBB, MI, MBBDL, TII->get(TRAP))
      .addImm(24)
      .addReg(REG)
      .addReg(RGReg);
  } else {
    // 32 bit
    if (TM.isPositionIndependent()) {
      // Get the PC into RGReg and the LR value into LRReg
      MCSymbol *HereSym = MF.getContext().createTempSymbol();
      BuildMI(MBB, MI, MBBDL, TII->get(PPC::RETGUARD_LOAD_PC), RGReg)
        .addReg(LRReg, RegState::Define)
        .addSym(HereSym);
      // XOR the LRReg with the retguard cookie value
      BuildMI(MBB, MI, MBBDL, TII->get(XOR), REG)
        .addReg(REG)
        .addReg(LRReg);
      // Get the random cookie address into RGReg
      BuildMI(MBB, MI, MBBDL, TII->get(PPC::RETGUARD_LOAD_GOT), RGReg)
        .addReg(RGReg)
        .addSym(HereSym);
      BuildMI(MBB, MI, MBBDL, TII->get(PPC::LWZtoc), RGReg)
        .addGlobalAddress(cookie, 0, 0)
        .addReg(RGReg);
      // Load the cookie random balue
      BuildMI(MBB, MI, MBBDL, TII->get(PPC::LWZ), RGReg)
        .addImm(0)
        .addReg(RGReg);
      // Trap if they don't compare
      BuildMI(MBB, MI, MBBDL, TII->get(TRAP))
        .addImm(24)
        .addReg(REG)
        .addReg(RGReg);
    } else {
      // Get LR into a register
      BuildMI(MBB, MI, MBBDL, TII->get(MFLR), LRReg);
      // XOR the LR Reg with the retguard cookie value
      BuildMI(MBB, MI, MBBDL, TII->get(XOR), REG)
        .addReg(REG)
        .addReg(LRReg);
      BuildMI(MBB, MI, MBBDL, TII->get(PPC::RETGUARD_LOAD_COOKIE), RGReg)
        .addGlobalAddress(cookie);
      // Trap if they don't compare
      BuildMI(MBB, MI, MBBDL, TII->get(TRAP))
        .addImm(24)
        .addReg(REG)
        .addReg(RGReg);
    }
  }
}

bool PPCReturnProtectorLowering::opcodeIsReturn(unsigned opcode) const {
  switch (opcode) {
    case PPC::BLR:
    case PPC::BCCLR:
    case PPC::BCLR:
    case PPC::BCLRn:
    case PPC::BDZLR:
    case PPC::BDNZLR:
    case PPC::BDZLRp:
    case PPC::BDNZLRp:
    case PPC::BDZLRm:
    case PPC::BDNZLRm:
    case PPC::BLR8:
    case PPC::BDZLR8:
    case PPC::BDNZLR8:
      return true;
    default:
      return false;
  }
}

void PPCReturnProtectorLowering::fillTempRegisters(
    MachineFunction &MF, std::vector<unsigned> &TempRegs) const {

  const Function &F = MF.getFunction();

  bool is64bit = MF.getSubtarget<PPCSubtarget>().isPPC64();

  // R0/R12 are also the hardcoded temp regs for the rest of
  // frame lowering, so leave them alone.
  //TempRegs.push_back(is64bit ? PPC::X0  : PPC::R0);
  //TempRegs.push_back(is64bit ? PPC::X12 : PPC::R12);
  // X11 is also the 'nest' param or environment pointer
  TempRegs.push_back(is64bit ? PPC::X11 : PPC::R11);

  // We can use any of the caller saved unused arg registers.
  // X3/R3 and X4/R4 are used to return.
  const MCPhysReg Args64[] = {PPC::X5, PPC::X6, PPC::X7,
                              PPC::X8, PPC::X9, PPC::X10};
  const MCPhysReg Args32[] = {PPC::R5, PPC::R6, PPC::R7,
                              PPC::R8, PPC::R9, PPC::R10};
  MachineRegisterInfo &MRI = MF.getRegInfo();
  for (int i = 0; i < 6; ++i) {
    MCPhysReg Reg = is64bit ? Args64[i] : Args32[i];
    if (!MRI.isLiveIn(Reg))
      TempRegs.push_back(Reg);
  }
}
