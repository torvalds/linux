//===- HexagonBitTracker.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "HexagonBitTracker.h"
#include "Hexagon.h"
#include "HexagonInstrInfo.h"
#include "HexagonRegisterInfo.h"
#include "HexagonSubtarget.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

using namespace llvm;

using BT = BitTracker;

HexagonEvaluator::HexagonEvaluator(const HexagonRegisterInfo &tri,
                                   MachineRegisterInfo &mri,
                                   const HexagonInstrInfo &tii,
                                   MachineFunction &mf)
    : MachineEvaluator(tri, mri), MF(mf), MFI(mf.getFrameInfo()), TII(tii) {
  // Populate the VRX map (VR to extension-type).
  // Go over all the formal parameters of the function. If a given parameter
  // P is sign- or zero-extended, locate the virtual register holding that
  // parameter and create an entry in the VRX map indicating the type of ex-
  // tension (and the source type).
  // This is a bit complicated to do accurately, since the memory layout in-
  // formation is necessary to precisely determine whether an aggregate para-
  // meter will be passed in a register or in memory. What is given in MRI
  // is the association between the physical register that is live-in (i.e.
  // holds an argument), and the virtual register that this value will be
  // copied into. This, by itself, is not sufficient to map back the virtual
  // register to a formal parameter from Function (since consecutive live-ins
  // from MRI may not correspond to consecutive formal parameters from Func-
  // tion). To avoid the complications with in-memory arguments, only consi-
  // der the initial sequence of formal parameters that are known to be
  // passed via registers.
  unsigned InVirtReg, InPhysReg = 0;

  for (const Argument &Arg : MF.getFunction().args()) {
    Type *ATy = Arg.getType();
    unsigned Width = 0;
    if (ATy->isIntegerTy())
      Width = ATy->getIntegerBitWidth();
    else if (ATy->isPointerTy())
      Width = 32;
    // If pointer size is not set through target data, it will default to
    // Module::AnyPointerSize.
    if (Width == 0 || Width > 64)
      break;
    if (Arg.hasAttribute(Attribute::ByVal))
      continue;
    InPhysReg = getNextPhysReg(InPhysReg, Width);
    if (!InPhysReg)
      break;
    InVirtReg = getVirtRegFor(InPhysReg);
    if (!InVirtReg)
      continue;
    if (Arg.hasAttribute(Attribute::SExt))
      VRX.insert(std::make_pair(InVirtReg, ExtType(ExtType::SExt, Width)));
    else if (Arg.hasAttribute(Attribute::ZExt))
      VRX.insert(std::make_pair(InVirtReg, ExtType(ExtType::ZExt, Width)));
  }
}

BT::BitMask HexagonEvaluator::mask(Register Reg, unsigned Sub) const {
  if (Sub == 0)
    return MachineEvaluator::mask(Reg, 0);
  const TargetRegisterClass &RC = *MRI.getRegClass(Reg);
  unsigned ID = RC.getID();
  uint16_t RW = getRegBitWidth(RegisterRef(Reg, Sub));
  const auto &HRI = static_cast<const HexagonRegisterInfo&>(TRI);
  bool IsSubLo = (Sub == HRI.getHexagonSubRegIndex(RC, Hexagon::ps_sub_lo));
  switch (ID) {
    case Hexagon::DoubleRegsRegClassID:
    case Hexagon::HvxWRRegClassID:
    case Hexagon::HvxVQRRegClassID:
      return IsSubLo ? BT::BitMask(0, RW-1)
                     : BT::BitMask(RW, 2*RW-1);
    default:
      break;
  }
#ifndef NDEBUG
  dbgs() << printReg(Reg, &TRI, Sub) << " in reg class "
         << TRI.getRegClassName(&RC) << '\n';
#endif
  llvm_unreachable("Unexpected register/subregister");
}

uint16_t HexagonEvaluator::getPhysRegBitWidth(MCRegister Reg) const {
  using namespace Hexagon;
  const auto &HST = MF.getSubtarget<HexagonSubtarget>();
  if (HST.useHVXOps()) {
    for (auto &RC : {HvxVRRegClass, HvxWRRegClass, HvxQRRegClass,
                     HvxVQRRegClass})
      if (RC.contains(Reg))
        return TRI.getRegSizeInBits(RC);
  }
  // Default treatment for other physical registers.
  if (const TargetRegisterClass *RC = TRI.getMinimalPhysRegClass(Reg))
    return TRI.getRegSizeInBits(*RC);

  llvm_unreachable(
      (Twine("Unhandled physical register") + TRI.getName(Reg)).str().c_str());
}

const TargetRegisterClass &HexagonEvaluator::composeWithSubRegIndex(
      const TargetRegisterClass &RC, unsigned Idx) const {
  if (Idx == 0)
    return RC;

#ifndef NDEBUG
  const auto &HRI = static_cast<const HexagonRegisterInfo&>(TRI);
  bool IsSubLo = (Idx == HRI.getHexagonSubRegIndex(RC, Hexagon::ps_sub_lo));
  bool IsSubHi = (Idx == HRI.getHexagonSubRegIndex(RC, Hexagon::ps_sub_hi));
  assert(IsSubLo != IsSubHi && "Must refer to either low or high subreg");
#endif

  switch (RC.getID()) {
    case Hexagon::DoubleRegsRegClassID:
      return Hexagon::IntRegsRegClass;
    case Hexagon::HvxWRRegClassID:
      return Hexagon::HvxVRRegClass;
    case Hexagon::HvxVQRRegClassID:
      return Hexagon::HvxWRRegClass;
    default:
      break;
  }
#ifndef NDEBUG
  dbgs() << "Reg class id: " << RC.getID() << " idx: " << Idx << '\n';
#endif
  llvm_unreachable("Unimplemented combination of reg class/subreg idx");
}

namespace {

class RegisterRefs {
  std::vector<BT::RegisterRef> Vector;

public:
  RegisterRefs(const MachineInstr &MI) : Vector(MI.getNumOperands()) {
    for (unsigned i = 0, n = Vector.size(); i < n; ++i) {
      const MachineOperand &MO = MI.getOperand(i);
      if (MO.isReg())
        Vector[i] = BT::RegisterRef(MO);
      // For indices that don't correspond to registers, the entry will
      // remain constructed via the default constructor.
    }
  }

  size_t size() const { return Vector.size(); }

  const BT::RegisterRef &operator[](unsigned n) const {
    // The main purpose of this operator is to assert with bad argument.
    assert(n < Vector.size());
    return Vector[n];
  }
};

} // end anonymous namespace

bool HexagonEvaluator::evaluate(const MachineInstr &MI,
                                const CellMapType &Inputs,
                                CellMapType &Outputs) const {
  using namespace Hexagon;

  unsigned NumDefs = 0;

  // Basic correctness check: there should not be any defs with subregisters.
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isDef())
      continue;
    NumDefs++;
    assert(MO.getSubReg() == 0);
  }

  if (NumDefs == 0)
    return false;

  unsigned Opc = MI.getOpcode();

  if (MI.mayLoad()) {
    switch (Opc) {
      // These instructions may be marked as mayLoad, but they are generating
      // immediate values, so skip them.
      case CONST32:
      case CONST64:
        break;
      default:
        return evaluateLoad(MI, Inputs, Outputs);
    }
  }

  // Check COPY instructions that copy formal parameters into virtual
  // registers. Such parameters can be sign- or zero-extended at the
  // call site, and we should take advantage of this knowledge. The MRI
  // keeps a list of pairs of live-in physical and virtual registers,
  // which provides information about which virtual registers will hold
  // the argument values. The function will still contain instructions
  // defining those virtual registers, and in practice those are COPY
  // instructions from a physical to a virtual register. In such cases,
  // applying the argument extension to the virtual register can be seen
  // as simply mirroring the extension that had already been applied to
  // the physical register at the call site. If the defining instruction
  // was not a COPY, it would not be clear how to mirror that extension
  // on the callee's side. For that reason, only check COPY instructions
  // for potential extensions.
  if (MI.isCopy()) {
    if (evaluateFormalCopy(MI, Inputs, Outputs))
      return true;
  }

  // Beyond this point, if any operand is a global, skip that instruction.
  // The reason is that certain instructions that can take an immediate
  // operand can also have a global symbol in that operand. To avoid
  // checking what kind of operand a given instruction has individually
  // for each instruction, do it here. Global symbols as operands gene-
  // rally do not provide any useful information.
  for (const MachineOperand &MO : MI.operands()) {
    if (MO.isGlobal() || MO.isBlockAddress() || MO.isSymbol() || MO.isJTI() ||
        MO.isCPI())
      return false;
  }

  RegisterRefs Reg(MI);
#define op(i) MI.getOperand(i)
#define rc(i) RegisterCell::ref(getCell(Reg[i], Inputs))
#define im(i) MI.getOperand(i).getImm()

  // If the instruction has no register operands, skip it.
  if (Reg.size() == 0)
    return false;

  // Record result for register in operand 0.
  auto rr0 = [this,Reg] (const BT::RegisterCell &Val, CellMapType &Outputs)
        -> bool {
    putCell(Reg[0], Val, Outputs);
    return true;
  };
  // Get the cell corresponding to the N-th operand.
  auto cop = [this, &Reg, &MI, &Inputs](unsigned N,
                                        uint16_t W) -> BT::RegisterCell {
    const MachineOperand &Op = MI.getOperand(N);
    if (Op.isImm())
      return eIMM(Op.getImm(), W);
    if (!Op.isReg())
      return RegisterCell::self(0, W);
    assert(getRegBitWidth(Reg[N]) == W && "Register width mismatch");
    return rc(N);
  };
  // Extract RW low bits of the cell.
  auto lo = [this] (const BT::RegisterCell &RC, uint16_t RW)
        -> BT::RegisterCell {
    assert(RW <= RC.width());
    return eXTR(RC, 0, RW);
  };
  // Extract RW high bits of the cell.
  auto hi = [this] (const BT::RegisterCell &RC, uint16_t RW)
        -> BT::RegisterCell {
    uint16_t W = RC.width();
    assert(RW <= W);
    return eXTR(RC, W-RW, W);
  };
  // Extract N-th halfword (counting from the least significant position).
  auto half = [this] (const BT::RegisterCell &RC, unsigned N)
        -> BT::RegisterCell {
    assert(N*16+16 <= RC.width());
    return eXTR(RC, N*16, N*16+16);
  };
  // Shuffle bits (pick even/odd from cells and merge into result).
  auto shuffle = [this] (const BT::RegisterCell &Rs, const BT::RegisterCell &Rt,
                         uint16_t BW, bool Odd) -> BT::RegisterCell {
    uint16_t I = Odd, Ws = Rs.width();
    assert(Ws == Rt.width());
    RegisterCell RC = eXTR(Rt, I*BW, I*BW+BW).cat(eXTR(Rs, I*BW, I*BW+BW));
    I += 2;
    while (I*BW < Ws) {
      RC.cat(eXTR(Rt, I*BW, I*BW+BW)).cat(eXTR(Rs, I*BW, I*BW+BW));
      I += 2;
    }
    return RC;
  };

  // The bitwidth of the 0th operand. In most (if not all) of the
  // instructions below, the 0th operand is the defined register.
  // Pre-compute the bitwidth here, because it is needed in many cases
  // cases below.
  uint16_t W0 = (Reg[0].Reg != 0) ? getRegBitWidth(Reg[0]) : 0;

  // Register id of the 0th operand. It can be 0.
  unsigned Reg0 = Reg[0].Reg;

  switch (Opc) {
    // Transfer immediate:

    case A2_tfrsi:
    case A2_tfrpi:
    case CONST32:
    case CONST64:
      return rr0(eIMM(im(1), W0), Outputs);
    case PS_false:
      return rr0(RegisterCell(W0).fill(0, W0, BT::BitValue::Zero), Outputs);
    case PS_true:
      return rr0(RegisterCell(W0).fill(0, W0, BT::BitValue::One), Outputs);
    case PS_fi: {
      int FI = op(1).getIndex();
      int Off = op(2).getImm();
      unsigned A = MFI.getObjectAlign(FI).value() + std::abs(Off);
      unsigned L = llvm::countr_zero(A);
      RegisterCell RC = RegisterCell::self(Reg[0].Reg, W0);
      RC.fill(0, L, BT::BitValue::Zero);
      return rr0(RC, Outputs);
    }

    // Transfer register:

    case A2_tfr:
    case A2_tfrp:
    case C2_pxfer_map:
      return rr0(rc(1), Outputs);
    case C2_tfrpr: {
      uint16_t RW = W0;
      uint16_t PW = 8; // XXX Pred size: getRegBitWidth(Reg[1]);
      assert(PW <= RW);
      RegisterCell PC = eXTR(rc(1), 0, PW);
      RegisterCell RC = RegisterCell(RW).insert(PC, BT::BitMask(0, PW-1));
      RC.fill(PW, RW, BT::BitValue::Zero);
      return rr0(RC, Outputs);
    }
    case C2_tfrrp: {
      uint16_t RW = W0;
      uint16_t PW = 8; // XXX Pred size: getRegBitWidth(Reg[1]);
      RegisterCell RC = RegisterCell::self(Reg[0].Reg, RW);
      RC.fill(PW, RW, BT::BitValue::Zero);
      return rr0(eINS(RC, eXTR(rc(1), 0, PW), 0), Outputs);
    }

    // Arithmetic:

    case A2_abs:
    case A2_absp:
      // TODO
      break;

    case A2_addsp: {
      uint16_t W1 = getRegBitWidth(Reg[1]);
      assert(W0 == 64 && W1 == 32);
      RegisterCell CW = RegisterCell(W0).insert(rc(1), BT::BitMask(0, W1-1));
      RegisterCell RC = eADD(eSXT(CW, W1), rc(2));
      return rr0(RC, Outputs);
    }
    case A2_add:
    case A2_addp:
      return rr0(eADD(rc(1), rc(2)), Outputs);
    case A2_addi:
      return rr0(eADD(rc(1), eIMM(im(2), W0)), Outputs);
    case S4_addi_asl_ri: {
      RegisterCell RC = eADD(eIMM(im(1), W0), eASL(rc(2), im(3)));
      return rr0(RC, Outputs);
    }
    case S4_addi_lsr_ri: {
      RegisterCell RC = eADD(eIMM(im(1), W0), eLSR(rc(2), im(3)));
      return rr0(RC, Outputs);
    }
    case S4_addaddi: {
      RegisterCell RC = eADD(rc(1), eADD(rc(2), eIMM(im(3), W0)));
      return rr0(RC, Outputs);
    }
    case M4_mpyri_addi: {
      RegisterCell M = eMLS(rc(2), eIMM(im(3), W0));
      RegisterCell RC = eADD(eIMM(im(1), W0), lo(M, W0));
      return rr0(RC, Outputs);
    }
    case M4_mpyrr_addi: {
      RegisterCell M = eMLS(rc(2), rc(3));
      RegisterCell RC = eADD(eIMM(im(1), W0), lo(M, W0));
      return rr0(RC, Outputs);
    }
    case M4_mpyri_addr_u2: {
      RegisterCell M = eMLS(eIMM(im(2), W0), rc(3));
      RegisterCell RC = eADD(rc(1), lo(M, W0));
      return rr0(RC, Outputs);
    }
    case M4_mpyri_addr: {
      RegisterCell M = eMLS(rc(2), eIMM(im(3), W0));
      RegisterCell RC = eADD(rc(1), lo(M, W0));
      return rr0(RC, Outputs);
    }
    case M4_mpyrr_addr: {
      RegisterCell M = eMLS(rc(2), rc(3));
      RegisterCell RC = eADD(rc(1), lo(M, W0));
      return rr0(RC, Outputs);
    }
    case S4_subaddi: {
      RegisterCell RC = eADD(rc(1), eSUB(eIMM(im(2), W0), rc(3)));
      return rr0(RC, Outputs);
    }
    case M2_accii: {
      RegisterCell RC = eADD(rc(1), eADD(rc(2), eIMM(im(3), W0)));
      return rr0(RC, Outputs);
    }
    case M2_acci: {
      RegisterCell RC = eADD(rc(1), eADD(rc(2), rc(3)));
      return rr0(RC, Outputs);
    }
    case M2_subacc: {
      RegisterCell RC = eADD(rc(1), eSUB(rc(2), rc(3)));
      return rr0(RC, Outputs);
    }
    case S2_addasl_rrri: {
      RegisterCell RC = eADD(rc(1), eASL(rc(2), im(3)));
      return rr0(RC, Outputs);
    }
    case C4_addipc: {
      RegisterCell RPC = RegisterCell::self(Reg[0].Reg, W0);
      RPC.fill(0, 2, BT::BitValue::Zero);
      return rr0(eADD(RPC, eIMM(im(2), W0)), Outputs);
    }
    case A2_sub:
    case A2_subp:
      return rr0(eSUB(rc(1), rc(2)), Outputs);
    case A2_subri:
      return rr0(eSUB(eIMM(im(1), W0), rc(2)), Outputs);
    case S4_subi_asl_ri: {
      RegisterCell RC = eSUB(eIMM(im(1), W0), eASL(rc(2), im(3)));
      return rr0(RC, Outputs);
    }
    case S4_subi_lsr_ri: {
      RegisterCell RC = eSUB(eIMM(im(1), W0), eLSR(rc(2), im(3)));
      return rr0(RC, Outputs);
    }
    case M2_naccii: {
      RegisterCell RC = eSUB(rc(1), eADD(rc(2), eIMM(im(3), W0)));
      return rr0(RC, Outputs);
    }
    case M2_nacci: {
      RegisterCell RC = eSUB(rc(1), eADD(rc(2), rc(3)));
      return rr0(RC, Outputs);
    }
    // 32-bit negation is done by "Rd = A2_subri 0, Rs"
    case A2_negp:
      return rr0(eSUB(eIMM(0, W0), rc(1)), Outputs);

    case M2_mpy_up: {
      RegisterCell M = eMLS(rc(1), rc(2));
      return rr0(hi(M, W0), Outputs);
    }
    case M2_dpmpyss_s0:
      return rr0(eMLS(rc(1), rc(2)), Outputs);
    case M2_dpmpyss_acc_s0:
      return rr0(eADD(rc(1), eMLS(rc(2), rc(3))), Outputs);
    case M2_dpmpyss_nac_s0:
      return rr0(eSUB(rc(1), eMLS(rc(2), rc(3))), Outputs);
    case M2_mpyi: {
      RegisterCell M = eMLS(rc(1), rc(2));
      return rr0(lo(M, W0), Outputs);
    }
    case M2_macsip: {
      RegisterCell M = eMLS(rc(2), eIMM(im(3), W0));
      RegisterCell RC = eADD(rc(1), lo(M, W0));
      return rr0(RC, Outputs);
    }
    case M2_macsin: {
      RegisterCell M = eMLS(rc(2), eIMM(im(3), W0));
      RegisterCell RC = eSUB(rc(1), lo(M, W0));
      return rr0(RC, Outputs);
    }
    case M2_maci: {
      RegisterCell M = eMLS(rc(2), rc(3));
      RegisterCell RC = eADD(rc(1), lo(M, W0));
      return rr0(RC, Outputs);
    }
    case M2_mnaci: {
      RegisterCell M = eMLS(rc(2), rc(3));
      RegisterCell RC = eSUB(rc(1), lo(M, W0));
      return rr0(RC, Outputs);
    }
    case M2_mpysmi: {
      RegisterCell M = eMLS(rc(1), eIMM(im(2), W0));
      return rr0(lo(M, 32), Outputs);
    }
    case M2_mpysin: {
      RegisterCell M = eMLS(rc(1), eIMM(-im(2), W0));
      return rr0(lo(M, 32), Outputs);
    }
    case M2_mpysip: {
      RegisterCell M = eMLS(rc(1), eIMM(im(2), W0));
      return rr0(lo(M, 32), Outputs);
    }
    case M2_mpyu_up: {
      RegisterCell M = eMLU(rc(1), rc(2));
      return rr0(hi(M, W0), Outputs);
    }
    case M2_dpmpyuu_s0:
      return rr0(eMLU(rc(1), rc(2)), Outputs);
    case M2_dpmpyuu_acc_s0:
      return rr0(eADD(rc(1), eMLU(rc(2), rc(3))), Outputs);
    case M2_dpmpyuu_nac_s0:
      return rr0(eSUB(rc(1), eMLU(rc(2), rc(3))), Outputs);
    //case M2_mpysu_up:

    // Logical/bitwise:

    case A2_andir:
      return rr0(eAND(rc(1), eIMM(im(2), W0)), Outputs);
    case A2_and:
    case A2_andp:
      return rr0(eAND(rc(1), rc(2)), Outputs);
    case A4_andn:
    case A4_andnp:
      return rr0(eAND(rc(1), eNOT(rc(2))), Outputs);
    case S4_andi_asl_ri: {
      RegisterCell RC = eAND(eIMM(im(1), W0), eASL(rc(2), im(3)));
      return rr0(RC, Outputs);
    }
    case S4_andi_lsr_ri: {
      RegisterCell RC = eAND(eIMM(im(1), W0), eLSR(rc(2), im(3)));
      return rr0(RC, Outputs);
    }
    case M4_and_and:
      return rr0(eAND(rc(1), eAND(rc(2), rc(3))), Outputs);
    case M4_and_andn:
      return rr0(eAND(rc(1), eAND(rc(2), eNOT(rc(3)))), Outputs);
    case M4_and_or:
      return rr0(eAND(rc(1), eORL(rc(2), rc(3))), Outputs);
    case M4_and_xor:
      return rr0(eAND(rc(1), eXOR(rc(2), rc(3))), Outputs);
    case A2_orir:
      return rr0(eORL(rc(1), eIMM(im(2), W0)), Outputs);
    case A2_or:
    case A2_orp:
      return rr0(eORL(rc(1), rc(2)), Outputs);
    case A4_orn:
    case A4_ornp:
      return rr0(eORL(rc(1), eNOT(rc(2))), Outputs);
    case S4_ori_asl_ri: {
      RegisterCell RC = eORL(eIMM(im(1), W0), eASL(rc(2), im(3)));
      return rr0(RC, Outputs);
    }
    case S4_ori_lsr_ri: {
      RegisterCell RC = eORL(eIMM(im(1), W0), eLSR(rc(2), im(3)));
      return rr0(RC, Outputs);
    }
    case M4_or_and:
      return rr0(eORL(rc(1), eAND(rc(2), rc(3))), Outputs);
    case M4_or_andn:
      return rr0(eORL(rc(1), eAND(rc(2), eNOT(rc(3)))), Outputs);
    case S4_or_andi:
    case S4_or_andix: {
      RegisterCell RC = eORL(rc(1), eAND(rc(2), eIMM(im(3), W0)));
      return rr0(RC, Outputs);
    }
    case S4_or_ori: {
      RegisterCell RC = eORL(rc(1), eORL(rc(2), eIMM(im(3), W0)));
      return rr0(RC, Outputs);
    }
    case M4_or_or:
      return rr0(eORL(rc(1), eORL(rc(2), rc(3))), Outputs);
    case M4_or_xor:
      return rr0(eORL(rc(1), eXOR(rc(2), rc(3))), Outputs);
    case A2_xor:
    case A2_xorp:
      return rr0(eXOR(rc(1), rc(2)), Outputs);
    case M4_xor_and:
      return rr0(eXOR(rc(1), eAND(rc(2), rc(3))), Outputs);
    case M4_xor_andn:
      return rr0(eXOR(rc(1), eAND(rc(2), eNOT(rc(3)))), Outputs);
    case M4_xor_or:
      return rr0(eXOR(rc(1), eORL(rc(2), rc(3))), Outputs);
    case M4_xor_xacc:
      return rr0(eXOR(rc(1), eXOR(rc(2), rc(3))), Outputs);
    case A2_not:
    case A2_notp:
      return rr0(eNOT(rc(1)), Outputs);

    case S2_asl_i_r:
    case S2_asl_i_p:
      return rr0(eASL(rc(1), im(2)), Outputs);
    case A2_aslh:
      return rr0(eASL(rc(1), 16), Outputs);
    case S2_asl_i_r_acc:
    case S2_asl_i_p_acc:
      return rr0(eADD(rc(1), eASL(rc(2), im(3))), Outputs);
    case S2_asl_i_r_nac:
    case S2_asl_i_p_nac:
      return rr0(eSUB(rc(1), eASL(rc(2), im(3))), Outputs);
    case S2_asl_i_r_and:
    case S2_asl_i_p_and:
      return rr0(eAND(rc(1), eASL(rc(2), im(3))), Outputs);
    case S2_asl_i_r_or:
    case S2_asl_i_p_or:
      return rr0(eORL(rc(1), eASL(rc(2), im(3))), Outputs);
    case S2_asl_i_r_xacc:
    case S2_asl_i_p_xacc:
      return rr0(eXOR(rc(1), eASL(rc(2), im(3))), Outputs);
    case S2_asl_i_vh:
    case S2_asl_i_vw:
      // TODO
      break;

    case S2_asr_i_r:
    case S2_asr_i_p:
      return rr0(eASR(rc(1), im(2)), Outputs);
    case A2_asrh:
      return rr0(eASR(rc(1), 16), Outputs);
    case S2_asr_i_r_acc:
    case S2_asr_i_p_acc:
      return rr0(eADD(rc(1), eASR(rc(2), im(3))), Outputs);
    case S2_asr_i_r_nac:
    case S2_asr_i_p_nac:
      return rr0(eSUB(rc(1), eASR(rc(2), im(3))), Outputs);
    case S2_asr_i_r_and:
    case S2_asr_i_p_and:
      return rr0(eAND(rc(1), eASR(rc(2), im(3))), Outputs);
    case S2_asr_i_r_or:
    case S2_asr_i_p_or:
      return rr0(eORL(rc(1), eASR(rc(2), im(3))), Outputs);
    case S2_asr_i_r_rnd: {
      // The input is first sign-extended to 64 bits, then the output
      // is truncated back to 32 bits.
      assert(W0 == 32);
      RegisterCell XC = eSXT(rc(1).cat(eIMM(0, W0)), W0);
      RegisterCell RC = eASR(eADD(eASR(XC, im(2)), eIMM(1, 2*W0)), 1);
      return rr0(eXTR(RC, 0, W0), Outputs);
    }
    case S2_asr_i_r_rnd_goodsyntax: {
      int64_t S = im(2);
      if (S == 0)
        return rr0(rc(1), Outputs);
      // Result: S2_asr_i_r_rnd Rs, u5-1
      RegisterCell XC = eSXT(rc(1).cat(eIMM(0, W0)), W0);
      RegisterCell RC = eLSR(eADD(eASR(XC, S-1), eIMM(1, 2*W0)), 1);
      return rr0(eXTR(RC, 0, W0), Outputs);
    }
    case S2_asr_r_vh:
    case S2_asr_i_vw:
    case S2_asr_i_svw_trun:
      // TODO
      break;

    case S2_lsr_i_r:
    case S2_lsr_i_p:
      return rr0(eLSR(rc(1), im(2)), Outputs);
    case S2_lsr_i_r_acc:
    case S2_lsr_i_p_acc:
      return rr0(eADD(rc(1), eLSR(rc(2), im(3))), Outputs);
    case S2_lsr_i_r_nac:
    case S2_lsr_i_p_nac:
      return rr0(eSUB(rc(1), eLSR(rc(2), im(3))), Outputs);
    case S2_lsr_i_r_and:
    case S2_lsr_i_p_and:
      return rr0(eAND(rc(1), eLSR(rc(2), im(3))), Outputs);
    case S2_lsr_i_r_or:
    case S2_lsr_i_p_or:
      return rr0(eORL(rc(1), eLSR(rc(2), im(3))), Outputs);
    case S2_lsr_i_r_xacc:
    case S2_lsr_i_p_xacc:
      return rr0(eXOR(rc(1), eLSR(rc(2), im(3))), Outputs);

    case S2_clrbit_i: {
      RegisterCell RC = rc(1);
      RC[im(2)] = BT::BitValue::Zero;
      return rr0(RC, Outputs);
    }
    case S2_setbit_i: {
      RegisterCell RC = rc(1);
      RC[im(2)] = BT::BitValue::One;
      return rr0(RC, Outputs);
    }
    case S2_togglebit_i: {
      RegisterCell RC = rc(1);
      uint16_t BX = im(2);
      RC[BX] = RC[BX].is(0) ? BT::BitValue::One
                            : RC[BX].is(1) ? BT::BitValue::Zero
                                           : BT::BitValue::self();
      return rr0(RC, Outputs);
    }

    case A4_bitspliti: {
      uint16_t W1 = getRegBitWidth(Reg[1]);
      uint16_t BX = im(2);
      // Res.uw[1] = Rs[bx+1:], Res.uw[0] = Rs[0:bx]
      const BT::BitValue Zero = BT::BitValue::Zero;
      RegisterCell RZ = RegisterCell(W0).fill(BX, W1, Zero)
                                        .fill(W1+(W1-BX), W0, Zero);
      RegisterCell BF1 = eXTR(rc(1), 0, BX), BF2 = eXTR(rc(1), BX, W1);
      RegisterCell RC = eINS(eINS(RZ, BF1, 0), BF2, W1);
      return rr0(RC, Outputs);
    }
    case S4_extract:
    case S4_extractp:
    case S2_extractu:
    case S2_extractup: {
      uint16_t Wd = im(2), Of = im(3);
      assert(Wd <= W0);
      if (Wd == 0)
        return rr0(eIMM(0, W0), Outputs);
      // If the width extends beyond the register size, pad the register
      // with 0 bits.
      RegisterCell Pad = (Wd+Of > W0) ? rc(1).cat(eIMM(0, Wd+Of-W0)) : rc(1);
      RegisterCell Ext = eXTR(Pad, Of, Wd+Of);
      // Ext is short, need to extend it with 0s or sign bit.
      RegisterCell RC = RegisterCell(W0).insert(Ext, BT::BitMask(0, Wd-1));
      if (Opc == S2_extractu || Opc == S2_extractup)
        return rr0(eZXT(RC, Wd), Outputs);
      return rr0(eSXT(RC, Wd), Outputs);
    }
    case S2_insert:
    case S2_insertp: {
      uint16_t Wd = im(3), Of = im(4);
      assert(Wd < W0 && Of < W0);
      // If Wd+Of exceeds W0, the inserted bits are truncated.
      if (Wd+Of > W0)
        Wd = W0-Of;
      if (Wd == 0)
        return rr0(rc(1), Outputs);
      return rr0(eINS(rc(1), eXTR(rc(2), 0, Wd), Of), Outputs);
    }

    // Bit permutations:

    case A2_combineii:
    case A4_combineii:
    case A4_combineir:
    case A4_combineri:
    case A2_combinew:
    case V6_vcombine:
      assert(W0 % 2 == 0);
      return rr0(cop(2, W0/2).cat(cop(1, W0/2)), Outputs);
    case A2_combine_ll:
    case A2_combine_lh:
    case A2_combine_hl:
    case A2_combine_hh: {
      assert(W0 == 32);
      assert(getRegBitWidth(Reg[1]) == 32 && getRegBitWidth(Reg[2]) == 32);
      // Low half in the output is 0 for _ll and _hl, 1 otherwise:
      unsigned LoH = !(Opc == A2_combine_ll || Opc == A2_combine_hl);
      // High half in the output is 0 for _ll and _lh, 1 otherwise:
      unsigned HiH = !(Opc == A2_combine_ll || Opc == A2_combine_lh);
      RegisterCell R1 = rc(1);
      RegisterCell R2 = rc(2);
      RegisterCell RC = half(R2, LoH).cat(half(R1, HiH));
      return rr0(RC, Outputs);
    }
    case S2_packhl: {
      assert(W0 == 64);
      assert(getRegBitWidth(Reg[1]) == 32 && getRegBitWidth(Reg[2]) == 32);
      RegisterCell R1 = rc(1);
      RegisterCell R2 = rc(2);
      RegisterCell RC = half(R2, 0).cat(half(R1, 0)).cat(half(R2, 1))
                                   .cat(half(R1, 1));
      return rr0(RC, Outputs);
    }
    case S2_shuffeb: {
      RegisterCell RC = shuffle(rc(1), rc(2), 8, false);
      return rr0(RC, Outputs);
    }
    case S2_shuffeh: {
      RegisterCell RC = shuffle(rc(1), rc(2), 16, false);
      return rr0(RC, Outputs);
    }
    case S2_shuffob: {
      RegisterCell RC = shuffle(rc(1), rc(2), 8, true);
      return rr0(RC, Outputs);
    }
    case S2_shuffoh: {
      RegisterCell RC = shuffle(rc(1), rc(2), 16, true);
      return rr0(RC, Outputs);
    }
    case C2_mask: {
      uint16_t WR = W0;
      uint16_t WP = 8; // XXX Pred size: getRegBitWidth(Reg[1]);
      assert(WR == 64 && WP == 8);
      RegisterCell R1 = rc(1);
      RegisterCell RC(WR);
      for (uint16_t i = 0; i < WP; ++i) {
        const BT::BitValue &V = R1[i];
        BT::BitValue F = (V.is(0) || V.is(1)) ? V : BT::BitValue::self();
        RC.fill(i*8, i*8+8, F);
      }
      return rr0(RC, Outputs);
    }

    // Mux:

    case C2_muxii:
    case C2_muxir:
    case C2_muxri:
    case C2_mux: {
      BT::BitValue PC0 = rc(1)[0];
      RegisterCell R2 = cop(2, W0);
      RegisterCell R3 = cop(3, W0);
      if (PC0.is(0) || PC0.is(1))
        return rr0(RegisterCell::ref(PC0 ? R2 : R3), Outputs);
      R2.meet(R3, Reg[0].Reg);
      return rr0(R2, Outputs);
    }
    case C2_vmux:
      // TODO
      break;

    // Sign- and zero-extension:

    case A2_sxtb:
      return rr0(eSXT(rc(1), 8), Outputs);
    case A2_sxth:
      return rr0(eSXT(rc(1), 16), Outputs);
    case A2_sxtw: {
      uint16_t W1 = getRegBitWidth(Reg[1]);
      assert(W0 == 64 && W1 == 32);
      RegisterCell RC = eSXT(rc(1).cat(eIMM(0, W1)), W1);
      return rr0(RC, Outputs);
    }
    case A2_zxtb:
      return rr0(eZXT(rc(1), 8), Outputs);
    case A2_zxth:
      return rr0(eZXT(rc(1), 16), Outputs);

    // Saturations

    case A2_satb:
      return rr0(eSXT(RegisterCell::self(0, W0).regify(Reg0), 8), Outputs);
    case A2_sath:
      return rr0(eSXT(RegisterCell::self(0, W0).regify(Reg0), 16), Outputs);
    case A2_satub:
      return rr0(eZXT(RegisterCell::self(0, W0).regify(Reg0), 8), Outputs);
    case A2_satuh:
      return rr0(eZXT(RegisterCell::self(0, W0).regify(Reg0), 16), Outputs);

    // Bit count:

    case S2_cl0:
    case S2_cl0p:
      // Always produce a 32-bit result.
      return rr0(eCLB(rc(1), false/*bit*/, 32), Outputs);
    case S2_cl1:
    case S2_cl1p:
      return rr0(eCLB(rc(1), true/*bit*/, 32), Outputs);
    case S2_clb:
    case S2_clbp: {
      uint16_t W1 = getRegBitWidth(Reg[1]);
      RegisterCell R1 = rc(1);
      BT::BitValue TV = R1[W1-1];
      if (TV.is(0) || TV.is(1))
        return rr0(eCLB(R1, TV, 32), Outputs);
      break;
    }
    case S2_ct0:
    case S2_ct0p:
      return rr0(eCTB(rc(1), false/*bit*/, 32), Outputs);
    case S2_ct1:
    case S2_ct1p:
      return rr0(eCTB(rc(1), true/*bit*/, 32), Outputs);
    case S5_popcountp:
      // TODO
      break;

    case C2_all8: {
      RegisterCell P1 = rc(1);
      bool Has0 = false, All1 = true;
      for (uint16_t i = 0; i < 8/*XXX*/; ++i) {
        if (!P1[i].is(1))
          All1 = false;
        if (!P1[i].is(0))
          continue;
        Has0 = true;
        break;
      }
      if (!Has0 && !All1)
        break;
      RegisterCell RC(W0);
      RC.fill(0, W0, (All1 ? BT::BitValue::One : BT::BitValue::Zero));
      return rr0(RC, Outputs);
    }
    case C2_any8: {
      RegisterCell P1 = rc(1);
      bool Has1 = false, All0 = true;
      for (uint16_t i = 0; i < 8/*XXX*/; ++i) {
        if (!P1[i].is(0))
          All0 = false;
        if (!P1[i].is(1))
          continue;
        Has1 = true;
        break;
      }
      if (!Has1 && !All0)
        break;
      RegisterCell RC(W0);
      RC.fill(0, W0, (Has1 ? BT::BitValue::One : BT::BitValue::Zero));
      return rr0(RC, Outputs);
    }
    case C2_and:
      return rr0(eAND(rc(1), rc(2)), Outputs);
    case C2_andn:
      return rr0(eAND(rc(1), eNOT(rc(2))), Outputs);
    case C2_not:
      return rr0(eNOT(rc(1)), Outputs);
    case C2_or:
      return rr0(eORL(rc(1), rc(2)), Outputs);
    case C2_orn:
      return rr0(eORL(rc(1), eNOT(rc(2))), Outputs);
    case C2_xor:
      return rr0(eXOR(rc(1), rc(2)), Outputs);
    case C4_and_and:
      return rr0(eAND(rc(1), eAND(rc(2), rc(3))), Outputs);
    case C4_and_andn:
      return rr0(eAND(rc(1), eAND(rc(2), eNOT(rc(3)))), Outputs);
    case C4_and_or:
      return rr0(eAND(rc(1), eORL(rc(2), rc(3))), Outputs);
    case C4_and_orn:
      return rr0(eAND(rc(1), eORL(rc(2), eNOT(rc(3)))), Outputs);
    case C4_or_and:
      return rr0(eORL(rc(1), eAND(rc(2), rc(3))), Outputs);
    case C4_or_andn:
      return rr0(eORL(rc(1), eAND(rc(2), eNOT(rc(3)))), Outputs);
    case C4_or_or:
      return rr0(eORL(rc(1), eORL(rc(2), rc(3))), Outputs);
    case C4_or_orn:
      return rr0(eORL(rc(1), eORL(rc(2), eNOT(rc(3)))), Outputs);
    case C2_bitsclr:
    case C2_bitsclri:
    case C2_bitsset:
    case C4_nbitsclr:
    case C4_nbitsclri:
    case C4_nbitsset:
      // TODO
      break;
    case S2_tstbit_i:
    case S4_ntstbit_i: {
      BT::BitValue V = rc(1)[im(2)];
      if (V.is(0) || V.is(1)) {
        // If instruction is S2_tstbit_i, test for 1, otherwise test for 0.
        bool TV = (Opc == S2_tstbit_i);
        BT::BitValue F = V.is(TV) ? BT::BitValue::One : BT::BitValue::Zero;
        return rr0(RegisterCell(W0).fill(0, W0, F), Outputs);
      }
      break;
    }

    default:
      // For instructions that define a single predicate registers, store
      // the low 8 bits of the register only.
      if (unsigned DefR = getUniqueDefVReg(MI)) {
        if (MRI.getRegClass(DefR) == &Hexagon::PredRegsRegClass) {
          BT::RegisterRef PD(DefR, 0);
          uint16_t RW = getRegBitWidth(PD);
          uint16_t PW = 8; // XXX Pred size: getRegBitWidth(Reg[1]);
          RegisterCell RC = RegisterCell::self(DefR, RW);
          RC.fill(PW, RW, BT::BitValue::Zero);
          putCell(PD, RC, Outputs);
          return true;
        }
      }
      return MachineEvaluator::evaluate(MI, Inputs, Outputs);
  }
  #undef im
  #undef rc
  #undef op
  return false;
}

bool HexagonEvaluator::evaluate(const MachineInstr &BI,
                                const CellMapType &Inputs,
                                BranchTargetList &Targets,
                                bool &FallsThru) const {
  // We need to evaluate one branch at a time. TII::analyzeBranch checks
  // all the branches in a basic block at once, so we cannot use it.
  unsigned Opc = BI.getOpcode();
  bool SimpleBranch = false;
  bool Negated = false;
  switch (Opc) {
    case Hexagon::J2_jumpf:
    case Hexagon::J2_jumpfpt:
    case Hexagon::J2_jumpfnew:
    case Hexagon::J2_jumpfnewpt:
      Negated = true;
      [[fallthrough]];
    case Hexagon::J2_jumpt:
    case Hexagon::J2_jumptpt:
    case Hexagon::J2_jumptnew:
    case Hexagon::J2_jumptnewpt:
      // Simple branch:  if([!]Pn) jump ...
      // i.e. Op0 = predicate, Op1 = branch target.
      SimpleBranch = true;
      break;
    case Hexagon::J2_jump:
      Targets.insert(BI.getOperand(0).getMBB());
      FallsThru = false;
      return true;
    default:
      // If the branch is of unknown type, assume that all successors are
      // executable.
      return false;
  }

  if (!SimpleBranch)
    return false;

  // BI is a conditional branch if we got here.
  RegisterRef PR = BI.getOperand(0);
  RegisterCell PC = getCell(PR, Inputs);
  const BT::BitValue &Test = PC[0];

  // If the condition is neither true nor false, then it's unknown.
  if (!Test.is(0) && !Test.is(1))
    return false;

  // "Test.is(!Negated)" means "branch condition is true".
  if (!Test.is(!Negated)) {
    // Condition known to be false.
    FallsThru = true;
    return true;
  }

  Targets.insert(BI.getOperand(1).getMBB());
  FallsThru = false;
  return true;
}

unsigned HexagonEvaluator::getUniqueDefVReg(const MachineInstr &MI) const {
  unsigned DefReg = 0;
  for (const MachineOperand &Op : MI.operands()) {
    if (!Op.isReg() || !Op.isDef())
      continue;
    Register R = Op.getReg();
    if (!R.isVirtual())
      continue;
    if (DefReg != 0)
      return 0;
    DefReg = R;
  }
  return DefReg;
}

bool HexagonEvaluator::evaluateLoad(const MachineInstr &MI,
                                    const CellMapType &Inputs,
                                    CellMapType &Outputs) const {
  using namespace Hexagon;

  if (TII.isPredicated(MI))
    return false;
  assert(MI.mayLoad() && "A load that mayn't?");
  unsigned Opc = MI.getOpcode();

  uint16_t BitNum;
  bool SignEx;

  switch (Opc) {
    default:
      return false;

#if 0
    // memb_fifo
    case L2_loadalignb_pbr:
    case L2_loadalignb_pcr:
    case L2_loadalignb_pi:
    // memh_fifo
    case L2_loadalignh_pbr:
    case L2_loadalignh_pcr:
    case L2_loadalignh_pi:
    // membh
    case L2_loadbsw2_pbr:
    case L2_loadbsw2_pci:
    case L2_loadbsw2_pcr:
    case L2_loadbsw2_pi:
    case L2_loadbsw4_pbr:
    case L2_loadbsw4_pci:
    case L2_loadbsw4_pcr:
    case L2_loadbsw4_pi:
    // memubh
    case L2_loadbzw2_pbr:
    case L2_loadbzw2_pci:
    case L2_loadbzw2_pcr:
    case L2_loadbzw2_pi:
    case L2_loadbzw4_pbr:
    case L2_loadbzw4_pci:
    case L2_loadbzw4_pcr:
    case L2_loadbzw4_pi:
#endif

    case L2_loadrbgp:
    case L2_loadrb_io:
    case L2_loadrb_pbr:
    case L2_loadrb_pci:
    case L2_loadrb_pcr:
    case L2_loadrb_pi:
    case PS_loadrbabs:
    case L4_loadrb_ap:
    case L4_loadrb_rr:
    case L4_loadrb_ur:
      BitNum = 8;
      SignEx = true;
      break;

    case L2_loadrubgp:
    case L2_loadrub_io:
    case L2_loadrub_pbr:
    case L2_loadrub_pci:
    case L2_loadrub_pcr:
    case L2_loadrub_pi:
    case PS_loadrubabs:
    case L4_loadrub_ap:
    case L4_loadrub_rr:
    case L4_loadrub_ur:
      BitNum = 8;
      SignEx = false;
      break;

    case L2_loadrhgp:
    case L2_loadrh_io:
    case L2_loadrh_pbr:
    case L2_loadrh_pci:
    case L2_loadrh_pcr:
    case L2_loadrh_pi:
    case PS_loadrhabs:
    case L4_loadrh_ap:
    case L4_loadrh_rr:
    case L4_loadrh_ur:
      BitNum = 16;
      SignEx = true;
      break;

    case L2_loadruhgp:
    case L2_loadruh_io:
    case L2_loadruh_pbr:
    case L2_loadruh_pci:
    case L2_loadruh_pcr:
    case L2_loadruh_pi:
    case L4_loadruh_rr:
    case PS_loadruhabs:
    case L4_loadruh_ap:
    case L4_loadruh_ur:
      BitNum = 16;
      SignEx = false;
      break;

    case L2_loadrigp:
    case L2_loadri_io:
    case L2_loadri_pbr:
    case L2_loadri_pci:
    case L2_loadri_pcr:
    case L2_loadri_pi:
    case L2_loadw_locked:
    case PS_loadriabs:
    case L4_loadri_ap:
    case L4_loadri_rr:
    case L4_loadri_ur:
    case LDriw_pred:
      BitNum = 32;
      SignEx = true;
      break;

    case L2_loadrdgp:
    case L2_loadrd_io:
    case L2_loadrd_pbr:
    case L2_loadrd_pci:
    case L2_loadrd_pcr:
    case L2_loadrd_pi:
    case L4_loadd_locked:
    case PS_loadrdabs:
    case L4_loadrd_ap:
    case L4_loadrd_rr:
    case L4_loadrd_ur:
      BitNum = 64;
      SignEx = true;
      break;
  }

  const MachineOperand &MD = MI.getOperand(0);
  assert(MD.isReg() && MD.isDef());
  RegisterRef RD = MD;

  uint16_t W = getRegBitWidth(RD);
  assert(W >= BitNum && BitNum > 0);
  RegisterCell Res(W);

  for (uint16_t i = 0; i < BitNum; ++i)
    Res[i] = BT::BitValue::self(BT::BitRef(RD.Reg, i));

  if (SignEx) {
    const BT::BitValue &Sign = Res[BitNum-1];
    for (uint16_t i = BitNum; i < W; ++i)
      Res[i] = BT::BitValue::ref(Sign);
  } else {
    for (uint16_t i = BitNum; i < W; ++i)
      Res[i] = BT::BitValue::Zero;
  }

  putCell(RD, Res, Outputs);
  return true;
}

bool HexagonEvaluator::evaluateFormalCopy(const MachineInstr &MI,
                                          const CellMapType &Inputs,
                                          CellMapType &Outputs) const {
  // If MI defines a formal parameter, but is not a copy (loads are handled
  // in evaluateLoad), then it's not clear what to do.
  assert(MI.isCopy());

  RegisterRef RD = MI.getOperand(0);
  RegisterRef RS = MI.getOperand(1);
  assert(RD.Sub == 0);
  if (!RS.Reg.isPhysical())
    return false;
  RegExtMap::const_iterator F = VRX.find(RD.Reg);
  if (F == VRX.end())
    return false;

  uint16_t EW = F->second.Width;
  // Store RD's cell into the map. This will associate the cell with a virtual
  // register, and make zero-/sign-extends possible (otherwise we would be ex-
  // tending "self" bit values, which will have no effect, since "self" values
  // cannot be references to anything).
  putCell(RD, getCell(RS, Inputs), Outputs);

  RegisterCell Res;
  // Read RD's cell from the outputs instead of RS's cell from the inputs:
  if (F->second.Type == ExtType::SExt)
    Res = eSXT(getCell(RD, Outputs), EW);
  else if (F->second.Type == ExtType::ZExt)
    Res = eZXT(getCell(RD, Outputs), EW);

  putCell(RD, Res, Outputs);
  return true;
}

unsigned HexagonEvaluator::getNextPhysReg(unsigned PReg, unsigned Width) const {
  using namespace Hexagon;

  bool Is64 = DoubleRegsRegClass.contains(PReg);
  assert(PReg == 0 || Is64 || IntRegsRegClass.contains(PReg));

  static const unsigned Phys32[] = { R0, R1, R2, R3, R4, R5 };
  static const unsigned Phys64[] = { D0, D1, D2 };
  const unsigned Num32 = sizeof(Phys32)/sizeof(unsigned);
  const unsigned Num64 = sizeof(Phys64)/sizeof(unsigned);

  // Return the first parameter register of the required width.
  if (PReg == 0)
    return (Width <= 32) ? Phys32[0] : Phys64[0];

  // Set Idx32, Idx64 in such a way that Idx+1 would give the index of the
  // next register.
  unsigned Idx32 = 0, Idx64 = 0;
  if (!Is64) {
    while (Idx32 < Num32) {
      if (Phys32[Idx32] == PReg)
        break;
      Idx32++;
    }
    Idx64 = Idx32/2;
  } else {
    while (Idx64 < Num64) {
      if (Phys64[Idx64] == PReg)
        break;
      Idx64++;
    }
    Idx32 = Idx64*2+1;
  }

  if (Width <= 32)
    return (Idx32+1 < Num32) ? Phys32[Idx32+1] : 0;
  return (Idx64+1 < Num64) ? Phys64[Idx64+1] : 0;
}

unsigned HexagonEvaluator::getVirtRegFor(unsigned PReg) const {
  for (std::pair<unsigned,unsigned> P : MRI.liveins())
    if (P.first == PReg)
      return P.second;
  return 0;
}
