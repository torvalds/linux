//===-- llvm/CodeGen/MachineOperand.h - MachineOperand class ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MachineOperand class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEOPERAND_H
#define LLVM_CODEGEN_MACHINEOPERAND_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/IR/Intrinsics.h"
#include <cassert>

namespace llvm {

class LLT;
class BlockAddress;
class Constant;
class ConstantFP;
class ConstantInt;
class GlobalValue;
class MachineBasicBlock;
class MachineInstr;
class MachineRegisterInfo;
class MCCFIInstruction;
class MDNode;
class ModuleSlotTracker;
class TargetIntrinsicInfo;
class TargetRegisterInfo;
class hash_code;
class raw_ostream;
class MCSymbol;

/// MachineOperand class - Representation of each machine instruction operand.
///
/// This class isn't a POD type because it has a private constructor, but its
/// destructor must be trivial. Functions like MachineInstr::addOperand(),
/// MachineRegisterInfo::moveOperands(), and MF::DeleteMachineInstr() depend on
/// not having to call the MachineOperand destructor.
///
class MachineOperand {
public:
  enum MachineOperandType : unsigned char {
    MO_Register,          ///< Register operand.
    MO_Immediate,         ///< Immediate operand
    MO_CImmediate,        ///< Immediate >64bit operand
    MO_FPImmediate,       ///< Floating-point immediate operand
    MO_MachineBasicBlock, ///< MachineBasicBlock reference
    MO_FrameIndex,        ///< Abstract Stack Frame Index
    MO_ConstantPoolIndex, ///< Address of indexed Constant in Constant Pool
    MO_TargetIndex,       ///< Target-dependent index+offset operand.
    MO_JumpTableIndex,    ///< Address of indexed Jump Table for switch
    MO_ExternalSymbol,    ///< Name of external global symbol
    MO_GlobalAddress,     ///< Address of a global value
    MO_BlockAddress,      ///< Address of a basic block
    MO_RegisterMask,      ///< Mask of preserved registers.
    MO_RegisterLiveOut,   ///< Mask of live-out registers.
    MO_Metadata,          ///< Metadata reference (for debug info)
    MO_MCSymbol,          ///< MCSymbol reference (for debug/eh info)
    MO_CFIIndex,          ///< MCCFIInstruction index.
    MO_IntrinsicID,       ///< Intrinsic ID for ISel
    MO_Predicate,         ///< Generic predicate for ISel
    MO_ShuffleMask,       ///< Other IR Constant for ISel (shuffle masks)
    MO_DbgInstrRef, ///< Integer indices referring to an instruction+operand
    MO_Last = MO_DbgInstrRef
  };

private:
  /// OpKind - Specify what kind of operand this is.  This discriminates the
  /// union.
  unsigned OpKind : 8;

  /// Subregister number for MO_Register.  A value of 0 indicates the
  /// MO_Register has no subReg.
  ///
  /// For all other kinds of operands, this field holds target-specific flags.
  unsigned SubReg_TargetFlags : 12;

  /// TiedTo - Non-zero when this register operand is tied to another register
  /// operand. The encoding of this field is described in the block comment
  /// before MachineInstr::tieOperands().
  unsigned TiedTo : 4;

  /// IsDef - True if this is a def, false if this is a use of the register.
  /// This is only valid on register operands.
  ///
  unsigned IsDef : 1;

  /// IsImp - True if this is an implicit def or use, false if it is explicit.
  /// This is only valid on register opderands.
  ///
  unsigned IsImp : 1;

  /// IsDeadOrKill
  /// For uses: IsKill - Conservatively indicates the last use of a register
  /// on this path through the function. A register operand with true value of
  /// this flag must be the last use of the register, a register operand with
  /// false value may or may not be the last use of the register. After regalloc
  /// we can use recomputeLivenessFlags to get precise kill flags.
  /// For defs: IsDead - True if this register is never used by a subsequent
  /// instruction.
  /// This is only valid on register operands.
  unsigned IsDeadOrKill : 1;

  /// See isRenamable().
  unsigned IsRenamable : 1;

  /// IsUndef - True if this register operand reads an "undef" value, i.e. the
  /// read value doesn't matter.  This flag can be set on both use and def
  /// operands.  On a sub-register def operand, it refers to the part of the
  /// register that isn't written.  On a full-register def operand, it is a
  /// noop.  See readsReg().
  ///
  /// This is only valid on registers.
  ///
  /// Note that an instruction may have multiple <undef> operands referring to
  /// the same register.  In that case, the instruction may depend on those
  /// operands reading the same dont-care value.  For example:
  ///
  ///   %1 = XOR undef %2, undef %2
  ///
  /// Any register can be used for %2, and its value doesn't matter, but
  /// the two operands must be the same register.
  ///
  unsigned IsUndef : 1;

  /// IsInternalRead - True if this operand reads a value that was defined
  /// inside the same instruction or bundle.  This flag can be set on both use
  /// and def operands.  On a sub-register def operand, it refers to the part
  /// of the register that isn't written.  On a full-register def operand, it
  /// is a noop.
  ///
  /// When this flag is set, the instruction bundle must contain at least one
  /// other def of the register.  If multiple instructions in the bundle define
  /// the register, the meaning is target-defined.
  unsigned IsInternalRead : 1;

  /// IsEarlyClobber - True if this MO_Register 'def' operand is written to
  /// by the MachineInstr before all input registers are read.  This is used to
  /// model the GCC inline asm '&' constraint modifier.
  unsigned IsEarlyClobber : 1;

  /// IsDebug - True if this MO_Register 'use' operand is in a debug pseudo,
  /// not a real instruction.  Such uses should be ignored during codegen.
  unsigned IsDebug : 1;

  /// SmallContents - This really should be part of the Contents union, but
  /// lives out here so we can get a better packed struct.
  /// MO_Register: Register number.
  /// OffsetedInfo: Low bits of offset.
  union {
    unsigned RegNo;           // For MO_Register.
    unsigned OffsetLo;        // Matches Contents.OffsetedInfo.OffsetHi.
  } SmallContents;

  /// ParentMI - This is the instruction that this operand is embedded into.
  /// This is valid for all operand types, when the operand is in an instr.
  MachineInstr *ParentMI = nullptr;

  /// Contents union - This contains the payload for the various operand types.
  union ContentsUnion {
    ContentsUnion() {}
    MachineBasicBlock *MBB;  // For MO_MachineBasicBlock.
    const ConstantFP *CFP;   // For MO_FPImmediate.
    const ConstantInt *CI;   // For MO_CImmediate. Integers > 64bit.
    int64_t ImmVal;          // For MO_Immediate.
    const uint32_t *RegMask; // For MO_RegisterMask and MO_RegisterLiveOut.
    const MDNode *MD;        // For MO_Metadata.
    MCSymbol *Sym;           // For MO_MCSymbol.
    unsigned CFIIndex;       // For MO_CFI.
    Intrinsic::ID IntrinsicID; // For MO_IntrinsicID.
    unsigned Pred;           // For MO_Predicate
    ArrayRef<int> ShuffleMask; // For MO_ShuffleMask

    struct {                  // For MO_Register.
      // Register number is in SmallContents.RegNo.
      MachineOperand *Prev;   // Access list for register. See MRI.
      MachineOperand *Next;
    } Reg;

    struct { // For MO_DbgInstrRef.
      unsigned InstrIdx;
      unsigned OpIdx;
    } InstrRef;

    /// OffsetedInfo - This struct contains the offset and an object identifier.
    /// this represent the object as with an optional offset from it.
    struct {
      union {
        int Index;                // For MO_*Index - The index itself.
        const char *SymbolName;   // For MO_ExternalSymbol.
        const GlobalValue *GV;    // For MO_GlobalAddress.
        const BlockAddress *BA;   // For MO_BlockAddress.
      } Val;
      // Low bits of offset are in SmallContents.OffsetLo.
      int OffsetHi;               // An offset from the object, high 32 bits.
    } OffsetedInfo;
  } Contents;

  explicit MachineOperand(MachineOperandType K)
      : OpKind(K), SubReg_TargetFlags(0) {
    // Assert that the layout is what we expect. It's easy to grow this object.
    static_assert(alignof(MachineOperand) <= alignof(int64_t),
                  "MachineOperand shouldn't be more than 8 byte aligned");
    static_assert(sizeof(Contents) <= 2 * sizeof(void *),
                  "Contents should be at most two pointers");
    static_assert(sizeof(MachineOperand) <=
                      alignTo<alignof(int64_t)>(2 * sizeof(unsigned) +
                                                3 * sizeof(void *)),
                  "MachineOperand too big. Should be Kind, SmallContents, "
                  "ParentMI, and Contents");
  }

public:
  /// getType - Returns the MachineOperandType for this operand.
  ///
  MachineOperandType getType() const { return (MachineOperandType)OpKind; }

  unsigned getTargetFlags() const {
    return isReg() ? 0 : SubReg_TargetFlags;
  }
  void setTargetFlags(unsigned F) {
    assert(!isReg() && "Register operands can't have target flags");
    SubReg_TargetFlags = F;
    assert(SubReg_TargetFlags == F && "Target flags out of range");
  }
  void addTargetFlag(unsigned F) {
    assert(!isReg() && "Register operands can't have target flags");
    SubReg_TargetFlags |= F;
    assert((SubReg_TargetFlags & F) && "Target flags out of range");
  }


  /// getParent - Return the instruction that this operand belongs to.
  ///
  MachineInstr *getParent() { return ParentMI; }
  const MachineInstr *getParent() const { return ParentMI; }

  /// clearParent - Reset the parent pointer.
  ///
  /// The MachineOperand copy constructor also copies ParentMI, expecting the
  /// original to be deleted. If a MachineOperand is ever stored outside a
  /// MachineInstr, the parent pointer must be cleared.
  ///
  /// Never call clearParent() on an operand in a MachineInstr.
  ///
  void clearParent() { ParentMI = nullptr; }

  /// Returns the index of this operand in the instruction that it belongs to.
  unsigned getOperandNo() const;

  /// Print a subreg index operand.
  /// MO_Immediate operands can also be subreg idices. If it's the case, the
  /// subreg index name will be printed. MachineInstr::isOperandSubregIdx can be
  /// called to check this.
  static void printSubRegIdx(raw_ostream &OS, uint64_t Index,
                             const TargetRegisterInfo *TRI);

  /// Print operand target flags.
  static void printTargetFlags(raw_ostream& OS, const MachineOperand &Op);

  /// Print a MCSymbol as an operand.
  static void printSymbol(raw_ostream &OS, MCSymbol &Sym);

  /// Print a stack object reference.
  static void printStackObjectReference(raw_ostream &OS, unsigned FrameIndex,
                                        bool IsFixed, StringRef Name);

  /// Print the offset with explicit +/- signs.
  static void printOperandOffset(raw_ostream &OS, int64_t Offset);

  /// Print an IRSlotNumber.
  static void printIRSlotNumber(raw_ostream &OS, int Slot);

  /// Print the MachineOperand to \p os.
  /// Providing a valid \p TRI and \p IntrinsicInfo results in a more
  /// target-specific printing. If \p TRI and \p IntrinsicInfo are null, the
  /// function will try to pick it up from the parent.
  void print(raw_ostream &os, const TargetRegisterInfo *TRI = nullptr,
             const TargetIntrinsicInfo *IntrinsicInfo = nullptr) const;

  /// More complex way of printing a MachineOperand.
  /// \param TypeToPrint specifies the generic type to be printed on uses and
  /// defs. It can be determined using MachineInstr::getTypeToPrint.
  /// \param OpIdx - specifies the index of the operand in machine instruction.
  /// This will be used by target dependent MIR formatter. Could be std::nullopt
  /// if the index is unknown, e.g. called by dump().
  /// \param PrintDef - whether we want to print `def` on an operand which
  /// isDef. Sometimes, if the operand is printed before '=', we don't print
  /// `def`.
  /// \param IsStandalone - whether we want a verbose output of the MO. This
  /// prints extra information that can be easily inferred when printing the
  /// whole function, but not when printing only a fragment of it.
  /// \param ShouldPrintRegisterTies - whether we want to print register ties.
  /// Sometimes they are easily determined by the instruction's descriptor
  /// (MachineInstr::hasComplexRegiterTies can determine if it's needed).
  /// \param TiedOperandIdx - if we need to print register ties this needs to
  /// provide the index of the tied register. If not, it will be ignored.
  /// \param TRI - provide more target-specific information to the printer.
  /// Unlike the previous function, this one will not try and get the
  /// information from it's parent.
  /// \param IntrinsicInfo - same as \p TRI.
  void print(raw_ostream &os, ModuleSlotTracker &MST, LLT TypeToPrint,
             std::optional<unsigned> OpIdx, bool PrintDef, bool IsStandalone,
             bool ShouldPrintRegisterTies, unsigned TiedOperandIdx,
             const TargetRegisterInfo *TRI,
             const TargetIntrinsicInfo *IntrinsicInfo) const;

  /// Same as print(os, TRI, IntrinsicInfo), but allows to specify the low-level
  /// type to be printed the same way the full version of print(...) does it.
  void print(raw_ostream &os, LLT TypeToPrint,
             const TargetRegisterInfo *TRI = nullptr,
             const TargetIntrinsicInfo *IntrinsicInfo = nullptr) const;

  void dump() const;

  //===--------------------------------------------------------------------===//
  // Accessors that tell you what kind of MachineOperand you're looking at.
  //===--------------------------------------------------------------------===//

  /// isReg - Tests if this is a MO_Register operand.
  bool isReg() const { return OpKind == MO_Register; }
  /// isImm - Tests if this is a MO_Immediate operand.
  bool isImm() const { return OpKind == MO_Immediate; }
  /// isCImm - Test if this is a MO_CImmediate operand.
  bool isCImm() const { return OpKind == MO_CImmediate; }
  /// isFPImm - Tests if this is a MO_FPImmediate operand.
  bool isFPImm() const { return OpKind == MO_FPImmediate; }
  /// isMBB - Tests if this is a MO_MachineBasicBlock operand.
  bool isMBB() const { return OpKind == MO_MachineBasicBlock; }
  /// isFI - Tests if this is a MO_FrameIndex operand.
  bool isFI() const { return OpKind == MO_FrameIndex; }
  /// isCPI - Tests if this is a MO_ConstantPoolIndex operand.
  bool isCPI() const { return OpKind == MO_ConstantPoolIndex; }
  /// isTargetIndex - Tests if this is a MO_TargetIndex operand.
  bool isTargetIndex() const { return OpKind == MO_TargetIndex; }
  /// isJTI - Tests if this is a MO_JumpTableIndex operand.
  bool isJTI() const { return OpKind == MO_JumpTableIndex; }
  /// isGlobal - Tests if this is a MO_GlobalAddress operand.
  bool isGlobal() const { return OpKind == MO_GlobalAddress; }
  /// isSymbol - Tests if this is a MO_ExternalSymbol operand.
  bool isSymbol() const { return OpKind == MO_ExternalSymbol; }
  /// isBlockAddress - Tests if this is a MO_BlockAddress operand.
  bool isBlockAddress() const { return OpKind == MO_BlockAddress; }
  /// isRegMask - Tests if this is a MO_RegisterMask operand.
  bool isRegMask() const { return OpKind == MO_RegisterMask; }
  /// isRegLiveOut - Tests if this is a MO_RegisterLiveOut operand.
  bool isRegLiveOut() const { return OpKind == MO_RegisterLiveOut; }
  /// isMetadata - Tests if this is a MO_Metadata operand.
  bool isMetadata() const { return OpKind == MO_Metadata; }
  bool isMCSymbol() const { return OpKind == MO_MCSymbol; }
  bool isDbgInstrRef() const { return OpKind == MO_DbgInstrRef; }
  bool isCFIIndex() const { return OpKind == MO_CFIIndex; }
  bool isIntrinsicID() const { return OpKind == MO_IntrinsicID; }
  bool isPredicate() const { return OpKind == MO_Predicate; }
  bool isShuffleMask() const { return OpKind == MO_ShuffleMask; }
  //===--------------------------------------------------------------------===//
  // Accessors for Register Operands
  //===--------------------------------------------------------------------===//

  /// getReg - Returns the register number.
  Register getReg() const {
    assert(isReg() && "This is not a register operand!");
    return Register(SmallContents.RegNo);
  }

  unsigned getSubReg() const {
    assert(isReg() && "Wrong MachineOperand accessor");
    return SubReg_TargetFlags;
  }

  bool isUse() const {
    assert(isReg() && "Wrong MachineOperand accessor");
    return !IsDef;
  }

  bool isDef() const {
    assert(isReg() && "Wrong MachineOperand accessor");
    return IsDef;
  }

  bool isImplicit() const {
    assert(isReg() && "Wrong MachineOperand accessor");
    return IsImp;
  }

  bool isDead() const {
    assert(isReg() && "Wrong MachineOperand accessor");
    return IsDeadOrKill & IsDef;
  }

  bool isKill() const {
    assert(isReg() && "Wrong MachineOperand accessor");
    return IsDeadOrKill & !IsDef;
  }

  bool isUndef() const {
    assert(isReg() && "Wrong MachineOperand accessor");
    return IsUndef;
  }

  /// isRenamable - Returns true if this register may be renamed, i.e. it does
  /// not generate a value that is somehow read in a way that is not represented
  /// by the Machine IR (e.g. to meet an ABI or ISA requirement).  This is only
  /// valid on physical register operands.  Virtual registers are assumed to
  /// always be renamable regardless of the value of this field.
  ///
  /// Operands that are renamable can freely be changed to any other register
  /// that is a member of the register class returned by
  /// MI->getRegClassConstraint().
  ///
  /// isRenamable can return false for several different reasons:
  ///
  /// - ABI constraints (since liveness is not always precisely modeled).  We
  ///   conservatively handle these cases by setting all physical register
  ///   operands that didn’t start out as virtual regs to not be renamable.
  ///   Also any physical register operands created after register allocation or
  ///   whose register is changed after register allocation will not be
  ///   renamable.  This state is tracked in the MachineOperand::IsRenamable
  ///   bit.
  ///
  /// - Opcode/target constraints: for opcodes that have complex register class
  ///   requirements (e.g. that depend on other operands/instructions), we set
  ///   hasExtraSrcRegAllocReq/hasExtraDstRegAllocReq in the machine opcode
  ///   description.  Operands belonging to instructions with opcodes that are
  ///   marked hasExtraSrcRegAllocReq/hasExtraDstRegAllocReq return false from
  ///   isRenamable().  Additionally, the AllowRegisterRenaming target property
  ///   prevents any operands from being marked renamable for targets that don't
  ///   have detailed opcode hasExtraSrcRegAllocReq/hasExtraDstRegAllocReq
  ///   values.
  bool isRenamable() const;

  bool isInternalRead() const {
    assert(isReg() && "Wrong MachineOperand accessor");
    return IsInternalRead;
  }

  bool isEarlyClobber() const {
    assert(isReg() && "Wrong MachineOperand accessor");
    return IsEarlyClobber;
  }

  bool isTied() const {
    assert(isReg() && "Wrong MachineOperand accessor");
    return TiedTo;
  }

  bool isDebug() const {
    assert(isReg() && "Wrong MachineOperand accessor");
    return IsDebug;
  }

  /// readsReg - Returns true if this operand reads the previous value of its
  /// register.  A use operand with the <undef> flag set doesn't read its
  /// register.  A sub-register def implicitly reads the other parts of the
  /// register being redefined unless the <undef> flag is set.
  ///
  /// This refers to reading the register value from before the current
  /// instruction or bundle. Internal bundle reads are not included.
  bool readsReg() const {
    assert(isReg() && "Wrong MachineOperand accessor");
    return !isUndef() && !isInternalRead() && (isUse() || getSubReg());
  }

  /// Return true if this operand can validly be appended to an arbitrary
  /// operand list. i.e. this behaves like an implicit operand.
  bool isValidExcessOperand() const {
    if ((isReg() && isImplicit()) || isRegMask())
      return true;

    // Debug operands
    return isMetadata() || isMCSymbol();
  }

  //===--------------------------------------------------------------------===//
  // Mutators for Register Operands
  //===--------------------------------------------------------------------===//

  /// Change the register this operand corresponds to.
  ///
  void setReg(Register Reg);

  void setSubReg(unsigned subReg) {
    assert(isReg() && "Wrong MachineOperand mutator");
    SubReg_TargetFlags = subReg;
    assert(SubReg_TargetFlags == subReg && "SubReg out of range");
  }

  /// substVirtReg - Substitute the current register with the virtual
  /// subregister Reg:SubReg. Take any existing SubReg index into account,
  /// using TargetRegisterInfo to compose the subreg indices if necessary.
  /// Reg must be a virtual register, SubIdx can be 0.
  ///
  void substVirtReg(Register Reg, unsigned SubIdx, const TargetRegisterInfo&);

  /// substPhysReg - Substitute the current register with the physical register
  /// Reg, taking any existing SubReg into account. For instance,
  /// substPhysReg(%eax) will change %reg1024:sub_8bit to %al.
  ///
  void substPhysReg(MCRegister Reg, const TargetRegisterInfo&);

  void setIsUse(bool Val = true) { setIsDef(!Val); }

  /// Change a def to a use, or a use to a def.
  void setIsDef(bool Val = true);

  void setImplicit(bool Val = true) {
    assert(isReg() && "Wrong MachineOperand mutator");
    IsImp = Val;
  }

  void setIsKill(bool Val = true) {
    assert(isReg() && !IsDef && "Wrong MachineOperand mutator");
    assert((!Val || !isDebug()) && "Marking a debug operation as kill");
    IsDeadOrKill = Val;
  }

  void setIsDead(bool Val = true) {
    assert(isReg() && IsDef && "Wrong MachineOperand mutator");
    IsDeadOrKill = Val;
  }

  void setIsUndef(bool Val = true) {
    assert(isReg() && "Wrong MachineOperand mutator");
    IsUndef = Val;
  }

  void setIsRenamable(bool Val = true);

  void setIsInternalRead(bool Val = true) {
    assert(isReg() && "Wrong MachineOperand mutator");
    IsInternalRead = Val;
  }

  void setIsEarlyClobber(bool Val = true) {
    assert(isReg() && IsDef && "Wrong MachineOperand mutator");
    IsEarlyClobber = Val;
  }

  void setIsDebug(bool Val = true) {
    assert(isReg() && !IsDef && "Wrong MachineOperand mutator");
    IsDebug = Val;
  }

  //===--------------------------------------------------------------------===//
  // Accessors for various operand types.
  //===--------------------------------------------------------------------===//

  int64_t getImm() const {
    assert(isImm() && "Wrong MachineOperand accessor");
    return Contents.ImmVal;
  }

  const ConstantInt *getCImm() const {
    assert(isCImm() && "Wrong MachineOperand accessor");
    return Contents.CI;
  }

  const ConstantFP *getFPImm() const {
    assert(isFPImm() && "Wrong MachineOperand accessor");
    return Contents.CFP;
  }

  MachineBasicBlock *getMBB() const {
    assert(isMBB() && "Wrong MachineOperand accessor");
    return Contents.MBB;
  }

  int getIndex() const {
    assert((isFI() || isCPI() || isTargetIndex() || isJTI()) &&
           "Wrong MachineOperand accessor");
    return Contents.OffsetedInfo.Val.Index;
  }

  const GlobalValue *getGlobal() const {
    assert(isGlobal() && "Wrong MachineOperand accessor");
    return Contents.OffsetedInfo.Val.GV;
  }

  const BlockAddress *getBlockAddress() const {
    assert(isBlockAddress() && "Wrong MachineOperand accessor");
    return Contents.OffsetedInfo.Val.BA;
  }

  MCSymbol *getMCSymbol() const {
    assert(isMCSymbol() && "Wrong MachineOperand accessor");
    return Contents.Sym;
  }

  unsigned getInstrRefInstrIndex() const {
    assert(isDbgInstrRef() && "Wrong MachineOperand accessor");
    return Contents.InstrRef.InstrIdx;
  }

  unsigned getInstrRefOpIndex() const {
    assert(isDbgInstrRef() && "Wrong MachineOperand accessor");
    return Contents.InstrRef.OpIdx;
  }

  unsigned getCFIIndex() const {
    assert(isCFIIndex() && "Wrong MachineOperand accessor");
    return Contents.CFIIndex;
  }

  Intrinsic::ID getIntrinsicID() const {
    assert(isIntrinsicID() && "Wrong MachineOperand accessor");
    return Contents.IntrinsicID;
  }

  unsigned getPredicate() const {
    assert(isPredicate() && "Wrong MachineOperand accessor");
    return Contents.Pred;
  }

  ArrayRef<int> getShuffleMask() const {
    assert(isShuffleMask() && "Wrong MachineOperand accessor");
    return Contents.ShuffleMask;
  }

  /// Return the offset from the symbol in this operand. This always returns 0
  /// for ExternalSymbol operands.
  int64_t getOffset() const {
    assert((isGlobal() || isSymbol() || isMCSymbol() || isCPI() ||
            isTargetIndex() || isBlockAddress()) &&
           "Wrong MachineOperand accessor");
    return int64_t(uint64_t(Contents.OffsetedInfo.OffsetHi) << 32) |
           SmallContents.OffsetLo;
  }

  const char *getSymbolName() const {
    assert(isSymbol() && "Wrong MachineOperand accessor");
    return Contents.OffsetedInfo.Val.SymbolName;
  }

  /// clobbersPhysReg - Returns true if this RegMask clobbers PhysReg.
  /// It is sometimes necessary to detach the register mask pointer from its
  /// machine operand. This static method can be used for such detached bit
  /// mask pointers.
  static bool clobbersPhysReg(const uint32_t *RegMask, MCRegister PhysReg) {
    // See TargetRegisterInfo.h.
    assert(PhysReg < (1u << 30) && "Not a physical register");
    return !(RegMask[PhysReg / 32] & (1u << PhysReg % 32));
  }

  /// clobbersPhysReg - Returns true if this RegMask operand clobbers PhysReg.
  bool clobbersPhysReg(MCRegister PhysReg) const {
     return clobbersPhysReg(getRegMask(), PhysReg);
  }

  /// getRegMask - Returns a bit mask of registers preserved by this RegMask
  /// operand.
  const uint32_t *getRegMask() const {
    assert(isRegMask() && "Wrong MachineOperand accessor");
    return Contents.RegMask;
  }

  /// Returns number of elements needed for a regmask array.
  static unsigned getRegMaskSize(unsigned NumRegs) {
    return (NumRegs + 31) / 32;
  }

  /// getRegLiveOut - Returns a bit mask of live-out registers.
  const uint32_t *getRegLiveOut() const {
    assert(isRegLiveOut() && "Wrong MachineOperand accessor");
    return Contents.RegMask;
  }

  const MDNode *getMetadata() const {
    assert(isMetadata() && "Wrong MachineOperand accessor");
    return Contents.MD;
  }

  //===--------------------------------------------------------------------===//
  // Mutators for various operand types.
  //===--------------------------------------------------------------------===//

  void setImm(int64_t immVal) {
    assert(isImm() && "Wrong MachineOperand mutator");
    Contents.ImmVal = immVal;
  }

  void setCImm(const ConstantInt *CI) {
    assert(isCImm() && "Wrong MachineOperand mutator");
    Contents.CI = CI;
  }

  void setFPImm(const ConstantFP *CFP) {
    assert(isFPImm() && "Wrong MachineOperand mutator");
    Contents.CFP = CFP;
  }

  void setOffset(int64_t Offset) {
    assert((isGlobal() || isSymbol() || isMCSymbol() || isCPI() ||
            isTargetIndex() || isBlockAddress()) &&
           "Wrong MachineOperand mutator");
    SmallContents.OffsetLo = unsigned(Offset);
    Contents.OffsetedInfo.OffsetHi = int(Offset >> 32);
  }

  void setIndex(int Idx) {
    assert((isFI() || isCPI() || isTargetIndex() || isJTI()) &&
           "Wrong MachineOperand mutator");
    Contents.OffsetedInfo.Val.Index = Idx;
  }

  void setMetadata(const MDNode *MD) {
    assert(isMetadata() && "Wrong MachineOperand mutator");
    Contents.MD = MD;
  }

  void setInstrRefInstrIndex(unsigned InstrIdx) {
    assert(isDbgInstrRef() && "Wrong MachineOperand mutator");
    Contents.InstrRef.InstrIdx = InstrIdx;
  }
  void setInstrRefOpIndex(unsigned OpIdx) {
    assert(isDbgInstrRef() && "Wrong MachineOperand mutator");
    Contents.InstrRef.OpIdx = OpIdx;
  }

  void setMBB(MachineBasicBlock *MBB) {
    assert(isMBB() && "Wrong MachineOperand mutator");
    Contents.MBB = MBB;
  }

  /// Sets value of register mask operand referencing Mask.  The
  /// operand does not take ownership of the memory referenced by Mask, it must
  /// remain valid for the lifetime of the operand. See CreateRegMask().
  /// Any physreg with a 0 bit in the mask is clobbered by the instruction.
  void setRegMask(const uint32_t *RegMaskPtr) {
    assert(isRegMask() && "Wrong MachineOperand mutator");
    Contents.RegMask = RegMaskPtr;
  }

  void setIntrinsicID(Intrinsic::ID IID) {
    assert(isIntrinsicID() && "Wrong MachineOperand mutator");
    Contents.IntrinsicID = IID;
  }

  void setPredicate(unsigned Predicate) {
    assert(isPredicate() && "Wrong MachineOperand mutator");
    Contents.Pred = Predicate;
  }

  //===--------------------------------------------------------------------===//
  // Other methods.
  //===--------------------------------------------------------------------===//

  /// Returns true if this operand is identical to the specified operand except
  /// for liveness related flags (isKill, isUndef and isDead). Note that this
  /// should stay in sync with the hash_value overload below.
  bool isIdenticalTo(const MachineOperand &Other) const;

  /// MachineOperand hash_value overload.
  ///
  /// Note that this includes the same information in the hash that
  /// isIdenticalTo uses for comparison. It is thus suited for use in hash
  /// tables which use that function for equality comparisons only. This must
  /// stay exactly in sync with isIdenticalTo above.
  friend hash_code hash_value(const MachineOperand &MO);

  /// ChangeToImmediate - Replace this operand with a new immediate operand of
  /// the specified value.  If an operand is known to be an immediate already,
  /// the setImm method should be used.
  void ChangeToImmediate(int64_t ImmVal, unsigned TargetFlags = 0);

  /// ChangeToFPImmediate - Replace this operand with a new FP immediate operand
  /// of the specified value.  If an operand is known to be an FP immediate
  /// already, the setFPImm method should be used.
  void ChangeToFPImmediate(const ConstantFP *FPImm, unsigned TargetFlags = 0);

  /// ChangeToES - Replace this operand with a new external symbol operand.
  void ChangeToES(const char *SymName, unsigned TargetFlags = 0);

  /// ChangeToGA - Replace this operand with a new global address operand.
  void ChangeToGA(const GlobalValue *GV, int64_t Offset,
                  unsigned TargetFlags = 0);

  /// ChangeToBA - Replace this operand with a new block address operand.
  void ChangeToBA(const BlockAddress *BA, int64_t Offset,
                  unsigned TargetFlags = 0);

  /// ChangeToMCSymbol - Replace this operand with a new MC symbol operand.
  void ChangeToMCSymbol(MCSymbol *Sym, unsigned TargetFlags = 0);

  /// Replace this operand with a frame index.
  void ChangeToFrameIndex(int Idx, unsigned TargetFlags = 0);

  /// Replace this operand with a target index.
  void ChangeToTargetIndex(unsigned Idx, int64_t Offset,
                           unsigned TargetFlags = 0);

  /// Replace this operand with an Instruction Reference.
  void ChangeToDbgInstrRef(unsigned InstrIdx, unsigned OpIdx,
                           unsigned TargetFlags = 0);

  /// ChangeToRegister - Replace this operand with a new register operand of
  /// the specified value.  If an operand is known to be an register already,
  /// the setReg method should be used.
  void ChangeToRegister(Register Reg, bool isDef, bool isImp = false,
                        bool isKill = false, bool isDead = false,
                        bool isUndef = false, bool isDebug = false);

  /// getTargetIndexName - If this MachineOperand is a TargetIndex that has a
  /// name, attempt to get the name. Returns nullptr if the TargetIndex does not
  /// have a name. Asserts if MO is not a TargetIndex.
  const char *getTargetIndexName() const;

  //===--------------------------------------------------------------------===//
  // Construction methods.
  //===--------------------------------------------------------------------===//

  static MachineOperand CreateImm(int64_t Val) {
    MachineOperand Op(MachineOperand::MO_Immediate);
    Op.setImm(Val);
    return Op;
  }

  static MachineOperand CreateCImm(const ConstantInt *CI) {
    MachineOperand Op(MachineOperand::MO_CImmediate);
    Op.Contents.CI = CI;
    return Op;
  }

  static MachineOperand CreateFPImm(const ConstantFP *CFP) {
    MachineOperand Op(MachineOperand::MO_FPImmediate);
    Op.Contents.CFP = CFP;
    return Op;
  }

  static MachineOperand CreateReg(Register Reg, bool isDef, bool isImp = false,
                                  bool isKill = false, bool isDead = false,
                                  bool isUndef = false,
                                  bool isEarlyClobber = false,
                                  unsigned SubReg = 0, bool isDebug = false,
                                  bool isInternalRead = false,
                                  bool isRenamable = false) {
    assert(!(isDead && !isDef) && "Dead flag on non-def");
    assert(!(isKill && isDef) && "Kill flag on def");
    MachineOperand Op(MachineOperand::MO_Register);
    Op.IsDef = isDef;
    Op.IsImp = isImp;
    Op.IsDeadOrKill = isKill | isDead;
    Op.IsRenamable = isRenamable;
    Op.IsUndef = isUndef;
    Op.IsInternalRead = isInternalRead;
    Op.IsEarlyClobber = isEarlyClobber;
    Op.TiedTo = 0;
    Op.IsDebug = isDebug;
    Op.SmallContents.RegNo = Reg;
    Op.Contents.Reg.Prev = nullptr;
    Op.Contents.Reg.Next = nullptr;
    Op.setSubReg(SubReg);
    return Op;
  }
  static MachineOperand CreateMBB(MachineBasicBlock *MBB,
                                  unsigned TargetFlags = 0) {
    MachineOperand Op(MachineOperand::MO_MachineBasicBlock);
    Op.setMBB(MBB);
    Op.setTargetFlags(TargetFlags);
    return Op;
  }
  static MachineOperand CreateFI(int Idx) {
    MachineOperand Op(MachineOperand::MO_FrameIndex);
    Op.setIndex(Idx);
    return Op;
  }
  static MachineOperand CreateCPI(unsigned Idx, int Offset,
                                  unsigned TargetFlags = 0) {
    MachineOperand Op(MachineOperand::MO_ConstantPoolIndex);
    Op.setIndex(Idx);
    Op.setOffset(Offset);
    Op.setTargetFlags(TargetFlags);
    return Op;
  }
  static MachineOperand CreateTargetIndex(unsigned Idx, int64_t Offset,
                                          unsigned TargetFlags = 0) {
    MachineOperand Op(MachineOperand::MO_TargetIndex);
    Op.setIndex(Idx);
    Op.setOffset(Offset);
    Op.setTargetFlags(TargetFlags);
    return Op;
  }
  static MachineOperand CreateJTI(unsigned Idx, unsigned TargetFlags = 0) {
    MachineOperand Op(MachineOperand::MO_JumpTableIndex);
    Op.setIndex(Idx);
    Op.setTargetFlags(TargetFlags);
    return Op;
  }
  static MachineOperand CreateGA(const GlobalValue *GV, int64_t Offset,
                                 unsigned TargetFlags = 0) {
    MachineOperand Op(MachineOperand::MO_GlobalAddress);
    Op.Contents.OffsetedInfo.Val.GV = GV;
    Op.setOffset(Offset);
    Op.setTargetFlags(TargetFlags);
    return Op;
  }
  static MachineOperand CreateES(const char *SymName,
                                 unsigned TargetFlags = 0) {
    MachineOperand Op(MachineOperand::MO_ExternalSymbol);
    Op.Contents.OffsetedInfo.Val.SymbolName = SymName;
    Op.setOffset(0); // Offset is always 0.
    Op.setTargetFlags(TargetFlags);
    return Op;
  }
  static MachineOperand CreateBA(const BlockAddress *BA, int64_t Offset,
                                 unsigned TargetFlags = 0) {
    MachineOperand Op(MachineOperand::MO_BlockAddress);
    Op.Contents.OffsetedInfo.Val.BA = BA;
    Op.setOffset(Offset);
    Op.setTargetFlags(TargetFlags);
    return Op;
  }
  /// CreateRegMask - Creates a register mask operand referencing Mask.  The
  /// operand does not take ownership of the memory referenced by Mask, it
  /// must remain valid for the lifetime of the operand.
  ///
  /// A RegMask operand represents a set of non-clobbered physical registers
  /// on an instruction that clobbers many registers, typically a call.  The
  /// bit mask has a bit set for each physreg that is preserved by this
  /// instruction, as described in the documentation for
  /// TargetRegisterInfo::getCallPreservedMask().
  ///
  /// Any physreg with a 0 bit in the mask is clobbered by the instruction.
  ///
  static MachineOperand CreateRegMask(const uint32_t *Mask) {
    assert(Mask && "Missing register mask");
    MachineOperand Op(MachineOperand::MO_RegisterMask);
    Op.Contents.RegMask = Mask;
    return Op;
  }
  static MachineOperand CreateRegLiveOut(const uint32_t *Mask) {
    assert(Mask && "Missing live-out register mask");
    MachineOperand Op(MachineOperand::MO_RegisterLiveOut);
    Op.Contents.RegMask = Mask;
    return Op;
  }
  static MachineOperand CreateMetadata(const MDNode *Meta) {
    MachineOperand Op(MachineOperand::MO_Metadata);
    Op.Contents.MD = Meta;
    return Op;
  }

  static MachineOperand CreateMCSymbol(MCSymbol *Sym,
                                       unsigned TargetFlags = 0) {
    MachineOperand Op(MachineOperand::MO_MCSymbol);
    Op.Contents.Sym = Sym;
    Op.setOffset(0);
    Op.setTargetFlags(TargetFlags);
    return Op;
  }

  static MachineOperand CreateDbgInstrRef(unsigned InstrIdx, unsigned OpIdx) {
    MachineOperand Op(MachineOperand::MO_DbgInstrRef);
    Op.Contents.InstrRef.InstrIdx = InstrIdx;
    Op.Contents.InstrRef.OpIdx = OpIdx;
    return Op;
  }

  static MachineOperand CreateCFIIndex(unsigned CFIIndex) {
    MachineOperand Op(MachineOperand::MO_CFIIndex);
    Op.Contents.CFIIndex = CFIIndex;
    return Op;
  }

  static MachineOperand CreateIntrinsicID(Intrinsic::ID ID) {
    MachineOperand Op(MachineOperand::MO_IntrinsicID);
    Op.Contents.IntrinsicID = ID;
    return Op;
  }

  static MachineOperand CreatePredicate(unsigned Pred) {
    MachineOperand Op(MachineOperand::MO_Predicate);
    Op.Contents.Pred = Pred;
    return Op;
  }

  static MachineOperand CreateShuffleMask(ArrayRef<int> Mask) {
    MachineOperand Op(MachineOperand::MO_ShuffleMask);
    Op.Contents.ShuffleMask = Mask;
    return Op;
  }

  friend class MachineInstr;
  friend class MachineRegisterInfo;

private:
  // If this operand is currently a register operand, and if this is in a
  // function, deregister the operand from the register's use/def list.
  void removeRegFromUses();

  /// Artificial kinds for DenseMap usage.
  enum : unsigned char {
    MO_Empty = MO_Last + 1,
    MO_Tombstone,
  };

  friend struct DenseMapInfo<MachineOperand>;

  //===--------------------------------------------------------------------===//
  // Methods for handling register use/def lists.
  //===--------------------------------------------------------------------===//

  /// isOnRegUseList - Return true if this operand is on a register use/def
  /// list or false if not.  This can only be called for register operands
  /// that are part of a machine instruction.
  bool isOnRegUseList() const {
    assert(isReg() && "Can only add reg operand to use lists");
    return Contents.Reg.Prev != nullptr;
  }
};

template <> struct DenseMapInfo<MachineOperand> {
  static MachineOperand getEmptyKey() {
    return MachineOperand(static_cast<MachineOperand::MachineOperandType>(
        MachineOperand::MO_Empty));
  }
  static MachineOperand getTombstoneKey() {
    return MachineOperand(static_cast<MachineOperand::MachineOperandType>(
        MachineOperand::MO_Tombstone));
  }
  static unsigned getHashValue(const MachineOperand &MO) {
    return hash_value(MO);
  }
  static bool isEqual(const MachineOperand &LHS, const MachineOperand &RHS) {
    if (LHS.getType() == static_cast<MachineOperand::MachineOperandType>(
                             MachineOperand::MO_Empty) ||
        LHS.getType() == static_cast<MachineOperand::MachineOperandType>(
                             MachineOperand::MO_Tombstone))
      return LHS.getType() == RHS.getType();
    return LHS.isIdenticalTo(RHS);
  }
};

inline raw_ostream &operator<<(raw_ostream &OS, const MachineOperand &MO) {
  MO.print(OS);
  return OS;
}

// See friend declaration above. This additional declaration is required in
// order to compile LLVM with IBM xlC compiler.
hash_code hash_value(const MachineOperand &MO);
} // namespace llvm

#endif
