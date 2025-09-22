//===-- X86InstrInfo.cpp - X86 Instruction Information --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the X86 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "X86InstrInfo.h"
#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86InstrFoldTables.h"
#include "X86MachineFunctionInfo.h"
#include "X86Subtarget.h"
#include "X86TargetMachine.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LivePhysRegs.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/MachineCombinerPattern.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/StackMaps.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetOptions.h"
#include <optional>

using namespace llvm;

#define DEBUG_TYPE "x86-instr-info"

#define GET_INSTRINFO_CTOR_DTOR
#include "X86GenInstrInfo.inc"

static cl::opt<bool>
    NoFusing("disable-spill-fusing",
             cl::desc("Disable fusing of spill code into instructions"),
             cl::Hidden);
static cl::opt<bool>
    PrintFailedFusing("print-failed-fuse-candidates",
                      cl::desc("Print instructions that the allocator wants to"
                               " fuse, but the X86 backend currently can't"),
                      cl::Hidden);
static cl::opt<bool>
    ReMatPICStubLoad("remat-pic-stub-load",
                     cl::desc("Re-materialize load from stub in PIC mode"),
                     cl::init(false), cl::Hidden);
static cl::opt<unsigned>
    PartialRegUpdateClearance("partial-reg-update-clearance",
                              cl::desc("Clearance between two register writes "
                                       "for inserting XOR to avoid partial "
                                       "register update"),
                              cl::init(64), cl::Hidden);
static cl::opt<unsigned> UndefRegClearance(
    "undef-reg-clearance",
    cl::desc("How many idle instructions we would like before "
             "certain undef register reads"),
    cl::init(128), cl::Hidden);

// Pin the vtable to this file.
void X86InstrInfo::anchor() {}

X86InstrInfo::X86InstrInfo(X86Subtarget &STI)
    : X86GenInstrInfo((STI.isTarget64BitLP64() ? X86::ADJCALLSTACKDOWN64
                                               : X86::ADJCALLSTACKDOWN32),
                      (STI.isTarget64BitLP64() ? X86::ADJCALLSTACKUP64
                                               : X86::ADJCALLSTACKUP32),
                      X86::CATCHRET, (STI.is64Bit() ? X86::RET64 : X86::RET32)),
      Subtarget(STI), RI(STI.getTargetTriple()) {}

const TargetRegisterClass *
X86InstrInfo::getRegClass(const MCInstrDesc &MCID, unsigned OpNum,
                          const TargetRegisterInfo *TRI,
                          const MachineFunction &MF) const {
  auto *RC = TargetInstrInfo::getRegClass(MCID, OpNum, TRI, MF);
  // If the target does not have egpr, then r16-r31 will be resereved for all
  // instructions.
  if (!RC || !Subtarget.hasEGPR())
    return RC;

  if (X86II::canUseApxExtendedReg(MCID))
    return RC;

  switch (RC->getID()) {
  default:
    return RC;
  case X86::GR8RegClassID:
    return &X86::GR8_NOREX2RegClass;
  case X86::GR16RegClassID:
    return &X86::GR16_NOREX2RegClass;
  case X86::GR32RegClassID:
    return &X86::GR32_NOREX2RegClass;
  case X86::GR64RegClassID:
    return &X86::GR64_NOREX2RegClass;
  case X86::GR32_NOSPRegClassID:
    return &X86::GR32_NOREX2_NOSPRegClass;
  case X86::GR64_NOSPRegClassID:
    return &X86::GR64_NOREX2_NOSPRegClass;
  }
}

bool X86InstrInfo::isCoalescableExtInstr(const MachineInstr &MI,
                                         Register &SrcReg, Register &DstReg,
                                         unsigned &SubIdx) const {
  switch (MI.getOpcode()) {
  default:
    break;
  case X86::MOVSX16rr8:
  case X86::MOVZX16rr8:
  case X86::MOVSX32rr8:
  case X86::MOVZX32rr8:
  case X86::MOVSX64rr8:
    if (!Subtarget.is64Bit())
      // It's not always legal to reference the low 8-bit of the larger
      // register in 32-bit mode.
      return false;
    [[fallthrough]];
  case X86::MOVSX32rr16:
  case X86::MOVZX32rr16:
  case X86::MOVSX64rr16:
  case X86::MOVSX64rr32: {
    if (MI.getOperand(0).getSubReg() || MI.getOperand(1).getSubReg())
      // Be conservative.
      return false;
    SrcReg = MI.getOperand(1).getReg();
    DstReg = MI.getOperand(0).getReg();
    switch (MI.getOpcode()) {
    default:
      llvm_unreachable("Unreachable!");
    case X86::MOVSX16rr8:
    case X86::MOVZX16rr8:
    case X86::MOVSX32rr8:
    case X86::MOVZX32rr8:
    case X86::MOVSX64rr8:
      SubIdx = X86::sub_8bit;
      break;
    case X86::MOVSX32rr16:
    case X86::MOVZX32rr16:
    case X86::MOVSX64rr16:
      SubIdx = X86::sub_16bit;
      break;
    case X86::MOVSX64rr32:
      SubIdx = X86::sub_32bit;
      break;
    }
    return true;
  }
  }
  return false;
}

bool X86InstrInfo::isDataInvariant(MachineInstr &MI) {
  if (MI.mayLoad() || MI.mayStore())
    return false;

  // Some target-independent operations that trivially lower to data-invariant
  // instructions.
  if (MI.isCopyLike() || MI.isInsertSubreg())
    return true;

  unsigned Opcode = MI.getOpcode();
  using namespace X86;
  // On x86 it is believed that imul is constant time w.r.t. the loaded data.
  // However, they set flags and are perhaps the most surprisingly constant
  // time operations so we call them out here separately.
  if (isIMUL(Opcode))
    return true;
  // Bit scanning and counting instructions that are somewhat surprisingly
  // constant time as they scan across bits and do other fairly complex
  // operations like popcnt, but are believed to be constant time on x86.
  // However, these set flags.
  if (isBSF(Opcode) || isBSR(Opcode) || isLZCNT(Opcode) || isPOPCNT(Opcode) ||
      isTZCNT(Opcode))
    return true;
  // Bit manipulation instructions are effectively combinations of basic
  // arithmetic ops, and should still execute in constant time. These also
  // set flags.
  if (isBLCFILL(Opcode) || isBLCI(Opcode) || isBLCIC(Opcode) ||
      isBLCMSK(Opcode) || isBLCS(Opcode) || isBLSFILL(Opcode) ||
      isBLSI(Opcode) || isBLSIC(Opcode) || isBLSMSK(Opcode) || isBLSR(Opcode) ||
      isTZMSK(Opcode))
    return true;
  // Bit extracting and clearing instructions should execute in constant time,
  // and set flags.
  if (isBEXTR(Opcode) || isBZHI(Opcode))
    return true;
  // Shift and rotate.
  if (isROL(Opcode) || isROR(Opcode) || isSAR(Opcode) || isSHL(Opcode) ||
      isSHR(Opcode) || isSHLD(Opcode) || isSHRD(Opcode))
    return true;
  // Basic arithmetic is constant time on the input but does set flags.
  if (isADC(Opcode) || isADD(Opcode) || isAND(Opcode) || isOR(Opcode) ||
      isSBB(Opcode) || isSUB(Opcode) || isXOR(Opcode))
    return true;
  // Arithmetic with just 32-bit and 64-bit variants and no immediates.
  if (isANDN(Opcode))
    return true;
  // Unary arithmetic operations.
  if (isDEC(Opcode) || isINC(Opcode) || isNEG(Opcode))
    return true;
  // Unlike other arithmetic, NOT doesn't set EFLAGS.
  if (isNOT(Opcode))
    return true;
  // Various move instructions used to zero or sign extend things. Note that we
  // intentionally don't support the _NOREX variants as we can't handle that
  // register constraint anyways.
  if (isMOVSX(Opcode) || isMOVZX(Opcode) || isMOVSXD(Opcode) || isMOV(Opcode))
    return true;
  // Arithmetic instructions that are both constant time and don't set flags.
  if (isRORX(Opcode) || isSARX(Opcode) || isSHLX(Opcode) || isSHRX(Opcode))
    return true;
  // LEA doesn't actually access memory, and its arithmetic is constant time.
  if (isLEA(Opcode))
    return true;
  // By default, assume that the instruction is not data invariant.
  return false;
}

bool X86InstrInfo::isDataInvariantLoad(MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    // By default, assume that the load will immediately leak.
    return false;

  // On x86 it is believed that imul is constant time w.r.t. the loaded data.
  // However, they set flags and are perhaps the most surprisingly constant
  // time operations so we call them out here separately.
  case X86::IMUL16rm:
  case X86::IMUL16rmi:
  case X86::IMUL32rm:
  case X86::IMUL32rmi:
  case X86::IMUL64rm:
  case X86::IMUL64rmi32:

  // Bit scanning and counting instructions that are somewhat surprisingly
  // constant time as they scan across bits and do other fairly complex
  // operations like popcnt, but are believed to be constant time on x86.
  // However, these set flags.
  case X86::BSF16rm:
  case X86::BSF32rm:
  case X86::BSF64rm:
  case X86::BSR16rm:
  case X86::BSR32rm:
  case X86::BSR64rm:
  case X86::LZCNT16rm:
  case X86::LZCNT32rm:
  case X86::LZCNT64rm:
  case X86::POPCNT16rm:
  case X86::POPCNT32rm:
  case X86::POPCNT64rm:
  case X86::TZCNT16rm:
  case X86::TZCNT32rm:
  case X86::TZCNT64rm:

  // Bit manipulation instructions are effectively combinations of basic
  // arithmetic ops, and should still execute in constant time. These also
  // set flags.
  case X86::BLCFILL32rm:
  case X86::BLCFILL64rm:
  case X86::BLCI32rm:
  case X86::BLCI64rm:
  case X86::BLCIC32rm:
  case X86::BLCIC64rm:
  case X86::BLCMSK32rm:
  case X86::BLCMSK64rm:
  case X86::BLCS32rm:
  case X86::BLCS64rm:
  case X86::BLSFILL32rm:
  case X86::BLSFILL64rm:
  case X86::BLSI32rm:
  case X86::BLSI64rm:
  case X86::BLSIC32rm:
  case X86::BLSIC64rm:
  case X86::BLSMSK32rm:
  case X86::BLSMSK64rm:
  case X86::BLSR32rm:
  case X86::BLSR64rm:
  case X86::TZMSK32rm:
  case X86::TZMSK64rm:

  // Bit extracting and clearing instructions should execute in constant time,
  // and set flags.
  case X86::BEXTR32rm:
  case X86::BEXTR64rm:
  case X86::BEXTRI32mi:
  case X86::BEXTRI64mi:
  case X86::BZHI32rm:
  case X86::BZHI64rm:

  // Basic arithmetic is constant time on the input but does set flags.
  case X86::ADC8rm:
  case X86::ADC16rm:
  case X86::ADC32rm:
  case X86::ADC64rm:
  case X86::ADD8rm:
  case X86::ADD16rm:
  case X86::ADD32rm:
  case X86::ADD64rm:
  case X86::AND8rm:
  case X86::AND16rm:
  case X86::AND32rm:
  case X86::AND64rm:
  case X86::ANDN32rm:
  case X86::ANDN64rm:
  case X86::OR8rm:
  case X86::OR16rm:
  case X86::OR32rm:
  case X86::OR64rm:
  case X86::SBB8rm:
  case X86::SBB16rm:
  case X86::SBB32rm:
  case X86::SBB64rm:
  case X86::SUB8rm:
  case X86::SUB16rm:
  case X86::SUB32rm:
  case X86::SUB64rm:
  case X86::XOR8rm:
  case X86::XOR16rm:
  case X86::XOR32rm:
  case X86::XOR64rm:

  // Integer multiply w/o affecting flags is still believed to be constant
  // time on x86. Called out separately as this is among the most surprising
  // instructions to exhibit that behavior.
  case X86::MULX32rm:
  case X86::MULX64rm:

  // Arithmetic instructions that are both constant time and don't set flags.
  case X86::RORX32mi:
  case X86::RORX64mi:
  case X86::SARX32rm:
  case X86::SARX64rm:
  case X86::SHLX32rm:
  case X86::SHLX64rm:
  case X86::SHRX32rm:
  case X86::SHRX64rm:

  // Conversions are believed to be constant time and don't set flags.
  case X86::CVTTSD2SI64rm:
  case X86::VCVTTSD2SI64rm:
  case X86::VCVTTSD2SI64Zrm:
  case X86::CVTTSD2SIrm:
  case X86::VCVTTSD2SIrm:
  case X86::VCVTTSD2SIZrm:
  case X86::CVTTSS2SI64rm:
  case X86::VCVTTSS2SI64rm:
  case X86::VCVTTSS2SI64Zrm:
  case X86::CVTTSS2SIrm:
  case X86::VCVTTSS2SIrm:
  case X86::VCVTTSS2SIZrm:
  case X86::CVTSI2SDrm:
  case X86::VCVTSI2SDrm:
  case X86::VCVTSI2SDZrm:
  case X86::CVTSI2SSrm:
  case X86::VCVTSI2SSrm:
  case X86::VCVTSI2SSZrm:
  case X86::CVTSI642SDrm:
  case X86::VCVTSI642SDrm:
  case X86::VCVTSI642SDZrm:
  case X86::CVTSI642SSrm:
  case X86::VCVTSI642SSrm:
  case X86::VCVTSI642SSZrm:
  case X86::CVTSS2SDrm:
  case X86::VCVTSS2SDrm:
  case X86::VCVTSS2SDZrm:
  case X86::CVTSD2SSrm:
  case X86::VCVTSD2SSrm:
  case X86::VCVTSD2SSZrm:
  // AVX512 added unsigned integer conversions.
  case X86::VCVTTSD2USI64Zrm:
  case X86::VCVTTSD2USIZrm:
  case X86::VCVTTSS2USI64Zrm:
  case X86::VCVTTSS2USIZrm:
  case X86::VCVTUSI2SDZrm:
  case X86::VCVTUSI642SDZrm:
  case X86::VCVTUSI2SSZrm:
  case X86::VCVTUSI642SSZrm:

  // Loads to register don't set flags.
  case X86::MOV8rm:
  case X86::MOV8rm_NOREX:
  case X86::MOV16rm:
  case X86::MOV32rm:
  case X86::MOV64rm:
  case X86::MOVSX16rm8:
  case X86::MOVSX32rm16:
  case X86::MOVSX32rm8:
  case X86::MOVSX32rm8_NOREX:
  case X86::MOVSX64rm16:
  case X86::MOVSX64rm32:
  case X86::MOVSX64rm8:
  case X86::MOVZX16rm8:
  case X86::MOVZX32rm16:
  case X86::MOVZX32rm8:
  case X86::MOVZX32rm8_NOREX:
  case X86::MOVZX64rm16:
  case X86::MOVZX64rm8:
    return true;
  }
}

int X86InstrInfo::getSPAdjust(const MachineInstr &MI) const {
  const MachineFunction *MF = MI.getParent()->getParent();
  const TargetFrameLowering *TFI = MF->getSubtarget().getFrameLowering();

  if (isFrameInstr(MI)) {
    int SPAdj = alignTo(getFrameSize(MI), TFI->getStackAlign());
    SPAdj -= getFrameAdjustment(MI);
    if (!isFrameSetup(MI))
      SPAdj = -SPAdj;
    return SPAdj;
  }

  // To know whether a call adjusts the stack, we need information
  // that is bound to the following ADJCALLSTACKUP pseudo.
  // Look for the next ADJCALLSTACKUP that follows the call.
  if (MI.isCall()) {
    const MachineBasicBlock *MBB = MI.getParent();
    auto I = ++MachineBasicBlock::const_iterator(MI);
    for (auto E = MBB->end(); I != E; ++I) {
      if (I->getOpcode() == getCallFrameDestroyOpcode() || I->isCall())
        break;
    }

    // If we could not find a frame destroy opcode, then it has already
    // been simplified, so we don't care.
    if (I->getOpcode() != getCallFrameDestroyOpcode())
      return 0;

    return -(I->getOperand(1).getImm());
  }

  // Currently handle only PUSHes we can reasonably expect to see
  // in call sequences
  switch (MI.getOpcode()) {
  default:
    return 0;
  case X86::PUSH32r:
  case X86::PUSH32rmm:
  case X86::PUSH32rmr:
  case X86::PUSH32i:
    return 4;
  case X86::PUSH64r:
  case X86::PUSH64rmm:
  case X86::PUSH64rmr:
  case X86::PUSH64i32:
    return 8;
  }
}

/// Return true and the FrameIndex if the specified
/// operand and follow operands form a reference to the stack frame.
bool X86InstrInfo::isFrameOperand(const MachineInstr &MI, unsigned int Op,
                                  int &FrameIndex) const {
  if (MI.getOperand(Op + X86::AddrBaseReg).isFI() &&
      MI.getOperand(Op + X86::AddrScaleAmt).isImm() &&
      MI.getOperand(Op + X86::AddrIndexReg).isReg() &&
      MI.getOperand(Op + X86::AddrDisp).isImm() &&
      MI.getOperand(Op + X86::AddrScaleAmt).getImm() == 1 &&
      MI.getOperand(Op + X86::AddrIndexReg).getReg() == 0 &&
      MI.getOperand(Op + X86::AddrDisp).getImm() == 0) {
    FrameIndex = MI.getOperand(Op + X86::AddrBaseReg).getIndex();
    return true;
  }
  return false;
}

static bool isFrameLoadOpcode(int Opcode, unsigned &MemBytes) {
  switch (Opcode) {
  default:
    return false;
  case X86::MOV8rm:
  case X86::KMOVBkm:
  case X86::KMOVBkm_EVEX:
    MemBytes = 1;
    return true;
  case X86::MOV16rm:
  case X86::KMOVWkm:
  case X86::KMOVWkm_EVEX:
  case X86::VMOVSHZrm:
  case X86::VMOVSHZrm_alt:
    MemBytes = 2;
    return true;
  case X86::MOV32rm:
  case X86::MOVSSrm:
  case X86::MOVSSrm_alt:
  case X86::VMOVSSrm:
  case X86::VMOVSSrm_alt:
  case X86::VMOVSSZrm:
  case X86::VMOVSSZrm_alt:
  case X86::KMOVDkm:
  case X86::KMOVDkm_EVEX:
    MemBytes = 4;
    return true;
  case X86::MOV64rm:
  case X86::LD_Fp64m:
  case X86::MOVSDrm:
  case X86::MOVSDrm_alt:
  case X86::VMOVSDrm:
  case X86::VMOVSDrm_alt:
  case X86::VMOVSDZrm:
  case X86::VMOVSDZrm_alt:
  case X86::MMX_MOVD64rm:
  case X86::MMX_MOVQ64rm:
  case X86::KMOVQkm:
  case X86::KMOVQkm_EVEX:
    MemBytes = 8;
    return true;
  case X86::MOVAPSrm:
  case X86::MOVUPSrm:
  case X86::MOVAPDrm:
  case X86::MOVUPDrm:
  case X86::MOVDQArm:
  case X86::MOVDQUrm:
  case X86::VMOVAPSrm:
  case X86::VMOVUPSrm:
  case X86::VMOVAPDrm:
  case X86::VMOVUPDrm:
  case X86::VMOVDQArm:
  case X86::VMOVDQUrm:
  case X86::VMOVAPSZ128rm:
  case X86::VMOVUPSZ128rm:
  case X86::VMOVAPSZ128rm_NOVLX:
  case X86::VMOVUPSZ128rm_NOVLX:
  case X86::VMOVAPDZ128rm:
  case X86::VMOVUPDZ128rm:
  case X86::VMOVDQU8Z128rm:
  case X86::VMOVDQU16Z128rm:
  case X86::VMOVDQA32Z128rm:
  case X86::VMOVDQU32Z128rm:
  case X86::VMOVDQA64Z128rm:
  case X86::VMOVDQU64Z128rm:
    MemBytes = 16;
    return true;
  case X86::VMOVAPSYrm:
  case X86::VMOVUPSYrm:
  case X86::VMOVAPDYrm:
  case X86::VMOVUPDYrm:
  case X86::VMOVDQAYrm:
  case X86::VMOVDQUYrm:
  case X86::VMOVAPSZ256rm:
  case X86::VMOVUPSZ256rm:
  case X86::VMOVAPSZ256rm_NOVLX:
  case X86::VMOVUPSZ256rm_NOVLX:
  case X86::VMOVAPDZ256rm:
  case X86::VMOVUPDZ256rm:
  case X86::VMOVDQU8Z256rm:
  case X86::VMOVDQU16Z256rm:
  case X86::VMOVDQA32Z256rm:
  case X86::VMOVDQU32Z256rm:
  case X86::VMOVDQA64Z256rm:
  case X86::VMOVDQU64Z256rm:
    MemBytes = 32;
    return true;
  case X86::VMOVAPSZrm:
  case X86::VMOVUPSZrm:
  case X86::VMOVAPDZrm:
  case X86::VMOVUPDZrm:
  case X86::VMOVDQU8Zrm:
  case X86::VMOVDQU16Zrm:
  case X86::VMOVDQA32Zrm:
  case X86::VMOVDQU32Zrm:
  case X86::VMOVDQA64Zrm:
  case X86::VMOVDQU64Zrm:
    MemBytes = 64;
    return true;
  }
}

static bool isFrameStoreOpcode(int Opcode, unsigned &MemBytes) {
  switch (Opcode) {
  default:
    return false;
  case X86::MOV8mr:
  case X86::KMOVBmk:
  case X86::KMOVBmk_EVEX:
    MemBytes = 1;
    return true;
  case X86::MOV16mr:
  case X86::KMOVWmk:
  case X86::KMOVWmk_EVEX:
  case X86::VMOVSHZmr:
    MemBytes = 2;
    return true;
  case X86::MOV32mr:
  case X86::MOVSSmr:
  case X86::VMOVSSmr:
  case X86::VMOVSSZmr:
  case X86::KMOVDmk:
  case X86::KMOVDmk_EVEX:
    MemBytes = 4;
    return true;
  case X86::MOV64mr:
  case X86::ST_FpP64m:
  case X86::MOVSDmr:
  case X86::VMOVSDmr:
  case X86::VMOVSDZmr:
  case X86::MMX_MOVD64mr:
  case X86::MMX_MOVQ64mr:
  case X86::MMX_MOVNTQmr:
  case X86::KMOVQmk:
  case X86::KMOVQmk_EVEX:
    MemBytes = 8;
    return true;
  case X86::MOVAPSmr:
  case X86::MOVUPSmr:
  case X86::MOVAPDmr:
  case X86::MOVUPDmr:
  case X86::MOVDQAmr:
  case X86::MOVDQUmr:
  case X86::VMOVAPSmr:
  case X86::VMOVUPSmr:
  case X86::VMOVAPDmr:
  case X86::VMOVUPDmr:
  case X86::VMOVDQAmr:
  case X86::VMOVDQUmr:
  case X86::VMOVUPSZ128mr:
  case X86::VMOVAPSZ128mr:
  case X86::VMOVUPSZ128mr_NOVLX:
  case X86::VMOVAPSZ128mr_NOVLX:
  case X86::VMOVUPDZ128mr:
  case X86::VMOVAPDZ128mr:
  case X86::VMOVDQA32Z128mr:
  case X86::VMOVDQU32Z128mr:
  case X86::VMOVDQA64Z128mr:
  case X86::VMOVDQU64Z128mr:
  case X86::VMOVDQU8Z128mr:
  case X86::VMOVDQU16Z128mr:
    MemBytes = 16;
    return true;
  case X86::VMOVUPSYmr:
  case X86::VMOVAPSYmr:
  case X86::VMOVUPDYmr:
  case X86::VMOVAPDYmr:
  case X86::VMOVDQUYmr:
  case X86::VMOVDQAYmr:
  case X86::VMOVUPSZ256mr:
  case X86::VMOVAPSZ256mr:
  case X86::VMOVUPSZ256mr_NOVLX:
  case X86::VMOVAPSZ256mr_NOVLX:
  case X86::VMOVUPDZ256mr:
  case X86::VMOVAPDZ256mr:
  case X86::VMOVDQU8Z256mr:
  case X86::VMOVDQU16Z256mr:
  case X86::VMOVDQA32Z256mr:
  case X86::VMOVDQU32Z256mr:
  case X86::VMOVDQA64Z256mr:
  case X86::VMOVDQU64Z256mr:
    MemBytes = 32;
    return true;
  case X86::VMOVUPSZmr:
  case X86::VMOVAPSZmr:
  case X86::VMOVUPDZmr:
  case X86::VMOVAPDZmr:
  case X86::VMOVDQU8Zmr:
  case X86::VMOVDQU16Zmr:
  case X86::VMOVDQA32Zmr:
  case X86::VMOVDQU32Zmr:
  case X86::VMOVDQA64Zmr:
  case X86::VMOVDQU64Zmr:
    MemBytes = 64;
    return true;
  }
  return false;
}

Register X86InstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                           int &FrameIndex) const {
  unsigned Dummy;
  return X86InstrInfo::isLoadFromStackSlot(MI, FrameIndex, Dummy);
}

Register X86InstrInfo::isLoadFromStackSlot(const MachineInstr &MI,
                                           int &FrameIndex,
                                           unsigned &MemBytes) const {
  if (isFrameLoadOpcode(MI.getOpcode(), MemBytes))
    if (MI.getOperand(0).getSubReg() == 0 && isFrameOperand(MI, 1, FrameIndex))
      return MI.getOperand(0).getReg();
  return 0;
}

Register X86InstrInfo::isLoadFromStackSlotPostFE(const MachineInstr &MI,
                                                 int &FrameIndex) const {
  unsigned Dummy;
  if (isFrameLoadOpcode(MI.getOpcode(), Dummy)) {
    unsigned Reg;
    if ((Reg = isLoadFromStackSlot(MI, FrameIndex)))
      return Reg;
    // Check for post-frame index elimination operations
    SmallVector<const MachineMemOperand *, 1> Accesses;
    if (hasLoadFromStackSlot(MI, Accesses)) {
      FrameIndex =
          cast<FixedStackPseudoSourceValue>(Accesses.front()->getPseudoValue())
              ->getFrameIndex();
      return MI.getOperand(0).getReg();
    }
  }
  return 0;
}

Register X86InstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                          int &FrameIndex) const {
  unsigned Dummy;
  return X86InstrInfo::isStoreToStackSlot(MI, FrameIndex, Dummy);
}

Register X86InstrInfo::isStoreToStackSlot(const MachineInstr &MI,
                                          int &FrameIndex,
                                          unsigned &MemBytes) const {
  if (isFrameStoreOpcode(MI.getOpcode(), MemBytes))
    if (MI.getOperand(X86::AddrNumOperands).getSubReg() == 0 &&
        isFrameOperand(MI, 0, FrameIndex))
      return MI.getOperand(X86::AddrNumOperands).getReg();
  return 0;
}

Register X86InstrInfo::isStoreToStackSlotPostFE(const MachineInstr &MI,
                                                int &FrameIndex) const {
  unsigned Dummy;
  if (isFrameStoreOpcode(MI.getOpcode(), Dummy)) {
    unsigned Reg;
    if ((Reg = isStoreToStackSlot(MI, FrameIndex)))
      return Reg;
    // Check for post-frame index elimination operations
    SmallVector<const MachineMemOperand *, 1> Accesses;
    if (hasStoreToStackSlot(MI, Accesses)) {
      FrameIndex =
          cast<FixedStackPseudoSourceValue>(Accesses.front()->getPseudoValue())
              ->getFrameIndex();
      return MI.getOperand(X86::AddrNumOperands).getReg();
    }
  }
  return 0;
}

/// Return true if register is PIC base; i.e.g defined by X86::MOVPC32r.
static bool regIsPICBase(Register BaseReg, const MachineRegisterInfo &MRI) {
  // Don't waste compile time scanning use-def chains of physregs.
  if (!BaseReg.isVirtual())
    return false;
  bool isPICBase = false;
  for (const MachineInstr &DefMI : MRI.def_instructions(BaseReg)) {
    if (DefMI.getOpcode() != X86::MOVPC32r)
      return false;
    assert(!isPICBase && "More than one PIC base?");
    isPICBase = true;
  }
  return isPICBase;
}

bool X86InstrInfo::isReallyTriviallyReMaterializable(
    const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  default:
    // This function should only be called for opcodes with the ReMaterializable
    // flag set.
    llvm_unreachable("Unknown rematerializable operation!");
    break;
  case X86::IMPLICIT_DEF:
    // Defer to generic logic.
    break;
  case X86::LOAD_STACK_GUARD:
  case X86::LD_Fp032:
  case X86::LD_Fp064:
  case X86::LD_Fp080:
  case X86::LD_Fp132:
  case X86::LD_Fp164:
  case X86::LD_Fp180:
  case X86::AVX1_SETALLONES:
  case X86::AVX2_SETALLONES:
  case X86::AVX512_128_SET0:
  case X86::AVX512_256_SET0:
  case X86::AVX512_512_SET0:
  case X86::AVX512_512_SETALLONES:
  case X86::AVX512_FsFLD0SD:
  case X86::AVX512_FsFLD0SH:
  case X86::AVX512_FsFLD0SS:
  case X86::AVX512_FsFLD0F128:
  case X86::AVX_SET0:
  case X86::FsFLD0SD:
  case X86::FsFLD0SS:
  case X86::FsFLD0SH:
  case X86::FsFLD0F128:
  case X86::KSET0D:
  case X86::KSET0Q:
  case X86::KSET0W:
  case X86::KSET1D:
  case X86::KSET1Q:
  case X86::KSET1W:
  case X86::MMX_SET0:
  case X86::MOV32ImmSExti8:
  case X86::MOV32r0:
  case X86::MOV32r1:
  case X86::MOV32r_1:
  case X86::MOV32ri64:
  case X86::MOV64ImmSExti8:
  case X86::V_SET0:
  case X86::V_SETALLONES:
  case X86::MOV16ri:
  case X86::MOV32ri:
  case X86::MOV64ri:
  case X86::MOV64ri32:
  case X86::MOV8ri:
  case X86::PTILEZEROV:
    return true;

  case X86::MOV8rm:
  case X86::MOV8rm_NOREX:
  case X86::MOV16rm:
  case X86::MOV32rm:
  case X86::MOV64rm:
  case X86::MOVSSrm:
  case X86::MOVSSrm_alt:
  case X86::MOVSDrm:
  case X86::MOVSDrm_alt:
  case X86::MOVAPSrm:
  case X86::MOVUPSrm:
  case X86::MOVAPDrm:
  case X86::MOVUPDrm:
  case X86::MOVDQArm:
  case X86::MOVDQUrm:
  case X86::VMOVSSrm:
  case X86::VMOVSSrm_alt:
  case X86::VMOVSDrm:
  case X86::VMOVSDrm_alt:
  case X86::VMOVAPSrm:
  case X86::VMOVUPSrm:
  case X86::VMOVAPDrm:
  case X86::VMOVUPDrm:
  case X86::VMOVDQArm:
  case X86::VMOVDQUrm:
  case X86::VMOVAPSYrm:
  case X86::VMOVUPSYrm:
  case X86::VMOVAPDYrm:
  case X86::VMOVUPDYrm:
  case X86::VMOVDQAYrm:
  case X86::VMOVDQUYrm:
  case X86::MMX_MOVD64rm:
  case X86::MMX_MOVQ64rm:
  case X86::VBROADCASTSSrm:
  case X86::VBROADCASTSSYrm:
  case X86::VBROADCASTSDYrm:
  // AVX-512
  case X86::VPBROADCASTBZ128rm:
  case X86::VPBROADCASTBZ256rm:
  case X86::VPBROADCASTBZrm:
  case X86::VBROADCASTF32X2Z256rm:
  case X86::VBROADCASTF32X2Zrm:
  case X86::VBROADCASTI32X2Z128rm:
  case X86::VBROADCASTI32X2Z256rm:
  case X86::VBROADCASTI32X2Zrm:
  case X86::VPBROADCASTWZ128rm:
  case X86::VPBROADCASTWZ256rm:
  case X86::VPBROADCASTWZrm:
  case X86::VPBROADCASTDZ128rm:
  case X86::VPBROADCASTDZ256rm:
  case X86::VPBROADCASTDZrm:
  case X86::VBROADCASTSSZ128rm:
  case X86::VBROADCASTSSZ256rm:
  case X86::VBROADCASTSSZrm:
  case X86::VPBROADCASTQZ128rm:
  case X86::VPBROADCASTQZ256rm:
  case X86::VPBROADCASTQZrm:
  case X86::VBROADCASTSDZ256rm:
  case X86::VBROADCASTSDZrm:
  case X86::VMOVSSZrm:
  case X86::VMOVSSZrm_alt:
  case X86::VMOVSDZrm:
  case X86::VMOVSDZrm_alt:
  case X86::VMOVSHZrm:
  case X86::VMOVSHZrm_alt:
  case X86::VMOVAPDZ128rm:
  case X86::VMOVAPDZ256rm:
  case X86::VMOVAPDZrm:
  case X86::VMOVAPSZ128rm:
  case X86::VMOVAPSZ256rm:
  case X86::VMOVAPSZ128rm_NOVLX:
  case X86::VMOVAPSZ256rm_NOVLX:
  case X86::VMOVAPSZrm:
  case X86::VMOVDQA32Z128rm:
  case X86::VMOVDQA32Z256rm:
  case X86::VMOVDQA32Zrm:
  case X86::VMOVDQA64Z128rm:
  case X86::VMOVDQA64Z256rm:
  case X86::VMOVDQA64Zrm:
  case X86::VMOVDQU16Z128rm:
  case X86::VMOVDQU16Z256rm:
  case X86::VMOVDQU16Zrm:
  case X86::VMOVDQU32Z128rm:
  case X86::VMOVDQU32Z256rm:
  case X86::VMOVDQU32Zrm:
  case X86::VMOVDQU64Z128rm:
  case X86::VMOVDQU64Z256rm:
  case X86::VMOVDQU64Zrm:
  case X86::VMOVDQU8Z128rm:
  case X86::VMOVDQU8Z256rm:
  case X86::VMOVDQU8Zrm:
  case X86::VMOVUPDZ128rm:
  case X86::VMOVUPDZ256rm:
  case X86::VMOVUPDZrm:
  case X86::VMOVUPSZ128rm:
  case X86::VMOVUPSZ256rm:
  case X86::VMOVUPSZ128rm_NOVLX:
  case X86::VMOVUPSZ256rm_NOVLX:
  case X86::VMOVUPSZrm: {
    // Loads from constant pools are trivially rematerializable.
    if (MI.getOperand(1 + X86::AddrBaseReg).isReg() &&
        MI.getOperand(1 + X86::AddrScaleAmt).isImm() &&
        MI.getOperand(1 + X86::AddrIndexReg).isReg() &&
        MI.getOperand(1 + X86::AddrIndexReg).getReg() == 0 &&
        MI.isDereferenceableInvariantLoad()) {
      Register BaseReg = MI.getOperand(1 + X86::AddrBaseReg).getReg();
      if (BaseReg == 0 || BaseReg == X86::RIP)
        return true;
      // Allow re-materialization of PIC load.
      if (!(!ReMatPICStubLoad && MI.getOperand(1 + X86::AddrDisp).isGlobal())) {
        const MachineFunction &MF = *MI.getParent()->getParent();
        const MachineRegisterInfo &MRI = MF.getRegInfo();
        if (regIsPICBase(BaseReg, MRI))
          return true;
      }
    }
    break;
  }

  case X86::LEA32r:
  case X86::LEA64r: {
    if (MI.getOperand(1 + X86::AddrScaleAmt).isImm() &&
        MI.getOperand(1 + X86::AddrIndexReg).isReg() &&
        MI.getOperand(1 + X86::AddrIndexReg).getReg() == 0 &&
        !MI.getOperand(1 + X86::AddrDisp).isReg()) {
      // lea fi#, lea GV, etc. are all rematerializable.
      if (!MI.getOperand(1 + X86::AddrBaseReg).isReg())
        return true;
      Register BaseReg = MI.getOperand(1 + X86::AddrBaseReg).getReg();
      if (BaseReg == 0)
        return true;
      // Allow re-materialization of lea PICBase + x.
      const MachineFunction &MF = *MI.getParent()->getParent();
      const MachineRegisterInfo &MRI = MF.getRegInfo();
      if (regIsPICBase(BaseReg, MRI))
        return true;
    }
    break;
  }
  }
  return TargetInstrInfo::isReallyTriviallyReMaterializable(MI);
}

void X86InstrInfo::reMaterialize(MachineBasicBlock &MBB,
                                 MachineBasicBlock::iterator I,
                                 Register DestReg, unsigned SubIdx,
                                 const MachineInstr &Orig,
                                 const TargetRegisterInfo &TRI) const {
  bool ClobbersEFLAGS = Orig.modifiesRegister(X86::EFLAGS, &TRI);
  if (ClobbersEFLAGS && MBB.computeRegisterLiveness(&TRI, X86::EFLAGS, I) !=
                            MachineBasicBlock::LQR_Dead) {
    // The instruction clobbers EFLAGS. Re-materialize as MOV32ri to avoid side
    // effects.
    int Value;
    switch (Orig.getOpcode()) {
    case X86::MOV32r0:
      Value = 0;
      break;
    case X86::MOV32r1:
      Value = 1;
      break;
    case X86::MOV32r_1:
      Value = -1;
      break;
    default:
      llvm_unreachable("Unexpected instruction!");
    }

    const DebugLoc &DL = Orig.getDebugLoc();
    BuildMI(MBB, I, DL, get(X86::MOV32ri))
        .add(Orig.getOperand(0))
        .addImm(Value);
  } else {
    MachineInstr *MI = MBB.getParent()->CloneMachineInstr(&Orig);
    MBB.insert(I, MI);
  }

  MachineInstr &NewMI = *std::prev(I);
  NewMI.substituteRegister(Orig.getOperand(0).getReg(), DestReg, SubIdx, TRI);
}

/// True if MI has a condition code def, e.g. EFLAGS, that is not marked dead.
bool X86InstrInfo::hasLiveCondCodeDef(MachineInstr &MI) const {
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isReg() && MO.isDef() && MO.getReg() == X86::EFLAGS &&
        !MO.isDead()) {
      return true;
    }
  }
  return false;
}

/// Check whether the shift count for a machine operand is non-zero.
inline static unsigned getTruncatedShiftCount(const MachineInstr &MI,
                                              unsigned ShiftAmtOperandIdx) {
  // The shift count is six bits with the REX.W prefix and five bits without.
  unsigned ShiftCountMask = (MI.getDesc().TSFlags & X86II::REX_W) ? 63 : 31;
  unsigned Imm = MI.getOperand(ShiftAmtOperandIdx).getImm();
  return Imm & ShiftCountMask;
}

/// Check whether the given shift count is appropriate
/// can be represented by a LEA instruction.
inline static bool isTruncatedShiftCountForLEA(unsigned ShAmt) {
  // Left shift instructions can be transformed into load-effective-address
  // instructions if we can encode them appropriately.
  // A LEA instruction utilizes a SIB byte to encode its scale factor.
  // The SIB.scale field is two bits wide which means that we can encode any
  // shift amount less than 4.
  return ShAmt < 4 && ShAmt > 0;
}

static bool findRedundantFlagInstr(MachineInstr &CmpInstr,
                                   MachineInstr &CmpValDefInstr,
                                   const MachineRegisterInfo *MRI,
                                   MachineInstr **AndInstr,
                                   const TargetRegisterInfo *TRI,
                                   bool &NoSignFlag, bool &ClearsOverflowFlag) {
  if (!(CmpValDefInstr.getOpcode() == X86::SUBREG_TO_REG &&
        CmpInstr.getOpcode() == X86::TEST64rr) &&
      !(CmpValDefInstr.getOpcode() == X86::COPY &&
        CmpInstr.getOpcode() == X86::TEST16rr))
    return false;

  // CmpInstr is a TEST16rr/TEST64rr instruction, and
  // `X86InstrInfo::analyzeCompare` guarantees that it's analyzable only if two
  // registers are identical.
  assert((CmpInstr.getOperand(0).getReg() == CmpInstr.getOperand(1).getReg()) &&
         "CmpInstr is an analyzable TEST16rr/TEST64rr, and "
         "`X86InstrInfo::analyzeCompare` requires two reg operands are the"
         "same.");

  // Caller (`X86InstrInfo::optimizeCompareInstr`) guarantees that
  // `CmpValDefInstr` defines the value that's used by `CmpInstr`; in this case
  // if `CmpValDefInstr` sets the EFLAGS, it is likely that `CmpInstr` is
  // redundant.
  assert(
      (MRI->getVRegDef(CmpInstr.getOperand(0).getReg()) == &CmpValDefInstr) &&
      "Caller guarantees that TEST64rr is a user of SUBREG_TO_REG or TEST16rr "
      "is a user of COPY sub16bit.");
  MachineInstr *VregDefInstr = nullptr;
  if (CmpInstr.getOpcode() == X86::TEST16rr) {
    if (!CmpValDefInstr.getOperand(1).getReg().isVirtual())
      return false;
    VregDefInstr = MRI->getVRegDef(CmpValDefInstr.getOperand(1).getReg());
    if (!VregDefInstr)
      return false;
    // We can only remove test when AND32ri or AND64ri32 whose imm can fit 16bit
    // size, others 32/64 bit ops would test higher bits which test16rr don't
    // want to.
    if (!((VregDefInstr->getOpcode() == X86::AND32ri ||
           VregDefInstr->getOpcode() == X86::AND64ri32) &&
          isUInt<16>(VregDefInstr->getOperand(2).getImm())))
      return false;
  }

  if (CmpInstr.getOpcode() == X86::TEST64rr) {
    // As seen in X86 td files, CmpValDefInstr.getOperand(1).getImm() is
    // typically 0.
    if (CmpValDefInstr.getOperand(1).getImm() != 0)
      return false;

    // As seen in X86 td files, CmpValDefInstr.getOperand(3) is typically
    // sub_32bit or sub_xmm.
    if (CmpValDefInstr.getOperand(3).getImm() != X86::sub_32bit)
      return false;

    VregDefInstr = MRI->getVRegDef(CmpValDefInstr.getOperand(2).getReg());
  }

  assert(VregDefInstr && "Must have a definition (SSA)");

  // Requires `CmpValDefInstr` and `VregDefInstr` are from the same MBB
  // to simplify the subsequent analysis.
  //
  // FIXME: If `VregDefInstr->getParent()` is the only predecessor of
  // `CmpValDefInstr.getParent()`, this could be handled.
  if (VregDefInstr->getParent() != CmpValDefInstr.getParent())
    return false;

  if (X86::isAND(VregDefInstr->getOpcode())) {
    // Get a sequence of instructions like
    //   %reg = and* ...                    // Set EFLAGS
    //   ...                                // EFLAGS not changed
    //   %extended_reg = subreg_to_reg 0, %reg, %subreg.sub_32bit
    //   test64rr %extended_reg, %extended_reg, implicit-def $eflags
    // or
    //   %reg = and32* ...
    //   ...                         // EFLAGS not changed.
    //   %src_reg = copy %reg.sub_16bit:gr32
    //   test16rr %src_reg, %src_reg, implicit-def $eflags
    //
    // If subsequent readers use a subset of bits that don't change
    // after `and*` instructions, it's likely that the test64rr could
    // be optimized away.
    for (const MachineInstr &Instr :
         make_range(std::next(MachineBasicBlock::iterator(VregDefInstr)),
                    MachineBasicBlock::iterator(CmpValDefInstr))) {
      // There are instructions between 'VregDefInstr' and
      // 'CmpValDefInstr' that modifies EFLAGS.
      if (Instr.modifiesRegister(X86::EFLAGS, TRI))
        return false;
    }

    *AndInstr = VregDefInstr;

    // AND instruction will essentially update SF and clear OF, so
    // NoSignFlag should be false in the sense that SF is modified by `AND`.
    //
    // However, the implementation artifically sets `NoSignFlag` to true
    // to poison the SF bit; that is to say, if SF is looked at later, the
    // optimization (to erase TEST64rr) will be disabled.
    //
    // The reason to poison SF bit is that SF bit value could be different
    // in the `AND` and `TEST` operation; signed bit is not known for `AND`,
    // and is known to be 0 as a result of `TEST64rr`.
    //
    // FIXME: As opposed to poisoning the SF bit directly, consider peeking into
    // the AND instruction and using the static information to guide peephole
    // optimization if possible. For example, it's possible to fold a
    // conditional move into a copy if the relevant EFLAG bits could be deduced
    // from an immediate operand of and operation.
    //
    NoSignFlag = true;
    // ClearsOverflowFlag is true for AND operation (no surprise).
    ClearsOverflowFlag = true;
    return true;
  }
  return false;
}

bool X86InstrInfo::classifyLEAReg(MachineInstr &MI, const MachineOperand &Src,
                                  unsigned Opc, bool AllowSP, Register &NewSrc,
                                  bool &isKill, MachineOperand &ImplicitOp,
                                  LiveVariables *LV, LiveIntervals *LIS) const {
  MachineFunction &MF = *MI.getParent()->getParent();
  const TargetRegisterClass *RC;
  if (AllowSP) {
    RC = Opc != X86::LEA32r ? &X86::GR64RegClass : &X86::GR32RegClass;
  } else {
    RC = Opc != X86::LEA32r ? &X86::GR64_NOSPRegClass : &X86::GR32_NOSPRegClass;
  }
  Register SrcReg = Src.getReg();
  isKill = MI.killsRegister(SrcReg, /*TRI=*/nullptr);

  // For both LEA64 and LEA32 the register already has essentially the right
  // type (32-bit or 64-bit) we may just need to forbid SP.
  if (Opc != X86::LEA64_32r) {
    NewSrc = SrcReg;
    assert(!Src.isUndef() && "Undef op doesn't need optimization");

    if (NewSrc.isVirtual() && !MF.getRegInfo().constrainRegClass(NewSrc, RC))
      return false;

    return true;
  }

  // This is for an LEA64_32r and incoming registers are 32-bit. One way or
  // another we need to add 64-bit registers to the final MI.
  if (SrcReg.isPhysical()) {
    ImplicitOp = Src;
    ImplicitOp.setImplicit();

    NewSrc = getX86SubSuperRegister(SrcReg, 64);
    assert(NewSrc.isValid() && "Invalid Operand");
    assert(!Src.isUndef() && "Undef op doesn't need optimization");
  } else {
    // Virtual register of the wrong class, we have to create a temporary 64-bit
    // vreg to feed into the LEA.
    NewSrc = MF.getRegInfo().createVirtualRegister(RC);
    MachineInstr *Copy =
        BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), get(TargetOpcode::COPY))
            .addReg(NewSrc, RegState::Define | RegState::Undef, X86::sub_32bit)
            .addReg(SrcReg, getKillRegState(isKill));

    // Which is obviously going to be dead after we're done with it.
    isKill = true;

    if (LV)
      LV->replaceKillInstruction(SrcReg, MI, *Copy);

    if (LIS) {
      SlotIndex CopyIdx = LIS->InsertMachineInstrInMaps(*Copy);
      SlotIndex Idx = LIS->getInstructionIndex(MI);
      LiveInterval &LI = LIS->getInterval(SrcReg);
      LiveRange::Segment *S = LI.getSegmentContaining(Idx);
      if (S->end.getBaseIndex() == Idx)
        S->end = CopyIdx.getRegSlot();
    }
  }

  // We've set all the parameters without issue.
  return true;
}

MachineInstr *X86InstrInfo::convertToThreeAddressWithLEA(unsigned MIOpc,
                                                         MachineInstr &MI,
                                                         LiveVariables *LV,
                                                         LiveIntervals *LIS,
                                                         bool Is8BitOp) const {
  // We handle 8-bit adds and various 16-bit opcodes in the switch below.
  MachineBasicBlock &MBB = *MI.getParent();
  MachineRegisterInfo &RegInfo = MBB.getParent()->getRegInfo();
  assert((Is8BitOp ||
          RegInfo.getTargetRegisterInfo()->getRegSizeInBits(
              *RegInfo.getRegClass(MI.getOperand(0).getReg())) == 16) &&
         "Unexpected type for LEA transform");

  // TODO: For a 32-bit target, we need to adjust the LEA variables with
  // something like this:
  //   Opcode = X86::LEA32r;
  //   InRegLEA = RegInfo.createVirtualRegister(&X86::GR32_NOSPRegClass);
  //   OutRegLEA =
  //       Is8BitOp ? RegInfo.createVirtualRegister(&X86::GR32ABCD_RegClass)
  //                : RegInfo.createVirtualRegister(&X86::GR32RegClass);
  if (!Subtarget.is64Bit())
    return nullptr;

  unsigned Opcode = X86::LEA64_32r;
  Register InRegLEA = RegInfo.createVirtualRegister(&X86::GR64_NOSPRegClass);
  Register OutRegLEA = RegInfo.createVirtualRegister(&X86::GR32RegClass);
  Register InRegLEA2;

  // Build and insert into an implicit UNDEF value. This is OK because
  // we will be shifting and then extracting the lower 8/16-bits.
  // This has the potential to cause partial register stall. e.g.
  //   movw    (%rbp,%rcx,2), %dx
  //   leal    -65(%rdx), %esi
  // But testing has shown this *does* help performance in 64-bit mode (at
  // least on modern x86 machines).
  MachineBasicBlock::iterator MBBI = MI.getIterator();
  Register Dest = MI.getOperand(0).getReg();
  Register Src = MI.getOperand(1).getReg();
  Register Src2;
  bool IsDead = MI.getOperand(0).isDead();
  bool IsKill = MI.getOperand(1).isKill();
  unsigned SubReg = Is8BitOp ? X86::sub_8bit : X86::sub_16bit;
  assert(!MI.getOperand(1).isUndef() && "Undef op doesn't need optimization");
  MachineInstr *ImpDef =
      BuildMI(MBB, MBBI, MI.getDebugLoc(), get(X86::IMPLICIT_DEF), InRegLEA);
  MachineInstr *InsMI =
      BuildMI(MBB, MBBI, MI.getDebugLoc(), get(TargetOpcode::COPY))
          .addReg(InRegLEA, RegState::Define, SubReg)
          .addReg(Src, getKillRegState(IsKill));
  MachineInstr *ImpDef2 = nullptr;
  MachineInstr *InsMI2 = nullptr;

  MachineInstrBuilder MIB =
      BuildMI(MBB, MBBI, MI.getDebugLoc(), get(Opcode), OutRegLEA);
  switch (MIOpc) {
  default:
    llvm_unreachable("Unreachable!");
  case X86::SHL8ri:
  case X86::SHL16ri: {
    unsigned ShAmt = MI.getOperand(2).getImm();
    MIB.addReg(0)
        .addImm(1LL << ShAmt)
        .addReg(InRegLEA, RegState::Kill)
        .addImm(0)
        .addReg(0);
    break;
  }
  case X86::INC8r:
  case X86::INC16r:
    addRegOffset(MIB, InRegLEA, true, 1);
    break;
  case X86::DEC8r:
  case X86::DEC16r:
    addRegOffset(MIB, InRegLEA, true, -1);
    break;
  case X86::ADD8ri:
  case X86::ADD8ri_DB:
  case X86::ADD16ri:
  case X86::ADD16ri_DB:
    addRegOffset(MIB, InRegLEA, true, MI.getOperand(2).getImm());
    break;
  case X86::ADD8rr:
  case X86::ADD8rr_DB:
  case X86::ADD16rr:
  case X86::ADD16rr_DB: {
    Src2 = MI.getOperand(2).getReg();
    bool IsKill2 = MI.getOperand(2).isKill();
    assert(!MI.getOperand(2).isUndef() && "Undef op doesn't need optimization");
    if (Src == Src2) {
      // ADD8rr/ADD16rr killed %reg1028, %reg1028
      // just a single insert_subreg.
      addRegReg(MIB, InRegLEA, true, InRegLEA, false);
    } else {
      if (Subtarget.is64Bit())
        InRegLEA2 = RegInfo.createVirtualRegister(&X86::GR64_NOSPRegClass);
      else
        InRegLEA2 = RegInfo.createVirtualRegister(&X86::GR32_NOSPRegClass);
      // Build and insert into an implicit UNDEF value. This is OK because
      // we will be shifting and then extracting the lower 8/16-bits.
      ImpDef2 = BuildMI(MBB, &*MIB, MI.getDebugLoc(), get(X86::IMPLICIT_DEF),
                        InRegLEA2);
      InsMI2 = BuildMI(MBB, &*MIB, MI.getDebugLoc(), get(TargetOpcode::COPY))
                   .addReg(InRegLEA2, RegState::Define, SubReg)
                   .addReg(Src2, getKillRegState(IsKill2));
      addRegReg(MIB, InRegLEA, true, InRegLEA2, true);
    }
    if (LV && IsKill2 && InsMI2)
      LV->replaceKillInstruction(Src2, MI, *InsMI2);
    break;
  }
  }

  MachineInstr *NewMI = MIB;
  MachineInstr *ExtMI =
      BuildMI(MBB, MBBI, MI.getDebugLoc(), get(TargetOpcode::COPY))
          .addReg(Dest, RegState::Define | getDeadRegState(IsDead))
          .addReg(OutRegLEA, RegState::Kill, SubReg);

  if (LV) {
    // Update live variables.
    LV->getVarInfo(InRegLEA).Kills.push_back(NewMI);
    if (InRegLEA2)
      LV->getVarInfo(InRegLEA2).Kills.push_back(NewMI);
    LV->getVarInfo(OutRegLEA).Kills.push_back(ExtMI);
    if (IsKill)
      LV->replaceKillInstruction(Src, MI, *InsMI);
    if (IsDead)
      LV->replaceKillInstruction(Dest, MI, *ExtMI);
  }

  if (LIS) {
    LIS->InsertMachineInstrInMaps(*ImpDef);
    SlotIndex InsIdx = LIS->InsertMachineInstrInMaps(*InsMI);
    if (ImpDef2)
      LIS->InsertMachineInstrInMaps(*ImpDef2);
    SlotIndex Ins2Idx;
    if (InsMI2)
      Ins2Idx = LIS->InsertMachineInstrInMaps(*InsMI2);
    SlotIndex NewIdx = LIS->ReplaceMachineInstrInMaps(MI, *NewMI);
    SlotIndex ExtIdx = LIS->InsertMachineInstrInMaps(*ExtMI);
    LIS->getInterval(InRegLEA);
    LIS->getInterval(OutRegLEA);
    if (InRegLEA2)
      LIS->getInterval(InRegLEA2);

    // Move the use of Src up to InsMI.
    LiveInterval &SrcLI = LIS->getInterval(Src);
    LiveRange::Segment *SrcSeg = SrcLI.getSegmentContaining(NewIdx);
    if (SrcSeg->end == NewIdx.getRegSlot())
      SrcSeg->end = InsIdx.getRegSlot();

    if (InsMI2) {
      // Move the use of Src2 up to InsMI2.
      LiveInterval &Src2LI = LIS->getInterval(Src2);
      LiveRange::Segment *Src2Seg = Src2LI.getSegmentContaining(NewIdx);
      if (Src2Seg->end == NewIdx.getRegSlot())
        Src2Seg->end = Ins2Idx.getRegSlot();
    }

    // Move the definition of Dest down to ExtMI.
    LiveInterval &DestLI = LIS->getInterval(Dest);
    LiveRange::Segment *DestSeg =
        DestLI.getSegmentContaining(NewIdx.getRegSlot());
    assert(DestSeg->start == NewIdx.getRegSlot() &&
           DestSeg->valno->def == NewIdx.getRegSlot());
    DestSeg->start = ExtIdx.getRegSlot();
    DestSeg->valno->def = ExtIdx.getRegSlot();
  }

  return ExtMI;
}

/// This method must be implemented by targets that
/// set the M_CONVERTIBLE_TO_3_ADDR flag.  When this flag is set, the target
/// may be able to convert a two-address instruction into a true
/// three-address instruction on demand.  This allows the X86 target (for
/// example) to convert ADD and SHL instructions into LEA instructions if they
/// would require register copies due to two-addressness.
///
/// This method returns a null pointer if the transformation cannot be
/// performed, otherwise it returns the new instruction.
///
MachineInstr *X86InstrInfo::convertToThreeAddress(MachineInstr &MI,
                                                  LiveVariables *LV,
                                                  LiveIntervals *LIS) const {
  // The following opcodes also sets the condition code register(s). Only
  // convert them to equivalent lea if the condition code register def's
  // are dead!
  if (hasLiveCondCodeDef(MI))
    return nullptr;

  MachineFunction &MF = *MI.getParent()->getParent();
  // All instructions input are two-addr instructions.  Get the known operands.
  const MachineOperand &Dest = MI.getOperand(0);
  const MachineOperand &Src = MI.getOperand(1);

  // Ideally, operations with undef should be folded before we get here, but we
  // can't guarantee it. Bail out because optimizing undefs is a waste of time.
  // Without this, we have to forward undef state to new register operands to
  // avoid machine verifier errors.
  if (Src.isUndef())
    return nullptr;
  if (MI.getNumOperands() > 2)
    if (MI.getOperand(2).isReg() && MI.getOperand(2).isUndef())
      return nullptr;

  MachineInstr *NewMI = nullptr;
  Register SrcReg, SrcReg2;
  bool Is64Bit = Subtarget.is64Bit();

  bool Is8BitOp = false;
  unsigned NumRegOperands = 2;
  unsigned MIOpc = MI.getOpcode();
  switch (MIOpc) {
  default:
    llvm_unreachable("Unreachable!");
  case X86::SHL64ri: {
    assert(MI.getNumOperands() >= 3 && "Unknown shift instruction!");
    unsigned ShAmt = getTruncatedShiftCount(MI, 2);
    if (!isTruncatedShiftCountForLEA(ShAmt))
      return nullptr;

    // LEA can't handle RSP.
    if (Src.getReg().isVirtual() && !MF.getRegInfo().constrainRegClass(
                                        Src.getReg(), &X86::GR64_NOSPRegClass))
      return nullptr;

    NewMI = BuildMI(MF, MI.getDebugLoc(), get(X86::LEA64r))
                .add(Dest)
                .addReg(0)
                .addImm(1LL << ShAmt)
                .add(Src)
                .addImm(0)
                .addReg(0);
    break;
  }
  case X86::SHL32ri: {
    assert(MI.getNumOperands() >= 3 && "Unknown shift instruction!");
    unsigned ShAmt = getTruncatedShiftCount(MI, 2);
    if (!isTruncatedShiftCountForLEA(ShAmt))
      return nullptr;

    unsigned Opc = Is64Bit ? X86::LEA64_32r : X86::LEA32r;

    // LEA can't handle ESP.
    bool isKill;
    MachineOperand ImplicitOp = MachineOperand::CreateReg(0, false);
    if (!classifyLEAReg(MI, Src, Opc, /*AllowSP=*/false, SrcReg, isKill,
                        ImplicitOp, LV, LIS))
      return nullptr;

    MachineInstrBuilder MIB = BuildMI(MF, MI.getDebugLoc(), get(Opc))
                                  .add(Dest)
                                  .addReg(0)
                                  .addImm(1LL << ShAmt)
                                  .addReg(SrcReg, getKillRegState(isKill))
                                  .addImm(0)
                                  .addReg(0);
    if (ImplicitOp.getReg() != 0)
      MIB.add(ImplicitOp);
    NewMI = MIB;

    // Add kills if classifyLEAReg created a new register.
    if (LV && SrcReg != Src.getReg())
      LV->getVarInfo(SrcReg).Kills.push_back(NewMI);
    break;
  }
  case X86::SHL8ri:
    Is8BitOp = true;
    [[fallthrough]];
  case X86::SHL16ri: {
    assert(MI.getNumOperands() >= 3 && "Unknown shift instruction!");
    unsigned ShAmt = getTruncatedShiftCount(MI, 2);
    if (!isTruncatedShiftCountForLEA(ShAmt))
      return nullptr;
    return convertToThreeAddressWithLEA(MIOpc, MI, LV, LIS, Is8BitOp);
  }
  case X86::INC64r:
  case X86::INC32r: {
    assert(MI.getNumOperands() >= 2 && "Unknown inc instruction!");
    unsigned Opc = MIOpc == X86::INC64r
                       ? X86::LEA64r
                       : (Is64Bit ? X86::LEA64_32r : X86::LEA32r);
    bool isKill;
    MachineOperand ImplicitOp = MachineOperand::CreateReg(0, false);
    if (!classifyLEAReg(MI, Src, Opc, /*AllowSP=*/false, SrcReg, isKill,
                        ImplicitOp, LV, LIS))
      return nullptr;

    MachineInstrBuilder MIB = BuildMI(MF, MI.getDebugLoc(), get(Opc))
                                  .add(Dest)
                                  .addReg(SrcReg, getKillRegState(isKill));
    if (ImplicitOp.getReg() != 0)
      MIB.add(ImplicitOp);

    NewMI = addOffset(MIB, 1);

    // Add kills if classifyLEAReg created a new register.
    if (LV && SrcReg != Src.getReg())
      LV->getVarInfo(SrcReg).Kills.push_back(NewMI);
    break;
  }
  case X86::DEC64r:
  case X86::DEC32r: {
    assert(MI.getNumOperands() >= 2 && "Unknown dec instruction!");
    unsigned Opc = MIOpc == X86::DEC64r
                       ? X86::LEA64r
                       : (Is64Bit ? X86::LEA64_32r : X86::LEA32r);

    bool isKill;
    MachineOperand ImplicitOp = MachineOperand::CreateReg(0, false);
    if (!classifyLEAReg(MI, Src, Opc, /*AllowSP=*/false, SrcReg, isKill,
                        ImplicitOp, LV, LIS))
      return nullptr;

    MachineInstrBuilder MIB = BuildMI(MF, MI.getDebugLoc(), get(Opc))
                                  .add(Dest)
                                  .addReg(SrcReg, getKillRegState(isKill));
    if (ImplicitOp.getReg() != 0)
      MIB.add(ImplicitOp);

    NewMI = addOffset(MIB, -1);

    // Add kills if classifyLEAReg created a new register.
    if (LV && SrcReg != Src.getReg())
      LV->getVarInfo(SrcReg).Kills.push_back(NewMI);
    break;
  }
  case X86::DEC8r:
  case X86::INC8r:
    Is8BitOp = true;
    [[fallthrough]];
  case X86::DEC16r:
  case X86::INC16r:
    return convertToThreeAddressWithLEA(MIOpc, MI, LV, LIS, Is8BitOp);
  case X86::ADD64rr:
  case X86::ADD64rr_DB:
  case X86::ADD32rr:
  case X86::ADD32rr_DB: {
    assert(MI.getNumOperands() >= 3 && "Unknown add instruction!");
    unsigned Opc;
    if (MIOpc == X86::ADD64rr || MIOpc == X86::ADD64rr_DB)
      Opc = X86::LEA64r;
    else
      Opc = Is64Bit ? X86::LEA64_32r : X86::LEA32r;

    const MachineOperand &Src2 = MI.getOperand(2);
    bool isKill2;
    MachineOperand ImplicitOp2 = MachineOperand::CreateReg(0, false);
    if (!classifyLEAReg(MI, Src2, Opc, /*AllowSP=*/false, SrcReg2, isKill2,
                        ImplicitOp2, LV, LIS))
      return nullptr;

    bool isKill;
    MachineOperand ImplicitOp = MachineOperand::CreateReg(0, false);
    if (Src.getReg() == Src2.getReg()) {
      // Don't call classify LEAReg a second time on the same register, in case
      // the first call inserted a COPY from Src2 and marked it as killed.
      isKill = isKill2;
      SrcReg = SrcReg2;
    } else {
      if (!classifyLEAReg(MI, Src, Opc, /*AllowSP=*/true, SrcReg, isKill,
                          ImplicitOp, LV, LIS))
        return nullptr;
    }

    MachineInstrBuilder MIB = BuildMI(MF, MI.getDebugLoc(), get(Opc)).add(Dest);
    if (ImplicitOp.getReg() != 0)
      MIB.add(ImplicitOp);
    if (ImplicitOp2.getReg() != 0)
      MIB.add(ImplicitOp2);

    NewMI = addRegReg(MIB, SrcReg, isKill, SrcReg2, isKill2);

    // Add kills if classifyLEAReg created a new register.
    if (LV) {
      if (SrcReg2 != Src2.getReg())
        LV->getVarInfo(SrcReg2).Kills.push_back(NewMI);
      if (SrcReg != SrcReg2 && SrcReg != Src.getReg())
        LV->getVarInfo(SrcReg).Kills.push_back(NewMI);
    }
    NumRegOperands = 3;
    break;
  }
  case X86::ADD8rr:
  case X86::ADD8rr_DB:
    Is8BitOp = true;
    [[fallthrough]];
  case X86::ADD16rr:
  case X86::ADD16rr_DB:
    return convertToThreeAddressWithLEA(MIOpc, MI, LV, LIS, Is8BitOp);
  case X86::ADD64ri32:
  case X86::ADD64ri32_DB:
    assert(MI.getNumOperands() >= 3 && "Unknown add instruction!");
    NewMI = addOffset(
        BuildMI(MF, MI.getDebugLoc(), get(X86::LEA64r)).add(Dest).add(Src),
        MI.getOperand(2));
    break;
  case X86::ADD32ri:
  case X86::ADD32ri_DB: {
    assert(MI.getNumOperands() >= 3 && "Unknown add instruction!");
    unsigned Opc = Is64Bit ? X86::LEA64_32r : X86::LEA32r;

    bool isKill;
    MachineOperand ImplicitOp = MachineOperand::CreateReg(0, false);
    if (!classifyLEAReg(MI, Src, Opc, /*AllowSP=*/true, SrcReg, isKill,
                        ImplicitOp, LV, LIS))
      return nullptr;

    MachineInstrBuilder MIB = BuildMI(MF, MI.getDebugLoc(), get(Opc))
                                  .add(Dest)
                                  .addReg(SrcReg, getKillRegState(isKill));
    if (ImplicitOp.getReg() != 0)
      MIB.add(ImplicitOp);

    NewMI = addOffset(MIB, MI.getOperand(2));

    // Add kills if classifyLEAReg created a new register.
    if (LV && SrcReg != Src.getReg())
      LV->getVarInfo(SrcReg).Kills.push_back(NewMI);
    break;
  }
  case X86::ADD8ri:
  case X86::ADD8ri_DB:
    Is8BitOp = true;
    [[fallthrough]];
  case X86::ADD16ri:
  case X86::ADD16ri_DB:
    return convertToThreeAddressWithLEA(MIOpc, MI, LV, LIS, Is8BitOp);
  case X86::SUB8ri:
  case X86::SUB16ri:
    /// FIXME: Support these similar to ADD8ri/ADD16ri*.
    return nullptr;
  case X86::SUB32ri: {
    if (!MI.getOperand(2).isImm())
      return nullptr;
    int64_t Imm = MI.getOperand(2).getImm();
    if (!isInt<32>(-Imm))
      return nullptr;

    assert(MI.getNumOperands() >= 3 && "Unknown add instruction!");
    unsigned Opc = Is64Bit ? X86::LEA64_32r : X86::LEA32r;

    bool isKill;
    MachineOperand ImplicitOp = MachineOperand::CreateReg(0, false);
    if (!classifyLEAReg(MI, Src, Opc, /*AllowSP=*/true, SrcReg, isKill,
                        ImplicitOp, LV, LIS))
      return nullptr;

    MachineInstrBuilder MIB = BuildMI(MF, MI.getDebugLoc(), get(Opc))
                                  .add(Dest)
                                  .addReg(SrcReg, getKillRegState(isKill));
    if (ImplicitOp.getReg() != 0)
      MIB.add(ImplicitOp);

    NewMI = addOffset(MIB, -Imm);

    // Add kills if classifyLEAReg created a new register.
    if (LV && SrcReg != Src.getReg())
      LV->getVarInfo(SrcReg).Kills.push_back(NewMI);
    break;
  }

  case X86::SUB64ri32: {
    if (!MI.getOperand(2).isImm())
      return nullptr;
    int64_t Imm = MI.getOperand(2).getImm();
    if (!isInt<32>(-Imm))
      return nullptr;

    assert(MI.getNumOperands() >= 3 && "Unknown sub instruction!");

    MachineInstrBuilder MIB =
        BuildMI(MF, MI.getDebugLoc(), get(X86::LEA64r)).add(Dest).add(Src);
    NewMI = addOffset(MIB, -Imm);
    break;
  }

  case X86::VMOVDQU8Z128rmk:
  case X86::VMOVDQU8Z256rmk:
  case X86::VMOVDQU8Zrmk:
  case X86::VMOVDQU16Z128rmk:
  case X86::VMOVDQU16Z256rmk:
  case X86::VMOVDQU16Zrmk:
  case X86::VMOVDQU32Z128rmk:
  case X86::VMOVDQA32Z128rmk:
  case X86::VMOVDQU32Z256rmk:
  case X86::VMOVDQA32Z256rmk:
  case X86::VMOVDQU32Zrmk:
  case X86::VMOVDQA32Zrmk:
  case X86::VMOVDQU64Z128rmk:
  case X86::VMOVDQA64Z128rmk:
  case X86::VMOVDQU64Z256rmk:
  case X86::VMOVDQA64Z256rmk:
  case X86::VMOVDQU64Zrmk:
  case X86::VMOVDQA64Zrmk:
  case X86::VMOVUPDZ128rmk:
  case X86::VMOVAPDZ128rmk:
  case X86::VMOVUPDZ256rmk:
  case X86::VMOVAPDZ256rmk:
  case X86::VMOVUPDZrmk:
  case X86::VMOVAPDZrmk:
  case X86::VMOVUPSZ128rmk:
  case X86::VMOVAPSZ128rmk:
  case X86::VMOVUPSZ256rmk:
  case X86::VMOVAPSZ256rmk:
  case X86::VMOVUPSZrmk:
  case X86::VMOVAPSZrmk:
  case X86::VBROADCASTSDZ256rmk:
  case X86::VBROADCASTSDZrmk:
  case X86::VBROADCASTSSZ128rmk:
  case X86::VBROADCASTSSZ256rmk:
  case X86::VBROADCASTSSZrmk:
  case X86::VPBROADCASTDZ128rmk:
  case X86::VPBROADCASTDZ256rmk:
  case X86::VPBROADCASTDZrmk:
  case X86::VPBROADCASTQZ128rmk:
  case X86::VPBROADCASTQZ256rmk:
  case X86::VPBROADCASTQZrmk: {
    unsigned Opc;
    switch (MIOpc) {
    default:
      llvm_unreachable("Unreachable!");
    case X86::VMOVDQU8Z128rmk:
      Opc = X86::VPBLENDMBZ128rmk;
      break;
    case X86::VMOVDQU8Z256rmk:
      Opc = X86::VPBLENDMBZ256rmk;
      break;
    case X86::VMOVDQU8Zrmk:
      Opc = X86::VPBLENDMBZrmk;
      break;
    case X86::VMOVDQU16Z128rmk:
      Opc = X86::VPBLENDMWZ128rmk;
      break;
    case X86::VMOVDQU16Z256rmk:
      Opc = X86::VPBLENDMWZ256rmk;
      break;
    case X86::VMOVDQU16Zrmk:
      Opc = X86::VPBLENDMWZrmk;
      break;
    case X86::VMOVDQU32Z128rmk:
      Opc = X86::VPBLENDMDZ128rmk;
      break;
    case X86::VMOVDQU32Z256rmk:
      Opc = X86::VPBLENDMDZ256rmk;
      break;
    case X86::VMOVDQU32Zrmk:
      Opc = X86::VPBLENDMDZrmk;
      break;
    case X86::VMOVDQU64Z128rmk:
      Opc = X86::VPBLENDMQZ128rmk;
      break;
    case X86::VMOVDQU64Z256rmk:
      Opc = X86::VPBLENDMQZ256rmk;
      break;
    case X86::VMOVDQU64Zrmk:
      Opc = X86::VPBLENDMQZrmk;
      break;
    case X86::VMOVUPDZ128rmk:
      Opc = X86::VBLENDMPDZ128rmk;
      break;
    case X86::VMOVUPDZ256rmk:
      Opc = X86::VBLENDMPDZ256rmk;
      break;
    case X86::VMOVUPDZrmk:
      Opc = X86::VBLENDMPDZrmk;
      break;
    case X86::VMOVUPSZ128rmk:
      Opc = X86::VBLENDMPSZ128rmk;
      break;
    case X86::VMOVUPSZ256rmk:
      Opc = X86::VBLENDMPSZ256rmk;
      break;
    case X86::VMOVUPSZrmk:
      Opc = X86::VBLENDMPSZrmk;
      break;
    case X86::VMOVDQA32Z128rmk:
      Opc = X86::VPBLENDMDZ128rmk;
      break;
    case X86::VMOVDQA32Z256rmk:
      Opc = X86::VPBLENDMDZ256rmk;
      break;
    case X86::VMOVDQA32Zrmk:
      Opc = X86::VPBLENDMDZrmk;
      break;
    case X86::VMOVDQA64Z128rmk:
      Opc = X86::VPBLENDMQZ128rmk;
      break;
    case X86::VMOVDQA64Z256rmk:
      Opc = X86::VPBLENDMQZ256rmk;
      break;
    case X86::VMOVDQA64Zrmk:
      Opc = X86::VPBLENDMQZrmk;
      break;
    case X86::VMOVAPDZ128rmk:
      Opc = X86::VBLENDMPDZ128rmk;
      break;
    case X86::VMOVAPDZ256rmk:
      Opc = X86::VBLENDMPDZ256rmk;
      break;
    case X86::VMOVAPDZrmk:
      Opc = X86::VBLENDMPDZrmk;
      break;
    case X86::VMOVAPSZ128rmk:
      Opc = X86::VBLENDMPSZ128rmk;
      break;
    case X86::VMOVAPSZ256rmk:
      Opc = X86::VBLENDMPSZ256rmk;
      break;
    case X86::VMOVAPSZrmk:
      Opc = X86::VBLENDMPSZrmk;
      break;
    case X86::VBROADCASTSDZ256rmk:
      Opc = X86::VBLENDMPDZ256rmbk;
      break;
    case X86::VBROADCASTSDZrmk:
      Opc = X86::VBLENDMPDZrmbk;
      break;
    case X86::VBROADCASTSSZ128rmk:
      Opc = X86::VBLENDMPSZ128rmbk;
      break;
    case X86::VBROADCASTSSZ256rmk:
      Opc = X86::VBLENDMPSZ256rmbk;
      break;
    case X86::VBROADCASTSSZrmk:
      Opc = X86::VBLENDMPSZrmbk;
      break;
    case X86::VPBROADCASTDZ128rmk:
      Opc = X86::VPBLENDMDZ128rmbk;
      break;
    case X86::VPBROADCASTDZ256rmk:
      Opc = X86::VPBLENDMDZ256rmbk;
      break;
    case X86::VPBROADCASTDZrmk:
      Opc = X86::VPBLENDMDZrmbk;
      break;
    case X86::VPBROADCASTQZ128rmk:
      Opc = X86::VPBLENDMQZ128rmbk;
      break;
    case X86::VPBROADCASTQZ256rmk:
      Opc = X86::VPBLENDMQZ256rmbk;
      break;
    case X86::VPBROADCASTQZrmk:
      Opc = X86::VPBLENDMQZrmbk;
      break;
    }

    NewMI = BuildMI(MF, MI.getDebugLoc(), get(Opc))
                .add(Dest)
                .add(MI.getOperand(2))
                .add(Src)
                .add(MI.getOperand(3))
                .add(MI.getOperand(4))
                .add(MI.getOperand(5))
                .add(MI.getOperand(6))
                .add(MI.getOperand(7));
    NumRegOperands = 4;
    break;
  }

  case X86::VMOVDQU8Z128rrk:
  case X86::VMOVDQU8Z256rrk:
  case X86::VMOVDQU8Zrrk:
  case X86::VMOVDQU16Z128rrk:
  case X86::VMOVDQU16Z256rrk:
  case X86::VMOVDQU16Zrrk:
  case X86::VMOVDQU32Z128rrk:
  case X86::VMOVDQA32Z128rrk:
  case X86::VMOVDQU32Z256rrk:
  case X86::VMOVDQA32Z256rrk:
  case X86::VMOVDQU32Zrrk:
  case X86::VMOVDQA32Zrrk:
  case X86::VMOVDQU64Z128rrk:
  case X86::VMOVDQA64Z128rrk:
  case X86::VMOVDQU64Z256rrk:
  case X86::VMOVDQA64Z256rrk:
  case X86::VMOVDQU64Zrrk:
  case X86::VMOVDQA64Zrrk:
  case X86::VMOVUPDZ128rrk:
  case X86::VMOVAPDZ128rrk:
  case X86::VMOVUPDZ256rrk:
  case X86::VMOVAPDZ256rrk:
  case X86::VMOVUPDZrrk:
  case X86::VMOVAPDZrrk:
  case X86::VMOVUPSZ128rrk:
  case X86::VMOVAPSZ128rrk:
  case X86::VMOVUPSZ256rrk:
  case X86::VMOVAPSZ256rrk:
  case X86::VMOVUPSZrrk:
  case X86::VMOVAPSZrrk: {
    unsigned Opc;
    switch (MIOpc) {
    default:
      llvm_unreachable("Unreachable!");
    case X86::VMOVDQU8Z128rrk:
      Opc = X86::VPBLENDMBZ128rrk;
      break;
    case X86::VMOVDQU8Z256rrk:
      Opc = X86::VPBLENDMBZ256rrk;
      break;
    case X86::VMOVDQU8Zrrk:
      Opc = X86::VPBLENDMBZrrk;
      break;
    case X86::VMOVDQU16Z128rrk:
      Opc = X86::VPBLENDMWZ128rrk;
      break;
    case X86::VMOVDQU16Z256rrk:
      Opc = X86::VPBLENDMWZ256rrk;
      break;
    case X86::VMOVDQU16Zrrk:
      Opc = X86::VPBLENDMWZrrk;
      break;
    case X86::VMOVDQU32Z128rrk:
      Opc = X86::VPBLENDMDZ128rrk;
      break;
    case X86::VMOVDQU32Z256rrk:
      Opc = X86::VPBLENDMDZ256rrk;
      break;
    case X86::VMOVDQU32Zrrk:
      Opc = X86::VPBLENDMDZrrk;
      break;
    case X86::VMOVDQU64Z128rrk:
      Opc = X86::VPBLENDMQZ128rrk;
      break;
    case X86::VMOVDQU64Z256rrk:
      Opc = X86::VPBLENDMQZ256rrk;
      break;
    case X86::VMOVDQU64Zrrk:
      Opc = X86::VPBLENDMQZrrk;
      break;
    case X86::VMOVUPDZ128rrk:
      Opc = X86::VBLENDMPDZ128rrk;
      break;
    case X86::VMOVUPDZ256rrk:
      Opc = X86::VBLENDMPDZ256rrk;
      break;
    case X86::VMOVUPDZrrk:
      Opc = X86::VBLENDMPDZrrk;
      break;
    case X86::VMOVUPSZ128rrk:
      Opc = X86::VBLENDMPSZ128rrk;
      break;
    case X86::VMOVUPSZ256rrk:
      Opc = X86::VBLENDMPSZ256rrk;
      break;
    case X86::VMOVUPSZrrk:
      Opc = X86::VBLENDMPSZrrk;
      break;
    case X86::VMOVDQA32Z128rrk:
      Opc = X86::VPBLENDMDZ128rrk;
      break;
    case X86::VMOVDQA32Z256rrk:
      Opc = X86::VPBLENDMDZ256rrk;
      break;
    case X86::VMOVDQA32Zrrk:
      Opc = X86::VPBLENDMDZrrk;
      break;
    case X86::VMOVDQA64Z128rrk:
      Opc = X86::VPBLENDMQZ128rrk;
      break;
    case X86::VMOVDQA64Z256rrk:
      Opc = X86::VPBLENDMQZ256rrk;
      break;
    case X86::VMOVDQA64Zrrk:
      Opc = X86::VPBLENDMQZrrk;
      break;
    case X86::VMOVAPDZ128rrk:
      Opc = X86::VBLENDMPDZ128rrk;
      break;
    case X86::VMOVAPDZ256rrk:
      Opc = X86::VBLENDMPDZ256rrk;
      break;
    case X86::VMOVAPDZrrk:
      Opc = X86::VBLENDMPDZrrk;
      break;
    case X86::VMOVAPSZ128rrk:
      Opc = X86::VBLENDMPSZ128rrk;
      break;
    case X86::VMOVAPSZ256rrk:
      Opc = X86::VBLENDMPSZ256rrk;
      break;
    case X86::VMOVAPSZrrk:
      Opc = X86::VBLENDMPSZrrk;
      break;
    }

    NewMI = BuildMI(MF, MI.getDebugLoc(), get(Opc))
                .add(Dest)
                .add(MI.getOperand(2))
                .add(Src)
                .add(MI.getOperand(3));
    NumRegOperands = 4;
    break;
  }
  }

  if (!NewMI)
    return nullptr;

  if (LV) { // Update live variables
    for (unsigned I = 0; I < NumRegOperands; ++I) {
      MachineOperand &Op = MI.getOperand(I);
      if (Op.isReg() && (Op.isDead() || Op.isKill()))
        LV->replaceKillInstruction(Op.getReg(), MI, *NewMI);
    }
  }

  MachineBasicBlock &MBB = *MI.getParent();
  MBB.insert(MI.getIterator(), NewMI); // Insert the new inst

  if (LIS) {
    LIS->ReplaceMachineInstrInMaps(MI, *NewMI);
    if (SrcReg)
      LIS->getInterval(SrcReg);
    if (SrcReg2)
      LIS->getInterval(SrcReg2);
  }

  return NewMI;
}

/// This determines which of three possible cases of a three source commute
/// the source indexes correspond to taking into account any mask operands.
/// All prevents commuting a passthru operand. Returns -1 if the commute isn't
/// possible.
/// Case 0 - Possible to commute the first and second operands.
/// Case 1 - Possible to commute the first and third operands.
/// Case 2 - Possible to commute the second and third operands.
static unsigned getThreeSrcCommuteCase(uint64_t TSFlags, unsigned SrcOpIdx1,
                                       unsigned SrcOpIdx2) {
  // Put the lowest index to SrcOpIdx1 to simplify the checks below.
  if (SrcOpIdx1 > SrcOpIdx2)
    std::swap(SrcOpIdx1, SrcOpIdx2);

  unsigned Op1 = 1, Op2 = 2, Op3 = 3;
  if (X86II::isKMasked(TSFlags)) {
    Op2++;
    Op3++;
  }

  if (SrcOpIdx1 == Op1 && SrcOpIdx2 == Op2)
    return 0;
  if (SrcOpIdx1 == Op1 && SrcOpIdx2 == Op3)
    return 1;
  if (SrcOpIdx1 == Op2 && SrcOpIdx2 == Op3)
    return 2;
  llvm_unreachable("Unknown three src commute case.");
}

unsigned X86InstrInfo::getFMA3OpcodeToCommuteOperands(
    const MachineInstr &MI, unsigned SrcOpIdx1, unsigned SrcOpIdx2,
    const X86InstrFMA3Group &FMA3Group) const {

  unsigned Opc = MI.getOpcode();

  // TODO: Commuting the 1st operand of FMA*_Int requires some additional
  // analysis. The commute optimization is legal only if all users of FMA*_Int
  // use only the lowest element of the FMA*_Int instruction. Such analysis are
  // not implemented yet. So, just return 0 in that case.
  // When such analysis are available this place will be the right place for
  // calling it.
  assert(!(FMA3Group.isIntrinsic() && (SrcOpIdx1 == 1 || SrcOpIdx2 == 1)) &&
         "Intrinsic instructions can't commute operand 1");

  // Determine which case this commute is or if it can't be done.
  unsigned Case =
      getThreeSrcCommuteCase(MI.getDesc().TSFlags, SrcOpIdx1, SrcOpIdx2);
  assert(Case < 3 && "Unexpected case number!");

  // Define the FMA forms mapping array that helps to map input FMA form
  // to output FMA form to preserve the operation semantics after
  // commuting the operands.
  const unsigned Form132Index = 0;
  const unsigned Form213Index = 1;
  const unsigned Form231Index = 2;
  static const unsigned FormMapping[][3] = {
      // 0: SrcOpIdx1 == 1 && SrcOpIdx2 == 2;
      // FMA132 A, C, b; ==> FMA231 C, A, b;
      // FMA213 B, A, c; ==> FMA213 A, B, c;
      // FMA231 C, A, b; ==> FMA132 A, C, b;
      {Form231Index, Form213Index, Form132Index},
      // 1: SrcOpIdx1 == 1 && SrcOpIdx2 == 3;
      // FMA132 A, c, B; ==> FMA132 B, c, A;
      // FMA213 B, a, C; ==> FMA231 C, a, B;
      // FMA231 C, a, B; ==> FMA213 B, a, C;
      {Form132Index, Form231Index, Form213Index},
      // 2: SrcOpIdx1 == 2 && SrcOpIdx2 == 3;
      // FMA132 a, C, B; ==> FMA213 a, B, C;
      // FMA213 b, A, C; ==> FMA132 b, C, A;
      // FMA231 c, A, B; ==> FMA231 c, B, A;
      {Form213Index, Form132Index, Form231Index}};

  unsigned FMAForms[3];
  FMAForms[0] = FMA3Group.get132Opcode();
  FMAForms[1] = FMA3Group.get213Opcode();
  FMAForms[2] = FMA3Group.get231Opcode();

  // Everything is ready, just adjust the FMA opcode and return it.
  for (unsigned FormIndex = 0; FormIndex < 3; FormIndex++)
    if (Opc == FMAForms[FormIndex])
      return FMAForms[FormMapping[Case][FormIndex]];

  llvm_unreachable("Illegal FMA3 format");
}

static void commuteVPTERNLOG(MachineInstr &MI, unsigned SrcOpIdx1,
                             unsigned SrcOpIdx2) {
  // Determine which case this commute is or if it can't be done.
  unsigned Case =
      getThreeSrcCommuteCase(MI.getDesc().TSFlags, SrcOpIdx1, SrcOpIdx2);
  assert(Case < 3 && "Unexpected case value!");

  // For each case we need to swap two pairs of bits in the final immediate.
  static const uint8_t SwapMasks[3][4] = {
      {0x04, 0x10, 0x08, 0x20}, // Swap bits 2/4 and 3/5.
      {0x02, 0x10, 0x08, 0x40}, // Swap bits 1/4 and 3/6.
      {0x02, 0x04, 0x20, 0x40}, // Swap bits 1/2 and 5/6.
  };

  uint8_t Imm = MI.getOperand(MI.getNumOperands() - 1).getImm();
  // Clear out the bits we are swapping.
  uint8_t NewImm = Imm & ~(SwapMasks[Case][0] | SwapMasks[Case][1] |
                           SwapMasks[Case][2] | SwapMasks[Case][3]);
  // If the immediate had a bit of the pair set, then set the opposite bit.
  if (Imm & SwapMasks[Case][0])
    NewImm |= SwapMasks[Case][1];
  if (Imm & SwapMasks[Case][1])
    NewImm |= SwapMasks[Case][0];
  if (Imm & SwapMasks[Case][2])
    NewImm |= SwapMasks[Case][3];
  if (Imm & SwapMasks[Case][3])
    NewImm |= SwapMasks[Case][2];
  MI.getOperand(MI.getNumOperands() - 1).setImm(NewImm);
}

// Returns true if this is a VPERMI2 or VPERMT2 instruction that can be
// commuted.
static bool isCommutableVPERMV3Instruction(unsigned Opcode) {
#define VPERM_CASES(Suffix)                                                    \
  case X86::VPERMI2##Suffix##Z128rr:                                           \
  case X86::VPERMT2##Suffix##Z128rr:                                           \
  case X86::VPERMI2##Suffix##Z256rr:                                           \
  case X86::VPERMT2##Suffix##Z256rr:                                           \
  case X86::VPERMI2##Suffix##Zrr:                                              \
  case X86::VPERMT2##Suffix##Zrr:                                              \
  case X86::VPERMI2##Suffix##Z128rm:                                           \
  case X86::VPERMT2##Suffix##Z128rm:                                           \
  case X86::VPERMI2##Suffix##Z256rm:                                           \
  case X86::VPERMT2##Suffix##Z256rm:                                           \
  case X86::VPERMI2##Suffix##Zrm:                                              \
  case X86::VPERMT2##Suffix##Zrm:                                              \
  case X86::VPERMI2##Suffix##Z128rrkz:                                         \
  case X86::VPERMT2##Suffix##Z128rrkz:                                         \
  case X86::VPERMI2##Suffix##Z256rrkz:                                         \
  case X86::VPERMT2##Suffix##Z256rrkz:                                         \
  case X86::VPERMI2##Suffix##Zrrkz:                                            \
  case X86::VPERMT2##Suffix##Zrrkz:                                            \
  case X86::VPERMI2##Suffix##Z128rmkz:                                         \
  case X86::VPERMT2##Suffix##Z128rmkz:                                         \
  case X86::VPERMI2##Suffix##Z256rmkz:                                         \
  case X86::VPERMT2##Suffix##Z256rmkz:                                         \
  case X86::VPERMI2##Suffix##Zrmkz:                                            \
  case X86::VPERMT2##Suffix##Zrmkz:

#define VPERM_CASES_BROADCAST(Suffix)                                          \
  VPERM_CASES(Suffix)                                                          \
  case X86::VPERMI2##Suffix##Z128rmb:                                          \
  case X86::VPERMT2##Suffix##Z128rmb:                                          \
  case X86::VPERMI2##Suffix##Z256rmb:                                          \
  case X86::VPERMT2##Suffix##Z256rmb:                                          \
  case X86::VPERMI2##Suffix##Zrmb:                                             \
  case X86::VPERMT2##Suffix##Zrmb:                                             \
  case X86::VPERMI2##Suffix##Z128rmbkz:                                        \
  case X86::VPERMT2##Suffix##Z128rmbkz:                                        \
  case X86::VPERMI2##Suffix##Z256rmbkz:                                        \
  case X86::VPERMT2##Suffix##Z256rmbkz:                                        \
  case X86::VPERMI2##Suffix##Zrmbkz:                                           \
  case X86::VPERMT2##Suffix##Zrmbkz:

  switch (Opcode) {
  default:
    return false;
    VPERM_CASES(B)
    VPERM_CASES_BROADCAST(D)
    VPERM_CASES_BROADCAST(PD)
    VPERM_CASES_BROADCAST(PS)
    VPERM_CASES_BROADCAST(Q)
    VPERM_CASES(W)
    return true;
  }
#undef VPERM_CASES_BROADCAST
#undef VPERM_CASES
}

// Returns commuted opcode for VPERMI2 and VPERMT2 instructions by switching
// from the I opcode to the T opcode and vice versa.
static unsigned getCommutedVPERMV3Opcode(unsigned Opcode) {
#define VPERM_CASES(Orig, New)                                                 \
  case X86::Orig##Z128rr:                                                      \
    return X86::New##Z128rr;                                                   \
  case X86::Orig##Z128rrkz:                                                    \
    return X86::New##Z128rrkz;                                                 \
  case X86::Orig##Z128rm:                                                      \
    return X86::New##Z128rm;                                                   \
  case X86::Orig##Z128rmkz:                                                    \
    return X86::New##Z128rmkz;                                                 \
  case X86::Orig##Z256rr:                                                      \
    return X86::New##Z256rr;                                                   \
  case X86::Orig##Z256rrkz:                                                    \
    return X86::New##Z256rrkz;                                                 \
  case X86::Orig##Z256rm:                                                      \
    return X86::New##Z256rm;                                                   \
  case X86::Orig##Z256rmkz:                                                    \
    return X86::New##Z256rmkz;                                                 \
  case X86::Orig##Zrr:                                                         \
    return X86::New##Zrr;                                                      \
  case X86::Orig##Zrrkz:                                                       \
    return X86::New##Zrrkz;                                                    \
  case X86::Orig##Zrm:                                                         \
    return X86::New##Zrm;                                                      \
  case X86::Orig##Zrmkz:                                                       \
    return X86::New##Zrmkz;

#define VPERM_CASES_BROADCAST(Orig, New)                                       \
  VPERM_CASES(Orig, New)                                                       \
  case X86::Orig##Z128rmb:                                                     \
    return X86::New##Z128rmb;                                                  \
  case X86::Orig##Z128rmbkz:                                                   \
    return X86::New##Z128rmbkz;                                                \
  case X86::Orig##Z256rmb:                                                     \
    return X86::New##Z256rmb;                                                  \
  case X86::Orig##Z256rmbkz:                                                   \
    return X86::New##Z256rmbkz;                                                \
  case X86::Orig##Zrmb:                                                        \
    return X86::New##Zrmb;                                                     \
  case X86::Orig##Zrmbkz:                                                      \
    return X86::New##Zrmbkz;

  switch (Opcode) {
    VPERM_CASES(VPERMI2B, VPERMT2B)
    VPERM_CASES_BROADCAST(VPERMI2D, VPERMT2D)
    VPERM_CASES_BROADCAST(VPERMI2PD, VPERMT2PD)
    VPERM_CASES_BROADCAST(VPERMI2PS, VPERMT2PS)
    VPERM_CASES_BROADCAST(VPERMI2Q, VPERMT2Q)
    VPERM_CASES(VPERMI2W, VPERMT2W)
    VPERM_CASES(VPERMT2B, VPERMI2B)
    VPERM_CASES_BROADCAST(VPERMT2D, VPERMI2D)
    VPERM_CASES_BROADCAST(VPERMT2PD, VPERMI2PD)
    VPERM_CASES_BROADCAST(VPERMT2PS, VPERMI2PS)
    VPERM_CASES_BROADCAST(VPERMT2Q, VPERMI2Q)
    VPERM_CASES(VPERMT2W, VPERMI2W)
  }

  llvm_unreachable("Unreachable!");
#undef VPERM_CASES_BROADCAST
#undef VPERM_CASES
}

MachineInstr *X86InstrInfo::commuteInstructionImpl(MachineInstr &MI, bool NewMI,
                                                   unsigned OpIdx1,
                                                   unsigned OpIdx2) const {
  auto CloneIfNew = [&](MachineInstr &MI) {
    return std::exchange(NewMI, false)
               ? MI.getParent()->getParent()->CloneMachineInstr(&MI)
               : &MI;
  };
  MachineInstr *WorkingMI = nullptr;
  unsigned Opc = MI.getOpcode();

#define CASE_ND(OP)                                                            \
  case X86::OP:                                                                \
  case X86::OP##_ND:

  switch (Opc) {
  // SHLD B, C, I <-> SHRD C, B, (BitWidth - I)
  CASE_ND(SHRD16rri8)
  CASE_ND(SHLD16rri8)
  CASE_ND(SHRD32rri8)
  CASE_ND(SHLD32rri8)
  CASE_ND(SHRD64rri8)
  CASE_ND(SHLD64rri8) {
    unsigned Size;
    switch (Opc) {
    default:
      llvm_unreachable("Unreachable!");
#define FROM_TO_SIZE(A, B, S)                                                  \
  case X86::A:                                                                 \
    Opc = X86::B;                                                              \
    Size = S;                                                                  \
    break;                                                                     \
  case X86::A##_ND:                                                            \
    Opc = X86::B##_ND;                                                         \
    Size = S;                                                                  \
    break;                                                                     \
  case X86::B:                                                                 \
    Opc = X86::A;                                                              \
    Size = S;                                                                  \
    break;                                                                     \
  case X86::B##_ND:                                                            \
    Opc = X86::A##_ND;                                                         \
    Size = S;                                                                  \
    break;

    FROM_TO_SIZE(SHRD16rri8, SHLD16rri8, 16)
    FROM_TO_SIZE(SHRD32rri8, SHLD32rri8, 32)
    FROM_TO_SIZE(SHRD64rri8, SHLD64rri8, 64)
#undef FROM_TO_SIZE
    }
    WorkingMI = CloneIfNew(MI);
    WorkingMI->setDesc(get(Opc));
    WorkingMI->getOperand(3).setImm(Size - MI.getOperand(3).getImm());
    break;
  }
  case X86::PFSUBrr:
  case X86::PFSUBRrr:
    // PFSUB  x, y: x = x - y
    // PFSUBR x, y: x = y - x
    WorkingMI = CloneIfNew(MI);
    WorkingMI->setDesc(
        get(X86::PFSUBRrr == Opc ? X86::PFSUBrr : X86::PFSUBRrr));
    break;
  case X86::BLENDPDrri:
  case X86::BLENDPSrri:
  case X86::VBLENDPDrri:
  case X86::VBLENDPSrri:
    // If we're optimizing for size, try to use MOVSD/MOVSS.
    if (MI.getParent()->getParent()->getFunction().hasOptSize()) {
      unsigned Mask = (Opc == X86::BLENDPDrri || Opc == X86::VBLENDPDrri) ? 0x03: 0x0F;
      if ((MI.getOperand(3).getImm() ^ Mask) == 1) {
#define FROM_TO(FROM, TO)                                                      \
  case X86::FROM:                                                              \
    Opc = X86::TO;                                                             \
    break;
        switch (Opc) {
        default:
          llvm_unreachable("Unreachable!");
        FROM_TO(BLENDPDrri, MOVSDrr)
        FROM_TO(BLENDPSrri, MOVSSrr)
        FROM_TO(VBLENDPDrri, VMOVSDrr)
        FROM_TO(VBLENDPSrri, VMOVSSrr)
        }
        WorkingMI = CloneIfNew(MI);
        WorkingMI->setDesc(get(Opc));
        WorkingMI->removeOperand(3);
        break;
      }
#undef FROM_TO
    }
    [[fallthrough]];
  case X86::PBLENDWrri:
  case X86::VBLENDPDYrri:
  case X86::VBLENDPSYrri:
  case X86::VPBLENDDrri:
  case X86::VPBLENDWrri:
  case X86::VPBLENDDYrri:
  case X86::VPBLENDWYrri: {
    int8_t Mask;
    switch (Opc) {
    default:
      llvm_unreachable("Unreachable!");
    case X86::BLENDPDrri:
      Mask = (int8_t)0x03;
      break;
    case X86::BLENDPSrri:
      Mask = (int8_t)0x0F;
      break;
    case X86::PBLENDWrri:
      Mask = (int8_t)0xFF;
      break;
    case X86::VBLENDPDrri:
      Mask = (int8_t)0x03;
      break;
    case X86::VBLENDPSrri:
      Mask = (int8_t)0x0F;
      break;
    case X86::VBLENDPDYrri:
      Mask = (int8_t)0x0F;
      break;
    case X86::VBLENDPSYrri:
      Mask = (int8_t)0xFF;
      break;
    case X86::VPBLENDDrri:
      Mask = (int8_t)0x0F;
      break;
    case X86::VPBLENDWrri:
      Mask = (int8_t)0xFF;
      break;
    case X86::VPBLENDDYrri:
      Mask = (int8_t)0xFF;
      break;
    case X86::VPBLENDWYrri:
      Mask = (int8_t)0xFF;
      break;
    }
    // Only the least significant bits of Imm are used.
    // Using int8_t to ensure it will be sign extended to the int64_t that
    // setImm takes in order to match isel behavior.
    int8_t Imm = MI.getOperand(3).getImm() & Mask;
    WorkingMI = CloneIfNew(MI);
    WorkingMI->getOperand(3).setImm(Mask ^ Imm);
    break;
  }
  case X86::INSERTPSrr:
  case X86::VINSERTPSrr:
  case X86::VINSERTPSZrr: {
    unsigned Imm = MI.getOperand(MI.getNumOperands() - 1).getImm();
    unsigned ZMask = Imm & 15;
    unsigned DstIdx = (Imm >> 4) & 3;
    unsigned SrcIdx = (Imm >> 6) & 3;

    // We can commute insertps if we zero 2 of the elements, the insertion is
    // "inline" and we don't override the insertion with a zero.
    if (DstIdx == SrcIdx && (ZMask & (1 << DstIdx)) == 0 &&
        llvm::popcount(ZMask) == 2) {
      unsigned AltIdx = llvm::countr_zero((ZMask | (1 << DstIdx)) ^ 15);
      assert(AltIdx < 4 && "Illegal insertion index");
      unsigned AltImm = (AltIdx << 6) | (AltIdx << 4) | ZMask;
      WorkingMI = CloneIfNew(MI);
      WorkingMI->getOperand(MI.getNumOperands() - 1).setImm(AltImm);
      break;
    }
    return nullptr;
  }
  case X86::MOVSDrr:
  case X86::MOVSSrr:
  case X86::VMOVSDrr:
  case X86::VMOVSSrr: {
    // On SSE41 or later we can commute a MOVSS/MOVSD to a BLENDPS/BLENDPD.
    if (Subtarget.hasSSE41()) {
      unsigned Mask;
      switch (Opc) {
      default:
        llvm_unreachable("Unreachable!");
      case X86::MOVSDrr:
        Opc = X86::BLENDPDrri;
        Mask = 0x02;
        break;
      case X86::MOVSSrr:
        Opc = X86::BLENDPSrri;
        Mask = 0x0E;
        break;
      case X86::VMOVSDrr:
        Opc = X86::VBLENDPDrri;
        Mask = 0x02;
        break;
      case X86::VMOVSSrr:
        Opc = X86::VBLENDPSrri;
        Mask = 0x0E;
        break;
      }

      WorkingMI = CloneIfNew(MI);
      WorkingMI->setDesc(get(Opc));
      WorkingMI->addOperand(MachineOperand::CreateImm(Mask));
      break;
    }

    WorkingMI = CloneIfNew(MI);
    WorkingMI->setDesc(get(X86::SHUFPDrri));
    WorkingMI->addOperand(MachineOperand::CreateImm(0x02));
    break;
  }
  case X86::SHUFPDrri: {
    // Commute to MOVSD.
    assert(MI.getOperand(3).getImm() == 0x02 && "Unexpected immediate!");
    WorkingMI = CloneIfNew(MI);
    WorkingMI->setDesc(get(X86::MOVSDrr));
    WorkingMI->removeOperand(3);
    break;
  }
  case X86::PCLMULQDQrri:
  case X86::VPCLMULQDQrri:
  case X86::VPCLMULQDQYrri:
  case X86::VPCLMULQDQZrri:
  case X86::VPCLMULQDQZ128rri:
  case X86::VPCLMULQDQZ256rri: {
    // SRC1 64bits = Imm[0] ? SRC1[127:64] : SRC1[63:0]
    // SRC2 64bits = Imm[4] ? SRC2[127:64] : SRC2[63:0]
    unsigned Imm = MI.getOperand(3).getImm();
    unsigned Src1Hi = Imm & 0x01;
    unsigned Src2Hi = Imm & 0x10;
    WorkingMI = CloneIfNew(MI);
    WorkingMI->getOperand(3).setImm((Src1Hi << 4) | (Src2Hi >> 4));
    break;
  }
  case X86::VPCMPBZ128rri:
  case X86::VPCMPUBZ128rri:
  case X86::VPCMPBZ256rri:
  case X86::VPCMPUBZ256rri:
  case X86::VPCMPBZrri:
  case X86::VPCMPUBZrri:
  case X86::VPCMPDZ128rri:
  case X86::VPCMPUDZ128rri:
  case X86::VPCMPDZ256rri:
  case X86::VPCMPUDZ256rri:
  case X86::VPCMPDZrri:
  case X86::VPCMPUDZrri:
  case X86::VPCMPQZ128rri:
  case X86::VPCMPUQZ128rri:
  case X86::VPCMPQZ256rri:
  case X86::VPCMPUQZ256rri:
  case X86::VPCMPQZrri:
  case X86::VPCMPUQZrri:
  case X86::VPCMPWZ128rri:
  case X86::VPCMPUWZ128rri:
  case X86::VPCMPWZ256rri:
  case X86::VPCMPUWZ256rri:
  case X86::VPCMPWZrri:
  case X86::VPCMPUWZrri:
  case X86::VPCMPBZ128rrik:
  case X86::VPCMPUBZ128rrik:
  case X86::VPCMPBZ256rrik:
  case X86::VPCMPUBZ256rrik:
  case X86::VPCMPBZrrik:
  case X86::VPCMPUBZrrik:
  case X86::VPCMPDZ128rrik:
  case X86::VPCMPUDZ128rrik:
  case X86::VPCMPDZ256rrik:
  case X86::VPCMPUDZ256rrik:
  case X86::VPCMPDZrrik:
  case X86::VPCMPUDZrrik:
  case X86::VPCMPQZ128rrik:
  case X86::VPCMPUQZ128rrik:
  case X86::VPCMPQZ256rrik:
  case X86::VPCMPUQZ256rrik:
  case X86::VPCMPQZrrik:
  case X86::VPCMPUQZrrik:
  case X86::VPCMPWZ128rrik:
  case X86::VPCMPUWZ128rrik:
  case X86::VPCMPWZ256rrik:
  case X86::VPCMPUWZ256rrik:
  case X86::VPCMPWZrrik:
  case X86::VPCMPUWZrrik:
    WorkingMI = CloneIfNew(MI);
    // Flip comparison mode immediate (if necessary).
    WorkingMI->getOperand(MI.getNumOperands() - 1)
        .setImm(X86::getSwappedVPCMPImm(
            MI.getOperand(MI.getNumOperands() - 1).getImm() & 0x7));
    break;
  case X86::VPCOMBri:
  case X86::VPCOMUBri:
  case X86::VPCOMDri:
  case X86::VPCOMUDri:
  case X86::VPCOMQri:
  case X86::VPCOMUQri:
  case X86::VPCOMWri:
  case X86::VPCOMUWri:
    WorkingMI = CloneIfNew(MI);
    // Flip comparison mode immediate (if necessary).
    WorkingMI->getOperand(3).setImm(
        X86::getSwappedVPCOMImm(MI.getOperand(3).getImm() & 0x7));
    break;
  case X86::VCMPSDZrri:
  case X86::VCMPSSZrri:
  case X86::VCMPPDZrri:
  case X86::VCMPPSZrri:
  case X86::VCMPSHZrri:
  case X86::VCMPPHZrri:
  case X86::VCMPPHZ128rri:
  case X86::VCMPPHZ256rri:
  case X86::VCMPPDZ128rri:
  case X86::VCMPPSZ128rri:
  case X86::VCMPPDZ256rri:
  case X86::VCMPPSZ256rri:
  case X86::VCMPPDZrrik:
  case X86::VCMPPSZrrik:
  case X86::VCMPPDZ128rrik:
  case X86::VCMPPSZ128rrik:
  case X86::VCMPPDZ256rrik:
  case X86::VCMPPSZ256rrik:
    WorkingMI = CloneIfNew(MI);
    WorkingMI->getOperand(MI.getNumExplicitOperands() - 1)
        .setImm(X86::getSwappedVCMPImm(
            MI.getOperand(MI.getNumExplicitOperands() - 1).getImm() & 0x1f));
    break;
  case X86::VPERM2F128rr:
  case X86::VPERM2I128rr:
    // Flip permute source immediate.
    // Imm & 0x02: lo = if set, select Op1.lo/hi else Op0.lo/hi.
    // Imm & 0x20: hi = if set, select Op1.lo/hi else Op0.lo/hi.
    WorkingMI = CloneIfNew(MI);
    WorkingMI->getOperand(3).setImm((MI.getOperand(3).getImm() & 0xFF) ^ 0x22);
    break;
  case X86::MOVHLPSrr:
  case X86::UNPCKHPDrr:
  case X86::VMOVHLPSrr:
  case X86::VUNPCKHPDrr:
  case X86::VMOVHLPSZrr:
  case X86::VUNPCKHPDZ128rr:
    assert(Subtarget.hasSSE2() && "Commuting MOVHLP/UNPCKHPD requires SSE2!");

    switch (Opc) {
    default:
      llvm_unreachable("Unreachable!");
    case X86::MOVHLPSrr:
      Opc = X86::UNPCKHPDrr;
      break;
    case X86::UNPCKHPDrr:
      Opc = X86::MOVHLPSrr;
      break;
    case X86::VMOVHLPSrr:
      Opc = X86::VUNPCKHPDrr;
      break;
    case X86::VUNPCKHPDrr:
      Opc = X86::VMOVHLPSrr;
      break;
    case X86::VMOVHLPSZrr:
      Opc = X86::VUNPCKHPDZ128rr;
      break;
    case X86::VUNPCKHPDZ128rr:
      Opc = X86::VMOVHLPSZrr;
      break;
    }
    WorkingMI = CloneIfNew(MI);
    WorkingMI->setDesc(get(Opc));
    break;
  CASE_ND(CMOV16rr)
  CASE_ND(CMOV32rr)
  CASE_ND(CMOV64rr) {
    WorkingMI = CloneIfNew(MI);
    unsigned OpNo = MI.getDesc().getNumOperands() - 1;
    X86::CondCode CC = static_cast<X86::CondCode>(MI.getOperand(OpNo).getImm());
    WorkingMI->getOperand(OpNo).setImm(X86::GetOppositeBranchCondition(CC));
    break;
  }
  case X86::VPTERNLOGDZrri:
  case X86::VPTERNLOGDZrmi:
  case X86::VPTERNLOGDZ128rri:
  case X86::VPTERNLOGDZ128rmi:
  case X86::VPTERNLOGDZ256rri:
  case X86::VPTERNLOGDZ256rmi:
  case X86::VPTERNLOGQZrri:
  case X86::VPTERNLOGQZrmi:
  case X86::VPTERNLOGQZ128rri:
  case X86::VPTERNLOGQZ128rmi:
  case X86::VPTERNLOGQZ256rri:
  case X86::VPTERNLOGQZ256rmi:
  case X86::VPTERNLOGDZrrik:
  case X86::VPTERNLOGDZ128rrik:
  case X86::VPTERNLOGDZ256rrik:
  case X86::VPTERNLOGQZrrik:
  case X86::VPTERNLOGQZ128rrik:
  case X86::VPTERNLOGQZ256rrik:
  case X86::VPTERNLOGDZrrikz:
  case X86::VPTERNLOGDZrmikz:
  case X86::VPTERNLOGDZ128rrikz:
  case X86::VPTERNLOGDZ128rmikz:
  case X86::VPTERNLOGDZ256rrikz:
  case X86::VPTERNLOGDZ256rmikz:
  case X86::VPTERNLOGQZrrikz:
  case X86::VPTERNLOGQZrmikz:
  case X86::VPTERNLOGQZ128rrikz:
  case X86::VPTERNLOGQZ128rmikz:
  case X86::VPTERNLOGQZ256rrikz:
  case X86::VPTERNLOGQZ256rmikz:
  case X86::VPTERNLOGDZ128rmbi:
  case X86::VPTERNLOGDZ256rmbi:
  case X86::VPTERNLOGDZrmbi:
  case X86::VPTERNLOGQZ128rmbi:
  case X86::VPTERNLOGQZ256rmbi:
  case X86::VPTERNLOGQZrmbi:
  case X86::VPTERNLOGDZ128rmbikz:
  case X86::VPTERNLOGDZ256rmbikz:
  case X86::VPTERNLOGDZrmbikz:
  case X86::VPTERNLOGQZ128rmbikz:
  case X86::VPTERNLOGQZ256rmbikz:
  case X86::VPTERNLOGQZrmbikz: {
    WorkingMI = CloneIfNew(MI);
    commuteVPTERNLOG(*WorkingMI, OpIdx1, OpIdx2);
    break;
  }
  default:
    if (isCommutableVPERMV3Instruction(Opc)) {
      WorkingMI = CloneIfNew(MI);
      WorkingMI->setDesc(get(getCommutedVPERMV3Opcode(Opc)));
      break;
    }

    if (auto *FMA3Group = getFMA3Group(Opc, MI.getDesc().TSFlags)) {
      WorkingMI = CloneIfNew(MI);
      WorkingMI->setDesc(
          get(getFMA3OpcodeToCommuteOperands(MI, OpIdx1, OpIdx2, *FMA3Group)));
      break;
    }
  }
  return TargetInstrInfo::commuteInstructionImpl(MI, NewMI, OpIdx1, OpIdx2);
}

bool X86InstrInfo::findThreeSrcCommutedOpIndices(const MachineInstr &MI,
                                                 unsigned &SrcOpIdx1,
                                                 unsigned &SrcOpIdx2,
                                                 bool IsIntrinsic) const {
  uint64_t TSFlags = MI.getDesc().TSFlags;

  unsigned FirstCommutableVecOp = 1;
  unsigned LastCommutableVecOp = 3;
  unsigned KMaskOp = -1U;
  if (X86II::isKMasked(TSFlags)) {
    // For k-zero-masked operations it is Ok to commute the first vector
    // operand. Unless this is an intrinsic instruction.
    // For regular k-masked operations a conservative choice is done as the
    // elements of the first vector operand, for which the corresponding bit
    // in the k-mask operand is set to 0, are copied to the result of the
    // instruction.
    // TODO/FIXME: The commute still may be legal if it is known that the
    // k-mask operand is set to either all ones or all zeroes.
    // It is also Ok to commute the 1st operand if all users of MI use only
    // the elements enabled by the k-mask operand. For example,
    //   v4 = VFMADD213PSZrk v1, k, v2, v3; // v1[i] = k[i] ? v2[i]*v1[i]+v3[i]
    //                                                     : v1[i];
    //   VMOVAPSZmrk <mem_addr>, k, v4; // this is the ONLY user of v4 ->
    //                                  // Ok, to commute v1 in FMADD213PSZrk.

    // The k-mask operand has index = 2 for masked and zero-masked operations.
    KMaskOp = 2;

    // The operand with index = 1 is used as a source for those elements for
    // which the corresponding bit in the k-mask is set to 0.
    if (X86II::isKMergeMasked(TSFlags) || IsIntrinsic)
      FirstCommutableVecOp = 3;

    LastCommutableVecOp++;
  } else if (IsIntrinsic) {
    // Commuting the first operand of an intrinsic instruction isn't possible
    // unless we can prove that only the lowest element of the result is used.
    FirstCommutableVecOp = 2;
  }

  if (isMem(MI, LastCommutableVecOp))
    LastCommutableVecOp--;

  // Only the first RegOpsNum operands are commutable.
  // Also, the value 'CommuteAnyOperandIndex' is valid here as it means
  // that the operand is not specified/fixed.
  if (SrcOpIdx1 != CommuteAnyOperandIndex &&
      (SrcOpIdx1 < FirstCommutableVecOp || SrcOpIdx1 > LastCommutableVecOp ||
       SrcOpIdx1 == KMaskOp))
    return false;
  if (SrcOpIdx2 != CommuteAnyOperandIndex &&
      (SrcOpIdx2 < FirstCommutableVecOp || SrcOpIdx2 > LastCommutableVecOp ||
       SrcOpIdx2 == KMaskOp))
    return false;

  // Look for two different register operands assumed to be commutable
  // regardless of the FMA opcode. The FMA opcode is adjusted later.
  if (SrcOpIdx1 == CommuteAnyOperandIndex ||
      SrcOpIdx2 == CommuteAnyOperandIndex) {
    unsigned CommutableOpIdx2 = SrcOpIdx2;

    // At least one of operands to be commuted is not specified and
    // this method is free to choose appropriate commutable operands.
    if (SrcOpIdx1 == SrcOpIdx2)
      // Both of operands are not fixed. By default set one of commutable
      // operands to the last register operand of the instruction.
      CommutableOpIdx2 = LastCommutableVecOp;
    else if (SrcOpIdx2 == CommuteAnyOperandIndex)
      // Only one of operands is not fixed.
      CommutableOpIdx2 = SrcOpIdx1;

    // CommutableOpIdx2 is well defined now. Let's choose another commutable
    // operand and assign its index to CommutableOpIdx1.
    Register Op2Reg = MI.getOperand(CommutableOpIdx2).getReg();

    unsigned CommutableOpIdx1;
    for (CommutableOpIdx1 = LastCommutableVecOp;
         CommutableOpIdx1 >= FirstCommutableVecOp; CommutableOpIdx1--) {
      // Just ignore and skip the k-mask operand.
      if (CommutableOpIdx1 == KMaskOp)
        continue;

      // The commuted operands must have different registers.
      // Otherwise, the commute transformation does not change anything and
      // is useless then.
      if (Op2Reg != MI.getOperand(CommutableOpIdx1).getReg())
        break;
    }

    // No appropriate commutable operands were found.
    if (CommutableOpIdx1 < FirstCommutableVecOp)
      return false;

    // Assign the found pair of commutable indices to SrcOpIdx1 and SrcOpidx2
    // to return those values.
    if (!fixCommutedOpIndices(SrcOpIdx1, SrcOpIdx2, CommutableOpIdx1,
                              CommutableOpIdx2))
      return false;
  }

  return true;
}

bool X86InstrInfo::findCommutedOpIndices(const MachineInstr &MI,
                                         unsigned &SrcOpIdx1,
                                         unsigned &SrcOpIdx2) const {
  const MCInstrDesc &Desc = MI.getDesc();
  if (!Desc.isCommutable())
    return false;

  switch (MI.getOpcode()) {
  case X86::CMPSDrri:
  case X86::CMPSSrri:
  case X86::CMPPDrri:
  case X86::CMPPSrri:
  case X86::VCMPSDrri:
  case X86::VCMPSSrri:
  case X86::VCMPPDrri:
  case X86::VCMPPSrri:
  case X86::VCMPPDYrri:
  case X86::VCMPPSYrri:
  case X86::VCMPSDZrri:
  case X86::VCMPSSZrri:
  case X86::VCMPPDZrri:
  case X86::VCMPPSZrri:
  case X86::VCMPSHZrri:
  case X86::VCMPPHZrri:
  case X86::VCMPPHZ128rri:
  case X86::VCMPPHZ256rri:
  case X86::VCMPPDZ128rri:
  case X86::VCMPPSZ128rri:
  case X86::VCMPPDZ256rri:
  case X86::VCMPPSZ256rri:
  case X86::VCMPPDZrrik:
  case X86::VCMPPSZrrik:
  case X86::VCMPPDZ128rrik:
  case X86::VCMPPSZ128rrik:
  case X86::VCMPPDZ256rrik:
  case X86::VCMPPSZ256rrik: {
    unsigned OpOffset = X86II::isKMasked(Desc.TSFlags) ? 1 : 0;

    // Float comparison can be safely commuted for
    // Ordered/Unordered/Equal/NotEqual tests
    unsigned Imm = MI.getOperand(3 + OpOffset).getImm() & 0x7;
    switch (Imm) {
    default:
      // EVEX versions can be commuted.
      if ((Desc.TSFlags & X86II::EncodingMask) == X86II::EVEX)
        break;
      return false;
    case 0x00: // EQUAL
    case 0x03: // UNORDERED
    case 0x04: // NOT EQUAL
    case 0x07: // ORDERED
      break;
    }

    // The indices of the commutable operands are 1 and 2 (or 2 and 3
    // when masked).
    // Assign them to the returned operand indices here.
    return fixCommutedOpIndices(SrcOpIdx1, SrcOpIdx2, 1 + OpOffset,
                                2 + OpOffset);
  }
  case X86::MOVSSrr:
    // X86::MOVSDrr is always commutable. MOVSS is only commutable if we can
    // form sse4.1 blend. We assume VMOVSSrr/VMOVSDrr is always commutable since
    // AVX implies sse4.1.
    if (Subtarget.hasSSE41())
      return TargetInstrInfo::findCommutedOpIndices(MI, SrcOpIdx1, SrcOpIdx2);
    return false;
  case X86::SHUFPDrri:
    // We can commute this to MOVSD.
    if (MI.getOperand(3).getImm() == 0x02)
      return TargetInstrInfo::findCommutedOpIndices(MI, SrcOpIdx1, SrcOpIdx2);
    return false;
  case X86::MOVHLPSrr:
  case X86::UNPCKHPDrr:
  case X86::VMOVHLPSrr:
  case X86::VUNPCKHPDrr:
  case X86::VMOVHLPSZrr:
  case X86::VUNPCKHPDZ128rr:
    if (Subtarget.hasSSE2())
      return TargetInstrInfo::findCommutedOpIndices(MI, SrcOpIdx1, SrcOpIdx2);
    return false;
  case X86::VPTERNLOGDZrri:
  case X86::VPTERNLOGDZrmi:
  case X86::VPTERNLOGDZ128rri:
  case X86::VPTERNLOGDZ128rmi:
  case X86::VPTERNLOGDZ256rri:
  case X86::VPTERNLOGDZ256rmi:
  case X86::VPTERNLOGQZrri:
  case X86::VPTERNLOGQZrmi:
  case X86::VPTERNLOGQZ128rri:
  case X86::VPTERNLOGQZ128rmi:
  case X86::VPTERNLOGQZ256rri:
  case X86::VPTERNLOGQZ256rmi:
  case X86::VPTERNLOGDZrrik:
  case X86::VPTERNLOGDZ128rrik:
  case X86::VPTERNLOGDZ256rrik:
  case X86::VPTERNLOGQZrrik:
  case X86::VPTERNLOGQZ128rrik:
  case X86::VPTERNLOGQZ256rrik:
  case X86::VPTERNLOGDZrrikz:
  case X86::VPTERNLOGDZrmikz:
  case X86::VPTERNLOGDZ128rrikz:
  case X86::VPTERNLOGDZ128rmikz:
  case X86::VPTERNLOGDZ256rrikz:
  case X86::VPTERNLOGDZ256rmikz:
  case X86::VPTERNLOGQZrrikz:
  case X86::VPTERNLOGQZrmikz:
  case X86::VPTERNLOGQZ128rrikz:
  case X86::VPTERNLOGQZ128rmikz:
  case X86::VPTERNLOGQZ256rrikz:
  case X86::VPTERNLOGQZ256rmikz:
  case X86::VPTERNLOGDZ128rmbi:
  case X86::VPTERNLOGDZ256rmbi:
  case X86::VPTERNLOGDZrmbi:
  case X86::VPTERNLOGQZ128rmbi:
  case X86::VPTERNLOGQZ256rmbi:
  case X86::VPTERNLOGQZrmbi:
  case X86::VPTERNLOGDZ128rmbikz:
  case X86::VPTERNLOGDZ256rmbikz:
  case X86::VPTERNLOGDZrmbikz:
  case X86::VPTERNLOGQZ128rmbikz:
  case X86::VPTERNLOGQZ256rmbikz:
  case X86::VPTERNLOGQZrmbikz:
    return findThreeSrcCommutedOpIndices(MI, SrcOpIdx1, SrcOpIdx2);
  case X86::VPDPWSSDYrr:
  case X86::VPDPWSSDrr:
  case X86::VPDPWSSDSYrr:
  case X86::VPDPWSSDSrr:
  case X86::VPDPWUUDrr:
  case X86::VPDPWUUDYrr:
  case X86::VPDPWUUDSrr:
  case X86::VPDPWUUDSYrr:
  case X86::VPDPBSSDSrr:
  case X86::VPDPBSSDSYrr:
  case X86::VPDPBSSDrr:
  case X86::VPDPBSSDYrr:
  case X86::VPDPBUUDSrr:
  case X86::VPDPBUUDSYrr:
  case X86::VPDPBUUDrr:
  case X86::VPDPBUUDYrr:
  case X86::VPDPWSSDZ128r:
  case X86::VPDPWSSDZ128rk:
  case X86::VPDPWSSDZ128rkz:
  case X86::VPDPWSSDZ256r:
  case X86::VPDPWSSDZ256rk:
  case X86::VPDPWSSDZ256rkz:
  case X86::VPDPWSSDZr:
  case X86::VPDPWSSDZrk:
  case X86::VPDPWSSDZrkz:
  case X86::VPDPWSSDSZ128r:
  case X86::VPDPWSSDSZ128rk:
  case X86::VPDPWSSDSZ128rkz:
  case X86::VPDPWSSDSZ256r:
  case X86::VPDPWSSDSZ256rk:
  case X86::VPDPWSSDSZ256rkz:
  case X86::VPDPWSSDSZr:
  case X86::VPDPWSSDSZrk:
  case X86::VPDPWSSDSZrkz:
  case X86::VPMADD52HUQrr:
  case X86::VPMADD52HUQYrr:
  case X86::VPMADD52HUQZ128r:
  case X86::VPMADD52HUQZ128rk:
  case X86::VPMADD52HUQZ128rkz:
  case X86::VPMADD52HUQZ256r:
  case X86::VPMADD52HUQZ256rk:
  case X86::VPMADD52HUQZ256rkz:
  case X86::VPMADD52HUQZr:
  case X86::VPMADD52HUQZrk:
  case X86::VPMADD52HUQZrkz:
  case X86::VPMADD52LUQrr:
  case X86::VPMADD52LUQYrr:
  case X86::VPMADD52LUQZ128r:
  case X86::VPMADD52LUQZ128rk:
  case X86::VPMADD52LUQZ128rkz:
  case X86::VPMADD52LUQZ256r:
  case X86::VPMADD52LUQZ256rk:
  case X86::VPMADD52LUQZ256rkz:
  case X86::VPMADD52LUQZr:
  case X86::VPMADD52LUQZrk:
  case X86::VPMADD52LUQZrkz:
  case X86::VFMADDCPHZr:
  case X86::VFMADDCPHZrk:
  case X86::VFMADDCPHZrkz:
  case X86::VFMADDCPHZ128r:
  case X86::VFMADDCPHZ128rk:
  case X86::VFMADDCPHZ128rkz:
  case X86::VFMADDCPHZ256r:
  case X86::VFMADDCPHZ256rk:
  case X86::VFMADDCPHZ256rkz:
  case X86::VFMADDCSHZr:
  case X86::VFMADDCSHZrk:
  case X86::VFMADDCSHZrkz: {
    unsigned CommutableOpIdx1 = 2;
    unsigned CommutableOpIdx2 = 3;
    if (X86II::isKMasked(Desc.TSFlags)) {
      // Skip the mask register.
      ++CommutableOpIdx1;
      ++CommutableOpIdx2;
    }
    if (!fixCommutedOpIndices(SrcOpIdx1, SrcOpIdx2, CommutableOpIdx1,
                              CommutableOpIdx2))
      return false;
    if (!MI.getOperand(SrcOpIdx1).isReg() || !MI.getOperand(SrcOpIdx2).isReg())
      // No idea.
      return false;
    return true;
  }

  default:
    const X86InstrFMA3Group *FMA3Group =
        getFMA3Group(MI.getOpcode(), MI.getDesc().TSFlags);
    if (FMA3Group)
      return findThreeSrcCommutedOpIndices(MI, SrcOpIdx1, SrcOpIdx2,
                                           FMA3Group->isIntrinsic());

    // Handled masked instructions since we need to skip over the mask input
    // and the preserved input.
    if (X86II::isKMasked(Desc.TSFlags)) {
      // First assume that the first input is the mask operand and skip past it.
      unsigned CommutableOpIdx1 = Desc.getNumDefs() + 1;
      unsigned CommutableOpIdx2 = Desc.getNumDefs() + 2;
      // Check if the first input is tied. If there isn't one then we only
      // need to skip the mask operand which we did above.
      if ((MI.getDesc().getOperandConstraint(Desc.getNumDefs(),
                                             MCOI::TIED_TO) != -1)) {
        // If this is zero masking instruction with a tied operand, we need to
        // move the first index back to the first input since this must
        // be a 3 input instruction and we want the first two non-mask inputs.
        // Otherwise this is a 2 input instruction with a preserved input and
        // mask, so we need to move the indices to skip one more input.
        if (X86II::isKMergeMasked(Desc.TSFlags)) {
          ++CommutableOpIdx1;
          ++CommutableOpIdx2;
        } else {
          --CommutableOpIdx1;
        }
      }

      if (!fixCommutedOpIndices(SrcOpIdx1, SrcOpIdx2, CommutableOpIdx1,
                                CommutableOpIdx2))
        return false;

      if (!MI.getOperand(SrcOpIdx1).isReg() ||
          !MI.getOperand(SrcOpIdx2).isReg())
        // No idea.
        return false;
      return true;
    }

    return TargetInstrInfo::findCommutedOpIndices(MI, SrcOpIdx1, SrcOpIdx2);
  }
  return false;
}

static bool isConvertibleLEA(MachineInstr *MI) {
  unsigned Opcode = MI->getOpcode();
  if (Opcode != X86::LEA32r && Opcode != X86::LEA64r &&
      Opcode != X86::LEA64_32r)
    return false;

  const MachineOperand &Scale = MI->getOperand(1 + X86::AddrScaleAmt);
  const MachineOperand &Disp = MI->getOperand(1 + X86::AddrDisp);
  const MachineOperand &Segment = MI->getOperand(1 + X86::AddrSegmentReg);

  if (Segment.getReg() != 0 || !Disp.isImm() || Disp.getImm() != 0 ||
      Scale.getImm() > 1)
    return false;

  return true;
}

bool X86InstrInfo::hasCommutePreference(MachineInstr &MI, bool &Commute) const {
  // Currently we're interested in following sequence only.
  //   r3 = lea r1, r2
  //   r5 = add r3, r4
  // Both r3 and r4 are killed in add, we hope the add instruction has the
  // operand order
  //   r5 = add r4, r3
  // So later in X86FixupLEAs the lea instruction can be rewritten as add.
  unsigned Opcode = MI.getOpcode();
  if (Opcode != X86::ADD32rr && Opcode != X86::ADD64rr)
    return false;

  const MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
  Register Reg1 = MI.getOperand(1).getReg();
  Register Reg2 = MI.getOperand(2).getReg();

  // Check if Reg1 comes from LEA in the same MBB.
  if (MachineInstr *Inst = MRI.getUniqueVRegDef(Reg1)) {
    if (isConvertibleLEA(Inst) && Inst->getParent() == MI.getParent()) {
      Commute = true;
      return true;
    }
  }

  // Check if Reg2 comes from LEA in the same MBB.
  if (MachineInstr *Inst = MRI.getUniqueVRegDef(Reg2)) {
    if (isConvertibleLEA(Inst) && Inst->getParent() == MI.getParent()) {
      Commute = false;
      return true;
    }
  }

  return false;
}

int X86::getCondSrcNoFromDesc(const MCInstrDesc &MCID) {
  unsigned Opcode = MCID.getOpcode();
  if (!(X86::isJCC(Opcode) || X86::isSETCC(Opcode) || X86::isCMOVCC(Opcode) ||
        X86::isCFCMOVCC(Opcode) || X86::isCCMPCC(Opcode) ||
        X86::isCTESTCC(Opcode)))
    return -1;
  // Assume that condition code is always the last use operand.
  unsigned NumUses = MCID.getNumOperands() - MCID.getNumDefs();
  return NumUses - 1;
}

X86::CondCode X86::getCondFromMI(const MachineInstr &MI) {
  const MCInstrDesc &MCID = MI.getDesc();
  int CondNo = getCondSrcNoFromDesc(MCID);
  if (CondNo < 0)
    return X86::COND_INVALID;
  CondNo += MCID.getNumDefs();
  return static_cast<X86::CondCode>(MI.getOperand(CondNo).getImm());
}

X86::CondCode X86::getCondFromBranch(const MachineInstr &MI) {
  return X86::isJCC(MI.getOpcode()) ? X86::getCondFromMI(MI)
                                    : X86::COND_INVALID;
}

X86::CondCode X86::getCondFromSETCC(const MachineInstr &MI) {
  return X86::isSETCC(MI.getOpcode()) ? X86::getCondFromMI(MI)
                                      : X86::COND_INVALID;
}

X86::CondCode X86::getCondFromCMov(const MachineInstr &MI) {
  return X86::isCMOVCC(MI.getOpcode()) ? X86::getCondFromMI(MI)
                                       : X86::COND_INVALID;
}

X86::CondCode X86::getCondFromCFCMov(const MachineInstr &MI) {
  return X86::isCFCMOVCC(MI.getOpcode()) ? X86::getCondFromMI(MI)
                                         : X86::COND_INVALID;
}

X86::CondCode X86::getCondFromCCMP(const MachineInstr &MI) {
  return X86::isCCMPCC(MI.getOpcode()) || X86::isCTESTCC(MI.getOpcode())
             ? X86::getCondFromMI(MI)
             : X86::COND_INVALID;
}

int X86::getCCMPCondFlagsFromCondCode(X86::CondCode CC) {
  // CCMP/CTEST has two conditional operands:
  // - SCC: source conditonal code (same as CMOV)
  // - DCF: destination conditional flags, which has 4 valid bits
  //
  // +----+----+----+----+
  // | OF | SF | ZF | CF |
  // +----+----+----+----+
  //
  // If SCC(source conditional code) evaluates to false, CCMP/CTEST will updates
  // the conditional flags by as follows:
  //
  // OF = DCF.OF
  // SF = DCF.SF
  // ZF = DCF.ZF
  // CF = DCF.CF
  // PF = DCF.CF
  // AF = 0 (Auxiliary Carry Flag)
  //
  // Otherwise, the CMP or TEST is executed and it updates the
  // CSPAZO flags normally.
  //
  // NOTE:
  // If SCC = P, then SCC evaluates to true regardless of the CSPAZO value.
  // If SCC = NP, then SCC evaluates to false regardless of the CSPAZO value.

  enum { CF = 1, ZF = 2, SF = 4, OF = 8, PF = CF };

  switch (CC) {
  default:
    llvm_unreachable("Illegal condition code!");
  case X86::COND_NO:
  case X86::COND_NE:
  case X86::COND_GE:
  case X86::COND_G:
  case X86::COND_AE:
  case X86::COND_A:
  case X86::COND_NS:
  case X86::COND_NP:
    return 0;
  case X86::COND_O:
    return OF;
  case X86::COND_B:
  case X86::COND_BE:
    return CF;
    break;
  case X86::COND_E:
  case X86::COND_LE:
    return ZF;
  case X86::COND_S:
  case X86::COND_L:
    return SF;
  case X86::COND_P:
    return PF;
  }
}

#define GET_X86_NF_TRANSFORM_TABLE
#define GET_X86_ND2NONND_TABLE
#include "X86GenInstrMapping.inc"

static unsigned getNewOpcFromTable(ArrayRef<X86TableEntry> Table,
                                   unsigned Opc) {
  const auto I = llvm::lower_bound(Table, Opc);
  return (I == Table.end() || I->OldOpc != Opc) ? 0U : I->NewOpc;
}
unsigned X86::getNFVariant(unsigned Opc) {
  return getNewOpcFromTable(X86NFTransformTable, Opc);
}

unsigned X86::getNonNDVariant(unsigned Opc) {
  return getNewOpcFromTable(X86ND2NonNDTable, Opc);
}

/// Return the inverse of the specified condition,
/// e.g. turning COND_E to COND_NE.
X86::CondCode X86::GetOppositeBranchCondition(X86::CondCode CC) {
  switch (CC) {
  default:
    llvm_unreachable("Illegal condition code!");
  case X86::COND_E:
    return X86::COND_NE;
  case X86::COND_NE:
    return X86::COND_E;
  case X86::COND_L:
    return X86::COND_GE;
  case X86::COND_LE:
    return X86::COND_G;
  case X86::COND_G:
    return X86::COND_LE;
  case X86::COND_GE:
    return X86::COND_L;
  case X86::COND_B:
    return X86::COND_AE;
  case X86::COND_BE:
    return X86::COND_A;
  case X86::COND_A:
    return X86::COND_BE;
  case X86::COND_AE:
    return X86::COND_B;
  case X86::COND_S:
    return X86::COND_NS;
  case X86::COND_NS:
    return X86::COND_S;
  case X86::COND_P:
    return X86::COND_NP;
  case X86::COND_NP:
    return X86::COND_P;
  case X86::COND_O:
    return X86::COND_NO;
  case X86::COND_NO:
    return X86::COND_O;
  case X86::COND_NE_OR_P:
    return X86::COND_E_AND_NP;
  case X86::COND_E_AND_NP:
    return X86::COND_NE_OR_P;
  }
}

/// Assuming the flags are set by MI(a,b), return the condition code if we
/// modify the instructions such that flags are set by MI(b,a).
static X86::CondCode getSwappedCondition(X86::CondCode CC) {
  switch (CC) {
  default:
    return X86::COND_INVALID;
  case X86::COND_E:
    return X86::COND_E;
  case X86::COND_NE:
    return X86::COND_NE;
  case X86::COND_L:
    return X86::COND_G;
  case X86::COND_LE:
    return X86::COND_GE;
  case X86::COND_G:
    return X86::COND_L;
  case X86::COND_GE:
    return X86::COND_LE;
  case X86::COND_B:
    return X86::COND_A;
  case X86::COND_BE:
    return X86::COND_AE;
  case X86::COND_A:
    return X86::COND_B;
  case X86::COND_AE:
    return X86::COND_BE;
  }
}

std::pair<X86::CondCode, bool>
X86::getX86ConditionCode(CmpInst::Predicate Predicate) {
  X86::CondCode CC = X86::COND_INVALID;
  bool NeedSwap = false;
  switch (Predicate) {
  default:
    break;
  // Floating-point Predicates
  case CmpInst::FCMP_UEQ:
    CC = X86::COND_E;
    break;
  case CmpInst::FCMP_OLT:
    NeedSwap = true;
    [[fallthrough]];
  case CmpInst::FCMP_OGT:
    CC = X86::COND_A;
    break;
  case CmpInst::FCMP_OLE:
    NeedSwap = true;
    [[fallthrough]];
  case CmpInst::FCMP_OGE:
    CC = X86::COND_AE;
    break;
  case CmpInst::FCMP_UGT:
    NeedSwap = true;
    [[fallthrough]];
  case CmpInst::FCMP_ULT:
    CC = X86::COND_B;
    break;
  case CmpInst::FCMP_UGE:
    NeedSwap = true;
    [[fallthrough]];
  case CmpInst::FCMP_ULE:
    CC = X86::COND_BE;
    break;
  case CmpInst::FCMP_ONE:
    CC = X86::COND_NE;
    break;
  case CmpInst::FCMP_UNO:
    CC = X86::COND_P;
    break;
  case CmpInst::FCMP_ORD:
    CC = X86::COND_NP;
    break;
  case CmpInst::FCMP_OEQ:
    [[fallthrough]];
  case CmpInst::FCMP_UNE:
    CC = X86::COND_INVALID;
    break;

  // Integer Predicates
  case CmpInst::ICMP_EQ:
    CC = X86::COND_E;
    break;
  case CmpInst::ICMP_NE:
    CC = X86::COND_NE;
    break;
  case CmpInst::ICMP_UGT:
    CC = X86::COND_A;
    break;
  case CmpInst::ICMP_UGE:
    CC = X86::COND_AE;
    break;
  case CmpInst::ICMP_ULT:
    CC = X86::COND_B;
    break;
  case CmpInst::ICMP_ULE:
    CC = X86::COND_BE;
    break;
  case CmpInst::ICMP_SGT:
    CC = X86::COND_G;
    break;
  case CmpInst::ICMP_SGE:
    CC = X86::COND_GE;
    break;
  case CmpInst::ICMP_SLT:
    CC = X86::COND_L;
    break;
  case CmpInst::ICMP_SLE:
    CC = X86::COND_LE;
    break;
  }

  return std::make_pair(CC, NeedSwap);
}

/// Return a cmov opcode for the given register size in bytes, and operand type.
unsigned X86::getCMovOpcode(unsigned RegBytes, bool HasMemoryOperand,
                            bool HasNDD) {
  switch (RegBytes) {
  default:
    llvm_unreachable("Illegal register size!");
#define GET_ND_IF_ENABLED(OPC) (HasNDD ? OPC##_ND : OPC)
  case 2:
    return HasMemoryOperand ? GET_ND_IF_ENABLED(X86::CMOV16rm)
                            : GET_ND_IF_ENABLED(X86::CMOV16rr);
  case 4:
    return HasMemoryOperand ? GET_ND_IF_ENABLED(X86::CMOV32rm)
                            : GET_ND_IF_ENABLED(X86::CMOV32rr);
  case 8:
    return HasMemoryOperand ? GET_ND_IF_ENABLED(X86::CMOV64rm)
                            : GET_ND_IF_ENABLED(X86::CMOV64rr);
  }
}

/// Get the VPCMP immediate for the given condition.
unsigned X86::getVPCMPImmForCond(ISD::CondCode CC) {
  switch (CC) {
  default:
    llvm_unreachable("Unexpected SETCC condition");
  case ISD::SETNE:
    return 4;
  case ISD::SETEQ:
    return 0;
  case ISD::SETULT:
  case ISD::SETLT:
    return 1;
  case ISD::SETUGT:
  case ISD::SETGT:
    return 6;
  case ISD::SETUGE:
  case ISD::SETGE:
    return 5;
  case ISD::SETULE:
  case ISD::SETLE:
    return 2;
  }
}

/// Get the VPCMP immediate if the operands are swapped.
unsigned X86::getSwappedVPCMPImm(unsigned Imm) {
  switch (Imm) {
  default:
    llvm_unreachable("Unreachable!");
  case 0x01:
    Imm = 0x06;
    break; // LT  -> NLE
  case 0x02:
    Imm = 0x05;
    break; // LE  -> NLT
  case 0x05:
    Imm = 0x02;
    break; // NLT -> LE
  case 0x06:
    Imm = 0x01;
    break;   // NLE -> LT
  case 0x00: // EQ
  case 0x03: // FALSE
  case 0x04: // NE
  case 0x07: // TRUE
    break;
  }

  return Imm;
}

/// Get the VPCOM immediate if the operands are swapped.
unsigned X86::getSwappedVPCOMImm(unsigned Imm) {
  switch (Imm) {
  default:
    llvm_unreachable("Unreachable!");
  case 0x00:
    Imm = 0x02;
    break; // LT -> GT
  case 0x01:
    Imm = 0x03;
    break; // LE -> GE
  case 0x02:
    Imm = 0x00;
    break; // GT -> LT
  case 0x03:
    Imm = 0x01;
    break;   // GE -> LE
  case 0x04: // EQ
  case 0x05: // NE
  case 0x06: // FALSE
  case 0x07: // TRUE
    break;
  }

  return Imm;
}

/// Get the VCMP immediate if the operands are swapped.
unsigned X86::getSwappedVCMPImm(unsigned Imm) {
  // Only need the lower 2 bits to distinquish.
  switch (Imm & 0x3) {
  default:
    llvm_unreachable("Unreachable!");
  case 0x00:
  case 0x03:
    // EQ/NE/TRUE/FALSE/ORD/UNORD don't change immediate when commuted.
    break;
  case 0x01:
  case 0x02:
    // Need to toggle bits 3:0. Bit 4 stays the same.
    Imm ^= 0xf;
    break;
  }

  return Imm;
}

unsigned X86::getVectorRegisterWidth(const MCOperandInfo &Info) {
  if (Info.RegClass == X86::VR128RegClassID ||
      Info.RegClass == X86::VR128XRegClassID)
    return 128;
  if (Info.RegClass == X86::VR256RegClassID ||
      Info.RegClass == X86::VR256XRegClassID)
    return 256;
  if (Info.RegClass == X86::VR512RegClassID)
    return 512;
  llvm_unreachable("Unknown register class!");
}

/// Return true if the Reg is X87 register.
static bool isX87Reg(unsigned Reg) {
  return (Reg == X86::FPCW || Reg == X86::FPSW ||
          (Reg >= X86::ST0 && Reg <= X86::ST7));
}

/// check if the instruction is X87 instruction
bool X86::isX87Instruction(MachineInstr &MI) {
  // Call defs X87 register, so we special case it here because
  // otherwise calls are incorrectly flagged as x87 instructions
  // as a result.
  if (MI.isCall())
    return false;
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg())
      continue;
    if (isX87Reg(MO.getReg()))
      return true;
  }
  return false;
}

int X86::getFirstAddrOperandIdx(const MachineInstr &MI) {
  auto IsMemOp = [](const MCOperandInfo &OpInfo) {
    return OpInfo.OperandType == MCOI::OPERAND_MEMORY;
  };

  const MCInstrDesc &Desc = MI.getDesc();

  // Directly invoke the MC-layer routine for real (i.e., non-pseudo)
  // instructions (fast case).
  if (!X86II::isPseudo(Desc.TSFlags)) {
    int MemRefIdx = X86II::getMemoryOperandNo(Desc.TSFlags);
    if (MemRefIdx >= 0)
      return MemRefIdx + X86II::getOperandBias(Desc);
#ifdef EXPENSIVE_CHECKS
    assert(none_of(Desc.operands(), IsMemOp) &&
           "Got false negative from X86II::getMemoryOperandNo()!");
#endif
    return -1;
  }

  // Otherwise, handle pseudo instructions by examining the type of their
  // operands (slow case). An instruction cannot have a memory reference if it
  // has fewer than AddrNumOperands (= 5) explicit operands.
  unsigned NumOps = Desc.getNumOperands();
  if (NumOps < X86::AddrNumOperands) {
#ifdef EXPENSIVE_CHECKS
    assert(none_of(Desc.operands(), IsMemOp) &&
           "Expected no operands to have OPERAND_MEMORY type!");
#endif
    return -1;
  }

  // The first operand with type OPERAND_MEMORY indicates the start of a memory
  // reference. We expect the following AddrNumOperand-1 operands to also have
  // OPERAND_MEMORY type.
  for (unsigned I = 0, E = NumOps - X86::AddrNumOperands; I != E; ++I) {
    if (IsMemOp(Desc.operands()[I])) {
#ifdef EXPENSIVE_CHECKS
      assert(std::all_of(Desc.operands().begin() + I,
                         Desc.operands().begin() + I + X86::AddrNumOperands,
                         IsMemOp) &&
             "Expected all five operands in the memory reference to have "
             "OPERAND_MEMORY type!");
#endif
      return I;
    }
  }

  return -1;
}

const Constant *X86::getConstantFromPool(const MachineInstr &MI,
                                         unsigned OpNo) {
  assert(MI.getNumOperands() >= (OpNo + X86::AddrNumOperands) &&
         "Unexpected number of operands!");

  const MachineOperand &Index = MI.getOperand(OpNo + X86::AddrIndexReg);
  if (!Index.isReg() || Index.getReg() != X86::NoRegister)
    return nullptr;

  const MachineOperand &Disp = MI.getOperand(OpNo + X86::AddrDisp);
  if (!Disp.isCPI() || Disp.getOffset() != 0)
    return nullptr;

  ArrayRef<MachineConstantPoolEntry> Constants =
      MI.getParent()->getParent()->getConstantPool()->getConstants();
  const MachineConstantPoolEntry &ConstantEntry = Constants[Disp.getIndex()];

  // Bail if this is a machine constant pool entry, we won't be able to dig out
  // anything useful.
  if (ConstantEntry.isMachineConstantPoolEntry())
    return nullptr;

  return ConstantEntry.Val.ConstVal;
}

bool X86InstrInfo::isUnconditionalTailCall(const MachineInstr &MI) const {
  switch (MI.getOpcode()) {
  case X86::TCRETURNdi:
  case X86::TCRETURNri:
  case X86::TCRETURNmi:
  case X86::TCRETURNdi64:
  case X86::TCRETURNri64:
  case X86::TCRETURNmi64:
    return true;
  default:
    return false;
  }
}

bool X86InstrInfo::canMakeTailCallConditional(
    SmallVectorImpl<MachineOperand> &BranchCond,
    const MachineInstr &TailCall) const {

  const MachineFunction *MF = TailCall.getMF();

  if (MF->getTarget().getCodeModel() == CodeModel::Kernel) {
    // Kernel patches thunk calls in runtime, these should never be conditional.
    const MachineOperand &Target = TailCall.getOperand(0);
    if (Target.isSymbol()) {
      StringRef Symbol(Target.getSymbolName());
      // this is currently only relevant to r11/kernel indirect thunk.
      if (Symbol == "__x86_indirect_thunk_r11")
        return false;
    }
  }

  if (TailCall.getOpcode() != X86::TCRETURNdi &&
      TailCall.getOpcode() != X86::TCRETURNdi64) {
    // Only direct calls can be done with a conditional branch.
    return false;
  }

  if (Subtarget.isTargetWin64() && MF->hasWinCFI()) {
    // Conditional tail calls confuse the Win64 unwinder.
    return false;
  }

  assert(BranchCond.size() == 1);
  if (BranchCond[0].getImm() > X86::LAST_VALID_COND) {
    // Can't make a conditional tail call with this condition.
    return false;
  }

  const X86MachineFunctionInfo *X86FI = MF->getInfo<X86MachineFunctionInfo>();
  if (X86FI->getTCReturnAddrDelta() != 0 ||
      TailCall.getOperand(1).getImm() != 0) {
    // A conditional tail call cannot do any stack adjustment.
    return false;
  }

  return true;
}

void X86InstrInfo::replaceBranchWithTailCall(
    MachineBasicBlock &MBB, SmallVectorImpl<MachineOperand> &BranchCond,
    const MachineInstr &TailCall) const {
  assert(canMakeTailCallConditional(BranchCond, TailCall));

  MachineBasicBlock::iterator I = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (!I->isBranch())
      assert(0 && "Can't find the branch to replace!");

    X86::CondCode CC = X86::getCondFromBranch(*I);
    assert(BranchCond.size() == 1);
    if (CC != BranchCond[0].getImm())
      continue;

    break;
  }

  unsigned Opc = TailCall.getOpcode() == X86::TCRETURNdi ? X86::TCRETURNdicc
                                                         : X86::TCRETURNdi64cc;

  auto MIB = BuildMI(MBB, I, MBB.findDebugLoc(I), get(Opc));
  MIB->addOperand(TailCall.getOperand(0)); // Destination.
  MIB.addImm(0);                           // Stack offset (not used).
  MIB->addOperand(BranchCond[0]);          // Condition.
  MIB.copyImplicitOps(TailCall);           // Regmask and (imp-used) parameters.

  // Add implicit uses and defs of all live regs potentially clobbered by the
  // call. This way they still appear live across the call.
  LivePhysRegs LiveRegs(getRegisterInfo());
  LiveRegs.addLiveOuts(MBB);
  SmallVector<std::pair<MCPhysReg, const MachineOperand *>, 8> Clobbers;
  LiveRegs.stepForward(*MIB, Clobbers);
  for (const auto &C : Clobbers) {
    MIB.addReg(C.first, RegState::Implicit);
    MIB.addReg(C.first, RegState::Implicit | RegState::Define);
  }

  I->eraseFromParent();
}

// Given a MBB and its TBB, find the FBB which was a fallthrough MBB (it may
// not be a fallthrough MBB now due to layout changes). Return nullptr if the
// fallthrough MBB cannot be identified.
static MachineBasicBlock *getFallThroughMBB(MachineBasicBlock *MBB,
                                            MachineBasicBlock *TBB) {
  // Look for non-EHPad successors other than TBB. If we find exactly one, it
  // is the fallthrough MBB. If we find zero, then TBB is both the target MBB
  // and fallthrough MBB. If we find more than one, we cannot identify the
  // fallthrough MBB and should return nullptr.
  MachineBasicBlock *FallthroughBB = nullptr;
  for (MachineBasicBlock *Succ : MBB->successors()) {
    if (Succ->isEHPad() || (Succ == TBB && FallthroughBB))
      continue;
    // Return a nullptr if we found more than one fallthrough successor.
    if (FallthroughBB && FallthroughBB != TBB)
      return nullptr;
    FallthroughBB = Succ;
  }
  return FallthroughBB;
}

bool X86InstrInfo::analyzeBranchImpl(
    MachineBasicBlock &MBB, MachineBasicBlock *&TBB, MachineBasicBlock *&FBB,
    SmallVectorImpl<MachineOperand> &Cond,
    SmallVectorImpl<MachineInstr *> &CondBranches, bool AllowModify) const {

  // Start from the bottom of the block and work up, examining the
  // terminator instructions.
  MachineBasicBlock::iterator I = MBB.end();
  MachineBasicBlock::iterator UnCondBrIter = MBB.end();
  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;

    // Working from the bottom, when we see a non-terminator instruction, we're
    // done.
    if (!isUnpredicatedTerminator(*I))
      break;

    // A terminator that isn't a branch can't easily be handled by this
    // analysis.
    if (!I->isBranch())
      return true;

    // Handle unconditional branches.
    if (I->getOpcode() == X86::JMP_1) {
      UnCondBrIter = I;

      if (!AllowModify) {
        TBB = I->getOperand(0).getMBB();
        continue;
      }

      // If the block has any instructions after a JMP, delete them.
      MBB.erase(std::next(I), MBB.end());

      Cond.clear();
      FBB = nullptr;

      // Delete the JMP if it's equivalent to a fall-through.
      if (MBB.isLayoutSuccessor(I->getOperand(0).getMBB())) {
        TBB = nullptr;
        I->eraseFromParent();
        I = MBB.end();
        UnCondBrIter = MBB.end();
        continue;
      }

      // TBB is used to indicate the unconditional destination.
      TBB = I->getOperand(0).getMBB();
      continue;
    }

    // Handle conditional branches.
    X86::CondCode BranchCode = X86::getCondFromBranch(*I);
    if (BranchCode == X86::COND_INVALID)
      return true; // Can't handle indirect branch.

    // In practice we should never have an undef eflags operand, if we do
    // abort here as we are not prepared to preserve the flag.
    if (I->findRegisterUseOperand(X86::EFLAGS, /*TRI=*/nullptr)->isUndef())
      return true;

    // Working from the bottom, handle the first conditional branch.
    if (Cond.empty()) {
      FBB = TBB;
      TBB = I->getOperand(0).getMBB();
      Cond.push_back(MachineOperand::CreateImm(BranchCode));
      CondBranches.push_back(&*I);
      continue;
    }

    // Handle subsequent conditional branches. Only handle the case where all
    // conditional branches branch to the same destination and their condition
    // opcodes fit one of the special multi-branch idioms.
    assert(Cond.size() == 1);
    assert(TBB);

    // If the conditions are the same, we can leave them alone.
    X86::CondCode OldBranchCode = (X86::CondCode)Cond[0].getImm();
    auto NewTBB = I->getOperand(0).getMBB();
    if (OldBranchCode == BranchCode && TBB == NewTBB)
      continue;

    // If they differ, see if they fit one of the known patterns. Theoretically,
    // we could handle more patterns here, but we shouldn't expect to see them
    // if instruction selection has done a reasonable job.
    if (TBB == NewTBB &&
        ((OldBranchCode == X86::COND_P && BranchCode == X86::COND_NE) ||
         (OldBranchCode == X86::COND_NE && BranchCode == X86::COND_P))) {
      BranchCode = X86::COND_NE_OR_P;
    } else if ((OldBranchCode == X86::COND_NP && BranchCode == X86::COND_NE) ||
               (OldBranchCode == X86::COND_E && BranchCode == X86::COND_P)) {
      if (NewTBB != (FBB ? FBB : getFallThroughMBB(&MBB, TBB)))
        return true;

      // X86::COND_E_AND_NP usually has two different branch destinations.
      //
      // JP B1
      // JE B2
      // JMP B1
      // B1:
      // B2:
      //
      // Here this condition branches to B2 only if NP && E. It has another
      // equivalent form:
      //
      // JNE B1
      // JNP B2
      // JMP B1
      // B1:
      // B2:
      //
      // Similarly it branches to B2 only if E && NP. That is why this condition
      // is named with COND_E_AND_NP.
      BranchCode = X86::COND_E_AND_NP;
    } else
      return true;

    // Update the MachineOperand.
    Cond[0].setImm(BranchCode);
    CondBranches.push_back(&*I);
  }

  return false;
}

bool X86InstrInfo::analyzeBranch(MachineBasicBlock &MBB,
                                 MachineBasicBlock *&TBB,
                                 MachineBasicBlock *&FBB,
                                 SmallVectorImpl<MachineOperand> &Cond,
                                 bool AllowModify) const {
  SmallVector<MachineInstr *, 4> CondBranches;
  return analyzeBranchImpl(MBB, TBB, FBB, Cond, CondBranches, AllowModify);
}

static int getJumpTableIndexFromAddr(const MachineInstr &MI) {
  const MCInstrDesc &Desc = MI.getDesc();
  int MemRefBegin = X86II::getMemoryOperandNo(Desc.TSFlags);
  assert(MemRefBegin >= 0 && "instr should have memory operand");
  MemRefBegin += X86II::getOperandBias(Desc);

  const MachineOperand &MO = MI.getOperand(MemRefBegin + X86::AddrDisp);
  if (!MO.isJTI())
    return -1;

  return MO.getIndex();
}

static int getJumpTableIndexFromReg(const MachineRegisterInfo &MRI,
                                    Register Reg) {
  if (!Reg.isVirtual())
    return -1;
  MachineInstr *MI = MRI.getUniqueVRegDef(Reg);
  if (MI == nullptr)
    return -1;
  unsigned Opcode = MI->getOpcode();
  if (Opcode != X86::LEA64r && Opcode != X86::LEA32r)
    return -1;
  return getJumpTableIndexFromAddr(*MI);
}

int X86InstrInfo::getJumpTableIndex(const MachineInstr &MI) const {
  unsigned Opcode = MI.getOpcode();
  // Switch-jump pattern for non-PIC code looks like:
  //   JMP64m $noreg, 8, %X, %jump-table.X, $noreg
  if (Opcode == X86::JMP64m || Opcode == X86::JMP32m) {
    return getJumpTableIndexFromAddr(MI);
  }
  // The pattern for PIC code looks like:
  //   %0 = LEA64r $rip, 1, $noreg, %jump-table.X
  //   %1 = MOVSX64rm32 %0, 4, XX, 0, $noreg
  //   %2 = ADD64rr %1, %0
  //   JMP64r %2
  if (Opcode == X86::JMP64r || Opcode == X86::JMP32r) {
    Register Reg = MI.getOperand(0).getReg();
    if (!Reg.isVirtual())
      return -1;
    const MachineFunction &MF = *MI.getParent()->getParent();
    const MachineRegisterInfo &MRI = MF.getRegInfo();
    MachineInstr *Add = MRI.getUniqueVRegDef(Reg);
    if (Add == nullptr)
      return -1;
    if (Add->getOpcode() != X86::ADD64rr && Add->getOpcode() != X86::ADD32rr)
      return -1;
    int JTI1 = getJumpTableIndexFromReg(MRI, Add->getOperand(1).getReg());
    if (JTI1 >= 0)
      return JTI1;
    int JTI2 = getJumpTableIndexFromReg(MRI, Add->getOperand(2).getReg());
    if (JTI2 >= 0)
      return JTI2;
  }
  return -1;
}

bool X86InstrInfo::analyzeBranchPredicate(MachineBasicBlock &MBB,
                                          MachineBranchPredicate &MBP,
                                          bool AllowModify) const {
  using namespace std::placeholders;

  SmallVector<MachineOperand, 4> Cond;
  SmallVector<MachineInstr *, 4> CondBranches;
  if (analyzeBranchImpl(MBB, MBP.TrueDest, MBP.FalseDest, Cond, CondBranches,
                        AllowModify))
    return true;

  if (Cond.size() != 1)
    return true;

  assert(MBP.TrueDest && "expected!");

  if (!MBP.FalseDest)
    MBP.FalseDest = MBB.getNextNode();

  const TargetRegisterInfo *TRI = &getRegisterInfo();

  MachineInstr *ConditionDef = nullptr;
  bool SingleUseCondition = true;

  for (MachineInstr &MI : llvm::drop_begin(llvm::reverse(MBB))) {
    if (MI.modifiesRegister(X86::EFLAGS, TRI)) {
      ConditionDef = &MI;
      break;
    }

    if (MI.readsRegister(X86::EFLAGS, TRI))
      SingleUseCondition = false;
  }

  if (!ConditionDef)
    return true;

  if (SingleUseCondition) {
    for (auto *Succ : MBB.successors())
      if (Succ->isLiveIn(X86::EFLAGS))
        SingleUseCondition = false;
  }

  MBP.ConditionDef = ConditionDef;
  MBP.SingleUseCondition = SingleUseCondition;

  // Currently we only recognize the simple pattern:
  //
  //   test %reg, %reg
  //   je %label
  //
  const unsigned TestOpcode =
      Subtarget.is64Bit() ? X86::TEST64rr : X86::TEST32rr;

  if (ConditionDef->getOpcode() == TestOpcode &&
      ConditionDef->getNumOperands() == 3 &&
      ConditionDef->getOperand(0).isIdenticalTo(ConditionDef->getOperand(1)) &&
      (Cond[0].getImm() == X86::COND_NE || Cond[0].getImm() == X86::COND_E)) {
    MBP.LHS = ConditionDef->getOperand(0);
    MBP.RHS = MachineOperand::CreateImm(0);
    MBP.Predicate = Cond[0].getImm() == X86::COND_NE
                        ? MachineBranchPredicate::PRED_NE
                        : MachineBranchPredicate::PRED_EQ;
    return false;
  }

  return true;
}

unsigned X86InstrInfo::removeBranch(MachineBasicBlock &MBB,
                                    int *BytesRemoved) const {
  assert(!BytesRemoved && "code size not handled");

  MachineBasicBlock::iterator I = MBB.end();
  unsigned Count = 0;

  while (I != MBB.begin()) {
    --I;
    if (I->isDebugInstr())
      continue;
    if (I->getOpcode() != X86::JMP_1 &&
        X86::getCondFromBranch(*I) == X86::COND_INVALID)
      break;
    // Remove the branch.
    I->eraseFromParent();
    I = MBB.end();
    ++Count;
  }

  return Count;
}

unsigned X86InstrInfo::insertBranch(MachineBasicBlock &MBB,
                                    MachineBasicBlock *TBB,
                                    MachineBasicBlock *FBB,
                                    ArrayRef<MachineOperand> Cond,
                                    const DebugLoc &DL, int *BytesAdded) const {
  // Shouldn't be a fall through.
  assert(TBB && "insertBranch must not be told to insert a fallthrough");
  assert((Cond.size() == 1 || Cond.size() == 0) &&
         "X86 branch conditions have one component!");
  assert(!BytesAdded && "code size not handled");

  if (Cond.empty()) {
    // Unconditional branch?
    assert(!FBB && "Unconditional branch with multiple successors!");
    BuildMI(&MBB, DL, get(X86::JMP_1)).addMBB(TBB);
    return 1;
  }

  // If FBB is null, it is implied to be a fall-through block.
  bool FallThru = FBB == nullptr;

  // Conditional branch.
  unsigned Count = 0;
  X86::CondCode CC = (X86::CondCode)Cond[0].getImm();
  switch (CC) {
  case X86::COND_NE_OR_P:
    // Synthesize NE_OR_P with two branches.
    BuildMI(&MBB, DL, get(X86::JCC_1)).addMBB(TBB).addImm(X86::COND_NE);
    ++Count;
    BuildMI(&MBB, DL, get(X86::JCC_1)).addMBB(TBB).addImm(X86::COND_P);
    ++Count;
    break;
  case X86::COND_E_AND_NP:
    // Use the next block of MBB as FBB if it is null.
    if (FBB == nullptr) {
      FBB = getFallThroughMBB(&MBB, TBB);
      assert(FBB && "MBB cannot be the last block in function when the false "
                    "body is a fall-through.");
    }
    // Synthesize COND_E_AND_NP with two branches.
    BuildMI(&MBB, DL, get(X86::JCC_1)).addMBB(FBB).addImm(X86::COND_NE);
    ++Count;
    BuildMI(&MBB, DL, get(X86::JCC_1)).addMBB(TBB).addImm(X86::COND_NP);
    ++Count;
    break;
  default: {
    BuildMI(&MBB, DL, get(X86::JCC_1)).addMBB(TBB).addImm(CC);
    ++Count;
  }
  }
  if (!FallThru) {
    // Two-way Conditional branch. Insert the second branch.
    BuildMI(&MBB, DL, get(X86::JMP_1)).addMBB(FBB);
    ++Count;
  }
  return Count;
}

bool X86InstrInfo::canInsertSelect(const MachineBasicBlock &MBB,
                                   ArrayRef<MachineOperand> Cond,
                                   Register DstReg, Register TrueReg,
                                   Register FalseReg, int &CondCycles,
                                   int &TrueCycles, int &FalseCycles) const {
  // Not all subtargets have cmov instructions.
  if (!Subtarget.canUseCMOV())
    return false;
  if (Cond.size() != 1)
    return false;
  // We cannot do the composite conditions, at least not in SSA form.
  if ((X86::CondCode)Cond[0].getImm() > X86::LAST_VALID_COND)
    return false;

  // Check register classes.
  const MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetRegisterClass *RC =
      RI.getCommonSubClass(MRI.getRegClass(TrueReg), MRI.getRegClass(FalseReg));
  if (!RC)
    return false;

  // We have cmov instructions for 16, 32, and 64 bit general purpose registers.
  if (X86::GR16RegClass.hasSubClassEq(RC) ||
      X86::GR32RegClass.hasSubClassEq(RC) ||
      X86::GR64RegClass.hasSubClassEq(RC)) {
    // This latency applies to Pentium M, Merom, Wolfdale, Nehalem, and Sandy
    // Bridge. Probably Ivy Bridge as well.
    CondCycles = 2;
    TrueCycles = 2;
    FalseCycles = 2;
    return true;
  }

  // Can't do vectors.
  return false;
}

void X86InstrInfo::insertSelect(MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator I,
                                const DebugLoc &DL, Register DstReg,
                                ArrayRef<MachineOperand> Cond, Register TrueReg,
                                Register FalseReg) const {
  MachineRegisterInfo &MRI = MBB.getParent()->getRegInfo();
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();
  const TargetRegisterClass &RC = *MRI.getRegClass(DstReg);
  assert(Cond.size() == 1 && "Invalid Cond array");
  unsigned Opc =
      X86::getCMovOpcode(TRI.getRegSizeInBits(RC) / 8,
                         false /*HasMemoryOperand*/, Subtarget.hasNDD());
  BuildMI(MBB, I, DL, get(Opc), DstReg)
      .addReg(FalseReg)
      .addReg(TrueReg)
      .addImm(Cond[0].getImm());
}

/// Test if the given register is a physical h register.
static bool isHReg(unsigned Reg) {
  return X86::GR8_ABCD_HRegClass.contains(Reg);
}

// Try and copy between VR128/VR64 and GR64 registers.
static unsigned CopyToFromAsymmetricReg(unsigned DestReg, unsigned SrcReg,
                                        const X86Subtarget &Subtarget) {
  bool HasAVX = Subtarget.hasAVX();
  bool HasAVX512 = Subtarget.hasAVX512();
  bool HasEGPR = Subtarget.hasEGPR();

  // SrcReg(MaskReg) -> DestReg(GR64)
  // SrcReg(MaskReg) -> DestReg(GR32)

  // All KMASK RegClasses hold the same k registers, can be tested against
  // anyone.
  if (X86::VK16RegClass.contains(SrcReg)) {
    if (X86::GR64RegClass.contains(DestReg)) {
      assert(Subtarget.hasBWI());
      return HasEGPR ? X86::KMOVQrk_EVEX : X86::KMOVQrk;
    }
    if (X86::GR32RegClass.contains(DestReg))
      return Subtarget.hasBWI() ? (HasEGPR ? X86::KMOVDrk_EVEX : X86::KMOVDrk)
                                : (HasEGPR ? X86::KMOVWrk_EVEX : X86::KMOVWrk);
  }

  // SrcReg(GR64) -> DestReg(MaskReg)
  // SrcReg(GR32) -> DestReg(MaskReg)

  // All KMASK RegClasses hold the same k registers, can be tested against
  // anyone.
  if (X86::VK16RegClass.contains(DestReg)) {
    if (X86::GR64RegClass.contains(SrcReg)) {
      assert(Subtarget.hasBWI());
      return HasEGPR ? X86::KMOVQkr_EVEX : X86::KMOVQkr;
    }
    if (X86::GR32RegClass.contains(SrcReg))
      return Subtarget.hasBWI() ? (HasEGPR ? X86::KMOVDkr_EVEX : X86::KMOVDkr)
                                : (HasEGPR ? X86::KMOVWkr_EVEX : X86::KMOVWkr);
  }

  // SrcReg(VR128) -> DestReg(GR64)
  // SrcReg(VR64)  -> DestReg(GR64)
  // SrcReg(GR64)  -> DestReg(VR128)
  // SrcReg(GR64)  -> DestReg(VR64)

  if (X86::GR64RegClass.contains(DestReg)) {
    if (X86::VR128XRegClass.contains(SrcReg))
      // Copy from a VR128 register to a GR64 register.
      return HasAVX512 ? X86::VMOVPQIto64Zrr
             : HasAVX  ? X86::VMOVPQIto64rr
                       : X86::MOVPQIto64rr;
    if (X86::VR64RegClass.contains(SrcReg))
      // Copy from a VR64 register to a GR64 register.
      return X86::MMX_MOVD64from64rr;
  } else if (X86::GR64RegClass.contains(SrcReg)) {
    // Copy from a GR64 register to a VR128 register.
    if (X86::VR128XRegClass.contains(DestReg))
      return HasAVX512 ? X86::VMOV64toPQIZrr
             : HasAVX  ? X86::VMOV64toPQIrr
                       : X86::MOV64toPQIrr;
    // Copy from a GR64 register to a VR64 register.
    if (X86::VR64RegClass.contains(DestReg))
      return X86::MMX_MOVD64to64rr;
  }

  // SrcReg(VR128) -> DestReg(GR32)
  // SrcReg(GR32)  -> DestReg(VR128)

  if (X86::GR32RegClass.contains(DestReg) &&
      X86::VR128XRegClass.contains(SrcReg))
    // Copy from a VR128 register to a GR32 register.
    return HasAVX512 ? X86::VMOVPDI2DIZrr
           : HasAVX  ? X86::VMOVPDI2DIrr
                     : X86::MOVPDI2DIrr;

  if (X86::VR128XRegClass.contains(DestReg) &&
      X86::GR32RegClass.contains(SrcReg))
    // Copy from a VR128 register to a VR128 register.
    return HasAVX512 ? X86::VMOVDI2PDIZrr
           : HasAVX  ? X86::VMOVDI2PDIrr
                     : X86::MOVDI2PDIrr;
  return 0;
}

void X86InstrInfo::copyPhysReg(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MI,
                               const DebugLoc &DL, MCRegister DestReg,
                               MCRegister SrcReg, bool KillSrc) const {
  // First deal with the normal symmetric copies.
  bool HasAVX = Subtarget.hasAVX();
  bool HasVLX = Subtarget.hasVLX();
  bool HasEGPR = Subtarget.hasEGPR();
  unsigned Opc = 0;
  if (X86::GR64RegClass.contains(DestReg, SrcReg))
    Opc = X86::MOV64rr;
  else if (X86::GR32RegClass.contains(DestReg, SrcReg))
    Opc = X86::MOV32rr;
  else if (X86::GR16RegClass.contains(DestReg, SrcReg))
    Opc = X86::MOV16rr;
  else if (X86::GR8RegClass.contains(DestReg, SrcReg)) {
    // Copying to or from a physical H register on x86-64 requires a NOREX
    // move.  Otherwise use a normal move.
    if ((isHReg(DestReg) || isHReg(SrcReg)) && Subtarget.is64Bit()) {
      Opc = X86::MOV8rr_NOREX;
      // Both operands must be encodable without an REX prefix.
      assert(X86::GR8_NOREXRegClass.contains(SrcReg, DestReg) &&
             "8-bit H register can not be copied outside GR8_NOREX");
    } else
      Opc = X86::MOV8rr;
  } else if (X86::VR64RegClass.contains(DestReg, SrcReg))
    Opc = X86::MMX_MOVQ64rr;
  else if (X86::VR128XRegClass.contains(DestReg, SrcReg)) {
    if (HasVLX)
      Opc = X86::VMOVAPSZ128rr;
    else if (X86::VR128RegClass.contains(DestReg, SrcReg))
      Opc = HasAVX ? X86::VMOVAPSrr : X86::MOVAPSrr;
    else {
      // If this an extended register and we don't have VLX we need to use a
      // 512-bit move.
      Opc = X86::VMOVAPSZrr;
      const TargetRegisterInfo *TRI = &getRegisterInfo();
      DestReg =
          TRI->getMatchingSuperReg(DestReg, X86::sub_xmm, &X86::VR512RegClass);
      SrcReg =
          TRI->getMatchingSuperReg(SrcReg, X86::sub_xmm, &X86::VR512RegClass);
    }
  } else if (X86::VR256XRegClass.contains(DestReg, SrcReg)) {
    if (HasVLX)
      Opc = X86::VMOVAPSZ256rr;
    else if (X86::VR256RegClass.contains(DestReg, SrcReg))
      Opc = X86::VMOVAPSYrr;
    else {
      // If this an extended register and we don't have VLX we need to use a
      // 512-bit move.
      Opc = X86::VMOVAPSZrr;
      const TargetRegisterInfo *TRI = &getRegisterInfo();
      DestReg =
          TRI->getMatchingSuperReg(DestReg, X86::sub_ymm, &X86::VR512RegClass);
      SrcReg =
          TRI->getMatchingSuperReg(SrcReg, X86::sub_ymm, &X86::VR512RegClass);
    }
  } else if (X86::VR512RegClass.contains(DestReg, SrcReg))
    Opc = X86::VMOVAPSZrr;
  // All KMASK RegClasses hold the same k registers, can be tested against
  // anyone.
  else if (X86::VK16RegClass.contains(DestReg, SrcReg))
    Opc = Subtarget.hasBWI() ? (HasEGPR ? X86::KMOVQkk_EVEX : X86::KMOVQkk)
                             : (HasEGPR ? X86::KMOVQkk_EVEX : X86::KMOVWkk);
  if (!Opc)
    Opc = CopyToFromAsymmetricReg(DestReg, SrcReg, Subtarget);

  if (Opc) {
    BuildMI(MBB, MI, DL, get(Opc), DestReg)
        .addReg(SrcReg, getKillRegState(KillSrc));
    return;
  }

  if (SrcReg == X86::EFLAGS || DestReg == X86::EFLAGS) {
    // FIXME: We use a fatal error here because historically LLVM has tried
    // lower some of these physreg copies and we want to ensure we get
    // reasonable bug reports if someone encounters a case no other testing
    // found. This path should be removed after the LLVM 7 release.
    report_fatal_error("Unable to copy EFLAGS physical register!");
  }

  LLVM_DEBUG(dbgs() << "Cannot copy " << RI.getName(SrcReg) << " to "
                    << RI.getName(DestReg) << '\n');
  report_fatal_error("Cannot emit physreg copy instruction");
}

std::optional<DestSourcePair>
X86InstrInfo::isCopyInstrImpl(const MachineInstr &MI) const {
  if (MI.isMoveReg()) {
    // FIXME: Dirty hack for apparent invariant that doesn't hold when
    // subreg_to_reg is coalesced with ordinary copies, such that the bits that
    // were asserted as 0 are now undef.
    if (MI.getOperand(0).isUndef() && MI.getOperand(0).getSubReg())
      return std::nullopt;

    return DestSourcePair{MI.getOperand(0), MI.getOperand(1)};
  }
  return std::nullopt;
}

static unsigned getLoadStoreOpcodeForFP16(bool Load, const X86Subtarget &STI) {
  if (STI.hasFP16())
    return Load ? X86::VMOVSHZrm_alt : X86::VMOVSHZmr;
  if (Load)
    return STI.hasAVX512() ? X86::VMOVSSZrm
           : STI.hasAVX()  ? X86::VMOVSSrm
                           : X86::MOVSSrm;
  else
    return STI.hasAVX512() ? X86::VMOVSSZmr
           : STI.hasAVX()  ? X86::VMOVSSmr
                           : X86::MOVSSmr;
}

static unsigned getLoadStoreRegOpcode(Register Reg,
                                      const TargetRegisterClass *RC,
                                      bool IsStackAligned,
                                      const X86Subtarget &STI, bool Load) {
  bool HasAVX = STI.hasAVX();
  bool HasAVX512 = STI.hasAVX512();
  bool HasVLX = STI.hasVLX();
  bool HasEGPR = STI.hasEGPR();

  assert(RC != nullptr && "Invalid target register class");
  switch (STI.getRegisterInfo()->getSpillSize(*RC)) {
  default:
    llvm_unreachable("Unknown spill size");
  case 1:
    assert(X86::GR8RegClass.hasSubClassEq(RC) && "Unknown 1-byte regclass");
    if (STI.is64Bit())
      // Copying to or from a physical H register on x86-64 requires a NOREX
      // move.  Otherwise use a normal move.
      if (isHReg(Reg) || X86::GR8_ABCD_HRegClass.hasSubClassEq(RC))
        return Load ? X86::MOV8rm_NOREX : X86::MOV8mr_NOREX;
    return Load ? X86::MOV8rm : X86::MOV8mr;
  case 2:
    if (X86::VK16RegClass.hasSubClassEq(RC))
      return Load ? (HasEGPR ? X86::KMOVWkm_EVEX : X86::KMOVWkm)
                  : (HasEGPR ? X86::KMOVWmk_EVEX : X86::KMOVWmk);
    assert(X86::GR16RegClass.hasSubClassEq(RC) && "Unknown 2-byte regclass");
    return Load ? X86::MOV16rm : X86::MOV16mr;
  case 4:
    if (X86::GR32RegClass.hasSubClassEq(RC))
      return Load ? X86::MOV32rm : X86::MOV32mr;
    if (X86::FR32XRegClass.hasSubClassEq(RC))
      return Load ? (HasAVX512 ? X86::VMOVSSZrm_alt
                     : HasAVX  ? X86::VMOVSSrm_alt
                               : X86::MOVSSrm_alt)
                  : (HasAVX512 ? X86::VMOVSSZmr
                     : HasAVX  ? X86::VMOVSSmr
                               : X86::MOVSSmr);
    if (X86::RFP32RegClass.hasSubClassEq(RC))
      return Load ? X86::LD_Fp32m : X86::ST_Fp32m;
    if (X86::VK32RegClass.hasSubClassEq(RC)) {
      assert(STI.hasBWI() && "KMOVD requires BWI");
      return Load ? (HasEGPR ? X86::KMOVDkm_EVEX : X86::KMOVDkm)
                  : (HasEGPR ? X86::KMOVDmk_EVEX : X86::KMOVDmk);
    }
    // All of these mask pair classes have the same spill size, the same kind
    // of kmov instructions can be used with all of them.
    if (X86::VK1PAIRRegClass.hasSubClassEq(RC) ||
        X86::VK2PAIRRegClass.hasSubClassEq(RC) ||
        X86::VK4PAIRRegClass.hasSubClassEq(RC) ||
        X86::VK8PAIRRegClass.hasSubClassEq(RC) ||
        X86::VK16PAIRRegClass.hasSubClassEq(RC))
      return Load ? X86::MASKPAIR16LOAD : X86::MASKPAIR16STORE;
    if (X86::FR16RegClass.hasSubClassEq(RC) ||
        X86::FR16XRegClass.hasSubClassEq(RC))
      return getLoadStoreOpcodeForFP16(Load, STI);
    llvm_unreachable("Unknown 4-byte regclass");
  case 8:
    if (X86::GR64RegClass.hasSubClassEq(RC))
      return Load ? X86::MOV64rm : X86::MOV64mr;
    if (X86::FR64XRegClass.hasSubClassEq(RC))
      return Load ? (HasAVX512 ? X86::VMOVSDZrm_alt
                     : HasAVX  ? X86::VMOVSDrm_alt
                               : X86::MOVSDrm_alt)
                  : (HasAVX512 ? X86::VMOVSDZmr
                     : HasAVX  ? X86::VMOVSDmr
                               : X86::MOVSDmr);
    if (X86::VR64RegClass.hasSubClassEq(RC))
      return Load ? X86::MMX_MOVQ64rm : X86::MMX_MOVQ64mr;
    if (X86::RFP64RegClass.hasSubClassEq(RC))
      return Load ? X86::LD_Fp64m : X86::ST_Fp64m;
    if (X86::VK64RegClass.hasSubClassEq(RC)) {
      assert(STI.hasBWI() && "KMOVQ requires BWI");
      return Load ? (HasEGPR ? X86::KMOVQkm_EVEX : X86::KMOVQkm)
                  : (HasEGPR ? X86::KMOVQmk_EVEX : X86::KMOVQmk);
    }
    llvm_unreachable("Unknown 8-byte regclass");
  case 10:
    assert(X86::RFP80RegClass.hasSubClassEq(RC) && "Unknown 10-byte regclass");
    return Load ? X86::LD_Fp80m : X86::ST_FpP80m;
  case 16: {
    if (X86::VR128XRegClass.hasSubClassEq(RC)) {
      // If stack is realigned we can use aligned stores.
      if (IsStackAligned)
        return Load ? (HasVLX      ? X86::VMOVAPSZ128rm
                       : HasAVX512 ? X86::VMOVAPSZ128rm_NOVLX
                       : HasAVX    ? X86::VMOVAPSrm
                                   : X86::MOVAPSrm)
                    : (HasVLX      ? X86::VMOVAPSZ128mr
                       : HasAVX512 ? X86::VMOVAPSZ128mr_NOVLX
                       : HasAVX    ? X86::VMOVAPSmr
                                   : X86::MOVAPSmr);
      else
        return Load ? (HasVLX      ? X86::VMOVUPSZ128rm
                       : HasAVX512 ? X86::VMOVUPSZ128rm_NOVLX
                       : HasAVX    ? X86::VMOVUPSrm
                                   : X86::MOVUPSrm)
                    : (HasVLX      ? X86::VMOVUPSZ128mr
                       : HasAVX512 ? X86::VMOVUPSZ128mr_NOVLX
                       : HasAVX    ? X86::VMOVUPSmr
                                   : X86::MOVUPSmr);
    }
    llvm_unreachable("Unknown 16-byte regclass");
  }
  case 32:
    assert(X86::VR256XRegClass.hasSubClassEq(RC) && "Unknown 32-byte regclass");
    // If stack is realigned we can use aligned stores.
    if (IsStackAligned)
      return Load ? (HasVLX      ? X86::VMOVAPSZ256rm
                     : HasAVX512 ? X86::VMOVAPSZ256rm_NOVLX
                                 : X86::VMOVAPSYrm)
                  : (HasVLX      ? X86::VMOVAPSZ256mr
                     : HasAVX512 ? X86::VMOVAPSZ256mr_NOVLX
                                 : X86::VMOVAPSYmr);
    else
      return Load ? (HasVLX      ? X86::VMOVUPSZ256rm
                     : HasAVX512 ? X86::VMOVUPSZ256rm_NOVLX
                                 : X86::VMOVUPSYrm)
                  : (HasVLX      ? X86::VMOVUPSZ256mr
                     : HasAVX512 ? X86::VMOVUPSZ256mr_NOVLX
                                 : X86::VMOVUPSYmr);
  case 64:
    assert(X86::VR512RegClass.hasSubClassEq(RC) && "Unknown 64-byte regclass");
    assert(STI.hasAVX512() && "Using 512-bit register requires AVX512");
    if (IsStackAligned)
      return Load ? X86::VMOVAPSZrm : X86::VMOVAPSZmr;
    else
      return Load ? X86::VMOVUPSZrm : X86::VMOVUPSZmr;
  case 1024:
    assert(X86::TILERegClass.hasSubClassEq(RC) && "Unknown 1024-byte regclass");
    assert(STI.hasAMXTILE() && "Using 8*1024-bit register requires AMX-TILE");
#define GET_EGPR_IF_ENABLED(OPC) (STI.hasEGPR() ? OPC##_EVEX : OPC)
    return Load ? GET_EGPR_IF_ENABLED(X86::TILELOADD)
                : GET_EGPR_IF_ENABLED(X86::TILESTORED);
#undef GET_EGPR_IF_ENABLED
  }
}

std::optional<ExtAddrMode>
X86InstrInfo::getAddrModeFromMemoryOp(const MachineInstr &MemI,
                                      const TargetRegisterInfo *TRI) const {
  const MCInstrDesc &Desc = MemI.getDesc();
  int MemRefBegin = X86II::getMemoryOperandNo(Desc.TSFlags);
  if (MemRefBegin < 0)
    return std::nullopt;

  MemRefBegin += X86II::getOperandBias(Desc);

  auto &BaseOp = MemI.getOperand(MemRefBegin + X86::AddrBaseReg);
  if (!BaseOp.isReg()) // Can be an MO_FrameIndex
    return std::nullopt;

  const MachineOperand &DispMO = MemI.getOperand(MemRefBegin + X86::AddrDisp);
  // Displacement can be symbolic
  if (!DispMO.isImm())
    return std::nullopt;

  ExtAddrMode AM;
  AM.BaseReg = BaseOp.getReg();
  AM.ScaledReg = MemI.getOperand(MemRefBegin + X86::AddrIndexReg).getReg();
  AM.Scale = MemI.getOperand(MemRefBegin + X86::AddrScaleAmt).getImm();
  AM.Displacement = DispMO.getImm();
  return AM;
}

bool X86InstrInfo::verifyInstruction(const MachineInstr &MI,
                                     StringRef &ErrInfo) const {
  std::optional<ExtAddrMode> AMOrNone = getAddrModeFromMemoryOp(MI, nullptr);
  if (!AMOrNone)
    return true;

  ExtAddrMode AM = *AMOrNone;
  assert(AM.Form == ExtAddrMode::Formula::Basic);
  if (AM.ScaledReg != X86::NoRegister) {
    switch (AM.Scale) {
    case 1:
    case 2:
    case 4:
    case 8:
      break;
    default:
      ErrInfo = "Scale factor in address must be 1, 2, 4 or 8";
      return false;
    }
  }
  if (!isInt<32>(AM.Displacement)) {
    ErrInfo = "Displacement in address must fit into 32-bit signed "
              "integer";
    return false;
  }

  return true;
}

bool X86InstrInfo::getConstValDefinedInReg(const MachineInstr &MI,
                                           const Register Reg,
                                           int64_t &ImmVal) const {
  Register MovReg = Reg;
  const MachineInstr *MovMI = &MI;

  // Follow use-def for SUBREG_TO_REG to find the real move immediate
  // instruction. It is quite common for x86-64.
  if (MI.isSubregToReg()) {
    // We use following pattern to setup 64b immediate.
    //      %8:gr32 = MOV32r0 implicit-def dead $eflags
    //      %6:gr64 = SUBREG_TO_REG 0, killed %8:gr32, %subreg.sub_32bit
    if (!MI.getOperand(1).isImm())
      return false;
    unsigned FillBits = MI.getOperand(1).getImm();
    unsigned SubIdx = MI.getOperand(3).getImm();
    MovReg = MI.getOperand(2).getReg();
    if (SubIdx != X86::sub_32bit || FillBits != 0)
      return false;
    const MachineRegisterInfo &MRI = MI.getParent()->getParent()->getRegInfo();
    MovMI = MRI.getUniqueVRegDef(MovReg);
    if (!MovMI)
      return false;
  }

  if (MovMI->getOpcode() == X86::MOV32r0 &&
      MovMI->getOperand(0).getReg() == MovReg) {
    ImmVal = 0;
    return true;
  }

  if (MovMI->getOpcode() != X86::MOV32ri &&
      MovMI->getOpcode() != X86::MOV64ri &&
      MovMI->getOpcode() != X86::MOV32ri64 && MovMI->getOpcode() != X86::MOV8ri)
    return false;
  // Mov Src can be a global address.
  if (!MovMI->getOperand(1).isImm() || MovMI->getOperand(0).getReg() != MovReg)
    return false;
  ImmVal = MovMI->getOperand(1).getImm();
  return true;
}

bool X86InstrInfo::preservesZeroValueInReg(
    const MachineInstr *MI, const Register NullValueReg,
    const TargetRegisterInfo *TRI) const {
  if (!MI->modifiesRegister(NullValueReg, TRI))
    return true;
  switch (MI->getOpcode()) {
  // Shift right/left of a null unto itself is still a null, i.e. rax = shl rax
  // X.
  case X86::SHR64ri:
  case X86::SHR32ri:
  case X86::SHL64ri:
  case X86::SHL32ri:
    assert(MI->getOperand(0).isDef() && MI->getOperand(1).isUse() &&
           "expected for shift opcode!");
    return MI->getOperand(0).getReg() == NullValueReg &&
           MI->getOperand(1).getReg() == NullValueReg;
  // Zero extend of a sub-reg of NullValueReg into itself does not change the
  // null value.
  case X86::MOV32rr:
    return llvm::all_of(MI->operands(), [&](const MachineOperand &MO) {
      return TRI->isSubRegisterEq(NullValueReg, MO.getReg());
    });
  default:
    return false;
  }
  llvm_unreachable("Should be handled above!");
}

bool X86InstrInfo::getMemOperandsWithOffsetWidth(
    const MachineInstr &MemOp, SmallVectorImpl<const MachineOperand *> &BaseOps,
    int64_t &Offset, bool &OffsetIsScalable, LocationSize &Width,
    const TargetRegisterInfo *TRI) const {
  const MCInstrDesc &Desc = MemOp.getDesc();
  int MemRefBegin = X86II::getMemoryOperandNo(Desc.TSFlags);
  if (MemRefBegin < 0)
    return false;

  MemRefBegin += X86II::getOperandBias(Desc);

  const MachineOperand *BaseOp =
      &MemOp.getOperand(MemRefBegin + X86::AddrBaseReg);
  if (!BaseOp->isReg()) // Can be an MO_FrameIndex
    return false;

  if (MemOp.getOperand(MemRefBegin + X86::AddrScaleAmt).getImm() != 1)
    return false;

  if (MemOp.getOperand(MemRefBegin + X86::AddrIndexReg).getReg() !=
      X86::NoRegister)
    return false;

  const MachineOperand &DispMO = MemOp.getOperand(MemRefBegin + X86::AddrDisp);

  // Displacement can be symbolic
  if (!DispMO.isImm())
    return false;

  Offset = DispMO.getImm();

  if (!BaseOp->isReg())
    return false;

  OffsetIsScalable = false;
  // FIXME: Relying on memoperands() may not be right thing to do here. Check
  // with X86 maintainers, and fix it accordingly. For now, it is ok, since
  // there is no use of `Width` for X86 back-end at the moment.
  Width =
      !MemOp.memoperands_empty() ? MemOp.memoperands().front()->getSize() : 0;
  BaseOps.push_back(BaseOp);
  return true;
}

static unsigned getStoreRegOpcode(Register SrcReg,
                                  const TargetRegisterClass *RC,
                                  bool IsStackAligned,
                                  const X86Subtarget &STI) {
  return getLoadStoreRegOpcode(SrcReg, RC, IsStackAligned, STI, false);
}

static unsigned getLoadRegOpcode(Register DestReg,
                                 const TargetRegisterClass *RC,
                                 bool IsStackAligned, const X86Subtarget &STI) {
  return getLoadStoreRegOpcode(DestReg, RC, IsStackAligned, STI, true);
}

static bool isAMXOpcode(unsigned Opc) {
  switch (Opc) {
  default:
    return false;
  case X86::TILELOADD:
  case X86::TILESTORED:
  case X86::TILELOADD_EVEX:
  case X86::TILESTORED_EVEX:
    return true;
  }
}

void X86InstrInfo::loadStoreTileReg(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator MI,
                                    unsigned Opc, Register Reg, int FrameIdx,
                                    bool isKill) const {
  switch (Opc) {
  default:
    llvm_unreachable("Unexpected special opcode!");
  case X86::TILESTORED:
  case X86::TILESTORED_EVEX: {
    // tilestored %tmm, (%sp, %idx)
    MachineRegisterInfo &RegInfo = MBB.getParent()->getRegInfo();
    Register VirtReg = RegInfo.createVirtualRegister(&X86::GR64_NOSPRegClass);
    BuildMI(MBB, MI, DebugLoc(), get(X86::MOV64ri), VirtReg).addImm(64);
    MachineInstr *NewMI =
        addFrameReference(BuildMI(MBB, MI, DebugLoc(), get(Opc)), FrameIdx)
            .addReg(Reg, getKillRegState(isKill));
    MachineOperand &MO = NewMI->getOperand(X86::AddrIndexReg);
    MO.setReg(VirtReg);
    MO.setIsKill(true);
    break;
  }
  case X86::TILELOADD:
  case X86::TILELOADD_EVEX: {
    // tileloadd (%sp, %idx), %tmm
    MachineRegisterInfo &RegInfo = MBB.getParent()->getRegInfo();
    Register VirtReg = RegInfo.createVirtualRegister(&X86::GR64_NOSPRegClass);
    BuildMI(MBB, MI, DebugLoc(), get(X86::MOV64ri), VirtReg).addImm(64);
    MachineInstr *NewMI = addFrameReference(
        BuildMI(MBB, MI, DebugLoc(), get(Opc), Reg), FrameIdx);
    MachineOperand &MO = NewMI->getOperand(1 + X86::AddrIndexReg);
    MO.setReg(VirtReg);
    MO.setIsKill(true);
    break;
  }
  }
}

void X86InstrInfo::storeRegToStackSlot(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, Register SrcReg,
    bool isKill, int FrameIdx, const TargetRegisterClass *RC,
    const TargetRegisterInfo *TRI, Register VReg) const {
  const MachineFunction &MF = *MBB.getParent();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  assert(MFI.getObjectSize(FrameIdx) >= TRI->getSpillSize(*RC) &&
         "Stack slot too small for store");

  unsigned Alignment = std::max<uint32_t>(TRI->getSpillSize(*RC), 16);
  bool isAligned =
      (Subtarget.getFrameLowering()->getStackAlign() >= Alignment) ||
      (RI.canRealignStack(MF) && !MFI.isFixedObjectIndex(FrameIdx));

  unsigned Opc = getStoreRegOpcode(SrcReg, RC, isAligned, Subtarget);
  if (isAMXOpcode(Opc))
    loadStoreTileReg(MBB, MI, Opc, SrcReg, FrameIdx, isKill);
  else
    addFrameReference(BuildMI(MBB, MI, DebugLoc(), get(Opc)), FrameIdx)
        .addReg(SrcReg, getKillRegState(isKill));
}

void X86InstrInfo::loadRegFromStackSlot(MachineBasicBlock &MBB,
                                        MachineBasicBlock::iterator MI,
                                        Register DestReg, int FrameIdx,
                                        const TargetRegisterClass *RC,
                                        const TargetRegisterInfo *TRI,
                                        Register VReg) const {
  const MachineFunction &MF = *MBB.getParent();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  assert(MFI.getObjectSize(FrameIdx) >= TRI->getSpillSize(*RC) &&
         "Load size exceeds stack slot");
  unsigned Alignment = std::max<uint32_t>(TRI->getSpillSize(*RC), 16);
  bool isAligned =
      (Subtarget.getFrameLowering()->getStackAlign() >= Alignment) ||
      (RI.canRealignStack(MF) && !MFI.isFixedObjectIndex(FrameIdx));

  unsigned Opc = getLoadRegOpcode(DestReg, RC, isAligned, Subtarget);
  if (isAMXOpcode(Opc))
    loadStoreTileReg(MBB, MI, Opc, DestReg, FrameIdx);
  else
    addFrameReference(BuildMI(MBB, MI, DebugLoc(), get(Opc), DestReg),
                      FrameIdx);
}

bool X86InstrInfo::analyzeCompare(const MachineInstr &MI, Register &SrcReg,
                                  Register &SrcReg2, int64_t &CmpMask,
                                  int64_t &CmpValue) const {
  switch (MI.getOpcode()) {
  default:
    break;
  case X86::CMP64ri32:
  case X86::CMP32ri:
  case X86::CMP16ri:
  case X86::CMP8ri:
    SrcReg = MI.getOperand(0).getReg();
    SrcReg2 = 0;
    if (MI.getOperand(1).isImm()) {
      CmpMask = ~0;
      CmpValue = MI.getOperand(1).getImm();
    } else {
      CmpMask = CmpValue = 0;
    }
    return true;
  // A SUB can be used to perform comparison.
  CASE_ND(SUB64rm)
  CASE_ND(SUB32rm)
  CASE_ND(SUB16rm)
  CASE_ND(SUB8rm)
    SrcReg = MI.getOperand(1).getReg();
    SrcReg2 = 0;
    CmpMask = 0;
    CmpValue = 0;
    return true;
  CASE_ND(SUB64rr)
  CASE_ND(SUB32rr)
  CASE_ND(SUB16rr)
  CASE_ND(SUB8rr)
    SrcReg = MI.getOperand(1).getReg();
    SrcReg2 = MI.getOperand(2).getReg();
    CmpMask = 0;
    CmpValue = 0;
    return true;
  CASE_ND(SUB64ri32)
  CASE_ND(SUB32ri)
  CASE_ND(SUB16ri)
  CASE_ND(SUB8ri)
    SrcReg = MI.getOperand(1).getReg();
    SrcReg2 = 0;
    if (MI.getOperand(2).isImm()) {
      CmpMask = ~0;
      CmpValue = MI.getOperand(2).getImm();
    } else {
      CmpMask = CmpValue = 0;
    }
    return true;
  case X86::CMP64rr:
  case X86::CMP32rr:
  case X86::CMP16rr:
  case X86::CMP8rr:
    SrcReg = MI.getOperand(0).getReg();
    SrcReg2 = MI.getOperand(1).getReg();
    CmpMask = 0;
    CmpValue = 0;
    return true;
  case X86::TEST8rr:
  case X86::TEST16rr:
  case X86::TEST32rr:
  case X86::TEST64rr:
    SrcReg = MI.getOperand(0).getReg();
    if (MI.getOperand(1).getReg() != SrcReg)
      return false;
    // Compare against zero.
    SrcReg2 = 0;
    CmpMask = ~0;
    CmpValue = 0;
    return true;
  }
  return false;
}

bool X86InstrInfo::isRedundantFlagInstr(const MachineInstr &FlagI,
                                        Register SrcReg, Register SrcReg2,
                                        int64_t ImmMask, int64_t ImmValue,
                                        const MachineInstr &OI, bool *IsSwapped,
                                        int64_t *ImmDelta) const {
  switch (OI.getOpcode()) {
  case X86::CMP64rr:
  case X86::CMP32rr:
  case X86::CMP16rr:
  case X86::CMP8rr:
  CASE_ND(SUB64rr)
  CASE_ND(SUB32rr)
  CASE_ND(SUB16rr)
  CASE_ND(SUB8rr) {
    Register OISrcReg;
    Register OISrcReg2;
    int64_t OIMask;
    int64_t OIValue;
    if (!analyzeCompare(OI, OISrcReg, OISrcReg2, OIMask, OIValue) ||
        OIMask != ImmMask || OIValue != ImmValue)
      return false;
    if (SrcReg == OISrcReg && SrcReg2 == OISrcReg2) {
      *IsSwapped = false;
      return true;
    }
    if (SrcReg == OISrcReg2 && SrcReg2 == OISrcReg) {
      *IsSwapped = true;
      return true;
    }
    return false;
  }
  case X86::CMP64ri32:
  case X86::CMP32ri:
  case X86::CMP16ri:
  case X86::CMP8ri:
  CASE_ND(SUB64ri32)
  CASE_ND(SUB32ri)
  CASE_ND(SUB16ri)
  CASE_ND(SUB8ri)
  case X86::TEST64rr:
  case X86::TEST32rr:
  case X86::TEST16rr:
  case X86::TEST8rr: {
    if (ImmMask != 0) {
      Register OISrcReg;
      Register OISrcReg2;
      int64_t OIMask;
      int64_t OIValue;
      if (analyzeCompare(OI, OISrcReg, OISrcReg2, OIMask, OIValue) &&
          SrcReg == OISrcReg && ImmMask == OIMask) {
        if (OIValue == ImmValue) {
          *ImmDelta = 0;
          return true;
        } else if (static_cast<uint64_t>(ImmValue) ==
                   static_cast<uint64_t>(OIValue) - 1) {
          *ImmDelta = -1;
          return true;
        } else if (static_cast<uint64_t>(ImmValue) ==
                   static_cast<uint64_t>(OIValue) + 1) {
          *ImmDelta = 1;
          return true;
        } else {
          return false;
        }
      }
    }
    return FlagI.isIdenticalTo(OI);
  }
  default:
    return false;
  }
}

/// Check whether the definition can be converted
/// to remove a comparison against zero.
inline static bool isDefConvertible(const MachineInstr &MI, bool &NoSignFlag,
                                    bool &ClearsOverflowFlag) {
  NoSignFlag = false;
  ClearsOverflowFlag = false;

  // "ELF Handling for Thread-Local Storage" specifies that x86-64 GOTTPOFF, and
  // i386 GOTNTPOFF/INDNTPOFF relocations can convert an ADD to a LEA during
  // Initial Exec to Local Exec relaxation. In these cases, we must not depend
  // on the EFLAGS modification of ADD actually happening in the final binary.
  if (MI.getOpcode() == X86::ADD64rm || MI.getOpcode() == X86::ADD32rm) {
    unsigned Flags = MI.getOperand(5).getTargetFlags();
    if (Flags == X86II::MO_GOTTPOFF || Flags == X86II::MO_INDNTPOFF ||
        Flags == X86II::MO_GOTNTPOFF)
      return false;
  }

  switch (MI.getOpcode()) {
  default:
    return false;

  // The shift instructions only modify ZF if their shift count is non-zero.
  // N.B.: The processor truncates the shift count depending on the encoding.
  CASE_ND(SAR8ri)
  CASE_ND(SAR16ri)
  CASE_ND(SAR32ri)
  CASE_ND(SAR64ri)
  CASE_ND(SHR8ri)
  CASE_ND(SHR16ri)
  CASE_ND(SHR32ri)
  CASE_ND(SHR64ri)
    return getTruncatedShiftCount(MI, 2) != 0;

  // Some left shift instructions can be turned into LEA instructions but only
  // if their flags aren't used. Avoid transforming such instructions.
  CASE_ND(SHL8ri)
  CASE_ND(SHL16ri)
  CASE_ND(SHL32ri)
  CASE_ND(SHL64ri) {
    unsigned ShAmt = getTruncatedShiftCount(MI, 2);
    if (isTruncatedShiftCountForLEA(ShAmt))
      return false;
    return ShAmt != 0;
  }

  CASE_ND(SHRD16rri8)
  CASE_ND(SHRD32rri8)
  CASE_ND(SHRD64rri8)
  CASE_ND(SHLD16rri8)
  CASE_ND(SHLD32rri8)
  CASE_ND(SHLD64rri8)
    return getTruncatedShiftCount(MI, 3) != 0;

  CASE_ND(SUB64ri32)
  CASE_ND(SUB32ri)
  CASE_ND(SUB16ri)
  CASE_ND(SUB8ri)
  CASE_ND(SUB64rr)
  CASE_ND(SUB32rr)
  CASE_ND(SUB16rr)
  CASE_ND(SUB8rr)
  CASE_ND(SUB64rm)
  CASE_ND(SUB32rm)
  CASE_ND(SUB16rm)
  CASE_ND(SUB8rm)
  CASE_ND(DEC64r)
  CASE_ND(DEC32r)
  CASE_ND(DEC16r)
  CASE_ND(DEC8r)
  CASE_ND(ADD64ri32)
  CASE_ND(ADD32ri)
  CASE_ND(ADD16ri)
  CASE_ND(ADD8ri)
  CASE_ND(ADD64rr)
  CASE_ND(ADD32rr)
  CASE_ND(ADD16rr)
  CASE_ND(ADD8rr)
  CASE_ND(ADD64rm)
  CASE_ND(ADD32rm)
  CASE_ND(ADD16rm)
  CASE_ND(ADD8rm)
  CASE_ND(INC64r)
  CASE_ND(INC32r)
  CASE_ND(INC16r)
  CASE_ND(INC8r)
  CASE_ND(ADC64ri32)
  CASE_ND(ADC32ri)
  CASE_ND(ADC16ri)
  CASE_ND(ADC8ri)
  CASE_ND(ADC64rr)
  CASE_ND(ADC32rr)
  CASE_ND(ADC16rr)
  CASE_ND(ADC8rr)
  CASE_ND(ADC64rm)
  CASE_ND(ADC32rm)
  CASE_ND(ADC16rm)
  CASE_ND(ADC8rm)
  CASE_ND(SBB64ri32)
  CASE_ND(SBB32ri)
  CASE_ND(SBB16ri)
  CASE_ND(SBB8ri)
  CASE_ND(SBB64rr)
  CASE_ND(SBB32rr)
  CASE_ND(SBB16rr)
  CASE_ND(SBB8rr)
  CASE_ND(SBB64rm)
  CASE_ND(SBB32rm)
  CASE_ND(SBB16rm)
  CASE_ND(SBB8rm)
  CASE_ND(NEG8r)
  CASE_ND(NEG16r)
  CASE_ND(NEG32r)
  CASE_ND(NEG64r)
  case X86::LZCNT16rr:
  case X86::LZCNT16rm:
  case X86::LZCNT32rr:
  case X86::LZCNT32rm:
  case X86::LZCNT64rr:
  case X86::LZCNT64rm:
  case X86::POPCNT16rr:
  case X86::POPCNT16rm:
  case X86::POPCNT32rr:
  case X86::POPCNT32rm:
  case X86::POPCNT64rr:
  case X86::POPCNT64rm:
  case X86::TZCNT16rr:
  case X86::TZCNT16rm:
  case X86::TZCNT32rr:
  case X86::TZCNT32rm:
  case X86::TZCNT64rr:
  case X86::TZCNT64rm:
    return true;
  CASE_ND(AND64ri32)
  CASE_ND(AND32ri)
  CASE_ND(AND16ri)
  CASE_ND(AND8ri)
  CASE_ND(AND64rr)
  CASE_ND(AND32rr)
  CASE_ND(AND16rr)
  CASE_ND(AND8rr)
  CASE_ND(AND64rm)
  CASE_ND(AND32rm)
  CASE_ND(AND16rm)
  CASE_ND(AND8rm)
  CASE_ND(XOR64ri32)
  CASE_ND(XOR32ri)
  CASE_ND(XOR16ri)
  CASE_ND(XOR8ri)
  CASE_ND(XOR64rr)
  CASE_ND(XOR32rr)
  CASE_ND(XOR16rr)
  CASE_ND(XOR8rr)
  CASE_ND(XOR64rm)
  CASE_ND(XOR32rm)
  CASE_ND(XOR16rm)
  CASE_ND(XOR8rm)
  CASE_ND(OR64ri32)
  CASE_ND(OR32ri)
  CASE_ND(OR16ri)
  CASE_ND(OR8ri)
  CASE_ND(OR64rr)
  CASE_ND(OR32rr)
  CASE_ND(OR16rr)
  CASE_ND(OR8rr)
  CASE_ND(OR64rm)
  CASE_ND(OR32rm)
  CASE_ND(OR16rm)
  CASE_ND(OR8rm)
  case X86::ANDN32rr:
  case X86::ANDN32rm:
  case X86::ANDN64rr:
  case X86::ANDN64rm:
  case X86::BLSI32rr:
  case X86::BLSI32rm:
  case X86::BLSI64rr:
  case X86::BLSI64rm:
  case X86::BLSMSK32rr:
  case X86::BLSMSK32rm:
  case X86::BLSMSK64rr:
  case X86::BLSMSK64rm:
  case X86::BLSR32rr:
  case X86::BLSR32rm:
  case X86::BLSR64rr:
  case X86::BLSR64rm:
  case X86::BLCFILL32rr:
  case X86::BLCFILL32rm:
  case X86::BLCFILL64rr:
  case X86::BLCFILL64rm:
  case X86::BLCI32rr:
  case X86::BLCI32rm:
  case X86::BLCI64rr:
  case X86::BLCI64rm:
  case X86::BLCIC32rr:
  case X86::BLCIC32rm:
  case X86::BLCIC64rr:
  case X86::BLCIC64rm:
  case X86::BLCMSK32rr:
  case X86::BLCMSK32rm:
  case X86::BLCMSK64rr:
  case X86::BLCMSK64rm:
  case X86::BLCS32rr:
  case X86::BLCS32rm:
  case X86::BLCS64rr:
  case X86::BLCS64rm:
  case X86::BLSFILL32rr:
  case X86::BLSFILL32rm:
  case X86::BLSFILL64rr:
  case X86::BLSFILL64rm:
  case X86::BLSIC32rr:
  case X86::BLSIC32rm:
  case X86::BLSIC64rr:
  case X86::BLSIC64rm:
  case X86::BZHI32rr:
  case X86::BZHI32rm:
  case X86::BZHI64rr:
  case X86::BZHI64rm:
  case X86::T1MSKC32rr:
  case X86::T1MSKC32rm:
  case X86::T1MSKC64rr:
  case X86::T1MSKC64rm:
  case X86::TZMSK32rr:
  case X86::TZMSK32rm:
  case X86::TZMSK64rr:
  case X86::TZMSK64rm:
    // These instructions clear the overflow flag just like TEST.
    // FIXME: These are not the only instructions in this switch that clear the
    // overflow flag.
    ClearsOverflowFlag = true;
    return true;
  case X86::BEXTR32rr:
  case X86::BEXTR64rr:
  case X86::BEXTR32rm:
  case X86::BEXTR64rm:
  case X86::BEXTRI32ri:
  case X86::BEXTRI32mi:
  case X86::BEXTRI64ri:
  case X86::BEXTRI64mi:
    // BEXTR doesn't update the sign flag so we can't use it. It does clear
    // the overflow flag, but that's not useful without the sign flag.
    NoSignFlag = true;
    return true;
  }
}

/// Check whether the use can be converted to remove a comparison against zero.
static X86::CondCode isUseDefConvertible(const MachineInstr &MI) {
  switch (MI.getOpcode()) {
  default:
    return X86::COND_INVALID;
  CASE_ND(NEG8r)
  CASE_ND(NEG16r)
  CASE_ND(NEG32r)
  CASE_ND(NEG64r)
    return X86::COND_AE;
  case X86::LZCNT16rr:
  case X86::LZCNT32rr:
  case X86::LZCNT64rr:
    return X86::COND_B;
  case X86::POPCNT16rr:
  case X86::POPCNT32rr:
  case X86::POPCNT64rr:
    return X86::COND_E;
  case X86::TZCNT16rr:
  case X86::TZCNT32rr:
  case X86::TZCNT64rr:
    return X86::COND_B;
  case X86::BSF16rr:
  case X86::BSF32rr:
  case X86::BSF64rr:
  case X86::BSR16rr:
  case X86::BSR32rr:
  case X86::BSR64rr:
    return X86::COND_E;
  case X86::BLSI32rr:
  case X86::BLSI64rr:
    return X86::COND_AE;
  case X86::BLSR32rr:
  case X86::BLSR64rr:
  case X86::BLSMSK32rr:
  case X86::BLSMSK64rr:
    return X86::COND_B;
    // TODO: TBM instructions.
  }
}

/// Check if there exists an earlier instruction that
/// operates on the same source operands and sets flags in the same way as
/// Compare; remove Compare if possible.
bool X86InstrInfo::optimizeCompareInstr(MachineInstr &CmpInstr, Register SrcReg,
                                        Register SrcReg2, int64_t CmpMask,
                                        int64_t CmpValue,
                                        const MachineRegisterInfo *MRI) const {
  // Check whether we can replace SUB with CMP.
  switch (CmpInstr.getOpcode()) {
  default:
    break;
  CASE_ND(SUB64ri32)
  CASE_ND(SUB32ri)
  CASE_ND(SUB16ri)
  CASE_ND(SUB8ri)
  CASE_ND(SUB64rm)
  CASE_ND(SUB32rm)
  CASE_ND(SUB16rm)
  CASE_ND(SUB8rm)
  CASE_ND(SUB64rr)
  CASE_ND(SUB32rr)
  CASE_ND(SUB16rr)
  CASE_ND(SUB8rr) {
    if (!MRI->use_nodbg_empty(CmpInstr.getOperand(0).getReg()))
      return false;
    // There is no use of the destination register, we can replace SUB with CMP.
    unsigned NewOpcode = 0;
#define FROM_TO(A, B)                                                          \
  CASE_ND(A) NewOpcode = X86::B;                                               \
  break;
    switch (CmpInstr.getOpcode()) {
    default:
      llvm_unreachable("Unreachable!");
    FROM_TO(SUB64rm, CMP64rm)
    FROM_TO(SUB32rm, CMP32rm)
    FROM_TO(SUB16rm, CMP16rm)
    FROM_TO(SUB8rm, CMP8rm)
    FROM_TO(SUB64rr, CMP64rr)
    FROM_TO(SUB32rr, CMP32rr)
    FROM_TO(SUB16rr, CMP16rr)
    FROM_TO(SUB8rr, CMP8rr)
    FROM_TO(SUB64ri32, CMP64ri32)
    FROM_TO(SUB32ri, CMP32ri)
    FROM_TO(SUB16ri, CMP16ri)
    FROM_TO(SUB8ri, CMP8ri)
    }
#undef FROM_TO
    CmpInstr.setDesc(get(NewOpcode));
    CmpInstr.removeOperand(0);
    // Mutating this instruction invalidates any debug data associated with it.
    CmpInstr.dropDebugNumber();
    // Fall through to optimize Cmp if Cmp is CMPrr or CMPri.
    if (NewOpcode == X86::CMP64rm || NewOpcode == X86::CMP32rm ||
        NewOpcode == X86::CMP16rm || NewOpcode == X86::CMP8rm)
      return false;
  }
  }

  // The following code tries to remove the comparison by re-using EFLAGS
  // from earlier instructions.

  bool IsCmpZero = (CmpMask != 0 && CmpValue == 0);

  // Transformation currently requires SSA values.
  if (SrcReg2.isPhysical())
    return false;
  MachineInstr *SrcRegDef = MRI->getVRegDef(SrcReg);
  assert(SrcRegDef && "Must have a definition (SSA)");

  MachineInstr *MI = nullptr;
  MachineInstr *Sub = nullptr;
  MachineInstr *Movr0Inst = nullptr;
  bool NoSignFlag = false;
  bool ClearsOverflowFlag = false;
  bool ShouldUpdateCC = false;
  bool IsSwapped = false;
  X86::CondCode NewCC = X86::COND_INVALID;
  int64_t ImmDelta = 0;

  // Search backward from CmpInstr for the next instruction defining EFLAGS.
  const TargetRegisterInfo *TRI = &getRegisterInfo();
  MachineBasicBlock &CmpMBB = *CmpInstr.getParent();
  MachineBasicBlock::reverse_iterator From =
      std::next(MachineBasicBlock::reverse_iterator(CmpInstr));
  for (MachineBasicBlock *MBB = &CmpMBB;;) {
    for (MachineInstr &Inst : make_range(From, MBB->rend())) {
      // Try to use EFLAGS from the instruction defining %SrcReg. Example:
      //     %eax = addl ...
      //     ...                // EFLAGS not changed
      //     testl %eax, %eax   // <-- can be removed
      if (&Inst == SrcRegDef) {
        if (IsCmpZero &&
            isDefConvertible(Inst, NoSignFlag, ClearsOverflowFlag)) {
          MI = &Inst;
          break;
        }

        // Look back for the following pattern, in which case the
        // test16rr/test64rr instruction could be erased.
        //
        // Example for test16rr:
        //  %reg = and32ri %in_reg, 5
        //  ...                         // EFLAGS not changed.
        //  %src_reg = copy %reg.sub_16bit:gr32
        //  test16rr %src_reg, %src_reg, implicit-def $eflags
        // Example for test64rr:
        //  %reg = and32ri %in_reg, 5
        //  ...                         // EFLAGS not changed.
        //  %src_reg = subreg_to_reg 0, %reg, %subreg.sub_index
        //  test64rr %src_reg, %src_reg, implicit-def $eflags
        MachineInstr *AndInstr = nullptr;
        if (IsCmpZero &&
            findRedundantFlagInstr(CmpInstr, Inst, MRI, &AndInstr, TRI,
                                   NoSignFlag, ClearsOverflowFlag)) {
          assert(AndInstr != nullptr && X86::isAND(AndInstr->getOpcode()));
          MI = AndInstr;
          break;
        }
        // Cannot find other candidates before definition of SrcReg.
        return false;
      }

      if (Inst.modifiesRegister(X86::EFLAGS, TRI)) {
        // Try to use EFLAGS produced by an instruction reading %SrcReg.
        // Example:
        //      %eax = ...
        //      ...
        //      popcntl %eax
        //      ...                 // EFLAGS not changed
        //      testl %eax, %eax    // <-- can be removed
        if (IsCmpZero) {
          NewCC = isUseDefConvertible(Inst);
          if (NewCC != X86::COND_INVALID && Inst.getOperand(1).isReg() &&
              Inst.getOperand(1).getReg() == SrcReg) {
            ShouldUpdateCC = true;
            MI = &Inst;
            break;
          }
        }

        // Try to use EFLAGS from an instruction with similar flag results.
        // Example:
        //     sub x, y  or  cmp x, y
        //     ...           // EFLAGS not changed
        //     cmp x, y      // <-- can be removed
        if (isRedundantFlagInstr(CmpInstr, SrcReg, SrcReg2, CmpMask, CmpValue,
                                 Inst, &IsSwapped, &ImmDelta)) {
          Sub = &Inst;
          break;
        }

        // MOV32r0 is implemented with xor which clobbers condition code. It is
        // safe to move up, if the definition to EFLAGS is dead and earlier
        // instructions do not read or write EFLAGS.
        if (!Movr0Inst && Inst.getOpcode() == X86::MOV32r0 &&
            Inst.registerDefIsDead(X86::EFLAGS, TRI)) {
          Movr0Inst = &Inst;
          continue;
        }

        // Cannot do anything for any other EFLAG changes.
        return false;
      }
    }

    if (MI || Sub)
      break;

    // Reached begin of basic block. Continue in predecessor if there is
    // exactly one.
    if (MBB->pred_size() != 1)
      return false;
    MBB = *MBB->pred_begin();
    From = MBB->rbegin();
  }

  // Scan forward from the instruction after CmpInstr for uses of EFLAGS.
  // It is safe to remove CmpInstr if EFLAGS is redefined or killed.
  // If we are done with the basic block, we need to check whether EFLAGS is
  // live-out.
  bool FlagsMayLiveOut = true;
  SmallVector<std::pair<MachineInstr *, X86::CondCode>, 4> OpsToUpdate;
  MachineBasicBlock::iterator AfterCmpInstr =
      std::next(MachineBasicBlock::iterator(CmpInstr));
  for (MachineInstr &Instr : make_range(AfterCmpInstr, CmpMBB.end())) {
    bool ModifyEFLAGS = Instr.modifiesRegister(X86::EFLAGS, TRI);
    bool UseEFLAGS = Instr.readsRegister(X86::EFLAGS, TRI);
    // We should check the usage if this instruction uses and updates EFLAGS.
    if (!UseEFLAGS && ModifyEFLAGS) {
      // It is safe to remove CmpInstr if EFLAGS is updated again.
      FlagsMayLiveOut = false;
      break;
    }
    if (!UseEFLAGS && !ModifyEFLAGS)
      continue;

    // EFLAGS is used by this instruction.
    X86::CondCode OldCC = X86::getCondFromMI(Instr);
    if ((MI || IsSwapped || ImmDelta != 0) && OldCC == X86::COND_INVALID)
      return false;

    X86::CondCode ReplacementCC = X86::COND_INVALID;
    if (MI) {
      switch (OldCC) {
      default:
        break;
      case X86::COND_A:
      case X86::COND_AE:
      case X86::COND_B:
      case X86::COND_BE:
        // CF is used, we can't perform this optimization.
        return false;
      case X86::COND_G:
      case X86::COND_GE:
      case X86::COND_L:
      case X86::COND_LE:
        // If SF is used, but the instruction doesn't update the SF, then we
        // can't do the optimization.
        if (NoSignFlag)
          return false;
        [[fallthrough]];
      case X86::COND_O:
      case X86::COND_NO:
        // If OF is used, the instruction needs to clear it like CmpZero does.
        if (!ClearsOverflowFlag)
          return false;
        break;
      case X86::COND_S:
      case X86::COND_NS:
        // If SF is used, but the instruction doesn't update the SF, then we
        // can't do the optimization.
        if (NoSignFlag)
          return false;
        break;
      }

      // If we're updating the condition code check if we have to reverse the
      // condition.
      if (ShouldUpdateCC)
        switch (OldCC) {
        default:
          return false;
        case X86::COND_E:
          ReplacementCC = NewCC;
          break;
        case X86::COND_NE:
          ReplacementCC = GetOppositeBranchCondition(NewCC);
          break;
        }
    } else if (IsSwapped) {
      // If we have SUB(r1, r2) and CMP(r2, r1), the condition code needs
      // to be changed from r2 > r1 to r1 < r2, from r2 < r1 to r1 > r2, etc.
      // We swap the condition code and synthesize the new opcode.
      ReplacementCC = getSwappedCondition(OldCC);
      if (ReplacementCC == X86::COND_INVALID)
        return false;
      ShouldUpdateCC = true;
    } else if (ImmDelta != 0) {
      unsigned BitWidth = TRI->getRegSizeInBits(*MRI->getRegClass(SrcReg));
      // Shift amount for min/max constants to adjust for 8/16/32 instruction
      // sizes.
      switch (OldCC) {
      case X86::COND_L: // x <s (C + 1)  -->  x <=s C
        if (ImmDelta != 1 || APInt::getSignedMinValue(BitWidth) == CmpValue)
          return false;
        ReplacementCC = X86::COND_LE;
        break;
      case X86::COND_B: // x <u (C + 1)  -->  x <=u C
        if (ImmDelta != 1 || CmpValue == 0)
          return false;
        ReplacementCC = X86::COND_BE;
        break;
      case X86::COND_GE: // x >=s (C + 1)  -->  x >s C
        if (ImmDelta != 1 || APInt::getSignedMinValue(BitWidth) == CmpValue)
          return false;
        ReplacementCC = X86::COND_G;
        break;
      case X86::COND_AE: // x >=u (C + 1)  -->  x >u C
        if (ImmDelta != 1 || CmpValue == 0)
          return false;
        ReplacementCC = X86::COND_A;
        break;
      case X86::COND_G: // x >s (C - 1)  -->  x >=s C
        if (ImmDelta != -1 || APInt::getSignedMaxValue(BitWidth) == CmpValue)
          return false;
        ReplacementCC = X86::COND_GE;
        break;
      case X86::COND_A: // x >u (C - 1)  -->  x >=u C
        if (ImmDelta != -1 || APInt::getMaxValue(BitWidth) == CmpValue)
          return false;
        ReplacementCC = X86::COND_AE;
        break;
      case X86::COND_LE: // x <=s (C - 1)  -->  x <s C
        if (ImmDelta != -1 || APInt::getSignedMaxValue(BitWidth) == CmpValue)
          return false;
        ReplacementCC = X86::COND_L;
        break;
      case X86::COND_BE: // x <=u (C - 1)  -->  x <u C
        if (ImmDelta != -1 || APInt::getMaxValue(BitWidth) == CmpValue)
          return false;
        ReplacementCC = X86::COND_B;
        break;
      default:
        return false;
      }
      ShouldUpdateCC = true;
    }

    if (ShouldUpdateCC && ReplacementCC != OldCC) {
      // Push the MachineInstr to OpsToUpdate.
      // If it is safe to remove CmpInstr, the condition code of these
      // instructions will be modified.
      OpsToUpdate.push_back(std::make_pair(&Instr, ReplacementCC));
    }
    if (ModifyEFLAGS || Instr.killsRegister(X86::EFLAGS, TRI)) {
      // It is safe to remove CmpInstr if EFLAGS is updated again or killed.
      FlagsMayLiveOut = false;
      break;
    }
  }

  // If we have to update users but EFLAGS is live-out abort, since we cannot
  // easily find all of the users.
  if ((MI != nullptr || ShouldUpdateCC) && FlagsMayLiveOut) {
    for (MachineBasicBlock *Successor : CmpMBB.successors())
      if (Successor->isLiveIn(X86::EFLAGS))
        return false;
  }

  // The instruction to be updated is either Sub or MI.
  assert((MI == nullptr || Sub == nullptr) && "Should not have Sub and MI set");
  Sub = MI != nullptr ? MI : Sub;
  MachineBasicBlock *SubBB = Sub->getParent();
  // Move Movr0Inst to the appropriate place before Sub.
  if (Movr0Inst) {
    // Only move within the same block so we don't accidentally move to a
    // block with higher execution frequency.
    if (&CmpMBB != SubBB)
      return false;
    // Look backwards until we find a def that doesn't use the current EFLAGS.
    MachineBasicBlock::reverse_iterator InsertI = Sub,
                                        InsertE = Sub->getParent()->rend();
    for (; InsertI != InsertE; ++InsertI) {
      MachineInstr *Instr = &*InsertI;
      if (!Instr->readsRegister(X86::EFLAGS, TRI) &&
          Instr->modifiesRegister(X86::EFLAGS, TRI)) {
        Movr0Inst->getParent()->remove(Movr0Inst);
        Instr->getParent()->insert(MachineBasicBlock::iterator(Instr),
                                   Movr0Inst);
        break;
      }
    }
    if (InsertI == InsertE)
      return false;
  }

  // Make sure Sub instruction defines EFLAGS and mark the def live.
  MachineOperand *FlagDef =
      Sub->findRegisterDefOperand(X86::EFLAGS, /*TRI=*/nullptr);
  assert(FlagDef && "Unable to locate a def EFLAGS operand");
  FlagDef->setIsDead(false);

  CmpInstr.eraseFromParent();

  // Modify the condition code of instructions in OpsToUpdate.
  for (auto &Op : OpsToUpdate) {
    Op.first->getOperand(Op.first->getDesc().getNumOperands() - 1)
        .setImm(Op.second);
  }
  // Add EFLAGS to block live-ins between CmpBB and block of flags producer.
  for (MachineBasicBlock *MBB = &CmpMBB; MBB != SubBB;
       MBB = *MBB->pred_begin()) {
    assert(MBB->pred_size() == 1 && "Expected exactly one predecessor");
    if (!MBB->isLiveIn(X86::EFLAGS))
      MBB->addLiveIn(X86::EFLAGS);
  }
  return true;
}

/// Try to remove the load by folding it to a register
/// operand at the use. We fold the load instructions if load defines a virtual
/// register, the virtual register is used once in the same BB, and the
/// instructions in-between do not load or store, and have no side effects.
MachineInstr *X86InstrInfo::optimizeLoadInstr(MachineInstr &MI,
                                              const MachineRegisterInfo *MRI,
                                              Register &FoldAsLoadDefReg,
                                              MachineInstr *&DefMI) const {
  // Check whether we can move DefMI here.
  DefMI = MRI->getVRegDef(FoldAsLoadDefReg);
  assert(DefMI);
  bool SawStore = false;
  if (!DefMI->isSafeToMove(nullptr, SawStore))
    return nullptr;

  // Collect information about virtual register operands of MI.
  SmallVector<unsigned, 1> SrcOperandIds;
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    if (Reg != FoldAsLoadDefReg)
      continue;
    // Do not fold if we have a subreg use or a def.
    if (MO.getSubReg() || MO.isDef())
      return nullptr;
    SrcOperandIds.push_back(i);
  }
  if (SrcOperandIds.empty())
    return nullptr;

  // Check whether we can fold the def into SrcOperandId.
  if (MachineInstr *FoldMI = foldMemoryOperand(MI, SrcOperandIds, *DefMI)) {
    FoldAsLoadDefReg = 0;
    return FoldMI;
  }

  return nullptr;
}

/// \returns true if the instruction can be changed to COPY when imm is 0.
static bool canConvert2Copy(unsigned Opc) {
  switch (Opc) {
  default:
    return false;
  CASE_ND(ADD64ri32)
  CASE_ND(SUB64ri32)
  CASE_ND(OR64ri32)
  CASE_ND(XOR64ri32)
  CASE_ND(ADD32ri)
  CASE_ND(SUB32ri)
  CASE_ND(OR32ri)
  CASE_ND(XOR32ri)
    return true;
  }
}

/// Convert an ALUrr opcode to corresponding ALUri opcode. Such as
///     ADD32rr  ==>  ADD32ri
static unsigned convertALUrr2ALUri(unsigned Opc) {
  switch (Opc) {
  default:
    return 0;
#define FROM_TO(FROM, TO)                                                      \
  case X86::FROM:                                                              \
    return X86::TO;                                                            \
  case X86::FROM##_ND:                                                         \
    return X86::TO##_ND;
    FROM_TO(ADD64rr, ADD64ri32)
    FROM_TO(ADC64rr, ADC64ri32)
    FROM_TO(SUB64rr, SUB64ri32)
    FROM_TO(SBB64rr, SBB64ri32)
    FROM_TO(AND64rr, AND64ri32)
    FROM_TO(OR64rr, OR64ri32)
    FROM_TO(XOR64rr, XOR64ri32)
    FROM_TO(SHR64rCL, SHR64ri)
    FROM_TO(SHL64rCL, SHL64ri)
    FROM_TO(SAR64rCL, SAR64ri)
    FROM_TO(ROL64rCL, ROL64ri)
    FROM_TO(ROR64rCL, ROR64ri)
    FROM_TO(RCL64rCL, RCL64ri)
    FROM_TO(RCR64rCL, RCR64ri)
    FROM_TO(ADD32rr, ADD32ri)
    FROM_TO(ADC32rr, ADC32ri)
    FROM_TO(SUB32rr, SUB32ri)
    FROM_TO(SBB32rr, SBB32ri)
    FROM_TO(AND32rr, AND32ri)
    FROM_TO(OR32rr, OR32ri)
    FROM_TO(XOR32rr, XOR32ri)
    FROM_TO(SHR32rCL, SHR32ri)
    FROM_TO(SHL32rCL, SHL32ri)
    FROM_TO(SAR32rCL, SAR32ri)
    FROM_TO(ROL32rCL, ROL32ri)
    FROM_TO(ROR32rCL, ROR32ri)
    FROM_TO(RCL32rCL, RCL32ri)
    FROM_TO(RCR32rCL, RCR32ri)
#undef FROM_TO
#define FROM_TO(FROM, TO)                                                      \
  case X86::FROM:                                                              \
    return X86::TO;
    FROM_TO(TEST64rr, TEST64ri32)
    FROM_TO(CTEST64rr, CTEST64ri32)
    FROM_TO(CMP64rr, CMP64ri32)
    FROM_TO(CCMP64rr, CCMP64ri32)
    FROM_TO(TEST32rr, TEST32ri)
    FROM_TO(CTEST32rr, CTEST32ri)
    FROM_TO(CMP32rr, CMP32ri)
    FROM_TO(CCMP32rr, CCMP32ri)
#undef FROM_TO
  }
}

/// Reg is assigned ImmVal in DefMI, and is used in UseMI.
/// If MakeChange is true, this function tries to replace Reg by ImmVal in
/// UseMI. If MakeChange is false, just check if folding is possible.
//
/// \returns true if folding is successful or possible.
bool X86InstrInfo::foldImmediateImpl(MachineInstr &UseMI, MachineInstr *DefMI,
                                     Register Reg, int64_t ImmVal,
                                     MachineRegisterInfo *MRI,
                                     bool MakeChange) const {
  bool Modified = false;

  // 64 bit operations accept sign extended 32 bit immediates.
  // 32 bit operations accept all 32 bit immediates, so we don't need to check
  // them.
  const TargetRegisterClass *RC = nullptr;
  if (Reg.isVirtual())
    RC = MRI->getRegClass(Reg);
  if ((Reg.isPhysical() && X86::GR64RegClass.contains(Reg)) ||
      (Reg.isVirtual() && X86::GR64RegClass.hasSubClassEq(RC))) {
    if (!isInt<32>(ImmVal))
      return false;
  }

  if (UseMI.findRegisterUseOperand(Reg, /*TRI=*/nullptr)->getSubReg())
    return false;
  // Immediate has larger code size than register. So avoid folding the
  // immediate if it has more than 1 use and we are optimizing for size.
  if (UseMI.getMF()->getFunction().hasOptSize() && Reg.isVirtual() &&
      !MRI->hasOneNonDBGUse(Reg))
    return false;

  unsigned Opc = UseMI.getOpcode();
  unsigned NewOpc;
  if (Opc == TargetOpcode::COPY) {
    Register ToReg = UseMI.getOperand(0).getReg();
    const TargetRegisterClass *RC = nullptr;
    if (ToReg.isVirtual())
      RC = MRI->getRegClass(ToReg);
    bool GR32Reg = (ToReg.isVirtual() && X86::GR32RegClass.hasSubClassEq(RC)) ||
                   (ToReg.isPhysical() && X86::GR32RegClass.contains(ToReg));
    bool GR64Reg = (ToReg.isVirtual() && X86::GR64RegClass.hasSubClassEq(RC)) ||
                   (ToReg.isPhysical() && X86::GR64RegClass.contains(ToReg));
    bool GR8Reg = (ToReg.isVirtual() && X86::GR8RegClass.hasSubClassEq(RC)) ||
                  (ToReg.isPhysical() && X86::GR8RegClass.contains(ToReg));

    if (ImmVal == 0) {
      // We have MOV32r0 only.
      if (!GR32Reg)
        return false;
    }

    if (GR64Reg) {
      if (isUInt<32>(ImmVal))
        NewOpc = X86::MOV32ri64;
      else
        NewOpc = X86::MOV64ri;
    } else if (GR32Reg) {
      NewOpc = X86::MOV32ri;
      if (ImmVal == 0) {
        // MOV32r0 clobbers EFLAGS.
        const TargetRegisterInfo *TRI = &getRegisterInfo();
        if (UseMI.getParent()->computeRegisterLiveness(
                TRI, X86::EFLAGS, UseMI) != MachineBasicBlock::LQR_Dead)
          return false;

        // MOV32r0 is different than other cases because it doesn't encode the
        // immediate in the instruction. So we directly modify it here.
        if (!MakeChange)
          return true;
        UseMI.setDesc(get(X86::MOV32r0));
        UseMI.removeOperand(
            UseMI.findRegisterUseOperandIdx(Reg, /*TRI=*/nullptr));
        UseMI.addOperand(MachineOperand::CreateReg(X86::EFLAGS, /*isDef=*/true,
                                                   /*isImp=*/true,
                                                   /*isKill=*/false,
                                                   /*isDead=*/true));
        Modified = true;
      }
    } else if (GR8Reg)
      NewOpc = X86::MOV8ri;
    else
      return false;
  } else
    NewOpc = convertALUrr2ALUri(Opc);

  if (!NewOpc)
    return false;

  // For SUB instructions the immediate can only be the second source operand.
  if ((NewOpc == X86::SUB64ri32 || NewOpc == X86::SUB32ri ||
       NewOpc == X86::SBB64ri32 || NewOpc == X86::SBB32ri ||
       NewOpc == X86::SUB64ri32_ND || NewOpc == X86::SUB32ri_ND ||
       NewOpc == X86::SBB64ri32_ND || NewOpc == X86::SBB32ri_ND) &&
      UseMI.findRegisterUseOperandIdx(Reg, /*TRI=*/nullptr) != 2)
    return false;
  // For CMP instructions the immediate can only be at index 1.
  if (((NewOpc == X86::CMP64ri32 || NewOpc == X86::CMP32ri) ||
       (NewOpc == X86::CCMP64ri32 || NewOpc == X86::CCMP32ri)) &&
      UseMI.findRegisterUseOperandIdx(Reg, /*TRI=*/nullptr) != 1)
    return false;

  using namespace X86;
  if (isSHL(Opc) || isSHR(Opc) || isSAR(Opc) || isROL(Opc) || isROR(Opc) ||
      isRCL(Opc) || isRCR(Opc)) {
    unsigned RegIdx = UseMI.findRegisterUseOperandIdx(Reg, /*TRI=*/nullptr);
    if (RegIdx < 2)
      return false;
    if (!isInt<8>(ImmVal))
      return false;
    assert(Reg == X86::CL);

    if (!MakeChange)
      return true;
    UseMI.setDesc(get(NewOpc));
    UseMI.removeOperand(RegIdx);
    UseMI.addOperand(MachineOperand::CreateImm(ImmVal));
    // Reg is physical register $cl, so we don't know if DefMI is dead through
    // MRI. Let the caller handle it, or pass dead-mi-elimination can delete
    // the dead physical register define instruction.
    return true;
  }

  if (!MakeChange)
    return true;

  if (!Modified) {
    // Modify the instruction.
    if (ImmVal == 0 && canConvert2Copy(NewOpc) &&
        UseMI.registerDefIsDead(X86::EFLAGS, /*TRI=*/nullptr)) {
      //          %100 = add %101, 0
      //    ==>
      //          %100 = COPY %101
      UseMI.setDesc(get(TargetOpcode::COPY));
      UseMI.removeOperand(
          UseMI.findRegisterUseOperandIdx(Reg, /*TRI=*/nullptr));
      UseMI.removeOperand(
          UseMI.findRegisterDefOperandIdx(X86::EFLAGS, /*TRI=*/nullptr));
      UseMI.untieRegOperand(0);
      UseMI.clearFlag(MachineInstr::MIFlag::NoSWrap);
      UseMI.clearFlag(MachineInstr::MIFlag::NoUWrap);
    } else {
      unsigned Op1 = 1, Op2 = CommuteAnyOperandIndex;
      unsigned ImmOpNum = 2;
      if (!UseMI.getOperand(0).isDef()) {
        Op1 = 0; // TEST, CMP, CTEST, CCMP
        ImmOpNum = 1;
      }
      if (Opc == TargetOpcode::COPY)
        ImmOpNum = 1;
      if (findCommutedOpIndices(UseMI, Op1, Op2) &&
          UseMI.getOperand(Op1).getReg() == Reg)
        commuteInstruction(UseMI);

      assert(UseMI.getOperand(ImmOpNum).getReg() == Reg);
      UseMI.setDesc(get(NewOpc));
      UseMI.getOperand(ImmOpNum).ChangeToImmediate(ImmVal);
    }
  }

  if (Reg.isVirtual() && MRI->use_nodbg_empty(Reg))
    DefMI->eraseFromBundle();

  return true;
}

/// foldImmediate - 'Reg' is known to be defined by a move immediate
/// instruction, try to fold the immediate into the use instruction.
bool X86InstrInfo::foldImmediate(MachineInstr &UseMI, MachineInstr &DefMI,
                                 Register Reg, MachineRegisterInfo *MRI) const {
  int64_t ImmVal;
  if (!getConstValDefinedInReg(DefMI, Reg, ImmVal))
    return false;

  return foldImmediateImpl(UseMI, &DefMI, Reg, ImmVal, MRI, true);
}

/// Expand a single-def pseudo instruction to a two-addr
/// instruction with two undef reads of the register being defined.
/// This is used for mapping:
///   %xmm4 = V_SET0
/// to:
///   %xmm4 = PXORrr undef %xmm4, undef %xmm4
///
static bool Expand2AddrUndef(MachineInstrBuilder &MIB,
                             const MCInstrDesc &Desc) {
  assert(Desc.getNumOperands() == 3 && "Expected two-addr instruction.");
  Register Reg = MIB.getReg(0);
  MIB->setDesc(Desc);

  // MachineInstr::addOperand() will insert explicit operands before any
  // implicit operands.
  MIB.addReg(Reg, RegState::Undef).addReg(Reg, RegState::Undef);
  // But we don't trust that.
  assert(MIB.getReg(1) == Reg && MIB.getReg(2) == Reg && "Misplaced operand");
  return true;
}

/// Expand a single-def pseudo instruction to a two-addr
/// instruction with two %k0 reads.
/// This is used for mapping:
///   %k4 = K_SET1
/// to:
///   %k4 = KXNORrr %k0, %k0
static bool Expand2AddrKreg(MachineInstrBuilder &MIB, const MCInstrDesc &Desc,
                            Register Reg) {
  assert(Desc.getNumOperands() == 3 && "Expected two-addr instruction.");
  MIB->setDesc(Desc);
  MIB.addReg(Reg, RegState::Undef).addReg(Reg, RegState::Undef);
  return true;
}

static bool expandMOV32r1(MachineInstrBuilder &MIB, const TargetInstrInfo &TII,
                          bool MinusOne) {
  MachineBasicBlock &MBB = *MIB->getParent();
  const DebugLoc &DL = MIB->getDebugLoc();
  Register Reg = MIB.getReg(0);

  // Insert the XOR.
  BuildMI(MBB, MIB.getInstr(), DL, TII.get(X86::XOR32rr), Reg)
      .addReg(Reg, RegState::Undef)
      .addReg(Reg, RegState::Undef);

  // Turn the pseudo into an INC or DEC.
  MIB->setDesc(TII.get(MinusOne ? X86::DEC32r : X86::INC32r));
  MIB.addReg(Reg);

  return true;
}

static bool ExpandMOVImmSExti8(MachineInstrBuilder &MIB,
                               const TargetInstrInfo &TII,
                               const X86Subtarget &Subtarget) {
  MachineBasicBlock &MBB = *MIB->getParent();
  const DebugLoc &DL = MIB->getDebugLoc();
  int64_t Imm = MIB->getOperand(1).getImm();
  assert(Imm != 0 && "Using push/pop for 0 is not efficient.");
  MachineBasicBlock::iterator I = MIB.getInstr();

  int StackAdjustment;

  if (Subtarget.is64Bit()) {
    assert(MIB->getOpcode() == X86::MOV64ImmSExti8 ||
           MIB->getOpcode() == X86::MOV32ImmSExti8);

    // Can't use push/pop lowering if the function might write to the red zone.
    X86MachineFunctionInfo *X86FI =
        MBB.getParent()->getInfo<X86MachineFunctionInfo>();
    if (X86FI->getUsesRedZone()) {
      MIB->setDesc(TII.get(MIB->getOpcode() == X86::MOV32ImmSExti8
                               ? X86::MOV32ri
                               : X86::MOV64ri));
      return true;
    }

    // 64-bit mode doesn't have 32-bit push/pop, so use 64-bit operations and
    // widen the register if necessary.
    StackAdjustment = 8;
    BuildMI(MBB, I, DL, TII.get(X86::PUSH64i32)).addImm(Imm);
    MIB->setDesc(TII.get(X86::POP64r));
    MIB->getOperand(0).setReg(getX86SubSuperRegister(MIB.getReg(0), 64));
  } else {
    assert(MIB->getOpcode() == X86::MOV32ImmSExti8);
    StackAdjustment = 4;
    BuildMI(MBB, I, DL, TII.get(X86::PUSH32i)).addImm(Imm);
    MIB->setDesc(TII.get(X86::POP32r));
  }
  MIB->removeOperand(1);
  MIB->addImplicitDefUseOperands(*MBB.getParent());

  // Build CFI if necessary.
  MachineFunction &MF = *MBB.getParent();
  const X86FrameLowering *TFL = Subtarget.getFrameLowering();
  bool IsWin64Prologue = MF.getTarget().getMCAsmInfo()->usesWindowsCFI();
  bool NeedsDwarfCFI = !IsWin64Prologue && MF.needsFrameMoves();
  bool EmitCFI = !TFL->hasFP(MF) && NeedsDwarfCFI;
  if (EmitCFI) {
    TFL->BuildCFI(
        MBB, I, DL,
        MCCFIInstruction::createAdjustCfaOffset(nullptr, StackAdjustment));
    TFL->BuildCFI(
        MBB, std::next(I), DL,
        MCCFIInstruction::createAdjustCfaOffset(nullptr, -StackAdjustment));
  }

  return true;
}

// LoadStackGuard has so far only been implemented for 64-bit MachO. Different
// code sequence is needed for other targets.
static void expandLoadStackGuard(MachineInstrBuilder &MIB,
                                 const TargetInstrInfo &TII) {
  MachineBasicBlock &MBB = *MIB->getParent();
  const DebugLoc &DL = MIB->getDebugLoc();
  Register Reg = MIB.getReg(0);
  const GlobalValue *GV =
      cast<GlobalValue>((*MIB->memoperands_begin())->getValue());
  auto Flags = MachineMemOperand::MOLoad |
               MachineMemOperand::MODereferenceable |
               MachineMemOperand::MOInvariant;
  MachineMemOperand *MMO = MBB.getParent()->getMachineMemOperand(
      MachinePointerInfo::getGOT(*MBB.getParent()), Flags, 8, Align(8));
  MachineBasicBlock::iterator I = MIB.getInstr();

  BuildMI(MBB, I, DL, TII.get(X86::MOV64rm), Reg)
      .addReg(X86::RIP)
      .addImm(1)
      .addReg(0)
      .addGlobalAddress(GV, 0, X86II::MO_GOTPCREL)
      .addReg(0)
      .addMemOperand(MMO);
  MIB->setDebugLoc(DL);
  MIB->setDesc(TII.get(X86::MOV64rm));
  MIB.addReg(Reg, RegState::Kill).addImm(1).addReg(0).addImm(0).addReg(0);
}

static bool expandXorFP(MachineInstrBuilder &MIB, const TargetInstrInfo &TII) {
  MachineBasicBlock &MBB = *MIB->getParent();
  MachineFunction &MF = *MBB.getParent();
  const X86Subtarget &Subtarget = MF.getSubtarget<X86Subtarget>();
  const X86RegisterInfo *TRI = Subtarget.getRegisterInfo();
  unsigned XorOp =
      MIB->getOpcode() == X86::XOR64_FP ? X86::XOR64rr : X86::XOR32rr;
  MIB->setDesc(TII.get(XorOp));
  MIB.addReg(TRI->getFrameRegister(MF), RegState::Undef);
  return true;
}

// This is used to handle spills for 128/256-bit registers when we have AVX512,
// but not VLX. If it uses an extended register we need to use an instruction
// that loads the lower 128/256-bit, but is available with only AVX512F.
static bool expandNOVLXLoad(MachineInstrBuilder &MIB,
                            const TargetRegisterInfo *TRI,
                            const MCInstrDesc &LoadDesc,
                            const MCInstrDesc &BroadcastDesc, unsigned SubIdx) {
  Register DestReg = MIB.getReg(0);
  // Check if DestReg is XMM16-31 or YMM16-31.
  if (TRI->getEncodingValue(DestReg) < 16) {
    // We can use a normal VEX encoded load.
    MIB->setDesc(LoadDesc);
  } else {
    // Use a 128/256-bit VBROADCAST instruction.
    MIB->setDesc(BroadcastDesc);
    // Change the destination to a 512-bit register.
    DestReg = TRI->getMatchingSuperReg(DestReg, SubIdx, &X86::VR512RegClass);
    MIB->getOperand(0).setReg(DestReg);
  }
  return true;
}

// This is used to handle spills for 128/256-bit registers when we have AVX512,
// but not VLX. If it uses an extended register we need to use an instruction
// that stores the lower 128/256-bit, but is available with only AVX512F.
static bool expandNOVLXStore(MachineInstrBuilder &MIB,
                             const TargetRegisterInfo *TRI,
                             const MCInstrDesc &StoreDesc,
                             const MCInstrDesc &ExtractDesc, unsigned SubIdx) {
  Register SrcReg = MIB.getReg(X86::AddrNumOperands);
  // Check if DestReg is XMM16-31 or YMM16-31.
  if (TRI->getEncodingValue(SrcReg) < 16) {
    // We can use a normal VEX encoded store.
    MIB->setDesc(StoreDesc);
  } else {
    // Use a VEXTRACTF instruction.
    MIB->setDesc(ExtractDesc);
    // Change the destination to a 512-bit register.
    SrcReg = TRI->getMatchingSuperReg(SrcReg, SubIdx, &X86::VR512RegClass);
    MIB->getOperand(X86::AddrNumOperands).setReg(SrcReg);
    MIB.addImm(0x0); // Append immediate to extract from the lower bits.
  }

  return true;
}

static bool expandSHXDROT(MachineInstrBuilder &MIB, const MCInstrDesc &Desc) {
  MIB->setDesc(Desc);
  int64_t ShiftAmt = MIB->getOperand(2).getImm();
  // Temporarily remove the immediate so we can add another source register.
  MIB->removeOperand(2);
  // Add the register. Don't copy the kill flag if there is one.
  MIB.addReg(MIB.getReg(1), getUndefRegState(MIB->getOperand(1).isUndef()));
  // Add back the immediate.
  MIB.addImm(ShiftAmt);
  return true;
}

bool X86InstrInfo::expandPostRAPseudo(MachineInstr &MI) const {
  bool HasAVX = Subtarget.hasAVX();
  MachineInstrBuilder MIB(*MI.getParent()->getParent(), MI);
  switch (MI.getOpcode()) {
  case X86::MOV32r0:
    return Expand2AddrUndef(MIB, get(X86::XOR32rr));
  case X86::MOV32r1:
    return expandMOV32r1(MIB, *this, /*MinusOne=*/false);
  case X86::MOV32r_1:
    return expandMOV32r1(MIB, *this, /*MinusOne=*/true);
  case X86::MOV32ImmSExti8:
  case X86::MOV64ImmSExti8:
    return ExpandMOVImmSExti8(MIB, *this, Subtarget);
  case X86::SETB_C32r:
    return Expand2AddrUndef(MIB, get(X86::SBB32rr));
  case X86::SETB_C64r:
    return Expand2AddrUndef(MIB, get(X86::SBB64rr));
  case X86::MMX_SET0:
    return Expand2AddrUndef(MIB, get(X86::MMX_PXORrr));
  case X86::V_SET0:
  case X86::FsFLD0SS:
  case X86::FsFLD0SD:
  case X86::FsFLD0SH:
  case X86::FsFLD0F128:
    return Expand2AddrUndef(MIB, get(HasAVX ? X86::VXORPSrr : X86::XORPSrr));
  case X86::AVX_SET0: {
    assert(HasAVX && "AVX not supported");
    const TargetRegisterInfo *TRI = &getRegisterInfo();
    Register SrcReg = MIB.getReg(0);
    Register XReg = TRI->getSubReg(SrcReg, X86::sub_xmm);
    MIB->getOperand(0).setReg(XReg);
    Expand2AddrUndef(MIB, get(X86::VXORPSrr));
    MIB.addReg(SrcReg, RegState::ImplicitDefine);
    return true;
  }
  case X86::AVX512_128_SET0:
  case X86::AVX512_FsFLD0SH:
  case X86::AVX512_FsFLD0SS:
  case X86::AVX512_FsFLD0SD:
  case X86::AVX512_FsFLD0F128: {
    bool HasVLX = Subtarget.hasVLX();
    Register SrcReg = MIB.getReg(0);
    const TargetRegisterInfo *TRI = &getRegisterInfo();
    if (HasVLX || TRI->getEncodingValue(SrcReg) < 16)
      return Expand2AddrUndef(MIB,
                              get(HasVLX ? X86::VPXORDZ128rr : X86::VXORPSrr));
    // Extended register without VLX. Use a larger XOR.
    SrcReg =
        TRI->getMatchingSuperReg(SrcReg, X86::sub_xmm, &X86::VR512RegClass);
    MIB->getOperand(0).setReg(SrcReg);
    return Expand2AddrUndef(MIB, get(X86::VPXORDZrr));
  }
  case X86::AVX512_256_SET0:
  case X86::AVX512_512_SET0: {
    bool HasVLX = Subtarget.hasVLX();
    Register SrcReg = MIB.getReg(0);
    const TargetRegisterInfo *TRI = &getRegisterInfo();
    if (HasVLX || TRI->getEncodingValue(SrcReg) < 16) {
      Register XReg = TRI->getSubReg(SrcReg, X86::sub_xmm);
      MIB->getOperand(0).setReg(XReg);
      Expand2AddrUndef(MIB, get(HasVLX ? X86::VPXORDZ128rr : X86::VXORPSrr));
      MIB.addReg(SrcReg, RegState::ImplicitDefine);
      return true;
    }
    if (MI.getOpcode() == X86::AVX512_256_SET0) {
      // No VLX so we must reference a zmm.
      unsigned ZReg =
          TRI->getMatchingSuperReg(SrcReg, X86::sub_ymm, &X86::VR512RegClass);
      MIB->getOperand(0).setReg(ZReg);
    }
    return Expand2AddrUndef(MIB, get(X86::VPXORDZrr));
  }
  case X86::V_SETALLONES:
    return Expand2AddrUndef(MIB,
                            get(HasAVX ? X86::VPCMPEQDrr : X86::PCMPEQDrr));
  case X86::AVX2_SETALLONES:
    return Expand2AddrUndef(MIB, get(X86::VPCMPEQDYrr));
  case X86::AVX1_SETALLONES: {
    Register Reg = MIB.getReg(0);
    // VCMPPSYrri with an immediate 0xf should produce VCMPTRUEPS.
    MIB->setDesc(get(X86::VCMPPSYrri));
    MIB.addReg(Reg, RegState::Undef).addReg(Reg, RegState::Undef).addImm(0xf);
    return true;
  }
  case X86::AVX512_512_SETALLONES: {
    Register Reg = MIB.getReg(0);
    MIB->setDesc(get(X86::VPTERNLOGDZrri));
    // VPTERNLOGD needs 3 register inputs and an immediate.
    // 0xff will return 1s for any input.
    MIB.addReg(Reg, RegState::Undef)
        .addReg(Reg, RegState::Undef)
        .addReg(Reg, RegState::Undef)
        .addImm(0xff);
    return true;
  }
  case X86::AVX512_512_SEXT_MASK_32:
  case X86::AVX512_512_SEXT_MASK_64: {
    Register Reg = MIB.getReg(0);
    Register MaskReg = MIB.getReg(1);
    unsigned MaskState = getRegState(MIB->getOperand(1));
    unsigned Opc = (MI.getOpcode() == X86::AVX512_512_SEXT_MASK_64)
                       ? X86::VPTERNLOGQZrrikz
                       : X86::VPTERNLOGDZrrikz;
    MI.removeOperand(1);
    MIB->setDesc(get(Opc));
    // VPTERNLOG needs 3 register inputs and an immediate.
    // 0xff will return 1s for any input.
    MIB.addReg(Reg, RegState::Undef)
        .addReg(MaskReg, MaskState)
        .addReg(Reg, RegState::Undef)
        .addReg(Reg, RegState::Undef)
        .addImm(0xff);
    return true;
  }
  case X86::VMOVAPSZ128rm_NOVLX:
    return expandNOVLXLoad(MIB, &getRegisterInfo(), get(X86::VMOVAPSrm),
                           get(X86::VBROADCASTF32X4rm), X86::sub_xmm);
  case X86::VMOVUPSZ128rm_NOVLX:
    return expandNOVLXLoad(MIB, &getRegisterInfo(), get(X86::VMOVUPSrm),
                           get(X86::VBROADCASTF32X4rm), X86::sub_xmm);
  case X86::VMOVAPSZ256rm_NOVLX:
    return expandNOVLXLoad(MIB, &getRegisterInfo(), get(X86::VMOVAPSYrm),
                           get(X86::VBROADCASTF64X4rm), X86::sub_ymm);
  case X86::VMOVUPSZ256rm_NOVLX:
    return expandNOVLXLoad(MIB, &getRegisterInfo(), get(X86::VMOVUPSYrm),
                           get(X86::VBROADCASTF64X4rm), X86::sub_ymm);
  case X86::VMOVAPSZ128mr_NOVLX:
    return expandNOVLXStore(MIB, &getRegisterInfo(), get(X86::VMOVAPSmr),
                            get(X86::VEXTRACTF32x4Zmr), X86::sub_xmm);
  case X86::VMOVUPSZ128mr_NOVLX:
    return expandNOVLXStore(MIB, &getRegisterInfo(), get(X86::VMOVUPSmr),
                            get(X86::VEXTRACTF32x4Zmr), X86::sub_xmm);
  case X86::VMOVAPSZ256mr_NOVLX:
    return expandNOVLXStore(MIB, &getRegisterInfo(), get(X86::VMOVAPSYmr),
                            get(X86::VEXTRACTF64x4Zmr), X86::sub_ymm);
  case X86::VMOVUPSZ256mr_NOVLX:
    return expandNOVLXStore(MIB, &getRegisterInfo(), get(X86::VMOVUPSYmr),
                            get(X86::VEXTRACTF64x4Zmr), X86::sub_ymm);
  case X86::MOV32ri64: {
    Register Reg = MIB.getReg(0);
    Register Reg32 = RI.getSubReg(Reg, X86::sub_32bit);
    MI.setDesc(get(X86::MOV32ri));
    MIB->getOperand(0).setReg(Reg32);
    MIB.addReg(Reg, RegState::ImplicitDefine);
    return true;
  }

  case X86::RDFLAGS32:
  case X86::RDFLAGS64: {
    unsigned Is64Bit = MI.getOpcode() == X86::RDFLAGS64;
    MachineBasicBlock &MBB = *MIB->getParent();

    MachineInstr *NewMI = BuildMI(MBB, MI, MIB->getDebugLoc(),
                                  get(Is64Bit ? X86::PUSHF64 : X86::PUSHF32))
                              .getInstr();

    // Permit reads of the EFLAGS and DF registers without them being defined.
    // This intrinsic exists to read external processor state in flags, such as
    // the trap flag, interrupt flag, and direction flag, none of which are
    // modeled by the backend.
    assert(NewMI->getOperand(2).getReg() == X86::EFLAGS &&
           "Unexpected register in operand! Should be EFLAGS.");
    NewMI->getOperand(2).setIsUndef();
    assert(NewMI->getOperand(3).getReg() == X86::DF &&
           "Unexpected register in operand! Should be DF.");
    NewMI->getOperand(3).setIsUndef();

    MIB->setDesc(get(Is64Bit ? X86::POP64r : X86::POP32r));
    return true;
  }

  case X86::WRFLAGS32:
  case X86::WRFLAGS64: {
    unsigned Is64Bit = MI.getOpcode() == X86::WRFLAGS64;
    MachineBasicBlock &MBB = *MIB->getParent();

    BuildMI(MBB, MI, MIB->getDebugLoc(),
            get(Is64Bit ? X86::PUSH64r : X86::PUSH32r))
        .addReg(MI.getOperand(0).getReg());
    BuildMI(MBB, MI, MIB->getDebugLoc(),
            get(Is64Bit ? X86::POPF64 : X86::POPF32));
    MI.eraseFromParent();
    return true;
  }

  // KNL does not recognize dependency-breaking idioms for mask registers,
  // so kxnor %k1, %k1, %k2 has a RAW dependence on %k1.
  // Using %k0 as the undef input register is a performance heuristic based
  // on the assumption that %k0 is used less frequently than the other mask
  // registers, since it is not usable as a write mask.
  // FIXME: A more advanced approach would be to choose the best input mask
  // register based on context.
  case X86::KSET0W:
    return Expand2AddrKreg(MIB, get(X86::KXORWrr), X86::K0);
  case X86::KSET0D:
    return Expand2AddrKreg(MIB, get(X86::KXORDrr), X86::K0);
  case X86::KSET0Q:
    return Expand2AddrKreg(MIB, get(X86::KXORQrr), X86::K0);
  case X86::KSET1W:
    return Expand2AddrKreg(MIB, get(X86::KXNORWrr), X86::K0);
  case X86::KSET1D:
    return Expand2AddrKreg(MIB, get(X86::KXNORDrr), X86::K0);
  case X86::KSET1Q:
    return Expand2AddrKreg(MIB, get(X86::KXNORQrr), X86::K0);
  case TargetOpcode::LOAD_STACK_GUARD:
    expandLoadStackGuard(MIB, *this);
    return true;
  case X86::XOR64_FP:
  case X86::XOR32_FP:
    return expandXorFP(MIB, *this);
  case X86::SHLDROT32ri:
    return expandSHXDROT(MIB, get(X86::SHLD32rri8));
  case X86::SHLDROT64ri:
    return expandSHXDROT(MIB, get(X86::SHLD64rri8));
  case X86::SHRDROT32ri:
    return expandSHXDROT(MIB, get(X86::SHRD32rri8));
  case X86::SHRDROT64ri:
    return expandSHXDROT(MIB, get(X86::SHRD64rri8));
  case X86::ADD8rr_DB:
    MIB->setDesc(get(X86::OR8rr));
    break;
  case X86::ADD16rr_DB:
    MIB->setDesc(get(X86::OR16rr));
    break;
  case X86::ADD32rr_DB:
    MIB->setDesc(get(X86::OR32rr));
    break;
  case X86::ADD64rr_DB:
    MIB->setDesc(get(X86::OR64rr));
    break;
  case X86::ADD8ri_DB:
    MIB->setDesc(get(X86::OR8ri));
    break;
  case X86::ADD16ri_DB:
    MIB->setDesc(get(X86::OR16ri));
    break;
  case X86::ADD32ri_DB:
    MIB->setDesc(get(X86::OR32ri));
    break;
  case X86::ADD64ri32_DB:
    MIB->setDesc(get(X86::OR64ri32));
    break;
  }
  return false;
}

/// Return true for all instructions that only update
/// the first 32 or 64-bits of the destination register and leave the rest
/// unmodified. This can be used to avoid folding loads if the instructions
/// only update part of the destination register, and the non-updated part is
/// not needed. e.g. cvtss2sd, sqrtss. Unfolding the load from these
/// instructions breaks the partial register dependency and it can improve
/// performance. e.g.:
///
///   movss (%rdi), %xmm0
///   cvtss2sd %xmm0, %xmm0
///
/// Instead of
///   cvtss2sd (%rdi), %xmm0
///
/// FIXME: This should be turned into a TSFlags.
///
static bool hasPartialRegUpdate(unsigned Opcode, const X86Subtarget &Subtarget,
                                bool ForLoadFold = false) {
  switch (Opcode) {
  case X86::CVTSI2SSrr:
  case X86::CVTSI2SSrm:
  case X86::CVTSI642SSrr:
  case X86::CVTSI642SSrm:
  case X86::CVTSI2SDrr:
  case X86::CVTSI2SDrm:
  case X86::CVTSI642SDrr:
  case X86::CVTSI642SDrm:
    // Load folding won't effect the undef register update since the input is
    // a GPR.
    return !ForLoadFold;
  case X86::CVTSD2SSrr:
  case X86::CVTSD2SSrm:
  case X86::CVTSS2SDrr:
  case X86::CVTSS2SDrm:
  case X86::MOVHPDrm:
  case X86::MOVHPSrm:
  case X86::MOVLPDrm:
  case X86::MOVLPSrm:
  case X86::RCPSSr:
  case X86::RCPSSm:
  case X86::RCPSSr_Int:
  case X86::RCPSSm_Int:
  case X86::ROUNDSDri:
  case X86::ROUNDSDmi:
  case X86::ROUNDSSri:
  case X86::ROUNDSSmi:
  case X86::RSQRTSSr:
  case X86::RSQRTSSm:
  case X86::RSQRTSSr_Int:
  case X86::RSQRTSSm_Int:
  case X86::SQRTSSr:
  case X86::SQRTSSm:
  case X86::SQRTSSr_Int:
  case X86::SQRTSSm_Int:
  case X86::SQRTSDr:
  case X86::SQRTSDm:
  case X86::SQRTSDr_Int:
  case X86::SQRTSDm_Int:
    return true;
  case X86::VFCMULCPHZ128rm:
  case X86::VFCMULCPHZ128rmb:
  case X86::VFCMULCPHZ128rmbkz:
  case X86::VFCMULCPHZ128rmkz:
  case X86::VFCMULCPHZ128rr:
  case X86::VFCMULCPHZ128rrkz:
  case X86::VFCMULCPHZ256rm:
  case X86::VFCMULCPHZ256rmb:
  case X86::VFCMULCPHZ256rmbkz:
  case X86::VFCMULCPHZ256rmkz:
  case X86::VFCMULCPHZ256rr:
  case X86::VFCMULCPHZ256rrkz:
  case X86::VFCMULCPHZrm:
  case X86::VFCMULCPHZrmb:
  case X86::VFCMULCPHZrmbkz:
  case X86::VFCMULCPHZrmkz:
  case X86::VFCMULCPHZrr:
  case X86::VFCMULCPHZrrb:
  case X86::VFCMULCPHZrrbkz:
  case X86::VFCMULCPHZrrkz:
  case X86::VFMULCPHZ128rm:
  case X86::VFMULCPHZ128rmb:
  case X86::VFMULCPHZ128rmbkz:
  case X86::VFMULCPHZ128rmkz:
  case X86::VFMULCPHZ128rr:
  case X86::VFMULCPHZ128rrkz:
  case X86::VFMULCPHZ256rm:
  case X86::VFMULCPHZ256rmb:
  case X86::VFMULCPHZ256rmbkz:
  case X86::VFMULCPHZ256rmkz:
  case X86::VFMULCPHZ256rr:
  case X86::VFMULCPHZ256rrkz:
  case X86::VFMULCPHZrm:
  case X86::VFMULCPHZrmb:
  case X86::VFMULCPHZrmbkz:
  case X86::VFMULCPHZrmkz:
  case X86::VFMULCPHZrr:
  case X86::VFMULCPHZrrb:
  case X86::VFMULCPHZrrbkz:
  case X86::VFMULCPHZrrkz:
  case X86::VFCMULCSHZrm:
  case X86::VFCMULCSHZrmkz:
  case X86::VFCMULCSHZrr:
  case X86::VFCMULCSHZrrb:
  case X86::VFCMULCSHZrrbkz:
  case X86::VFCMULCSHZrrkz:
  case X86::VFMULCSHZrm:
  case X86::VFMULCSHZrmkz:
  case X86::VFMULCSHZrr:
  case X86::VFMULCSHZrrb:
  case X86::VFMULCSHZrrbkz:
  case X86::VFMULCSHZrrkz:
    return Subtarget.hasMULCFalseDeps();
  case X86::VPERMDYrm:
  case X86::VPERMDYrr:
  case X86::VPERMQYmi:
  case X86::VPERMQYri:
  case X86::VPERMPSYrm:
  case X86::VPERMPSYrr:
  case X86::VPERMPDYmi:
  case X86::VPERMPDYri:
  case X86::VPERMDZ256rm:
  case X86::VPERMDZ256rmb:
  case X86::VPERMDZ256rmbkz:
  case X86::VPERMDZ256rmkz:
  case X86::VPERMDZ256rr:
  case X86::VPERMDZ256rrkz:
  case X86::VPERMDZrm:
  case X86::VPERMDZrmb:
  case X86::VPERMDZrmbkz:
  case X86::VPERMDZrmkz:
  case X86::VPERMDZrr:
  case X86::VPERMDZrrkz:
  case X86::VPERMQZ256mbi:
  case X86::VPERMQZ256mbikz:
  case X86::VPERMQZ256mi:
  case X86::VPERMQZ256mikz:
  case X86::VPERMQZ256ri:
  case X86::VPERMQZ256rikz:
  case X86::VPERMQZ256rm:
  case X86::VPERMQZ256rmb:
  case X86::VPERMQZ256rmbkz:
  case X86::VPERMQZ256rmkz:
  case X86::VPERMQZ256rr:
  case X86::VPERMQZ256rrkz:
  case X86::VPERMQZmbi:
  case X86::VPERMQZmbikz:
  case X86::VPERMQZmi:
  case X86::VPERMQZmikz:
  case X86::VPERMQZri:
  case X86::VPERMQZrikz:
  case X86::VPERMQZrm:
  case X86::VPERMQZrmb:
  case X86::VPERMQZrmbkz:
  case X86::VPERMQZrmkz:
  case X86::VPERMQZrr:
  case X86::VPERMQZrrkz:
  case X86::VPERMPSZ256rm:
  case X86::VPERMPSZ256rmb:
  case X86::VPERMPSZ256rmbkz:
  case X86::VPERMPSZ256rmkz:
  case X86::VPERMPSZ256rr:
  case X86::VPERMPSZ256rrkz:
  case X86::VPERMPSZrm:
  case X86::VPERMPSZrmb:
  case X86::VPERMPSZrmbkz:
  case X86::VPERMPSZrmkz:
  case X86::VPERMPSZrr:
  case X86::VPERMPSZrrkz:
  case X86::VPERMPDZ256mbi:
  case X86::VPERMPDZ256mbikz:
  case X86::VPERMPDZ256mi:
  case X86::VPERMPDZ256mikz:
  case X86::VPERMPDZ256ri:
  case X86::VPERMPDZ256rikz:
  case X86::VPERMPDZ256rm:
  case X86::VPERMPDZ256rmb:
  case X86::VPERMPDZ256rmbkz:
  case X86::VPERMPDZ256rmkz:
  case X86::VPERMPDZ256rr:
  case X86::VPERMPDZ256rrkz:
  case X86::VPERMPDZmbi:
  case X86::VPERMPDZmbikz:
  case X86::VPERMPDZmi:
  case X86::VPERMPDZmikz:
  case X86::VPERMPDZri:
  case X86::VPERMPDZrikz:
  case X86::VPERMPDZrm:
  case X86::VPERMPDZrmb:
  case X86::VPERMPDZrmbkz:
  case X86::VPERMPDZrmkz:
  case X86::VPERMPDZrr:
  case X86::VPERMPDZrrkz:
    return Subtarget.hasPERMFalseDeps();
  case X86::VRANGEPDZ128rmbi:
  case X86::VRANGEPDZ128rmbikz:
  case X86::VRANGEPDZ128rmi:
  case X86::VRANGEPDZ128rmikz:
  case X86::VRANGEPDZ128rri:
  case X86::VRANGEPDZ128rrikz:
  case X86::VRANGEPDZ256rmbi:
  case X86::VRANGEPDZ256rmbikz:
  case X86::VRANGEPDZ256rmi:
  case X86::VRANGEPDZ256rmikz:
  case X86::VRANGEPDZ256rri:
  case X86::VRANGEPDZ256rrikz:
  case X86::VRANGEPDZrmbi:
  case X86::VRANGEPDZrmbikz:
  case X86::VRANGEPDZrmi:
  case X86::VRANGEPDZrmikz:
  case X86::VRANGEPDZrri:
  case X86::VRANGEPDZrrib:
  case X86::VRANGEPDZrribkz:
  case X86::VRANGEPDZrrikz:
  case X86::VRANGEPSZ128rmbi:
  case X86::VRANGEPSZ128rmbikz:
  case X86::VRANGEPSZ128rmi:
  case X86::VRANGEPSZ128rmikz:
  case X86::VRANGEPSZ128rri:
  case X86::VRANGEPSZ128rrikz:
  case X86::VRANGEPSZ256rmbi:
  case X86::VRANGEPSZ256rmbikz:
  case X86::VRANGEPSZ256rmi:
  case X86::VRANGEPSZ256rmikz:
  case X86::VRANGEPSZ256rri:
  case X86::VRANGEPSZ256rrikz:
  case X86::VRANGEPSZrmbi:
  case X86::VRANGEPSZrmbikz:
  case X86::VRANGEPSZrmi:
  case X86::VRANGEPSZrmikz:
  case X86::VRANGEPSZrri:
  case X86::VRANGEPSZrrib:
  case X86::VRANGEPSZrribkz:
  case X86::VRANGEPSZrrikz:
  case X86::VRANGESDZrmi:
  case X86::VRANGESDZrmikz:
  case X86::VRANGESDZrri:
  case X86::VRANGESDZrrib:
  case X86::VRANGESDZrribkz:
  case X86::VRANGESDZrrikz:
  case X86::VRANGESSZrmi:
  case X86::VRANGESSZrmikz:
  case X86::VRANGESSZrri:
  case X86::VRANGESSZrrib:
  case X86::VRANGESSZrribkz:
  case X86::VRANGESSZrrikz:
    return Subtarget.hasRANGEFalseDeps();
  case X86::VGETMANTSSZrmi:
  case X86::VGETMANTSSZrmikz:
  case X86::VGETMANTSSZrri:
  case X86::VGETMANTSSZrrib:
  case X86::VGETMANTSSZrribkz:
  case X86::VGETMANTSSZrrikz:
  case X86::VGETMANTSDZrmi:
  case X86::VGETMANTSDZrmikz:
  case X86::VGETMANTSDZrri:
  case X86::VGETMANTSDZrrib:
  case X86::VGETMANTSDZrribkz:
  case X86::VGETMANTSDZrrikz:
  case X86::VGETMANTSHZrmi:
  case X86::VGETMANTSHZrmikz:
  case X86::VGETMANTSHZrri:
  case X86::VGETMANTSHZrrib:
  case X86::VGETMANTSHZrribkz:
  case X86::VGETMANTSHZrrikz:
  case X86::VGETMANTPSZ128rmbi:
  case X86::VGETMANTPSZ128rmbikz:
  case X86::VGETMANTPSZ128rmi:
  case X86::VGETMANTPSZ128rmikz:
  case X86::VGETMANTPSZ256rmbi:
  case X86::VGETMANTPSZ256rmbikz:
  case X86::VGETMANTPSZ256rmi:
  case X86::VGETMANTPSZ256rmikz:
  case X86::VGETMANTPSZrmbi:
  case X86::VGETMANTPSZrmbikz:
  case X86::VGETMANTPSZrmi:
  case X86::VGETMANTPSZrmikz:
  case X86::VGETMANTPDZ128rmbi:
  case X86::VGETMANTPDZ128rmbikz:
  case X86::VGETMANTPDZ128rmi:
  case X86::VGETMANTPDZ128rmikz:
  case X86::VGETMANTPDZ256rmbi:
  case X86::VGETMANTPDZ256rmbikz:
  case X86::VGETMANTPDZ256rmi:
  case X86::VGETMANTPDZ256rmikz:
  case X86::VGETMANTPDZrmbi:
  case X86::VGETMANTPDZrmbikz:
  case X86::VGETMANTPDZrmi:
  case X86::VGETMANTPDZrmikz:
    return Subtarget.hasGETMANTFalseDeps();
  case X86::VPMULLQZ128rm:
  case X86::VPMULLQZ128rmb:
  case X86::VPMULLQZ128rmbkz:
  case X86::VPMULLQZ128rmkz:
  case X86::VPMULLQZ128rr:
  case X86::VPMULLQZ128rrkz:
  case X86::VPMULLQZ256rm:
  case X86::VPMULLQZ256rmb:
  case X86::VPMULLQZ256rmbkz:
  case X86::VPMULLQZ256rmkz:
  case X86::VPMULLQZ256rr:
  case X86::VPMULLQZ256rrkz:
  case X86::VPMULLQZrm:
  case X86::VPMULLQZrmb:
  case X86::VPMULLQZrmbkz:
  case X86::VPMULLQZrmkz:
  case X86::VPMULLQZrr:
  case X86::VPMULLQZrrkz:
    return Subtarget.hasMULLQFalseDeps();
  // GPR
  case X86::POPCNT32rm:
  case X86::POPCNT32rr:
  case X86::POPCNT64rm:
  case X86::POPCNT64rr:
    return Subtarget.hasPOPCNTFalseDeps();
  case X86::LZCNT32rm:
  case X86::LZCNT32rr:
  case X86::LZCNT64rm:
  case X86::LZCNT64rr:
  case X86::TZCNT32rm:
  case X86::TZCNT32rr:
  case X86::TZCNT64rm:
  case X86::TZCNT64rr:
    return Subtarget.hasLZCNTFalseDeps();
  }

  return false;
}

/// Inform the BreakFalseDeps pass how many idle
/// instructions we would like before a partial register update.
unsigned X86InstrInfo::getPartialRegUpdateClearance(
    const MachineInstr &MI, unsigned OpNum,
    const TargetRegisterInfo *TRI) const {
  if (OpNum != 0 || !hasPartialRegUpdate(MI.getOpcode(), Subtarget))
    return 0;

  // If MI is marked as reading Reg, the partial register update is wanted.
  const MachineOperand &MO = MI.getOperand(0);
  Register Reg = MO.getReg();
  if (Reg.isVirtual()) {
    if (MO.readsReg() || MI.readsVirtualRegister(Reg))
      return 0;
  } else {
    if (MI.readsRegister(Reg, TRI))
      return 0;
  }

  // If any instructions in the clearance range are reading Reg, insert a
  // dependency breaking instruction, which is inexpensive and is likely to
  // be hidden in other instruction's cycles.
  return PartialRegUpdateClearance;
}

// Return true for any instruction the copies the high bits of the first source
// operand into the unused high bits of the destination operand.
// Also returns true for instructions that have two inputs where one may
// be undef and we want it to use the same register as the other input.
static bool hasUndefRegUpdate(unsigned Opcode, unsigned OpNum,
                              bool ForLoadFold = false) {
  // Set the OpNum parameter to the first source operand.
  switch (Opcode) {
  case X86::MMX_PUNPCKHBWrr:
  case X86::MMX_PUNPCKHWDrr:
  case X86::MMX_PUNPCKHDQrr:
  case X86::MMX_PUNPCKLBWrr:
  case X86::MMX_PUNPCKLWDrr:
  case X86::MMX_PUNPCKLDQrr:
  case X86::MOVHLPSrr:
  case X86::PACKSSWBrr:
  case X86::PACKUSWBrr:
  case X86::PACKSSDWrr:
  case X86::PACKUSDWrr:
  case X86::PUNPCKHBWrr:
  case X86::PUNPCKLBWrr:
  case X86::PUNPCKHWDrr:
  case X86::PUNPCKLWDrr:
  case X86::PUNPCKHDQrr:
  case X86::PUNPCKLDQrr:
  case X86::PUNPCKHQDQrr:
  case X86::PUNPCKLQDQrr:
  case X86::SHUFPDrri:
  case X86::SHUFPSrri:
    // These instructions are sometimes used with an undef first or second
    // source. Return true here so BreakFalseDeps will assign this source to the
    // same register as the first source to avoid a false dependency.
    // Operand 1 of these instructions is tied so they're separate from their
    // VEX counterparts.
    return OpNum == 2 && !ForLoadFold;

  case X86::VMOVLHPSrr:
  case X86::VMOVLHPSZrr:
  case X86::VPACKSSWBrr:
  case X86::VPACKUSWBrr:
  case X86::VPACKSSDWrr:
  case X86::VPACKUSDWrr:
  case X86::VPACKSSWBZ128rr:
  case X86::VPACKUSWBZ128rr:
  case X86::VPACKSSDWZ128rr:
  case X86::VPACKUSDWZ128rr:
  case X86::VPERM2F128rr:
  case X86::VPERM2I128rr:
  case X86::VSHUFF32X4Z256rri:
  case X86::VSHUFF32X4Zrri:
  case X86::VSHUFF64X2Z256rri:
  case X86::VSHUFF64X2Zrri:
  case X86::VSHUFI32X4Z256rri:
  case X86::VSHUFI32X4Zrri:
  case X86::VSHUFI64X2Z256rri:
  case X86::VSHUFI64X2Zrri:
  case X86::VPUNPCKHBWrr:
  case X86::VPUNPCKLBWrr:
  case X86::VPUNPCKHBWYrr:
  case X86::VPUNPCKLBWYrr:
  case X86::VPUNPCKHBWZ128rr:
  case X86::VPUNPCKLBWZ128rr:
  case X86::VPUNPCKHBWZ256rr:
  case X86::VPUNPCKLBWZ256rr:
  case X86::VPUNPCKHBWZrr:
  case X86::VPUNPCKLBWZrr:
  case X86::VPUNPCKHWDrr:
  case X86::VPUNPCKLWDrr:
  case X86::VPUNPCKHWDYrr:
  case X86::VPUNPCKLWDYrr:
  case X86::VPUNPCKHWDZ128rr:
  case X86::VPUNPCKLWDZ128rr:
  case X86::VPUNPCKHWDZ256rr:
  case X86::VPUNPCKLWDZ256rr:
  case X86::VPUNPCKHWDZrr:
  case X86::VPUNPCKLWDZrr:
  case X86::VPUNPCKHDQrr:
  case X86::VPUNPCKLDQrr:
  case X86::VPUNPCKHDQYrr:
  case X86::VPUNPCKLDQYrr:
  case X86::VPUNPCKHDQZ128rr:
  case X86::VPUNPCKLDQZ128rr:
  case X86::VPUNPCKHDQZ256rr:
  case X86::VPUNPCKLDQZ256rr:
  case X86::VPUNPCKHDQZrr:
  case X86::VPUNPCKLDQZrr:
  case X86::VPUNPCKHQDQrr:
  case X86::VPUNPCKLQDQrr:
  case X86::VPUNPCKHQDQYrr:
  case X86::VPUNPCKLQDQYrr:
  case X86::VPUNPCKHQDQZ128rr:
  case X86::VPUNPCKLQDQZ128rr:
  case X86::VPUNPCKHQDQZ256rr:
  case X86::VPUNPCKLQDQZ256rr:
  case X86::VPUNPCKHQDQZrr:
  case X86::VPUNPCKLQDQZrr:
    // These instructions are sometimes used with an undef first or second
    // source. Return true here so BreakFalseDeps will assign this source to the
    // same register as the first source to avoid a false dependency.
    return (OpNum == 1 || OpNum == 2) && !ForLoadFold;

  case X86::VCVTSI2SSrr:
  case X86::VCVTSI2SSrm:
  case X86::VCVTSI2SSrr_Int:
  case X86::VCVTSI2SSrm_Int:
  case X86::VCVTSI642SSrr:
  case X86::VCVTSI642SSrm:
  case X86::VCVTSI642SSrr_Int:
  case X86::VCVTSI642SSrm_Int:
  case X86::VCVTSI2SDrr:
  case X86::VCVTSI2SDrm:
  case X86::VCVTSI2SDrr_Int:
  case X86::VCVTSI2SDrm_Int:
  case X86::VCVTSI642SDrr:
  case X86::VCVTSI642SDrm:
  case X86::VCVTSI642SDrr_Int:
  case X86::VCVTSI642SDrm_Int:
  // AVX-512
  case X86::VCVTSI2SSZrr:
  case X86::VCVTSI2SSZrm:
  case X86::VCVTSI2SSZrr_Int:
  case X86::VCVTSI2SSZrrb_Int:
  case X86::VCVTSI2SSZrm_Int:
  case X86::VCVTSI642SSZrr:
  case X86::VCVTSI642SSZrm:
  case X86::VCVTSI642SSZrr_Int:
  case X86::VCVTSI642SSZrrb_Int:
  case X86::VCVTSI642SSZrm_Int:
  case X86::VCVTSI2SDZrr:
  case X86::VCVTSI2SDZrm:
  case X86::VCVTSI2SDZrr_Int:
  case X86::VCVTSI2SDZrm_Int:
  case X86::VCVTSI642SDZrr:
  case X86::VCVTSI642SDZrm:
  case X86::VCVTSI642SDZrr_Int:
  case X86::VCVTSI642SDZrrb_Int:
  case X86::VCVTSI642SDZrm_Int:
  case X86::VCVTUSI2SSZrr:
  case X86::VCVTUSI2SSZrm:
  case X86::VCVTUSI2SSZrr_Int:
  case X86::VCVTUSI2SSZrrb_Int:
  case X86::VCVTUSI2SSZrm_Int:
  case X86::VCVTUSI642SSZrr:
  case X86::VCVTUSI642SSZrm:
  case X86::VCVTUSI642SSZrr_Int:
  case X86::VCVTUSI642SSZrrb_Int:
  case X86::VCVTUSI642SSZrm_Int:
  case X86::VCVTUSI2SDZrr:
  case X86::VCVTUSI2SDZrm:
  case X86::VCVTUSI2SDZrr_Int:
  case X86::VCVTUSI2SDZrm_Int:
  case X86::VCVTUSI642SDZrr:
  case X86::VCVTUSI642SDZrm:
  case X86::VCVTUSI642SDZrr_Int:
  case X86::VCVTUSI642SDZrrb_Int:
  case X86::VCVTUSI642SDZrm_Int:
  case X86::VCVTSI2SHZrr:
  case X86::VCVTSI2SHZrm:
  case X86::VCVTSI2SHZrr_Int:
  case X86::VCVTSI2SHZrrb_Int:
  case X86::VCVTSI2SHZrm_Int:
  case X86::VCVTSI642SHZrr:
  case X86::VCVTSI642SHZrm:
  case X86::VCVTSI642SHZrr_Int:
  case X86::VCVTSI642SHZrrb_Int:
  case X86::VCVTSI642SHZrm_Int:
  case X86::VCVTUSI2SHZrr:
  case X86::VCVTUSI2SHZrm:
  case X86::VCVTUSI2SHZrr_Int:
  case X86::VCVTUSI2SHZrrb_Int:
  case X86::VCVTUSI2SHZrm_Int:
  case X86::VCVTUSI642SHZrr:
  case X86::VCVTUSI642SHZrm:
  case X86::VCVTUSI642SHZrr_Int:
  case X86::VCVTUSI642SHZrrb_Int:
  case X86::VCVTUSI642SHZrm_Int:
    // Load folding won't effect the undef register update since the input is
    // a GPR.
    return OpNum == 1 && !ForLoadFold;
  case X86::VCVTSD2SSrr:
  case X86::VCVTSD2SSrm:
  case X86::VCVTSD2SSrr_Int:
  case X86::VCVTSD2SSrm_Int:
  case X86::VCVTSS2SDrr:
  case X86::VCVTSS2SDrm:
  case X86::VCVTSS2SDrr_Int:
  case X86::VCVTSS2SDrm_Int:
  case X86::VRCPSSr:
  case X86::VRCPSSr_Int:
  case X86::VRCPSSm:
  case X86::VRCPSSm_Int:
  case X86::VROUNDSDri:
  case X86::VROUNDSDmi:
  case X86::VROUNDSDri_Int:
  case X86::VROUNDSDmi_Int:
  case X86::VROUNDSSri:
  case X86::VROUNDSSmi:
  case X86::VROUNDSSri_Int:
  case X86::VROUNDSSmi_Int:
  case X86::VRSQRTSSr:
  case X86::VRSQRTSSr_Int:
  case X86::VRSQRTSSm:
  case X86::VRSQRTSSm_Int:
  case X86::VSQRTSSr:
  case X86::VSQRTSSr_Int:
  case X86::VSQRTSSm:
  case X86::VSQRTSSm_Int:
  case X86::VSQRTSDr:
  case X86::VSQRTSDr_Int:
  case X86::VSQRTSDm:
  case X86::VSQRTSDm_Int:
  // AVX-512
  case X86::VCVTSD2SSZrr:
  case X86::VCVTSD2SSZrr_Int:
  case X86::VCVTSD2SSZrrb_Int:
  case X86::VCVTSD2SSZrm:
  case X86::VCVTSD2SSZrm_Int:
  case X86::VCVTSS2SDZrr:
  case X86::VCVTSS2SDZrr_Int:
  case X86::VCVTSS2SDZrrb_Int:
  case X86::VCVTSS2SDZrm:
  case X86::VCVTSS2SDZrm_Int:
  case X86::VGETEXPSDZr:
  case X86::VGETEXPSDZrb:
  case X86::VGETEXPSDZm:
  case X86::VGETEXPSSZr:
  case X86::VGETEXPSSZrb:
  case X86::VGETEXPSSZm:
  case X86::VGETMANTSDZrri:
  case X86::VGETMANTSDZrrib:
  case X86::VGETMANTSDZrmi:
  case X86::VGETMANTSSZrri:
  case X86::VGETMANTSSZrrib:
  case X86::VGETMANTSSZrmi:
  case X86::VRNDSCALESDZr:
  case X86::VRNDSCALESDZr_Int:
  case X86::VRNDSCALESDZrb_Int:
  case X86::VRNDSCALESDZm:
  case X86::VRNDSCALESDZm_Int:
  case X86::VRNDSCALESSZr:
  case X86::VRNDSCALESSZr_Int:
  case X86::VRNDSCALESSZrb_Int:
  case X86::VRNDSCALESSZm:
  case X86::VRNDSCALESSZm_Int:
  case X86::VRCP14SDZrr:
  case X86::VRCP14SDZrm:
  case X86::VRCP14SSZrr:
  case X86::VRCP14SSZrm:
  case X86::VRCPSHZrr:
  case X86::VRCPSHZrm:
  case X86::VRSQRTSHZrr:
  case X86::VRSQRTSHZrm:
  case X86::VREDUCESHZrmi:
  case X86::VREDUCESHZrri:
  case X86::VREDUCESHZrrib:
  case X86::VGETEXPSHZr:
  case X86::VGETEXPSHZrb:
  case X86::VGETEXPSHZm:
  case X86::VGETMANTSHZrri:
  case X86::VGETMANTSHZrrib:
  case X86::VGETMANTSHZrmi:
  case X86::VRNDSCALESHZr:
  case X86::VRNDSCALESHZr_Int:
  case X86::VRNDSCALESHZrb_Int:
  case X86::VRNDSCALESHZm:
  case X86::VRNDSCALESHZm_Int:
  case X86::VSQRTSHZr:
  case X86::VSQRTSHZr_Int:
  case X86::VSQRTSHZrb_Int:
  case X86::VSQRTSHZm:
  case X86::VSQRTSHZm_Int:
  case X86::VRCP28SDZr:
  case X86::VRCP28SDZrb:
  case X86::VRCP28SDZm:
  case X86::VRCP28SSZr:
  case X86::VRCP28SSZrb:
  case X86::VRCP28SSZm:
  case X86::VREDUCESSZrmi:
  case X86::VREDUCESSZrri:
  case X86::VREDUCESSZrrib:
  case X86::VRSQRT14SDZrr:
  case X86::VRSQRT14SDZrm:
  case X86::VRSQRT14SSZrr:
  case X86::VRSQRT14SSZrm:
  case X86::VRSQRT28SDZr:
  case X86::VRSQRT28SDZrb:
  case X86::VRSQRT28SDZm:
  case X86::VRSQRT28SSZr:
  case X86::VRSQRT28SSZrb:
  case X86::VRSQRT28SSZm:
  case X86::VSQRTSSZr:
  case X86::VSQRTSSZr_Int:
  case X86::VSQRTSSZrb_Int:
  case X86::VSQRTSSZm:
  case X86::VSQRTSSZm_Int:
  case X86::VSQRTSDZr:
  case X86::VSQRTSDZr_Int:
  case X86::VSQRTSDZrb_Int:
  case X86::VSQRTSDZm:
  case X86::VSQRTSDZm_Int:
  case X86::VCVTSD2SHZrr:
  case X86::VCVTSD2SHZrr_Int:
  case X86::VCVTSD2SHZrrb_Int:
  case X86::VCVTSD2SHZrm:
  case X86::VCVTSD2SHZrm_Int:
  case X86::VCVTSS2SHZrr:
  case X86::VCVTSS2SHZrr_Int:
  case X86::VCVTSS2SHZrrb_Int:
  case X86::VCVTSS2SHZrm:
  case X86::VCVTSS2SHZrm_Int:
  case X86::VCVTSH2SDZrr:
  case X86::VCVTSH2SDZrr_Int:
  case X86::VCVTSH2SDZrrb_Int:
  case X86::VCVTSH2SDZrm:
  case X86::VCVTSH2SDZrm_Int:
  case X86::VCVTSH2SSZrr:
  case X86::VCVTSH2SSZrr_Int:
  case X86::VCVTSH2SSZrrb_Int:
  case X86::VCVTSH2SSZrm:
  case X86::VCVTSH2SSZrm_Int:
    return OpNum == 1;
  case X86::VMOVSSZrrk:
  case X86::VMOVSDZrrk:
    return OpNum == 3 && !ForLoadFold;
  case X86::VMOVSSZrrkz:
  case X86::VMOVSDZrrkz:
    return OpNum == 2 && !ForLoadFold;
  }

  return false;
}

/// Inform the BreakFalseDeps pass how many idle instructions we would like
/// before certain undef register reads.
///
/// This catches the VCVTSI2SD family of instructions:
///
/// vcvtsi2sdq %rax, undef %xmm0, %xmm14
///
/// We should to be careful *not* to catch VXOR idioms which are presumably
/// handled specially in the pipeline:
///
/// vxorps undef %xmm1, undef %xmm1, %xmm1
///
/// Like getPartialRegUpdateClearance, this makes a strong assumption that the
/// high bits that are passed-through are not live.
unsigned
X86InstrInfo::getUndefRegClearance(const MachineInstr &MI, unsigned OpNum,
                                   const TargetRegisterInfo *TRI) const {
  const MachineOperand &MO = MI.getOperand(OpNum);
  if (MO.getReg().isPhysical() && hasUndefRegUpdate(MI.getOpcode(), OpNum))
    return UndefRegClearance;

  return 0;
}

void X86InstrInfo::breakPartialRegDependency(
    MachineInstr &MI, unsigned OpNum, const TargetRegisterInfo *TRI) const {
  Register Reg = MI.getOperand(OpNum).getReg();
  // If MI kills this register, the false dependence is already broken.
  if (MI.killsRegister(Reg, TRI))
    return;

  if (X86::VR128RegClass.contains(Reg)) {
    // These instructions are all floating point domain, so xorps is the best
    // choice.
    unsigned Opc = Subtarget.hasAVX() ? X86::VXORPSrr : X86::XORPSrr;
    BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), get(Opc), Reg)
        .addReg(Reg, RegState::Undef)
        .addReg(Reg, RegState::Undef);
    MI.addRegisterKilled(Reg, TRI, true);
  } else if (X86::VR256RegClass.contains(Reg)) {
    // Use vxorps to clear the full ymm register.
    // It wants to read and write the xmm sub-register.
    Register XReg = TRI->getSubReg(Reg, X86::sub_xmm);
    BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), get(X86::VXORPSrr), XReg)
        .addReg(XReg, RegState::Undef)
        .addReg(XReg, RegState::Undef)
        .addReg(Reg, RegState::ImplicitDefine);
    MI.addRegisterKilled(Reg, TRI, true);
  } else if (X86::VR128XRegClass.contains(Reg)) {
    // Only handle VLX targets.
    if (!Subtarget.hasVLX())
      return;
    // Since vxorps requires AVX512DQ, vpxord should be the best choice.
    BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), get(X86::VPXORDZ128rr), Reg)
        .addReg(Reg, RegState::Undef)
        .addReg(Reg, RegState::Undef);
    MI.addRegisterKilled(Reg, TRI, true);
  } else if (X86::VR256XRegClass.contains(Reg) ||
             X86::VR512RegClass.contains(Reg)) {
    // Only handle VLX targets.
    if (!Subtarget.hasVLX())
      return;
    // Use vpxord to clear the full ymm/zmm register.
    // It wants to read and write the xmm sub-register.
    Register XReg = TRI->getSubReg(Reg, X86::sub_xmm);
    BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), get(X86::VPXORDZ128rr), XReg)
        .addReg(XReg, RegState::Undef)
        .addReg(XReg, RegState::Undef)
        .addReg(Reg, RegState::ImplicitDefine);
    MI.addRegisterKilled(Reg, TRI, true);
  } else if (X86::GR64RegClass.contains(Reg)) {
    // Using XOR32rr because it has shorter encoding and zeros up the upper bits
    // as well.
    Register XReg = TRI->getSubReg(Reg, X86::sub_32bit);
    BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), get(X86::XOR32rr), XReg)
        .addReg(XReg, RegState::Undef)
        .addReg(XReg, RegState::Undef)
        .addReg(Reg, RegState::ImplicitDefine);
    MI.addRegisterKilled(Reg, TRI, true);
  } else if (X86::GR32RegClass.contains(Reg)) {
    BuildMI(*MI.getParent(), MI, MI.getDebugLoc(), get(X86::XOR32rr), Reg)
        .addReg(Reg, RegState::Undef)
        .addReg(Reg, RegState::Undef);
    MI.addRegisterKilled(Reg, TRI, true);
  }
}

static void addOperands(MachineInstrBuilder &MIB, ArrayRef<MachineOperand> MOs,
                        int PtrOffset = 0) {
  unsigned NumAddrOps = MOs.size();

  if (NumAddrOps < 4) {
    // FrameIndex only - add an immediate offset (whether its zero or not).
    for (unsigned i = 0; i != NumAddrOps; ++i)
      MIB.add(MOs[i]);
    addOffset(MIB, PtrOffset);
  } else {
    // General Memory Addressing - we need to add any offset to an existing
    // offset.
    assert(MOs.size() == 5 && "Unexpected memory operand list length");
    for (unsigned i = 0; i != NumAddrOps; ++i) {
      const MachineOperand &MO = MOs[i];
      if (i == 3 && PtrOffset != 0) {
        MIB.addDisp(MO, PtrOffset);
      } else {
        MIB.add(MO);
      }
    }
  }
}

static void updateOperandRegConstraints(MachineFunction &MF,
                                        MachineInstr &NewMI,
                                        const TargetInstrInfo &TII) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const TargetRegisterInfo &TRI = *MRI.getTargetRegisterInfo();

  for (int Idx : llvm::seq<int>(0, NewMI.getNumOperands())) {
    MachineOperand &MO = NewMI.getOperand(Idx);
    // We only need to update constraints on virtual register operands.
    if (!MO.isReg())
      continue;
    Register Reg = MO.getReg();
    if (!Reg.isVirtual())
      continue;

    auto *NewRC = MRI.constrainRegClass(
        Reg, TII.getRegClass(NewMI.getDesc(), Idx, &TRI, MF));
    if (!NewRC) {
      LLVM_DEBUG(
          dbgs() << "WARNING: Unable to update register constraint for operand "
                 << Idx << " of instruction:\n";
          NewMI.dump(); dbgs() << "\n");
    }
  }
}

static MachineInstr *fuseTwoAddrInst(MachineFunction &MF, unsigned Opcode,
                                     ArrayRef<MachineOperand> MOs,
                                     MachineBasicBlock::iterator InsertPt,
                                     MachineInstr &MI,
                                     const TargetInstrInfo &TII) {
  // Create the base instruction with the memory operand as the first part.
  // Omit the implicit operands, something BuildMI can't do.
  MachineInstr *NewMI =
      MF.CreateMachineInstr(TII.get(Opcode), MI.getDebugLoc(), true);
  MachineInstrBuilder MIB(MF, NewMI);
  addOperands(MIB, MOs);

  // Loop over the rest of the ri operands, converting them over.
  unsigned NumOps = MI.getDesc().getNumOperands() - 2;
  for (unsigned i = 0; i != NumOps; ++i) {
    MachineOperand &MO = MI.getOperand(i + 2);
    MIB.add(MO);
  }
  for (const MachineOperand &MO : llvm::drop_begin(MI.operands(), NumOps + 2))
    MIB.add(MO);

  updateOperandRegConstraints(MF, *NewMI, TII);

  MachineBasicBlock *MBB = InsertPt->getParent();
  MBB->insert(InsertPt, NewMI);

  return MIB;
}

static MachineInstr *fuseInst(MachineFunction &MF, unsigned Opcode,
                              unsigned OpNo, ArrayRef<MachineOperand> MOs,
                              MachineBasicBlock::iterator InsertPt,
                              MachineInstr &MI, const TargetInstrInfo &TII,
                              int PtrOffset = 0) {
  // Omit the implicit operands, something BuildMI can't do.
  MachineInstr *NewMI =
      MF.CreateMachineInstr(TII.get(Opcode), MI.getDebugLoc(), true);
  MachineInstrBuilder MIB(MF, NewMI);

  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    MachineOperand &MO = MI.getOperand(i);
    if (i == OpNo) {
      assert(MO.isReg() && "Expected to fold into reg operand!");
      addOperands(MIB, MOs, PtrOffset);
    } else {
      MIB.add(MO);
    }
  }

  updateOperandRegConstraints(MF, *NewMI, TII);

  // Copy the NoFPExcept flag from the instruction we're fusing.
  if (MI.getFlag(MachineInstr::MIFlag::NoFPExcept))
    NewMI->setFlag(MachineInstr::MIFlag::NoFPExcept);

  MachineBasicBlock *MBB = InsertPt->getParent();
  MBB->insert(InsertPt, NewMI);

  return MIB;
}

static MachineInstr *makeM0Inst(const TargetInstrInfo &TII, unsigned Opcode,
                                ArrayRef<MachineOperand> MOs,
                                MachineBasicBlock::iterator InsertPt,
                                MachineInstr &MI) {
  MachineInstrBuilder MIB = BuildMI(*InsertPt->getParent(), InsertPt,
                                    MI.getDebugLoc(), TII.get(Opcode));
  addOperands(MIB, MOs);
  return MIB.addImm(0);
}

MachineInstr *X86InstrInfo::foldMemoryOperandCustom(
    MachineFunction &MF, MachineInstr &MI, unsigned OpNum,
    ArrayRef<MachineOperand> MOs, MachineBasicBlock::iterator InsertPt,
    unsigned Size, Align Alignment) const {
  switch (MI.getOpcode()) {
  case X86::INSERTPSrr:
  case X86::VINSERTPSrr:
  case X86::VINSERTPSZrr:
    // Attempt to convert the load of inserted vector into a fold load
    // of a single float.
    if (OpNum == 2) {
      unsigned Imm = MI.getOperand(MI.getNumOperands() - 1).getImm();
      unsigned ZMask = Imm & 15;
      unsigned DstIdx = (Imm >> 4) & 3;
      unsigned SrcIdx = (Imm >> 6) & 3;

      const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
      const TargetRegisterClass *RC = getRegClass(MI.getDesc(), OpNum, &RI, MF);
      unsigned RCSize = TRI.getRegSizeInBits(*RC) / 8;
      if ((Size == 0 || Size >= 16) && RCSize >= 16 &&
          (MI.getOpcode() != X86::INSERTPSrr || Alignment >= Align(4))) {
        int PtrOffset = SrcIdx * 4;
        unsigned NewImm = (DstIdx << 4) | ZMask;
        unsigned NewOpCode =
            (MI.getOpcode() == X86::VINSERTPSZrr)  ? X86::VINSERTPSZrm
            : (MI.getOpcode() == X86::VINSERTPSrr) ? X86::VINSERTPSrm
                                                   : X86::INSERTPSrm;
        MachineInstr *NewMI =
            fuseInst(MF, NewOpCode, OpNum, MOs, InsertPt, MI, *this, PtrOffset);
        NewMI->getOperand(NewMI->getNumOperands() - 1).setImm(NewImm);
        return NewMI;
      }
    }
    break;
  case X86::MOVHLPSrr:
  case X86::VMOVHLPSrr:
  case X86::VMOVHLPSZrr:
    // Move the upper 64-bits of the second operand to the lower 64-bits.
    // To fold the load, adjust the pointer to the upper and use (V)MOVLPS.
    // TODO: In most cases AVX doesn't have a 8-byte alignment requirement.
    if (OpNum == 2) {
      const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
      const TargetRegisterClass *RC = getRegClass(MI.getDesc(), OpNum, &RI, MF);
      unsigned RCSize = TRI.getRegSizeInBits(*RC) / 8;
      if ((Size == 0 || Size >= 16) && RCSize >= 16 && Alignment >= Align(8)) {
        unsigned NewOpCode =
            (MI.getOpcode() == X86::VMOVHLPSZrr)  ? X86::VMOVLPSZ128rm
            : (MI.getOpcode() == X86::VMOVHLPSrr) ? X86::VMOVLPSrm
                                                  : X86::MOVLPSrm;
        MachineInstr *NewMI =
            fuseInst(MF, NewOpCode, OpNum, MOs, InsertPt, MI, *this, 8);
        return NewMI;
      }
    }
    break;
  case X86::UNPCKLPDrr:
    // If we won't be able to fold this to the memory form of UNPCKL, use
    // MOVHPD instead. Done as custom because we can't have this in the load
    // table twice.
    if (OpNum == 2) {
      const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
      const TargetRegisterClass *RC = getRegClass(MI.getDesc(), OpNum, &RI, MF);
      unsigned RCSize = TRI.getRegSizeInBits(*RC) / 8;
      if ((Size == 0 || Size >= 16) && RCSize >= 16 && Alignment < Align(16)) {
        MachineInstr *NewMI =
            fuseInst(MF, X86::MOVHPDrm, OpNum, MOs, InsertPt, MI, *this);
        return NewMI;
      }
    }
    break;
  case X86::MOV32r0:
    if (auto *NewMI =
            makeM0Inst(*this, (Size == 4) ? X86::MOV32mi : X86::MOV64mi32, MOs,
                       InsertPt, MI))
      return NewMI;
    break;
  }

  return nullptr;
}

static bool shouldPreventUndefRegUpdateMemFold(MachineFunction &MF,
                                               MachineInstr &MI) {
  if (!hasUndefRegUpdate(MI.getOpcode(), 1, /*ForLoadFold*/ true) ||
      !MI.getOperand(1).isReg())
    return false;

  // The are two cases we need to handle depending on where in the pipeline
  // the folding attempt is being made.
  // -Register has the undef flag set.
  // -Register is produced by the IMPLICIT_DEF instruction.

  if (MI.getOperand(1).isUndef())
    return true;

  MachineRegisterInfo &RegInfo = MF.getRegInfo();
  MachineInstr *VRegDef = RegInfo.getUniqueVRegDef(MI.getOperand(1).getReg());
  return VRegDef && VRegDef->isImplicitDef();
}

unsigned X86InstrInfo::commuteOperandsForFold(MachineInstr &MI,
                                              unsigned Idx1) const {
  unsigned Idx2 = CommuteAnyOperandIndex;
  if (!findCommutedOpIndices(MI, Idx1, Idx2))
    return Idx1;

  bool HasDef = MI.getDesc().getNumDefs();
  Register Reg0 = HasDef ? MI.getOperand(0).getReg() : Register();
  Register Reg1 = MI.getOperand(Idx1).getReg();
  Register Reg2 = MI.getOperand(Idx2).getReg();
  bool Tied1 = 0 == MI.getDesc().getOperandConstraint(Idx1, MCOI::TIED_TO);
  bool Tied2 = 0 == MI.getDesc().getOperandConstraint(Idx2, MCOI::TIED_TO);

  // If either of the commutable operands are tied to the destination
  // then we can not commute + fold.
  if ((HasDef && Reg0 == Reg1 && Tied1) || (HasDef && Reg0 == Reg2 && Tied2))
    return Idx1;

  return commuteInstruction(MI, false, Idx1, Idx2) ? Idx2 : Idx1;
}

static void printFailMsgforFold(const MachineInstr &MI, unsigned Idx) {
  if (PrintFailedFusing && !MI.isCopy())
    dbgs() << "We failed to fuse operand " << Idx << " in " << MI;
}

MachineInstr *X86InstrInfo::foldMemoryOperandImpl(
    MachineFunction &MF, MachineInstr &MI, unsigned OpNum,
    ArrayRef<MachineOperand> MOs, MachineBasicBlock::iterator InsertPt,
    unsigned Size, Align Alignment, bool AllowCommute) const {
  bool isSlowTwoMemOps = Subtarget.slowTwoMemOps();
  unsigned Opc = MI.getOpcode();

  // For CPUs that favor the register form of a call or push,
  // do not fold loads into calls or pushes, unless optimizing for size
  // aggressively.
  if (isSlowTwoMemOps && !MF.getFunction().hasMinSize() &&
      (Opc == X86::CALL32r || Opc == X86::CALL64r || Opc == X86::PUSH16r ||
       Opc == X86::PUSH32r || Opc == X86::PUSH64r))
    return nullptr;

  // Avoid partial and undef register update stalls unless optimizing for size.
  if (!MF.getFunction().hasOptSize() &&
      (hasPartialRegUpdate(Opc, Subtarget, /*ForLoadFold*/ true) ||
       shouldPreventUndefRegUpdateMemFold(MF, MI)))
    return nullptr;

  unsigned NumOps = MI.getDesc().getNumOperands();
  bool IsTwoAddr = NumOps > 1 && OpNum < 2 && MI.getOperand(0).isReg() &&
                   MI.getOperand(1).isReg() &&
                   MI.getOperand(0).getReg() == MI.getOperand(1).getReg();

  // FIXME: AsmPrinter doesn't know how to handle
  // X86II::MO_GOT_ABSOLUTE_ADDRESS after folding.
  if (Opc == X86::ADD32ri &&
      MI.getOperand(2).getTargetFlags() == X86II::MO_GOT_ABSOLUTE_ADDRESS)
    return nullptr;

  // GOTTPOFF relocation loads can only be folded into add instructions.
  // FIXME: Need to exclude other relocations that only support specific
  // instructions.
  if (MOs.size() == X86::AddrNumOperands &&
      MOs[X86::AddrDisp].getTargetFlags() == X86II::MO_GOTTPOFF &&
      Opc != X86::ADD64rr)
    return nullptr;

  // Don't fold loads into indirect calls that need a KCFI check as we'll
  // have to unfold these in X86TargetLowering::EmitKCFICheck anyway.
  if (MI.isCall() && MI.getCFIType())
    return nullptr;

  // Attempt to fold any custom cases we have.
  if (auto *CustomMI = foldMemoryOperandCustom(MF, MI, OpNum, MOs, InsertPt,
                                               Size, Alignment))
    return CustomMI;

  // Folding a memory location into the two-address part of a two-address
  // instruction is different than folding it other places.  It requires
  // replacing the *two* registers with the memory location.
  //
  // Utilize the mapping NonNDD -> RMW for the NDD variant.
  unsigned NonNDOpc = Subtarget.hasNDD() ? X86::getNonNDVariant(Opc) : 0U;
  const X86FoldTableEntry *I =
      IsTwoAddr ? lookupTwoAddrFoldTable(NonNDOpc ? NonNDOpc : Opc)
                : lookupFoldTable(Opc, OpNum);

  MachineInstr *NewMI = nullptr;
  if (I) {
    unsigned Opcode = I->DstOp;
    if (Alignment <
        Align(1ULL << ((I->Flags & TB_ALIGN_MASK) >> TB_ALIGN_SHIFT)))
      return nullptr;
    bool NarrowToMOV32rm = false;
    if (Size) {
      const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
      const TargetRegisterClass *RC = getRegClass(MI.getDesc(), OpNum, &RI, MF);
      unsigned RCSize = TRI.getRegSizeInBits(*RC) / 8;
      // Check if it's safe to fold the load. If the size of the object is
      // narrower than the load width, then it's not.
      // FIXME: Allow scalar intrinsic instructions like ADDSSrm_Int.
      if ((I->Flags & TB_FOLDED_LOAD) && Size < RCSize) {
        // If this is a 64-bit load, but the spill slot is 32, then we can do
        // a 32-bit load which is implicitly zero-extended. This likely is
        // due to live interval analysis remat'ing a load from stack slot.
        if (Opcode != X86::MOV64rm || RCSize != 8 || Size != 4)
          return nullptr;
        if (MI.getOperand(0).getSubReg() || MI.getOperand(1).getSubReg())
          return nullptr;
        Opcode = X86::MOV32rm;
        NarrowToMOV32rm = true;
      }
      // For stores, make sure the size of the object is equal to the size of
      // the store. If the object is larger, the extra bits would be garbage. If
      // the object is smaller we might overwrite another object or fault.
      if ((I->Flags & TB_FOLDED_STORE) && Size != RCSize)
        return nullptr;
    }

    NewMI = IsTwoAddr ? fuseTwoAddrInst(MF, Opcode, MOs, InsertPt, MI, *this)
                      : fuseInst(MF, Opcode, OpNum, MOs, InsertPt, MI, *this);

    if (NarrowToMOV32rm) {
      // If this is the special case where we use a MOV32rm to load a 32-bit
      // value and zero-extend the top bits. Change the destination register
      // to a 32-bit one.
      Register DstReg = NewMI->getOperand(0).getReg();
      if (DstReg.isPhysical())
        NewMI->getOperand(0).setReg(RI.getSubReg(DstReg, X86::sub_32bit));
      else
        NewMI->getOperand(0).setSubReg(X86::sub_32bit);
    }
    return NewMI;
  }

  if (AllowCommute) {
    // If the instruction and target operand are commutable, commute the
    // instruction and try again.
    unsigned CommuteOpIdx2 = commuteOperandsForFold(MI, OpNum);
    if (CommuteOpIdx2 == OpNum) {
      printFailMsgforFold(MI, OpNum);
      return nullptr;
    }
    // Attempt to fold with the commuted version of the instruction.
    NewMI = foldMemoryOperandImpl(MF, MI, CommuteOpIdx2, MOs, InsertPt, Size,
                                  Alignment, /*AllowCommute=*/false);
    if (NewMI)
      return NewMI;
    // Folding failed again - undo the commute before returning.
    commuteInstruction(MI, false, OpNum, CommuteOpIdx2);
  }

  printFailMsgforFold(MI, OpNum);
  return nullptr;
}

MachineInstr *X86InstrInfo::foldMemoryOperandImpl(
    MachineFunction &MF, MachineInstr &MI, ArrayRef<unsigned> Ops,
    MachineBasicBlock::iterator InsertPt, int FrameIndex, LiveIntervals *LIS,
    VirtRegMap *VRM) const {
  // Check switch flag
  if (NoFusing)
    return nullptr;

  // Avoid partial and undef register update stalls unless optimizing for size.
  if (!MF.getFunction().hasOptSize() &&
      (hasPartialRegUpdate(MI.getOpcode(), Subtarget, /*ForLoadFold*/ true) ||
       shouldPreventUndefRegUpdateMemFold(MF, MI)))
    return nullptr;

  // Don't fold subreg spills, or reloads that use a high subreg.
  for (auto Op : Ops) {
    MachineOperand &MO = MI.getOperand(Op);
    auto SubReg = MO.getSubReg();
    // MOV32r0 is special b/c it's used to clear a 64-bit register too.
    // (See patterns for MOV32r0 in TD files).
    if (MI.getOpcode() == X86::MOV32r0 && SubReg == X86::sub_32bit)
      continue;
    if (SubReg && (MO.isDef() || SubReg == X86::sub_8bit_hi))
      return nullptr;
  }

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  unsigned Size = MFI.getObjectSize(FrameIndex);
  Align Alignment = MFI.getObjectAlign(FrameIndex);
  // If the function stack isn't realigned we don't want to fold instructions
  // that need increased alignment.
  if (!RI.hasStackRealignment(MF))
    Alignment =
        std::min(Alignment, Subtarget.getFrameLowering()->getStackAlign());

  auto Impl = [&]() {
    return foldMemoryOperandImpl(MF, MI, Ops[0],
                                 MachineOperand::CreateFI(FrameIndex), InsertPt,
                                 Size, Alignment, /*AllowCommute=*/true);
  };
  if (Ops.size() == 2 && Ops[0] == 0 && Ops[1] == 1) {
    unsigned NewOpc = 0;
    unsigned RCSize = 0;
    unsigned Opc = MI.getOpcode();
    switch (Opc) {
    default:
      // NDD can be folded into RMW though its Op0 and Op1 are not tied.
      return (Subtarget.hasNDD() ? X86::getNonNDVariant(Opc) : 0U) ? Impl()
                                                                   : nullptr;
    case X86::TEST8rr:
      NewOpc = X86::CMP8ri;
      RCSize = 1;
      break;
    case X86::TEST16rr:
      NewOpc = X86::CMP16ri;
      RCSize = 2;
      break;
    case X86::TEST32rr:
      NewOpc = X86::CMP32ri;
      RCSize = 4;
      break;
    case X86::TEST64rr:
      NewOpc = X86::CMP64ri32;
      RCSize = 8;
      break;
    }
    // Check if it's safe to fold the load. If the size of the object is
    // narrower than the load width, then it's not.
    if (Size < RCSize)
      return nullptr;
    // Change to CMPXXri r, 0 first.
    MI.setDesc(get(NewOpc));
    MI.getOperand(1).ChangeToImmediate(0);
  } else if (Ops.size() != 1)
    return nullptr;

  return Impl();
}

/// Check if \p LoadMI is a partial register load that we can't fold into \p MI
/// because the latter uses contents that wouldn't be defined in the folded
/// version.  For instance, this transformation isn't legal:
///   movss (%rdi), %xmm0
///   addps %xmm0, %xmm0
/// ->
///   addps (%rdi), %xmm0
///
/// But this one is:
///   movss (%rdi), %xmm0
///   addss %xmm0, %xmm0
/// ->
///   addss (%rdi), %xmm0
///
static bool isNonFoldablePartialRegisterLoad(const MachineInstr &LoadMI,
                                             const MachineInstr &UserMI,
                                             const MachineFunction &MF) {
  unsigned Opc = LoadMI.getOpcode();
  unsigned UserOpc = UserMI.getOpcode();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  const TargetRegisterClass *RC =
      MF.getRegInfo().getRegClass(LoadMI.getOperand(0).getReg());
  unsigned RegSize = TRI.getRegSizeInBits(*RC);

  if ((Opc == X86::MOVSSrm || Opc == X86::VMOVSSrm || Opc == X86::VMOVSSZrm ||
       Opc == X86::MOVSSrm_alt || Opc == X86::VMOVSSrm_alt ||
       Opc == X86::VMOVSSZrm_alt) &&
      RegSize > 32) {
    // These instructions only load 32 bits, we can't fold them if the
    // destination register is wider than 32 bits (4 bytes), and its user
    // instruction isn't scalar (SS).
    switch (UserOpc) {
    case X86::CVTSS2SDrr_Int:
    case X86::VCVTSS2SDrr_Int:
    case X86::VCVTSS2SDZrr_Int:
    case X86::VCVTSS2SDZrr_Intk:
    case X86::VCVTSS2SDZrr_Intkz:
    case X86::CVTSS2SIrr_Int:
    case X86::CVTSS2SI64rr_Int:
    case X86::VCVTSS2SIrr_Int:
    case X86::VCVTSS2SI64rr_Int:
    case X86::VCVTSS2SIZrr_Int:
    case X86::VCVTSS2SI64Zrr_Int:
    case X86::CVTTSS2SIrr_Int:
    case X86::CVTTSS2SI64rr_Int:
    case X86::VCVTTSS2SIrr_Int:
    case X86::VCVTTSS2SI64rr_Int:
    case X86::VCVTTSS2SIZrr_Int:
    case X86::VCVTTSS2SI64Zrr_Int:
    case X86::VCVTSS2USIZrr_Int:
    case X86::VCVTSS2USI64Zrr_Int:
    case X86::VCVTTSS2USIZrr_Int:
    case X86::VCVTTSS2USI64Zrr_Int:
    case X86::RCPSSr_Int:
    case X86::VRCPSSr_Int:
    case X86::RSQRTSSr_Int:
    case X86::VRSQRTSSr_Int:
    case X86::ROUNDSSri_Int:
    case X86::VROUNDSSri_Int:
    case X86::COMISSrr_Int:
    case X86::VCOMISSrr_Int:
    case X86::VCOMISSZrr_Int:
    case X86::UCOMISSrr_Int:
    case X86::VUCOMISSrr_Int:
    case X86::VUCOMISSZrr_Int:
    case X86::ADDSSrr_Int:
    case X86::VADDSSrr_Int:
    case X86::VADDSSZrr_Int:
    case X86::CMPSSrri_Int:
    case X86::VCMPSSrri_Int:
    case X86::VCMPSSZrri_Int:
    case X86::DIVSSrr_Int:
    case X86::VDIVSSrr_Int:
    case X86::VDIVSSZrr_Int:
    case X86::MAXSSrr_Int:
    case X86::VMAXSSrr_Int:
    case X86::VMAXSSZrr_Int:
    case X86::MINSSrr_Int:
    case X86::VMINSSrr_Int:
    case X86::VMINSSZrr_Int:
    case X86::MULSSrr_Int:
    case X86::VMULSSrr_Int:
    case X86::VMULSSZrr_Int:
    case X86::SQRTSSr_Int:
    case X86::VSQRTSSr_Int:
    case X86::VSQRTSSZr_Int:
    case X86::SUBSSrr_Int:
    case X86::VSUBSSrr_Int:
    case X86::VSUBSSZrr_Int:
    case X86::VADDSSZrr_Intk:
    case X86::VADDSSZrr_Intkz:
    case X86::VCMPSSZrri_Intk:
    case X86::VDIVSSZrr_Intk:
    case X86::VDIVSSZrr_Intkz:
    case X86::VMAXSSZrr_Intk:
    case X86::VMAXSSZrr_Intkz:
    case X86::VMINSSZrr_Intk:
    case X86::VMINSSZrr_Intkz:
    case X86::VMULSSZrr_Intk:
    case X86::VMULSSZrr_Intkz:
    case X86::VSQRTSSZr_Intk:
    case X86::VSQRTSSZr_Intkz:
    case X86::VSUBSSZrr_Intk:
    case X86::VSUBSSZrr_Intkz:
    case X86::VFMADDSS4rr_Int:
    case X86::VFNMADDSS4rr_Int:
    case X86::VFMSUBSS4rr_Int:
    case X86::VFNMSUBSS4rr_Int:
    case X86::VFMADD132SSr_Int:
    case X86::VFNMADD132SSr_Int:
    case X86::VFMADD213SSr_Int:
    case X86::VFNMADD213SSr_Int:
    case X86::VFMADD231SSr_Int:
    case X86::VFNMADD231SSr_Int:
    case X86::VFMSUB132SSr_Int:
    case X86::VFNMSUB132SSr_Int:
    case X86::VFMSUB213SSr_Int:
    case X86::VFNMSUB213SSr_Int:
    case X86::VFMSUB231SSr_Int:
    case X86::VFNMSUB231SSr_Int:
    case X86::VFMADD132SSZr_Int:
    case X86::VFNMADD132SSZr_Int:
    case X86::VFMADD213SSZr_Int:
    case X86::VFNMADD213SSZr_Int:
    case X86::VFMADD231SSZr_Int:
    case X86::VFNMADD231SSZr_Int:
    case X86::VFMSUB132SSZr_Int:
    case X86::VFNMSUB132SSZr_Int:
    case X86::VFMSUB213SSZr_Int:
    case X86::VFNMSUB213SSZr_Int:
    case X86::VFMSUB231SSZr_Int:
    case X86::VFNMSUB231SSZr_Int:
    case X86::VFMADD132SSZr_Intk:
    case X86::VFNMADD132SSZr_Intk:
    case X86::VFMADD213SSZr_Intk:
    case X86::VFNMADD213SSZr_Intk:
    case X86::VFMADD231SSZr_Intk:
    case X86::VFNMADD231SSZr_Intk:
    case X86::VFMSUB132SSZr_Intk:
    case X86::VFNMSUB132SSZr_Intk:
    case X86::VFMSUB213SSZr_Intk:
    case X86::VFNMSUB213SSZr_Intk:
    case X86::VFMSUB231SSZr_Intk:
    case X86::VFNMSUB231SSZr_Intk:
    case X86::VFMADD132SSZr_Intkz:
    case X86::VFNMADD132SSZr_Intkz:
    case X86::VFMADD213SSZr_Intkz:
    case X86::VFNMADD213SSZr_Intkz:
    case X86::VFMADD231SSZr_Intkz:
    case X86::VFNMADD231SSZr_Intkz:
    case X86::VFMSUB132SSZr_Intkz:
    case X86::VFNMSUB132SSZr_Intkz:
    case X86::VFMSUB213SSZr_Intkz:
    case X86::VFNMSUB213SSZr_Intkz:
    case X86::VFMSUB231SSZr_Intkz:
    case X86::VFNMSUB231SSZr_Intkz:
    case X86::VFIXUPIMMSSZrri:
    case X86::VFIXUPIMMSSZrrik:
    case X86::VFIXUPIMMSSZrrikz:
    case X86::VFPCLASSSSZrr:
    case X86::VFPCLASSSSZrrk:
    case X86::VGETEXPSSZr:
    case X86::VGETEXPSSZrk:
    case X86::VGETEXPSSZrkz:
    case X86::VGETMANTSSZrri:
    case X86::VGETMANTSSZrrik:
    case X86::VGETMANTSSZrrikz:
    case X86::VRANGESSZrri:
    case X86::VRANGESSZrrik:
    case X86::VRANGESSZrrikz:
    case X86::VRCP14SSZrr:
    case X86::VRCP14SSZrrk:
    case X86::VRCP14SSZrrkz:
    case X86::VRCP28SSZr:
    case X86::VRCP28SSZrk:
    case X86::VRCP28SSZrkz:
    case X86::VREDUCESSZrri:
    case X86::VREDUCESSZrrik:
    case X86::VREDUCESSZrrikz:
    case X86::VRNDSCALESSZr_Int:
    case X86::VRNDSCALESSZr_Intk:
    case X86::VRNDSCALESSZr_Intkz:
    case X86::VRSQRT14SSZrr:
    case X86::VRSQRT14SSZrrk:
    case X86::VRSQRT14SSZrrkz:
    case X86::VRSQRT28SSZr:
    case X86::VRSQRT28SSZrk:
    case X86::VRSQRT28SSZrkz:
    case X86::VSCALEFSSZrr:
    case X86::VSCALEFSSZrrk:
    case X86::VSCALEFSSZrrkz:
      return false;
    default:
      return true;
    }
  }

  if ((Opc == X86::MOVSDrm || Opc == X86::VMOVSDrm || Opc == X86::VMOVSDZrm ||
       Opc == X86::MOVSDrm_alt || Opc == X86::VMOVSDrm_alt ||
       Opc == X86::VMOVSDZrm_alt) &&
      RegSize > 64) {
    // These instructions only load 64 bits, we can't fold them if the
    // destination register is wider than 64 bits (8 bytes), and its user
    // instruction isn't scalar (SD).
    switch (UserOpc) {
    case X86::CVTSD2SSrr_Int:
    case X86::VCVTSD2SSrr_Int:
    case X86::VCVTSD2SSZrr_Int:
    case X86::VCVTSD2SSZrr_Intk:
    case X86::VCVTSD2SSZrr_Intkz:
    case X86::CVTSD2SIrr_Int:
    case X86::CVTSD2SI64rr_Int:
    case X86::VCVTSD2SIrr_Int:
    case X86::VCVTSD2SI64rr_Int:
    case X86::VCVTSD2SIZrr_Int:
    case X86::VCVTSD2SI64Zrr_Int:
    case X86::CVTTSD2SIrr_Int:
    case X86::CVTTSD2SI64rr_Int:
    case X86::VCVTTSD2SIrr_Int:
    case X86::VCVTTSD2SI64rr_Int:
    case X86::VCVTTSD2SIZrr_Int:
    case X86::VCVTTSD2SI64Zrr_Int:
    case X86::VCVTSD2USIZrr_Int:
    case X86::VCVTSD2USI64Zrr_Int:
    case X86::VCVTTSD2USIZrr_Int:
    case X86::VCVTTSD2USI64Zrr_Int:
    case X86::ROUNDSDri_Int:
    case X86::VROUNDSDri_Int:
    case X86::COMISDrr_Int:
    case X86::VCOMISDrr_Int:
    case X86::VCOMISDZrr_Int:
    case X86::UCOMISDrr_Int:
    case X86::VUCOMISDrr_Int:
    case X86::VUCOMISDZrr_Int:
    case X86::ADDSDrr_Int:
    case X86::VADDSDrr_Int:
    case X86::VADDSDZrr_Int:
    case X86::CMPSDrri_Int:
    case X86::VCMPSDrri_Int:
    case X86::VCMPSDZrri_Int:
    case X86::DIVSDrr_Int:
    case X86::VDIVSDrr_Int:
    case X86::VDIVSDZrr_Int:
    case X86::MAXSDrr_Int:
    case X86::VMAXSDrr_Int:
    case X86::VMAXSDZrr_Int:
    case X86::MINSDrr_Int:
    case X86::VMINSDrr_Int:
    case X86::VMINSDZrr_Int:
    case X86::MULSDrr_Int:
    case X86::VMULSDrr_Int:
    case X86::VMULSDZrr_Int:
    case X86::SQRTSDr_Int:
    case X86::VSQRTSDr_Int:
    case X86::VSQRTSDZr_Int:
    case X86::SUBSDrr_Int:
    case X86::VSUBSDrr_Int:
    case X86::VSUBSDZrr_Int:
    case X86::VADDSDZrr_Intk:
    case X86::VADDSDZrr_Intkz:
    case X86::VCMPSDZrri_Intk:
    case X86::VDIVSDZrr_Intk:
    case X86::VDIVSDZrr_Intkz:
    case X86::VMAXSDZrr_Intk:
    case X86::VMAXSDZrr_Intkz:
    case X86::VMINSDZrr_Intk:
    case X86::VMINSDZrr_Intkz:
    case X86::VMULSDZrr_Intk:
    case X86::VMULSDZrr_Intkz:
    case X86::VSQRTSDZr_Intk:
    case X86::VSQRTSDZr_Intkz:
    case X86::VSUBSDZrr_Intk:
    case X86::VSUBSDZrr_Intkz:
    case X86::VFMADDSD4rr_Int:
    case X86::VFNMADDSD4rr_Int:
    case X86::VFMSUBSD4rr_Int:
    case X86::VFNMSUBSD4rr_Int:
    case X86::VFMADD132SDr_Int:
    case X86::VFNMADD132SDr_Int:
    case X86::VFMADD213SDr_Int:
    case X86::VFNMADD213SDr_Int:
    case X86::VFMADD231SDr_Int:
    case X86::VFNMADD231SDr_Int:
    case X86::VFMSUB132SDr_Int:
    case X86::VFNMSUB132SDr_Int:
    case X86::VFMSUB213SDr_Int:
    case X86::VFNMSUB213SDr_Int:
    case X86::VFMSUB231SDr_Int:
    case X86::VFNMSUB231SDr_Int:
    case X86::VFMADD132SDZr_Int:
    case X86::VFNMADD132SDZr_Int:
    case X86::VFMADD213SDZr_Int:
    case X86::VFNMADD213SDZr_Int:
    case X86::VFMADD231SDZr_Int:
    case X86::VFNMADD231SDZr_Int:
    case X86::VFMSUB132SDZr_Int:
    case X86::VFNMSUB132SDZr_Int:
    case X86::VFMSUB213SDZr_Int:
    case X86::VFNMSUB213SDZr_Int:
    case X86::VFMSUB231SDZr_Int:
    case X86::VFNMSUB231SDZr_Int:
    case X86::VFMADD132SDZr_Intk:
    case X86::VFNMADD132SDZr_Intk:
    case X86::VFMADD213SDZr_Intk:
    case X86::VFNMADD213SDZr_Intk:
    case X86::VFMADD231SDZr_Intk:
    case X86::VFNMADD231SDZr_Intk:
    case X86::VFMSUB132SDZr_Intk:
    case X86::VFNMSUB132SDZr_Intk:
    case X86::VFMSUB213SDZr_Intk:
    case X86::VFNMSUB213SDZr_Intk:
    case X86::VFMSUB231SDZr_Intk:
    case X86::VFNMSUB231SDZr_Intk:
    case X86::VFMADD132SDZr_Intkz:
    case X86::VFNMADD132SDZr_Intkz:
    case X86::VFMADD213SDZr_Intkz:
    case X86::VFNMADD213SDZr_Intkz:
    case X86::VFMADD231SDZr_Intkz:
    case X86::VFNMADD231SDZr_Intkz:
    case X86::VFMSUB132SDZr_Intkz:
    case X86::VFNMSUB132SDZr_Intkz:
    case X86::VFMSUB213SDZr_Intkz:
    case X86::VFNMSUB213SDZr_Intkz:
    case X86::VFMSUB231SDZr_Intkz:
    case X86::VFNMSUB231SDZr_Intkz:
    case X86::VFIXUPIMMSDZrri:
    case X86::VFIXUPIMMSDZrrik:
    case X86::VFIXUPIMMSDZrrikz:
    case X86::VFPCLASSSDZrr:
    case X86::VFPCLASSSDZrrk:
    case X86::VGETEXPSDZr:
    case X86::VGETEXPSDZrk:
    case X86::VGETEXPSDZrkz:
    case X86::VGETMANTSDZrri:
    case X86::VGETMANTSDZrrik:
    case X86::VGETMANTSDZrrikz:
    case X86::VRANGESDZrri:
    case X86::VRANGESDZrrik:
    case X86::VRANGESDZrrikz:
    case X86::VRCP14SDZrr:
    case X86::VRCP14SDZrrk:
    case X86::VRCP14SDZrrkz:
    case X86::VRCP28SDZr:
    case X86::VRCP28SDZrk:
    case X86::VRCP28SDZrkz:
    case X86::VREDUCESDZrri:
    case X86::VREDUCESDZrrik:
    case X86::VREDUCESDZrrikz:
    case X86::VRNDSCALESDZr_Int:
    case X86::VRNDSCALESDZr_Intk:
    case X86::VRNDSCALESDZr_Intkz:
    case X86::VRSQRT14SDZrr:
    case X86::VRSQRT14SDZrrk:
    case X86::VRSQRT14SDZrrkz:
    case X86::VRSQRT28SDZr:
    case X86::VRSQRT28SDZrk:
    case X86::VRSQRT28SDZrkz:
    case X86::VSCALEFSDZrr:
    case X86::VSCALEFSDZrrk:
    case X86::VSCALEFSDZrrkz:
      return false;
    default:
      return true;
    }
  }

  if ((Opc == X86::VMOVSHZrm || Opc == X86::VMOVSHZrm_alt) && RegSize > 16) {
    // These instructions only load 16 bits, we can't fold them if the
    // destination register is wider than 16 bits (2 bytes), and its user
    // instruction isn't scalar (SH).
    switch (UserOpc) {
    case X86::VADDSHZrr_Int:
    case X86::VCMPSHZrri_Int:
    case X86::VDIVSHZrr_Int:
    case X86::VMAXSHZrr_Int:
    case X86::VMINSHZrr_Int:
    case X86::VMULSHZrr_Int:
    case X86::VSUBSHZrr_Int:
    case X86::VADDSHZrr_Intk:
    case X86::VADDSHZrr_Intkz:
    case X86::VCMPSHZrri_Intk:
    case X86::VDIVSHZrr_Intk:
    case X86::VDIVSHZrr_Intkz:
    case X86::VMAXSHZrr_Intk:
    case X86::VMAXSHZrr_Intkz:
    case X86::VMINSHZrr_Intk:
    case X86::VMINSHZrr_Intkz:
    case X86::VMULSHZrr_Intk:
    case X86::VMULSHZrr_Intkz:
    case X86::VSUBSHZrr_Intk:
    case X86::VSUBSHZrr_Intkz:
    case X86::VFMADD132SHZr_Int:
    case X86::VFNMADD132SHZr_Int:
    case X86::VFMADD213SHZr_Int:
    case X86::VFNMADD213SHZr_Int:
    case X86::VFMADD231SHZr_Int:
    case X86::VFNMADD231SHZr_Int:
    case X86::VFMSUB132SHZr_Int:
    case X86::VFNMSUB132SHZr_Int:
    case X86::VFMSUB213SHZr_Int:
    case X86::VFNMSUB213SHZr_Int:
    case X86::VFMSUB231SHZr_Int:
    case X86::VFNMSUB231SHZr_Int:
    case X86::VFMADD132SHZr_Intk:
    case X86::VFNMADD132SHZr_Intk:
    case X86::VFMADD213SHZr_Intk:
    case X86::VFNMADD213SHZr_Intk:
    case X86::VFMADD231SHZr_Intk:
    case X86::VFNMADD231SHZr_Intk:
    case X86::VFMSUB132SHZr_Intk:
    case X86::VFNMSUB132SHZr_Intk:
    case X86::VFMSUB213SHZr_Intk:
    case X86::VFNMSUB213SHZr_Intk:
    case X86::VFMSUB231SHZr_Intk:
    case X86::VFNMSUB231SHZr_Intk:
    case X86::VFMADD132SHZr_Intkz:
    case X86::VFNMADD132SHZr_Intkz:
    case X86::VFMADD213SHZr_Intkz:
    case X86::VFNMADD213SHZr_Intkz:
    case X86::VFMADD231SHZr_Intkz:
    case X86::VFNMADD231SHZr_Intkz:
    case X86::VFMSUB132SHZr_Intkz:
    case X86::VFNMSUB132SHZr_Intkz:
    case X86::VFMSUB213SHZr_Intkz:
    case X86::VFNMSUB213SHZr_Intkz:
    case X86::VFMSUB231SHZr_Intkz:
    case X86::VFNMSUB231SHZr_Intkz:
      return false;
    default:
      return true;
    }
  }

  return false;
}

MachineInstr *X86InstrInfo::foldMemoryOperandImpl(
    MachineFunction &MF, MachineInstr &MI, ArrayRef<unsigned> Ops,
    MachineBasicBlock::iterator InsertPt, MachineInstr &LoadMI,
    LiveIntervals *LIS) const {

  // TODO: Support the case where LoadMI loads a wide register, but MI
  // only uses a subreg.
  for (auto Op : Ops) {
    if (MI.getOperand(Op).getSubReg())
      return nullptr;
  }

  // If loading from a FrameIndex, fold directly from the FrameIndex.
  unsigned NumOps = LoadMI.getDesc().getNumOperands();
  int FrameIndex;
  if (isLoadFromStackSlot(LoadMI, FrameIndex)) {
    if (isNonFoldablePartialRegisterLoad(LoadMI, MI, MF))
      return nullptr;
    return foldMemoryOperandImpl(MF, MI, Ops, InsertPt, FrameIndex, LIS);
  }

  // Check switch flag
  if (NoFusing)
    return nullptr;

  // Avoid partial and undef register update stalls unless optimizing for size.
  if (!MF.getFunction().hasOptSize() &&
      (hasPartialRegUpdate(MI.getOpcode(), Subtarget, /*ForLoadFold*/ true) ||
       shouldPreventUndefRegUpdateMemFold(MF, MI)))
    return nullptr;

  // Determine the alignment of the load.
  Align Alignment;
  unsigned LoadOpc = LoadMI.getOpcode();
  if (LoadMI.hasOneMemOperand())
    Alignment = (*LoadMI.memoperands_begin())->getAlign();
  else
    switch (LoadOpc) {
    case X86::AVX512_512_SET0:
    case X86::AVX512_512_SETALLONES:
      Alignment = Align(64);
      break;
    case X86::AVX2_SETALLONES:
    case X86::AVX1_SETALLONES:
    case X86::AVX_SET0:
    case X86::AVX512_256_SET0:
      Alignment = Align(32);
      break;
    case X86::V_SET0:
    case X86::V_SETALLONES:
    case X86::AVX512_128_SET0:
    case X86::FsFLD0F128:
    case X86::AVX512_FsFLD0F128:
      Alignment = Align(16);
      break;
    case X86::MMX_SET0:
    case X86::FsFLD0SD:
    case X86::AVX512_FsFLD0SD:
      Alignment = Align(8);
      break;
    case X86::FsFLD0SS:
    case X86::AVX512_FsFLD0SS:
      Alignment = Align(4);
      break;
    case X86::FsFLD0SH:
    case X86::AVX512_FsFLD0SH:
      Alignment = Align(2);
      break;
    default:
      return nullptr;
    }
  if (Ops.size() == 2 && Ops[0] == 0 && Ops[1] == 1) {
    unsigned NewOpc = 0;
    switch (MI.getOpcode()) {
    default:
      return nullptr;
    case X86::TEST8rr:
      NewOpc = X86::CMP8ri;
      break;
    case X86::TEST16rr:
      NewOpc = X86::CMP16ri;
      break;
    case X86::TEST32rr:
      NewOpc = X86::CMP32ri;
      break;
    case X86::TEST64rr:
      NewOpc = X86::CMP64ri32;
      break;
    }
    // Change to CMPXXri r, 0 first.
    MI.setDesc(get(NewOpc));
    MI.getOperand(1).ChangeToImmediate(0);
  } else if (Ops.size() != 1)
    return nullptr;

  // Make sure the subregisters match.
  // Otherwise we risk changing the size of the load.
  if (LoadMI.getOperand(0).getSubReg() != MI.getOperand(Ops[0]).getSubReg())
    return nullptr;

  SmallVector<MachineOperand, X86::AddrNumOperands> MOs;
  switch (LoadOpc) {
  case X86::MMX_SET0:
  case X86::V_SET0:
  case X86::V_SETALLONES:
  case X86::AVX2_SETALLONES:
  case X86::AVX1_SETALLONES:
  case X86::AVX_SET0:
  case X86::AVX512_128_SET0:
  case X86::AVX512_256_SET0:
  case X86::AVX512_512_SET0:
  case X86::AVX512_512_SETALLONES:
  case X86::FsFLD0SH:
  case X86::AVX512_FsFLD0SH:
  case X86::FsFLD0SD:
  case X86::AVX512_FsFLD0SD:
  case X86::FsFLD0SS:
  case X86::AVX512_FsFLD0SS:
  case X86::FsFLD0F128:
  case X86::AVX512_FsFLD0F128: {
    // Folding a V_SET0 or V_SETALLONES as a load, to ease register pressure.
    // Create a constant-pool entry and operands to load from it.

    // Large code model can't fold loads this way.
    if (MF.getTarget().getCodeModel() == CodeModel::Large)
      return nullptr;

    // x86-32 PIC requires a PIC base register for constant pools.
    unsigned PICBase = 0;
    // Since we're using Small or Kernel code model, we can always use
    // RIP-relative addressing for a smaller encoding.
    if (Subtarget.is64Bit()) {
      PICBase = X86::RIP;
    } else if (MF.getTarget().isPositionIndependent()) {
      // FIXME: PICBase = getGlobalBaseReg(&MF);
      // This doesn't work for several reasons.
      // 1. GlobalBaseReg may have been spilled.
      // 2. It may not be live at MI.
      return nullptr;
    }

    // Create a constant-pool entry.
    MachineConstantPool &MCP = *MF.getConstantPool();
    Type *Ty;
    bool IsAllOnes = false;
    switch (LoadOpc) {
    case X86::FsFLD0SS:
    case X86::AVX512_FsFLD0SS:
      Ty = Type::getFloatTy(MF.getFunction().getContext());
      break;
    case X86::FsFLD0SD:
    case X86::AVX512_FsFLD0SD:
      Ty = Type::getDoubleTy(MF.getFunction().getContext());
      break;
    case X86::FsFLD0F128:
    case X86::AVX512_FsFLD0F128:
      Ty = Type::getFP128Ty(MF.getFunction().getContext());
      break;
    case X86::FsFLD0SH:
    case X86::AVX512_FsFLD0SH:
      Ty = Type::getHalfTy(MF.getFunction().getContext());
      break;
    case X86::AVX512_512_SETALLONES:
      IsAllOnes = true;
      [[fallthrough]];
    case X86::AVX512_512_SET0:
      Ty = FixedVectorType::get(Type::getInt32Ty(MF.getFunction().getContext()),
                                16);
      break;
    case X86::AVX1_SETALLONES:
    case X86::AVX2_SETALLONES:
      IsAllOnes = true;
      [[fallthrough]];
    case X86::AVX512_256_SET0:
    case X86::AVX_SET0:
      Ty = FixedVectorType::get(Type::getInt32Ty(MF.getFunction().getContext()),
                                8);

      break;
    case X86::MMX_SET0:
      Ty = FixedVectorType::get(Type::getInt32Ty(MF.getFunction().getContext()),
                                2);
      break;
    case X86::V_SETALLONES:
      IsAllOnes = true;
      [[fallthrough]];
    case X86::V_SET0:
    case X86::AVX512_128_SET0:
      Ty = FixedVectorType::get(Type::getInt32Ty(MF.getFunction().getContext()),
                                4);
      break;
    }

    const Constant *C =
        IsAllOnes ? Constant::getAllOnesValue(Ty) : Constant::getNullValue(Ty);
    unsigned CPI = MCP.getConstantPoolIndex(C, Alignment);

    // Create operands to load from the constant pool entry.
    MOs.push_back(MachineOperand::CreateReg(PICBase, false));
    MOs.push_back(MachineOperand::CreateImm(1));
    MOs.push_back(MachineOperand::CreateReg(0, false));
    MOs.push_back(MachineOperand::CreateCPI(CPI, 0));
    MOs.push_back(MachineOperand::CreateReg(0, false));
    break;
  }
  case X86::VPBROADCASTBZ128rm:
  case X86::VPBROADCASTBZ256rm:
  case X86::VPBROADCASTBZrm:
  case X86::VBROADCASTF32X2Z256rm:
  case X86::VBROADCASTF32X2Zrm:
  case X86::VBROADCASTI32X2Z128rm:
  case X86::VBROADCASTI32X2Z256rm:
  case X86::VBROADCASTI32X2Zrm:
    // No instructions currently fuse with 8bits or 32bits x 2.
    return nullptr;

#define FOLD_BROADCAST(SIZE)                                                   \
  MOs.append(LoadMI.operands_begin() + NumOps - X86::AddrNumOperands,          \
             LoadMI.operands_begin() + NumOps);                                \
  return foldMemoryBroadcast(MF, MI, Ops[0], MOs, InsertPt, /*Size=*/SIZE,     \
                             /*AllowCommute=*/true);
  case X86::VPBROADCASTWZ128rm:
  case X86::VPBROADCASTWZ256rm:
  case X86::VPBROADCASTWZrm:
    FOLD_BROADCAST(16);
  case X86::VPBROADCASTDZ128rm:
  case X86::VPBROADCASTDZ256rm:
  case X86::VPBROADCASTDZrm:
  case X86::VBROADCASTSSZ128rm:
  case X86::VBROADCASTSSZ256rm:
  case X86::VBROADCASTSSZrm:
    FOLD_BROADCAST(32);
  case X86::VPBROADCASTQZ128rm:
  case X86::VPBROADCASTQZ256rm:
  case X86::VPBROADCASTQZrm:
  case X86::VBROADCASTSDZ256rm:
  case X86::VBROADCASTSDZrm:
    FOLD_BROADCAST(64);
  default: {
    if (isNonFoldablePartialRegisterLoad(LoadMI, MI, MF))
      return nullptr;

    // Folding a normal load. Just copy the load's address operands.
    MOs.append(LoadMI.operands_begin() + NumOps - X86::AddrNumOperands,
               LoadMI.operands_begin() + NumOps);
    break;
  }
  }
  return foldMemoryOperandImpl(MF, MI, Ops[0], MOs, InsertPt,
                               /*Size=*/0, Alignment, /*AllowCommute=*/true);
}

MachineInstr *
X86InstrInfo::foldMemoryBroadcast(MachineFunction &MF, MachineInstr &MI,
                                  unsigned OpNum, ArrayRef<MachineOperand> MOs,
                                  MachineBasicBlock::iterator InsertPt,
                                  unsigned BitsSize, bool AllowCommute) const {

  if (auto *I = lookupBroadcastFoldTable(MI.getOpcode(), OpNum))
    return matchBroadcastSize(*I, BitsSize)
               ? fuseInst(MF, I->DstOp, OpNum, MOs, InsertPt, MI, *this)
               : nullptr;

  if (AllowCommute) {
    // If the instruction and target operand are commutable, commute the
    // instruction and try again.
    unsigned CommuteOpIdx2 = commuteOperandsForFold(MI, OpNum);
    if (CommuteOpIdx2 == OpNum) {
      printFailMsgforFold(MI, OpNum);
      return nullptr;
    }
    MachineInstr *NewMI =
        foldMemoryBroadcast(MF, MI, CommuteOpIdx2, MOs, InsertPt, BitsSize,
                            /*AllowCommute=*/false);
    if (NewMI)
      return NewMI;
    // Folding failed again - undo the commute before returning.
    commuteInstruction(MI, false, OpNum, CommuteOpIdx2);
  }

  printFailMsgforFold(MI, OpNum);
  return nullptr;
}

static SmallVector<MachineMemOperand *, 2>
extractLoadMMOs(ArrayRef<MachineMemOperand *> MMOs, MachineFunction &MF) {
  SmallVector<MachineMemOperand *, 2> LoadMMOs;

  for (MachineMemOperand *MMO : MMOs) {
    if (!MMO->isLoad())
      continue;

    if (!MMO->isStore()) {
      // Reuse the MMO.
      LoadMMOs.push_back(MMO);
    } else {
      // Clone the MMO and unset the store flag.
      LoadMMOs.push_back(MF.getMachineMemOperand(
          MMO, MMO->getFlags() & ~MachineMemOperand::MOStore));
    }
  }

  return LoadMMOs;
}

static SmallVector<MachineMemOperand *, 2>
extractStoreMMOs(ArrayRef<MachineMemOperand *> MMOs, MachineFunction &MF) {
  SmallVector<MachineMemOperand *, 2> StoreMMOs;

  for (MachineMemOperand *MMO : MMOs) {
    if (!MMO->isStore())
      continue;

    if (!MMO->isLoad()) {
      // Reuse the MMO.
      StoreMMOs.push_back(MMO);
    } else {
      // Clone the MMO and unset the load flag.
      StoreMMOs.push_back(MF.getMachineMemOperand(
          MMO, MMO->getFlags() & ~MachineMemOperand::MOLoad));
    }
  }

  return StoreMMOs;
}

static unsigned getBroadcastOpcode(const X86FoldTableEntry *I,
                                   const TargetRegisterClass *RC,
                                   const X86Subtarget &STI) {
  assert(STI.hasAVX512() && "Expected at least AVX512!");
  unsigned SpillSize = STI.getRegisterInfo()->getSpillSize(*RC);
  assert((SpillSize == 64 || STI.hasVLX()) &&
         "Can't broadcast less than 64 bytes without AVX512VL!");

#define CASE_BCAST_TYPE_OPC(TYPE, OP16, OP32, OP64)                            \
  case TYPE:                                                                   \
    switch (SpillSize) {                                                       \
    default:                                                                   \
      llvm_unreachable("Unknown spill size");                                  \
    case 16:                                                                   \
      return X86::OP16;                                                        \
    case 32:                                                                   \
      return X86::OP32;                                                        \
    case 64:                                                                   \
      return X86::OP64;                                                        \
    }                                                                          \
    break;

  switch (I->Flags & TB_BCAST_MASK) {
  default:
    llvm_unreachable("Unexpected broadcast type!");
    CASE_BCAST_TYPE_OPC(TB_BCAST_W, VPBROADCASTWZ128rm, VPBROADCASTWZ256rm,
                        VPBROADCASTWZrm)
    CASE_BCAST_TYPE_OPC(TB_BCAST_D, VPBROADCASTDZ128rm, VPBROADCASTDZ256rm,
                        VPBROADCASTDZrm)
    CASE_BCAST_TYPE_OPC(TB_BCAST_Q, VPBROADCASTQZ128rm, VPBROADCASTQZ256rm,
                        VPBROADCASTQZrm)
    CASE_BCAST_TYPE_OPC(TB_BCAST_SH, VPBROADCASTWZ128rm, VPBROADCASTWZ256rm,
                        VPBROADCASTWZrm)
    CASE_BCAST_TYPE_OPC(TB_BCAST_SS, VBROADCASTSSZ128rm, VBROADCASTSSZ256rm,
                        VBROADCASTSSZrm)
    CASE_BCAST_TYPE_OPC(TB_BCAST_SD, VMOVDDUPZ128rm, VBROADCASTSDZ256rm,
                        VBROADCASTSDZrm)
  }
}

bool X86InstrInfo::unfoldMemoryOperand(
    MachineFunction &MF, MachineInstr &MI, unsigned Reg, bool UnfoldLoad,
    bool UnfoldStore, SmallVectorImpl<MachineInstr *> &NewMIs) const {
  const X86FoldTableEntry *I = lookupUnfoldTable(MI.getOpcode());
  if (I == nullptr)
    return false;
  unsigned Opc = I->DstOp;
  unsigned Index = I->Flags & TB_INDEX_MASK;
  bool FoldedLoad = I->Flags & TB_FOLDED_LOAD;
  bool FoldedStore = I->Flags & TB_FOLDED_STORE;
  if (UnfoldLoad && !FoldedLoad)
    return false;
  UnfoldLoad &= FoldedLoad;
  if (UnfoldStore && !FoldedStore)
    return false;
  UnfoldStore &= FoldedStore;

  const MCInstrDesc &MCID = get(Opc);

  const TargetRegisterClass *RC = getRegClass(MCID, Index, &RI, MF);
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  // TODO: Check if 32-byte or greater accesses are slow too?
  if (!MI.hasOneMemOperand() && RC == &X86::VR128RegClass &&
      Subtarget.isUnalignedMem16Slow())
    // Without memoperands, loadRegFromAddr and storeRegToStackSlot will
    // conservatively assume the address is unaligned. That's bad for
    // performance.
    return false;
  SmallVector<MachineOperand, X86::AddrNumOperands> AddrOps;
  SmallVector<MachineOperand, 2> BeforeOps;
  SmallVector<MachineOperand, 2> AfterOps;
  SmallVector<MachineOperand, 4> ImpOps;
  for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
    MachineOperand &Op = MI.getOperand(i);
    if (i >= Index && i < Index + X86::AddrNumOperands)
      AddrOps.push_back(Op);
    else if (Op.isReg() && Op.isImplicit())
      ImpOps.push_back(Op);
    else if (i < Index)
      BeforeOps.push_back(Op);
    else if (i > Index)
      AfterOps.push_back(Op);
  }

  // Emit the load or broadcast instruction.
  if (UnfoldLoad) {
    auto MMOs = extractLoadMMOs(MI.memoperands(), MF);

    unsigned Opc;
    if (I->Flags & TB_BCAST_MASK) {
      Opc = getBroadcastOpcode(I, RC, Subtarget);
    } else {
      unsigned Alignment = std::max<uint32_t>(TRI.getSpillSize(*RC), 16);
      bool isAligned = !MMOs.empty() && MMOs.front()->getAlign() >= Alignment;
      Opc = getLoadRegOpcode(Reg, RC, isAligned, Subtarget);
    }

    DebugLoc DL;
    MachineInstrBuilder MIB = BuildMI(MF, DL, get(Opc), Reg);
    for (const MachineOperand &AddrOp : AddrOps)
      MIB.add(AddrOp);
    MIB.setMemRefs(MMOs);
    NewMIs.push_back(MIB);

    if (UnfoldStore) {
      // Address operands cannot be marked isKill.
      for (unsigned i = 1; i != 1 + X86::AddrNumOperands; ++i) {
        MachineOperand &MO = NewMIs[0]->getOperand(i);
        if (MO.isReg())
          MO.setIsKill(false);
      }
    }
  }

  // Emit the data processing instruction.
  MachineInstr *DataMI = MF.CreateMachineInstr(MCID, MI.getDebugLoc(), true);
  MachineInstrBuilder MIB(MF, DataMI);

  if (FoldedStore)
    MIB.addReg(Reg, RegState::Define);
  for (MachineOperand &BeforeOp : BeforeOps)
    MIB.add(BeforeOp);
  if (FoldedLoad)
    MIB.addReg(Reg);
  for (MachineOperand &AfterOp : AfterOps)
    MIB.add(AfterOp);
  for (MachineOperand &ImpOp : ImpOps) {
    MIB.addReg(ImpOp.getReg(), getDefRegState(ImpOp.isDef()) |
                                   RegState::Implicit |
                                   getKillRegState(ImpOp.isKill()) |
                                   getDeadRegState(ImpOp.isDead()) |
                                   getUndefRegState(ImpOp.isUndef()));
  }
  // Change CMP32ri r, 0 back to TEST32rr r, r, etc.
  switch (DataMI->getOpcode()) {
  default:
    break;
  case X86::CMP64ri32:
  case X86::CMP32ri:
  case X86::CMP16ri:
  case X86::CMP8ri: {
    MachineOperand &MO0 = DataMI->getOperand(0);
    MachineOperand &MO1 = DataMI->getOperand(1);
    if (MO1.isImm() && MO1.getImm() == 0) {
      unsigned NewOpc;
      switch (DataMI->getOpcode()) {
      default:
        llvm_unreachable("Unreachable!");
      case X86::CMP64ri32:
        NewOpc = X86::TEST64rr;
        break;
      case X86::CMP32ri:
        NewOpc = X86::TEST32rr;
        break;
      case X86::CMP16ri:
        NewOpc = X86::TEST16rr;
        break;
      case X86::CMP8ri:
        NewOpc = X86::TEST8rr;
        break;
      }
      DataMI->setDesc(get(NewOpc));
      MO1.ChangeToRegister(MO0.getReg(), false);
    }
  }
  }
  NewMIs.push_back(DataMI);

  // Emit the store instruction.
  if (UnfoldStore) {
    const TargetRegisterClass *DstRC = getRegClass(MCID, 0, &RI, MF);
    auto MMOs = extractStoreMMOs(MI.memoperands(), MF);
    unsigned Alignment = std::max<uint32_t>(TRI.getSpillSize(*DstRC), 16);
    bool isAligned = !MMOs.empty() && MMOs.front()->getAlign() >= Alignment;
    unsigned Opc = getStoreRegOpcode(Reg, DstRC, isAligned, Subtarget);
    DebugLoc DL;
    MachineInstrBuilder MIB = BuildMI(MF, DL, get(Opc));
    for (const MachineOperand &AddrOp : AddrOps)
      MIB.add(AddrOp);
    MIB.addReg(Reg, RegState::Kill);
    MIB.setMemRefs(MMOs);
    NewMIs.push_back(MIB);
  }

  return true;
}

bool X86InstrInfo::unfoldMemoryOperand(
    SelectionDAG &DAG, SDNode *N, SmallVectorImpl<SDNode *> &NewNodes) const {
  if (!N->isMachineOpcode())
    return false;

  const X86FoldTableEntry *I = lookupUnfoldTable(N->getMachineOpcode());
  if (I == nullptr)
    return false;
  unsigned Opc = I->DstOp;
  unsigned Index = I->Flags & TB_INDEX_MASK;
  bool FoldedLoad = I->Flags & TB_FOLDED_LOAD;
  bool FoldedStore = I->Flags & TB_FOLDED_STORE;
  const MCInstrDesc &MCID = get(Opc);
  MachineFunction &MF = DAG.getMachineFunction();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  const TargetRegisterClass *RC = getRegClass(MCID, Index, &RI, MF);
  unsigned NumDefs = MCID.NumDefs;
  std::vector<SDValue> AddrOps;
  std::vector<SDValue> BeforeOps;
  std::vector<SDValue> AfterOps;
  SDLoc dl(N);
  unsigned NumOps = N->getNumOperands();
  for (unsigned i = 0; i != NumOps - 1; ++i) {
    SDValue Op = N->getOperand(i);
    if (i >= Index - NumDefs && i < Index - NumDefs + X86::AddrNumOperands)
      AddrOps.push_back(Op);
    else if (i < Index - NumDefs)
      BeforeOps.push_back(Op);
    else if (i > Index - NumDefs)
      AfterOps.push_back(Op);
  }
  SDValue Chain = N->getOperand(NumOps - 1);
  AddrOps.push_back(Chain);

  // Emit the load instruction.
  SDNode *Load = nullptr;
  if (FoldedLoad) {
    EVT VT = *TRI.legalclasstypes_begin(*RC);
    auto MMOs = extractLoadMMOs(cast<MachineSDNode>(N)->memoperands(), MF);
    if (MMOs.empty() && RC == &X86::VR128RegClass &&
        Subtarget.isUnalignedMem16Slow())
      // Do not introduce a slow unaligned load.
      return false;
    // FIXME: If a VR128 can have size 32, we should be checking if a 32-byte
    // memory access is slow above.

    unsigned Opc;
    if (I->Flags & TB_BCAST_MASK) {
      Opc = getBroadcastOpcode(I, RC, Subtarget);
    } else {
      unsigned Alignment = std::max<uint32_t>(TRI.getSpillSize(*RC), 16);
      bool isAligned = !MMOs.empty() && MMOs.front()->getAlign() >= Alignment;
      Opc = getLoadRegOpcode(0, RC, isAligned, Subtarget);
    }

    Load = DAG.getMachineNode(Opc, dl, VT, MVT::Other, AddrOps);
    NewNodes.push_back(Load);

    // Preserve memory reference information.
    DAG.setNodeMemRefs(cast<MachineSDNode>(Load), MMOs);
  }

  // Emit the data processing instruction.
  std::vector<EVT> VTs;
  const TargetRegisterClass *DstRC = nullptr;
  if (MCID.getNumDefs() > 0) {
    DstRC = getRegClass(MCID, 0, &RI, MF);
    VTs.push_back(*TRI.legalclasstypes_begin(*DstRC));
  }
  for (unsigned i = 0, e = N->getNumValues(); i != e; ++i) {
    EVT VT = N->getValueType(i);
    if (VT != MVT::Other && i >= (unsigned)MCID.getNumDefs())
      VTs.push_back(VT);
  }
  if (Load)
    BeforeOps.push_back(SDValue(Load, 0));
  llvm::append_range(BeforeOps, AfterOps);
  // Change CMP32ri r, 0 back to TEST32rr r, r, etc.
  switch (Opc) {
  default:
    break;
  case X86::CMP64ri32:
  case X86::CMP32ri:
  case X86::CMP16ri:
  case X86::CMP8ri:
    if (isNullConstant(BeforeOps[1])) {
      switch (Opc) {
      default:
        llvm_unreachable("Unreachable!");
      case X86::CMP64ri32:
        Opc = X86::TEST64rr;
        break;
      case X86::CMP32ri:
        Opc = X86::TEST32rr;
        break;
      case X86::CMP16ri:
        Opc = X86::TEST16rr;
        break;
      case X86::CMP8ri:
        Opc = X86::TEST8rr;
        break;
      }
      BeforeOps[1] = BeforeOps[0];
    }
  }
  SDNode *NewNode = DAG.getMachineNode(Opc, dl, VTs, BeforeOps);
  NewNodes.push_back(NewNode);

  // Emit the store instruction.
  if (FoldedStore) {
    AddrOps.pop_back();
    AddrOps.push_back(SDValue(NewNode, 0));
    AddrOps.push_back(Chain);
    auto MMOs = extractStoreMMOs(cast<MachineSDNode>(N)->memoperands(), MF);
    if (MMOs.empty() && RC == &X86::VR128RegClass &&
        Subtarget.isUnalignedMem16Slow())
      // Do not introduce a slow unaligned store.
      return false;
    // FIXME: If a VR128 can have size 32, we should be checking if a 32-byte
    // memory access is slow above.
    unsigned Alignment = std::max<uint32_t>(TRI.getSpillSize(*RC), 16);
    bool isAligned = !MMOs.empty() && MMOs.front()->getAlign() >= Alignment;
    SDNode *Store =
        DAG.getMachineNode(getStoreRegOpcode(0, DstRC, isAligned, Subtarget),
                           dl, MVT::Other, AddrOps);
    NewNodes.push_back(Store);

    // Preserve memory reference information.
    DAG.setNodeMemRefs(cast<MachineSDNode>(Store), MMOs);
  }

  return true;
}

unsigned
X86InstrInfo::getOpcodeAfterMemoryUnfold(unsigned Opc, bool UnfoldLoad,
                                         bool UnfoldStore,
                                         unsigned *LoadRegIndex) const {
  const X86FoldTableEntry *I = lookupUnfoldTable(Opc);
  if (I == nullptr)
    return 0;
  bool FoldedLoad = I->Flags & TB_FOLDED_LOAD;
  bool FoldedStore = I->Flags & TB_FOLDED_STORE;
  if (UnfoldLoad && !FoldedLoad)
    return 0;
  if (UnfoldStore && !FoldedStore)
    return 0;
  if (LoadRegIndex)
    *LoadRegIndex = I->Flags & TB_INDEX_MASK;
  return I->DstOp;
}

bool X86InstrInfo::areLoadsFromSameBasePtr(SDNode *Load1, SDNode *Load2,
                                           int64_t &Offset1,
                                           int64_t &Offset2) const {
  if (!Load1->isMachineOpcode() || !Load2->isMachineOpcode())
    return false;

  auto IsLoadOpcode = [&](unsigned Opcode) {
    switch (Opcode) {
    default:
      return false;
    case X86::MOV8rm:
    case X86::MOV16rm:
    case X86::MOV32rm:
    case X86::MOV64rm:
    case X86::LD_Fp32m:
    case X86::LD_Fp64m:
    case X86::LD_Fp80m:
    case X86::MOVSSrm:
    case X86::MOVSSrm_alt:
    case X86::MOVSDrm:
    case X86::MOVSDrm_alt:
    case X86::MMX_MOVD64rm:
    case X86::MMX_MOVQ64rm:
    case X86::MOVAPSrm:
    case X86::MOVUPSrm:
    case X86::MOVAPDrm:
    case X86::MOVUPDrm:
    case X86::MOVDQArm:
    case X86::MOVDQUrm:
    // AVX load instructions
    case X86::VMOVSSrm:
    case X86::VMOVSSrm_alt:
    case X86::VMOVSDrm:
    case X86::VMOVSDrm_alt:
    case X86::VMOVAPSrm:
    case X86::VMOVUPSrm:
    case X86::VMOVAPDrm:
    case X86::VMOVUPDrm:
    case X86::VMOVDQArm:
    case X86::VMOVDQUrm:
    case X86::VMOVAPSYrm:
    case X86::VMOVUPSYrm:
    case X86::VMOVAPDYrm:
    case X86::VMOVUPDYrm:
    case X86::VMOVDQAYrm:
    case X86::VMOVDQUYrm:
    // AVX512 load instructions
    case X86::VMOVSSZrm:
    case X86::VMOVSSZrm_alt:
    case X86::VMOVSDZrm:
    case X86::VMOVSDZrm_alt:
    case X86::VMOVAPSZ128rm:
    case X86::VMOVUPSZ128rm:
    case X86::VMOVAPSZ128rm_NOVLX:
    case X86::VMOVUPSZ128rm_NOVLX:
    case X86::VMOVAPDZ128rm:
    case X86::VMOVUPDZ128rm:
    case X86::VMOVDQU8Z128rm:
    case X86::VMOVDQU16Z128rm:
    case X86::VMOVDQA32Z128rm:
    case X86::VMOVDQU32Z128rm:
    case X86::VMOVDQA64Z128rm:
    case X86::VMOVDQU64Z128rm:
    case X86::VMOVAPSZ256rm:
    case X86::VMOVUPSZ256rm:
    case X86::VMOVAPSZ256rm_NOVLX:
    case X86::VMOVUPSZ256rm_NOVLX:
    case X86::VMOVAPDZ256rm:
    case X86::VMOVUPDZ256rm:
    case X86::VMOVDQU8Z256rm:
    case X86::VMOVDQU16Z256rm:
    case X86::VMOVDQA32Z256rm:
    case X86::VMOVDQU32Z256rm:
    case X86::VMOVDQA64Z256rm:
    case X86::VMOVDQU64Z256rm:
    case X86::VMOVAPSZrm:
    case X86::VMOVUPSZrm:
    case X86::VMOVAPDZrm:
    case X86::VMOVUPDZrm:
    case X86::VMOVDQU8Zrm:
    case X86::VMOVDQU16Zrm:
    case X86::VMOVDQA32Zrm:
    case X86::VMOVDQU32Zrm:
    case X86::VMOVDQA64Zrm:
    case X86::VMOVDQU64Zrm:
    case X86::KMOVBkm:
    case X86::KMOVBkm_EVEX:
    case X86::KMOVWkm:
    case X86::KMOVWkm_EVEX:
    case X86::KMOVDkm:
    case X86::KMOVDkm_EVEX:
    case X86::KMOVQkm:
    case X86::KMOVQkm_EVEX:
      return true;
    }
  };

  if (!IsLoadOpcode(Load1->getMachineOpcode()) ||
      !IsLoadOpcode(Load2->getMachineOpcode()))
    return false;

  // Lambda to check if both the loads have the same value for an operand index.
  auto HasSameOp = [&](int I) {
    return Load1->getOperand(I) == Load2->getOperand(I);
  };

  // All operands except the displacement should match.
  if (!HasSameOp(X86::AddrBaseReg) || !HasSameOp(X86::AddrScaleAmt) ||
      !HasSameOp(X86::AddrIndexReg) || !HasSameOp(X86::AddrSegmentReg))
    return false;

  // Chain Operand must be the same.
  if (!HasSameOp(5))
    return false;

  // Now let's examine if the displacements are constants.
  auto Disp1 = dyn_cast<ConstantSDNode>(Load1->getOperand(X86::AddrDisp));
  auto Disp2 = dyn_cast<ConstantSDNode>(Load2->getOperand(X86::AddrDisp));
  if (!Disp1 || !Disp2)
    return false;

  Offset1 = Disp1->getSExtValue();
  Offset2 = Disp2->getSExtValue();
  return true;
}

bool X86InstrInfo::shouldScheduleLoadsNear(SDNode *Load1, SDNode *Load2,
                                           int64_t Offset1, int64_t Offset2,
                                           unsigned NumLoads) const {
  assert(Offset2 > Offset1);
  if ((Offset2 - Offset1) / 8 > 64)
    return false;

  unsigned Opc1 = Load1->getMachineOpcode();
  unsigned Opc2 = Load2->getMachineOpcode();
  if (Opc1 != Opc2)
    return false; // FIXME: overly conservative?

  switch (Opc1) {
  default:
    break;
  case X86::LD_Fp32m:
  case X86::LD_Fp64m:
  case X86::LD_Fp80m:
  case X86::MMX_MOVD64rm:
  case X86::MMX_MOVQ64rm:
    return false;
  }

  EVT VT = Load1->getValueType(0);
  switch (VT.getSimpleVT().SimpleTy) {
  default:
    // XMM registers. In 64-bit mode we can be a bit more aggressive since we
    // have 16 of them to play with.
    if (Subtarget.is64Bit()) {
      if (NumLoads >= 3)
        return false;
    } else if (NumLoads) {
      return false;
    }
    break;
  case MVT::i8:
  case MVT::i16:
  case MVT::i32:
  case MVT::i64:
  case MVT::f32:
  case MVT::f64:
    if (NumLoads)
      return false;
    break;
  }

  return true;
}

bool X86InstrInfo::isSchedulingBoundary(const MachineInstr &MI,
                                        const MachineBasicBlock *MBB,
                                        const MachineFunction &MF) const {

  // ENDBR instructions should not be scheduled around.
  unsigned Opcode = MI.getOpcode();
  if (Opcode == X86::ENDBR64 || Opcode == X86::ENDBR32 ||
      Opcode == X86::PLDTILECFGV)
    return true;

  // Frame setup and destory can't be scheduled around.
  if (MI.getFlag(MachineInstr::FrameSetup) ||
      MI.getFlag(MachineInstr::FrameDestroy))
    return true;

  return TargetInstrInfo::isSchedulingBoundary(MI, MBB, MF);
}

bool X86InstrInfo::reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const {
  assert(Cond.size() == 1 && "Invalid X86 branch condition!");
  X86::CondCode CC = static_cast<X86::CondCode>(Cond[0].getImm());
  Cond[0].setImm(GetOppositeBranchCondition(CC));
  return false;
}

bool X86InstrInfo::isSafeToMoveRegClassDefs(
    const TargetRegisterClass *RC) const {
  // FIXME: Return false for x87 stack register classes for now. We can't
  // allow any loads of these registers before FpGet_ST0_80.
  return !(RC == &X86::CCRRegClass || RC == &X86::DFCCRRegClass ||
           RC == &X86::RFP32RegClass || RC == &X86::RFP64RegClass ||
           RC == &X86::RFP80RegClass);
}

/// Return a virtual register initialized with the
/// the global base register value. Output instructions required to
/// initialize the register in the function entry block, if necessary.
///
/// TODO: Eliminate this and move the code to X86MachineFunctionInfo.
///
unsigned X86InstrInfo::getGlobalBaseReg(MachineFunction *MF) const {
  X86MachineFunctionInfo *X86FI = MF->getInfo<X86MachineFunctionInfo>();
  Register GlobalBaseReg = X86FI->getGlobalBaseReg();
  if (GlobalBaseReg != 0)
    return GlobalBaseReg;

  // Create the register. The code to initialize it is inserted
  // later, by the CGBR pass (below).
  MachineRegisterInfo &RegInfo = MF->getRegInfo();
  GlobalBaseReg = RegInfo.createVirtualRegister(
      Subtarget.is64Bit() ? &X86::GR64_NOSPRegClass : &X86::GR32_NOSPRegClass);
  X86FI->setGlobalBaseReg(GlobalBaseReg);
  return GlobalBaseReg;
}

// FIXME: Some shuffle and unpack instructions have equivalents in different
// domains, but they require a bit more work than just switching opcodes.

static const uint16_t *lookup(unsigned opcode, unsigned domain,
                              ArrayRef<uint16_t[3]> Table) {
  for (const uint16_t(&Row)[3] : Table)
    if (Row[domain - 1] == opcode)
      return Row;
  return nullptr;
}

static const uint16_t *lookupAVX512(unsigned opcode, unsigned domain,
                                    ArrayRef<uint16_t[4]> Table) {
  // If this is the integer domain make sure to check both integer columns.
  for (const uint16_t(&Row)[4] : Table)
    if (Row[domain - 1] == opcode || (domain == 3 && Row[3] == opcode))
      return Row;
  return nullptr;
}

// Helper to attempt to widen/narrow blend masks.
static bool AdjustBlendMask(unsigned OldMask, unsigned OldWidth,
                            unsigned NewWidth, unsigned *pNewMask = nullptr) {
  assert(((OldWidth % NewWidth) == 0 || (NewWidth % OldWidth) == 0) &&
         "Illegal blend mask scale");
  unsigned NewMask = 0;

  if ((OldWidth % NewWidth) == 0) {
    unsigned Scale = OldWidth / NewWidth;
    unsigned SubMask = (1u << Scale) - 1;
    for (unsigned i = 0; i != NewWidth; ++i) {
      unsigned Sub = (OldMask >> (i * Scale)) & SubMask;
      if (Sub == SubMask)
        NewMask |= (1u << i);
      else if (Sub != 0x0)
        return false;
    }
  } else {
    unsigned Scale = NewWidth / OldWidth;
    unsigned SubMask = (1u << Scale) - 1;
    for (unsigned i = 0; i != OldWidth; ++i) {
      if (OldMask & (1 << i)) {
        NewMask |= (SubMask << (i * Scale));
      }
    }
  }

  if (pNewMask)
    *pNewMask = NewMask;
  return true;
}

uint16_t X86InstrInfo::getExecutionDomainCustom(const MachineInstr &MI) const {
  unsigned Opcode = MI.getOpcode();
  unsigned NumOperands = MI.getDesc().getNumOperands();

  auto GetBlendDomains = [&](unsigned ImmWidth, bool Is256) {
    uint16_t validDomains = 0;
    if (MI.getOperand(NumOperands - 1).isImm()) {
      unsigned Imm = MI.getOperand(NumOperands - 1).getImm();
      if (AdjustBlendMask(Imm, ImmWidth, Is256 ? 8 : 4))
        validDomains |= 0x2; // PackedSingle
      if (AdjustBlendMask(Imm, ImmWidth, Is256 ? 4 : 2))
        validDomains |= 0x4; // PackedDouble
      if (!Is256 || Subtarget.hasAVX2())
        validDomains |= 0x8; // PackedInt
    }
    return validDomains;
  };

  switch (Opcode) {
  case X86::BLENDPDrmi:
  case X86::BLENDPDrri:
  case X86::VBLENDPDrmi:
  case X86::VBLENDPDrri:
    return GetBlendDomains(2, false);
  case X86::VBLENDPDYrmi:
  case X86::VBLENDPDYrri:
    return GetBlendDomains(4, true);
  case X86::BLENDPSrmi:
  case X86::BLENDPSrri:
  case X86::VBLENDPSrmi:
  case X86::VBLENDPSrri:
  case X86::VPBLENDDrmi:
  case X86::VPBLENDDrri:
    return GetBlendDomains(4, false);
  case X86::VBLENDPSYrmi:
  case X86::VBLENDPSYrri:
  case X86::VPBLENDDYrmi:
  case X86::VPBLENDDYrri:
    return GetBlendDomains(8, true);
  case X86::PBLENDWrmi:
  case X86::PBLENDWrri:
  case X86::VPBLENDWrmi:
  case X86::VPBLENDWrri:
  // Treat VPBLENDWY as a 128-bit vector as it repeats the lo/hi masks.
  case X86::VPBLENDWYrmi:
  case X86::VPBLENDWYrri:
    return GetBlendDomains(8, false);
  case X86::VPANDDZ128rr:
  case X86::VPANDDZ128rm:
  case X86::VPANDDZ256rr:
  case X86::VPANDDZ256rm:
  case X86::VPANDQZ128rr:
  case X86::VPANDQZ128rm:
  case X86::VPANDQZ256rr:
  case X86::VPANDQZ256rm:
  case X86::VPANDNDZ128rr:
  case X86::VPANDNDZ128rm:
  case X86::VPANDNDZ256rr:
  case X86::VPANDNDZ256rm:
  case X86::VPANDNQZ128rr:
  case X86::VPANDNQZ128rm:
  case X86::VPANDNQZ256rr:
  case X86::VPANDNQZ256rm:
  case X86::VPORDZ128rr:
  case X86::VPORDZ128rm:
  case X86::VPORDZ256rr:
  case X86::VPORDZ256rm:
  case X86::VPORQZ128rr:
  case X86::VPORQZ128rm:
  case X86::VPORQZ256rr:
  case X86::VPORQZ256rm:
  case X86::VPXORDZ128rr:
  case X86::VPXORDZ128rm:
  case X86::VPXORDZ256rr:
  case X86::VPXORDZ256rm:
  case X86::VPXORQZ128rr:
  case X86::VPXORQZ128rm:
  case X86::VPXORQZ256rr:
  case X86::VPXORQZ256rm:
    // If we don't have DQI see if we can still switch from an EVEX integer
    // instruction to a VEX floating point instruction.
    if (Subtarget.hasDQI())
      return 0;

    if (RI.getEncodingValue(MI.getOperand(0).getReg()) >= 16)
      return 0;
    if (RI.getEncodingValue(MI.getOperand(1).getReg()) >= 16)
      return 0;
    // Register forms will have 3 operands. Memory form will have more.
    if (NumOperands == 3 &&
        RI.getEncodingValue(MI.getOperand(2).getReg()) >= 16)
      return 0;

    // All domains are valid.
    return 0xe;
  case X86::MOVHLPSrr:
    // We can swap domains when both inputs are the same register.
    // FIXME: This doesn't catch all the cases we would like. If the input
    // register isn't KILLed by the instruction, the two address instruction
    // pass puts a COPY on one input. The other input uses the original
    // register. This prevents the same physical register from being used by
    // both inputs.
    if (MI.getOperand(1).getReg() == MI.getOperand(2).getReg() &&
        MI.getOperand(0).getSubReg() == 0 &&
        MI.getOperand(1).getSubReg() == 0 && MI.getOperand(2).getSubReg() == 0)
      return 0x6;
    return 0;
  case X86::SHUFPDrri:
    return 0x6;
  }
  return 0;
}

#include "X86ReplaceableInstrs.def"

bool X86InstrInfo::setExecutionDomainCustom(MachineInstr &MI,
                                            unsigned Domain) const {
  assert(Domain > 0 && Domain < 4 && "Invalid execution domain");
  uint16_t dom = (MI.getDesc().TSFlags >> X86II::SSEDomainShift) & 3;
  assert(dom && "Not an SSE instruction");

  unsigned Opcode = MI.getOpcode();
  unsigned NumOperands = MI.getDesc().getNumOperands();

  auto SetBlendDomain = [&](unsigned ImmWidth, bool Is256) {
    if (MI.getOperand(NumOperands - 1).isImm()) {
      unsigned Imm = MI.getOperand(NumOperands - 1).getImm() & 255;
      Imm = (ImmWidth == 16 ? ((Imm << 8) | Imm) : Imm);
      unsigned NewImm = Imm;

      const uint16_t *table = lookup(Opcode, dom, ReplaceableBlendInstrs);
      if (!table)
        table = lookup(Opcode, dom, ReplaceableBlendAVX2Instrs);

      if (Domain == 1) { // PackedSingle
        AdjustBlendMask(Imm, ImmWidth, Is256 ? 8 : 4, &NewImm);
      } else if (Domain == 2) { // PackedDouble
        AdjustBlendMask(Imm, ImmWidth, Is256 ? 4 : 2, &NewImm);
      } else if (Domain == 3) { // PackedInt
        if (Subtarget.hasAVX2()) {
          // If we are already VPBLENDW use that, else use VPBLENDD.
          if ((ImmWidth / (Is256 ? 2 : 1)) != 8) {
            table = lookup(Opcode, dom, ReplaceableBlendAVX2Instrs);
            AdjustBlendMask(Imm, ImmWidth, Is256 ? 8 : 4, &NewImm);
          }
        } else {
          assert(!Is256 && "128-bit vector expected");
          AdjustBlendMask(Imm, ImmWidth, 8, &NewImm);
        }
      }

      assert(table && table[Domain - 1] && "Unknown domain op");
      MI.setDesc(get(table[Domain - 1]));
      MI.getOperand(NumOperands - 1).setImm(NewImm & 255);
    }
    return true;
  };

  switch (Opcode) {
  case X86::BLENDPDrmi:
  case X86::BLENDPDrri:
  case X86::VBLENDPDrmi:
  case X86::VBLENDPDrri:
    return SetBlendDomain(2, false);
  case X86::VBLENDPDYrmi:
  case X86::VBLENDPDYrri:
    return SetBlendDomain(4, true);
  case X86::BLENDPSrmi:
  case X86::BLENDPSrri:
  case X86::VBLENDPSrmi:
  case X86::VBLENDPSrri:
  case X86::VPBLENDDrmi:
  case X86::VPBLENDDrri:
    return SetBlendDomain(4, false);
  case X86::VBLENDPSYrmi:
  case X86::VBLENDPSYrri:
  case X86::VPBLENDDYrmi:
  case X86::VPBLENDDYrri:
    return SetBlendDomain(8, true);
  case X86::PBLENDWrmi:
  case X86::PBLENDWrri:
  case X86::VPBLENDWrmi:
  case X86::VPBLENDWrri:
    return SetBlendDomain(8, false);
  case X86::VPBLENDWYrmi:
  case X86::VPBLENDWYrri:
    return SetBlendDomain(16, true);
  case X86::VPANDDZ128rr:
  case X86::VPANDDZ128rm:
  case X86::VPANDDZ256rr:
  case X86::VPANDDZ256rm:
  case X86::VPANDQZ128rr:
  case X86::VPANDQZ128rm:
  case X86::VPANDQZ256rr:
  case X86::VPANDQZ256rm:
  case X86::VPANDNDZ128rr:
  case X86::VPANDNDZ128rm:
  case X86::VPANDNDZ256rr:
  case X86::VPANDNDZ256rm:
  case X86::VPANDNQZ128rr:
  case X86::VPANDNQZ128rm:
  case X86::VPANDNQZ256rr:
  case X86::VPANDNQZ256rm:
  case X86::VPORDZ128rr:
  case X86::VPORDZ128rm:
  case X86::VPORDZ256rr:
  case X86::VPORDZ256rm:
  case X86::VPORQZ128rr:
  case X86::VPORQZ128rm:
  case X86::VPORQZ256rr:
  case X86::VPORQZ256rm:
  case X86::VPXORDZ128rr:
  case X86::VPXORDZ128rm:
  case X86::VPXORDZ256rr:
  case X86::VPXORDZ256rm:
  case X86::VPXORQZ128rr:
  case X86::VPXORQZ128rm:
  case X86::VPXORQZ256rr:
  case X86::VPXORQZ256rm: {
    // Without DQI, convert EVEX instructions to VEX instructions.
    if (Subtarget.hasDQI())
      return false;

    const uint16_t *table =
        lookupAVX512(MI.getOpcode(), dom, ReplaceableCustomAVX512LogicInstrs);
    assert(table && "Instruction not found in table?");
    // Don't change integer Q instructions to D instructions and
    // use D intructions if we started with a PS instruction.
    if (Domain == 3 && (dom == 1 || table[3] == MI.getOpcode()))
      Domain = 4;
    MI.setDesc(get(table[Domain - 1]));
    return true;
  }
  case X86::UNPCKHPDrr:
  case X86::MOVHLPSrr:
    // We just need to commute the instruction which will switch the domains.
    if (Domain != dom && Domain != 3 &&
        MI.getOperand(1).getReg() == MI.getOperand(2).getReg() &&
        MI.getOperand(0).getSubReg() == 0 &&
        MI.getOperand(1).getSubReg() == 0 &&
        MI.getOperand(2).getSubReg() == 0) {
      commuteInstruction(MI, false);
      return true;
    }
    // We must always return true for MOVHLPSrr.
    if (Opcode == X86::MOVHLPSrr)
      return true;
    break;
  case X86::SHUFPDrri: {
    if (Domain == 1) {
      unsigned Imm = MI.getOperand(3).getImm();
      unsigned NewImm = 0x44;
      if (Imm & 1)
        NewImm |= 0x0a;
      if (Imm & 2)
        NewImm |= 0xa0;
      MI.getOperand(3).setImm(NewImm);
      MI.setDesc(get(X86::SHUFPSrri));
    }
    return true;
  }
  }
  return false;
}

std::pair<uint16_t, uint16_t>
X86InstrInfo::getExecutionDomain(const MachineInstr &MI) const {
  uint16_t domain = (MI.getDesc().TSFlags >> X86II::SSEDomainShift) & 3;
  unsigned opcode = MI.getOpcode();
  uint16_t validDomains = 0;
  if (domain) {
    // Attempt to match for custom instructions.
    validDomains = getExecutionDomainCustom(MI);
    if (validDomains)
      return std::make_pair(domain, validDomains);

    if (lookup(opcode, domain, ReplaceableInstrs)) {
      validDomains = 0xe;
    } else if (lookup(opcode, domain, ReplaceableInstrsAVX2)) {
      validDomains = Subtarget.hasAVX2() ? 0xe : 0x6;
    } else if (lookup(opcode, domain, ReplaceableInstrsFP)) {
      validDomains = 0x6;
    } else if (lookup(opcode, domain, ReplaceableInstrsAVX2InsertExtract)) {
      // Insert/extract instructions should only effect domain if AVX2
      // is enabled.
      if (!Subtarget.hasAVX2())
        return std::make_pair(0, 0);
      validDomains = 0xe;
    } else if (lookupAVX512(opcode, domain, ReplaceableInstrsAVX512)) {
      validDomains = 0xe;
    } else if (Subtarget.hasDQI() &&
               lookupAVX512(opcode, domain, ReplaceableInstrsAVX512DQ)) {
      validDomains = 0xe;
    } else if (Subtarget.hasDQI()) {
      if (const uint16_t *table =
              lookupAVX512(opcode, domain, ReplaceableInstrsAVX512DQMasked)) {
        if (domain == 1 || (domain == 3 && table[3] == opcode))
          validDomains = 0xa;
        else
          validDomains = 0xc;
      }
    }
  }
  return std::make_pair(domain, validDomains);
}

void X86InstrInfo::setExecutionDomain(MachineInstr &MI, unsigned Domain) const {
  assert(Domain > 0 && Domain < 4 && "Invalid execution domain");
  uint16_t dom = (MI.getDesc().TSFlags >> X86II::SSEDomainShift) & 3;
  assert(dom && "Not an SSE instruction");

  // Attempt to match for custom instructions.
  if (setExecutionDomainCustom(MI, Domain))
    return;

  const uint16_t *table = lookup(MI.getOpcode(), dom, ReplaceableInstrs);
  if (!table) { // try the other table
    assert((Subtarget.hasAVX2() || Domain < 3) &&
           "256-bit vector operations only available in AVX2");
    table = lookup(MI.getOpcode(), dom, ReplaceableInstrsAVX2);
  }
  if (!table) { // try the FP table
    table = lookup(MI.getOpcode(), dom, ReplaceableInstrsFP);
    assert((!table || Domain < 3) &&
           "Can only select PackedSingle or PackedDouble");
  }
  if (!table) { // try the other table
    assert(Subtarget.hasAVX2() &&
           "256-bit insert/extract only available in AVX2");
    table = lookup(MI.getOpcode(), dom, ReplaceableInstrsAVX2InsertExtract);
  }
  if (!table) { // try the AVX512 table
    assert(Subtarget.hasAVX512() && "Requires AVX-512");
    table = lookupAVX512(MI.getOpcode(), dom, ReplaceableInstrsAVX512);
    // Don't change integer Q instructions to D instructions.
    if (table && Domain == 3 && table[3] == MI.getOpcode())
      Domain = 4;
  }
  if (!table) { // try the AVX512DQ table
    assert((Subtarget.hasDQI() || Domain >= 3) && "Requires AVX-512DQ");
    table = lookupAVX512(MI.getOpcode(), dom, ReplaceableInstrsAVX512DQ);
    // Don't change integer Q instructions to D instructions and
    // use D instructions if we started with a PS instruction.
    if (table && Domain == 3 && (dom == 1 || table[3] == MI.getOpcode()))
      Domain = 4;
  }
  if (!table) { // try the AVX512DQMasked table
    assert((Subtarget.hasDQI() || Domain >= 3) && "Requires AVX-512DQ");
    table = lookupAVX512(MI.getOpcode(), dom, ReplaceableInstrsAVX512DQMasked);
    if (table && Domain == 3 && (dom == 1 || table[3] == MI.getOpcode()))
      Domain = 4;
  }
  assert(table && "Cannot change domain");
  MI.setDesc(get(table[Domain - 1]));
}

void X86InstrInfo::insertNoop(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI) const {
  DebugLoc DL;
  BuildMI(MBB, MI, DL, get(X86::NOOP));
}

/// Return the noop instruction to use for a noop.
MCInst X86InstrInfo::getNop() const {
  MCInst Nop;
  Nop.setOpcode(X86::NOOP);
  return Nop;
}

bool X86InstrInfo::isHighLatencyDef(int opc) const {
  switch (opc) {
  default:
    return false;
  case X86::DIVPDrm:
  case X86::DIVPDrr:
  case X86::DIVPSrm:
  case X86::DIVPSrr:
  case X86::DIVSDrm:
  case X86::DIVSDrm_Int:
  case X86::DIVSDrr:
  case X86::DIVSDrr_Int:
  case X86::DIVSSrm:
  case X86::DIVSSrm_Int:
  case X86::DIVSSrr:
  case X86::DIVSSrr_Int:
  case X86::SQRTPDm:
  case X86::SQRTPDr:
  case X86::SQRTPSm:
  case X86::SQRTPSr:
  case X86::SQRTSDm:
  case X86::SQRTSDm_Int:
  case X86::SQRTSDr:
  case X86::SQRTSDr_Int:
  case X86::SQRTSSm:
  case X86::SQRTSSm_Int:
  case X86::SQRTSSr:
  case X86::SQRTSSr_Int:
  // AVX instructions with high latency
  case X86::VDIVPDrm:
  case X86::VDIVPDrr:
  case X86::VDIVPDYrm:
  case X86::VDIVPDYrr:
  case X86::VDIVPSrm:
  case X86::VDIVPSrr:
  case X86::VDIVPSYrm:
  case X86::VDIVPSYrr:
  case X86::VDIVSDrm:
  case X86::VDIVSDrm_Int:
  case X86::VDIVSDrr:
  case X86::VDIVSDrr_Int:
  case X86::VDIVSSrm:
  case X86::VDIVSSrm_Int:
  case X86::VDIVSSrr:
  case X86::VDIVSSrr_Int:
  case X86::VSQRTPDm:
  case X86::VSQRTPDr:
  case X86::VSQRTPDYm:
  case X86::VSQRTPDYr:
  case X86::VSQRTPSm:
  case X86::VSQRTPSr:
  case X86::VSQRTPSYm:
  case X86::VSQRTPSYr:
  case X86::VSQRTSDm:
  case X86::VSQRTSDm_Int:
  case X86::VSQRTSDr:
  case X86::VSQRTSDr_Int:
  case X86::VSQRTSSm:
  case X86::VSQRTSSm_Int:
  case X86::VSQRTSSr:
  case X86::VSQRTSSr_Int:
  // AVX512 instructions with high latency
  case X86::VDIVPDZ128rm:
  case X86::VDIVPDZ128rmb:
  case X86::VDIVPDZ128rmbk:
  case X86::VDIVPDZ128rmbkz:
  case X86::VDIVPDZ128rmk:
  case X86::VDIVPDZ128rmkz:
  case X86::VDIVPDZ128rr:
  case X86::VDIVPDZ128rrk:
  case X86::VDIVPDZ128rrkz:
  case X86::VDIVPDZ256rm:
  case X86::VDIVPDZ256rmb:
  case X86::VDIVPDZ256rmbk:
  case X86::VDIVPDZ256rmbkz:
  case X86::VDIVPDZ256rmk:
  case X86::VDIVPDZ256rmkz:
  case X86::VDIVPDZ256rr:
  case X86::VDIVPDZ256rrk:
  case X86::VDIVPDZ256rrkz:
  case X86::VDIVPDZrrb:
  case X86::VDIVPDZrrbk:
  case X86::VDIVPDZrrbkz:
  case X86::VDIVPDZrm:
  case X86::VDIVPDZrmb:
  case X86::VDIVPDZrmbk:
  case X86::VDIVPDZrmbkz:
  case X86::VDIVPDZrmk:
  case X86::VDIVPDZrmkz:
  case X86::VDIVPDZrr:
  case X86::VDIVPDZrrk:
  case X86::VDIVPDZrrkz:
  case X86::VDIVPSZ128rm:
  case X86::VDIVPSZ128rmb:
  case X86::VDIVPSZ128rmbk:
  case X86::VDIVPSZ128rmbkz:
  case X86::VDIVPSZ128rmk:
  case X86::VDIVPSZ128rmkz:
  case X86::VDIVPSZ128rr:
  case X86::VDIVPSZ128rrk:
  case X86::VDIVPSZ128rrkz:
  case X86::VDIVPSZ256rm:
  case X86::VDIVPSZ256rmb:
  case X86::VDIVPSZ256rmbk:
  case X86::VDIVPSZ256rmbkz:
  case X86::VDIVPSZ256rmk:
  case X86::VDIVPSZ256rmkz:
  case X86::VDIVPSZ256rr:
  case X86::VDIVPSZ256rrk:
  case X86::VDIVPSZ256rrkz:
  case X86::VDIVPSZrrb:
  case X86::VDIVPSZrrbk:
  case X86::VDIVPSZrrbkz:
  case X86::VDIVPSZrm:
  case X86::VDIVPSZrmb:
  case X86::VDIVPSZrmbk:
  case X86::VDIVPSZrmbkz:
  case X86::VDIVPSZrmk:
  case X86::VDIVPSZrmkz:
  case X86::VDIVPSZrr:
  case X86::VDIVPSZrrk:
  case X86::VDIVPSZrrkz:
  case X86::VDIVSDZrm:
  case X86::VDIVSDZrr:
  case X86::VDIVSDZrm_Int:
  case X86::VDIVSDZrm_Intk:
  case X86::VDIVSDZrm_Intkz:
  case X86::VDIVSDZrr_Int:
  case X86::VDIVSDZrr_Intk:
  case X86::VDIVSDZrr_Intkz:
  case X86::VDIVSDZrrb_Int:
  case X86::VDIVSDZrrb_Intk:
  case X86::VDIVSDZrrb_Intkz:
  case X86::VDIVSSZrm:
  case X86::VDIVSSZrr:
  case X86::VDIVSSZrm_Int:
  case X86::VDIVSSZrm_Intk:
  case X86::VDIVSSZrm_Intkz:
  case X86::VDIVSSZrr_Int:
  case X86::VDIVSSZrr_Intk:
  case X86::VDIVSSZrr_Intkz:
  case X86::VDIVSSZrrb_Int:
  case X86::VDIVSSZrrb_Intk:
  case X86::VDIVSSZrrb_Intkz:
  case X86::VSQRTPDZ128m:
  case X86::VSQRTPDZ128mb:
  case X86::VSQRTPDZ128mbk:
  case X86::VSQRTPDZ128mbkz:
  case X86::VSQRTPDZ128mk:
  case X86::VSQRTPDZ128mkz:
  case X86::VSQRTPDZ128r:
  case X86::VSQRTPDZ128rk:
  case X86::VSQRTPDZ128rkz:
  case X86::VSQRTPDZ256m:
  case X86::VSQRTPDZ256mb:
  case X86::VSQRTPDZ256mbk:
  case X86::VSQRTPDZ256mbkz:
  case X86::VSQRTPDZ256mk:
  case X86::VSQRTPDZ256mkz:
  case X86::VSQRTPDZ256r:
  case X86::VSQRTPDZ256rk:
  case X86::VSQRTPDZ256rkz:
  case X86::VSQRTPDZm:
  case X86::VSQRTPDZmb:
  case X86::VSQRTPDZmbk:
  case X86::VSQRTPDZmbkz:
  case X86::VSQRTPDZmk:
  case X86::VSQRTPDZmkz:
  case X86::VSQRTPDZr:
  case X86::VSQRTPDZrb:
  case X86::VSQRTPDZrbk:
  case X86::VSQRTPDZrbkz:
  case X86::VSQRTPDZrk:
  case X86::VSQRTPDZrkz:
  case X86::VSQRTPSZ128m:
  case X86::VSQRTPSZ128mb:
  case X86::VSQRTPSZ128mbk:
  case X86::VSQRTPSZ128mbkz:
  case X86::VSQRTPSZ128mk:
  case X86::VSQRTPSZ128mkz:
  case X86::VSQRTPSZ128r:
  case X86::VSQRTPSZ128rk:
  case X86::VSQRTPSZ128rkz:
  case X86::VSQRTPSZ256m:
  case X86::VSQRTPSZ256mb:
  case X86::VSQRTPSZ256mbk:
  case X86::VSQRTPSZ256mbkz:
  case X86::VSQRTPSZ256mk:
  case X86::VSQRTPSZ256mkz:
  case X86::VSQRTPSZ256r:
  case X86::VSQRTPSZ256rk:
  case X86::VSQRTPSZ256rkz:
  case X86::VSQRTPSZm:
  case X86::VSQRTPSZmb:
  case X86::VSQRTPSZmbk:
  case X86::VSQRTPSZmbkz:
  case X86::VSQRTPSZmk:
  case X86::VSQRTPSZmkz:
  case X86::VSQRTPSZr:
  case X86::VSQRTPSZrb:
  case X86::VSQRTPSZrbk:
  case X86::VSQRTPSZrbkz:
  case X86::VSQRTPSZrk:
  case X86::VSQRTPSZrkz:
  case X86::VSQRTSDZm:
  case X86::VSQRTSDZm_Int:
  case X86::VSQRTSDZm_Intk:
  case X86::VSQRTSDZm_Intkz:
  case X86::VSQRTSDZr:
  case X86::VSQRTSDZr_Int:
  case X86::VSQRTSDZr_Intk:
  case X86::VSQRTSDZr_Intkz:
  case X86::VSQRTSDZrb_Int:
  case X86::VSQRTSDZrb_Intk:
  case X86::VSQRTSDZrb_Intkz:
  case X86::VSQRTSSZm:
  case X86::VSQRTSSZm_Int:
  case X86::VSQRTSSZm_Intk:
  case X86::VSQRTSSZm_Intkz:
  case X86::VSQRTSSZr:
  case X86::VSQRTSSZr_Int:
  case X86::VSQRTSSZr_Intk:
  case X86::VSQRTSSZr_Intkz:
  case X86::VSQRTSSZrb_Int:
  case X86::VSQRTSSZrb_Intk:
  case X86::VSQRTSSZrb_Intkz:

  case X86::VGATHERDPDYrm:
  case X86::VGATHERDPDZ128rm:
  case X86::VGATHERDPDZ256rm:
  case X86::VGATHERDPDZrm:
  case X86::VGATHERDPDrm:
  case X86::VGATHERDPSYrm:
  case X86::VGATHERDPSZ128rm:
  case X86::VGATHERDPSZ256rm:
  case X86::VGATHERDPSZrm:
  case X86::VGATHERDPSrm:
  case X86::VGATHERPF0DPDm:
  case X86::VGATHERPF0DPSm:
  case X86::VGATHERPF0QPDm:
  case X86::VGATHERPF0QPSm:
  case X86::VGATHERPF1DPDm:
  case X86::VGATHERPF1DPSm:
  case X86::VGATHERPF1QPDm:
  case X86::VGATHERPF1QPSm:
  case X86::VGATHERQPDYrm:
  case X86::VGATHERQPDZ128rm:
  case X86::VGATHERQPDZ256rm:
  case X86::VGATHERQPDZrm:
  case X86::VGATHERQPDrm:
  case X86::VGATHERQPSYrm:
  case X86::VGATHERQPSZ128rm:
  case X86::VGATHERQPSZ256rm:
  case X86::VGATHERQPSZrm:
  case X86::VGATHERQPSrm:
  case X86::VPGATHERDDYrm:
  case X86::VPGATHERDDZ128rm:
  case X86::VPGATHERDDZ256rm:
  case X86::VPGATHERDDZrm:
  case X86::VPGATHERDDrm:
  case X86::VPGATHERDQYrm:
  case X86::VPGATHERDQZ128rm:
  case X86::VPGATHERDQZ256rm:
  case X86::VPGATHERDQZrm:
  case X86::VPGATHERDQrm:
  case X86::VPGATHERQDYrm:
  case X86::VPGATHERQDZ128rm:
  case X86::VPGATHERQDZ256rm:
  case X86::VPGATHERQDZrm:
  case X86::VPGATHERQDrm:
  case X86::VPGATHERQQYrm:
  case X86::VPGATHERQQZ128rm:
  case X86::VPGATHERQQZ256rm:
  case X86::VPGATHERQQZrm:
  case X86::VPGATHERQQrm:
  case X86::VSCATTERDPDZ128mr:
  case X86::VSCATTERDPDZ256mr:
  case X86::VSCATTERDPDZmr:
  case X86::VSCATTERDPSZ128mr:
  case X86::VSCATTERDPSZ256mr:
  case X86::VSCATTERDPSZmr:
  case X86::VSCATTERPF0DPDm:
  case X86::VSCATTERPF0DPSm:
  case X86::VSCATTERPF0QPDm:
  case X86::VSCATTERPF0QPSm:
  case X86::VSCATTERPF1DPDm:
  case X86::VSCATTERPF1DPSm:
  case X86::VSCATTERPF1QPDm:
  case X86::VSCATTERPF1QPSm:
  case X86::VSCATTERQPDZ128mr:
  case X86::VSCATTERQPDZ256mr:
  case X86::VSCATTERQPDZmr:
  case X86::VSCATTERQPSZ128mr:
  case X86::VSCATTERQPSZ256mr:
  case X86::VSCATTERQPSZmr:
  case X86::VPSCATTERDDZ128mr:
  case X86::VPSCATTERDDZ256mr:
  case X86::VPSCATTERDDZmr:
  case X86::VPSCATTERDQZ128mr:
  case X86::VPSCATTERDQZ256mr:
  case X86::VPSCATTERDQZmr:
  case X86::VPSCATTERQDZ128mr:
  case X86::VPSCATTERQDZ256mr:
  case X86::VPSCATTERQDZmr:
  case X86::VPSCATTERQQZ128mr:
  case X86::VPSCATTERQQZ256mr:
  case X86::VPSCATTERQQZmr:
    return true;
  }
}

bool X86InstrInfo::hasHighOperandLatency(const TargetSchedModel &SchedModel,
                                         const MachineRegisterInfo *MRI,
                                         const MachineInstr &DefMI,
                                         unsigned DefIdx,
                                         const MachineInstr &UseMI,
                                         unsigned UseIdx) const {
  return isHighLatencyDef(DefMI.getOpcode());
}

bool X86InstrInfo::hasReassociableOperands(const MachineInstr &Inst,
                                           const MachineBasicBlock *MBB) const {
  assert(Inst.getNumExplicitOperands() == 3 && Inst.getNumExplicitDefs() == 1 &&
         Inst.getNumDefs() <= 2 && "Reassociation needs binary operators");

  // Integer binary math/logic instructions have a third source operand:
  // the EFLAGS register. That operand must be both defined here and never
  // used; ie, it must be dead. If the EFLAGS operand is live, then we can
  // not change anything because rearranging the operands could affect other
  // instructions that depend on the exact status flags (zero, sign, etc.)
  // that are set by using these particular operands with this operation.
  const MachineOperand *FlagDef =
      Inst.findRegisterDefOperand(X86::EFLAGS, /*TRI=*/nullptr);
  assert((Inst.getNumDefs() == 1 || FlagDef) && "Implicit def isn't flags?");
  if (FlagDef && !FlagDef->isDead())
    return false;

  return TargetInstrInfo::hasReassociableOperands(Inst, MBB);
}

// TODO: There are many more machine instruction opcodes to match:
//       1. Other data types (integer, vectors)
//       2. Other math / logic operations (xor, or)
//       3. Other forms of the same operation (intrinsics and other variants)
bool X86InstrInfo::isAssociativeAndCommutative(const MachineInstr &Inst,
                                               bool Invert) const {
  if (Invert)
    return false;
  switch (Inst.getOpcode()) {
  CASE_ND(ADD8rr)
  CASE_ND(ADD16rr)
  CASE_ND(ADD32rr)
  CASE_ND(ADD64rr)
  CASE_ND(AND8rr)
  CASE_ND(AND16rr)
  CASE_ND(AND32rr)
  CASE_ND(AND64rr)
  CASE_ND(OR8rr)
  CASE_ND(OR16rr)
  CASE_ND(OR32rr)
  CASE_ND(OR64rr)
  CASE_ND(XOR8rr)
  CASE_ND(XOR16rr)
  CASE_ND(XOR32rr)
  CASE_ND(XOR64rr)
  CASE_ND(IMUL16rr)
  CASE_ND(IMUL32rr)
  CASE_ND(IMUL64rr)
  case X86::PANDrr:
  case X86::PORrr:
  case X86::PXORrr:
  case X86::ANDPDrr:
  case X86::ANDPSrr:
  case X86::ORPDrr:
  case X86::ORPSrr:
  case X86::XORPDrr:
  case X86::XORPSrr:
  case X86::PADDBrr:
  case X86::PADDWrr:
  case X86::PADDDrr:
  case X86::PADDQrr:
  case X86::PMULLWrr:
  case X86::PMULLDrr:
  case X86::PMAXSBrr:
  case X86::PMAXSDrr:
  case X86::PMAXSWrr:
  case X86::PMAXUBrr:
  case X86::PMAXUDrr:
  case X86::PMAXUWrr:
  case X86::PMINSBrr:
  case X86::PMINSDrr:
  case X86::PMINSWrr:
  case X86::PMINUBrr:
  case X86::PMINUDrr:
  case X86::PMINUWrr:
  case X86::VPANDrr:
  case X86::VPANDYrr:
  case X86::VPANDDZ128rr:
  case X86::VPANDDZ256rr:
  case X86::VPANDDZrr:
  case X86::VPANDQZ128rr:
  case X86::VPANDQZ256rr:
  case X86::VPANDQZrr:
  case X86::VPORrr:
  case X86::VPORYrr:
  case X86::VPORDZ128rr:
  case X86::VPORDZ256rr:
  case X86::VPORDZrr:
  case X86::VPORQZ128rr:
  case X86::VPORQZ256rr:
  case X86::VPORQZrr:
  case X86::VPXORrr:
  case X86::VPXORYrr:
  case X86::VPXORDZ128rr:
  case X86::VPXORDZ256rr:
  case X86::VPXORDZrr:
  case X86::VPXORQZ128rr:
  case X86::VPXORQZ256rr:
  case X86::VPXORQZrr:
  case X86::VANDPDrr:
  case X86::VANDPSrr:
  case X86::VANDPDYrr:
  case X86::VANDPSYrr:
  case X86::VANDPDZ128rr:
  case X86::VANDPSZ128rr:
  case X86::VANDPDZ256rr:
  case X86::VANDPSZ256rr:
  case X86::VANDPDZrr:
  case X86::VANDPSZrr:
  case X86::VORPDrr:
  case X86::VORPSrr:
  case X86::VORPDYrr:
  case X86::VORPSYrr:
  case X86::VORPDZ128rr:
  case X86::VORPSZ128rr:
  case X86::VORPDZ256rr:
  case X86::VORPSZ256rr:
  case X86::VORPDZrr:
  case X86::VORPSZrr:
  case X86::VXORPDrr:
  case X86::VXORPSrr:
  case X86::VXORPDYrr:
  case X86::VXORPSYrr:
  case X86::VXORPDZ128rr:
  case X86::VXORPSZ128rr:
  case X86::VXORPDZ256rr:
  case X86::VXORPSZ256rr:
  case X86::VXORPDZrr:
  case X86::VXORPSZrr:
  case X86::KADDBrr:
  case X86::KADDWrr:
  case X86::KADDDrr:
  case X86::KADDQrr:
  case X86::KANDBrr:
  case X86::KANDWrr:
  case X86::KANDDrr:
  case X86::KANDQrr:
  case X86::KORBrr:
  case X86::KORWrr:
  case X86::KORDrr:
  case X86::KORQrr:
  case X86::KXORBrr:
  case X86::KXORWrr:
  case X86::KXORDrr:
  case X86::KXORQrr:
  case X86::VPADDBrr:
  case X86::VPADDWrr:
  case X86::VPADDDrr:
  case X86::VPADDQrr:
  case X86::VPADDBYrr:
  case X86::VPADDWYrr:
  case X86::VPADDDYrr:
  case X86::VPADDQYrr:
  case X86::VPADDBZ128rr:
  case X86::VPADDWZ128rr:
  case X86::VPADDDZ128rr:
  case X86::VPADDQZ128rr:
  case X86::VPADDBZ256rr:
  case X86::VPADDWZ256rr:
  case X86::VPADDDZ256rr:
  case X86::VPADDQZ256rr:
  case X86::VPADDBZrr:
  case X86::VPADDWZrr:
  case X86::VPADDDZrr:
  case X86::VPADDQZrr:
  case X86::VPMULLWrr:
  case X86::VPMULLWYrr:
  case X86::VPMULLWZ128rr:
  case X86::VPMULLWZ256rr:
  case X86::VPMULLWZrr:
  case X86::VPMULLDrr:
  case X86::VPMULLDYrr:
  case X86::VPMULLDZ128rr:
  case X86::VPMULLDZ256rr:
  case X86::VPMULLDZrr:
  case X86::VPMULLQZ128rr:
  case X86::VPMULLQZ256rr:
  case X86::VPMULLQZrr:
  case X86::VPMAXSBrr:
  case X86::VPMAXSBYrr:
  case X86::VPMAXSBZ128rr:
  case X86::VPMAXSBZ256rr:
  case X86::VPMAXSBZrr:
  case X86::VPMAXSDrr:
  case X86::VPMAXSDYrr:
  case X86::VPMAXSDZ128rr:
  case X86::VPMAXSDZ256rr:
  case X86::VPMAXSDZrr:
  case X86::VPMAXSQZ128rr:
  case X86::VPMAXSQZ256rr:
  case X86::VPMAXSQZrr:
  case X86::VPMAXSWrr:
  case X86::VPMAXSWYrr:
  case X86::VPMAXSWZ128rr:
  case X86::VPMAXSWZ256rr:
  case X86::VPMAXSWZrr:
  case X86::VPMAXUBrr:
  case X86::VPMAXUBYrr:
  case X86::VPMAXUBZ128rr:
  case X86::VPMAXUBZ256rr:
  case X86::VPMAXUBZrr:
  case X86::VPMAXUDrr:
  case X86::VPMAXUDYrr:
  case X86::VPMAXUDZ128rr:
  case X86::VPMAXUDZ256rr:
  case X86::VPMAXUDZrr:
  case X86::VPMAXUQZ128rr:
  case X86::VPMAXUQZ256rr:
  case X86::VPMAXUQZrr:
  case X86::VPMAXUWrr:
  case X86::VPMAXUWYrr:
  case X86::VPMAXUWZ128rr:
  case X86::VPMAXUWZ256rr:
  case X86::VPMAXUWZrr:
  case X86::VPMINSBrr:
  case X86::VPMINSBYrr:
  case X86::VPMINSBZ128rr:
  case X86::VPMINSBZ256rr:
  case X86::VPMINSBZrr:
  case X86::VPMINSDrr:
  case X86::VPMINSDYrr:
  case X86::VPMINSDZ128rr:
  case X86::VPMINSDZ256rr:
  case X86::VPMINSDZrr:
  case X86::VPMINSQZ128rr:
  case X86::VPMINSQZ256rr:
  case X86::VPMINSQZrr:
  case X86::VPMINSWrr:
  case X86::VPMINSWYrr:
  case X86::VPMINSWZ128rr:
  case X86::VPMINSWZ256rr:
  case X86::VPMINSWZrr:
  case X86::VPMINUBrr:
  case X86::VPMINUBYrr:
  case X86::VPMINUBZ128rr:
  case X86::VPMINUBZ256rr:
  case X86::VPMINUBZrr:
  case X86::VPMINUDrr:
  case X86::VPMINUDYrr:
  case X86::VPMINUDZ128rr:
  case X86::VPMINUDZ256rr:
  case X86::VPMINUDZrr:
  case X86::VPMINUQZ128rr:
  case X86::VPMINUQZ256rr:
  case X86::VPMINUQZrr:
  case X86::VPMINUWrr:
  case X86::VPMINUWYrr:
  case X86::VPMINUWZ128rr:
  case X86::VPMINUWZ256rr:
  case X86::VPMINUWZrr:
  // Normal min/max instructions are not commutative because of NaN and signed
  // zero semantics, but these are. Thus, there's no need to check for global
  // relaxed math; the instructions themselves have the properties we need.
  case X86::MAXCPDrr:
  case X86::MAXCPSrr:
  case X86::MAXCSDrr:
  case X86::MAXCSSrr:
  case X86::MINCPDrr:
  case X86::MINCPSrr:
  case X86::MINCSDrr:
  case X86::MINCSSrr:
  case X86::VMAXCPDrr:
  case X86::VMAXCPSrr:
  case X86::VMAXCPDYrr:
  case X86::VMAXCPSYrr:
  case X86::VMAXCPDZ128rr:
  case X86::VMAXCPSZ128rr:
  case X86::VMAXCPDZ256rr:
  case X86::VMAXCPSZ256rr:
  case X86::VMAXCPDZrr:
  case X86::VMAXCPSZrr:
  case X86::VMAXCSDrr:
  case X86::VMAXCSSrr:
  case X86::VMAXCSDZrr:
  case X86::VMAXCSSZrr:
  case X86::VMINCPDrr:
  case X86::VMINCPSrr:
  case X86::VMINCPDYrr:
  case X86::VMINCPSYrr:
  case X86::VMINCPDZ128rr:
  case X86::VMINCPSZ128rr:
  case X86::VMINCPDZ256rr:
  case X86::VMINCPSZ256rr:
  case X86::VMINCPDZrr:
  case X86::VMINCPSZrr:
  case X86::VMINCSDrr:
  case X86::VMINCSSrr:
  case X86::VMINCSDZrr:
  case X86::VMINCSSZrr:
  case X86::VMAXCPHZ128rr:
  case X86::VMAXCPHZ256rr:
  case X86::VMAXCPHZrr:
  case X86::VMAXCSHZrr:
  case X86::VMINCPHZ128rr:
  case X86::VMINCPHZ256rr:
  case X86::VMINCPHZrr:
  case X86::VMINCSHZrr:
    return true;
  case X86::ADDPDrr:
  case X86::ADDPSrr:
  case X86::ADDSDrr:
  case X86::ADDSSrr:
  case X86::MULPDrr:
  case X86::MULPSrr:
  case X86::MULSDrr:
  case X86::MULSSrr:
  case X86::VADDPDrr:
  case X86::VADDPSrr:
  case X86::VADDPDYrr:
  case X86::VADDPSYrr:
  case X86::VADDPDZ128rr:
  case X86::VADDPSZ128rr:
  case X86::VADDPDZ256rr:
  case X86::VADDPSZ256rr:
  case X86::VADDPDZrr:
  case X86::VADDPSZrr:
  case X86::VADDSDrr:
  case X86::VADDSSrr:
  case X86::VADDSDZrr:
  case X86::VADDSSZrr:
  case X86::VMULPDrr:
  case X86::VMULPSrr:
  case X86::VMULPDYrr:
  case X86::VMULPSYrr:
  case X86::VMULPDZ128rr:
  case X86::VMULPSZ128rr:
  case X86::VMULPDZ256rr:
  case X86::VMULPSZ256rr:
  case X86::VMULPDZrr:
  case X86::VMULPSZrr:
  case X86::VMULSDrr:
  case X86::VMULSSrr:
  case X86::VMULSDZrr:
  case X86::VMULSSZrr:
  case X86::VADDPHZ128rr:
  case X86::VADDPHZ256rr:
  case X86::VADDPHZrr:
  case X86::VADDSHZrr:
  case X86::VMULPHZ128rr:
  case X86::VMULPHZ256rr:
  case X86::VMULPHZrr:
  case X86::VMULSHZrr:
    return Inst.getFlag(MachineInstr::MIFlag::FmReassoc) &&
           Inst.getFlag(MachineInstr::MIFlag::FmNsz);
  default:
    return false;
  }
}

/// If \p DescribedReg overlaps with the MOVrr instruction's destination
/// register then, if possible, describe the value in terms of the source
/// register.
static std::optional<ParamLoadedValue>
describeMOVrrLoadedValue(const MachineInstr &MI, Register DescribedReg,
                         const TargetRegisterInfo *TRI) {
  Register DestReg = MI.getOperand(0).getReg();
  Register SrcReg = MI.getOperand(1).getReg();

  auto Expr = DIExpression::get(MI.getMF()->getFunction().getContext(), {});

  // If the described register is the destination, just return the source.
  if (DestReg == DescribedReg)
    return ParamLoadedValue(MachineOperand::CreateReg(SrcReg, false), Expr);

  // If the described register is a sub-register of the destination register,
  // then pick out the source register's corresponding sub-register.
  if (unsigned SubRegIdx = TRI->getSubRegIndex(DestReg, DescribedReg)) {
    Register SrcSubReg = TRI->getSubReg(SrcReg, SubRegIdx);
    return ParamLoadedValue(MachineOperand::CreateReg(SrcSubReg, false), Expr);
  }

  // The remaining case to consider is when the described register is a
  // super-register of the destination register. MOV8rr and MOV16rr does not
  // write to any of the other bytes in the register, meaning that we'd have to
  // describe the value using a combination of the source register and the
  // non-overlapping bits in the described register, which is not currently
  // possible.
  if (MI.getOpcode() == X86::MOV8rr || MI.getOpcode() == X86::MOV16rr ||
      !TRI->isSuperRegister(DestReg, DescribedReg))
    return std::nullopt;

  assert(MI.getOpcode() == X86::MOV32rr && "Unexpected super-register case");
  return ParamLoadedValue(MachineOperand::CreateReg(SrcReg, false), Expr);
}

std::optional<ParamLoadedValue>
X86InstrInfo::describeLoadedValue(const MachineInstr &MI, Register Reg) const {
  const MachineOperand *Op = nullptr;
  DIExpression *Expr = nullptr;

  const TargetRegisterInfo *TRI = &getRegisterInfo();

  switch (MI.getOpcode()) {
  case X86::LEA32r:
  case X86::LEA64r:
  case X86::LEA64_32r: {
    // We may need to describe a 64-bit parameter with a 32-bit LEA.
    if (!TRI->isSuperRegisterEq(MI.getOperand(0).getReg(), Reg))
      return std::nullopt;

    // Operand 4 could be global address. For now we do not support
    // such situation.
    if (!MI.getOperand(4).isImm() || !MI.getOperand(2).isImm())
      return std::nullopt;

    const MachineOperand &Op1 = MI.getOperand(1);
    const MachineOperand &Op2 = MI.getOperand(3);
    assert(Op2.isReg() &&
           (Op2.getReg() == X86::NoRegister || Op2.getReg().isPhysical()));

    // Omit situations like:
    // %rsi = lea %rsi, 4, ...
    if ((Op1.isReg() && Op1.getReg() == MI.getOperand(0).getReg()) ||
        Op2.getReg() == MI.getOperand(0).getReg())
      return std::nullopt;
    else if ((Op1.isReg() && Op1.getReg() != X86::NoRegister &&
              TRI->regsOverlap(Op1.getReg(), MI.getOperand(0).getReg())) ||
             (Op2.getReg() != X86::NoRegister &&
              TRI->regsOverlap(Op2.getReg(), MI.getOperand(0).getReg())))
      return std::nullopt;

    int64_t Coef = MI.getOperand(2).getImm();
    int64_t Offset = MI.getOperand(4).getImm();
    SmallVector<uint64_t, 8> Ops;

    if ((Op1.isReg() && Op1.getReg() != X86::NoRegister)) {
      Op = &Op1;
    } else if (Op1.isFI())
      Op = &Op1;

    if (Op && Op->isReg() && Op->getReg() == Op2.getReg() && Coef > 0) {
      Ops.push_back(dwarf::DW_OP_constu);
      Ops.push_back(Coef + 1);
      Ops.push_back(dwarf::DW_OP_mul);
    } else {
      if (Op && Op2.getReg() != X86::NoRegister) {
        int dwarfReg = TRI->getDwarfRegNum(Op2.getReg(), false);
        if (dwarfReg < 0)
          return std::nullopt;
        else if (dwarfReg < 32) {
          Ops.push_back(dwarf::DW_OP_breg0 + dwarfReg);
          Ops.push_back(0);
        } else {
          Ops.push_back(dwarf::DW_OP_bregx);
          Ops.push_back(dwarfReg);
          Ops.push_back(0);
        }
      } else if (!Op) {
        assert(Op2.getReg() != X86::NoRegister);
        Op = &Op2;
      }

      if (Coef > 1) {
        assert(Op2.getReg() != X86::NoRegister);
        Ops.push_back(dwarf::DW_OP_constu);
        Ops.push_back(Coef);
        Ops.push_back(dwarf::DW_OP_mul);
      }

      if (((Op1.isReg() && Op1.getReg() != X86::NoRegister) || Op1.isFI()) &&
          Op2.getReg() != X86::NoRegister) {
        Ops.push_back(dwarf::DW_OP_plus);
      }
    }

    DIExpression::appendOffset(Ops, Offset);
    Expr = DIExpression::get(MI.getMF()->getFunction().getContext(), Ops);

    return ParamLoadedValue(*Op, Expr);
  }
  case X86::MOV8ri:
  case X86::MOV16ri:
    // TODO: Handle MOV8ri and MOV16ri.
    return std::nullopt;
  case X86::MOV32ri:
  case X86::MOV64ri:
  case X86::MOV64ri32:
    // MOV32ri may be used for producing zero-extended 32-bit immediates in
    // 64-bit parameters, so we need to consider super-registers.
    if (!TRI->isSuperRegisterEq(MI.getOperand(0).getReg(), Reg))
      return std::nullopt;
    return ParamLoadedValue(MI.getOperand(1), Expr);
  case X86::MOV8rr:
  case X86::MOV16rr:
  case X86::MOV32rr:
  case X86::MOV64rr:
    return describeMOVrrLoadedValue(MI, Reg, TRI);
  case X86::XOR32rr: {
    // 64-bit parameters are zero-materialized using XOR32rr, so also consider
    // super-registers.
    if (!TRI->isSuperRegisterEq(MI.getOperand(0).getReg(), Reg))
      return std::nullopt;
    if (MI.getOperand(1).getReg() == MI.getOperand(2).getReg())
      return ParamLoadedValue(MachineOperand::CreateImm(0), Expr);
    return std::nullopt;
  }
  case X86::MOVSX64rr32: {
    // We may need to describe the lower 32 bits of the MOVSX; for example, in
    // cases like this:
    //
    //  $ebx = [...]
    //  $rdi = MOVSX64rr32 $ebx
    //  $esi = MOV32rr $edi
    if (!TRI->isSubRegisterEq(MI.getOperand(0).getReg(), Reg))
      return std::nullopt;

    Expr = DIExpression::get(MI.getMF()->getFunction().getContext(), {});

    // If the described register is the destination register we need to
    // sign-extend the source register from 32 bits. The other case we handle
    // is when the described register is the 32-bit sub-register of the
    // destination register, in case we just need to return the source
    // register.
    if (Reg == MI.getOperand(0).getReg())
      Expr = DIExpression::appendExt(Expr, 32, 64, true);
    else
      assert(X86MCRegisterClasses[X86::GR32RegClassID].contains(Reg) &&
             "Unhandled sub-register case for MOVSX64rr32");

    return ParamLoadedValue(MI.getOperand(1), Expr);
  }
  default:
    assert(!MI.isMoveImmediate() && "Unexpected MoveImm instruction");
    return TargetInstrInfo::describeLoadedValue(MI, Reg);
  }
}

/// This is an architecture-specific helper function of reassociateOps.
/// Set special operand attributes for new instructions after reassociation.
void X86InstrInfo::setSpecialOperandAttr(MachineInstr &OldMI1,
                                         MachineInstr &OldMI2,
                                         MachineInstr &NewMI1,
                                         MachineInstr &NewMI2) const {
  // Integer instructions may define an implicit EFLAGS dest register operand.
  MachineOperand *OldFlagDef1 =
      OldMI1.findRegisterDefOperand(X86::EFLAGS, /*TRI=*/nullptr);
  MachineOperand *OldFlagDef2 =
      OldMI2.findRegisterDefOperand(X86::EFLAGS, /*TRI=*/nullptr);

  assert(!OldFlagDef1 == !OldFlagDef2 &&
         "Unexpected instruction type for reassociation");

  if (!OldFlagDef1 || !OldFlagDef2)
    return;

  assert(OldFlagDef1->isDead() && OldFlagDef2->isDead() &&
         "Must have dead EFLAGS operand in reassociable instruction");

  MachineOperand *NewFlagDef1 =
      NewMI1.findRegisterDefOperand(X86::EFLAGS, /*TRI=*/nullptr);
  MachineOperand *NewFlagDef2 =
      NewMI2.findRegisterDefOperand(X86::EFLAGS, /*TRI=*/nullptr);

  assert(NewFlagDef1 && NewFlagDef2 &&
         "Unexpected operand in reassociable instruction");

  // Mark the new EFLAGS operands as dead to be helpful to subsequent iterations
  // of this pass or other passes. The EFLAGS operands must be dead in these new
  // instructions because the EFLAGS operands in the original instructions must
  // be dead in order for reassociation to occur.
  NewFlagDef1->setIsDead();
  NewFlagDef2->setIsDead();
}

std::pair<unsigned, unsigned>
X86InstrInfo::decomposeMachineOperandsTargetFlags(unsigned TF) const {
  return std::make_pair(TF, 0u);
}

ArrayRef<std::pair<unsigned, const char *>>
X86InstrInfo::getSerializableDirectMachineOperandTargetFlags() const {
  using namespace X86II;
  static const std::pair<unsigned, const char *> TargetFlags[] = {
      {MO_GOT_ABSOLUTE_ADDRESS, "x86-got-absolute-address"},
      {MO_PIC_BASE_OFFSET, "x86-pic-base-offset"},
      {MO_GOT, "x86-got"},
      {MO_GOTOFF, "x86-gotoff"},
      {MO_GOTPCREL, "x86-gotpcrel"},
      {MO_GOTPCREL_NORELAX, "x86-gotpcrel-norelax"},
      {MO_PLT, "x86-plt"},
      {MO_TLSGD, "x86-tlsgd"},
      {MO_TLSLD, "x86-tlsld"},
      {MO_TLSLDM, "x86-tlsldm"},
      {MO_GOTTPOFF, "x86-gottpoff"},
      {MO_INDNTPOFF, "x86-indntpoff"},
      {MO_TPOFF, "x86-tpoff"},
      {MO_DTPOFF, "x86-dtpoff"},
      {MO_NTPOFF, "x86-ntpoff"},
      {MO_GOTNTPOFF, "x86-gotntpoff"},
      {MO_DLLIMPORT, "x86-dllimport"},
      {MO_DARWIN_NONLAZY, "x86-darwin-nonlazy"},
      {MO_DARWIN_NONLAZY_PIC_BASE, "x86-darwin-nonlazy-pic-base"},
      {MO_TLVP, "x86-tlvp"},
      {MO_TLVP_PIC_BASE, "x86-tlvp-pic-base"},
      {MO_SECREL, "x86-secrel"},
      {MO_COFFSTUB, "x86-coffstub"}};
  return ArrayRef(TargetFlags);
}

namespace {
/// Create Global Base Reg pass. This initializes the PIC
/// global base register for x86-32.
struct CGBR : public MachineFunctionPass {
  static char ID;
  CGBR() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    const X86TargetMachine *TM =
        static_cast<const X86TargetMachine *>(&MF.getTarget());
    const X86Subtarget &STI = MF.getSubtarget<X86Subtarget>();

    // Only emit a global base reg in PIC mode.
    if (!TM->isPositionIndependent())
      return false;

    X86MachineFunctionInfo *X86FI = MF.getInfo<X86MachineFunctionInfo>();
    Register GlobalBaseReg = X86FI->getGlobalBaseReg();

    // If we didn't need a GlobalBaseReg, don't insert code.
    if (GlobalBaseReg == 0)
      return false;

    // Insert the set of GlobalBaseReg into the first MBB of the function
    MachineBasicBlock &FirstMBB = MF.front();
    MachineBasicBlock::iterator MBBI = FirstMBB.begin();
    DebugLoc DL = FirstMBB.findDebugLoc(MBBI);
    MachineRegisterInfo &RegInfo = MF.getRegInfo();
    const X86InstrInfo *TII = STI.getInstrInfo();

    Register PC;
    if (STI.isPICStyleGOT())
      PC = RegInfo.createVirtualRegister(&X86::GR32RegClass);
    else
      PC = GlobalBaseReg;

    if (STI.is64Bit()) {
      if (TM->getCodeModel() == CodeModel::Large) {
        // In the large code model, we are aiming for this code, though the
        // register allocation may vary:
        //   leaq .LN$pb(%rip), %rax
        //   movq $_GLOBAL_OFFSET_TABLE_ - .LN$pb, %rcx
        //   addq %rcx, %rax
        // RAX now holds address of _GLOBAL_OFFSET_TABLE_.
        Register PBReg = RegInfo.createVirtualRegister(&X86::GR64RegClass);
        Register GOTReg = RegInfo.createVirtualRegister(&X86::GR64RegClass);
        BuildMI(FirstMBB, MBBI, DL, TII->get(X86::LEA64r), PBReg)
            .addReg(X86::RIP)
            .addImm(0)
            .addReg(0)
            .addSym(MF.getPICBaseSymbol())
            .addReg(0);
        std::prev(MBBI)->setPreInstrSymbol(MF, MF.getPICBaseSymbol());
        BuildMI(FirstMBB, MBBI, DL, TII->get(X86::MOV64ri), GOTReg)
            .addExternalSymbol("_GLOBAL_OFFSET_TABLE_",
                               X86II::MO_PIC_BASE_OFFSET);
        BuildMI(FirstMBB, MBBI, DL, TII->get(X86::ADD64rr), PC)
            .addReg(PBReg, RegState::Kill)
            .addReg(GOTReg, RegState::Kill);
      } else {
        // In other code models, use a RIP-relative LEA to materialize the
        // GOT.
        BuildMI(FirstMBB, MBBI, DL, TII->get(X86::LEA64r), PC)
            .addReg(X86::RIP)
            .addImm(0)
            .addReg(0)
            .addExternalSymbol("_GLOBAL_OFFSET_TABLE_")
            .addReg(0);
      }
    } else {
      // Operand of MovePCtoStack is completely ignored by asm printer. It's
      // only used in JIT code emission as displacement to pc.
      BuildMI(FirstMBB, MBBI, DL, TII->get(X86::MOVPC32r), PC).addImm(0);

      // If we're using vanilla 'GOT' PIC style, we should use relative
      // addressing not to pc, but to _GLOBAL_OFFSET_TABLE_ external.
      if (STI.isPICStyleGOT()) {
        // Generate addl $__GLOBAL_OFFSET_TABLE_ + [.-piclabel],
        // %some_register
        BuildMI(FirstMBB, MBBI, DL, TII->get(X86::ADD32ri), GlobalBaseReg)
            .addReg(PC)
            .addExternalSymbol("_GLOBAL_OFFSET_TABLE_",
                               X86II::MO_GOT_ABSOLUTE_ADDRESS);
      }
    }

    return true;
  }

  StringRef getPassName() const override {
    return "X86 PIC Global Base Reg Initialization";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};
} // namespace

char CGBR::ID = 0;
FunctionPass *llvm::createX86GlobalBaseRegPass() { return new CGBR(); }

namespace {
struct LDTLSCleanup : public MachineFunctionPass {
  static char ID;
  LDTLSCleanup() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override {
    if (skipFunction(MF.getFunction()))
      return false;

    X86MachineFunctionInfo *MFI = MF.getInfo<X86MachineFunctionInfo>();
    if (MFI->getNumLocalDynamicTLSAccesses() < 2) {
      // No point folding accesses if there isn't at least two.
      return false;
    }

    MachineDominatorTree *DT =
        &getAnalysis<MachineDominatorTreeWrapperPass>().getDomTree();
    return VisitNode(DT->getRootNode(), 0);
  }

  // Visit the dominator subtree rooted at Node in pre-order.
  // If TLSBaseAddrReg is non-null, then use that to replace any
  // TLS_base_addr instructions. Otherwise, create the register
  // when the first such instruction is seen, and then use it
  // as we encounter more instructions.
  bool VisitNode(MachineDomTreeNode *Node, unsigned TLSBaseAddrReg) {
    MachineBasicBlock *BB = Node->getBlock();
    bool Changed = false;

    // Traverse the current block.
    for (MachineBasicBlock::iterator I = BB->begin(), E = BB->end(); I != E;
         ++I) {
      switch (I->getOpcode()) {
      case X86::TLS_base_addr32:
      case X86::TLS_base_addr64:
        if (TLSBaseAddrReg)
          I = ReplaceTLSBaseAddrCall(*I, TLSBaseAddrReg);
        else
          I = SetRegister(*I, &TLSBaseAddrReg);
        Changed = true;
        break;
      default:
        break;
      }
    }

    // Visit the children of this block in the dominator tree.
    for (auto &I : *Node) {
      Changed |= VisitNode(I, TLSBaseAddrReg);
    }

    return Changed;
  }

  // Replace the TLS_base_addr instruction I with a copy from
  // TLSBaseAddrReg, returning the new instruction.
  MachineInstr *ReplaceTLSBaseAddrCall(MachineInstr &I,
                                       unsigned TLSBaseAddrReg) {
    MachineFunction *MF = I.getParent()->getParent();
    const X86Subtarget &STI = MF->getSubtarget<X86Subtarget>();
    const bool is64Bit = STI.is64Bit();
    const X86InstrInfo *TII = STI.getInstrInfo();

    // Insert a Copy from TLSBaseAddrReg to RAX/EAX.
    MachineInstr *Copy =
        BuildMI(*I.getParent(), I, I.getDebugLoc(),
                TII->get(TargetOpcode::COPY), is64Bit ? X86::RAX : X86::EAX)
            .addReg(TLSBaseAddrReg);

    // Erase the TLS_base_addr instruction.
    I.eraseFromParent();

    return Copy;
  }

  // Create a virtual register in *TLSBaseAddrReg, and populate it by
  // inserting a copy instruction after I. Returns the new instruction.
  MachineInstr *SetRegister(MachineInstr &I, unsigned *TLSBaseAddrReg) {
    MachineFunction *MF = I.getParent()->getParent();
    const X86Subtarget &STI = MF->getSubtarget<X86Subtarget>();
    const bool is64Bit = STI.is64Bit();
    const X86InstrInfo *TII = STI.getInstrInfo();

    // Create a virtual register for the TLS base address.
    MachineRegisterInfo &RegInfo = MF->getRegInfo();
    *TLSBaseAddrReg = RegInfo.createVirtualRegister(
        is64Bit ? &X86::GR64RegClass : &X86::GR32RegClass);

    // Insert a copy from RAX/EAX to TLSBaseAddrReg.
    MachineInstr *Next = I.getNextNode();
    MachineInstr *Copy = BuildMI(*I.getParent(), Next, I.getDebugLoc(),
                                 TII->get(TargetOpcode::COPY), *TLSBaseAddrReg)
                             .addReg(is64Bit ? X86::RAX : X86::EAX);

    return Copy;
  }

  StringRef getPassName() const override {
    return "Local Dynamic TLS Access Clean-up";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineDominatorTreeWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};
} // namespace

char LDTLSCleanup::ID = 0;
FunctionPass *llvm::createCleanupLocalDynamicTLSPass() {
  return new LDTLSCleanup();
}

/// Constants defining how certain sequences should be outlined.
///
/// \p MachineOutlinerDefault implies that the function is called with a call
/// instruction, and a return must be emitted for the outlined function frame.
///
/// That is,
///
/// I1                                 OUTLINED_FUNCTION:
/// I2 --> call OUTLINED_FUNCTION       I1
/// I3                                  I2
///                                     I3
///                                     ret
///
/// * Call construction overhead: 1 (call instruction)
/// * Frame construction overhead: 1 (return instruction)
///
/// \p MachineOutlinerTailCall implies that the function is being tail called.
/// A jump is emitted instead of a call, and the return is already present in
/// the outlined sequence. That is,
///
/// I1                                 OUTLINED_FUNCTION:
/// I2 --> jmp OUTLINED_FUNCTION       I1
/// ret                                I2
///                                    ret
///
/// * Call construction overhead: 1 (jump instruction)
/// * Frame construction overhead: 0 (don't need to return)
///
enum MachineOutlinerClass { MachineOutlinerDefault, MachineOutlinerTailCall };

std::optional<outliner::OutlinedFunction>
X86InstrInfo::getOutliningCandidateInfo(
    std::vector<outliner::Candidate> &RepeatedSequenceLocs) const {
  unsigned SequenceSize = 0;
  for (auto &MI : RepeatedSequenceLocs[0]) {
    // FIXME: x86 doesn't implement getInstSizeInBytes, so
    // we can't tell the cost.  Just assume each instruction
    // is one byte.
    if (MI.isDebugInstr() || MI.isKill())
      continue;
    SequenceSize += 1;
  }

  // We check to see if CFI Instructions are present, and if they are
  // we find the number of CFI Instructions in the candidates.
  unsigned CFICount = 0;
  for (auto &I : RepeatedSequenceLocs[0]) {
    if (I.isCFIInstruction())
      CFICount++;
  }

  // We compare the number of found CFI Instructions to  the number of CFI
  // instructions in the parent function for each candidate.  We must check this
  // since if we outline one of the CFI instructions in a function, we have to
  // outline them all for correctness. If we do not, the address offsets will be
  // incorrect between the two sections of the program.
  for (outliner::Candidate &C : RepeatedSequenceLocs) {
    std::vector<MCCFIInstruction> CFIInstructions =
        C.getMF()->getFrameInstructions();

    if (CFICount > 0 && CFICount != CFIInstructions.size())
      return std::nullopt;
  }

  // FIXME: Use real size in bytes for call and ret instructions.
  if (RepeatedSequenceLocs[0].back().isTerminator()) {
    for (outliner::Candidate &C : RepeatedSequenceLocs)
      C.setCallInfo(MachineOutlinerTailCall, 1);

    return outliner::OutlinedFunction(RepeatedSequenceLocs, SequenceSize,
                                      0, // Number of bytes to emit frame.
                                      MachineOutlinerTailCall // Type of frame.
    );
  }

  if (CFICount > 0)
    return std::nullopt;

  for (outliner::Candidate &C : RepeatedSequenceLocs)
    C.setCallInfo(MachineOutlinerDefault, 1);

  return outliner::OutlinedFunction(RepeatedSequenceLocs, SequenceSize, 1,
                                    MachineOutlinerDefault);
}

bool X86InstrInfo::isFunctionSafeToOutlineFrom(
    MachineFunction &MF, bool OutlineFromLinkOnceODRs) const {
  const Function &F = MF.getFunction();

  // Does the function use a red zone? If it does, then we can't risk messing
  // with the stack.
  if (Subtarget.getFrameLowering()->has128ByteRedZone(MF)) {
    // It could have a red zone. If it does, then we don't want to touch it.
    const X86MachineFunctionInfo *X86FI = MF.getInfo<X86MachineFunctionInfo>();
    if (!X86FI || X86FI->getUsesRedZone())
      return false;
  }

  // If we *don't* want to outline from things that could potentially be deduped
  // then return false.
  if (!OutlineFromLinkOnceODRs && F.hasLinkOnceODRLinkage())
    return false;

  // This function is viable for outlining, so return true.
  return true;
}

outliner::InstrType
X86InstrInfo::getOutliningTypeImpl(MachineBasicBlock::iterator &MIT,
                                   unsigned Flags) const {
  MachineInstr &MI = *MIT;

  // Is this a terminator for a basic block?
  if (MI.isTerminator())
    // TargetInstrInfo::getOutliningType has already filtered out anything
    // that would break this, so we can allow it here.
    return outliner::InstrType::Legal;

  // Don't outline anything that modifies or reads from the stack pointer.
  //
  // FIXME: There are instructions which are being manually built without
  // explicit uses/defs so we also have to check the MCInstrDesc. We should be
  // able to remove the extra checks once those are fixed up. For example,
  // sometimes we might get something like %rax = POP64r 1. This won't be
  // caught by modifiesRegister or readsRegister even though the instruction
  // really ought to be formed so that modifiesRegister/readsRegister would
  // catch it.
  if (MI.modifiesRegister(X86::RSP, &RI) || MI.readsRegister(X86::RSP, &RI) ||
      MI.getDesc().hasImplicitUseOfPhysReg(X86::RSP) ||
      MI.getDesc().hasImplicitDefOfPhysReg(X86::RSP))
    return outliner::InstrType::Illegal;

  // Outlined calls change the instruction pointer, so don't read from it.
  if (MI.readsRegister(X86::RIP, &RI) ||
      MI.getDesc().hasImplicitUseOfPhysReg(X86::RIP) ||
      MI.getDesc().hasImplicitDefOfPhysReg(X86::RIP))
    return outliner::InstrType::Illegal;

  // Don't outline CFI instructions.
  if (MI.isCFIInstruction())
    return outliner::InstrType::Illegal;

  return outliner::InstrType::Legal;
}

void X86InstrInfo::buildOutlinedFrame(
    MachineBasicBlock &MBB, MachineFunction &MF,
    const outliner::OutlinedFunction &OF) const {
  // If we're a tail call, we already have a return, so don't do anything.
  if (OF.FrameConstructionID == MachineOutlinerTailCall)
    return;

  // We're a normal call, so our sequence doesn't have a return instruction.
  // Add it in.
  MachineInstr *retq = BuildMI(MF, DebugLoc(), get(X86::RET64));
  MBB.insert(MBB.end(), retq);
}

MachineBasicBlock::iterator X86InstrInfo::insertOutlinedCall(
    Module &M, MachineBasicBlock &MBB, MachineBasicBlock::iterator &It,
    MachineFunction &MF, outliner::Candidate &C) const {
  // Is it a tail call?
  if (C.CallConstructionID == MachineOutlinerTailCall) {
    // Yes, just insert a JMP.
    It = MBB.insert(It, BuildMI(MF, DebugLoc(), get(X86::TAILJMPd64))
                            .addGlobalAddress(M.getNamedValue(MF.getName())));
  } else {
    // No, insert a call.
    It = MBB.insert(It, BuildMI(MF, DebugLoc(), get(X86::CALL64pcrel32))
                            .addGlobalAddress(M.getNamedValue(MF.getName())));
  }

  return It;
}

void X86InstrInfo::buildClearRegister(Register Reg, MachineBasicBlock &MBB,
                                      MachineBasicBlock::iterator Iter,
                                      DebugLoc &DL,
                                      bool AllowSideEffects) const {
  const MachineFunction &MF = *MBB.getParent();
  const X86Subtarget &ST = MF.getSubtarget<X86Subtarget>();
  const TargetRegisterInfo &TRI = getRegisterInfo();

  if (ST.hasMMX() && X86::VR64RegClass.contains(Reg))
    // FIXME: Should we ignore MMX registers?
    return;

  if (TRI.isGeneralPurposeRegister(MF, Reg)) {
    // Convert register to the 32-bit version. Both 'movl' and 'xorl' clear the
    // upper bits of a 64-bit register automagically.
    Reg = getX86SubSuperRegister(Reg, 32);

    if (!AllowSideEffects)
      // XOR affects flags, so use a MOV instead.
      BuildMI(MBB, Iter, DL, get(X86::MOV32ri), Reg).addImm(0);
    else
      BuildMI(MBB, Iter, DL, get(X86::XOR32rr), Reg)
          .addReg(Reg, RegState::Undef)
          .addReg(Reg, RegState::Undef);
  } else if (X86::VR128RegClass.contains(Reg)) {
    // XMM#
    if (!ST.hasSSE1())
      return;

    // PXOR is safe to use because it doesn't affect flags.
    BuildMI(MBB, Iter, DL, get(X86::PXORrr), Reg)
        .addReg(Reg, RegState::Undef)
        .addReg(Reg, RegState::Undef);
  } else if (X86::VR256RegClass.contains(Reg)) {
    // YMM#
    if (!ST.hasAVX())
      return;

    // VPXOR is safe to use because it doesn't affect flags.
    BuildMI(MBB, Iter, DL, get(X86::VPXORrr), Reg)
        .addReg(Reg, RegState::Undef)
        .addReg(Reg, RegState::Undef);
  } else if (X86::VR512RegClass.contains(Reg)) {
    // ZMM#
    if (!ST.hasAVX512())
      return;

    // VPXORY is safe to use because it doesn't affect flags.
    BuildMI(MBB, Iter, DL, get(X86::VPXORYrr), Reg)
        .addReg(Reg, RegState::Undef)
        .addReg(Reg, RegState::Undef);
  } else if (X86::VK1RegClass.contains(Reg) || X86::VK2RegClass.contains(Reg) ||
             X86::VK4RegClass.contains(Reg) || X86::VK8RegClass.contains(Reg) ||
             X86::VK16RegClass.contains(Reg)) {
    if (!ST.hasVLX())
      return;

    // KXOR is safe to use because it doesn't affect flags.
    unsigned Op = ST.hasBWI() ? X86::KXORQrr : X86::KXORWrr;
    BuildMI(MBB, Iter, DL, get(Op), Reg)
        .addReg(Reg, RegState::Undef)
        .addReg(Reg, RegState::Undef);
  }
}

bool X86InstrInfo::getMachineCombinerPatterns(
    MachineInstr &Root, SmallVectorImpl<unsigned> &Patterns,
    bool DoRegPressureReduce) const {
  unsigned Opc = Root.getOpcode();
  switch (Opc) {
  case X86::VPDPWSSDrr:
  case X86::VPDPWSSDrm:
  case X86::VPDPWSSDYrr:
  case X86::VPDPWSSDYrm: {
    if (!Subtarget.hasFastDPWSSD()) {
      Patterns.push_back(X86MachineCombinerPattern::DPWSSD);
      return true;
    }
    break;
  }
  case X86::VPDPWSSDZ128r:
  case X86::VPDPWSSDZ128m:
  case X86::VPDPWSSDZ256r:
  case X86::VPDPWSSDZ256m:
  case X86::VPDPWSSDZr:
  case X86::VPDPWSSDZm: {
   if (Subtarget.hasBWI() && !Subtarget.hasFastDPWSSD()) {
     Patterns.push_back(X86MachineCombinerPattern::DPWSSD);
     return true;
    }
    break;
  }
  }
  return TargetInstrInfo::getMachineCombinerPatterns(Root,
                                                     Patterns, DoRegPressureReduce);
}

static void
genAlternativeDpCodeSequence(MachineInstr &Root, const TargetInstrInfo &TII,
                             SmallVectorImpl<MachineInstr *> &InsInstrs,
                             SmallVectorImpl<MachineInstr *> &DelInstrs,
                             DenseMap<unsigned, unsigned> &InstrIdxForVirtReg) {
  MachineFunction *MF = Root.getMF();
  MachineRegisterInfo &RegInfo = MF->getRegInfo();

  unsigned Opc = Root.getOpcode();
  unsigned AddOpc = 0;
  unsigned MaddOpc = 0;
  switch (Opc) {
  default:
    assert(false && "It should not reach here");
    break;
  // vpdpwssd xmm2,xmm3,xmm1
  // -->
  // vpmaddwd xmm3,xmm3,xmm1
  // vpaddd xmm2,xmm2,xmm3
  case X86::VPDPWSSDrr:
    MaddOpc = X86::VPMADDWDrr;
    AddOpc = X86::VPADDDrr;
    break;
  case X86::VPDPWSSDrm:
    MaddOpc = X86::VPMADDWDrm;
    AddOpc = X86::VPADDDrr;
    break;
  case X86::VPDPWSSDZ128r:
    MaddOpc = X86::VPMADDWDZ128rr;
    AddOpc = X86::VPADDDZ128rr;
    break;
  case X86::VPDPWSSDZ128m:
    MaddOpc = X86::VPMADDWDZ128rm;
    AddOpc = X86::VPADDDZ128rr;
    break;
  // vpdpwssd ymm2,ymm3,ymm1
  // -->
  // vpmaddwd ymm3,ymm3,ymm1
  // vpaddd ymm2,ymm2,ymm3
  case X86::VPDPWSSDYrr:
    MaddOpc = X86::VPMADDWDYrr;
    AddOpc = X86::VPADDDYrr;
    break;
  case X86::VPDPWSSDYrm:
    MaddOpc = X86::VPMADDWDYrm;
    AddOpc = X86::VPADDDYrr;
    break;
  case X86::VPDPWSSDZ256r:
    MaddOpc = X86::VPMADDWDZ256rr;
    AddOpc = X86::VPADDDZ256rr;
    break;
  case X86::VPDPWSSDZ256m:
    MaddOpc = X86::VPMADDWDZ256rm;
    AddOpc = X86::VPADDDZ256rr;
    break;
  // vpdpwssd zmm2,zmm3,zmm1
  // -->
  // vpmaddwd zmm3,zmm3,zmm1
  // vpaddd zmm2,zmm2,zmm3
  case X86::VPDPWSSDZr:
    MaddOpc = X86::VPMADDWDZrr;
    AddOpc = X86::VPADDDZrr;
    break;
  case X86::VPDPWSSDZm:
    MaddOpc = X86::VPMADDWDZrm;
    AddOpc = X86::VPADDDZrr;
    break;
  }
  // Create vpmaddwd.
  const TargetRegisterClass *RC =
      RegInfo.getRegClass(Root.getOperand(0).getReg());
  Register NewReg = RegInfo.createVirtualRegister(RC);
  MachineInstr *Madd = Root.getMF()->CloneMachineInstr(&Root);
  Madd->setDesc(TII.get(MaddOpc));
  Madd->untieRegOperand(1);
  Madd->removeOperand(1);
  Madd->getOperand(0).setReg(NewReg);
  InstrIdxForVirtReg.insert(std::make_pair(NewReg, 0));
  // Create vpaddd.
  Register DstReg = Root.getOperand(0).getReg();
  bool IsKill = Root.getOperand(1).isKill();
  MachineInstr *Add =
      BuildMI(*MF, MIMetadata(Root), TII.get(AddOpc), DstReg)
          .addReg(Root.getOperand(1).getReg(), getKillRegState(IsKill))
          .addReg(Madd->getOperand(0).getReg(), getKillRegState(true));
  InsInstrs.push_back(Madd);
  InsInstrs.push_back(Add);
  DelInstrs.push_back(&Root);
}

void X86InstrInfo::genAlternativeCodeSequence(
    MachineInstr &Root, unsigned Pattern,
    SmallVectorImpl<MachineInstr *> &InsInstrs,
    SmallVectorImpl<MachineInstr *> &DelInstrs,
    DenseMap<unsigned, unsigned> &InstrIdxForVirtReg) const {
  switch (Pattern) {
  default:
    // Reassociate instructions.
    TargetInstrInfo::genAlternativeCodeSequence(Root, Pattern, InsInstrs,
                                                DelInstrs, InstrIdxForVirtReg);
    return;
  case X86MachineCombinerPattern::DPWSSD:
    genAlternativeDpCodeSequence(Root, *this, InsInstrs, DelInstrs,
                                 InstrIdxForVirtReg);
    return;
  }
}

// See also: X86DAGToDAGISel::SelectInlineAsmMemoryOperand().
void X86InstrInfo::getFrameIndexOperands(SmallVectorImpl<MachineOperand> &Ops,
                                         int FI) const {
  X86AddressMode M;
  M.BaseType = X86AddressMode::FrameIndexBase;
  M.Base.FrameIndex = FI;
  M.getFullAddress(Ops);
}

#define GET_INSTRINFO_HELPERS
#include "X86GenInstrInfo.inc"
