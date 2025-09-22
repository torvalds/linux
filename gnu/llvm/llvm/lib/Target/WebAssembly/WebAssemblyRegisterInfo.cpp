//===-- WebAssemblyRegisterInfo.cpp - WebAssembly Register Information ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the WebAssembly implementation of the
/// TargetRegisterInfo class.
///
//===----------------------------------------------------------------------===//

#include "WebAssemblyRegisterInfo.h"
#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssemblyFrameLowering.h"
#include "WebAssemblyInstrInfo.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "WebAssemblySubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetOptions.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-reg-info"

#define GET_REGINFO_TARGET_DESC
#include "WebAssemblyGenRegisterInfo.inc"

WebAssemblyRegisterInfo::WebAssemblyRegisterInfo(const Triple &TT)
    : WebAssemblyGenRegisterInfo(0), TT(TT) {}

const MCPhysReg *
WebAssemblyRegisterInfo::getCalleeSavedRegs(const MachineFunction *) const {
  static const MCPhysReg CalleeSavedRegs[] = {0};
  return CalleeSavedRegs;
}

BitVector
WebAssemblyRegisterInfo::getReservedRegs(const MachineFunction & /*MF*/) const {
  BitVector Reserved(getNumRegs());
  for (auto Reg : {WebAssembly::SP32, WebAssembly::SP64, WebAssembly::FP32,
                   WebAssembly::FP64})
    Reserved.set(Reg);
  return Reserved;
}

bool WebAssemblyRegisterInfo::eliminateFrameIndex(
    MachineBasicBlock::iterator II, int SPAdj, unsigned FIOperandNum,
    RegScavenger * /*RS*/) const {
  assert(SPAdj == 0);
  MachineInstr &MI = *II;

  MachineBasicBlock &MBB = *MI.getParent();
  MachineFunction &MF = *MBB.getParent();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  int FrameIndex = MI.getOperand(FIOperandNum).getIndex();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  int64_t FrameOffset = MFI.getStackSize() + MFI.getObjectOffset(FrameIndex);

  assert(MFI.getObjectSize(FrameIndex) != 0 &&
         "We assume that variable-sized objects have already been lowered, "
         "and don't use FrameIndex operands.");
  Register FrameRegister = getFrameRegister(MF);

  // If this is the address operand of a load or store, make it relative to SP
  // and fold the frame offset directly in.
  unsigned AddrOperandNum = WebAssembly::getNamedOperandIdx(
      MI.getOpcode(), WebAssembly::OpName::addr);
  if (AddrOperandNum == FIOperandNum) {
    unsigned OffsetOperandNum = WebAssembly::getNamedOperandIdx(
        MI.getOpcode(), WebAssembly::OpName::off);
    assert(FrameOffset >= 0 && MI.getOperand(OffsetOperandNum).getImm() >= 0);
    int64_t Offset = MI.getOperand(OffsetOperandNum).getImm() + FrameOffset;

    if (static_cast<uint64_t>(Offset) <= std::numeric_limits<uint32_t>::max()) {
      MI.getOperand(OffsetOperandNum).setImm(Offset);
      MI.getOperand(FIOperandNum)
          .ChangeToRegister(FrameRegister, /*isDef=*/false);
      return false;
    }
  }

  // If this is an address being added to a constant, fold the frame offset
  // into the constant.
  if (MI.getOpcode() == WebAssemblyFrameLowering::getOpcAdd(MF)) {
    MachineOperand &OtherMO = MI.getOperand(3 - FIOperandNum);
    if (OtherMO.isReg()) {
      Register OtherMOReg = OtherMO.getReg();
      if (OtherMOReg.isVirtual()) {
        MachineInstr *Def = MF.getRegInfo().getUniqueVRegDef(OtherMOReg);
        // TODO: For now we just opportunistically do this in the case where
        // the CONST_I32/64 happens to have exactly one def and one use. We
        // should generalize this to optimize in more cases.
        if (Def && Def->getOpcode() ==
              WebAssemblyFrameLowering::getOpcConst(MF) &&
            MRI.hasOneNonDBGUse(Def->getOperand(0).getReg())) {
          MachineOperand &ImmMO = Def->getOperand(1);
          if (ImmMO.isImm()) {
            ImmMO.setImm(ImmMO.getImm() + uint32_t(FrameOffset));
            MI.getOperand(FIOperandNum)
                .ChangeToRegister(FrameRegister, /*isDef=*/false);
            return false;
          }
        }
      }
    }
  }

  // Otherwise create an i32/64.add SP, offset and make it the operand.
  const auto *TII = MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();

  unsigned FIRegOperand = FrameRegister;
  if (FrameOffset) {
    // Create i32/64.add SP, offset and make it the operand.
    const TargetRegisterClass *PtrRC =
        MRI.getTargetRegisterInfo()->getPointerRegClass(MF);
    Register OffsetOp = MRI.createVirtualRegister(PtrRC);
    BuildMI(MBB, *II, II->getDebugLoc(),
            TII->get(WebAssemblyFrameLowering::getOpcConst(MF)),
            OffsetOp)
        .addImm(FrameOffset);
    FIRegOperand = MRI.createVirtualRegister(PtrRC);
    BuildMI(MBB, *II, II->getDebugLoc(),
            TII->get(WebAssemblyFrameLowering::getOpcAdd(MF)),
            FIRegOperand)
        .addReg(FrameRegister)
        .addReg(OffsetOp);
  }
  MI.getOperand(FIOperandNum).ChangeToRegister(FIRegOperand, /*isDef=*/false);
  return false;
}

Register
WebAssemblyRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  // If the PReg has been replaced by a VReg, return that.
  const auto &MFI = MF.getInfo<WebAssemblyFunctionInfo>();
  if (MFI->isFrameBaseVirtual())
    return MFI->getFrameBaseVreg();
  static const unsigned Regs[2][2] = {
      /*            !isArch64Bit       isArch64Bit      */
      /* !hasFP */ {WebAssembly::SP32, WebAssembly::SP64},
      /*  hasFP */ {WebAssembly::FP32, WebAssembly::FP64}};
  const WebAssemblyFrameLowering *TFI = getFrameLowering(MF);
  return Regs[TFI->hasFP(MF)][TT.isArch64Bit()];
}

const TargetRegisterClass *
WebAssemblyRegisterInfo::getPointerRegClass(const MachineFunction &MF,
                                            unsigned Kind) const {
  assert(Kind == 0 && "Only one kind of pointer on WebAssembly");
  if (MF.getSubtarget<WebAssemblySubtarget>().hasAddr64())
    return &WebAssembly::I64RegClass;
  return &WebAssembly::I32RegClass;
}
