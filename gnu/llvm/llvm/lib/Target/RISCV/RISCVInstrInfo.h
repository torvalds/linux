//===-- RISCVInstrInfo.h - RISC-V Instruction Information -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the RISC-V implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_RISCV_RISCVINSTRINFO_H
#define LLVM_LIB_TARGET_RISCV_RISCVINSTRINFO_H

#include "RISCV.h"
#include "RISCVRegisterInfo.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/DiagnosticInfo.h"

#define GET_INSTRINFO_HEADER
#define GET_INSTRINFO_OPERAND_ENUM
#include "RISCVGenInstrInfo.inc"
#include "RISCVGenRegisterInfo.inc"

namespace llvm {

class RISCVSubtarget;

static const MachineMemOperand::Flags MONontemporalBit0 =
    MachineMemOperand::MOTargetFlag1;
static const MachineMemOperand::Flags MONontemporalBit1 =
    MachineMemOperand::MOTargetFlag2;

namespace RISCVCC {

enum CondCode {
  COND_EQ,
  COND_NE,
  COND_LT,
  COND_GE,
  COND_LTU,
  COND_GEU,
  COND_INVALID
};

CondCode getOppositeBranchCondition(CondCode);
unsigned getBrCond(CondCode CC, bool Imm = false);

} // end of namespace RISCVCC

// RISCV MachineCombiner patterns
enum RISCVMachineCombinerPattern : unsigned {
  FMADD_AX = MachineCombinerPattern::TARGET_PATTERN_START,
  FMADD_XA,
  FMSUB,
  FNMSUB,
  SHXADD_ADD_SLLI_OP1,
  SHXADD_ADD_SLLI_OP2,
};

class RISCVInstrInfo : public RISCVGenInstrInfo {

public:
  explicit RISCVInstrInfo(RISCVSubtarget &STI);

  MCInst getNop() const override;
  const MCInstrDesc &getBrCond(RISCVCC::CondCode CC, bool Imm = false) const;

  Register isLoadFromStackSlot(const MachineInstr &MI,
                               int &FrameIndex) const override;
  Register isLoadFromStackSlot(const MachineInstr &MI, int &FrameIndex,
                               unsigned &MemBytes) const override;
  Register isStoreToStackSlot(const MachineInstr &MI,
                              int &FrameIndex) const override;
  Register isStoreToStackSlot(const MachineInstr &MI, int &FrameIndex,
                              unsigned &MemBytes) const override;

  bool isReallyTriviallyReMaterializable(const MachineInstr &MI) const override;

  void copyPhysRegVector(MachineBasicBlock &MBB,
                         MachineBasicBlock::iterator MBBI, const DebugLoc &DL,
                         MCRegister DstReg, MCRegister SrcReg, bool KillSrc,
                         const TargetRegisterClass *RegClass) const;
  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                   const DebugLoc &DL, MCRegister DstReg, MCRegister SrcReg,
                   bool KillSrc) const override;

  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI, Register SrcReg,
                           bool IsKill, int FrameIndex,
                           const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI,
                           Register VReg) const override;

  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI, Register DstReg,
                            int FrameIndex, const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI,
                            Register VReg) const override;

  using TargetInstrInfo::foldMemoryOperandImpl;
  MachineInstr *foldMemoryOperandImpl(MachineFunction &MF, MachineInstr &MI,
                                      ArrayRef<unsigned> Ops,
                                      MachineBasicBlock::iterator InsertPt,
                                      int FrameIndex,
                                      LiveIntervals *LIS = nullptr,
                                      VirtRegMap *VRM = nullptr) const override;

  // Materializes the given integer Val into DstReg.
  void movImm(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
              const DebugLoc &DL, Register DstReg, uint64_t Val,
              MachineInstr::MIFlag Flag = MachineInstr::NoFlags,
              bool DstRenamable = false, bool DstIsDead = false) const;

  unsigned getInstSizeInBytes(const MachineInstr &MI) const override;

  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify) const override;

  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &dl,
                        int *BytesAdded = nullptr) const override;

  void insertIndirectBranch(MachineBasicBlock &MBB,
                            MachineBasicBlock &NewDestBB,
                            MachineBasicBlock &RestoreBB, const DebugLoc &DL,
                            int64_t BrOffset, RegScavenger *RS) const override;

  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;

  bool
  reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;

  bool optimizeCondBranch(MachineInstr &MI) const override;

  MachineBasicBlock *getBranchDestBlock(const MachineInstr &MI) const override;

  bool isBranchOffsetInRange(unsigned BranchOpc,
                             int64_t BrOffset) const override;

  bool analyzeSelect(const MachineInstr &MI,
                     SmallVectorImpl<MachineOperand> &Cond, unsigned &TrueOp,
                     unsigned &FalseOp, bool &Optimizable) const override;

  MachineInstr *optimizeSelect(MachineInstr &MI,
                               SmallPtrSetImpl<MachineInstr *> &SeenMIs,
                               bool) const override;

  bool isAsCheapAsAMove(const MachineInstr &MI) const override;

  std::optional<DestSourcePair>
  isCopyInstrImpl(const MachineInstr &MI) const override;

  bool verifyInstruction(const MachineInstr &MI,
                         StringRef &ErrInfo) const override;

  bool canFoldIntoAddrMode(const MachineInstr &MemI, Register Reg,
                           const MachineInstr &AddrI,
                           ExtAddrMode &AM) const override;

  MachineInstr *emitLdStWithAddr(MachineInstr &MemI,
                                 const ExtAddrMode &AM) const override;

  bool getMemOperandsWithOffsetWidth(
      const MachineInstr &MI, SmallVectorImpl<const MachineOperand *> &BaseOps,
      int64_t &Offset, bool &OffsetIsScalable, LocationSize &Width,
      const TargetRegisterInfo *TRI) const override;

  bool shouldClusterMemOps(ArrayRef<const MachineOperand *> BaseOps1,
                           int64_t Offset1, bool OffsetIsScalable1,
                           ArrayRef<const MachineOperand *> BaseOps2,
                           int64_t Offset2, bool OffsetIsScalable2,
                           unsigned ClusterSize,
                           unsigned NumBytes) const override;

  bool getMemOperandWithOffsetWidth(const MachineInstr &LdSt,
                                    const MachineOperand *&BaseOp,
                                    int64_t &Offset, LocationSize &Width,
                                    const TargetRegisterInfo *TRI) const;

  bool areMemAccessesTriviallyDisjoint(const MachineInstr &MIa,
                                       const MachineInstr &MIb) const override;


  std::pair<unsigned, unsigned>
  decomposeMachineOperandsTargetFlags(unsigned TF) const override;

  ArrayRef<std::pair<unsigned, const char *>>
  getSerializableDirectMachineOperandTargetFlags() const override;

  // Return true if the function can safely be outlined from.
  bool isFunctionSafeToOutlineFrom(MachineFunction &MF,
                                   bool OutlineFromLinkOnceODRs) const override;

  // Return true if MBB is safe to outline from, and return any target-specific
  // information in Flags.
  bool isMBBSafeToOutlineFrom(MachineBasicBlock &MBB,
                              unsigned &Flags) const override;

  bool shouldOutlineFromFunctionByDefault(MachineFunction &MF) const override;

  // Calculate target-specific information for a set of outlining candidates.
  std::optional<outliner::OutlinedFunction> getOutliningCandidateInfo(
      std::vector<outliner::Candidate> &RepeatedSequenceLocs) const override;

  // Return if/how a given MachineInstr should be outlined.
  virtual outliner::InstrType
  getOutliningTypeImpl(MachineBasicBlock::iterator &MBBI,
                       unsigned Flags) const override;

  // Insert a custom frame for outlined functions.
  void buildOutlinedFrame(MachineBasicBlock &MBB, MachineFunction &MF,
                          const outliner::OutlinedFunction &OF) const override;

  // Insert a call to an outlined function into a given basic block.
  MachineBasicBlock::iterator
  insertOutlinedCall(Module &M, MachineBasicBlock &MBB,
                     MachineBasicBlock::iterator &It, MachineFunction &MF,
                     outliner::Candidate &C) const override;

  std::optional<RegImmPair> isAddImmediate(const MachineInstr &MI,
                                           Register Reg) const override;

  bool findCommutedOpIndices(const MachineInstr &MI, unsigned &SrcOpIdx1,
                             unsigned &SrcOpIdx2) const override;
  MachineInstr *commuteInstructionImpl(MachineInstr &MI, bool NewMI,
                                       unsigned OpIdx1,
                                       unsigned OpIdx2) const override;

  MachineInstr *convertToThreeAddress(MachineInstr &MI, LiveVariables *LV,
                                      LiveIntervals *LIS) const override;

  // MIR printer helper function to annotate Operands with a comment.
  std::string
  createMIROperandComment(const MachineInstr &MI, const MachineOperand &Op,
                          unsigned OpIdx,
                          const TargetRegisterInfo *TRI) const override;

  /// Generate code to multiply the value in DestReg by Amt - handles all
  /// the common optimizations for this idiom, and supports fallback for
  /// subtargets which don't support multiply instructions.
  void mulImm(MachineFunction &MF, MachineBasicBlock &MBB,
              MachineBasicBlock::iterator II, const DebugLoc &DL,
              Register DestReg, uint32_t Amt, MachineInstr::MIFlag Flag) const;

  bool useMachineCombiner() const override { return true; }

  MachineTraceStrategy getMachineCombinerTraceStrategy() const override;

  CombinerObjective getCombinerObjective(unsigned Pattern) const override;

  bool getMachineCombinerPatterns(MachineInstr &Root,
                                  SmallVectorImpl<unsigned> &Patterns,
                                  bool DoRegPressureReduce) const override;

  void
  finalizeInsInstrs(MachineInstr &Root, unsigned &Pattern,
                    SmallVectorImpl<MachineInstr *> &InsInstrs) const override;

  void genAlternativeCodeSequence(
      MachineInstr &Root, unsigned Pattern,
      SmallVectorImpl<MachineInstr *> &InsInstrs,
      SmallVectorImpl<MachineInstr *> &DelInstrs,
      DenseMap<unsigned, unsigned> &InstrIdxForVirtReg) const override;

  bool hasReassociableOperands(const MachineInstr &Inst,
                               const MachineBasicBlock *MBB) const override;

  bool hasReassociableSibling(const MachineInstr &Inst,
                              bool &Commuted) const override;

  bool isAssociativeAndCommutative(const MachineInstr &Inst,
                                   bool Invert) const override;

  std::optional<unsigned> getInverseOpcode(unsigned Opcode) const override;

  void getReassociateOperandIndices(
      const MachineInstr &Root, unsigned Pattern,
      std::array<unsigned, 5> &OperandIndices) const override;

  ArrayRef<std::pair<MachineMemOperand::Flags, const char *>>
  getSerializableMachineMemOperandTargetFlags() const override;

  unsigned getUndefInitOpcode(unsigned RegClassID) const override {
    switch (RegClassID) {
    case RISCV::VRRegClassID:
      return RISCV::PseudoRVVInitUndefM1;
    case RISCV::VRM2RegClassID:
      return RISCV::PseudoRVVInitUndefM2;
    case RISCV::VRM4RegClassID:
      return RISCV::PseudoRVVInitUndefM4;
    case RISCV::VRM8RegClassID:
      return RISCV::PseudoRVVInitUndefM8;
    default:
      llvm_unreachable("Unexpected register class.");
    }
  }

protected:
  const RISCVSubtarget &STI;

private:
  unsigned getInstBundleLength(const MachineInstr &MI) const;

  bool isVectorAssociativeAndCommutative(const MachineInstr &MI,
                                         bool Invert = false) const;
  bool areRVVInstsReassociable(const MachineInstr &MI1,
                               const MachineInstr &MI2) const;
  bool hasReassociableVectorSibling(const MachineInstr &Inst,
                                    bool &Commuted) const;
};

namespace RISCV {

// Returns true if this is the sext.w pattern, addiw rd, rs1, 0.
bool isSEXT_W(const MachineInstr &MI);
bool isZEXT_W(const MachineInstr &MI);
bool isZEXT_B(const MachineInstr &MI);

// Returns true if the given MI is an RVV instruction opcode for which we may
// expect to see a FrameIndex operand.
bool isRVVSpill(const MachineInstr &MI);

std::optional<std::pair<unsigned, unsigned>>
isRVVSpillForZvlsseg(unsigned Opcode);

bool isFaultFirstLoad(const MachineInstr &MI);

// Implemented in RISCVGenInstrInfo.inc
int16_t getNamedOperandIdx(uint16_t Opcode, uint16_t NamedIndex);

// Return true if both input instructions have equal rounding mode. If at least
// one of the instructions does not have rounding mode, false will be returned.
bool hasEqualFRM(const MachineInstr &MI1, const MachineInstr &MI2);

// If \p Opcode is a .vx vector instruction, returns the lower number of bits
// that are used from the scalar .x operand for a given \p Log2SEW. Otherwise
// returns null.
std::optional<unsigned> getVectorLowDemandedScalarBits(uint16_t Opcode,
                                                       unsigned Log2SEW);

// Returns the MC opcode of RVV pseudo instruction.
unsigned getRVVMCOpcode(unsigned RVVPseudoOpcode);

// Special immediate for AVL operand of V pseudo instructions to indicate VLMax.
static constexpr int64_t VLMaxSentinel = -1LL;

// Mask assignments for floating-point
static constexpr unsigned FPMASK_Negative_Infinity = 0x001;
static constexpr unsigned FPMASK_Negative_Normal = 0x002;
static constexpr unsigned FPMASK_Negative_Subnormal = 0x004;
static constexpr unsigned FPMASK_Negative_Zero = 0x008;
static constexpr unsigned FPMASK_Positive_Zero = 0x010;
static constexpr unsigned FPMASK_Positive_Subnormal = 0x020;
static constexpr unsigned FPMASK_Positive_Normal = 0x040;
static constexpr unsigned FPMASK_Positive_Infinity = 0x080;
static constexpr unsigned FPMASK_Signaling_NaN = 0x100;
static constexpr unsigned FPMASK_Quiet_NaN = 0x200;
} // namespace RISCV

namespace RISCVVPseudosTable {

struct PseudoInfo {
  uint16_t Pseudo;
  uint16_t BaseInstr;
};

#define GET_RISCVVPseudosTable_DECL
#include "RISCVGenSearchableTables.inc"

} // end namespace RISCVVPseudosTable

namespace RISCV {

struct RISCVMaskedPseudoInfo {
  uint16_t MaskedPseudo;
  uint16_t UnmaskedPseudo;
  uint8_t MaskOpIdx;
  uint8_t ActiveElementsAffectResult : 1;
};
#define GET_RISCVMaskedPseudosTable_DECL
#include "RISCVGenSearchableTables.inc"
} // end namespace RISCV

} // end namespace llvm
#endif
