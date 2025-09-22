//===-- SIRegisterInfo.cpp - SI Register Information ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// SI implementation of the TargetRegisterInfo class.
//
//===----------------------------------------------------------------------===//

#include "AMDGPU.h"
#include "AMDGPURegisterBankInfo.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUInstPrinter.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIMachineFunctionInfo.h"
#include "SIRegisterInfo.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"

using namespace llvm;

#define GET_REGINFO_TARGET_DESC
#include "AMDGPUGenRegisterInfo.inc"

static cl::opt<bool> EnableSpillSGPRToVGPR(
  "amdgpu-spill-sgpr-to-vgpr",
  cl::desc("Enable spilling SGPRs to VGPRs"),
  cl::ReallyHidden,
  cl::init(true));

std::array<std::vector<int16_t>, 16> SIRegisterInfo::RegSplitParts;
std::array<std::array<uint16_t, 32>, 9> SIRegisterInfo::SubRegFromChannelTable;

// Map numbers of DWORDs to indexes in SubRegFromChannelTable.
// Valid indexes are shifted 1, such that a 0 mapping means unsupported.
// e.g. for 8 DWORDs (256-bit), SubRegFromChannelTableWidthMap[8] = 8,
//      meaning index 7 in SubRegFromChannelTable.
static const std::array<unsigned, 17> SubRegFromChannelTableWidthMap = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 9};

namespace llvm {

// A temporary struct to spill SGPRs.
// This is mostly to spill SGPRs to memory. Spilling SGPRs into VGPR lanes emits
// just v_writelane and v_readlane.
//
// When spilling to memory, the SGPRs are written into VGPR lanes and the VGPR
// is saved to scratch (or the other way around for loads).
// For this, a VGPR is required where the needed lanes can be clobbered. The
// RegScavenger can provide a VGPR where currently active lanes can be
// clobbered, but we still need to save inactive lanes.
// The high-level steps are:
// - Try to scavenge SGPR(s) to save exec
// - Try to scavenge VGPR
// - Save needed, all or inactive lanes of a TmpVGPR
// - Spill/Restore SGPRs using TmpVGPR
// - Restore TmpVGPR
//
// To save all lanes of TmpVGPR, exec needs to be saved and modified. If we
// cannot scavenge temporary SGPRs to save exec, we use the following code:
// buffer_store_dword TmpVGPR ; only if active lanes need to be saved
// s_not exec, exec
// buffer_store_dword TmpVGPR ; save inactive lanes
// s_not exec, exec
struct SGPRSpillBuilder {
  struct PerVGPRData {
    unsigned PerVGPR;
    unsigned NumVGPRs;
    int64_t VGPRLanes;
  };

  // The SGPR to save
  Register SuperReg;
  MachineBasicBlock::iterator MI;
  ArrayRef<int16_t> SplitParts;
  unsigned NumSubRegs;
  bool IsKill;
  const DebugLoc &DL;

  /* When spilling to stack */
  // The SGPRs are written into this VGPR, which is then written to scratch
  // (or vice versa for loads).
  Register TmpVGPR = AMDGPU::NoRegister;
  // Temporary spill slot to save TmpVGPR to.
  int TmpVGPRIndex = 0;
  // If TmpVGPR is live before the spill or if it is scavenged.
  bool TmpVGPRLive = false;
  // Scavenged SGPR to save EXEC.
  Register SavedExecReg = AMDGPU::NoRegister;
  // Stack index to write the SGPRs to.
  int Index;
  unsigned EltSize = 4;

  RegScavenger *RS;
  MachineBasicBlock *MBB;
  MachineFunction &MF;
  SIMachineFunctionInfo &MFI;
  const SIInstrInfo &TII;
  const SIRegisterInfo &TRI;
  bool IsWave32;
  Register ExecReg;
  unsigned MovOpc;
  unsigned NotOpc;

  SGPRSpillBuilder(const SIRegisterInfo &TRI, const SIInstrInfo &TII,
                   bool IsWave32, MachineBasicBlock::iterator MI, int Index,
                   RegScavenger *RS)
      : SGPRSpillBuilder(TRI, TII, IsWave32, MI, MI->getOperand(0).getReg(),
                         MI->getOperand(0).isKill(), Index, RS) {}

  SGPRSpillBuilder(const SIRegisterInfo &TRI, const SIInstrInfo &TII,
                   bool IsWave32, MachineBasicBlock::iterator MI, Register Reg,
                   bool IsKill, int Index, RegScavenger *RS)
      : SuperReg(Reg), MI(MI), IsKill(IsKill), DL(MI->getDebugLoc()),
        Index(Index), RS(RS), MBB(MI->getParent()), MF(*MBB->getParent()),
        MFI(*MF.getInfo<SIMachineFunctionInfo>()), TII(TII), TRI(TRI),
        IsWave32(IsWave32) {
    const TargetRegisterClass *RC = TRI.getPhysRegBaseClass(SuperReg);
    SplitParts = TRI.getRegSplitParts(RC, EltSize);
    NumSubRegs = SplitParts.empty() ? 1 : SplitParts.size();

    if (IsWave32) {
      ExecReg = AMDGPU::EXEC_LO;
      MovOpc = AMDGPU::S_MOV_B32;
      NotOpc = AMDGPU::S_NOT_B32;
    } else {
      ExecReg = AMDGPU::EXEC;
      MovOpc = AMDGPU::S_MOV_B64;
      NotOpc = AMDGPU::S_NOT_B64;
    }

    assert(SuperReg != AMDGPU::M0 && "m0 should never spill");
    assert(SuperReg != AMDGPU::EXEC_LO && SuperReg != AMDGPU::EXEC_HI &&
           SuperReg != AMDGPU::EXEC && "exec should never spill");
  }

  PerVGPRData getPerVGPRData() {
    PerVGPRData Data;
    Data.PerVGPR = IsWave32 ? 32 : 64;
    Data.NumVGPRs = (NumSubRegs + (Data.PerVGPR - 1)) / Data.PerVGPR;
    Data.VGPRLanes = (1LL << std::min(Data.PerVGPR, NumSubRegs)) - 1LL;
    return Data;
  }

  // Tries to scavenge SGPRs to save EXEC and a VGPR. Uses v0 if no VGPR is
  // free.
  // Writes these instructions if an SGPR can be scavenged:
  // s_mov_b64 s[6:7], exec   ; Save exec
  // s_mov_b64 exec, 3        ; Wanted lanemask
  // buffer_store_dword v1    ; Write scavenged VGPR to emergency slot
  //
  // Writes these instructions if no SGPR can be scavenged:
  // buffer_store_dword v0    ; Only if no free VGPR was found
  // s_not_b64 exec, exec
  // buffer_store_dword v0    ; Save inactive lanes
  //                          ; exec stays inverted, it is flipped back in
  //                          ; restore.
  void prepare() {
    // Scavenged temporary VGPR to use. It must be scavenged once for any number
    // of spilled subregs.
    // FIXME: The liveness analysis is limited and does not tell if a register
    // is in use in lanes that are currently inactive. We can never be sure if
    // a register as actually in use in another lane, so we need to save all
    // used lanes of the chosen VGPR.
    assert(RS && "Cannot spill SGPR to memory without RegScavenger");
    TmpVGPR = RS->scavengeRegisterBackwards(AMDGPU::VGPR_32RegClass, MI, false,
                                            0, false);

    // Reserve temporary stack slot
    TmpVGPRIndex = MFI.getScavengeFI(MF.getFrameInfo(), TRI);
    if (TmpVGPR) {
      // Found a register that is dead in the currently active lanes, we only
      // need to spill inactive lanes.
      TmpVGPRLive = false;
    } else {
      // Pick v0 because it doesn't make a difference.
      TmpVGPR = AMDGPU::VGPR0;
      TmpVGPRLive = true;
    }

    if (TmpVGPRLive) {
      // We need to inform the scavenger that this index is already in use until
      // we're done with the custom emergency spill.
      RS->assignRegToScavengingIndex(TmpVGPRIndex, TmpVGPR);
    }

    // We may end up recursively calling the scavenger, and don't want to re-use
    // the same register.
    RS->setRegUsed(TmpVGPR);

    // Try to scavenge SGPRs to save exec
    assert(!SavedExecReg && "Exec is already saved, refuse to save again");
    const TargetRegisterClass &RC =
        IsWave32 ? AMDGPU::SGPR_32RegClass : AMDGPU::SGPR_64RegClass;
    RS->setRegUsed(SuperReg);
    SavedExecReg = RS->scavengeRegisterBackwards(RC, MI, false, 0, false);

    int64_t VGPRLanes = getPerVGPRData().VGPRLanes;

    if (SavedExecReg) {
      RS->setRegUsed(SavedExecReg);
      // Set exec to needed lanes
      BuildMI(*MBB, MI, DL, TII.get(MovOpc), SavedExecReg).addReg(ExecReg);
      auto I =
          BuildMI(*MBB, MI, DL, TII.get(MovOpc), ExecReg).addImm(VGPRLanes);
      if (!TmpVGPRLive)
        I.addReg(TmpVGPR, RegState::ImplicitDefine);
      // Spill needed lanes
      TRI.buildVGPRSpillLoadStore(*this, TmpVGPRIndex, 0, /*IsLoad*/ false);
    } else {
      // The modify and restore of exec clobber SCC, which we would have to save
      // and restore. FIXME: We probably would need to reserve a register for
      // this.
      if (RS->isRegUsed(AMDGPU::SCC))
        MI->emitError("unhandled SGPR spill to memory");

      // Spill active lanes
      if (TmpVGPRLive)
        TRI.buildVGPRSpillLoadStore(*this, TmpVGPRIndex, 0, /*IsLoad*/ false,
                                    /*IsKill*/ false);
      // Spill inactive lanes
      auto I = BuildMI(*MBB, MI, DL, TII.get(NotOpc), ExecReg).addReg(ExecReg);
      if (!TmpVGPRLive)
        I.addReg(TmpVGPR, RegState::ImplicitDefine);
      I->getOperand(2).setIsDead(); // Mark SCC as dead.
      TRI.buildVGPRSpillLoadStore(*this, TmpVGPRIndex, 0, /*IsLoad*/ false);
    }
  }

  // Writes these instructions if an SGPR can be scavenged:
  // buffer_load_dword v1     ; Write scavenged VGPR to emergency slot
  // s_waitcnt vmcnt(0)       ; If a free VGPR was found
  // s_mov_b64 exec, s[6:7]   ; Save exec
  //
  // Writes these instructions if no SGPR can be scavenged:
  // buffer_load_dword v0     ; Restore inactive lanes
  // s_waitcnt vmcnt(0)       ; If a free VGPR was found
  // s_not_b64 exec, exec
  // buffer_load_dword v0     ; Only if no free VGPR was found
  void restore() {
    if (SavedExecReg) {
      // Restore used lanes
      TRI.buildVGPRSpillLoadStore(*this, TmpVGPRIndex, 0, /*IsLoad*/ true,
                                  /*IsKill*/ false);
      // Restore exec
      auto I = BuildMI(*MBB, MI, DL, TII.get(MovOpc), ExecReg)
                   .addReg(SavedExecReg, RegState::Kill);
      // Add an implicit use of the load so it is not dead.
      // FIXME This inserts an unnecessary waitcnt
      if (!TmpVGPRLive) {
        I.addReg(TmpVGPR, RegState::ImplicitKill);
      }
    } else {
      // Restore inactive lanes
      TRI.buildVGPRSpillLoadStore(*this, TmpVGPRIndex, 0, /*IsLoad*/ true,
                                  /*IsKill*/ false);
      auto I = BuildMI(*MBB, MI, DL, TII.get(NotOpc), ExecReg).addReg(ExecReg);
      if (!TmpVGPRLive)
        I.addReg(TmpVGPR, RegState::ImplicitKill);
      I->getOperand(2).setIsDead(); // Mark SCC as dead.

      // Restore active lanes
      if (TmpVGPRLive)
        TRI.buildVGPRSpillLoadStore(*this, TmpVGPRIndex, 0, /*IsLoad*/ true);
    }

    // Inform the scavenger where we're releasing our custom scavenged register.
    if (TmpVGPRLive) {
      MachineBasicBlock::iterator RestorePt = std::prev(MI);
      RS->assignRegToScavengingIndex(TmpVGPRIndex, TmpVGPR, &*RestorePt);
    }
  }

  // Write TmpVGPR to memory or read TmpVGPR from memory.
  // Either using a single buffer_load/store if exec is set to the needed mask
  // or using
  // buffer_load
  // s_not exec, exec
  // buffer_load
  // s_not exec, exec
  void readWriteTmpVGPR(unsigned Offset, bool IsLoad) {
    if (SavedExecReg) {
      // Spill needed lanes
      TRI.buildVGPRSpillLoadStore(*this, Index, Offset, IsLoad);
    } else {
      // The modify and restore of exec clobber SCC, which we would have to save
      // and restore. FIXME: We probably would need to reserve a register for
      // this.
      if (RS->isRegUsed(AMDGPU::SCC))
        MI->emitError("unhandled SGPR spill to memory");

      // Spill active lanes
      TRI.buildVGPRSpillLoadStore(*this, Index, Offset, IsLoad,
                                  /*IsKill*/ false);
      // Spill inactive lanes
      auto Not0 = BuildMI(*MBB, MI, DL, TII.get(NotOpc), ExecReg).addReg(ExecReg);
      Not0->getOperand(2).setIsDead(); // Mark SCC as dead.
      TRI.buildVGPRSpillLoadStore(*this, Index, Offset, IsLoad);
      auto Not1 = BuildMI(*MBB, MI, DL, TII.get(NotOpc), ExecReg).addReg(ExecReg);
      Not1->getOperand(2).setIsDead(); // Mark SCC as dead.
    }
  }

  void setMI(MachineBasicBlock *NewMBB, MachineBasicBlock::iterator NewMI) {
    assert(MBB->getParent() == &MF);
    MI = NewMI;
    MBB = NewMBB;
  }
};

} // namespace llvm

SIRegisterInfo::SIRegisterInfo(const GCNSubtarget &ST)
    : AMDGPUGenRegisterInfo(AMDGPU::PC_REG, ST.getAMDGPUDwarfFlavour(),
                            ST.getAMDGPUDwarfFlavour()),
      ST(ST), SpillSGPRToVGPR(EnableSpillSGPRToVGPR), isWave32(ST.isWave32()) {

  assert(getSubRegIndexLaneMask(AMDGPU::sub0).getAsInteger() == 3 &&
         getSubRegIndexLaneMask(AMDGPU::sub31).getAsInteger() == (3ULL << 62) &&
         (getSubRegIndexLaneMask(AMDGPU::lo16) |
          getSubRegIndexLaneMask(AMDGPU::hi16)).getAsInteger() ==
           getSubRegIndexLaneMask(AMDGPU::sub0).getAsInteger() &&
         "getNumCoveredRegs() will not work with generated subreg masks!");

  RegPressureIgnoredUnits.resize(getNumRegUnits());
  RegPressureIgnoredUnits.set(*regunits(MCRegister::from(AMDGPU::M0)).begin());
  for (auto Reg : AMDGPU::VGPR_16RegClass) {
    if (AMDGPU::isHi(Reg, *this))
      RegPressureIgnoredUnits.set(*regunits(Reg).begin());
  }

  // HACK: Until this is fully tablegen'd.
  static llvm::once_flag InitializeRegSplitPartsFlag;

  static auto InitializeRegSplitPartsOnce = [this]() {
    for (unsigned Idx = 1, E = getNumSubRegIndices() - 1; Idx < E; ++Idx) {
      unsigned Size = getSubRegIdxSize(Idx);
      if (Size & 31)
        continue;
      std::vector<int16_t> &Vec = RegSplitParts[Size / 32 - 1];
      unsigned Pos = getSubRegIdxOffset(Idx);
      if (Pos % Size)
        continue;
      Pos /= Size;
      if (Vec.empty()) {
        unsigned MaxNumParts = 1024 / Size; // Maximum register is 1024 bits.
        Vec.resize(MaxNumParts);
      }
      Vec[Pos] = Idx;
    }
  };

  static llvm::once_flag InitializeSubRegFromChannelTableFlag;

  static auto InitializeSubRegFromChannelTableOnce = [this]() {
    for (auto &Row : SubRegFromChannelTable)
      Row.fill(AMDGPU::NoSubRegister);
    for (unsigned Idx = 1; Idx < getNumSubRegIndices(); ++Idx) {
      unsigned Width = getSubRegIdxSize(Idx) / 32;
      unsigned Offset = getSubRegIdxOffset(Idx) / 32;
      assert(Width < SubRegFromChannelTableWidthMap.size());
      Width = SubRegFromChannelTableWidthMap[Width];
      if (Width == 0)
        continue;
      unsigned TableIdx = Width - 1;
      assert(TableIdx < SubRegFromChannelTable.size());
      assert(Offset < SubRegFromChannelTable[TableIdx].size());
      SubRegFromChannelTable[TableIdx][Offset] = Idx;
    }
  };

  llvm::call_once(InitializeRegSplitPartsFlag, InitializeRegSplitPartsOnce);
  llvm::call_once(InitializeSubRegFromChannelTableFlag,
                  InitializeSubRegFromChannelTableOnce);
}

void SIRegisterInfo::reserveRegisterTuples(BitVector &Reserved,
                                           MCRegister Reg) const {
  for (MCRegAliasIterator R(Reg, this, true); R.isValid(); ++R)
    Reserved.set(*R);
}

// Forced to be here by one .inc
const MCPhysReg *SIRegisterInfo::getCalleeSavedRegs(
  const MachineFunction *MF) const {
  CallingConv::ID CC = MF->getFunction().getCallingConv();
  switch (CC) {
  case CallingConv::C:
  case CallingConv::Fast:
  case CallingConv::Cold:
    return ST.hasGFX90AInsts() ? CSR_AMDGPU_GFX90AInsts_SaveList
                               : CSR_AMDGPU_SaveList;
  case CallingConv::AMDGPU_Gfx:
    return ST.hasGFX90AInsts() ? CSR_AMDGPU_SI_Gfx_GFX90AInsts_SaveList
                               : CSR_AMDGPU_SI_Gfx_SaveList;
  case CallingConv::AMDGPU_CS_ChainPreserve:
    return CSR_AMDGPU_CS_ChainPreserve_SaveList;
  default: {
    // Dummy to not crash RegisterClassInfo.
    static const MCPhysReg NoCalleeSavedReg = AMDGPU::NoRegister;
    return &NoCalleeSavedReg;
  }
  }
}

const MCPhysReg *
SIRegisterInfo::getCalleeSavedRegsViaCopy(const MachineFunction *MF) const {
  return nullptr;
}

const uint32_t *SIRegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                                     CallingConv::ID CC) const {
  switch (CC) {
  case CallingConv::C:
  case CallingConv::Fast:
  case CallingConv::Cold:
    return ST.hasGFX90AInsts() ? CSR_AMDGPU_GFX90AInsts_RegMask
                               : CSR_AMDGPU_RegMask;
  case CallingConv::AMDGPU_Gfx:
    return ST.hasGFX90AInsts() ? CSR_AMDGPU_SI_Gfx_GFX90AInsts_RegMask
                               : CSR_AMDGPU_SI_Gfx_RegMask;
  case CallingConv::AMDGPU_CS_Chain:
  case CallingConv::AMDGPU_CS_ChainPreserve:
    // Calls to these functions never return, so we can pretend everything is
    // preserved.
    return AMDGPU_AllVGPRs_RegMask;
  default:
    return nullptr;
  }
}

const uint32_t *SIRegisterInfo::getNoPreservedMask() const {
  return CSR_AMDGPU_NoRegs_RegMask;
}

bool SIRegisterInfo::isChainScratchRegister(Register VGPR) {
  return VGPR >= AMDGPU::VGPR0 && VGPR < AMDGPU::VGPR8;
}

const TargetRegisterClass *
SIRegisterInfo::getLargestLegalSuperClass(const TargetRegisterClass *RC,
                                          const MachineFunction &MF) const {
  // FIXME: Should have a helper function like getEquivalentVGPRClass to get the
  // equivalent AV class. If used one, the verifier will crash after
  // RegBankSelect in the GISel flow. The aligned regclasses are not fully given
  // until Instruction selection.
  if (ST.hasMAIInsts() && (isVGPRClass(RC) || isAGPRClass(RC))) {
    if (RC == &AMDGPU::VGPR_32RegClass || RC == &AMDGPU::AGPR_32RegClass)
      return &AMDGPU::AV_32RegClass;
    if (RC == &AMDGPU::VReg_64RegClass || RC == &AMDGPU::AReg_64RegClass)
      return &AMDGPU::AV_64RegClass;
    if (RC == &AMDGPU::VReg_64_Align2RegClass ||
        RC == &AMDGPU::AReg_64_Align2RegClass)
      return &AMDGPU::AV_64_Align2RegClass;
    if (RC == &AMDGPU::VReg_96RegClass || RC == &AMDGPU::AReg_96RegClass)
      return &AMDGPU::AV_96RegClass;
    if (RC == &AMDGPU::VReg_96_Align2RegClass ||
        RC == &AMDGPU::AReg_96_Align2RegClass)
      return &AMDGPU::AV_96_Align2RegClass;
    if (RC == &AMDGPU::VReg_128RegClass || RC == &AMDGPU::AReg_128RegClass)
      return &AMDGPU::AV_128RegClass;
    if (RC == &AMDGPU::VReg_128_Align2RegClass ||
        RC == &AMDGPU::AReg_128_Align2RegClass)
      return &AMDGPU::AV_128_Align2RegClass;
    if (RC == &AMDGPU::VReg_160RegClass || RC == &AMDGPU::AReg_160RegClass)
      return &AMDGPU::AV_160RegClass;
    if (RC == &AMDGPU::VReg_160_Align2RegClass ||
        RC == &AMDGPU::AReg_160_Align2RegClass)
      return &AMDGPU::AV_160_Align2RegClass;
    if (RC == &AMDGPU::VReg_192RegClass || RC == &AMDGPU::AReg_192RegClass)
      return &AMDGPU::AV_192RegClass;
    if (RC == &AMDGPU::VReg_192_Align2RegClass ||
        RC == &AMDGPU::AReg_192_Align2RegClass)
      return &AMDGPU::AV_192_Align2RegClass;
    if (RC == &AMDGPU::VReg_256RegClass || RC == &AMDGPU::AReg_256RegClass)
      return &AMDGPU::AV_256RegClass;
    if (RC == &AMDGPU::VReg_256_Align2RegClass ||
        RC == &AMDGPU::AReg_256_Align2RegClass)
      return &AMDGPU::AV_256_Align2RegClass;
    if (RC == &AMDGPU::VReg_512RegClass || RC == &AMDGPU::AReg_512RegClass)
      return &AMDGPU::AV_512RegClass;
    if (RC == &AMDGPU::VReg_512_Align2RegClass ||
        RC == &AMDGPU::AReg_512_Align2RegClass)
      return &AMDGPU::AV_512_Align2RegClass;
    if (RC == &AMDGPU::VReg_1024RegClass || RC == &AMDGPU::AReg_1024RegClass)
      return &AMDGPU::AV_1024RegClass;
    if (RC == &AMDGPU::VReg_1024_Align2RegClass ||
        RC == &AMDGPU::AReg_1024_Align2RegClass)
      return &AMDGPU::AV_1024_Align2RegClass;
  }

  return TargetRegisterInfo::getLargestLegalSuperClass(RC, MF);
}

Register SIRegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  const SIFrameLowering *TFI = ST.getFrameLowering();
  const SIMachineFunctionInfo *FuncInfo = MF.getInfo<SIMachineFunctionInfo>();
  // During ISel lowering we always reserve the stack pointer in entry and chain
  // functions, but never actually want to reference it when accessing our own
  // frame. If we need a frame pointer we use it, but otherwise we can just use
  // an immediate "0" which we represent by returning NoRegister.
  if (FuncInfo->isBottomOfStack()) {
    return TFI->hasFP(MF) ? FuncInfo->getFrameOffsetReg() : Register();
  }
  return TFI->hasFP(MF) ? FuncInfo->getFrameOffsetReg()
                        : FuncInfo->getStackPtrOffsetReg();
}

bool SIRegisterInfo::hasBasePointer(const MachineFunction &MF) const {
  // When we need stack realignment, we can't reference off of the
  // stack pointer, so we reserve a base pointer.
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MFI.getNumFixedObjects() && shouldRealignStack(MF);
}

Register SIRegisterInfo::getBaseRegister() const { return AMDGPU::SGPR34; }

const uint32_t *SIRegisterInfo::getAllVGPRRegMask() const {
  return AMDGPU_AllVGPRs_RegMask;
}

const uint32_t *SIRegisterInfo::getAllAGPRRegMask() const {
  return AMDGPU_AllAGPRs_RegMask;
}

const uint32_t *SIRegisterInfo::getAllVectorRegMask() const {
  return AMDGPU_AllVectorRegs_RegMask;
}

const uint32_t *SIRegisterInfo::getAllAllocatableSRegMask() const {
  return AMDGPU_AllAllocatableSRegs_RegMask;
}

unsigned SIRegisterInfo::getSubRegFromChannel(unsigned Channel,
                                              unsigned NumRegs) {
  assert(NumRegs < SubRegFromChannelTableWidthMap.size());
  unsigned NumRegIndex = SubRegFromChannelTableWidthMap[NumRegs];
  assert(NumRegIndex && "Not implemented");
  assert(Channel < SubRegFromChannelTable[NumRegIndex - 1].size());
  return SubRegFromChannelTable[NumRegIndex - 1][Channel];
}

MCRegister
SIRegisterInfo::getAlignedHighSGPRForRC(const MachineFunction &MF,
                                        const unsigned Align,
                                        const TargetRegisterClass *RC) const {
  unsigned BaseIdx = alignDown(ST.getMaxNumSGPRs(MF), Align) - Align;
  MCRegister BaseReg(AMDGPU::SGPR_32RegClass.getRegister(BaseIdx));
  return getMatchingSuperReg(BaseReg, AMDGPU::sub0, RC);
}

MCRegister SIRegisterInfo::reservedPrivateSegmentBufferReg(
  const MachineFunction &MF) const {
  return getAlignedHighSGPRForRC(MF, /*Align=*/4, &AMDGPU::SGPR_128RegClass);
}

BitVector SIRegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  Reserved.set(AMDGPU::MODE);

  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();

  // Reserve special purpose registers.
  //
  // EXEC_LO and EXEC_HI could be allocated and used as regular register, but
  // this seems likely to result in bugs, so I'm marking them as reserved.
  reserveRegisterTuples(Reserved, AMDGPU::EXEC);
  reserveRegisterTuples(Reserved, AMDGPU::FLAT_SCR);

  // M0 has to be reserved so that llvm accepts it as a live-in into a block.
  reserveRegisterTuples(Reserved, AMDGPU::M0);

  // Reserve src_vccz, src_execz, src_scc.
  reserveRegisterTuples(Reserved, AMDGPU::SRC_VCCZ);
  reserveRegisterTuples(Reserved, AMDGPU::SRC_EXECZ);
  reserveRegisterTuples(Reserved, AMDGPU::SRC_SCC);

  // Reserve the memory aperture registers
  reserveRegisterTuples(Reserved, AMDGPU::SRC_SHARED_BASE);
  reserveRegisterTuples(Reserved, AMDGPU::SRC_SHARED_LIMIT);
  reserveRegisterTuples(Reserved, AMDGPU::SRC_PRIVATE_BASE);
  reserveRegisterTuples(Reserved, AMDGPU::SRC_PRIVATE_LIMIT);

  // Reserve src_pops_exiting_wave_id - support is not implemented in Codegen.
  reserveRegisterTuples(Reserved, AMDGPU::SRC_POPS_EXITING_WAVE_ID);

  // Reserve xnack_mask registers - support is not implemented in Codegen.
  reserveRegisterTuples(Reserved, AMDGPU::XNACK_MASK);

  // Reserve lds_direct register - support is not implemented in Codegen.
  reserveRegisterTuples(Reserved, AMDGPU::LDS_DIRECT);

  // Reserve Trap Handler registers - support is not implemented in Codegen.
  reserveRegisterTuples(Reserved, AMDGPU::TBA);
  reserveRegisterTuples(Reserved, AMDGPU::TMA);
  reserveRegisterTuples(Reserved, AMDGPU::TTMP0_TTMP1);
  reserveRegisterTuples(Reserved, AMDGPU::TTMP2_TTMP3);
  reserveRegisterTuples(Reserved, AMDGPU::TTMP4_TTMP5);
  reserveRegisterTuples(Reserved, AMDGPU::TTMP6_TTMP7);
  reserveRegisterTuples(Reserved, AMDGPU::TTMP8_TTMP9);
  reserveRegisterTuples(Reserved, AMDGPU::TTMP10_TTMP11);
  reserveRegisterTuples(Reserved, AMDGPU::TTMP12_TTMP13);
  reserveRegisterTuples(Reserved, AMDGPU::TTMP14_TTMP15);

  // Reserve null register - it shall never be allocated
  reserveRegisterTuples(Reserved, AMDGPU::SGPR_NULL64);

  // Reserve SGPRs.
  //
  unsigned MaxNumSGPRs = ST.getMaxNumSGPRs(MF);
  unsigned TotalNumSGPRs = AMDGPU::SGPR_32RegClass.getNumRegs();
  for (const TargetRegisterClass *RC : regclasses()) {
    if (RC->isBaseClass() && isSGPRClass(RC)) {
      unsigned NumRegs = divideCeil(getRegSizeInBits(*RC), 32);
      for (MCPhysReg Reg : *RC) {
        unsigned Index = getHWRegIndex(Reg);
        if (Index + NumRegs > MaxNumSGPRs && Index < TotalNumSGPRs)
          Reserved.set(Reg);
      }
    }
  }

  Register ScratchRSrcReg = MFI->getScratchRSrcReg();
  if (ScratchRSrcReg != AMDGPU::NoRegister) {
    // Reserve 4 SGPRs for the scratch buffer resource descriptor in case we
    // need to spill.
    // TODO: May need to reserve a VGPR if doing LDS spilling.
    reserveRegisterTuples(Reserved, ScratchRSrcReg);
  }

  Register LongBranchReservedReg = MFI->getLongBranchReservedReg();
  if (LongBranchReservedReg)
    reserveRegisterTuples(Reserved, LongBranchReservedReg);

  // We have to assume the SP is needed in case there are calls in the function,
  // which is detected after the function is lowered. If we aren't really going
  // to need SP, don't bother reserving it.
  MCRegister StackPtrReg = MFI->getStackPtrOffsetReg();
  if (StackPtrReg) {
    reserveRegisterTuples(Reserved, StackPtrReg);
    assert(!isSubRegister(ScratchRSrcReg, StackPtrReg));
  }

  MCRegister FrameReg = MFI->getFrameOffsetReg();
  if (FrameReg) {
    reserveRegisterTuples(Reserved, FrameReg);
    assert(!isSubRegister(ScratchRSrcReg, FrameReg));
  }

  if (hasBasePointer(MF)) {
    MCRegister BasePtrReg = getBaseRegister();
    reserveRegisterTuples(Reserved, BasePtrReg);
    assert(!isSubRegister(ScratchRSrcReg, BasePtrReg));
  }

  // FIXME: Use same reserved register introduced in D149775
  // SGPR used to preserve EXEC MASK around WWM spill/copy instructions.
  Register ExecCopyReg = MFI->getSGPRForEXECCopy();
  if (ExecCopyReg)
    reserveRegisterTuples(Reserved, ExecCopyReg);

  // Reserve VGPRs/AGPRs.
  //
  unsigned MaxNumVGPRs = ST.getMaxNumVGPRs(MF);
  unsigned MaxNumAGPRs = MaxNumVGPRs;
  unsigned TotalNumVGPRs = AMDGPU::VGPR_32RegClass.getNumRegs();

  // On GFX90A, the number of VGPRs and AGPRs need not be equal. Theoretically,
  // a wave may have up to 512 total vector registers combining together both
  // VGPRs and AGPRs. Hence, in an entry function without calls and without
  // AGPRs used within it, it is possible to use the whole vector register
  // budget for VGPRs.
  //
  // TODO: it shall be possible to estimate maximum AGPR/VGPR pressure and split
  //       register file accordingly.
  if (ST.hasGFX90AInsts()) {
    if (MFI->usesAGPRs(MF)) {
      MaxNumVGPRs /= 2;
      MaxNumAGPRs = MaxNumVGPRs;
    } else {
      if (MaxNumVGPRs > TotalNumVGPRs) {
        MaxNumAGPRs = MaxNumVGPRs - TotalNumVGPRs;
        MaxNumVGPRs = TotalNumVGPRs;
      } else
        MaxNumAGPRs = 0;
    }
  }

  for (const TargetRegisterClass *RC : regclasses()) {
    if (RC->isBaseClass() && isVGPRClass(RC)) {
      unsigned NumRegs = divideCeil(getRegSizeInBits(*RC), 32);
      for (MCPhysReg Reg : *RC) {
        unsigned Index = getHWRegIndex(Reg);
        if (Index + NumRegs > MaxNumVGPRs)
          Reserved.set(Reg);
      }
    }
  }

  // Reserve all the AGPRs if there are no instructions to use it.
  if (!ST.hasMAIInsts())
    MaxNumAGPRs = 0;
  for (const TargetRegisterClass *RC : regclasses()) {
    if (RC->isBaseClass() && isAGPRClass(RC)) {
      unsigned NumRegs = divideCeil(getRegSizeInBits(*RC), 32);
      for (MCPhysReg Reg : *RC) {
        unsigned Index = getHWRegIndex(Reg);
        if (Index + NumRegs > MaxNumAGPRs)
          Reserved.set(Reg);
      }
    }
  }

  // On GFX908, in order to guarantee copying between AGPRs, we need a scratch
  // VGPR available at all times.
  if (ST.hasMAIInsts() && !ST.hasGFX90AInsts()) {
    reserveRegisterTuples(Reserved, MFI->getVGPRForAGPRCopy());
  }

  for (Register Reg : MFI->getWWMReservedRegs())
    reserveRegisterTuples(Reserved, Reg);

  // FIXME: Stop using reserved registers for this.
  for (MCPhysReg Reg : MFI->getAGPRSpillVGPRs())
    reserveRegisterTuples(Reserved, Reg);

  for (MCPhysReg Reg : MFI->getVGPRSpillAGPRs())
    reserveRegisterTuples(Reserved, Reg);

  return Reserved;
}

bool SIRegisterInfo::isAsmClobberable(const MachineFunction &MF,
                                      MCRegister PhysReg) const {
  return !MF.getRegInfo().isReserved(PhysReg);
}

bool SIRegisterInfo::shouldRealignStack(const MachineFunction &MF) const {
  const SIMachineFunctionInfo *Info = MF.getInfo<SIMachineFunctionInfo>();
  // On entry or in chain functions, the base address is 0, so it can't possibly
  // need any more alignment.

  // FIXME: Should be able to specify the entry frame alignment per calling
  // convention instead.
  if (Info->isBottomOfStack())
    return false;

  return TargetRegisterInfo::shouldRealignStack(MF);
}

bool SIRegisterInfo::requiresRegisterScavenging(const MachineFunction &Fn) const {
  const SIMachineFunctionInfo *Info = Fn.getInfo<SIMachineFunctionInfo>();
  if (Info->isEntryFunction()) {
    const MachineFrameInfo &MFI = Fn.getFrameInfo();
    return MFI.hasStackObjects() || MFI.hasCalls();
  }

  // May need scavenger for dealing with callee saved registers.
  return true;
}

bool SIRegisterInfo::requiresFrameIndexScavenging(
  const MachineFunction &MF) const {
  // Do not use frame virtual registers. They used to be used for SGPRs, but
  // once we reach PrologEpilogInserter, we can no longer spill SGPRs. If the
  // scavenger fails, we can increment/decrement the necessary SGPRs to avoid a
  // spill.
  return false;
}

bool SIRegisterInfo::requiresFrameIndexReplacementScavenging(
  const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MFI.hasStackObjects();
}

bool SIRegisterInfo::requiresVirtualBaseRegisters(
  const MachineFunction &) const {
  // There are no special dedicated stack or frame pointers.
  return true;
}

int64_t SIRegisterInfo::getScratchInstrOffset(const MachineInstr *MI) const {
  assert(SIInstrInfo::isMUBUF(*MI) || SIInstrInfo::isFLATScratch(*MI));

  int OffIdx = AMDGPU::getNamedOperandIdx(MI->getOpcode(),
                                          AMDGPU::OpName::offset);
  return MI->getOperand(OffIdx).getImm();
}

int64_t SIRegisterInfo::getFrameIndexInstrOffset(const MachineInstr *MI,
                                                 int Idx) const {
  if (!SIInstrInfo::isMUBUF(*MI) && !SIInstrInfo::isFLATScratch(*MI))
    return 0;

  assert((Idx == AMDGPU::getNamedOperandIdx(MI->getOpcode(),
                                            AMDGPU::OpName::vaddr) ||
         (Idx == AMDGPU::getNamedOperandIdx(MI->getOpcode(),
                                            AMDGPU::OpName::saddr))) &&
         "Should never see frame index on non-address operand");

  return getScratchInstrOffset(MI);
}

bool SIRegisterInfo::needsFrameBaseReg(MachineInstr *MI, int64_t Offset) const {
  if (!SIInstrInfo::isMUBUF(*MI) && !SIInstrInfo::isFLATScratch(*MI))
    return false;

  int64_t FullOffset = Offset + getScratchInstrOffset(MI);

  const SIInstrInfo *TII = ST.getInstrInfo();
  if (SIInstrInfo::isMUBUF(*MI))
    return !TII->isLegalMUBUFImmOffset(FullOffset);

  return !TII->isLegalFLATOffset(FullOffset, AMDGPUAS::PRIVATE_ADDRESS,
                                 SIInstrFlags::FlatScratch);
}

Register SIRegisterInfo::materializeFrameBaseRegister(MachineBasicBlock *MBB,
                                                      int FrameIdx,
                                                      int64_t Offset) const {
  MachineBasicBlock::iterator Ins = MBB->begin();
  DebugLoc DL; // Defaults to "unknown"

  if (Ins != MBB->end())
    DL = Ins->getDebugLoc();

  MachineFunction *MF = MBB->getParent();
  const SIInstrInfo *TII = ST.getInstrInfo();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  unsigned MovOpc = ST.enableFlatScratch() ? AMDGPU::S_MOV_B32
                                           : AMDGPU::V_MOV_B32_e32;

  Register BaseReg = MRI.createVirtualRegister(
      ST.enableFlatScratch() ? &AMDGPU::SReg_32_XEXEC_HIRegClass
                             : &AMDGPU::VGPR_32RegClass);

  if (Offset == 0) {
    BuildMI(*MBB, Ins, DL, TII->get(MovOpc), BaseReg)
      .addFrameIndex(FrameIdx);
    return BaseReg;
  }

  Register OffsetReg = MRI.createVirtualRegister(&AMDGPU::SReg_32_XM0RegClass);

  Register FIReg = MRI.createVirtualRegister(
      ST.enableFlatScratch() ? &AMDGPU::SReg_32_XM0RegClass
                             : &AMDGPU::VGPR_32RegClass);

  BuildMI(*MBB, Ins, DL, TII->get(AMDGPU::S_MOV_B32), OffsetReg)
    .addImm(Offset);
  BuildMI(*MBB, Ins, DL, TII->get(MovOpc), FIReg)
    .addFrameIndex(FrameIdx);

  if (ST.enableFlatScratch() ) {
    BuildMI(*MBB, Ins, DL, TII->get(AMDGPU::S_ADD_I32), BaseReg)
        .addReg(OffsetReg, RegState::Kill)
        .addReg(FIReg);
    return BaseReg;
  }

  TII->getAddNoCarry(*MBB, Ins, DL, BaseReg)
    .addReg(OffsetReg, RegState::Kill)
    .addReg(FIReg)
    .addImm(0); // clamp bit

  return BaseReg;
}

void SIRegisterInfo::resolveFrameIndex(MachineInstr &MI, Register BaseReg,
                                       int64_t Offset) const {
  const SIInstrInfo *TII = ST.getInstrInfo();
  bool IsFlat = TII->isFLATScratch(MI);

#ifndef NDEBUG
  // FIXME: Is it possible to be storing a frame index to itself?
  bool SeenFI = false;
  for (const MachineOperand &MO: MI.operands()) {
    if (MO.isFI()) {
      if (SeenFI)
        llvm_unreachable("should not see multiple frame indices");

      SeenFI = true;
    }
  }
#endif

  MachineOperand *FIOp =
      TII->getNamedOperand(MI, IsFlat ? AMDGPU::OpName::saddr
                                      : AMDGPU::OpName::vaddr);

  MachineOperand *OffsetOp = TII->getNamedOperand(MI, AMDGPU::OpName::offset);
  int64_t NewOffset = OffsetOp->getImm() + Offset;

  assert(FIOp && FIOp->isFI() && "frame index must be address operand");
  assert(TII->isMUBUF(MI) || TII->isFLATScratch(MI));

  if (IsFlat) {
    assert(TII->isLegalFLATOffset(NewOffset, AMDGPUAS::PRIVATE_ADDRESS,
                                  SIInstrFlags::FlatScratch) &&
           "offset should be legal");
    FIOp->ChangeToRegister(BaseReg, false);
    OffsetOp->setImm(NewOffset);
    return;
  }

#ifndef NDEBUG
  MachineOperand *SOffset = TII->getNamedOperand(MI, AMDGPU::OpName::soffset);
  assert(SOffset->isImm() && SOffset->getImm() == 0);
#endif

  assert(TII->isLegalMUBUFImmOffset(NewOffset) && "offset should be legal");

  FIOp->ChangeToRegister(BaseReg, false);
  OffsetOp->setImm(NewOffset);
}

bool SIRegisterInfo::isFrameOffsetLegal(const MachineInstr *MI,
                                        Register BaseReg,
                                        int64_t Offset) const {
  if (!SIInstrInfo::isMUBUF(*MI) && !SIInstrInfo::isFLATScratch(*MI))
    return false;

  int64_t NewOffset = Offset + getScratchInstrOffset(MI);

  const SIInstrInfo *TII = ST.getInstrInfo();
  if (SIInstrInfo::isMUBUF(*MI))
    return TII->isLegalMUBUFImmOffset(NewOffset);

  return TII->isLegalFLATOffset(NewOffset, AMDGPUAS::PRIVATE_ADDRESS,
                                SIInstrFlags::FlatScratch);
}

const TargetRegisterClass *SIRegisterInfo::getPointerRegClass(
  const MachineFunction &MF, unsigned Kind) const {
  // This is inaccurate. It depends on the instruction and address space. The
  // only place where we should hit this is for dealing with frame indexes /
  // private accesses, so this is correct in that case.
  return &AMDGPU::VGPR_32RegClass;
}

const TargetRegisterClass *
SIRegisterInfo::getCrossCopyRegClass(const TargetRegisterClass *RC) const {
  if (isAGPRClass(RC) && !ST.hasGFX90AInsts())
    return getEquivalentVGPRClass(RC);
  if (RC == &AMDGPU::SCC_CLASSRegClass)
    return getWaveMaskRegClass();

  return RC;
}

static unsigned getNumSubRegsForSpillOp(unsigned Op) {

  switch (Op) {
  case AMDGPU::SI_SPILL_S1024_SAVE:
  case AMDGPU::SI_SPILL_S1024_RESTORE:
  case AMDGPU::SI_SPILL_V1024_SAVE:
  case AMDGPU::SI_SPILL_V1024_RESTORE:
  case AMDGPU::SI_SPILL_A1024_SAVE:
  case AMDGPU::SI_SPILL_A1024_RESTORE:
  case AMDGPU::SI_SPILL_AV1024_SAVE:
  case AMDGPU::SI_SPILL_AV1024_RESTORE:
    return 32;
  case AMDGPU::SI_SPILL_S512_SAVE:
  case AMDGPU::SI_SPILL_S512_RESTORE:
  case AMDGPU::SI_SPILL_V512_SAVE:
  case AMDGPU::SI_SPILL_V512_RESTORE:
  case AMDGPU::SI_SPILL_A512_SAVE:
  case AMDGPU::SI_SPILL_A512_RESTORE:
  case AMDGPU::SI_SPILL_AV512_SAVE:
  case AMDGPU::SI_SPILL_AV512_RESTORE:
    return 16;
  case AMDGPU::SI_SPILL_S384_SAVE:
  case AMDGPU::SI_SPILL_S384_RESTORE:
  case AMDGPU::SI_SPILL_V384_SAVE:
  case AMDGPU::SI_SPILL_V384_RESTORE:
  case AMDGPU::SI_SPILL_A384_SAVE:
  case AMDGPU::SI_SPILL_A384_RESTORE:
  case AMDGPU::SI_SPILL_AV384_SAVE:
  case AMDGPU::SI_SPILL_AV384_RESTORE:
    return 12;
  case AMDGPU::SI_SPILL_S352_SAVE:
  case AMDGPU::SI_SPILL_S352_RESTORE:
  case AMDGPU::SI_SPILL_V352_SAVE:
  case AMDGPU::SI_SPILL_V352_RESTORE:
  case AMDGPU::SI_SPILL_A352_SAVE:
  case AMDGPU::SI_SPILL_A352_RESTORE:
  case AMDGPU::SI_SPILL_AV352_SAVE:
  case AMDGPU::SI_SPILL_AV352_RESTORE:
    return 11;
  case AMDGPU::SI_SPILL_S320_SAVE:
  case AMDGPU::SI_SPILL_S320_RESTORE:
  case AMDGPU::SI_SPILL_V320_SAVE:
  case AMDGPU::SI_SPILL_V320_RESTORE:
  case AMDGPU::SI_SPILL_A320_SAVE:
  case AMDGPU::SI_SPILL_A320_RESTORE:
  case AMDGPU::SI_SPILL_AV320_SAVE:
  case AMDGPU::SI_SPILL_AV320_RESTORE:
    return 10;
  case AMDGPU::SI_SPILL_S288_SAVE:
  case AMDGPU::SI_SPILL_S288_RESTORE:
  case AMDGPU::SI_SPILL_V288_SAVE:
  case AMDGPU::SI_SPILL_V288_RESTORE:
  case AMDGPU::SI_SPILL_A288_SAVE:
  case AMDGPU::SI_SPILL_A288_RESTORE:
  case AMDGPU::SI_SPILL_AV288_SAVE:
  case AMDGPU::SI_SPILL_AV288_RESTORE:
    return 9;
  case AMDGPU::SI_SPILL_S256_SAVE:
  case AMDGPU::SI_SPILL_S256_RESTORE:
  case AMDGPU::SI_SPILL_V256_SAVE:
  case AMDGPU::SI_SPILL_V256_RESTORE:
  case AMDGPU::SI_SPILL_A256_SAVE:
  case AMDGPU::SI_SPILL_A256_RESTORE:
  case AMDGPU::SI_SPILL_AV256_SAVE:
  case AMDGPU::SI_SPILL_AV256_RESTORE:
    return 8;
  case AMDGPU::SI_SPILL_S224_SAVE:
  case AMDGPU::SI_SPILL_S224_RESTORE:
  case AMDGPU::SI_SPILL_V224_SAVE:
  case AMDGPU::SI_SPILL_V224_RESTORE:
  case AMDGPU::SI_SPILL_A224_SAVE:
  case AMDGPU::SI_SPILL_A224_RESTORE:
  case AMDGPU::SI_SPILL_AV224_SAVE:
  case AMDGPU::SI_SPILL_AV224_RESTORE:
    return 7;
  case AMDGPU::SI_SPILL_S192_SAVE:
  case AMDGPU::SI_SPILL_S192_RESTORE:
  case AMDGPU::SI_SPILL_V192_SAVE:
  case AMDGPU::SI_SPILL_V192_RESTORE:
  case AMDGPU::SI_SPILL_A192_SAVE:
  case AMDGPU::SI_SPILL_A192_RESTORE:
  case AMDGPU::SI_SPILL_AV192_SAVE:
  case AMDGPU::SI_SPILL_AV192_RESTORE:
    return 6;
  case AMDGPU::SI_SPILL_S160_SAVE:
  case AMDGPU::SI_SPILL_S160_RESTORE:
  case AMDGPU::SI_SPILL_V160_SAVE:
  case AMDGPU::SI_SPILL_V160_RESTORE:
  case AMDGPU::SI_SPILL_A160_SAVE:
  case AMDGPU::SI_SPILL_A160_RESTORE:
  case AMDGPU::SI_SPILL_AV160_SAVE:
  case AMDGPU::SI_SPILL_AV160_RESTORE:
    return 5;
  case AMDGPU::SI_SPILL_S128_SAVE:
  case AMDGPU::SI_SPILL_S128_RESTORE:
  case AMDGPU::SI_SPILL_V128_SAVE:
  case AMDGPU::SI_SPILL_V128_RESTORE:
  case AMDGPU::SI_SPILL_A128_SAVE:
  case AMDGPU::SI_SPILL_A128_RESTORE:
  case AMDGPU::SI_SPILL_AV128_SAVE:
  case AMDGPU::SI_SPILL_AV128_RESTORE:
    return 4;
  case AMDGPU::SI_SPILL_S96_SAVE:
  case AMDGPU::SI_SPILL_S96_RESTORE:
  case AMDGPU::SI_SPILL_V96_SAVE:
  case AMDGPU::SI_SPILL_V96_RESTORE:
  case AMDGPU::SI_SPILL_A96_SAVE:
  case AMDGPU::SI_SPILL_A96_RESTORE:
  case AMDGPU::SI_SPILL_AV96_SAVE:
  case AMDGPU::SI_SPILL_AV96_RESTORE:
    return 3;
  case AMDGPU::SI_SPILL_S64_SAVE:
  case AMDGPU::SI_SPILL_S64_RESTORE:
  case AMDGPU::SI_SPILL_V64_SAVE:
  case AMDGPU::SI_SPILL_V64_RESTORE:
  case AMDGPU::SI_SPILL_A64_SAVE:
  case AMDGPU::SI_SPILL_A64_RESTORE:
  case AMDGPU::SI_SPILL_AV64_SAVE:
  case AMDGPU::SI_SPILL_AV64_RESTORE:
    return 2;
  case AMDGPU::SI_SPILL_S32_SAVE:
  case AMDGPU::SI_SPILL_S32_RESTORE:
  case AMDGPU::SI_SPILL_V32_SAVE:
  case AMDGPU::SI_SPILL_V32_RESTORE:
  case AMDGPU::SI_SPILL_A32_SAVE:
  case AMDGPU::SI_SPILL_A32_RESTORE:
  case AMDGPU::SI_SPILL_AV32_SAVE:
  case AMDGPU::SI_SPILL_AV32_RESTORE:
  case AMDGPU::SI_SPILL_WWM_V32_SAVE:
  case AMDGPU::SI_SPILL_WWM_V32_RESTORE:
  case AMDGPU::SI_SPILL_WWM_AV32_SAVE:
  case AMDGPU::SI_SPILL_WWM_AV32_RESTORE:
    return 1;
  default: llvm_unreachable("Invalid spill opcode");
  }
}

static int getOffsetMUBUFStore(unsigned Opc) {
  switch (Opc) {
  case AMDGPU::BUFFER_STORE_DWORD_OFFEN:
    return AMDGPU::BUFFER_STORE_DWORD_OFFSET;
  case AMDGPU::BUFFER_STORE_BYTE_OFFEN:
    return AMDGPU::BUFFER_STORE_BYTE_OFFSET;
  case AMDGPU::BUFFER_STORE_SHORT_OFFEN:
    return AMDGPU::BUFFER_STORE_SHORT_OFFSET;
  case AMDGPU::BUFFER_STORE_DWORDX2_OFFEN:
    return AMDGPU::BUFFER_STORE_DWORDX2_OFFSET;
  case AMDGPU::BUFFER_STORE_DWORDX3_OFFEN:
    return AMDGPU::BUFFER_STORE_DWORDX3_OFFSET;
  case AMDGPU::BUFFER_STORE_DWORDX4_OFFEN:
    return AMDGPU::BUFFER_STORE_DWORDX4_OFFSET;
  case AMDGPU::BUFFER_STORE_SHORT_D16_HI_OFFEN:
    return AMDGPU::BUFFER_STORE_SHORT_D16_HI_OFFSET;
  case AMDGPU::BUFFER_STORE_BYTE_D16_HI_OFFEN:
    return AMDGPU::BUFFER_STORE_BYTE_D16_HI_OFFSET;
  default:
    return -1;
  }
}

static int getOffsetMUBUFLoad(unsigned Opc) {
  switch (Opc) {
  case AMDGPU::BUFFER_LOAD_DWORD_OFFEN:
    return AMDGPU::BUFFER_LOAD_DWORD_OFFSET;
  case AMDGPU::BUFFER_LOAD_UBYTE_OFFEN:
    return AMDGPU::BUFFER_LOAD_UBYTE_OFFSET;
  case AMDGPU::BUFFER_LOAD_SBYTE_OFFEN:
    return AMDGPU::BUFFER_LOAD_SBYTE_OFFSET;
  case AMDGPU::BUFFER_LOAD_USHORT_OFFEN:
    return AMDGPU::BUFFER_LOAD_USHORT_OFFSET;
  case AMDGPU::BUFFER_LOAD_SSHORT_OFFEN:
    return AMDGPU::BUFFER_LOAD_SSHORT_OFFSET;
  case AMDGPU::BUFFER_LOAD_DWORDX2_OFFEN:
    return AMDGPU::BUFFER_LOAD_DWORDX2_OFFSET;
  case AMDGPU::BUFFER_LOAD_DWORDX3_OFFEN:
    return AMDGPU::BUFFER_LOAD_DWORDX3_OFFSET;
  case AMDGPU::BUFFER_LOAD_DWORDX4_OFFEN:
    return AMDGPU::BUFFER_LOAD_DWORDX4_OFFSET;
  case AMDGPU::BUFFER_LOAD_UBYTE_D16_OFFEN:
    return AMDGPU::BUFFER_LOAD_UBYTE_D16_OFFSET;
  case AMDGPU::BUFFER_LOAD_UBYTE_D16_HI_OFFEN:
    return AMDGPU::BUFFER_LOAD_UBYTE_D16_HI_OFFSET;
  case AMDGPU::BUFFER_LOAD_SBYTE_D16_OFFEN:
    return AMDGPU::BUFFER_LOAD_SBYTE_D16_OFFSET;
  case AMDGPU::BUFFER_LOAD_SBYTE_D16_HI_OFFEN:
    return AMDGPU::BUFFER_LOAD_SBYTE_D16_HI_OFFSET;
  case AMDGPU::BUFFER_LOAD_SHORT_D16_OFFEN:
    return AMDGPU::BUFFER_LOAD_SHORT_D16_OFFSET;
  case AMDGPU::BUFFER_LOAD_SHORT_D16_HI_OFFEN:
    return AMDGPU::BUFFER_LOAD_SHORT_D16_HI_OFFSET;
  default:
    return -1;
  }
}

static int getOffenMUBUFStore(unsigned Opc) {
  switch (Opc) {
  case AMDGPU::BUFFER_STORE_DWORD_OFFSET:
    return AMDGPU::BUFFER_STORE_DWORD_OFFEN;
  case AMDGPU::BUFFER_STORE_BYTE_OFFSET:
    return AMDGPU::BUFFER_STORE_BYTE_OFFEN;
  case AMDGPU::BUFFER_STORE_SHORT_OFFSET:
    return AMDGPU::BUFFER_STORE_SHORT_OFFEN;
  case AMDGPU::BUFFER_STORE_DWORDX2_OFFSET:
    return AMDGPU::BUFFER_STORE_DWORDX2_OFFEN;
  case AMDGPU::BUFFER_STORE_DWORDX3_OFFSET:
    return AMDGPU::BUFFER_STORE_DWORDX3_OFFEN;
  case AMDGPU::BUFFER_STORE_DWORDX4_OFFSET:
    return AMDGPU::BUFFER_STORE_DWORDX4_OFFEN;
  case AMDGPU::BUFFER_STORE_SHORT_D16_HI_OFFSET:
    return AMDGPU::BUFFER_STORE_SHORT_D16_HI_OFFEN;
  case AMDGPU::BUFFER_STORE_BYTE_D16_HI_OFFSET:
    return AMDGPU::BUFFER_STORE_BYTE_D16_HI_OFFEN;
  default:
    return -1;
  }
}

static int getOffenMUBUFLoad(unsigned Opc) {
  switch (Opc) {
  case AMDGPU::BUFFER_LOAD_DWORD_OFFSET:
    return AMDGPU::BUFFER_LOAD_DWORD_OFFEN;
  case AMDGPU::BUFFER_LOAD_UBYTE_OFFSET:
    return AMDGPU::BUFFER_LOAD_UBYTE_OFFEN;
  case AMDGPU::BUFFER_LOAD_SBYTE_OFFSET:
    return AMDGPU::BUFFER_LOAD_SBYTE_OFFEN;
  case AMDGPU::BUFFER_LOAD_USHORT_OFFSET:
    return AMDGPU::BUFFER_LOAD_USHORT_OFFEN;
  case AMDGPU::BUFFER_LOAD_SSHORT_OFFSET:
    return AMDGPU::BUFFER_LOAD_SSHORT_OFFEN;
  case AMDGPU::BUFFER_LOAD_DWORDX2_OFFSET:
    return AMDGPU::BUFFER_LOAD_DWORDX2_OFFEN;
  case AMDGPU::BUFFER_LOAD_DWORDX3_OFFSET:
    return AMDGPU::BUFFER_LOAD_DWORDX3_OFFEN;
  case AMDGPU::BUFFER_LOAD_DWORDX4_OFFSET:
    return AMDGPU::BUFFER_LOAD_DWORDX4_OFFEN;
  case AMDGPU::BUFFER_LOAD_UBYTE_D16_OFFSET:
    return AMDGPU::BUFFER_LOAD_UBYTE_D16_OFFEN;
  case AMDGPU::BUFFER_LOAD_UBYTE_D16_HI_OFFSET:
    return AMDGPU::BUFFER_LOAD_UBYTE_D16_HI_OFFEN;
  case AMDGPU::BUFFER_LOAD_SBYTE_D16_OFFSET:
    return AMDGPU::BUFFER_LOAD_SBYTE_D16_OFFEN;
  case AMDGPU::BUFFER_LOAD_SBYTE_D16_HI_OFFSET:
    return AMDGPU::BUFFER_LOAD_SBYTE_D16_HI_OFFEN;
  case AMDGPU::BUFFER_LOAD_SHORT_D16_OFFSET:
    return AMDGPU::BUFFER_LOAD_SHORT_D16_OFFEN;
  case AMDGPU::BUFFER_LOAD_SHORT_D16_HI_OFFSET:
    return AMDGPU::BUFFER_LOAD_SHORT_D16_HI_OFFEN;
  default:
    return -1;
  }
}

static MachineInstrBuilder spillVGPRtoAGPR(const GCNSubtarget &ST,
                                           MachineBasicBlock &MBB,
                                           MachineBasicBlock::iterator MI,
                                           int Index, unsigned Lane,
                                           unsigned ValueReg, bool IsKill) {
  MachineFunction *MF = MBB.getParent();
  SIMachineFunctionInfo *MFI = MF->getInfo<SIMachineFunctionInfo>();
  const SIInstrInfo *TII = ST.getInstrInfo();

  MCPhysReg Reg = MFI->getVGPRToAGPRSpill(Index, Lane);

  if (Reg == AMDGPU::NoRegister)
    return MachineInstrBuilder();

  bool IsStore = MI->mayStore();
  MachineRegisterInfo &MRI = MF->getRegInfo();
  auto *TRI = static_cast<const SIRegisterInfo*>(MRI.getTargetRegisterInfo());

  unsigned Dst = IsStore ? Reg : ValueReg;
  unsigned Src = IsStore ? ValueReg : Reg;
  bool IsVGPR = TRI->isVGPR(MRI, Reg);
  DebugLoc DL = MI->getDebugLoc();
  if (IsVGPR == TRI->isVGPR(MRI, ValueReg)) {
    // Spiller during regalloc may restore a spilled register to its superclass.
    // It could result in AGPR spills restored to VGPRs or the other way around,
    // making the src and dst with identical regclasses at this point. It just
    // needs a copy in such cases.
    auto CopyMIB = BuildMI(MBB, MI, DL, TII->get(AMDGPU::COPY), Dst)
                       .addReg(Src, getKillRegState(IsKill));
    CopyMIB->setAsmPrinterFlag(MachineInstr::ReloadReuse);
    return CopyMIB;
  }
  unsigned Opc = (IsStore ^ IsVGPR) ? AMDGPU::V_ACCVGPR_WRITE_B32_e64
                                    : AMDGPU::V_ACCVGPR_READ_B32_e64;

  auto MIB = BuildMI(MBB, MI, DL, TII->get(Opc), Dst)
                 .addReg(Src, getKillRegState(IsKill));
  MIB->setAsmPrinterFlag(MachineInstr::ReloadReuse);
  return MIB;
}

// This differs from buildSpillLoadStore by only scavenging a VGPR. It does not
// need to handle the case where an SGPR may need to be spilled while spilling.
static bool buildMUBUFOffsetLoadStore(const GCNSubtarget &ST,
                                      MachineFrameInfo &MFI,
                                      MachineBasicBlock::iterator MI,
                                      int Index,
                                      int64_t Offset) {
  const SIInstrInfo *TII = ST.getInstrInfo();
  MachineBasicBlock *MBB = MI->getParent();
  const DebugLoc &DL = MI->getDebugLoc();
  bool IsStore = MI->mayStore();

  unsigned Opc = MI->getOpcode();
  int LoadStoreOp = IsStore ?
    getOffsetMUBUFStore(Opc) : getOffsetMUBUFLoad(Opc);
  if (LoadStoreOp == -1)
    return false;

  const MachineOperand *Reg = TII->getNamedOperand(*MI, AMDGPU::OpName::vdata);
  if (spillVGPRtoAGPR(ST, *MBB, MI, Index, 0, Reg->getReg(), false).getInstr())
    return true;

  MachineInstrBuilder NewMI =
      BuildMI(*MBB, MI, DL, TII->get(LoadStoreOp))
          .add(*Reg)
          .add(*TII->getNamedOperand(*MI, AMDGPU::OpName::srsrc))
          .add(*TII->getNamedOperand(*MI, AMDGPU::OpName::soffset))
          .addImm(Offset)
          .addImm(0) // cpol
          .addImm(0) // swz
          .cloneMemRefs(*MI);

  const MachineOperand *VDataIn = TII->getNamedOperand(*MI,
                                                       AMDGPU::OpName::vdata_in);
  if (VDataIn)
    NewMI.add(*VDataIn);
  return true;
}

static unsigned getFlatScratchSpillOpcode(const SIInstrInfo *TII,
                                          unsigned LoadStoreOp,
                                          unsigned EltSize) {
  bool IsStore = TII->get(LoadStoreOp).mayStore();
  bool HasVAddr = AMDGPU::hasNamedOperand(LoadStoreOp, AMDGPU::OpName::vaddr);
  bool UseST =
      !HasVAddr && !AMDGPU::hasNamedOperand(LoadStoreOp, AMDGPU::OpName::saddr);

  switch (EltSize) {
  case 4:
    LoadStoreOp = IsStore ? AMDGPU::SCRATCH_STORE_DWORD_SADDR
                          : AMDGPU::SCRATCH_LOAD_DWORD_SADDR;
    break;
  case 8:
    LoadStoreOp = IsStore ? AMDGPU::SCRATCH_STORE_DWORDX2_SADDR
                          : AMDGPU::SCRATCH_LOAD_DWORDX2_SADDR;
    break;
  case 12:
    LoadStoreOp = IsStore ? AMDGPU::SCRATCH_STORE_DWORDX3_SADDR
                          : AMDGPU::SCRATCH_LOAD_DWORDX3_SADDR;
    break;
  case 16:
    LoadStoreOp = IsStore ? AMDGPU::SCRATCH_STORE_DWORDX4_SADDR
                          : AMDGPU::SCRATCH_LOAD_DWORDX4_SADDR;
    break;
  default:
    llvm_unreachable("Unexpected spill load/store size!");
  }

  if (HasVAddr)
    LoadStoreOp = AMDGPU::getFlatScratchInstSVfromSS(LoadStoreOp);
  else if (UseST)
    LoadStoreOp = AMDGPU::getFlatScratchInstSTfromSS(LoadStoreOp);

  return LoadStoreOp;
}

void SIRegisterInfo::buildSpillLoadStore(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI, const DebugLoc &DL,
    unsigned LoadStoreOp, int Index, Register ValueReg, bool IsKill,
    MCRegister ScratchOffsetReg, int64_t InstOffset, MachineMemOperand *MMO,
    RegScavenger *RS, LiveRegUnits *LiveUnits) const {
  assert((!RS || !LiveUnits) && "Only RS or LiveUnits can be set but not both");

  MachineFunction *MF = MBB.getParent();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const MachineFrameInfo &MFI = MF->getFrameInfo();
  const SIMachineFunctionInfo *FuncInfo = MF->getInfo<SIMachineFunctionInfo>();

  const MCInstrDesc *Desc = &TII->get(LoadStoreOp);
  bool IsStore = Desc->mayStore();
  bool IsFlat = TII->isFLATScratch(LoadStoreOp);

  bool CanClobberSCC = false;
  bool Scavenged = false;
  MCRegister SOffset = ScratchOffsetReg;

  const TargetRegisterClass *RC = getRegClassForReg(MF->getRegInfo(), ValueReg);
  // On gfx90a+ AGPR is a regular VGPR acceptable for loads and stores.
  const bool IsAGPR = !ST.hasGFX90AInsts() && isAGPRClass(RC);
  const unsigned RegWidth = AMDGPU::getRegBitWidth(*RC) / 8;

  // Always use 4 byte operations for AGPRs because we need to scavenge
  // a temporary VGPR.
  unsigned EltSize = (IsFlat && !IsAGPR) ? std::min(RegWidth, 16u) : 4u;
  unsigned NumSubRegs = RegWidth / EltSize;
  unsigned Size = NumSubRegs * EltSize;
  unsigned RemSize = RegWidth - Size;
  unsigned NumRemSubRegs = RemSize ? 1 : 0;
  int64_t Offset = InstOffset + MFI.getObjectOffset(Index);
  int64_t MaterializedOffset = Offset;

  int64_t MaxOffset = Offset + Size + RemSize - EltSize;
  int64_t ScratchOffsetRegDelta = 0;

  if (IsFlat && EltSize > 4) {
    LoadStoreOp = getFlatScratchSpillOpcode(TII, LoadStoreOp, EltSize);
    Desc = &TII->get(LoadStoreOp);
  }

  Align Alignment = MFI.getObjectAlign(Index);
  const MachinePointerInfo &BasePtrInfo = MMO->getPointerInfo();

  assert((IsFlat || ((Offset % EltSize) == 0)) &&
         "unexpected VGPR spill offset");

  // Track a VGPR to use for a constant offset we need to materialize.
  Register TmpOffsetVGPR;

  // Track a VGPR to use as an intermediate value.
  Register TmpIntermediateVGPR;
  bool UseVGPROffset = false;

  // Materialize a VGPR offset required for the given SGPR/VGPR/Immediate
  // combination.
  auto MaterializeVOffset = [&](Register SGPRBase, Register TmpVGPR,
                                int64_t VOffset) {
    // We are using a VGPR offset
    if (IsFlat && SGPRBase) {
      // We only have 1 VGPR offset, or 1 SGPR offset. We don't have a free
      // SGPR, so perform the add as vector.
      // We don't need a base SGPR in the kernel.

      if (ST.getConstantBusLimit(AMDGPU::V_ADD_U32_e64) >= 2) {
        BuildMI(MBB, MI, DL, TII->get(AMDGPU::V_ADD_U32_e64), TmpVGPR)
          .addReg(SGPRBase)
          .addImm(VOffset)
          .addImm(0); // clamp
      } else {
        BuildMI(MBB, MI, DL, TII->get(AMDGPU::V_MOV_B32_e32), TmpVGPR)
          .addReg(SGPRBase);
        BuildMI(MBB, MI, DL, TII->get(AMDGPU::V_ADD_U32_e32), TmpVGPR)
          .addImm(VOffset)
          .addReg(TmpOffsetVGPR);
      }
    } else {
      assert(TmpOffsetVGPR);
      BuildMI(MBB, MI, DL, TII->get(AMDGPU::V_MOV_B32_e32), TmpVGPR)
        .addImm(VOffset);
    }
  };

  bool IsOffsetLegal =
      IsFlat ? TII->isLegalFLATOffset(MaxOffset, AMDGPUAS::PRIVATE_ADDRESS,
                                      SIInstrFlags::FlatScratch)
             : TII->isLegalMUBUFImmOffset(MaxOffset);
  if (!IsOffsetLegal || (IsFlat && !SOffset && !ST.hasFlatScratchSTMode())) {
    SOffset = MCRegister();

    // We don't have access to the register scavenger if this function is called
    // during  PEI::scavengeFrameVirtualRegs() so use LiveUnits in this case.
    // TODO: Clobbering SCC is not necessary for scratch instructions in the
    // entry.
    if (RS) {
      SOffset = RS->scavengeRegisterBackwards(AMDGPU::SGPR_32RegClass, MI, false, 0, false);

      // Piggy back on the liveness scan we just did see if SCC is dead.
      CanClobberSCC = !RS->isRegUsed(AMDGPU::SCC);
    } else if (LiveUnits) {
      CanClobberSCC = LiveUnits->available(AMDGPU::SCC);
      for (MCRegister Reg : AMDGPU::SGPR_32RegClass) {
        if (LiveUnits->available(Reg) && !MF->getRegInfo().isReserved(Reg)) {
          SOffset = Reg;
          break;
        }
      }
    }

    if (ScratchOffsetReg != AMDGPU::NoRegister && !CanClobberSCC)
      SOffset = Register();

    if (!SOffset) {
      UseVGPROffset = true;

      if (RS) {
        TmpOffsetVGPR = RS->scavengeRegisterBackwards(AMDGPU::VGPR_32RegClass, MI, false, 0);
      } else {
        assert(LiveUnits);
        for (MCRegister Reg : AMDGPU::VGPR_32RegClass) {
          if (LiveUnits->available(Reg) && !MF->getRegInfo().isReserved(Reg)) {
            TmpOffsetVGPR = Reg;
            break;
          }
        }
      }

      assert(TmpOffsetVGPR);
    } else if (!SOffset && CanClobberSCC) {
      // There are no free SGPRs, and since we are in the process of spilling
      // VGPRs too.  Since we need a VGPR in order to spill SGPRs (this is true
      // on SI/CI and on VI it is true until we implement spilling using scalar
      // stores), we have no way to free up an SGPR.  Our solution here is to
      // add the offset directly to the ScratchOffset or StackPtrOffset
      // register, and then subtract the offset after the spill to return the
      // register to it's original value.

      // TODO: If we don't have to do an emergency stack slot spill, converting
      // to use the VGPR offset is fewer instructions.
      if (!ScratchOffsetReg)
        ScratchOffsetReg = FuncInfo->getStackPtrOffsetReg();
      SOffset = ScratchOffsetReg;
      ScratchOffsetRegDelta = Offset;
    } else {
      Scavenged = true;
    }

    // We currently only support spilling VGPRs to EltSize boundaries, meaning
    // we can simplify the adjustment of Offset here to just scale with
    // WavefrontSize.
    if (!IsFlat && !UseVGPROffset)
      Offset *= ST.getWavefrontSize();

    if (!UseVGPROffset && !SOffset)
      report_fatal_error("could not scavenge SGPR to spill in entry function");

    if (UseVGPROffset) {
      // We are using a VGPR offset
      MaterializeVOffset(ScratchOffsetReg, TmpOffsetVGPR, Offset);
    } else if (ScratchOffsetReg == AMDGPU::NoRegister) {
      BuildMI(MBB, MI, DL, TII->get(AMDGPU::S_MOV_B32), SOffset).addImm(Offset);
    } else {
      assert(Offset != 0);
      auto Add = BuildMI(MBB, MI, DL, TII->get(AMDGPU::S_ADD_I32), SOffset)
          .addReg(ScratchOffsetReg)
          .addImm(Offset);
      Add->getOperand(3).setIsDead(); // Mark SCC as dead.
    }

    Offset = 0;
  }

  if (IsFlat && SOffset == AMDGPU::NoRegister) {
    assert(AMDGPU::getNamedOperandIdx(LoadStoreOp, AMDGPU::OpName::vaddr) < 0
           && "Unexpected vaddr for flat scratch with a FI operand");

    if (UseVGPROffset) {
      LoadStoreOp = AMDGPU::getFlatScratchInstSVfromSS(LoadStoreOp);
    } else {
      assert(ST.hasFlatScratchSTMode());
      LoadStoreOp = AMDGPU::getFlatScratchInstSTfromSS(LoadStoreOp);
    }

    Desc = &TII->get(LoadStoreOp);
  }

  for (unsigned i = 0, e = NumSubRegs + NumRemSubRegs, RegOffset = 0; i != e;
       ++i, RegOffset += EltSize) {
    if (i == NumSubRegs) {
      EltSize = RemSize;
      LoadStoreOp = getFlatScratchSpillOpcode(TII, LoadStoreOp, EltSize);
    }
    Desc = &TII->get(LoadStoreOp);

    if (!IsFlat && UseVGPROffset) {
      int NewLoadStoreOp = IsStore ? getOffenMUBUFStore(LoadStoreOp)
                                   : getOffenMUBUFLoad(LoadStoreOp);
      Desc = &TII->get(NewLoadStoreOp);
    }

    if (UseVGPROffset && TmpOffsetVGPR == TmpIntermediateVGPR) {
      // If we are spilling an AGPR beyond the range of the memory instruction
      // offset and need to use a VGPR offset, we ideally have at least 2
      // scratch VGPRs. If we don't have a second free VGPR without spilling,
      // recycle the VGPR used for the offset which requires resetting after
      // each subregister.

      MaterializeVOffset(ScratchOffsetReg, TmpOffsetVGPR, MaterializedOffset);
    }

    unsigned NumRegs = EltSize / 4;
    Register SubReg = e == 1
            ? ValueReg
            : Register(getSubReg(ValueReg,
                                 getSubRegFromChannel(RegOffset / 4, NumRegs)));

    unsigned SOffsetRegState = 0;
    unsigned SrcDstRegState = getDefRegState(!IsStore);
    const bool IsLastSubReg = i + 1 == e;
    const bool IsFirstSubReg = i == 0;
    if (IsLastSubReg) {
      SOffsetRegState |= getKillRegState(Scavenged);
      // The last implicit use carries the "Kill" flag.
      SrcDstRegState |= getKillRegState(IsKill);
    }

    // Make sure the whole register is defined if there are undef components by
    // adding an implicit def of the super-reg on the first instruction.
    bool NeedSuperRegDef = e > 1 && IsStore && IsFirstSubReg;
    bool NeedSuperRegImpOperand = e > 1;

    // Remaining element size to spill into memory after some parts of it
    // spilled into either AGPRs or VGPRs.
    unsigned RemEltSize = EltSize;

    // AGPRs to spill VGPRs and vice versa are allocated in a reverse order,
    // starting from the last lane. In case if a register cannot be completely
    // spilled into another register that will ensure its alignment does not
    // change. For targets with VGPR alignment requirement this is important
    // in case of flat scratch usage as we might get a scratch_load or
    // scratch_store of an unaligned register otherwise.
    for (int LaneS = (RegOffset + EltSize) / 4 - 1, Lane = LaneS,
             LaneE = RegOffset / 4;
         Lane >= LaneE; --Lane) {
      bool IsSubReg = e > 1 || EltSize > 4;
      Register Sub = IsSubReg
             ? Register(getSubReg(ValueReg, getSubRegFromChannel(Lane)))
             : ValueReg;
      auto MIB = spillVGPRtoAGPR(ST, MBB, MI, Index, Lane, Sub, IsKill);
      if (!MIB.getInstr())
        break;
      if (NeedSuperRegDef || (IsSubReg && IsStore && Lane == LaneS && IsFirstSubReg)) {
        MIB.addReg(ValueReg, RegState::ImplicitDefine);
        NeedSuperRegDef = false;
      }
      if ((IsSubReg || NeedSuperRegImpOperand) && (IsFirstSubReg || IsLastSubReg)) {
        NeedSuperRegImpOperand = true;
        unsigned State = SrcDstRegState;
        if (!IsLastSubReg || (Lane != LaneE))
          State &= ~RegState::Kill;
        if (!IsFirstSubReg || (Lane != LaneS))
          State &= ~RegState::Define;
        MIB.addReg(ValueReg, RegState::Implicit | State);
      }
      RemEltSize -= 4;
    }

    if (!RemEltSize) // Fully spilled into AGPRs.
      continue;

    if (RemEltSize != EltSize) { // Partially spilled to AGPRs
      assert(IsFlat && EltSize > 4);

      unsigned NumRegs = RemEltSize / 4;
      SubReg = Register(getSubReg(ValueReg,
                        getSubRegFromChannel(RegOffset / 4, NumRegs)));
      unsigned Opc = getFlatScratchSpillOpcode(TII, LoadStoreOp, RemEltSize);
      Desc = &TII->get(Opc);
    }

    unsigned FinalReg = SubReg;

    if (IsAGPR) {
      assert(EltSize == 4);

      if (!TmpIntermediateVGPR) {
        TmpIntermediateVGPR = FuncInfo->getVGPRForAGPRCopy();
        assert(MF->getRegInfo().isReserved(TmpIntermediateVGPR));
      }
      if (IsStore) {
        auto AccRead = BuildMI(MBB, MI, DL,
                               TII->get(AMDGPU::V_ACCVGPR_READ_B32_e64),
                               TmpIntermediateVGPR)
                           .addReg(SubReg, getKillRegState(IsKill));
        if (NeedSuperRegDef)
          AccRead.addReg(ValueReg, RegState::ImplicitDefine);
        AccRead->setAsmPrinterFlag(MachineInstr::ReloadReuse);
      }
      SubReg = TmpIntermediateVGPR;
    } else if (UseVGPROffset) {
      if (!TmpOffsetVGPR) {
        TmpOffsetVGPR = RS->scavengeRegisterBackwards(AMDGPU::VGPR_32RegClass,
                                                      MI, false, 0);
        RS->setRegUsed(TmpOffsetVGPR);
      }
    }

    MachinePointerInfo PInfo = BasePtrInfo.getWithOffset(RegOffset);
    MachineMemOperand *NewMMO =
        MF->getMachineMemOperand(PInfo, MMO->getFlags(), RemEltSize,
                                 commonAlignment(Alignment, RegOffset));

    auto MIB =
        BuildMI(MBB, MI, DL, *Desc)
            .addReg(SubReg, getDefRegState(!IsStore) | getKillRegState(IsKill));

    if (UseVGPROffset) {
      // For an AGPR spill, we reuse the same temp VGPR for the offset and the
      // intermediate accvgpr_write.
      MIB.addReg(TmpOffsetVGPR, getKillRegState(IsLastSubReg && !IsAGPR));
    }

    if (!IsFlat)
      MIB.addReg(FuncInfo->getScratchRSrcReg());

    if (SOffset == AMDGPU::NoRegister) {
      if (!IsFlat) {
        if (UseVGPROffset && ScratchOffsetReg) {
          MIB.addReg(ScratchOffsetReg);
        } else {
          assert(FuncInfo->isBottomOfStack());
          MIB.addImm(0);
        }
      }
    } else {
      MIB.addReg(SOffset, SOffsetRegState);
    }

    MIB.addImm(Offset + RegOffset);

    bool LastUse = MMO->getFlags() & MOLastUse;
    MIB.addImm(LastUse ? AMDGPU::CPol::TH_LU : 0); // cpol

    if (!IsFlat)
      MIB.addImm(0); // swz
    MIB.addMemOperand(NewMMO);

    if (!IsAGPR && NeedSuperRegDef)
      MIB.addReg(ValueReg, RegState::ImplicitDefine);

    if (!IsStore && IsAGPR && TmpIntermediateVGPR != AMDGPU::NoRegister) {
      MIB = BuildMI(MBB, MI, DL, TII->get(AMDGPU::V_ACCVGPR_WRITE_B32_e64),
                    FinalReg)
                .addReg(TmpIntermediateVGPR, RegState::Kill);
      MIB->setAsmPrinterFlag(MachineInstr::ReloadReuse);
    }

    if (NeedSuperRegImpOperand && (IsFirstSubReg || IsLastSubReg))
      MIB.addReg(ValueReg, RegState::Implicit | SrcDstRegState);

    // The epilog restore of a wwm-scratch register can cause undesired
    // optimization during machine-cp post PrologEpilogInserter if the same
    // register was assigned for return value ABI lowering with a COPY
    // instruction. As given below, with the epilog reload, the earlier COPY
    // appeared to be dead during machine-cp.
    // ...
    // v0 in WWM operation, needs the WWM spill at prolog/epilog.
    // $vgpr0 = V_WRITELANE_B32 $sgpr20, 0, $vgpr0
    // ...
    // Epilog block:
    // $vgpr0 = COPY $vgpr1 // outgoing value moved to v0
    // ...
    // WWM spill restore to preserve the inactive lanes of v0.
    // $sgpr4_sgpr5 = S_XOR_SAVEEXEC_B64 -1
    // $vgpr0 = BUFFER_LOAD $sgpr0_sgpr1_sgpr2_sgpr3, $sgpr32, 0, 0, 0
    // $exec = S_MOV_B64 killed $sgpr4_sgpr5
    // ...
    // SI_RETURN implicit $vgpr0
    // ...
    // To fix it, mark the same reg as a tied op for such restore instructions
    // so that it marks a usage for the preceding COPY.
    if (!IsStore && MI != MBB.end() && MI->isReturn() &&
        MI->readsRegister(SubReg, this)) {
      MIB.addReg(SubReg, RegState::Implicit);
      MIB->tieOperands(0, MIB->getNumOperands() - 1);
    }
  }

  if (ScratchOffsetRegDelta != 0) {
    // Subtract the offset we added to the ScratchOffset register.
    BuildMI(MBB, MI, DL, TII->get(AMDGPU::S_ADD_I32), SOffset)
        .addReg(SOffset)
        .addImm(-ScratchOffsetRegDelta);
  }
}

void SIRegisterInfo::buildVGPRSpillLoadStore(SGPRSpillBuilder &SB, int Index,
                                             int Offset, bool IsLoad,
                                             bool IsKill) const {
  // Load/store VGPR
  MachineFrameInfo &FrameInfo = SB.MF.getFrameInfo();
  assert(FrameInfo.getStackID(Index) != TargetStackID::SGPRSpill);

  Register FrameReg =
      FrameInfo.isFixedObjectIndex(Index) && hasBasePointer(SB.MF)
          ? getBaseRegister()
          : getFrameRegister(SB.MF);

  Align Alignment = FrameInfo.getObjectAlign(Index);
  MachinePointerInfo PtrInfo = MachinePointerInfo::getFixedStack(SB.MF, Index);
  MachineMemOperand *MMO = SB.MF.getMachineMemOperand(
      PtrInfo, IsLoad ? MachineMemOperand::MOLoad : MachineMemOperand::MOStore,
      SB.EltSize, Alignment);

  if (IsLoad) {
    unsigned Opc = ST.enableFlatScratch() ? AMDGPU::SCRATCH_LOAD_DWORD_SADDR
                                          : AMDGPU::BUFFER_LOAD_DWORD_OFFSET;
    buildSpillLoadStore(*SB.MBB, SB.MI, SB.DL, Opc, Index, SB.TmpVGPR, false,
                        FrameReg, (int64_t)Offset * SB.EltSize, MMO, SB.RS);
  } else {
    unsigned Opc = ST.enableFlatScratch() ? AMDGPU::SCRATCH_STORE_DWORD_SADDR
                                          : AMDGPU::BUFFER_STORE_DWORD_OFFSET;
    buildSpillLoadStore(*SB.MBB, SB.MI, SB.DL, Opc, Index, SB.TmpVGPR, IsKill,
                        FrameReg, (int64_t)Offset * SB.EltSize, MMO, SB.RS);
    // This only ever adds one VGPR spill
    SB.MFI.addToSpilledVGPRs(1);
  }
}

bool SIRegisterInfo::spillSGPR(MachineBasicBlock::iterator MI, int Index,
                               RegScavenger *RS, SlotIndexes *Indexes,
                               LiveIntervals *LIS, bool OnlyToVGPR,
                               bool SpillToPhysVGPRLane) const {
  SGPRSpillBuilder SB(*this, *ST.getInstrInfo(), isWave32, MI, Index, RS);

  ArrayRef<SpilledReg> VGPRSpills =
      SpillToPhysVGPRLane ? SB.MFI.getSGPRSpillToPhysicalVGPRLanes(Index)
                          : SB.MFI.getSGPRSpillToVirtualVGPRLanes(Index);
  bool SpillToVGPR = !VGPRSpills.empty();
  if (OnlyToVGPR && !SpillToVGPR)
    return false;

  assert(SpillToVGPR || (SB.SuperReg != SB.MFI.getStackPtrOffsetReg() &&
                         SB.SuperReg != SB.MFI.getFrameOffsetReg()));

  if (SpillToVGPR) {

    assert(SB.NumSubRegs == VGPRSpills.size() &&
           "Num of VGPR lanes should be equal to num of SGPRs spilled");

    for (unsigned i = 0, e = SB.NumSubRegs; i < e; ++i) {
      Register SubReg =
          SB.NumSubRegs == 1
              ? SB.SuperReg
              : Register(getSubReg(SB.SuperReg, SB.SplitParts[i]));
      SpilledReg Spill = VGPRSpills[i];

      bool IsFirstSubreg = i == 0;
      bool IsLastSubreg = i == SB.NumSubRegs - 1;
      bool UseKill = SB.IsKill && IsLastSubreg;


      // Mark the "old value of vgpr" input undef only if this is the first sgpr
      // spill to this specific vgpr in the first basic block.
      auto MIB = BuildMI(*SB.MBB, MI, SB.DL,
                         SB.TII.get(AMDGPU::SI_SPILL_S32_TO_VGPR), Spill.VGPR)
                     .addReg(SubReg, getKillRegState(UseKill))
                     .addImm(Spill.Lane)
                     .addReg(Spill.VGPR);
      if (Indexes) {
        if (IsFirstSubreg)
          Indexes->replaceMachineInstrInMaps(*MI, *MIB);
        else
          Indexes->insertMachineInstrInMaps(*MIB);
      }

      if (IsFirstSubreg && SB.NumSubRegs > 1) {
        // We may be spilling a super-register which is only partially defined,
        // and need to ensure later spills think the value is defined.
        MIB.addReg(SB.SuperReg, RegState::ImplicitDefine);
      }

      if (SB.NumSubRegs > 1 && (IsFirstSubreg || IsLastSubreg))
        MIB.addReg(SB.SuperReg, getKillRegState(UseKill) | RegState::Implicit);

      // FIXME: Since this spills to another register instead of an actual
      // frame index, we should delete the frame index when all references to
      // it are fixed.
    }
  } else {
    SB.prepare();

    // SubReg carries the "Kill" flag when SubReg == SB.SuperReg.
    unsigned SubKillState = getKillRegState((SB.NumSubRegs == 1) && SB.IsKill);

    // Per VGPR helper data
    auto PVD = SB.getPerVGPRData();

    for (unsigned Offset = 0; Offset < PVD.NumVGPRs; ++Offset) {
      unsigned TmpVGPRFlags = RegState::Undef;

      // Write sub registers into the VGPR
      for (unsigned i = Offset * PVD.PerVGPR,
                    e = std::min((Offset + 1) * PVD.PerVGPR, SB.NumSubRegs);
           i < e; ++i) {
        Register SubReg =
            SB.NumSubRegs == 1
                ? SB.SuperReg
                : Register(getSubReg(SB.SuperReg, SB.SplitParts[i]));

        MachineInstrBuilder WriteLane =
            BuildMI(*SB.MBB, MI, SB.DL,
                    SB.TII.get(AMDGPU::SI_SPILL_S32_TO_VGPR), SB.TmpVGPR)
                .addReg(SubReg, SubKillState)
                .addImm(i % PVD.PerVGPR)
                .addReg(SB.TmpVGPR, TmpVGPRFlags);
        TmpVGPRFlags = 0;

        if (Indexes) {
          if (i == 0)
            Indexes->replaceMachineInstrInMaps(*MI, *WriteLane);
          else
            Indexes->insertMachineInstrInMaps(*WriteLane);
        }

        // There could be undef components of a spilled super register.
        // TODO: Can we detect this and skip the spill?
        if (SB.NumSubRegs > 1) {
          // The last implicit use of the SB.SuperReg carries the "Kill" flag.
          unsigned SuperKillState = 0;
          if (i + 1 == SB.NumSubRegs)
            SuperKillState |= getKillRegState(SB.IsKill);
          WriteLane.addReg(SB.SuperReg, RegState::Implicit | SuperKillState);
        }
      }

      // Write out VGPR
      SB.readWriteTmpVGPR(Offset, /*IsLoad*/ false);
    }

    SB.restore();
  }

  MI->eraseFromParent();
  SB.MFI.addToSpilledSGPRs(SB.NumSubRegs);

  if (LIS)
    LIS->removeAllRegUnitsForPhysReg(SB.SuperReg);

  return true;
}

bool SIRegisterInfo::restoreSGPR(MachineBasicBlock::iterator MI, int Index,
                                 RegScavenger *RS, SlotIndexes *Indexes,
                                 LiveIntervals *LIS, bool OnlyToVGPR,
                                 bool SpillToPhysVGPRLane) const {
  SGPRSpillBuilder SB(*this, *ST.getInstrInfo(), isWave32, MI, Index, RS);

  ArrayRef<SpilledReg> VGPRSpills =
      SpillToPhysVGPRLane ? SB.MFI.getSGPRSpillToPhysicalVGPRLanes(Index)
                          : SB.MFI.getSGPRSpillToVirtualVGPRLanes(Index);
  bool SpillToVGPR = !VGPRSpills.empty();
  if (OnlyToVGPR && !SpillToVGPR)
    return false;

  if (SpillToVGPR) {
    for (unsigned i = 0, e = SB.NumSubRegs; i < e; ++i) {
      Register SubReg =
          SB.NumSubRegs == 1
              ? SB.SuperReg
              : Register(getSubReg(SB.SuperReg, SB.SplitParts[i]));

      SpilledReg Spill = VGPRSpills[i];
      auto MIB = BuildMI(*SB.MBB, MI, SB.DL,
                         SB.TII.get(AMDGPU::SI_RESTORE_S32_FROM_VGPR), SubReg)
                     .addReg(Spill.VGPR)
                     .addImm(Spill.Lane);
      if (SB.NumSubRegs > 1 && i == 0)
        MIB.addReg(SB.SuperReg, RegState::ImplicitDefine);
      if (Indexes) {
        if (i == e - 1)
          Indexes->replaceMachineInstrInMaps(*MI, *MIB);
        else
          Indexes->insertMachineInstrInMaps(*MIB);
      }
    }
  } else {
    SB.prepare();

    // Per VGPR helper data
    auto PVD = SB.getPerVGPRData();

    for (unsigned Offset = 0; Offset < PVD.NumVGPRs; ++Offset) {
      // Load in VGPR data
      SB.readWriteTmpVGPR(Offset, /*IsLoad*/ true);

      // Unpack lanes
      for (unsigned i = Offset * PVD.PerVGPR,
                    e = std::min((Offset + 1) * PVD.PerVGPR, SB.NumSubRegs);
           i < e; ++i) {
        Register SubReg =
            SB.NumSubRegs == 1
                ? SB.SuperReg
                : Register(getSubReg(SB.SuperReg, SB.SplitParts[i]));

        bool LastSubReg = (i + 1 == e);
        auto MIB = BuildMI(*SB.MBB, MI, SB.DL,
                           SB.TII.get(AMDGPU::SI_RESTORE_S32_FROM_VGPR), SubReg)
                       .addReg(SB.TmpVGPR, getKillRegState(LastSubReg))
                       .addImm(i);
        if (SB.NumSubRegs > 1 && i == 0)
          MIB.addReg(SB.SuperReg, RegState::ImplicitDefine);
        if (Indexes) {
          if (i == e - 1)
            Indexes->replaceMachineInstrInMaps(*MI, *MIB);
          else
            Indexes->insertMachineInstrInMaps(*MIB);
        }
      }
    }

    SB.restore();
  }

  MI->eraseFromParent();

  if (LIS)
    LIS->removeAllRegUnitsForPhysReg(SB.SuperReg);

  return true;
}

bool SIRegisterInfo::spillEmergencySGPR(MachineBasicBlock::iterator MI,
                                        MachineBasicBlock &RestoreMBB,
                                        Register SGPR, RegScavenger *RS) const {
  SGPRSpillBuilder SB(*this, *ST.getInstrInfo(), isWave32, MI, SGPR, false, 0,
                      RS);
  SB.prepare();
  // Generate the spill of SGPR to SB.TmpVGPR.
  unsigned SubKillState = getKillRegState((SB.NumSubRegs == 1) && SB.IsKill);
  auto PVD = SB.getPerVGPRData();
  for (unsigned Offset = 0; Offset < PVD.NumVGPRs; ++Offset) {
    unsigned TmpVGPRFlags = RegState::Undef;
    // Write sub registers into the VGPR
    for (unsigned i = Offset * PVD.PerVGPR,
                  e = std::min((Offset + 1) * PVD.PerVGPR, SB.NumSubRegs);
         i < e; ++i) {
      Register SubReg =
          SB.NumSubRegs == 1
              ? SB.SuperReg
              : Register(getSubReg(SB.SuperReg, SB.SplitParts[i]));

      MachineInstrBuilder WriteLane =
          BuildMI(*SB.MBB, MI, SB.DL, SB.TII.get(AMDGPU::V_WRITELANE_B32),
                  SB.TmpVGPR)
              .addReg(SubReg, SubKillState)
              .addImm(i % PVD.PerVGPR)
              .addReg(SB.TmpVGPR, TmpVGPRFlags);
      TmpVGPRFlags = 0;
      // There could be undef components of a spilled super register.
      // TODO: Can we detect this and skip the spill?
      if (SB.NumSubRegs > 1) {
        // The last implicit use of the SB.SuperReg carries the "Kill" flag.
        unsigned SuperKillState = 0;
        if (i + 1 == SB.NumSubRegs)
          SuperKillState |= getKillRegState(SB.IsKill);
        WriteLane.addReg(SB.SuperReg, RegState::Implicit | SuperKillState);
      }
    }
    // Don't need to write VGPR out.
  }

  // Restore clobbered registers in the specified restore block.
  MI = RestoreMBB.end();
  SB.setMI(&RestoreMBB, MI);
  // Generate the restore of SGPR from SB.TmpVGPR.
  for (unsigned Offset = 0; Offset < PVD.NumVGPRs; ++Offset) {
    // Don't need to load VGPR in.
    // Unpack lanes
    for (unsigned i = Offset * PVD.PerVGPR,
                  e = std::min((Offset + 1) * PVD.PerVGPR, SB.NumSubRegs);
         i < e; ++i) {
      Register SubReg =
          SB.NumSubRegs == 1
              ? SB.SuperReg
              : Register(getSubReg(SB.SuperReg, SB.SplitParts[i]));
      bool LastSubReg = (i + 1 == e);
      auto MIB = BuildMI(*SB.MBB, MI, SB.DL, SB.TII.get(AMDGPU::V_READLANE_B32),
                         SubReg)
                     .addReg(SB.TmpVGPR, getKillRegState(LastSubReg))
                     .addImm(i);
      if (SB.NumSubRegs > 1 && i == 0)
        MIB.addReg(SB.SuperReg, RegState::ImplicitDefine);
    }
  }
  SB.restore();

  SB.MFI.addToSpilledSGPRs(SB.NumSubRegs);
  return false;
}

/// Special case of eliminateFrameIndex. Returns true if the SGPR was spilled to
/// a VGPR and the stack slot can be safely eliminated when all other users are
/// handled.
bool SIRegisterInfo::eliminateSGPRToVGPRSpillFrameIndex(
    MachineBasicBlock::iterator MI, int FI, RegScavenger *RS,
    SlotIndexes *Indexes, LiveIntervals *LIS, bool SpillToPhysVGPRLane) const {
  switch (MI->getOpcode()) {
  case AMDGPU::SI_SPILL_S1024_SAVE:
  case AMDGPU::SI_SPILL_S512_SAVE:
  case AMDGPU::SI_SPILL_S384_SAVE:
  case AMDGPU::SI_SPILL_S352_SAVE:
  case AMDGPU::SI_SPILL_S320_SAVE:
  case AMDGPU::SI_SPILL_S288_SAVE:
  case AMDGPU::SI_SPILL_S256_SAVE:
  case AMDGPU::SI_SPILL_S224_SAVE:
  case AMDGPU::SI_SPILL_S192_SAVE:
  case AMDGPU::SI_SPILL_S160_SAVE:
  case AMDGPU::SI_SPILL_S128_SAVE:
  case AMDGPU::SI_SPILL_S96_SAVE:
  case AMDGPU::SI_SPILL_S64_SAVE:
  case AMDGPU::SI_SPILL_S32_SAVE:
    return spillSGPR(MI, FI, RS, Indexes, LIS, true, SpillToPhysVGPRLane);
  case AMDGPU::SI_SPILL_S1024_RESTORE:
  case AMDGPU::SI_SPILL_S512_RESTORE:
  case AMDGPU::SI_SPILL_S384_RESTORE:
  case AMDGPU::SI_SPILL_S352_RESTORE:
  case AMDGPU::SI_SPILL_S320_RESTORE:
  case AMDGPU::SI_SPILL_S288_RESTORE:
  case AMDGPU::SI_SPILL_S256_RESTORE:
  case AMDGPU::SI_SPILL_S224_RESTORE:
  case AMDGPU::SI_SPILL_S192_RESTORE:
  case AMDGPU::SI_SPILL_S160_RESTORE:
  case AMDGPU::SI_SPILL_S128_RESTORE:
  case AMDGPU::SI_SPILL_S96_RESTORE:
  case AMDGPU::SI_SPILL_S64_RESTORE:
  case AMDGPU::SI_SPILL_S32_RESTORE:
    return restoreSGPR(MI, FI, RS, Indexes, LIS, true, SpillToPhysVGPRLane);
  default:
    llvm_unreachable("not an SGPR spill instruction");
  }
}

bool SIRegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator MI,
                                        int SPAdj, unsigned FIOperandNum,
                                        RegScavenger *RS) const {
  MachineFunction *MF = MI->getParent()->getParent();
  MachineBasicBlock *MBB = MI->getParent();
  SIMachineFunctionInfo *MFI = MF->getInfo<SIMachineFunctionInfo>();
  MachineFrameInfo &FrameInfo = MF->getFrameInfo();
  const SIInstrInfo *TII = ST.getInstrInfo();
  DebugLoc DL = MI->getDebugLoc();

  assert(SPAdj == 0 && "unhandled SP adjustment in call sequence?");

  assert(MF->getRegInfo().isReserved(MFI->getScratchRSrcReg()) &&
         "unreserved scratch RSRC register");

  MachineOperand &FIOp = MI->getOperand(FIOperandNum);
  int Index = MI->getOperand(FIOperandNum).getIndex();

  Register FrameReg = FrameInfo.isFixedObjectIndex(Index) && hasBasePointer(*MF)
                          ? getBaseRegister()
                          : getFrameRegister(*MF);

  switch (MI->getOpcode()) {
    // SGPR register spill
    case AMDGPU::SI_SPILL_S1024_SAVE:
    case AMDGPU::SI_SPILL_S512_SAVE:
    case AMDGPU::SI_SPILL_S384_SAVE:
    case AMDGPU::SI_SPILL_S352_SAVE:
    case AMDGPU::SI_SPILL_S320_SAVE:
    case AMDGPU::SI_SPILL_S288_SAVE:
    case AMDGPU::SI_SPILL_S256_SAVE:
    case AMDGPU::SI_SPILL_S224_SAVE:
    case AMDGPU::SI_SPILL_S192_SAVE:
    case AMDGPU::SI_SPILL_S160_SAVE:
    case AMDGPU::SI_SPILL_S128_SAVE:
    case AMDGPU::SI_SPILL_S96_SAVE:
    case AMDGPU::SI_SPILL_S64_SAVE:
    case AMDGPU::SI_SPILL_S32_SAVE: {
      return spillSGPR(MI, Index, RS);
    }

    // SGPR register restore
    case AMDGPU::SI_SPILL_S1024_RESTORE:
    case AMDGPU::SI_SPILL_S512_RESTORE:
    case AMDGPU::SI_SPILL_S384_RESTORE:
    case AMDGPU::SI_SPILL_S352_RESTORE:
    case AMDGPU::SI_SPILL_S320_RESTORE:
    case AMDGPU::SI_SPILL_S288_RESTORE:
    case AMDGPU::SI_SPILL_S256_RESTORE:
    case AMDGPU::SI_SPILL_S224_RESTORE:
    case AMDGPU::SI_SPILL_S192_RESTORE:
    case AMDGPU::SI_SPILL_S160_RESTORE:
    case AMDGPU::SI_SPILL_S128_RESTORE:
    case AMDGPU::SI_SPILL_S96_RESTORE:
    case AMDGPU::SI_SPILL_S64_RESTORE:
    case AMDGPU::SI_SPILL_S32_RESTORE: {
      return restoreSGPR(MI, Index, RS);
    }

    // VGPR register spill
    case AMDGPU::SI_SPILL_V1024_SAVE:
    case AMDGPU::SI_SPILL_V512_SAVE:
    case AMDGPU::SI_SPILL_V384_SAVE:
    case AMDGPU::SI_SPILL_V352_SAVE:
    case AMDGPU::SI_SPILL_V320_SAVE:
    case AMDGPU::SI_SPILL_V288_SAVE:
    case AMDGPU::SI_SPILL_V256_SAVE:
    case AMDGPU::SI_SPILL_V224_SAVE:
    case AMDGPU::SI_SPILL_V192_SAVE:
    case AMDGPU::SI_SPILL_V160_SAVE:
    case AMDGPU::SI_SPILL_V128_SAVE:
    case AMDGPU::SI_SPILL_V96_SAVE:
    case AMDGPU::SI_SPILL_V64_SAVE:
    case AMDGPU::SI_SPILL_V32_SAVE:
    case AMDGPU::SI_SPILL_A1024_SAVE:
    case AMDGPU::SI_SPILL_A512_SAVE:
    case AMDGPU::SI_SPILL_A384_SAVE:
    case AMDGPU::SI_SPILL_A352_SAVE:
    case AMDGPU::SI_SPILL_A320_SAVE:
    case AMDGPU::SI_SPILL_A288_SAVE:
    case AMDGPU::SI_SPILL_A256_SAVE:
    case AMDGPU::SI_SPILL_A224_SAVE:
    case AMDGPU::SI_SPILL_A192_SAVE:
    case AMDGPU::SI_SPILL_A160_SAVE:
    case AMDGPU::SI_SPILL_A128_SAVE:
    case AMDGPU::SI_SPILL_A96_SAVE:
    case AMDGPU::SI_SPILL_A64_SAVE:
    case AMDGPU::SI_SPILL_A32_SAVE:
    case AMDGPU::SI_SPILL_AV1024_SAVE:
    case AMDGPU::SI_SPILL_AV512_SAVE:
    case AMDGPU::SI_SPILL_AV384_SAVE:
    case AMDGPU::SI_SPILL_AV352_SAVE:
    case AMDGPU::SI_SPILL_AV320_SAVE:
    case AMDGPU::SI_SPILL_AV288_SAVE:
    case AMDGPU::SI_SPILL_AV256_SAVE:
    case AMDGPU::SI_SPILL_AV224_SAVE:
    case AMDGPU::SI_SPILL_AV192_SAVE:
    case AMDGPU::SI_SPILL_AV160_SAVE:
    case AMDGPU::SI_SPILL_AV128_SAVE:
    case AMDGPU::SI_SPILL_AV96_SAVE:
    case AMDGPU::SI_SPILL_AV64_SAVE:
    case AMDGPU::SI_SPILL_AV32_SAVE:
    case AMDGPU::SI_SPILL_WWM_V32_SAVE:
    case AMDGPU::SI_SPILL_WWM_AV32_SAVE: {
      const MachineOperand *VData = TII->getNamedOperand(*MI,
                                                         AMDGPU::OpName::vdata);
      assert(TII->getNamedOperand(*MI, AMDGPU::OpName::soffset)->getReg() ==
             MFI->getStackPtrOffsetReg());

      unsigned Opc = ST.enableFlatScratch() ? AMDGPU::SCRATCH_STORE_DWORD_SADDR
                                            : AMDGPU::BUFFER_STORE_DWORD_OFFSET;
      auto *MBB = MI->getParent();
      bool IsWWMRegSpill = TII->isWWMRegSpillOpcode(MI->getOpcode());
      if (IsWWMRegSpill) {
        TII->insertScratchExecCopy(*MF, *MBB, MI, DL, MFI->getSGPRForEXECCopy(),
                                  RS->isRegUsed(AMDGPU::SCC));
      }
      buildSpillLoadStore(
          *MBB, MI, DL, Opc, Index, VData->getReg(), VData->isKill(), FrameReg,
          TII->getNamedOperand(*MI, AMDGPU::OpName::offset)->getImm(),
          *MI->memoperands_begin(), RS);
      MFI->addToSpilledVGPRs(getNumSubRegsForSpillOp(MI->getOpcode()));
      if (IsWWMRegSpill)
        TII->restoreExec(*MF, *MBB, MI, DL, MFI->getSGPRForEXECCopy());

      MI->eraseFromParent();
      return true;
    }
    case AMDGPU::SI_SPILL_V32_RESTORE:
    case AMDGPU::SI_SPILL_V64_RESTORE:
    case AMDGPU::SI_SPILL_V96_RESTORE:
    case AMDGPU::SI_SPILL_V128_RESTORE:
    case AMDGPU::SI_SPILL_V160_RESTORE:
    case AMDGPU::SI_SPILL_V192_RESTORE:
    case AMDGPU::SI_SPILL_V224_RESTORE:
    case AMDGPU::SI_SPILL_V256_RESTORE:
    case AMDGPU::SI_SPILL_V288_RESTORE:
    case AMDGPU::SI_SPILL_V320_RESTORE:
    case AMDGPU::SI_SPILL_V352_RESTORE:
    case AMDGPU::SI_SPILL_V384_RESTORE:
    case AMDGPU::SI_SPILL_V512_RESTORE:
    case AMDGPU::SI_SPILL_V1024_RESTORE:
    case AMDGPU::SI_SPILL_A32_RESTORE:
    case AMDGPU::SI_SPILL_A64_RESTORE:
    case AMDGPU::SI_SPILL_A96_RESTORE:
    case AMDGPU::SI_SPILL_A128_RESTORE:
    case AMDGPU::SI_SPILL_A160_RESTORE:
    case AMDGPU::SI_SPILL_A192_RESTORE:
    case AMDGPU::SI_SPILL_A224_RESTORE:
    case AMDGPU::SI_SPILL_A256_RESTORE:
    case AMDGPU::SI_SPILL_A288_RESTORE:
    case AMDGPU::SI_SPILL_A320_RESTORE:
    case AMDGPU::SI_SPILL_A352_RESTORE:
    case AMDGPU::SI_SPILL_A384_RESTORE:
    case AMDGPU::SI_SPILL_A512_RESTORE:
    case AMDGPU::SI_SPILL_A1024_RESTORE:
    case AMDGPU::SI_SPILL_AV32_RESTORE:
    case AMDGPU::SI_SPILL_AV64_RESTORE:
    case AMDGPU::SI_SPILL_AV96_RESTORE:
    case AMDGPU::SI_SPILL_AV128_RESTORE:
    case AMDGPU::SI_SPILL_AV160_RESTORE:
    case AMDGPU::SI_SPILL_AV192_RESTORE:
    case AMDGPU::SI_SPILL_AV224_RESTORE:
    case AMDGPU::SI_SPILL_AV256_RESTORE:
    case AMDGPU::SI_SPILL_AV288_RESTORE:
    case AMDGPU::SI_SPILL_AV320_RESTORE:
    case AMDGPU::SI_SPILL_AV352_RESTORE:
    case AMDGPU::SI_SPILL_AV384_RESTORE:
    case AMDGPU::SI_SPILL_AV512_RESTORE:
    case AMDGPU::SI_SPILL_AV1024_RESTORE:
    case AMDGPU::SI_SPILL_WWM_V32_RESTORE:
    case AMDGPU::SI_SPILL_WWM_AV32_RESTORE: {
      const MachineOperand *VData = TII->getNamedOperand(*MI,
                                                         AMDGPU::OpName::vdata);
      assert(TII->getNamedOperand(*MI, AMDGPU::OpName::soffset)->getReg() ==
             MFI->getStackPtrOffsetReg());

      unsigned Opc = ST.enableFlatScratch() ? AMDGPU::SCRATCH_LOAD_DWORD_SADDR
                                            : AMDGPU::BUFFER_LOAD_DWORD_OFFSET;
      auto *MBB = MI->getParent();
      bool IsWWMRegSpill = TII->isWWMRegSpillOpcode(MI->getOpcode());
      if (IsWWMRegSpill) {
        TII->insertScratchExecCopy(*MF, *MBB, MI, DL, MFI->getSGPRForEXECCopy(),
                                  RS->isRegUsed(AMDGPU::SCC));
      }

      buildSpillLoadStore(
          *MBB, MI, DL, Opc, Index, VData->getReg(), VData->isKill(), FrameReg,
          TII->getNamedOperand(*MI, AMDGPU::OpName::offset)->getImm(),
          *MI->memoperands_begin(), RS);

      if (IsWWMRegSpill)
        TII->restoreExec(*MF, *MBB, MI, DL, MFI->getSGPRForEXECCopy());

      MI->eraseFromParent();
      return true;
    }

    default: {
      // Other access to frame index
      const DebugLoc &DL = MI->getDebugLoc();

      int64_t Offset = FrameInfo.getObjectOffset(Index);
      if (ST.enableFlatScratch()) {
        if (TII->isFLATScratch(*MI)) {
          assert((int16_t)FIOperandNum ==
                 AMDGPU::getNamedOperandIdx(MI->getOpcode(),
                                            AMDGPU::OpName::saddr));

          // The offset is always swizzled, just replace it
          if (FrameReg)
            FIOp.ChangeToRegister(FrameReg, false);

          MachineOperand *OffsetOp =
            TII->getNamedOperand(*MI, AMDGPU::OpName::offset);
          int64_t NewOffset = Offset + OffsetOp->getImm();
          if (TII->isLegalFLATOffset(NewOffset, AMDGPUAS::PRIVATE_ADDRESS,
                                     SIInstrFlags::FlatScratch)) {
            OffsetOp->setImm(NewOffset);
            if (FrameReg)
              return false;
            Offset = 0;
          }

          if (!Offset) {
            unsigned Opc = MI->getOpcode();
            int NewOpc = -1;
            if (AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::vaddr)) {
              NewOpc = AMDGPU::getFlatScratchInstSVfromSVS(Opc);
            } else if (ST.hasFlatScratchSTMode()) {
              // On GFX10 we have ST mode to use no registers for an address.
              // Otherwise we need to materialize 0 into an SGPR.
              NewOpc = AMDGPU::getFlatScratchInstSTfromSS(Opc);
            }

            if (NewOpc != -1) {
              // removeOperand doesn't fixup tied operand indexes as it goes, so
              // it asserts. Untie vdst_in for now and retie them afterwards.
              int VDstIn = AMDGPU::getNamedOperandIdx(Opc,
                                                     AMDGPU::OpName::vdst_in);
              bool TiedVDst = VDstIn != -1 &&
                              MI->getOperand(VDstIn).isReg() &&
                              MI->getOperand(VDstIn).isTied();
              if (TiedVDst)
                MI->untieRegOperand(VDstIn);

              MI->removeOperand(
                  AMDGPU::getNamedOperandIdx(Opc, AMDGPU::OpName::saddr));

              if (TiedVDst) {
                int NewVDst =
                    AMDGPU::getNamedOperandIdx(NewOpc, AMDGPU::OpName::vdst);
                int NewVDstIn =
                    AMDGPU::getNamedOperandIdx(NewOpc, AMDGPU::OpName::vdst_in);
                assert (NewVDst != -1 && NewVDstIn != -1 && "Must be tied!");
                MI->tieOperands(NewVDst, NewVDstIn);
              }
              MI->setDesc(TII->get(NewOpc));
              return false;
            }
          }
        }

        if (!FrameReg) {
          FIOp.ChangeToImmediate(Offset);
          if (TII->isImmOperandLegal(*MI, FIOperandNum, FIOp))
            return false;
        }

        // We need to use register here. Check if we can use an SGPR or need
        // a VGPR.
        FIOp.ChangeToRegister(AMDGPU::M0, false);
        bool UseSGPR = TII->isOperandLegal(*MI, FIOperandNum, &FIOp);

        if (!Offset && FrameReg && UseSGPR) {
          FIOp.setReg(FrameReg);
          return false;
        }

        const TargetRegisterClass *RC = UseSGPR ? &AMDGPU::SReg_32_XM0RegClass
                                                : &AMDGPU::VGPR_32RegClass;

        Register TmpReg =
            RS->scavengeRegisterBackwards(*RC, MI, false, 0, !UseSGPR);
        FIOp.setReg(TmpReg);
        FIOp.setIsKill();

        if ((!FrameReg || !Offset) && TmpReg) {
          unsigned Opc = UseSGPR ? AMDGPU::S_MOV_B32 : AMDGPU::V_MOV_B32_e32;
          auto MIB = BuildMI(*MBB, MI, DL, TII->get(Opc), TmpReg);
          if (FrameReg)
            MIB.addReg(FrameReg);
          else
            MIB.addImm(Offset);

          return false;
        }

        bool NeedSaveSCC = RS->isRegUsed(AMDGPU::SCC) &&
                           !MI->definesRegister(AMDGPU::SCC, /*TRI=*/nullptr);

        Register TmpSReg =
            UseSGPR ? TmpReg
                    : RS->scavengeRegisterBackwards(AMDGPU::SReg_32_XM0RegClass,
                                                    MI, false, 0, !UseSGPR);

        // TODO: for flat scratch another attempt can be made with a VGPR index
        //       if no SGPRs can be scavenged.
        if ((!TmpSReg && !FrameReg) || (!TmpReg && !UseSGPR))
          report_fatal_error("Cannot scavenge register in FI elimination!");

        if (!TmpSReg) {
          // Use frame register and restore it after.
          TmpSReg = FrameReg;
          FIOp.setReg(FrameReg);
          FIOp.setIsKill(false);
        }

        if (NeedSaveSCC) {
          assert(!(Offset & 0x1) && "Flat scratch offset must be aligned!");
          BuildMI(*MBB, MI, DL, TII->get(AMDGPU::S_ADDC_U32), TmpSReg)
              .addReg(FrameReg)
              .addImm(Offset);
          BuildMI(*MBB, MI, DL, TII->get(AMDGPU::S_BITCMP1_B32))
              .addReg(TmpSReg)
              .addImm(0);
          BuildMI(*MBB, MI, DL, TII->get(AMDGPU::S_BITSET0_B32), TmpSReg)
              .addImm(0)
              .addReg(TmpSReg);
        } else {
          BuildMI(*MBB, MI, DL, TII->get(AMDGPU::S_ADD_I32), TmpSReg)
              .addReg(FrameReg)
              .addImm(Offset);
        }

        if (!UseSGPR)
          BuildMI(*MBB, MI, DL, TII->get(AMDGPU::V_MOV_B32_e32), TmpReg)
            .addReg(TmpSReg, RegState::Kill);

        if (TmpSReg == FrameReg) {
          // Undo frame register modification.
          if (NeedSaveSCC &&
              !MI->registerDefIsDead(AMDGPU::SCC, /*TRI=*/nullptr)) {
            MachineBasicBlock::iterator I =
                BuildMI(*MBB, std::next(MI), DL, TII->get(AMDGPU::S_ADDC_U32),
                        TmpSReg)
                    .addReg(FrameReg)
                    .addImm(-Offset);
            I = BuildMI(*MBB, std::next(I), DL, TII->get(AMDGPU::S_BITCMP1_B32))
                    .addReg(TmpSReg)
                    .addImm(0);
            BuildMI(*MBB, std::next(I), DL, TII->get(AMDGPU::S_BITSET0_B32),
                    TmpSReg)
                .addImm(0)
                .addReg(TmpSReg);
          } else {
            BuildMI(*MBB, std::next(MI), DL, TII->get(AMDGPU::S_ADD_I32),
                    FrameReg)
                .addReg(FrameReg)
                .addImm(-Offset);
          }
        }

        return false;
      }

      bool IsMUBUF = TII->isMUBUF(*MI);

      if (!IsMUBUF && !MFI->isBottomOfStack()) {
        // Convert to a swizzled stack address by scaling by the wave size.
        // In an entry function/kernel the offset is already swizzled.
        bool IsSALU = isSGPRClass(TII->getOpRegClass(*MI, FIOperandNum));
        bool LiveSCC = RS->isRegUsed(AMDGPU::SCC) &&
                       !MI->definesRegister(AMDGPU::SCC, /*TRI=*/nullptr);
        const TargetRegisterClass *RC = IsSALU && !LiveSCC
                                            ? &AMDGPU::SReg_32RegClass
                                            : &AMDGPU::VGPR_32RegClass;
        bool IsCopy = MI->getOpcode() == AMDGPU::V_MOV_B32_e32 ||
                      MI->getOpcode() == AMDGPU::V_MOV_B32_e64;
        Register ResultReg =
            IsCopy ? MI->getOperand(0).getReg()
                   : RS->scavengeRegisterBackwards(*RC, MI, false, 0);

        int64_t Offset = FrameInfo.getObjectOffset(Index);
        if (Offset == 0) {
          unsigned OpCode = IsSALU && !LiveSCC ? AMDGPU::S_LSHR_B32
                                               : AMDGPU::V_LSHRREV_B32_e64;
          auto Shift = BuildMI(*MBB, MI, DL, TII->get(OpCode), ResultReg);
          if (OpCode == AMDGPU::V_LSHRREV_B32_e64)
            // For V_LSHRREV, the operands are reversed (the shift count goes
            // first).
            Shift.addImm(ST.getWavefrontSizeLog2()).addReg(FrameReg);
          else
            Shift.addReg(FrameReg).addImm(ST.getWavefrontSizeLog2());
          if (IsSALU && !LiveSCC)
            Shift.getInstr()->getOperand(3).setIsDead(); // Mark SCC as dead.
          if (IsSALU && LiveSCC) {
            Register NewDest = RS->scavengeRegisterBackwards(
                AMDGPU::SReg_32RegClass, Shift, false, 0);
            BuildMI(*MBB, MI, DL, TII->get(AMDGPU::V_READFIRSTLANE_B32),
                    NewDest)
                .addReg(ResultReg);
            ResultReg = NewDest;
          }
        } else {
          MachineInstrBuilder MIB;
          if (!IsSALU) {
            if ((MIB = TII->getAddNoCarry(*MBB, MI, DL, ResultReg, *RS)) !=
                nullptr) {
              // Reuse ResultReg in intermediate step.
              Register ScaledReg = ResultReg;

              BuildMI(*MBB, *MIB, DL, TII->get(AMDGPU::V_LSHRREV_B32_e64),
                      ScaledReg)
                .addImm(ST.getWavefrontSizeLog2())
                .addReg(FrameReg);

              const bool IsVOP2 = MIB->getOpcode() == AMDGPU::V_ADD_U32_e32;

              // TODO: Fold if use instruction is another add of a constant.
              if (IsVOP2 || AMDGPU::isInlinableLiteral32(Offset, ST.hasInv2PiInlineImm())) {
                // FIXME: This can fail
                MIB.addImm(Offset);
                MIB.addReg(ScaledReg, RegState::Kill);
                if (!IsVOP2)
                  MIB.addImm(0); // clamp bit
              } else {
                assert(MIB->getOpcode() == AMDGPU::V_ADD_CO_U32_e64 &&
                       "Need to reuse carry out register");

                // Use scavenged unused carry out as offset register.
                Register ConstOffsetReg;
                if (!isWave32)
                  ConstOffsetReg = getSubReg(MIB.getReg(1), AMDGPU::sub0);
                else
                  ConstOffsetReg = MIB.getReg(1);

                BuildMI(*MBB, *MIB, DL, TII->get(AMDGPU::S_MOV_B32), ConstOffsetReg)
                    .addImm(Offset);
                MIB.addReg(ConstOffsetReg, RegState::Kill);
                MIB.addReg(ScaledReg, RegState::Kill);
                MIB.addImm(0); // clamp bit
              }
            }
          }
          if (!MIB || IsSALU) {
            // We have to produce a carry out, and there isn't a free SGPR pair
            // for it. We can keep the whole computation on the SALU to avoid
            // clobbering an additional register at the cost of an extra mov.

            // We may have 1 free scratch SGPR even though a carry out is
            // unavailable. Only one additional mov is needed.
            Register TmpScaledReg = RS->scavengeRegisterBackwards(
                AMDGPU::SReg_32_XM0RegClass, MI, false, 0, false);
            Register ScaledReg = TmpScaledReg.isValid() ? TmpScaledReg : FrameReg;

            BuildMI(*MBB, MI, DL, TII->get(AMDGPU::S_LSHR_B32), ScaledReg)
              .addReg(FrameReg)
              .addImm(ST.getWavefrontSizeLog2());
            BuildMI(*MBB, MI, DL, TII->get(AMDGPU::S_ADD_I32), ScaledReg)
                .addReg(ScaledReg, RegState::Kill)
                .addImm(Offset);
            if (!IsSALU)
              BuildMI(*MBB, MI, DL, TII->get(AMDGPU::COPY), ResultReg)
                  .addReg(ScaledReg, RegState::Kill);
            else
              ResultReg = ScaledReg;

            // If there were truly no free SGPRs, we need to undo everything.
            if (!TmpScaledReg.isValid()) {
              BuildMI(*MBB, MI, DL, TII->get(AMDGPU::S_ADD_I32), ScaledReg)
                .addReg(ScaledReg, RegState::Kill)
                .addImm(-Offset);
              BuildMI(*MBB, MI, DL, TII->get(AMDGPU::S_LSHL_B32), ScaledReg)
                .addReg(FrameReg)
                .addImm(ST.getWavefrontSizeLog2());
            }
          }
        }

        // Don't introduce an extra copy if we're just materializing in a mov.
        if (IsCopy) {
          MI->eraseFromParent();
          return true;
        }
        FIOp.ChangeToRegister(ResultReg, false, false, true);
        return false;
      }

      if (IsMUBUF) {
        // Disable offen so we don't need a 0 vgpr base.
        assert(static_cast<int>(FIOperandNum) ==
               AMDGPU::getNamedOperandIdx(MI->getOpcode(),
                                          AMDGPU::OpName::vaddr));

        auto &SOffset = *TII->getNamedOperand(*MI, AMDGPU::OpName::soffset);
        assert((SOffset.isImm() && SOffset.getImm() == 0));

        if (FrameReg != AMDGPU::NoRegister)
          SOffset.ChangeToRegister(FrameReg, false);

        int64_t Offset = FrameInfo.getObjectOffset(Index);
        int64_t OldImm
          = TII->getNamedOperand(*MI, AMDGPU::OpName::offset)->getImm();
        int64_t NewOffset = OldImm + Offset;

        if (TII->isLegalMUBUFImmOffset(NewOffset) &&
            buildMUBUFOffsetLoadStore(ST, FrameInfo, MI, Index, NewOffset)) {
          MI->eraseFromParent();
          return true;
        }
      }

      // If the offset is simply too big, don't convert to a scratch wave offset
      // relative index.

      FIOp.ChangeToImmediate(Offset);
      if (!TII->isImmOperandLegal(*MI, FIOperandNum, FIOp)) {
        Register TmpReg = RS->scavengeRegisterBackwards(AMDGPU::VGPR_32RegClass,
                                                        MI, false, 0);
        BuildMI(*MBB, MI, DL, TII->get(AMDGPU::V_MOV_B32_e32), TmpReg)
          .addImm(Offset);
        FIOp.ChangeToRegister(TmpReg, false, false, true);
      }
    }
  }
  return false;
}

StringRef SIRegisterInfo::getRegAsmName(MCRegister Reg) const {
  return AMDGPUInstPrinter::getRegisterName(Reg);
}

unsigned AMDGPU::getRegBitWidth(const TargetRegisterClass &RC) {
  return getRegBitWidth(RC.getID());
}

static const TargetRegisterClass *
getAnyVGPRClassForBitWidth(unsigned BitWidth) {
  if (BitWidth == 64)
    return &AMDGPU::VReg_64RegClass;
  if (BitWidth == 96)
    return &AMDGPU::VReg_96RegClass;
  if (BitWidth == 128)
    return &AMDGPU::VReg_128RegClass;
  if (BitWidth == 160)
    return &AMDGPU::VReg_160RegClass;
  if (BitWidth == 192)
    return &AMDGPU::VReg_192RegClass;
  if (BitWidth == 224)
    return &AMDGPU::VReg_224RegClass;
  if (BitWidth == 256)
    return &AMDGPU::VReg_256RegClass;
  if (BitWidth == 288)
    return &AMDGPU::VReg_288RegClass;
  if (BitWidth == 320)
    return &AMDGPU::VReg_320RegClass;
  if (BitWidth == 352)
    return &AMDGPU::VReg_352RegClass;
  if (BitWidth == 384)
    return &AMDGPU::VReg_384RegClass;
  if (BitWidth == 512)
    return &AMDGPU::VReg_512RegClass;
  if (BitWidth == 1024)
    return &AMDGPU::VReg_1024RegClass;

  return nullptr;
}

static const TargetRegisterClass *
getAlignedVGPRClassForBitWidth(unsigned BitWidth) {
  if (BitWidth == 64)
    return &AMDGPU::VReg_64_Align2RegClass;
  if (BitWidth == 96)
    return &AMDGPU::VReg_96_Align2RegClass;
  if (BitWidth == 128)
    return &AMDGPU::VReg_128_Align2RegClass;
  if (BitWidth == 160)
    return &AMDGPU::VReg_160_Align2RegClass;
  if (BitWidth == 192)
    return &AMDGPU::VReg_192_Align2RegClass;
  if (BitWidth == 224)
    return &AMDGPU::VReg_224_Align2RegClass;
  if (BitWidth == 256)
    return &AMDGPU::VReg_256_Align2RegClass;
  if (BitWidth == 288)
    return &AMDGPU::VReg_288_Align2RegClass;
  if (BitWidth == 320)
    return &AMDGPU::VReg_320_Align2RegClass;
  if (BitWidth == 352)
    return &AMDGPU::VReg_352_Align2RegClass;
  if (BitWidth == 384)
    return &AMDGPU::VReg_384_Align2RegClass;
  if (BitWidth == 512)
    return &AMDGPU::VReg_512_Align2RegClass;
  if (BitWidth == 1024)
    return &AMDGPU::VReg_1024_Align2RegClass;

  return nullptr;
}

const TargetRegisterClass *
SIRegisterInfo::getVGPRClassForBitWidth(unsigned BitWidth) const {
  if (BitWidth == 1)
    return &AMDGPU::VReg_1RegClass;
  if (BitWidth == 16)
    return &AMDGPU::VGPR_16RegClass;
  if (BitWidth == 32)
    return &AMDGPU::VGPR_32RegClass;
  return ST.needsAlignedVGPRs() ? getAlignedVGPRClassForBitWidth(BitWidth)
                                : getAnyVGPRClassForBitWidth(BitWidth);
}

static const TargetRegisterClass *
getAnyAGPRClassForBitWidth(unsigned BitWidth) {
  if (BitWidth == 64)
    return &AMDGPU::AReg_64RegClass;
  if (BitWidth == 96)
    return &AMDGPU::AReg_96RegClass;
  if (BitWidth == 128)
    return &AMDGPU::AReg_128RegClass;
  if (BitWidth == 160)
    return &AMDGPU::AReg_160RegClass;
  if (BitWidth == 192)
    return &AMDGPU::AReg_192RegClass;
  if (BitWidth == 224)
    return &AMDGPU::AReg_224RegClass;
  if (BitWidth == 256)
    return &AMDGPU::AReg_256RegClass;
  if (BitWidth == 288)
    return &AMDGPU::AReg_288RegClass;
  if (BitWidth == 320)
    return &AMDGPU::AReg_320RegClass;
  if (BitWidth == 352)
    return &AMDGPU::AReg_352RegClass;
  if (BitWidth == 384)
    return &AMDGPU::AReg_384RegClass;
  if (BitWidth == 512)
    return &AMDGPU::AReg_512RegClass;
  if (BitWidth == 1024)
    return &AMDGPU::AReg_1024RegClass;

  return nullptr;
}

static const TargetRegisterClass *
getAlignedAGPRClassForBitWidth(unsigned BitWidth) {
  if (BitWidth == 64)
    return &AMDGPU::AReg_64_Align2RegClass;
  if (BitWidth == 96)
    return &AMDGPU::AReg_96_Align2RegClass;
  if (BitWidth == 128)
    return &AMDGPU::AReg_128_Align2RegClass;
  if (BitWidth == 160)
    return &AMDGPU::AReg_160_Align2RegClass;
  if (BitWidth == 192)
    return &AMDGPU::AReg_192_Align2RegClass;
  if (BitWidth == 224)
    return &AMDGPU::AReg_224_Align2RegClass;
  if (BitWidth == 256)
    return &AMDGPU::AReg_256_Align2RegClass;
  if (BitWidth == 288)
    return &AMDGPU::AReg_288_Align2RegClass;
  if (BitWidth == 320)
    return &AMDGPU::AReg_320_Align2RegClass;
  if (BitWidth == 352)
    return &AMDGPU::AReg_352_Align2RegClass;
  if (BitWidth == 384)
    return &AMDGPU::AReg_384_Align2RegClass;
  if (BitWidth == 512)
    return &AMDGPU::AReg_512_Align2RegClass;
  if (BitWidth == 1024)
    return &AMDGPU::AReg_1024_Align2RegClass;

  return nullptr;
}

const TargetRegisterClass *
SIRegisterInfo::getAGPRClassForBitWidth(unsigned BitWidth) const {
  if (BitWidth == 16)
    return &AMDGPU::AGPR_LO16RegClass;
  if (BitWidth == 32)
    return &AMDGPU::AGPR_32RegClass;
  return ST.needsAlignedVGPRs() ? getAlignedAGPRClassForBitWidth(BitWidth)
                                : getAnyAGPRClassForBitWidth(BitWidth);
}

static const TargetRegisterClass *
getAnyVectorSuperClassForBitWidth(unsigned BitWidth) {
  if (BitWidth == 64)
    return &AMDGPU::AV_64RegClass;
  if (BitWidth == 96)
    return &AMDGPU::AV_96RegClass;
  if (BitWidth == 128)
    return &AMDGPU::AV_128RegClass;
  if (BitWidth == 160)
    return &AMDGPU::AV_160RegClass;
  if (BitWidth == 192)
    return &AMDGPU::AV_192RegClass;
  if (BitWidth == 224)
    return &AMDGPU::AV_224RegClass;
  if (BitWidth == 256)
    return &AMDGPU::AV_256RegClass;
  if (BitWidth == 288)
    return &AMDGPU::AV_288RegClass;
  if (BitWidth == 320)
    return &AMDGPU::AV_320RegClass;
  if (BitWidth == 352)
    return &AMDGPU::AV_352RegClass;
  if (BitWidth == 384)
    return &AMDGPU::AV_384RegClass;
  if (BitWidth == 512)
    return &AMDGPU::AV_512RegClass;
  if (BitWidth == 1024)
    return &AMDGPU::AV_1024RegClass;

  return nullptr;
}

static const TargetRegisterClass *
getAlignedVectorSuperClassForBitWidth(unsigned BitWidth) {
  if (BitWidth == 64)
    return &AMDGPU::AV_64_Align2RegClass;
  if (BitWidth == 96)
    return &AMDGPU::AV_96_Align2RegClass;
  if (BitWidth == 128)
    return &AMDGPU::AV_128_Align2RegClass;
  if (BitWidth == 160)
    return &AMDGPU::AV_160_Align2RegClass;
  if (BitWidth == 192)
    return &AMDGPU::AV_192_Align2RegClass;
  if (BitWidth == 224)
    return &AMDGPU::AV_224_Align2RegClass;
  if (BitWidth == 256)
    return &AMDGPU::AV_256_Align2RegClass;
  if (BitWidth == 288)
    return &AMDGPU::AV_288_Align2RegClass;
  if (BitWidth == 320)
    return &AMDGPU::AV_320_Align2RegClass;
  if (BitWidth == 352)
    return &AMDGPU::AV_352_Align2RegClass;
  if (BitWidth == 384)
    return &AMDGPU::AV_384_Align2RegClass;
  if (BitWidth == 512)
    return &AMDGPU::AV_512_Align2RegClass;
  if (BitWidth == 1024)
    return &AMDGPU::AV_1024_Align2RegClass;

  return nullptr;
}

const TargetRegisterClass *
SIRegisterInfo::getVectorSuperClassForBitWidth(unsigned BitWidth) const {
  if (BitWidth == 32)
    return &AMDGPU::AV_32RegClass;
  return ST.needsAlignedVGPRs()
             ? getAlignedVectorSuperClassForBitWidth(BitWidth)
             : getAnyVectorSuperClassForBitWidth(BitWidth);
}

const TargetRegisterClass *
SIRegisterInfo::getSGPRClassForBitWidth(unsigned BitWidth) {
  if (BitWidth == 16)
    return &AMDGPU::SGPR_LO16RegClass;
  if (BitWidth == 32)
    return &AMDGPU::SReg_32RegClass;
  if (BitWidth == 64)
    return &AMDGPU::SReg_64RegClass;
  if (BitWidth == 96)
    return &AMDGPU::SGPR_96RegClass;
  if (BitWidth == 128)
    return &AMDGPU::SGPR_128RegClass;
  if (BitWidth == 160)
    return &AMDGPU::SGPR_160RegClass;
  if (BitWidth == 192)
    return &AMDGPU::SGPR_192RegClass;
  if (BitWidth == 224)
    return &AMDGPU::SGPR_224RegClass;
  if (BitWidth == 256)
    return &AMDGPU::SGPR_256RegClass;
  if (BitWidth == 288)
    return &AMDGPU::SGPR_288RegClass;
  if (BitWidth == 320)
    return &AMDGPU::SGPR_320RegClass;
  if (BitWidth == 352)
    return &AMDGPU::SGPR_352RegClass;
  if (BitWidth == 384)
    return &AMDGPU::SGPR_384RegClass;
  if (BitWidth == 512)
    return &AMDGPU::SGPR_512RegClass;
  if (BitWidth == 1024)
    return &AMDGPU::SGPR_1024RegClass;

  return nullptr;
}

bool SIRegisterInfo::isSGPRReg(const MachineRegisterInfo &MRI,
                               Register Reg) const {
  const TargetRegisterClass *RC;
  if (Reg.isVirtual())
    RC = MRI.getRegClass(Reg);
  else
    RC = getPhysRegBaseClass(Reg);
  return RC ? isSGPRClass(RC) : false;
}

const TargetRegisterClass *
SIRegisterInfo::getEquivalentVGPRClass(const TargetRegisterClass *SRC) const {
  unsigned Size = getRegSizeInBits(*SRC);
  const TargetRegisterClass *VRC = getVGPRClassForBitWidth(Size);
  assert(VRC && "Invalid register class size");
  return VRC;
}

const TargetRegisterClass *
SIRegisterInfo::getEquivalentAGPRClass(const TargetRegisterClass *SRC) const {
  unsigned Size = getRegSizeInBits(*SRC);
  const TargetRegisterClass *ARC = getAGPRClassForBitWidth(Size);
  assert(ARC && "Invalid register class size");
  return ARC;
}

const TargetRegisterClass *
SIRegisterInfo::getEquivalentSGPRClass(const TargetRegisterClass *VRC) const {
  unsigned Size = getRegSizeInBits(*VRC);
  if (Size == 32)
    return &AMDGPU::SGPR_32RegClass;
  const TargetRegisterClass *SRC = getSGPRClassForBitWidth(Size);
  assert(SRC && "Invalid register class size");
  return SRC;
}

const TargetRegisterClass *
SIRegisterInfo::getCompatibleSubRegClass(const TargetRegisterClass *SuperRC,
                                         const TargetRegisterClass *SubRC,
                                         unsigned SubIdx) const {
  // Ensure this subregister index is aligned in the super register.
  const TargetRegisterClass *MatchRC =
      getMatchingSuperRegClass(SuperRC, SubRC, SubIdx);
  return MatchRC && MatchRC->hasSubClassEq(SuperRC) ? MatchRC : nullptr;
}

bool SIRegisterInfo::opCanUseInlineConstant(unsigned OpType) const {
  if (OpType >= AMDGPU::OPERAND_REG_INLINE_AC_FIRST &&
      OpType <= AMDGPU::OPERAND_REG_INLINE_AC_LAST)
    return !ST.hasMFMAInlineLiteralBug();

  return OpType >= AMDGPU::OPERAND_SRC_FIRST &&
         OpType <= AMDGPU::OPERAND_SRC_LAST;
}

bool SIRegisterInfo::shouldRewriteCopySrc(
  const TargetRegisterClass *DefRC,
  unsigned DefSubReg,
  const TargetRegisterClass *SrcRC,
  unsigned SrcSubReg) const {
  // We want to prefer the smallest register class possible, so we don't want to
  // stop and rewrite on anything that looks like a subregister
  // extract. Operations mostly don't care about the super register class, so we
  // only want to stop on the most basic of copies between the same register
  // class.
  //
  // e.g. if we have something like
  // %0 = ...
  // %1 = ...
  // %2 = REG_SEQUENCE %0, sub0, %1, sub1, %2, sub2
  // %3 = COPY %2, sub0
  //
  // We want to look through the COPY to find:
  //  => %3 = COPY %0

  // Plain copy.
  return getCommonSubClass(DefRC, SrcRC) != nullptr;
}

bool SIRegisterInfo::opCanUseLiteralConstant(unsigned OpType) const {
  // TODO: 64-bit operands have extending behavior from 32-bit literal.
  return OpType >= AMDGPU::OPERAND_REG_IMM_FIRST &&
         OpType <= AMDGPU::OPERAND_REG_IMM_LAST;
}

/// Returns a lowest register that is not used at any point in the function.
///        If all registers are used, then this function will return
///         AMDGPU::NoRegister. If \p ReserveHighestRegister = true, then return
///         highest unused register.
MCRegister SIRegisterInfo::findUnusedRegister(
    const MachineRegisterInfo &MRI, const TargetRegisterClass *RC,
    const MachineFunction &MF, bool ReserveHighestRegister) const {
  if (ReserveHighestRegister) {
    for (MCRegister Reg : reverse(*RC))
      if (MRI.isAllocatable(Reg) && !MRI.isPhysRegUsed(Reg))
        return Reg;
  } else {
    for (MCRegister Reg : *RC)
      if (MRI.isAllocatable(Reg) && !MRI.isPhysRegUsed(Reg))
        return Reg;
  }
  return MCRegister();
}

bool SIRegisterInfo::isUniformReg(const MachineRegisterInfo &MRI,
                                  const RegisterBankInfo &RBI,
                                  Register Reg) const {
  auto *RB = RBI.getRegBank(Reg, MRI, *MRI.getTargetRegisterInfo());
  if (!RB)
    return false;

  return !RBI.isDivergentRegBank(RB);
}

ArrayRef<int16_t> SIRegisterInfo::getRegSplitParts(const TargetRegisterClass *RC,
                                                   unsigned EltSize) const {
  const unsigned RegBitWidth = AMDGPU::getRegBitWidth(*RC);
  assert(RegBitWidth >= 32 && RegBitWidth <= 1024);

  const unsigned RegDWORDs = RegBitWidth / 32;
  const unsigned EltDWORDs = EltSize / 4;
  assert(RegSplitParts.size() + 1 >= EltDWORDs);

  const std::vector<int16_t> &Parts = RegSplitParts[EltDWORDs - 1];
  const unsigned NumParts = RegDWORDs / EltDWORDs;

  return ArrayRef(Parts.data(), NumParts);
}

const TargetRegisterClass*
SIRegisterInfo::getRegClassForReg(const MachineRegisterInfo &MRI,
                                  Register Reg) const {
  return Reg.isVirtual() ? MRI.getRegClass(Reg) : getPhysRegBaseClass(Reg);
}

const TargetRegisterClass *
SIRegisterInfo::getRegClassForOperandReg(const MachineRegisterInfo &MRI,
                                         const MachineOperand &MO) const {
  const TargetRegisterClass *SrcRC = getRegClassForReg(MRI, MO.getReg());
  return getSubRegisterClass(SrcRC, MO.getSubReg());
}

bool SIRegisterInfo::isVGPR(const MachineRegisterInfo &MRI,
                            Register Reg) const {
  const TargetRegisterClass *RC = getRegClassForReg(MRI, Reg);
  // Registers without classes are unaddressable, SGPR-like registers.
  return RC && isVGPRClass(RC);
}

bool SIRegisterInfo::isAGPR(const MachineRegisterInfo &MRI,
                            Register Reg) const {
  const TargetRegisterClass *RC = getRegClassForReg(MRI, Reg);

  // Registers without classes are unaddressable, SGPR-like registers.
  return RC && isAGPRClass(RC);
}

bool SIRegisterInfo::shouldCoalesce(MachineInstr *MI,
                                    const TargetRegisterClass *SrcRC,
                                    unsigned SubReg,
                                    const TargetRegisterClass *DstRC,
                                    unsigned DstSubReg,
                                    const TargetRegisterClass *NewRC,
                                    LiveIntervals &LIS) const {
  unsigned SrcSize = getRegSizeInBits(*SrcRC);
  unsigned DstSize = getRegSizeInBits(*DstRC);
  unsigned NewSize = getRegSizeInBits(*NewRC);

  // Do not increase size of registers beyond dword, we would need to allocate
  // adjacent registers and constraint regalloc more than needed.

  // Always allow dword coalescing.
  if (SrcSize <= 32 || DstSize <= 32)
    return true;

  return NewSize <= DstSize || NewSize <= SrcSize;
}

unsigned SIRegisterInfo::getRegPressureLimit(const TargetRegisterClass *RC,
                                             MachineFunction &MF) const {
  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();

  unsigned Occupancy = ST.getOccupancyWithLocalMemSize(MFI->getLDSSize(),
                                                       MF.getFunction());
  switch (RC->getID()) {
  default:
    return AMDGPUGenRegisterInfo::getRegPressureLimit(RC, MF);
  case AMDGPU::VGPR_32RegClassID:
    return std::min(ST.getMaxNumVGPRs(Occupancy), ST.getMaxNumVGPRs(MF));
  case AMDGPU::SGPR_32RegClassID:
  case AMDGPU::SGPR_LO16RegClassID:
    return std::min(ST.getMaxNumSGPRs(Occupancy, true), ST.getMaxNumSGPRs(MF));
  }
}

unsigned SIRegisterInfo::getRegPressureSetLimit(const MachineFunction &MF,
                                                unsigned Idx) const {
  if (Idx == AMDGPU::RegisterPressureSets::VGPR_32 ||
      Idx == AMDGPU::RegisterPressureSets::AGPR_32)
    return getRegPressureLimit(&AMDGPU::VGPR_32RegClass,
                               const_cast<MachineFunction &>(MF));

  if (Idx == AMDGPU::RegisterPressureSets::SReg_32)
    return getRegPressureLimit(&AMDGPU::SGPR_32RegClass,
                               const_cast<MachineFunction &>(MF));

  llvm_unreachable("Unexpected register pressure set!");
}

const int *SIRegisterInfo::getRegUnitPressureSets(unsigned RegUnit) const {
  static const int Empty[] = { -1 };

  if (RegPressureIgnoredUnits[RegUnit])
    return Empty;

  return AMDGPUGenRegisterInfo::getRegUnitPressureSets(RegUnit);
}

MCRegister SIRegisterInfo::getReturnAddressReg(const MachineFunction &MF) const {
  // Not a callee saved register.
  return AMDGPU::SGPR30_SGPR31;
}

const TargetRegisterClass *
SIRegisterInfo::getRegClassForSizeOnBank(unsigned Size,
                                         const RegisterBank &RB) const {
  switch (RB.getID()) {
  case AMDGPU::VGPRRegBankID:
    return getVGPRClassForBitWidth(
        std::max(ST.useRealTrue16Insts() ? 16u : 32u, Size));
  case AMDGPU::VCCRegBankID:
    assert(Size == 1);
    return isWave32 ? &AMDGPU::SReg_32_XM0_XEXECRegClass
                    : &AMDGPU::SReg_64_XEXECRegClass;
  case AMDGPU::SGPRRegBankID:
    return getSGPRClassForBitWidth(std::max(32u, Size));
  case AMDGPU::AGPRRegBankID:
    return getAGPRClassForBitWidth(std::max(32u, Size));
  default:
    llvm_unreachable("unknown register bank");
  }
}

const TargetRegisterClass *
SIRegisterInfo::getConstrainedRegClassForOperand(const MachineOperand &MO,
                                         const MachineRegisterInfo &MRI) const {
  const RegClassOrRegBank &RCOrRB = MRI.getRegClassOrRegBank(MO.getReg());
  if (const RegisterBank *RB = RCOrRB.dyn_cast<const RegisterBank*>())
    return getRegClassForTypeOnBank(MRI.getType(MO.getReg()), *RB);

  if (const auto *RC = RCOrRB.dyn_cast<const TargetRegisterClass *>())
    return getAllocatableClass(RC);

  return nullptr;
}

MCRegister SIRegisterInfo::getVCC() const {
  return isWave32 ? AMDGPU::VCC_LO : AMDGPU::VCC;
}

MCRegister SIRegisterInfo::getExec() const {
  return isWave32 ? AMDGPU::EXEC_LO : AMDGPU::EXEC;
}

const TargetRegisterClass *SIRegisterInfo::getVGPR64Class() const {
  // VGPR tuples have an alignment requirement on gfx90a variants.
  return ST.needsAlignedVGPRs() ? &AMDGPU::VReg_64_Align2RegClass
                                : &AMDGPU::VReg_64RegClass;
}

const TargetRegisterClass *
SIRegisterInfo::getRegClass(unsigned RCID) const {
  switch ((int)RCID) {
  case AMDGPU::SReg_1RegClassID:
    return getBoolRC();
  case AMDGPU::SReg_1_XEXECRegClassID:
    return isWave32 ? &AMDGPU::SReg_32_XM0_XEXECRegClass
      : &AMDGPU::SReg_64_XEXECRegClass;
  case -1:
    return nullptr;
  default:
    return AMDGPUGenRegisterInfo::getRegClass(RCID);
  }
}

// Find reaching register definition
MachineInstr *SIRegisterInfo::findReachingDef(Register Reg, unsigned SubReg,
                                              MachineInstr &Use,
                                              MachineRegisterInfo &MRI,
                                              LiveIntervals *LIS) const {
  auto &MDT = LIS->getDomTree();
  SlotIndex UseIdx = LIS->getInstructionIndex(Use);
  SlotIndex DefIdx;

  if (Reg.isVirtual()) {
    if (!LIS->hasInterval(Reg))
      return nullptr;
    LiveInterval &LI = LIS->getInterval(Reg);
    LaneBitmask SubLanes = SubReg ? getSubRegIndexLaneMask(SubReg)
                                  : MRI.getMaxLaneMaskForVReg(Reg);
    VNInfo *V = nullptr;
    if (LI.hasSubRanges()) {
      for (auto &S : LI.subranges()) {
        if ((S.LaneMask & SubLanes) == SubLanes) {
          V = S.getVNInfoAt(UseIdx);
          break;
        }
      }
    } else {
      V = LI.getVNInfoAt(UseIdx);
    }
    if (!V)
      return nullptr;
    DefIdx = V->def;
  } else {
    // Find last def.
    for (MCRegUnit Unit : regunits(Reg.asMCReg())) {
      LiveRange &LR = LIS->getRegUnit(Unit);
      if (VNInfo *V = LR.getVNInfoAt(UseIdx)) {
        if (!DefIdx.isValid() ||
            MDT.dominates(LIS->getInstructionFromIndex(DefIdx),
                          LIS->getInstructionFromIndex(V->def)))
          DefIdx = V->def;
      } else {
        return nullptr;
      }
    }
  }

  MachineInstr *Def = LIS->getInstructionFromIndex(DefIdx);

  if (!Def || !MDT.dominates(Def, &Use))
    return nullptr;

  assert(Def->modifiesRegister(Reg, this));

  return Def;
}

MCPhysReg SIRegisterInfo::get32BitRegister(MCPhysReg Reg) const {
  assert(getRegSizeInBits(*getPhysRegBaseClass(Reg)) <= 32);

  for (const TargetRegisterClass &RC : { AMDGPU::VGPR_32RegClass,
                                         AMDGPU::SReg_32RegClass,
                                         AMDGPU::AGPR_32RegClass } ) {
    if (MCPhysReg Super = getMatchingSuperReg(Reg, AMDGPU::lo16, &RC))
      return Super;
  }
  if (MCPhysReg Super = getMatchingSuperReg(Reg, AMDGPU::hi16,
                                            &AMDGPU::VGPR_32RegClass)) {
      return Super;
  }

  return AMDGPU::NoRegister;
}

bool SIRegisterInfo::isProperlyAlignedRC(const TargetRegisterClass &RC) const {
  if (!ST.needsAlignedVGPRs())
    return true;

  if (isVGPRClass(&RC))
    return RC.hasSuperClassEq(getVGPRClassForBitWidth(getRegSizeInBits(RC)));
  if (isAGPRClass(&RC))
    return RC.hasSuperClassEq(getAGPRClassForBitWidth(getRegSizeInBits(RC)));
  if (isVectorSuperClass(&RC))
    return RC.hasSuperClassEq(
        getVectorSuperClassForBitWidth(getRegSizeInBits(RC)));

  return true;
}

const TargetRegisterClass *
SIRegisterInfo::getProperlyAlignedRC(const TargetRegisterClass *RC) const {
  if (!RC || !ST.needsAlignedVGPRs())
    return RC;

  unsigned Size = getRegSizeInBits(*RC);
  if (Size <= 32)
    return RC;

  if (isVGPRClass(RC))
    return getAlignedVGPRClassForBitWidth(Size);
  if (isAGPRClass(RC))
    return getAlignedAGPRClassForBitWidth(Size);
  if (isVectorSuperClass(RC))
    return getAlignedVectorSuperClassForBitWidth(Size);

  return RC;
}

ArrayRef<MCPhysReg>
SIRegisterInfo::getAllSGPR128(const MachineFunction &MF) const {
  return ArrayRef(AMDGPU::SGPR_128RegClass.begin(), ST.getMaxNumSGPRs(MF) / 4);
}

ArrayRef<MCPhysReg>
SIRegisterInfo::getAllSGPR64(const MachineFunction &MF) const {
  return ArrayRef(AMDGPU::SGPR_64RegClass.begin(), ST.getMaxNumSGPRs(MF) / 2);
}

ArrayRef<MCPhysReg>
SIRegisterInfo::getAllSGPR32(const MachineFunction &MF) const {
  return ArrayRef(AMDGPU::SGPR_32RegClass.begin(), ST.getMaxNumSGPRs(MF));
}

unsigned
SIRegisterInfo::getSubRegAlignmentNumBits(const TargetRegisterClass *RC,
                                          unsigned SubReg) const {
  switch (RC->TSFlags & SIRCFlags::RegKindMask) {
  case SIRCFlags::HasSGPR:
    return std::min(128u, getSubRegIdxSize(SubReg));
  case SIRCFlags::HasAGPR:
  case SIRCFlags::HasVGPR:
  case SIRCFlags::HasVGPR | SIRCFlags::HasAGPR:
    return std::min(32u, getSubRegIdxSize(SubReg));
  default:
    break;
  }
  return 0;
}
