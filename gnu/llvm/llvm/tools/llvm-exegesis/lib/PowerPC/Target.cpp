//===-- Target.cpp ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// The PowerPC ExegesisTarget.
//===----------------------------------------------------------------------===//
#include "../Target.h"
#include "PPC.h"
#include "PPCRegisterInfo.h"

#define GET_AVAILABLE_OPCODE_CHECKER
#include "PPCGenInstrInfo.inc"

namespace llvm {
namespace exegesis {

// Helper to fill a memory operand with a value.
static void setMemOp(InstructionTemplate &IT, int OpIdx,
                     const MCOperand &OpVal) {
  const auto Op = IT.getInstr().Operands[OpIdx];
  assert(Op.isExplicit() && "invalid memory pattern");
  IT.getValueFor(Op) = OpVal;
}

#include "PPCGenExegesis.inc"

namespace {
class ExegesisPowerPCTarget : public ExegesisTarget {
public:
  ExegesisPowerPCTarget()
      : ExegesisTarget(PPCCpuPfmCounters, PPC_MC::isOpcodeAvailable) {}

private:
  std::vector<MCInst> setRegTo(const MCSubtargetInfo &STI, unsigned Reg,
                               const APInt &Value) const override;
  bool matchesArch(Triple::ArchType Arch) const override {
    return Arch == Triple::ppc64le;
  }
  unsigned getScratchMemoryRegister(const Triple &) const override;
  void fillMemoryOperands(InstructionTemplate &IT, unsigned Reg,
                          unsigned Offset) const override;
};
} // end anonymous namespace

static unsigned getLoadImmediateOpcode(unsigned RegBitWidth) {
  switch (RegBitWidth) {
  case 32:
    return PPC::LI;
  case 64:
    return PPC::LI8;
  }
  llvm_unreachable("Invalid Value Width");
}

// Generates instruction to load an immediate value into a register.
static MCInst loadImmediate(unsigned Reg, unsigned RegBitWidth,
                            const APInt &Value) {
  if (Value.getBitWidth() > RegBitWidth)
    llvm_unreachable("Value must fit in the Register");
  // We don't really care the value in reg, ignore the 16 bit
  // restriction for now.
  // TODO: make sure we get the exact value in reg if needed.
  return MCInstBuilder(getLoadImmediateOpcode(RegBitWidth))
      .addReg(Reg)
      .addImm(Value.getZExtValue());
}

unsigned
ExegesisPowerPCTarget::getScratchMemoryRegister(const Triple &TT) const {
  // R13 is reserved as Thread Pointer, we won't use threading in benchmark, so
  // use it as scratch memory register
  return TT.isArch64Bit() ? PPC::X13 : PPC::R13;
}

void ExegesisPowerPCTarget::fillMemoryOperands(InstructionTemplate &IT,
                                               unsigned Reg,
                                               unsigned Offset) const {
  int MemOpIdx = 0;
  if (IT.getInstr().hasTiedRegisters())
    MemOpIdx = 1;
  int DispOpIdx = MemOpIdx + 1;
  const auto DispOp = IT.getInstr().Operands[DispOpIdx];
  if (DispOp.isReg())
    // We don't really care about the real address in snippets,
    // So hardcode X1 for X-form Memory Operations for simplicity.
    // TODO: materialize the offset into a reggister
    setMemOp(IT, DispOpIdx, MCOperand::createReg(PPC::X1));
  else
    setMemOp(IT, DispOpIdx, MCOperand::createImm(Offset)); // Disp
  setMemOp(IT, MemOpIdx + 2, MCOperand::createReg(Reg));   // BaseReg
}

std::vector<MCInst> ExegesisPowerPCTarget::setRegTo(const MCSubtargetInfo &STI,
                                                    unsigned Reg,
                                                    const APInt &Value) const {
  // X11 is optional use in function linkage, should be the least used one
  // Use it as scratch reg to load immediate.
  unsigned ScratchImmReg = PPC::X11;

  if (PPC::GPRCRegClass.contains(Reg))
    return {loadImmediate(Reg, 32, Value)};
  if (PPC::G8RCRegClass.contains(Reg))
    return {loadImmediate(Reg, 64, Value)};
  if (PPC::F4RCRegClass.contains(Reg))
    return {loadImmediate(ScratchImmReg, 64, Value),
            MCInstBuilder(PPC::MTVSRD).addReg(Reg).addReg(ScratchImmReg)};
  // We don't care the real value in reg, so set 64 bits or duplicate 64 bits
  // for simplicity.
  // TODO: update these if we need a accurate 128 values in registers.
  if (PPC::VRRCRegClass.contains(Reg))
    return {loadImmediate(ScratchImmReg, 64, Value),
            MCInstBuilder(PPC::MTVRD).addReg(Reg).addReg(ScratchImmReg)};
  if (PPC::VSRCRegClass.contains(Reg))
    return {loadImmediate(ScratchImmReg, 64, Value),
            MCInstBuilder(PPC::MTVSRDD)
                .addReg(Reg)
                .addReg(ScratchImmReg)
                .addReg(ScratchImmReg)};
  if (PPC::VFRCRegClass.contains(Reg))
    return {loadImmediate(ScratchImmReg, 64, Value),
            MCInstBuilder(PPC::MTVSRD).addReg(Reg).addReg(ScratchImmReg)};
  // SPE not supported yet
  if (PPC::SPERCRegClass.contains(Reg)) {
    errs() << "Unsupported SPE Reg:" << Reg << "\n";
    return {};
  }
  errs() << "setRegTo is not implemented, results will be unreliable:" << Reg
         << "\n";
  return {};
}

static ExegesisTarget *getTheExegesisPowerPCTarget() {
  static ExegesisPowerPCTarget Target;
  return &Target;
}

void InitializePowerPCExegesisTarget() {
  ExegesisTarget::registerTarget(getTheExegesisPowerPCTarget());
}

} // namespace exegesis
} // namespace llvm
