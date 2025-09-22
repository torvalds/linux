//===- AMDGPUBaseInfo.h - Top level definitions for AMDGPU ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_UTILS_AMDGPUBASEINFO_H
#define LLVM_LIB_TARGET_AMDGPU_UTILS_AMDGPUBASEINFO_H

#include "AMDGPUSubtarget.h"
#include "SIDefines.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Alignment.h"
#include <array>
#include <functional>
#include <utility>

struct amd_kernel_code_t;

namespace llvm {

struct Align;
class Argument;
class Function;
class GlobalValue;
class MCInstrInfo;
class MCRegisterClass;
class MCRegisterInfo;
class MCSubtargetInfo;
class StringRef;
class Triple;
class raw_ostream;

namespace AMDGPU {

struct AMDGPUMCKernelCodeT;
struct IsaVersion;

/// Generic target versions emitted by this version of LLVM.
///
/// These numbers are incremented every time a codegen breaking change occurs
/// within a generic family.
namespace GenericVersion {
static constexpr unsigned GFX9 = 1;
static constexpr unsigned GFX10_1 = 1;
static constexpr unsigned GFX10_3 = 1;
static constexpr unsigned GFX11 = 1;
static constexpr unsigned GFX12 = 1;
} // namespace GenericVersion

enum { AMDHSA_COV4 = 4, AMDHSA_COV5 = 5, AMDHSA_COV6 = 6 };

/// \returns True if \p STI is AMDHSA.
bool isHsaAbi(const MCSubtargetInfo &STI);

/// \returns Code object version from the IR module flag.
unsigned getAMDHSACodeObjectVersion(const Module &M);

/// \returns Code object version from ELF's e_ident[EI_ABIVERSION].
unsigned getAMDHSACodeObjectVersion(unsigned ABIVersion);

/// \returns The default HSA code object version. This should only be used when
/// we lack a more accurate CodeObjectVersion value (e.g. from the IR module
/// flag or a .amdhsa_code_object_version directive)
unsigned getDefaultAMDHSACodeObjectVersion();

/// \returns ABIVersion suitable for use in ELF's e_ident[EI_ABIVERSION]. \param
/// CodeObjectVersion is a value returned by getAMDHSACodeObjectVersion().
uint8_t getELFABIVersion(const Triple &OS, unsigned CodeObjectVersion);

/// \returns The offset of the multigrid_sync_arg argument from implicitarg_ptr
unsigned getMultigridSyncArgImplicitArgPosition(unsigned COV);

/// \returns The offset of the hostcall pointer argument from implicitarg_ptr
unsigned getHostcallImplicitArgPosition(unsigned COV);

unsigned getDefaultQueueImplicitArgPosition(unsigned COV);
unsigned getCompletionActionImplicitArgPosition(unsigned COV);

struct GcnBufferFormatInfo {
  unsigned Format;
  unsigned BitsPerComp;
  unsigned NumComponents;
  unsigned NumFormat;
  unsigned DataFormat;
};

struct MAIInstInfo {
  uint16_t Opcode;
  bool is_dgemm;
  bool is_gfx940_xdl;
};

#define GET_MIMGBaseOpcode_DECL
#define GET_MIMGDim_DECL
#define GET_MIMGEncoding_DECL
#define GET_MIMGLZMapping_DECL
#define GET_MIMGMIPMapping_DECL
#define GET_MIMGBiASMapping_DECL
#define GET_MAIInstInfoTable_DECL
#include "AMDGPUGenSearchableTables.inc"

namespace IsaInfo {

enum {
  // The closed Vulkan driver sets 96, which limits the wave count to 8 but
  // doesn't spill SGPRs as much as when 80 is set.
  FIXED_NUM_SGPRS_FOR_INIT_BUG = 96,
  TRAP_NUM_SGPRS = 16
};

enum class TargetIDSetting {
  Unsupported,
  Any,
  Off,
  On
};

class AMDGPUTargetID {
private:
  const MCSubtargetInfo &STI;
  TargetIDSetting XnackSetting;
  TargetIDSetting SramEccSetting;

public:
  explicit AMDGPUTargetID(const MCSubtargetInfo &STI);
  ~AMDGPUTargetID() = default;

  /// \return True if the current xnack setting is not "Unsupported".
  bool isXnackSupported() const {
    return XnackSetting != TargetIDSetting::Unsupported;
  }

  /// \returns True if the current xnack setting is "On" or "Any".
  bool isXnackOnOrAny() const {
    return XnackSetting == TargetIDSetting::On ||
        XnackSetting == TargetIDSetting::Any;
  }

  /// \returns True if current xnack setting is "On" or "Off",
  /// false otherwise.
  bool isXnackOnOrOff() const {
    return getXnackSetting() == TargetIDSetting::On ||
        getXnackSetting() == TargetIDSetting::Off;
  }

  /// \returns The current xnack TargetIDSetting, possible options are
  /// "Unsupported", "Any", "Off", and "On".
  TargetIDSetting getXnackSetting() const {
    return XnackSetting;
  }

  /// Sets xnack setting to \p NewXnackSetting.
  void setXnackSetting(TargetIDSetting NewXnackSetting) {
    XnackSetting = NewXnackSetting;
  }

  /// \return True if the current sramecc setting is not "Unsupported".
  bool isSramEccSupported() const {
    return SramEccSetting != TargetIDSetting::Unsupported;
  }

  /// \returns True if the current sramecc setting is "On" or "Any".
  bool isSramEccOnOrAny() const {
  return SramEccSetting == TargetIDSetting::On ||
      SramEccSetting == TargetIDSetting::Any;
  }

  /// \returns True if current sramecc setting is "On" or "Off",
  /// false otherwise.
  bool isSramEccOnOrOff() const {
    return getSramEccSetting() == TargetIDSetting::On ||
        getSramEccSetting() == TargetIDSetting::Off;
  }

  /// \returns The current sramecc TargetIDSetting, possible options are
  /// "Unsupported", "Any", "Off", and "On".
  TargetIDSetting getSramEccSetting() const {
    return SramEccSetting;
  }

  /// Sets sramecc setting to \p NewSramEccSetting.
  void setSramEccSetting(TargetIDSetting NewSramEccSetting) {
    SramEccSetting = NewSramEccSetting;
  }

  void setTargetIDFromFeaturesString(StringRef FS);
  void setTargetIDFromTargetIDStream(StringRef TargetID);

  /// \returns String representation of an object.
  std::string toString() const;
};

/// \returns Wavefront size for given subtarget \p STI.
unsigned getWavefrontSize(const MCSubtargetInfo *STI);

/// \returns Local memory size in bytes for given subtarget \p STI.
unsigned getLocalMemorySize(const MCSubtargetInfo *STI);

/// \returns Maximum addressable local memory size in bytes for given subtarget
/// \p STI.
unsigned getAddressableLocalMemorySize(const MCSubtargetInfo *STI);

/// \returns Number of execution units per compute unit for given subtarget \p
/// STI.
unsigned getEUsPerCU(const MCSubtargetInfo *STI);

/// \returns Maximum number of work groups per compute unit for given subtarget
/// \p STI and limited by given \p FlatWorkGroupSize.
unsigned getMaxWorkGroupsPerCU(const MCSubtargetInfo *STI,
                               unsigned FlatWorkGroupSize);

/// \returns Minimum number of waves per execution unit for given subtarget \p
/// STI.
unsigned getMinWavesPerEU(const MCSubtargetInfo *STI);

/// \returns Maximum number of waves per execution unit for given subtarget \p
/// STI without any kind of limitation.
unsigned getMaxWavesPerEU(const MCSubtargetInfo *STI);

/// \returns Number of waves per execution unit required to support the given \p
/// FlatWorkGroupSize.
unsigned getWavesPerEUForWorkGroup(const MCSubtargetInfo *STI,
                                   unsigned FlatWorkGroupSize);

/// \returns Minimum flat work group size for given subtarget \p STI.
unsigned getMinFlatWorkGroupSize(const MCSubtargetInfo *STI);

/// \returns Maximum flat work group size for given subtarget \p STI.
unsigned getMaxFlatWorkGroupSize(const MCSubtargetInfo *STI);

/// \returns Number of waves per work group for given subtarget \p STI and
/// \p FlatWorkGroupSize.
unsigned getWavesPerWorkGroup(const MCSubtargetInfo *STI,
                              unsigned FlatWorkGroupSize);

/// \returns SGPR allocation granularity for given subtarget \p STI.
unsigned getSGPRAllocGranule(const MCSubtargetInfo *STI);

/// \returns SGPR encoding granularity for given subtarget \p STI.
unsigned getSGPREncodingGranule(const MCSubtargetInfo *STI);

/// \returns Total number of SGPRs for given subtarget \p STI.
unsigned getTotalNumSGPRs(const MCSubtargetInfo *STI);

/// \returns Addressable number of SGPRs for given subtarget \p STI.
unsigned getAddressableNumSGPRs(const MCSubtargetInfo *STI);

/// \returns Minimum number of SGPRs that meets the given number of waves per
/// execution unit requirement for given subtarget \p STI.
unsigned getMinNumSGPRs(const MCSubtargetInfo *STI, unsigned WavesPerEU);

/// \returns Maximum number of SGPRs that meets the given number of waves per
/// execution unit requirement for given subtarget \p STI.
unsigned getMaxNumSGPRs(const MCSubtargetInfo *STI, unsigned WavesPerEU,
                        bool Addressable);

/// \returns Number of extra SGPRs implicitly required by given subtarget \p
/// STI when the given special registers are used.
unsigned getNumExtraSGPRs(const MCSubtargetInfo *STI, bool VCCUsed,
                          bool FlatScrUsed, bool XNACKUsed);

/// \returns Number of extra SGPRs implicitly required by given subtarget \p
/// STI when the given special registers are used. XNACK is inferred from
/// \p STI.
unsigned getNumExtraSGPRs(const MCSubtargetInfo *STI, bool VCCUsed,
                          bool FlatScrUsed);

/// \returns Number of SGPR blocks needed for given subtarget \p STI when
/// \p NumSGPRs are used. \p NumSGPRs should already include any special
/// register counts.
unsigned getNumSGPRBlocks(const MCSubtargetInfo *STI, unsigned NumSGPRs);

/// \returns VGPR allocation granularity for given subtarget \p STI.
///
/// For subtargets which support it, \p EnableWavefrontSize32 should match
/// the ENABLE_WAVEFRONT_SIZE32 kernel descriptor field.
unsigned
getVGPRAllocGranule(const MCSubtargetInfo *STI,
                    std::optional<bool> EnableWavefrontSize32 = std::nullopt);

/// \returns VGPR encoding granularity for given subtarget \p STI.
///
/// For subtargets which support it, \p EnableWavefrontSize32 should match
/// the ENABLE_WAVEFRONT_SIZE32 kernel descriptor field.
unsigned getVGPREncodingGranule(
    const MCSubtargetInfo *STI,
    std::optional<bool> EnableWavefrontSize32 = std::nullopt);

/// \returns Total number of VGPRs for given subtarget \p STI.
unsigned getTotalNumVGPRs(const MCSubtargetInfo *STI);

/// \returns Addressable number of architectural VGPRs for a given subtarget \p
/// STI.
unsigned getAddressableNumArchVGPRs(const MCSubtargetInfo *STI);

/// \returns Addressable number of VGPRs for given subtarget \p STI.
unsigned getAddressableNumVGPRs(const MCSubtargetInfo *STI);

/// \returns Minimum number of VGPRs that meets given number of waves per
/// execution unit requirement for given subtarget \p STI.
unsigned getMinNumVGPRs(const MCSubtargetInfo *STI, unsigned WavesPerEU);

/// \returns Maximum number of VGPRs that meets given number of waves per
/// execution unit requirement for given subtarget \p STI.
unsigned getMaxNumVGPRs(const MCSubtargetInfo *STI, unsigned WavesPerEU);

/// \returns Number of waves reachable for a given \p NumVGPRs usage for given
/// subtarget \p STI.
unsigned getNumWavesPerEUWithNumVGPRs(const MCSubtargetInfo *STI,
                                      unsigned NumVGPRs);

/// \returns Number of waves reachable for a given \p NumVGPRs usage, \p Granule
/// size, \p MaxWaves possible, and \p TotalNumVGPRs available.
unsigned getNumWavesPerEUWithNumVGPRs(unsigned NumVGPRs, unsigned Granule,
                                      unsigned MaxWaves,
                                      unsigned TotalNumVGPRs);

/// \returns Occupancy for a given \p SGPRs usage, \p MaxWaves possible, and \p
/// Gen.
unsigned getOccupancyWithNumSGPRs(unsigned SGPRs, unsigned MaxWaves,
                                  AMDGPUSubtarget::Generation Gen);

/// \returns Number of VGPR blocks needed for given subtarget \p STI when
/// \p NumVGPRs are used. We actually return the number of blocks -1, since
/// that's what we encode.
///
/// For subtargets which support it, \p EnableWavefrontSize32 should match the
/// ENABLE_WAVEFRONT_SIZE32 kernel descriptor field.
unsigned getEncodedNumVGPRBlocks(
    const MCSubtargetInfo *STI, unsigned NumVGPRs,
    std::optional<bool> EnableWavefrontSize32 = std::nullopt);

/// \returns Number of VGPR blocks that need to be allocated for the given
/// subtarget \p STI when \p NumVGPRs are used.
unsigned getAllocatedNumVGPRBlocks(
    const MCSubtargetInfo *STI, unsigned NumVGPRs,
    std::optional<bool> EnableWavefrontSize32 = std::nullopt);

} // end namespace IsaInfo

// Represents a field in an encoded value.
template <unsigned HighBit, unsigned LowBit, unsigned D = 0>
struct EncodingField {
  static_assert(HighBit >= LowBit, "Invalid bit range!");
  static constexpr unsigned Offset = LowBit;
  static constexpr unsigned Width = HighBit - LowBit + 1;

  using ValueType = unsigned;
  static constexpr ValueType Default = D;

  ValueType Value;
  constexpr EncodingField(ValueType Value) : Value(Value) {}

  constexpr uint64_t encode() const { return Value; }
  static ValueType decode(uint64_t Encoded) { return Encoded; }
};

// Represents a single bit in an encoded value.
template <unsigned Bit, unsigned D = 0>
using EncodingBit = EncodingField<Bit, Bit, D>;

// A helper for encoding and decoding multiple fields.
template <typename... Fields> struct EncodingFields {
  static constexpr uint64_t encode(Fields... Values) {
    return ((Values.encode() << Values.Offset) | ...);
  }

  static std::tuple<typename Fields::ValueType...> decode(uint64_t Encoded) {
    return {Fields::decode((Encoded >> Fields::Offset) &
                           maxUIntN(Fields::Width))...};
  }
};

LLVM_READONLY
int16_t getNamedOperandIdx(uint16_t Opcode, uint16_t NamedIdx);

LLVM_READONLY
inline bool hasNamedOperand(uint64_t Opcode, uint64_t NamedIdx) {
  return getNamedOperandIdx(Opcode, NamedIdx) != -1;
}

LLVM_READONLY
int getSOPPWithRelaxation(uint16_t Opcode);

struct MIMGBaseOpcodeInfo {
  MIMGBaseOpcode BaseOpcode;
  bool Store;
  bool Atomic;
  bool AtomicX2;
  bool Sampler;
  bool Gather4;

  uint8_t NumExtraArgs;
  bool Gradients;
  bool G16;
  bool Coordinates;
  bool LodOrClampOrMip;
  bool HasD16;
  bool MSAA;
  bool BVH;
  bool A16;
  bool NoReturn;
};

LLVM_READONLY
const MIMGBaseOpcodeInfo *getMIMGBaseOpcode(unsigned Opc);

LLVM_READONLY
const MIMGBaseOpcodeInfo *getMIMGBaseOpcodeInfo(unsigned BaseOpcode);

struct MIMGDimInfo {
  MIMGDim Dim;
  uint8_t NumCoords;
  uint8_t NumGradients;
  bool MSAA;
  bool DA;
  uint8_t Encoding;
  const char *AsmSuffix;
};

LLVM_READONLY
const MIMGDimInfo *getMIMGDimInfo(unsigned DimEnum);

LLVM_READONLY
const MIMGDimInfo *getMIMGDimInfoByEncoding(uint8_t DimEnc);

LLVM_READONLY
const MIMGDimInfo *getMIMGDimInfoByAsmSuffix(StringRef AsmSuffix);

struct MIMGLZMappingInfo {
  MIMGBaseOpcode L;
  MIMGBaseOpcode LZ;
};

struct MIMGMIPMappingInfo {
  MIMGBaseOpcode MIP;
  MIMGBaseOpcode NONMIP;
};

struct MIMGBiasMappingInfo {
  MIMGBaseOpcode Bias;
  MIMGBaseOpcode NoBias;
};

struct MIMGOffsetMappingInfo {
  MIMGBaseOpcode Offset;
  MIMGBaseOpcode NoOffset;
};

struct MIMGG16MappingInfo {
  MIMGBaseOpcode G;
  MIMGBaseOpcode G16;
};

LLVM_READONLY
const MIMGLZMappingInfo *getMIMGLZMappingInfo(unsigned L);

struct WMMAOpcodeMappingInfo {
  unsigned Opcode2Addr;
  unsigned Opcode3Addr;
};

LLVM_READONLY
const MIMGMIPMappingInfo *getMIMGMIPMappingInfo(unsigned MIP);

LLVM_READONLY
const MIMGBiasMappingInfo *getMIMGBiasMappingInfo(unsigned Bias);

LLVM_READONLY
const MIMGOffsetMappingInfo *getMIMGOffsetMappingInfo(unsigned Offset);

LLVM_READONLY
const MIMGG16MappingInfo *getMIMGG16MappingInfo(unsigned G);

LLVM_READONLY
int getMIMGOpcode(unsigned BaseOpcode, unsigned MIMGEncoding,
                  unsigned VDataDwords, unsigned VAddrDwords);

LLVM_READONLY
int getMaskedMIMGOp(unsigned Opc, unsigned NewChannels);

LLVM_READONLY
unsigned getAddrSizeMIMGOp(const MIMGBaseOpcodeInfo *BaseOpcode,
                           const MIMGDimInfo *Dim, bool IsA16,
                           bool IsG16Supported);

struct MIMGInfo {
  uint16_t Opcode;
  uint16_t BaseOpcode;
  uint8_t MIMGEncoding;
  uint8_t VDataDwords;
  uint8_t VAddrDwords;
  uint8_t VAddrOperands;
};

LLVM_READONLY
const MIMGInfo *getMIMGInfo(unsigned Opc);

LLVM_READONLY
int getMTBUFBaseOpcode(unsigned Opc);

LLVM_READONLY
int getMTBUFOpcode(unsigned BaseOpc, unsigned Elements);

LLVM_READONLY
int getMTBUFElements(unsigned Opc);

LLVM_READONLY
bool getMTBUFHasVAddr(unsigned Opc);

LLVM_READONLY
bool getMTBUFHasSrsrc(unsigned Opc);

LLVM_READONLY
bool getMTBUFHasSoffset(unsigned Opc);

LLVM_READONLY
int getMUBUFBaseOpcode(unsigned Opc);

LLVM_READONLY
int getMUBUFOpcode(unsigned BaseOpc, unsigned Elements);

LLVM_READONLY
int getMUBUFElements(unsigned Opc);

LLVM_READONLY
bool getMUBUFHasVAddr(unsigned Opc);

LLVM_READONLY
bool getMUBUFHasSrsrc(unsigned Opc);

LLVM_READONLY
bool getMUBUFHasSoffset(unsigned Opc);

LLVM_READONLY
bool getMUBUFIsBufferInv(unsigned Opc);

LLVM_READONLY
bool getMUBUFTfe(unsigned Opc);

LLVM_READONLY
bool getSMEMIsBuffer(unsigned Opc);

LLVM_READONLY
bool getVOP1IsSingle(unsigned Opc);

LLVM_READONLY
bool getVOP2IsSingle(unsigned Opc);

LLVM_READONLY
bool getVOP3IsSingle(unsigned Opc);

LLVM_READONLY
bool isVOPC64DPP(unsigned Opc);

LLVM_READONLY
bool isVOPCAsmOnly(unsigned Opc);

/// Returns true if MAI operation is a double precision GEMM.
LLVM_READONLY
bool getMAIIsDGEMM(unsigned Opc);

LLVM_READONLY
bool getMAIIsGFX940XDL(unsigned Opc);

struct CanBeVOPD {
  bool X;
  bool Y;
};

/// \returns SIEncodingFamily used for VOPD encoding on a \p ST.
LLVM_READONLY
unsigned getVOPDEncodingFamily(const MCSubtargetInfo &ST);

LLVM_READONLY
CanBeVOPD getCanBeVOPD(unsigned Opc);

LLVM_READONLY
const GcnBufferFormatInfo *getGcnBufferFormatInfo(uint8_t BitsPerComp,
                                                  uint8_t NumComponents,
                                                  uint8_t NumFormat,
                                                  const MCSubtargetInfo &STI);
LLVM_READONLY
const GcnBufferFormatInfo *getGcnBufferFormatInfo(uint8_t Format,
                                                  const MCSubtargetInfo &STI);

LLVM_READONLY
int getMCOpcode(uint16_t Opcode, unsigned Gen);

LLVM_READONLY
unsigned getVOPDOpcode(unsigned Opc);

LLVM_READONLY
int getVOPDFull(unsigned OpX, unsigned OpY, unsigned EncodingFamily);

LLVM_READONLY
bool isVOPD(unsigned Opc);

LLVM_READNONE
bool isMAC(unsigned Opc);

LLVM_READNONE
bool isPermlane16(unsigned Opc);

LLVM_READNONE
bool isGenericAtomic(unsigned Opc);

LLVM_READNONE
bool isCvt_F32_Fp8_Bf8_e64(unsigned Opc);

namespace VOPD {

enum Component : unsigned {
  DST = 0,
  SRC0,
  SRC1,
  SRC2,

  DST_NUM = 1,
  MAX_SRC_NUM = 3,
  MAX_OPR_NUM = DST_NUM + MAX_SRC_NUM
};

// LSB mask for VGPR banks per VOPD component operand.
// 4 banks result in a mask 3, setting 2 lower bits.
constexpr unsigned VOPD_VGPR_BANK_MASKS[] = {1, 3, 3, 1};

enum ComponentIndex : unsigned { X = 0, Y = 1 };
constexpr unsigned COMPONENTS[] = {ComponentIndex::X, ComponentIndex::Y};
constexpr unsigned COMPONENTS_NUM = 2;

// Properties of VOPD components.
class ComponentProps {
private:
  unsigned SrcOperandsNum = 0;
  unsigned MandatoryLiteralIdx = ~0u;
  bool HasSrc2Acc = false;

public:
  ComponentProps() = default;
  ComponentProps(const MCInstrDesc &OpDesc);

  // Return the total number of src operands this component has.
  unsigned getCompSrcOperandsNum() const { return SrcOperandsNum; }

  // Return the number of src operands of this component visible to the parser.
  unsigned getCompParsedSrcOperandsNum() const {
    return SrcOperandsNum - HasSrc2Acc;
  }

  // Return true iif this component has a mandatory literal.
  bool hasMandatoryLiteral() const { return MandatoryLiteralIdx != ~0u; }

  // If this component has a mandatory literal, return component operand
  // index of this literal (i.e. either Component::SRC1 or Component::SRC2).
  unsigned getMandatoryLiteralCompOperandIndex() const {
    assert(hasMandatoryLiteral());
    return MandatoryLiteralIdx;
  }

  // Return true iif this component has operand
  // with component index CompSrcIdx and this operand may be a register.
  bool hasRegSrcOperand(unsigned CompSrcIdx) const {
    assert(CompSrcIdx < Component::MAX_SRC_NUM);
    return SrcOperandsNum > CompSrcIdx && !hasMandatoryLiteralAt(CompSrcIdx);
  }

  // Return true iif this component has tied src2.
  bool hasSrc2Acc() const { return HasSrc2Acc; }

private:
  bool hasMandatoryLiteralAt(unsigned CompSrcIdx) const {
    assert(CompSrcIdx < Component::MAX_SRC_NUM);
    return MandatoryLiteralIdx == Component::DST_NUM + CompSrcIdx;
  }
};

enum ComponentKind : unsigned {
  SINGLE = 0,  // A single VOP1 or VOP2 instruction which may be used in VOPD.
  COMPONENT_X, // A VOPD instruction, X component.
  COMPONENT_Y, // A VOPD instruction, Y component.
  MAX = COMPONENT_Y
};

// Interface functions of this class map VOPD component operand indices
// to indices of operands in MachineInstr/MCInst or parsed operands array.
//
// Note that this class operates with 3 kinds of indices:
// - VOPD component operand indices (Component::DST, Component::SRC0, etc.);
// - MC operand indices (they refer operands in a MachineInstr/MCInst);
// - parsed operand indices (they refer operands in parsed operands array).
//
// For SINGLE components mapping between these indices is trivial.
// But things get more complicated for COMPONENT_X and
// COMPONENT_Y because these components share the same
// MachineInstr/MCInst and the same parsed operands array.
// Below is an example of component operand to parsed operand
// mapping for the following instruction:
//
//   v_dual_add_f32 v255, v4, v5 :: v_dual_mov_b32 v6, v1
//
//                          PARSED        COMPONENT         PARSED
// COMPONENT               OPERANDS     OPERAND INDEX    OPERAND INDEX
// -------------------------------------------------------------------
//                     "v_dual_add_f32"                        0
// v_dual_add_f32            v255          0 (DST)    -->      1
//                           v4            1 (SRC0)   -->      2
//                           v5            2 (SRC1)   -->      3
//                          "::"                               4
//                     "v_dual_mov_b32"                        5
// v_dual_mov_b32            v6            0 (DST)    -->      6
//                           v1            1 (SRC0)   -->      7
// -------------------------------------------------------------------
//
class ComponentLayout {
private:
  // Regular MachineInstr/MCInst operands are ordered as follows:
  //   dst, src0 [, other src operands]
  // VOPD MachineInstr/MCInst operands are ordered as follows:
  //   dstX, dstY, src0X [, other OpX operands], src0Y [, other OpY operands]
  // Each ComponentKind has operand indices defined below.
  static constexpr unsigned MC_DST_IDX[] = {0, 0, 1};
  static constexpr unsigned FIRST_MC_SRC_IDX[] = {1, 2, 2 /* + OpX.MCSrcNum */};

  // Parsed operands of regular instructions are ordered as follows:
  //   Mnemo dst src0 [vsrc1 ...]
  // Parsed VOPD operands are ordered as follows:
  //   OpXMnemo dstX src0X [vsrc1X|imm vsrc1X|vsrc1X imm] '::'
  //   OpYMnemo dstY src0Y [vsrc1Y|imm vsrc1Y|vsrc1Y imm]
  // Each ComponentKind has operand indices defined below.
  static constexpr unsigned PARSED_DST_IDX[] = {1, 1,
                                                4 /* + OpX.ParsedSrcNum */};
  static constexpr unsigned FIRST_PARSED_SRC_IDX[] = {
      2, 2, 5 /* + OpX.ParsedSrcNum */};

private:
  const ComponentKind Kind;
  const ComponentProps PrevComp;

public:
  // Create layout for COMPONENT_X or SINGLE component.
  ComponentLayout(ComponentKind Kind) : Kind(Kind) {
    assert(Kind == ComponentKind::SINGLE || Kind == ComponentKind::COMPONENT_X);
  }

  // Create layout for COMPONENT_Y which depends on COMPONENT_X layout.
  ComponentLayout(const ComponentProps &OpXProps)
      : Kind(ComponentKind::COMPONENT_Y), PrevComp(OpXProps) {}

public:
  // Return the index of dst operand in MCInst operands.
  unsigned getIndexOfDstInMCOperands() const { return MC_DST_IDX[Kind]; }

  // Return the index of the specified src operand in MCInst operands.
  unsigned getIndexOfSrcInMCOperands(unsigned CompSrcIdx) const {
    assert(CompSrcIdx < Component::MAX_SRC_NUM);
    return FIRST_MC_SRC_IDX[Kind] + getPrevCompSrcNum() + CompSrcIdx;
  }

  // Return the index of dst operand in the parsed operands array.
  unsigned getIndexOfDstInParsedOperands() const {
    return PARSED_DST_IDX[Kind] + getPrevCompParsedSrcNum();
  }

  // Return the index of the specified src operand in the parsed operands array.
  unsigned getIndexOfSrcInParsedOperands(unsigned CompSrcIdx) const {
    assert(CompSrcIdx < Component::MAX_SRC_NUM);
    return FIRST_PARSED_SRC_IDX[Kind] + getPrevCompParsedSrcNum() + CompSrcIdx;
  }

private:
  unsigned getPrevCompSrcNum() const {
    return PrevComp.getCompSrcOperandsNum();
  }
  unsigned getPrevCompParsedSrcNum() const {
    return PrevComp.getCompParsedSrcOperandsNum();
  }
};

// Layout and properties of VOPD components.
class ComponentInfo : public ComponentLayout, public ComponentProps {
public:
  // Create ComponentInfo for COMPONENT_X or SINGLE component.
  ComponentInfo(const MCInstrDesc &OpDesc,
                ComponentKind Kind = ComponentKind::SINGLE)
      : ComponentLayout(Kind), ComponentProps(OpDesc) {}

  // Create ComponentInfo for COMPONENT_Y which depends on COMPONENT_X layout.
  ComponentInfo(const MCInstrDesc &OpDesc, const ComponentProps &OpXProps)
      : ComponentLayout(OpXProps), ComponentProps(OpDesc) {}

  // Map component operand index to parsed operand index.
  // Return 0 if the specified operand does not exist.
  unsigned getIndexInParsedOperands(unsigned CompOprIdx) const;
};

// Properties of VOPD instructions.
class InstInfo {
private:
  const ComponentInfo CompInfo[COMPONENTS_NUM];

public:
  using RegIndices = std::array<unsigned, Component::MAX_OPR_NUM>;

  InstInfo(const MCInstrDesc &OpX, const MCInstrDesc &OpY)
      : CompInfo{OpX, OpY} {}

  InstInfo(const ComponentInfo &OprInfoX, const ComponentInfo &OprInfoY)
      : CompInfo{OprInfoX, OprInfoY} {}

  const ComponentInfo &operator[](size_t ComponentIdx) const {
    assert(ComponentIdx < COMPONENTS_NUM);
    return CompInfo[ComponentIdx];
  }

  // Check VOPD operands constraints.
  // GetRegIdx(Component, MCOperandIdx) must return a VGPR register index
  // for the specified component and MC operand. The callback must return 0
  // if the operand is not a register or not a VGPR.
  // If \p SkipSrc is set to true then constraints for source operands are not
  // checked.
  bool hasInvalidOperand(std::function<unsigned(unsigned, unsigned)> GetRegIdx,
                         bool SkipSrc = false) const {
    return getInvalidCompOperandIndex(GetRegIdx, SkipSrc).has_value();
  }

  // Check VOPD operands constraints.
  // Return the index of an invalid component operand, if any.
  // If \p SkipSrc is set to true then constraints for source operands are not
  // checked.
  std::optional<unsigned> getInvalidCompOperandIndex(
      std::function<unsigned(unsigned, unsigned)> GetRegIdx,
      bool SkipSrc = false) const;

private:
  RegIndices
  getRegIndices(unsigned ComponentIdx,
                std::function<unsigned(unsigned, unsigned)> GetRegIdx) const;
};

} // namespace VOPD

LLVM_READONLY
std::pair<unsigned, unsigned> getVOPDComponents(unsigned VOPDOpcode);

LLVM_READONLY
// Get properties of 2 single VOP1/VOP2 instructions
// used as components to create a VOPD instruction.
VOPD::InstInfo getVOPDInstInfo(const MCInstrDesc &OpX, const MCInstrDesc &OpY);

LLVM_READONLY
// Get properties of VOPD X and Y components.
VOPD::InstInfo
getVOPDInstInfo(unsigned VOPDOpcode, const MCInstrInfo *InstrInfo);

LLVM_READONLY
bool isTrue16Inst(unsigned Opc);

LLVM_READONLY
bool isInvalidSingleUseConsumerInst(unsigned Opc);

LLVM_READONLY
bool isInvalidSingleUseProducerInst(unsigned Opc);

LLVM_READONLY
unsigned mapWMMA2AddrTo3AddrOpcode(unsigned Opc);

LLVM_READONLY
unsigned mapWMMA3AddrTo2AddrOpcode(unsigned Opc);

void initDefaultAMDKernelCodeT(AMDGPUMCKernelCodeT &Header,
                               const MCSubtargetInfo *STI);

bool isGroupSegment(const GlobalValue *GV);
bool isGlobalSegment(const GlobalValue *GV);
bool isReadOnlySegment(const GlobalValue *GV);

/// \returns True if constants should be emitted to .text section for given
/// target triple \p TT, false otherwise.
bool shouldEmitConstantsToTextSection(const Triple &TT);

/// \returns Integer value requested using \p F's \p Name attribute.
///
/// \returns \p Default if attribute is not present.
///
/// \returns \p Default and emits error if requested value cannot be converted
/// to integer.
int getIntegerAttribute(const Function &F, StringRef Name, int Default);

/// \returns A pair of integer values requested using \p F's \p Name attribute
/// in "first[,second]" format ("second" is optional unless \p OnlyFirstRequired
/// is false).
///
/// \returns \p Default if attribute is not present.
///
/// \returns \p Default and emits error if one of the requested values cannot be
/// converted to integer, or \p OnlyFirstRequired is false and "second" value is
/// not present.
std::pair<unsigned, unsigned>
getIntegerPairAttribute(const Function &F, StringRef Name,
                        std::pair<unsigned, unsigned> Default,
                        bool OnlyFirstRequired = false);

/// \returns Generate a vector of integer values requested using \p F's \p Name
/// attribute.
///
/// \returns true if exactly Size (>2) number of integers are found in the
/// attribute.
///
/// \returns false if any error occurs.
SmallVector<unsigned> getIntegerVecAttribute(const Function &F, StringRef Name,
                                             unsigned Size);

/// Represents the counter values to wait for in an s_waitcnt instruction.
///
/// Large values (including the maximum possible integer) can be used to
/// represent "don't care" waits.
struct Waitcnt {
  unsigned LoadCnt = ~0u; // Corresponds to Vmcnt prior to gfx12.
  unsigned ExpCnt = ~0u;
  unsigned DsCnt = ~0u;     // Corresponds to LGKMcnt prior to gfx12.
  unsigned StoreCnt = ~0u;  // Corresponds to VScnt on gfx10/gfx11.
  unsigned SampleCnt = ~0u; // gfx12+ only.
  unsigned BvhCnt = ~0u;    // gfx12+ only.
  unsigned KmCnt = ~0u;     // gfx12+ only.

  Waitcnt() = default;
  // Pre-gfx12 constructor.
  Waitcnt(unsigned VmCnt, unsigned ExpCnt, unsigned LgkmCnt, unsigned VsCnt)
      : LoadCnt(VmCnt), ExpCnt(ExpCnt), DsCnt(LgkmCnt), StoreCnt(VsCnt),
        SampleCnt(~0u), BvhCnt(~0u), KmCnt(~0u) {}

  // gfx12+ constructor.
  Waitcnt(unsigned LoadCnt, unsigned ExpCnt, unsigned DsCnt, unsigned StoreCnt,
          unsigned SampleCnt, unsigned BvhCnt, unsigned KmCnt)
      : LoadCnt(LoadCnt), ExpCnt(ExpCnt), DsCnt(DsCnt), StoreCnt(StoreCnt),
        SampleCnt(SampleCnt), BvhCnt(BvhCnt), KmCnt(KmCnt) {}

  bool hasWait() const { return StoreCnt != ~0u || hasWaitExceptStoreCnt(); }

  bool hasWaitExceptStoreCnt() const {
    return LoadCnt != ~0u || ExpCnt != ~0u || DsCnt != ~0u ||
           SampleCnt != ~0u || BvhCnt != ~0u || KmCnt != ~0u;
  }

  bool hasWaitStoreCnt() const { return StoreCnt != ~0u; }

  Waitcnt combined(const Waitcnt &Other) const {
    // Does the right thing provided self and Other are either both pre-gfx12
    // or both gfx12+.
    return Waitcnt(
        std::min(LoadCnt, Other.LoadCnt), std::min(ExpCnt, Other.ExpCnt),
        std::min(DsCnt, Other.DsCnt), std::min(StoreCnt, Other.StoreCnt),
        std::min(SampleCnt, Other.SampleCnt), std::min(BvhCnt, Other.BvhCnt),
        std::min(KmCnt, Other.KmCnt));
  }
};

// The following methods are only meaningful on targets that support
// S_WAITCNT.

/// \returns Vmcnt bit mask for given isa \p Version.
unsigned getVmcntBitMask(const IsaVersion &Version);

/// \returns Expcnt bit mask for given isa \p Version.
unsigned getExpcntBitMask(const IsaVersion &Version);

/// \returns Lgkmcnt bit mask for given isa \p Version.
unsigned getLgkmcntBitMask(const IsaVersion &Version);

/// \returns Waitcnt bit mask for given isa \p Version.
unsigned getWaitcntBitMask(const IsaVersion &Version);

/// \returns Decoded Vmcnt from given \p Waitcnt for given isa \p Version.
unsigned decodeVmcnt(const IsaVersion &Version, unsigned Waitcnt);

/// \returns Decoded Expcnt from given \p Waitcnt for given isa \p Version.
unsigned decodeExpcnt(const IsaVersion &Version, unsigned Waitcnt);

/// \returns Decoded Lgkmcnt from given \p Waitcnt for given isa \p Version.
unsigned decodeLgkmcnt(const IsaVersion &Version, unsigned Waitcnt);

/// Decodes Vmcnt, Expcnt and Lgkmcnt from given \p Waitcnt for given isa
/// \p Version, and writes decoded values into \p Vmcnt, \p Expcnt and
/// \p Lgkmcnt respectively. Should not be used on gfx12+, the instruction
/// which needs it is deprecated
///
/// \details \p Vmcnt, \p Expcnt and \p Lgkmcnt are decoded as follows:
///     \p Vmcnt = \p Waitcnt[3:0]        (pre-gfx9)
///     \p Vmcnt = \p Waitcnt[15:14,3:0]  (gfx9,10)
///     \p Vmcnt = \p Waitcnt[15:10]      (gfx11)
///     \p Expcnt = \p Waitcnt[6:4]       (pre-gfx11)
///     \p Expcnt = \p Waitcnt[2:0]       (gfx11)
///     \p Lgkmcnt = \p Waitcnt[11:8]     (pre-gfx10)
///     \p Lgkmcnt = \p Waitcnt[13:8]     (gfx10)
///     \p Lgkmcnt = \p Waitcnt[9:4]      (gfx11)
///
void decodeWaitcnt(const IsaVersion &Version, unsigned Waitcnt,
                   unsigned &Vmcnt, unsigned &Expcnt, unsigned &Lgkmcnt);

Waitcnt decodeWaitcnt(const IsaVersion &Version, unsigned Encoded);

/// \returns \p Waitcnt with encoded \p Vmcnt for given isa \p Version.
unsigned encodeVmcnt(const IsaVersion &Version, unsigned Waitcnt,
                     unsigned Vmcnt);

/// \returns \p Waitcnt with encoded \p Expcnt for given isa \p Version.
unsigned encodeExpcnt(const IsaVersion &Version, unsigned Waitcnt,
                      unsigned Expcnt);

/// \returns \p Waitcnt with encoded \p Lgkmcnt for given isa \p Version.
unsigned encodeLgkmcnt(const IsaVersion &Version, unsigned Waitcnt,
                       unsigned Lgkmcnt);

/// Encodes \p Vmcnt, \p Expcnt and \p Lgkmcnt into Waitcnt for given isa
/// \p Version. Should not be used on gfx12+, the instruction which needs
/// it is deprecated
///
/// \details \p Vmcnt, \p Expcnt and \p Lgkmcnt are encoded as follows:
///     Waitcnt[2:0]   = \p Expcnt      (gfx11+)
///     Waitcnt[3:0]   = \p Vmcnt       (pre-gfx9)
///     Waitcnt[3:0]   = \p Vmcnt[3:0]  (gfx9,10)
///     Waitcnt[6:4]   = \p Expcnt      (pre-gfx11)
///     Waitcnt[9:4]   = \p Lgkmcnt     (gfx11)
///     Waitcnt[11:8]  = \p Lgkmcnt     (pre-gfx10)
///     Waitcnt[13:8]  = \p Lgkmcnt     (gfx10)
///     Waitcnt[15:10] = \p Vmcnt       (gfx11)
///     Waitcnt[15:14] = \p Vmcnt[5:4]  (gfx9,10)
///
/// \returns Waitcnt with encoded \p Vmcnt, \p Expcnt and \p Lgkmcnt for given
/// isa \p Version.
///
unsigned encodeWaitcnt(const IsaVersion &Version,
                       unsigned Vmcnt, unsigned Expcnt, unsigned Lgkmcnt);

unsigned encodeWaitcnt(const IsaVersion &Version, const Waitcnt &Decoded);

// The following methods are only meaningful on targets that support
// S_WAIT_*CNT, introduced with gfx12.

/// \returns Loadcnt bit mask for given isa \p Version.
/// Returns 0 for versions that do not support LOADcnt
unsigned getLoadcntBitMask(const IsaVersion &Version);

/// \returns Samplecnt bit mask for given isa \p Version.
/// Returns 0 for versions that do not support SAMPLEcnt
unsigned getSamplecntBitMask(const IsaVersion &Version);

/// \returns Bvhcnt bit mask for given isa \p Version.
/// Returns 0 for versions that do not support BVHcnt
unsigned getBvhcntBitMask(const IsaVersion &Version);

/// \returns Dscnt bit mask for given isa \p Version.
/// Returns 0 for versions that do not support DScnt
unsigned getDscntBitMask(const IsaVersion &Version);

/// \returns Dscnt bit mask for given isa \p Version.
/// Returns 0 for versions that do not support KMcnt
unsigned getKmcntBitMask(const IsaVersion &Version);

/// \return STOREcnt or VScnt bit mask for given isa \p Version.
/// returns 0 for versions that do not support STOREcnt or VScnt.
/// STOREcnt and VScnt are the same counter, the name used
/// depends on the ISA version.
unsigned getStorecntBitMask(const IsaVersion &Version);

// The following are only meaningful on targets that support
// S_WAIT_LOADCNT_DSCNT and S_WAIT_STORECNT_DSCNT.

/// \returns Decoded Waitcnt structure from given \p LoadcntDscnt for given
/// isa \p Version.
Waitcnt decodeLoadcntDscnt(const IsaVersion &Version, unsigned LoadcntDscnt);

/// \returns Decoded Waitcnt structure from given \p StorecntDscnt for given
/// isa \p Version.
Waitcnt decodeStorecntDscnt(const IsaVersion &Version, unsigned StorecntDscnt);

/// \returns \p Loadcnt and \p Dscnt components of \p Decoded  encoded as an
/// immediate that can be used with S_WAIT_LOADCNT_DSCNT for given isa
/// \p Version.
unsigned encodeLoadcntDscnt(const IsaVersion &Version, const Waitcnt &Decoded);

/// \returns \p Storecnt and \p Dscnt components of \p Decoded  encoded as an
/// immediate that can be used with S_WAIT_STORECNT_DSCNT for given isa
/// \p Version.
unsigned encodeStorecntDscnt(const IsaVersion &Version, const Waitcnt &Decoded);

namespace Hwreg {

using HwregId = EncodingField<5, 0>;
using HwregOffset = EncodingField<10, 6>;

struct HwregSize : EncodingField<15, 11, 32> {
  using EncodingField::EncodingField;
  constexpr uint64_t encode() const { return Value - 1; }
  static ValueType decode(uint64_t Encoded) { return Encoded + 1; }
};

using HwregEncoding = EncodingFields<HwregId, HwregOffset, HwregSize>;

} // namespace Hwreg

namespace DepCtr {

int getDefaultDepCtrEncoding(const MCSubtargetInfo &STI);
int encodeDepCtr(const StringRef Name, int64_t Val, unsigned &UsedOprMask,
                 const MCSubtargetInfo &STI);
bool isSymbolicDepCtrEncoding(unsigned Code, bool &HasNonDefaultVal,
                              const MCSubtargetInfo &STI);
bool decodeDepCtr(unsigned Code, int &Id, StringRef &Name, unsigned &Val,
                  bool &IsDefault, const MCSubtargetInfo &STI);

/// \returns Decoded VaVdst from given immediate \p Encoded.
unsigned decodeFieldVaVdst(unsigned Encoded);

/// \returns Decoded VmVsrc from given immediate \p Encoded.
unsigned decodeFieldVmVsrc(unsigned Encoded);

/// \returns Decoded SaSdst from given immediate \p Encoded.
unsigned decodeFieldSaSdst(unsigned Encoded);

/// \returns \p VmVsrc as an encoded Depctr immediate.
unsigned encodeFieldVmVsrc(unsigned VmVsrc);

/// \returns \p Encoded combined with encoded \p VmVsrc.
unsigned encodeFieldVmVsrc(unsigned Encoded, unsigned VmVsrc);

/// \returns \p VaVdst as an encoded Depctr immediate.
unsigned encodeFieldVaVdst(unsigned VaVdst);

/// \returns \p Encoded combined with encoded \p VaVdst.
unsigned encodeFieldVaVdst(unsigned Encoded, unsigned VaVdst);

/// \returns \p SaSdst as an encoded Depctr immediate.
unsigned encodeFieldSaSdst(unsigned SaSdst);

/// \returns \p Encoded combined with encoded \p SaSdst.
unsigned encodeFieldSaSdst(unsigned Encoded, unsigned SaSdst);

} // namespace DepCtr

namespace Exp {

bool getTgtName(unsigned Id, StringRef &Name, int &Index);

LLVM_READONLY
unsigned getTgtId(const StringRef Name);

LLVM_READNONE
bool isSupportedTgtId(unsigned Id, const MCSubtargetInfo &STI);

} // namespace Exp

namespace MTBUFFormat {

LLVM_READNONE
int64_t encodeDfmtNfmt(unsigned Dfmt, unsigned Nfmt);

void decodeDfmtNfmt(unsigned Format, unsigned &Dfmt, unsigned &Nfmt);

int64_t getDfmt(const StringRef Name);

StringRef getDfmtName(unsigned Id);

int64_t getNfmt(const StringRef Name, const MCSubtargetInfo &STI);

StringRef getNfmtName(unsigned Id, const MCSubtargetInfo &STI);

bool isValidDfmtNfmt(unsigned Val, const MCSubtargetInfo &STI);

bool isValidNfmt(unsigned Val, const MCSubtargetInfo &STI);

int64_t getUnifiedFormat(const StringRef Name, const MCSubtargetInfo &STI);

StringRef getUnifiedFormatName(unsigned Id, const MCSubtargetInfo &STI);

bool isValidUnifiedFormat(unsigned Val, const MCSubtargetInfo &STI);

int64_t convertDfmtNfmt2Ufmt(unsigned Dfmt, unsigned Nfmt,
                             const MCSubtargetInfo &STI);

bool isValidFormatEncoding(unsigned Val, const MCSubtargetInfo &STI);

unsigned getDefaultFormatEncoding(const MCSubtargetInfo &STI);

} // namespace MTBUFFormat

namespace SendMsg {

LLVM_READNONE
bool isValidMsgId(int64_t MsgId, const MCSubtargetInfo &STI);

LLVM_READNONE
bool isValidMsgOp(int64_t MsgId, int64_t OpId, const MCSubtargetInfo &STI,
                  bool Strict = true);

LLVM_READNONE
bool isValidMsgStream(int64_t MsgId, int64_t OpId, int64_t StreamId,
                      const MCSubtargetInfo &STI, bool Strict = true);

LLVM_READNONE
bool msgRequiresOp(int64_t MsgId, const MCSubtargetInfo &STI);

LLVM_READNONE
bool msgSupportsStream(int64_t MsgId, int64_t OpId, const MCSubtargetInfo &STI);

void decodeMsg(unsigned Val, uint16_t &MsgId, uint16_t &OpId,
               uint16_t &StreamId, const MCSubtargetInfo &STI);

LLVM_READNONE
uint64_t encodeMsg(uint64_t MsgId,
                   uint64_t OpId,
                   uint64_t StreamId);

} // namespace SendMsg


unsigned getInitialPSInputAddr(const Function &F);

bool getHasColorExport(const Function &F);

bool getHasDepthExport(const Function &F);

LLVM_READNONE
bool isShader(CallingConv::ID CC);

LLVM_READNONE
bool isGraphics(CallingConv::ID CC);

LLVM_READNONE
bool isCompute(CallingConv::ID CC);

LLVM_READNONE
bool isEntryFunctionCC(CallingConv::ID CC);

// These functions are considered entrypoints into the current module, i.e. they
// are allowed to be called from outside the current module. This is different
// from isEntryFunctionCC, which is only true for functions that are entered by
// the hardware. Module entry points include all entry functions but also
// include functions that can be called from other functions inside or outside
// the current module. Module entry functions are allowed to allocate LDS.
LLVM_READNONE
bool isModuleEntryFunctionCC(CallingConv::ID CC);

LLVM_READNONE
bool isChainCC(CallingConv::ID CC);

bool isKernelCC(const Function *Func);

// FIXME: Remove this when calling conventions cleaned up
LLVM_READNONE
inline bool isKernel(CallingConv::ID CC) {
  switch (CC) {
  case CallingConv::AMDGPU_KERNEL:
  case CallingConv::SPIR_KERNEL:
    return true;
  default:
    return false;
  }
}

bool hasXNACK(const MCSubtargetInfo &STI);
bool hasSRAMECC(const MCSubtargetInfo &STI);
bool hasMIMG_R128(const MCSubtargetInfo &STI);
bool hasA16(const MCSubtargetInfo &STI);
bool hasG16(const MCSubtargetInfo &STI);
bool hasPackedD16(const MCSubtargetInfo &STI);
bool hasGDS(const MCSubtargetInfo &STI);
unsigned getNSAMaxSize(const MCSubtargetInfo &STI, bool HasSampler = false);
unsigned getMaxNumUserSGPRs(const MCSubtargetInfo &STI);

bool isSI(const MCSubtargetInfo &STI);
bool isCI(const MCSubtargetInfo &STI);
bool isVI(const MCSubtargetInfo &STI);
bool isGFX9(const MCSubtargetInfo &STI);
bool isGFX9_GFX10(const MCSubtargetInfo &STI);
bool isGFX9_GFX10_GFX11(const MCSubtargetInfo &STI);
bool isGFX8_GFX9_GFX10(const MCSubtargetInfo &STI);
bool isGFX8Plus(const MCSubtargetInfo &STI);
bool isGFX9Plus(const MCSubtargetInfo &STI);
bool isNotGFX9Plus(const MCSubtargetInfo &STI);
bool isGFX10(const MCSubtargetInfo &STI);
bool isGFX10_GFX11(const MCSubtargetInfo &STI);
bool isGFX10Plus(const MCSubtargetInfo &STI);
bool isNotGFX10Plus(const MCSubtargetInfo &STI);
bool isGFX10Before1030(const MCSubtargetInfo &STI);
bool isGFX11(const MCSubtargetInfo &STI);
bool isGFX11Plus(const MCSubtargetInfo &STI);
bool isGFX12(const MCSubtargetInfo &STI);
bool isGFX12Plus(const MCSubtargetInfo &STI);
bool isNotGFX12Plus(const MCSubtargetInfo &STI);
bool isNotGFX11Plus(const MCSubtargetInfo &STI);
bool isGCN3Encoding(const MCSubtargetInfo &STI);
bool isGFX10_AEncoding(const MCSubtargetInfo &STI);
bool isGFX10_BEncoding(const MCSubtargetInfo &STI);
bool hasGFX10_3Insts(const MCSubtargetInfo &STI);
bool isGFX10_3_GFX11(const MCSubtargetInfo &STI);
bool isGFX90A(const MCSubtargetInfo &STI);
bool isGFX940(const MCSubtargetInfo &STI);
bool hasArchitectedFlatScratch(const MCSubtargetInfo &STI);
bool hasMAIInsts(const MCSubtargetInfo &STI);
bool hasVOPD(const MCSubtargetInfo &STI);
bool hasDPPSrc1SGPR(const MCSubtargetInfo &STI);
int getTotalNumVGPRs(bool has90AInsts, int32_t ArgNumAGPR, int32_t ArgNumVGPR);
unsigned hasKernargPreload(const MCSubtargetInfo &STI);
bool hasSMRDSignedImmOffset(const MCSubtargetInfo &ST);

/// Is Reg - scalar register
bool isSGPR(unsigned Reg, const MCRegisterInfo* TRI);

/// \returns if \p Reg occupies the high 16-bits of a 32-bit register.
/// The bit indicating isHi is the LSB of the encoding.
bool isHi(unsigned Reg, const MCRegisterInfo &MRI);

/// If \p Reg is a pseudo reg, return the correct hardware register given
/// \p STI otherwise return \p Reg.
unsigned getMCReg(unsigned Reg, const MCSubtargetInfo &STI);

/// Convert hardware register \p Reg to a pseudo register
LLVM_READNONE
unsigned mc2PseudoReg(unsigned Reg);

LLVM_READNONE
bool isInlineValue(unsigned Reg);

/// Is this an AMDGPU specific source operand? These include registers,
/// inline constants, literals and mandatory literals (KImm).
bool isSISrcOperand(const MCInstrDesc &Desc, unsigned OpNo);

/// Is this a KImm operand?
bool isKImmOperand(const MCInstrDesc &Desc, unsigned OpNo);

/// Is this floating-point operand?
bool isSISrcFPOperand(const MCInstrDesc &Desc, unsigned OpNo);

/// Does this operand support only inlinable literals?
bool isSISrcInlinableOperand(const MCInstrDesc &Desc, unsigned OpNo);

/// Get the size in bits of a register from the register class \p RC.
unsigned getRegBitWidth(unsigned RCID);

/// Get the size in bits of a register from the register class \p RC.
unsigned getRegBitWidth(const MCRegisterClass &RC);

/// Get size of register operand
unsigned getRegOperandSize(const MCRegisterInfo *MRI, const MCInstrDesc &Desc,
                           unsigned OpNo);

LLVM_READNONE
inline unsigned getOperandSize(const MCOperandInfo &OpInfo) {
  switch (OpInfo.OperandType) {
  case AMDGPU::OPERAND_REG_IMM_INT32:
  case AMDGPU::OPERAND_REG_IMM_FP32:
  case AMDGPU::OPERAND_REG_IMM_FP32_DEFERRED:
  case AMDGPU::OPERAND_REG_INLINE_C_INT32:
  case AMDGPU::OPERAND_REG_INLINE_C_FP32:
  case AMDGPU::OPERAND_REG_INLINE_AC_INT32:
  case AMDGPU::OPERAND_REG_INLINE_AC_FP32:
  case AMDGPU::OPERAND_REG_IMM_V2INT32:
  case AMDGPU::OPERAND_REG_IMM_V2FP32:
  case AMDGPU::OPERAND_REG_INLINE_C_V2INT32:
  case AMDGPU::OPERAND_REG_INLINE_C_V2FP32:
  case AMDGPU::OPERAND_KIMM32:
  case AMDGPU::OPERAND_KIMM16: // mandatory literal is always size 4
  case AMDGPU::OPERAND_INLINE_SPLIT_BARRIER_INT32:
    return 4;

  case AMDGPU::OPERAND_REG_IMM_INT64:
  case AMDGPU::OPERAND_REG_IMM_FP64:
  case AMDGPU::OPERAND_REG_INLINE_C_INT64:
  case AMDGPU::OPERAND_REG_INLINE_C_FP64:
  case AMDGPU::OPERAND_REG_INLINE_AC_FP64:
    return 8;

  case AMDGPU::OPERAND_REG_IMM_INT16:
  case AMDGPU::OPERAND_REG_IMM_BF16:
  case AMDGPU::OPERAND_REG_IMM_FP16:
  case AMDGPU::OPERAND_REG_IMM_BF16_DEFERRED:
  case AMDGPU::OPERAND_REG_IMM_FP16_DEFERRED:
  case AMDGPU::OPERAND_REG_INLINE_C_INT16:
  case AMDGPU::OPERAND_REG_INLINE_C_BF16:
  case AMDGPU::OPERAND_REG_INLINE_C_FP16:
  case AMDGPU::OPERAND_REG_INLINE_C_V2INT16:
  case AMDGPU::OPERAND_REG_INLINE_C_V2BF16:
  case AMDGPU::OPERAND_REG_INLINE_C_V2FP16:
  case AMDGPU::OPERAND_REG_INLINE_AC_INT16:
  case AMDGPU::OPERAND_REG_INLINE_AC_BF16:
  case AMDGPU::OPERAND_REG_INLINE_AC_FP16:
  case AMDGPU::OPERAND_REG_INLINE_AC_V2INT16:
  case AMDGPU::OPERAND_REG_INLINE_AC_V2BF16:
  case AMDGPU::OPERAND_REG_INLINE_AC_V2FP16:
  case AMDGPU::OPERAND_REG_IMM_V2INT16:
  case AMDGPU::OPERAND_REG_IMM_V2BF16:
  case AMDGPU::OPERAND_REG_IMM_V2FP16:
    return 2;

  default:
    llvm_unreachable("unhandled operand type");
  }
}

LLVM_READNONE
inline unsigned getOperandSize(const MCInstrDesc &Desc, unsigned OpNo) {
  return getOperandSize(Desc.operands()[OpNo]);
}

/// Is this literal inlinable, and not one of the values intended for floating
/// point values.
LLVM_READNONE
inline bool isInlinableIntLiteral(int64_t Literal) {
  return Literal >= -16 && Literal <= 64;
}

/// Is this literal inlinable
LLVM_READNONE
bool isInlinableLiteral64(int64_t Literal, bool HasInv2Pi);

LLVM_READNONE
bool isInlinableLiteral32(int32_t Literal, bool HasInv2Pi);

LLVM_READNONE
bool isInlinableLiteralBF16(int16_t Literal, bool HasInv2Pi);

LLVM_READNONE
bool isInlinableLiteralFP16(int16_t Literal, bool HasInv2Pi);

LLVM_READNONE
bool isInlinableLiteralBF16(int16_t Literal, bool HasInv2Pi);

LLVM_READNONE
bool isInlinableLiteralI16(int32_t Literal, bool HasInv2Pi);

LLVM_READNONE
std::optional<unsigned> getInlineEncodingV2I16(uint32_t Literal);

LLVM_READNONE
std::optional<unsigned> getInlineEncodingV2BF16(uint32_t Literal);

LLVM_READNONE
std::optional<unsigned> getInlineEncodingV2F16(uint32_t Literal);

LLVM_READNONE
bool isInlinableLiteralV216(uint32_t Literal, uint8_t OpType);

LLVM_READNONE
bool isInlinableLiteralV2I16(uint32_t Literal);

LLVM_READNONE
bool isInlinableLiteralV2BF16(uint32_t Literal);

LLVM_READNONE
bool isInlinableLiteralV2F16(uint32_t Literal);

LLVM_READNONE
bool isValid32BitLiteral(uint64_t Val, bool IsFP64);

bool isArgPassedInSGPR(const Argument *Arg);

bool isArgPassedInSGPR(const CallBase *CB, unsigned ArgNo);

LLVM_READONLY
bool isLegalSMRDEncodedUnsignedOffset(const MCSubtargetInfo &ST,
                                      int64_t EncodedOffset);

LLVM_READONLY
bool isLegalSMRDEncodedSignedOffset(const MCSubtargetInfo &ST,
                                    int64_t EncodedOffset,
                                    bool IsBuffer);

/// Convert \p ByteOffset to dwords if the subtarget uses dword SMRD immediate
/// offsets.
uint64_t convertSMRDOffsetUnits(const MCSubtargetInfo &ST, uint64_t ByteOffset);

/// \returns The encoding that will be used for \p ByteOffset in the
/// SMRD offset field, or std::nullopt if it won't fit. On GFX9 and GFX10
/// S_LOAD instructions have a signed offset, on other subtargets it is
/// unsigned. S_BUFFER has an unsigned offset for all subtargets.
std::optional<int64_t> getSMRDEncodedOffset(const MCSubtargetInfo &ST,
                                            int64_t ByteOffset, bool IsBuffer,
                                            bool HasSOffset = false);

/// \return The encoding that can be used for a 32-bit literal offset in an SMRD
/// instruction. This is only useful on CI.s
std::optional<int64_t> getSMRDEncodedLiteralOffset32(const MCSubtargetInfo &ST,
                                                     int64_t ByteOffset);

/// For pre-GFX12 FLAT instructions the offset must be positive;
/// MSB is ignored and forced to zero.
///
/// \return The number of bits available for the signed offset field in flat
/// instructions. Note that some forms of the instruction disallow negative
/// offsets.
unsigned getNumFlatOffsetBits(const MCSubtargetInfo &ST);

/// \returns true if this offset is small enough to fit in the SMRD
/// offset field.  \p ByteOffset should be the offset in bytes and
/// not the encoded offset.
bool isLegalSMRDImmOffset(const MCSubtargetInfo &ST, int64_t ByteOffset);

LLVM_READNONE
inline bool isLegalDPALU_DPPControl(unsigned DC) {
  return DC >= DPP::ROW_NEWBCAST_FIRST && DC <= DPP::ROW_NEWBCAST_LAST;
}

/// \returns true if an instruction may have a 64-bit VGPR operand.
bool hasAny64BitVGPROperands(const MCInstrDesc &OpDesc);

/// \returns true if an instruction is a DP ALU DPP.
bool isDPALU_DPP(const MCInstrDesc &OpDesc);

/// \returns true if the intrinsic is divergent
bool isIntrinsicSourceOfDivergence(unsigned IntrID);

/// \returns true if the intrinsic is uniform
bool isIntrinsicAlwaysUniform(unsigned IntrID);

/// \returns lds block size in terms of dwords. \p
/// This is used to calculate the lds size encoded for PAL metadata 3.0+ which
/// must be defined in terms of bytes.
unsigned getLdsDwGranularity(const MCSubtargetInfo &ST);

} // end namespace AMDGPU

raw_ostream &operator<<(raw_ostream &OS,
                        const AMDGPU::IsaInfo::TargetIDSetting S);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_UTILS_AMDGPUBASEINFO_H
