//===----- RISCVMergeBaseOffset.cpp - Optimise address calculations  ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Merge the offset of address calculation into the offset field
// of instructions in a global address lowering sequence.
//
//===----------------------------------------------------------------------===//

#include "RISCV.h"
#include "RISCVTargetMachine.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Debug.h"
#include "llvm/Target/TargetOptions.h"
#include <optional>
using namespace llvm;

#define DEBUG_TYPE "riscv-merge-base-offset"
#define RISCV_MERGE_BASE_OFFSET_NAME "RISC-V Merge Base Offset"
namespace {

class RISCVMergeBaseOffsetOpt : public MachineFunctionPass {
  const RISCVSubtarget *ST = nullptr;
  MachineRegisterInfo *MRI;

public:
  static char ID;
  bool runOnMachineFunction(MachineFunction &Fn) override;
  bool detectFoldable(MachineInstr &Hi, MachineInstr *&Lo);

  bool detectAndFoldOffset(MachineInstr &Hi, MachineInstr &Lo);
  void foldOffset(MachineInstr &Hi, MachineInstr &Lo, MachineInstr &Tail,
                  int64_t Offset);
  bool foldLargeOffset(MachineInstr &Hi, MachineInstr &Lo,
                       MachineInstr &TailAdd, Register GSReg);
  bool foldShiftedOffset(MachineInstr &Hi, MachineInstr &Lo,
                         MachineInstr &TailShXAdd, Register GSReg);

  bool foldIntoMemoryOps(MachineInstr &Hi, MachineInstr &Lo);

  RISCVMergeBaseOffsetOpt() : MachineFunctionPass(ID) {}

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::IsSSA);
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  StringRef getPassName() const override {
    return RISCV_MERGE_BASE_OFFSET_NAME;
  }
};
} // end anonymous namespace

char RISCVMergeBaseOffsetOpt::ID = 0;
INITIALIZE_PASS(RISCVMergeBaseOffsetOpt, DEBUG_TYPE,
                RISCV_MERGE_BASE_OFFSET_NAME, false, false)

// Detect either of the patterns:
//
// 1. (medlow pattern):
//   lui   vreg1, %hi(s)
//   addi  vreg2, vreg1, %lo(s)
//
// 2. (medany pattern):
// .Lpcrel_hi1:
//   auipc vreg1, %pcrel_hi(s)
//   addi  vreg2, vreg1, %pcrel_lo(.Lpcrel_hi1)
//
// The pattern is only accepted if:
//    1) The first instruction has only one use, which is the ADDI.
//    2) The address operands have the appropriate type, reflecting the
//       lowering of a global address or constant pool using medlow or medany.
//    3) The offset value in the Global Address or Constant Pool is 0.
bool RISCVMergeBaseOffsetOpt::detectFoldable(MachineInstr &Hi,
                                             MachineInstr *&Lo) {
  if (Hi.getOpcode() != RISCV::LUI && Hi.getOpcode() != RISCV::AUIPC &&
      Hi.getOpcode() != RISCV::PseudoMovAddr)
    return false;

  const MachineOperand &HiOp1 = Hi.getOperand(1);
  unsigned ExpectedFlags =
      Hi.getOpcode() == RISCV::AUIPC ? RISCVII::MO_PCREL_HI : RISCVII::MO_HI;
  if (HiOp1.getTargetFlags() != ExpectedFlags)
    return false;

  if (!(HiOp1.isGlobal() || HiOp1.isCPI() || HiOp1.isBlockAddress()) ||
      HiOp1.getOffset() != 0)
    return false;

  if (Hi.getOpcode() == RISCV::PseudoMovAddr) {
    // Most of the code should handle it correctly without modification by
    // setting Lo and Hi both point to PseudoMovAddr
    Lo = &Hi;
  } else {
    Register HiDestReg = Hi.getOperand(0).getReg();
    if (!MRI->hasOneUse(HiDestReg))
      return false;

    Lo = &*MRI->use_instr_begin(HiDestReg);
    if (Lo->getOpcode() != RISCV::ADDI)
      return false;
  }

  const MachineOperand &LoOp2 = Lo->getOperand(2);
  if (Hi.getOpcode() == RISCV::LUI || Hi.getOpcode() == RISCV::PseudoMovAddr) {
    if (LoOp2.getTargetFlags() != RISCVII::MO_LO ||
        !(LoOp2.isGlobal() || LoOp2.isCPI() || LoOp2.isBlockAddress()) ||
        LoOp2.getOffset() != 0)
      return false;
  } else {
    assert(Hi.getOpcode() == RISCV::AUIPC);
    if (LoOp2.getTargetFlags() != RISCVII::MO_PCREL_LO ||
        LoOp2.getType() != MachineOperand::MO_MCSymbol)
      return false;
  }

  if (HiOp1.isGlobal()) {
    LLVM_DEBUG(dbgs() << "  Found lowered global address: "
                      << *HiOp1.getGlobal() << "\n");
  } else if (HiOp1.isBlockAddress()) {
    LLVM_DEBUG(dbgs() << "  Found lowered basic address: "
                      << *HiOp1.getBlockAddress() << "\n");
  } else if (HiOp1.isCPI()) {
    LLVM_DEBUG(dbgs() << "  Found lowered constant pool: " << HiOp1.getIndex()
                      << "\n");
  }

  return true;
}

// Update the offset in Hi and Lo instructions.
// Delete the tail instruction and update all the uses to use the
// output from Lo.
void RISCVMergeBaseOffsetOpt::foldOffset(MachineInstr &Hi, MachineInstr &Lo,
                                         MachineInstr &Tail, int64_t Offset) {
  assert(isInt<32>(Offset) && "Unexpected offset");
  // Put the offset back in Hi and the Lo
  Hi.getOperand(1).setOffset(Offset);
  if (Hi.getOpcode() != RISCV::AUIPC)
    Lo.getOperand(2).setOffset(Offset);
  // Delete the tail instruction.
  MRI->constrainRegClass(Lo.getOperand(0).getReg(),
                         MRI->getRegClass(Tail.getOperand(0).getReg()));
  MRI->replaceRegWith(Tail.getOperand(0).getReg(), Lo.getOperand(0).getReg());
  Tail.eraseFromParent();
  LLVM_DEBUG(dbgs() << "  Merged offset " << Offset << " into base.\n"
                    << "     " << Hi << "     " << Lo;);
}

// Detect patterns for large offsets that are passed into an ADD instruction.
// If the pattern is found, updates the offset in Hi and Lo instructions
// and deletes TailAdd and the instructions that produced the offset.
//
//                     Base address lowering is of the form:
//                       Hi:  lui   vreg1, %hi(s)
//                       Lo:  addi  vreg2, vreg1, %lo(s)
//                       /                                  \
//                      /                                    \
//                     /                                      \
//                    /  The large offset can be of two forms: \
//  1) Offset that has non zero bits in lower      2) Offset that has non zero
//     12 bits and upper 20 bits                      bits in upper 20 bits only
//   OffseLUI: lui   vreg3, 4
// OffsetTail: addi  voff, vreg3, 188                OffsetTail: lui  voff, 128
//                    \                                        /
//                     \                                      /
//                      \                                    /
//                       \                                  /
//                         TailAdd: add  vreg4, vreg2, voff
bool RISCVMergeBaseOffsetOpt::foldLargeOffset(MachineInstr &Hi,
                                              MachineInstr &Lo,
                                              MachineInstr &TailAdd,
                                              Register GAReg) {
  assert((TailAdd.getOpcode() == RISCV::ADD) && "Expected ADD instruction!");
  Register Rs = TailAdd.getOperand(1).getReg();
  Register Rt = TailAdd.getOperand(2).getReg();
  Register Reg = Rs == GAReg ? Rt : Rs;

  // Can't fold if the register has more than one use.
  if (!Reg.isVirtual() || !MRI->hasOneUse(Reg))
    return false;
  // This can point to an ADDI(W) or a LUI:
  MachineInstr &OffsetTail = *MRI->getVRegDef(Reg);
  if (OffsetTail.getOpcode() == RISCV::ADDI ||
      OffsetTail.getOpcode() == RISCV::ADDIW) {
    // The offset value has non zero bits in both %hi and %lo parts.
    // Detect an ADDI that feeds from a LUI instruction.
    MachineOperand &AddiImmOp = OffsetTail.getOperand(2);
    if (AddiImmOp.getTargetFlags() != RISCVII::MO_None)
      return false;
    Register AddiReg = OffsetTail.getOperand(1).getReg();
    int64_t OffLo = AddiImmOp.getImm();

    // Handle rs1 of ADDI is X0.
    if (AddiReg == RISCV::X0) {
      LLVM_DEBUG(dbgs() << "  Offset Instrs: " << OffsetTail);
      foldOffset(Hi, Lo, TailAdd, OffLo);
      OffsetTail.eraseFromParent();
      return true;
    }

    MachineInstr &OffsetLui = *MRI->getVRegDef(AddiReg);
    MachineOperand &LuiImmOp = OffsetLui.getOperand(1);
    if (OffsetLui.getOpcode() != RISCV::LUI ||
        LuiImmOp.getTargetFlags() != RISCVII::MO_None ||
        !MRI->hasOneUse(OffsetLui.getOperand(0).getReg()))
      return false;
    int64_t Offset = SignExtend64<32>(LuiImmOp.getImm() << 12);
    Offset += OffLo;
    // RV32 ignores the upper 32 bits. ADDIW sign extends the result.
    if (!ST->is64Bit() || OffsetTail.getOpcode() == RISCV::ADDIW)
      Offset = SignExtend64<32>(Offset);
    // We can only fold simm32 offsets.
    if (!isInt<32>(Offset))
      return false;
    LLVM_DEBUG(dbgs() << "  Offset Instrs: " << OffsetTail
                      << "                 " << OffsetLui);
    foldOffset(Hi, Lo, TailAdd, Offset);
    OffsetTail.eraseFromParent();
    OffsetLui.eraseFromParent();
    return true;
  } else if (OffsetTail.getOpcode() == RISCV::LUI) {
    // The offset value has all zero bits in the lower 12 bits. Only LUI
    // exists.
    LLVM_DEBUG(dbgs() << "  Offset Instr: " << OffsetTail);
    int64_t Offset = SignExtend64<32>(OffsetTail.getOperand(1).getImm() << 12);
    foldOffset(Hi, Lo, TailAdd, Offset);
    OffsetTail.eraseFromParent();
    return true;
  }
  return false;
}

// Detect patterns for offsets that are passed into a SHXADD instruction.
// The offset has 1, 2, or 3 trailing zeros and fits in simm13, simm14, simm15.
// The constant is created with addi voff, x0, C, and shXadd is used to
// fill insert the trailing zeros and do the addition.
// If the pattern is found, updates the offset in Hi and Lo instructions
// and deletes TailShXAdd and the instructions that produced the offset.
//
// Hi:         lui     vreg1, %hi(s)
// Lo:         addi    vreg2, vreg1, %lo(s)
// OffsetTail: addi    voff, x0, C
// TailAdd:    shXadd  vreg4, voff, vreg2
bool RISCVMergeBaseOffsetOpt::foldShiftedOffset(MachineInstr &Hi,
                                                MachineInstr &Lo,
                                                MachineInstr &TailShXAdd,
                                                Register GAReg) {
  assert((TailShXAdd.getOpcode() == RISCV::SH1ADD ||
          TailShXAdd.getOpcode() == RISCV::SH2ADD ||
          TailShXAdd.getOpcode() == RISCV::SH3ADD) &&
         "Expected SHXADD instruction!");

  if (GAReg != TailShXAdd.getOperand(2).getReg())
    return false;

  // The first source is the shifted operand.
  Register Rs1 = TailShXAdd.getOperand(1).getReg();

  // Can't fold if the register has more than one use.
  if (!Rs1.isVirtual() || !MRI->hasOneUse(Rs1))
    return false;
  // This can point to an ADDI X0, C.
  MachineInstr &OffsetTail = *MRI->getVRegDef(Rs1);
  if (OffsetTail.getOpcode() != RISCV::ADDI)
    return false;
  if (!OffsetTail.getOperand(1).isReg() ||
      OffsetTail.getOperand(1).getReg() != RISCV::X0 ||
      !OffsetTail.getOperand(2).isImm())
    return false;

  int64_t Offset = OffsetTail.getOperand(2).getImm();
  assert(isInt<12>(Offset) && "Unexpected offset");

  unsigned ShAmt;
  switch (TailShXAdd.getOpcode()) {
  default: llvm_unreachable("Unexpected opcode");
  case RISCV::SH1ADD: ShAmt = 1; break;
  case RISCV::SH2ADD: ShAmt = 2; break;
  case RISCV::SH3ADD: ShAmt = 3; break;
  }

  Offset = (uint64_t)Offset << ShAmt;

  LLVM_DEBUG(dbgs() << "  Offset Instr: " << OffsetTail);
  foldOffset(Hi, Lo, TailShXAdd, Offset);
  OffsetTail.eraseFromParent();
  return true;
}

bool RISCVMergeBaseOffsetOpt::detectAndFoldOffset(MachineInstr &Hi,
                                                  MachineInstr &Lo) {
  Register DestReg = Lo.getOperand(0).getReg();

  // Look for arithmetic instructions we can get an offset from.
  // We might be able to remove the arithmetic instructions by folding the
  // offset into the LUI+ADDI.
  if (!MRI->hasOneUse(DestReg))
    return false;

  // Lo has only one use.
  MachineInstr &Tail = *MRI->use_instr_begin(DestReg);
  switch (Tail.getOpcode()) {
  default:
    LLVM_DEBUG(dbgs() << "Don't know how to get offset from this instr:"
                      << Tail);
    break;
  case RISCV::ADDI: {
    // Offset is simply an immediate operand.
    int64_t Offset = Tail.getOperand(2).getImm();

    // We might have two ADDIs in a row.
    Register TailDestReg = Tail.getOperand(0).getReg();
    if (MRI->hasOneUse(TailDestReg)) {
      MachineInstr &TailTail = *MRI->use_instr_begin(TailDestReg);
      if (TailTail.getOpcode() == RISCV::ADDI) {
        Offset += TailTail.getOperand(2).getImm();
        LLVM_DEBUG(dbgs() << "  Offset Instrs: " << Tail << TailTail);
        foldOffset(Hi, Lo, TailTail, Offset);
        Tail.eraseFromParent();
        return true;
      }
    }

    LLVM_DEBUG(dbgs() << "  Offset Instr: " << Tail);
    foldOffset(Hi, Lo, Tail, Offset);
    return true;
  }
  case RISCV::ADD:
    // The offset is too large to fit in the immediate field of ADDI.
    // This can be in two forms:
    // 1) LUI hi_Offset followed by:
    //    ADDI lo_offset
    //    This happens in case the offset has non zero bits in
    //    both hi 20 and lo 12 bits.
    // 2) LUI (offset20)
    //    This happens in case the lower 12 bits of the offset are zeros.
    return foldLargeOffset(Hi, Lo, Tail, DestReg);
  case RISCV::SH1ADD:
  case RISCV::SH2ADD:
  case RISCV::SH3ADD:
    // The offset is too large to fit in the immediate field of ADDI.
    // It may be encoded as (SH2ADD (ADDI X0, C), DestReg) or
    // (SH3ADD (ADDI X0, C), DestReg).
    return foldShiftedOffset(Hi, Lo, Tail, DestReg);
  }

  return false;
}

bool RISCVMergeBaseOffsetOpt::foldIntoMemoryOps(MachineInstr &Hi,
                                                MachineInstr &Lo) {
  Register DestReg = Lo.getOperand(0).getReg();

  // If all the uses are memory ops with the same offset, we can transform:
  //
  // 1. (medlow pattern):
  // Hi:   lui vreg1, %hi(foo)          --->  lui vreg1, %hi(foo+8)
  // Lo:   addi vreg2, vreg1, %lo(foo)  --->  lw vreg3, lo(foo+8)(vreg1)
  // Tail: lw vreg3, 8(vreg2)
  //
  // 2. (medany pattern):
  // Hi: 1:auipc vreg1, %pcrel_hi(s)         ---> auipc vreg1, %pcrel_hi(foo+8)
  // Lo:   addi  vreg2, vreg1, %pcrel_lo(1b) ---> lw vreg3, %pcrel_lo(1b)(vreg1)
  // Tail: lw vreg3, 8(vreg2)

  std::optional<int64_t> CommonOffset;
  DenseMap<const MachineInstr *, SmallVector<unsigned>>
      InlineAsmMemoryOpIndexesMap;
  for (const MachineInstr &UseMI : MRI->use_instructions(DestReg)) {
    switch (UseMI.getOpcode()) {
    default:
      LLVM_DEBUG(dbgs() << "Not a load or store instruction: " << UseMI);
      return false;
    case RISCV::LB:
    case RISCV::LH:
    case RISCV::LW:
    case RISCV::LBU:
    case RISCV::LHU:
    case RISCV::LWU:
    case RISCV::LD:
    case RISCV::FLH:
    case RISCV::FLW:
    case RISCV::FLD:
    case RISCV::SB:
    case RISCV::SH:
    case RISCV::SW:
    case RISCV::SD:
    case RISCV::FSH:
    case RISCV::FSW:
    case RISCV::FSD: {
      if (UseMI.getOperand(1).isFI())
        return false;
      // Register defined by Lo should not be the value register.
      if (DestReg == UseMI.getOperand(0).getReg())
        return false;
      assert(DestReg == UseMI.getOperand(1).getReg() &&
             "Expected base address use");
      // All load/store instructions must use the same offset.
      int64_t Offset = UseMI.getOperand(2).getImm();
      if (CommonOffset && Offset != CommonOffset)
        return false;
      CommonOffset = Offset;
      break;
    }
    case RISCV::INLINEASM:
    case RISCV::INLINEASM_BR: {
      SmallVector<unsigned> InlineAsmMemoryOpIndexes;
      unsigned NumOps = 0;
      for (unsigned I = InlineAsm::MIOp_FirstOperand;
           I < UseMI.getNumOperands(); I += 1 + NumOps) {
        const MachineOperand &FlagsMO = UseMI.getOperand(I);
        // Should be an imm.
        if (!FlagsMO.isImm())
          continue;

        const InlineAsm::Flag Flags(FlagsMO.getImm());
        NumOps = Flags.getNumOperandRegisters();

        // Memory constraints have two operands.
        if (NumOps != 2 || !Flags.isMemKind()) {
          // If the register is used by something other than a memory contraint,
          // we should not fold.
          for (unsigned J = 0; J < NumOps; ++J) {
            const MachineOperand &MO = UseMI.getOperand(I + 1 + J);
            if (MO.isReg() && MO.getReg() == DestReg)
              return false;
          }
          continue;
        }

        // We can't do this for constraint A because AMO instructions don't have
        // an immediate offset field.
        if (Flags.getMemoryConstraintID() == InlineAsm::ConstraintCode::A)
          return false;

        const MachineOperand &AddrMO = UseMI.getOperand(I + 1);
        if (!AddrMO.isReg() || AddrMO.getReg() != DestReg)
          continue;

        const MachineOperand &OffsetMO = UseMI.getOperand(I + 2);
        if (!OffsetMO.isImm())
          continue;

        // All inline asm memory operands must use the same offset.
        int64_t Offset = OffsetMO.getImm();
        if (CommonOffset && Offset != CommonOffset)
          return false;
        CommonOffset = Offset;
        InlineAsmMemoryOpIndexes.push_back(I + 1);
      }
      InlineAsmMemoryOpIndexesMap.insert(
          std::make_pair(&UseMI, InlineAsmMemoryOpIndexes));
      break;
    }
    }
  }

  // We found a common offset.
  // Update the offsets in global address lowering.
  // We may have already folded some arithmetic so we need to add to any
  // existing offset.
  int64_t NewOffset = Hi.getOperand(1).getOffset() + *CommonOffset;
  // RV32 ignores the upper 32 bits.
  if (!ST->is64Bit())
    NewOffset = SignExtend64<32>(NewOffset);
  // We can only fold simm32 offsets.
  if (!isInt<32>(NewOffset))
    return false;

  Hi.getOperand(1).setOffset(NewOffset);
  MachineOperand &ImmOp = Lo.getOperand(2);
  // Expand PseudoMovAddr into LUI
  if (Hi.getOpcode() == RISCV::PseudoMovAddr) {
    auto *TII = ST->getInstrInfo();
    Hi.setDesc(TII->get(RISCV::LUI));
    Hi.removeOperand(2);
  }

  if (Hi.getOpcode() != RISCV::AUIPC)
    ImmOp.setOffset(NewOffset);

  // Update the immediate in the load/store instructions to add the offset.
  for (MachineInstr &UseMI :
       llvm::make_early_inc_range(MRI->use_instructions(DestReg))) {
    if (UseMI.getOpcode() == RISCV::INLINEASM ||
        UseMI.getOpcode() == RISCV::INLINEASM_BR) {
      auto &InlineAsmMemoryOpIndexes = InlineAsmMemoryOpIndexesMap[&UseMI];
      for (unsigned I : InlineAsmMemoryOpIndexes) {
        MachineOperand &MO = UseMI.getOperand(I + 1);
        switch (ImmOp.getType()) {
        case MachineOperand::MO_GlobalAddress:
          MO.ChangeToGA(ImmOp.getGlobal(), ImmOp.getOffset(),
                        ImmOp.getTargetFlags());
          break;
        case MachineOperand::MO_MCSymbol:
          MO.ChangeToMCSymbol(ImmOp.getMCSymbol(), ImmOp.getTargetFlags());
          MO.setOffset(ImmOp.getOffset());
          break;
        case MachineOperand::MO_BlockAddress:
          MO.ChangeToBA(ImmOp.getBlockAddress(), ImmOp.getOffset(),
                        ImmOp.getTargetFlags());
          break;
        default:
          report_fatal_error("unsupported machine operand type");
          break;
        }
      }
    } else {
      UseMI.removeOperand(2);
      UseMI.addOperand(ImmOp);
    }
  }

  // Prevent Lo (originally PseudoMovAddr, which is also pointed by Hi) from
  // being erased
  if (&Lo == &Hi)
    return true;

  MRI->replaceRegWith(Lo.getOperand(0).getReg(), Hi.getOperand(0).getReg());
  Lo.eraseFromParent();
  return true;
}

bool RISCVMergeBaseOffsetOpt::runOnMachineFunction(MachineFunction &Fn) {
  if (skipFunction(Fn.getFunction()))
    return false;

  ST = &Fn.getSubtarget<RISCVSubtarget>();

  bool MadeChange = false;
  MRI = &Fn.getRegInfo();
  for (MachineBasicBlock &MBB : Fn) {
    LLVM_DEBUG(dbgs() << "MBB: " << MBB.getName() << "\n");
    for (MachineInstr &Hi : MBB) {
      MachineInstr *Lo = nullptr;
      if (!detectFoldable(Hi, Lo))
        continue;
      MadeChange |= detectAndFoldOffset(Hi, *Lo);
      MadeChange |= foldIntoMemoryOps(Hi, *Lo);
    }
  }

  return MadeChange;
}

/// Returns an instance of the Merge Base Offset Optimization pass.
FunctionPass *llvm::createRISCVMergeBaseOffsetOptPass() {
  return new RISCVMergeBaseOffsetOpt();
}
