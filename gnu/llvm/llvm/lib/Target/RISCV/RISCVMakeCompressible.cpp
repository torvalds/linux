//===-- RISCVMakeCompressible.cpp - Make more instructions compressible ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass searches for instructions that are prevented from being compressed
// by one of the following:
//
//   1. The use of a single uncompressed register.
//   2. A base register + offset where the offset is too large to be compressed
//   and the base register may or may not be compressed.
//
//
// For case 1, if a compressed register is available, then the uncompressed
// register is copied to the compressed register and its uses are replaced.
//
// For example, storing zero uses the uncompressible zero register:
//   sw zero, 0(a0)   # if zero
//   sw zero, 8(a0)   # if zero
//   sw zero, 4(a0)   # if zero
//   sw zero, 24(a0)   # if zero
//
// If a compressed register (e.g. a1) is available, the above can be transformed
// to the following to improve code size:
//   li a1, 0
//   c.sw a1, 0(a0)
//   c.sw a1, 8(a0)
//   c.sw a1, 4(a0)
//   c.sw a1, 24(a0)
//
//
// For case 2, if a compressed register is available, then the original base
// is copied and adjusted such that:
//
//   new_base_register = base_register + adjustment
//   base_register + large_offset = new_base_register + small_offset
//
// For example, the following offsets are too large for c.sw:
//   lui a2, 983065
//   sw  a1, -236(a2)
//   sw  a1, -240(a2)
//   sw  a1, -244(a2)
//   sw  a1, -248(a2)
//   sw  a1, -252(a2)
//   sw  a0, -256(a2)
//
// If a compressed register is available (e.g. a3), a new base could be created
// such that the addresses can accessed with a compressible offset, thus
// improving code size:
//   lui a2, 983065
//   addi  a3, a2, -256
//   c.sw  a1, 20(a3)
//   c.sw  a1, 16(a3)
//   c.sw  a1, 12(a3)
//   c.sw  a1, 8(a3)
//   c.sw  a1, 4(a3)
//   c.sw  a0, 0(a3)
//
//
// This optimization is only applied if there are enough uses of the copied
// register for code size to be reduced.
//
//===----------------------------------------------------------------------===//

#include "RISCV.h"
#include "RISCVSubtarget.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Debug.h"

using namespace llvm;

#define DEBUG_TYPE "riscv-make-compressible"
#define RISCV_COMPRESS_INSTRS_NAME "RISC-V Make Compressible"

namespace {

struct RISCVMakeCompressibleOpt : public MachineFunctionPass {
  static char ID;

  bool runOnMachineFunction(MachineFunction &Fn) override;

  RISCVMakeCompressibleOpt() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return RISCV_COMPRESS_INSTRS_NAME; }
};
} // namespace

char RISCVMakeCompressibleOpt::ID = 0;
INITIALIZE_PASS(RISCVMakeCompressibleOpt, "riscv-make-compressible",
                RISCV_COMPRESS_INSTRS_NAME, false, false)

// Return log2(widthInBytes) of load/store done by Opcode.
static unsigned log2LdstWidth(unsigned Opcode) {
  switch (Opcode) {
  default:
    llvm_unreachable("Unexpected opcode");
  case RISCV::LBU:
  case RISCV::SB:
    return 0;
  case RISCV::LH:
  case RISCV::LHU:
  case RISCV::SH:
    return 1;
  case RISCV::LW:
  case RISCV::SW:
  case RISCV::FLW:
  case RISCV::FSW:
    return 2;
  case RISCV::LD:
  case RISCV::SD:
  case RISCV::FLD:
  case RISCV::FSD:
    return 3;
  }
}

// Return bit field size of immediate operand of Opcode.
static unsigned offsetMask(unsigned Opcode) {
  switch (Opcode) {
  default:
    llvm_unreachable("Unexpected opcode");
  case RISCV::LBU:
  case RISCV::SB:
    return maskTrailingOnes<unsigned>(2U);
  case RISCV::LH:
  case RISCV::LHU:
  case RISCV::SH:
    return maskTrailingOnes<unsigned>(1U);
  case RISCV::LW:
  case RISCV::SW:
  case RISCV::FLW:
  case RISCV::FSW:
  case RISCV::LD:
  case RISCV::SD:
  case RISCV::FLD:
  case RISCV::FSD:
    return maskTrailingOnes<unsigned>(5U);
  }
}

// Return a mask for the offset bits of a non-stack-pointer based compressed
// load/store.
static uint8_t compressedLDSTOffsetMask(unsigned Opcode) {
  return offsetMask(Opcode) << log2LdstWidth(Opcode);
}

// Return true if Offset fits within a compressed stack-pointer based
// load/store.
static bool compressibleSPOffset(int64_t Offset, unsigned Opcode) {
  // Compressed sp-based loads and stores only work for 32/64 bits.
  switch (log2LdstWidth(Opcode)) {
  case 2:
    return isShiftedUInt<6, 2>(Offset);
  case 3:
    return isShiftedUInt<6, 3>(Offset);
  }
  return false;
}

// Given an offset for a load/store, return the adjustment required to the base
// register such that the address can be accessed with a compressible offset.
// This will return 0 if the offset is already compressible.
static int64_t getBaseAdjustForCompression(int64_t Offset, unsigned Opcode) {
  // Return the excess bits that do not fit in a compressible offset.
  return Offset & ~compressedLDSTOffsetMask(Opcode);
}

// Return true if Reg is in a compressed register class.
static bool isCompressedReg(Register Reg) {
  return RISCV::GPRCRegClass.contains(Reg) ||
         RISCV::FPR32CRegClass.contains(Reg) ||
         RISCV::FPR64CRegClass.contains(Reg);
}

// Return true if MI is a load for which there exists a compressed version.
static bool isCompressibleLoad(const MachineInstr &MI) {
  const RISCVSubtarget &STI = MI.getMF()->getSubtarget<RISCVSubtarget>();

  switch (MI.getOpcode()) {
  default:
    return false;
  case RISCV::LBU:
  case RISCV::LH:
  case RISCV::LHU:
    return STI.hasStdExtZcb();
  case RISCV::LW:
  case RISCV::LD:
    return STI.hasStdExtCOrZca();
  case RISCV::FLW:
    return !STI.is64Bit() && STI.hasStdExtCOrZcfOrZce();
  case RISCV::FLD:
    return STI.hasStdExtCOrZcd();
  }
}

// Return true if MI is a store for which there exists a compressed version.
static bool isCompressibleStore(const MachineInstr &MI) {
  const RISCVSubtarget &STI = MI.getMF()->getSubtarget<RISCVSubtarget>();

  switch (MI.getOpcode()) {
  default:
    return false;
  case RISCV::SB:
  case RISCV::SH:
    return STI.hasStdExtZcb();
  case RISCV::SW:
  case RISCV::SD:
    return STI.hasStdExtCOrZca();
  case RISCV::FSW:
    return !STI.is64Bit() && STI.hasStdExtCOrZcfOrZce();
  case RISCV::FSD:
    return STI.hasStdExtCOrZcd();
  }
}

// Find a single register and/or large offset which, if compressible, would
// allow the given instruction to be compressed.
//
// Possible return values:
//
//   {Reg, 0}               - Uncompressed Reg needs replacing with a compressed
//                            register.
//   {Reg, N}               - Reg needs replacing with a compressed register and
//                            N needs adding to the new register. (Reg may be
//                            compressed or uncompressed).
//   {RISCV::NoRegister, 0} - No suitable optimization found for this
//   instruction.
static RegImmPair getRegImmPairPreventingCompression(const MachineInstr &MI) {
  const unsigned Opcode = MI.getOpcode();

  if (isCompressibleLoad(MI) || isCompressibleStore(MI)) {
    const MachineOperand &MOImm = MI.getOperand(2);
    if (!MOImm.isImm())
      return RegImmPair(RISCV::NoRegister, 0);

    int64_t Offset = MOImm.getImm();
    int64_t NewBaseAdjust = getBaseAdjustForCompression(Offset, Opcode);
    Register Base = MI.getOperand(1).getReg();

    // Memory accesses via the stack pointer do not have a requirement for
    // either of the registers to be compressible and can take a larger offset.
    if (RISCV::SPRegClass.contains(Base)) {
      if (!compressibleSPOffset(Offset, Opcode) && NewBaseAdjust)
        return RegImmPair(Base, NewBaseAdjust);
    } else {
      Register SrcDest = MI.getOperand(0).getReg();
      bool SrcDestCompressed = isCompressedReg(SrcDest);
      bool BaseCompressed = isCompressedReg(Base);

      // If only Base and/or offset prevent compression, then return Base and
      // any adjustment required to make the offset compressible.
      if ((!BaseCompressed || NewBaseAdjust) && SrcDestCompressed)
        return RegImmPair(Base, NewBaseAdjust);

      // For loads, we can only change the base register since dest is defined
      // rather than used.
      //
      // For stores, we can change SrcDest (and Base if SrcDest == Base) but
      // cannot resolve an uncompressible offset in this case.
      if (isCompressibleStore(MI)) {
        if (!SrcDestCompressed && (BaseCompressed || SrcDest == Base) &&
            !NewBaseAdjust)
          return RegImmPair(SrcDest, NewBaseAdjust);
      }
    }
  }
  return RegImmPair(RISCV::NoRegister, 0);
}

// Check all uses after FirstMI of the given register, keeping a vector of
// instructions that would be compressible if the given register (and offset if
// applicable) were compressible.
//
// If there are enough uses for this optimization to improve code size and a
// compressed register is available, return that compressed register.
static Register analyzeCompressibleUses(MachineInstr &FirstMI,
                                        RegImmPair RegImm,
                                        SmallVectorImpl<MachineInstr *> &MIs) {
  MachineBasicBlock &MBB = *FirstMI.getParent();
  const TargetRegisterInfo *TRI =
      MBB.getParent()->getSubtarget().getRegisterInfo();

  for (MachineBasicBlock::instr_iterator I = FirstMI.getIterator(),
                                         E = MBB.instr_end();
       I != E; ++I) {
    MachineInstr &MI = *I;

    // Determine if this is an instruction which would benefit from using the
    // new register.
    RegImmPair CandidateRegImm = getRegImmPairPreventingCompression(MI);
    if (CandidateRegImm.Reg == RegImm.Reg && CandidateRegImm.Imm == RegImm.Imm)
      MIs.push_back(&MI);

    // If RegImm.Reg is modified by this instruction, then we cannot optimize
    // past this instruction. If the register is already compressed, then it may
    // possible to optimize a large offset in the current instruction - this
    // will have been detected by the preceeding call to
    // getRegImmPairPreventingCompression.
    if (MI.modifiesRegister(RegImm.Reg, TRI))
      break;
  }

  // Adjusting the base costs one new uncompressed addi and therefore three uses
  // are required for a code size reduction. If no base adjustment is required,
  // then copying the register costs one new c.mv (or c.li Rd, 0 for "copying"
  // the zero register) and therefore two uses are required for a code size
  // reduction.
  if (MIs.size() < 2 || (RegImm.Imm != 0 && MIs.size() < 3))
    return RISCV::NoRegister;

  // Find a compressible register which will be available from the first
  // instruction we care about to the last.
  const TargetRegisterClass *RCToScavenge;

  // Work out the compressed register class from which to scavenge.
  if (RISCV::GPRRegClass.contains(RegImm.Reg))
    RCToScavenge = &RISCV::GPRCRegClass;
  else if (RISCV::FPR32RegClass.contains(RegImm.Reg))
    RCToScavenge = &RISCV::FPR32CRegClass;
  else if (RISCV::FPR64RegClass.contains(RegImm.Reg))
    RCToScavenge = &RISCV::FPR64CRegClass;
  else
    return RISCV::NoRegister;

  RegScavenger RS;
  RS.enterBasicBlockEnd(MBB);
  RS.backward(std::next(MIs.back()->getIterator()));
  return RS.scavengeRegisterBackwards(*RCToScavenge, FirstMI.getIterator(),
                                      /*RestoreAfter=*/false, /*SPAdj=*/0,
                                      /*AllowSpill=*/false);
}

// Update uses of the old register in the given instruction to the new register.
static void updateOperands(MachineInstr &MI, RegImmPair OldRegImm,
                           Register NewReg) {
  unsigned Opcode = MI.getOpcode();

  // If this pass is extended to support more instructions, the check for
  // definedness may need to be strengthened.
  assert((isCompressibleLoad(MI) || isCompressibleStore(MI)) &&
         "Unsupported instruction for this optimization.");

  int SkipN = 0;

  // Skip the first (value) operand to a store instruction (except if the store
  // offset is zero) in order to avoid an incorrect transformation.
  // e.g. sd a0, 808(a0) to addi a2, a0, 768; sd a2, 40(a2)
  if (isCompressibleStore(MI) && OldRegImm.Imm != 0)
    SkipN = 1;

  // Update registers
  for (MachineOperand &MO : drop_begin(MI.operands(), SkipN))
    if (MO.isReg() && MO.getReg() == OldRegImm.Reg) {
      // Do not update operands that define the old register.
      //
      // The new register was scavenged for the range of instructions that are
      // being updated, therefore it should not be defined within this range
      // except possibly in the final instruction.
      if (MO.isDef()) {
        assert(isCompressibleLoad(MI));
        continue;
      }
      // Update reg
      MO.setReg(NewReg);
    }

  // Update offset
  MachineOperand &MOImm = MI.getOperand(2);
  int64_t NewOffset = MOImm.getImm() & compressedLDSTOffsetMask(Opcode);
  MOImm.setImm(NewOffset);
}

bool RISCVMakeCompressibleOpt::runOnMachineFunction(MachineFunction &Fn) {
  // This is a size optimization.
  if (skipFunction(Fn.getFunction()) || !Fn.getFunction().hasMinSize())
    return false;

  const RISCVSubtarget &STI = Fn.getSubtarget<RISCVSubtarget>();
  const RISCVInstrInfo &TII = *STI.getInstrInfo();

  // This optimization only makes sense if compressed instructions are emitted.
  if (!STI.hasStdExtCOrZca())
    return false;

  for (MachineBasicBlock &MBB : Fn) {
    LLVM_DEBUG(dbgs() << "MBB: " << MBB.getName() << "\n");
    for (MachineInstr &MI : MBB) {
      // Determine if this instruction would otherwise be compressed if not for
      // an uncompressible register or offset.
      RegImmPair RegImm = getRegImmPairPreventingCompression(MI);
      if (!RegImm.Reg && RegImm.Imm == 0)
        continue;

      // Determine if there is a set of instructions for which replacing this
      // register with a compressed register (and compressible offset if
      // applicable) is possible and will allow compression.
      SmallVector<MachineInstr *, 8> MIs;
      Register NewReg = analyzeCompressibleUses(MI, RegImm, MIs);
      if (!NewReg)
        continue;

      // Create the appropriate copy and/or offset.
      if (RISCV::GPRRegClass.contains(RegImm.Reg)) {
        assert(isInt<12>(RegImm.Imm));
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(RISCV::ADDI), NewReg)
            .addReg(RegImm.Reg)
            .addImm(RegImm.Imm);
      } else {
        // If we are looking at replacing an FPR register we don't expect to
        // have any offset. The only compressible FP instructions with an offset
        // are loads and stores, for which the offset applies to the GPR operand
        // not the FPR operand.
        assert(RegImm.Imm == 0);
        unsigned Opcode = RISCV::FPR32RegClass.contains(RegImm.Reg)
                              ? RISCV::FSGNJ_S
                              : RISCV::FSGNJ_D;
        BuildMI(MBB, MI, MI.getDebugLoc(), TII.get(Opcode), NewReg)
            .addReg(RegImm.Reg)
            .addReg(RegImm.Reg);
      }

      // Update the set of instructions to use the compressed register and
      // compressible offset instead. These instructions should now be
      // compressible.
      // TODO: Update all uses if RegImm.Imm == 0? Not just those that are
      // expected to become compressible.
      for (MachineInstr *UpdateMI : MIs)
        updateOperands(*UpdateMI, RegImm, NewReg);
    }
  }
  return true;
}

/// Returns an instance of the Make Compressible Optimization pass.
FunctionPass *llvm::createRISCVMakeCompressibleOptPass() {
  return new RISCVMakeCompressibleOpt();
}
