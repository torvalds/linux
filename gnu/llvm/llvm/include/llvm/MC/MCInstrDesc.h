//===-- llvm/MC/MCInstrDesc.h - Instruction Descriptors -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the MCOperandInfo and MCInstrDesc classes, which
// are used to describe target instructions and their operands.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCINSTRDESC_H
#define LLVM_MC_MCINSTRDESC_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/MC/MCRegister.h"

namespace llvm {
class MCRegisterInfo;

class MCInst;

//===----------------------------------------------------------------------===//
// Machine Operand Flags and Description
//===----------------------------------------------------------------------===//

namespace MCOI {
/// Operand constraints. These are encoded in 16 bits with one of the
/// low-order 3 bits specifying that a constraint is present and the
/// corresponding high-order hex digit specifying the constraint value.
/// This allows for a maximum of 3 constraints.
enum OperandConstraint {
  TIED_TO = 0,  // Must be allocated the same register as specified value.
  EARLY_CLOBBER // If present, operand is an early clobber register.
};

// Define a macro to produce each constraint value.
#define MCOI_TIED_TO(op) \
  ((1 << MCOI::TIED_TO) | ((op) << (4 + MCOI::TIED_TO * 4)))

#define MCOI_EARLY_CLOBBER \
  (1 << MCOI::EARLY_CLOBBER)

/// These are flags set on operands, but should be considered
/// private, all access should go through the MCOperandInfo accessors.
/// See the accessors for a description of what these are.
enum OperandFlags {
  LookupPtrRegClass = 0,
  Predicate,
  OptionalDef,
  BranchTarget
};

/// Operands are tagged with one of the values of this enum.
enum OperandType {
  OPERAND_UNKNOWN = 0,
  OPERAND_IMMEDIATE = 1,
  OPERAND_REGISTER = 2,
  OPERAND_MEMORY = 3,
  OPERAND_PCREL = 4,

  OPERAND_FIRST_GENERIC = 6,
  OPERAND_GENERIC_0 = 6,
  OPERAND_GENERIC_1 = 7,
  OPERAND_GENERIC_2 = 8,
  OPERAND_GENERIC_3 = 9,
  OPERAND_GENERIC_4 = 10,
  OPERAND_GENERIC_5 = 11,
  OPERAND_LAST_GENERIC = 11,

  OPERAND_FIRST_GENERIC_IMM = 12,
  OPERAND_GENERIC_IMM_0 = 12,
  OPERAND_LAST_GENERIC_IMM = 12,

  OPERAND_FIRST_TARGET = 13,
};

} // namespace MCOI

/// This holds information about one operand of a machine instruction,
/// indicating the register class for register operands, etc.
class MCOperandInfo {
public:
  /// This specifies the register class enumeration of the operand
  /// if the operand is a register.  If isLookupPtrRegClass is set, then this is
  /// an index that is passed to TargetRegisterInfo::getPointerRegClass(x) to
  /// get a dynamic register class.
  int16_t RegClass;

  /// These are flags from the MCOI::OperandFlags enum.
  uint8_t Flags;

  /// Information about the type of the operand.
  uint8_t OperandType;

  /// Operand constraints (see OperandConstraint enum).
  uint16_t Constraints;

  /// Set if this operand is a pointer value and it requires a callback
  /// to look up its register class.
  bool isLookupPtrRegClass() const {
    return Flags & (1 << MCOI::LookupPtrRegClass);
  }

  /// Set if this is one of the operands that made up of the predicate
  /// operand that controls an isPredicable() instruction.
  bool isPredicate() const { return Flags & (1 << MCOI::Predicate); }

  /// Set if this operand is a optional def.
  bool isOptionalDef() const { return Flags & (1 << MCOI::OptionalDef); }

  /// Set if this operand is a branch target.
  bool isBranchTarget() const { return Flags & (1 << MCOI::BranchTarget); }

  bool isGenericType() const {
    return OperandType >= MCOI::OPERAND_FIRST_GENERIC &&
           OperandType <= MCOI::OPERAND_LAST_GENERIC;
  }

  unsigned getGenericTypeIndex() const {
    assert(isGenericType() && "non-generic types don't have an index");
    return OperandType - MCOI::OPERAND_FIRST_GENERIC;
  }

  bool isGenericImm() const {
    return OperandType >= MCOI::OPERAND_FIRST_GENERIC_IMM &&
           OperandType <= MCOI::OPERAND_LAST_GENERIC_IMM;
  }

  unsigned getGenericImmIndex() const {
    assert(isGenericImm() && "non-generic immediates don't have an index");
    return OperandType - MCOI::OPERAND_FIRST_GENERIC_IMM;
  }
};

//===----------------------------------------------------------------------===//
// Machine Instruction Flags and Description
//===----------------------------------------------------------------------===//

namespace MCID {
/// These should be considered private to the implementation of the
/// MCInstrDesc class.  Clients should use the predicate methods on MCInstrDesc,
/// not use these directly.  These all correspond to bitfields in the
/// MCInstrDesc::Flags field.
enum Flag {
  PreISelOpcode = 0,
  Variadic,
  HasOptionalDef,
  Pseudo,
  Meta,
  Return,
  EHScopeReturn,
  Call,
  Barrier,
  Terminator,
  Branch,
  IndirectBranch,
  Compare,
  MoveImm,
  MoveReg,
  Bitcast,
  Select,
  DelaySlot,
  FoldableAsLoad,
  MayLoad,
  MayStore,
  MayRaiseFPException,
  Predicable,
  NotDuplicable,
  UnmodeledSideEffects,
  Commutable,
  ConvertibleTo3Addr,
  UsesCustomInserter,
  HasPostISelHook,
  Rematerializable,
  CheapAsAMove,
  ExtraSrcRegAllocReq,
  ExtraDefRegAllocReq,
  RegSequence,
  ExtractSubreg,
  InsertSubreg,
  Convergent,
  Add,
  Trap,
  VariadicOpsAreDefs,
  Authenticated,
};
} // namespace MCID

/// Describe properties that are true of each instruction in the target
/// description file.  This captures information about side effects, register
/// use and many other things.  There is one instance of this struct for each
/// target instruction class, and the MachineInstr class points to this struct
/// directly to describe itself.
class MCInstrDesc {
public:
  // FIXME: Disable copies and moves.
  // Do not allow MCInstrDescs to be copied or moved. They should only exist in
  // the <Target>Insts table because they rely on knowing their own address to
  // find other information elsewhere in the same table.

  unsigned short Opcode;         // The opcode number
  unsigned short NumOperands;    // Num of args (may be more if variable_ops)
  unsigned char NumDefs;         // Num of args that are definitions
  unsigned char Size;            // Number of bytes in encoding.
  unsigned short SchedClass;     // enum identifying instr sched class
  unsigned char NumImplicitUses; // Num of regs implicitly used
  unsigned char NumImplicitDefs; // Num of regs implicitly defined
  unsigned short ImplicitOffset; // Offset to start of implicit op list
  unsigned short OpInfoOffset;   // Offset to info about operands
  uint64_t Flags;                // Flags identifying machine instr class
  uint64_t TSFlags;              // Target Specific Flag values

  /// Returns the value of the specified operand constraint if
  /// it is present. Returns -1 if it is not present.
  int getOperandConstraint(unsigned OpNum,
                           MCOI::OperandConstraint Constraint) const {
    if (OpNum < NumOperands &&
        (operands()[OpNum].Constraints & (1 << Constraint))) {
      unsigned ValuePos = 4 + Constraint * 4;
      return (int)(operands()[OpNum].Constraints >> ValuePos) & 0x0f;
    }
    return -1;
  }

  /// Return the opcode number for this descriptor.
  unsigned getOpcode() const { return Opcode; }

  /// Return the number of declared MachineOperands for this
  /// MachineInstruction.  Note that variadic (isVariadic() returns true)
  /// instructions may have additional operands at the end of the list, and note
  /// that the machine instruction may include implicit register def/uses as
  /// well.
  unsigned getNumOperands() const { return NumOperands; }

  ArrayRef<MCOperandInfo> operands() const {
    auto OpInfo = reinterpret_cast<const MCOperandInfo *>(this + Opcode + 1);
    return ArrayRef(OpInfo + OpInfoOffset, NumOperands);
  }

  /// Return the number of MachineOperands that are register
  /// definitions.  Register definitions always occur at the start of the
  /// machine operand list.  This is the number of "outs" in the .td file,
  /// and does not include implicit defs.
  unsigned getNumDefs() const { return NumDefs; }

  /// Return flags of this instruction.
  uint64_t getFlags() const { return Flags; }

  /// \returns true if this instruction is emitted before instruction selection
  /// and should be legalized/regbankselected/selected.
  bool isPreISelOpcode() const { return Flags & (1ULL << MCID::PreISelOpcode); }

  /// Return true if this instruction can have a variable number of
  /// operands.  In this case, the variable operands will be after the normal
  /// operands but before the implicit definitions and uses (if any are
  /// present).
  bool isVariadic() const { return Flags & (1ULL << MCID::Variadic); }

  /// Set if this instruction has an optional definition, e.g.
  /// ARM instructions which can set condition code if 's' bit is set.
  bool hasOptionalDef() const { return Flags & (1ULL << MCID::HasOptionalDef); }

  /// Return true if this is a pseudo instruction that doesn't
  /// correspond to a real machine instruction.
  bool isPseudo() const { return Flags & (1ULL << MCID::Pseudo); }

  /// Return true if this is a meta instruction that doesn't
  /// produce any output in the form of executable instructions.
  bool isMetaInstruction() const { return Flags & (1ULL << MCID::Meta); }

  /// Return true if the instruction is a return.
  bool isReturn() const { return Flags & (1ULL << MCID::Return); }

  /// Return true if the instruction is an add instruction.
  bool isAdd() const { return Flags & (1ULL << MCID::Add); }

  /// Return true if this instruction is a trap.
  bool isTrap() const { return Flags & (1ULL << MCID::Trap); }

  /// Return true if the instruction is a register to register move.
  bool isMoveReg() const { return Flags & (1ULL << MCID::MoveReg); }

  ///  Return true if the instruction is a call.
  bool isCall() const { return Flags & (1ULL << MCID::Call); }

  /// Returns true if the specified instruction stops control flow
  /// from executing the instruction immediately following it.  Examples include
  /// unconditional branches and return instructions.
  bool isBarrier() const { return Flags & (1ULL << MCID::Barrier); }

  /// Returns true if this instruction part of the terminator for
  /// a basic block.  Typically this is things like return and branch
  /// instructions.
  ///
  /// Various passes use this to insert code into the bottom of a basic block,
  /// but before control flow occurs.
  bool isTerminator() const { return Flags & (1ULL << MCID::Terminator); }

  /// Returns true if this is a conditional, unconditional, or
  /// indirect branch.  Predicates below can be used to discriminate between
  /// these cases, and the TargetInstrInfo::analyzeBranch method can be used to
  /// get more information.
  bool isBranch() const { return Flags & (1ULL << MCID::Branch); }

  /// Return true if this is an indirect branch, such as a
  /// branch through a register.
  bool isIndirectBranch() const { return Flags & (1ULL << MCID::IndirectBranch); }

  /// Return true if this is a branch which may fall
  /// through to the next instruction or may transfer control flow to some other
  /// block.  The TargetInstrInfo::analyzeBranch method can be used to get more
  /// information about this branch.
  bool isConditionalBranch() const {
    return isBranch() && !isBarrier() && !isIndirectBranch();
  }

  /// Return true if this is a branch which always
  /// transfers control flow to some other block.  The
  /// TargetInstrInfo::analyzeBranch method can be used to get more information
  /// about this branch.
  bool isUnconditionalBranch() const {
    return isBranch() && isBarrier() && !isIndirectBranch();
  }

  /// Return true if this is a branch or an instruction which directly
  /// writes to the program counter. Considered 'may' affect rather than
  /// 'does' affect as things like predication are not taken into account.
  bool mayAffectControlFlow(const MCInst &MI, const MCRegisterInfo &RI) const;

  /// Return true if this instruction has a predicate operand
  /// that controls execution. It may be set to 'always', or may be set to other
  /// values. There are various methods in TargetInstrInfo that can be used to
  /// control and modify the predicate in this instruction.
  bool isPredicable() const { return Flags & (1ULL << MCID::Predicable); }

  /// Return true if this instruction is a comparison.
  bool isCompare() const { return Flags & (1ULL << MCID::Compare); }

  /// Return true if this instruction is a move immediate
  /// (including conditional moves) instruction.
  bool isMoveImmediate() const { return Flags & (1ULL << MCID::MoveImm); }

  /// Return true if this instruction is a bitcast instruction.
  bool isBitcast() const { return Flags & (1ULL << MCID::Bitcast); }

  /// Return true if this is a select instruction.
  bool isSelect() const { return Flags & (1ULL << MCID::Select); }

  /// Return true if this instruction cannot be safely
  /// duplicated.  For example, if the instruction has a unique labels attached
  /// to it, duplicating it would cause multiple definition errors.
  bool isNotDuplicable() const { return Flags & (1ULL << MCID::NotDuplicable); }

  /// Returns true if the specified instruction has a delay slot which
  /// must be filled by the code generator.
  bool hasDelaySlot() const { return Flags & (1ULL << MCID::DelaySlot); }

  /// Return true for instructions that can be folded as memory operands
  /// in other instructions. The most common use for this is instructions that
  /// are simple loads from memory that don't modify the loaded value in any
  /// way, but it can also be used for instructions that can be expressed as
  /// constant-pool loads, such as V_SETALLONES on x86, to allow them to be
  /// folded when it is beneficial.  This should only be set on instructions
  /// that return a value in their only virtual register definition.
  bool canFoldAsLoad() const { return Flags & (1ULL << MCID::FoldableAsLoad); }

  /// Return true if this instruction behaves
  /// the same way as the generic REG_SEQUENCE instructions.
  /// E.g., on ARM,
  /// dX VMOVDRR rY, rZ
  /// is equivalent to
  /// dX = REG_SEQUENCE rY, ssub_0, rZ, ssub_1.
  ///
  /// Note that for the optimizers to be able to take advantage of
  /// this property, TargetInstrInfo::getRegSequenceLikeInputs has to be
  /// override accordingly.
  bool isRegSequenceLike() const { return Flags & (1ULL << MCID::RegSequence); }

  /// Return true if this instruction behaves
  /// the same way as the generic EXTRACT_SUBREG instructions.
  /// E.g., on ARM,
  /// rX, rY VMOVRRD dZ
  /// is equivalent to two EXTRACT_SUBREG:
  /// rX = EXTRACT_SUBREG dZ, ssub_0
  /// rY = EXTRACT_SUBREG dZ, ssub_1
  ///
  /// Note that for the optimizers to be able to take advantage of
  /// this property, TargetInstrInfo::getExtractSubregLikeInputs has to be
  /// override accordingly.
  bool isExtractSubregLike() const {
    return Flags & (1ULL << MCID::ExtractSubreg);
  }

  /// Return true if this instruction behaves
  /// the same way as the generic INSERT_SUBREG instructions.
  /// E.g., on ARM,
  /// dX = VSETLNi32 dY, rZ, Imm
  /// is equivalent to a INSERT_SUBREG:
  /// dX = INSERT_SUBREG dY, rZ, translateImmToSubIdx(Imm)
  ///
  /// Note that for the optimizers to be able to take advantage of
  /// this property, TargetInstrInfo::getInsertSubregLikeInputs has to be
  /// override accordingly.
  bool isInsertSubregLike() const { return Flags & (1ULL << MCID::InsertSubreg); }


  /// Return true if this instruction is convergent.
  ///
  /// Convergent instructions may not be made control-dependent on any
  /// additional values.
  bool isConvergent() const { return Flags & (1ULL << MCID::Convergent); }

  /// Return true if variadic operands of this instruction are definitions.
  bool variadicOpsAreDefs() const {
    return Flags & (1ULL << MCID::VariadicOpsAreDefs);
  }

  /// Return true if this instruction authenticates a pointer (e.g. LDRAx/BRAx
  /// from ARMv8.3, which perform loads/branches with authentication).
  ///
  /// An authenticated instruction may fail in an ABI-defined manner when
  /// operating on an invalid signed pointer.
  bool isAuthenticated() const {
    return Flags & (1ULL << MCID::Authenticated);
  }

  //===--------------------------------------------------------------------===//
  // Side Effect Analysis
  //===--------------------------------------------------------------------===//

  /// Return true if this instruction could possibly read memory.
  /// Instructions with this flag set are not necessarily simple load
  /// instructions, they may load a value and modify it, for example.
  bool mayLoad() const { return Flags & (1ULL << MCID::MayLoad); }

  /// Return true if this instruction could possibly modify memory.
  /// Instructions with this flag set are not necessarily simple store
  /// instructions, they may store a modified value based on their operands, or
  /// may not actually modify anything, for example.
  bool mayStore() const { return Flags & (1ULL << MCID::MayStore); }

  /// Return true if this instruction may raise a floating-point exception.
  bool mayRaiseFPException() const {
    return Flags & (1ULL << MCID::MayRaiseFPException);
  }

  /// Return true if this instruction has side
  /// effects that are not modeled by other flags.  This does not return true
  /// for instructions whose effects are captured by:
  ///
  ///  1. Their operand list and implicit definition/use list.  Register use/def
  ///     info is explicit for instructions.
  ///  2. Memory accesses.  Use mayLoad/mayStore.
  ///  3. Calling, branching, returning: use isCall/isReturn/isBranch.
  ///
  /// Examples of side effects would be modifying 'invisible' machine state like
  /// a control register, flushing a cache, modifying a register invisible to
  /// LLVM, etc.
  bool hasUnmodeledSideEffects() const {
    return Flags & (1ULL << MCID::UnmodeledSideEffects);
  }

  //===--------------------------------------------------------------------===//
  // Flags that indicate whether an instruction can be modified by a method.
  //===--------------------------------------------------------------------===//

  /// Return true if this may be a 2- or 3-address instruction (of the
  /// form "X = op Y, Z, ..."), which produces the same result if Y and Z are
  /// exchanged.  If this flag is set, then the
  /// TargetInstrInfo::commuteInstruction method may be used to hack on the
  /// instruction.
  ///
  /// Note that this flag may be set on instructions that are only commutable
  /// sometimes.  In these cases, the call to commuteInstruction will fail.
  /// Also note that some instructions require non-trivial modification to
  /// commute them.
  bool isCommutable() const { return Flags & (1ULL << MCID::Commutable); }

  /// Return true if this is a 2-address instruction which can be changed
  /// into a 3-address instruction if needed.  Doing this transformation can be
  /// profitable in the register allocator, because it means that the
  /// instruction can use a 2-address form if possible, but degrade into a less
  /// efficient form if the source and dest register cannot be assigned to the
  /// same register.  For example, this allows the x86 backend to turn a "shl
  /// reg, 3" instruction into an LEA instruction, which is the same speed as
  /// the shift but has bigger code size.
  ///
  /// If this returns true, then the target must implement the
  /// TargetInstrInfo::convertToThreeAddress method for this instruction, which
  /// is allowed to fail if the transformation isn't valid for this specific
  /// instruction (e.g. shl reg, 4 on x86).
  ///
  bool isConvertibleTo3Addr() const {
    return Flags & (1ULL << MCID::ConvertibleTo3Addr);
  }

  /// Return true if this instruction requires custom insertion support
  /// when the DAG scheduler is inserting it into a machine basic block.  If
  /// this is true for the instruction, it basically means that it is a pseudo
  /// instruction used at SelectionDAG time that is expanded out into magic code
  /// by the target when MachineInstrs are formed.
  ///
  /// If this is true, the TargetLoweringInfo::InsertAtEndOfBasicBlock method
  /// is used to insert this into the MachineBasicBlock.
  bool usesCustomInsertionHook() const {
    return Flags & (1ULL << MCID::UsesCustomInserter);
  }

  /// Return true if this instruction requires *adjustment* after
  /// instruction selection by calling a target hook. For example, this can be
  /// used to fill in ARM 's' optional operand depending on whether the
  /// conditional flag register is used.
  bool hasPostISelHook() const { return Flags & (1ULL << MCID::HasPostISelHook); }

  /// Returns true if this instruction is a candidate for remat. This
  /// flag is only used in TargetInstrInfo method isTriviallyRematerializable.
  ///
  /// If this flag is set, the isReallyTriviallyReMaterializable() method is
  /// called to verify the instruction is really rematerializable.
  bool isRematerializable() const {
    return Flags & (1ULL << MCID::Rematerializable);
  }

  /// Returns true if this instruction has the same cost (or less) than a
  /// move instruction. This is useful during certain types of optimizations
  /// (e.g., remat during two-address conversion or machine licm) where we would
  /// like to remat or hoist the instruction, but not if it costs more than
  /// moving the instruction into the appropriate register. Note, we are not
  /// marking copies from and to the same register class with this flag.
  ///
  /// This method could be called by interface TargetInstrInfo::isAsCheapAsAMove
  /// for different subtargets.
  bool isAsCheapAsAMove() const { return Flags & (1ULL << MCID::CheapAsAMove); }

  /// Returns true if this instruction source operands have special
  /// register allocation requirements that are not captured by the operand
  /// register classes. e.g. ARM::STRD's two source registers must be an even /
  /// odd pair, ARM::STM registers have to be in ascending order.  Post-register
  /// allocation passes should not attempt to change allocations for sources of
  /// instructions with this flag.
  bool hasExtraSrcRegAllocReq() const {
    return Flags & (1ULL << MCID::ExtraSrcRegAllocReq);
  }

  /// Returns true if this instruction def operands have special register
  /// allocation requirements that are not captured by the operand register
  /// classes. e.g. ARM::LDRD's two def registers must be an even / odd pair,
  /// ARM::LDM registers have to be in ascending order.  Post-register
  /// allocation passes should not attempt to change allocations for definitions
  /// of instructions with this flag.
  bool hasExtraDefRegAllocReq() const {
    return Flags & (1ULL << MCID::ExtraDefRegAllocReq);
  }

  /// Return a list of registers that are potentially read by any
  /// instance of this machine instruction.  For example, on X86, the "adc"
  /// instruction adds two register operands and adds the carry bit in from the
  /// flags register.  In this case, the instruction is marked as implicitly
  /// reading the flags.  Likewise, the variable shift instruction on X86 is
  /// marked as implicitly reading the 'CL' register, which it always does.
  ArrayRef<MCPhysReg> implicit_uses() const {
    auto ImplicitOps =
        reinterpret_cast<const MCPhysReg *>(this + Opcode + 1) + ImplicitOffset;
    return {ImplicitOps, NumImplicitUses};
  }

  /// Return a list of registers that are potentially written by any
  /// instance of this machine instruction.  For example, on X86, many
  /// instructions implicitly set the flags register.  In this case, they are
  /// marked as setting the FLAGS.  Likewise, many instructions always deposit
  /// their result in a physical register.  For example, the X86 divide
  /// instruction always deposits the quotient and remainder in the EAX/EDX
  /// registers.  For that instruction, this will return a list containing the
  /// EAX/EDX/EFLAGS registers.
  ArrayRef<MCPhysReg> implicit_defs() const {
    auto ImplicitOps =
        reinterpret_cast<const MCPhysReg *>(this + Opcode + 1) + ImplicitOffset;
    return {ImplicitOps + NumImplicitUses, NumImplicitDefs};
  }

  /// Return true if this instruction implicitly
  /// uses the specified physical register.
  bool hasImplicitUseOfPhysReg(unsigned Reg) const {
    return is_contained(implicit_uses(), Reg);
  }

  /// Return true if this instruction implicitly
  /// defines the specified physical register.
  bool hasImplicitDefOfPhysReg(unsigned Reg,
                               const MCRegisterInfo *MRI = nullptr) const;

  /// Return the scheduling class for this instruction.  The
  /// scheduling class is an index into the InstrItineraryData table.  This
  /// returns zero if there is no known scheduling information for the
  /// instruction.
  unsigned getSchedClass() const { return SchedClass; }

  /// Return the number of bytes in the encoding of this instruction,
  /// or zero if the encoding size cannot be known from the opcode.
  unsigned getSize() const { return Size; }

  /// Find the index of the first operand in the
  /// operand list that is used to represent the predicate. It returns -1 if
  /// none is found.
  int findFirstPredOperandIdx() const {
    if (isPredicable()) {
      for (unsigned i = 0, e = getNumOperands(); i != e; ++i)
        if (operands()[i].isPredicate())
          return i;
    }
    return -1;
  }

  /// Return true if this instruction defines the specified physical
  /// register, either explicitly or implicitly.
  bool hasDefOfPhysReg(const MCInst &MI, unsigned Reg,
                       const MCRegisterInfo &RI) const;
};

} // end namespace llvm

#endif
