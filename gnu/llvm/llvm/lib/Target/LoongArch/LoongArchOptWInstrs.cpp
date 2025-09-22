//===- LoongArchOptWInstrs.cpp - MI W instruction optimizations ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This pass does some optimizations for *W instructions at the MI level.
//
// First it removes unneeded sext(addi.w rd, rs, 0) instructions. Either
// because the sign extended bits aren't consumed or because the input was
// already sign extended by an earlier instruction.
//
// Then:
// 1. Unless explicit disabled or the target prefers instructions with W suffix,
//    it removes the -w suffix from opw instructions whenever all users are
//    dependent only on the lower word of the result of the instruction.
//    The cases handled are:
//    * addi.w because it helps reduce test differences between LA32 and LA64
//      w/o being a pessimization.
//
// 2. Or if explicit enabled or the target prefers instructions with W suffix,
//    it adds the W suffix to the instruction whenever all users are dependent
//    only on the lower word of the result of the instruction.
//    The cases handled are:
//    * add.d/addi.d/sub.d/mul.d.
//    * slli.d with imm < 32.
//    * ld.d/ld.wu.
//===---------------------------------------------------------------------===//

#include "LoongArch.h"
#include "LoongArchMachineFunctionInfo.h"
#include "LoongArchSubtarget.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TargetInstrInfo.h"

using namespace llvm;

#define DEBUG_TYPE "loongarch-opt-w-instrs"
#define LOONGARCH_OPT_W_INSTRS_NAME "LoongArch Optimize W Instructions"

STATISTIC(NumRemovedSExtW, "Number of removed sign-extensions");
STATISTIC(NumTransformedToWInstrs,
          "Number of instructions transformed to W-ops");

static cl::opt<bool>
    DisableSExtWRemoval("loongarch-disable-sextw-removal",
                        cl::desc("Disable removal of sign-extend insn"),
                        cl::init(false), cl::Hidden);
static cl::opt<bool>
    DisableCvtToDSuffix("loongarch-disable-cvt-to-d-suffix",
                        cl::desc("Disable convert to D suffix"),
                        cl::init(false), cl::Hidden);

namespace {

class LoongArchOptWInstrs : public MachineFunctionPass {
public:
  static char ID;

  LoongArchOptWInstrs() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;
  bool removeSExtWInstrs(MachineFunction &MF, const LoongArchInstrInfo &TII,
                         const LoongArchSubtarget &ST,
                         MachineRegisterInfo &MRI);
  bool convertToDSuffixes(MachineFunction &MF, const LoongArchInstrInfo &TII,
                          const LoongArchSubtarget &ST,
                          MachineRegisterInfo &MRI);
  bool convertToWSuffixes(MachineFunction &MF, const LoongArchInstrInfo &TII,
                          const LoongArchSubtarget &ST,
                          MachineRegisterInfo &MRI);

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  StringRef getPassName() const override { return LOONGARCH_OPT_W_INSTRS_NAME; }
};

} // end anonymous namespace

char LoongArchOptWInstrs::ID = 0;
INITIALIZE_PASS(LoongArchOptWInstrs, DEBUG_TYPE, LOONGARCH_OPT_W_INSTRS_NAME,
                false, false)

FunctionPass *llvm::createLoongArchOptWInstrsPass() {
  return new LoongArchOptWInstrs();
}

// Checks if all users only demand the lower \p OrigBits of the original
// instruction's result.
// TODO: handle multiple interdependent transformations
static bool hasAllNBitUsers(const MachineInstr &OrigMI,
                            const LoongArchSubtarget &ST,
                            const MachineRegisterInfo &MRI, unsigned OrigBits) {

  SmallSet<std::pair<const MachineInstr *, unsigned>, 4> Visited;
  SmallVector<std::pair<const MachineInstr *, unsigned>, 4> Worklist;

  Worklist.push_back(std::make_pair(&OrigMI, OrigBits));

  while (!Worklist.empty()) {
    auto P = Worklist.pop_back_val();
    const MachineInstr *MI = P.first;
    unsigned Bits = P.second;

    if (!Visited.insert(P).second)
      continue;

    // Only handle instructions with one def.
    if (MI->getNumExplicitDefs() != 1)
      return false;

    Register DestReg = MI->getOperand(0).getReg();
    if (!DestReg.isVirtual())
      return false;

    for (auto &UserOp : MRI.use_nodbg_operands(DestReg)) {
      const MachineInstr *UserMI = UserOp.getParent();
      unsigned OpIdx = UserOp.getOperandNo();

      switch (UserMI->getOpcode()) {
      default:
        // TODO: Add vector
        return false;

      case LoongArch::ADD_W:
      case LoongArch::ADDI_W:
      case LoongArch::SUB_W:
      case LoongArch::ALSL_W:
      case LoongArch::ALSL_WU:
      case LoongArch::MUL_W:
      case LoongArch::MULH_W:
      case LoongArch::MULH_WU:
      case LoongArch::MULW_D_W:
      case LoongArch::MULW_D_WU:
      // TODO: {DIV,MOD}.{W,WU} consumes the upper 32 bits before LA664+.
      // case LoongArch::DIV_W:
      // case LoongArch::DIV_WU:
      // case LoongArch::MOD_W:
      // case LoongArch::MOD_WU:
      case LoongArch::SLL_W:
      case LoongArch::SLLI_W:
      case LoongArch::SRL_W:
      case LoongArch::SRLI_W:
      case LoongArch::SRA_W:
      case LoongArch::SRAI_W:
      case LoongArch::ROTR_W:
      case LoongArch::ROTRI_W:
      case LoongArch::CLO_W:
      case LoongArch::CLZ_W:
      case LoongArch::CTO_W:
      case LoongArch::CTZ_W:
      case LoongArch::BYTEPICK_W:
      case LoongArch::REVB_2H:
      case LoongArch::BITREV_4B:
      case LoongArch::BITREV_W:
      case LoongArch::BSTRINS_W:
      case LoongArch::BSTRPICK_W:
      case LoongArch::CRC_W_W_W:
      case LoongArch::CRCC_W_W_W:
      case LoongArch::MOVGR2FCSR:
      case LoongArch::MOVGR2FRH_W:
      case LoongArch::MOVGR2FR_W_64:
        if (Bits >= 32)
          break;
        return false;
      case LoongArch::MOVGR2CF:
        if (Bits >= 1)
          break;
        return false;
      case LoongArch::EXT_W_B:
        if (Bits >= 8)
          break;
        return false;
      case LoongArch::EXT_W_H:
        if (Bits >= 16)
          break;
        return false;

      case LoongArch::SRLI_D: {
        // If we are shifting right by less than Bits, and users don't demand
        // any bits that were shifted into [Bits-1:0], then we can consider this
        // as an N-Bit user.
        unsigned ShAmt = UserMI->getOperand(2).getImm();
        if (Bits > ShAmt) {
          Worklist.push_back(std::make_pair(UserMI, Bits - ShAmt));
          break;
        }
        return false;
      }

      // these overwrite higher input bits, otherwise the lower word of output
      // depends only on the lower word of input. So check their uses read W.
      case LoongArch::SLLI_D:
        if (Bits >= (ST.getGRLen() - UserMI->getOperand(2).getImm()))
          break;
        Worklist.push_back(std::make_pair(UserMI, Bits));
        break;
      case LoongArch::ANDI: {
        uint64_t Imm = UserMI->getOperand(2).getImm();
        if (Bits >= (unsigned)llvm::bit_width(Imm))
          break;
        Worklist.push_back(std::make_pair(UserMI, Bits));
        break;
      }
      case LoongArch::ORI: {
        uint64_t Imm = UserMI->getOperand(2).getImm();
        if (Bits >= (unsigned)llvm::bit_width<uint64_t>(~Imm))
          break;
        Worklist.push_back(std::make_pair(UserMI, Bits));
        break;
      }

      case LoongArch::SLL_D:
        // Operand 2 is the shift amount which uses log2(grlen) bits.
        if (OpIdx == 2) {
          if (Bits >= Log2_32(ST.getGRLen()))
            break;
          return false;
        }
        Worklist.push_back(std::make_pair(UserMI, Bits));
        break;

      case LoongArch::SRA_D:
      case LoongArch::SRL_D:
      case LoongArch::ROTR_D:
        // Operand 2 is the shift amount which uses 6 bits.
        if (OpIdx == 2 && Bits >= Log2_32(ST.getGRLen()))
          break;
        return false;

      case LoongArch::ST_B:
      case LoongArch::STX_B:
      case LoongArch::STGT_B:
      case LoongArch::STLE_B:
      case LoongArch::IOCSRWR_B:
        // The first argument is the value to store.
        if (OpIdx == 0 && Bits >= 8)
          break;
        return false;
      case LoongArch::ST_H:
      case LoongArch::STX_H:
      case LoongArch::STGT_H:
      case LoongArch::STLE_H:
      case LoongArch::IOCSRWR_H:
        // The first argument is the value to store.
        if (OpIdx == 0 && Bits >= 16)
          break;
        return false;
      case LoongArch::ST_W:
      case LoongArch::STX_W:
      case LoongArch::SCREL_W:
      case LoongArch::STPTR_W:
      case LoongArch::STGT_W:
      case LoongArch::STLE_W:
      case LoongArch::IOCSRWR_W:
        // The first argument is the value to store.
        if (OpIdx == 0 && Bits >= 32)
          break;
        return false;

      case LoongArch::CRC_W_B_W:
      case LoongArch::CRCC_W_B_W:
        if ((OpIdx == 1 && Bits >= 8) || (OpIdx == 2 && Bits >= 32))
          break;
        return false;
      case LoongArch::CRC_W_H_W:
      case LoongArch::CRCC_W_H_W:
        if ((OpIdx == 1 && Bits >= 16) || (OpIdx == 2 && Bits >= 32))
          break;
        return false;
      case LoongArch::CRC_W_D_W:
      case LoongArch::CRCC_W_D_W:
        if (OpIdx == 2 && Bits >= 32)
          break;
        return false;

      // For these, lower word of output in these operations, depends only on
      // the lower word of input. So, we check all uses only read lower word.
      case LoongArch::COPY:
      case LoongArch::PHI:
      case LoongArch::ADD_D:
      case LoongArch::ADDI_D:
      case LoongArch::SUB_D:
      case LoongArch::MUL_D:
      case LoongArch::AND:
      case LoongArch::OR:
      case LoongArch::NOR:
      case LoongArch::XOR:
      case LoongArch::XORI:
      case LoongArch::ANDN:
      case LoongArch::ORN:
        Worklist.push_back(std::make_pair(UserMI, Bits));
        break;

      case LoongArch::MASKNEZ:
      case LoongArch::MASKEQZ:
        if (OpIdx != 1)
          return false;
        Worklist.push_back(std::make_pair(UserMI, Bits));
        break;
      }
    }
  }

  return true;
}

static bool hasAllWUsers(const MachineInstr &OrigMI,
                         const LoongArchSubtarget &ST,
                         const MachineRegisterInfo &MRI) {
  return hasAllNBitUsers(OrigMI, ST, MRI, 32);
}

// This function returns true if the machine instruction always outputs a value
// where bits 63:32 match bit 31.
static bool isSignExtendingOpW(const MachineInstr &MI,
                               const MachineRegisterInfo &MRI, unsigned OpNo) {
  switch (MI.getOpcode()) {
  // Normal cases
  case LoongArch::ADD_W:
  case LoongArch::SUB_W:
  case LoongArch::ADDI_W:
  case LoongArch::ALSL_W:
  case LoongArch::LU12I_W:
  case LoongArch::SLT:
  case LoongArch::SLTU:
  case LoongArch::SLTI:
  case LoongArch::SLTUI:
  case LoongArch::ANDI:
  case LoongArch::MUL_W:
  case LoongArch::MULH_W:
  case LoongArch::MULH_WU:
  case LoongArch::DIV_W:
  case LoongArch::MOD_W:
  case LoongArch::DIV_WU:
  case LoongArch::MOD_WU:
  case LoongArch::SLL_W:
  case LoongArch::SRL_W:
  case LoongArch::SRA_W:
  case LoongArch::ROTR_W:
  case LoongArch::SLLI_W:
  case LoongArch::SRLI_W:
  case LoongArch::SRAI_W:
  case LoongArch::ROTRI_W:
  case LoongArch::EXT_W_B:
  case LoongArch::EXT_W_H:
  case LoongArch::CLO_W:
  case LoongArch::CLZ_W:
  case LoongArch::CTO_W:
  case LoongArch::CTZ_W:
  case LoongArch::BYTEPICK_W:
  case LoongArch::REVB_2H:
  case LoongArch::BITREV_4B:
  case LoongArch::BITREV_W:
  case LoongArch::BSTRINS_W:
  case LoongArch::BSTRPICK_W:
  case LoongArch::LD_B:
  case LoongArch::LD_H:
  case LoongArch::LD_W:
  case LoongArch::LD_BU:
  case LoongArch::LD_HU:
  case LoongArch::LL_W:
  case LoongArch::LLACQ_W:
  case LoongArch::RDTIMEL_W:
  case LoongArch::RDTIMEH_W:
  case LoongArch::CPUCFG:
  case LoongArch::LDX_B:
  case LoongArch::LDX_H:
  case LoongArch::LDX_W:
  case LoongArch::LDX_BU:
  case LoongArch::LDX_HU:
  case LoongArch::LDPTR_W:
  case LoongArch::LDGT_B:
  case LoongArch::LDGT_H:
  case LoongArch::LDGT_W:
  case LoongArch::LDLE_B:
  case LoongArch::LDLE_H:
  case LoongArch::LDLE_W:
  case LoongArch::AMSWAP_B:
  case LoongArch::AMSWAP_H:
  case LoongArch::AMSWAP_W:
  case LoongArch::AMADD_B:
  case LoongArch::AMADD_H:
  case LoongArch::AMADD_W:
  case LoongArch::AMAND_W:
  case LoongArch::AMOR_W:
  case LoongArch::AMXOR_W:
  case LoongArch::AMMAX_W:
  case LoongArch::AMMIN_W:
  case LoongArch::AMMAX_WU:
  case LoongArch::AMMIN_WU:
  case LoongArch::AMSWAP__DB_B:
  case LoongArch::AMSWAP__DB_H:
  case LoongArch::AMSWAP__DB_W:
  case LoongArch::AMADD__DB_B:
  case LoongArch::AMADD__DB_H:
  case LoongArch::AMADD__DB_W:
  case LoongArch::AMAND__DB_W:
  case LoongArch::AMOR__DB_W:
  case LoongArch::AMXOR__DB_W:
  case LoongArch::AMMAX__DB_W:
  case LoongArch::AMMIN__DB_W:
  case LoongArch::AMMAX__DB_WU:
  case LoongArch::AMMIN__DB_WU:
  case LoongArch::AMCAS_B:
  case LoongArch::AMCAS_H:
  case LoongArch::AMCAS_W:
  case LoongArch::AMCAS__DB_B:
  case LoongArch::AMCAS__DB_H:
  case LoongArch::AMCAS__DB_W:
  case LoongArch::CRC_W_B_W:
  case LoongArch::CRC_W_H_W:
  case LoongArch::CRC_W_W_W:
  case LoongArch::CRC_W_D_W:
  case LoongArch::CRCC_W_B_W:
  case LoongArch::CRCC_W_H_W:
  case LoongArch::CRCC_W_W_W:
  case LoongArch::CRCC_W_D_W:
  case LoongArch::IOCSRRD_B:
  case LoongArch::IOCSRRD_H:
  case LoongArch::IOCSRRD_W:
  case LoongArch::MOVFR2GR_S:
  case LoongArch::MOVFCSR2GR:
  case LoongArch::MOVCF2GR:
  case LoongArch::MOVFRH2GR_S:
  case LoongArch::MOVFR2GR_S_64:
    // TODO: Add vector
    return true;
  // Special cases that require checking operands.
  // shifting right sufficiently makes the value 32-bit sign-extended
  case LoongArch::SRAI_D:
    return MI.getOperand(2).getImm() >= 32;
  case LoongArch::SRLI_D:
    return MI.getOperand(2).getImm() > 32;
  // The LI pattern ADDI rd, R0, imm and ORI rd, R0, imm are sign extended.
  case LoongArch::ADDI_D:
  case LoongArch::ORI:
    return MI.getOperand(1).isReg() &&
           MI.getOperand(1).getReg() == LoongArch::R0;
  // A bits extract is sign extended if the msb is less than 31.
  case LoongArch::BSTRPICK_D:
    return MI.getOperand(2).getImm() < 31;
  // Copying from R0 produces zero.
  case LoongArch::COPY:
    return MI.getOperand(1).getReg() == LoongArch::R0;
  // Ignore the scratch register destination.
  case LoongArch::PseudoMaskedAtomicSwap32:
  case LoongArch::PseudoAtomicSwap32:
  case LoongArch::PseudoMaskedAtomicLoadAdd32:
  case LoongArch::PseudoMaskedAtomicLoadSub32:
  case LoongArch::PseudoAtomicLoadNand32:
  case LoongArch::PseudoMaskedAtomicLoadNand32:
  case LoongArch::PseudoAtomicLoadAdd32:
  case LoongArch::PseudoAtomicLoadSub32:
  case LoongArch::PseudoAtomicLoadAnd32:
  case LoongArch::PseudoAtomicLoadOr32:
  case LoongArch::PseudoAtomicLoadXor32:
  case LoongArch::PseudoMaskedAtomicLoadUMax32:
  case LoongArch::PseudoMaskedAtomicLoadUMin32:
  case LoongArch::PseudoCmpXchg32:
  case LoongArch::PseudoMaskedCmpXchg32:
  case LoongArch::PseudoMaskedAtomicLoadMax32:
  case LoongArch::PseudoMaskedAtomicLoadMin32:
    return OpNo == 0;
  }

  return false;
}

static bool isSignExtendedW(Register SrcReg, const LoongArchSubtarget &ST,
                            const MachineRegisterInfo &MRI,
                            SmallPtrSetImpl<MachineInstr *> &FixableDef) {
  SmallSet<Register, 4> Visited;
  SmallVector<Register, 4> Worklist;

  auto AddRegToWorkList = [&](Register SrcReg) {
    if (!SrcReg.isVirtual())
      return false;
    Worklist.push_back(SrcReg);
    return true;
  };

  if (!AddRegToWorkList(SrcReg))
    return false;

  while (!Worklist.empty()) {
    Register Reg = Worklist.pop_back_val();

    // If we already visited this register, we don't need to check it again.
    if (!Visited.insert(Reg).second)
      continue;

    MachineInstr *MI = MRI.getVRegDef(Reg);
    if (!MI)
      continue;

    int OpNo = MI->findRegisterDefOperandIdx(Reg, /*TRI=*/nullptr);
    assert(OpNo != -1 && "Couldn't find register");

    // If this is a sign extending operation we don't need to look any further.
    if (isSignExtendingOpW(*MI, MRI, OpNo))
      continue;

    // Is this an instruction that propagates sign extend?
    switch (MI->getOpcode()) {
    default:
      // Unknown opcode, give up.
      return false;
    case LoongArch::COPY: {
      const MachineFunction *MF = MI->getMF();
      const LoongArchMachineFunctionInfo *LAFI =
          MF->getInfo<LoongArchMachineFunctionInfo>();

      // If this is the entry block and the register is livein, see if we know
      // it is sign extended.
      if (MI->getParent() == &MF->front()) {
        Register VReg = MI->getOperand(0).getReg();
        if (MF->getRegInfo().isLiveIn(VReg) && LAFI->isSExt32Register(VReg))
          continue;
      }

      Register CopySrcReg = MI->getOperand(1).getReg();
      if (CopySrcReg == LoongArch::R4) {
        // For a method return value, we check the ZExt/SExt flags in attribute.
        // We assume the following code sequence for method call.
        // PseudoCALL @bar, ...
        // ADJCALLSTACKUP 0, 0, implicit-def dead $r3, implicit $r3
        // %0:gpr = COPY $r4
        //
        // We use the PseudoCall to look up the IR function being called to find
        // its return attributes.
        const MachineBasicBlock *MBB = MI->getParent();
        auto II = MI->getIterator();
        if (II == MBB->instr_begin() ||
            (--II)->getOpcode() != LoongArch::ADJCALLSTACKUP)
          return false;

        const MachineInstr &CallMI = *(--II);
        if (!CallMI.isCall() || !CallMI.getOperand(0).isGlobal())
          return false;

        auto *CalleeFn =
            dyn_cast_if_present<Function>(CallMI.getOperand(0).getGlobal());
        if (!CalleeFn)
          return false;

        auto *IntTy = dyn_cast<IntegerType>(CalleeFn->getReturnType());
        if (!IntTy)
          return false;

        const AttributeSet &Attrs = CalleeFn->getAttributes().getRetAttrs();
        unsigned BitWidth = IntTy->getBitWidth();
        if ((BitWidth <= 32 && Attrs.hasAttribute(Attribute::SExt)) ||
            (BitWidth < 32 && Attrs.hasAttribute(Attribute::ZExt)))
          continue;
      }

      if (!AddRegToWorkList(CopySrcReg))
        return false;

      break;
    }

    // For these, we just need to check if the 1st operand is sign extended.
    case LoongArch::MOD_D:
    case LoongArch::ANDI:
    case LoongArch::ORI:
    case LoongArch::XORI:
      // |Remainder| is always <= |Dividend|. If D is 32-bit, then so is R.
      // DIV doesn't work because of the edge case 0xf..f 8000 0000 / (long)-1
      // Logical operations use a sign extended 12-bit immediate.
      if (!AddRegToWorkList(MI->getOperand(1).getReg()))
        return false;

      break;
    case LoongArch::MOD_DU:
    case LoongArch::AND:
    case LoongArch::OR:
    case LoongArch::XOR:
    case LoongArch::ANDN:
    case LoongArch::ORN:
    case LoongArch::PHI: {
      // If all incoming values are sign-extended, the output of AND, OR, XOR,
      // or PHI is also sign-extended.

      // The input registers for PHI are operand 1, 3, ...
      // The input registers for others are operand 1 and 2.
      unsigned B = 1, E = 3, D = 1;
      switch (MI->getOpcode()) {
      case LoongArch::PHI:
        E = MI->getNumOperands();
        D = 2;
        break;
      }

      for (unsigned I = B; I != E; I += D) {
        if (!MI->getOperand(I).isReg())
          return false;

        if (!AddRegToWorkList(MI->getOperand(I).getReg()))
          return false;
      }

      break;
    }

    case LoongArch::MASKEQZ:
    case LoongArch::MASKNEZ:
      // Instructions return zero or operand 1. Result is sign extended if
      // operand 1 is sign extended.
      if (!AddRegToWorkList(MI->getOperand(1).getReg()))
        return false;
      break;

    // With these opcode, we can "fix" them with the W-version
    // if we know all users of the result only rely on bits 31:0
    case LoongArch::SLLI_D:
      // SLLI_W reads the lowest 5 bits, while SLLI_D reads lowest 6 bits
      if (MI->getOperand(2).getImm() >= 32)
        return false;
      [[fallthrough]];
    case LoongArch::ADDI_D:
    case LoongArch::ADD_D:
    case LoongArch::LD_D:
    case LoongArch::LD_WU:
    case LoongArch::MUL_D:
    case LoongArch::SUB_D:
      if (hasAllWUsers(*MI, ST, MRI)) {
        FixableDef.insert(MI);
        break;
      }
      return false;
    // If all incoming values are sign-extended and all users only use
    // the lower 32 bits, then convert them to W versions.
    case LoongArch::DIV_D: {
      if (!AddRegToWorkList(MI->getOperand(1).getReg()))
        return false;
      if (!AddRegToWorkList(MI->getOperand(2).getReg()))
        return false;
      if (hasAllWUsers(*MI, ST, MRI)) {
        FixableDef.insert(MI);
        break;
      }
      return false;
    }
    }
  }

  // If we get here, then every node we visited produces a sign extended value
  // or propagated sign extended values. So the result must be sign extended.
  return true;
}

static unsigned getWOp(unsigned Opcode) {
  switch (Opcode) {
  case LoongArch::ADDI_D:
    return LoongArch::ADDI_W;
  case LoongArch::ADD_D:
    return LoongArch::ADD_W;
  case LoongArch::DIV_D:
    return LoongArch::DIV_W;
  case LoongArch::LD_D:
  case LoongArch::LD_WU:
    return LoongArch::LD_W;
  case LoongArch::MUL_D:
    return LoongArch::MUL_W;
  case LoongArch::SLLI_D:
    return LoongArch::SLLI_W;
  case LoongArch::SUB_D:
    return LoongArch::SUB_W;
  default:
    llvm_unreachable("Unexpected opcode for replacement with W variant");
  }
}

bool LoongArchOptWInstrs::removeSExtWInstrs(MachineFunction &MF,
                                            const LoongArchInstrInfo &TII,
                                            const LoongArchSubtarget &ST,
                                            MachineRegisterInfo &MRI) {
  if (DisableSExtWRemoval)
    return false;

  bool MadeChange = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : llvm::make_early_inc_range(MBB)) {
      // We're looking for the sext.w pattern ADDI.W rd, rs, 0.
      if (!LoongArch::isSEXT_W(MI))
        continue;

      Register SrcReg = MI.getOperand(1).getReg();

      SmallPtrSet<MachineInstr *, 4> FixableDefs;

      // If all users only use the lower bits, this sext.w is redundant.
      // Or if all definitions reaching MI sign-extend their output,
      // then sext.w is redundant.
      if (!hasAllWUsers(MI, ST, MRI) &&
          !isSignExtendedW(SrcReg, ST, MRI, FixableDefs))
        continue;

      Register DstReg = MI.getOperand(0).getReg();
      if (!MRI.constrainRegClass(SrcReg, MRI.getRegClass(DstReg)))
        continue;

      // Convert Fixable instructions to their W versions.
      for (MachineInstr *Fixable : FixableDefs) {
        LLVM_DEBUG(dbgs() << "Replacing " << *Fixable);
        Fixable->setDesc(TII.get(getWOp(Fixable->getOpcode())));
        Fixable->clearFlag(MachineInstr::MIFlag::NoSWrap);
        Fixable->clearFlag(MachineInstr::MIFlag::NoUWrap);
        Fixable->clearFlag(MachineInstr::MIFlag::IsExact);
        LLVM_DEBUG(dbgs() << "     with " << *Fixable);
        ++NumTransformedToWInstrs;
      }

      LLVM_DEBUG(dbgs() << "Removing redundant sign-extension\n");
      MRI.replaceRegWith(DstReg, SrcReg);
      MRI.clearKillFlags(SrcReg);
      MI.eraseFromParent();
      ++NumRemovedSExtW;
      MadeChange = true;
    }
  }

  return MadeChange;
}

bool LoongArchOptWInstrs::convertToDSuffixes(MachineFunction &MF,
                                             const LoongArchInstrInfo &TII,
                                             const LoongArchSubtarget &ST,
                                             MachineRegisterInfo &MRI) {
  bool MadeChange = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      unsigned Opc;
      switch (MI.getOpcode()) {
      default:
        continue;
      case LoongArch::ADDI_W:
        Opc = LoongArch::ADDI_D;
        break;
      }

      if (hasAllWUsers(MI, ST, MRI)) {
        MI.setDesc(TII.get(Opc));
        MadeChange = true;
      }
    }
  }

  return MadeChange;
}

bool LoongArchOptWInstrs::convertToWSuffixes(MachineFunction &MF,
                                             const LoongArchInstrInfo &TII,
                                             const LoongArchSubtarget &ST,
                                             MachineRegisterInfo &MRI) {
  bool MadeChange = false;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      unsigned WOpc;
      // TODO: Add more?
      switch (MI.getOpcode()) {
      default:
        continue;
      case LoongArch::ADD_D:
        WOpc = LoongArch::ADD_W;
        break;
      case LoongArch::ADDI_D:
        WOpc = LoongArch::ADDI_W;
        break;
      case LoongArch::SUB_D:
        WOpc = LoongArch::SUB_W;
        break;
      case LoongArch::MUL_D:
        WOpc = LoongArch::MUL_W;
        break;
      case LoongArch::SLLI_D:
        // SLLI.W reads the lowest 5 bits, while SLLI.D reads lowest 6 bits
        if (MI.getOperand(2).getImm() >= 32)
          continue;
        WOpc = LoongArch::SLLI_W;
        break;
      case LoongArch::LD_D:
      case LoongArch::LD_WU:
        WOpc = LoongArch::LD_W;
        break;
      }

      if (hasAllWUsers(MI, ST, MRI)) {
        LLVM_DEBUG(dbgs() << "Replacing " << MI);
        MI.setDesc(TII.get(WOpc));
        MI.clearFlag(MachineInstr::MIFlag::NoSWrap);
        MI.clearFlag(MachineInstr::MIFlag::NoUWrap);
        MI.clearFlag(MachineInstr::MIFlag::IsExact);
        LLVM_DEBUG(dbgs() << "     with " << MI);
        ++NumTransformedToWInstrs;
        MadeChange = true;
      }
    }
  }

  return MadeChange;
}

bool LoongArchOptWInstrs::runOnMachineFunction(MachineFunction &MF) {
  if (skipFunction(MF.getFunction()))
    return false;

  MachineRegisterInfo &MRI = MF.getRegInfo();
  const LoongArchSubtarget &ST = MF.getSubtarget<LoongArchSubtarget>();
  const LoongArchInstrInfo &TII = *ST.getInstrInfo();

  if (!ST.is64Bit())
    return false;

  bool MadeChange = false;
  MadeChange |= removeSExtWInstrs(MF, TII, ST, MRI);

  if (!(DisableCvtToDSuffix || ST.preferWInst()))
    MadeChange |= convertToDSuffixes(MF, TII, ST, MRI);

  if (ST.preferWInst())
    MadeChange |= convertToWSuffixes(MF, TII, ST, MRI);

  return MadeChange;
}
