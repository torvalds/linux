//===-- RISCVFrameLowering.cpp - RISC-V Frame Information -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the RISC-V implementation of TargetFrameLowering class.
//
//===----------------------------------------------------------------------===//

#include "RISCVFrameLowering.h"
#include "RISCVMachineFunctionInfo.h"
#include "RISCVSubtarget.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/MC/MCDwarf.h"
#include "llvm/Support/LEB128.h"

#include <algorithm>

using namespace llvm;

static Align getABIStackAlignment(RISCVABI::ABI ABI) {
  if (ABI == RISCVABI::ABI_ILP32E)
    return Align(4);
  if (ABI == RISCVABI::ABI_LP64E)
    return Align(8);
  return Align(16);
}

RISCVFrameLowering::RISCVFrameLowering(const RISCVSubtarget &STI)
    : TargetFrameLowering(
          StackGrowsDown, getABIStackAlignment(STI.getTargetABI()),
          /*LocalAreaOffset=*/0,
          /*TransientStackAlignment=*/getABIStackAlignment(STI.getTargetABI())),
      STI(STI) {}

// Offsets which need to be scale by XLen representing locations of CSRs which
// are given a fixed location by save/restore libcalls or Zcmp Push/Pop.
static const std::pair<MCPhysReg, int8_t> FixedCSRFIMap[] = {
    {/*ra*/ RISCV::X1, -1},   {/*s0*/ RISCV::X8, -2},
    {/*s1*/ RISCV::X9, -3},   {/*s2*/ RISCV::X18, -4},
    {/*s3*/ RISCV::X19, -5},  {/*s4*/ RISCV::X20, -6},
    {/*s5*/ RISCV::X21, -7},  {/*s6*/ RISCV::X22, -8},
    {/*s7*/ RISCV::X23, -9},  {/*s8*/ RISCV::X24, -10},
    {/*s9*/ RISCV::X25, -11}, {/*s10*/ RISCV::X26, -12},
    {/*s11*/ RISCV::X27, -13}};

// For now we use x3, a.k.a gp, as pointer to shadow call stack.
// User should not use x3 in their asm.
static void emitSCSPrologue(MachineFunction &MF, MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI,
                            const DebugLoc &DL) {
  if (!MF.getFunction().hasFnAttribute(Attribute::ShadowCallStack))
    return;

  const auto &STI = MF.getSubtarget<RISCVSubtarget>();
  const llvm::RISCVRegisterInfo *TRI = STI.getRegisterInfo();
  Register RAReg = TRI->getRARegister();

  // Do not save RA to the SCS if it's not saved to the regular stack,
  // i.e. RA is not at risk of being overwritten.
  std::vector<CalleeSavedInfo> &CSI = MF.getFrameInfo().getCalleeSavedInfo();
  if (llvm::none_of(
          CSI, [&](CalleeSavedInfo &CSR) { return CSR.getReg() == RAReg; }))
    return;

  const RISCVInstrInfo *TII = STI.getInstrInfo();
  if (!STI.hasForcedSWShadowStack() && STI.hasStdExtZicfiss()) {
    BuildMI(MBB, MI, DL, TII->get(RISCV::SSPUSH)).addReg(RAReg);
    return;
  }

  Register SCSPReg = RISCVABI::getSCSPReg();

  bool IsRV64 = STI.hasFeature(RISCV::Feature64Bit);
  int64_t SlotSize = STI.getXLen() / 8;
  // Store return address to shadow call stack
  // addi    gp, gp, [4|8]
  // s[w|d]  ra, -[4|8](gp)
  BuildMI(MBB, MI, DL, TII->get(RISCV::ADDI))
      .addReg(SCSPReg, RegState::Define)
      .addReg(SCSPReg)
      .addImm(SlotSize)
      .setMIFlag(MachineInstr::FrameSetup);
  BuildMI(MBB, MI, DL, TII->get(IsRV64 ? RISCV::SD : RISCV::SW))
      .addReg(RAReg)
      .addReg(SCSPReg)
      .addImm(-SlotSize)
      .setMIFlag(MachineInstr::FrameSetup);

  // Emit a CFI instruction that causes SlotSize to be subtracted from the value
  // of the shadow stack pointer when unwinding past this frame.
  char DwarfSCSReg = TRI->getDwarfRegNum(SCSPReg, /*IsEH*/ true);
  assert(DwarfSCSReg < 32 && "SCS Register should be < 32 (X3).");

  char Offset = static_cast<char>(-SlotSize) & 0x7f;
  const char CFIInst[] = {
      dwarf::DW_CFA_val_expression,
      DwarfSCSReg, // register
      2,           // length
      static_cast<char>(unsigned(dwarf::DW_OP_breg0 + DwarfSCSReg)),
      Offset, // addend (sleb128)
  };

  unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createEscape(
      nullptr, StringRef(CFIInst, sizeof(CFIInst))));
  BuildMI(MBB, MI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(CFIIndex)
      .setMIFlag(MachineInstr::FrameSetup);
}

static void emitSCSEpilogue(MachineFunction &MF, MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI,
                            const DebugLoc &DL) {
  if (!MF.getFunction().hasFnAttribute(Attribute::ShadowCallStack))
    return;

  const auto &STI = MF.getSubtarget<RISCVSubtarget>();
  Register RAReg = STI.getRegisterInfo()->getRARegister();

  // See emitSCSPrologue() above.
  std::vector<CalleeSavedInfo> &CSI = MF.getFrameInfo().getCalleeSavedInfo();
  if (llvm::none_of(
          CSI, [&](CalleeSavedInfo &CSR) { return CSR.getReg() == RAReg; }))
    return;

  const RISCVInstrInfo *TII = STI.getInstrInfo();
  if (!STI.hasForcedSWShadowStack() && STI.hasStdExtZicfiss()) {
    BuildMI(MBB, MI, DL, TII->get(RISCV::SSPOPCHK)).addReg(RAReg);
    return;
  }

  Register SCSPReg = RISCVABI::getSCSPReg();

  bool IsRV64 = STI.hasFeature(RISCV::Feature64Bit);
  int64_t SlotSize = STI.getXLen() / 8;
  // Load return address from shadow call stack
  // l[w|d]  ra, -[4|8](gp)
  // addi    gp, gp, -[4|8]
  BuildMI(MBB, MI, DL, TII->get(IsRV64 ? RISCV::LD : RISCV::LW))
      .addReg(RAReg, RegState::Define)
      .addReg(SCSPReg)
      .addImm(-SlotSize)
      .setMIFlag(MachineInstr::FrameDestroy);
  BuildMI(MBB, MI, DL, TII->get(RISCV::ADDI))
      .addReg(SCSPReg, RegState::Define)
      .addReg(SCSPReg)
      .addImm(-SlotSize)
      .setMIFlag(MachineInstr::FrameDestroy);
  // Restore the SCS pointer
  unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createRestore(
      nullptr, STI.getRegisterInfo()->getDwarfRegNum(SCSPReg, /*IsEH*/ true)));
  BuildMI(MBB, MI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(CFIIndex)
      .setMIFlags(MachineInstr::FrameDestroy);
}

// Get the ID of the libcall used for spilling and restoring callee saved
// registers. The ID is representative of the number of registers saved or
// restored by the libcall, except it is zero-indexed - ID 0 corresponds to a
// single register.
static int getLibCallID(const MachineFunction &MF,
                        const std::vector<CalleeSavedInfo> &CSI) {
  const auto *RVFI = MF.getInfo<RISCVMachineFunctionInfo>();

  if (CSI.empty() || !RVFI->useSaveRestoreLibCalls(MF))
    return -1;

  Register MaxReg = RISCV::NoRegister;
  for (auto &CS : CSI)
    // assignCalleeSavedSpillSlots assigns negative frame indexes to
    // registers which can be saved by libcall.
    if (CS.getFrameIdx() < 0)
      MaxReg = std::max(MaxReg.id(), CS.getReg().id());

  if (MaxReg == RISCV::NoRegister)
    return -1;

  switch (MaxReg) {
  default:
    llvm_unreachable("Something has gone wrong!");
  case /*s11*/ RISCV::X27: return 12;
  case /*s10*/ RISCV::X26: return 11;
  case /*s9*/  RISCV::X25: return 10;
  case /*s8*/  RISCV::X24: return 9;
  case /*s7*/  RISCV::X23: return 8;
  case /*s6*/  RISCV::X22: return 7;
  case /*s5*/  RISCV::X21: return 6;
  case /*s4*/  RISCV::X20: return 5;
  case /*s3*/  RISCV::X19: return 4;
  case /*s2*/  RISCV::X18: return 3;
  case /*s1*/  RISCV::X9:  return 2;
  case /*s0*/  RISCV::X8:  return 1;
  case /*ra*/  RISCV::X1:  return 0;
  }
}

// Get the name of the libcall used for spilling callee saved registers.
// If this function will not use save/restore libcalls, then return a nullptr.
static const char *
getSpillLibCallName(const MachineFunction &MF,
                    const std::vector<CalleeSavedInfo> &CSI) {
  static const char *const SpillLibCalls[] = {
    "__riscv_save_0",
    "__riscv_save_1",
    "__riscv_save_2",
    "__riscv_save_3",
    "__riscv_save_4",
    "__riscv_save_5",
    "__riscv_save_6",
    "__riscv_save_7",
    "__riscv_save_8",
    "__riscv_save_9",
    "__riscv_save_10",
    "__riscv_save_11",
    "__riscv_save_12"
  };

  int LibCallID = getLibCallID(MF, CSI);
  if (LibCallID == -1)
    return nullptr;
  return SpillLibCalls[LibCallID];
}

// Get the name of the libcall used for restoring callee saved registers.
// If this function will not use save/restore libcalls, then return a nullptr.
static const char *
getRestoreLibCallName(const MachineFunction &MF,
                      const std::vector<CalleeSavedInfo> &CSI) {
  static const char *const RestoreLibCalls[] = {
    "__riscv_restore_0",
    "__riscv_restore_1",
    "__riscv_restore_2",
    "__riscv_restore_3",
    "__riscv_restore_4",
    "__riscv_restore_5",
    "__riscv_restore_6",
    "__riscv_restore_7",
    "__riscv_restore_8",
    "__riscv_restore_9",
    "__riscv_restore_10",
    "__riscv_restore_11",
    "__riscv_restore_12"
  };

  int LibCallID = getLibCallID(MF, CSI);
  if (LibCallID == -1)
    return nullptr;
  return RestoreLibCalls[LibCallID];
}

// Return encoded value and register count for PUSH/POP instruction,
// representing registers to store/load.
static std::pair<unsigned, unsigned>
getPushPopEncodingAndNum(const Register MaxReg) {
  switch (MaxReg) {
  default:
    llvm_unreachable("Unexpected Reg for Push/Pop Inst");
  case RISCV::X27: /*s11*/
    return std::make_pair(llvm::RISCVZC::RLISTENCODE::RA_S0_S11, 13);
  case RISCV::X25: /*s9*/
    return std::make_pair(llvm::RISCVZC::RLISTENCODE::RA_S0_S9, 11);
  case RISCV::X24: /*s8*/
    return std::make_pair(llvm::RISCVZC::RLISTENCODE::RA_S0_S8, 10);
  case RISCV::X23: /*s7*/
    return std::make_pair(llvm::RISCVZC::RLISTENCODE::RA_S0_S7, 9);
  case RISCV::X22: /*s6*/
    return std::make_pair(llvm::RISCVZC::RLISTENCODE::RA_S0_S6, 8);
  case RISCV::X21: /*s5*/
    return std::make_pair(llvm::RISCVZC::RLISTENCODE::RA_S0_S5, 7);
  case RISCV::X20: /*s4*/
    return std::make_pair(llvm::RISCVZC::RLISTENCODE::RA_S0_S4, 6);
  case RISCV::X19: /*s3*/
    return std::make_pair(llvm::RISCVZC::RLISTENCODE::RA_S0_S3, 5);
  case RISCV::X18: /*s2*/
    return std::make_pair(llvm::RISCVZC::RLISTENCODE::RA_S0_S2, 4);
  case RISCV::X9: /*s1*/
    return std::make_pair(llvm::RISCVZC::RLISTENCODE::RA_S0_S1, 3);
  case RISCV::X8: /*s0*/
    return std::make_pair(llvm::RISCVZC::RLISTENCODE::RA_S0, 2);
  case RISCV::X1: /*ra*/
    return std::make_pair(llvm::RISCVZC::RLISTENCODE::RA, 1);
  }
}

// Get the max reg of Push/Pop for restoring callee saved registers.
static Register getMaxPushPopReg(const MachineFunction &MF,
                                 const std::vector<CalleeSavedInfo> &CSI) {
  Register MaxPushPopReg = RISCV::NoRegister;
  for (auto &CS : CSI) {
    if (llvm::find_if(FixedCSRFIMap, [&](auto P) {
          return P.first == CS.getReg();
        }) != std::end(FixedCSRFIMap))
      MaxPushPopReg = std::max(MaxPushPopReg.id(), CS.getReg().id());
  }
  assert(MaxPushPopReg != RISCV::X26 && "x26 requires x27 to also be pushed");
  return MaxPushPopReg;
}

// Return true if the specified function should have a dedicated frame
// pointer register.  This is true if frame pointer elimination is
// disabled, if it needs dynamic stack realignment, if the function has
// variable sized allocas, or if the frame address is taken.
bool RISCVFrameLowering::hasFP(const MachineFunction &MF) const {
  const TargetRegisterInfo *RegInfo = MF.getSubtarget().getRegisterInfo();

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MF.getTarget().Options.DisableFramePointerElim(MF) ||
         RegInfo->hasStackRealignment(MF) || MFI.hasVarSizedObjects() ||
         MFI.isFrameAddressTaken();
}

bool RISCVFrameLowering::hasBP(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterInfo *TRI = STI.getRegisterInfo();

  // If we do not reserve stack space for outgoing arguments in prologue,
  // we will adjust the stack pointer before call instruction. After the
  // adjustment, we can not use SP to access the stack objects for the
  // arguments. Instead, use BP to access these stack objects.
  return (MFI.hasVarSizedObjects() ||
          (!hasReservedCallFrame(MF) && (!MFI.isMaxCallFrameSizeComputed() ||
                                         MFI.getMaxCallFrameSize() != 0))) &&
         TRI->hasStackRealignment(MF);
}

// Determines the size of the frame and maximum call frame size.
void RISCVFrameLowering::determineFrameLayout(MachineFunction &MF) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto *RVFI = MF.getInfo<RISCVMachineFunctionInfo>();

  // Get the number of bytes to allocate from the FrameInfo.
  uint64_t FrameSize = MFI.getStackSize();

  // Get the alignment.
  Align StackAlign = getStackAlign();

  // Make sure the frame is aligned.
  FrameSize = alignTo(FrameSize, StackAlign);

  // Update frame info.
  MFI.setStackSize(FrameSize);

  // When using SP or BP to access stack objects, we may require extra padding
  // to ensure the bottom of the RVV stack is correctly aligned within the main
  // stack. We calculate this as the amount required to align the scalar local
  // variable section up to the RVV alignment.
  const TargetRegisterInfo *TRI = STI.getRegisterInfo();
  if (RVFI->getRVVStackSize() && (!hasFP(MF) || TRI->hasStackRealignment(MF))) {
    int ScalarLocalVarSize = FrameSize - RVFI->getCalleeSavedStackSize() -
                             RVFI->getVarArgsSaveSize();
    if (auto RVVPadding =
            offsetToAlignment(ScalarLocalVarSize, RVFI->getRVVStackAlign()))
      RVFI->setRVVPadding(RVVPadding);
  }
}

// Returns the stack size including RVV padding (when required), rounded back
// up to the required stack alignment.
uint64_t RISCVFrameLowering::getStackSizeWithRVVPadding(
    const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  auto *RVFI = MF.getInfo<RISCVMachineFunctionInfo>();
  return alignTo(MFI.getStackSize() + RVFI->getRVVPadding(), getStackAlign());
}

// Returns the register used to hold the frame pointer.
static Register getFPReg(const RISCVSubtarget &STI) { return RISCV::X8; }

// Returns the register used to hold the stack pointer.
static Register getSPReg(const RISCVSubtarget &STI) { return RISCV::X2; }

static SmallVector<CalleeSavedInfo, 8>
getUnmanagedCSI(const MachineFunction &MF,
                const std::vector<CalleeSavedInfo> &CSI) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  SmallVector<CalleeSavedInfo, 8> NonLibcallCSI;

  for (auto &CS : CSI) {
    int FI = CS.getFrameIdx();
    if (FI >= 0 && MFI.getStackID(FI) == TargetStackID::Default)
      NonLibcallCSI.push_back(CS);
  }

  return NonLibcallCSI;
}

static SmallVector<CalleeSavedInfo, 8>
getRVVCalleeSavedInfo(const MachineFunction &MF,
                      const std::vector<CalleeSavedInfo> &CSI) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  SmallVector<CalleeSavedInfo, 8> RVVCSI;

  for (auto &CS : CSI) {
    int FI = CS.getFrameIdx();
    if (FI >= 0 && MFI.getStackID(FI) == TargetStackID::ScalableVector)
      RVVCSI.push_back(CS);
  }

  return RVVCSI;
}

void RISCVFrameLowering::adjustStackForRVV(MachineFunction &MF,
                                           MachineBasicBlock &MBB,
                                           MachineBasicBlock::iterator MBBI,
                                           const DebugLoc &DL, int64_t Amount,
                                           MachineInstr::MIFlag Flag) const {
  assert(Amount != 0 && "Did not need to adjust stack pointer for RVV.");

  const Register SPReg = getSPReg(STI);

  // Optimize compile time offset case
  StackOffset Offset = StackOffset::getScalable(Amount);
  if (auto VLEN = STI.getRealVLen()) {
    // 1. Multiply the number of v-slots by the (constant) length of register
    const int64_t VLENB = *VLEN / 8;
    assert(Amount % 8 == 0 &&
           "Reserve the stack by the multiple of one vector size.");
    const int64_t NumOfVReg = Amount / 8;
    const int64_t FixedOffset = NumOfVReg * VLENB;
    if (!isInt<32>(FixedOffset)) {
      report_fatal_error(
        "Frame size outside of the signed 32-bit range not supported");
    }
    Offset = StackOffset::getFixed(FixedOffset);
  }

  const RISCVRegisterInfo &RI = *STI.getRegisterInfo();
  // We must keep the stack pointer aligned through any intermediate
  // updates.
  RI.adjustReg(MBB, MBBI, DL, SPReg, SPReg, Offset,
               Flag, getStackAlign());
}

static void appendScalableVectorExpression(const TargetRegisterInfo &TRI,
                                           SmallVectorImpl<char> &Expr,
                                           int FixedOffset, int ScalableOffset,
                                           llvm::raw_string_ostream &Comment) {
  unsigned DwarfVLenB = TRI.getDwarfRegNum(RISCV::VLENB, true);
  uint8_t Buffer[16];
  if (FixedOffset) {
    Expr.push_back(dwarf::DW_OP_consts);
    Expr.append(Buffer, Buffer + encodeSLEB128(FixedOffset, Buffer));
    Expr.push_back((uint8_t)dwarf::DW_OP_plus);
    Comment << (FixedOffset < 0 ? " - " : " + ") << std::abs(FixedOffset);
  }

  Expr.push_back((uint8_t)dwarf::DW_OP_consts);
  Expr.append(Buffer, Buffer + encodeSLEB128(ScalableOffset, Buffer));

  Expr.push_back((uint8_t)dwarf::DW_OP_bregx);
  Expr.append(Buffer, Buffer + encodeULEB128(DwarfVLenB, Buffer));
  Expr.push_back(0);

  Expr.push_back((uint8_t)dwarf::DW_OP_mul);
  Expr.push_back((uint8_t)dwarf::DW_OP_plus);

  Comment << (ScalableOffset < 0 ? " - " : " + ") << std::abs(ScalableOffset)
          << " * vlenb";
}

static MCCFIInstruction createDefCFAExpression(const TargetRegisterInfo &TRI,
                                               Register Reg,
                                               uint64_t FixedOffset,
                                               uint64_t ScalableOffset) {
  assert(ScalableOffset != 0 && "Did not need to adjust CFA for RVV");
  SmallString<64> Expr;
  std::string CommentBuffer;
  llvm::raw_string_ostream Comment(CommentBuffer);
  // Build up the expression (Reg + FixedOffset + ScalableOffset * VLENB).
  unsigned DwarfReg = TRI.getDwarfRegNum(Reg, true);
  Expr.push_back((uint8_t)(dwarf::DW_OP_breg0 + DwarfReg));
  Expr.push_back(0);
  if (Reg == RISCV::X2)
    Comment << "sp";
  else
    Comment << printReg(Reg, &TRI);

  appendScalableVectorExpression(TRI, Expr, FixedOffset, ScalableOffset,
                                 Comment);

  SmallString<64> DefCfaExpr;
  uint8_t Buffer[16];
  DefCfaExpr.push_back(dwarf::DW_CFA_def_cfa_expression);
  DefCfaExpr.append(Buffer, Buffer + encodeULEB128(Expr.size(), Buffer));
  DefCfaExpr.append(Expr.str());

  return MCCFIInstruction::createEscape(nullptr, DefCfaExpr.str(), SMLoc(),
                                        Comment.str());
}

static MCCFIInstruction createDefCFAOffset(const TargetRegisterInfo &TRI,
                                           Register Reg, uint64_t FixedOffset,
                                           uint64_t ScalableOffset) {
  assert(ScalableOffset != 0 && "Did not need to adjust CFA for RVV");
  SmallString<64> Expr;
  std::string CommentBuffer;
  llvm::raw_string_ostream Comment(CommentBuffer);
  Comment << printReg(Reg, &TRI) << "  @ cfa";

  // Build up the expression (FixedOffset + ScalableOffset * VLENB).
  appendScalableVectorExpression(TRI, Expr, FixedOffset, ScalableOffset,
                                 Comment);

  SmallString<64> DefCfaExpr;
  uint8_t Buffer[16];
  unsigned DwarfReg = TRI.getDwarfRegNum(Reg, true);
  DefCfaExpr.push_back(dwarf::DW_CFA_expression);
  DefCfaExpr.append(Buffer, Buffer + encodeULEB128(DwarfReg, Buffer));
  DefCfaExpr.append(Buffer, Buffer + encodeULEB128(Expr.size(), Buffer));
  DefCfaExpr.append(Expr.str());

  return MCCFIInstruction::createEscape(nullptr, DefCfaExpr.str(), SMLoc(),
                                        Comment.str());
}

void RISCVFrameLowering::emitPrologue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto *RVFI = MF.getInfo<RISCVMachineFunctionInfo>();
  const RISCVRegisterInfo *RI = STI.getRegisterInfo();
  const RISCVInstrInfo *TII = STI.getInstrInfo();
  MachineBasicBlock::iterator MBBI = MBB.begin();

  Register FPReg = getFPReg(STI);
  Register SPReg = getSPReg(STI);
  Register BPReg = RISCVABI::getBPReg();

  // Debug location must be unknown since the first debug location is used
  // to determine the end of the prologue.
  DebugLoc DL;

  // All calls are tail calls in GHC calling conv, and functions have no
  // prologue/epilogue.
  if (MF.getFunction().getCallingConv() == CallingConv::GHC)
    return;

  // Emit prologue for shadow call stack.
  emitSCSPrologue(MF, MBB, MBBI, DL);

  auto FirstFrameSetup = MBBI;

  // Since spillCalleeSavedRegisters may have inserted a libcall, skip past
  // any instructions marked as FrameSetup
  while (MBBI != MBB.end() && MBBI->getFlag(MachineInstr::FrameSetup))
    ++MBBI;

  // Determine the correct frame layout
  determineFrameLayout(MF);

  // If libcalls are used to spill and restore callee-saved registers, the frame
  // has two sections; the opaque section managed by the libcalls, and the
  // section managed by MachineFrameInfo which can also hold callee saved
  // registers in fixed stack slots, both of which have negative frame indices.
  // This gets even more complicated when incoming arguments are passed via the
  // stack, as these too have negative frame indices. An example is detailed
  // below:
  //
  //  | incoming arg | <- FI[-3]
  //  | libcallspill |
  //  | calleespill  | <- FI[-2]
  //  | calleespill  | <- FI[-1]
  //  | this_frame   | <- FI[0]
  //
  // For negative frame indices, the offset from the frame pointer will differ
  // depending on which of these groups the frame index applies to.
  // The following calculates the correct offset knowing the number of callee
  // saved registers spilt by the two methods.
  if (int LibCallRegs = getLibCallID(MF, MFI.getCalleeSavedInfo()) + 1) {
    // Calculate the size of the frame managed by the libcall. The stack
    // alignment of these libcalls should be the same as how we set it in
    // getABIStackAlignment.
    unsigned LibCallFrameSize =
        alignTo((STI.getXLen() / 8) * LibCallRegs, getStackAlign());
    RVFI->setLibCallStackSize(LibCallFrameSize);
  }

  // FIXME (note copied from Lanai): This appears to be overallocating.  Needs
  // investigation. Get the number of bytes to allocate from the FrameInfo.
  uint64_t RealStackSize = getStackSizeWithRVVPadding(MF);
  uint64_t StackSize = RealStackSize - RVFI->getReservedSpillsSize();
  uint64_t RVVStackSize = RVFI->getRVVStackSize();

  // Early exit if there is no need to allocate on the stack
  if (RealStackSize == 0 && !MFI.adjustsStack() && RVVStackSize == 0)
    return;

  // If the stack pointer has been marked as reserved, then produce an error if
  // the frame requires stack allocation
  if (STI.isRegisterReservedByUser(SPReg))
    MF.getFunction().getContext().diagnose(DiagnosticInfoUnsupported{
        MF.getFunction(), "Stack pointer required, but has been reserved."});

  uint64_t FirstSPAdjustAmount = getFirstSPAdjustAmount(MF);
  // Split the SP adjustment to reduce the offsets of callee saved spill.
  if (FirstSPAdjustAmount) {
    StackSize = FirstSPAdjustAmount;
    RealStackSize = FirstSPAdjustAmount;
  }

  if (RVFI->isPushable(MF) && FirstFrameSetup != MBB.end() &&
      FirstFrameSetup->getOpcode() == RISCV::CM_PUSH) {
    // Use available stack adjustment in push instruction to allocate additional
    // stack space. Align the stack size down to a multiple of 16. This is
    // needed for RVE.
    // FIXME: Can we increase the stack size to a multiple of 16 instead?
    uint64_t Spimm = std::min(alignDown(StackSize, 16), (uint64_t)48);
    FirstFrameSetup->getOperand(1).setImm(Spimm);
    StackSize -= Spimm;
  }

  if (StackSize != 0) {
    // Allocate space on the stack if necessary.
    RI->adjustReg(MBB, MBBI, DL, SPReg, SPReg,
                  StackOffset::getFixed(-StackSize), MachineInstr::FrameSetup,
                  getStackAlign());
  }

  // Emit ".cfi_def_cfa_offset RealStackSize"
  unsigned CFIIndex = MF.addFrameInst(
      MCCFIInstruction::cfiDefCfaOffset(nullptr, RealStackSize));
  BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
      .addCFIIndex(CFIIndex)
      .setMIFlag(MachineInstr::FrameSetup);

  const auto &CSI = MFI.getCalleeSavedInfo();

  // The frame pointer is callee-saved, and code has been generated for us to
  // save it to the stack. We need to skip over the storing of callee-saved
  // registers as the frame pointer must be modified after it has been saved
  // to the stack, not before.
  // FIXME: assumes exactly one instruction is used to save each callee-saved
  // register.
  std::advance(MBBI, getUnmanagedCSI(MF, CSI).size());

  // Iterate over list of callee-saved registers and emit .cfi_offset
  // directives.
  for (const auto &Entry : CSI) {
    int FrameIdx = Entry.getFrameIdx();
    if (FrameIdx >= 0 &&
        MFI.getStackID(FrameIdx) == TargetStackID::ScalableVector)
      continue;

    int64_t Offset = MFI.getObjectOffset(FrameIdx);
    Register Reg = Entry.getReg();
    unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::createOffset(
        nullptr, RI->getDwarfRegNum(Reg, true), Offset));
    BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // Generate new FP.
  if (hasFP(MF)) {
    if (STI.isRegisterReservedByUser(FPReg))
      MF.getFunction().getContext().diagnose(DiagnosticInfoUnsupported{
          MF.getFunction(), "Frame pointer required, but has been reserved."});
    // The frame pointer does need to be reserved from register allocation.
    assert(MF.getRegInfo().isReserved(FPReg) && "FP not reserved");

    RI->adjustReg(MBB, MBBI, DL, FPReg, SPReg,
                  StackOffset::getFixed(RealStackSize - RVFI->getVarArgsSaveSize()),
                  MachineInstr::FrameSetup, getStackAlign());

    // Emit ".cfi_def_cfa $fp, RVFI->getVarArgsSaveSize()"
    unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::cfiDefCfa(
        nullptr, RI->getDwarfRegNum(FPReg, true), RVFI->getVarArgsSaveSize()));
    BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
        .addCFIIndex(CFIIndex)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // Emit the second SP adjustment after saving callee saved registers.
  if (FirstSPAdjustAmount) {
    uint64_t SecondSPAdjustAmount =
        getStackSizeWithRVVPadding(MF) - FirstSPAdjustAmount;
    assert(SecondSPAdjustAmount > 0 &&
           "SecondSPAdjustAmount should be greater than zero");
    RI->adjustReg(MBB, MBBI, DL, SPReg, SPReg,
                  StackOffset::getFixed(-SecondSPAdjustAmount),
                  MachineInstr::FrameSetup, getStackAlign());

    // If we are using a frame-pointer, and thus emitted ".cfi_def_cfa fp, 0",
    // don't emit an sp-based .cfi_def_cfa_offset
    if (!hasFP(MF)) {
      // Emit ".cfi_def_cfa_offset StackSize"
      unsigned CFIIndex = MF.addFrameInst(MCCFIInstruction::cfiDefCfaOffset(
          nullptr, getStackSizeWithRVVPadding(MF)));
      BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlag(MachineInstr::FrameSetup);
    }
  }

  if (RVVStackSize) {
    adjustStackForRVV(MF, MBB, MBBI, DL, -RVVStackSize,
                      MachineInstr::FrameSetup);
    if (!hasFP(MF)) {
      // Emit .cfi_def_cfa_expression "sp + StackSize + RVVStackSize * vlenb".
      unsigned CFIIndex = MF.addFrameInst(createDefCFAExpression(
          *RI, SPReg, getStackSizeWithRVVPadding(MF), RVVStackSize / 8));
      BuildMI(MBB, MBBI, DL, TII->get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlag(MachineInstr::FrameSetup);
    }

    std::advance(MBBI, getRVVCalleeSavedInfo(MF, CSI).size());
    emitCalleeSavedRVVPrologCFI(MBB, MBBI, hasFP(MF));
  }

  if (hasFP(MF)) {
    // Realign Stack
    const RISCVRegisterInfo *RI = STI.getRegisterInfo();
    if (RI->hasStackRealignment(MF)) {
      Align MaxAlignment = MFI.getMaxAlign();

      const RISCVInstrInfo *TII = STI.getInstrInfo();
      if (isInt<12>(-(int)MaxAlignment.value())) {
        BuildMI(MBB, MBBI, DL, TII->get(RISCV::ANDI), SPReg)
            .addReg(SPReg)
            .addImm(-(int)MaxAlignment.value())
            .setMIFlag(MachineInstr::FrameSetup);
      } else {
        unsigned ShiftAmount = Log2(MaxAlignment);
        Register VR =
            MF.getRegInfo().createVirtualRegister(&RISCV::GPRRegClass);
        BuildMI(MBB, MBBI, DL, TII->get(RISCV::SRLI), VR)
            .addReg(SPReg)
            .addImm(ShiftAmount)
            .setMIFlag(MachineInstr::FrameSetup);
        BuildMI(MBB, MBBI, DL, TII->get(RISCV::SLLI), SPReg)
            .addReg(VR)
            .addImm(ShiftAmount)
            .setMIFlag(MachineInstr::FrameSetup);
      }
      // FP will be used to restore the frame in the epilogue, so we need
      // another base register BP to record SP after re-alignment. SP will
      // track the current stack after allocating variable sized objects.
      if (hasBP(MF)) {
        // move BP, SP
        BuildMI(MBB, MBBI, DL, TII->get(RISCV::ADDI), BPReg)
            .addReg(SPReg)
            .addImm(0)
            .setMIFlag(MachineInstr::FrameSetup);
      }
    }
  }
}

void RISCVFrameLowering::emitEpilogue(MachineFunction &MF,
                                      MachineBasicBlock &MBB) const {
  const RISCVRegisterInfo *RI = STI.getRegisterInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  auto *RVFI = MF.getInfo<RISCVMachineFunctionInfo>();
  Register FPReg = getFPReg(STI);
  Register SPReg = getSPReg(STI);

  // All calls are tail calls in GHC calling conv, and functions have no
  // prologue/epilogue.
  if (MF.getFunction().getCallingConv() == CallingConv::GHC)
    return;

  // Get the insert location for the epilogue. If there were no terminators in
  // the block, get the last instruction.
  MachineBasicBlock::iterator MBBI = MBB.end();
  DebugLoc DL;
  if (!MBB.empty()) {
    MBBI = MBB.getLastNonDebugInstr();
    if (MBBI != MBB.end())
      DL = MBBI->getDebugLoc();

    MBBI = MBB.getFirstTerminator();

    // If callee-saved registers are saved via libcall, place stack adjustment
    // before this call.
    while (MBBI != MBB.begin() &&
           std::prev(MBBI)->getFlag(MachineInstr::FrameDestroy))
      --MBBI;
  }

  const auto &CSI = getUnmanagedCSI(MF, MFI.getCalleeSavedInfo());

  // Skip to before the restores of scalar callee-saved registers
  // FIXME: assumes exactly one instruction is used to restore each
  // callee-saved register.
  auto LastFrameDestroy = MBBI;
  if (!CSI.empty())
    LastFrameDestroy = std::prev(MBBI, CSI.size());

  uint64_t RealStackSize = getStackSizeWithRVVPadding(MF);
  uint64_t StackSize = RealStackSize - RVFI->getReservedSpillsSize();
  uint64_t FPOffset = RealStackSize - RVFI->getVarArgsSaveSize();
  uint64_t RVVStackSize = RVFI->getRVVStackSize();

  // Restore the stack pointer using the value of the frame pointer. Only
  // necessary if the stack pointer was modified, meaning the stack size is
  // unknown.
  //
  // In order to make sure the stack point is right through the EH region,
  // we also need to restore stack pointer from the frame pointer if we
  // don't preserve stack space within prologue/epilogue for outgoing variables,
  // normally it's just checking the variable sized object is present or not
  // is enough, but we also don't preserve that at prologue/epilogue when
  // have vector objects in stack.
  if (RI->hasStackRealignment(MF) || MFI.hasVarSizedObjects() ||
      !hasReservedCallFrame(MF)) {
    assert(hasFP(MF) && "frame pointer should not have been eliminated");
    RI->adjustReg(MBB, LastFrameDestroy, DL, SPReg, FPReg,
                  StackOffset::getFixed(-FPOffset),
                  MachineInstr::FrameDestroy, getStackAlign());
  } else {
    if (RVVStackSize)
      adjustStackForRVV(MF, MBB, LastFrameDestroy, DL, RVVStackSize,
                        MachineInstr::FrameDestroy);
  }

  uint64_t FirstSPAdjustAmount = getFirstSPAdjustAmount(MF);
  if (FirstSPAdjustAmount) {
    uint64_t SecondSPAdjustAmount =
        getStackSizeWithRVVPadding(MF) - FirstSPAdjustAmount;
    assert(SecondSPAdjustAmount > 0 &&
           "SecondSPAdjustAmount should be greater than zero");

    RI->adjustReg(MBB, LastFrameDestroy, DL, SPReg, SPReg,
                  StackOffset::getFixed(SecondSPAdjustAmount),
                  MachineInstr::FrameDestroy, getStackAlign());
  }

  if (FirstSPAdjustAmount)
    StackSize = FirstSPAdjustAmount;

  if (RVFI->isPushable(MF) && MBBI != MBB.end() &&
      MBBI->getOpcode() == RISCV::CM_POP) {
    // Use available stack adjustment in pop instruction to deallocate stack
    // space. Align the stack size down to a multiple of 16. This is needed for
    // RVE.
    // FIXME: Can we increase the stack size to a multiple of 16 instead?
    uint64_t Spimm = std::min(alignDown(StackSize, 16), (uint64_t)48);
    MBBI->getOperand(1).setImm(Spimm);
    StackSize -= Spimm;
  }

  // Deallocate stack
  if (StackSize != 0) {
    RI->adjustReg(MBB, MBBI, DL, SPReg, SPReg, StackOffset::getFixed(StackSize),
                  MachineInstr::FrameDestroy, getStackAlign());
  }

  // Emit epilogue for shadow call stack.
  emitSCSEpilogue(MF, MBB, MBBI, DL);
}

StackOffset
RISCVFrameLowering::getFrameIndexReference(const MachineFunction &MF, int FI,
                                           Register &FrameReg) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterInfo *RI = MF.getSubtarget().getRegisterInfo();
  const auto *RVFI = MF.getInfo<RISCVMachineFunctionInfo>();

  // Callee-saved registers should be referenced relative to the stack
  // pointer (positive offset), otherwise use the frame pointer (negative
  // offset).
  const auto &CSI = getUnmanagedCSI(MF, MFI.getCalleeSavedInfo());
  int MinCSFI = 0;
  int MaxCSFI = -1;
  StackOffset Offset;
  auto StackID = MFI.getStackID(FI);

  assert((StackID == TargetStackID::Default ||
          StackID == TargetStackID::ScalableVector) &&
         "Unexpected stack ID for the frame object.");
  if (StackID == TargetStackID::Default) {
    assert(getOffsetOfLocalArea() == 0 && "LocalAreaOffset is not 0!");
    Offset = StackOffset::getFixed(MFI.getObjectOffset(FI) +
                                   MFI.getOffsetAdjustment());
  } else if (StackID == TargetStackID::ScalableVector) {
    Offset = StackOffset::getScalable(MFI.getObjectOffset(FI));
  }

  uint64_t FirstSPAdjustAmount = getFirstSPAdjustAmount(MF);

  if (CSI.size()) {
    MinCSFI = CSI[0].getFrameIdx();
    MaxCSFI = CSI[CSI.size() - 1].getFrameIdx();
  }

  if (FI >= MinCSFI && FI <= MaxCSFI) {
    FrameReg = RISCV::X2;

    if (FirstSPAdjustAmount)
      Offset += StackOffset::getFixed(FirstSPAdjustAmount);
    else
      Offset += StackOffset::getFixed(getStackSizeWithRVVPadding(MF));
    return Offset;
  }

  if (RI->hasStackRealignment(MF) && !MFI.isFixedObjectIndex(FI)) {
    // If the stack was realigned, the frame pointer is set in order to allow
    // SP to be restored, so we need another base register to record the stack
    // after realignment.
    // |--------------------------| -- <-- FP
    // | callee-allocated save    | | <----|
    // | area for register varargs| |      |
    // |--------------------------| |      |
    // | callee-saved registers   | |      |
    // |--------------------------| --     |
    // | realignment (the size of | |      |
    // | this area is not counted | |      |
    // | in MFI.getStackSize())   | |      |
    // |--------------------------| --     |-- MFI.getStackSize()
    // | RVV alignment padding    | |      |
    // | (not counted in          | |      |
    // | MFI.getStackSize() but   | |      |
    // | counted in               | |      |
    // | RVFI.getRVVStackSize())  | |      |
    // |--------------------------| --     |
    // | RVV objects              | |      |
    // | (not counted in          | |      |
    // | MFI.getStackSize())      | |      |
    // |--------------------------| --     |
    // | padding before RVV       | |      |
    // | (not counted in          | |      |
    // | MFI.getStackSize() or in | |      |
    // | RVFI.getRVVStackSize())  | |      |
    // |--------------------------| --     |
    // | scalar local variables   | | <----'
    // |--------------------------| -- <-- BP (if var sized objects present)
    // | VarSize objects          | |
    // |--------------------------| -- <-- SP
    if (hasBP(MF)) {
      FrameReg = RISCVABI::getBPReg();
    } else {
      // VarSize objects must be empty in this case!
      assert(!MFI.hasVarSizedObjects());
      FrameReg = RISCV::X2;
    }
  } else {
    FrameReg = RI->getFrameRegister(MF);
  }

  if (FrameReg == getFPReg(STI)) {
    Offset += StackOffset::getFixed(RVFI->getVarArgsSaveSize());
    // When using FP to access scalable vector objects, we need to minus
    // the frame size.
    //
    // |--------------------------| -- <-- FP
    // | callee-allocated save    | |
    // | area for register varargs| |
    // |--------------------------| |
    // | callee-saved registers   | |
    // |--------------------------| | MFI.getStackSize()
    // | scalar local variables   | |
    // |--------------------------| -- (Offset of RVV objects is from here.)
    // | RVV objects              |
    // |--------------------------|
    // | VarSize objects          |
    // |--------------------------| <-- SP
    if (MFI.getStackID(FI) == TargetStackID::ScalableVector) {
      assert(!RI->hasStackRealignment(MF) &&
             "Can't index across variable sized realign");
      // We don't expect any extra RVV alignment padding, as the stack size
      // and RVV object sections should be correct aligned in their own
      // right.
      assert(MFI.getStackSize() == getStackSizeWithRVVPadding(MF) &&
             "Inconsistent stack layout");
      Offset -= StackOffset::getFixed(MFI.getStackSize());
    }
    return Offset;
  }

  // This case handles indexing off both SP and BP.
  // If indexing off SP, there must not be any var sized objects
  assert(FrameReg == RISCVABI::getBPReg() || !MFI.hasVarSizedObjects());

  // When using SP to access frame objects, we need to add RVV stack size.
  //
  // |--------------------------| -- <-- FP
  // | callee-allocated save    | | <----|
  // | area for register varargs| |      |
  // |--------------------------| |      |
  // | callee-saved registers   | |      |
  // |--------------------------| --     |
  // | RVV alignment padding    | |      |
  // | (not counted in          | |      |
  // | MFI.getStackSize() but   | |      |
  // | counted in               | |      |
  // | RVFI.getRVVStackSize())  | |      |
  // |--------------------------| --     |
  // | RVV objects              | |      |-- MFI.getStackSize()
  // | (not counted in          | |      |
  // | MFI.getStackSize())      | |      |
  // |--------------------------| --     |
  // | padding before RVV       | |      |
  // | (not counted in          | |      |
  // | MFI.getStackSize())      | |      |
  // |--------------------------| --     |
  // | scalar local variables   | | <----'
  // |--------------------------| -- <-- BP (if var sized objects present)
  // | VarSize objects          | |
  // |--------------------------| -- <-- SP
  //
  // The total amount of padding surrounding RVV objects is described by
  // RVV->getRVVPadding() and it can be zero. It allows us to align the RVV
  // objects to the required alignment.
  if (MFI.getStackID(FI) == TargetStackID::Default) {
    if (MFI.isFixedObjectIndex(FI)) {
      assert(!RI->hasStackRealignment(MF) &&
             "Can't index across variable sized realign");
      Offset += StackOffset::get(getStackSizeWithRVVPadding(MF),
                                 RVFI->getRVVStackSize());
    } else {
      Offset += StackOffset::getFixed(MFI.getStackSize());
    }
  } else if (MFI.getStackID(FI) == TargetStackID::ScalableVector) {
    // Ensure the base of the RVV stack is correctly aligned: add on the
    // alignment padding.
    int ScalarLocalVarSize = MFI.getStackSize() -
                             RVFI->getCalleeSavedStackSize() -
                             RVFI->getRVPushStackSize() -
                             RVFI->getVarArgsSaveSize() + RVFI->getRVVPadding();
    Offset += StackOffset::get(ScalarLocalVarSize, RVFI->getRVVStackSize());
  }
  return Offset;
}

void RISCVFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                              BitVector &SavedRegs,
                                              RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  // Unconditionally spill RA and FP only if the function uses a frame
  // pointer.
  if (hasFP(MF)) {
    SavedRegs.set(RISCV::X1);
    SavedRegs.set(RISCV::X8);
  }
  // Mark BP as used if function has dedicated base pointer.
  if (hasBP(MF))
    SavedRegs.set(RISCVABI::getBPReg());

  // When using cm.push/pop we must save X27 if we save X26.
  auto *RVFI = MF.getInfo<RISCVMachineFunctionInfo>();
  if (RVFI->isPushable(MF) && SavedRegs.test(RISCV::X26))
    SavedRegs.set(RISCV::X27);
}

std::pair<int64_t, Align>
RISCVFrameLowering::assignRVVStackObjectOffsets(MachineFunction &MF) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  // Create a buffer of RVV objects to allocate.
  SmallVector<int, 8> ObjectsToAllocate;
  auto pushRVVObjects = [&](int FIBegin, int FIEnd) {
    for (int I = FIBegin, E = FIEnd; I != E; ++I) {
      unsigned StackID = MFI.getStackID(I);
      if (StackID != TargetStackID::ScalableVector)
        continue;
      if (MFI.isDeadObjectIndex(I))
        continue;

      ObjectsToAllocate.push_back(I);
    }
  };
  // First push RVV Callee Saved object, then push RVV stack object
  std::vector<CalleeSavedInfo> &CSI = MF.getFrameInfo().getCalleeSavedInfo();
  const auto &RVVCSI = getRVVCalleeSavedInfo(MF, CSI);
  if (!RVVCSI.empty())
    pushRVVObjects(RVVCSI[0].getFrameIdx(),
                   RVVCSI[RVVCSI.size() - 1].getFrameIdx() + 1);
  pushRVVObjects(0, MFI.getObjectIndexEnd() - RVVCSI.size());

  // The minimum alignment is 16 bytes.
  Align RVVStackAlign(16);
  const auto &ST = MF.getSubtarget<RISCVSubtarget>();

  if (!ST.hasVInstructions()) {
    assert(ObjectsToAllocate.empty() &&
           "Can't allocate scalable-vector objects without V instructions");
    return std::make_pair(0, RVVStackAlign);
  }

  // Allocate all RVV locals and spills
  int64_t Offset = 0;
  for (int FI : ObjectsToAllocate) {
    // ObjectSize in bytes.
    int64_t ObjectSize = MFI.getObjectSize(FI);
    auto ObjectAlign = std::max(Align(8), MFI.getObjectAlign(FI));
    // If the data type is the fractional vector type, reserve one vector
    // register for it.
    if (ObjectSize < 8)
      ObjectSize = 8;
    Offset = alignTo(Offset + ObjectSize, ObjectAlign);
    MFI.setObjectOffset(FI, -Offset);
    // Update the maximum alignment of the RVV stack section
    RVVStackAlign = std::max(RVVStackAlign, ObjectAlign);
  }

  // Ensure the alignment of the RVV stack. Since we want the most-aligned
  // object right at the bottom (i.e., any padding at the top of the frame),
  // readjust all RVV objects down by the alignment padding.
  uint64_t StackSize = Offset;
  if (auto AlignmentPadding = offsetToAlignment(StackSize, RVVStackAlign)) {
    StackSize += AlignmentPadding;
    for (int FI : ObjectsToAllocate)
      MFI.setObjectOffset(FI, MFI.getObjectOffset(FI) - AlignmentPadding);
  }

  return std::make_pair(StackSize, RVVStackAlign);
}

static unsigned getScavSlotsNumForRVV(MachineFunction &MF) {
  // For RVV spill, scalable stack offsets computing requires up to two scratch
  // registers
  static constexpr unsigned ScavSlotsNumRVVSpillScalableObject = 2;

  // For RVV spill, non-scalable stack offsets computing requires up to one
  // scratch register.
  static constexpr unsigned ScavSlotsNumRVVSpillNonScalableObject = 1;

  // ADDI instruction's destination register can be used for computing
  // offsets. So Scalable stack offsets require up to one scratch register.
  static constexpr unsigned ScavSlotsADDIScalableObject = 1;

  static constexpr unsigned MaxScavSlotsNumKnown =
      std::max({ScavSlotsADDIScalableObject, ScavSlotsNumRVVSpillScalableObject,
                ScavSlotsNumRVVSpillNonScalableObject});

  unsigned MaxScavSlotsNum = 0;
  if (!MF.getSubtarget<RISCVSubtarget>().hasVInstructions())
    return false;
  for (const MachineBasicBlock &MBB : MF)
    for (const MachineInstr &MI : MBB) {
      bool IsRVVSpill = RISCV::isRVVSpill(MI);
      for (auto &MO : MI.operands()) {
        if (!MO.isFI())
          continue;
        bool IsScalableVectorID = MF.getFrameInfo().getStackID(MO.getIndex()) ==
                                  TargetStackID::ScalableVector;
        if (IsRVVSpill) {
          MaxScavSlotsNum = std::max(
              MaxScavSlotsNum, IsScalableVectorID
                                   ? ScavSlotsNumRVVSpillScalableObject
                                   : ScavSlotsNumRVVSpillNonScalableObject);
        } else if (MI.getOpcode() == RISCV::ADDI && IsScalableVectorID) {
          MaxScavSlotsNum =
              std::max(MaxScavSlotsNum, ScavSlotsADDIScalableObject);
        }
      }
      if (MaxScavSlotsNum == MaxScavSlotsNumKnown)
        return MaxScavSlotsNumKnown;
    }
  return MaxScavSlotsNum;
}

static bool hasRVVFrameObject(const MachineFunction &MF) {
  // Originally, the function will scan all the stack objects to check whether
  // if there is any scalable vector object on the stack or not. However, it
  // causes errors in the register allocator. In issue 53016, it returns false
  // before RA because there is no RVV stack objects. After RA, it returns true
  // because there are spilling slots for RVV values during RA. It will not
  // reserve BP during register allocation and generate BP access in the PEI
  // pass due to the inconsistent behavior of the function.
  //
  // The function is changed to use hasVInstructions() as the return value. It
  // is not precise, but it can make the register allocation correct.
  //
  // FIXME: Find a better way to make the decision or revisit the solution in
  // D103622.
  //
  // Refer to https://github.com/llvm/llvm-project/issues/53016.
  return MF.getSubtarget<RISCVSubtarget>().hasVInstructions();
}

static unsigned estimateFunctionSizeInBytes(const MachineFunction &MF,
                                            const RISCVInstrInfo &TII) {
  unsigned FnSize = 0;
  for (auto &MBB : MF) {
    for (auto &MI : MBB) {
      // Far branches over 20-bit offset will be relaxed in branch relaxation
      // pass. In the worst case, conditional branches will be relaxed into
      // the following instruction sequence. Unconditional branches are
      // relaxed in the same way, with the exception that there is no first
      // branch instruction.
      //
      //        foo
      //        bne     t5, t6, .rev_cond # `TII->getInstSizeInBytes(MI)` bytes
      //        sd      s11, 0(sp)        # 4 bytes, or 2 bytes in RVC
      //        jump    .restore, s11     # 8 bytes
      // .rev_cond
      //        bar
      //        j       .dest_bb          # 4 bytes, or 2 bytes in RVC
      // .restore:
      //        ld      s11, 0(sp)        # 4 bytes, or 2 bytes in RVC
      // .dest:
      //        baz
      if (MI.isConditionalBranch())
        FnSize += TII.getInstSizeInBytes(MI);
      if (MI.isConditionalBranch() || MI.isUnconditionalBranch()) {
        if (MF.getSubtarget<RISCVSubtarget>().hasStdExtCOrZca())
          FnSize += 2 + 8 + 2 + 2;
        else
          FnSize += 4 + 8 + 4 + 4;
        continue;
      }

      FnSize += TII.getInstSizeInBytes(MI);
    }
  }
  return FnSize;
}

void RISCVFrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
  const RISCVRegisterInfo *RegInfo =
      MF.getSubtarget<RISCVSubtarget>().getRegisterInfo();
  const RISCVInstrInfo *TII = MF.getSubtarget<RISCVSubtarget>().getInstrInfo();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterClass *RC = &RISCV::GPRRegClass;
  auto *RVFI = MF.getInfo<RISCVMachineFunctionInfo>();

  int64_t RVVStackSize;
  Align RVVStackAlign;
  std::tie(RVVStackSize, RVVStackAlign) = assignRVVStackObjectOffsets(MF);

  RVFI->setRVVStackSize(RVVStackSize);
  RVFI->setRVVStackAlign(RVVStackAlign);

  if (hasRVVFrameObject(MF)) {
    // Ensure the entire stack is aligned to at least the RVV requirement: some
    // scalable-vector object alignments are not considered by the
    // target-independent code.
    MFI.ensureMaxAlignment(RVVStackAlign);
  }

  unsigned ScavSlotsNum = 0;

  // estimateStackSize has been observed to under-estimate the final stack
  // size, so give ourselves wiggle-room by checking for stack size
  // representable an 11-bit signed field rather than 12-bits.
  if (!isInt<11>(MFI.estimateStackSize(MF)))
    ScavSlotsNum = 1;

  // Far branches over 20-bit offset require a spill slot for scratch register.
  bool IsLargeFunction = !isInt<20>(estimateFunctionSizeInBytes(MF, *TII));
  if (IsLargeFunction)
    ScavSlotsNum = std::max(ScavSlotsNum, 1u);

  // RVV loads & stores have no capacity to hold the immediate address offsets
  // so we must always reserve an emergency spill slot if the MachineFunction
  // contains any RVV spills.
  ScavSlotsNum = std::max(ScavSlotsNum, getScavSlotsNumForRVV(MF));

  for (unsigned I = 0; I < ScavSlotsNum; I++) {
    int FI = MFI.CreateStackObject(RegInfo->getSpillSize(*RC),
                                   RegInfo->getSpillAlign(*RC), false);
    RS->addScavengingFrameIndex(FI);

    if (IsLargeFunction && RVFI->getBranchRelaxationScratchFrameIndex() == -1)
      RVFI->setBranchRelaxationScratchFrameIndex(FI);
  }

  unsigned Size = RVFI->getReservedSpillsSize();
  for (const auto &Info : MFI.getCalleeSavedInfo()) {
    int FrameIdx = Info.getFrameIdx();
    if (FrameIdx < 0 || MFI.getStackID(FrameIdx) != TargetStackID::Default)
      continue;

    Size += MFI.getObjectSize(FrameIdx);
  }
  RVFI->setCalleeSavedStackSize(Size);
}

// Not preserve stack space within prologue for outgoing variables when the
// function contains variable size objects or there are vector objects accessed
// by the frame pointer.
// Let eliminateCallFramePseudoInstr preserve stack space for it.
bool RISCVFrameLowering::hasReservedCallFrame(const MachineFunction &MF) const {
  return !MF.getFrameInfo().hasVarSizedObjects() &&
         !(hasFP(MF) && hasRVVFrameObject(MF));
}

// Eliminate ADJCALLSTACKDOWN, ADJCALLSTACKUP pseudo instructions.
MachineBasicBlock::iterator RISCVFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const {
  Register SPReg = RISCV::X2;
  DebugLoc DL = MI->getDebugLoc();

  if (!hasReservedCallFrame(MF)) {
    // If space has not been reserved for a call frame, ADJCALLSTACKDOWN and
    // ADJCALLSTACKUP must be converted to instructions manipulating the stack
    // pointer. This is necessary when there is a variable length stack
    // allocation (e.g. alloca), which means it's not possible to allocate
    // space for outgoing arguments from within the function prologue.
    int64_t Amount = MI->getOperand(0).getImm();

    if (Amount != 0) {
      // Ensure the stack remains aligned after adjustment.
      Amount = alignSPAdjust(Amount);

      if (MI->getOpcode() == RISCV::ADJCALLSTACKDOWN)
        Amount = -Amount;

      const RISCVRegisterInfo &RI = *STI.getRegisterInfo();
      RI.adjustReg(MBB, MI, DL, SPReg, SPReg, StackOffset::getFixed(Amount),
                   MachineInstr::NoFlags, getStackAlign());
    }
  }

  return MBB.erase(MI);
}

// We would like to split the SP adjustment to reduce prologue/epilogue
// as following instructions. In this way, the offset of the callee saved
// register could fit in a single store. Supposed that the first sp adjust
// amount is 2032.
//   add     sp,sp,-2032
//   sw      ra,2028(sp)
//   sw      s0,2024(sp)
//   sw      s1,2020(sp)
//   sw      s3,2012(sp)
//   sw      s4,2008(sp)
//   add     sp,sp,-64
uint64_t
RISCVFrameLowering::getFirstSPAdjustAmount(const MachineFunction &MF) const {
  const auto *RVFI = MF.getInfo<RISCVMachineFunctionInfo>();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const std::vector<CalleeSavedInfo> &CSI = MFI.getCalleeSavedInfo();
  uint64_t StackSize = getStackSizeWithRVVPadding(MF);

  // Disable SplitSPAdjust if save-restore libcall is used. The callee-saved
  // registers will be pushed by the save-restore libcalls, so we don't have to
  // split the SP adjustment in this case.
  if (RVFI->getReservedSpillsSize())
    return 0;

  // Return the FirstSPAdjustAmount if the StackSize can not fit in a signed
  // 12-bit and there exists a callee-saved register needing to be pushed.
  if (!isInt<12>(StackSize) && (CSI.size() > 0)) {
    // FirstSPAdjustAmount is chosen at most as (2048 - StackAlign) because
    // 2048 will cause sp = sp + 2048 in the epilogue to be split into multiple
    // instructions. Offsets smaller than 2048 can fit in a single load/store
    // instruction, and we have to stick with the stack alignment. 2048 has
    // 16-byte alignment. The stack alignment for RV32 and RV64 is 16 and for
    // RV32E it is 4. So (2048 - StackAlign) will satisfy the stack alignment.
    const uint64_t StackAlign = getStackAlign().value();

    // Amount of (2048 - StackAlign) will prevent callee saved and restored
    // instructions be compressed, so try to adjust the amount to the largest
    // offset that stack compression instructions accept when target supports
    // compression instructions.
    if (STI.hasStdExtCOrZca()) {
      // The compression extensions may support the following instructions:
      // riscv32: c.lwsp rd, offset[7:2] => 2^(6 + 2)
      //          c.swsp rs2, offset[7:2] => 2^(6 + 2)
      //          c.flwsp rd, offset[7:2] => 2^(6 + 2)
      //          c.fswsp rs2, offset[7:2] => 2^(6 + 2)
      // riscv64: c.ldsp rd, offset[8:3] => 2^(6 + 3)
      //          c.sdsp rs2, offset[8:3] => 2^(6 + 3)
      //          c.fldsp rd, offset[8:3] => 2^(6 + 3)
      //          c.fsdsp rs2, offset[8:3] => 2^(6 + 3)
      const uint64_t RVCompressLen = STI.getXLen() * 8;
      // Compared with amount (2048 - StackAlign), StackSize needs to
      // satisfy the following conditions to avoid using more instructions
      // to adjust the sp after adjusting the amount, such as
      // StackSize meets the condition (StackSize <= 2048 + RVCompressLen),
      // case1: Amount is 2048 - StackAlign: use addi + addi to adjust sp.
      // case2: Amount is RVCompressLen: use addi + addi to adjust sp.
      auto CanCompress = [&](uint64_t CompressLen) -> bool {
        if (StackSize <= 2047 + CompressLen ||
            (StackSize > 2048 * 2 - StackAlign &&
             StackSize <= 2047 * 2 + CompressLen) ||
            StackSize > 2048 * 3 - StackAlign)
          return true;

        return false;
      };
      // In the epilogue, addi sp, sp, 496 is used to recover the sp and it
      // can be compressed(C.ADDI16SP, offset can be [-512, 496]), but
      // addi sp, sp, 512 can not be compressed. So try to use 496 first.
      const uint64_t ADDI16SPCompressLen = 496;
      if (STI.is64Bit() && CanCompress(ADDI16SPCompressLen))
        return ADDI16SPCompressLen;
      if (CanCompress(RVCompressLen))
        return RVCompressLen;
    }
    return 2048 - StackAlign;
  }
  return 0;
}

bool RISCVFrameLowering::assignCalleeSavedSpillSlots(
    MachineFunction &MF, const TargetRegisterInfo *TRI,
    std::vector<CalleeSavedInfo> &CSI, unsigned &MinCSFrameIndex,
    unsigned &MaxCSFrameIndex) const {
  // Early exit if no callee saved registers are modified!
  if (CSI.empty())
    return true;

  auto *RVFI = MF.getInfo<RISCVMachineFunctionInfo>();

  if (RVFI->isPushable(MF)) {
    // Determine how many GPRs we need to push and save it to RVFI.
    Register MaxReg = getMaxPushPopReg(MF, CSI);
    if (MaxReg != RISCV::NoRegister) {
      auto [RegEnc, PushedRegNum] = getPushPopEncodingAndNum(MaxReg);
      RVFI->setRVPushRegs(PushedRegNum);
      RVFI->setRVPushStackSize(alignTo((STI.getXLen() / 8) * PushedRegNum, 16));

      // Use encoded number to represent registers to spill.
      RVFI->setRVPushRlist(RegEnc);
    }
  }

  MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterInfo *RegInfo = MF.getSubtarget().getRegisterInfo();

  for (auto &CS : CSI) {
    unsigned Reg = CS.getReg();
    const TargetRegisterClass *RC = RegInfo->getMinimalPhysRegClass(Reg);
    unsigned Size = RegInfo->getSpillSize(*RC);

    // This might need a fixed stack slot.
    if (RVFI->useSaveRestoreLibCalls(MF) || RVFI->isPushable(MF)) {
      const auto *FII = llvm::find_if(
          FixedCSRFIMap, [&](auto P) { return P.first == CS.getReg(); });
      if (FII != std::end(FixedCSRFIMap)) {
        int64_t Offset;
        if (RVFI->isPushable(MF))
          Offset = -((FII->second + RVFI->getRVPushRegs() + 1) * (int64_t)Size);
        else
          Offset = FII->second * (int64_t)Size;

        int FrameIdx = MFI.CreateFixedSpillStackObject(Size, Offset);
        assert(FrameIdx < 0);
        CS.setFrameIdx(FrameIdx);
        continue;
      }
    }

    // Not a fixed slot.
    Align Alignment = RegInfo->getSpillAlign(*RC);
    // We may not be able to satisfy the desired alignment specification of
    // the TargetRegisterClass if the stack alignment is smaller. Use the
    // min.
    Alignment = std::min(Alignment, getStackAlign());
    int FrameIdx = MFI.CreateStackObject(Size, Alignment, true);
    if ((unsigned)FrameIdx < MinCSFrameIndex)
      MinCSFrameIndex = FrameIdx;
    if ((unsigned)FrameIdx > MaxCSFrameIndex)
      MaxCSFrameIndex = FrameIdx;
    CS.setFrameIdx(FrameIdx);
  }

  // Allocate a fixed object that covers the full push or libcall size.
  if (RVFI->isPushable(MF)) {
    if (int64_t PushSize = RVFI->getRVPushStackSize())
      MFI.CreateFixedSpillStackObject(PushSize, -PushSize);
  } else if (int LibCallRegs = getLibCallID(MF, CSI) + 1) {
    int64_t LibCallFrameSize =
        alignTo((STI.getXLen() / 8) * LibCallRegs, getStackAlign());
    MFI.CreateFixedSpillStackObject(LibCallFrameSize, -LibCallFrameSize);
  }

  return true;
}

bool RISCVFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return true;

  MachineFunction *MF = MBB.getParent();
  const TargetInstrInfo &TII = *MF->getSubtarget().getInstrInfo();
  DebugLoc DL;
  if (MI != MBB.end() && !MI->isDebugInstr())
    DL = MI->getDebugLoc();

  // Emit CM.PUSH with base SPimm & evaluate Push stack
  RISCVMachineFunctionInfo *RVFI = MF->getInfo<RISCVMachineFunctionInfo>();
  if (RVFI->isPushable(*MF)) {
    unsigned PushedRegNum = RVFI->getRVPushRegs();
    if (PushedRegNum > 0) {
      // Use encoded number to represent registers to spill.
      int RegEnc = RVFI->getRVPushRlist();
      MachineInstrBuilder PushBuilder =
          BuildMI(MBB, MI, DL, TII.get(RISCV::CM_PUSH))
              .setMIFlag(MachineInstr::FrameSetup);
      PushBuilder.addImm((int64_t)RegEnc);
      PushBuilder.addImm(0);

      for (unsigned i = 0; i < PushedRegNum; i++)
        PushBuilder.addUse(FixedCSRFIMap[i].first, RegState::Implicit);
    }
  } else if (const char *SpillLibCall = getSpillLibCallName(*MF, CSI)) {
    // Add spill libcall via non-callee-saved register t0.
    BuildMI(MBB, MI, DL, TII.get(RISCV::PseudoCALLReg), RISCV::X5)
        .addExternalSymbol(SpillLibCall, RISCVII::MO_CALL)
        .setMIFlag(MachineInstr::FrameSetup);

    // Add registers spilled in libcall as liveins.
    for (auto &CS : CSI)
      MBB.addLiveIn(CS.getReg());
  }

  // Manually spill values not spilled by libcall & Push/Pop.
  const auto &UnmanagedCSI = getUnmanagedCSI(*MF, CSI);
  const auto &RVVCSI = getRVVCalleeSavedInfo(*MF, CSI);

  auto storeRegToStackSlot = [&](decltype(UnmanagedCSI) CSInfo) {
    for (auto &CS : CSInfo) {
      // Insert the spill to the stack frame.
      Register Reg = CS.getReg();
      const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
      TII.storeRegToStackSlot(MBB, MI, Reg, !MBB.isLiveIn(Reg),
                              CS.getFrameIdx(), RC, TRI, Register());
    }
  };
  storeRegToStackSlot(UnmanagedCSI);
  storeRegToStackSlot(RVVCSI);

  return true;
}

void RISCVFrameLowering::emitCalleeSavedRVVPrologCFI(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, bool HasFP) const {
  MachineFunction *MF = MBB.getParent();
  const MachineFrameInfo &MFI = MF->getFrameInfo();
  RISCVMachineFunctionInfo *RVFI = MF->getInfo<RISCVMachineFunctionInfo>();
  const TargetInstrInfo &TII = *STI.getInstrInfo();
  DebugLoc DL = MBB.findDebugLoc(MI);

  const auto &RVVCSI = getRVVCalleeSavedInfo(*MF, MFI.getCalleeSavedInfo());
  if (RVVCSI.empty())
    return;

  uint64_t FixedSize = getStackSizeWithRVVPadding(*MF);
  if (!HasFP) {
    uint64_t ScalarLocalVarSize =
        MFI.getStackSize() - RVFI->getCalleeSavedStackSize() -
        RVFI->getRVPushStackSize() - RVFI->getVarArgsSaveSize() +
        RVFI->getRVVPadding();
    FixedSize -= ScalarLocalVarSize;
  }

  for (auto &CS : RVVCSI) {
    // Insert the spill to the stack frame.
    int FI = CS.getFrameIdx();
    if (FI >= 0 && MFI.getStackID(FI) == TargetStackID::ScalableVector) {
      unsigned CFIIndex = MF->addFrameInst(
          createDefCFAOffset(*STI.getRegisterInfo(), CS.getReg(), -FixedSize,
                             MFI.getObjectOffset(FI) / 8));
      BuildMI(MBB, MI, DL, TII.get(TargetOpcode::CFI_INSTRUCTION))
          .addCFIIndex(CFIIndex)
          .setMIFlag(MachineInstr::FrameSetup);
    }
  }
}

bool RISCVFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  if (CSI.empty())
    return true;

  MachineFunction *MF = MBB.getParent();
  const TargetInstrInfo &TII = *MF->getSubtarget().getInstrInfo();
  DebugLoc DL;
  if (MI != MBB.end() && !MI->isDebugInstr())
    DL = MI->getDebugLoc();

  // Manually restore values not restored by libcall & Push/Pop.
  // Reverse the restore order in epilog.  In addition, the return
  // address will be restored first in the epilogue. It increases
  // the opportunity to avoid the load-to-use data hazard between
  // loading RA and return by RA.  loadRegFromStackSlot can insert
  // multiple instructions.
  const auto &UnmanagedCSI = getUnmanagedCSI(*MF, CSI);
  const auto &RVVCSI = getRVVCalleeSavedInfo(*MF, CSI);

  auto loadRegFromStackSlot = [&](decltype(UnmanagedCSI) CSInfo) {
    for (auto &CS : CSInfo) {
      Register Reg = CS.getReg();
      const TargetRegisterClass *RC = TRI->getMinimalPhysRegClass(Reg);
      TII.loadRegFromStackSlot(MBB, MI, Reg, CS.getFrameIdx(), RC, TRI,
                               Register());
      assert(MI != MBB.begin() &&
             "loadRegFromStackSlot didn't insert any code!");
    }
  };
  loadRegFromStackSlot(RVVCSI);
  loadRegFromStackSlot(UnmanagedCSI);

  RISCVMachineFunctionInfo *RVFI = MF->getInfo<RISCVMachineFunctionInfo>();
  if (RVFI->isPushable(*MF)) {
    int RegEnc = RVFI->getRVPushRlist();
    if (RegEnc != llvm::RISCVZC::RLISTENCODE::INVALID_RLIST) {
      MachineInstrBuilder PopBuilder =
          BuildMI(MBB, MI, DL, TII.get(RISCV::CM_POP))
              .setMIFlag(MachineInstr::FrameDestroy);
      // Use encoded number to represent registers to restore.
      PopBuilder.addImm(RegEnc);
      PopBuilder.addImm(0);

      for (unsigned i = 0; i < RVFI->getRVPushRegs(); i++)
        PopBuilder.addDef(FixedCSRFIMap[i].first, RegState::ImplicitDefine);
    }
  } else {
    const char *RestoreLibCall = getRestoreLibCallName(*MF, CSI);
    if (RestoreLibCall) {
      // Add restore libcall via tail call.
      MachineBasicBlock::iterator NewMI =
          BuildMI(MBB, MI, DL, TII.get(RISCV::PseudoTAIL))
              .addExternalSymbol(RestoreLibCall, RISCVII::MO_CALL)
              .setMIFlag(MachineInstr::FrameDestroy);

      // Remove trailing returns, since the terminator is now a tail call to the
      // restore function.
      if (MI != MBB.end() && MI->getOpcode() == RISCV::PseudoRET) {
        NewMI->copyImplicitOps(*MF, *MI);
        MI->eraseFromParent();
      }
    }
  }
  return true;
}

bool RISCVFrameLowering::enableShrinkWrapping(const MachineFunction &MF) const {
  // Keep the conventional code flow when not optimizing.
  if (MF.getFunction().hasOptNone())
    return false;

  return true;
}

bool RISCVFrameLowering::canUseAsPrologue(const MachineBasicBlock &MBB) const {
  MachineBasicBlock *TmpMBB = const_cast<MachineBasicBlock *>(&MBB);
  const MachineFunction *MF = MBB.getParent();
  const auto *RVFI = MF->getInfo<RISCVMachineFunctionInfo>();

  if (!RVFI->useSaveRestoreLibCalls(*MF))
    return true;

  // Inserting a call to a __riscv_save libcall requires the use of the register
  // t0 (X5) to hold the return address. Therefore if this register is already
  // used we can't insert the call.

  RegScavenger RS;
  RS.enterBasicBlock(*TmpMBB);
  return !RS.isRegUsed(RISCV::X5);
}

bool RISCVFrameLowering::canUseAsEpilogue(const MachineBasicBlock &MBB) const {
  const MachineFunction *MF = MBB.getParent();
  MachineBasicBlock *TmpMBB = const_cast<MachineBasicBlock *>(&MBB);
  const auto *RVFI = MF->getInfo<RISCVMachineFunctionInfo>();

  if (!RVFI->useSaveRestoreLibCalls(*MF))
    return true;

  // Using the __riscv_restore libcalls to restore CSRs requires a tail call.
  // This means if we still need to continue executing code within this function
  // the restore cannot take place in this basic block.

  if (MBB.succ_size() > 1)
    return false;

  MachineBasicBlock *SuccMBB =
      MBB.succ_empty() ? TmpMBB->getFallThrough() : *MBB.succ_begin();

  // Doing a tail call should be safe if there are no successors, because either
  // we have a returning block or the end of the block is unreachable, so the
  // restore will be eliminated regardless.
  if (!SuccMBB)
    return true;

  // The successor can only contain a return, since we would effectively be
  // replacing the successor with our own tail return at the end of our block.
  return SuccMBB->isReturnBlock() && SuccMBB->size() == 1;
}

bool RISCVFrameLowering::isSupportedStackID(TargetStackID::Value ID) const {
  switch (ID) {
  case TargetStackID::Default:
  case TargetStackID::ScalableVector:
    return true;
  case TargetStackID::NoAlloc:
  case TargetStackID::SGPRSpill:
  case TargetStackID::WasmLocal:
    return false;
  }
  llvm_unreachable("Invalid TargetStackID::Value");
}

TargetStackID::Value RISCVFrameLowering::getStackIDForScalableVectors() const {
  return TargetStackID::ScalableVector;
}
