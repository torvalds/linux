//===- CodeGenInstruction.h - Instruction Class Wrapper ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a wrapper class for the 'Instruction' TableGen class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_UTILS_TABLEGEN_CODEGENINSTRUCTION_H
#define LLVM_UTILS_TABLEGEN_CODEGENINSTRUCTION_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGenTypes/MachineValueType.h"
#include "llvm/TableGen/Record.h"
#include <cassert>
#include <string>
#include <utility>
#include <vector>

namespace llvm {
class CodeGenTarget;

class CGIOperandList {
public:
  class ConstraintInfo {
    enum { None, EarlyClobber, Tied } Kind = None;
    unsigned OtherTiedOperand = 0;

  public:
    ConstraintInfo() = default;

    static ConstraintInfo getEarlyClobber() {
      ConstraintInfo I;
      I.Kind = EarlyClobber;
      I.OtherTiedOperand = 0;
      return I;
    }

    static ConstraintInfo getTied(unsigned Op) {
      ConstraintInfo I;
      I.Kind = Tied;
      I.OtherTiedOperand = Op;
      return I;
    }

    bool isNone() const { return Kind == None; }
    bool isEarlyClobber() const { return Kind == EarlyClobber; }
    bool isTied() const { return Kind == Tied; }

    unsigned getTiedOperand() const {
      assert(isTied());
      return OtherTiedOperand;
    }

    bool operator==(const ConstraintInfo &RHS) const {
      if (Kind != RHS.Kind)
        return false;
      if (Kind == Tied && OtherTiedOperand != RHS.OtherTiedOperand)
        return false;
      return true;
    }
    bool operator!=(const ConstraintInfo &RHS) const { return !(*this == RHS); }
  };

  /// OperandInfo - The information we keep track of for each operand in the
  /// operand list for a tablegen instruction.
  struct OperandInfo {
    /// Rec - The definition this operand is declared as.
    ///
    Record *Rec;

    /// Name - If this operand was assigned a symbolic name, this is it,
    /// otherwise, it's empty.
    std::string Name;

    /// The names of sub-operands, if given, otherwise empty.
    std::vector<std::string> SubOpNames;

    /// PrinterMethodName - The method used to print operands of this type in
    /// the asmprinter.
    std::string PrinterMethodName;

    /// The method used to get the machine operand value for binary
    /// encoding, per sub-operand. If empty, uses "getMachineOpValue".
    std::vector<std::string> EncoderMethodNames;

    /// OperandType - A value from MCOI::OperandType representing the type of
    /// the operand.
    std::string OperandType;

    /// MIOperandNo - Currently (this is meant to be phased out), some logical
    /// operands correspond to multiple MachineInstr operands.  In the X86
    /// target for example, one address operand is represented as 4
    /// MachineOperands.  Because of this, the operand number in the
    /// OperandList may not match the MachineInstr operand num.  Until it
    /// does, this contains the MI operand index of this operand.
    unsigned MIOperandNo;
    unsigned MINumOperands; // The number of operands.

    /// DoNotEncode - Bools are set to true in this vector for each operand in
    /// the DisableEncoding list.  These should not be emitted by the code
    /// emitter.
    BitVector DoNotEncode;

    /// MIOperandInfo - Default MI operand type. Note an operand may be made
    /// up of multiple MI operands.
    DagInit *MIOperandInfo;

    /// Constraint info for this operand.  This operand can have pieces, so we
    /// track constraint info for each.
    std::vector<ConstraintInfo> Constraints;

    OperandInfo(Record *R, const std::string &N, const std::string &PMN,
                const std::string &OT, unsigned MION, unsigned MINO,
                DagInit *MIOI)
        : Rec(R), Name(N), SubOpNames(MINO), PrinterMethodName(PMN),
          EncoderMethodNames(MINO), OperandType(OT), MIOperandNo(MION),
          MINumOperands(MINO), DoNotEncode(MINO), MIOperandInfo(MIOI),
          Constraints(MINO) {}

    /// getTiedOperand - If this operand is tied to another one, return the
    /// other operand number.  Otherwise, return -1.
    int getTiedRegister() const {
      for (unsigned j = 0, e = Constraints.size(); j != e; ++j) {
        const CGIOperandList::ConstraintInfo &CI = Constraints[j];
        if (CI.isTied())
          return CI.getTiedOperand();
      }
      return -1;
    }
  };

  CGIOperandList(Record *D);

  Record *TheDef; // The actual record containing this OperandList.

  /// NumDefs - Number of def operands declared, this is the number of
  /// elements in the instruction's (outs) list.
  ///
  unsigned NumDefs;

  /// OperandList - The list of declared operands, along with their declared
  /// type (which is a record).
  std::vector<OperandInfo> OperandList;

  /// SubOpAliases - List of alias names for suboperands.
  StringMap<std::pair<unsigned, unsigned>> SubOpAliases;

  // Information gleaned from the operand list.
  bool isPredicable;
  bool hasOptionalDef;
  bool isVariadic;

  // Provide transparent accessors to the operand list.
  bool empty() const { return OperandList.empty(); }
  unsigned size() const { return OperandList.size(); }
  const OperandInfo &operator[](unsigned i) const { return OperandList[i]; }
  OperandInfo &operator[](unsigned i) { return OperandList[i]; }
  OperandInfo &back() { return OperandList.back(); }
  const OperandInfo &back() const { return OperandList.back(); }

  typedef std::vector<OperandInfo>::iterator iterator;
  typedef std::vector<OperandInfo>::const_iterator const_iterator;
  iterator begin() { return OperandList.begin(); }
  const_iterator begin() const { return OperandList.begin(); }
  iterator end() { return OperandList.end(); }
  const_iterator end() const { return OperandList.end(); }

  /// getOperandNamed - Return the index of the operand with the specified
  /// non-empty name.  If the instruction does not have an operand with the
  /// specified name, abort.
  unsigned getOperandNamed(StringRef Name) const;

  /// hasOperandNamed - Query whether the instruction has an operand of the
  /// given name. If so, return true and set OpIdx to the index of the
  /// operand. Otherwise, return false.
  bool hasOperandNamed(StringRef Name, unsigned &OpIdx) const;

  bool hasSubOperandAlias(StringRef Name,
                          std::pair<unsigned, unsigned> &SubOp) const;

  /// ParseOperandName - Parse an operand name like "$foo" or "$foo.bar",
  /// where $foo is a whole operand and $foo.bar refers to a suboperand.
  /// This aborts if the name is invalid.  If AllowWholeOp is true, references
  /// to operands with suboperands are allowed, otherwise not.
  std::pair<unsigned, unsigned> ParseOperandName(StringRef Op,
                                                 bool AllowWholeOp = true);

  /// getFlattenedOperandNumber - Flatten a operand/suboperand pair into a
  /// flat machineinstr operand #.
  unsigned getFlattenedOperandNumber(std::pair<unsigned, unsigned> Op) const {
    return OperandList[Op.first].MIOperandNo + Op.second;
  }

  /// getSubOperandNumber - Unflatten a operand number into an
  /// operand/suboperand pair.
  std::pair<unsigned, unsigned> getSubOperandNumber(unsigned Op) const {
    for (unsigned i = 0;; ++i) {
      assert(i < OperandList.size() && "Invalid flat operand #");
      if (OperandList[i].MIOperandNo + OperandList[i].MINumOperands > Op)
        return std::pair(i, Op - OperandList[i].MIOperandNo);
    }
  }

  /// isFlatOperandNotEmitted - Return true if the specified flat operand #
  /// should not be emitted with the code emitter.
  bool isFlatOperandNotEmitted(unsigned FlatOpNo) const {
    std::pair<unsigned, unsigned> Op = getSubOperandNumber(FlatOpNo);
    if (OperandList[Op.first].DoNotEncode.size() > Op.second)
      return OperandList[Op.first].DoNotEncode[Op.second];
    return false;
  }

  void ProcessDisableEncoding(StringRef Value);
};

class CodeGenInstruction {
public:
  Record *TheDef;      // The actual record defining this instruction.
  StringRef Namespace; // The namespace the instruction is in.

  /// AsmString - The format string used to emit a .s file for the
  /// instruction.
  std::string AsmString;

  /// Operands - This is information about the (ins) and (outs) list specified
  /// to the instruction.
  CGIOperandList Operands;

  /// ImplicitDefs/ImplicitUses - These are lists of registers that are
  /// implicitly defined and used by the instruction.
  std::vector<Record *> ImplicitDefs, ImplicitUses;

  // Various boolean values we track for the instruction.
  bool isPreISelOpcode : 1;
  bool isReturn : 1;
  bool isEHScopeReturn : 1;
  bool isBranch : 1;
  bool isIndirectBranch : 1;
  bool isCompare : 1;
  bool isMoveImm : 1;
  bool isMoveReg : 1;
  bool isBitcast : 1;
  bool isSelect : 1;
  bool isBarrier : 1;
  bool isCall : 1;
  bool isAdd : 1;
  bool isTrap : 1;
  bool canFoldAsLoad : 1;
  bool mayLoad : 1;
  bool mayLoad_Unset : 1;
  bool mayStore : 1;
  bool mayStore_Unset : 1;
  bool mayRaiseFPException : 1;
  bool isPredicable : 1;
  bool isConvertibleToThreeAddress : 1;
  bool isCommutable : 1;
  bool isTerminator : 1;
  bool isReMaterializable : 1;
  bool hasDelaySlot : 1;
  bool usesCustomInserter : 1;
  bool hasPostISelHook : 1;
  bool hasCtrlDep : 1;
  bool isNotDuplicable : 1;
  bool hasSideEffects : 1;
  bool hasSideEffects_Unset : 1;
  bool isAsCheapAsAMove : 1;
  bool hasExtraSrcRegAllocReq : 1;
  bool hasExtraDefRegAllocReq : 1;
  bool isCodeGenOnly : 1;
  bool isPseudo : 1;
  bool isMeta : 1;
  bool isRegSequence : 1;
  bool isExtractSubreg : 1;
  bool isInsertSubreg : 1;
  bool isConvergent : 1;
  bool hasNoSchedulingInfo : 1;
  bool FastISelShouldIgnore : 1;
  bool hasChain : 1;
  bool hasChain_Inferred : 1;
  bool variadicOpsAreDefs : 1;
  bool isAuthenticated : 1;

  std::string DeprecatedReason;
  bool HasComplexDeprecationPredicate;

  /// Are there any undefined flags?
  bool hasUndefFlags() const {
    return mayLoad_Unset || mayStore_Unset || hasSideEffects_Unset;
  }

  // The record used to infer instruction flags, or NULL if no flag values
  // have been inferred.
  Record *InferredFrom;

  // The enum value assigned by CodeGenTarget::computeInstrsByEnum.
  mutable unsigned EnumVal = 0;

  CodeGenInstruction(Record *R);

  /// HasOneImplicitDefWithKnownVT - If the instruction has at least one
  /// implicit def and it has a known VT, return the VT, otherwise return
  /// MVT::Other.
  MVT::SimpleValueType
  HasOneImplicitDefWithKnownVT(const CodeGenTarget &TargetInfo) const;

  /// FlattenAsmStringVariants - Flatten the specified AsmString to only
  /// include text from the specified variant, returning the new string.
  static std::string FlattenAsmStringVariants(StringRef AsmString,
                                              unsigned Variant);

  // Is the specified operand in a generic instruction implicitly a pointer.
  // This can be used on intructions that use typeN or ptypeN to identify
  // operands that should be considered as pointers even though SelectionDAG
  // didn't make a distinction between integer and pointers.
  bool isInOperandAPointer(unsigned i) const {
    return isOperandImpl("InOperandList", i, "IsPointer");
  }

  bool isOutOperandAPointer(unsigned i) const {
    return isOperandImpl("OutOperandList", i, "IsPointer");
  }

  /// Check if the operand is required to be an immediate.
  bool isInOperandImmArg(unsigned i) const {
    return isOperandImpl("InOperandList", i, "IsImmediate");
  }

  /// Return true if the instruction uses a variable length encoding.
  bool isVariableLengthEncoding() const {
    const RecordVal *RV = TheDef->getValue("Inst");
    return RV && isa<DagInit>(RV->getValue());
  }

private:
  bool isOperandImpl(StringRef OpListName, unsigned i,
                     StringRef PropertyName) const;
};
} // namespace llvm

#endif
