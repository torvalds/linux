//===- SIInstrInfo.h - SI Instruction Info Interface ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Interface definition for SIInstrInfo.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_SIINSTRINFO_H
#define LLVM_LIB_TARGET_AMDGPU_SIINSTRINFO_H

#include "AMDGPUMIRFormatter.h"
#include "MCTargetDesc/AMDGPUMCTargetDesc.h"
#include "SIRegisterInfo.h"
#include "Utils/AMDGPUBaseInfo.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetSchedule.h"

#define GET_INSTRINFO_HEADER
#include "AMDGPUGenInstrInfo.inc"

namespace llvm {

class APInt;
class GCNSubtarget;
class LiveVariables;
class MachineDominatorTree;
class MachineRegisterInfo;
class RegScavenger;
class TargetRegisterClass;
class ScheduleHazardRecognizer;

/// Mark the MMO of a uniform load if there are no potentially clobbering stores
/// on any path from the start of an entry function to this load.
static const MachineMemOperand::Flags MONoClobber =
    MachineMemOperand::MOTargetFlag1;

/// Mark the MMO of a load as the last use.
static const MachineMemOperand::Flags MOLastUse =
    MachineMemOperand::MOTargetFlag2;

/// Utility to store machine instructions worklist.
struct SIInstrWorklist {
  SIInstrWorklist() = default;

  void insert(MachineInstr *MI);

  MachineInstr *top() const {
    auto iter = InstrList.begin();
    return *iter;
  }

  void erase_top() {
    auto iter = InstrList.begin();
    InstrList.erase(iter);
  }

  bool empty() const { return InstrList.empty(); }

  void clear() {
    InstrList.clear();
    DeferredList.clear();
  }

  bool isDeferred(MachineInstr *MI);

  SetVector<MachineInstr *> &getDeferredList() { return DeferredList; }

private:
  /// InstrList contains the MachineInstrs.
  SetVector<MachineInstr *> InstrList;
  /// Deferred instructions are specific MachineInstr
  /// that will be added by insert method.
  SetVector<MachineInstr *> DeferredList;
};

class SIInstrInfo final : public AMDGPUGenInstrInfo {
private:
  const SIRegisterInfo RI;
  const GCNSubtarget &ST;
  TargetSchedModel SchedModel;
  mutable std::unique_ptr<AMDGPUMIRFormatter> Formatter;

  // The inverse predicate should have the negative value.
  enum BranchPredicate {
    INVALID_BR = 0,
    SCC_TRUE = 1,
    SCC_FALSE = -1,
    VCCNZ = 2,
    VCCZ = -2,
    EXECNZ = -3,
    EXECZ = 3
  };

  using SetVectorType = SmallSetVector<MachineInstr *, 32>;

  static unsigned getBranchOpcode(BranchPredicate Cond);
  static BranchPredicate getBranchPredicate(unsigned Opcode);

public:
  unsigned buildExtractSubReg(MachineBasicBlock::iterator MI,
                              MachineRegisterInfo &MRI,
                              const MachineOperand &SuperReg,
                              const TargetRegisterClass *SuperRC,
                              unsigned SubIdx,
                              const TargetRegisterClass *SubRC) const;
  MachineOperand buildExtractSubRegOrImm(
      MachineBasicBlock::iterator MI, MachineRegisterInfo &MRI,
      const MachineOperand &SuperReg, const TargetRegisterClass *SuperRC,
      unsigned SubIdx, const TargetRegisterClass *SubRC) const;

private:
  void swapOperands(MachineInstr &Inst) const;

  std::pair<bool, MachineBasicBlock *>
  moveScalarAddSub(SIInstrWorklist &Worklist, MachineInstr &Inst,
                   MachineDominatorTree *MDT = nullptr) const;

  void lowerSelect(SIInstrWorklist &Worklist, MachineInstr &Inst,
                   MachineDominatorTree *MDT = nullptr) const;

  void lowerScalarAbs(SIInstrWorklist &Worklist, MachineInstr &Inst) const;

  void lowerScalarXnor(SIInstrWorklist &Worklist, MachineInstr &Inst) const;

  void splitScalarNotBinop(SIInstrWorklist &Worklist, MachineInstr &Inst,
                           unsigned Opcode) const;

  void splitScalarBinOpN2(SIInstrWorklist &Worklist, MachineInstr &Inst,
                          unsigned Opcode) const;

  void splitScalar64BitUnaryOp(SIInstrWorklist &Worklist, MachineInstr &Inst,
                               unsigned Opcode, bool Swap = false) const;

  void splitScalar64BitBinaryOp(SIInstrWorklist &Worklist, MachineInstr &Inst,
                                unsigned Opcode,
                                MachineDominatorTree *MDT = nullptr) const;

  void splitScalarSMulU64(SIInstrWorklist &Worklist, MachineInstr &Inst,
                          MachineDominatorTree *MDT) const;

  void splitScalarSMulPseudo(SIInstrWorklist &Worklist, MachineInstr &Inst,
                             MachineDominatorTree *MDT) const;

  void splitScalar64BitXnor(SIInstrWorklist &Worklist, MachineInstr &Inst,
                            MachineDominatorTree *MDT = nullptr) const;

  void splitScalar64BitBCNT(SIInstrWorklist &Worklist,
                            MachineInstr &Inst) const;
  void splitScalar64BitBFE(SIInstrWorklist &Worklist, MachineInstr &Inst) const;
  void splitScalar64BitCountOp(SIInstrWorklist &Worklist, MachineInstr &Inst,
                               unsigned Opcode,
                               MachineDominatorTree *MDT = nullptr) const;
  void movePackToVALU(SIInstrWorklist &Worklist, MachineRegisterInfo &MRI,
                      MachineInstr &Inst) const;

  void addUsersToMoveToVALUWorklist(Register Reg, MachineRegisterInfo &MRI,
                                    SIInstrWorklist &Worklist) const;

  void addSCCDefUsersToVALUWorklist(MachineOperand &Op,
                                    MachineInstr &SCCDefInst,
                                    SIInstrWorklist &Worklist,
                                    Register NewCond = Register()) const;
  void addSCCDefsToVALUWorklist(MachineInstr *SCCUseInst,
                                SIInstrWorklist &Worklist) const;

  const TargetRegisterClass *
  getDestEquivalentVGPRClass(const MachineInstr &Inst) const;

  bool checkInstOffsetsDoNotOverlap(const MachineInstr &MIa,
                                    const MachineInstr &MIb) const;

  Register findUsedSGPR(const MachineInstr &MI, int OpIndices[3]) const;

protected:
  /// If the specific machine instruction is a instruction that moves/copies
  /// value from one register to another register return destination and source
  /// registers as machine operands.
  std::optional<DestSourcePair>
  isCopyInstrImpl(const MachineInstr &MI) const override;

  bool swapSourceModifiers(MachineInstr &MI,
                           MachineOperand &Src0, unsigned Src0OpName,
                           MachineOperand &Src1, unsigned Src1OpName) const;

  MachineInstr *commuteInstructionImpl(MachineInstr &MI, bool NewMI,
                                       unsigned OpIdx0,
                                       unsigned OpIdx1) const override;

public:
  enum TargetOperandFlags {
    MO_MASK = 0xf,

    MO_NONE = 0,
    // MO_GOTPCREL -> symbol@GOTPCREL -> R_AMDGPU_GOTPCREL.
    MO_GOTPCREL = 1,
    // MO_GOTPCREL32_LO -> symbol@gotpcrel32@lo -> R_AMDGPU_GOTPCREL32_LO.
    MO_GOTPCREL32 = 2,
    MO_GOTPCREL32_LO = 2,
    // MO_GOTPCREL32_HI -> symbol@gotpcrel32@hi -> R_AMDGPU_GOTPCREL32_HI.
    MO_GOTPCREL32_HI = 3,
    // MO_REL32_LO -> symbol@rel32@lo -> R_AMDGPU_REL32_LO.
    MO_REL32 = 4,
    MO_REL32_LO = 4,
    // MO_REL32_HI -> symbol@rel32@hi -> R_AMDGPU_REL32_HI.
    MO_REL32_HI = 5,

    MO_FAR_BRANCH_OFFSET = 6,

    MO_ABS32_LO = 8,
    MO_ABS32_HI = 9,
  };

  explicit SIInstrInfo(const GCNSubtarget &ST);

  const SIRegisterInfo &getRegisterInfo() const {
    return RI;
  }

  const GCNSubtarget &getSubtarget() const {
    return ST;
  }

  bool isReallyTriviallyReMaterializable(const MachineInstr &MI) const override;

  bool isIgnorableUse(const MachineOperand &MO) const override;

  bool isSafeToSink(MachineInstr &MI, MachineBasicBlock *SuccToSinkTo,
                    MachineCycleInfo *CI) const override;

  bool areLoadsFromSameBasePtr(SDNode *Load0, SDNode *Load1, int64_t &Offset0,
                               int64_t &Offset1) const override;

  bool getMemOperandsWithOffsetWidth(
      const MachineInstr &LdSt,
      SmallVectorImpl<const MachineOperand *> &BaseOps, int64_t &Offset,
      bool &OffsetIsScalable, LocationSize &Width,
      const TargetRegisterInfo *TRI) const final;

  bool shouldClusterMemOps(ArrayRef<const MachineOperand *> BaseOps1,
                           int64_t Offset1, bool OffsetIsScalable1,
                           ArrayRef<const MachineOperand *> BaseOps2,
                           int64_t Offset2, bool OffsetIsScalable2,
                           unsigned ClusterSize,
                           unsigned NumBytes) const override;

  bool shouldScheduleLoadsNear(SDNode *Load0, SDNode *Load1, int64_t Offset0,
                               int64_t Offset1, unsigned NumLoads) const override;

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                   const DebugLoc &DL, MCRegister DestReg, MCRegister SrcReg,
                   bool KillSrc) const override;

  void materializeImmediate(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI, const DebugLoc &DL,
                            Register DestReg, int64_t Value) const;

  const TargetRegisterClass *getPreferredSelectRegClass(
                               unsigned Size) const;

  Register insertNE(MachineBasicBlock *MBB,
                    MachineBasicBlock::iterator I, const DebugLoc &DL,
                    Register SrcReg, int Value) const;

  Register insertEQ(MachineBasicBlock *MBB,
                    MachineBasicBlock::iterator I, const DebugLoc &DL,
                    Register SrcReg, int Value)  const;

  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MI, Register SrcReg,
                           bool isKill, int FrameIndex,
                           const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI,
                           Register VReg) const override;

  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MI, Register DestReg,
                            int FrameIndex, const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI,
                            Register VReg) const override;

  bool expandPostRAPseudo(MachineInstr &MI) const override;

  void reMaterialize(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                     Register DestReg, unsigned SubIdx,
                     const MachineInstr &Orig,
                     const TargetRegisterInfo &TRI) const override;

  // Splits a V_MOV_B64_DPP_PSEUDO opcode into a pair of v_mov_b32_dpp
  // instructions. Returns a pair of generated instructions.
  // Can split either post-RA with physical registers or pre-RA with
  // virtual registers. In latter case IR needs to be in SSA form and
  // and a REG_SEQUENCE is produced to define original register.
  std::pair<MachineInstr*, MachineInstr*>
  expandMovDPP64(MachineInstr &MI) const;

  // Returns an opcode that can be used to move a value to a \p DstRC
  // register.  If there is no hardware instruction that can store to \p
  // DstRC, then AMDGPU::COPY is returned.
  unsigned getMovOpcode(const TargetRegisterClass *DstRC) const;

  const MCInstrDesc &getIndirectRegWriteMovRelPseudo(unsigned VecSize,
                                                     unsigned EltSize,
                                                     bool IsSGPR) const;

  const MCInstrDesc &getIndirectGPRIDXPseudo(unsigned VecSize,
                                             bool IsIndirectSrc) const;
  LLVM_READONLY
  int commuteOpcode(unsigned Opc) const;

  LLVM_READONLY
  inline int commuteOpcode(const MachineInstr &MI) const {
    return commuteOpcode(MI.getOpcode());
  }

  bool findCommutedOpIndices(const MachineInstr &MI, unsigned &SrcOpIdx0,
                             unsigned &SrcOpIdx1) const override;

  bool findCommutedOpIndices(const MCInstrDesc &Desc, unsigned &SrcOpIdx0,
                             unsigned &SrcOpIdx1) const;

  bool isBranchOffsetInRange(unsigned BranchOpc,
                             int64_t BrOffset) const override;

  MachineBasicBlock *getBranchDestBlock(const MachineInstr &MI) const override;

  /// Return whether the block terminate with divergent branch.
  /// Note this only work before lowering the pseudo control flow instructions.
  bool hasDivergentBranch(const MachineBasicBlock *MBB) const;

  void insertIndirectBranch(MachineBasicBlock &MBB,
                            MachineBasicBlock &NewDestBB,
                            MachineBasicBlock &RestoreBB, const DebugLoc &DL,
                            int64_t BrOffset, RegScavenger *RS) const override;

  bool analyzeBranchImpl(MachineBasicBlock &MBB,
                         MachineBasicBlock::iterator I,
                         MachineBasicBlock *&TBB,
                         MachineBasicBlock *&FBB,
                         SmallVectorImpl<MachineOperand> &Cond,
                         bool AllowModify) const;

  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify = false) const override;

  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;

  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;

  bool reverseBranchCondition(
    SmallVectorImpl<MachineOperand> &Cond) const override;

  bool canInsertSelect(const MachineBasicBlock &MBB,
                       ArrayRef<MachineOperand> Cond, Register DstReg,
                       Register TrueReg, Register FalseReg, int &CondCycles,
                       int &TrueCycles, int &FalseCycles) const override;

  void insertSelect(MachineBasicBlock &MBB,
                    MachineBasicBlock::iterator I, const DebugLoc &DL,
                    Register DstReg, ArrayRef<MachineOperand> Cond,
                    Register TrueReg, Register FalseReg) const override;

  void insertVectorSelect(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator I, const DebugLoc &DL,
                          Register DstReg, ArrayRef<MachineOperand> Cond,
                          Register TrueReg, Register FalseReg) const;

  bool analyzeCompare(const MachineInstr &MI, Register &SrcReg,
                      Register &SrcReg2, int64_t &CmpMask,
                      int64_t &CmpValue) const override;

  bool optimizeCompareInstr(MachineInstr &CmpInstr, Register SrcReg,
                            Register SrcReg2, int64_t CmpMask, int64_t CmpValue,
                            const MachineRegisterInfo *MRI) const override;

  bool
  areMemAccessesTriviallyDisjoint(const MachineInstr &MIa,
                                  const MachineInstr &MIb) const override;

  static bool isFoldableCopy(const MachineInstr &MI);

  void removeModOperands(MachineInstr &MI) const;

  bool foldImmediate(MachineInstr &UseMI, MachineInstr &DefMI, Register Reg,
                     MachineRegisterInfo *MRI) const final;

  unsigned getMachineCSELookAheadLimit() const override { return 500; }

  MachineInstr *convertToThreeAddress(MachineInstr &MI, LiveVariables *LV,
                                      LiveIntervals *LIS) const override;

  bool isSchedulingBoundary(const MachineInstr &MI,
                            const MachineBasicBlock *MBB,
                            const MachineFunction &MF) const override;

  static bool isSALU(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::SALU;
  }

  bool isSALU(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::SALU;
  }

  static bool isVALU(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::VALU;
  }

  bool isVALU(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::VALU;
  }

  static bool isImage(const MachineInstr &MI) {
    return isMIMG(MI) || isVSAMPLE(MI) || isVIMAGE(MI);
  }

  bool isImage(uint16_t Opcode) const {
    return isMIMG(Opcode) || isVSAMPLE(Opcode) || isVIMAGE(Opcode);
  }

  static bool isVMEM(const MachineInstr &MI) {
    return isMUBUF(MI) || isMTBUF(MI) || isImage(MI);
  }

  bool isVMEM(uint16_t Opcode) const {
    return isMUBUF(Opcode) || isMTBUF(Opcode) || isImage(Opcode);
  }

  static bool isSOP1(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::SOP1;
  }

  bool isSOP1(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::SOP1;
  }

  static bool isSOP2(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::SOP2;
  }

  bool isSOP2(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::SOP2;
  }

  static bool isSOPC(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::SOPC;
  }

  bool isSOPC(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::SOPC;
  }

  static bool isSOPK(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::SOPK;
  }

  bool isSOPK(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::SOPK;
  }

  static bool isSOPP(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::SOPP;
  }

  bool isSOPP(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::SOPP;
  }

  static bool isPacked(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::IsPacked;
  }

  bool isPacked(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::IsPacked;
  }

  static bool isVOP1(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::VOP1;
  }

  bool isVOP1(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::VOP1;
  }

  static bool isVOP2(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::VOP2;
  }

  bool isVOP2(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::VOP2;
  }

  static bool isVOP3(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::VOP3;
  }

  bool isVOP3(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::VOP3;
  }

  static bool isSDWA(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::SDWA;
  }

  bool isSDWA(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::SDWA;
  }

  static bool isVOPC(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::VOPC;
  }

  bool isVOPC(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::VOPC;
  }

  static bool isMUBUF(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::MUBUF;
  }

  bool isMUBUF(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::MUBUF;
  }

  static bool isMTBUF(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::MTBUF;
  }

  bool isMTBUF(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::MTBUF;
  }

  static bool isSMRD(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::SMRD;
  }

  bool isSMRD(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::SMRD;
  }

  bool isBufferSMRD(const MachineInstr &MI) const;

  static bool isDS(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::DS;
  }

  bool isDS(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::DS;
  }

  static bool isLDSDMA(const MachineInstr &MI) {
    return isVALU(MI) && (isMUBUF(MI) || isFLAT(MI));
  }

  bool isLDSDMA(uint16_t Opcode) {
    return isVALU(Opcode) && (isMUBUF(Opcode) || isFLAT(Opcode));
  }

  static bool isGWS(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::GWS;
  }

  bool isGWS(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::GWS;
  }

  bool isAlwaysGDS(uint16_t Opcode) const;

  static bool isMIMG(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::MIMG;
  }

  bool isMIMG(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::MIMG;
  }

  static bool isVIMAGE(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::VIMAGE;
  }

  bool isVIMAGE(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::VIMAGE;
  }

  static bool isVSAMPLE(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::VSAMPLE;
  }

  bool isVSAMPLE(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::VSAMPLE;
  }

  static bool isGather4(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::Gather4;
  }

  bool isGather4(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::Gather4;
  }

  static bool isFLAT(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::FLAT;
  }

  // Is a FLAT encoded instruction which accesses a specific segment,
  // i.e. global_* or scratch_*.
  static bool isSegmentSpecificFLAT(const MachineInstr &MI) {
    auto Flags = MI.getDesc().TSFlags;
    return Flags & (SIInstrFlags::FlatGlobal | SIInstrFlags::FlatScratch);
  }

  bool isSegmentSpecificFLAT(uint16_t Opcode) const {
    auto Flags = get(Opcode).TSFlags;
    return Flags & (SIInstrFlags::FlatGlobal | SIInstrFlags::FlatScratch);
  }

  static bool isFLATGlobal(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::FlatGlobal;
  }

  bool isFLATGlobal(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::FlatGlobal;
  }

  static bool isFLATScratch(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::FlatScratch;
  }

  bool isFLATScratch(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::FlatScratch;
  }

  // Any FLAT encoded instruction, including global_* and scratch_*.
  bool isFLAT(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::FLAT;
  }

  static bool isEXP(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::EXP;
  }

  static bool isDualSourceBlendEXP(const MachineInstr &MI) {
    if (!isEXP(MI))
      return false;
    unsigned Target = MI.getOperand(0).getImm();
    return Target == AMDGPU::Exp::ET_DUAL_SRC_BLEND0 ||
           Target == AMDGPU::Exp::ET_DUAL_SRC_BLEND1;
  }

  bool isEXP(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::EXP;
  }

  static bool isAtomicNoRet(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::IsAtomicNoRet;
  }

  bool isAtomicNoRet(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::IsAtomicNoRet;
  }

  static bool isAtomicRet(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::IsAtomicRet;
  }

  bool isAtomicRet(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::IsAtomicRet;
  }

  static bool isAtomic(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & (SIInstrFlags::IsAtomicRet |
                                   SIInstrFlags::IsAtomicNoRet);
  }

  bool isAtomic(uint16_t Opcode) const {
    return get(Opcode).TSFlags & (SIInstrFlags::IsAtomicRet |
                                  SIInstrFlags::IsAtomicNoRet);
  }

  static bool mayWriteLDSThroughDMA(const MachineInstr &MI) {
    return isLDSDMA(MI) && MI.getOpcode() != AMDGPU::BUFFER_STORE_LDS_DWORD;
  }

  static bool isWQM(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::WQM;
  }

  bool isWQM(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::WQM;
  }

  static bool isDisableWQM(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::DisableWQM;
  }

  bool isDisableWQM(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::DisableWQM;
  }

  // SI_SPILL_S32_TO_VGPR and SI_RESTORE_S32_FROM_VGPR form a special case of
  // SGPRs spilling to VGPRs which are SGPR spills but from VALU instructions
  // therefore we need an explicit check for them since just checking if the
  // Spill bit is set and what instruction type it came from misclassifies
  // them.
  static bool isVGPRSpill(const MachineInstr &MI) {
    return MI.getOpcode() != AMDGPU::SI_SPILL_S32_TO_VGPR &&
           MI.getOpcode() != AMDGPU::SI_RESTORE_S32_FROM_VGPR &&
           (isSpill(MI) && isVALU(MI));
  }

  bool isVGPRSpill(uint16_t Opcode) const {
    return Opcode != AMDGPU::SI_SPILL_S32_TO_VGPR &&
           Opcode != AMDGPU::SI_RESTORE_S32_FROM_VGPR &&
           (isSpill(Opcode) && isVALU(Opcode));
  }

  static bool isSGPRSpill(const MachineInstr &MI) {
    return MI.getOpcode() == AMDGPU::SI_SPILL_S32_TO_VGPR ||
           MI.getOpcode() == AMDGPU::SI_RESTORE_S32_FROM_VGPR ||
           (isSpill(MI) && isSALU(MI));
  }

  bool isSGPRSpill(uint16_t Opcode) const {
    return Opcode == AMDGPU::SI_SPILL_S32_TO_VGPR ||
           Opcode == AMDGPU::SI_RESTORE_S32_FROM_VGPR ||
           (isSpill(Opcode) && isSALU(Opcode));
  }

  bool isSpill(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::Spill;
  }

  static bool isSpill(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::Spill;
  }

  static bool isWWMRegSpillOpcode(uint16_t Opcode) {
    return Opcode == AMDGPU::SI_SPILL_WWM_V32_SAVE ||
           Opcode == AMDGPU::SI_SPILL_WWM_AV32_SAVE ||
           Opcode == AMDGPU::SI_SPILL_WWM_V32_RESTORE ||
           Opcode == AMDGPU::SI_SPILL_WWM_AV32_RESTORE;
  }

  static bool isChainCallOpcode(uint64_t Opcode) {
    return Opcode == AMDGPU::SI_CS_CHAIN_TC_W32 ||
           Opcode == AMDGPU::SI_CS_CHAIN_TC_W64;
  }

  static bool isDPP(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::DPP;
  }

  bool isDPP(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::DPP;
  }

  static bool isTRANS(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::TRANS;
  }

  bool isTRANS(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::TRANS;
  }

  static bool isVOP3P(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::VOP3P;
  }

  bool isVOP3P(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::VOP3P;
  }

  static bool isVINTRP(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::VINTRP;
  }

  bool isVINTRP(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::VINTRP;
  }

  static bool isMAI(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::IsMAI;
  }

  bool isMAI(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::IsMAI;
  }

  static bool isMFMA(const MachineInstr &MI) {
    return isMAI(MI) && MI.getOpcode() != AMDGPU::V_ACCVGPR_WRITE_B32_e64 &&
           MI.getOpcode() != AMDGPU::V_ACCVGPR_READ_B32_e64;
  }

  static bool isDOT(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::IsDOT;
  }

  static bool isWMMA(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::IsWMMA;
  }

  bool isWMMA(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::IsWMMA;
  }

  static bool isMFMAorWMMA(const MachineInstr &MI) {
    return isMFMA(MI) || isWMMA(MI) || isSWMMAC(MI);
  }

  static bool isSWMMAC(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::IsSWMMAC;
  }

  bool isSWMMAC(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::IsSWMMAC;
  }

  bool isDOT(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::IsDOT;
  }

  static bool isLDSDIR(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::LDSDIR;
  }

  bool isLDSDIR(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::LDSDIR;
  }

  static bool isVINTERP(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::VINTERP;
  }

  bool isVINTERP(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::VINTERP;
  }

  static bool isScalarUnit(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & (SIInstrFlags::SALU | SIInstrFlags::SMRD);
  }

  static bool usesVM_CNT(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::VM_CNT;
  }

  static bool usesLGKM_CNT(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::LGKM_CNT;
  }

  // Most sopk treat the immediate as a signed 16-bit, however some
  // use it as unsigned.
  static bool sopkIsZext(unsigned Opcode) {
    return Opcode == AMDGPU::S_CMPK_EQ_U32 || Opcode == AMDGPU::S_CMPK_LG_U32 ||
           Opcode == AMDGPU::S_CMPK_GT_U32 || Opcode == AMDGPU::S_CMPK_GE_U32 ||
           Opcode == AMDGPU::S_CMPK_LT_U32 || Opcode == AMDGPU::S_CMPK_LE_U32 ||
           Opcode == AMDGPU::S_GETREG_B32;
  }

  /// \returns true if this is an s_store_dword* instruction. This is more
  /// specific than isSMEM && mayStore.
  static bool isScalarStore(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::SCALAR_STORE;
  }

  bool isScalarStore(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::SCALAR_STORE;
  }

  static bool isFixedSize(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::FIXED_SIZE;
  }

  bool isFixedSize(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::FIXED_SIZE;
  }

  static bool hasFPClamp(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::FPClamp;
  }

  bool hasFPClamp(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::FPClamp;
  }

  static bool hasIntClamp(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::IntClamp;
  }

  uint64_t getClampMask(const MachineInstr &MI) const {
    const uint64_t ClampFlags = SIInstrFlags::FPClamp |
                                SIInstrFlags::IntClamp |
                                SIInstrFlags::ClampLo |
                                SIInstrFlags::ClampHi;
      return MI.getDesc().TSFlags & ClampFlags;
  }

  static bool usesFPDPRounding(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::FPDPRounding;
  }

  bool usesFPDPRounding(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::FPDPRounding;
  }

  static bool isFPAtomic(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::FPAtomic;
  }

  bool isFPAtomic(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::FPAtomic;
  }

  static bool isNeverUniform(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::IsNeverUniform;
  }

  // Check to see if opcode is for a barrier start. Pre gfx12 this is just the
  // S_BARRIER, but after support for S_BARRIER_SIGNAL* / S_BARRIER_WAIT we want
  // to check for the barrier start (S_BARRIER_SIGNAL*)
  bool isBarrierStart(unsigned Opcode) const {
    return Opcode == AMDGPU::S_BARRIER ||
           Opcode == AMDGPU::S_BARRIER_SIGNAL_M0 ||
           Opcode == AMDGPU::S_BARRIER_SIGNAL_ISFIRST_M0 ||
           Opcode == AMDGPU::S_BARRIER_SIGNAL_IMM ||
           Opcode == AMDGPU::S_BARRIER_SIGNAL_ISFIRST_IMM;
  }

  bool isBarrier(unsigned Opcode) const {
    return isBarrierStart(Opcode) || Opcode == AMDGPU::S_BARRIER_WAIT ||
           Opcode == AMDGPU::S_BARRIER_INIT_M0 ||
           Opcode == AMDGPU::S_BARRIER_INIT_IMM ||
           Opcode == AMDGPU::S_BARRIER_JOIN_IMM ||
           Opcode == AMDGPU::S_BARRIER_LEAVE ||
           Opcode == AMDGPU::DS_GWS_INIT ||
           Opcode == AMDGPU::DS_GWS_BARRIER;
  }

  static bool isF16PseudoScalarTrans(unsigned Opcode) {
    return Opcode == AMDGPU::V_S_EXP_F16_e64 ||
           Opcode == AMDGPU::V_S_LOG_F16_e64 ||
           Opcode == AMDGPU::V_S_RCP_F16_e64 ||
           Opcode == AMDGPU::V_S_RSQ_F16_e64 ||
           Opcode == AMDGPU::V_S_SQRT_F16_e64;
  }

  static bool doesNotReadTiedSource(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & SIInstrFlags::TiedSourceNotRead;
  }

  bool doesNotReadTiedSource(uint16_t Opcode) const {
    return get(Opcode).TSFlags & SIInstrFlags::TiedSourceNotRead;
  }

  static unsigned getNonSoftWaitcntOpcode(unsigned Opcode) {
    switch (Opcode) {
    case AMDGPU::S_WAITCNT_soft:
      return AMDGPU::S_WAITCNT;
    case AMDGPU::S_WAITCNT_VSCNT_soft:
      return AMDGPU::S_WAITCNT_VSCNT;
    case AMDGPU::S_WAIT_LOADCNT_soft:
      return AMDGPU::S_WAIT_LOADCNT;
    case AMDGPU::S_WAIT_STORECNT_soft:
      return AMDGPU::S_WAIT_STORECNT;
    case AMDGPU::S_WAIT_SAMPLECNT_soft:
      return AMDGPU::S_WAIT_SAMPLECNT;
    case AMDGPU::S_WAIT_BVHCNT_soft:
      return AMDGPU::S_WAIT_BVHCNT;
    case AMDGPU::S_WAIT_DSCNT_soft:
      return AMDGPU::S_WAIT_DSCNT;
    case AMDGPU::S_WAIT_KMCNT_soft:
      return AMDGPU::S_WAIT_KMCNT;
    default:
      return Opcode;
    }
  }

  bool isWaitcnt(unsigned Opcode) const {
    switch (getNonSoftWaitcntOpcode(Opcode)) {
    case AMDGPU::S_WAITCNT:
    case AMDGPU::S_WAITCNT_VSCNT:
    case AMDGPU::S_WAITCNT_VMCNT:
    case AMDGPU::S_WAITCNT_EXPCNT:
    case AMDGPU::S_WAITCNT_LGKMCNT:
    case AMDGPU::S_WAIT_LOADCNT:
    case AMDGPU::S_WAIT_LOADCNT_DSCNT:
    case AMDGPU::S_WAIT_STORECNT:
    case AMDGPU::S_WAIT_STORECNT_DSCNT:
    case AMDGPU::S_WAIT_SAMPLECNT:
    case AMDGPU::S_WAIT_BVHCNT:
    case AMDGPU::S_WAIT_EXPCNT:
    case AMDGPU::S_WAIT_DSCNT:
    case AMDGPU::S_WAIT_KMCNT:
    case AMDGPU::S_WAIT_IDLE:
      return true;
    default:
      return false;
    }
  }

  bool isVGPRCopy(const MachineInstr &MI) const {
    assert(isCopyInstr(MI));
    Register Dest = MI.getOperand(0).getReg();
    const MachineFunction &MF = *MI.getParent()->getParent();
    const MachineRegisterInfo &MRI = MF.getRegInfo();
    return !RI.isSGPRReg(MRI, Dest);
  }

  bool hasVGPRUses(const MachineInstr &MI) const {
    const MachineFunction &MF = *MI.getParent()->getParent();
    const MachineRegisterInfo &MRI = MF.getRegInfo();
    return llvm::any_of(MI.explicit_uses(),
                        [&MRI, this](const MachineOperand &MO) {
      return MO.isReg() && RI.isVGPR(MRI, MO.getReg());});
  }

  /// Return true if the instruction modifies the mode register.q
  static bool modifiesModeRegister(const MachineInstr &MI);

  /// This function is used to determine if an instruction can be safely
  /// executed under EXEC = 0 without hardware error, indeterminate results,
  /// and/or visible effects on future vector execution or outside the shader.
  /// Note: as of 2024 the only use of this is SIPreEmitPeephole where it is
  /// used in removing branches over short EXEC = 0 sequences.
  /// As such it embeds certain assumptions which may not apply to every case
  /// of EXEC = 0 execution.
  bool hasUnwantedEffectsWhenEXECEmpty(const MachineInstr &MI) const;

  /// Returns true if the instruction could potentially depend on the value of
  /// exec. If false, exec dependencies may safely be ignored.
  bool mayReadEXEC(const MachineRegisterInfo &MRI, const MachineInstr &MI) const;

  bool isInlineConstant(const APInt &Imm) const;

  bool isInlineConstant(const APFloat &Imm) const;

  // Returns true if this non-register operand definitely does not need to be
  // encoded as a 32-bit literal. Note that this function handles all kinds of
  // operands, not just immediates.
  //
  // Some operands like FrameIndexes could resolve to an inline immediate value
  // that will not require an additional 4-bytes; this function assumes that it
  // will.
  bool isInlineConstant(const MachineOperand &MO, uint8_t OperandType) const;

  bool isInlineConstant(const MachineOperand &MO,
                        const MCOperandInfo &OpInfo) const {
    return isInlineConstant(MO, OpInfo.OperandType);
  }

  /// \p returns true if \p UseMO is substituted with \p DefMO in \p MI it would
  /// be an inline immediate.
  bool isInlineConstant(const MachineInstr &MI,
                        const MachineOperand &UseMO,
                        const MachineOperand &DefMO) const {
    assert(UseMO.getParent() == &MI);
    int OpIdx = UseMO.getOperandNo();
    if (OpIdx >= MI.getDesc().NumOperands)
      return false;

    return isInlineConstant(DefMO, MI.getDesc().operands()[OpIdx]);
  }

  /// \p returns true if the operand \p OpIdx in \p MI is a valid inline
  /// immediate.
  bool isInlineConstant(const MachineInstr &MI, unsigned OpIdx) const {
    const MachineOperand &MO = MI.getOperand(OpIdx);
    return isInlineConstant(MO, MI.getDesc().operands()[OpIdx].OperandType);
  }

  bool isInlineConstant(const MachineInstr &MI, unsigned OpIdx,
                        const MachineOperand &MO) const {
    if (OpIdx >= MI.getDesc().NumOperands)
      return false;

    if (isCopyInstr(MI)) {
      unsigned Size = getOpSize(MI, OpIdx);
      assert(Size == 8 || Size == 4);

      uint8_t OpType = (Size == 8) ?
        AMDGPU::OPERAND_REG_IMM_INT64 : AMDGPU::OPERAND_REG_IMM_INT32;
      return isInlineConstant(MO, OpType);
    }

    return isInlineConstant(MO, MI.getDesc().operands()[OpIdx].OperandType);
  }

  bool isInlineConstant(const MachineOperand &MO) const {
    return isInlineConstant(*MO.getParent(), MO.getOperandNo());
  }

  bool isImmOperandLegal(const MachineInstr &MI, unsigned OpNo,
                         const MachineOperand &MO) const;

  /// Return true if this 64-bit VALU instruction has a 32-bit encoding.
  /// This function will return false if you pass it a 32-bit instruction.
  bool hasVALU32BitEncoding(unsigned Opcode) const;

  /// Returns true if this operand uses the constant bus.
  bool usesConstantBus(const MachineRegisterInfo &MRI,
                       const MachineOperand &MO,
                       const MCOperandInfo &OpInfo) const;

  /// Return true if this instruction has any modifiers.
  ///  e.g. src[012]_mod, omod, clamp.
  bool hasModifiers(unsigned Opcode) const;

  bool hasModifiersSet(const MachineInstr &MI,
                       unsigned OpName) const;
  bool hasAnyModifiersSet(const MachineInstr &MI) const;

  bool canShrink(const MachineInstr &MI,
                 const MachineRegisterInfo &MRI) const;

  MachineInstr *buildShrunkInst(MachineInstr &MI,
                                unsigned NewOpcode) const;

  bool verifyInstruction(const MachineInstr &MI,
                         StringRef &ErrInfo) const override;

  unsigned getVALUOp(const MachineInstr &MI) const;

  void insertScratchExecCopy(MachineFunction &MF, MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator MBBI,
                             const DebugLoc &DL, Register Reg, bool IsSCCLive,
                             SlotIndexes *Indexes = nullptr) const;

  void restoreExec(MachineFunction &MF, MachineBasicBlock &MBB,
                   MachineBasicBlock::iterator MBBI, const DebugLoc &DL,
                   Register Reg, SlotIndexes *Indexes = nullptr) const;

  /// Return the correct register class for \p OpNo.  For target-specific
  /// instructions, this will return the register class that has been defined
  /// in tablegen.  For generic instructions, like REG_SEQUENCE it will return
  /// the register class of its machine operand.
  /// to infer the correct register class base on the other operands.
  const TargetRegisterClass *getOpRegClass(const MachineInstr &MI,
                                           unsigned OpNo) const;

  /// Return the size in bytes of the operand OpNo on the given
  // instruction opcode.
  unsigned getOpSize(uint16_t Opcode, unsigned OpNo) const {
    const MCOperandInfo &OpInfo = get(Opcode).operands()[OpNo];

    if (OpInfo.RegClass == -1) {
      // If this is an immediate operand, this must be a 32-bit literal.
      assert(OpInfo.OperandType == MCOI::OPERAND_IMMEDIATE);
      return 4;
    }

    return RI.getRegSizeInBits(*RI.getRegClass(OpInfo.RegClass)) / 8;
  }

  /// This form should usually be preferred since it handles operands
  /// with unknown register classes.
  unsigned getOpSize(const MachineInstr &MI, unsigned OpNo) const {
    const MachineOperand &MO = MI.getOperand(OpNo);
    if (MO.isReg()) {
      if (unsigned SubReg = MO.getSubReg()) {
        return RI.getSubRegIdxSize(SubReg) / 8;
      }
    }
    return RI.getRegSizeInBits(*getOpRegClass(MI, OpNo)) / 8;
  }

  /// Legalize the \p OpIndex operand of this instruction by inserting
  /// a MOV.  For example:
  /// ADD_I32_e32 VGPR0, 15
  /// to
  /// MOV VGPR1, 15
  /// ADD_I32_e32 VGPR0, VGPR1
  ///
  /// If the operand being legalized is a register, then a COPY will be used
  /// instead of MOV.
  void legalizeOpWithMove(MachineInstr &MI, unsigned OpIdx) const;

  /// Check if \p MO is a legal operand if it was the \p OpIdx Operand
  /// for \p MI.
  bool isOperandLegal(const MachineInstr &MI, unsigned OpIdx,
                      const MachineOperand *MO = nullptr) const;

  /// Check if \p MO would be a valid operand for the given operand
  /// definition \p OpInfo. Note this does not attempt to validate constant bus
  /// restrictions (e.g. literal constant usage).
  bool isLegalVSrcOperand(const MachineRegisterInfo &MRI,
                          const MCOperandInfo &OpInfo,
                          const MachineOperand &MO) const;

  /// Check if \p MO (a register operand) is a legal register for the
  /// given operand description.
  bool isLegalRegOperand(const MachineRegisterInfo &MRI,
                         const MCOperandInfo &OpInfo,
                         const MachineOperand &MO) const;

  /// Legalize operands in \p MI by either commuting it or inserting a
  /// copy of src1.
  void legalizeOperandsVOP2(MachineRegisterInfo &MRI, MachineInstr &MI) const;

  /// Fix operands in \p MI to satisfy constant bus requirements.
  void legalizeOperandsVOP3(MachineRegisterInfo &MRI, MachineInstr &MI) const;

  /// Copy a value from a VGPR (\p SrcReg) to SGPR.  This function can only
  /// be used when it is know that the value in SrcReg is same across all
  /// threads in the wave.
  /// \returns The SGPR register that \p SrcReg was copied to.
  Register readlaneVGPRToSGPR(Register SrcReg, MachineInstr &UseMI,
                              MachineRegisterInfo &MRI) const;

  void legalizeOperandsSMRD(MachineRegisterInfo &MRI, MachineInstr &MI) const;
  void legalizeOperandsFLAT(MachineRegisterInfo &MRI, MachineInstr &MI) const;

  void legalizeGenericOperand(MachineBasicBlock &InsertMBB,
                              MachineBasicBlock::iterator I,
                              const TargetRegisterClass *DstRC,
                              MachineOperand &Op, MachineRegisterInfo &MRI,
                              const DebugLoc &DL) const;

  /// Legalize all operands in this instruction.  This function may create new
  /// instructions and control-flow around \p MI.  If present, \p MDT is
  /// updated.
  /// \returns A new basic block that contains \p MI if new blocks were created.
  MachineBasicBlock *
  legalizeOperands(MachineInstr &MI, MachineDominatorTree *MDT = nullptr) const;

  /// Change SADDR form of a FLAT \p Inst to its VADDR form if saddr operand
  /// was moved to VGPR. \returns true if succeeded.
  bool moveFlatAddrToVGPR(MachineInstr &Inst) const;

  /// Replace the instructions opcode with the equivalent VALU
  /// opcode.  This function will also move the users of MachineInstruntions
  /// in the \p WorkList to the VALU if necessary. If present, \p MDT is
  /// updated.
  void moveToVALU(SIInstrWorklist &Worklist, MachineDominatorTree *MDT) const;

  void moveToVALUImpl(SIInstrWorklist &Worklist, MachineDominatorTree *MDT,
                      MachineInstr &Inst) const;

  void insertNoop(MachineBasicBlock &MBB,
                  MachineBasicBlock::iterator MI) const override;

  void insertNoops(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                   unsigned Quantity) const override;

  void insertReturn(MachineBasicBlock &MBB) const;

  /// Build instructions that simulate the behavior of a `s_trap 2` instructions
  /// for hardware (namely, gfx11) that runs in PRIV=1 mode. There, s_trap is
  /// interpreted as a nop.
  MachineBasicBlock *insertSimulatedTrap(MachineRegisterInfo &MRI,
                                         MachineBasicBlock &MBB,
                                         MachineInstr &MI,
                                         const DebugLoc &DL) const;

  /// Return the number of wait states that result from executing this
  /// instruction.
  static unsigned getNumWaitStates(const MachineInstr &MI);

  /// Returns the operand named \p Op.  If \p MI does not have an
  /// operand named \c Op, this function returns nullptr.
  LLVM_READONLY
  MachineOperand *getNamedOperand(MachineInstr &MI, unsigned OperandName) const;

  LLVM_READONLY
  const MachineOperand *getNamedOperand(const MachineInstr &MI,
                                        unsigned OpName) const {
    return getNamedOperand(const_cast<MachineInstr &>(MI), OpName);
  }

  /// Get required immediate operand
  int64_t getNamedImmOperand(const MachineInstr &MI, unsigned OpName) const {
    int Idx = AMDGPU::getNamedOperandIdx(MI.getOpcode(), OpName);
    return MI.getOperand(Idx).getImm();
  }

  uint64_t getDefaultRsrcDataFormat() const;
  uint64_t getScratchRsrcWords23() const;

  bool isLowLatencyInstruction(const MachineInstr &MI) const;
  bool isHighLatencyDef(int Opc) const override;

  /// Return the descriptor of the target-specific machine instruction
  /// that corresponds to the specified pseudo or native opcode.
  const MCInstrDesc &getMCOpcodeFromPseudo(unsigned Opcode) const {
    return get(pseudoToMCOpcode(Opcode));
  }

  unsigned isStackAccess(const MachineInstr &MI, int &FrameIndex) const;
  unsigned isSGPRStackAccess(const MachineInstr &MI, int &FrameIndex) const;

  Register isLoadFromStackSlot(const MachineInstr &MI,
                               int &FrameIndex) const override;
  Register isStoreToStackSlot(const MachineInstr &MI,
                              int &FrameIndex) const override;

  unsigned getInstBundleSize(const MachineInstr &MI) const;
  unsigned getInstSizeInBytes(const MachineInstr &MI) const override;

  bool mayAccessFlatAddressSpace(const MachineInstr &MI) const;

  bool isNonUniformBranchInstr(MachineInstr &Instr) const;

  void convertNonUniformIfRegion(MachineBasicBlock *IfEntry,
                                 MachineBasicBlock *IfEnd) const;

  void convertNonUniformLoopRegion(MachineBasicBlock *LoopEntry,
                                   MachineBasicBlock *LoopEnd) const;

  std::pair<unsigned, unsigned>
  decomposeMachineOperandsTargetFlags(unsigned TF) const override;

  ArrayRef<std::pair<int, const char *>>
  getSerializableTargetIndices() const override;

  ArrayRef<std::pair<unsigned, const char *>>
  getSerializableDirectMachineOperandTargetFlags() const override;

  ArrayRef<std::pair<MachineMemOperand::Flags, const char *>>
  getSerializableMachineMemOperandTargetFlags() const override;

  ScheduleHazardRecognizer *
  CreateTargetPostRAHazardRecognizer(const InstrItineraryData *II,
                                 const ScheduleDAG *DAG) const override;

  ScheduleHazardRecognizer *
  CreateTargetPostRAHazardRecognizer(const MachineFunction &MF) const override;

  ScheduleHazardRecognizer *
  CreateTargetMIHazardRecognizer(const InstrItineraryData *II,
                                 const ScheduleDAGMI *DAG) const override;

  unsigned getLiveRangeSplitOpcode(Register Reg,
                                   const MachineFunction &MF) const override;

  bool isBasicBlockPrologue(const MachineInstr &MI,
                            Register Reg = Register()) const override;

  MachineInstr *createPHIDestinationCopy(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator InsPt,
                                         const DebugLoc &DL, Register Src,
                                         Register Dst) const override;

  MachineInstr *createPHISourceCopy(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator InsPt,
                                    const DebugLoc &DL, Register Src,
                                    unsigned SrcSubReg,
                                    Register Dst) const override;

  bool isWave32() const;

  /// Return a partially built integer add instruction without carry.
  /// Caller must add source operands.
  /// For pre-GFX9 it will generate unused carry destination operand.
  /// TODO: After GFX9 it should return a no-carry operation.
  MachineInstrBuilder getAddNoCarry(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator I,
                                    const DebugLoc &DL,
                                    Register DestReg) const;

  MachineInstrBuilder getAddNoCarry(MachineBasicBlock &MBB,
                                    MachineBasicBlock::iterator I,
                                    const DebugLoc &DL,
                                    Register DestReg,
                                    RegScavenger &RS) const;

  static bool isKillTerminator(unsigned Opcode);
  const MCInstrDesc &getKillTerminatorFromPseudo(unsigned Opcode) const;

  bool isLegalMUBUFImmOffset(unsigned Imm) const;

  static unsigned getMaxMUBUFImmOffset(const GCNSubtarget &ST);

  bool splitMUBUFOffset(uint32_t Imm, uint32_t &SOffset, uint32_t &ImmOffset,
                        Align Alignment = Align(4)) const;

  /// Returns if \p Offset is legal for the subtarget as the offset to a FLAT
  /// encoded instruction. If \p Signed, this is for an instruction that
  /// interprets the offset as signed.
  bool isLegalFLATOffset(int64_t Offset, unsigned AddrSpace,
                         uint64_t FlatVariant) const;

  /// Split \p COffsetVal into {immediate offset field, remainder offset}
  /// values.
  std::pair<int64_t, int64_t> splitFlatOffset(int64_t COffsetVal,
                                              unsigned AddrSpace,
                                              uint64_t FlatVariant) const;

  /// Returns true if negative offsets are allowed for the given \p FlatVariant.
  bool allowNegativeFlatOffset(uint64_t FlatVariant) const;

  /// \brief Return a target-specific opcode if Opcode is a pseudo instruction.
  /// Return -1 if the target-specific opcode for the pseudo instruction does
  /// not exist. If Opcode is not a pseudo instruction, this is identity.
  int pseudoToMCOpcode(int Opcode) const;

  /// \brief Check if this instruction should only be used by assembler.
  /// Return true if this opcode should not be used by codegen.
  bool isAsmOnlyOpcode(int MCOp) const;

  const TargetRegisterClass *getRegClass(const MCInstrDesc &TID, unsigned OpNum,
                                         const TargetRegisterInfo *TRI,
                                         const MachineFunction &MF)
    const override;

  void fixImplicitOperands(MachineInstr &MI) const;

  MachineInstr *foldMemoryOperandImpl(MachineFunction &MF, MachineInstr &MI,
                                      ArrayRef<unsigned> Ops,
                                      MachineBasicBlock::iterator InsertPt,
                                      int FrameIndex,
                                      LiveIntervals *LIS = nullptr,
                                      VirtRegMap *VRM = nullptr) const override;

  unsigned getInstrLatency(const InstrItineraryData *ItinData,
                           const MachineInstr &MI,
                           unsigned *PredCost = nullptr) const override;

  InstructionUniformity
  getInstructionUniformity(const MachineInstr &MI) const override final;

  InstructionUniformity
  getGenericInstructionUniformity(const MachineInstr &MI) const;

  const MIRFormatter *getMIRFormatter() const override {
    if (!Formatter)
      Formatter = std::make_unique<AMDGPUMIRFormatter>();
    return Formatter.get();
  }

  static unsigned getDSShaderTypeValue(const MachineFunction &MF);

  const TargetSchedModel &getSchedModel() const { return SchedModel; }

  // Enforce operand's \p OpName even alignment if required by target.
  // This is used if an operand is a 32 bit register but needs to be aligned
  // regardless.
  void enforceOperandRCAlignment(MachineInstr &MI, unsigned OpName) const;
};

/// \brief Returns true if a reg:subreg pair P has a TRC class
inline bool isOfRegClass(const TargetInstrInfo::RegSubRegPair &P,
                         const TargetRegisterClass &TRC,
                         MachineRegisterInfo &MRI) {
  auto *RC = MRI.getRegClass(P.Reg);
  if (!P.SubReg)
    return RC == &TRC;
  auto *TRI = MRI.getTargetRegisterInfo();
  return RC == TRI->getMatchingSuperRegClass(RC, &TRC, P.SubReg);
}

/// \brief Create RegSubRegPair from a register MachineOperand
inline
TargetInstrInfo::RegSubRegPair getRegSubRegPair(const MachineOperand &O) {
  assert(O.isReg());
  return TargetInstrInfo::RegSubRegPair(O.getReg(), O.getSubReg());
}

/// \brief Return the SubReg component from REG_SEQUENCE
TargetInstrInfo::RegSubRegPair getRegSequenceSubReg(MachineInstr &MI,
                                                    unsigned SubReg);

/// \brief Return the defining instruction for a given reg:subreg pair
/// skipping copy like instructions and subreg-manipulation pseudos.
/// Following another subreg of a reg:subreg isn't supported.
MachineInstr *getVRegSubRegDef(const TargetInstrInfo::RegSubRegPair &P,
                               MachineRegisterInfo &MRI);

/// \brief Return false if EXEC is not changed between the def of \p VReg at \p
/// DefMI and the use at \p UseMI. Should be run on SSA. Currently does not
/// attempt to track between blocks.
bool execMayBeModifiedBeforeUse(const MachineRegisterInfo &MRI,
                                Register VReg,
                                const MachineInstr &DefMI,
                                const MachineInstr &UseMI);

/// \brief Return false if EXEC is not changed between the def of \p VReg at \p
/// DefMI and all its uses. Should be run on SSA. Currently does not attempt to
/// track between blocks.
bool execMayBeModifiedBeforeAnyUse(const MachineRegisterInfo &MRI,
                                   Register VReg,
                                   const MachineInstr &DefMI);

namespace AMDGPU {

  LLVM_READONLY
  int getVOPe64(uint16_t Opcode);

  LLVM_READONLY
  int getVOPe32(uint16_t Opcode);

  LLVM_READONLY
  int getSDWAOp(uint16_t Opcode);

  LLVM_READONLY
  int getDPPOp32(uint16_t Opcode);

  LLVM_READONLY
  int getDPPOp64(uint16_t Opcode);

  LLVM_READONLY
  int getBasicFromSDWAOp(uint16_t Opcode);

  LLVM_READONLY
  int getCommuteRev(uint16_t Opcode);

  LLVM_READONLY
  int getCommuteOrig(uint16_t Opcode);

  LLVM_READONLY
  int getAddr64Inst(uint16_t Opcode);

  /// Check if \p Opcode is an Addr64 opcode.
  ///
  /// \returns \p Opcode if it is an Addr64 opcode, otherwise -1.
  LLVM_READONLY
  int getIfAddr64Inst(uint16_t Opcode);

  LLVM_READONLY
  int getSOPKOp(uint16_t Opcode);

  /// \returns SADDR form of a FLAT Global instruction given an \p Opcode
  /// of a VADDR form.
  LLVM_READONLY
  int getGlobalSaddrOp(uint16_t Opcode);

  /// \returns VADDR form of a FLAT Global instruction given an \p Opcode
  /// of a SADDR form.
  LLVM_READONLY
  int getGlobalVaddrOp(uint16_t Opcode);

  LLVM_READONLY
  int getVCMPXNoSDstOp(uint16_t Opcode);

  /// \returns ST form with only immediate offset of a FLAT Scratch instruction
  /// given an \p Opcode of an SS (SADDR) form.
  LLVM_READONLY
  int getFlatScratchInstSTfromSS(uint16_t Opcode);

  /// \returns SV (VADDR) form of a FLAT Scratch instruction given an \p Opcode
  /// of an SVS (SADDR + VADDR) form.
  LLVM_READONLY
  int getFlatScratchInstSVfromSVS(uint16_t Opcode);

  /// \returns SS (SADDR) form of a FLAT Scratch instruction given an \p Opcode
  /// of an SV (VADDR) form.
  LLVM_READONLY
  int getFlatScratchInstSSfromSV(uint16_t Opcode);

  /// \returns SV (VADDR) form of a FLAT Scratch instruction given an \p Opcode
  /// of an SS (SADDR) form.
  LLVM_READONLY
  int getFlatScratchInstSVfromSS(uint16_t Opcode);

  /// \returns earlyclobber version of a MAC MFMA is exists.
  LLVM_READONLY
  int getMFMAEarlyClobberOp(uint16_t Opcode);

  /// \returns v_cmpx version of a v_cmp instruction.
  LLVM_READONLY
  int getVCMPXOpFromVCMP(uint16_t Opcode);

  const uint64_t RSRC_DATA_FORMAT = 0xf00000000000LL;
  const uint64_t RSRC_ELEMENT_SIZE_SHIFT = (32 + 19);
  const uint64_t RSRC_INDEX_STRIDE_SHIFT = (32 + 21);
  const uint64_t RSRC_TID_ENABLE = UINT64_C(1) << (32 + 23);

} // end namespace AMDGPU

namespace AMDGPU {
enum AsmComments {
  // For sgpr to vgpr spill instructions
  SGPR_SPILL = MachineInstr::TAsmComments
};
} // namespace AMDGPU

namespace SI {
namespace KernelInputOffsets {

/// Offsets in bytes from the start of the input buffer
enum Offsets {
  NGROUPS_X = 0,
  NGROUPS_Y = 4,
  NGROUPS_Z = 8,
  GLOBAL_SIZE_X = 12,
  GLOBAL_SIZE_Y = 16,
  GLOBAL_SIZE_Z = 20,
  LOCAL_SIZE_X = 24,
  LOCAL_SIZE_Y = 28,
  LOCAL_SIZE_Z = 32
};

} // end namespace KernelInputOffsets
} // end namespace SI

} // end namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_SIINSTRINFO_H
