//===- llvm/CodeGen/GlobalISel/GenericMachineInstrs.h -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Declares convenience wrapper classes for interpreting MachineInstr instances
/// as specific generic operations.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_GLOBALISEL_GENERICMACHINEINSTRS_H
#define LLVM_CODEGEN_GLOBALISEL_GENERICMACHINEINSTRS_H

#include "llvm/ADT/APInt.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineMemOperand.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Support/Casting.h"

namespace llvm {

/// A base class for all GenericMachineInstrs.
class GenericMachineInstr : public MachineInstr {
  constexpr static unsigned PoisonFlags = NoUWrap | NoSWrap | NoUSWrap |
                                          IsExact | Disjoint | NonNeg |
                                          FmNoNans | FmNoInfs;

public:
  GenericMachineInstr() = delete;

  /// Access the Idx'th operand as a register and return it.
  /// This assumes that the Idx'th operand is a Register type.
  Register getReg(unsigned Idx) const { return getOperand(Idx).getReg(); }

  static bool classof(const MachineInstr *MI) {
    return isPreISelGenericOpcode(MI->getOpcode());
  }

  bool hasPoisonGeneratingFlags() const { return getFlags() & PoisonFlags; }

  void dropPoisonGeneratingFlags() {
    clearFlags(PoisonFlags);
    assert(!hasPoisonGeneratingFlags());
  }
};

/// Provides common memory operand functionality.
class GMemOperation : public GenericMachineInstr {
public:
  /// Get the MachineMemOperand on this instruction.
  MachineMemOperand &getMMO() const { return **memoperands_begin(); }

  /// Returns true if the attached MachineMemOperand  has the atomic flag set.
  bool isAtomic() const { return getMMO().isAtomic(); }
  /// Returns true if the attached MachineMemOpeand as the volatile flag set.
  bool isVolatile() const { return getMMO().isVolatile(); }
  /// Returns true if the memory operation is neither atomic or volatile.
  bool isSimple() const { return !isAtomic() && !isVolatile(); }
  /// Returns true if this memory operation doesn't have any ordering
  /// constraints other than normal aliasing. Volatile and (ordered) atomic
  /// memory operations can't be reordered.
  bool isUnordered() const { return getMMO().isUnordered(); }

  /// Returns the size in bytes of the memory access.
  LocationSize getMemSize() const { return getMMO().getSize(); }
  /// Returns the size in bits of the memory access.
  LocationSize getMemSizeInBits() const { return getMMO().getSizeInBits(); }

  static bool classof(const MachineInstr *MI) {
    return GenericMachineInstr::classof(MI) && MI->hasOneMemOperand();
  }
};

/// Represents any type of generic load or store.
/// G_LOAD, G_STORE, G_ZEXTLOAD, G_SEXTLOAD.
class GLoadStore : public GMemOperation {
public:
  /// Get the source register of the pointer value.
  Register getPointerReg() const { return getOperand(1).getReg(); }

  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_LOAD:
    case TargetOpcode::G_STORE:
    case TargetOpcode::G_ZEXTLOAD:
    case TargetOpcode::G_SEXTLOAD:
      return true;
    default:
      return false;
    }
  }
};

/// Represents indexed loads. These are different enough from regular loads
/// that they get their own class. Including them in GAnyLoad would probably
/// make a footgun for someone.
class GIndexedLoad : public GMemOperation {
public:
  /// Get the definition register of the loaded value.
  Register getDstReg() const { return getOperand(0).getReg(); }
  /// Get the def register of the writeback value.
  Register getWritebackReg() const { return getOperand(1).getReg(); }
  /// Get the base register of the pointer value.
  Register getBaseReg() const { return getOperand(2).getReg(); }
  /// Get the offset register of the pointer value.
  Register getOffsetReg() const { return getOperand(3).getReg(); }

  bool isPre() const { return getOperand(4).getImm() == 1; }
  bool isPost() const { return !isPre(); }

  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_INDEXED_LOAD;
  }
};

/// Represents a G_INDEX_ZEXTLOAD/G_INDEXED_SEXTLOAD.
class GIndexedExtLoad : public GIndexedLoad {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_INDEXED_SEXTLOAD ||
           MI->getOpcode() == TargetOpcode::G_INDEXED_ZEXTLOAD;
  }
};

/// Represents either G_INDEXED_LOAD, G_INDEXED_ZEXTLOAD or G_INDEXED_SEXTLOAD.
class GIndexedAnyExtLoad : public GIndexedLoad {
public:
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_INDEXED_LOAD:
    case TargetOpcode::G_INDEXED_ZEXTLOAD:
    case TargetOpcode::G_INDEXED_SEXTLOAD:
      return true;
    default:
      return false;
    }
  }
};

/// Represents a G_ZEXTLOAD.
class GIndexedZExtLoad : GIndexedExtLoad {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_INDEXED_ZEXTLOAD;
  }
};

/// Represents a G_SEXTLOAD.
class GIndexedSExtLoad : GIndexedExtLoad {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_INDEXED_SEXTLOAD;
  }
};

/// Represents indexed stores.
class GIndexedStore : public GMemOperation {
public:
  /// Get the def register of the writeback value.
  Register getWritebackReg() const { return getOperand(0).getReg(); }
  /// Get the stored value register.
  Register getValueReg() const { return getOperand(1).getReg(); }
  /// Get the base register of the pointer value.
  Register getBaseReg() const { return getOperand(2).getReg(); }
  /// Get the offset register of the pointer value.
  Register getOffsetReg() const { return getOperand(3).getReg(); }

  bool isPre() const { return getOperand(4).getImm() == 1; }
  bool isPost() const { return !isPre(); }

  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_INDEXED_STORE;
  }
};

/// Represents any generic load, including sign/zero extending variants.
class GAnyLoad : public GLoadStore {
public:
  /// Get the definition register of the loaded value.
  Register getDstReg() const { return getOperand(0).getReg(); }

  /// Returns the Ranges that describes the dereference.
  const MDNode *getRanges() const {
    return getMMO().getRanges();
  }

  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_LOAD:
    case TargetOpcode::G_ZEXTLOAD:
    case TargetOpcode::G_SEXTLOAD:
      return true;
    default:
      return false;
    }
  }
};

/// Represents a G_LOAD.
class GLoad : public GAnyLoad {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_LOAD;
  }
};

/// Represents either a G_SEXTLOAD or G_ZEXTLOAD.
class GExtLoad : public GAnyLoad {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_SEXTLOAD ||
           MI->getOpcode() == TargetOpcode::G_ZEXTLOAD;
  }
};

/// Represents a G_SEXTLOAD.
class GSExtLoad : public GExtLoad {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_SEXTLOAD;
  }
};

/// Represents a G_ZEXTLOAD.
class GZExtLoad : public GExtLoad {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_ZEXTLOAD;
  }
};

/// Represents a G_STORE.
class GStore : public GLoadStore {
public:
  /// Get the stored value register.
  Register getValueReg() const { return getOperand(0).getReg(); }

  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_STORE;
  }
};

/// Represents a G_UNMERGE_VALUES.
class GUnmerge : public GenericMachineInstr {
public:
  /// Returns the number of def registers.
  unsigned getNumDefs() const { return getNumOperands() - 1; }
  /// Get the unmerge source register.
  Register getSourceReg() const { return getOperand(getNumDefs()).getReg(); }

  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_UNMERGE_VALUES;
  }
};

/// Represents G_BUILD_VECTOR, G_CONCAT_VECTORS or G_MERGE_VALUES.
/// All these have the common property of generating a single value from
/// multiple sources.
class GMergeLikeInstr : public GenericMachineInstr {
public:
  /// Returns the number of source registers.
  unsigned getNumSources() const { return getNumOperands() - 1; }
  /// Returns the I'th source register.
  Register getSourceReg(unsigned I) const { return getReg(I + 1); }

  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_MERGE_VALUES:
    case TargetOpcode::G_CONCAT_VECTORS:
    case TargetOpcode::G_BUILD_VECTOR:
      return true;
    default:
      return false;
    }
  }
};

/// Represents a G_MERGE_VALUES.
class GMerge : public GMergeLikeInstr {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_MERGE_VALUES;
  }
};

/// Represents a G_CONCAT_VECTORS.
class GConcatVectors : public GMergeLikeInstr {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_CONCAT_VECTORS;
  }
};

/// Represents a G_BUILD_VECTOR.
class GBuildVector : public GMergeLikeInstr {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_BUILD_VECTOR;
  }
};

/// Represents a G_BUILD_VECTOR_TRUNC.
class GBuildVectorTrunc : public GMergeLikeInstr {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_BUILD_VECTOR_TRUNC;
  }
};

/// Represents a G_SHUFFLE_VECTOR.
class GShuffleVector : public GenericMachineInstr {
public:
  Register getSrc1Reg() const { return getOperand(1).getReg(); }
  Register getSrc2Reg() const { return getOperand(2).getReg(); }
  ArrayRef<int> getMask() const { return getOperand(3).getShuffleMask(); }

  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_SHUFFLE_VECTOR;
  }
};

/// Represents a G_PTR_ADD.
class GPtrAdd : public GenericMachineInstr {
public:
  Register getBaseReg() const { return getReg(1); }
  Register getOffsetReg() const { return getReg(2); }

  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_PTR_ADD;
  }
};

/// Represents a G_IMPLICIT_DEF.
class GImplicitDef : public GenericMachineInstr {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_IMPLICIT_DEF;
  }
};

/// Represents a G_SELECT.
class GSelect : public GenericMachineInstr {
public:
  Register getCondReg() const { return getReg(1); }
  Register getTrueReg() const { return getReg(2); }
  Register getFalseReg() const { return getReg(3); }

  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_SELECT;
  }
};

/// Represent a G_ICMP or G_FCMP.
class GAnyCmp : public GenericMachineInstr {
public:
  CmpInst::Predicate getCond() const {
    return static_cast<CmpInst::Predicate>(getOperand(1).getPredicate());
  }
  Register getLHSReg() const { return getReg(2); }
  Register getRHSReg() const { return getReg(3); }

  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_ICMP ||
           MI->getOpcode() == TargetOpcode::G_FCMP;
  }
};

/// Represent a G_ICMP.
class GICmp : public GAnyCmp {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_ICMP;
  }
};

/// Represent a G_FCMP.
class GFCmp : public GAnyCmp {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_FCMP;
  }
};

/// Represents overflowing binary operations.
/// Only carry-out:
/// G_UADDO, G_SADDO, G_USUBO, G_SSUBO, G_UMULO, G_SMULO
/// Carry-in and carry-out:
/// G_UADDE, G_SADDE, G_USUBE, G_SSUBE
class GBinOpCarryOut : public GenericMachineInstr {
public:
  Register getDstReg() const { return getReg(0); }
  Register getCarryOutReg() const { return getReg(1); }
  MachineOperand &getLHS() { return getOperand(2); }
  MachineOperand &getRHS() { return getOperand(3); }
  Register getLHSReg() const { return getOperand(2).getReg(); }
  Register getRHSReg() const { return getOperand(3).getReg(); }

  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_UADDO:
    case TargetOpcode::G_SADDO:
    case TargetOpcode::G_USUBO:
    case TargetOpcode::G_SSUBO:
    case TargetOpcode::G_UADDE:
    case TargetOpcode::G_SADDE:
    case TargetOpcode::G_USUBE:
    case TargetOpcode::G_SSUBE:
    case TargetOpcode::G_UMULO:
    case TargetOpcode::G_SMULO:
      return true;
    default:
      return false;
    }
  }
};

/// Represents overflowing add/sub operations.
/// Only carry-out:
/// G_UADDO, G_SADDO, G_USUBO, G_SSUBO
/// Carry-in and carry-out:
/// G_UADDE, G_SADDE, G_USUBE, G_SSUBE
class GAddSubCarryOut : public GBinOpCarryOut {
public:
  bool isAdd() const {
    switch (getOpcode()) {
    case TargetOpcode::G_UADDO:
    case TargetOpcode::G_SADDO:
    case TargetOpcode::G_UADDE:
    case TargetOpcode::G_SADDE:
      return true;
    default:
      return false;
    }
  }
  bool isSub() const { return !isAdd(); }

  bool isSigned() const {
    switch (getOpcode()) {
    case TargetOpcode::G_SADDO:
    case TargetOpcode::G_SSUBO:
    case TargetOpcode::G_SADDE:
    case TargetOpcode::G_SSUBE:
      return true;
    default:
      return false;
    }
  }
  bool isUnsigned() const { return !isSigned(); }

  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_UADDO:
    case TargetOpcode::G_SADDO:
    case TargetOpcode::G_USUBO:
    case TargetOpcode::G_SSUBO:
    case TargetOpcode::G_UADDE:
    case TargetOpcode::G_SADDE:
    case TargetOpcode::G_USUBE:
    case TargetOpcode::G_SSUBE:
      return true;
    default:
      return false;
    }
  }
};

/// Represents overflowing add operations.
/// G_UADDO, G_SADDO
class GAddCarryOut : public GBinOpCarryOut {
public:
  bool isSigned() const { return getOpcode() == TargetOpcode::G_SADDO; }

  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_UADDO:
    case TargetOpcode::G_SADDO:
      return true;
    default:
      return false;
    }
  }
};

/// Represents overflowing add/sub operations that also consume a carry-in.
/// G_UADDE, G_SADDE, G_USUBE, G_SSUBE
class GAddSubCarryInOut : public GAddSubCarryOut {
public:
  Register getCarryInReg() const { return getReg(4); }

  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_UADDE:
    case TargetOpcode::G_SADDE:
    case TargetOpcode::G_USUBE:
    case TargetOpcode::G_SSUBE:
      return true;
    default:
      return false;
    }
  }
};

/// Represents a call to an intrinsic.
class GIntrinsic final : public GenericMachineInstr {
public:
  Intrinsic::ID getIntrinsicID() const {
    return getOperand(getNumExplicitDefs()).getIntrinsicID();
  }

  bool is(Intrinsic::ID ID) const { return getIntrinsicID() == ID; }

  bool hasSideEffects() const {
    switch (getOpcode()) {
    case TargetOpcode::G_INTRINSIC_W_SIDE_EFFECTS:
    case TargetOpcode::G_INTRINSIC_CONVERGENT_W_SIDE_EFFECTS:
      return true;
    default:
      return false;
    }
  }

  bool isConvergent() const {
    switch (getOpcode()) {
    case TargetOpcode::G_INTRINSIC_CONVERGENT:
    case TargetOpcode::G_INTRINSIC_CONVERGENT_W_SIDE_EFFECTS:
      return true;
    default:
      return false;
    }
  }

  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_INTRINSIC:
    case TargetOpcode::G_INTRINSIC_W_SIDE_EFFECTS:
    case TargetOpcode::G_INTRINSIC_CONVERGENT:
    case TargetOpcode::G_INTRINSIC_CONVERGENT_W_SIDE_EFFECTS:
      return true;
    default:
      return false;
    }
  }
};

// Represents a (non-sequential) vector reduction operation.
class GVecReduce : public GenericMachineInstr {
public:
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_VECREDUCE_FADD:
    case TargetOpcode::G_VECREDUCE_FMUL:
    case TargetOpcode::G_VECREDUCE_FMAX:
    case TargetOpcode::G_VECREDUCE_FMIN:
    case TargetOpcode::G_VECREDUCE_FMAXIMUM:
    case TargetOpcode::G_VECREDUCE_FMINIMUM:
    case TargetOpcode::G_VECREDUCE_ADD:
    case TargetOpcode::G_VECREDUCE_MUL:
    case TargetOpcode::G_VECREDUCE_AND:
    case TargetOpcode::G_VECREDUCE_OR:
    case TargetOpcode::G_VECREDUCE_XOR:
    case TargetOpcode::G_VECREDUCE_SMAX:
    case TargetOpcode::G_VECREDUCE_SMIN:
    case TargetOpcode::G_VECREDUCE_UMAX:
    case TargetOpcode::G_VECREDUCE_UMIN:
      return true;
    default:
      return false;
    }
  }

  /// Get the opcode for the equivalent scalar operation for this reduction.
  /// E.g. for G_VECREDUCE_FADD, this returns G_FADD.
  unsigned getScalarOpcForReduction() {
    unsigned ScalarOpc;
    switch (getOpcode()) {
    case TargetOpcode::G_VECREDUCE_FADD:
      ScalarOpc = TargetOpcode::G_FADD;
      break;
    case TargetOpcode::G_VECREDUCE_FMUL:
      ScalarOpc = TargetOpcode::G_FMUL;
      break;
    case TargetOpcode::G_VECREDUCE_FMAX:
      ScalarOpc = TargetOpcode::G_FMAXNUM;
      break;
    case TargetOpcode::G_VECREDUCE_FMIN:
      ScalarOpc = TargetOpcode::G_FMINNUM;
      break;
    case TargetOpcode::G_VECREDUCE_FMAXIMUM:
      ScalarOpc = TargetOpcode::G_FMAXIMUM;
      break;
    case TargetOpcode::G_VECREDUCE_FMINIMUM:
      ScalarOpc = TargetOpcode::G_FMINIMUM;
      break;
    case TargetOpcode::G_VECREDUCE_ADD:
      ScalarOpc = TargetOpcode::G_ADD;
      break;
    case TargetOpcode::G_VECREDUCE_MUL:
      ScalarOpc = TargetOpcode::G_MUL;
      break;
    case TargetOpcode::G_VECREDUCE_AND:
      ScalarOpc = TargetOpcode::G_AND;
      break;
    case TargetOpcode::G_VECREDUCE_OR:
      ScalarOpc = TargetOpcode::G_OR;
      break;
    case TargetOpcode::G_VECREDUCE_XOR:
      ScalarOpc = TargetOpcode::G_XOR;
      break;
    case TargetOpcode::G_VECREDUCE_SMAX:
      ScalarOpc = TargetOpcode::G_SMAX;
      break;
    case TargetOpcode::G_VECREDUCE_SMIN:
      ScalarOpc = TargetOpcode::G_SMIN;
      break;
    case TargetOpcode::G_VECREDUCE_UMAX:
      ScalarOpc = TargetOpcode::G_UMAX;
      break;
    case TargetOpcode::G_VECREDUCE_UMIN:
      ScalarOpc = TargetOpcode::G_UMIN;
      break;
    default:
      llvm_unreachable("Unhandled reduction");
    }
    return ScalarOpc;
  }
};

/// Represents a G_PHI.
class GPhi : public GenericMachineInstr {
public:
  /// Returns the number of incoming values.
  unsigned getNumIncomingValues() const { return (getNumOperands() - 1) / 2; }
  /// Returns the I'th incoming vreg.
  Register getIncomingValue(unsigned I) const {
    return getOperand(I * 2 + 1).getReg();
  }
  /// Returns the I'th incoming basic block.
  MachineBasicBlock *getIncomingBlock(unsigned I) const {
    return getOperand(I * 2 + 2).getMBB();
  }

  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_PHI;
  }
};

/// Represents a binary operation, i.e, x = y op z.
class GBinOp : public GenericMachineInstr {
public:
  Register getLHSReg() const { return getReg(1); }
  Register getRHSReg() const { return getReg(2); }

  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    // Integer.
    case TargetOpcode::G_ADD:
    case TargetOpcode::G_SUB:
    case TargetOpcode::G_MUL:
    case TargetOpcode::G_SDIV:
    case TargetOpcode::G_UDIV:
    case TargetOpcode::G_SREM:
    case TargetOpcode::G_UREM:
    case TargetOpcode::G_SMIN:
    case TargetOpcode::G_SMAX:
    case TargetOpcode::G_UMIN:
    case TargetOpcode::G_UMAX:
    // Floating point.
    case TargetOpcode::G_FMINNUM:
    case TargetOpcode::G_FMAXNUM:
    case TargetOpcode::G_FMINNUM_IEEE:
    case TargetOpcode::G_FMAXNUM_IEEE:
    case TargetOpcode::G_FMINIMUM:
    case TargetOpcode::G_FMAXIMUM:
    case TargetOpcode::G_FADD:
    case TargetOpcode::G_FSUB:
    case TargetOpcode::G_FMUL:
    case TargetOpcode::G_FDIV:
    case TargetOpcode::G_FPOW:
    // Logical.
    case TargetOpcode::G_AND:
    case TargetOpcode::G_OR:
    case TargetOpcode::G_XOR:
      return true;
    default:
      return false;
    }
  };
};

/// Represents an integer binary operation.
class GIntBinOp : public GBinOp {
public:
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_ADD:
    case TargetOpcode::G_SUB:
    case TargetOpcode::G_MUL:
    case TargetOpcode::G_SDIV:
    case TargetOpcode::G_UDIV:
    case TargetOpcode::G_SREM:
    case TargetOpcode::G_UREM:
    case TargetOpcode::G_SMIN:
    case TargetOpcode::G_SMAX:
    case TargetOpcode::G_UMIN:
    case TargetOpcode::G_UMAX:
      return true;
    default:
      return false;
    }
  };
};

/// Represents a floating point binary operation.
class GFBinOp : public GBinOp {
public:
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_FMINNUM:
    case TargetOpcode::G_FMAXNUM:
    case TargetOpcode::G_FMINNUM_IEEE:
    case TargetOpcode::G_FMAXNUM_IEEE:
    case TargetOpcode::G_FMINIMUM:
    case TargetOpcode::G_FMAXIMUM:
    case TargetOpcode::G_FADD:
    case TargetOpcode::G_FSUB:
    case TargetOpcode::G_FMUL:
    case TargetOpcode::G_FDIV:
    case TargetOpcode::G_FPOW:
      return true;
    default:
      return false;
    }
  };
};

/// Represents a logical binary operation.
class GLogicalBinOp : public GBinOp {
public:
  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_AND:
    case TargetOpcode::G_OR:
    case TargetOpcode::G_XOR:
      return true;
    default:
      return false;
    }
  };
};

/// Represents an integer addition.
class GAdd : public GIntBinOp {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_ADD;
  };
};

/// Represents a logical and.
class GAnd : public GLogicalBinOp {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_AND;
  };
};

/// Represents a logical or.
class GOr : public GLogicalBinOp {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_OR;
  };
};

/// Represents an extract vector element.
class GExtractVectorElement : public GenericMachineInstr {
public:
  Register getVectorReg() const { return getOperand(1).getReg(); }
  Register getIndexReg() const { return getOperand(2).getReg(); }

  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_EXTRACT_VECTOR_ELT;
  }
};

/// Represents an insert vector element.
class GInsertVectorElement : public GenericMachineInstr {
public:
  Register getVectorReg() const { return getOperand(1).getReg(); }
  Register getElementReg() const { return getOperand(2).getReg(); }
  Register getIndexReg() const { return getOperand(3).getReg(); }

  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_INSERT_VECTOR_ELT;
  }
};

/// Represents a freeze.
class GFreeze : public GenericMachineInstr {
public:
  Register getSourceReg() const { return getOperand(1).getReg(); }

  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_FREEZE;
  }
};

/// Represents a cast operation.
/// It models the llvm::CastInst concept.
/// The exception is bitcast.
class GCastOp : public GenericMachineInstr {
public:
  Register getSrcReg() const { return getOperand(1).getReg(); }

  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_ADDRSPACE_CAST:
    case TargetOpcode::G_FPEXT:
    case TargetOpcode::G_FPTOSI:
    case TargetOpcode::G_FPTOUI:
    case TargetOpcode::G_FPTRUNC:
    case TargetOpcode::G_INTTOPTR:
    case TargetOpcode::G_PTRTOINT:
    case TargetOpcode::G_SEXT:
    case TargetOpcode::G_SITOFP:
    case TargetOpcode::G_TRUNC:
    case TargetOpcode::G_UITOFP:
    case TargetOpcode::G_ZEXT:
    case TargetOpcode::G_ANYEXT:
      return true;
    default:
      return false;
    }
  };
};

/// Represents a sext.
class GSext : public GCastOp {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_SEXT;
  };
};

/// Represents a zext.
class GZext : public GCastOp {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_ZEXT;
  };
};

/// Represents a trunc.
class GTrunc : public GCastOp {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_TRUNC;
  };
};

/// Represents a vscale.
class GVScale : public GenericMachineInstr {
public:
  APInt getSrc() const { return getOperand(1).getCImm()->getValue(); }

  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_VSCALE;
  };
};

/// Represents an integer subtraction.
class GSub : public GIntBinOp {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_SUB;
  };
};

/// Represents an integer multiplication.
class GMul : public GIntBinOp {
public:
  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_MUL;
  };
};

/// Represents a shift left.
class GShl : public GenericMachineInstr {
public:
  Register getSrcReg() const { return getOperand(1).getReg(); }
  Register getShiftReg() const { return getOperand(2).getReg(); }

  static bool classof(const MachineInstr *MI) {
    return MI->getOpcode() == TargetOpcode::G_SHL;
  };
};

/// Represents a threeway compare.
class GSUCmp : public GenericMachineInstr {
public:
  Register getLHSReg() const { return getOperand(1).getReg(); }
  Register getRHSReg() const { return getOperand(2).getReg(); }

  bool isSigned() const { return getOpcode() == TargetOpcode::G_SCMP; }

  static bool classof(const MachineInstr *MI) {
    switch (MI->getOpcode()) {
    case TargetOpcode::G_SCMP:
    case TargetOpcode::G_UCMP:
      return true;
    default:
      return false;
    }
  };
};

} // namespace llvm

#endif // LLVM_CODEGEN_GLOBALISEL_GENERICMACHINEINSTRS_H
