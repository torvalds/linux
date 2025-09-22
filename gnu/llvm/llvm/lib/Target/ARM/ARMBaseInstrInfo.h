//===-- ARMBaseInstrInfo.h - ARM Base Instruction Information ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the Base ARM implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_ARMBASEINSTRINFO_H
#define LLVM_LIB_TARGET_ARM_ARMBASEINSTRINFO_H

#include "ARMBaseRegisterInfo.h"
#include "MCTargetDesc/ARMBaseInfo.h"
#include "MCTargetDesc/ARMMCTargetDesc.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/IntrinsicsARM.h"
#include "llvm/Support/ErrorHandling.h"
#include <array>
#include <cstdint>

#define GET_INSTRINFO_HEADER
#include "ARMGenInstrInfo.inc"

namespace llvm {

class ARMBaseRegisterInfo;
class ARMSubtarget;

class ARMBaseInstrInfo : public ARMGenInstrInfo {
  const ARMSubtarget &Subtarget;

protected:
  // Can be only subclassed.
  explicit ARMBaseInstrInfo(const ARMSubtarget &STI);

  void expandLoadStackGuardBase(MachineBasicBlock::iterator MI,
                                unsigned LoadImmOpc, unsigned LoadOpc) const;

  /// Build the equivalent inputs of a REG_SEQUENCE for the given \p MI
  /// and \p DefIdx.
  /// \p [out] InputRegs of the equivalent REG_SEQUENCE. Each element of
  /// the list is modeled as <Reg:SubReg, SubIdx>.
  /// E.g., REG_SEQUENCE %1:sub1, sub0, %2, sub1 would produce
  /// two elements:
  /// - %1:sub1, sub0
  /// - %2<:0>, sub1
  ///
  /// \returns true if it is possible to build such an input sequence
  /// with the pair \p MI, \p DefIdx. False otherwise.
  ///
  /// \pre MI.isRegSequenceLike().
  bool getRegSequenceLikeInputs(
      const MachineInstr &MI, unsigned DefIdx,
      SmallVectorImpl<RegSubRegPairAndIdx> &InputRegs) const override;

  /// Build the equivalent inputs of a EXTRACT_SUBREG for the given \p MI
  /// and \p DefIdx.
  /// \p [out] InputReg of the equivalent EXTRACT_SUBREG.
  /// E.g., EXTRACT_SUBREG %1:sub1, sub0, sub1 would produce:
  /// - %1:sub1, sub0
  ///
  /// \returns true if it is possible to build such an input sequence
  /// with the pair \p MI, \p DefIdx. False otherwise.
  ///
  /// \pre MI.isExtractSubregLike().
  bool getExtractSubregLikeInputs(const MachineInstr &MI, unsigned DefIdx,
                                  RegSubRegPairAndIdx &InputReg) const override;

  /// Build the equivalent inputs of a INSERT_SUBREG for the given \p MI
  /// and \p DefIdx.
  /// \p [out] BaseReg and \p [out] InsertedReg contain
  /// the equivalent inputs of INSERT_SUBREG.
  /// E.g., INSERT_SUBREG %0:sub0, %1:sub1, sub3 would produce:
  /// - BaseReg: %0:sub0
  /// - InsertedReg: %1:sub1, sub3
  ///
  /// \returns true if it is possible to build such an input sequence
  /// with the pair \p MI, \p DefIdx. False otherwise.
  ///
  /// \pre MI.isInsertSubregLike().
  bool
  getInsertSubregLikeInputs(const MachineInstr &MI, unsigned DefIdx,
                            RegSubRegPair &BaseReg,
                            RegSubRegPairAndIdx &InsertedReg) const override;

  /// Commutes the operands in the given instruction.
  /// The commutable operands are specified by their indices OpIdx1 and OpIdx2.
  ///
  /// Do not call this method for a non-commutable instruction or for
  /// non-commutable pair of operand indices OpIdx1 and OpIdx2.
  /// Even though the instruction is commutable, the method may still
  /// fail to commute the operands, null pointer is returned in such cases.
  MachineInstr *commuteInstructionImpl(MachineInstr &MI, bool NewMI,
                                       unsigned OpIdx1,
                                       unsigned OpIdx2) const override;
  /// If the specific machine instruction is an instruction that moves/copies
  /// value from one register to another register return destination and source
  /// registers as machine operands.
  std::optional<DestSourcePair>
  isCopyInstrImpl(const MachineInstr &MI) const override;

  /// Specialization of \ref TargetInstrInfo::describeLoadedValue, used to
  /// enhance debug entry value descriptions for ARM targets.
  std::optional<ParamLoadedValue>
  describeLoadedValue(const MachineInstr &MI, Register Reg) const override;

public:
  // Return whether the target has an explicit NOP encoding.
  bool hasNOP() const;

  // Return the non-pre/post incrementing version of 'Opc'. Return 0
  // if there is not such an opcode.
  virtual unsigned getUnindexedOpcode(unsigned Opc) const = 0;

  MachineInstr *convertToThreeAddress(MachineInstr &MI, LiveVariables *LV,
                                      LiveIntervals *LIS) const override;

  virtual const ARMBaseRegisterInfo &getRegisterInfo() const = 0;
  const ARMSubtarget &getSubtarget() const { return Subtarget; }

  ScheduleHazardRecognizer *
  CreateTargetHazardRecognizer(const TargetSubtargetInfo *STI,
                               const ScheduleDAG *DAG) const override;

  ScheduleHazardRecognizer *
  CreateTargetMIHazardRecognizer(const InstrItineraryData *II,
                                 const ScheduleDAGMI *DAG) const override;

  ScheduleHazardRecognizer *
  CreateTargetPostRAHazardRecognizer(const InstrItineraryData *II,
                                     const ScheduleDAG *DAG) const override;

  // Branch analysis.
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

  bool
  reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;

  // Predication support.
  bool isPredicated(const MachineInstr &MI) const override;

  // MIR printer helper function to annotate Operands with a comment.
  std::string
  createMIROperandComment(const MachineInstr &MI, const MachineOperand &Op,
                          unsigned OpIdx,
                          const TargetRegisterInfo *TRI) const override;

  ARMCC::CondCodes getPredicate(const MachineInstr &MI) const {
    int PIdx = MI.findFirstPredOperandIdx();
    return PIdx != -1 ? (ARMCC::CondCodes)MI.getOperand(PIdx).getImm()
                      : ARMCC::AL;
  }

  bool PredicateInstruction(MachineInstr &MI,
                            ArrayRef<MachineOperand> Pred) const override;

  bool SubsumesPredicate(ArrayRef<MachineOperand> Pred1,
                         ArrayRef<MachineOperand> Pred2) const override;

  bool ClobbersPredicate(MachineInstr &MI, std::vector<MachineOperand> &Pred,
                         bool SkipDead) const override;

  bool isPredicable(const MachineInstr &MI) const override;

  // CPSR defined in instruction
  static bool isCPSRDefined(const MachineInstr &MI);

  /// GetInstSize - Returns the size of the specified MachineInstr.
  ///
  unsigned getInstSizeInBytes(const MachineInstr &MI) const override;

  Register isLoadFromStackSlot(const MachineInstr &MI,
                               int &FrameIndex) const override;
  Register isStoreToStackSlot(const MachineInstr &MI,
                              int &FrameIndex) const override;
  Register isLoadFromStackSlotPostFE(const MachineInstr &MI,
                                     int &FrameIndex) const override;
  Register isStoreToStackSlotPostFE(const MachineInstr &MI,
                                    int &FrameIndex) const override;

  void copyToCPSR(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                  unsigned SrcReg, bool KillSrc,
                  const ARMSubtarget &Subtarget) const;
  void copyFromCPSR(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                    unsigned DestReg, bool KillSrc,
                    const ARMSubtarget &Subtarget) const;

  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator I,
                   const DebugLoc &DL, MCRegister DestReg, MCRegister SrcReg,
                   bool KillSrc) const override;

  void storeRegToStackSlot(MachineBasicBlock &MBB,
                           MachineBasicBlock::iterator MBBI, Register SrcReg,
                           bool isKill, int FrameIndex,
                           const TargetRegisterClass *RC,
                           const TargetRegisterInfo *TRI,
                           Register VReg) const override;

  void loadRegFromStackSlot(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator MBBI, Register DestReg,
                            int FrameIndex, const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI,
                            Register VReg) const override;

  bool expandPostRAPseudo(MachineInstr &MI) const override;

  bool shouldSink(const MachineInstr &MI) const override;

  void reMaterialize(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                     Register DestReg, unsigned SubIdx,
                     const MachineInstr &Orig,
                     const TargetRegisterInfo &TRI) const override;

  MachineInstr &
  duplicate(MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertBefore,
            const MachineInstr &Orig) const override;

  const MachineInstrBuilder &AddDReg(MachineInstrBuilder &MIB, unsigned Reg,
                                     unsigned SubIdx, unsigned State,
                                     const TargetRegisterInfo *TRI) const;

  bool produceSameValue(const MachineInstr &MI0, const MachineInstr &MI1,
                        const MachineRegisterInfo *MRI) const override;

  /// areLoadsFromSameBasePtr - This is used by the pre-regalloc scheduler to
  /// determine if two loads are loading from the same base address. It should
  /// only return true if the base pointers are the same and the only
  /// differences between the two addresses is the offset. It also returns the
  /// offsets by reference.
  bool areLoadsFromSameBasePtr(SDNode *Load1, SDNode *Load2, int64_t &Offset1,
                               int64_t &Offset2) const override;

  /// shouldScheduleLoadsNear - This is a used by the pre-regalloc scheduler to
  /// determine (in conjunction with areLoadsFromSameBasePtr) if two loads
  /// should be scheduled togther. On some targets if two loads are loading from
  /// addresses in the same cache line, it's better if they are scheduled
  /// together. This function takes two integers that represent the load offsets
  /// from the common base address. It returns true if it decides it's desirable
  /// to schedule the two loads together. "NumLoads" is the number of loads that
  /// have already been scheduled after Load1.
  bool shouldScheduleLoadsNear(SDNode *Load1, SDNode *Load2,
                               int64_t Offset1, int64_t Offset2,
                               unsigned NumLoads) const override;

  bool isSchedulingBoundary(const MachineInstr &MI,
                            const MachineBasicBlock *MBB,
                            const MachineFunction &MF) const override;

  bool isProfitableToIfCvt(MachineBasicBlock &MBB,
                           unsigned NumCycles, unsigned ExtraPredCycles,
                           BranchProbability Probability) const override;

  bool isProfitableToIfCvt(MachineBasicBlock &TMBB, unsigned NumT,
                           unsigned ExtraT, MachineBasicBlock &FMBB,
                           unsigned NumF, unsigned ExtraF,
                           BranchProbability Probability) const override;

  bool isProfitableToDupForIfCvt(MachineBasicBlock &MBB, unsigned NumCycles,
                                 BranchProbability Probability) const override {
    return NumCycles == 1;
  }

  unsigned extraSizeToPredicateInstructions(const MachineFunction &MF,
                                            unsigned NumInsts) const override;
  unsigned predictBranchSizeForIfCvt(MachineInstr &MI) const override;

  bool isProfitableToUnpredicate(MachineBasicBlock &TMBB,
                                 MachineBasicBlock &FMBB) const override;

  /// analyzeCompare - For a comparison instruction, return the source registers
  /// in SrcReg and SrcReg2 if having two register operands, and the value it
  /// compares against in CmpValue. Return true if the comparison instruction
  /// can be analyzed.
  bool analyzeCompare(const MachineInstr &MI, Register &SrcReg,
                      Register &SrcReg2, int64_t &CmpMask,
                      int64_t &CmpValue) const override;

  /// optimizeCompareInstr - Convert the instruction to set the zero flag so
  /// that we can remove a "comparison with zero"; Remove a redundant CMP
  /// instruction if the flags can be updated in the same way by an earlier
  /// instruction such as SUB.
  bool optimizeCompareInstr(MachineInstr &CmpInstr, Register SrcReg,
                            Register SrcReg2, int64_t CmpMask, int64_t CmpValue,
                            const MachineRegisterInfo *MRI) const override;

  bool analyzeSelect(const MachineInstr &MI,
                     SmallVectorImpl<MachineOperand> &Cond, unsigned &TrueOp,
                     unsigned &FalseOp, bool &Optimizable) const override;

  MachineInstr *optimizeSelect(MachineInstr &MI,
                               SmallPtrSetImpl<MachineInstr *> &SeenMIs,
                               bool) const override;

  /// foldImmediate - 'Reg' is known to be defined by a move immediate
  /// instruction, try to fold the immediate into the use instruction.
  bool foldImmediate(MachineInstr &UseMI, MachineInstr &DefMI, Register Reg,
                     MachineRegisterInfo *MRI) const override;

  unsigned getNumMicroOps(const InstrItineraryData *ItinData,
                          const MachineInstr &MI) const override;

  std::optional<unsigned> getOperandLatency(const InstrItineraryData *ItinData,
                                            const MachineInstr &DefMI,
                                            unsigned DefIdx,
                                            const MachineInstr &UseMI,
                                            unsigned UseIdx) const override;
  std::optional<unsigned> getOperandLatency(const InstrItineraryData *ItinData,
                                            SDNode *DefNode, unsigned DefIdx,
                                            SDNode *UseNode,
                                            unsigned UseIdx) const override;

  /// VFP/NEON execution domains.
  std::pair<uint16_t, uint16_t>
  getExecutionDomain(const MachineInstr &MI) const override;
  void setExecutionDomain(MachineInstr &MI, unsigned Domain) const override;

  unsigned
  getPartialRegUpdateClearance(const MachineInstr &, unsigned,
                               const TargetRegisterInfo *) const override;
  void breakPartialRegDependency(MachineInstr &, unsigned,
                                 const TargetRegisterInfo *TRI) const override;

  /// Get the number of addresses by LDM or VLDM or zero for unknown.
  unsigned getNumLDMAddresses(const MachineInstr &MI) const;

  std::pair<unsigned, unsigned>
  decomposeMachineOperandsTargetFlags(unsigned TF) const override;
  ArrayRef<std::pair<unsigned, const char *>>
  getSerializableDirectMachineOperandTargetFlags() const override;
  ArrayRef<std::pair<unsigned, const char *>>
  getSerializableBitmaskMachineOperandTargetFlags() const override;

  /// ARM supports the MachineOutliner.
  bool isFunctionSafeToOutlineFrom(MachineFunction &MF,
                                   bool OutlineFromLinkOnceODRs) const override;
  std::optional<outliner::OutlinedFunction> getOutliningCandidateInfo(
      std::vector<outliner::Candidate> &RepeatedSequenceLocs) const override;
  void mergeOutliningCandidateAttributes(
      Function &F, std::vector<outliner::Candidate> &Candidates) const override;
  outliner::InstrType getOutliningTypeImpl(MachineBasicBlock::iterator &MIT,
                                       unsigned Flags) const override;
  bool isMBBSafeToOutlineFrom(MachineBasicBlock &MBB,
                              unsigned &Flags) const override;
  void buildOutlinedFrame(MachineBasicBlock &MBB, MachineFunction &MF,
                          const outliner::OutlinedFunction &OF) const override;
  MachineBasicBlock::iterator
  insertOutlinedCall(Module &M, MachineBasicBlock &MBB,
                     MachineBasicBlock::iterator &It, MachineFunction &MF,
                     outliner::Candidate &C) const override;

  /// Enable outlining by default at -Oz.
  bool shouldOutlineFromFunctionByDefault(MachineFunction &MF) const override;

  bool isUnspillableTerminatorImpl(const MachineInstr *MI) const override {
    return MI->getOpcode() == ARM::t2LoopEndDec ||
           MI->getOpcode() == ARM::t2DoLoopStartTP ||
           MI->getOpcode() == ARM::t2WhileLoopStartLR ||
           MI->getOpcode() == ARM::t2WhileLoopStartTP;
  }

  /// Analyze loop L, which must be a single-basic-block loop, and if the
  /// conditions can be understood enough produce a PipelinerLoopInfo object.
  std::unique_ptr<TargetInstrInfo::PipelinerLoopInfo>
  analyzeLoopForPipelining(MachineBasicBlock *LoopBB) const override;

private:
  /// Returns an unused general-purpose register which can be used for
  /// constructing an outlined call if one exists. Returns 0 otherwise.
  Register findRegisterToSaveLRTo(outliner::Candidate &C) const;

  /// Adds an instruction which saves the link register on top of the stack into
  /// the MachineBasicBlock \p MBB at position \p It. If \p Auth is true,
  /// compute and store an authentication code alongiside the link register.
  /// If \p CFI is true, emit CFI instructions.
  void saveLROnStack(MachineBasicBlock &MBB, MachineBasicBlock::iterator It,
                     bool CFI, bool Auth) const;

  /// Adds an instruction which restores the link register from the top the
  /// stack into the MachineBasicBlock \p MBB at position \p It. If \p Auth is
  /// true, restore an authentication code and authenticate LR.
  /// If \p CFI is true, emit CFI instructions.
  void restoreLRFromStack(MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator It, bool CFI,
                          bool Auth) const;

  /// Emit CFI instructions into the MachineBasicBlock \p MBB at position \p It,
  /// for the case when the LR is saved in the register \p Reg.
  void emitCFIForLRSaveToReg(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator It,
                             Register Reg) const;

  /// Emit CFI instructions into the MachineBasicBlock \p MBB at position \p It,
  /// after the LR is was restored from a register.
  void emitCFIForLRRestoreFromReg(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator It) const;
  /// \brief Sets the offsets on outlined instructions in \p MBB which use SP
  /// so that they will be valid post-outlining.
  ///
  /// \param MBB A \p MachineBasicBlock in an outlined function.
  void fixupPostOutline(MachineBasicBlock &MBB) const;

  /// Returns true if the machine instruction offset can handle the stack fixup
  /// and updates it if requested.
  bool checkAndUpdateStackOffset(MachineInstr *MI, int64_t Fixup,
                                 bool Updt) const;

  unsigned getInstBundleLength(const MachineInstr &MI) const;

  std::optional<unsigned> getVLDMDefCycle(const InstrItineraryData *ItinData,
                                          const MCInstrDesc &DefMCID,
                                          unsigned DefClass, unsigned DefIdx,
                                          unsigned DefAlign) const;
  std::optional<unsigned> getLDMDefCycle(const InstrItineraryData *ItinData,
                                         const MCInstrDesc &DefMCID,
                                         unsigned DefClass, unsigned DefIdx,
                                         unsigned DefAlign) const;
  std::optional<unsigned> getVSTMUseCycle(const InstrItineraryData *ItinData,
                                          const MCInstrDesc &UseMCID,
                                          unsigned UseClass, unsigned UseIdx,
                                          unsigned UseAlign) const;
  std::optional<unsigned> getSTMUseCycle(const InstrItineraryData *ItinData,
                                         const MCInstrDesc &UseMCID,
                                         unsigned UseClass, unsigned UseIdx,
                                         unsigned UseAlign) const;
  std::optional<unsigned> getOperandLatency(const InstrItineraryData *ItinData,
                                            const MCInstrDesc &DefMCID,
                                            unsigned DefIdx, unsigned DefAlign,
                                            const MCInstrDesc &UseMCID,
                                            unsigned UseIdx,
                                            unsigned UseAlign) const;

  std::optional<unsigned> getOperandLatencyImpl(
      const InstrItineraryData *ItinData, const MachineInstr &DefMI,
      unsigned DefIdx, const MCInstrDesc &DefMCID, unsigned DefAdj,
      const MachineOperand &DefMO, unsigned Reg, const MachineInstr &UseMI,
      unsigned UseIdx, const MCInstrDesc &UseMCID, unsigned UseAdj) const;

  unsigned getPredicationCost(const MachineInstr &MI) const override;

  unsigned getInstrLatency(const InstrItineraryData *ItinData,
                           const MachineInstr &MI,
                           unsigned *PredCost = nullptr) const override;

  unsigned getInstrLatency(const InstrItineraryData *ItinData,
                           SDNode *Node) const override;

  bool hasHighOperandLatency(const TargetSchedModel &SchedModel,
                             const MachineRegisterInfo *MRI,
                             const MachineInstr &DefMI, unsigned DefIdx,
                             const MachineInstr &UseMI,
                             unsigned UseIdx) const override;
  bool hasLowDefLatency(const TargetSchedModel &SchedModel,
                        const MachineInstr &DefMI,
                        unsigned DefIdx) const override;

  /// verifyInstruction - Perform target specific instruction verification.
  bool verifyInstruction(const MachineInstr &MI,
                         StringRef &ErrInfo) const override;

  virtual void expandLoadStackGuard(MachineBasicBlock::iterator MI) const = 0;

  void expandMEMCPY(MachineBasicBlock::iterator) const;

  /// Identify instructions that can be folded into a MOVCC instruction, and
  /// return the defining instruction.
  MachineInstr *canFoldIntoMOVCC(Register Reg, const MachineRegisterInfo &MRI,
                                 const TargetInstrInfo *TII) const;

  bool isReallyTriviallyReMaterializable(const MachineInstr &MI) const override;

private:
  /// Modeling special VFP / NEON fp MLA / MLS hazards.

  /// MLxEntryMap - Map fp MLA / MLS to the corresponding entry in the internal
  /// MLx table.
  DenseMap<unsigned, unsigned> MLxEntryMap;

  /// MLxHazardOpcodes - Set of add / sub and multiply opcodes that would cause
  /// stalls when scheduled together with fp MLA / MLS opcodes.
  SmallSet<unsigned, 16> MLxHazardOpcodes;

public:
  /// isFpMLxInstruction - Return true if the specified opcode is a fp MLA / MLS
  /// instruction.
  bool isFpMLxInstruction(unsigned Opcode) const {
    return MLxEntryMap.count(Opcode);
  }

  /// isFpMLxInstruction - This version also returns the multiply opcode and the
  /// addition / subtraction opcode to expand to. Return true for 'HasLane' for
  /// the MLX instructions with an extra lane operand.
  bool isFpMLxInstruction(unsigned Opcode, unsigned &MulOpc,
                          unsigned &AddSubOpc, bool &NegAcc,
                          bool &HasLane) const;

  /// canCauseFpMLxStall - Return true if an instruction of the specified opcode
  /// will cause stalls when scheduled after (within 4-cycle window) a fp
  /// MLA / MLS instruction.
  bool canCauseFpMLxStall(unsigned Opcode) const {
    return MLxHazardOpcodes.count(Opcode);
  }

  /// Returns true if the instruction has a shift by immediate that can be
  /// executed in one cycle less.
  bool isSwiftFastImmShift(const MachineInstr *MI) const;

  /// Returns predicate register associated with the given frame instruction.
  unsigned getFramePred(const MachineInstr &MI) const {
    assert(isFrameInstr(MI));
    // Operands of ADJCALLSTACKDOWN/ADJCALLSTACKUP:
    // - argument declared in the pattern:
    // 0 - frame size
    // 1 - arg of CALLSEQ_START/CALLSEQ_END
    // 2 - predicate code (like ARMCC::AL)
    // - added by predOps:
    // 3 - predicate reg
    return MI.getOperand(3).getReg();
  }

  std::optional<RegImmPair> isAddImmediate(const MachineInstr &MI,
                                           Register Reg) const override;

  unsigned getUndefInitOpcode(unsigned RegClassID) const override {
    if (RegClassID == ARM::MQPRRegClass.getID())
      return ARM::PseudoARMInitUndefMQPR;
    if (RegClassID == ARM::SPRRegClass.getID())
      return ARM::PseudoARMInitUndefSPR;
    if (RegClassID == ARM::DPR_VFP2RegClass.getID())
      return ARM::PseudoARMInitUndefDPR_VFP2;
    if (RegClassID == ARM::GPRRegClass.getID())
      return ARM::PseudoARMInitUndefGPR;

    llvm_unreachable("Unexpected register class.");
  }
};

/// Get the operands corresponding to the given \p Pred value. By default, the
/// predicate register is assumed to be 0 (no register), but you can pass in a
/// \p PredReg if that is not the case.
static inline std::array<MachineOperand, 2> predOps(ARMCC::CondCodes Pred,
                                                    unsigned PredReg = 0) {
  return {{MachineOperand::CreateImm(static_cast<int64_t>(Pred)),
           MachineOperand::CreateReg(PredReg, false)}};
}

/// Get the operand corresponding to the conditional code result. By default,
/// this is 0 (no register).
static inline MachineOperand condCodeOp(unsigned CCReg = 0) {
  return MachineOperand::CreateReg(CCReg, false);
}

/// Get the operand corresponding to the conditional code result for Thumb1.
/// This operand will always refer to CPSR and it will have the Define flag set.
/// You can optionally set the Dead flag by means of \p isDead.
static inline MachineOperand t1CondCodeOp(bool isDead = false) {
  return MachineOperand::CreateReg(ARM::CPSR,
                                   /*Define*/ true, /*Implicit*/ false,
                                   /*Kill*/ false, isDead);
}

static inline
bool isUncondBranchOpcode(int Opc) {
  return Opc == ARM::B || Opc == ARM::tB || Opc == ARM::t2B;
}

// This table shows the VPT instruction variants, i.e. the different
// mask field encodings, see also B5.6. Predication/conditional execution in
// the ArmARM.
static inline bool isVPTOpcode(int Opc) {
  return Opc == ARM::MVE_VPTv16i8 || Opc == ARM::MVE_VPTv16u8 ||
         Opc == ARM::MVE_VPTv16s8 || Opc == ARM::MVE_VPTv8i16 ||
         Opc == ARM::MVE_VPTv8u16 || Opc == ARM::MVE_VPTv8s16 ||
         Opc == ARM::MVE_VPTv4i32 || Opc == ARM::MVE_VPTv4u32 ||
         Opc == ARM::MVE_VPTv4s32 || Opc == ARM::MVE_VPTv4f32 ||
         Opc == ARM::MVE_VPTv8f16 || Opc == ARM::MVE_VPTv16i8r ||
         Opc == ARM::MVE_VPTv16u8r || Opc == ARM::MVE_VPTv16s8r ||
         Opc == ARM::MVE_VPTv8i16r || Opc == ARM::MVE_VPTv8u16r ||
         Opc == ARM::MVE_VPTv8s16r || Opc == ARM::MVE_VPTv4i32r ||
         Opc == ARM::MVE_VPTv4u32r || Opc == ARM::MVE_VPTv4s32r ||
         Opc == ARM::MVE_VPTv4f32r || Opc == ARM::MVE_VPTv8f16r ||
         Opc == ARM::MVE_VPST;
}

static inline
unsigned VCMPOpcodeToVPT(unsigned Opcode) {
  switch (Opcode) {
  default:
    return 0;
  case ARM::MVE_VCMPf32:
    return ARM::MVE_VPTv4f32;
  case ARM::MVE_VCMPf16:
    return ARM::MVE_VPTv8f16;
  case ARM::MVE_VCMPi8:
    return ARM::MVE_VPTv16i8;
  case ARM::MVE_VCMPi16:
    return ARM::MVE_VPTv8i16;
  case ARM::MVE_VCMPi32:
    return ARM::MVE_VPTv4i32;
  case ARM::MVE_VCMPu8:
    return ARM::MVE_VPTv16u8;
  case ARM::MVE_VCMPu16:
    return ARM::MVE_VPTv8u16;
  case ARM::MVE_VCMPu32:
    return ARM::MVE_VPTv4u32;
  case ARM::MVE_VCMPs8:
    return ARM::MVE_VPTv16s8;
  case ARM::MVE_VCMPs16:
    return ARM::MVE_VPTv8s16;
  case ARM::MVE_VCMPs32:
    return ARM::MVE_VPTv4s32;

  case ARM::MVE_VCMPf32r:
    return ARM::MVE_VPTv4f32r;
  case ARM::MVE_VCMPf16r:
    return ARM::MVE_VPTv8f16r;
  case ARM::MVE_VCMPi8r:
    return ARM::MVE_VPTv16i8r;
  case ARM::MVE_VCMPi16r:
    return ARM::MVE_VPTv8i16r;
  case ARM::MVE_VCMPi32r:
    return ARM::MVE_VPTv4i32r;
  case ARM::MVE_VCMPu8r:
    return ARM::MVE_VPTv16u8r;
  case ARM::MVE_VCMPu16r:
    return ARM::MVE_VPTv8u16r;
  case ARM::MVE_VCMPu32r:
    return ARM::MVE_VPTv4u32r;
  case ARM::MVE_VCMPs8r:
    return ARM::MVE_VPTv16s8r;
  case ARM::MVE_VCMPs16r:
    return ARM::MVE_VPTv8s16r;
  case ARM::MVE_VCMPs32r:
    return ARM::MVE_VPTv4s32r;
  }
}

static inline
bool isCondBranchOpcode(int Opc) {
  return Opc == ARM::Bcc || Opc == ARM::tBcc || Opc == ARM::t2Bcc;
}

static inline bool isJumpTableBranchOpcode(int Opc) {
  return Opc == ARM::BR_JTr || Opc == ARM::BR_JTm_i12 ||
         Opc == ARM::BR_JTm_rs || Opc == ARM::BR_JTadd || Opc == ARM::tBR_JTr ||
         Opc == ARM::t2BR_JT;
}

static inline
bool isIndirectBranchOpcode(int Opc) {
  return Opc == ARM::BX || Opc == ARM::MOVPCRX || Opc == ARM::tBRIND;
}

static inline bool isIndirectCall(const MachineInstr &MI) {
  int Opc = MI.getOpcode();
  switch (Opc) {
    // indirect calls:
  case ARM::BLX:
  case ARM::BLX_noip:
  case ARM::BLX_pred:
  case ARM::BLX_pred_noip:
  case ARM::BX_CALL:
  case ARM::BMOVPCRX_CALL:
  case ARM::TCRETURNri:
  case ARM::TCRETURNrinotr12:
  case ARM::TAILJMPr:
  case ARM::TAILJMPr4:
  case ARM::tBLXr:
  case ARM::tBLXr_noip:
  case ARM::tBLXNSr:
  case ARM::tBLXNS_CALL:
  case ARM::tBX_CALL:
  case ARM::tTAILJMPr:
    assert(MI.isCall(MachineInstr::IgnoreBundle));
    return true;
    // direct calls:
  case ARM::BL:
  case ARM::BL_pred:
  case ARM::BMOVPCB_CALL:
  case ARM::BL_PUSHLR:
  case ARM::BLXi:
  case ARM::TCRETURNdi:
  case ARM::TAILJMPd:
  case ARM::SVC:
  case ARM::HVC:
  case ARM::TPsoft:
  case ARM::tTAILJMPd:
  case ARM::t2SMC:
  case ARM::t2HVC:
  case ARM::tBL:
  case ARM::tBLXi:
  case ARM::tBL_PUSHLR:
  case ARM::tTAILJMPdND:
  case ARM::tSVC:
  case ARM::tTPsoft:
    assert(MI.isCall(MachineInstr::IgnoreBundle));
    return false;
  }
  assert(!MI.isCall(MachineInstr::IgnoreBundle));
  return false;
}

static inline bool isIndirectControlFlowNotComingBack(const MachineInstr &MI) {
  int opc = MI.getOpcode();
  return MI.isReturn() || isIndirectBranchOpcode(MI.getOpcode()) ||
         isJumpTableBranchOpcode(opc);
}

static inline bool isSpeculationBarrierEndBBOpcode(int Opc) {
  return Opc == ARM::SpeculationBarrierISBDSBEndBB ||
         Opc == ARM::SpeculationBarrierSBEndBB ||
         Opc == ARM::t2SpeculationBarrierISBDSBEndBB ||
         Opc == ARM::t2SpeculationBarrierSBEndBB;
}

static inline bool isPopOpcode(int Opc) {
  return Opc == ARM::tPOP_RET || Opc == ARM::LDMIA_RET ||
         Opc == ARM::t2LDMIA_RET || Opc == ARM::tPOP || Opc == ARM::LDMIA_UPD ||
         Opc == ARM::t2LDMIA_UPD || Opc == ARM::VLDMDIA_UPD;
}

static inline bool isPushOpcode(int Opc) {
  return Opc == ARM::tPUSH || Opc == ARM::t2STMDB_UPD ||
         Opc == ARM::STMDB_UPD || Opc == ARM::VSTMDDB_UPD;
}

static inline bool isSubImmOpcode(int Opc) {
  return Opc == ARM::SUBri ||
         Opc == ARM::tSUBi3 || Opc == ARM::tSUBi8 ||
         Opc == ARM::tSUBSi3 || Opc == ARM::tSUBSi8 ||
         Opc == ARM::t2SUBri || Opc == ARM::t2SUBri12 || Opc == ARM::t2SUBSri;
}

static inline bool isMovRegOpcode(int Opc) {
  return Opc == ARM::MOVr || Opc == ARM::tMOVr || Opc == ARM::t2MOVr;
}
/// isValidCoprocessorNumber - decide whether an explicit coprocessor
/// number is legal in generic instructions like CDP. The answer can
/// vary with the subtarget.
static inline bool isValidCoprocessorNumber(unsigned Num,
                                            const FeatureBitset& featureBits) {
  // In Armv7 and Armv8-M CP10 and CP11 clash with VFP/NEON, however, the
  // coprocessor is still valid for CDP/MCR/MRC and friends. Allowing it is
  // useful for code which is shared with older architectures which do not know
  // the new VFP/NEON mnemonics.

  // Armv8-A disallows everything *other* than 111x (CP14 and CP15).
  if (featureBits[ARM::HasV8Ops] && (Num & 0xE) != 0xE)
    return false;

  // Armv8.1-M disallows 100x (CP8,CP9) and 111x (CP14,CP15)
  // which clash with MVE.
  if (featureBits[ARM::HasV8_1MMainlineOps] &&
      ((Num & 0xE) == 0x8 || (Num & 0xE) == 0xE))
    return false;

  return true;
}

static inline bool isSEHInstruction(const MachineInstr &MI) {
  unsigned Opc = MI.getOpcode();
  switch (Opc) {
  case ARM::SEH_StackAlloc:
  case ARM::SEH_SaveRegs:
  case ARM::SEH_SaveRegs_Ret:
  case ARM::SEH_SaveSP:
  case ARM::SEH_SaveFRegs:
  case ARM::SEH_SaveLR:
  case ARM::SEH_Nop:
  case ARM::SEH_Nop_Ret:
  case ARM::SEH_PrologEnd:
  case ARM::SEH_EpilogStart:
  case ARM::SEH_EpilogEnd:
    return true;
  default:
    return false;
  }
}

/// getInstrPredicate - If instruction is predicated, returns its predicate
/// condition, otherwise returns AL. It also returns the condition code
/// register by reference.
ARMCC::CondCodes getInstrPredicate(const MachineInstr &MI, Register &PredReg);

unsigned getMatchingCondBranchOpcode(unsigned Opc);

/// Map pseudo instructions that imply an 'S' bit onto real opcodes. Whether
/// the instruction is encoded with an 'S' bit is determined by the optional
/// CPSR def operand.
unsigned convertAddSubFlagsOpcode(unsigned OldOpc);

/// emitARMRegPlusImmediate / emitT2RegPlusImmediate - Emits a series of
/// instructions to materializea destreg = basereg + immediate in ARM / Thumb2
/// code.
void emitARMRegPlusImmediate(MachineBasicBlock &MBB,
                             MachineBasicBlock::iterator &MBBI,
                             const DebugLoc &dl, Register DestReg,
                             Register BaseReg, int NumBytes,
                             ARMCC::CondCodes Pred, Register PredReg,
                             const ARMBaseInstrInfo &TII, unsigned MIFlags = 0);

void emitT2RegPlusImmediate(MachineBasicBlock &MBB,
                            MachineBasicBlock::iterator &MBBI,
                            const DebugLoc &dl, Register DestReg,
                            Register BaseReg, int NumBytes,
                            ARMCC::CondCodes Pred, Register PredReg,
                            const ARMBaseInstrInfo &TII, unsigned MIFlags = 0);
void emitThumbRegPlusImmediate(MachineBasicBlock &MBB,
                               MachineBasicBlock::iterator &MBBI,
                               const DebugLoc &dl, Register DestReg,
                               Register BaseReg, int NumBytes,
                               const TargetInstrInfo &TII,
                               const ARMBaseRegisterInfo &MRI,
                               unsigned MIFlags = 0);

/// Tries to add registers to the reglist of a given base-updating
/// push/pop instruction to adjust the stack by an additional
/// NumBytes. This can save a few bytes per function in code-size, but
/// obviously generates more memory traffic. As such, it only takes
/// effect in functions being optimised for size.
bool tryFoldSPUpdateIntoPushPop(const ARMSubtarget &Subtarget,
                                MachineFunction &MF, MachineInstr *MI,
                                unsigned NumBytes);

/// rewriteARMFrameIndex / rewriteT2FrameIndex -
/// Rewrite MI to access 'Offset' bytes from the FP. Return false if the
/// offset could not be handled directly in MI, and return the left-over
/// portion by reference.
bool rewriteARMFrameIndex(MachineInstr &MI, unsigned FrameRegIdx,
                          Register FrameReg, int &Offset,
                          const ARMBaseInstrInfo &TII);

bool rewriteT2FrameIndex(MachineInstr &MI, unsigned FrameRegIdx,
                         Register FrameReg, int &Offset,
                         const ARMBaseInstrInfo &TII,
                         const TargetRegisterInfo *TRI);

/// Return true if Reg is defd between From and To
bool registerDefinedBetween(unsigned Reg, MachineBasicBlock::iterator From,
                            MachineBasicBlock::iterator To,
                            const TargetRegisterInfo *TRI);

/// Search backwards from a tBcc to find a tCMPi8 against 0, meaning
/// we can convert them to a tCBZ or tCBNZ. Return nullptr if not found.
MachineInstr *findCMPToFoldIntoCBZ(MachineInstr *Br,
                                   const TargetRegisterInfo *TRI);

void addUnpredicatedMveVpredNOp(MachineInstrBuilder &MIB);
void addUnpredicatedMveVpredROp(MachineInstrBuilder &MIB, Register DestReg);

void addPredicatedMveVpredNOp(MachineInstrBuilder &MIB, unsigned Cond);
void addPredicatedMveVpredROp(MachineInstrBuilder &MIB, unsigned Cond,
                              unsigned Inactive);

/// Returns the number of instructions required to materialize the given
/// constant in a register, or 3 if a literal pool load is needed.
/// If ForCodesize is specified, an approximate cost in bytes is returned.
unsigned ConstantMaterializationCost(unsigned Val,
                                     const ARMSubtarget *Subtarget,
                                     bool ForCodesize = false);

/// Returns true if Val1 has a lower Constant Materialization Cost than Val2.
/// Uses the cost from ConstantMaterializationCost, first with ForCodesize as
/// specified. If the scores are equal, return the comparison for !ForCodesize.
bool HasLowerConstantMaterializationCost(unsigned Val1, unsigned Val2,
                                         const ARMSubtarget *Subtarget,
                                         bool ForCodesize = false);

// Return the immediate if this is ADDri or SUBri, scaled as appropriate.
// Returns 0 for unknown instructions.
inline int getAddSubImmediate(MachineInstr &MI) {
  int Scale = 1;
  unsigned ImmOp;
  switch (MI.getOpcode()) {
  case ARM::t2ADDri:
    ImmOp = 2;
    break;
  case ARM::t2SUBri:
  case ARM::t2SUBri12:
    ImmOp = 2;
    Scale = -1;
    break;
  case ARM::tSUBi3:
  case ARM::tSUBi8:
    ImmOp = 3;
    Scale = -1;
    break;
  default:
    return 0;
  }
  return Scale * MI.getOperand(ImmOp).getImm();
}

// Given a memory access Opcode, check that the give Imm would be a valid Offset
// for this instruction using its addressing mode.
inline bool isLegalAddressImm(unsigned Opcode, int Imm,
                              const TargetInstrInfo *TII) {
  const MCInstrDesc &Desc = TII->get(Opcode);
  unsigned AddrMode = (Desc.TSFlags & ARMII::AddrModeMask);
  switch (AddrMode) {
  case ARMII::AddrModeT2_i7:
    return std::abs(Imm) < ((1 << 7) * 1);
  case ARMII::AddrModeT2_i7s2:
    return std::abs(Imm) < ((1 << 7) * 2) && Imm % 2 == 0;
  case ARMII::AddrModeT2_i7s4:
    return std::abs(Imm) < ((1 << 7) * 4) && Imm % 4 == 0;
  case ARMII::AddrModeT2_i8:
    return std::abs(Imm) < ((1 << 8) * 1);
  case ARMII::AddrModeT2_i8pos:
    return Imm >= 0 && Imm < ((1 << 8) * 1);
  case ARMII::AddrModeT2_i8neg:
    return Imm < 0 && -Imm < ((1 << 8) * 1);
  case ARMII::AddrModeT2_i8s4:
    return std::abs(Imm) < ((1 << 8) * 4) && Imm % 4 == 0;
  case ARMII::AddrModeT2_i12:
    return Imm >= 0 && Imm < ((1 << 12) * 1);
  case ARMII::AddrMode2:
    return std::abs(Imm) < ((1 << 12) * 1);
  default:
    llvm_unreachable("Unhandled Addressing mode");
  }
}

// Return true if the given intrinsic is a gather
inline bool isGather(IntrinsicInst *IntInst) {
  if (IntInst == nullptr)
    return false;
  unsigned IntrinsicID = IntInst->getIntrinsicID();
  return (IntrinsicID == Intrinsic::masked_gather ||
          IntrinsicID == Intrinsic::arm_mve_vldr_gather_base ||
          IntrinsicID == Intrinsic::arm_mve_vldr_gather_base_predicated ||
          IntrinsicID == Intrinsic::arm_mve_vldr_gather_base_wb ||
          IntrinsicID == Intrinsic::arm_mve_vldr_gather_base_wb_predicated ||
          IntrinsicID == Intrinsic::arm_mve_vldr_gather_offset ||
          IntrinsicID == Intrinsic::arm_mve_vldr_gather_offset_predicated);
}

// Return true if the given intrinsic is a scatter
inline bool isScatter(IntrinsicInst *IntInst) {
  if (IntInst == nullptr)
    return false;
  unsigned IntrinsicID = IntInst->getIntrinsicID();
  return (IntrinsicID == Intrinsic::masked_scatter ||
          IntrinsicID == Intrinsic::arm_mve_vstr_scatter_base ||
          IntrinsicID == Intrinsic::arm_mve_vstr_scatter_base_predicated ||
          IntrinsicID == Intrinsic::arm_mve_vstr_scatter_base_wb ||
          IntrinsicID == Intrinsic::arm_mve_vstr_scatter_base_wb_predicated ||
          IntrinsicID == Intrinsic::arm_mve_vstr_scatter_offset ||
          IntrinsicID == Intrinsic::arm_mve_vstr_scatter_offset_predicated);
}

// Return true if the given intrinsic is a gather or scatter
inline bool isGatherScatter(IntrinsicInst *IntInst) {
  if (IntInst == nullptr)
    return false;
  return isGather(IntInst) || isScatter(IntInst);
}

unsigned getBLXOpcode(const MachineFunction &MF);
unsigned gettBLXrOpcode(const MachineFunction &MF);
unsigned getBLXpredOpcode(const MachineFunction &MF);

} // end namespace llvm

#endif // LLVM_LIB_TARGET_ARM_ARMBASEINSTRINFO_H
