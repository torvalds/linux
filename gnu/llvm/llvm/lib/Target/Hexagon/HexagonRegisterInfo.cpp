//===-- HexagonRegisterInfo.cpp - Hexagon Register Information ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Hexagon implementation of the TargetRegisterInfo
// class.
//
//===----------------------------------------------------------------------===//

#include "HexagonRegisterInfo.h"
#include "Hexagon.h"
#include "HexagonMachineFunctionInfo.h"
#include "HexagonSubtarget.h"
#include "HexagonTargetMachine.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Type.h"
#include "llvm/MC/MachineLocation.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"

#define GET_REGINFO_TARGET_DESC
#include "HexagonGenRegisterInfo.inc"

using namespace llvm;

static cl::opt<unsigned> FrameIndexSearchRange(
    "hexagon-frame-index-search-range", cl::init(32), cl::Hidden,
    cl::desc("Limit on instruction search range in frame index elimination"));

static cl::opt<unsigned> FrameIndexReuseLimit(
    "hexagon-frame-index-reuse-limit", cl::init(~0), cl::Hidden,
    cl::desc("Limit on the number of reused registers in frame index "
    "elimination"));

HexagonRegisterInfo::HexagonRegisterInfo(unsigned HwMode)
    : HexagonGenRegisterInfo(Hexagon::R31, 0/*DwarfFlavor*/, 0/*EHFlavor*/,
                             0/*PC*/, HwMode) {}


bool HexagonRegisterInfo::isEHReturnCalleeSaveReg(Register R) const {
  return R == Hexagon::R0 || R == Hexagon::R1 || R == Hexagon::R2 ||
         R == Hexagon::R3 || R == Hexagon::D0 || R == Hexagon::D1;
}

const MCPhysReg *
HexagonRegisterInfo::getCallerSavedRegs(const MachineFunction *MF,
      const TargetRegisterClass *RC) const {
  using namespace Hexagon;

  static const MCPhysReg Int32[] = {
    R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15, 0
  };
  static const MCPhysReg Int64[] = {
    D0, D1, D2, D3, D4, D5, D6, D7, 0
  };
  static const MCPhysReg Pred[] = {
    P0, P1, P2, P3, 0
  };
  static const MCPhysReg VecSgl[] = {
     V0,  V1,  V2,  V3,  V4,  V5,  V6,  V7,  V8,  V9, V10, V11, V12, V13,
    V14, V15, V16, V17, V18, V19, V20, V21, V22, V23, V24, V25, V26, V27,
    V28, V29, V30, V31,   0
  };
  static const MCPhysReg VecDbl[] = {
    W0, W1, W2, W3, W4, W5, W6, W7, W8, W9, W10, W11, W12, W13, W14, W15, 0
  };
  static const MCPhysReg VecPred[] = {
    Q0, Q1, Q2, Q3, 0
  };

  switch (RC->getID()) {
    case IntRegsRegClassID:
      return Int32;
    case DoubleRegsRegClassID:
      return Int64;
    case PredRegsRegClassID:
      return Pred;
    case HvxVRRegClassID:
      return VecSgl;
    case HvxWRRegClassID:
      return VecDbl;
    case HvxQRRegClassID:
      return VecPred;
    default:
      break;
  }

  static const MCPhysReg Empty[] = { 0 };
#ifndef NDEBUG
  dbgs() << "Register class: " << getRegClassName(RC) << "\n";
#endif
  llvm_unreachable("Unexpected register class");
  return Empty;
}


const MCPhysReg *
HexagonRegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  static const MCPhysReg CalleeSavedRegsV3[] = {
    Hexagon::R16,   Hexagon::R17,   Hexagon::R18,   Hexagon::R19,
    Hexagon::R20,   Hexagon::R21,   Hexagon::R22,   Hexagon::R23,
    Hexagon::R24,   Hexagon::R25,   Hexagon::R26,   Hexagon::R27, 0
  };

  // Functions that contain a call to __builtin_eh_return also save the first 4
  // parameter registers.
  static const MCPhysReg CalleeSavedRegsV3EHReturn[] = {
    Hexagon::R0,    Hexagon::R1,    Hexagon::R2,    Hexagon::R3,
    Hexagon::R16,   Hexagon::R17,   Hexagon::R18,   Hexagon::R19,
    Hexagon::R20,   Hexagon::R21,   Hexagon::R22,   Hexagon::R23,
    Hexagon::R24,   Hexagon::R25,   Hexagon::R26,   Hexagon::R27, 0
  };

  bool HasEHReturn = MF->getInfo<HexagonMachineFunctionInfo>()->hasEHReturn();

  return HasEHReturn ? CalleeSavedRegsV3EHReturn : CalleeSavedRegsV3;
}


const uint32_t *HexagonRegisterInfo::getCallPreservedMask(
      const MachineFunction &MF, CallingConv::ID) const {
  return HexagonCSR_RegMask;
}


BitVector HexagonRegisterInfo::getReservedRegs(const MachineFunction &MF)
      const {
  BitVector Reserved(getNumRegs());
  Reserved.set(Hexagon::R29);
  Reserved.set(Hexagon::R30);
  Reserved.set(Hexagon::R31);
  Reserved.set(Hexagon::VTMP);

  // Guest registers.
  Reserved.set(Hexagon::GELR);        // G0
  Reserved.set(Hexagon::GSR);         // G1
  Reserved.set(Hexagon::GOSP);        // G2
  Reserved.set(Hexagon::G3);          // G3

  // Control registers.
  Reserved.set(Hexagon::SA0);         // C0
  Reserved.set(Hexagon::LC0);         // C1
  Reserved.set(Hexagon::SA1);         // C2
  Reserved.set(Hexagon::LC1);         // C3
  Reserved.set(Hexagon::P3_0);        // C4
  Reserved.set(Hexagon::USR);         // C8
  Reserved.set(Hexagon::PC);          // C9
  Reserved.set(Hexagon::UGP);         // C10
  Reserved.set(Hexagon::GP);          // C11
  Reserved.set(Hexagon::CS0);         // C12
  Reserved.set(Hexagon::CS1);         // C13
  Reserved.set(Hexagon::UPCYCLELO);   // C14
  Reserved.set(Hexagon::UPCYCLEHI);   // C15
  Reserved.set(Hexagon::FRAMELIMIT);  // C16
  Reserved.set(Hexagon::FRAMEKEY);    // C17
  Reserved.set(Hexagon::PKTCOUNTLO);  // C18
  Reserved.set(Hexagon::PKTCOUNTHI);  // C19
  Reserved.set(Hexagon::UTIMERLO);    // C30
  Reserved.set(Hexagon::UTIMERHI);    // C31
  // Out of the control registers, only C8 is explicitly defined in
  // HexagonRegisterInfo.td. If others are defined, make sure to add
  // them here as well.
  Reserved.set(Hexagon::C8);
  Reserved.set(Hexagon::USR_OVF);

  // Leveraging these registers will require more work to recognize
  // the new semantics posed, Hi/LoVec patterns, etc.
  // Note well: if enabled, they should be restricted to only
  // where `HST.useHVXOps() && HST.hasV67Ops()` is true.
  for (auto Reg : Hexagon_MC::GetVectRegRev())
    Reserved.set(Reg);

  if (MF.getSubtarget<HexagonSubtarget>().hasReservedR19())
    Reserved.set(Hexagon::R19);

  Register AP =
      MF.getInfo<HexagonMachineFunctionInfo>()->getStackAlignBaseReg();
  if (AP.isValid())
    Reserved.set(AP);

  for (int x = Reserved.find_first(); x >= 0; x = Reserved.find_next(x))
    markSuperRegs(Reserved, x);

  return Reserved;
}

bool HexagonRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                              int SPAdj, unsigned FIOp,
                                              RegScavenger *RS) const {
  static unsigned ReuseCount = 0;
  //
  // Hexagon_TODO: Do we need to enforce this for Hexagon?
  assert(SPAdj == 0 && "Unexpected");

  MachineInstr &MI = *II;
  MachineBasicBlock &MB = *MI.getParent();
  MachineFunction &MF = *MB.getParent();
  auto &HST = MF.getSubtarget<HexagonSubtarget>();
  auto &HII = *HST.getInstrInfo();
  auto &HFI = *HST.getFrameLowering();

  Register BP;
  int FI = MI.getOperand(FIOp).getIndex();
  // Select the base pointer (BP) and calculate the actual offset from BP
  // to the beginning of the object at index FI.
  int Offset = HFI.getFrameIndexReference(MF, FI, BP).getFixed();
  // Add the offset from the instruction.
  int RealOffset = Offset + MI.getOperand(FIOp+1).getImm();

  unsigned Opc = MI.getOpcode();
  switch (Opc) {
    case Hexagon::PS_fia:
      MI.setDesc(HII.get(Hexagon::A2_addi));
      MI.getOperand(FIOp).ChangeToImmediate(RealOffset);
      MI.removeOperand(FIOp+1);
      return false;
    case Hexagon::PS_fi:
      // Set up the instruction for updating below.
      MI.setDesc(HII.get(Hexagon::A2_addi));
      break;
  }

  if (!HII.isValidOffset(Opc, RealOffset, this)) {
    // If the offset is not valid, calculate the address in a temporary
    // register and use it with offset 0.
    int InstOffset = 0;
    // The actual base register (BP) is typically shared between many
    // instructions where frame indices are being replaced. In scalar
    // instructions the offset range is large, and the need for an extra
    // add instruction is infrequent. Vector loads/stores, however, have
    // a much smaller offset range: [-8, 7), or #s4. In those cases it
    // makes sense to "standardize" the immediate in the "addi" instruction
    // so that multiple loads/stores could be based on it.
    bool IsPair = false;
    switch (MI.getOpcode()) {
      // All of these instructions have the same format: base+#s4.
      case Hexagon::PS_vloadrw_ai:
      case Hexagon::PS_vloadrw_nt_ai:
      case Hexagon::PS_vstorerw_ai:
      case Hexagon::PS_vstorerw_nt_ai:
        IsPair = true;
        [[fallthrough]];
      case Hexagon::PS_vloadrv_ai:
      case Hexagon::PS_vloadrv_nt_ai:
      case Hexagon::PS_vstorerv_ai:
      case Hexagon::PS_vstorerv_nt_ai:
      case Hexagon::V6_vL32b_ai:
      case Hexagon::V6_vS32b_ai: {
        unsigned HwLen = HST.getVectorLength();
        if (RealOffset % HwLen == 0) {
          int VecOffset = RealOffset / HwLen;
          // Rewrite the offset as "base + [-8, 7)".
          VecOffset += 8;
          // Pairs are expanded into two instructions: make sure that both
          // can use the same base (i.e. VecOffset+1 is not a different
          // multiple of 16 than VecOffset).
          if (!IsPair || (VecOffset + 1) % 16 != 0) {
            RealOffset = (VecOffset & -16) * HwLen;
            InstOffset = (VecOffset % 16 - 8) * HwLen;
          }
        }
      }
    }

    // Search backwards in the block for "Reg = A2_addi BP, RealOffset".
    // This will give us a chance to avoid creating a new register.
    Register ReuseBP;

    if (ReuseCount < FrameIndexReuseLimit) {
      unsigned SearchCount = 0, SearchRange = FrameIndexSearchRange;
      SmallSet<Register,2> SeenVRegs;
      bool PassedCall = false;
      LiveRegUnits Defs(*this), Uses(*this);

      for (auto I = std::next(II.getReverse()), E = MB.rend(); I != E; ++I) {
        if (SearchCount == SearchRange)
          break;
        ++SearchCount;
        const MachineInstr &BI = *I;
        LiveRegUnits::accumulateUsedDefed(BI, Defs, Uses, this);
        PassedCall |= BI.isCall();
        for (const MachineOperand &Op : BI.operands()) {
          if (SeenVRegs.size() > 1)
            break;
          if (Op.isReg() && Op.getReg().isVirtual())
            SeenVRegs.insert(Op.getReg());
        }
        if (BI.getOpcode() != Hexagon::A2_addi)
          continue;
        if (BI.getOperand(1).getReg() != BP)
          continue;
        const auto &Op2 = BI.getOperand(2);
        if (!Op2.isImm() || Op2.getImm() != RealOffset)
          continue;

        Register R = BI.getOperand(0).getReg();
        if (R.isPhysical()) {
          if (Defs.available(R))
            ReuseBP = R;
        } else if (R.isVirtual()) {
          // Extending a range of a virtual register can be dangerous,
          // since the scavenger will need to find a physical register
          // for it. Avoid extending the range past a function call,
          // and avoid overlapping it with another virtual register.
          if (!PassedCall && SeenVRegs.size() <= 1)
            ReuseBP = R;
        }
        break;
      }
      if (ReuseBP)
        ++ReuseCount;
    }

    auto &MRI = MF.getRegInfo();
    if (!ReuseBP) {
      ReuseBP = MRI.createVirtualRegister(&Hexagon::IntRegsRegClass);
      const DebugLoc &DL = MI.getDebugLoc();
      BuildMI(MB, II, DL, HII.get(Hexagon::A2_addi), ReuseBP)
        .addReg(BP)
        .addImm(RealOffset);
    }
    BP = ReuseBP;
    RealOffset = InstOffset;
  }

  MI.getOperand(FIOp).ChangeToRegister(BP, false, false, false);
  MI.getOperand(FIOp+1).ChangeToImmediate(RealOffset);
  return false;
}


bool HexagonRegisterInfo::shouldCoalesce(MachineInstr *MI,
      const TargetRegisterClass *SrcRC, unsigned SubReg,
      const TargetRegisterClass *DstRC, unsigned DstSubReg,
      const TargetRegisterClass *NewRC, LiveIntervals &LIS) const {
  // Coalescing will extend the live interval of the destination register.
  // If the destination register is a vector pair, avoid introducing function
  // calls into the interval, since it could result in a spilling of a pair
  // instead of a single vector.
  MachineFunction &MF = *MI->getParent()->getParent();
  const HexagonSubtarget &HST = MF.getSubtarget<HexagonSubtarget>();
  if (!HST.useHVXOps() || NewRC->getID() != Hexagon::HvxWRRegClass.getID())
    return true;
  bool SmallSrc = SrcRC->getID() == Hexagon::HvxVRRegClass.getID();
  bool SmallDst = DstRC->getID() == Hexagon::HvxVRRegClass.getID();
  if (!SmallSrc && !SmallDst)
    return true;

  Register DstReg = MI->getOperand(0).getReg();
  Register SrcReg = MI->getOperand(1).getReg();
  const SlotIndexes &Indexes = *LIS.getSlotIndexes();
  auto HasCall = [&Indexes] (const LiveInterval::Segment &S) {
    for (SlotIndex I = S.start.getBaseIndex(), E = S.end.getBaseIndex();
         I != E; I = I.getNextIndex()) {
      if (const MachineInstr *MI = Indexes.getInstructionFromIndex(I))
        if (MI->isCall())
          return true;
    }
    return false;
  };

  if (SmallSrc == SmallDst) {
    // Both must be true, because the case for both being false was
    // checked earlier. Both registers will be coalesced into a register
    // of a wider class (HvxWR), and we don't want its live range to
    // span over calls.
    return !any_of(LIS.getInterval(DstReg), HasCall) &&
           !any_of(LIS.getInterval(SrcReg), HasCall);
  }

  // If one register is large (HvxWR) and the other is small (HvxVR), then
  // coalescing is ok if the large is already live across a function call,
  // or if the small one is not.
  Register SmallReg = SmallSrc ? SrcReg : DstReg;
  Register LargeReg = SmallSrc ? DstReg : SrcReg;
  return  any_of(LIS.getInterval(LargeReg), HasCall) ||
         !any_of(LIS.getInterval(SmallReg), HasCall);
}


Register HexagonRegisterInfo::getFrameRegister(const MachineFunction
                                               &MF) const {
  const HexagonFrameLowering *TFI = getFrameLowering(MF);
  if (TFI->hasFP(MF))
    return getFrameRegister();
  return getStackRegister();
}


Register HexagonRegisterInfo::getFrameRegister() const {
  return Hexagon::R30;
}


Register HexagonRegisterInfo::getStackRegister() const {
  return Hexagon::R29;
}


unsigned HexagonRegisterInfo::getHexagonSubRegIndex(
      const TargetRegisterClass &RC, unsigned GenIdx) const {
  assert(GenIdx == Hexagon::ps_sub_lo || GenIdx == Hexagon::ps_sub_hi);

  static const unsigned ISub[] = { Hexagon::isub_lo, Hexagon::isub_hi };
  static const unsigned VSub[] = { Hexagon::vsub_lo, Hexagon::vsub_hi };
  static const unsigned WSub[] = { Hexagon::wsub_lo, Hexagon::wsub_hi };

  switch (RC.getID()) {
    case Hexagon::CtrRegs64RegClassID:
    case Hexagon::DoubleRegsRegClassID:
      return ISub[GenIdx];
    case Hexagon::HvxWRRegClassID:
      return VSub[GenIdx];
    case Hexagon::HvxVQRRegClassID:
      return WSub[GenIdx];
  }

  if (const TargetRegisterClass *SuperRC = *RC.getSuperClasses())
    return getHexagonSubRegIndex(*SuperRC, GenIdx);

  llvm_unreachable("Invalid register class");
}

bool HexagonRegisterInfo::useFPForScavengingIndex(const MachineFunction &MF)
      const {
  return MF.getSubtarget<HexagonSubtarget>().getFrameLowering()->hasFP(MF);
}

const TargetRegisterClass *
HexagonRegisterInfo::getPointerRegClass(const MachineFunction &MF,
                                        unsigned Kind) const {
  return &Hexagon::IntRegsRegClass;
}
