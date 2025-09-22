//===- X86CompressEVEX.cpp ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass compresses instructions from EVEX space to legacy/VEX/EVEX space
// when possible in order to reduce code size or facilitate HW decoding.
//
// Possible compression:
//   a. AVX512 instruction (EVEX) -> AVX instruction (VEX)
//   b. Promoted instruction (EVEX) -> pre-promotion instruction (legacy/VEX)
//   c. NDD (EVEX) -> non-NDD (legacy)
//   d. NF_ND (EVEX) -> NF (EVEX)
//   e. NonNF (EVEX) -> NF (EVEX)
//
// Compression a, b and c can always reduce code size, with some exceptions
// such as promoted 16-bit CRC32 which is as long as the legacy version.
//
// legacy:
//   crc32w %si, %eax ## encoding: [0x66,0xf2,0x0f,0x38,0xf1,0xc6]
// promoted:
//   crc32w %si, %eax ## encoding: [0x62,0xf4,0x7d,0x08,0xf1,0xc6]
//
// From performance perspective, these should be same (same uops and same EXE
// ports). From a FMV perspective, an older legacy encoding is preferred b/c it
// can execute in more places (broader HW install base). So we will still do
// the compression.
//
// Compression d can help hardware decode (HW may skip reading the NDD
// register) although the instruction length remains unchanged.
//
// Compression e can help hardware skip updating EFLAGS although the instruction
// length remains unchanged.
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/X86BaseInfo.h"
#include "MCTargetDesc/X86InstComments.h"
#include "X86.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/MC/MCInstrDesc.h"
#include "llvm/Pass.h"
#include <atomic>
#include <cassert>
#include <cstdint>

using namespace llvm;

#define COMP_EVEX_DESC "Compressing EVEX instrs when possible"
#define COMP_EVEX_NAME "x86-compress-evex"

#define DEBUG_TYPE COMP_EVEX_NAME

namespace {
// Including the generated EVEX compression tables.
#define GET_X86_COMPRESS_EVEX_TABLE
#include "X86GenInstrMapping.inc"

class CompressEVEXPass : public MachineFunctionPass {
public:
  static char ID;
  CompressEVEXPass() : MachineFunctionPass(ID) {}
  StringRef getPassName() const override { return COMP_EVEX_DESC; }

  bool runOnMachineFunction(MachineFunction &MF) override;

  // This pass runs after regalloc and doesn't support VReg operands.
  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }
};

} // end anonymous namespace

char CompressEVEXPass::ID = 0;

static bool usesExtendedRegister(const MachineInstr &MI) {
  auto isHiRegIdx = [](unsigned Reg) {
    // Check for XMM register with indexes between 16 - 31.
    if (Reg >= X86::XMM16 && Reg <= X86::XMM31)
      return true;
    // Check for YMM register with indexes between 16 - 31.
    if (Reg >= X86::YMM16 && Reg <= X86::YMM31)
      return true;
    // Check for GPR with indexes between 16 - 31.
    if (X86II::isApxExtendedReg(Reg))
      return true;
    return false;
  };

  // Check that operands are not ZMM regs or
  // XMM/YMM regs with hi indexes between 16 - 31.
  for (const MachineOperand &MO : MI.explicit_operands()) {
    if (!MO.isReg())
      continue;

    Register Reg = MO.getReg();
    assert(!X86II::isZMMReg(Reg) &&
           "ZMM instructions should not be in the EVEX->VEX tables");
    if (isHiRegIdx(Reg))
      return true;
  }

  return false;
}

// Do any custom cleanup needed to finalize the conversion.
static bool performCustomAdjustments(MachineInstr &MI, unsigned NewOpc) {
  (void)NewOpc;
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
  case X86::VALIGNDZ128rri:
  case X86::VALIGNDZ128rmi:
  case X86::VALIGNQZ128rri:
  case X86::VALIGNQZ128rmi: {
    assert((NewOpc == X86::VPALIGNRrri || NewOpc == X86::VPALIGNRrmi) &&
           "Unexpected new opcode!");
    unsigned Scale =
        (Opc == X86::VALIGNQZ128rri || Opc == X86::VALIGNQZ128rmi) ? 8 : 4;
    MachineOperand &Imm = MI.getOperand(MI.getNumExplicitOperands() - 1);
    Imm.setImm(Imm.getImm() * Scale);
    break;
  }
  case X86::VSHUFF32X4Z256rmi:
  case X86::VSHUFF32X4Z256rri:
  case X86::VSHUFF64X2Z256rmi:
  case X86::VSHUFF64X2Z256rri:
  case X86::VSHUFI32X4Z256rmi:
  case X86::VSHUFI32X4Z256rri:
  case X86::VSHUFI64X2Z256rmi:
  case X86::VSHUFI64X2Z256rri: {
    assert((NewOpc == X86::VPERM2F128rr || NewOpc == X86::VPERM2I128rr ||
            NewOpc == X86::VPERM2F128rm || NewOpc == X86::VPERM2I128rm) &&
           "Unexpected new opcode!");
    MachineOperand &Imm = MI.getOperand(MI.getNumExplicitOperands() - 1);
    int64_t ImmVal = Imm.getImm();
    // Set bit 5, move bit 1 to bit 4, copy bit 0.
    Imm.setImm(0x20 | ((ImmVal & 2) << 3) | (ImmVal & 1));
    break;
  }
  case X86::VRNDSCALEPDZ128rri:
  case X86::VRNDSCALEPDZ128rmi:
  case X86::VRNDSCALEPSZ128rri:
  case X86::VRNDSCALEPSZ128rmi:
  case X86::VRNDSCALEPDZ256rri:
  case X86::VRNDSCALEPDZ256rmi:
  case X86::VRNDSCALEPSZ256rri:
  case X86::VRNDSCALEPSZ256rmi:
  case X86::VRNDSCALESDZr:
  case X86::VRNDSCALESDZm:
  case X86::VRNDSCALESSZr:
  case X86::VRNDSCALESSZm:
  case X86::VRNDSCALESDZr_Int:
  case X86::VRNDSCALESDZm_Int:
  case X86::VRNDSCALESSZr_Int:
  case X86::VRNDSCALESSZm_Int:
    const MachineOperand &Imm = MI.getOperand(MI.getNumExplicitOperands() - 1);
    int64_t ImmVal = Imm.getImm();
    // Ensure that only bits 3:0 of the immediate are used.
    if ((ImmVal & 0xf) != ImmVal)
      return false;
    break;
  }

  return true;
}

static bool CompressEVEXImpl(MachineInstr &MI, const X86Subtarget &ST) {
  uint64_t TSFlags = MI.getDesc().TSFlags;

  // Check for EVEX instructions only.
  if ((TSFlags & X86II::EncodingMask) != X86II::EVEX)
    return false;

  // Instructions with mask or 512-bit vector can't be converted to VEX.
  if (TSFlags & (X86II::EVEX_K | X86II::EVEX_L2))
    return false;

  auto IsRedundantNewDataDest = [&](unsigned &Opc) {
    // $rbx = ADD64rr_ND $rbx, $rax / $rbx = ADD64rr_ND $rax, $rbx
    //   ->
    // $rbx = ADD64rr $rbx, $rax
    const MCInstrDesc &Desc = MI.getDesc();
    Register Reg0 = MI.getOperand(0).getReg();
    const MachineOperand &Op1 = MI.getOperand(1);
    if (!Op1.isReg() || X86::getFirstAddrOperandIdx(MI) == 1 ||
        X86::isCFCMOVCC(MI.getOpcode()))
      return false;
    Register Reg1 = Op1.getReg();
    if (Reg1 == Reg0)
      return true;

    // Op1 and Op2 may be commutable for ND instructions.
    if (!Desc.isCommutable() || Desc.getNumOperands() < 3 ||
        !MI.getOperand(2).isReg() || MI.getOperand(2).getReg() != Reg0)
      return false;
    // Opcode may change after commute, e.g. SHRD -> SHLD
    ST.getInstrInfo()->commuteInstruction(MI, false, 1, 2);
    Opc = MI.getOpcode();
    return true;
  };

  // EVEX_B has several meanings.
  // AVX512:
  //  register form: rounding control or SAE
  //  memory form: broadcast
  //
  // APX:
  //  MAP4: NDD
  //
  // For AVX512 cases, EVEX prefix is needed in order to carry this information
  // thus preventing the transformation to VEX encoding.
  bool IsND = X86II::hasNewDataDest(TSFlags);
  if (TSFlags & X86II::EVEX_B && !IsND)
    return false;
  unsigned Opc = MI.getOpcode();
  // MOVBE*rr is special because it has semantic of NDD but not set EVEX_B.
  bool IsNDLike = IsND || Opc == X86::MOVBE32rr || Opc == X86::MOVBE64rr;
  bool IsRedundantNDD = IsNDLike ? IsRedundantNewDataDest(Opc) : false;

  auto GetCompressedOpc = [&](unsigned Opc) -> unsigned {
    ArrayRef<X86TableEntry> Table = ArrayRef(X86CompressEVEXTable);
    const auto I = llvm::lower_bound(Table, Opc);
    if (I == Table.end() || I->OldOpc != Opc)
      return 0;

    if (usesExtendedRegister(MI) || !checkPredicate(I->NewOpc, &ST) ||
        !performCustomAdjustments(MI, I->NewOpc))
      return 0;
    return I->NewOpc;
  };
  // NonNF -> NF only if it's not a compressible NDD instruction and eflags is
  // dead.
  unsigned NewOpc = IsRedundantNDD
                        ? X86::getNonNDVariant(Opc)
                        : ((IsNDLike && ST.hasNF() &&
                            MI.registerDefIsDead(X86::EFLAGS, /*TRI=*/nullptr))
                               ? X86::getNFVariant(Opc)
                               : GetCompressedOpc(Opc));

  if (!NewOpc)
    return false;

  const MCInstrDesc &NewDesc = ST.getInstrInfo()->get(NewOpc);
  MI.setDesc(NewDesc);
  unsigned AsmComment;
  switch (NewDesc.TSFlags & X86II::EncodingMask) {
  case X86II::LEGACY:
    AsmComment = X86::AC_EVEX_2_LEGACY;
    break;
  case X86II::VEX:
    AsmComment = X86::AC_EVEX_2_VEX;
    break;
  case X86II::EVEX:
    AsmComment = X86::AC_EVEX_2_EVEX;
    assert(IsND && (NewDesc.TSFlags & X86II::EVEX_NF) &&
           "Unknown EVEX2EVEX compression");
    break;
  default:
    llvm_unreachable("Unknown EVEX compression");
  }
  MI.setAsmPrinterFlag(AsmComment);
  if (IsRedundantNDD)
    MI.tieOperands(0, 1);

  return true;
}

bool CompressEVEXPass::runOnMachineFunction(MachineFunction &MF) {
#ifndef NDEBUG
  // Make sure the tables are sorted.
  static std::atomic<bool> TableChecked(false);
  if (!TableChecked.load(std::memory_order_relaxed)) {
    assert(llvm::is_sorted(X86CompressEVEXTable) &&
           "X86CompressEVEXTable is not sorted!");
    TableChecked.store(true, std::memory_order_relaxed);
  }
#endif
  const X86Subtarget &ST = MF.getSubtarget<X86Subtarget>();
  if (!ST.hasAVX512() && !ST.hasEGPR() && !ST.hasNDD())
    return false;

  bool Changed = false;

  for (MachineBasicBlock &MBB : MF) {
    // Traverse the basic block.
    for (MachineInstr &MI : MBB)
      Changed |= CompressEVEXImpl(MI, ST);
  }

  return Changed;
}

INITIALIZE_PASS(CompressEVEXPass, COMP_EVEX_NAME, COMP_EVEX_DESC, false, false)

FunctionPass *llvm::createX86CompressEVEXPass() {
  return new CompressEVEXPass();
}
