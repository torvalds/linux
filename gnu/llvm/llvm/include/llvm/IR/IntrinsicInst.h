//===-- llvm/IntrinsicInst.h - Intrinsic Instruction Wrappers ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines classes that make it really easy to deal with intrinsic
// functions with the isa/dyncast family of functions.  In particular, this
// allows you to do things like:
//
//     if (MemCpyInst *MCI = dyn_cast<MemCpyInst>(Inst))
//        ... MCI->getDest() ... MCI->getSource() ...
//
// All intrinsic function calls are instances of the call instruction, so these
// are all subclasses of the CallInst class.  Note that none of these classes
// has state or virtual methods, which is an important part of this gross/neat
// hack working.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_INTRINSICINST_H
#define LLVM_IR_INTRINSICINST_H

#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/FPEnv.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <cstdint>
#include <optional>

namespace llvm {

class Metadata;

/// A wrapper class for inspecting calls to intrinsic functions.
/// This allows the standard isa/dyncast/cast functionality to work with calls
/// to intrinsic functions.
class IntrinsicInst : public CallInst {
public:
  IntrinsicInst() = delete;
  IntrinsicInst(const IntrinsicInst &) = delete;
  IntrinsicInst &operator=(const IntrinsicInst &) = delete;

  /// Return the intrinsic ID of this intrinsic.
  Intrinsic::ID getIntrinsicID() const {
    return getCalledFunction()->getIntrinsicID();
  }

  bool isAssociative() const {
    switch (getIntrinsicID()) {
    case Intrinsic::smax:
    case Intrinsic::smin:
    case Intrinsic::umax:
    case Intrinsic::umin:
      return true;
    default:
      return false;
    }
  }

  /// Return true if swapping the first two arguments to the intrinsic produces
  /// the same result.
  bool isCommutative() const {
    switch (getIntrinsicID()) {
    case Intrinsic::maxnum:
    case Intrinsic::minnum:
    case Intrinsic::maximum:
    case Intrinsic::minimum:
    case Intrinsic::smax:
    case Intrinsic::smin:
    case Intrinsic::umax:
    case Intrinsic::umin:
    case Intrinsic::sadd_sat:
    case Intrinsic::uadd_sat:
    case Intrinsic::sadd_with_overflow:
    case Intrinsic::uadd_with_overflow:
    case Intrinsic::smul_with_overflow:
    case Intrinsic::umul_with_overflow:
    case Intrinsic::smul_fix:
    case Intrinsic::umul_fix:
    case Intrinsic::smul_fix_sat:
    case Intrinsic::umul_fix_sat:
    case Intrinsic::fma:
    case Intrinsic::fmuladd:
      return true;
    default:
      return false;
    }
  }

  /// Checks if the intrinsic is an annotation.
  bool isAssumeLikeIntrinsic() const {
    switch (getIntrinsicID()) {
    default: break;
    case Intrinsic::assume:
    case Intrinsic::sideeffect:
    case Intrinsic::pseudoprobe:
    case Intrinsic::dbg_assign:
    case Intrinsic::dbg_declare:
    case Intrinsic::dbg_value:
    case Intrinsic::dbg_label:
    case Intrinsic::invariant_start:
    case Intrinsic::invariant_end:
    case Intrinsic::lifetime_start:
    case Intrinsic::lifetime_end:
    case Intrinsic::experimental_noalias_scope_decl:
    case Intrinsic::objectsize:
    case Intrinsic::ptr_annotation:
    case Intrinsic::var_annotation:
      return true;
    }
    return false;
  }

  /// Check if the intrinsic might lower into a regular function call in the
  /// course of IR transformations
  static bool mayLowerToFunctionCall(Intrinsic::ID IID);

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const CallInst *I) {
    if (const Function *CF = I->getCalledFunction())
      return CF->isIntrinsic();
    return false;
  }
  static bool classof(const Value *V) {
    return isa<CallInst>(V) && classof(cast<CallInst>(V));
  }
};

/// Check if \p ID corresponds to a lifetime intrinsic.
static inline bool isLifetimeIntrinsic(Intrinsic::ID ID) {
  switch (ID) {
  case Intrinsic::lifetime_start:
  case Intrinsic::lifetime_end:
    return true;
  default:
    return false;
  }
}

/// This is the common base class for lifetime intrinsics.
class LifetimeIntrinsic : public IntrinsicInst {
public:
  /// \name Casting methods
  /// @{
  static bool classof(const IntrinsicInst *I) {
    return isLifetimeIntrinsic(I->getIntrinsicID());
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// @}
};

/// Check if \p ID corresponds to a debug info intrinsic.
static inline bool isDbgInfoIntrinsic(Intrinsic::ID ID) {
  switch (ID) {
  case Intrinsic::dbg_declare:
  case Intrinsic::dbg_value:
  case Intrinsic::dbg_label:
  case Intrinsic::dbg_assign:
    return true;
  default:
    return false;
  }
}

/// This is the common base class for debug info intrinsics.
class DbgInfoIntrinsic : public IntrinsicInst {
public:
  /// \name Casting methods
  /// @{
  static bool classof(const IntrinsicInst *I) {
    return isDbgInfoIntrinsic(I->getIntrinsicID());
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// @}
};

// Iterator for ValueAsMetadata that internally uses direct pointer iteration
// over either a ValueAsMetadata* or a ValueAsMetadata**, dereferencing to the
// ValueAsMetadata .
class location_op_iterator
    : public iterator_facade_base<location_op_iterator,
                                  std::bidirectional_iterator_tag, Value *> {
  PointerUnion<ValueAsMetadata *, ValueAsMetadata **> I;

public:
  location_op_iterator(ValueAsMetadata *SingleIter) : I(SingleIter) {}
  location_op_iterator(ValueAsMetadata **MultiIter) : I(MultiIter) {}

  location_op_iterator(const location_op_iterator &R) : I(R.I) {}
  location_op_iterator &operator=(const location_op_iterator &R) {
    I = R.I;
    return *this;
  }
  bool operator==(const location_op_iterator &RHS) const { return I == RHS.I; }
  const Value *operator*() const {
    ValueAsMetadata *VAM = isa<ValueAsMetadata *>(I)
                               ? cast<ValueAsMetadata *>(I)
                               : *cast<ValueAsMetadata **>(I);
    return VAM->getValue();
  };
  Value *operator*() {
    ValueAsMetadata *VAM = isa<ValueAsMetadata *>(I)
                               ? cast<ValueAsMetadata *>(I)
                               : *cast<ValueAsMetadata **>(I);
    return VAM->getValue();
  }
  location_op_iterator &operator++() {
    if (isa<ValueAsMetadata *>(I))
      I = cast<ValueAsMetadata *>(I) + 1;
    else
      I = cast<ValueAsMetadata **>(I) + 1;
    return *this;
  }
  location_op_iterator &operator--() {
    if (isa<ValueAsMetadata *>(I))
      I = cast<ValueAsMetadata *>(I) - 1;
    else
      I = cast<ValueAsMetadata **>(I) - 1;
    return *this;
  }
};

/// Lightweight class that wraps the location operand metadata of a debug
/// intrinsic. The raw location may be a ValueAsMetadata, an empty MDTuple,
/// or a DIArgList.
class RawLocationWrapper {
  Metadata *RawLocation = nullptr;

public:
  RawLocationWrapper() = default;
  explicit RawLocationWrapper(Metadata *RawLocation)
      : RawLocation(RawLocation) {
    // Allow ValueAsMetadata, empty MDTuple, DIArgList.
    assert(RawLocation && "unexpected null RawLocation");
    assert(isa<ValueAsMetadata>(RawLocation) || isa<DIArgList>(RawLocation) ||
           (isa<MDNode>(RawLocation) &&
            !cast<MDNode>(RawLocation)->getNumOperands()));
  }
  Metadata *getRawLocation() const { return RawLocation; }
  /// Get the locations corresponding to the variable referenced by the debug
  /// info intrinsic.  Depending on the intrinsic, this could be the
  /// variable's value or its address.
  iterator_range<location_op_iterator> location_ops() const;
  Value *getVariableLocationOp(unsigned OpIdx) const;
  unsigned getNumVariableLocationOps() const {
    if (hasArgList())
      return cast<DIArgList>(getRawLocation())->getArgs().size();
    return 1;
  }
  bool hasArgList() const { return isa<DIArgList>(getRawLocation()); }
  bool isKillLocation(const DIExpression *Expression) const {
    // Check for "kill" sentinel values.
    // Non-variadic: empty metadata.
    if (!hasArgList() && isa<MDNode>(getRawLocation()))
      return true;
    // Variadic: empty DIArgList with empty expression.
    if (getNumVariableLocationOps() == 0 && !Expression->isComplex())
      return true;
    // Variadic and non-variadic: Interpret expressions using undef or poison
    // values as kills.
    return any_of(location_ops(), [](Value *V) { return isa<UndefValue>(V); });
  }

  friend bool operator==(const RawLocationWrapper &A,
                         const RawLocationWrapper &B) {
    return A.RawLocation == B.RawLocation;
  }
  friend bool operator!=(const RawLocationWrapper &A,
                         const RawLocationWrapper &B) {
    return !(A == B);
  }
  friend bool operator>(const RawLocationWrapper &A,
                        const RawLocationWrapper &B) {
    return A.RawLocation > B.RawLocation;
  }
  friend bool operator>=(const RawLocationWrapper &A,
                         const RawLocationWrapper &B) {
    return A.RawLocation >= B.RawLocation;
  }
  friend bool operator<(const RawLocationWrapper &A,
                        const RawLocationWrapper &B) {
    return A.RawLocation < B.RawLocation;
  }
  friend bool operator<=(const RawLocationWrapper &A,
                         const RawLocationWrapper &B) {
    return A.RawLocation <= B.RawLocation;
  }
};

/// This is the common base class for debug info intrinsics for variables.
class DbgVariableIntrinsic : public DbgInfoIntrinsic {
public:
  /// Get the locations corresponding to the variable referenced by the debug
  /// info intrinsic.  Depending on the intrinsic, this could be the
  /// variable's value or its address.
  iterator_range<location_op_iterator> location_ops() const;

  Value *getVariableLocationOp(unsigned OpIdx) const;

  void replaceVariableLocationOp(Value *OldValue, Value *NewValue,
                                 bool AllowEmpty = false);
  void replaceVariableLocationOp(unsigned OpIdx, Value *NewValue);
  /// Adding a new location operand will always result in this intrinsic using
  /// an ArgList, and must always be accompanied by a new expression that uses
  /// the new operand.
  void addVariableLocationOps(ArrayRef<Value *> NewValues,
                              DIExpression *NewExpr);

  void setVariable(DILocalVariable *NewVar) {
    setArgOperand(1, MetadataAsValue::get(NewVar->getContext(), NewVar));
  }

  void setExpression(DIExpression *NewExpr) {
    setArgOperand(2, MetadataAsValue::get(NewExpr->getContext(), NewExpr));
  }

  unsigned getNumVariableLocationOps() const {
    return getWrappedLocation().getNumVariableLocationOps();
  }

  bool hasArgList() const { return getWrappedLocation().hasArgList(); }

  /// Does this describe the address of a local variable. True for dbg.declare,
  /// but not dbg.value, which describes its value, or dbg.assign, which
  /// describes a combination of the variable's value and address.
  bool isAddressOfVariable() const {
    return getIntrinsicID() == Intrinsic::dbg_declare;
  }

  void setKillLocation() {
    // TODO: When/if we remove duplicate values from DIArgLists, we don't need
    // this set anymore.
    SmallPtrSet<Value *, 4> RemovedValues;
    for (Value *OldValue : location_ops()) {
      if (!RemovedValues.insert(OldValue).second)
        continue;
      Value *Poison = PoisonValue::get(OldValue->getType());
      replaceVariableLocationOp(OldValue, Poison);
    }
  }

  bool isKillLocation() const {
    return getWrappedLocation().isKillLocation(getExpression());
  }

  DILocalVariable *getVariable() const {
    return cast<DILocalVariable>(getRawVariable());
  }

  DIExpression *getExpression() const {
    return cast<DIExpression>(getRawExpression());
  }

  Metadata *getRawLocation() const {
    return cast<MetadataAsValue>(getArgOperand(0))->getMetadata();
  }

  RawLocationWrapper getWrappedLocation() const {
    return RawLocationWrapper(getRawLocation());
  }

  Metadata *getRawVariable() const {
    return cast<MetadataAsValue>(getArgOperand(1))->getMetadata();
  }

  Metadata *getRawExpression() const {
    return cast<MetadataAsValue>(getArgOperand(2))->getMetadata();
  }

  /// Use of this should generally be avoided; instead,
  /// replaceVariableLocationOp and addVariableLocationOps should be used where
  /// possible to avoid creating invalid state.
  void setRawLocation(Metadata *Location) {
    return setArgOperand(0, MetadataAsValue::get(getContext(), Location));
  }

  /// Get the size (in bits) of the variable, or fragment of the variable that
  /// is described.
  std::optional<uint64_t> getFragmentSizeInBits() const;

  /// Get the FragmentInfo for the variable.
  std::optional<DIExpression::FragmentInfo> getFragment() const {
    return getExpression()->getFragmentInfo();
  }

  /// Get the FragmentInfo for the variable if it exists, otherwise return a
  /// FragmentInfo that covers the entire variable if the variable size is
  /// known, otherwise return a zero-sized fragment.
  DIExpression::FragmentInfo getFragmentOrEntireVariable() const {
    DIExpression::FragmentInfo VariableSlice(0, 0);
    // Get the fragment or variable size, or zero.
    if (auto Sz = getFragmentSizeInBits())
      VariableSlice.SizeInBits = *Sz;
    if (auto Frag = getExpression()->getFragmentInfo())
      VariableSlice.OffsetInBits = Frag->OffsetInBits;
    return VariableSlice;
  }

  /// \name Casting methods
  /// @{
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::dbg_declare:
    case Intrinsic::dbg_value:
    case Intrinsic::dbg_assign:
      return true;
    default:
      return false;
    }
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// @}
protected:
  void setArgOperand(unsigned i, Value *v) {
    DbgInfoIntrinsic::setArgOperand(i, v);
  }
  void setOperand(unsigned i, Value *v) { DbgInfoIntrinsic::setOperand(i, v); }
};

/// This represents the llvm.dbg.declare instruction.
class DbgDeclareInst : public DbgVariableIntrinsic {
public:
  Value *getAddress() const {
    assert(getNumVariableLocationOps() == 1 &&
           "dbg.declare must have exactly 1 location operand.");
    return getVariableLocationOp(0);
  }

  /// \name Casting methods
  /// @{
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::dbg_declare;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// @}
};

/// This represents the llvm.dbg.value instruction.
class DbgValueInst : public DbgVariableIntrinsic {
public:
  // The default argument should only be used in ISel, and the default option
  // should be removed once ISel support for multiple location ops is complete.
  Value *getValue(unsigned OpIdx = 0) const {
    return getVariableLocationOp(OpIdx);
  }
  iterator_range<location_op_iterator> getValues() const {
    return location_ops();
  }

  /// \name Casting methods
  /// @{
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::dbg_value ||
           I->getIntrinsicID() == Intrinsic::dbg_assign;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// @}
};

/// This represents the llvm.dbg.assign instruction.
class DbgAssignIntrinsic : public DbgValueInst {
  enum Operands {
    OpValue,
    OpVar,
    OpExpr,
    OpAssignID,
    OpAddress,
    OpAddressExpr,
  };

public:
  Value *getAddress() const;
  Metadata *getRawAddress() const {
    return cast<MetadataAsValue>(getArgOperand(OpAddress))->getMetadata();
  }
  Metadata *getRawAssignID() const {
    return cast<MetadataAsValue>(getArgOperand(OpAssignID))->getMetadata();
  }
  DIAssignID *getAssignID() const { return cast<DIAssignID>(getRawAssignID()); }
  Metadata *getRawAddressExpression() const {
    return cast<MetadataAsValue>(getArgOperand(OpAddressExpr))->getMetadata();
  }
  DIExpression *getAddressExpression() const {
    return cast<DIExpression>(getRawAddressExpression());
  }
  void setAddressExpression(DIExpression *NewExpr) {
    setArgOperand(OpAddressExpr,
                  MetadataAsValue::get(NewExpr->getContext(), NewExpr));
  }
  void setAssignId(DIAssignID *New);
  void setAddress(Value *V);
  /// Kill the address component.
  void setKillAddress();
  /// Check whether this kills the address component. This doesn't take into
  /// account the position of the intrinsic, therefore a returned value of false
  /// does not guarentee the address is a valid location for the variable at the
  /// intrinsic's position in IR.
  bool isKillAddress() const;
  void setValue(Value *V);
  /// \name Casting methods
  /// @{
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::dbg_assign;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// @}
};

/// This represents the llvm.dbg.label instruction.
class DbgLabelInst : public DbgInfoIntrinsic {
public:
  DILabel *getLabel() const { return cast<DILabel>(getRawLabel()); }
  void setLabel(DILabel *NewLabel) {
    setArgOperand(0, MetadataAsValue::get(getContext(), NewLabel));
  }

  Metadata *getRawLabel() const {
    return cast<MetadataAsValue>(getArgOperand(0))->getMetadata();
  }

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// @{
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::dbg_label;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// @}
};

/// This is the common base class for vector predication intrinsics.
class VPIntrinsic : public IntrinsicInst {
public:
  /// \brief Declares a llvm.vp.* intrinsic in \p M that matches the parameters
  /// \p Params. Additionally, the load and gather intrinsics require
  /// \p ReturnType to be specified.
  static Function *getDeclarationForParams(Module *M, Intrinsic::ID,
                                           Type *ReturnType,
                                           ArrayRef<Value *> Params);

  static std::optional<unsigned> getMaskParamPos(Intrinsic::ID IntrinsicID);
  static std::optional<unsigned> getVectorLengthParamPos(
      Intrinsic::ID IntrinsicID);

  /// The llvm.vp.* intrinsics for this instruction Opcode
  static Intrinsic::ID getForOpcode(unsigned OC);

  /// The llvm.vp.* intrinsics for this intrinsic ID \p Id. Return \p Id if it
  /// is already a VP intrinsic.
  static Intrinsic::ID getForIntrinsic(Intrinsic::ID Id);

  // Whether \p ID is a VP intrinsic ID.
  static bool isVPIntrinsic(Intrinsic::ID);

  /// \return The mask parameter or nullptr.
  Value *getMaskParam() const;
  void setMaskParam(Value *);

  /// \return The vector length parameter or nullptr.
  Value *getVectorLengthParam() const;
  void setVectorLengthParam(Value *);

  /// \return Whether the vector length param can be ignored.
  bool canIgnoreVectorLengthParam() const;

  /// \return The static element count (vector number of elements) the vector
  /// length parameter applies to.
  ElementCount getStaticVectorLength() const;

  /// \return The alignment of the pointer used by this load/store/gather or
  /// scatter.
  MaybeAlign getPointerAlignment() const;
  // MaybeAlign setPointerAlignment(Align NewAlign); // TODO

  /// \return The pointer operand of this load,store, gather or scatter.
  Value *getMemoryPointerParam() const;
  static std::optional<unsigned> getMemoryPointerParamPos(Intrinsic::ID);

  /// \return The data (payload) operand of this store or scatter.
  Value *getMemoryDataParam() const;
  static std::optional<unsigned> getMemoryDataParamPos(Intrinsic::ID);

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I) {
    return isVPIntrinsic(I->getIntrinsicID());
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  // Equivalent non-predicated opcode
  std::optional<unsigned> getFunctionalOpcode() const {
    return getFunctionalOpcodeForVP(getIntrinsicID());
  }

  // Equivalent non-predicated intrinsic ID
  std::optional<unsigned> getFunctionalIntrinsicID() const {
    return getFunctionalIntrinsicIDForVP(getIntrinsicID());
  }

  // Equivalent non-predicated constrained ID
  std::optional<unsigned> getConstrainedIntrinsicID() const {
    return getConstrainedIntrinsicIDForVP(getIntrinsicID());
  }

  // Equivalent non-predicated opcode
  static std::optional<unsigned> getFunctionalOpcodeForVP(Intrinsic::ID ID);

  // Equivalent non-predicated intrinsic ID
  static std::optional<Intrinsic::ID>
  getFunctionalIntrinsicIDForVP(Intrinsic::ID ID);

  // Equivalent non-predicated constrained ID
  static std::optional<Intrinsic::ID>
  getConstrainedIntrinsicIDForVP(Intrinsic::ID ID);
};

/// This represents vector predication reduction intrinsics.
class VPReductionIntrinsic : public VPIntrinsic {
public:
  static bool isVPReduction(Intrinsic::ID ID);

  unsigned getStartParamPos() const;
  unsigned getVectorParamPos() const;

  static std::optional<unsigned> getStartParamPos(Intrinsic::ID ID);
  static std::optional<unsigned> getVectorParamPos(Intrinsic::ID ID);

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// @{
  static bool classof(const IntrinsicInst *I) {
    return VPReductionIntrinsic::isVPReduction(I->getIntrinsicID());
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// @}
};

class VPCastIntrinsic : public VPIntrinsic {
public:
  static bool isVPCast(Intrinsic::ID ID);

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// @{
  static bool classof(const IntrinsicInst *I) {
    return VPCastIntrinsic::isVPCast(I->getIntrinsicID());
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// @}
};

class VPCmpIntrinsic : public VPIntrinsic {
public:
  static bool isVPCmp(Intrinsic::ID ID);

  CmpInst::Predicate getPredicate() const;

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// @{
  static bool classof(const IntrinsicInst *I) {
    return VPCmpIntrinsic::isVPCmp(I->getIntrinsicID());
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// @}
};

class VPBinOpIntrinsic : public VPIntrinsic {
public:
  static bool isVPBinOp(Intrinsic::ID ID);

  /// Methods for support type inquiry through isa, cast, and dyn_cast:
  /// @{
  static bool classof(const IntrinsicInst *I) {
    return VPBinOpIntrinsic::isVPBinOp(I->getIntrinsicID());
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  /// @}
};


/// This is the common base class for constrained floating point intrinsics.
class ConstrainedFPIntrinsic : public IntrinsicInst {
public:
  unsigned getNonMetadataArgCount() const;
  std::optional<RoundingMode> getRoundingMode() const;
  std::optional<fp::ExceptionBehavior> getExceptionBehavior() const;
  bool isDefaultFPEnvironment() const;

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I);
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// Constrained floating point compare intrinsics.
class ConstrainedFPCmpIntrinsic : public ConstrainedFPIntrinsic {
public:
  FCmpInst::Predicate getPredicate() const;
  bool isSignaling() const {
    return getIntrinsicID() == Intrinsic::experimental_constrained_fcmps;
  }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::experimental_constrained_fcmp:
    case Intrinsic::experimental_constrained_fcmps:
      return true;
    default:
      return false;
    }
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class represents min/max intrinsics.
class MinMaxIntrinsic : public IntrinsicInst {
public:
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::umin:
    case Intrinsic::umax:
    case Intrinsic::smin:
    case Intrinsic::smax:
      return true;
    default:
      return false;
    }
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  Value *getLHS() const { return const_cast<Value *>(getArgOperand(0)); }
  Value *getRHS() const { return const_cast<Value *>(getArgOperand(1)); }

  /// Returns the comparison predicate underlying the intrinsic.
  static ICmpInst::Predicate getPredicate(Intrinsic::ID ID) {
    switch (ID) {
    case Intrinsic::umin:
      return ICmpInst::Predicate::ICMP_ULT;
    case Intrinsic::umax:
      return ICmpInst::Predicate::ICMP_UGT;
    case Intrinsic::smin:
      return ICmpInst::Predicate::ICMP_SLT;
    case Intrinsic::smax:
      return ICmpInst::Predicate::ICMP_SGT;
    default:
      llvm_unreachable("Invalid intrinsic");
    }
  }

  /// Returns the comparison predicate underlying the intrinsic.
  ICmpInst::Predicate getPredicate() const {
    return getPredicate(getIntrinsicID());
  }

  /// Whether the intrinsic is signed or unsigned.
  static bool isSigned(Intrinsic::ID ID) {
    return ICmpInst::isSigned(getPredicate(ID));
  };

  /// Whether the intrinsic is signed or unsigned.
  bool isSigned() const { return isSigned(getIntrinsicID()); };

  /// Min/max intrinsics are monotonic, they operate on a fixed-bitwidth values,
  /// so there is a certain threshold value, upon reaching which,
  /// their value can no longer change. Return said threshold.
  static APInt getSaturationPoint(Intrinsic::ID ID, unsigned numBits) {
    switch (ID) {
    case Intrinsic::umin:
      return APInt::getMinValue(numBits);
    case Intrinsic::umax:
      return APInt::getMaxValue(numBits);
    case Intrinsic::smin:
      return APInt::getSignedMinValue(numBits);
    case Intrinsic::smax:
      return APInt::getSignedMaxValue(numBits);
    default:
      llvm_unreachable("Invalid intrinsic");
    }
  }

  /// Min/max intrinsics are monotonic, they operate on a fixed-bitwidth values,
  /// so there is a certain threshold value, upon reaching which,
  /// their value can no longer change. Return said threshold.
  APInt getSaturationPoint(unsigned numBits) const {
    return getSaturationPoint(getIntrinsicID(), numBits);
  }

  /// Min/max intrinsics are monotonic, they operate on a fixed-bitwidth values,
  /// so there is a certain threshold value, upon reaching which,
  /// their value can no longer change. Return said threshold.
  static Constant *getSaturationPoint(Intrinsic::ID ID, Type *Ty) {
    return Constant::getIntegerValue(
        Ty, getSaturationPoint(ID, Ty->getScalarSizeInBits()));
  }

  /// Min/max intrinsics are monotonic, they operate on a fixed-bitwidth values,
  /// so there is a certain threshold value, upon reaching which,
  /// their value can no longer change. Return said threshold.
  Constant *getSaturationPoint(Type *Ty) const {
    return getSaturationPoint(getIntrinsicID(), Ty);
  }
};

/// This class represents a ucmp/scmp intrinsic
class CmpIntrinsic : public IntrinsicInst {
public:
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::scmp:
    case Intrinsic::ucmp:
      return true;
    default:
      return false;
    }
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  Value *getLHS() const { return const_cast<Value *>(getArgOperand(0)); }
  Value *getRHS() const { return const_cast<Value *>(getArgOperand(1)); }

  static bool isSigned(Intrinsic::ID ID) { return ID == Intrinsic::scmp; }
  bool isSigned() const { return isSigned(getIntrinsicID()); }

  static CmpInst::Predicate getGTPredicate(Intrinsic::ID ID) {
    return isSigned(ID) ? ICmpInst::ICMP_SGT : ICmpInst::ICMP_UGT;
  }
  CmpInst::Predicate getGTPredicate() const {
    return getGTPredicate(getIntrinsicID());
  }

  static CmpInst::Predicate getLTPredicate(Intrinsic::ID ID) {
    return isSigned(ID) ? ICmpInst::ICMP_SLT : ICmpInst::ICMP_ULT;
  }
  CmpInst::Predicate getLTPredicate() const {
    return getLTPredicate(getIntrinsicID());
  }
};

/// This class represents an intrinsic that is based on a binary operation.
/// This includes op.with.overflow and saturating add/sub intrinsics.
class BinaryOpIntrinsic : public IntrinsicInst {
public:
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::uadd_with_overflow:
    case Intrinsic::sadd_with_overflow:
    case Intrinsic::usub_with_overflow:
    case Intrinsic::ssub_with_overflow:
    case Intrinsic::umul_with_overflow:
    case Intrinsic::smul_with_overflow:
    case Intrinsic::uadd_sat:
    case Intrinsic::sadd_sat:
    case Intrinsic::usub_sat:
    case Intrinsic::ssub_sat:
      return true;
    default:
      return false;
    }
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  Value *getLHS() const { return const_cast<Value *>(getArgOperand(0)); }
  Value *getRHS() const { return const_cast<Value *>(getArgOperand(1)); }

  /// Returns the binary operation underlying the intrinsic.
  Instruction::BinaryOps getBinaryOp() const;

  /// Whether the intrinsic is signed or unsigned.
  bool isSigned() const;

  /// Returns one of OBO::NoSignedWrap or OBO::NoUnsignedWrap.
  unsigned getNoWrapKind() const;
};

/// Represents an op.with.overflow intrinsic.
class WithOverflowInst : public BinaryOpIntrinsic {
public:
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::uadd_with_overflow:
    case Intrinsic::sadd_with_overflow:
    case Intrinsic::usub_with_overflow:
    case Intrinsic::ssub_with_overflow:
    case Intrinsic::umul_with_overflow:
    case Intrinsic::smul_with_overflow:
      return true;
    default:
      return false;
    }
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// Represents a saturating add/sub intrinsic.
class SaturatingInst : public BinaryOpIntrinsic {
public:
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::uadd_sat:
    case Intrinsic::sadd_sat:
    case Intrinsic::usub_sat:
    case Intrinsic::ssub_sat:
      return true;
    default:
      return false;
    }
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// Common base class for all memory intrinsics. Simply provides
/// common methods.
/// Written as CRTP to avoid a common base class amongst the
/// three atomicity hierarchies.
template <typename Derived> class MemIntrinsicBase : public IntrinsicInst {
private:
  enum { ARG_DEST = 0, ARG_LENGTH = 2 };

public:
  Value *getRawDest() const {
    return const_cast<Value *>(getArgOperand(ARG_DEST));
  }
  const Use &getRawDestUse() const { return getArgOperandUse(ARG_DEST); }
  Use &getRawDestUse() { return getArgOperandUse(ARG_DEST); }

  Value *getLength() const {
    return const_cast<Value *>(getArgOperand(ARG_LENGTH));
  }
  const Use &getLengthUse() const { return getArgOperandUse(ARG_LENGTH); }
  Use &getLengthUse() { return getArgOperandUse(ARG_LENGTH); }

  /// This is just like getRawDest, but it strips off any cast
  /// instructions (including addrspacecast) that feed it, giving the
  /// original input.  The returned value is guaranteed to be a pointer.
  Value *getDest() const { return getRawDest()->stripPointerCasts(); }

  unsigned getDestAddressSpace() const {
    return cast<PointerType>(getRawDest()->getType())->getAddressSpace();
  }

  /// FIXME: Remove this function once transition to Align is over.
  /// Use getDestAlign() instead.
  LLVM_DEPRECATED("Use getDestAlign() instead", "getDestAlign")
  unsigned getDestAlignment() const {
    if (auto MA = getParamAlign(ARG_DEST))
      return MA->value();
    return 0;
  }
  MaybeAlign getDestAlign() const { return getParamAlign(ARG_DEST); }

  /// Set the specified arguments of the instruction.
  void setDest(Value *Ptr) {
    assert(getRawDest()->getType() == Ptr->getType() &&
           "setDest called with pointer of wrong type!");
    setArgOperand(ARG_DEST, Ptr);
  }

  void setDestAlignment(MaybeAlign Alignment) {
    removeParamAttr(ARG_DEST, Attribute::Alignment);
    if (Alignment)
      addParamAttr(ARG_DEST,
                   Attribute::getWithAlignment(getContext(), *Alignment));
  }
  void setDestAlignment(Align Alignment) {
    removeParamAttr(ARG_DEST, Attribute::Alignment);
    addParamAttr(ARG_DEST,
                 Attribute::getWithAlignment(getContext(), Alignment));
  }

  void setLength(Value *L) {
    assert(getLength()->getType() == L->getType() &&
           "setLength called with value of wrong type!");
    setArgOperand(ARG_LENGTH, L);
  }
};

/// Common base class for all memory transfer intrinsics. Simply provides
/// common methods.
template <class BaseCL> class MemTransferBase : public BaseCL {
private:
  enum { ARG_SOURCE = 1 };

public:
  /// Return the arguments to the instruction.
  Value *getRawSource() const {
    return const_cast<Value *>(BaseCL::getArgOperand(ARG_SOURCE));
  }
  const Use &getRawSourceUse() const {
    return BaseCL::getArgOperandUse(ARG_SOURCE);
  }
  Use &getRawSourceUse() { return BaseCL::getArgOperandUse(ARG_SOURCE); }

  /// This is just like getRawSource, but it strips off any cast
  /// instructions that feed it, giving the original input.  The returned
  /// value is guaranteed to be a pointer.
  Value *getSource() const { return getRawSource()->stripPointerCasts(); }

  unsigned getSourceAddressSpace() const {
    return cast<PointerType>(getRawSource()->getType())->getAddressSpace();
  }

  /// FIXME: Remove this function once transition to Align is over.
  /// Use getSourceAlign() instead.
  LLVM_DEPRECATED("Use getSourceAlign() instead", "getSourceAlign")
  unsigned getSourceAlignment() const {
    if (auto MA = BaseCL::getParamAlign(ARG_SOURCE))
      return MA->value();
    return 0;
  }

  MaybeAlign getSourceAlign() const {
    return BaseCL::getParamAlign(ARG_SOURCE);
  }

  void setSource(Value *Ptr) {
    assert(getRawSource()->getType() == Ptr->getType() &&
           "setSource called with pointer of wrong type!");
    BaseCL::setArgOperand(ARG_SOURCE, Ptr);
  }

  void setSourceAlignment(MaybeAlign Alignment) {
    BaseCL::removeParamAttr(ARG_SOURCE, Attribute::Alignment);
    if (Alignment)
      BaseCL::addParamAttr(ARG_SOURCE, Attribute::getWithAlignment(
                                           BaseCL::getContext(), *Alignment));
  }

  void setSourceAlignment(Align Alignment) {
    BaseCL::removeParamAttr(ARG_SOURCE, Attribute::Alignment);
    BaseCL::addParamAttr(ARG_SOURCE, Attribute::getWithAlignment(
                                         BaseCL::getContext(), Alignment));
  }
};

/// Common base class for all memset intrinsics. Simply provides
/// common methods.
template <class BaseCL> class MemSetBase : public BaseCL {
private:
  enum { ARG_VALUE = 1 };

public:
  Value *getValue() const {
    return const_cast<Value *>(BaseCL::getArgOperand(ARG_VALUE));
  }
  const Use &getValueUse() const { return BaseCL::getArgOperandUse(ARG_VALUE); }
  Use &getValueUse() { return BaseCL::getArgOperandUse(ARG_VALUE); }

  void setValue(Value *Val) {
    assert(getValue()->getType() == Val->getType() &&
           "setValue called with value of wrong type!");
    BaseCL::setArgOperand(ARG_VALUE, Val);
  }
};

// The common base class for the atomic memset/memmove/memcpy intrinsics
// i.e. llvm.element.unordered.atomic.memset/memcpy/memmove
class AtomicMemIntrinsic : public MemIntrinsicBase<AtomicMemIntrinsic> {
private:
  enum { ARG_ELEMENTSIZE = 3 };

public:
  Value *getRawElementSizeInBytes() const {
    return const_cast<Value *>(getArgOperand(ARG_ELEMENTSIZE));
  }

  ConstantInt *getElementSizeInBytesCst() const {
    return cast<ConstantInt>(getRawElementSizeInBytes());
  }

  uint32_t getElementSizeInBytes() const {
    return getElementSizeInBytesCst()->getZExtValue();
  }

  void setElementSizeInBytes(Constant *V) {
    assert(V->getType() == Type::getInt8Ty(getContext()) &&
           "setElementSizeInBytes called with value of wrong type!");
    setArgOperand(ARG_ELEMENTSIZE, V);
  }

  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::memcpy_element_unordered_atomic:
    case Intrinsic::memmove_element_unordered_atomic:
    case Intrinsic::memset_element_unordered_atomic:
      return true;
    default:
      return false;
    }
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class represents atomic memset intrinsic
// i.e. llvm.element.unordered.atomic.memset
class AtomicMemSetInst : public MemSetBase<AtomicMemIntrinsic> {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::memset_element_unordered_atomic;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

// This class wraps the atomic memcpy/memmove intrinsics
// i.e. llvm.element.unordered.atomic.memcpy/memmove
class AtomicMemTransferInst : public MemTransferBase<AtomicMemIntrinsic> {
public:
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::memcpy_element_unordered_atomic:
    case Intrinsic::memmove_element_unordered_atomic:
      return true;
    default:
      return false;
    }
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class represents the atomic memcpy intrinsic
/// i.e. llvm.element.unordered.atomic.memcpy
class AtomicMemCpyInst : public AtomicMemTransferInst {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::memcpy_element_unordered_atomic;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class represents the atomic memmove intrinsic
/// i.e. llvm.element.unordered.atomic.memmove
class AtomicMemMoveInst : public AtomicMemTransferInst {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::memmove_element_unordered_atomic;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This is the common base class for memset/memcpy/memmove.
class MemIntrinsic : public MemIntrinsicBase<MemIntrinsic> {
private:
  enum { ARG_VOLATILE = 3 };

public:
  ConstantInt *getVolatileCst() const {
    return cast<ConstantInt>(const_cast<Value *>(getArgOperand(ARG_VOLATILE)));
  }

  bool isVolatile() const { return !getVolatileCst()->isZero(); }

  void setVolatile(Constant *V) { setArgOperand(ARG_VOLATILE, V); }

  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::memcpy:
    case Intrinsic::memmove:
    case Intrinsic::memset:
    case Intrinsic::memset_inline:
    case Intrinsic::memcpy_inline:
      return true;
    default:
      return false;
    }
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class wraps the llvm.memset and llvm.memset.inline intrinsics.
class MemSetInst : public MemSetBase<MemIntrinsic> {
public:
  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::memset:
    case Intrinsic::memset_inline:
      return true;
    default:
      return false;
    }
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class wraps the llvm.memset.inline intrinsic.
class MemSetInlineInst : public MemSetInst {
public:
  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::memset_inline;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class wraps the llvm.memcpy/memmove intrinsics.
class MemTransferInst : public MemTransferBase<MemIntrinsic> {
public:
  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::memcpy:
    case Intrinsic::memmove:
    case Intrinsic::memcpy_inline:
      return true;
    default:
      return false;
    }
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class wraps the llvm.memcpy intrinsic.
class MemCpyInst : public MemTransferInst {
public:
  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::memcpy ||
           I->getIntrinsicID() == Intrinsic::memcpy_inline;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class wraps the llvm.memmove intrinsic.
class MemMoveInst : public MemTransferInst {
public:
  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::memmove;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class wraps the llvm.memcpy.inline intrinsic.
class MemCpyInlineInst : public MemCpyInst {
public:
  // Methods for support type inquiry through isa, cast, and dyn_cast:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::memcpy_inline;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

// The common base class for any memset/memmove/memcpy intrinsics;
// whether they be atomic or non-atomic.
// i.e. llvm.element.unordered.atomic.memset/memcpy/memmove
//  and llvm.memset/memcpy/memmove
class AnyMemIntrinsic : public MemIntrinsicBase<AnyMemIntrinsic> {
public:
  bool isVolatile() const {
    // Only the non-atomic intrinsics can be volatile
    if (auto *MI = dyn_cast<MemIntrinsic>(this))
      return MI->isVolatile();
    return false;
  }

  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::memcpy:
    case Intrinsic::memcpy_inline:
    case Intrinsic::memmove:
    case Intrinsic::memset:
    case Intrinsic::memset_inline:
    case Intrinsic::memcpy_element_unordered_atomic:
    case Intrinsic::memmove_element_unordered_atomic:
    case Intrinsic::memset_element_unordered_atomic:
      return true;
    default:
      return false;
    }
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class represents any memset intrinsic
// i.e. llvm.element.unordered.atomic.memset
// and  llvm.memset
class AnyMemSetInst : public MemSetBase<AnyMemIntrinsic> {
public:
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::memset:
    case Intrinsic::memset_inline:
    case Intrinsic::memset_element_unordered_atomic:
      return true;
    default:
      return false;
    }
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

// This class wraps any memcpy/memmove intrinsics
// i.e. llvm.element.unordered.atomic.memcpy/memmove
// and  llvm.memcpy/memmove
class AnyMemTransferInst : public MemTransferBase<AnyMemIntrinsic> {
public:
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::memcpy:
    case Intrinsic::memcpy_inline:
    case Intrinsic::memmove:
    case Intrinsic::memcpy_element_unordered_atomic:
    case Intrinsic::memmove_element_unordered_atomic:
      return true;
    default:
      return false;
    }
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class represents any memcpy intrinsic
/// i.e. llvm.element.unordered.atomic.memcpy
///  and llvm.memcpy
class AnyMemCpyInst : public AnyMemTransferInst {
public:
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::memcpy:
    case Intrinsic::memcpy_inline:
    case Intrinsic::memcpy_element_unordered_atomic:
      return true;
    default:
      return false;
    }
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This class represents any memmove intrinsic
/// i.e. llvm.element.unordered.atomic.memmove
///  and llvm.memmove
class AnyMemMoveInst : public AnyMemTransferInst {
public:
  static bool classof(const IntrinsicInst *I) {
    switch (I->getIntrinsicID()) {
    case Intrinsic::memmove:
    case Intrinsic::memmove_element_unordered_atomic:
      return true;
    default:
      return false;
    }
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.va_start intrinsic.
class VAStartInst : public IntrinsicInst {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::vastart;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  Value *getArgList() const { return const_cast<Value *>(getArgOperand(0)); }
};

/// This represents the llvm.va_end intrinsic.
class VAEndInst : public IntrinsicInst {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::vaend;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  Value *getArgList() const { return const_cast<Value *>(getArgOperand(0)); }
};

/// This represents the llvm.va_copy intrinsic.
class VACopyInst : public IntrinsicInst {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::vacopy;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  Value *getDest() const { return const_cast<Value *>(getArgOperand(0)); }
  Value *getSrc() const { return const_cast<Value *>(getArgOperand(1)); }
};

/// A base class for all instrprof intrinsics.
class InstrProfInstBase : public IntrinsicInst {
protected:
  static bool isCounterBase(const IntrinsicInst &I) {
    switch (I.getIntrinsicID()) {
    case Intrinsic::instrprof_cover:
    case Intrinsic::instrprof_increment:
    case Intrinsic::instrprof_increment_step:
    case Intrinsic::instrprof_callsite:
    case Intrinsic::instrprof_timestamp:
    case Intrinsic::instrprof_value_profile:
      return true;
    }
    return false;
  }
  static bool isMCDCBitmapBase(const IntrinsicInst &I) {
    switch (I.getIntrinsicID()) {
    case Intrinsic::instrprof_mcdc_parameters:
    case Intrinsic::instrprof_mcdc_tvbitmap_update:
      return true;
    }
    return false;
  }

public:
  static bool classof(const Value *V) {
    if (const auto *Instr = dyn_cast<IntrinsicInst>(V))
      return isCounterBase(*Instr) || isMCDCBitmapBase(*Instr);
    return false;
  }
  // The name of the instrumented function.
  GlobalVariable *getName() const {
    return cast<GlobalVariable>(
        const_cast<Value *>(getArgOperand(0))->stripPointerCasts());
  }
  // The hash of the CFG for the instrumented function.
  ConstantInt *getHash() const {
    return cast<ConstantInt>(const_cast<Value *>(getArgOperand(1)));
  }
};

/// A base class for all instrprof counter intrinsics.
class InstrProfCntrInstBase : public InstrProfInstBase {
public:
  static bool classof(const Value *V) {
    if (const auto *Instr = dyn_cast<IntrinsicInst>(V))
      return InstrProfInstBase::isCounterBase(*Instr);
    return false;
  }

  // The number of counters for the instrumented function.
  ConstantInt *getNumCounters() const;
  // The index of the counter that this instruction acts on.
  ConstantInt *getIndex() const;
};

/// This represents the llvm.instrprof.cover intrinsic.
class InstrProfCoverInst : public InstrProfCntrInstBase {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::instrprof_cover;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.instrprof.increment intrinsic.
class InstrProfIncrementInst : public InstrProfCntrInstBase {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::instrprof_increment ||
           I->getIntrinsicID() == Intrinsic::instrprof_increment_step;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  Value *getStep() const;
};

/// This represents the llvm.instrprof.increment.step intrinsic.
class InstrProfIncrementInstStep : public InstrProfIncrementInst {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::instrprof_increment_step;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.instrprof.callsite intrinsic.
/// It is structurally like the increment or step counters, hence the
/// inheritance relationship, albeit somewhat tenuous (it's not 'counting' per
/// se)
class InstrProfCallsite : public InstrProfCntrInstBase {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::instrprof_callsite;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
  Value *getCallee() const;
};

/// This represents the llvm.instrprof.timestamp intrinsic.
class InstrProfTimestampInst : public InstrProfCntrInstBase {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::instrprof_timestamp;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.instrprof.value.profile intrinsic.
class InstrProfValueProfileInst : public InstrProfCntrInstBase {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::instrprof_value_profile;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  Value *getTargetValue() const {
    return cast<Value>(const_cast<Value *>(getArgOperand(2)));
  }

  ConstantInt *getValueKind() const {
    return cast<ConstantInt>(const_cast<Value *>(getArgOperand(3)));
  }

  // Returns the value site index.
  ConstantInt *getIndex() const {
    return cast<ConstantInt>(const_cast<Value *>(getArgOperand(4)));
  }
};

/// A base class for instrprof mcdc intrinsics that require global bitmap bytes.
class InstrProfMCDCBitmapInstBase : public InstrProfInstBase {
public:
  static bool classof(const IntrinsicInst *I) {
    return InstrProfInstBase::isMCDCBitmapBase(*I);
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// \return The number of bits used for the MCDC bitmaps for the instrumented
  /// function.
  ConstantInt *getNumBitmapBits() const {
    return cast<ConstantInt>(const_cast<Value *>(getArgOperand(2)));
  }

  /// \return The number of bytes used for the MCDC bitmaps for the instrumented
  /// function.
  auto getNumBitmapBytes() const {
    return alignTo(getNumBitmapBits()->getZExtValue(), CHAR_BIT) / CHAR_BIT;
  }
};

/// This represents the llvm.instrprof.mcdc.parameters intrinsic.
class InstrProfMCDCBitmapParameters : public InstrProfMCDCBitmapInstBase {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::instrprof_mcdc_parameters;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// This represents the llvm.instrprof.mcdc.tvbitmap.update intrinsic.
class InstrProfMCDCTVBitmapUpdate : public InstrProfMCDCBitmapInstBase {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::instrprof_mcdc_tvbitmap_update;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// \return The index of the TestVector Bitmap upon which this intrinsic
  /// acts.
  ConstantInt *getBitmapIndex() const {
    return cast<ConstantInt>(const_cast<Value *>(getArgOperand(2)));
  }

  /// \return The address of the corresponding condition bitmap containing
  /// the index of the TestVector to update within the TestVector Bitmap.
  Value *getMCDCCondBitmapAddr() const {
    return cast<Value>(const_cast<Value *>(getArgOperand(3)));
  }
};

class PseudoProbeInst : public IntrinsicInst {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::pseudoprobe;
  }

  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  ConstantInt *getFuncGuid() const {
    return cast<ConstantInt>(const_cast<Value *>(getArgOperand(0)));
  }

  ConstantInt *getIndex() const {
    return cast<ConstantInt>(const_cast<Value *>(getArgOperand(1)));
  }

  ConstantInt *getAttributes() const {
    return cast<ConstantInt>(const_cast<Value *>(getArgOperand(2)));
  }

  ConstantInt *getFactor() const {
    return cast<ConstantInt>(const_cast<Value *>(getArgOperand(3)));
  }
};

class NoAliasScopeDeclInst : public IntrinsicInst {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::experimental_noalias_scope_decl;
  }

  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  MDNode *getScopeList() const {
    auto *MV =
        cast<MetadataAsValue>(getOperand(Intrinsic::NoAliasScopeDeclScopeArg));
    return cast<MDNode>(MV->getMetadata());
  }

  void setScopeList(MDNode *ScopeList) {
    setOperand(Intrinsic::NoAliasScopeDeclScopeArg,
               MetadataAsValue::get(getContext(), ScopeList));
  }
};

/// Common base class for representing values projected from a statepoint.
/// Currently, the only projections available are gc.result and gc.relocate.
class GCProjectionInst : public IntrinsicInst {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::experimental_gc_relocate ||
      I->getIntrinsicID() == Intrinsic::experimental_gc_result;
  }

  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// Return true if this relocate is tied to the invoke statepoint.
  /// This includes relocates which are on the unwinding path.
  bool isTiedToInvoke() const {
    const Value *Token = getArgOperand(0);

    return isa<LandingPadInst>(Token) || isa<InvokeInst>(Token);
  }

  /// The statepoint with which this gc.relocate is associated.
  const Value *getStatepoint() const;
};

/// Represents calls to the gc.relocate intrinsic.
class GCRelocateInst : public GCProjectionInst {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::experimental_gc_relocate;
  }

  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  /// The index into the associate statepoint's argument list
  /// which contains the base pointer of the pointer whose
  /// relocation this gc.relocate describes.
  unsigned getBasePtrIndex() const {
    return cast<ConstantInt>(getArgOperand(1))->getZExtValue();
  }

  /// The index into the associate statepoint's argument list which
  /// contains the pointer whose relocation this gc.relocate describes.
  unsigned getDerivedPtrIndex() const {
    return cast<ConstantInt>(getArgOperand(2))->getZExtValue();
  }

  Value *getBasePtr() const;
  Value *getDerivedPtr() const;
};

/// Represents calls to the gc.result intrinsic.
class GCResultInst : public GCProjectionInst {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::experimental_gc_result;
  }

  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};


/// This represents the llvm.assume intrinsic.
class AssumeInst : public IntrinsicInst {
public:
  static bool classof(const IntrinsicInst *I) {
    return I->getIntrinsicID() == Intrinsic::assume;
  }
  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }
};

/// Check if \p ID corresponds to a convergence control intrinsic.
static inline bool isConvergenceControlIntrinsic(unsigned IntrinsicID) {
  switch (IntrinsicID) {
  default:
    return false;
  case Intrinsic::experimental_convergence_anchor:
  case Intrinsic::experimental_convergence_entry:
  case Intrinsic::experimental_convergence_loop:
    return true;
  }
}

/// Represents calls to the llvm.experimintal.convergence.* intrinsics.
class ConvergenceControlInst : public IntrinsicInst {
public:
  static bool classof(const IntrinsicInst *I) {
    return isConvergenceControlIntrinsic(I->getIntrinsicID());
  }

  static bool classof(const Value *V) {
    return isa<IntrinsicInst>(V) && classof(cast<IntrinsicInst>(V));
  }

  bool isAnchor() {
    return getIntrinsicID() == Intrinsic::experimental_convergence_anchor;
  }
  bool isEntry() {
    return getIntrinsicID() == Intrinsic::experimental_convergence_entry;
  }
  bool isLoop() {
    return getIntrinsicID() == Intrinsic::experimental_convergence_loop;
  }
};

} // end namespace llvm

#endif // LLVM_IR_INTRINSICINST_H
