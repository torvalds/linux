//===- AMDGPUBaseInfo.cpp - AMDGPU Base encoding information --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "AMDGPUBaseInfo.h"
#include "AMDGPU.h"
#include "AMDGPUAsmUtils.h"
#include "AMDKernelCodeT.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "Utils/AMDKernelCodeTUtils.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/IntrinsicsR600.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/AMDHSAKernelDescriptor.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/TargetParser/TargetParser.h"
#include <optional>

#define GET_INSTRINFO_NAMED_OPS
#define GET_INSTRMAP_INFO
#include "AMDGPUGenInstrInfo.inc"

static llvm::cl::opt<unsigned> DefaultAMDHSACodeObjectVersion(
    "amdhsa-code-object-version", llvm::cl::Hidden,
    llvm::cl::init(llvm::AMDGPU::AMDHSA_COV5),
    llvm::cl::desc("Set default AMDHSA Code Object Version (module flag "
                   "or asm directive still take priority if present)"));

namespace {

/// \returns Bit mask for given bit \p Shift and bit \p Width.
unsigned getBitMask(unsigned Shift, unsigned Width) {
  return ((1 << Width) - 1) << Shift;
}

/// Packs \p Src into \p Dst for given bit \p Shift and bit \p Width.
///
/// \returns Packed \p Dst.
unsigned packBits(unsigned Src, unsigned Dst, unsigned Shift, unsigned Width) {
  unsigned Mask = getBitMask(Shift, Width);
  return ((Src << Shift) & Mask) | (Dst & ~Mask);
}

/// Unpacks bits from \p Src for given bit \p Shift and bit \p Width.
///
/// \returns Unpacked bits.
unsigned unpackBits(unsigned Src, unsigned Shift, unsigned Width) {
  return (Src & getBitMask(Shift, Width)) >> Shift;
}

/// \returns Vmcnt bit shift (lower bits).
unsigned getVmcntBitShiftLo(unsigned VersionMajor) {
  return VersionMajor >= 11 ? 10 : 0;
}

/// \returns Vmcnt bit width (lower bits).
unsigned getVmcntBitWidthLo(unsigned VersionMajor) {
  return VersionMajor >= 11 ? 6 : 4;
}

/// \returns Expcnt bit shift.
unsigned getExpcntBitShift(unsigned VersionMajor) {
  return VersionMajor >= 11 ? 0 : 4;
}

/// \returns Expcnt bit width.
unsigned getExpcntBitWidth(unsigned VersionMajor) { return 3; }

/// \returns Lgkmcnt bit shift.
unsigned getLgkmcntBitShift(unsigned VersionMajor) {
  return VersionMajor >= 11 ? 4 : 8;
}

/// \returns Lgkmcnt bit width.
unsigned getLgkmcntBitWidth(unsigned VersionMajor) {
  return VersionMajor >= 10 ? 6 : 4;
}

/// \returns Vmcnt bit shift (higher bits).
unsigned getVmcntBitShiftHi(unsigned VersionMajor) { return 14; }

/// \returns Vmcnt bit width (higher bits).
unsigned getVmcntBitWidthHi(unsigned VersionMajor) {
  return (VersionMajor == 9 || VersionMajor == 10) ? 2 : 0;
}

/// \returns Loadcnt bit width
unsigned getLoadcntBitWidth(unsigned VersionMajor) {
  return VersionMajor >= 12 ? 6 : 0;
}

/// \returns Samplecnt bit width.
unsigned getSamplecntBitWidth(unsigned VersionMajor) {
  return VersionMajor >= 12 ? 6 : 0;
}

/// \returns Bvhcnt bit width.
unsigned getBvhcntBitWidth(unsigned VersionMajor) {
  return VersionMajor >= 12 ? 3 : 0;
}

/// \returns Dscnt bit width.
unsigned getDscntBitWidth(unsigned VersionMajor) {
  return VersionMajor >= 12 ? 6 : 0;
}

/// \returns Dscnt bit shift in combined S_WAIT instructions.
unsigned getDscntBitShift(unsigned VersionMajor) { return 0; }

/// \returns Storecnt or Vscnt bit width, depending on VersionMajor.
unsigned getStorecntBitWidth(unsigned VersionMajor) {
  return VersionMajor >= 10 ? 6 : 0;
}

/// \returns Kmcnt bit width.
unsigned getKmcntBitWidth(unsigned VersionMajor) {
  return VersionMajor >= 12 ? 5 : 0;
}

/// \returns shift for Loadcnt/Storecnt in combined S_WAIT instructions.
unsigned getLoadcntStorecntBitShift(unsigned VersionMajor) {
  return VersionMajor >= 12 ? 8 : 0;
}

/// \returns VmVsrc bit width
inline unsigned getVmVsrcBitWidth() { return 3; }

/// \returns VmVsrc bit shift
inline unsigned getVmVsrcBitShift() { return 2; }

/// \returns VaVdst bit width
inline unsigned getVaVdstBitWidth() { return 4; }

/// \returns VaVdst bit shift
inline unsigned getVaVdstBitShift() { return 12; }

/// \returns SaSdst bit width
inline unsigned getSaSdstBitWidth() { return 1; }

/// \returns SaSdst bit shift
inline unsigned getSaSdstBitShift() { return 0; }

} // end anonymous namespace

namespace llvm {

namespace AMDGPU {

/// \returns true if the target supports signed immediate offset for SMRD
/// instructions.
bool hasSMRDSignedImmOffset(const MCSubtargetInfo &ST) {
  return isGFX9Plus(ST);
}

/// \returns True if \p STI is AMDHSA.
bool isHsaAbi(const MCSubtargetInfo &STI) {
  return STI.getTargetTriple().getOS() == Triple::AMDHSA;
}

unsigned getAMDHSACodeObjectVersion(const Module &M) {
  if (auto Ver = mdconst::extract_or_null<ConstantInt>(
          M.getModuleFlag("amdhsa_code_object_version"))) {
    return (unsigned)Ver->getZExtValue() / 100;
  }

  return getDefaultAMDHSACodeObjectVersion();
}

unsigned getDefaultAMDHSACodeObjectVersion() {
  return DefaultAMDHSACodeObjectVersion;
}

unsigned getAMDHSACodeObjectVersion(unsigned ABIVersion) {
  switch (ABIVersion) {
  case ELF::ELFABIVERSION_AMDGPU_HSA_V4:
    return 4;
  case ELF::ELFABIVERSION_AMDGPU_HSA_V5:
    return 5;
  case ELF::ELFABIVERSION_AMDGPU_HSA_V6:
    return 6;
  default:
    return getDefaultAMDHSACodeObjectVersion();
  }
}

uint8_t getELFABIVersion(const Triple &T, unsigned CodeObjectVersion) {
  if (T.getOS() != Triple::AMDHSA)
    return 0;

  switch (CodeObjectVersion) {
  case 4:
    return ELF::ELFABIVERSION_AMDGPU_HSA_V4;
  case 5:
    return ELF::ELFABIVERSION_AMDGPU_HSA_V5;
  case 6:
    return ELF::ELFABIVERSION_AMDGPU_HSA_V6;
  default:
    report_fatal_error("Unsupported AMDHSA Code Object Version " +
                       Twine(CodeObjectVersion));
  }
}

unsigned getMultigridSyncArgImplicitArgPosition(unsigned CodeObjectVersion) {
  switch (CodeObjectVersion) {
  case AMDHSA_COV4:
    return 48;
  case AMDHSA_COV5:
  case AMDHSA_COV6:
  default:
    return AMDGPU::ImplicitArg::MULTIGRID_SYNC_ARG_OFFSET;
  }
}


// FIXME: All such magic numbers about the ABI should be in a
// central TD file.
unsigned getHostcallImplicitArgPosition(unsigned CodeObjectVersion) {
  switch (CodeObjectVersion) {
  case AMDHSA_COV4:
    return 24;
  case AMDHSA_COV5:
  case AMDHSA_COV6:
  default:
    return AMDGPU::ImplicitArg::HOSTCALL_PTR_OFFSET;
  }
}

unsigned getDefaultQueueImplicitArgPosition(unsigned CodeObjectVersion) {
  switch (CodeObjectVersion) {
  case AMDHSA_COV4:
    return 32;
  case AMDHSA_COV5:
  case AMDHSA_COV6:
  default:
    return AMDGPU::ImplicitArg::DEFAULT_QUEUE_OFFSET;
  }
}

unsigned getCompletionActionImplicitArgPosition(unsigned CodeObjectVersion) {
  switch (CodeObjectVersion) {
  case AMDHSA_COV4:
    return 40;
  case AMDHSA_COV5:
  case AMDHSA_COV6:
  default:
    return AMDGPU::ImplicitArg::COMPLETION_ACTION_OFFSET;
  }
}

#define GET_MIMGBaseOpcodesTable_IMPL
#define GET_MIMGDimInfoTable_IMPL
#define GET_MIMGInfoTable_IMPL
#define GET_MIMGLZMappingTable_IMPL
#define GET_MIMGMIPMappingTable_IMPL
#define GET_MIMGBiasMappingTable_IMPL
#define GET_MIMGOffsetMappingTable_IMPL
#define GET_MIMGG16MappingTable_IMPL
#define GET_MAIInstInfoTable_IMPL
#include "AMDGPUGenSearchableTables.inc"

int getMIMGOpcode(unsigned BaseOpcode, unsigned MIMGEncoding,
                  unsigned VDataDwords, unsigned VAddrDwords) {
  const MIMGInfo *Info = getMIMGOpcodeHelper(BaseOpcode, MIMGEncoding,
                                             VDataDwords, VAddrDwords);
  return Info ? Info->Opcode : -1;
}

const MIMGBaseOpcodeInfo *getMIMGBaseOpcode(unsigned Opc) {
  const MIMGInfo *Info = getMIMGInfo(Opc);
  return Info ? getMIMGBaseOpcodeInfo(Info->BaseOpcode) : nullptr;
}

int getMaskedMIMGOp(unsigned Opc, unsigned NewChannels) {
  const MIMGInfo *OrigInfo = getMIMGInfo(Opc);
  const MIMGInfo *NewInfo =
      getMIMGOpcodeHelper(OrigInfo->BaseOpcode, OrigInfo->MIMGEncoding,
                          NewChannels, OrigInfo->VAddrDwords);
  return NewInfo ? NewInfo->Opcode : -1;
}

unsigned getAddrSizeMIMGOp(const MIMGBaseOpcodeInfo *BaseOpcode,
                           const MIMGDimInfo *Dim, bool IsA16,
                           bool IsG16Supported) {
  unsigned AddrWords = BaseOpcode->NumExtraArgs;
  unsigned AddrComponents = (BaseOpcode->Coordinates ? Dim->NumCoords : 0) +
                            (BaseOpcode->LodOrClampOrMip ? 1 : 0);
  if (IsA16)
    AddrWords += divideCeil(AddrComponents, 2);
  else
    AddrWords += AddrComponents;

  // Note: For subtargets that support A16 but not G16, enabling A16 also
  // enables 16 bit gradients.
  // For subtargets that support A16 (operand) and G16 (done with a different
  // instruction encoding), they are independent.

  if (BaseOpcode->Gradients) {
    if ((IsA16 && !IsG16Supported) || BaseOpcode->G16)
      // There are two gradients per coordinate, we pack them separately.
      // For the 3d case,
      // we get (dy/du, dx/du) (-, dz/du) (dy/dv, dx/dv) (-, dz/dv)
      AddrWords += alignTo<2>(Dim->NumGradients / 2);
    else
      AddrWords += Dim->NumGradients;
  }
  return AddrWords;
}

struct MUBUFInfo {
  uint16_t Opcode;
  uint16_t BaseOpcode;
  uint8_t elements;
  bool has_vaddr;
  bool has_srsrc;
  bool has_soffset;
  bool IsBufferInv;
  bool tfe;
};

struct MTBUFInfo {
  uint16_t Opcode;
  uint16_t BaseOpcode;
  uint8_t elements;
  bool has_vaddr;
  bool has_srsrc;
  bool has_soffset;
};

struct SMInfo {
  uint16_t Opcode;
  bool IsBuffer;
};

struct VOPInfo {
  uint16_t Opcode;
  bool IsSingle;
};

struct VOPC64DPPInfo {
  uint16_t Opcode;
};

struct VOPCDPPAsmOnlyInfo {
  uint16_t Opcode;
};

struct VOP3CDPPAsmOnlyInfo {
  uint16_t Opcode;
};

struct VOPDComponentInfo {
  uint16_t BaseVOP;
  uint16_t VOPDOp;
  bool CanBeVOPDX;
};

struct VOPDInfo {
  uint16_t Opcode;
  uint16_t OpX;
  uint16_t OpY;
  uint16_t Subtarget;
};

struct VOPTrue16Info {
  uint16_t Opcode;
  bool IsTrue16;
};

struct SingleUseExceptionInfo {
  uint16_t Opcode;
  bool IsInvalidSingleUseConsumer;
  bool IsInvalidSingleUseProducer;
};

#define GET_MTBUFInfoTable_DECL
#define GET_MTBUFInfoTable_IMPL
#define GET_MUBUFInfoTable_DECL
#define GET_MUBUFInfoTable_IMPL
#define GET_SingleUseExceptionTable_DECL
#define GET_SingleUseExceptionTable_IMPL
#define GET_SMInfoTable_DECL
#define GET_SMInfoTable_IMPL
#define GET_VOP1InfoTable_DECL
#define GET_VOP1InfoTable_IMPL
#define GET_VOP2InfoTable_DECL
#define GET_VOP2InfoTable_IMPL
#define GET_VOP3InfoTable_DECL
#define GET_VOP3InfoTable_IMPL
#define GET_VOPC64DPPTable_DECL
#define GET_VOPC64DPPTable_IMPL
#define GET_VOPC64DPP8Table_DECL
#define GET_VOPC64DPP8Table_IMPL
#define GET_VOPCAsmOnlyInfoTable_DECL
#define GET_VOPCAsmOnlyInfoTable_IMPL
#define GET_VOP3CAsmOnlyInfoTable_DECL
#define GET_VOP3CAsmOnlyInfoTable_IMPL
#define GET_VOPDComponentTable_DECL
#define GET_VOPDComponentTable_IMPL
#define GET_VOPDPairs_DECL
#define GET_VOPDPairs_IMPL
#define GET_VOPTrue16Table_DECL
#define GET_VOPTrue16Table_IMPL
#define GET_WMMAOpcode2AddrMappingTable_DECL
#define GET_WMMAOpcode2AddrMappingTable_IMPL
#define GET_WMMAOpcode3AddrMappingTable_DECL
#define GET_WMMAOpcode3AddrMappingTable_IMPL
#include "AMDGPUGenSearchableTables.inc"

int getMTBUFBaseOpcode(unsigned Opc) {
  const MTBUFInfo *Info = getMTBUFInfoFromOpcode(Opc);
  return Info ? Info->BaseOpcode : -1;
}

int getMTBUFOpcode(unsigned BaseOpc, unsigned Elements) {
  const MTBUFInfo *Info = getMTBUFInfoFromBaseOpcodeAndElements(BaseOpc, Elements);
  return Info ? Info->Opcode : -1;
}

int getMTBUFElements(unsigned Opc) {
  const MTBUFInfo *Info = getMTBUFOpcodeHelper(Opc);
  return Info ? Info->elements : 0;
}

bool getMTBUFHasVAddr(unsigned Opc) {
  const MTBUFInfo *Info = getMTBUFOpcodeHelper(Opc);
  return Info ? Info->has_vaddr : false;
}

bool getMTBUFHasSrsrc(unsigned Opc) {
  const MTBUFInfo *Info = getMTBUFOpcodeHelper(Opc);
  return Info ? Info->has_srsrc : false;
}

bool getMTBUFHasSoffset(unsigned Opc) {
  const MTBUFInfo *Info = getMTBUFOpcodeHelper(Opc);
  return Info ? Info->has_soffset : false;
}

int getMUBUFBaseOpcode(unsigned Opc) {
  const MUBUFInfo *Info = getMUBUFInfoFromOpcode(Opc);
  return Info ? Info->BaseOpcode : -1;
}

int getMUBUFOpcode(unsigned BaseOpc, unsigned Elements) {
  const MUBUFInfo *Info = getMUBUFInfoFromBaseOpcodeAndElements(BaseOpc, Elements);
  return Info ? Info->Opcode : -1;
}

int getMUBUFElements(unsigned Opc) {
  const MUBUFInfo *Info = getMUBUFOpcodeHelper(Opc);
  return Info ? Info->elements : 0;
}

bool getMUBUFHasVAddr(unsigned Opc) {
  const MUBUFInfo *Info = getMUBUFOpcodeHelper(Opc);
  return Info ? Info->has_vaddr : false;
}

bool getMUBUFHasSrsrc(unsigned Opc) {
  const MUBUFInfo *Info = getMUBUFOpcodeHelper(Opc);
  return Info ? Info->has_srsrc : false;
}

bool getMUBUFHasSoffset(unsigned Opc) {
  const MUBUFInfo *Info = getMUBUFOpcodeHelper(Opc);
  return Info ? Info->has_soffset : false;
}

bool getMUBUFIsBufferInv(unsigned Opc) {
  const MUBUFInfo *Info = getMUBUFOpcodeHelper(Opc);
  return Info ? Info->IsBufferInv : false;
}

bool getMUBUFTfe(unsigned Opc) {
  const MUBUFInfo *Info = getMUBUFOpcodeHelper(Opc);
  return Info ? Info->tfe : false;
}

bool getSMEMIsBuffer(unsigned Opc) {
  const SMInfo *Info = getSMEMOpcodeHelper(Opc);
  return Info ? Info->IsBuffer : false;
}

bool getVOP1IsSingle(unsigned Opc) {
  const VOPInfo *Info = getVOP1OpcodeHelper(Opc);
  return Info ? Info->IsSingle : true;
}

bool getVOP2IsSingle(unsigned Opc) {
  const VOPInfo *Info = getVOP2OpcodeHelper(Opc);
  return Info ? Info->IsSingle : true;
}

bool getVOP3IsSingle(unsigned Opc) {
  const VOPInfo *Info = getVOP3OpcodeHelper(Opc);
  return Info ? Info->IsSingle : true;
}

bool isVOPC64DPP(unsigned Opc) {
  return isVOPC64DPPOpcodeHelper(Opc) || isVOPC64DPP8OpcodeHelper(Opc);
}

bool isVOPCAsmOnly(unsigned Opc) { return isVOPCAsmOnlyOpcodeHelper(Opc); }

bool getMAIIsDGEMM(unsigned Opc) {
  const MAIInstInfo *Info = getMAIInstInfoHelper(Opc);
  return Info ? Info->is_dgemm : false;
}

bool getMAIIsGFX940XDL(unsigned Opc) {
  const MAIInstInfo *Info = getMAIInstInfoHelper(Opc);
  return Info ? Info->is_gfx940_xdl : false;
}

unsigned getVOPDEncodingFamily(const MCSubtargetInfo &ST) {
  if (ST.hasFeature(AMDGPU::FeatureGFX12Insts))
    return SIEncodingFamily::GFX12;
  if (ST.hasFeature(AMDGPU::FeatureGFX11Insts))
    return SIEncodingFamily::GFX11;
  llvm_unreachable("Subtarget generation does not support VOPD!");
}

CanBeVOPD getCanBeVOPD(unsigned Opc) {
  const VOPDComponentInfo *Info = getVOPDComponentHelper(Opc);
  if (Info)
    return {Info->CanBeVOPDX, true};
  return {false, false};
}

unsigned getVOPDOpcode(unsigned Opc) {
  const VOPDComponentInfo *Info = getVOPDComponentHelper(Opc);
  return Info ? Info->VOPDOp : ~0u;
}

bool isVOPD(unsigned Opc) {
  return AMDGPU::hasNamedOperand(Opc, AMDGPU::OpName::src0X);
}

bool isMAC(unsigned Opc) {
  return Opc == AMDGPU::V_MAC_F32_e64_gfx6_gfx7 ||
         Opc == AMDGPU::V_MAC_F32_e64_gfx10 ||
         Opc == AMDGPU::V_MAC_F32_e64_vi ||
         Opc == AMDGPU::V_MAC_LEGACY_F32_e64_gfx6_gfx7 ||
         Opc == AMDGPU::V_MAC_LEGACY_F32_e64_gfx10 ||
         Opc == AMDGPU::V_MAC_F16_e64_vi ||
         Opc == AMDGPU::V_FMAC_F64_e64_gfx90a ||
         Opc == AMDGPU::V_FMAC_F32_e64_gfx10 ||
         Opc == AMDGPU::V_FMAC_F32_e64_gfx11 ||
         Opc == AMDGPU::V_FMAC_F32_e64_gfx12 ||
         Opc == AMDGPU::V_FMAC_F32_e64_vi ||
         Opc == AMDGPU::V_FMAC_LEGACY_F32_e64_gfx10 ||
         Opc == AMDGPU::V_FMAC_DX9_ZERO_F32_e64_gfx11 ||
         Opc == AMDGPU::V_FMAC_F16_e64_gfx10 ||
         Opc == AMDGPU::V_FMAC_F16_t16_e64_gfx11 ||
         Opc == AMDGPU::V_FMAC_F16_t16_e64_gfx12 ||
         Opc == AMDGPU::V_DOT2C_F32_F16_e64_vi ||
         Opc == AMDGPU::V_DOT2C_I32_I16_e64_vi ||
         Opc == AMDGPU::V_DOT4C_I32_I8_e64_vi ||
         Opc == AMDGPU::V_DOT8C_I32_I4_e64_vi;
}

bool isPermlane16(unsigned Opc) {
  return Opc == AMDGPU::V_PERMLANE16_B32_gfx10 ||
         Opc == AMDGPU::V_PERMLANEX16_B32_gfx10 ||
         Opc == AMDGPU::V_PERMLANE16_B32_e64_gfx11 ||
         Opc == AMDGPU::V_PERMLANEX16_B32_e64_gfx11 ||
         Opc == AMDGPU::V_PERMLANE16_B32_e64_gfx12 ||
         Opc == AMDGPU::V_PERMLANEX16_B32_e64_gfx12 ||
         Opc == AMDGPU::V_PERMLANE16_VAR_B32_e64_gfx12 ||
         Opc == AMDGPU::V_PERMLANEX16_VAR_B32_e64_gfx12;
}

bool isCvt_F32_Fp8_Bf8_e64(unsigned Opc) {
  return Opc == AMDGPU::V_CVT_F32_BF8_e64_gfx12 ||
         Opc == AMDGPU::V_CVT_F32_FP8_e64_gfx12 ||
         Opc == AMDGPU::V_CVT_F32_BF8_e64_dpp_gfx12 ||
         Opc == AMDGPU::V_CVT_F32_FP8_e64_dpp_gfx12 ||
         Opc == AMDGPU::V_CVT_F32_BF8_e64_dpp8_gfx12 ||
         Opc == AMDGPU::V_CVT_F32_FP8_e64_dpp8_gfx12 ||
         Opc == AMDGPU::V_CVT_PK_F32_BF8_e64_gfx12 ||
         Opc == AMDGPU::V_CVT_PK_F32_FP8_e64_gfx12;
}

bool isGenericAtomic(unsigned Opc) {
  return Opc == AMDGPU::G_AMDGPU_BUFFER_ATOMIC_SWAP ||
         Opc == AMDGPU::G_AMDGPU_BUFFER_ATOMIC_ADD ||
         Opc == AMDGPU::G_AMDGPU_BUFFER_ATOMIC_SUB ||
         Opc == AMDGPU::G_AMDGPU_BUFFER_ATOMIC_SMIN ||
         Opc == AMDGPU::G_AMDGPU_BUFFER_ATOMIC_UMIN ||
         Opc == AMDGPU::G_AMDGPU_BUFFER_ATOMIC_SMAX ||
         Opc == AMDGPU::G_AMDGPU_BUFFER_ATOMIC_UMAX ||
         Opc == AMDGPU::G_AMDGPU_BUFFER_ATOMIC_AND ||
         Opc == AMDGPU::G_AMDGPU_BUFFER_ATOMIC_OR ||
         Opc == AMDGPU::G_AMDGPU_BUFFER_ATOMIC_XOR ||
         Opc == AMDGPU::G_AMDGPU_BUFFER_ATOMIC_INC ||
         Opc == AMDGPU::G_AMDGPU_BUFFER_ATOMIC_DEC ||
         Opc == AMDGPU::G_AMDGPU_BUFFER_ATOMIC_FADD ||
         Opc == AMDGPU::G_AMDGPU_BUFFER_ATOMIC_FMIN ||
         Opc == AMDGPU::G_AMDGPU_BUFFER_ATOMIC_FMAX ||
         Opc == AMDGPU::G_AMDGPU_BUFFER_ATOMIC_CMPSWAP ||
         Opc == AMDGPU::G_AMDGPU_ATOMIC_CMPXCHG;
}

bool isTrue16Inst(unsigned Opc) {
  const VOPTrue16Info *Info = getTrue16OpcodeHelper(Opc);
  return Info ? Info->IsTrue16 : false;
}

bool isInvalidSingleUseConsumerInst(unsigned Opc) {
  const SingleUseExceptionInfo *Info = getSingleUseExceptionHelper(Opc);
  return Info && Info->IsInvalidSingleUseConsumer;
}

bool isInvalidSingleUseProducerInst(unsigned Opc) {
  const SingleUseExceptionInfo *Info = getSingleUseExceptionHelper(Opc);
  return Info && Info->IsInvalidSingleUseProducer;
}

unsigned mapWMMA2AddrTo3AddrOpcode(unsigned Opc) {
  const WMMAOpcodeMappingInfo *Info = getWMMAMappingInfoFrom2AddrOpcode(Opc);
  return Info ? Info->Opcode3Addr : ~0u;
}

unsigned mapWMMA3AddrTo2AddrOpcode(unsigned Opc) {
  const WMMAOpcodeMappingInfo *Info = getWMMAMappingInfoFrom3AddrOpcode(Opc);
  return Info ? Info->Opcode2Addr : ~0u;
}

// Wrapper for Tablegen'd function.  enum Subtarget is not defined in any
// header files, so we need to wrap it in a function that takes unsigned
// instead.
int getMCOpcode(uint16_t Opcode, unsigned Gen) {
  return getMCOpcodeGen(Opcode, static_cast<Subtarget>(Gen));
}

int getVOPDFull(unsigned OpX, unsigned OpY, unsigned EncodingFamily) {
  const VOPDInfo *Info =
      getVOPDInfoFromComponentOpcodes(OpX, OpY, EncodingFamily);
  return Info ? Info->Opcode : -1;
}

std::pair<unsigned, unsigned> getVOPDComponents(unsigned VOPDOpcode) {
  const VOPDInfo *Info = getVOPDOpcodeHelper(VOPDOpcode);
  assert(Info);
  auto OpX = getVOPDBaseFromComponent(Info->OpX);
  auto OpY = getVOPDBaseFromComponent(Info->OpY);
  assert(OpX && OpY);
  return {OpX->BaseVOP, OpY->BaseVOP};
}

namespace VOPD {

ComponentProps::ComponentProps(const MCInstrDesc &OpDesc) {
  assert(OpDesc.getNumDefs() == Component::DST_NUM);

  assert(OpDesc.getOperandConstraint(Component::SRC0, MCOI::TIED_TO) == -1);
  assert(OpDesc.getOperandConstraint(Component::SRC1, MCOI::TIED_TO) == -1);
  auto TiedIdx = OpDesc.getOperandConstraint(Component::SRC2, MCOI::TIED_TO);
  assert(TiedIdx == -1 || TiedIdx == Component::DST);
  HasSrc2Acc = TiedIdx != -1;

  SrcOperandsNum = OpDesc.getNumOperands() - OpDesc.getNumDefs();
  assert(SrcOperandsNum <= Component::MAX_SRC_NUM);

  auto OperandsNum = OpDesc.getNumOperands();
  unsigned CompOprIdx;
  for (CompOprIdx = Component::SRC1; CompOprIdx < OperandsNum; ++CompOprIdx) {
    if (OpDesc.operands()[CompOprIdx].OperandType == AMDGPU::OPERAND_KIMM32) {
      MandatoryLiteralIdx = CompOprIdx;
      break;
    }
  }
}

unsigned ComponentInfo::getIndexInParsedOperands(unsigned CompOprIdx) const {
  assert(CompOprIdx < Component::MAX_OPR_NUM);

  if (CompOprIdx == Component::DST)
    return getIndexOfDstInParsedOperands();

  auto CompSrcIdx = CompOprIdx - Component::DST_NUM;
  if (CompSrcIdx < getCompParsedSrcOperandsNum())
    return getIndexOfSrcInParsedOperands(CompSrcIdx);

  // The specified operand does not exist.
  return 0;
}

std::optional<unsigned> InstInfo::getInvalidCompOperandIndex(
    std::function<unsigned(unsigned, unsigned)> GetRegIdx, bool SkipSrc) const {

  auto OpXRegs = getRegIndices(ComponentIndex::X, GetRegIdx);
  auto OpYRegs = getRegIndices(ComponentIndex::Y, GetRegIdx);

  const unsigned CompOprNum =
      SkipSrc ? Component::DST_NUM : Component::MAX_OPR_NUM;
  unsigned CompOprIdx;
  for (CompOprIdx = 0; CompOprIdx < CompOprNum; ++CompOprIdx) {
    unsigned BanksMasks = VOPD_VGPR_BANK_MASKS[CompOprIdx];
    if (OpXRegs[CompOprIdx] && OpYRegs[CompOprIdx] &&
        ((OpXRegs[CompOprIdx] & BanksMasks) ==
         (OpYRegs[CompOprIdx] & BanksMasks)))
      return CompOprIdx;
  }

  return {};
}

// Return an array of VGPR registers [DST,SRC0,SRC1,SRC2] used
// by the specified component. If an operand is unused
// or is not a VGPR, the corresponding value is 0.
//
// GetRegIdx(Component, MCOperandIdx) must return a VGPR register index
// for the specified component and MC operand. The callback must return 0
// if the operand is not a register or not a VGPR.
InstInfo::RegIndices InstInfo::getRegIndices(
    unsigned CompIdx,
    std::function<unsigned(unsigned, unsigned)> GetRegIdx) const {
  assert(CompIdx < COMPONENTS_NUM);

  const auto &Comp = CompInfo[CompIdx];
  InstInfo::RegIndices RegIndices;

  RegIndices[DST] = GetRegIdx(CompIdx, Comp.getIndexOfDstInMCOperands());

  for (unsigned CompOprIdx : {SRC0, SRC1, SRC2}) {
    unsigned CompSrcIdx = CompOprIdx - DST_NUM;
    RegIndices[CompOprIdx] =
        Comp.hasRegSrcOperand(CompSrcIdx)
            ? GetRegIdx(CompIdx, Comp.getIndexOfSrcInMCOperands(CompSrcIdx))
            : 0;
  }
  return RegIndices;
}

} // namespace VOPD

VOPD::InstInfo getVOPDInstInfo(const MCInstrDesc &OpX, const MCInstrDesc &OpY) {
  return VOPD::InstInfo(OpX, OpY);
}

VOPD::InstInfo getVOPDInstInfo(unsigned VOPDOpcode,
                               const MCInstrInfo *InstrInfo) {
  auto [OpX, OpY] = getVOPDComponents(VOPDOpcode);
  const auto &OpXDesc = InstrInfo->get(OpX);
  const auto &OpYDesc = InstrInfo->get(OpY);
  VOPD::ComponentInfo OpXInfo(OpXDesc, VOPD::ComponentKind::COMPONENT_X);
  VOPD::ComponentInfo OpYInfo(OpYDesc, OpXInfo);
  return VOPD::InstInfo(OpXInfo, OpYInfo);
}

namespace IsaInfo {

AMDGPUTargetID::AMDGPUTargetID(const MCSubtargetInfo &STI)
    : STI(STI), XnackSetting(TargetIDSetting::Any),
      SramEccSetting(TargetIDSetting::Any) {
  if (!STI.getFeatureBits().test(FeatureSupportsXNACK))
    XnackSetting = TargetIDSetting::Unsupported;
  if (!STI.getFeatureBits().test(FeatureSupportsSRAMECC))
    SramEccSetting = TargetIDSetting::Unsupported;
}

void AMDGPUTargetID::setTargetIDFromFeaturesString(StringRef FS) {
  // Check if xnack or sramecc is explicitly enabled or disabled.  In the
  // absence of the target features we assume we must generate code that can run
  // in any environment.
  SubtargetFeatures Features(FS);
  std::optional<bool> XnackRequested;
  std::optional<bool> SramEccRequested;

  for (const std::string &Feature : Features.getFeatures()) {
    if (Feature == "+xnack")
      XnackRequested = true;
    else if (Feature == "-xnack")
      XnackRequested = false;
    else if (Feature == "+sramecc")
      SramEccRequested = true;
    else if (Feature == "-sramecc")
      SramEccRequested = false;
  }

  bool XnackSupported = isXnackSupported();
  bool SramEccSupported = isSramEccSupported();

  if (XnackRequested) {
    if (XnackSupported) {
      XnackSetting =
          *XnackRequested ? TargetIDSetting::On : TargetIDSetting::Off;
    } else {
      // If a specific xnack setting was requested and this GPU does not support
      // xnack emit a warning. Setting will remain set to "Unsupported".
      if (*XnackRequested) {
        errs() << "warning: xnack 'On' was requested for a processor that does "
                  "not support it!\n";
      } else {
        errs() << "warning: xnack 'Off' was requested for a processor that "
                  "does not support it!\n";
      }
    }
  }

  if (SramEccRequested) {
    if (SramEccSupported) {
      SramEccSetting =
          *SramEccRequested ? TargetIDSetting::On : TargetIDSetting::Off;
    } else {
      // If a specific sramecc setting was requested and this GPU does not
      // support sramecc emit a warning. Setting will remain set to
      // "Unsupported".
      if (*SramEccRequested) {
        errs() << "warning: sramecc 'On' was requested for a processor that "
                  "does not support it!\n";
      } else {
        errs() << "warning: sramecc 'Off' was requested for a processor that "
                  "does not support it!\n";
      }
    }
  }
}

static TargetIDSetting
getTargetIDSettingFromFeatureString(StringRef FeatureString) {
  if (FeatureString.ends_with("-"))
    return TargetIDSetting::Off;
  if (FeatureString.ends_with("+"))
    return TargetIDSetting::On;

  llvm_unreachable("Malformed feature string");
}

void AMDGPUTargetID::setTargetIDFromTargetIDStream(StringRef TargetID) {
  SmallVector<StringRef, 3> TargetIDSplit;
  TargetID.split(TargetIDSplit, ':');

  for (const auto &FeatureString : TargetIDSplit) {
    if (FeatureString.starts_with("xnack"))
      XnackSetting = getTargetIDSettingFromFeatureString(FeatureString);
    if (FeatureString.starts_with("sramecc"))
      SramEccSetting = getTargetIDSettingFromFeatureString(FeatureString);
  }
}

std::string AMDGPUTargetID::toString() const {
  std::string StringRep;
  raw_string_ostream StreamRep(StringRep);

  auto TargetTriple = STI.getTargetTriple();
  auto Version = getIsaVersion(STI.getCPU());

  StreamRep << TargetTriple.getArchName() << '-'
            << TargetTriple.getVendorName() << '-'
            << TargetTriple.getOSName() << '-'
            << TargetTriple.getEnvironmentName() << '-';

  std::string Processor;
  // TODO: Following else statement is present here because we used various
  // alias names for GPUs up until GFX9 (e.g. 'fiji' is same as 'gfx803').
  // Remove once all aliases are removed from GCNProcessors.td.
  if (Version.Major >= 9)
    Processor = STI.getCPU().str();
  else
    Processor = (Twine("gfx") + Twine(Version.Major) + Twine(Version.Minor) +
                 Twine(Version.Stepping))
                    .str();

  std::string Features;
  if (STI.getTargetTriple().getOS() == Triple::AMDHSA) {
    // sramecc.
    if (getSramEccSetting() == TargetIDSetting::Off)
      Features += ":sramecc-";
    else if (getSramEccSetting() == TargetIDSetting::On)
      Features += ":sramecc+";
    // xnack.
    if (getXnackSetting() == TargetIDSetting::Off)
      Features += ":xnack-";
    else if (getXnackSetting() == TargetIDSetting::On)
      Features += ":xnack+";
  }

  StreamRep << Processor << Features;

  StreamRep.flush();
  return StringRep;
}

unsigned getWavefrontSize(const MCSubtargetInfo *STI) {
  if (STI->getFeatureBits().test(FeatureWavefrontSize16))
    return 16;
  if (STI->getFeatureBits().test(FeatureWavefrontSize32))
    return 32;

  return 64;
}

unsigned getLocalMemorySize(const MCSubtargetInfo *STI) {
  unsigned BytesPerCU = 0;
  if (STI->getFeatureBits().test(FeatureLocalMemorySize32768))
    BytesPerCU = 32768;
  if (STI->getFeatureBits().test(FeatureLocalMemorySize65536))
    BytesPerCU = 65536;

  // "Per CU" really means "per whatever functional block the waves of a
  // workgroup must share". So the effective local memory size is doubled in
  // WGP mode on gfx10.
  if (isGFX10Plus(*STI) && !STI->getFeatureBits().test(FeatureCuMode))
    BytesPerCU *= 2;

  return BytesPerCU;
}

unsigned getAddressableLocalMemorySize(const MCSubtargetInfo *STI) {
  if (STI->getFeatureBits().test(FeatureLocalMemorySize32768))
    return 32768;
  if (STI->getFeatureBits().test(FeatureLocalMemorySize65536))
    return 65536;
  return 0;
}

unsigned getEUsPerCU(const MCSubtargetInfo *STI) {
  // "Per CU" really means "per whatever functional block the waves of a
  // workgroup must share". For gfx10 in CU mode this is the CU, which contains
  // two SIMDs.
  if (isGFX10Plus(*STI) && STI->getFeatureBits().test(FeatureCuMode))
    return 2;
  // Pre-gfx10 a CU contains four SIMDs. For gfx10 in WGP mode the WGP contains
  // two CUs, so a total of four SIMDs.
  return 4;
}

unsigned getMaxWorkGroupsPerCU(const MCSubtargetInfo *STI,
                               unsigned FlatWorkGroupSize) {
  assert(FlatWorkGroupSize != 0);
  if (STI->getTargetTriple().getArch() != Triple::amdgcn)
    return 8;
  unsigned MaxWaves = getMaxWavesPerEU(STI) * getEUsPerCU(STI);
  unsigned N = getWavesPerWorkGroup(STI, FlatWorkGroupSize);
  if (N == 1) {
    // Single-wave workgroups don't consume barrier resources.
    return MaxWaves;
  }

  unsigned MaxBarriers = 16;
  if (isGFX10Plus(*STI) && !STI->getFeatureBits().test(FeatureCuMode))
    MaxBarriers = 32;

  return std::min(MaxWaves / N, MaxBarriers);
}

unsigned getMinWavesPerEU(const MCSubtargetInfo *STI) {
  return 1;
}

unsigned getMaxWavesPerEU(const MCSubtargetInfo *STI) {
  // FIXME: Need to take scratch memory into account.
  if (isGFX90A(*STI))
    return 8;
  if (!isGFX10Plus(*STI))
    return 10;
  return hasGFX10_3Insts(*STI) ? 16 : 20;
}

unsigned getWavesPerEUForWorkGroup(const MCSubtargetInfo *STI,
                                   unsigned FlatWorkGroupSize) {
  return divideCeil(getWavesPerWorkGroup(STI, FlatWorkGroupSize),
                    getEUsPerCU(STI));
}

unsigned getMinFlatWorkGroupSize(const MCSubtargetInfo *STI) {
  return 1;
}

unsigned getMaxFlatWorkGroupSize(const MCSubtargetInfo *STI) {
  // Some subtargets allow encoding 2048, but this isn't tested or supported.
  return 1024;
}

unsigned getWavesPerWorkGroup(const MCSubtargetInfo *STI,
                              unsigned FlatWorkGroupSize) {
  return divideCeil(FlatWorkGroupSize, getWavefrontSize(STI));
}

unsigned getSGPRAllocGranule(const MCSubtargetInfo *STI) {
  IsaVersion Version = getIsaVersion(STI->getCPU());
  if (Version.Major >= 10)
    return getAddressableNumSGPRs(STI);
  if (Version.Major >= 8)
    return 16;
  return 8;
}

unsigned getSGPREncodingGranule(const MCSubtargetInfo *STI) {
  return 8;
}

unsigned getTotalNumSGPRs(const MCSubtargetInfo *STI) {
  IsaVersion Version = getIsaVersion(STI->getCPU());
  if (Version.Major >= 8)
    return 800;
  return 512;
}

unsigned getAddressableNumSGPRs(const MCSubtargetInfo *STI) {
  if (STI->getFeatureBits().test(FeatureSGPRInitBug))
    return FIXED_NUM_SGPRS_FOR_INIT_BUG;

  IsaVersion Version = getIsaVersion(STI->getCPU());
  if (Version.Major >= 10)
    return 106;
  if (Version.Major >= 8)
    return 102;
  return 104;
}

unsigned getMinNumSGPRs(const MCSubtargetInfo *STI, unsigned WavesPerEU) {
  assert(WavesPerEU != 0);

  IsaVersion Version = getIsaVersion(STI->getCPU());
  if (Version.Major >= 10)
    return 0;

  if (WavesPerEU >= getMaxWavesPerEU(STI))
    return 0;

  unsigned MinNumSGPRs = getTotalNumSGPRs(STI) / (WavesPerEU + 1);
  if (STI->getFeatureBits().test(FeatureTrapHandler))
    MinNumSGPRs -= std::min(MinNumSGPRs, (unsigned)TRAP_NUM_SGPRS);
  MinNumSGPRs = alignDown(MinNumSGPRs, getSGPRAllocGranule(STI)) + 1;
  return std::min(MinNumSGPRs, getAddressableNumSGPRs(STI));
}

unsigned getMaxNumSGPRs(const MCSubtargetInfo *STI, unsigned WavesPerEU,
                        bool Addressable) {
  assert(WavesPerEU != 0);

  unsigned AddressableNumSGPRs = getAddressableNumSGPRs(STI);
  IsaVersion Version = getIsaVersion(STI->getCPU());
  if (Version.Major >= 10)
    return Addressable ? AddressableNumSGPRs : 108;
  if (Version.Major >= 8 && !Addressable)
    AddressableNumSGPRs = 112;
  unsigned MaxNumSGPRs = getTotalNumSGPRs(STI) / WavesPerEU;
  if (STI->getFeatureBits().test(FeatureTrapHandler))
    MaxNumSGPRs -= std::min(MaxNumSGPRs, (unsigned)TRAP_NUM_SGPRS);
  MaxNumSGPRs = alignDown(MaxNumSGPRs, getSGPRAllocGranule(STI));
  return std::min(MaxNumSGPRs, AddressableNumSGPRs);
}

unsigned getNumExtraSGPRs(const MCSubtargetInfo *STI, bool VCCUsed,
                          bool FlatScrUsed, bool XNACKUsed) {
  unsigned ExtraSGPRs = 0;
  if (VCCUsed)
    ExtraSGPRs = 2;

  IsaVersion Version = getIsaVersion(STI->getCPU());
  if (Version.Major >= 10)
    return ExtraSGPRs;

  if (Version.Major < 8) {
    if (FlatScrUsed)
      ExtraSGPRs = 4;
  } else {
    if (XNACKUsed)
      ExtraSGPRs = 4;

    if (FlatScrUsed ||
        STI->getFeatureBits().test(AMDGPU::FeatureArchitectedFlatScratch))
      ExtraSGPRs = 6;
  }

  return ExtraSGPRs;
}

unsigned getNumExtraSGPRs(const MCSubtargetInfo *STI, bool VCCUsed,
                          bool FlatScrUsed) {
  return getNumExtraSGPRs(STI, VCCUsed, FlatScrUsed,
                          STI->getFeatureBits().test(AMDGPU::FeatureXNACK));
}

static unsigned getGranulatedNumRegisterBlocks(unsigned NumRegs,
                                               unsigned Granule) {
  return divideCeil(std::max(1u, NumRegs), Granule);
}

unsigned getNumSGPRBlocks(const MCSubtargetInfo *STI, unsigned NumSGPRs) {
  // SGPRBlocks is actual number of SGPR blocks minus 1.
  return getGranulatedNumRegisterBlocks(NumSGPRs, getSGPREncodingGranule(STI)) -
         1;
}

unsigned getVGPRAllocGranule(const MCSubtargetInfo *STI,
                             std::optional<bool> EnableWavefrontSize32) {
  if (STI->getFeatureBits().test(FeatureGFX90AInsts))
    return 8;

  bool IsWave32 = EnableWavefrontSize32 ?
      *EnableWavefrontSize32 :
      STI->getFeatureBits().test(FeatureWavefrontSize32);

  if (STI->getFeatureBits().test(Feature1_5xVGPRs))
    return IsWave32 ? 24 : 12;

  if (hasGFX10_3Insts(*STI))
    return IsWave32 ? 16 : 8;

  return IsWave32 ? 8 : 4;
}

unsigned getVGPREncodingGranule(const MCSubtargetInfo *STI,
                                std::optional<bool> EnableWavefrontSize32) {
  if (STI->getFeatureBits().test(FeatureGFX90AInsts))
    return 8;

  bool IsWave32 = EnableWavefrontSize32 ?
      *EnableWavefrontSize32 :
      STI->getFeatureBits().test(FeatureWavefrontSize32);

  return IsWave32 ? 8 : 4;
}

unsigned getTotalNumVGPRs(const MCSubtargetInfo *STI) {
  if (STI->getFeatureBits().test(FeatureGFX90AInsts))
    return 512;
  if (!isGFX10Plus(*STI))
    return 256;
  bool IsWave32 = STI->getFeatureBits().test(FeatureWavefrontSize32);
  if (STI->getFeatureBits().test(Feature1_5xVGPRs))
    return IsWave32 ? 1536 : 768;
  return IsWave32 ? 1024 : 512;
}

unsigned getAddressableNumArchVGPRs(const MCSubtargetInfo *STI) { return 256; }

unsigned getAddressableNumVGPRs(const MCSubtargetInfo *STI) {
  if (STI->getFeatureBits().test(FeatureGFX90AInsts))
    return 512;
  return getAddressableNumArchVGPRs(STI);
}

unsigned getNumWavesPerEUWithNumVGPRs(const MCSubtargetInfo *STI,
                                      unsigned NumVGPRs) {
  return getNumWavesPerEUWithNumVGPRs(NumVGPRs, getVGPRAllocGranule(STI),
                                      getMaxWavesPerEU(STI),
                                      getTotalNumVGPRs(STI));
}

unsigned getNumWavesPerEUWithNumVGPRs(unsigned NumVGPRs, unsigned Granule,
                                      unsigned MaxWaves,
                                      unsigned TotalNumVGPRs) {
  if (NumVGPRs < Granule)
    return MaxWaves;
  unsigned RoundedRegs = alignTo(NumVGPRs, Granule);
  return std::min(std::max(TotalNumVGPRs / RoundedRegs, 1u), MaxWaves);
}

unsigned getOccupancyWithNumSGPRs(unsigned SGPRs, unsigned MaxWaves,
                                  AMDGPUSubtarget::Generation Gen) {
  if (Gen >= AMDGPUSubtarget::GFX10)
    return MaxWaves;

  if (Gen >= AMDGPUSubtarget::VOLCANIC_ISLANDS) {
    if (SGPRs <= 80)
      return 10;
    if (SGPRs <= 88)
      return 9;
    if (SGPRs <= 100)
      return 8;
    return 7;
  }
  if (SGPRs <= 48)
    return 10;
  if (SGPRs <= 56)
    return 9;
  if (SGPRs <= 64)
    return 8;
  if (SGPRs <= 72)
    return 7;
  if (SGPRs <= 80)
    return 6;
  return 5;
}

unsigned getMinNumVGPRs(const MCSubtargetInfo *STI, unsigned WavesPerEU) {
  assert(WavesPerEU != 0);

  unsigned MaxWavesPerEU = getMaxWavesPerEU(STI);
  if (WavesPerEU >= MaxWavesPerEU)
    return 0;

  unsigned TotNumVGPRs = getTotalNumVGPRs(STI);
  unsigned AddrsableNumVGPRs = getAddressableNumVGPRs(STI);
  unsigned Granule = getVGPRAllocGranule(STI);
  unsigned MaxNumVGPRs = alignDown(TotNumVGPRs / WavesPerEU, Granule);

  if (MaxNumVGPRs == alignDown(TotNumVGPRs / MaxWavesPerEU, Granule))
    return 0;

  unsigned MinWavesPerEU = getNumWavesPerEUWithNumVGPRs(STI, AddrsableNumVGPRs);
  if (WavesPerEU < MinWavesPerEU)
    return getMinNumVGPRs(STI, MinWavesPerEU);

  unsigned MaxNumVGPRsNext = alignDown(TotNumVGPRs / (WavesPerEU + 1), Granule);
  unsigned MinNumVGPRs = 1 + std::min(MaxNumVGPRs - Granule, MaxNumVGPRsNext);
  return std::min(MinNumVGPRs, AddrsableNumVGPRs);
}

unsigned getMaxNumVGPRs(const MCSubtargetInfo *STI, unsigned WavesPerEU) {
  assert(WavesPerEU != 0);

  unsigned MaxNumVGPRs = alignDown(getTotalNumVGPRs(STI) / WavesPerEU,
                                   getVGPRAllocGranule(STI));
  unsigned AddressableNumVGPRs = getAddressableNumVGPRs(STI);
  return std::min(MaxNumVGPRs, AddressableNumVGPRs);
}

unsigned getEncodedNumVGPRBlocks(const MCSubtargetInfo *STI, unsigned NumVGPRs,
                                 std::optional<bool> EnableWavefrontSize32) {
  return getGranulatedNumRegisterBlocks(
             NumVGPRs, getVGPREncodingGranule(STI, EnableWavefrontSize32)) -
         1;
}

unsigned getAllocatedNumVGPRBlocks(const MCSubtargetInfo *STI,
                                   unsigned NumVGPRs,
                                   std::optional<bool> EnableWavefrontSize32) {
  return getGranulatedNumRegisterBlocks(
      NumVGPRs, getVGPRAllocGranule(STI, EnableWavefrontSize32));
}
} // end namespace IsaInfo

void initDefaultAMDKernelCodeT(AMDGPUMCKernelCodeT &KernelCode,
                               const MCSubtargetInfo *STI) {
  IsaVersion Version = getIsaVersion(STI->getCPU());
  KernelCode.amd_kernel_code_version_major = 1;
  KernelCode.amd_kernel_code_version_minor = 2;
  KernelCode.amd_machine_kind = 1; // AMD_MACHINE_KIND_AMDGPU
  KernelCode.amd_machine_version_major = Version.Major;
  KernelCode.amd_machine_version_minor = Version.Minor;
  KernelCode.amd_machine_version_stepping = Version.Stepping;
  KernelCode.kernel_code_entry_byte_offset = sizeof(amd_kernel_code_t);
  if (STI->getFeatureBits().test(FeatureWavefrontSize32)) {
    KernelCode.wavefront_size = 5;
    KernelCode.code_properties |= AMD_CODE_PROPERTY_ENABLE_WAVEFRONT_SIZE32;
  } else {
    KernelCode.wavefront_size = 6;
  }

  // If the code object does not support indirect functions, then the value must
  // be 0xffffffff.
  KernelCode.call_convention = -1;

  // These alignment values are specified in powers of two, so alignment =
  // 2^n.  The minimum alignment is 2^4 = 16.
  KernelCode.kernarg_segment_alignment = 4;
  KernelCode.group_segment_alignment = 4;
  KernelCode.private_segment_alignment = 4;

  if (Version.Major >= 10) {
    KernelCode.compute_pgm_resource_registers |=
        S_00B848_WGP_MODE(STI->getFeatureBits().test(FeatureCuMode) ? 0 : 1) |
        S_00B848_MEM_ORDERED(1);
  }
}

bool isGroupSegment(const GlobalValue *GV) {
  return GV->getAddressSpace() == AMDGPUAS::LOCAL_ADDRESS;
}

bool isGlobalSegment(const GlobalValue *GV) {
  return GV->getAddressSpace() == AMDGPUAS::GLOBAL_ADDRESS;
}

bool isReadOnlySegment(const GlobalValue *GV) {
  unsigned AS = GV->getAddressSpace();
  return AS == AMDGPUAS::CONSTANT_ADDRESS ||
         AS == AMDGPUAS::CONSTANT_ADDRESS_32BIT;
}

bool shouldEmitConstantsToTextSection(const Triple &TT) {
  return TT.getArch() == Triple::r600;
}

std::pair<unsigned, unsigned>
getIntegerPairAttribute(const Function &F, StringRef Name,
                        std::pair<unsigned, unsigned> Default,
                        bool OnlyFirstRequired) {
  Attribute A = F.getFnAttribute(Name);
  if (!A.isStringAttribute())
    return Default;

  LLVMContext &Ctx = F.getContext();
  std::pair<unsigned, unsigned> Ints = Default;
  std::pair<StringRef, StringRef> Strs = A.getValueAsString().split(',');
  if (Strs.first.trim().getAsInteger(0, Ints.first)) {
    Ctx.emitError("can't parse first integer attribute " + Name);
    return Default;
  }
  if (Strs.second.trim().getAsInteger(0, Ints.second)) {
    if (!OnlyFirstRequired || !Strs.second.trim().empty()) {
      Ctx.emitError("can't parse second integer attribute " + Name);
      return Default;
    }
  }

  return Ints;
}

SmallVector<unsigned> getIntegerVecAttribute(const Function &F, StringRef Name,
                                             unsigned Size) {
  assert(Size > 2);
  SmallVector<unsigned> Default(Size, 0);

  Attribute A = F.getFnAttribute(Name);
  if (!A.isStringAttribute())
    return Default;

  SmallVector<unsigned> Vals(Size, 0);

  LLVMContext &Ctx = F.getContext();

  StringRef S = A.getValueAsString();
  unsigned i = 0;
  for (; !S.empty() && i < Size; i++) {
    std::pair<StringRef, StringRef> Strs = S.split(',');
    unsigned IntVal;
    if (Strs.first.trim().getAsInteger(0, IntVal)) {
      Ctx.emitError("can't parse integer attribute " + Strs.first + " in " +
                    Name);
      return Default;
    }
    Vals[i] = IntVal;
    S = Strs.second;
  }

  if (!S.empty() || i < Size) {
    Ctx.emitError("attribute " + Name +
                  " has incorrect number of integers; expected " +
                  llvm::utostr(Size));
    return Default;
  }
  return Vals;
}

unsigned getVmcntBitMask(const IsaVersion &Version) {
  return (1 << (getVmcntBitWidthLo(Version.Major) +
                getVmcntBitWidthHi(Version.Major))) -
         1;
}

unsigned getLoadcntBitMask(const IsaVersion &Version) {
  return (1 << getLoadcntBitWidth(Version.Major)) - 1;
}

unsigned getSamplecntBitMask(const IsaVersion &Version) {
  return (1 << getSamplecntBitWidth(Version.Major)) - 1;
}

unsigned getBvhcntBitMask(const IsaVersion &Version) {
  return (1 << getBvhcntBitWidth(Version.Major)) - 1;
}

unsigned getExpcntBitMask(const IsaVersion &Version) {
  return (1 << getExpcntBitWidth(Version.Major)) - 1;
}

unsigned getLgkmcntBitMask(const IsaVersion &Version) {
  return (1 << getLgkmcntBitWidth(Version.Major)) - 1;
}

unsigned getDscntBitMask(const IsaVersion &Version) {
  return (1 << getDscntBitWidth(Version.Major)) - 1;
}

unsigned getKmcntBitMask(const IsaVersion &Version) {
  return (1 << getKmcntBitWidth(Version.Major)) - 1;
}

unsigned getStorecntBitMask(const IsaVersion &Version) {
  return (1 << getStorecntBitWidth(Version.Major)) - 1;
}

unsigned getWaitcntBitMask(const IsaVersion &Version) {
  unsigned VmcntLo = getBitMask(getVmcntBitShiftLo(Version.Major),
                                getVmcntBitWidthLo(Version.Major));
  unsigned Expcnt = getBitMask(getExpcntBitShift(Version.Major),
                               getExpcntBitWidth(Version.Major));
  unsigned Lgkmcnt = getBitMask(getLgkmcntBitShift(Version.Major),
                                getLgkmcntBitWidth(Version.Major));
  unsigned VmcntHi = getBitMask(getVmcntBitShiftHi(Version.Major),
                                getVmcntBitWidthHi(Version.Major));
  return VmcntLo | Expcnt | Lgkmcnt | VmcntHi;
}

unsigned decodeVmcnt(const IsaVersion &Version, unsigned Waitcnt) {
  unsigned VmcntLo = unpackBits(Waitcnt, getVmcntBitShiftLo(Version.Major),
                                getVmcntBitWidthLo(Version.Major));
  unsigned VmcntHi = unpackBits(Waitcnt, getVmcntBitShiftHi(Version.Major),
                                getVmcntBitWidthHi(Version.Major));
  return VmcntLo | VmcntHi << getVmcntBitWidthLo(Version.Major);
}

unsigned decodeExpcnt(const IsaVersion &Version, unsigned Waitcnt) {
  return unpackBits(Waitcnt, getExpcntBitShift(Version.Major),
                    getExpcntBitWidth(Version.Major));
}

unsigned decodeLgkmcnt(const IsaVersion &Version, unsigned Waitcnt) {
  return unpackBits(Waitcnt, getLgkmcntBitShift(Version.Major),
                    getLgkmcntBitWidth(Version.Major));
}

void decodeWaitcnt(const IsaVersion &Version, unsigned Waitcnt,
                   unsigned &Vmcnt, unsigned &Expcnt, unsigned &Lgkmcnt) {
  Vmcnt = decodeVmcnt(Version, Waitcnt);
  Expcnt = decodeExpcnt(Version, Waitcnt);
  Lgkmcnt = decodeLgkmcnt(Version, Waitcnt);
}

Waitcnt decodeWaitcnt(const IsaVersion &Version, unsigned Encoded) {
  Waitcnt Decoded;
  Decoded.LoadCnt = decodeVmcnt(Version, Encoded);
  Decoded.ExpCnt = decodeExpcnt(Version, Encoded);
  Decoded.DsCnt = decodeLgkmcnt(Version, Encoded);
  return Decoded;
}

unsigned encodeVmcnt(const IsaVersion &Version, unsigned Waitcnt,
                     unsigned Vmcnt) {
  Waitcnt = packBits(Vmcnt, Waitcnt, getVmcntBitShiftLo(Version.Major),
                     getVmcntBitWidthLo(Version.Major));
  return packBits(Vmcnt >> getVmcntBitWidthLo(Version.Major), Waitcnt,
                  getVmcntBitShiftHi(Version.Major),
                  getVmcntBitWidthHi(Version.Major));
}

unsigned encodeExpcnt(const IsaVersion &Version, unsigned Waitcnt,
                      unsigned Expcnt) {
  return packBits(Expcnt, Waitcnt, getExpcntBitShift(Version.Major),
                  getExpcntBitWidth(Version.Major));
}

unsigned encodeLgkmcnt(const IsaVersion &Version, unsigned Waitcnt,
                       unsigned Lgkmcnt) {
  return packBits(Lgkmcnt, Waitcnt, getLgkmcntBitShift(Version.Major),
                  getLgkmcntBitWidth(Version.Major));
}

unsigned encodeWaitcnt(const IsaVersion &Version,
                       unsigned Vmcnt, unsigned Expcnt, unsigned Lgkmcnt) {
  unsigned Waitcnt = getWaitcntBitMask(Version);
  Waitcnt = encodeVmcnt(Version, Waitcnt, Vmcnt);
  Waitcnt = encodeExpcnt(Version, Waitcnt, Expcnt);
  Waitcnt = encodeLgkmcnt(Version, Waitcnt, Lgkmcnt);
  return Waitcnt;
}

unsigned encodeWaitcnt(const IsaVersion &Version, const Waitcnt &Decoded) {
  return encodeWaitcnt(Version, Decoded.LoadCnt, Decoded.ExpCnt, Decoded.DsCnt);
}

static unsigned getCombinedCountBitMask(const IsaVersion &Version,
                                        bool IsStore) {
  unsigned Dscnt = getBitMask(getDscntBitShift(Version.Major),
                              getDscntBitWidth(Version.Major));
  if (IsStore) {
    unsigned Storecnt = getBitMask(getLoadcntStorecntBitShift(Version.Major),
                                   getStorecntBitWidth(Version.Major));
    return Dscnt | Storecnt;
  }
  unsigned Loadcnt = getBitMask(getLoadcntStorecntBitShift(Version.Major),
                                getLoadcntBitWidth(Version.Major));
  return Dscnt | Loadcnt;
}

Waitcnt decodeLoadcntDscnt(const IsaVersion &Version, unsigned LoadcntDscnt) {
  Waitcnt Decoded;
  Decoded.LoadCnt =
      unpackBits(LoadcntDscnt, getLoadcntStorecntBitShift(Version.Major),
                 getLoadcntBitWidth(Version.Major));
  Decoded.DsCnt = unpackBits(LoadcntDscnt, getDscntBitShift(Version.Major),
                             getDscntBitWidth(Version.Major));
  return Decoded;
}

Waitcnt decodeStorecntDscnt(const IsaVersion &Version, unsigned StorecntDscnt) {
  Waitcnt Decoded;
  Decoded.StoreCnt =
      unpackBits(StorecntDscnt, getLoadcntStorecntBitShift(Version.Major),
                 getStorecntBitWidth(Version.Major));
  Decoded.DsCnt = unpackBits(StorecntDscnt, getDscntBitShift(Version.Major),
                             getDscntBitWidth(Version.Major));
  return Decoded;
}

static unsigned encodeLoadcnt(const IsaVersion &Version, unsigned Waitcnt,
                              unsigned Loadcnt) {
  return packBits(Loadcnt, Waitcnt, getLoadcntStorecntBitShift(Version.Major),
                  getLoadcntBitWidth(Version.Major));
}

static unsigned encodeStorecnt(const IsaVersion &Version, unsigned Waitcnt,
                               unsigned Storecnt) {
  return packBits(Storecnt, Waitcnt, getLoadcntStorecntBitShift(Version.Major),
                  getStorecntBitWidth(Version.Major));
}

static unsigned encodeDscnt(const IsaVersion &Version, unsigned Waitcnt,
                            unsigned Dscnt) {
  return packBits(Dscnt, Waitcnt, getDscntBitShift(Version.Major),
                  getDscntBitWidth(Version.Major));
}

static unsigned encodeLoadcntDscnt(const IsaVersion &Version, unsigned Loadcnt,
                                   unsigned Dscnt) {
  unsigned Waitcnt = getCombinedCountBitMask(Version, false);
  Waitcnt = encodeLoadcnt(Version, Waitcnt, Loadcnt);
  Waitcnt = encodeDscnt(Version, Waitcnt, Dscnt);
  return Waitcnt;
}

unsigned encodeLoadcntDscnt(const IsaVersion &Version, const Waitcnt &Decoded) {
  return encodeLoadcntDscnt(Version, Decoded.LoadCnt, Decoded.DsCnt);
}

static unsigned encodeStorecntDscnt(const IsaVersion &Version,
                                    unsigned Storecnt, unsigned Dscnt) {
  unsigned Waitcnt = getCombinedCountBitMask(Version, true);
  Waitcnt = encodeStorecnt(Version, Waitcnt, Storecnt);
  Waitcnt = encodeDscnt(Version, Waitcnt, Dscnt);
  return Waitcnt;
}

unsigned encodeStorecntDscnt(const IsaVersion &Version,
                             const Waitcnt &Decoded) {
  return encodeStorecntDscnt(Version, Decoded.StoreCnt, Decoded.DsCnt);
}

//===----------------------------------------------------------------------===//
// Custom Operand Values
//===----------------------------------------------------------------------===//

static unsigned getDefaultCustomOperandEncoding(const CustomOperandVal *Opr,
                                                int Size,
                                                const MCSubtargetInfo &STI) {
  unsigned Enc = 0;
  for (int Idx = 0; Idx < Size; ++Idx) {
    const auto &Op = Opr[Idx];
    if (Op.isSupported(STI))
      Enc |= Op.encode(Op.Default);
  }
  return Enc;
}

static bool isSymbolicCustomOperandEncoding(const CustomOperandVal *Opr,
                                            int Size, unsigned Code,
                                            bool &HasNonDefaultVal,
                                            const MCSubtargetInfo &STI) {
  unsigned UsedOprMask = 0;
  HasNonDefaultVal = false;
  for (int Idx = 0; Idx < Size; ++Idx) {
    const auto &Op = Opr[Idx];
    if (!Op.isSupported(STI))
      continue;
    UsedOprMask |= Op.getMask();
    unsigned Val = Op.decode(Code);
    if (!Op.isValid(Val))
      return false;
    HasNonDefaultVal |= (Val != Op.Default);
  }
  return (Code & ~UsedOprMask) == 0;
}

static bool decodeCustomOperand(const CustomOperandVal *Opr, int Size,
                                unsigned Code, int &Idx, StringRef &Name,
                                unsigned &Val, bool &IsDefault,
                                const MCSubtargetInfo &STI) {
  while (Idx < Size) {
    const auto &Op = Opr[Idx++];
    if (Op.isSupported(STI)) {
      Name = Op.Name;
      Val = Op.decode(Code);
      IsDefault = (Val == Op.Default);
      return true;
    }
  }

  return false;
}

static int encodeCustomOperandVal(const CustomOperandVal &Op,
                                  int64_t InputVal) {
  if (InputVal < 0 || InputVal > Op.Max)
    return OPR_VAL_INVALID;
  return Op.encode(InputVal);
}

static int encodeCustomOperand(const CustomOperandVal *Opr, int Size,
                               const StringRef Name, int64_t InputVal,
                               unsigned &UsedOprMask,
                               const MCSubtargetInfo &STI) {
  int InvalidId = OPR_ID_UNKNOWN;
  for (int Idx = 0; Idx < Size; ++Idx) {
    const auto &Op = Opr[Idx];
    if (Op.Name == Name) {
      if (!Op.isSupported(STI)) {
        InvalidId = OPR_ID_UNSUPPORTED;
        continue;
      }
      auto OprMask = Op.getMask();
      if (OprMask & UsedOprMask)
        return OPR_ID_DUPLICATE;
      UsedOprMask |= OprMask;
      return encodeCustomOperandVal(Op, InputVal);
    }
  }
  return InvalidId;
}

//===----------------------------------------------------------------------===//
// DepCtr
//===----------------------------------------------------------------------===//

namespace DepCtr {

int getDefaultDepCtrEncoding(const MCSubtargetInfo &STI) {
  static int Default = -1;
  if (Default == -1)
    Default = getDefaultCustomOperandEncoding(DepCtrInfo, DEP_CTR_SIZE, STI);
  return Default;
}

bool isSymbolicDepCtrEncoding(unsigned Code, bool &HasNonDefaultVal,
                              const MCSubtargetInfo &STI) {
  return isSymbolicCustomOperandEncoding(DepCtrInfo, DEP_CTR_SIZE, Code,
                                         HasNonDefaultVal, STI);
}

bool decodeDepCtr(unsigned Code, int &Id, StringRef &Name, unsigned &Val,
                  bool &IsDefault, const MCSubtargetInfo &STI) {
  return decodeCustomOperand(DepCtrInfo, DEP_CTR_SIZE, Code, Id, Name, Val,
                             IsDefault, STI);
}

int encodeDepCtr(const StringRef Name, int64_t Val, unsigned &UsedOprMask,
                 const MCSubtargetInfo &STI) {
  return encodeCustomOperand(DepCtrInfo, DEP_CTR_SIZE, Name, Val, UsedOprMask,
                             STI);
}

unsigned decodeFieldVmVsrc(unsigned Encoded) {
  return unpackBits(Encoded, getVmVsrcBitShift(), getVmVsrcBitWidth());
}

unsigned decodeFieldVaVdst(unsigned Encoded) {
  return unpackBits(Encoded, getVaVdstBitShift(), getVaVdstBitWidth());
}

unsigned decodeFieldSaSdst(unsigned Encoded) {
  return unpackBits(Encoded, getSaSdstBitShift(), getSaSdstBitWidth());
}

unsigned encodeFieldVmVsrc(unsigned Encoded, unsigned VmVsrc) {
  return packBits(VmVsrc, Encoded, getVmVsrcBitShift(), getVmVsrcBitWidth());
}

unsigned encodeFieldVmVsrc(unsigned VmVsrc) {
  return encodeFieldVmVsrc(0xffff, VmVsrc);
}

unsigned encodeFieldVaVdst(unsigned Encoded, unsigned VaVdst) {
  return packBits(VaVdst, Encoded, getVaVdstBitShift(), getVaVdstBitWidth());
}

unsigned encodeFieldVaVdst(unsigned VaVdst) {
  return encodeFieldVaVdst(0xffff, VaVdst);
}

unsigned encodeFieldSaSdst(unsigned Encoded, unsigned SaSdst) {
  return packBits(SaSdst, Encoded, getSaSdstBitShift(), getSaSdstBitWidth());
}

unsigned encodeFieldSaSdst(unsigned SaSdst) {
  return encodeFieldSaSdst(0xffff, SaSdst);
}

} // namespace DepCtr

//===----------------------------------------------------------------------===//
// exp tgt
//===----------------------------------------------------------------------===//

namespace Exp {

struct ExpTgt {
  StringLiteral Name;
  unsigned Tgt;
  unsigned MaxIndex;
};

static constexpr ExpTgt ExpTgtInfo[] = {
  {{"null"},           ET_NULL,            ET_NULL_MAX_IDX},
  {{"mrtz"},           ET_MRTZ,            ET_MRTZ_MAX_IDX},
  {{"prim"},           ET_PRIM,            ET_PRIM_MAX_IDX},
  {{"mrt"},            ET_MRT0,            ET_MRT_MAX_IDX},
  {{"pos"},            ET_POS0,            ET_POS_MAX_IDX},
  {{"dual_src_blend"}, ET_DUAL_SRC_BLEND0, ET_DUAL_SRC_BLEND_MAX_IDX},
  {{"param"},          ET_PARAM0,          ET_PARAM_MAX_IDX},
};

bool getTgtName(unsigned Id, StringRef &Name, int &Index) {
  for (const ExpTgt &Val : ExpTgtInfo) {
    if (Val.Tgt <= Id && Id <= Val.Tgt + Val.MaxIndex) {
      Index = (Val.MaxIndex == 0) ? -1 : (Id - Val.Tgt);
      Name = Val.Name;
      return true;
    }
  }
  return false;
}

unsigned getTgtId(const StringRef Name) {

  for (const ExpTgt &Val : ExpTgtInfo) {
    if (Val.MaxIndex == 0 && Name == Val.Name)
      return Val.Tgt;

    if (Val.MaxIndex > 0 && Name.starts_with(Val.Name)) {
      StringRef Suffix = Name.drop_front(Val.Name.size());

      unsigned Id;
      if (Suffix.getAsInteger(10, Id) || Id > Val.MaxIndex)
        return ET_INVALID;

      // Disable leading zeroes
      if (Suffix.size() > 1 && Suffix[0] == '0')
        return ET_INVALID;

      return Val.Tgt + Id;
    }
  }
  return ET_INVALID;
}

bool isSupportedTgtId(unsigned Id, const MCSubtargetInfo &STI) {
  switch (Id) {
  case ET_NULL:
    return !isGFX11Plus(STI);
  case ET_POS4:
  case ET_PRIM:
    return isGFX10Plus(STI);
  case ET_DUAL_SRC_BLEND0:
  case ET_DUAL_SRC_BLEND1:
    return isGFX11Plus(STI);
  default:
    if (Id >= ET_PARAM0 && Id <= ET_PARAM31)
      return !isGFX11Plus(STI);
    return true;
  }
}

} // namespace Exp

//===----------------------------------------------------------------------===//
// MTBUF Format
//===----------------------------------------------------------------------===//

namespace MTBUFFormat {

int64_t getDfmt(const StringRef Name) {
  for (int Id = DFMT_MIN; Id <= DFMT_MAX; ++Id) {
    if (Name == DfmtSymbolic[Id])
      return Id;
  }
  return DFMT_UNDEF;
}

StringRef getDfmtName(unsigned Id) {
  assert(Id <= DFMT_MAX);
  return DfmtSymbolic[Id];
}

static StringLiteral const *getNfmtLookupTable(const MCSubtargetInfo &STI) {
  if (isSI(STI) || isCI(STI))
    return NfmtSymbolicSICI;
  if (isVI(STI) || isGFX9(STI))
    return NfmtSymbolicVI;
  return NfmtSymbolicGFX10;
}

int64_t getNfmt(const StringRef Name, const MCSubtargetInfo &STI) {
  auto lookupTable = getNfmtLookupTable(STI);
  for (int Id = NFMT_MIN; Id <= NFMT_MAX; ++Id) {
    if (Name == lookupTable[Id])
      return Id;
  }
  return NFMT_UNDEF;
}

StringRef getNfmtName(unsigned Id, const MCSubtargetInfo &STI) {
  assert(Id <= NFMT_MAX);
  return getNfmtLookupTable(STI)[Id];
}

bool isValidDfmtNfmt(unsigned Id, const MCSubtargetInfo &STI) {
  unsigned Dfmt;
  unsigned Nfmt;
  decodeDfmtNfmt(Id, Dfmt, Nfmt);
  return isValidNfmt(Nfmt, STI);
}

bool isValidNfmt(unsigned Id, const MCSubtargetInfo &STI) {
  return !getNfmtName(Id, STI).empty();
}

int64_t encodeDfmtNfmt(unsigned Dfmt, unsigned Nfmt) {
  return (Dfmt << DFMT_SHIFT) | (Nfmt << NFMT_SHIFT);
}

void decodeDfmtNfmt(unsigned Format, unsigned &Dfmt, unsigned &Nfmt) {
  Dfmt = (Format >> DFMT_SHIFT) & DFMT_MASK;
  Nfmt = (Format >> NFMT_SHIFT) & NFMT_MASK;
}

int64_t getUnifiedFormat(const StringRef Name, const MCSubtargetInfo &STI) {
  if (isGFX11Plus(STI)) {
    for (int Id = UfmtGFX11::UFMT_FIRST; Id <= UfmtGFX11::UFMT_LAST; ++Id) {
      if (Name == UfmtSymbolicGFX11[Id])
        return Id;
    }
  } else {
    for (int Id = UfmtGFX10::UFMT_FIRST; Id <= UfmtGFX10::UFMT_LAST; ++Id) {
      if (Name == UfmtSymbolicGFX10[Id])
        return Id;
    }
  }
  return UFMT_UNDEF;
}

StringRef getUnifiedFormatName(unsigned Id, const MCSubtargetInfo &STI) {
  if(isValidUnifiedFormat(Id, STI))
    return isGFX10(STI) ? UfmtSymbolicGFX10[Id] : UfmtSymbolicGFX11[Id];
  return "";
}

bool isValidUnifiedFormat(unsigned Id, const MCSubtargetInfo &STI) {
  return isGFX10(STI) ? Id <= UfmtGFX10::UFMT_LAST : Id <= UfmtGFX11::UFMT_LAST;
}

int64_t convertDfmtNfmt2Ufmt(unsigned Dfmt, unsigned Nfmt,
                             const MCSubtargetInfo &STI) {
  int64_t Fmt = encodeDfmtNfmt(Dfmt, Nfmt);
  if (isGFX11Plus(STI)) {
    for (int Id = UfmtGFX11::UFMT_FIRST; Id <= UfmtGFX11::UFMT_LAST; ++Id) {
      if (Fmt == DfmtNfmt2UFmtGFX11[Id])
        return Id;
    }
  } else {
    for (int Id = UfmtGFX10::UFMT_FIRST; Id <= UfmtGFX10::UFMT_LAST; ++Id) {
      if (Fmt == DfmtNfmt2UFmtGFX10[Id])
        return Id;
    }
  }
  return UFMT_UNDEF;
}

bool isValidFormatEncoding(unsigned Val, const MCSubtargetInfo &STI) {
  return isGFX10Plus(STI) ? (Val <= UFMT_MAX) : (Val <= DFMT_NFMT_MAX);
}

unsigned getDefaultFormatEncoding(const MCSubtargetInfo &STI) {
  if (isGFX10Plus(STI))
    return UFMT_DEFAULT;
  return DFMT_NFMT_DEFAULT;
}

} // namespace MTBUFFormat

//===----------------------------------------------------------------------===//
// SendMsg
//===----------------------------------------------------------------------===//

namespace SendMsg {

static uint64_t getMsgIdMask(const MCSubtargetInfo &STI) {
  return isGFX11Plus(STI) ? ID_MASK_GFX11Plus_ : ID_MASK_PreGFX11_;
}

bool isValidMsgId(int64_t MsgId, const MCSubtargetInfo &STI) {
  return (MsgId & ~(getMsgIdMask(STI))) == 0;
}

bool isValidMsgOp(int64_t MsgId, int64_t OpId, const MCSubtargetInfo &STI,
                  bool Strict) {
  assert(isValidMsgId(MsgId, STI));

  if (!Strict)
    return 0 <= OpId && isUInt<OP_WIDTH_>(OpId);

  if (msgRequiresOp(MsgId, STI)) {
    if (MsgId == ID_GS_PreGFX11 && OpId == OP_GS_NOP)
      return false;

    return !getMsgOpName(MsgId, OpId, STI).empty();
  }

  return OpId == OP_NONE_;
}

bool isValidMsgStream(int64_t MsgId, int64_t OpId, int64_t StreamId,
                      const MCSubtargetInfo &STI, bool Strict) {
  assert(isValidMsgOp(MsgId, OpId, STI, Strict));

  if (!Strict)
    return 0 <= StreamId && isUInt<STREAM_ID_WIDTH_>(StreamId);

  if (!isGFX11Plus(STI)) {
    switch (MsgId) {
    case ID_GS_PreGFX11:
      return STREAM_ID_FIRST_ <= StreamId && StreamId < STREAM_ID_LAST_;
    case ID_GS_DONE_PreGFX11:
      return (OpId == OP_GS_NOP) ?
          (StreamId == STREAM_ID_NONE_) :
          (STREAM_ID_FIRST_ <= StreamId && StreamId < STREAM_ID_LAST_);
    }
  }
  return StreamId == STREAM_ID_NONE_;
}

bool msgRequiresOp(int64_t MsgId, const MCSubtargetInfo &STI) {
  return MsgId == ID_SYSMSG ||
      (!isGFX11Plus(STI) &&
       (MsgId == ID_GS_PreGFX11 || MsgId == ID_GS_DONE_PreGFX11));
}

bool msgSupportsStream(int64_t MsgId, int64_t OpId,
                       const MCSubtargetInfo &STI) {
  return !isGFX11Plus(STI) &&
      (MsgId == ID_GS_PreGFX11 || MsgId == ID_GS_DONE_PreGFX11) &&
      OpId != OP_GS_NOP;
}

void decodeMsg(unsigned Val, uint16_t &MsgId, uint16_t &OpId,
               uint16_t &StreamId, const MCSubtargetInfo &STI) {
  MsgId = Val & getMsgIdMask(STI);
  if (isGFX11Plus(STI)) {
    OpId = 0;
    StreamId = 0;
  } else {
    OpId = (Val & OP_MASK_) >> OP_SHIFT_;
    StreamId = (Val & STREAM_ID_MASK_) >> STREAM_ID_SHIFT_;
  }
}

uint64_t encodeMsg(uint64_t MsgId,
                   uint64_t OpId,
                   uint64_t StreamId) {
  return MsgId | (OpId << OP_SHIFT_) | (StreamId << STREAM_ID_SHIFT_);
}

} // namespace SendMsg

//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

unsigned getInitialPSInputAddr(const Function &F) {
  return F.getFnAttributeAsParsedInteger("InitialPSInputAddr", 0);
}

bool getHasColorExport(const Function &F) {
  // As a safe default always respond as if PS has color exports.
  return F.getFnAttributeAsParsedInteger(
             "amdgpu-color-export",
             F.getCallingConv() == CallingConv::AMDGPU_PS ? 1 : 0) != 0;
}

bool getHasDepthExport(const Function &F) {
  return F.getFnAttributeAsParsedInteger("amdgpu-depth-export", 0) != 0;
}

bool isShader(CallingConv::ID cc) {
  switch(cc) {
    case CallingConv::AMDGPU_VS:
    case CallingConv::AMDGPU_LS:
    case CallingConv::AMDGPU_HS:
    case CallingConv::AMDGPU_ES:
    case CallingConv::AMDGPU_GS:
    case CallingConv::AMDGPU_PS:
    case CallingConv::AMDGPU_CS_Chain:
    case CallingConv::AMDGPU_CS_ChainPreserve:
    case CallingConv::AMDGPU_CS:
      return true;
    default:
      return false;
  }
}

bool isGraphics(CallingConv::ID cc) {
  return isShader(cc) || cc == CallingConv::AMDGPU_Gfx;
}

bool isCompute(CallingConv::ID cc) {
  return !isGraphics(cc) || cc == CallingConv::AMDGPU_CS;
}

bool isEntryFunctionCC(CallingConv::ID CC) {
  switch (CC) {
  case CallingConv::AMDGPU_KERNEL:
  case CallingConv::SPIR_KERNEL:
  case CallingConv::AMDGPU_VS:
  case CallingConv::AMDGPU_GS:
  case CallingConv::AMDGPU_PS:
  case CallingConv::AMDGPU_CS:
  case CallingConv::AMDGPU_ES:
  case CallingConv::AMDGPU_HS:
  case CallingConv::AMDGPU_LS:
    return true;
  default:
    return false;
  }
}

bool isModuleEntryFunctionCC(CallingConv::ID CC) {
  switch (CC) {
  case CallingConv::AMDGPU_Gfx:
    return true;
  default:
    return isEntryFunctionCC(CC) || isChainCC(CC);
  }
}

bool isChainCC(CallingConv::ID CC) {
  switch (CC) {
  case CallingConv::AMDGPU_CS_Chain:
  case CallingConv::AMDGPU_CS_ChainPreserve:
    return true;
  default:
    return false;
  }
}

bool isKernelCC(const Function *Func) {
  return AMDGPU::isModuleEntryFunctionCC(Func->getCallingConv());
}

bool hasXNACK(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureXNACK);
}

bool hasSRAMECC(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureSRAMECC);
}

bool hasMIMG_R128(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureMIMG_R128) && !STI.hasFeature(AMDGPU::FeatureR128A16);
}

bool hasA16(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureA16);
}

bool hasG16(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureG16);
}

bool hasPackedD16(const MCSubtargetInfo &STI) {
  return !STI.hasFeature(AMDGPU::FeatureUnpackedD16VMem) && !isCI(STI) &&
         !isSI(STI);
}

bool hasGDS(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureGDS);
}

unsigned getNSAMaxSize(const MCSubtargetInfo &STI, bool HasSampler) {
  auto Version = getIsaVersion(STI.getCPU());
  if (Version.Major == 10)
    return Version.Minor >= 3 ? 13 : 5;
  if (Version.Major == 11)
    return 5;
  if (Version.Major >= 12)
    return HasSampler ? 4 : 5;
  return 0;
}

unsigned getMaxNumUserSGPRs(const MCSubtargetInfo &STI) { return 16; }

bool isSI(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureSouthernIslands);
}

bool isCI(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureSeaIslands);
}

bool isVI(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureVolcanicIslands);
}

bool isGFX9(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureGFX9);
}

bool isGFX9_GFX10(const MCSubtargetInfo &STI) {
  return isGFX9(STI) || isGFX10(STI);
}

bool isGFX9_GFX10_GFX11(const MCSubtargetInfo &STI) {
  return isGFX9(STI) || isGFX10(STI) || isGFX11(STI);
}

bool isGFX8_GFX9_GFX10(const MCSubtargetInfo &STI) {
  return isVI(STI) || isGFX9(STI) || isGFX10(STI);
}

bool isGFX8Plus(const MCSubtargetInfo &STI) {
  return isVI(STI) || isGFX9Plus(STI);
}

bool isGFX9Plus(const MCSubtargetInfo &STI) {
  return isGFX9(STI) || isGFX10Plus(STI);
}

bool isNotGFX9Plus(const MCSubtargetInfo &STI) { return !isGFX9Plus(STI); }

bool isGFX10(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureGFX10);
}

bool isGFX10_GFX11(const MCSubtargetInfo &STI) {
  return isGFX10(STI) || isGFX11(STI);
}

bool isGFX10Plus(const MCSubtargetInfo &STI) {
  return isGFX10(STI) || isGFX11Plus(STI);
}

bool isGFX11(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureGFX11);
}

bool isGFX11Plus(const MCSubtargetInfo &STI) {
  return isGFX11(STI) || isGFX12Plus(STI);
}

bool isGFX12(const MCSubtargetInfo &STI) {
  return STI.getFeatureBits()[AMDGPU::FeatureGFX12];
}

bool isGFX12Plus(const MCSubtargetInfo &STI) { return isGFX12(STI); }

bool isNotGFX12Plus(const MCSubtargetInfo &STI) { return !isGFX12Plus(STI); }

bool isNotGFX11Plus(const MCSubtargetInfo &STI) {
  return !isGFX11Plus(STI);
}

bool isNotGFX10Plus(const MCSubtargetInfo &STI) {
  return isSI(STI) || isCI(STI) || isVI(STI) || isGFX9(STI);
}

bool isGFX10Before1030(const MCSubtargetInfo &STI) {
  return isGFX10(STI) && !AMDGPU::isGFX10_BEncoding(STI);
}

bool isGCN3Encoding(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureGCN3Encoding);
}

bool isGFX10_AEncoding(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureGFX10_AEncoding);
}

bool isGFX10_BEncoding(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureGFX10_BEncoding);
}

bool hasGFX10_3Insts(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureGFX10_3Insts);
}

bool isGFX10_3_GFX11(const MCSubtargetInfo &STI) {
  return isGFX10_BEncoding(STI) && !isGFX12Plus(STI);
}

bool isGFX90A(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureGFX90AInsts);
}

bool isGFX940(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureGFX940Insts);
}

bool hasArchitectedFlatScratch(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureArchitectedFlatScratch);
}

bool hasMAIInsts(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureMAIInsts);
}

bool hasVOPD(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureVOPD);
}

bool hasDPPSrc1SGPR(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureDPPSrc1SGPR);
}

unsigned hasKernargPreload(const MCSubtargetInfo &STI) {
  return STI.hasFeature(AMDGPU::FeatureKernargPreload);
}

int32_t getTotalNumVGPRs(bool has90AInsts, int32_t ArgNumAGPR,
                         int32_t ArgNumVGPR) {
  if (has90AInsts && ArgNumAGPR)
    return alignTo(ArgNumVGPR, 4) + ArgNumAGPR;
  return std::max(ArgNumVGPR, ArgNumAGPR);
}

bool isSGPR(unsigned Reg, const MCRegisterInfo* TRI) {
  const MCRegisterClass SGPRClass = TRI->getRegClass(AMDGPU::SReg_32RegClassID);
  const unsigned FirstSubReg = TRI->getSubReg(Reg, AMDGPU::sub0);
  return SGPRClass.contains(FirstSubReg != 0 ? FirstSubReg : Reg) ||
    Reg == AMDGPU::SCC;
}

bool isHi(unsigned Reg, const MCRegisterInfo &MRI) {
  return MRI.getEncodingValue(Reg) & AMDGPU::HWEncoding::IS_HI;
}

#define MAP_REG2REG \
  using namespace AMDGPU; \
  switch(Reg) { \
  default: return Reg; \
  CASE_CI_VI(FLAT_SCR) \
  CASE_CI_VI(FLAT_SCR_LO) \
  CASE_CI_VI(FLAT_SCR_HI) \
  CASE_VI_GFX9PLUS(TTMP0) \
  CASE_VI_GFX9PLUS(TTMP1) \
  CASE_VI_GFX9PLUS(TTMP2) \
  CASE_VI_GFX9PLUS(TTMP3) \
  CASE_VI_GFX9PLUS(TTMP4) \
  CASE_VI_GFX9PLUS(TTMP5) \
  CASE_VI_GFX9PLUS(TTMP6) \
  CASE_VI_GFX9PLUS(TTMP7) \
  CASE_VI_GFX9PLUS(TTMP8) \
  CASE_VI_GFX9PLUS(TTMP9) \
  CASE_VI_GFX9PLUS(TTMP10) \
  CASE_VI_GFX9PLUS(TTMP11) \
  CASE_VI_GFX9PLUS(TTMP12) \
  CASE_VI_GFX9PLUS(TTMP13) \
  CASE_VI_GFX9PLUS(TTMP14) \
  CASE_VI_GFX9PLUS(TTMP15) \
  CASE_VI_GFX9PLUS(TTMP0_TTMP1) \
  CASE_VI_GFX9PLUS(TTMP2_TTMP3) \
  CASE_VI_GFX9PLUS(TTMP4_TTMP5) \
  CASE_VI_GFX9PLUS(TTMP6_TTMP7) \
  CASE_VI_GFX9PLUS(TTMP8_TTMP9) \
  CASE_VI_GFX9PLUS(TTMP10_TTMP11) \
  CASE_VI_GFX9PLUS(TTMP12_TTMP13) \
  CASE_VI_GFX9PLUS(TTMP14_TTMP15) \
  CASE_VI_GFX9PLUS(TTMP0_TTMP1_TTMP2_TTMP3) \
  CASE_VI_GFX9PLUS(TTMP4_TTMP5_TTMP6_TTMP7) \
  CASE_VI_GFX9PLUS(TTMP8_TTMP9_TTMP10_TTMP11) \
  CASE_VI_GFX9PLUS(TTMP12_TTMP13_TTMP14_TTMP15) \
  CASE_VI_GFX9PLUS(TTMP0_TTMP1_TTMP2_TTMP3_TTMP4_TTMP5_TTMP6_TTMP7) \
  CASE_VI_GFX9PLUS(TTMP4_TTMP5_TTMP6_TTMP7_TTMP8_TTMP9_TTMP10_TTMP11) \
  CASE_VI_GFX9PLUS(TTMP8_TTMP9_TTMP10_TTMP11_TTMP12_TTMP13_TTMP14_TTMP15) \
  CASE_VI_GFX9PLUS(TTMP0_TTMP1_TTMP2_TTMP3_TTMP4_TTMP5_TTMP6_TTMP7_TTMP8_TTMP9_TTMP10_TTMP11_TTMP12_TTMP13_TTMP14_TTMP15) \
  CASE_GFXPRE11_GFX11PLUS(M0) \
  CASE_GFXPRE11_GFX11PLUS(SGPR_NULL) \
  CASE_GFXPRE11_GFX11PLUS_TO(SGPR_NULL64, SGPR_NULL) \
  }

#define CASE_CI_VI(node) \
  assert(!isSI(STI)); \
  case node: return isCI(STI) ? node##_ci : node##_vi;

#define CASE_VI_GFX9PLUS(node) \
  case node: return isGFX9Plus(STI) ? node##_gfx9plus : node##_vi;

#define CASE_GFXPRE11_GFX11PLUS(node) \
  case node: return isGFX11Plus(STI) ? node##_gfx11plus : node##_gfxpre11;

#define CASE_GFXPRE11_GFX11PLUS_TO(node, result) \
  case node: return isGFX11Plus(STI) ? result##_gfx11plus : result##_gfxpre11;

unsigned getMCReg(unsigned Reg, const MCSubtargetInfo &STI) {
  if (STI.getTargetTriple().getArch() == Triple::r600)
    return Reg;
  MAP_REG2REG
}

#undef CASE_CI_VI
#undef CASE_VI_GFX9PLUS
#undef CASE_GFXPRE11_GFX11PLUS
#undef CASE_GFXPRE11_GFX11PLUS_TO

#define CASE_CI_VI(node)   case node##_ci: case node##_vi:   return node;
#define CASE_VI_GFX9PLUS(node) case node##_vi: case node##_gfx9plus: return node;
#define CASE_GFXPRE11_GFX11PLUS(node) case node##_gfx11plus: case node##_gfxpre11: return node;
#define CASE_GFXPRE11_GFX11PLUS_TO(node, result)

unsigned mc2PseudoReg(unsigned Reg) {
  MAP_REG2REG
}

bool isInlineValue(unsigned Reg) {
  switch (Reg) {
  case AMDGPU::SRC_SHARED_BASE_LO:
  case AMDGPU::SRC_SHARED_BASE:
  case AMDGPU::SRC_SHARED_LIMIT_LO:
  case AMDGPU::SRC_SHARED_LIMIT:
  case AMDGPU::SRC_PRIVATE_BASE_LO:
  case AMDGPU::SRC_PRIVATE_BASE:
  case AMDGPU::SRC_PRIVATE_LIMIT_LO:
  case AMDGPU::SRC_PRIVATE_LIMIT:
  case AMDGPU::SRC_POPS_EXITING_WAVE_ID:
    return true;
  case AMDGPU::SRC_VCCZ:
  case AMDGPU::SRC_EXECZ:
  case AMDGPU::SRC_SCC:
    return true;
  case AMDGPU::SGPR_NULL:
    return true;
  default:
    return false;
  }
}

#undef CASE_CI_VI
#undef CASE_VI_GFX9PLUS
#undef CASE_GFXPRE11_GFX11PLUS
#undef CASE_GFXPRE11_GFX11PLUS_TO
#undef MAP_REG2REG

bool isSISrcOperand(const MCInstrDesc &Desc, unsigned OpNo) {
  assert(OpNo < Desc.NumOperands);
  unsigned OpType = Desc.operands()[OpNo].OperandType;
  return OpType >= AMDGPU::OPERAND_SRC_FIRST &&
         OpType <= AMDGPU::OPERAND_SRC_LAST;
}

bool isKImmOperand(const MCInstrDesc &Desc, unsigned OpNo) {
  assert(OpNo < Desc.NumOperands);
  unsigned OpType = Desc.operands()[OpNo].OperandType;
  return OpType >= AMDGPU::OPERAND_KIMM_FIRST &&
         OpType <= AMDGPU::OPERAND_KIMM_LAST;
}

bool isSISrcFPOperand(const MCInstrDesc &Desc, unsigned OpNo) {
  assert(OpNo < Desc.NumOperands);
  unsigned OpType = Desc.operands()[OpNo].OperandType;
  switch (OpType) {
  case AMDGPU::OPERAND_REG_IMM_FP32:
  case AMDGPU::OPERAND_REG_IMM_FP32_DEFERRED:
  case AMDGPU::OPERAND_REG_IMM_FP64:
  case AMDGPU::OPERAND_REG_IMM_FP16:
  case AMDGPU::OPERAND_REG_IMM_FP16_DEFERRED:
  case AMDGPU::OPERAND_REG_IMM_V2FP16:
  case AMDGPU::OPERAND_REG_INLINE_C_FP32:
  case AMDGPU::OPERAND_REG_INLINE_C_FP64:
  case AMDGPU::OPERAND_REG_INLINE_C_FP16:
  case AMDGPU::OPERAND_REG_INLINE_C_V2FP16:
  case AMDGPU::OPERAND_REG_INLINE_AC_FP32:
  case AMDGPU::OPERAND_REG_INLINE_AC_FP16:
  case AMDGPU::OPERAND_REG_INLINE_AC_V2FP16:
  case AMDGPU::OPERAND_REG_IMM_V2FP32:
  case AMDGPU::OPERAND_REG_INLINE_C_V2FP32:
  case AMDGPU::OPERAND_REG_INLINE_AC_FP64:
    return true;
  default:
    return false;
  }
}

bool isSISrcInlinableOperand(const MCInstrDesc &Desc, unsigned OpNo) {
  assert(OpNo < Desc.NumOperands);
  unsigned OpType = Desc.operands()[OpNo].OperandType;
  return (OpType >= AMDGPU::OPERAND_REG_INLINE_C_FIRST &&
          OpType <= AMDGPU::OPERAND_REG_INLINE_C_LAST) ||
         (OpType >= AMDGPU::OPERAND_REG_INLINE_AC_FIRST &&
          OpType <= AMDGPU::OPERAND_REG_INLINE_AC_LAST);
}

// Avoid using MCRegisterClass::getSize, since that function will go away
// (move from MC* level to Target* level). Return size in bits.
unsigned getRegBitWidth(unsigned RCID) {
  switch (RCID) {
  case AMDGPU::SGPR_LO16RegClassID:
  case AMDGPU::AGPR_LO16RegClassID:
    return 16;
  case AMDGPU::SGPR_32RegClassID:
  case AMDGPU::VGPR_32RegClassID:
  case AMDGPU::VRegOrLds_32RegClassID:
  case AMDGPU::AGPR_32RegClassID:
  case AMDGPU::VS_32RegClassID:
  case AMDGPU::AV_32RegClassID:
  case AMDGPU::SReg_32RegClassID:
  case AMDGPU::SReg_32_XM0RegClassID:
  case AMDGPU::SRegOrLds_32RegClassID:
    return 32;
  case AMDGPU::SGPR_64RegClassID:
  case AMDGPU::VS_64RegClassID:
  case AMDGPU::SReg_64RegClassID:
  case AMDGPU::VReg_64RegClassID:
  case AMDGPU::AReg_64RegClassID:
  case AMDGPU::SReg_64_XEXECRegClassID:
  case AMDGPU::VReg_64_Align2RegClassID:
  case AMDGPU::AReg_64_Align2RegClassID:
  case AMDGPU::AV_64RegClassID:
  case AMDGPU::AV_64_Align2RegClassID:
    return 64;
  case AMDGPU::SGPR_96RegClassID:
  case AMDGPU::SReg_96RegClassID:
  case AMDGPU::VReg_96RegClassID:
  case AMDGPU::AReg_96RegClassID:
  case AMDGPU::VReg_96_Align2RegClassID:
  case AMDGPU::AReg_96_Align2RegClassID:
  case AMDGPU::AV_96RegClassID:
  case AMDGPU::AV_96_Align2RegClassID:
    return 96;
  case AMDGPU::SGPR_128RegClassID:
  case AMDGPU::SReg_128RegClassID:
  case AMDGPU::VReg_128RegClassID:
  case AMDGPU::AReg_128RegClassID:
  case AMDGPU::VReg_128_Align2RegClassID:
  case AMDGPU::AReg_128_Align2RegClassID:
  case AMDGPU::AV_128RegClassID:
  case AMDGPU::AV_128_Align2RegClassID:
    return 128;
  case AMDGPU::SGPR_160RegClassID:
  case AMDGPU::SReg_160RegClassID:
  case AMDGPU::VReg_160RegClassID:
  case AMDGPU::AReg_160RegClassID:
  case AMDGPU::VReg_160_Align2RegClassID:
  case AMDGPU::AReg_160_Align2RegClassID:
  case AMDGPU::AV_160RegClassID:
  case AMDGPU::AV_160_Align2RegClassID:
    return 160;
  case AMDGPU::SGPR_192RegClassID:
  case AMDGPU::SReg_192RegClassID:
  case AMDGPU::VReg_192RegClassID:
  case AMDGPU::AReg_192RegClassID:
  case AMDGPU::VReg_192_Align2RegClassID:
  case AMDGPU::AReg_192_Align2RegClassID:
  case AMDGPU::AV_192RegClassID:
  case AMDGPU::AV_192_Align2RegClassID:
    return 192;
  case AMDGPU::SGPR_224RegClassID:
  case AMDGPU::SReg_224RegClassID:
  case AMDGPU::VReg_224RegClassID:
  case AMDGPU::AReg_224RegClassID:
  case AMDGPU::VReg_224_Align2RegClassID:
  case AMDGPU::AReg_224_Align2RegClassID:
  case AMDGPU::AV_224RegClassID:
  case AMDGPU::AV_224_Align2RegClassID:
    return 224;
  case AMDGPU::SGPR_256RegClassID:
  case AMDGPU::SReg_256RegClassID:
  case AMDGPU::VReg_256RegClassID:
  case AMDGPU::AReg_256RegClassID:
  case AMDGPU::VReg_256_Align2RegClassID:
  case AMDGPU::AReg_256_Align2RegClassID:
  case AMDGPU::AV_256RegClassID:
  case AMDGPU::AV_256_Align2RegClassID:
    return 256;
  case AMDGPU::SGPR_288RegClassID:
  case AMDGPU::SReg_288RegClassID:
  case AMDGPU::VReg_288RegClassID:
  case AMDGPU::AReg_288RegClassID:
  case AMDGPU::VReg_288_Align2RegClassID:
  case AMDGPU::AReg_288_Align2RegClassID:
  case AMDGPU::AV_288RegClassID:
  case AMDGPU::AV_288_Align2RegClassID:
    return 288;
  case AMDGPU::SGPR_320RegClassID:
  case AMDGPU::SReg_320RegClassID:
  case AMDGPU::VReg_320RegClassID:
  case AMDGPU::AReg_320RegClassID:
  case AMDGPU::VReg_320_Align2RegClassID:
  case AMDGPU::AReg_320_Align2RegClassID:
  case AMDGPU::AV_320RegClassID:
  case AMDGPU::AV_320_Align2RegClassID:
    return 320;
  case AMDGPU::SGPR_352RegClassID:
  case AMDGPU::SReg_352RegClassID:
  case AMDGPU::VReg_352RegClassID:
  case AMDGPU::AReg_352RegClassID:
  case AMDGPU::VReg_352_Align2RegClassID:
  case AMDGPU::AReg_352_Align2RegClassID:
  case AMDGPU::AV_352RegClassID:
  case AMDGPU::AV_352_Align2RegClassID:
    return 352;
  case AMDGPU::SGPR_384RegClassID:
  case AMDGPU::SReg_384RegClassID:
  case AMDGPU::VReg_384RegClassID:
  case AMDGPU::AReg_384RegClassID:
  case AMDGPU::VReg_384_Align2RegClassID:
  case AMDGPU::AReg_384_Align2RegClassID:
  case AMDGPU::AV_384RegClassID:
  case AMDGPU::AV_384_Align2RegClassID:
    return 384;
  case AMDGPU::SGPR_512RegClassID:
  case AMDGPU::SReg_512RegClassID:
  case AMDGPU::VReg_512RegClassID:
  case AMDGPU::AReg_512RegClassID:
  case AMDGPU::VReg_512_Align2RegClassID:
  case AMDGPU::AReg_512_Align2RegClassID:
  case AMDGPU::AV_512RegClassID:
  case AMDGPU::AV_512_Align2RegClassID:
    return 512;
  case AMDGPU::SGPR_1024RegClassID:
  case AMDGPU::SReg_1024RegClassID:
  case AMDGPU::VReg_1024RegClassID:
  case AMDGPU::AReg_1024RegClassID:
  case AMDGPU::VReg_1024_Align2RegClassID:
  case AMDGPU::AReg_1024_Align2RegClassID:
  case AMDGPU::AV_1024RegClassID:
  case AMDGPU::AV_1024_Align2RegClassID:
    return 1024;
  default:
    llvm_unreachable("Unexpected register class");
  }
}

unsigned getRegBitWidth(const MCRegisterClass &RC) {
  return getRegBitWidth(RC.getID());
}

unsigned getRegOperandSize(const MCRegisterInfo *MRI, const MCInstrDesc &Desc,
                           unsigned OpNo) {
  assert(OpNo < Desc.NumOperands);
  unsigned RCID = Desc.operands()[OpNo].RegClass;
  return getRegBitWidth(RCID) / 8;
}

bool isInlinableLiteral64(int64_t Literal, bool HasInv2Pi) {
  if (isInlinableIntLiteral(Literal))
    return true;

  uint64_t Val = static_cast<uint64_t>(Literal);
  return (Val == llvm::bit_cast<uint64_t>(0.0)) ||
         (Val == llvm::bit_cast<uint64_t>(1.0)) ||
         (Val == llvm::bit_cast<uint64_t>(-1.0)) ||
         (Val == llvm::bit_cast<uint64_t>(0.5)) ||
         (Val == llvm::bit_cast<uint64_t>(-0.5)) ||
         (Val == llvm::bit_cast<uint64_t>(2.0)) ||
         (Val == llvm::bit_cast<uint64_t>(-2.0)) ||
         (Val == llvm::bit_cast<uint64_t>(4.0)) ||
         (Val == llvm::bit_cast<uint64_t>(-4.0)) ||
         (Val == 0x3fc45f306dc9c882 && HasInv2Pi);
}

bool isInlinableLiteral32(int32_t Literal, bool HasInv2Pi) {
  if (isInlinableIntLiteral(Literal))
    return true;

  // The actual type of the operand does not seem to matter as long
  // as the bits match one of the inline immediate values.  For example:
  //
  // -nan has the hexadecimal encoding of 0xfffffffe which is -2 in decimal,
  // so it is a legal inline immediate.
  //
  // 1065353216 has the hexadecimal encoding 0x3f800000 which is 1.0f in
  // floating-point, so it is a legal inline immediate.

  uint32_t Val = static_cast<uint32_t>(Literal);
  return (Val == llvm::bit_cast<uint32_t>(0.0f)) ||
         (Val == llvm::bit_cast<uint32_t>(1.0f)) ||
         (Val == llvm::bit_cast<uint32_t>(-1.0f)) ||
         (Val == llvm::bit_cast<uint32_t>(0.5f)) ||
         (Val == llvm::bit_cast<uint32_t>(-0.5f)) ||
         (Val == llvm::bit_cast<uint32_t>(2.0f)) ||
         (Val == llvm::bit_cast<uint32_t>(-2.0f)) ||
         (Val == llvm::bit_cast<uint32_t>(4.0f)) ||
         (Val == llvm::bit_cast<uint32_t>(-4.0f)) ||
         (Val == 0x3e22f983 && HasInv2Pi);
}

bool isInlinableLiteralBF16(int16_t Literal, bool HasInv2Pi) {
  if (!HasInv2Pi)
    return false;
  if (isInlinableIntLiteral(Literal))
    return true;
  uint16_t Val = static_cast<uint16_t>(Literal);
  return Val == 0x3F00 || // 0.5
         Val == 0xBF00 || // -0.5
         Val == 0x3F80 || // 1.0
         Val == 0xBF80 || // -1.0
         Val == 0x4000 || // 2.0
         Val == 0xC000 || // -2.0
         Val == 0x4080 || // 4.0
         Val == 0xC080 || // -4.0
         Val == 0x3E22;   // 1.0 / (2.0 * pi)
}

bool isInlinableLiteralI16(int32_t Literal, bool HasInv2Pi) {
  return isInlinableLiteral32(Literal, HasInv2Pi);
}

bool isInlinableLiteralFP16(int16_t Literal, bool HasInv2Pi) {
  if (!HasInv2Pi)
    return false;
  if (isInlinableIntLiteral(Literal))
    return true;
  uint16_t Val = static_cast<uint16_t>(Literal);
  return Val == 0x3C00 || // 1.0
         Val == 0xBC00 || // -1.0
         Val == 0x3800 || // 0.5
         Val == 0xB800 || // -0.5
         Val == 0x4000 || // 2.0
         Val == 0xC000 || // -2.0
         Val == 0x4400 || // 4.0
         Val == 0xC400 || // -4.0
         Val == 0x3118;   // 1/2pi
}

std::optional<unsigned> getInlineEncodingV216(bool IsFloat, uint32_t Literal) {
  // Unfortunately, the Instruction Set Architecture Reference Guide is
  // misleading about how the inline operands work for (packed) 16-bit
  // instructions. In a nutshell, the actual HW behavior is:
  //
  //  - integer encodings (-16 .. 64) are always produced as sign-extended
  //    32-bit values
  //  - float encodings are produced as:
  //    - for F16 instructions: corresponding half-precision float values in
  //      the LSBs, 0 in the MSBs
  //    - for UI16 instructions: corresponding single-precision float value
  int32_t Signed = static_cast<int32_t>(Literal);
  if (Signed >= 0 && Signed <= 64)
    return 128 + Signed;

  if (Signed >= -16 && Signed <= -1)
    return 192 + std::abs(Signed);

  if (IsFloat) {
    // clang-format off
    switch (Literal) {
    case 0x3800: return 240; // 0.5
    case 0xB800: return 241; // -0.5
    case 0x3C00: return 242; // 1.0
    case 0xBC00: return 243; // -1.0
    case 0x4000: return 244; // 2.0
    case 0xC000: return 245; // -2.0
    case 0x4400: return 246; // 4.0
    case 0xC400: return 247; // -4.0
    case 0x3118: return 248; // 1.0 / (2.0 * pi)
    default: break;
    }
    // clang-format on
  } else {
    // clang-format off
    switch (Literal) {
    case 0x3F000000: return 240; // 0.5
    case 0xBF000000: return 241; // -0.5
    case 0x3F800000: return 242; // 1.0
    case 0xBF800000: return 243; // -1.0
    case 0x40000000: return 244; // 2.0
    case 0xC0000000: return 245; // -2.0
    case 0x40800000: return 246; // 4.0
    case 0xC0800000: return 247; // -4.0
    case 0x3E22F983: return 248; // 1.0 / (2.0 * pi)
    default: break;
    }
    // clang-format on
  }

  return {};
}

// Encoding of the literal as an inline constant for a V_PK_*_IU16 instruction
// or nullopt.
std::optional<unsigned> getInlineEncodingV2I16(uint32_t Literal) {
  return getInlineEncodingV216(false, Literal);
}

// Encoding of the literal as an inline constant for a V_PK_*_BF16 instruction
// or nullopt.
std::optional<unsigned> getInlineEncodingV2BF16(uint32_t Literal) {
  int32_t Signed = static_cast<int32_t>(Literal);
  if (Signed >= 0 && Signed <= 64)
    return 128 + Signed;

  if (Signed >= -16 && Signed <= -1)
    return 192 + std::abs(Signed);

  // clang-format off
  switch (Literal) {
  case 0x3F00: return 240; // 0.5
  case 0xBF00: return 241; // -0.5
  case 0x3F80: return 242; // 1.0
  case 0xBF80: return 243; // -1.0
  case 0x4000: return 244; // 2.0
  case 0xC000: return 245; // -2.0
  case 0x4080: return 246; // 4.0
  case 0xC080: return 247; // -4.0
  case 0x3E22: return 248; // 1.0 / (2.0 * pi)
  default: break;
  }
  // clang-format on

  return std::nullopt;
}

// Encoding of the literal as an inline constant for a V_PK_*_F16 instruction
// or nullopt.
std::optional<unsigned> getInlineEncodingV2F16(uint32_t Literal) {
  return getInlineEncodingV216(true, Literal);
}

// Whether the given literal can be inlined for a V_PK_* instruction.
bool isInlinableLiteralV216(uint32_t Literal, uint8_t OpType) {
  switch (OpType) {
  case AMDGPU::OPERAND_REG_IMM_V2INT16:
  case AMDGPU::OPERAND_REG_INLINE_C_V2INT16:
  case AMDGPU::OPERAND_REG_INLINE_AC_V2INT16:
    return getInlineEncodingV216(false, Literal).has_value();
  case AMDGPU::OPERAND_REG_IMM_V2FP16:
  case AMDGPU::OPERAND_REG_INLINE_C_V2FP16:
  case AMDGPU::OPERAND_REG_INLINE_AC_V2FP16:
    return getInlineEncodingV216(true, Literal).has_value();
  case AMDGPU::OPERAND_REG_IMM_V2BF16:
  case AMDGPU::OPERAND_REG_INLINE_C_V2BF16:
  case AMDGPU::OPERAND_REG_INLINE_AC_V2BF16:
    return isInlinableLiteralV2BF16(Literal);
  default:
    llvm_unreachable("bad packed operand type");
  }
}

// Whether the given literal can be inlined for a V_PK_*_IU16 instruction.
bool isInlinableLiteralV2I16(uint32_t Literal) {
  return getInlineEncodingV2I16(Literal).has_value();
}

// Whether the given literal can be inlined for a V_PK_*_BF16 instruction.
bool isInlinableLiteralV2BF16(uint32_t Literal) {
  return getInlineEncodingV2BF16(Literal).has_value();
}

// Whether the given literal can be inlined for a V_PK_*_F16 instruction.
bool isInlinableLiteralV2F16(uint32_t Literal) {
  return getInlineEncodingV2F16(Literal).has_value();
}

bool isValid32BitLiteral(uint64_t Val, bool IsFP64) {
  if (IsFP64)
    return !(Val & 0xffffffffu);

  return isUInt<32>(Val) || isInt<32>(Val);
}

bool isArgPassedInSGPR(const Argument *A) {
  const Function *F = A->getParent();

  // Arguments to compute shaders are never a source of divergence.
  CallingConv::ID CC = F->getCallingConv();
  switch (CC) {
  case CallingConv::AMDGPU_KERNEL:
  case CallingConv::SPIR_KERNEL:
    return true;
  case CallingConv::AMDGPU_VS:
  case CallingConv::AMDGPU_LS:
  case CallingConv::AMDGPU_HS:
  case CallingConv::AMDGPU_ES:
  case CallingConv::AMDGPU_GS:
  case CallingConv::AMDGPU_PS:
  case CallingConv::AMDGPU_CS:
  case CallingConv::AMDGPU_Gfx:
  case CallingConv::AMDGPU_CS_Chain:
  case CallingConv::AMDGPU_CS_ChainPreserve:
    // For non-compute shaders, SGPR inputs are marked with either inreg or
    // byval. Everything else is in VGPRs.
    return A->hasAttribute(Attribute::InReg) ||
           A->hasAttribute(Attribute::ByVal);
  default:
    // TODO: treat i1 as divergent?
    return A->hasAttribute(Attribute::InReg);
  }
}

bool isArgPassedInSGPR(const CallBase *CB, unsigned ArgNo) {
  // Arguments to compute shaders are never a source of divergence.
  CallingConv::ID CC = CB->getCallingConv();
  switch (CC) {
  case CallingConv::AMDGPU_KERNEL:
  case CallingConv::SPIR_KERNEL:
    return true;
  case CallingConv::AMDGPU_VS:
  case CallingConv::AMDGPU_LS:
  case CallingConv::AMDGPU_HS:
  case CallingConv::AMDGPU_ES:
  case CallingConv::AMDGPU_GS:
  case CallingConv::AMDGPU_PS:
  case CallingConv::AMDGPU_CS:
  case CallingConv::AMDGPU_Gfx:
  case CallingConv::AMDGPU_CS_Chain:
  case CallingConv::AMDGPU_CS_ChainPreserve:
    // For non-compute shaders, SGPR inputs are marked with either inreg or
    // byval. Everything else is in VGPRs.
    return CB->paramHasAttr(ArgNo, Attribute::InReg) ||
           CB->paramHasAttr(ArgNo, Attribute::ByVal);
  default:
    return CB->paramHasAttr(ArgNo, Attribute::InReg);
  }
}

static bool hasSMEMByteOffset(const MCSubtargetInfo &ST) {
  return isGCN3Encoding(ST) || isGFX10Plus(ST);
}

bool isLegalSMRDEncodedUnsignedOffset(const MCSubtargetInfo &ST,
                                      int64_t EncodedOffset) {
  if (isGFX12Plus(ST))
    return isUInt<23>(EncodedOffset);

  return hasSMEMByteOffset(ST) ? isUInt<20>(EncodedOffset)
                               : isUInt<8>(EncodedOffset);
}

bool isLegalSMRDEncodedSignedOffset(const MCSubtargetInfo &ST,
                                    int64_t EncodedOffset,
                                    bool IsBuffer) {
  if (isGFX12Plus(ST))
    return isInt<24>(EncodedOffset);

  return !IsBuffer &&
         hasSMRDSignedImmOffset(ST) &&
         isInt<21>(EncodedOffset);
}

static bool isDwordAligned(uint64_t ByteOffset) {
  return (ByteOffset & 3) == 0;
}

uint64_t convertSMRDOffsetUnits(const MCSubtargetInfo &ST,
                                uint64_t ByteOffset) {
  if (hasSMEMByteOffset(ST))
    return ByteOffset;

  assert(isDwordAligned(ByteOffset));
  return ByteOffset >> 2;
}

std::optional<int64_t> getSMRDEncodedOffset(const MCSubtargetInfo &ST,
                                            int64_t ByteOffset, bool IsBuffer,
                                            bool HasSOffset) {
  // For unbuffered smem loads, it is illegal for the Immediate Offset to be
  // negative if the resulting (Offset + (M0 or SOffset or zero) is negative.
  // Handle case where SOffset is not present.
  if (!IsBuffer && !HasSOffset && ByteOffset < 0 && hasSMRDSignedImmOffset(ST))
    return std::nullopt;

  if (isGFX12Plus(ST)) // 24 bit signed offsets
    return isInt<24>(ByteOffset) ? std::optional<int64_t>(ByteOffset)
                                 : std::nullopt;

  // The signed version is always a byte offset.
  if (!IsBuffer && hasSMRDSignedImmOffset(ST)) {
    assert(hasSMEMByteOffset(ST));
    return isInt<20>(ByteOffset) ? std::optional<int64_t>(ByteOffset)
                                 : std::nullopt;
  }

  if (!isDwordAligned(ByteOffset) && !hasSMEMByteOffset(ST))
    return std::nullopt;

  int64_t EncodedOffset = convertSMRDOffsetUnits(ST, ByteOffset);
  return isLegalSMRDEncodedUnsignedOffset(ST, EncodedOffset)
             ? std::optional<int64_t>(EncodedOffset)
             : std::nullopt;
}

std::optional<int64_t> getSMRDEncodedLiteralOffset32(const MCSubtargetInfo &ST,
                                                     int64_t ByteOffset) {
  if (!isCI(ST) || !isDwordAligned(ByteOffset))
    return std::nullopt;

  int64_t EncodedOffset = convertSMRDOffsetUnits(ST, ByteOffset);
  return isUInt<32>(EncodedOffset) ? std::optional<int64_t>(EncodedOffset)
                                   : std::nullopt;
}

unsigned getNumFlatOffsetBits(const MCSubtargetInfo &ST) {
  if (AMDGPU::isGFX10(ST))
    return 12;

  if (AMDGPU::isGFX12(ST))
    return 24;
  return 13;
}

namespace {

struct SourceOfDivergence {
  unsigned Intr;
};
const SourceOfDivergence *lookupSourceOfDivergence(unsigned Intr);

struct AlwaysUniform {
  unsigned Intr;
};
const AlwaysUniform *lookupAlwaysUniform(unsigned Intr);

#define GET_SourcesOfDivergence_IMPL
#define GET_UniformIntrinsics_IMPL
#define GET_Gfx9BufferFormat_IMPL
#define GET_Gfx10BufferFormat_IMPL
#define GET_Gfx11PlusBufferFormat_IMPL
#include "AMDGPUGenSearchableTables.inc"

} // end anonymous namespace

bool isIntrinsicSourceOfDivergence(unsigned IntrID) {
  return lookupSourceOfDivergence(IntrID);
}

bool isIntrinsicAlwaysUniform(unsigned IntrID) {
  return lookupAlwaysUniform(IntrID);
}

const GcnBufferFormatInfo *getGcnBufferFormatInfo(uint8_t BitsPerComp,
                                                  uint8_t NumComponents,
                                                  uint8_t NumFormat,
                                                  const MCSubtargetInfo &STI) {
  return isGFX11Plus(STI)
             ? getGfx11PlusBufferFormatInfo(BitsPerComp, NumComponents,
                                            NumFormat)
             : isGFX10(STI) ? getGfx10BufferFormatInfo(BitsPerComp,
                                                       NumComponents, NumFormat)
                            : getGfx9BufferFormatInfo(BitsPerComp,
                                                      NumComponents, NumFormat);
}

const GcnBufferFormatInfo *getGcnBufferFormatInfo(uint8_t Format,
                                                  const MCSubtargetInfo &STI) {
  return isGFX11Plus(STI) ? getGfx11PlusBufferFormatInfo(Format)
                          : isGFX10(STI) ? getGfx10BufferFormatInfo(Format)
                                         : getGfx9BufferFormatInfo(Format);
}

bool hasAny64BitVGPROperands(const MCInstrDesc &OpDesc) {
  for (auto OpName : { OpName::vdst, OpName::src0, OpName::src1,
                       OpName::src2 }) {
    int Idx = getNamedOperandIdx(OpDesc.getOpcode(), OpName);
    if (Idx == -1)
      continue;

    if (OpDesc.operands()[Idx].RegClass == AMDGPU::VReg_64RegClassID ||
        OpDesc.operands()[Idx].RegClass == AMDGPU::VReg_64_Align2RegClassID)
      return true;
  }

  return false;
}

bool isDPALU_DPP(const MCInstrDesc &OpDesc) {
  return hasAny64BitVGPROperands(OpDesc);
}

unsigned getLdsDwGranularity(const MCSubtargetInfo &ST) {
  // Currently this is 128 for all subtargets
  return 128;
}

} // namespace AMDGPU

raw_ostream &operator<<(raw_ostream &OS,
                        const AMDGPU::IsaInfo::TargetIDSetting S) {
  switch (S) {
  case (AMDGPU::IsaInfo::TargetIDSetting::Unsupported):
    OS << "Unsupported";
    break;
  case (AMDGPU::IsaInfo::TargetIDSetting::Any):
    OS << "Any";
    break;
  case (AMDGPU::IsaInfo::TargetIDSetting::Off):
    OS << "Off";
    break;
  case (AMDGPU::IsaInfo::TargetIDSetting::On):
    OS << "On";
    break;
  }
  return OS;
}

} // namespace llvm
