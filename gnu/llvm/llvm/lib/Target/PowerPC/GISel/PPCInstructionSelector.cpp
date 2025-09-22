//===- PPCInstructionSelector.cpp --------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file implements the targeting of the InstructionSelector class for
/// PowerPC.
//===----------------------------------------------------------------------===//

#include "PPC.h"
#include "PPCInstrInfo.h"
#include "PPCMachineFunctionInfo.h"
#include "PPCRegisterBankInfo.h"
#include "PPCSubtarget.h"
#include "PPCTargetMachine.h"
#include "llvm/CodeGen/GlobalISel/GIMatchTableExecutorImpl.h"
#include "llvm/CodeGen/GlobalISel/GenericMachineInstrs.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/IR/IntrinsicsPowerPC.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "ppc-gisel"

using namespace llvm;

namespace {

#define GET_GLOBALISEL_PREDICATE_BITSET
#include "PPCGenGlobalISel.inc"
#undef GET_GLOBALISEL_PREDICATE_BITSET

class PPCInstructionSelector : public InstructionSelector {
public:
  PPCInstructionSelector(const PPCTargetMachine &TM, const PPCSubtarget &STI,
                         const PPCRegisterBankInfo &RBI);

  bool select(MachineInstr &I) override;
  static const char *getName() { return DEBUG_TYPE; }

private:
  /// tblgen generated 'select' implementation that is used as the initial
  /// selector for the patterns that do not require complex C++.
  bool selectImpl(MachineInstr &I, CodeGenCoverage &CoverageInfo) const;

  bool selectFPToInt(MachineInstr &I, MachineBasicBlock &MBB,
                  MachineRegisterInfo &MRI) const;
  bool selectIntToFP(MachineInstr &I, MachineBasicBlock &MBB,
                  MachineRegisterInfo &MRI) const;

  bool selectZExt(MachineInstr &I, MachineBasicBlock &MBB,
                  MachineRegisterInfo &MRI) const;
  bool selectConstantPool(MachineInstr &I, MachineBasicBlock &MBB,
                          MachineRegisterInfo &MRI) const;

  std::optional<bool> selectI64ImmDirect(MachineInstr &I,
                                         MachineBasicBlock &MBB,
                                         MachineRegisterInfo &MRI, Register Reg,
                                         uint64_t Imm) const;
  bool selectI64Imm(MachineInstr &I, MachineBasicBlock &MBB,
                    MachineRegisterInfo &MRI) const;

  const PPCTargetMachine &TM;
  const PPCSubtarget &STI;
  const PPCInstrInfo &TII;
  const PPCRegisterInfo &TRI;
  const PPCRegisterBankInfo &RBI;

#define GET_GLOBALISEL_PREDICATES_DECL
#include "PPCGenGlobalISel.inc"
#undef GET_GLOBALISEL_PREDICATES_DECL

#define GET_GLOBALISEL_TEMPORARIES_DECL
#include "PPCGenGlobalISel.inc"
#undef GET_GLOBALISEL_TEMPORARIES_DECL
};

} // end anonymous namespace

#define GET_GLOBALISEL_IMPL
#include "PPCGenGlobalISel.inc"
#undef GET_GLOBALISEL_IMPL

PPCInstructionSelector::PPCInstructionSelector(const PPCTargetMachine &TM,
                                               const PPCSubtarget &STI,
                                               const PPCRegisterBankInfo &RBI)
    : TM(TM), STI(STI), TII(*STI.getInstrInfo()), TRI(*STI.getRegisterInfo()),
      RBI(RBI),
#define GET_GLOBALISEL_PREDICATES_INIT
#include "PPCGenGlobalISel.inc"
#undef GET_GLOBALISEL_PREDICATES_INIT
#define GET_GLOBALISEL_TEMPORARIES_INIT
#include "PPCGenGlobalISel.inc"
#undef GET_GLOBALISEL_TEMPORARIES_INIT
{
}

static const TargetRegisterClass *getRegClass(LLT Ty, const RegisterBank *RB) {
  if (RB->getID() == PPC::GPRRegBankID) {
    if (Ty.getSizeInBits() == 64)
      return &PPC::G8RCRegClass;
    if (Ty.getSizeInBits() <= 32)
      return &PPC::GPRCRegClass;
  }
  if (RB->getID() == PPC::FPRRegBankID) {
    if (Ty.getSizeInBits() == 32)
      return &PPC::F4RCRegClass;
    if (Ty.getSizeInBits() == 64)
      return &PPC::F8RCRegClass;
  }
  if (RB->getID() == PPC::VECRegBankID) {
    if (Ty.getSizeInBits() == 128)
      return &PPC::VSRCRegClass;
  }
  if (RB->getID() == PPC::CRRegBankID) {
    if (Ty.getSizeInBits() == 1)
      return &PPC::CRBITRCRegClass;
    if (Ty.getSizeInBits() == 4)
      return &PPC::CRRCRegClass;
  }

  llvm_unreachable("Unknown RegBank!");
}

static bool selectCopy(MachineInstr &I, const TargetInstrInfo &TII,
                       MachineRegisterInfo &MRI, const TargetRegisterInfo &TRI,
                       const RegisterBankInfo &RBI) {
  Register DstReg = I.getOperand(0).getReg();

  if (DstReg.isPhysical())
    return true;

  const RegisterBank *DstRegBank = RBI.getRegBank(DstReg, MRI, TRI);
  const TargetRegisterClass *DstRC =
      getRegClass(MRI.getType(DstReg), DstRegBank);

  // No need to constrain SrcReg. It will get constrained when we hit another of
  // its use or its defs.
  // Copies do not have constraints.
  if (!RBI.constrainGenericRegister(DstReg, *DstRC, MRI)) {
    LLVM_DEBUG(dbgs() << "Failed to constrain " << TII.getName(I.getOpcode())
                      << " operand\n");
    return false;
  }

  return true;
}

static unsigned selectLoadStoreOp(unsigned GenericOpc, unsigned RegBankID,
                                  unsigned OpSize) {
  const bool IsStore = GenericOpc == TargetOpcode::G_STORE;
  switch (RegBankID) {
  case PPC::GPRRegBankID:
    switch (OpSize) {
    case 32:
      return IsStore ? PPC::STW : PPC::LWZ;
    case 64:
      return IsStore ? PPC::STD : PPC::LD;
    default:
      llvm_unreachable("Unexpected size!");
    }
    break;
  case PPC::FPRRegBankID:
    switch (OpSize) {
    case 32:
      return IsStore ? PPC::STFS : PPC::LFS;
    case 64:
      return IsStore ? PPC::STFD : PPC::LFD;
    default:
      llvm_unreachable("Unexpected size!");
    }
    break;
  default:
    llvm_unreachable("Unexpected register bank!");
  }
  return GenericOpc;
}

bool PPCInstructionSelector::selectIntToFP(MachineInstr &I,
                                           MachineBasicBlock &MBB,
                                           MachineRegisterInfo &MRI) const {
  if (!STI.hasDirectMove() || !STI.isPPC64() || !STI.hasFPCVT())
    return false;

  const DebugLoc &DbgLoc = I.getDebugLoc();
  const Register DstReg = I.getOperand(0).getReg();
  const Register SrcReg = I.getOperand(1).getReg();

  Register MoveReg = MRI.createVirtualRegister(&PPC::VSFRCRegClass);

  // For now, only handle the case for 64 bit integer.
  BuildMI(MBB, I, DbgLoc, TII.get(PPC::MTVSRD), MoveReg).addReg(SrcReg);

  bool IsSingle = MRI.getType(DstReg).getSizeInBits() == 32;
  bool IsSigned = I.getOpcode() == TargetOpcode::G_SITOFP;
  unsigned ConvOp = IsSingle ? (IsSigned ? PPC::XSCVSXDSP : PPC::XSCVUXDSP)
                             : (IsSigned ? PPC::XSCVSXDDP : PPC::XSCVUXDDP);

  MachineInstr *MI =
      BuildMI(MBB, I, DbgLoc, TII.get(ConvOp), DstReg).addReg(MoveReg);

  I.eraseFromParent();
  return constrainSelectedInstRegOperands(*MI, TII, TRI, RBI);
}

bool PPCInstructionSelector::selectFPToInt(MachineInstr &I,
                                           MachineBasicBlock &MBB,
                                           MachineRegisterInfo &MRI) const {
  if (!STI.hasDirectMove() || !STI.isPPC64() || !STI.hasFPCVT())
    return false;

  const DebugLoc &DbgLoc = I.getDebugLoc();
  const Register DstReg = I.getOperand(0).getReg();
  const Register SrcReg = I.getOperand(1).getReg();

  Register CopyReg = MRI.createVirtualRegister(&PPC::VSFRCRegClass);
  BuildMI(MBB, I, DbgLoc, TII.get(TargetOpcode::COPY), CopyReg).addReg(SrcReg);

  Register ConvReg = MRI.createVirtualRegister(&PPC::VSFRCRegClass);

  bool IsSigned = I.getOpcode() == TargetOpcode::G_FPTOSI;

  // single-precision is stored as double-precision on PPC in registers, so
  // always use double-precision convertions.
  unsigned ConvOp = IsSigned ? PPC::XSCVDPSXDS : PPC::XSCVDPUXDS;

  BuildMI(MBB, I, DbgLoc, TII.get(ConvOp), ConvReg).addReg(CopyReg);

  MachineInstr *MI =
      BuildMI(MBB, I, DbgLoc, TII.get(PPC::MFVSRD), DstReg).addReg(ConvReg);

  I.eraseFromParent();
  return constrainSelectedInstRegOperands(*MI, TII, TRI, RBI);
}

bool PPCInstructionSelector::selectZExt(MachineInstr &I, MachineBasicBlock &MBB,
                                        MachineRegisterInfo &MRI) const {
  const Register DstReg = I.getOperand(0).getReg();
  const LLT DstTy = MRI.getType(DstReg);
  const RegisterBank *DstRegBank = RBI.getRegBank(DstReg, MRI, TRI);

  const Register SrcReg = I.getOperand(1).getReg();

  assert(DstTy.getSizeInBits() == 64 && "Unexpected dest size!");
  assert(MRI.getType(SrcReg).getSizeInBits() == 32 && "Unexpected src size!");

  Register ImpDefReg =
      MRI.createVirtualRegister(getRegClass(DstTy, DstRegBank));
  BuildMI(MBB, I, I.getDebugLoc(), TII.get(TargetOpcode::IMPLICIT_DEF),
          ImpDefReg);

  Register NewDefReg =
      MRI.createVirtualRegister(getRegClass(DstTy, DstRegBank));
  BuildMI(MBB, I, I.getDebugLoc(), TII.get(TargetOpcode::INSERT_SUBREG),
          NewDefReg)
      .addReg(ImpDefReg)
      .addReg(SrcReg)
      .addImm(PPC::sub_32);

  MachineInstr *MI =
      BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::RLDICL), DstReg)
          .addReg(NewDefReg)
          .addImm(0)
          .addImm(32);

  I.eraseFromParent();
  return constrainSelectedInstRegOperands(*MI, TII, TRI, RBI);
}

// For any 32 < Num < 64, check if the Imm contains at least Num consecutive
// zeros and return the number of bits by the left of these consecutive zeros.
static uint32_t findContiguousZerosAtLeast(uint64_t Imm, unsigned Num) {
  uint32_t HiTZ = llvm::countr_zero<uint32_t>(Hi_32(Imm));
  uint32_t LoLZ = llvm::countl_zero<uint32_t>(Lo_32(Imm));
  if ((HiTZ + LoLZ) >= Num)
    return (32 + HiTZ);
  return 0;
}

// Direct materialization of 64-bit constants by enumerated patterns.
// Similar to PPCISelDAGToDAG::selectI64ImmDirect().
std::optional<bool> PPCInstructionSelector::selectI64ImmDirect(MachineInstr &I,
                                                MachineBasicBlock &MBB,
                                                MachineRegisterInfo &MRI,
                                                Register Reg,
                                                uint64_t Imm) const {
  unsigned TZ = llvm::countr_zero<uint64_t>(Imm);
  unsigned LZ = llvm::countl_zero<uint64_t>(Imm);
  unsigned TO = llvm::countr_one<uint64_t>(Imm);
  unsigned LO = llvm::countl_one<uint64_t>(Imm);
  uint32_t Hi32 = Hi_32(Imm);
  uint32_t Lo32 = Lo_32(Imm);
  uint32_t Shift = 0;

  // Following patterns use 1 instructions to materialize the Imm.

  // 1-1) Patterns : {zeros}{15-bit valve}
  //                 {ones}{15-bit valve}
  if (isInt<16>(Imm))
    return BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::LI8), Reg)
        .addImm(Imm)
        .constrainAllUses(TII, TRI, RBI);
  // 1-2) Patterns : {zeros}{15-bit valve}{16 zeros}
  //                 {ones}{15-bit valve}{16 zeros}
  if (TZ > 15 && (LZ > 32 || LO > 32))
    return BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::LIS8), Reg)
        .addImm((Imm >> 16) & 0xffff)
        .constrainAllUses(TII, TRI, RBI);

  // Following patterns use 2 instructions to materialize the Imm.

  assert(LZ < 64 && "Unexpected leading zeros here.");
  // Count of ones follwing the leading zeros.
  unsigned FO = llvm::countl_one<uint64_t>(Imm << LZ);
  // 2-1) Patterns : {zeros}{31-bit value}
  //                 {ones}{31-bit value}
  if (isInt<32>(Imm)) {
    uint64_t ImmHi16 = (Imm >> 16) & 0xffff;
    unsigned Opcode = ImmHi16 ? PPC::LIS8 : PPC::LI8;
    Register TmpReg = MRI.createVirtualRegister(&PPC::G8RCRegClass);
    if (!BuildMI(MBB, I, I.getDebugLoc(), TII.get(Opcode), TmpReg)
             .addImm((Imm >> 16) & 0xffff)
             .constrainAllUses(TII, TRI, RBI))
      return false;
    return BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::ORI8), Reg)
        .addReg(TmpReg, RegState::Kill)
        .addImm(Imm & 0xffff)
        .constrainAllUses(TII, TRI, RBI);
  }
  // 2-2) Patterns : {zeros}{ones}{15-bit value}{zeros}
  //                 {zeros}{15-bit value}{zeros}
  //                 {zeros}{ones}{15-bit value}
  //                 {ones}{15-bit value}{zeros}
  // We can take advantage of LI's sign-extension semantics to generate leading
  // ones, and then use RLDIC to mask off the ones in both sides after rotation.
  if ((LZ + FO + TZ) > 48) {
    Register TmpReg = MRI.createVirtualRegister(&PPC::G8RCRegClass);
    if (!BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::LI8), TmpReg)
             .addImm((Imm >> TZ) & 0xffff)
             .constrainAllUses(TII, TRI, RBI))
      return false;
    return BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::RLDIC), Reg)
        .addReg(TmpReg, RegState::Kill)
        .addImm(TZ)
        .addImm(LZ)
        .constrainAllUses(TII, TRI, RBI);
  }
  // 2-3) Pattern : {zeros}{15-bit value}{ones}
  // Shift right the Imm by (48 - LZ) bits to construct a negtive 16 bits value,
  // therefore we can take advantage of LI's sign-extension semantics, and then
  // mask them off after rotation.
  //
  // +--LZ--||-15-bit-||--TO--+     +-------------|--16-bit--+
  // |00000001bbbbbbbbb1111111| ->  |00000000000001bbbbbbbbb1|
  // +------------------------+     +------------------------+
  // 63                      0      63                      0
  //          Imm                   (Imm >> (48 - LZ) & 0xffff)
  // +----sext-----|--16-bit--+     +clear-|-----------------+
  // |11111111111111bbbbbbbbb1| ->  |00000001bbbbbbbbb1111111|
  // +------------------------+     +------------------------+
  // 63                      0      63                      0
  // LI8: sext many leading zeros   RLDICL: rotate left (48 - LZ), clear left LZ
  if ((LZ + TO) > 48) {
    // Since the immediates with (LZ > 32) have been handled by previous
    // patterns, here we have (LZ <= 32) to make sure we will not shift right
    // the Imm by a negative value.
    assert(LZ <= 32 && "Unexpected shift value.");
    Register TmpReg = MRI.createVirtualRegister(&PPC::G8RCRegClass);
    if (!BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::LI8), TmpReg)
             .addImm(Imm >> (48 - LZ) & 0xffff)
             .constrainAllUses(TII, TRI, RBI))
      return false;
    return BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::RLDICL), Reg)
        .addReg(TmpReg, RegState::Kill)
        .addImm(48 - LZ)
        .addImm(LZ)
        .constrainAllUses(TII, TRI, RBI);
  }
  // 2-4) Patterns : {zeros}{ones}{15-bit value}{ones}
  //                 {ones}{15-bit value}{ones}
  // We can take advantage of LI's sign-extension semantics to generate leading
  // ones, and then use RLDICL to mask off the ones in left sides (if required)
  // after rotation.
  //
  // +-LZ-FO||-15-bit-||--TO--+     +-------------|--16-bit--+
  // |00011110bbbbbbbbb1111111| ->  |000000000011110bbbbbbbbb|
  // +------------------------+     +------------------------+
  // 63                      0      63                      0
  //            Imm                    (Imm >> TO) & 0xffff
  // +----sext-----|--16-bit--+     +LZ|---------------------+
  // |111111111111110bbbbbbbbb| ->  |00011110bbbbbbbbb1111111|
  // +------------------------+     +------------------------+
  // 63                      0      63                      0
  // LI8: sext many leading zeros   RLDICL: rotate left TO, clear left LZ
  if ((LZ + FO + TO) > 48) {
    Register TmpReg = MRI.createVirtualRegister(&PPC::G8RCRegClass);
    if (!BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::LI8), TmpReg)
             .addImm((Imm >> TO) & 0xffff)
             .constrainAllUses(TII, TRI, RBI))
      return false;
    return BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::RLDICL), Reg)
        .addReg(TmpReg, RegState::Kill)
        .addImm(TO)
        .addImm(LZ)
        .constrainAllUses(TII, TRI, RBI);
  }
  // 2-5) Pattern : {32 zeros}{****}{0}{15-bit value}
  // If Hi32 is zero and the Lo16(in Lo32) can be presented as a positive 16 bit
  // value, we can use LI for Lo16 without generating leading ones then add the
  // Hi16(in Lo32).
  if (LZ == 32 && ((Lo32 & 0x8000) == 0)) {
    Register TmpReg = MRI.createVirtualRegister(&PPC::G8RCRegClass);
    if (!BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::LI8), TmpReg)
             .addImm(Lo32 & 0xffff)
             .constrainAllUses(TII, TRI, RBI))
      return false;
    return BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::ORIS8), Reg)
        .addReg(TmpReg, RegState::Kill)
        .addImm(Lo32 >> 16)
        .constrainAllUses(TII, TRI, RBI);
  }
  // 2-6) Patterns : {******}{49 zeros}{******}
  //                 {******}{49 ones}{******}
  // If the Imm contains 49 consecutive zeros/ones, it means that a total of 15
  // bits remain on both sides. Rotate right the Imm to construct an int<16>
  // value, use LI for int<16> value and then use RLDICL without mask to rotate
  // it back.
  //
  // 1) findContiguousZerosAtLeast(Imm, 49)
  // +------|--zeros-|------+     +---ones--||---15 bit--+
  // |bbbbbb0000000000aaaaaa| ->  |0000000000aaaaaabbbbbb|
  // +----------------------+     +----------------------+
  // 63                    0      63                    0
  //
  // 2) findContiguousZerosAtLeast(~Imm, 49)
  // +------|--ones--|------+     +---ones--||---15 bit--+
  // |bbbbbb1111111111aaaaaa| ->  |1111111111aaaaaabbbbbb|
  // +----------------------+     +----------------------+
  // 63                    0      63                    0
  if ((Shift = findContiguousZerosAtLeast(Imm, 49)) ||
      (Shift = findContiguousZerosAtLeast(~Imm, 49))) {
    uint64_t RotImm = APInt(64, Imm).rotr(Shift).getZExtValue();
    Register TmpReg = MRI.createVirtualRegister(&PPC::G8RCRegClass);
    if (!BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::LI8), TmpReg)
             .addImm(RotImm & 0xffff)
             .constrainAllUses(TII, TRI, RBI))
      return false;
    return BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::RLDICL), Reg)
        .addReg(TmpReg, RegState::Kill)
        .addImm(Shift)
        .addImm(0)
        .constrainAllUses(TII, TRI, RBI);
  }

  // Following patterns use 3 instructions to materialize the Imm.

  // 3-1) Patterns : {zeros}{ones}{31-bit value}{zeros}
  //                 {zeros}{31-bit value}{zeros}
  //                 {zeros}{ones}{31-bit value}
  //                 {ones}{31-bit value}{zeros}
  // We can take advantage of LIS's sign-extension semantics to generate leading
  // ones, add the remaining bits with ORI, and then use RLDIC to mask off the
  // ones in both sides after rotation.
  if ((LZ + FO + TZ) > 32) {
    uint64_t ImmHi16 = (Imm >> (TZ + 16)) & 0xffff;
    unsigned Opcode = ImmHi16 ? PPC::LIS8 : PPC::LI8;
    Register TmpReg = MRI.createVirtualRegister(&PPC::G8RCRegClass);
    Register Tmp2Reg = MRI.createVirtualRegister(&PPC::G8RCRegClass);
    if (!BuildMI(MBB, I, I.getDebugLoc(), TII.get(Opcode), TmpReg)
             .addImm(ImmHi16)
             .constrainAllUses(TII, TRI, RBI))
      return false;
    if (!BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::ORI8), Tmp2Reg)
             .addReg(TmpReg, RegState::Kill)
             .addImm((Imm >> TZ) & 0xffff)
             .constrainAllUses(TII, TRI, RBI))
      return false;
    return BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::RLDIC), Reg)
        .addReg(Tmp2Reg, RegState::Kill)
        .addImm(TZ)
        .addImm(LZ)
        .constrainAllUses(TII, TRI, RBI);
  }
  // 3-2) Pattern : {zeros}{31-bit value}{ones}
  // Shift right the Imm by (32 - LZ) bits to construct a negative 32 bits
  // value, therefore we can take advantage of LIS's sign-extension semantics,
  // add the remaining bits with ORI, and then mask them off after rotation.
  // This is similar to Pattern 2-3, please refer to the diagram there.
  if ((LZ + TO) > 32) {
    // Since the immediates with (LZ > 32) have been handled by previous
    // patterns, here we have (LZ <= 32) to make sure we will not shift right
    // the Imm by a negative value.
    assert(LZ <= 32 && "Unexpected shift value.");
    Register TmpReg = MRI.createVirtualRegister(&PPC::G8RCRegClass);
    Register Tmp2Reg = MRI.createVirtualRegister(&PPC::G8RCRegClass);
    if (!BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::LIS8), TmpReg)
            .addImm((Imm >> (48 - LZ)) & 0xffff)
            .constrainAllUses(TII, TRI, RBI))
      return false;
    if (!BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::ORI8), Tmp2Reg)
             .addReg(TmpReg, RegState::Kill)
             .addImm((Imm >> (32 - LZ)) & 0xffff)
             .constrainAllUses(TII, TRI, RBI))
      return false;
    return BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::RLDICL), Reg)
        .addReg(Tmp2Reg, RegState::Kill)
        .addImm(32 - LZ)
        .addImm(LZ)
        .constrainAllUses(TII, TRI, RBI);
  }
  // 3-3) Patterns : {zeros}{ones}{31-bit value}{ones}
  //                 {ones}{31-bit value}{ones}
  // We can take advantage of LIS's sign-extension semantics to generate leading
  // ones, add the remaining bits with ORI, and then use RLDICL to mask off the
  // ones in left sides (if required) after rotation.
  // This is similar to Pattern 2-4, please refer to the diagram there.
  if ((LZ + FO + TO) > 32) {
    Register TmpReg = MRI.createVirtualRegister(&PPC::G8RCRegClass);
    Register Tmp2Reg = MRI.createVirtualRegister(&PPC::G8RCRegClass);
    if (!BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::LIS8), TmpReg)
             .addImm((Imm >> (TO + 16)) & 0xffff)
             .constrainAllUses(TII, TRI, RBI))
      return false;
    if (!BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::ORI8), Tmp2Reg)
             .addReg(TmpReg, RegState::Kill)
             .addImm((Imm >> TO) & 0xffff)
             .constrainAllUses(TII, TRI, RBI))
      return false;
    return BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::RLDICL), Reg)
        .addReg(Tmp2Reg, RegState::Kill)
        .addImm(TO)
        .addImm(LZ)
        .constrainAllUses(TII, TRI, RBI);
  }
  // 3-4) Patterns : High word == Low word
  if (Hi32 == Lo32) {
    // Handle the first 32 bits.
    uint64_t ImmHi16 = (Lo32 >> 16) & 0xffff;
    unsigned Opcode = ImmHi16 ? PPC::LIS8 : PPC::LI8;
    Register TmpReg = MRI.createVirtualRegister(&PPC::G8RCRegClass);
    Register Tmp2Reg = MRI.createVirtualRegister(&PPC::G8RCRegClass);
    if (!BuildMI(MBB, I, I.getDebugLoc(), TII.get(Opcode), TmpReg)
             .addImm(ImmHi16)
             .constrainAllUses(TII, TRI, RBI))
      return false;
    if (!BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::ORI8), Tmp2Reg)
             .addReg(TmpReg, RegState::Kill)
             .addImm(Lo32 & 0xffff)
             .constrainAllUses(TII, TRI, RBI))
      return false;
    return BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::RLDIMI), Reg)
        .addReg(Tmp2Reg)
        .addReg(Tmp2Reg, RegState::Kill)
        .addImm(32)
        .addImm(0)
        .constrainAllUses(TII, TRI, RBI);
  }
  // 3-5) Patterns : {******}{33 zeros}{******}
  //                 {******}{33 ones}{******}
  // If the Imm contains 33 consecutive zeros/ones, it means that a total of 31
  // bits remain on both sides. Rotate right the Imm to construct an int<32>
  // value, use LIS + ORI for int<32> value and then use RLDICL without mask to
  // rotate it back.
  // This is similar to Pattern 2-6, please refer to the diagram there.
  if ((Shift = findContiguousZerosAtLeast(Imm, 33)) ||
      (Shift = findContiguousZerosAtLeast(~Imm, 33))) {
    uint64_t RotImm = APInt(64, Imm).rotr(Shift).getZExtValue();
    uint64_t ImmHi16 = (RotImm >> 16) & 0xffff;
    unsigned Opcode = ImmHi16 ? PPC::LIS8 : PPC::LI8;
    Register TmpReg = MRI.createVirtualRegister(&PPC::G8RCRegClass);
    Register Tmp2Reg = MRI.createVirtualRegister(&PPC::G8RCRegClass);
    if (!BuildMI(MBB, I, I.getDebugLoc(), TII.get(Opcode), TmpReg)
             .addImm(ImmHi16)
             .constrainAllUses(TII, TRI, RBI))
      return false;
    if (!BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::ORI8), Tmp2Reg)
             .addReg(TmpReg, RegState::Kill)
             .addImm(RotImm & 0xffff)
             .constrainAllUses(TII, TRI, RBI))
      return false;
    return BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::RLDICL), Reg)
        .addReg(Tmp2Reg, RegState::Kill)
        .addImm(Shift)
        .addImm(0)
        .constrainAllUses(TII, TRI, RBI);
  }

  // If we end up here then no instructions were inserted.
  return std::nullopt;
}

// Derived from PPCISelDAGToDAG::selectI64Imm().
// TODO: Add support for prefixed instructions.
bool PPCInstructionSelector::selectI64Imm(MachineInstr &I,
                                          MachineBasicBlock &MBB,
                                          MachineRegisterInfo &MRI) const {
  assert(I.getOpcode() == TargetOpcode::G_CONSTANT && "Unexpected G code");

  Register DstReg = I.getOperand(0).getReg();
  int64_t Imm = I.getOperand(1).getCImm()->getValue().getZExtValue();
  // No more than 3 instructions are used if we can select the i64 immediate
  // directly.
  if (std::optional<bool> Res = selectI64ImmDirect(I, MBB, MRI, DstReg, Imm)) {
    I.eraseFromParent();
    return *Res;
  }

  // Calculate the last bits as required.
  uint32_t Hi16 = (Lo_32(Imm) >> 16) & 0xffff;
  uint32_t Lo16 = Lo_32(Imm) & 0xffff;

  Register Reg =
      (Hi16 || Lo16) ? MRI.createVirtualRegister(&PPC::G8RCRegClass) : DstReg;

  // Handle the upper 32 bit value.
  std::optional<bool> Res =
      selectI64ImmDirect(I, MBB, MRI, Reg, Imm & 0xffffffff00000000);
  if (!Res || !*Res)
    return false;

  // Add in the last bits as required.
  if (Hi16) {
    Register TmpReg =
        Lo16 ? MRI.createVirtualRegister(&PPC::G8RCRegClass) : DstReg;
    if (!BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::ORIS8), TmpReg)
             .addReg(Reg, RegState::Kill)
             .addImm(Hi16)
             .constrainAllUses(TII, TRI, RBI))
      return false;
    Reg = TmpReg;
  }
  if (Lo16) {
    if (!BuildMI(MBB, I, I.getDebugLoc(), TII.get(PPC::ORI8), DstReg)
             .addReg(Reg, RegState::Kill)
             .addImm(Lo16)
             .constrainAllUses(TII, TRI, RBI))
      return false;
  }
  I.eraseFromParent();
  return true;
}

bool PPCInstructionSelector::selectConstantPool(
    MachineInstr &I, MachineBasicBlock &MBB, MachineRegisterInfo &MRI) const {
  const DebugLoc &DbgLoc = I.getDebugLoc();
  MachineFunction *MF = MBB.getParent();

  // TODO: handle 32-bit.
  // TODO: Enabling floating point constant pool selection on AIX requires
  // global isel on big endian target enabled first.
  // See CallLowering::enableBigEndian().
  if (!STI.isPPC64() || !STI.isLittleEndian())
    return false;

  MF->getInfo<PPCFunctionInfo>()->setUsesTOCBasePtr();

  const Register DstReg = I.getOperand(0).getReg();
  unsigned CPI = I.getOperand(1).getIndex();

  // Address stored in the TOC entry. This is related to code model and the ABI
  // we are currently using. For now we only handle 64-bit Linux LE. PowerPC
  // only supports small, medium and large code model.
  const CodeModel::Model CModel = TM.getCodeModel();
  assert(!(CModel == CodeModel::Tiny || CModel == CodeModel::Kernel) &&
         "PowerPC doesn't support tiny or kernel code models.");

  const MCRegister TOCReg = STI.getTOCPointerRegister();
  MachineMemOperand *MMO = MF->getMachineMemOperand(
      MachinePointerInfo::getGOT(*MF), MachineMemOperand::MOLoad,
      MRI.getType(DstReg), MF->getDataLayout().getPointerABIAlignment(0));

  MachineInstr *MI = nullptr;
  // For now we only handle 64-bit Linux.
  if (CModel == CodeModel::Small) {
    // For small code model, generate LDtocCPT(CPI, X2).
    MI = BuildMI(MBB, I, DbgLoc, TII.get(PPC::LDtocCPT), DstReg)
             .addConstantPoolIndex(CPI)
             .addReg(TOCReg)
             .addMemOperand(MMO);
  } else {
    Register HaAddrReg = MRI.createVirtualRegister(&PPC::G8RCRegClass);
    BuildMI(MBB, I, DbgLoc, TII.get(PPC::ADDIStocHA8), HaAddrReg)
        .addReg(TOCReg)
        .addConstantPoolIndex(CPI);

    if (CModel == CodeModel::Large)
      // For large code model, generate LDtocL(CPI, ADDIStocHA8(X2, CPI))
      MI = BuildMI(MBB, I, DbgLoc, TII.get(PPC::LDtocL), DstReg)
               .addConstantPoolIndex(CPI)
               .addReg(HaAddrReg)
               .addMemOperand(MMO);
    else
      // For medium code model, generate ADDItocL8(CPI, ADDIStocHA8(X2, CPI))
      MI = BuildMI(MBB, I, DbgLoc, TII.get(PPC::ADDItocL8), DstReg)
               .addReg(HaAddrReg)
               .addConstantPoolIndex(CPI);
  }

  I.eraseFromParent();
  return constrainSelectedInstRegOperands(*MI, TII, TRI, RBI);
}

bool PPCInstructionSelector::select(MachineInstr &I) {
  auto &MBB = *I.getParent();
  auto &MF = *MBB.getParent();
  auto &MRI = MF.getRegInfo();

  if (!isPreISelGenericOpcode(I.getOpcode())) {
    if (I.isCopy())
      return selectCopy(I, TII, MRI, TRI, RBI);

    return true;
  }

  if (selectImpl(I, *CoverageInfo))
    return true;

  unsigned Opcode = I.getOpcode();

  switch (Opcode) {
  default:
    return false;
  case TargetOpcode::G_LOAD:
  case TargetOpcode::G_STORE: {
    GLoadStore &LdSt = cast<GLoadStore>(I);
    LLT PtrTy = MRI.getType(LdSt.getPointerReg());

    if (PtrTy != LLT::pointer(0, 64)) {
      LLVM_DEBUG(dbgs() << "Load/Store pointer has type: " << PtrTy
                        << ", expected: " << LLT::pointer(0, 64) << '\n');
      return false;
    }

    auto SelectLoadStoreAddressingMode = [&]() -> MachineInstr * {
      const unsigned NewOpc = selectLoadStoreOp(
          I.getOpcode(), RBI.getRegBank(LdSt.getReg(0), MRI, TRI)->getID(),
          LdSt.getMemSizeInBits().getValue());

      if (NewOpc == I.getOpcode())
        return nullptr;

      // For now, simply use DForm with load/store addr as base and 0 as imm.
      // FIXME: optimize load/store with some specific address patterns.
      I.setDesc(TII.get(NewOpc));
      Register AddrReg = I.getOperand(1).getReg();
      bool IsKill = I.getOperand(1).isKill();
      I.getOperand(1).ChangeToImmediate(0);
      I.addOperand(*I.getParent()->getParent(),
                   MachineOperand::CreateReg(AddrReg, /* isDef */ false,
                                             /* isImp */ false, IsKill));
      return &I;
    };

    MachineInstr *LoadStore = SelectLoadStoreAddressingMode();
    if (!LoadStore)
      return false;

    return constrainSelectedInstRegOperands(*LoadStore, TII, TRI, RBI);
  }
  case TargetOpcode::G_SITOFP:
  case TargetOpcode::G_UITOFP:
    return selectIntToFP(I, MBB, MRI);
  case TargetOpcode::G_FPTOSI:
  case TargetOpcode::G_FPTOUI:
    return selectFPToInt(I, MBB, MRI);
  // G_SEXT will be selected in tb-gen pattern.
  case TargetOpcode::G_ZEXT:
    return selectZExt(I, MBB, MRI);
  case TargetOpcode::G_CONSTANT:
    return selectI64Imm(I, MBB, MRI);
  case TargetOpcode::G_CONSTANT_POOL:
    return selectConstantPool(I, MBB, MRI);
  }
  return false;
}

namespace llvm {
InstructionSelector *
createPPCInstructionSelector(const PPCTargetMachine &TM,
                             const PPCSubtarget &Subtarget,
                             const PPCRegisterBankInfo &RBI) {
  return new PPCInstructionSelector(TM, Subtarget, RBI);
}
} // end namespace llvm
