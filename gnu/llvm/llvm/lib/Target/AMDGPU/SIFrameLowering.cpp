//===----------------------- SIFrameLowering.cpp --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//==-----------------------------------------------------------------------===//

#include "SIFrameLowering.h"
#include "AMDGPU.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIMachineFunctionInfo.h"
#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "frame-info"

static cl::opt<bool> EnableSpillVGPRToAGPR(
  "amdgpu-spill-vgpr-to-agpr",
  cl::desc("Enable spilling VGPRs to AGPRs"),
  cl::ReallyHidden,
  cl::init(true));

// Find a register matching \p RC from \p LiveUnits which is unused and
// available throughout the function. On failure, returns AMDGPU::NoRegister.
// TODO: Rewrite the loop here to iterate over MCRegUnits instead of
// MCRegisters. This should reduce the number of iterations and avoid redundant
// checking.
static MCRegister findUnusedRegister(MachineRegisterInfo &MRI,
                                     const LiveRegUnits &LiveUnits,
                                     const TargetRegisterClass &RC) {
  for (MCRegister Reg : RC) {
    if (!MRI.isPhysRegUsed(Reg) && LiveUnits.available(Reg) &&
        !MRI.isReserved(Reg))
      return Reg;
  }
  return MCRegister();
}

// Find a scratch register that we can use in the prologue. We avoid using
// callee-save registers since they may appear to be free when this is called
// from canUseAsPrologue (during shrink wrapping), but then no longer be free
// when this is called from emitPrologue.
static MCRegister findScratchNonCalleeSaveRegister(
    MachineRegisterInfo &MRI, LiveRegUnits &LiveUnits,
    const TargetRegisterClass &RC, bool Unused = false) {
  // Mark callee saved registers as used so we will not choose them.
  const MCPhysReg *CSRegs = MRI.getCalleeSavedRegs();
  for (unsigned i = 0; CSRegs[i]; ++i)
    LiveUnits.addReg(CSRegs[i]);

  // We are looking for a register that can be used throughout the entire
  // function, so any use is unacceptable.
  if (Unused)
    return findUnusedRegister(MRI, LiveUnits, RC);

  for (MCRegister Reg : RC) {
    if (LiveUnits.available(Reg) && !MRI.isReserved(Reg))
      return Reg;
  }

  return MCRegister();
}

/// Query target location for spilling SGPRs
/// \p IncludeScratchCopy : Also look for free scratch SGPRs
static void getVGPRSpillLaneOrTempRegister(
    MachineFunction &MF, LiveRegUnits &LiveUnits, Register SGPR,
    const TargetRegisterClass &RC = AMDGPU::SReg_32_XM0_XEXECRegClass,
    bool IncludeScratchCopy = true) {
  SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  MachineFrameInfo &FrameInfo = MF.getFrameInfo();

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  unsigned Size = TRI->getSpillSize(RC);
  Align Alignment = TRI->getSpillAlign(RC);

  // We need to save and restore the given SGPR.

  Register ScratchSGPR;
  // 1: Try to save the given register into an unused scratch SGPR. The
  // LiveUnits should have all the callee saved registers marked as used. For
  // certain cases we skip copy to scratch SGPR.
  if (IncludeScratchCopy)
    ScratchSGPR = findUnusedRegister(MF.getRegInfo(), LiveUnits, RC);

  if (!ScratchSGPR) {
    int FI = FrameInfo.CreateStackObject(Size, Alignment, true, nullptr,
                                         TargetStackID::SGPRSpill);

    if (TRI->spillSGPRToVGPR() &&
        MFI->allocateSGPRSpillToVGPRLane(MF, FI, /*SpillToPhysVGPRLane=*/true,
                                         /*IsPrologEpilog=*/true)) {
      // 2: There's no free lane to spill, and no free register to save the
      // SGPR, so we're forced to take another VGPR to use for the spill.
      MFI->addToPrologEpilogSGPRSpills(
          SGPR, PrologEpilogSGPRSaveRestoreInfo(
                    SGPRSaveKind::SPILL_TO_VGPR_LANE, FI));

      LLVM_DEBUG(auto Spill = MFI->getSGPRSpillToPhysicalVGPRLanes(FI).front();
                 dbgs() << printReg(SGPR, TRI) << " requires fallback spill to "
                        << printReg(Spill.VGPR, TRI) << ':' << Spill.Lane
                        << '\n';);
    } else {
      // Remove dead <FI> index
      MF.getFrameInfo().RemoveStackObject(FI);
      // 3: If all else fails, spill the register to memory.
      FI = FrameInfo.CreateSpillStackObject(Size, Alignment);
      MFI->addToPrologEpilogSGPRSpills(
          SGPR,
          PrologEpilogSGPRSaveRestoreInfo(SGPRSaveKind::SPILL_TO_MEM, FI));
      LLVM_DEBUG(dbgs() << "Reserved FI " << FI << " for spilling "
                        << printReg(SGPR, TRI) << '\n');
    }
  } else {
    MFI->addToPrologEpilogSGPRSpills(
        SGPR, PrologEpilogSGPRSaveRestoreInfo(
                  SGPRSaveKind::COPY_TO_SCRATCH_SGPR, ScratchSGPR));
    LiveUnits.addReg(ScratchSGPR);
    LLVM_DEBUG(dbgs() << "Saving " << printReg(SGPR, TRI) << " with copy to "
                      << printReg(ScratchSGPR, TRI) << '\n');
  }
}

// We need to specially emit stack operations here because a different frame
// register is used than in the rest of the function, as getFrameRegister would
// use.
static void buildPrologSpill(const GCNSubtarget &ST, const SIRegisterInfo &TRI,
                             const SIMachineFunctionInfo &FuncInfo,
                             LiveRegUnits &LiveUnits, MachineFunction &MF,
                             MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator I, const DebugLoc &DL,
                             Register SpillReg, int FI, Register FrameReg,
                             int64_t DwordOff = 0) {
  unsigned Opc = ST.enableFlatScratch() ? AMDGPU::SCRATCH_STORE_DWORD_SADDR
                                        : AMDGPU::BUFFER_STORE_DWORD_OFFSET;

  MachineFrameInfo &FrameInfo = MF.getFrameInfo();
  MachinePointerInfo PtrInfo = MachinePointerInfo::getFixedStack(MF, FI);
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      PtrInfo, MachineMemOperand::MOStore, FrameInfo.getObjectSize(FI),
      FrameInfo.getObjectAlign(FI));
  LiveUnits.addReg(SpillReg);
  bool IsKill = !MBB.isLiveIn(SpillReg);
  TRI.buildSpillLoadStore(MBB, I, DL, Opc, FI, SpillReg, IsKill, FrameReg,
                          DwordOff, MMO, nullptr, &LiveUnits);
  if (IsKill)
    LiveUnits.removeReg(SpillReg);
}

static void buildEpilogRestore(const GCNSubtarget &ST,
                               const SIRegisterInfo &TRI,
                               const SIMachineFunctionInfo &FuncInfo,
                               LiveRegUnits &LiveUnits, MachineFunction &MF,
                               MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator I,
                               const DebugLoc &DL, Register SpillReg, int FI,
                               Register FrameReg, int64_t DwordOff = 0) {
  unsigned Opc = ST.enableFlatScratch() ? AMDGPU::SCRATCH_LOAD_DWORD_SADDR
                                        : AMDGPU::BUFFER_LOAD_DWORD_OFFSET;

  MachineFrameInfo &FrameInfo = MF.getFrameInfo();
  MachinePointerInfo PtrInfo = MachinePointerInfo::getFixedStack(MF, FI);
  MachineMemOperand *MMO = MF.getMachineMemOperand(
      PtrInfo, MachineMemOperand::MOLoad, FrameInfo.getObjectSize(FI),
      FrameInfo.getObjectAlign(FI));
  TRI.buildSpillLoadStore(MBB, I, DL, Opc, FI, SpillReg, false, FrameReg,
                          DwordOff, MMO, nullptr, &LiveUnits);
}

static void buildGitPtr(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                        const DebugLoc &DL, const SIInstrInfo *TII,
                        Register TargetReg) {
  MachineFunction *MF = MBB.getParent();
  const SIMachineFunctionInfo *MFI = MF->getInfo<SIMachineFunctionInfo>();
  const SIRegisterInfo *TRI = &TII->getRegisterInfo();
  const MCInstrDesc &SMovB32 = TII->get(AMDGPU::S_MOV_B32);
  Register TargetLo = TRI->getSubReg(TargetReg, AMDGPU::sub0);
  Register TargetHi = TRI->getSubReg(TargetReg, AMDGPU::sub1);

  if (MFI->getGITPtrHigh() != 0xffffffff) {
    BuildMI(MBB, I, DL, SMovB32, TargetHi)
        .addImm(MFI->getGITPtrHigh())
        .addReg(TargetReg, RegState::ImplicitDefine);
  } else {
    const MCInstrDesc &GetPC64 = TII->get(AMDGPU::S_GETPC_B64_pseudo);
    BuildMI(MBB, I, DL, GetPC64, TargetReg);
  }
  Register GitPtrLo = MFI->getGITPtrLoReg(*MF);
  MF->getRegInfo().addLiveIn(GitPtrLo);
  MBB.addLiveIn(GitPtrLo);
  BuildMI(MBB, I, DL, SMovB32, TargetLo)
    .addReg(GitPtrLo);
}

static void initLiveUnits(LiveRegUnits &LiveUnits, const SIRegisterInfo &TRI,
                          const SIMachineFunctionInfo *FuncInfo,
                          MachineFunction &MF, MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator MBBI, bool IsProlog) {
  if (LiveUnits.empty()) {
    LiveUnits.init(TRI);
    if (IsProlog) {
      LiveUnits.addLiveIns(MBB);
    } else {
      // In epilog.
      LiveUnits.addLiveOuts(MBB);
      LiveUnits.stepBackward(*MBBI);
    }
  }
}

namespace llvm {

// SpillBuilder to save/restore special SGPR spills like the one needed for FP,
// BP, etc. These spills are delayed until the current function's frame is
// finalized. For a given register, the builder uses the
// PrologEpilogSGPRSaveRestoreInfo to decide the spill method.
class PrologEpilogSGPRSpillBuilder {
  MachineBasicBlock::iterator MI;
  MachineBasicBlock &MBB;
  MachineFunction &MF;
  const GCNSubtarget &ST;
  MachineFrameInfo &MFI;
  SIMachineFunctionInfo *FuncInfo;
  const SIInstrInfo *TII;
  const SIRegisterInfo &TRI;
  Register SuperReg;
  const PrologEpilogSGPRSaveRestoreInfo SI;
  LiveRegUnits &LiveUnits;
  const DebugLoc &DL;
  Register FrameReg;
  ArrayRef<int16_t> SplitParts;
  unsigned NumSubRegs;
  unsigned EltSize = 4;

  void saveToMemory(const int FI) const {
    MachineRegisterInfo &MRI = MF.getRegInfo();
    assert(!MFI.isDeadObjectIndex(FI));

    initLiveUnits(LiveUnits, TRI, FuncInfo, MF, MBB, MI, /*IsProlog*/ true);

    MCPhysReg TmpVGPR = findScratchNonCalleeSaveRegister(
        MRI, LiveUnits, AMDGPU::VGPR_32RegClass);
    if (!TmpVGPR)
      report_fatal_error("failed to find free scratch register");

    for (unsigned I = 0, DwordOff = 0; I < NumSubRegs; ++I) {
      Register SubReg = NumSubRegs == 1
                            ? SuperReg
                            : Register(TRI.getSubReg(SuperReg, SplitParts[I]));
      BuildMI(MBB, MI, DL, TII->get(AMDGPU::V_MOV_B32_e32), TmpVGPR)
          .addReg(SubReg);

      buildPrologSpill(ST, TRI, *FuncInfo, LiveUnits, MF, MBB, MI, DL, TmpVGPR,
                       FI, FrameReg, DwordOff);
      DwordOff += 4;
    }
  }

  void saveToVGPRLane(const int FI) const {
    assert(!MFI.isDeadObjectIndex(FI));

    assert(MFI.getStackID(FI) == TargetStackID::SGPRSpill);
    ArrayRef<SIRegisterInfo::SpilledReg> Spill =
        FuncInfo->getSGPRSpillToPhysicalVGPRLanes(FI);
    assert(Spill.size() == NumSubRegs);

    for (unsigned I = 0; I < NumSubRegs; ++I) {
      Register SubReg = NumSubRegs == 1
                            ? SuperReg
                            : Register(TRI.getSubReg(SuperReg, SplitParts[I]));
      BuildMI(MBB, MI, DL, TII->get(AMDGPU::SI_SPILL_S32_TO_VGPR),
              Spill[I].VGPR)
          .addReg(SubReg)
          .addImm(Spill[I].Lane)
          .addReg(Spill[I].VGPR, RegState::Undef);
    }
  }

  void copyToScratchSGPR(Register DstReg) const {
    BuildMI(MBB, MI, DL, TII->get(AMDGPU::COPY), DstReg)
        .addReg(SuperReg)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  void restoreFromMemory(const int FI) {
    MachineRegisterInfo &MRI = MF.getRegInfo();

    initLiveUnits(LiveUnits, TRI, FuncInfo, MF, MBB, MI, /*IsProlog*/ false);
    MCPhysReg TmpVGPR = findScratchNonCalleeSaveRegister(
        MRI, LiveUnits, AMDGPU::VGPR_32RegClass);
    if (!TmpVGPR)
      report_fatal_error("failed to find free scratch register");

    for (unsigned I = 0, DwordOff = 0; I < NumSubRegs; ++I) {
      Register SubReg = NumSubRegs == 1
                            ? SuperReg
                            : Register(TRI.getSubReg(SuperReg, SplitParts[I]));

      buildEpilogRestore(ST, TRI, *FuncInfo, LiveUnits, MF, MBB, MI, DL,
                         TmpVGPR, FI, FrameReg, DwordOff);
      BuildMI(MBB, MI, DL, TII->get(AMDGPU::V_READFIRSTLANE_B32), SubReg)
          .addReg(TmpVGPR, RegState::Kill);
      DwordOff += 4;
    }
  }

  void restoreFromVGPRLane(const int FI) {
    assert(MFI.getStackID(FI) == TargetStackID::SGPRSpill);
    ArrayRef<SIRegisterInfo::SpilledReg> Spill =
        FuncInfo->getSGPRSpillToPhysicalVGPRLanes(FI);
    assert(Spill.size() == NumSubRegs);

    for (unsigned I = 0; I < NumSubRegs; ++I) {
      Register SubReg = NumSubRegs == 1
                            ? SuperReg
                            : Register(TRI.getSubReg(SuperReg, SplitParts[I]));
      BuildMI(MBB, MI, DL, TII->get(AMDGPU::SI_RESTORE_S32_FROM_VGPR), SubReg)
          .addReg(Spill[I].VGPR)
          .addImm(Spill[I].Lane);
    }
  }

  void copyFromScratchSGPR(Register SrcReg) const {
    BuildMI(MBB, MI, DL, TII->get(AMDGPU::COPY), SuperReg)
        .addReg(SrcReg)
        .setMIFlag(MachineInstr::FrameDestroy);
  }

public:
  PrologEpilogSGPRSpillBuilder(Register Reg,
                               const PrologEpilogSGPRSaveRestoreInfo SI,
                               MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator MI,
                               const DebugLoc &DL, const SIInstrInfo *TII,
                               const SIRegisterInfo &TRI,
                               LiveRegUnits &LiveUnits, Register FrameReg)
      : MI(MI), MBB(MBB), MF(*MBB.getParent()),
        ST(MF.getSubtarget<GCNSubtarget>()), MFI(MF.getFrameInfo()),
        FuncInfo(MF.getInfo<SIMachineFunctionInfo>()), TII(TII), TRI(TRI),
        SuperReg(Reg), SI(SI), LiveUnits(LiveUnits), DL(DL),
        FrameReg(FrameReg) {
    const TargetRegisterClass *RC = TRI.getPhysRegBaseClass(SuperReg);
    SplitParts = TRI.getRegSplitParts(RC, EltSize);
    NumSubRegs = SplitParts.empty() ? 1 : SplitParts.size();

    assert(SuperReg != AMDGPU::M0 && "m0 should never spill");
  }

  void save() {
    switch (SI.getKind()) {
    case SGPRSaveKind::SPILL_TO_MEM:
      return saveToMemory(SI.getIndex());
    case SGPRSaveKind::SPILL_TO_VGPR_LANE:
      return saveToVGPRLane(SI.getIndex());
    case SGPRSaveKind::COPY_TO_SCRATCH_SGPR:
      return copyToScratchSGPR(SI.getReg());
    }
  }

  void restore() {
    switch (SI.getKind()) {
    case SGPRSaveKind::SPILL_TO_MEM:
      return restoreFromMemory(SI.getIndex());
    case SGPRSaveKind::SPILL_TO_VGPR_LANE:
      return restoreFromVGPRLane(SI.getIndex());
    case SGPRSaveKind::COPY_TO_SCRATCH_SGPR:
      return copyFromScratchSGPR(SI.getReg());
    }
  }
};

} // namespace llvm

// Emit flat scratch setup code, assuming `MFI->hasFlatScratchInit()`
void SIFrameLowering::emitEntryFunctionFlatScratchInit(
    MachineFunction &MF, MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
    const DebugLoc &DL, Register ScratchWaveOffsetReg) const {
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo *TRI = &TII->getRegisterInfo();
  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();

  // We don't need this if we only have spills since there is no user facing
  // scratch.

  // TODO: If we know we don't have flat instructions earlier, we can omit
  // this from the input registers.
  //
  // TODO: We only need to know if we access scratch space through a flat
  // pointer. Because we only detect if flat instructions are used at all,
  // this will be used more often than necessary on VI.

  Register FlatScrInitLo;
  Register FlatScrInitHi;

  if (ST.isAmdPalOS()) {
    // Extract the scratch offset from the descriptor in the GIT
    LiveRegUnits LiveUnits;
    LiveUnits.init(*TRI);
    LiveUnits.addLiveIns(MBB);

    // Find unused reg to load flat scratch init into
    MachineRegisterInfo &MRI = MF.getRegInfo();
    Register FlatScrInit = AMDGPU::NoRegister;
    ArrayRef<MCPhysReg> AllSGPR64s = TRI->getAllSGPR64(MF);
    unsigned NumPreloaded = (MFI->getNumPreloadedSGPRs() + 1) / 2;
    AllSGPR64s = AllSGPR64s.slice(
        std::min(static_cast<unsigned>(AllSGPR64s.size()), NumPreloaded));
    Register GITPtrLoReg = MFI->getGITPtrLoReg(MF);
    for (MCPhysReg Reg : AllSGPR64s) {
      if (LiveUnits.available(Reg) && !MRI.isReserved(Reg) &&
          MRI.isAllocatable(Reg) && !TRI->isSubRegisterEq(Reg, GITPtrLoReg)) {
        FlatScrInit = Reg;
        break;
      }
    }
    assert(FlatScrInit && "Failed to find free register for scratch init");

    FlatScrInitLo = TRI->getSubReg(FlatScrInit, AMDGPU::sub0);
    FlatScrInitHi = TRI->getSubReg(FlatScrInit, AMDGPU::sub1);

    buildGitPtr(MBB, I, DL, TII, FlatScrInit);

    // We now have the GIT ptr - now get the scratch descriptor from the entry
    // at offset 0 (or offset 16 for a compute shader).
    MachinePointerInfo PtrInfo(AMDGPUAS::CONSTANT_ADDRESS);
    const MCInstrDesc &LoadDwordX2 = TII->get(AMDGPU::S_LOAD_DWORDX2_IMM);
    auto *MMO = MF.getMachineMemOperand(
        PtrInfo,
        MachineMemOperand::MOLoad | MachineMemOperand::MOInvariant |
            MachineMemOperand::MODereferenceable,
        8, Align(4));
    unsigned Offset =
        MF.getFunction().getCallingConv() == CallingConv::AMDGPU_CS ? 16 : 0;
    const GCNSubtarget &Subtarget = MF.getSubtarget<GCNSubtarget>();
    unsigned EncodedOffset = AMDGPU::convertSMRDOffsetUnits(Subtarget, Offset);
    BuildMI(MBB, I, DL, LoadDwordX2, FlatScrInit)
        .addReg(FlatScrInit)
        .addImm(EncodedOffset) // offset
        .addImm(0)             // cpol
        .addMemOperand(MMO);

    // Mask the offset in [47:0] of the descriptor
    const MCInstrDesc &SAndB32 = TII->get(AMDGPU::S_AND_B32);
    auto And = BuildMI(MBB, I, DL, SAndB32, FlatScrInitHi)
        .addReg(FlatScrInitHi)
        .addImm(0xffff);
    And->getOperand(3).setIsDead(); // Mark SCC as dead.
  } else {
    Register FlatScratchInitReg =
        MFI->getPreloadedReg(AMDGPUFunctionArgInfo::FLAT_SCRATCH_INIT);
    assert(FlatScratchInitReg);

    MachineRegisterInfo &MRI = MF.getRegInfo();
    MRI.addLiveIn(FlatScratchInitReg);
    MBB.addLiveIn(FlatScratchInitReg);

    FlatScrInitLo = TRI->getSubReg(FlatScratchInitReg, AMDGPU::sub0);
    FlatScrInitHi = TRI->getSubReg(FlatScratchInitReg, AMDGPU::sub1);
  }

  // Do a 64-bit pointer add.
  if (ST.flatScratchIsPointer()) {
    if (ST.getGeneration() >= AMDGPUSubtarget::GFX10) {
      BuildMI(MBB, I, DL, TII->get(AMDGPU::S_ADD_U32), FlatScrInitLo)
        .addReg(FlatScrInitLo)
        .addReg(ScratchWaveOffsetReg);
      auto Addc = BuildMI(MBB, I, DL, TII->get(AMDGPU::S_ADDC_U32),
                          FlatScrInitHi)
        .addReg(FlatScrInitHi)
        .addImm(0);
      Addc->getOperand(3).setIsDead(); // Mark SCC as dead.

      using namespace AMDGPU::Hwreg;
      BuildMI(MBB, I, DL, TII->get(AMDGPU::S_SETREG_B32))
          .addReg(FlatScrInitLo)
          .addImm(int16_t(HwregEncoding::encode(ID_FLAT_SCR_LO, 0, 32)));
      BuildMI(MBB, I, DL, TII->get(AMDGPU::S_SETREG_B32))
          .addReg(FlatScrInitHi)
          .addImm(int16_t(HwregEncoding::encode(ID_FLAT_SCR_HI, 0, 32)));
      return;
    }

    // For GFX9.
    BuildMI(MBB, I, DL, TII->get(AMDGPU::S_ADD_U32), AMDGPU::FLAT_SCR_LO)
      .addReg(FlatScrInitLo)
      .addReg(ScratchWaveOffsetReg);
    auto Addc = BuildMI(MBB, I, DL, TII->get(AMDGPU::S_ADDC_U32),
                        AMDGPU::FLAT_SCR_HI)
      .addReg(FlatScrInitHi)
      .addImm(0);
    Addc->getOperand(3).setIsDead(); // Mark SCC as dead.

    return;
  }

  assert(ST.getGeneration() < AMDGPUSubtarget::GFX9);

  // Copy the size in bytes.
  BuildMI(MBB, I, DL, TII->get(AMDGPU::COPY), AMDGPU::FLAT_SCR_LO)
    .addReg(FlatScrInitHi, RegState::Kill);

  // Add wave offset in bytes to private base offset.
  // See comment in AMDKernelCodeT.h for enable_sgpr_flat_scratch_init.
  BuildMI(MBB, I, DL, TII->get(AMDGPU::S_ADD_I32), FlatScrInitLo)
      .addReg(FlatScrInitLo)
      .addReg(ScratchWaveOffsetReg);

  // Convert offset to 256-byte units.
  auto LShr = BuildMI(MBB, I, DL, TII->get(AMDGPU::S_LSHR_B32),
                      AMDGPU::FLAT_SCR_HI)
    .addReg(FlatScrInitLo, RegState::Kill)
    .addImm(8);
  LShr->getOperand(3).setIsDead(); // Mark SCC as dead.
}

// Note SGPRSpill stack IDs should only be used for SGPR spilling to VGPRs, not
// memory. They should have been removed by now.
static bool allStackObjectsAreDead(const MachineFrameInfo &MFI) {
  for (int I = MFI.getObjectIndexBegin(), E = MFI.getObjectIndexEnd();
       I != E; ++I) {
    if (!MFI.isDeadObjectIndex(I))
      return false;
  }

  return true;
}

// Shift down registers reserved for the scratch RSRC.
Register SIFrameLowering::getEntryFunctionReservedScratchRsrcReg(
    MachineFunction &MF) const {

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo *TRI = &TII->getRegisterInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();

  assert(MFI->isEntryFunction());

  Register ScratchRsrcReg = MFI->getScratchRSrcReg();

  if (!ScratchRsrcReg || (!MRI.isPhysRegUsed(ScratchRsrcReg) &&
                          allStackObjectsAreDead(MF.getFrameInfo())))
    return Register();

  if (ST.hasSGPRInitBug() ||
      ScratchRsrcReg != TRI->reservedPrivateSegmentBufferReg(MF))
    return ScratchRsrcReg;

  // We reserved the last registers for this. Shift it down to the end of those
  // which were actually used.
  //
  // FIXME: It might be safer to use a pseudoregister before replacement.

  // FIXME: We should be able to eliminate unused input registers. We only
  // cannot do this for the resources required for scratch access. For now we
  // skip over user SGPRs and may leave unused holes.

  unsigned NumPreloaded = (MFI->getNumPreloadedSGPRs() + 3) / 4;
  ArrayRef<MCPhysReg> AllSGPR128s = TRI->getAllSGPR128(MF);
  AllSGPR128s = AllSGPR128s.slice(std::min(static_cast<unsigned>(AllSGPR128s.size()), NumPreloaded));

  // Skip the last N reserved elements because they should have already been
  // reserved for VCC etc.
  Register GITPtrLoReg = MFI->getGITPtrLoReg(MF);
  for (MCPhysReg Reg : AllSGPR128s) {
    // Pick the first unallocated one. Make sure we don't clobber the other
    // reserved input we needed. Also for PAL, make sure we don't clobber
    // the GIT pointer passed in SGPR0 or SGPR8.
    if (!MRI.isPhysRegUsed(Reg) && MRI.isAllocatable(Reg) &&
        (!GITPtrLoReg || !TRI->isSubRegisterEq(Reg, GITPtrLoReg))) {
      MRI.replaceRegWith(ScratchRsrcReg, Reg);
      MFI->setScratchRSrcReg(Reg);
      MRI.reserveReg(Reg, TRI);
      return Reg;
    }
  }

  return ScratchRsrcReg;
}

static unsigned getScratchScaleFactor(const GCNSubtarget &ST) {
  return ST.enableFlatScratch() ? 1 : ST.getWavefrontSize();
}

void SIFrameLowering::emitEntryFunctionPrologue(MachineFunction &MF,
                                                MachineBasicBlock &MBB) const {
  assert(&MF.front() == &MBB && "Shrink-wrapping not yet supported");

  // FIXME: If we only have SGPR spills, we won't actually be using scratch
  // memory since these spill to VGPRs. We should be cleaning up these unused
  // SGPR spill frame indices somewhere.

  // FIXME: We still have implicit uses on SGPR spill instructions in case they
  // need to spill to vector memory. It's likely that will not happen, but at
  // this point it appears we need the setup. This part of the prolog should be
  // emitted after frame indices are eliminated.

  // FIXME: Remove all of the isPhysRegUsed checks

  SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo *TRI = &TII->getRegisterInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const Function &F = MF.getFunction();
  MachineFrameInfo &FrameInfo = MF.getFrameInfo();

  assert(MFI->isEntryFunction());

  Register PreloadedScratchWaveOffsetReg = MFI->getPreloadedReg(
      AMDGPUFunctionArgInfo::PRIVATE_SEGMENT_WAVE_BYTE_OFFSET);

  // We need to do the replacement of the private segment buffer register even
  // if there are no stack objects. There could be stores to undef or a
  // constant without an associated object.
  //
  // This will return `Register()` in cases where there are no actual
  // uses of the SRSRC.
  Register ScratchRsrcReg;
  if (!ST.enableFlatScratch())
    ScratchRsrcReg = getEntryFunctionReservedScratchRsrcReg(MF);

  // Make the selected register live throughout the function.
  if (ScratchRsrcReg) {
    for (MachineBasicBlock &OtherBB : MF) {
      if (&OtherBB != &MBB) {
        OtherBB.addLiveIn(ScratchRsrcReg);
      }
    }
  }

  // Now that we have fixed the reserved SRSRC we need to locate the
  // (potentially) preloaded SRSRC.
  Register PreloadedScratchRsrcReg;
  if (ST.isAmdHsaOrMesa(F)) {
    PreloadedScratchRsrcReg =
        MFI->getPreloadedReg(AMDGPUFunctionArgInfo::PRIVATE_SEGMENT_BUFFER);
    if (ScratchRsrcReg && PreloadedScratchRsrcReg) {
      // We added live-ins during argument lowering, but since they were not
      // used they were deleted. We're adding the uses now, so add them back.
      MRI.addLiveIn(PreloadedScratchRsrcReg);
      MBB.addLiveIn(PreloadedScratchRsrcReg);
    }
  }

  // Debug location must be unknown since the first debug location is used to
  // determine the end of the prologue.
  DebugLoc DL;
  MachineBasicBlock::iterator I = MBB.begin();

  // We found the SRSRC first because it needs four registers and has an
  // alignment requirement. If the SRSRC that we found is clobbering with
  // the scratch wave offset, which may be in a fixed SGPR or a free SGPR
  // chosen by SITargetLowering::allocateSystemSGPRs, COPY the scratch
  // wave offset to a free SGPR.
  Register ScratchWaveOffsetReg;
  if (PreloadedScratchWaveOffsetReg &&
      TRI->isSubRegisterEq(ScratchRsrcReg, PreloadedScratchWaveOffsetReg)) {
    ArrayRef<MCPhysReg> AllSGPRs = TRI->getAllSGPR32(MF);
    unsigned NumPreloaded = MFI->getNumPreloadedSGPRs();
    AllSGPRs = AllSGPRs.slice(
        std::min(static_cast<unsigned>(AllSGPRs.size()), NumPreloaded));
    Register GITPtrLoReg = MFI->getGITPtrLoReg(MF);
    for (MCPhysReg Reg : AllSGPRs) {
      if (!MRI.isPhysRegUsed(Reg) && MRI.isAllocatable(Reg) &&
          !TRI->isSubRegisterEq(ScratchRsrcReg, Reg) && GITPtrLoReg != Reg) {
        ScratchWaveOffsetReg = Reg;
        BuildMI(MBB, I, DL, TII->get(AMDGPU::COPY), ScratchWaveOffsetReg)
            .addReg(PreloadedScratchWaveOffsetReg, RegState::Kill);
        break;
      }
    }

    // FIXME: We can spill incoming arguments and restore at the end of the
    // prolog.
    if (!ScratchWaveOffsetReg)
      report_fatal_error(
          "could not find temporary scratch offset register in prolog");
  } else {
    ScratchWaveOffsetReg = PreloadedScratchWaveOffsetReg;
  }
  assert(ScratchWaveOffsetReg || !PreloadedScratchWaveOffsetReg);

  if (hasFP(MF)) {
    Register FPReg = MFI->getFrameOffsetReg();
    assert(FPReg != AMDGPU::FP_REG);
    BuildMI(MBB, I, DL, TII->get(AMDGPU::S_MOV_B32), FPReg).addImm(0);
  }

  if (requiresStackPointerReference(MF)) {
    Register SPReg = MFI->getStackPtrOffsetReg();
    assert(SPReg != AMDGPU::SP_REG);
    BuildMI(MBB, I, DL, TII->get(AMDGPU::S_MOV_B32), SPReg)
        .addImm(FrameInfo.getStackSize() * getScratchScaleFactor(ST));
  }

  bool NeedsFlatScratchInit =
      MFI->getUserSGPRInfo().hasFlatScratchInit() &&
      (MRI.isPhysRegUsed(AMDGPU::FLAT_SCR) || FrameInfo.hasCalls() ||
       (!allStackObjectsAreDead(FrameInfo) && ST.enableFlatScratch()));

  if ((NeedsFlatScratchInit || ScratchRsrcReg) &&
      PreloadedScratchWaveOffsetReg && !ST.flatScratchIsArchitected()) {
    MRI.addLiveIn(PreloadedScratchWaveOffsetReg);
    MBB.addLiveIn(PreloadedScratchWaveOffsetReg);
  }

  if (NeedsFlatScratchInit) {
    emitEntryFunctionFlatScratchInit(MF, MBB, I, DL, ScratchWaveOffsetReg);
  }

  if (ScratchRsrcReg) {
    emitEntryFunctionScratchRsrcRegSetup(MF, MBB, I, DL,
                                         PreloadedScratchRsrcReg,
                                         ScratchRsrcReg, ScratchWaveOffsetReg);
  }
}

// Emit scratch RSRC setup code, assuming `ScratchRsrcReg != AMDGPU::NoReg`
void SIFrameLowering::emitEntryFunctionScratchRsrcRegSetup(
    MachineFunction &MF, MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
    const DebugLoc &DL, Register PreloadedScratchRsrcReg,
    Register ScratchRsrcReg, Register ScratchWaveOffsetReg) const {

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo *TRI = &TII->getRegisterInfo();
  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  const Function &Fn = MF.getFunction();

  if (ST.isAmdPalOS()) {
    // The pointer to the GIT is formed from the offset passed in and either
    // the amdgpu-git-ptr-high function attribute or the top part of the PC
    Register Rsrc01 = TRI->getSubReg(ScratchRsrcReg, AMDGPU::sub0_sub1);
    Register Rsrc03 = TRI->getSubReg(ScratchRsrcReg, AMDGPU::sub3);

    buildGitPtr(MBB, I, DL, TII, Rsrc01);

    // We now have the GIT ptr - now get the scratch descriptor from the entry
    // at offset 0 (or offset 16 for a compute shader).
    MachinePointerInfo PtrInfo(AMDGPUAS::CONSTANT_ADDRESS);
    const MCInstrDesc &LoadDwordX4 = TII->get(AMDGPU::S_LOAD_DWORDX4_IMM);
    auto MMO = MF.getMachineMemOperand(PtrInfo,
                                       MachineMemOperand::MOLoad |
                                           MachineMemOperand::MOInvariant |
                                           MachineMemOperand::MODereferenceable,
                                       16, Align(4));
    unsigned Offset = Fn.getCallingConv() == CallingConv::AMDGPU_CS ? 16 : 0;
    const GCNSubtarget &Subtarget = MF.getSubtarget<GCNSubtarget>();
    unsigned EncodedOffset = AMDGPU::convertSMRDOffsetUnits(Subtarget, Offset);
    BuildMI(MBB, I, DL, LoadDwordX4, ScratchRsrcReg)
      .addReg(Rsrc01)
      .addImm(EncodedOffset) // offset
      .addImm(0) // cpol
      .addReg(ScratchRsrcReg, RegState::ImplicitDefine)
      .addMemOperand(MMO);

    // The driver will always set the SRD for wave 64 (bits 118:117 of
    // descriptor / bits 22:21 of third sub-reg will be 0b11)
    // If the shader is actually wave32 we have to modify the const_index_stride
    // field of the descriptor 3rd sub-reg (bits 22:21) to 0b10 (stride=32). The
    // reason the driver does this is that there can be cases where it presents
    // 2 shaders with different wave size (e.g. VsFs).
    // TODO: convert to using SCRATCH instructions or multiple SRD buffers
    if (ST.isWave32()) {
      const MCInstrDesc &SBitsetB32 = TII->get(AMDGPU::S_BITSET0_B32);
      BuildMI(MBB, I, DL, SBitsetB32, Rsrc03)
          .addImm(21)
          .addReg(Rsrc03);
    }
  } else if (ST.isMesaGfxShader(Fn) || !PreloadedScratchRsrcReg) {
    assert(!ST.isAmdHsaOrMesa(Fn));
    const MCInstrDesc &SMovB32 = TII->get(AMDGPU::S_MOV_B32);

    Register Rsrc2 = TRI->getSubReg(ScratchRsrcReg, AMDGPU::sub2);
    Register Rsrc3 = TRI->getSubReg(ScratchRsrcReg, AMDGPU::sub3);

    // Use relocations to get the pointer, and setup the other bits manually.
    uint64_t Rsrc23 = TII->getScratchRsrcWords23();

    if (MFI->getUserSGPRInfo().hasImplicitBufferPtr()) {
      Register Rsrc01 = TRI->getSubReg(ScratchRsrcReg, AMDGPU::sub0_sub1);

      if (AMDGPU::isCompute(MF.getFunction().getCallingConv())) {
        const MCInstrDesc &Mov64 = TII->get(AMDGPU::S_MOV_B64);

        BuildMI(MBB, I, DL, Mov64, Rsrc01)
          .addReg(MFI->getImplicitBufferPtrUserSGPR())
          .addReg(ScratchRsrcReg, RegState::ImplicitDefine);
      } else {
        const MCInstrDesc &LoadDwordX2 = TII->get(AMDGPU::S_LOAD_DWORDX2_IMM);

        MachinePointerInfo PtrInfo(AMDGPUAS::CONSTANT_ADDRESS);
        auto MMO = MF.getMachineMemOperand(
            PtrInfo,
            MachineMemOperand::MOLoad | MachineMemOperand::MOInvariant |
                MachineMemOperand::MODereferenceable,
            8, Align(4));
        BuildMI(MBB, I, DL, LoadDwordX2, Rsrc01)
          .addReg(MFI->getImplicitBufferPtrUserSGPR())
          .addImm(0) // offset
          .addImm(0) // cpol
          .addMemOperand(MMO)
          .addReg(ScratchRsrcReg, RegState::ImplicitDefine);

        MF.getRegInfo().addLiveIn(MFI->getImplicitBufferPtrUserSGPR());
        MBB.addLiveIn(MFI->getImplicitBufferPtrUserSGPR());
      }
    } else {
      Register Rsrc0 = TRI->getSubReg(ScratchRsrcReg, AMDGPU::sub0);
      Register Rsrc1 = TRI->getSubReg(ScratchRsrcReg, AMDGPU::sub1);

      BuildMI(MBB, I, DL, SMovB32, Rsrc0)
        .addExternalSymbol("SCRATCH_RSRC_DWORD0")
        .addReg(ScratchRsrcReg, RegState::ImplicitDefine);

      BuildMI(MBB, I, DL, SMovB32, Rsrc1)
        .addExternalSymbol("SCRATCH_RSRC_DWORD1")
        .addReg(ScratchRsrcReg, RegState::ImplicitDefine);
    }

    BuildMI(MBB, I, DL, SMovB32, Rsrc2)
      .addImm(Rsrc23 & 0xffffffff)
      .addReg(ScratchRsrcReg, RegState::ImplicitDefine);

    BuildMI(MBB, I, DL, SMovB32, Rsrc3)
      .addImm(Rsrc23 >> 32)
      .addReg(ScratchRsrcReg, RegState::ImplicitDefine);
  } else if (ST.isAmdHsaOrMesa(Fn)) {
    assert(PreloadedScratchRsrcReg);

    if (ScratchRsrcReg != PreloadedScratchRsrcReg) {
      BuildMI(MBB, I, DL, TII->get(AMDGPU::COPY), ScratchRsrcReg)
          .addReg(PreloadedScratchRsrcReg, RegState::Kill);
    }
  }

  // Add the scratch wave offset into the scratch RSRC.
  //
  // We only want to update the first 48 bits, which is the base address
  // pointer, without touching the adjacent 16 bits of flags. We know this add
  // cannot carry-out from bit 47, otherwise the scratch allocation would be
  // impossible to fit in the 48-bit global address space.
  //
  // TODO: Evaluate if it is better to just construct an SRD using the flat
  // scratch init and some constants rather than update the one we are passed.
  Register ScratchRsrcSub0 = TRI->getSubReg(ScratchRsrcReg, AMDGPU::sub0);
  Register ScratchRsrcSub1 = TRI->getSubReg(ScratchRsrcReg, AMDGPU::sub1);

  // We cannot Kill ScratchWaveOffsetReg here because we allow it to be used in
  // the kernel body via inreg arguments.
  BuildMI(MBB, I, DL, TII->get(AMDGPU::S_ADD_U32), ScratchRsrcSub0)
      .addReg(ScratchRsrcSub0)
      .addReg(ScratchWaveOffsetReg)
      .addReg(ScratchRsrcReg, RegState::ImplicitDefine);
  auto Addc = BuildMI(MBB, I, DL, TII->get(AMDGPU::S_ADDC_U32), ScratchRsrcSub1)
      .addReg(ScratchRsrcSub1)
      .addImm(0)
      .addReg(ScratchRsrcReg, RegState::ImplicitDefine);
  Addc->getOperand(3).setIsDead(); // Mark SCC as dead.
}

bool SIFrameLowering::isSupportedStackID(TargetStackID::Value ID) const {
  switch (ID) {
  case TargetStackID::Default:
  case TargetStackID::NoAlloc:
  case TargetStackID::SGPRSpill:
    return true;
  case TargetStackID::ScalableVector:
  case TargetStackID::WasmLocal:
    return false;
  }
  llvm_unreachable("Invalid TargetStackID::Value");
}

// Activate only the inactive lanes when \p EnableInactiveLanes is true.
// Otherwise, activate all lanes. It returns the saved exec.
static Register buildScratchExecCopy(LiveRegUnits &LiveUnits,
                                     MachineFunction &MF,
                                     MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator MBBI,
                                     const DebugLoc &DL, bool IsProlog,
                                     bool EnableInactiveLanes) {
  Register ScratchExecCopy;
  MachineRegisterInfo &MRI = MF.getRegInfo();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo &TRI = TII->getRegisterInfo();
  SIMachineFunctionInfo *FuncInfo = MF.getInfo<SIMachineFunctionInfo>();

  initLiveUnits(LiveUnits, TRI, FuncInfo, MF, MBB, MBBI, IsProlog);

  ScratchExecCopy = findScratchNonCalleeSaveRegister(
      MRI, LiveUnits, *TRI.getWaveMaskRegClass());
  if (!ScratchExecCopy)
    report_fatal_error("failed to find free scratch register");

  LiveUnits.addReg(ScratchExecCopy);

  const unsigned SaveExecOpc =
      ST.isWave32() ? (EnableInactiveLanes ? AMDGPU::S_XOR_SAVEEXEC_B32
                                           : AMDGPU::S_OR_SAVEEXEC_B32)
                    : (EnableInactiveLanes ? AMDGPU::S_XOR_SAVEEXEC_B64
                                           : AMDGPU::S_OR_SAVEEXEC_B64);
  auto SaveExec =
      BuildMI(MBB, MBBI, DL, TII->get(SaveExecOpc), ScratchExecCopy).addImm(-1);
  SaveExec->getOperand(3).setIsDead(); // Mark SCC as dead.

  return ScratchExecCopy;
}

void SIFrameLowering::emitCSRSpillStores(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MBBI, DebugLoc &DL, LiveRegUnits &LiveUnits,
    Register FrameReg, Register FramePtrRegScratchCopy) const {
  SIMachineFunctionInfo *FuncInfo = MF.getInfo<SIMachineFunctionInfo>();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo &TRI = TII->getRegisterInfo();

  // Spill Whole-Wave Mode VGPRs. Save only the inactive lanes of the scratch
  // registers. However, save all lanes of callee-saved VGPRs. Due to this, we
  // might end up flipping the EXEC bits twice.
  Register ScratchExecCopy;
  SmallVector<std::pair<Register, int>, 2> WWMCalleeSavedRegs, WWMScratchRegs;
  FuncInfo->splitWWMSpillRegisters(MF, WWMCalleeSavedRegs, WWMScratchRegs);
  if (!WWMScratchRegs.empty())
    ScratchExecCopy =
        buildScratchExecCopy(LiveUnits, MF, MBB, MBBI, DL,
                             /*IsProlog*/ true, /*EnableInactiveLanes*/ true);

  auto StoreWWMRegisters =
      [&](SmallVectorImpl<std::pair<Register, int>> &WWMRegs) {
        for (const auto &Reg : WWMRegs) {
          Register VGPR = Reg.first;
          int FI = Reg.second;
          buildPrologSpill(ST, TRI, *FuncInfo, LiveUnits, MF, MBB, MBBI, DL,
                           VGPR, FI, FrameReg);
        }
      };

  StoreWWMRegisters(WWMScratchRegs);
  if (!WWMCalleeSavedRegs.empty()) {
    if (ScratchExecCopy) {
      unsigned MovOpc = ST.isWave32() ? AMDGPU::S_MOV_B32 : AMDGPU::S_MOV_B64;
      BuildMI(MBB, MBBI, DL, TII->get(MovOpc), TRI.getExec()).addImm(-1);
    } else {
      ScratchExecCopy = buildScratchExecCopy(LiveUnits, MF, MBB, MBBI, DL,
                                             /*IsProlog*/ true,
                                             /*EnableInactiveLanes*/ false);
    }
  }

  StoreWWMRegisters(WWMCalleeSavedRegs);
  if (ScratchExecCopy) {
    // FIXME: Split block and make terminator.
    unsigned ExecMov = ST.isWave32() ? AMDGPU::S_MOV_B32 : AMDGPU::S_MOV_B64;
    BuildMI(MBB, MBBI, DL, TII->get(ExecMov), TRI.getExec())
        .addReg(ScratchExecCopy, RegState::Kill);
    LiveUnits.addReg(ScratchExecCopy);
  }

  Register FramePtrReg = FuncInfo->getFrameOffsetReg();

  for (const auto &Spill : FuncInfo->getPrologEpilogSGPRSpills()) {
    // Special handle FP spill:
    // Skip if FP is saved to a scratch SGPR, the save has already been emitted.
    // Otherwise, FP has been moved to a temporary register and spill it
    // instead.
    Register Reg =
        Spill.first == FramePtrReg ? FramePtrRegScratchCopy : Spill.first;
    if (!Reg)
      continue;

    PrologEpilogSGPRSpillBuilder SB(Reg, Spill.second, MBB, MBBI, DL, TII, TRI,
                                    LiveUnits, FrameReg);
    SB.save();
  }

  // If a copy to scratch SGPR has been chosen for any of the SGPR spills, make
  // such scratch registers live throughout the function.
  SmallVector<Register, 1> ScratchSGPRs;
  FuncInfo->getAllScratchSGPRCopyDstRegs(ScratchSGPRs);
  if (!ScratchSGPRs.empty()) {
    for (MachineBasicBlock &MBB : MF) {
      for (MCPhysReg Reg : ScratchSGPRs)
        MBB.addLiveIn(Reg);

      MBB.sortUniqueLiveIns();
    }
    if (!LiveUnits.empty()) {
      for (MCPhysReg Reg : ScratchSGPRs)
        LiveUnits.addReg(Reg);
    }
  }
}

void SIFrameLowering::emitCSRSpillRestores(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MBBI, DebugLoc &DL, LiveRegUnits &LiveUnits,
    Register FrameReg, Register FramePtrRegScratchCopy) const {
  const SIMachineFunctionInfo *FuncInfo = MF.getInfo<SIMachineFunctionInfo>();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo &TRI = TII->getRegisterInfo();
  Register FramePtrReg = FuncInfo->getFrameOffsetReg();

  for (const auto &Spill : FuncInfo->getPrologEpilogSGPRSpills()) {
    // Special handle FP restore:
    // Skip if FP needs to be restored from the scratch SGPR. Otherwise, restore
    // the FP value to a temporary register. The frame pointer should be
    // overwritten only at the end when all other spills are restored from
    // current frame.
    Register Reg =
        Spill.first == FramePtrReg ? FramePtrRegScratchCopy : Spill.first;
    if (!Reg)
      continue;

    PrologEpilogSGPRSpillBuilder SB(Reg, Spill.second, MBB, MBBI, DL, TII, TRI,
                                    LiveUnits, FrameReg);
    SB.restore();
  }

  // Restore Whole-Wave Mode VGPRs. Restore only the inactive lanes of the
  // scratch registers. However, restore all lanes of callee-saved VGPRs. Due to
  // this, we might end up flipping the EXEC bits twice.
  Register ScratchExecCopy;
  SmallVector<std::pair<Register, int>, 2> WWMCalleeSavedRegs, WWMScratchRegs;
  FuncInfo->splitWWMSpillRegisters(MF, WWMCalleeSavedRegs, WWMScratchRegs);
  if (!WWMScratchRegs.empty())
    ScratchExecCopy =
        buildScratchExecCopy(LiveUnits, MF, MBB, MBBI, DL,
                             /*IsProlog*/ false, /*EnableInactiveLanes*/ true);

  auto RestoreWWMRegisters =
      [&](SmallVectorImpl<std::pair<Register, int>> &WWMRegs) {
        for (const auto &Reg : WWMRegs) {
          Register VGPR = Reg.first;
          int FI = Reg.second;
          buildEpilogRestore(ST, TRI, *FuncInfo, LiveUnits, MF, MBB, MBBI, DL,
                             VGPR, FI, FrameReg);
        }
      };

  RestoreWWMRegisters(WWMScratchRegs);
  if (!WWMCalleeSavedRegs.empty()) {
    if (ScratchExecCopy) {
      unsigned MovOpc = ST.isWave32() ? AMDGPU::S_MOV_B32 : AMDGPU::S_MOV_B64;
      BuildMI(MBB, MBBI, DL, TII->get(MovOpc), TRI.getExec()).addImm(-1);
    } else {
      ScratchExecCopy = buildScratchExecCopy(LiveUnits, MF, MBB, MBBI, DL,
                                             /*IsProlog*/ false,
                                             /*EnableInactiveLanes*/ false);
    }
  }

  RestoreWWMRegisters(WWMCalleeSavedRegs);
  if (ScratchExecCopy) {
    // FIXME: Split block and make terminator.
    unsigned ExecMov = ST.isWave32() ? AMDGPU::S_MOV_B32 : AMDGPU::S_MOV_B64;
    BuildMI(MBB, MBBI, DL, TII->get(ExecMov), TRI.getExec())
        .addReg(ScratchExecCopy, RegState::Kill);
  }
}

void SIFrameLowering::emitPrologue(MachineFunction &MF,
                                   MachineBasicBlock &MBB) const {
  SIMachineFunctionInfo *FuncInfo = MF.getInfo<SIMachineFunctionInfo>();
  if (FuncInfo->isEntryFunction()) {
    emitEntryFunctionPrologue(MF, MBB);
    return;
  }

  MachineFrameInfo &MFI = MF.getFrameInfo();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo &TRI = TII->getRegisterInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();

  Register StackPtrReg = FuncInfo->getStackPtrOffsetReg();
  Register FramePtrReg = FuncInfo->getFrameOffsetReg();
  Register BasePtrReg =
      TRI.hasBasePointer(MF) ? TRI.getBaseRegister() : Register();
  LiveRegUnits LiveUnits;

  MachineBasicBlock::iterator MBBI = MBB.begin();
  // DebugLoc must be unknown since the first instruction with DebugLoc is used
  // to determine the end of the prologue.
  DebugLoc DL;

  if (FuncInfo->isChainFunction()) {
    // Functions with the amdgpu_cs_chain[_preserve] CC don't receive a SP, but
    // are free to set one up if they need it.
    bool UseSP = requiresStackPointerReference(MF);
    if (UseSP) {
      assert(StackPtrReg != AMDGPU::SP_REG);

      BuildMI(MBB, MBBI, DL, TII->get(AMDGPU::S_MOV_B32), StackPtrReg)
          .addImm(MFI.getStackSize() * getScratchScaleFactor(ST));
    }
  }

  bool HasFP = false;
  bool HasBP = false;
  uint32_t NumBytes = MFI.getStackSize();
  uint32_t RoundedSize = NumBytes;

  if (TRI.hasStackRealignment(MF))
    HasFP = true;

  Register FramePtrRegScratchCopy;
  if (!HasFP && !hasFP(MF)) {
    // Emit the CSR spill stores with SP base register.
    emitCSRSpillStores(MF, MBB, MBBI, DL, LiveUnits,
                       FuncInfo->isChainFunction() ? Register() : StackPtrReg,
                       FramePtrRegScratchCopy);
  } else {
    // CSR spill stores will use FP as base register.
    Register SGPRForFPSaveRestoreCopy =
        FuncInfo->getScratchSGPRCopyDstReg(FramePtrReg);

    initLiveUnits(LiveUnits, TRI, FuncInfo, MF, MBB, MBBI, /*IsProlog*/ true);
    if (SGPRForFPSaveRestoreCopy) {
      // Copy FP to the scratch register now and emit the CFI entry. It avoids
      // the extra FP copy needed in the other two cases when FP is spilled to
      // memory or to a VGPR lane.
      PrologEpilogSGPRSpillBuilder SB(
          FramePtrReg,
          FuncInfo->getPrologEpilogSGPRSaveRestoreInfo(FramePtrReg), MBB, MBBI,
          DL, TII, TRI, LiveUnits, FramePtrReg);
      SB.save();
      LiveUnits.addReg(SGPRForFPSaveRestoreCopy);
    } else {
      // Copy FP into a new scratch register so that its previous value can be
      // spilled after setting up the new frame.
      FramePtrRegScratchCopy = findScratchNonCalleeSaveRegister(
          MRI, LiveUnits, AMDGPU::SReg_32_XM0_XEXECRegClass);
      if (!FramePtrRegScratchCopy)
        report_fatal_error("failed to find free scratch register");

      LiveUnits.addReg(FramePtrRegScratchCopy);
      BuildMI(MBB, MBBI, DL, TII->get(AMDGPU::COPY), FramePtrRegScratchCopy)
          .addReg(FramePtrReg);
    }
  }

  if (HasFP) {
    const unsigned Alignment = MFI.getMaxAlign().value();

    RoundedSize += Alignment;
    if (LiveUnits.empty()) {
      LiveUnits.init(TRI);
      LiveUnits.addLiveIns(MBB);
    }

    // s_add_i32 s33, s32, NumBytes
    // s_and_b32 s33, s33, 0b111...0000
    BuildMI(MBB, MBBI, DL, TII->get(AMDGPU::S_ADD_I32), FramePtrReg)
        .addReg(StackPtrReg)
        .addImm((Alignment - 1) * getScratchScaleFactor(ST))
        .setMIFlag(MachineInstr::FrameSetup);
    auto And = BuildMI(MBB, MBBI, DL, TII->get(AMDGPU::S_AND_B32), FramePtrReg)
        .addReg(FramePtrReg, RegState::Kill)
        .addImm(-Alignment * getScratchScaleFactor(ST))
        .setMIFlag(MachineInstr::FrameSetup);
    And->getOperand(3).setIsDead(); // Mark SCC as dead.
    FuncInfo->setIsStackRealigned(true);
  } else if ((HasFP = hasFP(MF))) {
    BuildMI(MBB, MBBI, DL, TII->get(AMDGPU::COPY), FramePtrReg)
        .addReg(StackPtrReg)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  // If FP is used, emit the CSR spills with FP base register.
  if (HasFP) {
    emitCSRSpillStores(MF, MBB, MBBI, DL, LiveUnits, FramePtrReg,
                       FramePtrRegScratchCopy);
    if (FramePtrRegScratchCopy)
      LiveUnits.removeReg(FramePtrRegScratchCopy);
  }

  // If we need a base pointer, set it up here. It's whatever the value of
  // the stack pointer is at this point. Any variable size objects will be
  // allocated after this, so we can still use the base pointer to reference
  // the incoming arguments.
  if ((HasBP = TRI.hasBasePointer(MF))) {
    BuildMI(MBB, MBBI, DL, TII->get(AMDGPU::COPY), BasePtrReg)
        .addReg(StackPtrReg)
        .setMIFlag(MachineInstr::FrameSetup);
  }

  if (HasFP && RoundedSize != 0) {
    auto Add = BuildMI(MBB, MBBI, DL, TII->get(AMDGPU::S_ADD_I32), StackPtrReg)
        .addReg(StackPtrReg)
        .addImm(RoundedSize * getScratchScaleFactor(ST))
        .setMIFlag(MachineInstr::FrameSetup);
    Add->getOperand(3).setIsDead(); // Mark SCC as dead.
  }

  bool FPSaved = FuncInfo->hasPrologEpilogSGPRSpillEntry(FramePtrReg);
  (void)FPSaved;
  assert((!HasFP || FPSaved) &&
         "Needed to save FP but didn't save it anywhere");

  // If we allow spilling to AGPRs we may have saved FP but then spill
  // everything into AGPRs instead of the stack.
  assert((HasFP || !FPSaved || EnableSpillVGPRToAGPR) &&
         "Saved FP but didn't need it");

  bool BPSaved = FuncInfo->hasPrologEpilogSGPRSpillEntry(BasePtrReg);
  (void)BPSaved;
  assert((!HasBP || BPSaved) &&
         "Needed to save BP but didn't save it anywhere");

  assert((HasBP || !BPSaved) && "Saved BP but didn't need it");
}

void SIFrameLowering::emitEpilogue(MachineFunction &MF,
                                   MachineBasicBlock &MBB) const {
  const SIMachineFunctionInfo *FuncInfo = MF.getInfo<SIMachineFunctionInfo>();
  if (FuncInfo->isEntryFunction())
    return;

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo &TRI = TII->getRegisterInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  LiveRegUnits LiveUnits;
  // Get the insert location for the epilogue. If there were no terminators in
  // the block, get the last instruction.
  MachineBasicBlock::iterator MBBI = MBB.end();
  DebugLoc DL;
  if (!MBB.empty()) {
    MBBI = MBB.getLastNonDebugInstr();
    if (MBBI != MBB.end())
      DL = MBBI->getDebugLoc();

    MBBI = MBB.getFirstTerminator();
  }

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  uint32_t NumBytes = MFI.getStackSize();
  uint32_t RoundedSize = FuncInfo->isStackRealigned()
                             ? NumBytes + MFI.getMaxAlign().value()
                             : NumBytes;
  const Register StackPtrReg = FuncInfo->getStackPtrOffsetReg();
  Register FramePtrReg = FuncInfo->getFrameOffsetReg();
  bool FPSaved = FuncInfo->hasPrologEpilogSGPRSpillEntry(FramePtrReg);

  Register FramePtrRegScratchCopy;
  Register SGPRForFPSaveRestoreCopy =
      FuncInfo->getScratchSGPRCopyDstReg(FramePtrReg);
  if (FPSaved) {
    // CSR spill restores should use FP as base register. If
    // SGPRForFPSaveRestoreCopy is not true, restore the previous value of FP
    // into a new scratch register and copy to FP later when other registers are
    // restored from the current stack frame.
    initLiveUnits(LiveUnits, TRI, FuncInfo, MF, MBB, MBBI, /*IsProlog*/ false);
    if (SGPRForFPSaveRestoreCopy) {
      LiveUnits.addReg(SGPRForFPSaveRestoreCopy);
    } else {
      FramePtrRegScratchCopy = findScratchNonCalleeSaveRegister(
          MRI, LiveUnits, AMDGPU::SReg_32_XM0_XEXECRegClass);
      if (!FramePtrRegScratchCopy)
        report_fatal_error("failed to find free scratch register");

      LiveUnits.addReg(FramePtrRegScratchCopy);
    }

    emitCSRSpillRestores(MF, MBB, MBBI, DL, LiveUnits, FramePtrReg,
                         FramePtrRegScratchCopy);
  }

  if (RoundedSize != 0 && hasFP(MF)) {
    auto Add = BuildMI(MBB, MBBI, DL, TII->get(AMDGPU::S_ADD_I32), StackPtrReg)
        .addReg(StackPtrReg)
        .addImm(-static_cast<int64_t>(RoundedSize * getScratchScaleFactor(ST)))
        .setMIFlag(MachineInstr::FrameDestroy);
    Add->getOperand(3).setIsDead(); // Mark SCC as dead.
  }

  if (FPSaved) {
    // Insert the copy to restore FP.
    Register SrcReg = SGPRForFPSaveRestoreCopy ? SGPRForFPSaveRestoreCopy
                                               : FramePtrRegScratchCopy;
    MachineInstrBuilder MIB =
        BuildMI(MBB, MBBI, DL, TII->get(AMDGPU::COPY), FramePtrReg)
            .addReg(SrcReg);
    if (SGPRForFPSaveRestoreCopy)
      MIB.setMIFlag(MachineInstr::FrameDestroy);
  } else {
    // Insert the CSR spill restores with SP as the base register.
    emitCSRSpillRestores(MF, MBB, MBBI, DL, LiveUnits, StackPtrReg,
                         FramePtrRegScratchCopy);
  }
}

#ifndef NDEBUG
static bool allSGPRSpillsAreDead(const MachineFunction &MF) {
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const SIMachineFunctionInfo *FuncInfo = MF.getInfo<SIMachineFunctionInfo>();
  for (int I = MFI.getObjectIndexBegin(), E = MFI.getObjectIndexEnd();
       I != E; ++I) {
    if (!MFI.isDeadObjectIndex(I) &&
        MFI.getStackID(I) == TargetStackID::SGPRSpill &&
        !FuncInfo->checkIndexInPrologEpilogSGPRSpills(I)) {
      return false;
    }
  }

  return true;
}
#endif

StackOffset SIFrameLowering::getFrameIndexReference(const MachineFunction &MF,
                                                    int FI,
                                                    Register &FrameReg) const {
  const SIRegisterInfo *RI = MF.getSubtarget<GCNSubtarget>().getRegisterInfo();

  FrameReg = RI->getFrameRegister(MF);
  return StackOffset::getFixed(MF.getFrameInfo().getObjectOffset(FI));
}

void SIFrameLowering::processFunctionBeforeFrameFinalized(
  MachineFunction &MF,
  RegScavenger *RS) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  SIMachineFunctionInfo *FuncInfo = MF.getInfo<SIMachineFunctionInfo>();

  // Allocate spill slots for WWM reserved VGPRs.
  // For chain functions, we only need to do this if we have calls to
  // llvm.amdgcn.cs.chain.
  bool IsChainWithoutCalls =
      FuncInfo->isChainFunction() && !MF.getFrameInfo().hasTailCall();
  if (!FuncInfo->isEntryFunction() && !IsChainWithoutCalls) {
    for (Register Reg : FuncInfo->getWWMReservedRegs()) {
      const TargetRegisterClass *RC = TRI->getPhysRegBaseClass(Reg);
      FuncInfo->allocateWWMSpill(MF, Reg, TRI->getSpillSize(*RC),
                                 TRI->getSpillAlign(*RC));
    }
  }

  const bool SpillVGPRToAGPR = ST.hasMAIInsts() && FuncInfo->hasSpilledVGPRs()
                               && EnableSpillVGPRToAGPR;

  if (SpillVGPRToAGPR) {
    // To track the spill frame indices handled in this pass.
    BitVector SpillFIs(MFI.getObjectIndexEnd(), false);
    BitVector NonVGPRSpillFIs(MFI.getObjectIndexEnd(), false);

    bool SeenDbgInstr = false;

    for (MachineBasicBlock &MBB : MF) {
      for (MachineInstr &MI : llvm::make_early_inc_range(MBB)) {
        int FrameIndex;
        if (MI.isDebugInstr())
          SeenDbgInstr = true;

        if (TII->isVGPRSpill(MI)) {
          // Try to eliminate stack used by VGPR spills before frame
          // finalization.
          unsigned FIOp = AMDGPU::getNamedOperandIdx(MI.getOpcode(),
                                                     AMDGPU::OpName::vaddr);
          int FI = MI.getOperand(FIOp).getIndex();
          Register VReg =
            TII->getNamedOperand(MI, AMDGPU::OpName::vdata)->getReg();
          if (FuncInfo->allocateVGPRSpillToAGPR(MF, FI,
                                                TRI->isAGPR(MRI, VReg))) {
            assert(RS != nullptr);
            RS->enterBasicBlockEnd(MBB);
            RS->backward(std::next(MI.getIterator()));
            TRI->eliminateFrameIndex(MI, 0, FIOp, RS);
            SpillFIs.set(FI);
            continue;
          }
        } else if (TII->isStoreToStackSlot(MI, FrameIndex) ||
                   TII->isLoadFromStackSlot(MI, FrameIndex))
          if (!MFI.isFixedObjectIndex(FrameIndex))
            NonVGPRSpillFIs.set(FrameIndex);
      }
    }

    // Stack slot coloring may assign different objects to the same stack slot.
    // If not, then the VGPR to AGPR spill slot is dead.
    for (unsigned FI : SpillFIs.set_bits())
      if (!NonVGPRSpillFIs.test(FI))
        FuncInfo->setVGPRToAGPRSpillDead(FI);

    for (MachineBasicBlock &MBB : MF) {
      for (MCPhysReg Reg : FuncInfo->getVGPRSpillAGPRs())
        MBB.addLiveIn(Reg);

      for (MCPhysReg Reg : FuncInfo->getAGPRSpillVGPRs())
        MBB.addLiveIn(Reg);

      MBB.sortUniqueLiveIns();

      if (!SpillFIs.empty() && SeenDbgInstr) {
        // FIXME: The dead frame indices are replaced with a null register from
        // the debug value instructions. We should instead, update it with the
        // correct register value. But not sure the register value alone is
        for (MachineInstr &MI : MBB) {
          if (MI.isDebugValue() && MI.getOperand(0).isFI() &&
              !MFI.isFixedObjectIndex(MI.getOperand(0).getIndex()) &&
              SpillFIs[MI.getOperand(0).getIndex()]) {
            MI.getOperand(0).ChangeToRegister(Register(), false /*isDef*/);
          }
        }
      }
    }
  }

  // At this point we've already allocated all spilled SGPRs to VGPRs if we
  // can. Any remaining SGPR spills will go to memory, so move them back to the
  // default stack.
  bool HaveSGPRToVMemSpill =
      FuncInfo->removeDeadFrameIndices(MFI, /*ResetSGPRSpillStackIDs*/ true);
  assert(allSGPRSpillsAreDead(MF) &&
         "SGPR spill should have been removed in SILowerSGPRSpills");

  // FIXME: The other checks should be redundant with allStackObjectsAreDead,
  // but currently hasNonSpillStackObjects is set only from source
  // allocas. Stack temps produced from legalization are not counted currently.
  if (!allStackObjectsAreDead(MFI)) {
    assert(RS && "RegScavenger required if spilling");

    // Add an emergency spill slot
    RS->addScavengingFrameIndex(FuncInfo->getScavengeFI(MFI, *TRI));

    // If we are spilling SGPRs to memory with a large frame, we may need a
    // second VGPR emergency frame index.
    if (HaveSGPRToVMemSpill &&
        allocateScavengingFrameIndexesNearIncomingSP(MF)) {
      RS->addScavengingFrameIndex(MFI.CreateStackObject(4, Align(4), false));
    }
  }
}

void SIFrameLowering::processFunctionBeforeFrameIndicesReplaced(
    MachineFunction &MF, RegScavenger *RS) const {
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  SIMachineFunctionInfo *FuncInfo = MF.getInfo<SIMachineFunctionInfo>();

  if (ST.hasMAIInsts() && !ST.hasGFX90AInsts()) {
    // On gfx908, we had initially reserved highest available VGPR for AGPR
    // copy. Now since we are done with RA, check if there exist an unused VGPR
    // which is lower than the eariler reserved VGPR before RA. If one exist,
    // use it for AGPR copy instead of one reserved before RA.
    Register VGPRForAGPRCopy = FuncInfo->getVGPRForAGPRCopy();
    Register UnusedLowVGPR =
        TRI->findUnusedRegister(MRI, &AMDGPU::VGPR_32RegClass, MF);
    if (UnusedLowVGPR && (TRI->getHWRegIndex(UnusedLowVGPR) <
                          TRI->getHWRegIndex(VGPRForAGPRCopy))) {
      // Reserve this newly identified VGPR (for AGPR copy)
      // reserved registers should already be frozen at this point
      // so we can avoid calling MRI.freezeReservedRegs and just use
      // MRI.reserveReg
      FuncInfo->setVGPRForAGPRCopy(UnusedLowVGPR);
      MRI.reserveReg(UnusedLowVGPR, TRI);
    }
  }
  // We initally reserved the highest available SGPR pair for long branches
  // now, after RA, we shift down to a lower unused one if one exists
  Register LongBranchReservedReg = FuncInfo->getLongBranchReservedReg();
  Register UnusedLowSGPR =
      TRI->findUnusedRegister(MRI, &AMDGPU::SGPR_64RegClass, MF);
  // If LongBranchReservedReg is null then we didn't find a long branch
  // and never reserved a register to begin with so there is nothing to
  // shift down. Then if UnusedLowSGPR is null, there isn't available lower
  // register to use so just keep the original one we set.
  if (LongBranchReservedReg && UnusedLowSGPR) {
    FuncInfo->setLongBranchReservedReg(UnusedLowSGPR);
    MRI.reserveReg(UnusedLowSGPR, TRI);
  }
}

// The special SGPR spills like the one needed for FP, BP or any reserved
// registers delayed until frame lowering.
void SIFrameLowering::determinePrologEpilogSGPRSaves(
    MachineFunction &MF, BitVector &SavedVGPRs,
    bool NeedExecCopyReservedReg) const {
  MachineFrameInfo &FrameInfo = MF.getFrameInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  LiveRegUnits LiveUnits;
  LiveUnits.init(*TRI);
  // Initially mark callee saved registers as used so we will not choose them
  // while looking for scratch SGPRs.
  const MCPhysReg *CSRegs = MF.getRegInfo().getCalleeSavedRegs();
  for (unsigned I = 0; CSRegs[I]; ++I)
    LiveUnits.addReg(CSRegs[I]);

  const TargetRegisterClass &RC = *TRI->getWaveMaskRegClass();

  Register ReservedRegForExecCopy = MFI->getSGPRForEXECCopy();
  if (NeedExecCopyReservedReg ||
      (ReservedRegForExecCopy &&
       MRI.isPhysRegUsed(ReservedRegForExecCopy, /*SkipRegMaskTest=*/true))) {
    MRI.reserveReg(ReservedRegForExecCopy, TRI);
    Register UnusedScratchReg = findUnusedRegister(MRI, LiveUnits, RC);
    if (UnusedScratchReg) {
      // If found any unused scratch SGPR, reserve the register itself for Exec
      // copy and there is no need for any spills in that case.
      MFI->setSGPRForEXECCopy(UnusedScratchReg);
      MRI.replaceRegWith(ReservedRegForExecCopy, UnusedScratchReg);
      LiveUnits.addReg(UnusedScratchReg);
    } else {
      // Needs spill.
      assert(!MFI->hasPrologEpilogSGPRSpillEntry(ReservedRegForExecCopy) &&
             "Re-reserving spill slot for EXEC copy register");
      getVGPRSpillLaneOrTempRegister(MF, LiveUnits, ReservedRegForExecCopy, RC,
                                     /*IncludeScratchCopy=*/false);
    }
  } else if (ReservedRegForExecCopy) {
    // Reset it at this point. There are no whole-wave copies and spills
    // encountered.
    MFI->setSGPRForEXECCopy(AMDGPU::NoRegister);
  }

  // hasFP only knows about stack objects that already exist. We're now
  // determining the stack slots that will be created, so we have to predict
  // them. Stack objects force FP usage with calls.
  //
  // Note a new VGPR CSR may be introduced if one is used for the spill, but we
  // don't want to report it here.
  //
  // FIXME: Is this really hasReservedCallFrame?
  const bool WillHaveFP =
      FrameInfo.hasCalls() &&
      (SavedVGPRs.any() || !allStackObjectsAreDead(FrameInfo));

  if (WillHaveFP || hasFP(MF)) {
    Register FramePtrReg = MFI->getFrameOffsetReg();
    assert(!MFI->hasPrologEpilogSGPRSpillEntry(FramePtrReg) &&
           "Re-reserving spill slot for FP");
    getVGPRSpillLaneOrTempRegister(MF, LiveUnits, FramePtrReg);
  }

  if (TRI->hasBasePointer(MF)) {
    Register BasePtrReg = TRI->getBaseRegister();
    assert(!MFI->hasPrologEpilogSGPRSpillEntry(BasePtrReg) &&
           "Re-reserving spill slot for BP");
    getVGPRSpillLaneOrTempRegister(MF, LiveUnits, BasePtrReg);
  }
}

// Only report VGPRs to generic code.
void SIFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                           BitVector &SavedVGPRs,
                                           RegScavenger *RS) const {
  SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();

  // If this is a function with the amdgpu_cs_chain[_preserve] calling
  // convention and it doesn't contain any calls to llvm.amdgcn.cs.chain, then
  // we don't need to save and restore anything.
  if (MFI->isChainFunction() && !MF.getFrameInfo().hasTailCall())
    return;

  MFI->shiftSpillPhysVGPRsToLowestRange(MF);

  TargetFrameLowering::determineCalleeSaves(MF, SavedVGPRs, RS);
  if (MFI->isEntryFunction())
    return;

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  const SIInstrInfo *TII = ST.getInstrInfo();
  bool NeedExecCopyReservedReg = false;

  MachineInstr *ReturnMI = nullptr;
  for (MachineBasicBlock &MBB : MF) {
    for (MachineInstr &MI : MBB) {
      // WRITELANE instructions used for SGPR spills can overwrite the inactive
      // lanes of VGPRs and callee must spill and restore them even if they are
      // marked Caller-saved.

      // TODO: Handle this elsewhere at an early point. Walking through all MBBs
      // here would be a bad heuristic. A better way should be by calling
      // allocateWWMSpill during the regalloc pipeline whenever a physical
      // register is allocated for the intended virtual registers.
      if (MI.getOpcode() == AMDGPU::SI_SPILL_S32_TO_VGPR)
        MFI->allocateWWMSpill(MF, MI.getOperand(0).getReg());
      else if (MI.getOpcode() == AMDGPU::SI_RESTORE_S32_FROM_VGPR)
        MFI->allocateWWMSpill(MF, MI.getOperand(1).getReg());
      else if (TII->isWWMRegSpillOpcode(MI.getOpcode()))
        NeedExecCopyReservedReg = true;
      else if (MI.getOpcode() == AMDGPU::SI_RETURN ||
               MI.getOpcode() == AMDGPU::SI_RETURN_TO_EPILOG ||
               (MFI->isChainFunction() &&
                TII->isChainCallOpcode(MI.getOpcode()))) {
        // We expect all return to be the same size.
        assert(!ReturnMI ||
               (count_if(MI.operands(), [](auto Op) { return Op.isReg(); }) ==
                count_if(ReturnMI->operands(), [](auto Op) { return Op.isReg(); })));
        ReturnMI = &MI;
      }
    }
  }

  // Remove any VGPRs used in the return value because these do not need to be saved.
  // This prevents CSR restore from clobbering return VGPRs.
  if (ReturnMI) {
    for (auto &Op : ReturnMI->operands()) {
      if (Op.isReg())
        SavedVGPRs.reset(Op.getReg());
    }
  }

  // Ignore the SGPRs the default implementation found.
  SavedVGPRs.clearBitsNotInMask(TRI->getAllVectorRegMask());

  // Do not save AGPRs prior to GFX90A because there was no easy way to do so.
  // In gfx908 there was do AGPR loads and stores and thus spilling also
  // require a temporary VGPR.
  if (!ST.hasGFX90AInsts())
    SavedVGPRs.clearBitsInMask(TRI->getAllAGPRRegMask());

  determinePrologEpilogSGPRSaves(MF, SavedVGPRs, NeedExecCopyReservedReg);

  // The Whole-Wave VGPRs need to be specially inserted in the prolog, so don't
  // allow the default insertion to handle them.
  for (auto &Reg : MFI->getWWMSpills())
    SavedVGPRs.reset(Reg.first);

  // Mark all lane VGPRs as BB LiveIns.
  for (MachineBasicBlock &MBB : MF) {
    for (auto &Reg : MFI->getWWMSpills())
      MBB.addLiveIn(Reg.first);

    MBB.sortUniqueLiveIns();
  }
}

void SIFrameLowering::determineCalleeSavesSGPR(MachineFunction &MF,
                                               BitVector &SavedRegs,
                                               RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);
  const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
  if (MFI->isEntryFunction())
    return;

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();

  // The SP is specifically managed and we don't want extra spills of it.
  SavedRegs.reset(MFI->getStackPtrOffsetReg());

  const BitVector AllSavedRegs = SavedRegs;
  SavedRegs.clearBitsInMask(TRI->getAllVectorRegMask());

  // We have to anticipate introducing CSR VGPR spills or spill of caller
  // save VGPR reserved for SGPR spills as we now always create stack entry
  // for it, if we don't have any stack objects already, since we require a FP
  // if there is a call and stack. We will allocate a VGPR for SGPR spills if
  // there are any SGPR spills. Whether they are CSR spills or otherwise.
  MachineFrameInfo &FrameInfo = MF.getFrameInfo();
  const bool WillHaveFP =
      FrameInfo.hasCalls() && (AllSavedRegs.any() || MFI->hasSpilledSGPRs());

  // FP will be specially managed like SP.
  if (WillHaveFP || hasFP(MF))
    SavedRegs.reset(MFI->getFrameOffsetReg());

  // Return address use with return instruction is hidden through the SI_RETURN
  // pseudo. Given that and since the IPRA computes actual register usage and
  // does not use CSR list, the clobbering of return address by function calls
  // (D117243) or otherwise (D120922) is ignored/not seen by the IPRA's register
  // usage collection. This will ensure save/restore of return address happens
  // in those scenarios.
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  Register RetAddrReg = TRI->getReturnAddressReg(MF);
  if (!MFI->isEntryFunction() &&
      (FrameInfo.hasCalls() || MRI.isPhysRegModified(RetAddrReg))) {
    SavedRegs.set(TRI->getSubReg(RetAddrReg, AMDGPU::sub0));
    SavedRegs.set(TRI->getSubReg(RetAddrReg, AMDGPU::sub1));
  }
}

bool SIFrameLowering::assignCalleeSavedSpillSlots(
    MachineFunction &MF, const TargetRegisterInfo *TRI,
    std::vector<CalleeSavedInfo> &CSI) const {
  if (CSI.empty())
    return true; // Early exit if no callee saved registers are modified!

  const SIMachineFunctionInfo *FuncInfo = MF.getInfo<SIMachineFunctionInfo>();
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *RI = ST.getRegisterInfo();
  Register FramePtrReg = FuncInfo->getFrameOffsetReg();
  Register BasePtrReg = RI->getBaseRegister();
  Register SGPRForFPSaveRestoreCopy =
      FuncInfo->getScratchSGPRCopyDstReg(FramePtrReg);
  Register SGPRForBPSaveRestoreCopy =
      FuncInfo->getScratchSGPRCopyDstReg(BasePtrReg);
  if (!SGPRForFPSaveRestoreCopy && !SGPRForBPSaveRestoreCopy)
    return false;

  unsigned NumModifiedRegs = 0;

  if (SGPRForFPSaveRestoreCopy)
    NumModifiedRegs++;
  if (SGPRForBPSaveRestoreCopy)
    NumModifiedRegs++;

  for (auto &CS : CSI) {
    if (CS.getReg() == FramePtrReg && SGPRForFPSaveRestoreCopy) {
      CS.setDstReg(SGPRForFPSaveRestoreCopy);
      if (--NumModifiedRegs)
        break;
    } else if (CS.getReg() == BasePtrReg && SGPRForBPSaveRestoreCopy) {
      CS.setDstReg(SGPRForBPSaveRestoreCopy);
      if (--NumModifiedRegs)
        break;
    }
  }

  return false;
}

bool SIFrameLowering::allocateScavengingFrameIndexesNearIncomingSP(
  const MachineFunction &MF) const {

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const SIInstrInfo *TII = ST.getInstrInfo();
  uint64_t EstStackSize = MFI.estimateStackSize(MF);
  uint64_t MaxOffset = EstStackSize - 1;

  // We need the emergency stack slots to be allocated in range of the
  // MUBUF/flat scratch immediate offset from the base register, so assign these
  // first at the incoming SP position.
  //
  // TODO: We could try sorting the objects to find a hole in the first bytes
  // rather than allocating as close to possible. This could save a lot of space
  // on frames with alignment requirements.
  if (ST.enableFlatScratch()) {
    if (TII->isLegalFLATOffset(MaxOffset, AMDGPUAS::PRIVATE_ADDRESS,
                               SIInstrFlags::FlatScratch))
      return false;
  } else {
    if (TII->isLegalMUBUFImmOffset(MaxOffset))
      return false;
  }

  return true;
}

MachineBasicBlock::iterator SIFrameLowering::eliminateCallFramePseudoInstr(
  MachineFunction &MF,
  MachineBasicBlock &MBB,
  MachineBasicBlock::iterator I) const {
  int64_t Amount = I->getOperand(0).getImm();
  if (Amount == 0)
    return MBB.erase(I);

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIInstrInfo *TII = ST.getInstrInfo();
  const DebugLoc &DL = I->getDebugLoc();
  unsigned Opc = I->getOpcode();
  bool IsDestroy = Opc == TII->getCallFrameDestroyOpcode();
  uint64_t CalleePopAmount = IsDestroy ? I->getOperand(1).getImm() : 0;

  if (!hasReservedCallFrame(MF)) {
    Amount = alignTo(Amount, getStackAlign());
    assert(isUInt<32>(Amount) && "exceeded stack address space size");
    const SIMachineFunctionInfo *MFI = MF.getInfo<SIMachineFunctionInfo>();
    Register SPReg = MFI->getStackPtrOffsetReg();

    Amount *= getScratchScaleFactor(ST);
    if (IsDestroy)
      Amount = -Amount;
    auto Add = BuildMI(MBB, I, DL, TII->get(AMDGPU::S_ADD_I32), SPReg)
        .addReg(SPReg)
        .addImm(Amount);
    Add->getOperand(3).setIsDead(); // Mark SCC as dead.
  } else if (CalleePopAmount != 0) {
    llvm_unreachable("is this used?");
  }

  return MBB.erase(I);
}

/// Returns true if the frame will require a reference to the stack pointer.
///
/// This is the set of conditions common to setting up the stack pointer in a
/// kernel, and for using a frame pointer in a callable function.
///
/// FIXME: Should also check hasOpaqueSPAdjustment and if any inline asm
/// references SP.
static bool frameTriviallyRequiresSP(const MachineFrameInfo &MFI) {
  return MFI.hasVarSizedObjects() || MFI.hasStackMap() || MFI.hasPatchPoint();
}

// The FP for kernels is always known 0, so we never really need to setup an
// explicit register for it. However, DisableFramePointerElim will force us to
// use a register for it.
bool SIFrameLowering::hasFP(const MachineFunction &MF) const {
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  // For entry & chain functions we can use an immediate offset in most cases,
  // so the presence of calls doesn't imply we need a distinct frame pointer.
  if (MFI.hasCalls() &&
      !MF.getInfo<SIMachineFunctionInfo>()->isEntryFunction() &&
      !MF.getInfo<SIMachineFunctionInfo>()->isChainFunction()) {
    // All offsets are unsigned, so need to be addressed in the same direction
    // as stack growth.

    // FIXME: This function is pretty broken, since it can be called before the
    // frame layout is determined or CSR spills are inserted.
    return MFI.getStackSize() != 0;
  }

  return frameTriviallyRequiresSP(MFI) || MFI.isFrameAddressTaken() ||
         MF.getSubtarget<GCNSubtarget>().getRegisterInfo()->hasStackRealignment(
             MF) ||
         MF.getTarget().Options.DisableFramePointerElim(MF);
}

// This is essentially a reduced version of hasFP for entry functions. Since the
// stack pointer is known 0 on entry to kernels, we never really need an FP
// register. We may need to initialize the stack pointer depending on the frame
// properties, which logically overlaps many of the cases where an ordinary
// function would require an FP.
// Also used for chain functions. While not technically entry functions, chain
// functions may need to set up a stack pointer in some situations.
bool SIFrameLowering::requiresStackPointerReference(
    const MachineFunction &MF) const {
  // Callable functions always require a stack pointer reference.
  assert((MF.getInfo<SIMachineFunctionInfo>()->isEntryFunction() ||
          MF.getInfo<SIMachineFunctionInfo>()->isChainFunction()) &&
         "only expected to call this for entry points and chain functions");

  const MachineFrameInfo &MFI = MF.getFrameInfo();

  // Entry points ordinarily don't need to initialize SP. We have to set it up
  // for callees if there are any. Also note tail calls are impossible/don't
  // make any sense for kernels.
  if (MFI.hasCalls())
    return true;

  // We still need to initialize the SP if we're doing anything weird that
  // references the SP, like variable sized stack objects.
  return frameTriviallyRequiresSP(MFI);
}
