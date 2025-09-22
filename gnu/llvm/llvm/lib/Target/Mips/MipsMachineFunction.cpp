//===-- MipsMachineFunctionInfo.cpp - Private data used for Mips ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MipsMachineFunction.h"
#include "MCTargetDesc/MipsABIInfo.h"
#include "MipsSubtarget.h"
#include "MipsTargetMachine.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/PseudoSourceValueManager.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<bool>
FixGlobalBaseReg("mips-fix-global-base-reg", cl::Hidden, cl::init(true),
                 cl::desc("Always use $gp as the global base register."));

MachineFunctionInfo *
MipsFunctionInfo::clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
                        const DenseMap<MachineBasicBlock *, MachineBasicBlock *>
                            &Src2DstMBB) const {
  return DestMF.cloneInfo<MipsFunctionInfo>(*this);
}

MipsFunctionInfo::~MipsFunctionInfo() = default;

bool MipsFunctionInfo::globalBaseRegSet() const {
  return GlobalBaseReg;
}

static const TargetRegisterClass &getGlobalBaseRegClass(MachineFunction &MF) {
  auto &STI = MF.getSubtarget<MipsSubtarget>();
  auto &TM = static_cast<const MipsTargetMachine &>(MF.getTarget());

  if (STI.inMips16Mode())
    return Mips::CPU16RegsRegClass;

  if (STI.inMicroMipsMode())
    return Mips::GPRMM16RegClass;

  if (TM.getABI().IsN64())
    return Mips::GPR64RegClass;

  return Mips::GPR32RegClass;
}

Register MipsFunctionInfo::getGlobalBaseReg(MachineFunction &MF) {
  if (!GlobalBaseReg)
    GlobalBaseReg =
        MF.getRegInfo().createVirtualRegister(&getGlobalBaseRegClass(MF));
  return GlobalBaseReg;
}

Register MipsFunctionInfo::getGlobalBaseRegForGlobalISel(MachineFunction &MF) {
  if (!GlobalBaseReg) {
    getGlobalBaseReg(MF);
    initGlobalBaseReg(MF);
  }
  return GlobalBaseReg;
}

void MipsFunctionInfo::initGlobalBaseReg(MachineFunction &MF) {
  if (!GlobalBaseReg)
    return;

  MachineBasicBlock &MBB = MF.front();
  MachineBasicBlock::iterator I = MBB.begin();
  MachineRegisterInfo &RegInfo = MF.getRegInfo();
  const TargetInstrInfo &TII = *MF.getSubtarget().getInstrInfo();
  DebugLoc DL;
  const TargetRegisterClass *RC;
  const MipsABIInfo &ABI =
      static_cast<const MipsTargetMachine &>(MF.getTarget()).getABI();
  RC = (ABI.IsN64()) ? &Mips::GPR64RegClass : &Mips::GPR32RegClass;

  Register V0 = RegInfo.createVirtualRegister(RC);
  Register V1 = RegInfo.createVirtualRegister(RC);

  if (ABI.IsN64()) {
    MF.getRegInfo().addLiveIn(Mips::T9_64);
    MBB.addLiveIn(Mips::T9_64);

    // lui $v0, %hi(%neg(%gp_rel(fname)))
    // daddu $v1, $v0, $t9
    // daddiu $globalbasereg, $v1, %lo(%neg(%gp_rel(fname)))
    const GlobalValue *FName = &MF.getFunction();
    BuildMI(MBB, I, DL, TII.get(Mips::LUi64), V0)
        .addGlobalAddress(FName, 0, MipsII::MO_GPOFF_HI);
    BuildMI(MBB, I, DL, TII.get(Mips::DADDu), V1).addReg(V0)
        .addReg(Mips::T9_64);
    BuildMI(MBB, I, DL, TII.get(Mips::DADDiu), GlobalBaseReg).addReg(V1)
        .addGlobalAddress(FName, 0, MipsII::MO_GPOFF_LO);
    return;
  }

  if (!MF.getTarget().isPositionIndependent()) {
    // Set global register to __gnu_local_gp.
    //
    // lui   $v0, %hi(__gnu_local_gp)
    // addiu $globalbasereg, $v0, %lo(__gnu_local_gp)
    BuildMI(MBB, I, DL, TII.get(Mips::LUi), V0)
        .addExternalSymbol("__gnu_local_gp", MipsII::MO_ABS_HI);
    BuildMI(MBB, I, DL, TII.get(Mips::ADDiu), GlobalBaseReg).addReg(V0)
        .addExternalSymbol("__gnu_local_gp", MipsII::MO_ABS_LO);
    return;
  }

  MF.getRegInfo().addLiveIn(Mips::T9);
  MBB.addLiveIn(Mips::T9);

  if (ABI.IsN32()) {
    // lui $v0, %hi(%neg(%gp_rel(fname)))
    // addu $v1, $v0, $t9
    // addiu $globalbasereg, $v1, %lo(%neg(%gp_rel(fname)))
    const GlobalValue *FName = &MF.getFunction();
    BuildMI(MBB, I, DL, TII.get(Mips::LUi), V0)
        .addGlobalAddress(FName, 0, MipsII::MO_GPOFF_HI);
    BuildMI(MBB, I, DL, TII.get(Mips::ADDu), V1).addReg(V0).addReg(Mips::T9);
    BuildMI(MBB, I, DL, TII.get(Mips::ADDiu), GlobalBaseReg).addReg(V1)
        .addGlobalAddress(FName, 0, MipsII::MO_GPOFF_LO);
    return;
  }

  assert(ABI.IsO32());

  // For O32 ABI, the following instruction sequence is emitted to initialize
  // the global base register:
  //
  //  0. lui   $2, %hi(_gp_disp)
  //  1. addiu $2, $2, %lo(_gp_disp)
  //  2. addu  $globalbasereg, $2, $t9
  //
  // We emit only the last instruction here.
  //
  // GNU linker requires that the first two instructions appear at the beginning
  // of a function and no instructions be inserted before or between them.
  // The two instructions are emitted during lowering to MC layer in order to
  // avoid any reordering.
  //
  // Register $2 (Mips::V0) is added to the list of live-in registers to ensure
  // the value instruction 1 (addiu) defines is valid when instruction 2 (addu)
  // reads it.
  MF.getRegInfo().addLiveIn(Mips::V0);
  MBB.addLiveIn(Mips::V0);
  BuildMI(MBB, I, DL, TII.get(Mips::ADDu), GlobalBaseReg)
      .addReg(Mips::V0).addReg(Mips::T9);
}

void MipsFunctionInfo::createEhDataRegsFI(MachineFunction &MF) {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  for (int &I : EhDataRegFI) {
    const TargetRegisterClass &RC =
        static_cast<const MipsTargetMachine &>(MF.getTarget()).getABI().IsN64()
            ? Mips::GPR64RegClass
            : Mips::GPR32RegClass;

    I = MF.getFrameInfo().CreateStackObject(TRI.getSpillSize(RC),
                                            TRI.getSpillAlign(RC), false);
  }
}

void MipsFunctionInfo::createISRRegFI(MachineFunction &MF) {
  // ISRs require spill slots for Status & ErrorPC Coprocessor 0 registers.
  // The current implementation only supports Mips32r2+ not Mips64rX. Status
  // is always 32 bits, ErrorPC is 32 or 64 bits dependent on architecture,
  // however Mips32r2+ is the supported architecture.
  const TargetRegisterClass &RC = Mips::GPR32RegClass;
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();

  for (int &I : ISRDataRegFI)
    I = MF.getFrameInfo().CreateStackObject(TRI.getSpillSize(RC),
                                            TRI.getSpillAlign(RC), false);
}

bool MipsFunctionInfo::isEhDataRegFI(int FI) const {
  return CallsEhReturn && (FI == EhDataRegFI[0] || FI == EhDataRegFI[1]
                        || FI == EhDataRegFI[2] || FI == EhDataRegFI[3]);
}

bool MipsFunctionInfo::isISRRegFI(int FI) const {
  return IsISR && (FI == ISRDataRegFI[0] || FI == ISRDataRegFI[1]);
}
MachinePointerInfo MipsFunctionInfo::callPtrInfo(MachineFunction &MF,
                                                 const char *ES) {
  return MachinePointerInfo(MF.getPSVManager().getExternalSymbolCallEntry(ES));
}

MachinePointerInfo MipsFunctionInfo::callPtrInfo(MachineFunction &MF,
                                                 const GlobalValue *GV) {
  return MachinePointerInfo(MF.getPSVManager().getGlobalValueCallEntry(GV));
}

int MipsFunctionInfo::getMoveF64ViaSpillFI(MachineFunction &MF,
                                           const TargetRegisterClass *RC) {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  if (MoveF64ViaSpillFI == -1) {
    MoveF64ViaSpillFI = MF.getFrameInfo().CreateStackObject(
        TRI.getSpillSize(*RC), TRI.getSpillAlign(*RC), false);
  }
  return MoveF64ViaSpillFI;
}

void MipsFunctionInfo::anchor() {}
