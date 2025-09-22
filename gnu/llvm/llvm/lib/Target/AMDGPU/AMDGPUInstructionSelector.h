//===- AMDGPUInstructionSelector --------------------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares the targeting of the InstructionSelector class for
/// AMDGPU.
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_AMDGPUINSTRUCTIONSELECTOR_H
#define LLVM_LIB_TARGET_AMDGPU_AMDGPUINSTRUCTIONSELECTOR_H

#include "SIDefines.h"
#include "llvm/CodeGen/GlobalISel/InstructionSelector.h"
#include "llvm/IR/InstrTypes.h"

namespace {
#define GET_GLOBALISEL_PREDICATE_BITSET
#define AMDGPUSubtarget GCNSubtarget
#include "AMDGPUGenGlobalISel.inc"
#undef GET_GLOBALISEL_PREDICATE_BITSET
#undef AMDGPUSubtarget
}

namespace llvm {

namespace AMDGPU {
struct ImageDimIntrinsicInfo;
}

class AMDGPURegisterBankInfo;
class AMDGPUTargetMachine;
class BlockFrequencyInfo;
class ProfileSummaryInfo;
class GCNSubtarget;
class MachineInstr;
class MachineIRBuilder;
class MachineOperand;
class MachineRegisterInfo;
class RegisterBank;
class SIInstrInfo;
class SIRegisterInfo;
class TargetRegisterClass;

class AMDGPUInstructionSelector final : public InstructionSelector {
private:
  MachineRegisterInfo *MRI;
  const GCNSubtarget *Subtarget;

public:
  AMDGPUInstructionSelector(const GCNSubtarget &STI,
                            const AMDGPURegisterBankInfo &RBI,
                            const AMDGPUTargetMachine &TM);

  bool select(MachineInstr &I) override;
  static const char *getName();

  void setupMF(MachineFunction &MF, GISelKnownBits *KB,
               CodeGenCoverage *CoverageInfo, ProfileSummaryInfo *PSI,
               BlockFrequencyInfo *BFI) override;

private:
  struct GEPInfo {
    SmallVector<unsigned, 2> SgprParts;
    SmallVector<unsigned, 2> VgprParts;
    int64_t Imm = 0;
  };

  bool isSGPR(Register Reg) const;

  bool isInstrUniform(const MachineInstr &MI) const;
  bool isVCC(Register Reg, const MachineRegisterInfo &MRI) const;

  const RegisterBank *getArtifactRegBank(
    Register Reg, const MachineRegisterInfo &MRI,
    const TargetRegisterInfo &TRI) const;

  /// tblgen-erated 'select' implementation.
  bool selectImpl(MachineInstr &I, CodeGenCoverage &CoverageInfo) const;

  MachineOperand getSubOperand64(MachineOperand &MO,
                                 const TargetRegisterClass &SubRC,
                                 unsigned SubIdx) const;

  bool constrainCopyLikeIntrin(MachineInstr &MI, unsigned NewOpc) const;
  bool selectCOPY(MachineInstr &I) const;
  bool selectPHI(MachineInstr &I) const;
  bool selectG_TRUNC(MachineInstr &I) const;
  bool selectG_SZA_EXT(MachineInstr &I) const;
  bool selectG_FPEXT(MachineInstr &I) const;
  bool selectG_CONSTANT(MachineInstr &I) const;
  bool selectG_FNEG(MachineInstr &I) const;
  bool selectG_FABS(MachineInstr &I) const;
  bool selectG_AND_OR_XOR(MachineInstr &I) const;
  bool selectG_ADD_SUB(MachineInstr &I) const;
  bool selectG_UADDO_USUBO_UADDE_USUBE(MachineInstr &I) const;
  bool selectG_AMDGPU_MAD_64_32(MachineInstr &I) const;
  bool selectG_EXTRACT(MachineInstr &I) const;
  bool selectG_FMA_FMAD(MachineInstr &I) const;
  bool selectG_MERGE_VALUES(MachineInstr &I) const;
  bool selectG_UNMERGE_VALUES(MachineInstr &I) const;
  bool selectG_BUILD_VECTOR(MachineInstr &I) const;
  bool selectG_IMPLICIT_DEF(MachineInstr &I) const;
  bool selectG_INSERT(MachineInstr &I) const;
  bool selectG_SBFX_UBFX(MachineInstr &I) const;

  bool selectInterpP1F16(MachineInstr &MI) const;
  bool selectWritelane(MachineInstr &MI) const;
  bool selectDivScale(MachineInstr &MI) const;
  bool selectIntrinsicCmp(MachineInstr &MI) const;
  bool selectBallot(MachineInstr &I) const;
  bool selectRelocConstant(MachineInstr &I) const;
  bool selectGroupStaticSize(MachineInstr &I) const;
  bool selectReturnAddress(MachineInstr &I) const;
  bool selectG_INTRINSIC(MachineInstr &I) const;

  bool selectEndCfIntrinsic(MachineInstr &MI) const;
  bool selectDSOrderedIntrinsic(MachineInstr &MI, Intrinsic::ID IID) const;
  bool selectDSGWSIntrinsic(MachineInstr &MI, Intrinsic::ID IID) const;
  bool selectDSAppendConsume(MachineInstr &MI, bool IsAppend) const;
  bool selectSBarrier(MachineInstr &MI) const;
  bool selectDSBvhStackIntrinsic(MachineInstr &MI) const;

  bool selectImageIntrinsic(MachineInstr &MI,
                            const AMDGPU::ImageDimIntrinsicInfo *Intr) const;
  bool selectG_INTRINSIC_W_SIDE_EFFECTS(MachineInstr &I) const;
  int getS_CMPOpcode(CmpInst::Predicate P, unsigned Size) const;
  bool selectG_ICMP_or_FCMP(MachineInstr &I) const;
  bool hasVgprParts(ArrayRef<GEPInfo> AddrInfo) const;
  void getAddrModeInfo(const MachineInstr &Load, const MachineRegisterInfo &MRI,
                       SmallVectorImpl<GEPInfo> &AddrInfo) const;

  void initM0(MachineInstr &I) const;
  bool selectG_LOAD_STORE_ATOMICRMW(MachineInstr &I) const;
  bool selectG_SELECT(MachineInstr &I) const;
  bool selectG_BRCOND(MachineInstr &I) const;
  bool selectG_GLOBAL_VALUE(MachineInstr &I) const;
  bool selectG_PTRMASK(MachineInstr &I) const;
  bool selectG_EXTRACT_VECTOR_ELT(MachineInstr &I) const;
  bool selectG_INSERT_VECTOR_ELT(MachineInstr &I) const;
  bool selectBufferLoadLds(MachineInstr &MI) const;
  bool selectGlobalLoadLds(MachineInstr &MI) const;
  bool selectBVHIntrinsic(MachineInstr &I) const;
  bool selectSMFMACIntrin(MachineInstr &I) const;
  bool selectWaveAddress(MachineInstr &I) const;
  bool selectStackRestore(MachineInstr &MI) const;
  bool selectNamedBarrierInst(MachineInstr &I, Intrinsic::ID IID) const;
  bool selectSBarrierSignalIsfirst(MachineInstr &I, Intrinsic::ID IID) const;
  bool selectSBarrierLeave(MachineInstr &I) const;

  std::pair<Register, unsigned> selectVOP3ModsImpl(MachineOperand &Root,
                                                   bool IsCanonicalizing = true,
                                                   bool AllowAbs = true,
                                                   bool OpSel = false) const;

  Register copyToVGPRIfSrcFolded(Register Src, unsigned Mods,
                                 MachineOperand Root, MachineInstr *InsertPt,
                                 bool ForceVGPR = false) const;

  InstructionSelector::ComplexRendererFns
  selectVCSRC(MachineOperand &Root) const;

  InstructionSelector::ComplexRendererFns
  selectVSRC0(MachineOperand &Root) const;

  InstructionSelector::ComplexRendererFns
  selectVOP3Mods0(MachineOperand &Root) const;
  InstructionSelector::ComplexRendererFns
  selectVOP3BMods0(MachineOperand &Root) const;
  InstructionSelector::ComplexRendererFns
  selectVOP3OMods(MachineOperand &Root) const;
  InstructionSelector::ComplexRendererFns
  selectVOP3Mods(MachineOperand &Root) const;
  InstructionSelector::ComplexRendererFns
  selectVOP3ModsNonCanonicalizing(MachineOperand &Root) const;
  InstructionSelector::ComplexRendererFns
  selectVOP3BMods(MachineOperand &Root) const;

  ComplexRendererFns selectVOP3NoMods(MachineOperand &Root) const;

  std::pair<Register, unsigned>
  selectVOP3PModsImpl(Register Src, const MachineRegisterInfo &MRI,
                      bool IsDOT = false) const;

  InstructionSelector::ComplexRendererFns
  selectVOP3PMods(MachineOperand &Root) const;

  InstructionSelector::ComplexRendererFns
  selectVOP3PModsDOT(MachineOperand &Root) const;

  InstructionSelector::ComplexRendererFns
  selectVOP3PModsNeg(MachineOperand &Root) const;

  InstructionSelector::ComplexRendererFns
  selectWMMAOpSelVOP3PMods(MachineOperand &Root) const;

  InstructionSelector::ComplexRendererFns
  selectWMMAModsF32NegAbs(MachineOperand &Root) const;
  InstructionSelector::ComplexRendererFns
  selectWMMAModsF16Neg(MachineOperand &Root) const;
  InstructionSelector::ComplexRendererFns
  selectWMMAModsF16NegAbs(MachineOperand &Root) const;
  InstructionSelector::ComplexRendererFns
  selectWMMAVISrc(MachineOperand &Root) const;
  InstructionSelector::ComplexRendererFns
  selectSWMMACIndex8(MachineOperand &Root) const;
  InstructionSelector::ComplexRendererFns
  selectSWMMACIndex16(MachineOperand &Root) const;

  InstructionSelector::ComplexRendererFns
  selectVOP3OpSelMods(MachineOperand &Root) const;

  InstructionSelector::ComplexRendererFns
  selectVINTERPMods(MachineOperand &Root) const;
  InstructionSelector::ComplexRendererFns
  selectVINTERPModsHi(MachineOperand &Root) const;

  bool selectSmrdOffset(MachineOperand &Root, Register &Base, Register *SOffset,
                        int64_t *Offset) const;
  InstructionSelector::ComplexRendererFns
  selectSmrdImm(MachineOperand &Root) const;
  InstructionSelector::ComplexRendererFns
  selectSmrdImm32(MachineOperand &Root) const;
  InstructionSelector::ComplexRendererFns
  selectSmrdSgpr(MachineOperand &Root) const;
  InstructionSelector::ComplexRendererFns
  selectSmrdSgprImm(MachineOperand &Root) const;

  std::pair<Register, int> selectFlatOffsetImpl(MachineOperand &Root,
                                                uint64_t FlatVariant) const;

  InstructionSelector::ComplexRendererFns
  selectFlatOffset(MachineOperand &Root) const;
  InstructionSelector::ComplexRendererFns
  selectGlobalOffset(MachineOperand &Root) const;
  InstructionSelector::ComplexRendererFns
  selectScratchOffset(MachineOperand &Root) const;

  InstructionSelector::ComplexRendererFns
  selectGlobalSAddr(MachineOperand &Root) const;

  InstructionSelector::ComplexRendererFns
  selectScratchSAddr(MachineOperand &Root) const;
  bool checkFlatScratchSVSSwizzleBug(Register VAddr, Register SAddr,
                                     uint64_t ImmOffset) const;
  InstructionSelector::ComplexRendererFns
  selectScratchSVAddr(MachineOperand &Root) const;

  InstructionSelector::ComplexRendererFns
  selectMUBUFScratchOffen(MachineOperand &Root) const;
  InstructionSelector::ComplexRendererFns
  selectMUBUFScratchOffset(MachineOperand &Root) const;

  bool isDSOffsetLegal(Register Base, int64_t Offset) const;
  bool isDSOffset2Legal(Register Base, int64_t Offset0, int64_t Offset1,
                        unsigned Size) const;
  bool isFlatScratchBaseLegal(Register Addr) const;
  bool isFlatScratchBaseLegalSV(Register Addr) const;
  bool isFlatScratchBaseLegalSVImm(Register Addr) const;

  std::pair<Register, unsigned>
  selectDS1Addr1OffsetImpl(MachineOperand &Root) const;
  InstructionSelector::ComplexRendererFns
  selectDS1Addr1Offset(MachineOperand &Root) const;

  InstructionSelector::ComplexRendererFns
  selectDS64Bit4ByteAligned(MachineOperand &Root) const;

  InstructionSelector::ComplexRendererFns
  selectDS128Bit8ByteAligned(MachineOperand &Root) const;

  std::pair<Register, unsigned> selectDSReadWrite2Impl(MachineOperand &Root,
                                                       unsigned size) const;
  InstructionSelector::ComplexRendererFns
  selectDSReadWrite2(MachineOperand &Root, unsigned size) const;

  std::pair<Register, int64_t>
  getPtrBaseWithConstantOffset(Register Root,
                               const MachineRegisterInfo &MRI) const;

  // Parse out a chain of up to two g_ptr_add instructions.
  // g_ptr_add (n0, _)
  // g_ptr_add (n0, (n1 = g_ptr_add n2, n3))
  struct MUBUFAddressData {
    Register N0, N2, N3;
    int64_t Offset = 0;
  };

  bool shouldUseAddr64(MUBUFAddressData AddrData) const;

  void splitIllegalMUBUFOffset(MachineIRBuilder &B,
                               Register &SOffset, int64_t &ImmOffset) const;

  MUBUFAddressData parseMUBUFAddress(Register Src) const;

  bool selectMUBUFAddr64Impl(MachineOperand &Root, Register &VAddr,
                             Register &RSrcReg, Register &SOffset,
                             int64_t &Offset) const;

  bool selectMUBUFOffsetImpl(MachineOperand &Root, Register &RSrcReg,
                             Register &SOffset, int64_t &Offset) const;

  InstructionSelector::ComplexRendererFns
  selectBUFSOffset(MachineOperand &Root) const;

  InstructionSelector::ComplexRendererFns
  selectMUBUFAddr64(MachineOperand &Root) const;

  InstructionSelector::ComplexRendererFns
  selectMUBUFOffset(MachineOperand &Root) const;

  ComplexRendererFns selectSMRDBufferImm(MachineOperand &Root) const;
  ComplexRendererFns selectSMRDBufferImm32(MachineOperand &Root) const;
  ComplexRendererFns selectSMRDBufferSgprImm(MachineOperand &Root) const;

  std::pair<Register, unsigned> selectVOP3PMadMixModsImpl(MachineOperand &Root,
                                                          bool &Matched) const;
  ComplexRendererFns selectVOP3PMadMixModsExt(MachineOperand &Root) const;
  ComplexRendererFns selectVOP3PMadMixMods(MachineOperand &Root) const;

  void renderTruncImm32(MachineInstrBuilder &MIB, const MachineInstr &MI,
                        int OpIdx = -1) const;

  void renderTruncTImm(MachineInstrBuilder &MIB, const MachineInstr &MI,
                       int OpIdx) const;

  void renderOpSelTImm(MachineInstrBuilder &MIB, const MachineInstr &MI,
                       int OpIdx) const;

  void renderNegateImm(MachineInstrBuilder &MIB, const MachineInstr &MI,
                       int OpIdx) const;

  void renderBitcastImm(MachineInstrBuilder &MIB, const MachineInstr &MI,
                        int OpIdx) const;

  void renderPopcntImm(MachineInstrBuilder &MIB, const MachineInstr &MI,
                       int OpIdx) const;
  void renderExtractCPol(MachineInstrBuilder &MIB, const MachineInstr &MI,
                         int OpIdx) const;
  void renderExtractSWZ(MachineInstrBuilder &MIB, const MachineInstr &MI,
                        int OpIdx) const;
  void renderExtractCpolSetGLC(MachineInstrBuilder &MIB, const MachineInstr &MI,
                               int OpIdx) const;

  void renderFrameIndex(MachineInstrBuilder &MIB, const MachineInstr &MI,
                        int OpIdx) const;

  void renderFPPow2ToExponent(MachineInstrBuilder &MIB, const MachineInstr &MI,
                              int OpIdx) const;

  bool isInlineImmediate(const APInt &Imm) const;
  bool isInlineImmediate(const APFloat &Imm) const;

  // Returns true if TargetOpcode::G_AND MachineInstr `MI`'s masking of the
  // shift amount operand's `ShAmtBits` bits is unneeded.
  bool isUnneededShiftMask(const MachineInstr &MI, unsigned ShAmtBits) const;

  const SIInstrInfo &TII;
  const SIRegisterInfo &TRI;
  const AMDGPURegisterBankInfo &RBI;
  const AMDGPUTargetMachine &TM;
  const GCNSubtarget &STI;
  bool EnableLateStructurizeCFG;
#define GET_GLOBALISEL_PREDICATES_DECL
#define AMDGPUSubtarget GCNSubtarget
#include "AMDGPUGenGlobalISel.inc"
#undef GET_GLOBALISEL_PREDICATES_DECL
#undef AMDGPUSubtarget

#define GET_GLOBALISEL_TEMPORARIES_DECL
#include "AMDGPUGenGlobalISel.inc"
#undef GET_GLOBALISEL_TEMPORARIES_DECL
};

} // End llvm namespace.
#endif
