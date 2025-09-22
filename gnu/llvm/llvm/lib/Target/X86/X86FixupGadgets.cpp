//===-- X86FixupGadgets.cpp - Fixup Instructions that make ROP Gadgets ----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This file defines a function pass that checks instructions for sequences
/// that will lower to a potentially useful ROP gadget, and attempts to
/// replace those sequences with alternatives that are not useful for ROP.
///
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrBuilder.h"
#include "X86InstrInfo.h"
#include "X86MachineFunctionInfo.h"
#include "X86Subtarget.h"
#include "X86TargetMachine.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define FIXUPGADGETS_DESC "X86 ROP Gadget Fixup"
#define FIXUPGADGETS_NAME "x86-fixup-gadgets"

#define DEBUG_TYPE FIXUPGADGETS_NAME

// Toggle with cc1 option: -mllvm -x86-fixup-gadgets=<true|false>
static cl::opt<bool> FixupGadgets(
    "x86-fixup-gadgets", cl::Hidden,
    cl::desc("Replace ROP friendly instructions with safe alternatives"),
    cl::init(true));

namespace {
class FixupGadgetsPass : public MachineFunctionPass {

public:
  static char ID;

  StringRef getPassName() const override { return FIXUPGADGETS_DESC; }

  FixupGadgetsPass()
      : MachineFunctionPass(ID), STI(nullptr), TII(nullptr), TRI(nullptr) {}

  /// Loop over all the instructions and replace ROP friendly
  /// seuqences with less ROP friendly alternatives
  bool runOnMachineFunction(MachineFunction &MF) override;

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

private:
  const X86Subtarget *STI;
  const X86InstrInfo *TII;
  const X86RegisterInfo *TRI;
  bool Is64Bit;

  struct FixupInfo {
    unsigned op1;
    unsigned op2;
    bool fixup;
    bool align;
  };

  uint8_t getRegNum(const MachineOperand &MO) const;
  uint8_t getRegNum(unsigned reg) const;
  struct FixupInfo isROPFriendly(MachineInstr &MI) const;
  bool isROPFriendlyImm(const MachineOperand &MO) const;
  bool isROPFriendlyRegPair(const MachineOperand &Dst,
                            const MachineOperand &Src) const;
  bool isROPFriendlyReg(const MachineOperand &Dst, uint8_t RegOpcode) const;
  bool badModRM(uint8_t Mod, uint8_t RegOpcode, uint8_t RM) const;
  void checkSIB(const MachineInstr &MI, unsigned CurOp,
                struct FixupInfo &info) const;
  bool needsFixup(struct FixupInfo &fi) const;
  bool needsAlign(struct FixupInfo &fi) const;
  unsigned getWidestRegForReg(unsigned reg) const;
  unsigned getEquivalentRegForReg(unsigned oreg, unsigned nreg) const;
  bool hasImplicitUseOrDef(const MachineInstr &MI, unsigned Reg1,
                           unsigned Reg2) const;
  bool fixupWithoutExchange(MachineInstr &MI);

  bool fixupInstruction(MachineFunction &MF, MachineBasicBlock &MBB,
                        MachineInstr &MI, struct FixupInfo Info);
};
char FixupGadgetsPass::ID = 0;
} // namespace

FunctionPass *llvm::createX86FixupGadgetsPass() {
  return new FixupGadgetsPass();
}

uint8_t FixupGadgetsPass::getRegNum(const MachineOperand &MO) const {
  return TRI->getEncodingValue(MO.getReg()) & 0x7;
}

uint8_t FixupGadgetsPass::getRegNum(unsigned reg) const {
  return TRI->getEncodingValue(reg) & 0x7;
}

bool FixupGadgetsPass::isROPFriendlyImm(const MachineOperand &MO) const {
  int64_t imm = MO.getImm();
  for (int i = 0; i < 8; ++i) {
    uint8_t byte = (imm & 0xff);
    if (byte == 0xc2 || byte == 0xc3 || byte == 0xca || byte == 0xcb) {
      return true;
    }
    imm = imm >> 8;
  }
  return false;
}

bool FixupGadgetsPass::isROPFriendlyRegPair(const MachineOperand &Dst,
                                            const MachineOperand &Src) const {

  if (!Dst.isReg() || !Src.isReg())
    llvm_unreachable("Testing non registers for bad reg pair!");

  uint8_t Mod = 3;
  uint8_t RegOpcode = getRegNum(Src);
  uint8_t RM = getRegNum(Dst);
  return badModRM(Mod, RegOpcode, RM);
}

bool FixupGadgetsPass::isROPFriendlyReg(const MachineOperand &Dst, uint8_t RegOpcode) const {

  if (!Dst.isReg())
    llvm_unreachable("Testing non register for bad reg!");

  uint8_t Mod = 3;
  uint8_t RM = getRegNum(Dst);
  return badModRM(Mod, RegOpcode, RM);
}

bool FixupGadgetsPass::badModRM(uint8_t Mod, uint8_t RegOpcode,
                                uint8_t RM) const {
  uint8_t ModRM = ((Mod << 6) | (RegOpcode << 3) | RM);
  if (ModRM == 0xc2 || ModRM == 0xc3 || ModRM == 0xca || ModRM == 0xcb)
    return true;
  return false;
}

void FixupGadgetsPass::checkSIB(const MachineInstr &MI, unsigned CurOp,
                                struct FixupInfo &info) const {

  const MachineOperand &Base = MI.getOperand(CurOp + X86::AddrBaseReg);
  const MachineOperand &Scale = MI.getOperand(CurOp + X86::AddrScaleAmt);
  const MachineOperand &Index = MI.getOperand(CurOp + X86::AddrIndexReg);

  if (!Scale.isImm() || !Base.isReg() || !Index.isReg())
    llvm_unreachable("Wrong type operands");

  if (Scale.getImm() != 8 || Base.getReg() == 0 || Index.getReg() == 0)
    return;

  if (badModRM(3, getRegNum(Index), getRegNum(Base))) {
    info.op1 = CurOp + X86::AddrBaseReg;
    info.op2 = CurOp + X86::AddrIndexReg;
    info.fixup = true;
  }
}

struct FixupGadgetsPass::FixupInfo
FixupGadgetsPass::isROPFriendly(MachineInstr &MI) const {

  const MCInstrDesc &Desc = MI.getDesc();
  unsigned CurOp = X86II::getOperandBias(Desc);
  uint64_t TSFlags = Desc.TSFlags;
  uint64_t Form = TSFlags & X86II::FormMask;
  bool HasVEX_4V = TSFlags & X86II::VEX_4V;
  bool HasEVEX_K = TSFlags & X86II::EVEX_K;

  struct FixupInfo info = {0, 0, false, false};

  // Look for constants with c3 in them
  for (const auto &MO : MI.operands()) {
    if (MO.isImm() && isROPFriendlyImm(MO)) {
      info.align = true;
      break;
    }
  }

  switch (Form) {
  case X86II::Pseudo: {
    // Pesudos that are replaced with real instructions later
    switch (MI.getOpcode()) {
    case X86::ADD64rr_DB:
    case X86::ADD32rr_DB:
    case X86::ADD16rr_DB:
      goto Handle_MRMDestReg;
    case X86::ADD16ri_DB:
    case X86::ADD32ri_DB:
    case X86::ADD64ri32_DB:
      goto Handle_MRMXr;
    default:
      break;
    }
    break;
  }
  case X86II::AddRegFrm: {
    uint8_t BaseOpcode = X86II::getBaseOpcodeFor(TSFlags);
    uint8_t Opcode = BaseOpcode + getRegNum(MI.getOperand(CurOp));
    if (Opcode == 0xc2 || Opcode == 0xc3 || Opcode == 0xca || Opcode == 0xcb) {
      info.op1 = CurOp;
      info.fixup = true;
    }
    break;
  }
  case X86II::MRMDestMem: {
    checkSIB(MI, CurOp, info);
    unsigned opcode = MI.getOpcode();
    if (opcode == X86::MOVNTImr || opcode == X86::MOVNTI_64mr)
      info.align = true;
    break;
  }
  case X86II::MRMSrcMem: {
    CurOp += 1;
    if (HasVEX_4V)
      CurOp += 1;
    if (HasEVEX_K)
      CurOp += 1;
    checkSIB(MI, CurOp, info);
    break;
  }
  case X86II::MRMSrcMem4VOp3: {
    CurOp += 1;
    checkSIB(MI, CurOp, info);
    break;
  }
  case X86II::MRMSrcMemOp4: {
    CurOp += 3;
    checkSIB(MI, CurOp, info);
    break;
  }
  case X86II::MRMXm:
  case X86II::MRM0m:
  case X86II::MRM1m:
  case X86II::MRM2m:
  case X86II::MRM3m:
  case X86II::MRM4m:
  case X86II::MRM5m:
  case X86II::MRM6m:
  case X86II::MRM7m: {
    if (HasVEX_4V)
      CurOp += 1;
    if (HasEVEX_K)
      CurOp += 1;
    checkSIB(MI, CurOp, info);
    break;
  }
  case X86II::MRMDestReg: {
  Handle_MRMDestReg:
    const MachineOperand &DstReg = MI.getOperand(CurOp);
    info.op1 = CurOp;
    CurOp += 1;
    if (HasVEX_4V)
      CurOp += 1;
    if (HasEVEX_K)
      CurOp += 1;
    const MachineOperand &SrcReg = MI.getOperand(CurOp);
    info.op2 = CurOp;
    if (isROPFriendlyRegPair(DstReg, SrcReg))
      info.fixup = true;
    break;
  }
  case X86II::MRMSrcReg: {
    const MachineOperand &DstReg = MI.getOperand(CurOp);
    info.op1 = CurOp;
    CurOp += 1;
    if (HasVEX_4V)
      CurOp += 1;
    if (HasEVEX_K)
      CurOp += 1;
    const MachineOperand &SrcReg = MI.getOperand(CurOp);
    info.op2 = CurOp;
    if (isROPFriendlyRegPair(SrcReg, DstReg))
      info.fixup = true;
    break;
  }
  case X86II::MRMSrcReg4VOp3: {
    const MachineOperand &DstReg = MI.getOperand(CurOp);
    info.op1 = CurOp;
    CurOp += 1;
    const MachineOperand &SrcReg = MI.getOperand(CurOp);
    info.op2 = CurOp;
    if (isROPFriendlyRegPair(SrcReg, DstReg))
      info.fixup = true;
    break;
  }
  case X86II::MRMSrcRegOp4: {
    const MachineOperand &DstReg = MI.getOperand(CurOp);
    info.op1 = CurOp;
    CurOp += 3;
    const MachineOperand &SrcReg = MI.getOperand(CurOp);
    info.op2 = CurOp;
    if (isROPFriendlyRegPair(SrcReg, DstReg))
      info.fixup = true;
    break;
  }
  case X86II::MRMXr:
  case X86II::MRM0r:
  case X86II::MRM1r: {
Handle_MRMXr:
    if (HasVEX_4V)
      CurOp += 1;
    if (HasEVEX_K)
      CurOp += 1;
    const MachineOperand &DstReg = MI.getOperand(CurOp);
    info.op1 = CurOp;
    if (isROPFriendlyReg(DstReg, Form == X86II::MRM1r ? 1 : 0))
      info.fixup = true;
    break;
  }
  case X86II::MRM_C2:
  case X86II::MRM_C3:
  case X86II::MRM_CA:
  case X86II::MRM_CB: {
    info.align = true;
    break;
  }
  default:
    break;
  }
  return info;
}

bool FixupGadgetsPass::needsFixup(struct FixupInfo &fi) const {
  return (fi.fixup == true);
}

bool FixupGadgetsPass::needsAlign(struct FixupInfo &fi) const {
  return (fi.align == true);
}

unsigned FixupGadgetsPass::getWidestRegForReg(unsigned reg) const {

  switch (reg) {
  case X86::AL:
  case X86::AH:
  case X86::AX:
  case X86::EAX:
  case X86::RAX:
    return Is64Bit ? X86::RAX : X86::EAX;
  case X86::BL:
  case X86::BH:
  case X86::BX:
  case X86::EBX:
  case X86::RBX:
    return Is64Bit ? X86::RBX : X86::EBX;
  case X86::CL:
  case X86::CH:
  case X86::CX:
  case X86::ECX:
  case X86::RCX:
    return Is64Bit ? X86::RCX : X86::ECX;
  case X86::DL:
  case X86::DH:
  case X86::DX:
  case X86::EDX:
  case X86::RDX:
    return Is64Bit ? X86::RDX : X86::EDX;
  case X86::R8B:
  case X86::R8W:
  case X86::R8D:
  case X86::R8:
    return X86::R8;
  case X86::R9B:
  case X86::R9W:
  case X86::R9D:
  case X86::R9:
    return X86::R9;
  case X86::R10B:
  case X86::R10W:
  case X86::R10D:
  case X86::R10:
    return X86::R10;
  case X86::R11B:
  case X86::R11W:
  case X86::R11D:
  case X86::R11:
    return X86::R11;
  default:
    return X86::NoRegister; // Non-GP Reg
  }
  return 0;
}

// For given register oreg return the equivalent size register
// from the nreg register set. Eg. For oreg ebx and nreg ax, return eax.
unsigned FixupGadgetsPass::getEquivalentRegForReg(unsigned oreg,
                                                  unsigned nreg) const {
  unsigned compreg = getWidestRegForReg(nreg);

  switch (oreg) {
  case X86::AL:
  case X86::BL:
  case X86::CL:
  case X86::DL:
  case X86::R8B:
  case X86::R9B:
  case X86::R10B:
  case X86::R11B:
    switch (compreg) {
    case X86::EAX:
    case X86::RAX:
      return X86::AL;
    case X86::EBX:
    case X86::RBX:
      return X86::BL;
    case X86::ECX:
    case X86::RCX:
      return X86::CL;
    case X86::EDX:
    case X86::RDX:
      return X86::DL;
    case X86::R8:
      return X86::R8B;
    case X86::R9:
      return X86::R9B;
    case X86::R10:
      return X86::R10B;
    case X86::R11:
      return X86::R11B;
    default:
      llvm_unreachable("Unknown 8 bit register");
    }
    break;
  case X86::AH:
  case X86::BH:
  case X86::CH:
  case X86::DH:
    switch (compreg) {
    case X86::EAX:
      return X86::AH;
    case X86::EBX:
      return X86::BH;
    case X86::ECX:
      return X86::CH;
    case X86::EDX:
      return X86::DH;
    default:
      llvm_unreachable("Using H registers in REX mode");
    }
    break;
  case X86::AX:
  case X86::BX:
  case X86::CX:
  case X86::DX:
  case X86::R8W:
  case X86::R9W:
  case X86::R10W:
  case X86::R11W:
    switch (compreg) {
    case X86::EAX:
    case X86::RAX:
      return X86::AX;
    case X86::EBX:
    case X86::RBX:
      return X86::BX;
    case X86::ECX:
    case X86::RCX:
      return X86::CX;
    case X86::EDX:
    case X86::RDX:
      return X86::DX;
    case X86::R8:
      return X86::R8W;
    case X86::R9:
      return X86::R9W;
    case X86::R10:
      return X86::R10W;
    case X86::R11:
      return X86::R11W;
    default:
      llvm_unreachable("Unknown 16 bit register");
    }
    break;
  case X86::EAX:
  case X86::EBX:
  case X86::ECX:
  case X86::EDX:
  case X86::R8D:
  case X86::R9D:
  case X86::R10D:
  case X86::R11D:
    switch (compreg) {
    case X86::EAX:
    case X86::RAX:
      return X86::EAX;
    case X86::EBX:
    case X86::RBX:
      return X86::EBX;
    case X86::ECX:
    case X86::RCX:
      return X86::ECX;
    case X86::EDX:
    case X86::RDX:
      return X86::EDX;
    case X86::R8:
      return X86::R8D;
    case X86::R9:
      return X86::R9D;
    case X86::R10:
      return X86::R10D;
    case X86::R11:
      return X86::R11D;
    default:
      llvm_unreachable("Unknown 32 bit register");
    }
    break;
  case X86::RAX:
  case X86::RBX:
  case X86::RCX:
  case X86::RDX:
  case X86::R8:
  case X86::R9:
  case X86::R10:
  case X86::R11:
    return compreg;
  default:
    llvm_unreachable("Unknown input register!");
  }
}

bool FixupGadgetsPass::hasImplicitUseOrDef(const MachineInstr &MI,
                                           unsigned Reg1, unsigned Reg2) const {

  const MCInstrDesc &Desc = MI.getDesc();

  const ArrayRef<MCPhysReg> ImpDefs = Desc.implicit_defs();
  for (MCPhysReg ImpDef : ImpDefs) {
    unsigned w = getWidestRegForReg(ImpDef);
    if (w == Reg1 || w == Reg2) {
      return true;
    }
  }

  const ArrayRef<MCPhysReg> ImpUses = Desc.implicit_uses();
  for (MCPhysReg ImpUse : ImpUses) {
    unsigned w = getWidestRegForReg(ImpUse);
    if (w == Reg1 || w == Reg2) {
      return true;
    }
  }
  return false;
}

bool FixupGadgetsPass::fixupWithoutExchange(MachineInstr &MI) {
  switch (MI.getOpcode()) {
    case X86::MOV8rr_REV:
      MI.setDesc(TII->get(X86::MOV8rr));
      break;
    case X86::MOV16rr_REV:
      MI.setDesc(TII->get(X86::MOV16rr));
      break;
    case X86::MOV32rr_REV:
      MI.setDesc(TII->get(X86::MOV32rr));
      break;
    case X86::MOV64rr_REV:
      MI.setDesc(TII->get(X86::MOV64rr));
      break;
    case X86::MOV8rr:
      MI.setDesc(TII->get(X86::MOV8rr_REV));
      break;
    case X86::MOV16rr:
      MI.setDesc(TII->get(X86::MOV16rr_REV));
      break;
    case X86::MOV32rr:
      MI.setDesc(TII->get(X86::MOV32rr_REV));
      break;
    case X86::MOV64rr:
      MI.setDesc(TII->get(X86::MOV64rr_REV));
      break;
    default:
      return false;
  }
  return true;
}

bool FixupGadgetsPass::fixupInstruction(MachineFunction &MF,
                                        MachineBasicBlock &MBB,
                                        MachineInstr &MI, FixupInfo Info) {

  if (!needsAlign(Info) && !needsFixup(Info))
    return false;

  DebugLoc DL = MI.getDebugLoc();

  // Check for only needs alignment
  if (needsAlign(Info) && !needsFixup(Info)) {
    BuildMI(MBB, MI, DL, TII->get(X86::JMP_TRAP));
    return true;
  }

  unsigned XCHG = Is64Bit ? X86::XCHG64rr : X86::XCHG32rr;

  unsigned OrigReg1 = MI.getOperand(Info.op1).getReg();
  // Swap with RAX/EAX unless we have a second register to swap with
  unsigned OrigReg2 = Is64Bit ? X86::RAX : X86::EAX;
  if (Info.op2)
    OrigReg2 = MI.getOperand(Info.op2).getReg();

  unsigned SwapReg1 = getWidestRegForReg(OrigReg1);
  unsigned SwapReg2 = getWidestRegForReg(OrigReg2);
  unsigned CompReg1 = SwapReg1;
  unsigned CompReg2 = SwapReg2;

  // Just align if:
  // - we have a non-GP reg to swap with
  // - the instruction implicitly uses one of the registers we are swapping
  // - if we are fixing an instruction that skips the xchg back
  if (SwapReg1 == X86::NoRegister || SwapReg2 == X86::NoRegister ||
      hasImplicitUseOrDef(MI, CompReg1, CompReg2) || MI.isCall() ||
      MI.isReturn() || MI.isBranch() || MI.isIndirectBranch() ||
      MI.isBarrier()) {
    BuildMI(MBB, MI, DL, TII->get(X86::JMP_TRAP));
    return true;
  }

  // Make sure our XCHG doesn't make a gadget
  if (badModRM(3, getRegNum(SwapReg1), getRegNum(SwapReg2))) {
    unsigned treg = SwapReg1;
    SwapReg1 = SwapReg2;
    SwapReg2 = treg;
  }

  // Check for specific instructions we can fix without the xchg dance
  if (fixupWithoutExchange(MI)) {
      return true;
  }

  // Swap the two registers to start
  BuildMI(MBB, MI, DL, TII->get(XCHG))
      .addReg(SwapReg1, RegState::Define)
      .addReg(SwapReg2, RegState::Define)
      .addReg(SwapReg1).addReg(SwapReg2);

  // Check for needs alignment
  if (needsAlign(Info))
    BuildMI(MBB, MI, DL, TII->get(X86::JMP_TRAP));

  // Swap the registers inside the instruction
  for (MachineOperand &MO : MI.operands()) {
    if (!MO.isReg())
      continue;

    unsigned reg = MO.getReg();
    unsigned match = getWidestRegForReg(reg);
    if (match == CompReg1)
      MO.setReg(getEquivalentRegForReg(reg, OrigReg2));
    else if (match == CompReg2)
      MO.setReg(getEquivalentRegForReg(reg, OrigReg1));
  }

  // And swap the two registers back
  BuildMI(MBB, ++MachineBasicBlock::instr_iterator(MI), DL, TII->get(XCHG))
      .addReg(SwapReg1, RegState::Define)
      .addReg(SwapReg2, RegState::Define)
      .addReg(SwapReg1).addReg(SwapReg2);

  return true;
}

bool FixupGadgetsPass::runOnMachineFunction(MachineFunction &MF) {
  if (!FixupGadgets)
    return false;

  STI = &MF.getSubtarget<X86Subtarget>();
  TII = STI->getInstrInfo();
  TRI = STI->getRegisterInfo();
  Is64Bit = STI->is64Bit();
  std::vector<std::pair<MachineInstr *, FixupInfo>> fixups;
  FixupInfo info;

  bool modified = false;

  for (auto &MBB : MF) {
    fixups.clear();
    for (auto &MI : MBB) {
      info = isROPFriendly(MI);
      if (needsAlign(info) || needsFixup(info))
        fixups.push_back(std::make_pair(&MI, info));
    }
    for (auto &fixup : fixups)
      modified |= fixupInstruction(MF, MBB, *fixup.first, fixup.second);
  }

  return modified;
}
