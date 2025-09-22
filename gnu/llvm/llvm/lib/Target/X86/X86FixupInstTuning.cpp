//===-- X86FixupInstTunings.cpp - replace instructions -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file does a tuning pass replacing slower machine instructions
// with faster ones. We do this here, as opposed to during normal ISel, as
// attempting to get the "right" instruction can break patterns. This pass
// is not meant search for special cases where an instruction can be transformed
// to another, it is only meant to do transformations where the old instruction
// is always replacable with the new instructions. For example:
//
//      `vpermq ymm` -> `vshufd ymm`
//          -- BAD, not always valid (lane cross/non-repeated mask)
//
//      `vpermilps ymm` -> `vshufd ymm`
//          -- GOOD, always replaceable
//
//===----------------------------------------------------------------------===//

#include "X86.h"
#include "X86InstrInfo.h"
#include "X86Subtarget.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"

using namespace llvm;

#define DEBUG_TYPE "x86-fixup-inst-tuning"

STATISTIC(NumInstChanges, "Number of instructions changes");

namespace {
class X86FixupInstTuningPass : public MachineFunctionPass {
public:
  static char ID;

  X86FixupInstTuningPass() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override { return "X86 Fixup Inst Tuning"; }

  bool runOnMachineFunction(MachineFunction &MF) override;
  bool processInstruction(MachineFunction &MF, MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator &I);

  // This pass runs after regalloc and doesn't support VReg operands.
  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().set(
        MachineFunctionProperties::Property::NoVRegs);
  }

private:
  const X86InstrInfo *TII = nullptr;
  const X86Subtarget *ST = nullptr;
  const MCSchedModel *SM = nullptr;
};
} // end anonymous namespace

char X86FixupInstTuningPass::ID = 0;

INITIALIZE_PASS(X86FixupInstTuningPass, DEBUG_TYPE, DEBUG_TYPE, false, false)

FunctionPass *llvm::createX86FixupInstTuning() {
  return new X86FixupInstTuningPass();
}

template <typename T>
static std::optional<bool> CmpOptionals(T NewVal, T CurVal) {
  if (NewVal.has_value() && CurVal.has_value() && *NewVal != *CurVal)
    return *NewVal < *CurVal;

  return std::nullopt;
}

bool X86FixupInstTuningPass::processInstruction(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator &I) {
  MachineInstr &MI = *I;
  unsigned Opc = MI.getOpcode();
  unsigned NumOperands = MI.getDesc().getNumOperands();

  auto GetInstTput = [&](unsigned Opcode) -> std::optional<double> {
    // We already checked that SchedModel exists in `NewOpcPreferable`.
    return MCSchedModel::getReciprocalThroughput(
        *ST, *(SM->getSchedClassDesc(TII->get(Opcode).getSchedClass())));
  };

  auto GetInstLat = [&](unsigned Opcode) -> std::optional<double> {
    // We already checked that SchedModel exists in `NewOpcPreferable`.
    return MCSchedModel::computeInstrLatency(
        *ST, *(SM->getSchedClassDesc(TII->get(Opcode).getSchedClass())));
  };

  auto GetInstSize = [&](unsigned Opcode) -> std::optional<unsigned> {
    if (unsigned Size = TII->get(Opcode).getSize())
      return Size;
    // Zero size means we where unable to compute it.
    return std::nullopt;
  };

  auto NewOpcPreferable = [&](unsigned NewOpc,
                              bool ReplaceInTie = true) -> bool {
    std::optional<bool> Res;
    if (SM->hasInstrSchedModel()) {
      // Compare tput -> lat -> code size.
      Res = CmpOptionals(GetInstTput(NewOpc), GetInstTput(Opc));
      if (Res.has_value())
        return *Res;

      Res = CmpOptionals(GetInstLat(NewOpc), GetInstLat(Opc));
      if (Res.has_value())
        return *Res;
    }

    Res = CmpOptionals(GetInstSize(Opc), GetInstSize(NewOpc));
    if (Res.has_value())
      return *Res;

    // We either have either were unable to get tput/lat/codesize or all values
    // were equal. Return specified option for a tie.
    return ReplaceInTie;
  };

  // `vpermilpd r, i` -> `vshufpd r, r, i`
  // `vpermilpd r, i, k` -> `vshufpd r, r, i, k`
  // `vshufpd` is always as fast or faster than `vpermilpd` and takes
  // 1 less byte of code size for VEX and EVEX encoding.
  auto ProcessVPERMILPDri = [&](unsigned NewOpc) -> bool {
    if (!NewOpcPreferable(NewOpc))
      return false;
    unsigned MaskImm = MI.getOperand(NumOperands - 1).getImm();
    MI.removeOperand(NumOperands - 1);
    MI.addOperand(MI.getOperand(NumOperands - 2));
    MI.setDesc(TII->get(NewOpc));
    MI.addOperand(MachineOperand::CreateImm(MaskImm));
    return true;
  };

  // `vpermilps r, i` -> `vshufps r, r, i`
  // `vpermilps r, i, k` -> `vshufps r, r, i, k`
  // `vshufps` is always as fast or faster than `vpermilps` and takes
  // 1 less byte of code size for VEX and EVEX encoding.
  auto ProcessVPERMILPSri = [&](unsigned NewOpc) -> bool {
    if (!NewOpcPreferable(NewOpc))
      return false;
    unsigned MaskImm = MI.getOperand(NumOperands - 1).getImm();
    MI.removeOperand(NumOperands - 1);
    MI.addOperand(MI.getOperand(NumOperands - 2));
    MI.setDesc(TII->get(NewOpc));
    MI.addOperand(MachineOperand::CreateImm(MaskImm));
    return true;
  };

  // `vpermilps m, i` -> `vpshufd m, i` iff no domain delay penalty on shuffles.
  // `vpshufd` is always as fast or faster than `vpermilps` and takes 1 less
  // byte of code size.
  auto ProcessVPERMILPSmi = [&](unsigned NewOpc) -> bool {
    // TODO: Might be work adding bypass delay if -Os/-Oz is enabled as
    // `vpshufd` saves a byte of code size.
    if (!ST->hasNoDomainDelayShuffle() ||
        !NewOpcPreferable(NewOpc, /*ReplaceInTie*/ false))
      return false;
    MI.setDesc(TII->get(NewOpc));
    return true;
  };

  // `vunpcklpd/vmovlhps r, r` -> `vunpcklqdq r, r`/`vshufpd r, r, 0x00`
  // `vunpckhpd/vmovlhps r, r` -> `vunpckhqdq r, r`/`vshufpd r, r, 0xff`
  // `vunpcklpd r, r, k` -> `vunpcklqdq r, r, k`/`vshufpd r, r, k, 0x00`
  // `vunpckhpd r, r, k` -> `vunpckhqdq r, r, k`/`vshufpd r, r, k, 0xff`
  // `vunpcklpd r, m` -> `vunpcklqdq r, m, k`
  // `vunpckhpd r, m` -> `vunpckhqdq r, m, k`
  // `vunpcklpd r, m, k` -> `vunpcklqdq r, m, k`
  // `vunpckhpd r, m, k` -> `vunpckhqdq r, m, k`
  // 1) If no bypass delay and `vunpck{l|h}qdq` faster than `vunpck{l|h}pd`
  //        -> `vunpck{l|h}qdq`
  // 2) If `vshufpd` faster than `vunpck{l|h}pd`
  //        -> `vshufpd`
  //
  // `vunpcklps` -> `vunpckldq` (for all operand types if no bypass delay)
  auto ProcessUNPCK = [&](unsigned NewOpc, unsigned MaskImm) -> bool {
    if (!NewOpcPreferable(NewOpc, /*ReplaceInTie*/ false))
      return false;

    MI.setDesc(TII->get(NewOpc));
    MI.addOperand(MachineOperand::CreateImm(MaskImm));
    return true;
  };

  auto ProcessUNPCKToIntDomain = [&](unsigned NewOpc) -> bool {
    // TODO it may be worth it to set ReplaceInTie to `true` as there is no real
    // downside to the integer unpck, but if someone doesn't specify exact
    // target we won't find it faster.
    if (!ST->hasNoDomainDelayShuffle() ||
        !NewOpcPreferable(NewOpc, /*ReplaceInTie*/ false))
      return false;
    MI.setDesc(TII->get(NewOpc));
    return true;
  };

  auto ProcessUNPCKLPDrr = [&](unsigned NewOpcIntDomain,
                               unsigned NewOpc) -> bool {
    if (ProcessUNPCKToIntDomain(NewOpcIntDomain))
      return true;
    return ProcessUNPCK(NewOpc, 0x00);
  };
  auto ProcessUNPCKHPDrr = [&](unsigned NewOpcIntDomain,
                               unsigned NewOpc) -> bool {
    if (ProcessUNPCKToIntDomain(NewOpcIntDomain))
      return true;
    return ProcessUNPCK(NewOpc, 0xff);
  };

  auto ProcessUNPCKPDrm = [&](unsigned NewOpcIntDomain) -> bool {
    return ProcessUNPCKToIntDomain(NewOpcIntDomain);
  };

  auto ProcessUNPCKPS = [&](unsigned NewOpc) -> bool {
    return ProcessUNPCKToIntDomain(NewOpc);
  };

  switch (Opc) {
  case X86::VPERMILPDri:
    return ProcessVPERMILPDri(X86::VSHUFPDrri);
  case X86::VPERMILPDYri:
    return ProcessVPERMILPDri(X86::VSHUFPDYrri);
  case X86::VPERMILPDZ128ri:
    return ProcessVPERMILPDri(X86::VSHUFPDZ128rri);
  case X86::VPERMILPDZ256ri:
    return ProcessVPERMILPDri(X86::VSHUFPDZ256rri);
  case X86::VPERMILPDZri:
    return ProcessVPERMILPDri(X86::VSHUFPDZrri);
  case X86::VPERMILPDZ128rikz:
    return ProcessVPERMILPDri(X86::VSHUFPDZ128rrikz);
  case X86::VPERMILPDZ256rikz:
    return ProcessVPERMILPDri(X86::VSHUFPDZ256rrikz);
  case X86::VPERMILPDZrikz:
    return ProcessVPERMILPDri(X86::VSHUFPDZrrikz);
  case X86::VPERMILPDZ128rik:
    return ProcessVPERMILPDri(X86::VSHUFPDZ128rrik);
  case X86::VPERMILPDZ256rik:
    return ProcessVPERMILPDri(X86::VSHUFPDZ256rrik);
  case X86::VPERMILPDZrik:
    return ProcessVPERMILPDri(X86::VSHUFPDZrrik);

  case X86::VPERMILPSri:
    return ProcessVPERMILPSri(X86::VSHUFPSrri);
  case X86::VPERMILPSYri:
    return ProcessVPERMILPSri(X86::VSHUFPSYrri);
  case X86::VPERMILPSZ128ri:
    return ProcessVPERMILPSri(X86::VSHUFPSZ128rri);
  case X86::VPERMILPSZ256ri:
    return ProcessVPERMILPSri(X86::VSHUFPSZ256rri);
  case X86::VPERMILPSZri:
    return ProcessVPERMILPSri(X86::VSHUFPSZrri);
  case X86::VPERMILPSZ128rikz:
    return ProcessVPERMILPSri(X86::VSHUFPSZ128rrikz);
  case X86::VPERMILPSZ256rikz:
    return ProcessVPERMILPSri(X86::VSHUFPSZ256rrikz);
  case X86::VPERMILPSZrikz:
    return ProcessVPERMILPSri(X86::VSHUFPSZrrikz);
  case X86::VPERMILPSZ128rik:
    return ProcessVPERMILPSri(X86::VSHUFPSZ128rrik);
  case X86::VPERMILPSZ256rik:
    return ProcessVPERMILPSri(X86::VSHUFPSZ256rrik);
  case X86::VPERMILPSZrik:
    return ProcessVPERMILPSri(X86::VSHUFPSZrrik);
  case X86::VPERMILPSmi:
    return ProcessVPERMILPSmi(X86::VPSHUFDmi);
  case X86::VPERMILPSYmi:
    // TODO: See if there is a more generic way we can test if the replacement
    // instruction is supported.
    return ST->hasAVX2() ? ProcessVPERMILPSmi(X86::VPSHUFDYmi) : false;
  case X86::VPERMILPSZ128mi:
    return ProcessVPERMILPSmi(X86::VPSHUFDZ128mi);
  case X86::VPERMILPSZ256mi:
    return ProcessVPERMILPSmi(X86::VPSHUFDZ256mi);
  case X86::VPERMILPSZmi:
    return ProcessVPERMILPSmi(X86::VPSHUFDZmi);
  case X86::VPERMILPSZ128mikz:
    return ProcessVPERMILPSmi(X86::VPSHUFDZ128mikz);
  case X86::VPERMILPSZ256mikz:
    return ProcessVPERMILPSmi(X86::VPSHUFDZ256mikz);
  case X86::VPERMILPSZmikz:
    return ProcessVPERMILPSmi(X86::VPSHUFDZmikz);
  case X86::VPERMILPSZ128mik:
    return ProcessVPERMILPSmi(X86::VPSHUFDZ128mik);
  case X86::VPERMILPSZ256mik:
    return ProcessVPERMILPSmi(X86::VPSHUFDZ256mik);
  case X86::VPERMILPSZmik:
    return ProcessVPERMILPSmi(X86::VPSHUFDZmik);

  case X86::MOVLHPSrr:
  case X86::UNPCKLPDrr:
    return ProcessUNPCKLPDrr(X86::PUNPCKLQDQrr, X86::SHUFPDrri);
  case X86::VMOVLHPSrr:
  case X86::VUNPCKLPDrr:
    return ProcessUNPCKLPDrr(X86::VPUNPCKLQDQrr, X86::VSHUFPDrri);
  case X86::VUNPCKLPDYrr:
    return ProcessUNPCKLPDrr(X86::VPUNPCKLQDQYrr, X86::VSHUFPDYrri);
    // VMOVLHPS is always 128 bits.
  case X86::VMOVLHPSZrr:
  case X86::VUNPCKLPDZ128rr:
    return ProcessUNPCKLPDrr(X86::VPUNPCKLQDQZ128rr, X86::VSHUFPDZ128rri);
  case X86::VUNPCKLPDZ256rr:
    return ProcessUNPCKLPDrr(X86::VPUNPCKLQDQZ256rr, X86::VSHUFPDZ256rri);
  case X86::VUNPCKLPDZrr:
    return ProcessUNPCKLPDrr(X86::VPUNPCKLQDQZrr, X86::VSHUFPDZrri);
  case X86::VUNPCKLPDZ128rrk:
    return ProcessUNPCKLPDrr(X86::VPUNPCKLQDQZ128rrk, X86::VSHUFPDZ128rrik);
  case X86::VUNPCKLPDZ256rrk:
    return ProcessUNPCKLPDrr(X86::VPUNPCKLQDQZ256rrk, X86::VSHUFPDZ256rrik);
  case X86::VUNPCKLPDZrrk:
    return ProcessUNPCKLPDrr(X86::VPUNPCKLQDQZrrk, X86::VSHUFPDZrrik);
  case X86::VUNPCKLPDZ128rrkz:
    return ProcessUNPCKLPDrr(X86::VPUNPCKLQDQZ128rrkz, X86::VSHUFPDZ128rrikz);
  case X86::VUNPCKLPDZ256rrkz:
    return ProcessUNPCKLPDrr(X86::VPUNPCKLQDQZ256rrkz, X86::VSHUFPDZ256rrikz);
  case X86::VUNPCKLPDZrrkz:
    return ProcessUNPCKLPDrr(X86::VPUNPCKLQDQZrrkz, X86::VSHUFPDZrrikz);
  case X86::UNPCKHPDrr:
    return ProcessUNPCKHPDrr(X86::PUNPCKHQDQrr, X86::SHUFPDrri);
  case X86::VUNPCKHPDrr:
    return ProcessUNPCKHPDrr(X86::VPUNPCKHQDQrr, X86::VSHUFPDrri);
  case X86::VUNPCKHPDYrr:
    return ProcessUNPCKHPDrr(X86::VPUNPCKHQDQYrr, X86::VSHUFPDYrri);
  case X86::VUNPCKHPDZ128rr:
    return ProcessUNPCKHPDrr(X86::VPUNPCKHQDQZ128rr, X86::VSHUFPDZ128rri);
  case X86::VUNPCKHPDZ256rr:
    return ProcessUNPCKHPDrr(X86::VPUNPCKHQDQZ256rr, X86::VSHUFPDZ256rri);
  case X86::VUNPCKHPDZrr:
    return ProcessUNPCKHPDrr(X86::VPUNPCKHQDQZrr, X86::VSHUFPDZrri);
  case X86::VUNPCKHPDZ128rrk:
    return ProcessUNPCKHPDrr(X86::VPUNPCKHQDQZ128rrk, X86::VSHUFPDZ128rrik);
  case X86::VUNPCKHPDZ256rrk:
    return ProcessUNPCKHPDrr(X86::VPUNPCKHQDQZ256rrk, X86::VSHUFPDZ256rrik);
  case X86::VUNPCKHPDZrrk:
    return ProcessUNPCKHPDrr(X86::VPUNPCKHQDQZrrk, X86::VSHUFPDZrrik);
  case X86::VUNPCKHPDZ128rrkz:
    return ProcessUNPCKHPDrr(X86::VPUNPCKHQDQZ128rrkz, X86::VSHUFPDZ128rrikz);
  case X86::VUNPCKHPDZ256rrkz:
    return ProcessUNPCKHPDrr(X86::VPUNPCKHQDQZ256rrkz, X86::VSHUFPDZ256rrikz);
  case X86::VUNPCKHPDZrrkz:
    return ProcessUNPCKHPDrr(X86::VPUNPCKHQDQZrrkz, X86::VSHUFPDZrrikz);
  case X86::UNPCKLPDrm:
    return ProcessUNPCKPDrm(X86::PUNPCKLQDQrm);
  case X86::VUNPCKLPDrm:
    return ProcessUNPCKPDrm(X86::VPUNPCKLQDQrm);
  case X86::VUNPCKLPDYrm:
    return ProcessUNPCKPDrm(X86::VPUNPCKLQDQYrm);
  case X86::VUNPCKLPDZ128rm:
    return ProcessUNPCKPDrm(X86::VPUNPCKLQDQZ128rm);
  case X86::VUNPCKLPDZ256rm:
    return ProcessUNPCKPDrm(X86::VPUNPCKLQDQZ256rm);
  case X86::VUNPCKLPDZrm:
    return ProcessUNPCKPDrm(X86::VPUNPCKLQDQZrm);
  case X86::VUNPCKLPDZ128rmk:
    return ProcessUNPCKPDrm(X86::VPUNPCKLQDQZ128rmk);
  case X86::VUNPCKLPDZ256rmk:
    return ProcessUNPCKPDrm(X86::VPUNPCKLQDQZ256rmk);
  case X86::VUNPCKLPDZrmk:
    return ProcessUNPCKPDrm(X86::VPUNPCKLQDQZrmk);
  case X86::VUNPCKLPDZ128rmkz:
    return ProcessUNPCKPDrm(X86::VPUNPCKLQDQZ128rmkz);
  case X86::VUNPCKLPDZ256rmkz:
    return ProcessUNPCKPDrm(X86::VPUNPCKLQDQZ256rmkz);
  case X86::VUNPCKLPDZrmkz:
    return ProcessUNPCKPDrm(X86::VPUNPCKLQDQZrmkz);
  case X86::UNPCKHPDrm:
    return ProcessUNPCKPDrm(X86::PUNPCKHQDQrm);
  case X86::VUNPCKHPDrm:
    return ProcessUNPCKPDrm(X86::VPUNPCKHQDQrm);
  case X86::VUNPCKHPDYrm:
    return ProcessUNPCKPDrm(X86::VPUNPCKHQDQYrm);
  case X86::VUNPCKHPDZ128rm:
    return ProcessUNPCKPDrm(X86::VPUNPCKHQDQZ128rm);
  case X86::VUNPCKHPDZ256rm:
    return ProcessUNPCKPDrm(X86::VPUNPCKHQDQZ256rm);
  case X86::VUNPCKHPDZrm:
    return ProcessUNPCKPDrm(X86::VPUNPCKHQDQZrm);
  case X86::VUNPCKHPDZ128rmk:
    return ProcessUNPCKPDrm(X86::VPUNPCKHQDQZ128rmk);
  case X86::VUNPCKHPDZ256rmk:
    return ProcessUNPCKPDrm(X86::VPUNPCKHQDQZ256rmk);
  case X86::VUNPCKHPDZrmk:
    return ProcessUNPCKPDrm(X86::VPUNPCKHQDQZrmk);
  case X86::VUNPCKHPDZ128rmkz:
    return ProcessUNPCKPDrm(X86::VPUNPCKHQDQZ128rmkz);
  case X86::VUNPCKHPDZ256rmkz:
    return ProcessUNPCKPDrm(X86::VPUNPCKHQDQZ256rmkz);
  case X86::VUNPCKHPDZrmkz:
    return ProcessUNPCKPDrm(X86::VPUNPCKHQDQZrmkz);

  case X86::UNPCKLPSrr:
    return ProcessUNPCKPS(X86::PUNPCKLDQrr);
  case X86::VUNPCKLPSrr:
    return ProcessUNPCKPS(X86::VPUNPCKLDQrr);
  case X86::VUNPCKLPSYrr:
    return ProcessUNPCKPS(X86::VPUNPCKLDQYrr);
  case X86::VUNPCKLPSZ128rr:
    return ProcessUNPCKPS(X86::VPUNPCKLDQZ128rr);
  case X86::VUNPCKLPSZ256rr:
    return ProcessUNPCKPS(X86::VPUNPCKLDQZ256rr);
  case X86::VUNPCKLPSZrr:
    return ProcessUNPCKPS(X86::VPUNPCKLDQZrr);
  case X86::VUNPCKLPSZ128rrk:
    return ProcessUNPCKPS(X86::VPUNPCKLDQZ128rrk);
  case X86::VUNPCKLPSZ256rrk:
    return ProcessUNPCKPS(X86::VPUNPCKLDQZ256rrk);
  case X86::VUNPCKLPSZrrk:
    return ProcessUNPCKPS(X86::VPUNPCKLDQZrrk);
  case X86::VUNPCKLPSZ128rrkz:
    return ProcessUNPCKPS(X86::VPUNPCKLDQZ128rrkz);
  case X86::VUNPCKLPSZ256rrkz:
    return ProcessUNPCKPS(X86::VPUNPCKLDQZ256rrkz);
  case X86::VUNPCKLPSZrrkz:
    return ProcessUNPCKPS(X86::VPUNPCKLDQZrrkz);
  case X86::UNPCKHPSrr:
    return ProcessUNPCKPS(X86::PUNPCKHDQrr);
  case X86::VUNPCKHPSrr:
    return ProcessUNPCKPS(X86::VPUNPCKHDQrr);
  case X86::VUNPCKHPSYrr:
    return ProcessUNPCKPS(X86::VPUNPCKHDQYrr);
  case X86::VUNPCKHPSZ128rr:
    return ProcessUNPCKPS(X86::VPUNPCKHDQZ128rr);
  case X86::VUNPCKHPSZ256rr:
    return ProcessUNPCKPS(X86::VPUNPCKHDQZ256rr);
  case X86::VUNPCKHPSZrr:
    return ProcessUNPCKPS(X86::VPUNPCKHDQZrr);
  case X86::VUNPCKHPSZ128rrk:
    return ProcessUNPCKPS(X86::VPUNPCKHDQZ128rrk);
  case X86::VUNPCKHPSZ256rrk:
    return ProcessUNPCKPS(X86::VPUNPCKHDQZ256rrk);
  case X86::VUNPCKHPSZrrk:
    return ProcessUNPCKPS(X86::VPUNPCKHDQZrrk);
  case X86::VUNPCKHPSZ128rrkz:
    return ProcessUNPCKPS(X86::VPUNPCKHDQZ128rrkz);
  case X86::VUNPCKHPSZ256rrkz:
    return ProcessUNPCKPS(X86::VPUNPCKHDQZ256rrkz);
  case X86::VUNPCKHPSZrrkz:
    return ProcessUNPCKPS(X86::VPUNPCKHDQZrrkz);
  case X86::UNPCKLPSrm:
    return ProcessUNPCKPS(X86::PUNPCKLDQrm);
  case X86::VUNPCKLPSrm:
    return ProcessUNPCKPS(X86::VPUNPCKLDQrm);
  case X86::VUNPCKLPSYrm:
    return ProcessUNPCKPS(X86::VPUNPCKLDQYrm);
  case X86::VUNPCKLPSZ128rm:
    return ProcessUNPCKPS(X86::VPUNPCKLDQZ128rm);
  case X86::VUNPCKLPSZ256rm:
    return ProcessUNPCKPS(X86::VPUNPCKLDQZ256rm);
  case X86::VUNPCKLPSZrm:
    return ProcessUNPCKPS(X86::VPUNPCKLDQZrm);
  case X86::VUNPCKLPSZ128rmk:
    return ProcessUNPCKPS(X86::VPUNPCKLDQZ128rmk);
  case X86::VUNPCKLPSZ256rmk:
    return ProcessUNPCKPS(X86::VPUNPCKLDQZ256rmk);
  case X86::VUNPCKLPSZrmk:
    return ProcessUNPCKPS(X86::VPUNPCKLDQZrmk);
  case X86::VUNPCKLPSZ128rmkz:
    return ProcessUNPCKPS(X86::VPUNPCKLDQZ128rmkz);
  case X86::VUNPCKLPSZ256rmkz:
    return ProcessUNPCKPS(X86::VPUNPCKLDQZ256rmkz);
  case X86::VUNPCKLPSZrmkz:
    return ProcessUNPCKPS(X86::VPUNPCKLDQZrmkz);
  case X86::UNPCKHPSrm:
    return ProcessUNPCKPS(X86::PUNPCKHDQrm);
  case X86::VUNPCKHPSrm:
    return ProcessUNPCKPS(X86::VPUNPCKHDQrm);
  case X86::VUNPCKHPSYrm:
    return ProcessUNPCKPS(X86::VPUNPCKHDQYrm);
  case X86::VUNPCKHPSZ128rm:
    return ProcessUNPCKPS(X86::VPUNPCKHDQZ128rm);
  case X86::VUNPCKHPSZ256rm:
    return ProcessUNPCKPS(X86::VPUNPCKHDQZ256rm);
  case X86::VUNPCKHPSZrm:
    return ProcessUNPCKPS(X86::VPUNPCKHDQZrm);
  case X86::VUNPCKHPSZ128rmk:
    return ProcessUNPCKPS(X86::VPUNPCKHDQZ128rmk);
  case X86::VUNPCKHPSZ256rmk:
    return ProcessUNPCKPS(X86::VPUNPCKHDQZ256rmk);
  case X86::VUNPCKHPSZrmk:
    return ProcessUNPCKPS(X86::VPUNPCKHDQZrmk);
  case X86::VUNPCKHPSZ128rmkz:
    return ProcessUNPCKPS(X86::VPUNPCKHDQZ128rmkz);
  case X86::VUNPCKHPSZ256rmkz:
    return ProcessUNPCKPS(X86::VPUNPCKHDQZ256rmkz);
  case X86::VUNPCKHPSZrmkz:
    return ProcessUNPCKPS(X86::VPUNPCKHDQZrmkz);
  default:
    return false;
  }
}

bool X86FixupInstTuningPass::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "Start X86FixupInstTuning\n";);
  bool Changed = false;
  ST = &MF.getSubtarget<X86Subtarget>();
  TII = ST->getInstrInfo();
  SM = &ST->getSchedModel();

  for (MachineBasicBlock &MBB : MF) {
    for (MachineBasicBlock::iterator I = MBB.begin(); I != MBB.end(); ++I) {
      if (processInstruction(MF, MBB, I)) {
        ++NumInstChanges;
        Changed = true;
      }
    }
  }
  LLVM_DEBUG(dbgs() << "End X86FixupInstTuning\n";);
  return Changed;
}
