//===- VPlanValue.h - Represent Values in Vectorizer Plan -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declarations of the entities induced by Vectorization
/// Plans, e.g. the instructions the VPlan intends to generate if executed.
/// VPlan models the following entities:
/// VPValue   VPUser   VPDef
///    |        |
///   VPInstruction
/// These are documented in docs/VectorizationPlan.rst.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_VPLAN_VALUE_H
#define LLVM_TRANSFORMS_VECTORIZE_VPLAN_VALUE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/ADT/iterator_range.h"

namespace llvm {

// Forward declarations.
class raw_ostream;
class Value;
class VPDef;
class VPSlotTracker;
class VPUser;
class VPRecipeBase;

// This is the base class of the VPlan Def/Use graph, used for modeling the data
// flow into, within and out of the VPlan. VPValues can stand for live-ins
// coming from the input IR, instructions which VPlan will generate if executed
// and live-outs which the VPlan will need to fix accordingly.
class VPValue {
  friend class VPBuilder;
  friend class VPDef;
  friend class VPInstruction;
  friend struct VPlanTransforms;
  friend class VPBasicBlock;
  friend class VPInterleavedAccessInfo;
  friend class VPSlotTracker;
  friend class VPRecipeBase;

  const unsigned char SubclassID; ///< Subclass identifier (for isa/dyn_cast).

  SmallVector<VPUser *, 1> Users;

protected:
  // Hold the underlying Value, if any, attached to this VPValue.
  Value *UnderlyingVal;

  /// Pointer to the VPDef that defines this VPValue. If it is nullptr, the
  /// VPValue is not defined by any recipe modeled in VPlan.
  VPDef *Def;

  VPValue(const unsigned char SC, Value *UV = nullptr, VPDef *Def = nullptr);

  // DESIGN PRINCIPLE: Access to the underlying IR must be strictly limited to
  // the front-end and back-end of VPlan so that the middle-end is as
  // independent as possible of the underlying IR. We grant access to the
  // underlying IR using friendship. In that way, we should be able to use VPlan
  // for multiple underlying IRs (Polly?) by providing a new VPlan front-end,
  // back-end and analysis information for the new IR.

public:
  /// Return the underlying Value attached to this VPValue.
  Value *getUnderlyingValue() const { return UnderlyingVal; }

  /// An enumeration for keeping track of the concrete subclass of VPValue that
  /// are actually instantiated.
  enum {
    VPValueSC, /// A generic VPValue, like live-in values or defined by a recipe
               /// that defines multiple values.
    VPVRecipeSC /// A VPValue sub-class that is a VPRecipeBase.
  };

  /// Create a live-in VPValue.
  VPValue(Value *UV = nullptr) : VPValue(VPValueSC, UV, nullptr) {}
  /// Create a VPValue for a \p Def which is a subclass of VPValue.
  VPValue(VPDef *Def, Value *UV = nullptr) : VPValue(VPVRecipeSC, UV, Def) {}
  /// Create a VPValue for a \p Def which defines multiple values.
  VPValue(Value *UV, VPDef *Def) : VPValue(VPValueSC, UV, Def) {}
  VPValue(const VPValue &) = delete;
  VPValue &operator=(const VPValue &) = delete;

  virtual ~VPValue();

  /// \return an ID for the concrete type of this object.
  /// This is used to implement the classof checks. This should not be used
  /// for any other purpose, as the values may change as LLVM evolves.
  unsigned getVPValueID() const { return SubclassID; }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  void printAsOperand(raw_ostream &OS, VPSlotTracker &Tracker) const;
  void print(raw_ostream &OS, VPSlotTracker &Tracker) const;

  /// Dump the value to stderr (for debugging).
  void dump() const;
#endif

  unsigned getNumUsers() const { return Users.size(); }
  void addUser(VPUser &User) { Users.push_back(&User); }

  /// Remove a single \p User from the list of users.
  void removeUser(VPUser &User) {
    // The same user can be added multiple times, e.g. because the same VPValue
    // is used twice by the same VPUser. Remove a single one.
    auto *I = find(Users, &User);
    if (I != Users.end())
      Users.erase(I);
  }

  typedef SmallVectorImpl<VPUser *>::iterator user_iterator;
  typedef SmallVectorImpl<VPUser *>::const_iterator const_user_iterator;
  typedef iterator_range<user_iterator> user_range;
  typedef iterator_range<const_user_iterator> const_user_range;

  user_iterator user_begin() { return Users.begin(); }
  const_user_iterator user_begin() const { return Users.begin(); }
  user_iterator user_end() { return Users.end(); }
  const_user_iterator user_end() const { return Users.end(); }
  user_range users() { return user_range(user_begin(), user_end()); }
  const_user_range users() const {
    return const_user_range(user_begin(), user_end());
  }

  /// Returns true if the value has more than one unique user.
  bool hasMoreThanOneUniqueUser() {
    if (getNumUsers() == 0)
      return false;

    // Check if all users match the first user.
    auto Current = std::next(user_begin());
    while (Current != user_end() && *user_begin() == *Current)
      Current++;
    return Current != user_end();
  }

  void replaceAllUsesWith(VPValue *New);

  /// Go through the uses list for this VPValue and make each use point to \p
  /// New if the callback ShouldReplace returns true for the given use specified
  /// by a pair of (VPUser, the use index).
  void replaceUsesWithIf(
      VPValue *New,
      llvm::function_ref<bool(VPUser &U, unsigned Idx)> ShouldReplace);

  /// Returns the recipe defining this VPValue or nullptr if it is not defined
  /// by a recipe, i.e. is a live-in.
  VPRecipeBase *getDefiningRecipe();
  const VPRecipeBase *getDefiningRecipe() const;

  /// Returns true if this VPValue is defined by a recipe.
  bool hasDefiningRecipe() const { return getDefiningRecipe(); }

  /// Returns true if this VPValue is a live-in, i.e. defined outside the VPlan.
  bool isLiveIn() const { return !hasDefiningRecipe(); }

  /// Returns the underlying IR value, if this VPValue is defined outside the
  /// scope of VPlan. Returns nullptr if the VPValue is defined by a VPDef
  /// inside a VPlan.
  Value *getLiveInIRValue() {
    assert(isLiveIn() &&
           "VPValue is not a live-in; it is defined by a VPDef inside a VPlan");
    return getUnderlyingValue();
  }
  const Value *getLiveInIRValue() const {
    assert(isLiveIn() &&
           "VPValue is not a live-in; it is defined by a VPDef inside a VPlan");
    return getUnderlyingValue();
  }

  /// Returns true if the VPValue is defined outside any vector regions, i.e. it
  /// is a live-in value.
  /// TODO: Also handle recipes defined in pre-header blocks.
  bool isDefinedOutsideVectorRegions() const { return !hasDefiningRecipe(); }

  // Set \p Val as the underlying Value of this VPValue.
  void setUnderlyingValue(Value *Val) {
    assert(!UnderlyingVal && "Underlying Value is already set.");
    UnderlyingVal = Val;
  }
};

typedef DenseMap<Value *, VPValue *> Value2VPValueTy;
typedef DenseMap<VPValue *, Value *> VPValue2ValueTy;

raw_ostream &operator<<(raw_ostream &OS, const VPValue &V);

/// This class augments VPValue with operands which provide the inverse def-use
/// edges from VPValue's users to their defs.
class VPUser {
public:
  /// Subclass identifier (for isa/dyn_cast).
  enum class VPUserID {
    Recipe,
    LiveOut,
  };

private:
  SmallVector<VPValue *, 2> Operands;

  VPUserID ID;

protected:
#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Print the operands to \p O.
  void printOperands(raw_ostream &O, VPSlotTracker &SlotTracker) const;
#endif

  VPUser(ArrayRef<VPValue *> Operands, VPUserID ID) : ID(ID) {
    for (VPValue *Operand : Operands)
      addOperand(Operand);
  }

  VPUser(std::initializer_list<VPValue *> Operands, VPUserID ID)
      : VPUser(ArrayRef<VPValue *>(Operands), ID) {}

  template <typename IterT>
  VPUser(iterator_range<IterT> Operands, VPUserID ID) : ID(ID) {
    for (VPValue *Operand : Operands)
      addOperand(Operand);
  }

public:
  VPUser() = delete;
  VPUser(const VPUser &) = delete;
  VPUser &operator=(const VPUser &) = delete;
  virtual ~VPUser() {
    for (VPValue *Op : operands())
      Op->removeUser(*this);
  }

  VPUserID getVPUserID() const { return ID; }

  void addOperand(VPValue *Operand) {
    Operands.push_back(Operand);
    Operand->addUser(*this);
  }

  unsigned getNumOperands() const { return Operands.size(); }
  inline VPValue *getOperand(unsigned N) const {
    assert(N < Operands.size() && "Operand index out of bounds");
    return Operands[N];
  }

  void setOperand(unsigned I, VPValue *New) {
    Operands[I]->removeUser(*this);
    Operands[I] = New;
    New->addUser(*this);
  }

  typedef SmallVectorImpl<VPValue *>::iterator operand_iterator;
  typedef SmallVectorImpl<VPValue *>::const_iterator const_operand_iterator;
  typedef iterator_range<operand_iterator> operand_range;
  typedef iterator_range<const_operand_iterator> const_operand_range;

  operand_iterator op_begin() { return Operands.begin(); }
  const_operand_iterator op_begin() const { return Operands.begin(); }
  operand_iterator op_end() { return Operands.end(); }
  const_operand_iterator op_end() const { return Operands.end(); }
  operand_range operands() { return operand_range(op_begin(), op_end()); }
  const_operand_range operands() const {
    return const_operand_range(op_begin(), op_end());
  }

  /// Returns true if the VPUser uses scalars of operand \p Op. Conservatively
  /// returns if only first (scalar) lane is used, as default.
  virtual bool usesScalars(const VPValue *Op) const {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    return onlyFirstLaneUsed(Op);
  }

  /// Returns true if the VPUser only uses the first lane of operand \p Op.
  /// Conservatively returns false.
  virtual bool onlyFirstLaneUsed(const VPValue *Op) const {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    return false;
  }

  /// Returns true if the VPUser only uses the first part of operand \p Op.
  /// Conservatively returns false.
  virtual bool onlyFirstPartUsed(const VPValue *Op) const {
    assert(is_contained(operands(), Op) &&
           "Op must be an operand of the recipe");
    return false;
  }
};

/// This class augments a recipe with a set of VPValues defined by the recipe.
/// It allows recipes to define zero, one or multiple VPValues. A VPDef owns
/// the VPValues it defines and is responsible for deleting its defined values.
/// Single-value VPDefs that also inherit from VPValue must make sure to inherit
/// from VPDef before VPValue.
class VPDef {
  friend class VPValue;

  /// Subclass identifier (for isa/dyn_cast).
  const unsigned char SubclassID;

  /// The VPValues defined by this VPDef.
  TinyPtrVector<VPValue *> DefinedValues;

  /// Add \p V as a defined value by this VPDef.
  void addDefinedValue(VPValue *V) {
    assert(V->Def == this &&
           "can only add VPValue already linked with this VPDef");
    DefinedValues.push_back(V);
  }

  /// Remove \p V from the values defined by this VPDef. \p V must be a defined
  /// value of this VPDef.
  void removeDefinedValue(VPValue *V) {
    assert(V->Def == this && "can only remove VPValue linked with this VPDef");
    assert(is_contained(DefinedValues, V) &&
           "VPValue to remove must be in DefinedValues");
    llvm::erase(DefinedValues, V);
    V->Def = nullptr;
  }

public:
  /// An enumeration for keeping track of the concrete subclass of VPRecipeBase
  /// that is actually instantiated. Values of this enumeration are kept in the
  /// SubclassID field of the VPRecipeBase objects. They are used for concrete
  /// type identification.
  using VPRecipeTy = enum {
    VPBranchOnMaskSC,
    VPDerivedIVSC,
    VPExpandSCEVSC,
    VPInstructionSC,
    VPInterleaveSC,
    VPReductionEVLSC,
    VPReductionSC,
    VPReplicateSC,
    VPScalarCastSC,
    VPScalarIVStepsSC,
    VPVectorPointerSC,
    VPWidenCallSC,
    VPWidenCanonicalIVSC,
    VPWidenCastSC,
    VPWidenGEPSC,
    VPWidenLoadEVLSC,
    VPWidenLoadSC,
    VPWidenStoreEVLSC,
    VPWidenStoreSC,
    VPWidenSC,
    VPWidenSelectSC,
    VPBlendSC,
    // START: Phi-like recipes. Need to be kept together.
    VPWidenPHISC,
    VPPredInstPHISC,
    // START: SubclassID for recipes that inherit VPHeaderPHIRecipe.
    // VPHeaderPHIRecipe need to be kept together.
    VPCanonicalIVPHISC,
    VPActiveLaneMaskPHISC,
    VPEVLBasedIVPHISC,
    VPFirstOrderRecurrencePHISC,
    VPWidenIntOrFpInductionSC,
    VPWidenPointerInductionSC,
    VPReductionPHISC,
    // END: SubclassID for recipes that inherit VPHeaderPHIRecipe
    // END: Phi-like recipes
    VPFirstPHISC = VPWidenPHISC,
    VPFirstHeaderPHISC = VPCanonicalIVPHISC,
    VPLastHeaderPHISC = VPReductionPHISC,
    VPLastPHISC = VPReductionPHISC,
  };

  VPDef(const unsigned char SC) : SubclassID(SC) {}

  virtual ~VPDef() {
    for (VPValue *D : make_early_inc_range(DefinedValues)) {
      assert(D->Def == this &&
             "all defined VPValues should point to the containing VPDef");
      assert(D->getNumUsers() == 0 &&
             "all defined VPValues should have no more users");
      D->Def = nullptr;
      delete D;
    }
  }

  /// Returns the only VPValue defined by the VPDef. Can only be called for
  /// VPDefs with a single defined value.
  VPValue *getVPSingleValue() {
    assert(DefinedValues.size() == 1 && "must have exactly one defined value");
    assert(DefinedValues[0] && "defined value must be non-null");
    return DefinedValues[0];
  }
  const VPValue *getVPSingleValue() const {
    assert(DefinedValues.size() == 1 && "must have exactly one defined value");
    assert(DefinedValues[0] && "defined value must be non-null");
    return DefinedValues[0];
  }

  /// Returns the VPValue with index \p I defined by the VPDef.
  VPValue *getVPValue(unsigned I) {
    assert(DefinedValues[I] && "defined value must be non-null");
    return DefinedValues[I];
  }
  const VPValue *getVPValue(unsigned I) const {
    assert(DefinedValues[I] && "defined value must be non-null");
    return DefinedValues[I];
  }

  /// Returns an ArrayRef of the values defined by the VPDef.
  ArrayRef<VPValue *> definedValues() { return DefinedValues; }
  /// Returns an ArrayRef of the values defined by the VPDef.
  ArrayRef<VPValue *> definedValues() const { return DefinedValues; }

  /// Returns the number of values defined by the VPDef.
  unsigned getNumDefinedValues() const { return DefinedValues.size(); }

  /// \return an ID for the concrete type of this object.
  /// This is used to implement the classof checks. This should not be used
  /// for any other purpose, as the values may change as LLVM evolves.
  unsigned getVPDefID() const { return SubclassID; }

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
  /// Dump the VPDef to stderr (for debugging).
  void dump() const;

  /// Each concrete VPDef prints itself.
  virtual void print(raw_ostream &O, const Twine &Indent,
                     VPSlotTracker &SlotTracker) const = 0;
#endif
};

class VPlan;
class VPBasicBlock;

/// This class can be used to assign names to VPValues. For VPValues without
/// underlying value, assign consecutive numbers and use those as names (wrapped
/// in vp<>). Otherwise, use the name from the underlying value (wrapped in
/// ir<>), appending a .V version number if there are multiple uses of the same
/// name. Allows querying names for VPValues for printing, similar to the
/// ModuleSlotTracker for IR values.
class VPSlotTracker {
  /// Keep track of versioned names assigned to VPValues with underlying IR
  /// values.
  DenseMap<const VPValue *, std::string> VPValue2Name;
  /// Keep track of the next number to use to version the base name.
  StringMap<unsigned> BaseName2Version;

  /// Number to assign to the next VPValue without underlying value.
  unsigned NextSlot = 0;

  void assignName(const VPValue *V);
  void assignNames(const VPlan &Plan);
  void assignNames(const VPBasicBlock *VPBB);

public:
  VPSlotTracker(const VPlan *Plan = nullptr) {
    if (Plan)
      assignNames(*Plan);
  }

  /// Returns the name assigned to \p V, if there is one, otherwise try to
  /// construct one from the underlying value, if there's one; else return
  /// <badref>.
  std::string getOrCreateName(const VPValue *V) const;
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_VECTORIZE_VPLAN_VALUE_H
