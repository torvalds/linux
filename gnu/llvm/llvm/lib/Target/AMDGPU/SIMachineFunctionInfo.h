//==- SIMachineFunctionInfo.h - SIMachineFunctionInfo interface --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_SIMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_AMDGPU_SIMACHINEFUNCTIONINFO_H

#include "AMDGPUArgumentUsageInfo.h"
#include "AMDGPUMachineFunction.h"
#include "AMDGPUTargetMachine.h"
#include "GCNSubtarget.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIInstrInfo.h"
#include "SIModeRegisterDefaults.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MIRYamlMapping.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

namespace llvm {

class MachineFrameInfo;
class MachineFunction;
class SIMachineFunctionInfo;
class SIRegisterInfo;
class TargetRegisterClass;

class AMDGPUPseudoSourceValue : public PseudoSourceValue {
public:
  enum AMDGPUPSVKind : unsigned {
    PSVImage = PseudoSourceValue::TargetCustom,
    GWSResource
  };

protected:
  AMDGPUPseudoSourceValue(unsigned Kind, const AMDGPUTargetMachine &TM)
      : PseudoSourceValue(Kind, TM) {}

public:
  bool isConstant(const MachineFrameInfo *) const override {
    // This should probably be true for most images, but we will start by being
    // conservative.
    return false;
  }

  bool isAliased(const MachineFrameInfo *) const override {
    return true;
  }

  bool mayAlias(const MachineFrameInfo *) const override {
    return true;
  }
};

class AMDGPUGWSResourcePseudoSourceValue final : public AMDGPUPseudoSourceValue {
public:
  explicit AMDGPUGWSResourcePseudoSourceValue(const AMDGPUTargetMachine &TM)
      : AMDGPUPseudoSourceValue(GWSResource, TM) {}

  static bool classof(const PseudoSourceValue *V) {
    return V->kind() == GWSResource;
  }

  // These are inaccessible memory from IR.
  bool isAliased(const MachineFrameInfo *) const override {
    return false;
  }

  // These are inaccessible memory from IR.
  bool mayAlias(const MachineFrameInfo *) const override {
    return false;
  }

  void printCustom(raw_ostream &OS) const override {
    OS << "GWSResource";
  }
};

namespace yaml {

struct SIArgument {
  bool IsRegister;
  union {
    StringValue RegisterName;
    unsigned StackOffset;
  };
  std::optional<unsigned> Mask;

  // Default constructor, which creates a stack argument.
  SIArgument() : IsRegister(false), StackOffset(0) {}
  SIArgument(const SIArgument &Other) {
    IsRegister = Other.IsRegister;
    if (IsRegister) {
      ::new ((void *)std::addressof(RegisterName))
          StringValue(Other.RegisterName);
    } else
      StackOffset = Other.StackOffset;
    Mask = Other.Mask;
  }
  SIArgument &operator=(const SIArgument &Other) {
    IsRegister = Other.IsRegister;
    if (IsRegister) {
      ::new ((void *)std::addressof(RegisterName))
          StringValue(Other.RegisterName);
    } else
      StackOffset = Other.StackOffset;
    Mask = Other.Mask;
    return *this;
  }
  ~SIArgument() {
    if (IsRegister)
      RegisterName.~StringValue();
  }

  // Helper to create a register or stack argument.
  static inline SIArgument createArgument(bool IsReg) {
    if (IsReg)
      return SIArgument(IsReg);
    return SIArgument();
  }

private:
  // Construct a register argument.
  SIArgument(bool) : IsRegister(true), RegisterName() {}
};

template <> struct MappingTraits<SIArgument> {
  static void mapping(IO &YamlIO, SIArgument &A) {
    if (YamlIO.outputting()) {
      if (A.IsRegister)
        YamlIO.mapRequired("reg", A.RegisterName);
      else
        YamlIO.mapRequired("offset", A.StackOffset);
    } else {
      auto Keys = YamlIO.keys();
      if (is_contained(Keys, "reg")) {
        A = SIArgument::createArgument(true);
        YamlIO.mapRequired("reg", A.RegisterName);
      } else if (is_contained(Keys, "offset"))
        YamlIO.mapRequired("offset", A.StackOffset);
      else
        YamlIO.setError("missing required key 'reg' or 'offset'");
    }
    YamlIO.mapOptional("mask", A.Mask);
  }
  static const bool flow = true;
};

struct SIArgumentInfo {
  std::optional<SIArgument> PrivateSegmentBuffer;
  std::optional<SIArgument> DispatchPtr;
  std::optional<SIArgument> QueuePtr;
  std::optional<SIArgument> KernargSegmentPtr;
  std::optional<SIArgument> DispatchID;
  std::optional<SIArgument> FlatScratchInit;
  std::optional<SIArgument> PrivateSegmentSize;

  std::optional<SIArgument> WorkGroupIDX;
  std::optional<SIArgument> WorkGroupIDY;
  std::optional<SIArgument> WorkGroupIDZ;
  std::optional<SIArgument> WorkGroupInfo;
  std::optional<SIArgument> LDSKernelId;
  std::optional<SIArgument> PrivateSegmentWaveByteOffset;

  std::optional<SIArgument> ImplicitArgPtr;
  std::optional<SIArgument> ImplicitBufferPtr;

  std::optional<SIArgument> WorkItemIDX;
  std::optional<SIArgument> WorkItemIDY;
  std::optional<SIArgument> WorkItemIDZ;
};

template <> struct MappingTraits<SIArgumentInfo> {
  static void mapping(IO &YamlIO, SIArgumentInfo &AI) {
    YamlIO.mapOptional("privateSegmentBuffer", AI.PrivateSegmentBuffer);
    YamlIO.mapOptional("dispatchPtr", AI.DispatchPtr);
    YamlIO.mapOptional("queuePtr", AI.QueuePtr);
    YamlIO.mapOptional("kernargSegmentPtr", AI.KernargSegmentPtr);
    YamlIO.mapOptional("dispatchID", AI.DispatchID);
    YamlIO.mapOptional("flatScratchInit", AI.FlatScratchInit);
    YamlIO.mapOptional("privateSegmentSize", AI.PrivateSegmentSize);

    YamlIO.mapOptional("workGroupIDX", AI.WorkGroupIDX);
    YamlIO.mapOptional("workGroupIDY", AI.WorkGroupIDY);
    YamlIO.mapOptional("workGroupIDZ", AI.WorkGroupIDZ);
    YamlIO.mapOptional("workGroupInfo", AI.WorkGroupInfo);
    YamlIO.mapOptional("LDSKernelId", AI.LDSKernelId);
    YamlIO.mapOptional("privateSegmentWaveByteOffset",
                       AI.PrivateSegmentWaveByteOffset);

    YamlIO.mapOptional("implicitArgPtr", AI.ImplicitArgPtr);
    YamlIO.mapOptional("implicitBufferPtr", AI.ImplicitBufferPtr);

    YamlIO.mapOptional("workItemIDX", AI.WorkItemIDX);
    YamlIO.mapOptional("workItemIDY", AI.WorkItemIDY);
    YamlIO.mapOptional("workItemIDZ", AI.WorkItemIDZ);
  }
};

// Default to default mode for default calling convention.
struct SIMode {
  bool IEEE = true;
  bool DX10Clamp = true;
  bool FP32InputDenormals = true;
  bool FP32OutputDenormals = true;
  bool FP64FP16InputDenormals = true;
  bool FP64FP16OutputDenormals = true;

  SIMode() = default;

  SIMode(const SIModeRegisterDefaults &Mode) {
    IEEE = Mode.IEEE;
    DX10Clamp = Mode.DX10Clamp;
    FP32InputDenormals = Mode.FP32Denormals.Input != DenormalMode::PreserveSign;
    FP32OutputDenormals =
        Mode.FP32Denormals.Output != DenormalMode::PreserveSign;
    FP64FP16InputDenormals =
        Mode.FP64FP16Denormals.Input != DenormalMode::PreserveSign;
    FP64FP16OutputDenormals =
        Mode.FP64FP16Denormals.Output != DenormalMode::PreserveSign;
  }

  bool operator ==(const SIMode Other) const {
    return IEEE == Other.IEEE &&
           DX10Clamp == Other.DX10Clamp &&
           FP32InputDenormals == Other.FP32InputDenormals &&
           FP32OutputDenormals == Other.FP32OutputDenormals &&
           FP64FP16InputDenormals == Other.FP64FP16InputDenormals &&
           FP64FP16OutputDenormals == Other.FP64FP16OutputDenormals;
  }
};

template <> struct MappingTraits<SIMode> {
  static void mapping(IO &YamlIO, SIMode &Mode) {
    YamlIO.mapOptional("ieee", Mode.IEEE, true);
    YamlIO.mapOptional("dx10-clamp", Mode.DX10Clamp, true);
    YamlIO.mapOptional("fp32-input-denormals", Mode.FP32InputDenormals, true);
    YamlIO.mapOptional("fp32-output-denormals", Mode.FP32OutputDenormals, true);
    YamlIO.mapOptional("fp64-fp16-input-denormals", Mode.FP64FP16InputDenormals, true);
    YamlIO.mapOptional("fp64-fp16-output-denormals", Mode.FP64FP16OutputDenormals, true);
  }
};

struct SIMachineFunctionInfo final : public yaml::MachineFunctionInfo {
  uint64_t ExplicitKernArgSize = 0;
  Align MaxKernArgAlign;
  uint32_t LDSSize = 0;
  uint32_t GDSSize = 0;
  Align DynLDSAlign;
  bool IsEntryFunction = false;
  bool IsChainFunction = false;
  bool NoSignedZerosFPMath = false;
  bool MemoryBound = false;
  bool WaveLimiter = false;
  bool HasSpilledSGPRs = false;
  bool HasSpilledVGPRs = false;
  uint32_t HighBitsOf32BitAddress = 0;

  // TODO: 10 may be a better default since it's the maximum.
  unsigned Occupancy = 0;

  SmallVector<StringValue> WWMReservedRegs;

  StringValue ScratchRSrcReg = "$private_rsrc_reg";
  StringValue FrameOffsetReg = "$fp_reg";
  StringValue StackPtrOffsetReg = "$sp_reg";

  unsigned BytesInStackArgArea = 0;
  bool ReturnsVoid = true;

  std::optional<SIArgumentInfo> ArgInfo;

  unsigned PSInputAddr = 0;
  unsigned PSInputEnable = 0;

  SIMode Mode;
  std::optional<FrameIndex> ScavengeFI;
  StringValue VGPRForAGPRCopy;
  StringValue SGPRForEXECCopy;
  StringValue LongBranchReservedReg;

  SIMachineFunctionInfo() = default;
  SIMachineFunctionInfo(const llvm::SIMachineFunctionInfo &,
                        const TargetRegisterInfo &TRI,
                        const llvm::MachineFunction &MF);

  void mappingImpl(yaml::IO &YamlIO) override;
  ~SIMachineFunctionInfo() = default;
};

template <> struct MappingTraits<SIMachineFunctionInfo> {
  static void mapping(IO &YamlIO, SIMachineFunctionInfo &MFI) {
    YamlIO.mapOptional("explicitKernArgSize", MFI.ExplicitKernArgSize,
                       UINT64_C(0));
    YamlIO.mapOptional("maxKernArgAlign", MFI.MaxKernArgAlign);
    YamlIO.mapOptional("ldsSize", MFI.LDSSize, 0u);
    YamlIO.mapOptional("gdsSize", MFI.GDSSize, 0u);
    YamlIO.mapOptional("dynLDSAlign", MFI.DynLDSAlign, Align());
    YamlIO.mapOptional("isEntryFunction", MFI.IsEntryFunction, false);
    YamlIO.mapOptional("isChainFunction", MFI.IsChainFunction, false);
    YamlIO.mapOptional("noSignedZerosFPMath", MFI.NoSignedZerosFPMath, false);
    YamlIO.mapOptional("memoryBound", MFI.MemoryBound, false);
    YamlIO.mapOptional("waveLimiter", MFI.WaveLimiter, false);
    YamlIO.mapOptional("hasSpilledSGPRs", MFI.HasSpilledSGPRs, false);
    YamlIO.mapOptional("hasSpilledVGPRs", MFI.HasSpilledVGPRs, false);
    YamlIO.mapOptional("scratchRSrcReg", MFI.ScratchRSrcReg,
                       StringValue("$private_rsrc_reg"));
    YamlIO.mapOptional("frameOffsetReg", MFI.FrameOffsetReg,
                       StringValue("$fp_reg"));
    YamlIO.mapOptional("stackPtrOffsetReg", MFI.StackPtrOffsetReg,
                       StringValue("$sp_reg"));
    YamlIO.mapOptional("bytesInStackArgArea", MFI.BytesInStackArgArea, 0u);
    YamlIO.mapOptional("returnsVoid", MFI.ReturnsVoid, true);
    YamlIO.mapOptional("argumentInfo", MFI.ArgInfo);
    YamlIO.mapOptional("psInputAddr", MFI.PSInputAddr, 0u);
    YamlIO.mapOptional("psInputEnable", MFI.PSInputEnable, 0u);
    YamlIO.mapOptional("mode", MFI.Mode, SIMode());
    YamlIO.mapOptional("highBitsOf32BitAddress",
                       MFI.HighBitsOf32BitAddress, 0u);
    YamlIO.mapOptional("occupancy", MFI.Occupancy, 0);
    YamlIO.mapOptional("wwmReservedRegs", MFI.WWMReservedRegs);
    YamlIO.mapOptional("scavengeFI", MFI.ScavengeFI);
    YamlIO.mapOptional("vgprForAGPRCopy", MFI.VGPRForAGPRCopy,
                       StringValue()); // Don't print out when it's empty.
    YamlIO.mapOptional("sgprForEXECCopy", MFI.SGPRForEXECCopy,
                       StringValue()); // Don't print out when it's empty.
    YamlIO.mapOptional("longBranchReservedReg", MFI.LongBranchReservedReg,
                       StringValue());
  }
};

} // end namespace yaml

// A CSR SGPR value can be preserved inside a callee using one of the following
// methods.
//   1. Copy to an unused scratch SGPR.
//   2. Spill to a VGPR lane.
//   3. Spill to memory via. a scratch VGPR.
// class PrologEpilogSGPRSaveRestoreInfo represents the save/restore method used
// for an SGPR at function prolog/epilog.
enum class SGPRSaveKind : uint8_t {
  COPY_TO_SCRATCH_SGPR,
  SPILL_TO_VGPR_LANE,
  SPILL_TO_MEM
};

class PrologEpilogSGPRSaveRestoreInfo {
  SGPRSaveKind Kind;
  union {
    int Index;
    Register Reg;
  };

public:
  PrologEpilogSGPRSaveRestoreInfo(SGPRSaveKind K, int I) : Kind(K), Index(I) {}
  PrologEpilogSGPRSaveRestoreInfo(SGPRSaveKind K, Register R)
      : Kind(K), Reg(R) {}
  Register getReg() const { return Reg; }
  int getIndex() const { return Index; }
  SGPRSaveKind getKind() const { return Kind; }
};

/// This class keeps track of the SPI_SP_INPUT_ADDR config register, which
/// tells the hardware which interpolation parameters to load.
class SIMachineFunctionInfo final : public AMDGPUMachineFunction,
                                    private MachineRegisterInfo::Delegate {
  friend class GCNTargetMachine;

  // State of MODE register, assumed FP mode.
  SIModeRegisterDefaults Mode;

  // Registers that may be reserved for spilling purposes. These may be the same
  // as the input registers.
  Register ScratchRSrcReg = AMDGPU::PRIVATE_RSRC_REG;

  // This is the unswizzled offset from the current dispatch's scratch wave
  // base to the beginning of the current function's frame.
  Register FrameOffsetReg = AMDGPU::FP_REG;

  // This is an ABI register used in the non-entry calling convention to
  // communicate the unswizzled offset from the current dispatch's scratch wave
  // base to the beginning of the new function's frame.
  Register StackPtrOffsetReg = AMDGPU::SP_REG;

  // Registers that may be reserved when RA doesn't allocate enough
  // registers to plan for the case where an indirect branch ends up
  // being needed during branch relaxation.
  Register LongBranchReservedReg;

  AMDGPUFunctionArgInfo ArgInfo;

  // Graphics info.
  unsigned PSInputAddr = 0;
  unsigned PSInputEnable = 0;

  /// Number of bytes of arguments this function has on the stack. If the callee
  /// is expected to restore the argument stack this should be a multiple of 16,
  /// all usable during a tail call.
  ///
  /// The alternative would forbid tail call optimisation in some cases: if we
  /// want to transfer control from a function with 8-bytes of stack-argument
  /// space to a function with 16-bytes then misalignment of this value would
  /// make a stack adjustment necessary, which could not be undone by the
  /// callee.
  unsigned BytesInStackArgArea = 0;

  bool ReturnsVoid = true;

  // A pair of default/requested minimum/maximum flat work group sizes.
  // Minimum - first, maximum - second.
  std::pair<unsigned, unsigned> FlatWorkGroupSizes = {0, 0};

  // A pair of default/requested minimum/maximum number of waves per execution
  // unit. Minimum - first, maximum - second.
  std::pair<unsigned, unsigned> WavesPerEU = {0, 0};

  const AMDGPUGWSResourcePseudoSourceValue GWSResourcePSV;

  // Default/requested number of work groups for the function.
  SmallVector<unsigned> MaxNumWorkGroups = {0, 0, 0};

private:
  unsigned NumUserSGPRs = 0;
  unsigned NumSystemSGPRs = 0;

  bool HasSpilledSGPRs = false;
  bool HasSpilledVGPRs = false;
  bool HasNonSpillStackObjects = false;
  bool IsStackRealigned = false;

  unsigned NumSpilledSGPRs = 0;
  unsigned NumSpilledVGPRs = 0;

  // Tracks information about user SGPRs that will be setup by hardware which
  // will apply to all wavefronts of the grid.
  GCNUserSGPRUsageInfo UserSGPRInfo;

  // Feature bits required for inputs passed in system SGPRs.
  bool WorkGroupIDX : 1; // Always initialized.
  bool WorkGroupIDY : 1;
  bool WorkGroupIDZ : 1;
  bool WorkGroupInfo : 1;
  bool LDSKernelId : 1;
  bool PrivateSegmentWaveByteOffset : 1;

  bool WorkItemIDX : 1; // Always initialized.
  bool WorkItemIDY : 1;
  bool WorkItemIDZ : 1;

  // Pointer to where the ABI inserts special kernel arguments separate from the
  // user arguments. This is an offset from the KernargSegmentPtr.
  bool ImplicitArgPtr : 1;

  bool MayNeedAGPRs : 1;

  // The hard-wired high half of the address of the global information table
  // for AMDPAL OS type. 0xffffffff represents no hard-wired high half, since
  // current hardware only allows a 16 bit value.
  unsigned GITPtrHigh;

  unsigned HighBitsOf32BitAddress;

  // Flags associated with the virtual registers.
  IndexedMap<uint8_t, VirtReg2IndexFunctor> VRegFlags;

  // Current recorded maximum possible occupancy.
  unsigned Occupancy;

  mutable std::optional<bool> UsesAGPRs;

  MCPhysReg getNextUserSGPR() const;

  MCPhysReg getNextSystemSGPR() const;

  // MachineRegisterInfo callback functions to notify events.
  void MRI_NoteNewVirtualRegister(Register Reg) override;
  void MRI_NoteCloneVirtualRegister(Register NewReg, Register SrcReg) override;

public:
  struct VGPRSpillToAGPR {
    SmallVector<MCPhysReg, 32> Lanes;
    bool FullyAllocated = false;
    bool IsDead = false;
  };

private:
  // To track virtual VGPR + lane index for each subregister of the SGPR spilled
  // to frameindex key during SILowerSGPRSpills pass.
  DenseMap<int, std::vector<SIRegisterInfo::SpilledReg>>
      SGPRSpillsToVirtualVGPRLanes;
  // To track physical VGPR + lane index for CSR SGPR spills and special SGPRs
  // like Frame Pointer identified during PrologEpilogInserter.
  DenseMap<int, std::vector<SIRegisterInfo::SpilledReg>>
      SGPRSpillsToPhysicalVGPRLanes;
  unsigned NumVirtualVGPRSpillLanes = 0;
  unsigned NumPhysicalVGPRSpillLanes = 0;
  SmallVector<Register, 2> SpillVGPRs;
  SmallVector<Register, 2> SpillPhysVGPRs;
  using WWMSpillsMap = MapVector<Register, int>;
  // To track the registers used in instructions that can potentially modify the
  // inactive lanes. The WWM instructions and the writelane instructions for
  // spilling SGPRs to VGPRs fall under such category of operations. The VGPRs
  // modified by them should be spilled/restored at function prolog/epilog to
  // avoid any undesired outcome. Each entry in this map holds a pair of values,
  // the VGPR and its stack slot index.
  WWMSpillsMap WWMSpills;

  using ReservedRegSet = SmallSetVector<Register, 8>;
  // To track the VGPRs reserved for WWM instructions. They get stack slots
  // later during PrologEpilogInserter and get added into the superset WWMSpills
  // for actual spilling. A separate set makes the register reserved part and
  // the serialization easier.
  ReservedRegSet WWMReservedRegs;

  using PrologEpilogSGPRSpill =
      std::pair<Register, PrologEpilogSGPRSaveRestoreInfo>;
  // To track the SGPR spill method used for a CSR SGPR register during
  // frame lowering. Even though the SGPR spills are handled during
  // SILowerSGPRSpills pass, some special handling needed later during the
  // PrologEpilogInserter.
  SmallVector<PrologEpilogSGPRSpill, 3> PrologEpilogSGPRSpills;

  // To save/restore EXEC MASK around WWM spills and copies.
  Register SGPRForEXECCopy;

  DenseMap<int, VGPRSpillToAGPR> VGPRToAGPRSpills;

  // AGPRs used for VGPR spills.
  SmallVector<MCPhysReg, 32> SpillAGPR;

  // VGPRs used for AGPR spills.
  SmallVector<MCPhysReg, 32> SpillVGPR;

  // Emergency stack slot. Sometimes, we create this before finalizing the stack
  // frame, so save it here and add it to the RegScavenger later.
  std::optional<int> ScavengeFI;

private:
  Register VGPRForAGPRCopy;

  bool allocateVirtualVGPRForSGPRSpills(MachineFunction &MF, int FI,
                                        unsigned LaneIndex);
  bool allocatePhysicalVGPRForSGPRSpills(MachineFunction &MF, int FI,
                                         unsigned LaneIndex,
                                         bool IsPrologEpilog);

public:
  Register getVGPRForAGPRCopy() const {
    return VGPRForAGPRCopy;
  }

  void setVGPRForAGPRCopy(Register NewVGPRForAGPRCopy) {
    VGPRForAGPRCopy = NewVGPRForAGPRCopy;
  }

  bool isCalleeSavedReg(const MCPhysReg *CSRegs, MCPhysReg Reg) const;

public:
  SIMachineFunctionInfo(const SIMachineFunctionInfo &MFI) = default;
  SIMachineFunctionInfo(const Function &F, const GCNSubtarget *STI);

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override;

  bool initializeBaseYamlFields(const yaml::SIMachineFunctionInfo &YamlMFI,
                                const MachineFunction &MF,
                                PerFunctionMIParsingState &PFS,
                                SMDiagnostic &Error, SMRange &SourceRange);

  void reserveWWMRegister(Register Reg) { WWMReservedRegs.insert(Reg); }

  SIModeRegisterDefaults getMode() const { return Mode; }

  ArrayRef<SIRegisterInfo::SpilledReg>
  getSGPRSpillToVirtualVGPRLanes(int FrameIndex) const {
    auto I = SGPRSpillsToVirtualVGPRLanes.find(FrameIndex);
    return (I == SGPRSpillsToVirtualVGPRLanes.end())
               ? ArrayRef<SIRegisterInfo::SpilledReg>()
               : ArrayRef(I->second);
  }

  ArrayRef<Register> getSGPRSpillVGPRs() const { return SpillVGPRs; }

  const WWMSpillsMap &getWWMSpills() const { return WWMSpills; }
  const ReservedRegSet &getWWMReservedRegs() const { return WWMReservedRegs; }

  ArrayRef<PrologEpilogSGPRSpill> getPrologEpilogSGPRSpills() const {
    assert(is_sorted(PrologEpilogSGPRSpills, llvm::less_first()));
    return PrologEpilogSGPRSpills;
  }

  GCNUserSGPRUsageInfo &getUserSGPRInfo() { return UserSGPRInfo; }

  const GCNUserSGPRUsageInfo &getUserSGPRInfo() const { return UserSGPRInfo; }

  void addToPrologEpilogSGPRSpills(Register Reg,
                                   PrologEpilogSGPRSaveRestoreInfo SI) {
    assert(!hasPrologEpilogSGPRSpillEntry(Reg));

    // Insert a new entry in the right place to keep the vector in sorted order.
    // This should be cheap since the vector is expected to be very short.
    PrologEpilogSGPRSpills.insert(
        upper_bound(
            PrologEpilogSGPRSpills, Reg,
            [](const auto &LHS, const auto &RHS) { return LHS < RHS.first; }),
        std::make_pair(Reg, SI));
  }

  // Check if an entry created for \p Reg in PrologEpilogSGPRSpills. Return true
  // on success and false otherwise.
  bool hasPrologEpilogSGPRSpillEntry(Register Reg) const {
    auto I = find_if(PrologEpilogSGPRSpills,
                     [&Reg](const auto &Spill) { return Spill.first == Reg; });
    return I != PrologEpilogSGPRSpills.end();
  }

  // Get the scratch SGPR if allocated to save/restore \p Reg.
  Register getScratchSGPRCopyDstReg(Register Reg) const {
    auto I = find_if(PrologEpilogSGPRSpills,
                     [&Reg](const auto &Spill) { return Spill.first == Reg; });
    if (I != PrologEpilogSGPRSpills.end() &&
        I->second.getKind() == SGPRSaveKind::COPY_TO_SCRATCH_SGPR)
      return I->second.getReg();

    return AMDGPU::NoRegister;
  }

  // Get all scratch SGPRs allocated to copy/restore the SGPR spills.
  void getAllScratchSGPRCopyDstRegs(SmallVectorImpl<Register> &Regs) const {
    for (const auto &SI : PrologEpilogSGPRSpills) {
      if (SI.second.getKind() == SGPRSaveKind::COPY_TO_SCRATCH_SGPR)
        Regs.push_back(SI.second.getReg());
    }
  }

  // Check if \p FI is allocated for any SGPR spill to a VGPR lane during PEI.
  bool checkIndexInPrologEpilogSGPRSpills(int FI) const {
    return find_if(PrologEpilogSGPRSpills,
                   [FI](const std::pair<Register,
                                        PrologEpilogSGPRSaveRestoreInfo> &SI) {
                     return SI.second.getKind() ==
                                SGPRSaveKind::SPILL_TO_VGPR_LANE &&
                            SI.second.getIndex() == FI;
                   }) != PrologEpilogSGPRSpills.end();
  }

  const PrologEpilogSGPRSaveRestoreInfo &
  getPrologEpilogSGPRSaveRestoreInfo(Register Reg) const {
    auto I = find_if(PrologEpilogSGPRSpills,
                     [&Reg](const auto &Spill) { return Spill.first == Reg; });
    assert(I != PrologEpilogSGPRSpills.end());

    return I->second;
  }

  ArrayRef<SIRegisterInfo::SpilledReg>
  getSGPRSpillToPhysicalVGPRLanes(int FrameIndex) const {
    auto I = SGPRSpillsToPhysicalVGPRLanes.find(FrameIndex);
    return (I == SGPRSpillsToPhysicalVGPRLanes.end())
               ? ArrayRef<SIRegisterInfo::SpilledReg>()
               : ArrayRef(I->second);
  }

  void setFlag(Register Reg, uint8_t Flag) {
    assert(Reg.isVirtual());
    if (VRegFlags.inBounds(Reg))
      VRegFlags[Reg] |= Flag;
  }

  bool checkFlag(Register Reg, uint8_t Flag) const {
    if (Reg.isPhysical())
      return false;

    return VRegFlags.inBounds(Reg) && VRegFlags[Reg] & Flag;
  }

  bool hasVRegFlags() { return VRegFlags.size(); }

  void allocateWWMSpill(MachineFunction &MF, Register VGPR, uint64_t Size = 4,
                        Align Alignment = Align(4));

  void splitWWMSpillRegisters(
      MachineFunction &MF,
      SmallVectorImpl<std::pair<Register, int>> &CalleeSavedRegs,
      SmallVectorImpl<std::pair<Register, int>> &ScratchRegs) const;

  ArrayRef<MCPhysReg> getAGPRSpillVGPRs() const {
    return SpillAGPR;
  }

  Register getSGPRForEXECCopy() const { return SGPRForEXECCopy; }

  void setSGPRForEXECCopy(Register Reg) { SGPRForEXECCopy = Reg; }

  ArrayRef<MCPhysReg> getVGPRSpillAGPRs() const {
    return SpillVGPR;
  }

  MCPhysReg getVGPRToAGPRSpill(int FrameIndex, unsigned Lane) const {
    auto I = VGPRToAGPRSpills.find(FrameIndex);
    return (I == VGPRToAGPRSpills.end()) ? (MCPhysReg)AMDGPU::NoRegister
                                         : I->second.Lanes[Lane];
  }

  void setVGPRToAGPRSpillDead(int FrameIndex) {
    auto I = VGPRToAGPRSpills.find(FrameIndex);
    if (I != VGPRToAGPRSpills.end())
      I->second.IsDead = true;
  }

  // To bring the Physical VGPRs in the highest range allocated for CSR SGPR
  // spilling into the lowest available range.
  void shiftSpillPhysVGPRsToLowestRange(MachineFunction &MF);

  bool allocateSGPRSpillToVGPRLane(MachineFunction &MF, int FI,
                                   bool SpillToPhysVGPRLane = false,
                                   bool IsPrologEpilog = false);
  bool allocateVGPRSpillToAGPR(MachineFunction &MF, int FI, bool isAGPRtoVGPR);

  /// If \p ResetSGPRSpillStackIDs is true, reset the stack ID from sgpr-spill
  /// to the default stack.
  bool removeDeadFrameIndices(MachineFrameInfo &MFI,
                              bool ResetSGPRSpillStackIDs);

  int getScavengeFI(MachineFrameInfo &MFI, const SIRegisterInfo &TRI);
  std::optional<int> getOptionalScavengeFI() const { return ScavengeFI; }

  unsigned getBytesInStackArgArea() const {
    return BytesInStackArgArea;
  }

  void setBytesInStackArgArea(unsigned Bytes) {
    BytesInStackArgArea = Bytes;
  }

  // Add user SGPRs.
  Register addPrivateSegmentBuffer(const SIRegisterInfo &TRI);
  Register addDispatchPtr(const SIRegisterInfo &TRI);
  Register addQueuePtr(const SIRegisterInfo &TRI);
  Register addKernargSegmentPtr(const SIRegisterInfo &TRI);
  Register addDispatchID(const SIRegisterInfo &TRI);
  Register addFlatScratchInit(const SIRegisterInfo &TRI);
  Register addPrivateSegmentSize(const SIRegisterInfo &TRI);
  Register addImplicitBufferPtr(const SIRegisterInfo &TRI);
  Register addLDSKernelId();
  SmallVectorImpl<MCRegister> *
  addPreloadedKernArg(const SIRegisterInfo &TRI, const TargetRegisterClass *RC,
                      unsigned AllocSizeDWord, int KernArgIdx,
                      int PaddingSGPRs);

  /// Increment user SGPRs used for padding the argument list only.
  Register addReservedUserSGPR() {
    Register Next = getNextUserSGPR();
    ++NumUserSGPRs;
    return Next;
  }

  // Add system SGPRs.
  Register addWorkGroupIDX() {
    ArgInfo.WorkGroupIDX = ArgDescriptor::createRegister(getNextSystemSGPR());
    NumSystemSGPRs += 1;
    return ArgInfo.WorkGroupIDX.getRegister();
  }

  Register addWorkGroupIDY() {
    ArgInfo.WorkGroupIDY = ArgDescriptor::createRegister(getNextSystemSGPR());
    NumSystemSGPRs += 1;
    return ArgInfo.WorkGroupIDY.getRegister();
  }

  Register addWorkGroupIDZ() {
    ArgInfo.WorkGroupIDZ = ArgDescriptor::createRegister(getNextSystemSGPR());
    NumSystemSGPRs += 1;
    return ArgInfo.WorkGroupIDZ.getRegister();
  }

  Register addWorkGroupInfo() {
    ArgInfo.WorkGroupInfo = ArgDescriptor::createRegister(getNextSystemSGPR());
    NumSystemSGPRs += 1;
    return ArgInfo.WorkGroupInfo.getRegister();
  }

  bool hasLDSKernelId() const { return LDSKernelId; }

  // Add special VGPR inputs
  void setWorkItemIDX(ArgDescriptor Arg) {
    ArgInfo.WorkItemIDX = Arg;
  }

  void setWorkItemIDY(ArgDescriptor Arg) {
    ArgInfo.WorkItemIDY = Arg;
  }

  void setWorkItemIDZ(ArgDescriptor Arg) {
    ArgInfo.WorkItemIDZ = Arg;
  }

  Register addPrivateSegmentWaveByteOffset() {
    ArgInfo.PrivateSegmentWaveByteOffset
      = ArgDescriptor::createRegister(getNextSystemSGPR());
    NumSystemSGPRs += 1;
    return ArgInfo.PrivateSegmentWaveByteOffset.getRegister();
  }

  void setPrivateSegmentWaveByteOffset(Register Reg) {
    ArgInfo.PrivateSegmentWaveByteOffset = ArgDescriptor::createRegister(Reg);
  }

  bool hasWorkGroupIDX() const {
    return WorkGroupIDX;
  }

  bool hasWorkGroupIDY() const {
    return WorkGroupIDY;
  }

  bool hasWorkGroupIDZ() const {
    return WorkGroupIDZ;
  }

  bool hasWorkGroupInfo() const {
    return WorkGroupInfo;
  }

  bool hasPrivateSegmentWaveByteOffset() const {
    return PrivateSegmentWaveByteOffset;
  }

  bool hasWorkItemIDX() const {
    return WorkItemIDX;
  }

  bool hasWorkItemIDY() const {
    return WorkItemIDY;
  }

  bool hasWorkItemIDZ() const {
    return WorkItemIDZ;
  }

  bool hasImplicitArgPtr() const {
    return ImplicitArgPtr;
  }

  AMDGPUFunctionArgInfo &getArgInfo() {
    return ArgInfo;
  }

  const AMDGPUFunctionArgInfo &getArgInfo() const {
    return ArgInfo;
  }

  std::tuple<const ArgDescriptor *, const TargetRegisterClass *, LLT>
  getPreloadedValue(AMDGPUFunctionArgInfo::PreloadedValue Value) const {
    return ArgInfo.getPreloadedValue(Value);
  }

  MCRegister getPreloadedReg(AMDGPUFunctionArgInfo::PreloadedValue Value) const {
    auto Arg = std::get<0>(ArgInfo.getPreloadedValue(Value));
    return Arg ? Arg->getRegister() : MCRegister();
  }

  unsigned getGITPtrHigh() const {
    return GITPtrHigh;
  }

  Register getGITPtrLoReg(const MachineFunction &MF) const;

  uint32_t get32BitAddressHighBits() const {
    return HighBitsOf32BitAddress;
  }

  unsigned getNumUserSGPRs() const {
    return NumUserSGPRs;
  }

  unsigned getNumPreloadedSGPRs() const {
    return NumUserSGPRs + NumSystemSGPRs;
  }

  unsigned getNumKernargPreloadedSGPRs() const {
    return UserSGPRInfo.getNumKernargPreloadSGPRs();
  }

  Register getPrivateSegmentWaveByteOffsetSystemSGPR() const {
    return ArgInfo.PrivateSegmentWaveByteOffset.getRegister();
  }

  /// Returns the physical register reserved for use as the resource
  /// descriptor for scratch accesses.
  Register getScratchRSrcReg() const {
    return ScratchRSrcReg;
  }

  void setScratchRSrcReg(Register Reg) {
    assert(Reg != 0 && "Should never be unset");
    ScratchRSrcReg = Reg;
  }

  Register getFrameOffsetReg() const {
    return FrameOffsetReg;
  }

  void setFrameOffsetReg(Register Reg) {
    assert(Reg != 0 && "Should never be unset");
    FrameOffsetReg = Reg;
  }

  void setStackPtrOffsetReg(Register Reg) {
    assert(Reg != 0 && "Should never be unset");
    StackPtrOffsetReg = Reg;
  }

  void setLongBranchReservedReg(Register Reg) { LongBranchReservedReg = Reg; }

  // Note the unset value for this is AMDGPU::SP_REG rather than
  // NoRegister. This is mostly a workaround for MIR tests where state that
  // can't be directly computed from the function is not preserved in serialized
  // MIR.
  Register getStackPtrOffsetReg() const {
    return StackPtrOffsetReg;
  }

  Register getLongBranchReservedReg() const { return LongBranchReservedReg; }

  Register getQueuePtrUserSGPR() const {
    return ArgInfo.QueuePtr.getRegister();
  }

  Register getImplicitBufferPtrUserSGPR() const {
    return ArgInfo.ImplicitBufferPtr.getRegister();
  }

  bool hasSpilledSGPRs() const {
    return HasSpilledSGPRs;
  }

  void setHasSpilledSGPRs(bool Spill = true) {
    HasSpilledSGPRs = Spill;
  }

  bool hasSpilledVGPRs() const {
    return HasSpilledVGPRs;
  }

  void setHasSpilledVGPRs(bool Spill = true) {
    HasSpilledVGPRs = Spill;
  }

  bool hasNonSpillStackObjects() const {
    return HasNonSpillStackObjects;
  }

  void setHasNonSpillStackObjects(bool StackObject = true) {
    HasNonSpillStackObjects = StackObject;
  }

  bool isStackRealigned() const {
    return IsStackRealigned;
  }

  void setIsStackRealigned(bool Realigned = true) {
    IsStackRealigned = Realigned;
  }

  unsigned getNumSpilledSGPRs() const {
    return NumSpilledSGPRs;
  }

  unsigned getNumSpilledVGPRs() const {
    return NumSpilledVGPRs;
  }

  void addToSpilledSGPRs(unsigned num) {
    NumSpilledSGPRs += num;
  }

  void addToSpilledVGPRs(unsigned num) {
    NumSpilledVGPRs += num;
  }

  unsigned getPSInputAddr() const {
    return PSInputAddr;
  }

  unsigned getPSInputEnable() const {
    return PSInputEnable;
  }

  bool isPSInputAllocated(unsigned Index) const {
    return PSInputAddr & (1 << Index);
  }

  void markPSInputAllocated(unsigned Index) {
    PSInputAddr |= 1 << Index;
  }

  void markPSInputEnabled(unsigned Index) {
    PSInputEnable |= 1 << Index;
  }

  bool returnsVoid() const {
    return ReturnsVoid;
  }

  void setIfReturnsVoid(bool Value) {
    ReturnsVoid = Value;
  }

  /// \returns A pair of default/requested minimum/maximum flat work group sizes
  /// for this function.
  std::pair<unsigned, unsigned> getFlatWorkGroupSizes() const {
    return FlatWorkGroupSizes;
  }

  /// \returns Default/requested minimum flat work group size for this function.
  unsigned getMinFlatWorkGroupSize() const {
    return FlatWorkGroupSizes.first;
  }

  /// \returns Default/requested maximum flat work group size for this function.
  unsigned getMaxFlatWorkGroupSize() const {
    return FlatWorkGroupSizes.second;
  }

  /// \returns A pair of default/requested minimum/maximum number of waves per
  /// execution unit.
  std::pair<unsigned, unsigned> getWavesPerEU() const {
    return WavesPerEU;
  }

  /// \returns Default/requested minimum number of waves per execution unit.
  unsigned getMinWavesPerEU() const {
    return WavesPerEU.first;
  }

  /// \returns Default/requested maximum number of waves per execution unit.
  unsigned getMaxWavesPerEU() const {
    return WavesPerEU.second;
  }

  const AMDGPUGWSResourcePseudoSourceValue *
  getGWSPSV(const AMDGPUTargetMachine &TM) {
    return &GWSResourcePSV;
  }

  unsigned getOccupancy() const {
    return Occupancy;
  }

  unsigned getMinAllowedOccupancy() const {
    if (!isMemoryBound() && !needsWaveLimiter())
      return Occupancy;
    return (Occupancy < 4) ? Occupancy : 4;
  }

  void limitOccupancy(const MachineFunction &MF);

  void limitOccupancy(unsigned Limit) {
    if (Occupancy > Limit)
      Occupancy = Limit;
  }

  void increaseOccupancy(const MachineFunction &MF, unsigned Limit) {
    if (Occupancy < Limit)
      Occupancy = Limit;
    limitOccupancy(MF);
  }

  bool mayNeedAGPRs() const {
    return MayNeedAGPRs;
  }

  // \returns true if a function has a use of AGPRs via inline asm or
  // has a call which may use it.
  bool mayUseAGPRs(const Function &F) const;

  // \returns true if a function needs or may need AGPRs.
  bool usesAGPRs(const MachineFunction &MF) const;

  /// \returns Default/requested number of work groups for this function.
  SmallVector<unsigned> getMaxNumWorkGroups() const { return MaxNumWorkGroups; }

  unsigned getMaxNumWorkGroupsX() const { return MaxNumWorkGroups[0]; }
  unsigned getMaxNumWorkGroupsY() const { return MaxNumWorkGroups[1]; }
  unsigned getMaxNumWorkGroupsZ() const { return MaxNumWorkGroups[2]; }
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_SIMACHINEFUNCTIONINFO_H
