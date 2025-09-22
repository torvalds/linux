//===- SIMachineFunctionInfo.cpp - SI Machine Function Info ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SIMachineFunctionInfo.h"
#include "AMDGPUSubtarget.h"
#include "AMDGPUTargetMachine.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIRegisterInfo.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/CodeGen/LiveIntervals.h"
#include "llvm/CodeGen/MIRParser/MIParser.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include <cassert>
#include <optional>
#include <vector>

enum { MAX_LANES = 64 };

using namespace llvm;

const GCNTargetMachine &getTM(const GCNSubtarget *STI) {
  const SITargetLowering *TLI = STI->getTargetLowering();
  return static_cast<const GCNTargetMachine &>(TLI->getTargetMachine());
}

SIMachineFunctionInfo::SIMachineFunctionInfo(const Function &F,
                                             const GCNSubtarget *STI)
    : AMDGPUMachineFunction(F, *STI), Mode(F, *STI), GWSResourcePSV(getTM(STI)),
      UserSGPRInfo(F, *STI), WorkGroupIDX(false), WorkGroupIDY(false),
      WorkGroupIDZ(false), WorkGroupInfo(false), LDSKernelId(false),
      PrivateSegmentWaveByteOffset(false), WorkItemIDX(false),
      WorkItemIDY(false), WorkItemIDZ(false), ImplicitArgPtr(false),
      GITPtrHigh(0xffffffff), HighBitsOf32BitAddress(0) {
  const GCNSubtarget &ST = *static_cast<const GCNSubtarget *>(STI);
  FlatWorkGroupSizes = ST.getFlatWorkGroupSizes(F);
  WavesPerEU = ST.getWavesPerEU(F);
  MaxNumWorkGroups = ST.getMaxNumWorkGroups(F);
  assert(MaxNumWorkGroups.size() == 3);

  Occupancy = ST.computeOccupancy(F, getLDSSize());
  CallingConv::ID CC = F.getCallingConv();

  VRegFlags.reserve(1024);

  const bool IsKernel = CC == CallingConv::AMDGPU_KERNEL ||
                        CC == CallingConv::SPIR_KERNEL;

  if (IsKernel) {
    WorkGroupIDX = true;
    WorkItemIDX = true;
  } else if (CC == CallingConv::AMDGPU_PS) {
    PSInputAddr = AMDGPU::getInitialPSInputAddr(F);
  }

  MayNeedAGPRs = ST.hasMAIInsts();

  if (AMDGPU::isChainCC(CC)) {
    // Chain functions don't receive an SP from their caller, but are free to
    // set one up. For now, we can use s32 to match what amdgpu_gfx functions
    // would use if called, but this can be revisited.
    // FIXME: Only reserve this if we actually need it.
    StackPtrOffsetReg = AMDGPU::SGPR32;

    ScratchRSrcReg = AMDGPU::SGPR48_SGPR49_SGPR50_SGPR51;

    ArgInfo.PrivateSegmentBuffer =
        ArgDescriptor::createRegister(ScratchRSrcReg);

    ImplicitArgPtr = false;
  } else if (!isEntryFunction()) {
    if (CC != CallingConv::AMDGPU_Gfx)
      ArgInfo = AMDGPUArgumentUsageInfo::FixedABIFunctionInfo;

    FrameOffsetReg = AMDGPU::SGPR33;
    StackPtrOffsetReg = AMDGPU::SGPR32;

    if (!ST.enableFlatScratch()) {
      // Non-entry functions have no special inputs for now, other registers
      // required for scratch access.
      ScratchRSrcReg = AMDGPU::SGPR0_SGPR1_SGPR2_SGPR3;

      ArgInfo.PrivateSegmentBuffer =
        ArgDescriptor::createRegister(ScratchRSrcReg);
    }

    if (!F.hasFnAttribute("amdgpu-no-implicitarg-ptr"))
      ImplicitArgPtr = true;
  } else {
    ImplicitArgPtr = false;
    MaxKernArgAlign = std::max(ST.getAlignmentForImplicitArgPtr(),
                               MaxKernArgAlign);

    if (ST.hasGFX90AInsts() &&
        ST.getMaxNumVGPRs(F) <= AMDGPU::VGPR_32RegClass.getNumRegs() &&
        !mayUseAGPRs(F))
      MayNeedAGPRs = false; // We will select all MAI with VGPR operands.
  }

  if (!AMDGPU::isGraphics(CC) ||
      ((CC == CallingConv::AMDGPU_CS || CC == CallingConv::AMDGPU_Gfx) &&
       ST.hasArchitectedSGPRs())) {
    if (IsKernel || !F.hasFnAttribute("amdgpu-no-workgroup-id-x"))
      WorkGroupIDX = true;

    if (!F.hasFnAttribute("amdgpu-no-workgroup-id-y"))
      WorkGroupIDY = true;

    if (!F.hasFnAttribute("amdgpu-no-workgroup-id-z"))
      WorkGroupIDZ = true;
  }

  if (!AMDGPU::isGraphics(CC)) {
    if (IsKernel || !F.hasFnAttribute("amdgpu-no-workitem-id-x"))
      WorkItemIDX = true;

    if (!F.hasFnAttribute("amdgpu-no-workitem-id-y") &&
        ST.getMaxWorkitemID(F, 1) != 0)
      WorkItemIDY = true;

    if (!F.hasFnAttribute("amdgpu-no-workitem-id-z") &&
        ST.getMaxWorkitemID(F, 2) != 0)
      WorkItemIDZ = true;

    if (!IsKernel && !F.hasFnAttribute("amdgpu-no-lds-kernel-id"))
      LDSKernelId = true;
  }

  if (isEntryFunction()) {
    // X, XY, and XYZ are the only supported combinations, so make sure Y is
    // enabled if Z is.
    if (WorkItemIDZ)
      WorkItemIDY = true;

    if (!ST.flatScratchIsArchitected()) {
      PrivateSegmentWaveByteOffset = true;

      // HS and GS always have the scratch wave offset in SGPR5 on GFX9.
      if (ST.getGeneration() >= AMDGPUSubtarget::GFX9 &&
          (CC == CallingConv::AMDGPU_HS || CC == CallingConv::AMDGPU_GS))
        ArgInfo.PrivateSegmentWaveByteOffset =
            ArgDescriptor::createRegister(AMDGPU::SGPR5);
    }
  }

  Attribute A = F.getFnAttribute("amdgpu-git-ptr-high");
  StringRef S = A.getValueAsString();
  if (!S.empty())
    S.consumeInteger(0, GITPtrHigh);

  A = F.getFnAttribute("amdgpu-32bit-address-high-bits");
  S = A.getValueAsString();
  if (!S.empty())
    S.consumeInteger(0, HighBitsOf32BitAddress);

  // On GFX908, in order to guarantee copying between AGPRs, we need a scratch
  // VGPR available at all times. For now, reserve highest available VGPR. After
  // RA, shift it to the lowest available unused VGPR if the one exist.
  if (ST.hasMAIInsts() && !ST.hasGFX90AInsts()) {
    VGPRForAGPRCopy =
        AMDGPU::VGPR_32RegClass.getRegister(ST.getMaxNumVGPRs(F) - 1);
  }
}

MachineFunctionInfo *SIMachineFunctionInfo::clone(
    BumpPtrAllocator &Allocator, MachineFunction &DestMF,
    const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
    const {
  return DestMF.cloneInfo<SIMachineFunctionInfo>(*this);
}

void SIMachineFunctionInfo::limitOccupancy(const MachineFunction &MF) {
  limitOccupancy(getMaxWavesPerEU());
  const GCNSubtarget& ST = MF.getSubtarget<GCNSubtarget>();
  limitOccupancy(ST.getOccupancyWithLocalMemSize(getLDSSize(),
                 MF.getFunction()));
}

Register SIMachineFunctionInfo::addPrivateSegmentBuffer(
  const SIRegisterInfo &TRI) {
  ArgInfo.PrivateSegmentBuffer =
    ArgDescriptor::createRegister(TRI.getMatchingSuperReg(
    getNextUserSGPR(), AMDGPU::sub0, &AMDGPU::SGPR_128RegClass));
  NumUserSGPRs += 4;
  return ArgInfo.PrivateSegmentBuffer.getRegister();
}

Register SIMachineFunctionInfo::addDispatchPtr(const SIRegisterInfo &TRI) {
  ArgInfo.DispatchPtr = ArgDescriptor::createRegister(TRI.getMatchingSuperReg(
    getNextUserSGPR(), AMDGPU::sub0, &AMDGPU::SReg_64RegClass));
  NumUserSGPRs += 2;
  return ArgInfo.DispatchPtr.getRegister();
}

Register SIMachineFunctionInfo::addQueuePtr(const SIRegisterInfo &TRI) {
  ArgInfo.QueuePtr = ArgDescriptor::createRegister(TRI.getMatchingSuperReg(
    getNextUserSGPR(), AMDGPU::sub0, &AMDGPU::SReg_64RegClass));
  NumUserSGPRs += 2;
  return ArgInfo.QueuePtr.getRegister();
}

Register SIMachineFunctionInfo::addKernargSegmentPtr(const SIRegisterInfo &TRI) {
  ArgInfo.KernargSegmentPtr
    = ArgDescriptor::createRegister(TRI.getMatchingSuperReg(
    getNextUserSGPR(), AMDGPU::sub0, &AMDGPU::SReg_64RegClass));
  NumUserSGPRs += 2;
  return ArgInfo.KernargSegmentPtr.getRegister();
}

Register SIMachineFunctionInfo::addDispatchID(const SIRegisterInfo &TRI) {
  ArgInfo.DispatchID = ArgDescriptor::createRegister(TRI.getMatchingSuperReg(
    getNextUserSGPR(), AMDGPU::sub0, &AMDGPU::SReg_64RegClass));
  NumUserSGPRs += 2;
  return ArgInfo.DispatchID.getRegister();
}

Register SIMachineFunctionInfo::addFlatScratchInit(const SIRegisterInfo &TRI) {
  ArgInfo.FlatScratchInit = ArgDescriptor::createRegister(TRI.getMatchingSuperReg(
    getNextUserSGPR(), AMDGPU::sub0, &AMDGPU::SReg_64RegClass));
  NumUserSGPRs += 2;
  return ArgInfo.FlatScratchInit.getRegister();
}

Register SIMachineFunctionInfo::addPrivateSegmentSize(const SIRegisterInfo &TRI) {
  ArgInfo.PrivateSegmentSize = ArgDescriptor::createRegister(getNextUserSGPR());
  NumUserSGPRs += 1;
  return ArgInfo.PrivateSegmentSize.getRegister();
}

Register SIMachineFunctionInfo::addImplicitBufferPtr(const SIRegisterInfo &TRI) {
  ArgInfo.ImplicitBufferPtr = ArgDescriptor::createRegister(TRI.getMatchingSuperReg(
    getNextUserSGPR(), AMDGPU::sub0, &AMDGPU::SReg_64RegClass));
  NumUserSGPRs += 2;
  return ArgInfo.ImplicitBufferPtr.getRegister();
}

Register SIMachineFunctionInfo::addLDSKernelId() {
  ArgInfo.LDSKernelId = ArgDescriptor::createRegister(getNextUserSGPR());
  NumUserSGPRs += 1;
  return ArgInfo.LDSKernelId.getRegister();
}

SmallVectorImpl<MCRegister> *SIMachineFunctionInfo::addPreloadedKernArg(
    const SIRegisterInfo &TRI, const TargetRegisterClass *RC,
    unsigned AllocSizeDWord, int KernArgIdx, int PaddingSGPRs) {
  assert(!ArgInfo.PreloadKernArgs.count(KernArgIdx) &&
         "Preload kernel argument allocated twice.");
  NumUserSGPRs += PaddingSGPRs;
  // If the available register tuples are aligned with the kernarg to be
  // preloaded use that register, otherwise we need to use a set of SGPRs and
  // merge them.
  Register PreloadReg =
      TRI.getMatchingSuperReg(getNextUserSGPR(), AMDGPU::sub0, RC);
  if (PreloadReg &&
      (RC == &AMDGPU::SReg_32RegClass || RC == &AMDGPU::SReg_64RegClass)) {
    ArgInfo.PreloadKernArgs[KernArgIdx].Regs.push_back(PreloadReg);
    NumUserSGPRs += AllocSizeDWord;
  } else {
    for (unsigned I = 0; I < AllocSizeDWord; ++I) {
      ArgInfo.PreloadKernArgs[KernArgIdx].Regs.push_back(getNextUserSGPR());
      NumUserSGPRs++;
    }
  }

  // Track the actual number of SGPRs that HW will preload to.
  UserSGPRInfo.allocKernargPreloadSGPRs(AllocSizeDWord + PaddingSGPRs);
  return &ArgInfo.PreloadKernArgs[KernArgIdx].Regs;
}

void SIMachineFunctionInfo::allocateWWMSpill(MachineFunction &MF, Register VGPR,
                                             uint64_t Size, Align Alignment) {
  // Skip if it is an entry function or the register is already added.
  if (isEntryFunction() || WWMSpills.count(VGPR))
    return;

  // Skip if this is a function with the amdgpu_cs_chain or
  // amdgpu_cs_chain_preserve calling convention and this is a scratch register.
  // We never need to allocate a spill for these because we don't even need to
  // restore the inactive lanes for them (they're scratchier than the usual
  // scratch registers).
  if (isChainFunction() && SIRegisterInfo::isChainScratchRegister(VGPR))
    return;

  WWMSpills.insert(std::make_pair(
      VGPR, MF.getFrameInfo().CreateSpillStackObject(Size, Alignment)));
}

// Separate out the callee-saved and scratch registers.
void SIMachineFunctionInfo::splitWWMSpillRegisters(
    MachineFunction &MF,
    SmallVectorImpl<std::pair<Register, int>> &CalleeSavedRegs,
    SmallVectorImpl<std::pair<Register, int>> &ScratchRegs) const {
  const MCPhysReg *CSRegs = MF.getRegInfo().getCalleeSavedRegs();
  for (auto &Reg : WWMSpills) {
    if (isCalleeSavedReg(CSRegs, Reg.first))
      CalleeSavedRegs.push_back(Reg);
    else
      ScratchRegs.push_back(Reg);
  }
}

bool SIMachineFunctionInfo::isCalleeSavedReg(const MCPhysReg *CSRegs,
                                             MCPhysReg Reg) const {
  for (unsigned I = 0; CSRegs[I]; ++I) {
    if (CSRegs[I] == Reg)
      return true;
  }

  return false;
}

void SIMachineFunctionInfo::shiftSpillPhysVGPRsToLowestRange(
    MachineFunction &MF) {
  const SIRegisterInfo *TRI = MF.getSubtarget<GCNSubtarget>().getRegisterInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  for (Register &Reg : SpillPhysVGPRs) {
    Register NewReg =
        TRI->findUnusedRegister(MRI, &AMDGPU::VGPR_32RegClass, MF);
    if (!NewReg || NewReg >= Reg)
      break;

    MRI.replaceRegWith(Reg, NewReg);

    // Update various tables with the new VGPR.
    WWMReservedRegs.remove(Reg);
    WWMReservedRegs.insert(NewReg);
    WWMSpills.insert(std::make_pair(NewReg, WWMSpills[Reg]));
    WWMSpills.erase(Reg);

    for (MachineBasicBlock &MBB : MF) {
      MBB.removeLiveIn(Reg);
      MBB.sortUniqueLiveIns();
    }

    Reg = NewReg;
  }
}

bool SIMachineFunctionInfo::allocateVirtualVGPRForSGPRSpills(
    MachineFunction &MF, int FI, unsigned LaneIndex) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  Register LaneVGPR;
  if (!LaneIndex) {
    LaneVGPR = MRI.createVirtualRegister(&AMDGPU::VGPR_32RegClass);
    SpillVGPRs.push_back(LaneVGPR);
  } else {
    LaneVGPR = SpillVGPRs.back();
  }

  SGPRSpillsToVirtualVGPRLanes[FI].emplace_back(LaneVGPR, LaneIndex);
  return true;
}

bool SIMachineFunctionInfo::allocatePhysicalVGPRForSGPRSpills(
    MachineFunction &MF, int FI, unsigned LaneIndex, bool IsPrologEpilog) {
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  MachineRegisterInfo &MRI = MF.getRegInfo();
  Register LaneVGPR;
  if (!LaneIndex) {
    // Find the highest available register if called before RA to ensure the
    // lowest registers are available for allocation. The LaneVGPR, in that
    // case, will be shifted back to the lowest range after VGPR allocation.
    LaneVGPR = TRI->findUnusedRegister(MRI, &AMDGPU::VGPR_32RegClass, MF,
                                       !IsPrologEpilog);
    if (LaneVGPR == AMDGPU::NoRegister) {
      // We have no VGPRs left for spilling SGPRs. Reset because we will not
      // partially spill the SGPR to VGPRs.
      SGPRSpillsToPhysicalVGPRLanes.erase(FI);
      return false;
    }

    allocateWWMSpill(MF, LaneVGPR);
    reserveWWMRegister(LaneVGPR);
    for (MachineBasicBlock &MBB : MF) {
      MBB.addLiveIn(LaneVGPR);
      MBB.sortUniqueLiveIns();
    }
    SpillPhysVGPRs.push_back(LaneVGPR);
  } else {
    LaneVGPR = SpillPhysVGPRs.back();
  }

  SGPRSpillsToPhysicalVGPRLanes[FI].emplace_back(LaneVGPR, LaneIndex);
  return true;
}

bool SIMachineFunctionInfo::allocateSGPRSpillToVGPRLane(
    MachineFunction &MF, int FI, bool SpillToPhysVGPRLane,
    bool IsPrologEpilog) {
  std::vector<SIRegisterInfo::SpilledReg> &SpillLanes =
      SpillToPhysVGPRLane ? SGPRSpillsToPhysicalVGPRLanes[FI]
                          : SGPRSpillsToVirtualVGPRLanes[FI];

  // This has already been allocated.
  if (!SpillLanes.empty())
    return true;

  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  MachineFrameInfo &FrameInfo = MF.getFrameInfo();
  unsigned WaveSize = ST.getWavefrontSize();

  unsigned Size = FrameInfo.getObjectSize(FI);
  unsigned NumLanes = Size / 4;

  if (NumLanes > WaveSize)
    return false;

  assert(Size >= 4 && "invalid sgpr spill size");
  assert(ST.getRegisterInfo()->spillSGPRToVGPR() &&
         "not spilling SGPRs to VGPRs");

  unsigned &NumSpillLanes = SpillToPhysVGPRLane ? NumPhysicalVGPRSpillLanes
                                                : NumVirtualVGPRSpillLanes;

  for (unsigned I = 0; I < NumLanes; ++I, ++NumSpillLanes) {
    unsigned LaneIndex = (NumSpillLanes % WaveSize);

    bool Allocated = SpillToPhysVGPRLane
                         ? allocatePhysicalVGPRForSGPRSpills(MF, FI, LaneIndex,
                                                             IsPrologEpilog)
                         : allocateVirtualVGPRForSGPRSpills(MF, FI, LaneIndex);
    if (!Allocated) {
      NumSpillLanes -= I;
      return false;
    }
  }

  return true;
}

/// Reserve AGPRs or VGPRs to support spilling for FrameIndex \p FI.
/// Either AGPR is spilled to VGPR to vice versa.
/// Returns true if a \p FI can be eliminated completely.
bool SIMachineFunctionInfo::allocateVGPRSpillToAGPR(MachineFunction &MF,
                                                    int FI,
                                                    bool isAGPRtoVGPR) {
  MachineRegisterInfo &MRI = MF.getRegInfo();
  MachineFrameInfo &FrameInfo = MF.getFrameInfo();
  const GCNSubtarget &ST =  MF.getSubtarget<GCNSubtarget>();

  assert(ST.hasMAIInsts() && FrameInfo.isSpillSlotObjectIndex(FI));

  auto &Spill = VGPRToAGPRSpills[FI];

  // This has already been allocated.
  if (!Spill.Lanes.empty())
    return Spill.FullyAllocated;

  unsigned Size = FrameInfo.getObjectSize(FI);
  unsigned NumLanes = Size / 4;
  Spill.Lanes.resize(NumLanes, AMDGPU::NoRegister);

  const TargetRegisterClass &RC =
      isAGPRtoVGPR ? AMDGPU::VGPR_32RegClass : AMDGPU::AGPR_32RegClass;
  auto Regs = RC.getRegisters();

  auto &SpillRegs = isAGPRtoVGPR ? SpillAGPR : SpillVGPR;
  const SIRegisterInfo *TRI = ST.getRegisterInfo();
  Spill.FullyAllocated = true;

  // FIXME: Move allocation logic out of MachineFunctionInfo and initialize
  // once.
  BitVector OtherUsedRegs;
  OtherUsedRegs.resize(TRI->getNumRegs());

  const uint32_t *CSRMask =
      TRI->getCallPreservedMask(MF, MF.getFunction().getCallingConv());
  if (CSRMask)
    OtherUsedRegs.setBitsInMask(CSRMask);

  // TODO: Should include register tuples, but doesn't matter with current
  // usage.
  for (MCPhysReg Reg : SpillAGPR)
    OtherUsedRegs.set(Reg);
  for (MCPhysReg Reg : SpillVGPR)
    OtherUsedRegs.set(Reg);

  SmallVectorImpl<MCPhysReg>::const_iterator NextSpillReg = Regs.begin();
  for (int I = NumLanes - 1; I >= 0; --I) {
    NextSpillReg = std::find_if(
        NextSpillReg, Regs.end(), [&MRI, &OtherUsedRegs](MCPhysReg Reg) {
          return MRI.isAllocatable(Reg) && !MRI.isPhysRegUsed(Reg) &&
                 !OtherUsedRegs[Reg];
        });

    if (NextSpillReg == Regs.end()) { // Registers exhausted
      Spill.FullyAllocated = false;
      break;
    }

    OtherUsedRegs.set(*NextSpillReg);
    SpillRegs.push_back(*NextSpillReg);
    MRI.reserveReg(*NextSpillReg, TRI);
    Spill.Lanes[I] = *NextSpillReg++;
  }

  return Spill.FullyAllocated;
}

bool SIMachineFunctionInfo::removeDeadFrameIndices(
    MachineFrameInfo &MFI, bool ResetSGPRSpillStackIDs) {
  // Remove dead frame indices from function frame, however keep FP & BP since
  // spills for them haven't been inserted yet. And also make sure to remove the
  // frame indices from `SGPRSpillsToVirtualVGPRLanes` data structure,
  // otherwise, it could result in an unexpected side effect and bug, in case of
  // any re-mapping of freed frame indices by later pass(es) like "stack slot
  // coloring".
  for (auto &R : make_early_inc_range(SGPRSpillsToVirtualVGPRLanes)) {
    MFI.RemoveStackObject(R.first);
    SGPRSpillsToVirtualVGPRLanes.erase(R.first);
  }

  // Remove the dead frame indices of CSR SGPRs which are spilled to physical
  // VGPR lanes during SILowerSGPRSpills pass.
  if (!ResetSGPRSpillStackIDs) {
    for (auto &R : make_early_inc_range(SGPRSpillsToPhysicalVGPRLanes)) {
      MFI.RemoveStackObject(R.first);
      SGPRSpillsToPhysicalVGPRLanes.erase(R.first);
    }
  }
  bool HaveSGPRToMemory = false;

  if (ResetSGPRSpillStackIDs) {
    // All other SGPRs must be allocated on the default stack, so reset the
    // stack ID.
    for (int I = MFI.getObjectIndexBegin(), E = MFI.getObjectIndexEnd(); I != E;
         ++I) {
      if (!checkIndexInPrologEpilogSGPRSpills(I)) {
        if (MFI.getStackID(I) == TargetStackID::SGPRSpill) {
          MFI.setStackID(I, TargetStackID::Default);
          HaveSGPRToMemory = true;
        }
      }
    }
  }

  for (auto &R : VGPRToAGPRSpills) {
    if (R.second.IsDead)
      MFI.RemoveStackObject(R.first);
  }

  return HaveSGPRToMemory;
}

int SIMachineFunctionInfo::getScavengeFI(MachineFrameInfo &MFI,
                                         const SIRegisterInfo &TRI) {
  if (ScavengeFI)
    return *ScavengeFI;

  ScavengeFI =
      MFI.CreateStackObject(TRI.getSpillSize(AMDGPU::SGPR_32RegClass),
                            TRI.getSpillAlign(AMDGPU::SGPR_32RegClass), false);
  return *ScavengeFI;
}

MCPhysReg SIMachineFunctionInfo::getNextUserSGPR() const {
  assert(NumSystemSGPRs == 0 && "System SGPRs must be added after user SGPRs");
  return AMDGPU::SGPR0 + NumUserSGPRs;
}

MCPhysReg SIMachineFunctionInfo::getNextSystemSGPR() const {
  return AMDGPU::SGPR0 + NumUserSGPRs + NumSystemSGPRs;
}

void SIMachineFunctionInfo::MRI_NoteNewVirtualRegister(Register Reg) {
  VRegFlags.grow(Reg);
}

void SIMachineFunctionInfo::MRI_NoteCloneVirtualRegister(Register NewReg,
                                                         Register SrcReg) {
  VRegFlags.grow(NewReg);
  VRegFlags[NewReg] = VRegFlags[SrcReg];
}

Register
SIMachineFunctionInfo::getGITPtrLoReg(const MachineFunction &MF) const {
  const GCNSubtarget &ST = MF.getSubtarget<GCNSubtarget>();
  if (!ST.isAmdPalOS())
    return Register();
  Register GitPtrLo = AMDGPU::SGPR0; // Low GIT address passed in
  if (ST.hasMergedShaders()) {
    switch (MF.getFunction().getCallingConv()) {
    case CallingConv::AMDGPU_HS:
    case CallingConv::AMDGPU_GS:
      // Low GIT address is passed in s8 rather than s0 for an LS+HS or
      // ES+GS merged shader on gfx9+.
      GitPtrLo = AMDGPU::SGPR8;
      return GitPtrLo;
    default:
      return GitPtrLo;
    }
  }
  return GitPtrLo;
}

static yaml::StringValue regToString(Register Reg,
                                     const TargetRegisterInfo &TRI) {
  yaml::StringValue Dest;
  {
    raw_string_ostream OS(Dest.Value);
    OS << printReg(Reg, &TRI);
  }
  return Dest;
}

static std::optional<yaml::SIArgumentInfo>
convertArgumentInfo(const AMDGPUFunctionArgInfo &ArgInfo,
                    const TargetRegisterInfo &TRI) {
  yaml::SIArgumentInfo AI;

  auto convertArg = [&](std::optional<yaml::SIArgument> &A,
                        const ArgDescriptor &Arg) {
    if (!Arg)
      return false;

    // Create a register or stack argument.
    yaml::SIArgument SA = yaml::SIArgument::createArgument(Arg.isRegister());
    if (Arg.isRegister()) {
      raw_string_ostream OS(SA.RegisterName.Value);
      OS << printReg(Arg.getRegister(), &TRI);
    } else
      SA.StackOffset = Arg.getStackOffset();
    // Check and update the optional mask.
    if (Arg.isMasked())
      SA.Mask = Arg.getMask();

    A = SA;
    return true;
  };

  // TODO: Need to serialize kernarg preloads.
  bool Any = false;
  Any |= convertArg(AI.PrivateSegmentBuffer, ArgInfo.PrivateSegmentBuffer);
  Any |= convertArg(AI.DispatchPtr, ArgInfo.DispatchPtr);
  Any |= convertArg(AI.QueuePtr, ArgInfo.QueuePtr);
  Any |= convertArg(AI.KernargSegmentPtr, ArgInfo.KernargSegmentPtr);
  Any |= convertArg(AI.DispatchID, ArgInfo.DispatchID);
  Any |= convertArg(AI.FlatScratchInit, ArgInfo.FlatScratchInit);
  Any |= convertArg(AI.LDSKernelId, ArgInfo.LDSKernelId);
  Any |= convertArg(AI.PrivateSegmentSize, ArgInfo.PrivateSegmentSize);
  Any |= convertArg(AI.WorkGroupIDX, ArgInfo.WorkGroupIDX);
  Any |= convertArg(AI.WorkGroupIDY, ArgInfo.WorkGroupIDY);
  Any |= convertArg(AI.WorkGroupIDZ, ArgInfo.WorkGroupIDZ);
  Any |= convertArg(AI.WorkGroupInfo, ArgInfo.WorkGroupInfo);
  Any |= convertArg(AI.PrivateSegmentWaveByteOffset,
                    ArgInfo.PrivateSegmentWaveByteOffset);
  Any |= convertArg(AI.ImplicitArgPtr, ArgInfo.ImplicitArgPtr);
  Any |= convertArg(AI.ImplicitBufferPtr, ArgInfo.ImplicitBufferPtr);
  Any |= convertArg(AI.WorkItemIDX, ArgInfo.WorkItemIDX);
  Any |= convertArg(AI.WorkItemIDY, ArgInfo.WorkItemIDY);
  Any |= convertArg(AI.WorkItemIDZ, ArgInfo.WorkItemIDZ);

  if (Any)
    return AI;

  return std::nullopt;
}

yaml::SIMachineFunctionInfo::SIMachineFunctionInfo(
    const llvm::SIMachineFunctionInfo &MFI, const TargetRegisterInfo &TRI,
    const llvm::MachineFunction &MF)
    : ExplicitKernArgSize(MFI.getExplicitKernArgSize()),
      MaxKernArgAlign(MFI.getMaxKernArgAlign()), LDSSize(MFI.getLDSSize()),
      GDSSize(MFI.getGDSSize()),
      DynLDSAlign(MFI.getDynLDSAlign()), IsEntryFunction(MFI.isEntryFunction()),
      NoSignedZerosFPMath(MFI.hasNoSignedZerosFPMath()),
      MemoryBound(MFI.isMemoryBound()), WaveLimiter(MFI.needsWaveLimiter()),
      HasSpilledSGPRs(MFI.hasSpilledSGPRs()),
      HasSpilledVGPRs(MFI.hasSpilledVGPRs()),
      HighBitsOf32BitAddress(MFI.get32BitAddressHighBits()),
      Occupancy(MFI.getOccupancy()),
      ScratchRSrcReg(regToString(MFI.getScratchRSrcReg(), TRI)),
      FrameOffsetReg(regToString(MFI.getFrameOffsetReg(), TRI)),
      StackPtrOffsetReg(regToString(MFI.getStackPtrOffsetReg(), TRI)),
      BytesInStackArgArea(MFI.getBytesInStackArgArea()),
      ReturnsVoid(MFI.returnsVoid()),
      ArgInfo(convertArgumentInfo(MFI.getArgInfo(), TRI)),
      PSInputAddr(MFI.getPSInputAddr()),
      PSInputEnable(MFI.getPSInputEnable()),
      Mode(MFI.getMode()) {
  for (Register Reg : MFI.getWWMReservedRegs())
    WWMReservedRegs.push_back(regToString(Reg, TRI));

  if (MFI.getLongBranchReservedReg())
    LongBranchReservedReg = regToString(MFI.getLongBranchReservedReg(), TRI);
  if (MFI.getVGPRForAGPRCopy())
    VGPRForAGPRCopy = regToString(MFI.getVGPRForAGPRCopy(), TRI);

  if (MFI.getSGPRForEXECCopy())
    SGPRForEXECCopy = regToString(MFI.getSGPRForEXECCopy(), TRI);

  auto SFI = MFI.getOptionalScavengeFI();
  if (SFI)
    ScavengeFI = yaml::FrameIndex(*SFI, MF.getFrameInfo());
}

void yaml::SIMachineFunctionInfo::mappingImpl(yaml::IO &YamlIO) {
  MappingTraits<SIMachineFunctionInfo>::mapping(YamlIO, *this);
}

bool SIMachineFunctionInfo::initializeBaseYamlFields(
    const yaml::SIMachineFunctionInfo &YamlMFI, const MachineFunction &MF,
    PerFunctionMIParsingState &PFS, SMDiagnostic &Error, SMRange &SourceRange) {
  ExplicitKernArgSize = YamlMFI.ExplicitKernArgSize;
  MaxKernArgAlign = YamlMFI.MaxKernArgAlign;
  LDSSize = YamlMFI.LDSSize;
  GDSSize = YamlMFI.GDSSize;
  DynLDSAlign = YamlMFI.DynLDSAlign;
  PSInputAddr = YamlMFI.PSInputAddr;
  PSInputEnable = YamlMFI.PSInputEnable;
  HighBitsOf32BitAddress = YamlMFI.HighBitsOf32BitAddress;
  Occupancy = YamlMFI.Occupancy;
  IsEntryFunction = YamlMFI.IsEntryFunction;
  NoSignedZerosFPMath = YamlMFI.NoSignedZerosFPMath;
  MemoryBound = YamlMFI.MemoryBound;
  WaveLimiter = YamlMFI.WaveLimiter;
  HasSpilledSGPRs = YamlMFI.HasSpilledSGPRs;
  HasSpilledVGPRs = YamlMFI.HasSpilledVGPRs;
  BytesInStackArgArea = YamlMFI.BytesInStackArgArea;
  ReturnsVoid = YamlMFI.ReturnsVoid;

  if (YamlMFI.ScavengeFI) {
    auto FIOrErr = YamlMFI.ScavengeFI->getFI(MF.getFrameInfo());
    if (!FIOrErr) {
      // Create a diagnostic for a the frame index.
      const MemoryBuffer &Buffer =
          *PFS.SM->getMemoryBuffer(PFS.SM->getMainFileID());

      Error = SMDiagnostic(*PFS.SM, SMLoc(), Buffer.getBufferIdentifier(), 1, 1,
                           SourceMgr::DK_Error, toString(FIOrErr.takeError()),
                           "", std::nullopt, std::nullopt);
      SourceRange = YamlMFI.ScavengeFI->SourceRange;
      return true;
    }
    ScavengeFI = *FIOrErr;
  } else {
    ScavengeFI = std::nullopt;
  }
  return false;
}

bool SIMachineFunctionInfo::mayUseAGPRs(const Function &F) const {
  return !F.hasFnAttribute("amdgpu-no-agpr");
}

bool SIMachineFunctionInfo::usesAGPRs(const MachineFunction &MF) const {
  if (UsesAGPRs)
    return *UsesAGPRs;

  if (!mayNeedAGPRs()) {
    UsesAGPRs = false;
    return false;
  }

  if (!AMDGPU::isEntryFunctionCC(MF.getFunction().getCallingConv()) ||
      MF.getFrameInfo().hasCalls()) {
    UsesAGPRs = true;
    return true;
  }

  const MachineRegisterInfo &MRI = MF.getRegInfo();

  for (unsigned I = 0, E = MRI.getNumVirtRegs(); I != E; ++I) {
    const Register Reg = Register::index2VirtReg(I);
    const TargetRegisterClass *RC = MRI.getRegClassOrNull(Reg);
    if (RC && SIRegisterInfo::isAGPRClass(RC)) {
      UsesAGPRs = true;
      return true;
    }
    if (!RC && !MRI.use_empty(Reg) && MRI.getType(Reg).isValid()) {
      // Defer caching UsesAGPRs, function might not yet been regbank selected.
      return true;
    }
  }

  for (MCRegister Reg : AMDGPU::AGPR_32RegClass) {
    if (MRI.isPhysRegUsed(Reg)) {
      UsesAGPRs = true;
      return true;
    }
  }

  UsesAGPRs = false;
  return false;
}
