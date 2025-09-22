//===-- SystemZInstrInfo.h - SystemZ instruction information ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the SystemZ implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZINSTRINFO_H
#define LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZINSTRINFO_H

#include "SystemZ.h"
#include "SystemZRegisterInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include <cstdint>

#define GET_INSTRINFO_HEADER
#include "SystemZGenInstrInfo.inc"

namespace llvm {

class SystemZSubtarget;

namespace SystemZII {

enum {
  // See comments in SystemZInstrFormats.td.
  SimpleBDXLoad          = (1 << 0),
  SimpleBDXStore         = (1 << 1),
  Has20BitOffset         = (1 << 2),
  HasIndex               = (1 << 3),
  Is128Bit               = (1 << 4),
  AccessSizeMask         = (31 << 5),
  AccessSizeShift        = 5,
  CCValuesMask           = (15 << 10),
  CCValuesShift          = 10,
  CompareZeroCCMaskMask  = (15 << 14),
  CompareZeroCCMaskShift = 14,
  CCMaskFirst            = (1 << 18),
  CCMaskLast             = (1 << 19),
  IsLogical              = (1 << 20),
  CCIfNoSignedWrap       = (1 << 21)
};

static inline unsigned getAccessSize(unsigned int Flags) {
  return (Flags & AccessSizeMask) >> AccessSizeShift;
}

static inline unsigned getCCValues(unsigned int Flags) {
  return (Flags & CCValuesMask) >> CCValuesShift;
}

static inline unsigned getCompareZeroCCMask(unsigned int Flags) {
  return (Flags & CompareZeroCCMaskMask) >> CompareZeroCCMaskShift;
}

// SystemZ MachineOperand target flags.
enum {
  // Masks out the bits for the access model.
  MO_SYMBOL_MODIFIER = (3 << 0),

  // @GOT (aka @GOTENT)
  MO_GOT = (1 << 0),

  // @INDNTPOFF
  MO_INDNTPOFF = (2 << 0)
};

// z/OS XPLink specific: classifies the types of
// accesses to the ADA (Associated Data Area).
// These enums contains values that overlap with the above MO_ enums,
// but that's fine since the above enums are used with ELF,
// while these values are used with z/OS.
enum {
  MO_ADA_DATA_SYMBOL_ADDR = 1,
  MO_ADA_INDIRECT_FUNC_DESC,
  MO_ADA_DIRECT_FUNC_DESC,
};

// Classifies a branch.
enum BranchType {
  // An instruction that branches on the current value of CC.
  BranchNormal,

  // An instruction that peforms a 32-bit signed comparison and branches
  // on the result.
  BranchC,

  // An instruction that peforms a 32-bit unsigned comparison and branches
  // on the result.
  BranchCL,

  // An instruction that peforms a 64-bit signed comparison and branches
  // on the result.
  BranchCG,

  // An instruction that peforms a 64-bit unsigned comparison and branches
  // on the result.
  BranchCLG,

  // An instruction that decrements a 32-bit register and branches if
  // the result is nonzero.
  BranchCT,

  // An instruction that decrements a 64-bit register and branches if
  // the result is nonzero.
  BranchCTG,

  // An instruction representing an asm goto statement.
  AsmGoto
};

// Information about a branch instruction.
class Branch {
  // The target of the branch. In case of INLINEASM_BR, this is nullptr.
  const MachineOperand *Target;

public:
  // The type of the branch.
  BranchType Type;

  // CCMASK_<N> is set if CC might be equal to N.
  unsigned CCValid;

  // CCMASK_<N> is set if the branch should be taken when CC == N.
  unsigned CCMask;

  Branch(BranchType type, unsigned ccValid, unsigned ccMask,
         const MachineOperand *target)
    : Target(target), Type(type), CCValid(ccValid), CCMask(ccMask) {}

  bool isIndirect() { return Target != nullptr && Target->isReg(); }
  bool hasMBBTarget() { return Target != nullptr && Target->isMBB(); }
  MachineBasicBlock *getMBBTarget() {
    return hasMBBTarget() ? Target->getMBB() : nullptr;
  }
};

// Kinds of fused compares in compare-and-* instructions.  Together with type
// of the converted compare, this identifies the compare-and-*
// instruction.
enum FusedCompareType {
  // Relative branch - CRJ etc.
  CompareAndBranch,

  // Indirect branch, used for return - CRBReturn etc.
  CompareAndReturn,

  // Indirect branch, used for sibcall - CRBCall etc.
  CompareAndSibcall,

  // Trap
  CompareAndTrap
};

} // end namespace SystemZII

namespace SystemZ {
int getTwoOperandOpcode(uint16_t Opcode);
int getTargetMemOpcode(uint16_t Opcode);

// Return a version of comparison CC mask CCMask in which the LT and GT
// actions are swapped.
unsigned reverseCCMask(unsigned CCMask);

// Create a new basic block after MBB.
MachineBasicBlock *emitBlockAfter(MachineBasicBlock *MBB);
// Split MBB after MI and return the new block (the one that contains
// instructions after MI).
MachineBasicBlock *splitBlockAfter(MachineBasicBlock::iterator MI,
                                   MachineBasicBlock *MBB);
// Split MBB before MI and return the new block (the one that contains MI).
MachineBasicBlock *splitBlockBefore(MachineBasicBlock::iterator MI,
                                    MachineBasicBlock *MBB);
}

class SystemZInstrInfo : public SystemZGenInstrInfo {
  const SystemZRegisterInfo RI;
  SystemZSubtarget &STI;

  void splitMove(MachineBasicBlock::iterator MI, unsigned NewOpcode) const;
  void splitAdjDynAlloc(MachineBasicBlock::iterator MI) const;
  void expandRIPseudo(MachineInstr &MI, unsigned LowOpcode, unsigned HighOpcode,
                      bool ConvertHigh) const;
  void expandRIEPseudo(MachineInstr &MI, unsigned LowOpcode,
                       unsigned LowOpcodeK, unsigned HighOpcode) const;
  void expandRXYPseudo(MachineInstr &MI, unsigned LowOpcode,
                       unsigned HighOpcode) const;
  void expandLOCPseudo(MachineInstr &MI, unsigned LowOpcode,
                       unsigned HighOpcode) const;
  void expandZExtPseudo(MachineInstr &MI, unsigned LowOpcode,
                        unsigned Size) const;
  void expandLoadStackGuard(MachineInstr *MI) const;

  MachineInstrBuilder
  emitGRX32Move(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
                const DebugLoc &DL, unsigned DestReg, unsigned SrcReg,
                unsigned LowLowOpcode, unsigned Size, bool KillSrc,
                bool UndefSrc) const;

  virtual void anchor();

protected:
  /// Commutes the operands in the given instruction by changing the operands
  /// order and/or changing the instruction's opcode and/or the immediate value
  /// operand.
  ///
  /// The arguments 'CommuteOpIdx1' and 'CommuteOpIdx2' specify the operands
  /// to be commuted.
  ///
  /// Do not call this method for a non-commutable instruction or
  /// non-commutable operands.
  /// Even though the instruction is commutable, the method may still
  /// fail to commute the operands, null pointer is returned in such cases.
  MachineInstr *commuteInstructionImpl(MachineInstr &MI, bool NewMI,
                                       unsigned CommuteOpIdx1,
                                       unsigned CommuteOpIdx2) const override;

public:
  explicit SystemZInstrInfo(SystemZSubtarget &STI);

  // Override TargetInstrInfo.
  Register isLoadFromStackSlot(const MachineInstr &MI,
                               int &FrameIndex) const override;
  Register isStoreToStackSlot(const MachineInstr &MI,
                              int &FrameIndex) const override;
  bool isStackSlotCopy(const MachineInstr &MI, int &DestFrameIndex,
                       int &SrcFrameIndex) const override;
  bool analyzeBranch(MachineBasicBlock &MBB, MachineBasicBlock *&TBB,
                     MachineBasicBlock *&FBB,
                     SmallVectorImpl<MachineOperand> &Cond,
                     bool AllowModify) const override;
  unsigned removeBranch(MachineBasicBlock &MBB,
                        int *BytesRemoved = nullptr) const override;
  unsigned insertBranch(MachineBasicBlock &MBB, MachineBasicBlock *TBB,
                        MachineBasicBlock *FBB, ArrayRef<MachineOperand> Cond,
                        const DebugLoc &DL,
                        int *BytesAdded = nullptr) const override;
  bool analyzeCompare(const MachineInstr &MI, Register &SrcReg,
                      Register &SrcReg2, int64_t &Mask,
                      int64_t &Value) const override;
  bool canInsertSelect(const MachineBasicBlock &, ArrayRef<MachineOperand> Cond,
                       Register, Register, Register, int &, int &,
                       int &) const override;
  void insertSelect(MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
                    const DebugLoc &DL, Register DstReg,
                    ArrayRef<MachineOperand> Cond, Register TrueReg,
                    Register FalseReg) const override;
  MachineInstr *optimizeLoadInstr(MachineInstr &MI,
                                  const MachineRegisterInfo *MRI,
                                  Register &FoldAsLoadDefReg,
                                  MachineInstr *&DefMI) const override;
  bool foldImmediate(MachineInstr &UseMI, MachineInstr &DefMI, Register Reg,
                     MachineRegisterInfo *MRI) const override;

  bool isPredicable(const MachineInstr &MI) const override;
  bool isProfitableToIfCvt(MachineBasicBlock &MBB, unsigned NumCycles,
                           unsigned ExtraPredCycles,
                           BranchProbability Probability) const override;
  bool isProfitableToIfCvt(MachineBasicBlock &TMBB,
                           unsigned NumCyclesT, unsigned ExtraPredCyclesT,
                           MachineBasicBlock &FMBB,
                           unsigned NumCyclesF, unsigned ExtraPredCyclesF,
                           BranchProbability Probability) const override;
  bool isProfitableToDupForIfCvt(MachineBasicBlock &MBB, unsigned NumCycles,
                            BranchProbability Probability) const override;
  bool PredicateInstruction(MachineInstr &MI,
                            ArrayRef<MachineOperand> Pred) const override;
  void copyPhysReg(MachineBasicBlock &MBB, MachineBasicBlock::iterator MBBI,
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
                            int FrameIdx, const TargetRegisterClass *RC,
                            const TargetRegisterInfo *TRI,
                            Register VReg) const override;
  MachineInstr *convertToThreeAddress(MachineInstr &MI, LiveVariables *LV,
                                      LiveIntervals *LIS) const override;

  bool useMachineCombiner() const override { return true; }
  bool isAssociativeAndCommutative(const MachineInstr &Inst,
                                   bool Invert) const override;
  std::optional<unsigned> getInverseOpcode(unsigned Opcode) const override;

  MachineInstr *
  foldMemoryOperandImpl(MachineFunction &MF, MachineInstr &MI,
                        ArrayRef<unsigned> Ops,
                        MachineBasicBlock::iterator InsertPt, int FrameIndex,
                        LiveIntervals *LIS = nullptr,
                        VirtRegMap *VRM = nullptr) const override;
  MachineInstr *foldMemoryOperandImpl(
      MachineFunction &MF, MachineInstr &MI, ArrayRef<unsigned> Ops,
      MachineBasicBlock::iterator InsertPt, MachineInstr &LoadMI,
      LiveIntervals *LIS = nullptr) const override;
  bool expandPostRAPseudo(MachineInstr &MBBI) const override;
  bool reverseBranchCondition(SmallVectorImpl<MachineOperand> &Cond) const
    override;

  // Return the SystemZRegisterInfo, which this class owns.
  const SystemZRegisterInfo &getRegisterInfo() const { return RI; }

  // Return the size in bytes of MI.
  unsigned getInstSizeInBytes(const MachineInstr &MI) const override;

  // Return true if MI is a conditional or unconditional branch.
  // When returning true, set Cond to the mask of condition-code
  // values on which the instruction will branch, and set Target
  // to the operand that contains the branch target.  This target
  // can be a register or a basic block.
  SystemZII::Branch getBranchInfo(const MachineInstr &MI) const;

  // Get the load and store opcodes for a given register class.
  void getLoadStoreOpcodes(const TargetRegisterClass *RC,
                           unsigned &LoadOpcode, unsigned &StoreOpcode) const;

  // Opcode is the opcode of an instruction that has an address operand,
  // and the caller wants to perform that instruction's operation on an
  // address that has displacement Offset.  Return the opcode of a suitable
  // instruction (which might be Opcode itself) or 0 if no such instruction
  // exists.  MI may be passed in order to allow examination of physical
  // register operands (i.e. if a VR32/64 reg ended up as an FP or Vector reg).
  unsigned getOpcodeForOffset(unsigned Opcode, int64_t Offset,
                              const MachineInstr *MI = nullptr) const;

  // Return true if Opcode has a mapping in 12 <-> 20 bit displacements.
  bool hasDisplacementPairInsn(unsigned Opcode) const;

  // If Opcode is a load instruction that has a LOAD AND TEST form,
  // return the opcode for the testing form, otherwise return 0.
  unsigned getLoadAndTest(unsigned Opcode) const;

  // Return true if ROTATE AND ... SELECTED BITS can be used to select bits
  // Mask of the R2 operand, given that only the low BitSize bits of Mask are
  // significant.  Set Start and End to the I3 and I4 operands if so.
  bool isRxSBGMask(uint64_t Mask, unsigned BitSize,
                   unsigned &Start, unsigned &End) const;

  // If Opcode is a COMPARE opcode for which an associated fused COMPARE AND *
  // operation exists, return the opcode for the latter, otherwise return 0.
  // MI, if nonnull, is the compare instruction.
  unsigned getFusedCompare(unsigned Opcode,
                           SystemZII::FusedCompareType Type,
                           const MachineInstr *MI = nullptr) const;

  // Try to find all CC users of the compare instruction (MBBI) and update
  // all of them to maintain equivalent behavior after swapping the compare
  // operands. Return false if not all users can be conclusively found and
  // handled. The compare instruction is *not* changed.
  bool prepareCompareSwapOperands(MachineBasicBlock::iterator MBBI) const;

  // If Opcode is a LOAD opcode for with an associated LOAD AND TRAP
  // operation exists, returh the opcode for the latter, otherwise return 0.
  unsigned getLoadAndTrap(unsigned Opcode) const;

  // Emit code before MBBI in MI to move immediate value Value into
  // physical register Reg.
  void loadImmediate(MachineBasicBlock &MBB,
                     MachineBasicBlock::iterator MBBI,
                     unsigned Reg, uint64_t Value) const;

  // Perform target specific instruction verification.
  bool verifyInstruction(const MachineInstr &MI,
                         StringRef &ErrInfo) const override;

  // Sometimes, it is possible for the target to tell, even without
  // aliasing information, that two MIs access different memory
  // addresses. This function returns true if two MIs access different
  // memory addresses and false otherwise.
  bool
  areMemAccessesTriviallyDisjoint(const MachineInstr &MIa,
                                  const MachineInstr &MIb) const override;

  bool getConstValDefinedInReg(const MachineInstr &MI, const Register Reg,
                               int64_t &ImmVal) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_SYSTEMZ_SYSTEMZINSTRINFO_H
