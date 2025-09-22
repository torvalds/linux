//===-- X86InstrInfo.h - X86 Instruction Information ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the X86 implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_X86_X86INSTRINFO_H
#define LLVM_LIB_TARGET_X86_X86INSTRINFO_H

#include "MCTargetDesc/X86BaseInfo.h"
#include "X86InstrFMA3Info.h"
#include "X86RegisterInfo.h"
#include "llvm/CodeGen/ISDOpcodes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include <vector>

#define GET_INSTRINFO_HEADER
#include "X86GenInstrInfo.inc"

namespace llvm {
class X86Subtarget;

// X86 MachineCombiner patterns
enum X86MachineCombinerPattern : unsigned {
  // X86 VNNI
  DPWSSD = MachineCombinerPattern::TARGET_PATTERN_START,
};

namespace X86 {

enum AsmComments {
  // For instr that was compressed from EVEX to LEGACY.
  AC_EVEX_2_LEGACY = MachineInstr::TAsmComments,
  // For instr that was compressed from EVEX to VEX.
  AC_EVEX_2_VEX = AC_EVEX_2_LEGACY << 1,
  // For instr that was compressed from EVEX to EVEX.
  AC_EVEX_2_EVEX = AC_EVEX_2_VEX << 1
};

/// Return a pair of condition code for the given predicate and whether
/// the instruction operands should be swaped to match the condition code.
std::pair<CondCode, bool> getX86ConditionCode(CmpInst::Predicate Predicate);

/// Return a cmov opcode for the given register size in bytes, and operand type.
unsigned getCMovOpcode(unsigned RegBytes, bool HasMemoryOperand = false,
                       bool HasNDD = false);

/// Return the source operand # for condition code by \p MCID. If the
/// instruction doesn't have a condition code, return -1.
int getCondSrcNoFromDesc(const MCInstrDesc &MCID);

/// Return the condition code of the instruction. If the instruction doesn't
/// have a condition code, return X86::COND_INVALID.
CondCode getCondFromMI(const MachineInstr &MI);

// Turn JCC instruction into condition code.
CondCode getCondFromBranch(const MachineInstr &MI);

// Turn SETCC instruction into condition code.
CondCode getCondFromSETCC(const MachineInstr &MI);

// Turn CMOV instruction into condition code.
CondCode getCondFromCMov(const MachineInstr &MI);

// Turn CFCMOV instruction into condition code.
CondCode getCondFromCFCMov(const MachineInstr &MI);

// Turn CCMP instruction into condition code.
CondCode getCondFromCCMP(const MachineInstr &MI);

// Turn condition code into condition flags for CCMP/CTEST.
int getCCMPCondFlagsFromCondCode(CondCode CC);

// Get the opcode of corresponding NF variant.
unsigned getNFVariant(unsigned Opc);

// Get the opcode of corresponding NonND variant.
unsigned getNonNDVariant(unsigned Opc);

/// GetOppositeBranchCondition - Return the inverse of the specified cond,
/// e.g. turning COND_E to COND_NE.
CondCode GetOppositeBranchCondition(CondCode CC);

/// Get the VPCMP immediate for the given condition.
unsigned getVPCMPImmForCond(ISD::CondCode CC);

/// Get the VPCMP immediate if the opcodes are swapped.
unsigned getSwappedVPCMPImm(unsigned Imm);

/// Get the VPCOM immediate if the opcodes are swapped.
unsigned getSwappedVPCOMImm(unsigned Imm);

/// Get the VCMP immediate if the opcodes are swapped.
unsigned getSwappedVCMPImm(unsigned Imm);

/// Get the width of the vector register operand.
unsigned getVectorRegisterWidth(const MCOperandInfo &Info);

/// Check if the instruction is X87 instruction.
bool isX87Instruction(MachineInstr &MI);

/// Return the index of the instruction's first address operand, if it has a
/// memory reference, or -1 if it has none. Unlike X86II::getMemoryOperandNo(),
/// this also works for both pseudo instructions (e.g., TCRETURNmi) as well as
/// real instructions (e.g., JMP64m).
int getFirstAddrOperandIdx(const MachineInstr &MI);

/// Find any constant pool entry associated with a specific instruction operand.
const Constant *getConstantFromPool(const MachineInstr &MI, unsigned OpNo);

} // namespace X86

/// isGlobalStubReference - Return true if the specified TargetFlag operand is
/// a reference to a stub for a global, not the global itself.
inline static bool isGlobalStubReference(unsigned char TargetFlag) {
  switch (TargetFlag) {
  case X86II::MO_DLLIMPORT:               // dllimport stub.
  case X86II::MO_GOTPCREL:                // rip-relative GOT reference.
  case X86II::MO_GOTPCREL_NORELAX:        // rip-relative GOT reference.
  case X86II::MO_GOT:                     // normal GOT reference.
  case X86II::MO_DARWIN_NONLAZY_PIC_BASE: // Normal $non_lazy_ptr ref.
  case X86II::MO_DARWIN_NONLAZY:          // Normal $non_lazy_ptr ref.
  case X86II::MO_COFFSTUB:                // COFF .refptr stub.
    return true;
  default:
    return false;
  }
}

/// isGlobalRelativeToPICBase - Return true if the specified global value
/// reference is relative to a 32-bit PIC base (X86ISD::GlobalBaseReg).  If this
/// is true, the addressing mode has the PIC base register added in (e.g. EBX).
inline static bool isGlobalRelativeToPICBase(unsigned char TargetFlag) {
  switch (TargetFlag) {
  case X86II::MO_GOTOFF:                  // isPICStyleGOT: local global.
  case X86II::MO_GOT:                     // isPICStyleGOT: other global.
  case X86II::MO_PIC_BASE_OFFSET:         // Darwin local global.
  case X86II::MO_DARWIN_NONLAZY_PIC_BASE: // Darwin/32 external global.
  case X86II::MO_TLVP:                    // ??? Pretty sure..
    return true;
  default:
    return false;
  }
}

inline static bool isScale(const MachineOperand &MO) {
  return MO.isImm() && (MO.getImm() == 1 || MO.getImm() == 2 ||
                        MO.getImm() == 4 || MO.getImm() == 8);
}

inline static bool isLeaMem(const MachineInstr &MI, unsigned Op) {
  if (MI.getOperand(Op).isFI())
    return true;
  return Op + X86::AddrSegmentReg <= MI.getNumOperands() &&
         MI.getOperand(Op + X86::AddrBaseReg).isReg() &&
         isScale(MI.getOperand(Op + X86::AddrScaleAmt)) &&
         MI.getOperand(Op + X86::AddrIndexReg).isReg() &&
         (MI.getOperand(Op + X86::AddrDisp).isImm() ||
          MI.getOperand(Op + X86::AddrDisp).isGlobal() ||
          MI.getOperand(Op + X86::AddrDisp).isCPI() ||
          MI.getOperand(Op + X86::AddrDisp).isJTI());
}

inline static bool isMem(const MachineInstr &MI, unsigned Op) {
  if (MI.getOperand(Op).isFI())
    return true;
  return Op + X86::AddrNumOperands <= MI.getNumOperands() &&
         MI.getOperand(Op + X86::AddrSegmentReg).isReg() && isLeaMem(MI, Op);
}

class X86InstrInfo final : public X86GenInstrInfo {
  X86Subtarget &Subtarget;
  const X86RegisterInfo RI;

  virtual void anchor();

  bool analyzeBranchImpl(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                         MachineBasicBlock *&FBB,
                         SmallVectorImpl<MachineOperand> &Cond,
                         SmallVectorImpl<MachineInstr *> &CondBranches,
                         bool AllowModify) const;

  bool foldImmediateImpl(MachineInstr &UseMI, MachineInstr *DefMI, Register Reg,
                         int64_t ImmVal, MachineRegisterInfo *MRI,
                         bool MakeChange) const;

public:
  explicit X86InstrInfo(X86Subtarget &STI);

  /// Given a machine instruction descriptor, returns the register
  /// class constraint for OpNum, or NULL. Returned register class
  /// may be different from the definition in the TD file, e.g.
  /// GR*RegClass (definition in TD file)
  /// ->
  /// GR*_NOREX2RegClass (Returned register class)
  const TargetRegisterClass *
  getRegClass(const MCInstrDesc &MCID, unsigned OpNum,
              const TargetRegisterInfo *TRI,
              const MachineFunction &MF) const override;

  /// getRegisterInfo - TargetInstrInfo is a superset of MRegister info.  As
  /// such, whenever a client has an instance of instruction info, it should
  /// always be able to get register info as well (through this method).
  ///
  const X86RegisterInfo &getRegisterInfo() const { return RI; }

  /// Returns the stack pointer adjustment that happens inside the frame
  /// setup..destroy sequence (e.g. by pushes, or inside the callee).
  int64_t getFrameAdjustment(const MachineInstr &I) const {
    assert(isFrameInstr(I));
    if (isFrameSetup(I))
      return I.getOperand(2).getImm();
    return I.getOperand(1).getImm();
  }

  /// Sets the stack pointer adjustment made inside the frame made up by this
  /// instruction.
  void setFrameAdjustment(MachineInstr &I, int64_t V) const {
    assert(isFrameInstr(I));
    if (isFrameSetup(I))
      I.getOperand(2).setImm(V);
    else
      I.getOperand(1).setImm(V);
  }

  /// getSPAdjust - This returns the stack pointer adjustment made by
  /// this instruction. For x86, we need to handle more complex call
  /// sequences involving PUSHes.
  int getSPAdjust(const MachineInstr &MI) const override;

  /// isCoalescableExtInstr - Return true if the instruction is a "coalescable"
  /// extension instruction. That is, it's like a copy where it's legal for the
  /// source to overlap the destination. e.g. X86::MOVSX64rr32. If this returns
  /// true, then it's expected the pre-extension value is available as a subreg
  /// of the result register. This also returns the sub-register index in
  /// SubIdx.
  bool isCoalescableExtInstr(const MachineInstr &MI, Register &SrcReg,
                             Register &DstReg, unsigned &SubIdx) const override;

  /// Returns true if the instruction has no behavior (specified or otherwise)
  /// that is based on the value of any of its register operands
  ///
  /// Instructions are considered data invariant even if they set EFLAGS.
  ///
  /// A classical example of something that is inherently not data invariant is
  /// an indirect jump -- the destination is loaded into icache based on the
  /// bits set in the jump destination register.
  ///
  /// FIXME: This should become part of our instruction tables.
  static bool isDataInvariant(MachineInstr &MI);

  /// Returns true if the instruction has no behavior (specified or otherwise)
  /// that is based on the value loaded from memory or the value of any
  /// non-address register operands.
  ///
  /// For example, if the latency of the instruction is dependent on the
  /// particular bits set in any of the registers *or* any of the bits loaded
  /// from memory.
  ///
  /// Instructions are considered data invariant even if they set EFLAGS.
  ///
  /// A classical example of something that is inherently not data invariant is
  /// an indirect jump -- the destination is loaded into icache based on the
  /// bits set in the jump destination register.
  ///
  /// FIXME: This should become part of our instruction tables.
  static bool isDataInvariantLoad(MachineInstr &MI);

  Register isLoadFromStackSlot(const MachineInstr &MI,
                               int &FrameIndex) const override;
  Register isLoadFromStackSlot(const MachineInstr &MI,
                               int &FrameIndex,
                               unsigned &MemBytes) const override;
  /// isLoadFromStackSlotPostFE - Check for post-frame ptr elimination
  /// stack locations as well.  This uses a heuristic so it isn't
  /// reliable for correctness.
  Register isLoadFromStackSlotPostFE(const MachineInstr &MI,
                                     int &FrameIndex) const override;

  Register isStoreToStackSlot(const MachineInstr &MI,
                              int &FrameIndex) const override;
  Register isStoreToStackSlot(const MachineInstr &MI,
                              int &FrameIndex,
                              unsigned &MemBytes) const override;
  /// isStoreToStackSlotPostFE - Check for post-frame ptr elimination
  /// stack locations as well.  This uses a heuristic so it isn't
  /// reliable for correctness.
  Register isStoreToStackSlotPostFE(const MachineInstr &MI,
                                    int &FrameIndex) const override;

  bool isReallyTriviallyReMaterializable(const MachineInstr &MI) const override;
  void reMaterialize(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                     Register DestReg, unsigned SubIdx,
                     const MachineInstr &Orig,
                     const TargetRegisterInfo &TRI) const override;

  /// Given an operand within a MachineInstr, insert preceding code to put it
  /// into the right format for a particular kind of LEA instruction. This may
  /// involve using an appropriate super-register instead (with an implicit use
  /// of the original) or creating a new virtual register and inserting COPY
  /// instructions to get the data into the right class.
  ///
  /// Reference parameters are set to indicate how caller should add this
  /// operand to the LEA instruction.
  bool classifyLEAReg(MachineInstr &MI, const MachineOperand &Src,
                      unsigned LEAOpcode, bool AllowSP, Register &NewSrc,
                      bool &isKill, MachineOperand &ImplicitOp,
                      LiveVariables *LV, LiveIntervals *LIS) const;

  /// convertToThreeAddress - This method must be implemented by targets that
  /// set the M_CONVERTIBLE_TO_3_ADDR flag.  When this flag is set, the target
  /// may be able to convert a two-address instruction into a true
  /// three-address instruction on demand.  This allows the X86 target (for
  /// example) to convert ADD and SHL instructions into LEA instructions if they
  /// would require register copies due to two-addressness.
  ///
  /// This method returns a null pointer if the transformation cannot be
  /// performed, otherwise it returns the new instruction.
  ///
  MachineInstr *convertToThreeAddress(MachineInstr &MI, LiveVariables *LV,
                                      LiveIntervals *LIS) const override;

  /// Returns true iff the routine could find two commutable operands in the
  /// given machine instruction.
  /// The 'SrcOpIdx1' and 'SrcOpIdx2' are INPUT and OUTPUT arguments. Their
  /// input values can be re-defined in this method only if the input values
  /// are not pre-defined, which is designated by the special value
  /// 'CommuteAnyOperandIndex' assigned to it.
  /// If both of indices are pre-defined and refer to some operands, then the
  /// method simply returns true if the corresponding operands are commutable
  /// and returns false otherwise.
  ///
  /// For example, calling this method this way:
  ///     unsigned Op1 = 1, Op2 = CommuteAnyOperandIndex;
  ///     findCommutedOpIndices(MI, Op1, Op2);
  /// can be interpreted as a query asking to find an operand that would be
  /// commutable with the operand#1.
  bool findCommutedOpIndices(const MachineInstr &MI, unsigned &SrcOpIdx1,
                             unsigned &SrcOpIdx2) const override;

  /// Returns true if we have preference on the operands order in MI, the
  /// commute decision is returned in Commute.
  bool hasCommutePreference(MachineInstr &MI, bool &Commute) const override;

  /// Returns an adjusted FMA opcode that must be used in FMA instruction that
  /// performs the same computations as the given \p MI but which has the
  /// operands \p SrcOpIdx1 and \p SrcOpIdx2 commuted.
  /// It may return 0 if it is unsafe to commute the operands.
  /// Note that a machine instruction (instead of its opcode) is passed as the
  /// first parameter to make it possible to analyze the instruction's uses and
  /// commute the first operand of FMA even when it seems unsafe when you look
  /// at the opcode. For example, it is Ok to commute the first operand of
  /// VFMADD*SD_Int, if ONLY the lowest 64-bit element of the result is used.
  ///
  /// The returned FMA opcode may differ from the opcode in the given \p MI.
  /// For example, commuting the operands #1 and #3 in the following FMA
  ///     FMA213 #1, #2, #3
  /// results into instruction with adjusted opcode:
  ///     FMA231 #3, #2, #1
  unsigned
  getFMA3OpcodeToCommuteOperands(const MachineInstr &MI, unsigned SrcOpIdx1,
                                 unsigned SrcOpIdx2,
                                 const X86InstrFMA3Group &FMA3Group) const;

  // Branch analysis.
  bool isUnconditionalTailCall(const MachineInstr &MI) const override;
  bool canMakeTailCallConditional(SmallVectorImpl<MachineOperand> &Cond,
                                  const MachineInstr &TailCall) const override;
  void replaceBranchWithTailCall(MachineBasicBlock &MBB,
                                 SmallVectorImpl<MachineOperand> &Cond,
                                 const MachineInstr &TailCall) const override;

  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify) const override;

  int getJumpTableIndex(const MachineInstr &MI) const override;

  std::optional<ExtAddrMode>
  getAddrModeFromMemoryOp(const MachineInstr &MemI,
                          const TargetRegisterInfo *TRI) const override;

  bool getConstValDefinedInReg(const MachineInstr &MI, const Register Reg,
                               int64_t &ImmVal) const override;

  bool preservesZeroValueInReg(const MachineInstr *MI,
                               const Register NullValueReg,
                               const TargetRegisterInfo *TRI) const override;

  bool getMemOperandsWithOffsetWidth(
      const MachineInstr &LdSt,
      SmallVectorImpl<const MachineOperand *> &BaseOps, int64_t &Offset,
      bool &OffsetIsScalable, LocationSize &Width,
      const TargetRegisterInfo *TRI) const override;
  bool analyzeBranchPredicate(MachineBasicBlock &MBB,
                              TargetInstrInfo::MachineBranchPredicate &MBP,
                              bool AllowModify = false) const override;

  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;
  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;
  bool canInsertSelect(const MachineBasicBlock &, ArrayRef<MachineOperand> Cond,
                       Register, Register, Register, int &, int &,
                       int &) const override;
  void insertSelect(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                    const DebugLoc &DL, Register DstReg,
                    ArrayRef<MachineOperand> Cond, Register TrueReg,
                    Register FalseReg) const override;
  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                   const DebugLoc &DL, MCRegister DestReg, MCRegister SrcReg,
                   bool KillSrc) const override;
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

  void loadStoreTileReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                        unsigned Opc, Register Reg, int FrameIdx,
                        bool isKill = false) const;

  bool expandPostRAPseudo(MachineInstr &MI) const override;

  /// Check whether the target can fold a load that feeds a subreg operand
  /// (or a subreg operand that feeds a store).
  bool isSubregFoldable() const override { return true; }

  /// Fold a load or store of the specified stack slot into the specified
  /// machine instruction for the specified operand(s). If folding happens, it
  /// is likely that the referenced instruction has been changed.
  ///
  /// \returns true on success.
  MachineInstr *
  foldMemoryOperandImpl(MachineFunction &MF, MachineInstr &MI,
                        ArrayRef<unsigned> Ops,
                        MachineBasicBlock::iterator InsertPt, int FrameIndex,
                        LiveIntervals *LIS = nullptr,
                        VirtRegMap *VRM = nullptr) const override;

  /// Same as the previous version except it allows folding of any load and
  /// store from / to any address, not just from a specific stack slot.
  MachineInstr *foldMemoryOperandImpl(
      MachineFunction &MF, MachineInstr &MI, ArrayRef<unsigned> Ops,
      MachineBasicBlock::iterator InsertPt, MachineInstr &LoadMI,
      LiveIntervals *LIS = nullptr) const override;

  bool
  unfoldMemoryOperand(MachineFunction &MF, MachineInstr &MI, unsigned Reg,
                      bool UnfoldLoad, bool UnfoldStore,
                      SmallVectorImpl<MachineInstr *> &NewMIs) const override;

  bool unfoldMemoryOperand(SelectionDAG &DAG, SDNode *N,
                           SmallVectorImpl<SDNode *> &NewNodes) const override;

  unsigned
  getOpcodeAfterMemoryUnfold(unsigned Opc, bool UnfoldLoad, bool UnfoldStore,
                             unsigned *LoadRegIndex = nullptr) const override;

  bool areLoadsFromSameBasePtr(SDNode *Load1, SDNode *Load2, int64_t &Offset1,
                               int64_t &Offset2) const override;

  /// Overrides the isSchedulingBoundary from Codegen/TargetInstrInfo.cpp to
  /// make it capable of identifying ENDBR intructions and prevent it from being
  /// re-scheduled.
  bool isSchedulingBoundary(const MachineInstr &MI,
                            const MachineBasicBlock *MBB,
                            const MachineFunction &MF) const override;

  /// This is a used by the pre-regalloc scheduler to determine (in conjunction
  /// with areLoadsFromSameBasePtr) if two loads should be scheduled togther. On
  /// some targets if two loads are loading from addresses in the same cache
  /// line, it's better if they are scheduled together. This function takes two
  /// integers that represent the load offsets from the common base address. It
  /// returns true if it decides it's desirable to schedule the two loads
  /// together. "NumLoads" is the number of loads that have already been
  /// scheduled after Load1.
  bool shouldScheduleLoadsNear(SDNode *Load1, SDNode *Load2, int64_t Offset1,
                               int64_t Offset2,
                               unsigned NumLoads) const override;

  void insertNoop(MachineBasicBlock &MBB,
                  MachineBasicBlock::iterator MI) const override;

  MCInst getNop() const override;

  bool
  reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const override;

  bool isSafeToMoveRegClassDefs(const TargetRegisterClass *RC) const override;

  /// True if MI has a condition code def, e.g. EFLAGS, that is
  /// not marked dead.
  bool hasLiveCondCodeDef(MachineInstr &MI) const;

  /// getGlobalBaseReg - Return a virtual register initialized with the
  /// the global base register value. Output instructions required to
  /// initialize the register in the function entry block, if necessary.
  ///
  unsigned getGlobalBaseReg(MachineFunction *MF) const;

  std::pair<uint16_t, uint16_t>
  getExecutionDomain(const MachineInstr &MI) const override;

  uint16_t getExecutionDomainCustom(const MachineInstr &MI) const;

  void setExecutionDomain(MachineInstr &MI, unsigned Domain) const override;

  bool setExecutionDomainCustom(MachineInstr &MI, unsigned Domain) const;

  unsigned
  getPartialRegUpdateClearance(const MachineInstr &MI, unsigned OpNum,
                               const TargetRegisterInfo *TRI) const override;
  unsigned getUndefRegClearance(const MachineInstr &MI, unsigned OpNum,
                                const TargetRegisterInfo *TRI) const override;
  void breakPartialRegDependency(MachineInstr &MI, unsigned OpNum,
                                 const TargetRegisterInfo *TRI) const override;

  MachineInstr *foldMemoryOperandImpl(MachineFunction &MF, MachineInstr &MI,
                                      unsigned OpNum,
                                      ArrayRef<MachineOperand> MOs,
                                      MachineBasicBlock::iterator InsertPt,
                                      unsigned Size, Align Alignment,
                                      bool AllowCommute) const;

  bool isHighLatencyDef(int opc) const override;

  bool hasHighOperandLatency(const TargetSchedModel &SchedModel,
                             const MachineRegisterInfo *MRI,
                             const MachineInstr &DefMI, unsigned DefIdx,
                             const MachineInstr &UseMI,
                             unsigned UseIdx) const override;

  bool useMachineCombiner() const override { return true; }

  bool isAssociativeAndCommutative(const MachineInstr &Inst,
                                   bool Invert) const override;

  bool hasReassociableOperands(const MachineInstr &Inst,
                               const MachineBasicBlock *MBB) const override;

  void setSpecialOperandAttr(MachineInstr &OldMI1, MachineInstr &OldMI2,
                             MachineInstr &NewMI1,
                             MachineInstr &NewMI2) const override;

  bool analyzeCompare(const MachineInstr &MI, Register &SrcReg,
                      Register &SrcReg2, int64_t &CmpMask,
                      int64_t &CmpValue) const override;

  /// Check if there exists an earlier instruction that operates on the same
  /// source operands and sets eflags in the same way as CMP and remove CMP if
  /// possible.
  bool optimizeCompareInstr(MachineInstr &CmpInstr, Register SrcReg,
                            Register SrcReg2, int64_t CmpMask, int64_t CmpValue,
                            const MachineRegisterInfo *MRI) const override;

  MachineInstr *optimizeLoadInstr(MachineInstr &MI,
                                  const MachineRegisterInfo *MRI,
                                  Register &FoldAsLoadDefReg,
                                  MachineInstr *&DefMI) const override;

  bool foldImmediate(MachineInstr &UseMI, MachineInstr &DefMI, Register Reg,
                     MachineRegisterInfo *MRI) const override;

  std::pair<unsigned, unsigned>
  decomposeMachineOperandsTargetFlags(unsigned TF) const override;

  ArrayRef<std::pair<unsigned, const char *>>
  getSerializableDirectMachineOperandTargetFlags() const override;

  std::optional<outliner::OutlinedFunction> getOutliningCandidateInfo(
      std::vector<outliner::Candidate> &RepeatedSequenceLocs) const override;

  bool isFunctionSafeToOutlineFrom(MachineFunction &MF,
                                   bool OutlineFromLinkOnceODRs) const override;

  outliner::InstrType
  getOutliningTypeImpl(MachineBasicBlock::iterator &MIT, unsigned Flags) const override;

  void buildOutlinedFrame(MachineBasicBlock &MBB, MachineFunction &MF,
                          const outliner::OutlinedFunction &OF) const override;

  MachineBasicBlock::iterator
  insertOutlinedCall(Module &M, MachineBasicBlock &MBB,
                     MachineBasicBlock::iterator &It, MachineFunction &MF,
                     outliner::Candidate &C) const override;

  void buildClearRegister(Register Reg, MachineBasicBlock &MBB,
                          MachineBasicBlock::iterator Iter, DebugLoc &DL,
                          bool AllowSideEffects = true) const override;

  bool verifyInstruction(const MachineInstr &MI,
                         StringRef &ErrInfo) const override;
#define GET_INSTRINFO_HELPER_DECLS
#include "X86GenInstrInfo.inc"

  static bool hasLockPrefix(const MachineInstr &MI) {
    return MI.getDesc().TSFlags & X86II::LOCK;
  }

  std::optional<ParamLoadedValue>
  describeLoadedValue(const MachineInstr &MI, Register Reg) const override;

protected:
  MachineInstr *commuteInstructionImpl(MachineInstr &MI, bool NewMI,
                                       unsigned CommuteOpIdx1,
                                       unsigned CommuteOpIdx2) const override;

  std::optional<DestSourcePair>
  isCopyInstrImpl(const MachineInstr &MI) const override;

  bool getMachineCombinerPatterns(MachineInstr &Root,
                                  SmallVectorImpl<unsigned> &Patterns,
                                  bool DoRegPressureReduce) const override;

  /// When getMachineCombinerPatterns() finds potential patterns,
  /// this function generates the instructions that could replace the
  /// original code sequence.
  void genAlternativeCodeSequence(
      MachineInstr &Root, unsigned Pattern,
      SmallVectorImpl<MachineInstr *> &InsInstrs,
      SmallVectorImpl<MachineInstr *> &DelInstrs,
      DenseMap<unsigned, unsigned> &InstrIdxForVirtReg) const override;

  /// When calculate the latency of the root instruction, accumulate the
  /// latency of the sequence to the root latency.
  /// \param Root - Instruction that could be combined with one of its operands
  /// For X86 instruction (vpmaddwd + vpmaddwd) -> vpdpwssd, the vpmaddwd
  /// is not in the critical path, so the root latency only include vpmaddwd.
  bool accumulateInstrSeqToRootLatency(MachineInstr &Root) const override {
    return false;
  }

  void getFrameIndexOperands(SmallVectorImpl<MachineOperand> &Ops,
                             int FI) const override;

private:
  /// This is a helper for convertToThreeAddress for 8 and 16-bit instructions.
  /// We use 32-bit LEA to form 3-address code by promoting to a 32-bit
  /// super-register and then truncating back down to a 8/16-bit sub-register.
  MachineInstr *convertToThreeAddressWithLEA(unsigned MIOpc, MachineInstr &MI,
                                             LiveVariables *LV,
                                             LiveIntervals *LIS,
                                             bool Is8BitOp) const;

  /// Handles memory folding for special case instructions, for instance those
  /// requiring custom manipulation of the address.
  MachineInstr *foldMemoryOperandCustom(MachineFunction &MF, MachineInstr &MI,
                                        unsigned OpNum,
                                        ArrayRef<MachineOperand> MOs,
                                        MachineBasicBlock::iterator InsertPt,
                                        unsigned Size, Align Alignment) const;

  MachineInstr *foldMemoryBroadcast(MachineFunction &MF, MachineInstr &MI,
                                    unsigned OpNum,
                                    ArrayRef<MachineOperand> MOs,
                                    MachineBasicBlock::iterator InsertPt,
                                    unsigned BitsSize, bool AllowCommute) const;

  /// isFrameOperand - Return true and the FrameIndex if the specified
  /// operand and follow operands form a reference to the stack frame.
  bool isFrameOperand(const MachineInstr &MI, unsigned int Op,
                      int &FrameIndex) const;

  /// Returns true iff the routine could find two commutable operands in the
  /// given machine instruction with 3 vector inputs.
  /// The 'SrcOpIdx1' and 'SrcOpIdx2' are INPUT and OUTPUT arguments. Their
  /// input values can be re-defined in this method only if the input values
  /// are not pre-defined, which is designated by the special value
  /// 'CommuteAnyOperandIndex' assigned to it.
  /// If both of indices are pre-defined and refer to some operands, then the
  /// method simply returns true if the corresponding operands are commutable
  /// and returns false otherwise.
  ///
  /// For example, calling this method this way:
  ///     unsigned Op1 = 1, Op2 = CommuteAnyOperandIndex;
  ///     findThreeSrcCommutedOpIndices(MI, Op1, Op2);
  /// can be interpreted as a query asking to find an operand that would be
  /// commutable with the operand#1.
  ///
  /// If IsIntrinsic is set, operand 1 will be ignored for commuting.
  bool findThreeSrcCommutedOpIndices(const MachineInstr &MI,
                                     unsigned &SrcOpIdx1,
                                     unsigned &SrcOpIdx2,
                                     bool IsIntrinsic = false) const;

  /// Returns true when instruction \p FlagI produces the same flags as \p OI.
  /// The caller should pass in the results of calling analyzeCompare on \p OI:
  /// \p SrcReg, \p SrcReg2, \p ImmMask, \p ImmValue.
  /// If the flags match \p OI as if it had the input operands swapped then the
  /// function succeeds and sets \p IsSwapped to true.
  ///
  /// Examples of OI, FlagI pairs returning true:
  ///   CMP %1, 42   and  CMP %1, 42
  ///   CMP %1, %2   and  %3 = SUB %1, %2
  ///   TEST %1, %1  and  %2 = SUB %1, 0
  ///   CMP %1, %2   and  %3 = SUB %2, %1  ; IsSwapped=true
  bool isRedundantFlagInstr(const MachineInstr &FlagI, Register SrcReg,
                            Register SrcReg2, int64_t ImmMask, int64_t ImmValue,
                            const MachineInstr &OI, bool *IsSwapped,
                            int64_t *ImmDelta) const;

  /// Commute operands of \p MI for memory fold.
  ///
  /// \param Idx1 the index of operand to be commuted.
  ///
  /// \returns the index of operand that is commuted with \p Idx1. If the method
  /// fails to commute the operands, it will return \p Idx1.
  unsigned commuteOperandsForFold(MachineInstr &MI, unsigned Idx1) const;
};
} // namespace llvm

#endif
