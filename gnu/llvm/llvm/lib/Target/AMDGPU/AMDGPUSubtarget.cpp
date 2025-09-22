//===-- AMDGPUSubtarget.cpp - AMDGPU Subtarget Information ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Implements the AMDGPU specific subclass of TargetSubtarget.
//
//===----------------------------------------------------------------------===//

#include "AMDGPUSubtarget.h"
#include "AMDGPUCallLowering.h"
#include "AMDGPUInstructionSelector.h"
#include "AMDGPULegalizerInfo.h"
#include "AMDGPURegisterBankInfo.h"
#include "AMDGPUTargetMachine.h"
#include "GCNSubtarget.h"
#include "R600Subtarget.h"
#include "SIMachineFunctionInfo.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/CodeGen/GlobalISel/InlineAsmLowering.h"
#include "llvm/CodeGen/MachineScheduler.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/IntrinsicsR600.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include <algorithm>

using namespace llvm;

#define DEBUG_TYPE "amdgpu-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#define AMDGPUSubtarget GCNSubtarget
#include "AMDGPUGenSubtargetInfo.inc"
#undef AMDGPUSubtarget

static cl::opt<bool> EnablePowerSched(
  "amdgpu-enable-power-sched",
  cl::desc("Enable scheduling to minimize mAI power bursts"),
  cl::init(false));

static cl::opt<bool> EnableVGPRIndexMode(
  "amdgpu-vgpr-index-mode",
  cl::desc("Use GPR indexing mode instead of movrel for vector indexing"),
  cl::init(false));

static cl::opt<bool> UseAA("amdgpu-use-aa-in-codegen",
                           cl::desc("Enable the use of AA during codegen."),
                           cl::init(true));

static cl::opt<unsigned> NSAThreshold("amdgpu-nsa-threshold",
                                      cl::desc("Number of addresses from which to enable MIMG NSA."),
                                      cl::init(3), cl::Hidden);

GCNSubtarget::~GCNSubtarget() = default;

GCNSubtarget &
GCNSubtarget::initializeSubtargetDependencies(const Triple &TT,
                                              StringRef GPU, StringRef FS) {
  // Determine default and user-specified characteristics
  //
  // We want to be able to turn these off, but making this a subtarget feature
  // for SI has the unhelpful behavior that it unsets everything else if you
  // disable it.
  //
  // Similarly we want enable-prt-strict-null to be on by default and not to
  // unset everything else if it is disabled

  SmallString<256> FullFS("+promote-alloca,+load-store-opt,+enable-ds128,");

  // Turn on features that HSA ABI requires. Also turn on FlatForGlobal by default
  if (isAmdHsaOS())
    FullFS += "+flat-for-global,+unaligned-access-mode,+trap-handler,";

  FullFS += "+enable-prt-strict-null,"; // This is overridden by a disable in FS

  // Disable mutually exclusive bits.
  if (FS.contains_insensitive("+wavefrontsize")) {
    if (!FS.contains_insensitive("wavefrontsize16"))
      FullFS += "-wavefrontsize16,";
    if (!FS.contains_insensitive("wavefrontsize32"))
      FullFS += "-wavefrontsize32,";
    if (!FS.contains_insensitive("wavefrontsize64"))
      FullFS += "-wavefrontsize64,";
  }

  FullFS += FS;

  ParseSubtargetFeatures(GPU, /*TuneCPU*/ GPU, FullFS);

  // Implement the "generic" processors, which acts as the default when no
  // generation features are enabled (e.g for -mcpu=''). HSA OS defaults to
  // the first amdgcn target that supports flat addressing. Other OSes defaults
  // to the first amdgcn target.
  if (Gen == AMDGPUSubtarget::INVALID) {
     Gen = TT.getOS() == Triple::AMDHSA ? AMDGPUSubtarget::SEA_ISLANDS
                                        : AMDGPUSubtarget::SOUTHERN_ISLANDS;
  }

  if (!hasFeature(AMDGPU::FeatureWavefrontSize32) &&
      !hasFeature(AMDGPU::FeatureWavefrontSize64)) {
    // If there is no default wave size it must be a generation before gfx10,
    // these have FeatureWavefrontSize64 in their definition already. For gfx10+
    // set wave32 as a default.
    ToggleFeature(AMDGPU::FeatureWavefrontSize32);
  }

  // We don't support FP64 for EG/NI atm.
  assert(!hasFP64() || (getGeneration() >= AMDGPUSubtarget::SOUTHERN_ISLANDS));

  // Targets must either support 64-bit offsets for MUBUF instructions, and/or
  // support flat operations, otherwise they cannot access a 64-bit global
  // address space
  assert(hasAddr64() || hasFlat());
  // Unless +-flat-for-global is specified, turn on FlatForGlobal for targets
  // that do not support ADDR64 variants of MUBUF instructions. Such targets
  // cannot use a 64 bit offset with a MUBUF instruction to access the global
  // address space
  if (!hasAddr64() && !FS.contains("flat-for-global") && !FlatForGlobal) {
    ToggleFeature(AMDGPU::FeatureFlatForGlobal);
    FlatForGlobal = true;
  }
  // Unless +-flat-for-global is specified, use MUBUF instructions for global
  // address space access if flat operations are not available.
  if (!hasFlat() && !FS.contains("flat-for-global") && FlatForGlobal) {
    ToggleFeature(AMDGPU::FeatureFlatForGlobal);
    FlatForGlobal = false;
  }

  // Set defaults if needed.
  if (MaxPrivateElementSize == 0)
    MaxPrivateElementSize = 4;

  if (LDSBankCount == 0)
    LDSBankCount = 32;

  if (TT.getArch() == Triple::amdgcn) {
    if (LocalMemorySize == 0)
      LocalMemorySize = 32768;

    // Do something sensible for unspecified target.
    if (!HasMovrel && !HasVGPRIndexMode)
      HasMovrel = true;
  }

  AddressableLocalMemorySize = LocalMemorySize;

  if (AMDGPU::isGFX10Plus(*this) &&
      !getFeatureBits().test(AMDGPU::FeatureCuMode))
    LocalMemorySize *= 2;

  // Don't crash on invalid devices.
  if (WavefrontSizeLog2 == 0)
    WavefrontSizeLog2 = 5;

  HasFminFmaxLegacy = getGeneration() < AMDGPUSubtarget::VOLCANIC_ISLANDS;
  HasSMulHi = getGeneration() >= AMDGPUSubtarget::GFX9;

  TargetID.setTargetIDFromFeaturesString(FS);

  LLVM_DEBUG(dbgs() << "xnack setting for subtarget: "
                    << TargetID.getXnackSetting() << '\n');
  LLVM_DEBUG(dbgs() << "sramecc setting for subtarget: "
                    << TargetID.getSramEccSetting() << '\n');

  return *this;
}

void GCNSubtarget::checkSubtargetFeatures(const Function &F) const {
  LLVMContext &Ctx = F.getContext();
  if (hasFeature(AMDGPU::FeatureWavefrontSize32) ==
      hasFeature(AMDGPU::FeatureWavefrontSize64)) {
    Ctx.diagnose(DiagnosticInfoUnsupported(
        F, "must specify exactly one of wavefrontsize32 and wavefrontsize64"));
  }
}

AMDGPUSubtarget::AMDGPUSubtarget(Triple TT) : TargetTriple(std::move(TT)) {}

bool AMDGPUSubtarget::useRealTrue16Insts() const {
  return hasTrue16BitInsts() && EnableRealTrue16Insts;
}

GCNSubtarget::GCNSubtarget(const Triple &TT, StringRef GPU, StringRef FS,
                           const GCNTargetMachine &TM)
    : // clang-format off
    AMDGPUGenSubtargetInfo(TT, GPU, /*TuneCPU*/ GPU, FS),
    AMDGPUSubtarget(TT),
    TargetTriple(TT),
    TargetID(*this),
    InstrItins(getInstrItineraryForCPU(GPU)),
    InstrInfo(initializeSubtargetDependencies(TT, GPU, FS)),
    TLInfo(TM, *this),
    FrameLowering(TargetFrameLowering::StackGrowsUp, getStackAlignment(), 0) {
  // clang-format on
  MaxWavesPerEU = AMDGPU::IsaInfo::getMaxWavesPerEU(this);
  EUsPerCU = AMDGPU::IsaInfo::getEUsPerCU(this);
  CallLoweringInfo = std::make_unique<AMDGPUCallLowering>(*getTargetLowering());
  InlineAsmLoweringInfo =
      std::make_unique<InlineAsmLowering>(getTargetLowering());
  Legalizer = std::make_unique<AMDGPULegalizerInfo>(*this, TM);
  RegBankInfo = std::make_unique<AMDGPURegisterBankInfo>(*this);
  InstSelector =
      std::make_unique<AMDGPUInstructionSelector>(*this, *RegBankInfo, TM);
}

unsigned GCNSubtarget::getConstantBusLimit(unsigned Opcode) const {
  if (getGeneration() < GFX10)
    return 1;

  switch (Opcode) {
  case AMDGPU::V_LSHLREV_B64_e64:
  case AMDGPU::V_LSHLREV_B64_gfx10:
  case AMDGPU::V_LSHLREV_B64_e64_gfx11:
  case AMDGPU::V_LSHLREV_B64_e32_gfx12:
  case AMDGPU::V_LSHLREV_B64_e64_gfx12:
  case AMDGPU::V_LSHL_B64_e64:
  case AMDGPU::V_LSHRREV_B64_e64:
  case AMDGPU::V_LSHRREV_B64_gfx10:
  case AMDGPU::V_LSHRREV_B64_e64_gfx11:
  case AMDGPU::V_LSHRREV_B64_e64_gfx12:
  case AMDGPU::V_LSHR_B64_e64:
  case AMDGPU::V_ASHRREV_I64_e64:
  case AMDGPU::V_ASHRREV_I64_gfx10:
  case AMDGPU::V_ASHRREV_I64_e64_gfx11:
  case AMDGPU::V_ASHRREV_I64_e64_gfx12:
  case AMDGPU::V_ASHR_I64_e64:
    return 1;
  }

  return 2;
}

/// This list was mostly derived from experimentation.
bool GCNSubtarget::zeroesHigh16BitsOfDest(unsigned Opcode) const {
  switch (Opcode) {
  case AMDGPU::V_CVT_F16_F32_e32:
  case AMDGPU::V_CVT_F16_F32_e64:
  case AMDGPU::V_CVT_F16_U16_e32:
  case AMDGPU::V_CVT_F16_U16_e64:
  case AMDGPU::V_CVT_F16_I16_e32:
  case AMDGPU::V_CVT_F16_I16_e64:
  case AMDGPU::V_RCP_F16_e64:
  case AMDGPU::V_RCP_F16_e32:
  case AMDGPU::V_RSQ_F16_e64:
  case AMDGPU::V_RSQ_F16_e32:
  case AMDGPU::V_SQRT_F16_e64:
  case AMDGPU::V_SQRT_F16_e32:
  case AMDGPU::V_LOG_F16_e64:
  case AMDGPU::V_LOG_F16_e32:
  case AMDGPU::V_EXP_F16_e64:
  case AMDGPU::V_EXP_F16_e32:
  case AMDGPU::V_SIN_F16_e64:
  case AMDGPU::V_SIN_F16_e32:
  case AMDGPU::V_COS_F16_e64:
  case AMDGPU::V_COS_F16_e32:
  case AMDGPU::V_FLOOR_F16_e64:
  case AMDGPU::V_FLOOR_F16_e32:
  case AMDGPU::V_CEIL_F16_e64:
  case AMDGPU::V_CEIL_F16_e32:
  case AMDGPU::V_TRUNC_F16_e64:
  case AMDGPU::V_TRUNC_F16_e32:
  case AMDGPU::V_RNDNE_F16_e64:
  case AMDGPU::V_RNDNE_F16_e32:
  case AMDGPU::V_FRACT_F16_e64:
  case AMDGPU::V_FRACT_F16_e32:
  case AMDGPU::V_FREXP_MANT_F16_e64:
  case AMDGPU::V_FREXP_MANT_F16_e32:
  case AMDGPU::V_FREXP_EXP_I16_F16_e64:
  case AMDGPU::V_FREXP_EXP_I16_F16_e32:
  case AMDGPU::V_LDEXP_F16_e64:
  case AMDGPU::V_LDEXP_F16_e32:
  case AMDGPU::V_LSHLREV_B16_e64:
  case AMDGPU::V_LSHLREV_B16_e32:
  case AMDGPU::V_LSHRREV_B16_e64:
  case AMDGPU::V_LSHRREV_B16_e32:
  case AMDGPU::V_ASHRREV_I16_e64:
  case AMDGPU::V_ASHRREV_I16_e32:
  case AMDGPU::V_ADD_U16_e64:
  case AMDGPU::V_ADD_U16_e32:
  case AMDGPU::V_SUB_U16_e64:
  case AMDGPU::V_SUB_U16_e32:
  case AMDGPU::V_SUBREV_U16_e64:
  case AMDGPU::V_SUBREV_U16_e32:
  case AMDGPU::V_MUL_LO_U16_e64:
  case AMDGPU::V_MUL_LO_U16_e32:
  case AMDGPU::V_ADD_F16_e64:
  case AMDGPU::V_ADD_F16_e32:
  case AMDGPU::V_SUB_F16_e64:
  case AMDGPU::V_SUB_F16_e32:
  case AMDGPU::V_SUBREV_F16_e64:
  case AMDGPU::V_SUBREV_F16_e32:
  case AMDGPU::V_MUL_F16_e64:
  case AMDGPU::V_MUL_F16_e32:
  case AMDGPU::V_MAX_F16_e64:
  case AMDGPU::V_MAX_F16_e32:
  case AMDGPU::V_MIN_F16_e64:
  case AMDGPU::V_MIN_F16_e32:
  case AMDGPU::V_MAX_U16_e64:
  case AMDGPU::V_MAX_U16_e32:
  case AMDGPU::V_MIN_U16_e64:
  case AMDGPU::V_MIN_U16_e32:
  case AMDGPU::V_MAX_I16_e64:
  case AMDGPU::V_MAX_I16_e32:
  case AMDGPU::V_MIN_I16_e64:
  case AMDGPU::V_MIN_I16_e32:
  case AMDGPU::V_MAD_F16_e64:
  case AMDGPU::V_MAD_U16_e64:
  case AMDGPU::V_MAD_I16_e64:
  case AMDGPU::V_FMA_F16_e64:
  case AMDGPU::V_DIV_FIXUP_F16_e64:
    // On gfx10, all 16-bit instructions preserve the high bits.
    return getGeneration() <= AMDGPUSubtarget::GFX9;
  case AMDGPU::V_MADAK_F16:
  case AMDGPU::V_MADMK_F16:
  case AMDGPU::V_MAC_F16_e64:
  case AMDGPU::V_MAC_F16_e32:
  case AMDGPU::V_FMAMK_F16:
  case AMDGPU::V_FMAAK_F16:
  case AMDGPU::V_FMAC_F16_e64:
  case AMDGPU::V_FMAC_F16_e32:
    // In gfx9, the preferred handling of the unused high 16-bits changed. Most
    // instructions maintain the legacy behavior of 0ing. Some instructions
    // changed to preserving the high bits.
    return getGeneration() == AMDGPUSubtarget::VOLCANIC_ISLANDS;
  case AMDGPU::V_MAD_MIXLO_F16:
  case AMDGPU::V_MAD_MIXHI_F16:
  default:
    return false;
  }
}

// Returns the maximum per-workgroup LDS allocation size (in bytes) that still
// allows the given function to achieve an occupancy of NWaves waves per
// SIMD / EU, taking into account only the function's *maximum* workgroup size.
unsigned
AMDGPUSubtarget::getMaxLocalMemSizeWithWaveCount(unsigned NWaves,
                                                 const Function &F) const {
  const unsigned WaveSize = getWavefrontSize();
  const unsigned WorkGroupSize = getFlatWorkGroupSizes(F).second;
  const unsigned WavesPerWorkgroup =
      std::max(1u, (WorkGroupSize + WaveSize - 1) / WaveSize);

  const unsigned WorkGroupsPerCU =
      std::max(1u, (NWaves * getEUsPerCU()) / WavesPerWorkgroup);

  return getLocalMemorySize() / WorkGroupsPerCU;
}

// FIXME: Should return min,max range.
//
// Returns the maximum occupancy, in number of waves per SIMD / EU, that can
// be achieved when only the given function is running on the machine; and
// taking into account the overall number of wave slots, the (maximum) workgroup
// size, and the per-workgroup LDS allocation size.
unsigned AMDGPUSubtarget::getOccupancyWithLocalMemSize(uint32_t Bytes,
  const Function &F) const {
  const unsigned MaxWorkGroupSize = getFlatWorkGroupSizes(F).second;
  const unsigned MaxWorkGroupsPerCu = getMaxWorkGroupsPerCU(MaxWorkGroupSize);
  if (!MaxWorkGroupsPerCu)
    return 0;

  const unsigned WaveSize = getWavefrontSize();

  // FIXME: Do we need to account for alignment requirement of LDS rounding the
  // size up?
  // Compute restriction based on LDS usage
  unsigned NumGroups = getLocalMemorySize() / (Bytes ? Bytes : 1u);

  // This can be queried with more LDS than is possible, so just assume the
  // worst.
  if (NumGroups == 0)
    return 1;

  NumGroups = std::min(MaxWorkGroupsPerCu, NumGroups);

  // Round to the number of waves per CU.
  const unsigned MaxGroupNumWaves = divideCeil(MaxWorkGroupSize, WaveSize);
  unsigned MaxWaves = NumGroups * MaxGroupNumWaves;

  // Number of waves per EU (SIMD).
  MaxWaves = divideCeil(MaxWaves, getEUsPerCU());

  // Clamp to the maximum possible number of waves.
  MaxWaves = std::min(MaxWaves, getMaxWavesPerEU());

  // FIXME: Needs to be a multiple of the group size?
  //MaxWaves = MaxGroupNumWaves * (MaxWaves / MaxGroupNumWaves);

  assert(MaxWaves > 0 && MaxWaves <= getMaxWavesPerEU() &&
         "computed invalid occupancy");
  return MaxWaves;
}

unsigned
AMDGPUSubtarget::getOccupancyWithLocalMemSize(const MachineFunction &MF) const {
  const auto *MFI = MF.getInfo<SIMachineFunctionInfo>();
  return getOccupancyWithLocalMemSize(MFI->getLDSSize(), MF.getFunction());
}

std::pair<unsigned, unsigned>
AMDGPUSubtarget::getDefaultFlatWorkGroupSize(CallingConv::ID CC) const {
  switch (CC) {
  case CallingConv::AMDGPU_VS:
  case CallingConv::AMDGPU_LS:
  case CallingConv::AMDGPU_HS:
  case CallingConv::AMDGPU_ES:
  case CallingConv::AMDGPU_GS:
  case CallingConv::AMDGPU_PS:
    return std::pair(1, getWavefrontSize());
  default:
    return std::pair(1u, getMaxFlatWorkGroupSize());
  }
}

std::pair<unsigned, unsigned> AMDGPUSubtarget::getFlatWorkGroupSizes(
  const Function &F) const {
  // Default minimum/maximum flat work group sizes.
  std::pair<unsigned, unsigned> Default =
    getDefaultFlatWorkGroupSize(F.getCallingConv());

  // Requested minimum/maximum flat work group sizes.
  std::pair<unsigned, unsigned> Requested = AMDGPU::getIntegerPairAttribute(
    F, "amdgpu-flat-work-group-size", Default);

  // Make sure requested minimum is less than requested maximum.
  if (Requested.first > Requested.second)
    return Default;

  // Make sure requested values do not violate subtarget's specifications.
  if (Requested.first < getMinFlatWorkGroupSize())
    return Default;
  if (Requested.second > getMaxFlatWorkGroupSize())
    return Default;

  return Requested;
}

std::pair<unsigned, unsigned> AMDGPUSubtarget::getEffectiveWavesPerEU(
    std::pair<unsigned, unsigned> Requested,
    std::pair<unsigned, unsigned> FlatWorkGroupSizes) const {
  // Default minimum/maximum number of waves per execution unit.
  std::pair<unsigned, unsigned> Default(1, getMaxWavesPerEU());

  // If minimum/maximum flat work group sizes were explicitly requested using
  // "amdgpu-flat-workgroup-size" attribute, then set default minimum/maximum
  // number of waves per execution unit to values implied by requested
  // minimum/maximum flat work group sizes.
  unsigned MinImpliedByFlatWorkGroupSize =
    getWavesPerEUForWorkGroup(FlatWorkGroupSizes.second);
  Default.first = MinImpliedByFlatWorkGroupSize;

  // Make sure requested minimum is less than requested maximum.
  if (Requested.second && Requested.first > Requested.second)
    return Default;

  // Make sure requested values do not violate subtarget's specifications.
  if (Requested.first < getMinWavesPerEU() ||
      Requested.second > getMaxWavesPerEU())
    return Default;

  // Make sure requested values are compatible with values implied by requested
  // minimum/maximum flat work group sizes.
  if (Requested.first < MinImpliedByFlatWorkGroupSize)
    return Default;

  return Requested;
}

std::pair<unsigned, unsigned> AMDGPUSubtarget::getWavesPerEU(
    const Function &F, std::pair<unsigned, unsigned> FlatWorkGroupSizes) const {
  // Default minimum/maximum number of waves per execution unit.
  std::pair<unsigned, unsigned> Default(1, getMaxWavesPerEU());

  // Requested minimum/maximum number of waves per execution unit.
  std::pair<unsigned, unsigned> Requested =
      AMDGPU::getIntegerPairAttribute(F, "amdgpu-waves-per-eu", Default, true);
  return getEffectiveWavesPerEU(Requested, FlatWorkGroupSizes);
}

static unsigned getReqdWorkGroupSize(const Function &Kernel, unsigned Dim) {
  auto Node = Kernel.getMetadata("reqd_work_group_size");
  if (Node && Node->getNumOperands() == 3)
    return mdconst::extract<ConstantInt>(Node->getOperand(Dim))->getZExtValue();
  return std::numeric_limits<unsigned>::max();
}

bool AMDGPUSubtarget::isMesaKernel(const Function &F) const {
  return isMesa3DOS() && !AMDGPU::isShader(F.getCallingConv());
}

unsigned AMDGPUSubtarget::getMaxWorkitemID(const Function &Kernel,
                                           unsigned Dimension) const {
  unsigned ReqdSize = getReqdWorkGroupSize(Kernel, Dimension);
  if (ReqdSize != std::numeric_limits<unsigned>::max())
    return ReqdSize - 1;
  return getFlatWorkGroupSizes(Kernel).second - 1;
}

bool AMDGPUSubtarget::isSingleLaneExecution(const Function &Func) const {
  for (int I = 0; I < 3; ++I) {
    if (getMaxWorkitemID(Func, I) > 0)
      return false;
  }

  return true;
}

bool AMDGPUSubtarget::makeLIDRangeMetadata(Instruction *I) const {
  Function *Kernel = I->getParent()->getParent();
  unsigned MinSize = 0;
  unsigned MaxSize = getFlatWorkGroupSizes(*Kernel).second;
  bool IdQuery = false;

  // If reqd_work_group_size is present it narrows value down.
  if (auto *CI = dyn_cast<CallInst>(I)) {
    const Function *F = CI->getCalledFunction();
    if (F) {
      unsigned Dim = UINT_MAX;
      switch (F->getIntrinsicID()) {
      case Intrinsic::amdgcn_workitem_id_x:
      case Intrinsic::r600_read_tidig_x:
        IdQuery = true;
        [[fallthrough]];
      case Intrinsic::r600_read_local_size_x:
        Dim = 0;
        break;
      case Intrinsic::amdgcn_workitem_id_y:
      case Intrinsic::r600_read_tidig_y:
        IdQuery = true;
        [[fallthrough]];
      case Intrinsic::r600_read_local_size_y:
        Dim = 1;
        break;
      case Intrinsic::amdgcn_workitem_id_z:
      case Intrinsic::r600_read_tidig_z:
        IdQuery = true;
        [[fallthrough]];
      case Intrinsic::r600_read_local_size_z:
        Dim = 2;
        break;
      default:
        break;
      }

      if (Dim <= 3) {
        unsigned ReqdSize = getReqdWorkGroupSize(*Kernel, Dim);
        if (ReqdSize != std::numeric_limits<unsigned>::max())
          MinSize = MaxSize = ReqdSize;
      }
    }
  }

  if (!MaxSize)
    return false;

  // Range metadata is [Lo, Hi). For ID query we need to pass max size
  // as Hi. For size query we need to pass Hi + 1.
  if (IdQuery)
    MinSize = 0;
  else
    ++MaxSize;

  APInt Lower{32, MinSize};
  APInt Upper{32, MaxSize};
  if (auto *CI = dyn_cast<CallBase>(I)) {
    ConstantRange Range(Lower, Upper);
    CI->addRangeRetAttr(Range);
  } else {
    MDBuilder MDB(I->getContext());
    MDNode *MaxWorkGroupSizeRange = MDB.createRange(Lower, Upper);
    I->setMetadata(LLVMContext::MD_range, MaxWorkGroupSizeRange);
  }
  return true;
}

unsigned AMDGPUSubtarget::getImplicitArgNumBytes(const Function &F) const {
  assert(AMDGPU::isKernel(F.getCallingConv()));

  // We don't allocate the segment if we know the implicit arguments weren't
  // used, even if the ABI implies we need them.
  if (F.hasFnAttribute("amdgpu-no-implicitarg-ptr"))
    return 0;

  if (isMesaKernel(F))
    return 16;

  // Assume all implicit inputs are used by default
  const Module *M = F.getParent();
  unsigned NBytes =
      AMDGPU::getAMDHSACodeObjectVersion(*M) >= AMDGPU::AMDHSA_COV5 ? 256 : 56;
  return F.getFnAttributeAsParsedInteger("amdgpu-implicitarg-num-bytes",
                                         NBytes);
}

uint64_t AMDGPUSubtarget::getExplicitKernArgSize(const Function &F,
                                                 Align &MaxAlign) const {
  assert(F.getCallingConv() == CallingConv::AMDGPU_KERNEL ||
         F.getCallingConv() == CallingConv::SPIR_KERNEL);

  const DataLayout &DL = F.getDataLayout();
  uint64_t ExplicitArgBytes = 0;
  MaxAlign = Align(1);

  for (const Argument &Arg : F.args()) {
    const bool IsByRef = Arg.hasByRefAttr();
    Type *ArgTy = IsByRef ? Arg.getParamByRefType() : Arg.getType();
    Align Alignment = DL.getValueOrABITypeAlignment(
        IsByRef ? Arg.getParamAlign() : std::nullopt, ArgTy);
    uint64_t AllocSize = DL.getTypeAllocSize(ArgTy);
    ExplicitArgBytes = alignTo(ExplicitArgBytes, Alignment) + AllocSize;
    MaxAlign = std::max(MaxAlign, Alignment);
  }

  return ExplicitArgBytes;
}

unsigned AMDGPUSubtarget::getKernArgSegmentSize(const Function &F,
                                                Align &MaxAlign) const {
  if (F.getCallingConv() != CallingConv::AMDGPU_KERNEL &&
      F.getCallingConv() != CallingConv::SPIR_KERNEL)
    return 0;

  uint64_t ExplicitArgBytes = getExplicitKernArgSize(F, MaxAlign);

  unsigned ExplicitOffset = getExplicitKernelArgOffset();

  uint64_t TotalSize = ExplicitOffset + ExplicitArgBytes;
  unsigned ImplicitBytes = getImplicitArgNumBytes(F);
  if (ImplicitBytes != 0) {
    const Align Alignment = getAlignmentForImplicitArgPtr();
    TotalSize = alignTo(ExplicitArgBytes, Alignment) + ImplicitBytes;
    MaxAlign = std::max(MaxAlign, Alignment);
  }

  // Being able to dereference past the end is useful for emitting scalar loads.
  return alignTo(TotalSize, 4);
}

AMDGPUDwarfFlavour AMDGPUSubtarget::getAMDGPUDwarfFlavour() const {
  return getWavefrontSize() == 32 ? AMDGPUDwarfFlavour::Wave32
                                  : AMDGPUDwarfFlavour::Wave64;
}

void GCNSubtarget::overrideSchedPolicy(MachineSchedPolicy &Policy,
                                      unsigned NumRegionInstrs) const {
  // Track register pressure so the scheduler can try to decrease
  // pressure once register usage is above the threshold defined by
  // SIRegisterInfo::getRegPressureSetLimit()
  Policy.ShouldTrackPressure = true;

  // Enabling both top down and bottom up scheduling seems to give us less
  // register spills than just using one of these approaches on its own.
  Policy.OnlyTopDown = false;
  Policy.OnlyBottomUp = false;

  // Enabling ShouldTrackLaneMasks crashes the SI Machine Scheduler.
  if (!enableSIScheduler())
    Policy.ShouldTrackLaneMasks = true;
}

void GCNSubtarget::mirFileLoaded(MachineFunction &MF) const {
  if (isWave32()) {
    // Fix implicit $vcc operands after MIParser has verified that they match
    // the instruction definitions.
    for (auto &MBB : MF) {
      for (auto &MI : MBB)
        InstrInfo.fixImplicitOperands(MI);
    }
  }
}

bool GCNSubtarget::hasMadF16() const {
  return InstrInfo.pseudoToMCOpcode(AMDGPU::V_MAD_F16_e64) != -1;
}

bool GCNSubtarget::useVGPRIndexMode() const {
  return !hasMovrel() || (EnableVGPRIndexMode && hasVGPRIndexMode());
}

bool GCNSubtarget::useAA() const { return UseAA; }

unsigned GCNSubtarget::getOccupancyWithNumSGPRs(unsigned SGPRs) const {
  return AMDGPU::IsaInfo::getOccupancyWithNumSGPRs(SGPRs, getMaxWavesPerEU(),
                                                   getGeneration());
}

unsigned GCNSubtarget::getOccupancyWithNumVGPRs(unsigned NumVGPRs) const {
  return AMDGPU::IsaInfo::getNumWavesPerEUWithNumVGPRs(this, NumVGPRs);
}

unsigned
GCNSubtarget::getBaseReservedNumSGPRs(const bool HasFlatScratch) const {
  if (getGeneration() >= AMDGPUSubtarget::GFX10)
    return 2; // VCC. FLAT_SCRATCH and XNACK are no longer in SGPRs.

  if (HasFlatScratch || HasArchitectedFlatScratch) {
    if (getGeneration() >= AMDGPUSubtarget::VOLCANIC_ISLANDS)
      return 6; // FLAT_SCRATCH, XNACK, VCC (in that order).
    if (getGeneration() == AMDGPUSubtarget::SEA_ISLANDS)
      return 4; // FLAT_SCRATCH, VCC (in that order).
  }

  if (isXNACKEnabled())
    return 4; // XNACK, VCC (in that order).
  return 2; // VCC.
}

unsigned GCNSubtarget::getReservedNumSGPRs(const MachineFunction &MF) const {
  const SIMachineFunctionInfo &MFI = *MF.getInfo<SIMachineFunctionInfo>();
  return getBaseReservedNumSGPRs(MFI.getUserSGPRInfo().hasFlatScratchInit());
}

unsigned GCNSubtarget::getReservedNumSGPRs(const Function &F) const {
  // In principle we do not need to reserve SGPR pair used for flat_scratch if
  // we know flat instructions do not access the stack anywhere in the
  // program. For now assume it's needed if we have flat instructions.
  const bool KernelUsesFlatScratch = hasFlatAddressSpace();
  return getBaseReservedNumSGPRs(KernelUsesFlatScratch);
}

unsigned GCNSubtarget::computeOccupancy(const Function &F, unsigned LDSSize,
                                        unsigned NumSGPRs,
                                        unsigned NumVGPRs) const {
  unsigned Occupancy =
    std::min(getMaxWavesPerEU(),
             getOccupancyWithLocalMemSize(LDSSize, F));
  if (NumSGPRs)
    Occupancy = std::min(Occupancy, getOccupancyWithNumSGPRs(NumSGPRs));
  if (NumVGPRs)
    Occupancy = std::min(Occupancy, getOccupancyWithNumVGPRs(NumVGPRs));
  return Occupancy;
}

unsigned GCNSubtarget::getBaseMaxNumSGPRs(
    const Function &F, std::pair<unsigned, unsigned> WavesPerEU,
    unsigned PreloadedSGPRs, unsigned ReservedNumSGPRs) const {
  // Compute maximum number of SGPRs function can use using default/requested
  // minimum number of waves per execution unit.
  unsigned MaxNumSGPRs = getMaxNumSGPRs(WavesPerEU.first, false);
  unsigned MaxAddressableNumSGPRs = getMaxNumSGPRs(WavesPerEU.first, true);

  // Check if maximum number of SGPRs was explicitly requested using
  // "amdgpu-num-sgpr" attribute.
  if (F.hasFnAttribute("amdgpu-num-sgpr")) {
    unsigned Requested =
        F.getFnAttributeAsParsedInteger("amdgpu-num-sgpr", MaxNumSGPRs);

    // Make sure requested value does not violate subtarget's specifications.
    if (Requested && (Requested <= ReservedNumSGPRs))
      Requested = 0;

    // If more SGPRs are required to support the input user/system SGPRs,
    // increase to accommodate them.
    //
    // FIXME: This really ends up using the requested number of SGPRs + number
    // of reserved special registers in total. Theoretically you could re-use
    // the last input registers for these special registers, but this would
    // require a lot of complexity to deal with the weird aliasing.
    unsigned InputNumSGPRs = PreloadedSGPRs;
    if (Requested && Requested < InputNumSGPRs)
      Requested = InputNumSGPRs;

    // Make sure requested value is compatible with values implied by
    // default/requested minimum/maximum number of waves per execution unit.
    if (Requested && Requested > getMaxNumSGPRs(WavesPerEU.first, false))
      Requested = 0;
    if (WavesPerEU.second &&
        Requested && Requested < getMinNumSGPRs(WavesPerEU.second))
      Requested = 0;

    if (Requested)
      MaxNumSGPRs = Requested;
  }

  if (hasSGPRInitBug())
    MaxNumSGPRs = AMDGPU::IsaInfo::FIXED_NUM_SGPRS_FOR_INIT_BUG;

  return std::min(MaxNumSGPRs - ReservedNumSGPRs, MaxAddressableNumSGPRs);
}

unsigned GCNSubtarget::getMaxNumSGPRs(const MachineFunction &MF) const {
  const Function &F = MF.getFunction();
  const SIMachineFunctionInfo &MFI = *MF.getInfo<SIMachineFunctionInfo>();
  return getBaseMaxNumSGPRs(F, MFI.getWavesPerEU(), MFI.getNumPreloadedSGPRs(),
                            getReservedNumSGPRs(MF));
}

static unsigned getMaxNumPreloadedSGPRs() {
  using USI = GCNUserSGPRUsageInfo;
  // Max number of user SGPRs
  const unsigned MaxUserSGPRs =
      USI::getNumUserSGPRForField(USI::PrivateSegmentBufferID) +
      USI::getNumUserSGPRForField(USI::DispatchPtrID) +
      USI::getNumUserSGPRForField(USI::QueuePtrID) +
      USI::getNumUserSGPRForField(USI::KernargSegmentPtrID) +
      USI::getNumUserSGPRForField(USI::DispatchIdID) +
      USI::getNumUserSGPRForField(USI::FlatScratchInitID) +
      USI::getNumUserSGPRForField(USI::ImplicitBufferPtrID);

  // Max number of system SGPRs
  const unsigned MaxSystemSGPRs = 1 + // WorkGroupIDX
                                  1 + // WorkGroupIDY
                                  1 + // WorkGroupIDZ
                                  1 + // WorkGroupInfo
                                  1;  // private segment wave byte offset

  // Max number of synthetic SGPRs
  const unsigned SyntheticSGPRs = 1; // LDSKernelId

  return MaxUserSGPRs + MaxSystemSGPRs + SyntheticSGPRs;
}

unsigned GCNSubtarget::getMaxNumSGPRs(const Function &F) const {
  return getBaseMaxNumSGPRs(F, getWavesPerEU(F), getMaxNumPreloadedSGPRs(),
                            getReservedNumSGPRs(F));
}

unsigned GCNSubtarget::getBaseMaxNumVGPRs(
    const Function &F, std::pair<unsigned, unsigned> WavesPerEU) const {
  // Compute maximum number of VGPRs function can use using default/requested
  // minimum number of waves per execution unit.
  unsigned MaxNumVGPRs = getMaxNumVGPRs(WavesPerEU.first);

  // Check if maximum number of VGPRs was explicitly requested using
  // "amdgpu-num-vgpr" attribute.
  if (F.hasFnAttribute("amdgpu-num-vgpr")) {
    unsigned Requested =
        F.getFnAttributeAsParsedInteger("amdgpu-num-vgpr", MaxNumVGPRs);

    if (hasGFX90AInsts())
      Requested *= 2;

    // Make sure requested value is compatible with values implied by
    // default/requested minimum/maximum number of waves per execution unit.
    if (Requested && Requested > getMaxNumVGPRs(WavesPerEU.first))
      Requested = 0;
    if (WavesPerEU.second &&
        Requested && Requested < getMinNumVGPRs(WavesPerEU.second))
      Requested = 0;

    if (Requested)
      MaxNumVGPRs = Requested;
  }

  return MaxNumVGPRs;
}

unsigned GCNSubtarget::getMaxNumVGPRs(const Function &F) const {
  return getBaseMaxNumVGPRs(F, getWavesPerEU(F));
}

unsigned GCNSubtarget::getMaxNumVGPRs(const MachineFunction &MF) const {
  const Function &F = MF.getFunction();
  const SIMachineFunctionInfo &MFI = *MF.getInfo<SIMachineFunctionInfo>();
  return getBaseMaxNumVGPRs(F, MFI.getWavesPerEU());
}

void GCNSubtarget::adjustSchedDependency(
    SUnit *Def, int DefOpIdx, SUnit *Use, int UseOpIdx, SDep &Dep,
    const TargetSchedModel *SchedModel) const {
  if (Dep.getKind() != SDep::Kind::Data || !Dep.getReg() ||
      !Def->isInstr() || !Use->isInstr())
    return;

  MachineInstr *DefI = Def->getInstr();
  MachineInstr *UseI = Use->getInstr();

  if (DefI->isBundle()) {
    const SIRegisterInfo *TRI = getRegisterInfo();
    auto Reg = Dep.getReg();
    MachineBasicBlock::const_instr_iterator I(DefI->getIterator());
    MachineBasicBlock::const_instr_iterator E(DefI->getParent()->instr_end());
    unsigned Lat = 0;
    for (++I; I != E && I->isBundledWithPred(); ++I) {
      if (I->modifiesRegister(Reg, TRI))
        Lat = InstrInfo.getInstrLatency(getInstrItineraryData(), *I);
      else if (Lat)
        --Lat;
    }
    Dep.setLatency(Lat);
  } else if (UseI->isBundle()) {
    const SIRegisterInfo *TRI = getRegisterInfo();
    auto Reg = Dep.getReg();
    MachineBasicBlock::const_instr_iterator I(UseI->getIterator());
    MachineBasicBlock::const_instr_iterator E(UseI->getParent()->instr_end());
    unsigned Lat = InstrInfo.getInstrLatency(getInstrItineraryData(), *DefI);
    for (++I; I != E && I->isBundledWithPred() && Lat; ++I) {
      if (I->readsRegister(Reg, TRI))
        break;
      --Lat;
    }
    Dep.setLatency(Lat);
  } else if (Dep.getLatency() == 0 && Dep.getReg() == AMDGPU::VCC_LO) {
    // Work around the fact that SIInstrInfo::fixImplicitOperands modifies
    // implicit operands which come from the MCInstrDesc, which can fool
    // ScheduleDAGInstrs::addPhysRegDataDeps into treating them as implicit
    // pseudo operands.
    Dep.setLatency(InstrInfo.getSchedModel().computeOperandLatency(
        DefI, DefOpIdx, UseI, UseOpIdx));
  }
}

namespace {
struct FillMFMAShadowMutation : ScheduleDAGMutation {
  const SIInstrInfo *TII;

  ScheduleDAGMI *DAG;

  FillMFMAShadowMutation(const SIInstrInfo *tii) : TII(tii) {}

  bool isSALU(const SUnit *SU) const {
    const MachineInstr *MI = SU->getInstr();
    return MI && TII->isSALU(*MI) && !MI->isTerminator();
  }

  bool isVALU(const SUnit *SU) const {
    const MachineInstr *MI = SU->getInstr();
    return MI && TII->isVALU(*MI);
  }

  // Link as many SALU instructions in chain as possible. Return the size
  // of the chain. Links up to MaxChain instructions.
  unsigned linkSALUChain(SUnit *From, SUnit *To, unsigned MaxChain,
                         SmallPtrSetImpl<SUnit *> &Visited) const {
    SmallVector<SUnit *, 8> Worklist({To});
    unsigned Linked = 0;

    while (!Worklist.empty() && MaxChain-- > 0) {
      SUnit *SU = Worklist.pop_back_val();
      if (!Visited.insert(SU).second)
        continue;

      LLVM_DEBUG(dbgs() << "Inserting edge from\n" ; DAG->dumpNode(*From);
                 dbgs() << "to\n"; DAG->dumpNode(*SU); dbgs() << '\n');

      if (SU != From && From != &DAG->ExitSU && DAG->canAddEdge(SU, From))
        if (DAG->addEdge(SU, SDep(From, SDep::Artificial)))
          ++Linked;

      for (SDep &SI : From->Succs) {
        SUnit *SUv = SI.getSUnit();
        if (SUv != From && SU != &DAG->ExitSU && isVALU(SUv) &&
            DAG->canAddEdge(SUv, SU))
          DAG->addEdge(SUv, SDep(SU, SDep::Artificial));
      }

      for (SDep &SI : SU->Succs) {
        SUnit *Succ = SI.getSUnit();
        if (Succ != SU && isSALU(Succ))
          Worklist.push_back(Succ);
      }
    }

    return Linked;
  }

  void apply(ScheduleDAGInstrs *DAGInstrs) override {
    const GCNSubtarget &ST = DAGInstrs->MF.getSubtarget<GCNSubtarget>();
    if (!ST.hasMAIInsts())
      return;
    DAG = static_cast<ScheduleDAGMI*>(DAGInstrs);
    const TargetSchedModel *TSchedModel = DAGInstrs->getSchedModel();
    if (!TSchedModel || DAG->SUnits.empty())
      return;

    // Scan for MFMA long latency instructions and try to add a dependency
    // of available SALU instructions to give them a chance to fill MFMA
    // shadow. That is desirable to fill MFMA shadow with SALU instructions
    // rather than VALU to prevent power consumption bursts and throttle.
    auto LastSALU = DAG->SUnits.begin();
    auto E = DAG->SUnits.end();
    SmallPtrSet<SUnit*, 32> Visited;
    for (SUnit &SU : DAG->SUnits) {
      MachineInstr &MAI = *SU.getInstr();
      if (!TII->isMAI(MAI) ||
           MAI.getOpcode() == AMDGPU::V_ACCVGPR_WRITE_B32_e64 ||
           MAI.getOpcode() == AMDGPU::V_ACCVGPR_READ_B32_e64)
        continue;

      unsigned Lat = TSchedModel->computeInstrLatency(&MAI) - 1;

      LLVM_DEBUG(dbgs() << "Found MFMA: "; DAG->dumpNode(SU);
                 dbgs() << "Need " << Lat
                        << " instructions to cover latency.\n");

      // Find up to Lat independent scalar instructions as early as
      // possible such that they can be scheduled after this MFMA.
      for ( ; Lat && LastSALU != E; ++LastSALU) {
        if (Visited.count(&*LastSALU))
          continue;

        if (&SU == &DAG->ExitSU || &SU == &*LastSALU || !isSALU(&*LastSALU) ||
            !DAG->canAddEdge(&*LastSALU, &SU))
          continue;

        Lat -= linkSALUChain(&SU, &*LastSALU, Lat, Visited);
      }
    }
  }
};
} // namespace

void GCNSubtarget::getPostRAMutations(
    std::vector<std::unique_ptr<ScheduleDAGMutation>> &Mutations) const {
  Mutations.push_back(std::make_unique<FillMFMAShadowMutation>(&InstrInfo));
}

std::unique_ptr<ScheduleDAGMutation>
GCNSubtarget::createFillMFMAShadowMutation(const TargetInstrInfo *TII) const {
  return EnablePowerSched ? std::make_unique<FillMFMAShadowMutation>(&InstrInfo)
                          : nullptr;
}

unsigned GCNSubtarget::getNSAThreshold(const MachineFunction &MF) const {
  if (getGeneration() >= AMDGPUSubtarget::GFX12)
    return 0; // Not MIMG encoding.

  if (NSAThreshold.getNumOccurrences() > 0)
    return std::max(NSAThreshold.getValue(), 2u);

  int Value = MF.getFunction().getFnAttributeAsParsedInteger(
      "amdgpu-nsa-threshold", -1);
  if (Value > 0)
    return std::max(Value, 2);

  return 3;
}

const AMDGPUSubtarget &AMDGPUSubtarget::get(const MachineFunction &MF) {
  if (MF.getTarget().getTargetTriple().getArch() == Triple::amdgcn)
    return static_cast<const AMDGPUSubtarget&>(MF.getSubtarget<GCNSubtarget>());
  return static_cast<const AMDGPUSubtarget &>(MF.getSubtarget<R600Subtarget>());
}

const AMDGPUSubtarget &AMDGPUSubtarget::get(const TargetMachine &TM, const Function &F) {
  if (TM.getTargetTriple().getArch() == Triple::amdgcn)
    return static_cast<const AMDGPUSubtarget&>(TM.getSubtarget<GCNSubtarget>(F));
  return static_cast<const AMDGPUSubtarget &>(
      TM.getSubtarget<R600Subtarget>(F));
}

GCNUserSGPRUsageInfo::GCNUserSGPRUsageInfo(const Function &F,
                                           const GCNSubtarget &ST)
    : ST(ST) {
  const CallingConv::ID CC = F.getCallingConv();
  const bool IsKernel =
      CC == CallingConv::AMDGPU_KERNEL || CC == CallingConv::SPIR_KERNEL;
  // FIXME: Should have analysis or something rather than attribute to detect
  // calls.
  const bool HasCalls = F.hasFnAttribute("amdgpu-calls");
  // FIXME: This attribute is a hack, we just need an analysis on the function
  // to look for allocas.
  const bool HasStackObjects = F.hasFnAttribute("amdgpu-stack-objects");

  if (IsKernel && (!F.arg_empty() || ST.getImplicitArgNumBytes(F) != 0))
    KernargSegmentPtr = true;

  bool IsAmdHsaOrMesa = ST.isAmdHsaOrMesa(F);
  if (IsAmdHsaOrMesa && !ST.enableFlatScratch())
    PrivateSegmentBuffer = true;
  else if (ST.isMesaGfxShader(F))
    ImplicitBufferPtr = true;

  if (!AMDGPU::isGraphics(CC)) {
    if (!F.hasFnAttribute("amdgpu-no-dispatch-ptr"))
      DispatchPtr = true;

    // FIXME: Can this always be disabled with < COv5?
    if (!F.hasFnAttribute("amdgpu-no-queue-ptr"))
      QueuePtr = true;

    if (!F.hasFnAttribute("amdgpu-no-dispatch-id"))
      DispatchID = true;
  }

  // TODO: This could be refined a lot. The attribute is a poor way of
  // detecting calls or stack objects that may require it before argument
  // lowering.
  if (ST.hasFlatAddressSpace() && AMDGPU::isEntryFunctionCC(CC) &&
      (IsAmdHsaOrMesa || ST.enableFlatScratch()) &&
      (HasCalls || HasStackObjects || ST.enableFlatScratch()) &&
      !ST.flatScratchIsArchitected()) {
    FlatScratchInit = true;
  }

  if (hasImplicitBufferPtr())
    NumUsedUserSGPRs += getNumUserSGPRForField(ImplicitBufferPtrID);

  if (hasPrivateSegmentBuffer())
    NumUsedUserSGPRs += getNumUserSGPRForField(PrivateSegmentBufferID);

  if (hasDispatchPtr())
    NumUsedUserSGPRs += getNumUserSGPRForField(DispatchPtrID);

  if (hasQueuePtr())
    NumUsedUserSGPRs += getNumUserSGPRForField(QueuePtrID);

  if (hasKernargSegmentPtr())
    NumUsedUserSGPRs += getNumUserSGPRForField(KernargSegmentPtrID);

  if (hasDispatchID())
    NumUsedUserSGPRs += getNumUserSGPRForField(DispatchIdID);

  if (hasFlatScratchInit())
    NumUsedUserSGPRs += getNumUserSGPRForField(FlatScratchInitID);

  if (hasPrivateSegmentSize())
    NumUsedUserSGPRs += getNumUserSGPRForField(PrivateSegmentSizeID);
}

void GCNUserSGPRUsageInfo::allocKernargPreloadSGPRs(unsigned NumSGPRs) {
  assert(NumKernargPreloadSGPRs + NumSGPRs <= AMDGPU::getMaxNumUserSGPRs(ST));
  NumKernargPreloadSGPRs += NumSGPRs;
  NumUsedUserSGPRs += NumSGPRs;
}

unsigned GCNUserSGPRUsageInfo::getNumFreeUserSGPRs() {
  return AMDGPU::getMaxNumUserSGPRs(ST) - NumUsedUserSGPRs;
}

SmallVector<unsigned>
AMDGPUSubtarget::getMaxNumWorkGroups(const Function &F) const {
  return AMDGPU::getIntegerVecAttribute(F, "amdgpu-max-num-workgroups", 3);
}
