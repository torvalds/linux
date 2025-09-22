//===-- SIRegisterInfo.h - SI Register Info Interface ----------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Interface definition for SIRegisterInfo
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_SIREGISTERINFO_H
#define LLVM_LIB_TARGET_AMDGPU_SIREGISTERINFO_H

#include "llvm/ADT/BitVector.h"

#define GET_REGINFO_HEADER
#include "AMDGPUGenRegisterInfo.inc"

#include "SIDefines.h"

namespace llvm {

class GCNSubtarget;
class LiveIntervals;
class LiveRegUnits;
class RegisterBank;
struct SGPRSpillBuilder;

class SIRegisterInfo final : public AMDGPUGenRegisterInfo {
private:
  const GCNSubtarget &ST;
  bool SpillSGPRToVGPR;
  bool isWave32;
  BitVector RegPressureIgnoredUnits;

  /// Sub reg indexes for getRegSplitParts.
  /// First index represents subreg size from 1 to 16 DWORDs.
  /// The inner vector is sorted by bit offset.
  /// Provided a register can be fully split with given subregs,
  /// all elements of the inner vector combined give a full lane mask.
  static std::array<std::vector<int16_t>, 16> RegSplitParts;

  // Table representing sub reg of given width and offset.
  // First index is subreg size: 32, 64, 96, 128, 160, 192, 224, 256, 512.
  // Second index is 32 different dword offsets.
  static std::array<std::array<uint16_t, 32>, 9> SubRegFromChannelTable;

  void reserveRegisterTuples(BitVector &, MCRegister Reg) const;

public:
  SIRegisterInfo(const GCNSubtarget &ST);

  struct SpilledReg {
    Register VGPR;
    int Lane = -1;

    SpilledReg() = default;
    SpilledReg(Register R, int L) : VGPR(R), Lane(L) {}

    bool hasLane() { return Lane != -1; }
    bool hasReg() { return VGPR != 0; }
  };

  /// \returns the sub reg enum value for the given \p Channel
  /// (e.g. getSubRegFromChannel(0) -> AMDGPU::sub0)
  static unsigned getSubRegFromChannel(unsigned Channel, unsigned NumRegs = 1);

  bool spillSGPRToVGPR() const {
    return SpillSGPRToVGPR;
  }

  /// Return the largest available SGPR aligned to \p Align for the register
  /// class \p RC.
  MCRegister getAlignedHighSGPRForRC(const MachineFunction &MF,
                                     const unsigned Align,
                                     const TargetRegisterClass *RC) const;

  /// Return the end register initially reserved for the scratch buffer in case
  /// spilling is needed.
  MCRegister reservedPrivateSegmentBufferReg(const MachineFunction &MF) const;

  BitVector getReservedRegs(const MachineFunction &MF) const override;
  bool isAsmClobberable(const MachineFunction &MF,
                        MCRegister PhysReg) const override;

  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;
  const MCPhysReg *getCalleeSavedRegsViaCopy(const MachineFunction *MF) const;
  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID) const override;
  const uint32_t *getNoPreservedMask() const override;

  // Functions with the amdgpu_cs_chain or amdgpu_cs_chain_preserve calling
  // conventions are free to use certain VGPRs without saving and restoring any
  // lanes (not even inactive ones).
  static bool isChainScratchRegister(Register VGPR);

  // Stack access is very expensive. CSRs are also the high registers, and we
  // want to minimize the number of used registers.
  unsigned getCSRFirstUseCost() const override {
    return 100;
  }

  const TargetRegisterClass *
  getLargestLegalSuperClass(const TargetRegisterClass *RC,
                            const MachineFunction &MF) const override;

  Register getFrameRegister(const MachineFunction &MF) const override;

  bool hasBasePointer(const MachineFunction &MF) const;
  Register getBaseRegister() const;

  bool shouldRealignStack(const MachineFunction &MF) const override;
  bool requiresRegisterScavenging(const MachineFunction &Fn) const override;

  bool requiresFrameIndexScavenging(const MachineFunction &MF) const override;
  bool requiresFrameIndexReplacementScavenging(
    const MachineFunction &MF) const override;
  bool requiresVirtualBaseRegisters(const MachineFunction &Fn) const override;

  int64_t getScratchInstrOffset(const MachineInstr *MI) const;

  int64_t getFrameIndexInstrOffset(const MachineInstr *MI,
                                   int Idx) const override;

  bool needsFrameBaseReg(MachineInstr *MI, int64_t Offset) const override;

  Register materializeFrameBaseRegister(MachineBasicBlock *MBB, int FrameIdx,
                                        int64_t Offset) const override;

  void resolveFrameIndex(MachineInstr &MI, Register BaseReg,
                         int64_t Offset) const override;

  bool isFrameOffsetLegal(const MachineInstr *MI, Register BaseReg,
                          int64_t Offset) const override;

  const TargetRegisterClass *getPointerRegClass(
    const MachineFunction &MF, unsigned Kind = 0) const override;

  /// Returns a legal register class to copy a register in the specified class
  /// to or from. If it is possible to copy the register directly without using
  /// a cross register class copy, return the specified RC. Returns NULL if it
  /// is not possible to copy between two registers of the specified class.
  const TargetRegisterClass *
  getCrossCopyRegClass(const TargetRegisterClass *RC) const override;

  void buildVGPRSpillLoadStore(SGPRSpillBuilder &SB, int Index, int Offset,
                               bool IsLoad, bool IsKill = true) const;

  /// If \p OnlyToVGPR is true, this will only succeed if this manages to find a
  /// free VGPR lane to spill.
  bool spillSGPR(MachineBasicBlock::iterator MI, int FI, RegScavenger *RS,
                 SlotIndexes *Indexes = nullptr, LiveIntervals *LIS = nullptr,
                 bool OnlyToVGPR = false,
                 bool SpillToPhysVGPRLane = false) const;

  bool restoreSGPR(MachineBasicBlock::iterator MI, int FI, RegScavenger *RS,
                   SlotIndexes *Indexes = nullptr, LiveIntervals *LIS = nullptr,
                   bool OnlyToVGPR = false,
                   bool SpillToPhysVGPRLane = false) const;

  bool spillEmergencySGPR(MachineBasicBlock::iterator MI,
                          MachineBasicBlock &RestoreMBB, Register SGPR,
                          RegScavenger *RS) const;

  bool eliminateFrameIndex(MachineBasicBlock::iterator MI, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS) const override;

  bool eliminateSGPRToVGPRSpillFrameIndex(
      MachineBasicBlock::iterator MI, int FI, RegScavenger *RS,
      SlotIndexes *Indexes = nullptr, LiveIntervals *LIS = nullptr,
      bool SpillToPhysVGPRLane = false) const;

  StringRef getRegAsmName(MCRegister Reg) const override;

  // Pseudo regs are not allowed
  unsigned getHWRegIndex(MCRegister Reg) const {
    return getEncodingValue(Reg) & 0xff;
  }

  LLVM_READONLY
  const TargetRegisterClass *getVGPRClassForBitWidth(unsigned BitWidth) const;

  LLVM_READONLY
  const TargetRegisterClass *getAGPRClassForBitWidth(unsigned BitWidth) const;

  LLVM_READONLY
  const TargetRegisterClass *
  getVectorSuperClassForBitWidth(unsigned BitWidth) const;

  LLVM_READONLY
  static const TargetRegisterClass *getSGPRClassForBitWidth(unsigned BitWidth);

  /// \returns true if this class contains only SGPR registers
  static bool isSGPRClass(const TargetRegisterClass *RC) {
    return hasSGPRs(RC) && !hasVGPRs(RC) && !hasAGPRs(RC);
  }

  /// \returns true if this class ID contains only SGPR registers
  bool isSGPRClassID(unsigned RCID) const {
    return isSGPRClass(getRegClass(RCID));
  }

  bool isSGPRReg(const MachineRegisterInfo &MRI, Register Reg) const;

  /// \returns true if this class contains only VGPR registers
  static bool isVGPRClass(const TargetRegisterClass *RC) {
    return hasVGPRs(RC) && !hasAGPRs(RC) && !hasSGPRs(RC);
  }

  /// \returns true if this class contains only AGPR registers
  static bool isAGPRClass(const TargetRegisterClass *RC) {
    return hasAGPRs(RC) && !hasVGPRs(RC) && !hasSGPRs(RC);
  }

  /// \returns true only if this class contains both VGPR and AGPR registers
  bool isVectorSuperClass(const TargetRegisterClass *RC) const {
    return hasVGPRs(RC) && hasAGPRs(RC) && !hasSGPRs(RC);
  }

  /// \returns true only if this class contains both VGPR and SGPR registers
  bool isVSSuperClass(const TargetRegisterClass *RC) const {
    return hasVGPRs(RC) && hasSGPRs(RC) && !hasAGPRs(RC);
  }

  /// \returns true if this class contains VGPR registers.
  static bool hasVGPRs(const TargetRegisterClass *RC) {
    return RC->TSFlags & SIRCFlags::HasVGPR;
  }

  /// \returns true if this class contains AGPR registers.
  static bool hasAGPRs(const TargetRegisterClass *RC) {
    return RC->TSFlags & SIRCFlags::HasAGPR;
  }

  /// \returns true if this class contains SGPR registers.
  static bool hasSGPRs(const TargetRegisterClass *RC) {
    return RC->TSFlags & SIRCFlags::HasSGPR;
  }

  /// \returns true if this class contains any vector registers.
  static bool hasVectorRegisters(const TargetRegisterClass *RC) {
    return hasVGPRs(RC) || hasAGPRs(RC);
  }

  /// \returns A VGPR reg class with the same width as \p SRC
  const TargetRegisterClass *
  getEquivalentVGPRClass(const TargetRegisterClass *SRC) const;

  /// \returns An AGPR reg class with the same width as \p SRC
  const TargetRegisterClass *
  getEquivalentAGPRClass(const TargetRegisterClass *SRC) const;

  /// \returns A SGPR reg class with the same width as \p SRC
  const TargetRegisterClass *
  getEquivalentSGPRClass(const TargetRegisterClass *VRC) const;

  /// Returns a register class which is compatible with \p SuperRC, such that a
  /// subregister exists with class \p SubRC with subregister index \p
  /// SubIdx. If this is impossible (e.g., an unaligned subregister index within
  /// a register tuple), return null.
  const TargetRegisterClass *
  getCompatibleSubRegClass(const TargetRegisterClass *SuperRC,
                           const TargetRegisterClass *SubRC,
                           unsigned SubIdx) const;

  bool shouldRewriteCopySrc(const TargetRegisterClass *DefRC,
                            unsigned DefSubReg,
                            const TargetRegisterClass *SrcRC,
                            unsigned SrcSubReg) const override;

  /// \returns True if operands defined with this operand type can accept
  /// a literal constant (i.e. any 32-bit immediate).
  bool opCanUseLiteralConstant(unsigned OpType) const;

  /// \returns True if operands defined with this operand type can accept
  /// an inline constant. i.e. An integer value in the range (-16, 64) or
  /// -4.0f, -2.0f, -1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 2.0f, 4.0f.
  bool opCanUseInlineConstant(unsigned OpType) const;

  MCRegister findUnusedRegister(const MachineRegisterInfo &MRI,
                                const TargetRegisterClass *RC,
                                const MachineFunction &MF,
                                bool ReserveHighestVGPR = false) const;

  const TargetRegisterClass *getRegClassForReg(const MachineRegisterInfo &MRI,
                                               Register Reg) const;
  const TargetRegisterClass *
  getRegClassForOperandReg(const MachineRegisterInfo &MRI,
                           const MachineOperand &MO) const;

  bool isVGPR(const MachineRegisterInfo &MRI, Register Reg) const;
  bool isAGPR(const MachineRegisterInfo &MRI, Register Reg) const;
  bool isVectorRegister(const MachineRegisterInfo &MRI, Register Reg) const {
    return isVGPR(MRI, Reg) || isAGPR(MRI, Reg);
  }

  // FIXME: SGPRs are assumed to be uniform, but this is not true for i1 SGPRs
  // (such as VCC) which hold a wave-wide vector of boolean values. Examining
  // just the register class is not suffcient; it needs to be combined with a
  // value type. The next predicate isUniformReg() does this correctly.
  bool isDivergentRegClass(const TargetRegisterClass *RC) const override {
    return !isSGPRClass(RC);
  }

  bool isUniformReg(const MachineRegisterInfo &MRI, const RegisterBankInfo &RBI,
                    Register Reg) const override;

  ArrayRef<int16_t> getRegSplitParts(const TargetRegisterClass *RC,
                                     unsigned EltSize) const;

  bool shouldCoalesce(MachineInstr *MI,
                      const TargetRegisterClass *SrcRC,
                      unsigned SubReg,
                      const TargetRegisterClass *DstRC,
                      unsigned DstSubReg,
                      const TargetRegisterClass *NewRC,
                      LiveIntervals &LIS) const override;

  unsigned getRegPressureLimit(const TargetRegisterClass *RC,
                               MachineFunction &MF) const override;

  unsigned getRegPressureSetLimit(const MachineFunction &MF,
                                  unsigned Idx) const override;

  const int *getRegUnitPressureSets(unsigned RegUnit) const override;

  MCRegister getReturnAddressReg(const MachineFunction &MF) const;

  const TargetRegisterClass *
  getRegClassForSizeOnBank(unsigned Size, const RegisterBank &Bank) const;

  const TargetRegisterClass *
  getRegClassForTypeOnBank(LLT Ty, const RegisterBank &Bank) const {
    return getRegClassForSizeOnBank(Ty.getSizeInBits(), Bank);
  }

  const TargetRegisterClass *
  getConstrainedRegClassForOperand(const MachineOperand &MO,
                                 const MachineRegisterInfo &MRI) const override;

  const TargetRegisterClass *getBoolRC() const {
    return isWave32 ? &AMDGPU::SReg_32RegClass
                    : &AMDGPU::SReg_64RegClass;
  }

  const TargetRegisterClass *getWaveMaskRegClass() const {
    return isWave32 ? &AMDGPU::SReg_32_XM0_XEXECRegClass
                    : &AMDGPU::SReg_64_XEXECRegClass;
  }

  // Return the appropriate register class to use for 64-bit VGPRs for the
  // subtarget.
  const TargetRegisterClass *getVGPR64Class() const;

  MCRegister getVCC() const;

  MCRegister getExec() const;

  const TargetRegisterClass *getRegClass(unsigned RCID) const;

  // Find reaching register definition
  MachineInstr *findReachingDef(Register Reg, unsigned SubReg,
                                MachineInstr &Use,
                                MachineRegisterInfo &MRI,
                                LiveIntervals *LIS) const;

  const uint32_t *getAllVGPRRegMask() const;
  const uint32_t *getAllAGPRRegMask() const;
  const uint32_t *getAllVectorRegMask() const;
  const uint32_t *getAllAllocatableSRegMask() const;

  // \returns number of 32 bit registers covered by a \p LM
  static unsigned getNumCoveredRegs(LaneBitmask LM) {
    // The assumption is that every lo16 subreg is an even bit and every hi16
    // is an adjacent odd bit or vice versa.
    uint64_t Mask = LM.getAsInteger();
    uint64_t Even = Mask & 0xAAAAAAAAAAAAAAAAULL;
    Mask = (Even >> 1) | Mask;
    uint64_t Odd = Mask & 0x5555555555555555ULL;
    return llvm::popcount(Odd);
  }

  // \returns a DWORD offset of a \p SubReg
  unsigned getChannelFromSubReg(unsigned SubReg) const {
    return SubReg ? (getSubRegIdxOffset(SubReg) + 31) / 32 : 0;
  }

  // \returns a DWORD size of a \p SubReg
  unsigned getNumChannelsFromSubReg(unsigned SubReg) const {
    return getNumCoveredRegs(getSubRegIndexLaneMask(SubReg));
  }

  // For a given 16 bit \p Reg \returns a 32 bit register holding it.
  // \returns \p Reg otherwise.
  MCPhysReg get32BitRegister(MCPhysReg Reg) const;

  // Returns true if a given register class is properly aligned for
  // the subtarget.
  bool isProperlyAlignedRC(const TargetRegisterClass &RC) const;

  // Given \p RC returns corresponding aligned register class if required
  // by the subtarget.
  const TargetRegisterClass *
  getProperlyAlignedRC(const TargetRegisterClass *RC) const;

  /// Return all SGPR128 which satisfy the waves per execution unit requirement
  /// of the subtarget.
  ArrayRef<MCPhysReg> getAllSGPR128(const MachineFunction &MF) const;

  /// Return all SGPR64 which satisfy the waves per execution unit requirement
  /// of the subtarget.
  ArrayRef<MCPhysReg> getAllSGPR64(const MachineFunction &MF) const;

  /// Return all SGPR32 which satisfy the waves per execution unit requirement
  /// of the subtarget.
  ArrayRef<MCPhysReg> getAllSGPR32(const MachineFunction &MF) const;

  // Insert spill or restore instructions.
  // When lowering spill pseudos, the RegScavenger should be set.
  // For creating spill instructions during frame lowering, where no scavenger
  // is available, LiveUnits can be used.
  void buildSpillLoadStore(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MI, const DebugLoc &DL,
                           unsigned LoadStoreOp, int Index, Register ValueReg,
                           bool ValueIsKill, MCRegister ScratchOffsetReg,
                           int64_t InstrOffset, MachineMemOperand *MMO,
                           RegScavenger *RS,
                           LiveRegUnits *LiveUnits = nullptr) const;

  // Return alignment in register file of first register in a register tuple.
  unsigned getRegClassAlignmentNumBits(const TargetRegisterClass *RC) const {
    return (RC->TSFlags & SIRCFlags::RegTupleAlignUnitsMask) * 32;
  }

  // Check if register class RC has required alignment.
  bool isRegClassAligned(const TargetRegisterClass *RC,
                         unsigned AlignNumBits) const {
    assert(AlignNumBits != 0);
    unsigned RCAlign = getRegClassAlignmentNumBits(RC);
    return RCAlign == AlignNumBits ||
           (RCAlign > AlignNumBits && (RCAlign % AlignNumBits) == 0);
  }

  // Return alignment of a SubReg relative to start of a register in RC class.
  // No check if the subreg is supported by the current RC is made.
  unsigned getSubRegAlignmentNumBits(const TargetRegisterClass *RC,
                                     unsigned SubReg) const;
};

namespace AMDGPU {
/// Get the size in bits of a register from the register class \p RC.
unsigned getRegBitWidth(const TargetRegisterClass &RC);
} // namespace AMDGPU

} // End namespace llvm

#endif
