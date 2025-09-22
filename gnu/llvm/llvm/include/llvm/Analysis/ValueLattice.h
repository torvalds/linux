//===- ValueLattice.h - Value constraint analysis ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_VALUELATTICE_H
#define LLVM_ANALYSIS_VALUELATTICE_H

#include "llvm/IR/Constants.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/Instructions.h"

//===----------------------------------------------------------------------===//
//                               ValueLatticeElement
//===----------------------------------------------------------------------===//

namespace llvm {

class Constant;

/// This class represents lattice values for constants.
///
/// FIXME: This is basically just for bringup, this can be made a lot more rich
/// in the future.
///
class ValueLatticeElement {
  enum ValueLatticeElementTy {
    /// This Value has no known value yet.  As a result, this implies the
    /// producing instruction is dead.  Caution: We use this as the starting
    /// state in our local meet rules.  In this usage, it's taken to mean
    /// "nothing known yet".
    /// Transition to any other state allowed.
    unknown,

    /// This Value is an UndefValue constant or produces undef. Undefined values
    /// can be merged with constants (or single element constant ranges),
    /// assuming all uses of the result will be replaced.
    /// Transition allowed to the following states:
    ///  constant
    ///  constantrange_including_undef
    ///  overdefined
    undef,

    /// This Value has a specific constant value.  The constant cannot be undef.
    /// (For constant integers, constantrange is used instead. Integer typed
    /// constantexprs can appear as constant.) Note that the constant state
    /// can be reached by merging undef & constant states.
    /// Transition allowed to the following states:
    ///  overdefined
    constant,

    /// This Value is known to not have the specified value. (For constant
    /// integers, constantrange is used instead.  As above, integer typed
    /// constantexprs can appear here.)
    /// Transition allowed to the following states:
    ///  overdefined
    notconstant,

    /// The Value falls within this range. (Used only for integer typed values.)
    /// Transition allowed to the following states:
    ///  constantrange (new range must be a superset of the existing range)
    ///  constantrange_including_undef
    ///  overdefined
    constantrange,

    /// This Value falls within this range, but also may be undef.
    /// Merging it with other constant ranges results in
    /// constantrange_including_undef.
    /// Transition allowed to the following states:
    ///  overdefined
    constantrange_including_undef,

    /// We can not precisely model the dynamic values this value might take.
    /// No transitions are allowed after reaching overdefined.
    overdefined,
  };

  ValueLatticeElementTy Tag : 8;
  /// Number of times a constant range has been extended with widening enabled.
  unsigned NumRangeExtensions : 8;

  /// The union either stores a pointer to a constant or a constant range,
  /// associated to the lattice element. We have to ensure that Range is
  /// initialized or destroyed when changing state to or from constantrange.
  union {
    Constant *ConstVal;
    ConstantRange Range;
  };

  /// Destroy contents of lattice value, without destructing the object.
  void destroy() {
    switch (Tag) {
    case overdefined:
    case unknown:
    case undef:
    case constant:
    case notconstant:
      break;
    case constantrange_including_undef:
    case constantrange:
      Range.~ConstantRange();
      break;
    };
  }

public:
  /// Struct to control some aspects related to merging constant ranges.
  struct MergeOptions {
    /// The merge value may include undef.
    bool MayIncludeUndef;

    /// Handle repeatedly extending a range by going to overdefined after a
    /// number of steps.
    bool CheckWiden;

    /// The number of allowed widening steps (including setting the range
    /// initially).
    unsigned MaxWidenSteps;

    MergeOptions() : MergeOptions(false, false) {}

    MergeOptions(bool MayIncludeUndef, bool CheckWiden,
                 unsigned MaxWidenSteps = 1)
        : MayIncludeUndef(MayIncludeUndef), CheckWiden(CheckWiden),
          MaxWidenSteps(MaxWidenSteps) {}

    MergeOptions &setMayIncludeUndef(bool V = true) {
      MayIncludeUndef = V;
      return *this;
    }

    MergeOptions &setCheckWiden(bool V = true) {
      CheckWiden = V;
      return *this;
    }

    MergeOptions &setMaxWidenSteps(unsigned Steps = 1) {
      CheckWiden = true;
      MaxWidenSteps = Steps;
      return *this;
    }
  };

  // ConstVal and Range are initialized on-demand.
  ValueLatticeElement() : Tag(unknown), NumRangeExtensions(0) {}

  ~ValueLatticeElement() { destroy(); }

  ValueLatticeElement(const ValueLatticeElement &Other)
      : Tag(Other.Tag), NumRangeExtensions(0) {
    switch (Other.Tag) {
    case constantrange:
    case constantrange_including_undef:
      new (&Range) ConstantRange(Other.Range);
      NumRangeExtensions = Other.NumRangeExtensions;
      break;
    case constant:
    case notconstant:
      ConstVal = Other.ConstVal;
      break;
    case overdefined:
    case unknown:
    case undef:
      break;
    }
  }

  ValueLatticeElement(ValueLatticeElement &&Other)
      : Tag(Other.Tag), NumRangeExtensions(0) {
    switch (Other.Tag) {
    case constantrange:
    case constantrange_including_undef:
      new (&Range) ConstantRange(std::move(Other.Range));
      NumRangeExtensions = Other.NumRangeExtensions;
      break;
    case constant:
    case notconstant:
      ConstVal = Other.ConstVal;
      break;
    case overdefined:
    case unknown:
    case undef:
      break;
    }
    Other.Tag = unknown;
  }

  ValueLatticeElement &operator=(const ValueLatticeElement &Other) {
    destroy();
    new (this) ValueLatticeElement(Other);
    return *this;
  }

  ValueLatticeElement &operator=(ValueLatticeElement &&Other) {
    destroy();
    new (this) ValueLatticeElement(std::move(Other));
    return *this;
  }

  static ValueLatticeElement get(Constant *C) {
    ValueLatticeElement Res;
    Res.markConstant(C);
    return Res;
  }
  static ValueLatticeElement getNot(Constant *C) {
    ValueLatticeElement Res;
    assert(!isa<UndefValue>(C) && "!= undef is not supported");
    Res.markNotConstant(C);
    return Res;
  }
  static ValueLatticeElement getRange(ConstantRange CR,
                                      bool MayIncludeUndef = false) {
    if (CR.isFullSet())
      return getOverdefined();

    if (CR.isEmptySet()) {
      ValueLatticeElement Res;
      if (MayIncludeUndef)
        Res.markUndef();
      return Res;
    }

    ValueLatticeElement Res;
    Res.markConstantRange(std::move(CR),
                          MergeOptions().setMayIncludeUndef(MayIncludeUndef));
    return Res;
  }
  static ValueLatticeElement getOverdefined() {
    ValueLatticeElement Res;
    Res.markOverdefined();
    return Res;
  }

  bool isUndef() const { return Tag == undef; }
  bool isUnknown() const { return Tag == unknown; }
  bool isUnknownOrUndef() const { return Tag == unknown || Tag == undef; }
  bool isConstant() const { return Tag == constant; }
  bool isNotConstant() const { return Tag == notconstant; }
  bool isConstantRangeIncludingUndef() const {
    return Tag == constantrange_including_undef;
  }
  /// Returns true if this value is a constant range. Use \p UndefAllowed to
  /// exclude non-singleton constant ranges that may also be undef. Note that
  /// this function also returns true if the range may include undef, but only
  /// contains a single element. In that case, it can be replaced by a constant.
  bool isConstantRange(bool UndefAllowed = true) const {
    return Tag == constantrange || (Tag == constantrange_including_undef &&
                                    (UndefAllowed || Range.isSingleElement()));
  }
  bool isOverdefined() const { return Tag == overdefined; }

  Constant *getConstant() const {
    assert(isConstant() && "Cannot get the constant of a non-constant!");
    return ConstVal;
  }

  Constant *getNotConstant() const {
    assert(isNotConstant() && "Cannot get the constant of a non-notconstant!");
    return ConstVal;
  }

  /// Returns the constant range for this value. Use \p UndefAllowed to exclude
  /// non-singleton constant ranges that may also be undef. Note that this
  /// function also returns a range if the range may include undef, but only
  /// contains a single element. In that case, it can be replaced by a constant.
  const ConstantRange &getConstantRange(bool UndefAllowed = true) const {
    assert(isConstantRange(UndefAllowed) &&
           "Cannot get the constant-range of a non-constant-range!");
    return Range;
  }

  std::optional<APInt> asConstantInteger() const {
    if (isConstant() && isa<ConstantInt>(getConstant())) {
      return cast<ConstantInt>(getConstant())->getValue();
    } else if (isConstantRange() && getConstantRange().isSingleElement()) {
      return *getConstantRange().getSingleElement();
    }
    return std::nullopt;
  }

  ConstantRange asConstantRange(unsigned BW, bool UndefAllowed = false) const {
    if (isConstantRange(UndefAllowed))
      return getConstantRange();
    if (isConstant())
      return getConstant()->toConstantRange();
    if (isUnknown())
      return ConstantRange::getEmpty(BW);
    return ConstantRange::getFull(BW);
  }

  ConstantRange asConstantRange(Type *Ty, bool UndefAllowed = false) const {
    assert(Ty->isIntOrIntVectorTy() && "Must be integer type");
    return asConstantRange(Ty->getScalarSizeInBits(), UndefAllowed);
  }

  bool markOverdefined() {
    if (isOverdefined())
      return false;
    destroy();
    Tag = overdefined;
    return true;
  }

  bool markUndef() {
    if (isUndef())
      return false;

    assert(isUnknown());
    Tag = undef;
    return true;
  }

  bool markConstant(Constant *V, bool MayIncludeUndef = false) {
    if (isa<UndefValue>(V))
      return markUndef();

    if (isConstant()) {
      assert(getConstant() == V && "Marking constant with different value");
      return false;
    }

    if (ConstantInt *CI = dyn_cast<ConstantInt>(V))
      return markConstantRange(
          ConstantRange(CI->getValue()),
          MergeOptions().setMayIncludeUndef(MayIncludeUndef));

    assert(isUnknown() || isUndef());
    Tag = constant;
    ConstVal = V;
    return true;
  }

  bool markNotConstant(Constant *V) {
    assert(V && "Marking constant with NULL");
    if (ConstantInt *CI = dyn_cast<ConstantInt>(V))
      return markConstantRange(
          ConstantRange(CI->getValue() + 1, CI->getValue()));

    if (isa<UndefValue>(V))
      return false;

    if (isNotConstant()) {
      assert(getNotConstant() == V && "Marking !constant with different value");
      return false;
    }

    assert(isUnknown());
    Tag = notconstant;
    ConstVal = V;
    return true;
  }

  /// Mark the object as constant range with \p NewR. If the object is already a
  /// constant range, nothing changes if the existing range is equal to \p
  /// NewR and the tag. Otherwise \p NewR must be a superset of the existing
  /// range or the object must be undef. The tag is set to
  /// constant_range_including_undef if either the existing value or the new
  /// range may include undef.
  bool markConstantRange(ConstantRange NewR,
                         MergeOptions Opts = MergeOptions()) {
    assert(!NewR.isEmptySet() && "should only be called for non-empty sets");

    if (NewR.isFullSet())
      return markOverdefined();

    ValueLatticeElementTy OldTag = Tag;
    ValueLatticeElementTy NewTag =
        (isUndef() || isConstantRangeIncludingUndef() || Opts.MayIncludeUndef)
            ? constantrange_including_undef
            : constantrange;
    if (isConstantRange()) {
      Tag = NewTag;
      if (getConstantRange() == NewR)
        return Tag != OldTag;

      // Simple form of widening. If a range is extended multiple times, go to
      // overdefined.
      if (Opts.CheckWiden && ++NumRangeExtensions > Opts.MaxWidenSteps)
        return markOverdefined();

      assert(NewR.contains(getConstantRange()) &&
             "Existing range must be a subset of NewR");
      Range = std::move(NewR);
      return true;
    }

    assert(isUnknown() || isUndef() || isConstant());
    assert((!isConstant() || NewR.contains(getConstant()->toConstantRange())) &&
           "Constant must be subset of new range");

    NumRangeExtensions = 0;
    Tag = NewTag;
    new (&Range) ConstantRange(std::move(NewR));
    return true;
  }

  /// Updates this object to approximate both this object and RHS. Returns
  /// true if this object has been changed.
  bool mergeIn(const ValueLatticeElement &RHS,
               MergeOptions Opts = MergeOptions()) {
    if (RHS.isUnknown() || isOverdefined())
      return false;
    if (RHS.isOverdefined()) {
      markOverdefined();
      return true;
    }

    if (isUndef()) {
      assert(!RHS.isUnknown());
      if (RHS.isUndef())
        return false;
      if (RHS.isConstant())
        return markConstant(RHS.getConstant(), true);
      if (RHS.isConstantRange())
        return markConstantRange(RHS.getConstantRange(true),
                                 Opts.setMayIncludeUndef());
      return markOverdefined();
    }

    if (isUnknown()) {
      assert(!RHS.isUnknown() && "Unknow RHS should be handled earlier");
      *this = RHS;
      return true;
    }

    if (isConstant()) {
      if (RHS.isConstant() && getConstant() == RHS.getConstant())
        return false;
      if (RHS.isUndef())
        return false;
      // If the constant is a vector of integers, try to treat it as a range.
      if (getConstant()->getType()->isVectorTy() &&
          getConstant()->getType()->getScalarType()->isIntegerTy()) {
        ConstantRange L = getConstant()->toConstantRange();
        ConstantRange NewR = L.unionWith(
            RHS.asConstantRange(L.getBitWidth(), /*UndefAllowed=*/true));
        return markConstantRange(
            std::move(NewR),
            Opts.setMayIncludeUndef(RHS.isConstantRangeIncludingUndef()));
      }
      markOverdefined();
      return true;
    }

    if (isNotConstant()) {
      if (RHS.isNotConstant() && getNotConstant() == RHS.getNotConstant())
        return false;
      markOverdefined();
      return true;
    }

    auto OldTag = Tag;
    assert(isConstantRange() && "New ValueLattice type?");
    if (RHS.isUndef()) {
      Tag = constantrange_including_undef;
      return OldTag != Tag;
    }

    const ConstantRange &L = getConstantRange();
    ConstantRange NewR = L.unionWith(
        RHS.asConstantRange(L.getBitWidth(), /*UndefAllowed=*/true));
    return markConstantRange(
        std::move(NewR),
        Opts.setMayIncludeUndef(RHS.isConstantRangeIncludingUndef()));
  }

  // Compares this symbolic value with Other using Pred and returns either
  /// true, false or undef constants, or nullptr if the comparison cannot be
  /// evaluated.
  Constant *getCompare(CmpInst::Predicate Pred, Type *Ty,
                       const ValueLatticeElement &Other,
                       const DataLayout &DL) const;

  unsigned getNumRangeExtensions() const { return NumRangeExtensions; }
  void setNumRangeExtensions(unsigned N) { NumRangeExtensions = N; }
};

static_assert(sizeof(ValueLatticeElement) <= 40,
              "size of ValueLatticeElement changed unexpectedly");

raw_ostream &operator<<(raw_ostream &OS, const ValueLatticeElement &Val);
} // end namespace llvm
#endif
