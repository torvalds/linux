//===- ValueHandle.h - Value Smart Pointer classes --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the ValueHandle class and its sub-classes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_VALUEHANDLE_H
#define LLVM_IR_VALUEHANDLE_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/IR/Value.h"
#include "llvm/Support/Casting.h"
#include <cassert>

namespace llvm {

/// This is the common base class of value handles.
///
/// ValueHandle's are smart pointers to Value's that have special behavior when
/// the value is deleted or ReplaceAllUsesWith'd.  See the specific handles
/// below for details.
class ValueHandleBase {
  friend class Value;

protected:
  /// This indicates what sub class the handle actually is.
  ///
  /// This is to avoid having a vtable for the light-weight handle pointers. The
  /// fully general Callback version does have a vtable.
  enum HandleBaseKind { Assert, Callback, Weak, WeakTracking };

  ValueHandleBase(const ValueHandleBase &RHS)
      : ValueHandleBase(RHS.PrevPair.getInt(), RHS) {}

  ValueHandleBase(HandleBaseKind Kind, const ValueHandleBase &RHS)
      : PrevPair(nullptr, Kind), Val(RHS.getValPtr()) {
    if (isValid(getValPtr()))
      AddToExistingUseList(RHS.getPrevPtr());
  }

private:
  PointerIntPair<ValueHandleBase**, 2, HandleBaseKind> PrevPair;
  ValueHandleBase *Next = nullptr;
  Value *Val = nullptr;

  void setValPtr(Value *V) { Val = V; }

public:
  explicit ValueHandleBase(HandleBaseKind Kind)
      : PrevPair(nullptr, Kind) {}
  ValueHandleBase(HandleBaseKind Kind, Value *V)
      : PrevPair(nullptr, Kind), Val(V) {
    if (isValid(getValPtr()))
      AddToUseList();
  }

  ~ValueHandleBase() {
    if (isValid(getValPtr()))
      RemoveFromUseList();
  }

  Value *operator=(Value *RHS) {
    if (getValPtr() == RHS)
      return RHS;
    if (isValid(getValPtr()))
      RemoveFromUseList();
    setValPtr(RHS);
    if (isValid(getValPtr()))
      AddToUseList();
    return RHS;
  }

  Value *operator=(const ValueHandleBase &RHS) {
    if (getValPtr() == RHS.getValPtr())
      return RHS.getValPtr();
    if (isValid(getValPtr()))
      RemoveFromUseList();
    setValPtr(RHS.getValPtr());
    if (isValid(getValPtr()))
      AddToExistingUseList(RHS.getPrevPtr());
    return getValPtr();
  }

  Value *operator->() const { return getValPtr(); }
  Value &operator*() const {
    Value *V = getValPtr();
    assert(V && "Dereferencing deleted ValueHandle");
    return *V;
  }

protected:
  Value *getValPtr() const { return Val; }

  static bool isValid(Value *V) {
    return V &&
           V != DenseMapInfo<Value *>::getEmptyKey() &&
           V != DenseMapInfo<Value *>::getTombstoneKey();
  }

  /// Remove this ValueHandle from its current use list.
  void RemoveFromUseList();

  /// Clear the underlying pointer without clearing the use list.
  ///
  /// This should only be used if a derived class has manually removed the
  /// handle from the use list.
  void clearValPtr() { setValPtr(nullptr); }

public:
  // Callbacks made from Value.
  static void ValueIsDeleted(Value *V);
  static void ValueIsRAUWd(Value *Old, Value *New);

private:
  // Internal implementation details.
  ValueHandleBase **getPrevPtr() const { return PrevPair.getPointer(); }
  HandleBaseKind getKind() const { return PrevPair.getInt(); }
  void setPrevPtr(ValueHandleBase **Ptr) { PrevPair.setPointer(Ptr); }

  /// Add this ValueHandle to the use list for V.
  ///
  /// List is the address of either the head of the list or a Next node within
  /// the existing use list.
  void AddToExistingUseList(ValueHandleBase **List);

  /// Add this ValueHandle to the use list after Node.
  void AddToExistingUseListAfter(ValueHandleBase *Node);

  /// Add this ValueHandle to the use list for V.
  void AddToUseList();
};

/// A nullable Value handle that is nullable.
///
/// This is a value handle that points to a value, and nulls itself
/// out if that value is deleted.
class WeakVH : public ValueHandleBase {
public:
  WeakVH() : ValueHandleBase(Weak) {}
  WeakVH(Value *P) : ValueHandleBase(Weak, P) {}
  WeakVH(const WeakVH &RHS)
      : ValueHandleBase(Weak, RHS) {}

  WeakVH &operator=(const WeakVH &RHS) = default;

  Value *operator=(Value *RHS) {
    return ValueHandleBase::operator=(RHS);
  }
  Value *operator=(const ValueHandleBase &RHS) {
    return ValueHandleBase::operator=(RHS);
  }

  operator Value*() const {
    return getValPtr();
  }
};

// Specialize simplify_type to allow WeakVH to participate in
// dyn_cast, isa, etc.
template <> struct simplify_type<WeakVH> {
  using SimpleType = Value *;

  static SimpleType getSimplifiedValue(WeakVH &WVH) { return WVH; }
};
template <> struct simplify_type<const WeakVH> {
  using SimpleType = Value *;

  static SimpleType getSimplifiedValue(const WeakVH &WVH) { return WVH; }
};

// Specialize DenseMapInfo to allow WeakVH to participate in DenseMap.
template <> struct DenseMapInfo<WeakVH> {
  static inline WeakVH getEmptyKey() {
    return WeakVH(DenseMapInfo<Value *>::getEmptyKey());
  }

  static inline WeakVH getTombstoneKey() {
    return WeakVH(DenseMapInfo<Value *>::getTombstoneKey());
  }

  static unsigned getHashValue(const WeakVH &Val) {
    return DenseMapInfo<Value *>::getHashValue(Val);
  }

  static bool isEqual(const WeakVH &LHS, const WeakVH &RHS) {
    return DenseMapInfo<Value *>::isEqual(LHS, RHS);
  }
};

/// Value handle that is nullable, but tries to track the Value.
///
/// This is a value handle that tries hard to point to a Value, even across
/// RAUW operations, but will null itself out if the value is destroyed.  this
/// is useful for advisory sorts of information, but should not be used as the
/// key of a map (since the map would have to rearrange itself when the pointer
/// changes).
class WeakTrackingVH : public ValueHandleBase {
public:
  WeakTrackingVH() : ValueHandleBase(WeakTracking) {}
  WeakTrackingVH(Value *P) : ValueHandleBase(WeakTracking, P) {}
  WeakTrackingVH(const WeakTrackingVH &RHS)
      : ValueHandleBase(WeakTracking, RHS) {}

  WeakTrackingVH &operator=(const WeakTrackingVH &RHS) = default;

  Value *operator=(Value *RHS) {
    return ValueHandleBase::operator=(RHS);
  }
  Value *operator=(const ValueHandleBase &RHS) {
    return ValueHandleBase::operator=(RHS);
  }

  operator Value*() const {
    return getValPtr();
  }

  bool pointsToAliveValue() const {
    return ValueHandleBase::isValid(getValPtr());
  }
};

// Specialize simplify_type to allow WeakTrackingVH to participate in
// dyn_cast, isa, etc.
template <> struct simplify_type<WeakTrackingVH> {
  using SimpleType = Value *;

  static SimpleType getSimplifiedValue(WeakTrackingVH &WVH) { return WVH; }
};
template <> struct simplify_type<const WeakTrackingVH> {
  using SimpleType = Value *;

  static SimpleType getSimplifiedValue(const WeakTrackingVH &WVH) {
    return WVH;
  }
};

/// Value handle that asserts if the Value is deleted.
///
/// This is a Value Handle that points to a value and asserts out if the value
/// is destroyed while the handle is still live.  This is very useful for
/// catching dangling pointer bugs and other things which can be non-obvious.
/// One particularly useful place to use this is as the Key of a map.  Dangling
/// pointer bugs often lead to really subtle bugs that only occur if another
/// object happens to get allocated to the same address as the old one.  Using
/// an AssertingVH ensures that an assert is triggered as soon as the bad
/// delete occurs.
///
/// Note that an AssertingVH handle does *not* follow values across RAUW
/// operations.  This means that RAUW's need to explicitly update the
/// AssertingVH's as it moves.  This is required because in non-assert mode this
/// class turns into a trivial wrapper around a pointer.
template <typename ValueTy>
class AssertingVH
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    : public ValueHandleBase
#endif
{
  friend struct DenseMapInfo<AssertingVH<ValueTy>>;

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  Value *getRawValPtr() const { return ValueHandleBase::getValPtr(); }
  void setRawValPtr(Value *P) { ValueHandleBase::operator=(P); }
#else
  Value *ThePtr;
  Value *getRawValPtr() const { return ThePtr; }
  void setRawValPtr(Value *P) { ThePtr = P; }
#endif
  // Convert a ValueTy*, which may be const, to the raw Value*.
  static Value *GetAsValue(Value *V) { return V; }
  static Value *GetAsValue(const Value *V) { return const_cast<Value*>(V); }

  ValueTy *getValPtr() const { return static_cast<ValueTy *>(getRawValPtr()); }
  void setValPtr(ValueTy *P) { setRawValPtr(GetAsValue(P)); }

public:
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  AssertingVH() : ValueHandleBase(Assert) {}
  AssertingVH(ValueTy *P) : ValueHandleBase(Assert, GetAsValue(P)) {}
  AssertingVH(const AssertingVH &RHS) : ValueHandleBase(Assert, RHS) {}
#else
  AssertingVH() : ThePtr(nullptr) {}
  AssertingVH(ValueTy *P) : ThePtr(GetAsValue(P)) {}
  AssertingVH(const AssertingVH &) = default;
#endif

  operator ValueTy*() const {
    return getValPtr();
  }

  ValueTy *operator=(ValueTy *RHS) {
    setValPtr(RHS);
    return getValPtr();
  }
  ValueTy *operator=(const AssertingVH<ValueTy> &RHS) {
    setValPtr(RHS.getValPtr());
    return getValPtr();
  }

  ValueTy *operator->() const { return getValPtr(); }
  ValueTy &operator*() const { return *getValPtr(); }
};

// Treat AssertingVH<T> like T* inside maps. This also allows using find_as()
// to look up a value without constructing a value handle.
template<typename T>
struct DenseMapInfo<AssertingVH<T>> : DenseMapInfo<T *> {};

/// Value handle that tracks a Value across RAUW.
///
/// TrackingVH is designed for situations where a client needs to hold a handle
/// to a Value (or subclass) across some operations which may move that value,
/// but should never destroy it or replace it with some unacceptable type.
///
/// It is an error to attempt to replace a value with one of a type which is
/// incompatible with any of its outstanding TrackingVHs.
///
/// It is an error to read from a TrackingVH that does not point to a valid
/// value.  A TrackingVH is said to not point to a valid value if either it
/// hasn't yet been assigned a value yet or because the value it was tracking
/// has since been deleted.
///
/// Assigning a value to a TrackingVH is always allowed, even if said TrackingVH
/// no longer points to a valid value.
template <typename ValueTy> class TrackingVH {
  WeakTrackingVH InnerHandle;

public:
  ValueTy *getValPtr() const {
    assert(InnerHandle.pointsToAliveValue() &&
           "TrackingVH must be non-null and valid on dereference!");

    // Check that the value is a member of the correct subclass. We would like
    // to check this property on assignment for better debugging, but we don't
    // want to require a virtual interface on this VH. Instead we allow RAUW to
    // replace this value with a value of an invalid type, and check it here.
    assert(isa<ValueTy>(InnerHandle) &&
           "Tracked Value was replaced by one with an invalid type!");
    return cast<ValueTy>(InnerHandle);
  }

  void setValPtr(ValueTy *P) {
    // Assigning to non-valid TrackingVH's are fine so we just unconditionally
    // assign here.
    InnerHandle = GetAsValue(P);
  }

  // Convert a ValueTy*, which may be const, to the type the base
  // class expects.
  static Value *GetAsValue(Value *V) { return V; }
  static Value *GetAsValue(const Value *V) { return const_cast<Value*>(V); }

public:
  TrackingVH() = default;
  TrackingVH(ValueTy *P) { setValPtr(P); }

  operator ValueTy*() const {
    return getValPtr();
  }

  ValueTy *operator=(ValueTy *RHS) {
    setValPtr(RHS);
    return getValPtr();
  }

  ValueTy *operator->() const { return getValPtr(); }
  ValueTy &operator*() const { return *getValPtr(); }
};

/// Value handle with callbacks on RAUW and destruction.
///
/// This is a value handle that allows subclasses to define callbacks that run
/// when the underlying Value has RAUW called on it or is destroyed.  This
/// class can be used as the key of a map, as long as the user takes it out of
/// the map before calling setValPtr() (since the map has to rearrange itself
/// when the pointer changes).  Unlike ValueHandleBase, this class has a vtable.
class CallbackVH : public ValueHandleBase {
  virtual void anchor();
protected:
  ~CallbackVH() = default;
  CallbackVH(const CallbackVH &) = default;
  CallbackVH &operator=(const CallbackVH &) = default;

  void setValPtr(Value *P) {
    ValueHandleBase::operator=(P);
  }

public:
  CallbackVH() : ValueHandleBase(Callback) {}
  CallbackVH(Value *P) : ValueHandleBase(Callback, P) {}
  CallbackVH(const Value *P) : CallbackVH(const_cast<Value *>(P)) {}

  operator Value*() const {
    return getValPtr();
  }

  /// Callback for Value destruction.
  ///
  /// Called when this->getValPtr() is destroyed, inside ~Value(), so you
  /// may call any non-virtual Value method on getValPtr(), but no subclass
  /// methods.  If WeakTrackingVH were implemented as a CallbackVH, it would use
  /// this
  /// method to call setValPtr(NULL).  AssertingVH would use this method to
  /// cause an assertion failure.
  ///
  /// All implementations must remove the reference from this object to the
  /// Value that's being destroyed.
  virtual void deleted() { setValPtr(nullptr); }

  /// Callback for Value RAUW.
  ///
  /// Called when this->getValPtr()->replaceAllUsesWith(new_value) is called,
  /// _before_ any of the uses have actually been replaced.  If WeakTrackingVH
  /// were
  /// implemented as a CallbackVH, it would use this method to call
  /// setValPtr(new_value).  AssertingVH would do nothing in this method.
  virtual void allUsesReplacedWith(Value *) {}
};

/// Value handle that poisons itself if the Value is deleted.
///
/// This is a Value Handle that points to a value and poisons itself if the
/// value is destroyed while the handle is still live.  This is very useful for
/// catching dangling pointer bugs where an \c AssertingVH cannot be used
/// because the dangling handle needs to outlive the value without ever being
/// used.
///
/// One particularly useful place to use this is as the Key of a map. Dangling
/// pointer bugs often lead to really subtle bugs that only occur if another
/// object happens to get allocated to the same address as the old one. Using
/// a PoisoningVH ensures that an assert is triggered if looking up a new value
/// in the map finds a handle from the old value.
///
/// Note that a PoisoningVH handle does *not* follow values across RAUW
/// operations. This means that RAUW's need to explicitly update the
/// PoisoningVH's as it moves. This is required because in non-assert mode this
/// class turns into a trivial wrapper around a pointer.
template <typename ValueTy>
class PoisoningVH final
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    : public CallbackVH
#endif
{
  friend struct DenseMapInfo<PoisoningVH<ValueTy>>;

  // Convert a ValueTy*, which may be const, to the raw Value*.
  static Value *GetAsValue(Value *V) { return V; }
  static Value *GetAsValue(const Value *V) { return const_cast<Value *>(V); }

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  /// A flag tracking whether this value has been poisoned.
  ///
  /// On delete and RAUW, we leave the value pointer alone so that as a raw
  /// pointer it produces the same value (and we fit into the same key of
  /// a hash table, etc), but we poison the handle so that any top-level usage
  /// will fail.
  bool Poisoned = false;

  Value *getRawValPtr() const { return ValueHandleBase::getValPtr(); }
  void setRawValPtr(Value *P) { ValueHandleBase::operator=(P); }

  /// Handle deletion by poisoning the handle.
  void deleted() override {
    assert(!Poisoned && "Tried to delete an already poisoned handle!");
    Poisoned = true;
    RemoveFromUseList();
  }

  /// Handle RAUW by poisoning the handle.
  void allUsesReplacedWith(Value *) override {
    assert(!Poisoned && "Tried to RAUW an already poisoned handle!");
    Poisoned = true;
    RemoveFromUseList();
  }
#else // LLVM_ENABLE_ABI_BREAKING_CHECKS
  Value *ThePtr = nullptr;

  Value *getRawValPtr() const { return ThePtr; }
  void setRawValPtr(Value *P) { ThePtr = P; }
#endif

  ValueTy *getValPtr() const {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    assert(!Poisoned && "Accessed a poisoned value handle!");
#endif
    return static_cast<ValueTy *>(getRawValPtr());
  }
  void setValPtr(ValueTy *P) { setRawValPtr(GetAsValue(P)); }

public:
  PoisoningVH() = default;
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  PoisoningVH(ValueTy *P) : CallbackVH(GetAsValue(P)) {}
  PoisoningVH(const PoisoningVH &RHS)
      : CallbackVH(RHS), Poisoned(RHS.Poisoned) {}

  ~PoisoningVH() {
    if (Poisoned)
      clearValPtr();
  }

  PoisoningVH &operator=(const PoisoningVH &RHS) {
    if (Poisoned)
      clearValPtr();
    CallbackVH::operator=(RHS);
    Poisoned = RHS.Poisoned;
    return *this;
  }
#else
  PoisoningVH(ValueTy *P) : ThePtr(GetAsValue(P)) {}
#endif

  operator ValueTy *() const { return getValPtr(); }

  ValueTy *operator->() const { return getValPtr(); }
  ValueTy &operator*() const { return *getValPtr(); }
};

// Specialize DenseMapInfo to allow PoisoningVH to participate in DenseMap.
template <typename T> struct DenseMapInfo<PoisoningVH<T>> {
  static inline PoisoningVH<T> getEmptyKey() {
    PoisoningVH<T> Res;
    Res.setRawValPtr(DenseMapInfo<Value *>::getEmptyKey());
    return Res;
  }

  static inline PoisoningVH<T> getTombstoneKey() {
    PoisoningVH<T> Res;
    Res.setRawValPtr(DenseMapInfo<Value *>::getTombstoneKey());
    return Res;
  }

  static unsigned getHashValue(const PoisoningVH<T> &Val) {
    return DenseMapInfo<Value *>::getHashValue(Val.getRawValPtr());
  }

  static bool isEqual(const PoisoningVH<T> &LHS, const PoisoningVH<T> &RHS) {
    return DenseMapInfo<Value *>::isEqual(LHS.getRawValPtr(),
                                          RHS.getRawValPtr());
  }

  // Allow lookup by T* via find_as(), without constructing a temporary
  // value handle.

  static unsigned getHashValue(const T *Val) {
    return DenseMapInfo<Value *>::getHashValue(Val);
  }

  static bool isEqual(const T *LHS, const PoisoningVH<T> &RHS) {
    return DenseMapInfo<Value *>::isEqual(LHS, RHS.getRawValPtr());
  }
};

} // end namespace llvm

#endif // LLVM_IR_VALUEHANDLE_H
